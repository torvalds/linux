/******************************************************************************
 *
 * Copyright(c) 2003 - 2009 Intel Corporation. All rights reserved.
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
#include <linux/etherdevice.h>

#include "iwl-dev.h"
#include "iwl-core.h"
#include "iwl-sta.h"

#define IWL_STA_DRIVER_ACTIVE BIT(0) /* driver entry is active */
#define IWL_STA_UCODE_ACTIVE  BIT(1) /* ucode entry is active */

u8 iwl_find_station(struct iwl_priv *priv, const u8 *addr)
{
	int i;
	int start = 0;
	int ret = IWL_INVALID_STATION;
	unsigned long flags;

	if ((priv->iw_mode == NL80211_IFTYPE_ADHOC) ||
	    (priv->iw_mode == NL80211_IFTYPE_AP))
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

	IWL_DEBUG_ASSOC_LIMIT(priv, "can not find STA %pM total %d\n",
			      addr, priv->num_stations);

 out:
	spin_unlock_irqrestore(&priv->sta_lock, flags);
	return ret;
}
EXPORT_SYMBOL(iwl_find_station);

int iwl_get_ra_sta_id(struct iwl_priv *priv, struct ieee80211_hdr *hdr)
{
	if (priv->iw_mode == NL80211_IFTYPE_STATION) {
		return IWL_AP_ID;
	} else {
		u8 *da = ieee80211_get_DA(hdr);
		return iwl_find_station(priv, da);
	}
}
EXPORT_SYMBOL(iwl_get_ra_sta_id);

static void iwl_sta_ucode_activate(struct iwl_priv *priv, u8 sta_id)
{
	unsigned long flags;

	spin_lock_irqsave(&priv->sta_lock, flags);

	if (!(priv->stations[sta_id].used & IWL_STA_DRIVER_ACTIVE))
		IWL_ERR(priv, "ACTIVATE a non DRIVER active station %d\n",
			sta_id);

	priv->stations[sta_id].used |= IWL_STA_UCODE_ACTIVE;
	IWL_DEBUG_ASSOC(priv, "Added STA to Ucode: %pM\n",
			priv->stations[sta_id].sta.sta.addr);

	spin_unlock_irqrestore(&priv->sta_lock, flags);
}

static void iwl_add_sta_callback(struct iwl_priv *priv,
				 struct iwl_device_cmd *cmd,
				 struct iwl_rx_packet *pkt)
{
	struct iwl_addsta_cmd *addsta =
		(struct iwl_addsta_cmd *)cmd->cmd.payload;
	u8 sta_id = addsta->sta.sta_id;

	if (pkt->hdr.flags & IWL_CMD_FAILED_MSK) {
		IWL_ERR(priv, "Bad return from REPLY_ADD_STA (0x%08X)\n",
			  pkt->hdr.flags);
		return;
	}

	switch (pkt->u.add_sta.status) {
	case ADD_STA_SUCCESS_MSK:
		iwl_sta_ucode_activate(priv, sta_id);
		 /* fall through */
	default:
		IWL_DEBUG_HC(priv, "Received REPLY_ADD_STA:(0x%08X)\n",
			     pkt->u.add_sta.status);
		break;
	}
}

int iwl_send_add_sta(struct iwl_priv *priv,
		     struct iwl_addsta_cmd *sta, u8 flags)
{
	struct iwl_rx_packet *pkt = NULL;
	int ret = 0;
	u8 data[sizeof(*sta)];
	struct iwl_host_cmd cmd = {
		.id = REPLY_ADD_STA,
		.flags = flags,
		.data = data,
	};

	if (flags & CMD_ASYNC)
		cmd.callback = iwl_add_sta_callback;
	else
		cmd.flags |= CMD_WANT_SKB;

	cmd.len = priv->cfg->ops->utils->build_addsta_hcmd(sta, data);
	ret = iwl_send_cmd(priv, &cmd);

	if (ret || (flags & CMD_ASYNC))
		return ret;

	pkt = (struct iwl_rx_packet *)cmd.reply_page;
	if (pkt->hdr.flags & IWL_CMD_FAILED_MSK) {
		IWL_ERR(priv, "Bad return from REPLY_ADD_STA (0x%08X)\n",
			  pkt->hdr.flags);
		ret = -EIO;
	}

	if (ret == 0) {
		switch (pkt->u.add_sta.status) {
		case ADD_STA_SUCCESS_MSK:
			iwl_sta_ucode_activate(priv, sta->sta.sta_id);
			IWL_DEBUG_INFO(priv, "REPLY_ADD_STA PASSED\n");
			break;
		default:
			ret = -EIO;
			IWL_WARN(priv, "REPLY_ADD_STA failed\n");
			break;
		}
	}

