/******************************************************************************
 *
 * Copyright(c) 2003 - 2010 Intel Corporation. All rights reserved.
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
#include "iwl-agn.h"

static struct iwl_link_quality_cmd *
iwl_sta_alloc_lq(struct iwl_priv *priv, u8 sta_id)
{
	int i, r;
	struct iwl_link_quality_cmd *link_cmd;
	u32 rate_flags = 0;
	__le32 rate_n_flags;

	link_cmd = kzalloc(sizeof(struct iwl_link_quality_cmd), GFP_KERNEL);
	if (!link_cmd) {
		IWL_ERR(priv, "Unable to allocate memory for LQ cmd.\n");
		return NULL;
	}
	/* Set up the rate scaling to start at selected rate, fall back
	 * all the way down to 1M in IEEE order, and then spin on 1M */
	if (priv->band == IEEE80211_BAND_5GHZ)
		r = IWL_RATE_6M_INDEX;
	else
		r = IWL_RATE_1M_INDEX;

	if (r >= IWL_FIRST_CCK_RATE && r <= IWL_LAST_CCK_RATE)
		rate_flags |= RATE_MCS_CCK_MSK;

	rate_flags |= first_antenna(priv->hw_params.valid_tx_ant) <<
				RATE_MCS_ANT_POS;
	rate_n_flags = iwl_hw_set_rate_n_flags(iwl_rates[r].plcp, rate_flags);
	for (i = 0; i < LINK_QUAL_MAX_RETRY_NUM; i++)
		link_cmd->rs_table[i].rate_n_flags = rate_n_flags;

	link_cmd->general_params.single_stream_ant_msk =
				first_antenna(priv->hw_params.valid_tx_ant);

	link_cmd->general_params.dual_stream_ant_msk =
		priv->hw_params.valid_tx_ant &
		~first_antenna(priv->hw_params.valid_tx_ant);
	if (!link_cmd->general_params.dual_stream_ant_msk) {
		link_cmd->general_params.dual_stream_ant_msk = ANT_AB;
	} else if (num_of_ant(priv->hw_params.valid_tx_ant) == 2) {
		link_cmd->general_params.dual_stream_ant_msk =
			priv->hw_params.valid_tx_ant;
	}

	link_cmd->agg_params.agg_dis_start_th = LINK_QUAL_AGG_DISABLE_START_DEF;
	link_cmd->agg_params.agg_time_limit =
		cpu_to_le16(LINK_QUAL_AGG_TIME_LIMIT_DEF);

	link_cmd->sta_id = sta_id;

	return link_cmd;
}

/*
 * iwlagn_add_bssid_station - Add the special IBSS BSSID station
 *
 * Function sleeps.
 */
int iwlagn_add_bssid_station(struct iwl_priv *priv, struct iwl_rxon_context *ctx,
			     const u8 *addr, u8 *sta_id_r)
{
	int ret;
	u8 sta_id;
	struct iwl_link_quality_cmd *link_cmd;
	unsigned long flags;

	if (sta_id_r)
		*sta_id_r = IWL_INVALID_STATION;

	ret = iwl_add_station_common(priv, ctx, addr, 0, NULL, &sta_id);
	if (ret) {
		IWL_ERR(priv, "Unable to add station %pM\n", addr);
		return ret;
	}

	if (sta_id_r)
		*sta_id_r = sta_id;

	spin_lock_irqsave(&priv->sta_lock, flags);
	priv->stations[sta_id].used |= IWL_STA_LOCAL;
	spin_unlock_irqrestore(&priv->sta_lock, flags);

	/* Set up default rate scaling table in device's station table */
	link_cmd = iwl_sta_alloc_lq(priv, sta_id);
	if (!link_cmd) {
		IWL_ERR(priv, "Unable to initialize rate scaling for station %pM.\n",
			addr);
		return -ENOMEM;
	}

	ret = iwl_send_lq_cmd(priv, ctx, link_cmd, CMD_SYNC, true);
	if (ret)
		IWL_ERR(priv, "Link quality command failed (%d)\n", ret);

	spin_lock_irqsave(&priv->sta_lock, flags);
	priv->stations[sta_id].lq = link_cmd;
	spin_unlock_irqrestore(&priv->sta_lock, flags);

	return 0;
}

