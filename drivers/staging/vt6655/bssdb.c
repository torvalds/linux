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
 * File: bssdb.c
 *
 * Purpose: Handles the Basic Service Set & Node Database functions
 *
 * Functions:
 *      BSSpSearchBSSList - Search known BSS list for Desire SSID or BSSID
 *      BSSvClearBSSList - Clear BSS List
 *      BSSbInsertToBSSList - Insert a BSS set into known BSS list
 *      BSSbUpdateToBSSList - Update BSS set in known BSS list
 *      BSSDBbIsSTAInNodeDB - Search Node DB table to find the index of matched DstAddr
 *      BSSvCreateOneNode - Allocate an Node for Node DB
 *      BSSvUpdateAPNode - Update AP Node content in Index 0 of KnownNodeDB
 *      BSSvSecondCallBack - One second timer callback function to update Node DB info & AP link status
 *      BSSvUpdateNodeTxCounter - Update Tx attemps, Tx failure counter in Node DB for auto-fall back rate control
 *
 * Revision History:
 *
 * Author: Lyndon Chen
 *
 * Date: July 17, 2002
 *
 */

#include "ttype.h"
#include "tmacro.h"
#include "tether.h"
#include "device.h"
#include "80211hdr.h"
#include "bssdb.h"
#include "wmgr.h"
#include "datarate.h"
#include "desc.h"
#include "wcmd.h"
#include "wpa.h"
#include "baseband.h"
#include "rf.h"
#include "card.h"
#include "channel.h"
#include "mac.h"
#include "wpa2.h"
#include "iowpa.h"

/*---------------------  Static Definitions -------------------------*/

/*---------------------  Static Classes  ----------------------------*/

/*---------------------  Static Variables  --------------------------*/
static int msglevel = MSG_LEVEL_INFO;

const unsigned short awHWRetry0[5][5] = {
	{RATE_18M, RATE_18M, RATE_12M, RATE_12M, RATE_12M},
	{RATE_24M, RATE_24M, RATE_18M, RATE_12M, RATE_12M},
	{RATE_36M, RATE_36M, RATE_24M, RATE_18M, RATE_18M},
	{RATE_48M, RATE_48M, RATE_36M, RATE_24M, RATE_24M},
	{RATE_54M, RATE_54M, RATE_48M, RATE_36M, RATE_36M}
};
const unsigned short awHWRetry1[5][5] = {
	{RATE_18M, RATE_18M, RATE_12M, RATE_6M, RATE_6M},
	{RATE_24M, RATE_24M, RATE_18M, RATE_6M, RATE_6M},
	{RATE_36M, RATE_36M, RATE_24M, RATE_12M, RATE_12M},
	{RATE_48M, RATE_48M, RATE_24M, RATE_12M, RATE_12M},
	{RATE_54M, RATE_54M, RATE_36M, RATE_18M, RATE_18M}
};

/*---------------------  Static Functions  --------------------------*/

void s_vCheckSensitivity(
	void *hDeviceContext
);

#ifdef Calcu_LinkQual
void s_uCalculateLinkQual(
	void *hDeviceContext
);
#endif

void s_vCheckPreEDThreshold(
	void *hDeviceContext
);
/*---------------------  Export Variables  --------------------------*/

/*---------------------  Export Functions  --------------------------*/

/*+
 *
 * Routine Description:
 *    Search known BSS list for Desire SSID or BSSID.
 *
 * Return Value:
 *    PTR to KnownBSS or NULL
 *
 -*/

PKnownBSS
BSSpSearchBSSList(
	void *hDeviceContext,
	unsigned char *pbyDesireBSSID,
	unsigned char *pbyDesireSSID,
	CARD_PHY_TYPE  ePhyType
)
{
	PSDevice        pDevice = (PSDevice)hDeviceContext;
	PSMgmtObject    pMgmt = pDevice->pMgmt;
	unsigned char *pbyBSSID = NULL;
	PWLAN_IE_SSID   pSSID = NULL;
	PKnownBSS       pCurrBSS = NULL;
	PKnownBSS       pSelect = NULL;
	unsigned char ZeroBSSID[WLAN_BSSID_LEN] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	unsigned int ii = 0;

	if (pbyDesireBSSID != NULL) {
		DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO
			"BSSpSearchBSSList BSSID[%pM]\n", pbyDesireBSSID);
		if ((!is_broadcast_ether_addr(pbyDesireBSSID)) &&
		    (memcmp(pbyDesireBSSID, ZeroBSSID, 6) != 0))
			pbyBSSID = pbyDesireBSSID;
	}
	if (pbyDesireSSID != NULL) {
		if (((PWLAN_IE_SSID)pbyDesireSSID)->len != 0)
			pSSID = (PWLAN_IE_SSID) pbyDesireSSID;
	}

	if (pbyBSSID != NULL) {
		/* match BSSID first */
		for (ii = 0; ii < MAX_BSS_NUM; ii++) {
			pCurrBSS = &(pMgmt->sBSSList[ii]);
			if (!pDevice->bLinkPass)
				pCurrBSS->bSelected = false;
			if ((pCurrBSS->bActive) &&
			    (!pCurrBSS->bSelected)) {
				if (ether_addr_equal(pCurrBSS->abyBSSID,
						     pbyBSSID)) {
					if (pSSID != NULL) {
						/* compare ssid */
						if (!memcmp(pSSID->abySSID,
							    ((PWLAN_IE_SSID)pCurrBSS->abySSID)->abySSID,
							    pSSID->len)) {
							if ((pMgmt->eConfigMode == WMAC_CONFIG_AUTO) ||
							    ((pMgmt->eConfigMode == WMAC_CONFIG_IBSS_STA) && WLAN_GET_CAP_INFO_IBSS(pCurrBSS->wCapInfo)) ||
							    ((pMgmt->eConfigMode == WMAC_CONFIG_ESS_STA) && WLAN_GET_CAP_INFO_ESS(pCurrBSS->wCapInfo))
) {
								pCurrBSS->bSelected = true;
								return pCurrBSS;
							}
						}
					} else {
						if ((pMgmt->eConfigMode == WMAC_CONFIG_AUTO) ||
						    ((pMgmt->eConfigMode == WMAC_CONFIG_IBSS_STA) && WLAN_GET_CAP_INFO_IBSS(pCurrBSS->wCapInfo)) ||
						    ((pMgmt->eConfigMode == WMAC_CONFIG_ESS_STA) && WLAN_GET_CAP_INFO_ESS(pCurrBSS->wCapInfo))
) {
							pCurrBSS->bSelected = true;
							return pCurrBSS;
						}
					}
				}
			}
		}
	} else {
		/* ignore BSSID */
		for (ii = 0; ii < MAX_BSS_NUM; ii++) {
			pCurrBSS = &(pMgmt->sBSSList[ii]);
			/* 2007-0721-01<Add>by MikeLiu */
			pCurrBSS->bSelected = false;
			if (pCurrBSS->bActive) {
				if (pSSID != NULL) {
					/* matched SSID */
					if (!!memcmp(pSSID->abySSID,
						     ((PWLAN_IE_SSID)pCurrBSS->abySSID)->abySSID,
						     pSSID->len) ||
					    (pSSID->len != ((PWLAN_IE_SSID)pCurrBSS->abySSID)->len)) {
						/* SSID not match skip this BSS */
						continue;
					}
				}
				if (((pMgmt->eConfigMode == WMAC_CONFIG_IBSS_STA) && WLAN_GET_CAP_INFO_ESS(pCurrBSS->wCapInfo)) ||
				    ((pMgmt->eConfigMode == WMAC_CONFIG_ESS_STA) && WLAN_GET_CAP_INFO_IBSS(pCurrBSS->wCapInfo))
) {
					/* Type not match skip this BSS */
					DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "BSS type mismatch.... Config[%d] BSS[0x%04x]\n", pMgmt->eConfigMode, pCurrBSS->wCapInfo);
					continue;
				}

				if (ePhyType != PHY_TYPE_AUTO) {
					if (((ePhyType == PHY_TYPE_11A) && (PHY_TYPE_11A != pCurrBSS->eNetworkTypeInUse)) ||
					    ((ePhyType != PHY_TYPE_11A) && (PHY_TYPE_11A == pCurrBSS->eNetworkTypeInUse))) {
						/* PhyType not match skip this BSS */
						DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Physical type mismatch.... ePhyType[%d] BSS[%d]\n", ePhyType, pCurrBSS->eNetworkTypeInUse);
						continue;
					}
				}

				if (pSelect == NULL) {
					pSelect = pCurrBSS;
				} else {
					/* compare RSSI, select signal strong one */
					if (pCurrBSS->uRSSI < pSelect->uRSSI)
						pSelect = pCurrBSS;
				}
			}
		}
		if (pSelect != NULL) {
			pSelect->bSelected = true;
			return pSelect;
		}
	}
	return NULL;
}

