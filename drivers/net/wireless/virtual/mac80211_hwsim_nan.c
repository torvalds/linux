// SPDX-License-Identifier: GPL-2.0-only
/*
 * mac80211_hwsim_nan - NAN software simulation for mac80211_hwsim
 * Copyright (C) 2025-2026 Intel Corporation
 */

#include <net/cfg80211.h>
#include "mac80211_hwsim_i.h"

/* Defined as the lower 23 bits being zero */
#define DW0_TSF_MASK		GENMASK(22, 0)

/* DWs are repeated every 512 TUs */
#define DWST_TU			512
#define DWST_TSF_MASK		(ieee80211_tu_to_usec(DWST_TU) - 1)

#define SLOT_TU			16
#define SLOT_TSF_MASK		(ieee80211_tu_to_usec(DWST_TU) - 1)

/* The 2.4 GHz DW is at the start, the 5 GHz is in slot 8 (after 128 TUs) */
#define DW_5G_OFFSET_TU		128

#define SLOT_24GHZ_DW		0
#define SLOT_5GHZ_DW		(DW_5G_OFFSET_TU / SLOT_TU)

/* The special DW0 happens every 16 DWSTs (8192 TUs) */
static_assert(16 * DWST_TU * 1024 == 8192 * 1024);
static_assert(DW0_TSF_MASK + 1 == 8192 * 1024);

/* warmup phase should be 120 seconds, which is approximately 225 DWSTs */
#define NAN_WARMUP_DWST		225

#define NAN_RSSI_CLOSE (-60)
#define NAN_RSSI_MIDDLE (-75)

/* Quiet time at the end of each slot where TX is suppressed */
#define NAN_CHAN_SWITCH_TIME_US		256

struct hwsim_nan_sta_iter_ctx {
	struct ieee80211_hw *hw;
	bool can_tx;
};

struct hwsim_nan_mcast_data_iter_ctx {
	struct ieee80211_hw *hw;
	struct ieee80211_vif *vif;
	size_t n_vif_sta;
	size_t n_sta_can_tx;
};

static void mac80211_hwsim_nan_resume_txqs(struct mac80211_hwsim_data *data);

static u64 hwsim_nan_get_timer_tsf(struct mac80211_hwsim_data *data)
{
	ktime_t expires = hrtimer_get_expires(&data->nan.slot_timer);

	return mac80211_hwsim_boottime_to_tsf(data, expires);
}

static u8 hwsim_nan_slot_from_tsf(u64 tsf)
{
	return (tsf & DWST_TSF_MASK) / ieee80211_tu_to_usec(SLOT_TU);
}

static u64 hwsim_nan_encode_master_rank(u8 master_pref, u8 random_factor,
					const u8 *addr)
{
	return ((u64)master_pref << 56) +
		((u64)random_factor << 48) +
		((u64)addr[5] << 40) +
		((u64)addr[4] << 32) +
		((u64)addr[3] << 24) +
		((u64)addr[2] << 16) +
		((u64)addr[1] << 8) +
		((u64)addr[0] << 0);
}

static u64 hwsim_nan_get_master_rank(struct mac80211_hwsim_data *data)
{
	u8 master_pref = 0;
	u8 random_factor = 0;

	if (data->nan.phase == MAC80211_HWSIM_NAN_PHASE_UP) {
		master_pref = data->nan.master_pref;
		random_factor = data->nan.random_factor;
	}

	return hwsim_nan_encode_master_rank(master_pref, random_factor,
					    data->nan.device_vif->addr);
}

static bool mac80211_hwsim_nan_is_dw_slot(struct mac80211_hwsim_data *data,
					  u8 slot)
{
	return slot == SLOT_24GHZ_DW ||
		(slot == SLOT_5GHZ_DW &&
		 (data->nan.bands & BIT(NL80211_BAND_5GHZ)));
}

static bool
hwsim_nan_rx_chandef_compatible(struct mac80211_hwsim_data *data, u8 slot,
				struct ieee80211_channel *rx_chan, u8 rx_bw)
{
	static const int bw_to_mhz[] = {
		[RATE_INFO_BW_20] = 20, [RATE_INFO_BW_40] = 40,
		[RATE_INFO_BW_80] = 80, [RATE_INFO_BW_160] = 160,
	};
	struct cfg80211_chan_def sched_chandef;
	int rx_mhz, sched_mhz;

	scoped_guard(spinlock_bh, &data->nan.state_lock)
		sched_chandef = data->nan.local_sched[slot];

	if (!sched_chandef.chan ||
	    sched_chandef.chan->center_freq != rx_chan->center_freq)
		return false;

	if (rx_bw >= ARRAY_SIZE(bw_to_mhz) || !bw_to_mhz[rx_bw])
		return false;

	rx_mhz = bw_to_mhz[rx_bw];
	sched_mhz = cfg80211_chandef_get_width(&sched_chandef);

	/* Accept RX at narrower or equal bandwidth */
	return rx_mhz <= sched_mhz;
}

static bool hwsim_nan_peer_present_in_dw(struct hwsim_sta_priv *sp, u64 tsf)
{
	u8 slot = hwsim_nan_slot_from_tsf(tsf);
	u8 cdw = 0;
	u8 dw_index, wake_interval;
	u16 committed_dw;

	scoped_guard(spinlock_bh, &sp->nan_sched.lock)
		committed_dw = sp->nan_sched.committed_dw;

	/* If peer doesn't advertise committed DW, assume presence in
	 * all 2.4 GHz DW slots
	 */
	if (!committed_dw)
		return slot == SLOT_24GHZ_DW;

	/* Get DW index (0-15) within the 16-DWST DW0 cycle */
	dw_index = (tsf / ieee80211_tu_to_usec(DWST_TU)) & 0xf;

	/* Extract CDW for the appropriate band (spec Table 80) */
	if (slot == SLOT_24GHZ_DW)
		cdw = committed_dw & 0x7;
	else if (slot == SLOT_5GHZ_DW)
		cdw = (committed_dw >> 3) & 0x7;

	if (cdw == 0)
		return false;

	/* Peer wakes every 2^(cdw-1) DWs: 1, 2, 4, 8, or 16 */
	wake_interval = 1 << (cdw - 1);

	return (dw_index % wake_interval) == 0;
}

static bool
hwsim_nan_peer_present_in_faw(struct hwsim_sta_priv *sp,
			      struct mac80211_hwsim_data *data, u8 slot)
{
	struct cfg80211_chan_def local_chandef;

