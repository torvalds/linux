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
 * File: vntwifi.c
 *
 * Purpose: export functions for vntwifi lib
 *
 * Functions:
 *
 * Revision History:
 *
 * Author: Yiching Chen
 *
 * Date: feb. 2, 2005
 *
 */

#include "vntwifi.h"
#include "IEEE11h.h"
#include "country.h"
#include "device.h"
#include "wmgr.h"
#include "datarate.h"

//#define	PLICE_DEBUG

/*---------------------  Static Definitions -------------------------*/
//static int          msglevel                =MSG_LEVEL_DEBUG;
//static int          msglevel                =MSG_LEVEL_INFO;

/*---------------------  Static Classes  ----------------------------*/

/*---------------------  Static Variables  --------------------------*/

/*---------------------  Static Functions  --------------------------*/

/*---------------------  Export Variables  --------------------------*/

/*---------------------  Export Functions  --------------------------*/

/*+
 *
 * Description:
 *    Set Operation Mode
 *
 * Parameters:
 *  In:
 *      pMgmtHandle - pointer to management object
 *      eOPMode     - Opreation Mode
 *  Out:
 *      none
 *
 * Return Value: none
 *
-*/
VOID
VNTWIFIvSetOPMode (
    IN PVOID pMgmtHandle,
    IN WMAC_CONFIG_MODE eOPMode
    )
{
    PSMgmtObject        pMgmt = (PSMgmtObject)pMgmtHandle;

    pMgmt->eConfigMode = eOPMode;
}


/*+
 *
 * Description:
 *    Set Operation Mode
 *
 * Parameters:
 *  In:
 *      pMgmtHandle - pointer to management object
 *      wBeaconPeriod - Beacon Period
 *      wATIMWindow - ATIM window
 *      uChannel - channel number
 *  Out:
 *      none
 *
 * Return Value: none
 *
-*/
VOID
VNTWIFIvSetIBSSParameter (
    IN PVOID pMgmtHandle,
    IN WORD  wBeaconPeriod,
    IN WORD  wATIMWindow,
    IN UINT  uChannel
    )
{
    PSMgmtObject        pMgmt = (PSMgmtObject)pMgmtHandle;

    pMgmt->wIBSSBeaconPeriod = wBeaconPeriod;
    pMgmt->wIBSSATIMWindow = wATIMWindow;
    pMgmt->uIBSSChannel = uChannel;
}

/*+
 *
 * Description:
 *    Get current SSID
 *
 * Parameters:
 *  In:
 *      pMgmtHandle - pointer to management object
 *  Out:
 *      none
 *
 * Return Value: current SSID pointer.
 *
-*/
PWLAN_IE_SSID
VNTWIFIpGetCurrentSSID (
    IN PVOID pMgmtHandle
    )
{
    PSMgmtObject        pMgmt = (PSMgmtObject)pMgmtHandle;
    return((PWLAN_IE_SSID) pMgmt->abyCurrSSID);
}

/*+
 *
 * Description:
 *    Get current link channel
 *
 * Parameters:
 *  In:
 *      pMgmtHandle - pointer to management object
 *  Out:
 *      none
 *
 * Return Value: current Channel.
 *
-*/
UINT
VNTWIFIpGetCurrentChannel (
    IN PVOID pMgmtHandle
    )
{
    PSMgmtObject        pMgmt = (PSMgmtObject)pMgmtHandle;
    if (pMgmtHandle != NULL) {
        return (pMgmt->uCurrChannel);
    }
    return 0;
}

/*+
 *
 * Description:
 *    Get current Assoc ID
 *
 * Parameters:
 *  In:
 *      pMgmtHandle - pointer to management object
 *  Out:
 *      none
 *
 * Return Value: current Assoc ID
 *
-*/
WORD
VNTWIFIwGetAssocID (
    IN PVOID pMgmtHandle
    )
{
    PSMgmtObject        pMgmt = (PSMgmtObject)pMgmtHandle;
    return(pMgmt->wCurrAID);
}