static int iwl_send_static_wepkey_cmd(struct iwl_priv *priv,
				      struct iwl_rxon_context *ctx,
				      bool send_if_empty)
{
	int i, not_empty = 0;
	u8 buff[sizeof(struct iwl_wep_cmd) +
		sizeof(struct iwl_wep_key) * WEP_KEYS_MAX];
	struct iwl_wep_cmd *wep_cmd = (struct iwl_wep_cmd *)buff;
	size_t cmd_size  = sizeof(struct iwl_wep_cmd);
	struct iwl_host_cmd cmd = {
		.id = ctx->wep_key_cmd,
		.data = wep_cmd,
		.flags = CMD_SYNC,
	};

	might_sleep();

	memset(wep_cmd, 0, cmd_size +
			(sizeof(struct iwl_wep_key) * WEP_KEYS_MAX));

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

	cmd_size += sizeof(struct iwl_wep_key) * WEP_KEYS_MAX;

	cmd.len = cmd_size;

	if (not_empty || send_if_empty)
		return iwl_send_cmd(priv, &cmd);
	else
		return 0;
}

int iwl_restore_default_wep_keys(struct iwl_priv *priv,
				 struct iwl_rxon_context *ctx)
{
	lockdep_assert_held(&priv->mutex);

	return iwl_send_static_wepkey_cmd(priv, ctx, false);
}

int iwl_remove_default_wep_key(struct iwl_priv *priv,
			       struct iwl_rxon_context *ctx,
			       struct ieee80211_key_conf *keyconf)
{
	int ret;

	lockdep_assert_held(&priv->mutex);

	IWL_DEBUG_WEP(priv, "Removing default WEP key: idx=%d\n",
		      keyconf->keyidx);

	memset(&ctx->wep_keys[keyconf->keyidx], 0, sizeof(ctx->wep_keys[0]));
	if (iwl_is_rfkill(priv)) {
		IWL_DEBUG_WEP(priv, "Not sending REPLY_WEPKEY command due to RFKILL.\n");
		/* but keys in device are clear anyway so return success */
		return 0;
	}
	ret = iwl_send_static_wepkey_cmd(priv, ctx, 1);
	IWL_DEBUG_WEP(priv, "Remove default WEP key: idx=%d ret=%d\n",
		      keyconf->keyidx, ret);

	return ret;
}

int iwl_set_default_wep_key(struct iwl_priv *priv,
			    struct iwl_rxon_context *ctx,
			    struct ieee80211_key_conf *keyconf)
{
	int ret;

	lockdep_assert_held(&priv->mutex);

	if (keyconf->keylen != WEP_KEY_LEN_128 &&
	    keyconf->keylen != WEP_KEY_LEN_64) {
		IWL_DEBUG_WEP(priv, "Bad WEP key length %d\n", keyconf->keylen);
		return -EINVAL;
	}

	keyconf->flags &= ~IEEE80211_KEY_FLAG_GENERATE_IV;
	keyconf->hw_key_idx = HW_KEY_DEFAULT;
	priv->stations[ctx->ap_sta_id].keyinfo.cipher = keyconf->cipher;

	ctx->wep_keys[keyconf->keyidx].key_size = keyconf->keylen;
	memcpy(&ctx->wep_keys[keyconf->keyidx].key, &keyconf->key,
							keyconf->keylen);

	ret = iwl_send_static_wepkey_cmd(priv, ctx, false);
	IWL_DEBUG_WEP(priv, "Set default WEP key: len=%d idx=%d ret=%d\n",
		keyconf->keylen, keyconf->keyidx, ret);

	return ret;
}

