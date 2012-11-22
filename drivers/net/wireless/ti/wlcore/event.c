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

#include "wlcore.h"
#include "debug.h"
#include "io.h"
#include "event.h"
#include "ps.h"
#include "scan.h"
#include "wl12xx_80211.h"

static void wl1271_event_rssi_trigger(struct wl1271 *wl,
				      struct wl12xx_vif *wlvif,
				      struct event_mailbox *mbox)
{
	struct ieee80211_vif *vif = wl12xx_wlvif_to_vif(wlvif);
	enum nl80211_cqm_rssi_threshold_event event;
	s8 metric = mbox->rssi_snr_trigger_metric[0];

	wl1271_debug(DEBUG_EVENT, "RSSI trigger metric: %d", metric);

	if (metric <= wlvif->rssi_thold)
		event = NL80211_CQM_RSSI_THRESHOLD_EVENT_LOW;
	else
		event = NL80211_CQM_RSSI_THRESHOLD_EVENT_HIGH;

	if (event != wlvif->last_rssi_event)
		ieee80211_cqm_rssi_notify(vif, event, GFP_KERNEL);
	wlvif->last_rssi_event = event;
}

static void wl1271_stop_ba_event(struct wl1271 *wl, struct wl12xx_vif *wlvif)
{
	struct ieee80211_vif *vif = wl12xx_wlvif_to_vif(wlvif);

	if (wlvif->bss_type != BSS_TYPE_AP_BSS) {
		if (!wlvif->sta.ba_rx_bitmap)
			return;
		ieee80211_stop_rx_ba_session(vif, wlvif->sta.ba_rx_bitmap,
					     vif->bss_conf.bssid);
	} else {
		u8 hlid;
		struct wl1271_link *lnk;
		for_each_set_bit(hlid, wlvif->ap.sta_hlid_map,
				 WL12XX_MAX_LINKS) {
			lnk = &wl->links[hlid];
			if (!lnk->ba_bitmap)
				continue;

			ieee80211_stop_rx_ba_session(vif,
						     lnk->ba_bitmap,
						     lnk->addr);
		}
	}
}

static void wl12xx_event_soft_gemini_sense(struct wl1271 *wl,
					       u8 enable)
{
	struct wl12xx_vif *wlvif;

	if (enable) {
		set_bit(WL1271_FLAG_SOFT_GEMINI, &wl->flags);
	} else {
		clear_bit(WL1271_FLAG_SOFT_GEMINI, &wl->flags);
		wl12xx_for_each_wlvif_sta(wl, wlvif) {
			wl1271_recalc_rx_streaming(wl, wlvif);
		}
	}

}

static void wl1271_event_mbox_dump(struct event_mailbox *mbox)
{
	wl1271_debug(DEBUG_EVENT, "MBOX DUMP:");
	wl1271_debug(DEBUG_EVENT, "\tvector: 0x%x", mbox->events_vector);
	wl1271_debug(DEBUG_EVENT, "\tmask: 0x%x", mbox->events_mask);
}

