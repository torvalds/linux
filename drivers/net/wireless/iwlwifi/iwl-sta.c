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
	keyconf->hw_key_idx = keyconf->keyidx;
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
	keyconf->hw_key_idx = keyconf->keyidx;

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
	keyconf->hw_key_idx = keyconf->keyidx;

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
	keyconf->hw_key_idx = keyconf->keyidx;

	spin_lock_irqsave(&priv->sta_lock, flags);

	priv->stations[sta_id].keyinfo.alg = keyconf->alg;
	priv->stations[sta_id].keyinfo.conf = keyconf;
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

	priv->key_mapping_key = 0;

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
	ret =  iwl_send_add_sta(priv, &priv->stations[sta_id].sta, 0);
	spin_unlock_irqrestore(&priv->sta_lock, flags);
	return ret;
}
EXPORT_SYMBOL(iwl_remove_dynamic_key);

int iwl_set_dynamic_key(struct iwl_priv *priv,
				struct ieee80211_key_conf *key, u8 sta_id)
{
	int ret;

	priv->key_mapping_key = 1;

	switch (key->alg) {
	case ALG_CCMP:
		ret = iwl_set_ccmp_dynamic_key_info(priv, key, sta_id);
		break;
	case ALG_TKIP:
		ret = iwl_set_tkip_dynamic_key_info(priv, key, sta_id);
		break;
	case ALG_WEP:
		ret = iwl_set_wep_dynamic_key_info(priv, key, sta_id);
		break;
	default:
		IWL_ERROR("Unknown alg: %s alg = %d\n", __func__, key->alg);
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

