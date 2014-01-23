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
 * File: wmgr.c
 *
 * Purpose: Handles the 802.11 management functions
 *
 * Author: Lyndon Chen
 *
 * Date: May 8, 2002
 *
 * Functions:
 *      nsMgrObjectInitial - Initialize Management Object data structure
 *      vMgrObjectReset - Reset Management Object data structure
 *      vMgrAssocBeginSta - Start associate function
 *      vMgrReAssocBeginSta - Start reassociate function
 *      vMgrDisassocBeginSta - Start disassociate function
 *      s_vMgrRxAssocRequest - Handle Rcv associate_request
 *      s_vMgrRxAssocResponse - Handle Rcv associate_response
 *      vMrgAuthenBeginSta - Start authentication function
 *      vMgrDeAuthenDeginSta - Start deauthentication function
 *      s_vMgrRxAuthentication - Handle Rcv authentication
 *      s_vMgrRxAuthenSequence_1 - Handle Rcv authentication sequence 1
 *      s_vMgrRxAuthenSequence_2 - Handle Rcv authentication sequence 2
 *      s_vMgrRxAuthenSequence_3 - Handle Rcv authentication sequence 3
 *      s_vMgrRxAuthenSequence_4 - Handle Rcv authentication sequence 4
 *      s_vMgrRxDisassociation - Handle Rcv disassociation
 *      s_vMgrRxBeacon - Handle Rcv Beacon
 *      vMgrCreateOwnIBSS - Create ad_hoc IBSS or AP BSS
 *      vMgrJoinBSSBegin - Join BSS function
 *      s_vMgrSynchBSS - Synch & adopt BSS parameters
 *      s_MgrMakeBeacon - Create Baecon frame
 *      s_MgrMakeProbeResponse - Create Probe Response frame
 *      s_MgrMakeAssocRequest - Create Associate Request frame
 *      s_MgrMakeReAssocRequest - Create ReAssociate Request frame
 *      s_vMgrRxProbeResponse - Handle Rcv probe_response
 *      s_vMrgRxProbeRequest - Handle Rcv probe_request
 *      bMgrPrepareBeaconToSend - Prepare Beacon frame
 *      s_vMgrLogStatus - Log 802.11 Status
 *      vMgrRxManagePacket - Rcv management frame dispatch function
 *      s_vMgrFormatTIM- Assembler TIM field of beacon
 *      vMgrTimerInit- Initial 1-sec and command call back funtions
 *
 * Revision History:
 *
 */

#include "tmacro.h"
#include "desc.h"
#include "device.h"
#include "card.h"
#include "channel.h"
#include "80211hdr.h"
#include "80211mgr.h"
#include "wmgr.h"
#include "wcmd.h"
#include "mac.h"
#include "bssdb.h"
#include "power.h"
#include "datarate.h"
#include "baseband.h"
#include "rxtx.h"
#include "wpa.h"
#include "rf.h"
#include "iowpa.h"

#define	PLICE_DEBUG

/*---------------------  Static Definitions -------------------------*/

/*---------------------  Static Classes  ----------------------------*/

/*---------------------  Static Variables  --------------------------*/
static int msglevel = MSG_LEVEL_INFO;
//static int          msglevel                =MSG_LEVEL_DEBUG;

/*---------------------  Static Functions  --------------------------*/
//2008-8-4 <add> by chester
static bool ChannelExceedZoneType(
	PSDevice pDevice,
	unsigned char byCurrChannel
);

// Association/diassociation functions
static
PSTxMgmtPacket
s_MgrMakeAssocRequest(
	PSDevice pDevice,
	PSMgmtObject pMgmt,
	unsigned char *pDAddr,
	unsigned short wCurrCapInfo,
	unsigned short wListenInterval,
	PWLAN_IE_SSID pCurrSSID,
	PWLAN_IE_SUPP_RATES pCurrRates,
	PWLAN_IE_SUPP_RATES pCurrExtSuppRates
);

static
void
s_vMgrRxAssocRequest(
	PSDevice pDevice,
	PSMgmtObject pMgmt,
	PSRxMgmtPacket pRxPacket,
	unsigned int uNodeIndex
);

static
PSTxMgmtPacket
s_MgrMakeReAssocRequest(
	PSDevice pDevice,
	PSMgmtObject pMgmt,
	unsigned char *pDAddr,
	unsigned short wCurrCapInfo,
	unsigned short wListenInterval,
	PWLAN_IE_SSID pCurrSSID,
	PWLAN_IE_SUPP_RATES pCurrRates,
	PWLAN_IE_SUPP_RATES pCurrExtSuppRates
);

static
void
s_vMgrRxAssocResponse(
	PSDevice pDevice,
	PSMgmtObject pMgmt,
	PSRxMgmtPacket pRxPacket,
	bool bReAssocType
);

static
void
s_vMgrRxDisassociation(
	PSDevice pDevice,
	PSMgmtObject pMgmt,
	PSRxMgmtPacket pRxPacket
);

// Authentication/deauthen functions
static
void
s_vMgrRxAuthenSequence_1(
	PSDevice pDevice,
	PSMgmtObject pMgmt,
	PWLAN_FR_AUTHEN pFrame
);

static
void
s_vMgrRxAuthenSequence_2(
	PSDevice pDevice,
	PSMgmtObject pMgmt,
	PWLAN_FR_AUTHEN pFrame
);

static
void
s_vMgrRxAuthenSequence_3(
	PSDevice pDevice,
	PSMgmtObject pMgmt,
	PWLAN_FR_AUTHEN pFrame
);

static
void
s_vMgrRxAuthenSequence_4(
	PSDevice pDevice,
	PSMgmtObject pMgmt,
	PWLAN_FR_AUTHEN pFrame
);

static
void
s_vMgrRxAuthentication(
	PSDevice pDevice,
	PSMgmtObject pMgmt,
	PSRxMgmtPacket pRxPacket
);

static
void
s_vMgrRxDeauthentication(
	PSDevice pDevice,
	PSMgmtObject pMgmt,
	PSRxMgmtPacket pRxPacket
);

// Scan functions
// probe request/response functions
static
void
s_vMgrRxProbeRequest(
	PSDevice pDevice,
	PSMgmtObject pMgmt,
	PSRxMgmtPacket pRxPacket
);

static
void
s_vMgrRxProbeResponse(
	PSDevice pDevice,
	PSMgmtObject pMgmt,
	PSRxMgmtPacket pRxPacket
);

// beacon functions
static
void
s_vMgrRxBeacon(
	PSDevice pDevice,
	PSMgmtObject pMgmt,
	PSRxMgmtPacket pRxPacket,
	bool bInScan
);

static
void
s_vMgrFormatTIM(
	PSMgmtObject pMgmt,
	PWLAN_IE_TIM pTIM
);

static
PSTxMgmtPacket
s_MgrMakeBeacon(
	PSDevice pDevice,
	PSMgmtObject pMgmt,
	unsigned short wCurrCapInfo,
	unsigned short wCurrBeaconPeriod,
	unsigned int uCurrChannel,
	unsigned short wCurrATIMWinodw,
	PWLAN_IE_SSID pCurrSSID,
	unsigned char *pCurrBSSID,
	PWLAN_IE_SUPP_RATES pCurrSuppRates,
	PWLAN_IE_SUPP_RATES pCurrExtSuppRates
);

// Association response
static
PSTxMgmtPacket
s_MgrMakeAssocResponse(
	PSDevice pDevice,
	PSMgmtObject pMgmt,
	unsigned short wCurrCapInfo,
	unsigned short wAssocStatus,
	unsigned short wAssocAID,
	unsigned char *pDstAddr,
	PWLAN_IE_SUPP_RATES pCurrSuppRates,
	PWLAN_IE_SUPP_RATES pCurrExtSuppRates
);

// ReAssociation response
static
PSTxMgmtPacket
s_MgrMakeReAssocResponse(
	PSDevice pDevice,
	PSMgmtObject pMgmt,
	unsigned short wCurrCapInfo,
	unsigned short wAssocStatus,
	unsigned short wAssocAID,
	unsigned char *pDstAddr,
	PWLAN_IE_SUPP_RATES pCurrSuppRates,
	PWLAN_IE_SUPP_RATES pCurrExtSuppRates
);

// Probe response
static
PSTxMgmtPacket
s_MgrMakeProbeResponse(
	PSDevice pDevice,
	PSMgmtObject pMgmt,
	unsigned short wCurrCapInfo,
	unsigned short wCurrBeaconPeriod,
	unsigned int uCurrChannel,
	unsigned short wCurrATIMWinodw,
	unsigned char *pDstAddr,
	PWLAN_IE_SSID pCurrSSID,
	unsigned char *pCurrBSSID,
	PWLAN_IE_SUPP_RATES pCurrSuppRates,
	PWLAN_IE_SUPP_RATES pCurrExtSuppRates,
	unsigned char byPHYType
);

// received status
static
void
s_vMgrLogStatus(
	PSMgmtObject pMgmt,
	unsigned short wStatus
);

static
void
s_vMgrSynchBSS(
	PSDevice      pDevice,
	unsigned int uBSSMode,
	PKnownBSS     pCurr,
	PCMD_STATUS  pStatus
);

static bool
s_bCipherMatch(
	PKnownBSS                        pBSSNode,
	NDIS_802_11_ENCRYPTION_STATUS    EncStatus,
	unsigned char *pbyCCSPK,
	unsigned char *pbyCCSGK
);

static void  Encyption_Rebuild(
	PSDevice pDevice,
	PKnownBSS pCurr
);

/*---------------------  Export Variables  --------------------------*/

/*---------------------  Export Functions  --------------------------*/

/*+
 *
 * Routine Description:
 *    Allocates and initializes the Management object.
 *
 * Return Value:
 *    Ndis_staus.
 *
 -*/

void
vMgrObjectInit(
	void *hDeviceContext
)
{
	PSDevice     pDevice = (PSDevice)hDeviceContext;
	PSMgmtObject    pMgmt = pDevice->pMgmt;
	int ii;

	pMgmt->pbyPSPacketPool = &pMgmt->byPSPacketPool[0];
	pMgmt->pbyMgmtPacketPool = &pMgmt->byMgmtPacketPool[0];
	pMgmt->uCurrChannel = pDevice->uChannel;
	for (ii = 0; ii < WLAN_BSSID_LEN; ii++) {
		pMgmt->abyDesireBSSID[ii] = 0xFF;
	}
	pMgmt->sAssocInfo.AssocInfo.Length = sizeof(NDIS_802_11_ASSOCIATION_INFORMATION);
	//memset(pMgmt->abyDesireSSID, 0, WLAN_IEHDR_LEN + WLAN_SSID_MAXLEN +1);
	pMgmt->byCSSPK = KEY_CTL_NONE;
	pMgmt->byCSSGK = KEY_CTL_NONE;
	pMgmt->wIBSSBeaconPeriod = DEFAULT_IBSS_BI;
	BSSvClearBSSList((void *)pDevice, false);

	return;
}

/*+
 *
 * Routine Description:
 *    Initializes timer object
 *
 * Return Value:
 *    Ndis_staus.
 *
 -*/

void
vMgrTimerInit(
	void *hDeviceContext
)
{
	PSDevice     pDevice = (PSDevice)hDeviceContext;
	PSMgmtObject    pMgmt = pDevice->pMgmt;

	init_timer(&pMgmt->sTimerSecondCallback);
	pMgmt->sTimerSecondCallback.data = (unsigned long) pDevice;
	pMgmt->sTimerSecondCallback.function = (TimerFunction)BSSvSecondCallBack;
	pMgmt->sTimerSecondCallback.expires = RUN_AT(HZ);

	init_timer(&pDevice->sTimerCommand);
	pDevice->sTimerCommand.data = (unsigned long) pDevice;
	pDevice->sTimerCommand.function = (TimerFunction)vCommandTimer;
	pDevice->sTimerCommand.expires = RUN_AT(HZ);

#ifdef TxInSleep
	init_timer(&pDevice->sTimerTxData);
	pDevice->sTimerTxData.data = (unsigned long) pDevice;
	pDevice->sTimerTxData.function = (TimerFunction)BSSvSecondTxData;
	pDevice->sTimerTxData.expires = RUN_AT(10*HZ);      //10s callback
	pDevice->fTxDataInSleep = false;
	pDevice->IsTxDataTrigger = false;
	pDevice->nTxDataTimeCout = 0;
#endif

	pDevice->cbFreeCmdQueue = CMD_Q_SIZE;
	pDevice->uCmdDequeueIdx = 0;
	pDevice->uCmdEnqueueIdx = 0;

	return;
}

/*+
 *
 * Routine Description:
 *    Reset the management object structure.
 *
 * Return Value:
 *    None.
 *
 -*/

void
vMgrObjectReset(
	void *hDeviceContext
)
{
	PSDevice         pDevice = (PSDevice)hDeviceContext;
	PSMgmtObject        pMgmt = pDevice->pMgmt;

	pMgmt->eCurrMode = WMAC_MODE_STANDBY;
	pMgmt->eCurrState = WMAC_STATE_IDLE;
	pDevice->bEnablePSMode = false;
	// TODO: timer

	return;
}

/*+
 *
 * Routine Description:
 *    Start the station association procedure.  Namely, send an
 *    association request frame to the AP.
 *
 * Return Value:
 *    None.
 *
 -*/

void
vMgrAssocBeginSta(
	void *hDeviceContext,
	PSMgmtObject pMgmt,
	PCMD_STATUS pStatus
)
{
	PSDevice             pDevice = (PSDevice)hDeviceContext;
	PSTxMgmtPacket          pTxPacket;

	pMgmt->wCurrCapInfo = 0;
	pMgmt->wCurrCapInfo |= WLAN_SET_CAP_INFO_ESS(1);
	if (pDevice->bEncryptionEnable) {
		pMgmt->wCurrCapInfo |= WLAN_SET_CAP_INFO_PRIVACY(1);
	}
	pMgmt->wCurrCapInfo |= WLAN_SET_CAP_INFO_SHORTPREAMBLE(1);
	if (pMgmt->wListenInterval == 0)
		pMgmt->wListenInterval = 1;    // at least one.

	// ERP Phy (802.11g) should support short preamble.
	if (pMgmt->eCurrentPHYMode == PHY_TYPE_11G) {
		pMgmt->wCurrCapInfo |= WLAN_SET_CAP_INFO_SHORTPREAMBLE(1);
		if (CARDbIsShorSlotTime(pMgmt->pAdapter) == true) {
			pMgmt->wCurrCapInfo |= WLAN_SET_CAP_INFO_SHORTSLOTTIME(1);
		}
	} else if (pMgmt->eCurrentPHYMode == PHY_TYPE_11B) {
		if (CARDbIsShortPreamble(pMgmt->pAdapter) == true) {
			pMgmt->wCurrCapInfo |= WLAN_SET_CAP_INFO_SHORTPREAMBLE(1);
		}
	}
	if (pMgmt->b11hEnable == true)
		pMgmt->wCurrCapInfo |= WLAN_SET_CAP_INFO_SPECTRUMMNG(1);

	/* build an assocreq frame and send it */
	pTxPacket = s_MgrMakeAssocRequest
		(
			pDevice,
			pMgmt,
			pMgmt->abyCurrBSSID,
			pMgmt->wCurrCapInfo,
			pMgmt->wListenInterval,
			(PWLAN_IE_SSID)pMgmt->abyCurrSSID,
			(PWLAN_IE_SUPP_RATES)pMgmt->abyCurrSuppRates,
			(PWLAN_IE_SUPP_RATES)pMgmt->abyCurrExtSuppRates
);

	if (pTxPacket != NULL) {
		/* send the frame */
		*pStatus = csMgmt_xmit(pDevice, pTxPacket);
		if (*pStatus == CMD_STATUS_PENDING) {
			pMgmt->eCurrState = WMAC_STATE_ASSOCPENDING;
			*pStatus = CMD_STATUS_SUCCESS;
		}
	} else
		*pStatus = CMD_STATUS_RESOURCES;

	return;
}

/*+
 *
 * Routine Description:
 *    Start the station re-association procedure.
 *
 * Return Value:
 *    None.
 *
 -*/

void
vMgrReAssocBeginSta(
	void *hDeviceContext,
	PSMgmtObject pMgmt,
	PCMD_STATUS pStatus
)
{
	PSDevice             pDevice = (PSDevice)hDeviceContext;
	PSTxMgmtPacket          pTxPacket;

	pMgmt->wCurrCapInfo = 0;
	pMgmt->wCurrCapInfo |= WLAN_SET_CAP_INFO_ESS(1);
	if (pDevice->bEncryptionEnable) {
		pMgmt->wCurrCapInfo |= WLAN_SET_CAP_INFO_PRIVACY(1);
	}

	pMgmt->wCurrCapInfo |= WLAN_SET_CAP_INFO_SHORTPREAMBLE(1);

	if (pMgmt->wListenInterval == 0)
		pMgmt->wListenInterval = 1;    // at least one.

	// ERP Phy (802.11g) should support short preamble.
	if (pMgmt->eCurrentPHYMode == PHY_TYPE_11G) {
		pMgmt->wCurrCapInfo |= WLAN_SET_CAP_INFO_SHORTPREAMBLE(1);
		if (CARDbIsShorSlotTime(pMgmt->pAdapter) == true) {
			pMgmt->wCurrCapInfo |= WLAN_SET_CAP_INFO_SHORTSLOTTIME(1);
		}
	} else if (pMgmt->eCurrentPHYMode == PHY_TYPE_11B) {
		if (CARDbIsShortPreamble(pMgmt->pAdapter) == true) {
			pMgmt->wCurrCapInfo |= WLAN_SET_CAP_INFO_SHORTPREAMBLE(1);
		}
	}
	if (pMgmt->b11hEnable == true)
		pMgmt->wCurrCapInfo |= WLAN_SET_CAP_INFO_SPECTRUMMNG(1);

	pTxPacket = s_MgrMakeReAssocRequest
		(
			pDevice,
			pMgmt,
			pMgmt->abyCurrBSSID,
			pMgmt->wCurrCapInfo,
			pMgmt->wListenInterval,
			(PWLAN_IE_SSID)pMgmt->abyCurrSSID,
			(PWLAN_IE_SUPP_RATES)pMgmt->abyCurrSuppRates,
			(PWLAN_IE_SUPP_RATES)pMgmt->abyCurrExtSuppRates
);

	if (pTxPacket != NULL) {
		/* send the frame */
		*pStatus = csMgmt_xmit(pDevice, pTxPacket);
		if (*pStatus != CMD_STATUS_PENDING) {
			DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Mgt:Reassociation tx failed.\n");
		} else {
			DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Mgt:Reassociation tx sending.\n");
		}
	}

	return;
}

/*+
 *
 * Routine Description:
 *    Send an dis-association request frame to the AP.
 *
 * Return Value:
 *    None.
 *
 -*/

void
vMgrDisassocBeginSta(
	void *hDeviceContext,
	PSMgmtObject pMgmt,
	unsigned char *abyDestAddress,
	unsigned short wReason,
	PCMD_STATUS pStatus
)
{
	PSDevice            pDevice = (PSDevice)hDeviceContext;
	PSTxMgmtPacket      pTxPacket = NULL;
	WLAN_FR_DISASSOC    sFrame;

	pTxPacket = (PSTxMgmtPacket)pMgmt->pbyMgmtPacketPool;
	memset(pTxPacket, 0, sizeof(STxMgmtPacket) + WLAN_DISASSOC_FR_MAXLEN);
	pTxPacket->p80211Header = (PUWLAN_80211HDR)((unsigned char *)pTxPacket + sizeof(STxMgmtPacket));

	// Setup the sFrame structure
	sFrame.pBuf = (unsigned char *)pTxPacket->p80211Header;
	sFrame.len = WLAN_DISASSOC_FR_MAXLEN;

	// format fixed field frame structure
	vMgrEncodeDisassociation(&sFrame);

	// Setup the header
	sFrame.pHdr->sA3.wFrameCtl = cpu_to_le16(
		(
			WLAN_SET_FC_FTYPE(WLAN_TYPE_MGR) |
			WLAN_SET_FC_FSTYPE(WLAN_FSTYPE_DISASSOC)
));

	memcpy(sFrame.pHdr->sA3.abyAddr1, abyDestAddress, WLAN_ADDR_LEN);
	memcpy(sFrame.pHdr->sA3.abyAddr2, pMgmt->abyMACAddr, WLAN_ADDR_LEN);
	memcpy(sFrame.pHdr->sA3.abyAddr3, pMgmt->abyCurrBSSID, WLAN_BSSID_LEN);

	// Set reason code
	*(sFrame.pwReason) = cpu_to_le16(wReason);
	pTxPacket->cbMPDULen = sFrame.len;
	pTxPacket->cbPayloadLen = sFrame.len - WLAN_HDR_ADDR3_LEN;

	// send the frame
	*pStatus = csMgmt_xmit(pDevice, pTxPacket);
	if (*pStatus == CMD_STATUS_PENDING) {
		pMgmt->eCurrState = WMAC_STATE_IDLE;
		*pStatus = CMD_STATUS_SUCCESS;
	}

	return;
}

/*+
 *
 * Routine Description:(AP function)
 *    Handle incoming station association request frames.
 *
 * Return Value:
 *    None.
 *
 -*/

static
void
s_vMgrRxAssocRequest(
	PSDevice pDevice,
	PSMgmtObject pMgmt,
	PSRxMgmtPacket pRxPacket,
	unsigned int uNodeIndex
)
{
	WLAN_FR_ASSOCREQ    sFrame;
	CMD_STATUS          Status;
	PSTxMgmtPacket      pTxPacket;
	unsigned short wAssocStatus = 0;
	unsigned short wAssocAID = 0;
	unsigned int uRateLen = WLAN_RATES_MAXLEN;
	unsigned char abyCurrSuppRates[WLAN_IEHDR_LEN + WLAN_RATES_MAXLEN + 1];
	unsigned char abyCurrExtSuppRates[WLAN_IEHDR_LEN + WLAN_RATES_MAXLEN + 1];

	if (pMgmt->eCurrMode != WMAC_MODE_ESS_AP)
		return;
	//  node index not found
	if (!uNodeIndex)
		return;

	//check if node is authenticated
	//decode the frame
	memset(&sFrame, 0, sizeof(WLAN_FR_ASSOCREQ));
	memset(abyCurrSuppRates, 0, WLAN_IEHDR_LEN + WLAN_RATES_MAXLEN + 1);
	memset(abyCurrExtSuppRates, 0, WLAN_IEHDR_LEN + WLAN_RATES_MAXLEN + 1);
	sFrame.len = pRxPacket->cbMPDULen;
	sFrame.pBuf = (unsigned char *)pRxPacket->p80211Header;

	vMgrDecodeAssocRequest(&sFrame);

	if (pMgmt->sNodeDBTable[uNodeIndex].eNodeState >= NODE_AUTH) {
		pMgmt->sNodeDBTable[uNodeIndex].eNodeState = NODE_ASSOC;
		pMgmt->sNodeDBTable[uNodeIndex].wCapInfo = cpu_to_le16(*sFrame.pwCapInfo);
		pMgmt->sNodeDBTable[uNodeIndex].wListenInterval = cpu_to_le16(*sFrame.pwListenInterval);
		pMgmt->sNodeDBTable[uNodeIndex].bPSEnable =
			WLAN_GET_FC_PWRMGT(sFrame.pHdr->sA3.wFrameCtl) ? true : false;
		// Todo: check sta basic rate, if ap can't support, set status code
		if (pDevice->eCurrentPHYType == PHY_TYPE_11B) {
			uRateLen = WLAN_RATES_MAXLEN_11B;
		}
		abyCurrSuppRates[0] = WLAN_EID_SUPP_RATES;
		abyCurrSuppRates[1] = RATEuSetIE((PWLAN_IE_SUPP_RATES)sFrame.pSuppRates,
						 (PWLAN_IE_SUPP_RATES)abyCurrSuppRates,
						 uRateLen);
		abyCurrExtSuppRates[0] = WLAN_EID_EXTSUPP_RATES;
		if (pDevice->eCurrentPHYType == PHY_TYPE_11G) {
			abyCurrExtSuppRates[1] = RATEuSetIE((PWLAN_IE_SUPP_RATES)sFrame.pExtSuppRates,
							    (PWLAN_IE_SUPP_RATES)abyCurrExtSuppRates,
							    uRateLen);
		} else {
			abyCurrExtSuppRates[1] = 0;
		}

		RATEvParseMaxRate((void *)pDevice,
				  (PWLAN_IE_SUPP_RATES)abyCurrSuppRates,
				  (PWLAN_IE_SUPP_RATES)abyCurrExtSuppRates,
				  false, // do not change our basic rate
				  &(pMgmt->sNodeDBTable[uNodeIndex].wMaxBasicRate),
				  &(pMgmt->sNodeDBTable[uNodeIndex].wMaxSuppRate),
				  &(pMgmt->sNodeDBTable[uNodeIndex].wSuppRate),
				  &(pMgmt->sNodeDBTable[uNodeIndex].byTopCCKBasicRate),
				  &(pMgmt->sNodeDBTable[uNodeIndex].byTopOFDMBasicRate)
);

		// set max tx rate
		pMgmt->sNodeDBTable[uNodeIndex].wTxDataRate =
			pMgmt->sNodeDBTable[uNodeIndex].wMaxSuppRate;
#ifdef	PLICE_DEBUG
		printk("RxAssocRequest:wTxDataRate is %d\n", pMgmt->sNodeDBTable[uNodeIndex].wTxDataRate);
#endif
		// Todo: check sta preamble, if ap can't support, set status code
		pMgmt->sNodeDBTable[uNodeIndex].bShortPreamble =
			WLAN_GET_CAP_INFO_SHORTPREAMBLE(*sFrame.pwCapInfo);
		pMgmt->sNodeDBTable[uNodeIndex].bShortSlotTime =
			WLAN_GET_CAP_INFO_SHORTSLOTTIME(*sFrame.pwCapInfo);
		pMgmt->sNodeDBTable[uNodeIndex].wAID = (unsigned short)uNodeIndex;
		wAssocStatus = WLAN_MGMT_STATUS_SUCCESS;
		wAssocAID = (unsigned short)uNodeIndex;
		// check if ERP support
		if (pMgmt->sNodeDBTable[uNodeIndex].wMaxSuppRate > RATE_11M)
			pMgmt->sNodeDBTable[uNodeIndex].bERPExist = true;

		if (pMgmt->sNodeDBTable[uNodeIndex].wMaxSuppRate <= RATE_11M) {
			// B only STA join
			pDevice->bProtectMode = true;
			pDevice->bNonERPPresent = true;
		}
		if (pMgmt->sNodeDBTable[uNodeIndex].bShortPreamble == false) {
			pDevice->bBarkerPreambleMd = true;
		}

		DBG_PRT(MSG_LEVEL_INFO, KERN_INFO "Associate AID= %d \n", wAssocAID);
		DBG_PRT(MSG_LEVEL_INFO, KERN_INFO "MAC=%2.2X:%2.2X:%2.2X:%2.2X:%2.2X:%2.2X \n",
			sFrame.pHdr->sA3.abyAddr2[0],
			sFrame.pHdr->sA3.abyAddr2[1],
			sFrame.pHdr->sA3.abyAddr2[2],
			sFrame.pHdr->sA3.abyAddr2[3],
			sFrame.pHdr->sA3.abyAddr2[4],
			sFrame.pHdr->sA3.abyAddr2[5]
			);
		DBG_PRT(MSG_LEVEL_INFO, KERN_INFO "Max Support rate = %d \n",
			pMgmt->sNodeDBTable[uNodeIndex].wMaxSuppRate);
	}//else { TODO: received STA under state1 handle }
	else {
		return;
	}

	// assoc response reply..
	pTxPacket = s_MgrMakeAssocResponse
		(
			pDevice,
			pMgmt,
			pMgmt->wCurrCapInfo,
			wAssocStatus,
			wAssocAID,
			sFrame.pHdr->sA3.abyAddr2,
			(PWLAN_IE_SUPP_RATES)pMgmt->abyCurrSuppRates,
			(PWLAN_IE_SUPP_RATES)pMgmt->abyCurrExtSuppRates
);
	if (pTxPacket != NULL) {
		if (pDevice->bEnableHostapd) {
			return;
		}
		/* send the frame */
		Status = csMgmt_xmit(pDevice, pTxPacket);
		if (Status != CMD_STATUS_PENDING) {
			DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Mgt:Assoc response tx failed\n");
		} else {
			DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Mgt:Assoc response tx sending..\n");
		}

	}

	return;
}

