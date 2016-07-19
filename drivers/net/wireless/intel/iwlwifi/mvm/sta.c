/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2012 - 2015 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016 Intel Deutschland GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110,
 * USA
 *
 * The full GNU General Public License is included in this distribution
 * in the file called COPYING.
 *
 * Contact Information:
 *  Intel Linux Wireless <linuxwifi@intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2012 - 2015 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016 Intel Deutschland GmbH
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************/
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
	return iwl_mvm_has_new_rx_api(mvm) ?
		sizeof(struct iwl_mvm_add_sta_cmd) :
		sizeof(struct iwl_mvm_add_sta_cmd_v7);
}

static int iwl_mvm_find_free_sta_id(struct iwl_mvm *mvm,
				    enum nl80211_iftype iftype)
{
	int sta_id;
	u32 reserved_ids = 0;

	BUILD_BUG_ON(IWL_MVM_STATION_COUNT > 32);
	WARN_ON_ONCE(test_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status));

	lockdep_assert_held(&mvm->mutex);

	/* d0i3/d3 assumes the AP's sta_id (of sta vif) is 0. reserve it. */
	if (iftype != NL80211_IFTYPE_STATION)
		reserved_ids = BIT(0);

	/* Don't take rcu_read_lock() since we are protected by mvm->mutex */
	for (sta_id = 0; sta_id < IWL_MVM_STATION_COUNT; sta_id++) {
		if (BIT(sta_id) & reserved_ids)
			continue;

		if (!rcu_dereference_protected(mvm->fw_id_to_mac_id[sta_id],
					       lockdep_is_held(&mvm->mutex)))
			return sta_id;
	}
	return IWL_MVM_STATION_COUNT;
}

/* send station add/update command to firmware */
int iwl_mvm_sta_send_to_fw(struct iwl_mvm *mvm, struct ieee80211_sta *sta,
			   bool update)
{
	struct iwl_mvm_sta *mvm_sta = iwl_mvm_sta_from_mac80211(sta);
	struct iwl_mvm_add_sta_cmd add_sta_cmd = {
		.sta_id = mvm_sta->sta_id,
		.mac_id_n_color = cpu_to_le32(mvm_sta->mac_id_n_color),
		.add_modify = update ? 1 : 0,
		.station_flags_msk = cpu_to_le32(STA_FLG_FAT_EN_MSK |
						 STA_FLG_MIMO_EN_MSK),
		.tid_disable_tx = cpu_to_le16(mvm_sta->tid_disable_agg),
	};
	int ret;
	u32 status;
	u32 agg_size = 0, mpdu_dens = 0;

	if (!update) {
		add_sta_cmd.tfd_queue_msk = cpu_to_le32(mvm_sta->tfd_queue_msk);
		memcpy(&add_sta_cmd.addr, sta->addr, ETH_ALEN);
	}

	switch (sta->bandwidth) {
	case IEEE80211_STA_RX_BW_160:
		add_sta_cmd.station_flags |= cpu_to_le32(STA_FLG_FAT_EN_160MHZ);
		/* fall through */
	case IEEE80211_STA_RX_BW_80:
		add_sta_cmd.station_flags |= cpu_to_le32(STA_FLG_FAT_EN_80MHZ);
		/* fall through */
	case IEEE80211_STA_RX_BW_40:
		add_sta_cmd.station_flags |= cpu_to_le32(STA_FLG_FAT_EN_40MHZ);
		/* fall through */
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

	if (sta->vht_cap.vht_supported) {
		agg_size = sta->vht_cap.cap &
			IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK;
		agg_size >>=
			IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_SHIFT;
	} else if (sta->ht_cap.ht_supported) {
		agg_size = sta->ht_cap.ampdu_factor;
	}

	add_sta_cmd.station_flags |=
		cpu_to_le32(agg_size << STA_FLG_MAX_AGG_SIZE_SHIFT);
	add_sta_cmd.station_flags |=
		cpu_to_le32(mpdu_dens << STA_FLG_AGG_MPDU_DENS_SHIFT);

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

static int iwl_mvm_tdls_sta_init(struct iwl_mvm *mvm,
				 struct ieee80211_sta *sta)
{
	unsigned long used_hw_queues;
	struct iwl_mvm_sta *mvmsta = iwl_mvm_sta_from_mac80211(sta);
	unsigned int wdg_timeout =
		iwl_mvm_get_wd_timeout(mvm, NULL, true, false);
	u32 ac;

	lockdep_assert_held(&mvm->mutex);

	used_hw_queues = iwl_mvm_get_used_hw_queues(mvm, NULL);

	/* Find available queues, and allocate them to the ACs */
	for (ac = 0; ac < IEEE80211_NUM_ACS; ac++) {
		u8 queue = find_first_zero_bit(&used_hw_queues,
					       mvm->first_agg_queue);

		if (queue >= mvm->first_agg_queue) {
			IWL_ERR(mvm, "Failed to allocate STA queue\n");
			return -EBUSY;
		}

		__set_bit(queue, &used_hw_queues);
		mvmsta->hw_queue[ac] = queue;
	}

	/* Found a place for all queues - enable them */
	for (ac = 0; ac < IEEE80211_NUM_ACS; ac++) {
		iwl_mvm_enable_ac_txq(mvm, mvmsta->hw_queue[ac],
				      mvmsta->hw_queue[ac],
				      iwl_mvm_ac_to_tx_fifo[ac], 0,
				      wdg_timeout);
		mvmsta->tfd_queue_msk |= BIT(mvmsta->hw_queue[ac]);
	}

	return 0;
}

static void iwl_mvm_tdls_sta_deinit(struct iwl_mvm *mvm,
				    struct ieee80211_sta *sta)
{
	struct iwl_mvm_sta *mvmsta = iwl_mvm_sta_from_mac80211(sta);
	unsigned long sta_msk;
	int i;

	lockdep_assert_held(&mvm->mutex);

	/* disable the TDLS STA-specific queues */
	sta_msk = mvmsta->tfd_queue_msk;
	for_each_set_bit(i, &sta_msk, sizeof(sta_msk) * BITS_PER_BYTE)
		iwl_mvm_disable_txq(mvm, i, i, IWL_MAX_TID_COUNT, 0);
}

int iwl_mvm_add_sta(struct iwl_mvm *mvm,
		    struct ieee80211_vif *vif,
		    struct ieee80211_sta *sta)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm_sta *mvm_sta = iwl_mvm_sta_from_mac80211(sta);
	struct iwl_mvm_rxq_dup_data *dup_data;
	int i, ret, sta_id;

	lockdep_assert_held(&mvm->mutex);

	if (!test_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status))
		sta_id = iwl_mvm_find_free_sta_id(mvm,
						  ieee80211_vif_type_p2p(vif));
	else
		sta_id = mvm_sta->sta_id;

	if (sta_id == IWL_MVM_STATION_COUNT)
		return -ENOSPC;

	spin_lock_init(&mvm_sta->lock);

	mvm_sta->sta_id = sta_id;
	mvm_sta->mac_id_n_color = FW_CMD_ID_AND_COLOR(mvmvif->id,
						      mvmvif->color);
	mvm_sta->vif = vif;
	mvm_sta->max_agg_bufsize = LINK_QUAL_AGG_FRAME_LIMIT_DEF;
	mvm_sta->tx_protection = 0;
	mvm_sta->tt_tx_protection = false;

	/* HW restart, don't assume the memory has been zeroed */
	atomic_set(&mvm->pending_frames[sta_id], 0);
	mvm_sta->tid_disable_agg = 0xffff; /* No aggs at first */
	mvm_sta->tfd_queue_msk = 0;

	/* allocate new queues for a TDLS station */
	if (sta->tdls) {
		ret = iwl_mvm_tdls_sta_init(mvm, sta);
		if (ret)
			return ret;
	} else {
		for (i = 0; i < IEEE80211_NUM_ACS; i++)
			if (vif->hw_queue[i] != IEEE80211_INVAL_HW_QUEUE)
				mvm_sta->tfd_queue_msk |= BIT(vif->hw_queue[i]);
	}

	/* for HW restart - reset everything but the sequence number */
	for (i = 0; i < IWL_MAX_TID_COUNT; i++) {
		u16 seq = mvm_sta->tid_data[i].seq_number;
		memset(&mvm_sta->tid_data[i], 0, sizeof(mvm_sta->tid_data[i]));
		mvm_sta->tid_data[i].seq_number = seq;
	}
	mvm_sta->agg_tids = 0;

	if (iwl_mvm_has_new_rx_api(mvm) &&
	    !test_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status)) {
		dup_data = kcalloc(mvm->trans->num_rx_queues,
				   sizeof(*dup_data),
				   GFP_KERNEL);
		if (!dup_data)
			return -ENOMEM;
		mvm_sta->dup_data = dup_data;
	}

	ret = iwl_mvm_sta_send_to_fw(mvm, sta, false);
	if (ret)
		goto err;

	if (vif->type == NL80211_IFTYPE_STATION) {
		if (!sta->tdls) {
			WARN_ON(mvmvif->ap_sta_id != IWL_MVM_STATION_COUNT);
			mvmvif->ap_sta_id = sta_id;
		} else {
			WARN_ON(mvmvif->ap_sta_id == IWL_MVM_STATION_COUNT);
		}
	}

	rcu_assign_pointer(mvm->fw_id_to_mac_id[sta_id], sta);

	return 0;