static int iwl_set_wep_dynamic_key_info(struct iwl_priv *priv,
					struct iwl_rxon_context *ctx,
					struct ieee80211_key_conf *keyconf,
					u8 sta_id)
{
	unsigned long flags;
	__le16 key_flags = 0;
	struct iwl_addsta_cmd sta_cmd;

	lockdep_assert_held(&priv->mutex);

	keyconf->flags &= ~IEEE80211_KEY_FLAG_GENERATE_IV;

	key_flags |= (STA_KEY_FLG_WEP | STA_KEY_FLG_MAP_KEY_MSK);
	key_flags |= cpu_to_le16(keyconf->keyidx << STA_KEY_FLG_KEYID_POS);
	key_flags &= ~STA_KEY_FLG_INVALID;

	if (keyconf->keylen == WEP_KEY_LEN_128)
		key_flags |= STA_KEY_FLG_KEY_SIZE_MSK;

	if (sta_id == ctx->bcast_sta_id)
		key_flags |= STA_KEY_MULTICAST_MSK;

	spin_lock_irqsave(&priv->sta_lock, flags);

	priv->stations[sta_id].keyinfo.cipher = keyconf->cipher;
	priv->stations[sta_id].keyinfo.keylen = keyconf->keylen;
	priv->stations[sta_id].keyinfo.keyidx = keyconf->keyidx;

	memcpy(priv->stations[sta_id].keyinfo.key,
				keyconf->key, keyconf->keylen);

	memcpy(&priv->stations[sta_id].sta.key.key[3],
				keyconf->key, keyconf->keylen);

	if ((priv->stations[sta_id].sta.key.key_flags & STA_KEY_FLG_ENCRYPT_MSK)
			== STA_KEY_FLG_NO_ENC)
		priv->stations[sta_id].sta.key.key_offset =
				 iwl_get_free_ucode_key_index(priv);
	/* else, we are overriding an existing key => no need to allocated room
	 * in uCode. */

	WARN(priv->stations[sta_id].sta.key.key_offset == WEP_INVALID_OFFSET,
		"no space for a new key");

	priv->stations[sta_id].sta.key.key_flags = key_flags;
	priv->stations[sta_id].sta.sta.modify_mask = STA_MODIFY_KEY_MASK;
	priv->stations[sta_id].sta.mode = STA_CONTROL_MODIFY_MSK;

	memcpy(&sta_cmd, &priv->stations[sta_id].sta, sizeof(struct iwl_addsta_cmd));
	spin_unlock_irqrestore(&priv->sta_lock, flags);

	return iwl_send_add_sta(priv, &sta_cmd, CMD_SYNC);
}

static int iwl_set_ccmp_dynamic_key_info(struct iwl_priv *priv,
					 struct iwl_rxon_context *ctx,
					 struct ieee80211_key_conf *keyconf,
					 u8 sta_id)
{
	unsigned long flags;
	__le16 key_flags = 0;
	struct iwl_addsta_cmd sta_cmd;

	lockdep_assert_held(&priv->mutex);

	key_flags |= (STA_KEY_FLG_CCMP | STA_KEY_FLG_MAP_KEY_MSK);
	key_flags |= cpu_to_le16(keyconf->keyidx << STA_KEY_FLG_KEYID_POS);
	key_flags &= ~STA_KEY_FLG_INVALID;

	if (sta_id == ctx->bcast_sta_id)
		key_flags |= STA_KEY_MULTICAST_MSK;

	keyconf->flags |= IEEE80211_KEY_FLAG_GENERATE_IV;

	spin_lock_irqsave(&priv->sta_lock, flags);
	priv->stations[sta_id].keyinfo.cipher = keyconf->cipher;
	priv->stations[sta_id].keyinfo.keylen = keyconf->keylen;

	memcpy(priv->stations[sta_id].keyinfo.key, keyconf->key,
	       keyconf->keylen);

	memcpy(priv->stations[sta_id].sta.key.key, keyconf->key,
	       keyconf->keylen);

	if ((priv->stations[sta_id].sta.key.key_flags & STA_KEY_FLG_ENCRYPT_MSK)
			== STA_KEY_FLG_NO_ENC)
		priv->stations[sta_id].sta.key.key_offset =
				 iwl_get_free_ucode_key_index(priv);
	/* else, we are overriding an existing key => no need to allocated room
	 * in uCode. */

	WARN(priv->stations[sta_id].sta.key.key_offset == WEP_INVALID_OFFSET,
		"no space for a new key");

	priv->stations[sta_id].sta.key.key_flags = key_flags;
	priv->stations[sta_id].sta.sta.modify_mask = STA_MODIFY_KEY_MASK;
	priv->stations[sta_id].sta.mode = STA_CONTROL_MODIFY_MSK;

	memcpy(&sta_cmd, &priv->stations[sta_id].sta, sizeof(struct iwl_addsta_cmd));
	spin_unlock_irqrestore(&priv->sta_lock, flags);

	return iwl_send_add_sta(priv, &sta_cmd, CMD_SYNC);
}

