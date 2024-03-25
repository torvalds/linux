// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2022-2023 Intel Corporation
 */
#include "mvm.h"
#include "time-sync.h"
#include "sta.h"

u32 iwl_mvm_sta_fw_id_mask(struct iwl_mvm *mvm, struct ieee80211_sta *sta,
			   int filter_link_id)
{
	struct ieee80211_link_sta *link_sta;
	struct iwl_mvm_sta *mvmsta;
	struct ieee80211_vif *vif;
	unsigned int link_id;
	u32 result = 0;

	if (!sta)
		return 0;

	mvmsta = iwl_mvm_sta_from_mac80211(sta);
	vif = mvmsta->vif;

	/* it's easy when the STA is not an MLD */
	if (!sta->valid_links)
		return BIT(mvmsta->deflink.sta_id);

	/* but if it is an MLD, get the mask of all the FW STAs it has ... */
	for_each_sta_active_link(vif, sta, link_sta, link_id) {
		struct iwl_mvm_link_sta *mvm_link_sta;

		/* unless we have a specific link in mind */
		if (filter_link_id >= 0 && link_id != filter_link_id)
			continue;

		mvm_link_sta =
			rcu_dereference_check(mvmsta->link[link_id],
					      lockdep_is_held(&mvm->mutex));
		if (!mvm_link_sta)
			continue;

		result |= BIT(mvm_link_sta->sta_id);
	}

	return result;
}

static int iwl_mvm_mld_send_sta_cmd(struct iwl_mvm *mvm,
				    struct iwl_mvm_sta_cfg_cmd *cmd)
{
	int ret = iwl_mvm_send_cmd_pdu(mvm,
				       WIDE_ID(MAC_CONF_GROUP, STA_CONFIG_CMD),
				       0, sizeof(*cmd), cmd);
	if (ret)
		IWL_ERR(mvm, "STA_CONFIG_CMD send failed, ret=0x%x\n", ret);
	return ret;
}

/*
 * Add an internal station to the FW table
 */
static int iwl_mvm_mld_add_int_sta_to_fw(struct iwl_mvm *mvm,
					 struct iwl_mvm_int_sta *sta,
					 const u8 *addr, int link_id)
{
	struct iwl_mvm_sta_cfg_cmd cmd;

	lockdep_assert_held(&mvm->mutex);

	memset(&cmd, 0, sizeof(cmd));
	cmd.sta_id = cpu_to_le32((u8)sta->sta_id);

	cmd.link_id = cpu_to_le32(link_id);

	cmd.station_type = cpu_to_le32(sta->type);

	if (fw_has_capa(&mvm->fw->ucode_capa,
			IWL_UCODE_TLV_CAPA_STA_EXP_MFP_SUPPORT) &&
	    sta->type == STATION_TYPE_BCAST_MGMT)
		cmd.mfp = cpu_to_le32(1);

	if (addr) {
		memcpy(cmd.peer_mld_address, addr, ETH_ALEN);
		memcpy(cmd.peer_link_address, addr, ETH_ALEN);
	}

	return iwl_mvm_mld_send_sta_cmd(mvm, &cmd);
}

/*
 * Remove a station from the FW table. Before sending the command to remove
 * the station validate that the station is indeed known to the driver (sanity
 * only).
 */
static int iwl_mvm_mld_rm_sta_from_fw(struct iwl_mvm *mvm, u32 sta_id)
{
	struct iwl_mvm_remove_sta_cmd rm_sta_cmd = {
		.sta_id = cpu_to_le32(sta_id),
	};
	int ret;

	/* Note: internal stations are marked as error values */
	if (!rcu_access_pointer(mvm->fw_id_to_mac_id[sta_id])) {
		IWL_ERR(mvm, "Invalid station id %d\n", sta_id);
		return -EINVAL;
	}

	ret = iwl_mvm_send_cmd_pdu(mvm, WIDE_ID(MAC_CONF_GROUP, STA_REMOVE_CMD),
				   0, sizeof(rm_sta_cmd), &rm_sta_cmd);
	if (ret) {
		IWL_ERR(mvm, "Failed to remove station. Id=%d\n", sta_id);
		return ret;
	}

	return 0;
}

static int iwl_mvm_add_aux_sta_to_fw(struct iwl_mvm *mvm,
				     struct iwl_mvm_int_sta *sta,
				     u32 lmac_id)
{
	int ret;

	struct iwl_mvm_aux_sta_cmd cmd = {
		.sta_id = cpu_to_le32(sta->sta_id),
		.lmac_id = cpu_to_le32(lmac_id),
	};

	ret = iwl_mvm_send_cmd_pdu(mvm, WIDE_ID(MAC_CONF_GROUP, AUX_STA_CMD),
				   0, sizeof(cmd), &cmd);
	if (ret)
		IWL_ERR(mvm, "Failed to send AUX_STA_CMD\n");
	return ret;
}

/*
 * Adds an internal sta to the FW table with its queues
 */
int iwl_mvm_mld_add_int_sta_with_queue(struct iwl_mvm *mvm,
				       struct iwl_mvm_int_sta *sta,
				       const u8 *addr, int link_id,
				       u16 *queue, u8 tid,
				       unsigned int *_wdg_timeout)
{
	int ret, txq;
	unsigned int wdg_timeout = _wdg_timeout ? *_wdg_timeout :
		mvm->trans->trans_cfg->base_params->wd_timeout;

	if (WARN_ON_ONCE(sta->sta_id == IWL_MVM_INVALID_STA))
		return -ENOSPC;

	if (sta->type == STATION_TYPE_AUX)
		ret = iwl_mvm_add_aux_sta_to_fw(mvm, sta, link_id);
	else
		ret = iwl_mvm_mld_add_int_sta_to_fw(mvm, sta, addr, link_id);
	if (ret)
		return ret;

	/*
	 * For 22000 firmware and on we cannot add queue to a station unknown
	 * to firmware so enable queue here - after the station was added
	 */
	txq = iwl_mvm_tvqm_enable_txq(mvm, NULL, sta->sta_id, tid,
				      wdg_timeout);
	if (txq < 0) {
		iwl_mvm_mld_rm_sta_from_fw(mvm, sta->sta_id);
		return txq;
	}
	*queue = txq;

	return 0;
}

