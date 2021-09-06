// SPDX-License-Identifier: ISC
/* Copyright (C) 2020 MediaTek Inc. */

#include <linux/firmware.h>
#include <linux/fs.h>
#include "mt7915.h"
#include "mcu.h"
#include "mac.h"
#include "eeprom.h"

struct mt7915_patch_hdr {
	char build_date[16];
	char platform[4];
	__be32 hw_sw_ver;
	__be32 patch_ver;
	__be16 checksum;
	u16 reserved;
	struct {
		__be32 patch_ver;
		__be32 subsys;
		__be32 feature;
		__be32 n_region;
		__be32 crc;
		u32 reserved[11];
	} desc;
} __packed;

struct mt7915_patch_sec {
	__be32 type;
	__be32 offs;
	__be32 size;
	union {
		__be32 spec[13];
		struct {
			__be32 addr;
			__be32 len;
			__be32 sec_key_idx;
			__be32 align_len;
			u32 reserved[9];
		} info;
	};
} __packed;

struct mt7915_fw_trailer {
	u8 chip_id;
	u8 eco_code;
	u8 n_region;
	u8 format_ver;
	u8 format_flag;
	u8 reserved[2];
	char fw_ver[10];
	char build_date[15];
	u32 crc;
} __packed;

struct mt7915_fw_region {
	__le32 decomp_crc;
	__le32 decomp_len;
	__le32 decomp_blk_sz;
	u8 reserved[4];
	__le32 addr;
	__le32 len;
	u8 feature_set;
	u8 reserved1[15];
} __packed;

#define MCU_PATCH_ADDRESS		0x200000

#define FW_FEATURE_SET_ENCRYPT		BIT(0)
#define FW_FEATURE_SET_KEY_IDX		GENMASK(2, 1)
#define FW_FEATURE_OVERRIDE_ADDR	BIT(5)

#define DL_MODE_ENCRYPT			BIT(0)
#define DL_MODE_KEY_IDX			GENMASK(2, 1)
#define DL_MODE_RESET_SEC_IV		BIT(3)
#define DL_MODE_WORKING_PDA_CR4		BIT(4)
#define DL_MODE_NEED_RSP		BIT(31)

#define FW_START_OVERRIDE		BIT(0)
#define FW_START_WORKING_PDA_CR4	BIT(2)

#define PATCH_SEC_TYPE_MASK		GENMASK(15, 0)
#define PATCH_SEC_TYPE_INFO		0x2

#define to_wcid_lo(id)			FIELD_GET(GENMASK(7, 0), (u16)id)
#define to_wcid_hi(id)			FIELD_GET(GENMASK(9, 8), (u16)id)

#define HE_PHY(p, c)			u8_get_bits(c, IEEE80211_HE_PHY_##p)
#define HE_MAC(m, c)			u8_get_bits(c, IEEE80211_HE_MAC_##m)

static enum mt7915_cipher_type
mt7915_mcu_get_cipher(int cipher)
{
	switch (cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
		return MT_CIPHER_WEP40;
	case WLAN_CIPHER_SUITE_WEP104:
		return MT_CIPHER_WEP104;
	case WLAN_CIPHER_SUITE_TKIP:
		return MT_CIPHER_TKIP;
	case WLAN_CIPHER_SUITE_AES_CMAC:
		return MT_CIPHER_BIP_CMAC_128;
	case WLAN_CIPHER_SUITE_CCMP:
		return MT_CIPHER_AES_CCMP;
	case WLAN_CIPHER_SUITE_CCMP_256:
		return MT_CIPHER_CCMP_256;
	case WLAN_CIPHER_SUITE_GCMP:
		return MT_CIPHER_GCMP;
	case WLAN_CIPHER_SUITE_GCMP_256:
		return MT_CIPHER_GCMP_256;
	case WLAN_CIPHER_SUITE_SMS4:
		return MT_CIPHER_WAPI;
	default:
		return MT_CIPHER_NONE;
	}
}

static u8 mt7915_mcu_chan_bw(struct cfg80211_chan_def *chandef)
{
	static const u8 width_to_bw[] = {
		[NL80211_CHAN_WIDTH_40] = CMD_CBW_40MHZ,
		[NL80211_CHAN_WIDTH_80] = CMD_CBW_80MHZ,
		[NL80211_CHAN_WIDTH_80P80] = CMD_CBW_8080MHZ,
		[NL80211_CHAN_WIDTH_160] = CMD_CBW_160MHZ,
		[NL80211_CHAN_WIDTH_5] = CMD_CBW_5MHZ,
		[NL80211_CHAN_WIDTH_10] = CMD_CBW_10MHZ,
		[NL80211_CHAN_WIDTH_20] = CMD_CBW_20MHZ,
		[NL80211_CHAN_WIDTH_20_NOHT] = CMD_CBW_20MHZ,
	};

	if (chandef->width >= ARRAY_SIZE(width_to_bw))
		return 0;

	return width_to_bw[chandef->width];
}

static const struct ieee80211_sta_he_cap *
mt7915_get_he_phy_cap(struct mt7915_phy *phy, struct ieee80211_vif *vif)
{
	struct ieee80211_supported_band *sband;
	enum nl80211_band band;

	band = phy->mt76->chandef.chan->band;
	sband = phy->mt76->hw->wiphy->bands[band];

	return ieee80211_get_he_iftype_cap(sband, vif->type);
}

static u8
mt7915_get_phy_mode(struct mt76_phy *mphy, struct ieee80211_vif *vif,
		    struct ieee80211_sta *sta)
{
	enum nl80211_band band = mphy->chandef.chan->band;
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

static u8
mt7915_mcu_get_sta_nss(u16 mcs_map)
{
	u8 nss;

	for (nss = 8; nss > 0; nss--) {
		u8 nss_mcs = (mcs_map >> (2 * (nss - 1))) & 3;

		if (nss_mcs != IEEE80211_VHT_MCS_NOT_SUPPORTED)
			break;
	}

	return nss - 1;
}

static int
mt7915_mcu_parse_response(struct mt76_dev *mdev, int cmd,
			  struct sk_buff *skb, int seq)
{
	struct mt7915_mcu_rxd *rxd;
	int ret = 0;

	if (!skb) {
		dev_err(mdev->dev, "Message %08x (seq %d) timeout\n",
			cmd, seq);
		return -ETIMEDOUT;
	}

	rxd = (struct mt7915_mcu_rxd *)skb->data;
	if (seq != rxd->seq)
		return -EAGAIN;

	if (cmd == MCU_CMD(PATCH_SEM_CONTROL)) {
		skb_pull(skb, sizeof(*rxd) - 4);
		ret = *skb->data;
	} else if (cmd == MCU_EXT_CMD(THERMAL_CTRL)) {
		skb_pull(skb, sizeof(*rxd) + 4);
		ret = le32_to_cpu(*(__le32 *)skb->data);
	} else {
		skb_pull(skb, sizeof(struct mt7915_mcu_rxd));
	}

	return ret;
}

static int
mt7915_mcu_send_message(struct mt76_dev *mdev, struct sk_buff *skb,
			int cmd, int *wait_seq)
{
	struct mt7915_dev *dev = container_of(mdev, struct mt7915_dev, mt76);
	struct mt7915_mcu_txd *mcu_txd;
	enum mt76_mcuq_id qid;
	__le32 *txd;
	u32 val;
	u8 seq;

	/* TODO: make dynamic based on msg type */
	mdev->mcu.timeout = 20 * HZ;

	seq = ++dev->mt76.mcu.msg_seq & 0xf;
	if (!seq)
		seq = ++dev->mt76.mcu.msg_seq & 0xf;

	if (cmd == MCU_CMD(FW_SCATTER)) {
		qid = MT_MCUQ_FWDL;
		goto exit;
	}

	mcu_txd = (struct mt7915_mcu_txd *)skb_push(skb, sizeof(*mcu_txd));
	if (test_bit(MT76_STATE_MCU_RUNNING, &dev->mphy.state))
		qid = MT_MCUQ_WA;
	else
		qid = MT_MCUQ_WM;

	txd = mcu_txd->txd;

	val = FIELD_PREP(MT_TXD0_TX_BYTES, skb->len) |
	      FIELD_PREP(MT_TXD0_PKT_FMT, MT_TX_TYPE_CMD) |
	      FIELD_PREP(MT_TXD0_Q_IDX, MT_TX_MCU_PORT_RX_Q0);
	txd[0] = cpu_to_le32(val);

	val = MT_TXD1_LONG_FORMAT |
	      FIELD_PREP(MT_TXD1_HDR_FORMAT, MT_HDR_FORMAT_CMD);
	txd[1] = cpu_to_le32(val);

	mcu_txd->len = cpu_to_le16(skb->len - sizeof(mcu_txd->txd));
	mcu_txd->pq_id = cpu_to_le16(MCU_PQ_ID(MT_TX_PORT_IDX_MCU,
					       MT_TX_MCU_PORT_RX_Q0));
	mcu_txd->pkt_type = MCU_PKT_ID;
	mcu_txd->seq = seq;

	mcu_txd->cid = FIELD_GET(__MCU_CMD_FIELD_ID, cmd);
	mcu_txd->set_query = MCU_Q_NA;
	mcu_txd->ext_cid = FIELD_GET(__MCU_CMD_FIELD_EXT_ID, cmd);
	if (mcu_txd->ext_cid) {
		mcu_txd->ext_cid_ack = 1;

		/* do not use Q_SET for efuse */
		if (cmd & __MCU_CMD_FIELD_QUERY)
			mcu_txd->set_query = MCU_Q_QUERY;
		else
			mcu_txd->set_query = MCU_Q_SET;
	}

	if (cmd & __MCU_CMD_FIELD_WA)
		mcu_txd->s2d_index = MCU_S2D_H2C;
	else
		mcu_txd->s2d_index = MCU_S2D_H2N;

exit:
	if (wait_seq)
		*wait_seq = seq;

	return mt76_tx_queue_skb_raw(dev, mdev->q_mcu[qid], skb, 0);
}

static void
mt7915_mcu_wa_cmd(struct mt7915_dev *dev, int cmd, u32 a1, u32 a2, u32 a3)
{
	struct {
		__le32 args[3];
	} req = {
		.args = {
			cpu_to_le32(a1),
			cpu_to_le32(a2),
			cpu_to_le32(a3),
		},
	};

	mt76_mcu_send_msg(&dev->mt76, cmd, &req, sizeof(req), true);
}

static void
mt7915_mcu_csa_finish(void *priv, u8 *mac, struct ieee80211_vif *vif)
{
	if (vif->csa_active)
		ieee80211_csa_finish(vif);
}

static void
mt7915_mcu_rx_csa_notify(struct mt7915_dev *dev, struct sk_buff *skb)
{
	struct mt76_phy *mphy = &dev->mt76.phy;
	struct mt7915_mcu_csa_notify *c;

	c = (struct mt7915_mcu_csa_notify *)skb->data;

	if (c->band_idx && dev->mt76.phy2)
		mphy = dev->mt76.phy2;

	ieee80211_iterate_active_interfaces_atomic(mphy->hw,
			IEEE80211_IFACE_ITER_RESUME_ALL,
			mt7915_mcu_csa_finish, mphy->hw);
}

static void
mt7915_mcu_rx_radar_detected(struct mt7915_dev *dev, struct sk_buff *skb)
{
	struct mt76_phy *mphy = &dev->mt76.phy;
	struct mt7915_mcu_rdd_report *r;

	r = (struct mt7915_mcu_rdd_report *)skb->data;

	if (r->band_idx && dev->mt76.phy2)
		mphy = dev->mt76.phy2;

	ieee80211_radar_detected(mphy->hw);
	dev->hw_pattern++;
}

static int
mt7915_mcu_tx_rate_parse(struct mt76_phy *mphy, struct mt7915_mcu_ra_info *ra,
			 struct rate_info *rate, u16 r)
{
	struct ieee80211_supported_band *sband;
	u16 ru_idx = le16_to_cpu(ra->ru_idx);
	bool cck = false;

	rate->mcs = FIELD_GET(MT_RA_RATE_MCS, r);
	rate->nss = FIELD_GET(MT_RA_RATE_NSS, r) + 1;

	switch (FIELD_GET(MT_RA_RATE_TX_MODE, r)) {
	case MT_PHY_TYPE_CCK:
		cck = true;
		fallthrough;
	case MT_PHY_TYPE_OFDM:
		if (mphy->chandef.chan->band == NL80211_BAND_5GHZ)
			sband = &mphy->sband_5g.sband;
		else
			sband = &mphy->sband_2g.sband;

		rate->mcs = mt76_get_rate(mphy->dev, sband, rate->mcs, cck);
		rate->legacy = sband->bitrates[rate->mcs].bitrate;
		break;
	case MT_PHY_TYPE_HT:
	case MT_PHY_TYPE_HT_GF:
		rate->mcs += (rate->nss - 1) * 8;
		if (rate->mcs > 31)
			return -EINVAL;

		rate->flags = RATE_INFO_FLAGS_MCS;
		if (ra->gi)
			rate->flags |= RATE_INFO_FLAGS_SHORT_GI;
		break;
	case MT_PHY_TYPE_VHT:
		if (rate->mcs > 9)
			return -EINVAL;

		rate->flags = RATE_INFO_FLAGS_VHT_MCS;
		if (ra->gi)
			rate->flags |= RATE_INFO_FLAGS_SHORT_GI;
		break;
	case MT_PHY_TYPE_HE_SU:
	case MT_PHY_TYPE_HE_EXT_SU:
	case MT_PHY_TYPE_HE_TB:
	case MT_PHY_TYPE_HE_MU:
		if (ra->gi > NL80211_RATE_INFO_HE_GI_3_2 || rate->mcs > 11)
			return -EINVAL;

		rate->he_gi = ra->gi;
		rate->he_dcm = FIELD_GET(MT_RA_RATE_DCM_EN, r);
		rate->flags = RATE_INFO_FLAGS_HE_MCS;
		break;
	default:
		return -EINVAL;
	}

	if (ru_idx) {
		switch (ru_idx) {
		case 1 ... 2:
			rate->he_ru_alloc = NL80211_RATE_INFO_HE_RU_ALLOC_996;
			break;
		case 3 ... 6:
			rate->he_ru_alloc = NL80211_RATE_INFO_HE_RU_ALLOC_484;
			break;
		case 7 ... 14:
			rate->he_ru_alloc = NL80211_RATE_INFO_HE_RU_ALLOC_242;
			break;
		default:
			rate->he_ru_alloc = NL80211_RATE_INFO_HE_RU_ALLOC_106;
			break;
		}
		rate->bw = RATE_INFO_BW_HE_RU;
	} else {
		u8 bw = mt7915_mcu_chan_bw(&mphy->chandef) -
			FIELD_GET(MT_RA_RATE_BW, r);

		switch (bw) {
		case IEEE80211_STA_RX_BW_160:
			rate->bw = RATE_INFO_BW_160;
			break;
		case IEEE80211_STA_RX_BW_80:
			rate->bw = RATE_INFO_BW_80;
			break;
		case IEEE80211_STA_RX_BW_40:
			rate->bw = RATE_INFO_BW_40;
			break;
		default:
			rate->bw = RATE_INFO_BW_20;
			break;
		}
	}