static int iwl_set_tkip_dynamic_key_info(struct iwl_priv *priv,
					 struct iwl_rxon_context *ctx,
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

	spin_lock_irqsave(&priv->sta_lock, flags);

	priv->stations[sta_id].keyinfo.cipher = keyconf->cipher;
	priv->stations[sta_id].keyinfo.keylen = 16;

	if ((priv->stations[sta_id].sta.key.key_flags & STA_KEY_FLG_ENCRYPT_MSK)
			== STA_KEY_FLG_NO_ENC)
		priv->stations[sta_id].sta.key.key_offset =
				 iwl_get_free_ucode_key_index(priv);
	/* else, we are overriding an existing key => no need to allocated room
	 * in uCode. */

	WARN(priv->stations[sta_id].sta.key.key_offset == WEP_INVALID_OFFSET,
		"no space for a new key");

	priv->stations[sta_id].sta.key.key_flags = key_flags;


	/* This copy is acutally not needed: we get the key with each TX */
	memcpy(priv->stations[sta_id].keyinfo.key, keyconf->key, 16);

	memcpy(priv->stations[sta_id].sta.key.key, keyconf->key, 16);

	spin_unlock_irqrestore(&priv->sta_lock, flags);

	return ret;
}

void iwl_update_tkip_key(struct iwl_priv *priv,
			 struct iwl_rxon_context *ctx,
			 struct ieee80211_key_conf *keyconf,
			 struct ieee80211_sta *sta, u32 iv32, u16 *phase1key)
{
	u8 sta_id;
	unsigned long flags;
	int i;

	if (iwl_scan_cancel(priv)) {
		/* cancel scan failed, just live w/ bad key and rely
		   briefly on SW decryption */
		return;
	}

	sta_id = iwl_sta_id_or_broadcast(priv, ctx, sta);
	if (sta_id == IWL_INVALID_STATION)
		return;

	spin_lock_irqsave(&priv->sta_lock, flags);

	priv->stations[sta_id].sta.key.tkip_rx_tsc_byte2 = (u8) iv32;

	for (i = 0; i < 5; i++)
		priv->stations[sta_id].sta.key.tkip_rx_ttak[i] =
			cpu_to_le16(phase1key[i]);

	priv->stations[sta_id].sta.sta.modify_mask = STA_MODIFY_KEY_MASK;
	priv->stations[sta_id].sta.mode = STA_CONTROL_MODIFY_MSK;

	iwl_send_add_sta(priv, &priv->stations[sta_id].sta, CMD_ASYNC);

	spin_unlock_irqrestore(&priv->sta_lock, flags);

}

