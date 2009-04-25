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
 * File: power.c
 *
 * Purpose: Handles 802.11 power managment  functions
 *
 * Author: Lyndon Chen
 *
 * Date: July 17, 2002
 *
 * Functions:
 *      PSvEnablePowerSaving - Enable Power Saving Mode
 *      PSvDiasblePowerSaving - Disable Power Saving Mode
 *      PSbConsiderPowerDown - Decide if we can Power Down
 *      PSvSendPSPOLL - Send PS-POLL packet
 *      PSbSendNullPacket - Send Null packet
 *      PSbIsNextTBTTWakeUp - Decide if we need to wake up at next Beacon
 *
 * Revision History:
 *
 */



#if !defined(__TTYPE_H__)
#include "ttype.h"
#endif
#if !defined(__TBIT_H__)
#include "tbit.h"
#endif
#if !defined(__MAC_H__)
#include "mac.h"
#endif
#if !defined(__DEVICE_H__)
#include "device.h"
#endif
#if !defined(__WMGR_H__)
#include "wmgr.h"
#endif
#if !defined(__POWER_H__)
#include "power.h"
#endif
#if !defined(__WCMD_H__)
#include "wcmd.h"
#endif
#if !defined(__TBIT_H__)
#include "tbit.h"
#endif
#if !defined(__UMEM_H__)
#include "umem.h"
#endif
#if !defined(__RXTX_H__)
#include "rxtx.h"
#endif
#if !defined(__CARD_H__)
#include "card.h"
#endif




/*---------------------  Static Definitions -------------------------*/




/*---------------------  Static Classes  ----------------------------*/

/*---------------------  Static Variables  --------------------------*/
static int          msglevel                =MSG_LEVEL_INFO;
/*---------------------  Static Functions  --------------------------*/


/*---------------------  Export Variables  --------------------------*/


/*---------------------  Export Functions  --------------------------*/

/*+
 *
 * Routine Description:
 * Enable hw power saving functions
 *
 * Return Value:
 *    None.
 *
-*/


VOID
PSvEnablePowerSaving(
    IN HANDLE hDeviceContext,
    IN WORD wListenInterval
    )
{
    PSDevice        pDevice = (PSDevice)hDeviceContext;
    PSMgmtObject    pMgmt = pDevice->pMgmt;
    WORD            wAID = pMgmt->wCurrAID | BIT14 | BIT15;

    // set period of power up before TBTT
    VNSvOutPortW(pDevice->PortOffset + MAC_REG_PWBT, C_PWBT);
    if (pDevice->eOPMode != OP_MODE_ADHOC) {
        // set AID
        VNSvOutPortW(pDevice->PortOffset + MAC_REG_AIDATIM, wAID);
    } else {
    	// set ATIM Window
        MACvWriteATIMW(pDevice->PortOffset, pMgmt->wCurrATIMWindow);
    }
    // Set AutoSleep
    MACvRegBitsOn(pDevice->PortOffset, MAC_REG_PSCFG, PSCFG_AUTOSLEEP);
    // Set HWUTSF
    MACvRegBitsOn(pDevice->PortOffset, MAC_REG_TFTCTL, TFTCTL_HWUTSF);

    if (wListenInterval >= 2) {
        // clear always listen beacon
        MACvRegBitsOff(pDevice->PortOffset, MAC_REG_PSCTL, PSCTL_ALBCN);
        //pDevice->wCFG &= ~CFG_ALB;
        // first time set listen next beacon
        MACvRegBitsOn(pDevice->PortOffset, MAC_REG_PSCTL, PSCTL_LNBCN);
        pMgmt->wCountToWakeUp = wListenInterval;
    }
    else {
        // always listen beacon
        MACvRegBitsOn(pDevice->PortOffset, MAC_REG_PSCTL, PSCTL_ALBCN);
        //pDevice->wCFG |= CFG_ALB;
        pMgmt->wCountToWakeUp = 0;
    }

    // enable power saving hw function
    MACvRegBitsOn(pDevice->PortOffset, MAC_REG_PSCTL, PSCTL_PSEN);
    pDevice->bEnablePSMode = TRUE;

    if (pDevice->eOPMode == OP_MODE_ADHOC) {
//        bMgrPrepareBeaconToSend((HANDLE)pDevice, pMgmt);
    }
    // We don't send null pkt in ad hoc mode since beacon will handle this.
    else if (pDevice->eOPMode == OP_MODE_INFRASTRUCTURE) {
        PSbSendNullPacket(pDevice);
    }
    pDevice->bPWBitOn = TRUE;
    DEVICE_PRT(MSG_LEVEL_DEBUG, KERN_INFO "PS:Power Saving Mode Enable... \n");
    return;
}






/*+
 *
 * Routine Description:
 * Disable hw power saving functions
 *
 * Return Value:
 *    None.
 *
-*/

