// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2026 Morse Micro
 */
#include <linux/slab.h>
#include <linux/timer.h>
#include "core.h"
#include "mac.h"
#include "bus.h"
#include "rc.h"

#define MM81X_RC_BW_TO_MMRC_BW(X)    \
	(((X) == 1) ? MMRC_BW_1MHZ : \
	 ((X) == 2) ? MMRC_BW_2MHZ : \
	 ((X) == 4) ? MMRC_BW_4MHZ : \
	 ((X) == 8) ? MMRC_BW_8MHZ : \
		      MMRC_BW_2MHZ)

static void mm81x_rc_work(struct work_struct *work)
{
	struct mm81x_rc *mrc = container_of(work, struct mm81x_rc, work);
	struct list_head *pos;

	spin_lock_bh(&mrc->lock);

	list_for_each(pos, &mrc->stas) {
		struct mm81x_rc_sta *mrc_sta =
			container_of(pos, struct mm81x_rc_sta, list);
		unsigned long now = jiffies;

		mrc_sta->last_update = now;

		mmrc_update(mrc_sta->tb);
	}

	spin_unlock_bh(&mrc->lock);

	mod_timer(&mrc->timer, jiffies + msecs_to_jiffies(100));
}

static void mm81x_rc_timer(struct timer_list *t)
{
	struct mm81x_rc *mrc = timer_container_of(mrc, t, timer);
	struct mm81x *mors = mrc->mors;

	queue_work(mors->net_wq, &mors->mrc.work);
}

void mm81x_rc_init(struct mm81x *mors)
{
	INIT_LIST_HEAD(&mors->mrc.stas);
	spin_lock_init(&mors->mrc.lock);

	INIT_WORK(&mors->mrc.work, mm81x_rc_work);
	timer_setup(&mors->mrc.timer, mm81x_rc_timer, 0);

	mors->mrc.mors = mors;
	mod_timer(&mors->mrc.timer, jiffies + msecs_to_jiffies(100));
}

void mm81x_rc_deinit(struct mm81x *mors)
{
	timer_shutdown_sync(&mors->mrc.timer);
	cancel_work_sync(&mors->mrc.work);
}

static void mm81x_rc_sta_config_guard_per_bw(struct ieee80211_sta *sta,
					     struct mmrc_sta_capabilities *caps)
{
	caps->guard = BIT(MMRC_GUARD_LONG);

	if (caps->bandwidth & BIT(MMRC_BW_1MHZ)) {
		caps->sgi_per_bw |= SGI_PER_BW(MMRC_BW_1MHZ);
		caps->guard |= BIT(MMRC_GUARD_SHORT);
	}

	if (caps->bandwidth & BIT(MMRC_BW_2MHZ)) {
		caps->sgi_per_bw |= SGI_PER_BW(MMRC_BW_2MHZ);
		caps->guard |= BIT(MMRC_GUARD_SHORT);
	}

	if (caps->bandwidth & BIT(MMRC_BW_4MHZ)) {
		caps->sgi_per_bw |= SGI_PER_BW(MMRC_BW_4MHZ);
		caps->guard |= BIT(MMRC_GUARD_SHORT);
	}

	if (caps->bandwidth & BIT(MMRC_BW_8MHZ)) {
		caps->sgi_per_bw |= SGI_PER_BW(MMRC_BW_8MHZ);
		caps->guard |= BIT(MMRC_GUARD_SHORT);
	}
}

static void mm81x_rc_sta_add_s1g_sta_caps(struct mm81x *mors,
					  struct mmrc_sta_capabilities *caps,
					  struct ieee80211_sta_s1g_cap *s1g_cap)
{
	int nss_idx = 0;
	u8 rx_mcs = s1g_cap->nss_mcs[0] & 0x3; /* 1SS */
	u8 tx_mcs = (s1g_cap->nss_mcs[2] >> 1) & 0x3; /* 1SS */
	u8 mcs = min(rx_mcs, tx_mcs);