/*+
 *
 * Description:
 *    This routine return max support rate of IES
 *
 * Parameters:
 *  In:
 *      pSupportRateIEs
 *      pExtSupportRateIEs
 *
 *  Out:
 *
 * Return Value: max support rate
 *
-*/
BYTE
VNTWIFIbyGetMaxSupportRate (
    IN PWLAN_IE_SUPP_RATES pSupportRateIEs,
    IN PWLAN_IE_SUPP_RATES pExtSupportRateIEs
    )
{
    BYTE    byMaxSupportRate = RATE_1M;
    BYTE    bySupportRate = RATE_1M;
    UINT    ii = 0;

    if (pSupportRateIEs) {
        for (ii = 0; ii < pSupportRateIEs->len; ii++) {
            bySupportRate = DATARATEbyGetRateIdx(pSupportRateIEs->abyRates[ii]);
            if (bySupportRate > byMaxSupportRate) {
                byMaxSupportRate = bySupportRate;
            }
        }
    }
    if (pExtSupportRateIEs) {
        for (ii = 0; ii < pExtSupportRateIEs->len; ii++) {
            bySupportRate = DATARATEbyGetRateIdx(pExtSupportRateIEs->abyRates[ii]);
            if (bySupportRate > byMaxSupportRate) {
                byMaxSupportRate = bySupportRate;
            }
        }
    }

    return byMaxSupportRate;
}

/*+
 *
 * Description:
 *    This routine return data rate of ACK packtet
 *
 * Parameters:
 *  In:
 *      byRxDataRate
 *      pSupportRateIEs
 *      pExtSupportRateIEs
 *
 *  Out:
 *
 * Return Value: max support rate
 *
-*/
BYTE
VNTWIFIbyGetACKTxRate (
    IN BYTE byRxDataRate,
    IN PWLAN_IE_SUPP_RATES pSupportRateIEs,
    IN PWLAN_IE_SUPP_RATES pExtSupportRateIEs
    )
{
    BYTE    byMaxAckRate;
    BYTE    byBasicRate;
    UINT    ii;

    if (byRxDataRate <= RATE_11M) {
        byMaxAckRate = RATE_1M;
    } else  {
        // 24M is mandatory for 802.11a and 802.11g
        byMaxAckRate = RATE_24M;
    }
    if (pSupportRateIEs) {
        for (ii = 0; ii < pSupportRateIEs->len; ii++) {
            if (pSupportRateIEs->abyRates[ii] & 0x80) {
                byBasicRate = DATARATEbyGetRateIdx(pSupportRateIEs->abyRates[ii]);
                if ((byBasicRate <= byRxDataRate) &&
                    (byBasicRate > byMaxAckRate))  {
                    byMaxAckRate = byBasicRate;
                }
            }
        }
    }
    if (pExtSupportRateIEs) {
        for (ii = 0; ii < pExtSupportRateIEs->len; ii++) {
            if (pExtSupportRateIEs->abyRates[ii] & 0x80) {
                byBasicRate = DATARATEbyGetRateIdx(pExtSupportRateIEs->abyRates[ii]);
                if ((byBasicRate <= byRxDataRate) &&
                    (byBasicRate > byMaxAckRate))  {
                    byMaxAckRate = byBasicRate;
                }
            }
        }
    }

    return byMaxAckRate;
}

/*+
 *
 * Description:
 *    Set Authentication Mode
 *
 * Parameters:
 *  In:
 *      pMgmtHandle - pointer to management object
 *      eAuthMode   - Authentication mode
 *  Out:
 *      none
 *
 * Return Value: none
 *
-*/
VOID
VNTWIFIvSetAuthenticationMode (
    IN PVOID pMgmtHandle,
    IN WMAC_AUTHENTICATION_MODE eAuthMode
    )
{
    PSMgmtObject        pMgmt = (PSMgmtObject)pMgmtHandle;

    pMgmt->eAuthenMode = eAuthMode;
    if ((eAuthMode == WMAC_AUTH_SHAREKEY) ||
        (eAuthMode == WMAC_AUTH_AUTO)) {
        pMgmt->bShareKeyAlgorithm = TRUE;
    } else {
        pMgmt->bShareKeyAlgorithm = FALSE;
    }
}

