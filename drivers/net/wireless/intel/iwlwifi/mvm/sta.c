// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2012-2015, 2018-2021 Intel Corporation
 * Copyright (C) 2013-2015 Intel Mobile Communications GmbH
 * Copyright (C) 2016-2017 Intel Deutschland GmbH
 */
#include <net/mac80211.h>

#include "mvm.h"
#include "sta.h"
#include "rs.h"

/*
 * New version of ADD_STA_sta command added new fields at the end of the
 * structure, so sending the size of the relevant API's structure is enough to
 * support both API versions.
 */
static inline int iwl_mvm_add_sta_cmd_size(struct iwl_mvm *mvm)
{
	if (iwl_mvm_has_new_rx_api(mvm) ||
	    fw_has_api(&mvm->fw->ucode_capa, IWL_UCODE_TLV_API_STA_TYPE))
		return sizeof(struct iwl_mvm_add_sta_cmd);
	else
		return sizeof(struct iwl_mvm_add_sta_cmd_v7);
}

static int iwl_mvm_find_free_sta_id(struct iwl_mvm *mvm,
				    enum nl80211_iftype iftype)
{
	int sta_id;
	u32 reserved_ids = 0;

	BUILD_BUG_ON(IWL_MVM_STATION_COUNT_MAX > 32);
	WARN_ON_ONCE(test_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status));

	lockdep_assert_held(&mvm->mutex);

	/* d0i3/d3 assumes the AP's sta_id (of sta vif) is 0. reserve it. */
	if (iftype != NL80211_IFTYPE_STATION)
		reserved_ids = BIT(0);

	/* Don't take rcu_read_lock() since we are protected by mvm->mutex */
	for (sta_id = 0; sta_id < mvm->fw->ucode_capa.num_stations; sta_id++) {
		if (BIT(sta_id) & reserved_ids)
			continue;

		if (!rcu_dereference_protected(mvm->fw_id_to_mac_id[sta_id],
					       lockdep_is_held(&mvm->mutex)))
			return sta_id;
	}
	return IWL_MVM_INVALID_STA;
}

/* send station add/update command to firmware */
int iwl_mvm_sta_send_to_fw(struct iwl_mvm *mvm, struct ieee80211_sta *sta,
			   bool update, unsigned int flags)
{
	struct iwl_mvm_sta *mvm_sta = iwl_mvm_sta_from_mac80211(sta);
	struct iwl_mvm_add_sta_cmd add_sta_cmd = {
		.sta_id = mvm_sta->sta_id,
		.mac_id_n_color = cpu_to_le32(mvm_sta->mac_id_n_color),
		.add_modify = update ? 1 : 0,
		.station_flags_msk = cpu_to_le32(STA_FLG_FAT_EN_MSK |
						 STA_FLG_MIMO_EN_MSK |
						 STA_FLG_RTS_MIMO_PROT),
		.tid_disable_tx = cpu_to_le16(mvm_sta->tid_disable_agg),
	};
	int ret;
	u32 status;
	u32 agg_size = 0, mpdu_dens = 0;

	if (fw_has_api(&mvm->fw->ucode_capa, IWL_UCODE_TLV_API_STA_TYPE))
		add_sta_cmd.station_type = mvm_sta->sta_type;

	if (!update || (flags & STA_MODIFY_QUEUES)) {
		memcpy(&add_sta_cmd.addr, sta->addr, ETH_ALEN);

		if (!iwl_mvm_has_new_tx_api(mvm)) {
			add_sta_cmd.tfd_queue_msk =
				cpu_to_le32(mvm_sta->tfd_queue_msk);

			if (flags & STA_MODIFY_QUEUES)
				add_sta_cmd.modify_mask |= STA_MODIFY_QUEUES;
		} else {
			WARN_ON(flags & STA_MODIFY_QUEUES);
		}
	}

	switch (sta->bandwidth) {
	case IEEE80211_STA_RX_BW_320:
	case IEEE80211_STA_RX_BW_160:
		add_sta_cmd.station_flags |= cpu_to_le32(STA_FLG_FAT_EN_160MHZ);
		fallthrough;
	case IEEE80211_STA_RX_BW_80:
		add_sta_cmd.station_flags |= cpu_to_le32(STA_FLG_FAT_EN_80MHZ);
		fallthrough;
	case IEEE80211_STA_RX_BW_40:
		add_sta_cmd.station_flags |= cpu_to_le32(STA_FLG_FAT_EN_40MHZ);
		fallthrough;
	case IEEE80211_STA_RX_BW_20:
		if (sta->ht_cap.ht_supported)
			add_sta_cmd.station_flags |=
				cpu_to_le32(STA_FLG_FAT_EN_20MHZ);
		break;
	}

	switch (sta->rx_nss) {
	case 1:
		add_sta_cmd.station_flags |= cpu_to_le32(STA_FLG_MIMO_EN_SISO);
		break;
	case 2:
		add_sta_cmd.station_flags |= cpu_to_le32(STA_FLG_MIMO_EN_MIMO2);
		break;
	case 3 ... 8:
		add_sta_cmd.station_flags |= cpu_to_le32(STA_FLG_MIMO_EN_MIMO3);
		break;
	}

	switch (sta->smps_mode) {
	case IEEE80211_SMPS_AUTOMATIC:
	case IEEE80211_SMPS_NUM_MODES:
		WARN_ON(1);
		break;
	case IEEE80211_SMPS_STATIC:
		/* override NSS */
		add_sta_cmd.station_flags &= ~cpu_to_le32(STA_FLG_MIMO_EN_MSK);
		add_sta_cmd.station_flags |= cpu_to_le32(STA_FLG_MIMO_EN_SISO);
		break;
	case IEEE80211_SMPS_DYNAMIC:
		add_sta_cmd.station_flags |= cpu_to_le32(STA_FLG_RTS_MIMO_PROT);
		break;
	case IEEE80211_SMPS_OFF:
		/* nothing */
		break;
	}

	if (sta->ht_cap.ht_supported) {
		add_sta_cmd.station_flags_msk |=
			cpu_to_le32(STA_FLG_MAX_AGG_SIZE_MSK |
				    STA_FLG_AGG_MPDU_DENS_MSK);

		mpdu_dens = sta->ht_cap.ampdu_density;
	}

	if (mvm_sta->vif->bss_conf.chandef.chan->band == NL80211_BAND_6GHZ) {
		add_sta_cmd.station_flags_msk |=
			cpu_to_le32(STA_FLG_MAX_AGG_SIZE_MSK |
				    STA_FLG_AGG_MPDU_DENS_MSK);

		mpdu_dens = le16_get_bits(sta->he_6ghz_capa.capa,
					  IEEE80211_HE_6GHZ_CAP_MIN_MPDU_START);
		agg_size = le16_get_bits(sta->he_6ghz_capa.capa,
				IEEE80211_HE_6GHZ_CAP_MAX_AMPDU_LEN_EXP);
	} else
	if (sta->vht_cap.vht_supported) {
		agg_size = sta->vht_cap.cap &
			IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK;
		agg_size >>=
			IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_SHIFT;
	} else if (sta->ht_cap.ht_supported) {
		agg_size = sta->ht_cap.ampdu_factor;
	}

	/* D6.0 10.12.2 A-MPDU length limit rules
	 * A STA indicates the maximum length of the A-MPDU preEOF padding
	 * that it can receive in an HE PPDU in the Maximum A-MPDU Length
	 * Exponent field in its HT Capabilities, VHT Capabilities,
	 * and HE 6 GHz Band Capabilities elements (if present) and the
	 * Maximum AMPDU Length Exponent Extension field in its HE
	 * Capabilities element
	 */
	if (sta->he_cap.has_he)
		agg_size += u8_get_bits(sta->he_cap.he_cap_elem.mac_cap_info[3],
					IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_MASK);

	/* Limit to max A-MPDU supported by FW */
	if (agg_size > (STA_FLG_MAX_AGG_SIZE_4M >> STA_FLG_MAX_AGG_SIZE_SHIFT))
		agg_size = (STA_FLG_MAX_AGG_SIZE_4M >>
			    STA_FLG_MAX_AGG_SIZE_SHIFT);

	add_sta_cmd.station_flags |=
		cpu_to_le32(agg_size << STA_FLG_MAX_AGG_SIZE_SHIFT);
	add_sta_cmd.station_flags |=
		cpu_to_le32(mpdu_dens << STA_FLG_AGG_MPDU_DENS_SHIFT);
	if (mvm_sta->sta_state >= IEEE80211_STA_ASSOC)
		add_sta_cmd.assoc_id = cpu_to_le16(sta->aid);

	if (sta->wme) {
		add_sta_cmd.modify_mask |= STA_MODIFY_UAPSD_ACS;

		if (sta->uapsd_queues & IEEE80211_WMM_IE_STA_QOSINFO_AC_BK)
			add_sta_cmd.uapsd_acs |= BIT(AC_BK);
		if (sta->uapsd_queues & IEEE80211_WMM_IE_STA_QOSINFO_AC_BE)
			add_sta_cmd.uapsd_acs |= BIT(AC_BE);
		if (sta->uapsd_queues & IEEE80211_WMM_IE_STA_QOSINFO_AC_VI)
			add_sta_cmd.uapsd_acs |= BIT(AC_VI);
		if (sta->uapsd_queues & IEEE80211_WMM_IE_STA_QOSINFO_AC_VO)
			add_sta_cmd.uapsd_acs |= BIT(AC_VO);
		add_sta_cmd.uapsd_acs |= add_sta_cmd.uapsd_acs << 4;
		add_sta_cmd.sp_length = sta->max_sp ? sta->max_sp * 2 : 128;
	}

	status = ADD_STA_SUCCESS;
	ret = iwl_mvm_send_cmd_pdu_status(mvm, ADD_STA,
					  iwl_mvm_add_sta_cmd_size(mvm),
					  &add_sta_cmd, &status);
	if (ret)
		return ret;

	switch (status & IWL_ADD_STA_STATUS_MASK) {
	case ADD_STA_SUCCESS:
		IWL_DEBUG_ASSOC(mvm, "ADD_STA PASSED\n");
		break;
	default:
		ret = -EIO;
		IWL_ERR(mvm, "ADD_STA failed\n");
		break;
	}

	return ret;
}

static void iwl_mvm_rx_agg_session_expired(struct timer_list *t)
{
	struct iwl_mvm_baid_data *data =
		from_timer(data, t, session_timer);
	struct iwl_mvm_baid_data __rcu **rcu_ptr = data->rcu_ptr;
	struct iwl_mvm_baid_data *ba_data;
	struct ieee80211_sta *sta;
	struct iwl_mvm_sta *mvm_sta;
	unsigned long timeout;

	rcu_read_lock();

	ba_data = rcu_dereference(*rcu_ptr);

	if (WARN_ON(!ba_data))
		goto unlock;

	if (!ba_data->timeout)
		goto unlock;

	timeout = ba_data->last_rx + TU_TO_JIFFIES(ba_data->timeout * 2);
	if (time_is_after_jiffies(timeout)) {
		mod_timer(&ba_data->session_timer, timeout);
		goto unlock;
	}

	/* Timer expired */
	sta = rcu_dereference(ba_data->mvm->fw_id_to_mac_id[ba_data->sta_id]);

	/*
	 * sta should be valid unless the following happens:
	 * The firmware asserts which triggers a reconfig flow, but
	 * the reconfig fails before we set the pointer to sta into
	 * the fw_id_to_mac_id pointer table. Mac80211 can't stop
	 * A-MDPU and hence the timer continues to run. Then, the
	 * timer expires and sta is NULL.
	 */
	if (!sta)
		goto unlock;

	mvm_sta = iwl_mvm_sta_from_mac80211(sta);
	ieee80211_rx_ba_timer_expired(mvm_sta->vif,
				      sta->addr, ba_data->tid);
unlock:
	rcu_read_unlock();
}

/* Disable aggregations for a bitmap of TIDs for a given station */
static int iwl_mvm_invalidate_sta_queue(struct iwl_mvm *mvm, int queue,
					unsigned long disable_agg_tids,
					bool remove_queue)
{
	struct iwl_mvm_add_sta_cmd cmd = {};
	struct ieee80211_sta *sta;
	struct iwl_mvm_sta *mvmsta;
	u32 status;
	u8 sta_id;

	if (WARN_ON(iwl_mvm_has_new_tx_api(mvm)))
		return -EINVAL;

	sta_id = mvm->queue_info[queue].ra_sta_id;

	rcu_read_lock();

	sta = rcu_dereference(mvm->fw_id_to_mac_id[sta_id]);

	if (WARN_ON_ONCE(IS_ERR_OR_NULL(sta))) {
		rcu_read_unlock();
		return -EINVAL;
	}

	mvmsta = iwl_mvm_sta_from_mac80211(sta);

	mvmsta->tid_disable_agg |= disable_agg_tids;

	cmd.mac_id_n_color = cpu_to_le32(mvmsta->mac_id_n_color);
	cmd.sta_id = mvmsta->sta_id;
	cmd.add_modify = STA_MODE_MODIFY;
	cmd.modify_mask = STA_MODIFY_QUEUES;
	if (disable_agg_tids)
		cmd.modify_mask |= STA_MODIFY_TID_DISABLE_TX;
	if (remove_queue)
		cmd.modify_mask |= STA_MODIFY_QUEUE_REMOVAL;
	cmd.tfd_queue_msk = cpu_to_le32(mvmsta->tfd_queue_msk);
	cmd.tid_disable_tx = cpu_to_le16(mvmsta->tid_disable_agg);

	rcu_read_unlock();

	/* Notify FW of queue removal from the STA queues */
	status = ADD_STA_SUCCESS;
	return iwl_mvm_send_cmd_pdu_status(mvm, ADD_STA,
					   iwl_mvm_add_sta_cmd_size(mvm),
					   &cmd, &status);
}

static int iwl_mvm_disable_txq(struct iwl_mvm *mvm, struct ieee80211_sta *sta,
			       u16 *queueptr, u8 tid, u8 flags)
{
	int queue = *queueptr;
	struct iwl_scd_txq_cfg_cmd cmd = {
		.scd_queue = queue,
		.action = SCD_CFG_DISABLE_QUEUE,
	};
	int ret;

	if (iwl_mvm_has_new_tx_api(mvm)) {
		iwl_trans_txq_free(mvm->trans, queue);
		*queueptr = IWL_MVM_INVALID_QUEUE;

		return 0;
	}

	if (WARN_ON(mvm->queue_info[queue].tid_bitmap == 0))
		return 0;

	mvm->queue_info[queue].tid_bitmap &= ~BIT(tid);

	cmd.action = mvm->queue_info[queue].tid_bitmap ?
		SCD_CFG_ENABLE_QUEUE : SCD_CFG_DISABLE_QUEUE;
	if (cmd.action == SCD_CFG_DISABLE_QUEUE)
		mvm->queue_info[queue].status = IWL_MVM_QUEUE_FREE;

	IWL_DEBUG_TX_QUEUES(mvm,
			    "Disabling TXQ #%d tids=0x%x\n",
			    queue,
			    mvm->queue_info[queue].tid_bitmap);

	/* If the queue is still enabled - nothing left to do in this func */
	if (cmd.action == SCD_CFG_ENABLE_QUEUE)
		return 0;

	cmd.sta_id = mvm->queue_info[queue].ra_sta_id;
	cmd.tid = mvm->queue_info[queue].txq_tid;

	/* Make sure queue info is correct even though we overwrite it */
	WARN(mvm->queue_info[queue].tid_bitmap,
	     "TXQ #%d info out-of-sync - tids=0x%x\n",
	     queue, mvm->queue_info[queue].tid_bitmap);

	/* If we are here - the queue is freed and we can zero out these vals */
	mvm->queue_info[queue].tid_bitmap = 0;

	if (sta) {
		struct iwl_mvm_txq *mvmtxq =
			iwl_mvm_txq_from_tid(sta, tid);

		mvmtxq->txq_id = IWL_MVM_INVALID_QUEUE;
	}

	/* Regardless if this is a reserved TXQ for a STA - mark it as false */
	mvm->queue_info[queue].reserved = false;

	iwl_trans_txq_disable(mvm->trans, queue, false);
	ret = iwl_mvm_send_cmd_pdu(mvm, SCD_QUEUE_CFG, flags,
				   sizeof(struct iwl_scd_txq_cfg_cmd), &cmd);

	if (ret)
		IWL_ERR(mvm, "Failed to disable queue %d (ret=%d)\n",
			queue, ret);
	return ret;
}

static int iwl_mvm_get_queue_agg_tids(struct iwl_mvm *mvm, int queue)
{
	struct ieee80211_sta *sta;
	struct iwl_mvm_sta *mvmsta;
	unsigned long tid_bitmap;
	unsigned long agg_tids = 0;
	u8 sta_id;
	int tid;

	lockdep_assert_held(&mvm->mutex);

	if (WARN_ON(iwl_mvm_has_new_tx_api(mvm)))
		return -EINVAL;

	sta_id = mvm->queue_info[queue].ra_sta_id;
	tid_bitmap = mvm->queue_info[queue].tid_bitmap;

	sta = rcu_dereference_protected(mvm->fw_id_to_mac_id[sta_id],
					lockdep_is_held(&mvm->mutex));

	if (WARN_ON_ONCE(IS_ERR_OR_NULL(sta)))
		return -EINVAL;

	mvmsta = iwl_mvm_sta_from_mac80211(sta);

	spin_lock_bh(&mvmsta->lock);
	for_each_set_bit(tid, &tid_bitmap, IWL_MAX_TID_COUNT + 1) {
		if (mvmsta->tid_data[tid].state == IWL_AGG_ON)
			agg_tids |= BIT(tid);
	}
	spin_unlock_bh(&mvmsta->lock);

	return agg_tids;
}

/*
 * Remove a queue from a station's resources.
 * Note that this only marks as free. It DOESN'T delete a BA agreement, and
 * doesn't disable the queue
 */
static int iwl_mvm_remove_sta_queue_marking(struct iwl_mvm *mvm, int queue)
{
	struct ieee80211_sta *sta;
	struct iwl_mvm_sta *mvmsta;
	unsigned long tid_bitmap;
	unsigned long disable_agg_tids = 0;
	u8 sta_id;
	int tid;

	lockdep_assert_held(&mvm->mutex);

	if (WARN_ON(iwl_mvm_has_new_tx_api(mvm)))
		return -EINVAL;

	sta_id = mvm->queue_info[queue].ra_sta_id;
	tid_bitmap = mvm->queue_info[queue].tid_bitmap;

	rcu_read_lock();

	sta = rcu_dereference(mvm->fw_id_to_mac_id[sta_id]);

	if (WARN_ON_ONCE(IS_ERR_OR_NULL(sta))) {
		rcu_read_unlock();
		return 0;
	}

	mvmsta = iwl_mvm_sta_from_mac80211(sta);

	spin_lock_bh(&mvmsta->lock);
	/* Unmap MAC queues and TIDs from this queue */
	for_each_set_bit(tid, &tid_bitmap, IWL_MAX_TID_COUNT + 1) {
		struct iwl_mvm_txq *mvmtxq =
			iwl_mvm_txq_from_tid(sta, tid);

		if (mvmsta->tid_data[tid].state == IWL_AGG_ON)
			disable_agg_tids |= BIT(tid);
		mvmsta->tid_data[tid].txq_id = IWL_MVM_INVALID_QUEUE;

		mvmtxq->txq_id = IWL_MVM_INVALID_QUEUE;
	}

	mvmsta->tfd_queue_msk &= ~BIT(queue); /* Don't use this queue anymore */
	spin_unlock_bh(&mvmsta->lock);

	rcu_read_unlock();

	/*
	 * The TX path may have been using this TXQ_ID from the tid_data,
	 * so make sure it's no longer running so that we can safely reuse
	 * this TXQ later. We've set all the TIDs to IWL_MVM_INVALID_QUEUE
	 * above, but nothing guarantees we've stopped using them. Thus,
	 * without this, we could get to iwl_mvm_disable_txq() and remove
	 * the queue while still sending frames to it.
	 */
	synchronize_net();

	return disable_agg_tids;
}

