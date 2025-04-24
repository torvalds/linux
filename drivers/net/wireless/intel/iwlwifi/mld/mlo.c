// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2024-2025 Intel Corporation
 */
#include "mlo.h"
#include "phy.h"

/* Block reasons helper */
#define HANDLE_EMLSR_BLOCKED_REASONS(HOW)	\
	HOW(PREVENTION)			\
	HOW(WOWLAN)			\
	HOW(ROC)			\
	HOW(NON_BSS)			\
	HOW(TMP_NON_BSS)		\
	HOW(TPT)

static const char *
iwl_mld_get_emlsr_blocked_string(enum iwl_mld_emlsr_blocked blocked)
{
	/* Using switch without "default" will warn about missing entries  */
	switch (blocked) {
#define REASON_CASE(x) case IWL_MLD_EMLSR_BLOCKED_##x: return #x;
	HANDLE_EMLSR_BLOCKED_REASONS(REASON_CASE)
#undef REASON_CASE
	}

	return "ERROR";
}

static void iwl_mld_print_emlsr_blocked(struct iwl_mld *mld, u32 mask)
{
#define NAME_FMT(x) "%s"
#define NAME_PR(x) (mask & IWL_MLD_EMLSR_BLOCKED_##x) ? "[" #x "]" : "",
	IWL_DEBUG_INFO(mld,
		       "EMLSR blocked = " HANDLE_EMLSR_BLOCKED_REASONS(NAME_FMT)
		       " (0x%x)\n",
		       HANDLE_EMLSR_BLOCKED_REASONS(NAME_PR)
		       mask);
#undef NAME_FMT
#undef NAME_PR
}

/* Exit reasons helper */
#define HANDLE_EMLSR_EXIT_REASONS(HOW)	\
	HOW(BLOCK)			\
	HOW(MISSED_BEACON)		\
	HOW(FAIL_ENTRY)			\
	HOW(CSA)			\
	HOW(EQUAL_BAND)			\
	HOW(LOW_RSSI)			\
	HOW(LINK_USAGE)			\
	HOW(BT_COEX)			\
	HOW(CHAN_LOAD)			\
	HOW(RFI)			\
	HOW(FW_REQUEST)			\
	HOW(INVALID)

static const char *
iwl_mld_get_emlsr_exit_string(enum iwl_mld_emlsr_exit exit)
{
	/* Using switch without "default" will warn about missing entries  */
	switch (exit) {
#define REASON_CASE(x) case IWL_MLD_EMLSR_EXIT_##x: return #x;
	HANDLE_EMLSR_EXIT_REASONS(REASON_CASE)
#undef REASON_CASE
	}

	return "ERROR";
}

static void iwl_mld_print_emlsr_exit(struct iwl_mld *mld, u32 mask)
{
#define NAME_FMT(x) "%s"
#define NAME_PR(x) (mask & IWL_MLD_EMLSR_EXIT_##x) ? "[" #x "]" : "",
	IWL_DEBUG_INFO(mld,
		       "EMLSR exit = " HANDLE_EMLSR_EXIT_REASONS(NAME_FMT)
		       " (0x%x)\n",
		       HANDLE_EMLSR_EXIT_REASONS(NAME_PR)
		       mask);
#undef NAME_FMT
#undef NAME_PR
}

void iwl_mld_emlsr_prevent_done_wk(struct wiphy *wiphy, struct wiphy_work *wk)
{
	struct iwl_mld_vif *mld_vif = container_of(wk, struct iwl_mld_vif,
						   emlsr.prevent_done_wk.work);
	struct ieee80211_vif *vif =
		container_of((void *)mld_vif, struct ieee80211_vif, drv_priv);

	if (WARN_ON(!(mld_vif->emlsr.blocked_reasons &
		      IWL_MLD_EMLSR_BLOCKED_PREVENTION)))
		return;

	iwl_mld_unblock_emlsr(mld_vif->mld, vif,
			      IWL_MLD_EMLSR_BLOCKED_PREVENTION);
}

void iwl_mld_emlsr_tmp_non_bss_done_wk(struct wiphy *wiphy,
				       struct wiphy_work *wk)
{
	struct iwl_mld_vif *mld_vif = container_of(wk, struct iwl_mld_vif,
						   emlsr.tmp_non_bss_done_wk.work);
	struct ieee80211_vif *vif =
		container_of((void *)mld_vif, struct ieee80211_vif, drv_priv);

	if (WARN_ON(!(mld_vif->emlsr.blocked_reasons &
		      IWL_MLD_EMLSR_BLOCKED_TMP_NON_BSS)))
		return;

	iwl_mld_unblock_emlsr(mld_vif->mld, vif,
			      IWL_MLD_EMLSR_BLOCKED_TMP_NON_BSS);
}

#define IWL_MLD_TRIGGER_LINK_SEL_TIME	(HZ * IWL_MLD_TRIGGER_LINK_SEL_TIME_SEC)
#define IWL_MLD_SCAN_EXPIRE_TIME	(HZ * IWL_MLD_SCAN_EXPIRE_TIME_SEC)

/* Exit reasons that can cause longer EMLSR prevention */
#define IWL_MLD_PREVENT_EMLSR_REASONS	(IWL_MLD_EMLSR_EXIT_MISSED_BEACON	| \
					 IWL_MLD_EMLSR_EXIT_LINK_USAGE		| \
					 IWL_MLD_EMLSR_EXIT_FW_REQUEST)
