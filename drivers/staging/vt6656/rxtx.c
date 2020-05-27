// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
 *
 * File: rxtx.c
 *
 * Purpose: handle WMAC/802.3/802.11 rx & tx functions
 *
 * Author: Lyndon Chen
 *
 * Date: May 20, 2003
 *
 * Functions:
 *      vnt_generate_tx_parameter - Generate tx dma required parameter.
 *      vnt_get_rsvtime- get frame reserved time
 *      vnt_fill_cts_head- fulfill CTS ctl header
 *
 * Revision History:
 *
 */

#include <linux/etherdevice.h>
#include "device.h"
#include "rxtx.h"
#include "card.h"
#include "mac.h"
#include "rf.h"
#include "usbpipe.h"

static const u16 vnt_time_stampoff[2][MAX_RATE] = {
	/* Long Preamble */
	{384, 288, 226, 209, 54, 43, 37, 31, 28, 25, 24, 23},

	/* Short Preamble */
	{384, 192, 130, 113, 54, 43, 37, 31, 28, 25, 24, 23},
};

#define DATADUR_B       10
#define DATADUR_A       11

static const u8 vnt_phy_signal[] = {
	0x00,	/* RATE_1M  */
	0x01,	/* RATE_2M  */
	0x02,	/* RATE_5M  */
	0x03,	/* RATE_11M */
	0x8b,	/* RATE_6M  */
	0x8f,	/* RATE_9M  */
	0x8a,	/* RATE_12M */
	0x8e,	/* RATE_18M */
	0x89,	/* RATE_24M */
	0x8d,	/* RATE_36M */
	0x88,	/* RATE_48M */
	0x8c	/* RATE_54M */
};

static struct vnt_usb_send_context
	*vnt_get_free_context(struct vnt_private *priv)
{
	struct vnt_usb_send_context *context = NULL;
	int ii;

	dev_dbg(&priv->usb->dev, "%s\n", __func__);

	for (ii = 0; ii < priv->num_tx_context; ii++) {
		if (!priv->tx_context[ii])
			return NULL;

		context = priv->tx_context[ii];
		if (!context->in_use) {
			context->in_use = true;
			return context;
		}
	}

	if (ii == priv->num_tx_context) {
		dev_dbg(&priv->usb->dev, "%s No Free Tx Context\n", __func__);

		ieee80211_stop_queues(priv->hw);
	}

	return NULL;
}

/* Get Length, Service, and Signal fields of Phy for Tx */
static void vnt_get_phy_field(struct vnt_private *priv, u32 frame_length,
			      u16 tx_rate, u8 pkt_type,
			      struct vnt_phy_field *phy)
{
	u32 bit_count;
	u32 count = 0;
	u32 tmp;
	int ext_bit;
	int i;
	u8 mask = 0;
	u8 preamble_type = priv->preamble_type;

	bit_count = frame_length * 8;
	ext_bit = false;

	switch (tx_rate) {
	case RATE_1M:
		count = bit_count;
		break;
	case RATE_2M:
		count = bit_count / 2;
		break;
	case RATE_5M:
		count = DIV_ROUND_UP(bit_count * 10, 55);
		break;
	case RATE_11M:
		count = bit_count / 11;
		tmp = count * 11;

		if (tmp != bit_count) {
			count++;

			if ((bit_count - tmp) <= 3)
				ext_bit = true;
		}

		break;
	}

	if (tx_rate > RATE_11M) {
		if (pkt_type == PK_TYPE_11A)
			mask = BIT(4);
	} else if (tx_rate > RATE_1M) {
		if (preamble_type == PREAMBLE_SHORT)
			mask = BIT(3);
	}

	i = tx_rate > RATE_54M ? RATE_54M : tx_rate;
	phy->signal = vnt_phy_signal[i] | mask;
	phy->service = 0x00;

	if (pkt_type == PK_TYPE_11B) {
		if (ext_bit)
			phy->service |= 0x80;
		phy->len = cpu_to_le16((u16)count);
	} else {
		phy->len = cpu_to_le16((u16)frame_length);
	}
}

static __le16 vnt_time_stamp_off(struct vnt_private *priv, u16 rate)
{
	return cpu_to_le16(vnt_time_stampoff[priv->preamble_type % 2]
							[rate % MAX_RATE]);
}