	scoped_guard(spinlock_bh, &data->nan.state_lock)
		local_chandef = data->nan.local_sched[slot];

	if (!local_chandef.chan)
		return false;

	scoped_guard(spinlock_bh, &sp->nan_sched.lock) {
		for (int i = 0; i < CFG80211_NAN_MAX_PEER_MAPS; i++) {
			struct cfg80211_chan_def *peer_chandef;

			if (sp->nan_sched.maps[i].map_id ==
			    CFG80211_NAN_INVALID_MAP_ID)
				continue;

			peer_chandef = &sp->nan_sched.maps[i].chans[slot];
			if (!peer_chandef->chan)
				continue;

			if (cfg80211_chandef_compatible(&local_chandef,
							peer_chandef))
				return true;
		}
	}

	return false;
}

static void
mac80211_hwsim_nan_schedule_slot(struct mac80211_hwsim_data *data, u8 slot,
				 bool discontinuity)
{
	u64 tsf;

	if (!discontinuity)
		tsf = hwsim_nan_get_timer_tsf(data);
	else
		tsf = mac80211_hwsim_get_tsf(data->hw, data->nan.device_vif);

	/* Only called by mac80211_hwsim_nan_dw_timer from softirq context */
	lockdep_assert_in_softirq();

	tsf &= ~DWST_TSF_MASK;
	tsf += ieee80211_tu_to_usec(slot * SLOT_TU);

	hrtimer_set_expires(&data->nan.slot_timer,
			    mac80211_hwsim_tsf_to_boottime(data, tsf));
}

void mac80211_hwsim_nan_rx(struct ieee80211_hw *hw,
			   struct sk_buff *skb)
{
	struct mac80211_hwsim_data *data = hw->priv;
	const struct ieee80211_mgmt *mgmt = (void *)skb->data;
	struct element *nan_elem = (void *)mgmt->u.beacon.variable;
	struct ieee80211_nan_anchor_master_info *ami = NULL;
	const struct ieee80211_nan_attr *nan_attr;
	struct ieee80211_rx_status rx_status;
	bool joined_cluster = false;
	bool adopt_tsf = false;
	bool is_sync_beacon;
	bool is_same_cluster;
	u64 master_rank = 0;
	ssize_t data_len;
	u8 slot;

	/* Need a NAN vendor element at the start */
	if (skb->len < (offsetofend(struct ieee80211_mgmt, u.beacon) + 6) ||
	    !ieee80211_is_beacon(mgmt->frame_control))
		return;

	data_len = skb->len - offsetofend(struct ieee80211_mgmt, u.beacon);

	/* Copy the RX status to add a MAC timestamp if needed */
	memcpy(&rx_status, IEEE80211_SKB_RXCB(skb),
	       sizeof(struct ieee80211_rx_status));

	/* And deal with the lack of mac time stamp */
	if ((rx_status.flag & RX_FLAG_MACTIME) != RX_FLAG_MACTIME_START) {
		u64 tsf = mac80211_hwsim_get_tsf(hw, data->nan.device_vif);;

		/* In that case there should be no timestamp */
		WARN_ON_ONCE(rx_status.flag & RX_FLAG_MACTIME);

		/* No mac timestamp, set current TSF for the frame end */
		rx_status.flag |= RX_FLAG_MACTIME_END;
		rx_status.mactime = tsf;

		/* And translate to the start for the rest of the code */
		rx_status.mactime =
			ieee80211_calculate_rx_timestamp(hw, &rx_status,
							 skb->len, 0);
		rx_status.flag &= ~RX_FLAG_MACTIME;
		rx_status.flag |= RX_FLAG_MACTIME_START;

		/* Match mac80211_hwsim_nan_receive, see comment there */
		slot = hwsim_nan_slot_from_tsf(tsf + 128);
	} else {
		slot = hwsim_nan_slot_from_tsf(rx_status.mactime);
	}

	/*
	 * (overly) simplify things, only track 2.4 GHz here. Also, ignore
	 * frames outside of the 2.4 GHz DW slot, unless in the initial SCAN
	 * phase.
	 */
	if ((slot != SLOT_24GHZ_DW &&
	     data->nan.phase != MAC80211_HWSIM_NAN_PHASE_SCAN) ||
	    rx_status.freq != 2437)
		return;

	/* Just ignore low RSSI beacons that we cannot sync to */
	if (rx_status.signal < NAN_RSSI_MIDDLE)
		return;

	/* Needs to be a valid NAN cluster ID in A3 */
	if (get_unaligned_be32(mgmt->bssid) != ((WLAN_OUI_WFA << 8) | 0x01))
		return;

	/* We are only interested in NAN beacons */
	if (nan_elem->id != WLAN_EID_VENDOR_SPECIFIC ||
	    nan_elem->datalen < 4 ||
	    get_unaligned_be32(nan_elem->data) !=
	    (WLAN_OUI_WFA << 8 | WLAN_OUI_TYPE_WFA_NAN))
		return;

	u8 *nan_defragmented __free(kfree) = kzalloc(data_len, GFP_ATOMIC);
	if (!nan_defragmented)
		return;

	data_len = cfg80211_defragment_element(nan_elem,
					       mgmt->u.beacon.variable,
					       data_len,
					       nan_defragmented, data_len,
					       WLAN_EID_FRAGMENT);

	if (data_len < 0)
		return;

	/* Assume it is a synchronization beacon if beacon_int is 512 TUs */
	is_sync_beacon = le16_to_cpu(mgmt->u.beacon.beacon_int) == DWST_TU;
	is_same_cluster = ether_addr_equal(mgmt->bssid, data->nan.cluster_id);

	for_each_nan_attr(nan_attr, nan_defragmented + 4, data_len - 4) {
		if (nan_attr->attr == NAN_ATTR_MASTER_INDICATION &&
		    le16_to_cpu(nan_attr->length) >=
		    sizeof(struct ieee80211_nan_master_indication)) {
			struct ieee80211_nan_master_indication *mi =
				(void *)nan_attr->data;

			master_rank =
				hwsim_nan_encode_master_rank(mi->master_pref,
							     mi->random_factor,
							     mgmt->sa);
		}

		if (nan_attr->attr == NAN_ATTR_CLUSTER_INFO &&
		    le16_to_cpu(nan_attr->length) >=
		    sizeof(struct ieee80211_nan_anchor_master_info)) {
			ami = (void *)nan_attr->data;

			/*
			 * The AMBTT should be set to the beacon timestamp when
			 * the sender is the anchor master. We can simply
			 * modify the structure because we created a copy when
			 * defragmenting the NAN element.
			 */
			if (ami->hop_count == 0)
				ami->ambtt = cpu_to_le32(
					le64_to_cpu(mgmt->u.beacon.timestamp));
		}
	}