static int wl1271_event_process(struct wl1271 *wl)
{
	struct event_mailbox *mbox = wl->mbox;
	struct ieee80211_vif *vif;
	struct wl12xx_vif *wlvif;
	u32 vector;
	bool disconnect_sta = false;
	unsigned long sta_bitmap = 0;
	int ret;

	wl1271_event_mbox_dump(mbox);

	vector = le32_to_cpu(mbox->events_vector);
	vector &= ~(le32_to_cpu(mbox->events_mask));
	wl1271_debug(DEBUG_EVENT, "vector: 0x%x", vector);

	if (vector & SCAN_COMPLETE_EVENT_ID) {
		wl1271_debug(DEBUG_EVENT, "status: 0x%x",
			     mbox->scheduled_scan_status);

		if (wl->scan_vif)
			wl->ops->scan_completed(wl,
					wl12xx_vif_to_data(wl->scan_vif));
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
			ieee80211_sched_scan_stopped(wl->hw);
			wl->sched_scanning = false;
		}
	}

	if (vector & SOFT_GEMINI_SENSE_EVENT_ID)
		wl12xx_event_soft_gemini_sense(wl,
					       mbox->soft_gemini_sense_info);

	/*
	 * We are HW_MONITOR device. On beacon loss - queue
	 * connection loss work. Cancel it on REGAINED event.
	 */
	if (vector & BSS_LOSE_EVENT_ID) {
		/* TODO: check for multi-role */
		int delay = wl->conf.conn.synch_fail_thold *
					wl->conf.conn.bss_lose_timeout;
		wl1271_info("Beacon loss detected.");

		/*
		 * if the work is already queued, it should take place. We
		 * don't want to delay the connection loss indication
		 * any more.
		 */
		ieee80211_queue_delayed_work(wl->hw, &wl->connection_loss_work,
					     msecs_to_jiffies(delay));

		wl12xx_for_each_wlvif_sta(wl, wlvif) {
			vif = wl12xx_wlvif_to_vif(wlvif);

			ieee80211_cqm_rssi_notify(
					vif,
					NL80211_CQM_RSSI_BEACON_LOSS_EVENT,
					GFP_KERNEL);
		}
	}

	if (vector & REGAINED_BSS_EVENT_ID) {
		/* TODO: check for multi-role */
		wl1271_info("Beacon regained.");
		cancel_delayed_work(&wl->connection_loss_work);

		/* sanity check - we can't lose and gain the beacon together */
		WARN(vector & BSS_LOSE_EVENT_ID,
		     "Concurrent beacon loss and gain from FW");
	}

	if (vector & RSSI_SNR_TRIGGER_0_EVENT_ID) {
		/* TODO: check actual multi-role support */
		wl1271_debug(DEBUG_EVENT, "RSSI_SNR_TRIGGER_0_EVENT");
		wl12xx_for_each_wlvif_sta(wl, wlvif) {
			wl1271_event_rssi_trigger(wl, wlvif, mbox);
		}
	}

	if (vector & BA_SESSION_RX_CONSTRAINT_EVENT_ID) {
		u8 role_id = mbox->role_id;
		wl1271_debug(DEBUG_EVENT, "BA_SESSION_RX_CONSTRAINT_EVENT_ID. "
			     "ba_allowed = 0x%x, role_id=%d",
			     mbox->rx_ba_allowed, role_id);

		wl12xx_for_each_wlvif(wl, wlvif) {
			if (role_id != 0xff && role_id != wlvif->role_id)
				continue;

			wlvif->ba_allowed = !!mbox->rx_ba_allowed;
			if (!wlvif->ba_allowed)
				wl1271_stop_ba_event(wl, wlvif);
		}
	}

	if (vector & CHANNEL_SWITCH_COMPLETE_EVENT_ID) {
		wl1271_debug(DEBUG_EVENT, "CHANNEL_SWITCH_COMPLETE_EVENT_ID. "
					  "status = 0x%x",
					  mbox->channel_switch_status);
		/*
		 * That event uses for two cases:
		 * 1) channel switch complete with status=0
		 * 2) channel switch failed status=1
		 */

		/* TODO: configure only the relevant vif */
		wl12xx_for_each_wlvif_sta(wl, wlvif) {
			bool success;

			if (!test_and_clear_bit(WLVIF_FLAG_CS_PROGRESS,
						&wlvif->flags))
				continue;

			success = mbox->channel_switch_status ? false : true;
			vif = wl12xx_wlvif_to_vif(wlvif);

			ieee80211_chswitch_done(vif, success);
		}
	}

	if ((vector & DUMMY_PACKET_EVENT_ID)) {
		wl1271_debug(DEBUG_EVENT, "DUMMY_PACKET_ID_EVENT_ID");
		ret = wl1271_tx_dummy_packet(wl);
		if (ret < 0)
			return ret;
	}

	/*
	 * "TX retries exceeded" has a different meaning according to mode.
	 * In AP mode the offending station is disconnected.
	 */
	if (vector & MAX_TX_RETRY_EVENT_ID) {
		wl1271_debug(DEBUG_EVENT, "MAX_TX_RETRY_EVENT_ID");
		sta_bitmap |= le16_to_cpu(mbox->sta_tx_retry_exceeded);
		disconnect_sta = true;
	}

	if (vector & INACTIVE_STA_EVENT_ID) {
		wl1271_debug(DEBUG_EVENT, "INACTIVE_STA_EVENT_ID");
		sta_bitmap |= le16_to_cpu(mbox->sta_aging_status);
		disconnect_sta = true;
	}

	if (vector & REMAIN_ON_CHANNEL_COMPLETE_EVENT_ID) {
		wl1271_debug(DEBUG_EVENT,
			     "REMAIN_ON_CHANNEL_COMPLETE_EVENT_ID");
		if (wl->roc_vif)
			ieee80211_ready_on_channel(wl->hw);
	}

	if (disconnect_sta) {
		u32 num_packets = wl->conf.tx.max_tx_retries;
		struct ieee80211_sta *sta;
		const u8 *addr;
		int h;

		for_each_set_bit(h, &sta_bitmap, WL12XX_MAX_LINKS) {
			bool found = false;
			/* find the ap vif connected to this sta */
			wl12xx_for_each_wlvif_ap(wl, wlvif) {
				if (!test_bit(h, wlvif->ap.sta_hlid_map))
					continue;
				found = true;
				break;
			}
			if (!found)
				continue;

			vif = wl12xx_wlvif_to_vif(wlvif);
			addr = wl->links[h].addr;

			rcu_read_lock();
			sta = ieee80211_find_sta(vif, addr);
			if (sta) {
				wl1271_debug(DEBUG_EVENT, "remove sta %d", h);
				ieee80211_report_low_ack(sta, num_packets);
			}
			rcu_read_unlock();
		}
	}
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

int wl1271_event_handle(struct wl1271 *wl, u8 mbox_num)
{
	int ret;

	wl1271_debug(DEBUG_EVENT, "EVENT on mbox %d", mbox_num);

	if (mbox_num > 1)
		return -EINVAL;

	/* first we read the mbox descriptor */
	ret = wlcore_read(wl, wl->mbox_ptr[mbox_num], wl->mbox,
			  sizeof(*wl->mbox), false);
	if (ret < 0)
		return ret;

	/* process the descriptor */
	ret = wl1271_event_process(wl);
	if (ret < 0)
		return ret;

	/*
	 * TODO: we just need this because one bit is in a different
	 * place.  Is there any better way?
	 */
	ret = wl->ops->ack_event(wl);

	return ret;
}