static int iwl_mvm_free_inactive_queue(struct iwl_mvm *mvm, int queue,
				       struct ieee80211_sta *old_sta,
				       u8 new_sta_id)
{
	struct iwl_mvm_sta *mvmsta;
	u8 sta_id, tid;
	unsigned long disable_agg_tids = 0;
	bool same_sta;
	u16 queue_tmp = queue;
	int ret;

	lockdep_assert_held(&mvm->mutex);

	if (WARN_ON(iwl_mvm_has_new_tx_api(mvm)))
		return -EINVAL;

	sta_id = mvm->queue_info[queue].ra_sta_id;
	tid = mvm->queue_info[queue].txq_tid;

	same_sta = sta_id == new_sta_id;

	mvmsta = iwl_mvm_sta_from_staid_protected(mvm, sta_id);
	if (WARN_ON(!mvmsta))
		return -EINVAL;

	disable_agg_tids = iwl_mvm_remove_sta_queue_marking(mvm, queue);
	/* Disable the queue */
	if (disable_agg_tids)
		iwl_mvm_invalidate_sta_queue(mvm, queue,
					     disable_agg_tids, false);

	ret = iwl_mvm_disable_txq(mvm, old_sta, &queue_tmp, tid, 0);
	if (ret) {
		IWL_ERR(mvm,
			"Failed to free inactive queue %d (ret=%d)\n",
			queue, ret);

		return ret;
	}

	/* If TXQ is allocated to another STA, update removal in FW */
	if (!same_sta)
		iwl_mvm_invalidate_sta_queue(mvm, queue, 0, true);

	return 0;
}

static int iwl_mvm_get_shared_queue(struct iwl_mvm *mvm,
				    unsigned long tfd_queue_mask, u8 ac)
{
	int queue = 0;
	u8 ac_to_queue[IEEE80211_NUM_ACS];
	int i;

	/*
	 * This protects us against grabbing a queue that's being reconfigured
	 * by the inactivity checker.
	 */
	lockdep_assert_held(&mvm->mutex);

	if (WARN_ON(iwl_mvm_has_new_tx_api(mvm)))
		return -EINVAL;

	memset(&ac_to_queue, IEEE80211_INVAL_HW_QUEUE, sizeof(ac_to_queue));

	/* See what ACs the existing queues for this STA have */
	for_each_set_bit(i, &tfd_queue_mask, IWL_MVM_DQA_MAX_DATA_QUEUE) {
		/* Only DATA queues can be shared */
		if (i < IWL_MVM_DQA_MIN_DATA_QUEUE &&
		    i != IWL_MVM_DQA_BSS_CLIENT_QUEUE)
			continue;

		ac_to_queue[mvm->queue_info[i].mac80211_ac] = i;
	}

	/*
	 * The queue to share is chosen only from DATA queues as follows (in
	 * descending priority):
	 * 1. An AC_BE queue
	 * 2. Same AC queue
	 * 3. Highest AC queue that is lower than new AC
	 * 4. Any existing AC (there always is at least 1 DATA queue)
	 */

	/* Priority 1: An AC_BE queue */
	if (ac_to_queue[IEEE80211_AC_BE] != IEEE80211_INVAL_HW_QUEUE)
		queue = ac_to_queue[IEEE80211_AC_BE];
	/* Priority 2: Same AC queue */
	else if (ac_to_queue[ac] != IEEE80211_INVAL_HW_QUEUE)
		queue = ac_to_queue[ac];
	/* Priority 3a: If new AC is VO and VI exists - use VI */
	else if (ac == IEEE80211_AC_VO &&
		 ac_to_queue[IEEE80211_AC_VI] != IEEE80211_INVAL_HW_QUEUE)
		queue = ac_to_queue[IEEE80211_AC_VI];
	/* Priority 3b: No BE so only AC less than the new one is BK */
	else if (ac_to_queue[IEEE80211_AC_BK] != IEEE80211_INVAL_HW_QUEUE)
		queue = ac_to_queue[IEEE80211_AC_BK];
	/* Priority 4a: No BE nor BK - use VI if exists */
	else if (ac_to_queue[IEEE80211_AC_VI] != IEEE80211_INVAL_HW_QUEUE)
		queue = ac_to_queue[IEEE80211_AC_VI];
	/* Priority 4b: No BE, BK nor VI - use VO if exists */
	else if (ac_to_queue[IEEE80211_AC_VO] != IEEE80211_INVAL_HW_QUEUE)
		queue = ac_to_queue[IEEE80211_AC_VO];

	/* Make sure queue found (or not) is legal */
	if (!iwl_mvm_is_dqa_data_queue(mvm, queue) &&
	    !iwl_mvm_is_dqa_mgmt_queue(mvm, queue) &&
	    (queue != IWL_MVM_DQA_BSS_CLIENT_QUEUE)) {
		IWL_ERR(mvm, "No DATA queues available to share\n");
		return -ENOSPC;
	}

	return queue;
}

/*
 * If a given queue has a higher AC than the TID stream that is being compared
 * to, the queue needs to be redirected to the lower AC. This function does that
 * in such a case, otherwise - if no redirection required - it does nothing,
 * unless the %force param is true.
 */
static int iwl_mvm_redirect_queue(struct iwl_mvm *mvm, int queue, int tid,
				  int ac, int ssn, unsigned int wdg_timeout,
				  bool force, struct iwl_mvm_txq *txq)
{
	struct iwl_scd_txq_cfg_cmd cmd = {
		.scd_queue = queue,
		.action = SCD_CFG_DISABLE_QUEUE,
	};
	bool shared_queue;
	int ret;

	if (WARN_ON(iwl_mvm_has_new_tx_api(mvm)))
		return -EINVAL;

	/*
	 * If the AC is lower than current one - FIFO needs to be redirected to
	 * the lowest one of the streams in the queue. Check if this is needed
	 * here.
	 * Notice that the enum ieee80211_ac_numbers is "flipped", so BK is with
	 * value 3 and VO with value 0, so to check if ac X is lower than ac Y
	 * we need to check if the numerical value of X is LARGER than of Y.
	 */
	if (ac <= mvm->queue_info[queue].mac80211_ac && !force) {
		IWL_DEBUG_TX_QUEUES(mvm,
				    "No redirection needed on TXQ #%d\n",
				    queue);
		return 0;
	}

	cmd.sta_id = mvm->queue_info[queue].ra_sta_id;
	cmd.tx_fifo = iwl_mvm_ac_to_tx_fifo[mvm->queue_info[queue].mac80211_ac];
	cmd.tid = mvm->queue_info[queue].txq_tid;
	shared_queue = hweight16(mvm->queue_info[queue].tid_bitmap) > 1;

	IWL_DEBUG_TX_QUEUES(mvm, "Redirecting TXQ #%d to FIFO #%d\n",
			    queue, iwl_mvm_ac_to_tx_fifo[ac]);

	/* Stop the queue and wait for it to empty */
	txq->stopped = true;

	ret = iwl_trans_wait_tx_queues_empty(mvm->trans, BIT(queue));
	if (ret) {
		IWL_ERR(mvm, "Error draining queue %d before reconfig\n",
			queue);
		ret = -EIO;
		goto out;
	}

	/* Before redirecting the queue we need to de-activate it */
	iwl_trans_txq_disable(mvm->trans, queue, false);
	ret = iwl_mvm_send_cmd_pdu(mvm, SCD_QUEUE_CFG, 0, sizeof(cmd), &cmd);
	if (ret)
		IWL_ERR(mvm, "Failed SCD disable TXQ %d (ret=%d)\n", queue,
			ret);

	/* Make sure the SCD wrptr is correctly set before reconfiguring */
	iwl_trans_txq_enable_cfg(mvm->trans, queue, ssn, NULL, wdg_timeout);

	/* Update the TID "owner" of the queue */
	mvm->queue_info[queue].txq_tid = tid;

	/* TODO: Work-around SCD bug when moving back by multiples of 0x40 */

	/* Redirect to lower AC */
	iwl_mvm_reconfig_scd(mvm, queue, iwl_mvm_ac_to_tx_fifo[ac],
			     cmd.sta_id, tid, IWL_FRAME_LIMIT, ssn);

	/* Update AC marking of the queue */
	mvm->queue_info[queue].mac80211_ac = ac;

	/*
	 * Mark queue as shared in transport if shared
	 * Note this has to be done after queue enablement because enablement
	 * can also set this value, and there is no indication there to shared
	 * queues
	 */
	if (shared_queue)
		iwl_trans_txq_set_shared_mode(mvm->trans, queue, true);

out:
	/* Continue using the queue */
	txq->stopped = false;

	return ret;
}

static int iwl_mvm_find_free_queue(struct iwl_mvm *mvm, u8 sta_id,
				   u8 minq, u8 maxq)
{
	int i;

	lockdep_assert_held(&mvm->mutex);

	if (WARN(maxq >= mvm->trans->trans_cfg->base_params->num_of_queues,
		 "max queue %d >= num_of_queues (%d)", maxq,
		 mvm->trans->trans_cfg->base_params->num_of_queues))
		maxq = mvm->trans->trans_cfg->base_params->num_of_queues - 1;

	/* This should not be hit with new TX path */
	if (WARN_ON(iwl_mvm_has_new_tx_api(mvm)))
		return -ENOSPC;

	/* Start by looking for a free queue */
	for (i = minq; i <= maxq; i++)
		if (mvm->queue_info[i].tid_bitmap == 0 &&
		    mvm->queue_info[i].status == IWL_MVM_QUEUE_FREE)
			return i;

	return -ENOSPC;
}

static int iwl_mvm_tvqm_enable_txq(struct iwl_mvm *mvm,
				   u8 sta_id, u8 tid, unsigned int timeout)
{
	int queue, size;

	if (tid == IWL_MAX_TID_COUNT) {
		tid = IWL_MGMT_TID;
		size = max_t(u32, IWL_MGMT_QUEUE_SIZE,
			     mvm->trans->cfg->min_txq_size);
	} else {
		struct ieee80211_sta *sta;

		rcu_read_lock();
		sta = rcu_dereference(mvm->fw_id_to_mac_id[sta_id]);

		/* this queue isn't used for traffic (cab_queue) */
		if (IS_ERR_OR_NULL(sta)) {
			size = IWL_MGMT_QUEUE_SIZE;
		} else if (sta->he_cap.has_he) {
			/* support for 256 ba size */
			size = IWL_DEFAULT_QUEUE_SIZE_HE;
		} else {
			size = IWL_DEFAULT_QUEUE_SIZE;
		}

		rcu_read_unlock();
	}

	/* take the min with bc tbl entries allowed */
	size = min_t(u32, size, mvm->trans->txqs.bc_tbl_size / sizeof(u16));

	/* size needs to be power of 2 values for calculating read/write pointers */
	size = rounddown_pow_of_two(size);

	do {
		__le16 enable = cpu_to_le16(TX_QUEUE_CFG_ENABLE_QUEUE);

		queue = iwl_trans_txq_alloc(mvm->trans, enable,
					    sta_id, tid, SCD_QUEUE_CFG,
					    size, timeout);

		if (queue < 0)
			IWL_DEBUG_TX_QUEUES(mvm,
					    "Failed allocating TXQ of size %d for sta %d tid %d, ret: %d\n",
					    size, sta_id, tid, queue);
		size /= 2;
	} while (queue < 0 && size >= 16);

	if (queue < 0)
		return queue;

	IWL_DEBUG_TX_QUEUES(mvm, "Enabling TXQ #%d for sta %d tid %d\n",
			    queue, sta_id, tid);

	return queue;
}

static int iwl_mvm_sta_alloc_queue_tvqm(struct iwl_mvm *mvm,
					struct ieee80211_sta *sta, u8 ac,
					int tid)
{
	struct iwl_mvm_sta *mvmsta = iwl_mvm_sta_from_mac80211(sta);
	struct iwl_mvm_txq *mvmtxq =
		iwl_mvm_txq_from_tid(sta, tid);
	unsigned int wdg_timeout =
		iwl_mvm_get_wd_timeout(mvm, mvmsta->vif, false, false);
	int queue = -1;

	lockdep_assert_held(&mvm->mutex);

	IWL_DEBUG_TX_QUEUES(mvm,
			    "Allocating queue for sta %d on tid %d\n",
			    mvmsta->sta_id, tid);
	queue = iwl_mvm_tvqm_enable_txq(mvm, mvmsta->sta_id, tid, wdg_timeout);
	if (queue < 0)
		return queue;

	mvmtxq->txq_id = queue;
	mvm->tvqm_info[queue].txq_tid = tid;
	mvm->tvqm_info[queue].sta_id = mvmsta->sta_id;

	IWL_DEBUG_TX_QUEUES(mvm, "Allocated queue is %d\n", queue);

	spin_lock_bh(&mvmsta->lock);
	mvmsta->tid_data[tid].txq_id = queue;
	spin_unlock_bh(&mvmsta->lock);

	return 0;
}

static bool iwl_mvm_update_txq_mapping(struct iwl_mvm *mvm,
				       struct ieee80211_sta *sta,
				       int queue, u8 sta_id, u8 tid)
{
	bool enable_queue = true;

	/* Make sure this TID isn't already enabled */
	if (mvm->queue_info[queue].tid_bitmap & BIT(tid)) {
		IWL_ERR(mvm, "Trying to enable TXQ %d with existing TID %d\n",
			queue, tid);
		return false;
	}

	/* Update mappings and refcounts */
	if (mvm->queue_info[queue].tid_bitmap)
		enable_queue = false;

	mvm->queue_info[queue].tid_bitmap |= BIT(tid);
	mvm->queue_info[queue].ra_sta_id = sta_id;

	if (enable_queue) {
		if (tid != IWL_MAX_TID_COUNT)
			mvm->queue_info[queue].mac80211_ac =
				tid_to_mac80211_ac[tid];
		else
			mvm->queue_info[queue].mac80211_ac = IEEE80211_AC_VO;

		mvm->queue_info[queue].txq_tid = tid;
	}

	if (sta) {
		struct iwl_mvm_txq *mvmtxq =
			iwl_mvm_txq_from_tid(sta, tid);

		mvmtxq->txq_id = queue;
	}

	IWL_DEBUG_TX_QUEUES(mvm,
			    "Enabling TXQ #%d tids=0x%x\n",
			    queue, mvm->queue_info[queue].tid_bitmap);

	return enable_queue;
}

static bool iwl_mvm_enable_txq(struct iwl_mvm *mvm, struct ieee80211_sta *sta,
			       int queue, u16 ssn,
			       const struct iwl_trans_txq_scd_cfg *cfg,
			       unsigned int wdg_timeout)
{
	struct iwl_scd_txq_cfg_cmd cmd = {
		.scd_queue = queue,
		.action = SCD_CFG_ENABLE_QUEUE,
		.window = cfg->frame_limit,
		.sta_id = cfg->sta_id,
		.ssn = cpu_to_le16(ssn),
		.tx_fifo = cfg->fifo,
		.aggregate = cfg->aggregate,
		.tid = cfg->tid,
	};
	bool inc_ssn;

	if (WARN_ON(iwl_mvm_has_new_tx_api(mvm)))
		return false;

	/* Send the enabling command if we need to */
	if (!iwl_mvm_update_txq_mapping(mvm, sta, queue, cfg->sta_id, cfg->tid))
		return false;

	inc_ssn = iwl_trans_txq_enable_cfg(mvm->trans, queue, ssn,
					   NULL, wdg_timeout);
	if (inc_ssn)
		le16_add_cpu(&cmd.ssn, 1);

	WARN(iwl_mvm_send_cmd_pdu(mvm, SCD_QUEUE_CFG, 0, sizeof(cmd), &cmd),
	     "Failed to configure queue %d on FIFO %d\n", queue, cfg->fifo);

	return inc_ssn;
}

static void iwl_mvm_change_queue_tid(struct iwl_mvm *mvm, int queue)
{
	struct iwl_scd_txq_cfg_cmd cmd = {
		.scd_queue = queue,
		.action = SCD_CFG_UPDATE_QUEUE_TID,
	};
	int tid;
	unsigned long tid_bitmap;
	int ret;

	lockdep_assert_held(&mvm->mutex);

	if (WARN_ON(iwl_mvm_has_new_tx_api(mvm)))
		return;

	tid_bitmap = mvm->queue_info[queue].tid_bitmap;

	if (WARN(!tid_bitmap, "TXQ %d has no tids assigned to it\n", queue))
		return;

	/* Find any TID for queue */
	tid = find_first_bit(&tid_bitmap, IWL_MAX_TID_COUNT + 1);
	cmd.tid = tid;
	cmd.tx_fifo = iwl_mvm_ac_to_tx_fifo[tid_to_mac80211_ac[tid]];

	ret = iwl_mvm_send_cmd_pdu(mvm, SCD_QUEUE_CFG, 0, sizeof(cmd), &cmd);
	if (ret) {
		IWL_ERR(mvm, "Failed to update owner of TXQ %d (ret=%d)\n",
			queue, ret);
		return;
	}

	mvm->queue_info[queue].txq_tid = tid;
	IWL_DEBUG_TX_QUEUES(mvm, "Changed TXQ %d ownership to tid %d\n",
			    queue, tid);
}

static void iwl_mvm_unshare_queue(struct iwl_mvm *mvm, int queue)
{
	struct ieee80211_sta *sta;
	struct iwl_mvm_sta *mvmsta;
	u8 sta_id;
	int tid = -1;
	unsigned long tid_bitmap;
	unsigned int wdg_timeout;
	int ssn;
	int ret = true;

	/* queue sharing is disabled on new TX path */
	if (WARN_ON(iwl_mvm_has_new_tx_api(mvm)))
		return;

	lockdep_assert_held(&mvm->mutex);

	sta_id = mvm->queue_info[queue].ra_sta_id;
	tid_bitmap = mvm->queue_info[queue].tid_bitmap;

	/* Find TID for queue, and make sure it is the only one on the queue */
	tid = find_first_bit(&tid_bitmap, IWL_MAX_TID_COUNT + 1);
	if (tid_bitmap != BIT(tid)) {
		IWL_ERR(mvm, "Failed to unshare q %d, active tids=0x%lx\n",
			queue, tid_bitmap);
		return;
	}

	IWL_DEBUG_TX_QUEUES(mvm, "Unsharing TXQ %d, keeping tid %d\n", queue,
			    tid);

	sta = rcu_dereference_protected(mvm->fw_id_to_mac_id[sta_id],
					lockdep_is_held(&mvm->mutex));

	if (WARN_ON_ONCE(IS_ERR_OR_NULL(sta)))
		return;

	mvmsta = iwl_mvm_sta_from_mac80211(sta);
	wdg_timeout = iwl_mvm_get_wd_timeout(mvm, mvmsta->vif, false, false);

	ssn = IEEE80211_SEQ_TO_SN(mvmsta->tid_data[tid].seq_number);

	ret = iwl_mvm_redirect_queue(mvm, queue, tid,
				     tid_to_mac80211_ac[tid], ssn,
				     wdg_timeout, true,
				     iwl_mvm_txq_from_tid(sta, tid));
	if (ret) {
		IWL_ERR(mvm, "Failed to redirect TXQ %d\n", queue);
		return;
	}

	/* If aggs should be turned back on - do it */
	if (mvmsta->tid_data[tid].state == IWL_AGG_ON) {
		struct iwl_mvm_add_sta_cmd cmd = {0};

		mvmsta->tid_disable_agg &= ~BIT(tid);

		cmd.mac_id_n_color = cpu_to_le32(mvmsta->mac_id_n_color);
		cmd.sta_id = mvmsta->sta_id;
		cmd.add_modify = STA_MODE_MODIFY;
		cmd.modify_mask = STA_MODIFY_TID_DISABLE_TX;
		cmd.tfd_queue_msk = cpu_to_le32(mvmsta->tfd_queue_msk);
		cmd.tid_disable_tx = cpu_to_le16(mvmsta->tid_disable_agg);

		ret = iwl_mvm_send_cmd_pdu(mvm, ADD_STA, CMD_ASYNC,
					   iwl_mvm_add_sta_cmd_size(mvm), &cmd);
		if (!ret) {
			IWL_DEBUG_TX_QUEUES(mvm,
					    "TXQ #%d is now aggregated again\n",
					    queue);

			/* Mark queue intenally as aggregating again */
			iwl_trans_txq_set_shared_mode(mvm->trans, queue, false);
		}
	}

	mvm->queue_info[queue].status = IWL_MVM_QUEUE_READY;
}

