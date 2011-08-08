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

#include "wl12xx.h"
#include "reg.h"
#include "io.h"
#include "event.h"
#include "ps.h"
#include "scan.h"
#include "wl12xx_80211.h"

void wl1271_pspoll_work(struct work_struct *work)
{
	struct delayed_work *dwork;
	struct wl1271 *wl;
	int ret;

	dwork = container_of(work, struct delayed_work, work);
	wl = container_of(dwork, struct wl1271, pspoll_work);

	wl1271_debug(DEBUG_EVENT, "pspoll work");

	mutex_lock(&wl->mutex);

	if (unlikely(wl->state == WL1271_STATE_OFF))
		goto out;

	if (!test_and_clear_bit(WL1271_FLAG_PSPOLL_FAILURE, &wl->flags))
		goto out;

	if (!test_bit(WL1271_FLAG_STA_ASSOCIATED, &wl->flags))
		goto out;

	/*
	 * if we end up here, then we were in powersave when the pspoll
	 * delivery failure occurred, and no-one changed state since, so
	 * we should go back to powersave.
	 */
	ret = wl1271_ps_elp_wakeup(wl);
	if (ret < 0)
		goto out;

	wl1271_ps_set_mode(wl, STATION_POWER_SAVE_MODE, wl->basic_rate, true);

	wl1271_ps_elp_sleep(wl);
out:
	mutex_unlock(&wl->mutex);
};

static void wl1271_event_pspoll_delivery_fail(struct wl1271 *wl)
{
	int delay = wl->conf.conn.ps_poll_recovery_period;
	int ret;

	wl->ps_poll_failures++;
	if (wl->ps_poll_failures == 1)
		wl1271_info("AP with dysfunctional ps-poll, "
			    "trying to work around it.");

	/* force active mode receive data from the AP */
	if (test_bit(WL1271_FLAG_PSM, &wl->flags)) {
		ret = wl1271_ps_set_mode(wl, STATION_ACTIVE_MODE,
					 wl->basic_rate, true);
		if (ret < 0)
			return;
		set_bit(WL1271_FLAG_PSPOLL_FAILURE, &wl->flags);
		ieee80211_queue_delayed_work(wl->hw, &wl->pspoll_work,
					     msecs_to_jiffies(delay));
	}

	/*
	 * If already in active mode, lets we should be getting data from
	 * the AP right away. If we enter PSM too fast after this, and data
	 * remains on the AP, we will get another event like this, and we'll
	 * go into active once more.
	 */
}

static int wl1271_event_ps_report(struct wl1271 *wl,
				  struct event_mailbox *mbox,
				  bool *beacon_loss)
{
	int ret = 0;
	u32 total_retries = wl->conf.conn.psm_entry_retries;

	wl1271_debug(DEBUG_EVENT, "ps_status: 0x%x", mbox->ps_status);

	switch (mbox->ps_status) {
	case EVENT_ENTER_POWER_SAVE_FAIL:
		wl1271_debug(DEBUG_PSM, "PSM entry failed");

		if (!test_bit(WL1271_FLAG_PSM, &wl->flags)) {
			/* remain in active mode */
			wl->psm_entry_retry = 0;
			break;
		}

		if (wl->psm_entry_retry < total_retries) {
			wl->psm_entry_retry++;
			ret = wl1271_ps_set_mode(wl, STATION_POWER_SAVE_MODE,
						 wl->basic_rate, true);
		} else {
			wl1271_info("No ack to nullfunc from AP.");
			wl->psm_entry_retry = 0;
			*beacon_loss = true;
		}
		break;
	case EVENT_ENTER_POWER_SAVE_SUCCESS:
		wl->psm_entry_retry = 0;

		/* enable beacon filtering */
		ret = wl1271_acx_beacon_filter_opt(wl, true);
		if (ret < 0)
			break;

		/*
		 * BET has only a minor effect in 5GHz and masks
		 * channel switch IEs, so we only enable BET on 2.4GHz
		*/
		if (wl->band == IEEE80211_BAND_2GHZ)
			/* enable beacon early termination */
			ret = wl1271_acx_bet_enable(wl, true);

		if (wl->ps_compl) {
			complete(wl->ps_compl);
			wl->ps_compl = NULL;
		}
		break;
	default:
		break;
	}

	return ret;
}

static void wl1271_event_rssi_trigger(struct wl1271 *wl,
				      struct event_mailbox *mbox)
{
	enum nl80211_cqm_rssi_threshold_event event;
	s8 metric = mbox->rssi_snr_trigger_metric[0];