/*+
 *
 * Description:(AP function)
 *      Handle incoming station re-association request frames.
 *
 * Parameters:
 *  In:
 *      pMgmt           - Management Object structure
 *      pRxPacket       - Received Packet
 *  Out:
 *      none
 *
 * Return Value: None.
 *
 -*/

static
void
s_vMgrRxReAssocRequest(
	PSDevice pDevice,
	PSMgmtObject pMgmt,
	PSRxMgmtPacket pRxPacket,
	unsigned int uNodeIndex
)
{
	WLAN_FR_REASSOCREQ    sFrame;
	CMD_STATUS          Status;
	PSTxMgmtPacket      pTxPacket;
	unsigned short wAssocStatus = 0;
	unsigned short wAssocAID = 0;
	unsigned int	uRateLen = WLAN_RATES_MAXLEN;
	unsigned char abyCurrSuppRates[WLAN_IEHDR_LEN + WLAN_RATES_MAXLEN + 1];
	unsigned char abyCurrExtSuppRates[WLAN_IEHDR_LEN + WLAN_RATES_MAXLEN + 1];

	if (pMgmt->eCurrMode != WMAC_MODE_ESS_AP)
		return;
	//  node index not found
	if (!uNodeIndex)
		return;
	//check if node is authenticated
	//decode the frame
	memset(&sFrame, 0, sizeof(WLAN_FR_REASSOCREQ));
	sFrame.len = pRxPacket->cbMPDULen;
	sFrame.pBuf = (unsigned char *)pRxPacket->p80211Header;
	vMgrDecodeReassocRequest(&sFrame);

	if (pMgmt->sNodeDBTable[uNodeIndex].eNodeState >= NODE_AUTH) {
		pMgmt->sNodeDBTable[uNodeIndex].eNodeState = NODE_ASSOC;
		pMgmt->sNodeDBTable[uNodeIndex].wCapInfo = cpu_to_le16(*sFrame.pwCapInfo);
		pMgmt->sNodeDBTable[uNodeIndex].wListenInterval = cpu_to_le16(*sFrame.pwListenInterval);
		pMgmt->sNodeDBTable[uNodeIndex].bPSEnable =
			WLAN_GET_FC_PWRMGT(sFrame.pHdr->sA3.wFrameCtl) ? true : false;
		// Todo: check sta basic rate, if ap can't support, set status code

		if (pDevice->eCurrentPHYType == PHY_TYPE_11B) {
			uRateLen = WLAN_RATES_MAXLEN_11B;
		}

		abyCurrSuppRates[0] = WLAN_EID_SUPP_RATES;
		abyCurrSuppRates[1] = RATEuSetIE((PWLAN_IE_SUPP_RATES)sFrame.pSuppRates,
						 (PWLAN_IE_SUPP_RATES)abyCurrSuppRates,
						 uRateLen);
		abyCurrExtSuppRates[0] = WLAN_EID_EXTSUPP_RATES;
		if (pDevice->eCurrentPHYType == PHY_TYPE_11G) {
			abyCurrExtSuppRates[1] = RATEuSetIE((PWLAN_IE_SUPP_RATES)sFrame.pExtSuppRates,
							    (PWLAN_IE_SUPP_RATES)abyCurrExtSuppRates,
							    uRateLen);
		} else {
			abyCurrExtSuppRates[1] = 0;
		}

		RATEvParseMaxRate((void *)pDevice,
				  (PWLAN_IE_SUPP_RATES)abyCurrSuppRates,
				  (PWLAN_IE_SUPP_RATES)abyCurrExtSuppRates,
				  false, // do not change our basic rate
				  &(pMgmt->sNodeDBTable[uNodeIndex].wMaxBasicRate),
				  &(pMgmt->sNodeDBTable[uNodeIndex].wMaxSuppRate),
				  &(pMgmt->sNodeDBTable[uNodeIndex].wSuppRate),
				  &(pMgmt->sNodeDBTable[uNodeIndex].byTopCCKBasicRate),
				  &(pMgmt->sNodeDBTable[uNodeIndex].byTopOFDMBasicRate)
);

		// set max tx rate
		pMgmt->sNodeDBTable[uNodeIndex].wTxDataRate =
			pMgmt->sNodeDBTable[uNodeIndex].wMaxSuppRate;
#ifdef	PLICE_DEBUG
		printk("RxReAssocRequest:TxDataRate is %d\n", pMgmt->sNodeDBTable[uNodeIndex].wTxDataRate);
#endif
		// Todo: check sta preamble, if ap can't support, set status code
		pMgmt->sNodeDBTable[uNodeIndex].bShortPreamble =
			WLAN_GET_CAP_INFO_SHORTPREAMBLE(*sFrame.pwCapInfo);
		pMgmt->sNodeDBTable[uNodeIndex].bShortSlotTime =
			WLAN_GET_CAP_INFO_SHORTSLOTTIME(*sFrame.pwCapInfo);
		pMgmt->sNodeDBTable[uNodeIndex].wAID = (unsigned short)uNodeIndex;
		wAssocStatus = WLAN_MGMT_STATUS_SUCCESS;
		wAssocAID = (unsigned short)uNodeIndex;

		// if suppurt ERP
		if (pMgmt->sNodeDBTable[uNodeIndex].wMaxSuppRate > RATE_11M)
			pMgmt->sNodeDBTable[uNodeIndex].bERPExist = true;

		if (pMgmt->sNodeDBTable[uNodeIndex].wMaxSuppRate <= RATE_11M) {
			// B only STA join
			pDevice->bProtectMode = true;
			pDevice->bNonERPPresent = true;
		}
		if (pMgmt->sNodeDBTable[uNodeIndex].bShortPreamble == false) {
			pDevice->bBarkerPreambleMd = true;
		}

		DBG_PRT(MSG_LEVEL_INFO, KERN_INFO "Rx ReAssociate AID= %d \n", wAssocAID);
		DBG_PRT(MSG_LEVEL_INFO, KERN_INFO "MAC=%2.2X:%2.2X:%2.2X:%2.2X:%2.2X:%2.2X \n",
			sFrame.pHdr->sA3.abyAddr2[0],
			sFrame.pHdr->sA3.abyAddr2[1],
			sFrame.pHdr->sA3.abyAddr2[2],
			sFrame.pHdr->sA3.abyAddr2[3],
			sFrame.pHdr->sA3.abyAddr2[4],
			sFrame.pHdr->sA3.abyAddr2[5]
			);
		DBG_PRT(MSG_LEVEL_INFO, KERN_INFO "Max Support rate = %d \n",
			pMgmt->sNodeDBTable[uNodeIndex].wMaxSuppRate);

	}

	// assoc response reply..
	pTxPacket = s_MgrMakeReAssocResponse
		(
			pDevice,
			pMgmt,
			pMgmt->wCurrCapInfo,
			wAssocStatus,
			wAssocAID,
			sFrame.pHdr->sA3.abyAddr2,
			(PWLAN_IE_SUPP_RATES)pMgmt->abyCurrSuppRates,
			(PWLAN_IE_SUPP_RATES)pMgmt->abyCurrExtSuppRates
			);

	if (pTxPacket != NULL) {
		/* send the frame */
		if (pDevice->bEnableHostapd) {
			return;
		}
		Status = csMgmt_xmit(pDevice, pTxPacket);
		if (Status != CMD_STATUS_PENDING) {
			DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Mgt:ReAssoc response tx failed\n");
		} else {
			DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Mgt:ReAssoc response tx sending..\n");
		}
	}
	return;
}

/*+
 *
 * Routine Description:
 *    Handle incoming association response frames.
 *
 * Return Value:
 *    None.
 *
 -*/

static
void
s_vMgrRxAssocResponse(
	PSDevice pDevice,
	PSMgmtObject pMgmt,
	PSRxMgmtPacket pRxPacket,
	bool bReAssocType
)
{
	WLAN_FR_ASSOCRESP   sFrame;
	PWLAN_IE_SSID   pItemSSID;
	unsigned char *pbyIEs;
	viawget_wpa_header *wpahdr;

	if (pMgmt->eCurrState == WMAC_STATE_ASSOCPENDING ||
	    pMgmt->eCurrState == WMAC_STATE_ASSOC) {
		sFrame.len = pRxPacket->cbMPDULen;
		sFrame.pBuf = (unsigned char *)pRxPacket->p80211Header;
		// decode the frame
		vMgrDecodeAssocResponse(&sFrame);
		if ((sFrame.pwCapInfo == 0) ||
		    (sFrame.pwStatus == 0) ||
		    (sFrame.pwAid == 0) ||
		    (sFrame.pSuppRates == 0)) {
			DBG_PORT80(0xCC);
			return;
		}

		pMgmt->sAssocInfo.AssocInfo.ResponseFixedIEs.Capabilities = *(sFrame.pwCapInfo);
		pMgmt->sAssocInfo.AssocInfo.ResponseFixedIEs.StatusCode = *(sFrame.pwStatus);
		pMgmt->sAssocInfo.AssocInfo.ResponseFixedIEs.AssociationId = *(sFrame.pwAid);
		pMgmt->sAssocInfo.AssocInfo.AvailableResponseFixedIEs |= 0x07;

		pMgmt->sAssocInfo.AssocInfo.ResponseIELength = sFrame.len - 24 - 6;
		pMgmt->sAssocInfo.AssocInfo.OffsetResponseIEs = pMgmt->sAssocInfo.AssocInfo.OffsetRequestIEs + pMgmt->sAssocInfo.AssocInfo.RequestIELength;
		pbyIEs = pMgmt->sAssocInfo.abyIEs;
		pbyIEs += pMgmt->sAssocInfo.AssocInfo.RequestIELength;
		memcpy(pbyIEs, (sFrame.pBuf + 24 + 6), pMgmt->sAssocInfo.AssocInfo.ResponseIELength);

		// save values and set current BSS state
		if (cpu_to_le16((*(sFrame.pwStatus))) == WLAN_MGMT_STATUS_SUCCESS) {
			// set AID
			pMgmt->wCurrAID = cpu_to_le16((*(sFrame.pwAid)));
			if ((pMgmt->wCurrAID >> 14) != (BIT0 | BIT1)) {
				DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "AID from AP, has two msb clear.\n");
			}
			DBG_PRT(MSG_LEVEL_INFO, KERN_INFO "Association Successful, AID=%d.\n", pMgmt->wCurrAID & ~(BIT14 | BIT15));
			pMgmt->eCurrState = WMAC_STATE_ASSOC;
			BSSvUpdateAPNode((void *)pDevice, sFrame.pwCapInfo, sFrame.pSuppRates, sFrame.pExtSuppRates);
			pItemSSID = (PWLAN_IE_SSID)pMgmt->abyCurrSSID;
			DBG_PRT(MSG_LEVEL_INFO, KERN_INFO "Link with AP(SSID): %s\n", pItemSSID->abySSID);
			pDevice->bLinkPass = true;
			pDevice->uBBVGADiffCount = 0;
			if ((pDevice->bWPADEVUp) && (pDevice->skb != NULL)) {
				if (skb_tailroom(pDevice->skb) < (sizeof(viawget_wpa_header) + pMgmt->sAssocInfo.AssocInfo.ResponseIELength +
								  pMgmt->sAssocInfo.AssocInfo.RequestIELength)) {    //data room not enough
					dev_kfree_skb(pDevice->skb);
					pDevice->skb = dev_alloc_skb((int)pDevice->rx_buf_sz);
				}
				wpahdr = (viawget_wpa_header *)pDevice->skb->data;
				wpahdr->type = VIAWGET_ASSOC_MSG;
				wpahdr->resp_ie_len = pMgmt->sAssocInfo.AssocInfo.ResponseIELength;
				wpahdr->req_ie_len = pMgmt->sAssocInfo.AssocInfo.RequestIELength;
				memcpy(pDevice->skb->data + sizeof(viawget_wpa_header), pMgmt->sAssocInfo.abyIEs, wpahdr->req_ie_len);
				memcpy(pDevice->skb->data + sizeof(viawget_wpa_header) + wpahdr->req_ie_len,
				       pbyIEs,
				       wpahdr->resp_ie_len
);
				skb_put(pDevice->skb, sizeof(viawget_wpa_header) + wpahdr->resp_ie_len + wpahdr->req_ie_len);
				pDevice->skb->dev = pDevice->wpadev;
				skb_reset_mac_header(pDevice->skb);
				pDevice->skb->pkt_type = PACKET_HOST;
				pDevice->skb->protocol = htons(ETH_P_802_2);
				memset(pDevice->skb->cb, 0, sizeof(pDevice->skb->cb));
				netif_rx(pDevice->skb);
				pDevice->skb = dev_alloc_skb((int)pDevice->rx_buf_sz);
			}

//2008-0409-07, <Add> by Einsn Liu
#ifdef WPA_SUPPLICANT_DRIVER_WEXT_SUPPORT
			{
				unsigned char buf[512];
				size_t len;
				union iwreq_data  wrqu;
				int we_event;

				memset(buf, 0, 512);

				len = pMgmt->sAssocInfo.AssocInfo.RequestIELength;
				if (len)	{
					memcpy(buf, pMgmt->sAssocInfo.abyIEs, len);
					memset(&wrqu, 0, sizeof(wrqu));
					wrqu.data.length = len;
					we_event = IWEVASSOCREQIE;
					wireless_send_event(pDevice->dev, we_event, &wrqu, buf);
				}

				memset(buf, 0, 512);
				len = pMgmt->sAssocInfo.AssocInfo.ResponseIELength;

				if (len)	{
					memcpy(buf, pbyIEs, len);
					memset(&wrqu, 0, sizeof(wrqu));
					wrqu.data.length = len;
					we_event = IWEVASSOCRESPIE;
					wireless_send_event(pDevice->dev, we_event, &wrqu, buf);
				}

				memset(&wrqu, 0, sizeof(wrqu));
				memcpy(wrqu.ap_addr.sa_data, &pMgmt->abyCurrBSSID[0], ETH_ALEN);
				wrqu.ap_addr.sa_family = ARPHRD_ETHER;
				wireless_send_event(pDevice->dev, SIOCGIWAP, &wrqu, NULL);
			}
#endif //#ifdef WPA_SUPPLICANT_DRIVER_WEXT_SUPPORT
//End Add -- //2008-0409-07, <Add> by Einsn Liu
		} else {
			if (bReAssocType) {
				pMgmt->eCurrState = WMAC_STATE_IDLE;
			} else {
				// jump back to the auth state and indicate the error
				pMgmt->eCurrState = WMAC_STATE_AUTH;
			}
			s_vMgrLogStatus(pMgmt, cpu_to_le16((*(sFrame.pwStatus))));
		}

	}

#ifdef WPA_SUPPLICANT_DRIVER_WEXT_SUPPORT
//need clear flags related to Networkmanager

	pDevice->bwextcount = 0;
	pDevice->bWPASuppWextEnabled = false;
#endif

	if (pMgmt->eCurrState == WMAC_STATE_ASSOC)
		timer_expire(pDevice->sTimerCommand, 0);
	return;
}

/*+
 *
 * Routine Description:
 *    Start the station authentication procedure.  Namely, send an
 *    authentication frame to the AP.
 *
 * Return Value:
 *    None.
 *
 -*/

void
vMgrAuthenBeginSta(
	void *hDeviceContext,
	PSMgmtObject  pMgmt,
	PCMD_STATUS pStatus
)
{
	PSDevice     pDevice = (PSDevice)hDeviceContext;
	WLAN_FR_AUTHEN  sFrame;
	PSTxMgmtPacket  pTxPacket = NULL;

	pTxPacket = (PSTxMgmtPacket)pMgmt->pbyMgmtPacketPool;
	memset(pTxPacket, 0, sizeof(STxMgmtPacket) + WLAN_AUTHEN_FR_MAXLEN);
	pTxPacket->p80211Header = (PUWLAN_80211HDR)((unsigned char *)pTxPacket + sizeof(STxMgmtPacket));
	sFrame.pBuf = (unsigned char *)pTxPacket->p80211Header;
	sFrame.len = WLAN_AUTHEN_FR_MAXLEN;
	vMgrEncodeAuthen(&sFrame);
	/* insert values */
	sFrame.pHdr->sA3.wFrameCtl = cpu_to_le16(
		(
			WLAN_SET_FC_FTYPE(WLAN_TYPE_MGR) |
			WLAN_SET_FC_FSTYPE(WLAN_FSTYPE_AUTHEN)
));
	memcpy(sFrame.pHdr->sA3.abyAddr1, pMgmt->abyCurrBSSID, WLAN_ADDR_LEN);
	memcpy(sFrame.pHdr->sA3.abyAddr2, pMgmt->abyMACAddr, WLAN_ADDR_LEN);
	memcpy(sFrame.pHdr->sA3.abyAddr3, pMgmt->abyCurrBSSID, WLAN_BSSID_LEN);
	if (pMgmt->bShareKeyAlgorithm)
		*(sFrame.pwAuthAlgorithm) = cpu_to_le16(WLAN_AUTH_ALG_SHAREDKEY);
	else
		*(sFrame.pwAuthAlgorithm) = cpu_to_le16(WLAN_AUTH_ALG_OPENSYSTEM);

	*(sFrame.pwAuthSequence) = cpu_to_le16(1);
	/* Adjust the length fields */
	pTxPacket->cbMPDULen = sFrame.len;
	pTxPacket->cbPayloadLen = sFrame.len - WLAN_HDR_ADDR3_LEN;

	*pStatus = csMgmt_xmit(pDevice, pTxPacket);
	if (*pStatus == CMD_STATUS_PENDING) {
		pMgmt->eCurrState = WMAC_STATE_AUTHPENDING;
		*pStatus = CMD_STATUS_SUCCESS;
	}

	return;
}

/*+
 *
 * Routine Description:
 *    Start the station(AP) deauthentication procedure.  Namely, send an
 *    deauthentication frame to the AP or Sta.
 *
 * Return Value:
 *    None.
 *
 -*/

void
vMgrDeAuthenBeginSta(
	void *hDeviceContext,
	PSMgmtObject  pMgmt,
	unsigned char *abyDestAddress,
	unsigned short wReason,
	PCMD_STATUS pStatus
)
{
	PSDevice            pDevice = (PSDevice)hDeviceContext;
	WLAN_FR_DEAUTHEN    sFrame;
	PSTxMgmtPacket      pTxPacket = NULL;

	pTxPacket = (PSTxMgmtPacket)pMgmt->pbyMgmtPacketPool;
	memset(pTxPacket, 0, sizeof(STxMgmtPacket) + WLAN_DEAUTHEN_FR_MAXLEN);
	pTxPacket->p80211Header = (PUWLAN_80211HDR)((unsigned char *)pTxPacket + sizeof(STxMgmtPacket));
	sFrame.pBuf = (unsigned char *)pTxPacket->p80211Header;
	sFrame.len = WLAN_DEAUTHEN_FR_MAXLEN;
	vMgrEncodeDeauthen(&sFrame);
	/* insert values */
	sFrame.pHdr->sA3.wFrameCtl = cpu_to_le16(
		(
			WLAN_SET_FC_FTYPE(WLAN_TYPE_MGR) |
			WLAN_SET_FC_FSTYPE(WLAN_FSTYPE_DEAUTHEN)
));

	memcpy(sFrame.pHdr->sA3.abyAddr1, abyDestAddress, WLAN_ADDR_LEN);
	memcpy(sFrame.pHdr->sA3.abyAddr2, pMgmt->abyMACAddr, WLAN_ADDR_LEN);
	memcpy(sFrame.pHdr->sA3.abyAddr3, pMgmt->abyCurrBSSID, WLAN_BSSID_LEN);

	*(sFrame.pwReason) = cpu_to_le16(wReason);       // deauthen. bcs left BSS
	/* Adjust the length fields */
	pTxPacket->cbMPDULen = sFrame.len;
	pTxPacket->cbPayloadLen = sFrame.len - WLAN_HDR_ADDR3_LEN;

	*pStatus = csMgmt_xmit(pDevice, pTxPacket);
	if (*pStatus == CMD_STATUS_PENDING) {
		*pStatus = CMD_STATUS_SUCCESS;
	}

	return;
}

/*+
 *
 * Routine Description:
 *    Handle incoming authentication frames.
 *
 * Return Value:
 *    None.
 *
 -*/

static
void
s_vMgrRxAuthentication(
	PSDevice pDevice,
	PSMgmtObject pMgmt,
	PSRxMgmtPacket pRxPacket
)
{
	WLAN_FR_AUTHEN  sFrame;

	// we better be an AP or a STA in AUTHPENDING otherwise ignore
	if (!(pMgmt->eCurrMode == WMAC_MODE_ESS_AP ||
	      pMgmt->eCurrState == WMAC_STATE_AUTHPENDING)) {
		return;
	}

	// decode the frame
	sFrame.len = pRxPacket->cbMPDULen;
	sFrame.pBuf = (unsigned char *)pRxPacket->p80211Header;
	vMgrDecodeAuthen(&sFrame);
	switch (cpu_to_le16((*(sFrame.pwAuthSequence)))) {
	case 1:
		//AP function
		s_vMgrRxAuthenSequence_1(pDevice, pMgmt, &sFrame);
		break;
	case 2:
		s_vMgrRxAuthenSequence_2(pDevice, pMgmt, &sFrame);
		break;
	case 3:
		//AP function
		s_vMgrRxAuthenSequence_3(pDevice, pMgmt, &sFrame);
		break;
	case 4:
		s_vMgrRxAuthenSequence_4(pDevice, pMgmt, &sFrame);
		break;
	default:
		DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Auth Sequence error, seq = %d\n",
			cpu_to_le16((*(sFrame.pwAuthSequence))));
		break;
	}
	return;
}

/*+
 *
 * Routine Description:
 *   Handles incoming authen frames with sequence 1.  Currently
 *   assumes we're an AP.  So far, no one appears to use authentication
 *   in Ad-Hoc mode.
 *
 * Return Value:
 *    None.
 *
 -*/

static
void
s_vMgrRxAuthenSequence_1(
	PSDevice pDevice,
	PSMgmtObject pMgmt,
	PWLAN_FR_AUTHEN pFrame
)
{
	PSTxMgmtPacket      pTxPacket = NULL;
	unsigned int	uNodeIndex;
	WLAN_FR_AUTHEN      sFrame;
	PSKeyItem           pTransmitKey;

	// Insert a Node entry
	if (!BSSDBbIsSTAInNodeDB(pMgmt, pFrame->pHdr->sA3.abyAddr2, &uNodeIndex)) {
		BSSvCreateOneNode((PSDevice)pDevice, &uNodeIndex);
		memcpy(pMgmt->sNodeDBTable[uNodeIndex].abyMACAddr, pFrame->pHdr->sA3.abyAddr2,
		       WLAN_ADDR_LEN);
	}

	if (pMgmt->bShareKeyAlgorithm) {
		pMgmt->sNodeDBTable[uNodeIndex].eNodeState = NODE_KNOWN;
		pMgmt->sNodeDBTable[uNodeIndex].byAuthSequence = 1;
	} else {
		pMgmt->sNodeDBTable[uNodeIndex].eNodeState = NODE_AUTH;
	}

	// send auth reply
	pTxPacket = (PSTxMgmtPacket)pMgmt->pbyMgmtPacketPool;
	memset(pTxPacket, 0, sizeof(STxMgmtPacket) + WLAN_AUTHEN_FR_MAXLEN);
	pTxPacket->p80211Header = (PUWLAN_80211HDR)((unsigned char *)pTxPacket + sizeof(STxMgmtPacket));
	sFrame.pBuf = (unsigned char *)pTxPacket->p80211Header;
	sFrame.len = WLAN_AUTHEN_FR_MAXLEN;
	// format buffer structure
	vMgrEncodeAuthen(&sFrame);
	// insert values
	sFrame.pHdr->sA3.wFrameCtl = cpu_to_le16(
		(
			WLAN_SET_FC_FTYPE(WLAN_TYPE_MGR) |
			WLAN_SET_FC_FSTYPE(WLAN_FSTYPE_AUTHEN)|
			WLAN_SET_FC_ISWEP(0)
));
	memcpy(sFrame.pHdr->sA3.abyAddr1, pFrame->pHdr->sA3.abyAddr2, WLAN_ADDR_LEN);
	memcpy(sFrame.pHdr->sA3.abyAddr2, pMgmt->abyMACAddr, WLAN_ADDR_LEN);
	memcpy(sFrame.pHdr->sA3.abyAddr3, pMgmt->abyCurrBSSID, WLAN_BSSID_LEN);
	*(sFrame.pwAuthAlgorithm) = *(pFrame->pwAuthAlgorithm);
	*(sFrame.pwAuthSequence) = cpu_to_le16(2);

	if (cpu_to_le16(*(pFrame->pwAuthAlgorithm)) == WLAN_AUTH_ALG_SHAREDKEY) {
		if (pMgmt->bShareKeyAlgorithm)
			*(sFrame.pwStatus) = cpu_to_le16(WLAN_MGMT_STATUS_SUCCESS);
		else
			*(sFrame.pwStatus) = cpu_to_le16(WLAN_MGMT_STATUS_UNSUPPORTED_AUTHALG);
	} else {
		if (pMgmt->bShareKeyAlgorithm)
			*(sFrame.pwStatus) = cpu_to_le16(WLAN_MGMT_STATUS_UNSUPPORTED_AUTHALG);
		else
			*(sFrame.pwStatus) = cpu_to_le16(WLAN_MGMT_STATUS_SUCCESS);
	}

	if (pMgmt->bShareKeyAlgorithm &&
	    (cpu_to_le16(*(sFrame.pwStatus)) == WLAN_MGMT_STATUS_SUCCESS)) {
		sFrame.pChallenge = (PWLAN_IE_CHALLENGE)(sFrame.pBuf + sFrame.len);
		sFrame.len += WLAN_CHALLENGE_IE_LEN;
		sFrame.pChallenge->byElementID = WLAN_EID_CHALLENGE;
		sFrame.pChallenge->len = WLAN_CHALLENGE_LEN;
		memset(pMgmt->abyChallenge, 0, WLAN_CHALLENGE_LEN);
		// get group key
		if (KeybGetTransmitKey(&(pDevice->sKey), pDevice->abyBroadcastAddr, GROUP_KEY, &pTransmitKey) == true) {
			rc4_init(&pDevice->SBox, pDevice->abyPRNG, pTransmitKey->uKeyLength+3);
			rc4_encrypt(&pDevice->SBox, pMgmt->abyChallenge, pMgmt->abyChallenge, WLAN_CHALLENGE_LEN);
		}
		memcpy(sFrame.pChallenge->abyChallenge, pMgmt->abyChallenge , WLAN_CHALLENGE_LEN);
	}

	/* Adjust the length fields */
	pTxPacket->cbMPDULen = sFrame.len;
	pTxPacket->cbPayloadLen = sFrame.len - WLAN_HDR_ADDR3_LEN;
	// send the frame
	if (pDevice->bEnableHostapd) {
		return;
	}
	DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Mgt:Authreq_reply sequence_1 tx.. \n");
	if (csMgmt_xmit(pDevice, pTxPacket) != CMD_STATUS_PENDING) {
		DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Mgt:Authreq_reply sequence_1 tx failed.\n");
	}
	return;
}

