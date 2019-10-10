/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * This file is part of wl1251
 *
 * Copyright (c) 1998-2007 Texas Instruments Incorporated
 * Copyright (C) 2008 Nokia Corporation
 */

#ifndef __WL1251_EVENT_H__
#define __WL1251_EVENT_H__

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
	RESERVED1_EVENT_ID                       = BIT(0),
	RESERVED2_EVENT_ID                       = BIT(1),
	MEASUREMENT_START_EVENT_ID               = BIT(2),
	SCAN_COMPLETE_EVENT_ID                   = BIT(3),
	CALIBRATION_COMPLETE_EVENT_ID            = BIT(4),
	ROAMING_TRIGGER_LOW_RSSI_EVENT_ID        = BIT(5),
	PS_REPORT_EVENT_ID                       = BIT(6),
	SYNCHRONIZATION_TIMEOUT_EVENT_ID         = BIT(7),
	HEALTH_REPORT_EVENT_ID                   = BIT(8),
	ACI_DETECTION_EVENT_ID                   = BIT(9),
	DEBUG_REPORT_EVENT_ID                    = BIT(10),
	MAC_STATUS_EVENT_ID                      = BIT(11),
	DISCONNECT_EVENT_COMPLETE_ID             = BIT(12),
	JOIN_EVENT_COMPLETE_ID                   = BIT(13),
	CHANNEL_SWITCH_COMPLETE_EVENT_ID         = BIT(14),
	BSS_LOSE_EVENT_ID                        = BIT(15),
	ROAMING_TRIGGER_MAX_TX_RETRY_EVENT_ID    = BIT(16),
	MEASUREMENT_COMPLETE_EVENT_ID            = BIT(17),
	AP_DISCOVERY_COMPLETE_EVENT_ID           = BIT(18),
	SCHEDULED_SCAN_COMPLETE_EVENT_ID         = BIT(19),
	PSPOLL_DELIVERY_FAILURE_EVENT_ID 	 = BIT(20),
	RESET_BSS_EVENT_ID                       = BIT(21),
	REGAINED_BSS_EVENT_ID                    = BIT(22),
	ROAMING_TRIGGER_REGAINED_RSSI_EVENT_ID   = BIT(23),
	ROAMING_TRIGGER_LOW_SNR_EVENT_ID         = BIT(24),
	ROAMING_TRIGGER_REGAINED_SNR_EVENT_ID    = BIT(25),

	DBG_EVENT_ID                             = BIT(26),
	BT_PTA_SENSE_EVENT_ID                    = BIT(27),
	BT_PTA_PREDICTION_EVENT_ID               = BIT(28),
	BT_PTA_AVALANCHE_EVENT_ID                = BIT(29),

	PLT_RX_CALIBRATION_COMPLETE_EVENT_ID     = BIT(30),

	EVENT_MBOX_ALL_EVENT_ID                  = 0x7fffffff,
};

struct event_debug_report {
	u8 debug_event_id;
	u8 num_params;
	u16 pad;
	u32 report_1;
	u32 report_2;
	u32 report_3;
} __packed;

struct event_mailbox {
	u32 events_vector;
	u32 events_mask;
	u32 reserved_1;
	u32 reserved_2;

	char average_rssi_level;
	u8 ps_status;
	u8 channel_switch_status;
	u8 scheduled_scan_status;

	/* Channels scanned by the scheduled scan */
	u16 scheduled_scan_channels;

	/* If bit 0 is set -> target's fatal error */
	u16 health_report;
	u16 bad_fft_counter;
	u8 bt_pta_sense_info;
	u8 bt_pta_protective_info;
	u32 reserved;
	u32 debug_report[2];

	/* Number of FCS errors since last event */
	u32 fcs_err_counter;

	struct event_debug_report report;
	u8 average_snr_level;
	u8 padding[19];
} __packed;

enum {
	EVENT_ENTER_POWER_SAVE_FAIL = 0,
	EVENT_ENTER_POWER_SAVE_SUCCESS,
	EVENT_EXIT_POWER_SAVE_FAIL,
	EVENT_EXIT_POWER_SAVE_SUCCESS,
};

int wl1251_event_unmask(struct wl1251 *wl);
void wl1251_event_mbox_config(struct wl1251 *wl);
int wl1251_event_handle(struct wl1251 *wl, u8 mbox);
int wl1251_event_wait(struct wl1251 *wl, u32 mask, int timeout_ms);

#endif
