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

#include "device.h"
#include "mac.h"
#include "card.h"
#include "wcmd.h"
#include "power.h"
#include "baseband.h"
#include "usbpipe.h"
#include "rxtx.h"
#include "rf.h"
#include "channel.h"

static int msglevel = MSG_LEVEL_INFO;
//static int msglevel = MSG_LEVEL_DEBUG;

static int s_bCommandComplete(struct vnt_private *);

static void vCommandTimerWait(struct vnt_private *priv, unsigned long msecs)
{
	schedule_delayed_work(&priv->run_command_work, msecs_to_jiffies(msecs));
}

void vRunCommand(struct work_struct *work)
{
	struct vnt_private *pDevice =
		container_of(work, struct vnt_private, run_command_work.work);

	if (pDevice->Flags & fMP_DISCONNECTED)
		return;

	if (pDevice->bCmdRunning != true)
		return;

	switch (pDevice->eCommandState) {
	case WLAN_CMD_INIT_MAC80211_START:
		if (pDevice->mac_hw)
			break;

		dev_info(&pDevice->usb->dev, "Starting mac80211\n");

		if (vnt_init(pDevice)) {
			/* If fail all ends TODO retry */
			dev_err(&pDevice->usb->dev, "failed to start\n");
			ieee80211_free_hw(pDevice->hw);
			return;
		}

		break;

	case WLAN_CMD_TBTT_WAKEUP_START:
		vnt_next_tbtt_wakeup(pDevice);
		break;

	case WLAN_CMD_BECON_SEND_START:
		if (!pDevice->vif)
			break;

		vnt_beacon_make(pDevice, pDevice->vif);

		vnt_mac_reg_bits_on(pDevice, MAC_REG_TCR, TCR_AUTOBCNTX);

		break;

	case WLAN_CMD_SETPOWER_START:

		vnt_rf_setpower(pDevice, pDevice->wCurrentRate,
				pDevice->hw->conf.chandef.chan->hw_value);

		break;

	case WLAN_CMD_CHANGE_ANTENNA_START:
		DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"Change from Antenna%d to", (int)pDevice->dwRxAntennaSel);
		if (pDevice->dwRxAntennaSel == 0) {
			pDevice->dwRxAntennaSel = 1;
			if (pDevice->bTxRxAntInv == true)
				BBvSetAntennaMode(pDevice, ANT_RXA);
			else
				BBvSetAntennaMode(pDevice, ANT_RXB);
		} else {
			pDevice->dwRxAntennaSel = 0;
			if (pDevice->bTxRxAntInv == true)
				BBvSetAntennaMode(pDevice, ANT_RXB);
			else
				BBvSetAntennaMode(pDevice, ANT_RXA);
		}
		break;

	case WLAN_CMD_11H_CHSW_START:
		vnt_set_channel(pDevice, pDevice->hw->conf.chandef.chan->hw_value);
		break;

	default:
		break;
	} //switch

	s_bCommandComplete(pDevice);

	return;
}

static int s_bCommandComplete(struct vnt_private *pDevice)
{
	int bRadioCmd = false;
	int bForceSCAN = true;

	pDevice->eCommandState = WLAN_CMD_IDLE;
	if (pDevice->cbFreeCmdQueue == CMD_Q_SIZE) {
		//Command Queue Empty
		pDevice->bCmdRunning = false;
		return true;
	} else {
		pDevice->eCommand = pDevice->eCmdQueue[pDevice->uCmdDequeueIdx].eCmd;
		bRadioCmd = pDevice->eCmdQueue[pDevice->uCmdDequeueIdx].bRadioCmd;
		bForceSCAN = pDevice->eCmdQueue[pDevice->uCmdDequeueIdx].bForceSCAN;
		ADD_ONE_WITH_WRAP_AROUND(pDevice->uCmdDequeueIdx, CMD_Q_SIZE);
		pDevice->cbFreeCmdQueue++;
		pDevice->bCmdRunning = true;
		switch (pDevice->eCommand) {
		case WLAN_CMD_INIT_MAC80211:
			pDevice->eCommandState = WLAN_CMD_INIT_MAC80211_START;
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

		case WLAN_CMD_11H_CHSW:
			pDevice->eCommandState = WLAN_CMD_11H_CHSW_START;
			break;

		default:
			break;
		}
		vCommandTimerWait(pDevice, 0);
	}

	return true;
}

int bScheduleCommand(struct vnt_private *pDevice,
		CMD_CODE eCommand, u8 *pbyItem0)
{

	if (pDevice->cbFreeCmdQueue == 0)
		return false;
	pDevice->eCmdQueue[pDevice->uCmdEnqueueIdx].eCmd = eCommand;
	pDevice->eCmdQueue[pDevice->uCmdEnqueueIdx].bForceSCAN = true;

	ADD_ONE_WITH_WRAP_AROUND(pDevice->uCmdEnqueueIdx, CMD_Q_SIZE);
	pDevice->cbFreeCmdQueue--;

	if (pDevice->bCmdRunning == false)
		s_bCommandComplete(pDevice);

	return true;

}

void vResetCommandTimer(struct vnt_private *pDevice)
{
	pDevice->cbFreeCmdQueue = CMD_Q_SIZE;
	pDevice->uCmdDequeueIdx = 0;
	pDevice->uCmdEnqueueIdx = 0;
	pDevice->eCommandState = WLAN_CMD_IDLE;
	pDevice->bCmdRunning = false;
	pDevice->bCmdClear = false;
}
