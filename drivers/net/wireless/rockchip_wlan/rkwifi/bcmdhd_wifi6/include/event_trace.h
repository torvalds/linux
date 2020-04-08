/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Trace log blocks sent over HBUS
 *
 * Copyright (C) 1999-2019, Broadcom.
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: event_trace.h 693870 2017-04-05 09:03:17Z $
 */

/**
 * @file
 * @brief
 * Define the trace event ID and tag ID
 */

#ifndef	_WL_DIAG_H
#define	_WL_DIAG_H

#define DIAG_MAJOR_VERSION      1	/* 4 bits */
#define DIAG_MINOR_VERSION      0	/* 4 bits */
#define DIAG_MICRO_VERSION      0	/* 4 bits */

#define DIAG_VERSION		\
	((DIAG_MICRO_VERSION&0xF) | (DIAG_MINOR_VERSION&0xF)<<4 | \
	(DIAG_MAJOR_VERSION&0xF)<<8)
					/* bit[11:8] major ver */
					/* bit[7:4] minor ver */
					/* bit[3:0] micro ver */

/* event ID for trace purpose only, to avoid the conflict with future new
* WLC_E_ , starting from 0x8000
*/
#define TRACE_FW_AUTH_STARTED			0x8000
#define TRACE_FW_ASSOC_STARTED			0x8001
#define TRACE_FW_RE_ASSOC_STARTED		0x8002
#define TRACE_G_SCAN_STARTED			0x8003
#define TRACE_ROAM_SCAN_STARTED			0x8004
#define TRACE_ROAM_SCAN_COMPLETE		0x8005
#define TRACE_FW_EAPOL_FRAME_TRANSMIT_START	0x8006
#define TRACE_FW_EAPOL_FRAME_TRANSMIT_STOP	0x8007
#define TRACE_BLOCK_ACK_NEGOTIATION_COMPLETE	0x8008	/* protocol status */
#define TRACE_BT_COEX_BT_SCO_START		0x8009
#define TRACE_BT_COEX_BT_SCO_STOP		0x800a
#define TRACE_BT_COEX_BT_SCAN_START		0x800b
#define TRACE_BT_COEX_BT_SCAN_STOP		0x800c
#define TRACE_BT_COEX_BT_HID_START		0x800d
#define TRACE_BT_COEX_BT_HID_STOP		0x800e
#define TRACE_ROAM_AUTH_STARTED			0x800f
/* Event ID for NAN, start from 0x9000 */
#define TRACE_NAN_CLUSTER_STARTED               0x9000
#define TRACE_NAN_CLUSTER_JOINED                0x9001
#define TRACE_NAN_CLUSTER_MERGED                0x9002
#define TRACE_NAN_ROLE_CHANGED                  0x9003
#define TRACE_NAN_SCAN_COMPLETE                 0x9004
#define TRACE_NAN_STATUS_CHNG                   0x9005

/* Parameters of wifi logger events are TLVs */
/* Event parameters tags are defined as: */
#define TRACE_TAG_VENDOR_SPECIFIC	0 /* take a byte stream as parameter */
#define TRACE_TAG_BSSID			1 /* takes a 6 bytes MAC address as parameter */
#define TRACE_TAG_ADDR			2 /* takes a 6 bytes MAC address as parameter */
#define TRACE_TAG_SSID			3 /* takes a 32 bytes SSID address as parameter */
#define TRACE_TAG_STATUS		4 /* takes an integer as parameter */
#define TRACE_TAG_CHANNEL_SPEC		5 /* takes one or more wifi_channel_spec as */
					  /* parameter */
#define TRACE_TAG_WAKE_LOCK_EVENT	6 /* takes a wake_lock_event struct as parameter */
#define TRACE_TAG_ADDR1			7 /* takes a 6 bytes MAC address as parameter */
#define TRACE_TAG_ADDR2			8 /* takes a 6 bytes MAC address as parameter */
#define TRACE_TAG_ADDR3			9 /* takes a 6 bytes MAC address as parameter */
#define TRACE_TAG_ADDR4			10 /* takes a 6 bytes MAC address as parameter */
#define TRACE_TAG_TSF			11 /* take a 64 bits TSF value as parameter */
#define TRACE_TAG_IE			12 /* take one or more specific 802.11 IEs */
					   /* parameter, IEs are in turn indicated in */
					   /* TLV format as per 802.11 spec */
#define TRACE_TAG_INTERFACE		13 /* take interface name as parameter */
#define TRACE_TAG_REASON_CODE		14 /* take a reason code as per 802.11 */
					   /* as parameter */
#define TRACE_TAG_RATE_MBPS		15 /* take a wifi rate in 0.5 mbps */
#define TRACE_TAG_REQUEST_ID		16 /* take an integer as parameter */
#define TRACE_TAG_BUCKET_ID		17 /* take an integer as parameter */
#define TRACE_TAG_GSCAN_PARAMS		18 /* takes a wifi_scan_cmd_params struct as parameter */
#define TRACE_TAG_GSCAN_CAPABILITIES	19 /* takes a wifi_gscan_capabilities struct as parameter */
#define TRACE_TAG_SCAN_ID		20 /* take an integer as parameter */
#define TRACE_TAG_RSSI			21 /* take an integer as parameter */
#define TRACE_TAG_CHANNEL		22 /* take an integer as parameter */
#define TRACE_TAG_LINK_ID		23 /* take an integer as parameter */
#define TRACE_TAG_LINK_ROLE		24 /* take an integer as parameter */
#define TRACE_TAG_LINK_STATE		25 /* take an integer as parameter */
#define TRACE_TAG_LINK_TYPE		26 /* take an integer as parameter */
#define TRACE_TAG_TSCO			27 /* take an integer as parameter */
#define TRACE_TAG_RSCO			28 /* take an integer as parameter */
#define TRACE_TAG_EAPOL_MESSAGE_TYPE	29 /* take an integer as parameter */
					   /* M1-1, M2-2, M3-3, M4-4 */

typedef union {
	struct {
		uint16 event:	16;
		uint16 version:	16;
	};
	uint32 t;
} wl_event_log_id_ver_t;

#endif	/* _WL_DIAG_H */
