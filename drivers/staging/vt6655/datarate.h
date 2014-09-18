/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 *
 * File: datarate.h
 *
 * Purpose: Handles the auto fallback & data rates functions
 *
 * Author: Lyndon Chen
 *
 * Date: July 16, 2002
 *
 */
#ifndef __DATARATE_H__
#define __DATARATE_H__

#define FALLBACK_PKT_COLLECT_TR_H  50
#define FALLBACK_PKT_COLLECT_TR_L  10
#define FALLBACK_POLL_SECOND       5
#define FALLBACK_RECOVER_SECOND    30
#define FALLBACK_THRESHOLD         15
#define UPGRADE_THRESHOLD          5
#define UPGRADE_CNT_THRD           3
#define RETRY_TIMES_THRD_H         2
#define RETRY_TIMES_THRD_L         1

void
RATEvParseMaxRate(
	void *pDeviceHandler,
	PWLAN_IE_SUPP_RATES pItemRates,
	PWLAN_IE_SUPP_RATES pItemExtRates,
	bool bUpdateBasicRate,
	unsigned short *pwMaxBasicRate,
	unsigned short *pwMaxSuppRate,
	unsigned short *pwSuppRate,
	unsigned char *pbyTopCCKRate,
	unsigned char *pbyTopOFDMRate
);

void
RATEvTxRateFallBack(
	void *pDeviceHandler,
	PKnownNodeDB psNodeDBTable
);

unsigned char
RATEuSetIE(
	PWLAN_IE_SUPP_RATES pSrcRates,
	PWLAN_IE_SUPP_RATES pDstRates,
	unsigned int uRateLen
);

unsigned short
wGetRateIdx(
	unsigned char byRate
);

unsigned char
DATARATEbyGetRateIdx(
	unsigned char byRate
);

#endif //__DATARATE_H__
