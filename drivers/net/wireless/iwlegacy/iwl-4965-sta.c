/******************************************************************************
 *
 * Copyright(c) 2003 - 2011 Intel Corporation. All rights reserved.
 *
 * Portions of this file are derived from the ipw3945 project, as well
 * as portions of the ieee80211 subsystem header files.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 *****************************************************************************/

#include <net/mac80211.h>

#include "iwl-dev.h"
#include "iwl-core.h"
#include "iwl-sta.h"
#include "iwl-4965.h"

static struct il_link_quality_cmd *
il4965_sta_alloc_lq(struct il_priv *il, u8 sta_id)
{
	int i, r;
	struct il_link_quality_cmd *link_cmd;
	u32 rate_flags = 0;
	__le32 rate_n_flags;

	link_cmd = kzalloc(sizeof(struct il_link_quality_cmd), GFP_KERNEL);
	if (!link_cmd) {
		IL_ERR("Unable to allocate memory for LQ cmd.\n");
		return NULL;
	}
	/* Set up the rate scaling to start at selected rate, fall back
	 * all the way down to 1M in IEEE order, and then spin on 1M */
	if (il->band == IEEE80211_BAND_5GHZ)
		r = RATE_6M_INDEX;
	else
		r = RATE_1M_INDEX;

	if (r >= IL_FIRST_CCK_RATE && r <= IL_LAST_CCK_RATE)
		rate_flags |= RATE_MCS_CCK_MSK;

	rate_flags |= il4965_first_antenna(il->hw_params.valid_tx_ant) <<
				RATE_MCS_ANT_POS;
	rate_n_flags = il4965_hw_set_rate_n_flags(il_rates[r].plcp,
						   rate_flags);
	for (i = 0; i < LINK_QUAL_MAX_RETRY_NUM; i++)
		link_cmd->rs_table[i].rate_n_flags = rate_n_flags;

	link_cmd->general_params.single_stream_ant_msk =
				il4965_first_antenna(il->hw_params.valid_tx_ant);

	link_cmd->general_params.dual_stream_ant_msk =
		il->hw_params.valid_tx_ant &
		~il4965_first_antenna(il->hw_params.valid_tx_ant);
	if (!link_cmd->general_params.dual_stream_ant_msk) {
		link_cmd->general_params.dual_stream_ant_msk = ANT_AB;
	} else if (il4965_num_of_ant(il->hw_params.valid_tx_ant) == 2) {
		link_cmd->general_params.dual_stream_ant_msk =
			il->hw_params.valid_tx_ant;
	}

	link_cmd->agg_params.agg_dis_start_th = LINK_QUAL_AGG_DISABLE_START_DEF;
	link_cmd->agg_params.agg_time_limit =
		cpu_to_le16(LINK_QUAL_AGG_TIME_LIMIT_DEF);

	link_cmd->sta_id = sta_id;

	return link_cmd;
}

/*
 * il4965_add_bssid_station - Add the special IBSS BSSID station
 *
 * Function sleeps.
 */
int
il4965_add_bssid_station(struct il_priv *il, struct il_rxon_context *ctx,
			     const u8 *addr, u8 *sta_id_r)
{
	int ret;
	u8 sta_id;
	struct il_link_quality_cmd *link_cmd;
	unsigned long flags;

	if (sta_id_r)
		*sta_id_r = IL_INVALID_STATION;

	ret = il_add_station_common(il, ctx, addr, 0, NULL, &sta_id);
	if (ret) {
		IL_ERR("Unable to add station %pM\n", addr);
		return ret;
	}

	if (sta_id_r)
		*sta_id_r = sta_id;

	spin_lock_irqsave(&il->sta_lock, flags);
	il->stations[sta_id].used |= IL_STA_LOCAL;
	spin_unlock_irqrestore(&il->sta_lock, flags);

	/* Set up default rate scaling table in device's station table */
	link_cmd = il4965_sta_alloc_lq(il, sta_id);
	if (!link_cmd) {
		IL_ERR(
			"Unable to initialize rate scaling for station %pM.\n",
			addr);
		return -ENOMEM;
	}

	ret = il_send_lq_cmd(il, ctx, link_cmd, CMD_SYNC, true);
	if (ret)
		IL_ERR("Link quality command failed (%d)\n", ret);

	spin_lock_irqsave(&il->sta_lock, flags);
	il->stations[sta_id].lq = link_cmd;
	spin_unlock_irqrestore(&il->sta_lock, flags);

	return 0;
}