/*+
 *
 * Description:
 *    Set Encryption Mode
 *
 * Parameters:
 *  In:
 *      pMgmtHandle - pointer to management object
 *      eAuthMode   - Authentication mode
 *  Out:
 *      none
 *
 * Return Value: none
 *
-*/
VOID
VNTWIFIvSetEncryptionMode (
    IN PVOID pMgmtHandle,
    IN WMAC_ENCRYPTION_MODE eEncryptionMode
    )
{
    PSMgmtObject        pMgmt = (PSMgmtObject)pMgmtHandle;

    pMgmt->eEncryptionMode = eEncryptionMode;
    if ((eEncryptionMode == WMAC_ENCRYPTION_WEPEnabled) ||
        (eEncryptionMode == WMAC_ENCRYPTION_TKIPEnabled) ||
        (eEncryptionMode == WMAC_ENCRYPTION_AESEnabled) ) {
        pMgmt->bPrivacyInvoked = TRUE;
    } else {
        pMgmt->bPrivacyInvoked = FALSE;
    }
}



BOOL
VNTWIFIbConfigPhyMode (
    IN PVOID pMgmtHandle,
    IN CARD_PHY_TYPE ePhyType
    )
{
    PSMgmtObject        pMgmt = (PSMgmtObject)pMgmtHandle;

    if ((ePhyType != PHY_TYPE_AUTO) &&
        (ePhyType != pMgmt->eCurrentPHYMode)) {
        if (CARDbSetPhyParameter(pMgmt->pAdapter, ePhyType, 0, 0, NULL, NULL)==TRUE) {
            pMgmt->eCurrentPHYMode = ePhyType;
        } else {
            return(FALSE);
        }
    }
    pMgmt->eConfigPHYMode = ePhyType;
    return(TRUE);
}


VOID
VNTWIFIbGetConfigPhyMode (
    IN  PVOID pMgmtHandle,
    OUT PVOID pePhyType
    )
{
    PSMgmtObject        pMgmt = (PSMgmtObject)pMgmtHandle;

    if ((pMgmt != NULL) && (pePhyType != NULL)) {
        *(PCARD_PHY_TYPE)pePhyType = pMgmt->eConfigPHYMode;
    }
}

/*+
 *
 * Description:
 *      Clear BSS List Database except current assoc BSS
 *
 * Parameters:
 *  In:
 *      pMgmtHandle     - Management Object structure
 *      bLinkPass       - Current Link status
 *  Out:
 *
 * Return Value: None.
 *
-*/


/*+
 *
 * Description:
 *      Query BSS List in management database
 *
 * Parameters:
 *  In:
 *      pMgmtHandle     - Management Object structure
 *  Out:
 *      puBSSCount      - BSS count
 *      pvFirstBSS      - pointer to first BSS
 *
 * Return Value: None.
 *
-*/

VOID
VNTWIFIvQueryBSSList (
    IN PVOID    pMgmtHandle,
    OUT PUINT   puBSSCount,
    OUT PVOID   *pvFirstBSS
    )
{
    UINT            ii = 0;
    PSMgmtObject    pMgmt = (PSMgmtObject)pMgmtHandle;
    PKnownBSS       pBSS = NULL;
    UINT            uCount = 0;

    *pvFirstBSS = NULL;

    for (ii = 0; ii < MAX_BSS_NUM; ii++) {
        pBSS = &(pMgmt->sBSSList[ii]);
        if (!pBSS->bActive) {
            continue;
        }
        if (*pvFirstBSS == NULL) {
            *pvFirstBSS = &(pMgmt->sBSSList[ii]);
        }
        uCount++;
    }
    *puBSSCount = uCount;
}