	priv->alloc_rxb_page--;
	free_pages(cmd.reply_page, priv->hw_params.rx_page_order);

	return ret;
}
EXPORT_SYMBOL(iwl_send_add_sta);

static void iwl_set_ht_add_station(struct iwl_priv *priv, u8 index,
				   struct ieee80211_sta_ht_cap *sta_ht_inf)
{
	__le32 sta_flags;
	u8 mimo_ps_mode;

	if (!sta_ht_inf || !sta_ht_inf->ht_supported)
		goto done;

	mimo_ps_mode = (sta_ht_inf->cap & IEEE80211_HT_CAP_SM_PS) >> 2;
	IWL_DEBUG_ASSOC(priv, "spatial multiplexing power save mode: %s\n",
			(mimo_ps_mode == WLAN_HT_CAP_SM_PS_STATIC) ?
			"static" :
			(mimo_ps_mode == WLAN_HT_CAP_SM_PS_DYNAMIC) ?
			"dynamic" : "disabled");

	sta_flags = priv->stations[index].sta.station_flags;

	sta_flags &= ~(STA_FLG_RTS_MIMO_PROT_MSK | STA_FLG_MIMO_DIS_MSK);

	switch (mimo_ps_mode) {
	case WLAN_HT_CAP_SM_PS_STATIC:
		sta_flags |= STA_FLG_MIMO_DIS_MSK;
		break;
	case WLAN_HT_CAP_SM_PS_DYNAMIC:
		sta_flags |= STA_FLG_RTS_MIMO_PROT_MSK;
		break;
	case WLAN_HT_CAP_SM_PS_DISABLED:
		break;
	default:
		IWL_WARN(priv, "Invalid MIMO PS mode %d\n", mimo_ps_mode);
		break;
	}

	sta_flags |= cpu_to_le32(
	      (u32)sta_ht_inf->ampdu_factor << STA_FLG_MAX_AGG_SIZE_POS);

	sta_flags |= cpu_to_le32(
	      (u32)sta_ht_inf->ampdu_density << STA_FLG_AGG_MPDU_DENSITY_POS);

	if (iwl_is_ht40_tx_allowed(priv, sta_ht_inf))
		sta_flags |= STA_FLG_HT40_EN_MSK;
	else
		sta_flags &= ~STA_FLG_HT40_EN_MSK;

	priv->stations[index].sta.station_flags = sta_flags;
 done:
	return;
}

/**
 * iwl_add_station - Add station to tables in driver and device
 */
u8 iwl_add_station(struct iwl_priv *priv, const u8 *addr, bool is_ap, u8 flags,
		struct ieee80211_sta_ht_cap *ht_info)
{
	struct iwl_station_entry *station;
	unsigned long flags_spin;
	int i;
	int sta_id = IWL_INVALID_STATION;
	u16 rate;

	spin_lock_irqsave(&priv->sta_lock, flags_spin);
	if (is_ap)
		sta_id = IWL_AP_ID;
	else if (is_broadcast_ether_addr(addr))
		sta_id = priv->hw_params.bcast_sta_id;
	else
		for (i = IWL_STA_ID; i < priv->hw_params.max_stations; i++) {
			if (!compare_ether_addr(priv->stations[i].sta.sta.addr,
						addr)) {
				sta_id = i;
				break;
			}

			if (!priv->stations[i].used &&
			    sta_id == IWL_INVALID_STATION)
				sta_id = i;
		}

	/* These two conditions have the same outcome, but keep them separate
	   since they have different meanings */
	if (unlikely(sta_id == IWL_INVALID_STATION)) {
		spin_unlock_irqrestore(&priv->sta_lock, flags_spin);
		return sta_id;
	}

	if (priv->stations[sta_id].used &&
	    !compare_ether_addr(priv->stations[sta_id].sta.sta.addr, addr)) {
		spin_unlock_irqrestore(&priv->sta_lock, flags_spin);
		return sta_id;
	}


	station = &priv->stations[sta_id];
	station->used = IWL_STA_DRIVER_ACTIVE;
	IWL_DEBUG_ASSOC(priv, "Add STA to driver ID %d: %pM\n",
			sta_id, addr);
	priv->num_stations++;

