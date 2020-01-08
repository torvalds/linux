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
 *      vnt_get_duration_le - get tx data required duration
 *      vnt_get_rtscts_duration_le- get rtx/cts required duration
 *      vnt_get_rtscts_rsvtime_le- get rts/cts reserved time
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

static const u16 vnt_fb_opt0[2][5] = {
	{RATE_12M, RATE_18M, RATE_24M, RATE_36M, RATE_48M}, /* fallback_rate0 */
	{RATE_12M, RATE_12M, RATE_18M, RATE_24M, RATE_36M}, /* fallback_rate1 */
};

static const u16 vnt_fb_opt1[2][5] = {
	{RATE_12M, RATE_18M, RATE_24M, RATE_24M, RATE_36M}, /* fallback_rate0 */
	{RATE_6M,  RATE_6M,  RATE_12M, RATE_12M, RATE_18M}, /* fallback_rate1 */
};

#define RTSDUR_BB       0
#define RTSDUR_BA       1
#define RTSDUR_AA       2
#define CTSDUR_BA       3
#define RTSDUR_BA_F0    4
#define RTSDUR_AA_F0    5
#define RTSDUR_BA_F1    6
#define RTSDUR_AA_F1    7
#define CTSDUR_BA_F0    8
#define CTSDUR_BA_F1    9
#define DATADUR_B       10
#define DATADUR_A       11
#define DATADUR_A_F0    12
#define DATADUR_A_F1    13

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
			memset(context->data, 0,
			       MAX_TOTAL_SIZE_WITH_ALL_HEADERS);

			context->hdr = NULL;

			return context;
		}
	}

	if (ii == priv->num_tx_context) {
		dev_dbg(&priv->usb->dev, "%s No Free Tx Context\n", __func__);

		ieee80211_stop_queues(priv->hw);
	}

	return NULL;
}

static __le16 vnt_time_stamp_off(struct vnt_private *priv, u16 rate)
{
	return cpu_to_le16(vnt_time_stampoff[priv->preamble_type % 2]
							[rate % MAX_RATE]);
}

static u32 vnt_get_rsvtime(struct vnt_private *priv, u8 pkt_type,
			   u32 frame_length, u16 rate, int need_ack)
{
	u32 data_time, ack_time;

	data_time = vnt_get_frame_time(priv->preamble_type, pkt_type,
				       frame_length, rate);

	if (pkt_type == PK_TYPE_11B)
		ack_time = vnt_get_frame_time(priv->preamble_type, pkt_type,
					      14, (u16)priv->top_cck_basic_rate);
	else
		ack_time = vnt_get_frame_time(priv->preamble_type, pkt_type,
					      14, (u16)priv->top_ofdm_basic_rate);

	if (need_ack)
		return data_time + priv->sifs + ack_time;

	return data_time;
}

static __le16 vnt_rxtx_rsvtime_le16(struct vnt_private *priv, u8 pkt_type,
				    u32 frame_length, u16 rate, int need_ack)
{
	return cpu_to_le16((u16)vnt_get_rsvtime(priv, pkt_type,
		frame_length, rate, need_ack));
}

static __le16 vnt_get_rtscts_rsvtime_le(struct vnt_private *priv, u8 rsv_type,
					u8 pkt_type, u32 frame_length,
					u16 current_rate)
{
	u32 rrv_time, rts_time, cts_time, ack_time, data_time;

	rrv_time = 0;
	rts_time = 0;
	cts_time = 0;
	ack_time = 0;

	data_time = vnt_get_frame_time(priv->preamble_type, pkt_type,
				       frame_length, current_rate);

	if (rsv_type == 0) {
		rts_time = vnt_get_frame_time(priv->preamble_type, pkt_type,
					      20, priv->top_cck_basic_rate);
		ack_time = vnt_get_frame_time(priv->preamble_type,
					      pkt_type, 14,
					      priv->top_cck_basic_rate);
		cts_time = ack_time;

	} else if (rsv_type == 1) {
		rts_time = vnt_get_frame_time(priv->preamble_type, pkt_type,
					      20, priv->top_cck_basic_rate);
		cts_time = vnt_get_frame_time(priv->preamble_type, pkt_type,
					      14, priv->top_cck_basic_rate);
		ack_time = vnt_get_frame_time(priv->preamble_type, pkt_type,
					      14, priv->top_ofdm_basic_rate);
	} else if (rsv_type == 2) {
		rts_time = vnt_get_frame_time(priv->preamble_type, pkt_type,
					      20, priv->top_ofdm_basic_rate);
		ack_time = vnt_get_frame_time(priv->preamble_type,
					      pkt_type, 14,
					      priv->top_ofdm_basic_rate);
		cts_time = ack_time;

	} else if (rsv_type == 3) {
		cts_time = vnt_get_frame_time(priv->preamble_type, pkt_type,
					      14, priv->top_cck_basic_rate);
		ack_time = vnt_get_frame_time(priv->preamble_type, pkt_type,
					      14, priv->top_ofdm_basic_rate);

		rrv_time = cts_time + ack_time + data_time + 2 * priv->sifs;

		return cpu_to_le16((u16)rrv_time);
	}

	rrv_time = rts_time + cts_time + ack_time + data_time + 3 * priv->sifs;

	return cpu_to_le16((u16)rrv_time);
}

