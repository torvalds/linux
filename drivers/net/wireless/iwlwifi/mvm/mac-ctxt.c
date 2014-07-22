/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
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
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
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

#include <linux/etherdevice.h>
#include <net/mac80211.h>
#include "iwl-io.h"
#include "iwl-prph.h"
#include "fw-api.h"
#include "mvm.h"

const u8 iwl_mvm_ac_to_tx_fifo[] = {
	IWL_MVM_TX_FIFO_VO,
	IWL_MVM_TX_FIFO_VI,
	IWL_MVM_TX_FIFO_BE,
	IWL_MVM_TX_FIFO_BK,
};

struct iwl_mvm_mac_iface_iterator_data {
	struct iwl_mvm *mvm;
	struct ieee80211_vif *vif;
	unsigned long available_mac_ids[BITS_TO_LONGS(NUM_MAC_INDEX_DRIVER)];
	unsigned long available_tsf_ids[BITS_TO_LONGS(NUM_TSF_IDS)];
	unsigned long used_hw_queues[BITS_TO_LONGS(IWL_MVM_MAX_QUEUES)];
	enum iwl_tsf_id preferred_tsf;
	bool found_vif;
};

static void iwl_mvm_mac_tsf_id_iter(void *_data, u8 *mac,
				    struct ieee80211_vif *vif)
{
	struct iwl_mvm_mac_iface_iterator_data *data = _data;
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	u16 min_bi;

	/* Skip the interface for which we are trying to assign a tsf_id  */
	if (vif == data->vif)
		return;

	/*
	 * The TSF is a hardware/firmware resource, there are 4 and
	 * the driver should assign and free them as needed. However,
	 * there are cases where 2 MACs should share the same TSF ID
	 * for the purpose of clock sync, an optimization to avoid
	 * clock drift causing overlapping TBTTs/DTIMs for a GO and
	 * client in the system.
	 *
	 * The firmware will decide according to the MAC type which
	 * will be the master and slave. Clients that need to sync
	 * with a remote station will be the master, and an AP or GO
	 * will be the slave.
	 *
	 * Depending on the new interface type it can be slaved to
	 * or become the master of an existing interface.
	 */
	switch (data->vif->type) {
	case NL80211_IFTYPE_STATION:
		/*
		 * The new interface is a client, so if the one we're iterating
		 * is an AP, and the beacon interval of the AP is a multiple or
		 * divisor of the beacon interval of the client, the same TSF
		 * should be used to avoid drift between the new client and
		 * existing AP. The existing AP will get drift updates from the
		 * new client context in this case.
		 */
		if (vif->type != NL80211_IFTYPE_AP ||
		    data->preferred_tsf != NUM_TSF_IDS ||
		    !test_bit(mvmvif->tsf_id, data->available_tsf_ids))
			break;

		min_bi = min(data->vif->bss_conf.beacon_int,
			     vif->bss_conf.beacon_int);

		if (!min_bi)
			break;

		if ((data->vif->bss_conf.beacon_int -
		     vif->bss_conf.beacon_int) % min_bi == 0) {
			data->preferred_tsf = mvmvif->tsf_id;
			return;
		}
		break;

	case NL80211_IFTYPE_AP:
		/*
		 * The new interface is AP/GO, so if its beacon interval is a
		 * multiple or a divisor of the beacon interval of an existing
		 * interface, it should get drift updates from an existing
		 * client or use the same TSF as an existing GO. There's no
		 * drift between TSFs internally but if they used different
		 * TSFs then a new client MAC could update one of them and
		 * cause drift that way.
		 */
		if ((vif->type != NL80211_IFTYPE_AP &&
		     vif->type != NL80211_IFTYPE_STATION) ||
		    data->preferred_tsf != NUM_TSF_IDS ||
		    !test_bit(mvmvif->tsf_id, data->available_tsf_ids))
			break;

		min_bi = min(data->vif->bss_conf.beacon_int,
			     vif->bss_conf.beacon_int);

		if (!min_bi)
			break;

		if ((data->vif->bss_conf.beacon_int -
		     vif->bss_conf.beacon_int) % min_bi == 0) {
			data->preferred_tsf = mvmvif->tsf_id;
			return;
		}
		break;
	default:
		/*
		 * For all other interface types there's no need to
		 * take drift into account. Either they're exclusive
		 * like IBSS and monitor, or we don't care much about
		 * their TSF (like P2P Device), but we won't be able
		 * to share the TSF resource.
		 */
		break;
	}

	/*
	 * Unless we exited above, we can't share the TSF resource
	 * that the virtual interface we're iterating over is using
	 * with the new one, so clear the available bit and if this
	 * was the preferred one, reset that as well.
	 */
	__clear_bit(mvmvif->tsf_id, data->available_tsf_ids);

	if (data->preferred_tsf == mvmvif->tsf_id)
		data->preferred_tsf = NUM_TSF_IDS;
}

static void iwl_mvm_mac_iface_iterator(void *_data, u8 *mac,
				       struct ieee80211_vif *vif)
{
	struct iwl_mvm_mac_iface_iterator_data *data = _data;
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	u32 ac;

	/* Iterator may already find the interface being added -- skip it */
	if (vif == data->vif) {
		data->found_vif = true;
		return;
	}

	/* Mark the queues used by the vif */
	for (ac = 0; ac < IEEE80211_NUM_ACS; ac++)
		if (vif->hw_queue[ac] != IEEE80211_INVAL_HW_QUEUE)
			__set_bit(vif->hw_queue[ac], data->used_hw_queues);

	if (vif->cab_queue != IEEE80211_INVAL_HW_QUEUE)
		__set_bit(vif->cab_queue, data->used_hw_queues);

	/* Mark MAC IDs as used by clearing the available bit, and
	 * (below) mark TSFs as used if their existing use is not
	 * compatible with the new interface type.
	 * No locking or atomic bit operations are needed since the
	 * data is on the stack of the caller function.
	 */
	__clear_bit(mvmvif->id, data->available_mac_ids);

	/* find a suitable tsf_id */
	iwl_mvm_mac_tsf_id_iter(_data, mac, vif);
}

/*
 * Get the mask of the queus used by the vif
 */
u32 iwl_mvm_mac_get_queues_mask(struct iwl_mvm *mvm,
				struct ieee80211_vif *vif)
{
	u32 qmask = 0, ac;