	/* Set up the REPLY_ADD_STA command to send to device */
	memset(&station->sta, 0, sizeof(struct iwl_addsta_cmd));
	memcpy(station->sta.sta.addr, addr, ETH_ALEN);
	station->sta.mode = 0;
	station->sta.sta.sta_id = sta_id;
	station->sta.station_flags = 0;

	/* BCAST station and IBSS stations do not work in HT mode */
	if (sta_id != priv->hw_params.bcast_sta_id &&
	    priv->iw_mode != NL80211_IFTYPE_ADHOC)
		iwl_set_ht_add_station(priv, sta_id, ht_info);

	/* 3945 only */
	rate = (priv->band == IEEE80211_BAND_5GHZ) ?
		IWL_RATE_6M_PLCP : IWL_RATE_1M_PLCP;
	/* Turn on both antennas for the station... */
	station->sta.rate_n_flags = cpu_to_le16(rate | RATE_MCS_ANT_AB_MSK);

	spin_unlock_irqrestore(&priv->sta_lock, flags_spin);

	/* Add station to device's station table */
	iwl_send_add_sta(priv, &station->sta, flags);
	return sta_id;

}
EXPORT_SYMBOL(iwl_add_station);

static void iwl_sta_ucode_deactivate(struct iwl_priv *priv, const char *addr)
{
	unsigned long flags;
	u8 sta_id = iwl_find_station(priv, addr);

	BUG_ON(sta_id == IWL_INVALID_STATION);

	IWL_DEBUG_ASSOC(priv, "Removed STA from Ucode: %pM\n", addr);

	spin_lock_irqsave(&priv->sta_lock, flags);

	/* Ucode must be active and driver must be non active */
	if (priv->stations[sta_id].used != IWL_STA_UCODE_ACTIVE)
		IWL_ERR(priv, "removed non active STA %d\n", sta_id);

	priv->stations[sta_id].used &= ~IWL_STA_UCODE_ACTIVE;

	memset(&priv->stations[sta_id], 0, sizeof(struct iwl_station_entry));
	spin_unlock_irqrestore(&priv->sta_lock, flags);
}

static void iwl_remove_sta_callback(struct iwl_priv *priv,
				    struct iwl_device_cmd *cmd,
				    struct iwl_rx_packet *pkt)
{
	struct iwl_rem_sta_cmd *rm_sta =
			(struct iwl_rem_sta_cmd *)cmd->cmd.payload;
	const char *addr = rm_sta->addr;

	if (pkt->hdr.flags & IWL_CMD_FAILED_MSK) {
		IWL_ERR(priv, "Bad return from REPLY_REMOVE_STA (0x%08X)\n",
		pkt->hdr.flags);
		return;
	}

	switch (pkt->u.rem_sta.status) {
	case REM_STA_SUCCESS_MSK:
		iwl_sta_ucode_deactivate(priv, addr);
		break;
	default:
		IWL_ERR(priv, "REPLY_REMOVE_STA failed\n");
		break;
	}
}

static int iwl_send_remove_station(struct iwl_priv *priv, const u8 *addr,
				   u8 flags)
{
	struct iwl_rx_packet *pkt;
	int ret;

	struct iwl_rem_sta_cmd rm_sta_cmd;

	struct iwl_host_cmd cmd = {
		.id = REPLY_REMOVE_STA,
		.len = sizeof(struct iwl_rem_sta_cmd),
		.flags = flags,
		.data = &rm_sta_cmd,
	};

	memset(&rm_sta_cmd, 0, sizeof(rm_sta_cmd));
	rm_sta_cmd.num_sta = 1;
	memcpy(&rm_sta_cmd.addr, addr , ETH_ALEN);

	if (flags & CMD_ASYNC)
		cmd.callback = iwl_remove_sta_callback;
	else
		cmd.flags |= CMD_WANT_SKB;
	ret = iwl_send_cmd(priv, &cmd);

	if (ret || (flags & CMD_ASYNC))
		return ret;

	pkt = (struct iwl_rx_packet *)cmd.reply_page;
	if (pkt->hdr.flags & IWL_CMD_FAILED_MSK) {
		IWL_ERR(priv, "Bad return from REPLY_REMOVE_STA (0x%08X)\n",
			  pkt->hdr.flags);
		ret = -EIO;
	}

	if (!ret) {
		switch (pkt->u.rem_sta.status) {
		case REM_STA_SUCCESS_MSK:
			iwl_sta_ucode_deactivate(priv, addr);
			IWL_DEBUG_ASSOC(priv, "REPLY_REMOVE_STA PASSED\n");
			break;
		default:
			ret = -EIO;
			IWL_ERR(priv, "REPLY_REMOVE_STA failed\n");
			break;
		}
	}

	priv->alloc_rxb_page--;
	free_pages(cmd.reply_page, priv->hw_params.rx_page_order);

	return ret;
}