/*+
 *
 * Routine Description:
 *    Clear BSS List
 *
 * Return Value:
 *    None.
 *
 -*/

void
BSSvClearBSSList(
	void *hDeviceContext,
	bool bKeepCurrBSSID
)
{
	PSDevice     pDevice = (PSDevice)hDeviceContext;
	PSMgmtObject    pMgmt = pDevice->pMgmt;
	unsigned int ii;

	for (ii = 0; ii < MAX_BSS_NUM; ii++) {
		if (bKeepCurrBSSID) {
			if (pMgmt->sBSSList[ii].bActive &&
			    ether_addr_equal(pMgmt->sBSSList[ii].abyBSSID,
					     pMgmt->abyCurrBSSID)) {
				continue;
			}
		}

		if ((pMgmt->sBSSList[ii].bActive) && (pMgmt->sBSSList[ii].uClearCount < BSS_CLEAR_COUNT)) {
			pMgmt->sBSSList[ii].uClearCount++;
			continue;
		}

		pMgmt->sBSSList[ii].bActive = false;
		memset(&pMgmt->sBSSList[ii], 0, sizeof(KnownBSS));
	}
	BSSvClearAnyBSSJoinRecord(pDevice);

	return;
}

/*+
 *
 * Routine Description:
 *    search BSS list by BSSID & SSID if matched
 *
 * Return Value:
 *    true if found.
 *
 -*/
PKnownBSS
BSSpAddrIsInBSSList(
	void *hDeviceContext,
	unsigned char *abyBSSID,
	PWLAN_IE_SSID pSSID
)
{
	PSDevice     pDevice = (PSDevice)hDeviceContext;
	PSMgmtObject    pMgmt = pDevice->pMgmt;
	PKnownBSS       pBSSList = NULL;
	unsigned int ii;

	for (ii = 0; ii < MAX_BSS_NUM; ii++) {
		pBSSList = &(pMgmt->sBSSList[ii]);
		if (pBSSList->bActive) {
			if (ether_addr_equal(pBSSList->abyBSSID, abyBSSID)) {
				if (pSSID->len == ((PWLAN_IE_SSID)pBSSList->abySSID)->len) {
					if (memcmp(pSSID->abySSID,
						   ((PWLAN_IE_SSID)pBSSList->abySSID)->abySSID,
						   pSSID->len) == 0)
						return pBSSList;
				}
			}
		}
	}

	return NULL;
};

/*+
 *
 * Routine Description:
 *    Insert a BSS set into known BSS list
 *
 * Return Value:
 *    true if success.
 *
 -*/

bool
BSSbInsertToBSSList(
	void *hDeviceContext,
	unsigned char *abyBSSIDAddr,
	QWORD qwTimestamp,
	unsigned short wBeaconInterval,
	unsigned short wCapInfo,
	unsigned char byCurrChannel,
	PWLAN_IE_SSID pSSID,
	PWLAN_IE_SUPP_RATES pSuppRates,
	PWLAN_IE_SUPP_RATES pExtSuppRates,
	PERPObject psERP,
	PWLAN_IE_RSN pRSN,
	PWLAN_IE_RSN_EXT pRSNWPA,
	PWLAN_IE_COUNTRY pIE_Country,
	PWLAN_IE_QUIET pIE_Quiet,
	unsigned int uIELength,
	unsigned char *pbyIEs,
	void *pRxPacketContext
)
{
	PSDevice     pDevice = (PSDevice)hDeviceContext;
	PSMgmtObject    pMgmt = pDevice->pMgmt;
	PSRxMgmtPacket  pRxPacket = (PSRxMgmtPacket)pRxPacketContext;
	PKnownBSS       pBSSList = NULL;
	unsigned int ii;
	bool bParsingQuiet = false;
	PWLAN_IE_QUIET  pQuiet = NULL;

	pBSSList = (PKnownBSS)&(pMgmt->sBSSList[0]);

	for (ii = 0; ii < MAX_BSS_NUM; ii++) {
		pBSSList = (PKnownBSS)&(pMgmt->sBSSList[ii]);
		if (!pBSSList->bActive)
			break;
	}

	if (ii == MAX_BSS_NUM) {
		DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Get free KnowBSS node failed.\n");
		return false;
	}
	/* save the BSS info */
	pBSSList->bActive = true;
	memcpy(pBSSList->abyBSSID, abyBSSIDAddr, WLAN_BSSID_LEN);
	HIDWORD(pBSSList->qwBSSTimestamp) = cpu_to_le32(HIDWORD(qwTimestamp));
	LODWORD(pBSSList->qwBSSTimestamp) = cpu_to_le32(LODWORD(qwTimestamp));
	pBSSList->wBeaconInterval = cpu_to_le16(wBeaconInterval);
	pBSSList->wCapInfo = cpu_to_le16(wCapInfo);
	pBSSList->uClearCount = 0;

	if (pSSID->len > WLAN_SSID_MAXLEN)
		pSSID->len = WLAN_SSID_MAXLEN;
	memcpy(pBSSList->abySSID, pSSID, pSSID->len + WLAN_IEHDR_LEN);

	pBSSList->uChannel = byCurrChannel;

	if (pSuppRates->len > WLAN_RATES_MAXLEN)
		pSuppRates->len = WLAN_RATES_MAXLEN;
	memcpy(pBSSList->abySuppRates, pSuppRates, pSuppRates->len + WLAN_IEHDR_LEN);

	if (pExtSuppRates != NULL) {
		if (pExtSuppRates->len > WLAN_RATES_MAXLEN)
			pExtSuppRates->len = WLAN_RATES_MAXLEN;
		memcpy(pBSSList->abyExtSuppRates, pExtSuppRates, pExtSuppRates->len + WLAN_IEHDR_LEN);
		DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "BSSbInsertToBSSList: pExtSuppRates->len = %d\n", pExtSuppRates->len);

	} else {
		memset(pBSSList->abyExtSuppRates, 0, WLAN_IEHDR_LEN + WLAN_RATES_MAXLEN + 1);
	}
	pBSSList->sERP.byERP = psERP->byERP;
	pBSSList->sERP.bERPExist = psERP->bERPExist;

	/* check if BSS is 802.11a/b/g */
	if (pBSSList->uChannel > CB_MAX_CHANNEL_24G) {
		pBSSList->eNetworkTypeInUse = PHY_TYPE_11A;
	} else {
		if (pBSSList->sERP.bERPExist)
			pBSSList->eNetworkTypeInUse = PHY_TYPE_11G;
		else
			pBSSList->eNetworkTypeInUse = PHY_TYPE_11B;
	}

	pBSSList->byRxRate = pRxPacket->byRxRate;
	pBSSList->qwLocalTSF = pRxPacket->qwLocalTSF;
	pBSSList->uRSSI = pRxPacket->uRSSI;
	pBSSList->bySQ = pRxPacket->bySQ;

	if ((pMgmt->eCurrMode == WMAC_MODE_ESS_STA) &&
	    (pMgmt->eCurrState == WMAC_STATE_ASSOC)) {
		/* assoc with BSS */
		if (pBSSList == pMgmt->pCurrBSS)
			bParsingQuiet = true;
	}

	WPA_ClearRSN(pBSSList);

	if (pRSNWPA != NULL) {
		unsigned int uLen = pRSNWPA->len + 2;

		if (uLen <= (uIELength - (unsigned int)((unsigned char *)pRSNWPA - pbyIEs))) {
			pBSSList->wWPALen = uLen;
			memcpy(pBSSList->byWPAIE, pRSNWPA, uLen);
			WPA_ParseRSN(pBSSList, pRSNWPA);
		}
	}

	WPA2_ClearRSN(pBSSList);

	if (pRSN != NULL) {
		unsigned int uLen = pRSN->len + 2;
		if (uLen <= (uIELength - (unsigned int)((unsigned char *)pRSN - pbyIEs))) {
			pBSSList->wRSNLen = uLen;
			memcpy(pBSSList->byRSNIE, pRSN, uLen);
			WPA2vParseRSN(pBSSList, pRSN);
		}
	}

	if ((pMgmt->eAuthenMode == WMAC_AUTH_WPA2) || pBSSList->bWPA2Valid) {
		PSKeyItem  pTransmitKey = NULL;
		bool bIs802_1x = false;

		for (ii = 0; ii < pBSSList->wAKMSSAuthCount; ii++) {
			if (pBSSList->abyAKMSSAuthType[ii] == WLAN_11i_AKMSS_802_1X) {
				bIs802_1x = true;
				break;
			}
		}
		if (bIs802_1x && (pSSID->len == ((PWLAN_IE_SSID)pMgmt->abyDesireSSID)->len) &&
		    (!memcmp(pSSID->abySSID, ((PWLAN_IE_SSID)pMgmt->abyDesireSSID)->abySSID, pSSID->len))) {
			bAdd_PMKID_Candidate((void *)pDevice, pBSSList->abyBSSID, &pBSSList->sRSNCapObj);

			if (pDevice->bLinkPass && (pMgmt->eCurrState == WMAC_STATE_ASSOC)) {
				if (KeybGetTransmitKey(&(pDevice->sKey), pDevice->abyBSSID, PAIRWISE_KEY, &pTransmitKey) ||
				    KeybGetTransmitKey(&(pDevice->sKey), pDevice->abyBSSID, GROUP_KEY, &pTransmitKey)) {
					pDevice->gsPMKIDCandidate.StatusType = Ndis802_11StatusType_PMKID_CandidateList;
					pDevice->gsPMKIDCandidate.Version = 1;

				}

			}
		}
	}

	if (pDevice->bUpdateBBVGA) {
		/* monitor if RSSI is too strong */
		pBSSList->byRSSIStatCnt = 0;
		RFvRSSITodBm(pDevice, (unsigned char)(pRxPacket->uRSSI), &pBSSList->ldBmMAX);
		pBSSList->ldBmAverage[0] = pBSSList->ldBmMAX;
		for (ii = 1; ii < RSSI_STAT_COUNT; ii++)
			pBSSList->ldBmAverage[ii] = 0;
	}

	if ((pIE_Country != NULL) && pMgmt->b11hEnable) {
		set_country_info(pMgmt->pAdapter, pBSSList->eNetworkTypeInUse,
				 pIE_Country);
	}

	if (bParsingQuiet && (pIE_Quiet != NULL)) {
		if ((((PWLAN_IE_QUIET)pIE_Quiet)->len == 8) &&
		    (((PWLAN_IE_QUIET)pIE_Quiet)->byQuietCount != 0)) {
			/* valid EID */
			if (pQuiet == NULL) {
				pQuiet = (PWLAN_IE_QUIET)pIE_Quiet;
				CARDbSetQuiet(pMgmt->pAdapter,
					      true,
					      pQuiet->byQuietCount,
					      pQuiet->byQuietPeriod,
					      *((unsigned short *)pQuiet->abyQuietDuration),
					      *((unsigned short *)pQuiet->abyQuietOffset)
);
			} else {
				pQuiet = (PWLAN_IE_QUIET)pIE_Quiet;
				CARDbSetQuiet(pMgmt->pAdapter,
					      false,
					      pQuiet->byQuietCount,
					      pQuiet->byQuietPeriod,
					      *((unsigned short *)pQuiet->abyQuietDuration),
					      *((unsigned short *)pQuiet->abyQuietOffset)
					);
			}
		}
	}

	if (bParsingQuiet && (pQuiet != NULL)) {
		CARDbStartQuiet(pMgmt->pAdapter);
	}

	pBSSList->uIELength = uIELength;
	if (pBSSList->uIELength > WLAN_BEACON_FR_MAXLEN)
		pBSSList->uIELength = WLAN_BEACON_FR_MAXLEN;
	memcpy(pBSSList->abyIEs, pbyIEs, pBSSList->uIELength);

	return true;
}