#define IWL_MLD_PREVENT_EMLSR_TIMEOUT	(HZ * 400)

#define IWL_MLD_EMLSR_PREVENT_SHORT	(HZ * 300)
#define IWL_MLD_EMLSR_PREVENT_LONG	(HZ * 600)

static void iwl_mld_check_emlsr_prevention(struct iwl_mld *mld,
					   struct iwl_mld_vif *mld_vif,
					   enum iwl_mld_emlsr_exit reason)
{
	unsigned long delay;

	/*
	 * Reset the counter if more than 400 seconds have passed between one
	 * exit and the other, or if we exited due to a different reason.
	 * Will also reset the counter after the long prevention is done.
	 */
	if (time_after(jiffies, mld_vif->emlsr.last_exit_ts +
				IWL_MLD_PREVENT_EMLSR_TIMEOUT) ||
	    mld_vif->emlsr.last_exit_reason != reason)
		mld_vif->emlsr.exit_repeat_count = 0;

	mld_vif->emlsr.last_exit_reason = reason;
	mld_vif->emlsr.last_exit_ts = jiffies;
	mld_vif->emlsr.exit_repeat_count++;

	/*
	 * Do not add a prevention when the reason was a block. For a block,
	 * EMLSR will be enabled again on unblock.
	 */
	if (reason == IWL_MLD_EMLSR_EXIT_BLOCK)
		return;

	/* Set prevention for a minimum of 30 seconds */
	mld_vif->emlsr.blocked_reasons |= IWL_MLD_EMLSR_BLOCKED_PREVENTION;
	delay = IWL_MLD_TRIGGER_LINK_SEL_TIME;

	/* Handle repeats for reasons that can cause long prevention */
	if (mld_vif->emlsr.exit_repeat_count > 1 &&
	    reason & IWL_MLD_PREVENT_EMLSR_REASONS) {
		if (mld_vif->emlsr.exit_repeat_count == 2)
			delay = IWL_MLD_EMLSR_PREVENT_SHORT;
		else
			delay = IWL_MLD_EMLSR_PREVENT_LONG;

		/*
		 * The timeouts are chosen so that this will not happen, i.e.
		 * IWL_MLD_EMLSR_PREVENT_LONG > IWL_MLD_PREVENT_EMLSR_TIMEOUT
		 */
		WARN_ON(mld_vif->emlsr.exit_repeat_count > 3);
	}

	IWL_DEBUG_INFO(mld,
		       "Preventing EMLSR for %ld seconds due to %u exits with the reason = %s (0x%x)\n",
		       delay / HZ, mld_vif->emlsr.exit_repeat_count,
		       iwl_mld_get_emlsr_exit_string(reason), reason);

	wiphy_delayed_work_queue(mld->wiphy,
				 &mld_vif->emlsr.prevent_done_wk, delay);
}

static void iwl_mld_clear_avg_chan_load_iter(struct ieee80211_hw *hw,
					     struct ieee80211_chanctx_conf *ctx,
					     void *dat)
{
	struct iwl_mld_phy *phy = iwl_mld_phy_from_mac80211(ctx);

	/* It is ok to do it for all chanctx (and not only for the ones that
	 * belong to the EMLSR vif) since EMLSR is not allowed if there is
	 * another vif.
	 */
	phy->avg_channel_load_not_by_us = 0;
}

static int _iwl_mld_exit_emlsr(struct iwl_mld *mld, struct ieee80211_vif *vif,
			       enum iwl_mld_emlsr_exit exit, u8 link_to_keep,
			       bool sync)
{
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);
	u16 new_active_links;
	int ret = 0;

	lockdep_assert_wiphy(mld->wiphy);

	/* On entry failure need to exit anyway, even if entered from debugfs */
	if (exit != IWL_MLD_EMLSR_EXIT_FAIL_ENTRY && !IWL_MLD_AUTO_EML_ENABLE)
		return 0;

	/* Ignore exit request if EMLSR is not active */
	if (!iwl_mld_emlsr_active(vif))
		return 0;

	if (WARN_ON(!ieee80211_vif_is_mld(vif) || !mld_vif->authorized))
		return 0;

	if (WARN_ON(!(vif->active_links & BIT(link_to_keep))))
		link_to_keep = __ffs(vif->active_links);

	new_active_links = BIT(link_to_keep);
	IWL_DEBUG_INFO(mld,
		       "Exiting EMLSR. reason = %s (0x%x). Current active links=0x%x, new active links = 0x%x\n",
		       iwl_mld_get_emlsr_exit_string(exit), exit,
		       vif->active_links, new_active_links);

	if (sync)
		ret = ieee80211_set_active_links(vif, new_active_links);
	else
		ieee80211_set_active_links_async(vif, new_active_links);

	/* Update latest exit reason and check EMLSR prevention */
	iwl_mld_check_emlsr_prevention(mld, mld_vif, exit);

	/* channel_load_not_by_us is invalid when in EMLSR.
	 * Clear it so wrong values won't be used.
	 */
	ieee80211_iter_chan_contexts_atomic(mld->hw,
					    iwl_mld_clear_avg_chan_load_iter,
					    NULL);

	return ret;
}

void iwl_mld_exit_emlsr(struct iwl_mld *mld, struct ieee80211_vif *vif,
			enum iwl_mld_emlsr_exit exit, u8 link_to_keep)
{
	_iwl_mld_exit_emlsr(mld, vif, exit, link_to_keep, false);
}