/**
 * iwl_remove_station - Remove driver's knowledge of station.
 */
int iwl_remove_station(struct iwl_priv *priv, const u8 *addr, bool is_ap)
{
	int sta_id = IWL_INVALID_STATION;
	int i, ret = -EINVAL;
	unsigned long flags;

	spin_lock_irqsave(&priv->sta_lock, flags);

	if (is_ap)
		sta_id = IWL_AP_ID;
	else if (is_broadcast_ether_addr(addr))
		sta_id = priv->hw_params.bcast_sta_id;
	else
		for (i = IWL_STA_ID; i < priv->hw_params.max_stations; i++)
			if (priv->stations[i].used &&
			    !compare_ether_addr(priv->stations[i].sta.sta.addr,
						addr)) {
				sta_id = i;
				break;
			}

	if (unlikely(sta_id == IWL_INVALID_STATION))
		goto out;

	IWL_DEBUG_ASSOC(priv, "Removing STA from driver:%d  %pM\n",
		sta_id, addr);

	if (!(priv->stations[sta_id].used & IWL_STA_DRIVER_ACTIVE)) {
		IWL_ERR(priv, "Removing %pM but non DRIVER active\n",
				addr);
		goto out;
	}

	if (!(priv->stations[sta_id].used & IWL_STA_UCODE_ACTIVE)) {
		IWL_ERR(priv, "Removing %pM but non UCODE active\n",
				addr);
		goto out;
	}


	priv->stations[sta_id].used &= ~IWL_STA_DRIVER_ACTIVE;

	priv->num_stations--;

	BUG_ON(priv->num_stations < 0);

	spin_unlock_irqrestore(&priv->sta_lock, flags);

	ret = iwl_send_remove_station(priv, addr, CMD_ASYNC);
	return ret;
out:
	spin_unlock_irqrestore(&priv->sta_lock, flags);
	return ret;
}

/**
 * iwl_clear_stations_table - Clear the driver's station table
 *
 * NOTE:  This does not clear or otherwise alter the device's station table.
 */
void iwl_clear_stations_table(struct iwl_priv *priv)
{
	unsigned long flags;
	int i;

	spin_lock_irqsave(&priv->sta_lock, flags);

	if (iwl_is_alive(priv) &&
	   !test_bit(STATUS_EXIT_PENDING, &priv->status) &&
	   iwl_send_cmd_pdu_async(priv, REPLY_REMOVE_ALL_STA, 0, NULL, NULL))
		IWL_ERR(priv, "Couldn't clear the station table\n");

	priv->num_stations = 0;
	memset(priv->stations, 0, sizeof(priv->stations));

	/* clean ucode key table bit map */
	priv->ucode_key_table = 0;

	/* keep track of static keys */
	for (i = 0; i < WEP_KEYS_MAX ; i++) {
		if (priv->wep_keys[i].key_size)
			set_bit(i, &priv->ucode_key_table);
	}

	spin_unlock_irqrestore(&priv->sta_lock, flags);
}
EXPORT_SYMBOL(iwl_clear_stations_table);

int iwl_get_free_ucode_key_index(struct iwl_priv *priv)
{
	int i;

	for (i = 0; i < STA_KEY_MAX_NUM; i++)
		if (!test_and_set_bit(i, &priv->ucode_key_table))
			return i;

	return WEP_INVALID_OFFSET;
}
EXPORT_SYMBOL(iwl_get_free_ucode_key_index);

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
		.flags = CMD_ASYNC,
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
	IWL_DEBUG_WEP(priv, "Removing default WEP key: idx=%d\n",
		      keyconf->keyidx);

	if (!test_and_clear_bit(keyconf->keyidx, &priv->ucode_key_table))
		IWL_ERR(priv, "index %d not used in uCode key table.\n",
			  keyconf->keyidx);

	priv->default_wep_key--;
	memset(&priv->wep_keys[keyconf->keyidx], 0, sizeof(priv->wep_keys[0]));
	if (iwl_is_rfkill(priv)) {
		IWL_DEBUG_WEP(priv, "Not sending REPLY_WEPKEY command due to RFKILL.\n");
		spin_unlock_irqrestore(&priv->sta_lock, flags);
		return 0;
	}
	ret = iwl_send_static_wepkey_cmd(priv, 1);
	IWL_DEBUG_WEP(priv, "Remove default WEP key: idx=%d ret=%d\n",
		      keyconf->keyidx, ret);
	spin_unlock_irqrestore(&priv->sta_lock, flags);

	return ret;
}
EXPORT_SYMBOL(iwl_remove_default_wep_key);