	wl1271_debug(DEBUG_EVENT, "RSSI trigger metric: %d", metric);

	if (metric <= wl->rssi_thold)
		event = NL80211_CQM_RSSI_THRESHOLD_EVENT_LOW;
	else
		event = NL80211_CQM_RSSI_THRESHOLD_EVENT_HIGH;

	if (event != wl->last_rssi_event)
		ieee80211_cqm_rssi_notify(wl->vif, event, GFP_KERNEL);
	wl->last_rssi_event = event;
}

static void wl1271_stop_ba_event(struct wl1271 *wl, u8 ba_allowed)
{
	/* Convert the value to bool */
	wl->ba_allowed = !!ba_allowed;

	/*
	 * Return in case:
	 * there are not BA open or the event indication is to allowed BA
	 */
	if ((!wl->ba_rx_bitmap) || (wl->ba_allowed))
		return;

	ieee80211_stop_rx_ba_session(wl->vif, wl->ba_rx_bitmap, wl->bssid);
}

static void wl12xx_event_soft_gemini_sense(struct wl1271 *wl,
					       u8 enable)
{
	if (enable) {
		/* disable dynamic PS when requested by the firmware */
		ieee80211_disable_dyn_ps(wl->vif);
		set_bit(WL1271_FLAG_SOFT_GEMINI, &wl->flags);
	} else {
		ieee80211_enable_dyn_ps(wl->vif);
		clear_bit(WL1271_FLAG_SOFT_GEMINI, &wl->flags);
		wl1271_recalc_rx_streaming(wl);
	}

}

static void wl1271_event_mbox_dump(struct event_mailbox *mbox)
{
	wl1271_debug(DEBUG_EVENT, "MBOX DUMP:");
	wl1271_debug(DEBUG_EVENT, "\tvector: 0x%x", mbox->events_vector);
	wl1271_debug(DEBUG_EVENT, "\tmask: 0x%x", mbox->events_mask);
}