/*+
 *
 * Routine Description:
 *    Update BSS set in known BSS list
 *
 * Return Value:
 *    true if success.
 *
 -*/
/* TODO: input structure modify */

bool
BSSbUpdateToBSSList(
	void *hDeviceContext,
	QWORD qwTimestamp,
	unsigned short wBeaconInterval,
	unsigned short wCapInfo,
	unsigned char byCurrChannel,
	bool bChannelHit,
	PWLAN_IE_SSID pSSID,
	PWLAN_IE_SUPP_RATES pSuppRates,
	PWLAN_IE_SUPP_RATES pExtSuppRates,
	PERPObject psERP,
	PWLAN_IE_RSN pRSN,
	PWLAN_IE_RSN_EXT pRSNWPA,
	PWLAN_IE_COUNTRY pIE_Country,
	PWLAN_IE_QUIET pIE_Quiet,
	PKnownBSS pBSSList,
	unsigned int uIELength,
	unsigned char *pbyIEs,
	void *pRxPacketContext
)
{
	int             ii;
	PSDevice        pDevice = (PSDevice)hDeviceContext;
	PSMgmtObject    pMgmt = pDevice->pMgmt;
	PSRxMgmtPacket  pRxPacket = (PSRxMgmtPacket)pRxPacketContext;
	long            ldBm;
	bool bParsingQuiet = false;
	PWLAN_IE_QUIET  pQuiet = NULL;

	if (pBSSList == NULL)
		return false;

	HIDWORD(pBSSList->qwBSSTimestamp) = cpu_to_le32(HIDWORD(qwTimestamp));
	LODWORD(pBSSList->qwBSSTimestamp) = cpu_to_le32(LODWORD(qwTimestamp));
	pBSSList->wBeaconInterval = cpu_to_le16(wBeaconInterval);
	pBSSList->wCapInfo = cpu_to_le16(wCapInfo);
	pBSSList->uClearCount = 0;
	pBSSList->uChannel = byCurrChannel;

	if (pSSID->len > WLAN_SSID_MAXLEN)
		pSSID->len = WLAN_SSID_MAXLEN;

	if ((pSSID->len != 0) && (pSSID->abySSID[0] != 0))
		memcpy(pBSSList->abySSID, pSSID, pSSID->len + WLAN_IEHDR_LEN);
	memcpy(pBSSList->abySuppRates, pSuppRates, pSuppRates->len + WLAN_IEHDR_LEN);

	if (pExtSuppRates != NULL)
		memcpy(pBSSList->abyExtSuppRates, pExtSuppRates, pExtSuppRates->len + WLAN_IEHDR_LEN);
	else
		memset(pBSSList->abyExtSuppRates, 0, WLAN_IEHDR_LEN + WLAN_RATES_MAXLEN + 1);
	pBSSList->sERP.byERP = psERP->byERP;
	pBSSList->sERP.bERPExist = psERP->bERPExist;

	/* check if BSS is 802.11a/b/g */
	if (pBSSList->uChannel > CB_MAX_CHANNEL_24G) {
		pBSSList->eNetworkTypeInUse = PHY_TYPE_11A;
	} else {
		if (pBSSList->sERP.bERPExist)
			pBSSList->eNetworkTypeInUse = PHY_TYPE_11G;
		else
			pBSSList->eNetworkTypeInUse = PHY_TYPE_11B;
	}

	pBSSList->byRxRate = pRxPacket->byRxRate;
	pBSSList->qwLocalTSF = pRxPacket->qwLocalTSF;
	if (bChannelHit)
		pBSSList->uRSSI = pRxPacket->uRSSI;
	pBSSList->bySQ = pRxPacket->bySQ;

	if ((pMgmt->eCurrMode == WMAC_MODE_ESS_STA) &&
	    (pMgmt->eCurrState == WMAC_STATE_ASSOC)) {
		/* assoc with BSS */
		if (pBSSList == pMgmt->pCurrBSS)
			bParsingQuiet = true;
	}

	WPA_ClearRSN(pBSSList);         /* mike update */

	if (pRSNWPA != NULL) {
		unsigned int uLen = pRSNWPA->len + 2;
		if (uLen <= (uIELength - (unsigned int)((unsigned char *)pRSNWPA - pbyIEs))) {
			pBSSList->wWPALen = uLen;
			memcpy(pBSSList->byWPAIE, pRSNWPA, uLen);
			WPA_ParseRSN(pBSSList, pRSNWPA);
		}
	}

	WPA2_ClearRSN(pBSSList);  /* mike update */

	if (pRSN != NULL) {
		unsigned int uLen = pRSN->len + 2;
		if (uLen <= (uIELength - (unsigned int)((unsigned char *)pRSN - pbyIEs))) {
			pBSSList->wRSNLen = uLen;
			memcpy(pBSSList->byRSNIE, pRSN, uLen);
			WPA2vParseRSN(pBSSList, pRSN);
		}
	}

	if (pRxPacket->uRSSI != 0) {
		RFvRSSITodBm(pDevice, (unsigned char)(pRxPacket->uRSSI), &ldBm);
		/* monitor if RSSI is too strong */
		pBSSList->byRSSIStatCnt++;
		pBSSList->byRSSIStatCnt %= RSSI_STAT_COUNT;
		pBSSList->ldBmAverage[pBSSList->byRSSIStatCnt] = ldBm;
		for (ii = 0; ii < RSSI_STAT_COUNT; ii++) {
			if (pBSSList->ldBmAverage[ii] != 0)
				pBSSList->ldBmMAX = max(pBSSList->ldBmAverage[ii], ldBm);
		}
	}

	if ((pIE_Country != NULL) && pMgmt->b11hEnable) {
		set_country_info(pMgmt->pAdapter, pBSSList->eNetworkTypeInUse,
				 pIE_Country);
	}

	if (bParsingQuiet && (pIE_Quiet != NULL)) {
		if ((((PWLAN_IE_QUIET)pIE_Quiet)->len == 8) &&
		    (((PWLAN_IE_QUIET)pIE_Quiet)->byQuietCount != 0)) {
			/* valid EID */
			if (pQuiet == NULL) {
				pQuiet = (PWLAN_IE_QUIET)pIE_Quiet;
				CARDbSetQuiet(pMgmt->pAdapter,
					      true,
					      pQuiet->byQuietCount,
					      pQuiet->byQuietPeriod,
					      *((unsigned short *)pQuiet->abyQuietDuration),
					      *((unsigned short *)pQuiet->abyQuietOffset)
);
			} else {
				pQuiet = (PWLAN_IE_QUIET)pIE_Quiet;
				CARDbSetQuiet(pMgmt->pAdapter,
					      false,
					      pQuiet->byQuietCount,
					      pQuiet->byQuietPeriod,
					      *((unsigned short *)pQuiet->abyQuietDuration),
					      *((unsigned short *)pQuiet->abyQuietOffset)
					);
			}
		}
	}

	if (bParsingQuiet && (pQuiet != NULL)) {
		CARDbStartQuiet(pMgmt->pAdapter);
	}

	pBSSList->uIELength = uIELength;
	if (pBSSList->uIELength > WLAN_BEACON_FR_MAXLEN)
		pBSSList->uIELength = WLAN_BEACON_FR_MAXLEN;
	memcpy(pBSSList->abyIEs, pbyIEs, pBSSList->uIELength);

	return true;
}

