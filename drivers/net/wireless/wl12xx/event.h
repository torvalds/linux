/*
 * This file is part of wl1271
 *
 * Copyright (C) 1998-2009 Texas Instruments. All rights reserved.
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

#ifndef __EVENT_H__
#define __EVENT_H__

/*
 * Mbox events
 *
 * The event mechanism is based on a pair of event buffers (buffers A and
 * B) at fixed locations in the target's memory. The host processes one
 * buffer while the other buffer continues to collect events. If the host
 * is not processing events, an interrupt is issued to signal that a buffer
 * is ready. Once the host is done with processing events from one buffer,
 * it signals the target (with an ACK interrupt) that the event buffer is
 * free.
 */

enum {
	RSSI_SNR_TRIGGER_0_EVENT_ID              = BIT(0),
	RSSI_SNR_TRIGGER_1_EVENT_ID              = BIT(1),
	RSSI_SNR_TRIGGER_2_EVENT_ID              = BIT(2),
	RSSI_SNR_TRIGGER_3_EVENT_ID              = BIT(3),
	RSSI_SNR_TRIGGER_4_EVENT_ID              = BIT(4),
	RSSI_SNR_TRIGGER_5_EVENT_ID              = BIT(5),
	RSSI_SNR_TRIGGER_6_EVENT_ID              = BIT(6),
	RSSI_SNR_TRIGGER_7_EVENT_ID              = BIT(7),
	MEASUREMENT_START_EVENT_ID		 = BIT(8),
	MEASUREMENT_COMPLETE_EVENT_ID		 = BIT(9),
	SCAN_COMPLETE_EVENT_ID			 = BIT(10),
	WFD_DISCOVERY_COMPLETE_EVENT_ID		 = BIT(11),
	AP_DISCOVERY_COMPLETE_EVENT_ID		 = BIT(12),
	PS_REPORT_EVENT_ID			 = BIT(13),
	PSPOLL_DELIVERY_FAILURE_EVENT_ID	 = BIT(14),
	DISCONNECT_EVENT_COMPLETE_ID		 = BIT(15),
	/* BIT(16) is reserved */
	CHANNEL_SWITCH_COMPLETE_EVENT_ID	 = BIT(17),
	BSS_LOSE_EVENT_ID			 = BIT(18),
	REGAINED_BSS_EVENT_ID			 = BIT(19),
	MAX_TX_RETRY_EVENT_ID			 = BIT(20),
	DUMMY_PACKET_EVENT_ID			 = BIT(21),
	SOFT_GEMINI_SENSE_EVENT_ID		 = BIT(22),
	CHANGE_AUTO_MODE_TIMEOUT_EVENT_ID	 = BIT(23),
	SOFT_GEMINI_AVALANCHE_EVENT_ID		 = BIT(24),
	PLT_RX_CALIBRATION_COMPLETE_EVENT_ID	 = BIT(25),
	INACTIVE_STA_EVENT_ID			 = BIT(26),
	PEER_REMOVE_COMPLETE_EVENT_ID		 = BIT(27),
	PERIODIC_SCAN_COMPLETE_EVENT_ID		 = BIT(28),
	PERIODIC_SCAN_REPORT_EVENT_ID		 = BIT(29),
	BA_SESSION_RX_CONSTRAINT_EVENT_ID	 = BIT(30),
	REMAIN_ON_CHANNEL_COMPLETE_EVENT_ID	 = BIT(31),
	EVENT_MBOX_ALL_EVENT_ID			 = 0x7fffffff,
};

enum {
	EVENT_ENTER_POWER_SAVE_FAIL = 0,
	EVENT_ENTER_POWER_SAVE_SUCCESS,
};

#define NUM_OF_RSSI_SNR_TRIGGERS 8

struct event_mailbox {
	__le32 events_vector;
	__le32 events_mask;
	__le32 reserved_1;
	__le32 reserved_2;

	u8 number_of_scan_results;
	u8 scan_tag;
	u8 completed_scan_status;
	u8 reserved_3;

	u8 soft_gemini_sense_info;
	u8 soft_gemini_protective_info;
	s8 rssi_snr_trigger_metric[NUM_OF_RSSI_SNR_TRIGGERS];
	u8 channel_switch_status;
	u8 scheduled_scan_status;
	u8 ps_status;
	/* tuned channel (roc) */
	u8 roc_channel;

	__le16 hlid_removed_bitmap;

	/* bitmap of aged stations (by HLID) */
	__le16 sta_aging_status;

	/* bitmap of stations (by HLID) which exceeded max tx retries */
	__le16 sta_tx_retry_exceeded;

	/* discovery completed results */
	u8 discovery_tag;
	u8 number_of_preq_results;
	u8 number_of_prsp_results;
	u8 reserved_5;

	/* rx ba constraint */
	u8 role_id; /* 0xFF means any role. */
	u8 rx_ba_allowed;
	u8 reserved_6[2];

	u8 ps_poll_delivery_failure_role_ids;
	u8 stopped_role_ids;
	u8 started_role_ids;
	u8 change_auto_mode_timeout;

	u8 reserved_7[12];
} __packed;

int wl1271_event_unmask(struct wl1271 *wl);
void wl1271_event_mbox_config(struct wl1271 *wl);
int wl1271_event_handle(struct wl1271 *wl, u8 mbox);
void wl1271_pspoll_work(struct work_struct *work);

#endif
