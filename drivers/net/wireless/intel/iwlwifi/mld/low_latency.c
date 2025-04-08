// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2024-2025 Intel Corporation
 */
#include "mld.h"
#include "iface.h"
#include "low_latency.h"
#include "hcmd.h"
#include "power.h"
#include "mlo.h"

#define MLD_LL_WK_INTERVAL_MSEC 500
#define MLD_LL_PERIOD (HZ * MLD_LL_WK_INTERVAL_MSEC / 1000)
#define MLD_LL_ACTIVE_WK_PERIOD (HZ * 10)

/* packets/MLD_LL_WK_PERIOD seconds */
#define MLD_LL_ENABLE_THRESH 100

static bool iwl_mld_calc_low_latency(struct iwl_mld *mld,
				     unsigned long timestamp)
{
	struct iwl_mld_low_latency *ll = &mld->low_latency;
	bool global_low_latency = false;
	u8 num_rx_q = mld->trans->num_rx_queues;

	for (int mac_id = 0; mac_id < NUM_MAC_INDEX_DRIVER; mac_id++) {
		u32 total_vo_vi_pkts = 0;
		bool ll_period_expired;

		/* If it's not initialized yet, it means we have not yet
		 * received/transmitted any vo/vi packet on this MAC.
		 */
		if (!ll->window_start[mac_id])
			continue;

		ll_period_expired =
			time_after(timestamp, ll->window_start[mac_id] +
				   MLD_LL_ACTIVE_WK_PERIOD);

		if (ll_period_expired)
			ll->window_start[mac_id] = timestamp;

		for (int q = 0; q < num_rx_q; q++) {
			struct iwl_mld_low_latency_packets_counters *counters =
				&mld->low_latency.pkts_counters[q];

			spin_lock_bh(&counters->lock);

			total_vo_vi_pkts += counters->vo_vi[mac_id];

			if (ll_period_expired)
				counters->vo_vi[mac_id] = 0;

			spin_unlock_bh(&counters->lock);
		}

		/* enable immediately with enough packets but defer
		 * disabling only if the low-latency period expired and
		 * below threshold.
		 */
		if (total_vo_vi_pkts > MLD_LL_ENABLE_THRESH)
			mld->low_latency.result[mac_id] = true;
		else if (ll_period_expired)
			mld->low_latency.result[mac_id] = false;

		global_low_latency |= mld->low_latency.result[mac_id];
	}

	return global_low_latency;
}

static void iwl_mld_low_latency_iter(void *_data, u8 *mac,
				     struct ieee80211_vif *vif)
{
	struct iwl_mld *mld = _data;
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);
	bool prev = mld_vif->low_latency_causes & LOW_LATENCY_TRAFFIC;
	bool low_latency;

	if (WARN_ON(mld_vif->fw_id >= ARRAY_SIZE(mld->low_latency.result)))
		return;

	low_latency = mld->low_latency.result[mld_vif->fw_id];

	if (prev != low_latency)
		iwl_mld_vif_update_low_latency(mld, vif, low_latency,
					       LOW_LATENCY_TRAFFIC);
}

static void iwl_mld_low_latency_wk(struct wiphy *wiphy, struct wiphy_work *wk)
{
	struct iwl_mld *mld = container_of(wk, struct iwl_mld,
					   low_latency.work.work);
	unsigned long timestamp = jiffies;
	bool low_latency_active;

	if (mld->fw_status.in_hw_restart)
		return;

	/* It is assumed that the work was scheduled only after checking
	 * at least MLD_LL_PERIOD has passed since the last update.
	 */

	low_latency_active = iwl_mld_calc_low_latency(mld, timestamp);

	/* Update the timestamp now after the low-latency calculation */
	mld->low_latency.timestamp = timestamp;

	/* If low-latency is active we need to force re-evaluation after
	 * 10 seconds, so that we can disable low-latency when
	 * the low-latency traffic ends.
	 *
	 * Otherwise, we don't need to run the work because there is nothing to
	 * disable.
	 *
	 * Note that this has no impact on the regular scheduling of the
	 * updates triggered by traffic - those happen whenever the
	 * MLD_LL_PERIOD timeout expire.
	 */
	if (low_latency_active)
		wiphy_delayed_work_queue(mld->wiphy, &mld->low_latency.work,
					 MLD_LL_ACTIVE_WK_PERIOD);

	ieee80211_iterate_active_interfaces_mtx(mld->hw,
						IEEE80211_IFACE_ITER_NORMAL,
						iwl_mld_low_latency_iter, mld);
}