/*+
 *
 * Routine Description:
 *    Search Node DB table to find the index of matched DstAddr
 *
 * Return Value:
 *    None
 *
 -*/

bool
BSSDBbIsSTAInNodeDB(void *pMgmtObject, unsigned char *abyDstAddr,
		    unsigned int *puNodeIndex)
{
	PSMgmtObject    pMgmt = (PSMgmtObject) pMgmtObject;
	unsigned int ii;

	/* Index = 0 reserved for AP Node */
	for (ii = 1; ii < (MAX_NODE_NUM + 1); ii++) {
		if (pMgmt->sNodeDBTable[ii].bActive) {
			if (ether_addr_equal(abyDstAddr,
					     pMgmt->sNodeDBTable[ii].abyMACAddr)) {
				*puNodeIndex = ii;
				return true;
			}
		}
	}

	return false;
};

/*+
 *
 * Routine Description:
 *    Find an empty node and allocat it; if there is no empty node,
 *    then use the most inactive one.
 *
 * Return Value:
 *    None
 *
 -*/
void
BSSvCreateOneNode(void *hDeviceContext, unsigned int *puNodeIndex)
{
	PSDevice     pDevice = (PSDevice)hDeviceContext;
	PSMgmtObject    pMgmt = pDevice->pMgmt;
	unsigned int ii;
	unsigned int BigestCount = 0;
	unsigned int SelectIndex;
	struct sk_buff  *skb;
	/*
	 * Index = 0 reserved for AP Node (In STA mode)
	 * Index = 0 reserved for Broadcast/MultiCast (In AP mode)
	 */
	SelectIndex = 1;
	for (ii = 1; ii < (MAX_NODE_NUM + 1); ii++) {
		if (pMgmt->sNodeDBTable[ii].bActive) {
			if (pMgmt->sNodeDBTable[ii].uInActiveCount > BigestCount) {
				BigestCount = pMgmt->sNodeDBTable[ii].uInActiveCount;
				SelectIndex = ii;
			}
		} else {
			break;
		}
	}

	/* if not found replace uInActiveCount is largest one */
	if (ii == (MAX_NODE_NUM + 1)) {
		*puNodeIndex = SelectIndex;
		DBG_PRT(MSG_LEVEL_NOTICE, KERN_INFO "Replace inactive node = %d\n", SelectIndex);
		/* clear ps buffer */
		if (pMgmt->sNodeDBTable[*puNodeIndex].sTxPSQueue.next != NULL) {
			while ((skb = skb_dequeue(&pMgmt->sNodeDBTable[*puNodeIndex].sTxPSQueue)) != NULL)
				dev_kfree_skb(skb);
		}
	} else {
		*puNodeIndex = ii;
	}

	memset(&pMgmt->sNodeDBTable[*puNodeIndex], 0, sizeof(KnownNodeDB));
	pMgmt->sNodeDBTable[*puNodeIndex].bActive = true;
	pMgmt->sNodeDBTable[*puNodeIndex].uRatePollTimeout = FALLBACK_POLL_SECOND;
	/* for AP mode PS queue */
	skb_queue_head_init(&pMgmt->sNodeDBTable[*puNodeIndex].sTxPSQueue);
	pMgmt->sNodeDBTable[*puNodeIndex].byAuthSequence = 0;
	pMgmt->sNodeDBTable[*puNodeIndex].wEnQueueCnt = 0;
	DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Create node index = %d\n", ii);
	return;
};

/*+
 *
 * Routine Description:
 *    Remove Node by NodeIndex
 *
 *
 * Return Value:
 *    None
 *
 -*/
void
BSSvRemoveOneNode(
	void *hDeviceContext,
	unsigned int uNodeIndex
)
{
	PSDevice        pDevice = (PSDevice)hDeviceContext;
	PSMgmtObject    pMgmt = pDevice->pMgmt;
	unsigned char byMask[8] = {1, 2, 4, 8, 0x10, 0x20, 0x40, 0x80};
	struct sk_buff  *skb;

	while ((skb = skb_dequeue(&pMgmt->sNodeDBTable[uNodeIndex].sTxPSQueue)) != NULL)
		dev_kfree_skb(skb);
	/* clear context */
	memset(&pMgmt->sNodeDBTable[uNodeIndex], 0, sizeof(KnownNodeDB));
	/* clear tx bit map */
	pMgmt->abyPSTxMap[pMgmt->sNodeDBTable[uNodeIndex].wAID >> 3] &=  ~byMask[pMgmt->sNodeDBTable[uNodeIndex].wAID & 7];

	return;
};
/*+
 *
 * Routine Description:
 *    Update AP Node content in Index 0 of KnownNodeDB
 *
 *
 * Return Value:
 *    None
 *
 -*/

