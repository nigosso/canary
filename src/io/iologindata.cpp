/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019-2024 OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "pch.hpp"

#include "io/iologindata.hpp"
#include "io/functions/iologindata_load_player.hpp"
#include "io/functions/iologindata_save_player.hpp"
#include "creatures/monsters/monster.hpp"
#include "creatures/players/wheel/player_wheel.hpp"
#include "lib/metrics/metrics.hpp"
#include "enums/account_type.hpp"
#include "enums/account_errors.hpp"

bool IOLoginData::gameWorldAuthentication(const std::string &accountDescriptor, const std::string &password, std::string &characterName, uint32_t &accountId, bool oldProtocol) {
	Account account(accountDescriptor);
	account.setProtocolCompat(oldProtocol);

	if (AccountErrors_t::Ok != enumFromValue<AccountErrors_t>(account.load())) {
		g_logger().error("Couldn't load account [{}].", account.getDescriptor());
		return false;
	}

	if (g_configManager().getString(AUTH_TYPE, __FUNCTION__) == "session") {
		if (!account.authenticate()) {
			return false;
		}
	} else {
		if (!account.authenticate(password)) {
			return false;
		}
	}

	if (AccountErrors_t::Ok != enumFromValue<AccountErrors_t>(account.load())) {
		g_logger().error("Failed to load account [{}]", accountDescriptor);
		return false;
	}

	auto [players, result] = account.getAccountPlayers();
	if (AccountErrors_t::Ok != enumFromValue<AccountErrors_t>(result)) {
		g_logger().error("Failed to load account [{}] players", accountDescriptor);
		return false;
	}

	if (players[characterName].deletion != 0) {
		g_logger().error("Account [{}] player [{}] not found or deleted.", accountDescriptor, characterName);
		return false;
	}

	accountId = account.getID();

	return true;
}

uint8_t IOLoginData::getAccountType(uint32_t accountId) {
	std::ostringstream query;
	query << "SELECT `type` FROM `accounts` WHERE `id` = " << accountId;
	DBResult_ptr result = Database::getInstance().storeQuery(query.str());
	if (!result) {
		return ACCOUNT_TYPE_NORMAL;
	}

	return result->getNumber<uint8_t>("type");
}

void IOLoginData::updateOnlineStatus(uint32_t guid, bool login) {
	static phmap::flat_hash_map<uint32_t, bool> updateOnline;
	if ((login && updateOnline.find(guid) != updateOnline.end()) || guid <= 0) {
		return;
	}

	const auto worldId = g_game().worlds()->getId();

	std::string query;
	if (login) {
		g_metrics().addUpDownCounter("players_online", 1);
		query = fmt::format("INSERT INTO `players_online` VALUES ({}, {})", guid, worldId);
		updateOnline[guid] = true;
	} else {
		g_metrics().addUpDownCounter("players_online", -1);
		query = fmt::format("DELETE FROM `players_online` WHERE `player_id` = {} AND `world_id` = {}", guid, worldId);
		updateOnline.erase(guid);
	}
	Database::getInstance().executeQuery(query);
}

// The boolean "disableIrrelevantInfo" will deactivate the loading of information that is not relevant to the preload, for example, forge, bosstiary, etc. None of this we need to access if the player is offline
bool IOLoginData::loadPlayerById(std::shared_ptr<Player> player, uint32_t id, bool disableIrrelevantInfo /* = true*/) {
	Database &db = Database::getInstance();
	std::string query = fmt::format("SELECT * FROM `players` WHERE `id` = {} AND `world_id` = {}", id, g_game().worlds()->getId());
	return loadPlayer(player, db.storeQuery(query), disableIrrelevantInfo);
}

bool IOLoginData::loadPlayerByName(std::shared_ptr<Player> player, const std::string &name, bool disableIrrelevantInfo /* = true*/) {
	Database &db = Database::getInstance();
	std::string query = fmt::format("SELECT * FROM `players` WHERE `name` = {} AND `world_id` = {}", db.escapeString(name), g_game().worlds()->getId());
	return loadPlayer(player, db.storeQuery(query), disableIrrelevantInfo);
}