	if (vif->type == NL80211_IFTYPE_P2P_DEVICE)
		return BIT(IWL_MVM_OFFCHANNEL_QUEUE);

	for (ac = 0; ac < IEEE80211_NUM_ACS; ac++)
		if (vif->hw_queue[ac] != IEEE80211_INVAL_HW_QUEUE)
			qmask |= BIT(vif->hw_queue[ac]);

	return qmask;
}

void iwl_mvm_mac_ctxt_recalc_tsf_id(struct iwl_mvm *mvm,
				    struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm_mac_iface_iterator_data data = {
		.mvm = mvm,
		.vif = vif,
		.available_tsf_ids = { (1 << NUM_TSF_IDS) - 1 },
		/* no preference yet */
		.preferred_tsf = NUM_TSF_IDS,
	};

	ieee80211_iterate_active_interfaces_atomic(
		mvm->hw, IEEE80211_IFACE_ITER_RESUME_ALL,
		iwl_mvm_mac_tsf_id_iter, &data);

	if (data.preferred_tsf != NUM_TSF_IDS)
		mvmvif->tsf_id = data.preferred_tsf;
	else if (!test_bit(mvmvif->tsf_id, data.available_tsf_ids))
		mvmvif->tsf_id = find_first_bit(data.available_tsf_ids,
						NUM_TSF_IDS);
}

static int iwl_mvm_mac_ctxt_allocate_resources(struct iwl_mvm *mvm,
					       struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm_mac_iface_iterator_data data = {
		.mvm = mvm,
		.vif = vif,
		.available_mac_ids = { (1 << NUM_MAC_INDEX_DRIVER) - 1 },
		.available_tsf_ids = { (1 << NUM_TSF_IDS) - 1 },
		/* no preference yet */
		.preferred_tsf = NUM_TSF_IDS,
		.used_hw_queues = {
			BIT(IWL_MVM_OFFCHANNEL_QUEUE) |
			BIT(mvm->aux_queue) |
			BIT(IWL_MVM_CMD_QUEUE)
		},
		.found_vif = false,
	};
	u32 ac;
	int ret, i;

	/*
	 * Allocate a MAC ID and a TSF for this MAC, along with the queues
	 * and other resources.
	 */

	/*
	 * Before the iterator, we start with all MAC IDs and TSFs available.
	 *
	 * During iteration, all MAC IDs are cleared that are in use by other
	 * virtual interfaces, and all TSF IDs are cleared that can't be used
	 * by this new virtual interface because they're used by an interface
	 * that can't share it with the new one.
	 * At the same time, we check if there's a preferred TSF in the case
	 * that we should share it with another interface.
	 */

	/* Currently, MAC ID 0 should be used only for the managed/IBSS vif */
	switch (vif->type) {
	case NL80211_IFTYPE_ADHOC:
		break;
	case NL80211_IFTYPE_STATION:
		if (!vif->p2p)
			break;
		/* fall through */
	default:
		__clear_bit(0, data.available_mac_ids);
	}

	ieee80211_iterate_active_interfaces_atomic(
		mvm->hw, IEEE80211_IFACE_ITER_RESUME_ALL,
		iwl_mvm_mac_iface_iterator, &data);

	/*
	 * In the case we're getting here during resume, it's similar to
	 * firmware restart, and with RESUME_ALL the iterator will find
	 * the vif being added already.
	 * We don't want to reassign any IDs in either case since doing
	 * so would probably assign different IDs (as interfaces aren't
	 * necessarily added in the same order), but the old IDs were
	 * preserved anyway, so skip ID assignment for both resume and
	 * recovery.
	 */
	if (data.found_vif)
		return 0;

	/* Therefore, in recovery, we can't get here */
	if (WARN_ON_ONCE(test_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status)))
		return -EBUSY;

	mvmvif->id = find_first_bit(data.available_mac_ids,
				    NUM_MAC_INDEX_DRIVER);
	if (mvmvif->id == NUM_MAC_INDEX_DRIVER) {
		IWL_ERR(mvm, "Failed to init MAC context - no free ID!\n");
		ret = -EIO;
		goto exit_fail;
	}

	if (data.preferred_tsf != NUM_TSF_IDS)
		mvmvif->tsf_id = data.preferred_tsf;
	else
		mvmvif->tsf_id = find_first_bit(data.available_tsf_ids,
						NUM_TSF_IDS);
	if (mvmvif->tsf_id == NUM_TSF_IDS) {
		IWL_ERR(mvm, "Failed to init MAC context - no free TSF!\n");
		ret = -EIO;
		goto exit_fail;
	}

	mvmvif->color = 0;

	INIT_LIST_HEAD(&mvmvif->time_event_data.list);
	mvmvif->time_event_data.id = TE_MAX;

	/* No need to allocate data queues to P2P Device MAC.*/
	if (vif->type == NL80211_IFTYPE_P2P_DEVICE) {
		for (ac = 0; ac < IEEE80211_NUM_ACS; ac++)
			vif->hw_queue[ac] = IEEE80211_INVAL_HW_QUEUE;

		return 0;
	}

	/* Find available queues, and allocate them to the ACs */
	for (ac = 0; ac < IEEE80211_NUM_ACS; ac++) {
		u8 queue = find_first_zero_bit(data.used_hw_queues,
					       mvm->first_agg_queue);

		if (queue >= mvm->first_agg_queue) {
			IWL_ERR(mvm, "Failed to allocate queue\n");
			ret = -EIO;
			goto exit_fail;
		}

		__set_bit(queue, data.used_hw_queues);
		vif->hw_queue[ac] = queue;
	}

	/* Allocate the CAB queue for softAP and GO interfaces */
	if (vif->type == NL80211_IFTYPE_AP) {
		u8 queue = find_first_zero_bit(data.used_hw_queues,
					       mvm->first_agg_queue);

		if (queue >= mvm->first_agg_queue) {
			IWL_ERR(mvm, "Failed to allocate cab queue\n");
			ret = -EIO;
			goto exit_fail;
		}

		vif->cab_queue = queue;
	} else {
		vif->cab_queue = IEEE80211_INVAL_HW_QUEUE;
	}

	mvmvif->bcast_sta.sta_id = IWL_MVM_STATION_COUNT;
	mvmvif->ap_sta_id = IWL_MVM_STATION_COUNT;

	for (i = 0; i < NUM_IWL_MVM_SMPS_REQ; i++)
		mvmvif->smps_requests[i] = IEEE80211_SMPS_AUTOMATIC;

	return 0;