VOID
VNTWIFIvGetNextBSS (
    IN PVOID            pMgmtHandle,
    IN PVOID            pvCurrentBSS,
    OUT PVOID           *pvNextBSS
    )
{
    PKnownBSS       pBSS = (PKnownBSS) pvCurrentBSS;
    PSMgmtObject    pMgmt = (PSMgmtObject)pMgmtHandle;

    *pvNextBSS = NULL;

    while (*pvNextBSS == NULL) {
        pBSS++;
        if (pBSS > &(pMgmt->sBSSList[MAX_BSS_NUM])) {
            return;
        }
        if (pBSS->bActive == TRUE) {
            *pvNextBSS = pBSS;
            return;
        }
    }
}





/*+
 *
 * Description:
 *      Update Tx attemps, Tx failure counter in Node DB
 *
 *  In:
 *  Out:
 *      none
 *
 * Return Value: none
 *
-*/
VOID
VNTWIFIvUpdateNodeTxCounter(
    IN PVOID    pMgmtHandle,
    IN PBYTE    pbyDestAddress,
    IN BOOL     bTxOk,
    IN WORD     wRate,
    IN PBYTE    pbyTxFailCount
    )
{
    PSMgmtObject    pMgmt = (PSMgmtObject)pMgmtHandle;
    UINT            uNodeIndex = 0;
    UINT            ii;

    if ((pMgmt->eCurrMode == WMAC_MODE_IBSS_STA) ||
        (pMgmt->eCurrMode == WMAC_MODE_ESS_AP)) {
        if (BSSDBbIsSTAInNodeDB(pMgmt, pbyDestAddress, &uNodeIndex) == FALSE) {
            return;
        }
    }
    pMgmt->sNodeDBTable[uNodeIndex].uTxAttempts++;
    if (bTxOk == TRUE) {
        // transmit success, TxAttempts at least plus one
        pMgmt->sNodeDBTable[uNodeIndex].uTxOk[MAX_RATE]++;
        pMgmt->sNodeDBTable[uNodeIndex].uTxOk[wRate]++;
    } else {
        pMgmt->sNodeDBTable[uNodeIndex].uTxFailures++;
    }
    pMgmt->sNodeDBTable[uNodeIndex].uTxRetry += pbyTxFailCount[MAX_RATE];
    for(ii=0;ii<MAX_RATE;ii++) {
        pMgmt->sNodeDBTable[uNodeIndex].uTxFail[ii] += pbyTxFailCount[ii];
    }
    return;
}


