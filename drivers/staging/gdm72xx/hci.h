/*
 * Copyright (c) 2012 GCT Semiconductor, Inc. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#ifndef __GDM72XX_HCI_H__
#define __GDM72XX_HCI_H__

#define HCI_HEADER_SIZE		4
#define HCI_VALUE_OFFS		(HCI_HEADER_SIZE)
#define HCI_MAX_PACKET		2048
#define HCI_MAX_PARAM		(HCI_MAX_PACKET-HCI_HEADER_SIZE)
#define HCI_MAX_TLV		32

/* CMD-EVT */

/* Category 0 */
#define WIMAX_RESET		0x0000
#define WIMAX_SET_INFO		0x0001
#define WIMAX_GET_INFO		0x0002
#define WIMAX_GET_INFO_RESULT	0x8003
#define WIMAX_RADIO_OFF		0x0004
#define WIMAX_RADIO_ON		0x0006
#define WIMAX_WIMAX_RESET	0x0007	/* Is this still here */

/* Category 1 */
#define WIMAX_NET_ENTRY			0x0100
#define WIMAX_NET_DISCONN		0x0102
#define WIMAX_ENTER_SLEEP		0x0103
#define WIMAX_EXIT_SLEEP		0x0104
#define WIMAX_ENTER_IDLE		0x0105
#define WIMAX_EXIT_IDLE			0x0106
#define WIMAX_MODE_CHANGE		0x8108
#define WIMAX_HANDOVER			0x8109	/* obsolete */
#define WIMAX_SCAN			0x010d
#define WIMAX_SCAN_COMPLETE		0x810e
#define WIMAX_SCAN_RESULT		0x810f
#define WIMAX_CONNECT			0x0110
#define WIMAX_CONNECT_START		0x8111
#define WIMAX_CONNECT_COMPLETE		0x8112
#define WIMAX_ASSOC_START		0x8113
#define WIMAX_ASSOC_COMPLETE		0x8114
#define WIMAX_DISCONN_IND		0x8115
#define WIMAX_ENTRY_IND			0x8116
#define WIMAX_HO_START			0x8117
#define WIMAX_HO_COMPLETE		0x8118
#define WIMAX_RADIO_STATE_IND		0x8119
#define WIMAX_IP_RENEW_IND		0x811a
#define WIMAX_DISCOVER_NSP		0x011d
#define WIMAX_DISCOVER_NSP_RESULT	0x811e
#define WIMAX_SDU_TX_FLOW		0x8125

/* Category 2 */
#define WIMAX_TX_EAP		0x0200
#define WIMAX_RX_EAP		0x8201
#define WIMAX_TX_SDU		0x0202
#define WIMAX_RX_SDU		0x8203
#define WIMAX_RX_SDU_AGGR	0x8204
#define WIMAX_TX_SDU_AGGR	0x0205

/* Category 3 */
#define WIMAX_DM_CMD		0x030a
#define WIMAX_DM_RSP		0x830b

#define WIMAX_CLI_CMD		0x030c
#define WIMAX_CLI_RSP		0x830d

#define WIMAX_DL_IMAGE		0x0310
#define WIMAX_DL_IMAGE_STATUS	0x8311
#define WIMAX_UL_IMAGE		0x0312
#define WIMAX_UL_IMAGE_RESULT	0x8313
#define WIMAX_UL_IMAGE_STATUS	0x0314
#define WIMAX_EVT_MODEM_REPORT	0x8325

/* Category 0xF */
#define WIMAX_FSM_UPDATE	0x8F01
#define WIMAX_IF_UPDOWN		0x8F02
#define WIMAX_IF_UP		1
#define WIMAX_IF_DOWN		2

/* WIMAX mode */
#define W_NULL		0
#define W_STANDBY	1
#define W_OOZ		2
#define W_AWAKE		3
#define W_IDLE		4
#define W_SLEEP		5
#define W_WAIT		6

#define W_NET_ENTRY_RNG		0x80
#define W_NET_ENTRY_SBC		0x81
#define W_NET_ENTRY_PKM		0x82
#define W_NET_ENTRY_REG		0x83
#define W_NET_ENTRY_DSX		0x84

#define W_NET_ENTRY_RNG_FAIL	0x1100100
#define W_NET_ENTRY_SBC_FAIL	0x1100200
#define W_NET_ENTRY_PKM_FAIL	0x1102000
#define W_NET_ENTRY_REG_FAIL	0x1103000
#define W_NET_ENTRY_DSX_FAIL	0x1104000

/* Scan Type */
#define W_SCAN_ALL_CHANNEL		0
#define W_SCAN_ALL_SUBSCRIPTION		1
#define W_SCAN_SPECIFIED_SUBSCRIPTION	2

/* TLV
 *
 * [31:31] indicates the type is composite.
 * [30:16] is the length of the type. 0 length means length is variable.
 * [15:0] is the actual type.
 */