static int il4965_static_wepkey_cmd(struct il_priv *il,
				      struct il_rxon_context *ctx,
				      bool send_if_empty)
{
	int i, not_empty = 0;
	u8 buff[sizeof(struct il_wep_cmd) +
		sizeof(struct il_wep_key) * WEP_KEYS_MAX];
	struct il_wep_cmd *wep_cmd = (struct il_wep_cmd *)buff;
	size_t cmd_size  = sizeof(struct il_wep_cmd);
	struct il_host_cmd cmd = {
		.id = ctx->wep_key_cmd,
		.data = wep_cmd,
		.flags = CMD_SYNC,
	};

	might_sleep();

	memset(wep_cmd, 0, cmd_size +
			(sizeof(struct il_wep_key) * WEP_KEYS_MAX));

	for (i = 0; i < WEP_KEYS_MAX ; i++) {
		wep_cmd->key[i].key_index = i;
		if (ctx->wep_keys[i].key_size) {
			wep_cmd->key[i].key_offset = i;
			not_empty = 1;
		} else {
			wep_cmd->key[i].key_offset = WEP_INVALID_OFFSET;
		}

		wep_cmd->key[i].key_size = ctx->wep_keys[i].key_size;
		memcpy(&wep_cmd->key[i].key[3], ctx->wep_keys[i].key,
				ctx->wep_keys[i].key_size);
	}

	wep_cmd->global_key_type = WEP_KEY_WEP_TYPE;
	wep_cmd->num_keys = WEP_KEYS_MAX;

	cmd_size += sizeof(struct il_wep_key) * WEP_KEYS_MAX;

	cmd.len = cmd_size;

	if (not_empty || send_if_empty)
		return il_send_cmd(il, &cmd);
	else
		return 0;
}

int il4965_restore_default_wep_keys(struct il_priv *il,
				 struct il_rxon_context *ctx)
{
	lockdep_assert_held(&il->mutex);

	return il4965_static_wepkey_cmd(il, ctx, false);
}

int il4965_remove_default_wep_key(struct il_priv *il,
			       struct il_rxon_context *ctx,
			       struct ieee80211_key_conf *keyconf)
{
	int ret;

	lockdep_assert_held(&il->mutex);

	D_WEP("Removing default WEP key: idx=%d\n",
		      keyconf->keyidx);

	memset(&ctx->wep_keys[keyconf->keyidx], 0, sizeof(ctx->wep_keys[0]));
	if (il_is_rfkill(il)) {
		D_WEP(
		"Not sending REPLY_WEPKEY command due to RFKILL.\n");
		/* but keys in device are clear anyway so return success */
		return 0;
	}
	ret = il4965_static_wepkey_cmd(il, ctx, 1);
	D_WEP("Remove default WEP key: idx=%d ret=%d\n",
		      keyconf->keyidx, ret);

	return ret;
}

int il4965_set_default_wep_key(struct il_priv *il,
			    struct il_rxon_context *ctx,
			    struct ieee80211_key_conf *keyconf)
{
	int ret;

	lockdep_assert_held(&il->mutex);

	if (keyconf->keylen != WEP_KEY_LEN_128 &&
	    keyconf->keylen != WEP_KEY_LEN_64) {
		D_WEP("Bad WEP key length %d\n", keyconf->keylen);
		return -EINVAL;
	}

	keyconf->flags &= ~IEEE80211_KEY_FLAG_GENERATE_IV;
	keyconf->hw_key_idx = HW_KEY_DEFAULT;
	il->stations[ctx->ap_sta_id].keyinfo.cipher = keyconf->cipher;