/*
 * Adds a new int sta: allocate it in the driver, add it to the FW table,
 * and add its queues.
 */
static int iwl_mvm_mld_add_int_sta(struct iwl_mvm *mvm,
				   struct iwl_mvm_int_sta *int_sta, u16 *queue,
				   enum nl80211_iftype iftype,
				   enum iwl_fw_sta_type sta_type,
				   int link_id, const u8 *addr, u8 tid,
				   unsigned int *wdg_timeout)
{
	int ret;

	lockdep_assert_held(&mvm->mutex);

	/* qmask argument is not used in the new tx api, send a don't care */
	ret = iwl_mvm_allocate_int_sta(mvm, int_sta, 0, iftype,
				       sta_type);
	if (ret)
		return ret;

	ret = iwl_mvm_mld_add_int_sta_with_queue(mvm, int_sta, addr, link_id,
						 queue, tid, wdg_timeout);
	if (ret) {
		iwl_mvm_dealloc_int_sta(mvm, int_sta);
		return ret;
	}

	return 0;
}

/* Allocate a new station entry for the broadcast station to the given vif,
 * and send it to the FW.
 * Note that each P2P mac should have its own broadcast station.
 */
int iwl_mvm_mld_add_bcast_sta(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			      struct ieee80211_bss_conf *link_conf)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm_vif_link_info *mvm_link =
		mvmvif->link[link_conf->link_id];
	struct iwl_mvm_int_sta *bsta = &mvm_link->bcast_sta;
	static const u8 _baddr[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
	const u8 *baddr = _baddr;
	unsigned int wdg_timeout =
		iwl_mvm_get_wd_timeout(mvm, vif, false, false);
	u16 *queue;

	lockdep_assert_held(&mvm->mutex);

	if (vif->type == NL80211_IFTYPE_ADHOC)
		baddr = link_conf->bssid;

	if (vif->type == NL80211_IFTYPE_AP ||
	    vif->type == NL80211_IFTYPE_ADHOC) {
		queue = &mvm_link->mgmt_queue;
	} else if (vif->type == NL80211_IFTYPE_P2P_DEVICE) {
		queue = &mvm->p2p_dev_queue;
	} else {
		WARN(1, "Missing required TXQ for adding bcast STA\n");
		return -EINVAL;
	}

	return iwl_mvm_mld_add_int_sta(mvm, bsta, queue,
				       ieee80211_vif_type_p2p(vif),
				       STATION_TYPE_BCAST_MGMT,
				       mvm_link->fw_link_id, baddr,
				       IWL_MAX_TID_COUNT, &wdg_timeout);
}

/* Allocate a new station entry for the broadcast station to the given vif,
 * and send it to the FW.
 * Note that each AP/GO mac should have its own multicast station.
 */
int iwl_mvm_mld_add_mcast_sta(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			      struct ieee80211_bss_conf *link_conf)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm_vif_link_info *mvm_link =
		mvmvif->link[link_conf->link_id];
	struct iwl_mvm_int_sta *msta = &mvm_link->mcast_sta;
	static const u8 _maddr[] = {0x03, 0x00, 0x00, 0x00, 0x00, 0x00};
	const u8 *maddr = _maddr;
	unsigned int timeout = iwl_mvm_get_wd_timeout(mvm, vif, false, false);

	lockdep_assert_held(&mvm->mutex);

	if (WARN_ON(vif->type != NL80211_IFTYPE_AP &&
		    vif->type != NL80211_IFTYPE_ADHOC))
		return -EOPNOTSUPP;

	/* In IBSS, ieee80211_check_queues() sets the cab_queue to be
	 * invalid, so make sure we use the queue we want.
	 * Note that this is done here as we want to avoid making DQA
	 * changes in mac80211 layer.
	 */
	if (vif->type == NL80211_IFTYPE_ADHOC)
		mvm_link->cab_queue = IWL_MVM_DQA_GCAST_QUEUE;

	return iwl_mvm_mld_add_int_sta(mvm, msta, &mvm_link->cab_queue,
				       vif->type, STATION_TYPE_MCAST,
				       mvm_link->fw_link_id, maddr, 0,
				       &timeout);
}

/* Allocate a new station entry for the sniffer station to the given vif,
 * and send it to the FW.
 */
int iwl_mvm_mld_add_snif_sta(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			     struct ieee80211_bss_conf *link_conf)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm_vif_link_info *mvm_link =
		mvmvif->link[link_conf->link_id];

	lockdep_assert_held(&mvm->mutex);

	return iwl_mvm_mld_add_int_sta(mvm, &mvm->snif_sta, &mvm->snif_queue,
				       vif->type, STATION_TYPE_BCAST_MGMT,
				       mvm_link->fw_link_id, NULL,
				       IWL_MAX_TID_COUNT, NULL);
}

int iwl_mvm_mld_add_aux_sta(struct iwl_mvm *mvm, u32 lmac_id)
{
	lockdep_assert_held(&mvm->mutex);

	/* In CDB NICs we need to specify which lmac to use for aux activity;
	 * use the link_id argument place to send lmac_id to the function.
	 */
	return iwl_mvm_mld_add_int_sta(mvm, &mvm->aux_sta, &mvm->aux_queue,
				       NL80211_IFTYPE_UNSPECIFIED,
				       STATION_TYPE_AUX, lmac_id, NULL,
				       IWL_MAX_TID_COUNT, NULL);
}

