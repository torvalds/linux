// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2024, 2026 Intel Corporation
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

	/* When a GTK is configured for a station, it can only be
	 * used for Rx and never for Tx. Thus, set the NO TX flag.
	 */
	if (!pairwise && sta)
		flags |= IWL_SEC_KEY_FLAG_NO_TX;

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

	if (vif->type == NL80211_IFTYPE_NAN_DATA && !sta) {
		/* Older firmware versions do not support transmission of
		 * multicast data frames.
		 */
		if (!iwl_mld_nan_use_nan_stations(mld))
			return 0;

		if (WARN_ON(mld_vif->nan.mcast_data_sta.sta_id ==
			    IWL_INVALID_STA))
			return 0;

		return BIT(mld_vif->nan.mcast_data_sta.sta_id);
	}

	if (vif->type == NL80211_IFTYPE_NAN && !sta) {
		/* Older firmware versions do not support installation of
		 * IGTK/BIGTK keys.
		 */
		if (!iwl_mld_nan_use_nan_stations(mld))
			return 0;

		if (WARN_ON(mld_vif->nan.bcast_sta.sta_id == IWL_INVALID_STA ||
			    mld_vif->nan.mgmt_sta.sta_id == IWL_INVALID_STA))
			return 0;

		if (key->keyidx >= 4 && key->keyidx <= 5)
			return BIT(mld_vif->nan.mgmt_sta.sta_id);

		if (key->keyidx >= 6 && key->keyidx <= 7)
			return BIT(mld_vif->nan.bcast_sta.sta_id);

		return 0;
	}

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

	/* STA should be non-NULL */
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

static struct ieee80211_key_conf **
iwl_mld_get_igtk_ptr(struct ieee80211_vif *vif,
		     struct ieee80211_sta *sta,
		     struct ieee80211_key_conf *key)
{
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);
	/* key's link ID is set to -1 for non-MLO */
	int link_id = key->link_id < 0 ? 0 : key->link_id;
	struct iwl_mld_link_sta *mld_ap_link_sta;
	struct iwl_mld_link *mld_link;
	struct iwl_mld_sta *mld_sta;

	if (key->keyidx != 4 && key->keyidx != 5)
		return NULL;

	switch (vif->type) {
	case NL80211_IFTYPE_STATION:
		if (WARN_ON(!sta))
			return NULL;

		mld_sta = iwl_mld_sta_from_mac80211(sta);
		mld_ap_link_sta = iwl_mld_link_sta_dereference_check(mld_sta,
								     link_id);
		if (WARN_ON(!mld_ap_link_sta))
			return NULL;

		return &mld_ap_link_sta->rx_igtk;
	case NL80211_IFTYPE_NAN:
		if (sta) {
			mld_sta = iwl_mld_sta_from_mac80211(sta);

			return &mld_sta->deflink.rx_igtk;
		}

		return &mld_vif->nan.tx_igtk;
	case NL80211_IFTYPE_AP:
		mld_link = iwl_mld_link_dereference_check(mld_vif, link_id);
		if (WARN_ON(!mld_link))
			return NULL;

		return &mld_link->tx_igtk;
	default:
		WARN_ONCE(1, "invalid iftype %d for IGTK\n", vif->type);
		return NULL;
	}
}

void iwl_mld_remove_key(struct iwl_mld *mld, struct ieee80211_vif *vif,
			struct ieee80211_sta *sta,
			struct ieee80211_key_conf *key)
{
	u32 sta_mask = iwl_mld_get_key_sta_mask(mld, vif, sta, key);
	u32 key_flags = iwl_mld_get_key_flags(mld, vif, sta, key);
	struct ieee80211_key_conf **igtk_ptr;

	lockdep_assert_wiphy(mld->wiphy);

	if (!sta_mask)
		return;

	igtk_ptr = iwl_mld_get_igtk_ptr(vif, sta, key);
	if (igtk_ptr && *igtk_ptr == key) {
		*igtk_ptr = NULL;
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
	struct ieee80211_key_conf **igtk_ptr;
	int ret;

	lockdep_assert_wiphy(mld->wiphy);

	if (!sta_mask) {
		/* for NAN (GTK) indicate SW-only, it's not used at all */
		if (vif->type == NL80211_IFTYPE_NAN_DATA && !sta &&
		    !iwl_mld_nan_use_nan_stations(mld))
			return 1;

		/* otherwise that's not valid */
		IWL_WARN(mld, "empty STA mask for key %d\n", key->keyidx);
		return -EINVAL;
	}

	igtk_ptr = iwl_mld_get_igtk_ptr(vif, sta, key);
	if (igtk_ptr) {
		if (mld->num_igtks == mld->fw->ucode_capa.num_mcast_key_entries)
			return -EOPNOTSUPP;

		if (*igtk_ptr) {
			IWL_DEBUG_MAC80211(mld, "remove old IGTK %d\n",
					   (*igtk_ptr)->keyidx);
			iwl_mld_remove_key(mld, vif, sta, *igtk_ptr);
		}
	}

	ret = iwl_mld_add_key_to_fw(mld, sta_mask, key_flags, key);
	if (ret) {
		IWL_WARN(mld, "failed to add key to FW (%d)\n", ret);
		return ret;
	}

	if (igtk_ptr) {
		WARN_ON(*igtk_ptr);
		*igtk_ptr = key;
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