	ctx->wep_keys[keyconf->keyidx].key_size = keyconf->keylen;
	memcpy(&ctx->wep_keys[keyconf->keyidx].key, &keyconf->key,
							keyconf->keylen);

	ret = il4965_static_wepkey_cmd(il, ctx, false);
	D_WEP("Set default WEP key: len=%d idx=%d ret=%d\n",
		keyconf->keylen, keyconf->keyidx, ret);

	return ret;
}

static int il4965_set_wep_dynamic_key_info(struct il_priv *il,
					struct il_rxon_context *ctx,
					struct ieee80211_key_conf *keyconf,
					u8 sta_id)
{
	unsigned long flags;
	__le16 key_flags = 0;
	struct il_addsta_cmd sta_cmd;

	lockdep_assert_held(&il->mutex);

	keyconf->flags &= ~IEEE80211_KEY_FLAG_GENERATE_IV;

	key_flags |= (STA_KEY_FLG_WEP | STA_KEY_FLG_MAP_KEY_MSK);
	key_flags |= cpu_to_le16(keyconf->keyidx << STA_KEY_FLG_KEYID_POS);
	key_flags &= ~STA_KEY_FLG_INVALID;

	if (keyconf->keylen == WEP_KEY_LEN_128)
		key_flags |= STA_KEY_FLG_KEY_SIZE_MSK;

	if (sta_id == ctx->bcast_sta_id)
		key_flags |= STA_KEY_MULTICAST_MSK;

	spin_lock_irqsave(&il->sta_lock, flags);

	il->stations[sta_id].keyinfo.cipher = keyconf->cipher;
	il->stations[sta_id].keyinfo.keylen = keyconf->keylen;
	il->stations[sta_id].keyinfo.keyidx = keyconf->keyidx;

	memcpy(il->stations[sta_id].keyinfo.key,
				keyconf->key, keyconf->keylen);

	memcpy(&il->stations[sta_id].sta.key.key[3],
				keyconf->key, keyconf->keylen);

	if ((il->stations[sta_id].sta.key.key_flags & STA_KEY_FLG_ENCRYPT_MSK)
			== STA_KEY_FLG_NO_ENC)
		il->stations[sta_id].sta.key.key_offset =
				 il_get_free_ucode_key_index(il);
	/* else, we are overriding an existing key => no need to allocated room
	 * in uCode. */

	WARN(il->stations[sta_id].sta.key.key_offset == WEP_INVALID_OFFSET,
		"no space for a new key");

	il->stations[sta_id].sta.key.key_flags = key_flags;
	il->stations[sta_id].sta.sta.modify_mask = STA_MODIFY_KEY_MASK;
	il->stations[sta_id].sta.mode = STA_CONTROL_MODIFY_MSK;

	memcpy(&sta_cmd, &il->stations[sta_id].sta,
			sizeof(struct il_addsta_cmd));
	spin_unlock_irqrestore(&il->sta_lock, flags);

	return il_send_add_sta(il, &sta_cmd, CMD_SYNC);
}

