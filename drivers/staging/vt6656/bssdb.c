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
 *      BSSbIsSTAInNodeDB - Search Node DB table to find the index of matched DstAddr
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
#include "mac.h"
#include "wpa2.h"
#include "control.h"
#include "rndis.h"
#include "iowpa.h"

/*---------------------  Static Definitions -------------------------*/




/*---------------------  Static Classes  ----------------------------*/

/*---------------------  Static Variables  --------------------------*/
static int          msglevel                =MSG_LEVEL_INFO;
//static int          msglevel                =MSG_LEVEL_DEBUG;



const WORD             awHWRetry0[5][5] = {
                                            {RATE_18M, RATE_18M, RATE_12M, RATE_12M, RATE_12M},
                                            {RATE_24M, RATE_24M, RATE_18M, RATE_12M, RATE_12M},
                                            {RATE_36M, RATE_36M, RATE_24M, RATE_18M, RATE_18M},
                                            {RATE_48M, RATE_48M, RATE_36M, RATE_24M, RATE_24M},
                                            {RATE_54M, RATE_54M, RATE_48M, RATE_36M, RATE_36M}
                                           };
const WORD             awHWRetry1[5][5] = {
                                            {RATE_18M, RATE_18M, RATE_12M, RATE_6M, RATE_6M},
                                            {RATE_24M, RATE_24M, RATE_18M, RATE_6M, RATE_6M},
                                            {RATE_36M, RATE_36M, RATE_24M, RATE_12M, RATE_12M},
                                            {RATE_48M, RATE_48M, RATE_24M, RATE_12M, RATE_12M},
                                            {RATE_54M, RATE_54M, RATE_36M, RATE_18M, RATE_18M}
                                           };



/*---------------------  Static Functions  --------------------------*/

void s_vCheckSensitivity(void *hDeviceContext);
void s_vCheckPreEDThreshold(void *hDeviceContext);
void s_uCalculateLinkQual(void *hDeviceContext);

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

PKnownBSS BSSpSearchBSSList(void *hDeviceContext,
			    PBYTE pbyDesireBSSID,
			    PBYTE pbyDesireSSID,
			    CARD_PHY_TYPE ePhyType)
{
    PSDevice        pDevice = (PSDevice)hDeviceContext;
    PSMgmtObject    pMgmt = &(pDevice->sMgmtObj);
    PBYTE           pbyBSSID = NULL;
    PWLAN_IE_SSID   pSSID = NULL;
    PKnownBSS       pCurrBSS = NULL;
    PKnownBSS       pSelect = NULL;
    BYTE                 ZeroBSSID[WLAN_BSSID_LEN]={0x00,0x00,0x00,0x00,0x00,0x00};
    unsigned int ii = 0;
    unsigned int jj = 0;
    if (pbyDesireBSSID != NULL) {
        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"BSSpSearchBSSList BSSID[%02X %02X %02X-%02X %02X %02X]\n",
                            *pbyDesireBSSID,*(pbyDesireBSSID+1),*(pbyDesireBSSID+2),
                            *(pbyDesireBSSID+3),*(pbyDesireBSSID+4),*(pbyDesireBSSID+5));
	if ((!is_broadcast_ether_addr(pbyDesireBSSID)) &&
	     (memcmp(pbyDesireBSSID, ZeroBSSID, 6)!= 0)){
            pbyBSSID = pbyDesireBSSID;
        }
    }
    if (pbyDesireSSID != NULL) {
        if (((PWLAN_IE_SSID)pbyDesireSSID)->len != 0) {
            pSSID = (PWLAN_IE_SSID) pbyDesireSSID;
        }
    }

    if ((pbyBSSID != NULL)&&(pDevice->bRoaming == FALSE)) {
        // match BSSID first
        for (ii = 0; ii <MAX_BSS_NUM; ii++) {
            pCurrBSS = &(pMgmt->sBSSList[ii]);

	   pCurrBSS->bSelected = FALSE;

            if ((pCurrBSS->bActive) &&
                (pCurrBSS->bSelected == FALSE)) {
		    if (!compare_ether_addr(pCurrBSS->abyBSSID, pbyBSSID)) {
                    if (pSSID != NULL) {
                        // compare ssid
                        if ( !memcmp(pSSID->abySSID,
                            ((PWLAN_IE_SSID)pCurrBSS->abySSID)->abySSID,
                            pSSID->len)) {
                            if ((pMgmt->eConfigMode == WMAC_CONFIG_AUTO) ||
                                ((pMgmt->eConfigMode == WMAC_CONFIG_IBSS_STA) && WLAN_GET_CAP_INFO_IBSS(pCurrBSS->wCapInfo)) ||
                                ((pMgmt->eConfigMode == WMAC_CONFIG_ESS_STA) && WLAN_GET_CAP_INFO_ESS(pCurrBSS->wCapInfo))
                                ) {
                                pCurrBSS->bSelected = TRUE;
                                return(pCurrBSS);
                            }
                        }
                    } else {
                        if ((pMgmt->eConfigMode == WMAC_CONFIG_AUTO) ||
                            ((pMgmt->eConfigMode == WMAC_CONFIG_IBSS_STA) && WLAN_GET_CAP_INFO_IBSS(pCurrBSS->wCapInfo)) ||
                            ((pMgmt->eConfigMode == WMAC_CONFIG_ESS_STA) && WLAN_GET_CAP_INFO_ESS(pCurrBSS->wCapInfo))
                            ) {
                            pCurrBSS->bSelected = TRUE;
                            return(pCurrBSS);
                        }
                    }
                }
            }
        }
    } else {
        // ignore BSSID
        for (ii = 0; ii <MAX_BSS_NUM; ii++) {
            pCurrBSS = &(pMgmt->sBSSList[ii]);

           //2007-0721-01<Mark>by MikeLiu
         //   if ((pCurrBSS->bActive) &&
         //       (pCurrBSS->bSelected == FALSE)) {

	  pCurrBSS->bSelected = FALSE;
          if (pCurrBSS->bActive) {

                if (pSSID != NULL) {
                    // matched SSID
                    if (memcmp(pSSID->abySSID,
                        ((PWLAN_IE_SSID)pCurrBSS->abySSID)->abySSID,
                        pSSID->len) ||
                        (pSSID->len != ((PWLAN_IE_SSID)pCurrBSS->abySSID)->len)) {
                        // SSID not match skip this BSS
                        continue;
                      }
                }
                if (((pMgmt->eConfigMode == WMAC_CONFIG_IBSS_STA) && WLAN_GET_CAP_INFO_ESS(pCurrBSS->wCapInfo)) ||
                    ((pMgmt->eConfigMode == WMAC_CONFIG_ESS_STA) && WLAN_GET_CAP_INFO_IBSS(pCurrBSS->wCapInfo))
                    ){
                    // Type not match skip this BSS
                    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"BSS type mismatch.... Config[%d] BSS[0x%04x]\n", pMgmt->eConfigMode, pCurrBSS->wCapInfo);
                    continue;
                }

                if (ePhyType != PHY_TYPE_AUTO) {
                    if (((ePhyType == PHY_TYPE_11A) && (PHY_TYPE_11A != pCurrBSS->eNetworkTypeInUse)) ||
                        ((ePhyType != PHY_TYPE_11A) && (PHY_TYPE_11A == pCurrBSS->eNetworkTypeInUse))) {
                        // PhyType not match skip this BSS
                        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"Physical type mismatch.... ePhyType[%d] BSS[%d]\n", ePhyType, pCurrBSS->eNetworkTypeInUse);
                        continue;
                    }
                }

        pMgmt->pSameBSS[jj].uChannel = pCurrBSS->uChannel;
        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"BSSpSearchBSSList pSelect1[%02X %02X %02X-%02X %02X %02X]\n",*pCurrBSS->abyBSSID,*(pCurrBSS->abyBSSID+1),*(pCurrBSS->abyBSSID+2),*(pCurrBSS->abyBSSID+3),*(pCurrBSS->abyBSSID+4),*(pCurrBSS->abyBSSID+5));
        jj++;


                if (pSelect == NULL) {
                    pSelect = pCurrBSS;
                } else {
                    // compare RSSI, select signal strong one
                    if (pCurrBSS->uRSSI < pSelect->uRSSI) {
                        pSelect = pCurrBSS;
                    }
                }
            }
        }