/*
 * Remove inactive TIDs of a given queue.
 * If all queue TIDs are inactive - mark the queue as inactive
 * If only some the queue TIDs are inactive - unmap them from the queue
 *
 * Returns %true if all TIDs were removed and the queue could be reused.
 */
static bool iwl_mvm_remove_inactive_tids(struct iwl_mvm *mvm,
					 struct iwl_mvm_sta *mvmsta, int queue,
					 unsigned long tid_bitmap,
					 unsigned long *unshare_queues,
					 unsigned long *changetid_queues)
{
	int tid;

	lockdep_assert_held(&mvmsta->lock);
	lockdep_assert_held(&mvm->mutex);

	if (WARN_ON(iwl_mvm_has_new_tx_api(mvm)))
		return false;

	/* Go over all non-active TIDs, incl. IWL_MAX_TID_COUNT (for mgmt) */
	for_each_set_bit(tid, &tid_bitmap, IWL_MAX_TID_COUNT + 1) {
		/* If some TFDs are still queued - don't mark TID as inactive */
		if (iwl_mvm_tid_queued(mvm, &mvmsta->tid_data[tid]))
			tid_bitmap &= ~BIT(tid);

		/* Don't mark as inactive any TID that has an active BA */
		if (mvmsta->tid_data[tid].state != IWL_AGG_OFF)
			tid_bitmap &= ~BIT(tid);
	}

	/* If all TIDs in the queue are inactive - return it can be reused */
	if (tid_bitmap == mvm->queue_info[queue].tid_bitmap) {
		IWL_DEBUG_TX_QUEUES(mvm, "Queue %d is inactive\n", queue);
		return true;
	}

	/*
	 * If we are here, this is a shared queue and not all TIDs timed-out.
	 * Remove the ones that did.
	 */
	for_each_set_bit(tid, &tid_bitmap, IWL_MAX_TID_COUNT + 1) {
		u16 tid_bitmap;

		mvmsta->tid_data[tid].txq_id = IWL_MVM_INVALID_QUEUE;
		mvm->queue_info[queue].tid_bitmap &= ~BIT(tid);

		tid_bitmap = mvm->queue_info[queue].tid_bitmap;

		/*
		 * We need to take into account a situation in which a TXQ was
		 * allocated to TID x, and then turned shared by adding TIDs y
		 * and z. If TID x becomes inactive and is removed from the TXQ,
		 * ownership must be given to one of the remaining TIDs.
		 * This is mainly because if TID x continues - a new queue can't
		 * be allocated for it as long as it is an owner of another TXQ.
		 *
		 * Mark this queue in the right bitmap, we'll send the command
		 * to the firmware later.
		 */
		if (!(tid_bitmap & BIT(mvm->queue_info[queue].txq_tid)))
			set_bit(queue, changetid_queues);

		IWL_DEBUG_TX_QUEUES(mvm,
				    "Removing inactive TID %d from shared Q:%d\n",
				    tid, queue);
	}

	IWL_DEBUG_TX_QUEUES(mvm,
			    "TXQ #%d left with tid bitmap 0x%x\n", queue,
			    mvm->queue_info[queue].tid_bitmap);

	/*
	 * There may be different TIDs with the same mac queues, so make
	 * sure all TIDs have existing corresponding mac queues enabled
	 */
	tid_bitmap = mvm->queue_info[queue].tid_bitmap;

	/* If the queue is marked as shared - "unshare" it */
	if (hweight16(mvm->queue_info[queue].tid_bitmap) == 1 &&
	    mvm->queue_info[queue].status == IWL_MVM_QUEUE_SHARED) {
		IWL_DEBUG_TX_QUEUES(mvm, "Marking Q:%d for reconfig\n",
				    queue);
		set_bit(queue, unshare_queues);
	}

	return false;
}

/*
 * Check for inactivity - this includes checking if any queue
 * can be unshared and finding one (and only one) that can be
 * reused.
 * This function is also invoked as a sort of clean-up task,
 * in which case @alloc_for_sta is IWL_MVM_INVALID_STA.
 *
 * Returns the queue number, or -ENOSPC.
 */
static int iwl_mvm_inactivity_check(struct iwl_mvm *mvm, u8 alloc_for_sta)
{
	unsigned long now = jiffies;
	unsigned long unshare_queues = 0;
	unsigned long changetid_queues = 0;
	int i, ret, free_queue = -ENOSPC;
	struct ieee80211_sta *queue_owner  = NULL;

	lockdep_assert_held(&mvm->mutex);

	if (iwl_mvm_has_new_tx_api(mvm))
		return -ENOSPC;

	rcu_read_lock();

	/* we skip the CMD queue below by starting at 1 */
	BUILD_BUG_ON(IWL_MVM_DQA_CMD_QUEUE != 0);

	for (i = 1; i < IWL_MAX_HW_QUEUES; i++) {
		struct ieee80211_sta *sta;
		struct iwl_mvm_sta *mvmsta;
		u8 sta_id;
		int tid;
		unsigned long inactive_tid_bitmap = 0;
		unsigned long queue_tid_bitmap;

		queue_tid_bitmap = mvm->queue_info[i].tid_bitmap;
		if (!queue_tid_bitmap)
			continue;

		/* If TXQ isn't in active use anyway - nothing to do here... */
		if (mvm->queue_info[i].status != IWL_MVM_QUEUE_READY &&
		    mvm->queue_info[i].status != IWL_MVM_QUEUE_SHARED)
			continue;

		/* Check to see if there are inactive TIDs on this queue */
		for_each_set_bit(tid, &queue_tid_bitmap,
				 IWL_MAX_TID_COUNT + 1) {
			if (time_after(mvm->queue_info[i].last_frame_time[tid] +
				       IWL_MVM_DQA_QUEUE_TIMEOUT, now))
				continue;

			inactive_tid_bitmap |= BIT(tid);
		}

		/* If all TIDs are active - finish check on this queue */
		if (!inactive_tid_bitmap)
			continue;

		/*
		 * If we are here - the queue hadn't been served recently and is
		 * in use
		 */

		sta_id = mvm->queue_info[i].ra_sta_id;
		sta = rcu_dereference(mvm->fw_id_to_mac_id[sta_id]);

		/*
		 * If the STA doesn't exist anymore, it isn't an error. It could
		 * be that it was removed since getting the queues, and in this
		 * case it should've inactivated its queues anyway.
		 */
		if (IS_ERR_OR_NULL(sta))
			continue;

		mvmsta = iwl_mvm_sta_from_mac80211(sta);

		spin_lock_bh(&mvmsta->lock);
		ret = iwl_mvm_remove_inactive_tids(mvm, mvmsta, i,
						   inactive_tid_bitmap,
						   &unshare_queues,
						   &changetid_queues);
		if (ret && free_queue < 0) {
			queue_owner = sta;
			free_queue = i;
		}
		/* only unlock sta lock - we still need the queue info lock */
		spin_unlock_bh(&mvmsta->lock);
	}


	/* Reconfigure queues requiring reconfiguation */
	for_each_set_bit(i, &unshare_queues, IWL_MAX_HW_QUEUES)
		iwl_mvm_unshare_queue(mvm, i);
	for_each_set_bit(i, &changetid_queues, IWL_MAX_HW_QUEUES)
		iwl_mvm_change_queue_tid(mvm, i);

	rcu_read_unlock();

	if (free_queue >= 0 && alloc_for_sta != IWL_MVM_INVALID_STA) {
		ret = iwl_mvm_free_inactive_queue(mvm, free_queue, queue_owner,
						  alloc_for_sta);
		if (ret)
			return ret;
	}

	return free_queue;
}

static int iwl_mvm_sta_alloc_queue(struct iwl_mvm *mvm,
				   struct ieee80211_sta *sta, u8 ac, int tid)
{
	struct iwl_mvm_sta *mvmsta = iwl_mvm_sta_from_mac80211(sta);
	struct iwl_trans_txq_scd_cfg cfg = {
		.fifo = iwl_mvm_mac_ac_to_tx_fifo(mvm, ac),
		.sta_id = mvmsta->sta_id,
		.tid = tid,
		.frame_limit = IWL_FRAME_LIMIT,
	};
	unsigned int wdg_timeout =
		iwl_mvm_get_wd_timeout(mvm, mvmsta->vif, false, false);
	int queue = -1;
	u16 queue_tmp;
	unsigned long disable_agg_tids = 0;
	enum iwl_mvm_agg_state queue_state;
	bool shared_queue = false, inc_ssn;
	int ssn;
	unsigned long tfd_queue_mask;
	int ret;

	lockdep_assert_held(&mvm->mutex);

	if (iwl_mvm_has_new_tx_api(mvm))
		return iwl_mvm_sta_alloc_queue_tvqm(mvm, sta, ac, tid);

	spin_lock_bh(&mvmsta->lock);
	tfd_queue_mask = mvmsta->tfd_queue_msk;
	ssn = IEEE80211_SEQ_TO_SN(mvmsta->tid_data[tid].seq_number);
	spin_unlock_bh(&mvmsta->lock);

	if (tid == IWL_MAX_TID_COUNT) {
		queue = iwl_mvm_find_free_queue(mvm, mvmsta->sta_id,
						IWL_MVM_DQA_MIN_MGMT_QUEUE,
						IWL_MVM_DQA_MAX_MGMT_QUEUE);
		if (queue >= IWL_MVM_DQA_MIN_MGMT_QUEUE)
			IWL_DEBUG_TX_QUEUES(mvm, "Found free MGMT queue #%d\n",
					    queue);

		/* If no such queue is found, we'll use a DATA queue instead */
	}

	if ((queue < 0 && mvmsta->reserved_queue != IEEE80211_INVAL_HW_QUEUE) &&
	    (mvm->queue_info[mvmsta->reserved_queue].status ==
			IWL_MVM_QUEUE_RESERVED)) {
		queue = mvmsta->reserved_queue;
		mvm->queue_info[queue].reserved = true;
		IWL_DEBUG_TX_QUEUES(mvm, "Using reserved queue #%d\n", queue);
	}

	if (queue < 0)
		queue = iwl_mvm_find_free_queue(mvm, mvmsta->sta_id,
						IWL_MVM_DQA_MIN_DATA_QUEUE,
						IWL_MVM_DQA_MAX_DATA_QUEUE);
	if (queue < 0) {
		/* try harder - perhaps kill an inactive queue */
		queue = iwl_mvm_inactivity_check(mvm, mvmsta->sta_id);
	}

	/* No free queue - we'll have to share */
	if (queue <= 0) {
		queue = iwl_mvm_get_shared_queue(mvm, tfd_queue_mask, ac);
		if (queue > 0) {
			shared_queue = true;
			mvm->queue_info[queue].status = IWL_MVM_QUEUE_SHARED;
		}
	}

	/*
	 * Mark TXQ as ready, even though it hasn't been fully configured yet,
	 * to make sure no one else takes it.
	 * This will allow avoiding re-acquiring the lock at the end of the
	 * configuration. On error we'll mark it back as free.
	 */
	if (queue > 0 && !shared_queue)
		mvm->queue_info[queue].status = IWL_MVM_QUEUE_READY;

	/* This shouldn't happen - out of queues */
	if (WARN_ON(queue <= 0)) {
		IWL_ERR(mvm, "No available queues for tid %d on sta_id %d\n",
			tid, cfg.sta_id);
		return queue;
	}

	/*
	 * Actual en/disablement of aggregations is through the ADD_STA HCMD,
	 * but for configuring the SCD to send A-MPDUs we need to mark the queue
	 * as aggregatable.
	 * Mark all DATA queues as allowing to be aggregated at some point
	 */
	cfg.aggregate = (queue >= IWL_MVM_DQA_MIN_DATA_QUEUE ||
			 queue == IWL_MVM_DQA_BSS_CLIENT_QUEUE);

	IWL_DEBUG_TX_QUEUES(mvm,
			    "Allocating %squeue #%d to sta %d on tid %d\n",
			    shared_queue ? "shared " : "", queue,
			    mvmsta->sta_id, tid);

	if (shared_queue) {
		/* Disable any open aggs on this queue */
		disable_agg_tids = iwl_mvm_get_queue_agg_tids(mvm, queue);

		if (disable_agg_tids) {
			IWL_DEBUG_TX_QUEUES(mvm, "Disabling aggs on queue %d\n",
					    queue);
			iwl_mvm_invalidate_sta_queue(mvm, queue,
						     disable_agg_tids, false);
		}
	}

	inc_ssn = iwl_mvm_enable_txq(mvm, sta, queue, ssn, &cfg, wdg_timeout);

	/*
	 * Mark queue as shared in transport if shared
	 * Note this has to be done after queue enablement because enablement
	 * can also set this value, and there is no indication there to shared
	 * queues
	 */
	if (shared_queue)
		iwl_trans_txq_set_shared_mode(mvm->trans, queue, true);

	spin_lock_bh(&mvmsta->lock);
	/*
	 * This looks racy, but it is not. We have only one packet for
	 * this ra/tid in our Tx path since we stop the Qdisc when we
	 * need to allocate a new TFD queue.
	 */
	if (inc_ssn) {
		mvmsta->tid_data[tid].seq_number += 0x10;
		ssn = (ssn + 1) & IEEE80211_SCTL_SEQ;
	}
	mvmsta->tid_data[tid].txq_id = queue;
	mvmsta->tfd_queue_msk |= BIT(queue);
	queue_state = mvmsta->tid_data[tid].state;

	if (mvmsta->reserved_queue == queue)
		mvmsta->reserved_queue = IEEE80211_INVAL_HW_QUEUE;
	spin_unlock_bh(&mvmsta->lock);

	if (!shared_queue) {
		ret = iwl_mvm_sta_send_to_fw(mvm, sta, true, STA_MODIFY_QUEUES);
		if (ret)
			goto out_err;

		/* If we need to re-enable aggregations... */
		if (queue_state == IWL_AGG_ON) {
			ret = iwl_mvm_sta_tx_agg(mvm, sta, tid, queue, true);
			if (ret)
				goto out_err;
		}
	} else {
		/* Redirect queue, if needed */
		ret = iwl_mvm_redirect_queue(mvm, queue, tid, ac, ssn,
					     wdg_timeout, false,
					     iwl_mvm_txq_from_tid(sta, tid));
		if (ret)
			goto out_err;
	}

	return 0;

out_err:
	queue_tmp = queue;
	iwl_mvm_disable_txq(mvm, sta, &queue_tmp, tid, 0);

	return ret;
}

void iwl_mvm_add_new_dqa_stream_wk(struct work_struct *wk)
{
	struct iwl_mvm *mvm = container_of(wk, struct iwl_mvm,
					   add_stream_wk);

	mutex_lock(&mvm->mutex);

	iwl_mvm_inactivity_check(mvm, IWL_MVM_INVALID_STA);

	while (!list_empty(&mvm->add_stream_txqs)) {
		struct iwl_mvm_txq *mvmtxq;
		struct ieee80211_txq *txq;
		u8 tid;

		mvmtxq = list_first_entry(&mvm->add_stream_txqs,
					  struct iwl_mvm_txq, list);

		txq = container_of((void *)mvmtxq, struct ieee80211_txq,
				   drv_priv);
		tid = txq->tid;
		if (tid == IEEE80211_NUM_TIDS)
			tid = IWL_MAX_TID_COUNT;

		/*
		 * We can't really do much here, but if this fails we can't
		 * transmit anyway - so just don't transmit the frame etc.
		 * and let them back up ... we've tried our best to allocate
		 * a queue in the function itself.
		 */
		if (iwl_mvm_sta_alloc_queue(mvm, txq->sta, txq->ac, tid)) {
			list_del_init(&mvmtxq->list);
			continue;
		}

		list_del_init(&mvmtxq->list);
		local_bh_disable();
		iwl_mvm_mac_itxq_xmit(mvm->hw, txq);
		local_bh_enable();
	}

	mutex_unlock(&mvm->mutex);
}

static int iwl_mvm_reserve_sta_stream(struct iwl_mvm *mvm,
				      struct ieee80211_sta *sta,
				      enum nl80211_iftype vif_type)
{
	struct iwl_mvm_sta *mvmsta = iwl_mvm_sta_from_mac80211(sta);
	int queue;

	/* queue reserving is disabled on new TX path */
	if (WARN_ON(iwl_mvm_has_new_tx_api(mvm)))
		return 0;

	/* run the general cleanup/unsharing of queues */
	iwl_mvm_inactivity_check(mvm, IWL_MVM_INVALID_STA);

	/* Make sure we have free resources for this STA */
	if (vif_type == NL80211_IFTYPE_STATION && !sta->tdls &&
	    !mvm->queue_info[IWL_MVM_DQA_BSS_CLIENT_QUEUE].tid_bitmap &&
	    (mvm->queue_info[IWL_MVM_DQA_BSS_CLIENT_QUEUE].status ==
	     IWL_MVM_QUEUE_FREE))
		queue = IWL_MVM_DQA_BSS_CLIENT_QUEUE;
	else
		queue = iwl_mvm_find_free_queue(mvm, mvmsta->sta_id,
						IWL_MVM_DQA_MIN_DATA_QUEUE,
						IWL_MVM_DQA_MAX_DATA_QUEUE);
	if (queue < 0) {
		/* try again - this time kick out a queue if needed */
		queue = iwl_mvm_inactivity_check(mvm, mvmsta->sta_id);
		if (queue < 0) {
			IWL_ERR(mvm, "No available queues for new station\n");
			return -ENOSPC;
		}
	}
	mvm->queue_info[queue].status = IWL_MVM_QUEUE_RESERVED;

	mvmsta->reserved_queue = queue;

	IWL_DEBUG_TX_QUEUES(mvm, "Reserving data queue #%d for sta_id %d\n",
			    queue, mvmsta->sta_id);

	return 0;
}

/*
 * In DQA mode, after a HW restart the queues should be allocated as before, in
 * order to avoid race conditions when there are shared queues. This function
 * does the re-mapping and queue allocation.
 *
 * Note that re-enabling aggregations isn't done in this function.
 */
static void iwl_mvm_realloc_queues_after_restart(struct iwl_mvm *mvm,
						 struct ieee80211_sta *sta)
{
	struct iwl_mvm_sta *mvm_sta = iwl_mvm_sta_from_mac80211(sta);
	unsigned int wdg =
		iwl_mvm_get_wd_timeout(mvm, mvm_sta->vif, false, false);
	int i;
	struct iwl_trans_txq_scd_cfg cfg = {
		.sta_id = mvm_sta->sta_id,
		.frame_limit = IWL_FRAME_LIMIT,
	};