exit_fail:
	memset(mvmvif, 0, sizeof(struct iwl_mvm_vif));
	memset(vif->hw_queue, IEEE80211_INVAL_HW_QUEUE, sizeof(vif->hw_queue));
	vif->cab_queue = IEEE80211_INVAL_HW_QUEUE;
	return ret;
}

int iwl_mvm_mac_ctxt_init(struct iwl_mvm *mvm, struct ieee80211_vif *vif)
{
	u32 ac;
	int ret;

	lockdep_assert_held(&mvm->mutex);

	ret = iwl_mvm_mac_ctxt_allocate_resources(mvm, vif);
	if (ret)
		return ret;

	switch (vif->type) {
	case NL80211_IFTYPE_P2P_DEVICE:
		iwl_trans_ac_txq_enable(mvm->trans, IWL_MVM_OFFCHANNEL_QUEUE,
					IWL_MVM_TX_FIFO_VO);
		break;
	case NL80211_IFTYPE_AP:
		iwl_trans_ac_txq_enable(mvm->trans, vif->cab_queue,
					IWL_MVM_TX_FIFO_MCAST);
		/* fall through */
	default:
		for (ac = 0; ac < IEEE80211_NUM_ACS; ac++)
			iwl_trans_ac_txq_enable(mvm->trans, vif->hw_queue[ac],
						iwl_mvm_ac_to_tx_fifo[ac]);
		break;
	}

	return 0;
}

void iwl_mvm_mac_ctxt_release(struct iwl_mvm *mvm, struct ieee80211_vif *vif)
{
	int ac;

	lockdep_assert_held(&mvm->mutex);

	switch (vif->type) {
	case NL80211_IFTYPE_P2P_DEVICE:
		iwl_trans_txq_disable(mvm->trans, IWL_MVM_OFFCHANNEL_QUEUE);
		break;
	case NL80211_IFTYPE_AP:
		iwl_trans_txq_disable(mvm->trans, vif->cab_queue);
		/* fall through */
	default:
		for (ac = 0; ac < IEEE80211_NUM_ACS; ac++)
			iwl_trans_txq_disable(mvm->trans, vif->hw_queue[ac]);
	}
}

static void iwl_mvm_ack_rates(struct iwl_mvm *mvm,
			      struct ieee80211_vif *vif,
			      enum ieee80211_band band,
			      u8 *cck_rates, u8 *ofdm_rates)
{
	struct ieee80211_supported_band *sband;
	unsigned long basic = vif->bss_conf.basic_rates;
	int lowest_present_ofdm = 100;
	int lowest_present_cck = 100;
	u8 cck = 0;
	u8 ofdm = 0;
	int i;

	sband = mvm->hw->wiphy->bands[band];

	for_each_set_bit(i, &basic, BITS_PER_LONG) {
		int hw = sband->bitrates[i].hw_value;
		if (hw >= IWL_FIRST_OFDM_RATE) {
			ofdm |= BIT(hw - IWL_FIRST_OFDM_RATE);
			if (lowest_present_ofdm > hw)
				lowest_present_ofdm = hw;
		} else {
			BUILD_BUG_ON(IWL_FIRST_CCK_RATE != 0);

			cck |= BIT(hw);
			if (lowest_present_cck > hw)
				lowest_present_cck = hw;
		}
	}

	/*
	 * Now we've got the basic rates as bitmaps in the ofdm and cck
	 * variables. This isn't sufficient though, as there might not
	 * be all the right rates in the bitmap. E.g. if the only basic
	 * rates are 5.5 Mbps and 11 Mbps, we still need to add 1 Mbps
	 * and 6 Mbps because the 802.11-2007 standard says in 9.6:
	 *
	 *    [...] a STA responding to a received frame shall transmit
	 *    its Control Response frame [...] at the highest rate in the
	 *    BSSBasicRateSet parameter that is less than or equal to the
	 *    rate of the immediately previous frame in the frame exchange
	 *    sequence ([...]) and that is of the same modulation class
	 *    ([...]) as the received frame. If no rate contained in the
	 *    BSSBasicRateSet parameter meets these conditions, then the
	 *    control frame sent in response to a received frame shall be
	 *    transmitted at the highest mandatory rate of the PHY that is
	 *    less than or equal to the rate of the received frame, and
	 *    that is of the same modulation class as the received frame.
	 *
	 * As a consequence, we need to add all mandatory rates that are
	 * lower than all of the basic rates to these bitmaps.
	 */

	if (IWL_RATE_24M_INDEX < lowest_present_ofdm)
		ofdm |= IWL_RATE_BIT_MSK(24) >> IWL_FIRST_OFDM_RATE;
	if (IWL_RATE_12M_INDEX < lowest_present_ofdm)
		ofdm |= IWL_RATE_BIT_MSK(12) >> IWL_FIRST_OFDM_RATE;
	/* 6M already there or needed so always add */
	ofdm |= IWL_RATE_BIT_MSK(6) >> IWL_FIRST_OFDM_RATE;

	/*
	 * CCK is a bit more complex with DSSS vs. HR/DSSS vs. ERP.
	 * Note, however:
	 *  - if no CCK rates are basic, it must be ERP since there must
	 *    be some basic rates at all, so they're OFDM => ERP PHY
	 *    (or we're in 5 GHz, and the cck bitmap will never be used)
	 *  - if 11M is a basic rate, it must be ERP as well, so add 5.5M
	 *  - if 5.5M is basic, 1M and 2M are mandatory
	 *  - if 2M is basic, 1M is mandatory
	 *  - if 1M is basic, that's the only valid ACK rate.
	 * As a consequence, it's not as complicated as it sounds, just add
	 * any lower rates to the ACK rate bitmap.
	 */
	if (IWL_RATE_11M_INDEX < lowest_present_cck)
		cck |= IWL_RATE_BIT_MSK(11) >> IWL_FIRST_CCK_RATE;
	if (IWL_RATE_5M_INDEX < lowest_present_cck)
		cck |= IWL_RATE_BIT_MSK(5) >> IWL_FIRST_CCK_RATE;
	if (IWL_RATE_2M_INDEX < lowest_present_cck)
		cck |= IWL_RATE_BIT_MSK(2) >> IWL_FIRST_CCK_RATE;
	/* 1M already there or needed so always add */
	cck |= IWL_RATE_BIT_MSK(1) >> IWL_FIRST_CCK_RATE;

	*cck_rates = cck;
	*ofdm_rates = ofdm;
}

