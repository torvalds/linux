/*
 * This file is part of wl1271
 *
 * Copyright (C) 2008-2009 Nokia Corporation
 *
 * Contact: Luciano Coelho <luciano.coelho@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include "ps.h"
#include "io.h"
#include "tx.h"
#include "debug.h"

#define WL1271_WAKEUP_TIMEOUT 500

#define ELP_ENTRY_DELAY  30
#define ELP_ENTRY_DELAY_FORCE_PS  5

void wl1271_elp_work(struct work_struct *work)
{
	struct delayed_work *dwork;
	struct wl1271 *wl;
	struct wl12xx_vif *wlvif;
	int ret;

	dwork = container_of(work, struct delayed_work, work);
	wl = container_of(dwork, struct wl1271, elp_work);

	wl1271_debug(DEBUG_PSM, "elp work");

	mutex_lock(&wl->mutex);

	if (unlikely(wl->state != WLCORE_STATE_ON))
		goto out;

	/* our work might have been already cancelled */
	if (unlikely(!test_bit(WL1271_FLAG_ELP_REQUESTED, &wl->flags)))
		goto out;

	if (test_bit(WL1271_FLAG_IN_ELP, &wl->flags))
		goto out;

	wl12xx_for_each_wlvif(wl, wlvif) {
		if (wlvif->bss_type == BSS_TYPE_AP_BSS)
			goto out;

		if (!test_bit(WLVIF_FLAG_IN_PS, &wlvif->flags) &&
		    test_bit(WLVIF_FLAG_IN_USE, &wlvif->flags))
			goto out;
	}

	wl1271_debug(DEBUG_PSM, "chip to elp");
	ret = wlcore_raw_write32(wl, HW_ACCESS_ELP_CTRL_REG, ELPCTRL_SLEEP);
	if (ret < 0) {
		wl12xx_queue_recovery_work(wl);
		goto out;
	}

	set_bit(WL1271_FLAG_IN_ELP, &wl->flags);

out:
	mutex_unlock(&wl->mutex);
}

/* Routines to toggle sleep mode while in ELP */
void wl1271_ps_elp_sleep(struct wl1271 *wl)
{
	struct wl12xx_vif *wlvif;
	u32 timeout;

	if (wl->sleep_auth != WL1271_PSM_ELP)
		return;

	/* we shouldn't get consecutive sleep requests */
	if (WARN_ON(test_and_set_bit(WL1271_FLAG_ELP_REQUESTED, &wl->flags)))
		return;

	wl12xx_for_each_wlvif(wl, wlvif) {
		if (wlvif->bss_type == BSS_TYPE_AP_BSS)
			return;

		if (!test_bit(WLVIF_FLAG_IN_PS, &wlvif->flags) &&
		    test_bit(WLVIF_FLAG_IN_USE, &wlvif->flags))
			return;
	}

	timeout = wl->conf.conn.forced_ps ?
			ELP_ENTRY_DELAY_FORCE_PS : ELP_ENTRY_DELAY;
	ieee80211_queue_delayed_work(wl->hw, &wl->elp_work,
				     msecs_to_jiffies(timeout));
}

int wl1271_ps_elp_wakeup(struct wl1271 *wl)
{
	DECLARE_COMPLETION_ONSTACK(compl);
	unsigned long flags;
	int ret;
	u32 start_time = jiffies;
	bool pending = false;

	/*
	 * we might try to wake up even if we didn't go to sleep
	 * before (e.g. on boot)
	 */
	if (!test_and_clear_bit(WL1271_FLAG_ELP_REQUESTED, &wl->flags))
		return 0;

	/* don't cancel_sync as it might contend for a mutex and deadlock */
	cancel_delayed_work(&wl->elp_work);

	if (!test_bit(WL1271_FLAG_IN_ELP, &wl->flags))
		return 0;

	wl1271_debug(DEBUG_PSM, "waking up chip from elp");

	/*
	 * The spinlock is required here to synchronize both the work and
	 * the completion variable in one entity.
	 */
	spin_lock_irqsave(&wl->wl_lock, flags);
	if (test_bit(WL1271_FLAG_IRQ_RUNNING, &wl->flags))
		pending = true;
	else
		wl->elp_compl = &compl;
	spin_unlock_irqrestore(&wl->wl_lock, flags);

	ret = wlcore_raw_write32(wl, HW_ACCESS_ELP_CTRL_REG, ELPCTRL_WAKE_UP);
	if (ret < 0) {
		wl12xx_queue_recovery_work(wl);
		goto err;
	}

	if (!pending) {
		ret = wait_for_completion_timeout(
			&compl, msecs_to_jiffies(WL1271_WAKEUP_TIMEOUT));
		if (ret == 0) {
			wl1271_error("ELP wakeup timeout!");
			wl12xx_queue_recovery_work(wl);
			ret = -ETIMEDOUT;
			goto err;
		}
	}

	clear_bit(WL1271_FLAG_IN_ELP, &wl->flags);

	wl1271_debug(DEBUG_PSM, "wakeup time: %u ms",
		     jiffies_to_msecs(jiffies - start_time));
	goto out;

err:
	spin_lock_irqsave(&wl->wl_lock, flags);
	wl->elp_compl = NULL;
	spin_unlock_irqrestore(&wl->wl_lock, flags);
	return ret;

out:
	return 0;
}