static int iwl_mvm_mld_disable_txq(struct iwl_mvm *mvm, u32 sta_mask,
				   u16 *queueptr, u8 tid)
{
	int queue = *queueptr;
	int ret = 0;

	if (tid == IWL_MAX_TID_COUNT)
		tid = IWL_MGMT_TID;

	if (mvm->sta_remove_requires_queue_remove) {
		u32 cmd_id = WIDE_ID(DATA_PATH_GROUP,
				     SCD_QUEUE_CONFIG_CMD);
		struct iwl_scd_queue_cfg_cmd remove_cmd = {
			.operation = cpu_to_le32(IWL_SCD_QUEUE_REMOVE),
			.u.remove.tid = cpu_to_le32(tid),
			.u.remove.sta_mask = cpu_to_le32(sta_mask),
		};

		ret = iwl_mvm_send_cmd_pdu(mvm, cmd_id, 0,
					   sizeof(remove_cmd),
					   &remove_cmd);
	}

	iwl_trans_txq_free(mvm->trans, queue);
	*queueptr = IWL_MVM_INVALID_QUEUE;

	return ret;
}

/* Removes a sta from the FW table, disable its queues, and dealloc it
 */
static int iwl_mvm_mld_rm_int_sta(struct iwl_mvm *mvm,
				  struct iwl_mvm_int_sta *int_sta,
				  bool flush, u8 tid, u16 *queuptr)
{
	int ret;

	lockdep_assert_held(&mvm->mutex);

	if (WARN_ON_ONCE(int_sta->sta_id == IWL_MVM_INVALID_STA))
		return -EINVAL;

	if (flush)
		iwl_mvm_flush_sta(mvm, int_sta->sta_id, int_sta->tfd_queue_msk);

	iwl_mvm_mld_disable_txq(mvm, BIT(int_sta->sta_id), queuptr, tid);

	ret = iwl_mvm_mld_rm_sta_from_fw(mvm, int_sta->sta_id);
	if (ret)
		IWL_WARN(mvm, "Failed sending remove station\n");

	iwl_mvm_dealloc_int_sta(mvm, int_sta);

	return ret;
}

int iwl_mvm_mld_rm_bcast_sta(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			     struct ieee80211_bss_conf *link_conf)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm_vif_link_info *link = mvmvif->link[link_conf->link_id];
	u16 *queueptr;

	lockdep_assert_held(&mvm->mutex);

	if (WARN_ON(!link))
		return -EIO;

	switch (vif->type) {
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_ADHOC:
		queueptr = &link->mgmt_queue;
		break;
	case NL80211_IFTYPE_P2P_DEVICE:
		queueptr = &mvm->p2p_dev_queue;
		break;
	default:
		WARN(1, "Can't free bcast queue on vif type %d\n",
		     vif->type);
		return -EINVAL;
	}

	return iwl_mvm_mld_rm_int_sta(mvm, &link->bcast_sta,
				      true, IWL_MAX_TID_COUNT, queueptr);
}

/* Send the FW a request to remove the station from it's internal data
 * structures, and in addition remove it from the local data structure.
 */
int iwl_mvm_mld_rm_mcast_sta(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			     struct ieee80211_bss_conf *link_conf)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm_vif_link_info *link = mvmvif->link[link_conf->link_id];

	lockdep_assert_held(&mvm->mutex);

	if (WARN_ON(!link))
		return -EIO;

	return iwl_mvm_mld_rm_int_sta(mvm, &link->mcast_sta, true, 0,
				      &link->cab_queue);
}

int iwl_mvm_mld_rm_snif_sta(struct iwl_mvm *mvm, struct ieee80211_vif *vif)
{
	lockdep_assert_held(&mvm->mutex);

	return iwl_mvm_mld_rm_int_sta(mvm, &mvm->snif_sta, false,
				      IWL_MAX_TID_COUNT, &mvm->snif_queue);
}

int iwl_mvm_mld_rm_aux_sta(struct iwl_mvm *mvm)
{
	lockdep_assert_held(&mvm->mutex);

	return iwl_mvm_mld_rm_int_sta(mvm, &mvm->aux_sta, false,
				      IWL_MAX_TID_COUNT, &mvm->aux_queue);
}

/* send a cfg sta command to add/update a sta in firmware */
static int iwl_mvm_mld_cfg_sta(struct iwl_mvm *mvm, struct ieee80211_sta *sta,
			       struct ieee80211_vif *vif,
			       struct ieee80211_link_sta *link_sta,
			       struct ieee80211_bss_conf *link_conf,
			       struct iwl_mvm_link_sta *mvm_link_sta)
{
	struct iwl_mvm_sta *mvm_sta = iwl_mvm_sta_from_mac80211(sta);
	struct iwl_mvm_vif *mvm_vif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm_vif_link_info *link_info =
					mvm_vif->link[link_conf->link_id];
	struct iwl_mvm_sta_cfg_cmd cmd = {
		.sta_id = cpu_to_le32(mvm_link_sta->sta_id),
		.station_type = cpu_to_le32(mvm_sta->sta_type),
	};
	u32 agg_size = 0, mpdu_dens = 0;

	/* when adding sta, link should exist in FW */
	if (WARN_ON(link_info->fw_link_id == IWL_MVM_FW_LINK_ID_INVALID))
		return -EINVAL;

	cmd.link_id = cpu_to_le32(link_info->fw_link_id);