bool IOLoginData::loadPlayer(std::shared_ptr<Player> player, DBResult_ptr result, bool disableIrrelevantInfo /* = false*/) {
	if (!result || !player) {
		std::string nullptrType = !result ? "Result" : "Player";
		g_logger().warn("[{}] - {} is nullptr", __FUNCTION__, nullptrType);
		return false;
	}

	try {
		// First
		IOLoginDataLoad::loadPlayerFirst(player, result);

		// Experience load
		IOLoginDataLoad::loadPlayerExperience(player, result);

		// Blessings load
		IOLoginDataLoad::loadPlayerBlessings(player, result);

		// load conditions
		IOLoginDataLoad::loadPlayerConditions(player, result);

		// load default outfit
		IOLoginDataLoad::loadPlayerDefaultOutfit(player, result);

		// skull system load
		IOLoginDataLoad::loadPlayerSkullSystem(player, result);

		// skill load
		IOLoginDataLoad::loadPlayerSkill(player, result);

		// kills load
		IOLoginDataLoad::loadPlayerKills(player, result);

		// guild load
		IOLoginDataLoad::loadPlayerGuild(player, result);

		// stash load items
		IOLoginDataLoad::loadPlayerStashItems(player, result);

		// bestiary charms
		IOLoginDataLoad::loadPlayerBestiaryCharms(player, result);

		// load inventory items
		IOLoginDataLoad::loadPlayerInventoryItems(player, result);

		// store Inbox
		IOLoginDataLoad::loadPlayerStoreInbox(player);

		// load depot items
		IOLoginDataLoad::loadPlayerDepotItems(player, result);

		// load reward items
		IOLoginDataLoad::loadRewardItems(player);

		// load inbox items
		IOLoginDataLoad::loadPlayerInboxItems(player, result);

		// load storage map
		IOLoginDataLoad::loadPlayerStorageMap(player, result);

		// load vip
		IOLoginDataLoad::loadPlayerVip(player, result);

		// load prey class
		IOLoginDataLoad::loadPlayerPreyClass(player, result);

		// Load task hunting class
		IOLoginDataLoad::loadPlayerTaskHuntingClass(player, result);

		// Load instant spells list
		IOLoginDataLoad::loadPlayerInstantSpellList(player, result);

		if (disableIrrelevantInfo) {
			return true;
		}

		// load forge history
		IOLoginDataLoad::loadPlayerForgeHistory(player, result);

		// load bosstiary
		IOLoginDataLoad::loadPlayerBosstiary(player, result);

		IOLoginDataLoad::loadPlayerInitializeSystem(player);
		IOLoginDataLoad::loadPlayerUpdateSystem(player);

		return true;
	} catch (const std::system_error &error) {
		g_logger().warn("[{}] Error while load player: {}", __FUNCTION__, error.what());
		return false;
	} catch (const std::exception &e) {
		g_logger().warn("[{}] Error while load player: {}", __FUNCTION__, e.what());
		return false;
	}
}

bool IOLoginData::savePlayer(std::shared_ptr<Player> player) {
	bool success = DBTransaction::executeWithinTransaction([player]() {
		return savePlayerGuard(player);
	});

	if (!success) {
		g_logger().error("[{}] Error occurred saving player", __FUNCTION__);
	}

	return success;
}

