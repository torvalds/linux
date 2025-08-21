// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2024 Intel Corporation
 */
#include "key.h"
#include "iface.h"
#include "sta.h"
#include "fw/api/datapath.h"

static u32 iwl_mld_get_key_flags(struct iwl_mld *mld,
				 struct ieee80211_vif *vif,
				 struct ieee80211_sta *sta,
				 struct ieee80211_key_conf *key)
{
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);
	bool pairwise = key->flags & IEEE80211_KEY_FLAG_PAIRWISE;
	bool igtk = key->keyidx == 4 || key->keyidx == 5;
	u32 flags = 0;

	if (!pairwise)
		flags |= IWL_SEC_KEY_FLAG_MCAST_KEY;

	switch (key->cipher) {
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
		sta = mld_vif->ap_sta;

	/* If we are installing an iGTK (in AP or STA mode), we need to tell
	 * the firmware this key will en/decrypt MGMT frames.
	 * Same goes if we are installing a pairwise key for an MFP station.
	 * In case we're installing a groupwise key (which is not an iGTK),
	 * then, we will not use this key for MGMT frames.
	 */
	if ((sta && sta->mfp && pairwise) || igtk)
		flags |= IWL_SEC_KEY_FLAG_MFP;

	if (key->flags & IEEE80211_KEY_FLAG_SPP_AMSDU)
		flags |= IWL_SEC_KEY_FLAG_SPP_AMSDU;

	return flags;
}

static u32 iwl_mld_get_key_sta_mask(struct iwl_mld *mld,
				    struct ieee80211_vif *vif,
				    struct ieee80211_sta *sta,
				    struct ieee80211_key_conf *key)
{
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);
	struct ieee80211_link_sta *link_sta;
	int sta_id;

	lockdep_assert_wiphy(mld->wiphy);

	/* AP group keys are per link and should be on the mcast/bcast STA */
	if (vif->type == NL80211_IFTYPE_AP &&
	    !(key->flags & IEEE80211_KEY_FLAG_PAIRWISE)) {
		struct iwl_mld_link *link = NULL;

		if (key->link_id >= 0)
			link = iwl_mld_link_dereference_check(mld_vif,
							      key->link_id);

		if (WARN_ON(!link))
			return 0;

		/* In this stage we should have both the bcast and mcast STAs */
		if (WARN_ON(link->bcast_sta.sta_id == IWL_INVALID_STA ||
			    link->mcast_sta.sta_id == IWL_INVALID_STA))
			return 0;

		/* IGTK/BIGTK to bcast STA */
		if (key->keyidx >= 4)
			return BIT(link->bcast_sta.sta_id);

		/* GTK for data to mcast STA */
		return BIT(link->mcast_sta.sta_id);
	}

	/* for client mode use the AP STA also for group keys */
	if (!sta && vif->type == NL80211_IFTYPE_STATION)
		sta = mld_vif->ap_sta;

	/* STA should be non-NULL now */
	if (WARN_ON(!sta))
		return 0;

	/* Key is not per-link, get the full sta mask */
	if (key->link_id < 0)
		return iwl_mld_fw_sta_id_mask(mld, sta);

	/* The link_sta shouldn't be NULL now, but this is checked in
	 * iwl_mld_fw_sta_id_mask
	 */
	link_sta = link_sta_dereference_check(sta, key->link_id);

	sta_id = iwl_mld_fw_sta_id_from_link_sta(mld, link_sta);
	if (sta_id < 0)
		return 0;

	return BIT(sta_id);
}