static int _iwl_mld_emlsr_block(struct iwl_mld *mld, struct ieee80211_vif *vif,
				enum iwl_mld_emlsr_blocked reason,
				u8 link_to_keep, bool sync)
{
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);

	lockdep_assert_wiphy(mld->wiphy);

	if (!IWL_MLD_AUTO_EML_ENABLE || !iwl_mld_vif_has_emlsr_cap(vif))
		return 0;

	if (mld_vif->emlsr.blocked_reasons & reason)
		return 0;

	mld_vif->emlsr.blocked_reasons |= reason;

	IWL_DEBUG_INFO(mld,
		       "Blocking EMLSR mode. reason = %s (0x%x)\n",
		       iwl_mld_get_emlsr_blocked_string(reason), reason);
	iwl_mld_print_emlsr_blocked(mld, mld_vif->emlsr.blocked_reasons);

	if (reason == IWL_MLD_EMLSR_BLOCKED_TPT)
		wiphy_delayed_work_cancel(mld_vif->mld->wiphy,
					  &mld_vif->emlsr.check_tpt_wk);

	return _iwl_mld_exit_emlsr(mld, vif, IWL_MLD_EMLSR_EXIT_BLOCK,
				   link_to_keep, sync);
}

void iwl_mld_block_emlsr(struct iwl_mld *mld, struct ieee80211_vif *vif,
			 enum iwl_mld_emlsr_blocked reason, u8 link_to_keep)
{
	_iwl_mld_emlsr_block(mld, vif, reason, link_to_keep, false);
}

int iwl_mld_block_emlsr_sync(struct iwl_mld *mld, struct ieee80211_vif *vif,
			     enum iwl_mld_emlsr_blocked reason, u8 link_to_keep)
{
	return _iwl_mld_emlsr_block(mld, vif, reason, link_to_keep, true);
}

static void _iwl_mld_select_links(struct iwl_mld *mld,
				  struct ieee80211_vif *vif);

void iwl_mld_unblock_emlsr(struct iwl_mld *mld, struct ieee80211_vif *vif,
			   enum iwl_mld_emlsr_blocked reason)
{
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);

	lockdep_assert_wiphy(mld->wiphy);

	if (!IWL_MLD_AUTO_EML_ENABLE || !iwl_mld_vif_has_emlsr_cap(vif))
		return;

	if (!(mld_vif->emlsr.blocked_reasons & reason))
		return;

	mld_vif->emlsr.blocked_reasons &= ~reason;

	IWL_DEBUG_INFO(mld,
		       "Unblocking EMLSR mode. reason = %s (0x%x)\n",
		       iwl_mld_get_emlsr_blocked_string(reason), reason);
	iwl_mld_print_emlsr_blocked(mld, mld_vif->emlsr.blocked_reasons);

	if (reason == IWL_MLD_EMLSR_BLOCKED_TPT)
		wiphy_delayed_work_queue(mld_vif->mld->wiphy,
					 &mld_vif->emlsr.check_tpt_wk,
					 round_jiffies_relative(IWL_MLD_TPT_COUNT_WINDOW));

	if (mld_vif->emlsr.blocked_reasons)
		return;

	IWL_DEBUG_INFO(mld, "EMLSR is unblocked\n");
	iwl_mld_int_mlo_scan(mld, vif);
}

static void
iwl_mld_vif_iter_emlsr_mode_notif(void *data, u8 *mac,
				  struct ieee80211_vif *vif)
{
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);
	struct iwl_esr_mode_notif *notif = (void *)data;

	if (!iwl_mld_vif_has_emlsr_cap(vif))
		return;

	switch (le32_to_cpu(notif->action)) {
	case ESR_RECOMMEND_LEAVE:
		iwl_mld_exit_emlsr(mld_vif->mld, vif,
				   IWL_MLD_EMLSR_EXIT_FW_REQUEST,
				   iwl_mld_get_primary_link(vif));
		break;
	case ESR_RECOMMEND_ENTER:
	case ESR_FORCE_LEAVE:
	default:
		IWL_WARN(mld_vif->mld, "Unexpected EMLSR notification: %d\n",
			 le32_to_cpu(notif->action));
	}
}

void iwl_mld_handle_emlsr_mode_notif(struct iwl_mld *mld,
				     struct iwl_rx_packet *pkt)
{
	ieee80211_iterate_active_interfaces_mtx(mld->hw,
						IEEE80211_IFACE_ITER_NORMAL,
						iwl_mld_vif_iter_emlsr_mode_notif,
						pkt->data);
}

static void
iwl_mld_vif_iter_disconnect_emlsr(void *data, u8 *mac,
				  struct ieee80211_vif *vif)
{
	if (!iwl_mld_vif_has_emlsr_cap(vif))
		return;

	ieee80211_connection_loss(vif);
}

void iwl_mld_handle_emlsr_trans_fail_notif(struct iwl_mld *mld,
					   struct iwl_rx_packet *pkt)
{
	const struct iwl_esr_trans_fail_notif *notif = (const void *)pkt->data;
	u32 fw_link_id = le32_to_cpu(notif->link_id);
	struct ieee80211_bss_conf *bss_conf =
		iwl_mld_fw_id_to_link_conf(mld, fw_link_id);

	IWL_DEBUG_INFO(mld, "Failed to %s EMLSR on link %d (FW: %d), reason %d\n",
		       le32_to_cpu(notif->activation) ? "enter" : "exit",
		       bss_conf ? bss_conf->link_id : -1,
		       le32_to_cpu(notif->link_id),
		       le32_to_cpu(notif->err_code));