void
BSSvUpdateAPNode(
	void *hDeviceContext,
	unsigned short *pwCapInfo,
	PWLAN_IE_SUPP_RATES pSuppRates,
	PWLAN_IE_SUPP_RATES pExtSuppRates
)
{
	PSDevice     pDevice = (PSDevice)hDeviceContext;
	PSMgmtObject    pMgmt = pDevice->pMgmt;
	unsigned int uRateLen = WLAN_RATES_MAXLEN;

	memset(&pMgmt->sNodeDBTable[0], 0, sizeof(KnownNodeDB));

	pMgmt->sNodeDBTable[0].bActive = true;
	if (pDevice->eCurrentPHYType == PHY_TYPE_11B)
		uRateLen = WLAN_RATES_MAXLEN_11B;
	pMgmt->abyCurrSuppRates[1] = RATEuSetIE((PWLAN_IE_SUPP_RATES)pSuppRates,
						(PWLAN_IE_SUPP_RATES)pMgmt->abyCurrSuppRates,
						uRateLen);
	pMgmt->abyCurrExtSuppRates[1] = RATEuSetIE((PWLAN_IE_SUPP_RATES)pExtSuppRates,
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
	memcpy(pMgmt->sNodeDBTable[0].abyMACAddr, pMgmt->abyCurrBSSID, WLAN_ADDR_LEN);
	pMgmt->sNodeDBTable[0].wTxDataRate = pMgmt->sNodeDBTable[0].wMaxSuppRate;
	pMgmt->sNodeDBTable[0].bShortPreamble = WLAN_GET_CAP_INFO_SHORTPREAMBLE(*pwCapInfo);
	pMgmt->sNodeDBTable[0].uRatePollTimeout = FALLBACK_POLL_SECOND;
	netdev_dbg(pDevice->dev, "BSSvUpdateAPNode:MaxSuppRate is %d\n",
		   pMgmt->sNodeDBTable[0].wMaxSuppRate);
	/* auto rate fallback function initiation */
	DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "pMgmt->sNodeDBTable[0].wTxDataRate = %d\n", pMgmt->sNodeDBTable[0].wTxDataRate);
};

/*+
 *
 * Routine Description:
 *    Add Multicast Node content in Index 0 of KnownNodeDB
 *
 *
 * Return Value:
 *    None
 *
 -*/

void
BSSvAddMulticastNode(
	void *hDeviceContext
)
{
	PSDevice     pDevice = (PSDevice)hDeviceContext;
	PSMgmtObject    pMgmt = pDevice->pMgmt;

	if (!pDevice->bEnableHostWEP)
		memset(&pMgmt->sNodeDBTable[0], 0, sizeof(KnownNodeDB));
	memset(pMgmt->sNodeDBTable[0].abyMACAddr, 0xff, WLAN_ADDR_LEN);
	pMgmt->sNodeDBTable[0].bActive = true;
	pMgmt->sNodeDBTable[0].bPSEnable = false;
	skb_queue_head_init(&pMgmt->sNodeDBTable[0].sTxPSQueue);
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
	pMgmt->sNodeDBTable[0].wTxDataRate = pMgmt->sNodeDBTable[0].wMaxBasicRate;
	netdev_dbg(pDevice->dev,
		   "BSSvAddMultiCastNode:pMgmt->sNodeDBTable[0].wTxDataRate is %d\n",
		   pMgmt->sNodeDBTable[0].wTxDataRate);
	pMgmt->sNodeDBTable[0].uRatePollTimeout = FALLBACK_POLL_SECOND;
};

/*+
 *
 * Routine Description:
 *
 *
 *  Second call back function to update Node DB info & AP link status
 *
 *
 * Return Value:
 *    none.
 *
 -*/
