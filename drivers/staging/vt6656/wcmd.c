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
 * File: wcmd.c
 *
 * Purpose: Handles the management command interface functions
 *
 * Author: Lyndon Chen
 *
 * Date: May 8, 2003
 *
 * Functions:
 *      s_vProbeChannel - Active scan channel
 *      s_MgrMakeProbeRequest - Make ProbeRequest packet
 *      CommandTimer - Timer function to handle command
 *      s_bCommandComplete - Command Complete function
 *      bScheduleCommand - Push Command and wait Command Scheduler to do
 *      vCommandTimer- Command call back functions
 *      vCommandTimerWait- Call back timer
 *      s_bClearBSSID_SCAN- Clear BSSID_SCAN cmd in CMD Queue
 *
 * Revision History:
 *
 */

#include "ttype.h"
#include "tmacro.h"
#include "device.h"
#include "mac.h"
#include "card.h"
#include "80211hdr.h"
#include "wcmd.h"
#include "wmgr.h"
#include "power.h"
#include "wctl.h"
#include "baseband.h"
#include "control.h"
#include "rxtx.h"
#include "rf.h"
#include "rndis.h"
#include "channel.h"
#include "iowpa.h"

/*---------------------  Static Definitions -------------------------*/




/*---------------------  Static Classes  ----------------------------*/

/*---------------------  Static Variables  --------------------------*/
static int          msglevel                =MSG_LEVEL_INFO;
//static int          msglevel                =MSG_LEVEL_DEBUG;
/*---------------------  Static Functions  --------------------------*/

static
void
s_vProbeChannel(
     PSDevice pDevice
    );


static
PSTxMgmtPacket
s_MgrMakeProbeRequest(
     PSDevice pDevice,
     PSMgmtObject pMgmt,
     PBYTE pScanBSSID,
     PWLAN_IE_SSID pSSID,
     PWLAN_IE_SUPP_RATES pCurrRates,
     PWLAN_IE_SUPP_RATES pCurrExtSuppRates
    );


static
BOOL
s_bCommandComplete (
    PSDevice pDevice
    );


static BOOL s_bClearBSSID_SCAN(void *hDeviceContext);

/*---------------------  Export Variables  --------------------------*/

/*---------------------  Export Functions  --------------------------*/

/*
 * Description:
 *      Stop AdHoc beacon during scan process
 *
 * Parameters:
 *  In:
 *      pDevice     - Pointer to the adapter
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */

static
void
vAdHocBeaconStop(PSDevice  pDevice)
{

    PSMgmtObject    pMgmt = &(pDevice->sMgmtObj);
    BOOL            bStop;

    /*
     * temporarily stop Beacon packet for AdHoc Server
     * if all of the following coditions are met:
     *  (1) STA is in AdHoc mode
     *  (2) VT3253 is programmed as automatic Beacon Transmitting
     *  (3) One of the following conditions is met
     *      (3.1) AdHoc channel is in B/G band and the
     *      current scan channel is in A band
     *      or
     *      (3.2) AdHoc channel is in A mode
     */
    bStop = FALSE;
    if ((pMgmt->eCurrMode == WMAC_MODE_IBSS_STA) &&
    (pMgmt->eCurrState >= WMAC_STATE_STARTED))
    {
        if ((pMgmt->uIBSSChannel <=  CB_MAX_CHANNEL_24G) &&
             (pMgmt->uScanChannel > CB_MAX_CHANNEL_24G))
        {
            bStop = TRUE;
        }
        if (pMgmt->uIBSSChannel >  CB_MAX_CHANNEL_24G)
        {
            bStop = TRUE;
        }
    }

    if (bStop)
    {
        //PMESG(("STOP_BEACON: IBSSChannel = %u, ScanChannel = %u\n",
        //        pMgmt->uIBSSChannel, pMgmt->uScanChannel));
        MACvRegBitsOff(pDevice, MAC_REG_TCR, TCR_AUTOBCNTX);
    }

} /* vAdHocBeaconStop */


/*
 * Description:
 *      Restart AdHoc beacon after scan process complete
 *
 * Parameters:
 *  In:
 *      pDevice     - Pointer to the adapter
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
static
void
vAdHocBeaconRestart(PSDevice pDevice)
{
    PSMgmtObject    pMgmt = &(pDevice->sMgmtObj);

    /*
     * Restart Beacon packet for AdHoc Server
     * if all of the following coditions are met:
     *  (1) STA is in AdHoc mode
     *  (2) VT3253 is programmed as automatic Beacon Transmitting
     */
    if ((pMgmt->eCurrMode == WMAC_MODE_IBSS_STA) &&
    (pMgmt->eCurrState >= WMAC_STATE_STARTED))
    {
        //PMESG(("RESTART_BEACON\n"));
        MACvRegBitsOn(pDevice, MAC_REG_TCR, TCR_AUTOBCNTX);
    }

}


/*+
 *
 * Routine Description:
 *   Prepare and send probe request management frames.
 *
 *
 * Return Value:
 *    none.
 *
-*/

static
void
s_vProbeChannel(
     PSDevice pDevice
    )
{
                                                     //1M,   2M,   5M,   11M,  18M,  24M,  36M,  54M
    BYTE abyCurrSuppRatesG[] = {WLAN_EID_SUPP_RATES, 8, 0x02, 0x04, 0x0B, 0x16, 0x24, 0x30, 0x48, 0x6C};
    BYTE abyCurrExtSuppRatesG[] = {WLAN_EID_EXTSUPP_RATES, 4, 0x0C, 0x12, 0x18, 0x60};
                                                           //6M,   9M,   12M,  48M
    BYTE abyCurrSuppRatesA[] = {WLAN_EID_SUPP_RATES, 8, 0x0C, 0x12, 0x18, 0x24, 0x30, 0x48, 0x60, 0x6C};
    BYTE abyCurrSuppRatesB[] = {WLAN_EID_SUPP_RATES, 4, 0x02, 0x04, 0x0B, 0x16};
    PBYTE           pbyRate;
    PSTxMgmtPacket  pTxPacket;
    PSMgmtObject    pMgmt = &(pDevice->sMgmtObj);
    unsigned int            ii;


    if (pDevice->byBBType == BB_TYPE_11A) {
        pbyRate = &abyCurrSuppRatesA[0];
    } else if (pDevice->byBBType == BB_TYPE_11B) {
        pbyRate = &abyCurrSuppRatesB[0];
    } else {
        pbyRate = &abyCurrSuppRatesG[0];
    }
    // build an assocreq frame and send it
    pTxPacket = s_MgrMakeProbeRequest
                (
                  pDevice,
                  pMgmt,
                  pMgmt->abyScanBSSID,
                  (PWLAN_IE_SSID)pMgmt->abyScanSSID,
                  (PWLAN_IE_SUPP_RATES)pbyRate,
                  (PWLAN_IE_SUPP_RATES)abyCurrExtSuppRatesG
                );

    if (pTxPacket != NULL ){
        for (ii = 0; ii < 1 ; ii++) {
            if (csMgmt_xmit(pDevice, pTxPacket) != CMD_STATUS_PENDING) {
                DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Probe request sending fail.. \n");
            }
            else {
                DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Probe request is sending.. \n");
            }
        }
    }

}