	return 0;
}

static void
mt7915_mcu_tx_rate_report(struct mt7915_dev *dev, struct sk_buff *skb)
{
	struct mt7915_mcu_ra_info *ra = (struct mt7915_mcu_ra_info *)skb->data;
	struct rate_info rate = {}, prob_rate = {};
	u16 probe = le16_to_cpu(ra->prob_up_rate);
	u16 attempts = le16_to_cpu(ra->attempts);
	u16 curr = le16_to_cpu(ra->curr_rate);
	u16 wcidx = le16_to_cpu(ra->wlan_idx);
	struct mt76_phy *mphy = &dev->mphy;
	struct mt7915_sta_stats *stats;
	struct mt7915_sta *msta;
	struct mt76_wcid *wcid;

	if (wcidx >= MT76_N_WCIDS)
		return;

	wcid = rcu_dereference(dev->mt76.wcid[wcidx]);
	if (!wcid)
		return;

	msta = container_of(wcid, struct mt7915_sta, wcid);
	stats = &msta->stats;

	if (msta->wcid.ext_phy && dev->mt76.phy2)
		mphy = dev->mt76.phy2;

	/* current rate */
	if (!mt7915_mcu_tx_rate_parse(mphy, ra, &rate, curr))
		stats->tx_rate = rate;

	/* probing rate */
	if (!mt7915_mcu_tx_rate_parse(mphy, ra, &prob_rate, probe))
		stats->prob_rate = prob_rate;

	if (attempts) {
		u16 success = le16_to_cpu(ra->success);

		stats->per = 1000 * (attempts - success) / attempts;
	}
}

static void
mt7915_mcu_rx_log_message(struct mt7915_dev *dev, struct sk_buff *skb)
{
	struct mt7915_mcu_rxd *rxd = (struct mt7915_mcu_rxd *)skb->data;
	const char *data = (char *)&rxd[1];
	const char *type;

	switch (rxd->s2d_index) {
	case 0:
		type = "WM";
		break;
	case 2:
		type = "WA";
		break;
	default:
		type = "unknown";
		break;
	}

	wiphy_info(mt76_hw(dev)->wiphy, "%s: %.*s", type,
		   (int)(skb->len - sizeof(*rxd)), data);
}

static void
mt7915_mcu_rx_ext_event(struct mt7915_dev *dev, struct sk_buff *skb)
{
	struct mt7915_mcu_rxd *rxd = (struct mt7915_mcu_rxd *)skb->data;

	switch (rxd->ext_eid) {
	case MCU_EXT_EVENT_RDD_REPORT:
		mt7915_mcu_rx_radar_detected(dev, skb);
		break;
	case MCU_EXT_EVENT_CSA_NOTIFY:
		mt7915_mcu_rx_csa_notify(dev, skb);
		break;
	case MCU_EXT_EVENT_RATE_REPORT:
		mt7915_mcu_tx_rate_report(dev, skb);
		break;
	case MCU_EXT_EVENT_FW_LOG_2_HOST:
		mt7915_mcu_rx_log_message(dev, skb);
		break;
	default:
		break;
	}
}

static void
mt7915_mcu_rx_unsolicited_event(struct mt7915_dev *dev, struct sk_buff *skb)
{
	struct mt7915_mcu_rxd *rxd = (struct mt7915_mcu_rxd *)skb->data;

	switch (rxd->eid) {
	case MCU_EVENT_EXT:
		mt7915_mcu_rx_ext_event(dev, skb);
		break;
	default:
		break;
	}
	dev_kfree_skb(skb);
}

void mt7915_mcu_rx_event(struct mt7915_dev *dev, struct sk_buff *skb)
{
	struct mt7915_mcu_rxd *rxd = (struct mt7915_mcu_rxd *)skb->data;

	if (rxd->ext_eid == MCU_EXT_EVENT_THERMAL_PROTECT ||
	    rxd->ext_eid == MCU_EXT_EVENT_FW_LOG_2_HOST ||
	    rxd->ext_eid == MCU_EXT_EVENT_ASSERT_DUMP ||
	    rxd->ext_eid == MCU_EXT_EVENT_PS_SYNC ||
	    rxd->ext_eid == MCU_EXT_EVENT_RATE_REPORT ||
	    !rxd->seq)
		mt7915_mcu_rx_unsolicited_event(dev, skb);
	else
		mt76_mcu_rx_event(&dev->mt76, skb);
}

static struct sk_buff *
mt7915_mcu_alloc_sta_req(struct mt7915_dev *dev, struct mt7915_vif *mvif,
			 struct mt7915_sta *msta, int len)
{
	struct sta_req_hdr hdr = {
		.bss_idx = mvif->idx,
		.wlan_idx_lo = msta ? to_wcid_lo(msta->wcid.idx) : 0,
		.wlan_idx_hi = msta ? to_wcid_hi(msta->wcid.idx) : 0,
		.muar_idx = msta ? mvif->omac_idx : 0,
		.is_tlv_append = 1,
	};
	struct sk_buff *skb;

	skb = mt76_mcu_msg_alloc(&dev->mt76, NULL, len);
	if (!skb)
		return ERR_PTR(-ENOMEM);

	skb_put_data(skb, &hdr, sizeof(hdr));

	return skb;
}

static struct wtbl_req_hdr *
mt7915_mcu_alloc_wtbl_req(struct mt7915_dev *dev, struct mt7915_sta *msta,
			  int cmd, void *sta_wtbl, struct sk_buff **skb)
{
	struct tlv *sta_hdr = sta_wtbl;
	struct wtbl_req_hdr hdr = {
		.wlan_idx_lo = to_wcid_lo(msta->wcid.idx),
		.wlan_idx_hi = to_wcid_hi(msta->wcid.idx),
		.operation = cmd,
	};
	struct sk_buff *nskb = *skb;

	if (!nskb) {
		nskb = mt76_mcu_msg_alloc(&dev->mt76, NULL,
					  MT7915_WTBL_UPDATE_MAX_SIZE);
		if (!nskb)
			return ERR_PTR(-ENOMEM);

		*skb = nskb;
	}

	if (sta_hdr)
		sta_hdr->len = cpu_to_le16(sizeof(hdr));

	return skb_put_data(nskb, &hdr, sizeof(hdr));
}

static struct tlv *
mt7915_mcu_add_nested_tlv(struct sk_buff *skb, int tag, int len,
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

static struct tlv *
mt7915_mcu_add_tlv(struct sk_buff *skb, int tag, int len)
{
	return mt7915_mcu_add_nested_tlv(skb, tag, len, skb->data, NULL);
}

static struct tlv *
mt7915_mcu_add_nested_subtlv(struct sk_buff *skb, int sub_tag, int sub_len,
			     __le16 *sub_ntlv, __le16 *len)
{
	struct tlv *ptlv, tlv = {
		.tag = cpu_to_le16(sub_tag),
		.len = cpu_to_le16(sub_len),
	};

	ptlv = skb_put(skb, sub_len);
	memcpy(ptlv, &tlv, sizeof(tlv));

	le16_add_cpu(sub_ntlv, 1);
	le16_add_cpu(len, sub_len);

	return ptlv;
}

/** bss info **/
static int
mt7915_mcu_bss_basic_tlv(struct sk_buff *skb, struct ieee80211_vif *vif,
			 struct mt7915_phy *phy, bool enable)
{
	struct mt7915_vif *mvif = (struct mt7915_vif *)vif->drv_priv;
	struct bss_info_basic *bss;
	u16 wlan_idx = mvif->sta.wcid.idx;
	u32 type = NETWORK_INFRA;
	struct tlv *tlv;

	tlv = mt7915_mcu_add_tlv(skb, BSS_INFO_BASIC, sizeof(*bss));

	switch (vif->type) {
	case NL80211_IFTYPE_MESH_POINT:
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_MONITOR:
		break;
	case NL80211_IFTYPE_STATION:
		/* TODO: enable BSS_INFO_UAPSD & BSS_INFO_PM */
		if (enable) {
			struct ieee80211_sta *sta;
			struct mt7915_sta *msta;

			rcu_read_lock();
			sta = ieee80211_find_sta(vif, vif->bss_conf.bssid);
			if (!sta) {
				rcu_read_unlock();
				return -EINVAL;
			}

			msta = (struct mt7915_sta *)sta->drv_priv;
			wlan_idx = msta->wcid.idx;
			rcu_read_unlock();
		}
		break;
	case NL80211_IFTYPE_ADHOC:
		type = NETWORK_IBSS;
		break;
	default:
		WARN_ON(1);
		break;
	}

	bss = (struct bss_info_basic *)tlv;
	bss->network_type = cpu_to_le32(type);
	bss->bmc_wcid_lo = to_wcid_lo(wlan_idx);
	bss->bmc_wcid_hi = to_wcid_hi(wlan_idx);
	bss->wmm_idx = mvif->wmm_idx;
	bss->active = enable;

	if (vif->type != NL80211_IFTYPE_MONITOR) {
		memcpy(bss->bssid, vif->bss_conf.bssid, ETH_ALEN);
		bss->bcn_interval = cpu_to_le16(vif->bss_conf.beacon_int);
		bss->dtim_period = vif->bss_conf.dtim_period;
		bss->phy_mode = mt7915_get_phy_mode(phy->mt76, vif, NULL);
	} else {
		memcpy(bss->bssid, phy->mt76->macaddr, ETH_ALEN);
	}

	return 0;
}

static void
mt7915_mcu_bss_omac_tlv(struct sk_buff *skb, struct ieee80211_vif *vif)
{
	struct mt7915_vif *mvif = (struct mt7915_vif *)vif->drv_priv;
	struct bss_info_omac *omac;
	struct tlv *tlv;
	u32 type = 0;
	u8 idx;

	tlv = mt7915_mcu_add_tlv(skb, BSS_INFO_OMAC, sizeof(*omac));

	switch (vif->type) {
	case NL80211_IFTYPE_MONITOR:
	case NL80211_IFTYPE_MESH_POINT:
	case NL80211_IFTYPE_AP:
		type = CONNECTION_INFRA_AP;
		break;
	case NL80211_IFTYPE_STATION:
		type = CONNECTION_INFRA_STA;
		break;
	case NL80211_IFTYPE_ADHOC:
		type = CONNECTION_IBSS_ADHOC;
		break;
	default:
		WARN_ON(1);
		break;
	}

	omac = (struct bss_info_omac *)tlv;
	idx = mvif->omac_idx > EXT_BSSID_START ? HW_BSSID_0 : mvif->omac_idx;
	omac->conn_type = cpu_to_le32(type);
	omac->omac_idx = mvif->omac_idx;
	omac->band_idx = mvif->band_idx;
	omac->hw_bss_idx = idx;
}

struct mt7915_he_obss_narrow_bw_ru_data {
	bool tolerated;
};

static void mt7915_check_he_obss_narrow_bw_ru_iter(struct wiphy *wiphy,
						   struct cfg80211_bss *bss,
						   void *_data)
{
	struct mt7915_he_obss_narrow_bw_ru_data *data = _data;
	const struct element *elem;

	elem = ieee80211_bss_get_elem(bss, WLAN_EID_EXT_CAPABILITY);

	if (!elem || elem->datalen < 10 ||
	    !(elem->data[10] &
	      WLAN_EXT_CAPA10_OBSS_NARROW_BW_RU_TOLERANCE_SUPPORT))
		data->tolerated = false;
}

static bool mt7915_check_he_obss_narrow_bw_ru(struct ieee80211_hw *hw,
					      struct ieee80211_vif *vif)
{
	struct mt7915_he_obss_narrow_bw_ru_data iter_data = {
		.tolerated = true,
	};

	if (!(vif->bss_conf.chandef.chan->flags & IEEE80211_CHAN_RADAR))
		return false;

	cfg80211_bss_iter(hw->wiphy, &vif->bss_conf.chandef,
			  mt7915_check_he_obss_narrow_bw_ru_iter,
			  &iter_data);

	/*
	 * If there is at least one AP on radar channel that cannot
	 * tolerate 26-tone RU UL OFDMA transmissions using HE TB PPDU.
	 */
	return !iter_data.tolerated;
}

static void
mt7915_mcu_bss_rfch_tlv(struct sk_buff *skb, struct ieee80211_vif *vif,
			struct mt7915_phy *phy)
{
	struct cfg80211_chan_def *chandef = &phy->mt76->chandef;
	struct bss_info_rf_ch *ch;
	struct tlv *tlv;
	int freq1 = chandef->center_freq1;

	tlv = mt7915_mcu_add_tlv(skb, BSS_INFO_RF_CH, sizeof(*ch));

	ch = (struct bss_info_rf_ch *)tlv;
	ch->pri_ch = chandef->chan->hw_value;
	ch->center_ch0 = ieee80211_frequency_to_channel(freq1);
	ch->bw = mt7915_mcu_chan_bw(chandef);

	if (chandef->width == NL80211_CHAN_WIDTH_80P80) {
		int freq2 = chandef->center_freq2;

		ch->center_ch1 = ieee80211_frequency_to_channel(freq2);
	}

	if (vif->bss_conf.he_support && vif->type == NL80211_IFTYPE_STATION) {
		struct mt7915_dev *dev = phy->dev;
		struct mt76_phy *mphy = &dev->mt76.phy;
		bool ext_phy = phy != &dev->phy;

		if (ext_phy && dev->mt76.phy2)
			mphy = dev->mt76.phy2;

		ch->he_ru26_block =
			mt7915_check_he_obss_narrow_bw_ru(mphy->hw, vif);
		ch->he_all_disable = false;
	} else {
		ch->he_all_disable = true;
	}
}

static void
mt7915_mcu_bss_ra_tlv(struct sk_buff *skb, struct ieee80211_vif *vif,
		      struct mt7915_phy *phy)
{
	int max_nss = hweight8(phy->mt76->chainmask);
	struct bss_info_ra *ra;
	struct tlv *tlv;

	tlv = mt7915_mcu_add_tlv(skb, BSS_INFO_RA, sizeof(*ra));

	ra = (struct bss_info_ra *)tlv;
	ra->op_mode = vif->type == NL80211_IFTYPE_AP;
	ra->adhoc_en = vif->type == NL80211_IFTYPE_ADHOC;
	ra->short_preamble = true;
	ra->tx_streams = max_nss;
	ra->rx_streams = max_nss;
	ra->algo = 4;
	ra->train_up_rule = 2;
	ra->train_up_high_thres = 110;
	ra->train_up_rule_rssi = -70;
	ra->low_traffic_thres = 2;
	ra->phy_cap = cpu_to_le32(0xfdf);
	ra->interval = cpu_to_le32(500);
	ra->fast_interval = cpu_to_le32(100);
}

static void
mt7915_mcu_bss_he_tlv(struct sk_buff *skb, struct ieee80211_vif *vif,
		      struct mt7915_phy *phy)
{
#define DEFAULT_HE_PE_DURATION		4
#define DEFAULT_HE_DURATION_RTS_THRES	1023
	const struct ieee80211_sta_he_cap *cap;
	struct bss_info_he *he;
	struct tlv *tlv;

	cap = mt7915_get_he_phy_cap(phy, vif);

	tlv = mt7915_mcu_add_tlv(skb, BSS_INFO_HE_BASIC, sizeof(*he));

	he = (struct bss_info_he *)tlv;
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

static void
mt7915_mcu_bss_hw_amsdu_tlv(struct sk_buff *skb)
{
#define TXD_CMP_MAP1		GENMASK(15, 0)
#define TXD_CMP_MAP2		(GENMASK(31, 0) & ~BIT(23))
	struct bss_info_hw_amsdu *amsdu;
	struct tlv *tlv;

	tlv = mt7915_mcu_add_tlv(skb, BSS_INFO_HW_AMSDU, sizeof(*amsdu));

	amsdu = (struct bss_info_hw_amsdu *)tlv;
	amsdu->cmp_bitmap_0 = cpu_to_le32(TXD_CMP_MAP1);
	amsdu->cmp_bitmap_1 = cpu_to_le32(TXD_CMP_MAP2);
	amsdu->trig_thres = cpu_to_le16(2);
	amsdu->enable = true;
}

static void
mt7915_mcu_bss_ext_tlv(struct sk_buff *skb, struct mt7915_vif *mvif)
{
/* SIFS 20us + 512 byte beacon tranmitted by 1Mbps (3906us) */
#define BCN_TX_ESTIMATE_TIME	(4096 + 20)
	struct bss_info_ext_bss *ext;
	int ext_bss_idx, tsf_offset;
	struct tlv *tlv;

	ext_bss_idx = mvif->omac_idx - EXT_BSSID_START;
	if (ext_bss_idx < 0)
		return;

	tlv = mt7915_mcu_add_tlv(skb, BSS_INFO_EXT_BSS, sizeof(*ext));

	ext = (struct bss_info_ext_bss *)tlv;
	tsf_offset = ext_bss_idx * BCN_TX_ESTIMATE_TIME;
	ext->mbss_tsf_offset = cpu_to_le32(tsf_offset);
}

static void
mt7915_mcu_bss_bmc_tlv(struct sk_buff *skb, struct mt7915_phy *phy)
{
	struct bss_info_bmc_rate *bmc;
	struct cfg80211_chan_def *chandef = &phy->mt76->chandef;
	enum nl80211_band band = chandef->chan->band;
	struct tlv *tlv;

	tlv = mt7915_mcu_add_tlv(skb, BSS_INFO_BMC_RATE, sizeof(*bmc));

	bmc = (struct bss_info_bmc_rate *)tlv;
	if (band == NL80211_BAND_2GHZ) {
		bmc->short_preamble = true;
	} else {
		bmc->bc_trans = cpu_to_le16(0x2000);
		bmc->mc_trans = cpu_to_le16(0x2080);
	}
}

static int
mt7915_mcu_muar_config(struct mt7915_phy *phy, struct ieee80211_vif *vif,
		       bool bssid, bool enable)
{
	struct mt7915_dev *dev = phy->dev;
	struct mt7915_vif *mvif = (struct mt7915_vif *)vif->drv_priv;
	u32 idx = mvif->omac_idx - REPEATER_BSSID_START;
	u32 mask = phy->omac_mask >> 32 & ~BIT(idx);
	const u8 *addr = vif->addr;
	struct {
		u8 mode;
		u8 force_clear;
		u8 clear_bitmap[8];
		u8 entry_count;
		u8 write;
		u8 band;

		u8 index;
		u8 bssid;
		u8 addr[ETH_ALEN];
	} __packed req = {
		.mode = !!mask || enable,
		.entry_count = 1,
		.write = 1,
		.band = phy != &dev->phy,
		.index = idx * 2 + bssid,
	};

	if (bssid)
		addr = vif->bss_conf.bssid;

	if (enable)
		ether_addr_copy(req.addr, addr);

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(MUAR_UPDATE), &req,
				 sizeof(req), true);
}

int mt7915_mcu_add_bss_info(struct mt7915_phy *phy,
			    struct ieee80211_vif *vif, int enable)
{
	struct mt7915_vif *mvif = (struct mt7915_vif *)vif->drv_priv;
	struct sk_buff *skb;

	if (mvif->omac_idx >= REPEATER_BSSID_START) {
		mt7915_mcu_muar_config(phy, vif, false, enable);
		mt7915_mcu_muar_config(phy, vif, true, enable);
	}

	skb = mt7915_mcu_alloc_sta_req(phy->dev, mvif, NULL,
				       MT7915_BSS_UPDATE_MAX_SIZE);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	/* bss_omac must be first */
	if (enable)
		mt7915_mcu_bss_omac_tlv(skb, vif);

	mt7915_mcu_bss_basic_tlv(skb, vif, phy, enable);

	if (vif->type == NL80211_IFTYPE_MONITOR)
		goto out;

	if (enable) {
		mt7915_mcu_bss_rfch_tlv(skb, vif, phy);
		mt7915_mcu_bss_bmc_tlv(skb, phy);
		mt7915_mcu_bss_ra_tlv(skb, vif, phy);
		mt7915_mcu_bss_hw_amsdu_tlv(skb);

		if (vif->bss_conf.he_support)
			mt7915_mcu_bss_he_tlv(skb, vif, phy);

		if (mvif->omac_idx >= EXT_BSSID_START &&
		    mvif->omac_idx < REPEATER_BSSID_START)
			mt7915_mcu_bss_ext_tlv(skb, mvif);
	}
out:
	return mt76_mcu_skb_send_msg(&phy->dev->mt76, skb,
				     MCU_EXT_CMD(BSS_INFO_UPDATE), true);
}

/** starec & wtbl **/
static int
mt7915_mcu_sta_key_tlv(struct mt7915_sta *msta, struct sk_buff *skb,
		       struct ieee80211_key_conf *key, enum set_key_cmd cmd)
{
	struct mt7915_sta_key_conf *bip = &msta->bip;
	struct sta_rec_sec *sec;
	struct tlv *tlv;
	u32 len = sizeof(*sec);

	tlv = mt7915_mcu_add_tlv(skb, STA_REC_KEY_V2, sizeof(*sec));

	sec = (struct sta_rec_sec *)tlv;
	sec->add = cmd;

	if (cmd == SET_KEY) {
		struct sec_key *sec_key;
		u8 cipher;

		cipher = mt7915_mcu_get_cipher(key->cipher);
		if (cipher == MT_CIPHER_NONE)
			return -EOPNOTSUPP;

		sec_key = &sec->key[0];
		sec_key->cipher_len = sizeof(*sec_key);

		if (cipher == MT_CIPHER_BIP_CMAC_128) {
			sec_key->cipher_id = MT_CIPHER_AES_CCMP;
			sec_key->key_id = bip->keyidx;
			sec_key->key_len = 16;
			memcpy(sec_key->key, bip->key, 16);

			sec_key = &sec->key[1];
			sec_key->cipher_id = MT_CIPHER_BIP_CMAC_128;
			sec_key->cipher_len = sizeof(*sec_key);
			sec_key->key_len = 16;
			memcpy(sec_key->key, key->key, 16);

			sec->n_cipher = 2;
		} else {
			sec_key->cipher_id = cipher;
			sec_key->key_id = key->keyidx;
			sec_key->key_len = key->keylen;
			memcpy(sec_key->key, key->key, key->keylen);

			if (cipher == MT_CIPHER_TKIP) {
				/* Rx/Tx MIC keys are swapped */
				memcpy(sec_key->key + 16, key->key + 24, 8);
				memcpy(sec_key->key + 24, key->key + 16, 8);
			}

			/* store key_conf for BIP batch update */
			if (cipher == MT_CIPHER_AES_CCMP) {
				memcpy(bip->key, key->key, key->keylen);
				bip->keyidx = key->keyidx;
			}

			len -= sizeof(*sec_key);
			sec->n_cipher = 1;
		}
	} else {
		len -= sizeof(sec->key);
		sec->n_cipher = 0;
	}
	sec->len = cpu_to_le16(len);

	return 0;
}

int mt7915_mcu_add_key(struct mt7915_dev *dev, struct ieee80211_vif *vif,
		       struct mt7915_sta *msta, struct ieee80211_key_conf *key,
		       enum set_key_cmd cmd)
{
	struct mt7915_vif *mvif = (struct mt7915_vif *)vif->drv_priv;
	struct sk_buff *skb;
	int len = sizeof(struct sta_req_hdr) + sizeof(struct sta_rec_sec);
	int ret;

	skb = mt7915_mcu_alloc_sta_req(dev, mvif, msta, len);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	ret = mt7915_mcu_sta_key_tlv(msta, skb, key, cmd);
	if (ret)
		return ret;

	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_EXT_CMD(STA_REC_UPDATE), true);
}

static void
mt7915_mcu_sta_ba_tlv(struct sk_buff *skb,
		      struct ieee80211_ampdu_params *params,
		      bool enable, bool tx)
{
	struct sta_rec_ba *ba;
	struct tlv *tlv;

	tlv = mt7915_mcu_add_tlv(skb, STA_REC_BA, sizeof(*ba));

	ba = (struct sta_rec_ba *)tlv;
	ba->ba_type = tx ? MT_BA_TYPE_ORIGINATOR : MT_BA_TYPE_RECIPIENT;
	ba->winsize = cpu_to_le16(params->buf_size);
	ba->ssn = cpu_to_le16(params->ssn);
	ba->ba_en = enable << params->tid;
	ba->amsdu = params->amsdu;
	ba->tid = params->tid;
}

static void
mt7915_mcu_wtbl_ba_tlv(struct sk_buff *skb,
		       struct ieee80211_ampdu_params *params,
		       bool enable, bool tx, void *sta_wtbl,
		       void *wtbl_tlv)
{
	struct wtbl_ba *ba;
	struct tlv *tlv;

	tlv = mt7915_mcu_add_nested_tlv(skb, WTBL_BA, sizeof(*ba),
					wtbl_tlv, sta_wtbl);

	ba = (struct wtbl_ba *)tlv;
	ba->tid = params->tid;

	if (tx) {
		ba->ba_type = MT_BA_TYPE_ORIGINATOR;
		ba->sn = enable ? cpu_to_le16(params->ssn) : 0;
		ba->ba_en = enable;
	} else {
		memcpy(ba->peer_addr, params->sta->addr, ETH_ALEN);
		ba->ba_type = MT_BA_TYPE_RECIPIENT;
		ba->rst_ba_tid = params->tid;
		ba->rst_ba_sel = RST_BA_MAC_TID_MATCH;
		ba->rst_ba_sb = 1;
	}

	if (enable && tx)
		ba->ba_winsize = cpu_to_le16(params->buf_size);
}

static int
mt7915_mcu_sta_ba(struct mt7915_dev *dev,
		  struct ieee80211_ampdu_params *params,
		  bool enable, bool tx)
{
	struct mt7915_sta *msta = (struct mt7915_sta *)params->sta->drv_priv;
	struct mt7915_vif *mvif = msta->vif;
	struct wtbl_req_hdr *wtbl_hdr;
	struct tlv *sta_wtbl;
	struct sk_buff *skb;
	int ret;

	if (enable && tx && !params->amsdu)
		msta->wcid.amsdu = false;

	skb = mt7915_mcu_alloc_sta_req(dev, mvif, msta,
				       MT7915_STA_UPDATE_MAX_SIZE);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	sta_wtbl = mt7915_mcu_add_tlv(skb, STA_REC_WTBL, sizeof(struct tlv));

	wtbl_hdr = mt7915_mcu_alloc_wtbl_req(dev, msta, WTBL_SET, sta_wtbl,
					     &skb);
	if (IS_ERR(wtbl_hdr))
		return PTR_ERR(wtbl_hdr);

	mt7915_mcu_wtbl_ba_tlv(skb, params, enable, tx, sta_wtbl, wtbl_hdr);

	ret = mt76_mcu_skb_send_msg(&dev->mt76, skb,
				    MCU_EXT_CMD(STA_REC_UPDATE), true);
	if (ret)
		return ret;

	skb = mt7915_mcu_alloc_sta_req(dev, mvif, msta,
				       MT7915_STA_UPDATE_MAX_SIZE);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	mt7915_mcu_sta_ba_tlv(skb, params, enable, tx);

	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_EXT_CMD(STA_REC_UPDATE), true);
}

