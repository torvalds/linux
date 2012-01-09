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

#include "reg.h"
#include "ps.h"
#include "io.h"
#include "tx.h"

#define WL1271_WAKEUP_TIMEOUT 500

void wl1271_elp_work(struct work_struct *work)
{
	struct delayed_work *dwork;
	struct wl1271 *wl;

	dwork = container_of(work, struct delayed_work, work);
	wl = container_of(dwork, struct wl1271, elp_work);

	wl1271_debug(DEBUG_PSM, "elp work");

	mutex_lock(&wl->mutex);

	if (unlikely(wl->state == WL1271_STATE_OFF))
		goto out;

	/* our work might have been already cancelled */
	if (unlikely(!test_bit(WL1271_FLAG_ELP_REQUESTED, &wl->flags)))
		goto out;

	if (test_bit(WL1271_FLAG_IN_ELP, &wl->flags) ||
	    (!test_bit(WL1271_FLAG_PSM, &wl->flags) &&
	     !test_bit(WL1271_FLAG_IDLE, &wl->flags)))
		goto out;

	wl1271_debug(DEBUG_PSM, "chip to elp");
	wl1271_raw_write32(wl, HW_ACCESS_ELP_CTRL_REG_ADDR, ELPCTRL_SLEEP);
	set_bit(WL1271_FLAG_IN_ELP, &wl->flags);

out:
	mutex_unlock(&wl->mutex);
}

#define ELP_ENTRY_DELAY  5

/* Routines to toggle sleep mode while in ELP */
void wl1271_ps_elp_sleep(struct wl1271 *wl)
{
	/* we shouldn't get consecutive sleep requests */
	if (WARN_ON(test_and_set_bit(WL1271_FLAG_ELP_REQUESTED, &wl->flags)))
		return;

	if (!test_bit(WL1271_FLAG_PSM, &wl->flags) &&
	    !test_bit(WL1271_FLAG_IDLE, &wl->flags))
		return;

	ieee80211_queue_delayed_work(wl->hw, &wl->elp_work,
				     msecs_to_jiffies(ELP_ENTRY_DELAY));
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

	wl1271_raw_write32(wl, HW_ACCESS_ELP_CTRL_REG_ADDR, ELPCTRL_WAKE_UP);

	if (!pending) {
		ret = wait_for_completion_timeout(
			&compl, msecs_to_jiffies(WL1271_WAKEUP_TIMEOUT));
		if (ret == 0) {
			wl1271_error("ELP wakeup timeout!");
			wl12xx_queue_recovery_work(wl);
			ret = -ETIMEDOUT;
			goto err;
		} else if (ret < 0) {
			wl1271_error("ELP wakeup completion error.");
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

int wl1271_ps_set_mode(struct wl1271 *wl, enum wl1271_cmd_ps_mode mode,
		       u32 rates, bool send)
{
	int ret;

	switch (mode) {
	case STATION_POWER_SAVE_MODE:
		wl1271_debug(DEBUG_PSM, "entering psm");

		ret = wl1271_acx_wake_up_conditions(wl);
		if (ret < 0) {
			wl1271_error("couldn't set wake up conditions");
			return ret;
		}

		ret = wl1271_cmd_ps_mode(wl, STATION_POWER_SAVE_MODE);
		if (ret < 0)
			return ret;

		set_bit(WL1271_FLAG_PSM, &wl->flags);
		break;
	case STATION_ACTIVE_MODE:
	default:
		wl1271_debug(DEBUG_PSM, "leaving psm");

		/* disable beacon early termination */
		if (wl->band == IEEE80211_BAND_2GHZ) {
			ret = wl1271_acx_bet_enable(wl, false);
			if (ret < 0)
				return ret;
		}

		/* disable beacon filtering */
		ret = wl1271_acx_beacon_filter_opt(wl, false);
		if (ret < 0)
			return ret;

		ret = wl1271_cmd_ps_mode(wl, STATION_ACTIVE_MODE);
		if (ret < 0)
			return ret;

		clear_bit(WL1271_FLAG_PSM, &wl->flags);
		break;
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

	/* filter all frames currently in the low level queues for this hlid */
	for (i = 0; i < NUM_TX_QUEUES; i++) {
		filtered[i] = 0;
		while ((skb = skb_dequeue(&wl->links[hlid].tx_queue[i]))) {
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
	for (i = 0; i < NUM_TX_QUEUES; i++)
		wl->tx_queue_count[i] -= filtered[i];
	spin_unlock_irqrestore(&wl->wl_lock, flags);

	wl1271_handle_tx_low_watermark(wl);
}

void wl1271_ps_link_start(struct wl1271 *wl, u8 hlid, bool clean_queues)
{
	struct ieee80211_sta *sta;

	if (test_bit(hlid, &wl->ap_ps_map))
		return;

	wl1271_debug(DEBUG_PSM, "start mac80211 PSM on hlid %d pkts %d "
		     "clean_queues %d", hlid, wl->links[hlid].allocated_pkts,
		     clean_queues);

	rcu_read_lock();
	sta = ieee80211_find_sta(wl->vif, wl->links[hlid].addr);
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

void wl1271_ps_link_end(struct wl1271 *wl, u8 hlid)
{
	struct ieee80211_sta *sta;

	if (!test_bit(hlid, &wl->ap_ps_map))
		return;

	wl1271_debug(DEBUG_PSM, "end mac80211 PSM on hlid %d", hlid);

	__clear_bit(hlid, &wl->ap_ps_map);

	rcu_read_lock();
	sta = ieee80211_find_sta(wl->vif, wl->links[hlid].addr);
	if (!sta) {
		wl1271_error("could not find sta %pM for ending ps",
			     wl->links[hlid].addr);
		goto end;
	}

	ieee80211_sta_ps_transition_ni(sta, false);
end:
	rcu_read_unlock();
}