/*+
 *
 * Routine Description:
 *  Constructs an probe request frame
 *
 *
 * Return Value:
 *    A ptr to Tx frame or NULL on allocation failure
 *
-*/


PSTxMgmtPacket
s_MgrMakeProbeRequest(
     PSDevice pDevice,
     PSMgmtObject pMgmt,
     PBYTE pScanBSSID,
     PWLAN_IE_SSID pSSID,
     PWLAN_IE_SUPP_RATES pCurrRates,
     PWLAN_IE_SUPP_RATES pCurrExtSuppRates

    )
{
    PSTxMgmtPacket      pTxPacket = NULL;
    WLAN_FR_PROBEREQ    sFrame;


    pTxPacket = (PSTxMgmtPacket)pMgmt->pbyMgmtPacketPool;
    memset(pTxPacket, 0, sizeof(STxMgmtPacket) + WLAN_PROBEREQ_FR_MAXLEN);
    pTxPacket->p80211Header = (PUWLAN_80211HDR)((PBYTE)pTxPacket + sizeof(STxMgmtPacket));
    sFrame.pBuf = (PBYTE)pTxPacket->p80211Header;
    sFrame.len = WLAN_PROBEREQ_FR_MAXLEN;
    vMgrEncodeProbeRequest(&sFrame);
    sFrame.pHdr->sA3.wFrameCtl = cpu_to_le16(
        (
        WLAN_SET_FC_FTYPE(WLAN_TYPE_MGR) |
        WLAN_SET_FC_FSTYPE(WLAN_FSTYPE_PROBEREQ)
        ));
    memcpy( sFrame.pHdr->sA3.abyAddr1, pScanBSSID, WLAN_ADDR_LEN);
    memcpy( sFrame.pHdr->sA3.abyAddr2, pMgmt->abyMACAddr, WLAN_ADDR_LEN);
    memcpy( sFrame.pHdr->sA3.abyAddr3, pScanBSSID, WLAN_BSSID_LEN);
    // Copy the SSID, pSSID->len=0 indicate broadcast SSID
    sFrame.pSSID = (PWLAN_IE_SSID)(sFrame.pBuf + sFrame.len);
    sFrame.len += pSSID->len + WLAN_IEHDR_LEN;
    memcpy(sFrame.pSSID, pSSID, pSSID->len + WLAN_IEHDR_LEN);
    sFrame.pSuppRates = (PWLAN_IE_SUPP_RATES)(sFrame.pBuf + sFrame.len);
    sFrame.len += pCurrRates->len + WLAN_IEHDR_LEN;
    memcpy(sFrame.pSuppRates, pCurrRates, pCurrRates->len + WLAN_IEHDR_LEN);
    // Copy the extension rate set
    if (pDevice->byBBType == BB_TYPE_11G) {
        sFrame.pExtSuppRates = (PWLAN_IE_SUPP_RATES)(sFrame.pBuf + sFrame.len);
        sFrame.len += pCurrExtSuppRates->len + WLAN_IEHDR_LEN;
        memcpy(sFrame.pExtSuppRates, pCurrExtSuppRates, pCurrExtSuppRates->len + WLAN_IEHDR_LEN);
    }
    pTxPacket->cbMPDULen = sFrame.len;
    pTxPacket->cbPayloadLen = sFrame.len - WLAN_HDR_ADDR3_LEN;

    return pTxPacket;
}

void vCommandTimerWait(void *hDeviceContext, unsigned long MSecond)
{
	PSDevice pDevice = (PSDevice)hDeviceContext;

	init_timer(&pDevice->sTimerCommand);

	pDevice->sTimerCommand.data = (unsigned long)pDevice;
	pDevice->sTimerCommand.function = (TimerFunction)vRunCommand;
	pDevice->sTimerCommand.expires = RUN_AT((MSecond * HZ) / 1000);

	add_timer(&pDevice->sTimerCommand);

	return;
}