int mt7915_mcu_add_tx_ba(struct mt7915_dev *dev,
			 struct ieee80211_ampdu_params *params,
			 bool enable)
{
	return mt7915_mcu_sta_ba(dev, params, enable, true);
}

int mt7915_mcu_add_rx_ba(struct mt7915_dev *dev,
			 struct ieee80211_ampdu_params *params,
			 bool enable)
{
	return mt7915_mcu_sta_ba(dev, params, enable, false);
}

static void
mt7915_mcu_wtbl_generic_tlv(struct sk_buff *skb, struct ieee80211_vif *vif,
			    struct ieee80211_sta *sta, void *sta_wtbl,
			    void *wtbl_tlv)
{
	struct mt7915_vif *mvif = (struct mt7915_vif *)vif->drv_priv;
	struct wtbl_generic *generic;
	struct wtbl_rx *rx;
	struct tlv *tlv;

	tlv = mt7915_mcu_add_nested_tlv(skb, WTBL_GENERIC, sizeof(*generic),
					wtbl_tlv, sta_wtbl);

	generic = (struct wtbl_generic *)tlv;

	if (sta) {
		memcpy(generic->peer_addr, sta->addr, ETH_ALEN);
		generic->partial_aid = cpu_to_le16(sta->aid);
		generic->muar_idx = mvif->omac_idx;
		generic->qos = sta->wme;
	} else {
		/* use BSSID in station mode */
		if (vif->type == NL80211_IFTYPE_STATION)
			memcpy(generic->peer_addr, vif->bss_conf.bssid,
			       ETH_ALEN);
		else
			eth_broadcast_addr(generic->peer_addr);

		generic->muar_idx = 0xe;
	}

	tlv = mt7915_mcu_add_nested_tlv(skb, WTBL_RX, sizeof(*rx),
					wtbl_tlv, sta_wtbl);

	rx = (struct wtbl_rx *)tlv;
	rx->rca1 = sta ? vif->type != NL80211_IFTYPE_AP : 1;
	rx->rca2 = 1;
	rx->rv = 1;
}

static void
mt7915_mcu_sta_basic_tlv(struct sk_buff *skb, struct ieee80211_vif *vif,
			 struct ieee80211_sta *sta, bool enable)
{
#define EXTRA_INFO_VER          BIT(0)
#define EXTRA_INFO_NEW          BIT(1)
	struct sta_rec_basic *basic;
	struct tlv *tlv;

	tlv = mt7915_mcu_add_tlv(skb, STA_REC_BASIC, sizeof(*basic));

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
		basic->conn_type = cpu_to_le32(CONNECTION_INFRA_STA);
		break;
	case NL80211_IFTYPE_STATION:
		basic->conn_type = cpu_to_le32(CONNECTION_INFRA_AP);
		break;
	case NL80211_IFTYPE_ADHOC:
		basic->conn_type = cpu_to_le32(CONNECTION_IBSS_ADHOC);
		break;
	default:
		WARN_ON(1);
		break;
	}

	memcpy(basic->peer_addr, sta->addr, ETH_ALEN);
	basic->aid = cpu_to_le16(sta->aid);
	basic->qos = sta->wme;
}