#define TLV_L(x)		(((x) >> 16) & 0xff)
#define TLV_T(x)		((x) & 0xff)
#define TLV_COMPOSITE(x)	((x) >> 31)

/* GENERAL */
#define T_MAC_ADDRESS			(0x00	| (6 << 16))
#define T_BSID				(0x01	| (6 << 16))
#define T_MSK				(0x02	| (64 << 16))
#define T_RSSI_THRSHLD			(0x03	| (1 << 16))
#define T_FREQUENCY			(0x04	| (4 << 16))
#define T_CONN_CS_TYPE			(0x05	| (1 << 16))
#define T_HOST_IP_VER			(0x06	| (1 << 16))
#define T_STBY_SCAN_INTERVAL		(0x07	| (4  << 16))
#define T_OOZ_SCAN_INTERVAL		(0x08	| (4 << 16))
#define T_IMEI				(0x09	| (8 << 16))
#define T_PID				(0x0a	| (12 << 16))
#define T_CAPABILITY			(0x1a	| (4 << 16))
#define T_RELEASE_NUMBER		(0x1b	| (4 << 16))
#define T_DRIVER_REVISION		(0x1c	| (4 << 16))
#define T_FW_REVISION			(0x1d	| (4 << 16))
#define T_MAC_HW_REVISION		(0x1e	| (4 << 16))
#define T_PHY_HW_REVISION		(0x1f	| (4 << 16))

/* HANDOVER */
#define T_SCAN_INTERVAL			(0x20	| (1 << 16))
#define T_RSC_RETAIN_TIME		(0x2f	| (2 << 16))

/* SLEEP */
#define T_TYPE1_ISW			(0x40	| (1 << 16))
#define T_SLP_START_TO			(0x4a	| (2 << 16))

/* IDLE */
#define T_IDLE_MODE_TO			(0x50	| (2 << 16))
#define T_IDLE_START_TO			(0x54	| (2 << 16))

/* MONITOR */
#define T_RSSI				(0x60	| (1 << 16))
#define T_CINR				(0x61	| (1 << 16))
#define T_TX_POWER			(0x6a   | (1 << 16))
#define T_CUR_FREQ			(0x7f	| (4 << 16))


/* WIMAX */
#define T_MAX_SUBSCRIPTION		(0xa1	| (1 << 16))
#define T_MAX_SF			(0xa2	| (1 << 16))
#define T_PHY_TYPE			(0xa3	| (1 << 16))
#define T_PKM				(0xa4	| (1 << 16))
#define T_AUTH_POLICY			(0xa5	| (1 << 16))
#define T_CS_TYPE			(0xa6	| (2 << 16))
#define T_VENDOR_NAME			(0xa7	| (0 << 16))
#define T_MOD_NAME			(0xa8	| (0 << 16))
#define T_PACKET_FILTER			(0xa9	| (1 << 16))
#define T_NSP_CHANGE_COUNT		(0xaa	| (4 << 16))
#define T_RADIO_STATE			(0xab	| (1 << 16))
#define T_URI_CONTACT_TYPE		(0xac	| (1 << 16))
#define T_URI_TEXT			(0xad	| (0 << 16))
#define T_URI				(0xae	| (0 << 16))
#define T_ENABLE_AUTH			(0xaf	| (1 << 16))
#define T_TIMEOUT			(0xb0   | (2 << 16))
#define T_RUN_MODE			(0xb1	| (1 << 16))
#define T_OMADMT_VER			(0xb2	| (4 << 16))
/* This is measured in seconds from 00:00:00 GMT January 1, 1970. */
#define T_RTC_TIME			(0xb3	| (4 << 16))
#define T_CERT_STATUS			(0xb4	| (4 << 16))
#define T_CERT_MASK			(0xb5	| (4 << 16))
#define T_EMSK				(0xb6	| (64 << 16))

/* Subscription TLV */
#define T_SUBSCRIPTION_LIST		(0xd1	| (0 << 16) | (1 << 31))
#define T_H_NSPID			(0xd2	| (3 << 16))
#define T_NSP_NAME			(0xd3	| (0 << 16))
#define T_SUBSCRIPTION_NAME		(0xd4	| (0 << 16))
#define T_SUBSCRIPTION_FLAG		(0xd5	| (2 << 16))
#define T_V_NSPID			(0xd6	| (3 << 16))
#define T_NAP_ID			(0xd7	| (3 << 16))
#define T_PREAMBLES			(0xd8	| (15 << 16))
#define T_BW				(0xd9	| (4 << 16))
#define T_FFTSIZE			(0xda	| (4 << 16))
#define T_DUPLEX_MODE			(0xdb	| (4 << 16))

struct hci_s {
	__be16	cmd_evt;
	__be16	length;
	u8	data[0];
} __packed;

#endif /* __GDM72XX_HCI_H__ */