static int il4965_set_ccmp_dynamic_key_info(struct il_priv *il,
					 struct il_rxon_context *ctx,
					 struct ieee80211_key_conf *keyconf,
					 u8 sta_id)
{
	unsigned long flags;
	__le16 key_flags = 0;
	struct il_addsta_cmd sta_cmd;

	lockdep_assert_held(&il->mutex);

	key_flags |= (STA_KEY_FLG_CCMP | STA_KEY_FLG_MAP_KEY_MSK);
	key_flags |= cpu_to_le16(keyconf->keyidx << STA_KEY_FLG_KEYID_POS);
	key_flags &= ~STA_KEY_FLG_INVALID;

	if (sta_id == ctx->bcast_sta_id)
		key_flags |= STA_KEY_MULTICAST_MSK;

	keyconf->flags |= IEEE80211_KEY_FLAG_GENERATE_IV;

	spin_lock_irqsave(&il->sta_lock, flags);
	il->stations[sta_id].keyinfo.cipher = keyconf->cipher;
	il->stations[sta_id].keyinfo.keylen = keyconf->keylen;

	memcpy(il->stations[sta_id].keyinfo.key, keyconf->key,
	       keyconf->keylen);

	memcpy(il->stations[sta_id].sta.key.key, keyconf->key,
	       keyconf->keylen);

	if ((il->stations[sta_id].sta.key.key_flags & STA_KEY_FLG_ENCRYPT_MSK)
			== STA_KEY_FLG_NO_ENC)
		il->stations[sta_id].sta.key.key_offset =
				 il_get_free_ucode_key_index(il);
	/* else, we are overriding an existing key => no need to allocated room
	 * in uCode. */

	WARN(il->stations[sta_id].sta.key.key_offset == WEP_INVALID_OFFSET,
		"no space for a new key");

	il->stations[sta_id].sta.key.key_flags = key_flags;
	il->stations[sta_id].sta.sta.modify_mask = STA_MODIFY_KEY_MASK;
	il->stations[sta_id].sta.mode = STA_CONTROL_MODIFY_MSK;

	memcpy(&sta_cmd, &il->stations[sta_id].sta,
			 sizeof(struct il_addsta_cmd));
	spin_unlock_irqrestore(&il->sta_lock, flags);

	return il_send_add_sta(il, &sta_cmd, CMD_SYNC);
}

static int il4965_set_tkip_dynamic_key_info(struct il_priv *il,
					 struct il_rxon_context *ctx,
					 struct ieee80211_key_conf *keyconf,
					 u8 sta_id)
{
	unsigned long flags;
	int ret = 0;
	__le16 key_flags = 0;

	key_flags |= (STA_KEY_FLG_TKIP | STA_KEY_FLG_MAP_KEY_MSK);
	key_flags |= cpu_to_le16(keyconf->keyidx << STA_KEY_FLG_KEYID_POS);
	key_flags &= ~STA_KEY_FLG_INVALID;

	if (sta_id == ctx->bcast_sta_id)
		key_flags |= STA_KEY_MULTICAST_MSK;

	keyconf->flags |= IEEE80211_KEY_FLAG_GENERATE_IV;
	keyconf->flags |= IEEE80211_KEY_FLAG_GENERATE_MMIC;

	spin_lock_irqsave(&il->sta_lock, flags);

	il->stations[sta_id].keyinfo.cipher = keyconf->cipher;
	il->stations[sta_id].keyinfo.keylen = 16;

	if ((il->stations[sta_id].sta.key.key_flags & STA_KEY_FLG_ENCRYPT_MSK)
			== STA_KEY_FLG_NO_ENC)
		il->stations[sta_id].sta.key.key_offset =
				 il_get_free_ucode_key_index(il);
	/* else, we are overriding an existing key => no need to allocated room
	 * in uCode. */

	WARN(il->stations[sta_id].sta.key.key_offset == WEP_INVALID_OFFSET,
		"no space for a new key");

	il->stations[sta_id].sta.key.key_flags = key_flags;


	/* This copy is acutally not needed: we get the key with each TX */
	memcpy(il->stations[sta_id].keyinfo.key, keyconf->key, 16);

	memcpy(il->stations[sta_id].sta.key.key, keyconf->key, 16);

	spin_unlock_irqrestore(&il->sta_lock, flags);

	return ret;
}

void il4965_update_tkip_key(struct il_priv *il,
			 struct il_rxon_context *ctx,
			 struct ieee80211_key_conf *keyconf,
			 struct ieee80211_sta *sta, u32 iv32, u16 *phase1key)
{
	u8 sta_id;
	unsigned long flags;
	int i;

	if (il_scan_cancel(il)) {
		/* cancel scan failed, just live w/ bad key and rely
		   briefly on SW decryption */
		return;
	}

	sta_id = il_sta_id_or_broadcast(il, ctx, sta);
	if (sta_id == IL_INVALID_STATION)
		return;

