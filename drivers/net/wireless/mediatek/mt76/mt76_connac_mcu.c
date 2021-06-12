// SPDX-License-Identifier: ISC
/* Copyright (C) 2020 MediaTek Inc. */

#include "mt76_connac_mcu.h"

int mt76_connac_mcu_start_firmware(struct mt76_dev *dev, u32 addr, u32 option)
{
	struct {
		__le32 option;
		__le32 addr;
	} req = {
		.option = cpu_to_le32(option),
		.addr = cpu_to_le32(addr),
	};

	return mt76_mcu_send_msg(dev, MCU_CMD_FW_START_REQ, &req, sizeof(req),
				 true);
}
EXPORT_SYMBOL_GPL(mt76_connac_mcu_start_firmware);

int mt76_connac_mcu_patch_sem_ctrl(struct mt76_dev *dev, bool get)
{
	u32 op = get ? PATCH_SEM_GET : PATCH_SEM_RELEASE;
	struct {
		__le32 op;
	} req = {
		.op = cpu_to_le32(op),
	};

	return mt76_mcu_send_msg(dev, MCU_CMD_PATCH_SEM_CONTROL, &req,
				 sizeof(req), true);
}
EXPORT_SYMBOL_GPL(mt76_connac_mcu_patch_sem_ctrl);

int mt76_connac_mcu_start_patch(struct mt76_dev *dev)
{
	struct {
		u8 check_crc;
		u8 reserved[3];
	} req = {
		.check_crc = 0,
	};

	return mt76_mcu_send_msg(dev, MCU_CMD_PATCH_FINISH_REQ, &req,
				 sizeof(req), true);
}
EXPORT_SYMBOL_GPL(mt76_connac_mcu_start_patch);

#define MCU_PATCH_ADDRESS	0x200000

int mt76_connac_mcu_init_download(struct mt76_dev *dev, u32 addr, u32 len,
				  u32 mode)
{
	struct {
		__le32 addr;
		__le32 len;
		__le32 mode;
	} req = {
		.addr = cpu_to_le32(addr),
		.len = cpu_to_le32(len),
		.mode = cpu_to_le32(mode),
	};
	int cmd;

	if (is_mt7921(dev) &&
	    (req.addr == cpu_to_le32(MCU_PATCH_ADDRESS) || addr == 0x900000))
		cmd = MCU_CMD_PATCH_START_REQ;
	else
		cmd = MCU_CMD_TARGET_ADDRESS_LEN_REQ;

	return mt76_mcu_send_msg(dev, cmd, &req, sizeof(req), true);
}
EXPORT_SYMBOL_GPL(mt76_connac_mcu_init_download);

int mt76_connac_mcu_set_channel_domain(struct mt76_phy *phy)
{
	struct mt76_dev *dev = phy->dev;
	struct mt76_connac_mcu_channel_domain {
		u8 alpha2[4]; /* regulatory_request.alpha2 */
		u8 bw_2g; /* BW_20_40M		0
			   * BW_20M		1
			   * BW_20_40_80M	2
			   * BW_20_40_80_160M	3
			   * BW_20_40_80_8080M	4
			   */
		u8 bw_5g;
		__le16 pad;
		u8 n_2ch;
		u8 n_5ch;
		__le16 pad2;
	} __packed hdr = {
		.bw_2g = 0,
		.bw_5g = 3,
	};
	struct mt76_connac_mcu_chan {
		__le16 hw_value;
		__le16 pad;
		__le32 flags;
	} __packed channel;
	int len, i, n_max_channels, n_2ch = 0, n_5ch = 0;
	struct ieee80211_channel *chan;
	struct sk_buff *skb;

	n_max_channels = phy->sband_2g.sband.n_channels +
			 phy->sband_5g.sband.n_channels;
	len = sizeof(hdr) + n_max_channels * sizeof(channel);

	skb = mt76_mcu_msg_alloc(dev, NULL, len);
	if (!skb)
		return -ENOMEM;

	skb_reserve(skb, sizeof(hdr));

	for (i = 0; i < phy->sband_2g.sband.n_channels; i++) {
		chan = &phy->sband_2g.sband.channels[i];
		if (chan->flags & IEEE80211_CHAN_DISABLED)
			continue;

		channel.hw_value = cpu_to_le16(chan->hw_value);
		channel.flags = cpu_to_le32(chan->flags);
		channel.pad = 0;

		skb_put_data(skb, &channel, sizeof(channel));
		n_2ch++;
	}
	for (i = 0; i < phy->sband_5g.sband.n_channels; i++) {
		chan = &phy->sband_5g.sband.channels[i];
		if (chan->flags & IEEE80211_CHAN_DISABLED)
			continue;

		channel.hw_value = cpu_to_le16(chan->hw_value);
		channel.flags = cpu_to_le32(chan->flags);
		channel.pad = 0;

		skb_put_data(skb, &channel, sizeof(channel));
		n_5ch++;
	}

	BUILD_BUG_ON(sizeof(dev->alpha2) > sizeof(hdr.alpha2));
	memcpy(hdr.alpha2, dev->alpha2, sizeof(dev->alpha2));
	hdr.n_2ch = n_2ch;
	hdr.n_5ch = n_5ch;

	memcpy(__skb_push(skb, sizeof(hdr)), &hdr, sizeof(hdr));

	return mt76_mcu_skb_send_msg(dev, skb, MCU_CMD_SET_CHAN_DOMAIN, false);
}
EXPORT_SYMBOL_GPL(mt76_connac_mcu_set_channel_domain);

int mt76_connac_mcu_set_mac_enable(struct mt76_dev *dev, int band, bool enable,
				   bool hdr_trans)
{
	struct {
		u8 enable;
		u8 band;
		u8 rsv[2];
	} __packed req_mac = {
		.enable = enable,
		.band = band,
	};

	return mt76_mcu_send_msg(dev, MCU_EXT_CMD_MAC_INIT_CTRL, &req_mac,
				 sizeof(req_mac), true);
}
EXPORT_SYMBOL_GPL(mt76_connac_mcu_set_mac_enable);

int mt76_connac_mcu_set_vif_ps(struct mt76_dev *dev, struct ieee80211_vif *vif)
{
	struct mt76_vif *mvif = (struct mt76_vif *)vif->drv_priv;
	struct {
		u8 bss_idx;
		u8 ps_state; /* 0: device awake
			      * 1: static power save
			      * 2: dynamic power saving
			      */
	} req = {
		.bss_idx = mvif->idx,
		.ps_state = vif->bss_conf.ps ? 2 : 0,
	};

	if (vif->type != NL80211_IFTYPE_STATION)
		return -EOPNOTSUPP;

	return mt76_mcu_send_msg(dev, MCU_CMD_SET_PS_PROFILE, &req,
				 sizeof(req), false);
}
EXPORT_SYMBOL_GPL(mt76_connac_mcu_set_vif_ps);

int mt76_connac_mcu_set_rts_thresh(struct mt76_dev *dev, u32 val, u8 band)
{
	struct {
		u8 prot_idx;
		u8 band;
		u8 rsv[2];
		__le32 len_thresh;
		__le32 pkt_thresh;
	} __packed req = {
		.prot_idx = 1,
		.band = band,
		.len_thresh = cpu_to_le32(val),
		.pkt_thresh = cpu_to_le32(0x2),
	};

	return mt76_mcu_send_msg(dev, MCU_EXT_CMD_PROTECT_CTRL, &req,
				 sizeof(req), true);
}
EXPORT_SYMBOL_GPL(mt76_connac_mcu_set_rts_thresh);

void mt76_connac_mcu_beacon_loss_iter(void *priv, u8 *mac,
				      struct ieee80211_vif *vif)
{
	struct mt76_vif *mvif = (struct mt76_vif *)vif->drv_priv;
	struct mt76_connac_beacon_loss_event *event = priv;

	if (mvif->idx != event->bss_idx)
		return;

	if (!(vif->driver_flags & IEEE80211_VIF_BEACON_FILTER))
		return;

	ieee80211_beacon_loss(vif);
}
EXPORT_SYMBOL_GPL(mt76_connac_mcu_beacon_loss_iter);

struct tlv *
mt76_connac_mcu_add_nested_tlv(struct sk_buff *skb, int tag, int len,
			       void *sta_ntlv, void *sta_wtbl)
{
	struct sta_ntlv_hdr *ntlv_hdr = sta_ntlv;
	struct tlv *sta_hdr = sta_wtbl;
	struct tlv *ptlv, tlv = {
		.tag = cpu_to_le16(tag),
		.len = cpu_to_le16(len),
	};
	u16 ntlv;

	ptlv = skb_put(skb, len);
	memcpy(ptlv, &tlv, sizeof(tlv));

	ntlv = le16_to_cpu(ntlv_hdr->tlv_num);
	ntlv_hdr->tlv_num = cpu_to_le16(ntlv + 1);

	if (sta_hdr) {
		u16 size = le16_to_cpu(sta_hdr->len);

		sta_hdr->len = cpu_to_le16(size + len);
	}

	return ptlv;
}
EXPORT_SYMBOL_GPL(mt76_connac_mcu_add_nested_tlv);

struct sk_buff *
mt76_connac_mcu_alloc_sta_req(struct mt76_dev *dev, struct mt76_vif *mvif,
			      struct mt76_wcid *wcid)
{
	struct sta_req_hdr hdr = {
		.bss_idx = mvif->idx,
		.muar_idx = wcid ? mvif->omac_idx : 0,
		.is_tlv_append = 1,
	};
	struct sk_buff *skb;

	mt76_connac_mcu_get_wlan_idx(dev, wcid, &hdr.wlan_idx_lo,
				     &hdr.wlan_idx_hi);
	skb = mt76_mcu_msg_alloc(dev, NULL, MT76_CONNAC_STA_UPDATE_MAX_SIZE);
	if (!skb)
		return ERR_PTR(-ENOMEM);

	skb_put_data(skb, &hdr, sizeof(hdr));

	return skb;
}
EXPORT_SYMBOL_GPL(mt76_connac_mcu_alloc_sta_req);

struct wtbl_req_hdr *
mt76_connac_mcu_alloc_wtbl_req(struct mt76_dev *dev, struct mt76_wcid *wcid,
			       int cmd, void *sta_wtbl, struct sk_buff **skb)
{
	struct tlv *sta_hdr = sta_wtbl;
	struct wtbl_req_hdr hdr = {
		.operation = cmd,
	};
	struct sk_buff *nskb = *skb;

	mt76_connac_mcu_get_wlan_idx(dev, wcid, &hdr.wlan_idx_lo,
				     &hdr.wlan_idx_hi);
	if (!nskb) {
		nskb = mt76_mcu_msg_alloc(dev, NULL,
					  MT76_CONNAC_WTBL_UPDATE_MAX_SIZE);
		if (!nskb)
			return ERR_PTR(-ENOMEM);

		*skb = nskb;
	}

	if (sta_hdr)
		sta_hdr->len = cpu_to_le16(sizeof(hdr));

	return skb_put_data(nskb, &hdr, sizeof(hdr));
}
EXPORT_SYMBOL_GPL(mt76_connac_mcu_alloc_wtbl_req);