static void
mt7915_mcu_sta_he_tlv(struct sk_buff *skb, struct ieee80211_sta *sta)
{
	struct ieee80211_sta_he_cap *he_cap = &sta->he_cap;
	struct ieee80211_he_cap_elem *elem = &he_cap->he_cap_elem;
	struct sta_rec_he *he;
	struct tlv *tlv;
	u32 cap = 0;

	tlv = mt7915_mcu_add_tlv(skb, STA_REC_HE, sizeof(*he));

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

static void
mt7915_mcu_sta_uapsd_tlv(struct sk_buff *skb, struct ieee80211_sta *sta,
		     struct ieee80211_vif *vif)
{
	struct sta_rec_uapsd *uapsd;
	struct tlv *tlv;

	if (vif->type != NL80211_IFTYPE_AP || !sta->wme)
		return;

	tlv = mt7915_mcu_add_tlv(skb, STA_REC_APPS, sizeof(*uapsd));
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

static void
mt7915_mcu_sta_muru_tlv(struct sk_buff *skb, struct ieee80211_sta *sta)
{
	struct ieee80211_sta_he_cap *he_cap = &sta->he_cap;
	struct ieee80211_he_cap_elem *elem = &he_cap->he_cap_elem;
	struct sta_rec_muru *muru;
	struct tlv *tlv;

	tlv = mt7915_mcu_add_tlv(skb, STA_REC_MURU, sizeof(*muru));

	muru = (struct sta_rec_muru *)tlv;
	muru->cfg.ofdma_dl_en = true;
	muru->cfg.mimo_dl_en = true;

	muru->ofdma_dl.punc_pream_rx =
		HE_PHY(CAP1_PREAMBLE_PUNC_RX_MASK, elem->phy_cap_info[1]);
	muru->ofdma_dl.he_20m_in_40m_2g =
		HE_PHY(CAP8_20MHZ_IN_40MHZ_HE_PPDU_IN_2G, elem->phy_cap_info[8]);
	muru->ofdma_dl.he_20m_in_160m =
		HE_PHY(CAP8_20MHZ_IN_160MHZ_HE_PPDU, elem->phy_cap_info[8]);
	muru->ofdma_dl.he_80m_in_160m =
		HE_PHY(CAP8_80MHZ_IN_160MHZ_HE_PPDU, elem->phy_cap_info[8]);
	muru->ofdma_dl.lt16_sigb = 0;
	muru->ofdma_dl.rx_su_comp_sigb = 0;
	muru->ofdma_dl.rx_su_non_comp_sigb = 0;

	muru->ofdma_ul.t_frame_dur =
		HE_MAC(CAP1_TF_MAC_PAD_DUR_MASK, elem->mac_cap_info[1]);
	muru->ofdma_ul.mu_cascading =
		HE_MAC(CAP2_MU_CASCADING, elem->mac_cap_info[2]);
	muru->ofdma_ul.uo_ra =
		HE_MAC(CAP3_OFDMA_RA, elem->mac_cap_info[3]);
	muru->ofdma_ul.he_2x996_tone = 0;
	muru->ofdma_ul.rx_t_frame_11ac = 0;

	muru->mimo_dl.vht_mu_bfee =
		!!(sta->vht_cap.cap & IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE);
	muru->mimo_dl.partial_bw_dl_mimo =
		HE_PHY(CAP6_PARTIAL_BANDWIDTH_DL_MUMIMO, elem->phy_cap_info[6]);

	muru->mimo_ul.full_ul_mimo =
		HE_PHY(CAP2_UL_MU_FULL_MU_MIMO, elem->phy_cap_info[2]);
	muru->mimo_ul.partial_ul_mimo =
		HE_PHY(CAP2_UL_MU_PARTIAL_MU_MIMO, elem->phy_cap_info[2]);
}

static int
mt7915_mcu_add_mu(struct mt7915_dev *dev, struct ieee80211_vif *vif,
		  struct ieee80211_sta *sta)
{
	struct mt7915_vif *mvif = (struct mt7915_vif *)vif->drv_priv;
	struct mt7915_sta *msta = (struct mt7915_sta *)sta->drv_priv;
	struct sk_buff *skb;
	int len = sizeof(struct sta_req_hdr) + sizeof(struct sta_rec_muru);

	if (!sta->vht_cap.vht_supported && !sta->he_cap.has_he)
		return 0;

	skb = mt7915_mcu_alloc_sta_req(dev, mvif, msta, len);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	/* starec muru */
	mt7915_mcu_sta_muru_tlv(skb, sta);

	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_EXT_CMD(STA_REC_UPDATE), true);
}

static void
mt7915_mcu_sta_amsdu_tlv(struct sk_buff *skb, struct ieee80211_sta *sta)
{
	struct mt7915_sta *msta = (struct mt7915_sta *)sta->drv_priv;
	struct sta_rec_amsdu *amsdu;
	struct tlv *tlv;

	if (!sta->max_amsdu_len)
	    return;

	tlv = mt7915_mcu_add_tlv(skb, STA_REC_HW_AMSDU, sizeof(*amsdu));
	amsdu = (struct sta_rec_amsdu *)tlv;
	amsdu->max_amsdu_num = 8;
	amsdu->amsdu_en = true;
	amsdu->max_mpdu_size = sta->max_amsdu_len >=
			       IEEE80211_MAX_MPDU_LEN_VHT_7991;
	msta->wcid.amsdu = true;
}

static bool
mt7915_hw_amsdu_supported(struct ieee80211_vif *vif)
{
	switch (vif->type) {
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_STATION:
		return true;
	default:
		return false;
	}
}

static void
mt7915_mcu_sta_tlv(struct mt7915_dev *dev, struct sk_buff *skb,
		   struct ieee80211_sta *sta, struct ieee80211_vif *vif)
{
	struct tlv *tlv;

	/* starec ht */
	if (sta->ht_cap.ht_supported) {
		struct sta_rec_ht *ht;

		tlv = mt7915_mcu_add_tlv(skb, STA_REC_HT, sizeof(*ht));
		ht = (struct sta_rec_ht *)tlv;
		ht->ht_cap = cpu_to_le16(sta->ht_cap.cap);

		if (mt7915_hw_amsdu_supported(vif))
			mt7915_mcu_sta_amsdu_tlv(skb, sta);
	}

	/* starec vht */
	if (sta->vht_cap.vht_supported) {
		struct sta_rec_vht *vht;

		tlv = mt7915_mcu_add_tlv(skb, STA_REC_VHT, sizeof(*vht));
		vht = (struct sta_rec_vht *)tlv;
		vht->vht_cap = cpu_to_le32(sta->vht_cap.cap);
		vht->vht_rx_mcs_map = sta->vht_cap.vht_mcs.rx_mcs_map;
		vht->vht_tx_mcs_map = sta->vht_cap.vht_mcs.tx_mcs_map;
	}

	/* starec he */
	if (sta->he_cap.has_he)
		mt7915_mcu_sta_he_tlv(skb, sta);

	/* starec uapsd */
	mt7915_mcu_sta_uapsd_tlv(skb, sta, vif);
}

static void
mt7915_mcu_wtbl_smps_tlv(struct sk_buff *skb, struct ieee80211_sta *sta,
			 void *sta_wtbl, void *wtbl_tlv)
{
	struct wtbl_smps *smps;
	struct tlv *tlv;

	tlv = mt7915_mcu_add_nested_tlv(skb, WTBL_SMPS, sizeof(*smps),
					wtbl_tlv, sta_wtbl);
	smps = (struct wtbl_smps *)tlv;

	if (sta->smps_mode == IEEE80211_SMPS_DYNAMIC)
		smps->smps = true;
}

static void
mt7915_mcu_wtbl_ht_tlv(struct sk_buff *skb, struct ieee80211_sta *sta,
		       void *sta_wtbl, void *wtbl_tlv)
{
	struct wtbl_ht *ht = NULL;
	struct tlv *tlv;

	/* wtbl ht */
	if (sta->ht_cap.ht_supported) {
		tlv = mt7915_mcu_add_nested_tlv(skb, WTBL_HT, sizeof(*ht),
						wtbl_tlv, sta_wtbl);
		ht = (struct wtbl_ht *)tlv;
		ht->ldpc = !!(sta->ht_cap.cap & IEEE80211_HT_CAP_LDPC_CODING);
		ht->af = sta->ht_cap.ampdu_factor;
		ht->mm = sta->ht_cap.ampdu_density;
		ht->ht = true;
	}

	/* wtbl vht */
	if (sta->vht_cap.vht_supported) {
		struct wtbl_vht *vht;
		u8 af;

		tlv = mt7915_mcu_add_nested_tlv(skb, WTBL_VHT, sizeof(*vht),
						wtbl_tlv, sta_wtbl);
		vht = (struct wtbl_vht *)tlv;
		vht->ldpc = !!(sta->vht_cap.cap & IEEE80211_VHT_CAP_RXLDPC);
		vht->vht = true;

		af = FIELD_GET(IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK,
			       sta->vht_cap.cap);
		if (ht)
			ht->af = max_t(u8, ht->af, af);
	}

	mt7915_mcu_wtbl_smps_tlv(skb, sta, sta_wtbl, wtbl_tlv);
}

static void
mt7915_mcu_wtbl_hdr_trans_tlv(struct sk_buff *skb, struct ieee80211_vif *vif,
			      struct ieee80211_sta *sta,
			      void *sta_wtbl, void *wtbl_tlv)
{
	struct mt7915_sta *msta;
	struct wtbl_hdr_trans *htr = NULL;
	struct tlv *tlv;

	tlv = mt7915_mcu_add_nested_tlv(skb, WTBL_HDR_TRANS, sizeof(*htr),
					wtbl_tlv, sta_wtbl);
	htr = (struct wtbl_hdr_trans *)tlv;
	htr->no_rx_trans = true;
	if (vif->type == NL80211_IFTYPE_STATION)
		htr->to_ds = true;
	else
		htr->from_ds = true;

	if (!sta)
		return;

	msta = (struct mt7915_sta *)sta->drv_priv;
	htr->no_rx_trans = !test_bit(MT_WCID_FLAG_HDR_TRANS, &msta->wcid.flags);
	if (test_bit(MT_WCID_FLAG_4ADDR, &msta->wcid.flags)) {
		htr->to_ds = true;
		htr->from_ds = true;
	}
}

int mt7915_mcu_sta_update_hdr_trans(struct mt7915_dev *dev,
				    struct ieee80211_vif *vif,
				    struct ieee80211_sta *sta)
{
	struct mt7915_sta *msta = (struct mt7915_sta *)sta->drv_priv;
	struct wtbl_req_hdr *wtbl_hdr;
	struct sk_buff *skb;

	skb = mt76_mcu_msg_alloc(&dev->mt76, NULL, MT7915_WTBL_UPDATE_MAX_SIZE);
	if (!skb)
		return -ENOMEM;

	wtbl_hdr = mt7915_mcu_alloc_wtbl_req(dev, msta, WTBL_SET, NULL, &skb);
	if (IS_ERR(wtbl_hdr))
		return PTR_ERR(wtbl_hdr);

	mt7915_mcu_wtbl_hdr_trans_tlv(skb, vif, sta, NULL, wtbl_hdr);

	return mt76_mcu_skb_send_msg(&dev->mt76, skb, MCU_EXT_CMD(WTBL_UPDATE),
				     true);
}

int mt7915_mcu_add_smps(struct mt7915_dev *dev, struct ieee80211_vif *vif,
			struct ieee80211_sta *sta)
{
	struct mt7915_vif *mvif = (struct mt7915_vif *)vif->drv_priv;
	struct mt7915_sta *msta = (struct mt7915_sta *)sta->drv_priv;
	struct wtbl_req_hdr *wtbl_hdr;
	struct tlv *sta_wtbl;
	struct sk_buff *skb;

	skb = mt7915_mcu_alloc_sta_req(dev, mvif, msta,
				       MT7915_STA_UPDATE_MAX_SIZE);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	sta_wtbl = mt7915_mcu_add_tlv(skb, STA_REC_WTBL, sizeof(struct tlv));

	wtbl_hdr = mt7915_mcu_alloc_wtbl_req(dev, msta, WTBL_SET, sta_wtbl,
					     &skb);
	if (IS_ERR(wtbl_hdr))
		return PTR_ERR(wtbl_hdr);

	mt7915_mcu_wtbl_smps_tlv(skb, sta, sta_wtbl, wtbl_hdr);

	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_EXT_CMD(STA_REC_UPDATE), true);
}

static void
mt7915_mcu_sta_sounding_rate(struct sta_rec_bf *bf)
{
	bf->bf_cap = MT_EBF;
	bf->sounding_phy = MT_PHY_TYPE_OFDM;
	bf->ndp_rate = 0;				/* mcs0 */
	bf->ndpa_rate = MT7915_CFEND_RATE_DEFAULT;	/* ofdm 24m */
	bf->rept_poll_rate = MT7915_CFEND_RATE_DEFAULT;	/* ofdm 24m */
}

static void
mt7915_mcu_sta_bfer_ht(struct ieee80211_sta *sta, struct mt7915_phy *phy,
		       struct sta_rec_bf *bf)
{
	struct ieee80211_mcs_info *mcs = &sta->ht_cap.mcs;
	u8 n = 0;

	bf->tx_mode = MT_PHY_TYPE_HT;
	bf->bf_cap = MT_IBF;

	if (mcs->tx_params & IEEE80211_HT_MCS_TX_RX_DIFF &&
	    (mcs->tx_params & IEEE80211_HT_MCS_TX_DEFINED))
		n = FIELD_GET(IEEE80211_HT_MCS_TX_MAX_STREAMS_MASK,
			      mcs->tx_params);
	else if (mcs->rx_mask[3])
		n = 3;
	else if (mcs->rx_mask[2])
		n = 2;
	else if (mcs->rx_mask[1])
		n = 1;

	bf->nr = hweight8(phy->mt76->chainmask) - 1;
	bf->nc = min_t(u8, bf->nr, n);
	bf->ibf_ncol = n;
}

static void
mt7915_mcu_sta_bfer_vht(struct ieee80211_sta *sta, struct mt7915_phy *phy,
			struct sta_rec_bf *bf, bool explicit)
{
	struct ieee80211_sta_vht_cap *pc = &sta->vht_cap;
	struct ieee80211_sta_vht_cap *vc = &phy->mt76->sband_5g.sband.vht_cap;
	u16 mcs_map = le16_to_cpu(pc->vht_mcs.rx_mcs_map);
	u8 nss_mcs = mt7915_mcu_get_sta_nss(mcs_map);
	u8 tx_ant = hweight8(phy->mt76->chainmask) - 1;

	bf->tx_mode = MT_PHY_TYPE_VHT;

	if (explicit) {
		u8 bfee_nr, bfer_nr;

		mt7915_mcu_sta_sounding_rate(bf);
		bfee_nr = FIELD_GET(IEEE80211_VHT_CAP_BEAMFORMEE_STS_MASK,
				    pc->cap);
		bfer_nr = FIELD_GET(IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_MASK,
				    vc->cap);
		bf->nr = min_t(u8, min_t(u8, bfer_nr, bfee_nr), tx_ant);
		bf->nc = min_t(u8, nss_mcs, bf->nr);
		bf->ibf_ncol = bf->nc;

		if (sta->bandwidth == IEEE80211_STA_RX_BW_160)
			bf->nr = 1;
	} else {
		bf->bf_cap = MT_IBF;
		bf->nr = tx_ant;
		bf->nc = min_t(u8, nss_mcs, bf->nr);
		bf->ibf_ncol = nss_mcs;

		if (sta->bandwidth == IEEE80211_STA_RX_BW_160)
			bf->ibf_nrow = 1;
	}
}

static void
mt7915_mcu_sta_bfer_he(struct ieee80211_sta *sta, struct ieee80211_vif *vif,
		       struct mt7915_phy *phy, struct sta_rec_bf *bf)
{
	struct ieee80211_sta_he_cap *pc = &sta->he_cap;
	struct ieee80211_he_cap_elem *pe = &pc->he_cap_elem;
	const struct ieee80211_sta_he_cap *vc = mt7915_get_he_phy_cap(phy, vif);
	const struct ieee80211_he_cap_elem *ve = &vc->he_cap_elem;
	u16 mcs_map = le16_to_cpu(pc->he_mcs_nss_supp.rx_mcs_80);
	u8 nss_mcs = mt7915_mcu_get_sta_nss(mcs_map);
	u8 bfee_nr, bfer_nr;

	bf->tx_mode = MT_PHY_TYPE_HE_SU;
	mt7915_mcu_sta_sounding_rate(bf);
	bf->trigger_su = HE_PHY(CAP6_TRIG_SU_BEAMFORMING_FB,
				pe->phy_cap_info[6]);
	bf->trigger_mu = HE_PHY(CAP6_TRIG_MU_BEAMFORMING_PARTIAL_BW_FB,
				pe->phy_cap_info[6]);
	bfer_nr = HE_PHY(CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_MASK,
			 ve->phy_cap_info[5]);
	bfee_nr = HE_PHY(CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_MASK,
			 pe->phy_cap_info[4]);
	bf->nr = min_t(u8, bfer_nr, bfee_nr);
	bf->nc = min_t(u8, nss_mcs, bf->nr);
	bf->ibf_ncol = bf->nc;

	if (sta->bandwidth != IEEE80211_STA_RX_BW_160)
		return;

	/* go over for 160MHz and 80p80 */
	if (pe->phy_cap_info[0] &
	    IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G) {
		mcs_map = le16_to_cpu(pc->he_mcs_nss_supp.rx_mcs_160);
		nss_mcs = mt7915_mcu_get_sta_nss(mcs_map);

		bf->nc_bw160 = nss_mcs;
	}

	if (pe->phy_cap_info[0] &
	    IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_80PLUS80_MHZ_IN_5G) {
		mcs_map = le16_to_cpu(pc->he_mcs_nss_supp.rx_mcs_80p80);
		nss_mcs = mt7915_mcu_get_sta_nss(mcs_map);

		if (bf->nc_bw160)
			bf->nc_bw160 = min_t(u8, bf->nc_bw160, nss_mcs);
		else
			bf->nc_bw160 = nss_mcs;
	}

	bfer_nr = HE_PHY(CAP5_BEAMFORMEE_NUM_SND_DIM_ABOVE_80MHZ_MASK,
			 ve->phy_cap_info[5]);
	bfee_nr = HE_PHY(CAP4_BEAMFORMEE_MAX_STS_ABOVE_80MHZ_MASK,
			 pe->phy_cap_info[4]);

	bf->nr_bw160 = min_t(int, bfer_nr, bfee_nr);
}