static int iwl_mld_add_key_to_fw(struct iwl_mld *mld, u32 sta_mask,
				 u32 key_flags, struct ieee80211_key_conf *key)
{
	struct iwl_sec_key_cmd cmd = {
		.action = cpu_to_le32(FW_CTXT_ACTION_ADD),
		.u.add.sta_mask = cpu_to_le32(sta_mask),
		.u.add.key_id = cpu_to_le32(key->keyidx),
		.u.add.key_flags = cpu_to_le32(key_flags),
		.u.add.tx_seq = cpu_to_le64(atomic64_read(&key->tx_pn)),
	};
	bool tkip = key->cipher == WLAN_CIPHER_SUITE_TKIP;
	int max_key_len = sizeof(cmd.u.add.key);

#ifdef CONFIG_PM_SLEEP
	/* If there was a rekey in wowlan, FW already has the key */
	if (mld->fw_status.resuming)
		return 0;
#endif

	if (WARN_ON(!sta_mask))
		return -EINVAL;

	if (WARN_ON(key->keylen > max_key_len))
		return -EINVAL;

	memcpy(cmd.u.add.key, key->key, key->keylen);

	if (tkip) {
		memcpy(cmd.u.add.tkip_mic_rx_key,
		       key->key + NL80211_TKIP_DATA_OFFSET_RX_MIC_KEY,
		       8);
		memcpy(cmd.u.add.tkip_mic_tx_key,
		       key->key + NL80211_TKIP_DATA_OFFSET_TX_MIC_KEY,
		       8);
	}

	return iwl_mld_send_cmd_pdu(mld, WIDE_ID(DATA_PATH_GROUP, SEC_KEY_CMD),
				    &cmd);
}

static void iwl_mld_remove_key_from_fw(struct iwl_mld *mld, u32 sta_mask,
				       u32 key_flags, u32 keyidx)
{
	struct iwl_sec_key_cmd cmd = {
		.action = cpu_to_le32(FW_CTXT_ACTION_REMOVE),
		.u.remove.sta_mask = cpu_to_le32(sta_mask),
		.u.remove.key_id = cpu_to_le32(keyidx),
		.u.remove.key_flags = cpu_to_le32(key_flags),
	};

#ifdef CONFIG_PM_SLEEP
	/* If there was a rekey in wowlan, FW already removed the key */
	if (mld->fw_status.resuming)
		return;
#endif

	if (WARN_ON(!sta_mask))
		return;

	iwl_mld_send_cmd_pdu(mld, WIDE_ID(DATA_PATH_GROUP, SEC_KEY_CMD), &cmd);
}

void iwl_mld_remove_key(struct iwl_mld *mld, struct ieee80211_vif *vif,
			struct ieee80211_sta *sta,
			struct ieee80211_key_conf *key)
{
	u32 sta_mask = iwl_mld_get_key_sta_mask(mld, vif, sta, key);
	u32 key_flags = iwl_mld_get_key_flags(mld, vif, sta, key);
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);

	lockdep_assert_wiphy(mld->wiphy);

	if (!sta_mask)
		return;

	if (key->keyidx == 4 || key->keyidx == 5) {
		struct iwl_mld_link *mld_link;
		unsigned int link_id = 0;

		/* set to -1 for non-MLO right now */
		if (key->link_id >= 0)
			link_id = key->link_id;

		mld_link = iwl_mld_link_dereference_check(mld_vif, link_id);
		if (WARN_ON(!mld_link))
			return;

		if (mld_link->igtk == key)
			mld_link->igtk = NULL;

		mld->num_igtks--;
	}

	iwl_mld_remove_key_from_fw(mld, sta_mask, key_flags, key->keyidx);

	/* no longer in HW */
	key->hw_key_idx = STA_KEY_IDX_INVALID;
}

int iwl_mld_add_key(struct iwl_mld *mld,
		    struct ieee80211_vif *vif,
		    struct ieee80211_sta *sta,
		    struct ieee80211_key_conf *key)
{
	u32 sta_mask = iwl_mld_get_key_sta_mask(mld, vif, sta, key);
	u32 key_flags = iwl_mld_get_key_flags(mld, vif, sta, key);
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);
	struct iwl_mld_link *mld_link = NULL;
	bool igtk = key->keyidx == 4 || key->keyidx == 5;
	int ret;

	lockdep_assert_wiphy(mld->wiphy);

	if (!sta_mask)
		return -EINVAL;

	if (igtk) {
		if (mld->num_igtks == IWL_MAX_NUM_IGTKS)
			return -EOPNOTSUPP;

		u8 link_id = 0;

		/* set to -1 for non-MLO right now */
		if (key->link_id >= 0)
			link_id = key->link_id;

		mld_link = iwl_mld_link_dereference_check(mld_vif, link_id);

		if (WARN_ON(!mld_link))
			return -EINVAL;

		if (mld_link->igtk) {
			IWL_DEBUG_MAC80211(mld, "remove old IGTK %d\n",
					   mld_link->igtk->keyidx);
			iwl_mld_remove_key(mld, vif, sta, mld_link->igtk);
		}

		WARN_ON(mld_link->igtk);
	}

	ret = iwl_mld_add_key_to_fw(mld, sta_mask, key_flags, key);
	if (ret)
		return ret;

	if (mld_link) {
		mld_link->igtk = key;
		mld->num_igtks++;
	}

	/* We don't really need this, but need it to be not invalid,
	 * so we will know if the key is in fw.
	 */
	key->hw_key_idx = 0;

	return 0;
}

