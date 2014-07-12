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

static const u8 fallback_rate0[5][5] = {
	{RATE_18M, RATE_18M, RATE_12M, RATE_12M, RATE_12M},
	{RATE_24M, RATE_24M, RATE_18M, RATE_12M, RATE_12M},
	{RATE_36M, RATE_36M, RATE_24M, RATE_18M, RATE_18M},
	{RATE_48M, RATE_48M, RATE_36M, RATE_24M, RATE_24M},
	{RATE_54M, RATE_54M, RATE_48M, RATE_36M, RATE_36M}
};

static const u8 fallback_rate1[5][5] = {
	{RATE_18M, RATE_18M, RATE_12M, RATE_6M, RATE_6M},
	{RATE_24M, RATE_24M, RATE_18M, RATE_6M, RATE_6M},
	{RATE_36M, RATE_36M, RATE_24M, RATE_12M, RATE_12M},
	{RATE_48M, RATE_48M, RATE_24M, RATE_12M, RATE_12M},
	{RATE_54M, RATE_54M, RATE_36M, RATE_18M, RATE_18M}
};

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

static int vnt_int_report_rate(struct vnt_private *priv, u8 pkt_no, u8 tsr)
{
	struct vnt_usb_send_context *context;
	struct ieee80211_tx_info *info;
	struct ieee80211_rate *rate;
	u8 tx_retry = (tsr & 0xf0) >> 4;
	s8 idx;

	if (pkt_no >= priv->cbTD)
		return -EINVAL;

	context = priv->apTD[pkt_no];

	if (!context->skb)
		return -EINVAL;

	info = IEEE80211_SKB_CB(context->skb);
	idx = info->control.rates[0].idx;

	if (context->fb_option && !(tsr & (TSR_TMO | TSR_RETRYTMO))) {
		u8 tx_rate;
		u8 retry = tx_retry;

		rate = ieee80211_get_tx_rate(priv->hw, info);
		tx_rate = rate->hw_value - RATE_18M;

		if (retry > 4)
			retry = 4;

		if (context->fb_option == AUTO_FB_0)
			tx_rate = fallback_rate0[tx_rate][retry];
		else if (context->fb_option == AUTO_FB_1)
			tx_rate = fallback_rate1[tx_rate][retry];

		if (info->band == IEEE80211_BAND_5GHZ)
			idx = tx_rate - RATE_6M;
		else
			idx = tx_rate;
	}

	ieee80211_tx_info_clear_status(info);

	info->status.rates[0].count = tx_retry;

	if (!(tsr & (TSR_TMO | TSR_RETRYTMO))) {
		info->status.rates[0].idx = idx;
		info->flags |= IEEE80211_TX_STAT_ACK;
	}

	ieee80211_tx_status_irqsafe(priv->hw, context->skb);

	context->in_use = false;

	return 0;
}

void INTnsProcessData(struct vnt_private *priv)
{
	struct vnt_interrupt_data *int_data;
	struct net_device_stats *stats = &priv->stats;
	struct ieee80211_low_level_stats *low_stats = &priv->low_stats;

	DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"---->s_nsInterruptProcessData\n");

	int_data = (struct vnt_interrupt_data *)priv->int_buf.data_buf;

	if (int_data->tsr0 & TSR_VALID)
		vnt_int_report_rate(priv, int_data->pkt0, int_data->tsr0);

	if (int_data->tsr1 & TSR_VALID)
		vnt_int_report_rate(priv, int_data->pkt1, int_data->tsr1);

	if (int_data->tsr2 & TSR_VALID)
		vnt_int_report_rate(priv, int_data->pkt2, int_data->tsr2);

	if (int_data->tsr3 & TSR_VALID)
		vnt_int_report_rate(priv, int_data->pkt3, int_data->tsr3);

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

		low_stats->dot11RTSSuccessCount += int_data->rts_success;
		low_stats->dot11RTSFailureCount += int_data->rts_fail;
		low_stats->dot11ACKFailureCount += int_data->ack_fail;
		low_stats->dot11FCSErrorCount += int_data->fcs_err;
	}

	priv->int_buf.in_use = false;

	stats->tx_errors = priv->wstats.discard.retries;
	stats->tx_dropped = priv->wstats.discard.retries;
}
