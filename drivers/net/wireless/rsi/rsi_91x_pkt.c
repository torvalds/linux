/**
 * Copyright (c) 2014 Redpine Signals Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "rsi_mgmt.h"

/**
 * rsi_send_data_pkt() - This function sends the recieved data packet from
 *			 driver to device.
 * @common: Pointer to the driver private structure.
 * @skb: Pointer to the socket buffer structure.
 *
 * Return: status: 0 on success, -1 on failure.
 */
int rsi_send_data_pkt(struct rsi_common *common, struct sk_buff *skb)
{
	struct rsi_hw *adapter = common->priv;
	struct ieee80211_hdr *tmp_hdr = NULL;
	struct ieee80211_tx_info *info;
	struct skb_info *tx_params;
	struct ieee80211_bss_conf *bss = NULL;
	int status = -EINVAL;
	u8 ieee80211_size = MIN_802_11_HDR_LEN;
	u8 extnd_size = 0;
	__le16 *frame_desc;
	u16 seq_num = 0;

	info = IEEE80211_SKB_CB(skb);
	bss = &info->control.vif->bss_conf;
	tx_params = (struct skb_info *)info->driver_data;

	if (!bss->assoc)
		goto err;

	tmp_hdr = (struct ieee80211_hdr *)&skb->data[0];
	seq_num = (le16_to_cpu(tmp_hdr->seq_ctrl) >> 4);

	extnd_size = ((uintptr_t)skb->data & 0x3);

	if ((FRAME_DESC_SZ + extnd_size) > skb_headroom(skb)) {
		rsi_dbg(ERR_ZONE, "%s: Unable to send pkt\n", __func__);
		status = -ENOSPC;
		goto err;
	}

	skb_push(skb, (FRAME_DESC_SZ + extnd_size));
	frame_desc = (__le16 *)&skb->data[0];
	memset((u8 *)frame_desc, 0, FRAME_DESC_SZ);

	if (ieee80211_is_data_qos(tmp_hdr->frame_control)) {
		ieee80211_size += 2;
		frame_desc[6] |= cpu_to_le16(BIT(12));
	}

	if ((!(info->flags & IEEE80211_TX_INTFL_DONT_ENCRYPT)) &&
	    (common->secinfo.security_enable)) {
		if (rsi_is_cipher_wep(common))
			ieee80211_size += 4;
		else
			ieee80211_size += 8;
		frame_desc[6] |= cpu_to_le16(BIT(15));
	}

	frame_desc[0] = cpu_to_le16((skb->len - FRAME_DESC_SZ) |
				    (RSI_WIFI_DATA_Q << 12));
	frame_desc[2] = cpu_to_le16((extnd_size) | (ieee80211_size) << 8);

	if (common->min_rate != 0xffff) {
		/* Send fixed rate */
		frame_desc[3] = cpu_to_le16(RATE_INFO_ENABLE);
		frame_desc[4] = cpu_to_le16(common->min_rate);

		if (conf_is_ht40(&common->priv->hw->conf))
			frame_desc[5] = cpu_to_le16(FULL40M_ENABLE);

		if (common->vif_info[0].sgi) {
			if (common->min_rate & 0x100) /* Only MCS rates */
				frame_desc[4] |=
					cpu_to_le16(ENABLE_SHORTGI_RATE);
		}

	}

	frame_desc[6] |= cpu_to_le16(seq_num & 0xfff);
	frame_desc[7] = cpu_to_le16(((tx_params->tid & 0xf) << 4) |
				    (skb->priority & 0xf) |
				    (tx_params->sta_id << 8));

	status = adapter->host_intf_write_pkt(common->priv,
					      skb->data,
					      skb->len);
	if (status)
		rsi_dbg(ERR_ZONE, "%s: Failed to write pkt\n",
			__func__);

err:
	++common->tx_stats.total_tx_pkt_freed[skb->priority];
	rsi_indicate_tx_status(common->priv, skb, status);
	return status;
}