static int wl1271_event_process(struct wl1271 *wl, struct event_mailbox *mbox)
{
	int ret;
	u32 vector;
	bool beacon_loss = false;
	bool is_ap = (wl->bss_type == BSS_TYPE_AP_BSS);
	bool disconnect_sta = false;
	unsigned long sta_bitmap = 0;

	wl1271_event_mbox_dump(mbox);

	vector = le32_to_cpu(mbox->events_vector);
	vector &= ~(le32_to_cpu(mbox->events_mask));
	wl1271_debug(DEBUG_EVENT, "vector: 0x%x", vector);

	if (vector & SCAN_COMPLETE_EVENT_ID) {
		wl1271_debug(DEBUG_EVENT, "status: 0x%x",
			     mbox->scheduled_scan_status);

		wl1271_scan_stm(wl);
	}

	if (vector & PERIODIC_SCAN_REPORT_EVENT_ID) {
		wl1271_debug(DEBUG_EVENT, "PERIODIC_SCAN_REPORT_EVENT "
			     "(status 0x%0x)", mbox->scheduled_scan_status);

		wl1271_scan_sched_scan_results(wl);
	}

	if (vector & PERIODIC_SCAN_COMPLETE_EVENT_ID) {
		wl1271_debug(DEBUG_EVENT, "PERIODIC_SCAN_COMPLETE_EVENT "
			     "(status 0x%0x)", mbox->scheduled_scan_status);
		if (wl->sched_scanning) {
			wl1271_scan_sched_scan_stop(wl);
			ieee80211_sched_scan_stopped(wl->hw);
		}
	}

	if (vector & SOFT_GEMINI_SENSE_EVENT_ID &&
	    wl->bss_type == BSS_TYPE_STA_BSS)
		wl12xx_event_soft_gemini_sense(wl,
					       mbox->soft_gemini_sense_info);

	/*
	 * The BSS_LOSE_EVENT_ID is only needed while psm (and hence beacon
	 * filtering) is enabled. Without PSM, the stack will receive all
	 * beacons and can detect beacon loss by itself.
	 *
	 * As there's possibility that the driver disables PSM before receiving
	 * BSS_LOSE_EVENT, beacon loss has to be reported to the stack.
	 *
	 */
	if ((vector & BSS_LOSE_EVENT_ID) && !is_ap) {
		wl1271_info("Beacon loss detected.");

		/* indicate to the stack, that beacons have been lost */
		beacon_loss = true;
	}

	if ((vector & PS_REPORT_EVENT_ID) && !is_ap) {
		wl1271_debug(DEBUG_EVENT, "PS_REPORT_EVENT");
		ret = wl1271_event_ps_report(wl, mbox, &beacon_loss);
		if (ret < 0)
			return ret;
	}

	if ((vector & PSPOLL_DELIVERY_FAILURE_EVENT_ID) && !is_ap)
		wl1271_event_pspoll_delivery_fail(wl);

	if (vector & RSSI_SNR_TRIGGER_0_EVENT_ID) {
		wl1271_debug(DEBUG_EVENT, "RSSI_SNR_TRIGGER_0_EVENT");
		if (wl->vif)
			wl1271_event_rssi_trigger(wl, mbox);
	}

	if ((vector & BA_SESSION_RX_CONSTRAINT_EVENT_ID) && !is_ap) {
		wl1271_debug(DEBUG_EVENT, "BA_SESSION_RX_CONSTRAINT_EVENT_ID. "
			     "ba_allowed = 0x%x", mbox->ba_allowed);

		if (wl->vif)
			wl1271_stop_ba_event(wl, mbox->ba_allowed);
	}

	if ((vector & DUMMY_PACKET_EVENT_ID) && !is_ap) {
		wl1271_debug(DEBUG_EVENT, "DUMMY_PACKET_ID_EVENT_ID");
		if (wl->vif)
			wl1271_tx_dummy_packet(wl);
	}

	/*
	 * "TX retries exceeded" has a different meaning according to mode.
	 * In AP mode the offending station is disconnected.
	 */
	if ((vector & MAX_TX_RETRY_EVENT_ID) && is_ap) {
		wl1271_debug(DEBUG_EVENT, "MAX_TX_RETRY_EVENT_ID");
		sta_bitmap |= le16_to_cpu(mbox->sta_tx_retry_exceeded);
		disconnect_sta = true;
	}

	if ((vector & INACTIVE_STA_EVENT_ID) && is_ap) {
		wl1271_debug(DEBUG_EVENT, "INACTIVE_STA_EVENT_ID");
		sta_bitmap |= le16_to_cpu(mbox->sta_aging_status);
		disconnect_sta = true;
	}

	if (is_ap && disconnect_sta) {
		u32 num_packets = wl->conf.tx.max_tx_retries;
		struct ieee80211_sta *sta;
		const u8 *addr;
		int h;

		for (h = find_first_bit(&sta_bitmap, AP_MAX_LINKS);
		     h < AP_MAX_LINKS;
		     h = find_next_bit(&sta_bitmap, AP_MAX_LINKS, h+1)) {
			if (!wl1271_is_active_sta(wl, h))
				continue;

			addr = wl->links[h].addr;

			rcu_read_lock();
			sta = ieee80211_find_sta(wl->vif, addr);
			if (sta) {
				wl1271_debug(DEBUG_EVENT, "remove sta %d", h);
				ieee80211_report_low_ack(sta, num_packets);
			}
			rcu_read_unlock();
		}
	}

	if (wl->vif && beacon_loss)
		ieee80211_connection_loss(wl->vif);

	return 0;
}

int wl1271_event_unmask(struct wl1271 *wl)
{
	int ret;

	ret = wl1271_acx_event_mbox_mask(wl, ~(wl->event_mask));
	if (ret < 0)
		return ret;

	return 0;
}

void wl1271_event_mbox_config(struct wl1271 *wl)
{
	wl->mbox_ptr[0] = wl1271_read32(wl, REG_EVENT_MAILBOX_PTR);
	wl->mbox_ptr[1] = wl->mbox_ptr[0] + sizeof(struct event_mailbox);

	wl1271_debug(DEBUG_EVENT, "MBOX ptrs: 0x%x 0x%x",
		     wl->mbox_ptr[0], wl->mbox_ptr[1]);
}

int wl1271_event_handle(struct wl1271 *wl, u8 mbox_num)
{
	struct event_mailbox mbox;
	int ret;

	wl1271_debug(DEBUG_EVENT, "EVENT on mbox %d", mbox_num);

	if (mbox_num > 1)
		return -EINVAL;

	/* first we read the mbox descriptor */
	wl1271_read(wl, wl->mbox_ptr[mbox_num], &mbox,
		    sizeof(struct event_mailbox), false);

	/* process the descriptor */
	ret = wl1271_event_process(wl, &mbox);
	if (ret < 0)
		return ret;

	/* then we let the firmware know it can go on...*/
	wl1271_write32(wl, ACX_REG_INTERRUPT_TRIG, INTR_TRIG_EVENT_ACK);

	return 0;
}