err:
	iwl_mvm_tdls_sta_deinit(mvm, sta);
	return ret;
}

int iwl_mvm_update_sta(struct iwl_mvm *mvm,
		       struct ieee80211_vif *vif,
		       struct ieee80211_sta *sta)
{
	return iwl_mvm_sta_send_to_fw(mvm, sta, true);
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

void iwl_mvm_sta_drained_wk(struct work_struct *wk)
{
	struct iwl_mvm *mvm = container_of(wk, struct iwl_mvm, sta_drained_wk);
	u8 sta_id;

	/*
	 * The mutex is needed because of the SYNC cmd, but not only: if the
	 * work would run concurrently with iwl_mvm_rm_sta, it would run before
	 * iwl_mvm_rm_sta sets the station as busy, and exit. Then
	 * iwl_mvm_rm_sta would set the station as busy, and nobody will clean
	 * that later.
	 */
	mutex_lock(&mvm->mutex);

	for_each_set_bit(sta_id, mvm->sta_drained, IWL_MVM_STATION_COUNT) {
		int ret;
		struct ieee80211_sta *sta =
			rcu_dereference_protected(mvm->fw_id_to_mac_id[sta_id],
						  lockdep_is_held(&mvm->mutex));

		/*
		 * This station is in use or RCU-removed; the latter happens in
		 * managed mode, where mac80211 removes the station before we
		 * can remove it from firmware (we can only do that after the
		 * MAC is marked unassociated), and possibly while the deauth
		 * frame to disconnect from the AP is still queued. Then, the
		 * station pointer is -ENOENT when the last skb is reclaimed.
		 */
		if (!IS_ERR(sta) || PTR_ERR(sta) == -ENOENT)
			continue;

		if (PTR_ERR(sta) == -EINVAL) {
			IWL_ERR(mvm, "Drained sta %d, but it is internal?\n",
				sta_id);
			continue;
		}

		if (!sta) {
			IWL_ERR(mvm, "Drained sta %d, but it was NULL?\n",
				sta_id);
			continue;
		}

		WARN_ON(PTR_ERR(sta) != -EBUSY);
		/* This station was removed and we waited until it got drained,
		 * we can now proceed and remove it.
		 */
		ret = iwl_mvm_rm_sta_common(mvm, sta_id);
		if (ret) {
			IWL_ERR(mvm,
				"Couldn't remove sta %d after it was drained\n",
				sta_id);
			continue;
		}
		RCU_INIT_POINTER(mvm->fw_id_to_mac_id[sta_id], NULL);
		clear_bit(sta_id, mvm->sta_drained);

		if (mvm->tfd_drained[sta_id]) {
			unsigned long i, msk = mvm->tfd_drained[sta_id];

			for_each_set_bit(i, &msk, sizeof(msk) * BITS_PER_BYTE)
				iwl_mvm_disable_txq(mvm, i, i,
						    IWL_MAX_TID_COUNT, 0);

			mvm->tfd_drained[sta_id] = 0;
			IWL_DEBUG_TDLS(mvm, "Drained sta %d, with queues %ld\n",
				       sta_id, msk);
		}
	}

	mutex_unlock(&mvm->mutex);
}

int iwl_mvm_rm_sta(struct iwl_mvm *mvm,
		   struct ieee80211_vif *vif,
		   struct ieee80211_sta *sta)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm_sta *mvm_sta = iwl_mvm_sta_from_mac80211(sta);
	int ret;