static __le16 vnt_get_duration_le(struct vnt_private *priv, u8 pkt_type,
				  int need_ack)
{
	u32 ack_time = 0;

	if (need_ack) {
		if (pkt_type == PK_TYPE_11B)
			ack_time = vnt_get_frame_time(priv->preamble_type,
						      pkt_type, 14,
						      priv->top_cck_basic_rate);
		else
			ack_time = vnt_get_frame_time(priv->preamble_type,
						      pkt_type, 14,
						      priv->top_ofdm_basic_rate);

		return cpu_to_le16((u16)(priv->sifs + ack_time));
	}

	return 0;
}

static __le16 vnt_get_rtscts_duration_le(struct vnt_usb_send_context *context,
					 u8 dur_type, u8 pkt_type, u16 rate)
{
	struct vnt_private *priv = context->priv;
	u32 cts_time = 0, dur_time = 0;
	u32 frame_length = context->frame_len;
	u8 need_ack = context->need_ack;

	switch (dur_type) {
	case RTSDUR_BB:
	case RTSDUR_BA:
	case RTSDUR_BA_F0:
	case RTSDUR_BA_F1:
		cts_time = vnt_get_frame_time(priv->preamble_type, pkt_type,
					      14, priv->top_cck_basic_rate);
		dur_time = cts_time + 2 * priv->sifs +
			vnt_get_rsvtime(priv, pkt_type,
					frame_length, rate, need_ack);
		break;

	case RTSDUR_AA:
	case RTSDUR_AA_F0:
	case RTSDUR_AA_F1:
		cts_time = vnt_get_frame_time(priv->preamble_type, pkt_type,
					      14, priv->top_ofdm_basic_rate);
		dur_time = cts_time + 2 * priv->sifs +
			vnt_get_rsvtime(priv, pkt_type,
					frame_length, rate, need_ack);
		break;

	case CTSDUR_BA:
	case CTSDUR_BA_F0:
	case CTSDUR_BA_F1:
		dur_time = priv->sifs + vnt_get_rsvtime(priv,
				pkt_type, frame_length, rate, need_ack);
		break;

	default:
		break;
	}

	return cpu_to_le16((u16)dur_time);
}

static u16 vnt_mac_hdr_pos(struct vnt_usb_send_context *tx_context,
			   struct ieee80211_hdr *hdr)
{
	u8 *head = tx_context->data + offsetof(struct vnt_tx_buffer, fifo_head);
	u8 *hdr_pos = (u8 *)hdr;

	tx_context->hdr = hdr;
	if (!tx_context->hdr)
		return 0;

	return (u16)(hdr_pos - head);
}

static u16 vnt_rxtx_datahead_g(struct vnt_usb_send_context *tx_context,
			       struct vnt_tx_datahead_g *buf)
{
	struct vnt_private *priv = tx_context->priv;
	struct ieee80211_hdr *hdr =
				(struct ieee80211_hdr *)tx_context->skb->data;
	u32 frame_len = tx_context->frame_len;
	u16 rate = tx_context->tx_rate;
	u8 need_ack = tx_context->need_ack;

	/* Get SignalField,ServiceField,Length */
	vnt_get_phy_field(priv, frame_len, rate, tx_context->pkt_type, &buf->a);
	vnt_get_phy_field(priv, frame_len, priv->top_cck_basic_rate,
			  PK_TYPE_11B, &buf->b);

	/* Get Duration and TimeStamp */
	if (ieee80211_is_nullfunc(hdr->frame_control)) {
		buf->duration_a = hdr->duration_id;
		buf->duration_b = hdr->duration_id;
	} else {
		buf->duration_a = vnt_get_duration_le(priv,
						tx_context->pkt_type, need_ack);
		buf->duration_b = vnt_get_duration_le(priv,
							PK_TYPE_11B, need_ack);
	}

	buf->time_stamp_off_a = vnt_time_stamp_off(priv, rate);
	buf->time_stamp_off_b = vnt_time_stamp_off(priv,
					priv->top_cck_basic_rate);

	tx_context->tx_hdr_size = vnt_mac_hdr_pos(tx_context, &buf->hdr);

	return le16_to_cpu(buf->duration_a);
}

