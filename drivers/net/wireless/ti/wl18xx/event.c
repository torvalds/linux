/*
 * This file is part of wl12xx
 *
 * Copyright (C) 2012 Texas Instruments. All rights reserved.
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

#include <net/genetlink.h>
#include "event.h"
#include "scan.h"
#include "conf.h"
#include "../wlcore/cmd.h"
#include "../wlcore/debug.h"
#include "../wlcore/vendor_cmd.h"

int wl18xx_wait_for_event(struct wl1271 *wl, enum wlcore_wait_event event,
			  bool *timeout)
{
	u32 local_event;

	switch (event) {
	case WLCORE_EVENT_PEER_REMOVE_COMPLETE:
		local_event = PEER_REMOVE_COMPLETE_EVENT_ID;
		break;

	case WLCORE_EVENT_DFS_CONFIG_COMPLETE:
		local_event = DFS_CHANNELS_CONFIG_COMPLETE_EVENT;
		break;

	default:
		/* event not implemented */
		return 0;
	}
	return wlcore_cmd_wait_for_event_or_timeout(wl, local_event, timeout);
}

static const char *wl18xx_radar_type_decode(u8 radar_type)
{
	switch (radar_type) {
	case RADAR_TYPE_REGULAR:
		return "REGULAR";
	case RADAR_TYPE_CHIRP:
		return "CHIRP";
	case RADAR_TYPE_NONE:
	default:
		return "N/A";
	}
}

static int wlcore_smart_config_sync_event(struct wl1271 *wl, u8 sync_channel,
					  u8 sync_band)
{
	struct sk_buff *skb;
	enum nl80211_band band;
	int freq;

	if (sync_band == WLCORE_BAND_5GHZ)
		band = NL80211_BAND_5GHZ;
	else
		band = NL80211_BAND_2GHZ;

	freq = ieee80211_channel_to_frequency(sync_channel, band);

	wl1271_debug(DEBUG_EVENT,
		     "SMART_CONFIG_SYNC_EVENT_ID, freq: %d (chan: %d band %d)",
		     freq, sync_channel, sync_band);
	skb = cfg80211_vendor_event_alloc(wl->hw->wiphy, NULL, 20,
					  WLCORE_VENDOR_EVENT_SC_SYNC,
					  GFP_KERNEL);

	if (nla_put_u32(skb, WLCORE_VENDOR_ATTR_FREQ, freq)) {
		kfree_skb(skb);
		return -EMSGSIZE;
	}
	cfg80211_vendor_event(skb, GFP_KERNEL);
	return 0;
}

static int wlcore_smart_config_decode_event(struct wl1271 *wl,
					    u8 ssid_len, u8 *ssid,
					    u8 pwd_len, u8 *pwd)
{
	struct sk_buff *skb;

	wl1271_debug(DEBUG_EVENT, "SMART_CONFIG_DECODE_EVENT_ID");
	wl1271_dump_ascii(DEBUG_EVENT, "SSID:", ssid, ssid_len);

	skb = cfg80211_vendor_event_alloc(wl->hw->wiphy, NULL,
					  ssid_len + pwd_len + 20,
					  WLCORE_VENDOR_EVENT_SC_DECODE,
					  GFP_KERNEL);

	if (nla_put(skb, WLCORE_VENDOR_ATTR_SSID, ssid_len, ssid) ||
	    nla_put(skb, WLCORE_VENDOR_ATTR_PSK, pwd_len, pwd)) {
		kfree_skb(skb);
		return -EMSGSIZE;
	}
	cfg80211_vendor_event(skb, GFP_KERNEL);
	return 0;
}

static void wlcore_event_time_sync(struct wl1271 *wl,
				   u16 tsf_high_msb, u16 tsf_high_lsb,
				   u16 tsf_low_msb, u16 tsf_low_lsb)
{
	u32 clock_low;
	u32 clock_high;

	clock_high = (tsf_high_msb << 16) | tsf_high_lsb;
	clock_low = (tsf_low_msb << 16) | tsf_low_lsb;

	wl1271_info("TIME_SYNC_EVENT_ID: clock_high %u, clock low %u",
		    clock_high, clock_low);
}

