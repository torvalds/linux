/*
 * Copyright (c) 1996, 2005 VIA Networking Technologies, Inc.
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
 * File: IEEE11h.c
 *
 * Purpose:
 *
 * Functions:
 *
 * Revision History:
 *
 * Author: Yiching Chen
 *
 * Date: Mar. 31, 2005
 *
 */

#include "ttype.h"
#include "tmacro.h"
#include "tether.h"
#include "IEEE11h.h"
#include "device.h"
#include "wmgr.h"
#include "rxtx.h"
#include "channel.h"

/*---------------------  Static Definitions -------------------------*/

#pragma pack(1)

typedef struct _WLAN_FRAME_ACTION {
	WLAN_80211HDR_A3    Header;
	unsigned char byCategory;
	unsigned char byAction;
	unsigned char abyVars[1];
} WLAN_FRAME_ACTION, *PWLAN_FRAME_ACTION;

typedef struct _WLAN_FRAME_MSRREQ {
	WLAN_80211HDR_A3    Header;
	unsigned char byCategory;
	unsigned char byAction;
	unsigned char byDialogToken;
	WLAN_IE_MEASURE_REQ sMSRReqEIDs[1];
} WLAN_FRAME_MSRREQ, *PWLAN_FRAME_MSRREQ;

typedef struct _WLAN_FRAME_MSRREP {
	WLAN_80211HDR_A3    Header;
	unsigned char byCategory;
	unsigned char byAction;
	unsigned char byDialogToken;
	WLAN_IE_MEASURE_REP sMSRRepEIDs[1];
} WLAN_FRAME_MSRREP, *PWLAN_FRAME_MSRREP;

typedef struct _WLAN_FRAME_TPCREQ {
	WLAN_80211HDR_A3    Header;
	unsigned char byCategory;
	unsigned char byAction;
	unsigned char byDialogToken;
	WLAN_IE_TPC_REQ     sTPCReqEIDs;
} WLAN_FRAME_TPCREQ, *PWLAN_FRAME_TPCREQ;

typedef struct _WLAN_FRAME_TPCREP {
	WLAN_80211HDR_A3    Header;
	unsigned char byCategory;
	unsigned char byAction;
	unsigned char byDialogToken;
	WLAN_IE_TPC_REP     sTPCRepEIDs;
} WLAN_FRAME_TPCREP, *PWLAN_FRAME_TPCREP;

#pragma pack()

/* action field reference ieee 802.11h Table 20e */
#define ACTION_MSRREQ       0
#define ACTION_MSRREP       1
#define ACTION_TPCREQ       2
#define ACTION_TPCREP       3
#define ACTION_CHSW         4

/*---------------------  Static Classes  ----------------------------*/

/*---------------------  Static Variables  --------------------------*/

/*---------------------  Static Functions  --------------------------*/

/*---------------------  Export Variables  --------------------------*/

/*---------------------  Export Functions  --------------------------*/

bool IEEE11hbMSRRepTx(void *pMgmtHandle)
{
	PSMgmtObject            pMgmt = (PSMgmtObject) pMgmtHandle;
	PWLAN_FRAME_MSRREP      pMSRRep = (PWLAN_FRAME_MSRREP)
		(pMgmt->abyCurrentMSRRep + sizeof(STxMgmtPacket));
	size_t                    uLength = 0;
	PSTxMgmtPacket          pTxPacket = NULL;

	pTxPacket = (PSTxMgmtPacket)pMgmt->abyCurrentMSRRep;
	memset(pTxPacket, 0, sizeof(STxMgmtPacket) + WLAN_A3FR_MAXLEN);
	pTxPacket->p80211Header = (PUWLAN_80211HDR)((unsigned char *)pTxPacket +
						    sizeof(STxMgmtPacket));

	pMSRRep->Header.wFrameCtl = (WLAN_SET_FC_FTYPE(WLAN_FTYPE_MGMT) |
				     WLAN_SET_FC_FSTYPE(WLAN_FSTYPE_ACTION)
);

	memcpy(pMSRRep->Header.abyAddr1, ((PWLAN_FRAME_MSRREQ)
					  (pMgmt->abyCurrentMSRReq))->Header.abyAddr2, WLAN_ADDR_LEN);
	memcpy(pMSRRep->Header.abyAddr2,
	       CARDpGetCurrentAddress(pMgmt->pAdapter), WLAN_ADDR_LEN);
	memcpy(pMSRRep->Header.abyAddr3, pMgmt->abyCurrBSSID, WLAN_BSSID_LEN);

	pMSRRep->byCategory = 0;
	pMSRRep->byAction = 1;
	pMSRRep->byDialogToken = ((PWLAN_FRAME_MSRREQ)
				  (pMgmt->abyCurrentMSRReq))->byDialogToken;

	uLength = pMgmt->uLengthOfRepEIDs + offsetof(WLAN_FRAME_MSRREP,
						     sMSRRepEIDs);

	pTxPacket->cbMPDULen = uLength;
	pTxPacket->cbPayloadLen = uLength - WLAN_HDR_ADDR3_LEN;
	if (csMgmt_xmit(pMgmt->pAdapter, pTxPacket) != CMD_STATUS_PENDING)
		return false;
	return true;
}