/* 2008-4-14 <add> by chester for led issue */
#ifdef FOR_LED_ON_NOTEBOOK
bool cc = false;
unsigned int status;
#endif
void
BSSvSecondCallBack(
	void *hDeviceContext
)
{
	PSDevice        pDevice = (PSDevice)hDeviceContext;
	PSMgmtObject    pMgmt = pDevice->pMgmt;
	unsigned int ii;
	PWLAN_IE_SSID   pItemSSID, pCurrSSID;
	unsigned int uSleepySTACnt = 0;
	unsigned int uNonShortSlotSTACnt = 0;
	unsigned int uLongPreambleSTACnt = 0;
	viawget_wpa_header *wpahdr;  /* DavidWang */

	spin_lock_irq(&pDevice->lock);

	pDevice->uAssocCount = 0;

	pDevice->byERPFlag &=
		~(WLAN_SET_ERP_BARKER_MODE(1) | WLAN_SET_ERP_NONERP_PRESENT(1));
	/* 2008-4-14 <add> by chester for led issue */
#ifdef FOR_LED_ON_NOTEBOOK
	MACvGPIOIn(pDevice->PortOffset, &pDevice->byGPIO);
	if (((!(pDevice->byGPIO & GPIO0_DATA) && (!pDevice->bHWRadioOff)) ||
	     ((pDevice->byGPIO & GPIO0_DATA) && pDevice->bHWRadioOff)) &&
	    (!cc)) {
		cc = true;
	} else if (cc) {
		if (pDevice->bHWRadioOff) {
			if (!(pDevice->byGPIO & GPIO0_DATA)) {
				if (status == 1)
					goto start;
				status = 1;
				CARDbRadioPowerOff(pDevice);
				pMgmt->sNodeDBTable[0].bActive = false;
				pMgmt->eCurrMode = WMAC_MODE_STANDBY;
				pMgmt->eCurrState = WMAC_STATE_IDLE;
				pDevice->bLinkPass = false;

			}
			if (pDevice->byGPIO & GPIO0_DATA) {
				if (status == 2)
					goto start;
				status = 2;
				CARDbRadioPowerOn(pDevice);
			}
		} else {
			if (pDevice->byGPIO & GPIO0_DATA) {
				if (status == 3)
					goto start;
				status = 3;
				CARDbRadioPowerOff(pDevice);
				pMgmt->sNodeDBTable[0].bActive = false;
				pMgmt->eCurrMode = WMAC_MODE_STANDBY;
				pMgmt->eCurrState = WMAC_STATE_IDLE;
				pDevice->bLinkPass = false;

			}
			if (!(pDevice->byGPIO & GPIO0_DATA)) {
				if (status == 4)
					goto start;
				status = 4;
				CARDbRadioPowerOn(pDevice);
			}
		}
	}
start:
#endif

	if (pDevice->wUseProtectCntDown > 0) {
		pDevice->wUseProtectCntDown--;
	} else {
		/* disable protect mode */
		pDevice->byERPFlag &= ~(WLAN_SET_ERP_USE_PROTECTION(1));
	}

	{
		pDevice->byReAssocCount++;
		/* 10 sec timeout */
		if ((pDevice->byReAssocCount > 10) && (!pDevice->bLinkPass)) {
			netdev_info(pDevice->dev, "Re-association timeout!!!\n");
			pDevice->byReAssocCount = 0;
#ifdef WPA_SUPPLICANT_DRIVER_WEXT_SUPPORT
			{
				union iwreq_data  wrqu;
				memset(&wrqu, 0, sizeof(wrqu));
				wrqu.ap_addr.sa_family = ARPHRD_ETHER;
				PRINT_K("wireless_send_event--->SIOCGIWAP(disassociated)\n");
				wireless_send_event(pDevice->dev, SIOCGIWAP, &wrqu, NULL);
			}
#endif
		} else if (pDevice->bLinkPass)
			pDevice->byReAssocCount = 0;
	}

#ifdef Calcu_LinkQual
	s_uCalculateLinkQual((void *)pDevice);
#endif

	for (ii = 0; ii < (MAX_NODE_NUM + 1); ii++) {
		if (pMgmt->sNodeDBTable[ii].bActive) {
			/* increase in-activity counter */
			pMgmt->sNodeDBTable[ii].uInActiveCount++;

			if (ii > 0) {
				if (pMgmt->sNodeDBTable[ii].uInActiveCount > MAX_INACTIVE_COUNT) {
					BSSvRemoveOneNode(pDevice, ii);
					DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO
						"Inactive timeout [%d] sec, STA index = [%d] remove\n", MAX_INACTIVE_COUNT, ii);
					continue;
				}

				if (pMgmt->sNodeDBTable[ii].eNodeState >= NODE_ASSOC) {
					pDevice->uAssocCount++;

					/* check if Non ERP exist */
					if (pMgmt->sNodeDBTable[ii].uInActiveCount < ERP_RECOVER_COUNT) {
						if (!pMgmt->sNodeDBTable[ii].bShortPreamble) {
							pDevice->byERPFlag |= WLAN_SET_ERP_BARKER_MODE(1);
							uLongPreambleSTACnt++;
						}
						if (!pMgmt->sNodeDBTable[ii].bERPExist) {
							pDevice->byERPFlag |= WLAN_SET_ERP_NONERP_PRESENT(1);
							pDevice->byERPFlag |= WLAN_SET_ERP_USE_PROTECTION(1);
						}
						if (!pMgmt->sNodeDBTable[ii].bShortSlotTime)
							uNonShortSlotSTACnt++;
					}
				}

				/* check if any STA in PS mode */
				if (pMgmt->sNodeDBTable[ii].bPSEnable)
					uSleepySTACnt++;

			}

			/* rate fallback check */
			if (!pDevice->bFixRate) {
				if (ii > 0) {
					/* ii = 0 for multicast node (AP & Adhoc) */
					RATEvTxRateFallBack((void *)pDevice, &(pMgmt->sNodeDBTable[ii]));
				} else {
					/* ii = 0 reserved for unicast AP node (Infra STA) */
					if (pMgmt->eCurrMode == WMAC_MODE_ESS_STA)
						netdev_dbg(pDevice->dev,
							   "SecondCallback:Before:TxDataRate is %d\n",
							   pMgmt->sNodeDBTable[0].wTxDataRate);
					RATEvTxRateFallBack((void *)pDevice, &(pMgmt->sNodeDBTable[ii]));
					netdev_dbg(pDevice->dev,
						   "SecondCallback:After:TxDataRate is %d\n",
						   pMgmt->sNodeDBTable[0].wTxDataRate);

				}

			}

			/* check if pending PS queue */
			if (pMgmt->sNodeDBTable[ii].wEnQueueCnt != 0) {
				DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Index= %d, Queue = %d pending\n",
					ii, pMgmt->sNodeDBTable[ii].wEnQueueCnt);
				if ((ii > 0) && (pMgmt->sNodeDBTable[ii].wEnQueueCnt > 15)) {
					BSSvRemoveOneNode(pDevice, ii);
					DBG_PRT(MSG_LEVEL_NOTICE, KERN_INFO "Pending many queues PS STA Index = %d remove\n", ii);
					continue;
				}
			}
		}

	}

	if ((pMgmt->eCurrMode == WMAC_MODE_ESS_AP) && (pDevice->eCurrentPHYType == PHY_TYPE_11G)) {
		/* on/off protect mode */
		if (WLAN_GET_ERP_USE_PROTECTION(pDevice->byERPFlag)) {
			if (!pDevice->bProtectMode) {
				MACvEnableProtectMD(pDevice->PortOffset);
				pDevice->bProtectMode = true;
			}
		} else {
			if (pDevice->bProtectMode) {
				MACvDisableProtectMD(pDevice->PortOffset);
				pDevice->bProtectMode = false;
			}
		}
		/* on/off short slot time */

		if (uNonShortSlotSTACnt > 0) {
			if (pDevice->bShortSlotTime) {
				pDevice->bShortSlotTime = false;
				BBvSetShortSlotTime(pDevice);
				vUpdateIFS((void *)pDevice);
			}
		} else {
			if (!pDevice->bShortSlotTime) {
				pDevice->bShortSlotTime = true;
				BBvSetShortSlotTime(pDevice);
				vUpdateIFS((void *)pDevice);
			}
		}

		/* on/off barker long preamble mode */

		if (uLongPreambleSTACnt > 0) {
			if (!pDevice->bBarkerPreambleMd) {
				MACvEnableBarkerPreambleMd(pDevice->PortOffset);
				pDevice->bBarkerPreambleMd = true;
			}
		} else {
			if (pDevice->bBarkerPreambleMd) {
				MACvDisableBarkerPreambleMd(pDevice->PortOffset);
				pDevice->bBarkerPreambleMd = false;
			}
		}

	}

	/* check if any STA in PS mode, enable DTIM multicast deliver */
	if (pMgmt->eCurrMode == WMAC_MODE_ESS_AP) {
		if (uSleepySTACnt > 0)
			pMgmt->sNodeDBTable[0].bPSEnable = true;
		else
			pMgmt->sNodeDBTable[0].bPSEnable = false;
	}

	pItemSSID = (PWLAN_IE_SSID)pMgmt->abyDesireSSID;
	pCurrSSID = (PWLAN_IE_SSID)pMgmt->abyCurrSSID;

	if ((pMgmt->eCurrMode == WMAC_MODE_STANDBY) ||
	    (pMgmt->eCurrMode == WMAC_MODE_ESS_STA)) {
		/* assoc with BSS */
		if (pMgmt->sNodeDBTable[0].bActive) {
			if (pDevice->bUpdateBBVGA)
				s_vCheckPreEDThreshold((void *)pDevice);

			if ((pMgmt->sNodeDBTable[0].uInActiveCount >= (LOST_BEACON_COUNT/2)) &&
			    (pDevice->byBBVGACurrent != pDevice->abyBBVGA[0])) {
				pDevice->byBBVGANew = pDevice->abyBBVGA[0];
				bScheduleCommand((void *)pDevice, WLAN_CMD_CHANGE_BBSENSITIVITY, NULL);
			}

			if (pMgmt->sNodeDBTable[0].uInActiveCount >= LOST_BEACON_COUNT) {
				pMgmt->sNodeDBTable[0].bActive = false;
				pMgmt->eCurrMode = WMAC_MODE_STANDBY;
				pMgmt->eCurrState = WMAC_STATE_IDLE;
				netif_stop_queue(pDevice->dev);
				pDevice->bLinkPass = false;
				pDevice->bRoaming = true;
				DBG_PRT(MSG_LEVEL_NOTICE, KERN_INFO "Lost AP beacon [%d] sec, disconnected !\n", pMgmt->sNodeDBTable[0].uInActiveCount);
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
					PRINT_K("wireless_send_event--->SIOCGIWAP(disassociated)\n");
					wireless_send_event(pDevice->dev, SIOCGIWAP, &wrqu, NULL);
				}
#endif
			}
		} else if (pItemSSID->len != 0) {
			if (pDevice->uAutoReConnectTime < 10) {
				pDevice->uAutoReConnectTime++;
#ifdef WPA_SUPPLICANT_DRIVER_WEXT_SUPPORT
				/*
				 * network manager support need not do
				 * Roaming scan???
				 */
				if (pDevice->bWPASuppWextEnabled)
					pDevice->uAutoReConnectTime = 0;
#endif
			} else {
				/*
				 * mike use old encryption status
				 * for wpa reauthentication
				 */
				if (pDevice->bWPADEVUp)
					pDevice->eEncryptionStatus = pDevice->eOldEncryptionStatus;

				DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Roaming ...\n");
				BSSvClearBSSList((void *)pDevice, pDevice->bLinkPass);
				pMgmt->eScanType = WMAC_SCAN_ACTIVE;
				bScheduleCommand((void *)pDevice, WLAN_CMD_BSSID_SCAN, pMgmt->abyDesireSSID);
				bScheduleCommand((void *)pDevice, WLAN_CMD_SSID, pMgmt->abyDesireSSID);
				pDevice->uAutoReConnectTime = 0;
			}
		}
	}

	if (pMgmt->eCurrMode == WMAC_MODE_IBSS_STA) {
		/* if adhoc started which essid is NULL string, rescanning */
		if ((pMgmt->eCurrState == WMAC_STATE_STARTED) && (pCurrSSID->len == 0)) {
			if (pDevice->uAutoReConnectTime < 10) {
				pDevice->uAutoReConnectTime++;
			} else {
				DBG_PRT(MSG_LEVEL_NOTICE, KERN_INFO "Adhoc re-scanning ...\n");
				pMgmt->eScanType = WMAC_SCAN_ACTIVE;
				bScheduleCommand((void *)pDevice, WLAN_CMD_BSSID_SCAN, NULL);
				bScheduleCommand((void *)pDevice, WLAN_CMD_SSID, NULL);
				pDevice->uAutoReConnectTime = 0;
			}
		}
		if (pMgmt->eCurrState == WMAC_STATE_JOINTED) {
			if (pDevice->bUpdateBBVGA)
				s_vCheckPreEDThreshold((void *)pDevice);
			if (pMgmt->sNodeDBTable[0].uInActiveCount >= ADHOC_LOST_BEACON_COUNT) {
				DBG_PRT(MSG_LEVEL_NOTICE, KERN_INFO "Lost other STA beacon [%d] sec, started !\n", pMgmt->sNodeDBTable[0].uInActiveCount);
				pMgmt->sNodeDBTable[0].uInActiveCount = 0;
				pMgmt->eCurrState = WMAC_STATE_STARTED;
				netif_stop_queue(pDevice->dev);
				pDevice->bLinkPass = false;
			}
		}
	}

	spin_unlock_irq(&pDevice->lock);

	pMgmt->sTimerSecondCallback.expires = RUN_AT(HZ);
	add_timer(&pMgmt->sTimerSecondCallback);
	return;
}

