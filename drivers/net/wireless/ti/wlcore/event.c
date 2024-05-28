// SPDX-License-Identifier: GPL-2.0-only
/*
 * This file is part of wl1271
 *
 * Copyright (C) 2008-2009 Nokia Corporation
 *
 * Contact: Luciano Coelho <luciano.coelho@nokia.com>
 */

#include "wlcore.h"
#include "debug.h"
#include "io.h"
#include "event.h"
#include "ps.h"
#include "scan.h"
#include "wl12xx_80211.h"
#include "hw_ops.h"

#define WL18XX_LOGGER_SDIO_BUFF_MAX	(0x1020)
#define WL18XX_DATA_RAM_BASE_ADDRESS	(0x20000000)
#define WL18XX_LOGGER_SDIO_BUFF_ADDR	(0x40159c)
#define WL18XX_LOGGER_BUFF_OFFSET	(sizeof(struct fw_logger_information))
#define WL18XX_LOGGER_READ_POINT_OFFSET		(12)

int wlcore_event_fw_logger(struct wl1271 *wl)
{
	int ret;
	struct fw_logger_information fw_log;
	u8  *buffer;
	u32 internal_fw_addrbase = WL18XX_DATA_RAM_BASE_ADDRESS;
	u32 addr = WL18XX_LOGGER_SDIO_BUFF_ADDR;
	u32 addr_ptr;
	u32 buff_start_ptr;
	u32 buff_read_ptr;
	u32 buff_end_ptr;
	u32 available_len;
	u32 actual_len;
	u32 clear_ptr;
	size_t len;
	u32 start_loc;

	buffer = kzalloc(WL18XX_LOGGER_SDIO_BUFF_MAX, GFP_KERNEL);
	if (!buffer) {
		wl1271_error("Fail to allocate fw logger memory");
		actual_len = 0;
		goto out;
	}

	ret = wlcore_read(wl, addr, buffer, WL18XX_LOGGER_SDIO_BUFF_MAX,
			  false);
	if (ret < 0) {
		wl1271_error("Fail to read logger buffer, error_id = %d",
			     ret);
		actual_len = 0;
		goto free_out;
	}

	memcpy(&fw_log, buffer, sizeof(fw_log));

	actual_len = le32_to_cpu(fw_log.actual_buff_size);
	if (actual_len == 0)
		goto free_out;

	/* Calculate the internal pointer to the fwlog structure */
	addr_ptr = internal_fw_addrbase + addr;

	/* Calculate the internal pointers to the start and end of log buffer */
	buff_start_ptr = addr_ptr + WL18XX_LOGGER_BUFF_OFFSET;
	buff_end_ptr = buff_start_ptr + le32_to_cpu(fw_log.max_buff_size);

	/* Read the read pointer and validate it */
	buff_read_ptr = le32_to_cpu(fw_log.buff_read_ptr);
	if (buff_read_ptr < buff_start_ptr ||
	    buff_read_ptr >= buff_end_ptr) {
		wl1271_error("buffer read pointer out of bounds: %x not in (%x-%x)\n",
			     buff_read_ptr, buff_start_ptr, buff_end_ptr);
		goto free_out;
	}

	start_loc = buff_read_ptr - addr_ptr;
	available_len = buff_end_ptr - buff_read_ptr;

	/* Copy initial part up to the end of ring buffer */
	len = min(actual_len, available_len);
	wl12xx_copy_fwlog(wl, &buffer[start_loc], len);
	clear_ptr = addr_ptr + start_loc + actual_len;
	if (clear_ptr == buff_end_ptr)
		clear_ptr = buff_start_ptr;

	/* Copy any remaining part from beginning of ring buffer */
	len = actual_len - len;
	if (len) {
		wl12xx_copy_fwlog(wl,
				  &buffer[WL18XX_LOGGER_BUFF_OFFSET],
				  len);
		clear_ptr = addr_ptr + WL18XX_LOGGER_BUFF_OFFSET + len;
	}

	/* Update the read pointer */
	ret = wlcore_write32(wl, addr + WL18XX_LOGGER_READ_POINT_OFFSET,
			     clear_ptr);
free_out:
	kfree(buffer);
out:
	return actual_len;
}
EXPORT_SYMBOL_GPL(wlcore_event_fw_logger);

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
			ieee80211_cqm_rssi_notify(vif, event, metric,
						  GFP_KERNEL);
		wlvif->last_rssi_event = event;
	}
}
EXPORT_SYMBOL_GPL(wlcore_event_rssi_trigger);

static void wl1271_stop_ba_event(struct wl1271 *wl, struct wl12xx_vif *wlvif)
{
	struct ieee80211_vif *vif = wl12xx_wlvif_to_vif(wlvif);

	if (wlvif->bss_type != BSS_TYPE_AP_BSS) {
		u8 hlid = wlvif->sta.hlid;
		if (!wl->links[hlid].ba_bitmap)
			return;
		ieee80211_stop_rx_ba_session(vif, wl->links[hlid].ba_bitmap,
					     vif->bss_conf.bssid);
	} else {
		u8 hlid;
		struct wl1271_link *lnk;
		for_each_set_bit(hlid, wlvif->ap.sta_hlid_map,
				 wl->num_links) {
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

	wl12xx_for_each_wlvif(wl, wlvif) {
		if (wlvif->role_id == WL12XX_INVALID_ROLE_ID ||
		    !test_bit(wlvif->role_id , &roles_bitmap))
			continue;

		if (!test_and_clear_bit(WLVIF_FLAG_CS_PROGRESS,
					&wlvif->flags))
			continue;

		vif = wl12xx_wlvif_to_vif(wlvif);

		if (wlvif->bss_type == BSS_TYPE_STA_BSS) {
			ieee80211_chswitch_done(vif, success, 0);
			cancel_delayed_work(&wlvif->channel_switch_work);
		} else {
			set_bit(WLVIF_FLAG_BEACON_DISABLED, &wlvif->flags);
			ieee80211_csa_finish(vif, 0);
		}
	}
}
EXPORT_SYMBOL_GPL(wlcore_event_channel_switch);

void wlcore_event_dummy_packet(struct wl1271 *wl)
{
	if (wl->plt) {
		wl1271_info("Got DUMMY_PACKET event in PLT mode.  FW bug, ignoring.");
		return;
	}

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

	for_each_set_bit(h, &sta_bitmap, wl->num_links) {
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

		vif = wl12xx_wlvif_to_vif(wlvif);

		/* don't attempt roaming in case of p2p */
		if (wlvif->p2p) {
			ieee80211_connection_loss(vif);
			continue;
		}

		/*
		 * if the work is already queued, it should take place.
		 * We don't want to delay the connection loss
		 * indication any more.
		 */
		ieee80211_queue_delayed_work(wl->hw,
					     &wlvif->connection_loss_work,
					     msecs_to_jiffies(delay));

		ieee80211_cqm_beacon_loss_notify(vif, GFP_KERNEL);
	}
}
EXPORT_SYMBOL_GPL(wlcore_event_beacon_loss);

int wl1271_event_unmask(struct wl1271 *wl)
{
	int ret;

	wl1271_debug(DEBUG_EVENT, "unmasking event_mask 0x%x", wl->event_mask);
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