	spin_lock_irqsave(&il->sta_lock, flags);

	il->stations[sta_id].sta.key.tkip_rx_tsc_byte2 = (u8) iv32;

	for (i = 0; i < 5; i++)
		il->stations[sta_id].sta.key.tkip_rx_ttak[i] =
			cpu_to_le16(phase1key[i]);

	il->stations[sta_id].sta.sta.modify_mask = STA_MODIFY_KEY_MASK;
	il->stations[sta_id].sta.mode = STA_CONTROL_MODIFY_MSK;

	il_send_add_sta(il, &il->stations[sta_id].sta, CMD_ASYNC);

	spin_unlock_irqrestore(&il->sta_lock, flags);

}

int il4965_remove_dynamic_key(struct il_priv *il,
			   struct il_rxon_context *ctx,
			   struct ieee80211_key_conf *keyconf,
			   u8 sta_id)
{
	unsigned long flags;
	u16 key_flags;
	u8 keyidx;
	struct il_addsta_cmd sta_cmd;

	lockdep_assert_held(&il->mutex);

	ctx->key_mapping_keys--;

	spin_lock_irqsave(&il->sta_lock, flags);
	key_flags = le16_to_cpu(il->stations[sta_id].sta.key.key_flags);
	keyidx = (key_flags >> STA_KEY_FLG_KEYID_POS) & 0x3;

	D_WEP("Remove dynamic key: idx=%d sta=%d\n",
		      keyconf->keyidx, sta_id);

	if (keyconf->keyidx != keyidx) {
		/* We need to remove a key with index different that the one
		 * in the uCode. This means that the key we need to remove has
		 * been replaced by another one with different index.
		 * Don't do anything and return ok
		 */
		spin_unlock_irqrestore(&il->sta_lock, flags);
		return 0;
	}

	if (il->stations[sta_id].sta.key.key_offset == WEP_INVALID_OFFSET) {
		IL_WARN("Removing wrong key %d 0x%x\n",
			    keyconf->keyidx, key_flags);
		spin_unlock_irqrestore(&il->sta_lock, flags);
		return 0;
	}

	if (!test_and_clear_bit(il->stations[sta_id].sta.key.key_offset,
		&il->ucode_key_table))
		IL_ERR("index %d not used in uCode key table.\n",
			il->stations[sta_id].sta.key.key_offset);
	memset(&il->stations[sta_id].keyinfo, 0,
					sizeof(struct il_hw_key));
	memset(&il->stations[sta_id].sta.key, 0,
					sizeof(struct il4965_keyinfo));
	il->stations[sta_id].sta.key.key_flags =
			STA_KEY_FLG_NO_ENC | STA_KEY_FLG_INVALID;
	il->stations[sta_id].sta.key.key_offset = WEP_INVALID_OFFSET;
	il->stations[sta_id].sta.sta.modify_mask = STA_MODIFY_KEY_MASK;
	il->stations[sta_id].sta.mode = STA_CONTROL_MODIFY_MSK;

	if (il_is_rfkill(il)) {
		D_WEP(
		 "Not sending REPLY_ADD_STA command because RFKILL enabled.\n");
		spin_unlock_irqrestore(&il->sta_lock, flags);
		return 0;
	}
	memcpy(&sta_cmd, &il->stations[sta_id].sta,
			sizeof(struct il_addsta_cmd));
	spin_unlock_irqrestore(&il->sta_lock, flags);

	return il_send_add_sta(il, &sta_cmd, CMD_SYNC);
}

int il4965_set_dynamic_key(struct il_priv *il, struct il_rxon_context *ctx,
			struct ieee80211_key_conf *keyconf, u8 sta_id)
{
	int ret;

	lockdep_assert_held(&il->mutex);

	ctx->key_mapping_keys++;
	keyconf->hw_key_idx = HW_KEY_DYNAMIC;