static __le16 vnt_rxtx_rsvtime_le16(struct vnt_usb_send_context *context)
{
	struct vnt_private *priv = context->priv;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(context->skb);
	struct ieee80211_rate *rate = ieee80211_get_tx_rate(priv->hw, info);

	return ieee80211_generic_frame_duration(priv->hw,
						 info->control.vif, info->band,
						 context->frame_len,
						 rate);
}

static __le16 vnt_get_rts_duration(struct vnt_usb_send_context *context)
{
	struct vnt_private *priv = context->priv;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(context->skb);

	return ieee80211_rts_duration(priv->hw, priv->vif,
				      context->frame_len, info);
}

static __le16 vnt_get_cts_duration(struct vnt_usb_send_context *context)
{
	struct vnt_private *priv = context->priv;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(context->skb);

	return ieee80211_ctstoself_duration(priv->hw, priv->vif,
					    context->frame_len, info);
}

static void vnt_rxtx_datahead_g(struct vnt_usb_send_context *tx_context,
				struct vnt_tx_datahead_g *buf)
{
	struct vnt_private *priv = tx_context->priv;
	struct ieee80211_hdr *hdr =
				(struct ieee80211_hdr *)tx_context->skb->data;
	u32 frame_len = tx_context->frame_len;
	u16 rate = tx_context->tx_rate;

	/* Get SignalField,ServiceField,Length */
	vnt_get_phy_field(priv, frame_len, rate, tx_context->pkt_type, &buf->a);
	vnt_get_phy_field(priv, frame_len, priv->top_cck_basic_rate,
			  PK_TYPE_11B, &buf->b);

	/* Get Duration and TimeStamp */
	buf->duration_a = hdr->duration_id;
	buf->duration_b = hdr->duration_id;
	buf->time_stamp_off_a = vnt_time_stamp_off(priv, rate);
	buf->time_stamp_off_b = vnt_time_stamp_off(priv,
						   priv->top_cck_basic_rate);
}

static void vnt_rxtx_datahead_ab(struct vnt_usb_send_context *tx_context,
				 struct vnt_tx_datahead_ab *buf)
{
	struct vnt_private *priv = tx_context->priv;
	struct ieee80211_hdr *hdr =
				(struct ieee80211_hdr *)tx_context->skb->data;
	u32 frame_len = tx_context->frame_len;
	u16 rate = tx_context->tx_rate;

	/* Get SignalField,ServiceField,Length */
	vnt_get_phy_field(priv, frame_len, rate,
			  tx_context->pkt_type, &buf->ab);

	/* Get Duration and TimeStampOff */
	buf->duration = hdr->duration_id;
	buf->time_stamp_off = vnt_time_stamp_off(priv, rate);
}

static void vnt_fill_ieee80211_rts(struct vnt_usb_send_context *tx_context,
				   struct ieee80211_rts *rts, __le16 duration)
{
	struct ieee80211_hdr *hdr =
				(struct ieee80211_hdr *)tx_context->skb->data;

	rts->duration = duration;
	rts->frame_control =
		cpu_to_le16(IEEE80211_FTYPE_CTL | IEEE80211_STYPE_RTS);

	ether_addr_copy(rts->ra, hdr->addr1);
	ether_addr_copy(rts->ta, hdr->addr2);
}

static void vnt_rxtx_rts_g_head(struct vnt_usb_send_context *tx_context,
				struct vnt_rts_g *buf)
{
	struct vnt_private *priv = tx_context->priv;
	u16 rts_frame_len = 20;

	vnt_get_phy_field(priv, rts_frame_len, priv->top_cck_basic_rate,
			  PK_TYPE_11B, &buf->b);
	vnt_get_phy_field(priv, rts_frame_len, priv->top_ofdm_basic_rate,
			  tx_context->pkt_type, &buf->a);

	buf->duration_bb = vnt_get_rts_duration(tx_context);
	buf->duration_aa = buf->duration_bb;
	buf->duration_ba = buf->duration_bb;

	vnt_fill_ieee80211_rts(tx_context, &buf->data, buf->duration_aa);

	vnt_rxtx_datahead_g(tx_context, &buf->data_head);
}

static void vnt_rxtx_rts_ab_head(struct vnt_usb_send_context *tx_context,
				 struct vnt_rts_ab *buf)
{
	struct vnt_private *priv = tx_context->priv;
	u16 rts_frame_len = 20;

	vnt_get_phy_field(priv, rts_frame_len, priv->top_ofdm_basic_rate,
			  tx_context->pkt_type, &buf->ab);

	buf->duration = vnt_get_rts_duration(tx_context);

	vnt_fill_ieee80211_rts(tx_context, &buf->data, buf->duration);