/*+
 *
 * Routine Description:
 *   Handles incoming auth frames with sequence number 2.  Currently
 *   assumes we're a station.
 *
 *
 * Return Value:
 *    None.
 *
 -*/

static
void
s_vMgrRxAuthenSequence_2(
	PSDevice pDevice,
	PSMgmtObject pMgmt,
	PWLAN_FR_AUTHEN pFrame
)
{
	WLAN_FR_AUTHEN      sFrame;
	PSTxMgmtPacket      pTxPacket = NULL;

	switch (cpu_to_le16((*(pFrame->pwAuthAlgorithm)))) {
	case WLAN_AUTH_ALG_OPENSYSTEM:
		if (cpu_to_le16((*(pFrame->pwStatus))) == WLAN_MGMT_STATUS_SUCCESS) {
			DBG_PRT(MSG_LEVEL_INFO, KERN_INFO "802.11 Authen (OPEN) Successful.\n");
			pMgmt->eCurrState = WMAC_STATE_AUTH;
			timer_expire(pDevice->sTimerCommand, 0);
		} else {
			DBG_PRT(MSG_LEVEL_INFO, KERN_INFO "802.11 Authen (OPEN) Failed.\n");
			s_vMgrLogStatus(pMgmt, cpu_to_le16((*(pFrame->pwStatus))));
			pMgmt->eCurrState = WMAC_STATE_IDLE;
		}
		if (pDevice->eCommandState == WLAN_AUTHENTICATE_WAIT) {
//                spin_unlock_irq(&pDevice->lock);
//                vCommandTimerWait((void *)pDevice, 0);
//                spin_lock_irq(&pDevice->lock);
		}

		break;

	case WLAN_AUTH_ALG_SHAREDKEY:

		if (cpu_to_le16((*(pFrame->pwStatus))) == WLAN_MGMT_STATUS_SUCCESS) {
			pTxPacket = (PSTxMgmtPacket)pMgmt->pbyMgmtPacketPool;
			memset(pTxPacket, 0, sizeof(STxMgmtPacket) + WLAN_AUTHEN_FR_MAXLEN);
			pTxPacket->p80211Header = (PUWLAN_80211HDR)((unsigned char *)pTxPacket + sizeof(STxMgmtPacket));
			sFrame.pBuf = (unsigned char *)pTxPacket->p80211Header;
			sFrame.len = WLAN_AUTHEN_FR_MAXLEN;
			// format buffer structure
			vMgrEncodeAuthen(&sFrame);
			// insert values
			sFrame.pHdr->sA3.wFrameCtl = cpu_to_le16(
				(
					WLAN_SET_FC_FTYPE(WLAN_TYPE_MGR) |
					WLAN_SET_FC_FSTYPE(WLAN_FSTYPE_AUTHEN)|
					WLAN_SET_FC_ISWEP(1)
));
			memcpy(sFrame.pHdr->sA3.abyAddr1, pMgmt->abyCurrBSSID, WLAN_BSSID_LEN);
			memcpy(sFrame.pHdr->sA3.abyAddr2, pMgmt->abyMACAddr, WLAN_ADDR_LEN);
			memcpy(sFrame.pHdr->sA3.abyAddr3, pMgmt->abyCurrBSSID, WLAN_BSSID_LEN);
			*(sFrame.pwAuthAlgorithm) = *(pFrame->pwAuthAlgorithm);
			*(sFrame.pwAuthSequence) = cpu_to_le16(3);
			*(sFrame.pwStatus) = cpu_to_le16(WLAN_MGMT_STATUS_SUCCESS);
			sFrame.pChallenge = (PWLAN_IE_CHALLENGE)(sFrame.pBuf + sFrame.len);
			sFrame.len += WLAN_CHALLENGE_IE_LEN;
			sFrame.pChallenge->byElementID = WLAN_EID_CHALLENGE;
			sFrame.pChallenge->len = WLAN_CHALLENGE_LEN;
			memcpy(sFrame.pChallenge->abyChallenge, pFrame->pChallenge->abyChallenge, WLAN_CHALLENGE_LEN);
			// Adjust the length fields
			pTxPacket->cbMPDULen = sFrame.len;
			pTxPacket->cbPayloadLen = sFrame.len - WLAN_HDR_ADDR3_LEN;
			// send the frame
			if (csMgmt_xmit(pDevice, pTxPacket) != CMD_STATUS_PENDING) {
				DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Mgt:Auth_reply sequence_2 tx failed.\n");
			}
			DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Mgt:Auth_reply sequence_2 tx ...\n");
		} else {
			DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Mgt:rx Auth_reply sequence_2 status error ...\n");
			if (pDevice->eCommandState == WLAN_AUTHENTICATE_WAIT) {
//                    spin_unlock_irq(&pDevice->lock);
//                    vCommandTimerWait((void *)pDevice, 0);
//                    spin_lock_irq(&pDevice->lock);
			}
			s_vMgrLogStatus(pMgmt, cpu_to_le16((*(pFrame->pwStatus))));
		}
		break;
	default:
		DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Mgt: rx auth.seq = 2 unknown AuthAlgorithm=%d\n", cpu_to_le16((*(pFrame->pwAuthAlgorithm))));
		break;
	}
	return;
}

/*+
 *
 * Routine Description:
 *   Handles incoming authen frames with sequence 3.  Currently
 *   assumes we're an AP.  This function assumes the frame has
 *   already been successfully decrypted.
 *
 *
 * Return Value:
 *    None.
 *
 -*/

static
void
s_vMgrRxAuthenSequence_3(
	PSDevice pDevice,
	PSMgmtObject pMgmt,
	PWLAN_FR_AUTHEN pFrame
)
{
	PSTxMgmtPacket      pTxPacket = NULL;
	unsigned int uStatusCode = 0;
	unsigned int uNodeIndex = 0;
	WLAN_FR_AUTHEN      sFrame;

	if (!WLAN_GET_FC_ISWEP(pFrame->pHdr->sA3.wFrameCtl)) {
		uStatusCode = WLAN_MGMT_STATUS_CHALLENGE_FAIL;
		goto reply;
	}
	if (BSSDBbIsSTAInNodeDB(pMgmt, pFrame->pHdr->sA3.abyAddr2, &uNodeIndex)) {
		if (pMgmt->sNodeDBTable[uNodeIndex].byAuthSequence != 1) {
			uStatusCode = WLAN_MGMT_STATUS_RX_AUTH_NOSEQ;
			goto reply;
		}
		if (memcmp(pMgmt->abyChallenge, pFrame->pChallenge->abyChallenge, WLAN_CHALLENGE_LEN) != 0) {
			uStatusCode = WLAN_MGMT_STATUS_CHALLENGE_FAIL;
			goto reply;
		}
	} else {
		uStatusCode = WLAN_MGMT_STATUS_UNSPEC_FAILURE;
		goto reply;
	}

	if (uNodeIndex) {
		pMgmt->sNodeDBTable[uNodeIndex].eNodeState = NODE_AUTH;
		pMgmt->sNodeDBTable[uNodeIndex].byAuthSequence = 0;
	}
	uStatusCode = WLAN_MGMT_STATUS_SUCCESS;
	DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Challenge text check ok..\n");

reply:
	// send auth reply
	pTxPacket = (PSTxMgmtPacket)pMgmt->pbyMgmtPacketPool;
	memset(pTxPacket, 0, sizeof(STxMgmtPacket) + WLAN_AUTHEN_FR_MAXLEN);
	pTxPacket->p80211Header = (PUWLAN_80211HDR)((unsigned char *)pTxPacket + sizeof(STxMgmtPacket));
	sFrame.pBuf = (unsigned char *)pTxPacket->p80211Header;
	sFrame.len = WLAN_AUTHEN_FR_MAXLEN;
	// format buffer structure
	vMgrEncodeAuthen(&sFrame);
	/* insert values */
	sFrame.pHdr->sA3.wFrameCtl = cpu_to_le16(
		(
			WLAN_SET_FC_FTYPE(WLAN_TYPE_MGR) |
			WLAN_SET_FC_FSTYPE(WLAN_FSTYPE_AUTHEN)|
			WLAN_SET_FC_ISWEP(0)
));
	memcpy(sFrame.pHdr->sA3.abyAddr1, pFrame->pHdr->sA3.abyAddr2, WLAN_ADDR_LEN);
	memcpy(sFrame.pHdr->sA3.abyAddr2, pMgmt->abyMACAddr, WLAN_ADDR_LEN);
	memcpy(sFrame.pHdr->sA3.abyAddr3, pMgmt->abyCurrBSSID, WLAN_BSSID_LEN);
	*(sFrame.pwAuthAlgorithm) = *(pFrame->pwAuthAlgorithm);
	*(sFrame.pwAuthSequence) = cpu_to_le16(4);
	*(sFrame.pwStatus) = cpu_to_le16(uStatusCode);

	/* Adjust the length fields */
	pTxPacket->cbMPDULen = sFrame.len;
	pTxPacket->cbPayloadLen = sFrame.len - WLAN_HDR_ADDR3_LEN;
	// send the frame
	if (pDevice->bEnableHostapd) {
		return;
	}
	if (csMgmt_xmit(pDevice, pTxPacket) != CMD_STATUS_PENDING) {
		DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Mgt:Authreq_reply sequence_4 tx failed.\n");
	}
	return;
}

/*+
 *
 * Routine Description:
 *   Handles incoming authen frames with sequence 4
 *
 *
 * Return Value:
 *    None.
 *
 -*/
static
void
s_vMgrRxAuthenSequence_4(
	PSDevice pDevice,
	PSMgmtObject pMgmt,
	PWLAN_FR_AUTHEN pFrame
)
{
	if (cpu_to_le16((*(pFrame->pwStatus))) == WLAN_MGMT_STATUS_SUCCESS) {
		DBG_PRT(MSG_LEVEL_INFO, KERN_INFO "802.11 Authen (SHAREDKEY) Successful.\n");
		pMgmt->eCurrState = WMAC_STATE_AUTH;
		timer_expire(pDevice->sTimerCommand, 0);
	} else{
		DBG_PRT(MSG_LEVEL_INFO, KERN_INFO "802.11 Authen (SHAREDKEY) Failed.\n");
		s_vMgrLogStatus(pMgmt, cpu_to_le16((*(pFrame->pwStatus))));
		pMgmt->eCurrState = WMAC_STATE_IDLE;
	}

	if (pDevice->eCommandState == WLAN_AUTHENTICATE_WAIT) {
//        spin_unlock_irq(&pDevice->lock);
//        vCommandTimerWait((void *)pDevice, 0);
//        spin_lock_irq(&pDevice->lock);
	}
}

/*+
 *
 * Routine Description:
 *   Handles incoming disassociation frames
 *
 *
 * Return Value:
 *    None.
 *
 -*/

static
void
s_vMgrRxDisassociation(
	PSDevice pDevice,
	PSMgmtObject pMgmt,
	PSRxMgmtPacket pRxPacket
)
{
	WLAN_FR_DISASSOC    sFrame;
	unsigned int uNodeIndex = 0;
//    CMD_STATUS          CmdStatus;
	viawget_wpa_header *wpahdr;

	if (pMgmt->eCurrMode == WMAC_MODE_ESS_AP) {
		// if is acting an AP..
		// a STA is leaving this BSS..
		sFrame.len = pRxPacket->cbMPDULen;
		sFrame.pBuf = (unsigned char *)pRxPacket->p80211Header;
		if (BSSDBbIsSTAInNodeDB(pMgmt, pRxPacket->p80211Header->sA3.abyAddr2, &uNodeIndex)) {
			BSSvRemoveOneNode(pDevice, uNodeIndex);
		} else {
			DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Rx disassoc, sta not found\n");
		}
	} else if (pMgmt->eCurrMode == WMAC_MODE_ESS_STA) {
		sFrame.len = pRxPacket->cbMPDULen;
		sFrame.pBuf = (unsigned char *)pRxPacket->p80211Header;
		vMgrDecodeDisassociation(&sFrame);
		DBG_PRT(MSG_LEVEL_NOTICE, KERN_INFO "AP disassociated me, reason=%d.\n", cpu_to_le16(*(sFrame.pwReason)));
		//TODO: do something let upper layer know or
		//try to send associate packet again because of inactivity timeout
		if ((pDevice->bWPADEVUp) && (pDevice->skb != NULL)) {
			wpahdr = (viawget_wpa_header *)pDevice->skb->data;
			wpahdr->type = VIAWGET_DISASSOC_MSG;
			wpahdr->resp_ie_len = 0;
			wpahdr->req_ie_len = 0;
			skb_put(pDevice->skb, sizeof(viawget_wpa_header));
			pDevice->skb->dev = pDevice->wpadev;
			skb_reset_mac_header(pDevice->skb);

			pDevice->skb->pkt_type = PACKET_HOST;
			pDevice->skb->protocol = htons(ETH_P_802_2);
			memset(pDevice->skb->cb, 0, sizeof(pDevice->skb->cb));
			netif_rx(pDevice->skb);
			pDevice->skb = dev_alloc_skb((int)pDevice->rx_buf_sz);
		}

#ifdef WPA_SUPPLICANT_DRIVER_WEXT_SUPPORT
		{
			union iwreq_data  wrqu;
			memset(&wrqu, 0, sizeof(wrqu));
			wrqu.ap_addr.sa_family = ARPHRD_ETHER;
			printk("wireless_send_event--->SIOCGIWAP(disassociated)\n");
			wireless_send_event(pDevice->dev, SIOCGIWAP, &wrqu, NULL);
		}
#endif
	}
	/* else, ignore it */

	return;
}

/*+
 *
 * Routine Description:
 *   Handles incoming deauthentication frames
 *
 *
 * Return Value:
 *    None.
 *
 -*/

static
void
s_vMgrRxDeauthentication(
	PSDevice pDevice,
	PSMgmtObject pMgmt,
	PSRxMgmtPacket pRxPacket
)
{
	WLAN_FR_DEAUTHEN    sFrame;
	unsigned int uNodeIndex = 0;
	viawget_wpa_header *wpahdr;

	if (pMgmt->eCurrMode == WMAC_MODE_ESS_AP) {
		//Todo:
		// if is acting an AP..
		// a STA is leaving this BSS..
		sFrame.len = pRxPacket->cbMPDULen;
		sFrame.pBuf = (unsigned char *)pRxPacket->p80211Header;
		if (BSSDBbIsSTAInNodeDB(pMgmt, pRxPacket->p80211Header->sA3.abyAddr2, &uNodeIndex)) {
			BSSvRemoveOneNode(pDevice, uNodeIndex);
		} else {
			DBG_PRT(MSG_LEVEL_NOTICE, KERN_INFO "Rx deauth, sta not found\n");
		}
	} else {
		if (pMgmt->eCurrMode == WMAC_MODE_ESS_STA) {
			sFrame.len = pRxPacket->cbMPDULen;
			sFrame.pBuf = (unsigned char *)pRxPacket->p80211Header;
			vMgrDecodeDeauthen(&sFrame);
			DBG_PRT(MSG_LEVEL_NOTICE, KERN_INFO  "AP deauthed me, reason=%d.\n", cpu_to_le16((*(sFrame.pwReason))));
			// TODO: update BSS list for specific BSSID if pre-authentication case
			if (ether_addr_equal(sFrame.pHdr->sA3.abyAddr3,
					     pMgmt->abyCurrBSSID)) {
				if (pMgmt->eCurrState >= WMAC_STATE_AUTHPENDING) {
					pMgmt->sNodeDBTable[0].bActive = false;
					pMgmt->eCurrMode = WMAC_MODE_STANDBY;
					pMgmt->eCurrState = WMAC_STATE_IDLE;
					netif_stop_queue(pDevice->dev);
					pDevice->bLinkPass = false;
				}
			}

			if ((pDevice->bWPADEVUp) && (pDevice->skb != NULL)) {
				wpahdr = (viawget_wpa_header *)pDevice->skb->data;
				wpahdr->type = VIAWGET_DISASSOC_MSG;
				wpahdr->resp_ie_len = 0;
				wpahdr->req_ie_len = 0;
				skb_put(pDevice->skb, sizeof(viawget_wpa_header));
				pDevice->skb->dev = pDevice->wpadev;
				skb_reset_mac_header(pDevice->skb);
				pDevice->skb->pkt_type = PACKET_HOST;
				pDevice->skb->protocol = htons(ETH_P_802_2);
				memset(pDevice->skb->cb, 0, sizeof(pDevice->skb->cb));
				netif_rx(pDevice->skb);
				pDevice->skb = dev_alloc_skb((int)pDevice->rx_buf_sz);
			}

#ifdef WPA_SUPPLICANT_DRIVER_WEXT_SUPPORT
			{
				union iwreq_data  wrqu;
				memset(&wrqu, 0, sizeof(wrqu));
				wrqu.ap_addr.sa_family = ARPHRD_ETHER;
				PRINT_K("wireless_send_event--->SIOCGIWAP(disauthen)\n");
				wireless_send_event(pDevice->dev, SIOCGIWAP, &wrqu, NULL);
			}
#endif

		}
		/* else, ignore it.  TODO: IBSS authentication service
		   would be implemented here */
	};
	return;
}

//2008-8-4 <add> by chester
/*+
 *
 * Routine Description:
 * check if current channel is match ZoneType.
 *for USA:1~11;
 *      Japan:1~13;
 *      Europe:1~13
 * Return Value:
 *               True:exceed;
 *                False:normal case
 -*/
static bool
ChannelExceedZoneType(
	PSDevice pDevice,
	unsigned char byCurrChannel
)
{
	bool exceed = false;

	switch (pDevice->byZoneType) {
	case 0x00:                  //USA:1~11
		if ((byCurrChannel < 1) || (byCurrChannel > 11))
			exceed = true;
		break;
	case 0x01:                  //Japan:1~13
	case 0x02:                  //Europe:1~13
		if ((byCurrChannel < 1) || (byCurrChannel > 13))
			exceed = true;
		break;
	default:                    //reserve for other zonetype
		break;
	}

	return exceed;
}

/*+
 *
 * Routine Description:
 *   Handles and analysis incoming beacon frames.
 *
 *
 * Return Value:
 *    None.
 *
 -*/