void mt76_connac_mcu_sta_basic_tlv(struct sk_buff *skb,
				   struct ieee80211_vif *vif,
				   struct ieee80211_sta *sta,
				   bool enable, bool newly)
{
	struct sta_rec_basic *basic;
	struct tlv *tlv;
	int conn_type;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_BASIC, sizeof(*basic));

	basic = (struct sta_rec_basic *)tlv;
	basic->extra_info = cpu_to_le16(EXTRA_INFO_VER);

	if (enable) {
		if (newly)
			basic->extra_info |= cpu_to_le16(EXTRA_INFO_NEW);
		basic->conn_state = CONN_STATE_PORT_SECURE;
	} else {
		basic->conn_state = CONN_STATE_DISCONNECT;
	}

	if (!sta) {
		basic->conn_type = cpu_to_le32(CONNECTION_INFRA_BC);
		eth_broadcast_addr(basic->peer_addr);
		return;
	}

	switch (vif->type) {
	case NL80211_IFTYPE_MESH_POINT:
	case NL80211_IFTYPE_AP:
		if (vif->p2p)
			conn_type = CONNECTION_P2P_GC;
		else
			conn_type = CONNECTION_INFRA_STA;
		basic->conn_type = cpu_to_le32(conn_type);
		basic->aid = cpu_to_le16(sta->aid);
		break;
	case NL80211_IFTYPE_STATION:
		if (vif->p2p)
			conn_type = CONNECTION_P2P_GO;
		else
			conn_type = CONNECTION_INFRA_AP;
		basic->conn_type = cpu_to_le32(conn_type);
		basic->aid = cpu_to_le16(vif->bss_conf.aid);
		break;
	case NL80211_IFTYPE_ADHOC:
		basic->conn_type = cpu_to_le32(CONNECTION_IBSS_ADHOC);
		basic->aid = cpu_to_le16(sta->aid);
		break;
	default:
		WARN_ON(1);
		break;
	}

	memcpy(basic->peer_addr, sta->addr, ETH_ALEN);
	basic->qos = sta->wme;
}
EXPORT_SYMBOL_GPL(mt76_connac_mcu_sta_basic_tlv);

static void
mt76_connac_mcu_sta_uapsd(struct sk_buff *skb, struct ieee80211_vif *vif,
			  struct ieee80211_sta *sta)
{
	struct sta_rec_uapsd *uapsd;
	struct tlv *tlv;

	if (vif->type != NL80211_IFTYPE_AP || !sta->wme)
		return;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_APPS, sizeof(*uapsd));
	uapsd = (struct sta_rec_uapsd *)tlv;

	if (sta->uapsd_queues & IEEE80211_WMM_IE_STA_QOSINFO_AC_VO) {
		uapsd->dac_map |= BIT(3);
		uapsd->tac_map |= BIT(3);
	}
	if (sta->uapsd_queues & IEEE80211_WMM_IE_STA_QOSINFO_AC_VI) {
		uapsd->dac_map |= BIT(2);
		uapsd->tac_map |= BIT(2);
	}
	if (sta->uapsd_queues & IEEE80211_WMM_IE_STA_QOSINFO_AC_BE) {
		uapsd->dac_map |= BIT(1);
		uapsd->tac_map |= BIT(1);
	}
	if (sta->uapsd_queues & IEEE80211_WMM_IE_STA_QOSINFO_AC_BK) {
		uapsd->dac_map |= BIT(0);
		uapsd->tac_map |= BIT(0);
	}
	uapsd->max_sp = sta->max_sp;
}

void mt76_connac_mcu_wtbl_hdr_trans_tlv(struct sk_buff *skb,
					struct ieee80211_vif *vif,
					struct mt76_wcid *wcid,
					void *sta_wtbl, void *wtbl_tlv)
{
	struct wtbl_hdr_trans *htr;
	struct tlv *tlv;

	tlv = mt76_connac_mcu_add_nested_tlv(skb, WTBL_HDR_TRANS,
					     sizeof(*htr),
					     wtbl_tlv, sta_wtbl);
	htr = (struct wtbl_hdr_trans *)tlv;
	htr->no_rx_trans = !test_bit(MT_WCID_FLAG_HDR_TRANS, &wcid->flags);

	if (vif->type == NL80211_IFTYPE_STATION)
		htr->to_ds = true;
	else
		htr->from_ds = true;

	if (test_bit(MT_WCID_FLAG_4ADDR, &wcid->flags)) {
		htr->to_ds = true;
		htr->from_ds = true;
	}
}
EXPORT_SYMBOL_GPL(mt76_connac_mcu_wtbl_hdr_trans_tlv);

int mt76_connac_mcu_sta_update_hdr_trans(struct mt76_dev *dev,
					 struct ieee80211_vif *vif,
					 struct mt76_wcid *wcid, int cmd)
{
	struct mt76_vif *mvif = (struct mt76_vif *)vif->drv_priv;
	struct wtbl_req_hdr *wtbl_hdr;
	struct tlv *sta_wtbl;
	struct sk_buff *skb;

	skb = mt76_connac_mcu_alloc_sta_req(dev, mvif, wcid);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	sta_wtbl = mt76_connac_mcu_add_tlv(skb, STA_REC_WTBL,
					   sizeof(struct tlv));

	wtbl_hdr = mt76_connac_mcu_alloc_wtbl_req(dev, wcid, WTBL_SET,
						  sta_wtbl, &skb);
	if (IS_ERR(wtbl_hdr))
		return PTR_ERR(wtbl_hdr);

	mt76_connac_mcu_wtbl_hdr_trans_tlv(skb, vif, wcid, sta_wtbl, wtbl_hdr);

	return mt76_mcu_skb_send_msg(dev, skb, cmd, true);
}
EXPORT_SYMBOL_GPL(mt76_connac_mcu_sta_update_hdr_trans);

void mt76_connac_mcu_wtbl_generic_tlv(struct mt76_dev *dev,
				      struct sk_buff *skb,
				      struct ieee80211_vif *vif,
				      struct ieee80211_sta *sta,
				      void *sta_wtbl, void *wtbl_tlv)
{
	struct mt76_vif *mvif = (struct mt76_vif *)vif->drv_priv;
	struct wtbl_generic *generic;
	struct wtbl_rx *rx;
	struct wtbl_spe *spe;
	struct tlv *tlv;

	tlv = mt76_connac_mcu_add_nested_tlv(skb, WTBL_GENERIC,
					     sizeof(*generic),
					     wtbl_tlv, sta_wtbl);

	generic = (struct wtbl_generic *)tlv;

	if (sta) {
		if (vif->type == NL80211_IFTYPE_STATION)
			generic->partial_aid = cpu_to_le16(vif->bss_conf.aid);
		else
			generic->partial_aid = cpu_to_le16(sta->aid);
		memcpy(generic->peer_addr, sta->addr, ETH_ALEN);
		generic->muar_idx = mvif->omac_idx;
		generic->qos = sta->wme;
	} else {
		if (is_mt7921(dev) &&
		    vif->type == NL80211_IFTYPE_STATION)
			memcpy(generic->peer_addr, vif->bss_conf.bssid,
			       ETH_ALEN);
		else
			eth_broadcast_addr(generic->peer_addr);

		generic->muar_idx = 0xe;
	}

	tlv = mt76_connac_mcu_add_nested_tlv(skb, WTBL_RX, sizeof(*rx),
					     wtbl_tlv, sta_wtbl);

	rx = (struct wtbl_rx *)tlv;
	rx->rca1 = sta ? vif->type != NL80211_IFTYPE_AP : 1;
	rx->rca2 = 1;
	rx->rv = 1;

	if (is_mt7921(dev))
		return;

	tlv = mt76_connac_mcu_add_nested_tlv(skb, WTBL_SPE, sizeof(*spe),
					     wtbl_tlv, sta_wtbl);
	spe = (struct wtbl_spe *)tlv;
	spe->spe_idx = 24;
}
EXPORT_SYMBOL_GPL(mt76_connac_mcu_wtbl_generic_tlv);

static void
mt76_connac_mcu_sta_amsdu_tlv(struct sk_buff *skb, struct ieee80211_sta *sta,
			      struct ieee80211_vif *vif)
{
	struct mt76_wcid *wcid = (struct mt76_wcid *)sta->drv_priv;
	struct sta_rec_amsdu *amsdu;
	struct tlv *tlv;

	if (vif->type != NL80211_IFTYPE_AP &&
	    vif->type != NL80211_IFTYPE_STATION)
		return;

	if (!sta->max_amsdu_len)
		return;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_HW_AMSDU, sizeof(*amsdu));
	amsdu = (struct sta_rec_amsdu *)tlv;
	amsdu->max_amsdu_num = 8;
	amsdu->amsdu_en = true;
	amsdu->max_mpdu_size = sta->max_amsdu_len >=
			       IEEE80211_MAX_MPDU_LEN_VHT_7991;

	wcid->amsdu = true;
}