	memcpy(&cmd.peer_mld_address, sta->addr, ETH_ALEN);
	memcpy(&cmd.peer_link_address, link_sta->addr, ETH_ALEN);

	if (mvm_sta->sta_state >= IEEE80211_STA_ASSOC)
		cmd.assoc_id = cpu_to_le32(sta->aid);

	if (fw_has_capa(&mvm->fw->ucode_capa,
			IWL_UCODE_TLV_CAPA_STA_EXP_MFP_SUPPORT) &&
	    (sta->mfp || mvm_sta->sta_state < IEEE80211_STA_AUTHORIZED))
		cmd.mfp = cpu_to_le32(1);

	switch (link_sta->rx_nss) {
	case 1:
		cmd.mimo = cpu_to_le32(0);
		break;
	case 2 ... 8:
		cmd.mimo = cpu_to_le32(1);
		break;
	}

	switch (sta->deflink.smps_mode) {
	case IEEE80211_SMPS_AUTOMATIC:
	case IEEE80211_SMPS_NUM_MODES:
		WARN_ON(1);
		break;
	case IEEE80211_SMPS_STATIC:
		/* override NSS */
		cmd.mimo = cpu_to_le32(0);
		break;
	case IEEE80211_SMPS_DYNAMIC:
		cmd.mimo_protection = cpu_to_le32(1);
		break;
	case IEEE80211_SMPS_OFF:
		/* nothing */
		break;
	}

	mpdu_dens = iwl_mvm_get_sta_ampdu_dens(link_sta, link_conf, &agg_size);
	cmd.tx_ampdu_spacing = cpu_to_le32(mpdu_dens);
	cmd.tx_ampdu_max_size = cpu_to_le32(agg_size);

	if (sta->wme) {
		cmd.sp_length =
			cpu_to_le32(sta->max_sp ? sta->max_sp * 2 : 128);
		cmd.uapsd_acs = cpu_to_le32(iwl_mvm_get_sta_uapsd_acs(sta));
	}

	if (link_sta->he_cap.has_he) {
		cmd.trig_rnd_alloc =
			cpu_to_le32(link_conf->uora_exists ? 1 : 0);

		/* PPE Thresholds */
		iwl_mvm_set_sta_pkt_ext(mvm, link_sta, &cmd.pkt_ext);

		/* HTC flags */
		cmd.htc_flags = iwl_mvm_get_sta_htc_flags(sta, link_sta);

		if (link_sta->he_cap.he_cap_elem.mac_cap_info[2] &
		    IEEE80211_HE_MAC_CAP2_ACK_EN)
			cmd.ack_enabled = cpu_to_le32(1);
	}

	return iwl_mvm_mld_send_sta_cmd(mvm, &cmd);
}

static void iwl_mvm_mld_free_sta_link(struct iwl_mvm *mvm,
				      struct iwl_mvm_sta *mvm_sta,
				      struct iwl_mvm_link_sta *mvm_sta_link,
				      unsigned int link_id,
				      bool is_in_fw)
{
	RCU_INIT_POINTER(mvm->fw_id_to_mac_id[mvm_sta_link->sta_id],
			 is_in_fw ? ERR_PTR(-EINVAL) : NULL);
	RCU_INIT_POINTER(mvm->fw_id_to_link_sta[mvm_sta_link->sta_id], NULL);
	RCU_INIT_POINTER(mvm_sta->link[link_id], NULL);

	if (mvm_sta_link != &mvm_sta->deflink)
		kfree_rcu(mvm_sta_link, rcu_head);
}

static void iwl_mvm_mld_sta_rm_all_sta_links(struct iwl_mvm *mvm,
					     struct iwl_mvm_sta *mvm_sta)
{
	unsigned int link_id;

	for (link_id = 0; link_id < ARRAY_SIZE(mvm_sta->link); link_id++) {
		struct iwl_mvm_link_sta *link =
			rcu_dereference_protected(mvm_sta->link[link_id],
						  lockdep_is_held(&mvm->mutex));

		if (!link)
			continue;

		iwl_mvm_mld_free_sta_link(mvm, mvm_sta, link, link_id, false);
	}
}

static int iwl_mvm_mld_alloc_sta_link(struct iwl_mvm *mvm,
				      struct ieee80211_vif *vif,
				      struct ieee80211_sta *sta,
				      unsigned int link_id)
{
	struct ieee80211_link_sta *link_sta =
		link_sta_dereference_protected(sta, link_id);
	struct iwl_mvm_sta *mvm_sta = iwl_mvm_sta_from_mac80211(sta);
	struct iwl_mvm_link_sta *link;
	u32 sta_id = iwl_mvm_find_free_sta_id(mvm,
					  ieee80211_vif_type_p2p(vif));

	if (sta_id == IWL_MVM_INVALID_STA)
		return -ENOSPC;

	if (rcu_access_pointer(sta->link[link_id]) == &sta->deflink) {
		link = &mvm_sta->deflink;
	} else {
		link = kzalloc(sizeof(*link), GFP_KERNEL);
		if (!link)
			return -ENOMEM;
	}

	link->sta_id = sta_id;
	rcu_assign_pointer(mvm_sta->link[link_id], link);
	rcu_assign_pointer(mvm->fw_id_to_mac_id[link->sta_id], sta);
	rcu_assign_pointer(mvm->fw_id_to_link_sta[link->sta_id],
			   link_sta);

	return 0;
}

/* allocate all the links of a sta, called when the station is first added */
static int iwl_mvm_mld_alloc_sta_links(struct iwl_mvm *mvm,
				       struct ieee80211_vif *vif,
				       struct ieee80211_sta *sta)
{
	struct iwl_mvm_sta *mvm_sta = iwl_mvm_sta_from_mac80211(sta);
	struct ieee80211_link_sta *link_sta;
	unsigned int link_id;
	int ret;