static void iwl_mvm_mac_ctxt_set_ht_flags(struct iwl_mvm *mvm,
					 struct ieee80211_vif *vif,
					 struct iwl_mac_ctx_cmd *cmd)
{
	/* for both sta and ap, ht_operation_mode hold the protection_mode */
	u8 protection_mode = vif->bss_conf.ht_operation_mode &
				 IEEE80211_HT_OP_MODE_PROTECTION;
	/* The fw does not distinguish between ht and fat */
	u32 ht_flag = MAC_PROT_FLG_HT_PROT | MAC_PROT_FLG_FAT_PROT;

	IWL_DEBUG_RATE(mvm, "protection mode set to %d\n", protection_mode);
	/*
	 * See section 9.23.3.1 of IEEE 80211-2012.
	 * Nongreenfield HT STAs Present is not supported.
	 */
	switch (protection_mode) {
	case IEEE80211_HT_OP_MODE_PROTECTION_NONE:
		break;
	case IEEE80211_HT_OP_MODE_PROTECTION_NONMEMBER:
	case IEEE80211_HT_OP_MODE_PROTECTION_NONHT_MIXED:
		cmd->protection_flags |= cpu_to_le32(ht_flag);
		break;
	case IEEE80211_HT_OP_MODE_PROTECTION_20MHZ:
		/* Protect when channel wider than 20MHz */
		if (vif->bss_conf.chandef.width > NL80211_CHAN_WIDTH_20)
			cmd->protection_flags |= cpu_to_le32(ht_flag);
		break;
	default:
		IWL_ERR(mvm, "Illegal protection mode %d\n",
			protection_mode);
		break;
	}
}

static void iwl_mvm_mac_ctxt_cmd_common(struct iwl_mvm *mvm,
					struct ieee80211_vif *vif,
					struct iwl_mac_ctx_cmd *cmd,
					u32 action)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct ieee80211_chanctx_conf *chanctx;
	bool ht_enabled = !!(vif->bss_conf.ht_operation_mode &
			     IEEE80211_HT_OP_MODE_PROTECTION);
	u8 cck_ack_rates, ofdm_ack_rates;
	int i;

	cmd->id_and_color = cpu_to_le32(FW_CMD_ID_AND_COLOR(mvmvif->id,
							    mvmvif->color));
	cmd->action = cpu_to_le32(action);

	switch (vif->type) {
	case NL80211_IFTYPE_STATION:
		if (vif->p2p)
			cmd->mac_type = cpu_to_le32(FW_MAC_TYPE_P2P_STA);
		else
			cmd->mac_type = cpu_to_le32(FW_MAC_TYPE_BSS_STA);
		break;
	case NL80211_IFTYPE_AP:
		cmd->mac_type = cpu_to_le32(FW_MAC_TYPE_GO);
		break;
	case NL80211_IFTYPE_MONITOR:
		cmd->mac_type = cpu_to_le32(FW_MAC_TYPE_LISTENER);
		break;
	case NL80211_IFTYPE_P2P_DEVICE:
		cmd->mac_type = cpu_to_le32(FW_MAC_TYPE_P2P_DEVICE);
		break;
	case NL80211_IFTYPE_ADHOC:
		cmd->mac_type = cpu_to_le32(FW_MAC_TYPE_IBSS);
		break;
	default:
		WARN_ON_ONCE(1);
	}

	cmd->tsf_id = cpu_to_le32(mvmvif->tsf_id);

	memcpy(cmd->node_addr, vif->addr, ETH_ALEN);
	if (vif->bss_conf.bssid)
		memcpy(cmd->bssid_addr, vif->bss_conf.bssid, ETH_ALEN);
	else
		eth_broadcast_addr(cmd->bssid_addr);

	rcu_read_lock();
	chanctx = rcu_dereference(vif->chanctx_conf);
	iwl_mvm_ack_rates(mvm, vif, chanctx ? chanctx->def.chan->band
					    : IEEE80211_BAND_2GHZ,
			  &cck_ack_rates, &ofdm_ack_rates);
	rcu_read_unlock();

	cmd->cck_rates = cpu_to_le32((u32)cck_ack_rates);
	cmd->ofdm_rates = cpu_to_le32((u32)ofdm_ack_rates);

	cmd->cck_short_preamble =
		cpu_to_le32(vif->bss_conf.use_short_preamble ?
			    MAC_FLG_SHORT_PREAMBLE : 0);
	cmd->short_slot =
		cpu_to_le32(vif->bss_conf.use_short_slot ?
			    MAC_FLG_SHORT_SLOT : 0);

	for (i = 0; i < IEEE80211_NUM_ACS; i++) {
		u8 txf = iwl_mvm_ac_to_tx_fifo[i];

		cmd->ac[txf].cw_min =
			cpu_to_le16(mvmvif->queue_params[i].cw_min);
		cmd->ac[txf].cw_max =
			cpu_to_le16(mvmvif->queue_params[i].cw_max);
		cmd->ac[txf].edca_txop =
			cpu_to_le16(mvmvif->queue_params[i].txop * 32);
		cmd->ac[txf].aifsn = mvmvif->queue_params[i].aifs;
		cmd->ac[txf].fifos_mask = BIT(txf);
	}

	/* in AP mode, the MCAST FIFO takes the EDCA params from VO */
	if (vif->type == NL80211_IFTYPE_AP)
		cmd->ac[IWL_MVM_TX_FIFO_VO].fifos_mask |=
			BIT(IWL_MVM_TX_FIFO_MCAST);

	if (vif->bss_conf.qos)
		cmd->qos_flags |= cpu_to_le32(MAC_QOS_FLG_UPDATE_EDCA);

	if (vif->bss_conf.use_cts_prot)
		cmd->protection_flags |= cpu_to_le32(MAC_PROT_FLG_TGG_PROTECT);

	IWL_DEBUG_RATE(mvm, "use_cts_prot %d, ht_operation_mode %d\n",
		       vif->bss_conf.use_cts_prot,
		       vif->bss_conf.ht_operation_mode);
	if (vif->bss_conf.chandef.width != NL80211_CHAN_WIDTH_20_NOHT)
		cmd->qos_flags |= cpu_to_le32(MAC_QOS_FLG_TGN);
	if (ht_enabled)
		iwl_mvm_mac_ctxt_set_ht_flags(mvm, vif, cmd);

	cmd->filter_flags = cpu_to_le32(MAC_FILTER_ACCEPT_GRP);
}