	switch (mcs) {
	case IEEE80211_VHT_MCS_SUPPORT_0_9: /* VHT 9 ->  S1G 9 */
		caps->rates |= BIT(MMRC_MCS9) | BIT(MMRC_MCS8);
		fallthrough;
	case IEEE80211_VHT_MCS_SUPPORT_0_8: /* VHT 8 -> S1G 7 */
		caps->rates |= BIT(MMRC_MCS7) | BIT(MMRC_MCS6) |
			       BIT(MMRC_MCS5) | BIT(MMRC_MCS4) | BIT(MMRC_MCS3);
		fallthrough;
	case IEEE80211_VHT_MCS_SUPPORT_0_7: /* VHT 7 -> S1G 2 */
		caps->rates |= BIT(MMRC_MCS2) | BIT(MMRC_MCS1) |
			       BIT(MMRC_MCS0) | BIT(MMRC_MCS10);
		caps->spatial_streams |= (BIT(nss_idx) & 0x0F);
		break;

	default:
		dev_warn(mors->dev, "Invalid MCS encoding 0x%02x for stream %d",
			 mcs, nss_idx);
	}
}

int mm81x_rc_sta_add(struct mm81x *mors, struct ieee80211_vif *vif,
		     struct ieee80211_sta *sta)
{
	struct ieee80211_sta_s1g_cap *s1g_cap = &sta->deflink.s1g_cap;
	struct mm81x_sta *msta = (struct mm81x_sta *)sta->drv_priv;
	struct mmrc_sta_capabilities caps;
	int oper_bw_mhz = cfg80211_chandef_get_width(&mors->chandef);
	size_t table_mem_size;
	struct mmrc_table *tb;

	memset(&caps, 0, sizeof(caps));

	mm81x_rc_sta_add_s1g_sta_caps(mors, &caps, s1g_cap);

	/* Configure STA for support up to 8MHZ */
	while (oper_bw_mhz > 0) {
		caps.bandwidth |= BIT(MM81X_RC_BW_TO_MMRC_BW(oper_bw_mhz));
		oper_bw_mhz >>= 1;
	}

	/* Configure STA for short and long guard */
	mm81x_rc_sta_config_guard_per_bw(sta, &caps);

	/* Set max rates */
	if (mors->hw->max_rates > 0 &&
	    mors->hw->max_rates < IEEE80211_TX_MAX_RATES)
		caps.max_rates = mors->hw->max_rates;
	else
		caps.max_rates = IEEE80211_TX_MAX_RATES;

	/* Set max reties */
	if (mors->hw->max_rate_tries >= MMRC_MIN_CHAIN_ATTEMPTS &&
	    mors->hw->max_rate_tries < MMRC_MAX_CHAIN_ATTEMPTS)
		caps.max_retries = mors->hw->max_rate_tries;
	else
		caps.max_retries = MMRC_MAX_CHAIN_ATTEMPTS;

	WARN_ON(msta->rc.tb);
	table_mem_size = mmrc_memory_required_for_caps(&caps);
	tb = kzalloc(table_mem_size, GFP_KERNEL);
	if (!tb)
		return -ENOMEM;

	/* Initialise the STA rate control table */
	mmrc_sta_init(tb, &caps, msta->avg_rssi);

	spin_lock_bh(&mors->mrc.lock);
	kfree(msta->rc.tb);
	msta->rc.tb = tb;
	list_add(&msta->rc.list, &mors->mrc.stas);
	msta->rc.last_update = jiffies;
	spin_unlock_bh(&mors->mrc.lock);

	return 0;
}

void mm81x_rc_sta_remove(struct mm81x *mors, struct ieee80211_sta *sta)
{
	struct mm81x_sta *msta = (struct mm81x_sta *)sta->drv_priv;

	spin_lock_bh(&mors->mrc.lock);
	if (msta->rc.tb) {
		list_del_init(&msta->rc.list);
		kfree(msta->rc.tb);
		msta->rc.tb = NULL;
	}
	spin_unlock_bh(&mors->mrc.lock);
}