static u16 vnt_rxtx_datahead_g_fb(struct vnt_usb_send_context *tx_context,
				  struct vnt_tx_datahead_g_fb *buf)
{
	struct vnt_private *priv = tx_context->priv;
	u32 frame_len = tx_context->frame_len;
	u16 rate = tx_context->tx_rate;
	u8 need_ack = tx_context->need_ack;

	/* Get SignalField,ServiceField,Length */
	vnt_get_phy_field(priv, frame_len, rate, tx_context->pkt_type, &buf->a);

	vnt_get_phy_field(priv, frame_len, priv->top_cck_basic_rate,
			  PK_TYPE_11B, &buf->b);

	/* Get Duration and TimeStamp */
	buf->duration_a = vnt_get_duration_le(priv, tx_context->pkt_type,
					      need_ack);
	buf->duration_b = vnt_get_duration_le(priv, PK_TYPE_11B, need_ack);

	buf->duration_a_f0 = vnt_get_duration_le(priv, tx_context->pkt_type,
						 need_ack);
	buf->duration_a_f1 = vnt_get_duration_le(priv, tx_context->pkt_type,
						 need_ack);

	buf->time_stamp_off_a = vnt_time_stamp_off(priv, rate);
	buf->time_stamp_off_b = vnt_time_stamp_off(priv,
						priv->top_cck_basic_rate);

	tx_context->tx_hdr_size = vnt_mac_hdr_pos(tx_context, &buf->hdr);

	return le16_to_cpu(buf->duration_a);
}

static u16 vnt_rxtx_datahead_a_fb(struct vnt_usb_send_context *tx_context,
				  struct vnt_tx_datahead_a_fb *buf)
{
	struct vnt_private *priv = tx_context->priv;
	u16 rate = tx_context->tx_rate;
	u8 pkt_type = tx_context->pkt_type;
	u8 need_ack = tx_context->need_ack;
	u32 frame_len = tx_context->frame_len;

	/* Get SignalField,ServiceField,Length */
	vnt_get_phy_field(priv, frame_len, rate, pkt_type, &buf->a);
	/* Get Duration and TimeStampOff */
	buf->duration = vnt_get_duration_le(priv, pkt_type, need_ack);

	buf->duration_f0 = vnt_get_duration_le(priv, pkt_type, need_ack);
	buf->duration_f1 = vnt_get_duration_le(priv, pkt_type, need_ack);

	buf->time_stamp_off = vnt_time_stamp_off(priv, rate);

	tx_context->tx_hdr_size = vnt_mac_hdr_pos(tx_context, &buf->hdr);

	return le16_to_cpu(buf->duration);
}

static u16 vnt_rxtx_datahead_ab(struct vnt_usb_send_context *tx_context,
				struct vnt_tx_datahead_ab *buf)
{
	struct vnt_private *priv = tx_context->priv;
	struct ieee80211_hdr *hdr =
				(struct ieee80211_hdr *)tx_context->skb->data;
	u32 frame_len = tx_context->frame_len;
	u16 rate = tx_context->tx_rate;
	u8 need_ack = tx_context->need_ack;

	/* Get SignalField,ServiceField,Length */
	vnt_get_phy_field(priv, frame_len, rate,
			  tx_context->pkt_type, &buf->ab);

	/* Get Duration and TimeStampOff */
	if (ieee80211_is_nullfunc(hdr->frame_control)) {
		buf->duration = hdr->duration_id;
	} else {
		buf->duration = vnt_get_duration_le(priv, tx_context->pkt_type,
						    need_ack);
	}

	buf->time_stamp_off = vnt_time_stamp_off(priv, rate);

	tx_context->tx_hdr_size = vnt_mac_hdr_pos(tx_context, &buf->hdr);

	return le16_to_cpu(buf->duration);
}

static int vnt_fill_ieee80211_rts(struct vnt_usb_send_context *tx_context,
				  struct ieee80211_rts *rts, __le16 duration)
{
	struct ieee80211_hdr *hdr =
				(struct ieee80211_hdr *)tx_context->skb->data;

	rts->duration = duration;
	rts->frame_control =
		cpu_to_le16(IEEE80211_FTYPE_CTL | IEEE80211_STYPE_RTS);

	ether_addr_copy(rts->ra, hdr->addr1);
	ether_addr_copy(rts->ta, hdr->addr2);

	return 0;
}

static u16 vnt_rxtx_rts_g_head(struct vnt_usb_send_context *tx_context,
			       struct vnt_rts_g *buf)
{
	struct vnt_private *priv = tx_context->priv;
	u16 rts_frame_len = 20;
	u16 current_rate = tx_context->tx_rate;

	vnt_get_phy_field(priv, rts_frame_len, priv->top_cck_basic_rate,
			  PK_TYPE_11B, &buf->b);
	vnt_get_phy_field(priv, rts_frame_len, priv->top_ofdm_basic_rate,
			  tx_context->pkt_type, &buf->a);