bool IOLoginData::savePlayerGuard(std::shared_ptr<Player> player) {
	if (!player) {
		throw DatabaseException("Player nullptr in function: " + std::string(__FUNCTION__));
	}

	if (!IOLoginDataSave::savePlayerFirst(player)) {
		throw DatabaseException("[" + std::string(__FUNCTION__) + "] - Failed to save player first: " + player->getName());
	}

	if (!IOLoginDataSave::savePlayerStash(player)) {
		throw DatabaseException("[IOLoginDataSave::savePlayerFirst] - Failed to save player stash: " + player->getName());
	}

	if (!IOLoginDataSave::savePlayerSpells(player)) {
		throw DatabaseException("[IOLoginDataSave::savePlayerSpells] - Failed to save player spells: " + player->getName());
	}

	if (!IOLoginDataSave::savePlayerKills(player)) {
		throw DatabaseException("IOLoginDataSave::savePlayerKills] - Failed to save player kills: " + player->getName());
	}

	if (!IOLoginDataSave::savePlayerBestiarySystem(player)) {
		throw DatabaseException("[IOLoginDataSave::savePlayerBestiarySystem] - Failed to save player bestiary system: " + player->getName());
	}

	if (!IOLoginDataSave::savePlayerItem(player)) {
		throw DatabaseException("[IOLoginDataSave::savePlayerItem] - Failed to save player item: " + player->getName());
	}

	if (!IOLoginDataSave::savePlayerDepotItems(player)) {
		throw DatabaseException("[IOLoginDataSave::savePlayerDepotItems] - Failed to save player depot items: " + player->getName());
	}

	if (!IOLoginDataSave::saveRewardItems(player)) {
		throw DatabaseException("[IOLoginDataSave::saveRewardItems] - Failed to save player reward items: " + player->getName());
	}

	if (!IOLoginDataSave::savePlayerInbox(player)) {
		throw DatabaseException("[IOLoginDataSave::savePlayerInbox] - Failed to save player inbox: " + player->getName());
	}

	if (!IOLoginDataSave::savePlayerPreyClass(player)) {
		throw DatabaseException("[IOLoginDataSave::savePlayerPreyClass] - Failed to save player prey class: " + player->getName());
	}

	if (!IOLoginDataSave::savePlayerTaskHuntingClass(player)) {
		throw DatabaseException("[IOLoginDataSave::savePlayerTaskHuntingClass] - Failed to save player task hunting class: " + player->getName());
	}

	if (!IOLoginDataSave::savePlayerForgeHistory(player)) {
		throw DatabaseException("[IOLoginDataSave::savePlayerForgeHistory] - Failed to save player forge history: " + player->getName());
	}

	if (!IOLoginDataSave::savePlayerBosstiary(player)) {
		throw DatabaseException("[IOLoginDataSave::savePlayerBosstiary] - Failed to save player bosstiary: " + player->getName());
	}

	if (!player->wheel()->saveDBPlayerSlotPointsOnLogout()) {
		throw DatabaseException("[PlayerWheel::saveDBPlayerSlotPointsOnLogout] - Failed to save player wheel info: " + player->getName());
	}

	if (!IOLoginDataSave::savePlayerStorage(player)) {
		throw DatabaseException("[IOLoginDataSave::savePlayerStorage] - Failed to save player storage: " + player->getName());
	}

	return true;
}

std::string IOLoginData::getNameByGuid(uint32_t guid) {
	std::string query = fmt::format("SELECT `name` FROM `players` WHERE `id` = {} AND `world_id` = {}", guid, g_game().worlds()->getId());
	DBResult_ptr result = Database::getInstance().storeQuery(query);
	if (!result) {
		return std::string();
	}
	return result->getString("name");
}

uint32_t IOLoginData::getGuidByName(const std::string &name) {
	Database &db = Database::getInstance();

	std::string query = fmt::format("SELECT `id` FROM `players` WHERE `name` = {} AND `world_id` = {}", db.escapeString(name), g_game().worlds()->getId());
	DBResult_ptr result = db.storeQuery(query);
	if (!result) {
		return 0;
	}
	return result->getNumber<uint32_t>("id");
}

bool IOLoginData::getGuidByNameEx(uint32_t &guid, bool &specialVip, std::string &name) {
	Database &db = Database::getInstance();

	std::string query = fmt::format("SELECT `name`, `id`, `group_id`, `account_id` FROM `players` WHERE `name` = {} AND `world_id` = {}", db.escapeString(name), g_game().worlds()->getId());
	DBResult_ptr result = db.storeQuery(query);
	if (!result) {
		return false;
	}

	name = result->getString("name");
	guid = result->getNumber<uint32_t>("id");
	if (auto group = g_game().groups.getGroup(result->getNumber<uint16_t>("group_id"))) {
		specialVip = group->flags[Groups::getFlagNumber(PlayerFlags_t::SpecialVIP)];
	} else {
		specialVip = false;
	}
	return true;
}

bool IOLoginData::formatPlayerName(std::string &name) {
	Database &db = Database::getInstance();

	std::string query = fmt::format("SELECT `name` FROM `players` WHERE `name` = {} AND `world_id` = {}", db.escapeString(name), g_game().worlds()->getId());

	DBResult_ptr result = db.storeQuery(query);
	if (!result) {
		return false;
	}

	name = result->getString("name");
	return true;
}

