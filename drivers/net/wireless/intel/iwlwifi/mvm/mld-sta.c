// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2022 Intel Corporation
 */
#include "mvm.h"
#include "time-sync.h"

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
					 const u8 *addr,
					 u16 mac_id)
{
	struct iwl_mvm_sta_cfg_cmd cmd;

	lockdep_assert_held(&mvm->mutex);

	memset(&cmd, 0, sizeof(cmd));
	cmd.sta_id = cpu_to_le32((u8)sta->sta_id);

	cmd.link_id = cpu_to_le32(mac_id);

	cmd.station_type = cpu_to_le32(sta->type);

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
	struct ieee80211_sta *sta;
	struct iwl_mvm_remove_sta_cmd rm_sta_cmd = {
		.sta_id = cpu_to_le32(sta_id),
	};
	int ret;

	sta = rcu_dereference_protected(mvm->fw_id_to_mac_id[sta_id],
					lockdep_is_held(&mvm->mutex));

	/* Note: internal stations are marked as error values */
	if (!sta) {
		IWL_ERR(mvm, "Invalid station id\n");
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
static int iwl_mvm_mld_add_int_sta_with_queue(struct iwl_mvm *mvm,
					      struct iwl_mvm_int_sta *sta,
					      const u8 *addr, int mac_id,
					      u16 *queue, u8 tid,
					      unsigned int *_wdg_timeout)
{
	int ret, txq;
	unsigned int wdg_timeout = _wdg_timeout ? *_wdg_timeout :
		mvm->trans->trans_cfg->base_params->wd_timeout;

	if (WARN_ON_ONCE(sta->sta_id == IWL_MVM_INVALID_STA))
		return -ENOSPC;

	if (sta->type == STATION_TYPE_AUX)
		ret = iwl_mvm_add_aux_sta_to_fw(mvm, sta, mac_id);
	else
		ret = iwl_mvm_mld_add_int_sta_to_fw(mvm, sta, addr, mac_id);
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
				   int mac_id, const u8 *addr, u8 tid,
				   unsigned int *wdg_timeout)
{
	int ret;

	lockdep_assert_held(&mvm->mutex);

	/* qmask argument is not used in the new tx api, send a don't care */
	ret = iwl_mvm_allocate_int_sta(mvm, int_sta, 0, iftype,
				       sta_type);
	if (ret)
		return ret;

	ret = iwl_mvm_mld_add_int_sta_with_queue(mvm, int_sta, addr, mac_id,
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
		queue = &mvm->probe_queue;
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

	/* In CDB NICs we need to specify which lmac to use for aux activity
	 * using the mac_id argument place to send lmac_id to the function
	 */
	return iwl_mvm_mld_add_int_sta(mvm, &mvm->aux_sta, &mvm->aux_queue,
				       NL80211_IFTYPE_UNSPECIFIED,
				       STATION_TYPE_AUX, lmac_id, NULL,
				       IWL_MAX_TID_COUNT, NULL);
}

static int iwl_mvm_mld_disable_txq(struct iwl_mvm *mvm, int sta_id,
				   u16 *queueptr, u8 tid)
{
	int queue = *queueptr;
	int ret = 0;

	if (mvm->sta_remove_requires_queue_remove) {
		u32 cmd_id = WIDE_ID(DATA_PATH_GROUP,
				     SCD_QUEUE_CONFIG_CMD);
		struct iwl_scd_queue_cfg_cmd remove_cmd = {
			.operation = cpu_to_le32(IWL_SCD_QUEUE_REMOVE),
			.u.remove.tid = cpu_to_le32(tid),
			.u.remove.sta_mask = cpu_to_le32(BIT(sta_id)),
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
		iwl_mvm_flush_sta(mvm, int_sta, true);

	iwl_mvm_mld_disable_txq(mvm, int_sta->sta_id, queuptr, tid);

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

	switch (vif->type) {
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_ADHOC:
		queueptr = &mvm->probe_queue;
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
			       struct ieee80211_vif *vif)
{
	struct iwl_mvm_sta *mvm_sta = iwl_mvm_sta_from_mac80211(sta);
	struct iwl_mvm_vif *mvm_vif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm_sta_cfg_cmd cmd = {
		.sta_id = cpu_to_le32(mvm_sta->deflink.sta_id),
		.station_type = cpu_to_le32(mvm_sta->sta_type),
		.mfp = cpu_to_le32(sta->mfp),
	};
	/* FIXME: use proper link_id */
	unsigned int link_id = 0;
	struct iwl_mvm_vif_link_info *link_info = mvm_vif->link[link_id];
	u32 agg_size = 0, mpdu_dens = 0;

	/* when adding sta, link should exist in FW */
	if (WARN_ON(link_info->fw_link_id == IWL_MVM_FW_LINK_ID_INVALID))
		return -EINVAL;

	cmd.link_id = cpu_to_le32(link_info->fw_link_id);

	/* For now the link addr is the same as the mld addr */
	memcpy(&cmd.peer_mld_address, sta->addr, ETH_ALEN);
	memcpy(&cmd.peer_link_address, sta->addr, ETH_ALEN);

	if (mvm_sta->sta_state >= IEEE80211_STA_ASSOC)
		cmd.assoc_id = cpu_to_le32(sta->aid);

	switch (sta->deflink.rx_nss) {
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

	mpdu_dens = iwl_mvm_get_sta_ampdu_dens(sta, &agg_size);
	cmd.tx_ampdu_spacing = cpu_to_le32(mpdu_dens);
	cmd.tx_ampdu_max_size = cpu_to_le32(agg_size);

	if (sta->wme) {
		cmd.sp_length =
			cpu_to_le32(sta->max_sp ? sta->max_sp * 2 : 128);
		cmd.uapsd_acs = cpu_to_le32(iwl_mvm_get_sta_uapsd_acs(sta));
	}

	if (sta->deflink.he_cap.has_he) {
		cmd.trig_rnd_alloc =
			cpu_to_le32(vif->bss_conf.uora_exists ? 1 : 0);

		/* PPE Thresholds */
		iwl_mvm_set_sta_pkt_ext(mvm, sta, &cmd.pkt_ext);

		/* HTC flags */
		cmd.htc_flags = iwl_mvm_get_sta_htc_flags(sta);

		if (sta->deflink.he_cap.he_cap_elem.mac_cap_info[2] &
		    IEEE80211_HE_MAC_CAP2_ACK_EN)
			cmd.ack_enabled = cpu_to_le32(1);
	}

	return iwl_mvm_mld_send_sta_cmd(mvm, &cmd);
}

int iwl_mvm_mld_add_sta(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			struct ieee80211_sta *sta)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm_sta *mvm_sta = iwl_mvm_sta_from_mac80211(sta);
	int sta_id, ret = 0;

	lockdep_assert_held(&mvm->mutex);

	if (!test_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status))
		sta_id = iwl_mvm_find_free_sta_id(mvm,
						  ieee80211_vif_type_p2p(vif));
	else
		sta_id = mvm_sta->deflink.sta_id;

	if (sta_id == IWL_MVM_INVALID_STA)
		return -ENOSPC;

	spin_lock_init(&mvm_sta->lock);

	/* if this is a HW restart re-alloc existing queues */
	if (test_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status)) {
		struct iwl_mvm_int_sta tmp_sta = {
			.sta_id = sta_id,
			.type = mvm_sta->sta_type,
		};

		/* First add an empty station since allocating
		 * a queue requires a valid station
		 */
		ret = iwl_mvm_mld_add_int_sta_to_fw(mvm, &tmp_sta,
						    vif->bss_conf.bssid,
						    mvmvif->id);
		if (ret)
			return ret;

		iwl_mvm_realloc_queues_after_restart(mvm, sta);
	} else {
		ret = iwl_mvm_sta_init(mvm, vif, sta, sta_id,
				       STATION_TYPE_PEER);
	}

	ret = iwl_mvm_mld_cfg_sta(mvm, sta, vif);
	if (ret)
		return ret;

	if (vif->type == NL80211_IFTYPE_STATION) {
		if (!sta->tdls) {
			WARN_ON(mvmvif->deflink.ap_sta_id != IWL_MVM_INVALID_STA);
			mvmvif->deflink.ap_sta_id = sta_id;
		} else {
			WARN_ON(mvmvif->deflink.ap_sta_id == IWL_MVM_INVALID_STA);
		}
	}

	rcu_assign_pointer(mvm->fw_id_to_mac_id[sta_id], sta);

	return 0;
}

int iwl_mvm_mld_update_sta(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			   struct ieee80211_sta *sta)
{
	lockdep_assert_held(&mvm->mutex);

	return iwl_mvm_mld_cfg_sta(mvm, sta, vif);
}

static void iwl_mvm_mld_disable_sta_queues(struct iwl_mvm *mvm,
					   struct ieee80211_vif *vif,
					   struct ieee80211_sta *sta)
{
	struct iwl_mvm_sta *mvm_sta = iwl_mvm_sta_from_mac80211(sta);
	int i;

	lockdep_assert_held(&mvm->mutex);

	for (i = 0; i < ARRAY_SIZE(mvm_sta->tid_data); i++) {
		if (mvm_sta->tid_data[i].txq_id == IWL_MVM_INVALID_QUEUE)
			continue;

		iwl_mvm_mld_disable_txq(mvm, mvm_sta->deflink.sta_id,
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
	int ret;

	lockdep_assert_held(&mvm->mutex);

	kfree(mvm_sta->dup_data);

	/* flush its queues here since we are freeing mvm_sta */
	ret = iwl_mvm_flush_sta(mvm, mvm_sta, false);
	if (ret)
		return ret;
	ret = iwl_mvm_wait_sta_queues_empty(mvm, mvm_sta);
	if (ret)
		return ret;

	iwl_mvm_mld_disable_sta_queues(mvm, vif, sta);

	if (iwl_mvm_sta_del(mvm, vif, sta, &ret))
		return ret;

	ret = iwl_mvm_mld_rm_sta_from_fw(mvm, mvm_sta->deflink.sta_id);
	RCU_INIT_POINTER(mvm->fw_id_to_mac_id[mvm_sta->deflink.sta_id], NULL);

	return ret;
}

int iwl_mvm_mld_rm_sta_id(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			  u8 sta_id)
{
	int ret = iwl_mvm_mld_rm_sta_from_fw(mvm, sta_id);

	lockdep_assert_held(&mvm->mutex);

	RCU_INIT_POINTER(mvm->fw_id_to_mac_id[sta_id], NULL);
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