VOID
VNTWIFIvGetTxRate(
    IN PVOID    pMgmtHandle,
    IN PBYTE    pbyDestAddress,
    OUT PWORD   pwTxDataRate,
    OUT PBYTE   pbyACKRate,
    OUT PBYTE   pbyCCKBasicRate,
    OUT PBYTE   pbyOFDMBasicRate
    )
{
    PSMgmtObject        pMgmt = (PSMgmtObject)pMgmtHandle;
    UINT                uNodeIndex = 0;
    WORD                wTxDataRate = RATE_1M;
    BYTE                byACKRate = RATE_1M;
    BYTE                byCCKBasicRate = RATE_1M;
    BYTE                byOFDMBasicRate = RATE_24M;
    PWLAN_IE_SUPP_RATES pSupportRateIEs = NULL;
    PWLAN_IE_SUPP_RATES pExtSupportRateIEs = NULL;


    if ((pMgmt->eCurrMode == WMAC_MODE_IBSS_STA) ||
        (pMgmt->eCurrMode == WMAC_MODE_ESS_AP)) {
        // Adhoc Tx rate decided from node DB
        if(BSSDBbIsSTAInNodeDB(pMgmt, pbyDestAddress, &uNodeIndex)) {
            wTxDataRate = (pMgmt->sNodeDBTable[uNodeIndex].wTxDataRate);
            pSupportRateIEs = (PWLAN_IE_SUPP_RATES) (pMgmt->sNodeDBTable[uNodeIndex].abyCurrSuppRates);
            pExtSupportRateIEs = (PWLAN_IE_SUPP_RATES) (pMgmt->sNodeDBTable[uNodeIndex].abyCurrExtSuppRates);
        } else {
            if (pMgmt->eCurrentPHYMode != PHY_TYPE_11A) {
                wTxDataRate = RATE_2M;
            } else {
                wTxDataRate = RATE_24M;
            }
            pSupportRateIEs = (PWLAN_IE_SUPP_RATES) pMgmt->abyCurrSuppRates;
            pExtSupportRateIEs = (PWLAN_IE_SUPP_RATES) pMgmt->abyCurrExtSuppRates;
        }
    } else { // Infrastructure: rate decided from AP Node, index = 0

		wTxDataRate = (pMgmt->sNodeDBTable[0].wTxDataRate);
#ifdef	PLICE_DEBUG
		printk("GetTxRate:AP MAC is %02x:%02x:%02x:%02x:%02x:%02x,TxRate is %d\n",
				pMgmt->sNodeDBTable[0].abyMACAddr[0],pMgmt->sNodeDBTable[0].abyMACAddr[1],
				pMgmt->sNodeDBTable[0].abyMACAddr[2],pMgmt->sNodeDBTable[0].abyMACAddr[3],
				pMgmt->sNodeDBTable[0].abyMACAddr[4],pMgmt->sNodeDBTable[0].abyMACAddr[5],wTxDataRate);
#endif


        pSupportRateIEs = (PWLAN_IE_SUPP_RATES) pMgmt->abyCurrSuppRates;
        pExtSupportRateIEs = (PWLAN_IE_SUPP_RATES) pMgmt->abyCurrExtSuppRates;
    }
    byACKRate = VNTWIFIbyGetACKTxRate(  (BYTE) wTxDataRate,
                                        pSupportRateIEs,
                                        pExtSupportRateIEs
                                        );
    if (byACKRate > (BYTE) wTxDataRate) {
        byACKRate = (BYTE) wTxDataRate;
    }
    byCCKBasicRate = VNTWIFIbyGetACKTxRate( RATE_11M,
                                            pSupportRateIEs,
                                            pExtSupportRateIEs
                                            );
    byOFDMBasicRate = VNTWIFIbyGetACKTxRate(RATE_54M,
                                            pSupportRateIEs,
                                            pExtSupportRateIEs
                                            );
    *pwTxDataRate = wTxDataRate;
    *pbyACKRate = byACKRate;
    *pbyCCKBasicRate = byCCKBasicRate;
    *pbyOFDMBasicRate = byOFDMBasicRate;
    return;
}

BYTE
VNTWIFIbyGetKeyCypher(
    IN PVOID    pMgmtHandle,
    IN BOOL     bGroupKey
    )
{
    PSMgmtObject    pMgmt = (PSMgmtObject)pMgmtHandle;

    if (bGroupKey == TRUE) {
        return (pMgmt->byCSSGK);
    } else {
        return (pMgmt->byCSSPK);
    }
}


