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

enum direction_value {
	DIR_UP			= 0,
	DIR_DOWN		= 1,
	DIR_DIRECT		= 2,
	DIR_BI_DIR		= 3,
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
