// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
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

int vnt_int_start_interrupt(struct vnt_private *priv)
{
	int ret = 0;
	unsigned long flags;

	dev_dbg(&priv->usb->dev, "---->Interrupt Polling Thread\n");

	spin_lock_irqsave(&priv->lock, flags);

	ret = vnt_start_interrupt_urb(priv);

	spin_unlock_irqrestore(&priv->lock, flags);

	return ret;
}

static int vnt_int_report_rate(struct vnt_private *priv, u8 pkt_no, u8 tsr)
{
	struct vnt_usb_send_context *context;
	struct ieee80211_tx_info *info;
	u8 tx_retry = (tsr & 0xf0) >> 4;
	s8 idx;

	if (pkt_no >= priv->num_tx_context)
		return -EINVAL;

	context = priv->tx_context[pkt_no];

	if (!context->skb)
		return -EINVAL;

	info = IEEE80211_SKB_CB(context->skb);
	idx = info->control.rates[0].idx;

	ieee80211_tx_info_clear_status(info);

	info->status.rates[0].count = tx_retry;

	if (!(tsr & TSR_TMO)) {
		info->status.rates[0].idx = idx;

		if (!(info->flags & IEEE80211_TX_CTL_NO_ACK))
			info->flags |= IEEE80211_TX_STAT_ACK;
	}

	ieee80211_tx_status_irqsafe(priv->hw, context->skb);

	context->in_use = false;

	return 0;
}

void vnt_int_process_data(struct vnt_private *priv)
{
	struct vnt_interrupt_data *int_data;
	struct ieee80211_low_level_stats *low_stats = &priv->low_stats;

	dev_dbg(&priv->usb->dev, "---->s_nsInterruptProcessData\n");

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
			vnt_schedule_command(priv, WLAN_CMD_BECON_SEND);

		if (int_data->isr0 & ISR_TBTT &&
		    priv->hw->conf.flags & IEEE80211_CONF_PS) {
			if (!priv->wake_up_count)
				priv->wake_up_count =
					priv->hw->conf.listen_interval;

			--priv->wake_up_count;

			/* Turn on wake up to listen next beacon */
			if (priv->wake_up_count == 1)
				vnt_schedule_command(priv,
						     WLAN_CMD_TBTT_WAKEUP);
		}
		priv->current_tsf = le64_to_cpu(int_data->tsf);

		low_stats->dot11RTSSuccessCount += int_data->rts_success;
		low_stats->dot11RTSFailureCount += int_data->rts_fail;
		low_stats->dot11ACKFailureCount += int_data->ack_fail;
		low_stats->dot11FCSErrorCount += int_data->fcs_err;
	}

	priv->int_buf.in_use = false;
}