	buf->duration_bb = vnt_get_rtscts_duration_le(tx_context, RTSDUR_BB,
						      PK_TYPE_11B,
						      priv->top_cck_basic_rate);
	buf->duration_aa = vnt_get_rtscts_duration_le(tx_context, RTSDUR_AA,
						      tx_context->pkt_type,
						      current_rate);
	buf->duration_ba = vnt_get_rtscts_duration_le(tx_context, RTSDUR_BA,
						      tx_context->pkt_type,
						      current_rate);

	vnt_fill_ieee80211_rts(tx_context, &buf->data, buf->duration_aa);

	return vnt_rxtx_datahead_g(tx_context, &buf->data_head);
}

static u16 vnt_rxtx_rts_g_fb_head(struct vnt_usb_send_context *tx_context,
				  struct vnt_rts_g_fb *buf)
{
	struct vnt_private *priv = tx_context->priv;
	u16 current_rate = tx_context->tx_rate;
	u16 rts_frame_len = 20;

	vnt_get_phy_field(priv, rts_frame_len, priv->top_cck_basic_rate,
			  PK_TYPE_11B, &buf->b);
	vnt_get_phy_field(priv, rts_frame_len, priv->top_ofdm_basic_rate,
			  tx_context->pkt_type, &buf->a);

	buf->duration_bb = vnt_get_rtscts_duration_le(tx_context, RTSDUR_BB,
						      PK_TYPE_11B,
						      priv->top_cck_basic_rate);
	buf->duration_aa = vnt_get_rtscts_duration_le(tx_context, RTSDUR_AA,
						      tx_context->pkt_type,
						      current_rate);
	buf->duration_ba = vnt_get_rtscts_duration_le(tx_context, RTSDUR_BA,
						      tx_context->pkt_type,
						      current_rate);

	buf->rts_duration_ba_f0 =
		vnt_get_rtscts_duration_le(tx_context, RTSDUR_BA_F0,
					   tx_context->pkt_type,
					   priv->tx_rate_fb0);
	buf->rts_duration_aa_f0 =
		vnt_get_rtscts_duration_le(tx_context, RTSDUR_AA_F0,
					   tx_context->pkt_type,
					   priv->tx_rate_fb0);
	buf->rts_duration_ba_f1 =
		vnt_get_rtscts_duration_le(tx_context, RTSDUR_BA_F1,
					   tx_context->pkt_type,
					   priv->tx_rate_fb1);
	buf->rts_duration_aa_f1 =
		vnt_get_rtscts_duration_le(tx_context, RTSDUR_AA_F1,
					   tx_context->pkt_type,
					   priv->tx_rate_fb1);

	vnt_fill_ieee80211_rts(tx_context, &buf->data, buf->duration_aa);

	return vnt_rxtx_datahead_g_fb(tx_context, &buf->data_head);
}

static u16 vnt_rxtx_rts_ab_head(struct vnt_usb_send_context *tx_context,
				struct vnt_rts_ab *buf)
{
	struct vnt_private *priv = tx_context->priv;
	u16 current_rate = tx_context->tx_rate;
	u16 rts_frame_len = 20;

	vnt_get_phy_field(priv, rts_frame_len, priv->top_ofdm_basic_rate,
			  tx_context->pkt_type, &buf->ab);

	buf->duration = vnt_get_rtscts_duration_le(tx_context, RTSDUR_AA,
						   tx_context->pkt_type,
						   current_rate);

	vnt_fill_ieee80211_rts(tx_context, &buf->data, buf->duration);

	return vnt_rxtx_datahead_ab(tx_context, &buf->data_head);
}

static u16 vnt_rxtx_rts_a_fb_head(struct vnt_usb_send_context *tx_context,
				  struct vnt_rts_a_fb *buf)
{
	struct vnt_private *priv = tx_context->priv;
	u16 current_rate = tx_context->tx_rate;
	u16 rts_frame_len = 20;

	vnt_get_phy_field(priv, rts_frame_len, priv->top_ofdm_basic_rate,
			  tx_context->pkt_type, &buf->a);

	buf->duration = vnt_get_rtscts_duration_le(tx_context, RTSDUR_AA,
						   tx_context->pkt_type,
						   current_rate);

	buf->rts_duration_f0 =
		vnt_get_rtscts_duration_le(tx_context, RTSDUR_AA_F0,
					   tx_context->pkt_type,
					   priv->tx_rate_fb0);

	buf->rts_duration_f1 =
		vnt_get_rtscts_duration_le(tx_context, RTSDUR_AA_F1,
					   tx_context->pkt_type,
					   priv->tx_rate_fb1);

	vnt_fill_ieee80211_rts(tx_context, &buf->data, buf->duration);

	return vnt_rxtx_datahead_a_fb(tx_context, &buf->data_head);
}