	lockdep_assert_held(&mvm->mutex);

	for_each_sta_active_link(vif, sta, link_sta, link_id) {
		if (WARN_ON(mvm_sta->link[link_id]))
			continue;

		ret = iwl_mvm_mld_alloc_sta_link(mvm, vif, sta, link_id);
		if (ret)
			goto err;
	}

	return 0;

err:
	iwl_mvm_mld_sta_rm_all_sta_links(mvm, mvm_sta);
	return ret;
}

static void iwl_mvm_mld_set_ap_sta_id(struct ieee80211_sta *sta,
				      struct iwl_mvm_vif_link_info *vif_link,
				      struct iwl_mvm_link_sta *sta_link)
{
	if (!sta->tdls) {
		WARN_ON(vif_link->ap_sta_id != IWL_MVM_INVALID_STA);
		vif_link->ap_sta_id = sta_link->sta_id;
	} else {
		WARN_ON(vif_link->ap_sta_id == IWL_MVM_INVALID_STA);
	}
}

/* FIXME: consider waiting for mac80211 to add the STA instead of allocating
 * queues here
 */
static int iwl_mvm_alloc_sta_after_restart(struct iwl_mvm *mvm,
					   struct ieee80211_vif *vif,
					   struct ieee80211_sta *sta)
{
	struct iwl_mvm_sta *mvm_sta = iwl_mvm_sta_from_mac80211(sta);
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct ieee80211_link_sta *link_sta;
	unsigned int link_id;
	/* no active link found */
	int ret = -EINVAL;
	int sta_id;

	/* First add an empty station since allocating a queue requires
	 * a valid station. Since we need a link_id to allocate a station,
	 * pick up the first valid one.
	 */
	for_each_sta_active_link(vif, sta, link_sta, link_id) {
		struct iwl_mvm_vif_link_info *mvm_link;
		struct ieee80211_bss_conf *link_conf =
			link_conf_dereference_protected(vif, link_id);
		struct iwl_mvm_link_sta *mvm_link_sta =
			rcu_dereference_protected(mvm_sta->link[link_id],
						  lockdep_is_held(&mvm->mutex));

		if (!link_conf)
			continue;

		mvm_link = mvmvif->link[link_conf->link_id];

		if (!mvm_link || !mvm_link_sta)
			continue;

		sta_id = mvm_link_sta->sta_id;
		ret = iwl_mvm_mld_cfg_sta(mvm, sta, vif, link_sta,
					  link_conf, mvm_link_sta);
		if (ret)
			return ret;

		rcu_assign_pointer(mvm->fw_id_to_mac_id[sta_id], sta);
		rcu_assign_pointer(mvm->fw_id_to_link_sta[sta_id], link_sta);
		ret = 0;
	}

	iwl_mvm_realloc_queues_after_restart(mvm, sta);

	return ret;
}

int iwl_mvm_mld_add_sta(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			struct ieee80211_sta *sta)
{
	struct iwl_mvm_vif *mvm_vif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm_sta *mvm_sta = iwl_mvm_sta_from_mac80211(sta);
	unsigned long link_sta_added_to_fw = 0;
	struct ieee80211_link_sta *link_sta;
	int ret = 0;
	unsigned int link_id;

	lockdep_assert_held(&mvm->mutex);

	if (!test_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status)) {
		ret = iwl_mvm_mld_alloc_sta_links(mvm, vif, sta);
		if (ret)
			return ret;

		spin_lock_init(&mvm_sta->lock);

		ret = iwl_mvm_sta_init(mvm, vif, sta, IWL_MVM_INVALID_STA,
				       STATION_TYPE_PEER);
	} else {
		ret = iwl_mvm_alloc_sta_after_restart(mvm, vif, sta);
	}

	if (ret)
		goto err;

	/* at this stage sta link pointers are already allocated */
	ret = iwl_mvm_mld_update_sta(mvm, vif, sta);
	if (ret)
		goto err;

	for_each_sta_active_link(vif, sta, link_sta, link_id) {
		struct ieee80211_bss_conf *link_conf =
			link_conf_dereference_protected(vif, link_id);
		struct iwl_mvm_link_sta *mvm_link_sta =
			rcu_dereference_protected(mvm_sta->link[link_id],
						  lockdep_is_held(&mvm->mutex));

		if (WARN_ON(!link_conf || !mvm_link_sta)) {
			ret = -EINVAL;
			goto err;
		}

		ret = iwl_mvm_mld_cfg_sta(mvm, sta, vif, link_sta, link_conf,
					  mvm_link_sta);
		if (ret)
			goto err;

		link_sta_added_to_fw |= BIT(link_id);

		if (vif->type == NL80211_IFTYPE_STATION)
			iwl_mvm_mld_set_ap_sta_id(sta, mvm_vif->link[link_id],
						  mvm_link_sta);
	}

	return 0;

err:
	/* remove all already allocated stations in FW */
	for_each_set_bit(link_id, &link_sta_added_to_fw,
			 IEEE80211_MLD_MAX_NUM_LINKS) {
		struct iwl_mvm_link_sta *mvm_link_sta =
			rcu_dereference_protected(mvm_sta->link[link_id],
						  lockdep_is_held(&mvm->mutex));

		iwl_mvm_mld_rm_sta_from_fw(mvm, mvm_link_sta->sta_id);
	}

	/* free all sta resources in the driver */
	iwl_mvm_mld_sta_rm_all_sta_links(mvm, mvm_sta);
	return ret;
}