pDevice->bSameBSSMaxNum = jj;

        if (pSelect != NULL) {
            pSelect->bSelected = TRUE;
                        if (pDevice->bRoaming == FALSE)  {
	//       Einsn Add @20070907
				memset(pbyDesireSSID, 0, WLAN_IEHDR_LEN + WLAN_SSID_MAXLEN + 1);
			memcpy(pbyDesireSSID,pCurrBSS->abySSID,WLAN_IEHDR_LEN + WLAN_SSID_MAXLEN + 1) ;
                                                }

            return(pSelect);
        }
    }
    return(NULL);

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


void BSSvClearBSSList(void *hDeviceContext, BOOL bKeepCurrBSSID)
{
    PSDevice     pDevice = (PSDevice)hDeviceContext;
    PSMgmtObject    pMgmt = &(pDevice->sMgmtObj);
    unsigned int            ii;

    for (ii = 0; ii < MAX_BSS_NUM; ii++) {
        if (bKeepCurrBSSID) {
            if (pMgmt->sBSSList[ii].bActive &&
		!compare_ether_addr(pMgmt->sBSSList[ii].abyBSSID,
				    pMgmt->abyCurrBSSID)) {
 //mike mark: there are two same BSSID in list if that AP is in hidden ssid mode,one 's SSID is null,
 //                 but other's is obvious, so if it acssociate with your STA  exactly,you must keep two
 //                 of them!!!!!!!!!
               // bKeepCurrBSSID = FALSE;
                continue;
            }
        }

	pMgmt->sBSSList[ii].bActive = FALSE;
        memset(&pMgmt->sBSSList[ii], 0, sizeof(KnownBSS));
    }
    BSSvClearAnyBSSJoinRecord(pDevice);
}