int iwl_remove_dynamic_key(struct iwl_priv *priv,
			   struct iwl_rxon_context *ctx,
			   struct ieee80211_key_conf *keyconf,
			   u8 sta_id)
{
	unsigned long flags;
	u16 key_flags;
	u8 keyidx;
	struct iwl_addsta_cmd sta_cmd;

	lockdep_assert_held(&priv->mutex);

	ctx->key_mapping_keys--;

	spin_lock_irqsave(&priv->sta_lock, flags);
	key_flags = le16_to_cpu(priv->stations[sta_id].sta.key.key_flags);
	keyidx = (key_flags >> STA_KEY_FLG_KEYID_POS) & 0x3;

	IWL_DEBUG_WEP(priv, "Remove dynamic key: idx=%d sta=%d\n",
		      keyconf->keyidx, sta_id);

	if (keyconf->keyidx != keyidx) {
		/* We need to remove a key with index different that the one
		 * in the uCode. This means that the key we need to remove has
		 * been replaced by another one with different index.
		 * Don't do anything and return ok
		 */
		spin_unlock_irqrestore(&priv->sta_lock, flags);
		return 0;
	}

	if (priv->stations[sta_id].sta.key.key_offset == WEP_INVALID_OFFSET) {
		IWL_WARN(priv, "Removing wrong key %d 0x%x\n",
			    keyconf->keyidx, key_flags);
		spin_unlock_irqrestore(&priv->sta_lock, flags);
		return 0;
	}

	if (!test_and_clear_bit(priv->stations[sta_id].sta.key.key_offset,
		&priv->ucode_key_table))
		IWL_ERR(priv, "index %d not used in uCode key table.\n",
			priv->stations[sta_id].sta.key.key_offset);
	memset(&priv->stations[sta_id].keyinfo, 0,
					sizeof(struct iwl_hw_key));
	memset(&priv->stations[sta_id].sta.key, 0,
					sizeof(struct iwl4965_keyinfo));
	priv->stations[sta_id].sta.key.key_flags =
			STA_KEY_FLG_NO_ENC | STA_KEY_FLG_INVALID;
	priv->stations[sta_id].sta.key.key_offset = WEP_INVALID_OFFSET;
	priv->stations[sta_id].sta.sta.modify_mask = STA_MODIFY_KEY_MASK;
	priv->stations[sta_id].sta.mode = STA_CONTROL_MODIFY_MSK;

	if (iwl_is_rfkill(priv)) {
		IWL_DEBUG_WEP(priv, "Not sending REPLY_ADD_STA command because RFKILL enabled.\n");
		spin_unlock_irqrestore(&priv->sta_lock, flags);
		return 0;
	}
	memcpy(&sta_cmd, &priv->stations[sta_id].sta, sizeof(struct iwl_addsta_cmd));
	spin_unlock_irqrestore(&priv->sta_lock, flags);

	return iwl_send_add_sta(priv, &sta_cmd, CMD_SYNC);
}

int iwl_set_dynamic_key(struct iwl_priv *priv, struct iwl_rxon_context *ctx,
			struct ieee80211_key_conf *keyconf, u8 sta_id)
{
	int ret;

	lockdep_assert_held(&priv->mutex);

	ctx->key_mapping_keys++;
	keyconf->hw_key_idx = HW_KEY_DYNAMIC;

	switch (keyconf->cipher) {
	case WLAN_CIPHER_SUITE_CCMP:
		ret = iwl_set_ccmp_dynamic_key_info(priv, ctx, keyconf, sta_id);
		break;
	case WLAN_CIPHER_SUITE_TKIP:
		ret = iwl_set_tkip_dynamic_key_info(priv, ctx, keyconf, sta_id);
		break;
	case WLAN_CIPHER_SUITE_WEP40:
	case WLAN_CIPHER_SUITE_WEP104:
		ret = iwl_set_wep_dynamic_key_info(priv, ctx, keyconf, sta_id);
		break;
	default:
		IWL_ERR(priv,
			"Unknown alg: %s cipher = %x\n", __func__,
			keyconf->cipher);
		ret = -EINVAL;
	}

	IWL_DEBUG_WEP(priv, "Set dynamic key: cipher=%x len=%d idx=%d sta=%d ret=%d\n",
		      keyconf->cipher, keyconf->keylen, keyconf->keyidx,
		      sta_id, ret);

	return ret;
}

/**
 * iwlagn_alloc_bcast_station - add broadcast station into driver's station table.
 *
 * This adds the broadcast station into the driver's station table
 * and marks it driver active, so that it will be restored to the
 * device at the next best time.
 */
int iwlagn_alloc_bcast_station(struct iwl_priv *priv,
			       struct iwl_rxon_context *ctx)
{
	struct iwl_link_quality_cmd *link_cmd;
	unsigned long flags;
	u8 sta_id;

	spin_lock_irqsave(&priv->sta_lock, flags);
	sta_id = iwl_prep_station(priv, ctx, iwl_bcast_addr, false, NULL);
	if (sta_id == IWL_INVALID_STATION) {
		IWL_ERR(priv, "Unable to prepare broadcast station\n");
		spin_unlock_irqrestore(&priv->sta_lock, flags);

		return -EINVAL;
	}

