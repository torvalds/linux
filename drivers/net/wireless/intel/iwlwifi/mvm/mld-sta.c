// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2022 Intel Corporation
 */
#include "mvm.h"

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
					 u16 phy_id)
{
	struct iwl_mvm_sta_cfg_cmd cmd;

	lockdep_assert_held(&mvm->mutex);

	memset(&cmd, 0, sizeof(cmd));
	cmd.sta_id = cpu_to_le32((u8)sta->sta_id);

	cmd.link_id = cpu_to_le32(phy_id);

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

/*
 * Adds an internal sta to the FW table with its queues
 */
static int iwl_mvm_mld_add_int_sta_with_queue(struct iwl_mvm *mvm,
					      struct iwl_mvm_int_sta *sta,
					      const u8 *addr, int phy_id,
					      u16 *queue, u8 tid,
					      unsigned int *_wdg_timeout)
{
	int ret, txq;
	unsigned int wdg_timeout = _wdg_timeout ? *_wdg_timeout :
		mvm->trans->trans_cfg->base_params->wd_timeout;

	if (WARN_ON_ONCE(sta->sta_id == IWL_MVM_INVALID_STA))
		return -ENOSPC;

	ret = iwl_mvm_mld_add_int_sta_to_fw(mvm, sta, addr, phy_id);
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
				   int phy_id, const u8 *addr, u8 tid,
				   unsigned int *wdg_timeout)
{
	int ret;

	lockdep_assert_held(&mvm->mutex);

	/* qmask argument is not used in the new tx api, send a don't care */
	ret = iwl_mvm_allocate_int_sta(mvm, int_sta, 0, iftype,
				       sta_type);
	if (ret)
		return ret;

	ret = iwl_mvm_mld_add_int_sta_with_queue(mvm, int_sta, addr, phy_id,
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
int iwl_mvm_mld_add_bcast_sta(struct iwl_mvm *mvm, struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm_int_sta *bsta = &mvmvif->bcast_sta;
	static const u8 _baddr[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
	const u8 *baddr = _baddr;
	unsigned int wdg_timeout =
		iwl_mvm_get_wd_timeout(mvm, vif, false, false);
	u16 *queue;

	lockdep_assert_held(&mvm->mutex);

	if (vif->type == NL80211_IFTYPE_ADHOC)
		baddr = vif->bss_conf.bssid;

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
				       mvmvif->phy_ctxt->id, baddr,
				       IWL_MAX_TID_COUNT, &wdg_timeout);
}

/* Allocate a new station entry for the sniffer station to the given vif,
 * and send it to the FW.
 */
int iwl_mvm_mld_add_snif_sta(struct iwl_mvm *mvm, struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);

	lockdep_assert_held(&mvm->mutex);

	return iwl_mvm_mld_add_int_sta(mvm, &mvm->snif_sta, &mvm->snif_queue,
				       vif->type, STATION_TYPE_BCAST_MGMT,
				       mvmvif->phy_ctxt->id, NULL,
				       IWL_MAX_TID_COUNT, NULL);
}

static int iwl_mvm_mld_disable_txq(struct iwl_mvm *mvm,
				   struct ieee80211_sta *sta,
				   u16 *queueptr, u8 tid)
{
	struct iwl_mvm_sta *mvmsta = iwl_mvm_sta_from_mac80211(sta);
	int queue = *queueptr;
	int ret = 0;

	if (mvm->sta_remove_requires_queue_remove) {
		u32 cmd_id = WIDE_ID(DATA_PATH_GROUP,
				     SCD_QUEUE_CONFIG_CMD);
		struct iwl_scd_queue_cfg_cmd remove_cmd = {
			.operation = cpu_to_le32(IWL_SCD_QUEUE_REMOVE),
			.u.remove.tid = cpu_to_le32(tid),
			.u.remove.sta_mask =
				cpu_to_le32(BIT(mvmsta->sta_id)),
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

	iwl_mvm_mld_disable_txq(mvm, NULL, queuptr, tid);

	ret = iwl_mvm_mld_rm_sta_from_fw(mvm, int_sta->sta_id);
	if (ret)
		IWL_WARN(mvm, "Failed sending remove station\n");

	iwl_mvm_dealloc_int_sta(mvm, int_sta);

	return ret;
}

int iwl_mvm_mld_rm_bcast_sta(struct iwl_mvm *mvm, struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
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

	return iwl_mvm_mld_rm_int_sta(mvm, &mvmvif->bcast_sta, true,
				      IWL_MAX_TID_COUNT, queueptr);
}

int iwl_mvm_mld_rm_snif_sta(struct iwl_mvm *mvm, struct ieee80211_vif *vif)
{
	lockdep_assert_held(&mvm->mutex);

	return iwl_mvm_mld_rm_int_sta(mvm, &mvm->snif_sta, false,
				      IWL_MAX_TID_COUNT, &mvm->snif_queue);
}

static void iwl_mvm_mld_sta_modify_disable_tx(struct iwl_mvm *mvm,
					      struct ieee80211_sta *sta,
					      bool disable)
{
	struct iwl_mvm_sta *mvm_sta = iwl_mvm_sta_from_mac80211(sta);
	struct iwl_mvm_sta_disable_tx_cmd cmd;
	int ret;

	spin_lock_bh(&mvm_sta->lock);

	if (mvm_sta->disable_tx == disable) {
		spin_unlock_bh(&mvm_sta->lock);
		return;
	}

	mvm_sta->disable_tx = disable;

	cmd.sta_id = cpu_to_le32(mvm_sta->sta_id);
	cmd.disable = cpu_to_le32(disable);

	ret = iwl_mvm_send_cmd_pdu(mvm,
				   WIDE_ID(MAC_CONF_GROUP, STA_DISABLE_TX_CMD),
				   CMD_ASYNC, sizeof(cmd), &cmd);
	if (ret)
		IWL_ERR(mvm,
			"Failed to send STA_DISABLE_TX_CMD command (%d)\n",
			ret);

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

		iwl_mvm_mld_sta_modify_disable_tx(mvm, sta, disable);
	}

	rcu_read_unlock();
}