	/* Make sure reserved queue is still marked as such (if allocated) */
	if (mvm_sta->reserved_queue != IEEE80211_INVAL_HW_QUEUE)
		mvm->queue_info[mvm_sta->reserved_queue].status =
			IWL_MVM_QUEUE_RESERVED;

	for (i = 0; i <= IWL_MAX_TID_COUNT; i++) {
		struct iwl_mvm_tid_data *tid_data = &mvm_sta->tid_data[i];
		int txq_id = tid_data->txq_id;
		int ac;

		if (txq_id == IWL_MVM_INVALID_QUEUE)
			continue;

		ac = tid_to_mac80211_ac[i];

		if (iwl_mvm_has_new_tx_api(mvm)) {
			IWL_DEBUG_TX_QUEUES(mvm,
					    "Re-mapping sta %d tid %d\n",
					    mvm_sta->sta_id, i);
			txq_id = iwl_mvm_tvqm_enable_txq(mvm, mvm_sta->sta_id,
							 i, wdg);
			/*
			 * on failures, just set it to IWL_MVM_INVALID_QUEUE
			 * to try again later, we have no other good way of
			 * failing here
			 */
			if (txq_id < 0)
				txq_id = IWL_MVM_INVALID_QUEUE;
			tid_data->txq_id = txq_id;

			/*
			 * Since we don't set the seq number after reset, and HW
			 * sets it now, FW reset will cause the seq num to start
			 * at 0 again, so driver will need to update it
			 * internally as well, so it keeps in sync with real val
			 */
			tid_data->seq_number = 0;
		} else {
			u16 seq = IEEE80211_SEQ_TO_SN(tid_data->seq_number);

			cfg.tid = i;
			cfg.fifo = iwl_mvm_mac_ac_to_tx_fifo(mvm, ac);
			cfg.aggregate = (txq_id >= IWL_MVM_DQA_MIN_DATA_QUEUE ||
					 txq_id ==
					 IWL_MVM_DQA_BSS_CLIENT_QUEUE);

			IWL_DEBUG_TX_QUEUES(mvm,
					    "Re-mapping sta %d tid %d to queue %d\n",
					    mvm_sta->sta_id, i, txq_id);

			iwl_mvm_enable_txq(mvm, sta, txq_id, seq, &cfg, wdg);
			mvm->queue_info[txq_id].status = IWL_MVM_QUEUE_READY;
		}
	}
}

static int iwl_mvm_add_int_sta_common(struct iwl_mvm *mvm,
				      struct iwl_mvm_int_sta *sta,
				      const u8 *addr,
				      u16 mac_id, u16 color)
{
	struct iwl_mvm_add_sta_cmd cmd;
	int ret;
	u32 status = ADD_STA_SUCCESS;

	lockdep_assert_held(&mvm->mutex);

	memset(&cmd, 0, sizeof(cmd));
	cmd.sta_id = sta->sta_id;

	if (iwl_fw_lookup_cmd_ver(mvm->fw, ADD_STA, 0) >= 12 &&
	    sta->type == IWL_STA_AUX_ACTIVITY)
		cmd.mac_id_n_color = cpu_to_le32(mac_id);
	else
		cmd.mac_id_n_color = cpu_to_le32(FW_CMD_ID_AND_COLOR(mac_id,
								     color));

	if (fw_has_api(&mvm->fw->ucode_capa, IWL_UCODE_TLV_API_STA_TYPE))
		cmd.station_type = sta->type;

	if (!iwl_mvm_has_new_tx_api(mvm))
		cmd.tfd_queue_msk = cpu_to_le32(sta->tfd_queue_msk);
	cmd.tid_disable_tx = cpu_to_le16(0xffff);

	if (addr)
		memcpy(cmd.addr, addr, ETH_ALEN);

	ret = iwl_mvm_send_cmd_pdu_status(mvm, ADD_STA,
					  iwl_mvm_add_sta_cmd_size(mvm),
					  &cmd, &status);
	if (ret)
		return ret;

	switch (status & IWL_ADD_STA_STATUS_MASK) {
	case ADD_STA_SUCCESS:
		IWL_DEBUG_INFO(mvm, "Internal station added.\n");
		return 0;
	default:
		ret = -EIO;
		IWL_ERR(mvm, "Add internal station failed, status=0x%x\n",
			status);
		break;
	}
	return ret;
}

int iwl_mvm_add_sta(struct iwl_mvm *mvm,
		    struct ieee80211_vif *vif,
		    struct ieee80211_sta *sta)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm_sta *mvm_sta = iwl_mvm_sta_from_mac80211(sta);
	struct iwl_mvm_rxq_dup_data *dup_data;
	int i, ret, sta_id;
	bool sta_update = false;
	unsigned int sta_flags = 0;

	lockdep_assert_held(&mvm->mutex);

	if (!test_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status))
		sta_id = iwl_mvm_find_free_sta_id(mvm,
						  ieee80211_vif_type_p2p(vif));
	else
		sta_id = mvm_sta->sta_id;

	if (sta_id == IWL_MVM_INVALID_STA)
		return -ENOSPC;

	spin_lock_init(&mvm_sta->lock);

	/* if this is a HW restart re-alloc existing queues */
	if (test_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status)) {
		struct iwl_mvm_int_sta tmp_sta = {
			.sta_id = sta_id,
			.type = mvm_sta->sta_type,
		};

		/*
		 * First add an empty station since allocating
		 * a queue requires a valid station
		 */
		ret = iwl_mvm_add_int_sta_common(mvm, &tmp_sta, sta->addr,
						 mvmvif->id, mvmvif->color);
		if (ret)
			goto err;

		iwl_mvm_realloc_queues_after_restart(mvm, sta);
		sta_update = true;
		sta_flags = iwl_mvm_has_new_tx_api(mvm) ? 0 : STA_MODIFY_QUEUES;
		goto update_fw;
	}

	mvm_sta->sta_id = sta_id;
	mvm_sta->mac_id_n_color = FW_CMD_ID_AND_COLOR(mvmvif->id,
						      mvmvif->color);
	mvm_sta->vif = vif;
	if (!mvm->trans->trans_cfg->gen2)
		mvm_sta->max_agg_bufsize = LINK_QUAL_AGG_FRAME_LIMIT_DEF;
	else
		mvm_sta->max_agg_bufsize = LINK_QUAL_AGG_FRAME_LIMIT_GEN2_DEF;
	mvm_sta->tx_protection = 0;
	mvm_sta->tt_tx_protection = false;
	mvm_sta->sta_type = sta->tdls ? IWL_STA_TDLS_LINK : IWL_STA_LINK;

	/* HW restart, don't assume the memory has been zeroed */
	mvm_sta->tid_disable_agg = 0xffff; /* No aggs at first */
	mvm_sta->tfd_queue_msk = 0;

	/* for HW restart - reset everything but the sequence number */
	for (i = 0; i <= IWL_MAX_TID_COUNT; i++) {
		u16 seq = mvm_sta->tid_data[i].seq_number;
		memset(&mvm_sta->tid_data[i], 0, sizeof(mvm_sta->tid_data[i]));
		mvm_sta->tid_data[i].seq_number = seq;

		/*
		 * Mark all queues for this STA as unallocated and defer TX
		 * frames until the queue is allocated
		 */
		mvm_sta->tid_data[i].txq_id = IWL_MVM_INVALID_QUEUE;
	}

	for (i = 0; i < ARRAY_SIZE(sta->txq); i++) {
		struct iwl_mvm_txq *mvmtxq =
			iwl_mvm_txq_from_mac80211(sta->txq[i]);

		mvmtxq->txq_id = IWL_MVM_INVALID_QUEUE;
		INIT_LIST_HEAD(&mvmtxq->list);
		atomic_set(&mvmtxq->tx_request, 0);
	}

	mvm_sta->agg_tids = 0;

	if (iwl_mvm_has_new_rx_api(mvm) &&
	    !test_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status)) {
		int q;

		dup_data = kcalloc(mvm->trans->num_rx_queues,
				   sizeof(*dup_data), GFP_KERNEL);
		if (!dup_data)
			return -ENOMEM;
		/*
		 * Initialize all the last_seq values to 0xffff which can never
		 * compare equal to the frame's seq_ctrl in the check in
		 * iwl_mvm_is_dup() since the lower 4 bits are the fragment
		 * number and fragmented packets don't reach that function.
		 *
		 * This thus allows receiving a packet with seqno 0 and the
		 * retry bit set as the very first packet on a new TID.
		 */
		for (q = 0; q < mvm->trans->num_rx_queues; q++)
			memset(dup_data[q].last_seq, 0xff,
			       sizeof(dup_data[q].last_seq));
		mvm_sta->dup_data = dup_data;
	}

	if (!iwl_mvm_has_new_tx_api(mvm)) {
		ret = iwl_mvm_reserve_sta_stream(mvm, sta,
						 ieee80211_vif_type_p2p(vif));
		if (ret)
			goto err;
	}

	/*
	 * if rs is registered with mac80211, then "add station" will be handled
	 * via the corresponding ops, otherwise need to notify rate scaling here
	 */
	if (iwl_mvm_has_tlc_offload(mvm))
		iwl_mvm_rs_add_sta(mvm, mvm_sta);
	else
		spin_lock_init(&mvm_sta->lq_sta.rs_drv.pers.lock);

	iwl_mvm_toggle_tx_ant(mvm, &mvm_sta->tx_ant);

update_fw:
	ret = iwl_mvm_sta_send_to_fw(mvm, sta, sta_update, sta_flags);
	if (ret)
		goto err;

	if (vif->type == NL80211_IFTYPE_STATION) {
		if (!sta->tdls) {
			WARN_ON(mvmvif->ap_sta_id != IWL_MVM_INVALID_STA);
			mvmvif->ap_sta_id = sta_id;
		} else {
			WARN_ON(mvmvif->ap_sta_id == IWL_MVM_INVALID_STA);
		}
	}

	rcu_assign_pointer(mvm->fw_id_to_mac_id[sta_id], sta);

	return 0;

err:
	return ret;
}

int iwl_mvm_drain_sta(struct iwl_mvm *mvm, struct iwl_mvm_sta *mvmsta,
		      bool drain)
{
	struct iwl_mvm_add_sta_cmd cmd = {};
	int ret;
	u32 status;

	lockdep_assert_held(&mvm->mutex);

	cmd.mac_id_n_color = cpu_to_le32(mvmsta->mac_id_n_color);
	cmd.sta_id = mvmsta->sta_id;
	cmd.add_modify = STA_MODE_MODIFY;
	cmd.station_flags = drain ? cpu_to_le32(STA_FLG_DRAIN_FLOW) : 0;
	cmd.station_flags_msk = cpu_to_le32(STA_FLG_DRAIN_FLOW);

	status = ADD_STA_SUCCESS;
	ret = iwl_mvm_send_cmd_pdu_status(mvm, ADD_STA,
					  iwl_mvm_add_sta_cmd_size(mvm),
					  &cmd, &status);
	if (ret)
		return ret;

	switch (status & IWL_ADD_STA_STATUS_MASK) {
	case ADD_STA_SUCCESS:
		IWL_DEBUG_INFO(mvm, "Frames for staid %d will drained in fw\n",
			       mvmsta->sta_id);
		break;
	default:
		ret = -EIO;
		IWL_ERR(mvm, "Couldn't drain frames for staid %d\n",
			mvmsta->sta_id);
		break;
	}

	return ret;
}

/*
 * Remove a station from the FW table. Before sending the command to remove
 * the station validate that the station is indeed known to the driver (sanity
 * only).
 */
static int iwl_mvm_rm_sta_common(struct iwl_mvm *mvm, u8 sta_id)
{
	struct ieee80211_sta *sta;
	struct iwl_mvm_rm_sta_cmd rm_sta_cmd = {
		.sta_id = sta_id,
	};
	int ret;

	sta = rcu_dereference_protected(mvm->fw_id_to_mac_id[sta_id],
					lockdep_is_held(&mvm->mutex));

	/* Note: internal stations are marked as error values */
	if (!sta) {
		IWL_ERR(mvm, "Invalid station id\n");
		return -EINVAL;
	}

	ret = iwl_mvm_send_cmd_pdu(mvm, REMOVE_STA, 0,
				   sizeof(rm_sta_cmd), &rm_sta_cmd);
	if (ret) {
		IWL_ERR(mvm, "Failed to remove station. Id=%d\n", sta_id);
		return ret;
	}

	return 0;
}

static void iwl_mvm_disable_sta_queues(struct iwl_mvm *mvm,
				       struct ieee80211_vif *vif,
				       struct ieee80211_sta *sta)
{
	struct iwl_mvm_sta *mvm_sta = iwl_mvm_sta_from_mac80211(sta);
	int i;

	lockdep_assert_held(&mvm->mutex);

	for (i = 0; i < ARRAY_SIZE(mvm_sta->tid_data); i++) {
		if (mvm_sta->tid_data[i].txq_id == IWL_MVM_INVALID_QUEUE)
			continue;

		iwl_mvm_disable_txq(mvm, sta, &mvm_sta->tid_data[i].txq_id, i,
				    0);
		mvm_sta->tid_data[i].txq_id = IWL_MVM_INVALID_QUEUE;
	}

	for (i = 0; i < ARRAY_SIZE(sta->txq); i++) {
		struct iwl_mvm_txq *mvmtxq =
			iwl_mvm_txq_from_mac80211(sta->txq[i]);

		mvmtxq->txq_id = IWL_MVM_INVALID_QUEUE;
	}
}

int iwl_mvm_wait_sta_queues_empty(struct iwl_mvm *mvm,
				  struct iwl_mvm_sta *mvm_sta)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mvm_sta->tid_data); i++) {
		u16 txq_id;
		int ret;

		spin_lock_bh(&mvm_sta->lock);
		txq_id = mvm_sta->tid_data[i].txq_id;
		spin_unlock_bh(&mvm_sta->lock);

		if (txq_id == IWL_MVM_INVALID_QUEUE)
			continue;

		ret = iwl_trans_wait_txq_empty(mvm->trans, txq_id);
		if (ret)
			return ret;
	}

	return 0;
}

int iwl_mvm_rm_sta(struct iwl_mvm *mvm,
		   struct ieee80211_vif *vif,
		   struct ieee80211_sta *sta)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm_sta *mvm_sta = iwl_mvm_sta_from_mac80211(sta);
	u8 sta_id = mvm_sta->sta_id;
	int ret;

	lockdep_assert_held(&mvm->mutex);

	if (iwl_mvm_has_new_rx_api(mvm))
		kfree(mvm_sta->dup_data);

	ret = iwl_mvm_drain_sta(mvm, mvm_sta, true);
	if (ret)
		return ret;

	/* flush its queues here since we are freeing mvm_sta */
	ret = iwl_mvm_flush_sta(mvm, mvm_sta, false);
	if (ret)
		return ret;
	if (iwl_mvm_has_new_tx_api(mvm)) {
		ret = iwl_mvm_wait_sta_queues_empty(mvm, mvm_sta);
	} else {
		u32 q_mask = mvm_sta->tfd_queue_msk;

		ret = iwl_trans_wait_tx_queues_empty(mvm->trans,
						     q_mask);
	}
	if (ret)
		return ret;

	ret = iwl_mvm_drain_sta(mvm, mvm_sta, false);

	iwl_mvm_disable_sta_queues(mvm, vif, sta);

	/* If there is a TXQ still marked as reserved - free it */
	if (mvm_sta->reserved_queue != IEEE80211_INVAL_HW_QUEUE) {
		u8 reserved_txq = mvm_sta->reserved_queue;
		enum iwl_mvm_queue_status *status;

		/*
		 * If no traffic has gone through the reserved TXQ - it
		 * is still marked as IWL_MVM_QUEUE_RESERVED, and
		 * should be manually marked as free again
		 */
		status = &mvm->queue_info[reserved_txq].status;
		if (WARN((*status != IWL_MVM_QUEUE_RESERVED) &&
			 (*status != IWL_MVM_QUEUE_FREE),
			 "sta_id %d reserved txq %d status %d",
			 sta_id, reserved_txq, *status))
			return -EINVAL;

		*status = IWL_MVM_QUEUE_FREE;
	}

	if (vif->type == NL80211_IFTYPE_STATION &&
	    mvmvif->ap_sta_id == sta_id) {
		/* if associated - we can't remove the AP STA now */
		if (vif->bss_conf.assoc)
			return ret;

		/* unassoc - go ahead - remove the AP STA now */
		mvmvif->ap_sta_id = IWL_MVM_INVALID_STA;
	}

	/*
	 * This shouldn't happen - the TDLS channel switch should be canceled
	 * before the STA is removed.
	 */
	if (WARN_ON_ONCE(mvm->tdls_cs.peer.sta_id == sta_id)) {
		mvm->tdls_cs.peer.sta_id = IWL_MVM_INVALID_STA;
		cancel_delayed_work(&mvm->tdls_cs.dwork);
	}

	/*
	 * Make sure that the tx response code sees the station as -EBUSY and
	 * calls the drain worker.
	 */
	spin_lock_bh(&mvm_sta->lock);
	spin_unlock_bh(&mvm_sta->lock);

	ret = iwl_mvm_rm_sta_common(mvm, mvm_sta->sta_id);
	RCU_INIT_POINTER(mvm->fw_id_to_mac_id[mvm_sta->sta_id], NULL);

	return ret;
}

int iwl_mvm_rm_sta_id(struct iwl_mvm *mvm,
		      struct ieee80211_vif *vif,
		      u8 sta_id)
{
	int ret = iwl_mvm_rm_sta_common(mvm, sta_id);

	lockdep_assert_held(&mvm->mutex);

	RCU_INIT_POINTER(mvm->fw_id_to_mac_id[sta_id], NULL);
	return ret;
}

int iwl_mvm_allocate_int_sta(struct iwl_mvm *mvm,
			     struct iwl_mvm_int_sta *sta,
			     u32 qmask, enum nl80211_iftype iftype,
			     enum iwl_sta_type type)
{
	if (!test_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status) ||
	    sta->sta_id == IWL_MVM_INVALID_STA) {
		sta->sta_id = iwl_mvm_find_free_sta_id(mvm, iftype);
		if (WARN_ON_ONCE(sta->sta_id == IWL_MVM_INVALID_STA))
			return -ENOSPC;
	}

	sta->tfd_queue_msk = qmask;
	sta->type = type;

	/* put a non-NULL value so iterating over the stations won't stop */
	rcu_assign_pointer(mvm->fw_id_to_mac_id[sta->sta_id], ERR_PTR(-EINVAL));
	return 0;
}

void iwl_mvm_dealloc_int_sta(struct iwl_mvm *mvm, struct iwl_mvm_int_sta *sta)
{
	RCU_INIT_POINTER(mvm->fw_id_to_mac_id[sta->sta_id], NULL);
	memset(sta, 0, sizeof(struct iwl_mvm_int_sta));
	sta->sta_id = IWL_MVM_INVALID_STA;
}

static void iwl_mvm_enable_aux_snif_queue(struct iwl_mvm *mvm, u16 queue,
					  u8 sta_id, u8 fifo)
{
	unsigned int wdg_timeout =
		mvm->trans->trans_cfg->base_params->wd_timeout;
	struct iwl_trans_txq_scd_cfg cfg = {
		.fifo = fifo,
		.sta_id = sta_id,
		.tid = IWL_MAX_TID_COUNT,
		.aggregate = false,
		.frame_limit = IWL_FRAME_LIMIT,
	};

	WARN_ON(iwl_mvm_has_new_tx_api(mvm));

	iwl_mvm_enable_txq(mvm, NULL, queue, 0, &cfg, wdg_timeout);
}