	switch (keyconf->cipher) {
	case WLAN_CIPHER_SUITE_CCMP:
		ret = il4965_set_ccmp_dynamic_key_info(il, ctx,
							keyconf, sta_id);
		break;
	case WLAN_CIPHER_SUITE_TKIP:
		ret = il4965_set_tkip_dynamic_key_info(il, ctx,
							keyconf, sta_id);
		break;
	case WLAN_CIPHER_SUITE_WEP40:
	case WLAN_CIPHER_SUITE_WEP104:
		ret = il4965_set_wep_dynamic_key_info(il, ctx,
							keyconf, sta_id);
		break;
	default:
		IL_ERR(
			"Unknown alg: %s cipher = %x\n", __func__,
			keyconf->cipher);
		ret = -EINVAL;
	}

	D_WEP(
		"Set dynamic key: cipher=%x len=%d idx=%d sta=%d ret=%d\n",
		      keyconf->cipher, keyconf->keylen, keyconf->keyidx,
		      sta_id, ret);

	return ret;
}

/**
 * il4965_alloc_bcast_station - add broadcast station into driver's station table.
 *
 * This adds the broadcast station into the driver's station table
 * and marks it driver active, so that it will be restored to the
 * device at the next best time.
 */
int il4965_alloc_bcast_station(struct il_priv *il,
			       struct il_rxon_context *ctx)
{
	struct il_link_quality_cmd *link_cmd;
	unsigned long flags;
	u8 sta_id;

	spin_lock_irqsave(&il->sta_lock, flags);
	sta_id = il_prep_station(il, ctx, il_bcast_addr,
								false, NULL);
	if (sta_id == IL_INVALID_STATION) {
		IL_ERR("Unable to prepare broadcast station\n");
		spin_unlock_irqrestore(&il->sta_lock, flags);

		return -EINVAL;
	}

	il->stations[sta_id].used |= IL_STA_DRIVER_ACTIVE;
	il->stations[sta_id].used |= IL_STA_BCAST;
	spin_unlock_irqrestore(&il->sta_lock, flags);

	link_cmd = il4965_sta_alloc_lq(il, sta_id);
	if (!link_cmd) {
		IL_ERR(
			"Unable to initialize rate scaling for bcast station.\n");
		return -ENOMEM;
	}

	spin_lock_irqsave(&il->sta_lock, flags);
	il->stations[sta_id].lq = link_cmd;
	spin_unlock_irqrestore(&il->sta_lock, flags);

	return 0;
}

/**
 * il4965_update_bcast_station - update broadcast station's LQ command
 *
 * Only used by iwl4965. Placed here to have all bcast station management
 * code together.
 */
static int il4965_update_bcast_station(struct il_priv *il,
				    struct il_rxon_context *ctx)
{
	unsigned long flags;
	struct il_link_quality_cmd *link_cmd;
	u8 sta_id = ctx->bcast_sta_id;

	link_cmd = il4965_sta_alloc_lq(il, sta_id);
	if (!link_cmd) {
		IL_ERR(
		"Unable to initialize rate scaling for bcast station.\n");
		return -ENOMEM;
	}

	spin_lock_irqsave(&il->sta_lock, flags);
	if (il->stations[sta_id].lq)
		kfree(il->stations[sta_id].lq);
	else
		D_INFO(
		"Bcast station rate scaling has not been initialized yet.\n");
	il->stations[sta_id].lq = link_cmd;
	spin_unlock_irqrestore(&il->sta_lock, flags);

	return 0;
}

int il4965_update_bcast_stations(struct il_priv *il)
{
	struct il_rxon_context *ctx;
	int ret = 0;

	for_each_context(il, ctx) {
		ret = il4965_update_bcast_station(il, ctx);
		if (ret)
			break;
	}

	return ret;
}

/**
 * il4965_sta_tx_modify_enable_tid - Enable Tx for this TID in station table
 */