	/* Do the rest of the processing under lock */
	spin_lock_bh(&data->nan.state_lock);

	/*
	 * sync beacon should be discarded if the master rank is the same
	 * and the AMBTT is older than 16 * 512 TUs compared to our own TSF.
	 *
	 * Subtract the AMBTT from the lowered TSF. If the AMBTT is older
	 * (smaller) then the calculation will not underflow.
	 */
	if (is_sync_beacon && ami &&
	    ami->master_rank == data->nan.current_ami.master_rank &&
	    (((u32)rx_status.mactime -
	      ieee80211_tu_to_usec(16 * 512)) -
	     le32_to_cpu(ami->ambtt)) < 0x8000000) {
		wiphy_dbg(hw->wiphy,
			  "NAN: ignoring sync beacon with old AMBTT\n");
		is_sync_beacon = false;
	}

	if (is_same_cluster && is_sync_beacon &&
	    master_rank > hwsim_nan_get_master_rank(data)) {
		if (rx_status.signal > NAN_RSSI_CLOSE)
			data->nan.master_transition_score += 3;
		else
			data->nan.master_transition_score += 1;
	}

	if (is_same_cluster && is_sync_beacon && ami &&
	    ((ami->master_rank == data->nan.current_ami.master_rank &&
	      ami->hop_count < data->nan.current_ami.hop_count) ||
	     (master_rank > hwsim_nan_get_master_rank(data) &&
	      ami->hop_count == data->nan.current_ami.hop_count))) {
		if (rx_status.signal > NAN_RSSI_CLOSE)
			data->nan.sync_transition_score += 3;
		else
			data->nan.sync_transition_score += 1;
	}

	/*
	 * Decide on TSF adjustments before updating any other state
	 */
	if (is_same_cluster && is_sync_beacon && ami &&
	    data->nan.current_ami.hop_count != 0) {
		if (le64_to_cpu(ami->master_rank) >
		    le64_to_cpu(data->nan.current_ami.master_rank) &&
		    ami->master_rank != data->nan.last_ami.master_rank)
			adopt_tsf = true;

		if (le64_to_cpu(ami->master_rank) >
		    le64_to_cpu(data->nan.current_ami.master_rank) &&
		    ami->master_rank == data->nan.last_ami.master_rank &&
		    le32_to_cpu(ami->ambtt) >
		    le32_to_cpu(data->nan.last_ami.ambtt))
			adopt_tsf = true;

		if (le64_to_cpu(ami->master_rank) <
		    le64_to_cpu(data->nan.current_ami.master_rank) &&
		    le64_to_cpu(ami->master_rank) >
		    hwsim_nan_get_master_rank(data) &&
		    ether_addr_equal(ami->master_addr,
				     data->nan.current_ami.master_addr))
			adopt_tsf = true;

		if (ami->master_rank == data->nan.current_ami.master_rank &&
		    le32_to_cpu(ami->ambtt) >
		    le32_to_cpu(data->nan.current_ami.ambtt))
			adopt_tsf = true;

		/* Anchor Master case is handled below */
	}

	/*
	 * NAN Cluster merging
	 */
	if (!is_same_cluster && ami) {
		u64 curr_amr;
		u64 own_cg;
		u64 frame_amr;
		u64 cg;

		/* Shifted down by 19 bits compared to spec */
		frame_amr = le64_to_cpu(ami->master_rank);
		cg = (u64)ami->master_pref << (64 - 19);
		cg += le64_to_cpu(mgmt->u.beacon.timestamp) >> 19;

		curr_amr = le64_to_cpu(data->nan.current_ami.master_rank);
		own_cg = (u64)data->nan.current_ami.master_pref << (64 - 19);
		own_cg += rx_status.mactime >> 19;

		/*
		 * Check if the cluster shall be joined
		 *
		 * When in the "scan" phase, just join immediately.
		 */
		if (cg > own_cg ||
		    (cg == own_cg && frame_amr > curr_amr) ||
		    data->nan.phase == MAC80211_HWSIM_NAN_PHASE_SCAN) {
			/* Avoid a state transition */
			data->nan.master_transition_score = 0;
			data->nan.sync_transition_score = 0;

			/*
			 * NOTE: The spec says we should TX sync beacons on the
			 * old schedule after joining. We do not implement this.
			 */

			wiphy_dbg(hw->wiphy, "NAN: joining cluster %pM\n",
				  mgmt->bssid);

			joined_cluster = true;
			adopt_tsf = true;

			memcpy(&data->nan.last_ami, &data->nan.current_ami,
			       sizeof(data->nan.last_ami));
			memcpy(&data->nan.current_ami, ami,
			       sizeof(data->nan.last_ami));
			data->nan.current_ami.hop_count += 1;

			memcpy(data->nan.cluster_id, mgmt->bssid, ETH_ALEN);

			/*
			 * Assume we are UP if we joined a cluster.
			 *
			 * If the other anchor master is still in the warmup
			 * phase, then we may temporarily become the anchor
			 * master until it sets its own master preference to
			 * be non-zero.
			 */
			data->nan.phase = MAC80211_HWSIM_NAN_PHASE_UP;
			data->nan.random_factor_valid_dwst = 0;
		}
	}