static void mm81x_rc_sta_fill_basic_rates(struct mm81x_skb_tx_info *tx_info,
					  struct ieee80211_tx_info *info,
					  int tx_bw)
{
	int i;
	enum dot11_bandwidth bw_idx = mm81x_ratecode_bw_mhz_to_bw_index(tx_bw);
	enum mm81x_rate_preamble pream = MM81X_RATE_PREAMBLE_S1G_SHORT;

	mm81x_ratecode_mcs_index_set(&tx_info->rates[0].mm81x_ratecode, 0);
	mm81x_ratecode_nss_index_set(&tx_info->rates[0].mm81x_ratecode,
				     NSS_TO_NSS_IDX(1));
	mm81x_ratecode_bw_index_set(&tx_info->rates[0].mm81x_ratecode, bw_idx);
	if (bw_idx == DOT11_BANDWIDTH_1MHZ)
		pream = MM81X_RATE_PREAMBLE_S1G_1M;
	mm81x_ratecode_preamble_set(&tx_info->rates[0].mm81x_ratecode, pream);
	tx_info->rates[0].count = 4;

	for (i = 1; i < IEEE80211_TX_MAX_RATES; i++)
		tx_info->rates[i].count = 0;

	info->control.rates[0].idx = 0;
	info->control.rates[0].count = tx_info->rates[0].count;
	info->control.rates[0].flags = 0;
	info->control.rates[1].idx = -1;
}

static int mm81x_rc_sta_get_rates(struct mm81x *mors, struct mm81x_sta *msta,
				  struct mmrc_rate_table *rates, size_t size)
{
	int ret = -ENOENT;
	struct list_head *pos;

	spin_lock_bh(&mors->mrc.lock);
	list_for_each(pos, &mors->mrc.stas) {
		struct mm81x_rc_sta *mrc_sta =
			list_entry(pos, struct mm81x_rc_sta, list);

		if (&msta->rc == mrc_sta) {
			ret = 0;
			mmrc_get_rates(msta->rc.tb, rates, size);
			break;
		}
	}
	spin_unlock_bh(&mors->mrc.lock);

	return ret;
}

static bool mm81x_rc_use_basic_rates(struct ieee80211_sta *sta,
				     struct sk_buff *skb,
				     struct ieee80211_hdr *hdr)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);

	if (!sta)
		return true;

	if (ieee80211_is_qos_nullfunc(hdr->frame_control) ||
	    ieee80211_is_nullfunc(hdr->frame_control))
		return true;

	if (!ieee80211_is_data_qos(hdr->frame_control))
		return true;

	/* Use basic rates for EAPOL exchanges or when instructed */
	if (unlikely((skb->protocol == cpu_to_be16(ETH_P_PAE) ||
		      info->flags & IEEE80211_TX_CTL_USE_MINRATE)))
		return true;

	return false;
}