/**
 * rsi_send_mgmt_pkt() - This functions sends the received management packet
 *			 from driver to device.
 * @common: Pointer to the driver private structure.
 * @skb: Pointer to the socket buffer structure.
 *
 * Return: status: 0 on success, -1 on failure.
 */
int rsi_send_mgmt_pkt(struct rsi_common *common,
		      struct sk_buff *skb)
{
	struct rsi_hw *adapter = common->priv;
	struct ieee80211_hdr *wh = NULL;
	struct ieee80211_tx_info *info;
	struct ieee80211_bss_conf *bss = NULL;
	struct ieee80211_hw *hw = adapter->hw;
	struct ieee80211_conf *conf = &hw->conf;
	struct skb_info *tx_params;
	int status = -E2BIG;
	__le16 *msg = NULL;
	u8 extnd_size = 0;
	u8 vap_id = 0;

	info = IEEE80211_SKB_CB(skb);
	tx_params = (struct skb_info *)info->driver_data;
	extnd_size = ((uintptr_t)skb->data & 0x3);

	if (tx_params->flags & INTERNAL_MGMT_PKT) {
		if ((extnd_size) > skb_headroom(skb)) {
			rsi_dbg(ERR_ZONE, "%s: Unable to send pkt\n", __func__);
			dev_kfree_skb(skb);
			return -ENOSPC;
		}
		skb_push(skb, extnd_size);
		skb->data[extnd_size + 4] = extnd_size;
		status = adapter->host_intf_write_pkt(common->priv,
						      (u8 *)skb->data,
						      skb->len);
		if (status) {
			rsi_dbg(ERR_ZONE,
				"%s: Failed to write the packet\n", __func__);
		}
		dev_kfree_skb(skb);
		return status;
	}

	bss = &info->control.vif->bss_conf;
	wh = (struct ieee80211_hdr *)&skb->data[0];

	if (FRAME_DESC_SZ > skb_headroom(skb))
		goto err;

	skb_push(skb, FRAME_DESC_SZ);
	memset(skb->data, 0, FRAME_DESC_SZ);
	msg = (__le16 *)skb->data;

	if (skb->len > MAX_MGMT_PKT_SIZE) {
		rsi_dbg(INFO_ZONE, "%s: Dropping mgmt pkt > 512\n", __func__);
		goto err;
	}

	msg[0] = cpu_to_le16((skb->len - FRAME_DESC_SZ) |
			    (RSI_WIFI_MGMT_Q << 12));
	msg[1] = cpu_to_le16(TX_DOT11_MGMT);
	msg[2] = cpu_to_le16(MIN_802_11_HDR_LEN << 8);
	msg[3] = cpu_to_le16(RATE_INFO_ENABLE);
	msg[6] = cpu_to_le16(le16_to_cpu(wh->seq_ctrl) >> 4);

	if (wh->addr1[0] & BIT(0))
		msg[3] |= cpu_to_le16(RSI_BROADCAST_PKT);

	if (common->band == IEEE80211_BAND_2GHZ)
		msg[4] = cpu_to_le16(RSI_11B_MODE);
	else
		msg[4] = cpu_to_le16((RSI_RATE_6 & 0x0f) | RSI_11G_MODE);

	if (conf_is_ht40(conf)) {
		msg[4] = cpu_to_le16(0xB | RSI_11G_MODE);
		msg[5] = cpu_to_le16(0x6);
	}

	/* Indicate to firmware to give cfm */
	if ((skb->data[16] == IEEE80211_STYPE_PROBE_REQ) && (!bss->assoc)) {
		msg[1] |= cpu_to_le16(BIT(10));
		msg[7] = cpu_to_le16(PROBEREQ_CONFIRM);
		common->mgmt_q_block = true;
	}

	msg[7] |= cpu_to_le16(vap_id << 8);

	status = adapter->host_intf_write_pkt(common->priv,
					      (u8 *)msg,
					      skb->len);
	if (status)
		rsi_dbg(ERR_ZONE, "%s: Failed to write the packet\n", __func__);

err:
	rsi_indicate_tx_status(common->priv, skb, status);
	return status;
}