void IOLoginData::increaseBankBalance(uint32_t guid, uint64_t bankBalance) {
	std::string query = fmt::format("UPDATE `players` SET `balance` = `balance` + {} WHERE `id` = {} AND `world_id` = {}", bankBalance, guid, g_game().worlds()->getId());
	Database::getInstance().executeQuery(query);
}

bool IOLoginData::hasBiddedOnHouse(uint32_t guid) {
	Database &db = Database::getInstance();

	std::string query = fmt::format("SELECT `id` FROM `houses` WHERE `highest_bidder` = {} AND `world_id` = {} LIMIT 1", guid, g_game().worlds()->getId());
	return db.storeQuery(query).get() != nullptr;
}

std::vector<VIPEntry> IOLoginData::getVIPEntries(uint32_t accountId) {
	std::string query = fmt::format(
		"SELECT `player_id`, (SELECT `name` FROM `players` WHERE `id` = `player_id`) AS `name`, `description`, `icon`, `notify` FROM `account_viplist` WHERE `account_id` = {} AND `world_id` = {}",
		accountId, g_game().worlds()->getId()
	);
	std::vector<VIPEntry> entries;

	if (const auto &result = Database::getInstance().storeQuery(query)) {
		entries.reserve(result->countResults());
		do {
			entries.emplace_back(
				result->getNumber<uint32_t>("player_id"),
				result->getString("name"),
				result->getString("description"),
				result->getNumber<uint32_t>("icon"),
				result->getNumber<uint16_t>("notify") != 0
			);
		} while (result->next());
	}

	return entries;
}

void IOLoginData::addVIPEntry(uint32_t accountId, uint32_t guid, const std::string &description, uint32_t icon, bool notify) {
	std::string query = fmt::format(
		"INSERT INTO `account_viplist` (`account_id`, `player_id`, `world_id`, `description`, `icon`, `notify`) VALUES ({}, {}, {}, {}, {}, {})",
		accountId, guid, g_game().worlds()->getId(), g_database().escapeString(description), icon, notify
	);
	if (!g_database().executeQuery(query)) {
		g_logger().error("Failed to add VIP entry for account {}. QUERY: {}", accountId, query.c_str());
	}
}

void IOLoginData::editVIPEntry(uint32_t accountId, uint32_t guid, const std::string &description, uint32_t icon, bool notify) {
	std::string query = fmt::format(
		"UPDATE `account_viplist` SET `description` = {}, `icon` = {}, `notify` = {} WHERE `account_id` = {} AND `player_id` = {} AND `world_id` = {}",
		g_database().escapeString(description), icon, notify, accountId, guid, g_game().worlds()->getId()
	);
	if (!g_database().executeQuery(query)) {
		g_logger().error("Failed to edit VIP entry for account {}. QUERY: {}", accountId, query.c_str());
	}
}

void IOLoginData::removeVIPEntry(uint32_t accountId, uint32_t guid) {
	std::string query = fmt::format("DELETE FROM `account_viplist` WHERE `account_id` = {} AND `player_id` = {} AND `world_id` = {}", accountId, guid, g_game().worlds()->getId());
	g_database().executeQuery(query);
}

std::vector<VIPGroupEntry> IOLoginData::getVIPGroupEntries(uint32_t accountId, uint32_t guid) {
	std::string query = fmt::format("SELECT `id`, `name`, `customizable` FROM `account_vipgroups` WHERE `account_id` = {}", accountId);

	std::vector<VIPGroupEntry> entries;

	if (const auto &result = g_database().storeQuery(query)) {
		entries.reserve(result->countResults());

		do {
			entries.emplace_back(
				result->getNumber<uint8_t>("id"),
				result->getString("name"),
				result->getNumber<uint8_t>("customizable") == 0 ? false : true
			);
		} while (result->next());
	}
	return entries;
}

void IOLoginData::addVIPGroupEntry(uint8_t groupId, uint32_t accountId, const std::string &groupName, bool customizable) {
	std::string query = fmt::format("INSERT INTO `account_vipgroups` (`id`, `account_id`, `name`, `customizable`) VALUES ({}, {}, {}, {})", groupId, accountId, g_database().escapeString(groupName), customizable);
	if (!g_database().executeQuery(query)) {
		g_logger().error("Failed to add VIP Group entry for account {} and group {}. QUERY: {}", accountId, groupId, query.c_str());
	}
}