void mm81x_rc_sta_fill_tx_rates(struct mm81x *mors,
				struct mm81x_skb_tx_info *tx_info,
				struct sk_buff *skb, struct ieee80211_sta *sta,
				int tx_bw, bool rts_allowed)
{
	int ret, i;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct mm81x_sta *msta;
	struct mmrc_rate_table rates;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);

	BUILD_BUG_ON((MMRC_BW_1MHZ != (enum mmrc_bw)DOT11_BANDWIDTH_1MHZ ||
		      MMRC_BW_2MHZ != (enum mmrc_bw)DOT11_BANDWIDTH_2MHZ ||
		      MMRC_BW_4MHZ != (enum mmrc_bw)DOT11_BANDWIDTH_4MHZ ||
		      MMRC_BW_16MHZ != (enum mmrc_bw)DOT11_BANDWIDTH_16MHZ));

	memset(&info->control.rates, 0, sizeof(info->control.rates));
	memset(&info->status.rates, 0, sizeof(info->status.rates));
	mm81x_rc_sta_fill_basic_rates(tx_info, info, tx_bw);

	/* Use basic rates for non data packets */
	if (mm81x_rc_use_basic_rates(sta, skb, hdr))
		return;

	msta = (struct mm81x_sta *)sta->drv_priv;
	if (!msta)
		return;

	ret = mm81x_rc_sta_get_rates(mors, msta, &rates, skb->len);
	if (ret != 0)
		return;

	for (i = 0; i < IEEE80211_TX_MAX_RATES; i++) {
		info->control.rates[i].flags = 0;
		if (rates.rates[i].rate != MMRC_MCS_UNUSED) {
			u8 mcs = rates.rates[i].rate;
			u8 nss_index = rates.rates[i].ss;
			enum dot11_bandwidth bw_idx =
				(enum dot11_bandwidth)rates.rates[i].bw;
			enum mm81x_rate_preamble pream =
				MM81X_RATE_PREAMBLE_S1G_SHORT;

			mm81x_ratecode_bw_index_set(
				&tx_info->rates[i].mm81x_ratecode, bw_idx);
			mm81x_ratecode_mcs_index_set(
				&tx_info->rates[i].mm81x_ratecode, mcs);
			mm81x_ratecode_nss_index_set(
				&tx_info->rates[i].mm81x_ratecode, nss_index);
			if (bw_idx == DOT11_BANDWIDTH_1MHZ)
				pream = MM81X_RATE_PREAMBLE_S1G_1M;
			mm81x_ratecode_preamble_set(
				&tx_info->rates[i].mm81x_ratecode, pream);
			tx_info->rates[i].count = rates.rates[i].attempts;

			if (rts_allowed &&
			    (rates.rates[i].flags & BIT(MMRC_FLAGS_CTS_RTS))) {
				mm81x_ratecode_enable_rts(
					&tx_info->rates[i].mm81x_ratecode);
				info->control.rates[i].flags |=
					IEEE80211_TX_RC_USE_RTS_CTS;
			}

			if (rates.rates[i].guard == MMRC_GUARD_SHORT) {
				mm81x_ratecode_enable_sgi(
					&tx_info->rates[i].mm81x_ratecode);
				info->control.rates[i].flags |=
					IEEE80211_TX_RC_SHORT_GI;
			}

			/* Update skb tx_info */
			info->control.rates[i].idx = rates.rates[i].rate;
			info->control.rates[i].count = rates.rates[i].attempts;
		} else {
			info->control.rates[i].idx = -1;
			info->control.rates[i].count = 0;
			tx_info->rates[i].count = 0;
		}
	}
}

static void mm81x_rc_sta_set_rates(struct mm81x *mors, struct mm81x_sta *msta,
				   struct mmrc_rate_table *rates, int attempts,
				   bool was_aggregated)
{
	struct list_head *pos;

	spin_lock_bh(&mors->mrc.lock);
	list_for_each(pos, &mors->mrc.stas) {
		struct mm81x_rc_sta *mrc_sta =
			list_entry(pos, struct mm81x_rc_sta, list);

		if (&msta->rc == mrc_sta) {
			mmrc_feedback(msta->rc.tb, rates, attempts,
				      was_aggregated);
			break;
		}
	}
	spin_unlock_bh(&mors->mrc.lock);
}

