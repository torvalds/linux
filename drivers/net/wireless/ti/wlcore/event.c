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

void wlcore_event_rssi_trigger(struct wl1271 *wl, s8 *metric_arr)
{
	struct wl12xx_vif *wlvif;
	struct ieee80211_vif *vif;
	enum nl80211_cqm_rssi_threshold_event event;
	s8 metric = metric_arr[0];

	wl1271_debug(DEBUG_EVENT, "RSSI trigger metric: %d", metric);

	/* TODO: check actual multi-role support */
	wl12xx_for_each_wlvif_sta(wl, wlvif) {
		if (metric <= wlvif->rssi_thold)
			event = NL80211_CQM_RSSI_THRESHOLD_EVENT_LOW;
		else
			event = NL80211_CQM_RSSI_THRESHOLD_EVENT_HIGH;

		vif = wl12xx_wlvif_to_vif(wlvif);
		if (event != wlvif->last_rssi_event)
			ieee80211_cqm_rssi_notify(vif, event, GFP_KERNEL);
		wlvif->last_rssi_event = event;
	}
}
EXPORT_SYMBOL_GPL(wlcore_event_rssi_trigger);

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

void wlcore_event_soft_gemini_sense(struct wl1271 *wl, u8 enable)
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
EXPORT_SYMBOL_GPL(wlcore_event_soft_gemini_sense);

void wlcore_event_sched_scan_report(struct wl1271 *wl,
				    u8 status)
{
	wl1271_debug(DEBUG_EVENT, "PERIODIC_SCAN_REPORT_EVENT (status 0x%0x)",
		     status);

	wl1271_scan_sched_scan_results(wl);
}
EXPORT_SYMBOL_GPL(wlcore_event_sched_scan_report);

void wlcore_event_sched_scan_completed(struct wl1271 *wl,
				       u8 status)
{
	wl1271_debug(DEBUG_EVENT, "PERIODIC_SCAN_COMPLETE_EVENT (status 0x%0x)",
		     status);

	if (wl->sched_vif) {
		ieee80211_sched_scan_stopped(wl->hw);
		wl->sched_vif = NULL;
	}
}
EXPORT_SYMBOL_GPL(wlcore_event_sched_scan_completed);

void wlcore_event_ba_rx_constraint(struct wl1271 *wl,
				   unsigned long roles_bitmap,
				   unsigned long allowed_bitmap)
{
	struct wl12xx_vif *wlvif;

	wl1271_debug(DEBUG_EVENT, "%s: roles=0x%lx allowed=0x%lx",
		     __func__, roles_bitmap, allowed_bitmap);

	wl12xx_for_each_wlvif(wl, wlvif) {
		if (wlvif->role_id == WL12XX_INVALID_ROLE_ID ||
		    !test_bit(wlvif->role_id , &roles_bitmap))
			continue;

		wlvif->ba_allowed = !!test_bit(wlvif->role_id,
					       &allowed_bitmap);
		if (!wlvif->ba_allowed)
			wl1271_stop_ba_event(wl, wlvif);
	}
}
EXPORT_SYMBOL_GPL(wlcore_event_ba_rx_constraint);

void wlcore_event_channel_switch(struct wl1271 *wl,
				 unsigned long roles_bitmap,
				 bool success)
{
	struct wl12xx_vif *wlvif;
	struct ieee80211_vif *vif;

	wl1271_debug(DEBUG_EVENT, "%s: roles=0x%lx success=%d",
		     __func__, roles_bitmap, success);

	wl12xx_for_each_wlvif_sta(wl, wlvif) {
		if (wlvif->role_id == WL12XX_INVALID_ROLE_ID ||
		    !test_bit(wlvif->role_id , &roles_bitmap))
			continue;

		if (!test_and_clear_bit(WLVIF_FLAG_CS_PROGRESS,
					&wlvif->flags))
			continue;

		vif = wl12xx_wlvif_to_vif(wlvif);

		ieee80211_chswitch_done(vif, success);
		cancel_delayed_work(&wlvif->channel_switch_work);
	}
}
EXPORT_SYMBOL_GPL(wlcore_event_channel_switch);

void wlcore_event_dummy_packet(struct wl1271 *wl)
{
	wl1271_debug(DEBUG_EVENT, "DUMMY_PACKET_ID_EVENT_ID");
	wl1271_tx_dummy_packet(wl);
}
EXPORT_SYMBOL_GPL(wlcore_event_dummy_packet);

static void wlcore_disconnect_sta(struct wl1271 *wl, unsigned long sta_bitmap)
{
	u32 num_packets = wl->conf.tx.max_tx_retries;
	struct wl12xx_vif *wlvif;
	struct ieee80211_vif *vif;
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

void wlcore_event_max_tx_failure(struct wl1271 *wl, unsigned long sta_bitmap)
{
	wl1271_debug(DEBUG_EVENT, "MAX_TX_FAILURE_EVENT_ID");
	wlcore_disconnect_sta(wl, sta_bitmap);
}
EXPORT_SYMBOL_GPL(wlcore_event_max_tx_failure);

void wlcore_event_inactive_sta(struct wl1271 *wl, unsigned long sta_bitmap)
{
	wl1271_debug(DEBUG_EVENT, "INACTIVE_STA_EVENT_ID");
	wlcore_disconnect_sta(wl, sta_bitmap);
}
EXPORT_SYMBOL_GPL(wlcore_event_inactive_sta);

void wlcore_event_roc_complete(struct wl1271 *wl)
{
	wl1271_debug(DEBUG_EVENT, "REMAIN_ON_CHANNEL_COMPLETE_EVENT_ID");
	if (wl->roc_vif)
		ieee80211_ready_on_channel(wl->hw);
}
EXPORT_SYMBOL_GPL(wlcore_event_roc_complete);

void wlcore_event_beacon_loss(struct wl1271 *wl, unsigned long roles_bitmap)
{
	/*
	 * We are HW_MONITOR device. On beacon loss - queue
	 * connection loss work. Cancel it on REGAINED event.
	 */
	struct wl12xx_vif *wlvif;
	struct ieee80211_vif *vif;
	int delay = wl->conf.conn.synch_fail_thold *
				wl->conf.conn.bss_lose_timeout;

	wl1271_info("Beacon loss detected. roles:0x%lx", roles_bitmap);

	wl12xx_for_each_wlvif_sta(wl, wlvif) {
		if (wlvif->role_id == WL12XX_INVALID_ROLE_ID ||
		    !test_bit(wlvif->role_id , &roles_bitmap))
			continue;

		/*
		 * if the work is already queued, it should take place.
		 * We don't want to delay the connection loss
		 * indication any more.
		 */
		ieee80211_queue_delayed_work(wl->hw,
					     &wlvif->connection_loss_work,
					     msecs_to_jiffies(delay));

		vif = wl12xx_wlvif_to_vif(wlvif);
		ieee80211_cqm_rssi_notify(
				vif,
				NL80211_CQM_RSSI_BEACON_LOSS_EVENT,
				GFP_KERNEL);
	}
}
EXPORT_SYMBOL_GPL(wlcore_event_beacon_loss);

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
			  wl->mbox_size, false);
	if (ret < 0)
		return ret;

	/* process the descriptor */
	ret = wl->ops->process_mailbox_events(wl);
	if (ret < 0)
		return ret;

	/*
	 * TODO: we just need this because one bit is in a different
	 * place.  Is there any better way?
	 */
	ret = wl->ops->ack_event(wl);

	return ret;
}