static int iwl_mvm_enable_aux_snif_queue_tvqm(struct iwl_mvm *mvm, u8 sta_id)
{
	unsigned int wdg_timeout =
		mvm->trans->trans_cfg->base_params->wd_timeout;

	WARN_ON(!iwl_mvm_has_new_tx_api(mvm));

	return iwl_mvm_tvqm_enable_txq(mvm, sta_id, IWL_MAX_TID_COUNT,
				       wdg_timeout);
}

static int iwl_mvm_add_int_sta_with_queue(struct iwl_mvm *mvm, int macidx,
					  int maccolor, u8 *addr,
					  struct iwl_mvm_int_sta *sta,
					  u16 *queue, int fifo)
{
	int ret;

	/* Map queue to fifo - needs to happen before adding station */
	if (!iwl_mvm_has_new_tx_api(mvm))
		iwl_mvm_enable_aux_snif_queue(mvm, *queue, sta->sta_id, fifo);

	ret = iwl_mvm_add_int_sta_common(mvm, sta, addr, macidx, maccolor);
	if (ret) {
		if (!iwl_mvm_has_new_tx_api(mvm))
			iwl_mvm_disable_txq(mvm, NULL, queue,
					    IWL_MAX_TID_COUNT, 0);
		return ret;
	}

	/*
	 * For 22000 firmware and on we cannot add queue to a station unknown
	 * to firmware so enable queue here - after the station was added
	 */
	if (iwl_mvm_has_new_tx_api(mvm)) {
		int txq;

		txq = iwl_mvm_enable_aux_snif_queue_tvqm(mvm, sta->sta_id);
		if (txq < 0) {
			iwl_mvm_rm_sta_common(mvm, sta->sta_id);
			return txq;
		}

		*queue = txq;
	}

	return 0;
}

int iwl_mvm_add_aux_sta(struct iwl_mvm *mvm, u32 lmac_id)
{
	int ret;

	lockdep_assert_held(&mvm->mutex);

	/* Allocate aux station and assign to it the aux queue */
	ret = iwl_mvm_allocate_int_sta(mvm, &mvm->aux_sta, BIT(mvm->aux_queue),
				       NL80211_IFTYPE_UNSPECIFIED,
				       IWL_STA_AUX_ACTIVITY);
	if (ret)
		return ret;

	/*
	 * In CDB NICs we need to specify which lmac to use for aux activity
	 * using the mac_id argument place to send lmac_id to the function
	 */
	ret = iwl_mvm_add_int_sta_with_queue(mvm, lmac_id, 0, NULL,
					     &mvm->aux_sta, &mvm->aux_queue,
					     IWL_MVM_TX_FIFO_MCAST);
	if (ret) {
		iwl_mvm_dealloc_int_sta(mvm, &mvm->aux_sta);
		return ret;
	}

	return 0;
}

int iwl_mvm_add_snif_sta(struct iwl_mvm *mvm, struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);

	lockdep_assert_held(&mvm->mutex);

	return iwl_mvm_add_int_sta_with_queue(mvm, mvmvif->id, mvmvif->color,
					      NULL, &mvm->snif_sta,
					      &mvm->snif_queue,
					      IWL_MVM_TX_FIFO_BE);
}

int iwl_mvm_rm_snif_sta(struct iwl_mvm *mvm, struct ieee80211_vif *vif)
{
	int ret;

	lockdep_assert_held(&mvm->mutex);

	if (WARN_ON_ONCE(mvm->snif_sta.sta_id == IWL_MVM_INVALID_STA))
		return -EINVAL;

	iwl_mvm_disable_txq(mvm, NULL, &mvm->snif_queue, IWL_MAX_TID_COUNT, 0);
	ret = iwl_mvm_rm_sta_common(mvm, mvm->snif_sta.sta_id);
	if (ret)
		IWL_WARN(mvm, "Failed sending remove station\n");

	return ret;
}

int iwl_mvm_rm_aux_sta(struct iwl_mvm *mvm)
{
	int ret;

	lockdep_assert_held(&mvm->mutex);

	if (WARN_ON_ONCE(mvm->aux_sta.sta_id == IWL_MVM_INVALID_STA))
		return -EINVAL;

	iwl_mvm_disable_txq(mvm, NULL, &mvm->aux_queue, IWL_MAX_TID_COUNT, 0);
	ret = iwl_mvm_rm_sta_common(mvm, mvm->aux_sta.sta_id);
	if (ret)
		IWL_WARN(mvm, "Failed sending remove station\n");
	iwl_mvm_dealloc_int_sta(mvm, &mvm->aux_sta);

	return ret;
}

void iwl_mvm_dealloc_snif_sta(struct iwl_mvm *mvm)
{
	iwl_mvm_dealloc_int_sta(mvm, &mvm->snif_sta);
}

/*
 * Send the add station command for the vif's broadcast station.
 * Assumes that the station was already allocated.
 *
 * @mvm: the mvm component
 * @vif: the interface to which the broadcast station is added
 * @bsta: the broadcast station to add.
 */
int iwl_mvm_send_add_bcast_sta(struct iwl_mvm *mvm, struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm_int_sta *bsta = &mvmvif->bcast_sta;
	static const u8 _baddr[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
	const u8 *baddr = _baddr;
	int queue;
	int ret;
	unsigned int wdg_timeout =
		iwl_mvm_get_wd_timeout(mvm, vif, false, false);
	struct iwl_trans_txq_scd_cfg cfg = {
		.fifo = IWL_MVM_TX_FIFO_VO,
		.sta_id = mvmvif->bcast_sta.sta_id,
		.tid = IWL_MAX_TID_COUNT,
		.aggregate = false,
		.frame_limit = IWL_FRAME_LIMIT,
	};

	lockdep_assert_held(&mvm->mutex);

	if (!iwl_mvm_has_new_tx_api(mvm)) {
		if (vif->type == NL80211_IFTYPE_AP ||
		    vif->type == NL80211_IFTYPE_ADHOC) {
			queue = mvm->probe_queue;
		} else if (vif->type == NL80211_IFTYPE_P2P_DEVICE) {
			queue = mvm->p2p_dev_queue;
		} else {
			WARN(1, "Missing required TXQ for adding bcast STA\n");
			return -EINVAL;
		}

		bsta->tfd_queue_msk |= BIT(queue);

		iwl_mvm_enable_txq(mvm, NULL, queue, 0, &cfg, wdg_timeout);
	}

	if (vif->type == NL80211_IFTYPE_ADHOC)
		baddr = vif->bss_conf.bssid;

	if (WARN_ON_ONCE(bsta->sta_id == IWL_MVM_INVALID_STA))
		return -ENOSPC;

	ret = iwl_mvm_add_int_sta_common(mvm, bsta, baddr,
					 mvmvif->id, mvmvif->color);
	if (ret)
		return ret;

	/*
	 * For 22000 firmware and on we cannot add queue to a station unknown
	 * to firmware so enable queue here - after the station was added
	 */
	if (iwl_mvm_has_new_tx_api(mvm)) {
		queue = iwl_mvm_tvqm_enable_txq(mvm, bsta->sta_id,
						IWL_MAX_TID_COUNT,
						wdg_timeout);
		if (queue < 0) {
			iwl_mvm_rm_sta_common(mvm, bsta->sta_id);
			return queue;
		}

		if (vif->type == NL80211_IFTYPE_AP ||
		    vif->type == NL80211_IFTYPE_ADHOC)
			mvm->probe_queue = queue;
		else if (vif->type == NL80211_IFTYPE_P2P_DEVICE)
			mvm->p2p_dev_queue = queue;
	}

	return 0;
}

static void iwl_mvm_free_bcast_sta_queues(struct iwl_mvm *mvm,
					  struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	u16 *queueptr, queue;

	lockdep_assert_held(&mvm->mutex);

	iwl_mvm_flush_sta(mvm, &mvmvif->bcast_sta, true);

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
		return;
	}

	queue = *queueptr;
	iwl_mvm_disable_txq(mvm, NULL, queueptr, IWL_MAX_TID_COUNT, 0);
	if (iwl_mvm_has_new_tx_api(mvm))
		return;

	WARN_ON(!(mvmvif->bcast_sta.tfd_queue_msk & BIT(queue)));
	mvmvif->bcast_sta.tfd_queue_msk &= ~BIT(queue);
}

/* Send the FW a request to remove the station from it's internal data
 * structures, but DO NOT remove the entry from the local data structures. */
int iwl_mvm_send_rm_bcast_sta(struct iwl_mvm *mvm, struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	int ret;

	lockdep_assert_held(&mvm->mutex);

	iwl_mvm_free_bcast_sta_queues(mvm, vif);

	ret = iwl_mvm_rm_sta_common(mvm, mvmvif->bcast_sta.sta_id);
	if (ret)
		IWL_WARN(mvm, "Failed sending remove station\n");
	return ret;
}

int iwl_mvm_alloc_bcast_sta(struct iwl_mvm *mvm, struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);

	lockdep_assert_held(&mvm->mutex);

	return iwl_mvm_allocate_int_sta(mvm, &mvmvif->bcast_sta, 0,
					ieee80211_vif_type_p2p(vif),
					IWL_STA_GENERAL_PURPOSE);
}

/* Allocate a new station entry for the broadcast station to the given vif,
 * and send it to the FW.
 * Note that each P2P mac should have its own broadcast station.
 *
 * @mvm: the mvm component
 * @vif: the interface to which the broadcast station is added
 * @bsta: the broadcast station to add. */
int iwl_mvm_add_p2p_bcast_sta(struct iwl_mvm *mvm, struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm_int_sta *bsta = &mvmvif->bcast_sta;
	int ret;

	lockdep_assert_held(&mvm->mutex);

	ret = iwl_mvm_alloc_bcast_sta(mvm, vif);
	if (ret)
		return ret;

	ret = iwl_mvm_send_add_bcast_sta(mvm, vif);

	if (ret)
		iwl_mvm_dealloc_int_sta(mvm, bsta);

	return ret;
}

void iwl_mvm_dealloc_bcast_sta(struct iwl_mvm *mvm, struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);

	iwl_mvm_dealloc_int_sta(mvm, &mvmvif->bcast_sta);
}

/*
 * Send the FW a request to remove the station from it's internal data
 * structures, and in addition remove it from the local data structure.
 */
int iwl_mvm_rm_p2p_bcast_sta(struct iwl_mvm *mvm, struct ieee80211_vif *vif)
{
	int ret;

	lockdep_assert_held(&mvm->mutex);

	ret = iwl_mvm_send_rm_bcast_sta(mvm, vif);

	iwl_mvm_dealloc_bcast_sta(mvm, vif);

	return ret;
}

/*
 * Allocate a new station entry for the multicast station to the given vif,
 * and send it to the FW.
 * Note that each AP/GO mac should have its own multicast station.
 *
 * @mvm: the mvm component
 * @vif: the interface to which the multicast station is added
 */
int iwl_mvm_add_mcast_sta(struct iwl_mvm *mvm, struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm_int_sta *msta = &mvmvif->mcast_sta;
	static const u8 _maddr[] = {0x03, 0x00, 0x00, 0x00, 0x00, 0x00};
	const u8 *maddr = _maddr;
	struct iwl_trans_txq_scd_cfg cfg = {
		.fifo = vif->type == NL80211_IFTYPE_AP ?
			IWL_MVM_TX_FIFO_MCAST : IWL_MVM_TX_FIFO_BE,
		.sta_id = msta->sta_id,
		.tid = 0,
		.aggregate = false,
		.frame_limit = IWL_FRAME_LIMIT,
	};
	unsigned int timeout = iwl_mvm_get_wd_timeout(mvm, vif, false, false);
	int ret;

	lockdep_assert_held(&mvm->mutex);

	if (WARN_ON(vif->type != NL80211_IFTYPE_AP &&
		    vif->type != NL80211_IFTYPE_ADHOC))
		return -ENOTSUPP;

	/*
	 * In IBSS, ieee80211_check_queues() sets the cab_queue to be
	 * invalid, so make sure we use the queue we want.
	 * Note that this is done here as we want to avoid making DQA
	 * changes in mac80211 layer.
	 */
	if (vif->type == NL80211_IFTYPE_ADHOC)
		mvmvif->cab_queue = IWL_MVM_DQA_GCAST_QUEUE;

	/*
	 * While in previous FWs we had to exclude cab queue from TFD queue
	 * mask, now it is needed as any other queue.
	 */
	if (!iwl_mvm_has_new_tx_api(mvm) &&
	    fw_has_api(&mvm->fw->ucode_capa, IWL_UCODE_TLV_API_STA_TYPE)) {
		iwl_mvm_enable_txq(mvm, NULL, mvmvif->cab_queue, 0, &cfg,
				   timeout);
		msta->tfd_queue_msk |= BIT(mvmvif->cab_queue);
	}
	ret = iwl_mvm_add_int_sta_common(mvm, msta, maddr,
					 mvmvif->id, mvmvif->color);
	if (ret)
		goto err;

	/*
	 * Enable cab queue after the ADD_STA command is sent.
	 * This is needed for 22000 firmware which won't accept SCD_QUEUE_CFG
	 * command with unknown station id, and for FW that doesn't support
	 * station API since the cab queue is not included in the
	 * tfd_queue_mask.
	 */
	if (iwl_mvm_has_new_tx_api(mvm)) {
		int queue = iwl_mvm_tvqm_enable_txq(mvm, msta->sta_id,
						    0,
						    timeout);
		if (queue < 0) {
			ret = queue;
			goto err;
		}
		mvmvif->cab_queue = queue;
	} else if (!fw_has_api(&mvm->fw->ucode_capa,
			       IWL_UCODE_TLV_API_STA_TYPE))
		iwl_mvm_enable_txq(mvm, NULL, mvmvif->cab_queue, 0, &cfg,
				   timeout);

	return 0;
err:
	iwl_mvm_dealloc_int_sta(mvm, msta);
	return ret;
}

static int __iwl_mvm_remove_sta_key(struct iwl_mvm *mvm, u8 sta_id,
				    struct ieee80211_key_conf *keyconf,
				    bool mcast)
{
	union {
		struct iwl_mvm_add_sta_key_cmd_v1 cmd_v1;
		struct iwl_mvm_add_sta_key_cmd cmd;
	} u = {};
	bool new_api = fw_has_api(&mvm->fw->ucode_capa,
				  IWL_UCODE_TLV_API_TKIP_MIC_KEYS);
	__le16 key_flags;
	int ret, size;
	u32 status;

	/* This is a valid situation for GTK removal */
	if (sta_id == IWL_MVM_INVALID_STA)
		return 0;

	key_flags = cpu_to_le16((keyconf->keyidx << STA_KEY_FLG_KEYID_POS) &
				 STA_KEY_FLG_KEYID_MSK);
	key_flags |= cpu_to_le16(STA_KEY_FLG_NO_ENC | STA_KEY_FLG_WEP_KEY_MAP);
	key_flags |= cpu_to_le16(STA_KEY_NOT_VALID);

	if (mcast)
		key_flags |= cpu_to_le16(STA_KEY_MULTICAST);

	/*
	 * The fields assigned here are in the same location at the start
	 * of the command, so we can do this union trick.
	 */
	u.cmd.common.key_flags = key_flags;
	u.cmd.common.key_offset = keyconf->hw_key_idx;
	u.cmd.common.sta_id = sta_id;

	size = new_api ? sizeof(u.cmd) : sizeof(u.cmd_v1);

	status = ADD_STA_SUCCESS;
	ret = iwl_mvm_send_cmd_pdu_status(mvm, ADD_STA_KEY, size, &u.cmd,
					  &status);

	switch (status) {
	case ADD_STA_SUCCESS:
		IWL_DEBUG_WEP(mvm, "MODIFY_STA: remove sta key passed\n");
		break;
	default:
		ret = -EIO;
		IWL_ERR(mvm, "MODIFY_STA: remove sta key failed\n");
		break;
	}

	return ret;
}

/*
 * Send the FW a request to remove the station from it's internal data
 * structures, and in addition remove it from the local data structure.
 */
int iwl_mvm_rm_mcast_sta(struct iwl_mvm *mvm, struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	int ret;

	lockdep_assert_held(&mvm->mutex);

	iwl_mvm_flush_sta(mvm, &mvmvif->mcast_sta, true);

	iwl_mvm_disable_txq(mvm, NULL, &mvmvif->cab_queue, 0, 0);

	ret = iwl_mvm_rm_sta_common(mvm, mvmvif->mcast_sta.sta_id);
	if (ret)
		IWL_WARN(mvm, "Failed sending remove station\n");

	return ret;
}

#define IWL_MAX_RX_BA_SESSIONS 16

static void iwl_mvm_sync_rxq_del_ba(struct iwl_mvm *mvm, u8 baid)
{
	struct iwl_mvm_delba_data notif = {
		.baid = baid,
	};

	iwl_mvm_sync_rx_queues_internal(mvm, IWL_MVM_RXQ_NOTIF_DEL_BA, true,
					&notif, sizeof(notif));
};

static void iwl_mvm_free_reorder(struct iwl_mvm *mvm,
				 struct iwl_mvm_baid_data *data)
{
	int i;

	iwl_mvm_sync_rxq_del_ba(mvm, data->baid);

	for (i = 0; i < mvm->trans->num_rx_queues; i++) {
		int j;
		struct iwl_mvm_reorder_buffer *reorder_buf =
			&data->reorder_buf[i];
		struct iwl_mvm_reorder_buf_entry *entries =
			&data->entries[i * data->entries_per_queue];

		spin_lock_bh(&reorder_buf->lock);
		if (likely(!reorder_buf->num_stored)) {
			spin_unlock_bh(&reorder_buf->lock);
			continue;
		}

		/*
		 * This shouldn't happen in regular DELBA since the internal
		 * delBA notification should trigger a release of all frames in
		 * the reorder buffer.
		 */
		WARN_ON(1);

		for (j = 0; j < reorder_buf->buf_size; j++)
			__skb_queue_purge(&entries[j].e.frames);
		/*
		 * Prevent timer re-arm. This prevents a very far fetched case
		 * where we timed out on the notification. There may be prior
		 * RX frames pending in the RX queue before the notification
		 * that might get processed between now and the actual deletion
		 * and we would re-arm the timer although we are deleting the
		 * reorder buffer.
		 */
		reorder_buf->removed = true;
		spin_unlock_bh(&reorder_buf->lock);
		del_timer_sync(&reorder_buf->reorder_timer);
	}
}

static void iwl_mvm_init_reorder_buffer(struct iwl_mvm *mvm,
					struct iwl_mvm_baid_data *data,
					u16 ssn, u16 buf_size)
{
	int i;

	for (i = 0; i < mvm->trans->num_rx_queues; i++) {
		struct iwl_mvm_reorder_buffer *reorder_buf =
			&data->reorder_buf[i];
		struct iwl_mvm_reorder_buf_entry *entries =
			&data->entries[i * data->entries_per_queue];
		int j;

		reorder_buf->num_stored = 0;
		reorder_buf->head_sn = ssn;
		reorder_buf->buf_size = buf_size;
		/* rx reorder timer */
		timer_setup(&reorder_buf->reorder_timer,
			    iwl_mvm_reorder_timer_expired, 0);
		spin_lock_init(&reorder_buf->lock);
		reorder_buf->mvm = mvm;
		reorder_buf->queue = i;
		reorder_buf->valid = false;
		for (j = 0; j < reorder_buf->buf_size; j++)
			__skb_queue_head_init(&entries[j].e.frames);
	}
}