int iwl_mld_low_latency_init(struct iwl_mld *mld)
{
	struct iwl_mld_low_latency *ll = &mld->low_latency;
	unsigned long ts = jiffies;

	ll->pkts_counters = kcalloc(mld->trans->num_rx_queues,
				    sizeof(*ll->pkts_counters), GFP_KERNEL);
	if (!ll->pkts_counters)
		return -ENOMEM;

	for (int q = 0; q < mld->trans->num_rx_queues; q++)
		spin_lock_init(&ll->pkts_counters[q].lock);

	wiphy_delayed_work_init(&ll->work, iwl_mld_low_latency_wk);

	ll->timestamp = ts;

	/* The low-latency window_start will be initialized per-MAC on
	 * the first vo/vi packet received/transmitted.
	 */

	return 0;
}

void iwl_mld_low_latency_free(struct iwl_mld *mld)
{
	struct iwl_mld_low_latency *ll = &mld->low_latency;

	kfree(ll->pkts_counters);
	ll->pkts_counters = NULL;
}

void iwl_mld_low_latency_restart_cleanup(struct iwl_mld *mld)
{
	struct iwl_mld_low_latency *ll = &mld->low_latency;

	ll->timestamp = jiffies;

	memset(ll->window_start, 0, sizeof(ll->window_start));
	memset(ll->result, 0, sizeof(ll->result));

	for (int q = 0; q < mld->trans->num_rx_queues; q++)
		memset(ll->pkts_counters[q].vo_vi, 0,
		       sizeof(ll->pkts_counters[q].vo_vi));
}

static int iwl_mld_send_low_latency_cmd(struct iwl_mld *mld, bool low_latency,
					u16 mac_id)
{
	struct iwl_mac_low_latency_cmd cmd = {
		.mac_id = cpu_to_le32(mac_id)
	};
	u16 cmd_id = WIDE_ID(MAC_CONF_GROUP, LOW_LATENCY_CMD);
	int ret;

	if (low_latency) {
		/* Currently we don't care about the direction */
		cmd.low_latency_rx = 1;
		cmd.low_latency_tx = 1;
	}

	ret = iwl_mld_send_cmd_pdu(mld, cmd_id, &cmd);
	if (ret)
		IWL_ERR(mld, "Failed to send low latency command\n");

	return ret;
}

static void iwl_mld_vif_set_low_latency(struct iwl_mld_vif *mld_vif, bool set,
					enum iwl_mld_low_latency_cause cause)
{
	if (set)
		mld_vif->low_latency_causes |= cause;
	else
		mld_vif->low_latency_causes &= ~cause;
}

void iwl_mld_vif_update_low_latency(struct iwl_mld *mld,
				    struct ieee80211_vif *vif,
				    bool low_latency,
				    enum iwl_mld_low_latency_cause cause)
{
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);
	bool prev;

	prev = iwl_mld_vif_low_latency(mld_vif);
	iwl_mld_vif_set_low_latency(mld_vif, low_latency, cause);

	low_latency = iwl_mld_vif_low_latency(mld_vif);
	if (low_latency == prev)
		return;

	if (iwl_mld_send_low_latency_cmd(mld, low_latency, mld_vif->fw_id)) {
		/* revert to previous low-latency state */
		iwl_mld_vif_set_low_latency(mld_vif, prev, cause);
		return;
	}

	if (low_latency)
		iwl_mld_leave_omi_bw_reduction(mld);

	if (ieee80211_vif_type_p2p(vif) != NL80211_IFTYPE_P2P_CLIENT)
		return;

	iwl_mld_update_mac_power(mld, vif, false);

	if (low_latency)
		iwl_mld_retry_emlsr(mld, vif);
}

