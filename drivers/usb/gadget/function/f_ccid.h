/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2011, 2017 The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __F_CCID_H
#define __F_CCID_H

#define PROTOCOL_TO 0x01
#define PROTOCOL_T1 0x02
#define ABDATA_SIZE 512

/* define for dwFeatures for Smart Card Device Class Descriptors */
/* No special characteristics */
#define CCID_FEATURES_NADA       0x00000000
/* Automatic parameter configuration based on ATR data */
#define CCID_FEATURES_AUTO_PCONF 0x00000002
/* Automatic activation of ICC on inserting */
#define CCID_FEATURES_AUTO_ACTIV 0x00000004
/* Automatic ICC voltage selection */
#define CCID_FEATURES_AUTO_VOLT  0x00000008
/* Automatic ICC clock frequency change */
#define CCID_FEATURES_AUTO_CLOCK 0x00000010
/* Automatic baud rate change */
#define CCID_FEATURES_AUTO_BAUD  0x00000020
/*Automatic parameters negotiation made by the CCID */
#define CCID_FEATURES_AUTO_PNEGO 0x00000040
/* Automatic PPS made by the CCID according to the active parameters */
#define CCID_FEATURES_AUTO_PPS   0x00000080
/* CCID can set ICC in clock stop mode */
#define CCID_FEATURES_ICCSTOP    0x00000100
/* NAD value other than 00 accepted (T=1 protocol in use) */
#define CCID_FEATURES_NAD        0x00000200
/* Automatic IFSD exchange as first exchange (T=1 protocol in use) */
#define CCID_FEATURES_AUTO_IFSD  0x00000400
/* TPDU level exchanges with CCID */
#define CCID_FEATURES_EXC_TPDU   0x00010000
/* Short APDU level exchange with CCID */
#define CCID_FEATURES_EXC_SAPDU  0x00020000
/* Short and Extended APDU level exchange with CCID */
#define CCID_FEATURES_EXC_APDU   0x00040000
/* USB Wake up signaling supported on card insertion and removal */
#define CCID_FEATURES_WAKEUP     0x00100000

#define CCID_NOTIFY_CARD	_IOW('C', 1, struct usb_ccid_notification)
#define CCID_NOTIFY_HWERROR	_IOW('C', 2, struct usb_ccid_notification)
#define CCID_READ_DTR		_IOR('C', 3, int)

struct usb_ccid_notification {
	__u8 buf[4];
} __packed;

struct ccid_bulk_in_header {
	__u8 bMessageType;
	__u32 wLength;
	__u8 bSlot;
	__u8 bSeq;
	__u8 bStatus;
	__u8 bError;
	__u8 bSpecific;
	__u8 abData[ABDATA_SIZE];
	__u8 bSizeToSend;
} __packed;

struct ccid_bulk_out_header {
	__u8 bMessageType;
	__u32 wLength;
	__u8 bSlot;
	__u8 bSeq;
	__u8 bSpecific_0;
	__u8 bSpecific_1;
	__u8 bSpecific_2;
	__u8 APDU[ABDATA_SIZE];
} __packed;
#endif