int iwl_mvm_mld_update_sta(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			   struct ieee80211_sta *sta)
{
	struct iwl_mvm_sta *mvm_sta = iwl_mvm_sta_from_mac80211(sta);
	struct ieee80211_link_sta *link_sta;
	unsigned int link_id;
	int ret = -EINVAL;

	lockdep_assert_held(&mvm->mutex);

	for_each_sta_active_link(vif, sta, link_sta, link_id) {
		struct ieee80211_bss_conf *link_conf =
			link_conf_dereference_protected(vif, link_id);
		struct iwl_mvm_link_sta *mvm_link_sta =
			rcu_dereference_protected(mvm_sta->link[link_id],
						  lockdep_is_held(&mvm->mutex));

		if (WARN_ON(!link_conf || !mvm_link_sta))
			return -EINVAL;

		ret = iwl_mvm_mld_cfg_sta(mvm, sta, vif, link_sta, link_conf,
					  mvm_link_sta);

		if (ret) {
			IWL_ERR(mvm, "Failed to update sta link %d\n", link_id);
			break;
		}
	}

	return ret;
}

static void iwl_mvm_mld_disable_sta_queues(struct iwl_mvm *mvm,
					   struct ieee80211_vif *vif,
					   struct ieee80211_sta *sta)
{
	struct iwl_mvm_sta *mvm_sta = iwl_mvm_sta_from_mac80211(sta);
	u32 sta_mask = iwl_mvm_sta_fw_id_mask(mvm, sta, -1);
	int i;

	lockdep_assert_held(&mvm->mutex);

	for (i = 0; i < ARRAY_SIZE(mvm_sta->tid_data); i++) {
		if (mvm_sta->tid_data[i].txq_id == IWL_MVM_INVALID_QUEUE)
			continue;

		iwl_mvm_mld_disable_txq(mvm, sta_mask,
					&mvm_sta->tid_data[i].txq_id, i);
		mvm_sta->tid_data[i].txq_id = IWL_MVM_INVALID_QUEUE;
	}

	for (i = 0; i < ARRAY_SIZE(sta->txq); i++) {
		struct iwl_mvm_txq *mvmtxq =
			iwl_mvm_txq_from_mac80211(sta->txq[i]);

		mvmtxq->txq_id = IWL_MVM_INVALID_QUEUE;
	}
}

int iwl_mvm_mld_rm_sta(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
		       struct ieee80211_sta *sta)
{
	struct iwl_mvm_sta *mvm_sta = iwl_mvm_sta_from_mac80211(sta);
	struct ieee80211_link_sta *link_sta;
	unsigned int link_id;
	int ret;

	lockdep_assert_held(&mvm->mutex);

	/* flush its queues here since we are freeing mvm_sta */
	for_each_sta_active_link(vif, sta, link_sta, link_id) {
		struct iwl_mvm_link_sta *mvm_link_sta =
			rcu_dereference_protected(mvm_sta->link[link_id],
						  lockdep_is_held(&mvm->mutex));

		if (WARN_ON(!mvm_link_sta))
			return -EINVAL;

		ret = iwl_mvm_flush_sta_tids(mvm, mvm_link_sta->sta_id,
					     0xffff);
		if (ret)
			return ret;
	}

	ret = iwl_mvm_wait_sta_queues_empty(mvm, mvm_sta);
	if (ret)
		return ret;

	iwl_mvm_mld_disable_sta_queues(mvm, vif, sta);

	for_each_sta_active_link(vif, sta, link_sta, link_id) {
		struct iwl_mvm_link_sta *mvm_link_sta =
			rcu_dereference_protected(mvm_sta->link[link_id],
						  lockdep_is_held(&mvm->mutex));
		bool stay_in_fw;

		stay_in_fw = iwl_mvm_sta_del(mvm, vif, sta, link_sta, &ret);
		if (ret)
			break;

		if (!stay_in_fw)
			ret = iwl_mvm_mld_rm_sta_from_fw(mvm,
							 mvm_link_sta->sta_id);

		iwl_mvm_mld_free_sta_link(mvm, mvm_sta, mvm_link_sta,
					  link_id, stay_in_fw);
	}

	return ret;
}

int iwl_mvm_mld_rm_sta_id(struct iwl_mvm *mvm, u8 sta_id)
{
	int ret;

	lockdep_assert_held(&mvm->mutex);

	if (WARN_ON(sta_id == IWL_MVM_INVALID_STA))
		return 0;

	ret = iwl_mvm_mld_rm_sta_from_fw(mvm, sta_id);

	RCU_INIT_POINTER(mvm->fw_id_to_mac_id[sta_id], NULL);
	RCU_INIT_POINTER(mvm->fw_id_to_link_sta[sta_id], NULL);
	return ret;
}

void iwl_mvm_mld_sta_modify_disable_tx(struct iwl_mvm *mvm,
				       struct iwl_mvm_sta *mvmsta,
				       bool disable)
{
	struct iwl_mvm_sta_disable_tx_cmd cmd;
	int ret;

	cmd.sta_id = cpu_to_le32(mvmsta->deflink.sta_id);
	cmd.disable = cpu_to_le32(disable);

	if (WARN_ON(iwl_mvm_has_no_host_disable_tx(mvm)))
		return;

	ret = iwl_mvm_send_cmd_pdu(mvm,
				   WIDE_ID(MAC_CONF_GROUP, STA_DISABLE_TX_CMD),
				   CMD_ASYNC, sizeof(cmd), &cmd);
	if (ret)
		IWL_ERR(mvm,
			"Failed to send STA_DISABLE_TX_CMD command (%d)\n",
			ret);
}