/*+
 *
 * Routine Description:
 *
 *
 *  Update Tx attemps, Tx failure counter in Node DB
 *
 *
 * Return Value:
 *    none.
 *
 -*/

void
BSSvUpdateNodeTxCounter(
	void *hDeviceContext,
	unsigned char byTsr0,
	unsigned char byTsr1,
	unsigned char *pbyBuffer,
	unsigned int uFIFOHeaderSize
)
{
	PSDevice        pDevice = (PSDevice)hDeviceContext;
	PSMgmtObject    pMgmt = pDevice->pMgmt;
	unsigned int uNodeIndex = 0;
	unsigned char byTxRetry = (byTsr0 & TSR0_NCR);
	PSTxBufHead     pTxBufHead;
	PS802_11Header  pMACHeader;
	unsigned short wRate;
	unsigned short wFallBackRate = RATE_1M;
	unsigned char byFallBack;
	unsigned int ii;
	pTxBufHead = (PSTxBufHead) pbyBuffer;
	if (pTxBufHead->wFIFOCtl & FIFOCTL_AUTO_FB_0)
		byFallBack = AUTO_FB_0;
	else if (pTxBufHead->wFIFOCtl & FIFOCTL_AUTO_FB_1)
		byFallBack = AUTO_FB_1;
	else
		byFallBack = AUTO_FB_NONE;
	wRate = pTxBufHead->wReserved;

	/* Only Unicast using support rates */
	if (pTxBufHead->wFIFOCtl & FIFOCTL_NEEDACK) {
		DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "wRate %04X, byTsr0 %02X, byTsr1 %02X\n", wRate, byTsr0, byTsr1);
		if (pMgmt->eCurrMode == WMAC_MODE_ESS_STA) {
			pMgmt->sNodeDBTable[0].uTxAttempts += 1;
			if ((byTsr1 & TSR1_TERR) == 0) {
				/* transmit success, TxAttempts at least plus one */
				pMgmt->sNodeDBTable[0].uTxOk[MAX_RATE]++;
				if ((byFallBack == AUTO_FB_NONE) ||
				    (wRate < RATE_18M)) {
					wFallBackRate = wRate;
				} else if (byFallBack == AUTO_FB_0) {
					if (byTxRetry < 5)
						wFallBackRate = awHWRetry0[wRate-RATE_18M][byTxRetry];
					else
						wFallBackRate = awHWRetry0[wRate-RATE_18M][4];
				} else if (byFallBack == AUTO_FB_1) {
					if (byTxRetry < 5)
						wFallBackRate = awHWRetry1[wRate-RATE_18M][byTxRetry];
					else
						wFallBackRate = awHWRetry1[wRate-RATE_18M][4];
				}
				pMgmt->sNodeDBTable[0].uTxOk[wFallBackRate]++;
			} else {
				pMgmt->sNodeDBTable[0].uTxFailures++;
			}
			pMgmt->sNodeDBTable[0].uTxRetry += byTxRetry;
			if (byTxRetry != 0) {
				pMgmt->sNodeDBTable[0].uTxFail[MAX_RATE] += byTxRetry;
				if ((byFallBack == AUTO_FB_NONE) ||
				    (wRate < RATE_18M)) {
					pMgmt->sNodeDBTable[0].uTxFail[wRate] += byTxRetry;
				} else if (byFallBack == AUTO_FB_0) {
					for (ii = 0; ii < byTxRetry; ii++) {
						if (ii < 5)
							wFallBackRate = awHWRetry0[wRate-RATE_18M][ii];
						else
							wFallBackRate = awHWRetry0[wRate-RATE_18M][4];
						pMgmt->sNodeDBTable[0].uTxFail[wFallBackRate]++;
					}
				} else if (byFallBack == AUTO_FB_1) {
					for (ii = 0; ii < byTxRetry; ii++) {
						if (ii < 5)
							wFallBackRate = awHWRetry1[wRate-RATE_18M][ii];
						else
							wFallBackRate = awHWRetry1[wRate-RATE_18M][4];
						pMgmt->sNodeDBTable[0].uTxFail[wFallBackRate]++;
					}
				}
			}
		}

		if ((pMgmt->eCurrMode == WMAC_MODE_IBSS_STA) ||
		    (pMgmt->eCurrMode == WMAC_MODE_ESS_AP)) {
			pMACHeader = (PS802_11Header)(pbyBuffer + uFIFOHeaderSize);

			if (BSSDBbIsSTAInNodeDB((void *)pMgmt,  &(pMACHeader->abyAddr1[0]), &uNodeIndex)) {
				pMgmt->sNodeDBTable[uNodeIndex].uTxAttempts += 1;
				if ((byTsr1 & TSR1_TERR) == 0) {
					/* transmit success, TxAttempts at least plus one */
					pMgmt->sNodeDBTable[uNodeIndex].uTxOk[MAX_RATE]++;
					if ((byFallBack == AUTO_FB_NONE) ||
					    (wRate < RATE_18M)) {
						wFallBackRate = wRate;
					} else if (byFallBack == AUTO_FB_0) {
						if (byTxRetry < 5)
							wFallBackRate = awHWRetry0[wRate-RATE_18M][byTxRetry];
						else
							wFallBackRate = awHWRetry0[wRate-RATE_18M][4];
					} else if (byFallBack == AUTO_FB_1) {
						if (byTxRetry < 5)
							wFallBackRate = awHWRetry1[wRate-RATE_18M][byTxRetry];
						else
							wFallBackRate = awHWRetry1[wRate-RATE_18M][4];
					}
					pMgmt->sNodeDBTable[uNodeIndex].uTxOk[wFallBackRate]++;
				} else {
					pMgmt->sNodeDBTable[uNodeIndex].uTxFailures++;
				}
				pMgmt->sNodeDBTable[uNodeIndex].uTxRetry += byTxRetry;
				if (byTxRetry != 0) {
					pMgmt->sNodeDBTable[uNodeIndex].uTxFail[MAX_RATE] += byTxRetry;
					if ((byFallBack == AUTO_FB_NONE) ||
					    (wRate < RATE_18M)) {
						pMgmt->sNodeDBTable[uNodeIndex].uTxFail[wRate] += byTxRetry;
					} else if (byFallBack == AUTO_FB_0) {
						for (ii = 0; ii < byTxRetry; ii++) {
							if (ii < 5)
								wFallBackRate = awHWRetry0[wRate - RATE_18M][ii];
							else
								wFallBackRate = awHWRetry0[wRate - RATE_18M][4];
							pMgmt->sNodeDBTable[uNodeIndex].uTxFail[wFallBackRate]++;
						}
					} else if (byFallBack == AUTO_FB_1) {
						for (ii = 0; ii < byTxRetry; ii++) {
							if (ii < 5)
								wFallBackRate = awHWRetry1[wRate-RATE_18M][ii];
							else
								wFallBackRate = awHWRetry1[wRate-RATE_18M][4];
							pMgmt->sNodeDBTable[uNodeIndex].uTxFail[wFallBackRate]++;
						}
					}
				}
			}
		}
	}

	return;
}