	lockdep_assert_held(&mvm->mutex);

	if (iwl_mvm_has_new_rx_api(mvm))
		kfree(mvm_sta->dup_data);

	if (vif->type == NL80211_IFTYPE_STATION &&
	    mvmvif->ap_sta_id == mvm_sta->sta_id) {
		ret = iwl_mvm_drain_sta(mvm, mvm_sta, true);
		if (ret)
			return ret;
		/* flush its queues here since we are freeing mvm_sta */
		ret = iwl_mvm_flush_tx_path(mvm, mvm_sta->tfd_queue_msk, 0);
		if (ret)
			return ret;
		ret = iwl_trans_wait_tx_queue_empty(mvm->trans,
						    mvm_sta->tfd_queue_msk);
		if (ret)
			return ret;
		ret = iwl_mvm_drain_sta(mvm, mvm_sta, false);

		/* if we are associated - we can't remove the AP STA now */
		if (vif->bss_conf.assoc)
			return ret;

		/* unassoc - go ahead - remove the AP STA now */
		mvmvif->ap_sta_id = IWL_MVM_STATION_COUNT;

		/* clear d0i3_ap_sta_id if no longer relevant */
		if (mvm->d0i3_ap_sta_id == mvm_sta->sta_id)
			mvm->d0i3_ap_sta_id = IWL_MVM_STATION_COUNT;
	}

	/*
	 * This shouldn't happen - the TDLS channel switch should be canceled
	 * before the STA is removed.
	 */
	if (WARN_ON_ONCE(mvm->tdls_cs.peer.sta_id == mvm_sta->sta_id)) {
		mvm->tdls_cs.peer.sta_id = IWL_MVM_STATION_COUNT;
		cancel_delayed_work(&mvm->tdls_cs.dwork);
	}

	/*
	 * Make sure that the tx response code sees the station as -EBUSY and
	 * calls the drain worker.
	 */
	spin_lock_bh(&mvm_sta->lock);
	/*
	 * There are frames pending on the AC queues for this station.
	 * We need to wait until all the frames are drained...
	 */
	if (atomic_read(&mvm->pending_frames[mvm_sta->sta_id])) {
		rcu_assign_pointer(mvm->fw_id_to_mac_id[mvm_sta->sta_id],
				   ERR_PTR(-EBUSY));
		spin_unlock_bh(&mvm_sta->lock);

		/* disable TDLS sta queues on drain complete */
		if (sta->tdls) {
			mvm->tfd_drained[mvm_sta->sta_id] =
							mvm_sta->tfd_queue_msk;
			IWL_DEBUG_TDLS(mvm, "Draining TDLS sta %d\n",
				       mvm_sta->sta_id);
		}

		ret = iwl_mvm_drain_sta(mvm, mvm_sta, true);
	} else {
		spin_unlock_bh(&mvm_sta->lock);

		if (sta->tdls)
			iwl_mvm_tdls_sta_deinit(mvm, sta);

		ret = iwl_mvm_rm_sta_common(mvm, mvm_sta->sta_id);
		RCU_INIT_POINTER(mvm->fw_id_to_mac_id[mvm_sta->sta_id], NULL);
	}

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
			     u32 qmask, enum nl80211_iftype iftype)
{
	if (!test_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status)) {
		sta->sta_id = iwl_mvm_find_free_sta_id(mvm, iftype);
		if (WARN_ON_ONCE(sta->sta_id == IWL_MVM_STATION_COUNT))
			return -ENOSPC;
	}

	sta->tfd_queue_msk = qmask;

	/* put a non-NULL value so iterating over the stations won't stop */
	rcu_assign_pointer(mvm->fw_id_to_mac_id[sta->sta_id], ERR_PTR(-EINVAL));
	return 0;
}

static void iwl_mvm_dealloc_int_sta(struct iwl_mvm *mvm,
				    struct iwl_mvm_int_sta *sta)
{
	RCU_INIT_POINTER(mvm->fw_id_to_mac_id[sta->sta_id], NULL);
	memset(sta, 0, sizeof(struct iwl_mvm_int_sta));
	sta->sta_id = IWL_MVM_STATION_COUNT;
}