void vRunCommand(void *hDeviceContext)
{
    PSDevice        pDevice = (PSDevice)hDeviceContext;
    PSMgmtObject    pMgmt = &(pDevice->sMgmtObj);
    PWLAN_IE_SSID   pItemSSID;
    PWLAN_IE_SSID   pItemSSIDCurr;
    CMD_STATUS      Status;
    unsigned int            ii;
    BYTE            byMask[8] = {1, 2, 4, 8, 0x10, 0x20, 0x40, 0x80};
    struct sk_buff  *skb;
    BYTE            byData;


    if (pDevice->dwDiagRefCount != 0)
        return;
    if (pDevice->bCmdRunning != TRUE)
        return;

    spin_lock_irq(&pDevice->lock);

    switch ( pDevice->eCommandState ) {

        case WLAN_CMD_SCAN_START:

		pDevice->byReAssocCount = 0;
            if (pDevice->bRadioOff == TRUE) {
                s_bCommandComplete(pDevice);
                spin_unlock_irq(&pDevice->lock);
                return;
            }

            if (pMgmt->eCurrMode == WMAC_MODE_ESS_AP) {
                s_bCommandComplete(pDevice);
                spin_unlock_irq(&pDevice->lock);
                return;
            }

            pItemSSID = (PWLAN_IE_SSID)pMgmt->abyScanSSID;

            if (pMgmt->uScanChannel == 0 ) {
                pMgmt->uScanChannel = pDevice->byMinChannel;
            }
            if (pMgmt->uScanChannel > pDevice->byMaxChannel) {
                pMgmt->eScanState = WMAC_NO_SCANNING;

                if (pDevice->byBBType != pDevice->byScanBBType) {
                    pDevice->byBBType = pDevice->byScanBBType;
                    CARDvSetBSSMode(pDevice);
                }

                if (pDevice->bUpdateBBVGA) {
                    BBvSetShortSlotTime(pDevice);
                    BBvSetVGAGainOffset(pDevice, pDevice->byBBVGACurrent);
                    BBvUpdatePreEDThreshold(pDevice, FALSE);
                }
                // Set channel back
                vAdHocBeaconRestart(pDevice);
                // Set channel back
                CARDbSetMediaChannel(pDevice, pMgmt->uCurrChannel);
                // Set Filter
                if (pMgmt->bCurrBSSIDFilterOn) {
                    MACvRegBitsOn(pDevice, MAC_REG_RCR, RCR_BSSID);
                    pDevice->byRxMode |= RCR_BSSID;
                }
                DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Scanning, set back to channel: [%d]\n", pMgmt->uCurrChannel);
                pDevice->bStopDataPkt = FALSE;
                s_bCommandComplete(pDevice);
                spin_unlock_irq(&pDevice->lock);
                return;

            } else {
                if (!ChannelValid(pDevice->byZoneType, pMgmt->uScanChannel)) {
                    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Invalid channel pMgmt->uScanChannel = %d \n",pMgmt->uScanChannel);
                    s_bCommandComplete(pDevice);
                    spin_unlock_irq(&pDevice->lock);
                    return;
                }
                if (pMgmt->uScanChannel == pDevice->byMinChannel) {
                   // pMgmt->eScanType = WMAC_SCAN_ACTIVE;          //mike mark
                    pMgmt->abyScanBSSID[0] = 0xFF;
                    pMgmt->abyScanBSSID[1] = 0xFF;
                    pMgmt->abyScanBSSID[2] = 0xFF;
                    pMgmt->abyScanBSSID[3] = 0xFF;
                    pMgmt->abyScanBSSID[4] = 0xFF;
                    pMgmt->abyScanBSSID[5] = 0xFF;
                    pItemSSID->byElementID = WLAN_EID_SSID;
                    // clear bssid list
		    /* BSSvClearBSSList((void *) pDevice,
		       pDevice->bLinkPass); */
                    pMgmt->eScanState = WMAC_IS_SCANNING;
                    pDevice->byScanBBType = pDevice->byBBType;  //lucas
                    pDevice->bStopDataPkt = TRUE;
                    // Turn off RCR_BSSID filter every time
                    MACvRegBitsOff(pDevice, MAC_REG_RCR, RCR_BSSID);
                    pDevice->byRxMode &= ~RCR_BSSID;

                }
                //lucas
                vAdHocBeaconStop(pDevice);
                if ((pDevice->byBBType != BB_TYPE_11A) && (pMgmt->uScanChannel > CB_MAX_CHANNEL_24G)) {
                    pDevice->byBBType = BB_TYPE_11A;
                    CARDvSetBSSMode(pDevice);
                }
                else if ((pDevice->byBBType == BB_TYPE_11A) && (pMgmt->uScanChannel <= CB_MAX_CHANNEL_24G)) {
                    pDevice->byBBType = BB_TYPE_11G;
                    CARDvSetBSSMode(pDevice);
                }
                DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Scanning....  channel: [%d]\n", pMgmt->uScanChannel);
                // Set channel
                CARDbSetMediaChannel(pDevice, pMgmt->uScanChannel);
                // Set Baseband to be more sensitive.

                if (pDevice->bUpdateBBVGA) {
                    BBvSetShortSlotTime(pDevice);
                    BBvSetVGAGainOffset(pDevice, pDevice->abyBBVGA[0]);
                    BBvUpdatePreEDThreshold(pDevice, TRUE);
                }
                pMgmt->uScanChannel++;

                while (!ChannelValid(pDevice->byZoneType, pMgmt->uScanChannel) &&
                        pMgmt->uScanChannel <= pDevice->byMaxChannel ){
                    pMgmt->uScanChannel++;
                }

                if (pMgmt->uScanChannel > pDevice->byMaxChannel) {
                    // Set Baseband to be not sensitive and rescan
                    pDevice->eCommandState = WLAN_CMD_SCAN_END;

                }
                if ((pMgmt->b11hEnable == FALSE) ||
                    (pMgmt->uScanChannel < CB_MAX_CHANNEL_24G)) {
                    s_vProbeChannel(pDevice);
                    spin_unlock_irq(&pDevice->lock);
		     vCommandTimerWait((void *) pDevice, 100);
                    return;
                } else {
                    spin_unlock_irq(&pDevice->lock);
		    vCommandTimerWait((void *) pDevice, WCMD_PASSIVE_SCAN_TIME);
                    return;
                }

            }

            break;

        case WLAN_CMD_SCAN_END:

            // Set Baseband's sensitivity back.
            if (pDevice->byBBType != pDevice->byScanBBType) {
                pDevice->byBBType = pDevice->byScanBBType;
                CARDvSetBSSMode(pDevice);
            }

            if (pDevice->bUpdateBBVGA) {
                BBvSetShortSlotTime(pDevice);
                BBvSetVGAGainOffset(pDevice, pDevice->byBBVGACurrent);
                BBvUpdatePreEDThreshold(pDevice, FALSE);
            }

            // Set channel back
            vAdHocBeaconRestart(pDevice);
            // Set channel back
            CARDbSetMediaChannel(pDevice, pMgmt->uCurrChannel);
            // Set Filter
            if (pMgmt->bCurrBSSIDFilterOn) {
                MACvRegBitsOn(pDevice, MAC_REG_RCR, RCR_BSSID);
                pDevice->byRxMode |= RCR_BSSID;
            }
            DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Scanning, set back to channel: [%d]\n", pMgmt->uCurrChannel);
            pMgmt->eScanState = WMAC_NO_SCANNING;
            pDevice->bStopDataPkt = FALSE;

#ifdef WPA_SUPPLICANT_DRIVER_WEXT_SUPPORT
	if(pMgmt->eScanType == WMAC_SCAN_PASSIVE)
		{
			//send scan event to wpa_Supplicant
				union iwreq_data wrqu;
				PRINT_K("wireless_send_event--->SIOCGIWSCAN(scan done)\n");
				memset(&wrqu, 0, sizeof(wrqu));
				wireless_send_event(pDevice->dev, SIOCGIWSCAN, &wrqu, NULL);
			}
#endif
            s_bCommandComplete(pDevice);
            break;

        case WLAN_CMD_DISASSOCIATE_START :
		pDevice->byReAssocCount = 0;
            if ((pMgmt->eCurrMode == WMAC_MODE_ESS_STA) &&
                (pMgmt->eCurrState != WMAC_STATE_ASSOC)) {
                s_bCommandComplete(pDevice);
                spin_unlock_irq(&pDevice->lock);
                return;
            } else {

          #ifdef WPA_SUPPLICANT_DRIVER_WEXT_SUPPORT
		      pDevice->bwextstep0 = FALSE;
                        pDevice->bwextstep1 = FALSE;
                        pDevice->bwextstep2 = FALSE;
                        pDevice->bwextstep3 = FALSE;
		   pDevice->bWPASuppWextEnabled = FALSE;
	 #endif
                   pDevice->fWPA_Authened = FALSE;

                DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"Send Disassociation Packet..\n");
                // reason = 8 : disassoc because sta has left
		vMgrDisassocBeginSta((void *) pDevice,
				     pMgmt,
				     pMgmt->abyCurrBSSID,
				     (8),
				     &Status);
                pDevice->bLinkPass = FALSE;
                ControlvMaskByte(pDevice,MESSAGE_REQUEST_MACREG,MAC_REG_PAPEDELAY,LEDSTS_STS,LEDSTS_SLOW);
                // unlock command busy
                pItemSSID = (PWLAN_IE_SSID)pMgmt->abyCurrSSID;
                pItemSSID->len = 0;
                memset(pItemSSID->abySSID, 0, WLAN_SSID_MAXLEN);
                pMgmt->eCurrState = WMAC_STATE_IDLE;
                pMgmt->sNodeDBTable[0].bActive = FALSE;
//                pDevice->bBeaconBufReady = FALSE;
            }
            netif_stop_queue(pDevice->dev);
            if (pDevice->bNeedRadioOFF == TRUE)
                CARDbRadioPowerOff(pDevice);
            s_bCommandComplete(pDevice);
            break;


        case WLAN_CMD_SSID_START:

		pDevice->byReAssocCount = 0;
            if (pDevice->bRadioOff == TRUE) {
                s_bCommandComplete(pDevice);
                spin_unlock_irq(&pDevice->lock);
                return;
            }

            memcpy(pMgmt->abyAdHocSSID,pMgmt->abyDesireSSID,
                              ((PWLAN_IE_SSID)pMgmt->abyDesireSSID)->len + WLAN_IEHDR_LEN);

            pItemSSID = (PWLAN_IE_SSID)pMgmt->abyDesireSSID;
            pItemSSIDCurr = (PWLAN_IE_SSID)pMgmt->abyCurrSSID;
            DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO" cmd: desire ssid = %s\n", pItemSSID->abySSID);
            DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO" cmd: curr ssid = %s\n", pItemSSIDCurr->abySSID);

            if (pMgmt->eCurrState == WMAC_STATE_ASSOC) {
                DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO" Cmd pMgmt->eCurrState == WMAC_STATE_ASSOC\n");
                DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO" pItemSSID->len =%d\n",pItemSSID->len);
                DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO" pItemSSIDCurr->len = %d\n",pItemSSIDCurr->len);
                DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO" desire ssid = %s\n", pItemSSID->abySSID);
                DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO" curr ssid = %s\n", pItemSSIDCurr->abySSID);
            }

            if ((pMgmt->eCurrState == WMAC_STATE_ASSOC) ||
                ((pMgmt->eCurrMode == WMAC_MODE_IBSS_STA)&& (pMgmt->eCurrState == WMAC_STATE_JOINTED))) {

                if (pItemSSID->len == pItemSSIDCurr->len) {
                    if (memcmp(pItemSSID->abySSID, pItemSSIDCurr->abySSID, pItemSSID->len) == 0) {
                        s_bCommandComplete(pDevice);
                        spin_unlock_irq(&pDevice->lock);
                        return;
                    }
                }
                netif_stop_queue(pDevice->dev);
                pDevice->bLinkPass = FALSE;
                ControlvMaskByte(pDevice,MESSAGE_REQUEST_MACREG,MAC_REG_PAPEDELAY,LEDSTS_STS,LEDSTS_SLOW);
            }
            // set initial state
            pMgmt->eCurrState = WMAC_STATE_IDLE;
            pMgmt->eCurrMode = WMAC_MODE_STANDBY;
	    PSvDisablePowerSaving((void *) pDevice);
            BSSvClearNodeDBTable(pDevice, 0);
	    vMgrJoinBSSBegin((void *) pDevice, &Status);
            // if Infra mode
            if ((pMgmt->eCurrMode == WMAC_MODE_ESS_STA) && (pMgmt->eCurrState == WMAC_STATE_JOINTED)) {
                // Call mgr to begin the deauthentication
                // reason = (3) because sta has left ESS
	      if (pMgmt->eCurrState >= WMAC_STATE_AUTH) {
		vMgrDeAuthenBeginSta((void *)pDevice,
				     pMgmt,
				     pMgmt->abyCurrBSSID,
				     (3),
				     &Status);
	      }
                // Call mgr to begin the authentication
		vMgrAuthenBeginSta((void *) pDevice, pMgmt, &Status);
                if (Status == CMD_STATUS_SUCCESS) {
		   pDevice->byLinkWaitCount = 0;
                    pDevice->eCommandState = WLAN_AUTHENTICATE_WAIT;
		    vCommandTimerWait((void *) pDevice, AUTHENTICATE_TIMEOUT);
                    spin_unlock_irq(&pDevice->lock);
                    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO" Set eCommandState = WLAN_AUTHENTICATE_WAIT\n");
                    return;
                }
            }
            // if Adhoc mode
            else if (pMgmt->eCurrMode == WMAC_MODE_IBSS_STA) {
                if (pMgmt->eCurrState == WMAC_STATE_JOINTED) {
                    if (netif_queue_stopped(pDevice->dev)){
                        netif_wake_queue(pDevice->dev);
                    }
                    pDevice->bLinkPass = TRUE;
                    ControlvMaskByte(pDevice,MESSAGE_REQUEST_MACREG,MAC_REG_PAPEDELAY,LEDSTS_STS,LEDSTS_INTER);
                    pMgmt->sNodeDBTable[0].bActive = TRUE;
                    pMgmt->sNodeDBTable[0].uInActiveCount = 0;
                }
                else {
                    // start own IBSS
		    DBG_PRT(MSG_LEVEL_DEBUG,
			    KERN_INFO "CreateOwn IBSS by CurrMode = IBSS_STA\n");
		    vMgrCreateOwnIBSS((void *) pDevice, &Status);
                    if (Status != CMD_STATUS_SUCCESS){
			DBG_PRT(MSG_LEVEL_DEBUG,
				KERN_INFO "WLAN_CMD_IBSS_CREATE fail!\n");
                    }
                    BSSvAddMulticastNode(pDevice);
                }
                s_bClearBSSID_SCAN(pDevice);
            }
            // if SSID not found
            else if (pMgmt->eCurrMode == WMAC_MODE_STANDBY) {
                if (pMgmt->eConfigMode == WMAC_CONFIG_IBSS_STA ||
                    pMgmt->eConfigMode == WMAC_CONFIG_AUTO) {
                    // start own IBSS
			DBG_PRT(MSG_LEVEL_DEBUG,
				KERN_INFO "CreateOwn IBSS by CurrMode = STANDBY\n");
		    vMgrCreateOwnIBSS((void *) pDevice, &Status);
                    if (Status != CMD_STATUS_SUCCESS){
			DBG_PRT(MSG_LEVEL_DEBUG,
				KERN_INFO "WLAN_CMD_IBSS_CREATE fail!\n");
                    }
                    BSSvAddMulticastNode(pDevice);
                    s_bClearBSSID_SCAN(pDevice);
/*
                    pDevice->bLinkPass = TRUE;
                    ControlvMaskByte(pDevice,MESSAGE_REQUEST_MACREG,MAC_REG_PAPEDELAY,LEDSTS_STS,LEDSTS_INTER);
                    if (netif_queue_stopped(pDevice->dev)){
                        netif_wake_queue(pDevice->dev);
                    }
                    s_bClearBSSID_SCAN(pDevice);
*/
                }
                else {
                    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Disconnect SSID none\n");
                     #ifdef WPA_SUPPLICANT_DRIVER_WEXT_SUPPORT
                    // if(pDevice->bWPASuppWextEnabled == TRUE)
                        {
                  	union iwreq_data  wrqu;
                  	memset(&wrqu, 0, sizeof (wrqu));
                          wrqu.ap_addr.sa_family = ARPHRD_ETHER;
                  	PRINT_K("wireless_send_event--->SIOCGIWAP(disassociated:vMgrJoinBSSBegin Fail !!)\n");
                  	wireless_send_event(pDevice->dev, SIOCGIWAP, &wrqu, NULL);
                       }
                    #endif
                }
            }
            s_bCommandComplete(pDevice);
            break;

        case WLAN_AUTHENTICATE_WAIT :
            DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"eCommandState == WLAN_AUTHENTICATE_WAIT\n");
            if (pMgmt->eCurrState == WMAC_STATE_AUTH) {
		pDevice->byLinkWaitCount = 0;
                // Call mgr to begin the association
                DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"eCurrState == WMAC_STATE_AUTH\n");
		vMgrAssocBeginSta((void *) pDevice, pMgmt, &Status);
                if (Status == CMD_STATUS_SUCCESS) {
                    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"eCommandState = WLAN_ASSOCIATE_WAIT\n");
		  pDevice->byLinkWaitCount = 0;
                    pDevice->eCommandState = WLAN_ASSOCIATE_WAIT;
		    vCommandTimerWait((void *) pDevice, ASSOCIATE_TIMEOUT);
                    spin_unlock_irq(&pDevice->lock);
                    return;
                }
            }
	   else if(pMgmt->eCurrState < WMAC_STATE_AUTHPENDING) {
               printk("WLAN_AUTHENTICATE_WAIT:Authen Fail???\n");
	   }
	   else  if(pDevice->byLinkWaitCount <= 4){    //mike add:wait another 2 sec if authenticated_frame delay!
                pDevice->byLinkWaitCount ++;
	       printk("WLAN_AUTHENTICATE_WAIT:wait %d times!!\n",pDevice->byLinkWaitCount);
	       spin_unlock_irq(&pDevice->lock);
	       vCommandTimerWait((void *) pDevice, AUTHENTICATE_TIMEOUT/2);
	       return;
	   }
	          pDevice->byLinkWaitCount = 0;

            s_bCommandComplete(pDevice);
            break;

        case WLAN_ASSOCIATE_WAIT :
            if (pMgmt->eCurrState == WMAC_STATE_ASSOC) {
                DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"eCurrState == WMAC_STATE_ASSOC\n");
                if (pDevice->ePSMode != WMAC_POWER_CAM) {
			PSvEnablePowerSaving((void *) pDevice,
					     pMgmt->wListenInterval);
                }