static int iwl_mvm_mac_ctxt_send_cmd(struct iwl_mvm *mvm,
				     struct iwl_mac_ctx_cmd *cmd)
{
	int ret = iwl_mvm_send_cmd_pdu(mvm, MAC_CONTEXT_CMD, 0,
				       sizeof(*cmd), cmd);
	if (ret)
		IWL_ERR(mvm, "Failed to send MAC context (action:%d): %d\n",
			le32_to_cpu(cmd->action), ret);
	return ret;
}

static int iwl_mvm_mac_ctxt_cmd_sta(struct iwl_mvm *mvm,
				    struct ieee80211_vif *vif,
				    u32 action, bool force_assoc_off)
{
	struct iwl_mac_ctx_cmd cmd = {};
	struct iwl_mac_data_sta *ctxt_sta;

	WARN_ON(vif->type != NL80211_IFTYPE_STATION);

	/* Fill the common data for all mac context types */
	iwl_mvm_mac_ctxt_cmd_common(mvm, vif, &cmd, action);

	if (vif->p2p) {
		struct ieee80211_p2p_noa_attr *noa =
			&vif->bss_conf.p2p_noa_attr;

		cmd.p2p_sta.ctwin = cpu_to_le32(noa->oppps_ctwindow &
					IEEE80211_P2P_OPPPS_CTWINDOW_MASK);
		ctxt_sta = &cmd.p2p_sta.sta;
	} else {
		ctxt_sta = &cmd.sta;
	}

	/* We need the dtim_period to set the MAC as associated */
	if (vif->bss_conf.assoc && vif->bss_conf.dtim_period &&
	    !force_assoc_off) {
		u32 dtim_offs;

		/* Allow beacons to pass through as long as we are not
		 * associated, or we do not have dtim period information.
		 */
		cmd.filter_flags |= cpu_to_le32(MAC_FILTER_IN_BEACON);

		/*
		 * The DTIM count counts down, so when it is N that means N
		 * more beacon intervals happen until the DTIM TBTT. Therefore
		 * add this to the current time. If that ends up being in the
		 * future, the firmware will handle it.
		 *
		 * Also note that the system_timestamp (which we get here as
		 * "sync_device_ts") and TSF timestamp aren't at exactly the
		 * same offset in the frame -- the TSF is at the first symbol
		 * of the TSF, the system timestamp is at signal acquisition
		 * time. This means there's an offset between them of at most
		 * a few hundred microseconds (24 * 8 bits + PLCP time gives
		 * 384us in the longest case), this is currently not relevant
		 * as the firmware wakes up around 2ms before the TBTT.
		 */
		dtim_offs = vif->bss_conf.sync_dtim_count *
				vif->bss_conf.beacon_int;
		/* convert TU to usecs */
		dtim_offs *= 1024;

		ctxt_sta->dtim_tsf =
			cpu_to_le64(vif->bss_conf.sync_tsf + dtim_offs);
		ctxt_sta->dtim_time =
			cpu_to_le32(vif->bss_conf.sync_device_ts + dtim_offs);

		IWL_DEBUG_INFO(mvm, "DTIM TBTT is 0x%llx/0x%x, offset %d\n",
			       le64_to_cpu(ctxt_sta->dtim_tsf),
			       le32_to_cpu(ctxt_sta->dtim_time),
			       dtim_offs);

		ctxt_sta->is_assoc = cpu_to_le32(1);
	} else {
		ctxt_sta->is_assoc = cpu_to_le32(0);
	}

	ctxt_sta->bi = cpu_to_le32(vif->bss_conf.beacon_int);
	ctxt_sta->bi_reciprocal =
		cpu_to_le32(iwl_mvm_reciprocal(vif->bss_conf.beacon_int));
	ctxt_sta->dtim_interval = cpu_to_le32(vif->bss_conf.beacon_int *
					      vif->bss_conf.dtim_period);
	ctxt_sta->dtim_reciprocal =
		cpu_to_le32(iwl_mvm_reciprocal(vif->bss_conf.beacon_int *
					       vif->bss_conf.dtim_period));

	ctxt_sta->listen_interval = cpu_to_le32(mvm->hw->conf.listen_interval);
	ctxt_sta->assoc_id = cpu_to_le32(vif->bss_conf.aid);

	return iwl_mvm_mac_ctxt_send_cmd(mvm, &cmd);
}

static int iwl_mvm_mac_ctxt_cmd_listener(struct iwl_mvm *mvm,
					 struct ieee80211_vif *vif,
					 u32 action)
{
	struct iwl_mac_ctx_cmd cmd = {};

	WARN_ON(vif->type != NL80211_IFTYPE_MONITOR);

	iwl_mvm_mac_ctxt_cmd_common(mvm, vif, &cmd, action);

	cmd.filter_flags = cpu_to_le32(MAC_FILTER_IN_PROMISC |
				       MAC_FILTER_IN_CONTROL_AND_MGMT |
				       MAC_FILTER_IN_BEACON |
				       MAC_FILTER_IN_PROBE_REQUEST |
				       MAC_FILTER_IN_CRC32);
	mvm->hw->flags |= IEEE80211_HW_RX_INCLUDES_FCS;

	return iwl_mvm_mac_ctxt_send_cmd(mvm, &cmd);
}

static int iwl_mvm_mac_ctxt_cmd_ibss(struct iwl_mvm *mvm,
				     struct ieee80211_vif *vif,
				     u32 action)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mac_ctx_cmd cmd = {};

	WARN_ON(vif->type != NL80211_IFTYPE_ADHOC);

	iwl_mvm_mac_ctxt_cmd_common(mvm, vif, &cmd, action);

	cmd.filter_flags = cpu_to_le32(MAC_FILTER_IN_BEACON |
				       MAC_FILTER_IN_PROBE_REQUEST);

	/* cmd.ibss.beacon_time/cmd.ibss.beacon_tsf are curently ignored */
	cmd.ibss.bi = cpu_to_le32(vif->bss_conf.beacon_int);
	cmd.ibss.bi_reciprocal =
		cpu_to_le32(iwl_mvm_reciprocal(vif->bss_conf.beacon_int));

	/* TODO: Assumes that the beacon id == mac context id */
	cmd.ibss.beacon_template = cpu_to_le32(mvmvif->id);

	return iwl_mvm_mac_ctxt_send_cmd(mvm, &cmd);
}