	/*
	 * Anchor master selection
	 */
	/* We are not anchor master */
	if (is_same_cluster && is_sync_beacon && ami &&
	    data->nan.current_ami.hop_count != 0) {
		if (le64_to_cpu(data->nan.current_ami.master_rank) <
		    le64_to_cpu(ami->master_rank)) {
			if (ami->master_rank == data->nan.last_ami.master_rank &&
			    le32_to_cpu(ami->ambtt) <=
			    le32_to_cpu(data->nan.last_ami.ambtt)) {
				/* disregard frame */
			} else {
				memcpy(&data->nan.last_ami,
				       &data->nan.current_ami,
				       sizeof(data->nan.last_ami));
				memcpy(&data->nan.current_ami, ami,
				       sizeof(data->nan.last_ami));
				data->nan.current_ami.hop_count += 1;
			}
		}

		if (le64_to_cpu(data->nan.current_ami.master_rank) >
		    le64_to_cpu(ami->master_rank)) {
			if (!ether_addr_equal(data->nan.current_ami.master_addr,
					      ami->master_addr)) {
				/* disregard frame */
			} else {
				u64 amr = hwsim_nan_get_master_rank(data);

				if (amr > le64_to_cpu(ami->master_rank)) {
					/* assume ourselves as anchor master */
					wiphy_dbg(hw->wiphy,
						  "NAN: assume anchor master role\n");
					data->nan.current_ami.master_rank =
						cpu_to_le64(amr);
					data->nan.current_ami.hop_count = 0;
					memset(&data->nan.last_ami, 0,
					       sizeof(data->nan.last_ami));
					data->nan.last_ami.ambtt =
						data->nan.current_ami.ambtt;
					data->nan.current_ami.ambtt = 0;
				} else {
					memcpy(&data->nan.last_ami,
					       &data->nan.current_ami,
					       sizeof(data->nan.last_ami));
					memcpy(&data->nan.current_ami, ami,
					       sizeof(data->nan.last_ami));
					data->nan.current_ami.hop_count += 1;
				}
			}
		}

		if (data->nan.current_ami.master_rank == ami->master_rank) {
			if (le32_to_cpu(data->nan.current_ami.ambtt) <
			    le32_to_cpu(ami->ambtt)) {
				data->nan.current_ami.ambtt = ami->ambtt;
			}

			if (data->nan.current_ami.hop_count >
			    ami->hop_count + 1) {
				data->nan.current_ami.hop_count =
					ami->hop_count + 1;
			}
		}
	}

	/* We are anchor master */
	if (is_same_cluster && is_sync_beacon && ami &&
	    data->nan.current_ami.hop_count == 0) {
		WARN_ON_ONCE(!ether_addr_equal(data->nan.current_ami.master_addr,
					       data->nan.device_vif->addr));

		if (le64_to_cpu(ami->master_rank) <
		    le64_to_cpu(data->nan.current_ami.master_rank) ||
		    ether_addr_equal(ami->master_addr,
				     data->nan.current_ami.master_addr)) {
			/* disregard */
		} else {
			wiphy_dbg(hw->wiphy, "NAN: lost anchor master role\n");
			adopt_tsf = true;
			memcpy(&data->nan.last_ami, &data->nan.current_ami,
			       sizeof(data->nan.last_ami));
			memcpy(&data->nan.current_ami, ami,
			       sizeof(data->nan.last_ami));
			data->nan.current_ami.hop_count += 1;
		}
	}

	if (adopt_tsf && !data->nan.tsf_adjusted) {
		int threshold = 5;
		s64 adjustment;

		/* Timestamp is likely inaccurate (and late) in this case */
		if (!(IEEE80211_SKB_RXCB(skb)->flag & RX_FLAG_MACTIME))
			threshold = 128;

		adjustment =
			le64_to_cpu(mgmt->u.beacon.timestamp) -
			ieee80211_calculate_rx_timestamp(hw, &rx_status,
							 skb->len, 24);

		scoped_guard(spinlock_bh, &data->tsf_offset_lock) {
			if (adjustment < -threshold || adjustment > threshold) {
				if (adjustment < -(s64)ieee80211_tu_to_usec(4) ||
				    adjustment > (s64)ieee80211_tu_to_usec(4))
					data->nan.tsf_discontinuity = true;

				wiphy_debug(hw->wiphy,
					    "NAN: Adjusting TSF by +/- %d us or more: %lld us (discontinuity: %d, from: %pM, old offset: %lld)\n",
					    threshold, adjustment,
					    data->nan.tsf_discontinuity, mgmt->sa,
					    data->tsf_offset);
			} else {
				/* smooth things out a little bit */
				adjustment /= 2;
			}

			/*
			 * Do the TSF adjustment
			 * The flag prevents further adjustments until the next
			 * 2.4 GHz DW starts to avoid race conditions for
			 * in-flight packets.
			 */
			data->nan.tsf_adjusted = true;
			data->tsf_offset += adjustment;
		}
	}

	spin_unlock_bh(&data->nan.state_lock);

	if (joined_cluster)
		ieee80211_nan_cluster_joined(data->nan.device_vif,
					     data->nan.cluster_id, false,
					     GFP_ATOMIC);
}

static void
mac80211_hwsim_nan_exec_state_transitions(struct mac80211_hwsim_data *data)
{
	bool notify_join = false;

	/*
	 * Handle NAN role and state transitions at the end of the DW period
	 * in accordance to Wi-Fi Aware version 4.0 section 3.3.7 point 2, i.e.
	 * end of 5 GHz DW if enabled else at the end of the 2.4 GHz DW.
	 */

	spin_lock(&data->nan.state_lock);

	/* Handle role transitions, Wi-Fi Aware version 4.0 section 3.3.6  */
	if (data->nan.master_transition_score < 3)
		data->nan.role = MAC80211_HWSIM_NAN_ROLE_MASTER;
	else if (data->nan.role == MAC80211_HWSIM_NAN_ROLE_MASTER &&
		 data->nan.master_transition_score >= 3)
		data->nan.role = MAC80211_HWSIM_NAN_ROLE_SYNC;
	else if (data->nan.role == MAC80211_HWSIM_NAN_ROLE_SYNC &&
		 data->nan.sync_transition_score >= 3)
		data->nan.role = MAC80211_HWSIM_NAN_ROLE_NON_SYNC;
	else if (data->nan.role == MAC80211_HWSIM_NAN_ROLE_NON_SYNC &&
		 data->nan.sync_transition_score < 3)
		data->nan.role = MAC80211_HWSIM_NAN_ROLE_SYNC;

	/*
	 * The discovery beacon timer will stop automatically. Make sure it is
	 * running if we are master. Do not bother with a proper alignment it
	 * will sync itself to the TSF after the first TX.
	 */
	if (data->nan.role == MAC80211_HWSIM_NAN_ROLE_MASTER &&
	    !hrtimer_active(&data->nan.discovery_beacon_timer))
		hrtimer_start(&data->nan.discovery_beacon_timer,
			      ns_to_ktime(10 * NSEC_PER_USEC),
			      HRTIMER_MODE_REL_SOFT);

	data->nan.master_transition_score = 0;
	data->nan.sync_transition_score = 0;

	if (data->nan.random_factor_valid_dwst == 0) {
		u64 amr;

		if (data->nan.phase == MAC80211_HWSIM_NAN_PHASE_SCAN) {
			data->nan.phase = MAC80211_HWSIM_NAN_PHASE_WARMUP;
			data->nan.random_factor_valid_dwst = NAN_WARMUP_DWST;

			notify_join = true;
		} else {
			data->nan.phase = MAC80211_HWSIM_NAN_PHASE_UP;
			data->nan.random_factor_valid_dwst =
				get_random_u32_inclusive(120, 240);
			data->nan.random_factor = get_random_u8();
		}

		amr = hwsim_nan_get_master_rank(data);

		if (data->nan.current_ami.hop_count == 0) {
			/* Update if we are already anchor master */
			data->nan.current_ami.master_rank = cpu_to_le64(amr);
		} else if (le64_to_cpu(data->nan.current_ami.master_rank) < amr) {
			/* assume role if we have a higher rank */
			wiphy_dbg(data->hw->wiphy,
				  "NAN: assume anchor master role\n");
			data->nan.current_ami.master_rank = cpu_to_le64(amr);
			data->nan.current_ami.hop_count = 0;
			memset(&data->nan.last_ami, 0,
			       sizeof(data->nan.last_ami));
			data->nan.last_ami.ambtt = data->nan.current_ami.ambtt;
			data->nan.current_ami.ambtt = 0;
		}
	} else {
		data->nan.random_factor_valid_dwst--;
	}

	spin_unlock(&data->nan.state_lock);

	if (notify_join)
		ieee80211_nan_cluster_joined(data->nan.device_vif,
					     data->nan.cluster_id, true,
					     GFP_ATOMIC);
}