int wl1271_ps_set_mode(struct wl1271 *wl, struct wl12xx_vif *wlvif,
		       enum wl1271_cmd_ps_mode mode)
{
	int ret;
	u16 timeout = wl->conf.conn.dynamic_ps_timeout;

	switch (mode) {
	case STATION_AUTO_PS_MODE:
	case STATION_POWER_SAVE_MODE:
		wl1271_debug(DEBUG_PSM, "entering psm (mode=%d,timeout=%u)",
			     mode, timeout);

		ret = wl1271_acx_wake_up_conditions(wl, wlvif,
					    wl->conf.conn.wake_up_event,
					    wl->conf.conn.listen_interval);
		if (ret < 0) {
			wl1271_error("couldn't set wake up conditions");
			return ret;
		}

		ret = wl1271_cmd_ps_mode(wl, wlvif, mode, timeout);
		if (ret < 0)
			return ret;

		set_bit(WLVIF_FLAG_IN_PS, &wlvif->flags);

		/*
		 * enable beacon early termination.
		 * Not relevant for 5GHz and for high rates.
		 */
		if ((wlvif->band == IEEE80211_BAND_2GHZ) &&
		    (wlvif->basic_rate < CONF_HW_BIT_RATE_9MBPS)) {
			ret = wl1271_acx_bet_enable(wl, wlvif, true);
			if (ret < 0)
				return ret;
		}
		break;
	case STATION_ACTIVE_MODE:
		wl1271_debug(DEBUG_PSM, "leaving psm");

		/* disable beacon early termination */
		if ((wlvif->band == IEEE80211_BAND_2GHZ) &&
		    (wlvif->basic_rate < CONF_HW_BIT_RATE_9MBPS)) {
			ret = wl1271_acx_bet_enable(wl, wlvif, false);
			if (ret < 0)
				return ret;
		}

		ret = wl1271_cmd_ps_mode(wl, wlvif, mode, 0);
		if (ret < 0)
			return ret;

		clear_bit(WLVIF_FLAG_IN_PS, &wlvif->flags);
		break;
	default:
		wl1271_warning("trying to set ps to unsupported mode %d", mode);
		ret = -EINVAL;
	}

	return ret;
}

static void wl1271_ps_filter_frames(struct wl1271 *wl, u8 hlid)
{
	int i;
	struct sk_buff *skb;
	struct ieee80211_tx_info *info;
	unsigned long flags;
	int filtered[NUM_TX_QUEUES];
	struct wl1271_link *lnk = &wl->links[hlid];

	/* filter all frames currently in the low level queues for this hlid */
	for (i = 0; i < NUM_TX_QUEUES; i++) {
		filtered[i] = 0;
		while ((skb = skb_dequeue(&lnk->tx_queue[i]))) {
			filtered[i]++;

			if (WARN_ON(wl12xx_is_dummy_packet(wl, skb)))
				continue;

			info = IEEE80211_SKB_CB(skb);
			info->flags |= IEEE80211_TX_STAT_TX_FILTERED;
			info->status.rates[0].idx = -1;
			ieee80211_tx_status_ni(wl->hw, skb);
		}
	}

	spin_lock_irqsave(&wl->wl_lock, flags);
	for (i = 0; i < NUM_TX_QUEUES; i++) {
		wl->tx_queue_count[i] -= filtered[i];
		if (lnk->wlvif)
			lnk->wlvif->tx_queue_count[i] -= filtered[i];
	}
	spin_unlock_irqrestore(&wl->wl_lock, flags);

	wl1271_handle_tx_low_watermark(wl);
}

void wl12xx_ps_link_start(struct wl1271 *wl, struct wl12xx_vif *wlvif,
			  u8 hlid, bool clean_queues)
{
	struct ieee80211_sta *sta;
	struct ieee80211_vif *vif = wl12xx_wlvif_to_vif(wlvif);

	if (test_bit(hlid, &wl->ap_ps_map))
		return;

	wl1271_debug(DEBUG_PSM, "start mac80211 PSM on hlid %d pkts %d "
		     "clean_queues %d", hlid, wl->links[hlid].allocated_pkts,
		     clean_queues);

	rcu_read_lock();
	sta = ieee80211_find_sta(vif, wl->links[hlid].addr);
	if (!sta) {
		wl1271_error("could not find sta %pM for starting ps",
			     wl->links[hlid].addr);
		rcu_read_unlock();
		return;
	}

	ieee80211_sta_ps_transition_ni(sta, true);
	rcu_read_unlock();

	/* do we want to filter all frames from this link's queues? */
	if (clean_queues)
		wl1271_ps_filter_frames(wl, hlid);

	__set_bit(hlid, &wl->ap_ps_map);
}

void wl12xx_ps_link_end(struct wl1271 *wl, struct wl12xx_vif *wlvif, u8 hlid)
{
	struct ieee80211_sta *sta;
	struct ieee80211_vif *vif = wl12xx_wlvif_to_vif(wlvif);

	if (!test_bit(hlid, &wl->ap_ps_map))
		return;

	wl1271_debug(DEBUG_PSM, "end mac80211 PSM on hlid %d", hlid);

	__clear_bit(hlid, &wl->ap_ps_map);

	rcu_read_lock();
	sta = ieee80211_find_sta(vif, wl->links[hlid].addr);
	if (!sta) {
		wl1271_error("could not find sta %pM for ending ps",
			     wl->links[hlid].addr);
		goto end;
	}

	ieee80211_sta_ps_transition_ni(sta, false);
end:
	rcu_read_unlock();
}