void mm81x_rc_sta_feedback_rates(struct mm81x *mors, struct sk_buff *skb,
				 struct ieee80211_sta *sta,
				 struct mm81x_skb_tx_status *tx_sts,
				 int attempts)
{
	int i;
	u32 tx_airtime = 0;
	struct mmrc_rate_table rates;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct ieee80211_tx_info *txi = IEEE80211_SKB_CB(skb);
	struct ieee80211_tx_rate *r = &txi->status.rates[0];
	int count = min_t(int, MM81X_SKB_MAX_RATES, IEEE80211_TX_MAX_RATES);
	struct mm81x_sta *msta = msta = (struct mm81x_sta *)sta->drv_priv;

	/* Don't update rate info if basic rates were used */
	if (mm81x_rc_use_basic_rates(sta, skb, hdr))
		goto exit;

	if (attempts <= 0)
		/* Did we really send the packet? */
		goto exit;

	for (i = 0; i < count; i++) {
		rates.rates[i].rate = mm81x_ratecode_mcs_index_get(
			tx_sts->rates[i].mm81x_ratecode);
		rates.rates[i].ss = mm81x_ratecode_nss_index_get(
			tx_sts->rates[i].mm81x_ratecode);
		rates.rates[i].guard =
			mm81x_ratecode_sgi_get(tx_sts->rates[i].mm81x_ratecode);
		rates.rates[i].bw = mm81x_ratecode_bw_index_get(
			tx_sts->rates[i].mm81x_ratecode);
		rates.rates[i].flags =
			mm81x_ratecode_rts_get(tx_sts->rates[i].mm81x_ratecode);
		rates.rates[i].attempts = tx_sts->rates[i].count;

		tx_airtime +=
			mmrc_calculate_rate_tx_time(&rates.rates[i], skb->len);
	}

	if (msta) {
		/*
		 * Save the rate information. This will be used to update
		 * station's tx rate stats
		 */
		msta->last_sta_tx_rate.bw = rates.rates[0].bw;
		msta->last_sta_tx_rate.rate = rates.rates[0].rate;
		msta->last_sta_tx_rate.ss = rates.rates[0].ss;
		msta->last_sta_tx_rate.guard = rates.rates[0].guard;
	}

	mm81x_rc_sta_set_rates(mors, msta, &rates, attempts,
			       !!(le32_to_cpu(tx_sts->flags) &
				  MM81X_TX_STATUS_WAS_AGGREGATED));

	ieee80211_sta_register_airtime(sta, tx_sts->tid, tx_airtime, 0);

exit:
	ieee80211_tx_info_clear_status(txi);

	if (!(le32_to_cpu(tx_sts->flags) & MM81X_TX_STATUS_FLAGS_NO_ACK) &&
	    !(txi->flags & IEEE80211_TX_CTL_NO_ACK))
		txi->flags |= IEEE80211_TX_STAT_ACK;

	if (le32_to_cpu(tx_sts->flags) & MM81X_TX_STATUS_FLAGS_PS_FILTERED) {
		txi->flags |= IEEE80211_TX_STAT_TX_FILTERED;

		/*
		 * Clear TX CTL AMPDU flag so that this frame gets rescheduled
		 * in ieee80211_handle_filtered_frame(). This flag will get set
		 * again by mac80211's tx path on rescheduling.
		 */
		txi->flags &= ~IEEE80211_TX_CTL_AMPDU;
		if (msta) {
			if (!msta->tx_ps_filter_en)
				dev_dbg(mors->dev, "TX ps filter set sta[%pM]",
					msta->addr);
			msta->tx_ps_filter_en = true;
		}
	}

	for (i = 0; i < count; i++) {
		if (tx_sts->rates[i].count > 0) {
			r[i].count = tx_sts->rates[i].count;
			r[i].flags |= IEEE80211_TX_RC_MCS;
		} else {
			r[i].idx = -1;
		}
	}

	/* single packet per A-MPDU (for now) */
	if (txi->flags & IEEE80211_TX_CTL_AMPDU) {
		txi->flags |= IEEE80211_TX_STAT_AMPDU;
		txi->status.ampdu_len = 1;
		txi->status.ampdu_ack_len =
			txi->flags & IEEE80211_TX_STAT_ACK ? 1 : 0;
	}

	/*
	 * Inform mac80211 that the SP (elicited by a PS-Poll or u-APSD) is
	 * over
	 */
	if (sta && (txi->flags & IEEE80211_TX_STATUS_EOSP)) {
		txi->flags &= ~IEEE80211_TX_STATUS_EOSP;
		ieee80211_sta_eosp(sta);
	}
}

void mm81x_rc_sta_state_check(struct mm81x *mors, struct ieee80211_vif *vif,
			      struct ieee80211_sta *sta,
			      enum ieee80211_sta_state old_state,
			      enum ieee80211_sta_state new_state)
{
	struct mm81x_sta *msta = (struct mm81x_sta *)sta->drv_priv;

	/* Add to Morse RC STA list */
	if (old_state < new_state && new_state == IEEE80211_STA_ASSOC) {
		/* Newly associated, add to RC */
		mm81x_rc_sta_add(mors, vif, sta);
	} else if (old_state > new_state && (old_state == IEEE80211_STA_ASSOC ||
					     old_state == IEEE80211_STA_AUTH)) {
		/* Lost or failed association; remove from list */
		mm81x_rc_sta_remove(mors, sta);
	} else if (old_state < new_state && old_state == IEEE80211_STA_NONE &&
		   msta->rc.list.prev) {
		/*
		 * Special case for driver warning issue causing a sta to be
		 * left on the list
		 */
		dev_dbg(mors->dev, "Remove stale sta from rc list");
		mm81x_rc_sta_remove(mors, sta);
	}
}