int iwl_mvm_sta_rx_agg(struct iwl_mvm *mvm, struct ieee80211_sta *sta,
		       int tid, u16 ssn, bool start, u16 buf_size, u16 timeout)
{
	struct iwl_mvm_sta *mvm_sta = iwl_mvm_sta_from_mac80211(sta);
	struct iwl_mvm_add_sta_cmd cmd = {};
	struct iwl_mvm_baid_data *baid_data = NULL;
	int ret;
	u32 status;

	lockdep_assert_held(&mvm->mutex);

	if (start && mvm->rx_ba_sessions >= IWL_MAX_RX_BA_SESSIONS) {
		IWL_WARN(mvm, "Not enough RX BA SESSIONS\n");
		return -ENOSPC;
	}

	if (iwl_mvm_has_new_rx_api(mvm) && start) {
		u16 reorder_buf_size = buf_size * sizeof(baid_data->entries[0]);

		/* sparse doesn't like the __align() so don't check */
#ifndef __CHECKER__
		/*
		 * The division below will be OK if either the cache line size
		 * can be divided by the entry size (ALIGN will round up) or if
		 * if the entry size can be divided by the cache line size, in
		 * which case the ALIGN() will do nothing.
		 */
		BUILD_BUG_ON(SMP_CACHE_BYTES % sizeof(baid_data->entries[0]) &&
			     sizeof(baid_data->entries[0]) % SMP_CACHE_BYTES);
#endif

		/*
		 * Upward align the reorder buffer size to fill an entire cache
		 * line for each queue, to avoid sharing cache lines between
		 * different queues.
		 */
		reorder_buf_size = ALIGN(reorder_buf_size, SMP_CACHE_BYTES);

		/*
		 * Allocate here so if allocation fails we can bail out early
		 * before starting the BA session in the firmware
		 */
		baid_data = kzalloc(sizeof(*baid_data) +
				    mvm->trans->num_rx_queues *
				    reorder_buf_size,
				    GFP_KERNEL);
		if (!baid_data)
			return -ENOMEM;

		/*
		 * This division is why we need the above BUILD_BUG_ON(),
		 * if that doesn't hold then this will not be right.
		 */
		baid_data->entries_per_queue =
			reorder_buf_size / sizeof(baid_data->entries[0]);
	}

	cmd.mac_id_n_color = cpu_to_le32(mvm_sta->mac_id_n_color);
	cmd.sta_id = mvm_sta->sta_id;
	cmd.add_modify = STA_MODE_MODIFY;
	if (start) {
		cmd.add_immediate_ba_tid = (u8) tid;
		cmd.add_immediate_ba_ssn = cpu_to_le16(ssn);
		cmd.rx_ba_window = cpu_to_le16(buf_size);
	} else {
		cmd.remove_immediate_ba_tid = (u8) tid;
	}
	cmd.modify_mask = start ? STA_MODIFY_ADD_BA_TID :
				  STA_MODIFY_REMOVE_BA_TID;

	status = ADD_STA_SUCCESS;
	ret = iwl_mvm_send_cmd_pdu_status(mvm, ADD_STA,
					  iwl_mvm_add_sta_cmd_size(mvm),
					  &cmd, &status);
	if (ret)
		goto out_free;

	switch (status & IWL_ADD_STA_STATUS_MASK) {
	case ADD_STA_SUCCESS:
		IWL_DEBUG_HT(mvm, "RX BA Session %sed in fw\n",
			     start ? "start" : "stopp");
		break;
	case ADD_STA_IMMEDIATE_BA_FAILURE:
		IWL_WARN(mvm, "RX BA Session refused by fw\n");
		ret = -ENOSPC;
		break;
	default:
		ret = -EIO;
		IWL_ERR(mvm, "RX BA Session failed %sing, status 0x%x\n",
			start ? "start" : "stopp", status);
		break;
	}

	if (ret)
		goto out_free;

	if (start) {
		u8 baid;

		mvm->rx_ba_sessions++;

		if (!iwl_mvm_has_new_rx_api(mvm))
			return 0;

		if (WARN_ON(!(status & IWL_ADD_STA_BAID_VALID_MASK))) {
			ret = -EINVAL;
			goto out_free;
		}
		baid = (u8)((status & IWL_ADD_STA_BAID_MASK) >>
			    IWL_ADD_STA_BAID_SHIFT);
		baid_data->baid = baid;
		baid_data->timeout = timeout;
		baid_data->last_rx = jiffies;
		baid_data->rcu_ptr = &mvm->baid_map[baid];
		timer_setup(&baid_data->session_timer,
			    iwl_mvm_rx_agg_session_expired, 0);
		baid_data->mvm = mvm;
		baid_data->tid = tid;
		baid_data->sta_id = mvm_sta->sta_id;

		mvm_sta->tid_to_baid[tid] = baid;
		if (timeout)
			mod_timer(&baid_data->session_timer,
				  TU_TO_EXP_TIME(timeout * 2));

		iwl_mvm_init_reorder_buffer(mvm, baid_data, ssn, buf_size);
		/*
		 * protect the BA data with RCU to cover a case where our
		 * internal RX sync mechanism will timeout (not that it's
		 * supposed to happen) and we will free the session data while
		 * RX is being processed in parallel
		 */
		IWL_DEBUG_HT(mvm, "Sta %d(%d) is assigned to BAID %d\n",
			     mvm_sta->sta_id, tid, baid);
		WARN_ON(rcu_access_pointer(mvm->baid_map[baid]));
		rcu_assign_pointer(mvm->baid_map[baid], baid_data);
	} else  {
		u8 baid = mvm_sta->tid_to_baid[tid];

		if (mvm->rx_ba_sessions > 0)
			/* check that restart flow didn't zero the counter */
			mvm->rx_ba_sessions--;
		if (!iwl_mvm_has_new_rx_api(mvm))
			return 0;

		if (WARN_ON(baid == IWL_RX_REORDER_DATA_INVALID_BAID))
			return -EINVAL;

		baid_data = rcu_access_pointer(mvm->baid_map[baid]);
		if (WARN_ON(!baid_data))
			return -EINVAL;

		/* synchronize all rx queues so we can safely delete */
		iwl_mvm_free_reorder(mvm, baid_data);
		del_timer_sync(&baid_data->session_timer);
		RCU_INIT_POINTER(mvm->baid_map[baid], NULL);
		kfree_rcu(baid_data, rcu_head);
		IWL_DEBUG_HT(mvm, "BAID %d is free\n", baid);

		/*
		 * After we've deleted it, do another queue sync
		 * so if an IWL_MVM_RXQ_NSSN_SYNC was concurrently
		 * running it won't find a new session in the old
		 * BAID. It can find the NULL pointer for the BAID,
		 * but we must not have it find a different session.
		 */
		iwl_mvm_sync_rx_queues_internal(mvm, IWL_MVM_RXQ_EMPTY,
						true, NULL, 0);
	}
	return 0;

out_free:
	kfree(baid_data);
	return ret;
}

int iwl_mvm_sta_tx_agg(struct iwl_mvm *mvm, struct ieee80211_sta *sta,
		       int tid, u8 queue, bool start)
{
	struct iwl_mvm_sta *mvm_sta = iwl_mvm_sta_from_mac80211(sta);
	struct iwl_mvm_add_sta_cmd cmd = {};
	int ret;
	u32 status;

	lockdep_assert_held(&mvm->mutex);

	if (start) {
		mvm_sta->tfd_queue_msk |= BIT(queue);
		mvm_sta->tid_disable_agg &= ~BIT(tid);
	} else {
		/* In DQA-mode the queue isn't removed on agg termination */
		mvm_sta->tid_disable_agg |= BIT(tid);
	}

	cmd.mac_id_n_color = cpu_to_le32(mvm_sta->mac_id_n_color);
	cmd.sta_id = mvm_sta->sta_id;
	cmd.add_modify = STA_MODE_MODIFY;
	if (!iwl_mvm_has_new_tx_api(mvm))
		cmd.modify_mask = STA_MODIFY_QUEUES;
	cmd.modify_mask |= STA_MODIFY_TID_DISABLE_TX;
	cmd.tfd_queue_msk = cpu_to_le32(mvm_sta->tfd_queue_msk);
	cmd.tid_disable_tx = cpu_to_le16(mvm_sta->tid_disable_agg);

	status = ADD_STA_SUCCESS;
	ret = iwl_mvm_send_cmd_pdu_status(mvm, ADD_STA,
					  iwl_mvm_add_sta_cmd_size(mvm),
					  &cmd, &status);
	if (ret)
		return ret;

	switch (status & IWL_ADD_STA_STATUS_MASK) {
	case ADD_STA_SUCCESS:
		break;
	default:
		ret = -EIO;
		IWL_ERR(mvm, "TX BA Session failed %sing, status 0x%x\n",
			start ? "start" : "stopp", status);
		break;
	}

	return ret;
}

const u8 tid_to_mac80211_ac[] = {
	IEEE80211_AC_BE,
	IEEE80211_AC_BK,
	IEEE80211_AC_BK,
	IEEE80211_AC_BE,
	IEEE80211_AC_VI,
	IEEE80211_AC_VI,
	IEEE80211_AC_VO,
	IEEE80211_AC_VO,
	IEEE80211_AC_VO, /* We treat MGMT as TID 8, which is set as AC_VO */
};

static const u8 tid_to_ucode_ac[] = {
	AC_BE,
	AC_BK,
	AC_BK,
	AC_BE,
	AC_VI,
	AC_VI,
	AC_VO,
	AC_VO,
};

int iwl_mvm_sta_tx_agg_start(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			     struct ieee80211_sta *sta, u16 tid, u16 *ssn)
{
	struct iwl_mvm_sta *mvmsta = iwl_mvm_sta_from_mac80211(sta);
	struct iwl_mvm_tid_data *tid_data;
	u16 normalized_ssn;
	u16 txq_id;
	int ret;

	if (WARN_ON_ONCE(tid >= IWL_MAX_TID_COUNT))
		return -EINVAL;

	if (mvmsta->tid_data[tid].state != IWL_AGG_QUEUED &&
	    mvmsta->tid_data[tid].state != IWL_AGG_OFF) {
		IWL_ERR(mvm,
			"Start AGG when state is not IWL_AGG_QUEUED or IWL_AGG_OFF %d!\n",
			mvmsta->tid_data[tid].state);
		return -ENXIO;
	}

	lockdep_assert_held(&mvm->mutex);

	if (mvmsta->tid_data[tid].txq_id == IWL_MVM_INVALID_QUEUE &&
	    iwl_mvm_has_new_tx_api(mvm)) {
		u8 ac = tid_to_mac80211_ac[tid];

		ret = iwl_mvm_sta_alloc_queue_tvqm(mvm, sta, ac, tid);
		if (ret)
			return ret;
	}

	spin_lock_bh(&mvmsta->lock);

	/*
	 * Note the possible cases:
	 *  1. An enabled TXQ - TXQ needs to become agg'ed
	 *  2. The TXQ hasn't yet been enabled, so find a free one and mark
	 *	it as reserved
	 */
	txq_id = mvmsta->tid_data[tid].txq_id;
	if (txq_id == IWL_MVM_INVALID_QUEUE) {
		ret = iwl_mvm_find_free_queue(mvm, mvmsta->sta_id,
					      IWL_MVM_DQA_MIN_DATA_QUEUE,
					      IWL_MVM_DQA_MAX_DATA_QUEUE);
		if (ret < 0) {
			IWL_ERR(mvm, "Failed to allocate agg queue\n");
			goto out;
		}

		txq_id = ret;

		/* TXQ hasn't yet been enabled, so mark it only as reserved */
		mvm->queue_info[txq_id].status = IWL_MVM_QUEUE_RESERVED;
	} else if (WARN_ON(txq_id >= IWL_MAX_HW_QUEUES)) {
		ret = -ENXIO;
		IWL_ERR(mvm, "tid_id %d out of range (0, %d)!\n",
			tid, IWL_MAX_HW_QUEUES - 1);
		goto out;

	} else if (unlikely(mvm->queue_info[txq_id].status ==
			    IWL_MVM_QUEUE_SHARED)) {
		ret = -ENXIO;
		IWL_DEBUG_TX_QUEUES(mvm,
				    "Can't start tid %d agg on shared queue!\n",
				    tid);
		goto out;
	}

	IWL_DEBUG_TX_QUEUES(mvm,
			    "AGG for tid %d will be on queue #%d\n",
			    tid, txq_id);

	tid_data = &mvmsta->tid_data[tid];
	tid_data->ssn = IEEE80211_SEQ_TO_SN(tid_data->seq_number);
	tid_data->txq_id = txq_id;
	*ssn = tid_data->ssn;

	IWL_DEBUG_TX_QUEUES(mvm,
			    "Start AGG: sta %d tid %d queue %d - ssn = %d, next_recl = %d\n",
			    mvmsta->sta_id, tid, txq_id, tid_data->ssn,
			    tid_data->next_reclaimed);

	/*
	 * In 22000 HW, the next_reclaimed index is only 8 bit, so we'll need
	 * to align the wrap around of ssn so we compare relevant values.
	 */
	normalized_ssn = tid_data->ssn;
	if (mvm->trans->trans_cfg->gen2)
		normalized_ssn &= 0xff;

	if (normalized_ssn == tid_data->next_reclaimed) {
		tid_data->state = IWL_AGG_STARTING;
		ret = IEEE80211_AMPDU_TX_START_IMMEDIATE;
	} else {
		tid_data->state = IWL_EMPTYING_HW_QUEUE_ADDBA;
		ret = IEEE80211_AMPDU_TX_START_DELAY_ADDBA;
	}

out:
	spin_unlock_bh(&mvmsta->lock);

	return ret;
}

int iwl_mvm_sta_tx_agg_oper(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			    struct ieee80211_sta *sta, u16 tid, u16 buf_size,
			    bool amsdu)
{
	struct iwl_mvm_sta *mvmsta = iwl_mvm_sta_from_mac80211(sta);
	struct iwl_mvm_tid_data *tid_data = &mvmsta->tid_data[tid];
	unsigned int wdg_timeout =
		iwl_mvm_get_wd_timeout(mvm, vif, sta->tdls, false);
	int queue, ret;
	bool alloc_queue = true;
	enum iwl_mvm_queue_status queue_status;
	u16 ssn;

	struct iwl_trans_txq_scd_cfg cfg = {
		.sta_id = mvmsta->sta_id,
		.tid = tid,
		.frame_limit = buf_size,
		.aggregate = true,
	};

	/*
	 * When FW supports TLC_OFFLOAD, it also implements Tx aggregation
	 * manager, so this function should never be called in this case.
	 */
	if (WARN_ON_ONCE(iwl_mvm_has_tlc_offload(mvm)))
		return -EINVAL;

	BUILD_BUG_ON((sizeof(mvmsta->agg_tids) * BITS_PER_BYTE)
		     != IWL_MAX_TID_COUNT);

	spin_lock_bh(&mvmsta->lock);
	ssn = tid_data->ssn;
	queue = tid_data->txq_id;
	tid_data->state = IWL_AGG_ON;
	mvmsta->agg_tids |= BIT(tid);
	tid_data->ssn = 0xffff;
	tid_data->amsdu_in_ampdu_allowed = amsdu;
	spin_unlock_bh(&mvmsta->lock);

	if (iwl_mvm_has_new_tx_api(mvm)) {
		/*
		 * If there is no queue for this tid, iwl_mvm_sta_tx_agg_start()
		 * would have failed, so if we are here there is no need to
		 * allocate a queue.
		 * However, if aggregation size is different than the default
		 * size, the scheduler should be reconfigured.
		 * We cannot do this with the new TX API, so return unsupported
		 * for now, until it will be offloaded to firmware..
		 * Note that if SCD default value changes - this condition
		 * should be updated as well.
		 */
		if (buf_size < IWL_FRAME_LIMIT)
			return -ENOTSUPP;

		ret = iwl_mvm_sta_tx_agg(mvm, sta, tid, queue, true);
		if (ret)
			return -EIO;
		goto out;
	}

	cfg.fifo = iwl_mvm_ac_to_tx_fifo[tid_to_mac80211_ac[tid]];

	queue_status = mvm->queue_info[queue].status;

	/* Maybe there is no need to even alloc a queue... */
	if (mvm->queue_info[queue].status == IWL_MVM_QUEUE_READY)
		alloc_queue = false;

	/*
	 * Only reconfig the SCD for the queue if the window size has
	 * changed from current (become smaller)
	 */
	if (!alloc_queue && buf_size < IWL_FRAME_LIMIT) {
		/*
		 * If reconfiguring an existing queue, it first must be
		 * drained
		 */
		ret = iwl_trans_wait_tx_queues_empty(mvm->trans,
						     BIT(queue));
		if (ret) {
			IWL_ERR(mvm,
				"Error draining queue before reconfig\n");
			return ret;
		}

		ret = iwl_mvm_reconfig_scd(mvm, queue, cfg.fifo,
					   mvmsta->sta_id, tid,
					   buf_size, ssn);
		if (ret) {
			IWL_ERR(mvm,
				"Error reconfiguring TXQ #%d\n", queue);
			return ret;
		}
	}

	if (alloc_queue)
		iwl_mvm_enable_txq(mvm, sta, queue, ssn,
				   &cfg, wdg_timeout);

	/* Send ADD_STA command to enable aggs only if the queue isn't shared */
	if (queue_status != IWL_MVM_QUEUE_SHARED) {
		ret = iwl_mvm_sta_tx_agg(mvm, sta, tid, queue, true);
		if (ret)
			return -EIO;
	}

	/* No need to mark as reserved */
	mvm->queue_info[queue].status = IWL_MVM_QUEUE_READY;

out:
	/*
	 * Even though in theory the peer could have different
	 * aggregation reorder buffer sizes for different sessions,
	 * our ucode doesn't allow for that and has a global limit
	 * for each station. Therefore, use the minimum of all the
	 * aggregation sessions and our default value.
	 */
	mvmsta->max_agg_bufsize =
		min(mvmsta->max_agg_bufsize, buf_size);
	mvmsta->lq_sta.rs_drv.lq.agg_frame_cnt_limit = mvmsta->max_agg_bufsize;

	IWL_DEBUG_HT(mvm, "Tx aggregation enabled on ra = %pM tid = %d\n",
		     sta->addr, tid);

	return iwl_mvm_send_lq_cmd(mvm, &mvmsta->lq_sta.rs_drv.lq);
}

static void iwl_mvm_unreserve_agg_queue(struct iwl_mvm *mvm,
					struct iwl_mvm_sta *mvmsta,
					struct iwl_mvm_tid_data *tid_data)
{
	u16 txq_id = tid_data->txq_id;

	lockdep_assert_held(&mvm->mutex);

	if (iwl_mvm_has_new_tx_api(mvm))
		return;

	/*
	 * The TXQ is marked as reserved only if no traffic came through yet
	 * This means no traffic has been sent on this TID (agg'd or not), so
	 * we no longer have use for the queue. Since it hasn't even been
	 * allocated through iwl_mvm_enable_txq, so we can just mark it back as
	 * free.
	 */
	if (mvm->queue_info[txq_id].status == IWL_MVM_QUEUE_RESERVED) {
		mvm->queue_info[txq_id].status = IWL_MVM_QUEUE_FREE;
		tid_data->txq_id = IWL_MVM_INVALID_QUEUE;
	}
}