static u16 vnt_fill_cts_fb_head(struct vnt_usb_send_context *tx_context,
				union vnt_tx_data_head *head)
{
	struct vnt_private *priv = tx_context->priv;
	struct vnt_cts_fb *buf = &head->cts_g_fb;
	u32 cts_frame_len = 14;
	u16 current_rate = tx_context->tx_rate;

	/* Get SignalField,ServiceField,Length */
	vnt_get_phy_field(priv, cts_frame_len, priv->top_cck_basic_rate,
			  PK_TYPE_11B, &buf->b);

	buf->duration_ba =
		vnt_get_rtscts_duration_le(tx_context, CTSDUR_BA,
					   tx_context->pkt_type,
					   current_rate);
	/* Get CTSDuration_ba_f0 */
	buf->cts_duration_ba_f0 =
		vnt_get_rtscts_duration_le(tx_context, CTSDUR_BA_F0,
					   tx_context->pkt_type,
					   priv->tx_rate_fb0);
	/* Get CTSDuration_ba_f1 */
	buf->cts_duration_ba_f1 =
		vnt_get_rtscts_duration_le(tx_context, CTSDUR_BA_F1,
					   tx_context->pkt_type,
					   priv->tx_rate_fb1);
	/* Get CTS Frame body */
	buf->data.duration = buf->duration_ba;
	buf->data.frame_control =
		cpu_to_le16(IEEE80211_FTYPE_CTL | IEEE80211_STYPE_CTS);

	ether_addr_copy(buf->data.ra, priv->current_net_addr);

	return vnt_rxtx_datahead_g_fb(tx_context, &buf->data_head);
}

static u16 vnt_fill_cts_head(struct vnt_usb_send_context *tx_context,
			     union vnt_tx_data_head *head)
{
	struct vnt_private *priv = tx_context->priv;
	struct vnt_cts *buf = &head->cts_g;
	u32 cts_frame_len = 14;
	u16 current_rate = tx_context->tx_rate;

	/* Get SignalField,ServiceField,Length */
	vnt_get_phy_field(priv, cts_frame_len, priv->top_cck_basic_rate,
			  PK_TYPE_11B, &buf->b);
	/* Get CTSDuration_ba */
	buf->duration_ba =
		vnt_get_rtscts_duration_le(tx_context, CTSDUR_BA,
					   tx_context->pkt_type,
					   current_rate);
	/*Get CTS Frame body*/
	buf->data.duration = buf->duration_ba;
	buf->data.frame_control =
		cpu_to_le16(IEEE80211_FTYPE_CTL | IEEE80211_STYPE_CTS);

	ether_addr_copy(buf->data.ra, priv->current_net_addr);

	return vnt_rxtx_datahead_g(tx_context, &buf->data_head);
}

static u16 vnt_rxtx_rts(struct vnt_usb_send_context *tx_context,
			union vnt_tx_head *tx_head, bool need_mic)
{
	struct vnt_private *priv = tx_context->priv;
	struct vnt_rrv_time_rts *buf = &tx_head->tx_rts.rts;
	union vnt_tx_data_head *head = &tx_head->tx_rts.tx.head;
	u32 frame_len = tx_context->frame_len;
	u16 current_rate = tx_context->tx_rate;
	u8 need_ack = tx_context->need_ack;

	buf->rts_rrv_time_aa = vnt_get_rtscts_rsvtime_le(priv, 2,
			tx_context->pkt_type, frame_len, current_rate);
	buf->rts_rrv_time_ba = vnt_get_rtscts_rsvtime_le(priv, 1,
			tx_context->pkt_type, frame_len, current_rate);
	buf->rts_rrv_time_bb = vnt_get_rtscts_rsvtime_le(priv, 0,
			tx_context->pkt_type, frame_len, current_rate);

	buf->rrv_time_a = vnt_rxtx_rsvtime_le16(priv, tx_context->pkt_type,
						frame_len, current_rate,
						need_ack);
	buf->rrv_time_b = vnt_rxtx_rsvtime_le16(priv, PK_TYPE_11B, frame_len,
					priv->top_cck_basic_rate, need_ack);

	if (need_mic)
		head = &tx_head->tx_rts.tx.mic.head;

	if (tx_context->fb_option)
		return vnt_rxtx_rts_g_fb_head(tx_context, &head->rts_g_fb);

	return vnt_rxtx_rts_g_head(tx_context, &head->rts_g);
}

static u16 vnt_rxtx_cts(struct vnt_usb_send_context *tx_context,
			union vnt_tx_head *tx_head, bool need_mic)
{
	struct vnt_private *priv = tx_context->priv;
	struct vnt_rrv_time_cts *buf = &tx_head->tx_cts.cts;
	union vnt_tx_data_head *head = &tx_head->tx_cts.tx.head;
	u32 frame_len = tx_context->frame_len;
	u16 current_rate = tx_context->tx_rate;
	u8 need_ack = tx_context->need_ack;