/*
                if (pMgmt->eAuthenMode >= WMAC_AUTH_WPA) {
                    KeybRemoveAllKey(pDevice, &(pDevice->sKey), pDevice->abyBSSID);
                }
*/
                pDevice->byLinkWaitCount = 0;
                pDevice->byReAssocCount = 0;
                pDevice->bLinkPass = TRUE;
                ControlvMaskByte(pDevice,MESSAGE_REQUEST_MACREG,MAC_REG_PAPEDELAY,LEDSTS_STS,LEDSTS_INTER);
                s_bClearBSSID_SCAN(pDevice);

                if (netif_queue_stopped(pDevice->dev)){
                    netif_wake_queue(pDevice->dev);
                }

		 if(pDevice->IsTxDataTrigger != FALSE)   {    //TxDataTimer is not triggered at the first time
                     // printk("Re-initial TxDataTimer****\n");
		    del_timer(&pDevice->sTimerTxData);
                      init_timer(&pDevice->sTimerTxData);
			pDevice->sTimerTxData.data = (unsigned long) pDevice;
                      pDevice->sTimerTxData.function = (TimerFunction)BSSvSecondTxData;
                      pDevice->sTimerTxData.expires = RUN_AT(10*HZ);      //10s callback
                      pDevice->fTxDataInSleep = FALSE;
                      pDevice->nTxDataTimeCout = 0;
		 }
		 else {
		   // printk("mike:-->First time trigger TimerTxData InSleep\n");
		 }
		pDevice->IsTxDataTrigger = TRUE;
                add_timer(&pDevice->sTimerTxData);

            }
	   else if(pMgmt->eCurrState < WMAC_STATE_ASSOCPENDING) {
               printk("WLAN_ASSOCIATE_WAIT:Association Fail???\n");
	   }
	   else  if(pDevice->byLinkWaitCount <= 4){    //mike add:wait another 2 sec if associated_frame delay!
                pDevice->byLinkWaitCount ++;
	       printk("WLAN_ASSOCIATE_WAIT:wait %d times!!\n",pDevice->byLinkWaitCount);
	       spin_unlock_irq(&pDevice->lock);
	       vCommandTimerWait((void *) pDevice, ASSOCIATE_TIMEOUT/2);
	       return;
	   }
	          pDevice->byLinkWaitCount = 0;

            s_bCommandComplete(pDevice);
            break;

        case WLAN_CMD_AP_MODE_START :
            DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"eCommandState == WLAN_CMD_AP_MODE_START\n");

            if (pMgmt->eConfigMode == WMAC_CONFIG_AP) {
                del_timer(&pMgmt->sTimerSecondCallback);
                pMgmt->eCurrState = WMAC_STATE_IDLE;
                pMgmt->eCurrMode = WMAC_MODE_STANDBY;
                pDevice->bLinkPass = FALSE;
                ControlvMaskByte(pDevice,MESSAGE_REQUEST_MACREG,MAC_REG_PAPEDELAY,LEDSTS_STS,LEDSTS_SLOW);
                if (pDevice->bEnableHostWEP == TRUE)
                    BSSvClearNodeDBTable(pDevice, 1);
                else
                    BSSvClearNodeDBTable(pDevice, 0);
                pDevice->uAssocCount = 0;
                pMgmt->eCurrState = WMAC_STATE_IDLE;
                pDevice->bFixRate = FALSE;

		vMgrCreateOwnIBSS((void *) pDevice, &Status);
		if (Status != CMD_STATUS_SUCCESS) {
			DBG_PRT(MSG_LEVEL_DEBUG,
				KERN_INFO "vMgrCreateOwnIBSS fail!\n");
                }
                // always turn off unicast bit
                MACvRegBitsOff(pDevice, MAC_REG_RCR, RCR_UNICAST);
                pDevice->byRxMode &= ~RCR_UNICAST;
                DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "wcmd: rx_mode = %x\n", pDevice->byRxMode );
                BSSvAddMulticastNode(pDevice);
                if (netif_queue_stopped(pDevice->dev)){
                    netif_wake_queue(pDevice->dev);
                }
                pDevice->bLinkPass = TRUE;
                ControlvMaskByte(pDevice,MESSAGE_REQUEST_MACREG,MAC_REG_PAPEDELAY,LEDSTS_STS,LEDSTS_INTER);
                add_timer(&pMgmt->sTimerSecondCallback);
            }
            s_bCommandComplete(pDevice);
            break;

        case WLAN_CMD_TX_PSPACKET_START :
            // DTIM Multicast tx
            if (pMgmt->sNodeDBTable[0].bRxPSPoll) {
                while ((skb = skb_dequeue(&pMgmt->sNodeDBTable[0].sTxPSQueue)) != NULL) {
                    if (skb_queue_empty(&pMgmt->sNodeDBTable[0].sTxPSQueue)) {
                        pMgmt->abyPSTxMap[0] &= ~byMask[0];
                        pDevice->bMoreData = FALSE;
                    }
                    else {
                        pDevice->bMoreData = TRUE;
                    }

                    if (nsDMA_tx_packet(pDevice, TYPE_AC0DMA, skb) != 0) {
                        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Multicast ps tx fail \n");
                    }

                    pMgmt->sNodeDBTable[0].wEnQueueCnt--;
                }
            }

            // PS nodes tx
            for (ii = 1; ii < (MAX_NODE_NUM + 1); ii++) {
                if (pMgmt->sNodeDBTable[ii].bActive &&
                    pMgmt->sNodeDBTable[ii].bRxPSPoll) {
                    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Index=%d Enqueu Cnt= %d\n",
                               ii, pMgmt->sNodeDBTable[ii].wEnQueueCnt);
                    while ((skb = skb_dequeue(&pMgmt->sNodeDBTable[ii].sTxPSQueue)) != NULL) {
                        if (skb_queue_empty(&pMgmt->sNodeDBTable[ii].sTxPSQueue)) {
                            // clear tx map
                            pMgmt->abyPSTxMap[pMgmt->sNodeDBTable[ii].wAID >> 3] &=
                                    ~byMask[pMgmt->sNodeDBTable[ii].wAID & 7];
                            pDevice->bMoreData = FALSE;
                        }
                        else {
                            pDevice->bMoreData = TRUE;
                        }

                        if (nsDMA_tx_packet(pDevice, TYPE_AC0DMA, skb) != 0) {
                            DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "sta ps tx fail \n");
                        }

                        pMgmt->sNodeDBTable[ii].wEnQueueCnt--;
                        // check if sta ps enable, wait next pspoll
                        // if sta ps disable, send all pending buffers.
                        if (pMgmt->sNodeDBTable[ii].bPSEnable)
                            break;
                    }
                    if (skb_queue_empty(&pMgmt->sNodeDBTable[ii].sTxPSQueue)) {
                        // clear tx map
                        pMgmt->abyPSTxMap[pMgmt->sNodeDBTable[ii].wAID >> 3] &=
                                    ~byMask[pMgmt->sNodeDBTable[ii].wAID & 7];
                        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Index=%d PS queue clear \n", ii);
                    }
                    pMgmt->sNodeDBTable[ii].bRxPSPoll = FALSE;
                }
            }

            s_bCommandComplete(pDevice);
            break;

        case WLAN_CMD_RADIO_START:

            DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"eCommandState == WLAN_CMD_RADIO_START\n");
       //     if (pDevice->bRadioCmd == TRUE)
       //         CARDbRadioPowerOn(pDevice);
       //     else
       //         CARDbRadioPowerOff(pDevice);

       {
	       int ntStatus = STATUS_SUCCESS;
        BYTE            byTmp;

        ntStatus = CONTROLnsRequestIn(pDevice,
                                    MESSAGE_TYPE_READ,
                                    MAC_REG_GPIOCTL1,
                                    MESSAGE_REQUEST_MACREG,
                                    1,
                                    &byTmp);

        if ( ntStatus != STATUS_SUCCESS ) {
                s_bCommandComplete(pDevice);
                spin_unlock_irq(&pDevice->lock);
                return;
        }
        if ( (byTmp & GPIO3_DATA) == 0 ) {
	    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO" WLAN_CMD_RADIO_START_OFF........................\n");
                // Old commands are useless.
                // empty command Q
	       pDevice->cbFreeCmdQueue = CMD_Q_SIZE;
                pDevice->uCmdDequeueIdx = 0;
                pDevice->uCmdEnqueueIdx = 0;
                //0415pDevice->bCmdRunning = FALSE;
                pDevice->bCmdClear = TRUE;
                pDevice->bStopTx0Pkt = FALSE;
                pDevice->bStopDataPkt = TRUE;

                pDevice->byKeyIndex = 0;
                pDevice->bTransmitKey = FALSE;
	    spin_unlock_irq(&pDevice->lock);
	    KeyvInitTable(pDevice,&pDevice->sKey);
	    spin_lock_irq(&pDevice->lock);
	       pMgmt->byCSSPK = KEY_CTL_NONE;
                pMgmt->byCSSGK = KEY_CTL_NONE;

	  if (pDevice->bLinkPass == TRUE) {
                // reason = 8 : disassoc because sta has left
		vMgrDisassocBeginSta((void *) pDevice,
				     pMgmt,
				     pMgmt->abyCurrBSSID,
				     (8),
				     &Status);
                       pDevice->bLinkPass = FALSE;
                // unlock command busy
                        pMgmt->eCurrState = WMAC_STATE_IDLE;
                        pMgmt->sNodeDBTable[0].bActive = FALSE;
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
	       #ifdef WPA_SUPPLICANT_DRIVER_WEXT_SUPPORT
	               pDevice->bwextstep0 = FALSE;
                        pDevice->bwextstep1 = FALSE;
                        pDevice->bwextstep2 = FALSE;
                        pDevice->bwextstep3 = FALSE;
		      pDevice->bWPASuppWextEnabled = FALSE;
		#endif
	                  //clear current SSID
                  pItemSSID = (PWLAN_IE_SSID)pMgmt->abyCurrSSID;
                  pItemSSID->len = 0;
                  memset(pItemSSID->abySSID, 0, WLAN_SSID_MAXLEN);
                //clear desired SSID
                pItemSSID = (PWLAN_IE_SSID)pMgmt->abyDesireSSID;
                pItemSSID->len = 0;
                memset(pItemSSID->abySSID, 0, WLAN_SSID_MAXLEN);

	    netif_stop_queue(pDevice->dev);
	    CARDbRadioPowerOff(pDevice);
             MACvRegBitsOn(pDevice,MAC_REG_GPIOCTL1,GPIO3_INTMD);
	    ControlvMaskByte(pDevice,MESSAGE_REQUEST_MACREG,MAC_REG_PAPEDELAY,LEDSTS_STS,LEDSTS_OFF);
	    pDevice->bHWRadioOff = TRUE;
        } else {
            DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO" WLAN_CMD_RADIO_START_ON........................\n");
            pDevice->bHWRadioOff = FALSE;
                CARDbRadioPowerOn(pDevice);
            MACvRegBitsOff(pDevice,MAC_REG_GPIOCTL1,GPIO3_INTMD);
            ControlvMaskByte(pDevice,MESSAGE_REQUEST_MACREG,MAC_REG_PAPEDELAY,LEDSTS_STS,LEDSTS_ON);
        }
      }

            s_bCommandComplete(pDevice);
            break;


        case WLAN_CMD_CHANGE_BBSENSITIVITY_START:

            pDevice->bStopDataPkt = TRUE;
            pDevice->byBBVGACurrent = pDevice->byBBVGANew;
            BBvSetVGAGainOffset(pDevice, pDevice->byBBVGACurrent);
            DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"Change sensitivity pDevice->byBBVGACurrent = %x\n", pDevice->byBBVGACurrent);
            pDevice->bStopDataPkt = FALSE;
            s_bCommandComplete(pDevice);
            break;

        case WLAN_CMD_TBTT_WAKEUP_START:
            PSbIsNextTBTTWakeUp(pDevice);
            s_bCommandComplete(pDevice);
            break;

        case WLAN_CMD_BECON_SEND_START:
            bMgrPrepareBeaconToSend(pDevice, pMgmt);
            s_bCommandComplete(pDevice);
            break;

        case WLAN_CMD_SETPOWER_START:

            RFbSetPower(pDevice, pDevice->wCurrentRate, pMgmt->uCurrChannel);

            s_bCommandComplete(pDevice);
            break;

        case WLAN_CMD_CHANGE_ANTENNA_START:
            DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"Change from Antenna%d to", (int)pDevice->dwRxAntennaSel);
            if ( pDevice->dwRxAntennaSel == 0) {
                pDevice->dwRxAntennaSel=1;
                if (pDevice->bTxRxAntInv == TRUE)
                    BBvSetAntennaMode(pDevice, ANT_RXA);
                else
                    BBvSetAntennaMode(pDevice, ANT_RXB);
            } else {
                pDevice->dwRxAntennaSel=0;
                if (pDevice->bTxRxAntInv == TRUE)
                    BBvSetAntennaMode(pDevice, ANT_RXB);
                else
                    BBvSetAntennaMode(pDevice, ANT_RXA);
            }
            s_bCommandComplete(pDevice);
            break;

        case WLAN_CMD_REMOVE_ALLKEY_START:
            KeybRemoveAllKey(pDevice, &(pDevice->sKey), pDevice->abyBSSID);
            s_bCommandComplete(pDevice);
            break;


        case WLAN_CMD_MAC_DISPOWERSAVING_START:
            ControlvReadByte (pDevice, MESSAGE_REQUEST_MACREG, MAC_REG_PSCTL, &byData);
            if ( (byData & PSCTL_PS) != 0 ) {
                // disable power saving hw function
                CONTROLnsRequestOut(pDevice,
                                MESSAGE_TYPE_DISABLE_PS,
                                0,
                                0,
                                0,
                                NULL
                                );
            }
            s_bCommandComplete(pDevice);
            break;

        case WLAN_CMD_11H_CHSW_START:
            CARDbSetMediaChannel(pDevice, pDevice->byNewChannel);
            pDevice->bChannelSwitch = FALSE;
            pMgmt->uCurrChannel = pDevice->byNewChannel;
            pDevice->bStopDataPkt = FALSE;
            s_bCommandComplete(pDevice);
            break;

        default:
            s_bCommandComplete(pDevice);
            break;
    } //switch

    spin_unlock_irq(&pDevice->lock);
    return;
}


