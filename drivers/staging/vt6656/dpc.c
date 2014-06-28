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
 * File: dpc.c
 *
 * Purpose: handle dpc rx functions
 *
 * Author: Lyndon Chen
 *
 * Date: May 20, 2003
 *
 * Functions:
 *      device_receive_frame - Rcv 802.11 frame function
 *      s_bHandleRxEncryption- Rcv decrypted data via on-fly
 *      s_byGetRateIdx- get rate index
 *      s_vGetDASA- get data offset
 *      s_vProcessRxMACHeader- Rcv 802.11 and translate to 802.3
 *
 * Revision History:
 *
 */

#include "dpc.h"
#include "device.h"
#include "rxtx.h"
#include "card.h"
#include "mac.h"
#include "baseband.h"
#include "rf.h"
#include "usbpipe.h"

int vnt_rx_data(struct vnt_private *priv, struct vnt_rcb *ptr_rcb,
	unsigned long bytes_received)
{
	struct ieee80211_hw *hw = priv->hw;
	struct ieee80211_supported_band *sband;
	struct sk_buff *skb;
	struct ieee80211_rx_status rx_status = { 0 };
	struct ieee80211_hdr *hdr;
	__le16 fc;
	u8 *rsr, *new_rsr, *rssi, *frame;
	__le64 *tsf_time;
	u32 frame_size;
	int ii, r;
	u8 *rx_sts, *rx_rate, *sq, *sq_3;
	u32 wbk_status;
	u8 *skb_data;
	u16 *pay_load_len;
	u16 pay_load_with_padding;
	u8 rate_idx = 0;
	u8 rate[MAX_RATE] = {2, 4, 11, 22, 12, 18, 24, 36, 48, 72, 96, 108};
	long rx_dbm;

	skb = ptr_rcb->skb;

	/* [31:16]RcvByteCount ( not include 4-byte Status ) */
	wbk_status = *((u32 *)(skb->data));
	frame_size = wbk_status >> 16;
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

	skb_data = (u8 *)skb->data;

	rx_sts = skb_data+4;
	rx_rate = skb_data+5;

	/* real Frame Size = USBframe_size -4WbkStatus - 4RxStatus */
	/* -8TSF - 4RSR - 4SQ3 - ?Padding */

	/* if SQ3 the range is 24~27, if no SQ3 the range is 20~23 */

	pay_load_len = (u16 *) (skb_data + 6);

	/*Fix hardware bug => PLCP_Length error */
	if (((bytes_received - (*pay_load_len)) > 27) ||
		((bytes_received - (*pay_load_len)) < 24) ||
			(bytes_received < (*pay_load_len))) {
		dev_dbg(&priv->usb->dev, "Wrong PLCP Length %x\n",
							*pay_load_len);
		return false;
	}

	sband = hw->wiphy->bands[hw->conf.chandef.chan->band];

	for (r = RATE_1M; r < MAX_RATE; r++) {
		if (*rx_rate == rate[r])
			break;
	}

	priv->rx_rate = r;

	for (ii = 0; ii < sband->n_bitrates; ii++) {
		if (sband->bitrates[ii].hw_value == r) {
			rate_idx = ii;
				break;
		}
	}

	if (ii == sband->n_bitrates) {
		dev_dbg(&priv->usb->dev, "Wrong RxRate %x\n", *rx_rate);
		return false;
	}

	pay_load_with_padding = ((*pay_load_len / 4) +
		((*pay_load_len % 4) ? 1 : 0)) * 4;

	tsf_time = (__le64 *)(skb_data + 8 + pay_load_with_padding);

	priv->tsf_time = le64_to_cpu(*tsf_time);

	if (priv->byBBType == BB_TYPE_11G) {
		sq_3 = skb_data + 8 + pay_load_with_padding + 12;
		sq = sq_3;
	} else {
		sq = skb_data + 8 + pay_load_with_padding + 8;
		sq_3 = sq;
	}

	new_rsr = skb_data + 8 + pay_load_with_padding + 9;
	rssi = skb_data + 8 + pay_load_with_padding + 10;
	rsr = skb_data + 8 + pay_load_with_padding + 11;

	frame_size = *pay_load_len;

	vnt_rf_rssi_to_dbm(priv, *rssi, &rx_dbm);

	priv->byBBPreEDRSSI = (u8)rx_dbm + 1;
	priv->uCurrRSSI = priv->byBBPreEDRSSI;

	frame = skb_data + 8;

	skb_pull(skb, 8);
	skb_trim(skb, frame_size);

	rx_status.mactime = priv->tsf_time;
	rx_status.band = hw->conf.chandef.chan->band;
	rx_status.signal = rx_dbm;
	rx_status.flag = 0;
	rx_status.freq = hw->conf.chandef.chan->center_freq;

	hdr = (struct ieee80211_hdr *)(skb->data);
	fc = hdr->frame_control;

	rx_status.rate_idx = rate_idx;

	if (ieee80211_has_protected(fc)) {
		if (priv->byLocalID > REV_ID_VT3253_A1)
			rx_status.flag = RX_FLAG_DECRYPTED;
	}

	memcpy(IEEE80211_SKB_RXCB(skb), &rx_status, sizeof(rx_status));

	ieee80211_rx_irqsafe(priv->hw, skb);

	return true;
}
