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
 * File: power.c
 *
 * Purpose: Handles 802.11 power management  functions
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

#include "ttype.h"
#include "mac.h"
#include "device.h"
#include "wmgr.h"
#include "power.h"
#include "wcmd.h"
#include "rxtx.h"
#include "card.h"

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


void
PSvEnablePowerSaving(
    void *hDeviceContext,
    unsigned short wListenInterval
    )
{
    PSDevice        pDevice = (PSDevice)hDeviceContext;
    PSMgmtObject    pMgmt = pDevice->pMgmt;
    unsigned short wAID = pMgmt->wCurrAID | BIT14 | BIT15;

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
    pDevice->bEnablePSMode = true;

    if (pDevice->eOPMode == OP_MODE_ADHOC) {
//        bMgrPrepareBeaconToSend((void *)pDevice, pMgmt);
    }
    // We don't send null pkt in ad hoc mode since beacon will handle this.
    else if (pDevice->eOPMode == OP_MODE_INFRASTRUCTURE) {
        PSbSendNullPacket(pDevice);
    }
    pDevice->bPWBitOn = true;
    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "PS:Power Saving Mode Enable... \n");
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

void
PSvDisablePowerSaving(
    void *hDeviceContext
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

    pDevice->bEnablePSMode = false;

    if (pDevice->eOPMode == OP_MODE_INFRASTRUCTURE) {
        PSbSendNullPacket(pDevice);
    }
    pDevice->bPWBitOn = false;
    return;
}


/*+
 *
 * Routine Description:
 * Consider to power down when no more packets to tx or rx.
 *
 * Return Value:
 *    true, if power down success
 *    false, if fail
-*/


bool
PSbConsiderPowerDown(
    void *hDeviceContext,
    bool bCheckRxDMA,
    bool bCheckCountToWakeUp
    )
{
    PSDevice        pDevice = (PSDevice)hDeviceContext;
    PSMgmtObject    pMgmt = pDevice->pMgmt;
    unsigned int uIdx;

    // check if already in Doze mode
    if (MACbIsRegBitsOn(pDevice->PortOffset, MAC_REG_PSCTL, PSCTL_PS))
        return true;

    if (pMgmt->eCurrMode != WMAC_MODE_IBSS_STA) {
        // check if in TIM wake period
        if (pMgmt->bInTIMWake)
            return false;
    }

    // check scan state
    if (pDevice->bCmdRunning)
        return false;

    // Force PSEN on
    MACvRegBitsOn(pDevice->PortOffset, MAC_REG_PSCTL, PSCTL_PSEN);

    // check if all TD are empty,
    for (uIdx = 0; uIdx < TYPE_MAXTD; uIdx ++) {
        if (pDevice->iTDUsed[uIdx] != 0)
            return false;
    }

    // check if rx isr is clear
    if (bCheckRxDMA &&
        ((pDevice->dwIsr& ISR_RXDMA0) != 0) &&
        ((pDevice->dwIsr & ISR_RXDMA1) != 0)){
        return false;
    }

    if (pMgmt->eCurrMode != WMAC_MODE_IBSS_STA) {
        if (bCheckCountToWakeUp &&
           (pMgmt->wCountToWakeUp == 0 || pMgmt->wCountToWakeUp == 1)) {
             return false;
        }
    }

    // no Tx, no Rx isr, now go to Doze
    MACvRegBitsOn(pDevice->PortOffset, MAC_REG_PSCTL, PSCTL_GO2DOZE);
    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Go to Doze ZZZZZZZZZZZZZZZ\n");
    return true;
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



void
PSvSendPSPOLL(
    void *hDeviceContext
    )
{
    PSDevice            pDevice = (PSDevice)hDeviceContext;
    PSMgmtObject        pMgmt = pDevice->pMgmt;
    PSTxMgmtPacket      pTxPacket = NULL;


    memset(pMgmt->pbyPSPacketPool, 0, sizeof(STxMgmtPacket) + WLAN_HDR_ADDR2_LEN);
    pTxPacket = (PSTxMgmtPacket)pMgmt->pbyPSPacketPool;
    pTxPacket->p80211Header = (PUWLAN_80211HDR)((unsigned char *)pTxPacket + sizeof(STxMgmtPacket));
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
        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Send PS-Poll packet failed..\n");
    }
    else {
//        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Send PS-Poll packet success..\n");
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
bool
PSbSendNullPacket(
    void *hDeviceContext
    )
{
    PSDevice            pDevice = (PSDevice)hDeviceContext;
    PSTxMgmtPacket      pTxPacket = NULL;
    PSMgmtObject        pMgmt = pDevice->pMgmt;
    unsigned int uIdx;


    if (pDevice->bLinkPass == false) {
        return false;
    }
    #ifdef TxInSleep
     if ((pDevice->bEnablePSMode == false) &&
	  (pDevice->fTxDataInSleep == false)){
        return false;
    }
#else
    if (pDevice->bEnablePSMode == false) {
        return false;
    }
#endif
    if (pDevice->bEnablePSMode) {
        for (uIdx = 0; uIdx < TYPE_MAXTD; uIdx ++) {
            if (pDevice->iTDUsed[uIdx] != 0)
                return false;
        }
    }

    memset(pMgmt->pbyPSPacketPool, 0, sizeof(STxMgmtPacket) + WLAN_NULLDATA_FR_MAXLEN);
    pTxPacket = (PSTxMgmtPacket)pMgmt->pbyPSPacketPool;
    pTxPacket->p80211Header = (PUWLAN_80211HDR)((unsigned char *)pTxPacket + sizeof(STxMgmtPacket));

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
        pTxPacket->p80211Header->sA3.wFrameCtl |= cpu_to_le16((unsigned short)WLAN_SET_FC_TODS(1));
    }

    memcpy(pTxPacket->p80211Header->sA3.abyAddr1, pMgmt->abyCurrBSSID, WLAN_ADDR_LEN);
    memcpy(pTxPacket->p80211Header->sA3.abyAddr2, pMgmt->abyMACAddr, WLAN_ADDR_LEN);
    memcpy(pTxPacket->p80211Header->sA3.abyAddr3, pMgmt->abyCurrBSSID, WLAN_BSSID_LEN);
    pTxPacket->cbMPDULen = WLAN_HDR_ADDR3_LEN;
    pTxPacket->cbPayloadLen = 0;
    // send the frame
    if (csMgmt_xmit(pDevice, pTxPacket) != CMD_STATUS_PENDING) {
        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Send Null Packet failed !\n");
        return false;
    }
    else {

//            DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Send Null Packet success....\n");
    }


    return true ;
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

bool
PSbIsNextTBTTWakeUp(
    void *hDeviceContext
    )
{

    PSDevice         pDevice = (PSDevice)hDeviceContext;
    PSMgmtObject        pMgmt = pDevice->pMgmt;
    bool bWakeUp = false;

    if (pMgmt->wListenInterval >= 2) {
        if (pMgmt->wCountToWakeUp == 0) {
            pMgmt->wCountToWakeUp = pMgmt->wListenInterval;
        }

        pMgmt->wCountToWakeUp --;

        if (pMgmt->wCountToWakeUp == 1) {
            // Turn on wake up to listen next beacon
            MACvRegBitsOn(pDevice->PortOffset, MAC_REG_PSCTL, PSCTL_LNBCN);
            bWakeUp = true;
        }

    }

    return bWakeUp;
}

