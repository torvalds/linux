// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
 *
 * File: dpc.c
 *
 * Purpose: handle dpc rx functions
 *
 * Author: Lyndon Chen
 *
 * Date: May 20, 2003
 *
 * Functions:
 *
 * Revision History:
 *
 */

#include "dpc.h"
#include "device.h"
#include "mac.h"
#include "baseband.h"
#include "rf.h"

int vnt_rx_data(struct vnt_private *priv, struct vnt_rcb *ptr_rcb,
		unsigned long bytes_received)
{
	struct ieee80211_hw *hw = priv->hw;
	struct ieee80211_supported_band *sband;
	struct sk_buff *skb;
	struct ieee80211_rx_status *rx_status;
	struct vnt_rx_header *head;
	struct vnt_rx_tail *tail;
	u32 frame_size;
	int ii;
	u16 rx_bitrate, pay_load_with_padding;
	u8 rate_idx = 0;
	long rx_dbm;

	skb = ptr_rcb->skb;
	rx_status = IEEE80211_SKB_RXCB(skb);

	/* [31:16]RcvByteCount ( not include 4-byte Status ) */
	head = (struct vnt_rx_header *)skb->data;
	frame_size = head->wbk_status >> 16;
	frame_size += 4;

	if (bytes_received != frame_size) {
		dev_dbg(&priv->usb->dev, "------- WRONG Length 1\n");
		return false;
	}

	if ((bytes_received > 2372) || (bytes_received <= 40)) {
		/* Frame Size error drop this packet.*/
		dev_dbg(&priv->usb->dev, "------ WRONG Length 2\n");
		return false;
	}

	/* real Frame Size = USBframe_size -4WbkStatus - 4RxStatus */
	/* -8TSF - 4RSR - 4SQ3 - ?Padding */

	/* if SQ3 the range is 24~27, if no SQ3 the range is 20~23 */

	/*Fix hardware bug => PLCP_Length error */
	if (((bytes_received - head->pay_load_len) > 27) ||
	    ((bytes_received - head->pay_load_len) < 24) ||
	    (bytes_received < head->pay_load_len)) {
		dev_dbg(&priv->usb->dev, "Wrong PLCP Length %x\n",
			head->pay_load_len);
		return false;
	}

	sband = hw->wiphy->bands[hw->conf.chandef.chan->band];
	rx_bitrate = head->rx_rate * 5; /* rx_rate * 5 */

	for (ii = 0; ii < sband->n_bitrates; ii++) {
		if (sband->bitrates[ii].bitrate == rx_bitrate) {
			rate_idx = ii;
				break;
		}
	}

	if (ii == sband->n_bitrates) {
		dev_dbg(&priv->usb->dev, "Wrong Rx Bit Rate %d\n", rx_bitrate);
		return false;
	}

	pay_load_with_padding = ((head->pay_load_len / 4) +
		((head->pay_load_len % 4) ? 1 : 0)) * 4;

	tail = (struct vnt_rx_tail *)(skb->data +
				      sizeof(*head) + pay_load_with_padding);
	priv->tsf_time = le64_to_cpu(tail->tsf_time);

	if (tail->rsr & (RSR_IVLDTYP | RSR_IVLDLEN))
		return false;

	vnt_rf_rssi_to_dbm(priv, tail->rssi, &rx_dbm);

	priv->bb_pre_ed_rssi = (u8)rx_dbm + 1;
	priv->current_rssi = priv->bb_pre_ed_rssi;

	skb_pull(skb, sizeof(*head));
	skb_trim(skb, head->pay_load_len);

	rx_status->mactime = priv->tsf_time;
	rx_status->band = hw->conf.chandef.chan->band;
	rx_status->signal = rx_dbm;
	rx_status->flag = 0;
	rx_status->freq = hw->conf.chandef.chan->center_freq;

	if (!(tail->rsr & RSR_CRCOK))
		rx_status->flag |= RX_FLAG_FAILED_FCS_CRC;

	rx_status->rate_idx = rate_idx;

	if (tail->new_rsr & NEWRSR_DECRYPTOK)
		rx_status->flag |= RX_FLAG_DECRYPTED;

	ieee80211_rx_irqsafe(priv->hw, skb);

	return true;
}
