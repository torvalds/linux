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
 * Purpose: Handles 802.11 power management functions
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

#include "mac.h"
#include "device.h"
#include "wmgr.h"
#include "power.h"
#include "wcmd.h"
#include "rxtx.h"
#include "card.h"
#include "control.h"
#include "rndis.h"

static int msglevel = MSG_LEVEL_INFO;

/*
 *
 * Routine Description:
 * Enable hw power saving functions
 *
 * Return Value:
 *    None.
 *
 */

void PSvEnablePowerSaving(struct vnt_private *pDevice, u16 wListenInterval)
{
	struct vnt_manager *pMgmt = &pDevice->vnt_mgmt;
	u16 wAID = pMgmt->wCurrAID | BIT14 | BIT15;

	/* set period of power up before TBTT */
	MACvWriteWord(pDevice, MAC_REG_PWBT, C_PWBT);

	if (pDevice->eOPMode != OP_MODE_ADHOC) {
		/* set AID */
		MACvWriteWord(pDevice, MAC_REG_AIDATIM, wAID);
	} else {
		/* set ATIM Window */
		/* MACvWriteATIMW(pDevice->PortOffset, pMgmt->wCurrATIMWindow); */
	}

	/* Warren:06-18-2004,the sequence must follow PSEN->AUTOSLEEP->GO2DOZE */
	/* enable power saving hw function */
	MACvRegBitsOn(pDevice, MAC_REG_PSCTL, PSCTL_PSEN);

	/* Set AutoSleep */
	MACvRegBitsOn(pDevice, MAC_REG_PSCFG, PSCFG_AUTOSLEEP);

	/* Warren:MUST turn on this once before turn on AUTOSLEEP ,or the AUTOSLEEP doesn't work */
	MACvRegBitsOn(pDevice, MAC_REG_PSCTL, PSCTL_GO2DOZE);

	if (wListenInterval >= 2) {

		/* clear always listen beacon */
		MACvRegBitsOff(pDevice, MAC_REG_PSCTL, PSCTL_ALBCN);

		/* first time set listen next beacon */
		MACvRegBitsOn(pDevice, MAC_REG_PSCTL, PSCTL_LNBCN);

		pMgmt->wCountToWakeUp = wListenInterval;

	} else {

		/* always listen beacon */
		MACvRegBitsOn(pDevice, MAC_REG_PSCTL, PSCTL_ALBCN);

		pMgmt->wCountToWakeUp = 0;
	}

	pDevice->bEnablePSMode = true;

	/* We don't send null pkt in ad hoc mode since beacon will handle this. */
	if (pDevice->eOPMode == OP_MODE_INFRASTRUCTURE)
		PSbSendNullPacket(pDevice);

	pDevice->bPWBitOn = true;
	DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "PS:Power Saving Mode Enable...\n");
}

/*
 *
 * Routine Description:
 * Disable hw power saving functions
 *
 * Return Value:
 *    None.
 *
 */

void PSvDisablePowerSaving(struct vnt_private *pDevice)
{

	/* disable power saving hw function */
	CONTROLnsRequestOut(pDevice, MESSAGE_TYPE_DISABLE_PS, 0,
						0, 0, NULL);

	/* clear AutoSleep */
	MACvRegBitsOff(pDevice, MAC_REG_PSCFG, PSCFG_AUTOSLEEP);

	/* set always listen beacon */
	MACvRegBitsOn(pDevice, MAC_REG_PSCTL, PSCTL_ALBCN);
	pDevice->bEnablePSMode = false;

	if (pDevice->eOPMode == OP_MODE_INFRASTRUCTURE)
		PSbSendNullPacket(pDevice);

	pDevice->bPWBitOn = false;
}

/*
 *
 * Routine Description:
 * Consider to power down when no more packets to tx or rx.
 *
 * Return Value:
 *    true, if power down success
 *    false, if fail
 */

int PSbConsiderPowerDown(struct vnt_private *pDevice, int bCheckRxDMA,
	int bCheckCountToWakeUp)
{
	struct vnt_manager *pMgmt = &pDevice->vnt_mgmt;
	u8 byData;

	/* check if already in Doze mode */
	ControlvReadByte(pDevice, MESSAGE_REQUEST_MACREG,
					MAC_REG_PSCTL, &byData);

	if ((byData & PSCTL_PS) != 0)
		return true;

	if (pMgmt->eCurrMode != WMAC_MODE_IBSS_STA) {
		/* check if in TIM wake period */
		if (pMgmt->bInTIMWake)
			return false;
	}

	/* check scan state */
	if (pDevice->bCmdRunning)
		return false;

	/* Tx Burst */
	if (pDevice->bPSModeTxBurst)
		return false;

	/* Froce PSEN on */
	MACvRegBitsOn(pDevice, MAC_REG_PSCTL, PSCTL_PSEN);

	if (pMgmt->eCurrMode != WMAC_MODE_IBSS_STA) {
		if (bCheckCountToWakeUp && (pMgmt->wCountToWakeUp == 0
			|| pMgmt->wCountToWakeUp == 1)) {
				return false;
		}
	}

	pDevice->bPSRxBeacon = true;

	/* no Tx, no Rx isr, now go to Doze */
	MACvRegBitsOn(pDevice, MAC_REG_PSCTL, PSCTL_GO2DOZE);
	DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Go to Doze ZZZZZZZZZZZZZZZ\n");
	return true;
}