	buf->rrv_time_a = vnt_rxtx_rsvtime_le16(priv, tx_context->pkt_type,
					frame_len, current_rate, need_ack);
	buf->rrv_time_b = vnt_rxtx_rsvtime_le16(priv, PK_TYPE_11B,
				frame_len, priv->top_cck_basic_rate, need_ack);

	buf->cts_rrv_time_ba = vnt_get_rtscts_rsvtime_le(priv, 3,
			tx_context->pkt_type, frame_len, current_rate);

	if (need_mic)
		head = &tx_head->tx_cts.tx.mic.head;

	/* Fill CTS */
	if (tx_context->fb_option)
		return vnt_fill_cts_fb_head(tx_context, head);

	return vnt_fill_cts_head(tx_context, head);
}

static u16 vnt_rxtx_ab(struct vnt_usb_send_context *tx_context,
		       union vnt_tx_head *tx_head, bool need_rts, bool need_mic)
{
	struct vnt_private *priv = tx_context->priv;
	struct vnt_rrv_time_ab *buf = &tx_head->tx_ab.ab;
	union vnt_tx_data_head *head = &tx_head->tx_ab.tx.head;
	u32 frame_len = tx_context->frame_len;
	u16 current_rate = tx_context->tx_rate;
	u8 need_ack = tx_context->need_ack;

	buf->rrv_time = vnt_rxtx_rsvtime_le16(priv, tx_context->pkt_type,
			frame_len, current_rate, need_ack);

	if (need_mic)
		head = &tx_head->tx_ab.tx.mic.head;

	if (need_rts) {
		if (tx_context->pkt_type == PK_TYPE_11B)
			buf->rts_rrv_time = vnt_get_rtscts_rsvtime_le(priv, 0,
				tx_context->pkt_type, frame_len, current_rate);
		else /* PK_TYPE_11A */
			buf->rts_rrv_time = vnt_get_rtscts_rsvtime_le(priv, 2,
				tx_context->pkt_type, frame_len, current_rate);

		if (tx_context->fb_option &&
		    tx_context->pkt_type == PK_TYPE_11A)
			return vnt_rxtx_rts_a_fb_head(tx_context,
						      &head->rts_a_fb);

		return vnt_rxtx_rts_ab_head(tx_context, &head->rts_ab);
	}

	if (tx_context->pkt_type == PK_TYPE_11A)
		return vnt_rxtx_datahead_a_fb(tx_context,
					      &head->data_head_a_fb);

	return vnt_rxtx_datahead_ab(tx_context, &head->data_head_ab);
}

static u16 vnt_generate_tx_parameter(struct vnt_usb_send_context *tx_context,
				     struct vnt_tx_buffer *tx_buffer,
				     struct vnt_mic_hdr **mic_hdr, u32 need_mic,
				     bool need_rts)
{
	if (tx_context->pkt_type == PK_TYPE_11GB ||
	    tx_context->pkt_type == PK_TYPE_11GA) {
		if (need_rts) {
			if (need_mic)
				*mic_hdr =
					&tx_buffer->tx_head.tx_rts.tx.mic.hdr;

			return vnt_rxtx_rts(tx_context, &tx_buffer->tx_head,
					    need_mic);
		}

		if (need_mic)
			*mic_hdr = &tx_buffer->tx_head.tx_cts.tx.mic.hdr;

		return vnt_rxtx_cts(tx_context, &tx_buffer->tx_head, need_mic);
	}

	if (need_mic)
		*mic_hdr = &tx_buffer->tx_head.tx_ab.tx.mic.hdr;

	return vnt_rxtx_ab(tx_context, &tx_buffer->tx_head, need_rts, need_mic);
}

static void vnt_fill_txkey(struct vnt_usb_send_context *tx_context,
			   u8 *key_buffer, struct ieee80211_key_conf *tx_key,
			   struct sk_buff *skb, u16 payload_len,
			   struct vnt_mic_hdr *mic_hdr)
{
	struct ieee80211_hdr *hdr = tx_context->hdr;
	u64 pn64;
	u8 *iv = ((u8 *)hdr + ieee80211_get_hdrlen_from_skb(skb));

	/* strip header and icv len from payload */
	payload_len -= ieee80211_get_hdrlen_from_skb(skb);
	payload_len -= tx_key->icv_len;

	switch (tx_key->cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
	case WLAN_CIPHER_SUITE_WEP104:
		memcpy(key_buffer, iv, 3);
		memcpy(key_buffer + 3, tx_key->key, tx_key->keylen);

		if (tx_key->keylen == WLAN_KEY_LEN_WEP40) {
			memcpy(key_buffer + 8, iv, 3);
			memcpy(key_buffer + 11,
			       tx_key->key, WLAN_KEY_LEN_WEP40);
		}

		break;
	case WLAN_CIPHER_SUITE_TKIP:
		ieee80211_get_tkip_p2k(tx_key, skb, key_buffer);

		break;
	case WLAN_CIPHER_SUITE_CCMP:

		if (!mic_hdr)
			return;

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

		memcpy(key_buffer, tx_key->key, WLAN_KEY_LEN_CCMP);

		break;
	default:
		break;
	}
}