static void
mt7915_mcu_sta_bfer_tlv(struct sk_buff *skb, struct ieee80211_sta *sta,
			struct ieee80211_vif *vif, struct mt7915_phy *phy,
			bool enable, bool explicit)
{
	int tx_ant = hweight8(phy->mt76->chainmask) - 1;
	struct sta_rec_bf *bf;
	struct tlv *tlv;
	const u8 matrix[4][4] = {
		{0, 0, 0, 0},
		{1, 1, 0, 0},	/* 2x1, 2x2, 2x3, 2x4 */
		{2, 4, 4, 0},	/* 3x1, 3x2, 3x3, 3x4 */
		{3, 5, 6, 0}	/* 4x1, 4x2, 4x3, 4x4 */
	};

#define MT_BFER_FREE		cpu_to_le16(GENMASK(15, 0))

	tlv = mt7915_mcu_add_tlv(skb, STA_REC_BF, sizeof(*bf));
	bf = (struct sta_rec_bf *)tlv;

	if (!enable) {
		bf->pfmu = MT_BFER_FREE;
		return;
	}

	/* he: eBF only, in accordance with spec
	 * vht: support eBF and iBF
	 * ht: iBF only, since mac80211 lacks of eBF support
	 */
	if (sta->he_cap.has_he && explicit)
		mt7915_mcu_sta_bfer_he(sta, vif, phy, bf);
	else if (sta->vht_cap.vht_supported)
		mt7915_mcu_sta_bfer_vht(sta, phy, bf, explicit);
	else if (sta->ht_cap.ht_supported)
		mt7915_mcu_sta_bfer_ht(sta, phy, bf);
	else
		return;

	bf->bw = sta->bandwidth;
	bf->ibf_dbw = sta->bandwidth;
	bf->ibf_nrow = tx_ant;

	if (!explicit && sta->bandwidth <= IEEE80211_STA_RX_BW_40 && !bf->nc)
		bf->ibf_timeout = 0x48;
	else
		bf->ibf_timeout = 0x18;

	if (explicit && bf->nr != tx_ant)
		bf->mem_20m = matrix[tx_ant][bf->nc];
	else
		bf->mem_20m = matrix[bf->nr][bf->nc];

	switch (sta->bandwidth) {
	case IEEE80211_STA_RX_BW_160:
	case IEEE80211_STA_RX_BW_80:
		bf->mem_total = bf->mem_20m * 2;
		break;
	case IEEE80211_STA_RX_BW_40:
		bf->mem_total = bf->mem_20m;
		break;
	case IEEE80211_STA_RX_BW_20:
	default:
		break;
	}
}

static void
mt7915_mcu_sta_bfee_tlv(struct sk_buff *skb, struct ieee80211_sta *sta,
			struct mt7915_phy *phy)
{
	int tx_ant = hweight8(phy->mt76->chainmask) - 1;
	struct sta_rec_bfee *bfee;
	struct tlv *tlv;
	u8 nr = 0;

	tlv = mt7915_mcu_add_tlv(skb, STA_REC_BFEE, sizeof(*bfee));
	bfee = (struct sta_rec_bfee *)tlv;

	if (sta->he_cap.has_he) {
		struct ieee80211_he_cap_elem *pe = &sta->he_cap.he_cap_elem;

		nr = HE_PHY(CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_MASK,
			    pe->phy_cap_info[5]);
	} else if (sta->vht_cap.vht_supported) {
		struct ieee80211_sta_vht_cap *pc = &sta->vht_cap;

		nr = FIELD_GET(IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_MASK,
			       pc->cap);
	}

	/* reply with identity matrix to avoid 2x2 BF negative gain */
	bfee->fb_identity_matrix = !!(nr == 1 && tx_ant == 2);
}

static int
mt7915_mcu_add_txbf(struct mt7915_dev *dev, struct ieee80211_vif *vif,
		    struct ieee80211_sta *sta, bool enable)
{
	struct mt7915_vif *mvif = (struct mt7915_vif *)vif->drv_priv;
	struct mt7915_sta *msta = (struct mt7915_sta *)sta->drv_priv;
	struct mt7915_phy *phy;
	struct sk_buff *skb;
	int r, len;
	bool ebfee = 0, ebf = 0;

	if (vif->type != NL80211_IFTYPE_STATION &&
	    vif->type != NL80211_IFTYPE_AP)
		return 0;

	phy = mvif->band_idx ? mt7915_ext_phy(dev) : &dev->phy;

	if (sta->he_cap.has_he) {
		struct ieee80211_he_cap_elem *pe;
		const struct ieee80211_he_cap_elem *ve;
		const struct ieee80211_sta_he_cap *vc;

		pe = &sta->he_cap.he_cap_elem;
		vc = mt7915_get_he_phy_cap(phy, vif);
		ve = &vc->he_cap_elem;

		ebfee = !!((HE_PHY(CAP3_SU_BEAMFORMER, pe->phy_cap_info[3]) ||
			    HE_PHY(CAP4_MU_BEAMFORMER, pe->phy_cap_info[4])) &&
			   HE_PHY(CAP4_SU_BEAMFORMEE, ve->phy_cap_info[4]));
		ebf = !!((HE_PHY(CAP3_SU_BEAMFORMER, ve->phy_cap_info[3]) ||
			  HE_PHY(CAP4_MU_BEAMFORMER, ve->phy_cap_info[4])) &&
			 HE_PHY(CAP4_SU_BEAMFORMEE, pe->phy_cap_info[4]));
	} else if (sta->vht_cap.vht_supported) {
		struct ieee80211_sta_vht_cap *pc;
		struct ieee80211_sta_vht_cap *vc;
		u32 cr, ce;

		pc = &sta->vht_cap;
		vc = &phy->mt76->sband_5g.sband.vht_cap;
		cr = IEEE80211_VHT_CAP_SU_BEAMFORMER_CAPABLE |
		     IEEE80211_VHT_CAP_MU_BEAMFORMER_CAPABLE;
		ce = IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE |
		     IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE;

		ebfee = !!((pc->cap & cr) && (vc->cap & ce));
		ebf = !!((vc->cap & cr) && (pc->cap & ce));
	}

	/* must keep each tag independent */

	/* starec bf */
	if (ebf || dev->ibf) {
		len = sizeof(struct sta_req_hdr) + sizeof(struct sta_rec_bf);

		skb = mt7915_mcu_alloc_sta_req(dev, mvif, msta, len);
		if (IS_ERR(skb))
			return PTR_ERR(skb);

		mt7915_mcu_sta_bfer_tlv(skb, sta, vif, phy, enable, ebf);

		r = mt76_mcu_skb_send_msg(&dev->mt76, skb,
					  MCU_EXT_CMD(STA_REC_UPDATE), true);
		if (r)
			return r;
	}

	/* starec bfee */
	if (ebfee) {
		len = sizeof(struct sta_req_hdr) + sizeof(struct sta_rec_bfee);

		skb = mt7915_mcu_alloc_sta_req(dev, mvif, msta, len);
		if (IS_ERR(skb))
			return PTR_ERR(skb);

		mt7915_mcu_sta_bfee_tlv(skb, sta, phy);

		r = mt76_mcu_skb_send_msg(&dev->mt76, skb,
					  MCU_EXT_CMD(STA_REC_UPDATE), true);
		if (r)
			return r;
	}

	return 0;
}

static void
mt7915_mcu_sta_rate_ctrl_tlv(struct sk_buff *skb, struct mt7915_dev *dev,
			     struct ieee80211_vif *vif, struct ieee80211_sta *sta)
{
	struct mt7915_sta *msta = (struct mt7915_sta *)sta->drv_priv;
	struct mt76_phy *mphy = &dev->mphy;
	enum nl80211_band band;
	struct sta_rec_ra *ra;
	struct tlv *tlv;
	u32 supp_rate, n_rates, cap = sta->wme ? STA_CAP_WMM : 0;
	u8 i, nss = sta->rx_nss, mcs = 0;

	tlv = mt7915_mcu_add_tlv(skb, STA_REC_RA, sizeof(*ra));
	ra = (struct sta_rec_ra *)tlv;

	if (msta->wcid.ext_phy && dev->mt76.phy2)
		mphy = dev->mt76.phy2;

	band = mphy->chandef.chan->band;
	supp_rate = sta->supp_rates[band];
	n_rates = hweight32(supp_rate);

	ra->valid = true;
	ra->auto_rate = true;
	ra->phy_mode = mt7915_get_phy_mode(mphy, vif, sta);
	ra->channel = mphy->chandef.chan->hw_value;
	ra->bw = sta->bandwidth;
	ra->rate_len = n_rates;
	ra->phy.bw = sta->bandwidth;

	if (n_rates) {
		if (band == NL80211_BAND_2GHZ) {
			ra->supp_mode = MODE_CCK;
			ra->supp_cck_rate = supp_rate & GENMASK(3, 0);
			ra->phy.type = MT_PHY_TYPE_CCK;

			if (n_rates > 4) {
				ra->supp_mode |= MODE_OFDM;
				ra->supp_ofdm_rate = supp_rate >> 4;
				ra->phy.type = MT_PHY_TYPE_OFDM;
			}
		} else {
			ra->supp_mode = MODE_OFDM;
			ra->supp_ofdm_rate = supp_rate;
			ra->phy.type = MT_PHY_TYPE_OFDM;
		}
	}

	if (sta->ht_cap.ht_supported) {
		for (i = 0; i < nss; i++)
			ra->ht_mcs[i] = sta->ht_cap.mcs.rx_mask[i];

		ra->supp_ht_mcs = *(__le32 *)ra->ht_mcs;
		ra->supp_mode |= MODE_HT;
		mcs = hweight32(le32_to_cpu(ra->supp_ht_mcs)) - 1;
		ra->af = sta->ht_cap.ampdu_factor;
		ra->ht_gf = !!(sta->ht_cap.cap & IEEE80211_HT_CAP_GRN_FLD);

		cap |= STA_CAP_HT;
		if (sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_20)
			cap |= STA_CAP_SGI_20;
		if (sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_40)
			cap |= STA_CAP_SGI_40;
		if (sta->ht_cap.cap & IEEE80211_HT_CAP_TX_STBC)
			cap |= STA_CAP_TX_STBC;
		if (sta->ht_cap.cap & IEEE80211_HT_CAP_RX_STBC)
			cap |= STA_CAP_RX_STBC;
		if (sta->ht_cap.cap & IEEE80211_HT_CAP_LDPC_CODING)
			cap |= STA_CAP_LDPC;
	}

	if (sta->vht_cap.vht_supported) {
		u16 mcs_map = le16_to_cpu(sta->vht_cap.vht_mcs.rx_mcs_map);
		u16 vht_mcs;
		u8 af, mcs_prev;

		af = FIELD_GET(IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK,
			       sta->vht_cap.cap);
		ra->af = max_t(u8, ra->af, af);

		cap |= STA_CAP_VHT;
		if (sta->vht_cap.cap & IEEE80211_VHT_CAP_SHORT_GI_80)
			cap |= STA_CAP_VHT_SGI_80;
		if (sta->vht_cap.cap & IEEE80211_VHT_CAP_SHORT_GI_160)
			cap |= STA_CAP_VHT_SGI_160;
		if (sta->vht_cap.cap & IEEE80211_VHT_CAP_TXSTBC)
			cap |= STA_CAP_VHT_TX_STBC;
		if (sta->vht_cap.cap & IEEE80211_VHT_CAP_RXSTBC_1)
			cap |= STA_CAP_VHT_RX_STBC;
		if (sta->vht_cap.cap & IEEE80211_VHT_CAP_RXLDPC)
			cap |= STA_CAP_VHT_LDPC;

		ra->supp_mode |= MODE_VHT;
		for (mcs = 0, i = 0; i < nss; i++, mcs_map >>= 2) {
			switch (mcs_map & 0x3) {
			case IEEE80211_VHT_MCS_SUPPORT_0_9:
				vht_mcs = GENMASK(9, 0);
				break;
			case IEEE80211_VHT_MCS_SUPPORT_0_8:
				vht_mcs = GENMASK(8, 0);
				break;
			case IEEE80211_VHT_MCS_SUPPORT_0_7:
				vht_mcs = GENMASK(7, 0);
				break;
			default:
				vht_mcs = 0;
			}

			ra->supp_vht_mcs[i] = cpu_to_le16(vht_mcs);

			mcs_prev = hweight16(vht_mcs) - 1;
			if (mcs_prev > mcs)
				mcs = mcs_prev;

			/* only support 2ss on 160MHz */
			if (i > 1 && (ra->bw == CMD_CBW_160MHZ ||
				      ra->bw == CMD_CBW_8080MHZ))
				break;
		}
	}

	if (sta->he_cap.has_he) {
		ra->supp_mode |= MODE_HE;
		cap |= STA_CAP_HE;
	}

	ra->sta_status = cpu_to_le32(cap);

	switch (BIT(fls(ra->supp_mode) - 1)) {
	case MODE_VHT:
		ra->phy.type = MT_PHY_TYPE_VHT;
		ra->phy.mcs = mcs;
		ra->phy.nss = nss;
		ra->phy.stbc = !!(sta->vht_cap.cap & IEEE80211_VHT_CAP_TXSTBC);
		ra->phy.ldpc = !!(sta->vht_cap.cap & IEEE80211_VHT_CAP_RXLDPC);
		ra->phy.sgi =
			!!(sta->vht_cap.cap & IEEE80211_VHT_CAP_SHORT_GI_80);
		break;
	case MODE_HT:
		ra->phy.type = MT_PHY_TYPE_HT;
		ra->phy.mcs = mcs;
		ra->phy.ldpc = sta->ht_cap.cap & IEEE80211_HT_CAP_LDPC_CODING;
		ra->phy.stbc = !!(sta->ht_cap.cap & IEEE80211_HT_CAP_TX_STBC);
		ra->phy.sgi = !!(sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_20);
		break;
	default:
		break;
	}
}

int mt7915_mcu_add_rate_ctrl(struct mt7915_dev *dev, struct ieee80211_vif *vif,
			     struct ieee80211_sta *sta)
{
	struct mt7915_vif *mvif = (struct mt7915_vif *)vif->drv_priv;
	struct mt7915_sta *msta = (struct mt7915_sta *)sta->drv_priv;
	struct sk_buff *skb;
	int len = sizeof(struct sta_req_hdr) + sizeof(struct sta_rec_ra);

	skb = mt7915_mcu_alloc_sta_req(dev, mvif, msta, len);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	mt7915_mcu_sta_rate_ctrl_tlv(skb, dev, vif, sta);

	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_EXT_CMD(STA_REC_UPDATE), true);
}

int mt7915_mcu_add_sta_adv(struct mt7915_dev *dev, struct ieee80211_vif *vif,
			   struct ieee80211_sta *sta, bool enable)
{
	int ret;

	if (!sta)
		return 0;

	/* must keep the order */
	ret = mt7915_mcu_add_txbf(dev, vif, sta, enable);
	if (ret)
		return ret;

	ret = mt7915_mcu_add_mu(dev, vif, sta);
	if (ret)
		return ret;

	if (enable)
		return mt7915_mcu_add_rate_ctrl(dev, vif, sta);

	return 0;
}

int mt7915_mcu_add_sta(struct mt7915_dev *dev, struct ieee80211_vif *vif,
		       struct ieee80211_sta *sta, bool enable)
{
	struct mt7915_vif *mvif = (struct mt7915_vif *)vif->drv_priv;
	struct wtbl_req_hdr *wtbl_hdr;
	struct mt7915_sta *msta;
	struct tlv *sta_wtbl;
	struct sk_buff *skb;

	msta = sta ? (struct mt7915_sta *)sta->drv_priv : &mvif->sta;

	skb = mt7915_mcu_alloc_sta_req(dev, mvif, msta,
				       MT7915_STA_UPDATE_MAX_SIZE);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	mt7915_mcu_sta_basic_tlv(skb, vif, sta, enable);
	if (enable && sta)
		mt7915_mcu_sta_tlv(dev, skb, sta, vif);

	sta_wtbl = mt7915_mcu_add_tlv(skb, STA_REC_WTBL, sizeof(struct tlv));

	wtbl_hdr = mt7915_mcu_alloc_wtbl_req(dev, msta, WTBL_RESET_AND_SET,
					     sta_wtbl, &skb);
	if (IS_ERR(wtbl_hdr))
		return PTR_ERR(wtbl_hdr);

