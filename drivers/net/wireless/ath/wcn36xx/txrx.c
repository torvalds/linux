/*
 * Copyright (c) 2013 Eugene Krasnikov <k.eugene.e@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "txrx.h"

static inline int get_rssi0(struct wcn36xx_rx_bd *bd)
{
	return 100 - ((bd->phy_stat0 >> 24) & 0xff);
}

int wcn36xx_rx_skb(struct wcn36xx *wcn, struct sk_buff *skb)
{
	struct ieee80211_rx_status status;
	struct ieee80211_hdr *hdr;
	struct wcn36xx_rx_bd *bd;
	u16 fc, sn;

	/*
	 * All fields must be 0, otherwise it can lead to
	 * unexpected consequences.
	 */
	memset(&status, 0, sizeof(status));

	bd = (struct wcn36xx_rx_bd *)skb->data;
	buff_to_be((u32 *)bd, sizeof(*bd)/sizeof(u32));
	wcn36xx_dbg_dump(WCN36XX_DBG_RX_DUMP,
			 "BD   <<< ", (char *)bd,
			 sizeof(struct wcn36xx_rx_bd));

	skb_put(skb, bd->pdu.mpdu_header_off + bd->pdu.mpdu_len);
	skb_pull(skb, bd->pdu.mpdu_header_off);

	status.mactime = 10;
	status.freq = WCN36XX_CENTER_FREQ(wcn);
	status.band = WCN36XX_BAND(wcn);
	status.signal = -get_rssi0(bd);
	status.antenna = 1;
	status.rate_idx = 1;
	status.flag = 0;
	status.rx_flags = 0;
	status.flag |= RX_FLAG_IV_STRIPPED |
		       RX_FLAG_MMIC_STRIPPED |
		       RX_FLAG_DECRYPTED;

	wcn36xx_dbg(WCN36XX_DBG_RX, "status.flags=%llx\n", status.flag);

	memcpy(IEEE80211_SKB_RXCB(skb), &status, sizeof(status));

	hdr = (struct ieee80211_hdr *) skb->data;
	fc = __le16_to_cpu(hdr->frame_control);
	sn = IEEE80211_SEQ_TO_SN(__le16_to_cpu(hdr->seq_ctrl));

	if (ieee80211_is_beacon(hdr->frame_control)) {
		wcn36xx_dbg(WCN36XX_DBG_BEACON, "beacon skb %p len %d fc %04x sn %d\n",
			    skb, skb->len, fc, sn);
		wcn36xx_dbg_dump(WCN36XX_DBG_BEACON_DUMP, "SKB <<< ",
				 (char *)skb->data, skb->len);
	} else {
		wcn36xx_dbg(WCN36XX_DBG_RX, "rx skb %p len %d fc %04x sn %d\n",
			    skb, skb->len, fc, sn);
		wcn36xx_dbg_dump(WCN36XX_DBG_RX_DUMP, "SKB <<< ",
				 (char *)skb->data, skb->len);
	}

	ieee80211_rx_irqsafe(wcn->hw, skb);

	return 0;
}

static void wcn36xx_set_tx_pdu(struct wcn36xx_tx_bd *bd,
			       u32 mpdu_header_len,
			       u32 len,
			       u16 tid)
{
	bd->pdu.mpdu_header_len = mpdu_header_len;
	bd->pdu.mpdu_header_off = sizeof(*bd);
	bd->pdu.mpdu_data_off = bd->pdu.mpdu_header_len +
		bd->pdu.mpdu_header_off;
	bd->pdu.mpdu_len = len;
	bd->pdu.tid = tid;
	bd->pdu.bd_ssn = WCN36XX_TXBD_SSN_FILL_DPU_QOS;
}

static inline struct wcn36xx_vif *get_vif_by_addr(struct wcn36xx *wcn,
						  u8 *addr)
{
	struct wcn36xx_vif *vif_priv = NULL;
	struct ieee80211_vif *vif = NULL;
	list_for_each_entry(vif_priv, &wcn->vif_list, list) {
			vif = wcn36xx_priv_to_vif(vif_priv);
			if (memcmp(vif->addr, addr, ETH_ALEN) == 0)
				return vif_priv;
	}
	wcn36xx_warn("vif %pM not found\n", addr);
	return NULL;
}