#define HE_PHY(p, c)	u8_get_bits(c, IEEE80211_HE_PHY_##p)
#define HE_MAC(m, c)	u8_get_bits(c, IEEE80211_HE_MAC_##m)
static void
mt76_connac_mcu_sta_he_tlv(struct sk_buff *skb, struct ieee80211_sta *sta)
{
	struct ieee80211_sta_he_cap *he_cap = &sta->he_cap;
	struct ieee80211_he_cap_elem *elem = &he_cap->he_cap_elem;
	struct sta_rec_he *he;
	struct tlv *tlv;
	u32 cap = 0;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_HE, sizeof(*he));

	he = (struct sta_rec_he *)tlv;

	if (elem->mac_cap_info[0] & IEEE80211_HE_MAC_CAP0_HTC_HE)
		cap |= STA_REC_HE_CAP_HTC;

	if (elem->mac_cap_info[2] & IEEE80211_HE_MAC_CAP2_BSR)
		cap |= STA_REC_HE_CAP_BSR;

	if (elem->mac_cap_info[3] & IEEE80211_HE_MAC_CAP3_OMI_CONTROL)
		cap |= STA_REC_HE_CAP_OM;

	if (elem->mac_cap_info[4] & IEEE80211_HE_MAC_CAP4_AMSDU_IN_AMPDU)
		cap |= STA_REC_HE_CAP_AMSDU_IN_AMPDU;

	if (elem->mac_cap_info[4] & IEEE80211_HE_MAC_CAP4_BQR)
		cap |= STA_REC_HE_CAP_BQR;

	if (elem->phy_cap_info[0] &
	    (IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_RU_MAPPING_IN_2G |
	     IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_RU_MAPPING_IN_5G))
		cap |= STA_REC_HE_CAP_BW20_RU242_SUPPORT;

	if (elem->phy_cap_info[1] &
	    IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD)
		cap |= STA_REC_HE_CAP_LDPC;

	if (elem->phy_cap_info[1] &
	    IEEE80211_HE_PHY_CAP1_HE_LTF_AND_GI_FOR_HE_PPDUS_0_8US)
		cap |= STA_REC_HE_CAP_SU_PPDU_1LTF_8US_GI;

	if (elem->phy_cap_info[2] &
	    IEEE80211_HE_PHY_CAP2_NDP_4x_LTF_AND_3_2US)
		cap |= STA_REC_HE_CAP_NDP_4LTF_3DOT2MS_GI;

	if (elem->phy_cap_info[2] &
	    IEEE80211_HE_PHY_CAP2_STBC_TX_UNDER_80MHZ)
		cap |= STA_REC_HE_CAP_LE_EQ_80M_TX_STBC;

	if (elem->phy_cap_info[2] &
	    IEEE80211_HE_PHY_CAP2_STBC_RX_UNDER_80MHZ)
		cap |= STA_REC_HE_CAP_LE_EQ_80M_RX_STBC;

	if (elem->phy_cap_info[6] &
	    IEEE80211_HE_PHY_CAP6_PARTIAL_BW_EXT_RANGE)
		cap |= STA_REC_HE_CAP_PARTIAL_BW_EXT_RANGE;

	if (elem->phy_cap_info[7] &
	    IEEE80211_HE_PHY_CAP7_HE_SU_MU_PPDU_4XLTF_AND_08_US_GI)
		cap |= STA_REC_HE_CAP_SU_MU_PPDU_4LTF_8US_GI;

	if (elem->phy_cap_info[7] &
	    IEEE80211_HE_PHY_CAP7_STBC_TX_ABOVE_80MHZ)
		cap |= STA_REC_HE_CAP_GT_80M_TX_STBC;

	if (elem->phy_cap_info[7] &
	    IEEE80211_HE_PHY_CAP7_STBC_RX_ABOVE_80MHZ)
		cap |= STA_REC_HE_CAP_GT_80M_RX_STBC;

	if (elem->phy_cap_info[8] &
	    IEEE80211_HE_PHY_CAP8_HE_ER_SU_PPDU_4XLTF_AND_08_US_GI)
		cap |= STA_REC_HE_CAP_ER_SU_PPDU_4LTF_8US_GI;

	if (elem->phy_cap_info[8] &
	    IEEE80211_HE_PHY_CAP8_HE_ER_SU_1XLTF_AND_08_US_GI)
		cap |= STA_REC_HE_CAP_ER_SU_PPDU_1LTF_8US_GI;

	if (elem->phy_cap_info[9] &
	    IEEE80211_HE_PHY_CAP9_NON_TRIGGERED_CQI_FEEDBACK)
		cap |= STA_REC_HE_CAP_TRIG_CQI_FK;

	if (elem->phy_cap_info[9] &
	    IEEE80211_HE_PHY_CAP9_TX_1024_QAM_LESS_THAN_242_TONE_RU)
		cap |= STA_REC_HE_CAP_TX_1024QAM_UNDER_RU242;

	if (elem->phy_cap_info[9] &
	    IEEE80211_HE_PHY_CAP9_RX_1024_QAM_LESS_THAN_242_TONE_RU)
		cap |= STA_REC_HE_CAP_RX_1024QAM_UNDER_RU242;

	he->he_cap = cpu_to_le32(cap);

	switch (sta->bandwidth) {
	case IEEE80211_STA_RX_BW_160:
		if (elem->phy_cap_info[0] &
		    IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_80PLUS80_MHZ_IN_5G)
			he->max_nss_mcs[CMD_HE_MCS_BW8080] =
				he_cap->he_mcs_nss_supp.rx_mcs_80p80;

		he->max_nss_mcs[CMD_HE_MCS_BW160] =
				he_cap->he_mcs_nss_supp.rx_mcs_160;
		fallthrough;
	default:
		he->max_nss_mcs[CMD_HE_MCS_BW80] =
				he_cap->he_mcs_nss_supp.rx_mcs_80;
		break;
	}

	he->t_frame_dur =
		HE_MAC(CAP1_TF_MAC_PAD_DUR_MASK, elem->mac_cap_info[1]);
	he->max_ampdu_exp =
		HE_MAC(CAP3_MAX_AMPDU_LEN_EXP_MASK, elem->mac_cap_info[3]);

	he->bw_set =
		HE_PHY(CAP0_CHANNEL_WIDTH_SET_MASK, elem->phy_cap_info[0]);
	he->device_class =
		HE_PHY(CAP1_DEVICE_CLASS_A, elem->phy_cap_info[1]);
	he->punc_pream_rx =
		HE_PHY(CAP1_PREAMBLE_PUNC_RX_MASK, elem->phy_cap_info[1]);

	he->dcm_tx_mode =
		HE_PHY(CAP3_DCM_MAX_CONST_TX_MASK, elem->phy_cap_info[3]);
	he->dcm_tx_max_nss =
		HE_PHY(CAP3_DCM_MAX_TX_NSS_2, elem->phy_cap_info[3]);
	he->dcm_rx_mode =
		HE_PHY(CAP3_DCM_MAX_CONST_RX_MASK, elem->phy_cap_info[3]);
	he->dcm_rx_max_nss =
		HE_PHY(CAP3_DCM_MAX_RX_NSS_2, elem->phy_cap_info[3]);
	he->dcm_rx_max_nss =
		HE_PHY(CAP8_DCM_MAX_RU_MASK, elem->phy_cap_info[8]);

	he->pkt_ext = 2;
}

static u8
mt76_connac_get_phy_mode_v2(struct mt76_phy *mphy, struct ieee80211_vif *vif,
			    enum nl80211_band band, struct ieee80211_sta *sta)
{
	struct ieee80211_sta_ht_cap *ht_cap;
	struct ieee80211_sta_vht_cap *vht_cap;
	const struct ieee80211_sta_he_cap *he_cap;
	u8 mode = 0;

	if (sta) {
		ht_cap = &sta->ht_cap;
		vht_cap = &sta->vht_cap;
		he_cap = &sta->he_cap;
	} else {
		struct ieee80211_supported_band *sband;

		sband = mphy->hw->wiphy->bands[band];
		ht_cap = &sband->ht_cap;
		vht_cap = &sband->vht_cap;
		he_cap = ieee80211_get_he_iftype_cap(sband, vif->type);
	}

	if (band == NL80211_BAND_2GHZ) {
		mode |= PHY_TYPE_BIT_HR_DSSS | PHY_TYPE_BIT_ERP;

		if (ht_cap->ht_supported)
			mode |= PHY_TYPE_BIT_HT;

		if (he_cap->has_he)
			mode |= PHY_TYPE_BIT_HE;
	} else if (band == NL80211_BAND_5GHZ) {
		mode |= PHY_TYPE_BIT_OFDM;

		if (ht_cap->ht_supported)
			mode |= PHY_TYPE_BIT_HT;

		if (vht_cap->vht_supported)
			mode |= PHY_TYPE_BIT_VHT;

		if (he_cap->has_he)
			mode |= PHY_TYPE_BIT_HE;
	}

	return mode;
}

void mt76_connac_mcu_sta_tlv(struct mt76_phy *mphy, struct sk_buff *skb,
			     struct ieee80211_sta *sta,
			     struct ieee80211_vif *vif,
			     u8 rcpi, u8 sta_state)
{
	struct cfg80211_chan_def *chandef = &mphy->chandef;
	enum nl80211_band band = chandef->chan->band;
	struct mt76_dev *dev = mphy->dev;
	struct sta_rec_ra_info *ra_info;
	struct sta_rec_state *state;
	struct sta_rec_phy *phy;
	struct tlv *tlv;

	/* starec ht */
	if (sta->ht_cap.ht_supported) {
		struct sta_rec_ht *ht;

		tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_HT, sizeof(*ht));
		ht = (struct sta_rec_ht *)tlv;
		ht->ht_cap = cpu_to_le16(sta->ht_cap.cap);
	}

	/* starec vht */
	if (sta->vht_cap.vht_supported) {
		struct sta_rec_vht *vht;
		int len;

		len = is_mt7921(dev) ? sizeof(*vht) : sizeof(*vht) - 4;
		tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_VHT, len);
		vht = (struct sta_rec_vht *)tlv;
		vht->vht_cap = cpu_to_le32(sta->vht_cap.cap);
		vht->vht_rx_mcs_map = sta->vht_cap.vht_mcs.rx_mcs_map;
		vht->vht_tx_mcs_map = sta->vht_cap.vht_mcs.tx_mcs_map;
	}

	/* starec uapsd */
	mt76_connac_mcu_sta_uapsd(skb, vif, sta);

	if (!is_mt7921(dev))
		return;

	if (sta->ht_cap.ht_supported)
		mt76_connac_mcu_sta_amsdu_tlv(skb, sta, vif);

	/* starec he */
	if (sta->he_cap.has_he)
		mt76_connac_mcu_sta_he_tlv(skb, sta);

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_PHY, sizeof(*phy));
	phy = (struct sta_rec_phy *)tlv;
	phy->phy_type = mt76_connac_get_phy_mode_v2(mphy, vif, band, sta);
	phy->basic_rate = cpu_to_le16((u16)vif->bss_conf.basic_rates);
	phy->rcpi = rcpi;
	phy->ampdu = FIELD_PREP(IEEE80211_HT_AMPDU_PARM_FACTOR,
				sta->ht_cap.ampdu_factor) |
		     FIELD_PREP(IEEE80211_HT_AMPDU_PARM_DENSITY,
				sta->ht_cap.ampdu_density);

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_RA, sizeof(*ra_info));
	ra_info = (struct sta_rec_ra_info *)tlv;
	ra_info->legacy = cpu_to_le16((u16)sta->supp_rates[band]);

	if (sta->ht_cap.ht_supported)
		memcpy(ra_info->rx_mcs_bitmask, sta->ht_cap.mcs.rx_mask,
		       HT_MCS_MASK_NUM);

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_STATE, sizeof(*state));
	state = (struct sta_rec_state *)tlv;
	state->state = sta_state;

	if (sta->vht_cap.vht_supported) {
		state->vht_opmode = sta->bandwidth;
		state->vht_opmode |= (sta->rx_nss - 1) <<
			IEEE80211_OPMODE_NOTIF_RX_NSS_SHIFT;
	}
}
EXPORT_SYMBOL_GPL(mt76_connac_mcu_sta_tlv);

static void
mt76_connac_mcu_wtbl_smps_tlv(struct sk_buff *skb, struct ieee80211_sta *sta,
			      void *sta_wtbl, void *wtbl_tlv)
{
	struct wtbl_smps *smps;
	struct tlv *tlv;

	tlv = mt76_connac_mcu_add_nested_tlv(skb, WTBL_SMPS, sizeof(*smps),
					     wtbl_tlv, sta_wtbl);
	smps = (struct wtbl_smps *)tlv;

	if (sta->smps_mode == IEEE80211_SMPS_DYNAMIC)
		smps->smps = true;
}