int vnt_tx_packet(struct vnt_private *priv, struct sk_buff *skb)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_tx_rate *tx_rate = &info->control.rates[0];
	struct ieee80211_rate *rate;
	struct ieee80211_key_conf *tx_key;
	struct ieee80211_hdr *hdr;
	struct vnt_mic_hdr *mic_hdr = NULL;
	struct vnt_tx_buffer *tx_buffer;
	struct vnt_tx_fifo_head *tx_buffer_head;
	struct vnt_usb_send_context *tx_context;
	unsigned long flags;
	u16 tx_bytes, tx_header_size, tx_body_size, current_rate, duration_id;
	u8 pkt_type, fb_option = AUTO_FB_NONE;
	bool need_rts = false;
	bool need_mic = false;

	hdr = (struct ieee80211_hdr *)(skb->data);

	rate = ieee80211_get_tx_rate(priv->hw, info);

	current_rate = rate->hw_value;
	if (priv->current_rate != current_rate &&
	    !(priv->hw->conf.flags & IEEE80211_CONF_OFFCHANNEL)) {
		priv->current_rate = current_rate;
		vnt_schedule_command(priv, WLAN_CMD_SETPOWER);
	}

	if (current_rate > RATE_11M) {
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

	tx_context->skb = skb;
	tx_context->pkt_type = pkt_type;
	tx_context->need_ack = false;
	tx_context->frame_len = skb->len + 4;
	tx_context->tx_rate = current_rate;

	spin_unlock_irqrestore(&priv->lock, flags);

	tx_buffer = (struct vnt_tx_buffer *)tx_context->data;
	tx_buffer_head = &tx_buffer->fifo_head;
	tx_body_size = skb->len;

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

	if (!(info->flags & IEEE80211_TX_CTL_NO_ACK)) {
		tx_buffer_head->fifo_ctl |= cpu_to_le16(FIFOCTL_NEEDACK);
		tx_context->need_ack = true;
	}

	if (ieee80211_has_retry(hdr->frame_control))
		tx_buffer_head->fifo_ctl |= cpu_to_le16(FIFOCTL_LRETRY);

	if (tx_rate->flags & IEEE80211_TX_RC_USE_SHORT_PREAMBLE)
		priv->preamble_type = PREAMBLE_SHORT;
	else
		priv->preamble_type = PREAMBLE_LONG;

	if (tx_rate->flags & IEEE80211_TX_RC_USE_RTS_CTS) {
		need_rts = true;
		tx_buffer_head->fifo_ctl |= cpu_to_le16(FIFOCTL_RTS);
	}

	if (ieee80211_has_a4(hdr->frame_control))
		tx_buffer_head->fifo_ctl |= cpu_to_le16(FIFOCTL_LHEAD);

	tx_buffer_head->frag_ctl =
			cpu_to_le16(ieee80211_get_hdrlen_from_skb(skb) << 10);

	if (info->control.hw_key) {
		tx_key = info->control.hw_key;
		switch (info->control.hw_key->cipher) {
		case WLAN_CIPHER_SUITE_WEP40:
		case WLAN_CIPHER_SUITE_WEP104:
			tx_buffer_head->frag_ctl |= cpu_to_le16(FRAGCTL_LEGACY);
			break;
		case WLAN_CIPHER_SUITE_TKIP:
			tx_buffer_head->frag_ctl |= cpu_to_le16(FRAGCTL_TKIP);
			break;
		case WLAN_CIPHER_SUITE_CCMP:
			tx_buffer_head->frag_ctl |= cpu_to_le16(FRAGCTL_AES);
			need_mic = true;
		default:
			break;
		}
		tx_context->frame_len += tx_key->icv_len;
	}

	tx_buffer_head->current_rate = cpu_to_le16(current_rate);

	/* legacy rates TODO use ieee80211_tx_rate */
	if (current_rate >= RATE_18M && ieee80211_is_data(hdr->frame_control)) {
		if (priv->auto_fb_ctrl == AUTO_FB_0) {
			tx_buffer_head->fifo_ctl |=
						cpu_to_le16(FIFOCTL_AUTO_FB_0);

			priv->tx_rate_fb0 =
				vnt_fb_opt0[FB_RATE0][current_rate - RATE_18M];
			priv->tx_rate_fb1 =
				vnt_fb_opt0[FB_RATE1][current_rate - RATE_18M];

			fb_option = AUTO_FB_0;
		} else if (priv->auto_fb_ctrl == AUTO_FB_1) {
			tx_buffer_head->fifo_ctl |=
						cpu_to_le16(FIFOCTL_AUTO_FB_1);

			priv->tx_rate_fb0 =
				vnt_fb_opt1[FB_RATE0][current_rate - RATE_18M];
			priv->tx_rate_fb1 =
				vnt_fb_opt1[FB_RATE1][current_rate - RATE_18M];

			fb_option = AUTO_FB_1;
		}
	}

	tx_context->fb_option = fb_option;

	duration_id = vnt_generate_tx_parameter(tx_context, tx_buffer, &mic_hdr,
						need_mic, need_rts);

	tx_header_size = tx_context->tx_hdr_size;
	if (!tx_header_size) {
		tx_context->in_use = false;
		return -ENOMEM;
	}

	tx_buffer_head->frag_ctl |= cpu_to_le16(FRAGCTL_NONFRAG);

	tx_bytes = tx_header_size + tx_body_size;

	memcpy(tx_context->hdr, skb->data, tx_body_size);

	hdr->duration_id = cpu_to_le16(duration_id);

	if (info->control.hw_key) {
		tx_key = info->control.hw_key;
		if (tx_key->keylen > 0)
			vnt_fill_txkey(tx_context, tx_buffer_head->tx_key,
				       tx_key, skb, tx_body_size, mic_hdr);
	}

	priv->seq_counter = (le16_to_cpu(hdr->seq_ctrl) &
						IEEE80211_SCTL_SEQ) >> 4;

	tx_buffer->tx_byte_count = cpu_to_le16(tx_bytes);
	tx_buffer->pkt_no = tx_context->pkt_no;
	tx_buffer->type = 0x00;

	tx_bytes += 4;

	tx_context->type = CONTEXT_DATA_PACKET;
	tx_context->buf_len = tx_bytes;

	spin_lock_irqsave(&priv->lock, flags);

	if (vnt_tx_context(priv, tx_context) != STATUS_PENDING) {
		spin_unlock_irqrestore(&priv->lock, flags);
		return -EIO;
	}

	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

static int vnt_beacon_xmit(struct vnt_private *priv, struct sk_buff *skb)
{
	struct vnt_beacon_buffer *beacon_buffer;
	struct vnt_tx_short_buf_head *short_head;
	struct ieee80211_tx_info *info;
	struct vnt_usb_send_context *context;
	struct ieee80211_mgmt *mgmt_hdr;
	unsigned long flags;
	u32 frame_size = skb->len + 4;
	u16 current_rate, count;

	spin_lock_irqsave(&priv->lock, flags);

	context = vnt_get_free_context(priv);
	if (!context) {
		dev_dbg(&priv->usb->dev, "%s No free context!\n", __func__);
		spin_unlock_irqrestore(&priv->lock, flags);
		return -ENOMEM;
	}

	context->skb = skb;

	spin_unlock_irqrestore(&priv->lock, flags);

	beacon_buffer = (struct vnt_beacon_buffer *)&context->data[0];
	short_head = &beacon_buffer->short_head;

	if (priv->bb_type == BB_TYPE_11A) {
		current_rate = RATE_6M;

		/* Get SignalField,ServiceField,Length */
		vnt_get_phy_field(priv, frame_size, current_rate,
				  PK_TYPE_11A, &short_head->ab);

		/* Get Duration and TimeStampOff */
		short_head->duration = vnt_get_duration_le(priv,
							   PK_TYPE_11A, false);
		short_head->time_stamp_off =
				vnt_time_stamp_off(priv, current_rate);
	} else {
		current_rate = RATE_1M;
		short_head->fifo_ctl |= cpu_to_le16(FIFOCTL_11B);

		/* Get SignalField,ServiceField,Length */
		vnt_get_phy_field(priv, frame_size, current_rate,
				  PK_TYPE_11B, &short_head->ab);

		/* Get Duration and TimeStampOff */
		short_head->duration = vnt_get_duration_le(priv,
						PK_TYPE_11B, false);
		short_head->time_stamp_off =
			vnt_time_stamp_off(priv, current_rate);
	}

	/* Generate Beacon Header */
	mgmt_hdr = &beacon_buffer->mgmt_hdr;
	memcpy(mgmt_hdr, skb->data, skb->len);

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

	count = sizeof(struct vnt_tx_short_buf_head) + skb->len;

	beacon_buffer->tx_byte_count = cpu_to_le16(count);
	beacon_buffer->pkt_no = context->pkt_no;
	beacon_buffer->type = 0x01;

	context->type = CONTEXT_BEACON_PACKET;
	context->buf_len = count + 4; /* USB header */

	spin_lock_irqsave(&priv->lock, flags);

	if (vnt_tx_context(priv, context) != STATUS_PENDING)
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