/*+
 *
 * Routine Description:
 *    Clear Nodes & skb in DB Table
 *
 *
 * Parameters:
 *  In:
 *      hDeviceContext        - The adapter context.
 *      uStartIndex           - starting index
 *  Out:
 *      none
 *
 * Return Value:
 *    None.
 *
 -*/

void
BSSvClearNodeDBTable(
	void *hDeviceContext,
	unsigned int uStartIndex
)

{
	PSDevice     pDevice = (PSDevice)hDeviceContext;
	PSMgmtObject    pMgmt = pDevice->pMgmt;
	struct sk_buff  *skb;
	unsigned int ii;

	for (ii = uStartIndex; ii < (MAX_NODE_NUM + 1); ii++) {
		if (pMgmt->sNodeDBTable[ii].bActive) {
			/* check if sTxPSQueue has been initial */
			if (pMgmt->sNodeDBTable[ii].sTxPSQueue.next != NULL) {
				while ((skb = skb_dequeue(&pMgmt->sNodeDBTable[ii].sTxPSQueue)) != NULL) {
					DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "PS skb != NULL %d\n", ii);
					dev_kfree_skb(skb);
				}
			}
			memset(&pMgmt->sNodeDBTable[ii], 0, sizeof(KnownNodeDB));
		}
	}

	return;
};

void s_vCheckSensitivity(
	void *hDeviceContext
)
{
	PSDevice        pDevice = (PSDevice)hDeviceContext;
	PKnownBSS       pBSSList = NULL;
	PSMgmtObject    pMgmt = pDevice->pMgmt;
	int             ii;

	if ((pDevice->byLocalID <= REV_ID_VT3253_A1) && (pDevice->byRFType == RF_RFMD2959) &&
	    (pMgmt->eCurrMode == WMAC_MODE_IBSS_STA)) {
		return;
	}

	if ((pMgmt->eCurrState == WMAC_STATE_ASSOC) ||
	    ((pMgmt->eCurrMode == WMAC_MODE_IBSS_STA) && (pMgmt->eCurrState == WMAC_STATE_JOINTED))) {
		pBSSList = BSSpAddrIsInBSSList(pDevice, pMgmt->abyCurrBSSID, (PWLAN_IE_SSID)pMgmt->abyCurrSSID);
		if (pBSSList != NULL) {
			/* Update BB Reg if RSSI is too strong */
			long    LocalldBmAverage = 0;
			long    uNumofdBm = 0;
			for (ii = 0; ii < RSSI_STAT_COUNT; ii++) {
				if (pBSSList->ldBmAverage[ii] != 0) {
					uNumofdBm++;
					LocalldBmAverage += pBSSList->ldBmAverage[ii];
				}
			}
			if (uNumofdBm > 0) {
				LocalldBmAverage = LocalldBmAverage/uNumofdBm;
				for (ii = 0; ii < BB_VGA_LEVEL; ii++) {
					DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "LocalldBmAverage:%ld, %ld %02x\n", LocalldBmAverage, pDevice->ldBmThreshold[ii], pDevice->abyBBVGA[ii]);
					if (LocalldBmAverage < pDevice->ldBmThreshold[ii]) {
						pDevice->byBBVGANew = pDevice->abyBBVGA[ii];
						break;
					}
				}
				if (pDevice->byBBVGANew != pDevice->byBBVGACurrent) {
					pDevice->uBBVGADiffCount++;
					if (pDevice->uBBVGADiffCount >= BB_VGA_CHANGE_THRESHOLD)
						bScheduleCommand((void *)pDevice, WLAN_CMD_CHANGE_BBSENSITIVITY, NULL);
				} else {
					pDevice->uBBVGADiffCount = 0;
				}
			}
		}
	}
}

void
BSSvClearAnyBSSJoinRecord(
	void *hDeviceContext
)
{
	PSDevice        pDevice = (PSDevice)hDeviceContext;
	PSMgmtObject    pMgmt = pDevice->pMgmt;
	unsigned int ii;

	for (ii = 0; ii < MAX_BSS_NUM; ii++)
		pMgmt->sBSSList[ii].bSelected = false;
	return;
}

#ifdef Calcu_LinkQual
void s_uCalculateLinkQual(
	void *hDeviceContext
)
{
	PSDevice        pDevice = (PSDevice)hDeviceContext;
	unsigned long TxOkRatio, TxCnt;
	unsigned long RxOkRatio, RxCnt;
	unsigned long RssiRatio;
	long ldBm;

	TxCnt = pDevice->scStatistic.TxNoRetryOkCount +
		pDevice->scStatistic.TxRetryOkCount +
		pDevice->scStatistic.TxFailCount;
	RxCnt = pDevice->scStatistic.RxFcsErrCnt +
		pDevice->scStatistic.RxOkCnt;
	TxOkRatio = (TxCnt < 6) ? 4000 : ((pDevice->scStatistic.TxNoRetryOkCount * 4000) / TxCnt);
	RxOkRatio = (RxCnt < 6) ? 2000 : ((pDevice->scStatistic.RxOkCnt * 2000) / RxCnt);
	/* decide link quality */
	if (!pDevice->bLinkPass) {
		pDevice->scStatistic.LinkQuality = 0;
		pDevice->scStatistic.SignalStren = 0;
	} else {
		RFvRSSITodBm(pDevice, (unsigned char)(pDevice->uCurrRSSI), &ldBm);
		if (-ldBm < 50)
			RssiRatio = 4000;
		else if (-ldBm > 90)
			RssiRatio = 0;
		else
			RssiRatio = (40-(-ldBm-50))*4000/40;
		pDevice->scStatistic.SignalStren = RssiRatio/40;
		pDevice->scStatistic.LinkQuality = (RssiRatio+TxOkRatio+RxOkRatio)/100;
	}
	pDevice->scStatistic.RxFcsErrCnt = 0;
	pDevice->scStatistic.RxOkCnt = 0;
	pDevice->scStatistic.TxFailCount = 0;
	pDevice->scStatistic.TxNoRetryOkCount = 0;
	pDevice->scStatistic.TxRetryOkCount = 0;
	return;
}
#endif

void s_vCheckPreEDThreshold(
	void *hDeviceContext
)
{
	PSDevice        pDevice = (PSDevice)hDeviceContext;
	PKnownBSS       pBSSList = NULL;
	PSMgmtObject    pMgmt = &(pDevice->sMgmtObj);

	if ((pMgmt->eCurrState == WMAC_STATE_ASSOC) ||
	    ((pMgmt->eCurrMode == WMAC_MODE_IBSS_STA) && (pMgmt->eCurrState == WMAC_STATE_JOINTED))) {
		pBSSList = BSSpAddrIsInBSSList(pDevice, pMgmt->abyCurrBSSID, (PWLAN_IE_SSID)pMgmt->abyCurrSSID);
		if (pBSSList != NULL)
			pDevice->byBBPreEDRSSI = (unsigned char) (~(pBSSList->ldBmAverRange) + 1);
	}
	return;
}