void mt76_connac_mcu_wtbl_ht_tlv(struct mt76_dev *dev, struct sk_buff *skb,
				 struct ieee80211_sta *sta, void *sta_wtbl,
				 void *wtbl_tlv)
{
	struct wtbl_ht *ht = NULL;
	struct tlv *tlv;
	u32 flags = 0;

	if (sta->ht_cap.ht_supported) {
		tlv = mt76_connac_mcu_add_nested_tlv(skb, WTBL_HT, sizeof(*ht),
						     wtbl_tlv, sta_wtbl);
		ht = (struct wtbl_ht *)tlv;
		ht->ldpc = !!(sta->ht_cap.cap & IEEE80211_HT_CAP_LDPC_CODING);
		ht->af = sta->ht_cap.ampdu_factor;
		ht->mm = sta->ht_cap.ampdu_density;
		ht->ht = true;
	}

	if (sta->vht_cap.vht_supported) {
		struct wtbl_vht *vht;
		u8 af;

		tlv = mt76_connac_mcu_add_nested_tlv(skb, WTBL_VHT,
						     sizeof(*vht), wtbl_tlv,
						     sta_wtbl);
		vht = (struct wtbl_vht *)tlv;
		vht->ldpc = !!(sta->vht_cap.cap & IEEE80211_VHT_CAP_RXLDPC);
		vht->vht = true;

		af = FIELD_GET(IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK,
			       sta->vht_cap.cap);
		if (ht)
			ht->af = max(ht->af, af);
	}

	mt76_connac_mcu_wtbl_smps_tlv(skb, sta, sta_wtbl, wtbl_tlv);

	if (!is_mt7921(dev) && sta->ht_cap.ht_supported) {
		/* sgi */
		u32 msk = MT_WTBL_W5_SHORT_GI_20 | MT_WTBL_W5_SHORT_GI_40 |
			  MT_WTBL_W5_SHORT_GI_80 | MT_WTBL_W5_SHORT_GI_160;
		struct wtbl_raw *raw;

		tlv = mt76_connac_mcu_add_nested_tlv(skb, WTBL_RAW_DATA,
						     sizeof(*raw), wtbl_tlv,
						     sta_wtbl);

		if (sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_20)
			flags |= MT_WTBL_W5_SHORT_GI_20;
		if (sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_40)
			flags |= MT_WTBL_W5_SHORT_GI_40;

		if (sta->vht_cap.vht_supported) {
			if (sta->vht_cap.cap & IEEE80211_VHT_CAP_SHORT_GI_80)
				flags |= MT_WTBL_W5_SHORT_GI_80;
			if (sta->vht_cap.cap & IEEE80211_VHT_CAP_SHORT_GI_160)
				flags |= MT_WTBL_W5_SHORT_GI_160;
		}
		raw = (struct wtbl_raw *)tlv;
		raw->val = cpu_to_le32(flags);
		raw->msk = cpu_to_le32(~msk);
		raw->wtbl_idx = 1;
		raw->dw = 5;
	}
}
EXPORT_SYMBOL_GPL(mt76_connac_mcu_wtbl_ht_tlv);

int mt76_connac_mcu_sta_cmd(struct mt76_phy *phy,
			    struct mt76_sta_cmd_info *info)
{
	struct mt76_vif *mvif = (struct mt76_vif *)info->vif->drv_priv;
	struct mt76_dev *dev = phy->dev;
	struct wtbl_req_hdr *wtbl_hdr;
	struct tlv *sta_wtbl;
	struct sk_buff *skb;

	skb = mt76_connac_mcu_alloc_sta_req(dev, mvif, info->wcid);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	if (info->sta || !info->offload_fw)
		mt76_connac_mcu_sta_basic_tlv(skb, info->vif, info->sta,
					      info->enable, info->newly);
	if (info->sta && info->enable)
		mt76_connac_mcu_sta_tlv(phy, skb, info->sta,
					info->vif, info->rcpi,
					info->state);

	sta_wtbl = mt76_connac_mcu_add_tlv(skb, STA_REC_WTBL,
					   sizeof(struct tlv));

	wtbl_hdr = mt76_connac_mcu_alloc_wtbl_req(dev, info->wcid,
						  WTBL_RESET_AND_SET,
						  sta_wtbl, &skb);
	if (IS_ERR(wtbl_hdr))
		return PTR_ERR(wtbl_hdr);

	if (info->enable) {
		mt76_connac_mcu_wtbl_generic_tlv(dev, skb, info->vif,
						 info->sta, sta_wtbl,
						 wtbl_hdr);
		mt76_connac_mcu_wtbl_hdr_trans_tlv(skb, info->vif, info->wcid,
						   sta_wtbl, wtbl_hdr);
		if (info->sta)
			mt76_connac_mcu_wtbl_ht_tlv(dev, skb, info->sta,
						    sta_wtbl, wtbl_hdr);
	}

	return mt76_mcu_skb_send_msg(dev, skb, info->cmd, true);
}
EXPORT_SYMBOL_GPL(mt76_connac_mcu_sta_cmd);

void mt76_connac_mcu_wtbl_ba_tlv(struct mt76_dev *dev, struct sk_buff *skb,
				 struct ieee80211_ampdu_params *params,
				 bool enable, bool tx, void *sta_wtbl,
				 void *wtbl_tlv)
{
	struct wtbl_ba *ba;
	struct tlv *tlv;

	tlv = mt76_connac_mcu_add_nested_tlv(skb, WTBL_BA, sizeof(*ba),
					     wtbl_tlv, sta_wtbl);

	ba = (struct wtbl_ba *)tlv;
	ba->tid = params->tid;

	if (tx) {
		ba->ba_type = MT_BA_TYPE_ORIGINATOR;
		ba->sn = enable ? cpu_to_le16(params->ssn) : 0;
		ba->ba_winsize = enable ? cpu_to_le16(params->buf_size) : 0;
		ba->ba_en = enable;
	} else {
		memcpy(ba->peer_addr, params->sta->addr, ETH_ALEN);
		ba->ba_type = MT_BA_TYPE_RECIPIENT;
		ba->rst_ba_tid = params->tid;
		ba->rst_ba_sel = RST_BA_MAC_TID_MATCH;
		ba->rst_ba_sb = 1;
	}

	if (is_mt7921(dev)) {
		ba->ba_winsize = enable ? cpu_to_le16(params->buf_size) : 0;
		return;
	}

	if (enable && tx) {
		u8 ba_range[] = { 4, 8, 12, 24, 36, 48, 54, 64 };
		int i;

		for (i = 7; i > 0; i--) {
			if (params->buf_size >= ba_range[i])
				break;
		}
		ba->ba_winsize_idx = i;
	}
}
EXPORT_SYMBOL_GPL(mt76_connac_mcu_wtbl_ba_tlv);

int mt76_connac_mcu_uni_add_dev(struct mt76_phy *phy,
				struct ieee80211_vif *vif,
				struct mt76_wcid *wcid,
				bool enable)
{
	struct mt76_vif *mvif = (struct mt76_vif *)vif->drv_priv;
	struct mt76_dev *dev = phy->dev;
	struct {
		struct {
			u8 omac_idx;
			u8 band_idx;
			__le16 pad;
		} __packed hdr;
		struct req_tlv {
			__le16 tag;
			__le16 len;
			u8 active;
			u8 pad;
			u8 omac_addr[ETH_ALEN];
		} __packed tlv;
	} dev_req = {
		.hdr = {
			.omac_idx = mvif->omac_idx,
			.band_idx = mvif->band_idx,
		},
		.tlv = {
			.tag = cpu_to_le16(DEV_INFO_ACTIVE),
			.len = cpu_to_le16(sizeof(struct req_tlv)),
			.active = enable,
		},
	};
	struct {
		struct {
			u8 bss_idx;
			u8 pad[3];
		} __packed hdr;
		struct mt76_connac_bss_basic_tlv basic;
	} basic_req = {
		.hdr = {
			.bss_idx = mvif->idx,
		},
		.basic = {
			.tag = cpu_to_le16(UNI_BSS_INFO_BASIC),
			.len = cpu_to_le16(sizeof(struct mt76_connac_bss_basic_tlv)),
			.omac_idx = mvif->omac_idx,
			.band_idx = mvif->band_idx,
			.wmm_idx = mvif->wmm_idx,
			.active = enable,
			.bmc_tx_wlan_idx = cpu_to_le16(wcid->idx),
			.sta_idx = cpu_to_le16(wcid->idx),
			.conn_state = 1,
		},
	};
	int err, idx, cmd, len;
	void *data;

	switch (vif->type) {
	case NL80211_IFTYPE_MESH_POINT:
	case NL80211_IFTYPE_MONITOR:
	case NL80211_IFTYPE_AP:
		basic_req.basic.conn_type = cpu_to_le32(CONNECTION_INFRA_AP);
		break;
	case NL80211_IFTYPE_STATION:
		basic_req.basic.conn_type = cpu_to_le32(CONNECTION_INFRA_STA);
		break;
	case NL80211_IFTYPE_ADHOC:
		basic_req.basic.conn_type = cpu_to_le32(CONNECTION_IBSS_ADHOC);
		break;
	default:
		WARN_ON(1);
		break;
	}

	idx = mvif->omac_idx > EXT_BSSID_START ? HW_BSSID_0 : mvif->omac_idx;
	basic_req.basic.hw_bss_idx = idx;

	memcpy(dev_req.tlv.omac_addr, vif->addr, ETH_ALEN);

	cmd = enable ? MCU_UNI_CMD_DEV_INFO_UPDATE : MCU_UNI_CMD_BSS_INFO_UPDATE;
	data = enable ? (void *)&dev_req : (void *)&basic_req;
	len = enable ? sizeof(dev_req) : sizeof(basic_req);

	err = mt76_mcu_send_msg(dev, cmd, data, len, true);
	if (err < 0)
		return err;

	cmd = enable ? MCU_UNI_CMD_BSS_INFO_UPDATE : MCU_UNI_CMD_DEV_INFO_UPDATE;
	data = enable ? (void *)&basic_req : (void *)&dev_req;
	len = enable ? sizeof(basic_req) : sizeof(dev_req);

	return mt76_mcu_send_msg(dev, cmd, data, len, true);
}
EXPORT_SYMBOL_GPL(mt76_connac_mcu_uni_add_dev);

void mt76_connac_mcu_sta_ba_tlv(struct sk_buff *skb,
				struct ieee80211_ampdu_params *params,
				bool enable, bool tx)
{
	struct sta_rec_ba *ba;
	struct tlv *tlv;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_BA, sizeof(*ba));

	ba = (struct sta_rec_ba *)tlv;
	ba->ba_type = tx ? MT_BA_TYPE_ORIGINATOR : MT_BA_TYPE_RECIPIENT;
	ba->winsize = cpu_to_le16(params->buf_size);
	ba->ssn = cpu_to_le16(params->ssn);
	ba->ba_en = enable << params->tid;
	ba->amsdu = params->amsdu;
	ba->tid = params->tid;
}
EXPORT_SYMBOL_GPL(mt76_connac_mcu_sta_ba_tlv);