	if (enable) {
		mt7915_mcu_wtbl_generic_tlv(skb, vif, sta, sta_wtbl, wtbl_hdr);
		mt7915_mcu_wtbl_hdr_trans_tlv(skb, vif, sta, sta_wtbl, wtbl_hdr);
		if (sta)
			mt7915_mcu_wtbl_ht_tlv(skb, sta, sta_wtbl, wtbl_hdr);
	}

	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_EXT_CMD(STA_REC_UPDATE), true);
}

int mt7915_mcu_set_fixed_rate(struct mt7915_dev *dev,
			      struct ieee80211_sta *sta, u32 rate)
{
	struct mt7915_sta *msta = (struct mt7915_sta *)sta->drv_priv;
	struct mt7915_vif *mvif = msta->vif;
	struct sta_rec_ra_fixed *ra;
	struct sk_buff *skb;
	struct tlv *tlv;
	int len = sizeof(struct sta_req_hdr) + sizeof(*ra);

	skb = mt7915_mcu_alloc_sta_req(dev, mvif, msta, len);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	tlv = mt7915_mcu_add_tlv(skb, STA_REC_RA_UPDATE, sizeof(*ra));
	ra = (struct sta_rec_ra_fixed *)tlv;

	if (!rate) {
		ra->field = cpu_to_le32(RATE_PARAM_AUTO);
		goto out;
	} else {
		ra->field = cpu_to_le32(RATE_PARAM_FIXED);
	}

	ra->phy.type = FIELD_GET(RATE_CFG_PHY_TYPE, rate);
	ra->phy.bw = FIELD_GET(RATE_CFG_BW, rate);
	ra->phy.nss = FIELD_GET(RATE_CFG_NSS, rate);
	ra->phy.mcs = FIELD_GET(RATE_CFG_MCS, rate);
	ra->phy.stbc = FIELD_GET(RATE_CFG_STBC, rate);

	if (ra->phy.bw)
		ra->phy.ldpc = 7;
	else
		ra->phy.ldpc = FIELD_GET(RATE_CFG_LDPC, rate) * 7;

	/* HT/VHT - SGI: 1, LGI: 0; HE - SGI: 0, MGI: 1, LGI: 2 */
	if (ra->phy.type > MT_PHY_TYPE_VHT)
		ra->phy.sgi = ra->phy.mcs * 85;
	else
		ra->phy.sgi = ra->phy.mcs * 15;

out:
	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_EXT_CMD(STA_REC_UPDATE), true);
}

int mt7915_mcu_add_dev_info(struct mt7915_phy *phy,
			    struct ieee80211_vif *vif, bool enable)
{
	struct mt7915_dev *dev = phy->dev;
	struct mt7915_vif *mvif = (struct mt7915_vif *)vif->drv_priv;
	struct {
		struct req_hdr {
			u8 omac_idx;
			u8 dbdc_idx;
			__le16 tlv_num;
			u8 is_tlv_append;
			u8 rsv[3];
		} __packed hdr;
		struct req_tlv {
			__le16 tag;
			__le16 len;
			u8 active;
			u8 dbdc_idx;
			u8 omac_addr[ETH_ALEN];
		} __packed tlv;
	} data = {
		.hdr = {
			.omac_idx = mvif->omac_idx,
			.dbdc_idx = mvif->band_idx,
			.tlv_num = cpu_to_le16(1),
			.is_tlv_append = 1,
		},
		.tlv = {
			.tag = cpu_to_le16(DEV_INFO_ACTIVE),
			.len = cpu_to_le16(sizeof(struct req_tlv)),
			.active = enable,
			.dbdc_idx = mvif->band_idx,
		},
	};

	if (mvif->omac_idx >= REPEATER_BSSID_START)
		return mt7915_mcu_muar_config(phy, vif, false, enable);

	memcpy(data.tlv.omac_addr, vif->addr, ETH_ALEN);
	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(DEV_INFO_UPDATE),
				 &data, sizeof(data), true);
}

static void
mt7915_mcu_beacon_csa(struct sk_buff *rskb, struct sk_buff *skb,
		      struct bss_info_bcn *bcn,
		      struct ieee80211_mutable_offsets *offs)
{
	if (offs->cntdwn_counter_offs[0]) {
		struct tlv *tlv;
		struct bss_info_bcn_csa *csa;

		tlv = mt7915_mcu_add_nested_subtlv(rskb, BSS_INFO_BCN_CSA,
						   sizeof(*csa), &bcn->sub_ntlv,
						   &bcn->len);
		csa = (struct bss_info_bcn_csa *)tlv;
		csa->cnt = skb->data[offs->cntdwn_counter_offs[0]];
	}
}

static void
mt7915_mcu_beacon_cont(struct mt7915_dev *dev, struct sk_buff *rskb,
		       struct sk_buff *skb, struct bss_info_bcn *bcn,
		       struct ieee80211_mutable_offsets *offs)
{
	struct mt76_wcid *wcid = &dev->mt76.global_wcid;
	struct bss_info_bcn_cont *cont;
	struct tlv *tlv;
	u8 *buf;
	int len = sizeof(*cont) + MT_TXD_SIZE + skb->len;

	tlv = mt7915_mcu_add_nested_subtlv(rskb, BSS_INFO_BCN_CONTENT,
					   len, &bcn->sub_ntlv, &bcn->len);

	cont = (struct bss_info_bcn_cont *)tlv;
	cont->pkt_len = cpu_to_le16(MT_TXD_SIZE + skb->len);
	cont->tim_ofs = cpu_to_le16(offs->tim_offset);

	if (offs->cntdwn_counter_offs[0])
		cont->csa_ofs = cpu_to_le16(offs->cntdwn_counter_offs[0] - 4);

	buf = (u8 *)tlv + sizeof(*cont);
	mt7915_mac_write_txwi(dev, (__le32 *)buf, skb, wcid, NULL,
			      true);
	memcpy(buf + MT_TXD_SIZE, skb->data, skb->len);
}

int mt7915_mcu_add_beacon(struct ieee80211_hw *hw,
			  struct ieee80211_vif *vif, int en)
{
#define MAX_BEACON_SIZE 512
	struct mt7915_dev *dev = mt7915_hw_dev(hw);
	struct mt7915_phy *phy = mt7915_hw_phy(hw);
	struct mt7915_vif *mvif = (struct mt7915_vif *)vif->drv_priv;
	struct ieee80211_mutable_offsets offs;
	struct ieee80211_tx_info *info;
	struct sk_buff *skb, *rskb;
	struct tlv *tlv;
	struct bss_info_bcn *bcn;
	int len = MT7915_BEACON_UPDATE_SIZE + MAX_BEACON_SIZE;

	rskb = mt7915_mcu_alloc_sta_req(dev, mvif, NULL, len);
	if (IS_ERR(rskb))
		return PTR_ERR(rskb);

	tlv = mt7915_mcu_add_tlv(rskb, BSS_INFO_OFFLOAD, sizeof(*bcn));
	bcn = (struct bss_info_bcn *)tlv;
	bcn->enable = en;

	if (!en)
		goto out;

	skb = ieee80211_beacon_get_template(hw, vif, &offs);
	if (!skb)
		return -EINVAL;

	if (skb->len > MAX_BEACON_SIZE - MT_TXD_SIZE) {
		dev_err(dev->mt76.dev, "Bcn size limit exceed\n");
		dev_kfree_skb(skb);
		return -EINVAL;
	}

	if (mvif->band_idx) {
		info = IEEE80211_SKB_CB(skb);
		info->hw_queue |= MT_TX_HW_QUEUE_EXT_PHY;
	}

	/* TODO: subtag - bss color count & 11v MBSSID */
	mt7915_mcu_beacon_csa(rskb, skb, bcn, &offs);
	mt7915_mcu_beacon_cont(dev, rskb, skb, bcn, &offs);
	dev_kfree_skb(skb);

out:
	return mt76_mcu_skb_send_msg(&phy->dev->mt76, rskb,
				     MCU_EXT_CMD(BSS_INFO_UPDATE), true);
}

static int mt7915_mcu_start_firmware(struct mt7915_dev *dev, u32 addr,
				     u32 option)
{
	struct {
		__le32 option;
		__le32 addr;
	} req = {
		.option = cpu_to_le32(option),
		.addr = cpu_to_le32(addr),
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_CMD(FW_START_REQ), &req,
				 sizeof(req), true);
}

static int mt7915_mcu_restart(struct mt76_dev *dev)
{
	struct {
		u8 power_mode;
		u8 rsv[3];
	} req = {
		.power_mode = 1,
	};

	return mt76_mcu_send_msg(dev, MCU_CMD(NIC_POWER_CTRL), &req,
				 sizeof(req), false);
}

static int mt7915_mcu_patch_sem_ctrl(struct mt7915_dev *dev, bool get)
{
	struct {
		__le32 op;
	} req = {
		.op = cpu_to_le32(get ? PATCH_SEM_GET : PATCH_SEM_RELEASE),
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_CMD(PATCH_SEM_CONTROL), &req,
				 sizeof(req), true);
}

static int mt7915_mcu_start_patch(struct mt7915_dev *dev)
{
	struct {
		u8 check_crc;
		u8 reserved[3];
	} req = {
		.check_crc = 0,
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_CMD(PATCH_FINISH_REQ), &req,
				 sizeof(req), true);
}

static int mt7915_driver_own(struct mt7915_dev *dev)
{
	mt76_wr(dev, MT_TOP_LPCR_HOST_BAND0, MT_TOP_LPCR_HOST_DRV_OWN);
	if (!mt76_poll_msec(dev, MT_TOP_LPCR_HOST_BAND0,
			    MT_TOP_LPCR_HOST_FW_OWN, 0, 500)) {
		dev_err(dev->mt76.dev, "Timeout for driver own\n");
		return -EIO;
	}

	return 0;
}

static int mt7915_mcu_init_download(struct mt7915_dev *dev, u32 addr,
				    u32 len, u32 mode)
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
	int attr;

	if (req.addr == cpu_to_le32(MCU_PATCH_ADDRESS))
		attr = MCU_CMD(PATCH_START_REQ);
	else
		attr = MCU_CMD(TARGET_ADDRESS_LEN_REQ);

	return mt76_mcu_send_msg(&dev->mt76, attr, &req, sizeof(req), true);
}

static int mt7915_load_patch(struct mt7915_dev *dev)
{
	const struct mt7915_patch_hdr *hdr;
	const struct firmware *fw = NULL;
	int i, ret, sem;

	sem = mt7915_mcu_patch_sem_ctrl(dev, 1);
	switch (sem) {
	case PATCH_IS_DL:
		return 0;
	case PATCH_NOT_DL_SEM_SUCCESS:
		break;
	default:
		dev_err(dev->mt76.dev, "Failed to get patch semaphore\n");
		return -EAGAIN;
	}

	ret = request_firmware(&fw, MT7915_ROM_PATCH, dev->mt76.dev);
	if (ret)
		goto out;

	if (!fw || !fw->data || fw->size < sizeof(*hdr)) {
		dev_err(dev->mt76.dev, "Invalid firmware\n");
		ret = -EINVAL;
		goto out;
	}

	hdr = (const struct mt7915_patch_hdr *)(fw->data);

	dev_info(dev->mt76.dev, "HW/SW Version: 0x%x, Build Time: %.16s\n",
		 be32_to_cpu(hdr->hw_sw_ver), hdr->build_date);

	for (i = 0; i < be32_to_cpu(hdr->desc.n_region); i++) {
		struct mt7915_patch_sec *sec;
		const u8 *dl;
		u32 len, addr;

		sec = (struct mt7915_patch_sec *)(fw->data + sizeof(*hdr) +
						  i * sizeof(*sec));
		if ((be32_to_cpu(sec->type) & PATCH_SEC_TYPE_MASK) !=
		    PATCH_SEC_TYPE_INFO) {
			ret = -EINVAL;
			goto out;
		}

		addr = be32_to_cpu(sec->info.addr);
		len = be32_to_cpu(sec->info.len);
		dl = fw->data + be32_to_cpu(sec->offs);

		ret = mt7915_mcu_init_download(dev, addr, len,
					       DL_MODE_NEED_RSP);
		if (ret) {
			dev_err(dev->mt76.dev, "Download request failed\n");
			goto out;
		}

		ret = mt76_mcu_send_firmware(&dev->mt76, MCU_CMD(FW_SCATTER),
					     dl, len);
		if (ret) {
			dev_err(dev->mt76.dev, "Failed to send patch\n");
			goto out;
		}
	}

	ret = mt7915_mcu_start_patch(dev);
	if (ret)
		dev_err(dev->mt76.dev, "Failed to start patch\n");

out:
	sem = mt7915_mcu_patch_sem_ctrl(dev, 0);
	switch (sem) {
	case PATCH_REL_SEM_SUCCESS:
		break;
	default:
		ret = -EAGAIN;
		dev_err(dev->mt76.dev, "Failed to release patch semaphore\n");
		goto out;
	}
	release_firmware(fw);

	return ret;
}

static u32 mt7915_mcu_gen_dl_mode(u8 feature_set, bool is_wa)
{
	u32 ret = 0;

	ret |= (feature_set & FW_FEATURE_SET_ENCRYPT) ?
	       (DL_MODE_ENCRYPT | DL_MODE_RESET_SEC_IV) : 0;
	ret |= FIELD_PREP(DL_MODE_KEY_IDX,
			  FIELD_GET(FW_FEATURE_SET_KEY_IDX, feature_set));
	ret |= DL_MODE_NEED_RSP;
	ret |= is_wa ? DL_MODE_WORKING_PDA_CR4 : 0;

	return ret;
}

static int
mt7915_mcu_send_ram_firmware(struct mt7915_dev *dev,
			     const struct mt7915_fw_trailer *hdr,
			     const u8 *data, bool is_wa)
{
	int i, offset = 0;
	u32 override = 0, option = 0;

	for (i = 0; i < hdr->n_region; i++) {
		const struct mt7915_fw_region *region;
		int err;
		u32 len, addr, mode;

		region = (const struct mt7915_fw_region *)((const u8 *)hdr -
			 (hdr->n_region - i) * sizeof(*region));
		mode = mt7915_mcu_gen_dl_mode(region->feature_set, is_wa);
		len = le32_to_cpu(region->len);
		addr = le32_to_cpu(region->addr);

		if (region->feature_set & FW_FEATURE_OVERRIDE_ADDR)
			override = addr;

		err = mt7915_mcu_init_download(dev, addr, len, mode);
		if (err) {
			dev_err(dev->mt76.dev, "Download request failed\n");
			return err;
		}

		err = mt76_mcu_send_firmware(&dev->mt76, MCU_CMD(FW_SCATTER),
					     data + offset, len);
		if (err) {
			dev_err(dev->mt76.dev, "Failed to send firmware.\n");
			return err;
		}

		offset += len;
	}

	if (override)
		option |= FW_START_OVERRIDE;

	if (is_wa)
		option |= FW_START_WORKING_PDA_CR4;

	return mt7915_mcu_start_firmware(dev, override, option);
}

static int mt7915_load_ram(struct mt7915_dev *dev)
{
	const struct mt7915_fw_trailer *hdr;
	const struct firmware *fw;
	int ret;

	ret = request_firmware(&fw, MT7915_FIRMWARE_WM, dev->mt76.dev);
	if (ret)
		return ret;

	if (!fw || !fw->data || fw->size < sizeof(*hdr)) {
		dev_err(dev->mt76.dev, "Invalid firmware\n");
		ret = -EINVAL;
		goto out;
	}

	hdr = (const struct mt7915_fw_trailer *)(fw->data + fw->size -
					sizeof(*hdr));

	dev_info(dev->mt76.dev, "WM Firmware Version: %.10s, Build Time: %.15s\n",
		 hdr->fw_ver, hdr->build_date);

	ret = mt7915_mcu_send_ram_firmware(dev, hdr, fw->data, false);
	if (ret) {
		dev_err(dev->mt76.dev, "Failed to start WM firmware\n");
		goto out;
	}

	release_firmware(fw);

	ret = request_firmware(&fw, MT7915_FIRMWARE_WA, dev->mt76.dev);
	if (ret)
		return ret;

	if (!fw || !fw->data || fw->size < sizeof(*hdr)) {
		dev_err(dev->mt76.dev, "Invalid firmware\n");
		ret = -EINVAL;
		goto out;
	}

	hdr = (const struct mt7915_fw_trailer *)(fw->data + fw->size -
					sizeof(*hdr));

	dev_info(dev->mt76.dev, "WA Firmware Version: %.10s, Build Time: %.15s\n",
		 hdr->fw_ver, hdr->build_date);

	ret = mt7915_mcu_send_ram_firmware(dev, hdr, fw->data, true);
	if (ret) {
		dev_err(dev->mt76.dev, "Failed to start WA firmware\n");
		goto out;
	}

	snprintf(dev->mt76.hw->wiphy->fw_version,
		 sizeof(dev->mt76.hw->wiphy->fw_version),
		 "%.10s-%.15s", hdr->fw_ver, hdr->build_date);

out:
	release_firmware(fw);

	return ret;
}