/*+
 *
 * Routine Description:
 *    search BSS list by BSSID & SSID if matched
 *
 * Return Value:
 *    TRUE if found.
 *
-*/
PKnownBSS BSSpAddrIsInBSSList(void *hDeviceContext,
			      PBYTE abyBSSID,
			      PWLAN_IE_SSID pSSID)
{
    PSDevice     pDevice = (PSDevice)hDeviceContext;
    PSMgmtObject    pMgmt = &(pDevice->sMgmtObj);
    PKnownBSS       pBSSList = NULL;
    unsigned int            ii;

    for (ii = 0; ii < MAX_BSS_NUM; ii++) {
        pBSSList = &(pMgmt->sBSSList[ii]);
        if (pBSSList->bActive) {
		if (!compare_ether_addr(pBSSList->abyBSSID, abyBSSID)) {
                if (pSSID->len == ((PWLAN_IE_SSID)pBSSList->abySSID)->len){
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
 *    TRUE if success.
 *
-*/

BOOL BSSbInsertToBSSList(void *hDeviceContext,
			 PBYTE abyBSSIDAddr,
			 QWORD qwTimestamp,
			 WORD wBeaconInterval,
			 WORD wCapInfo,
			 BYTE byCurrChannel,
			 PWLAN_IE_SSID pSSID,
			 PWLAN_IE_SUPP_RATES pSuppRates,
			 PWLAN_IE_SUPP_RATES pExtSuppRates,
			 PERPObject psERP,
			 PWLAN_IE_RSN pRSN,
			 PWLAN_IE_RSN_EXT pRSNWPA,
			 PWLAN_IE_COUNTRY pIE_Country,
			 PWLAN_IE_QUIET pIE_Quiet,
			 unsigned int uIELength,
			 PBYTE pbyIEs,
			 void *pRxPacketContext)
{

    PSDevice     pDevice = (PSDevice)hDeviceContext;
    PSMgmtObject    pMgmt = &(pDevice->sMgmtObj);
    PSRxMgmtPacket  pRxPacket = (PSRxMgmtPacket)pRxPacketContext;
    PKnownBSS       pBSSList = NULL;
    unsigned int            ii;
    BOOL            bParsingQuiet = FALSE;



    pBSSList = (PKnownBSS)&(pMgmt->sBSSList[0]);

    for (ii = 0; ii < MAX_BSS_NUM; ii++) {
        pBSSList = (PKnownBSS)&(pMgmt->sBSSList[ii]);
        if (!pBSSList->bActive)
                break;
    }

    if (ii == MAX_BSS_NUM){
        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Get free KnowBSS node failed.\n");
        return FALSE;
    }
    // save the BSS info
    pBSSList->bActive = TRUE;
    memcpy( pBSSList->abyBSSID, abyBSSIDAddr, WLAN_BSSID_LEN);
    HIDWORD(pBSSList->qwBSSTimestamp) = cpu_to_le32(HIDWORD(qwTimestamp));
    LODWORD(pBSSList->qwBSSTimestamp) = cpu_to_le32(LODWORD(qwTimestamp));
    pBSSList->wBeaconInterval = cpu_to_le16(wBeaconInterval);
    pBSSList->wCapInfo = cpu_to_le16(wCapInfo);
    pBSSList->uClearCount = 0;

    if (pSSID->len > WLAN_SSID_MAXLEN)
        pSSID->len = WLAN_SSID_MAXLEN;
    memcpy( pBSSList->abySSID, pSSID, pSSID->len + WLAN_IEHDR_LEN);

    pBSSList->uChannel = byCurrChannel;

    if (pSuppRates->len > WLAN_RATES_MAXLEN)
        pSuppRates->len = WLAN_RATES_MAXLEN;
    memcpy( pBSSList->abySuppRates, pSuppRates, pSuppRates->len + WLAN_IEHDR_LEN);

    if (pExtSuppRates != NULL) {
        if (pExtSuppRates->len > WLAN_RATES_MAXLEN)
            pExtSuppRates->len = WLAN_RATES_MAXLEN;
        memcpy(pBSSList->abyExtSuppRates, pExtSuppRates, pExtSuppRates->len + WLAN_IEHDR_LEN);
        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"BSSbInsertToBSSList: pExtSuppRates->len = %d\n", pExtSuppRates->len);

    } else {
        memset(pBSSList->abyExtSuppRates, 0, WLAN_IEHDR_LEN + WLAN_RATES_MAXLEN + 1);
    }
    pBSSList->sERP.byERP = psERP->byERP;
    pBSSList->sERP.bERPExist = psERP->bERPExist;

    // Check if BSS is 802.11a/b/g
    if (pBSSList->uChannel > CB_MAX_CHANNEL_24G) {
        pBSSList->eNetworkTypeInUse = PHY_TYPE_11A;
    } else {
        if (pBSSList->sERP.bERPExist == TRUE) {
            pBSSList->eNetworkTypeInUse = PHY_TYPE_11G;
        } else {
            pBSSList->eNetworkTypeInUse = PHY_TYPE_11B;
        }
    }

    pBSSList->byRxRate = pRxPacket->byRxRate;
    pBSSList->qwLocalTSF = pRxPacket->qwLocalTSF;
    pBSSList->uRSSI = pRxPacket->uRSSI;
    pBSSList->bySQ = pRxPacket->bySQ;

   if ((pMgmt->eCurrMode == WMAC_MODE_ESS_STA) &&
        (pMgmt->eCurrState == WMAC_STATE_ASSOC)) {
        // assoc with BSS
        if (pBSSList == pMgmt->pCurrBSS) {
            bParsingQuiet = TRUE;
        }
    }

    WPA_ClearRSN(pBSSList);

    if (pRSNWPA != NULL) {
	unsigned int uLen = pRSNWPA->len + 2;

	if (uLen <= (uIELength -
		     (unsigned int) (ULONG_PTR) ((PBYTE) pRSNWPA - pbyIEs))) {
		pBSSList->wWPALen = uLen;
		memcpy(pBSSList->byWPAIE, pRSNWPA, uLen);
		WPA_ParseRSN(pBSSList, pRSNWPA);
	}
    }

    WPA2_ClearRSN(pBSSList);

    if (pRSN != NULL) {
	unsigned int uLen = pRSN->len + 2;

	if (uLen <= (uIELength -
		     (unsigned int) (ULONG_PTR) ((PBYTE) pRSN - pbyIEs))) {
		pBSSList->wRSNLen = uLen;
		memcpy(pBSSList->byRSNIE, pRSN, uLen);
		WPA2vParseRSN(pBSSList, pRSN);
	}
    }

    if ((pMgmt->eAuthenMode == WMAC_AUTH_WPA2) || (pBSSList->bWPA2Valid == TRUE)) {

        PSKeyItem  pTransmitKey = NULL;
        BOOL       bIs802_1x = FALSE;

        for (ii = 0; ii < pBSSList->wAKMSSAuthCount; ii ++) {
            if (pBSSList->abyAKMSSAuthType[ii] == WLAN_11i_AKMSS_802_1X) {
                bIs802_1x = TRUE;
                break;
            }
        }
        if ((bIs802_1x == TRUE) && (pSSID->len == ((PWLAN_IE_SSID)pMgmt->abyDesireSSID)->len) &&
            ( !memcmp(pSSID->abySSID, ((PWLAN_IE_SSID)pMgmt->abyDesireSSID)->abySSID, pSSID->len))) {

		bAdd_PMKID_Candidate((void *) pDevice,
				     pBSSList->abyBSSID,
				     &pBSSList->sRSNCapObj);

            if ((pDevice->bLinkPass == TRUE) && (pMgmt->eCurrState == WMAC_STATE_ASSOC)) {
                if ((KeybGetTransmitKey(&(pDevice->sKey), pDevice->abyBSSID, PAIRWISE_KEY, &pTransmitKey) == TRUE) ||
                    (KeybGetTransmitKey(&(pDevice->sKey), pDevice->abyBSSID, GROUP_KEY, &pTransmitKey) == TRUE)) {
                    pDevice->gsPMKIDCandidate.StatusType = Ndis802_11StatusType_PMKID_CandidateList;
                    pDevice->gsPMKIDCandidate.Version = 1;

                }

            }
        }
    }

    if (pDevice->bUpdateBBVGA) {
        // Moniter if RSSI is too strong.
        pBSSList->byRSSIStatCnt = 0;
        RFvRSSITodBm(pDevice, (BYTE)(pRxPacket->uRSSI), &pBSSList->ldBmMAX);
        pBSSList->ldBmAverage[0] = pBSSList->ldBmMAX;
        pBSSList->ldBmAverRange = pBSSList->ldBmMAX;
        for (ii = 1; ii < RSSI_STAT_COUNT; ii++)
            pBSSList->ldBmAverage[ii] = 0;
    }

    pBSSList->uIELength = uIELength;
    if (pBSSList->uIELength > WLAN_BEACON_FR_MAXLEN)
        pBSSList->uIELength = WLAN_BEACON_FR_MAXLEN;
    memcpy(pBSSList->abyIEs, pbyIEs, pBSSList->uIELength);

    return TRUE;
}


/*+
 *
 * Routine Description:
 *    Update BSS set in known BSS list
 *
 * Return Value:
 *    TRUE if success.
 *
-*/
// TODO: input structure modify

BOOL BSSbUpdateToBSSList(void *hDeviceContext,
			 QWORD qwTimestamp,
			 WORD wBeaconInterval,
			 WORD wCapInfo,
			 BYTE byCurrChannel,
			 BOOL bChannelHit,
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
			 PBYTE pbyIEs,
			 void *pRxPacketContext)
{
    int             ii, jj;
    PSDevice        pDevice = (PSDevice)hDeviceContext;
    PSMgmtObject    pMgmt = &(pDevice->sMgmtObj);
    PSRxMgmtPacket  pRxPacket = (PSRxMgmtPacket)pRxPacketContext;
    signed long            ldBm, ldBmSum;
    BOOL            bParsingQuiet = FALSE;

    if (pBSSList == NULL)
        return FALSE;


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
    memcpy(pBSSList->abySuppRates, pSuppRates,pSuppRates->len + WLAN_IEHDR_LEN);

    if (pExtSuppRates != NULL) {
        memcpy(pBSSList->abyExtSuppRates, pExtSuppRates,pExtSuppRates->len + WLAN_IEHDR_LEN);
    } else {
        memset(pBSSList->abyExtSuppRates, 0, WLAN_IEHDR_LEN + WLAN_RATES_MAXLEN + 1);
    }
    pBSSList->sERP.byERP = psERP->byERP;
    pBSSList->sERP.bERPExist = psERP->bERPExist;

    // Check if BSS is 802.11a/b/g
    if (pBSSList->uChannel > CB_MAX_CHANNEL_24G) {
        pBSSList->eNetworkTypeInUse = PHY_TYPE_11A;
    } else {
        if (pBSSList->sERP.bERPExist == TRUE) {
            pBSSList->eNetworkTypeInUse = PHY_TYPE_11G;
        } else {
            pBSSList->eNetworkTypeInUse = PHY_TYPE_11B;
        }
    }

    pBSSList->byRxRate = pRxPacket->byRxRate;
    pBSSList->qwLocalTSF = pRxPacket->qwLocalTSF;
    if(bChannelHit)
        pBSSList->uRSSI = pRxPacket->uRSSI;
    pBSSList->bySQ = pRxPacket->bySQ;

   if ((pMgmt->eCurrMode == WMAC_MODE_ESS_STA) &&
        (pMgmt->eCurrState == WMAC_STATE_ASSOC)) {
        // assoc with BSS
        if (pBSSList == pMgmt->pCurrBSS) {
            bParsingQuiet = TRUE;
        }
    }

   WPA_ClearRSN(pBSSList);         //mike update

   if (pRSNWPA != NULL) {
	unsigned int uLen = pRSNWPA->len + 2;
	if (uLen <= (uIELength -
		     (unsigned int) (ULONG_PTR) ((PBYTE) pRSNWPA - pbyIEs))) {
		pBSSList->wWPALen = uLen;
		memcpy(pBSSList->byWPAIE, pRSNWPA, uLen);
		WPA_ParseRSN(pBSSList, pRSNWPA);
	}
   }

   WPA2_ClearRSN(pBSSList);  //mike update

    if (pRSN != NULL) {
	unsigned int uLen = pRSN->len + 2;
	if (uLen <= (uIELength -
			(unsigned int) (ULONG_PTR) ((PBYTE) pRSN - pbyIEs))) {
		pBSSList->wRSNLen = uLen;
		memcpy(pBSSList->byRSNIE, pRSN, uLen);
		WPA2vParseRSN(pBSSList, pRSN);
	}
    }

    if (pRxPacket->uRSSI != 0) {
        RFvRSSITodBm(pDevice, (BYTE)(pRxPacket->uRSSI), &ldBm);
        // Moniter if RSSI is too strong.
        pBSSList->byRSSIStatCnt++;
        pBSSList->byRSSIStatCnt %= RSSI_STAT_COUNT;
        pBSSList->ldBmAverage[pBSSList->byRSSIStatCnt] = ldBm;
        ldBmSum = 0;
	for (ii = 0, jj = 0; ii < RSSI_STAT_COUNT; ii++) {
		if (pBSSList->ldBmAverage[ii] != 0) {
			pBSSList->ldBmMAX =
				max(pBSSList->ldBmAverage[ii], ldBm);
			ldBmSum +=
				pBSSList->ldBmAverage[ii];
			jj++;
		}
        }
        pBSSList->ldBmAverRange = ldBmSum /jj;
    }

    pBSSList->uIELength = uIELength;
    if (pBSSList->uIELength > WLAN_BEACON_FR_MAXLEN)
        pBSSList->uIELength = WLAN_BEACON_FR_MAXLEN;
    memcpy(pBSSList->abyIEs, pbyIEs, pBSSList->uIELength);

    return TRUE;
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

BOOL BSSbIsSTAInNodeDB(void *hDeviceContext,
		       PBYTE abyDstAddr,
		       unsigned int *puNodeIndex)
{
    PSDevice        pDevice = (PSDevice)hDeviceContext;
    PSMgmtObject    pMgmt = &(pDevice->sMgmtObj);
    unsigned int            ii;

    // Index = 0 reserved for AP Node
    for (ii = 1; ii < (MAX_NODE_NUM + 1); ii++) {
        if (pMgmt->sNodeDBTable[ii].bActive) {
		if (!compare_ether_addr(abyDstAddr,
					pMgmt->sNodeDBTable[ii].abyMACAddr)) {
                *puNodeIndex = ii;
                return TRUE;
            }
        }
    }

   return FALSE;
};



/*+
 *
 * Routine Description:
 *    Find an empty node and allocated; if no empty found,
 *    instand used of most inactive one.
 *
 * Return Value:
 *    None
 *
-*/
void BSSvCreateOneNode(void *hDeviceContext, unsigned int *puNodeIndex)
{

    PSDevice     pDevice = (PSDevice)hDeviceContext;
    PSMgmtObject    pMgmt = &(pDevice->sMgmtObj);
    unsigned int            ii;
    unsigned int            BigestCount = 0;
    unsigned int            SelectIndex;
    struct sk_buff  *skb;
    // Index = 0 reserved for AP Node (In STA mode)
    // Index = 0 reserved for Broadcast/MultiCast (In AP mode)
    SelectIndex = 1;
    for (ii = 1; ii < (MAX_NODE_NUM + 1); ii++) {
        if (pMgmt->sNodeDBTable[ii].bActive) {
            if (pMgmt->sNodeDBTable[ii].uInActiveCount > BigestCount) {
                BigestCount = pMgmt->sNodeDBTable[ii].uInActiveCount;
                SelectIndex = ii;
            }
        }
        else {
            break;
        }
    }

    // if not found replace uInActiveCount is largest one.
    if ( ii == (MAX_NODE_NUM + 1)) {
        *puNodeIndex = SelectIndex;
        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Replace inactive node = %d\n", SelectIndex);
        // clear ps buffer
        if (pMgmt->sNodeDBTable[*puNodeIndex].sTxPSQueue.next != NULL) {
      	    while ((skb = skb_dequeue(&pMgmt->sNodeDBTable[*puNodeIndex].sTxPSQueue)) != NULL)
            dev_kfree_skb(skb);
        }
    }
    else {
        *puNodeIndex = ii;
    }

    memset(&pMgmt->sNodeDBTable[*puNodeIndex], 0, sizeof(KnownNodeDB));
    pMgmt->sNodeDBTable[*puNodeIndex].bActive = TRUE;
    pMgmt->sNodeDBTable[*puNodeIndex].uRatePollTimeout = FALLBACK_POLL_SECOND;
    // for AP mode PS queue
    skb_queue_head_init(&pMgmt->sNodeDBTable[*puNodeIndex].sTxPSQueue);
    pMgmt->sNodeDBTable[*puNodeIndex].byAuthSequence = 0;
    pMgmt->sNodeDBTable[*puNodeIndex].wEnQueueCnt = 0;
    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Create node index = %d\n", ii);
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

void BSSvRemoveOneNode(void *hDeviceContext, unsigned int uNodeIndex)
{

    PSDevice        pDevice = (PSDevice)hDeviceContext;
    PSMgmtObject    pMgmt = &(pDevice->sMgmtObj);
    BYTE            byMask[8] = {1, 2, 4, 8, 0x10, 0x20, 0x40, 0x80};
    struct sk_buff  *skb;


    while ((skb = skb_dequeue(&pMgmt->sNodeDBTable[uNodeIndex].sTxPSQueue)) != NULL)
            dev_kfree_skb(skb);
    // clear context
    memset(&pMgmt->sNodeDBTable[uNodeIndex], 0, sizeof(KnownNodeDB));
    // clear tx bit map
    pMgmt->abyPSTxMap[pMgmt->sNodeDBTable[uNodeIndex].wAID >> 3] &=  ~byMask[pMgmt->sNodeDBTable[uNodeIndex].wAID & 7];
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

void BSSvUpdateAPNode(void *hDeviceContext,
		      PWORD pwCapInfo,
		      PWLAN_IE_SUPP_RATES pSuppRates,
		      PWLAN_IE_SUPP_RATES pExtSuppRates)
{
    PSDevice     pDevice = (PSDevice)hDeviceContext;
    PSMgmtObject    pMgmt = &(pDevice->sMgmtObj);
    unsigned int            uRateLen = WLAN_RATES_MAXLEN;

    memset(&pMgmt->sNodeDBTable[0], 0, sizeof(KnownNodeDB));

    pMgmt->sNodeDBTable[0].bActive = TRUE;
    if (pDevice->byBBType == BB_TYPE_11B) {
        uRateLen = WLAN_RATES_MAXLEN_11B;
    }
    pMgmt->abyCurrSuppRates[1] = RATEuSetIE((PWLAN_IE_SUPP_RATES)pSuppRates,
                                            (PWLAN_IE_SUPP_RATES)pMgmt->abyCurrSuppRates,
                                            uRateLen);
    pMgmt->abyCurrExtSuppRates[1] = RATEuSetIE((PWLAN_IE_SUPP_RATES)pExtSuppRates,
                                            (PWLAN_IE_SUPP_RATES)pMgmt->abyCurrExtSuppRates,
                                            uRateLen);
    RATEvParseMaxRate((void *) pDevice,
                       (PWLAN_IE_SUPP_RATES)pMgmt->abyCurrSuppRates,
                       (PWLAN_IE_SUPP_RATES)pMgmt->abyCurrExtSuppRates,
                       TRUE,
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
    // Auto rate fallback function initiation.
    // RATEbInit(pDevice);
    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"pMgmt->sNodeDBTable[0].wTxDataRate = %d \n", pMgmt->sNodeDBTable[0].wTxDataRate);

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

void BSSvAddMulticastNode(void *hDeviceContext)
{
    PSDevice     pDevice = (PSDevice)hDeviceContext;
    PSMgmtObject    pMgmt = &(pDevice->sMgmtObj);

    if (!pDevice->bEnableHostWEP)
        memset(&pMgmt->sNodeDBTable[0], 0, sizeof(KnownNodeDB));
    memset(pMgmt->sNodeDBTable[0].abyMACAddr, 0xff, WLAN_ADDR_LEN);
    pMgmt->sNodeDBTable[0].bActive = TRUE;
    pMgmt->sNodeDBTable[0].bPSEnable = FALSE;
    skb_queue_head_init(&pMgmt->sNodeDBTable[0].sTxPSQueue);
    RATEvParseMaxRate((void *) pDevice,
                      (PWLAN_IE_SUPP_RATES)pMgmt->abyCurrSuppRates,
                      (PWLAN_IE_SUPP_RATES)pMgmt->abyCurrExtSuppRates,
                      TRUE,
                      &(pMgmt->sNodeDBTable[0].wMaxBasicRate),
                      &(pMgmt->sNodeDBTable[0].wMaxSuppRate),
                       &(pMgmt->sNodeDBTable[0].wSuppRate),
                      &(pMgmt->sNodeDBTable[0].byTopCCKBasicRate),
                      &(pMgmt->sNodeDBTable[0].byTopOFDMBasicRate)
                     );
    pMgmt->sNodeDBTable[0].wTxDataRate = pMgmt->sNodeDBTable[0].wMaxBasicRate;
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

void BSSvSecondCallBack(void *hDeviceContext)
{
    PSDevice        pDevice = (PSDevice)hDeviceContext;
    PSMgmtObject    pMgmt = &(pDevice->sMgmtObj);
    unsigned int            ii;
    PWLAN_IE_SSID   pItemSSID, pCurrSSID;
    unsigned int            uSleepySTACnt = 0;
    unsigned int            uNonShortSlotSTACnt = 0;
    unsigned int            uLongPreambleSTACnt = 0;
    viawget_wpa_header *wpahdr;

    spin_lock_irq(&pDevice->lock);

    pDevice->uAssocCount = 0;

    //Power Saving Mode Tx Burst
    if ( pDevice->bEnablePSMode == TRUE ) {
        pDevice->ulPSModeWaitTx++;
        if ( pDevice->ulPSModeWaitTx >= 2 ) {
            pDevice->ulPSModeWaitTx = 0;
            pDevice->bPSModeTxBurst = FALSE;
        }
    }

    pDevice->byERPFlag &=
        ~(WLAN_SET_ERP_BARKER_MODE(1) | WLAN_SET_ERP_NONERP_PRESENT(1));

    if (pDevice->wUseProtectCntDown > 0) {
        pDevice->wUseProtectCntDown --;
    }
    else {
        // disable protect mode
        pDevice->byERPFlag &= ~(WLAN_SET_ERP_USE_PROTECTION(1));
    }

if(pDevice->byReAssocCount > 0) {
       pDevice->byReAssocCount++;
   if((pDevice->byReAssocCount > 10) && (pDevice->bLinkPass != TRUE)) {  //10 sec timeout
                     printk("Re-association timeout!!!\n");
		   pDevice->byReAssocCount = 0;
                     #ifdef WPA_SUPPLICANT_DRIVER_WEXT_SUPPORT
                    // if(pDevice->bWPASuppWextEnabled == TRUE)
                        {
                  	union iwreq_data  wrqu;
                  	memset(&wrqu, 0, sizeof (wrqu));
                          wrqu.ap_addr.sa_family = ARPHRD_ETHER;
                  	PRINT_K("wireless_send_event--->SIOCGIWAP(disassociated)\n");
                  	wireless_send_event(pDevice->dev, SIOCGIWAP, &wrqu, NULL);
                       }
                    #endif
     }
   else if(pDevice->bLinkPass == TRUE)
   	pDevice->byReAssocCount = 0;
}

if((pMgmt->eCurrState!=WMAC_STATE_ASSOC) &&
     (pMgmt->eLastState==WMAC_STATE_ASSOC))
{
  union iwreq_data      wrqu;
  memset(&wrqu, 0, sizeof(wrqu));
  wrqu.data.flags = RT_DISCONNECTED_EVENT_FLAG;
  wireless_send_event(pDevice->dev, IWEVCUSTOM, &wrqu, NULL);
}
 pMgmt->eLastState = pMgmt->eCurrState ;

   s_uCalculateLinkQual((void *)pDevice);

    for (ii = 0; ii < (MAX_NODE_NUM + 1); ii++) {

        if (pMgmt->sNodeDBTable[ii].bActive) {
            // Increase in-activity counter
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

                    // check if Non ERP exist
                    if (pMgmt->sNodeDBTable[ii].uInActiveCount < ERP_RECOVER_COUNT) {
                        if (!pMgmt->sNodeDBTable[ii].bShortPreamble) {
                            pDevice->byERPFlag |= WLAN_SET_ERP_BARKER_MODE(1);
                            uLongPreambleSTACnt ++;
                        }
                        if (!pMgmt->sNodeDBTable[ii].bERPExist) {
                            pDevice->byERPFlag |= WLAN_SET_ERP_NONERP_PRESENT(1);
                            pDevice->byERPFlag |= WLAN_SET_ERP_USE_PROTECTION(1);
                        }
                        if (!pMgmt->sNodeDBTable[ii].bShortSlotTime)
                            uNonShortSlotSTACnt++;
                    }
                }

                // check if any STA in PS mode
                if (pMgmt->sNodeDBTable[ii].bPSEnable)
                    uSleepySTACnt++;


            }

            // Rate fallback check
            if (!pDevice->bFixRate) {
                if (ii > 0) {
                    // ii = 0 for multicast node (AP & Adhoc)
			RATEvTxRateFallBack((void *)pDevice,
					    &(pMgmt->sNodeDBTable[ii]));
                }
                else {
                    // ii = 0 reserved for unicast AP node (Infra STA)
			if (pMgmt->eCurrMode == WMAC_MODE_ESS_STA)
				RATEvTxRateFallBack((void *)pDevice,
						    &(pMgmt->sNodeDBTable[ii]));
                }

            }

            // check if pending PS queue
            if (pMgmt->sNodeDBTable[ii].wEnQueueCnt != 0) {
                DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Index= %d, Queue = %d pending \n",
                           ii, pMgmt->sNodeDBTable[ii].wEnQueueCnt);
                if ((ii >0) && (pMgmt->sNodeDBTable[ii].wEnQueueCnt > 15)) {
                    BSSvRemoveOneNode(pDevice, ii);
                    DBG_PRT(MSG_LEVEL_NOTICE, KERN_INFO "Pending many queues PS STA Index = %d remove \n", ii);
                    continue;
                }
            }
        }

    }


    if ((pMgmt->eCurrMode == WMAC_MODE_ESS_AP) && (pDevice->byBBType == BB_TYPE_11G)) {

        // on/off protect mode
        if (WLAN_GET_ERP_USE_PROTECTION(pDevice->byERPFlag)) {
            if (!pDevice->bProtectMode) {
                MACvEnableProtectMD(pDevice);
                pDevice->bProtectMode = TRUE;
            }
        }
        else {
            if (pDevice->bProtectMode) {
                MACvDisableProtectMD(pDevice);
                pDevice->bProtectMode = FALSE;
            }
        }
        // on/off short slot time

        if (uNonShortSlotSTACnt > 0) {
            if (pDevice->bShortSlotTime) {
                pDevice->bShortSlotTime = FALSE;
                BBvSetShortSlotTime(pDevice);
		vUpdateIFS((void *)pDevice);
            }
        }
        else {
            if (!pDevice->bShortSlotTime) {
                pDevice->bShortSlotTime = TRUE;
                BBvSetShortSlotTime(pDevice);
		vUpdateIFS((void *)pDevice);
            }
        }

        // on/off barker long preamble mode

        if (uLongPreambleSTACnt > 0) {
            if (!pDevice->bBarkerPreambleMd) {
                MACvEnableBarkerPreambleMd(pDevice);
                pDevice->bBarkerPreambleMd = TRUE;
            }
        }
        else {
            if (pDevice->bBarkerPreambleMd) {
                MACvDisableBarkerPreambleMd(pDevice);
                pDevice->bBarkerPreambleMd = FALSE;
            }
        }

    }


    // Check if any STA in PS mode, enable DTIM multicast deliver
    if (pMgmt->eCurrMode == WMAC_MODE_ESS_AP) {
        if (uSleepySTACnt > 0)
            pMgmt->sNodeDBTable[0].bPSEnable = TRUE;
        else
            pMgmt->sNodeDBTable[0].bPSEnable = FALSE;
    }

    pItemSSID = (PWLAN_IE_SSID)pMgmt->abyDesireSSID;
    pCurrSSID = (PWLAN_IE_SSID)pMgmt->abyCurrSSID;

    if ((pMgmt->eCurrMode == WMAC_MODE_STANDBY) ||
        (pMgmt->eCurrMode == WMAC_MODE_ESS_STA)) {

        if (pMgmt->sNodeDBTable[0].bActive) { // Assoc with BSS

            if (pDevice->bUpdateBBVGA) {
		/* s_vCheckSensitivity((void *) pDevice); */
		s_vCheckPreEDThreshold((void *) pDevice);
            }

    	    if ((pMgmt->sNodeDBTable[0].uInActiveCount >= (LOST_BEACON_COUNT/2)) &&
    	        (pDevice->byBBVGACurrent != pDevice->abyBBVGA[0]) ) {
    	        pDevice->byBBVGANew = pDevice->abyBBVGA[0];
		bScheduleCommand((void *) pDevice,
				 WLAN_CMD_CHANGE_BBSENSITIVITY,
				 NULL);
    	    }

        	if (pMgmt->sNodeDBTable[0].uInActiveCount >= LOST_BEACON_COUNT) {
                pMgmt->sNodeDBTable[0].bActive = FALSE;
                pMgmt->eCurrMode = WMAC_MODE_STANDBY;
                pMgmt->eCurrState = WMAC_STATE_IDLE;
                netif_stop_queue(pDevice->dev);
                pDevice->bLinkPass = FALSE;
                ControlvMaskByte(pDevice,MESSAGE_REQUEST_MACREG,MAC_REG_PAPEDELAY,LEDSTS_STS,LEDSTS_SLOW);
                pDevice->bRoaming = TRUE;
                pDevice->bIsRoaming = FALSE;

                DBG_PRT(MSG_LEVEL_NOTICE, KERN_INFO "Lost AP beacon [%d] sec, disconnected !\n", pMgmt->sNodeDBTable[0].uInActiveCount);
		/* let wpa supplicant know AP may disconnect */
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
	memset(&wrqu, 0, sizeof (wrqu));
        wrqu.ap_addr.sa_family = ARPHRD_ETHER;
	PRINT_K("wireless_send_event--->SIOCGIWAP(disassociated)\n");
	wireless_send_event(pDevice->dev, SIOCGIWAP, &wrqu, NULL);
     }
  #endif
            }
        }
        else if (pItemSSID->len != 0) {
//Davidwang
      if ((pDevice->bEnableRoaming == TRUE)&&(!(pMgmt->Cisco_cckm))) {
DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "bRoaming %d, !\n", pDevice->bRoaming );
DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "bIsRoaming %d, !\n", pDevice->bIsRoaming );
          if ((pDevice->bRoaming == TRUE)&&(pDevice->bIsRoaming == TRUE)){
	    	DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Fast   Roaming ...\n");
		BSSvClearBSSList((void *) pDevice, pDevice->bLinkPass);
		bScheduleCommand((void *) pDevice,
				 WLAN_CMD_BSSID_SCAN,
				 pMgmt->abyDesireSSID);
		bScheduleCommand((void *) pDevice,
				 WLAN_CMD_SSID,
				 pMgmt->abyDesireSSID);
                pDevice->uAutoReConnectTime = 0;
                pDevice->uIsroamingTime = 0;
                pDevice->bRoaming = FALSE;

             wpahdr = (viawget_wpa_header *)pDevice->skb->data;
             wpahdr->type = VIAWGET_CCKM_ROAM_MSG;
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
      else if ((pDevice->bRoaming == FALSE)&&(pDevice->bIsRoaming == TRUE)) {
                            pDevice->uIsroamingTime++;
       if (pDevice->uIsroamingTime >= 20)
            pDevice->bIsRoaming = FALSE;
         }

   }
else {
            if (pDevice->uAutoReConnectTime < 10) {
                pDevice->uAutoReConnectTime++;
               #ifdef WPA_SUPPLICANT_DRIVER_WEXT_SUPPORT
                //network manager support need not do Roaming scan???
                if(pDevice->bWPASuppWextEnabled ==TRUE)
		 pDevice->uAutoReConnectTime = 0;
	     #endif
            }
            else {
	    //mike use old encryption status for wpa reauthen
	      if(pDevice->bWPADEVUp)
	          pDevice->eEncryptionStatus = pDevice->eOldEncryptionStatus;

                DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Roaming ...\n");
		BSSvClearBSSList((void *) pDevice, pDevice->bLinkPass);
		pMgmt->eScanType = WMAC_SCAN_ACTIVE;
		bScheduleCommand((void *) pDevice,
				 WLAN_CMD_BSSID_SCAN,
				 pMgmt->abyDesireSSID);
		bScheduleCommand((void *) pDevice,
				 WLAN_CMD_SSID,
				 pMgmt->abyDesireSSID);
                pDevice->uAutoReConnectTime = 0;
            }
        }
    }
    }

    if (pMgmt->eCurrMode == WMAC_MODE_IBSS_STA) {
        // if adhoc started which essid is NULL string, rescanning.
        if ((pMgmt->eCurrState == WMAC_STATE_STARTED) && (pCurrSSID->len == 0)) {
            if (pDevice->uAutoReConnectTime < 10) {
                pDevice->uAutoReConnectTime++;
            }
            else {
                DBG_PRT(MSG_LEVEL_NOTICE, KERN_INFO "Adhoc re-scanning ...\n");
	       pMgmt->eScanType = WMAC_SCAN_ACTIVE;
		bScheduleCommand((void *) pDevice, WLAN_CMD_BSSID_SCAN, NULL);
		bScheduleCommand((void *) pDevice, WLAN_CMD_SSID, NULL);
                pDevice->uAutoReConnectTime = 0;
            };
        }
        if (pMgmt->eCurrState == WMAC_STATE_JOINTED) {

		if (pDevice->bUpdateBBVGA) {
			/* s_vCheckSensitivity((void *) pDevice); */
			s_vCheckPreEDThreshold((void *) pDevice);
		}
        	if (pMgmt->sNodeDBTable[0].uInActiveCount >=ADHOC_LOST_BEACON_COUNT) {
        	    DBG_PRT(MSG_LEVEL_NOTICE, KERN_INFO "Lost other STA beacon [%d] sec, started !\n", pMgmt->sNodeDBTable[0].uInActiveCount);
                pMgmt->sNodeDBTable[0].uInActiveCount = 0;
                pMgmt->eCurrState = WMAC_STATE_STARTED;
                netif_stop_queue(pDevice->dev);
                pDevice->bLinkPass = FALSE;
                ControlvMaskByte(pDevice,MESSAGE_REQUEST_MACREG,MAC_REG_PAPEDELAY,LEDSTS_STS,LEDSTS_SLOW);
            }
        }
    }

    if (pDevice->bLinkPass == TRUE) {
        if (netif_queue_stopped(pDevice->dev))
            netif_wake_queue(pDevice->dev);
    }

    spin_unlock_irq(&pDevice->lock);

    pMgmt->sTimerSecondCallback.expires = RUN_AT(HZ);
    add_timer(&pMgmt->sTimerSecondCallback);
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

void BSSvUpdateNodeTxCounter(void *hDeviceContext,
			     PSStatCounter pStatistic,
			     BYTE byTSR,
			     BYTE byPktNO)
{
    PSDevice        pDevice = (PSDevice)hDeviceContext;
    PSMgmtObject    pMgmt = &(pDevice->sMgmtObj);
    unsigned int            uNodeIndex = 0;
    BYTE            byTxRetry;
    WORD            wRate;
    WORD            wFallBackRate = RATE_1M;
    BYTE            byFallBack;
    unsigned int            ii;
    PBYTE           pbyDestAddr;
    BYTE            byPktNum;
    WORD            wFIFOCtl;

    byPktNum = (byPktNO & 0x0F) >> 4;
    byTxRetry = (byTSR & 0xF0) >> 4;
    wRate = (WORD) (byPktNO & 0xF0) >> 4;
    wFIFOCtl = pStatistic->abyTxPktInfo[byPktNum].wFIFOCtl;
    pbyDestAddr = (PBYTE) &( pStatistic->abyTxPktInfo[byPktNum].abyDestAddr[0]);

    if (wFIFOCtl & FIFOCTL_AUTO_FB_0) {
        byFallBack = AUTO_FB_0;
    } else if (wFIFOCtl & FIFOCTL_AUTO_FB_1) {
        byFallBack = AUTO_FB_1;
    } else {
        byFallBack = AUTO_FB_NONE;
    }

    // Only Unicast using support rates
    if (wFIFOCtl & FIFOCTL_NEEDACK) {
        if (pMgmt->eCurrMode == WMAC_MODE_ESS_STA) {
            pMgmt->sNodeDBTable[0].uTxAttempts += 1;
            if ( !(byTSR & (TSR_TMO | TSR_RETRYTMO))) {
                // transmit success, TxAttempts at least plus one
                pMgmt->sNodeDBTable[0].uTxOk[MAX_RATE]++;
                if ( (byFallBack == AUTO_FB_NONE) ||
                     (wRate < RATE_18M) ) {
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
                pMgmt->sNodeDBTable[0].uTxFailures ++;
            }
            pMgmt->sNodeDBTable[0].uTxRetry += byTxRetry;
            if (byTxRetry != 0) {
                pMgmt->sNodeDBTable[0].uTxFail[MAX_RATE]+=byTxRetry;
                if ( (byFallBack == AUTO_FB_NONE) ||
                     (wRate < RATE_18M) ) {
                    pMgmt->sNodeDBTable[0].uTxFail[wRate]+=byTxRetry;
                } else if (byFallBack == AUTO_FB_0) {
			for (ii = 0; ii < byTxRetry; ii++) {
				if (ii < 5)
					wFallBackRate =
						awHWRetry0[wRate-RATE_18M][ii];
				else
					wFallBackRate =
						awHWRetry0[wRate-RATE_18M][4];
				pMgmt->sNodeDBTable[0].uTxFail[wFallBackRate]++;
			}
                } else if (byFallBack == AUTO_FB_1) {
			for (ii = 0; ii < byTxRetry; ii++) {
				if (ii < 5)
					wFallBackRate =
						awHWRetry1[wRate-RATE_18M][ii];
				else
					wFallBackRate =
						awHWRetry1[wRate-RATE_18M][4];
				pMgmt->sNodeDBTable[0].uTxFail[wFallBackRate]++;
			}
                }
            }
        }

	if ((pMgmt->eCurrMode == WMAC_MODE_IBSS_STA) ||
            (pMgmt->eCurrMode == WMAC_MODE_ESS_AP)) {

		if (BSSbIsSTAInNodeDB((void *) pDevice,
				      pbyDestAddr,
				      &uNodeIndex)) {
			pMgmt->sNodeDBTable[uNodeIndex].uTxAttempts += 1;
                if ( !(byTSR & (TSR_TMO | TSR_RETRYTMO))) {
                    // transmit success, TxAttempts at least plus one
                    pMgmt->sNodeDBTable[uNodeIndex].uTxOk[MAX_RATE]++;
                    if ( (byFallBack == AUTO_FB_NONE) ||
                         (wRate < RATE_18M) ) {
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
                    pMgmt->sNodeDBTable[uNodeIndex].uTxFailures ++;
                }
                pMgmt->sNodeDBTable[uNodeIndex].uTxRetry += byTxRetry;
                if (byTxRetry != 0) {
                    pMgmt->sNodeDBTable[uNodeIndex].uTxFail[MAX_RATE]+=byTxRetry;
                    if ( (byFallBack == AUTO_FB_NONE) ||
                         (wRate < RATE_18M) ) {
                        pMgmt->sNodeDBTable[uNodeIndex].uTxFail[wRate]+=byTxRetry;
                    } else if (byFallBack == AUTO_FB_0) {
			for (ii = 0; ii < byTxRetry; ii++) {
				if (ii < 5)
					wFallBackRate =
						awHWRetry0[wRate-RATE_18M][ii];
				else
					wFallBackRate =
						awHWRetry0[wRate-RATE_18M][4];
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

void BSSvClearNodeDBTable(void *hDeviceContext,
			  unsigned int uStartIndex)
{
    PSDevice     pDevice = (PSDevice)hDeviceContext;
    PSMgmtObject    pMgmt = &(pDevice->sMgmtObj);
    struct sk_buff  *skb;
    unsigned int            ii;

    for (ii = uStartIndex; ii < (MAX_NODE_NUM + 1); ii++) {
        if (pMgmt->sNodeDBTable[ii].bActive) {
            // check if sTxPSQueue has been initial
            if (pMgmt->sNodeDBTable[ii].sTxPSQueue.next != NULL) {
                while ((skb = skb_dequeue(&pMgmt->sNodeDBTable[ii].sTxPSQueue)) != NULL){
                        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "PS skb != NULL %d\n", ii);
                        dev_kfree_skb(skb);
                }
            }
            memset(&pMgmt->sNodeDBTable[ii], 0, sizeof(KnownNodeDB));
        }
    }
};

void s_vCheckSensitivity(void *hDeviceContext)
{
    PSDevice        pDevice = (PSDevice)hDeviceContext;
    PKnownBSS       pBSSList = NULL;
    PSMgmtObject    pMgmt = &(pDevice->sMgmtObj);
    int             ii;

    if ((pMgmt->eCurrState == WMAC_STATE_ASSOC) ||
        ((pMgmt->eCurrMode == WMAC_MODE_IBSS_STA) && (pMgmt->eCurrState == WMAC_STATE_JOINTED))) {
        pBSSList = BSSpAddrIsInBSSList(pDevice, pMgmt->abyCurrBSSID, (PWLAN_IE_SSID)pMgmt->abyCurrSSID);
        if (pBSSList != NULL) {
		/* Update BB register if RSSI is too strong */
		signed long    LocalldBmAverage = 0;
		signed long    uNumofdBm = 0;
            for (ii = 0; ii < RSSI_STAT_COUNT; ii++) {
                if (pBSSList->ldBmAverage[ii] != 0) {
                    uNumofdBm ++;
                    LocalldBmAverage += pBSSList->ldBmAverage[ii];
                }
            }
            if (uNumofdBm > 0) {
                LocalldBmAverage = LocalldBmAverage/uNumofdBm;
                for (ii=0;ii<BB_VGA_LEVEL;ii++) {
                    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"LocalldBmAverage:%ld, %ld %02x\n", LocalldBmAverage, pDevice->ldBmThreshold[ii], pDevice->abyBBVGA[ii]);
                    if (LocalldBmAverage < pDevice->ldBmThreshold[ii]) {
                	    pDevice->byBBVGANew = pDevice->abyBBVGA[ii];
                        break;
                    }
                }
                if (pDevice->byBBVGANew != pDevice->byBBVGACurrent) {
                    pDevice->uBBVGADiffCount++;
                    if (pDevice->uBBVGADiffCount >= BB_VGA_CHANGE_THRESHOLD)
			bScheduleCommand((void *) pDevice,
					 WLAN_CMD_CHANGE_BBSENSITIVITY,
					 NULL);
                } else {
                    pDevice->uBBVGADiffCount = 0;
                }
            }
        }
    }
}

void s_uCalculateLinkQual(void *hDeviceContext)
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
TxOkRatio = (TxCnt < 6) ? 4000:((pDevice->scStatistic.TxNoRetryOkCount * 4000) / TxCnt);
RxOkRatio = (RxCnt < 6) ? 2000:((pDevice->scStatistic.RxOkCnt * 2000) / RxCnt);
//decide link quality
if(pDevice->bLinkPass !=TRUE)
{
   pDevice->scStatistic.LinkQuality = 0;
   pDevice->scStatistic.SignalStren = 0;
}
else
{
   RFvRSSITodBm(pDevice, (BYTE)(pDevice->uCurrRSSI), &ldBm);
   if(-ldBm < 50)  {
   	RssiRatio = 4000;
     }
   else if(-ldBm > 90) {
   	RssiRatio = 0;
     }
   else {
   	RssiRatio = (40-(-ldBm-50))*4000/40;
     }
   pDevice->scStatistic.SignalStren = RssiRatio/40;
   pDevice->scStatistic.LinkQuality = (RssiRatio+TxOkRatio+RxOkRatio)/100;
}
   pDevice->scStatistic.RxFcsErrCnt = 0;
   pDevice->scStatistic.RxOkCnt = 0;
   pDevice->scStatistic.TxFailCount = 0;
   pDevice->scStatistic.TxNoRetryOkCount = 0;
   pDevice->scStatistic.TxRetryOkCount = 0;
}

void BSSvClearAnyBSSJoinRecord(void *hDeviceContext)
{
    PSDevice        pDevice = (PSDevice)hDeviceContext;
    PSMgmtObject    pMgmt = &(pDevice->sMgmtObj);
    unsigned int            ii;

	for (ii = 0; ii < MAX_BSS_NUM; ii++)
        pMgmt->sBSSList[ii].bSelected = FALSE;
}

void s_vCheckPreEDThreshold(void *hDeviceContext)
{
    PSDevice        pDevice = (PSDevice)hDeviceContext;
    PKnownBSS       pBSSList = NULL;
    PSMgmtObject    pMgmt = &(pDevice->sMgmtObj);

    if ((pMgmt->eCurrState == WMAC_STATE_ASSOC) ||
        ((pMgmt->eCurrMode == WMAC_MODE_IBSS_STA) && (pMgmt->eCurrState == WMAC_STATE_JOINTED))) {
        pBSSList = BSSpAddrIsInBSSList(pDevice, pMgmt->abyCurrBSSID, (PWLAN_IE_SSID)pMgmt->abyCurrSSID);
        if (pBSSList != NULL) {
            pDevice->byBBPreEDRSSI = (BYTE) (~(pBSSList->ldBmAverRange) + 1);
            BBvUpdatePreEDThreshold(pDevice, FALSE);
        }
    }
}