int mt76_connac_mcu_sta_ba(struct mt76_dev *dev, struct mt76_vif *mvif,
			   struct ieee80211_ampdu_params *params,
			   bool enable, bool tx)
{
	struct mt76_wcid *wcid = (struct mt76_wcid *)params->sta->drv_priv;
	struct wtbl_req_hdr *wtbl_hdr;
	struct tlv *sta_wtbl;
	struct sk_buff *skb;
	int ret;

	skb = mt76_connac_mcu_alloc_sta_req(dev, mvif, wcid);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	sta_wtbl = mt76_connac_mcu_add_tlv(skb, STA_REC_WTBL,
					   sizeof(struct tlv));

	wtbl_hdr = mt76_connac_mcu_alloc_wtbl_req(dev, wcid, WTBL_SET,
						  sta_wtbl, &skb);
	if (IS_ERR(wtbl_hdr))
		return PTR_ERR(wtbl_hdr);

	mt76_connac_mcu_wtbl_ba_tlv(dev, skb, params, enable, tx, sta_wtbl,
				    wtbl_hdr);

	ret = mt76_mcu_skb_send_msg(dev, skb, MCU_UNI_CMD_STA_REC_UPDATE, true);
	if (ret)
		return ret;

	skb = mt76_connac_mcu_alloc_sta_req(dev, mvif, wcid);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	mt76_connac_mcu_sta_ba_tlv(skb, params, enable, tx);

	return mt76_mcu_skb_send_msg(dev, skb, MCU_UNI_CMD_STA_REC_UPDATE,
				     true);
}
EXPORT_SYMBOL_GPL(mt76_connac_mcu_sta_ba);

static u8
mt76_connac_get_phy_mode(struct mt76_phy *phy, struct ieee80211_vif *vif,
			 enum nl80211_band band,
			 struct ieee80211_sta *sta)
{
	struct mt76_dev *dev = phy->dev;
	const struct ieee80211_sta_he_cap *he_cap;
	struct ieee80211_sta_vht_cap *vht_cap;
	struct ieee80211_sta_ht_cap *ht_cap;
	u8 mode = 0;

	if (!is_mt7921(dev))
		return 0x38;

	if (sta) {
		ht_cap = &sta->ht_cap;
		vht_cap = &sta->vht_cap;
		he_cap = &sta->he_cap;
	} else {
		struct ieee80211_supported_band *sband;

		sband = phy->hw->wiphy->bands[band];
		ht_cap = &sband->ht_cap;
		vht_cap = &sband->vht_cap;
		he_cap = ieee80211_get_he_iftype_cap(sband, vif->type);
	}

	if (band == NL80211_BAND_2GHZ) {
		mode |= PHY_MODE_B | PHY_MODE_G;

		if (ht_cap->ht_supported)
			mode |= PHY_MODE_GN;

		if (he_cap->has_he)
			mode |= PHY_MODE_AX_24G;
	} else if (band == NL80211_BAND_5GHZ) {
		mode |= PHY_MODE_A;

		if (ht_cap->ht_supported)
			mode |= PHY_MODE_AN;

		if (vht_cap->vht_supported)
			mode |= PHY_MODE_AC;

		if (he_cap->has_he)
			mode |= PHY_MODE_AX_5G;
	}

	return mode;
}

static const struct ieee80211_sta_he_cap *
mt76_connac_get_he_phy_cap(struct mt76_phy *phy, struct ieee80211_vif *vif)
{
	enum nl80211_band band = phy->chandef.chan->band;
	struct ieee80211_supported_band *sband;

	sband = phy->hw->wiphy->bands[band];

	return ieee80211_get_he_iftype_cap(sband, vif->type);
}

#define DEFAULT_HE_PE_DURATION		4
#define DEFAULT_HE_DURATION_RTS_THRES	1023
static void
mt76_connac_mcu_uni_bss_he_tlv(struct mt76_phy *phy, struct ieee80211_vif *vif,
			       struct tlv *tlv)
{
	const struct ieee80211_sta_he_cap *cap;
	struct bss_info_uni_he *he;

	cap = mt76_connac_get_he_phy_cap(phy, vif);

	he = (struct bss_info_uni_he *)tlv;
	he->he_pe_duration = vif->bss_conf.htc_trig_based_pkt_ext;
	if (!he->he_pe_duration)
		he->he_pe_duration = DEFAULT_HE_PE_DURATION;

	he->he_rts_thres = cpu_to_le16(vif->bss_conf.frame_time_rts_th);
	if (!he->he_rts_thres)
		he->he_rts_thres = cpu_to_le16(DEFAULT_HE_DURATION_RTS_THRES);

	he->max_nss_mcs[CMD_HE_MCS_BW80] = cap->he_mcs_nss_supp.tx_mcs_80;
	he->max_nss_mcs[CMD_HE_MCS_BW160] = cap->he_mcs_nss_supp.tx_mcs_160;
	he->max_nss_mcs[CMD_HE_MCS_BW8080] = cap->he_mcs_nss_supp.tx_mcs_80p80;
}

int mt76_connac_mcu_uni_add_bss(struct mt76_phy *phy,
				struct ieee80211_vif *vif,
				struct mt76_wcid *wcid,
				bool enable)
{
	struct mt76_vif *mvif = (struct mt76_vif *)vif->drv_priv;
	struct cfg80211_chan_def *chandef = &phy->chandef;
	int freq1 = chandef->center_freq1, freq2 = chandef->center_freq2;
	enum nl80211_band band = chandef->chan->band;
	struct mt76_dev *mdev = phy->dev;
	struct {
		struct {
			u8 bss_idx;
			u8 pad[3];
		} __packed hdr;
		struct mt76_connac_bss_basic_tlv basic;
		struct mt76_connac_bss_qos_tlv qos;
	} basic_req = {
		.hdr = {
			.bss_idx = mvif->idx,
		},
		.basic = {
			.tag = cpu_to_le16(UNI_BSS_INFO_BASIC),
			.len = cpu_to_le16(sizeof(struct mt76_connac_bss_basic_tlv)),
			.bcn_interval = cpu_to_le16(vif->bss_conf.beacon_int),
			.dtim_period = vif->bss_conf.dtim_period,
			.omac_idx = mvif->omac_idx,
			.band_idx = mvif->band_idx,
			.wmm_idx = mvif->wmm_idx,
			.active = true, /* keep bss deactivated */
			.phymode = mt76_connac_get_phy_mode(phy, vif, band, NULL),
		},
		.qos = {
			.tag = cpu_to_le16(UNI_BSS_INFO_QBSS),
			.len = cpu_to_le16(sizeof(struct mt76_connac_bss_qos_tlv)),
			.qos = vif->bss_conf.qos,
		},
	};
	struct {
		struct {
			u8 bss_idx;
			u8 pad[3];
		} __packed hdr;
		struct rlm_tlv {
			__le16 tag;
			__le16 len;
			u8 control_channel;
			u8 center_chan;
			u8 center_chan2;
			u8 bw;
			u8 tx_streams;
			u8 rx_streams;
			u8 short_st;
			u8 ht_op_info;
			u8 sco;
			u8 pad[3];
		} __packed rlm;
	} __packed rlm_req = {
		.hdr = {
			.bss_idx = mvif->idx,
		},
		.rlm = {
			.tag = cpu_to_le16(UNI_BSS_INFO_RLM),
			.len = cpu_to_le16(sizeof(struct rlm_tlv)),
			.control_channel = chandef->chan->hw_value,
			.center_chan = ieee80211_frequency_to_channel(freq1),
			.center_chan2 = ieee80211_frequency_to_channel(freq2),
			.tx_streams = hweight8(phy->antenna_mask),
			.ht_op_info = 4, /* set HT 40M allowed */
			.rx_streams = phy->chainmask,
			.short_st = true,
		},
	};
	int err, conn_type;
	u8 idx;

	idx = mvif->omac_idx > EXT_BSSID_START ? HW_BSSID_0 : mvif->omac_idx;
	basic_req.basic.hw_bss_idx = idx;

	switch (vif->type) {
	case NL80211_IFTYPE_MESH_POINT:
	case NL80211_IFTYPE_AP:
		if (vif->p2p)
			conn_type = CONNECTION_P2P_GO;
		else
			conn_type = CONNECTION_INFRA_AP;
		basic_req.basic.conn_type = cpu_to_le32(conn_type);
		break;
	case NL80211_IFTYPE_STATION:
		if (vif->p2p)
			conn_type = CONNECTION_P2P_GC;
		else
			conn_type = CONNECTION_INFRA_STA;
		basic_req.basic.conn_type = cpu_to_le32(conn_type);
		break;
	case NL80211_IFTYPE_ADHOC:
		basic_req.basic.conn_type = cpu_to_le32(CONNECTION_IBSS_ADHOC);
		break;
	default:
		WARN_ON(1);
		break;
	}

	memcpy(basic_req.basic.bssid, vif->bss_conf.bssid, ETH_ALEN);
	basic_req.basic.bmc_tx_wlan_idx = cpu_to_le16(wcid->idx);
	basic_req.basic.sta_idx = cpu_to_le16(wcid->idx);
	basic_req.basic.conn_state = !enable;

	err = mt76_mcu_send_msg(mdev, MCU_UNI_CMD_BSS_INFO_UPDATE, &basic_req,
				sizeof(basic_req), true);
	if (err < 0)
		return err;

	if (vif->bss_conf.he_support) {
		struct {
			struct {
				u8 bss_idx;
				u8 pad[3];
			} __packed hdr;
			struct bss_info_uni_he he;
			struct bss_info_uni_bss_color bss_color;
		} he_req = {
			.hdr = {
				.bss_idx = mvif->idx,
			},
			.he = {
				.tag = cpu_to_le16(UNI_BSS_INFO_HE_BASIC),
				.len = cpu_to_le16(sizeof(struct bss_info_uni_he)),
			},
			.bss_color = {
				.tag = cpu_to_le16(UNI_BSS_INFO_BSS_COLOR),
				.len = cpu_to_le16(sizeof(struct bss_info_uni_bss_color)),
				.enable = 0,
				.bss_color = 0,
			},
		};

		if (enable) {
			he_req.bss_color.enable =
				vif->bss_conf.he_bss_color.enabled;
			he_req.bss_color.bss_color =
				vif->bss_conf.he_bss_color.color;
		}

		mt76_connac_mcu_uni_bss_he_tlv(phy, vif,
					       (struct tlv *)&he_req.he);
		err = mt76_mcu_send_msg(mdev, MCU_UNI_CMD_BSS_INFO_UPDATE,
					&he_req, sizeof(he_req), true);
		if (err < 0)
			return err;
	}

	switch (chandef->width) {
	case NL80211_CHAN_WIDTH_40:
		rlm_req.rlm.bw = CMD_CBW_40MHZ;
		break;
	case NL80211_CHAN_WIDTH_80:
		rlm_req.rlm.bw = CMD_CBW_80MHZ;
		break;
	case NL80211_CHAN_WIDTH_80P80:
		rlm_req.rlm.bw = CMD_CBW_8080MHZ;
		break;
	case NL80211_CHAN_WIDTH_160:
		rlm_req.rlm.bw = CMD_CBW_160MHZ;
		break;
	case NL80211_CHAN_WIDTH_5:
		rlm_req.rlm.bw = CMD_CBW_5MHZ;
		break;
	case NL80211_CHAN_WIDTH_10:
		rlm_req.rlm.bw = CMD_CBW_10MHZ;
		break;
	case NL80211_CHAN_WIDTH_20_NOHT:
	case NL80211_CHAN_WIDTH_20:
	default:
		rlm_req.rlm.bw = CMD_CBW_20MHZ;
		rlm_req.rlm.ht_op_info = 0;
		break;
	}

	if (rlm_req.rlm.control_channel < rlm_req.rlm.center_chan)
		rlm_req.rlm.sco = 1; /* SCA */
	else if (rlm_req.rlm.control_channel > rlm_req.rlm.center_chan)
		rlm_req.rlm.sco = 3; /* SCB */

	return mt76_mcu_send_msg(mdev, MCU_UNI_CMD_BSS_INFO_UPDATE, &rlm_req,
				 sizeof(rlm_req), true);
}
EXPORT_SYMBOL_GPL(mt76_connac_mcu_uni_add_bss);