int iwl_set_default_wep_key(struct iwl_priv *priv,
			    struct ieee80211_key_conf *keyconf)
{
	int ret;
	unsigned long flags;

	if (keyconf->keylen != WEP_KEY_LEN_128 &&
	    keyconf->keylen != WEP_KEY_LEN_64) {
		IWL_DEBUG_WEP(priv, "Bad WEP key length %d\n", keyconf->keylen);
		return -EINVAL;
	}

	keyconf->flags &= ~IEEE80211_KEY_FLAG_GENERATE_IV;
	keyconf->hw_key_idx = HW_KEY_DEFAULT;
	priv->stations[IWL_AP_ID].keyinfo.alg = ALG_WEP;

	spin_lock_irqsave(&priv->sta_lock, flags);
	priv->default_wep_key++;

	if (test_and_set_bit(keyconf->keyidx, &priv->ucode_key_table))
		IWL_ERR(priv, "index %d already used in uCode key table.\n",
			keyconf->keyidx);

	priv->wep_keys[keyconf->keyidx].key_size = keyconf->keylen;
	memcpy(&priv->wep_keys[keyconf->keyidx].key, &keyconf->key,
							keyconf->keylen);

	ret = iwl_send_static_wepkey_cmd(priv, 0);
	IWL_DEBUG_WEP(priv, "Set default WEP key: len=%d idx=%d ret=%d\n",
		keyconf->keylen, keyconf->keyidx, ret);
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

	WARN(priv->stations[sta_id].sta.key.key_offset == WEP_INVALID_OFFSET,
		"no space for a new key");

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
	int ret;

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

	WARN(priv->stations[sta_id].sta.key.key_offset == WEP_INVALID_OFFSET,
		"no space for a new key");

	priv->stations[sta_id].sta.key.key_flags = key_flags;
	priv->stations[sta_id].sta.sta.modify_mask = STA_MODIFY_KEY_MASK;
	priv->stations[sta_id].sta.mode = STA_CONTROL_MODIFY_MSK;

	ret = iwl_send_add_sta(priv, &priv->stations[sta_id].sta, CMD_ASYNC);

	spin_unlock_irqrestore(&priv->sta_lock, flags);

	return ret;
}

static int iwl_set_tkip_dynamic_key_info(struct iwl_priv *priv,
				   struct ieee80211_key_conf *keyconf,
				   u8 sta_id)
{
	unsigned long flags;
	int ret = 0;
	__le16 key_flags = 0;

	key_flags |= (STA_KEY_FLG_TKIP | STA_KEY_FLG_MAP_KEY_MSK);
	key_flags |= cpu_to_le16(keyconf->keyidx << STA_KEY_FLG_KEYID_POS);
	key_flags &= ~STA_KEY_FLG_INVALID;

	if (sta_id == priv->hw_params.bcast_sta_id)
		key_flags |= STA_KEY_MULTICAST_MSK;

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
			struct ieee80211_key_conf *keyconf,
			const u8 *addr, u32 iv32, u16 *phase1key)
{
	u8 sta_id = IWL_INVALID_STATION;
	unsigned long flags;
	int i;

	sta_id = iwl_find_station(priv, addr);
	if (sta_id == IWL_INVALID_STATION) {
		IWL_DEBUG_MAC80211(priv, "leave - %pM not in station map.\n",
				   addr);
		return;
	}

	if (iwl_scan_cancel(priv)) {
		/* cancel scan failed, just live w/ bad key and rely
		   briefly on SW decryption */
		return;
	}

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
EXPORT_SYMBOL(iwl_update_tkip_key);

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
		IWL_DEBUG_WEP(priv, "Not sending REPLY_ADD_STA command because RFKILL enabled. \n");
		spin_unlock_irqrestore(&priv->sta_lock, flags);
		return 0;
	}
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
		IWL_ERR(priv,
			"Unknown alg: %s alg = %d\n", __func__, keyconf->alg);
		ret = -EINVAL;
	}

	IWL_DEBUG_WEP(priv, "Set dynamic key: alg= %d len=%d idx=%d sta=%d ret=%d\n",
		      keyconf->alg, keyconf->keylen, keyconf->keyidx,
		      sta_id, ret);

	return ret;
}
EXPORT_SYMBOL(iwl_set_dynamic_key);

