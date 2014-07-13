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

static int s_bCommandComplete(struct vnt_private *);

static void vCommandTimerWait(struct vnt_private *priv, unsigned long msecs)
{
	schedule_delayed_work(&priv->run_command_work, msecs_to_jiffies(msecs));
}

void vRunCommand(struct work_struct *work)
{
	struct vnt_private *priv =
		container_of(work, struct vnt_private, run_command_work.work);

	if (priv->Flags & fMP_DISCONNECTED)
		return;

	if (priv->bCmdRunning != true)
		return;

	switch (priv->command_state) {
	case WLAN_CMD_INIT_MAC80211_START:
		if (priv->mac_hw)
			break;

		dev_info(&priv->usb->dev, "Starting mac80211\n");

		if (vnt_init(priv)) {
			/* If fail all ends TODO retry */
			dev_err(&priv->usb->dev, "failed to start\n");
			ieee80211_free_hw(priv->hw);
			return;
		}

		break;

	case WLAN_CMD_TBTT_WAKEUP_START:
		vnt_next_tbtt_wakeup(priv);
		break;

	case WLAN_CMD_BECON_SEND_START:
		if (!priv->vif)
			break;

		vnt_beacon_make(priv, priv->vif);

		vnt_mac_reg_bits_on(priv, MAC_REG_TCR, TCR_AUTOBCNTX);

		break;

	case WLAN_CMD_SETPOWER_START:

		vnt_rf_setpower(priv, priv->wCurrentRate,
				priv->hw->conf.chandef.chan->hw_value);

		break;

	case WLAN_CMD_CHANGE_ANTENNA_START:
		dev_dbg(&priv->usb->dev, "Change from Antenna%d to",
							priv->dwRxAntennaSel);

		if (priv->dwRxAntennaSel == 0) {
			priv->dwRxAntennaSel = 1;
			if (priv->bTxRxAntInv == true)
				BBvSetAntennaMode(priv, ANT_RXA);
			else
				BBvSetAntennaMode(priv, ANT_RXB);
		} else {
			priv->dwRxAntennaSel = 0;
			if (priv->bTxRxAntInv == true)
				BBvSetAntennaMode(priv, ANT_RXB);
			else
				BBvSetAntennaMode(priv, ANT_RXA);
		}
		break;

	case WLAN_CMD_11H_CHSW_START:
		vnt_set_channel(priv, priv->hw->conf.chandef.chan->hw_value);
		break;

	default:
		break;
	} //switch

	s_bCommandComplete(priv);

	return;
}

static int s_bCommandComplete(struct vnt_private *priv)
{

	priv->command_state = WLAN_CMD_IDLE;
	if (priv->cbFreeCmdQueue == CMD_Q_SIZE) {
		/* Command Queue Empty */
		priv->bCmdRunning = false;
		return true;
	}

	priv->command = priv->cmd_queue[priv->uCmdDequeueIdx];

	ADD_ONE_WITH_WRAP_AROUND(priv->uCmdDequeueIdx, CMD_Q_SIZE);
	priv->cbFreeCmdQueue++;
	priv->bCmdRunning = true;

	switch (priv->command) {
	case WLAN_CMD_INIT_MAC80211:
		priv->command_state = WLAN_CMD_INIT_MAC80211_START;
		break;

	case WLAN_CMD_TBTT_WAKEUP:
		priv->command_state = WLAN_CMD_TBTT_WAKEUP_START;
		break;

	case WLAN_CMD_BECON_SEND:
		priv->command_state = WLAN_CMD_BECON_SEND_START;
		break;

	case WLAN_CMD_SETPOWER:
		priv->command_state = WLAN_CMD_SETPOWER_START;
		break;

	case WLAN_CMD_CHANGE_ANTENNA:
		priv->command_state = WLAN_CMD_CHANGE_ANTENNA_START;
		break;

	case WLAN_CMD_11H_CHSW:
		priv->command_state = WLAN_CMD_11H_CHSW_START;
		break;

	default:
		break;
	}

	vCommandTimerWait(priv, 0);

	return true;
}

int bScheduleCommand(struct vnt_private *priv, enum vnt_cmd command, u8 *item0)
{

	if (priv->cbFreeCmdQueue == 0)
		return false;

	priv->cmd_queue[priv->uCmdEnqueueIdx] = command;

	ADD_ONE_WITH_WRAP_AROUND(priv->uCmdEnqueueIdx, CMD_Q_SIZE);
	priv->cbFreeCmdQueue--;

	if (priv->bCmdRunning == false)
		s_bCommandComplete(priv);

	return true;

}

void vResetCommandTimer(struct vnt_private *priv)
{
	priv->cbFreeCmdQueue = CMD_Q_SIZE;
	priv->uCmdDequeueIdx = 0;
	priv->uCmdEnqueueIdx = 0;
	priv->command_state = WLAN_CMD_IDLE;
	priv->bCmdRunning = false;
}