	if (IWL_FW_CHECK(mld, !bss_conf,
			 "FW reported failure to %sactivate EMLSR on a non-existing link: %d\n",
			 le32_to_cpu(notif->activation) ? "" : "de",
			 fw_link_id)) {
		ieee80211_iterate_active_interfaces_mtx(
			mld->hw, IEEE80211_IFACE_ITER_NORMAL,
			iwl_mld_vif_iter_disconnect_emlsr, NULL);
		return;
	}

	/* Disconnect if we failed to deactivate a link */
	if (!le32_to_cpu(notif->activation)) {
		ieee80211_connection_loss(bss_conf->vif);
		return;
	}

	/*
	 * We failed to activate the second link, go back to the link specified
	 * by the firmware as that is the one that is still valid now.
	 */
	iwl_mld_exit_emlsr(mld, bss_conf->vif, IWL_MLD_EMLSR_EXIT_FAIL_ENTRY,
			   bss_conf->link_id);
}

/* Active non-station link tracking */
static void iwl_mld_count_non_bss_links(void *_data, u8 *mac,
					struct ieee80211_vif *vif)
{
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);
	int *count = _data;

	if (ieee80211_vif_type_p2p(vif) == NL80211_IFTYPE_STATION)
		return;

	*count += iwl_mld_count_active_links(mld_vif->mld, vif);
}

struct iwl_mld_update_emlsr_block_data {
	bool block;
	int result;
};

static void
iwl_mld_vif_iter_update_emlsr_non_bss_block(void *_data, u8 *mac,
					    struct ieee80211_vif *vif)
{
	struct iwl_mld_update_emlsr_block_data *data = _data;
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);
	int ret;

	if (data->block) {
		ret = iwl_mld_block_emlsr_sync(mld_vif->mld, vif,
					       IWL_MLD_EMLSR_BLOCKED_NON_BSS,
					       iwl_mld_get_primary_link(vif));
		if (ret)
			data->result = ret;
	} else {
		iwl_mld_unblock_emlsr(mld_vif->mld, vif,
				      IWL_MLD_EMLSR_BLOCKED_NON_BSS);
	}
}

int iwl_mld_emlsr_check_non_bss_block(struct iwl_mld *mld,
				      int pending_link_changes)
{
	/* An active link of a non-station vif blocks EMLSR. Upon activation
	 * block EMLSR on the bss vif. Upon deactivation, check if this link
	 * was the last non-station link active, and if so unblock the bss vif
	 */
	struct iwl_mld_update_emlsr_block_data block_data = {};
	int count = pending_link_changes;

	/* No need to count if we are activating a non-BSS link */
	if (count <= 0)
		ieee80211_iterate_active_interfaces_mtx(mld->hw,
							IEEE80211_IFACE_ITER_NORMAL,
							iwl_mld_count_non_bss_links,
							&count);

	/*
	 * We could skip updating it if the block change did not change (and
	 * pending_link_changes is non-zero).
	 */
	block_data.block = !!count;

	ieee80211_iterate_active_interfaces_mtx(mld->hw,
						IEEE80211_IFACE_ITER_NORMAL,
						iwl_mld_vif_iter_update_emlsr_non_bss_block,
						&block_data);

	return block_data.result;
}

#define EMLSR_SEC_LINK_MIN_PERC 10
#define EMLSR_MIN_TX 3000
#define EMLSR_MIN_RX 400