#ifdef CONFIG_IWLWIFI_DEBUG
static void iwl_dump_lq_cmd(struct iwl_priv *priv,
			   struct iwl_link_quality_cmd *lq)
{
	int i;
	IWL_DEBUG_RATE(priv, "lq station id 0x%x\n", lq->sta_id);
	IWL_DEBUG_RATE(priv, "lq ant 0x%X 0x%X\n",
		       lq->general_params.single_stream_ant_msk,
		       lq->general_params.dual_stream_ant_msk);

	for (i = 0; i < LINK_QUAL_MAX_RETRY_NUM; i++)
		IWL_DEBUG_RATE(priv, "lq index %d 0x%X\n",
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
		.flags = flags,
		.data = lq,
	};

	if ((lq->sta_id == 0xFF) &&
	    (priv->iw_mode == NL80211_IFTYPE_ADHOC))
		return -EINVAL;

	if (lq->sta_id == 0xFF)
		lq->sta_id = IWL_AP_ID;

	iwl_dump_lq_cmd(priv, lq);

	if (iwl_is_associated(priv) && priv->assoc_station_added)
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
 *       if the driver's iwl-agn-rs rate scaling algorithm is used, instead of
 *       rc80211_simple.
 *
 * NOTE: Run REPLY_ADD_STA command to set up station table entry, before
 *       calling this function (which runs REPLY_TX_LINK_QUALITY_CMD,
 *       which requires station table entry to exist).
 */
static void iwl_sta_init_lq(struct iwl_priv *priv, const u8 *addr, bool is_ap)
{
	int i, r;
	struct iwl_link_quality_cmd link_cmd = {
		.reserved1 = 0,
	};
	u32 rate_flags;

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

		rate_flags |= first_antenna(priv->hw_params.valid_tx_ant) <<
				RATE_MCS_ANT_POS;

		link_cmd.rs_table[i].rate_n_flags =
			iwl_hw_set_rate_n_flags(iwl_rates[r].plcp, rate_flags);
		r = iwl_get_prev_ieee_rate(r);
	}

	link_cmd.general_params.single_stream_ant_msk =
				first_antenna(priv->hw_params.valid_tx_ant);
	link_cmd.general_params.dual_stream_ant_msk = 3;
	link_cmd.agg_params.agg_dis_start_th = LINK_QUAL_AGG_DISABLE_START_DEF;
	link_cmd.agg_params.agg_time_limit =
		cpu_to_le16(LINK_QUAL_AGG_TIME_LIMIT_DEF);

	/* Update the rate scaling for control frame Tx to AP */
	link_cmd.sta_id = is_ap ? IWL_AP_ID : priv->hw_params.bcast_sta_id;

	iwl_send_cmd_pdu_async(priv, REPLY_TX_LINK_QUALITY_CMD,
			       sizeof(link_cmd), &link_cmd, NULL);
}

/**
 * iwl_rxon_add_station - add station into station table.
 *
 * there is only one AP station with id= IWL_AP_ID
 * NOTE: mutex must be held before calling this function
 */
int iwl_rxon_add_station(struct iwl_priv *priv, const u8 *addr, bool is_ap)
{
	struct ieee80211_sta *sta;
	struct ieee80211_sta_ht_cap ht_config;
	struct ieee80211_sta_ht_cap *cur_ht_config = NULL;
	u8 sta_id;

	/* Add station to device's station table */

	/*
	 * XXX: This check is definitely not correct, if we're an AP
	 *	it'll always be false which is not what we want, but
	 *	it doesn't look like iwlagn is prepared to be an HT
	 *	AP anyway.
	 */
	if (priv->current_ht_config.is_ht) {
		rcu_read_lock();
		sta = ieee80211_find_sta(priv->hw, addr);
		if (sta) {
			memcpy(&ht_config, &sta->ht_cap, sizeof(ht_config));
			cur_ht_config = &ht_config;
		}
		rcu_read_unlock();
	}

	sta_id = iwl_add_station(priv, addr, is_ap, CMD_SYNC, cur_ht_config);

	/* Set up default rate scaling table in device's station table */
	iwl_sta_init_lq(priv, addr, is_ap);

	return sta_id;
}
EXPORT_SYMBOL(iwl_rxon_add_station);

/**
 * iwl_sta_init_bcast_lq - Initialize a bcast station's hardware rate table
 *
 * NOTE: Run REPLY_ADD_STA command to set up station table entry, before
 *       calling this function (which runs REPLY_TX_LINK_QUALITY_CMD,
 *       which requires station table entry to exist).
 */