static
void
s_vMgrRxBeacon(
	PSDevice pDevice,
	PSMgmtObject pMgmt,
	PSRxMgmtPacket pRxPacket,
	bool bInScan
)
{
	PKnownBSS           pBSSList;
	WLAN_FR_BEACON      sFrame;
	QWORD               qwTSFOffset;
	bool bIsBSSIDEqual = false;
	bool bIsSSIDEqual = false;
	bool bTSFLargeDiff = false;
	bool bTSFOffsetPostive = false;
	bool bUpdateTSF = false;
	bool bIsAPBeacon = false;
	bool bIsChannelEqual = false;
	unsigned int uLocateByteIndex;
	unsigned char byTIMBitOn = 0;
	unsigned short wAIDNumber = 0;
	unsigned int uNodeIndex;
	QWORD               qwTimestamp, qwLocalTSF;
	QWORD               qwCurrTSF;
	unsigned short wStartIndex = 0;
	unsigned short wAIDIndex = 0;
	unsigned char byCurrChannel = pRxPacket->byRxChannel;
	ERPObject           sERP;
	unsigned int uRateLen = WLAN_RATES_MAXLEN;
	bool bChannelHit = false;
	bool bUpdatePhyParameter = false;
	unsigned char byIEChannel = 0;

	memset(&sFrame, 0, sizeof(WLAN_FR_BEACON));
	sFrame.len = pRxPacket->cbMPDULen;
	sFrame.pBuf = (unsigned char *)pRxPacket->p80211Header;

	// decode the beacon frame
	vMgrDecodeBeacon(&sFrame);

	if ((sFrame.pwBeaconInterval == 0) ||
	    (sFrame.pwCapInfo == 0) ||
	    (sFrame.pSSID == 0) ||
	    (sFrame.pSuppRates == 0)) {
		DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Rx beacon frame error\n");
		return;
	}

	if (sFrame.pDSParms != NULL) {
		if (byCurrChannel > CB_MAX_CHANNEL_24G) {
			// channel remapping to
			byIEChannel = get_channel_mapping(pDevice, sFrame.pDSParms->byCurrChannel, PHY_TYPE_11A);
		} else {
			byIEChannel = sFrame.pDSParms->byCurrChannel;
		}
		if (byCurrChannel != byIEChannel) {
			// adjust channel info. bcs we rcv adjacent channel packets
			bChannelHit = false;
			byCurrChannel = byIEChannel;
		}
	} else {
		// no DS channel info
		bChannelHit = true;
	}
//2008-0730-01<Add>by MikeLiu
	if (ChannelExceedZoneType(pDevice, byCurrChannel) == true)
		return;

	if (sFrame.pERP != NULL) {
		sERP.byERP = sFrame.pERP->byContext;
		sERP.bERPExist = true;

	} else {
		sERP.bERPExist = false;
		sERP.byERP = 0;
	}

	pBSSList = BSSpAddrIsInBSSList((void *)pDevice, sFrame.pHdr->sA3.abyAddr3, sFrame.pSSID);
	if (pBSSList == NULL) {
		DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Beacon/insert: RxChannel = : %d\n", byCurrChannel);
		BSSbInsertToBSSList((void *)pDevice,
				    sFrame.pHdr->sA3.abyAddr3,
				    *sFrame.pqwTimestamp,
				    *sFrame.pwBeaconInterval,
				    *sFrame.pwCapInfo,
				    byCurrChannel,
				    sFrame.pSSID,
				    sFrame.pSuppRates,
				    sFrame.pExtSuppRates,
				    &sERP,
				    sFrame.pRSN,
				    sFrame.pRSNWPA,
				    sFrame.pIE_Country,
				    sFrame.pIE_Quiet,
				    sFrame.len - WLAN_HDR_ADDR3_LEN,
				    sFrame.pHdr->sA4.abyAddr4,   // payload of beacon
				    (void *)pRxPacket
);
	} else {
//        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "update bcn: RxChannel = : %d\n", byCurrChannel);
		BSSbUpdateToBSSList((void *)pDevice,
				    *sFrame.pqwTimestamp,
				    *sFrame.pwBeaconInterval,
				    *sFrame.pwCapInfo,
				    byCurrChannel,
				    bChannelHit,
				    sFrame.pSSID,
				    sFrame.pSuppRates,
				    sFrame.pExtSuppRates,
				    &sERP,
				    sFrame.pRSN,
				    sFrame.pRSNWPA,
				    sFrame.pIE_Country,
				    sFrame.pIE_Quiet,
				    pBSSList,
				    sFrame.len - WLAN_HDR_ADDR3_LEN,
				    sFrame.pHdr->sA4.abyAddr4,   // payload of probresponse
				    (void *)pRxPacket
);

	}

	if (bInScan) {
		return;
	}

	if (byCurrChannel == (unsigned char)pMgmt->uCurrChannel)
		bIsChannelEqual = true;

	if (bIsChannelEqual && (pMgmt->eCurrMode == WMAC_MODE_ESS_AP)) {
		// if rx beacon without ERP field
		if (sERP.bERPExist) {
			if (WLAN_GET_ERP_USE_PROTECTION(sERP.byERP)) {
				pDevice->byERPFlag |= WLAN_SET_ERP_USE_PROTECTION(1);
				pDevice->wUseProtectCntDown = USE_PROTECT_PERIOD;
			}
		} else {
			pDevice->byERPFlag |= WLAN_SET_ERP_USE_PROTECTION(1);
			pDevice->wUseProtectCntDown = USE_PROTECT_PERIOD;
		}

		if (pMgmt->eCurrMode == WMAC_MODE_IBSS_STA) {
			if (!WLAN_GET_CAP_INFO_SHORTPREAMBLE(*sFrame.pwCapInfo))
				pDevice->byERPFlag |= WLAN_SET_ERP_BARKER_MODE(1);
			if (!sERP.bERPExist)
				pDevice->byERPFlag |= WLAN_SET_ERP_NONERP_PRESENT(1);
		}

		// set to MAC&BBP
		if (WLAN_GET_ERP_USE_PROTECTION(pDevice->byERPFlag)) {
			if (!pDevice->bProtectMode) {
				MACvEnableProtectMD(pDevice->PortOffset);
				pDevice->bProtectMode = true;
			}
		}
	}

	if (pMgmt->eCurrMode == WMAC_MODE_ESS_AP)
		return;

	// check if BSSID the same
	if (memcmp(sFrame.pHdr->sA3.abyAddr3,
		   pMgmt->abyCurrBSSID,
		   WLAN_BSSID_LEN) == 0) {
		bIsBSSIDEqual = true;

// 2008-05-21 <add> by Richardtai
		pDevice->uCurrRSSI = pRxPacket->uRSSI;
		pDevice->byCurrSQ = pRxPacket->bySQ;

		if (pMgmt->sNodeDBTable[0].uInActiveCount != 0) {
			pMgmt->sNodeDBTable[0].uInActiveCount = 0;
			//DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "BCN:Wake Count= [%d]\n", pMgmt->wCountToWakeUp);
		}
	}
	// check if SSID the same
	if (sFrame.pSSID->len == ((PWLAN_IE_SSID)pMgmt->abyCurrSSID)->len) {
		if (memcmp(sFrame.pSSID->abySSID,
			   ((PWLAN_IE_SSID)pMgmt->abyCurrSSID)->abySSID,
			   sFrame.pSSID->len
) == 0) {
			bIsSSIDEqual = true;
		}
	}

	if ((WLAN_GET_CAP_INFO_ESS(*sFrame.pwCapInfo) == true) &&
	    (bIsBSSIDEqual == true) &&
	    (bIsSSIDEqual == true) &&
	    (pMgmt->eCurrMode == WMAC_MODE_ESS_STA) &&
	    (pMgmt->eCurrState == WMAC_STATE_ASSOC)) {
		// add state check to prevent reconnect fail since we'll receive Beacon

		bIsAPBeacon = true;

		if (pBSSList != NULL) {
			// Compare PHY parameter setting
			if (pMgmt->wCurrCapInfo != pBSSList->wCapInfo) {
				bUpdatePhyParameter = true;
				pMgmt->wCurrCapInfo = pBSSList->wCapInfo;
			}
			if (sFrame.pERP != NULL) {
				if ((sFrame.pERP->byElementID == WLAN_EID_ERP) &&
				    (pMgmt->byERPContext != sFrame.pERP->byContext)) {
					bUpdatePhyParameter = true;
					pMgmt->byERPContext = sFrame.pERP->byContext;
				}
			}
			//
			// Basic Rate Set may change dynamically
			//
			if (pBSSList->eNetworkTypeInUse == PHY_TYPE_11B) {
				uRateLen = WLAN_RATES_MAXLEN_11B;
			}
			pMgmt->abyCurrSuppRates[1] = RATEuSetIE((PWLAN_IE_SUPP_RATES)pBSSList->abySuppRates,
								(PWLAN_IE_SUPP_RATES)pMgmt->abyCurrSuppRates,
								uRateLen);
			pMgmt->abyCurrExtSuppRates[1] = RATEuSetIE((PWLAN_IE_SUPP_RATES)pBSSList->abyExtSuppRates,
								   (PWLAN_IE_SUPP_RATES)pMgmt->abyCurrExtSuppRates,
								   uRateLen);
			RATEvParseMaxRate((void *)pDevice,
					  (PWLAN_IE_SUPP_RATES)pMgmt->abyCurrSuppRates,
					  (PWLAN_IE_SUPP_RATES)pMgmt->abyCurrExtSuppRates,
					  true,
					  &(pMgmt->sNodeDBTable[0].wMaxBasicRate),
					  &(pMgmt->sNodeDBTable[0].wMaxSuppRate),
					  &(pMgmt->sNodeDBTable[0].wSuppRate),
					  &(pMgmt->sNodeDBTable[0].byTopCCKBasicRate),
					  &(pMgmt->sNodeDBTable[0].byTopOFDMBasicRate)
				);
			if (bUpdatePhyParameter == true) {
				CARDbSetPhyParameter(pMgmt->pAdapter,
						     pMgmt->eCurrentPHYMode,
						     pMgmt->wCurrCapInfo,
						     pMgmt->byERPContext,
						     pMgmt->abyCurrSuppRates,
						     pMgmt->abyCurrExtSuppRates
					);
			}
			if (sFrame.pIE_PowerConstraint != NULL) {
				CARDvSetPowerConstraint(pMgmt->pAdapter,
							(unsigned char) pBSSList->uChannel,
							sFrame.pIE_PowerConstraint->byPower
);
			}
			if (sFrame.pIE_CHSW != NULL) {
				CARDbChannelSwitch(pMgmt->pAdapter,
						   sFrame.pIE_CHSW->byMode,
						   get_channel_mapping(pMgmt->pAdapter, sFrame.pIE_CHSW->byMode, pMgmt->eCurrentPHYMode),
						   sFrame.pIE_CHSW->byCount
					);

			} else if (bIsChannelEqual == false) {
				set_channel(pMgmt->pAdapter, pBSSList->uChannel);
			}
		}
	}

//    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Beacon 2 \n");
	// check if CF field exists
	if (WLAN_GET_CAP_INFO_ESS(*sFrame.pwCapInfo)) {
		if (sFrame.pCFParms->wCFPDurRemaining > 0) {
			// TODO: deal with CFP period to set NAV
		}
	}

	HIDWORD(qwTimestamp) = cpu_to_le32(HIDWORD(*sFrame.pqwTimestamp));
	LODWORD(qwTimestamp) = cpu_to_le32(LODWORD(*sFrame.pqwTimestamp));
	HIDWORD(qwLocalTSF) = HIDWORD(pRxPacket->qwLocalTSF);
	LODWORD(qwLocalTSF) = LODWORD(pRxPacket->qwLocalTSF);

	// check if beacon TSF larger or small than our local TSF
	if (HIDWORD(qwTimestamp) == HIDWORD(qwLocalTSF)) {
		if (LODWORD(qwTimestamp) >= LODWORD(qwLocalTSF)) {
			bTSFOffsetPostive = true;
		} else {
			bTSFOffsetPostive = false;
		}
	} else if (HIDWORD(qwTimestamp) > HIDWORD(qwLocalTSF)) {
		bTSFOffsetPostive = true;
	} else if (HIDWORD(qwTimestamp) < HIDWORD(qwLocalTSF)) {
		bTSFOffsetPostive = false;
	}

	if (bTSFOffsetPostive) {
		qwTSFOffset = CARDqGetTSFOffset(pRxPacket->byRxRate, (qwTimestamp), (qwLocalTSF));
	} else {
		qwTSFOffset = CARDqGetTSFOffset(pRxPacket->byRxRate, (qwLocalTSF), (qwTimestamp));
	}

	if (HIDWORD(qwTSFOffset) != 0 ||
	    (LODWORD(qwTSFOffset) > TRIVIAL_SYNC_DIFFERENCE)) {
		bTSFLargeDiff = true;
	}

	// if infra mode
	if (bIsAPBeacon == true) {
		// Infra mode: Local TSF always follow AP's TSF if Difference huge.
		if (bTSFLargeDiff)
			bUpdateTSF = true;

		if ((pDevice->bEnablePSMode == true) && (sFrame.pTIM != 0)) {
			// deal with DTIM, analysis TIM
			pMgmt->bMulticastTIM = WLAN_MGMT_IS_MULTICAST_TIM(sFrame.pTIM->byBitMapCtl) ? true : false;
			pMgmt->byDTIMCount = sFrame.pTIM->byDTIMCount;
			pMgmt->byDTIMPeriod = sFrame.pTIM->byDTIMPeriod;
			wAIDNumber = pMgmt->wCurrAID & ~(BIT14|BIT15);

			// check if AID in TIM field bit on
			// wStartIndex = N1
			wStartIndex = WLAN_MGMT_GET_TIM_OFFSET(sFrame.pTIM->byBitMapCtl) << 1;
			// AIDIndex = N2
			wAIDIndex = (wAIDNumber >> 3);
			if ((wAIDNumber > 0) && (wAIDIndex >= wStartIndex)) {
				uLocateByteIndex = wAIDIndex - wStartIndex;
				// len = byDTIMCount + byDTIMPeriod + byDTIMPeriod + byVirtBitMap[0~250]
				if (sFrame.pTIM->len >= (uLocateByteIndex + 4)) {
					byTIMBitOn  = (0x01) << ((wAIDNumber) % 8);
					pMgmt->bInTIM = sFrame.pTIM->byVirtBitMap[uLocateByteIndex] & byTIMBitOn ? true : false;
				} else {
					pMgmt->bInTIM = false;
				};
			} else {
				pMgmt->bInTIM = false;
			};

			if (pMgmt->bInTIM ||
			    (pMgmt->bMulticastTIM && (pMgmt->byDTIMCount == 0))) {
				pMgmt->bInTIMWake = true;
				// send out ps-poll packet
//                DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "BCN:In TIM\n");
				if (pMgmt->bInTIM) {
					PSvSendPSPOLL((PSDevice)pDevice);
//                    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "BCN:PS-POLL sent..\n");
				}

			} else {
				pMgmt->bInTIMWake = false;
				DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "BCN: Not In TIM..\n");
				if (pDevice->bPWBitOn == false) {
					DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "BCN: Send Null Packet\n");
					if (PSbSendNullPacket(pDevice))
						pDevice->bPWBitOn = true;
				}
				if (PSbConsiderPowerDown(pDevice, false, false)) {
					DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "BCN: Power down now...\n");
				}
			}

		}

	}
	// if adhoc mode
	if ((pMgmt->eCurrMode == WMAC_MODE_IBSS_STA) && !bIsAPBeacon && bIsChannelEqual) {
		if (bIsBSSIDEqual) {
			// Use sNodeDBTable[0].uInActiveCount as IBSS beacons received count.
			if (pMgmt->sNodeDBTable[0].uInActiveCount != 0)
				pMgmt->sNodeDBTable[0].uInActiveCount = 0;

			// adhoc mode:TSF updated only when beacon larger than local TSF
			if (bTSFLargeDiff && bTSFOffsetPostive &&
			    (pMgmt->eCurrState == WMAC_STATE_JOINTED))
				bUpdateTSF = true;

			// During dpc, already in spinlocked.
			if (BSSDBbIsSTAInNodeDB(pMgmt, sFrame.pHdr->sA3.abyAddr2, &uNodeIndex)) {
				// Update the STA, (Technically the Beacons of all the IBSS nodes
				// should be identical, but that's not happening in practice.
				pMgmt->abyCurrSuppRates[1] = RATEuSetIE((PWLAN_IE_SUPP_RATES)sFrame.pSuppRates,
									(PWLAN_IE_SUPP_RATES)pMgmt->abyCurrSuppRates,
									WLAN_RATES_MAXLEN_11B);
				RATEvParseMaxRate((void *)pDevice,
						  (PWLAN_IE_SUPP_RATES)pMgmt->abyCurrSuppRates,
						  NULL,
						  true,
						  &(pMgmt->sNodeDBTable[uNodeIndex].wMaxBasicRate),
						  &(pMgmt->sNodeDBTable[uNodeIndex].wMaxSuppRate),
						  &(pMgmt->sNodeDBTable[uNodeIndex].wSuppRate),
						  &(pMgmt->sNodeDBTable[uNodeIndex].byTopCCKBasicRate),
						  &(pMgmt->sNodeDBTable[uNodeIndex].byTopOFDMBasicRate)
					);
				pMgmt->sNodeDBTable[uNodeIndex].bShortPreamble = WLAN_GET_CAP_INFO_SHORTPREAMBLE(*sFrame.pwCapInfo);
				pMgmt->sNodeDBTable[uNodeIndex].bShortSlotTime = WLAN_GET_CAP_INFO_SHORTSLOTTIME(*sFrame.pwCapInfo);
				pMgmt->sNodeDBTable[uNodeIndex].uInActiveCount = 0;
			} else {
				// Todo, initial Node content
				BSSvCreateOneNode((PSDevice)pDevice, &uNodeIndex);

				pMgmt->abyCurrSuppRates[1] = RATEuSetIE((PWLAN_IE_SUPP_RATES)sFrame.pSuppRates,
									(PWLAN_IE_SUPP_RATES)pMgmt->abyCurrSuppRates,
									WLAN_RATES_MAXLEN_11B);
				RATEvParseMaxRate((void *)pDevice,
						  (PWLAN_IE_SUPP_RATES)pMgmt->abyCurrSuppRates,
						  NULL,
						  true,
						  &(pMgmt->sNodeDBTable[uNodeIndex].wMaxBasicRate),
						  &(pMgmt->sNodeDBTable[uNodeIndex].wMaxSuppRate),
						  &(pMgmt->sNodeDBTable[uNodeIndex].wSuppRate),
						  &(pMgmt->sNodeDBTable[uNodeIndex].byTopCCKBasicRate),
						  &(pMgmt->sNodeDBTable[uNodeIndex].byTopOFDMBasicRate)
					);

				memcpy(pMgmt->sNodeDBTable[uNodeIndex].abyMACAddr, sFrame.pHdr->sA3.abyAddr2, WLAN_ADDR_LEN);
				pMgmt->sNodeDBTable[uNodeIndex].bShortPreamble = WLAN_GET_CAP_INFO_SHORTPREAMBLE(*sFrame.pwCapInfo);
				pMgmt->sNodeDBTable[uNodeIndex].wTxDataRate = pMgmt->sNodeDBTable[uNodeIndex].wMaxSuppRate;
#ifdef	PLICE_DEBUG
				{
					printk("s_vMgrRxBeacon:TxDataRate is %d,Index is %d\n", pMgmt->sNodeDBTable[uNodeIndex].wTxDataRate, uNodeIndex);
				}
#endif
/*
  pMgmt->sNodeDBTable[uNodeIndex].bShortSlotTime = WLAN_GET_CAP_INFO_SHORTSLOTTIME(*sFrame.pwCapInfo);
  if (pMgmt->sNodeDBTable[uNodeIndex].wMaxSuppRate > RATE_11M)
  pMgmt->sNodeDBTable[uNodeIndex].bERPExist = true;
*/
			}

			// if other stations joined, indicate connection to upper layer..
			if (pMgmt->eCurrState == WMAC_STATE_STARTED) {
				DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Current IBSS State: [Started]........to: [Jointed] \n");
				pMgmt->eCurrState = WMAC_STATE_JOINTED;
				pDevice->bLinkPass = true;
				if (netif_queue_stopped(pDevice->dev)) {
					netif_wake_queue(pDevice->dev);
				}
				pMgmt->sNodeDBTable[0].bActive = true;
				pMgmt->sNodeDBTable[0].uInActiveCount = 0;

			}
		} else if (bIsSSIDEqual) {
			// See other adhoc sta with the same SSID but BSSID is different.
			// adpot this vars only when TSF larger then us.
			if (bTSFLargeDiff && bTSFOffsetPostive) {
				// we don't support ATIM under adhoc mode
				// if (sFrame.pIBSSParms->wATIMWindow == 0) {
				// adpot this vars
				// TODO: check sFrame cap if privacy on, and support rate syn
				memcpy(pMgmt->abyCurrBSSID, sFrame.pHdr->sA3.abyAddr3, WLAN_BSSID_LEN);
				memcpy(pDevice->abyBSSID, pMgmt->abyCurrBSSID, WLAN_BSSID_LEN);
				pMgmt->wCurrATIMWindow = cpu_to_le16(sFrame.pIBSSParms->wATIMWindow);
				pMgmt->wCurrBeaconPeriod = cpu_to_le16(*sFrame.pwBeaconInterval);
				pMgmt->abyCurrSuppRates[1] = RATEuSetIE((PWLAN_IE_SUPP_RATES)sFrame.pSuppRates,
									(PWLAN_IE_SUPP_RATES)pMgmt->abyCurrSuppRates,
									WLAN_RATES_MAXLEN_11B);
				// set HW beacon interval and re-synchronizing....
				DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Rejoining to Other Adhoc group with same SSID........\n");
				VNSvOutPortW(pDevice->PortOffset + MAC_REG_BI, pMgmt->wCurrBeaconPeriod);
				CARDbUpdateTSF(pDevice, pRxPacket->byRxRate, qwTimestamp, qwLocalTSF);
				CARDvUpdateNextTBTT(pDevice->PortOffset, qwTimestamp, pMgmt->wCurrBeaconPeriod);
				// Turn off bssid filter to avoid filter others adhoc station which bssid is different.
				MACvWriteBSSIDAddress(pDevice->PortOffset, pMgmt->abyCurrBSSID);

				CARDbSetPhyParameter(pMgmt->pAdapter,
						     pMgmt->eCurrentPHYMode,
						     pMgmt->wCurrCapInfo,
						     pMgmt->byERPContext,
						     pMgmt->abyCurrSuppRates,
						     pMgmt->abyCurrExtSuppRates);

				// MACvRegBitsOff(pDevice->PortOffset, MAC_REG_RCR, RCR_BSSID);
				// set highest basic rate
				// s_vSetHighestBasicRate(pDevice, (PWLAN_IE_SUPP_RATES)pMgmt->abyCurrSuppRates);
				// Prepare beacon frame
				bMgrPrepareBeaconToSend((void *)pDevice, pMgmt);
				//  }
			}
		}
	}
	// endian issue ???
	// Update TSF
if (bUpdateTSF) {
		CARDbGetCurrentTSF(pDevice->PortOffset, &qwCurrTSF);
		CARDbUpdateTSF(pDevice, pRxPacket->byRxRate, qwTimestamp, pRxPacket->qwLocalTSF);
		CARDbGetCurrentTSF(pDevice->PortOffset, &qwCurrTSF);
		CARDvUpdateNextTBTT(pDevice->PortOffset, qwTimestamp, pMgmt->wCurrBeaconPeriod);
	}

	return;
}

/*+
 *
 * Routine Description:
 *   Instructs the hw to create a bss using the supplied
 *   attributes. Note that this implementation only supports Ad-Hoc
 *   BSS creation.
 *
 *
 * Return Value:
 *    CMD_STATUS
 *
 -*/
void
vMgrCreateOwnIBSS(
	void *hDeviceContext,
	PCMD_STATUS pStatus
)
{
	PSDevice            pDevice = (PSDevice)hDeviceContext;
	PSMgmtObject        pMgmt = pDevice->pMgmt;
	unsigned short wMaxBasicRate;
	unsigned short wMaxSuppRate;
	unsigned char byTopCCKBasicRate;
	unsigned char byTopOFDMBasicRate;
	QWORD               qwCurrTSF;
	unsigned int ii;
	unsigned char abyRATE[] = {0x82, 0x84, 0x8B, 0x96, 0x24, 0x30, 0x48, 0x6C, 0x0C, 0x12, 0x18, 0x60};
	unsigned char abyCCK_RATE[] = {0x82, 0x84, 0x8B, 0x96};
	unsigned char abyOFDM_RATE[] = {0x0C, 0x12, 0x18, 0x24, 0x30, 0x48, 0x60, 0x6C};
	unsigned short wSuppRate;

	DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Create Basic Service Set .......\n");

	if (pMgmt->eConfigMode == WMAC_CONFIG_IBSS_STA) {
		if ((pMgmt->eAuthenMode == WMAC_AUTH_WPANONE) &&
		    (pDevice->eEncryptionStatus != Ndis802_11Encryption2Enabled) &&
		    (pDevice->eEncryptionStatus != Ndis802_11Encryption3Enabled)) {
			// encryption mode error
			*pStatus = CMD_STATUS_FAILURE;
			return;
		}
	}

	pMgmt->abyCurrSuppRates[0] = WLAN_EID_SUPP_RATES;
	pMgmt->abyCurrExtSuppRates[0] = WLAN_EID_EXTSUPP_RATES;

	if (pMgmt->eConfigMode == WMAC_CONFIG_AP) {
		pMgmt->eCurrentPHYMode = pMgmt->byAPBBType;
	} else {
		if (pDevice->byBBType == BB_TYPE_11G)
			pMgmt->eCurrentPHYMode = PHY_TYPE_11G;
		if (pDevice->byBBType == BB_TYPE_11B)
			pMgmt->eCurrentPHYMode = PHY_TYPE_11B;
		if (pDevice->byBBType == BB_TYPE_11A)
			pMgmt->eCurrentPHYMode = PHY_TYPE_11A;
	}

	if (pMgmt->eCurrentPHYMode != PHY_TYPE_11A) {
		pMgmt->abyCurrSuppRates[1] = WLAN_RATES_MAXLEN_11B;
		pMgmt->abyCurrExtSuppRates[1] = 0;
		for (ii = 0; ii < 4; ii++)
			pMgmt->abyCurrSuppRates[2+ii] = abyRATE[ii];
	} else {
		pMgmt->abyCurrSuppRates[1] = 8;
		pMgmt->abyCurrExtSuppRates[1] = 0;
		for (ii = 0; ii < 8; ii++)
			pMgmt->abyCurrSuppRates[2+ii] = abyRATE[ii];
	}

	if (pMgmt->eCurrentPHYMode == PHY_TYPE_11G) {
		pMgmt->abyCurrSuppRates[1] = 8;
		pMgmt->abyCurrExtSuppRates[1] = 4;
		for (ii = 0; ii < 4; ii++)
			pMgmt->abyCurrSuppRates[2+ii] =  abyCCK_RATE[ii];
		for (ii = 4; ii < 8; ii++)
			pMgmt->abyCurrSuppRates[2+ii] =  abyOFDM_RATE[ii-4];
		for (ii = 0; ii < 4; ii++)
			pMgmt->abyCurrExtSuppRates[2+ii] =  abyOFDM_RATE[ii+4];
	}

	// Disable Protect Mode
	pDevice->bProtectMode = 0;
	MACvDisableProtectMD(pDevice->PortOffset);

	pDevice->bBarkerPreambleMd = 0;
	MACvDisableBarkerPreambleMd(pDevice->PortOffset);

	// Kyle Test 2003.11.04

	// set HW beacon interval
	if (pMgmt->wIBSSBeaconPeriod == 0)
		pMgmt->wIBSSBeaconPeriod = DEFAULT_IBSS_BI;

	CARDbGetCurrentTSF(pDevice->PortOffset, &qwCurrTSF);
	// clear TSF counter
	VNSvOutPortB(pDevice->PortOffset + MAC_REG_TFTCTL, TFTCTL_TSFCNTRST);
	// enable TSF counter
	VNSvOutPortB(pDevice->PortOffset + MAC_REG_TFTCTL, TFTCTL_TSFCNTREN);

	// set Next TBTT
	CARDvSetFirstNextTBTT(pDevice->PortOffset, pMgmt->wIBSSBeaconPeriod);

	pMgmt->uIBSSChannel = pDevice->uChannel;

	if (pMgmt->uIBSSChannel == 0)
		pMgmt->uIBSSChannel = DEFAULT_IBSS_CHANNEL;

	// set basic rate

	RATEvParseMaxRate((void *)pDevice, (PWLAN_IE_SUPP_RATES)pMgmt->abyCurrSuppRates,
			  (PWLAN_IE_SUPP_RATES)pMgmt->abyCurrExtSuppRates, true,
			  &wMaxBasicRate, &wMaxSuppRate, &wSuppRate,
			  &byTopCCKBasicRate, &byTopOFDMBasicRate);

	if (pMgmt->eConfigMode == WMAC_CONFIG_AP) {
		pMgmt->eCurrMode = WMAC_MODE_ESS_AP;
	}

	if (pMgmt->eConfigMode == WMAC_CONFIG_IBSS_STA) {
		memcpy(pMgmt->abyIBSSDFSOwner, pDevice->abyCurrentNetAddr, 6);
		pMgmt->byIBSSDFSRecovery = 10;
		pMgmt->eCurrMode = WMAC_MODE_IBSS_STA;
	}

	// Adopt pre-configured IBSS vars to current vars
	pMgmt->eCurrState = WMAC_STATE_STARTED;
	pMgmt->wCurrBeaconPeriod = pMgmt->wIBSSBeaconPeriod;
	pMgmt->uCurrChannel = pMgmt->uIBSSChannel;
	pMgmt->wCurrATIMWindow = pMgmt->wIBSSATIMWindow;
	MACvWriteATIMW(pDevice->PortOffset, pMgmt->wCurrATIMWindow);
	pDevice->uCurrRSSI = 0;
	pDevice->byCurrSQ = 0;
	//memcpy(pMgmt->abyDesireSSID,pMgmt->abyAdHocSSID,
	// ((PWLAN_IE_SSID)pMgmt->abyAdHocSSID)->len + WLAN_IEHDR_LEN);
	memset(pMgmt->abyCurrSSID, 0, WLAN_IEHDR_LEN + WLAN_SSID_MAXLEN + 1);
	memcpy(pMgmt->abyCurrSSID,
	       pMgmt->abyDesireSSID,
	       ((PWLAN_IE_SSID)pMgmt->abyDesireSSID)->len + WLAN_IEHDR_LEN
);

	if (pMgmt->eCurrMode == WMAC_MODE_ESS_AP) {
		// AP mode BSSID = MAC addr
		memcpy(pMgmt->abyCurrBSSID, pMgmt->abyMACAddr, WLAN_ADDR_LEN);
		DBG_PRT(MSG_LEVEL_INFO, KERN_INFO "AP beacon created BSSID:%pM\n",
			pMgmt->abyCurrBSSID);
	}

	if (pMgmt->eCurrMode == WMAC_MODE_IBSS_STA) {
		// BSSID selected must be randomized as spec 11.1.3
		pMgmt->abyCurrBSSID[5] = (unsigned char) (LODWORD(qwCurrTSF) & 0x000000ff);
		pMgmt->abyCurrBSSID[4] = (unsigned char)((LODWORD(qwCurrTSF) & 0x0000ff00) >> 8);
		pMgmt->abyCurrBSSID[3] = (unsigned char)((LODWORD(qwCurrTSF) & 0x00ff0000) >> 16);
		pMgmt->abyCurrBSSID[2] = (unsigned char)((LODWORD(qwCurrTSF) & 0x00000ff0) >> 4);
		pMgmt->abyCurrBSSID[1] = (unsigned char)((LODWORD(qwCurrTSF) & 0x000ff000) >> 12);
		pMgmt->abyCurrBSSID[0] = (unsigned char)((LODWORD(qwCurrTSF) & 0x0ff00000) >> 20);
		pMgmt->abyCurrBSSID[5] ^= pMgmt->abyMACAddr[0];
		pMgmt->abyCurrBSSID[4] ^= pMgmt->abyMACAddr[1];
		pMgmt->abyCurrBSSID[3] ^= pMgmt->abyMACAddr[2];
		pMgmt->abyCurrBSSID[2] ^= pMgmt->abyMACAddr[3];
		pMgmt->abyCurrBSSID[1] ^= pMgmt->abyMACAddr[4];
		pMgmt->abyCurrBSSID[0] ^= pMgmt->abyMACAddr[5];
		pMgmt->abyCurrBSSID[0] &= ~IEEE_ADDR_GROUP;
		pMgmt->abyCurrBSSID[0] |= IEEE_ADDR_UNIVERSAL;

		DBG_PRT(MSG_LEVEL_INFO, KERN_INFO "Adhoc beacon created bssid:%pM\n",
			pMgmt->abyCurrBSSID);
	}

	// Set Capability Info
	pMgmt->wCurrCapInfo = 0;

	if (pMgmt->eCurrMode == WMAC_MODE_ESS_AP) {
		pMgmt->wCurrCapInfo |= WLAN_SET_CAP_INFO_ESS(1);
		pMgmt->byDTIMPeriod = DEFAULT_DTIM_PERIOD;
		pMgmt->byDTIMCount = pMgmt->byDTIMPeriod - 1;
	}

	if (pMgmt->eCurrMode == WMAC_MODE_IBSS_STA) {
		pMgmt->wCurrCapInfo |= WLAN_SET_CAP_INFO_IBSS(1);
	}

	if (pDevice->bEncryptionEnable) {
		pMgmt->wCurrCapInfo |= WLAN_SET_CAP_INFO_PRIVACY(1);
		if (pMgmt->eAuthenMode == WMAC_AUTH_WPANONE) {
			if (pDevice->eEncryptionStatus == Ndis802_11Encryption3Enabled) {
				pMgmt->byCSSPK = KEY_CTL_CCMP;
				pMgmt->byCSSGK = KEY_CTL_CCMP;
			} else if (pDevice->eEncryptionStatus == Ndis802_11Encryption2Enabled) {
				pMgmt->byCSSPK = KEY_CTL_TKIP;
				pMgmt->byCSSGK = KEY_CTL_TKIP;
			} else {
				pMgmt->byCSSPK = KEY_CTL_NONE;
				pMgmt->byCSSGK = KEY_CTL_WEP;
			}
		} else {
			pMgmt->byCSSPK = KEY_CTL_WEP;
			pMgmt->byCSSGK = KEY_CTL_WEP;
		}
	}

	pMgmt->byERPContext = 0;

//    memcpy(pDevice->abyBSSID, pMgmt->abyCurrBSSID, WLAN_BSSID_LEN);

	if (pMgmt->eConfigMode == WMAC_CONFIG_AP) {
		CARDbSetBSSID(pMgmt->pAdapter, pMgmt->abyCurrBSSID, OP_MODE_AP);
	} else {
		CARDbSetBSSID(pMgmt->pAdapter, pMgmt->abyCurrBSSID, OP_MODE_ADHOC);
	}

	CARDbSetPhyParameter(pMgmt->pAdapter,
			     pMgmt->eCurrentPHYMode,
			     pMgmt->wCurrCapInfo,
			     pMgmt->byERPContext,
			     pMgmt->abyCurrSuppRates,
			     pMgmt->abyCurrExtSuppRates
		);

	CARDbSetBeaconPeriod(pMgmt->pAdapter, pMgmt->wIBSSBeaconPeriod);
	// set channel and clear NAV
	set_channel(pMgmt->pAdapter, pMgmt->uIBSSChannel);
	pMgmt->uCurrChannel = pMgmt->uIBSSChannel;

	if (CARDbIsShortPreamble(pMgmt->pAdapter)) {
		pMgmt->wCurrCapInfo |= WLAN_SET_CAP_INFO_SHORTPREAMBLE(1);
	} else {
		pMgmt->wCurrCapInfo &= (~WLAN_SET_CAP_INFO_SHORTPREAMBLE(1));
	}

	if ((pMgmt->b11hEnable == true) &&
	    (pMgmt->eCurrentPHYMode == PHY_TYPE_11A)) {
		pMgmt->wCurrCapInfo |= WLAN_SET_CAP_INFO_SPECTRUMMNG(1);
	} else {
		pMgmt->wCurrCapInfo &= (~WLAN_SET_CAP_INFO_SPECTRUMMNG(1));
	}

	pMgmt->eCurrState = WMAC_STATE_STARTED;
	// Prepare beacon to send
	if (bMgrPrepareBeaconToSend((void *)pDevice, pMgmt)) {
		*pStatus = CMD_STATUS_SUCCESS;
	}

	return;
}

/*+
 *
 * Routine Description:
 *   Instructs wmac to join a bss using the supplied attributes.
 *   The arguments may the BSSID or SSID and the rest of the
 *   attributes are obtained from the scan result of known bss list.
 *
 *
 * Return Value:
 *    None.
 *
 -*/