struct remove_ap_keys_iter_data {
	u8 link_id;
	struct ieee80211_sta *sta;
};

static void iwl_mld_remove_ap_keys_iter(struct ieee80211_hw *hw,
					struct ieee80211_vif *vif,
					struct ieee80211_sta *sta,
					struct ieee80211_key_conf *key,
					void *_data)
{
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);
	struct remove_ap_keys_iter_data *data = _data;

	if (key->hw_key_idx == STA_KEY_IDX_INVALID)
		return;

	/* All the pairwise keys should have been removed by now */
	if (WARN_ON(sta))
		return;

	if (key->link_id >= 0 && key->link_id != data->link_id)
		return;

	iwl_mld_remove_key(mld, vif, data->sta, key);
}

void iwl_mld_remove_ap_keys(struct iwl_mld *mld, struct ieee80211_vif *vif,
			    struct ieee80211_sta *sta, unsigned int link_id)
{
	struct remove_ap_keys_iter_data iter_data = {
		.link_id = link_id,
		.sta = sta,
	};

	if (WARN_ON_ONCE(vif->type != NL80211_IFTYPE_STATION))
		return;

	ieee80211_iter_keys(mld->hw, vif,
			    iwl_mld_remove_ap_keys_iter,
			    &iter_data);
}

struct iwl_mvm_sta_key_update_data {
	struct ieee80211_sta *sta;
	u32 old_sta_mask;
	u32 new_sta_mask;
	int err;
};

static void iwl_mld_update_sta_key_iter(struct ieee80211_hw *hw,
					struct ieee80211_vif *vif,
					struct ieee80211_sta *sta,
					struct ieee80211_key_conf *key,
					void *_data)
{
	struct iwl_mvm_sta_key_update_data *data = _data;
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);
	struct iwl_sec_key_cmd cmd = {
		.action = cpu_to_le32(FW_CTXT_ACTION_MODIFY),
		.u.modify.old_sta_mask = cpu_to_le32(data->old_sta_mask),
		.u.modify.new_sta_mask = cpu_to_le32(data->new_sta_mask),
		.u.modify.key_id = cpu_to_le32(key->keyidx),
		.u.modify.key_flags =
			cpu_to_le32(iwl_mld_get_key_flags(mld, vif, sta, key)),
	};
	int err;

	/* only need to do this for pairwise keys (link_id == -1) */
	if (sta != data->sta || key->link_id >= 0)
		return;

	err = iwl_mld_send_cmd_pdu(mld, WIDE_ID(DATA_PATH_GROUP, SEC_KEY_CMD),
				   &cmd);

	if (err)
		data->err = err;
}

int iwl_mld_update_sta_keys(struct iwl_mld *mld,
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

	ieee80211_iter_keys(mld->hw, vif, iwl_mld_update_sta_key_iter,
			    &data);
	return data.err;
}

void iwl_mld_track_bigtk(struct iwl_mld *mld,
			 struct ieee80211_vif *vif,
			 struct ieee80211_key_conf *key, bool add)
{
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);
	struct iwl_mld_link *link;

	if (vif->type != NL80211_IFTYPE_STATION)
		return;

	if (WARN_ON(key->keyidx < 6 || key->keyidx > 7))
		return;

	if (WARN_ON(key->link_id < 0))
		return;

	link = iwl_mld_link_dereference_check(mld_vif, key->link_id);
	if (WARN_ON(!link))
		return;

	if (add)
		rcu_assign_pointer(link->bigtks[key->keyidx - 6], key);
	else
		RCU_INIT_POINTER(link->bigtks[key->keyidx - 6], NULL);
}

bool iwl_mld_beacon_protection_enabled(struct iwl_mld *mld,
				       struct ieee80211_bss_conf *link)
{
	struct iwl_mld_link *mld_link = iwl_mld_link_from_mac80211(link);

	if (WARN_ON(!mld_link))
		return false;

	return rcu_access_pointer(mld_link->bigtks[0]) ||
		rcu_access_pointer(mld_link->bigtks[1]);
}