static
BOOL
s_bCommandComplete (
    PSDevice pDevice
    )
{
    PWLAN_IE_SSID pSSID;
    BOOL          bRadioCmd = FALSE;
    //WORD          wDeAuthenReason = 0;
    BOOL          bForceSCAN = TRUE;
    PSMgmtObject  pMgmt = &(pDevice->sMgmtObj);


    pDevice->eCommandState = WLAN_CMD_IDLE;
    if (pDevice->cbFreeCmdQueue == CMD_Q_SIZE) {
        //Command Queue Empty
        pDevice->bCmdRunning = FALSE;
        return TRUE;
    }
    else {
        pDevice->eCommand = pDevice->eCmdQueue[pDevice->uCmdDequeueIdx].eCmd;
        pSSID = (PWLAN_IE_SSID)pDevice->eCmdQueue[pDevice->uCmdDequeueIdx].abyCmdDesireSSID;
        bRadioCmd = pDevice->eCmdQueue[pDevice->uCmdDequeueIdx].bRadioCmd;
        bForceSCAN = pDevice->eCmdQueue[pDevice->uCmdDequeueIdx].bForceSCAN;
        ADD_ONE_WITH_WRAP_AROUND(pDevice->uCmdDequeueIdx, CMD_Q_SIZE);
        pDevice->cbFreeCmdQueue++;
        pDevice->bCmdRunning = TRUE;
        switch ( pDevice->eCommand ) {
            case WLAN_CMD_BSSID_SCAN:
                DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"eCommandState= WLAN_CMD_BSSID_SCAN\n");
                pDevice->eCommandState = WLAN_CMD_SCAN_START;
                pMgmt->uScanChannel = 0;
                if (pSSID->len != 0) {
                    memcpy(pMgmt->abyScanSSID, pSSID, WLAN_IEHDR_LEN + WLAN_SSID_MAXLEN + 1);
                } else {
                    memset(pMgmt->abyScanSSID, 0, WLAN_IEHDR_LEN + WLAN_SSID_MAXLEN + 1);
                }
/*
                if ((bForceSCAN == FALSE) && (pDevice->bLinkPass == TRUE)) {
                    if ((pSSID->len == ((PWLAN_IE_SSID)pMgmt->abyCurrSSID)->len) &&
                        ( !memcmp(pSSID->abySSID, ((PWLAN_IE_SSID)pMgmt->abyCurrSSID)->abySSID, pSSID->len))) {
                        pDevice->eCommandState = WLAN_CMD_IDLE;
                    }
                }
*/
                break;
            case WLAN_CMD_SSID:
                pDevice->eCommandState = WLAN_CMD_SSID_START;
                if (pSSID->len > WLAN_SSID_MAXLEN)
                    pSSID->len = WLAN_SSID_MAXLEN;
                if (pSSID->len != 0)
                    memcpy(pMgmt->abyDesireSSID, pSSID, WLAN_IEHDR_LEN + WLAN_SSID_MAXLEN + 1);
                DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"eCommandState= WLAN_CMD_SSID_START\n");
                break;
            case WLAN_CMD_DISASSOCIATE:
                pDevice->eCommandState = WLAN_CMD_DISASSOCIATE_START;
                break;
            case WLAN_CMD_RX_PSPOLL:
                pDevice->eCommandState = WLAN_CMD_TX_PSPACKET_START;
                break;
            case WLAN_CMD_RUN_AP:
                pDevice->eCommandState = WLAN_CMD_AP_MODE_START;
                break;
            case WLAN_CMD_RADIO:
                pDevice->eCommandState = WLAN_CMD_RADIO_START;
                pDevice->bRadioCmd = bRadioCmd;
                break;
            case WLAN_CMD_CHANGE_BBSENSITIVITY:
                pDevice->eCommandState = WLAN_CMD_CHANGE_BBSENSITIVITY_START;
                break;

            case WLAN_CMD_TBTT_WAKEUP:
                pDevice->eCommandState = WLAN_CMD_TBTT_WAKEUP_START;
                break;

            case WLAN_CMD_BECON_SEND:
                pDevice->eCommandState = WLAN_CMD_BECON_SEND_START;
                break;

            case WLAN_CMD_SETPOWER:
                pDevice->eCommandState = WLAN_CMD_SETPOWER_START;
                break;

            case WLAN_CMD_CHANGE_ANTENNA:
                pDevice->eCommandState = WLAN_CMD_CHANGE_ANTENNA_START;
                break;

            case WLAN_CMD_REMOVE_ALLKEY:
                pDevice->eCommandState = WLAN_CMD_REMOVE_ALLKEY_START;
                break;

            case WLAN_CMD_MAC_DISPOWERSAVING:
                pDevice->eCommandState = WLAN_CMD_MAC_DISPOWERSAVING_START;
                break;

            case WLAN_CMD_11H_CHSW:
                pDevice->eCommandState = WLAN_CMD_11H_CHSW_START;
                break;

            default:
                break;

        }
	vCommandTimerWait((void *) pDevice, 0);
    }

    return TRUE;
}