void iwl_mld_emlsr_check_tpt(struct wiphy *wiphy, struct wiphy_work *wk)
{
	struct iwl_mld_vif *mld_vif = container_of(wk, struct iwl_mld_vif,
						   emlsr.check_tpt_wk.work);
	struct ieee80211_vif *vif =
		container_of((void *)mld_vif, struct ieee80211_vif, drv_priv);
	struct iwl_mld *mld = mld_vif->mld;
	struct iwl_mld_sta *mld_sta;
	struct iwl_mld_link *sec_link;
	unsigned long total_tx = 0, total_rx = 0;
	unsigned long sec_link_tx = 0, sec_link_rx = 0;
	u8 sec_link_tx_perc, sec_link_rx_perc;
	s8 sec_link_id;

	if (!iwl_mld_vif_has_emlsr_cap(vif) || !mld_vif->ap_sta)
		return;

	mld_sta = iwl_mld_sta_from_mac80211(mld_vif->ap_sta);

	/* We only count for the AP sta in a MLO connection */
	if (!mld_sta->mpdu_counters)
		return;

	/* This wk should only run when the TPT blocker isn't set.
	 * When the blocker is set, the decision to remove it, as well as
	 * clearing the counters is done in DP (to avoid having a wk every
	 * 5 seconds when idle. When the blocker is unset, we are not idle anyway)
	 */
	if (WARN_ON(mld_vif->emlsr.blocked_reasons & IWL_MLD_EMLSR_BLOCKED_TPT))
		return;
	/*
	 * TPT is unblocked, need to check if the TPT criteria is still met.
	 *
	 * If EMLSR is active, then we also need to check the secondar link
	 * requirements.
	 */
	if (iwl_mld_emlsr_active(vif)) {
		sec_link_id = iwl_mld_get_other_link(vif, iwl_mld_get_primary_link(vif));
		sec_link = iwl_mld_link_dereference_check(mld_vif, sec_link_id);
		if (WARN_ON_ONCE(!sec_link))
			return;
		/* We need the FW ID here */
		sec_link_id = sec_link->fw_id;
	} else {
		sec_link_id = -1;
	}

	/* Sum up RX and TX MPDUs from the different queues/links */
	for (int q = 0; q < mld->trans->num_rx_queues; q++) {
		struct iwl_mld_per_q_mpdu_counter *queue_counter =
			&mld_sta->mpdu_counters[q];

		spin_lock_bh(&queue_counter->lock);

		/* The link IDs that doesn't exist will contain 0 */
		for (int link = 0;
		     link < ARRAY_SIZE(queue_counter->per_link);
		     link++) {
			total_tx += queue_counter->per_link[link].tx;
			total_rx += queue_counter->per_link[link].rx;
		}

		if (sec_link_id != -1) {
			sec_link_tx += queue_counter->per_link[sec_link_id].tx;
			sec_link_rx += queue_counter->per_link[sec_link_id].rx;
		}

		memset(queue_counter->per_link, 0,
		       sizeof(queue_counter->per_link));

		spin_unlock_bh(&queue_counter->lock);
	}

	IWL_DEBUG_INFO(mld, "total Tx MPDUs: %ld. total Rx MPDUs: %ld\n",
		       total_tx, total_rx);

	/* If we don't have enough MPDUs - exit EMLSR */
	if (total_tx < IWL_MLD_ENTER_EMLSR_TPT_THRESH &&
	    total_rx < IWL_MLD_ENTER_EMLSR_TPT_THRESH) {
		iwl_mld_block_emlsr(mld, vif, IWL_MLD_EMLSR_BLOCKED_TPT,
				    iwl_mld_get_primary_link(vif));
		return;
	}

	/* EMLSR is not active */
	if (sec_link_id == -1)
		return;

	IWL_DEBUG_INFO(mld, "Secondary Link %d: Tx MPDUs: %ld. Rx MPDUs: %ld\n",
		       sec_link_id, sec_link_tx, sec_link_rx);

	/* Calculate the percentage of the secondary link TX/RX */
	sec_link_tx_perc = total_tx ? sec_link_tx * 100 / total_tx : 0;
	sec_link_rx_perc = total_rx ? sec_link_rx * 100 / total_rx : 0;

	/*
	 * The TX/RX percentage is checked only if it exceeds the required
	 * minimum. In addition, RX is checked only if the TX check failed.
	 */
	if ((total_tx > EMLSR_MIN_TX &&
	     sec_link_tx_perc < EMLSR_SEC_LINK_MIN_PERC) ||
	    (total_rx > EMLSR_MIN_RX &&
	     sec_link_rx_perc < EMLSR_SEC_LINK_MIN_PERC)) {
		iwl_mld_exit_emlsr(mld, vif, IWL_MLD_EMLSR_EXIT_LINK_USAGE,
				   iwl_mld_get_primary_link(vif));
		return;
	}

	/* Check again when the next window ends  */
	wiphy_delayed_work_queue(mld_vif->mld->wiphy,
				 &mld_vif->emlsr.check_tpt_wk,
				 round_jiffies_relative(IWL_MLD_TPT_COUNT_WINDOW));
}

void iwl_mld_emlsr_unblock_tpt_wk(struct wiphy *wiphy, struct wiphy_work *wk)
{
	struct iwl_mld_vif *mld_vif = container_of(wk, struct iwl_mld_vif,
						   emlsr.unblock_tpt_wk);
	struct ieee80211_vif *vif =
		container_of((void *)mld_vif, struct ieee80211_vif, drv_priv);

	iwl_mld_unblock_emlsr(mld_vif->mld, vif, IWL_MLD_EMLSR_BLOCKED_TPT);
}

/*
 * Link selection
 */

s8 iwl_mld_get_emlsr_rssi_thresh(struct iwl_mld *mld,
				 const struct cfg80211_chan_def *chandef,
				 bool low)
{
	if (WARN_ON(chandef->chan->band != NL80211_BAND_2GHZ &&
		    chandef->chan->band != NL80211_BAND_5GHZ &&
		    chandef->chan->band != NL80211_BAND_6GHZ))
		return S8_MAX;

#define RSSI_THRESHOLD(_low, _bw)			\
	(_low) ? IWL_MLD_LOW_RSSI_THRESH_##_bw##MHZ	\
	       : IWL_MLD_HIGH_RSSI_THRESH_##_bw##MHZ

	switch (chandef->width) {
	case NL80211_CHAN_WIDTH_20_NOHT:
	case NL80211_CHAN_WIDTH_20:
	/* 320 MHz has the same thresholds as 20 MHz */
	case NL80211_CHAN_WIDTH_320:
		return RSSI_THRESHOLD(low, 20);
	case NL80211_CHAN_WIDTH_40:
		return RSSI_THRESHOLD(low, 40);
	case NL80211_CHAN_WIDTH_80:
		return RSSI_THRESHOLD(low, 80);
	case NL80211_CHAN_WIDTH_160:
		return RSSI_THRESHOLD(low, 160);
	default:
		WARN_ON(1);
		return S8_MAX;
	}
#undef RSSI_THRESHOLD
}