struct iwl_mvm_go_iterator_data {
	bool go_active;
};

static void iwl_mvm_go_iterator(void *_data, u8 *mac, struct ieee80211_vif *vif)
{
	struct iwl_mvm_go_iterator_data *data = _data;
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);

	if (vif->type == NL80211_IFTYPE_AP && vif->p2p &&
	    mvmvif->ap_ibss_active)
		data->go_active = true;
}

static int iwl_mvm_mac_ctxt_cmd_p2p_device(struct iwl_mvm *mvm,
					   struct ieee80211_vif *vif,
					   u32 action)
{
	struct iwl_mac_ctx_cmd cmd = {};
	struct iwl_mvm_go_iterator_data data = {};

	WARN_ON(vif->type != NL80211_IFTYPE_P2P_DEVICE);

	iwl_mvm_mac_ctxt_cmd_common(mvm, vif, &cmd, action);

	cmd.protection_flags |= cpu_to_le32(MAC_PROT_FLG_TGG_PROTECT);

	/* Override the filter flags to accept only probe requests */
	cmd.filter_flags = cpu_to_le32(MAC_FILTER_IN_PROBE_REQUEST);

	/*
	 * This flag should be set to true when the P2P Device is
	 * discoverable and there is at least another active P2P GO. Settings
	 * this flag will allow the P2P Device to be discoverable on other
	 * channels in addition to its listen channel.
	 * Note that this flag should not be set in other cases as it opens the
	 * Rx filters on all MAC and increases the number of interrupts.
	 */
	ieee80211_iterate_active_interfaces_atomic(
		mvm->hw, IEEE80211_IFACE_ITER_RESUME_ALL,
		iwl_mvm_go_iterator, &data);

	cmd.p2p_dev.is_disc_extended = cpu_to_le32(data.go_active ? 1 : 0);
	return iwl_mvm_mac_ctxt_send_cmd(mvm, &cmd);
}

static void iwl_mvm_mac_ctxt_set_tim(struct iwl_mvm *mvm,
				     struct iwl_mac_beacon_cmd *beacon_cmd,
				     u8 *beacon, u32 frame_size)
{
	u32 tim_idx;
	struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)beacon;

	/* The index is relative to frame start but we start looking at the
	 * variable-length part of the beacon. */
	tim_idx = mgmt->u.beacon.variable - beacon;

	/* Parse variable-length elements of beacon to find WLAN_EID_TIM */
	while ((tim_idx < (frame_size - 2)) &&
			(beacon[tim_idx] != WLAN_EID_TIM))
		tim_idx += beacon[tim_idx+1] + 2;

	/* If TIM field was found, set variables */
	if ((tim_idx < (frame_size - 1)) && (beacon[tim_idx] == WLAN_EID_TIM)) {
		beacon_cmd->tim_idx = cpu_to_le32(tim_idx);
		beacon_cmd->tim_size = cpu_to_le32((u32)beacon[tim_idx+1]);
	} else {
		IWL_WARN(mvm, "Unable to find TIM Element in beacon\n");
	}
}

static int iwl_mvm_mac_ctxt_send_beacon(struct iwl_mvm *mvm,
					struct ieee80211_vif *vif,
					struct sk_buff *beacon)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_host_cmd cmd = {
		.id = BEACON_TEMPLATE_CMD,
		.flags = CMD_ASYNC,
	};
	struct iwl_mac_beacon_cmd beacon_cmd = {};
	struct ieee80211_tx_info *info;
	u32 beacon_skb_len;
	u32 rate;

	if (WARN_ON(!beacon))
		return -EINVAL;

	beacon_skb_len = beacon->len;

	/* TODO: for now the beacon template id is set to be the mac context id.
	 * Might be better to handle it as another resource ... */
	beacon_cmd.template_id = cpu_to_le32((u32)mvmvif->id);

	/* Set up TX command fields */
	beacon_cmd.tx.len = cpu_to_le16((u16)beacon_skb_len);
	beacon_cmd.tx.sta_id = mvmvif->bcast_sta.sta_id;
	beacon_cmd.tx.life_time = cpu_to_le32(TX_CMD_LIFE_TIME_INFINITE);
	beacon_cmd.tx.tx_flags = cpu_to_le32(TX_CMD_FLG_SEQ_CTL |
					     TX_CMD_FLG_BT_DIS  |
					     TX_CMD_FLG_TSF);

	mvm->mgmt_last_antenna_idx =
		iwl_mvm_next_antenna(mvm, mvm->fw->valid_tx_ant,
				     mvm->mgmt_last_antenna_idx);

	beacon_cmd.tx.rate_n_flags =
		cpu_to_le32(BIT(mvm->mgmt_last_antenna_idx) <<
			    RATE_MCS_ANT_POS);

	info = IEEE80211_SKB_CB(beacon);

	if (info->band == IEEE80211_BAND_5GHZ || vif->p2p) {
		rate = IWL_FIRST_OFDM_RATE;
	} else {
		rate = IWL_FIRST_CCK_RATE;
		beacon_cmd.tx.rate_n_flags |= cpu_to_le32(RATE_MCS_CCK_MSK);
	}
	beacon_cmd.tx.rate_n_flags |=
		cpu_to_le32(iwl_mvm_mac80211_idx_to_hwrate(rate));

	/* Set up TX beacon command fields */
	if (vif->type == NL80211_IFTYPE_AP)
		iwl_mvm_mac_ctxt_set_tim(mvm, &beacon_cmd,
					 beacon->data,
					 beacon_skb_len);

	/* Submit command */
	cmd.len[0] = sizeof(beacon_cmd);
	cmd.data[0] = &beacon_cmd;
	cmd.dataflags[0] = 0;
	cmd.len[1] = beacon_skb_len;
	cmd.data[1] = beacon->data;
	cmd.dataflags[1] = IWL_HCMD_DFL_DUP;

	return iwl_mvm_send_cmd(mvm, &cmd);
}