void iwl_mvm_mld_sta_modify_disable_tx_ap(struct iwl_mvm *mvm,
					  struct ieee80211_sta *sta,
					  bool disable)
{
	struct iwl_mvm_sta *mvm_sta = iwl_mvm_sta_from_mac80211(sta);

	spin_lock_bh(&mvm_sta->lock);

	if (mvm_sta->disable_tx == disable) {
		spin_unlock_bh(&mvm_sta->lock);
		return;
	}

	iwl_mvm_mld_sta_modify_disable_tx(mvm, mvm_sta, disable);

	spin_unlock_bh(&mvm_sta->lock);
}

void iwl_mvm_mld_modify_all_sta_disable_tx(struct iwl_mvm *mvm,
					   struct iwl_mvm_vif *mvmvif,
					   bool disable)
{
	struct ieee80211_sta *sta;
	struct iwl_mvm_sta *mvm_sta;
	int i;

	rcu_read_lock();

	/* Block/unblock all the stations of the given mvmvif */
	for (i = 0; i < mvm->fw->ucode_capa.num_stations; i++) {
		sta = rcu_dereference(mvm->fw_id_to_mac_id[i]);
		if (IS_ERR_OR_NULL(sta))
			continue;

		mvm_sta = iwl_mvm_sta_from_mac80211(sta);
		if (mvm_sta->mac_id_n_color !=
		    FW_CMD_ID_AND_COLOR(mvmvif->id, mvmvif->color))
			continue;

		iwl_mvm_mld_sta_modify_disable_tx(mvm, mvm_sta, disable);
	}

	rcu_read_unlock();
}

static int iwl_mvm_mld_update_sta_queues(struct iwl_mvm *mvm,
					 struct ieee80211_sta *sta,
					 u32 old_sta_mask,
					 u32 new_sta_mask)
{
	struct iwl_mvm_sta *mvm_sta = iwl_mvm_sta_from_mac80211(sta);
	struct iwl_scd_queue_cfg_cmd cmd = {
		.operation = cpu_to_le32(IWL_SCD_QUEUE_MODIFY),
		.u.modify.old_sta_mask = cpu_to_le32(old_sta_mask),
		.u.modify.new_sta_mask = cpu_to_le32(new_sta_mask),
	};
	struct iwl_host_cmd hcmd = {
		.id = WIDE_ID(DATA_PATH_GROUP, SCD_QUEUE_CONFIG_CMD),
		.len[0] = sizeof(cmd),
		.data[0] = &cmd
	};
	int tid;
	int ret;

	lockdep_assert_held(&mvm->mutex);

	for (tid = 0; tid <= IWL_MAX_TID_COUNT; tid++) {
		struct iwl_mvm_tid_data *tid_data = &mvm_sta->tid_data[tid];
		int txq_id = tid_data->txq_id;

		if (txq_id == IWL_MVM_INVALID_QUEUE)
			continue;

		if (tid == IWL_MAX_TID_COUNT)
			cmd.u.modify.tid = cpu_to_le32(IWL_MGMT_TID);
		else
			cmd.u.modify.tid = cpu_to_le32(tid);

		ret = iwl_mvm_send_cmd(mvm, &hcmd);
		if (ret)
			return ret;
	}

	return 0;
}

static int iwl_mvm_mld_update_sta_baids(struct iwl_mvm *mvm,
					u32 old_sta_mask,
					u32 new_sta_mask)
{
	struct iwl_rx_baid_cfg_cmd cmd = {
		.action = cpu_to_le32(IWL_RX_BAID_ACTION_MODIFY),
		.modify.old_sta_id_mask = cpu_to_le32(old_sta_mask),
		.modify.new_sta_id_mask = cpu_to_le32(new_sta_mask),
	};
	u32 cmd_id = WIDE_ID(DATA_PATH_GROUP, RX_BAID_ALLOCATION_CONFIG_CMD);
	int baid;

	BUILD_BUG_ON(sizeof(struct iwl_rx_baid_cfg_resp) != sizeof(baid));

	for (baid = 0; baid < ARRAY_SIZE(mvm->baid_map); baid++) {
		struct iwl_mvm_baid_data *data;
		int ret;

		data = rcu_dereference_protected(mvm->baid_map[baid],
						 lockdep_is_held(&mvm->mutex));
		if (!data)
			continue;

		if (!(data->sta_mask & old_sta_mask))
			continue;

		WARN_ONCE(data->sta_mask != old_sta_mask,
			  "BAID data for %d corrupted - expected 0x%x found 0x%x\n",
			  baid, old_sta_mask, data->sta_mask);

		cmd.modify.tid = cpu_to_le32(data->tid);

		ret = iwl_mvm_send_cmd_pdu(mvm, cmd_id, 0, sizeof(cmd), &cmd);
		data->sta_mask = new_sta_mask;
		if (ret)
			return ret;
	}

	return 0;
}

static int iwl_mvm_mld_update_sta_resources(struct iwl_mvm *mvm,
					    struct ieee80211_vif *vif,
					    struct ieee80211_sta *sta,
					    u32 old_sta_mask,
					    u32 new_sta_mask)
{
	int ret;

	ret = iwl_mvm_mld_update_sta_queues(mvm, sta,
					    old_sta_mask,
					    new_sta_mask);
	if (ret)
		return ret;

	ret = iwl_mvm_mld_update_sta_keys(mvm, vif, sta,
					  old_sta_mask,
					  new_sta_mask);
	if (ret)
		return ret;

	return iwl_mvm_mld_update_sta_baids(mvm, old_sta_mask, new_sta_mask);
}