VOID
PSvDisablePowerSaving(
    IN HANDLE hDeviceContext
    )
{
    PSDevice        pDevice = (PSDevice)hDeviceContext;
//    PSMgmtObject    pMgmt = pDevice->pMgmt;

    // disable power saving hw function
    MACbPSWakeup(pDevice->PortOffset);
    //clear AutoSleep
    MACvRegBitsOff(pDevice->PortOffset, MAC_REG_PSCFG, PSCFG_AUTOSLEEP);
    //clear HWUTSF
    MACvRegBitsOff(pDevice->PortOffset, MAC_REG_TFTCTL, TFTCTL_HWUTSF);
    // set always listen beacon
    MACvRegBitsOn(pDevice->PortOffset, MAC_REG_PSCTL, PSCTL_ALBCN);

    pDevice->bEnablePSMode = FALSE;

    if (pDevice->eOPMode == OP_MODE_INFRASTRUCTURE) {
        PSbSendNullPacket(pDevice);
    }
    pDevice->bPWBitOn = FALSE;
    return;
}


/*+
 *
 * Routine Description:
 * Consider to power down when no more packets to tx or rx.
 *
 * Return Value:
 *    TRUE, if power down success
 *    FALSE, if fail
-*/


BOOL
PSbConsiderPowerDown(
    IN HANDLE hDeviceContext,
    IN BOOL bCheckRxDMA,
    IN BOOL bCheckCountToWakeUp
    )
{
    PSDevice        pDevice = (PSDevice)hDeviceContext;
    PSMgmtObject    pMgmt = pDevice->pMgmt;
    UINT            uIdx;

    // check if already in Doze mode
    if (MACbIsRegBitsOn(pDevice->PortOffset, MAC_REG_PSCTL, PSCTL_PS))
        return TRUE;

    if (pMgmt->eCurrMode != WMAC_MODE_IBSS_STA) {
        // check if in TIM wake period
        if (pMgmt->bInTIMWake)
            return FALSE;
    }

    // check scan state
    if (pDevice->bCmdRunning)
        return FALSE;

    // Froce PSEN on
    MACvRegBitsOn(pDevice->PortOffset, MAC_REG_PSCTL, PSCTL_PSEN);

    // check if all TD are empty,
    for (uIdx = 0; uIdx < TYPE_MAXTD; uIdx ++) {
        if (pDevice->iTDUsed[uIdx] != 0)
            return FALSE;
    }

    // check if rx isr is clear
    if (bCheckRxDMA &&
        ((pDevice->dwIsr& ISR_RXDMA0) != 0) &&
        ((pDevice->dwIsr & ISR_RXDMA1) != 0)){
        return FALSE;
    };

    if (pMgmt->eCurrMode != WMAC_MODE_IBSS_STA) {
        if (bCheckCountToWakeUp &&
           (pMgmt->wCountToWakeUp == 0 || pMgmt->wCountToWakeUp == 1)) {
             return FALSE;
        }
    }

    // no Tx, no Rx isr, now go to Doze
    MACvRegBitsOn(pDevice->PortOffset, MAC_REG_PSCTL, PSCTL_GO2DOZE);
    DEVICE_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Go to Doze ZZZZZZZZZZZZZZZ\n");
    return TRUE;
}



/*+
 *
 * Routine Description:
 * Send PS-POLL packet
 *
 * Return Value:
 *    None.
 *
-*/



VOID
PSvSendPSPOLL(
    IN HANDLE hDeviceContext
    )
{
    PSDevice            pDevice = (PSDevice)hDeviceContext;
    PSMgmtObject        pMgmt = pDevice->pMgmt;
    PSTxMgmtPacket      pTxPacket = NULL;


    memset(pMgmt->pbyPSPacketPool, 0, sizeof(STxMgmtPacket) + WLAN_HDR_ADDR2_LEN);
    pTxPacket = (PSTxMgmtPacket)pMgmt->pbyPSPacketPool;
    pTxPacket->p80211Header = (PUWLAN_80211HDR)((PBYTE)pTxPacket + sizeof(STxMgmtPacket));
    pTxPacket->p80211Header->sA2.wFrameCtl = cpu_to_le16(
         (
         WLAN_SET_FC_FTYPE(WLAN_TYPE_CTL) |
         WLAN_SET_FC_FSTYPE(WLAN_FSTYPE_PSPOLL) |
         WLAN_SET_FC_PWRMGT(0)
         ));
    pTxPacket->p80211Header->sA2.wDurationID = pMgmt->wCurrAID | BIT14 | BIT15;
    memcpy(pTxPacket->p80211Header->sA2.abyAddr1, pMgmt->abyCurrBSSID, WLAN_ADDR_LEN);
    memcpy(pTxPacket->p80211Header->sA2.abyAddr2, pMgmt->abyMACAddr, WLAN_ADDR_LEN);
    pTxPacket->cbMPDULen = WLAN_HDR_ADDR2_LEN;
    pTxPacket->cbPayloadLen = 0;
    // send the frame
    if (csMgmt_xmit(pDevice, pTxPacket) != CMD_STATUS_PENDING) {
        DEVICE_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Send PS-Poll packet failed..\n");
    }
    else {
//        DEVICE_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Send PS-Poll packet success..\n");
    };

    return;
}