static u32
iwl_mld_emlsr_disallowed_with_link(struct iwl_mld *mld,
				   struct ieee80211_vif *vif,
				   struct iwl_mld_link_sel_data *link,
				   bool primary)
{
	struct wiphy *wiphy = mld->wiphy;
	struct ieee80211_bss_conf *conf;
	u32 ret = 0;

	conf = wiphy_dereference(wiphy, vif->link_conf[link->link_id]);
	if (WARN_ON_ONCE(!conf))
		return IWL_MLD_EMLSR_EXIT_INVALID;

	if (link->chandef->chan->band == NL80211_BAND_2GHZ && mld->bt_is_active)
		ret |= IWL_MLD_EMLSR_EXIT_BT_COEX;

	if (link->signal <
	    iwl_mld_get_emlsr_rssi_thresh(mld, link->chandef, false))
		ret |= IWL_MLD_EMLSR_EXIT_LOW_RSSI;

	if (conf->csa_active)
		ret |= IWL_MLD_EMLSR_EXIT_CSA;

	if (ret) {
		IWL_DEBUG_INFO(mld,
			       "Link %d is not allowed for EMLSR as %s\n",
			       link->link_id,
			       primary ? "primary" : "secondary");
		iwl_mld_print_emlsr_exit(mld, ret);
	}

	return ret;
}

static u8
iwl_mld_set_link_sel_data(struct iwl_mld *mld,
			  struct ieee80211_vif *vif,
			  struct iwl_mld_link_sel_data *data,
			  unsigned long usable_links,
			  u8 *best_link_idx)
{
	u8 n_data = 0;
	u16 max_grade = 0;
	unsigned long link_id;

	/*
	 * TODO: don't select links that weren't discovered in the last scan
	 * This requires mac80211 (or cfg80211) changes to forward/track when
	 * a BSS was last updated. cfg80211 already tracks this information but
	 * it is not exposed within the kernel.
	 */
	for_each_set_bit(link_id, &usable_links, IEEE80211_MLD_MAX_NUM_LINKS) {
		struct ieee80211_bss_conf *link_conf =
			link_conf_dereference_protected(vif, link_id);

		if (WARN_ON_ONCE(!link_conf))
			continue;

		/* Ignore any BSS that was not seen in the last MLO scan */
		if (ktime_before(link_conf->bss->ts_boottime,
				 mld->scan.last_mlo_scan_time))
			continue;

		data[n_data].link_id = link_id;
		data[n_data].chandef = &link_conf->chanreq.oper;
		data[n_data].signal = MBM_TO_DBM(link_conf->bss->signal);
		data[n_data].grade = iwl_mld_get_link_grade(mld, link_conf);

		if (n_data == 0 || data[n_data].grade > max_grade) {
			max_grade = data[n_data].grade;
			*best_link_idx = n_data;
		}
		n_data++;
	}

	return n_data;
}

static u32
iwl_mld_get_min_chan_load_thresh(struct ieee80211_chanctx_conf *chanctx)
{
	const struct iwl_mld_phy *phy = iwl_mld_phy_from_mac80211(chanctx);

	switch (phy->chandef.width) {
	case NL80211_CHAN_WIDTH_320:
	case NL80211_CHAN_WIDTH_160:
		return 5;
	case NL80211_CHAN_WIDTH_80:
		return 7;
	default:
		break;
	}
	return 10;
}

VISIBLE_IF_IWLWIFI_KUNIT bool
iwl_mld_channel_load_allows_emlsr(struct iwl_mld *mld,
				  struct ieee80211_vif *vif,
				  const struct iwl_mld_link_sel_data *a,
				  const struct iwl_mld_link_sel_data *b)
{
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);
	struct iwl_mld_link *link_a =
		iwl_mld_link_dereference_check(mld_vif, a->link_id);
	struct ieee80211_chanctx_conf *chanctx_a = NULL;
	u32 bw_a, bw_b, ratio;
	u32 primary_load_perc;

	if (!link_a || !link_a->active) {
		IWL_DEBUG_EHT(mld, "Primary link is not active. Can't enter EMLSR\n");
		return false;
	}

	chanctx_a = wiphy_dereference(mld->wiphy, link_a->chan_ctx);

	if (WARN_ON(!chanctx_a))
		return false;

	primary_load_perc =
		iwl_mld_phy_from_mac80211(chanctx_a)->avg_channel_load_not_by_us;

	IWL_DEBUG_EHT(mld, "Average channel load not by us: %u\n", primary_load_perc);

	if (primary_load_perc < iwl_mld_get_min_chan_load_thresh(chanctx_a)) {
		IWL_DEBUG_EHT(mld, "Channel load is below the minimum threshold\n");
		return false;
	}

	if (iwl_mld_vif_low_latency(mld_vif)) {
		IWL_DEBUG_EHT(mld, "Low latency vif, EMLSR is allowed\n");
		return true;
	}

	if (a->chandef->width <= b->chandef->width)
		return true;

	bw_a = cfg80211_chandef_get_width(a->chandef);
	bw_b = cfg80211_chandef_get_width(b->chandef);
	ratio = bw_a / bw_b;

	switch (ratio) {
	case 2:
		return primary_load_perc > 25;
	case 4:
		return primary_load_perc > 40;
	case 8:
	case 16:
		return primary_load_perc > 50;
	}

	return false;
}
EXPORT_SYMBOL_IF_IWLWIFI_KUNIT(iwl_mld_channel_load_allows_emlsr);