/*
BOOL
VNTWIFIbInit(
    IN PVOID    pAdapterHandler,
    OUT PVOID   *pMgmtHandler
    )
{

    PSMgmtObject        pMgmt = NULL;
    UINT                ii;


    pMgmt = (PSMgmtObject)kmalloc(sizeof(SMgmtObject), (int)GFP_ATOMIC);
    if (pMgmt == NULL) {
        *pMgmtHandler = NULL;
        return FALSE;
    }

    memset(pMgmt, 0, sizeof(SMgmtObject));
    pMgmt->pAdapter = (PVOID) pAdapterHandler;

    // should initial MAC address abyMACAddr
    for(ii=0;ii<WLAN_BSSID_LEN;ii++) {
        pMgmt->abyDesireBSSID[ii] = 0xFF;
    }
    pMgmt->pbyPSPacketPool = &pMgmt->byPSPacketPool[0];
    pMgmt->pbyMgmtPacketPool = &pMgmt->byMgmtPacketPool[0];
    pMgmt->byCSSPK = KEY_CTL_NONE;
    pMgmt->byCSSGK = KEY_CTL_NONE;
    pMgmt->wIBSSBeaconPeriod = DEFAULT_IBSS_BI;

    pMgmt->cbFreeCmdQueue = CMD_Q_SIZE;
    pMgmt->uCmdDequeueIdx = 0;
    pMgmt->uCmdEnqueueIdx = 0;
    pMgmt->eCommandState = WLAN_CMD_STATE_IDLE;
    pMgmt->bCmdStop = FALSE;
    pMgmt->bCmdRunning = FALSE;

    *pMgmtHandler = pMgmt;
    return TRUE;
}
*/



BOOL
VNTWIFIbSetPMKIDCache (
    IN PVOID pMgmtObject,
    IN ULONG ulCount,
    IN PVOID pPMKIDInfo
    )
{
    PSMgmtObject    pMgmt = (PSMgmtObject) pMgmtObject;

    if (ulCount > MAX_PMKID_CACHE) {
        return (FALSE);
    }
    pMgmt->gsPMKIDCache.BSSIDInfoCount = ulCount;
    memcpy(pMgmt->gsPMKIDCache.BSSIDInfo, pPMKIDInfo, (ulCount*sizeof(PMKIDInfo)));
    return (TRUE);
}



WORD
VNTWIFIwGetMaxSupportRate(
    IN PVOID pMgmtObject
    )
{
    WORD wRate = RATE_54M;
    PSMgmtObject    pMgmt = (PSMgmtObject) pMgmtObject;

    for(wRate = RATE_54M; wRate > RATE_1M; wRate--) {
        if (pMgmt->sNodeDBTable[0].wSuppRate & (1<<wRate)) {
            return (wRate);
        }
    }
    if (pMgmt->eCurrentPHYMode == PHY_TYPE_11A) {
        return (RATE_6M);
    } else {
        return (RATE_1M);
    }
}


VOID
VNTWIFIvSet11h (
    IN PVOID pMgmtObject,
    IN BOOL  b11hEnable
    )
{
    PSMgmtObject    pMgmt = (PSMgmtObject) pMgmtObject;

    pMgmt->b11hEnable = b11hEnable;
}