#define MT76_CONNAC_SCAN_CHANNEL_TIME		60
int mt76_connac_mcu_hw_scan(struct mt76_phy *phy, struct ieee80211_vif *vif,
			    struct ieee80211_scan_request *scan_req)
{
	struct mt76_vif *mvif = (struct mt76_vif *)vif->drv_priv;
	struct cfg80211_scan_request *sreq = &scan_req->req;
	int n_ssids = 0, err, i, duration;
	int ext_channels_num = max_t(int, sreq->n_channels - 32, 0);
	struct ieee80211_channel **scan_list = sreq->channels;
	struct mt76_dev *mdev = phy->dev;
	bool ext_phy = phy == mdev->phy2;
	struct mt76_connac_mcu_scan_channel *chan;
	struct mt76_connac_hw_scan_req *req;
	struct sk_buff *skb;

	skb = mt76_mcu_msg_alloc(mdev, NULL, sizeof(*req));
	if (!skb)
		return -ENOMEM;

	set_bit(MT76_HW_SCANNING, &phy->state);
	mvif->scan_seq_num = (mvif->scan_seq_num + 1) & 0x7f;

	req = (struct mt76_connac_hw_scan_req *)skb_put(skb, sizeof(*req));

	req->seq_num = mvif->scan_seq_num | ext_phy << 7;
	req->bss_idx = mvif->idx;
	req->scan_type = sreq->n_ssids ? 1 : 0;
	req->probe_req_num = sreq->n_ssids ? 2 : 0;
	req->version = 1;

	for (i = 0; i < sreq->n_ssids; i++) {
		if (!sreq->ssids[i].ssid_len)
			continue;

		req->ssids[i].ssid_len = cpu_to_le32(sreq->ssids[i].ssid_len);
		memcpy(req->ssids[i].ssid, sreq->ssids[i].ssid,
		       sreq->ssids[i].ssid_len);
		n_ssids++;
	}
	req->ssid_type = n_ssids ? BIT(2) : BIT(0);
	req->ssid_type_ext = n_ssids ? BIT(0) : 0;
	req->ssids_num = n_ssids;

	duration = is_mt7921(phy->dev) ? 0 : MT76_CONNAC_SCAN_CHANNEL_TIME;
	/* increase channel time for passive scan */
	if (!sreq->n_ssids)
		duration *= 2;
	req->timeout_value = cpu_to_le16(sreq->n_channels * duration);
	req->channel_min_dwell_time = cpu_to_le16(duration);
	req->channel_dwell_time = cpu_to_le16(duration);

	req->channels_num = min_t(u8, sreq->n_channels, 32);
	req->ext_channels_num = min_t(u8, ext_channels_num, 32);
	for (i = 0; i < req->channels_num + req->ext_channels_num; i++) {
		if (i >= 32)
			chan = &req->ext_channels[i - 32];
		else
			chan = &req->channels[i];

		chan->band = scan_list[i]->band == NL80211_BAND_2GHZ ? 1 : 2;
		chan->channel_num = scan_list[i]->hw_value;
	}
	req->channel_type = sreq->n_channels ? 4 : 0;

	if (sreq->ie_len > 0) {
		memcpy(req->ies, sreq->ie, sreq->ie_len);
		req->ies_len = cpu_to_le16(sreq->ie_len);
	}

	if (is_mt7921(phy->dev))
		req->scan_func |= SCAN_FUNC_SPLIT_SCAN;

	memcpy(req->bssid, sreq->bssid, ETH_ALEN);
	if (sreq->flags & NL80211_SCAN_FLAG_RANDOM_ADDR) {
		get_random_mask_addr(req->random_mac, sreq->mac_addr,
				     sreq->mac_addr_mask);
		req->scan_func |= SCAN_FUNC_RANDOM_MAC;
	}

	err = mt76_mcu_skb_send_msg(mdev, skb, MCU_CMD_START_HW_SCAN, false);
	if (err < 0)
		clear_bit(MT76_HW_SCANNING, &phy->state);

	return err;
}
EXPORT_SYMBOL_GPL(mt76_connac_mcu_hw_scan);

int mt76_connac_mcu_cancel_hw_scan(struct mt76_phy *phy,
				   struct ieee80211_vif *vif)
{
	struct mt76_vif *mvif = (struct mt76_vif *)vif->drv_priv;
	struct {
		u8 seq_num;
		u8 is_ext_channel;
		u8 rsv[2];
	} __packed req = {
		.seq_num = mvif->scan_seq_num,
	};

	if (test_and_clear_bit(MT76_HW_SCANNING, &phy->state)) {
		struct cfg80211_scan_info info = {
			.aborted = true,
		};

		ieee80211_scan_completed(phy->hw, &info);
	}

	return mt76_mcu_send_msg(phy->dev, MCU_CMD_CANCEL_HW_SCAN, &req,
				 sizeof(req), false);
}
EXPORT_SYMBOL_GPL(mt76_connac_mcu_cancel_hw_scan);

int mt76_connac_mcu_sched_scan_req(struct mt76_phy *phy,
				   struct ieee80211_vif *vif,
				   struct cfg80211_sched_scan_request *sreq)
{
	struct mt76_vif *mvif = (struct mt76_vif *)vif->drv_priv;
	struct ieee80211_channel **scan_list = sreq->channels;
	struct mt76_connac_mcu_scan_channel *chan;
	struct mt76_connac_sched_scan_req *req;
	struct mt76_dev *mdev = phy->dev;
	bool ext_phy = phy == mdev->phy2;
	struct cfg80211_match_set *match;
	struct cfg80211_ssid *ssid;
	struct sk_buff *skb;
	int i;

	skb = mt76_mcu_msg_alloc(mdev, NULL, sizeof(*req) + sreq->ie_len);
	if (!skb)
		return -ENOMEM;

	mvif->scan_seq_num = (mvif->scan_seq_num + 1) & 0x7f;

	req = (struct mt76_connac_sched_scan_req *)skb_put(skb, sizeof(*req));
	req->version = 1;
	req->seq_num = mvif->scan_seq_num | ext_phy << 7;

	if (sreq->flags & NL80211_SCAN_FLAG_RANDOM_ADDR) {
		u8 *addr = is_mt7663(phy->dev) ? req->mt7663.random_mac
					       : req->mt7921.random_mac;

		req->scan_func = 1;
		get_random_mask_addr(addr, sreq->mac_addr,
				     sreq->mac_addr_mask);
	}
	if (is_mt7921(phy->dev))
		req->mt7921.bss_idx = mvif->idx;

	req->ssids_num = sreq->n_ssids;
	for (i = 0; i < req->ssids_num; i++) {
		ssid = &sreq->ssids[i];
		memcpy(req->ssids[i].ssid, ssid->ssid, ssid->ssid_len);
		req->ssids[i].ssid_len = cpu_to_le32(ssid->ssid_len);
	}

	req->match_num = sreq->n_match_sets;
	for (i = 0; i < req->match_num; i++) {
		match = &sreq->match_sets[i];
		memcpy(req->match[i].ssid, match->ssid.ssid,
		       match->ssid.ssid_len);
		req->match[i].rssi_th = cpu_to_le32(match->rssi_thold);
		req->match[i].ssid_len = match->ssid.ssid_len;
	}

	req->channel_type = sreq->n_channels ? 4 : 0;
	req->channels_num = min_t(u8, sreq->n_channels, 64);
	for (i = 0; i < req->channels_num; i++) {
		chan = &req->channels[i];
		chan->band = scan_list[i]->band == NL80211_BAND_2GHZ ? 1 : 2;
		chan->channel_num = scan_list[i]->hw_value;
	}

	req->intervals_num = sreq->n_scan_plans;
	for (i = 0; i < req->intervals_num; i++)
		req->intervals[i] = cpu_to_le16(sreq->scan_plans[i].interval);

	if (sreq->ie_len > 0) {
		req->ie_len = cpu_to_le16(sreq->ie_len);
		memcpy(skb_put(skb, sreq->ie_len), sreq->ie, sreq->ie_len);
	}

	return mt76_mcu_skb_send_msg(mdev, skb, MCU_CMD_SCHED_SCAN_REQ, false);
}
EXPORT_SYMBOL_GPL(mt76_connac_mcu_sched_scan_req);

int mt76_connac_mcu_sched_scan_enable(struct mt76_phy *phy,
				      struct ieee80211_vif *vif,
				      bool enable)
{
	struct {
		u8 active; /* 0: enabled 1: disabled */
		u8 rsv[3];
	} __packed req = {
		.active = !enable,
	};

	if (enable)
		set_bit(MT76_HW_SCHED_SCANNING, &phy->state);
	else
		clear_bit(MT76_HW_SCHED_SCANNING, &phy->state);

	return mt76_mcu_send_msg(phy->dev, MCU_CMD_SCHED_SCAN_ENABLE, &req,
				 sizeof(req), false);
}
EXPORT_SYMBOL_GPL(mt76_connac_mcu_sched_scan_enable);

int mt76_connac_mcu_chip_config(struct mt76_dev *dev)
{
	struct mt76_connac_config req = {
		.resp_type = 0,
	};

	memcpy(req.data, "assert", 7);

	return mt76_mcu_send_msg(dev, MCU_CMD_CHIP_CONFIG, &req, sizeof(req),
				 false);
}
EXPORT_SYMBOL_GPL(mt76_connac_mcu_chip_config);

int mt76_connac_mcu_set_deep_sleep(struct mt76_dev *dev, bool enable)
{
	struct mt76_connac_config req = {
		.resp_type = 0,
	};

	snprintf(req.data, sizeof(req.data), "KeepFullPwr %d", !enable);

	return mt76_mcu_send_msg(dev, MCU_CMD_CHIP_CONFIG, &req, sizeof(req),
				 false);
}
EXPORT_SYMBOL_GPL(mt76_connac_mcu_set_deep_sleep);

int mt76_connac_sta_state_dp(struct mt76_dev *dev,
			     enum ieee80211_sta_state old_state,
			     enum ieee80211_sta_state new_state)
{
	if ((old_state == IEEE80211_STA_ASSOC &&
	     new_state == IEEE80211_STA_AUTHORIZED) ||
	    (old_state == IEEE80211_STA_NONE &&
	     new_state == IEEE80211_STA_NOTEXIST))
		mt76_connac_mcu_set_deep_sleep(dev, true);

	if ((old_state == IEEE80211_STA_NOTEXIST &&
	     new_state == IEEE80211_STA_NONE) ||
	    (old_state == IEEE80211_STA_AUTHORIZED &&
	     new_state == IEEE80211_STA_ASSOC))
		mt76_connac_mcu_set_deep_sleep(dev, false);

	return 0;
}
EXPORT_SYMBOL_GPL(mt76_connac_sta_state_dp);