static int mt7915_load_firmware(struct mt7915_dev *dev)
{
	int ret;

	ret = mt7915_load_patch(dev);
	if (ret)
		return ret;

	ret = mt7915_load_ram(dev);
	if (ret)
		return ret;

	if (!mt76_poll_msec(dev, MT_TOP_MISC, MT_TOP_MISC_FW_STATE,
			    FIELD_PREP(MT_TOP_MISC_FW_STATE,
				       FW_STATE_WACPU_RDY), 1000)) {
		dev_err(dev->mt76.dev, "Timeout for initializing firmware\n");
		return -EIO;
	}

	mt76_queue_tx_cleanup(dev, dev->mt76.q_mcu[MT_MCUQ_FWDL], false);

	dev_dbg(dev->mt76.dev, "Firmware init done\n");

	return 0;
}

int mt7915_mcu_fw_log_2_host(struct mt7915_dev *dev, u8 ctrl)
{
	struct {
		u8 ctrl_val;
		u8 pad[3];
	} data = {
		.ctrl_val = ctrl
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(FW_LOG_2_HOST), &data,
				 sizeof(data), true);
}

int mt7915_mcu_fw_dbg_ctrl(struct mt7915_dev *dev, u32 module, u8 level)
{
	struct {
		u8 ver;
		u8 pad;
		__le16 len;
		u8 level;
		u8 rsv[3];
		__le32 module_idx;
	} data = {
		.module_idx = cpu_to_le32(module),
		.level = level,
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(FW_DBG_CTRL), &data,
				 sizeof(data), false);
}

static int mt7915_mcu_set_mwds(struct mt7915_dev *dev, bool enabled)
{
	struct {
		u8 enable;
		u8 _rsv[3];
	} __packed req = {
		.enable = enabled
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_WA_EXT_CMD(MWDS_SUPPORT), &req,
				 sizeof(req), false);
}

int mt7915_mcu_init(struct mt7915_dev *dev)
{
	static const struct mt76_mcu_ops mt7915_mcu_ops = {
		.headroom = sizeof(struct mt7915_mcu_txd),
		.mcu_skb_send_msg = mt7915_mcu_send_message,
		.mcu_parse_response = mt7915_mcu_parse_response,
		.mcu_restart = mt7915_mcu_restart,
	};
	int ret;

	dev->mt76.mcu_ops = &mt7915_mcu_ops;

	ret = mt7915_driver_own(dev);
	if (ret)
		return ret;

	ret = mt7915_load_firmware(dev);
	if (ret)
		return ret;

	set_bit(MT76_STATE_MCU_RUNNING, &dev->mphy.state);
	mt7915_mcu_fw_log_2_host(dev, 0);
	mt7915_mcu_set_mwds(dev, 1);
	mt7915_mcu_wa_cmd(dev, MCU_WA_PARAM_CMD(SET), MCU_WA_PARAM_RED, 0, 0);

	return 0;
}

void mt7915_mcu_exit(struct mt7915_dev *dev)
{
	__mt76_mcu_restart(&dev->mt76);
	if (!mt76_poll_msec(dev, MT_TOP_MISC, MT_TOP_MISC_FW_STATE,
			    FIELD_PREP(MT_TOP_MISC_FW_STATE,
				       FW_STATE_FW_DOWNLOAD), 1000)) {
		dev_err(dev->mt76.dev, "Failed to exit mcu\n");
		return;
	}

	mt76_wr(dev, MT_TOP_LPCR_HOST_BAND0, MT_TOP_LPCR_HOST_FW_OWN);
	skb_queue_purge(&dev->mt76.mcu.res_q);
}

static int
mt7915_mcu_set_rx_hdr_trans_blacklist(struct mt7915_dev *dev, int band)
{
	struct {
		u8 operation;
		u8 count;
		u8 _rsv[2];
		u8 index;
		u8 enable;
		__le16 etype;
	} req = {
		.operation = 1,
		.count = 1,
		.enable = 1,
		.etype = cpu_to_le16(ETH_P_PAE),
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(RX_HDR_TRANS),
				 &req, sizeof(req), false);
}

int mt7915_mcu_set_mac(struct mt7915_dev *dev, int band,
		       bool enable, bool hdr_trans)
{
	struct {
		u8 operation;
		u8 enable;
		u8 check_bssid;
		u8 insert_vlan;
		u8 remove_vlan;
		u8 tid;
		u8 mode;
		u8 rsv;
	} __packed req_trans = {
		.enable = hdr_trans,
	};
	struct {
		u8 enable;
		u8 band;
		u8 rsv[2];
	} __packed req_mac = {
		.enable = enable,
		.band = band,
	};
	int ret;

	ret = mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(RX_HDR_TRANS),
				&req_trans, sizeof(req_trans), false);
	if (ret)
		return ret;

	if (hdr_trans)
		mt7915_mcu_set_rx_hdr_trans_blacklist(dev, band);

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(MAC_INIT_CTRL),
				 &req_mac, sizeof(req_mac), true);
}

int mt7915_mcu_set_scs(struct mt7915_dev *dev, u8 band, bool enable)
{
	struct {
		__le32 cmd;
		u8 band;
		u8 enable;
	} __packed req = {
		.cmd = cpu_to_le32(SCS_ENABLE),
		.band = band,
		.enable = enable + 1,
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(SCS_CTRL), &req,
				 sizeof(req), false);
}

int mt7915_mcu_set_rts_thresh(struct mt7915_phy *phy, u32 val)
{
	struct mt7915_dev *dev = phy->dev;
	struct {
		u8 prot_idx;
		u8 band;
		u8 rsv[2];
		__le32 len_thresh;
		__le32 pkt_thresh;
	} __packed req = {
		.prot_idx = 1,
		.band = phy != &dev->phy,
		.len_thresh = cpu_to_le32(val),
		.pkt_thresh = cpu_to_le32(0x2),
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(PROTECT_CTRL), &req,
				 sizeof(req), true);
}

int mt7915_mcu_update_edca(struct mt7915_dev *dev, void *param)
{
	struct mt7915_mcu_tx *req = (struct mt7915_mcu_tx *)param;
	u8 num = req->total;
	size_t len = sizeof(*req) -
		     (IEEE80211_NUM_ACS - num) * sizeof(struct edca);

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(EDCA_UPDATE), req,
				 len, true);
}

int mt7915_mcu_set_tx(struct mt7915_dev *dev, struct ieee80211_vif *vif)
{
#define TX_CMD_MODE		1
	struct mt7915_mcu_tx req = {
		.valid = true,
		.mode = TX_CMD_MODE,
		.total = IEEE80211_NUM_ACS,
	};
	struct mt7915_vif *mvif = (struct mt7915_vif *)vif->drv_priv;
	int ac;

	for (ac = 0; ac < IEEE80211_NUM_ACS; ac++) {
		struct ieee80211_tx_queue_params *q = &mvif->queue_params[ac];
		struct edca *e = &req.edca[ac];

		e->set = WMM_PARAM_SET;
		e->queue = ac + mvif->wmm_idx * MT7915_MAX_WMM_SETS;
		e->aifs = q->aifs;
		e->txop = cpu_to_le16(q->txop);

		if (q->cw_min)
			e->cw_min = fls(q->cw_min);
		else
			e->cw_min = 5;

		if (q->cw_max)
			e->cw_max = cpu_to_le16(fls(q->cw_max));
		else
			e->cw_max = cpu_to_le16(10);
	}

	return mt7915_mcu_update_edca(dev, &req);
}

int mt7915_mcu_set_pm(struct mt7915_dev *dev, int band, int enter)
{
#define ENTER_PM_STATE		1
#define EXIT_PM_STATE		2
	struct {
		u8 pm_number;
		u8 pm_state;
		u8 bssid[ETH_ALEN];
		u8 dtim_period;
		u8 wlan_idx_lo;
		__le16 bcn_interval;
		__le32 aid;
		__le32 rx_filter;
		u8 band_idx;
		u8 wlan_idx_hi;
		u8 rsv[2];
		__le32 feature;
		u8 omac_idx;
		u8 wmm_idx;
		u8 bcn_loss_cnt;
		u8 bcn_sp_duration;
	} __packed req = {
		.pm_number = 5,
		.pm_state = (enter) ? ENTER_PM_STATE : EXIT_PM_STATE,
		.band_idx = band,
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(PM_STATE_CTRL), &req,
				 sizeof(req), true);
}

int mt7915_mcu_rdd_cmd(struct mt7915_dev *dev,
		       enum mt7915_rdd_cmd cmd, u8 index,
		       u8 rx_sel, u8 val)
{
	struct {
		u8 ctrl;
		u8 rdd_idx;
		u8 rdd_rx_sel;
		u8 val;
		u8 rsv[4];
	} __packed req = {
		.ctrl = cmd,
		.rdd_idx = index,
		.rdd_rx_sel = rx_sel,
		.val = val,
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(SET_RDD_CTRL), &req,
				 sizeof(req), true);
}

int mt7915_mcu_set_fcc5_lpn(struct mt7915_dev *dev, int val)
{
	struct {
		__le32 tag;
		__le16 min_lpn;
		u8 rsv[2];
	} __packed req = {
		.tag = cpu_to_le32(0x1),
		.min_lpn = cpu_to_le16(val),
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(SET_RDD_TH), &req,
				 sizeof(req), true);
}

int mt7915_mcu_set_pulse_th(struct mt7915_dev *dev,
			    const struct mt7915_dfs_pulse *pulse)
{
	struct {
		__le32 tag;

		__le32 max_width;		/* us */
		__le32 max_pwr;			/* dbm */
		__le32 min_pwr;			/* dbm */
		__le32 min_stgr_pri;		/* us */
		__le32 max_stgr_pri;		/* us */
		__le32 min_cr_pri;		/* us */
		__le32 max_cr_pri;		/* us */
	} __packed req = {
		.tag = cpu_to_le32(0x3),

#define __req_field(field) .field = cpu_to_le32(pulse->field)
		__req_field(max_width),
		__req_field(max_pwr),
		__req_field(min_pwr),
		__req_field(min_stgr_pri),
		__req_field(max_stgr_pri),
		__req_field(min_cr_pri),
		__req_field(max_cr_pri),
#undef __req_field
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(SET_RDD_TH), &req,
				 sizeof(req), true);
}

int mt7915_mcu_set_radar_th(struct mt7915_dev *dev, int index,
			    const struct mt7915_dfs_pattern *pattern)
{
	struct {
		__le32 tag;
		__le16 radar_type;

		u8 enb;
		u8 stgr;
		u8 min_crpn;
		u8 max_crpn;
		u8 min_crpr;
		u8 min_pw;
		__le32 min_pri;
		__le32 max_pri;
		u8 max_pw;
		u8 min_crbn;
		u8 max_crbn;
		u8 min_stgpn;
		u8 max_stgpn;
		u8 min_stgpr;
		u8 rsv[2];
		__le32 min_stgpr_diff;
	} __packed req = {
		.tag = cpu_to_le32(0x2),
		.radar_type = cpu_to_le16(index),

#define __req_field_u8(field) .field = pattern->field
#define __req_field_u32(field) .field = cpu_to_le32(pattern->field)
		__req_field_u8(enb),
		__req_field_u8(stgr),
		__req_field_u8(min_crpn),
		__req_field_u8(max_crpn),
		__req_field_u8(min_crpr),
		__req_field_u8(min_pw),
		__req_field_u32(min_pri),
		__req_field_u32(max_pri),
		__req_field_u8(max_pw),
		__req_field_u8(min_crbn),
		__req_field_u8(max_crbn),
		__req_field_u8(min_stgpn),
		__req_field_u8(max_stgpn),
		__req_field_u8(min_stgpr),
		__req_field_u32(min_stgpr_diff),
#undef __req_field_u8
#undef __req_field_u32
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(SET_RDD_TH), &req,
				 sizeof(req), true);
}

int mt7915_mcu_set_chan_info(struct mt7915_phy *phy, int cmd)
{
	struct mt7915_dev *dev = phy->dev;
	struct cfg80211_chan_def *chandef = &phy->mt76->chandef;
	int freq1 = chandef->center_freq1;
	bool ext_phy = phy != &dev->phy;
	struct {
		u8 control_ch;
		u8 center_ch;
		u8 bw;
		u8 tx_streams_num;
		u8 rx_streams;	/* mask or num */
		u8 switch_reason;
		u8 band_idx;
		u8 center_ch2;	/* for 80+80 only */
		__le16 cac_case;
		u8 channel_band;
		u8 rsv0;
		__le32 outband_freq;
		u8 txpower_drop;
		u8 ap_bw;
		u8 ap_center_ch;
		u8 rsv1[57];
	} __packed req = {
		.control_ch = chandef->chan->hw_value,
		.center_ch = ieee80211_frequency_to_channel(freq1),
		.bw = mt7915_mcu_chan_bw(chandef),
		.tx_streams_num = hweight8(phy->mt76->antenna_mask),
		.rx_streams = phy->mt76->antenna_mask,
		.band_idx = ext_phy,
		.channel_band = chandef->chan->band,
	};

#ifdef CONFIG_NL80211_TESTMODE
	if (phy->mt76->test.tx_antenna_mask &&
	    (phy->mt76->test.state == MT76_TM_STATE_TX_FRAMES ||
	     phy->mt76->test.state == MT76_TM_STATE_RX_FRAMES ||
	     phy->mt76->test.state == MT76_TM_STATE_TX_CONT)) {
		req.tx_streams_num = fls(phy->mt76->test.tx_antenna_mask);
		req.rx_streams = phy->mt76->test.tx_antenna_mask;

		if (ext_phy) {
			req.tx_streams_num = 2;
			req.rx_streams >>= 2;
		}
	}
#endif

	if (phy->mt76->hw->conf.flags & IEEE80211_CONF_OFFCHANNEL)
		req.switch_reason = CH_SWITCH_SCAN_BYPASS_DPD;
	else if ((chandef->chan->flags & IEEE80211_CHAN_RADAR) &&
		 chandef->chan->dfs_state != NL80211_DFS_AVAILABLE)
		req.switch_reason = CH_SWITCH_DFS;
	else
		req.switch_reason = CH_SWITCH_NORMAL;

	if (cmd == MCU_EXT_CMD(CHANNEL_SWITCH))
		req.rx_streams = hweight8(req.rx_streams);

	if (chandef->width == NL80211_CHAN_WIDTH_80P80) {
		int freq2 = chandef->center_freq2;

		req.center_ch2 = ieee80211_frequency_to_channel(freq2);
	}

	return mt76_mcu_send_msg(&dev->mt76, cmd, &req, sizeof(req), true);
}

static int mt7915_mcu_set_eeprom_flash(struct mt7915_dev *dev)
{
#define TOTAL_PAGE_MASK		GENMASK(7, 5)
#define PAGE_IDX_MASK		GENMASK(4, 2)
#define PER_PAGE_SIZE		0x400
	struct mt7915_mcu_eeprom req = { .buffer_mode = EE_MODE_BUFFER };
	u8 total = MT7915_EEPROM_SIZE / PER_PAGE_SIZE;
	u8 *eep = (u8 *)dev->mt76.eeprom.data;
	int eep_len;
	int i;

	for (i = 0; i <= total; i++, eep += eep_len) {
		struct sk_buff *skb;
		int ret;

		if (i == total)
			eep_len = MT7915_EEPROM_SIZE % PER_PAGE_SIZE;
		else
			eep_len = PER_PAGE_SIZE;

		skb = mt76_mcu_msg_alloc(&dev->mt76, NULL,
					 sizeof(req) + eep_len);
		if (!skb)
			return -ENOMEM;

		req.format = FIELD_PREP(TOTAL_PAGE_MASK, total) |
			     FIELD_PREP(PAGE_IDX_MASK, i) | EE_FORMAT_WHOLE;
		req.len = cpu_to_le16(eep_len);

		skb_put_data(skb, &req, sizeof(req));
		skb_put_data(skb, eep, eep_len);

		ret = mt76_mcu_skb_send_msg(&dev->mt76, skb,
					    MCU_EXT_CMD(EFUSE_BUFFER_MODE), true);
		if (ret)
			return ret;
	}

	return 0;
}

