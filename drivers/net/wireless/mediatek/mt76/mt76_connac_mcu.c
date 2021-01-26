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

	return mt76_mcu_send_msg(dev, MCU_CMD_TARGET_ADDRESS_LEN_REQ, &req,
				 sizeof(req), true);
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
		.wlan_idx_lo = wcid ? wcid->idx : 0,
		.is_tlv_append = 1,
	};
	struct sk_buff *skb;

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
		.wlan_idx_lo = wcid ? wcid->idx : 0,
		.operation = cmd,
	};
	struct sk_buff *nskb = *skb;

	if (!nskb) {
		nskb = mt76_mcu_msg_alloc(dev, NULL,
					  MT76_CONNAC_WTBL_UPDATE_BA_SIZE);
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
				   bool enable)
{
	struct sta_rec_basic *basic;
	struct tlv *tlv;
	int conn_type;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_BASIC, sizeof(*basic));

	basic = (struct sta_rec_basic *)tlv;
	basic->extra_info = cpu_to_le16(EXTRA_INFO_VER);

	if (enable) {
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
		eth_broadcast_addr(generic->peer_addr);
		generic->muar_idx = 0xe;
	}

	tlv = mt76_connac_mcu_add_nested_tlv(skb, WTBL_RX, sizeof(*rx),
					     wtbl_tlv, sta_wtbl);

	rx = (struct wtbl_rx *)tlv;
	rx->rca1 = sta ? vif->type != NL80211_IFTYPE_AP : 1;
	rx->rca2 = 1;
	rx->rv = 1;

	tlv = mt76_connac_mcu_add_nested_tlv(skb, WTBL_SPE, sizeof(*spe),
					     wtbl_tlv, sta_wtbl);
	spe = (struct wtbl_spe *)tlv;
	spe->spe_idx = 24;
}
EXPORT_SYMBOL_GPL(mt76_connac_mcu_wtbl_generic_tlv);

void mt76_connac_mcu_sta_tlv(struct mt76_phy *mphy, struct sk_buff *skb,
			     struct ieee80211_sta *sta,
			     struct ieee80211_vif *vif)
{
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

		tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_VHT,
					      sizeof(*vht) - 4);
		vht = (struct sta_rec_vht *)tlv;
		vht->vht_cap = cpu_to_le32(sta->vht_cap.cap);
		vht->vht_rx_mcs_map = sta->vht_cap.vht_mcs.rx_mcs_map;
		vht->vht_tx_mcs_map = sta->vht_cap.vht_mcs.tx_mcs_map;
	}

	/* starec uapsd */
	mt76_connac_mcu_sta_uapsd(skb, vif, sta);
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

	if (sta->ht_cap.ht_supported) {
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

int mt76_connac_mcu_add_sta_cmd(struct mt76_phy *phy,
				struct ieee80211_vif *vif,
				struct ieee80211_sta *sta,
				struct mt76_wcid *wcid,
				bool enable, int cmd)
{
	struct mt76_vif *mvif = (struct mt76_vif *)vif->drv_priv;
	struct mt76_dev *dev = phy->dev;
	struct wtbl_req_hdr *wtbl_hdr;
	struct tlv *sta_wtbl;
	struct sk_buff *skb;

	skb = mt76_connac_mcu_alloc_sta_req(dev, mvif, wcid);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	mt76_connac_mcu_sta_basic_tlv(skb, vif, sta, enable);
	if (enable && sta)
		mt76_connac_mcu_sta_tlv(phy, skb, sta, vif);

	sta_wtbl = mt76_connac_mcu_add_tlv(skb, STA_REC_WTBL,
					   sizeof(struct tlv));

	wtbl_hdr = mt76_connac_mcu_alloc_wtbl_req(dev, wcid,
						  WTBL_RESET_AND_SET,
						  sta_wtbl, &skb);
	if (enable) {
		mt76_connac_mcu_wtbl_generic_tlv(dev, skb, vif, sta, sta_wtbl,
						 wtbl_hdr);
		if (sta)
			mt76_connac_mcu_wtbl_ht_tlv(dev, skb, sta, sta_wtbl,
						    wtbl_hdr);
	}

	return mt76_mcu_skb_send_msg(dev, skb, cmd, true);
}
EXPORT_SYMBOL_GPL(mt76_connac_mcu_add_sta_cmd);

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
			.phymode = 0x38,
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
		} he_req = {
			.hdr = {
				.bss_idx = mvif->idx,
			},
			.he = {
				.tag = cpu_to_le16(UNI_BSS_INFO_HE_BASIC),
				.len = cpu_to_le16(sizeof(struct bss_info_uni_he)),
			},
		};

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
	int n_ssids = 0, err, i, duration = MT76_CONNAC_SCAN_CHANNEL_TIME;
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

	memcpy(req->bssid, sreq->bssid, ETH_ALEN);
	if (sreq->flags & NL80211_SCAN_FLAG_RANDOM_ADDR) {
		get_random_mask_addr(req->random_mac, sreq->mac_addr,
				     sreq->mac_addr_mask);
		req->scan_func = 1;
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
		get_random_mask_addr(req->random_mac, sreq->mac_addr,
				     sreq->mac_addr_mask);
		req->scan_func = 1;
	}

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

MODULE_AUTHOR("Lorenzo Bianconi <lorenzo@kernel.org>");
MODULE_LICENSE("Dual BSD/GPL");