static void
mac80211_hwsim_nan_tx_beacon(struct mac80211_hwsim_data *data,
			     bool is_discovery,
			     struct ieee80211_channel *channel)
{
	struct ieee80211_vendor_ie nan_ie = {
		.element_id = WLAN_EID_VENDOR_SPECIFIC,
		.len = 27 - 2,
		.oui = { u32_get_bits(WLAN_OUI_WFA, 0xff0000),
			 u32_get_bits(WLAN_OUI_WFA, 0xff00),
			 u32_get_bits(WLAN_OUI_WFA, 0xff) },
		.oui_type = WLAN_OUI_TYPE_WFA_NAN,
	};
	size_t alloc_size =
		IEEE80211_TX_STATUS_HEADROOM +
		offsetofend(struct ieee80211_mgmt, u.beacon) +
		27 /* size of NAN vendor element */;
	struct ieee80211_nan_master_indication master_indication;
	struct ieee80211_nan_attr nan_attr;
	struct ieee80211_mgmt *mgmt;
	struct sk_buff *skb;

	/*
	 * TODO: Should the configured vendor elements or NAN attributes be
	 * included in some of these beacons?
	 */

	skb = alloc_skb(alloc_size, GFP_ATOMIC);
	if (!skb)
		return;

	spin_lock(&data->nan.state_lock);

	skb_reserve(skb, IEEE80211_TX_STATUS_HEADROOM);
	mgmt = skb_put(skb, offsetofend(struct ieee80211_mgmt, u.beacon));

	memset(mgmt, 0, offsetofend(struct ieee80211_mgmt, u.beacon));
	memcpy(mgmt->sa, data->nan.device_vif->addr, ETH_ALEN);
	memset(mgmt->da, 0xff, ETH_ALEN);
	memcpy(mgmt->bssid, data->nan.cluster_id, ETH_ALEN);

	mgmt->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
					  IEEE80211_STYPE_BEACON);
	mgmt->u.beacon.beacon_int = cpu_to_le16(is_discovery ? 100 : DWST_TU);
	mgmt->u.beacon.capab_info =
		cpu_to_le16(WLAN_CAPABILITY_SHORT_SLOT_TIME |
			    WLAN_CAPABILITY_SHORT_PREAMBLE);

	/* FIXME: set these to saner values? */
	mgmt->duration = 0;
	mgmt->seq_ctrl = 0;

	/* Put the NAN element */
	skb_put_data(skb, &nan_ie, sizeof(nan_ie));

	nan_attr.attr = NAN_ATTR_MASTER_INDICATION;
	nan_attr.length = cpu_to_le16(sizeof(master_indication));
	if (data->nan.phase == MAC80211_HWSIM_NAN_PHASE_UP) {
		master_indication.master_pref = data->nan.master_pref;
		master_indication.random_factor = data->nan.random_factor;
	} else {
		master_indication.master_pref = 0;
		master_indication.random_factor = 0;
	}

	skb_put_data(skb, &nan_attr, sizeof(nan_attr));
	skb_put_data(skb, &master_indication, sizeof(master_indication));

	nan_attr.attr = NAN_ATTR_CLUSTER_INFO;
	nan_attr.length = cpu_to_le16(sizeof(data->nan.current_ami));
	skb_put_data(skb, &nan_attr, sizeof(nan_attr));
	skb_put_data(skb, &data->nan.current_ami,
		     sizeof(data->nan.current_ami));

	spin_unlock(&data->nan.state_lock);

	mac80211_hwsim_tx_frame(data->hw, skb, channel);
}