BOOL bScheduleCommand(void *hDeviceContext,
		      CMD_CODE eCommand,
		      PBYTE pbyItem0)
{
    PSDevice        pDevice = (PSDevice)hDeviceContext;


    if (pDevice->cbFreeCmdQueue == 0) {
        return (FALSE);
    }
    pDevice->eCmdQueue[pDevice->uCmdEnqueueIdx].eCmd = eCommand;
    pDevice->eCmdQueue[pDevice->uCmdEnqueueIdx].bForceSCAN = TRUE;
    memset(pDevice->eCmdQueue[pDevice->uCmdEnqueueIdx].abyCmdDesireSSID, 0 , WLAN_IEHDR_LEN + WLAN_SSID_MAXLEN + 1);
    if (pbyItem0 != NULL) {
        switch (eCommand) {
            case WLAN_CMD_BSSID_SCAN:
                pDevice->eCmdQueue[pDevice->uCmdEnqueueIdx].bForceSCAN = FALSE;
                memcpy(pDevice->eCmdQueue[pDevice->uCmdEnqueueIdx].abyCmdDesireSSID,
                         pbyItem0, WLAN_IEHDR_LEN + WLAN_SSID_MAXLEN + 1);
                break;

            case WLAN_CMD_SSID:
                memcpy(pDevice->eCmdQueue[pDevice->uCmdEnqueueIdx].abyCmdDesireSSID,
                         pbyItem0, WLAN_IEHDR_LEN + WLAN_SSID_MAXLEN + 1);
                break;

            case WLAN_CMD_DISASSOCIATE:
                pDevice->eCmdQueue[pDevice->uCmdEnqueueIdx].bNeedRadioOFF = *((int *)pbyItem0);
                break;
/*
            case WLAN_CMD_DEAUTH:
                pDevice->eCmdQueue[pDevice->uCmdEnqueueIdx].wDeAuthenReason = *((PWORD)pbyItem0);
                break;
*/

            case WLAN_CMD_RADIO:
                pDevice->eCmdQueue[pDevice->uCmdEnqueueIdx].bRadioCmd = *((int *)pbyItem0);
                break;

            default:
                break;
        }
    }

    ADD_ONE_WITH_WRAP_AROUND(pDevice->uCmdEnqueueIdx, CMD_Q_SIZE);
    pDevice->cbFreeCmdQueue--;

    if (pDevice->bCmdRunning == FALSE) {
        s_bCommandComplete(pDevice);
    }
    else {
    }
    return (TRUE);

}