/* The beacon template for the AP/GO/IBSS has changed and needs update */
int iwl_mvm_mac_ctxt_beacon_changed(struct iwl_mvm *mvm,
				    struct ieee80211_vif *vif)
{
	struct sk_buff *beacon;
	int ret;

	WARN_ON(vif->type != NL80211_IFTYPE_AP &&
		vif->type != NL80211_IFTYPE_ADHOC);

	beacon = ieee80211_beacon_get(mvm->hw, vif);
	if (!beacon)
		return -ENOMEM;

	ret = iwl_mvm_mac_ctxt_send_beacon(mvm, vif, beacon);
	dev_kfree_skb(beacon);
	return ret;
}

struct iwl_mvm_mac_ap_iterator_data {
	struct iwl_mvm *mvm;
	struct ieee80211_vif *vif;
	u32 beacon_device_ts;
	u16 beacon_int;
};

/* Find the beacon_device_ts and beacon_int for a managed interface */
static void iwl_mvm_mac_ap_iterator(void *_data, u8 *mac,
				    struct ieee80211_vif *vif)
{
	struct iwl_mvm_mac_ap_iterator_data *data = _data;

	if (vif->type != NL80211_IFTYPE_STATION || !vif->bss_conf.assoc)
		return;

	/* Station client has higher priority over P2P client*/
	if (vif->p2p && data->beacon_device_ts)
		return;

	data->beacon_device_ts = vif->bss_conf.sync_device_ts;
	data->beacon_int = vif->bss_conf.beacon_int;
}

/*
 * Fill the specific data for mac context of type AP of P2P GO
 */
static void iwl_mvm_mac_ctxt_cmd_fill_ap(struct iwl_mvm *mvm,
					 struct ieee80211_vif *vif,
					 struct iwl_mac_data_ap *ctxt_ap,
					 bool add)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm_mac_ap_iterator_data data = {
		.mvm = mvm,
		.vif = vif,
		.beacon_device_ts = 0
	};

	ctxt_ap->bi = cpu_to_le32(vif->bss_conf.beacon_int);
	ctxt_ap->bi_reciprocal =
		cpu_to_le32(iwl_mvm_reciprocal(vif->bss_conf.beacon_int));
	ctxt_ap->dtim_interval = cpu_to_le32(vif->bss_conf.beacon_int *
					     vif->bss_conf.dtim_period);
	ctxt_ap->dtim_reciprocal =
		cpu_to_le32(iwl_mvm_reciprocal(vif->bss_conf.beacon_int *
					       vif->bss_conf.dtim_period));

	ctxt_ap->mcast_qid = cpu_to_le32(vif->cab_queue);

	/*
	 * Only set the beacon time when the MAC is being added, when we
	 * just modify the MAC then we should keep the time -- the firmware
	 * can otherwise have a "jumping" TBTT.
	 */
	if (add) {
		/*
		 * If there is a station/P2P client interface which is
		 * associated, set the AP's TBTT far enough from the station's
		 * TBTT. Otherwise, set it to the current system time
		 */
		ieee80211_iterate_active_interfaces_atomic(
			mvm->hw, IEEE80211_IFACE_ITER_RESUME_ALL,
			iwl_mvm_mac_ap_iterator, &data);

		if (data.beacon_device_ts) {
			u32 rand = (prandom_u32() % (64 - 36)) + 36;
			mvmvif->ap_beacon_time = data.beacon_device_ts +
				ieee80211_tu_to_usec(data.beacon_int * rand /
						     100);
		} else {
			mvmvif->ap_beacon_time =
				iwl_read_prph(mvm->trans,
					      DEVICE_SYSTEM_TIME_REG);
		}
	}

	ctxt_ap->beacon_time = cpu_to_le32(mvmvif->ap_beacon_time);
	ctxt_ap->beacon_tsf = 0; /* unused */

	/* TODO: Assume that the beacon id == mac context id */
	ctxt_ap->beacon_template = cpu_to_le32(mvmvif->id);
}

static int iwl_mvm_mac_ctxt_cmd_ap(struct iwl_mvm *mvm,
				   struct ieee80211_vif *vif,
				   u32 action)
{
	struct iwl_mac_ctx_cmd cmd = {};

	WARN_ON(vif->type != NL80211_IFTYPE_AP || vif->p2p);

	/* Fill the common data for all mac context types */
	iwl_mvm_mac_ctxt_cmd_common(mvm, vif, &cmd, action);

	/*
	 * pass probe requests and beacons from other APs (needed
	 * for ht protection)
	 */
	cmd.filter_flags |= cpu_to_le32(MAC_FILTER_IN_PROBE_REQUEST |
					MAC_FILTER_IN_BEACON);

	/* Fill the data specific for ap mode */
	iwl_mvm_mac_ctxt_cmd_fill_ap(mvm, vif, &cmd.ap,
				     action == FW_CTXT_ACTION_ADD);

	return iwl_mvm_mac_ctxt_send_cmd(mvm, &cmd);
}

static int iwl_mvm_mac_ctxt_cmd_go(struct iwl_mvm *mvm,
				   struct ieee80211_vif *vif,
				   u32 action)
{
	struct iwl_mac_ctx_cmd cmd = {};
	struct ieee80211_p2p_noa_attr *noa = &vif->bss_conf.p2p_noa_attr;

	WARN_ON(vif->type != NL80211_IFTYPE_AP || !vif->p2p);

	/* Fill the common data for all mac context types */
	iwl_mvm_mac_ctxt_cmd_common(mvm, vif, &cmd, action);

	/*
	 * pass probe requests and beacons from other APs (needed
	 * for ht protection)
	 */
	cmd.filter_flags |= cpu_to_le32(MAC_FILTER_IN_PROBE_REQUEST |
					MAC_FILTER_IN_BEACON);

	/* Fill the data specific for GO mode */
	iwl_mvm_mac_ctxt_cmd_fill_ap(mvm, vif, &cmd.go.ap,
				     action == FW_CTXT_ACTION_ADD);

	cmd.go.ctwin = cpu_to_le32(noa->oppps_ctwindow &
					IEEE80211_P2P_OPPPS_CTWINDOW_MASK);
	cmd.go.opp_ps_enabled =
			cpu_to_le32(!!(noa->oppps_ctwindow &
					IEEE80211_P2P_OPPPS_ENABLE_BIT));

	return iwl_mvm_mac_ctxt_send_cmd(mvm, &cmd);
}