static bool
iwl_mld_valid_emlsr_pair(struct ieee80211_vif *vif,
			 struct iwl_mld_link_sel_data *a,
			 struct iwl_mld_link_sel_data *b)
{
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);
	struct iwl_mld *mld = mld_vif->mld;
	u32 reason_mask = 0;

	/* Per-link considerations */
	if (iwl_mld_emlsr_disallowed_with_link(mld, vif, a, true) ||
	    iwl_mld_emlsr_disallowed_with_link(mld, vif, b, false))
		return false;

	if (a->chandef->chan->band == b->chandef->chan->band) {
		const struct cfg80211_chan_def *c_low = a->chandef;
		const struct cfg80211_chan_def *c_high = b->chandef;
		u32 c_low_upper_edge, c_high_lower_edge;

		if (c_low->chan->center_freq > c_high->chan->center_freq)
			swap(c_low, c_high);

		c_low_upper_edge = c_low->chan->center_freq +
				   cfg80211_chandef_get_width(c_low) / 2;
		c_high_lower_edge = c_high->chan->center_freq -
				    cfg80211_chandef_get_width(c_high) / 2;

		if (a->chandef->chan->band == NL80211_BAND_5GHZ &&
		    c_low_upper_edge <= 5330 && c_high_lower_edge >= 5490) {
			/* This case is fine - HW/FW can deal with it, there's
			 * enough separation between the two channels.
			 */
		} else {
			reason_mask |= IWL_MLD_EMLSR_EXIT_EQUAL_BAND;
		}
	}
	if (!iwl_mld_channel_load_allows_emlsr(mld, vif, a, b))
		reason_mask |= IWL_MLD_EMLSR_EXIT_CHAN_LOAD;

	if (reason_mask) {
		IWL_DEBUG_INFO(mld,
			       "Links %d and %d are not a valid pair for EMLSR\n",
			       a->link_id, b->link_id);
		IWL_DEBUG_INFO(mld,
			       "Links bandwidth are: %d and %d\n",
			       nl80211_chan_width_to_mhz(a->chandef->width),
			       nl80211_chan_width_to_mhz(b->chandef->width));
		iwl_mld_print_emlsr_exit(mld, reason_mask);
		return false;
	}

	return true;
}

/* Calculation is done with fixed-point with a scaling factor of 1/256 */
#define SCALE_FACTOR 256

/*
 * Returns the combined grade of two given links.
 * Returns 0 if EMLSR is not allowed with these 2 links.
 */
static
unsigned int iwl_mld_get_emlsr_grade(struct iwl_mld *mld,
				     struct ieee80211_vif *vif,
				     struct iwl_mld_link_sel_data *a,
				     struct iwl_mld_link_sel_data *b,
				     u8 *primary_id)
{
	struct ieee80211_bss_conf *primary_conf;
	struct wiphy *wiphy = ieee80211_vif_to_wdev(vif)->wiphy;
	unsigned int primary_load;

	lockdep_assert_wiphy(wiphy);

	/* a is always primary, b is always secondary */
	if (b->grade > a->grade)
		swap(a, b);

	*primary_id = a->link_id;

	if (!iwl_mld_valid_emlsr_pair(vif, a, b))
		return 0;

	primary_conf = wiphy_dereference(wiphy, vif->link_conf[*primary_id]);

	if (WARN_ON_ONCE(!primary_conf))
		return 0;

	primary_load = iwl_mld_get_chan_load(mld, primary_conf);

	/* The more the primary link is loaded, the more worthwhile EMLSR becomes */
	return a->grade + ((b->grade * primary_load) / SCALE_FACTOR);
}

static void _iwl_mld_select_links(struct iwl_mld *mld,
				  struct ieee80211_vif *vif)
{
	struct iwl_mld_link_sel_data data[IEEE80211_MLD_MAX_NUM_LINKS];
	struct iwl_mld_link_sel_data *best_link;
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);
	int max_active_links = iwl_mld_max_active_links(mld, vif);
	u16 new_active, usable_links = ieee80211_vif_usable_links(vif);
	u8 best_idx, new_primary, n_data;
	u16 max_grade;

	lockdep_assert_wiphy(mld->wiphy);

	if (!mld_vif->authorized || hweight16(usable_links) <= 1)
		return;

	if (WARN(ktime_before(mld->scan.last_mlo_scan_time,
			      ktime_sub_ns(ktime_get_boottime_ns(),
					   5ULL * NSEC_PER_SEC)),
		"Last MLO scan was too long ago, can't select links\n"))
		return;

	/* The logic below is simple and not suited for more than 2 links */
	WARN_ON_ONCE(max_active_links > 2);

	n_data = iwl_mld_set_link_sel_data(mld, vif, data, usable_links,
					   &best_idx);

	if (WARN(!n_data, "Couldn't find a valid grade for any link!\n"))
		return;

	/* Default to selecting the single best link */
	best_link = &data[best_idx];
	new_primary = best_link->link_id;
	new_active = BIT(best_link->link_id);
	max_grade = best_link->grade;

	/* If EMLSR is not possible, activate the best link */
	if (max_active_links == 1 || n_data == 1 ||
	    !iwl_mld_vif_has_emlsr_cap(vif) || !IWL_MLD_AUTO_EML_ENABLE ||
	    mld_vif->emlsr.blocked_reasons)
		goto set_active;

	/* Try to find the best link combination */
	for (u8 a = 0; a < n_data; a++) {
		for (u8 b = a + 1; b < n_data; b++) {
			u8 best_in_pair;
			u16 emlsr_grade =
				iwl_mld_get_emlsr_grade(mld, vif,
							&data[a], &data[b],
							&best_in_pair);

			/*
			 * Prefer (new) EMLSR combination to prefer EMLSR over
			 * a single link.
			 */
			if (emlsr_grade < max_grade)
				continue;

			max_grade = emlsr_grade;
			new_primary = best_in_pair;
			new_active = BIT(data[a].link_id) |
				     BIT(data[b].link_id);
		}
	}