int mt7915_mcu_set_eeprom(struct mt7915_dev *dev)
{
	struct mt7915_mcu_eeprom req = {
		.buffer_mode = EE_MODE_EFUSE,
		.format = EE_FORMAT_WHOLE,
	};

	if (dev->flash_mode)
		return mt7915_mcu_set_eeprom_flash(dev);

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(EFUSE_BUFFER_MODE),
				 &req, sizeof(req), true);
}

int mt7915_mcu_get_eeprom(struct mt7915_dev *dev, u32 offset)
{
	struct mt7915_mcu_eeprom_info req = {
		.addr = cpu_to_le32(round_down(offset, 16)),
	};
	struct mt7915_mcu_eeprom_info *res;
	struct sk_buff *skb;
	int ret;
	u8 *buf;

	ret = mt76_mcu_send_and_get_msg(&dev->mt76, MCU_EXT_QUERY(EFUSE_ACCESS), &req,
				sizeof(req), true, &skb);
	if (ret)
		return ret;

	res = (struct mt7915_mcu_eeprom_info *)skb->data;
	buf = dev->mt76.eeprom.data + le32_to_cpu(res->addr);
	memcpy(buf, res->data, 16);
	dev_kfree_skb(skb);

	return 0;
}

static int mt7915_mcu_set_pre_cal(struct mt7915_dev *dev, u8 idx,
				  u8 *data, u32 len, int cmd)
{
	struct {
		u8 dir;
		u8 valid;
		__le16 bitmap;
		s8 precal;
		u8 action;
		u8 band;
		u8 idx;
		u8 rsv[4];
		__le32 len;
	} req;
	struct sk_buff *skb;

	skb = mt76_mcu_msg_alloc(&dev->mt76, NULL, sizeof(req) + len);
	if (!skb)
		return -ENOMEM;

	req.idx = idx;
	req.len = cpu_to_le32(len);
	skb_put_data(skb, &req, sizeof(req));
	skb_put_data(skb, data, len);

	return mt76_mcu_skb_send_msg(&dev->mt76, skb, cmd, false);
}

int mt7915_mcu_apply_group_cal(struct mt7915_dev *dev)
{
	u8 idx = 0, *cal = dev->cal, *eep = dev->mt76.eeprom.data;
	u32 total = MT_EE_CAL_GROUP_SIZE;

	if (!(eep[MT_EE_DO_PRE_CAL] & MT_EE_WIFI_CAL_GROUP))
		return 0;

	/*
	 * Items: Rx DCOC, RSSI DCOC, Tx TSSI DCOC, Tx LPFG
	 * Tx FDIQ, Tx DCIQ, Rx FDIQ, Rx FIIQ, ADCDCOC
	 */
	while (total > 0) {
		int ret, len;

		len = min_t(u32, total, MT_EE_CAL_UNIT);

		ret = mt7915_mcu_set_pre_cal(dev, idx, cal, len,
					     MCU_EXT_CMD(GROUP_PRE_CAL_INFO));
		if (ret)
			return ret;

		total -= len;
		cal += len;
		idx++;
	}

	return 0;
}

static int mt7915_find_freq_idx(const u16 *freqs, int n_freqs, u16 cur)
{
	int i;

	for (i = 0; i < n_freqs; i++)
		if (cur == freqs[i])
			return i;

	return -1;
}

static int mt7915_dpd_freq_idx(u16 freq, u8 bw)
{
	static const u16 freq_list[] = {
		5180, 5200, 5220, 5240,
		5260, 5280, 5300, 5320,
		5500, 5520, 5540, 5560,
		5580, 5600, 5620, 5640,
		5660, 5680, 5700, 5745,
		5765, 5785, 5805, 5825
	};
	int offset_2g = ARRAY_SIZE(freq_list);
	int idx;

	if (freq < 4000) {
		if (freq < 2432)
			return offset_2g;
		if (freq < 2457)
			return offset_2g + 1;

		return offset_2g + 2;
	}

	if (bw == NL80211_CHAN_WIDTH_80P80 || bw == NL80211_CHAN_WIDTH_160)
		return -1;

	if (bw != NL80211_CHAN_WIDTH_20) {
		idx = mt7915_find_freq_idx(freq_list, ARRAY_SIZE(freq_list),
					   freq + 10);
		if (idx >= 0)
			return idx;

		idx = mt7915_find_freq_idx(freq_list, ARRAY_SIZE(freq_list),
					   freq - 10);
		if (idx >= 0)
			return idx;
	}

	return mt7915_find_freq_idx(freq_list, ARRAY_SIZE(freq_list), freq);
}

int mt7915_mcu_apply_tx_dpd(struct mt7915_phy *phy)
{
	struct mt7915_dev *dev = phy->dev;
	struct cfg80211_chan_def *chandef = &phy->mt76->chandef;
	u16 total = 2, idx, center_freq = chandef->center_freq1;
	u8 *cal = dev->cal, *eep = dev->mt76.eeprom.data;

	if (!(eep[MT_EE_DO_PRE_CAL] & MT_EE_WIFI_CAL_DPD))
		return 0;

	idx = mt7915_dpd_freq_idx(center_freq, chandef->width);
	if (idx < 0)
		return -EINVAL;

	/* Items: Tx DPD, Tx Flatness */
	idx = idx * 2;
	cal += MT_EE_CAL_GROUP_SIZE;

	while (total--) {
		int ret;

		cal += (idx * MT_EE_CAL_UNIT);
		ret = mt7915_mcu_set_pre_cal(dev, idx, cal, MT_EE_CAL_UNIT,
					     MCU_EXT_CMD(DPD_PRE_CAL_INFO));
		if (ret)
			return ret;

		idx++;
	}

	return 0;
}

int mt7915_mcu_get_temperature(struct mt7915_dev *dev, int index)
{
	struct {
		u8 ctrl_id;
		u8 action;
		u8 band;
		u8 rsv[5];
	} req = {
		.ctrl_id = THERMAL_SENSOR_TEMP_QUERY,
		.action = index,
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(THERMAL_CTRL), &req,
				 sizeof(req), true);
}

int mt7915_mcu_get_tx_rate(struct mt7915_dev *dev, u32 cmd, u16 wlan_idx)
{
	struct {
		__le32 cmd;
		__le16 wlan_idx;
		__le16 ru_idx;
		__le16 direction;
		__le16 dump_group;
	} req = {
		.cmd = cpu_to_le32(cmd),
		.wlan_idx = cpu_to_le16(wlan_idx),
		.dump_group = cpu_to_le16(1),
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(RATE_CTRL), &req,
				 sizeof(req), false);
}

int mt7915_mcu_set_txpower_sku(struct mt7915_phy *phy)
{
#define MT7915_SKU_RATE_NUM		161
	struct mt7915_dev *dev = phy->dev;
	struct mt76_phy *mphy = phy->mt76;
	struct ieee80211_hw *hw = mphy->hw;
	struct mt7915_sku_val {
		u8 format_id;
		u8 limit_type;
		u8 dbdc_idx;
		s8 val[MT7915_SKU_RATE_NUM];
	} __packed req = {
		.format_id = 4,
		.dbdc_idx = phy != &dev->phy,
	};
	struct mt76_power_limits limits_array;
	s8 *la = (s8 *)&limits_array;
	int i, idx, n_chains = hweight8(mphy->antenna_mask);
	int tx_power;

	tx_power = hw->conf.power_level * 2 -
		   mt76_tx_power_nss_delta(n_chains);

	tx_power = mt76_get_rate_power_limits(mphy, mphy->chandef.chan,
					      &limits_array, tx_power);
	mphy->txpower_cur = tx_power;

	for (i = 0, idx = 0; i < ARRAY_SIZE(mt7915_sku_group_len); i++) {
		u8 mcs_num, len = mt7915_sku_group_len[i];
		int j;

		if (i >= SKU_HT_BW20 && i <= SKU_VHT_BW160) {
			mcs_num = 10;

			if (i == SKU_HT_BW20 || i == SKU_VHT_BW20)
				la = (s8 *)&limits_array + 12;
		} else {
			mcs_num = len;
		}

		for (j = 0; j < min_t(u8, mcs_num, len); j++)
			req.val[idx + j] = la[j];

		la += mcs_num;
		idx += len;
	}

	return mt76_mcu_send_msg(&dev->mt76,
				 MCU_EXT_CMD(TX_POWER_FEATURE_CTRL), &req,
				 sizeof(req), true);
}

int mt7915_mcu_set_test_param(struct mt7915_dev *dev, u8 param, bool test_mode,
			      u8 en)
{
	struct {
		u8 test_mode_en;
		u8 param_idx;
		u8 _rsv[2];

		u8 enable;
		u8 _rsv2[3];

		u8 pad[8];
	} __packed req = {
		.test_mode_en = test_mode,
		.param_idx = param,
		.enable = en,
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(ATE_CTRL), &req,
				 sizeof(req), false);
}

int mt7915_mcu_set_sku_en(struct mt7915_phy *phy, bool enable)
{
	struct mt7915_dev *dev = phy->dev;
	struct mt7915_sku {
		u8 format_id;
		u8 sku_enable;
		u8 dbdc_idx;
		u8 rsv;
	} __packed req = {
		.format_id = 0,
		.dbdc_idx = phy != &dev->phy,
		.sku_enable = enable,
	};

	return mt76_mcu_send_msg(&dev->mt76,
				 MCU_EXT_CMD(TX_POWER_FEATURE_CTRL), &req,
				 sizeof(req), true);
}

int mt7915_mcu_set_ser(struct mt7915_dev *dev, u8 action, u8 set, u8 band)
{
	struct {
		u8 action;
		u8 set;
		u8 band;
		u8 rsv;
	} req = {
		.action = action,
		.set = set,
		.band = band,
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(SET_SER_TRIGGER),
				 &req, sizeof(req), false);
}

int mt7915_mcu_set_txbf_module(struct mt7915_dev *dev)
{
#define MT_BF_MODULE_UPDATE               25
	struct {
		u8 action;
		u8 bf_num;
		u8 bf_bitmap;
		u8 bf_sel[8];
		u8 rsv[8];
	} __packed req = {
		.action = MT_BF_MODULE_UPDATE,
		.bf_num = 2,
		.bf_bitmap = GENMASK(1, 0),
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(TXBF_ACTION), &req,
				 sizeof(req), true);
}

int mt7915_mcu_set_txbf_type(struct mt7915_dev *dev)
{
#define MT_BF_TYPE_UPDATE		20
	struct {
		u8 action;
		bool ebf;
		bool ibf;
		u8 rsv;
	} __packed req = {
		.action = MT_BF_TYPE_UPDATE,
		.ebf = true,
		.ibf = dev->ibf,
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(TXBF_ACTION), &req,
				 sizeof(req), true);
}

int mt7915_mcu_set_txbf_sounding(struct mt7915_dev *dev)
{
#define MT_BF_PROCESSING		4
	struct {
		u8 action;
		u8 snd_mode;
		u8 sta_num;
		u8 rsv;
		u8 wlan_idx[4];
		__le32 snd_period;	/* ms */
	} __packed req = {
		.action = true,
		.snd_mode = MT_BF_PROCESSING,
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(TXBF_ACTION), &req,
				 sizeof(req), true);
}

int mt7915_mcu_add_obss_spr(struct mt7915_dev *dev, struct ieee80211_vif *vif,
			    bool enable)
{
#define MT_SPR_ENABLE		1
	struct mt7915_vif *mvif = (struct mt7915_vif *)vif->drv_priv;
	struct {
		u8 action;
		u8 arg_num;
		u8 band_idx;
		u8 status;
		u8 drop_tx_idx;
		u8 sta_idx;	/* 256 sta */
		u8 rsv[2];
		__le32 val;
	} __packed req = {
		.action = MT_SPR_ENABLE,
		.arg_num = 1,
		.band_idx = mvif->band_idx,
		.val = cpu_to_le32(enable),
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(SET_SPR), &req,
				 sizeof(req), true);
}

int mt7915_mcu_get_rx_rate(struct mt7915_phy *phy, struct ieee80211_vif *vif,
			   struct ieee80211_sta *sta, struct rate_info *rate)
{
	struct mt7915_vif *mvif = (struct mt7915_vif *)vif->drv_priv;
	struct mt7915_sta *msta = (struct mt7915_sta *)sta->drv_priv;
	struct mt7915_dev *dev = phy->dev;
	struct mt76_phy *mphy = phy->mt76;
	struct {
		u8 category;
		u8 band;
		__le16 wcid;
	} __packed req = {
		.category = MCU_PHY_STATE_CONTENTION_RX_RATE,
		.band = mvif->band_idx,
		.wcid = cpu_to_le16(msta->wcid.idx),
	};
	struct ieee80211_supported_band *sband;
	struct mt7915_mcu_phy_rx_info *res;
	struct sk_buff *skb;
	int ret;
	bool cck = false;

	ret = mt76_mcu_send_and_get_msg(&dev->mt76, MCU_EXT_CMD(PHY_STAT_INFO),
					&req, sizeof(req), true, &skb);
	if (ret)
		return ret;

	res = (struct mt7915_mcu_phy_rx_info *)skb->data;

	rate->mcs = res->rate;
	rate->nss = res->nsts + 1;

	switch (res->mode) {
	case MT_PHY_TYPE_CCK:
		cck = true;
		fallthrough;
	case MT_PHY_TYPE_OFDM:
		if (mphy->chandef.chan->band == NL80211_BAND_5GHZ)
			sband = &mphy->sband_5g.sband;
		else
			sband = &mphy->sband_2g.sband;

		rate->mcs = mt76_get_rate(&dev->mt76, sband, rate->mcs, cck);
		rate->legacy = sband->bitrates[rate->mcs].bitrate;
		break;
	case MT_PHY_TYPE_HT:
	case MT_PHY_TYPE_HT_GF:
		if (rate->mcs > 31) {
			ret = -EINVAL;
			goto out;
		}

		rate->flags = RATE_INFO_FLAGS_MCS;
		if (res->gi)
			rate->flags |= RATE_INFO_FLAGS_SHORT_GI;
		break;
	case MT_PHY_TYPE_VHT:
		if (rate->mcs > 9) {
			ret = -EINVAL;
			goto out;
		}

		rate->flags = RATE_INFO_FLAGS_VHT_MCS;
		if (res->gi)
			rate->flags |= RATE_INFO_FLAGS_SHORT_GI;
		break;
	case MT_PHY_TYPE_HE_SU:
	case MT_PHY_TYPE_HE_EXT_SU:
	case MT_PHY_TYPE_HE_TB:
	case MT_PHY_TYPE_HE_MU:
		if (res->gi > NL80211_RATE_INFO_HE_GI_3_2 || rate->mcs > 11) {
			ret = -EINVAL;
			goto out;
		}
		rate->he_gi = res->gi;
		rate->flags = RATE_INFO_FLAGS_HE_MCS;
		break;
	default:
		ret = -EINVAL;
		goto out;
	}

	switch (res->bw) {
	case IEEE80211_STA_RX_BW_160:
		rate->bw = RATE_INFO_BW_160;
		break;
	case IEEE80211_STA_RX_BW_80:
		rate->bw = RATE_INFO_BW_80;
		break;
	case IEEE80211_STA_RX_BW_40:
		rate->bw = RATE_INFO_BW_40;
		break;
	default:
		rate->bw = RATE_INFO_BW_20;
		break;
	}

out:
	dev_kfree_skb(skb);

	return ret;
}
