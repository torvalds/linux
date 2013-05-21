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
 * File: int.c
 *
 * Purpose: Handle USB interrupt endpoint
 *
 * Author: Jerry Chen
 *
 * Date: Apr. 2, 2004
 *
 * Functions:
 *
 * Revision History:
 *      04-02-2004 Jerry Chen:  Initial release
 *
 */

#include "int.h"
#include "mib.h"
#include "tmacro.h"
#include "mac.h"
#include "power.h"
#include "bssdb.h"
#include "usbpipe.h"

static int msglevel = MSG_LEVEL_INFO; /* MSG_LEVEL_DEBUG */

/*+
 *
 *  Function:   InterruptPollingThread
 *
 *  Synopsis:   Thread running at IRQL PASSIVE_LEVEL.
 *
 *  Arguments: Device Extension
 *
 *  Returns:
 *
 *  Algorithm:  Call USBD for input data;
 *
 *  History:    dd-mm-yyyy   Author    Comment
 *
 *
 *  Notes:
 *
 *  USB reads are by nature 'Blocking', and when in a read, the device looks
 *  like it's in a 'stall' condition, so we deliberately time out every second
 *  if we've gotten no data
 *
-*/
void INTvWorkItem(struct vnt_private *pDevice)
{
	int ntStatus;

	DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"---->Interrupt Polling Thread\n");

	spin_lock_irq(&pDevice->lock);
	if (pDevice->fKillEventPollingThread != true)
		ntStatus = PIPEnsInterruptRead(pDevice);
	spin_unlock_irq(&pDevice->lock);
}

void INTnsProcessData(struct vnt_private *pDevice)
{
	PSINTData pINTData;
	struct vnt_manager *pMgmt = &pDevice->vnt_mgmt;
	struct net_device_stats *pStats = &pDevice->stats;

	DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"---->s_nsInterruptProcessData\n");

	pINTData = (PSINTData) pDevice->intBuf.pDataBuf;
	if (pINTData->byTSR0 & TSR_VALID) {
		STAvUpdateTDStatCounter(&(pDevice->scStatistic),
					(u8)(pINTData->byPkt0 & 0x0F),
					(u8)(pINTData->byPkt0>>4),
					pINTData->byTSR0);
		BSSvUpdateNodeTxCounter(pDevice,
					&(pDevice->scStatistic),
					pINTData->byTSR0,
					pINTData->byPkt0);
		/*DBG_PRN_GRP01(("TSR0 %02x\n", pINTData->byTSR0));*/
	}
	if (pINTData->byTSR1 & TSR_VALID) {
		STAvUpdateTDStatCounter(&(pDevice->scStatistic),
					(u8)(pINTData->byPkt1 & 0x0F),
					(u8)(pINTData->byPkt1>>4),
					pINTData->byTSR1);
		BSSvUpdateNodeTxCounter(pDevice,
					&(pDevice->scStatistic),
					pINTData->byTSR1,
					pINTData->byPkt1);
		/*DBG_PRN_GRP01(("TSR1 %02x\n", pINTData->byTSR1));*/
	}
	if (pINTData->byTSR2 & TSR_VALID) {
		STAvUpdateTDStatCounter(&(pDevice->scStatistic),
					(u8)(pINTData->byPkt2 & 0x0F),
					(u8)(pINTData->byPkt2>>4),
					pINTData->byTSR2);
		BSSvUpdateNodeTxCounter(pDevice,
					&(pDevice->scStatistic),
					pINTData->byTSR2,
					pINTData->byPkt2);
		/*DBG_PRN_GRP01(("TSR2 %02x\n", pINTData->byTSR2));*/
	}
	if (pINTData->byTSR3 & TSR_VALID) {
		STAvUpdateTDStatCounter(&(pDevice->scStatistic),
					(u8)(pINTData->byPkt3 & 0x0F),
					(u8)(pINTData->byPkt3>>4),
					pINTData->byTSR3);
		BSSvUpdateNodeTxCounter(pDevice,
					&(pDevice->scStatistic),
					pINTData->byTSR3,
					pINTData->byPkt3);
		/*DBG_PRN_GRP01(("TSR3 %02x\n", pINTData->byTSR3));*/
	}
	if (pINTData->byISR0 != 0) {
		if (pINTData->byISR0 & ISR_BNTX) {
			if (pDevice->eOPMode == OP_MODE_AP) {
				if (pMgmt->byDTIMCount > 0) {
					pMgmt->byDTIMCount--;
					pMgmt->sNodeDBTable[0].bRxPSPoll =
						false;
				} else if (pMgmt->byDTIMCount == 0) {
					/* check if multicast tx buffering */
					pMgmt->byDTIMCount =
						pMgmt->byDTIMPeriod-1;
					pMgmt->sNodeDBTable[0].bRxPSPoll = true;
					if (pMgmt->sNodeDBTable[0].bPSEnable)
						bScheduleCommand((void *) pDevice,
								 WLAN_CMD_RX_PSPOLL,
								 NULL);
				}
				bScheduleCommand((void *) pDevice,
						WLAN_CMD_BECON_SEND,
						NULL);
			} /* if (pDevice->eOPMode == OP_MODE_AP) */
		pDevice->bBeaconSent = true;
		} else {
			pDevice->bBeaconSent = false;
		}
		if (pINTData->byISR0 & ISR_TBTT) {
			if (pDevice->bEnablePSMode)
				bScheduleCommand((void *) pDevice,
						WLAN_CMD_TBTT_WAKEUP,
						NULL);
			if (pDevice->bChannelSwitch) {
				pDevice->byChannelSwitchCount--;
				if (pDevice->byChannelSwitchCount == 0)
					bScheduleCommand((void *) pDevice,
							WLAN_CMD_11H_CHSW,
							NULL);
			}
		}
		pDevice->qwCurrTSF = cpu_to_le64(pINTData->qwTSF);
		/*DBG_PRN_GRP01(("ISR0 = %02x ,
		  LoTsf =  %08x,
		  HiTsf =  %08x\n",
		  pINTData->byISR0,
		  pINTData->dwLoTSF,
		  pINTData->dwHiTSF)); */

		STAvUpdate802_11Counter(&pDevice->s802_11Counter,
					&pDevice->scStatistic,
					pINTData->byRTSSuccess,
					pINTData->byRTSFail,
					pINTData->byACKFail,
					pINTData->byFCSErr);
		STAvUpdateIsrStatCounter(&pDevice->scStatistic,
					pINTData->byISR0,
					pINTData->byISR1);
	}
	if (pINTData->byISR1 != 0)
		if (pINTData->byISR1 & ISR_GPIO3)
			bScheduleCommand((void *) pDevice,
					WLAN_CMD_RADIO,
					NULL);
	pDevice->intBuf.uDataLen = 0;
	pDevice->intBuf.bInUse = false;

	pStats->tx_packets = pDevice->scStatistic.ullTsrOK;
	pStats->tx_bytes = pDevice->scStatistic.ullTxDirectedBytes +
		pDevice->scStatistic.ullTxMulticastBytes +
		pDevice->scStatistic.ullTxBroadcastBytes;
	pStats->tx_errors = pDevice->scStatistic.dwTsrErr;
	pStats->tx_dropped = pDevice->scStatistic.dwTsrErr;
}