set_active:
	IWL_DEBUG_INFO(mld, "Link selection result: 0x%x. Primary = %d\n",
		       new_active, new_primary);

	mld_vif->emlsr.selected_primary = new_primary;
	mld_vif->emlsr.selected_links = new_active;

	ieee80211_set_active_links_async(vif, new_active);
}

static void iwl_mld_vif_iter_select_links(void *_data, u8 *mac,
					  struct ieee80211_vif *vif)
{
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);
	struct iwl_mld *mld = mld_vif->mld;

	_iwl_mld_select_links(mld, vif);
}

void iwl_mld_select_links(struct iwl_mld *mld)
{
	ieee80211_iterate_active_interfaces_mtx(mld->hw,
						IEEE80211_IFACE_ITER_NORMAL,
						iwl_mld_vif_iter_select_links,
						NULL);
}

static void iwl_mld_emlsr_check_bt_iter(void *_data, u8 *mac,
					struct ieee80211_vif *vif)
{
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);
	struct iwl_mld *mld = mld_vif->mld;
	struct ieee80211_bss_conf *link;
	unsigned int link_id;

	if (!mld->bt_is_active) {
		iwl_mld_retry_emlsr(mld, vif);
		return;
	}

	/* BT is turned ON but we are not in EMLSR, nothing to do */
	if (!iwl_mld_emlsr_active(vif))
		return;

	/* In EMLSR and BT is turned ON */

	for_each_vif_active_link(vif, link, link_id) {
		if (WARN_ON(!link->chanreq.oper.chan))
			continue;

		if (link->chanreq.oper.chan->band == NL80211_BAND_2GHZ) {
			iwl_mld_exit_emlsr(mld, vif, IWL_MLD_EMLSR_EXIT_BT_COEX,
					   iwl_mld_get_primary_link(vif));
			return;
		}
	}
}

void iwl_mld_emlsr_check_bt(struct iwl_mld *mld)
{
	ieee80211_iterate_active_interfaces_mtx(mld->hw,
						IEEE80211_IFACE_ITER_NORMAL,
						iwl_mld_emlsr_check_bt_iter,
						NULL);
}

struct iwl_mld_chan_load_data {
	struct iwl_mld_phy *phy;
	u32 prev_chan_load_not_by_us;
};

static void iwl_mld_chan_load_update_iter(void *_data, u8 *mac,
					  struct ieee80211_vif *vif)
{
	struct iwl_mld_chan_load_data *data = _data;
	const struct iwl_mld_phy *phy = data->phy;
	struct ieee80211_chanctx_conf *chanctx =
		container_of((const void *)phy, struct ieee80211_chanctx_conf,
			     drv_priv);
	struct iwl_mld *mld = iwl_mld_vif_from_mac80211(vif)->mld;
	struct ieee80211_bss_conf *prim_link;
	unsigned int prim_link_id;

	prim_link_id = iwl_mld_get_primary_link(vif);
	prim_link = link_conf_dereference_protected(vif, prim_link_id);

	if (WARN_ON(!prim_link))
		return;

	if (chanctx != rcu_access_pointer(prim_link->chanctx_conf))
		return;

	if (iwl_mld_emlsr_active(vif)) {
		int chan_load = iwl_mld_get_chan_load_by_others(mld, prim_link,
								true);

		if (chan_load < 0)
			return;

		/* chan_load is in range [0,255] */
		if (chan_load < NORMALIZE_PERCENT_TO_255(IWL_MLD_EXIT_EMLSR_CHAN_LOAD))
			iwl_mld_exit_emlsr(mld, vif,
					   IWL_MLD_EMLSR_EXIT_CHAN_LOAD,
					   prim_link_id);
	} else {
		u32 old_chan_load = data->prev_chan_load_not_by_us;
		u32 new_chan_load = phy->avg_channel_load_not_by_us;
		u32 min_thresh = iwl_mld_get_min_chan_load_thresh(chanctx);

#define THRESHOLD_CROSSED(threshold) \
	(old_chan_load <= (threshold) && new_chan_load > (threshold))

		if (THRESHOLD_CROSSED(min_thresh) || THRESHOLD_CROSSED(25) ||
		    THRESHOLD_CROSSED(40) || THRESHOLD_CROSSED(50))
			iwl_mld_retry_emlsr(mld, vif);
#undef THRESHOLD_CROSSED
	}
}

void iwl_mld_emlsr_check_chan_load(struct ieee80211_hw *hw,
				   struct iwl_mld_phy *phy,
				   u32 prev_chan_load_not_by_us)
{
	struct iwl_mld_chan_load_data data = {
		.phy = phy,
		.prev_chan_load_not_by_us = prev_chan_load_not_by_us,
	};

	ieee80211_iterate_active_interfaces_mtx(hw,
						IEEE80211_IFACE_ITER_NORMAL,
						iwl_mld_chan_load_update_iter,
						&data);
}

void iwl_mld_retry_emlsr(struct iwl_mld *mld, struct ieee80211_vif *vif)
{
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);

	if (!iwl_mld_vif_has_emlsr_cap(vif) || iwl_mld_emlsr_active(vif) ||
	    mld_vif->emlsr.blocked_reasons)
		return;

	iwl_mld_int_mlo_scan(mld, vif);
}