/*
 * Description:
 *      Clear BSSID_SCAN cmd in CMD Queue
 *
 * Parameters:
 *  In:
 *      hDeviceContext  - Pointer to the adapter
 *      eCommand        - Command
 *  Out:
 *      none
 *
 * Return Value: TRUE if success; otherwise FALSE
 *
 */
static BOOL s_bClearBSSID_SCAN(void *hDeviceContext)
{
    PSDevice        pDevice = (PSDevice)hDeviceContext;
    unsigned int            uCmdDequeueIdx = pDevice->uCmdDequeueIdx;
    unsigned int            ii;

    if ((pDevice->cbFreeCmdQueue < CMD_Q_SIZE) && (uCmdDequeueIdx != pDevice->uCmdEnqueueIdx)) {
        for (ii = 0; ii < (CMD_Q_SIZE - pDevice->cbFreeCmdQueue); ii ++) {
            if (pDevice->eCmdQueue[uCmdDequeueIdx].eCmd == WLAN_CMD_BSSID_SCAN)
                pDevice->eCmdQueue[uCmdDequeueIdx].eCmd = WLAN_CMD_IDLE;
            ADD_ONE_WITH_WRAP_AROUND(uCmdDequeueIdx, CMD_Q_SIZE);
            if (uCmdDequeueIdx == pDevice->uCmdEnqueueIdx)
                break;
        }
    }
    return TRUE;
}