void
vMgrJoinBSSBegin(
	void *hDeviceContext,
	PCMD_STATUS pStatus
)
{
	PSDevice     pDevice = (PSDevice)hDeviceContext;
	PSMgmtObject    pMgmt = pDevice->pMgmt;
	PKnownBSS       pCurr = NULL;
	unsigned int ii, uu;
	PWLAN_IE_SUPP_RATES pItemRates = NULL;
	PWLAN_IE_SUPP_RATES pItemExtRates = NULL;
	PWLAN_IE_SSID   pItemSSID;
	unsigned int uRateLen = WLAN_RATES_MAXLEN;
	unsigned short wMaxBasicRate = RATE_1M;
	unsigned short wMaxSuppRate = RATE_1M;
	unsigned short wSuppRate;
	unsigned char byTopCCKBasicRate = RATE_1M;
	unsigned char byTopOFDMBasicRate = RATE_1M;

	for (ii = 0; ii < MAX_BSS_NUM; ii++) {
		if (pMgmt->sBSSList[ii].bActive == true)
			break;
	}

	if (ii == MAX_BSS_NUM) {
		*pStatus = CMD_STATUS_RESOURCES;
		DBG_PRT(MSG_LEVEL_NOTICE, KERN_INFO "BSS finding:BSS list is empty.\n");
		return;
	}

	// memset(pMgmt->abyDesireBSSID, 0,  WLAN_BSSID_LEN);
	// Search known BSS list for prefer BSSID or SSID

	pCurr = BSSpSearchBSSList(pDevice,
				  pMgmt->abyDesireBSSID,
				  pMgmt->abyDesireSSID,
				  pMgmt->eConfigPHYMode
);

	if (pCurr == NULL) {
		*pStatus = CMD_STATUS_RESOURCES;
		pItemSSID = (PWLAN_IE_SSID)pMgmt->abyDesireSSID;
		DBG_PRT(MSG_LEVEL_NOTICE, KERN_INFO "Scanning [%s] not found, disconnected !\n", pItemSSID->abySSID);
		return;
	}

	DBG_PRT(MSG_LEVEL_NOTICE, KERN_INFO "AP(BSS) finding:Found a AP(BSS)..\n");
	if (WLAN_GET_CAP_INFO_ESS(cpu_to_le16(pCurr->wCapInfo))) {
		if ((pMgmt->eAuthenMode == WMAC_AUTH_WPA) || (pMgmt->eAuthenMode == WMAC_AUTH_WPAPSK)) {
			// patch for CISCO migration mode
/*
  if (pDevice->eEncryptionStatus == Ndis802_11Encryption2Enabled) {
  if (WPA_SearchRSN(0, WPA_TKIP, pCurr) == false) {
  DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "No match RSN info. ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n");
  // encryption mode error
  pMgmt->eCurrState = WMAC_STATE_IDLE;
  return;
  }
  } else if (pDevice->eEncryptionStatus == Ndis802_11Encryption3Enabled) {
  if (WPA_SearchRSN(0, WPA_AESCCMP, pCurr) == false) {
  DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "No match RSN info. ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n");
  // encryption mode error
  pMgmt->eCurrState = WMAC_STATE_IDLE;
  return;
  }
  }
*/
		}

#ifdef WPA_SUPPLICANT_DRIVER_WEXT_SUPPORT
		Encyption_Rebuild(pDevice, pCurr);
#endif
		// Infrastructure BSS
		s_vMgrSynchBSS(pDevice,
			       WMAC_MODE_ESS_STA,
			       pCurr,
			       pStatus
);

		if (*pStatus == CMD_STATUS_SUCCESS) {
			// Adopt this BSS state vars in Mgmt Object
			pMgmt->uCurrChannel = pCurr->uChannel;

			memset(pMgmt->abyCurrSuppRates, 0 , WLAN_IEHDR_LEN + WLAN_RATES_MAXLEN + 1);
			memset(pMgmt->abyCurrExtSuppRates, 0 , WLAN_IEHDR_LEN + WLAN_RATES_MAXLEN + 1);

			if (pCurr->eNetworkTypeInUse == PHY_TYPE_11B) {
				uRateLen = WLAN_RATES_MAXLEN_11B;
			}

			pItemRates = (PWLAN_IE_SUPP_RATES)pMgmt->abyCurrSuppRates;
			pItemExtRates = (PWLAN_IE_SUPP_RATES)pMgmt->abyCurrExtSuppRates;

			// Parse Support Rate IE
			pItemRates->byElementID = WLAN_EID_SUPP_RATES;
			pItemRates->len = RATEuSetIE((PWLAN_IE_SUPP_RATES)pCurr->abySuppRates,
						     pItemRates,
						     uRateLen);

			// Parse Extension Support Rate IE
			pItemExtRates->byElementID = WLAN_EID_EXTSUPP_RATES;
			pItemExtRates->len = RATEuSetIE((PWLAN_IE_SUPP_RATES)pCurr->abyExtSuppRates,
							pItemExtRates,
							uRateLen);
			// Stuffing Rate IE
			if ((pItemExtRates->len > 0) && (pItemRates->len < 8)) {
				for (ii = 0; ii < (unsigned int)(8 - pItemRates->len);) {
					pItemRates->abyRates[pItemRates->len + ii] = pItemExtRates->abyRates[ii];
					ii++;
					if (pItemExtRates->len <= ii)
						break;
				}
				pItemRates->len += (unsigned char)ii;
				if (pItemExtRates->len - ii > 0) {
					pItemExtRates->len -= (unsigned char)ii;
					for (uu = 0; uu < pItemExtRates->len; uu++) {
						pItemExtRates->abyRates[uu] = pItemExtRates->abyRates[uu + ii];
					}
				} else {
					pItemExtRates->len = 0;
				}
			}

			RATEvParseMaxRate((void *)pDevice, pItemRates, pItemExtRates, true,
					  &wMaxBasicRate, &wMaxSuppRate, &wSuppRate,
					  &byTopCCKBasicRate, &byTopOFDMBasicRate);

			// TODO: deal with if wCapInfo the privacy is on, but station WEP is off
			// TODO: deal with if wCapInfo the PS-Pollable is on.
			pMgmt->wCurrBeaconPeriod = pCurr->wBeaconInterval;
			memset(pMgmt->abyCurrSSID, 0, WLAN_IEHDR_LEN + WLAN_SSID_MAXLEN + 1);
			memcpy(pMgmt->abyCurrBSSID, pCurr->abyBSSID, WLAN_BSSID_LEN);
			memcpy(pMgmt->abyCurrSSID, pCurr->abySSID, WLAN_IEHDR_LEN + WLAN_SSID_MAXLEN + 1);

			pMgmt->eCurrMode = WMAC_MODE_ESS_STA;

			pMgmt->eCurrState = WMAC_STATE_JOINTED;
			// Adopt BSS state in Adapter Device Object
			//pDevice->byOpMode = OP_MODE_INFRASTRUCTURE;
//            memcpy(pDevice->abyBSSID, pCurr->abyBSSID, WLAN_BSSID_LEN);

			// Add current BSS to Candidate list
			// This should only works for WPA2 BSS, and WPA2 BSS check must be done before.
			if (pMgmt->eAuthenMode == WMAC_AUTH_WPA2) {
				bool bResult = bAdd_PMKID_Candidate((void *)pDevice, pMgmt->abyCurrBSSID, &pCurr->sRSNCapObj);
				DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "bAdd_PMKID_Candidate: 1(%d)\n", bResult);
				if (bResult == false) {
					vFlush_PMKID_Candidate((void *)pDevice);
					DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "vFlush_PMKID_Candidate: 4\n");
					bAdd_PMKID_Candidate((void *)pDevice, pMgmt->abyCurrBSSID, &pCurr->sRSNCapObj);
				}
			}

			// Preamble type auto-switch: if AP can receive short-preamble cap,
			// we can turn on too.

			DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Join ESS\n");

			DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "End of Join AP -- A/B/G Action\n");
		} else {
			pMgmt->eCurrState = WMAC_STATE_IDLE;
		};

	} else {
		// ad-hoc mode BSS
		if (pMgmt->eAuthenMode == WMAC_AUTH_WPANONE) {
			if (pDevice->eEncryptionStatus == Ndis802_11Encryption2Enabled) {
				if (WPA_SearchRSN(0, WPA_TKIP, pCurr) == false) {
					// encryption mode error
					pMgmt->eCurrState = WMAC_STATE_IDLE;
					return;
				}
			} else if (pDevice->eEncryptionStatus == Ndis802_11Encryption3Enabled) {
				if (WPA_SearchRSN(0, WPA_AESCCMP, pCurr) == false) {
					// encryption mode error
					pMgmt->eCurrState = WMAC_STATE_IDLE;
					return;
				}
			} else {
				// encryption mode error
				pMgmt->eCurrState = WMAC_STATE_IDLE;
				return;
			}
		}

		s_vMgrSynchBSS(pDevice,
			       WMAC_MODE_IBSS_STA,
			       pCurr,
			       pStatus
);

		if (*pStatus == CMD_STATUS_SUCCESS) {
			// Adopt this BSS state vars in Mgmt Object
			// TODO: check if CapInfo privacy on, but we don't..
			pMgmt->uCurrChannel = pCurr->uChannel;

			// Parse Support Rate IE
			pMgmt->abyCurrSuppRates[0] = WLAN_EID_SUPP_RATES;
			pMgmt->abyCurrSuppRates[1] = RATEuSetIE((PWLAN_IE_SUPP_RATES)pCurr->abySuppRates,
								(PWLAN_IE_SUPP_RATES)pMgmt->abyCurrSuppRates,
								WLAN_RATES_MAXLEN_11B);
			// set basic rate
			RATEvParseMaxRate((void *)pDevice, (PWLAN_IE_SUPP_RATES)pMgmt->abyCurrSuppRates,
					  NULL, true, &wMaxBasicRate, &wMaxSuppRate, &wSuppRate,
					  &byTopCCKBasicRate, &byTopOFDMBasicRate);

			pMgmt->wCurrCapInfo = pCurr->wCapInfo;
			pMgmt->wCurrBeaconPeriod = pCurr->wBeaconInterval;
			memset(pMgmt->abyCurrSSID, 0, WLAN_IEHDR_LEN + WLAN_SSID_MAXLEN);
			memcpy(pMgmt->abyCurrBSSID, pCurr->abyBSSID, WLAN_BSSID_LEN);
			memcpy(pMgmt->abyCurrSSID, pCurr->abySSID, WLAN_IEHDR_LEN + WLAN_SSID_MAXLEN);
//          pMgmt->wCurrATIMWindow = pCurr->wATIMWindow;
			MACvWriteATIMW(pDevice->PortOffset, pMgmt->wCurrATIMWindow);
			pMgmt->eCurrMode = WMAC_MODE_IBSS_STA;

			pMgmt->eCurrState = WMAC_STATE_STARTED;
			// Adopt BSS state in Adapter Device Object
			//pDevice->byOpMode = OP_MODE_ADHOC;
//            pDevice->bLinkPass = true;
//            memcpy(pDevice->abyBSSID, pCurr->abyBSSID, WLAN_BSSID_LEN);

			DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Join IBSS ok:%pM\n",
				pMgmt->abyCurrBSSID);
			// Preamble type auto-switch: if AP can receive short-preamble cap,
			// and if registry setting is short preamble we can turn on too.

			// Prepare beacon
			bMgrPrepareBeaconToSend((void *)pDevice, pMgmt);
		} else {
			pMgmt->eCurrState = WMAC_STATE_IDLE;
		};
	};
	return;
}

/*+
 *
 * Routine Description:
 * Set HW to synchronize a specific BSS from known BSS list.
 *
 *
 * Return Value:
 *    PCM_STATUS
 *
 -*/
static
void
s_vMgrSynchBSS(
	PSDevice      pDevice,
	unsigned int uBSSMode,
	PKnownBSS     pCurr,
	PCMD_STATUS  pStatus
)
{
	CARD_PHY_TYPE   ePhyType = PHY_TYPE_11B;
	PSMgmtObject  pMgmt = pDevice->pMgmt;
//    int     ii;
	//1M,   2M,   5M,   11M,  18M,  24M,  36M,  54M
	unsigned char abyCurrSuppRatesG[] = {WLAN_EID_SUPP_RATES, 8, 0x02, 0x04, 0x0B, 0x16, 0x24, 0x30, 0x48, 0x6C};
	unsigned char abyCurrExtSuppRatesG[] = {WLAN_EID_EXTSUPP_RATES, 4, 0x0C, 0x12, 0x18, 0x60};
	//6M,   9M,   12M,  48M
	unsigned char abyCurrSuppRatesA[] = {WLAN_EID_SUPP_RATES, 8, 0x0C, 0x12, 0x18, 0x24, 0x30, 0x48, 0x60, 0x6C};
	unsigned char abyCurrSuppRatesB[] = {WLAN_EID_SUPP_RATES, 4, 0x02, 0x04, 0x0B, 0x16};

	*pStatus = CMD_STATUS_FAILURE;

	if (s_bCipherMatch(pCurr,
			   pDevice->eEncryptionStatus,
			   &(pMgmt->byCSSPK),
			   &(pMgmt->byCSSGK)) == false) {
		DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "s_bCipherMatch Fail .......\n");
		return;
	}

	pMgmt->pCurrBSS = pCurr;

	// if previous mode is IBSS.
	if (pMgmt->eCurrMode == WMAC_MODE_IBSS_STA) {
		MACvRegBitsOff(pDevice->PortOffset, MAC_REG_BCNDMACTL, BEACON_READY);
		MACvRegBitsOff(pDevice->PortOffset, MAC_REG_TCR, TCR_AUTOBCNTX);
	}

	// Init the BSS informations
	pDevice->bCCK = true;
	pDevice->bProtectMode = false;
	MACvDisableProtectMD(pDevice->PortOffset);
	pDevice->bBarkerPreambleMd = false;
	MACvDisableBarkerPreambleMd(pDevice->PortOffset);
	pDevice->bNonERPPresent = false;
	pDevice->byPreambleType = 0;
	pDevice->wBasicRate = 0;
	// Set Basic Rate
	CARDbAddBasicRate((void *)pDevice, RATE_1M);
	// calculate TSF offset
	// TSF Offset = Received Timestamp TSF - Marked Local's TSF
	CARDbUpdateTSF(pDevice, pCurr->byRxRate, pCurr->qwBSSTimestamp, pCurr->qwLocalTSF);

	CARDbSetBeaconPeriod(pDevice, pCurr->wBeaconInterval);

	// set Next TBTT
	// Next TBTT = ((local_current_TSF / beacon_interval) + 1) * beacon_interval
	CARDvSetFirstNextTBTT(pDevice->PortOffset, pCurr->wBeaconInterval);

	// set BSSID
	MACvWriteBSSIDAddress(pDevice->PortOffset, pCurr->abyBSSID);

	MACvReadBSSIDAddress(pDevice->PortOffset, pMgmt->abyCurrBSSID);

	DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Sync:set CurrBSSID address = "
		"%pM\n", pMgmt->abyCurrBSSID);

	if (pCurr->eNetworkTypeInUse == PHY_TYPE_11A) {
		if ((pMgmt->eConfigPHYMode == PHY_TYPE_11A) ||
		    (pMgmt->eConfigPHYMode == PHY_TYPE_AUTO)) {
			ePhyType = PHY_TYPE_11A;
		} else {
			return;
		}
	} else if (pCurr->eNetworkTypeInUse == PHY_TYPE_11B) {
		if ((pMgmt->eConfigPHYMode == PHY_TYPE_11B) ||
		    (pMgmt->eConfigPHYMode == PHY_TYPE_11G) ||
		    (pMgmt->eConfigPHYMode == PHY_TYPE_AUTO)) {
			ePhyType = PHY_TYPE_11B;
		} else {
			return;
		}
	} else {
		if ((pMgmt->eConfigPHYMode == PHY_TYPE_11G) ||
		    (pMgmt->eConfigPHYMode == PHY_TYPE_AUTO)) {
			ePhyType = PHY_TYPE_11G;
		} else if (pMgmt->eConfigPHYMode == PHY_TYPE_11B) {
			ePhyType = PHY_TYPE_11B;
		} else {
			return;
		}
	}

	if (ePhyType == PHY_TYPE_11A) {
		memcpy(pMgmt->abyCurrSuppRates, &abyCurrSuppRatesA[0], sizeof(abyCurrSuppRatesA));
		pMgmt->abyCurrExtSuppRates[1] = 0;
	} else if (ePhyType == PHY_TYPE_11B) {
		memcpy(pMgmt->abyCurrSuppRates, &abyCurrSuppRatesB[0], sizeof(abyCurrSuppRatesB));
		pMgmt->abyCurrExtSuppRates[1] = 0;
	} else {
		memcpy(pMgmt->abyCurrSuppRates, &abyCurrSuppRatesG[0], sizeof(abyCurrSuppRatesG));
		memcpy(pMgmt->abyCurrExtSuppRates, &abyCurrExtSuppRatesG[0], sizeof(abyCurrExtSuppRatesG));
	}

	if (WLAN_GET_CAP_INFO_ESS(pCurr->wCapInfo)) {
		CARDbSetBSSID(pMgmt->pAdapter, pCurr->abyBSSID, OP_MODE_INFRASTRUCTURE);
		// Add current BSS to Candidate list
		// This should only works for WPA2 BSS, and WPA2 BSS check must be done before.
		if (pMgmt->eAuthenMode == WMAC_AUTH_WPA2) {
			CARDbAdd_PMKID_Candidate(pMgmt->pAdapter, pMgmt->abyCurrBSSID, pCurr->sRSNCapObj.bRSNCapExist, pCurr->sRSNCapObj.wRSNCap);
		}
	} else {
		CARDbSetBSSID(pMgmt->pAdapter, pCurr->abyBSSID, OP_MODE_ADHOC);
	}

	if (CARDbSetPhyParameter(pMgmt->pAdapter,
				 ePhyType,
				 pCurr->wCapInfo,
				 pCurr->sERP.byERP,
				 pMgmt->abyCurrSuppRates,
				 pMgmt->abyCurrExtSuppRates
		    ) != true) {
		DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "<----s_bSynchBSS Set Phy Mode Fail [%d]\n", ePhyType);
		return;
	}
	// set channel and clear NAV
	if (set_channel(pMgmt->pAdapter, pCurr->uChannel) == false) {
		DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "<----s_bSynchBSS Set Channel [%d]\n", pCurr->uChannel);
		return;
	}

/*
  for (ii=0; ii<BB_VGA_LEVEL; ii++) {
  if (pCurr->ldBmMAX< pDevice->ldBmThreshold[ii]) {
  pDevice->byBBVGANew = pDevice->abyBBVGA[ii];
  break;
  }
  }

  if (pDevice->byBBVGANew != pDevice->byBBVGACurrent) {
  DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "RSSI[%d] NewGain[%d] OldGain[%d] \n",
  (int)pCurr->ldBmMAX, pDevice->byBBVGANew, pDevice->byBBVGACurrent);
  printk("RSSI[%d] NewGain[%d] OldGain[%d] \n",
  (int)pCurr->ldBmMAX, pDevice->byBBVGANew, pDevice->byBBVGACurrent);
  BBvSetVGAGainOffset(pDevice, pDevice->byBBVGANew);
  }
  printk("ldBmMAX[%d] NewGain[%d] OldGain[%d] \n",
  (int)pCurr->ldBmMAX, pDevice->byBBVGANew, pDevice->byBBVGACurrent);
*/
	pMgmt->uCurrChannel = pCurr->uChannel;
	pMgmt->eCurrentPHYMode = ePhyType;
	pMgmt->byERPContext = pCurr->sERP.byERP;
	DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Sync:Set to channel = [%d]\n", (int)pCurr->uChannel);

	*pStatus = CMD_STATUS_SUCCESS;

	return;
};

//mike add: fix NetworkManager 0.7.0 hidden ssid mode in WPA encryption
//                   ,need reset eAuthenMode and eEncryptionStatus
static void  Encyption_Rebuild(
	PSDevice pDevice,
	PKnownBSS pCurr
)
{
	PSMgmtObject  pMgmt = &(pDevice->sMgmtObj);

	if ((pMgmt->eAuthenMode == WMAC_AUTH_WPAPSK) ||           //networkmanager 0.7.0 does not give the pairwise-key selection,
	    (pMgmt->eAuthenMode == WMAC_AUTH_WPA2PSK)) {         // so we need re-select it according to real pairwise-key info.
		if (pCurr->bWPAValid == true)  {   //WPA-PSK
			pMgmt->eAuthenMode = WMAC_AUTH_WPAPSK;
			if (pCurr->abyPKType[0] == WPA_TKIP) {
				pDevice->eEncryptionStatus = Ndis802_11Encryption2Enabled;    //TKIP
				PRINT_K("Encyption_Rebuild--->ssid reset config to [WPAPSK-TKIP]\n");
			} else if (pCurr->abyPKType[0] == WPA_AESCCMP) {
				pDevice->eEncryptionStatus = Ndis802_11Encryption3Enabled;    //AES
				PRINT_K("Encyption_Rebuild--->ssid reset config to [WPAPSK-AES]\n");
			}
		} else if (pCurr->bWPA2Valid == true) {  //WPA2-PSK
			pMgmt->eAuthenMode = WMAC_AUTH_WPA2PSK;
			if (pCurr->abyCSSPK[0] == WLAN_11i_CSS_TKIP) {
				pDevice->eEncryptionStatus = Ndis802_11Encryption2Enabled;     //TKIP
				PRINT_K("Encyption_Rebuild--->ssid reset config to [WPA2PSK-TKIP]\n");
			} else if (pCurr->abyCSSPK[0] == WLAN_11i_CSS_CCMP) {
				pDevice->eEncryptionStatus = Ndis802_11Encryption3Enabled;    //AES
				PRINT_K("Encyption_Rebuild--->ssid reset config to [WPA2PSK-AES]\n");
			}
		}
	}
	//  }
	return;
}

/*+
 *
 * Routine Description:
 *  Format TIM field
 *
 *
 * Return Value:
 *    void
 *
 -*/

static
void
s_vMgrFormatTIM(
	PSMgmtObject pMgmt,
	PWLAN_IE_TIM pTIM
)
{
	unsigned char byMask[8] = {1, 2, 4, 8, 0x10, 0x20, 0x40, 0x80};
	unsigned char byMap;
	unsigned int ii, jj;
	bool bStartFound = false;
	bool bMulticast = false;
	unsigned short wStartIndex = 0;
	unsigned short wEndIndex = 0;

	// Find size of partial virtual bitmap
	for (ii = 0; ii < (MAX_NODE_NUM + 1); ii++) {
		byMap = pMgmt->abyPSTxMap[ii];
		if (!ii) {
			// Mask out the broadcast bit which is indicated separately.
			bMulticast = (byMap & byMask[0]) != 0;
			if (bMulticast) {
				pMgmt->sNodeDBTable[0].bRxPSPoll = true;
			}
			byMap = 0;
		}
		if (byMap) {
			if (!bStartFound) {
				bStartFound = true;
				wStartIndex = ii;
			}
			wEndIndex = ii;
		}
	}

	// Round start index down to nearest even number
	wStartIndex &=  ~BIT0;

	// Round end index up to nearest even number
	wEndIndex = ((wEndIndex + 1) & ~BIT0);

	// Size of element payload

	pTIM->len =  3 + (wEndIndex - wStartIndex) + 1;

	// Fill in the Fixed parts of the TIM
	pTIM->byDTIMCount = pMgmt->byDTIMCount;
	pTIM->byDTIMPeriod = pMgmt->byDTIMPeriod;
	pTIM->byBitMapCtl = (bMulticast ? TIM_MULTICAST_MASK : 0) |
		(((wStartIndex >> 1) << 1) & TIM_BITMAPOFFSET_MASK);

	// Append variable part of TIM

	for (ii = wStartIndex, jj = 0; ii <= wEndIndex; ii++, jj++) {
		pTIM->byVirtBitMap[jj] = pMgmt->abyPSTxMap[ii];
	}

	// Aid = 0 don't used.
	pTIM->byVirtBitMap[0]  &= ~BIT0;
}

/*+
 *
 * Routine Description:
 *  Constructs an Beacon frame(Ad-hoc mode)
 *
 *
 * Return Value:
 *    PTR to frame; or NULL on allocation failure
 *
 -*/