int wl18xx_process_mailbox_events(struct wl1271 *wl)
{
	struct wl18xx_event_mailbox *mbox = wl->mbox;
	u32 vector;

	vector = le32_to_cpu(mbox->events_vector);
	wl1271_debug(DEBUG_EVENT, "MBOX vector: 0x%x", vector);

	if (vector & SCAN_COMPLETE_EVENT_ID) {
		wl1271_debug(DEBUG_EVENT, "scan results: %d",
			     mbox->number_of_scan_results);

		if (wl->scan_wlvif)
			wl18xx_scan_completed(wl, wl->scan_wlvif);
	}

	if (vector & TIME_SYNC_EVENT_ID)
		wlcore_event_time_sync(wl,
			mbox->time_sync_tsf_high_msb,
			mbox->time_sync_tsf_high_lsb,
			mbox->time_sync_tsf_low_msb,
			mbox->time_sync_tsf_low_lsb);

	if (vector & RADAR_DETECTED_EVENT_ID) {
		wl1271_info("radar event: channel %d type %s",
			    mbox->radar_channel,
			    wl18xx_radar_type_decode(mbox->radar_type));

		if (!wl->radar_debug_mode)
			ieee80211_radar_detected(wl->hw);
	}

	if (vector & PERIODIC_SCAN_REPORT_EVENT_ID) {
		wl1271_debug(DEBUG_EVENT,
			     "PERIODIC_SCAN_REPORT_EVENT (results %d)",
			     mbox->number_of_sched_scan_results);

		wlcore_scan_sched_scan_results(wl);
	}

	if (vector & PERIODIC_SCAN_COMPLETE_EVENT_ID)
		wlcore_event_sched_scan_completed(wl, 1);

	if (vector & RSSI_SNR_TRIGGER_0_EVENT_ID)
		wlcore_event_rssi_trigger(wl, mbox->rssi_snr_trigger_metric);

	if (vector & BA_SESSION_RX_CONSTRAINT_EVENT_ID)
		wlcore_event_ba_rx_constraint(wl,
				le16_to_cpu(mbox->rx_ba_role_id_bitmap),
				le16_to_cpu(mbox->rx_ba_allowed_bitmap));

	if (vector & BSS_LOSS_EVENT_ID)
		wlcore_event_beacon_loss(wl,
					 le16_to_cpu(mbox->bss_loss_bitmap));

	if (vector & CHANNEL_SWITCH_COMPLETE_EVENT_ID)
		wlcore_event_channel_switch(wl,
			le16_to_cpu(mbox->channel_switch_role_id_bitmap),
			true);

	if (vector & DUMMY_PACKET_EVENT_ID)
		wlcore_event_dummy_packet(wl);

	/*
	 * "TX retries exceeded" has a different meaning according to mode.
	 * In AP mode the offending station is disconnected.
	 */
	if (vector & MAX_TX_FAILURE_EVENT_ID)
		wlcore_event_max_tx_failure(wl,
				le16_to_cpu(mbox->tx_retry_exceeded_bitmap));

	if (vector & INACTIVE_STA_EVENT_ID)
		wlcore_event_inactive_sta(wl,
				le16_to_cpu(mbox->inactive_sta_bitmap));

	if (vector & REMAIN_ON_CHANNEL_COMPLETE_EVENT_ID)
		wlcore_event_roc_complete(wl);

	if (vector & SMART_CONFIG_SYNC_EVENT_ID)
		wlcore_smart_config_sync_event(wl, mbox->sc_sync_channel,
					       mbox->sc_sync_band);

	if (vector & SMART_CONFIG_DECODE_EVENT_ID)
		wlcore_smart_config_decode_event(wl,
						 mbox->sc_ssid_len,
						 mbox->sc_ssid,
						 mbox->sc_pwd_len,
						 mbox->sc_pwd);
	if (vector & FW_LOGGER_INDICATION)
		wlcore_event_fw_logger(wl);

	return 0;
}