void mt76_connac_mcu_coredump_event(struct mt76_dev *dev, struct sk_buff *skb,
				    struct mt76_connac_coredump *coredump)
{
	spin_lock_bh(&dev->lock);
	__skb_queue_tail(&coredump->msg_list, skb);
	spin_unlock_bh(&dev->lock);

	coredump->last_activity = jiffies;

	queue_delayed_work(dev->wq, &coredump->work,
			   MT76_CONNAC_COREDUMP_TIMEOUT);
}
EXPORT_SYMBOL_GPL(mt76_connac_mcu_coredump_event);

int mt76_connac_mcu_get_nic_capability(struct mt76_phy *phy)
{
	struct mt76_connac_cap_hdr {
		__le16 n_element;
		u8 rsv[2];
	} __packed * hdr;
	struct sk_buff *skb;
	int ret, i;

	ret = mt76_mcu_send_and_get_msg(phy->dev, MCU_CMD_GET_NIC_CAPAB, NULL,
					0, true, &skb);
	if (ret)
		return ret;

	hdr = (struct mt76_connac_cap_hdr *)skb->data;
	if (skb->len < sizeof(*hdr)) {
		ret = -EINVAL;
		goto out;
	}

	skb_pull(skb, sizeof(*hdr));

	for (i = 0; i < le16_to_cpu(hdr->n_element); i++) {
		struct tlv_hdr {
			__le32 type;
			__le32 len;
		} __packed * tlv = (struct tlv_hdr *)skb->data;
		int len;

		if (skb->len < sizeof(*tlv))
			break;

		skb_pull(skb, sizeof(*tlv));

		len = le32_to_cpu(tlv->len);
		if (skb->len < len)
			break;

		switch (le32_to_cpu(tlv->type)) {
		case MT_NIC_CAP_6G:
			phy->cap.has_6ghz = skb->data[0];
			break;
		default:
			break;
		}
		skb_pull(skb, len);
	}
out:
	dev_kfree_skb(skb);

	return ret;
}
EXPORT_SYMBOL_GPL(mt76_connac_mcu_get_nic_capability);

static void
mt76_connac_mcu_build_sku(struct mt76_dev *dev, s8 *sku,
			  struct mt76_power_limits *limits,
			  enum nl80211_band band)
{
	int max_power = is_mt7921(dev) ? 127 : 63;
	int i, offset = sizeof(limits->cck);

	memset(sku, max_power, MT_SKU_POWER_LIMIT);

	if (band == NL80211_BAND_2GHZ) {
		/* cck */
		memcpy(sku, limits->cck, sizeof(limits->cck));
	}

	/* ofdm */
	memcpy(&sku[offset], limits->ofdm, sizeof(limits->ofdm));
	offset += sizeof(limits->ofdm);

	/* ht */
	for (i = 0; i < 2; i++) {
		memcpy(&sku[offset], limits->mcs[i], 8);
		offset += 8;
	}
	sku[offset++] = limits->mcs[0][0];

	/* vht */
	for (i = 0; i < ARRAY_SIZE(limits->mcs); i++) {
		memcpy(&sku[offset], limits->mcs[i],
		       ARRAY_SIZE(limits->mcs[i]));
		offset += 12;
	}

	if (!is_mt7921(dev))
		return;

	/* he */
	for (i = 0; i < ARRAY_SIZE(limits->ru); i++) {
		memcpy(&sku[offset], limits->ru[i], ARRAY_SIZE(limits->ru[i]));
		offset += ARRAY_SIZE(limits->ru[i]);
	}
}

static int
mt76_connac_mcu_rate_txpower_band(struct mt76_phy *phy,
				  enum nl80211_band band)
{
	struct mt76_dev *dev = phy->dev;
	int sku_len, batch_len = is_mt7921(dev) ? 8 : 16;
	static const u8 chan_list_2ghz[] = {
		1, 2,  3,  4,  5,  6,  7,
		8, 9, 10, 11, 12, 13, 14
	};
	static const u8 chan_list_5ghz[] = {
		 36,  38,  40,  42,  44,  46,  48,
		 50,  52,  54,  56,  58,  60,  62,
		 64, 100, 102, 104, 106, 108, 110,
		112, 114, 116, 118, 120, 122, 124,
		126, 128, 132, 134, 136, 138, 140,
		142, 144, 149, 151, 153, 155, 157,
		159, 161, 165
	};
	int i, n_chan, batch_size, idx = 0, tx_power, last_ch;
	struct mt76_connac_sku_tlv sku_tlbv;
	struct mt76_power_limits limits;
	const u8 *ch_list;

	sku_len = is_mt7921(dev) ? sizeof(sku_tlbv) : sizeof(sku_tlbv) - 92;
	tx_power = 2 * phy->hw->conf.power_level;
	if (!tx_power)
		tx_power = 127;

	if (band == NL80211_BAND_2GHZ) {
		n_chan = ARRAY_SIZE(chan_list_2ghz);
		ch_list = chan_list_2ghz;
	} else {
		n_chan = ARRAY_SIZE(chan_list_5ghz);
		ch_list = chan_list_5ghz;
	}
	batch_size = DIV_ROUND_UP(n_chan, batch_len);

	if (!phy->cap.has_5ghz)
		last_ch = chan_list_2ghz[n_chan - 1];
	else
		last_ch = chan_list_5ghz[n_chan - 1];

	for (i = 0; i < batch_size; i++) {
		struct mt76_connac_tx_power_limit_tlv tx_power_tlv = {
			.band = band == NL80211_BAND_2GHZ ? 1 : 2,
		};
		int j, err, msg_len, num_ch;
		struct sk_buff *skb;

		num_ch = i == batch_size - 1 ? n_chan % batch_len : batch_len;
		msg_len = sizeof(tx_power_tlv) + num_ch * sizeof(sku_tlbv);
		skb = mt76_mcu_msg_alloc(dev, NULL, msg_len);
		if (!skb)
			return -ENOMEM;

		skb_reserve(skb, sizeof(tx_power_tlv));

		BUILD_BUG_ON(sizeof(dev->alpha2) > sizeof(tx_power_tlv.alpha2));
		memcpy(tx_power_tlv.alpha2, dev->alpha2, sizeof(dev->alpha2));
		tx_power_tlv.n_chan = num_ch;

		for (j = 0; j < num_ch; j++, idx++) {
			struct ieee80211_channel chan = {
				.hw_value = ch_list[idx],
				.band = band,
			};

			mt76_get_rate_power_limits(phy, &chan, &limits,
						   tx_power);

			tx_power_tlv.last_msg = ch_list[idx] == last_ch;
			sku_tlbv.channel = ch_list[idx];

			mt76_connac_mcu_build_sku(dev, sku_tlbv.pwr_limit,
						  &limits, band);
			skb_put_data(skb, &sku_tlbv, sku_len);
		}
		__skb_push(skb, sizeof(tx_power_tlv));
		memcpy(skb->data, &tx_power_tlv, sizeof(tx_power_tlv));

		err = mt76_mcu_skb_send_msg(dev, skb,
					    MCU_CMD_SET_RATE_TX_POWER, false);
		if (err < 0)
			return err;
	}

	return 0;
}