static void wcn36xx_tx_start_ampdu(struct wcn36xx *wcn,
				   struct wcn36xx_sta *sta_priv,
				   struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct ieee80211_sta *sta;
	u8 *qc, tid;

	if (!conf_is_ht(&wcn->hw->conf))
		return;

	sta = wcn36xx_priv_to_sta(sta_priv);

	if (WARN_ON(!ieee80211_is_data_qos(hdr->frame_control)))
		return;

	if (skb_get_queue_mapping(skb) == IEEE80211_AC_VO)
		return;

	qc = ieee80211_get_qos_ctl(hdr);
	tid = qc[0] & IEEE80211_QOS_CTL_TID_MASK;

	spin_lock(&sta_priv->ampdu_lock);
	if (sta_priv->ampdu_state[tid] != WCN36XX_AMPDU_NONE)
		goto out_unlock;

	if (sta_priv->non_agg_frame_ct++ >= WCN36XX_AMPDU_START_THRESH) {
		sta_priv->ampdu_state[tid] = WCN36XX_AMPDU_START;
		sta_priv->non_agg_frame_ct = 0;
		ieee80211_start_tx_ba_session(sta, tid, 0);
	}
out_unlock:
	spin_unlock(&sta_priv->ampdu_lock);
}

static void wcn36xx_set_tx_data(struct wcn36xx_tx_bd *bd,
				struct wcn36xx *wcn,
				struct wcn36xx_vif **vif_priv,
				struct wcn36xx_sta *sta_priv,
				struct sk_buff *skb,
				bool bcast)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct ieee80211_vif *vif = NULL;
	struct wcn36xx_vif *__vif_priv = NULL;
	bool is_data_qos;

	bd->bd_rate = WCN36XX_BD_RATE_DATA;

	/*
	 * For not unicast frames mac80211 will not set sta pointer so use
	 * self_sta_index instead.
	 */
	if (sta_priv) {
		__vif_priv = sta_priv->vif;
		vif = wcn36xx_priv_to_vif(__vif_priv);

		bd->dpu_sign = sta_priv->ucast_dpu_sign;
		if (vif->type == NL80211_IFTYPE_STATION) {
			bd->sta_index = sta_priv->bss_sta_index;
			bd->dpu_desc_idx = sta_priv->bss_dpu_desc_index;
		} else if (vif->type == NL80211_IFTYPE_AP ||
			   vif->type == NL80211_IFTYPE_ADHOC ||
			   vif->type == NL80211_IFTYPE_MESH_POINT) {
			bd->sta_index = sta_priv->sta_index;
			bd->dpu_desc_idx = sta_priv->dpu_desc_index;
		}
	} else {
		__vif_priv = get_vif_by_addr(wcn, hdr->addr2);
		bd->sta_index = __vif_priv->self_sta_index;
		bd->dpu_desc_idx = __vif_priv->self_dpu_desc_index;
		bd->dpu_sign = __vif_priv->self_ucast_dpu_sign;
	}

	if (ieee80211_is_nullfunc(hdr->frame_control) ||
	   (sta_priv && !sta_priv->is_data_encrypted))
		bd->dpu_ne = 1;

	if (bcast) {
		bd->ub = 1;
		bd->ack_policy = 1;
	}
	*vif_priv = __vif_priv;

	is_data_qos = ieee80211_is_data_qos(hdr->frame_control);

	wcn36xx_set_tx_pdu(bd,
			   is_data_qos ?
			   sizeof(struct ieee80211_qos_hdr) :
			   sizeof(struct ieee80211_hdr_3addr),
			   skb->len, sta_priv ? sta_priv->tid : 0);

	if (sta_priv && is_data_qos)
		wcn36xx_tx_start_ampdu(wcn, sta_priv, skb);
}