int iwl_mvm_mld_update_sta_links(struct iwl_mvm *mvm,
				 struct ieee80211_vif *vif,
				 struct ieee80211_sta *sta,
				 u16 old_links, u16 new_links)
{
	struct iwl_mvm_sta *mvm_sta = iwl_mvm_sta_from_mac80211(sta);
	struct iwl_mvm_vif *mvm_vif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm_link_sta *mvm_sta_link;
	struct iwl_mvm_vif_link_info *mvm_vif_link;
	unsigned long links_to_add = ~old_links & new_links;
	unsigned long links_to_rem = old_links & ~new_links;
	unsigned long old_links_long = old_links;
	u32 current_sta_mask = 0, sta_mask_added = 0, sta_mask_to_rem = 0;
	unsigned long link_sta_added_to_fw = 0, link_sta_allocated = 0;
	unsigned int link_id;
	int ret;

	lockdep_assert_held(&mvm->mutex);

	for_each_set_bit(link_id, &old_links_long,
			 IEEE80211_MLD_MAX_NUM_LINKS) {
		mvm_sta_link =
			rcu_dereference_protected(mvm_sta->link[link_id],
						  lockdep_is_held(&mvm->mutex));

		if (WARN_ON(!mvm_sta_link)) {
			ret = -EINVAL;
			goto err;
		}

		current_sta_mask |= BIT(mvm_sta_link->sta_id);
		if (links_to_rem & BIT(link_id))
			sta_mask_to_rem |= BIT(mvm_sta_link->sta_id);
	}

	if (sta_mask_to_rem) {
		ret = iwl_mvm_mld_update_sta_resources(mvm, vif, sta,
						       current_sta_mask,
						       current_sta_mask &
							~sta_mask_to_rem);
		if (WARN_ON(ret))
			goto err;

		current_sta_mask &= ~sta_mask_to_rem;
	}

	for_each_set_bit(link_id, &links_to_rem, IEEE80211_MLD_MAX_NUM_LINKS) {
		mvm_sta_link =
			rcu_dereference_protected(mvm_sta->link[link_id],
						  lockdep_is_held(&mvm->mutex));
		mvm_vif_link = mvm_vif->link[link_id];

		if (WARN_ON(!mvm_sta_link || !mvm_vif_link)) {
			ret = -EINVAL;
			goto err;
		}

		ret = iwl_mvm_mld_rm_sta_from_fw(mvm, mvm_sta_link->sta_id);
		if (WARN_ON(ret))
			goto err;

		if (vif->type == NL80211_IFTYPE_STATION)
			mvm_vif_link->ap_sta_id = IWL_MVM_INVALID_STA;

		iwl_mvm_mld_free_sta_link(mvm, mvm_sta, mvm_sta_link, link_id,
					  false);
	}

	for_each_set_bit(link_id, &links_to_add, IEEE80211_MLD_MAX_NUM_LINKS) {
		struct ieee80211_bss_conf *link_conf =
			link_conf_dereference_protected(vif, link_id);
		struct ieee80211_link_sta *link_sta =
			link_sta_dereference_protected(sta, link_id);
		mvm_vif_link = mvm_vif->link[link_id];

		if (WARN_ON(!mvm_vif_link || !link_conf || !link_sta)) {
			ret = -EINVAL;
			goto err;
		}

		if (test_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status)) {
			if (WARN_ON(!mvm_sta->link[link_id])) {
				ret = -EINVAL;
				goto err;
			}
		} else {
			if (WARN_ON(mvm_sta->link[link_id])) {
				ret = -EINVAL;
				goto err;
			}
			ret = iwl_mvm_mld_alloc_sta_link(mvm, vif, sta,
							 link_id);
			if (WARN_ON(ret))
				goto err;
		}

		link_sta->agg.max_rc_amsdu_len = 1;
		ieee80211_sta_recalc_aggregates(sta);

		mvm_sta_link =
			rcu_dereference_protected(mvm_sta->link[link_id],
						  lockdep_is_held(&mvm->mutex));

		if (WARN_ON(!mvm_sta_link)) {
			ret = -EINVAL;
			goto err;
		}

		if (vif->type == NL80211_IFTYPE_STATION)
			iwl_mvm_mld_set_ap_sta_id(sta, mvm_vif_link,
						  mvm_sta_link);

		link_sta_allocated |= BIT(link_id);

		sta_mask_added |= BIT(mvm_sta_link->sta_id);

		ret = iwl_mvm_mld_cfg_sta(mvm, sta, vif, link_sta, link_conf,
					  mvm_sta_link);
		if (WARN_ON(ret))
			goto err;

		link_sta_added_to_fw |= BIT(link_id);

		iwl_mvm_rs_add_sta_link(mvm, mvm_sta_link);
	}

	if (sta_mask_added) {
		ret = iwl_mvm_mld_update_sta_resources(mvm, vif, sta,
						       current_sta_mask,
						       current_sta_mask |
							sta_mask_added);
		if (WARN_ON(ret))
			goto err;
	}

	return 0;

err:
	/* remove all already allocated stations in FW */
	for_each_set_bit(link_id, &link_sta_added_to_fw,
			 IEEE80211_MLD_MAX_NUM_LINKS) {
		mvm_sta_link =
			rcu_dereference_protected(mvm_sta->link[link_id],
						  lockdep_is_held(&mvm->mutex));

		iwl_mvm_mld_rm_sta_from_fw(mvm, mvm_sta_link->sta_id);
	}

	/* remove all already allocated station links in driver */
	for_each_set_bit(link_id, &link_sta_allocated,
			 IEEE80211_MLD_MAX_NUM_LINKS) {
		mvm_sta_link =
			rcu_dereference_protected(mvm_sta->link[link_id],
						  lockdep_is_held(&mvm->mutex));

		iwl_mvm_mld_free_sta_link(mvm, mvm_sta, mvm_sta_link, link_id,
					  false);
	}

	return ret;
}