int mt76_connac_mcu_set_rate_txpower(struct mt76_phy *phy)
{
	int err;

	if (phy->cap.has_2ghz) {
		err = mt76_connac_mcu_rate_txpower_band(phy,
							NL80211_BAND_2GHZ);
		if (err < 0)
			return err;
	}
	if (phy->cap.has_5ghz) {
		err = mt76_connac_mcu_rate_txpower_band(phy,
							NL80211_BAND_5GHZ);
		if (err < 0)
			return err;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(mt76_connac_mcu_set_rate_txpower);

int mt76_connac_mcu_update_arp_filter(struct mt76_dev *dev,
				      struct mt76_vif *vif,
				      struct ieee80211_bss_conf *info)
{
	struct sk_buff *skb;
	int i, len = min_t(int, info->arp_addr_cnt,
			   IEEE80211_BSS_ARP_ADDR_LIST_LEN);
	struct {
		struct {
			u8 bss_idx;
			u8 pad[3];
		} __packed hdr;
		struct mt76_connac_arpns_tlv arp;
	} req_hdr = {
		.hdr = {
			.bss_idx = vif->idx,
		},
		.arp = {
			.tag = cpu_to_le16(UNI_OFFLOAD_OFFLOAD_ARP),
			.len = cpu_to_le16(sizeof(struct mt76_connac_arpns_tlv)),
			.ips_num = len,
			.mode = 2,  /* update */
			.option = 1,
		},
	};

	skb = mt76_mcu_msg_alloc(dev, NULL,
				 sizeof(req_hdr) + len * sizeof(__be32));
	if (!skb)
		return -ENOMEM;

	skb_put_data(skb, &req_hdr, sizeof(req_hdr));
	for (i = 0; i < len; i++) {
		u8 *addr = (u8 *)skb_put(skb, sizeof(__be32));

		memcpy(addr, &info->arp_addr_list[i], sizeof(__be32));
	}

	return mt76_mcu_skb_send_msg(dev, skb, MCU_UNI_CMD_OFFLOAD, true);
}
EXPORT_SYMBOL_GPL(mt76_connac_mcu_update_arp_filter);

#ifdef CONFIG_PM

const struct wiphy_wowlan_support mt76_connac_wowlan_support = {
	.flags = WIPHY_WOWLAN_MAGIC_PKT | WIPHY_WOWLAN_DISCONNECT |
		 WIPHY_WOWLAN_SUPPORTS_GTK_REKEY | WIPHY_WOWLAN_NET_DETECT,
	.n_patterns = 1,
	.pattern_min_len = 1,
	.pattern_max_len = MT76_CONNAC_WOW_PATTEN_MAX_LEN,
	.max_nd_match_sets = 10,
};
EXPORT_SYMBOL_GPL(mt76_connac_wowlan_support);

static void
mt76_connac_mcu_key_iter(struct ieee80211_hw *hw,
			 struct ieee80211_vif *vif,
			 struct ieee80211_sta *sta,
			 struct ieee80211_key_conf *key,
			 void *data)
{
	struct mt76_connac_gtk_rekey_tlv *gtk_tlv = data;
	u32 cipher;

	if (key->cipher != WLAN_CIPHER_SUITE_AES_CMAC &&
	    key->cipher != WLAN_CIPHER_SUITE_CCMP &&
	    key->cipher != WLAN_CIPHER_SUITE_TKIP)
		return;

	if (key->cipher == WLAN_CIPHER_SUITE_TKIP) {
		gtk_tlv->proto = cpu_to_le32(NL80211_WPA_VERSION_1);
		cipher = BIT(3);
	} else {
		gtk_tlv->proto = cpu_to_le32(NL80211_WPA_VERSION_2);
		cipher = BIT(4);
	}

	/* we are assuming here to have a single pairwise key */
	if (key->flags & IEEE80211_KEY_FLAG_PAIRWISE) {
		gtk_tlv->pairwise_cipher = cpu_to_le32(cipher);
		gtk_tlv->group_cipher = cpu_to_le32(cipher);
		gtk_tlv->keyid = key->keyidx;
	}
}

int mt76_connac_mcu_update_gtk_rekey(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif,
				     struct cfg80211_gtk_rekey_data *key)
{
	struct mt76_vif *mvif = (struct mt76_vif *)vif->drv_priv;
	struct mt76_connac_gtk_rekey_tlv *gtk_tlv;
	struct mt76_phy *phy = hw->priv;
	struct sk_buff *skb;
	struct {
		u8 bss_idx;
		u8 pad[3];
	} __packed hdr = {
		.bss_idx = mvif->idx,
	};

	skb = mt76_mcu_msg_alloc(phy->dev, NULL,
				 sizeof(hdr) + sizeof(*gtk_tlv));
	if (!skb)
		return -ENOMEM;

	skb_put_data(skb, &hdr, sizeof(hdr));
	gtk_tlv = (struct mt76_connac_gtk_rekey_tlv *)skb_put(skb,
							 sizeof(*gtk_tlv));
	gtk_tlv->tag = cpu_to_le16(UNI_OFFLOAD_OFFLOAD_GTK_REKEY);
	gtk_tlv->len = cpu_to_le16(sizeof(*gtk_tlv));
	gtk_tlv->rekey_mode = 2;
	gtk_tlv->option = 1;

	rcu_read_lock();
	ieee80211_iter_keys_rcu(hw, vif, mt76_connac_mcu_key_iter, gtk_tlv);
	rcu_read_unlock();

	memcpy(gtk_tlv->kek, key->kek, NL80211_KEK_LEN);
	memcpy(gtk_tlv->kck, key->kck, NL80211_KCK_LEN);
	memcpy(gtk_tlv->replay_ctr, key->replay_ctr, NL80211_REPLAY_CTR_LEN);

	return mt76_mcu_skb_send_msg(phy->dev, skb, MCU_UNI_CMD_OFFLOAD, true);
}
EXPORT_SYMBOL_GPL(mt76_connac_mcu_update_gtk_rekey);

static int
mt76_connac_mcu_set_arp_filter(struct mt76_dev *dev, struct ieee80211_vif *vif,
			       bool suspend)
{
	struct mt76_vif *mvif = (struct mt76_vif *)vif->drv_priv;
	struct {
		struct {
			u8 bss_idx;
			u8 pad[3];
		} __packed hdr;
		struct mt76_connac_arpns_tlv arpns;
	} req = {
		.hdr = {
			.bss_idx = mvif->idx,
		},
		.arpns = {
			.tag = cpu_to_le16(UNI_OFFLOAD_OFFLOAD_ARP),
			.len = cpu_to_le16(sizeof(struct mt76_connac_arpns_tlv)),
			.mode = suspend,
		},
	};

	return mt76_mcu_send_msg(dev, MCU_UNI_CMD_OFFLOAD, &req, sizeof(req),
				 true);
}

static int
mt76_connac_mcu_set_gtk_rekey(struct mt76_dev *dev, struct ieee80211_vif *vif,
			      bool suspend)
{
	struct mt76_vif *mvif = (struct mt76_vif *)vif->drv_priv;
	struct {
		struct {
			u8 bss_idx;
			u8 pad[3];
		} __packed hdr;
		struct mt76_connac_gtk_rekey_tlv gtk_tlv;
	} __packed req = {
		.hdr = {
			.bss_idx = mvif->idx,
		},
		.gtk_tlv = {
			.tag = cpu_to_le16(UNI_OFFLOAD_OFFLOAD_GTK_REKEY),
			.len = cpu_to_le16(sizeof(struct mt76_connac_gtk_rekey_tlv)),
			.rekey_mode = !suspend,
		},
	};

	return mt76_mcu_send_msg(dev, MCU_UNI_CMD_OFFLOAD, &req, sizeof(req),
				 true);
}

static int
mt76_connac_mcu_set_suspend_mode(struct mt76_dev *dev,
				 struct ieee80211_vif *vif,
				 bool enable, u8 mdtim,
				 bool wow_suspend)
{
	struct mt76_vif *mvif = (struct mt76_vif *)vif->drv_priv;
	struct {
		struct {
			u8 bss_idx;
			u8 pad[3];
		} __packed hdr;
		struct mt76_connac_suspend_tlv suspend_tlv;
	} req = {
		.hdr = {
			.bss_idx = mvif->idx,
		},
		.suspend_tlv = {
			.tag = cpu_to_le16(UNI_SUSPEND_MODE_SETTING),
			.len = cpu_to_le16(sizeof(struct mt76_connac_suspend_tlv)),
			.enable = enable,
			.mdtim = mdtim,
			.wow_suspend = wow_suspend,
		},
	};

	return mt76_mcu_send_msg(dev, MCU_UNI_CMD_SUSPEND, &req, sizeof(req),
				 true);
}

static int
mt76_connac_mcu_set_wow_pattern(struct mt76_dev *dev,
				struct ieee80211_vif *vif,
				u8 index, bool enable,
				struct cfg80211_pkt_pattern *pattern)
{
	struct mt76_vif *mvif = (struct mt76_vif *)vif->drv_priv;
	struct mt76_connac_wow_pattern_tlv *ptlv;
	struct sk_buff *skb;
	struct req_hdr {
		u8 bss_idx;
		u8 pad[3];
	} __packed hdr = {
		.bss_idx = mvif->idx,
	};

	skb = mt76_mcu_msg_alloc(dev, NULL, sizeof(hdr) + sizeof(*ptlv));
	if (!skb)
		return -ENOMEM;

	skb_put_data(skb, &hdr, sizeof(hdr));
	ptlv = (struct mt76_connac_wow_pattern_tlv *)skb_put(skb, sizeof(*ptlv));
	ptlv->tag = cpu_to_le16(UNI_SUSPEND_WOW_PATTERN);
	ptlv->len = cpu_to_le16(sizeof(*ptlv));
	ptlv->data_len = pattern->pattern_len;
	ptlv->enable = enable;
	ptlv->index = index;

	memcpy(ptlv->pattern, pattern->pattern, pattern->pattern_len);
	memcpy(ptlv->mask, pattern->mask, DIV_ROUND_UP(pattern->pattern_len, 8));

	return mt76_mcu_skb_send_msg(dev, skb, MCU_UNI_CMD_SUSPEND, true);
}

static int
mt76_connac_mcu_set_wow_ctrl(struct mt76_phy *phy, struct ieee80211_vif *vif,
			     bool suspend, struct cfg80211_wowlan *wowlan)
{
	struct mt76_vif *mvif = (struct mt76_vif *)vif->drv_priv;
	struct mt76_dev *dev = phy->dev;
	struct {
		struct {
			u8 bss_idx;
			u8 pad[3];
		} __packed hdr;
		struct mt76_connac_wow_ctrl_tlv wow_ctrl_tlv;
		struct mt76_connac_wow_gpio_param_tlv gpio_tlv;
	} req = {
		.hdr = {
			.bss_idx = mvif->idx,
		},
		.wow_ctrl_tlv = {
			.tag = cpu_to_le16(UNI_SUSPEND_WOW_CTRL),
			.len = cpu_to_le16(sizeof(struct mt76_connac_wow_ctrl_tlv)),
			.cmd = suspend ? 1 : 2,
		},
		.gpio_tlv = {
			.tag = cpu_to_le16(UNI_SUSPEND_WOW_GPIO_PARAM),
			.len = cpu_to_le16(sizeof(struct mt76_connac_wow_gpio_param_tlv)),
			.gpio_pin = 0xff, /* follow fw about GPIO pin */
		},
	};

	if (wowlan->magic_pkt)
		req.wow_ctrl_tlv.trigger |= UNI_WOW_DETECT_TYPE_MAGIC;
	if (wowlan->disconnect)
		req.wow_ctrl_tlv.trigger |= (UNI_WOW_DETECT_TYPE_DISCONNECT |
					     UNI_WOW_DETECT_TYPE_BCN_LOST);
	if (wowlan->nd_config) {
		mt76_connac_mcu_sched_scan_req(phy, vif, wowlan->nd_config);
		req.wow_ctrl_tlv.trigger |= UNI_WOW_DETECT_TYPE_SCH_SCAN_HIT;
		mt76_connac_mcu_sched_scan_enable(phy, vif, suspend);
	}
	if (wowlan->n_patterns)
		req.wow_ctrl_tlv.trigger |= UNI_WOW_DETECT_TYPE_BITMAP;

	if (mt76_is_mmio(dev))
		req.wow_ctrl_tlv.wakeup_hif = WOW_PCIE;
	else if (mt76_is_usb(dev))
		req.wow_ctrl_tlv.wakeup_hif = WOW_USB;
	else if (mt76_is_sdio(dev))
		req.wow_ctrl_tlv.wakeup_hif = WOW_GPIO;

	return mt76_mcu_send_msg(dev, MCU_UNI_CMD_SUSPEND, &req, sizeof(req),
				 true);
}

int mt76_connac_mcu_set_hif_suspend(struct mt76_dev *dev, bool suspend)
{
	struct {
		struct {
			u8 hif_type; /* 0x0: HIF_SDIO
				      * 0x1: HIF_USB
				      * 0x2: HIF_PCIE
				      */
			u8 pad[3];
		} __packed hdr;
		struct hif_suspend_tlv {
			__le16 tag;
			__le16 len;
			u8 suspend;
		} __packed hif_suspend;
	} req = {
		.hif_suspend = {
			.tag = cpu_to_le16(0), /* 0: UNI_HIF_CTRL_BASIC */
			.len = cpu_to_le16(sizeof(struct hif_suspend_tlv)),
			.suspend = suspend,
		},
	};

	if (mt76_is_mmio(dev))
		req.hdr.hif_type = 2;
	else if (mt76_is_usb(dev))
		req.hdr.hif_type = 1;
	else if (mt76_is_sdio(dev))
		req.hdr.hif_type = 0;

	return mt76_mcu_send_msg(dev, MCU_UNI_CMD_HIF_CTRL, &req, sizeof(req),
				 true);
}
EXPORT_SYMBOL_GPL(mt76_connac_mcu_set_hif_suspend);

void mt76_connac_mcu_set_suspend_iter(void *priv, u8 *mac,
				      struct ieee80211_vif *vif)
{
	struct mt76_phy *phy = priv;
	bool suspend = test_bit(MT76_STATE_SUSPEND, &phy->state);
	struct ieee80211_hw *hw = phy->hw;
	struct cfg80211_wowlan *wowlan = hw->wiphy->wowlan_config;
	int i;

	mt76_connac_mcu_set_gtk_rekey(phy->dev, vif, suspend);
	mt76_connac_mcu_set_arp_filter(phy->dev, vif, suspend);

	mt76_connac_mcu_set_suspend_mode(phy->dev, vif, suspend, 1, true);

	for (i = 0; i < wowlan->n_patterns; i++)
		mt76_connac_mcu_set_wow_pattern(phy->dev, vif, i, suspend,
						&wowlan->patterns[i]);
	mt76_connac_mcu_set_wow_ctrl(phy, vif, suspend, wowlan);
}
EXPORT_SYMBOL_GPL(mt76_connac_mcu_set_suspend_iter);

#endif /* CONFIG_PM */

MODULE_AUTHOR("Lorenzo Bianconi <lorenzo@kernel.org>");
MODULE_LICENSE("Dual BSD/GPL");