	vnt_rxtx_datahead_ab(tx_context, &buf->data_head);
}

static void vnt_fill_cts_head(struct vnt_usb_send_context *tx_context,
			      union vnt_tx_data_head *head)
{
	struct vnt_private *priv = tx_context->priv;
	struct vnt_cts *buf = &head->cts_g;
	u32 cts_frame_len = 14;

	/* Get SignalField,ServiceField,Length */
	vnt_get_phy_field(priv, cts_frame_len, priv->top_cck_basic_rate,
			  PK_TYPE_11B, &buf->b);
	/* Get CTSDuration_ba */
	buf->duration_ba = vnt_get_cts_duration(tx_context);
	/*Get CTS Frame body*/
	buf->data.duration = buf->duration_ba;
	buf->data.frame_control =
		cpu_to_le16(IEEE80211_FTYPE_CTL | IEEE80211_STYPE_CTS);

	ether_addr_copy(buf->data.ra, priv->current_net_addr);

	vnt_rxtx_datahead_g(tx_context, &buf->data_head);
}

/* returns true if mic_hdr is needed */
static bool vnt_fill_txkey(struct vnt_tx_buffer *tx_buffer, struct sk_buff *skb)
{
	struct vnt_tx_fifo_head *fifo = &tx_buffer->fifo_head;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_key_conf *tx_key = info->control.hw_key;
	struct vnt_mic_hdr *mic_hdr;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	u64 pn64;
	u16 payload_len = skb->len;
	u8 *iv = ((u8 *)hdr + ieee80211_get_hdrlen_from_skb(skb));

	/* strip header and icv len from payload */
	payload_len -= ieee80211_get_hdrlen_from_skb(skb);
	payload_len -= tx_key->icv_len;

	switch (tx_key->cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
	case WLAN_CIPHER_SUITE_WEP104:
		memcpy(fifo->tx_key, iv, 3);
		memcpy(fifo->tx_key + 3, tx_key->key, tx_key->keylen);

		if (tx_key->keylen == WLAN_KEY_LEN_WEP40) {
			memcpy(fifo->tx_key + 8, iv, 3);
			memcpy(fifo->tx_key + 11,
			       tx_key->key, WLAN_KEY_LEN_WEP40);
		}

		fifo->frag_ctl |= cpu_to_le16(FRAGCTL_LEGACY);
		break;
	case WLAN_CIPHER_SUITE_TKIP:
		ieee80211_get_tkip_p2k(tx_key, skb, fifo->tx_key);

		fifo->frag_ctl |= cpu_to_le16(FRAGCTL_TKIP);
		break;
	case WLAN_CIPHER_SUITE_CCMP:
		if (info->control.use_cts_prot) {
			if (info->control.use_rts)
				mic_hdr = &tx_buffer->tx_head.tx_rts.tx.mic.hdr;
			else
				mic_hdr = &tx_buffer->tx_head.tx_cts.tx.mic.hdr;
		} else {
			mic_hdr = &tx_buffer->tx_head.tx_ab.tx.mic.hdr;
		}

		mic_hdr->id = 0x59;
		mic_hdr->payload_len = cpu_to_be16(payload_len);
		ether_addr_copy(mic_hdr->mic_addr2, hdr->addr2);

		pn64 = atomic64_read(&tx_key->tx_pn);
		mic_hdr->ccmp_pn[5] = pn64;
		mic_hdr->ccmp_pn[4] = pn64 >> 8;
		mic_hdr->ccmp_pn[3] = pn64 >> 16;
		mic_hdr->ccmp_pn[2] = pn64 >> 24;
		mic_hdr->ccmp_pn[1] = pn64 >> 32;
		mic_hdr->ccmp_pn[0] = pn64 >> 40;

		if (ieee80211_has_a4(hdr->frame_control))
			mic_hdr->hlen = cpu_to_be16(28);
		else
			mic_hdr->hlen = cpu_to_be16(22);

		ether_addr_copy(mic_hdr->addr1, hdr->addr1);
		ether_addr_copy(mic_hdr->addr2, hdr->addr2);
		ether_addr_copy(mic_hdr->addr3, hdr->addr3);

		mic_hdr->frame_control = cpu_to_le16(
			le16_to_cpu(hdr->frame_control) & 0xc78f);
		mic_hdr->seq_ctrl = cpu_to_le16(
				le16_to_cpu(hdr->seq_ctrl) & 0xf);

		if (ieee80211_has_a4(hdr->frame_control))
			ether_addr_copy(mic_hdr->addr4, hdr->addr4);

		memcpy(fifo->tx_key, tx_key->key, WLAN_KEY_LEN_CCMP);

		fifo->frag_ctl |= cpu_to_le16(FRAGCTL_AES);
		return true;
	default:
		break;
	}

	return false;
}

