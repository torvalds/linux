/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 *
 * Contact Information: wlanfae <wlanfae@realtek.com>
 */
#ifndef __INC_QOS_TYPE_H
#define __INC_QOS_TYPE_H

struct qos_tsinfo {
	u8		ucTSID:4;
	u8		ucDirection:2;
};

struct octet_string {
	u8 *Octet;
	u16 Length;
};

#define AC0_BE	0
#define AC1_BK	1
#define AC2_VI	2
#define AC3_VO	3
#define AC_MAX	4

enum direction_value {
	DIR_UP			= 0,
	DIR_DOWN		= 1,
	DIR_DIRECT		= 2,
	DIR_BI_DIR		= 3,
};

struct acm {
	u64		UsedTime;
	u64		MediumTime;
	u8		HwAcmCtl;
};

union qos_tclas {
	struct _TYPE_GENERAL {
		u8		Priority;
		u8		ClassifierType;
		u8		Mask;
	} TYPE_GENERAL;

	struct _TYPE0_ETH {
		u8		Priority;
		u8		ClassifierType;
		u8		Mask;
		u8		SrcAddr[ETH_ALEN];
		u8		DstAddr[ETH_ALEN];
		u16		Type;
	} TYPE0_ETH;

	struct _TYPE1_IPV4 {
		u8		Priority;
		u8		ClassifierType;
		u8		Mask;
		u8		Version;
		u8		SrcIP[4];
		u8		DstIP[4];
		u16		SrcPort;
		u16		DstPort;
		u8		DSCP;
		u8		Protocol;
		u8		Reserved;
	} TYPE1_IPV4;

	struct _TYPE1_IPV6 {
		u8		Priority;
		u8		ClassifierType;
		u8		Mask;
		u8		Version;
		u8		SrcIP[16];
		u8		DstIP[16];
		u16		SrcPort;
		u16		DstPort;
		u8		FlowLabel[3];
	} TYPE1_IPV6;

	struct _TYPE2_8021Q {
		u8		Priority;
		u8		ClassifierType;
		u8		Mask;
		u16		TagType;
	} TYPE2_8021Q;
};

union aci_aifsn {
	u8	charData;

	struct {
		u8	AIFSN:4;
		u8	acm:1;
		u8	ACI:2;
		u8	Reserved:1;
	} f;
};

#endif
