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
#include "mac.h"
#include "power.h"
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
	unsigned long flags;
	int ntStatus;

	DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"---->Interrupt Polling Thread\n");

	spin_lock_irqsave(&pDevice->lock, flags);

	ntStatus = PIPEnsInterruptRead(pDevice);

	spin_unlock_irqrestore(&pDevice->lock, flags);
}

void INTnsProcessData(struct vnt_private *priv)
{
	struct vnt_interrupt_data *int_data;
	struct net_device_stats *stats = &priv->stats;

	DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"---->s_nsInterruptProcessData\n");

	int_data = (struct vnt_interrupt_data *)priv->int_buf.data_buf;

	if (int_data->tsr0 & TSR_VALID) {
		if (int_data->tsr0 & (TSR_TMO | TSR_RETRYTMO))
			priv->wstats.discard.retries++;
		else
			stats->tx_packets++;
	}

	if (int_data->tsr1 & TSR_VALID) {
		if (int_data->tsr1 & (TSR_TMO | TSR_RETRYTMO))
			priv->wstats.discard.retries++;
		else
			stats->tx_packets++;
	}

	if (int_data->tsr2 & TSR_VALID) {
		if (int_data->tsr2 & (TSR_TMO | TSR_RETRYTMO))
			priv->wstats.discard.retries++;
		else
			stats->tx_packets++;
	}

	if (int_data->tsr3 & TSR_VALID) {
		if (int_data->tsr3 & (TSR_TMO | TSR_RETRYTMO))
			priv->wstats.discard.retries++;
		else
			stats->tx_packets++;
	}

	if (int_data->isr0 != 0) {
		if (int_data->isr0 & ISR_BNTX &&
				priv->op_mode == NL80211_IFTYPE_AP)
			bScheduleCommand(priv, WLAN_CMD_BECON_SEND, NULL);

		if (int_data->isr0 & ISR_TBTT) {
			if (priv->hw->conf.flags & IEEE80211_CONF_PS)
				bScheduleCommand((void *) priv,
						WLAN_CMD_TBTT_WAKEUP,
						NULL);
#if 0 /* TODO channel switch */
			if (priv->bChannelSwitch) {
				priv->byChannelSwitchCount--;
				if (priv->byChannelSwitchCount == 0)
					bScheduleCommand((void *) priv,
							WLAN_CMD_11H_CHSW,
							NULL);
			}
#endif
		}
		priv->qwCurrTSF = le64_to_cpu(int_data->tsf);
	}

	if (int_data->isr1 != 0)
		if (int_data->isr1 & ISR_GPIO3)
			bScheduleCommand((void *) priv,
					WLAN_CMD_RADIO,
					NULL);

	priv->int_buf.in_use = false;

	stats->tx_errors = priv->wstats.discard.retries;
	stats->tx_dropped = priv->wstats.discard.retries;
}