static
PSTxMgmtPacket
s_MgrMakeBeacon(
	PSDevice pDevice,
	PSMgmtObject pMgmt,
	unsigned short wCurrCapInfo,
	unsigned short wCurrBeaconPeriod,
	unsigned int uCurrChannel,
	unsigned short wCurrATIMWinodw,
	PWLAN_IE_SSID pCurrSSID,
	unsigned char *pCurrBSSID,
	PWLAN_IE_SUPP_RATES pCurrSuppRates,
	PWLAN_IE_SUPP_RATES pCurrExtSuppRates
)
{
	PSTxMgmtPacket      pTxPacket = NULL;
	WLAN_FR_BEACON      sFrame;
	unsigned char abyBroadcastAddr[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	unsigned char *pbyBuffer;
	unsigned int uLength = 0;
	PWLAN_IE_IBSS_DFS   pIBSSDFS = NULL;
	unsigned int ii;

	// prepare beacon frame
	pTxPacket = (PSTxMgmtPacket)pMgmt->pbyMgmtPacketPool;
	memset(pTxPacket, 0, sizeof(STxMgmtPacket) + WLAN_BEACON_FR_MAXLEN);
	pTxPacket->p80211Header = (PUWLAN_80211HDR)((unsigned char *)pTxPacket + sizeof(STxMgmtPacket));
	// Setup the sFrame structure.
	sFrame.pBuf = (unsigned char *)pTxPacket->p80211Header;
	sFrame.len = WLAN_BEACON_FR_MAXLEN;
	vMgrEncodeBeacon(&sFrame);
	// Setup the header
	sFrame.pHdr->sA3.wFrameCtl = cpu_to_le16(
		(
			WLAN_SET_FC_FTYPE(WLAN_TYPE_MGR) |
			WLAN_SET_FC_FSTYPE(WLAN_FSTYPE_BEACON)
));

	if (pDevice->bEnablePSMode) {
		sFrame.pHdr->sA3.wFrameCtl |= cpu_to_le16((unsigned short)WLAN_SET_FC_PWRMGT(1));
	}

	memcpy(sFrame.pHdr->sA3.abyAddr1, abyBroadcastAddr, WLAN_ADDR_LEN);
	memcpy(sFrame.pHdr->sA3.abyAddr2, pMgmt->abyMACAddr, WLAN_ADDR_LEN);
	memcpy(sFrame.pHdr->sA3.abyAddr3, pCurrBSSID, WLAN_BSSID_LEN);
	*sFrame.pwBeaconInterval = cpu_to_le16(wCurrBeaconPeriod);
	*sFrame.pwCapInfo = cpu_to_le16(wCurrCapInfo);
	// Copy SSID
	sFrame.pSSID = (PWLAN_IE_SSID)(sFrame.pBuf + sFrame.len);
	sFrame.len += ((PWLAN_IE_SSID)pMgmt->abyCurrSSID)->len + WLAN_IEHDR_LEN;
	memcpy(sFrame.pSSID,
	       pCurrSSID,
	       ((PWLAN_IE_SSID)pCurrSSID)->len + WLAN_IEHDR_LEN
);
	// Copy the rate set
	sFrame.pSuppRates = (PWLAN_IE_SUPP_RATES)(sFrame.pBuf + sFrame.len);
	sFrame.len += ((PWLAN_IE_SUPP_RATES)pCurrSuppRates)->len + WLAN_IEHDR_LEN;
	memcpy(sFrame.pSuppRates,
	       pCurrSuppRates,
	       ((PWLAN_IE_SUPP_RATES)pCurrSuppRates)->len + WLAN_IEHDR_LEN
);
	// DS parameter
	if (pDevice->eCurrentPHYType != PHY_TYPE_11A) {
		sFrame.pDSParms = (PWLAN_IE_DS_PARMS)(sFrame.pBuf + sFrame.len);
		sFrame.len += (1) + WLAN_IEHDR_LEN;
		sFrame.pDSParms->byElementID = WLAN_EID_DS_PARMS;
		sFrame.pDSParms->len = 1;
		sFrame.pDSParms->byCurrChannel = (unsigned char)uCurrChannel;
	}
	// TIM field
	if (pMgmt->eCurrMode == WMAC_MODE_ESS_AP) {
		sFrame.pTIM = (PWLAN_IE_TIM)(sFrame.pBuf + sFrame.len);
		sFrame.pTIM->byElementID = WLAN_EID_TIM;
		s_vMgrFormatTIM(pMgmt, sFrame.pTIM);
		sFrame.len += (WLAN_IEHDR_LEN + sFrame.pTIM->len);
	}

	if (pMgmt->eCurrMode == WMAC_MODE_IBSS_STA) {
		// IBSS parameter
		sFrame.pIBSSParms = (PWLAN_IE_IBSS_PARMS)(sFrame.pBuf + sFrame.len);
		sFrame.len += (2) + WLAN_IEHDR_LEN;
		sFrame.pIBSSParms->byElementID = WLAN_EID_IBSS_PARMS;
		sFrame.pIBSSParms->len = 2;
		sFrame.pIBSSParms->wATIMWindow = wCurrATIMWinodw;
		if (pMgmt->eAuthenMode == WMAC_AUTH_WPANONE) {
			/* RSN parameter */
			sFrame.pRSNWPA = (PWLAN_IE_RSN_EXT)(sFrame.pBuf + sFrame.len);
			sFrame.pRSNWPA->byElementID = WLAN_EID_RSN_WPA;
			sFrame.pRSNWPA->len = 12;
			sFrame.pRSNWPA->abyOUI[0] = 0x00;
			sFrame.pRSNWPA->abyOUI[1] = 0x50;
			sFrame.pRSNWPA->abyOUI[2] = 0xf2;
			sFrame.pRSNWPA->abyOUI[3] = 0x01;
			sFrame.pRSNWPA->wVersion = 1;
			sFrame.pRSNWPA->abyMulticast[0] = 0x00;
			sFrame.pRSNWPA->abyMulticast[1] = 0x50;
			sFrame.pRSNWPA->abyMulticast[2] = 0xf2;
			if (pDevice->eEncryptionStatus == Ndis802_11Encryption3Enabled)
				sFrame.pRSNWPA->abyMulticast[3] = 0x04;//AES
			else if (pDevice->eEncryptionStatus == Ndis802_11Encryption2Enabled)
				sFrame.pRSNWPA->abyMulticast[3] = 0x02;//TKIP
			else if (pDevice->eEncryptionStatus == Ndis802_11Encryption1Enabled)
				sFrame.pRSNWPA->abyMulticast[3] = 0x01;//WEP40
			else
				sFrame.pRSNWPA->abyMulticast[3] = 0x00;//NONE

			// Pairwise Key Cipher Suite
			sFrame.pRSNWPA->wPKCount = 0;
			// Auth Key Management Suite
			*((unsigned short *)(sFrame.pBuf + sFrame.len + sFrame.pRSNWPA->len)) = 0;
			sFrame.pRSNWPA->len += 2;

			// RSN Capabilities
			*((unsigned short *)(sFrame.pBuf + sFrame.len + sFrame.pRSNWPA->len)) = 0;
			sFrame.pRSNWPA->len += 2;
			sFrame.len += sFrame.pRSNWPA->len + WLAN_IEHDR_LEN;
		}
	}

	if ((pMgmt->b11hEnable == true) &&
	    (pMgmt->eCurrentPHYMode == PHY_TYPE_11A)) {
		// Country IE
		pbyBuffer = (unsigned char *)(sFrame.pBuf + sFrame.len);
		set_country_IE(pMgmt->pAdapter, pbyBuffer);
		set_country_info(pMgmt->pAdapter, PHY_TYPE_11A, pbyBuffer);
		uLength += ((PWLAN_IE_COUNTRY) pbyBuffer)->len + WLAN_IEHDR_LEN;
		pbyBuffer += (((PWLAN_IE_COUNTRY) pbyBuffer)->len + WLAN_IEHDR_LEN);
		// Power Constrain IE
		((PWLAN_IE_PW_CONST) pbyBuffer)->byElementID = WLAN_EID_PWR_CONSTRAINT;
		((PWLAN_IE_PW_CONST) pbyBuffer)->len = 1;
		((PWLAN_IE_PW_CONST) pbyBuffer)->byPower = 0;
		pbyBuffer += (1) + WLAN_IEHDR_LEN;
		uLength += (1) + WLAN_IEHDR_LEN;
		if (pMgmt->bSwitchChannel == true) {
			// Channel Switch IE
			((PWLAN_IE_CH_SW) pbyBuffer)->byElementID = WLAN_EID_CH_SWITCH;
			((PWLAN_IE_CH_SW) pbyBuffer)->len = 3;
			((PWLAN_IE_CH_SW) pbyBuffer)->byMode = 1;
			((PWLAN_IE_CH_SW) pbyBuffer)->byChannel = get_channel_number(pMgmt->pAdapter, pMgmt->byNewChannel);
			((PWLAN_IE_CH_SW) pbyBuffer)->byCount = 0;
			pbyBuffer += (3) + WLAN_IEHDR_LEN;
			uLength += (3) + WLAN_IEHDR_LEN;
		}
		// TPC report
		((PWLAN_IE_TPC_REP) pbyBuffer)->byElementID = WLAN_EID_TPC_REP;
		((PWLAN_IE_TPC_REP) pbyBuffer)->len = 2;
		((PWLAN_IE_TPC_REP) pbyBuffer)->byTxPower = CARDbyGetTransmitPower(pMgmt->pAdapter);
		((PWLAN_IE_TPC_REP) pbyBuffer)->byLinkMargin = 0;
		pbyBuffer += (2) + WLAN_IEHDR_LEN;
		uLength += (2) + WLAN_IEHDR_LEN;
		// IBSS DFS
		if (pMgmt->eCurrMode != WMAC_MODE_ESS_AP) {
			pIBSSDFS = (PWLAN_IE_IBSS_DFS) pbyBuffer;
			pIBSSDFS->byElementID = WLAN_EID_IBSS_DFS;
			pIBSSDFS->len = 7;
			memcpy(pIBSSDFS->abyDFSOwner,
			       pMgmt->abyIBSSDFSOwner,
			       6);
			pIBSSDFS->byDFSRecovery = pMgmt->byIBSSDFSRecovery;
			pbyBuffer += (7) + WLAN_IEHDR_LEN;
			uLength += (7) + WLAN_IEHDR_LEN;
			for (ii = CB_MAX_CHANNEL_24G+1; ii <= CB_MAX_CHANNEL; ii++) {
				if (get_channel_map_info(pMgmt->pAdapter, ii, pbyBuffer, pbyBuffer+1) == true) {
					pbyBuffer += 2;
					uLength += 2;
					pIBSSDFS->len += 2;
				}
			}
		}
		sFrame.len += uLength;
	}

	if (pMgmt->eCurrentPHYMode == PHY_TYPE_11G) {
		sFrame.pERP = (PWLAN_IE_ERP)(sFrame.pBuf + sFrame.len);
		sFrame.len += 1 + WLAN_IEHDR_LEN;
		sFrame.pERP->byElementID = WLAN_EID_ERP;
		sFrame.pERP->len = 1;
		sFrame.pERP->byContext = 0;
		if (pDevice->bProtectMode == true)
			sFrame.pERP->byContext |= WLAN_EID_ERP_USE_PROTECTION;
		if (pDevice->bNonERPPresent == true)
			sFrame.pERP->byContext |= WLAN_EID_ERP_NONERP_PRESENT;
		if (pDevice->bBarkerPreambleMd == true)
			sFrame.pERP->byContext |= WLAN_EID_ERP_BARKER_MODE;
	}
	if (((PWLAN_IE_SUPP_RATES)pCurrExtSuppRates)->len != 0) {
		sFrame.pExtSuppRates = (PWLAN_IE_SUPP_RATES)(sFrame.pBuf + sFrame.len);
		sFrame.len += ((PWLAN_IE_SUPP_RATES)pCurrExtSuppRates)->len + WLAN_IEHDR_LEN;
		memcpy(sFrame.pExtSuppRates,
		       pCurrExtSuppRates,
		       ((PWLAN_IE_SUPP_RATES)pCurrExtSuppRates)->len + WLAN_IEHDR_LEN
);
	}
	// hostapd wpa/wpa2 IE
	if ((pMgmt->eCurrMode == WMAC_MODE_ESS_AP) && (pDevice->bEnableHostapd == true)) {
		if (pMgmt->eAuthenMode == WMAC_AUTH_WPANONE) {
			if (pMgmt->wWPAIELen != 0) {
				sFrame.pRSN = (PWLAN_IE_RSN)(sFrame.pBuf + sFrame.len);
				memcpy(sFrame.pRSN, pMgmt->abyWPAIE, pMgmt->wWPAIELen);
				sFrame.len += pMgmt->wWPAIELen;
			}
		}
	}

	/* Adjust the length fields */
	pTxPacket->cbMPDULen = sFrame.len;
	pTxPacket->cbPayloadLen = sFrame.len - WLAN_HDR_ADDR3_LEN;

	return pTxPacket;
}

/*+
 *
 * Routine Description:
 *  Constructs an Prob-response frame
 *
 *
 * Return Value:
 *    PTR to frame; or NULL on allocation failure
 *
 -*/

PSTxMgmtPacket
s_MgrMakeProbeResponse(
	PSDevice pDevice,
	PSMgmtObject pMgmt,
	unsigned short wCurrCapInfo,
	unsigned short wCurrBeaconPeriod,
	unsigned int uCurrChannel,
	unsigned short wCurrATIMWinodw,
	unsigned char *pDstAddr,
	PWLAN_IE_SSID pCurrSSID,
	unsigned char *pCurrBSSID,
	PWLAN_IE_SUPP_RATES pCurrSuppRates,
	PWLAN_IE_SUPP_RATES pCurrExtSuppRates,
	unsigned char byPHYType
)
{
	PSTxMgmtPacket      pTxPacket = NULL;
	WLAN_FR_PROBERESP   sFrame;
	unsigned char *pbyBuffer;
	unsigned int uLength = 0;
	PWLAN_IE_IBSS_DFS   pIBSSDFS = NULL;
	unsigned int ii;

	pTxPacket = (PSTxMgmtPacket)pMgmt->pbyMgmtPacketPool;
	memset(pTxPacket, 0, sizeof(STxMgmtPacket) + WLAN_PROBERESP_FR_MAXLEN);
	pTxPacket->p80211Header = (PUWLAN_80211HDR)((unsigned char *)pTxPacket + sizeof(STxMgmtPacket));
	// Setup the sFrame structure.
	sFrame.pBuf = (unsigned char *)pTxPacket->p80211Header;
	sFrame.len = WLAN_PROBERESP_FR_MAXLEN;
	vMgrEncodeProbeResponse(&sFrame);
	// Setup the header
	sFrame.pHdr->sA3.wFrameCtl = cpu_to_le16(
		(
			WLAN_SET_FC_FTYPE(WLAN_TYPE_MGR) |
			WLAN_SET_FC_FSTYPE(WLAN_FSTYPE_PROBERESP)
));
	memcpy(sFrame.pHdr->sA3.abyAddr1, pDstAddr, WLAN_ADDR_LEN);
	memcpy(sFrame.pHdr->sA3.abyAddr2, pMgmt->abyMACAddr, WLAN_ADDR_LEN);
	memcpy(sFrame.pHdr->sA3.abyAddr3, pCurrBSSID, WLAN_BSSID_LEN);
	*sFrame.pwBeaconInterval = cpu_to_le16(wCurrBeaconPeriod);
	*sFrame.pwCapInfo = cpu_to_le16(wCurrCapInfo);

	if (byPHYType == BB_TYPE_11B) {
		*sFrame.pwCapInfo &= cpu_to_le16((unsigned short)~(WLAN_SET_CAP_INFO_SHORTSLOTTIME(1)));
	}

	// Copy SSID
	sFrame.pSSID = (PWLAN_IE_SSID)(sFrame.pBuf + sFrame.len);
	sFrame.len += ((PWLAN_IE_SSID)pMgmt->abyCurrSSID)->len + WLAN_IEHDR_LEN;
	memcpy(sFrame.pSSID,
	       pCurrSSID,
	       ((PWLAN_IE_SSID)pCurrSSID)->len + WLAN_IEHDR_LEN
);
	// Copy the rate set
	sFrame.pSuppRates = (PWLAN_IE_SUPP_RATES)(sFrame.pBuf + sFrame.len);

	sFrame.len += ((PWLAN_IE_SUPP_RATES)pCurrSuppRates)->len + WLAN_IEHDR_LEN;
	memcpy(sFrame.pSuppRates,
	       pCurrSuppRates,
	       ((PWLAN_IE_SUPP_RATES)pCurrSuppRates)->len + WLAN_IEHDR_LEN
);

	// DS parameter
	if (pDevice->eCurrentPHYType != PHY_TYPE_11A) {
		sFrame.pDSParms = (PWLAN_IE_DS_PARMS)(sFrame.pBuf + sFrame.len);
		sFrame.len += (1) + WLAN_IEHDR_LEN;
		sFrame.pDSParms->byElementID = WLAN_EID_DS_PARMS;
		sFrame.pDSParms->len = 1;
		sFrame.pDSParms->byCurrChannel = (unsigned char)uCurrChannel;
	}

	if (pMgmt->eCurrMode != WMAC_MODE_ESS_AP) {
		// IBSS parameter
		sFrame.pIBSSParms = (PWLAN_IE_IBSS_PARMS)(sFrame.pBuf + sFrame.len);
		sFrame.len += (2) + WLAN_IEHDR_LEN;
		sFrame.pIBSSParms->byElementID = WLAN_EID_IBSS_PARMS;
		sFrame.pIBSSParms->len = 2;
		sFrame.pIBSSParms->wATIMWindow = 0;
	}
	if (pDevice->eCurrentPHYType == PHY_TYPE_11G) {
		sFrame.pERP = (PWLAN_IE_ERP)(sFrame.pBuf + sFrame.len);
		sFrame.len += 1 + WLAN_IEHDR_LEN;
		sFrame.pERP->byElementID = WLAN_EID_ERP;
		sFrame.pERP->len = 1;
		sFrame.pERP->byContext = 0;
		if (pDevice->bProtectMode == true)
			sFrame.pERP->byContext |= WLAN_EID_ERP_USE_PROTECTION;
		if (pDevice->bNonERPPresent == true)
			sFrame.pERP->byContext |= WLAN_EID_ERP_NONERP_PRESENT;
		if (pDevice->bBarkerPreambleMd == true)
			sFrame.pERP->byContext |= WLAN_EID_ERP_BARKER_MODE;
	}

	if ((pMgmt->b11hEnable == true) &&
	    (pMgmt->eCurrentPHYMode == PHY_TYPE_11A)) {
		// Country IE
		pbyBuffer = (unsigned char *)(sFrame.pBuf + sFrame.len);
		set_country_IE(pMgmt->pAdapter, pbyBuffer);
		set_country_info(pMgmt->pAdapter, PHY_TYPE_11A, pbyBuffer);
		uLength += ((PWLAN_IE_COUNTRY) pbyBuffer)->len + WLAN_IEHDR_LEN;
		pbyBuffer += (((PWLAN_IE_COUNTRY) pbyBuffer)->len + WLAN_IEHDR_LEN);
		// Power Constrain IE
		((PWLAN_IE_PW_CONST) pbyBuffer)->byElementID = WLAN_EID_PWR_CONSTRAINT;
		((PWLAN_IE_PW_CONST) pbyBuffer)->len = 1;
		((PWLAN_IE_PW_CONST) pbyBuffer)->byPower = 0;
		pbyBuffer += (1) + WLAN_IEHDR_LEN;
		uLength += (1) + WLAN_IEHDR_LEN;
		if (pMgmt->bSwitchChannel == true) {
			// Channel Switch IE
			((PWLAN_IE_CH_SW) pbyBuffer)->byElementID = WLAN_EID_CH_SWITCH;
			((PWLAN_IE_CH_SW) pbyBuffer)->len = 3;
			((PWLAN_IE_CH_SW) pbyBuffer)->byMode = 1;
			((PWLAN_IE_CH_SW) pbyBuffer)->byChannel = get_channel_number(pMgmt->pAdapter, pMgmt->byNewChannel);
			((PWLAN_IE_CH_SW) pbyBuffer)->byCount = 0;
			pbyBuffer += (3) + WLAN_IEHDR_LEN;
			uLength += (3) + WLAN_IEHDR_LEN;
		}
		// TPC report
		((PWLAN_IE_TPC_REP) pbyBuffer)->byElementID = WLAN_EID_TPC_REP;
		((PWLAN_IE_TPC_REP) pbyBuffer)->len = 2;
		((PWLAN_IE_TPC_REP) pbyBuffer)->byTxPower = CARDbyGetTransmitPower(pMgmt->pAdapter);
		((PWLAN_IE_TPC_REP) pbyBuffer)->byLinkMargin = 0;
		pbyBuffer += (2) + WLAN_IEHDR_LEN;
		uLength += (2) + WLAN_IEHDR_LEN;
		// IBSS DFS
		if (pMgmt->eCurrMode != WMAC_MODE_ESS_AP) {
			pIBSSDFS = (PWLAN_IE_IBSS_DFS) pbyBuffer;
			pIBSSDFS->byElementID = WLAN_EID_IBSS_DFS;
			pIBSSDFS->len = 7;
			memcpy(pIBSSDFS->abyDFSOwner,
			       pMgmt->abyIBSSDFSOwner,
			       6);
			pIBSSDFS->byDFSRecovery = pMgmt->byIBSSDFSRecovery;
			pbyBuffer += (7) + WLAN_IEHDR_LEN;
			uLength += (7) + WLAN_IEHDR_LEN;
			for (ii = CB_MAX_CHANNEL_24G + 1; ii <= CB_MAX_CHANNEL; ii++) {
				if (get_channel_map_info(pMgmt->pAdapter, ii, pbyBuffer, pbyBuffer+1) == true) {
					pbyBuffer += 2;
					uLength += 2;
					pIBSSDFS->len += 2;
				}
			}
		}
		sFrame.len += uLength;
	}

	if (((PWLAN_IE_SUPP_RATES)pCurrExtSuppRates)->len != 0) {
		sFrame.pExtSuppRates = (PWLAN_IE_SUPP_RATES)(sFrame.pBuf + sFrame.len);
		sFrame.len += ((PWLAN_IE_SUPP_RATES)pCurrExtSuppRates)->len + WLAN_IEHDR_LEN;
		memcpy(sFrame.pExtSuppRates,
		       pCurrExtSuppRates,
		       ((PWLAN_IE_SUPP_RATES)pCurrExtSuppRates)->len + WLAN_IEHDR_LEN
);
	}

	// hostapd wpa/wpa2 IE
	if ((pMgmt->eCurrMode == WMAC_MODE_ESS_AP) && (pDevice->bEnableHostapd == true)) {
		if (pMgmt->eAuthenMode == WMAC_AUTH_WPANONE) {
			if (pMgmt->wWPAIELen != 0) {
				sFrame.pRSN = (PWLAN_IE_RSN)(sFrame.pBuf + sFrame.len);
				memcpy(sFrame.pRSN, pMgmt->abyWPAIE, pMgmt->wWPAIELen);
				sFrame.len += pMgmt->wWPAIELen;
			}
		}
	}

	// Adjust the length fields
	pTxPacket->cbMPDULen = sFrame.len;
	pTxPacket->cbPayloadLen = sFrame.len - WLAN_HDR_ADDR3_LEN;

	return pTxPacket;
}

/*+
 *
 * Routine Description:
 *  Constructs an association request frame
 *
 *
 * Return Value:
 *    A ptr to frame or NULL on allocation failure
 *
 -*/

PSTxMgmtPacket
s_MgrMakeAssocRequest(
	PSDevice pDevice,
	PSMgmtObject pMgmt,
	unsigned char *pDAddr,
	unsigned short wCurrCapInfo,
	unsigned short wListenInterval,
	PWLAN_IE_SSID pCurrSSID,
	PWLAN_IE_SUPP_RATES pCurrRates,
	PWLAN_IE_SUPP_RATES pCurrExtSuppRates
)
{
	PSTxMgmtPacket      pTxPacket = NULL;
	WLAN_FR_ASSOCREQ    sFrame;
	unsigned char *pbyIEs;
	unsigned char *pbyRSN;

	pTxPacket = (PSTxMgmtPacket)pMgmt->pbyMgmtPacketPool;
	memset(pTxPacket, 0, sizeof(STxMgmtPacket) + WLAN_ASSOCREQ_FR_MAXLEN);
	pTxPacket->p80211Header = (PUWLAN_80211HDR)((unsigned char *)pTxPacket + sizeof(STxMgmtPacket));
	// Setup the sFrame structure.
	sFrame.pBuf = (unsigned char *)pTxPacket->p80211Header;
	sFrame.len = WLAN_ASSOCREQ_FR_MAXLEN;
	// format fixed field frame structure
	vMgrEncodeAssocRequest(&sFrame);
	// Setup the header
	sFrame.pHdr->sA3.wFrameCtl = cpu_to_le16(
		(
			WLAN_SET_FC_FTYPE(WLAN_TYPE_MGR) |
			WLAN_SET_FC_FSTYPE(WLAN_FSTYPE_ASSOCREQ)
));
	memcpy(sFrame.pHdr->sA3.abyAddr1, pDAddr, WLAN_ADDR_LEN);
	memcpy(sFrame.pHdr->sA3.abyAddr2, pMgmt->abyMACAddr, WLAN_ADDR_LEN);
	memcpy(sFrame.pHdr->sA3.abyAddr3, pMgmt->abyCurrBSSID, WLAN_BSSID_LEN);

	// Set the capability and listen interval
	*(sFrame.pwCapInfo) = cpu_to_le16(wCurrCapInfo);
	*(sFrame.pwListenInterval) = cpu_to_le16(wListenInterval);

	// sFrame.len point to end of fixed field
	sFrame.pSSID = (PWLAN_IE_SSID)(sFrame.pBuf + sFrame.len);
	sFrame.len += pCurrSSID->len + WLAN_IEHDR_LEN;
	memcpy(sFrame.pSSID, pCurrSSID, pCurrSSID->len + WLAN_IEHDR_LEN);

	pMgmt->sAssocInfo.AssocInfo.RequestIELength = pCurrSSID->len + WLAN_IEHDR_LEN;
	pMgmt->sAssocInfo.AssocInfo.OffsetRequestIEs = sizeof(NDIS_802_11_ASSOCIATION_INFORMATION);
	pbyIEs = pMgmt->sAssocInfo.abyIEs;
	memcpy(pbyIEs, pCurrSSID, pCurrSSID->len + WLAN_IEHDR_LEN);
	pbyIEs += pCurrSSID->len + WLAN_IEHDR_LEN;

	// Copy the rate set
	sFrame.pSuppRates = (PWLAN_IE_SUPP_RATES)(sFrame.pBuf + sFrame.len);
	if ((pDevice->eCurrentPHYType == PHY_TYPE_11B) && (pCurrRates->len > 4))
		sFrame.len += 4 + WLAN_IEHDR_LEN;
	else
		sFrame.len += pCurrRates->len + WLAN_IEHDR_LEN;
	memcpy(sFrame.pSuppRates, pCurrRates, pCurrRates->len + WLAN_IEHDR_LEN);

	// Copy the extension rate set
	if ((pDevice->eCurrentPHYType == PHY_TYPE_11G) && (pCurrExtSuppRates->len > 0)) {
		sFrame.pExtSuppRates = (PWLAN_IE_SUPP_RATES)(sFrame.pBuf + sFrame.len);
		sFrame.len += pCurrExtSuppRates->len + WLAN_IEHDR_LEN;
		memcpy(sFrame.pExtSuppRates, pCurrExtSuppRates, pCurrExtSuppRates->len + WLAN_IEHDR_LEN);
	}

	pMgmt->sAssocInfo.AssocInfo.RequestIELength += pCurrRates->len + WLAN_IEHDR_LEN;
	memcpy(pbyIEs, pCurrRates, pCurrRates->len + WLAN_IEHDR_LEN);
	pbyIEs += pCurrRates->len + WLAN_IEHDR_LEN;

	// for 802.11h
	if (pMgmt->b11hEnable == true) {
		if (sFrame.pCurrPowerCap == NULL) {
			sFrame.pCurrPowerCap = (PWLAN_IE_PW_CAP)(sFrame.pBuf + sFrame.len);
			sFrame.len += (2 + WLAN_IEHDR_LEN);
			sFrame.pCurrPowerCap->byElementID = WLAN_EID_PWR_CAPABILITY;
			sFrame.pCurrPowerCap->len = 2;
			CARDvGetPowerCapability(pMgmt->pAdapter,
						&(sFrame.pCurrPowerCap->byMinPower),
						&(sFrame.pCurrPowerCap->byMaxPower)
);
		}
		if (sFrame.pCurrSuppCh == NULL) {
			sFrame.pCurrSuppCh = (PWLAN_IE_SUPP_CH)(sFrame.pBuf + sFrame.len);
			sFrame.len += set_support_channels(pMgmt->pAdapter, (unsigned char *)sFrame.pCurrSuppCh);
		}
	}

	if (((pMgmt->eAuthenMode == WMAC_AUTH_WPA) ||
	     (pMgmt->eAuthenMode == WMAC_AUTH_WPAPSK) ||
	     (pMgmt->eAuthenMode == WMAC_AUTH_WPANONE)) &&
	    (pMgmt->pCurrBSS != NULL)) {
		/* WPA IE */
		sFrame.pRSNWPA = (PWLAN_IE_RSN_EXT)(sFrame.pBuf + sFrame.len);
		sFrame.pRSNWPA->byElementID = WLAN_EID_RSN_WPA;
		sFrame.pRSNWPA->len = 16;
		sFrame.pRSNWPA->abyOUI[0] = 0x00;
		sFrame.pRSNWPA->abyOUI[1] = 0x50;
		sFrame.pRSNWPA->abyOUI[2] = 0xf2;
		sFrame.pRSNWPA->abyOUI[3] = 0x01;
		sFrame.pRSNWPA->wVersion = 1;
		//Group Key Cipher Suite
		sFrame.pRSNWPA->abyMulticast[0] = 0x00;
		sFrame.pRSNWPA->abyMulticast[1] = 0x50;
		sFrame.pRSNWPA->abyMulticast[2] = 0xf2;
		if (pMgmt->byCSSGK == KEY_CTL_WEP) {
			sFrame.pRSNWPA->abyMulticast[3] = pMgmt->pCurrBSS->byGKType;
		} else if (pMgmt->byCSSGK == KEY_CTL_TKIP) {
			sFrame.pRSNWPA->abyMulticast[3] = WPA_TKIP;
		} else if (pMgmt->byCSSGK == KEY_CTL_CCMP) {
			sFrame.pRSNWPA->abyMulticast[3] = WPA_AESCCMP;
		} else {
			sFrame.pRSNWPA->abyMulticast[3] = WPA_NONE;
		}
		// Pairwise Key Cipher Suite
		sFrame.pRSNWPA->wPKCount = 1;
		sFrame.pRSNWPA->PKSList[0].abyOUI[0] = 0x00;
		sFrame.pRSNWPA->PKSList[0].abyOUI[1] = 0x50;
		sFrame.pRSNWPA->PKSList[0].abyOUI[2] = 0xf2;
		if (pMgmt->byCSSPK == KEY_CTL_TKIP) {
			sFrame.pRSNWPA->PKSList[0].abyOUI[3] = WPA_TKIP;
		} else if (pMgmt->byCSSPK == KEY_CTL_CCMP) {
			sFrame.pRSNWPA->PKSList[0].abyOUI[3] = WPA_AESCCMP;
		} else {
			sFrame.pRSNWPA->PKSList[0].abyOUI[3] = WPA_NONE;
		}
		// Auth Key Management Suite
		pbyRSN = (unsigned char *)(sFrame.pBuf + sFrame.len + 2 + sFrame.pRSNWPA->len);
		*pbyRSN++ = 0x01;
		*pbyRSN++ = 0x00;
		*pbyRSN++ = 0x00;

		*pbyRSN++ = 0x50;
		*pbyRSN++ = 0xf2;
		if (pMgmt->eAuthenMode == WMAC_AUTH_WPAPSK) {
			*pbyRSN++ = WPA_AUTH_PSK;
		} else if (pMgmt->eAuthenMode == WMAC_AUTH_WPA) {
			*pbyRSN++ = WPA_AUTH_IEEE802_1X;
		} else {
			*pbyRSN++ = WPA_NONE;
		}

		sFrame.pRSNWPA->len += 6;

		// RSN Capabilities

		*pbyRSN++ = 0x00;
		*pbyRSN++ = 0x00;
		sFrame.pRSNWPA->len += 2;

		sFrame.len += sFrame.pRSNWPA->len + WLAN_IEHDR_LEN;
		// copy to AssocInfo. for OID_802_11_ASSOCIATION_INFORMATION
		pMgmt->sAssocInfo.AssocInfo.RequestIELength += sFrame.pRSNWPA->len + WLAN_IEHDR_LEN;
		memcpy(pbyIEs, sFrame.pRSNWPA, sFrame.pRSNWPA->len + WLAN_IEHDR_LEN);
		pbyIEs += sFrame.pRSNWPA->len + WLAN_IEHDR_LEN;

	} else if (((pMgmt->eAuthenMode == WMAC_AUTH_WPA2) ||
		    (pMgmt->eAuthenMode == WMAC_AUTH_WPA2PSK)) &&
		   (pMgmt->pCurrBSS != NULL)) {
		unsigned int ii;
		unsigned short *pwPMKID;

		// WPA IE
		sFrame.pRSN = (PWLAN_IE_RSN)(sFrame.pBuf + sFrame.len);
		sFrame.pRSN->byElementID = WLAN_EID_RSN;
		sFrame.pRSN->len = 6; //Version(2)+GK(4)
		sFrame.pRSN->wVersion = 1;
		//Group Key Cipher Suite
		sFrame.pRSN->abyRSN[0] = 0x00;
		sFrame.pRSN->abyRSN[1] = 0x0F;
		sFrame.pRSN->abyRSN[2] = 0xAC;
		if (pMgmt->byCSSGK == KEY_CTL_WEP) {
			sFrame.pRSN->abyRSN[3] = pMgmt->pCurrBSS->byCSSGK;
		} else if (pMgmt->byCSSGK == KEY_CTL_TKIP) {
			sFrame.pRSN->abyRSN[3] = WLAN_11i_CSS_TKIP;
		} else if (pMgmt->byCSSGK == KEY_CTL_CCMP) {
			sFrame.pRSN->abyRSN[3] = WLAN_11i_CSS_CCMP;
		} else {
			sFrame.pRSN->abyRSN[3] = WLAN_11i_CSS_UNKNOWN;
		}

		// Pairwise Key Cipher Suite
		sFrame.pRSN->abyRSN[4] = 1;
		sFrame.pRSN->abyRSN[5] = 0;
		sFrame.pRSN->abyRSN[6] = 0x00;
		sFrame.pRSN->abyRSN[7] = 0x0F;
		sFrame.pRSN->abyRSN[8] = 0xAC;
		if (pMgmt->byCSSPK == KEY_CTL_TKIP) {
			sFrame.pRSN->abyRSN[9] = WLAN_11i_CSS_TKIP;
		} else if (pMgmt->byCSSPK == KEY_CTL_CCMP) {
			sFrame.pRSN->abyRSN[9] = WLAN_11i_CSS_CCMP;
		} else if (pMgmt->byCSSPK == KEY_CTL_NONE) {
			sFrame.pRSN->abyRSN[9] = WLAN_11i_CSS_USE_GROUP;
		} else {
			sFrame.pRSN->abyRSN[9] = WLAN_11i_CSS_UNKNOWN;
		}
		sFrame.pRSN->len += 6;

		// Auth Key Management Suite
		sFrame.pRSN->abyRSN[10] = 1;
		sFrame.pRSN->abyRSN[11] = 0;
		sFrame.pRSN->abyRSN[12] = 0x00;
		sFrame.pRSN->abyRSN[13] = 0x0F;
		sFrame.pRSN->abyRSN[14] = 0xAC;
		if (pMgmt->eAuthenMode == WMAC_AUTH_WPA2PSK) {
			sFrame.pRSN->abyRSN[15] = WLAN_11i_AKMSS_PSK;
		} else if (pMgmt->eAuthenMode == WMAC_AUTH_WPA2) {
			sFrame.pRSN->abyRSN[15] = WLAN_11i_AKMSS_802_1X;
		} else {
			sFrame.pRSN->abyRSN[15] = WLAN_11i_AKMSS_UNKNOWN;
		}
		sFrame.pRSN->len += 6;

		// RSN Capabilities
		if (pMgmt->pCurrBSS->sRSNCapObj.bRSNCapExist == true) {
			memcpy(&sFrame.pRSN->abyRSN[16], &pMgmt->pCurrBSS->sRSNCapObj.wRSNCap, 2);
		} else {
			sFrame.pRSN->abyRSN[16] = 0;
			sFrame.pRSN->abyRSN[17] = 0;
		}
		sFrame.pRSN->len += 2;

		if ((pDevice->gsPMKID.BSSIDInfoCount > 0) && (pDevice->bRoaming == true) && (pMgmt->eAuthenMode == WMAC_AUTH_WPA2)) {
			// RSN PMKID
			pbyRSN = &sFrame.pRSN->abyRSN[18];
			pwPMKID = (unsigned short *)pbyRSN; // Point to PMKID count
			*pwPMKID = 0;            // Initialize PMKID count
			pbyRSN += 2;             // Point to PMKID list
			for (ii = 0; ii < pDevice->gsPMKID.BSSIDInfoCount; ii++) {
				if (!memcmp(&pDevice->gsPMKID.BSSIDInfo[ii].BSSID[0], pMgmt->abyCurrBSSID, ETH_ALEN)) {
					(*pwPMKID)++;
					memcpy(pbyRSN, pDevice->gsPMKID.BSSIDInfo[ii].PMKID, 16);
					pbyRSN += 16;
				}
			}
			if (*pwPMKID != 0) {
				sFrame.pRSN->len += (2 + (*pwPMKID)*16);
			}
		}

		sFrame.len += sFrame.pRSN->len + WLAN_IEHDR_LEN;
		// copy to AssocInfo. for OID_802_11_ASSOCIATION_INFORMATION
		pMgmt->sAssocInfo.AssocInfo.RequestIELength += sFrame.pRSN->len + WLAN_IEHDR_LEN;
		memcpy(pbyIEs, sFrame.pRSN, sFrame.pRSN->len + WLAN_IEHDR_LEN);
		pbyIEs += sFrame.pRSN->len + WLAN_IEHDR_LEN;
	}

	// Adjust the length fields
	pTxPacket->cbMPDULen = sFrame.len;
	pTxPacket->cbPayloadLen = sFrame.len - WLAN_HDR_ADDR3_LEN;
	return pTxPacket;
}

/*+
 *
 * Routine Description:
 *  Constructs an re-association request frame
 *
 *
 * Return Value:
 *    A ptr to frame or NULL on allocation failure
 *
 -*/

PSTxMgmtPacket
s_MgrMakeReAssocRequest(
	PSDevice pDevice,
	PSMgmtObject pMgmt,
	unsigned char *pDAddr,
	unsigned short wCurrCapInfo,
	unsigned short wListenInterval,
	PWLAN_IE_SSID pCurrSSID,
	PWLAN_IE_SUPP_RATES pCurrRates,
	PWLAN_IE_SUPP_RATES pCurrExtSuppRates
)
{
	PSTxMgmtPacket      pTxPacket = NULL;
	WLAN_FR_REASSOCREQ  sFrame;
	unsigned char *pbyIEs;
	unsigned char *pbyRSN;

	pTxPacket = (PSTxMgmtPacket)pMgmt->pbyMgmtPacketPool;
	memset(pTxPacket, 0, sizeof(STxMgmtPacket) + WLAN_REASSOCREQ_FR_MAXLEN);
	pTxPacket->p80211Header = (PUWLAN_80211HDR)((unsigned char *)pTxPacket + sizeof(STxMgmtPacket));
	/* Setup the sFrame structure. */
	sFrame.pBuf = (unsigned char *)pTxPacket->p80211Header;
	sFrame.len = WLAN_REASSOCREQ_FR_MAXLEN;

	// format fixed field frame structure
	vMgrEncodeReassocRequest(&sFrame);

	/* Setup the header */
	sFrame.pHdr->sA3.wFrameCtl = cpu_to_le16(
		(
			WLAN_SET_FC_FTYPE(WLAN_TYPE_MGR) |
			WLAN_SET_FC_FSTYPE(WLAN_FSTYPE_REASSOCREQ)
));
	memcpy(sFrame.pHdr->sA3.abyAddr1, pDAddr, WLAN_ADDR_LEN);
	memcpy(sFrame.pHdr->sA3.abyAddr2, pMgmt->abyMACAddr, WLAN_ADDR_LEN);
	memcpy(sFrame.pHdr->sA3.abyAddr3, pMgmt->abyCurrBSSID, WLAN_BSSID_LEN);

	/* Set the capability and listen interval */
	*(sFrame.pwCapInfo) = cpu_to_le16(wCurrCapInfo);
	*(sFrame.pwListenInterval) = cpu_to_le16(wListenInterval);

	memcpy(sFrame.pAddrCurrAP, pMgmt->abyCurrBSSID, WLAN_BSSID_LEN);
	/* Copy the SSID */
	/* sFrame.len point to end of fixed field */
	sFrame.pSSID = (PWLAN_IE_SSID)(sFrame.pBuf + sFrame.len);
	sFrame.len += pCurrSSID->len + WLAN_IEHDR_LEN;
	memcpy(sFrame.pSSID, pCurrSSID, pCurrSSID->len + WLAN_IEHDR_LEN);

	pMgmt->sAssocInfo.AssocInfo.RequestIELength = pCurrSSID->len + WLAN_IEHDR_LEN;
	pMgmt->sAssocInfo.AssocInfo.OffsetRequestIEs = sizeof(NDIS_802_11_ASSOCIATION_INFORMATION);
	pbyIEs = pMgmt->sAssocInfo.abyIEs;
	memcpy(pbyIEs, pCurrSSID, pCurrSSID->len + WLAN_IEHDR_LEN);
	pbyIEs += pCurrSSID->len + WLAN_IEHDR_LEN;

	/* Copy the rate set */
	/* sFrame.len point to end of SSID */
	sFrame.pSuppRates = (PWLAN_IE_SUPP_RATES)(sFrame.pBuf + sFrame.len);
	sFrame.len += pCurrRates->len + WLAN_IEHDR_LEN;
	memcpy(sFrame.pSuppRates, pCurrRates, pCurrRates->len + WLAN_IEHDR_LEN);

	// Copy the extension rate set
	if ((pMgmt->eCurrentPHYMode == PHY_TYPE_11G) && (pCurrExtSuppRates->len > 0)) {
		sFrame.pExtSuppRates = (PWLAN_IE_SUPP_RATES)(sFrame.pBuf + sFrame.len);
		sFrame.len += pCurrExtSuppRates->len + WLAN_IEHDR_LEN;
		memcpy(sFrame.pExtSuppRates, pCurrExtSuppRates, pCurrExtSuppRates->len + WLAN_IEHDR_LEN);
	}

	pMgmt->sAssocInfo.AssocInfo.RequestIELength += pCurrRates->len + WLAN_IEHDR_LEN;
	memcpy(pbyIEs, pCurrRates, pCurrRates->len + WLAN_IEHDR_LEN);
	pbyIEs += pCurrRates->len + WLAN_IEHDR_LEN;

	if (((pMgmt->eAuthenMode == WMAC_AUTH_WPA) ||
	     (pMgmt->eAuthenMode == WMAC_AUTH_WPAPSK) ||
	     (pMgmt->eAuthenMode == WMAC_AUTH_WPANONE)) &&
	    (pMgmt->pCurrBSS != NULL)) {
		/* WPA IE */
		sFrame.pRSNWPA = (PWLAN_IE_RSN_EXT)(sFrame.pBuf + sFrame.len);
		sFrame.pRSNWPA->byElementID = WLAN_EID_RSN_WPA;
		sFrame.pRSNWPA->len = 16;
		sFrame.pRSNWPA->abyOUI[0] = 0x00;
		sFrame.pRSNWPA->abyOUI[1] = 0x50;
		sFrame.pRSNWPA->abyOUI[2] = 0xf2;
		sFrame.pRSNWPA->abyOUI[3] = 0x01;
		sFrame.pRSNWPA->wVersion = 1;
		//Group Key Cipher Suite
		sFrame.pRSNWPA->abyMulticast[0] = 0x00;
		sFrame.pRSNWPA->abyMulticast[1] = 0x50;
		sFrame.pRSNWPA->abyMulticast[2] = 0xf2;
		if (pMgmt->byCSSGK == KEY_CTL_WEP) {
			sFrame.pRSNWPA->abyMulticast[3] = pMgmt->pCurrBSS->byGKType;
		} else if (pMgmt->byCSSGK == KEY_CTL_TKIP) {
			sFrame.pRSNWPA->abyMulticast[3] = WPA_TKIP;
		} else if (pMgmt->byCSSGK == KEY_CTL_CCMP) {
			sFrame.pRSNWPA->abyMulticast[3] = WPA_AESCCMP;
		} else {
			sFrame.pRSNWPA->abyMulticast[3] = WPA_NONE;
		}
		// Pairwise Key Cipher Suite
		sFrame.pRSNWPA->wPKCount = 1;
		sFrame.pRSNWPA->PKSList[0].abyOUI[0] = 0x00;
		sFrame.pRSNWPA->PKSList[0].abyOUI[1] = 0x50;
		sFrame.pRSNWPA->PKSList[0].abyOUI[2] = 0xf2;
		if (pMgmt->byCSSPK == KEY_CTL_TKIP) {
			sFrame.pRSNWPA->PKSList[0].abyOUI[3] = WPA_TKIP;
		} else if (pMgmt->byCSSPK == KEY_CTL_CCMP) {
			sFrame.pRSNWPA->PKSList[0].abyOUI[3] = WPA_AESCCMP;
		} else {
			sFrame.pRSNWPA->PKSList[0].abyOUI[3] = WPA_NONE;
		}
		// Auth Key Management Suite
		pbyRSN = (unsigned char *)(sFrame.pBuf + sFrame.len + 2 + sFrame.pRSNWPA->len);
		*pbyRSN++ = 0x01;
		*pbyRSN++ = 0x00;
		*pbyRSN++ = 0x00;

		*pbyRSN++ = 0x50;
		*pbyRSN++ = 0xf2;
		if (pMgmt->eAuthenMode == WMAC_AUTH_WPAPSK) {
			*pbyRSN++ = WPA_AUTH_PSK;
		} else if (pMgmt->eAuthenMode == WMAC_AUTH_WPA) {
			*pbyRSN++ = WPA_AUTH_IEEE802_1X;
		} else {
			*pbyRSN++ = WPA_NONE;
		}

		sFrame.pRSNWPA->len += 6;

		// RSN Capabilities
		*pbyRSN++ = 0x00;
		*pbyRSN++ = 0x00;
		sFrame.pRSNWPA->len += 2;

		sFrame.len += sFrame.pRSNWPA->len + WLAN_IEHDR_LEN;
		// copy to AssocInfo. for OID_802_11_ASSOCIATION_INFORMATION
		pMgmt->sAssocInfo.AssocInfo.RequestIELength += sFrame.pRSNWPA->len + WLAN_IEHDR_LEN;
		memcpy(pbyIEs, sFrame.pRSNWPA, sFrame.pRSNWPA->len + WLAN_IEHDR_LEN);
		pbyIEs += sFrame.pRSNWPA->len + WLAN_IEHDR_LEN;

	} else if (((pMgmt->eAuthenMode == WMAC_AUTH_WPA2) ||
		    (pMgmt->eAuthenMode == WMAC_AUTH_WPA2PSK)) &&
		   (pMgmt->pCurrBSS != NULL)) {
		unsigned int ii;
		unsigned short *pwPMKID;

		/* WPA IE */
		sFrame.pRSN = (PWLAN_IE_RSN)(sFrame.pBuf + sFrame.len);
		sFrame.pRSN->byElementID = WLAN_EID_RSN;
		sFrame.pRSN->len = 6; //Version(2)+GK(4)
		sFrame.pRSN->wVersion = 1;
		//Group Key Cipher Suite
		sFrame.pRSN->abyRSN[0] = 0x00;
		sFrame.pRSN->abyRSN[1] = 0x0F;
		sFrame.pRSN->abyRSN[2] = 0xAC;
		if (pMgmt->byCSSGK == KEY_CTL_WEP) {
			sFrame.pRSN->abyRSN[3] = pMgmt->pCurrBSS->byCSSGK;
		} else if (pMgmt->byCSSGK == KEY_CTL_TKIP) {
			sFrame.pRSN->abyRSN[3] = WLAN_11i_CSS_TKIP;
		} else if (pMgmt->byCSSGK == KEY_CTL_CCMP) {
			sFrame.pRSN->abyRSN[3] = WLAN_11i_CSS_CCMP;
		} else {
			sFrame.pRSN->abyRSN[3] = WLAN_11i_CSS_UNKNOWN;
		}

		// Pairwise Key Cipher Suite
		sFrame.pRSN->abyRSN[4] = 1;
		sFrame.pRSN->abyRSN[5] = 0;
		sFrame.pRSN->abyRSN[6] = 0x00;
		sFrame.pRSN->abyRSN[7] = 0x0F;
		sFrame.pRSN->abyRSN[8] = 0xAC;
		if (pMgmt->byCSSPK == KEY_CTL_TKIP) {
			sFrame.pRSN->abyRSN[9] = WLAN_11i_CSS_TKIP;
		} else if (pMgmt->byCSSPK == KEY_CTL_CCMP) {
			sFrame.pRSN->abyRSN[9] = WLAN_11i_CSS_CCMP;
		} else if (pMgmt->byCSSPK == KEY_CTL_NONE) {
			sFrame.pRSN->abyRSN[9] = WLAN_11i_CSS_USE_GROUP;
		} else {
			sFrame.pRSN->abyRSN[9] = WLAN_11i_CSS_UNKNOWN;
		}
		sFrame.pRSN->len += 6;

		// Auth Key Management Suite
		sFrame.pRSN->abyRSN[10] = 1;
		sFrame.pRSN->abyRSN[11] = 0;
		sFrame.pRSN->abyRSN[12] = 0x00;
		sFrame.pRSN->abyRSN[13] = 0x0F;
		sFrame.pRSN->abyRSN[14] = 0xAC;
		if (pMgmt->eAuthenMode == WMAC_AUTH_WPA2PSK) {
			sFrame.pRSN->abyRSN[15] = WLAN_11i_AKMSS_PSK;
		} else if (pMgmt->eAuthenMode == WMAC_AUTH_WPA2) {
			sFrame.pRSN->abyRSN[15] = WLAN_11i_AKMSS_802_1X;
		} else {
			sFrame.pRSN->abyRSN[15] = WLAN_11i_AKMSS_UNKNOWN;
		}
		sFrame.pRSN->len += 6;

		// RSN Capabilities
		if (pMgmt->pCurrBSS->sRSNCapObj.bRSNCapExist == true) {
			memcpy(&sFrame.pRSN->abyRSN[16], &pMgmt->pCurrBSS->sRSNCapObj.wRSNCap, 2);
		} else {
			sFrame.pRSN->abyRSN[16] = 0;
			sFrame.pRSN->abyRSN[17] = 0;
		}
		sFrame.pRSN->len += 2;

		if ((pDevice->gsPMKID.BSSIDInfoCount > 0) && (pDevice->bRoaming == true) && (pMgmt->eAuthenMode == WMAC_AUTH_WPA2)) {
			// RSN PMKID
			pbyRSN = &sFrame.pRSN->abyRSN[18];
			pwPMKID = (unsigned short *)pbyRSN; // Point to PMKID count
			*pwPMKID = 0;            // Initialize PMKID count
			pbyRSN += 2;             // Point to PMKID list
			for (ii = 0; ii < pDevice->gsPMKID.BSSIDInfoCount; ii++) {
				if (!memcmp(&pDevice->gsPMKID.BSSIDInfo[ii].BSSID[0], pMgmt->abyCurrBSSID, ETH_ALEN)) {
					(*pwPMKID)++;
					memcpy(pbyRSN, pDevice->gsPMKID.BSSIDInfo[ii].PMKID, 16);
					pbyRSN += 16;
				}
			}
			if (*pwPMKID != 0) {
				sFrame.pRSN->len += (2 + (*pwPMKID) * 16);
			}
		}

		sFrame.len += sFrame.pRSN->len + WLAN_IEHDR_LEN;
		// copy to AssocInfo. for OID_802_11_ASSOCIATION_INFORMATION
		pMgmt->sAssocInfo.AssocInfo.RequestIELength += sFrame.pRSN->len + WLAN_IEHDR_LEN;
		memcpy(pbyIEs, sFrame.pRSN, sFrame.pRSN->len + WLAN_IEHDR_LEN);
		pbyIEs += sFrame.pRSN->len + WLAN_IEHDR_LEN;
	}

	/* Adjust the length fields */
	pTxPacket->cbMPDULen = sFrame.len;
	pTxPacket->cbPayloadLen = sFrame.len - WLAN_HDR_ADDR3_LEN;

	return pTxPacket;
}

/*+
 *
 * Routine Description:
 *  Constructs an assoc-response frame
 *
 *
 * Return Value:
 *    PTR to frame; or NULL on allocation failure
 *
 -*/

PSTxMgmtPacket
s_MgrMakeAssocResponse(
	PSDevice pDevice,
	PSMgmtObject pMgmt,
	unsigned short wCurrCapInfo,
	unsigned short wAssocStatus,
	unsigned short wAssocAID,
	unsigned char *pDstAddr,
	PWLAN_IE_SUPP_RATES pCurrSuppRates,
	PWLAN_IE_SUPP_RATES pCurrExtSuppRates
)
{
	PSTxMgmtPacket      pTxPacket = NULL;
	WLAN_FR_ASSOCRESP   sFrame;

	pTxPacket = (PSTxMgmtPacket)pMgmt->pbyMgmtPacketPool;
	memset(pTxPacket, 0, sizeof(STxMgmtPacket) + WLAN_ASSOCREQ_FR_MAXLEN);
	pTxPacket->p80211Header = (PUWLAN_80211HDR)((unsigned char *)pTxPacket + sizeof(STxMgmtPacket));
	// Setup the sFrame structure
	sFrame.pBuf = (unsigned char *)pTxPacket->p80211Header;
	sFrame.len = WLAN_REASSOCRESP_FR_MAXLEN;
	vMgrEncodeAssocResponse(&sFrame);
	// Setup the header
	sFrame.pHdr->sA3.wFrameCtl = cpu_to_le16(
		(
			WLAN_SET_FC_FTYPE(WLAN_TYPE_MGR) |
			WLAN_SET_FC_FSTYPE(WLAN_FSTYPE_ASSOCRESP)
));
	memcpy(sFrame.pHdr->sA3.abyAddr1, pDstAddr, WLAN_ADDR_LEN);
	memcpy(sFrame.pHdr->sA3.abyAddr2, pMgmt->abyMACAddr, WLAN_ADDR_LEN);
	memcpy(sFrame.pHdr->sA3.abyAddr3, pMgmt->abyCurrBSSID, WLAN_BSSID_LEN);

	*sFrame.pwCapInfo = cpu_to_le16(wCurrCapInfo);
	*sFrame.pwStatus = cpu_to_le16(wAssocStatus);
	*sFrame.pwAid = cpu_to_le16((unsigned short)(wAssocAID | BIT14 | BIT15));

	// Copy the rate set
	sFrame.pSuppRates = (PWLAN_IE_SUPP_RATES)(sFrame.pBuf + sFrame.len);
	sFrame.len += ((PWLAN_IE_SUPP_RATES)pCurrSuppRates)->len + WLAN_IEHDR_LEN;
	memcpy(sFrame.pSuppRates,
	       pCurrSuppRates,
	       ((PWLAN_IE_SUPP_RATES)pCurrSuppRates)->len + WLAN_IEHDR_LEN
);

	if (((PWLAN_IE_SUPP_RATES)pCurrExtSuppRates)->len != 0) {
		sFrame.pExtSuppRates = (PWLAN_IE_SUPP_RATES)(sFrame.pBuf + sFrame.len);
		sFrame.len += ((PWLAN_IE_SUPP_RATES)pCurrExtSuppRates)->len + WLAN_IEHDR_LEN;
		memcpy(sFrame.pExtSuppRates,
		       pCurrExtSuppRates,
		       ((PWLAN_IE_SUPP_RATES)pCurrExtSuppRates)->len + WLAN_IEHDR_LEN
);
	}

	// Adjust the length fields
	pTxPacket->cbMPDULen = sFrame.len;
	pTxPacket->cbPayloadLen = sFrame.len - WLAN_HDR_ADDR3_LEN;

	return pTxPacket;
}

/*+
 *
 * Routine Description:
 *  Constructs an reassoc-response frame
 *
 *
 * Return Value:
 *    PTR to frame; or NULL on allocation failure
 *
 -*/

PSTxMgmtPacket
s_MgrMakeReAssocResponse(
	PSDevice pDevice,
	PSMgmtObject pMgmt,
	unsigned short wCurrCapInfo,
	unsigned short wAssocStatus,
	unsigned short wAssocAID,
	unsigned char *pDstAddr,
	PWLAN_IE_SUPP_RATES pCurrSuppRates,
	PWLAN_IE_SUPP_RATES pCurrExtSuppRates
)
{
	PSTxMgmtPacket      pTxPacket = NULL;
	WLAN_FR_REASSOCRESP   sFrame;

	pTxPacket = (PSTxMgmtPacket)pMgmt->pbyMgmtPacketPool;
	memset(pTxPacket, 0, sizeof(STxMgmtPacket) + WLAN_ASSOCREQ_FR_MAXLEN);
	pTxPacket->p80211Header = (PUWLAN_80211HDR)((unsigned char *)pTxPacket + sizeof(STxMgmtPacket));
	// Setup the sFrame structure
	sFrame.pBuf = (unsigned char *)pTxPacket->p80211Header;
	sFrame.len = WLAN_REASSOCRESP_FR_MAXLEN;
	vMgrEncodeReassocResponse(&sFrame);
	// Setup the header
	sFrame.pHdr->sA3.wFrameCtl = cpu_to_le16(
		(
			WLAN_SET_FC_FTYPE(WLAN_TYPE_MGR) |
			WLAN_SET_FC_FSTYPE(WLAN_FSTYPE_REASSOCRESP)
));
	memcpy(sFrame.pHdr->sA3.abyAddr1, pDstAddr, WLAN_ADDR_LEN);
	memcpy(sFrame.pHdr->sA3.abyAddr2, pMgmt->abyMACAddr, WLAN_ADDR_LEN);
	memcpy(sFrame.pHdr->sA3.abyAddr3, pMgmt->abyCurrBSSID, WLAN_BSSID_LEN);

	*sFrame.pwCapInfo = cpu_to_le16(wCurrCapInfo);
	*sFrame.pwStatus = cpu_to_le16(wAssocStatus);
	*sFrame.pwAid = cpu_to_le16((unsigned short)(wAssocAID | BIT14 | BIT15));

	// Copy the rate set
	sFrame.pSuppRates = (PWLAN_IE_SUPP_RATES)(sFrame.pBuf + sFrame.len);
	sFrame.len += ((PWLAN_IE_SUPP_RATES)pCurrSuppRates)->len + WLAN_IEHDR_LEN;
	memcpy(sFrame.pSuppRates,
	       pCurrSuppRates,
	       ((PWLAN_IE_SUPP_RATES)pCurrSuppRates)->len + WLAN_IEHDR_LEN
);

	if (((PWLAN_IE_SUPP_RATES)pCurrExtSuppRates)->len != 0) {
		sFrame.pExtSuppRates = (PWLAN_IE_SUPP_RATES)(sFrame.pBuf + sFrame.len);
		sFrame.len += ((PWLAN_IE_SUPP_RATES)pCurrExtSuppRates)->len + WLAN_IEHDR_LEN;
		memcpy(sFrame.pExtSuppRates,
		       pCurrExtSuppRates,
		       ((PWLAN_IE_SUPP_RATES)pCurrExtSuppRates)->len + WLAN_IEHDR_LEN
);
	}

	// Adjust the length fields
	pTxPacket->cbMPDULen = sFrame.len;
	pTxPacket->cbPayloadLen = sFrame.len - WLAN_HDR_ADDR3_LEN;

	return pTxPacket;
}

/*+
 *
 * Routine Description:
 *  Handles probe response management frames.
 *
 *
 * Return Value:
 *    none.
 *
 -*/

static
void
s_vMgrRxProbeResponse(
	PSDevice pDevice,
	PSMgmtObject pMgmt,
	PSRxMgmtPacket pRxPacket
)
{
	PKnownBSS           pBSSList = NULL;
	WLAN_FR_PROBERESP   sFrame;
	unsigned char byCurrChannel = pRxPacket->byRxChannel;
	ERPObject           sERP;
	unsigned char byIEChannel = 0;
	bool bChannelHit = true;

	memset(&sFrame, 0, sizeof(WLAN_FR_PROBERESP));
	// decode the frame
	sFrame.len = pRxPacket->cbMPDULen;
	sFrame.pBuf = (unsigned char *)pRxPacket->p80211Header;
	vMgrDecodeProbeResponse(&sFrame);

	if ((sFrame.pqwTimestamp == 0) ||
	    (sFrame.pwBeaconInterval == 0) ||
	    (sFrame.pwCapInfo == 0) ||
	    (sFrame.pSSID == 0) ||
	    (sFrame.pSuppRates == 0)) {
		DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Probe resp:Fail addr:[%p] \n", pRxPacket->p80211Header);
		DBG_PORT80(0xCC);
		return;
	}

	if (sFrame.pSSID->len == 0)
		DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Rx Probe resp: SSID len = 0 \n");

	if (sFrame.pDSParms != 0) {
		if (byCurrChannel > CB_MAX_CHANNEL_24G) {
			// channel remapping to
			byIEChannel = get_channel_mapping(pMgmt->pAdapter, sFrame.pDSParms->byCurrChannel, PHY_TYPE_11A);
		} else {
			byIEChannel = sFrame.pDSParms->byCurrChannel;
		}
		if (byCurrChannel != byIEChannel) {
			// adjust channel info. bcs we rcv adjacent channel packets
			bChannelHit = false;
			byCurrChannel = byIEChannel;
		}
	} else {
		// no DS channel info
		bChannelHit = true;
	}

//2008-0730-01<Add>by MikeLiu
	if (ChannelExceedZoneType(pDevice, byCurrChannel) == true)
		return;

	if (sFrame.pERP != NULL) {
		sERP.byERP = sFrame.pERP->byContext;
		sERP.bERPExist = true;
	} else {
		sERP.bERPExist = false;
		sERP.byERP = 0;
	}

	// update or insert the bss
	pBSSList = BSSpAddrIsInBSSList((void *)pDevice, sFrame.pHdr->sA3.abyAddr3, sFrame.pSSID);
	if (pBSSList) {
		BSSbUpdateToBSSList((void *)pDevice,
				    *sFrame.pqwTimestamp,
				    *sFrame.pwBeaconInterval,
				    *sFrame.pwCapInfo,
				    byCurrChannel,
				    bChannelHit,
				    sFrame.pSSID,
				    sFrame.pSuppRates,
				    sFrame.pExtSuppRates,
				    &sERP,
				    sFrame.pRSN,
				    sFrame.pRSNWPA,
				    sFrame.pIE_Country,
				    sFrame.pIE_Quiet,
				    pBSSList,
				    sFrame.len - WLAN_HDR_ADDR3_LEN,
				    sFrame.pHdr->sA4.abyAddr4,   // payload of probresponse
				    (void *)pRxPacket
);
	} else {
		DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Probe resp/insert: RxChannel = : %d\n", byCurrChannel);
		BSSbInsertToBSSList((void *)pDevice,
				    sFrame.pHdr->sA3.abyAddr3,
				    *sFrame.pqwTimestamp,
				    *sFrame.pwBeaconInterval,
				    *sFrame.pwCapInfo,
				    byCurrChannel,
				    sFrame.pSSID,
				    sFrame.pSuppRates,
				    sFrame.pExtSuppRates,
				    &sERP,
				    sFrame.pRSN,
				    sFrame.pRSNWPA,
				    sFrame.pIE_Country,
				    sFrame.pIE_Quiet,
				    sFrame.len - WLAN_HDR_ADDR3_LEN,
				    sFrame.pHdr->sA4.abyAddr4,   // payload of beacon
				    (void *)pRxPacket
);
	}
	return;
}

/*+
 *
 * Routine Description:(AP)or(Ad-hoc STA)
 *  Handles probe request management frames.
 *
 *
 * Return Value:
 *    none.
 *
 -*/

static
void
s_vMgrRxProbeRequest(
	PSDevice pDevice,
	PSMgmtObject pMgmt,
	PSRxMgmtPacket pRxPacket
)
{
	WLAN_FR_PROBEREQ    sFrame;
	CMD_STATUS          Status;
	PSTxMgmtPacket      pTxPacket;
	unsigned char byPHYType = BB_TYPE_11B;

	// STA in Ad-hoc mode: when latest TBTT beacon transmit success,
	// STA have to response this request.
	if ((pMgmt->eCurrMode == WMAC_MODE_ESS_AP) ||
	    ((pMgmt->eCurrMode == WMAC_MODE_IBSS_STA) && pDevice->bBeaconSent)) {
		memset(&sFrame, 0, sizeof(WLAN_FR_PROBEREQ));
		// decode the frame
		sFrame.len = pRxPacket->cbMPDULen;
		sFrame.pBuf = (unsigned char *)pRxPacket->p80211Header;
		vMgrDecodeProbeRequest(&sFrame);
/*
  DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Probe request rx:MAC addr:%pM\n",
  sFrame.pHdr->sA3.abyAddr2);
*/
		if (sFrame.pSSID->len != 0) {
			if (sFrame.pSSID->len != ((PWLAN_IE_SSID)pMgmt->abyCurrSSID)->len)
				return;
			if (memcmp(sFrame.pSSID->abySSID,
				   ((PWLAN_IE_SSID)pMgmt->abyCurrSSID)->abySSID,
				   ((PWLAN_IE_SSID)pMgmt->abyCurrSSID)->len) != 0) {
				return;
			}
		}

		if ((sFrame.pSuppRates->len > 4) || (sFrame.pExtSuppRates != NULL)) {
			byPHYType = BB_TYPE_11G;
		}

		// Probe response reply..
		pTxPacket = s_MgrMakeProbeResponse
			(
				pDevice,
				pMgmt,
				pMgmt->wCurrCapInfo,
				pMgmt->wCurrBeaconPeriod,
				pMgmt->uCurrChannel,
				0,
				sFrame.pHdr->sA3.abyAddr2,
				(PWLAN_IE_SSID)pMgmt->abyCurrSSID,
				(unsigned char *)pMgmt->abyCurrBSSID,
				(PWLAN_IE_SUPP_RATES)pMgmt->abyCurrSuppRates,
				(PWLAN_IE_SUPP_RATES)pMgmt->abyCurrExtSuppRates,
				byPHYType
);
		if (pTxPacket != NULL) {
			/* send the frame */
			Status = csMgmt_xmit(pDevice, pTxPacket);
			if (Status != CMD_STATUS_PENDING) {
				DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Mgt:Probe response tx failed\n");
			} else {
//                DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Mgt:Probe response tx sending..\n");
			}
		}
	}

	return;
}

/*+
 *
 * Routine Description:
 *
 *  Entry point for the reception and handling of 802.11 management
 *  frames. Makes a determination of the frame type and then calls
 *  the appropriate function.
 *
 *
 * Return Value:
 *    none.
 *
 -*/

void
vMgrRxManagePacket(
	void *hDeviceContext,
	PSMgmtObject pMgmt,
	PSRxMgmtPacket pRxPacket
)
{
	PSDevice    pDevice = (PSDevice)hDeviceContext;
	bool bInScan = false;
	unsigned int uNodeIndex = 0;
	NODE_STATE  eNodeState = 0;
	CMD_STATUS  Status;

	if (pMgmt->eCurrMode == WMAC_MODE_ESS_AP) {
		if (BSSDBbIsSTAInNodeDB(pMgmt, pRxPacket->p80211Header->sA3.abyAddr2, &uNodeIndex))
			eNodeState = pMgmt->sNodeDBTable[uNodeIndex].eNodeState;
	}

	switch (WLAN_GET_FC_FSTYPE((pRxPacket->p80211Header->sA3.wFrameCtl))) {
	case WLAN_FSTYPE_ASSOCREQ:
		// Frame Clase = 2
		DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "rx assocreq\n");
		if (eNodeState < NODE_AUTH) {
			// send deauth notification
			// reason = (6) class 2 received from nonauth sta
			vMgrDeAuthenBeginSta(pDevice,
					     pMgmt,
					     pRxPacket->p80211Header->sA3.abyAddr2,
					     (6),
					     &Status
);
			DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "wmgr: send vMgrDeAuthenBeginSta 1\n");
		} else {
			s_vMgrRxAssocRequest(pDevice, pMgmt, pRxPacket, uNodeIndex);
		}
		break;

	case WLAN_FSTYPE_ASSOCRESP:
		// Frame Clase = 2
		DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "rx assocresp1\n");
		s_vMgrRxAssocResponse(pDevice, pMgmt, pRxPacket, false);
		DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "rx assocresp2\n");
		break;

	case WLAN_FSTYPE_REASSOCREQ:
		// Frame Clase = 2
		DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "rx reassocreq\n");
		// Todo: reassoc
		if (eNodeState < NODE_AUTH) {
			// send deauth notification
			// reason = (6) class 2 received from nonauth sta
			vMgrDeAuthenBeginSta(pDevice,
					     pMgmt,
					     pRxPacket->p80211Header->sA3.abyAddr2,
					     (6),
					     &Status
);
			DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "wmgr: send vMgrDeAuthenBeginSta 2\n");

		}
		s_vMgrRxReAssocRequest(pDevice, pMgmt, pRxPacket, uNodeIndex);
		break;

	case WLAN_FSTYPE_REASSOCRESP:
		// Frame Clase = 2
		DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "rx reassocresp\n");
		s_vMgrRxAssocResponse(pDevice, pMgmt, pRxPacket, true);
		break;

	case WLAN_FSTYPE_PROBEREQ:
		// Frame Clase = 0
		//DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "rx probereq\n");
		s_vMgrRxProbeRequest(pDevice, pMgmt, pRxPacket);
		break;

	case WLAN_FSTYPE_PROBERESP:
		// Frame Clase = 0
		DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "rx proberesp\n");

		s_vMgrRxProbeResponse(pDevice, pMgmt, pRxPacket);
		break;

	case WLAN_FSTYPE_BEACON:
		// Frame Clase = 0
		//DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "rx beacon\n");
		if (pMgmt->eScanState != WMAC_NO_SCANNING) {
			bInScan = true;
		}
		s_vMgrRxBeacon(pDevice, pMgmt, pRxPacket, bInScan);
		break;

	case WLAN_FSTYPE_ATIM:
		// Frame Clase = 1
		DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "rx atim\n");
		break;

	case WLAN_FSTYPE_DISASSOC:
		// Frame Clase = 2
		DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "rx disassoc\n");
		if (eNodeState < NODE_AUTH) {
			// send deauth notification
			// reason = (6) class 2 received from nonauth sta
			vMgrDeAuthenBeginSta(pDevice,
					     pMgmt,
					     pRxPacket->p80211Header->sA3.abyAddr2,
					     (6),
					     &Status
);
			DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "wmgr: send vMgrDeAuthenBeginSta 3\n");
		}
		s_vMgrRxDisassociation(pDevice, pMgmt, pRxPacket);
		break;

	case WLAN_FSTYPE_AUTHEN:
		// Frame Clase = 1
		DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO  "rx authen\n");
		s_vMgrRxAuthentication(pDevice, pMgmt, pRxPacket);
		break;

	case WLAN_FSTYPE_DEAUTHEN:
		// Frame Clase = 1
		DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "rx deauthen\n");
		s_vMgrRxDeauthentication(pDevice, pMgmt, pRxPacket);
		break;

	default:
		DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "rx unknown mgmt\n");
	}

	return;
}