/*+
 *
 * Routine Description:
 * Send NULL packet to AP for notification power state of STA
 *
 * Return Value:
 *    None.
 *
-*/
BOOL
PSbSendNullPacket(
    IN HANDLE hDeviceContext
    )
{
    PSDevice            pDevice = (PSDevice)hDeviceContext;
    PSTxMgmtPacket      pTxPacket = NULL;
    PSMgmtObject        pMgmt = pDevice->pMgmt;
    UINT                uIdx;


    if (pDevice->bLinkPass == FALSE) {
        return FALSE;
    }
    #ifdef TxInSleep
     if ((pDevice->bEnablePSMode == FALSE) &&
	  (pDevice->fTxDataInSleep == FALSE)){
        return FALSE;
    }
#else
    if (pDevice->bEnablePSMode == FALSE) {
        return FALSE;
    }
#endif
    if (pDevice->bEnablePSMode) {
        for (uIdx = 0; uIdx < TYPE_MAXTD; uIdx ++) {
            if (pDevice->iTDUsed[uIdx] != 0)
                return FALSE;
        }
    }

    memset(pMgmt->pbyPSPacketPool, 0, sizeof(STxMgmtPacket) + WLAN_NULLDATA_FR_MAXLEN);
    pTxPacket = (PSTxMgmtPacket)pMgmt->pbyPSPacketPool;
    pTxPacket->p80211Header = (PUWLAN_80211HDR)((PBYTE)pTxPacket + sizeof(STxMgmtPacket));

    if (pDevice->bEnablePSMode) {

        pTxPacket->p80211Header->sA3.wFrameCtl = cpu_to_le16(
             (
            WLAN_SET_FC_FTYPE(WLAN_TYPE_DATA) |
            WLAN_SET_FC_FSTYPE(WLAN_FSTYPE_NULL) |
            WLAN_SET_FC_PWRMGT(1)
            ));
    }
    else {
        pTxPacket->p80211Header->sA3.wFrameCtl = cpu_to_le16(
             (
            WLAN_SET_FC_FTYPE(WLAN_TYPE_DATA) |
            WLAN_SET_FC_FSTYPE(WLAN_FSTYPE_NULL) |
            WLAN_SET_FC_PWRMGT(0)
            ));
    }

    if(pMgmt->eCurrMode != WMAC_MODE_IBSS_STA) {
        pTxPacket->p80211Header->sA3.wFrameCtl |= cpu_to_le16((WORD)WLAN_SET_FC_TODS(1));
    }

    memcpy(pTxPacket->p80211Header->sA3.abyAddr1, pMgmt->abyCurrBSSID, WLAN_ADDR_LEN);
    memcpy(pTxPacket->p80211Header->sA3.abyAddr2, pMgmt->abyMACAddr, WLAN_ADDR_LEN);
    memcpy(pTxPacket->p80211Header->sA3.abyAddr3, pMgmt->abyCurrBSSID, WLAN_BSSID_LEN);
    pTxPacket->cbMPDULen = WLAN_HDR_ADDR3_LEN;
    pTxPacket->cbPayloadLen = 0;
    // send the frame
    if (csMgmt_xmit(pDevice, pTxPacket) != CMD_STATUS_PENDING) {
        DEVICE_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Send Null Packet failed !\n");
        return FALSE;
    }
    else {

//            DEVICE_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Send Null Packet success....\n");
    }


    return TRUE ;
}

/*+
 *
 * Routine Description:
 * Check if Next TBTT must wake up
 *
 * Return Value:
 *    None.
 *
-*/

BOOL
PSbIsNextTBTTWakeUp(
    IN HANDLE hDeviceContext
    )
{

    PSDevice         pDevice = (PSDevice)hDeviceContext;
    PSMgmtObject        pMgmt = pDevice->pMgmt;
    BOOL                bWakeUp = FALSE;

    if (pMgmt->wListenInterval >= 2) {
        if (pMgmt->wCountToWakeUp == 0) {
            pMgmt->wCountToWakeUp = pMgmt->wListenInterval;
        }

        pMgmt->wCountToWakeUp --;

        if (pMgmt->wCountToWakeUp == 1) {
            // Turn on wake up to listen next beacon
            MACvRegBitsOn(pDevice->PortOffset, MAC_REG_PSCTL, PSCTL_LNBCN);
            bWakeUp = TRUE;
        }

    }

    return bWakeUp;
}