static void iwl_sta_init_bcast_lq(struct iwl_priv *priv)
{
	int i, r;
	struct iwl_link_quality_cmd link_cmd = {
		.reserved1 = 0,
	};
	u32 rate_flags;

	/* Set up the rate scaling to start at selected rate, fall back
	 * all the way down to 1M in IEEE order, and then spin on 1M */
	if (priv->band == IEEE80211_BAND_5GHZ)
		r = IWL_RATE_6M_INDEX;
	else
		r = IWL_RATE_1M_INDEX;

	for (i = 0; i < LINK_QUAL_MAX_RETRY_NUM; i++) {
		rate_flags = 0;
		if (r >= IWL_FIRST_CCK_RATE && r <= IWL_LAST_CCK_RATE)
			rate_flags |= RATE_MCS_CCK_MSK;

		rate_flags |= first_antenna(priv->hw_params.valid_tx_ant) <<
				RATE_MCS_ANT_POS;

		link_cmd.rs_table[i].rate_n_flags =
			iwl_hw_set_rate_n_flags(iwl_rates[r].plcp, rate_flags);
		r = iwl_get_prev_ieee_rate(r);
	}

	link_cmd.general_params.single_stream_ant_msk =
				first_antenna(priv->hw_params.valid_tx_ant);
	link_cmd.general_params.dual_stream_ant_msk = 3;
	link_cmd.agg_params.agg_dis_start_th = LINK_QUAL_AGG_DISABLE_START_DEF;
	link_cmd.agg_params.agg_time_limit =
		cpu_to_le16(LINK_QUAL_AGG_TIME_LIMIT_DEF);

	/* Update the rate scaling for control frame Tx to AP */
	link_cmd.sta_id = priv->hw_params.bcast_sta_id;

	iwl_send_cmd_pdu_async(priv, REPLY_TX_LINK_QUALITY_CMD,
			       sizeof(link_cmd), &link_cmd, NULL);
}


/**
 * iwl_add_bcast_station - add broadcast station into station table.
 */
void iwl_add_bcast_station(struct iwl_priv *priv)
{
	iwl_add_station(priv, iwl_bcast_addr, false, CMD_SYNC, NULL);

	/* Set up default rate scaling table in device's station table */
	iwl_sta_init_bcast_lq(priv);
}
EXPORT_SYMBOL(iwl_add_bcast_station);

/**
 * iwl_get_sta_id - Find station's index within station table
 *
 * If new IBSS station, create new entry in station table
 */
int iwl_get_sta_id(struct iwl_priv *priv, struct ieee80211_hdr *hdr)
{
	int sta_id;
	__le16 fc = hdr->frame_control;

	/* If this frame is broadcast or management, use broadcast station id */
	if (!ieee80211_is_data(fc) ||  is_multicast_ether_addr(hdr->addr1))
		return priv->hw_params.bcast_sta_id;

	switch (priv->iw_mode) {

	/* If we are a client station in a BSS network, use the special
	 * AP station entry (that's the only station we communicate with) */
	case NL80211_IFTYPE_STATION:
		return IWL_AP_ID;

	/* If we are an AP, then find the station, or use BCAST */
	case NL80211_IFTYPE_AP:
		sta_id = iwl_find_station(priv, hdr->addr1);
		if (sta_id != IWL_INVALID_STATION)
			return sta_id;
		return priv->hw_params.bcast_sta_id;

	/* If this frame is going out to an IBSS network, find the station,
	 * or create a new station table entry */
	case NL80211_IFTYPE_ADHOC:
		sta_id = iwl_find_station(priv, hdr->addr1);
		if (sta_id != IWL_INVALID_STATION)
			return sta_id;

		/* Create new station table entry */
		sta_id = iwl_add_station(priv, hdr->addr1, false,
					CMD_ASYNC, NULL);

		if (sta_id != IWL_INVALID_STATION)
			return sta_id;

		IWL_DEBUG_DROP(priv, "Station %pM not in station map. "
			       "Defaulting to broadcast...\n",
			       hdr->addr1);
		iwl_print_hex_dump(priv, IWL_DL_DROP, (u8 *) hdr, sizeof(*hdr));
		return priv->hw_params.bcast_sta_id;

	default:
		IWL_WARN(priv, "Unknown mode of operation: %d\n",
			priv->iw_mode);
		return priv->hw_params.bcast_sta_id;
	}
}
EXPORT_SYMBOL(iwl_get_sta_id);

/**
 * iwl_sta_tx_modify_enable_tid - Enable Tx for this TID in station table
 */