/*+
 *
 * Routine Description:
 *
 *
 *  Prepare beacon to send
 *
 * Return Value:
 *    true if success; false if failed.
 *
 -*/
bool
bMgrPrepareBeaconToSend(
	void *hDeviceContext,
	PSMgmtObject pMgmt
)
{
	PSDevice            pDevice = (PSDevice)hDeviceContext;
	PSTxMgmtPacket      pTxPacket;

//    pDevice->bBeaconBufReady = false;
	if (pDevice->bEncryptionEnable || pDevice->bEnable8021x) {
		pMgmt->wCurrCapInfo |= WLAN_SET_CAP_INFO_PRIVACY(1);
	} else {
		pMgmt->wCurrCapInfo &= ~WLAN_SET_CAP_INFO_PRIVACY(1);
	}
	pTxPacket = s_MgrMakeBeacon
		(
			pDevice,
			pMgmt,
			pMgmt->wCurrCapInfo,
			pMgmt->wCurrBeaconPeriod,
			pMgmt->uCurrChannel,
			pMgmt->wCurrATIMWindow, //0,
			(PWLAN_IE_SSID)pMgmt->abyCurrSSID,
			(unsigned char *)pMgmt->abyCurrBSSID,
			(PWLAN_IE_SUPP_RATES)pMgmt->abyCurrSuppRates,
			(PWLAN_IE_SUPP_RATES)pMgmt->abyCurrExtSuppRates
);

	if ((pMgmt->eCurrMode == WMAC_MODE_IBSS_STA) &&
	    (pMgmt->abyCurrBSSID[0] == 0))
		return false;

	csBeacon_xmit(pDevice, pTxPacket);

	return true;
}