/*
 *
 * Routine Description:
 * Send PS-POLL packet
 *
 * Return Value:
 *    None.
 *
 */

void PSvSendPSPOLL(struct vnt_private *pDevice)
{
	struct vnt_manager *pMgmt = &pDevice->vnt_mgmt;
	struct vnt_tx_mgmt *pTxPacket = NULL;

	memset(pMgmt->pbyPSPacketPool, 0, sizeof(struct vnt_tx_mgmt)
		+ WLAN_HDR_ADDR2_LEN);
	pTxPacket = (struct vnt_tx_mgmt *)pMgmt->pbyPSPacketPool;
	pTxPacket->p80211Header = (PUWLAN_80211HDR)((u8 *)pTxPacket
		+ sizeof(struct vnt_tx_mgmt));

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

	/* log failure if sending failed */
	if (csMgmt_xmit(pDevice, pTxPacket) != CMD_STATUS_PENDING) {
		DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Send PS-Poll packet failed..\n");
	}
}

/*
 *
 * Routine Description:
 * Send NULL packet to AP for notification power state of STA
 *
 * Return Value:
 *    None.
 *
 */

int PSbSendNullPacket(struct vnt_private *pDevice)
{
	struct vnt_tx_mgmt *pTxPacket = NULL;
	struct vnt_manager *pMgmt = &pDevice->vnt_mgmt;
	u16 flags = 0;

	if (pDevice->bLinkPass == false)
		return false;

	if ((pDevice->bEnablePSMode == false) &&
		(pDevice->fTxDataInSleep == false)) {
			return false;
	}

	memset(pMgmt->pbyPSPacketPool, 0, sizeof(struct vnt_tx_mgmt)
		+ WLAN_NULLDATA_FR_MAXLEN);
	pTxPacket = (struct vnt_tx_mgmt *)pMgmt->pbyPSPacketPool;
	pTxPacket->p80211Header = (PUWLAN_80211HDR)((u8 *)pTxPacket
		+ sizeof(struct vnt_tx_mgmt));

	flags = WLAN_SET_FC_FTYPE(WLAN_TYPE_DATA) |
                        WLAN_SET_FC_FSTYPE(WLAN_FSTYPE_NULL);

	if (pDevice->bEnablePSMode)
		flags |= WLAN_SET_FC_PWRMGT(1);
	else
		flags |= WLAN_SET_FC_PWRMGT(0);

	pTxPacket->p80211Header->sA3.wFrameCtl = cpu_to_le16(flags);

	if (pMgmt->eCurrMode != WMAC_MODE_IBSS_STA)
		pTxPacket->p80211Header->sA3.wFrameCtl |= cpu_to_le16((u16)WLAN_SET_FC_TODS(1));

	memcpy(pTxPacket->p80211Header->sA3.abyAddr1, pMgmt->abyCurrBSSID, WLAN_ADDR_LEN);
	memcpy(pTxPacket->p80211Header->sA3.abyAddr2, pMgmt->abyMACAddr, WLAN_ADDR_LEN);
	memcpy(pTxPacket->p80211Header->sA3.abyAddr3, pMgmt->abyCurrBSSID, WLAN_BSSID_LEN);
	pTxPacket->cbMPDULen = WLAN_HDR_ADDR3_LEN;
	pTxPacket->cbPayloadLen = 0;
	/* log error if sending failed */
	if (csMgmt_xmit(pDevice, pTxPacket) != CMD_STATUS_PENDING) {
		DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Send Null Packet failed !\n");
		return false;
	}
	return true;
}

/*
 *
 * Routine Description:
 * Check if Next TBTT must wake up
 *
 * Return Value:
 *    None.
 *
 */

int PSbIsNextTBTTWakeUp(struct vnt_private *pDevice)
{
	struct vnt_manager *pMgmt = &pDevice->vnt_mgmt;
	int bWakeUp = false;

	if (pMgmt->wListenInterval >= 2) {
		if (pMgmt->wCountToWakeUp == 0)
			pMgmt->wCountToWakeUp = pMgmt->wListenInterval;

		pMgmt->wCountToWakeUp--;

		if (pMgmt->wCountToWakeUp == 1) {
			/* Turn on wake up to listen next beacon */
			MACvRegBitsOn(pDevice, MAC_REG_PSCTL, PSCTL_LNBCN);
			pDevice->bPSRxBeacon = false;
			bWakeUp = true;
		} else if (!pDevice->bPSRxBeacon) {
			/* Listen until RxBeacon */
			MACvRegBitsOn(pDevice, MAC_REG_PSCTL, PSCTL_LNBCN);
		}
	}
	return bWakeUp;
}

