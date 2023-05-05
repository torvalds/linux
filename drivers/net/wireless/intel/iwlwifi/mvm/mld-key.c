// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2022 Intel Corporation
 */
#include <linux/kernel.h>
#include <net/mac80211.h>
#include "mvm.h"
#include "fw/api/context.h"
#include "fw/api/datapath.h"

static u32 iwl_mvm_get_sec_sta_mask(struct iwl_mvm *mvm,
				    struct ieee80211_vif *vif,
				    struct ieee80211_sta *sta,
				    struct ieee80211_key_conf *keyconf)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm_vif_link_info *link_info = &mvmvif->deflink;

	lockdep_assert_held(&mvm->mutex);

	if (keyconf->link_id >= 0) {
		link_info = mvmvif->link[keyconf->link_id];
		if (!link_info)
			return 0;
	}

	/* AP group keys are per link and should be on the mcast STA */
	if (vif->type == NL80211_IFTYPE_AP &&
	    !(keyconf->flags & IEEE80211_KEY_FLAG_PAIRWISE))
		return BIT(link_info->mcast_sta.sta_id);

	/* for client mode use the AP STA also for group keys */
	if (!sta && vif->type == NL80211_IFTYPE_STATION)
		sta = mvmvif->ap_sta;

	/* During remove the STA was removed and the group keys come later
	 * (which sounds like a bad sequence, but remember that to mac80211 the
	 * group keys have no sta pointer), so we don't have a STA now.
	 * Since this happens for group keys only, just use the link_info as
	 * the group keys are per link; make sure that is the case by checking
	 * we do have a link_id or are not doing MLO.
	 * Of course the same can be done during add as well, but we must do
	 * it during remove, since we don't have the mvmvif->ap_sta pointer.
	 */
	if (!sta && (keyconf->link_id >= 0 || !vif->valid_links))
		return BIT(link_info->ap_sta_id);

	/* STA should be non-NULL now, but iwl_mvm_sta_fw_id_mask() checks */

	/* pass link_id to filter by it if not -1 (GTK on client) */
	return iwl_mvm_sta_fw_id_mask(mvm, sta, keyconf->link_id);
}

static u32 iwl_mvm_get_sec_flags(struct iwl_mvm *mvm,
				 struct ieee80211_vif *vif,
				 struct ieee80211_sta *sta,
				 struct ieee80211_key_conf *keyconf)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	u32 flags = 0;

	lockdep_assert_held(&mvm->mutex);

	if (!(keyconf->flags & IEEE80211_KEY_FLAG_PAIRWISE))
		flags |= IWL_SEC_KEY_FLAG_MCAST_KEY;

	switch (keyconf->cipher) {
	case WLAN_CIPHER_SUITE_WEP104:
		flags |= IWL_SEC_KEY_FLAG_KEY_SIZE;
		fallthrough;
	case WLAN_CIPHER_SUITE_WEP40:
		flags |= IWL_SEC_KEY_FLAG_CIPHER_WEP;
		break;
	case WLAN_CIPHER_SUITE_TKIP:
		flags |= IWL_SEC_KEY_FLAG_CIPHER_TKIP;
		break;
	case WLAN_CIPHER_SUITE_AES_CMAC:
	case WLAN_CIPHER_SUITE_CCMP:
		flags |= IWL_SEC_KEY_FLAG_CIPHER_CCMP;
		break;
	case WLAN_CIPHER_SUITE_GCMP_256:
	case WLAN_CIPHER_SUITE_BIP_GMAC_256:
		flags |= IWL_SEC_KEY_FLAG_KEY_SIZE;
		fallthrough;
	case WLAN_CIPHER_SUITE_GCMP:
	case WLAN_CIPHER_SUITE_BIP_GMAC_128:
		flags |= IWL_SEC_KEY_FLAG_CIPHER_GCMP;
		break;
	}

	if (!sta && vif->type == NL80211_IFTYPE_STATION)
		sta = mvmvif->ap_sta;

	if (!IS_ERR_OR_NULL(sta) && sta->mfp)
		flags |= IWL_SEC_KEY_FLAG_MFP;

	return flags;
}

struct iwl_mvm_sta_key_update_data {
	struct ieee80211_sta *sta;
	u32 old_sta_mask;
	u32 new_sta_mask;
	int err;
};

