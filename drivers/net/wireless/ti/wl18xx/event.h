/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * This file is part of wl18xx
 *
 * Copyright (C) 2012 Texas Instruments. All rights reserved.
 */

#ifndef __WL18XX_EVENT_H__
#define __WL18XX_EVENT_H__

#include "../wlcore/wlcore.h"

enum {
	SCAN_COMPLETE_EVENT_ID                   = BIT(8),
	RADAR_DETECTED_EVENT_ID                  = BIT(9),
	CHANNEL_SWITCH_COMPLETE_EVENT_ID         = BIT(10),
	BSS_LOSS_EVENT_ID                        = BIT(11),
	MAX_TX_FAILURE_EVENT_ID                  = BIT(12),
	DUMMY_PACKET_EVENT_ID                    = BIT(13),
	INACTIVE_STA_EVENT_ID                    = BIT(14),
	PEER_REMOVE_COMPLETE_EVENT_ID            = BIT(15),
	PERIODIC_SCAN_COMPLETE_EVENT_ID          = BIT(16),
	BA_SESSION_RX_CONSTRAINT_EVENT_ID        = BIT(17),
	REMAIN_ON_CHANNEL_COMPLETE_EVENT_ID      = BIT(18),
	DFS_CHANNELS_CONFIG_COMPLETE_EVENT       = BIT(19),
	PERIODIC_SCAN_REPORT_EVENT_ID            = BIT(20),
	RX_BA_WIN_SIZE_CHANGE_EVENT_ID           = BIT(21),
	SMART_CONFIG_SYNC_EVENT_ID               = BIT(22),
	SMART_CONFIG_DECODE_EVENT_ID             = BIT(23),
	TIME_SYNC_EVENT_ID                       = BIT(24),
	FW_LOGGER_INDICATION			= BIT(25),
};

enum wl18xx_radar_types {
	RADAR_TYPE_NONE,
	RADAR_TYPE_REGULAR,
	RADAR_TYPE_CHIRP
};

struct wl18xx_event_mailbox {
	__le32 events_vector;

	u8 number_of_scan_results;
	u8 number_of_sched_scan_results;

	__le16 channel_switch_role_id_bitmap;

	s8 rssi_snr_trigger_metric[NUM_OF_RSSI_SNR_TRIGGERS];

	/* bitmap of removed links */
	__le32 hlid_removed_bitmap;

	/* rx ba constraint */
	__le16 rx_ba_role_id_bitmap; /* 0xfff means any role. */
	__le16 rx_ba_allowed_bitmap;

	/* bitmap of roc completed (by role id) */
	__le16 roc_completed_bitmap;

	/* bitmap of stations (by role id) with bss loss */
	__le16 bss_loss_bitmap;

	/* bitmap of stations (by HLID) which exceeded max tx retries */
	__le16 tx_retry_exceeded_bitmap;

	/* time sync high msb*/
	__le16 time_sync_tsf_high_msb;

	/* bitmap of inactive stations (by HLID) */
	__le16 inactive_sta_bitmap;

	/* time sync high lsb*/
	__le16 time_sync_tsf_high_lsb;

	/* rx BA win size indicated by RX_BA_WIN_SIZE_CHANGE_EVENT_ID */
	u8 rx_ba_role_id;
	u8 rx_ba_link_id;
	u8 rx_ba_win_size;
	u8 padding;

	/* smart config */
	u8 sc_ssid_len;
	u8 sc_pwd_len;
	u8 sc_token_len;
	u8 padding1;
	u8 sc_ssid[32];
	u8 sc_pwd[64];
	u8 sc_token[32];

	/* smart config sync channel */
	u8 sc_sync_channel;
	u8 sc_sync_band;

	/* time sync low msb*/
	__le16 time_sync_tsf_low_msb;

	/* radar detect */
	u8 radar_channel;
	u8 radar_type;

	/* time sync low lsb*/
	__le16 time_sync_tsf_low_lsb;

} __packed;

int wl18xx_wait_for_event(struct wl1271 *wl, enum wlcore_wait_event event,
			  bool *timeout);
int wl18xx_process_mailbox_events(struct wl1271 *wl);

#endif