void IOLoginData::editVIPGroupEntry(uint8_t groupId, uint32_t accountId, const std::string &groupName, bool customizable) {
	std::string query = fmt::format("UPDATE `account_vipgroups` SET `name` = {}, `customizable` = {} WHERE `id` = {} AND `account_id` = {}", g_database().escapeString(groupName), customizable, groupId, accountId);
	if (!g_database().executeQuery(query)) {
		g_logger().error("Failed to update VIP Group entry for account {} and group {}. QUERY: {}", accountId, groupId, query.c_str());
	}
}

void IOLoginData::removeVIPGroupEntry(uint8_t groupId, uint32_t accountId) {
	std::string query = fmt::format("DELETE FROM `account_vipgroups` WHERE `id` = {} AND `account_id` = {}", groupId, accountId);
	g_database().executeQuery(query);
}

void IOLoginData::addGuidVIPGroupEntry(uint8_t groupId, uint32_t accountId, uint32_t guid) {
	std::string query = fmt::format("INSERT INTO `account_vipgrouplist` (`account_id`, `player_id`, `vipgroup_id`) VALUES ({}, {}, {})", accountId, guid, groupId);
	if (!g_database().executeQuery(query)) {
		g_logger().error("Failed to add guid VIP Group entry for account {}, player {} and group {}. QUERY: {}", accountId, guid, groupId, query.c_str());
	}
}

void IOLoginData::removeGuidVIPGroupEntry(uint32_t accountId, uint32_t guid) {
	std::string query = fmt::format("DELETE FROM `account_vipgrouplist` WHERE `account_id` = {} AND `player_id` = {}", accountId, guid);
	g_database().executeQuery(query);
}

void IOLoginData::createFirstWorld() {
	const auto &result = g_database().storeQuery("SELECT * FROM `worlds`");

	if (result.get() == nullptr || result->countResults() < 1) {
		const auto &retro = g_configManager().getBoolean(TOGGLE_SERVER_IS_RETRO, __FUNCTION__) ? "retro-" : "";
		const auto &serverName = g_configManager().getString(SERVER_NAME, __FUNCTION__);
		const auto &worldType = fmt::format("{}{}", retro, g_configManager().getString(WORLD_TYPE, __FUNCTION__));
		const auto &worldMotd = g_configManager().getString(SERVER_MOTD, __FUNCTION__);
		const auto &location = g_configManager().getString(WORLD_LOCATION, __FUNCTION__);
		const auto &ip = g_configManager().getString(IP, __FUNCTION__);
		const auto &port = g_configManager().getNumber(GAME_PORT, __FUNCTION__);

		std::string query = fmt::format(
			"INSERT INTO `worlds` (`name`, `type`, `motd`, `location`, `ip`, `port`, `creation`) VALUES ({}, {}, {}, {}, {}, {}, {})",
			g_database().escapeString(serverName), g_database().escapeString(worldType), g_database().escapeString(worldMotd), g_database().escapeString(location), g_database().escapeString(ip), port, getTimeNow()
		);
		const auto &insertResult = g_database().executeQuery(query);

		if (insertResult) {
			g_logger().info("Added initial world id 1 - {} to database", serverName);
		} else {
			g_logger().error("Failed to add initial world id 1 - {} to database", serverName);
		}
	}
}

std::vector<std::shared_ptr<World>> IOLoginData::loadWorlds() {
	std::vector<std::shared_ptr<World>> entries;

	if (const auto &result = Database::getInstance().storeQuery("SELECT * FROM `worlds`")) {
		entries.reserve(result->countResults());
		do {
			entries.emplace_back(std::make_shared<World>(
				result->getNumber<uint8_t>("id"),
				result->getString("name"),
				g_game().worlds()->getTypeByString(result->getString("type")),
				result->getString("motd"),
				g_game().worlds()->getLocationCode(result->getString("location")),
				result->getString("ip"),
				result->getNumber<uint16_t>("port"),
				result->getNumber<uint16_t>("creation")
			));
		} while (result->next());
	}

	return entries;
}