enum hrtimer_restart
mac80211_hwsim_nan_slot_timer(struct hrtimer *timer)
{
	struct mac80211_hwsim_data *data =
		container_of(timer, struct mac80211_hwsim_data,
			     nan.slot_timer);
	struct ieee80211_hw *hw = data->hw;
	struct ieee80211_channel *notify_dw_chan = NULL;
	struct ieee80211_channel *beacon_sync_chan = NULL;
	u64 tsf = hwsim_nan_get_timer_tsf(data);
	u8 slot = hwsim_nan_slot_from_tsf(tsf);
	bool dwst_of_dw0 = false;
	bool dw_end = false;
	bool tx_sync_beacon;

	if (!data->nan.device_vif)
		return HRTIMER_NORESTART;

	if ((tsf & DW0_TSF_MASK & ~DWST_TSF_MASK) == 0)
		dwst_of_dw0 = true;


	scoped_guard(spinlock, &data->nan.state_lock) {
		if (data->nan.tsf_discontinuity) {
			data->nan.tsf_discontinuity = false;

			mac80211_hwsim_nan_schedule_slot(data, 32, true);

			return HRTIMER_RESTART;
		}

		if (slot == SLOT_24GHZ_DW)
			data->nan.tsf_adjusted = false;

		tx_sync_beacon =
			data->nan.phase != MAC80211_HWSIM_NAN_PHASE_SCAN &&
			data->nan.role != MAC80211_HWSIM_NAN_ROLE_NON_SYNC;
	}

	switch (slot) {
	case SLOT_24GHZ_DW:
		wiphy_dbg(data->hw->wiphy, "Start of 2.4 GHz DW, is DW0=%d\n",
			  dwst_of_dw0);
		beacon_sync_chan = ieee80211_get_channel(hw->wiphy, 2437);
		break;

	case SLOT_24GHZ_DW + 1:
		if (!(data->nan.bands & BIT(NL80211_BAND_5GHZ))) {
			notify_dw_chan = ieee80211_get_channel(hw->wiphy, 2437);
			dw_end = true;
		} else {
			notify_dw_chan = ieee80211_get_channel(hw->wiphy, 5745);
		}
		break;

	case SLOT_5GHZ_DW:
		if (data->nan.bands & BIT(NL80211_BAND_5GHZ)) {
			wiphy_dbg(data->hw->wiphy, "Start of 5 GHz DW\n");
			beacon_sync_chan =
				ieee80211_get_channel(hw->wiphy, 5745);
		}
		break;

	case SLOT_5GHZ_DW + 1:
		if (data->nan.bands & BIT(NL80211_BAND_5GHZ)) {
			notify_dw_chan =
				ieee80211_get_channel(hw->wiphy, 2437);
			dw_end = true;
		}
		break;
	}

	/* TODO: This does not implement DW contention mitigation */
	if (beacon_sync_chan && tx_sync_beacon)
		mac80211_hwsim_nan_tx_beacon(data, false, beacon_sync_chan);

	if (dw_end)
		mac80211_hwsim_nan_exec_state_transitions(data);

	if (data->nan.notify_dw && notify_dw_chan) {
		struct wireless_dev *wdev =
			ieee80211_vif_to_wdev(data->nan.device_vif);

		cfg80211_next_nan_dw_notif(wdev, notify_dw_chan, GFP_ATOMIC);
	}

	mac80211_hwsim_nan_resume_txqs(data);

	mac80211_hwsim_nan_schedule_slot(data, slot + 1, false);

	return HRTIMER_RESTART;
}

enum hrtimer_restart
mac80211_hwsim_nan_discovery_beacon_timer(struct hrtimer *timer)
{
	struct mac80211_hwsim_data *data =
		container_of(timer, struct mac80211_hwsim_data,
			     nan.discovery_beacon_timer);
	u32 remainder;
	u64 tsf_now;
	u64 tbtt;

	if (!data->nan.device_vif)
		return HRTIMER_NORESTART;

	scoped_guard(spinlock, &data->nan.state_lock) {
		if (data->nan.phase == MAC80211_HWSIM_NAN_PHASE_SCAN ||
		    data->nan.role != MAC80211_HWSIM_NAN_ROLE_MASTER)
			return HRTIMER_NORESTART;
	}

	mac80211_hwsim_nan_tx_beacon(
		data, true, ieee80211_get_channel(data->hw->wiphy, 2437));

	if (data->nan.bands & BIT(NL80211_BAND_5GHZ))
		mac80211_hwsim_nan_tx_beacon(
			data, true,
			ieee80211_get_channel(data->hw->wiphy, 5745));

	/* Read the TSF from the current time in case of adjustments */
	tsf_now = mac80211_hwsim_get_tsf(data->hw, data->nan.device_vif);

	/* Wrap value to be after the next TBTT */
	tbtt = tsf_now + ieee80211_tu_to_usec(100);

	/* Round TBTT down to the correct time */
	div_u64_rem(tbtt, ieee80211_tu_to_usec(100), &remainder);
	tbtt = tbtt - remainder;

	hrtimer_set_expires(&data->nan.discovery_beacon_timer,
			    mac80211_hwsim_tsf_to_boottime(data, tbtt));

	return HRTIMER_RESTART;
}

int mac80211_hwsim_nan_start(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif,
			     struct cfg80211_nan_conf *conf)
{
	struct mac80211_hwsim_data *data = hw->priv;

	if (vif->type != NL80211_IFTYPE_NAN)
		return -EINVAL;

	if (data->nan.device_vif)
		return -EALREADY;

	/* set this before starting the timer, as preemption might occur */
	data->nan.device_vif = vif;
	data->nan.bands = conf->bands;

	scoped_guard(spinlock_bh, &data->nan.state_lock) {
		/* Start in the "scan" phase and stay there for a little bit */
		data->nan.phase = MAC80211_HWSIM_NAN_PHASE_SCAN;
		data->nan.random_factor_valid_dwst = 1;
		data->nan.random_factor = 0;
		data->nan.master_pref = conf->master_pref;
		data->nan.role = MAC80211_HWSIM_NAN_ROLE_MASTER;
		memset(&data->nan.current_ami, 0,
		       sizeof(data->nan.current_ami));
		memset(&data->nan.last_ami, 0, sizeof(data->nan.last_ami));
		data->nan.current_ami.master_rank =
			cpu_to_le64(hwsim_nan_get_master_rank(data));
	}

	/* Just run this "soon" and start in a random schedule position */
	hrtimer_start(&data->nan.slot_timer,
		      ns_to_ktime(10 * NSEC_PER_USEC),
		      HRTIMER_MODE_REL_SOFT);

	ether_addr_copy(data->nan.cluster_id, conf->cluster_id);

	data->nan.notify_dw = conf->enable_dw_notification;

	return 0;
}

int mac80211_hwsim_nan_stop(struct ieee80211_hw *hw,
			    struct ieee80211_vif *vif)
{
	struct mac80211_hwsim_data *data = hw->priv;

	if (vif->type != NL80211_IFTYPE_NAN || !data->nan.device_vif ||
	    data->nan.device_vif != vif)
		return -EINVAL;

	hrtimer_cancel(&data->nan.slot_timer);
	hrtimer_cancel(&data->nan.resume_txqs_timer);
	hrtimer_cancel(&data->nan.discovery_beacon_timer);
	data->nan.device_vif = NULL;

	return 0;
}

int mac80211_hwsim_nan_change_config(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif,
				     struct cfg80211_nan_conf *conf,
				     u32 changes)
{
	struct mac80211_hwsim_data *data = hw->priv;

	if (vif->type != NL80211_IFTYPE_NAN)
		return -EINVAL;

	if (!data->nan.device_vif)
		return -EINVAL;

	wiphy_debug(hw->wiphy, "nan_config_changed: changes=0x%x\n", changes);

	/* Handle only the changes we care about for simulation purposes */
	if (changes & CFG80211_NAN_CONF_CHANGED_BANDS)
		data->nan.bands = conf->bands;

	if (changes & CFG80211_NAN_CONF_CHANGED_CONFIG)
		data->nan.notify_dw = conf->enable_dw_notification;

	if (changes & CFG80211_NAN_CONF_CHANGED_PREF) {
		scoped_guard(spinlock_bh, &data->nan.state_lock)
			data->nan.master_pref = conf->master_pref;
	}

	return 0;
}