static void vnt_rxtx_rts(struct vnt_usb_send_context *tx_context)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(tx_context->skb);
	struct vnt_tx_buffer *tx_buffer = tx_context->tx_buffer;
	union vnt_tx_head *tx_head = &tx_buffer->tx_head;
	struct vnt_rrv_time_rts *buf = &tx_head->tx_rts.rts;
	union vnt_tx_data_head *head = &tx_head->tx_rts.tx.head;

	buf->rts_rrv_time_aa = vnt_get_rts_duration(tx_context);
	buf->rts_rrv_time_ba = buf->rts_rrv_time_aa;
	buf->rts_rrv_time_bb = buf->rts_rrv_time_aa;

	buf->rrv_time_a = vnt_rxtx_rsvtime_le16(tx_context);
	buf->rrv_time_b = buf->rrv_time_a;

	if (info->control.hw_key) {
		if (vnt_fill_txkey(tx_buffer, tx_context->skb))
			head = &tx_head->tx_rts.tx.mic.head;
	}

	vnt_rxtx_rts_g_head(tx_context, &head->rts_g);
}

static void vnt_rxtx_cts(struct vnt_usb_send_context *tx_context)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(tx_context->skb);
	struct vnt_tx_buffer *tx_buffer = tx_context->tx_buffer;
	union vnt_tx_head *tx_head = &tx_buffer->tx_head;
	struct vnt_rrv_time_cts *buf = &tx_head->tx_cts.cts;
	union vnt_tx_data_head *head = &tx_head->tx_cts.tx.head;

	buf->rrv_time_a = vnt_rxtx_rsvtime_le16(tx_context);
	buf->rrv_time_b = buf->rrv_time_a;

	buf->cts_rrv_time_ba = vnt_get_cts_duration(tx_context);

	if (info->control.hw_key) {
		if (vnt_fill_txkey(tx_buffer, tx_context->skb))
			head = &tx_head->tx_cts.tx.mic.head;
	}

	vnt_fill_cts_head(tx_context, head);
}

static void vnt_rxtx_ab(struct vnt_usb_send_context *tx_context)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(tx_context->skb);
	struct vnt_tx_buffer *tx_buffer = tx_context->tx_buffer;
	union vnt_tx_head *tx_head = &tx_buffer->tx_head;
	struct vnt_rrv_time_ab *buf = &tx_head->tx_ab.ab;
	union vnt_tx_data_head *head = &tx_head->tx_ab.tx.head;

	buf->rrv_time = vnt_rxtx_rsvtime_le16(tx_context);

	if (info->control.hw_key) {
		if (vnt_fill_txkey(tx_buffer, tx_context->skb))
			head = &tx_head->tx_ab.tx.mic.head;
	}

	if (info->control.use_rts) {
		buf->rts_rrv_time = vnt_get_rts_duration(tx_context);

		vnt_rxtx_rts_ab_head(tx_context, &head->rts_ab);

		return;
	}

	vnt_rxtx_datahead_ab(tx_context, &head->data_head_ab);
}

static void vnt_generate_tx_parameter(struct vnt_usb_send_context *tx_context)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(tx_context->skb);

	if (info->control.use_cts_prot) {
		if (info->control.use_rts) {
			vnt_rxtx_rts(tx_context);

			return;
		}

		vnt_rxtx_cts(tx_context);

		return;
	}

	vnt_rxtx_ab(tx_context);
}

static u16 vnt_get_hdr_size(struct ieee80211_tx_info *info)
{
	u16 size = sizeof(struct vnt_tx_datahead_ab);

	if (info->control.use_cts_prot) {
		if (info->control.use_rts)
			size = sizeof(struct vnt_rts_g);
		else
			size = sizeof(struct vnt_cts);
	} else if (info->control.use_rts) {
		size = sizeof(struct vnt_rts_ab);
	}

	if (info->control.hw_key) {
		if (info->control.hw_key->cipher == WLAN_CIPHER_SUITE_CCMP)
			size += sizeof(struct vnt_mic_hdr);
	}

	/* Get rrv_time header */
	if (info->control.use_cts_prot) {
		if (info->control.use_rts)
			size += sizeof(struct vnt_rrv_time_rts);
		else
			size += sizeof(struct vnt_rrv_time_cts);
	} else {
		size += sizeof(struct vnt_rrv_time_ab);
	}

	size += sizeof(struct vnt_tx_fifo_head);

	return size;
}