static void iwl_mvm_mld_update_sta_key(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif,
				       struct ieee80211_sta *sta,
				       struct ieee80211_key_conf *key,
				       void *_data)
{
	u32 cmd_id = WIDE_ID(DATA_PATH_GROUP, SEC_KEY_CMD);
	struct iwl_mvm_sta_key_update_data *data = _data;
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_sec_key_cmd cmd = {
		.action = cpu_to_le32(FW_CTXT_ACTION_MODIFY),
		.u.modify.old_sta_mask = cpu_to_le32(data->old_sta_mask),
		.u.modify.new_sta_mask = cpu_to_le32(data->new_sta_mask),
		.u.modify.key_id = cpu_to_le32(key->keyidx),
		.u.modify.key_flags =
			cpu_to_le32(iwl_mvm_get_sec_flags(mvm, vif, sta, key)),
	};
	int err;

	/* only need to do this for pairwise keys (link_id == -1) */
	if (sta != data->sta || key->link_id >= 0)
		return;

	err = iwl_mvm_send_cmd_pdu(mvm, cmd_id, CMD_ASYNC, sizeof(cmd), &cmd);

	if (err)
		data->err = err;
}

int iwl_mvm_mld_update_sta_keys(struct iwl_mvm *mvm,
				struct ieee80211_vif *vif,
				struct ieee80211_sta *sta,
				u32 old_sta_mask,
				u32 new_sta_mask)
{
	struct iwl_mvm_sta_key_update_data data = {
		.sta = sta,
		.old_sta_mask = old_sta_mask,
		.new_sta_mask = new_sta_mask,
	};

	ieee80211_iter_keys_rcu(mvm->hw, vif, iwl_mvm_mld_update_sta_key,
				&data);
	return data.err;
}

static int __iwl_mvm_sec_key_del(struct iwl_mvm *mvm, u32 sta_mask,
				 u32 key_flags, u32 keyidx, u32 flags)
{
	u32 cmd_id = WIDE_ID(DATA_PATH_GROUP, SEC_KEY_CMD);
	struct iwl_sec_key_cmd cmd = {
		.action = cpu_to_le32(FW_CTXT_ACTION_REMOVE),
		.u.remove.sta_mask = cpu_to_le32(sta_mask),
		.u.remove.key_id = cpu_to_le32(keyidx),
		.u.remove.key_flags = cpu_to_le32(key_flags),
	};

	return iwl_mvm_send_cmd_pdu(mvm, cmd_id, flags, sizeof(cmd), &cmd);
}

int iwl_mvm_sec_key_add(struct iwl_mvm *mvm,
			struct ieee80211_vif *vif,
			struct ieee80211_sta *sta,
			struct ieee80211_key_conf *keyconf)
{
	u32 sta_mask = iwl_mvm_get_sec_sta_mask(mvm, vif, sta, keyconf);
	u32 key_flags = iwl_mvm_get_sec_flags(mvm, vif, sta, keyconf);
	u32 cmd_id = WIDE_ID(DATA_PATH_GROUP, SEC_KEY_CMD);
	struct iwl_sec_key_cmd cmd = {
		.action = cpu_to_le32(FW_CTXT_ACTION_ADD),
		.u.add.sta_mask = cpu_to_le32(sta_mask),
		.u.add.key_id = cpu_to_le32(keyconf->keyidx),
		.u.add.key_flags = cpu_to_le32(key_flags),
		.u.add.tx_seq = cpu_to_le64(atomic64_read(&keyconf->tx_pn)),
	};
	int ret;

	if (WARN_ON(keyconf->keylen > sizeof(cmd.u.add.key)))
		return -EINVAL;

	if (WARN_ON(!sta_mask))
		return -EINVAL;

	if (keyconf->cipher == WLAN_CIPHER_SUITE_WEP40 ||
	    keyconf->cipher == WLAN_CIPHER_SUITE_WEP104)
		memcpy(cmd.u.add.key + IWL_SEC_WEP_KEY_OFFSET, keyconf->key,
		       keyconf->keylen);
	else
		memcpy(cmd.u.add.key, keyconf->key, keyconf->keylen);