	priv->stations[sta_id].used |= IWL_STA_DRIVER_ACTIVE;
	priv->stations[sta_id].used |= IWL_STA_BCAST;
	spin_unlock_irqrestore(&priv->sta_lock, flags);

	link_cmd = iwl_sta_alloc_lq(priv, sta_id);
	if (!link_cmd) {
		IWL_ERR(priv,
			"Unable to initialize rate scaling for bcast station.\n");
		return -ENOMEM;
	}

	spin_lock_irqsave(&priv->sta_lock, flags);
	priv->stations[sta_id].lq = link_cmd;
	spin_unlock_irqrestore(&priv->sta_lock, flags);

	return 0;
}

/**
 * iwl_update_bcast_station - update broadcast station's LQ command
 *
 * Only used by iwlagn. Placed here to have all bcast station management
 * code together.
 */
static int iwl_update_bcast_station(struct iwl_priv *priv,
				    struct iwl_rxon_context *ctx)
{
	unsigned long flags;
	struct iwl_link_quality_cmd *link_cmd;
	u8 sta_id = ctx->bcast_sta_id;

	link_cmd = iwl_sta_alloc_lq(priv, sta_id);
	if (!link_cmd) {
		IWL_ERR(priv, "Unable to initialize rate scaling for bcast station.\n");
		return -ENOMEM;
	}

	spin_lock_irqsave(&priv->sta_lock, flags);
	if (priv->stations[sta_id].lq)
		kfree(priv->stations[sta_id].lq);
	else
		IWL_DEBUG_INFO(priv, "Bcast station rate scaling has not been initialized yet.\n");
	priv->stations[sta_id].lq = link_cmd;
	spin_unlock_irqrestore(&priv->sta_lock, flags);

	return 0;
}

int iwl_update_bcast_stations(struct iwl_priv *priv)
{
	struct iwl_rxon_context *ctx;
	int ret = 0;

	for_each_context(priv, ctx) {
		ret = iwl_update_bcast_station(priv, ctx);
		if (ret)
			break;
	}

	return ret;
}

/**
 * iwl_sta_tx_modify_enable_tid - Enable Tx for this TID in station table
 */
int iwl_sta_tx_modify_enable_tid(struct iwl_priv *priv, int sta_id, int tid)
{
	unsigned long flags;
	struct iwl_addsta_cmd sta_cmd;

	lockdep_assert_held(&priv->mutex);

	/* Remove "disable" flag, to enable Tx for this TID */
	spin_lock_irqsave(&priv->sta_lock, flags);
	priv->stations[sta_id].sta.sta.modify_mask = STA_MODIFY_TID_DISABLE_TX;
	priv->stations[sta_id].sta.tid_disable_tx &= cpu_to_le16(~(1 << tid));
	priv->stations[sta_id].sta.mode = STA_CONTROL_MODIFY_MSK;
	memcpy(&sta_cmd, &priv->stations[sta_id].sta, sizeof(struct iwl_addsta_cmd));
	spin_unlock_irqrestore(&priv->sta_lock, flags);

	return iwl_send_add_sta(priv, &sta_cmd, CMD_SYNC);
}

int iwl_sta_rx_agg_start(struct iwl_priv *priv, struct ieee80211_sta *sta,
			 int tid, u16 ssn)
{
	unsigned long flags;
	int sta_id;
	struct iwl_addsta_cmd sta_cmd;

	lockdep_assert_held(&priv->mutex);

	sta_id = iwl_sta_id(sta);
	if (sta_id == IWL_INVALID_STATION)
		return -ENXIO;

	spin_lock_irqsave(&priv->sta_lock, flags);
	priv->stations[sta_id].sta.station_flags_msk = 0;
	priv->stations[sta_id].sta.sta.modify_mask = STA_MODIFY_ADDBA_TID_MSK;
	priv->stations[sta_id].sta.add_immediate_ba_tid = (u8)tid;
	priv->stations[sta_id].sta.add_immediate_ba_ssn = cpu_to_le16(ssn);
	priv->stations[sta_id].sta.mode = STA_CONTROL_MODIFY_MSK;
	memcpy(&sta_cmd, &priv->stations[sta_id].sta, sizeof(struct iwl_addsta_cmd));
	spin_unlock_irqrestore(&priv->sta_lock, flags);

	return iwl_send_add_sta(priv, &sta_cmd, CMD_SYNC);
}

