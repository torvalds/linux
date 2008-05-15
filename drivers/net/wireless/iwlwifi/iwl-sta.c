/******************************************************************************
 *
 * Copyright(c) 2003 - 2008 Intel Corporation. All rights reserved.
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
 * James P. Ketrenos <ipw2100-admin@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 *****************************************************************************/

#include <net/mac80211.h>
#include <linux/etherdevice.h>

#include "iwl-eeprom.h"
#include "iwl-dev.h"
#include "iwl-core.h"
#include "iwl-sta.h"
#include "iwl-io.h"
#include "iwl-helpers.h"

u8 iwl_find_station(struct iwl_priv *priv, const u8 *addr)
{
	int i;
	int start = 0;
	int ret = IWL_INVALID_STATION;
	unsigned long flags;
	DECLARE_MAC_BUF(mac);

	if ((priv->iw_mode == IEEE80211_IF_TYPE_IBSS) ||
	    (priv->iw_mode == IEEE80211_IF_TYPE_AP))
		start = IWL_STA_ID;

	if (is_broadcast_ether_addr(addr))
		return priv->hw_params.bcast_sta_id;

	spin_lock_irqsave(&priv->sta_lock, flags);
	for (i = start; i < priv->hw_params.max_stations; i++)
		if (priv->stations[i].used &&
		    (!compare_ether_addr(priv->stations[i].sta.sta.addr,
					 addr))) {
			ret = i;
			goto out;
		}

	IWL_DEBUG_ASSOC_LIMIT("can not find STA %s total %d\n",
			      print_mac(mac, addr), priv->num_stations);

 out:
	spin_unlock_irqrestore(&priv->sta_lock, flags);
	return ret;
}
EXPORT_SYMBOL(iwl_find_station);

int iwl_send_add_sta(struct iwl_priv *priv,
		     struct iwl_addsta_cmd *sta, u8 flags)
{
	struct iwl_rx_packet *res = NULL;
	int ret = 0;
	u8 data[sizeof(*sta)];
	struct iwl_host_cmd cmd = {
		.id = REPLY_ADD_STA,
		.meta.flags = flags,
		.data = data,
	};

	if (!(flags & CMD_ASYNC))
		cmd.meta.flags |= CMD_WANT_SKB;

	cmd.len = priv->cfg->ops->utils->build_addsta_hcmd(sta, data);
	ret = iwl_send_cmd(priv, &cmd);

	if (ret || (flags & CMD_ASYNC))
		return ret;

	res = (struct iwl_rx_packet *)cmd.meta.u.skb->data;
	if (res->hdr.flags & IWL_CMD_FAILED_MSK) {
		IWL_ERROR("Bad return from REPLY_ADD_STA (0x%08X)\n",
			  res->hdr.flags);
		ret = -EIO;
	}

	if (ret == 0) {
		switch (res->u.add_sta.status) {
		case ADD_STA_SUCCESS_MSK:
			IWL_DEBUG_INFO("REPLY_ADD_STA PASSED\n");
			break;
		default:
			ret = -EIO;
			IWL_WARNING("REPLY_ADD_STA failed\n");
			break;
		}
	}

	priv->alloc_rxb_skb--;
	dev_kfree_skb_any(cmd.meta.u.skb);

	return ret;
}
EXPORT_SYMBOL(iwl_send_add_sta);

#ifdef CONFIG_IWL4965_HT

static void iwl_set_ht_add_station(struct iwl_priv *priv, u8 index,
				   struct ieee80211_ht_info *sta_ht_inf)
{
	__le32 sta_flags;
	u8 mimo_ps_mode;

	if (!sta_ht_inf || !sta_ht_inf->ht_supported)
		goto done;

	mimo_ps_mode = (sta_ht_inf->cap & IEEE80211_HT_CAP_MIMO_PS) >> 2;

	sta_flags = priv->stations[index].sta.station_flags;

	sta_flags &= ~(STA_FLG_RTS_MIMO_PROT_MSK | STA_FLG_MIMO_DIS_MSK);

	switch (mimo_ps_mode) {
	case WLAN_HT_CAP_MIMO_PS_STATIC:
		sta_flags |= STA_FLG_MIMO_DIS_MSK;
		break;
	case WLAN_HT_CAP_MIMO_PS_DYNAMIC:
		sta_flags |= STA_FLG_RTS_MIMO_PROT_MSK;
		break;
	case WLAN_HT_CAP_MIMO_PS_DISABLED:
		break;
	default:
		IWL_WARNING("Invalid MIMO PS mode %d", mimo_ps_mode);
		break;
	}