/*+
 *
 * Routine Description:
 *
 *  Log a warning message based on the contents of the Status
 *  Code field of an 802.11 management frame.  Defines are
 *  derived from 802.11-1997 SPEC.
 *
 * Return Value:
 *    none.
 *
 -*/
static
void
s_vMgrLogStatus(
	PSMgmtObject pMgmt,
	unsigned short wStatus
)
{
	switch (wStatus) {
	case WLAN_MGMT_STATUS_UNSPEC_FAILURE:
		DBG_PRT(MSG_LEVEL_NOTICE, KERN_INFO "Status code == Unspecified error.\n");
		break;
	case WLAN_MGMT_STATUS_CAPS_UNSUPPORTED:
		DBG_PRT(MSG_LEVEL_NOTICE, KERN_INFO "Status code == Can't support all requested capabilities.\n");
		break;
	case WLAN_MGMT_STATUS_REASSOC_NO_ASSOC:
		DBG_PRT(MSG_LEVEL_NOTICE, KERN_INFO "Status code == Reassoc denied, can't confirm original Association.\n");
		break;
	case WLAN_MGMT_STATUS_ASSOC_DENIED_UNSPEC:
		DBG_PRT(MSG_LEVEL_NOTICE, KERN_INFO "Status code == Assoc denied, undefine in spec\n");
		break;
	case WLAN_MGMT_STATUS_UNSUPPORTED_AUTHALG:
		DBG_PRT(MSG_LEVEL_NOTICE, KERN_INFO "Status code == Peer doesn't support authen algorithm.\n");
		break;
	case WLAN_MGMT_STATUS_RX_AUTH_NOSEQ:
		DBG_PRT(MSG_LEVEL_NOTICE, KERN_INFO "Status code == Authen frame received out of sequence.\n");
		break;
	case WLAN_MGMT_STATUS_CHALLENGE_FAIL:
		DBG_PRT(MSG_LEVEL_NOTICE, KERN_INFO "Status code == Authen rejected, challenge  failure.\n");
		break;
	case WLAN_MGMT_STATUS_AUTH_TIMEOUT:
		DBG_PRT(MSG_LEVEL_NOTICE, KERN_INFO "Status code == Authen rejected, timeout waiting for next frame.\n");
		break;
	case WLAN_MGMT_STATUS_ASSOC_DENIED_BUSY:
		DBG_PRT(MSG_LEVEL_NOTICE, KERN_INFO "Status code == Assoc denied, AP too busy.\n");
		break;
	case WLAN_MGMT_STATUS_ASSOC_DENIED_RATES:
		DBG_PRT(MSG_LEVEL_NOTICE, KERN_INFO "Status code == Assoc denied, we haven't enough basic rates.\n");
		break;
	case WLAN_MGMT_STATUS_ASSOC_DENIED_SHORTPREAMBLE:
		DBG_PRT(MSG_LEVEL_NOTICE, KERN_INFO "Status code == Assoc denied, we do not support short preamble.\n");
		break;
	case WLAN_MGMT_STATUS_ASSOC_DENIED_PBCC:
		DBG_PRT(MSG_LEVEL_NOTICE, KERN_INFO "Status code == Assoc denied, we do not support PBCC.\n");
		break;
	case WLAN_MGMT_STATUS_ASSOC_DENIED_AGILITY:
		DBG_PRT(MSG_LEVEL_NOTICE, KERN_INFO "Status code == Assoc denied, we do not support channel agility.\n");
		break;
	default:
		DBG_PRT(MSG_LEVEL_NOTICE, KERN_INFO "Unknown status code %d.\n", wStatus);
		break;
	}
}

/*
 *
 * Description:
 *    Add BSSID in PMKID Candidate list.
 *
 * Parameters:
 *  In:
 *      hDeviceContext - device structure point
 *      pbyBSSID - BSSID address for adding
 *      wRSNCap - BSS's RSN capability
 *  Out:
 *      none
 *
 * Return Value: none.
 *
 -*/
bool
bAdd_PMKID_Candidate(
	void *hDeviceContext,
	unsigned char *pbyBSSID,
	PSRSNCapObject psRSNCapObj
)
{
	PSDevice         pDevice = (PSDevice)hDeviceContext;
	PPMKID_CANDIDATE pCandidateList;
	unsigned int ii = 0;

	DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "bAdd_PMKID_Candidate START: (%d)\n", (int)pDevice->gsPMKIDCandidate.NumCandidates);

	if ((pDevice == NULL) || (pbyBSSID == NULL) || (psRSNCapObj == NULL))
		return false;

	if (pDevice->gsPMKIDCandidate.NumCandidates >= MAX_PMKIDLIST)
		return false;

	// Update Old Candidate
	for (ii = 0; ii < pDevice->gsPMKIDCandidate.NumCandidates; ii++) {
		pCandidateList = &pDevice->gsPMKIDCandidate.CandidateList[ii];
		if (!memcmp(pCandidateList->BSSID, pbyBSSID, ETH_ALEN)) {
			if ((psRSNCapObj->bRSNCapExist == true) && (psRSNCapObj->wRSNCap & BIT0)) {
				pCandidateList->Flags |= NDIS_802_11_PMKID_CANDIDATE_PREAUTH_ENABLED;
			} else {
				pCandidateList->Flags &= ~(NDIS_802_11_PMKID_CANDIDATE_PREAUTH_ENABLED);
			}
			return true;
		}
	}

	// New Candidate
	pCandidateList = &pDevice->gsPMKIDCandidate.CandidateList[pDevice->gsPMKIDCandidate.NumCandidates];
	if ((psRSNCapObj->bRSNCapExist == true) && (psRSNCapObj->wRSNCap & BIT0)) {
		pCandidateList->Flags |= NDIS_802_11_PMKID_CANDIDATE_PREAUTH_ENABLED;
	} else {
		pCandidateList->Flags &= ~(NDIS_802_11_PMKID_CANDIDATE_PREAUTH_ENABLED);
	}
	memcpy(pCandidateList->BSSID, pbyBSSID, ETH_ALEN);
	pDevice->gsPMKIDCandidate.NumCandidates++;
	DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "NumCandidates:%d\n", (int)pDevice->gsPMKIDCandidate.NumCandidates);
	return true;
}

/*
 *
 * Description:
 *    Flush PMKID Candidate list.
 *
 * Parameters:
 *  In:
 *      hDeviceContext - device structure point
 *  Out:
 *      none
 *
 * Return Value: none.
 *
 -*/
void
vFlush_PMKID_Candidate(
	void *hDeviceContext
)
{
	PSDevice        pDevice = (PSDevice)hDeviceContext;

	if (pDevice == NULL)
		return;

	memset(&pDevice->gsPMKIDCandidate, 0, sizeof(SPMKIDCandidateEvent));
}

static bool
s_bCipherMatch(
	PKnownBSS                        pBSSNode,
	NDIS_802_11_ENCRYPTION_STATUS    EncStatus,
	unsigned char *pbyCCSPK,
	unsigned char *pbyCCSGK
)
{
	unsigned char byMulticastCipher = KEY_CTL_INVALID;
	unsigned char byCipherMask = 0x00;
	int i;

	if (pBSSNode == NULL)
		return false;

	// check cap. of BSS
	if ((WLAN_GET_CAP_INFO_PRIVACY(pBSSNode->wCapInfo) != 0) &&
	    (EncStatus == Ndis802_11Encryption1Enabled)) {
		// default is WEP only
		byMulticastCipher = KEY_CTL_WEP;
	}

	if ((WLAN_GET_CAP_INFO_PRIVACY(pBSSNode->wCapInfo) != 0) &&
	    (pBSSNode->bWPA2Valid == true) &&
	    //20080123-01,<Add> by Einsn Liu
	    ((EncStatus == Ndis802_11Encryption3Enabled) || (EncStatus == Ndis802_11Encryption2Enabled))) {
		//WPA2
		// check Group Key Cipher
		if ((pBSSNode->byCSSGK == WLAN_11i_CSS_WEP40) ||
		    (pBSSNode->byCSSGK == WLAN_11i_CSS_WEP104)) {
			byMulticastCipher = KEY_CTL_WEP;
		} else if (pBSSNode->byCSSGK == WLAN_11i_CSS_TKIP) {
			byMulticastCipher = KEY_CTL_TKIP;
		} else if (pBSSNode->byCSSGK == WLAN_11i_CSS_CCMP) {
			byMulticastCipher = KEY_CTL_CCMP;
		} else {
			byMulticastCipher = KEY_CTL_INVALID;
		}

		// check Pairwise Key Cipher
		for (i = 0; i < pBSSNode->wCSSPKCount; i++) {
			if ((pBSSNode->abyCSSPK[i] == WLAN_11i_CSS_WEP40) ||
			    (pBSSNode->abyCSSPK[i] == WLAN_11i_CSS_WEP104)) {
				// this should not happen as defined 802.11i
				byCipherMask |= 0x01;
			} else if (pBSSNode->abyCSSPK[i] == WLAN_11i_CSS_TKIP) {
				byCipherMask |= 0x02;
			} else if (pBSSNode->abyCSSPK[i] == WLAN_11i_CSS_CCMP) {
				byCipherMask |= 0x04;
			} else if (pBSSNode->abyCSSPK[i] == WLAN_11i_CSS_USE_GROUP) {
				// use group key only ignore all others
				byCipherMask = 0;
				i = pBSSNode->wCSSPKCount;
			}
		}

	} else if ((WLAN_GET_CAP_INFO_PRIVACY(pBSSNode->wCapInfo) != 0) &&
		   (pBSSNode->bWPAValid == true) &&
		   ((EncStatus == Ndis802_11Encryption3Enabled) || (EncStatus == Ndis802_11Encryption2Enabled))) {
		//WPA
		// check Group Key Cipher
		if ((pBSSNode->byGKType == WPA_WEP40) ||
		    (pBSSNode->byGKType == WPA_WEP104)) {
			byMulticastCipher = KEY_CTL_WEP;
		} else if (pBSSNode->byGKType == WPA_TKIP) {
			byMulticastCipher = KEY_CTL_TKIP;
		} else if (pBSSNode->byGKType == WPA_AESCCMP) {
			byMulticastCipher = KEY_CTL_CCMP;
		} else {
			byMulticastCipher = KEY_CTL_INVALID;
		}

		// check Pairwise Key Cipher
		for (i = 0; i < pBSSNode->wPKCount; i++) {
			if (pBSSNode->abyPKType[i] == WPA_TKIP) {
				byCipherMask |= 0x02;
			} else if (pBSSNode->abyPKType[i] == WPA_AESCCMP) {
				byCipherMask |= 0x04;
			} else if (pBSSNode->abyPKType[i] == WPA_NONE) {
				// use group key only ignore all others
				byCipherMask = 0;
				i = pBSSNode->wPKCount;
			}
		}
	}

	DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "%d, %d, %d, %d, EncStatus:%d\n",
		byMulticastCipher, byCipherMask, pBSSNode->bWPAValid, pBSSNode->bWPA2Valid, EncStatus);

	// mask our cap. with BSS
	if (EncStatus == Ndis802_11Encryption1Enabled) {
		// For supporting Cisco migration mode, don't care pairwise key cipher
		if ((byMulticastCipher == KEY_CTL_WEP) &&
		    (byCipherMask == 0)) {
			*pbyCCSGK = KEY_CTL_WEP;
			*pbyCCSPK = KEY_CTL_NONE;
			return true;
		} else {
			return false;
		}

	} else if (EncStatus == Ndis802_11Encryption2Enabled) {
		if ((byMulticastCipher == KEY_CTL_TKIP) &&
		    (byCipherMask == 0)) {
			*pbyCCSGK = KEY_CTL_TKIP;
			*pbyCCSPK = KEY_CTL_NONE;
			return true;
		} else if ((byMulticastCipher == KEY_CTL_WEP) &&
			   ((byCipherMask & 0x02) != 0)) {
			*pbyCCSGK = KEY_CTL_WEP;
			*pbyCCSPK = KEY_CTL_TKIP;
			return true;
		} else if ((byMulticastCipher == KEY_CTL_TKIP) &&
			   ((byCipherMask & 0x02) != 0)) {
			*pbyCCSGK = KEY_CTL_TKIP;
			*pbyCCSPK = KEY_CTL_TKIP;
			return true;
		} else {
			return false;
		}
	} else if (EncStatus == Ndis802_11Encryption3Enabled) {
		if ((byMulticastCipher == KEY_CTL_CCMP) &&
		    (byCipherMask == 0)) {
			// When CCMP is enable, "Use group cipher suite" shall not be a valid option.
			return false;
		} else if ((byMulticastCipher == KEY_CTL_WEP) &&
			   ((byCipherMask & 0x04) != 0)) {
			*pbyCCSGK = KEY_CTL_WEP;
			*pbyCCSPK = KEY_CTL_CCMP;
			return true;
		} else if ((byMulticastCipher == KEY_CTL_TKIP) &&
			   ((byCipherMask & 0x04) != 0)) {
			*pbyCCSGK = KEY_CTL_TKIP;
			*pbyCCSPK = KEY_CTL_CCMP;
			return true;
		} else if ((byMulticastCipher == KEY_CTL_CCMP) &&
			   ((byCipherMask & 0x04) != 0)) {
			*pbyCCSGK = KEY_CTL_CCMP;
			*pbyCCSPK = KEY_CTL_CCMP;
			return true;
		} else {
			return false;
		}
	}
	return true;
}