static int iwl_mvm_add_int_sta_common(struct iwl_mvm *mvm,
				      struct iwl_mvm_int_sta *sta,
				      const u8 *addr,
				      u16 mac_id, u16 color)
{
	struct iwl_mvm_add_sta_cmd cmd;
	int ret;
	u32 status;

	lockdep_assert_held(&mvm->mutex);

	memset(&cmd, 0, sizeof(cmd));
	cmd.sta_id = sta->sta_id;
	cmd.mac_id_n_color = cpu_to_le32(FW_CMD_ID_AND_COLOR(mac_id,
							     color));

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

int iwl_mvm_add_aux_sta(struct iwl_mvm *mvm)
{
	unsigned int wdg_timeout = iwlmvm_mod_params.tfd_q_hang_detect ?
					mvm->cfg->base_params->wd_timeout :
					IWL_WATCHDOG_DISABLED;
	int ret;

	lockdep_assert_held(&mvm->mutex);

	/* Map Aux queue to fifo - needs to happen before adding Aux station */
	iwl_mvm_enable_ac_txq(mvm, mvm->aux_queue, mvm->aux_queue,
			      IWL_MVM_TX_FIFO_MCAST, 0, wdg_timeout);

	/* Allocate aux station and assign to it the aux queue */
	ret = iwl_mvm_allocate_int_sta(mvm, &mvm->aux_sta, BIT(mvm->aux_queue),
				       NL80211_IFTYPE_UNSPECIFIED);
	if (ret)
		return ret;

	ret = iwl_mvm_add_int_sta_common(mvm, &mvm->aux_sta, NULL,
					 MAC_INDEX_AUX, 0);

	if (ret)
		iwl_mvm_dealloc_int_sta(mvm, &mvm->aux_sta);
	return ret;
}

int iwl_mvm_add_snif_sta(struct iwl_mvm *mvm, struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);

	lockdep_assert_held(&mvm->mutex);
	return iwl_mvm_add_int_sta_common(mvm, &mvm->snif_sta, vif->addr,
					 mvmvif->id, 0);
}

int iwl_mvm_rm_snif_sta(struct iwl_mvm *mvm, struct ieee80211_vif *vif)
{
	int ret;

	lockdep_assert_held(&mvm->mutex);

	ret = iwl_mvm_rm_sta_common(mvm, mvm->snif_sta.sta_id);
	if (ret)
		IWL_WARN(mvm, "Failed sending remove station\n");

	return ret;
}

void iwl_mvm_dealloc_snif_sta(struct iwl_mvm *mvm)
{
	iwl_mvm_dealloc_int_sta(mvm, &mvm->snif_sta);
}

void iwl_mvm_del_aux_sta(struct iwl_mvm *mvm)
{
	lockdep_assert_held(&mvm->mutex);

	iwl_mvm_dealloc_int_sta(mvm, &mvm->aux_sta);
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

	lockdep_assert_held(&mvm->mutex);

	if (vif->type == NL80211_IFTYPE_ADHOC)
		baddr = vif->bss_conf.bssid;

	if (WARN_ON_ONCE(bsta->sta_id == IWL_MVM_STATION_COUNT))
		return -ENOSPC;

	return iwl_mvm_add_int_sta_common(mvm, bsta, baddr,
					  mvmvif->id, mvmvif->color);
}

/* Send the FW a request to remove the station from it's internal data
 * structures, but DO NOT remove the entry from the local data structures. */
int iwl_mvm_send_rm_bcast_sta(struct iwl_mvm *mvm, struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	int ret;

	lockdep_assert_held(&mvm->mutex);

	ret = iwl_mvm_rm_sta_common(mvm, mvmvif->bcast_sta.sta_id);
	if (ret)
		IWL_WARN(mvm, "Failed sending remove station\n");
	return ret;
}

int iwl_mvm_alloc_bcast_sta(struct iwl_mvm *mvm, struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	u32 qmask;

	lockdep_assert_held(&mvm->mutex);

	qmask = iwl_mvm_mac_get_queues_mask(vif);

	/*
	 * The firmware defines the TFD queue mask to only be relevant
	 * for *unicast* queues, so the multicast (CAB) queue shouldn't
	 * be included.
	 */
	if (vif->type == NL80211_IFTYPE_AP)
		qmask &= ~BIT(vif->cab_queue);

	return iwl_mvm_allocate_int_sta(mvm, &mvmvif->bcast_sta, qmask,
					ieee80211_vif_type_p2p(vif));
}

/* Allocate a new station entry for the broadcast station to the given vif,
 * and send it to the FW.
 * Note that each P2P mac should have its own broadcast station.
 *
 * @mvm: the mvm component
 * @vif: the interface to which the broadcast station is added
 * @bsta: the broadcast station to add. */
int iwl_mvm_add_bcast_sta(struct iwl_mvm *mvm, struct ieee80211_vif *vif)
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
int iwl_mvm_rm_bcast_sta(struct iwl_mvm *mvm, struct ieee80211_vif *vif)
{
	int ret;

	lockdep_assert_held(&mvm->mutex);

	ret = iwl_mvm_send_rm_bcast_sta(mvm, vif);

	iwl_mvm_dealloc_bcast_sta(mvm, vif);

	return ret;
}

#define IWL_MAX_RX_BA_SESSIONS 16