//mike add:reset command timer
void vResetCommandTimer(void *hDeviceContext)
{
	PSDevice pDevice = (PSDevice)hDeviceContext;

	//delete timer
	del_timer(&pDevice->sTimerCommand);
	//init timer
	init_timer(&pDevice->sTimerCommand);
	pDevice->sTimerCommand.data = (unsigned long)pDevice;
	pDevice->sTimerCommand.function = (TimerFunction)vRunCommand;
	pDevice->sTimerCommand.expires = RUN_AT(HZ);
	pDevice->cbFreeCmdQueue = CMD_Q_SIZE;
	pDevice->uCmdDequeueIdx = 0;
	pDevice->uCmdEnqueueIdx = 0;
	pDevice->eCommandState = WLAN_CMD_IDLE;
	pDevice->bCmdRunning = FALSE;
	pDevice->bCmdClear = FALSE;
}

void BSSvSecondTxData(void *hDeviceContext)
{
	PSDevice pDevice = (PSDevice)hDeviceContext;
	PSMgmtObject pMgmt = &(pDevice->sMgmtObj);

	pDevice->nTxDataTimeCout++;

	if (pDevice->nTxDataTimeCout < 4) {   //don't tx data if timer less than 40s
		// printk("mike:%s-->no data Tx not exceed the desired Time as %d\n",__FUNCTION__,
		//  	(int)pDevice->nTxDataTimeCout);
		pDevice->sTimerTxData.expires = RUN_AT(10 * HZ);      //10s callback
		add_timer(&pDevice->sTimerTxData);
		return;
	}

	spin_lock_irq(&pDevice->lock);
	//is wap_supplicant running successful OR only open && sharekey mode!
	if (((pDevice->bLinkPass == TRUE) &&
		(pMgmt->eAuthenMode < WMAC_AUTH_WPA)) ||  //open && sharekey linking
		(pDevice->fWPA_Authened == TRUE)) {   //wpa linking
		//   printk("mike:%s-->InSleep Tx Data Procedure\n",__FUNCTION__);
		pDevice->fTxDataInSleep = TRUE;
		PSbSendNullPacket(pDevice);      //send null packet
		pDevice->fTxDataInSleep = FALSE;
	}
	spin_unlock_irq(&pDevice->lock);

	pDevice->sTimerTxData.expires = RUN_AT(10 * HZ);      //10s callback
	add_timer(&pDevice->sTimerTxData);
}