int iwl_sta_rx_agg_stop(struct iwl_priv *priv, struct ieee80211_sta *sta,
			int tid)
{
	unsigned long flags;
	int sta_id;
	struct iwl_addsta_cmd sta_cmd;

	lockdep_assert_held(&priv->mutex);

	sta_id = iwl_sta_id(sta);
	if (sta_id == IWL_INVALID_STATION) {
		IWL_ERR(priv, "Invalid station for AGG tid %d\n", tid);
		return -ENXIO;
	}

	spin_lock_irqsave(&priv->sta_lock, flags);
	priv->stations[sta_id].sta.station_flags_msk = 0;
	priv->stations[sta_id].sta.sta.modify_mask = STA_MODIFY_DELBA_TID_MSK;
	priv->stations[sta_id].sta.remove_immediate_ba_tid = (u8)tid;
	priv->stations[sta_id].sta.mode = STA_CONTROL_MODIFY_MSK;
	memcpy(&sta_cmd, &priv->stations[sta_id].sta, sizeof(struct iwl_addsta_cmd));
	spin_unlock_irqrestore(&priv->sta_lock, flags);

	return iwl_send_add_sta(priv, &sta_cmd, CMD_SYNC);
}

static void iwl_sta_modify_ps_wake(struct iwl_priv *priv, int sta_id)
{
	unsigned long flags;

	spin_lock_irqsave(&priv->sta_lock, flags);
	priv->stations[sta_id].sta.station_flags &= ~STA_FLG_PWR_SAVE_MSK;
	priv->stations[sta_id].sta.station_flags_msk = STA_FLG_PWR_SAVE_MSK;
	priv->stations[sta_id].sta.sta.modify_mask = 0;
	priv->stations[sta_id].sta.sleep_tx_count = 0;
	priv->stations[sta_id].sta.mode = STA_CONTROL_MODIFY_MSK;
	iwl_send_add_sta(priv, &priv->stations[sta_id].sta, CMD_ASYNC);
	spin_unlock_irqrestore(&priv->sta_lock, flags);

}

void iwl_sta_modify_sleep_tx_count(struct iwl_priv *priv, int sta_id, int cnt)
{
	unsigned long flags;

	spin_lock_irqsave(&priv->sta_lock, flags);
	priv->stations[sta_id].sta.station_flags |= STA_FLG_PWR_SAVE_MSK;
	priv->stations[sta_id].sta.station_flags_msk = STA_FLG_PWR_SAVE_MSK;
	priv->stations[sta_id].sta.sta.modify_mask =
					STA_MODIFY_SLEEP_TX_COUNT_MSK;
	priv->stations[sta_id].sta.sleep_tx_count = cpu_to_le16(cnt);
	priv->stations[sta_id].sta.mode = STA_CONTROL_MODIFY_MSK;
	iwl_send_add_sta(priv, &priv->stations[sta_id].sta, CMD_ASYNC);
	spin_unlock_irqrestore(&priv->sta_lock, flags);

}

void iwlagn_mac_sta_notify(struct ieee80211_hw *hw,
			   struct ieee80211_vif *vif,
			   enum sta_notify_cmd cmd,
			   struct ieee80211_sta *sta)
{
	struct iwl_priv *priv = hw->priv;
	struct iwl_station_priv *sta_priv = (void *)sta->drv_priv;
	int sta_id;

	switch (cmd) {
	case STA_NOTIFY_SLEEP:
		WARN_ON(!sta_priv->client);
		sta_priv->asleep = true;
		if (atomic_read(&sta_priv->pending_frames) > 0)
			ieee80211_sta_block_awake(hw, sta, true);
		break;
	case STA_NOTIFY_AWAKE:
		WARN_ON(!sta_priv->client);
		if (!sta_priv->asleep)
			break;
		sta_priv->asleep = false;
		sta_id = iwl_sta_id(sta);
		if (sta_id != IWL_INVALID_STATION)
			iwl_sta_modify_ps_wake(priv, sta_id);
		break;
	default:
		break;
	}
}