static bool iwl_mld_is_vo_vi_pkt(struct ieee80211_hdr *hdr)
{
	u8 tid;
	static const u8 tid_to_mac80211_ac[] = {
		IEEE80211_AC_BE,
		IEEE80211_AC_BK,
		IEEE80211_AC_BK,
		IEEE80211_AC_BE,
		IEEE80211_AC_VI,
		IEEE80211_AC_VI,
		IEEE80211_AC_VO,
		IEEE80211_AC_VO,
	};

	if (!hdr || !ieee80211_is_data_qos(hdr->frame_control))
		return false;

	tid = ieee80211_get_tid(hdr);
	if (tid >= IWL_MAX_TID_COUNT)
		return false;

	return tid_to_mac80211_ac[tid] < IEEE80211_AC_VI;
}

void iwl_mld_low_latency_update_counters(struct iwl_mld *mld,
					 struct ieee80211_hdr *hdr,
					 struct ieee80211_sta *sta,
					 u8 queue)
{
	struct iwl_mld_sta *mld_sta = iwl_mld_sta_from_mac80211(sta);
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(mld_sta->vif);
	struct iwl_mld_low_latency_packets_counters *counters;
	unsigned long ts = jiffies ? jiffies : 1;
	u8 fw_id = mld_vif->fw_id;

	/* we should have failed op mode init if NULL */
	if (WARN_ON_ONCE(!mld->low_latency.pkts_counters))
		return;

	if (WARN_ON_ONCE(fw_id >= ARRAY_SIZE(counters->vo_vi) ||
			 queue >= mld->trans->num_rx_queues))
		return;

	if (mld->low_latency.stopped)
		return;

	if (!iwl_mld_is_vo_vi_pkt(hdr))
		return;

	counters = &mld->low_latency.pkts_counters[queue];

	spin_lock_bh(&counters->lock);
	counters->vo_vi[fw_id]++;
	spin_unlock_bh(&counters->lock);

	/* Initialize the window_start on the first vo/vi packet */
	if (!mld->low_latency.window_start[fw_id])
		mld->low_latency.window_start[fw_id] = ts;

	if (time_is_before_jiffies(mld->low_latency.timestamp + MLD_LL_PERIOD))
		wiphy_delayed_work_queue(mld->wiphy, &mld->low_latency.work,
					 0);
}

void iwl_mld_low_latency_stop(struct iwl_mld *mld)
{
	lockdep_assert_wiphy(mld->wiphy);

	mld->low_latency.stopped = true;

	wiphy_delayed_work_cancel(mld->wiphy, &mld->low_latency.work);
}

void iwl_mld_low_latency_restart(struct iwl_mld *mld)
{
	struct iwl_mld_low_latency *ll = &mld->low_latency;
	bool low_latency = false;
	unsigned long ts = jiffies;

	lockdep_assert_wiphy(mld->wiphy);

	ll->timestamp = ts;
	mld->low_latency.stopped = false;

	for (int mac = 0; mac < NUM_MAC_INDEX_DRIVER; mac++) {
		ll->window_start[mac] = 0;
		low_latency |= ll->result[mac];

		for (int q = 0; q < mld->trans->num_rx_queues; q++) {
			spin_lock_bh(&ll->pkts_counters[q].lock);
			ll->pkts_counters[q].vo_vi[mac] = 0;
			spin_unlock_bh(&ll->pkts_counters[q].lock);
		}
	}

	/* if low latency is active, force re-evaluation to cover the case of
	 * no traffic.
	 */
	if (low_latency)
		wiphy_delayed_work_queue(mld->wiphy, &ll->work, MLD_LL_PERIOD);
}