void iwl_sta_tx_modify_enable_tid(struct iwl_priv *priv, int sta_id, int tid)
{
	unsigned long flags;

	/* Remove "disable" flag, to enable Tx for this TID */
	spin_lock_irqsave(&priv->sta_lock, flags);
	priv->stations[sta_id].sta.sta.modify_mask = STA_MODIFY_TID_DISABLE_TX;
	priv->stations[sta_id].sta.tid_disable_tx &= cpu_to_le16(~(1 << tid));
	priv->stations[sta_id].sta.mode = STA_CONTROL_MODIFY_MSK;
	spin_unlock_irqrestore(&priv->sta_lock, flags);

	iwl_send_add_sta(priv, &priv->stations[sta_id].sta, CMD_ASYNC);
}
EXPORT_SYMBOL(iwl_sta_tx_modify_enable_tid);

int iwl_sta_rx_agg_start(struct iwl_priv *priv,
			 const u8 *addr, int tid, u16 ssn)
{
	unsigned long flags;
	int sta_id;

	sta_id = iwl_find_station(priv, addr);
	if (sta_id == IWL_INVALID_STATION)
		return -ENXIO;

	spin_lock_irqsave(&priv->sta_lock, flags);
	priv->stations[sta_id].sta.station_flags_msk = 0;
	priv->stations[sta_id].sta.sta.modify_mask = STA_MODIFY_ADDBA_TID_MSK;
	priv->stations[sta_id].sta.add_immediate_ba_tid = (u8)tid;
	priv->stations[sta_id].sta.add_immediate_ba_ssn = cpu_to_le16(ssn);
	priv->stations[sta_id].sta.mode = STA_CONTROL_MODIFY_MSK;
	spin_unlock_irqrestore(&priv->sta_lock, flags);

	return iwl_send_add_sta(priv, &priv->stations[sta_id].sta,
					CMD_ASYNC);
}
EXPORT_SYMBOL(iwl_sta_rx_agg_start);

int iwl_sta_rx_agg_stop(struct iwl_priv *priv, const u8 *addr, int tid)
{
	unsigned long flags;
	int sta_id;

	sta_id = iwl_find_station(priv, addr);
	if (sta_id == IWL_INVALID_STATION) {
		IWL_ERR(priv, "Invalid station for AGG tid %d\n", tid);
		return -ENXIO;
	}

	spin_lock_irqsave(&priv->sta_lock, flags);
	priv->stations[sta_id].sta.station_flags_msk = 0;
	priv->stations[sta_id].sta.sta.modify_mask = STA_MODIFY_DELBA_TID_MSK;
	priv->stations[sta_id].sta.remove_immediate_ba_tid = (u8)tid;
	priv->stations[sta_id].sta.mode = STA_CONTROL_MODIFY_MSK;
	spin_unlock_irqrestore(&priv->sta_lock, flags);

	return iwl_send_add_sta(priv, &priv->stations[sta_id].sta,
					CMD_ASYNC);
}
EXPORT_SYMBOL(iwl_sta_rx_agg_stop);

static void iwl_sta_modify_ps_wake(struct iwl_priv *priv, int sta_id)
{
	unsigned long flags;

	spin_lock_irqsave(&priv->sta_lock, flags);
	priv->stations[sta_id].sta.station_flags &= ~STA_FLG_PWR_SAVE_MSK;
	priv->stations[sta_id].sta.station_flags_msk = STA_FLG_PWR_SAVE_MSK;
	priv->stations[sta_id].sta.sta.modify_mask = 0;
	priv->stations[sta_id].sta.mode = STA_CONTROL_MODIFY_MSK;
	spin_unlock_irqrestore(&priv->sta_lock, flags);

	iwl_send_add_sta(priv, &priv->stations[sta_id].sta, CMD_ASYNC);
}

void iwl_update_ps_mode(struct iwl_priv *priv, u16 ps_bit, u8 *addr)
{
	/* FIXME: need locking over ps_status ??? */
	u8 sta_id = iwl_find_station(priv, addr);

	if (sta_id != IWL_INVALID_STATION) {
		u8 sta_awake = priv->stations[sta_id].
				ps_status == STA_PS_STATUS_WAKE;

		if (sta_awake && ps_bit)
			priv->stations[sta_id].ps_status = STA_PS_STATUS_SLEEP;
		else if (!sta_awake && !ps_bit) {
			iwl_sta_modify_ps_wake(priv, sta_id);
			priv->stations[sta_id].ps_status = STA_PS_STATUS_WAKE;
		}
	}
}