static void wcn36xx_set_tx_mgmt(struct wcn36xx_tx_bd *bd,
				struct wcn36xx *wcn,
				struct wcn36xx_vif **vif_priv,
				struct sk_buff *skb,
				bool bcast)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct wcn36xx_vif *__vif_priv =
		get_vif_by_addr(wcn, hdr->addr2);
	bd->sta_index = __vif_priv->self_sta_index;
	bd->dpu_desc_idx = __vif_priv->self_dpu_desc_index;
	bd->dpu_ne = 1;

	/* default rate for unicast */
	if (ieee80211_is_mgmt(hdr->frame_control))
		bd->bd_rate = (WCN36XX_BAND(wcn) == NL80211_BAND_5GHZ) ?
			WCN36XX_BD_RATE_CTRL :
			WCN36XX_BD_RATE_MGMT;
	else if (ieee80211_is_ctl(hdr->frame_control))
		bd->bd_rate = WCN36XX_BD_RATE_CTRL;
	else
		wcn36xx_warn("frame control type unknown\n");

	/*
	 * In joining state trick hardware that probe is sent as
	 * unicast even if address is broadcast.
	 */
	if (__vif_priv->is_joining &&
	    ieee80211_is_probe_req(hdr->frame_control))
		bcast = false;

	if (bcast) {
		/* broadcast */
		bd->ub = 1;
		/* No ack needed not unicast */
		bd->ack_policy = 1;
		bd->queue_id = WCN36XX_TX_B_WQ_ID;
	} else
		bd->queue_id = WCN36XX_TX_U_WQ_ID;
	*vif_priv = __vif_priv;

	wcn36xx_set_tx_pdu(bd,
			   ieee80211_is_data_qos(hdr->frame_control) ?
			   sizeof(struct ieee80211_qos_hdr) :
			   sizeof(struct ieee80211_hdr_3addr),
			   skb->len, WCN36XX_TID);
}

int wcn36xx_start_tx(struct wcn36xx *wcn,
		     struct wcn36xx_sta *sta_priv,
		     struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct wcn36xx_vif *vif_priv = NULL;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	unsigned long flags;
	bool is_low = ieee80211_is_data(hdr->frame_control);
	bool bcast = is_broadcast_ether_addr(hdr->addr1) ||
		is_multicast_ether_addr(hdr->addr1);
	struct wcn36xx_tx_bd *bd = wcn36xx_dxe_get_next_bd(wcn, is_low);

	if (!bd) {
		/*
		 * TX DXE are used in pairs. One for the BD and one for the
		 * actual frame. The BD DXE's has a preallocated buffer while
		 * the skb ones does not. If this isn't true something is really
		 * wierd. TODO: Recover from this situation
		 */

		wcn36xx_err("bd address may not be NULL for BD DXE\n");
		return -EINVAL;
	}

	memset(bd, 0, sizeof(*bd));

	wcn36xx_dbg(WCN36XX_DBG_TX,
		    "tx skb %p len %d fc %04x sn %d %s %s\n",
		    skb, skb->len, __le16_to_cpu(hdr->frame_control),
		    IEEE80211_SEQ_TO_SN(__le16_to_cpu(hdr->seq_ctrl)),
		    is_low ? "low" : "high", bcast ? "bcast" : "ucast");

	wcn36xx_dbg_dump(WCN36XX_DBG_TX_DUMP, "", skb->data, skb->len);

	bd->dpu_rf = WCN36XX_BMU_WQ_TX;

	bd->tx_comp = !!(info->flags & IEEE80211_TX_CTL_REQ_TX_STATUS);
	if (bd->tx_comp) {
		wcn36xx_dbg(WCN36XX_DBG_DXE, "TX_ACK status requested\n");
		spin_lock_irqsave(&wcn->dxe_lock, flags);
		if (wcn->tx_ack_skb) {
			spin_unlock_irqrestore(&wcn->dxe_lock, flags);
			wcn36xx_warn("tx_ack_skb already set\n");
			return -EINVAL;
		}

		wcn->tx_ack_skb = skb;
		spin_unlock_irqrestore(&wcn->dxe_lock, flags);

		/* Only one at a time is supported by fw. Stop the TX queues
		 * until the ack status gets back.
		 *
		 * TODO: Add watchdog in case FW does not answer
		 */
		ieee80211_stop_queues(wcn->hw);
	}

	/* Data frames served first*/
	if (is_low)
		wcn36xx_set_tx_data(bd, wcn, &vif_priv, sta_priv, skb, bcast);
	else
		/* MGMT and CTRL frames are handeld here*/
		wcn36xx_set_tx_mgmt(bd, wcn, &vif_priv, skb, bcast);

	buff_to_be((u32 *)bd, sizeof(*bd)/sizeof(u32));
	bd->tx_bd_sign = 0xbdbdbdbd;

	return wcn36xx_dxe_tx_frame(wcn, vif_priv, skb, is_low);
}