	sta_flags |= cpu_to_le32(
	      (u32)sta_ht_inf->ampdu_factor << STA_FLG_MAX_AGG_SIZE_POS);

	sta_flags |= cpu_to_le32(
	      (u32)sta_ht_inf->ampdu_density << STA_FLG_AGG_MPDU_DENSITY_POS);

	if (iwl_is_fat_tx_allowed(priv, sta_ht_inf))
		sta_flags |= STA_FLG_FAT_EN_MSK;
	else
		sta_flags &= ~STA_FLG_FAT_EN_MSK;

	priv->stations[index].sta.station_flags = sta_flags;
 done:
	return;
}
#else
static inline void iwl_set_ht_add_station(struct iwl_priv *priv, u8 index,
					struct ieee80211_ht_info *sta_ht_info)
{
}
#endif

/**
 * iwl_add_station_flags - Add station to tables in driver and device
 */
u8 iwl_add_station_flags(struct iwl_priv *priv, const u8 *addr, int is_ap,
			 u8 flags, struct ieee80211_ht_info *ht_info)
{
	int i;
	int index = IWL_INVALID_STATION;
	struct iwl_station_entry *station;
	unsigned long flags_spin;
	DECLARE_MAC_BUF(mac);

	spin_lock_irqsave(&priv->sta_lock, flags_spin);
	if (is_ap)
		index = IWL_AP_ID;
	else if (is_broadcast_ether_addr(addr))
		index = priv->hw_params.bcast_sta_id;
	else
		for (i = IWL_STA_ID; i < priv->hw_params.max_stations; i++) {
			if (!compare_ether_addr(priv->stations[i].sta.sta.addr,
						addr)) {
				index = i;
				break;
			}

			if (!priv->stations[i].used &&
			    index == IWL_INVALID_STATION)
				index = i;
		}


	/* These two conditions have the same outcome, but keep them separate
	  since they have different meanings */
	if (unlikely(index == IWL_INVALID_STATION)) {
		spin_unlock_irqrestore(&priv->sta_lock, flags_spin);
		return index;
	}

	if (priv->stations[index].used &&
	    !compare_ether_addr(priv->stations[index].sta.sta.addr, addr)) {
		spin_unlock_irqrestore(&priv->sta_lock, flags_spin);
		return index;
	}


	IWL_DEBUG_ASSOC("Add STA ID %d: %s\n", index, print_mac(mac, addr));
	station = &priv->stations[index];
	station->used = 1;
	priv->num_stations++;

	/* Set up the REPLY_ADD_STA command to send to device */
	memset(&station->sta, 0, sizeof(struct iwl_addsta_cmd));
	memcpy(station->sta.sta.addr, addr, ETH_ALEN);
	station->sta.mode = 0;
	station->sta.sta.sta_id = index;
	station->sta.station_flags = 0;

	/* BCAST station and IBSS stations do not work in HT mode */
	if (index != priv->hw_params.bcast_sta_id &&
	    priv->iw_mode != IEEE80211_IF_TYPE_IBSS)
		iwl_set_ht_add_station(priv, index, ht_info);

	spin_unlock_irqrestore(&priv->sta_lock, flags_spin);

	/* Add station to device's station table */
	iwl_send_add_sta(priv, &station->sta, flags);
	return index;

}
EXPORT_SYMBOL(iwl_add_station_flags);

int iwl_get_free_ucode_key_index(struct iwl_priv *priv)
{
	int i;

	for (i = 0; i < STA_KEY_MAX_NUM; i++)
		if (!test_and_set_bit(i, &priv->ucode_key_table))
			return i;

	return -1;
}

