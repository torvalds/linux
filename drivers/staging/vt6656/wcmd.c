// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
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
 *	vnt_cmd_complete - Command Complete function
 *	vnt_schedule_command - Push Command and wait Command Scheduler to do
 *	vnt_cmd_timer_wait- Call back timer
 *
 * Revision History:
 *
 */

#include "device.h"
#include "mac.h"
#include "wcmd.h"
#include "power.h"
#include "usbpipe.h"
#include "rxtx.h"
#include "rf.h"

static void vnt_cmd_timer_wait(struct vnt_private *priv, unsigned long msecs)
{
	schedule_delayed_work(&priv->run_command_work, msecs_to_jiffies(msecs));
}

static int vnt_cmd_complete(struct vnt_private *priv)
{
	priv->command_state = WLAN_CMD_IDLE;
	if (priv->free_cmd_queue == CMD_Q_SIZE) {
		/* Command Queue Empty */
		priv->cmd_running = false;
		return true;
	}

	priv->command = priv->cmd_queue[priv->cmd_dequeue_idx];

	ADD_ONE_WITH_WRAP_AROUND(priv->cmd_dequeue_idx, CMD_Q_SIZE);
	priv->free_cmd_queue++;
	priv->cmd_running = true;

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

	default:
		break;
	}

	vnt_cmd_timer_wait(priv, 0);

	return true;
}

void vnt_run_command(struct work_struct *work)
{
	struct vnt_private *priv =
		container_of(work, struct vnt_private, run_command_work.work);

	if (test_bit(DEVICE_FLAGS_DISCONNECTED, &priv->flags))
		return;

	if (!priv->cmd_running)
		return;

	switch (priv->command_state) {
	case WLAN_CMD_INIT_MAC80211_START:
		if (priv->mac_hw)
			break;

		dev_info(&priv->usb->dev, "Starting mac80211\n");

		if (vnt_init(priv)) {
			/* If fail all ends TODO retry */
			dev_err(&priv->usb->dev, "failed to start\n");
			usb_set_intfdata(priv->intf, NULL);
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

		vnt_rf_setpower(priv, priv->current_rate,
				priv->hw->conf.chandef.chan->hw_value);

		break;

	case WLAN_CMD_CHANGE_ANTENNA_START:
		dev_dbg(&priv->usb->dev, "Change from Antenna%d to",
			priv->rx_antenna_sel);

		if (priv->rx_antenna_sel == 0) {
			priv->rx_antenna_sel = 1;
			if (priv->tx_rx_ant_inv)
				vnt_set_antenna_mode(priv, ANT_RXA);
			else
				vnt_set_antenna_mode(priv, ANT_RXB);
		} else {
			priv->rx_antenna_sel = 0;
			if (priv->tx_rx_ant_inv)
				vnt_set_antenna_mode(priv, ANT_RXB);
			else
				vnt_set_antenna_mode(priv, ANT_RXA);
		}
		break;

	default:
		break;
	}

	vnt_cmd_complete(priv);
}

int vnt_schedule_command(struct vnt_private *priv, enum vnt_cmd command)
{
	if (priv->free_cmd_queue == 0)
		return false;

	priv->cmd_queue[priv->cmd_enqueue_idx] = command;

	ADD_ONE_WITH_WRAP_AROUND(priv->cmd_enqueue_idx, CMD_Q_SIZE);
	priv->free_cmd_queue--;

	if (!priv->cmd_running)
		vnt_cmd_complete(priv);

	return true;
}

void vnt_reset_command_timer(struct vnt_private *priv)
{
	priv->free_cmd_queue = CMD_Q_SIZE;
	priv->cmd_dequeue_idx = 0;
	priv->cmd_enqueue_idx = 0;
	priv->command_state = WLAN_CMD_IDLE;
	priv->cmd_running = false;
}