int iwl_mvm_sta_tx_agg_stop(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			    struct ieee80211_sta *sta, u16 tid)
{
	struct iwl_mvm_sta *mvmsta = iwl_mvm_sta_from_mac80211(sta);
	struct iwl_mvm_tid_data *tid_data = &mvmsta->tid_data[tid];
	u16 txq_id;
	int err;

	/*
	 * If mac80211 is cleaning its state, then say that we finished since
	 * our state has been cleared anyway.
	 */
	if (test_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status)) {
		ieee80211_stop_tx_ba_cb_irqsafe(vif, sta->addr, tid);
		return 0;
	}

	spin_lock_bh(&mvmsta->lock);

	txq_id = tid_data->txq_id;

	IWL_DEBUG_TX_QUEUES(mvm, "Stop AGG: sta %d tid %d q %d state %d\n",
			    mvmsta->sta_id, tid, txq_id, tid_data->state);

	mvmsta->agg_tids &= ~BIT(tid);

	iwl_mvm_unreserve_agg_queue(mvm, mvmsta, tid_data);

	switch (tid_data->state) {
	case IWL_AGG_ON:
		tid_data->ssn = IEEE80211_SEQ_TO_SN(tid_data->seq_number);

		IWL_DEBUG_TX_QUEUES(mvm,
				    "ssn = %d, next_recl = %d\n",
				    tid_data->ssn, tid_data->next_reclaimed);

		tid_data->ssn = 0xffff;
		tid_data->state = IWL_AGG_OFF;
		spin_unlock_bh(&mvmsta->lock);

		ieee80211_stop_tx_ba_cb_irqsafe(vif, sta->addr, tid);

		iwl_mvm_sta_tx_agg(mvm, sta, tid, txq_id, false);
		return 0;
	case IWL_AGG_STARTING:
	case IWL_EMPTYING_HW_QUEUE_ADDBA:
		/*
		 * The agg session has been stopped before it was set up. This
		 * can happen when the AddBA timer times out for example.
		 */

		/* No barriers since we are under mutex */
		lockdep_assert_held(&mvm->mutex);

		ieee80211_stop_tx_ba_cb_irqsafe(vif, sta->addr, tid);
		tid_data->state = IWL_AGG_OFF;
		err = 0;
		break;
	default:
		IWL_ERR(mvm,
			"Stopping AGG while state not ON or starting for %d on %d (%d)\n",
			mvmsta->sta_id, tid, tid_data->state);
		IWL_ERR(mvm,
			"\ttid_data->txq_id = %d\n", tid_data->txq_id);
		err = -EINVAL;
	}

	spin_unlock_bh(&mvmsta->lock);

	return err;
}

int iwl_mvm_sta_tx_agg_flush(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			    struct ieee80211_sta *sta, u16 tid)
{
	struct iwl_mvm_sta *mvmsta = iwl_mvm_sta_from_mac80211(sta);
	struct iwl_mvm_tid_data *tid_data = &mvmsta->tid_data[tid];
	u16 txq_id;
	enum iwl_mvm_agg_state old_state;

	/*
	 * First set the agg state to OFF to avoid calling
	 * ieee80211_stop_tx_ba_cb in iwl_mvm_check_ratid_empty.
	 */
	spin_lock_bh(&mvmsta->lock);
	txq_id = tid_data->txq_id;
	IWL_DEBUG_TX_QUEUES(mvm, "Flush AGG: sta %d tid %d q %d state %d\n",
			    mvmsta->sta_id, tid, txq_id, tid_data->state);
	old_state = tid_data->state;
	tid_data->state = IWL_AGG_OFF;
	mvmsta->agg_tids &= ~BIT(tid);
	spin_unlock_bh(&mvmsta->lock);

	iwl_mvm_unreserve_agg_queue(mvm, mvmsta, tid_data);

	if (old_state >= IWL_AGG_ON) {
		iwl_mvm_drain_sta(mvm, mvmsta, true);

		if (iwl_mvm_has_new_tx_api(mvm)) {
			if (iwl_mvm_flush_sta_tids(mvm, mvmsta->sta_id,
						   BIT(tid)))
				IWL_ERR(mvm, "Couldn't flush the AGG queue\n");
			iwl_trans_wait_txq_empty(mvm->trans, txq_id);
		} else {
			if (iwl_mvm_flush_tx_path(mvm, BIT(txq_id)))
				IWL_ERR(mvm, "Couldn't flush the AGG queue\n");
			iwl_trans_wait_tx_queues_empty(mvm->trans, BIT(txq_id));
		}

		iwl_mvm_drain_sta(mvm, mvmsta, false);

		iwl_mvm_sta_tx_agg(mvm, sta, tid, txq_id, false);
	}

	return 0;
}

static int iwl_mvm_set_fw_key_idx(struct iwl_mvm *mvm)
{
	int i, max = -1, max_offs = -1;

	lockdep_assert_held(&mvm->mutex);

	/* Pick the unused key offset with the highest 'deleted'
	 * counter. Every time a key is deleted, all the counters
	 * are incremented and the one that was just deleted is
	 * reset to zero. Thus, the highest counter is the one
	 * that was deleted longest ago. Pick that one.
	 */
	for (i = 0; i < STA_KEY_MAX_NUM; i++) {
		if (test_bit(i, mvm->fw_key_table))
			continue;
		if (mvm->fw_key_deleted[i] > max) {
			max = mvm->fw_key_deleted[i];
			max_offs = i;
		}
	}

	if (max_offs < 0)
		return STA_KEY_IDX_INVALID;

	return max_offs;
}

static struct iwl_mvm_sta *iwl_mvm_get_key_sta(struct iwl_mvm *mvm,
					       struct ieee80211_vif *vif,
					       struct ieee80211_sta *sta)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);

	if (sta)
		return iwl_mvm_sta_from_mac80211(sta);

	/*
	 * The device expects GTKs for station interfaces to be
	 * installed as GTKs for the AP station. If we have no
	 * station ID, then use AP's station ID.
	 */
	if (vif->type == NL80211_IFTYPE_STATION &&
	    mvmvif->ap_sta_id != IWL_MVM_INVALID_STA) {
		u8 sta_id = mvmvif->ap_sta_id;

		sta = rcu_dereference_check(mvm->fw_id_to_mac_id[sta_id],
					    lockdep_is_held(&mvm->mutex));

		/*
		 * It is possible that the 'sta' parameter is NULL,
		 * for example when a GTK is removed - the sta_id will then
		 * be the AP ID, and no station was passed by mac80211.
		 */
		if (IS_ERR_OR_NULL(sta))
			return NULL;

		return iwl_mvm_sta_from_mac80211(sta);
	}

	return NULL;
}

static int iwl_mvm_pn_cmp(const u8 *pn1, const u8 *pn2, int len)
{
	int i;

	for (i = len - 1; i >= 0; i--) {
		if (pn1[i] > pn2[i])
			return 1;
		if (pn1[i] < pn2[i])
			return -1;
	}

	return 0;
}

static int iwl_mvm_send_sta_key(struct iwl_mvm *mvm,
				u32 sta_id,
				struct ieee80211_key_conf *key, bool mcast,
				u32 tkip_iv32, u16 *tkip_p1k, u32 cmd_flags,
				u8 key_offset, bool mfp)
{
	union {
		struct iwl_mvm_add_sta_key_cmd_v1 cmd_v1;
		struct iwl_mvm_add_sta_key_cmd cmd;
	} u = {};
	__le16 key_flags;
	int ret;
	u32 status;
	u16 keyidx;
	u64 pn = 0;
	int i, size;
	bool new_api = fw_has_api(&mvm->fw->ucode_capa,
				  IWL_UCODE_TLV_API_TKIP_MIC_KEYS);
	int api_ver = iwl_fw_lookup_cmd_ver(mvm->fw, ADD_STA_KEY,
					    new_api ? 2 : 1);

	if (sta_id == IWL_MVM_INVALID_STA)
		return -EINVAL;

	keyidx = (key->keyidx << STA_KEY_FLG_KEYID_POS) &
		 STA_KEY_FLG_KEYID_MSK;
	key_flags = cpu_to_le16(keyidx);
	key_flags |= cpu_to_le16(STA_KEY_FLG_WEP_KEY_MAP);

	switch (key->cipher) {
	case WLAN_CIPHER_SUITE_TKIP:
		key_flags |= cpu_to_le16(STA_KEY_FLG_TKIP);
		if (api_ver >= 2) {
			memcpy((void *)&u.cmd.tx_mic_key,
			       &key->key[NL80211_TKIP_DATA_OFFSET_TX_MIC_KEY],
			       IWL_MIC_KEY_SIZE);

			memcpy((void *)&u.cmd.rx_mic_key,
			       &key->key[NL80211_TKIP_DATA_OFFSET_RX_MIC_KEY],
			       IWL_MIC_KEY_SIZE);
			pn = atomic64_read(&key->tx_pn);

		} else {
			u.cmd_v1.tkip_rx_tsc_byte2 = tkip_iv32;
			for (i = 0; i < 5; i++)
				u.cmd_v1.tkip_rx_ttak[i] =
					cpu_to_le16(tkip_p1k[i]);
		}
		memcpy(u.cmd.common.key, key->key, key->keylen);
		break;
	case WLAN_CIPHER_SUITE_CCMP:
		key_flags |= cpu_to_le16(STA_KEY_FLG_CCM);
		memcpy(u.cmd.common.key, key->key, key->keylen);
		if (api_ver >= 2)
			pn = atomic64_read(&key->tx_pn);
		break;
	case WLAN_CIPHER_SUITE_WEP104:
		key_flags |= cpu_to_le16(STA_KEY_FLG_WEP_13BYTES);
		fallthrough;
	case WLAN_CIPHER_SUITE_WEP40:
		key_flags |= cpu_to_le16(STA_KEY_FLG_WEP);
		memcpy(u.cmd.common.key + 3, key->key, key->keylen);
		break;
	case WLAN_CIPHER_SUITE_GCMP_256:
		key_flags |= cpu_to_le16(STA_KEY_FLG_KEY_32BYTES);
		fallthrough;
	case WLAN_CIPHER_SUITE_GCMP:
		key_flags |= cpu_to_le16(STA_KEY_FLG_GCMP);
		memcpy(u.cmd.common.key, key->key, key->keylen);
		if (api_ver >= 2)
			pn = atomic64_read(&key->tx_pn);
		break;
	default:
		key_flags |= cpu_to_le16(STA_KEY_FLG_EXT);
		memcpy(u.cmd.common.key, key->key, key->keylen);
	}

	if (mcast)
		key_flags |= cpu_to_le16(STA_KEY_MULTICAST);
	if (mfp)
		key_flags |= cpu_to_le16(STA_KEY_MFP);

	u.cmd.common.key_offset = key_offset;
	u.cmd.common.key_flags = key_flags;
	u.cmd.common.sta_id = sta_id;

	if (key->cipher == WLAN_CIPHER_SUITE_TKIP)
		i = 0;
	else
		i = -1;

	for (; i < IEEE80211_NUM_TIDS; i++) {
		struct ieee80211_key_seq seq = {};
		u8 _rx_pn[IEEE80211_MAX_PN_LEN] = {}, *rx_pn = _rx_pn;
		int rx_pn_len = 8;
		/* there's a hole at 2/3 in FW format depending on version */
		int hole = api_ver >= 3 ? 0 : 2;

		ieee80211_get_key_rx_seq(key, i, &seq);

		if (key->cipher == WLAN_CIPHER_SUITE_TKIP) {
			rx_pn[0] = seq.tkip.iv16;
			rx_pn[1] = seq.tkip.iv16 >> 8;
			rx_pn[2 + hole] = seq.tkip.iv32;
			rx_pn[3 + hole] = seq.tkip.iv32 >> 8;
			rx_pn[4 + hole] = seq.tkip.iv32 >> 16;
			rx_pn[5 + hole] = seq.tkip.iv32 >> 24;
		} else if (key_flags & cpu_to_le16(STA_KEY_FLG_EXT)) {
			rx_pn = seq.hw.seq;
			rx_pn_len = seq.hw.seq_len;
		} else {
			rx_pn[0] = seq.ccmp.pn[0];
			rx_pn[1] = seq.ccmp.pn[1];
			rx_pn[2 + hole] = seq.ccmp.pn[2];
			rx_pn[3 + hole] = seq.ccmp.pn[3];
			rx_pn[4 + hole] = seq.ccmp.pn[4];
			rx_pn[5 + hole] = seq.ccmp.pn[5];
		}

		if (iwl_mvm_pn_cmp(rx_pn, (u8 *)&u.cmd.common.rx_secur_seq_cnt,
				   rx_pn_len) > 0)
			memcpy(&u.cmd.common.rx_secur_seq_cnt, rx_pn,
			       rx_pn_len);
	}

	if (api_ver >= 2) {
		u.cmd.transmit_seq_cnt = cpu_to_le64(pn);
		size = sizeof(u.cmd);
	} else {
		size = sizeof(u.cmd_v1);
	}

	status = ADD_STA_SUCCESS;
	if (cmd_flags & CMD_ASYNC)
		ret = iwl_mvm_send_cmd_pdu(mvm, ADD_STA_KEY, CMD_ASYNC, size,
					   &u.cmd);
	else
		ret = iwl_mvm_send_cmd_pdu_status(mvm, ADD_STA_KEY, size,
						  &u.cmd, &status);

	switch (status) {
	case ADD_STA_SUCCESS:
		IWL_DEBUG_WEP(mvm, "MODIFY_STA: set dynamic key passed\n");
		break;
	default:
		ret = -EIO;
		IWL_ERR(mvm, "MODIFY_STA: set dynamic key failed\n");
		break;
	}

	return ret;
}

static int iwl_mvm_send_sta_igtk(struct iwl_mvm *mvm,
				 struct ieee80211_key_conf *keyconf,
				 u8 sta_id, bool remove_key)
{
	struct iwl_mvm_mgmt_mcast_key_cmd igtk_cmd = {};

	/* verify the key details match the required command's expectations */
	if (WARN_ON((keyconf->flags & IEEE80211_KEY_FLAG_PAIRWISE) ||
		    (keyconf->keyidx != 4 && keyconf->keyidx != 5 &&
		     keyconf->keyidx != 6 && keyconf->keyidx != 7) ||
		    (keyconf->cipher != WLAN_CIPHER_SUITE_AES_CMAC &&
		     keyconf->cipher != WLAN_CIPHER_SUITE_BIP_GMAC_128 &&
		     keyconf->cipher != WLAN_CIPHER_SUITE_BIP_GMAC_256)))
		return -EINVAL;

	if (WARN_ON(!iwl_mvm_has_new_rx_api(mvm) &&
		    keyconf->cipher != WLAN_CIPHER_SUITE_AES_CMAC))
		return -EINVAL;

	igtk_cmd.key_id = cpu_to_le32(keyconf->keyidx);
	igtk_cmd.sta_id = cpu_to_le32(sta_id);

	if (remove_key) {
		/* This is a valid situation for IGTK */
		if (sta_id == IWL_MVM_INVALID_STA)
			return 0;

		igtk_cmd.ctrl_flags |= cpu_to_le32(STA_KEY_NOT_VALID);
	} else {
		struct ieee80211_key_seq seq;
		const u8 *pn;

		switch (keyconf->cipher) {
		case WLAN_CIPHER_SUITE_AES_CMAC:
			igtk_cmd.ctrl_flags |= cpu_to_le32(STA_KEY_FLG_CCM);
			break;
		case WLAN_CIPHER_SUITE_BIP_GMAC_128:
		case WLAN_CIPHER_SUITE_BIP_GMAC_256:
			igtk_cmd.ctrl_flags |= cpu_to_le32(STA_KEY_FLG_GCMP);
			break;
		default:
			return -EINVAL;
		}

		memcpy(igtk_cmd.igtk, keyconf->key, keyconf->keylen);
		if (keyconf->cipher == WLAN_CIPHER_SUITE_BIP_GMAC_256)
			igtk_cmd.ctrl_flags |=
				cpu_to_le32(STA_KEY_FLG_KEY_32BYTES);
		ieee80211_get_key_rx_seq(keyconf, 0, &seq);
		pn = seq.aes_cmac.pn;
		igtk_cmd.receive_seq_cnt = cpu_to_le64(((u64) pn[5] << 0) |
						       ((u64) pn[4] << 8) |
						       ((u64) pn[3] << 16) |
						       ((u64) pn[2] << 24) |
						       ((u64) pn[1] << 32) |
						       ((u64) pn[0] << 40));
	}

	IWL_DEBUG_INFO(mvm, "%s %sIGTK (%d) for sta %u\n",
		       remove_key ? "removing" : "installing",
		       keyconf->keyidx >= 6 ? "B" : "",
		       keyconf->keyidx, igtk_cmd.sta_id);

	if (!iwl_mvm_has_new_rx_api(mvm)) {
		struct iwl_mvm_mgmt_mcast_key_cmd_v1 igtk_cmd_v1 = {
			.ctrl_flags = igtk_cmd.ctrl_flags,
			.key_id = igtk_cmd.key_id,
			.sta_id = igtk_cmd.sta_id,
			.receive_seq_cnt = igtk_cmd.receive_seq_cnt
		};

		memcpy(igtk_cmd_v1.igtk, igtk_cmd.igtk,
		       ARRAY_SIZE(igtk_cmd_v1.igtk));
		return iwl_mvm_send_cmd_pdu(mvm, MGMT_MCAST_KEY, 0,
					    sizeof(igtk_cmd_v1), &igtk_cmd_v1);
	}
	return iwl_mvm_send_cmd_pdu(mvm, MGMT_MCAST_KEY, 0,
				    sizeof(igtk_cmd), &igtk_cmd);
}


static inline u8 *iwl_mvm_get_mac_addr(struct iwl_mvm *mvm,
				       struct ieee80211_vif *vif,
				       struct ieee80211_sta *sta)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);

	if (sta)
		return sta->addr;

	if (vif->type == NL80211_IFTYPE_STATION &&
	    mvmvif->ap_sta_id != IWL_MVM_INVALID_STA) {
		u8 sta_id = mvmvif->ap_sta_id;
		sta = rcu_dereference_protected(mvm->fw_id_to_mac_id[sta_id],
						lockdep_is_held(&mvm->mutex));
		return sta->addr;
	}


	return NULL;
}

static int __iwl_mvm_set_sta_key(struct iwl_mvm *mvm,
				 struct ieee80211_vif *vif,
				 struct ieee80211_sta *sta,
				 struct ieee80211_key_conf *keyconf,
				 u8 key_offset,
				 bool mcast)
{
	const u8 *addr;
	struct ieee80211_key_seq seq;
	u16 p1k[5];
	u32 sta_id;
	bool mfp = false;

	if (sta) {
		struct iwl_mvm_sta *mvm_sta = iwl_mvm_sta_from_mac80211(sta);

		sta_id = mvm_sta->sta_id;
		mfp = sta->mfp;
	} else if (vif->type == NL80211_IFTYPE_AP &&
		   !(keyconf->flags & IEEE80211_KEY_FLAG_PAIRWISE)) {
		struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);

		sta_id = mvmvif->mcast_sta.sta_id;
	} else {
		IWL_ERR(mvm, "Failed to find station id\n");
		return -EINVAL;
	}

	if (keyconf->cipher == WLAN_CIPHER_SUITE_TKIP) {
		addr = iwl_mvm_get_mac_addr(mvm, vif, sta);
		/* get phase 1 key from mac80211 */
		ieee80211_get_key_rx_seq(keyconf, 0, &seq);
		ieee80211_get_tkip_rx_p1k(keyconf, addr, seq.tkip.iv32, p1k);

		return iwl_mvm_send_sta_key(mvm, sta_id, keyconf, mcast,
					    seq.tkip.iv32, p1k, 0, key_offset,
					    mfp);
	}

	return iwl_mvm_send_sta_key(mvm, sta_id, keyconf, mcast,
				    0, NULL, 0, key_offset, mfp);
}