int iwl_send_static_wepkey_cmd(struct iwl_priv *priv, u8 send_if_empty)
{
	int i, not_empty = 0;
	u8 buff[sizeof(struct iwl_wep_cmd) +
		sizeof(struct iwl_wep_key) * WEP_KEYS_MAX];
	struct iwl_wep_cmd *wep_cmd = (struct iwl_wep_cmd *)buff;
	size_t cmd_size  = sizeof(struct iwl_wep_cmd);
	struct iwl_host_cmd cmd = {
		.id = REPLY_WEPKEY,
		.data = wep_cmd,
		.meta.flags = CMD_ASYNC,
	};

	memset(wep_cmd, 0, cmd_size +
			(sizeof(struct iwl_wep_key) * WEP_KEYS_MAX));

	for (i = 0; i < WEP_KEYS_MAX ; i++) {
		wep_cmd->key[i].key_index = i;
		if (priv->wep_keys[i].key_size) {
			wep_cmd->key[i].key_offset = i;
			not_empty = 1;
		} else {
			wep_cmd->key[i].key_offset = WEP_INVALID_OFFSET;
		}

		wep_cmd->key[i].key_size = priv->wep_keys[i].key_size;
		memcpy(&wep_cmd->key[i].key[3], priv->wep_keys[i].key,
				priv->wep_keys[i].key_size);
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
EXPORT_SYMBOL(iwl_send_static_wepkey_cmd);

int iwl_remove_default_wep_key(struct iwl_priv *priv,
			       struct ieee80211_key_conf *keyconf)
{
	int ret;
	unsigned long flags;

	spin_lock_irqsave(&priv->sta_lock, flags);

	if (!test_and_clear_bit(keyconf->keyidx, &priv->ucode_key_table))
		IWL_ERROR("index %d not used in uCode key table.\n",
			  keyconf->keyidx);

	priv->default_wep_key--;
	memset(&priv->wep_keys[keyconf->keyidx], 0, sizeof(priv->wep_keys[0]));
	ret = iwl_send_static_wepkey_cmd(priv, 1);
	spin_unlock_irqrestore(&priv->sta_lock, flags);

	return ret;
}
EXPORT_SYMBOL(iwl_remove_default_wep_key);

int iwl_set_default_wep_key(struct iwl_priv *priv,
			    struct ieee80211_key_conf *keyconf)
{
	int ret;
	unsigned long flags;

	keyconf->flags &= ~IEEE80211_KEY_FLAG_GENERATE_IV;
	keyconf->hw_key_idx = HW_KEY_DEFAULT;
	priv->stations[IWL_AP_ID].keyinfo.alg = ALG_WEP;

	spin_lock_irqsave(&priv->sta_lock, flags);
	priv->default_wep_key++;

	if (test_and_set_bit(keyconf->keyidx, &priv->ucode_key_table))
		IWL_ERROR("index %d already used in uCode key table.\n",
			keyconf->keyidx);

	priv->wep_keys[keyconf->keyidx].key_size = keyconf->keylen;
	memcpy(&priv->wep_keys[keyconf->keyidx].key, &keyconf->key,
							keyconf->keylen);

	ret = iwl_send_static_wepkey_cmd(priv, 0);
	spin_unlock_irqrestore(&priv->sta_lock, flags);

	return ret;
}
EXPORT_SYMBOL(iwl_set_default_wep_key);

static int iwl_set_wep_dynamic_key_info(struct iwl_priv *priv,
				struct ieee80211_key_conf *keyconf,
				u8 sta_id)
{
	unsigned long flags;
	__le16 key_flags = 0;
	int ret;

	keyconf->flags &= ~IEEE80211_KEY_FLAG_GENERATE_IV;

	key_flags |= (STA_KEY_FLG_WEP | STA_KEY_FLG_MAP_KEY_MSK);
	key_flags |= cpu_to_le16(keyconf->keyidx << STA_KEY_FLG_KEYID_POS);
	key_flags &= ~STA_KEY_FLG_INVALID;

	if (keyconf->keylen == WEP_KEY_LEN_128)
		key_flags |= STA_KEY_FLG_KEY_SIZE_MSK;

	if (sta_id == priv->hw_params.bcast_sta_id)
		key_flags |= STA_KEY_MULTICAST_MSK;

	spin_lock_irqsave(&priv->sta_lock, flags);

	priv->stations[sta_id].keyinfo.alg = keyconf->alg;
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

	priv->stations[sta_id].sta.key.key_flags = key_flags;
	priv->stations[sta_id].sta.sta.modify_mask = STA_MODIFY_KEY_MASK;
	priv->stations[sta_id].sta.mode = STA_CONTROL_MODIFY_MSK;

	ret = iwl_send_add_sta(priv, &priv->stations[sta_id].sta, CMD_ASYNC);

	spin_unlock_irqrestore(&priv->sta_lock, flags);

	return ret;
}

static int iwl_set_ccmp_dynamic_key_info(struct iwl_priv *priv,
				   struct ieee80211_key_conf *keyconf,
				   u8 sta_id)
{
	unsigned long flags;
	__le16 key_flags = 0;

	key_flags |= (STA_KEY_FLG_CCMP | STA_KEY_FLG_MAP_KEY_MSK);
	key_flags |= cpu_to_le16(keyconf->keyidx << STA_KEY_FLG_KEYID_POS);
	key_flags &= ~STA_KEY_FLG_INVALID;

	if (sta_id == priv->hw_params.bcast_sta_id)
		key_flags |= STA_KEY_MULTICAST_MSK;

	keyconf->flags |= IEEE80211_KEY_FLAG_GENERATE_IV;

	spin_lock_irqsave(&priv->sta_lock, flags);
	priv->stations[sta_id].keyinfo.alg = keyconf->alg;
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

	priv->stations[sta_id].sta.key.key_flags = key_flags;
	priv->stations[sta_id].sta.sta.modify_mask = STA_MODIFY_KEY_MASK;
	priv->stations[sta_id].sta.mode = STA_CONTROL_MODIFY_MSK;

	spin_unlock_irqrestore(&priv->sta_lock, flags);

	IWL_DEBUG_INFO("hwcrypto: modify ucode station key info\n");
	return iwl_send_add_sta(priv, &priv->stations[sta_id].sta, CMD_ASYNC);
}

static int iwl_set_tkip_dynamic_key_info(struct iwl_priv *priv,
				   struct ieee80211_key_conf *keyconf,
				   u8 sta_id)
{
	unsigned long flags;
	int ret = 0;

	keyconf->flags |= IEEE80211_KEY_FLAG_GENERATE_IV;
	keyconf->flags |= IEEE80211_KEY_FLAG_GENERATE_MMIC;

	spin_lock_irqsave(&priv->sta_lock, flags);

	priv->stations[sta_id].keyinfo.alg = keyconf->alg;
	priv->stations[sta_id].keyinfo.keylen = 16;

	if ((priv->stations[sta_id].sta.key.key_flags & STA_KEY_FLG_ENCRYPT_MSK)
			== STA_KEY_FLG_NO_ENC)
		priv->stations[sta_id].sta.key.key_offset =
				 iwl_get_free_ucode_key_index(priv);
	/* else, we are overriding an existing key => no need to allocated room
	 * in uCode. */

	/* This copy is acutally not needed: we get the key with each TX */
	memcpy(priv->stations[sta_id].keyinfo.key, keyconf->key, 16);

	memcpy(priv->stations[sta_id].sta.key.key, keyconf->key, 16);

	spin_unlock_irqrestore(&priv->sta_lock, flags);

	return ret;
}

int iwl_remove_dynamic_key(struct iwl_priv *priv,
				struct ieee80211_key_conf *keyconf,
				u8 sta_id)
{
	unsigned long flags;
	int ret = 0;
	u16 key_flags;
	u8 keyidx;

	priv->key_mapping_key--;

	spin_lock_irqsave(&priv->sta_lock, flags);
	key_flags = le16_to_cpu(priv->stations[sta_id].sta.key.key_flags);
	keyidx = (key_flags >> STA_KEY_FLG_KEYID_POS) & 0x3;

	if (keyconf->keyidx != keyidx) {
		/* We need to remove a key with index different that the one
		 * in the uCode. This means that the key we need to remove has
		 * been replaced by another one with different index.
		 * Don't do anything and return ok
		 */
		spin_unlock_irqrestore(&priv->sta_lock, flags);
		return 0;
	}

	if (!test_and_clear_bit(priv->stations[sta_id].sta.key.key_offset,
		&priv->ucode_key_table))
		IWL_ERROR("index %d not used in uCode key table.\n",
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

	IWL_DEBUG_INFO("hwcrypto: clear ucode station key info\n");
	ret =  iwl_send_add_sta(priv, &priv->stations[sta_id].sta, CMD_ASYNC);
	spin_unlock_irqrestore(&priv->sta_lock, flags);
	return ret;
}
EXPORT_SYMBOL(iwl_remove_dynamic_key);

int iwl_set_dynamic_key(struct iwl_priv *priv,
				struct ieee80211_key_conf *keyconf, u8 sta_id)
{
	int ret;

	priv->key_mapping_key++;
	keyconf->hw_key_idx = HW_KEY_DYNAMIC;

	switch (keyconf->alg) {
	case ALG_CCMP:
		ret = iwl_set_ccmp_dynamic_key_info(priv, keyconf, sta_id);
		break;
	case ALG_TKIP:
		ret = iwl_set_tkip_dynamic_key_info(priv, keyconf, sta_id);
		break;
	case ALG_WEP:
		ret = iwl_set_wep_dynamic_key_info(priv, keyconf, sta_id);
		break;
	default:
		IWL_ERROR("Unknown alg: %s alg = %d\n", __func__, keyconf->alg);
		ret = -EINVAL;
	}

	return ret;
}
EXPORT_SYMBOL(iwl_set_dynamic_key);

#ifdef CONFIG_IWLWIFI_DEBUG
static void iwl_dump_lq_cmd(struct iwl_priv *priv,
			   struct iwl_link_quality_cmd *lq)
{
	int i;
	IWL_DEBUG_RATE("lq station id 0x%x\n", lq->sta_id);
	IWL_DEBUG_RATE("lq dta 0x%X 0x%X\n",
		       lq->general_params.single_stream_ant_msk,
		       lq->general_params.dual_stream_ant_msk);

	for (i = 0; i < LINK_QUAL_MAX_RETRY_NUM; i++)
		IWL_DEBUG_RATE("lq index %d 0x%X\n",
			       i, lq->rs_table[i].rate_n_flags);
}
#else
static inline void iwl_dump_lq_cmd(struct iwl_priv *priv,
				   struct iwl_link_quality_cmd *lq)
{
}
#endif

int iwl_send_lq_cmd(struct iwl_priv *priv,
		    struct iwl_link_quality_cmd *lq, u8 flags)
{
	struct iwl_host_cmd cmd = {
		.id = REPLY_TX_LINK_QUALITY_CMD,
		.len = sizeof(struct iwl_link_quality_cmd),
		.meta.flags = flags,
		.data = lq,
	};

	if ((lq->sta_id == 0xFF) &&
	    (priv->iw_mode == IEEE80211_IF_TYPE_IBSS))
		return -EINVAL;

	if (lq->sta_id == 0xFF)
		lq->sta_id = IWL_AP_ID;

	iwl_dump_lq_cmd(priv,lq);

	if (iwl_is_associated(priv) && priv->assoc_station_added &&
	    priv->lq_mngr.lq_ready)
		return  iwl_send_cmd(priv, &cmd);

	return 0;
}
EXPORT_SYMBOL(iwl_send_lq_cmd);

/**
 * iwl_sta_init_lq - Initialize a station's hardware rate table
 *
 * The uCode's station table contains a table of fallback rates
 * for automatic fallback during transmission.
 *
 * NOTE: This sets up a default set of values.  These will be replaced later
 *       if the driver's iwl-4965-rs rate scaling algorithm is used, instead of
 *       rc80211_simple.
 *
 * NOTE: Run REPLY_ADD_STA command to set up station table entry, before
 *       calling this function (which runs REPLY_TX_LINK_QUALITY_CMD,
 *       which requires station table entry to exist).
 */
static void iwl_sta_init_lq(struct iwl_priv *priv, const u8 *addr, int is_ap)
{
	int i, r;
	struct iwl_link_quality_cmd link_cmd = {
		.reserved1 = 0,
	};
	u16 rate_flags;

	/* Set up the rate scaling to start at selected rate, fall back
	 * all the way down to 1M in IEEE order, and then spin on 1M */
	if (is_ap)
		r = IWL_RATE_54M_INDEX;
	else if (priv->band == IEEE80211_BAND_5GHZ)
		r = IWL_RATE_6M_INDEX;
	else
		r = IWL_RATE_1M_INDEX;

	for (i = 0; i < LINK_QUAL_MAX_RETRY_NUM; i++) {
		rate_flags = 0;
		if (r >= IWL_FIRST_CCK_RATE && r <= IWL_LAST_CCK_RATE)
			rate_flags |= RATE_MCS_CCK_MSK;

		/* Use Tx antenna B only */
		rate_flags |= RATE_MCS_ANT_B_MSK; /*FIXME:RS*/

		link_cmd.rs_table[i].rate_n_flags =
			iwl4965_hw_set_rate_n_flags(iwl_rates[r].plcp, rate_flags);
		r = iwl4965_get_prev_ieee_rate(r);
	}

	link_cmd.general_params.single_stream_ant_msk = 2;
	link_cmd.general_params.dual_stream_ant_msk = 3;
	link_cmd.agg_params.agg_dis_start_th = 3;
	link_cmd.agg_params.agg_time_limit = cpu_to_le16(4000);

	/* Update the rate scaling for control frame Tx to AP */
	link_cmd.sta_id = is_ap ? IWL_AP_ID : priv->hw_params.bcast_sta_id;

	iwl_send_cmd_pdu_async(priv, REPLY_TX_LINK_QUALITY_CMD,
			       sizeof(link_cmd), &link_cmd, NULL);
}
/**
 * iwl_rxon_add_station - add station into station table.
 *
 * there is only one AP station with id= IWL_AP_ID
 * NOTE: mutex must be held before calling this fnction
 */
int iwl_rxon_add_station(struct iwl_priv *priv, const u8 *addr, int is_ap)
{
	u8 sta_id;

	/* Add station to device's station table */
#ifdef CONFIG_IWL4965_HT
	struct ieee80211_conf *conf = &priv->hw->conf;
	struct ieee80211_ht_info *cur_ht_config = &conf->ht_conf;

	if ((is_ap) &&
	    (conf->flags & IEEE80211_CONF_SUPPORT_HT_MODE) &&
	    (priv->iw_mode == IEEE80211_IF_TYPE_STA))
		sta_id = iwl_add_station_flags(priv, addr, is_ap,
						   0, cur_ht_config);
	else
#endif /* CONFIG_IWL4965_HT */
		sta_id = iwl_add_station_flags(priv, addr, is_ap,
						   0, NULL);

	/* Set up default rate scaling table in device's station table */
	iwl_sta_init_lq(priv, addr, is_ap);

	return sta_id;
}
EXPORT_SYMBOL(iwl_rxon_add_station);


/**
 * iwl_get_sta_id - Find station's index within station table
 *
 * If new IBSS station, create new entry in station table
 */
int iwl_get_sta_id(struct iwl_priv *priv, struct ieee80211_hdr *hdr)
{
	int sta_id;
	u16 fc = le16_to_cpu(hdr->frame_control);
	DECLARE_MAC_BUF(mac);

	/* If this frame is broadcast or management, use broadcast station id */
	if (((fc & IEEE80211_FCTL_FTYPE) != IEEE80211_FTYPE_DATA) ||
	    is_multicast_ether_addr(hdr->addr1))
		return priv->hw_params.bcast_sta_id;

	switch (priv->iw_mode) {

	/* If we are a client station in a BSS network, use the special
	 * AP station entry (that's the only station we communicate with) */
	case IEEE80211_IF_TYPE_STA:
		return IWL_AP_ID;

	/* If we are an AP, then find the station, or use BCAST */
	case IEEE80211_IF_TYPE_AP:
		sta_id = iwl_find_station(priv, hdr->addr1);
		if (sta_id != IWL_INVALID_STATION)
			return sta_id;
		return priv->hw_params.bcast_sta_id;

	/* If this frame is going out to an IBSS network, find the station,
	 * or create a new station table entry */
	case IEEE80211_IF_TYPE_IBSS:
		sta_id = iwl_find_station(priv, hdr->addr1);
		if (sta_id != IWL_INVALID_STATION)
			return sta_id;

		/* Create new station table entry */
		sta_id = iwl_add_station_flags(priv, hdr->addr1,
						   0, CMD_ASYNC, NULL);

		if (sta_id != IWL_INVALID_STATION)
			return sta_id;

		IWL_DEBUG_DROP("Station %s not in station map. "
			       "Defaulting to broadcast...\n",
			       print_mac(mac, hdr->addr1));
		iwl_print_hex_dump(priv, IWL_DL_DROP, (u8 *) hdr, sizeof(*hdr));
		return priv->hw_params.bcast_sta_id;

	default:
		IWL_WARNING("Unknown mode of operation: %d", priv->iw_mode);
		return priv->hw_params.bcast_sta_id;
	}
}
EXPORT_SYMBOL(iwl_get_sta_id);