static int iwl_mvm_mac_ctx_send(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
				u32 action, bool force_assoc_off)
{
	switch (vif->type) {
	case NL80211_IFTYPE_STATION:
		return iwl_mvm_mac_ctxt_cmd_sta(mvm, vif, action,
						force_assoc_off);
		break;
	case NL80211_IFTYPE_AP:
		if (!vif->p2p)
			return iwl_mvm_mac_ctxt_cmd_ap(mvm, vif, action);
		else
			return iwl_mvm_mac_ctxt_cmd_go(mvm, vif, action);
		break;
	case NL80211_IFTYPE_MONITOR:
		return iwl_mvm_mac_ctxt_cmd_listener(mvm, vif, action);
	case NL80211_IFTYPE_P2P_DEVICE:
		return iwl_mvm_mac_ctxt_cmd_p2p_device(mvm, vif, action);
	case NL80211_IFTYPE_ADHOC:
		return iwl_mvm_mac_ctxt_cmd_ibss(mvm, vif, action);
	default:
		break;
	}

	return -EOPNOTSUPP;
}

int iwl_mvm_mac_ctxt_add(struct iwl_mvm *mvm, struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	int ret;

	if (WARN_ONCE(mvmvif->uploaded, "Adding active MAC %pM/%d\n",
		      vif->addr, ieee80211_vif_type_p2p(vif)))
		return -EIO;

	ret = iwl_mvm_mac_ctx_send(mvm, vif, FW_CTXT_ACTION_ADD,
				   true);
	if (ret)
		return ret;

	/* will only do anything at resume from D3 time */
	iwl_mvm_set_last_nonqos_seq(mvm, vif);

	mvmvif->uploaded = true;
	return 0;
}

int iwl_mvm_mac_ctxt_changed(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			     bool force_assoc_off)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);

	if (WARN_ONCE(!mvmvif->uploaded, "Changing inactive MAC %pM/%d\n",
		      vif->addr, ieee80211_vif_type_p2p(vif)))
		return -EIO;

	return iwl_mvm_mac_ctx_send(mvm, vif, FW_CTXT_ACTION_MODIFY,
				    force_assoc_off);
}

int iwl_mvm_mac_ctxt_remove(struct iwl_mvm *mvm, struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mac_ctx_cmd cmd;
	int ret;

	if (WARN_ONCE(!mvmvif->uploaded, "Removing inactive MAC %pM/%d\n",
		      vif->addr, ieee80211_vif_type_p2p(vif)))
		return -EIO;

	memset(&cmd, 0, sizeof(cmd));

	cmd.id_and_color = cpu_to_le32(FW_CMD_ID_AND_COLOR(mvmvif->id,
							   mvmvif->color));
	cmd.action = cpu_to_le32(FW_CTXT_ACTION_REMOVE);

	ret = iwl_mvm_send_cmd_pdu(mvm, MAC_CONTEXT_CMD, 0,
				   sizeof(cmd), &cmd);
	if (ret) {
		IWL_ERR(mvm, "Failed to remove MAC context: %d\n", ret);
		return ret;
	}

	mvmvif->uploaded = false;

	if (vif->type == NL80211_IFTYPE_MONITOR)
		mvm->hw->flags &= ~IEEE80211_HW_RX_INCLUDES_FCS;

	return 0;
}

int iwl_mvm_rx_beacon_notif(struct iwl_mvm *mvm,
			    struct iwl_rx_cmd_buffer *rxb,
			    struct iwl_device_cmd *cmd)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_beacon_notif *beacon = (void *)pkt->data;
	u16 status __maybe_unused =
		le16_to_cpu(beacon->beacon_notify_hdr.status.status);
	u32 rate __maybe_unused =
		le32_to_cpu(beacon->beacon_notify_hdr.initial_rate);

	lockdep_assert_held(&mvm->mutex);

	IWL_DEBUG_RX(mvm, "beacon status %#x retries:%d tsf:0x%16llX rate:%d\n",
		     status & TX_STATUS_MSK,
		     beacon->beacon_notify_hdr.failure_frame,
		     le64_to_cpu(beacon->tsf),
		     rate);

	if (unlikely(mvm->csa_vif && mvm->csa_vif->csa_active)) {
		if (!ieee80211_csa_is_complete(mvm->csa_vif)) {
			iwl_mvm_mac_ctxt_beacon_changed(mvm, mvm->csa_vif);
		} else {
			ieee80211_csa_finish(mvm->csa_vif);
			mvm->csa_vif = NULL;
		}
	}

	return 0;
}

static void iwl_mvm_beacon_loss_iterator(void *_data, u8 *mac,
					 struct ieee80211_vif *vif)
{
	struct iwl_missed_beacons_notif *missed_beacons = _data;
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);

	if (mvmvif->id != (u16)le32_to_cpu(missed_beacons->mac_id))
		return;

	/*
	 * TODO: the threshold should be adjusted based on latency conditions,
	 * and/or in case of a CS flow on one of the other AP vifs.
	 */
	if (le32_to_cpu(missed_beacons->consec_missed_beacons_since_last_rx) >
	     IWL_MVM_MISSED_BEACONS_THRESHOLD)
		ieee80211_beacon_loss(vif);
}

int iwl_mvm_rx_missed_beacons_notif(struct iwl_mvm *mvm,
				    struct iwl_rx_cmd_buffer *rxb,
				    struct iwl_device_cmd *cmd)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_missed_beacons_notif *mb = (void *)pkt->data;

	IWL_DEBUG_INFO(mvm,
		       "missed bcn mac_id=%u, consecutive=%u (%u, %u, %u)\n",
		       le32_to_cpu(mb->mac_id),
		       le32_to_cpu(mb->consec_missed_beacons),
		       le32_to_cpu(mb->consec_missed_beacons_since_last_rx),
		       le32_to_cpu(mb->num_recvd_beacons),
		       le32_to_cpu(mb->num_expected_beacons));

	ieee80211_iterate_active_interfaces_atomic(mvm->hw,
						   IEEE80211_IFACE_ITER_NORMAL,
						   iwl_mvm_beacon_loss_iterator,
						   mb);
	return 0;
}