int iwl_mvm_set_sta_key(struct iwl_mvm *mvm,
			struct ieee80211_vif *vif,
			struct ieee80211_sta *sta,
			struct ieee80211_key_conf *keyconf,
			u8 key_offset)
{
	bool mcast = !(keyconf->flags & IEEE80211_KEY_FLAG_PAIRWISE);
	struct iwl_mvm_sta *mvm_sta;
	u8 sta_id = IWL_MVM_INVALID_STA;
	int ret;
	static const u8 __maybe_unused zero_addr[ETH_ALEN] = {0};

	lockdep_assert_held(&mvm->mutex);

	if (vif->type != NL80211_IFTYPE_AP ||
	    keyconf->flags & IEEE80211_KEY_FLAG_PAIRWISE) {
		/* Get the station id from the mvm local station table */
		mvm_sta = iwl_mvm_get_key_sta(mvm, vif, sta);
		if (!mvm_sta) {
			IWL_ERR(mvm, "Failed to find station\n");
			return -EINVAL;
		}
		sta_id = mvm_sta->sta_id;

		/*
		 * It is possible that the 'sta' parameter is NULL, and thus
		 * there is a need to retrieve the sta from the local station
		 * table.
		 */
		if (!sta) {
			sta = rcu_dereference_protected(
				mvm->fw_id_to_mac_id[sta_id],
				lockdep_is_held(&mvm->mutex));
			if (IS_ERR_OR_NULL(sta)) {
				IWL_ERR(mvm, "Invalid station id\n");
				return -EINVAL;
			}
		}

		if (WARN_ON_ONCE(iwl_mvm_sta_from_mac80211(sta)->vif != vif))
			return -EINVAL;
	} else {
		struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);

		sta_id = mvmvif->mcast_sta.sta_id;
	}

	if (keyconf->cipher == WLAN_CIPHER_SUITE_AES_CMAC ||
	    keyconf->cipher == WLAN_CIPHER_SUITE_BIP_GMAC_128 ||
	    keyconf->cipher == WLAN_CIPHER_SUITE_BIP_GMAC_256) {
		ret = iwl_mvm_send_sta_igtk(mvm, keyconf, sta_id, false);
		goto end;
	}

	/* If the key_offset is not pre-assigned, we need to find a
	 * new offset to use.  In normal cases, the offset is not
	 * pre-assigned, but during HW_RESTART we want to reuse the
	 * same indices, so we pass them when this function is called.
	 *
	 * In D3 entry, we need to hardcoded the indices (because the
	 * firmware hardcodes the PTK offset to 0).  In this case, we
	 * need to make sure we don't overwrite the hw_key_idx in the
	 * keyconf structure, because otherwise we cannot configure
	 * the original ones back when resuming.
	 */
	if (key_offset == STA_KEY_IDX_INVALID) {
		key_offset  = iwl_mvm_set_fw_key_idx(mvm);
		if (key_offset == STA_KEY_IDX_INVALID)
			return -ENOSPC;
		keyconf->hw_key_idx = key_offset;
	}

	ret = __iwl_mvm_set_sta_key(mvm, vif, sta, keyconf, key_offset, mcast);
	if (ret)
		goto end;

	/*
	 * For WEP, the same key is used for multicast and unicast. Upload it
	 * again, using the same key offset, and now pointing the other one
	 * to the same key slot (offset).
	 * If this fails, remove the original as well.
	 */
	if ((keyconf->cipher == WLAN_CIPHER_SUITE_WEP40 ||
	     keyconf->cipher == WLAN_CIPHER_SUITE_WEP104) &&
	    sta) {
		ret = __iwl_mvm_set_sta_key(mvm, vif, sta, keyconf,
					    key_offset, !mcast);
		if (ret) {
			__iwl_mvm_remove_sta_key(mvm, sta_id, keyconf, mcast);
			goto end;
		}
	}

	__set_bit(key_offset, mvm->fw_key_table);

end:
	IWL_DEBUG_WEP(mvm, "key: cipher=%x len=%d idx=%d sta=%pM ret=%d\n",
		      keyconf->cipher, keyconf->keylen, keyconf->keyidx,
		      sta ? sta->addr : zero_addr, ret);
	return ret;
}

int iwl_mvm_remove_sta_key(struct iwl_mvm *mvm,
			   struct ieee80211_vif *vif,
			   struct ieee80211_sta *sta,
			   struct ieee80211_key_conf *keyconf)
{
	bool mcast = !(keyconf->flags & IEEE80211_KEY_FLAG_PAIRWISE);
	struct iwl_mvm_sta *mvm_sta;
	u8 sta_id = IWL_MVM_INVALID_STA;
	int ret, i;

	lockdep_assert_held(&mvm->mutex);

	/* Get the station from the mvm local station table */
	mvm_sta = iwl_mvm_get_key_sta(mvm, vif, sta);
	if (mvm_sta)
		sta_id = mvm_sta->sta_id;
	else if (!sta && vif->type == NL80211_IFTYPE_AP && mcast)
		sta_id = iwl_mvm_vif_from_mac80211(vif)->mcast_sta.sta_id;


	IWL_DEBUG_WEP(mvm, "mvm remove dynamic key: idx=%d sta=%d\n",
		      keyconf->keyidx, sta_id);

	if (keyconf->cipher == WLAN_CIPHER_SUITE_AES_CMAC ||
	    keyconf->cipher == WLAN_CIPHER_SUITE_BIP_GMAC_128 ||
	    keyconf->cipher == WLAN_CIPHER_SUITE_BIP_GMAC_256)
		return iwl_mvm_send_sta_igtk(mvm, keyconf, sta_id, true);

	if (!__test_and_clear_bit(keyconf->hw_key_idx, mvm->fw_key_table)) {
		IWL_ERR(mvm, "offset %d not used in fw key table.\n",
			keyconf->hw_key_idx);
		return -ENOENT;
	}

	/* track which key was deleted last */
	for (i = 0; i < STA_KEY_MAX_NUM; i++) {
		if (mvm->fw_key_deleted[i] < U8_MAX)
			mvm->fw_key_deleted[i]++;
	}
	mvm->fw_key_deleted[keyconf->hw_key_idx] = 0;

	if (sta && !mvm_sta) {
		IWL_DEBUG_WEP(mvm, "station non-existent, early return.\n");
		return 0;
	}

	ret = __iwl_mvm_remove_sta_key(mvm, sta_id, keyconf, mcast);
	if (ret)
		return ret;

	/* delete WEP key twice to get rid of (now useless) offset */
	if (keyconf->cipher == WLAN_CIPHER_SUITE_WEP40 ||
	    keyconf->cipher == WLAN_CIPHER_SUITE_WEP104)
		ret = __iwl_mvm_remove_sta_key(mvm, sta_id, keyconf, !mcast);

	return ret;
}

void iwl_mvm_update_tkip_key(struct iwl_mvm *mvm,
			     struct ieee80211_vif *vif,
			     struct ieee80211_key_conf *keyconf,
			     struct ieee80211_sta *sta, u32 iv32,
			     u16 *phase1key)
{
	struct iwl_mvm_sta *mvm_sta;
	bool mcast = !(keyconf->flags & IEEE80211_KEY_FLAG_PAIRWISE);
	bool mfp = sta ? sta->mfp : false;

	rcu_read_lock();

	mvm_sta = iwl_mvm_get_key_sta(mvm, vif, sta);
	if (WARN_ON_ONCE(!mvm_sta))
		goto unlock;
	iwl_mvm_send_sta_key(mvm, mvm_sta->sta_id, keyconf, mcast,
			     iv32, phase1key, CMD_ASYNC, keyconf->hw_key_idx,
			     mfp);

 unlock:
	rcu_read_unlock();
}

void iwl_mvm_sta_modify_ps_wake(struct iwl_mvm *mvm,
				struct ieee80211_sta *sta)
{
	struct iwl_mvm_sta *mvmsta = iwl_mvm_sta_from_mac80211(sta);
	struct iwl_mvm_add_sta_cmd cmd = {
		.add_modify = STA_MODE_MODIFY,
		.sta_id = mvmsta->sta_id,
		.station_flags_msk = cpu_to_le32(STA_FLG_PS),
		.mac_id_n_color = cpu_to_le32(mvmsta->mac_id_n_color),
	};
	int ret;

	ret = iwl_mvm_send_cmd_pdu(mvm, ADD_STA, CMD_ASYNC,
				   iwl_mvm_add_sta_cmd_size(mvm), &cmd);
	if (ret)
		IWL_ERR(mvm, "Failed to send ADD_STA command (%d)\n", ret);
}

void iwl_mvm_sta_modify_sleep_tx_count(struct iwl_mvm *mvm,
				       struct ieee80211_sta *sta,
				       enum ieee80211_frame_release_type reason,
				       u16 cnt, u16 tids, bool more_data,
				       bool single_sta_queue)
{
	struct iwl_mvm_sta *mvmsta = iwl_mvm_sta_from_mac80211(sta);
	struct iwl_mvm_add_sta_cmd cmd = {
		.add_modify = STA_MODE_MODIFY,
		.sta_id = mvmsta->sta_id,
		.modify_mask = STA_MODIFY_SLEEPING_STA_TX_COUNT,
		.sleep_tx_count = cpu_to_le16(cnt),
		.mac_id_n_color = cpu_to_le32(mvmsta->mac_id_n_color),
	};
	int tid, ret;
	unsigned long _tids = tids;

	/* convert TIDs to ACs - we don't support TSPEC so that's OK
	 * Note that this field is reserved and unused by firmware not
	 * supporting GO uAPSD, so it's safe to always do this.
	 */
	for_each_set_bit(tid, &_tids, IWL_MAX_TID_COUNT)
		cmd.awake_acs |= BIT(tid_to_ucode_ac[tid]);

	/* If we're releasing frames from aggregation or dqa queues then check
	 * if all the queues that we're releasing frames from, combined, have:
	 *  - more frames than the service period, in which case more_data
	 *    needs to be set
	 *  - fewer than 'cnt' frames, in which case we need to adjust the
	 *    firmware command (but do that unconditionally)
	 */
	if (single_sta_queue) {
		int remaining = cnt;
		int sleep_tx_count;

		spin_lock_bh(&mvmsta->lock);
		for_each_set_bit(tid, &_tids, IWL_MAX_TID_COUNT) {
			struct iwl_mvm_tid_data *tid_data;
			u16 n_queued;

			tid_data = &mvmsta->tid_data[tid];

			n_queued = iwl_mvm_tid_queued(mvm, tid_data);
			if (n_queued > remaining) {
				more_data = true;
				remaining = 0;
				break;
			}
			remaining -= n_queued;
		}
		sleep_tx_count = cnt - remaining;
		if (reason == IEEE80211_FRAME_RELEASE_UAPSD)
			mvmsta->sleep_tx_count = sleep_tx_count;
		spin_unlock_bh(&mvmsta->lock);

		cmd.sleep_tx_count = cpu_to_le16(sleep_tx_count);
		if (WARN_ON(cnt - remaining == 0)) {
			ieee80211_sta_eosp(sta);
			return;
		}
	}

	/* Note: this is ignored by firmware not supporting GO uAPSD */
	if (more_data)
		cmd.sleep_state_flags |= STA_SLEEP_STATE_MOREDATA;

	if (reason == IEEE80211_FRAME_RELEASE_PSPOLL) {
		mvmsta->next_status_eosp = true;
		cmd.sleep_state_flags |= STA_SLEEP_STATE_PS_POLL;
	} else {
		cmd.sleep_state_flags |= STA_SLEEP_STATE_UAPSD;
	}

	/* block the Tx queues until the FW updated the sleep Tx count */
	iwl_trans_block_txq_ptrs(mvm->trans, true);

	ret = iwl_mvm_send_cmd_pdu(mvm, ADD_STA,
				   CMD_ASYNC | CMD_WANT_ASYNC_CALLBACK,
				   iwl_mvm_add_sta_cmd_size(mvm), &cmd);
	if (ret)
		IWL_ERR(mvm, "Failed to send ADD_STA command (%d)\n", ret);
}

void iwl_mvm_rx_eosp_notif(struct iwl_mvm *mvm,
			   struct iwl_rx_cmd_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_mvm_eosp_notification *notif = (void *)pkt->data;
	struct ieee80211_sta *sta;
	u32 sta_id = le32_to_cpu(notif->sta_id);

	if (WARN_ON_ONCE(sta_id >= mvm->fw->ucode_capa.num_stations))
		return;

	rcu_read_lock();
	sta = rcu_dereference(mvm->fw_id_to_mac_id[sta_id]);
	if (!IS_ERR_OR_NULL(sta))
		ieee80211_sta_eosp(sta);
	rcu_read_unlock();
}

void iwl_mvm_sta_modify_disable_tx(struct iwl_mvm *mvm,
				   struct iwl_mvm_sta *mvmsta, bool disable)
{
	struct iwl_mvm_add_sta_cmd cmd = {
		.add_modify = STA_MODE_MODIFY,
		.sta_id = mvmsta->sta_id,
		.station_flags = disable ? cpu_to_le32(STA_FLG_DISABLE_TX) : 0,
		.station_flags_msk = cpu_to_le32(STA_FLG_DISABLE_TX),
		.mac_id_n_color = cpu_to_le32(mvmsta->mac_id_n_color),
	};
	int ret;

	ret = iwl_mvm_send_cmd_pdu(mvm, ADD_STA, CMD_ASYNC,
				   iwl_mvm_add_sta_cmd_size(mvm), &cmd);
	if (ret)
		IWL_ERR(mvm, "Failed to send ADD_STA command (%d)\n", ret);
}

void iwl_mvm_sta_modify_disable_tx_ap(struct iwl_mvm *mvm,
				      struct ieee80211_sta *sta,
				      bool disable)
{
	struct iwl_mvm_sta *mvm_sta = iwl_mvm_sta_from_mac80211(sta);

	spin_lock_bh(&mvm_sta->lock);

	if (mvm_sta->disable_tx == disable) {
		spin_unlock_bh(&mvm_sta->lock);
		return;
	}

	mvm_sta->disable_tx = disable;

	/*
	 * If sta PS state is handled by mac80211, tell it to start/stop
	 * queuing tx for this station.
	 */
	if (!ieee80211_hw_check(mvm->hw, AP_LINK_PS))
		ieee80211_sta_block_awake(mvm->hw, sta, disable);

	iwl_mvm_sta_modify_disable_tx(mvm, mvm_sta, disable);

	spin_unlock_bh(&mvm_sta->lock);
}

static void iwl_mvm_int_sta_modify_disable_tx(struct iwl_mvm *mvm,
					      struct iwl_mvm_vif *mvmvif,
					      struct iwl_mvm_int_sta *sta,
					      bool disable)
{
	u32 id = FW_CMD_ID_AND_COLOR(mvmvif->id, mvmvif->color);
	struct iwl_mvm_add_sta_cmd cmd = {
		.add_modify = STA_MODE_MODIFY,
		.sta_id = sta->sta_id,
		.station_flags = disable ? cpu_to_le32(STA_FLG_DISABLE_TX) : 0,
		.station_flags_msk = cpu_to_le32(STA_FLG_DISABLE_TX),
		.mac_id_n_color = cpu_to_le32(id),
	};
	int ret;

	ret = iwl_mvm_send_cmd_pdu(mvm, ADD_STA, CMD_ASYNC,
				   iwl_mvm_add_sta_cmd_size(mvm), &cmd);
	if (ret)
		IWL_ERR(mvm, "Failed to send ADD_STA command (%d)\n", ret);
}

void iwl_mvm_modify_all_sta_disable_tx(struct iwl_mvm *mvm,
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

		iwl_mvm_sta_modify_disable_tx_ap(mvm, sta, disable);
	}

	rcu_read_unlock();

	if (!fw_has_api(&mvm->fw->ucode_capa, IWL_UCODE_TLV_API_STA_TYPE))
		return;

	/* Need to block/unblock also multicast station */
	if (mvmvif->mcast_sta.sta_id != IWL_MVM_INVALID_STA)
		iwl_mvm_int_sta_modify_disable_tx(mvm, mvmvif,
						  &mvmvif->mcast_sta, disable);

	/*
	 * Only unblock the broadcast station (FW blocks it for immediate
	 * quiet, not the driver)
	 */
	if (!disable && mvmvif->bcast_sta.sta_id != IWL_MVM_INVALID_STA)
		iwl_mvm_int_sta_modify_disable_tx(mvm, mvmvif,
						  &mvmvif->bcast_sta, disable);
}

void iwl_mvm_csa_client_absent(struct iwl_mvm *mvm, struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm_sta *mvmsta;

	rcu_read_lock();

	mvmsta = iwl_mvm_sta_from_staid_rcu(mvm, mvmvif->ap_sta_id);

	if (mvmsta)
		iwl_mvm_sta_modify_disable_tx(mvm, mvmsta, true);

	rcu_read_unlock();
}

u16 iwl_mvm_tid_queued(struct iwl_mvm *mvm, struct iwl_mvm_tid_data *tid_data)
{
	u16 sn = IEEE80211_SEQ_TO_SN(tid_data->seq_number);

	/*
	 * In 22000 HW, the next_reclaimed index is only 8 bit, so we'll need
	 * to align the wrap around of ssn so we compare relevant values.
	 */
	if (mvm->trans->trans_cfg->gen2)
		sn &= 0xff;

	return ieee80211_sn_sub(sn, tid_data->next_reclaimed);
}

int iwl_mvm_add_pasn_sta(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			 struct iwl_mvm_int_sta *sta, u8 *addr, u32 cipher,
			 u8 *key, u32 key_len)
{
	int ret;
	u16 queue;
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct ieee80211_key_conf *keyconf;

	ret = iwl_mvm_allocate_int_sta(mvm, sta, 0,
				       NL80211_IFTYPE_UNSPECIFIED,
				       IWL_STA_LINK);
	if (ret)
		return ret;

	ret = iwl_mvm_add_int_sta_with_queue(mvm, mvmvif->id, mvmvif->color,
					     addr, sta, &queue,
					     IWL_MVM_TX_FIFO_BE);
	if (ret)
		goto out;

	keyconf = kzalloc(sizeof(*keyconf) + key_len, GFP_KERNEL);
	if (!keyconf) {
		ret = -ENOBUFS;
		goto out;
	}

	keyconf->cipher = cipher;
	memcpy(keyconf->key, key, key_len);
	keyconf->keylen = key_len;

	ret = iwl_mvm_send_sta_key(mvm, sta->sta_id, keyconf, false,
				   0, NULL, 0, 0, true);
	kfree(keyconf);
	return 0;
out:
	iwl_mvm_dealloc_int_sta(mvm, sta);
	return ret;
}

void iwl_mvm_cancel_channel_switch(struct iwl_mvm *mvm,
				   struct ieee80211_vif *vif,
				   u32 mac_id)
{
	struct iwl_cancel_channel_switch_cmd cancel_channel_switch_cmd = {
		.mac_id = cpu_to_le32(mac_id),
	};
	int ret;

	ret = iwl_mvm_send_cmd_pdu(mvm,
				   WIDE_ID(MAC_CONF_GROUP, CANCEL_CHANNEL_SWITCH_CMD),
				   CMD_ASYNC,
				   sizeof(cancel_channel_switch_cmd),
				   &cancel_channel_switch_cmd);
	if (ret)
		IWL_ERR(mvm, "Failed to cancel the channel switch\n");
}