int il4965_sta_tx_modify_enable_tid(struct il_priv *il, int sta_id, int tid)
{
	unsigned long flags;
	struct il_addsta_cmd sta_cmd;

	lockdep_assert_held(&il->mutex);

	/* Remove "disable" flag, to enable Tx for this TID */
	spin_lock_irqsave(&il->sta_lock, flags);
	il->stations[sta_id].sta.sta.modify_mask = STA_MODIFY_TID_DISABLE_TX;
	il->stations[sta_id].sta.tid_disable_tx &= cpu_to_le16(~(1 << tid));
	il->stations[sta_id].sta.mode = STA_CONTROL_MODIFY_MSK;
	memcpy(&sta_cmd, &il->stations[sta_id].sta,
					sizeof(struct il_addsta_cmd));
	spin_unlock_irqrestore(&il->sta_lock, flags);

	return il_send_add_sta(il, &sta_cmd, CMD_SYNC);
}

int il4965_sta_rx_agg_start(struct il_priv *il, struct ieee80211_sta *sta,
			 int tid, u16 ssn)
{
	unsigned long flags;
	int sta_id;
	struct il_addsta_cmd sta_cmd;

	lockdep_assert_held(&il->mutex);

	sta_id = il_sta_id(sta);
	if (sta_id == IL_INVALID_STATION)
		return -ENXIO;

	spin_lock_irqsave(&il->sta_lock, flags);
	il->stations[sta_id].sta.station_flags_msk = 0;
	il->stations[sta_id].sta.sta.modify_mask = STA_MODIFY_ADDBA_TID_MSK;
	il->stations[sta_id].sta.add_immediate_ba_tid = (u8)tid;
	il->stations[sta_id].sta.add_immediate_ba_ssn = cpu_to_le16(ssn);
	il->stations[sta_id].sta.mode = STA_CONTROL_MODIFY_MSK;
	memcpy(&sta_cmd, &il->stations[sta_id].sta,
					sizeof(struct il_addsta_cmd));
	spin_unlock_irqrestore(&il->sta_lock, flags);

	return il_send_add_sta(il, &sta_cmd, CMD_SYNC);
}

int il4965_sta_rx_agg_stop(struct il_priv *il, struct ieee80211_sta *sta,
			int tid)
{
	unsigned long flags;
	int sta_id;
	struct il_addsta_cmd sta_cmd;

	lockdep_assert_held(&il->mutex);

	sta_id = il_sta_id(sta);
	if (sta_id == IL_INVALID_STATION) {
		IL_ERR("Invalid station for AGG tid %d\n", tid);
		return -ENXIO;
	}

	spin_lock_irqsave(&il->sta_lock, flags);
	il->stations[sta_id].sta.station_flags_msk = 0;
	il->stations[sta_id].sta.sta.modify_mask = STA_MODIFY_DELBA_TID_MSK;
	il->stations[sta_id].sta.remove_immediate_ba_tid = (u8)tid;
	il->stations[sta_id].sta.mode = STA_CONTROL_MODIFY_MSK;
	memcpy(&sta_cmd, &il->stations[sta_id].sta,
				sizeof(struct il_addsta_cmd));
	spin_unlock_irqrestore(&il->sta_lock, flags);

	return il_send_add_sta(il, &sta_cmd, CMD_SYNC);
}

void
il4965_sta_modify_sleep_tx_count(struct il_priv *il, int sta_id, int cnt)
{
	unsigned long flags;

	spin_lock_irqsave(&il->sta_lock, flags);
	il->stations[sta_id].sta.station_flags |= STA_FLG_PWR_SAVE_MSK;
	il->stations[sta_id].sta.station_flags_msk = STA_FLG_PWR_SAVE_MSK;
	il->stations[sta_id].sta.sta.modify_mask =
					STA_MODIFY_SLEEP_TX_COUNT_MSK;
	il->stations[sta_id].sta.sleep_tx_count = cpu_to_le16(cnt);
	il->stations[sta_id].sta.mode = STA_CONTROL_MODIFY_MSK;
	il_send_add_sta(il,
				&il->stations[sta_id].sta, CMD_ASYNC);
	spin_unlock_irqrestore(&il->sta_lock, flags);

}