int vnt_tx_packet(struct vnt_private *priv, struct sk_buff *skb)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_tx_rate *tx_rate = &info->control.rates[0];
	struct ieee80211_rate *rate;
	struct ieee80211_hdr *hdr;
	struct vnt_tx_buffer *tx_buffer;
	struct vnt_tx_fifo_head *tx_buffer_head;
	struct vnt_usb_send_context *tx_context;
	unsigned long flags;
	u8 pkt_type;

	hdr = (struct ieee80211_hdr *)(skb->data);

	rate = ieee80211_get_tx_rate(priv->hw, info);

	if (rate->hw_value > RATE_11M) {
		if (info->band == NL80211_BAND_5GHZ) {
			pkt_type = PK_TYPE_11A;
		} else {
			if (tx_rate->flags & IEEE80211_TX_RC_USE_CTS_PROTECT) {
				if (priv->basic_rates & VNT_B_RATES)
					pkt_type = PK_TYPE_11GB;
				else
					pkt_type = PK_TYPE_11GA;
			} else {
				pkt_type = PK_TYPE_11A;
			}
		}
	} else {
		pkt_type = PK_TYPE_11B;
	}

	spin_lock_irqsave(&priv->lock, flags);

	tx_context = vnt_get_free_context(priv);
	if (!tx_context) {
		dev_dbg(&priv->usb->dev, "%s No free context\n", __func__);
		spin_unlock_irqrestore(&priv->lock, flags);
		return -ENOMEM;
	}

	tx_context->pkt_type = pkt_type;
	tx_context->frame_len = skb->len + 4;
	tx_context->tx_rate =  rate->hw_value;

	spin_unlock_irqrestore(&priv->lock, flags);

	tx_context->skb = skb_clone(skb, GFP_ATOMIC);
	if (!tx_context->skb) {
		tx_context->in_use = false;
		return -ENOMEM;
	}

	tx_buffer = skb_push(skb, vnt_get_hdr_size(info));
	tx_context->tx_buffer = tx_buffer;
	tx_buffer_head = &tx_buffer->fifo_head;

	tx_context->type = CONTEXT_DATA_PACKET;

	/*Set fifo controls */
	if (pkt_type == PK_TYPE_11A)
		tx_buffer_head->fifo_ctl = 0;
	else if (pkt_type == PK_TYPE_11B)
		tx_buffer_head->fifo_ctl = cpu_to_le16(FIFOCTL_11B);
	else if (pkt_type == PK_TYPE_11GB)
		tx_buffer_head->fifo_ctl = cpu_to_le16(FIFOCTL_11GB);
	else if (pkt_type == PK_TYPE_11GA)
		tx_buffer_head->fifo_ctl = cpu_to_le16(FIFOCTL_11GA);

	if (!ieee80211_is_data(hdr->frame_control)) {
		tx_buffer_head->fifo_ctl |= cpu_to_le16(FIFOCTL_GENINT |
							FIFOCTL_ISDMA0);
		tx_buffer_head->fifo_ctl |= cpu_to_le16(FIFOCTL_TMOEN);

		tx_buffer_head->time_stamp =
			cpu_to_le16(DEFAULT_MGN_LIFETIME_RES_64us);
	} else {
		tx_buffer_head->time_stamp =
			cpu_to_le16(DEFAULT_MSDU_LIFETIME_RES_64us);
	}

	if (!(info->flags & IEEE80211_TX_CTL_NO_ACK))
		tx_buffer_head->fifo_ctl |= cpu_to_le16(FIFOCTL_NEEDACK);

	if (ieee80211_has_retry(hdr->frame_control))
		tx_buffer_head->fifo_ctl |= cpu_to_le16(FIFOCTL_LRETRY);

	if (info->control.use_rts)
		tx_buffer_head->fifo_ctl |= cpu_to_le16(FIFOCTL_RTS);

	if (ieee80211_has_a4(hdr->frame_control))
		tx_buffer_head->fifo_ctl |= cpu_to_le16(FIFOCTL_LHEAD);

	tx_buffer_head->frag_ctl =
			cpu_to_le16(ieee80211_hdrlen(hdr->frame_control) << 10);

	if (info->control.hw_key)
		tx_context->frame_len += info->control.hw_key->icv_len;

	tx_buffer_head->current_rate = cpu_to_le16(rate->hw_value);

	vnt_generate_tx_parameter(tx_context);

	tx_buffer_head->frag_ctl |= cpu_to_le16(FRAGCTL_NONFRAG);

	priv->seq_counter = (le16_to_cpu(hdr->seq_ctrl) &
						IEEE80211_SCTL_SEQ) >> 4;

	spin_lock_irqsave(&priv->lock, flags);

	if (vnt_tx_context(priv, tx_context, skb)) {
		dev_kfree_skb(tx_context->skb);
		spin_unlock_irqrestore(&priv->lock, flags);
		return -EIO;
	}

	dev_kfree_skb(skb);

	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