BOOL
VNTWIFIbMeasureReport(
    IN PVOID pMgmtObject,
    IN BOOL  bEndOfReport,
    IN PVOID pvMeasureEID,
    IN BYTE  byReportMode,
    IN BYTE  byBasicMap,
    IN BYTE  byCCAFraction,
    IN PBYTE pbyRPIs
    )
{
    PSMgmtObject    pMgmt = (PSMgmtObject) pMgmtObject;
    PBYTE           pbyCurrentEID = (PBYTE) (pMgmt->pCurrMeasureEIDRep);

    //spin_lock_irq(&pDevice->lock);
    if ((pvMeasureEID != NULL) &&
        (pMgmt->uLengthOfRepEIDs < (WLAN_A3FR_MAXLEN - sizeof(MEASEURE_REP) - sizeof(WLAN_80211HDR_A3) - 3))
        ) {
        pMgmt->pCurrMeasureEIDRep->byElementID = WLAN_EID_MEASURE_REP;
        pMgmt->pCurrMeasureEIDRep->len = 3;
        pMgmt->pCurrMeasureEIDRep->byToken = ((PWLAN_IE_MEASURE_REQ) pvMeasureEID)->byToken;
        pMgmt->pCurrMeasureEIDRep->byMode = byReportMode;
        pMgmt->pCurrMeasureEIDRep->byType = ((PWLAN_IE_MEASURE_REQ) pvMeasureEID)->byType;
        switch (pMgmt->pCurrMeasureEIDRep->byType) {
            case MEASURE_TYPE_BASIC :
                pMgmt->pCurrMeasureEIDRep->len += sizeof(MEASEURE_REP_BASIC);
                memcpy(   &(pMgmt->pCurrMeasureEIDRep->sRep.sBasic),
                            &(((PWLAN_IE_MEASURE_REQ) pvMeasureEID)->sReq),
                            sizeof(MEASEURE_REQ));
                pMgmt->pCurrMeasureEIDRep->sRep.sBasic.byMap = byBasicMap;
                break;
            case MEASURE_TYPE_CCA :
                pMgmt->pCurrMeasureEIDRep->len += sizeof(MEASEURE_REP_CCA);
                memcpy(   &(pMgmt->pCurrMeasureEIDRep->sRep.sCCA),
                            &(((PWLAN_IE_MEASURE_REQ) pvMeasureEID)->sReq),
                            sizeof(MEASEURE_REQ));
                pMgmt->pCurrMeasureEIDRep->sRep.sCCA.byCCABusyFraction = byCCAFraction;
                break;
            case MEASURE_TYPE_RPI :
                pMgmt->pCurrMeasureEIDRep->len += sizeof(MEASEURE_REP_RPI);
                memcpy(   &(pMgmt->pCurrMeasureEIDRep->sRep.sRPI),
                            &(((PWLAN_IE_MEASURE_REQ) pvMeasureEID)->sReq),
                            sizeof(MEASEURE_REQ));
                memcpy(pMgmt->pCurrMeasureEIDRep->sRep.sRPI.abyRPIdensity, pbyRPIs, 8);
                break;
            default :
                break;
        }
        pbyCurrentEID += (2 + pMgmt->pCurrMeasureEIDRep->len);
        pMgmt->uLengthOfRepEIDs += (2 + pMgmt->pCurrMeasureEIDRep->len);
        pMgmt->pCurrMeasureEIDRep = (PWLAN_IE_MEASURE_REP) pbyCurrentEID;
    }
    if (bEndOfReport == TRUE) {
        IEEE11hbMSRRepTx(pMgmt);
    }
    //spin_unlock_irq(&pDevice->lock);
    return (TRUE);
}


BOOL
VNTWIFIbChannelSwitch(
    IN PVOID pMgmtObject,
    IN BYTE  byNewChannel
    )
{
    PSMgmtObject    pMgmt = (PSMgmtObject) pMgmtObject;

    //spin_lock_irq(&pDevice->lock);
    pMgmt->uCurrChannel = byNewChannel;
    pMgmt->bSwitchChannel = FALSE;
    //spin_unlock_irq(&pDevice->lock);
    return TRUE;
}

/*
BOOL
VNTWIFIbRadarPresent(
    IN PVOID pMgmtObject,
    IN BYTE  byChannel
    )
{
    PSMgmtObject    pMgmt = (PSMgmtObject) pMgmtObject;
    if ((pMgmt->eCurrMode == WMAC_MODE_IBSS_STA) &&
        (byChannel == (BYTE) pMgmt->uCurrChannel) &&
        (pMgmt->bSwitchChannel != TRUE) &&
        (pMgmt->b11hEnable == TRUE)) {
        if (IS_ETH_ADDRESS_EQUAL(pMgmt->abyIBSSDFSOwner, CARDpGetCurrentAddress(pMgmt->pAdapter))) {
            pMgmt->byNewChannel = CARDbyAutoChannelSelect(pMgmt->pAdapter,(BYTE) pMgmt->uCurrChannel);
            pMgmt->bSwitchChannel = TRUE;
        }
        BEACONbSendBeacon(pMgmt);
        CARDbChannelSwitch(pMgmt->pAdapter, 0, pMgmt->byNewChannel, 10);
    }
    return TRUE;
}
*/