static void hwsim_nan_can_sta_transmit(void *_ctx, struct ieee80211_sta *sta)
{
	struct hwsim_nan_sta_iter_ctx *ctx = _ctx;

	if (ctx->can_tx)
		return;

	for (int i = 0; i < ARRAY_SIZE(sta->txq); i++) {
		struct ieee80211_txq *txq = sta->txq[i];

		if (!txq)
			continue;

		if (txq->vif->type != NL80211_IFTYPE_NAN &&
		    txq->vif->type != NL80211_IFTYPE_NAN_DATA)
			return;

		if (mac80211_hwsim_nan_txq_transmitting(ctx->hw, txq)) {
			ctx->can_tx = true;
			return;
		}
	}
}

static void mac80211_hwsim_nan_resume_txqs(struct mac80211_hwsim_data *data)
{
	u64 tsf = mac80211_hwsim_get_tsf(data->hw, data->nan.device_vif);
	u8 slot = hwsim_nan_slot_from_tsf(tsf);
	bool is_dw_slot = mac80211_hwsim_nan_is_dw_slot(data, slot);
	struct hwsim_nan_sta_iter_ctx ctx = {
		.hw = data->hw,
		.can_tx = false,
	};
	u32 timeout_ns;

	/* Outside DW, require local FAW schedule to proceed */
	if (!is_dw_slot) {
		scoped_guard(spinlock_bh, &data->nan.state_lock) {
			if (!data->nan.local_sched[slot].chan)
				return;
		}
	}

	guard(rcu)();

	/* Check if management queue can transmit */
	if (mac80211_hwsim_nan_txq_transmitting(data->hw,
						data->nan.device_vif->txq_mgmt))
		goto resume_txqs_timer;

	/* Check if any STA queue can transmit */
	ieee80211_iterate_stations_atomic(data->hw,
					  hwsim_nan_can_sta_transmit,
					  &ctx);

	if (!ctx.can_tx)
		return;

resume_txqs_timer:
	/*
	 * Wait a bit and also randomize things so that not everyone is TXing
	 * at the same time. Each slot is 16 TU long, this waits between 100 us
	 * and 5 ms before starting to TX (unless a new frame arrives).
	 */
	timeout_ns = get_random_u32_inclusive(100 * NSEC_PER_USEC,
					      5 * NSEC_PER_MSEC);

	hrtimer_start(&data->nan.resume_txqs_timer,
		      ns_to_ktime(timeout_ns),
		      HRTIMER_MODE_REL_SOFT);
}

static void hwsim_nan_wake_sta_iter(void *_data, struct ieee80211_sta *sta)
{
	struct ieee80211_hw *hw = _data;

	for (int i = 0; i < ARRAY_SIZE(sta->txq); i++) {
		struct ieee80211_txq *txq = sta->txq[i];

		if (!txq)
			continue;

		/* exit early if non-NAN */
		if (txq->vif->type != NL80211_IFTYPE_NAN &&
		    txq->vif->type != NL80211_IFTYPE_NAN_DATA)
			return;

		if (mac80211_hwsim_nan_txq_transmitting(hw, txq))
			ieee80211_hwsim_wake_tx_queue(hw, txq);
	}
}

enum hrtimer_restart
mac80211_hwsim_nan_resume_txqs_timer(struct hrtimer *timer)
{
	struct mac80211_hwsim_data *data =
		container_of(timer, struct mac80211_hwsim_data,
			     nan.resume_txqs_timer);

	guard(rcu)();

	/* Wake TX queue for management frames on the NAN device interface */
	if (mac80211_hwsim_nan_txq_transmitting(data->hw,
						data->nan.device_vif->txq_mgmt))
		ieee80211_hwsim_wake_tx_queue(data->hw,
					      data->nan.device_vif->txq_mgmt);

	/* Wake TX queues for all stations */
	ieee80211_iterate_stations_atomic(data->hw,
					  hwsim_nan_wake_sta_iter,
					  data->hw);

	return HRTIMER_NORESTART;
}

static void
hwsim_nan_can_mcast_sta_transmit(void *_ctx, struct ieee80211_sta *sta)
{
	struct hwsim_nan_mcast_data_iter_ctx *ctx = _ctx;
	struct ieee80211_txq *txq = sta->txq[0];

	if (!txq || txq->vif != ctx->vif)
		return;

	ctx->n_vif_sta++;
	if (mac80211_hwsim_nan_txq_transmitting(ctx->hw, txq))
		ctx->n_sta_can_tx++;
}

static bool
mac80211_hwsim_nan_mcast_data_transmitting(struct ieee80211_hw *hw,
					   struct ieee80211_txq *txq)
{
	struct mac80211_hwsim_data *data = hw->priv;
	struct hwsim_nan_mcast_data_iter_ctx ctx = {
		.hw = hw,
		.vif = txq->vif,
		.n_sta_can_tx = 0,
		.n_vif_sta = 0,
	};

	/* Check if all the stations associated with the current
	 * interface are available.
	 */
	ieee80211_iterate_stations_atomic(data->hw,
					  hwsim_nan_can_mcast_sta_transmit,
					  &ctx);

	return ctx.n_vif_sta && ctx.n_sta_can_tx == ctx.n_vif_sta;
}

bool mac80211_hwsim_nan_txq_transmitting(struct ieee80211_hw *hw,
					 struct ieee80211_txq *txq)
{
	struct mac80211_hwsim_data *data = hw->priv;
	struct ieee80211_sta *nmi_sta;
	struct hwsim_sta_priv *sp;
	bool is_dw_slot;
	u64 tsf;
	u8 slot;

	if (WARN_ON_ONCE(!data->nan.device_vif))
		return true;

	tsf = mac80211_hwsim_get_tsf(hw, data->nan.device_vif);
	slot = hwsim_nan_slot_from_tsf(tsf);

	/* Enforce a maximum channel switch time and guard against TX delays */
	if (slot != hwsim_nan_slot_from_tsf(tsf + NAN_CHAN_SWITCH_TIME_US))
		return false;

	is_dw_slot = mac80211_hwsim_nan_is_dw_slot(data, slot);