static int vnt_beacon_xmit(struct vnt_private *priv, struct sk_buff *skb)
{
	struct vnt_tx_short_buf_head *short_head;
	struct ieee80211_tx_info *info;
	struct vnt_usb_send_context *context;
	struct ieee80211_mgmt *mgmt_hdr;
	unsigned long flags;
	u32 frame_size = skb->len + 4;
	u16 current_rate;

	spin_lock_irqsave(&priv->lock, flags);

	context = vnt_get_free_context(priv);
	if (!context) {
		dev_dbg(&priv->usb->dev, "%s No free context!\n", __func__);
		spin_unlock_irqrestore(&priv->lock, flags);
		return -ENOMEM;
	}

	context->skb = skb;

	spin_unlock_irqrestore(&priv->lock, flags);

	mgmt_hdr = (struct ieee80211_mgmt *)skb->data;
	short_head = skb_push(skb, sizeof(*short_head));

	if (priv->bb_type == BB_TYPE_11A) {
		current_rate = RATE_6M;

		/* Get SignalField,ServiceField,Length */
		vnt_get_phy_field(priv, frame_size, current_rate,
				  PK_TYPE_11A, &short_head->ab);

		/* Get TimeStampOff */
		short_head->time_stamp_off =
				vnt_time_stamp_off(priv, current_rate);
	} else {
		current_rate = RATE_1M;
		short_head->fifo_ctl |= cpu_to_le16(FIFOCTL_11B);

		/* Get SignalField,ServiceField,Length */
		vnt_get_phy_field(priv, frame_size, current_rate,
				  PK_TYPE_11B, &short_head->ab);

		/* Get TimeStampOff */
		short_head->time_stamp_off =
			vnt_time_stamp_off(priv, current_rate);
	}

	/* Get Duration */
	short_head->duration = mgmt_hdr->duration;

	/* time stamp always 0 */
	mgmt_hdr->u.beacon.timestamp = 0;

	info = IEEE80211_SKB_CB(skb);
	if (info->flags & IEEE80211_TX_CTL_ASSIGN_SEQ) {
		struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)mgmt_hdr;

		hdr->duration_id = 0;
		hdr->seq_ctrl = cpu_to_le16(priv->seq_counter << 4);
	}

	priv->seq_counter++;
	if (priv->seq_counter > 0x0fff)
		priv->seq_counter = 0;

	context->type = CONTEXT_BEACON_PACKET;

	spin_lock_irqsave(&priv->lock, flags);

	if (vnt_tx_context(priv, context, skb))
		ieee80211_free_txskb(priv->hw, context->skb);

	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

int vnt_beacon_make(struct vnt_private *priv, struct ieee80211_vif *vif)
{
	struct sk_buff *beacon;

	beacon = ieee80211_beacon_get(priv->hw, vif);
	if (!beacon)
		return -ENOMEM;

	if (vnt_beacon_xmit(priv, beacon)) {
		ieee80211_free_txskb(priv->hw, beacon);
		return -ENODEV;
	}

	return 0;
}

int vnt_beacon_enable(struct vnt_private *priv, struct ieee80211_vif *vif,
		      struct ieee80211_bss_conf *conf)
{
	vnt_mac_reg_bits_off(priv, MAC_REG_TCR, TCR_AUTOBCNTX);

	vnt_mac_reg_bits_off(priv, MAC_REG_TFTCTL, TFTCTL_TSFCNTREN);

	vnt_mac_set_beacon_interval(priv, conf->beacon_int);

	vnt_clear_current_tsf(priv);

	vnt_mac_reg_bits_on(priv, MAC_REG_TFTCTL, TFTCTL_TSFCNTREN);

	vnt_reset_next_tbtt(priv, conf->beacon_int);

	return vnt_beacon_make(priv, vif);
}