	if (keyconf->cipher == WLAN_CIPHER_SUITE_TKIP) {
		memcpy(cmd.u.add.tkip_mic_rx_key,
		       keyconf->key + NL80211_TKIP_DATA_OFFSET_RX_MIC_KEY,
		       8);
		memcpy(cmd.u.add.tkip_mic_tx_key,
		       keyconf->key + NL80211_TKIP_DATA_OFFSET_TX_MIC_KEY,
		       8);
	}

	ret = iwl_mvm_send_cmd_pdu(mvm, cmd_id, 0, sizeof(cmd), &cmd);
	if (ret)
		return ret;

	/*
	 * For WEP, the same key is used for multicast and unicast so need to
	 * upload it again. If this fails, remove the original as well.
	 */
	if (keyconf->cipher == WLAN_CIPHER_SUITE_WEP40 ||
	    keyconf->cipher == WLAN_CIPHER_SUITE_WEP104) {
		cmd.u.add.key_flags ^= cpu_to_le32(IWL_SEC_KEY_FLAG_MCAST_KEY);
		ret = iwl_mvm_send_cmd_pdu(mvm, cmd_id, 0, sizeof(cmd), &cmd);
		if (ret)
			__iwl_mvm_sec_key_del(mvm, sta_mask, key_flags,
					      keyconf->keyidx, 0);
	}

	return ret;
}

static int _iwl_mvm_sec_key_del(struct iwl_mvm *mvm,
				struct ieee80211_vif *vif,
				struct ieee80211_sta *sta,
				struct ieee80211_key_conf *keyconf,
				u32 flags)
{
	u32 sta_mask = iwl_mvm_get_sec_sta_mask(mvm, vif, sta, keyconf);
	u32 key_flags = iwl_mvm_get_sec_flags(mvm, vif, sta, keyconf);
	int ret;

	if (WARN_ON(!sta_mask))
		return -EINVAL;

	ret = __iwl_mvm_sec_key_del(mvm, sta_mask, key_flags, keyconf->keyidx,
				    flags);
	if (ret)
		return ret;

	/* For WEP, delete the key again as unicast */
	if (keyconf->cipher == WLAN_CIPHER_SUITE_WEP40 ||
	    keyconf->cipher == WLAN_CIPHER_SUITE_WEP104) {
		key_flags ^= IWL_SEC_KEY_FLAG_MCAST_KEY;
		ret = __iwl_mvm_sec_key_del(mvm, sta_mask, key_flags,
					    keyconf->keyidx, flags);
	}

	return ret;
}

int iwl_mvm_sec_key_del(struct iwl_mvm *mvm,
			struct ieee80211_vif *vif,
			struct ieee80211_sta *sta,
			struct ieee80211_key_conf *keyconf)
{
	return _iwl_mvm_sec_key_del(mvm, vif, sta, keyconf, 0);
}

static void iwl_mvm_sec_key_remove_ap_iter(struct ieee80211_hw *hw,
					   struct ieee80211_vif *vif,
					   struct ieee80211_sta *sta,
					   struct ieee80211_key_conf *key,
					   void *data)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	unsigned int link_id = (uintptr_t)data;

	if (key->hw_key_idx == STA_KEY_IDX_INVALID)
		return;

	if (sta)
		return;

	if (key->link_id >= 0 && key->link_id != link_id)
		return;

	_iwl_mvm_sec_key_del(mvm, vif, NULL, key, CMD_ASYNC);
	key->hw_key_idx = STA_KEY_IDX_INVALID;
}

void iwl_mvm_sec_key_remove_ap(struct iwl_mvm *mvm,
			       struct ieee80211_vif *vif,
			       struct iwl_mvm_vif_link_info *link,
			       unsigned int link_id)
{
	u32 sec_key_id = WIDE_ID(DATA_PATH_GROUP, SEC_KEY_CMD);
	u8 sec_key_ver = iwl_fw_lookup_cmd_ver(mvm->fw, sec_key_id, 0);

	if (WARN_ON_ONCE(vif->type != NL80211_IFTYPE_STATION ||
			 link->ap_sta_id == IWL_MVM_INVALID_STA))
		return;

	if (!sec_key_ver)
		return;

	ieee80211_iter_keys_rcu(mvm->hw, vif,
				iwl_mvm_sec_key_remove_ap_iter,
				(void *)(uintptr_t)link_id);
}