	if (!txq->sta) {
		/* Non-STA TXQ: allow management frames during DW */
		if (txq->vif->type == NL80211_IFTYPE_NAN)
			return is_dw_slot;

		/* Allow multicast data when all the peers are available
		 * on this slot
		 */
		return mac80211_hwsim_nan_mcast_data_transmitting(hw, txq);
	}

	/* STA TXQ: need peer schedule for availability check */
	nmi_sta = rcu_dereference(txq->sta->nmi) ?: txq->sta;
	sp = (void *)nmi_sta->drv_priv;

	/* DW slot: NDI can TX only mgmt but not worth checking,
	 * NMI checks peer's committed DW
	 */
	if (is_dw_slot) {
		if (txq->vif->type == NL80211_IFTYPE_NAN_DATA)
			return false;
		return hwsim_nan_peer_present_in_dw(sp, tsf);
	}

	/* FAW slot: verify local schedule and peer availability */
	return hwsim_nan_peer_present_in_faw(sp, data, slot);
}

void mac80211_hwsim_nan_get_tx_chandef(struct ieee80211_hw *hw,
				       struct cfg80211_chan_def *chandef)
{
	struct mac80211_hwsim_data *data = hw->priv;
	u64 tsf = mac80211_hwsim_get_tsf(data->hw, data->nan.device_vif);
	u8 slot = hwsim_nan_slot_from_tsf(tsf);

	/* DW slots are always 20 MHz */
	if (slot == SLOT_24GHZ_DW) {
		cfg80211_chandef_create(chandef,
					ieee80211_get_channel(hw->wiphy, 2437),
					NL80211_CHAN_NO_HT);
		return;
	}

	if (slot == SLOT_5GHZ_DW && data->nan.bands & BIT(NL80211_BAND_5GHZ)) {
		cfg80211_chandef_create(chandef,
					ieee80211_get_channel(hw->wiphy, 5745),
					NL80211_CHAN_NO_HT);
		return;
	}

	/* FAW slot: copy local schedule for this slot */
	scoped_guard(spinlock_bh, &data->nan.state_lock)
		*chandef = data->nan.local_sched[slot];
}

bool mac80211_hwsim_nan_receive(struct ieee80211_hw *hw,
				struct ieee80211_channel *channel,
				struct ieee80211_rx_status *rx_status)
{
	struct mac80211_hwsim_data *data = hw->priv;
	u8 slot;

	if (WARN_ON_ONCE(!data->nan.device_vif))
		return false;

	if (data->nan.phase == MAC80211_HWSIM_NAN_PHASE_SCAN)
		return channel->center_freq == 2437;

	if (rx_status->flag & RX_FLAG_MACTIME) {
		slot = hwsim_nan_slot_from_tsf(rx_status->mactime);
	} else {
		u64 tsf;

		/*
		 * This is not perfect, but that should be fine.
		 *
		 * Assume the frame might be a bit early in relation to our
		 * own TSF. This is largely because the TSF sync is going to be
		 * pretty bad when the frame was RXed via NL and the beacon as
		 * well as RX timestamps are not accurate.
		 */
		tsf = mac80211_hwsim_get_tsf(data->hw, data->nan.device_vif);
		slot = hwsim_nan_slot_from_tsf(tsf + 128);
	}

	if (slot == SLOT_24GHZ_DW && channel->center_freq == 2437)
		return true;

	if (slot == SLOT_5GHZ_DW && data->nan.bands & BIT(NL80211_BAND_5GHZ) &&
	    channel->center_freq == 5745)
		return true;

	/* Accept frames during FAW slots if chandef is compatible */
	return hwsim_nan_rx_chandef_compatible(data, slot, channel,
					       rx_status->bw);
}

void mac80211_hwsim_nan_local_sched_changed(struct ieee80211_hw *hw,
					    struct ieee80211_vif *vif)
{
	struct mac80211_hwsim_data *data = hw->priv;
	struct ieee80211_nan_channel **slots = vif->cfg.nan_sched.schedule;

	if (WARN_ON(vif->type != NL80211_IFTYPE_NAN))
		return;

	spin_lock_bh(&data->nan.state_lock);

	for (int i = 0; i < ARRAY_SIZE(data->nan.local_sched); i++) {
		struct ieee80211_chanctx_conf *chanctx;

		if (!slots[i] || IS_ERR(slots[i])) {
			memset(&data->nan.local_sched[i], 0,
			       sizeof(data->nan.local_sched[i]));
			continue;
		}

		chanctx = slots[i]->chanctx_conf;
		if (!chanctx) {
			memset(&data->nan.local_sched[i], 0,
			       sizeof(data->nan.local_sched[i]));
			continue;
		}

		data->nan.local_sched[i] = chanctx->def;
	}

	spin_unlock_bh(&data->nan.state_lock);
}

int mac80211_hwsim_nan_peer_sched_changed(struct ieee80211_hw *hw,
					  struct ieee80211_sta *sta)
{
	struct hwsim_sta_priv *sp = (void *)sta->drv_priv;
	struct ieee80211_nan_peer_sched *sched = sta->nan_sched;

	spin_lock_bh(&sp->nan_sched.lock);

	/* Clear existing schedule */
	sp->nan_sched.committed_dw = 0;
	for (int i = 0; i < CFG80211_NAN_MAX_PEER_MAPS; i++) {
		sp->nan_sched.maps[i].map_id = CFG80211_NAN_INVALID_MAP_ID;
		memset(sp->nan_sched.maps[i].chans, 0,
		       sizeof(sp->nan_sched.maps[i].chans));
	}

	if (!sched)
		goto out;

	sp->nan_sched.committed_dw = sched->committed_dw;

	for (int i = 0; i < CFG80211_NAN_MAX_PEER_MAPS; i++) {
		struct ieee80211_nan_peer_map *map = &sched->maps[i];

		if (map->map_id == CFG80211_NAN_INVALID_MAP_ID)
			continue;

		sp->nan_sched.maps[i].map_id = map->map_id;

		for (int j = 0; j < CFG80211_NAN_SCHED_NUM_TIME_SLOTS; j++) {
			struct ieee80211_nan_channel *peer_chan =
				map->slots[j];

			if (peer_chan && peer_chan->chanreq.oper.chan)
				sp->nan_sched.maps[i].chans[j] =
					peer_chan->chanreq.oper;
			else
				memset(&sp->nan_sched.maps[i].chans[j], 0,
				       sizeof(sp->nan_sched.maps[i].chans[j]));
		}
	}

out:
	spin_unlock_bh(&sp->nan_sched.lock);
	return 0;
}