int iwl_mvm_sta_rx_agg(struct iwl_mvm *mvm, struct ieee80211_sta *sta,
		       int tid, u16 ssn, bool start, u8 buf_size)
{
	struct iwl_mvm_sta *mvm_sta = iwl_mvm_sta_from_mac80211(sta);
	struct iwl_mvm_add_sta_cmd cmd = {};
	int ret;
	u32 status;

	lockdep_assert_held(&mvm->mutex);

	if (start && mvm->rx_ba_sessions >= IWL_MAX_RX_BA_SESSIONS) {
		IWL_WARN(mvm, "Not enough RX BA SESSIONS\n");
		return -ENOSPC;
	}

	cmd.mac_id_n_color = cpu_to_le32(mvm_sta->mac_id_n_color);
	cmd.sta_id = mvm_sta->sta_id;
	cmd.add_modify = STA_MODE_MODIFY;
	if (start) {
		cmd.add_immediate_ba_tid = (u8) tid;
		cmd.add_immediate_ba_ssn = cpu_to_le16(ssn);
		cmd.rx_ba_window = cpu_to_le16((u16)buf_size);
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
		return ret;

	switch (status & IWL_ADD_STA_STATUS_MASK) {
	case ADD_STA_SUCCESS:
		IWL_DEBUG_INFO(mvm, "RX BA Session %sed in fw\n",
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

	if (!ret) {
		if (start)
			mvm->rx_ba_sessions++;
		else if (mvm->rx_ba_sessions > 0)
			/* check that restart flow didn't zero the counter */
			mvm->rx_ba_sessions--;
	}

	return ret;
}

static int iwl_mvm_sta_tx_agg(struct iwl_mvm *mvm, struct ieee80211_sta *sta,
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
		mvm_sta->tfd_queue_msk &= ~BIT(queue);
		mvm_sta->tid_disable_agg |= BIT(tid);
	}

	cmd.mac_id_n_color = cpu_to_le32(mvm_sta->mac_id_n_color);
	cmd.sta_id = mvm_sta->sta_id;
	cmd.add_modify = STA_MODE_MODIFY;
	cmd.modify_mask = STA_MODIFY_QUEUES | STA_MODIFY_TID_DISABLE_TX;
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
	int txq_id;
	int ret;

	if (WARN_ON_ONCE(tid >= IWL_MAX_TID_COUNT))
		return -EINVAL;

	if (mvmsta->tid_data[tid].state != IWL_AGG_OFF) {
		IWL_ERR(mvm, "Start AGG when state is not IWL_AGG_OFF %d!\n",
			mvmsta->tid_data[tid].state);
		return -ENXIO;
	}

	lockdep_assert_held(&mvm->mutex);

	spin_lock_bh(&mvmsta->lock);

	/* possible race condition - we entered D0i3 while starting agg */
	if (test_bit(IWL_MVM_STATUS_IN_D0I3, &mvm->status)) {
		spin_unlock_bh(&mvmsta->lock);
		IWL_ERR(mvm, "Entered D0i3 while starting Tx agg\n");
		return -EIO;
	}

	spin_lock_bh(&mvm->queue_info_lock);

	txq_id = iwl_mvm_find_free_queue(mvm, mvm->first_agg_queue,
					 mvm->last_agg_queue);
	if (txq_id < 0) {
		ret = txq_id;
		spin_unlock_bh(&mvm->queue_info_lock);
		IWL_ERR(mvm, "Failed to allocate agg queue\n");
		goto release_locks;
	}
	mvm->queue_info[txq_id].setup_reserved = true;
	spin_unlock_bh(&mvm->queue_info_lock);

	tid_data = &mvmsta->tid_data[tid];
	tid_data->ssn = IEEE80211_SEQ_TO_SN(tid_data->seq_number);
	tid_data->txq_id = txq_id;
	*ssn = tid_data->ssn;

	IWL_DEBUG_TX_QUEUES(mvm,
			    "Start AGG: sta %d tid %d queue %d - ssn = %d, next_recl = %d\n",
			    mvmsta->sta_id, tid, txq_id, tid_data->ssn,
			    tid_data->next_reclaimed);

	if (tid_data->ssn == tid_data->next_reclaimed) {
		tid_data->state = IWL_AGG_STARTING;
		ieee80211_start_tx_ba_cb_irqsafe(vif, sta->addr, tid);
	} else {
		tid_data->state = IWL_EMPTYING_HW_QUEUE_ADDBA;
	}

	ret = 0;

release_locks:
	spin_unlock_bh(&mvmsta->lock);

	return ret;
}

int iwl_mvm_sta_tx_agg_oper(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			    struct ieee80211_sta *sta, u16 tid, u8 buf_size,
			    bool amsdu)
{
	struct iwl_mvm_sta *mvmsta = iwl_mvm_sta_from_mac80211(sta);
	struct iwl_mvm_tid_data *tid_data = &mvmsta->tid_data[tid];
	unsigned int wdg_timeout =
		iwl_mvm_get_wd_timeout(mvm, vif, sta->tdls, false);
	int queue, ret;
	u16 ssn;

	struct iwl_trans_txq_scd_cfg cfg = {
		.sta_id = mvmsta->sta_id,
		.tid = tid,
		.frame_limit = buf_size,
		.aggregate = true,
	};

	BUILD_BUG_ON((sizeof(mvmsta->agg_tids) * BITS_PER_BYTE)
		     != IWL_MAX_TID_COUNT);

	buf_size = min_t(int, buf_size, LINK_QUAL_AGG_FRAME_LIMIT_DEF);

	spin_lock_bh(&mvmsta->lock);
	ssn = tid_data->ssn;
	queue = tid_data->txq_id;
	tid_data->state = IWL_AGG_ON;
	mvmsta->agg_tids |= BIT(tid);
	tid_data->ssn = 0xffff;
	tid_data->amsdu_in_ampdu_allowed = amsdu;
	spin_unlock_bh(&mvmsta->lock);

	cfg.fifo = iwl_mvm_ac_to_tx_fifo[tid_to_mac80211_ac[tid]];

	iwl_mvm_enable_txq(mvm, queue, vif->hw_queue[tid_to_mac80211_ac[tid]],
			   ssn, &cfg, wdg_timeout);

	ret = iwl_mvm_sta_tx_agg(mvm, sta, tid, queue, true);
	if (ret)
		return -EIO;

	/* No need to mark as reserved */
	spin_lock_bh(&mvm->queue_info_lock);
	mvm->queue_info[queue].setup_reserved = false;
	spin_unlock_bh(&mvm->queue_info_lock);

	/*
	 * Even though in theory the peer could have different
	 * aggregation reorder buffer sizes for different sessions,
	 * our ucode doesn't allow for that and has a global limit
	 * for each station. Therefore, use the minimum of all the
	 * aggregation sessions and our default value.
	 */
	mvmsta->max_agg_bufsize =
		min(mvmsta->max_agg_bufsize, buf_size);
	mvmsta->lq_sta.lq.agg_frame_cnt_limit = mvmsta->max_agg_bufsize;

	IWL_DEBUG_HT(mvm, "Tx aggregation enabled on ra = %pM tid = %d\n",
		     sta->addr, tid);

	return iwl_mvm_send_lq_cmd(mvm, &mvmsta->lq_sta.lq, false);
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

	/* No need to mark as reserved anymore */
	spin_lock_bh(&mvm->queue_info_lock);
	mvm->queue_info[txq_id].setup_reserved = false;
	spin_unlock_bh(&mvm->queue_info_lock);

	switch (tid_data->state) {
	case IWL_AGG_ON:
		tid_data->ssn = IEEE80211_SEQ_TO_SN(tid_data->seq_number);

		IWL_DEBUG_TX_QUEUES(mvm,
				    "ssn = %d, next_recl = %d\n",
				    tid_data->ssn, tid_data->next_reclaimed);

		/* There are still packets for this RA / TID in the HW */
		if (tid_data->ssn != tid_data->next_reclaimed) {
			tid_data->state = IWL_EMPTYING_HW_QUEUE_DELBA;
			err = 0;
			break;
		}

		tid_data->ssn = 0xffff;
		tid_data->state = IWL_AGG_OFF;
		spin_unlock_bh(&mvmsta->lock);

		ieee80211_stop_tx_ba_cb_irqsafe(vif, sta->addr, tid);

		iwl_mvm_sta_tx_agg(mvm, sta, tid, txq_id, false);

		iwl_mvm_disable_txq(mvm, txq_id,
				    vif->hw_queue[tid_to_mac80211_ac[tid]], tid,
				    0);
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

	/* No need to mark as reserved */
	spin_lock_bh(&mvm->queue_info_lock);
	mvm->queue_info[txq_id].setup_reserved = false;
	spin_unlock_bh(&mvm->queue_info_lock);

	if (old_state >= IWL_AGG_ON) {
		iwl_mvm_drain_sta(mvm, mvmsta, true);
		if (iwl_mvm_flush_tx_path(mvm, BIT(txq_id), 0))
			IWL_ERR(mvm, "Couldn't flush the AGG queue\n");
		iwl_trans_wait_tx_queue_empty(mvm->trans,
					      mvmsta->tfd_queue_msk);
		iwl_mvm_drain_sta(mvm, mvmsta, false);

		iwl_mvm_sta_tx_agg(mvm, sta, tid, txq_id, false);

		iwl_mvm_disable_txq(mvm, tid_data->txq_id,
				    vif->hw_queue[tid_to_mac80211_ac[tid]], tid,
				    0);
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
	    mvmvif->ap_sta_id != IWL_MVM_STATION_COUNT) {
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

static int iwl_mvm_send_sta_key(struct iwl_mvm *mvm,
				struct iwl_mvm_sta *mvm_sta,
				struct ieee80211_key_conf *keyconf, bool mcast,
				u32 tkip_iv32, u16 *tkip_p1k, u32 cmd_flags,
				u8 key_offset)
{
	struct iwl_mvm_add_sta_key_cmd cmd = {};
	__le16 key_flags;
	int ret;
	u32 status;
	u16 keyidx;
	int i;
	u8 sta_id = mvm_sta->sta_id;

	keyidx = (keyconf->keyidx << STA_KEY_FLG_KEYID_POS) &
		 STA_KEY_FLG_KEYID_MSK;
	key_flags = cpu_to_le16(keyidx);
	key_flags |= cpu_to_le16(STA_KEY_FLG_WEP_KEY_MAP);

	switch (keyconf->cipher) {
	case WLAN_CIPHER_SUITE_TKIP:
		key_flags |= cpu_to_le16(STA_KEY_FLG_TKIP);
		cmd.tkip_rx_tsc_byte2 = tkip_iv32;
		for (i = 0; i < 5; i++)
			cmd.tkip_rx_ttak[i] = cpu_to_le16(tkip_p1k[i]);
		memcpy(cmd.key, keyconf->key, keyconf->keylen);
		break;
	case WLAN_CIPHER_SUITE_CCMP:
		key_flags |= cpu_to_le16(STA_KEY_FLG_CCM);
		memcpy(cmd.key, keyconf->key, keyconf->keylen);
		break;
	case WLAN_CIPHER_SUITE_WEP104:
		key_flags |= cpu_to_le16(STA_KEY_FLG_WEP_13BYTES);
		/* fall through */
	case WLAN_CIPHER_SUITE_WEP40:
		key_flags |= cpu_to_le16(STA_KEY_FLG_WEP);
		memcpy(cmd.key + 3, keyconf->key, keyconf->keylen);
		break;
	default:
		key_flags |= cpu_to_le16(STA_KEY_FLG_EXT);
		memcpy(cmd.key, keyconf->key, keyconf->keylen);
	}

	if (mcast)
		key_flags |= cpu_to_le16(STA_KEY_MULTICAST);

	cmd.key_offset = key_offset;
	cmd.key_flags = key_flags;
	cmd.sta_id = sta_id;

	status = ADD_STA_SUCCESS;
	if (cmd_flags & CMD_ASYNC)
		ret =  iwl_mvm_send_cmd_pdu(mvm, ADD_STA_KEY, CMD_ASYNC,
					    sizeof(cmd), &cmd);
	else
		ret = iwl_mvm_send_cmd_pdu_status(mvm, ADD_STA_KEY, sizeof(cmd),
						  &cmd, &status);

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
	if (WARN_ON((keyconf->cipher != WLAN_CIPHER_SUITE_AES_CMAC) ||
		    (keyconf->flags & IEEE80211_KEY_FLAG_PAIRWISE) ||
		    (keyconf->keyidx != 4 && keyconf->keyidx != 5)))
		return -EINVAL;

	igtk_cmd.key_id = cpu_to_le32(keyconf->keyidx);
	igtk_cmd.sta_id = cpu_to_le32(sta_id);

	if (remove_key) {
		igtk_cmd.ctrl_flags |= cpu_to_le32(STA_KEY_NOT_VALID);
	} else {
		struct ieee80211_key_seq seq;
		const u8 *pn;

		memcpy(igtk_cmd.IGTK, keyconf->key, keyconf->keylen);
		ieee80211_get_key_rx_seq(keyconf, 0, &seq);
		pn = seq.aes_cmac.pn;
		igtk_cmd.receive_seq_cnt = cpu_to_le64(((u64) pn[5] << 0) |
						       ((u64) pn[4] << 8) |
						       ((u64) pn[3] << 16) |
						       ((u64) pn[2] << 24) |
						       ((u64) pn[1] << 32) |
						       ((u64) pn[0] << 40));
	}

	IWL_DEBUG_INFO(mvm, "%s igtk for sta %u\n",
		       remove_key ? "removing" : "installing",
		       igtk_cmd.sta_id);

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
	    mvmvif->ap_sta_id != IWL_MVM_STATION_COUNT) {
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
	struct iwl_mvm_sta *mvm_sta = iwl_mvm_sta_from_mac80211(sta);
	int ret;
	const u8 *addr;
	struct ieee80211_key_seq seq;
	u16 p1k[5];

	switch (keyconf->cipher) {
	case WLAN_CIPHER_SUITE_TKIP:
		addr = iwl_mvm_get_mac_addr(mvm, vif, sta);
		/* get phase 1 key from mac80211 */
		ieee80211_get_key_rx_seq(keyconf, 0, &seq);
		ieee80211_get_tkip_rx_p1k(keyconf, addr, seq.tkip.iv32, p1k);
		ret = iwl_mvm_send_sta_key(mvm, mvm_sta, keyconf, mcast,
					   seq.tkip.iv32, p1k, 0, key_offset);
		break;
	case WLAN_CIPHER_SUITE_CCMP:
	case WLAN_CIPHER_SUITE_WEP40:
	case WLAN_CIPHER_SUITE_WEP104:
		ret = iwl_mvm_send_sta_key(mvm, mvm_sta, keyconf, mcast,
					   0, NULL, 0, key_offset);
		break;
	default:
		ret = iwl_mvm_send_sta_key(mvm, mvm_sta, keyconf, mcast,
					   0, NULL, 0, key_offset);
	}

	return ret;
}

static int __iwl_mvm_remove_sta_key(struct iwl_mvm *mvm, u8 sta_id,
				    struct ieee80211_key_conf *keyconf,
				    bool mcast)
{
	struct iwl_mvm_add_sta_key_cmd cmd = {};
	__le16 key_flags;
	int ret;
	u32 status;

	key_flags = cpu_to_le16((keyconf->keyidx << STA_KEY_FLG_KEYID_POS) &
				 STA_KEY_FLG_KEYID_MSK);
	key_flags |= cpu_to_le16(STA_KEY_FLG_NO_ENC | STA_KEY_FLG_WEP_KEY_MAP);
	key_flags |= cpu_to_le16(STA_KEY_NOT_VALID);

	if (mcast)
		key_flags |= cpu_to_le16(STA_KEY_MULTICAST);

	cmd.key_flags = key_flags;
	cmd.key_offset = keyconf->hw_key_idx;
	cmd.sta_id = sta_id;

	status = ADD_STA_SUCCESS;
	ret = iwl_mvm_send_cmd_pdu_status(mvm, ADD_STA_KEY, sizeof(cmd),
					  &cmd, &status);

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

int iwl_mvm_set_sta_key(struct iwl_mvm *mvm,
			struct ieee80211_vif *vif,
			struct ieee80211_sta *sta,
			struct ieee80211_key_conf *keyconf,
			u8 key_offset)
{
	bool mcast = !(keyconf->flags & IEEE80211_KEY_FLAG_PAIRWISE);
	struct iwl_mvm_sta *mvm_sta;
	u8 sta_id;
	int ret;
	static const u8 __maybe_unused zero_addr[ETH_ALEN] = {0};

	lockdep_assert_held(&mvm->mutex);

	/* Get the station id from the mvm local station table */
	mvm_sta = iwl_mvm_get_key_sta(mvm, vif, sta);
	if (!mvm_sta) {
		IWL_ERR(mvm, "Failed to find station\n");
		return -EINVAL;
	}
	sta_id = mvm_sta->sta_id;

	if (keyconf->cipher == WLAN_CIPHER_SUITE_AES_CMAC) {
		ret = iwl_mvm_send_sta_igtk(mvm, keyconf, sta_id, false);
		goto end;
	}

	/*
	 * It is possible that the 'sta' parameter is NULL, and thus
	 * there is a need to retrieve  the sta from the local station table.
	 */
	if (!sta) {
		sta = rcu_dereference_protected(mvm->fw_id_to_mac_id[sta_id],
						lockdep_is_held(&mvm->mutex));
		if (IS_ERR_OR_NULL(sta)) {
			IWL_ERR(mvm, "Invalid station id\n");
			return -EINVAL;
		}
	}

	if (WARN_ON_ONCE(iwl_mvm_sta_from_mac80211(sta)->vif != vif))
		return -EINVAL;

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
	if (keyconf->cipher == WLAN_CIPHER_SUITE_WEP40 ||
	    keyconf->cipher == WLAN_CIPHER_SUITE_WEP104) {
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
	u8 sta_id = IWL_MVM_STATION_COUNT;
	int ret, i;

	lockdep_assert_held(&mvm->mutex);

	/* Get the station from the mvm local station table */
	mvm_sta = iwl_mvm_get_key_sta(mvm, vif, sta);

	IWL_DEBUG_WEP(mvm, "mvm remove dynamic key: idx=%d sta=%d\n",
		      keyconf->keyidx, sta_id);

	if (keyconf->cipher == WLAN_CIPHER_SUITE_AES_CMAC)
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

	if (!mvm_sta) {
		IWL_DEBUG_WEP(mvm, "station non-existent, early return.\n");
		return 0;
	}

	sta_id = mvm_sta->sta_id;

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

	rcu_read_lock();

	mvm_sta = iwl_mvm_get_key_sta(mvm, vif, sta);
	if (WARN_ON_ONCE(!mvm_sta))
		goto unlock;
	iwl_mvm_send_sta_key(mvm, mvm_sta, keyconf, mcast,
			     iv32, phase1key, CMD_ASYNC, keyconf->hw_key_idx);

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
				       bool agg)
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

	/* If we're releasing frames from aggregation queues then check if the
	 * all queues combined that we're releasing frames from have
	 *  - more frames than the service period, in which case more_data
	 *    needs to be set
	 *  - fewer than 'cnt' frames, in which case we need to adjust the
	 *    firmware command (but do that unconditionally)
	 */
	if (agg) {
		int remaining = cnt;
		int sleep_tx_count;

		spin_lock_bh(&mvmsta->lock);
		for_each_set_bit(tid, &_tids, IWL_MAX_TID_COUNT) {
			struct iwl_mvm_tid_data *tid_data;
			u16 n_queued;

			tid_data = &mvmsta->tid_data[tid];
			if (WARN(tid_data->state != IWL_AGG_ON &&
				 tid_data->state != IWL_EMPTYING_HW_QUEUE_DELBA,
				 "TID %d state is %d\n",
				 tid, tid_data->state)) {
				spin_unlock_bh(&mvmsta->lock);
				ieee80211_sta_eosp(sta);
				return;
			}

			n_queued = iwl_mvm_tid_queued(tid_data);
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
		cmd.sleep_state_flags |= cpu_to_le16(STA_SLEEP_STATE_MOREDATA);

	if (reason == IEEE80211_FRAME_RELEASE_PSPOLL) {
		mvmsta->next_status_eosp = true;
		cmd.sleep_state_flags |= cpu_to_le16(STA_SLEEP_STATE_PS_POLL);
	} else {
		cmd.sleep_state_flags |= cpu_to_le16(STA_SLEEP_STATE_UAPSD);
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

	if (WARN_ON_ONCE(sta_id >= IWL_MVM_STATION_COUNT))
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
	 * Tell mac80211 to start/stop queuing tx for this station,
	 * but don't stop queuing if there are still pending frames
	 * for this station.
	 */
	if (disable || !atomic_read(&mvm->pending_frames[mvm_sta->sta_id]))
		ieee80211_sta_block_awake(mvm->hw, sta, disable);

	iwl_mvm_sta_modify_disable_tx(mvm, mvm_sta, disable);

	spin_unlock_bh(&mvm_sta->lock);
}

void iwl_mvm_modify_all_sta_disable_tx(struct iwl_mvm *mvm,
				       struct iwl_mvm_vif *mvmvif,
				       bool disable)
{
	struct ieee80211_sta *sta;
	struct iwl_mvm_sta *mvm_sta;
	int i;

	lockdep_assert_held(&mvm->mutex);

	/* Block/unblock all the stations of the given mvmvif */
	for (i = 0; i < IWL_MVM_STATION_COUNT; i++) {
		sta = rcu_dereference_protected(mvm->fw_id_to_mac_id[i],
						lockdep_is_held(&mvm->mutex));
		if (IS_ERR_OR_NULL(sta))
			continue;

		mvm_sta = iwl_mvm_sta_from_mac80211(sta);
		if (mvm_sta->mac_id_n_color !=
		    FW_CMD_ID_AND_COLOR(mvmvif->id, mvmvif->color))
			continue;

		iwl_mvm_sta_modify_disable_tx_ap(mvm, sta, disable);
	}
}

void iwl_mvm_csa_client_absent(struct iwl_mvm *mvm, struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm_sta *mvmsta;

	rcu_read_lock();

	mvmsta = iwl_mvm_sta_from_staid_rcu(mvm, mvmvif->ap_sta_id);

	if (!WARN_ON(!mvmsta))
		iwl_mvm_sta_modify_disable_tx(mvm, mvmsta, true);

	rcu_read_unlock();
}
