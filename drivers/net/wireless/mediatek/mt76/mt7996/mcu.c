// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2022 MediaTek Inc.
 */

#include <linux/firmware.h>
#include <linux/fs.h>
#include "mt7996.h"
#include "mcu.h"
#include "mac.h"
#include "eeprom.h"

#define fw_name(_dev, name, ...)	({			\
	char *_fw;						\
	switch (mt76_chip(&(_dev)->mt76)) {			\
	case 0x7992:						\
		_fw = MT7992_##name;				\
		break;						\
	case 0x7990:						\
	default:						\
		_fw = MT7996_##name;				\
		break;						\
	}							\
	_fw;							\
})

struct mt7996_patch_hdr {
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

struct mt7996_patch_sec {
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

struct mt7996_fw_trailer {
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

struct mt7996_fw_region {
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

#define HE_PHY(p, c)			u8_get_bits(c, IEEE80211_HE_PHY_##p)
#define HE_MAC(m, c)			u8_get_bits(c, IEEE80211_HE_MAC_##m)
#define EHT_PHY(p, c)			u8_get_bits(c, IEEE80211_EHT_PHY_##p)

static bool sr_scene_detect = true;
module_param(sr_scene_detect, bool, 0644);
MODULE_PARM_DESC(sr_scene_detect, "Enable firmware scene detection algorithm");

static u8
mt7996_mcu_get_sta_nss(u16 mcs_map)
{
	u8 nss;

	for (nss = 8; nss > 0; nss--) {
		u8 nss_mcs = (mcs_map >> (2 * (nss - 1))) & 3;

		if (nss_mcs != IEEE80211_VHT_MCS_NOT_SUPPORTED)
			break;
	}

	return nss - 1;
}

static void
mt7996_mcu_set_sta_he_mcs(struct ieee80211_sta *sta, __le16 *he_mcs,
			  u16 mcs_map)
{
	struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;
	enum nl80211_band band = msta->vif->phy->mt76->chandef.chan->band;
	const u16 *mask = msta->vif->bitrate_mask.control[band].he_mcs;
	int nss, max_nss = sta->deflink.rx_nss > 3 ? 4 : sta->deflink.rx_nss;

	for (nss = 0; nss < max_nss; nss++) {
		int mcs;

		switch ((mcs_map >> (2 * nss)) & 0x3) {
		case IEEE80211_HE_MCS_SUPPORT_0_11:
			mcs = GENMASK(11, 0);
			break;
		case IEEE80211_HE_MCS_SUPPORT_0_9:
			mcs = GENMASK(9, 0);
			break;
		case IEEE80211_HE_MCS_SUPPORT_0_7:
			mcs = GENMASK(7, 0);
			break;
		default:
			mcs = 0;
		}

		mcs = mcs ? fls(mcs & mask[nss]) - 1 : -1;

		switch (mcs) {
		case 0 ... 7:
			mcs = IEEE80211_HE_MCS_SUPPORT_0_7;
			break;
		case 8 ... 9:
			mcs = IEEE80211_HE_MCS_SUPPORT_0_9;
			break;
		case 10 ... 11:
			mcs = IEEE80211_HE_MCS_SUPPORT_0_11;
			break;
		default:
			mcs = IEEE80211_HE_MCS_NOT_SUPPORTED;
			break;
		}
		mcs_map &= ~(0x3 << (nss * 2));
		mcs_map |= mcs << (nss * 2);
	}

	*he_mcs = cpu_to_le16(mcs_map);
}

static void
mt7996_mcu_set_sta_vht_mcs(struct ieee80211_sta *sta, __le16 *vht_mcs,
			   const u16 *mask)
{
	u16 mcs, mcs_map = le16_to_cpu(sta->deflink.vht_cap.vht_mcs.rx_mcs_map);
	int nss, max_nss = sta->deflink.rx_nss > 3 ? 4 : sta->deflink.rx_nss;

	for (nss = 0; nss < max_nss; nss++, mcs_map >>= 2) {
		switch (mcs_map & 0x3) {
		case IEEE80211_VHT_MCS_SUPPORT_0_9:
			mcs = GENMASK(9, 0);
			break;
		case IEEE80211_VHT_MCS_SUPPORT_0_8:
			mcs = GENMASK(8, 0);
			break;
		case IEEE80211_VHT_MCS_SUPPORT_0_7:
			mcs = GENMASK(7, 0);
			break;
		default:
			mcs = 0;
		}

		vht_mcs[nss] = cpu_to_le16(mcs & mask[nss]);
	}
}

static void
mt7996_mcu_set_sta_ht_mcs(struct ieee80211_sta *sta, u8 *ht_mcs,
			  const u8 *mask)
{
	int nss, max_nss = sta->deflink.rx_nss > 3 ? 4 : sta->deflink.rx_nss;

	for (nss = 0; nss < max_nss; nss++)
		ht_mcs[nss] = sta->deflink.ht_cap.mcs.rx_mask[nss] & mask[nss];
}

static int
mt7996_mcu_parse_response(struct mt76_dev *mdev, int cmd,
			  struct sk_buff *skb, int seq)
{
	struct mt7996_mcu_rxd *rxd;
	struct mt7996_mcu_uni_event *event;
	int mcu_cmd = FIELD_GET(__MCU_CMD_FIELD_ID, cmd);
	int ret = 0;

	if (!skb) {
		dev_err(mdev->dev, "Message %08x (seq %d) timeout\n",
			cmd, seq);
		return -ETIMEDOUT;
	}

	rxd = (struct mt7996_mcu_rxd *)skb->data;
	if (seq != rxd->seq)
		return -EAGAIN;

	if (cmd == MCU_CMD(PATCH_SEM_CONTROL)) {
		skb_pull(skb, sizeof(*rxd) - 4);
		ret = *skb->data;
	} else if ((rxd->option & MCU_UNI_CMD_EVENT) &&
		    rxd->eid == MCU_UNI_EVENT_RESULT) {
		skb_pull(skb, sizeof(*rxd));
		event = (struct mt7996_mcu_uni_event *)skb->data;
		ret = le32_to_cpu(event->status);
		/* skip invalid event */
		if (mcu_cmd != event->cid)
			ret = -EAGAIN;
	} else {
		skb_pull(skb, sizeof(struct mt7996_mcu_rxd));
	}

	return ret;
}

static int
mt7996_mcu_send_message(struct mt76_dev *mdev, struct sk_buff *skb,
			int cmd, int *wait_seq)
{
	struct mt7996_dev *dev = container_of(mdev, struct mt7996_dev, mt76);
	int txd_len, mcu_cmd = FIELD_GET(__MCU_CMD_FIELD_ID, cmd);
	struct mt76_connac2_mcu_uni_txd *uni_txd;
	struct mt76_connac2_mcu_txd *mcu_txd;
	enum mt76_mcuq_id qid;
	__le32 *txd;
	u32 val;
	u8 seq;

	mdev->mcu.timeout = 20 * HZ;

	seq = ++dev->mt76.mcu.msg_seq & 0xf;
	if (!seq)
		seq = ++dev->mt76.mcu.msg_seq & 0xf;

	if (cmd == MCU_CMD(FW_SCATTER)) {
		qid = MT_MCUQ_FWDL;
		goto exit;
	}

	txd_len = cmd & __MCU_CMD_FIELD_UNI ? sizeof(*uni_txd) : sizeof(*mcu_txd);
	txd = (__le32 *)skb_push(skb, txd_len);
	if (test_bit(MT76_STATE_MCU_RUNNING, &dev->mphy.state))
		qid = MT_MCUQ_WA;
	else
		qid = MT_MCUQ_WM;

	val = FIELD_PREP(MT_TXD0_TX_BYTES, skb->len) |
	      FIELD_PREP(MT_TXD0_PKT_FMT, MT_TX_TYPE_CMD) |
	      FIELD_PREP(MT_TXD0_Q_IDX, MT_TX_MCU_PORT_RX_Q0);
	txd[0] = cpu_to_le32(val);

	val = FIELD_PREP(MT_TXD1_HDR_FORMAT, MT_HDR_FORMAT_CMD);
	txd[1] = cpu_to_le32(val);

	if (cmd & __MCU_CMD_FIELD_UNI) {
		uni_txd = (struct mt76_connac2_mcu_uni_txd *)txd;
		uni_txd->len = cpu_to_le16(skb->len - sizeof(uni_txd->txd));
		uni_txd->cid = cpu_to_le16(mcu_cmd);
		uni_txd->s2d_index = MCU_S2D_H2CN;
		uni_txd->pkt_type = MCU_PKT_ID;
		uni_txd->seq = seq;

		if (cmd & __MCU_CMD_FIELD_QUERY)
			uni_txd->option = MCU_CMD_UNI_QUERY_ACK;
		else
			uni_txd->option = MCU_CMD_UNI_EXT_ACK;

		if ((cmd & __MCU_CMD_FIELD_WA) && (cmd & __MCU_CMD_FIELD_WM))
			uni_txd->s2d_index = MCU_S2D_H2CN;
		else if (cmd & __MCU_CMD_FIELD_WA)
			uni_txd->s2d_index = MCU_S2D_H2C;
		else if (cmd & __MCU_CMD_FIELD_WM)
			uni_txd->s2d_index = MCU_S2D_H2N;

		goto exit;
	}

	mcu_txd = (struct mt76_connac2_mcu_txd *)txd;
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

int mt7996_mcu_wa_cmd(struct mt7996_dev *dev, int cmd, u32 a1, u32 a2, u32 a3)
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

	return mt76_mcu_send_msg(&dev->mt76, cmd, &req, sizeof(req), false);
}

static void
mt7996_mcu_csa_finish(void *priv, u8 *mac, struct ieee80211_vif *vif)
{
	if (!vif->bss_conf.csa_active || vif->type == NL80211_IFTYPE_STATION)
		return;

	ieee80211_csa_finish(vif, 0);
}

static void
mt7996_mcu_rx_radar_detected(struct mt7996_dev *dev, struct sk_buff *skb)
{
	struct mt76_phy *mphy = &dev->mt76.phy;
	struct mt7996_mcu_rdd_report *r;

	r = (struct mt7996_mcu_rdd_report *)skb->data;

	if (r->band_idx >= ARRAY_SIZE(dev->mt76.phys))
		return;

	if (r->band_idx == MT_RX_SEL2 && !dev->rdd2_phy)
		return;

	if (r->band_idx == MT_RX_SEL2)
		mphy = dev->rdd2_phy->mt76;
	else
		mphy = dev->mt76.phys[r->band_idx];

	if (!mphy)
		return;

	if (r->band_idx == MT_RX_SEL2)
		cfg80211_background_radar_event(mphy->hw->wiphy,
						&dev->rdd2_chandef,
						GFP_ATOMIC);
	else
		ieee80211_radar_detected(mphy->hw);
	dev->hw_pattern++;
}

static void
mt7996_mcu_rx_log_message(struct mt7996_dev *dev, struct sk_buff *skb)
{
#define UNI_EVENT_FW_LOG_FORMAT 0
	struct mt7996_mcu_rxd *rxd = (struct mt7996_mcu_rxd *)skb->data;
	const char *data = (char *)&rxd[1] + 4, *type;
	struct tlv *tlv = (struct tlv *)data;
	int len;

	if (!(rxd->option & MCU_UNI_CMD_EVENT)) {
		len = skb->len - sizeof(*rxd);
		data = (char *)&rxd[1];
		goto out;
	}

	if (le16_to_cpu(tlv->tag) != UNI_EVENT_FW_LOG_FORMAT)
		return;

	data += sizeof(*tlv) + 4;
	len = le16_to_cpu(tlv->len) - sizeof(*tlv) - 4;

out:
	switch (rxd->s2d_index) {
	case 0:
		if (mt7996_debugfs_rx_log(dev, data, len))
			return;

		type = "WM";
		break;
	case 2:
		type = "WA";
		break;
	default:
		type = "unknown";
		break;
	}

	wiphy_info(mt76_hw(dev)->wiphy, "%s: %.*s", type, len, data);
}

static void
mt7996_mcu_cca_finish(void *priv, u8 *mac, struct ieee80211_vif *vif)
{
	if (!vif->bss_conf.color_change_active || vif->type == NL80211_IFTYPE_STATION)
		return;

	ieee80211_color_change_finish(vif, 0);
}

static void
mt7996_mcu_ie_countdown(struct mt7996_dev *dev, struct sk_buff *skb)
{
#define UNI_EVENT_IE_COUNTDOWN_CSA 0
#define UNI_EVENT_IE_COUNTDOWN_BCC 1
	struct header {
		u8 band;
		u8 rsv[3];
	};
	struct mt76_phy *mphy = &dev->mt76.phy;
	struct mt7996_mcu_rxd *rxd = (struct mt7996_mcu_rxd *)skb->data;
	const char *data = (char *)&rxd[1], *tail;
	struct header *hdr = (struct header *)data;
	struct tlv *tlv = (struct tlv *)(data + 4);

	if (hdr->band >= ARRAY_SIZE(dev->mt76.phys))
		return;

	if (hdr->band && dev->mt76.phys[hdr->band])
		mphy = dev->mt76.phys[hdr->band];

	tail = skb->data + skb->len;
	data += sizeof(struct header);
	while (data + sizeof(struct tlv) < tail && le16_to_cpu(tlv->len)) {
		switch (le16_to_cpu(tlv->tag)) {
		case UNI_EVENT_IE_COUNTDOWN_CSA:
			ieee80211_iterate_active_interfaces_atomic(mphy->hw,
					IEEE80211_IFACE_ITER_RESUME_ALL,
					mt7996_mcu_csa_finish, mphy->hw);
			break;
		case UNI_EVENT_IE_COUNTDOWN_BCC:
			ieee80211_iterate_active_interfaces_atomic(mphy->hw,
					IEEE80211_IFACE_ITER_RESUME_ALL,
					mt7996_mcu_cca_finish, mphy->hw);
			break;
		}

		data += le16_to_cpu(tlv->len);
		tlv = (struct tlv *)data;
	}
}

static int
mt7996_mcu_update_tx_gi(struct rate_info *rate, struct all_sta_trx_rate *mcu_rate)
{
	switch (mcu_rate->tx_mode) {
	case MT_PHY_TYPE_CCK:
	case MT_PHY_TYPE_OFDM:
		break;
	case MT_PHY_TYPE_HT:
	case MT_PHY_TYPE_HT_GF:
	case MT_PHY_TYPE_VHT:
		if (mcu_rate->tx_gi)
			rate->flags |= RATE_INFO_FLAGS_SHORT_GI;
		else
			rate->flags &= ~RATE_INFO_FLAGS_SHORT_GI;
		break;
	case MT_PHY_TYPE_HE_SU:
	case MT_PHY_TYPE_HE_EXT_SU:
	case MT_PHY_TYPE_HE_TB:
	case MT_PHY_TYPE_HE_MU:
		if (mcu_rate->tx_gi > NL80211_RATE_INFO_HE_GI_3_2)
			return -EINVAL;
		rate->he_gi = mcu_rate->tx_gi;
		break;
	case MT_PHY_TYPE_EHT_SU:
	case MT_PHY_TYPE_EHT_TRIG:
	case MT_PHY_TYPE_EHT_MU:
		if (mcu_rate->tx_gi > NL80211_RATE_INFO_EHT_GI_3_2)
			return -EINVAL;
		rate->eht_gi = mcu_rate->tx_gi;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static void
mt7996_mcu_rx_all_sta_info_event(struct mt7996_dev *dev, struct sk_buff *skb)
{
	struct mt7996_mcu_all_sta_info_event *res;
	u16 i;

	skb_pull(skb, sizeof(struct mt7996_mcu_rxd));

	res = (struct mt7996_mcu_all_sta_info_event *)skb->data;

	for (i = 0; i < le16_to_cpu(res->sta_num); i++) {
		u8 ac;
		u16 wlan_idx;
		struct mt76_wcid *wcid;

		switch (le16_to_cpu(res->tag)) {
		case UNI_ALL_STA_TXRX_RATE:
			wlan_idx = le16_to_cpu(res->rate[i].wlan_idx);
			wcid = rcu_dereference(dev->mt76.wcid[wlan_idx]);

			if (!wcid)
				break;

			if (mt7996_mcu_update_tx_gi(&wcid->rate, &res->rate[i]))
				dev_err(dev->mt76.dev, "Failed to update TX GI\n");
			break;
		case UNI_ALL_STA_TXRX_ADM_STAT:
			wlan_idx = le16_to_cpu(res->adm_stat[i].wlan_idx);
			wcid = rcu_dereference(dev->mt76.wcid[wlan_idx]);

			if (!wcid)
				break;

			for (ac = 0; ac < IEEE80211_NUM_ACS; ac++) {
				wcid->stats.tx_bytes +=
					le32_to_cpu(res->adm_stat[i].tx_bytes[ac]);
				wcid->stats.rx_bytes +=
					le32_to_cpu(res->adm_stat[i].rx_bytes[ac]);
			}
			break;
		case UNI_ALL_STA_TXRX_MSDU_COUNT:
			wlan_idx = le16_to_cpu(res->msdu_cnt[i].wlan_idx);
			wcid = rcu_dereference(dev->mt76.wcid[wlan_idx]);

			if (!wcid)
				break;

			wcid->stats.tx_packets +=
				le32_to_cpu(res->msdu_cnt[i].tx_msdu_cnt);
			wcid->stats.rx_packets +=
				le32_to_cpu(res->msdu_cnt[i].rx_msdu_cnt);
			break;
		default:
			break;
		}
	}
}

static void
mt7996_mcu_rx_thermal_notify(struct mt7996_dev *dev, struct sk_buff *skb)
{
#define THERMAL_NOTIFY_TAG 0x4
#define THERMAL_NOTIFY 0x2
	struct mt76_phy *mphy = &dev->mt76.phy;
	struct mt7996_mcu_thermal_notify *n;
	struct mt7996_phy *phy;

	n = (struct mt7996_mcu_thermal_notify *)skb->data;

	if (le16_to_cpu(n->tag) != THERMAL_NOTIFY_TAG)
		return;

	if (n->event_id != THERMAL_NOTIFY)
		return;

	if (n->band_idx > MT_BAND2)
		return;

	mphy = dev->mt76.phys[n->band_idx];
	if (!mphy)
		return;

	phy = (struct mt7996_phy *)mphy->priv;
	phy->throttle_state = n->duty_percent;
}

static void
mt7996_mcu_rx_ext_event(struct mt7996_dev *dev, struct sk_buff *skb)
{
	struct mt7996_mcu_rxd *rxd = (struct mt7996_mcu_rxd *)skb->data;

	switch (rxd->ext_eid) {
	case MCU_EXT_EVENT_FW_LOG_2_HOST:
		mt7996_mcu_rx_log_message(dev, skb);
		break;
	default:
		break;
	}
}

static void
mt7996_mcu_rx_unsolicited_event(struct mt7996_dev *dev, struct sk_buff *skb)
{
	struct mt7996_mcu_rxd *rxd = (struct mt7996_mcu_rxd *)skb->data;

	switch (rxd->eid) {
	case MCU_EVENT_EXT:
		mt7996_mcu_rx_ext_event(dev, skb);
		break;
	case MCU_UNI_EVENT_THERMAL:
		mt7996_mcu_rx_thermal_notify(dev, skb);
		break;
	default:
		break;
	}
	dev_kfree_skb(skb);
}

static void
mt7996_mcu_wed_rro_event(struct mt7996_dev *dev, struct sk_buff *skb)
{
	struct mt7996_mcu_wed_rro_event *event = (void *)skb->data;

	if (!dev->has_rro)
		return;

	skb_pull(skb, sizeof(struct mt7996_mcu_rxd) + 4);

	switch (le16_to_cpu(event->tag)) {
	case UNI_WED_RRO_BA_SESSION_STATUS: {
		struct mt7996_mcu_wed_rro_ba_event *e;

		while (skb->len >= sizeof(*e)) {
			struct mt76_rx_tid *tid;
			struct mt76_wcid *wcid;
			u16 idx;

			e = (void *)skb->data;
			idx = le16_to_cpu(e->wlan_id);
			if (idx >= ARRAY_SIZE(dev->mt76.wcid))
				break;

			wcid = rcu_dereference(dev->mt76.wcid[idx]);
			if (!wcid || !wcid->sta)
				break;

			if (e->tid >= ARRAY_SIZE(wcid->aggr))
				break;

			tid = rcu_dereference(wcid->aggr[e->tid]);
			if (!tid)
				break;

			tid->id = le16_to_cpu(e->id);
			skb_pull(skb, sizeof(*e));
		}
		break;
	}
	case UNI_WED_RRO_BA_SESSION_DELETE: {
		struct mt7996_mcu_wed_rro_ba_delete_event *e;

		while (skb->len >= sizeof(*e)) {
			struct mt7996_wed_rro_session_id *session;

			e = (void *)skb->data;
			session = kzalloc(sizeof(*session), GFP_ATOMIC);
			if (!session)
				break;

			session->id = le16_to_cpu(e->session_id);

			spin_lock_bh(&dev->wed_rro.lock);
			list_add_tail(&session->list, &dev->wed_rro.poll_list);
			spin_unlock_bh(&dev->wed_rro.lock);

			ieee80211_queue_work(mt76_hw(dev), &dev->wed_rro.work);
			skb_pull(skb, sizeof(*e));
		}
		break;
	}
	default:
		break;
	}
}

static void
mt7996_mcu_uni_rx_unsolicited_event(struct mt7996_dev *dev, struct sk_buff *skb)
{
	struct mt7996_mcu_rxd *rxd = (struct mt7996_mcu_rxd *)skb->data;

	switch (rxd->eid) {
	case MCU_UNI_EVENT_FW_LOG_2_HOST:
		mt7996_mcu_rx_log_message(dev, skb);
		break;
	case MCU_UNI_EVENT_IE_COUNTDOWN:
		mt7996_mcu_ie_countdown(dev, skb);
		break;
	case MCU_UNI_EVENT_RDD_REPORT:
		mt7996_mcu_rx_radar_detected(dev, skb);
		break;
	case MCU_UNI_EVENT_ALL_STA_INFO:
		mt7996_mcu_rx_all_sta_info_event(dev, skb);
		break;
	case MCU_UNI_EVENT_WED_RRO:
		mt7996_mcu_wed_rro_event(dev, skb);
		break;
	default:
		break;
	}
	dev_kfree_skb(skb);
}

void mt7996_mcu_rx_event(struct mt7996_dev *dev, struct sk_buff *skb)
{
	struct mt7996_mcu_rxd *rxd = (struct mt7996_mcu_rxd *)skb->data;

	if (rxd->option & MCU_UNI_CMD_UNSOLICITED_EVENT) {
		mt7996_mcu_uni_rx_unsolicited_event(dev, skb);
		return;
	}

	/* WA still uses legacy event*/
	if (rxd->ext_eid == MCU_EXT_EVENT_FW_LOG_2_HOST ||
	    !rxd->seq)
		mt7996_mcu_rx_unsolicited_event(dev, skb);
	else
		mt76_mcu_rx_event(&dev->mt76, skb);
}

static struct tlv *
mt7996_mcu_add_uni_tlv(struct sk_buff *skb, u16 tag, u16 len)
{
	struct tlv *ptlv = skb_put(skb, len);

	ptlv->tag = cpu_to_le16(tag);
	ptlv->len = cpu_to_le16(len);

	return ptlv;
}

static void
mt7996_mcu_bss_rfch_tlv(struct sk_buff *skb, struct ieee80211_vif *vif,
			struct mt7996_phy *phy)
{
	static const u8 rlm_ch_band[] = {
		[NL80211_BAND_2GHZ] = 1,
		[NL80211_BAND_5GHZ] = 2,
		[NL80211_BAND_6GHZ] = 3,
	};
	struct cfg80211_chan_def *chandef = &phy->mt76->chandef;
	struct bss_rlm_tlv *ch;
	struct tlv *tlv;
	int freq1 = chandef->center_freq1;

	tlv = mt7996_mcu_add_uni_tlv(skb, UNI_BSS_INFO_RLM, sizeof(*ch));

	ch = (struct bss_rlm_tlv *)tlv;
	ch->control_channel = chandef->chan->hw_value;
	ch->center_chan = ieee80211_frequency_to_channel(freq1);
	ch->bw = mt76_connac_chan_bw(chandef);
	ch->tx_streams = hweight8(phy->mt76->antenna_mask);
	ch->rx_streams = hweight8(phy->mt76->antenna_mask);
	ch->band = rlm_ch_band[chandef->chan->band];

	if (chandef->width == NL80211_CHAN_WIDTH_80P80) {
		int freq2 = chandef->center_freq2;

		ch->center_chan2 = ieee80211_frequency_to_channel(freq2);
	}
}

static void
mt7996_mcu_bss_ra_tlv(struct sk_buff *skb, struct ieee80211_vif *vif,
		      struct mt7996_phy *phy)
{
	struct bss_ra_tlv *ra;
	struct tlv *tlv;

	tlv = mt7996_mcu_add_uni_tlv(skb, UNI_BSS_INFO_RA, sizeof(*ra));

	ra = (struct bss_ra_tlv *)tlv;
	ra->short_preamble = true;
}

static void
mt7996_mcu_bss_he_tlv(struct sk_buff *skb, struct ieee80211_vif *vif,
		      struct mt7996_phy *phy)
{
#define DEFAULT_HE_PE_DURATION		4
#define DEFAULT_HE_DURATION_RTS_THRES	1023
	const struct ieee80211_sta_he_cap *cap;
	struct bss_info_uni_he *he;
	struct tlv *tlv;

	cap = mt76_connac_get_he_phy_cap(phy->mt76, vif);

	tlv = mt7996_mcu_add_uni_tlv(skb, UNI_BSS_INFO_HE_BASIC, sizeof(*he));

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

static void
mt7996_mcu_bss_mbssid_tlv(struct sk_buff *skb, struct ieee80211_vif *vif,
			  struct mt7996_phy *phy, int enable)
{
	struct bss_info_uni_mbssid *mbssid;
	struct tlv *tlv;

	if (!vif->bss_conf.bssid_indicator)
		return;

	tlv = mt7996_mcu_add_uni_tlv(skb, UNI_BSS_INFO_11V_MBSSID, sizeof(*mbssid));

	mbssid = (struct bss_info_uni_mbssid *)tlv;

	if (enable) {
		mbssid->max_indicator = vif->bss_conf.bssid_indicator;
		mbssid->mbss_idx = vif->bss_conf.bssid_index;
		mbssid->tx_bss_omac_idx = 0;
	}
}

static void
mt7996_mcu_bss_bmc_tlv(struct sk_buff *skb, struct ieee80211_vif *vif,
		       struct mt7996_phy *phy)
{
	struct mt76_vif *mvif = (struct mt76_vif *)vif->drv_priv;
	struct bss_rate_tlv *bmc;
	struct cfg80211_chan_def *chandef = &phy->mt76->chandef;
	enum nl80211_band band = chandef->chan->band;
	struct tlv *tlv;
	u8 idx = mvif->mcast_rates_idx ?
		 mvif->mcast_rates_idx : mvif->basic_rates_idx;

	tlv = mt7996_mcu_add_uni_tlv(skb, UNI_BSS_INFO_RATE, sizeof(*bmc));

	bmc = (struct bss_rate_tlv *)tlv;

	bmc->short_preamble = (band == NL80211_BAND_2GHZ);
	bmc->bc_fixed_rate = idx;
	bmc->mc_fixed_rate = idx;
}

static void
mt7996_mcu_bss_txcmd_tlv(struct sk_buff *skb, bool en)
{
	struct bss_txcmd_tlv *txcmd;
	struct tlv *tlv;

	tlv = mt7996_mcu_add_uni_tlv(skb, UNI_BSS_INFO_TXCMD, sizeof(*txcmd));

	txcmd = (struct bss_txcmd_tlv *)tlv;
	txcmd->txcmd_mode = en;
}

static void
mt7996_mcu_bss_mld_tlv(struct sk_buff *skb, struct ieee80211_vif *vif)
{
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct bss_mld_tlv *mld;
	struct tlv *tlv;

	tlv = mt7996_mcu_add_uni_tlv(skb, UNI_BSS_INFO_MLD, sizeof(*mld));

	mld = (struct bss_mld_tlv *)tlv;
	mld->group_mld_id = 0xff;
	mld->own_mld_id = mvif->mt76.idx;
	mld->remap_idx = 0xff;
}

static void
mt7996_mcu_bss_sec_tlv(struct sk_buff *skb, struct ieee80211_vif *vif)
{
	struct mt76_vif *mvif = (struct mt76_vif *)vif->drv_priv;
	struct bss_sec_tlv *sec;
	struct tlv *tlv;

	tlv = mt7996_mcu_add_uni_tlv(skb, UNI_BSS_INFO_SEC, sizeof(*sec));

	sec = (struct bss_sec_tlv *)tlv;
	sec->cipher = mvif->cipher;
}

static int
mt7996_mcu_muar_config(struct mt7996_phy *phy, struct ieee80211_vif *vif,
		       bool bssid, bool enable)
{
#define UNI_MUAR_ENTRY 2
	struct mt7996_dev *dev = phy->dev;
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	u32 idx = mvif->mt76.omac_idx - REPEATER_BSSID_START;
	const u8 *addr = vif->addr;

	struct {
		struct {
			u8 band;
			u8 __rsv[3];
		} hdr;

		__le16 tag;
		__le16 len;

		bool smesh;
		u8 bssid;
		u8 index;
		u8 entry_add;
		u8 addr[ETH_ALEN];
		u8 __rsv[2];
	} __packed req = {
		.hdr.band = phy->mt76->band_idx,
		.tag = cpu_to_le16(UNI_MUAR_ENTRY),
		.len = cpu_to_le16(sizeof(req) - sizeof(req.hdr)),
		.smesh = false,
		.index = idx * 2 + bssid,
		.entry_add = true,
	};

	if (bssid)
		addr = vif->bss_conf.bssid;

	if (enable)
		memcpy(req.addr, addr, ETH_ALEN);

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(REPT_MUAR), &req,
				 sizeof(req), true);
}

static void
mt7996_mcu_bss_ifs_timing_tlv(struct sk_buff *skb, struct ieee80211_vif *vif)
{
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct mt7996_phy *phy = mvif->phy;
	struct bss_ifs_time_tlv *ifs_time;
	struct tlv *tlv;
	bool is_2ghz = phy->mt76->chandef.chan->band == NL80211_BAND_2GHZ;

	tlv = mt7996_mcu_add_uni_tlv(skb, UNI_BSS_INFO_IFS_TIME, sizeof(*ifs_time));

	ifs_time = (struct bss_ifs_time_tlv *)tlv;
	ifs_time->slot_valid = true;
	ifs_time->sifs_valid = true;
	ifs_time->rifs_valid = true;
	ifs_time->eifs_valid = true;

	ifs_time->slot_time = cpu_to_le16(phy->slottime);
	ifs_time->sifs_time = cpu_to_le16(10);
	ifs_time->rifs_time = cpu_to_le16(2);
	ifs_time->eifs_time = cpu_to_le16(is_2ghz ? 78 : 84);

	if (is_2ghz) {
		ifs_time->eifs_cck_valid = true;
		ifs_time->eifs_cck_time = cpu_to_le16(314);
	}
}

static int
mt7996_mcu_bss_basic_tlv(struct sk_buff *skb,
			 struct ieee80211_vif *vif,
			 struct ieee80211_sta *sta,
			 struct mt76_phy *phy, u16 wlan_idx,
			 bool enable)
{
	struct mt76_vif *mvif = (struct mt76_vif *)vif->drv_priv;
	struct cfg80211_chan_def *chandef = &phy->chandef;
	struct mt76_connac_bss_basic_tlv *bss;
	u32 type = CONNECTION_INFRA_AP;
	u16 sta_wlan_idx = wlan_idx;
	struct tlv *tlv;
	int idx;

	switch (vif->type) {
	case NL80211_IFTYPE_MESH_POINT:
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_MONITOR:
		break;
	case NL80211_IFTYPE_STATION:
		if (enable) {
			rcu_read_lock();
			if (!sta)
				sta = ieee80211_find_sta(vif,
							 vif->bss_conf.bssid);
			/* TODO: enable BSS_INFO_UAPSD & BSS_INFO_PM */
			if (sta) {
				struct mt76_wcid *wcid;

				wcid = (struct mt76_wcid *)sta->drv_priv;
				sta_wlan_idx = wcid->idx;
			}
			rcu_read_unlock();
		}
		type = CONNECTION_INFRA_STA;
		break;
	case NL80211_IFTYPE_ADHOC:
		type = CONNECTION_IBSS_ADHOC;
		break;
	default:
		WARN_ON(1);
		break;
	}

	tlv = mt7996_mcu_add_uni_tlv(skb, UNI_BSS_INFO_BASIC, sizeof(*bss));

	bss = (struct mt76_connac_bss_basic_tlv *)tlv;
	bss->bcn_interval = cpu_to_le16(vif->bss_conf.beacon_int);
	bss->dtim_period = vif->bss_conf.dtim_period;
	bss->bmc_tx_wlan_idx = cpu_to_le16(wlan_idx);
	bss->sta_idx = cpu_to_le16(sta_wlan_idx);
	bss->conn_type = cpu_to_le32(type);
	bss->omac_idx = mvif->omac_idx;
	bss->band_idx = mvif->band_idx;
	bss->wmm_idx = mvif->wmm_idx;
	bss->conn_state = !enable;
	bss->active = enable;

	idx = mvif->omac_idx > EXT_BSSID_START ? HW_BSSID_0 : mvif->omac_idx;
	bss->hw_bss_idx = idx;

	if (vif->type == NL80211_IFTYPE_MONITOR) {
		memcpy(bss->bssid, phy->macaddr, ETH_ALEN);
		return 0;
	}

	memcpy(bss->bssid, vif->bss_conf.bssid, ETH_ALEN);
	bss->bcn_interval = cpu_to_le16(vif->bss_conf.beacon_int);
	bss->dtim_period = vif->bss_conf.dtim_period;
	bss->phymode = mt76_connac_get_phy_mode(phy, vif,
						chandef->chan->band, NULL);
	bss->phymode_ext = mt76_connac_get_phy_mode_ext(phy, vif,
							chandef->chan->band);

	return 0;
}

static struct sk_buff *
__mt7996_mcu_alloc_bss_req(struct mt76_dev *dev, struct mt76_vif *mvif, int len)
{
	struct bss_req_hdr hdr = {
		.bss_idx = mvif->idx,
	};
	struct sk_buff *skb;

	skb = mt76_mcu_msg_alloc(dev, NULL, len);
	if (!skb)
		return ERR_PTR(-ENOMEM);

	skb_put_data(skb, &hdr, sizeof(hdr));

	return skb;
}

int mt7996_mcu_add_bss_info(struct mt7996_phy *phy,
			    struct ieee80211_vif *vif, int enable)
{
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct mt7996_dev *dev = phy->dev;
	struct sk_buff *skb;

	if (mvif->mt76.omac_idx >= REPEATER_BSSID_START) {
		mt7996_mcu_muar_config(phy, vif, false, enable);
		mt7996_mcu_muar_config(phy, vif, true, enable);
	}

	skb = __mt7996_mcu_alloc_bss_req(&dev->mt76, &mvif->mt76,
					 MT7996_BSS_UPDATE_MAX_SIZE);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	/* bss_basic must be first */
	mt7996_mcu_bss_basic_tlv(skb, vif, NULL, phy->mt76,
				 mvif->sta.wcid.idx, enable);
	mt7996_mcu_bss_sec_tlv(skb, vif);

	if (vif->type == NL80211_IFTYPE_MONITOR)
		goto out;

	if (enable) {
		mt7996_mcu_bss_rfch_tlv(skb, vif, phy);
		mt7996_mcu_bss_bmc_tlv(skb, vif, phy);
		mt7996_mcu_bss_ra_tlv(skb, vif, phy);
		mt7996_mcu_bss_txcmd_tlv(skb, true);
		mt7996_mcu_bss_ifs_timing_tlv(skb, vif);

		if (vif->bss_conf.he_support)
			mt7996_mcu_bss_he_tlv(skb, vif, phy);

		/* this tag is necessary no matter if the vif is MLD */
		mt7996_mcu_bss_mld_tlv(skb, vif);
	}

	mt7996_mcu_bss_mbssid_tlv(skb, vif, phy, enable);

out:
	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_WMWA_UNI_CMD(BSS_INFO_UPDATE), true);
}

int mt7996_mcu_set_timing(struct mt7996_phy *phy, struct ieee80211_vif *vif)
{
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct mt7996_dev *dev = phy->dev;
	struct sk_buff *skb;

	skb = __mt7996_mcu_alloc_bss_req(&dev->mt76, &mvif->mt76,
					 MT7996_BSS_UPDATE_MAX_SIZE);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	mt7996_mcu_bss_ifs_timing_tlv(skb, vif);

	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_WMWA_UNI_CMD(BSS_INFO_UPDATE), true);
}

static int
mt7996_mcu_sta_ba(struct mt7996_dev *dev, struct mt76_vif *mvif,
		  struct ieee80211_ampdu_params *params,
		  bool enable, bool tx)
{
	struct mt76_wcid *wcid = (struct mt76_wcid *)params->sta->drv_priv;
	struct sta_rec_ba_uni *ba;
	struct sk_buff *skb;
	struct tlv *tlv;

	skb = __mt76_connac_mcu_alloc_sta_req(&dev->mt76, mvif, wcid,
					      MT7996_STA_UPDATE_MAX_SIZE);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_BA, sizeof(*ba));

	ba = (struct sta_rec_ba_uni *)tlv;
	ba->ba_type = tx ? MT_BA_TYPE_ORIGINATOR : MT_BA_TYPE_RECIPIENT;
	ba->winsize = cpu_to_le16(params->buf_size);
	ba->ssn = cpu_to_le16(params->ssn);
	ba->ba_en = enable << params->tid;
	ba->amsdu = params->amsdu;
	ba->tid = params->tid;
	ba->ba_rdd_rro = !tx && enable && dev->has_rro;

	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_WMWA_UNI_CMD(STA_REC_UPDATE), true);
}

/** starec & wtbl **/
int mt7996_mcu_add_tx_ba(struct mt7996_dev *dev,
			 struct ieee80211_ampdu_params *params,
			 bool enable)
{
	struct mt7996_sta *msta = (struct mt7996_sta *)params->sta->drv_priv;
	struct mt7996_vif *mvif = msta->vif;

	if (enable && !params->amsdu)
		msta->wcid.amsdu = false;

	return mt7996_mcu_sta_ba(dev, &mvif->mt76, params, enable, true);
}

int mt7996_mcu_add_rx_ba(struct mt7996_dev *dev,
			 struct ieee80211_ampdu_params *params,
			 bool enable)
{
	struct mt7996_sta *msta = (struct mt7996_sta *)params->sta->drv_priv;
	struct mt7996_vif *mvif = msta->vif;

	return mt7996_mcu_sta_ba(dev, &mvif->mt76, params, enable, false);
}

static void
mt7996_mcu_sta_he_tlv(struct sk_buff *skb, struct ieee80211_sta *sta)
{
	struct ieee80211_he_cap_elem *elem = &sta->deflink.he_cap.he_cap_elem;
	struct ieee80211_he_mcs_nss_supp mcs_map;
	struct sta_rec_he_v2 *he;
	struct tlv *tlv;
	int i = 0;

	if (!sta->deflink.he_cap.has_he)
		return;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_HE_V2, sizeof(*he));

	he = (struct sta_rec_he_v2 *)tlv;
	for (i = 0; i < 11; i++) {
		if (i < 6)
			he->he_mac_cap[i] = elem->mac_cap_info[i];
		he->he_phy_cap[i] = elem->phy_cap_info[i];
	}

	mcs_map = sta->deflink.he_cap.he_mcs_nss_supp;
	switch (sta->deflink.bandwidth) {
	case IEEE80211_STA_RX_BW_160:
		if (elem->phy_cap_info[0] &
		    IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_80PLUS80_MHZ_IN_5G)
			mt7996_mcu_set_sta_he_mcs(sta,
						  &he->max_nss_mcs[CMD_HE_MCS_BW8080],
						  le16_to_cpu(mcs_map.rx_mcs_80p80));

		mt7996_mcu_set_sta_he_mcs(sta,
					  &he->max_nss_mcs[CMD_HE_MCS_BW160],
					  le16_to_cpu(mcs_map.rx_mcs_160));
		fallthrough;
	default:
		mt7996_mcu_set_sta_he_mcs(sta,
					  &he->max_nss_mcs[CMD_HE_MCS_BW80],
					  le16_to_cpu(mcs_map.rx_mcs_80));
		break;
	}

	he->pkt_ext = 2;
}

static void
mt7996_mcu_sta_he_6g_tlv(struct sk_buff *skb, struct ieee80211_sta *sta)
{
	struct sta_rec_he_6g_capa *he_6g;
	struct tlv *tlv;

	if (!sta->deflink.he_6ghz_capa.capa)
		return;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_HE_6G, sizeof(*he_6g));

	he_6g = (struct sta_rec_he_6g_capa *)tlv;
	he_6g->capa = sta->deflink.he_6ghz_capa.capa;
}

static void
mt7996_mcu_sta_eht_tlv(struct sk_buff *skb, struct ieee80211_sta *sta)
{
	struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;
	struct ieee80211_vif *vif = container_of((void *)msta->vif,
						 struct ieee80211_vif, drv_priv);
	struct ieee80211_eht_mcs_nss_supp *mcs_map;
	struct ieee80211_eht_cap_elem_fixed *elem;
	struct sta_rec_eht *eht;
	struct tlv *tlv;

	if (!sta->deflink.eht_cap.has_eht)
		return;

	mcs_map = &sta->deflink.eht_cap.eht_mcs_nss_supp;
	elem = &sta->deflink.eht_cap.eht_cap_elem;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_EHT, sizeof(*eht));

	eht = (struct sta_rec_eht *)tlv;
	eht->tid_bitmap = 0xff;
	eht->mac_cap = cpu_to_le16(*(u16 *)elem->mac_cap_info);
	eht->phy_cap = cpu_to_le64(*(u64 *)elem->phy_cap_info);
	eht->phy_cap_ext = cpu_to_le64(elem->phy_cap_info[8]);

	if (vif->type != NL80211_IFTYPE_STATION &&
	    (sta->deflink.he_cap.he_cap_elem.phy_cap_info[0] &
	     (IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_IN_2G |
	      IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_80MHZ_IN_5G |
	      IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G |
	      IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_80PLUS80_MHZ_IN_5G)) == 0) {
		memcpy(eht->mcs_map_bw20, &mcs_map->only_20mhz,
		       sizeof(eht->mcs_map_bw20));
		return;
	}

	memcpy(eht->mcs_map_bw80, &mcs_map->bw._80, sizeof(eht->mcs_map_bw80));
	memcpy(eht->mcs_map_bw160, &mcs_map->bw._160, sizeof(eht->mcs_map_bw160));
	memcpy(eht->mcs_map_bw320, &mcs_map->bw._320, sizeof(eht->mcs_map_bw320));
}

static void
mt7996_mcu_sta_ht_tlv(struct sk_buff *skb, struct ieee80211_sta *sta)
{
	struct sta_rec_ht_uni *ht;
	struct tlv *tlv;

	if (!sta->deflink.ht_cap.ht_supported)
		return;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_HT, sizeof(*ht));

	ht = (struct sta_rec_ht_uni *)tlv;
	ht->ht_cap = cpu_to_le16(sta->deflink.ht_cap.cap);
	ht->ampdu_param = u8_encode_bits(sta->deflink.ht_cap.ampdu_factor,
					 IEEE80211_HT_AMPDU_PARM_FACTOR) |
			  u8_encode_bits(sta->deflink.ht_cap.ampdu_density,
					 IEEE80211_HT_AMPDU_PARM_DENSITY);
}

static void
mt7996_mcu_sta_vht_tlv(struct sk_buff *skb, struct ieee80211_sta *sta)
{
	struct sta_rec_vht *vht;
	struct tlv *tlv;

	/* For 6G band, this tlv is necessary to let hw work normally */
	if (!sta->deflink.he_6ghz_capa.capa && !sta->deflink.vht_cap.vht_supported)
		return;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_VHT, sizeof(*vht));

	vht = (struct sta_rec_vht *)tlv;
	vht->vht_cap = cpu_to_le32(sta->deflink.vht_cap.cap);
	vht->vht_rx_mcs_map = sta->deflink.vht_cap.vht_mcs.rx_mcs_map;
	vht->vht_tx_mcs_map = sta->deflink.vht_cap.vht_mcs.tx_mcs_map;
}

static void
mt7996_mcu_sta_amsdu_tlv(struct mt7996_dev *dev, struct sk_buff *skb,
			 struct ieee80211_vif *vif, struct ieee80211_sta *sta)
{
	struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;
	struct sta_rec_amsdu *amsdu;
	struct tlv *tlv;

	if (vif->type != NL80211_IFTYPE_STATION &&
	    vif->type != NL80211_IFTYPE_MESH_POINT &&
	    vif->type != NL80211_IFTYPE_AP)
		return;

	if (!sta->deflink.agg.max_amsdu_len)
		return;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_HW_AMSDU, sizeof(*amsdu));
	amsdu = (struct sta_rec_amsdu *)tlv;
	amsdu->max_amsdu_num = 8;
	amsdu->amsdu_en = true;
	msta->wcid.amsdu = true;

	switch (sta->deflink.agg.max_amsdu_len) {
	case IEEE80211_MAX_MPDU_LEN_VHT_11454:
		amsdu->max_mpdu_size =
			IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_11454;
		return;
	case IEEE80211_MAX_MPDU_LEN_HT_7935:
	case IEEE80211_MAX_MPDU_LEN_VHT_7991:
		amsdu->max_mpdu_size = IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_7991;
		return;
	default:
		amsdu->max_mpdu_size = IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_3895;
		return;
	}
}

static void
mt7996_mcu_sta_muru_tlv(struct mt7996_dev *dev, struct sk_buff *skb,
			struct ieee80211_vif *vif, struct ieee80211_sta *sta)
{
	struct ieee80211_he_cap_elem *elem = &sta->deflink.he_cap.he_cap_elem;
	struct sta_rec_muru *muru;
	struct tlv *tlv;

	if (vif->type != NL80211_IFTYPE_STATION &&
	    vif->type != NL80211_IFTYPE_AP)
		return;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_MURU, sizeof(*muru));

	muru = (struct sta_rec_muru *)tlv;
	muru->cfg.mimo_dl_en = vif->bss_conf.eht_mu_beamformer ||
			       vif->bss_conf.he_mu_beamformer ||
			       vif->bss_conf.vht_mu_beamformer ||
			       vif->bss_conf.vht_mu_beamformee;
	muru->cfg.ofdma_dl_en = true;

	if (sta->deflink.vht_cap.vht_supported)
		muru->mimo_dl.vht_mu_bfee =
			!!(sta->deflink.vht_cap.cap & IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE);

	if (!sta->deflink.he_cap.has_he)
		return;

	muru->mimo_dl.partial_bw_dl_mimo =
		HE_PHY(CAP6_PARTIAL_BANDWIDTH_DL_MUMIMO, elem->phy_cap_info[6]);

	muru->mimo_ul.full_ul_mimo =
		HE_PHY(CAP2_UL_MU_FULL_MU_MIMO, elem->phy_cap_info[2]);
	muru->mimo_ul.partial_ul_mimo =
		HE_PHY(CAP2_UL_MU_PARTIAL_MU_MIMO, elem->phy_cap_info[2]);

	muru->ofdma_dl.punc_pream_rx =
		HE_PHY(CAP1_PREAMBLE_PUNC_RX_MASK, elem->phy_cap_info[1]);
	muru->ofdma_dl.he_20m_in_40m_2g =
		HE_PHY(CAP8_20MHZ_IN_40MHZ_HE_PPDU_IN_2G, elem->phy_cap_info[8]);
	muru->ofdma_dl.he_20m_in_160m =
		HE_PHY(CAP8_20MHZ_IN_160MHZ_HE_PPDU, elem->phy_cap_info[8]);
	muru->ofdma_dl.he_80m_in_160m =
		HE_PHY(CAP8_80MHZ_IN_160MHZ_HE_PPDU, elem->phy_cap_info[8]);

	muru->ofdma_ul.t_frame_dur =
		HE_MAC(CAP1_TF_MAC_PAD_DUR_MASK, elem->mac_cap_info[1]);
	muru->ofdma_ul.mu_cascading =
		HE_MAC(CAP2_MU_CASCADING, elem->mac_cap_info[2]);
	muru->ofdma_ul.uo_ra =
		HE_MAC(CAP3_OFDMA_RA, elem->mac_cap_info[3]);
	muru->ofdma_ul.rx_ctrl_frame_to_mbss =
		HE_MAC(CAP3_RX_CTRL_FRAME_TO_MULTIBSS, elem->mac_cap_info[3]);
}

static inline bool
mt7996_is_ebf_supported(struct mt7996_phy *phy, struct ieee80211_vif *vif,
			struct ieee80211_sta *sta, bool bfee)
{
	int sts = hweight16(phy->mt76->chainmask);

	if (vif->type != NL80211_IFTYPE_STATION &&
	    vif->type != NL80211_IFTYPE_AP)
		return false;

	if (!bfee && sts < 2)
		return false;

	if (sta->deflink.eht_cap.has_eht) {
		struct ieee80211_sta_eht_cap *pc = &sta->deflink.eht_cap;
		struct ieee80211_eht_cap_elem_fixed *pe = &pc->eht_cap_elem;

		if (bfee)
			return vif->bss_conf.eht_su_beamformee &&
			       EHT_PHY(CAP0_SU_BEAMFORMEE, pe->phy_cap_info[0]);
		else
			return vif->bss_conf.eht_su_beamformer &&
			       EHT_PHY(CAP0_SU_BEAMFORMER, pe->phy_cap_info[0]);
	}

	if (sta->deflink.he_cap.has_he) {
		struct ieee80211_he_cap_elem *pe = &sta->deflink.he_cap.he_cap_elem;

		if (bfee)
			return vif->bss_conf.he_su_beamformee &&
			       HE_PHY(CAP3_SU_BEAMFORMER, pe->phy_cap_info[3]);
		else
			return vif->bss_conf.he_su_beamformer &&
			       HE_PHY(CAP4_SU_BEAMFORMEE, pe->phy_cap_info[4]);
	}

	if (sta->deflink.vht_cap.vht_supported) {
		u32 cap = sta->deflink.vht_cap.cap;

		if (bfee)
			return vif->bss_conf.vht_su_beamformee &&
			       (cap & IEEE80211_VHT_CAP_SU_BEAMFORMER_CAPABLE);
		else
			return vif->bss_conf.vht_su_beamformer &&
			       (cap & IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE);
	}

	return false;
}

static void
mt7996_mcu_sta_sounding_rate(struct sta_rec_bf *bf)
{
	bf->sounding_phy = MT_PHY_TYPE_OFDM;
	bf->ndp_rate = 0;				/* mcs0 */
	bf->ndpa_rate = MT7996_CFEND_RATE_DEFAULT;	/* ofdm 24m */
	bf->rept_poll_rate = MT7996_CFEND_RATE_DEFAULT;	/* ofdm 24m */
}

static void
mt7996_mcu_sta_bfer_ht(struct ieee80211_sta *sta, struct mt7996_phy *phy,
		       struct sta_rec_bf *bf)
{
	struct ieee80211_mcs_info *mcs = &sta->deflink.ht_cap.mcs;
	u8 n = 0;

	bf->tx_mode = MT_PHY_TYPE_HT;

	if ((mcs->tx_params & IEEE80211_HT_MCS_TX_RX_DIFF) &&
	    (mcs->tx_params & IEEE80211_HT_MCS_TX_DEFINED))
		n = FIELD_GET(IEEE80211_HT_MCS_TX_MAX_STREAMS_MASK,
			      mcs->tx_params);
	else if (mcs->rx_mask[3])
		n = 3;
	else if (mcs->rx_mask[2])
		n = 2;
	else if (mcs->rx_mask[1])
		n = 1;

	bf->nrow = hweight8(phy->mt76->antenna_mask) - 1;
	bf->ncol = min_t(u8, bf->nrow, n);
	bf->ibf_ncol = n;
}

static void
mt7996_mcu_sta_bfer_vht(struct ieee80211_sta *sta, struct mt7996_phy *phy,
			struct sta_rec_bf *bf, bool explicit)
{
	struct ieee80211_sta_vht_cap *pc = &sta->deflink.vht_cap;
	struct ieee80211_sta_vht_cap *vc = &phy->mt76->sband_5g.sband.vht_cap;
	u16 mcs_map = le16_to_cpu(pc->vht_mcs.rx_mcs_map);
	u8 nss_mcs = mt7996_mcu_get_sta_nss(mcs_map);
	u8 tx_ant = hweight8(phy->mt76->antenna_mask) - 1;

	bf->tx_mode = MT_PHY_TYPE_VHT;

	if (explicit) {
		u8 sts, snd_dim;

		mt7996_mcu_sta_sounding_rate(bf);

		sts = FIELD_GET(IEEE80211_VHT_CAP_BEAMFORMEE_STS_MASK,
				pc->cap);
		snd_dim = FIELD_GET(IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_MASK,
				    vc->cap);
		bf->nrow = min_t(u8, min_t(u8, snd_dim, sts), tx_ant);
		bf->ncol = min_t(u8, nss_mcs, bf->nrow);
		bf->ibf_ncol = bf->ncol;

		if (sta->deflink.bandwidth == IEEE80211_STA_RX_BW_160)
			bf->nrow = 1;
	} else {
		bf->nrow = tx_ant;
		bf->ncol = min_t(u8, nss_mcs, bf->nrow);
		bf->ibf_ncol = nss_mcs;

		if (sta->deflink.bandwidth == IEEE80211_STA_RX_BW_160)
			bf->ibf_nrow = 1;
	}
}

static void
mt7996_mcu_sta_bfer_he(struct ieee80211_sta *sta, struct ieee80211_vif *vif,
		       struct mt7996_phy *phy, struct sta_rec_bf *bf)
{
	struct ieee80211_sta_he_cap *pc = &sta->deflink.he_cap;
	struct ieee80211_he_cap_elem *pe = &pc->he_cap_elem;
	const struct ieee80211_sta_he_cap *vc =
		mt76_connac_get_he_phy_cap(phy->mt76, vif);
	const struct ieee80211_he_cap_elem *ve = &vc->he_cap_elem;
	u16 mcs_map = le16_to_cpu(pc->he_mcs_nss_supp.rx_mcs_80);
	u8 nss_mcs = mt7996_mcu_get_sta_nss(mcs_map);
	u8 snd_dim, sts;

	bf->tx_mode = MT_PHY_TYPE_HE_SU;

	mt7996_mcu_sta_sounding_rate(bf);

	bf->trigger_su = HE_PHY(CAP6_TRIG_SU_BEAMFORMING_FB,
				pe->phy_cap_info[6]);
	bf->trigger_mu = HE_PHY(CAP6_TRIG_MU_BEAMFORMING_PARTIAL_BW_FB,
				pe->phy_cap_info[6]);
	snd_dim = HE_PHY(CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_MASK,
			 ve->phy_cap_info[5]);
	sts = HE_PHY(CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_MASK,
		     pe->phy_cap_info[4]);
	bf->nrow = min_t(u8, snd_dim, sts);
	bf->ncol = min_t(u8, nss_mcs, bf->nrow);
	bf->ibf_ncol = bf->ncol;

	if (sta->deflink.bandwidth != IEEE80211_STA_RX_BW_160)
		return;

	/* go over for 160MHz and 80p80 */
	if (pe->phy_cap_info[0] &
	    IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G) {
		mcs_map = le16_to_cpu(pc->he_mcs_nss_supp.rx_mcs_160);
		nss_mcs = mt7996_mcu_get_sta_nss(mcs_map);

		bf->ncol_gt_bw80 = nss_mcs;
	}

	if (pe->phy_cap_info[0] &
	    IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_80PLUS80_MHZ_IN_5G) {
		mcs_map = le16_to_cpu(pc->he_mcs_nss_supp.rx_mcs_80p80);
		nss_mcs = mt7996_mcu_get_sta_nss(mcs_map);

		if (bf->ncol_gt_bw80)
			bf->ncol_gt_bw80 = min_t(u8, bf->ncol_gt_bw80, nss_mcs);
		else
			bf->ncol_gt_bw80 = nss_mcs;
	}

	snd_dim = HE_PHY(CAP5_BEAMFORMEE_NUM_SND_DIM_ABOVE_80MHZ_MASK,
			 ve->phy_cap_info[5]);
	sts = HE_PHY(CAP4_BEAMFORMEE_MAX_STS_ABOVE_80MHZ_MASK,
		     pe->phy_cap_info[4]);

	bf->nrow_gt_bw80 = min_t(int, snd_dim, sts);
}

static void
mt7996_mcu_sta_bfer_eht(struct ieee80211_sta *sta, struct ieee80211_vif *vif,
			struct mt7996_phy *phy, struct sta_rec_bf *bf)
{
	struct ieee80211_sta_eht_cap *pc = &sta->deflink.eht_cap;
	struct ieee80211_eht_cap_elem_fixed *pe = &pc->eht_cap_elem;
	struct ieee80211_eht_mcs_nss_supp *eht_nss = &pc->eht_mcs_nss_supp;
	const struct ieee80211_sta_eht_cap *vc =
		mt76_connac_get_eht_phy_cap(phy->mt76, vif);
	const struct ieee80211_eht_cap_elem_fixed *ve = &vc->eht_cap_elem;
	u8 nss_mcs = u8_get_bits(eht_nss->bw._80.rx_tx_mcs9_max_nss,
				 IEEE80211_EHT_MCS_NSS_RX) - 1;
	u8 snd_dim, sts;

	bf->tx_mode = MT_PHY_TYPE_EHT_MU;

	mt7996_mcu_sta_sounding_rate(bf);

	bf->trigger_su = EHT_PHY(CAP3_TRIG_SU_BF_FDBK, pe->phy_cap_info[3]);
	bf->trigger_mu = EHT_PHY(CAP3_TRIG_MU_BF_PART_BW_FDBK, pe->phy_cap_info[3]);
	snd_dim = EHT_PHY(CAP2_SOUNDING_DIM_80MHZ_MASK, ve->phy_cap_info[2]);
	sts = EHT_PHY(CAP0_BEAMFORMEE_SS_80MHZ_MASK, pe->phy_cap_info[0]) +
	      (EHT_PHY(CAP1_BEAMFORMEE_SS_80MHZ_MASK, pe->phy_cap_info[1]) << 1);
	bf->nrow = min_t(u8, snd_dim, sts);
	bf->ncol = min_t(u8, nss_mcs, bf->nrow);
	bf->ibf_ncol = bf->ncol;

	if (sta->deflink.bandwidth < IEEE80211_STA_RX_BW_160)
		return;

	switch (sta->deflink.bandwidth) {
	case IEEE80211_STA_RX_BW_160:
		snd_dim = EHT_PHY(CAP2_SOUNDING_DIM_160MHZ_MASK, ve->phy_cap_info[2]);
		sts = EHT_PHY(CAP1_BEAMFORMEE_SS_160MHZ_MASK, pe->phy_cap_info[1]);
		nss_mcs = u8_get_bits(eht_nss->bw._160.rx_tx_mcs9_max_nss,
				      IEEE80211_EHT_MCS_NSS_RX) - 1;

		bf->nrow_gt_bw80 = min_t(u8, snd_dim, sts);
		bf->ncol_gt_bw80 = nss_mcs;
		break;
	case IEEE80211_STA_RX_BW_320:
		snd_dim = EHT_PHY(CAP2_SOUNDING_DIM_320MHZ_MASK, ve->phy_cap_info[2]) +
			  (EHT_PHY(CAP3_SOUNDING_DIM_320MHZ_MASK,
				   ve->phy_cap_info[3]) << 1);
		sts = EHT_PHY(CAP1_BEAMFORMEE_SS_320MHZ_MASK, pe->phy_cap_info[1]);
		nss_mcs = u8_get_bits(eht_nss->bw._320.rx_tx_mcs9_max_nss,
				      IEEE80211_EHT_MCS_NSS_RX) - 1;

		bf->nrow_gt_bw80 = min_t(u8, snd_dim, sts) << 4;
		bf->ncol_gt_bw80 = nss_mcs << 4;
		break;
	default:
		break;
	}
}

static void
mt7996_mcu_sta_bfer_tlv(struct mt7996_dev *dev, struct sk_buff *skb,
			struct ieee80211_vif *vif, struct ieee80211_sta *sta)
{
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct mt7996_phy *phy = mvif->phy;
	int tx_ant = hweight8(phy->mt76->chainmask) - 1;
	struct sta_rec_bf *bf;
	struct tlv *tlv;
	static const u8 matrix[4][4] = {
		{0, 0, 0, 0},
		{1, 1, 0, 0},	/* 2x1, 2x2, 2x3, 2x4 */
		{2, 4, 4, 0},	/* 3x1, 3x2, 3x3, 3x4 */
		{3, 5, 6, 0}	/* 4x1, 4x2, 4x3, 4x4 */
	};
	bool ebf;

	if (!(sta->deflink.ht_cap.ht_supported || sta->deflink.he_cap.has_he))
		return;

	ebf = mt7996_is_ebf_supported(phy, vif, sta, false);
	if (!ebf && !dev->ibf)
		return;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_BF, sizeof(*bf));
	bf = (struct sta_rec_bf *)tlv;

	/* he/eht: eBF only, in accordance with spec
	 * vht: support eBF and iBF
	 * ht: iBF only, since mac80211 lacks of eBF support
	 */
	if (sta->deflink.eht_cap.has_eht && ebf)
		mt7996_mcu_sta_bfer_eht(sta, vif, phy, bf);
	else if (sta->deflink.he_cap.has_he && ebf)
		mt7996_mcu_sta_bfer_he(sta, vif, phy, bf);
	else if (sta->deflink.vht_cap.vht_supported)
		mt7996_mcu_sta_bfer_vht(sta, phy, bf, ebf);
	else if (sta->deflink.ht_cap.ht_supported)
		mt7996_mcu_sta_bfer_ht(sta, phy, bf);
	else
		return;

	bf->bf_cap = ebf ? ebf : dev->ibf << 1;
	bf->bw = sta->deflink.bandwidth;
	bf->ibf_dbw = sta->deflink.bandwidth;
	bf->ibf_nrow = tx_ant;

	if (!ebf && sta->deflink.bandwidth <= IEEE80211_STA_RX_BW_40 && !bf->ncol)
		bf->ibf_timeout = 0x48;
	else
		bf->ibf_timeout = 0x18;

	if (ebf && bf->nrow != tx_ant)
		bf->mem_20m = matrix[tx_ant][bf->ncol];
	else
		bf->mem_20m = matrix[bf->nrow][bf->ncol];

	switch (sta->deflink.bandwidth) {
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
mt7996_mcu_sta_bfee_tlv(struct mt7996_dev *dev, struct sk_buff *skb,
			struct ieee80211_vif *vif, struct ieee80211_sta *sta)
{
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct mt7996_phy *phy = mvif->phy;
	int tx_ant = hweight8(phy->mt76->antenna_mask) - 1;
	struct sta_rec_bfee *bfee;
	struct tlv *tlv;
	u8 nrow = 0;

	if (!(sta->deflink.vht_cap.vht_supported || sta->deflink.he_cap.has_he))
		return;

	if (!mt7996_is_ebf_supported(phy, vif, sta, true))
		return;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_BFEE, sizeof(*bfee));
	bfee = (struct sta_rec_bfee *)tlv;

	if (sta->deflink.he_cap.has_he) {
		struct ieee80211_he_cap_elem *pe = &sta->deflink.he_cap.he_cap_elem;

		nrow = HE_PHY(CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_MASK,
			      pe->phy_cap_info[5]);
	} else if (sta->deflink.vht_cap.vht_supported) {
		struct ieee80211_sta_vht_cap *pc = &sta->deflink.vht_cap;

		nrow = FIELD_GET(IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_MASK,
				 pc->cap);
	}

	/* reply with identity matrix to avoid 2x2 BF negative gain */
	bfee->fb_identity_matrix = (nrow == 1 && tx_ant == 2);
}

static void
mt7996_mcu_sta_tx_proc_tlv(struct sk_buff *skb)
{
	struct sta_rec_tx_proc *tx_proc;
	struct tlv *tlv;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_TX_PROC, sizeof(*tx_proc));

	tx_proc = (struct sta_rec_tx_proc *)tlv;
	tx_proc->flag = cpu_to_le32(0);
}

static void
mt7996_mcu_sta_hdrt_tlv(struct mt7996_dev *dev, struct sk_buff *skb)
{
	struct sta_rec_hdrt *hdrt;
	struct tlv *tlv;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_HDRT, sizeof(*hdrt));

	hdrt = (struct sta_rec_hdrt *)tlv;
	hdrt->hdrt_mode = 1;
}

static void
mt7996_mcu_sta_hdr_trans_tlv(struct mt7996_dev *dev, struct sk_buff *skb,
			     struct ieee80211_vif *vif,
			     struct ieee80211_sta *sta)
{
	struct sta_rec_hdr_trans *hdr_trans;
	struct mt76_wcid *wcid;
	struct tlv *tlv;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_HDR_TRANS, sizeof(*hdr_trans));
	hdr_trans = (struct sta_rec_hdr_trans *)tlv;
	hdr_trans->dis_rx_hdr_tran = true;

	if (vif->type == NL80211_IFTYPE_STATION)
		hdr_trans->to_ds = true;
	else
		hdr_trans->from_ds = true;

	if (!sta)
		return;

	wcid = (struct mt76_wcid *)sta->drv_priv;
	hdr_trans->dis_rx_hdr_tran = !test_bit(MT_WCID_FLAG_HDR_TRANS, &wcid->flags);
	if (test_bit(MT_WCID_FLAG_4ADDR, &wcid->flags)) {
		hdr_trans->to_ds = true;
		hdr_trans->from_ds = true;
	}

	if (vif->type == NL80211_IFTYPE_MESH_POINT) {
		hdr_trans->to_ds = true;
		hdr_trans->from_ds = true;
		hdr_trans->mesh = true;
	}
}

static enum mcu_mmps_mode
mt7996_mcu_get_mmps_mode(enum ieee80211_smps_mode smps)
{
	switch (smps) {
	case IEEE80211_SMPS_OFF:
		return MCU_MMPS_DISABLE;
	case IEEE80211_SMPS_STATIC:
		return MCU_MMPS_STATIC;
	case IEEE80211_SMPS_DYNAMIC:
		return MCU_MMPS_DYNAMIC;
	default:
		return MCU_MMPS_DISABLE;
	}
}

int mt7996_mcu_set_fixed_rate_ctrl(struct mt7996_dev *dev,
				   void *data, u16 version)
{
	struct ra_fixed_rate *req;
	struct uni_header hdr;
	struct sk_buff *skb;
	struct tlv *tlv;
	int len;

	len = sizeof(hdr) + sizeof(*req);

	skb = mt76_mcu_msg_alloc(&dev->mt76, NULL, len);
	if (!skb)
		return -ENOMEM;

	skb_put_data(skb, &hdr, sizeof(hdr));

	tlv = mt7996_mcu_add_uni_tlv(skb, UNI_RA_FIXED_RATE, sizeof(*req));
	req = (struct ra_fixed_rate *)tlv;
	req->version = cpu_to_le16(version);
	memcpy(&req->rate, data, sizeof(req->rate));

	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_WM_UNI_CMD(RA), true);
}

int mt7996_mcu_set_fixed_field(struct mt7996_dev *dev, struct ieee80211_vif *vif,
			       struct ieee80211_sta *sta, void *data, u32 field)
{
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;
	struct sta_phy_uni *phy = data;
	struct sta_rec_ra_fixed_uni *ra;
	struct sk_buff *skb;
	struct tlv *tlv;

	skb = __mt76_connac_mcu_alloc_sta_req(&dev->mt76, &mvif->mt76,
					      &msta->wcid,
					      MT7996_STA_UPDATE_MAX_SIZE);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_RA_UPDATE, sizeof(*ra));
	ra = (struct sta_rec_ra_fixed_uni *)tlv;

	switch (field) {
	case RATE_PARAM_AUTO:
		break;
	case RATE_PARAM_FIXED:
	case RATE_PARAM_FIXED_MCS:
	case RATE_PARAM_FIXED_GI:
	case RATE_PARAM_FIXED_HE_LTF:
		if (phy)
			ra->phy = *phy;
		break;
	case RATE_PARAM_MMPS_UPDATE:
		ra->mmps_mode = mt7996_mcu_get_mmps_mode(sta->deflink.smps_mode);
		break;
	default:
		break;
	}
	ra->field = cpu_to_le32(field);

	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_WMWA_UNI_CMD(STA_REC_UPDATE), true);
}

static int
mt7996_mcu_add_rate_ctrl_fixed(struct mt7996_dev *dev, struct ieee80211_vif *vif,
			       struct ieee80211_sta *sta)
{
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct cfg80211_chan_def *chandef = &mvif->phy->mt76->chandef;
	struct cfg80211_bitrate_mask *mask = &mvif->bitrate_mask;
	enum nl80211_band band = chandef->chan->band;
	struct sta_phy_uni phy = {};
	int ret, nrates = 0;

#define __sta_phy_bitrate_mask_check(_mcs, _gi, _ht, _he)			\
	do {									\
		u8 i, gi = mask->control[band]._gi;				\
		gi = (_he) ? gi : gi == NL80211_TXRATE_FORCE_SGI;		\
		phy.sgi = gi;							\
		phy.he_ltf = mask->control[band].he_ltf;			\
		for (i = 0; i < ARRAY_SIZE(mask->control[band]._mcs); i++) {	\
			if (!mask->control[band]._mcs[i])			\
				continue;					\
			nrates += hweight16(mask->control[band]._mcs[i]);	\
			phy.mcs = ffs(mask->control[band]._mcs[i]) - 1;		\
			if (_ht)						\
				phy.mcs += 8 * i;				\
		}								\
	} while (0)

	if (sta->deflink.he_cap.has_he) {
		__sta_phy_bitrate_mask_check(he_mcs, he_gi, 0, 1);
	} else if (sta->deflink.vht_cap.vht_supported) {
		__sta_phy_bitrate_mask_check(vht_mcs, gi, 0, 0);
	} else if (sta->deflink.ht_cap.ht_supported) {
		__sta_phy_bitrate_mask_check(ht_mcs, gi, 1, 0);
	} else {
		nrates = hweight32(mask->control[band].legacy);
		phy.mcs = ffs(mask->control[band].legacy) - 1;
	}
#undef __sta_phy_bitrate_mask_check

	/* fall back to auto rate control */
	if (mask->control[band].gi == NL80211_TXRATE_DEFAULT_GI &&
	    mask->control[band].he_gi == GENMASK(7, 0) &&
	    mask->control[band].he_ltf == GENMASK(7, 0) &&
	    nrates != 1)
		return 0;

	/* fixed single rate */
	if (nrates == 1) {
		ret = mt7996_mcu_set_fixed_field(dev, vif, sta, &phy,
						 RATE_PARAM_FIXED_MCS);
		if (ret)
			return ret;
	}

	/* fixed GI */
	if (mask->control[band].gi != NL80211_TXRATE_DEFAULT_GI ||
	    mask->control[band].he_gi != GENMASK(7, 0)) {
		struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;
		u32 addr;

		/* firmware updates only TXCMD but doesn't take WTBL into
		 * account, so driver should update here to reflect the
		 * actual txrate hardware sends out.
		 */
		addr = mt7996_mac_wtbl_lmac_addr(dev, msta->wcid.idx, 7);
		if (sta->deflink.he_cap.has_he)
			mt76_rmw_field(dev, addr, GENMASK(31, 24), phy.sgi);
		else
			mt76_rmw_field(dev, addr, GENMASK(15, 12), phy.sgi);

		ret = mt7996_mcu_set_fixed_field(dev, vif, sta, &phy,
						 RATE_PARAM_FIXED_GI);
		if (ret)
			return ret;
	}

	/* fixed HE_LTF */
	if (mask->control[band].he_ltf != GENMASK(7, 0)) {
		ret = mt7996_mcu_set_fixed_field(dev, vif, sta, &phy,
						 RATE_PARAM_FIXED_HE_LTF);
		if (ret)
			return ret;
	}

	return 0;
}

static void
mt7996_mcu_sta_rate_ctrl_tlv(struct sk_buff *skb, struct mt7996_dev *dev,
			     struct ieee80211_vif *vif, struct ieee80211_sta *sta)
{
#define INIT_RCPI 180
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct mt76_phy *mphy = mvif->phy->mt76;
	struct cfg80211_chan_def *chandef = &mphy->chandef;
	struct cfg80211_bitrate_mask *mask = &mvif->bitrate_mask;
	enum nl80211_band band = chandef->chan->band;
	struct sta_rec_ra_uni *ra;
	struct tlv *tlv;
	u32 supp_rate = sta->deflink.supp_rates[band];
	u32 cap = sta->wme ? STA_CAP_WMM : 0;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_RA, sizeof(*ra));
	ra = (struct sta_rec_ra_uni *)tlv;

	ra->valid = true;
	ra->auto_rate = true;
	ra->phy_mode = mt76_connac_get_phy_mode(mphy, vif, band, sta);
	ra->channel = chandef->chan->hw_value;
	ra->bw = (sta->deflink.bandwidth == IEEE80211_STA_RX_BW_320) ?
		 CMD_CBW_320MHZ : sta->deflink.bandwidth;
	ra->phy.bw = ra->bw;
	ra->mmps_mode = mt7996_mcu_get_mmps_mode(sta->deflink.smps_mode);

	if (supp_rate) {
		supp_rate &= mask->control[band].legacy;
		ra->rate_len = hweight32(supp_rate);

		if (band == NL80211_BAND_2GHZ) {
			ra->supp_mode = MODE_CCK;
			ra->supp_cck_rate = supp_rate & GENMASK(3, 0);

			if (ra->rate_len > 4) {
				ra->supp_mode |= MODE_OFDM;
				ra->supp_ofdm_rate = supp_rate >> 4;
			}
		} else {
			ra->supp_mode = MODE_OFDM;
			ra->supp_ofdm_rate = supp_rate;
		}
	}

	if (sta->deflink.ht_cap.ht_supported) {
		ra->supp_mode |= MODE_HT;
		ra->af = sta->deflink.ht_cap.ampdu_factor;
		ra->ht_gf = !!(sta->deflink.ht_cap.cap & IEEE80211_HT_CAP_GRN_FLD);

		cap |= STA_CAP_HT;
		if (sta->deflink.ht_cap.cap & IEEE80211_HT_CAP_SGI_20)
			cap |= STA_CAP_SGI_20;
		if (sta->deflink.ht_cap.cap & IEEE80211_HT_CAP_SGI_40)
			cap |= STA_CAP_SGI_40;
		if (sta->deflink.ht_cap.cap & IEEE80211_HT_CAP_TX_STBC)
			cap |= STA_CAP_TX_STBC;
		if (sta->deflink.ht_cap.cap & IEEE80211_HT_CAP_RX_STBC)
			cap |= STA_CAP_RX_STBC;
		if (vif->bss_conf.ht_ldpc &&
		    (sta->deflink.ht_cap.cap & IEEE80211_HT_CAP_LDPC_CODING))
			cap |= STA_CAP_LDPC;

		mt7996_mcu_set_sta_ht_mcs(sta, ra->ht_mcs,
					  mask->control[band].ht_mcs);
		ra->supp_ht_mcs = *(__le32 *)ra->ht_mcs;
	}

	if (sta->deflink.vht_cap.vht_supported) {
		u8 af;

		ra->supp_mode |= MODE_VHT;
		af = FIELD_GET(IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK,
			       sta->deflink.vht_cap.cap);
		ra->af = max_t(u8, ra->af, af);

		cap |= STA_CAP_VHT;
		if (sta->deflink.vht_cap.cap & IEEE80211_VHT_CAP_SHORT_GI_80)
			cap |= STA_CAP_VHT_SGI_80;
		if (sta->deflink.vht_cap.cap & IEEE80211_VHT_CAP_SHORT_GI_160)
			cap |= STA_CAP_VHT_SGI_160;
		if (sta->deflink.vht_cap.cap & IEEE80211_VHT_CAP_TXSTBC)
			cap |= STA_CAP_VHT_TX_STBC;
		if (sta->deflink.vht_cap.cap & IEEE80211_VHT_CAP_RXSTBC_1)
			cap |= STA_CAP_VHT_RX_STBC;
		if (vif->bss_conf.vht_ldpc &&
		    (sta->deflink.vht_cap.cap & IEEE80211_VHT_CAP_RXLDPC))
			cap |= STA_CAP_VHT_LDPC;

		mt7996_mcu_set_sta_vht_mcs(sta, ra->supp_vht_mcs,
					   mask->control[band].vht_mcs);
	}

	if (sta->deflink.he_cap.has_he) {
		ra->supp_mode |= MODE_HE;
		cap |= STA_CAP_HE;

		if (sta->deflink.he_6ghz_capa.capa)
			ra->af = le16_get_bits(sta->deflink.he_6ghz_capa.capa,
					       IEEE80211_HE_6GHZ_CAP_MAX_AMPDU_LEN_EXP);
	}
	ra->sta_cap = cpu_to_le32(cap);

	memset(ra->rx_rcpi, INIT_RCPI, sizeof(ra->rx_rcpi));
}

int mt7996_mcu_add_rate_ctrl(struct mt7996_dev *dev, struct ieee80211_vif *vif,
			     struct ieee80211_sta *sta, bool changed)
{
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;
	struct sk_buff *skb;
	int ret;

	skb = __mt76_connac_mcu_alloc_sta_req(&dev->mt76, &mvif->mt76,
					      &msta->wcid,
					      MT7996_STA_UPDATE_MAX_SIZE);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	/* firmware rc algorithm refers to sta_rec_he for HE control.
	 * once dev->rc_work changes the settings driver should also
	 * update sta_rec_he here.
	 */
	if (changed)
		mt7996_mcu_sta_he_tlv(skb, sta);

	/* sta_rec_ra accommodates BW, NSS and only MCS range format
	 * i.e 0-{7,8,9} for VHT.
	 */
	mt7996_mcu_sta_rate_ctrl_tlv(skb, dev, vif, sta);

	ret = mt76_mcu_skb_send_msg(&dev->mt76, skb,
				    MCU_WMWA_UNI_CMD(STA_REC_UPDATE), true);
	if (ret)
		return ret;

	return mt7996_mcu_add_rate_ctrl_fixed(dev, vif, sta);
}

static int
mt7996_mcu_add_group(struct mt7996_dev *dev, struct ieee80211_vif *vif,
		     struct ieee80211_sta *sta)
{
#define MT_STA_BSS_GROUP		1
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct mt7996_sta *msta;
	struct {
		u8 __rsv1[4];

		__le16 tag;
		__le16 len;
		__le16 wlan_idx;
		u8 __rsv2[2];
		__le32 action;
		__le32 val;
		u8 __rsv3[8];
	} __packed req = {
		.tag = cpu_to_le16(UNI_VOW_DRR_CTRL),
		.len = cpu_to_le16(sizeof(req) - 4),
		.action = cpu_to_le32(MT_STA_BSS_GROUP),
		.val = cpu_to_le32(mvif->mt76.idx % 16),
	};

	msta = sta ? (struct mt7996_sta *)sta->drv_priv : &mvif->sta;
	req.wlan_idx = cpu_to_le16(msta->wcid.idx);

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(VOW), &req,
				 sizeof(req), true);
}

int mt7996_mcu_add_sta(struct mt7996_dev *dev, struct ieee80211_vif *vif,
		       struct ieee80211_sta *sta, bool enable, bool newly)
{
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct mt7996_sta *msta;
	struct sk_buff *skb;
	int ret;

	msta = sta ? (struct mt7996_sta *)sta->drv_priv : &mvif->sta;

	skb = __mt76_connac_mcu_alloc_sta_req(&dev->mt76, &mvif->mt76,
					      &msta->wcid,
					      MT7996_STA_UPDATE_MAX_SIZE);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	/* starec basic */
	mt76_connac_mcu_sta_basic_tlv(&dev->mt76, skb, vif, sta, enable, newly);

	if (!enable)
		goto out;

	/* starec hdr trans */
	mt7996_mcu_sta_hdr_trans_tlv(dev, skb, vif, sta);
	/* starec tx proc */
	mt7996_mcu_sta_tx_proc_tlv(skb);

	/* tag order is in accordance with firmware dependency. */
	if (sta) {
		/* starec hdrt mode */
		mt7996_mcu_sta_hdrt_tlv(dev, skb);
		/* starec bfer */
		mt7996_mcu_sta_bfer_tlv(dev, skb, vif, sta);
		/* starec ht */
		mt7996_mcu_sta_ht_tlv(skb, sta);
		/* starec vht */
		mt7996_mcu_sta_vht_tlv(skb, sta);
		/* starec uapsd */
		mt76_connac_mcu_sta_uapsd(skb, vif, sta);
		/* starec amsdu */
		mt7996_mcu_sta_amsdu_tlv(dev, skb, vif, sta);
		/* starec he */
		mt7996_mcu_sta_he_tlv(skb, sta);
		/* starec he 6g*/
		mt7996_mcu_sta_he_6g_tlv(skb, sta);
		/* starec eht */
		mt7996_mcu_sta_eht_tlv(skb, sta);
		/* starec muru */
		mt7996_mcu_sta_muru_tlv(dev, skb, vif, sta);
		/* starec bfee */
		mt7996_mcu_sta_bfee_tlv(dev, skb, vif, sta);
	}

	ret = mt7996_mcu_add_group(dev, vif, sta);
	if (ret) {
		dev_kfree_skb(skb);
		return ret;
	}
out:
	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_WMWA_UNI_CMD(STA_REC_UPDATE), true);
}

static int
mt7996_mcu_sta_key_tlv(struct mt76_wcid *wcid,
		       struct sk_buff *skb,
		       struct ieee80211_key_conf *key,
		       enum set_key_cmd cmd)
{
	struct sta_rec_sec_uni *sec;
	struct tlv *tlv;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_KEY_V2, sizeof(*sec));
	sec = (struct sta_rec_sec_uni *)tlv;
	sec->add = cmd;

	if (cmd == SET_KEY) {
		struct sec_key_uni *sec_key;
		u8 cipher;

		cipher = mt76_connac_mcu_get_cipher(key->cipher);
		if (cipher == MCU_CIPHER_NONE)
			return -EOPNOTSUPP;

		sec_key = &sec->key[0];
		sec_key->wlan_idx = cpu_to_le16(wcid->idx);
		sec_key->mgmt_prot = 0;
		sec_key->cipher_id = cipher;
		sec_key->cipher_len = sizeof(*sec_key);
		sec_key->key_id = key->keyidx;
		sec_key->key_len = key->keylen;
		sec_key->need_resp = 0;
		memcpy(sec_key->key, key->key, key->keylen);

		if (cipher == MCU_CIPHER_TKIP) {
			/* Rx/Tx MIC keys are swapped */
			memcpy(sec_key->key + 16, key->key + 24, 8);
			memcpy(sec_key->key + 24, key->key + 16, 8);
		}

		sec->n_cipher = 1;
	} else {
		sec->n_cipher = 0;
	}

	return 0;
}

int mt7996_mcu_add_key(struct mt76_dev *dev, struct ieee80211_vif *vif,
		       struct ieee80211_key_conf *key, int mcu_cmd,
		       struct mt76_wcid *wcid, enum set_key_cmd cmd)
{
	struct mt76_vif *mvif = (struct mt76_vif *)vif->drv_priv;
	struct sk_buff *skb;
	int ret;

	skb = __mt76_connac_mcu_alloc_sta_req(dev, mvif, wcid,
					      MT7996_STA_UPDATE_MAX_SIZE);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	ret = mt7996_mcu_sta_key_tlv(wcid, skb, key, cmd);
	if (ret)
		return ret;

	return mt76_mcu_skb_send_msg(dev, skb, mcu_cmd, true);
}

static int mt7996_mcu_get_pn(struct mt7996_dev *dev, struct ieee80211_vif *vif,
			     u8 *pn)
{
#define TSC_TYPE_BIGTK_PN 2
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct sta_rec_pn_info *pn_info;
	struct sk_buff *skb, *rskb;
	struct tlv *tlv;
	int ret;

	skb = mt76_connac_mcu_alloc_sta_req(&dev->mt76, &mvif->mt76, &mvif->sta.wcid);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_PN_INFO, sizeof(*pn_info));
	pn_info = (struct sta_rec_pn_info *)tlv;

	pn_info->tsc_type = TSC_TYPE_BIGTK_PN;
	ret = mt76_mcu_skb_send_and_get_msg(&dev->mt76, skb,
					    MCU_WM_UNI_CMD_QUERY(STA_REC_UPDATE),
					    true, &rskb);
	if (ret)
		return ret;

	skb_pull(rskb, 4);

	pn_info = (struct sta_rec_pn_info *)rskb->data;
	if (le16_to_cpu(pn_info->tag) == STA_REC_PN_INFO)
		memcpy(pn, pn_info->pn, 6);

	dev_kfree_skb(rskb);
	return 0;
}

int mt7996_mcu_bcn_prot_enable(struct mt7996_dev *dev, struct ieee80211_vif *vif,
			       struct ieee80211_key_conf *key)
{
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct mt7996_mcu_bcn_prot_tlv *bcn_prot;
	struct sk_buff *skb;
	struct tlv *tlv;
	u8 pn[6] = {};
	int len = sizeof(struct bss_req_hdr) +
		  sizeof(struct mt7996_mcu_bcn_prot_tlv);
	int ret;

	skb = __mt7996_mcu_alloc_bss_req(&dev->mt76, &mvif->mt76, len);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	tlv = mt76_connac_mcu_add_tlv(skb, UNI_BSS_INFO_BCN_PROT, sizeof(*bcn_prot));

	bcn_prot = (struct mt7996_mcu_bcn_prot_tlv *)tlv;

	ret = mt7996_mcu_get_pn(dev, vif, pn);
	if (ret) {
		dev_kfree_skb(skb);
		return ret;
	}

	switch (key->cipher) {
	case WLAN_CIPHER_SUITE_AES_CMAC:
		bcn_prot->cipher_id = MCU_CIPHER_BCN_PROT_CMAC_128;
		break;
	case WLAN_CIPHER_SUITE_BIP_GMAC_128:
		bcn_prot->cipher_id = MCU_CIPHER_BCN_PROT_GMAC_128;
		break;
	case WLAN_CIPHER_SUITE_BIP_GMAC_256:
		bcn_prot->cipher_id = MCU_CIPHER_BCN_PROT_GMAC_256;
		break;
	case WLAN_CIPHER_SUITE_BIP_CMAC_256:
	default:
		dev_err(dev->mt76.dev, "Not supported Bigtk Cipher\n");
		dev_kfree_skb(skb);
		return -EOPNOTSUPP;
	}

	pn[0]++;
	memcpy(bcn_prot->pn, pn, 6);
	bcn_prot->enable = BP_SW_MODE;
	memcpy(bcn_prot->key, key->key, WLAN_MAX_KEY_LEN);
	bcn_prot->key_id = key->keyidx;

	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_WMWA_UNI_CMD(BSS_INFO_UPDATE), true);
}
int mt7996_mcu_add_dev_info(struct mt7996_phy *phy,
			    struct ieee80211_vif *vif, bool enable)
{
	struct mt7996_dev *dev = phy->dev;
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct {
		struct req_hdr {
			u8 omac_idx;
			u8 band_idx;
			u8 __rsv[2];
		} __packed hdr;
		struct req_tlv {
			__le16 tag;
			__le16 len;
			u8 active;
			u8 __rsv;
			u8 omac_addr[ETH_ALEN];
		} __packed tlv;
	} data = {
		.hdr = {
			.omac_idx = mvif->mt76.omac_idx,
			.band_idx = mvif->mt76.band_idx,
		},
		.tlv = {
			.tag = cpu_to_le16(DEV_INFO_ACTIVE),
			.len = cpu_to_le16(sizeof(struct req_tlv)),
			.active = enable,
		},
	};

	if (mvif->mt76.omac_idx >= REPEATER_BSSID_START)
		return mt7996_mcu_muar_config(phy, vif, false, enable);

	memcpy(data.tlv.omac_addr, vif->addr, ETH_ALEN);
	return mt76_mcu_send_msg(&dev->mt76, MCU_WMWA_UNI_CMD(DEV_INFO_UPDATE),
				 &data, sizeof(data), true);
}

static void
mt7996_mcu_beacon_cntdwn(struct ieee80211_vif *vif, struct sk_buff *rskb,
			 struct sk_buff *skb,
			 struct ieee80211_mutable_offsets *offs)
{
	struct bss_bcn_cntdwn_tlv *info;
	struct tlv *tlv;
	u16 tag;

	if (!offs->cntdwn_counter_offs[0])
		return;

	tag = vif->bss_conf.csa_active ? UNI_BSS_INFO_BCN_CSA : UNI_BSS_INFO_BCN_BCC;

	tlv = mt7996_mcu_add_uni_tlv(rskb, tag, sizeof(*info));

	info = (struct bss_bcn_cntdwn_tlv *)tlv;
	info->cnt = skb->data[offs->cntdwn_counter_offs[0]];
}

static void
mt7996_mcu_beacon_mbss(struct sk_buff *rskb, struct sk_buff *skb,
		       struct ieee80211_vif *vif, struct bss_bcn_content_tlv *bcn,
		       struct ieee80211_mutable_offsets *offs)
{
	struct bss_bcn_mbss_tlv *mbss;
	const struct element *elem;
	struct tlv *tlv;

	if (!vif->bss_conf.bssid_indicator)
		return;

	tlv = mt7996_mcu_add_uni_tlv(rskb, UNI_BSS_INFO_BCN_MBSSID, sizeof(*mbss));

	mbss = (struct bss_bcn_mbss_tlv *)tlv;
	mbss->offset[0] = cpu_to_le16(offs->tim_offset);
	mbss->bitmap = cpu_to_le32(1);

	for_each_element_id(elem, WLAN_EID_MULTIPLE_BSSID,
			    &skb->data[offs->mbssid_off],
			    skb->len - offs->mbssid_off) {
		const struct element *sub_elem;

		if (elem->datalen < 2)
			continue;

		for_each_element(sub_elem, elem->data + 1, elem->datalen - 1) {
			const struct ieee80211_bssid_index *idx;
			const u8 *idx_ie;

			/* not a valid BSS profile */
			if (sub_elem->id || sub_elem->datalen < 4)
				continue;

			/* Find WLAN_EID_MULTI_BSSID_IDX
			 * in the merged nontransmitted profile
			 */
			idx_ie = cfg80211_find_ie(WLAN_EID_MULTI_BSSID_IDX,
						  sub_elem->data, sub_elem->datalen);
			if (!idx_ie || idx_ie[1] < sizeof(*idx))
				continue;

			idx = (void *)(idx_ie + 2);
			if (!idx->bssid_index || idx->bssid_index > 31)
				continue;

			mbss->offset[idx->bssid_index] = cpu_to_le16(idx_ie -
								     skb->data);
			mbss->bitmap |= cpu_to_le32(BIT(idx->bssid_index));
		}
	}
}

static void
mt7996_mcu_beacon_cont(struct mt7996_dev *dev, struct ieee80211_vif *vif,
		       struct sk_buff *rskb, struct sk_buff *skb,
		       struct bss_bcn_content_tlv *bcn,
		       struct ieee80211_mutable_offsets *offs)
{
	struct mt76_wcid *wcid = &dev->mt76.global_wcid;
	u8 *buf;

	bcn->pkt_len = cpu_to_le16(MT_TXD_SIZE + skb->len);
	bcn->tim_ie_pos = cpu_to_le16(offs->tim_offset);

	if (offs->cntdwn_counter_offs[0]) {
		u16 offset = offs->cntdwn_counter_offs[0];

		if (vif->bss_conf.csa_active)
			bcn->csa_ie_pos = cpu_to_le16(offset - 4);
		if (vif->bss_conf.color_change_active)
			bcn->bcc_ie_pos = cpu_to_le16(offset - 3);
	}

	buf = (u8 *)bcn + sizeof(*bcn);
	mt7996_mac_write_txwi(dev, (__le32 *)buf, skb, wcid, NULL, 0, 0,
			      BSS_CHANGED_BEACON);

	memcpy(buf + MT_TXD_SIZE, skb->data, skb->len);
}

int mt7996_mcu_add_beacon(struct ieee80211_hw *hw,
			  struct ieee80211_vif *vif, int en)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt7996_phy *phy = mt7996_hw_phy(hw);
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct ieee80211_mutable_offsets offs;
	struct ieee80211_tx_info *info;
	struct sk_buff *skb, *rskb;
	struct tlv *tlv;
	struct bss_bcn_content_tlv *bcn;
	int len;

	if (vif->bss_conf.nontransmitted)
		return 0;

	rskb = __mt7996_mcu_alloc_bss_req(&dev->mt76, &mvif->mt76,
					  MT7996_MAX_BSS_OFFLOAD_SIZE);
	if (IS_ERR(rskb))
		return PTR_ERR(rskb);

	skb = ieee80211_beacon_get_template(hw, vif, &offs, 0);
	if (!skb) {
		dev_kfree_skb(rskb);
		return -EINVAL;
	}

	if (skb->len > MT7996_MAX_BEACON_SIZE) {
		dev_err(dev->mt76.dev, "Bcn size limit exceed\n");
		dev_kfree_skb(rskb);
		dev_kfree_skb(skb);
		return -EINVAL;
	}

	info = IEEE80211_SKB_CB(skb);
	info->hw_queue |= FIELD_PREP(MT_TX_HW_QUEUE_PHY, phy->mt76->band_idx);

	len = ALIGN(sizeof(*bcn) + MT_TXD_SIZE + skb->len, 4);
	tlv = mt7996_mcu_add_uni_tlv(rskb, UNI_BSS_INFO_BCN_CONTENT, len);
	bcn = (struct bss_bcn_content_tlv *)tlv;
	bcn->enable = en;
	if (!en)
		goto out;

	mt7996_mcu_beacon_cont(dev, vif, rskb, skb, bcn, &offs);
	mt7996_mcu_beacon_mbss(rskb, skb, vif, bcn, &offs);
	mt7996_mcu_beacon_cntdwn(vif, rskb, skb, &offs);
out:
	dev_kfree_skb(skb);
	return mt76_mcu_skb_send_msg(&phy->dev->mt76, rskb,
				     MCU_WMWA_UNI_CMD(BSS_INFO_UPDATE), true);
}

int mt7996_mcu_beacon_inband_discov(struct mt7996_dev *dev,
				    struct ieee80211_vif *vif, u32 changed)
{
#define OFFLOAD_TX_MODE_SU	BIT(0)
#define OFFLOAD_TX_MODE_MU	BIT(1)
	struct ieee80211_hw *hw = mt76_hw(dev);
	struct mt7996_phy *phy = mt7996_hw_phy(hw);
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct cfg80211_chan_def *chandef = &mvif->phy->mt76->chandef;
	enum nl80211_band band = chandef->chan->band;
	struct mt76_wcid *wcid = &dev->mt76.global_wcid;
	struct bss_inband_discovery_tlv *discov;
	struct ieee80211_tx_info *info;
	struct sk_buff *rskb, *skb = NULL;
	struct tlv *tlv;
	u8 *buf, interval;
	int len;

	if (vif->bss_conf.nontransmitted)
		return 0;

	rskb = __mt7996_mcu_alloc_bss_req(&dev->mt76, &mvif->mt76,
					  MT7996_MAX_BSS_OFFLOAD_SIZE);
	if (IS_ERR(rskb))
		return PTR_ERR(rskb);

	if (changed & BSS_CHANGED_FILS_DISCOVERY &&
	    vif->bss_conf.fils_discovery.max_interval) {
		interval = vif->bss_conf.fils_discovery.max_interval;
		skb = ieee80211_get_fils_discovery_tmpl(hw, vif);
	} else if (changed & BSS_CHANGED_UNSOL_BCAST_PROBE_RESP &&
		   vif->bss_conf.unsol_bcast_probe_resp_interval) {
		interval = vif->bss_conf.unsol_bcast_probe_resp_interval;
		skb = ieee80211_get_unsol_bcast_probe_resp_tmpl(hw, vif);
	}

	if (!skb) {
		dev_kfree_skb(rskb);
		return -EINVAL;
	}

	if (skb->len > MT7996_MAX_BEACON_SIZE) {
		dev_err(dev->mt76.dev, "inband discovery size limit exceed\n");
		dev_kfree_skb(rskb);
		dev_kfree_skb(skb);
		return -EINVAL;
	}

	info = IEEE80211_SKB_CB(skb);
	info->control.vif = vif;
	info->band = band;
	info->hw_queue |= FIELD_PREP(MT_TX_HW_QUEUE_PHY, phy->mt76->band_idx);

	len = ALIGN(sizeof(*discov) + MT_TXD_SIZE + skb->len, 4);
	tlv = mt7996_mcu_add_uni_tlv(rskb, UNI_BSS_INFO_OFFLOAD, len);

	discov = (struct bss_inband_discovery_tlv *)tlv;
	discov->tx_mode = OFFLOAD_TX_MODE_SU;
	/* 0: UNSOL PROBE RESP, 1: FILS DISCOV */
	discov->tx_type = !!(changed & BSS_CHANGED_FILS_DISCOVERY);
	discov->tx_interval = interval;
	discov->prob_rsp_len = cpu_to_le16(MT_TXD_SIZE + skb->len);
	discov->enable = true;
	discov->wcid = cpu_to_le16(MT7996_WTBL_RESERVED);

	buf = (u8 *)tlv + sizeof(*discov);

	mt7996_mac_write_txwi(dev, (__le32 *)buf, skb, wcid, NULL, 0, 0, changed);

	memcpy(buf + MT_TXD_SIZE, skb->data, skb->len);

	dev_kfree_skb(skb);

	return mt76_mcu_skb_send_msg(&dev->mt76, rskb,
				     MCU_WMWA_UNI_CMD(BSS_INFO_UPDATE), true);
}

static int mt7996_driver_own(struct mt7996_dev *dev, u8 band)
{
	mt76_wr(dev, MT_TOP_LPCR_HOST_BAND(band), MT_TOP_LPCR_HOST_DRV_OWN);
	if (!mt76_poll_msec(dev, MT_TOP_LPCR_HOST_BAND(band),
			    MT_TOP_LPCR_HOST_FW_OWN_STAT, 0, 500)) {
		dev_err(dev->mt76.dev, "Timeout for driver own\n");
		return -EIO;
	}

	/* clear irq when the driver own success */
	mt76_wr(dev, MT_TOP_LPCR_HOST_BAND_IRQ_STAT(band),
		MT_TOP_LPCR_HOST_BAND_STAT);

	return 0;
}

static u32 mt7996_patch_sec_mode(u32 key_info)
{
	u32 sec = u32_get_bits(key_info, MT7996_PATCH_SEC), key = 0;

	if (key_info == GENMASK(31, 0) || sec == MT7996_SEC_MODE_PLAIN)
		return 0;

	if (sec == MT7996_SEC_MODE_AES)
		key = u32_get_bits(key_info, MT7996_PATCH_AES_KEY);
	else
		key = u32_get_bits(key_info, MT7996_PATCH_SCRAMBLE_KEY);

	return MT7996_SEC_ENCRYPT | MT7996_SEC_IV |
	       u32_encode_bits(key, MT7996_SEC_KEY_IDX);
}

static int mt7996_load_patch(struct mt7996_dev *dev)
{
	const struct mt7996_patch_hdr *hdr;
	const struct firmware *fw = NULL;
	int i, ret, sem;

	sem = mt76_connac_mcu_patch_sem_ctrl(&dev->mt76, 1);
	switch (sem) {
	case PATCH_IS_DL:
		return 0;
	case PATCH_NOT_DL_SEM_SUCCESS:
		break;
	default:
		dev_err(dev->mt76.dev, "Failed to get patch semaphore\n");
		return -EAGAIN;
	}

	ret = request_firmware(&fw, fw_name(dev, ROM_PATCH), dev->mt76.dev);
	if (ret)
		goto out;

	if (!fw || !fw->data || fw->size < sizeof(*hdr)) {
		dev_err(dev->mt76.dev, "Invalid firmware\n");
		ret = -EINVAL;
		goto out;
	}

	hdr = (const struct mt7996_patch_hdr *)(fw->data);

	dev_info(dev->mt76.dev, "HW/SW Version: 0x%x, Build Time: %.16s\n",
		 be32_to_cpu(hdr->hw_sw_ver), hdr->build_date);

	for (i = 0; i < be32_to_cpu(hdr->desc.n_region); i++) {
		struct mt7996_patch_sec *sec;
		const u8 *dl;
		u32 len, addr, sec_key_idx, mode = DL_MODE_NEED_RSP;

		sec = (struct mt7996_patch_sec *)(fw->data + sizeof(*hdr) +
						  i * sizeof(*sec));
		if ((be32_to_cpu(sec->type) & PATCH_SEC_TYPE_MASK) !=
		    PATCH_SEC_TYPE_INFO) {
			ret = -EINVAL;
			goto out;
		}

		addr = be32_to_cpu(sec->info.addr);
		len = be32_to_cpu(sec->info.len);
		sec_key_idx = be32_to_cpu(sec->info.sec_key_idx);
		dl = fw->data + be32_to_cpu(sec->offs);

		mode |= mt7996_patch_sec_mode(sec_key_idx);

		ret = mt76_connac_mcu_init_download(&dev->mt76, addr, len,
						    mode);
		if (ret) {
			dev_err(dev->mt76.dev, "Download request failed\n");
			goto out;
		}

		ret = __mt76_mcu_send_firmware(&dev->mt76, MCU_CMD(FW_SCATTER),
					       dl, len, 4096);
		if (ret) {
			dev_err(dev->mt76.dev, "Failed to send patch\n");
			goto out;
		}
	}

	ret = mt76_connac_mcu_start_patch(&dev->mt76);
	if (ret)
		dev_err(dev->mt76.dev, "Failed to start patch\n");

out:
	sem = mt76_connac_mcu_patch_sem_ctrl(&dev->mt76, 0);
	switch (sem) {
	case PATCH_REL_SEM_SUCCESS:
		break;
	default:
		ret = -EAGAIN;
		dev_err(dev->mt76.dev, "Failed to release patch semaphore\n");
		break;
	}
	release_firmware(fw);

	return ret;
}

static int
mt7996_mcu_send_ram_firmware(struct mt7996_dev *dev,
			     const struct mt7996_fw_trailer *hdr,
			     const u8 *data, enum mt7996_ram_type type)
{
	int i, offset = 0;
	u32 override = 0, option = 0;

	for (i = 0; i < hdr->n_region; i++) {
		const struct mt7996_fw_region *region;
		int err;
		u32 len, addr, mode;

		region = (const struct mt7996_fw_region *)((const u8 *)hdr -
			 (hdr->n_region - i) * sizeof(*region));
		/* DSP and WA use same mode */
		mode = mt76_connac_mcu_gen_dl_mode(&dev->mt76,
						   region->feature_set,
						   type != MT7996_RAM_TYPE_WM);
		len = le32_to_cpu(region->len);
		addr = le32_to_cpu(region->addr);

		if (region->feature_set & FW_FEATURE_OVERRIDE_ADDR)
			override = addr;

		err = mt76_connac_mcu_init_download(&dev->mt76, addr, len,
						    mode);
		if (err) {
			dev_err(dev->mt76.dev, "Download request failed\n");
			return err;
		}

		err = __mt76_mcu_send_firmware(&dev->mt76, MCU_CMD(FW_SCATTER),
					       data + offset, len, 4096);
		if (err) {
			dev_err(dev->mt76.dev, "Failed to send firmware.\n");
			return err;
		}

		offset += len;
	}

	if (override)
		option |= FW_START_OVERRIDE;

	if (type == MT7996_RAM_TYPE_WA)
		option |= FW_START_WORKING_PDA_CR4;
	else if (type == MT7996_RAM_TYPE_DSP)
		option |= FW_START_WORKING_PDA_DSP;

	return mt76_connac_mcu_start_firmware(&dev->mt76, override, option);
}

static int __mt7996_load_ram(struct mt7996_dev *dev, const char *fw_type,
			     const char *fw_file, enum mt7996_ram_type ram_type)
{
	const struct mt7996_fw_trailer *hdr;
	const struct firmware *fw;
	int ret;

	ret = request_firmware(&fw, fw_file, dev->mt76.dev);
	if (ret)
		return ret;

	if (!fw || !fw->data || fw->size < sizeof(*hdr)) {
		dev_err(dev->mt76.dev, "Invalid firmware\n");
		ret = -EINVAL;
		goto out;
	}

	hdr = (const void *)(fw->data + fw->size - sizeof(*hdr));
	dev_info(dev->mt76.dev, "%s Firmware Version: %.10s, Build Time: %.15s\n",
		 fw_type, hdr->fw_ver, hdr->build_date);

	ret = mt7996_mcu_send_ram_firmware(dev, hdr, fw->data, ram_type);
	if (ret) {
		dev_err(dev->mt76.dev, "Failed to start %s firmware\n", fw_type);
		goto out;
	}

	snprintf(dev->mt76.hw->wiphy->fw_version,
		 sizeof(dev->mt76.hw->wiphy->fw_version),
		 "%.10s-%.15s", hdr->fw_ver, hdr->build_date);

out:
	release_firmware(fw);

	return ret;
}

static int mt7996_load_ram(struct mt7996_dev *dev)
{
	int ret;

	ret = __mt7996_load_ram(dev, "WM", fw_name(dev, FIRMWARE_WM),
				MT7996_RAM_TYPE_WM);
	if (ret)
		return ret;

	ret = __mt7996_load_ram(dev, "DSP", fw_name(dev, FIRMWARE_DSP),
				MT7996_RAM_TYPE_DSP);
	if (ret)
		return ret;

	return __mt7996_load_ram(dev, "WA", fw_name(dev, FIRMWARE_WA),
				 MT7996_RAM_TYPE_WA);
}

static int
mt7996_firmware_state(struct mt7996_dev *dev, bool wa)
{
	u32 state = FIELD_PREP(MT_TOP_MISC_FW_STATE,
			       wa ? FW_STATE_RDY : FW_STATE_FW_DOWNLOAD);

	if (!mt76_poll_msec(dev, MT_TOP_MISC, MT_TOP_MISC_FW_STATE,
			    state, 1000)) {
		dev_err(dev->mt76.dev, "Timeout for initializing firmware\n");
		return -EIO;
	}
	return 0;
}

static int
mt7996_mcu_restart(struct mt76_dev *dev)
{
	struct {
		u8 __rsv1[4];

		__le16 tag;
		__le16 len;
		u8 power_mode;
		u8 __rsv2[3];
	} __packed req = {
		.tag = cpu_to_le16(UNI_POWER_OFF),
		.len = cpu_to_le16(sizeof(req) - 4),
		.power_mode = 1,
	};

	return mt76_mcu_send_msg(dev, MCU_WM_UNI_CMD(POWER_CTRL), &req,
				 sizeof(req), false);
}

static int mt7996_load_firmware(struct mt7996_dev *dev)
{
	int ret;

	/* make sure fw is download state */
	if (mt7996_firmware_state(dev, false)) {
		/* restart firmware once */
		mt7996_mcu_restart(&dev->mt76);
		ret = mt7996_firmware_state(dev, false);
		if (ret) {
			dev_err(dev->mt76.dev,
				"Firmware is not ready for download\n");
			return ret;
		}
	}

	ret = mt7996_load_patch(dev);
	if (ret)
		return ret;

	ret = mt7996_load_ram(dev);
	if (ret)
		return ret;

	ret = mt7996_firmware_state(dev, true);
	if (ret)
		return ret;

	mt76_queue_tx_cleanup(dev, dev->mt76.q_mcu[MT_MCUQ_FWDL], false);

	dev_dbg(dev->mt76.dev, "Firmware init done\n");

	return 0;
}

int mt7996_mcu_fw_log_2_host(struct mt7996_dev *dev, u8 type, u8 ctrl)
{
	struct {
		u8 _rsv[4];

		__le16 tag;
		__le16 len;
		u8 ctrl;
		u8 interval;
		u8 _rsv2[2];
	} __packed data = {
		.tag = cpu_to_le16(UNI_WSYS_CONFIG_FW_LOG_CTRL),
		.len = cpu_to_le16(sizeof(data) - 4),
		.ctrl = ctrl,
	};

	if (type == MCU_FW_LOG_WA)
		return mt76_mcu_send_msg(&dev->mt76, MCU_WA_UNI_CMD(WSYS_CONFIG),
					 &data, sizeof(data), true);

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(WSYS_CONFIG), &data,
				 sizeof(data), true);
}

int mt7996_mcu_fw_dbg_ctrl(struct mt7996_dev *dev, u32 module, u8 level)
{
	struct {
		u8 _rsv[4];

		__le16 tag;
		__le16 len;
		__le32 module_idx;
		u8 level;
		u8 _rsv2[3];
	} data = {
		.tag = cpu_to_le16(UNI_WSYS_CONFIG_FW_DBG_CTRL),
		.len = cpu_to_le16(sizeof(data) - 4),
		.module_idx = cpu_to_le32(module),
		.level = level,
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(WSYS_CONFIG), &data,
				 sizeof(data), false);
}

static int mt7996_mcu_set_mwds(struct mt7996_dev *dev, bool enabled)
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

static void mt7996_add_rx_airtime_tlv(struct sk_buff *skb, u8 band_idx)
{
	struct vow_rx_airtime *req;
	struct tlv *tlv;

	tlv = mt7996_mcu_add_uni_tlv(skb, UNI_VOW_RX_AT_AIRTIME_CLR_EN, sizeof(*req));
	req = (struct vow_rx_airtime *)tlv;
	req->enable = true;
	req->band = band_idx;

	tlv = mt7996_mcu_add_uni_tlv(skb, UNI_VOW_RX_AT_AIRTIME_EN, sizeof(*req));
	req = (struct vow_rx_airtime *)tlv;
	req->enable = true;
	req->band = band_idx;
}

static int
mt7996_mcu_init_rx_airtime(struct mt7996_dev *dev)
{
	struct uni_header hdr = {};
	struct sk_buff *skb;
	int len, num, i;

	num = 2 + 2 * (mt7996_band_valid(dev, MT_BAND1) +
		       mt7996_band_valid(dev, MT_BAND2));
	len = sizeof(hdr) + num * sizeof(struct vow_rx_airtime);
	skb = mt76_mcu_msg_alloc(&dev->mt76, NULL, len);
	if (!skb)
		return -ENOMEM;

	skb_put_data(skb, &hdr, sizeof(hdr));

	for (i = 0; i < __MT_MAX_BAND; i++) {
		if (mt7996_band_valid(dev, i))
			mt7996_add_rx_airtime_tlv(skb, i);
	}

	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_WM_UNI_CMD(VOW), true);
}

int mt7996_mcu_init_firmware(struct mt7996_dev *dev)
{
	int ret;

	/* force firmware operation mode into normal state,
	 * which should be set before firmware download stage.
	 */
	mt76_wr(dev, MT_SWDEF_MODE, MT_SWDEF_NORMAL_MODE);

	ret = mt7996_driver_own(dev, 0);
	if (ret)
		return ret;
	/* set driver own for band1 when two hif exist */
	if (dev->hif2) {
		ret = mt7996_driver_own(dev, 1);
		if (ret)
			return ret;
	}

	ret = mt7996_load_firmware(dev);
	if (ret)
		return ret;

	set_bit(MT76_STATE_MCU_RUNNING, &dev->mphy.state);
	ret = mt7996_mcu_fw_log_2_host(dev, MCU_FW_LOG_WM, 0);
	if (ret)
		return ret;

	ret = mt7996_mcu_fw_log_2_host(dev, MCU_FW_LOG_WA, 0);
	if (ret)
		return ret;

	ret = mt7996_mcu_set_mwds(dev, 1);
	if (ret)
		return ret;

	ret = mt7996_mcu_init_rx_airtime(dev);
	if (ret)
		return ret;

	return mt7996_mcu_wa_cmd(dev, MCU_WA_PARAM_CMD(SET),
				 MCU_WA_PARAM_RED, 0, 0);
}

int mt7996_mcu_init(struct mt7996_dev *dev)
{
	static const struct mt76_mcu_ops mt7996_mcu_ops = {
		.headroom = sizeof(struct mt76_connac2_mcu_txd), /* reuse */
		.mcu_skb_send_msg = mt7996_mcu_send_message,
		.mcu_parse_response = mt7996_mcu_parse_response,
	};

	dev->mt76.mcu_ops = &mt7996_mcu_ops;

	return mt7996_mcu_init_firmware(dev);
}

void mt7996_mcu_exit(struct mt7996_dev *dev)
{
	mt7996_mcu_restart(&dev->mt76);
	if (mt7996_firmware_state(dev, false)) {
		dev_err(dev->mt76.dev, "Failed to exit mcu\n");
		goto out;
	}

	mt76_wr(dev, MT_TOP_LPCR_HOST_BAND(0), MT_TOP_LPCR_HOST_FW_OWN);
	if (dev->hif2)
		mt76_wr(dev, MT_TOP_LPCR_HOST_BAND(1),
			MT_TOP_LPCR_HOST_FW_OWN);
out:
	skb_queue_purge(&dev->mt76.mcu.res_q);
}

int mt7996_mcu_set_hdr_trans(struct mt7996_dev *dev, bool hdr_trans)
{
	struct {
		u8 __rsv[4];
	} __packed hdr;
	struct hdr_trans_blacklist *req_blacklist;
	struct hdr_trans_en *req_en;
	struct sk_buff *skb;
	struct tlv *tlv;
	int len = MT7996_HDR_TRANS_MAX_SIZE + sizeof(hdr);

	skb = mt76_mcu_msg_alloc(&dev->mt76, NULL, len);
	if (!skb)
		return -ENOMEM;

	skb_put_data(skb, &hdr, sizeof(hdr));

	tlv = mt7996_mcu_add_uni_tlv(skb, UNI_HDR_TRANS_EN, sizeof(*req_en));
	req_en = (struct hdr_trans_en *)tlv;
	req_en->enable = hdr_trans;

	tlv = mt7996_mcu_add_uni_tlv(skb, UNI_HDR_TRANS_VLAN,
				     sizeof(struct hdr_trans_vlan));

	if (hdr_trans) {
		tlv = mt7996_mcu_add_uni_tlv(skb, UNI_HDR_TRANS_BLACKLIST,
					     sizeof(*req_blacklist));
		req_blacklist = (struct hdr_trans_blacklist *)tlv;
		req_blacklist->enable = 1;
		req_blacklist->type = cpu_to_le16(ETH_P_PAE);
	}

	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_WM_UNI_CMD(RX_HDR_TRANS), true);
}

int mt7996_mcu_set_tx(struct mt7996_dev *dev, struct ieee80211_vif *vif)
{
#define MCU_EDCA_AC_PARAM	0
#define WMM_AIFS_SET		BIT(0)
#define WMM_CW_MIN_SET		BIT(1)
#define WMM_CW_MAX_SET		BIT(2)
#define WMM_TXOP_SET		BIT(3)
#define WMM_PARAM_SET		(WMM_AIFS_SET | WMM_CW_MIN_SET | \
				 WMM_CW_MAX_SET | WMM_TXOP_SET)
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct {
		u8 bss_idx;
		u8 __rsv[3];
	} __packed hdr = {
		.bss_idx = mvif->mt76.idx,
	};
	struct sk_buff *skb;
	int len = sizeof(hdr) + IEEE80211_NUM_ACS * sizeof(struct edca);
	int ac;

	skb = mt76_mcu_msg_alloc(&dev->mt76, NULL, len);
	if (!skb)
		return -ENOMEM;

	skb_put_data(skb, &hdr, sizeof(hdr));

	for (ac = 0; ac < IEEE80211_NUM_ACS; ac++) {
		struct ieee80211_tx_queue_params *q = &mvif->queue_params[ac];
		struct edca *e;
		struct tlv *tlv;

		tlv = mt7996_mcu_add_uni_tlv(skb, MCU_EDCA_AC_PARAM, sizeof(*e));

		e = (struct edca *)tlv;
		e->set = WMM_PARAM_SET;
		e->queue = ac;
		e->aifs = q->aifs;
		e->txop = cpu_to_le16(q->txop);

		if (q->cw_min)
			e->cw_min = fls(q->cw_min);
		else
			e->cw_min = 5;

		if (q->cw_max)
			e->cw_max = fls(q->cw_max);
		else
			e->cw_max = 10;
	}

	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_WM_UNI_CMD(EDCA_UPDATE), true);
}

int mt7996_mcu_set_fcc5_lpn(struct mt7996_dev *dev, int val)
{
	struct {
		u8 _rsv[4];

		__le16 tag;
		__le16 len;

		__le32 ctrl;
		__le16 min_lpn;
		u8 rsv[2];
	} __packed req = {
		.tag = cpu_to_le16(UNI_RDD_CTRL_SET_TH),
		.len = cpu_to_le16(sizeof(req) - 4),

		.ctrl = cpu_to_le32(0x1),
		.min_lpn = cpu_to_le16(val),
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(RDD_CTRL),
				 &req, sizeof(req), true);
}

int mt7996_mcu_set_pulse_th(struct mt7996_dev *dev,
			    const struct mt7996_dfs_pulse *pulse)
{
	struct {
		u8 _rsv[4];

		__le16 tag;
		__le16 len;

		__le32 ctrl;

		__le32 max_width;		/* us */
		__le32 max_pwr;			/* dbm */
		__le32 min_pwr;			/* dbm */
		__le32 min_stgr_pri;		/* us */
		__le32 max_stgr_pri;		/* us */
		__le32 min_cr_pri;		/* us */
		__le32 max_cr_pri;		/* us */
	} __packed req = {
		.tag = cpu_to_le16(UNI_RDD_CTRL_SET_TH),
		.len = cpu_to_le16(sizeof(req) - 4),

		.ctrl = cpu_to_le32(0x3),

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

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(RDD_CTRL),
				 &req, sizeof(req), true);
}

int mt7996_mcu_set_radar_th(struct mt7996_dev *dev, int index,
			    const struct mt7996_dfs_pattern *pattern)
{
	struct {
		u8 _rsv[4];

		__le16 tag;
		__le16 len;

		__le32 ctrl;
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
		.tag = cpu_to_le16(UNI_RDD_CTRL_SET_TH),
		.len = cpu_to_le16(sizeof(req) - 4),

		.ctrl = cpu_to_le32(0x2),
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

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(RDD_CTRL),
				 &req, sizeof(req), true);
}

static int
mt7996_mcu_background_chain_ctrl(struct mt7996_phy *phy,
				 struct cfg80211_chan_def *chandef,
				 int cmd)
{
	struct mt7996_dev *dev = phy->dev;
	struct mt76_phy *mphy = phy->mt76;
	struct ieee80211_channel *chan = mphy->chandef.chan;
	int freq = mphy->chandef.center_freq1;
	struct mt7996_mcu_background_chain_ctrl req = {
		.tag = cpu_to_le16(0),
		.len = cpu_to_le16(sizeof(req) - 4),
		.monitor_scan_type = 2, /* simple rx */
	};

	if (!chandef && cmd != CH_SWITCH_BACKGROUND_SCAN_STOP)
		return -EINVAL;

	if (!cfg80211_chandef_valid(&mphy->chandef))
		return -EINVAL;

	switch (cmd) {
	case CH_SWITCH_BACKGROUND_SCAN_START: {
		req.chan = chan->hw_value;
		req.central_chan = ieee80211_frequency_to_channel(freq);
		req.bw = mt76_connac_chan_bw(&mphy->chandef);
		req.monitor_chan = chandef->chan->hw_value;
		req.monitor_central_chan =
			ieee80211_frequency_to_channel(chandef->center_freq1);
		req.monitor_bw = mt76_connac_chan_bw(chandef);
		req.band_idx = phy->mt76->band_idx;
		req.scan_mode = 1;
		break;
	}
	case CH_SWITCH_BACKGROUND_SCAN_RUNNING:
		req.monitor_chan = chandef->chan->hw_value;
		req.monitor_central_chan =
			ieee80211_frequency_to_channel(chandef->center_freq1);
		req.band_idx = phy->mt76->band_idx;
		req.scan_mode = 2;
		break;
	case CH_SWITCH_BACKGROUND_SCAN_STOP:
		req.chan = chan->hw_value;
		req.central_chan = ieee80211_frequency_to_channel(freq);
		req.bw = mt76_connac_chan_bw(&mphy->chandef);
		req.tx_stream = hweight8(mphy->antenna_mask);
		req.rx_stream = mphy->antenna_mask;
		break;
	default:
		return -EINVAL;
	}
	req.band = chandef ? chandef->chan->band == NL80211_BAND_5GHZ : 1;

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(OFFCH_SCAN_CTRL),
				 &req, sizeof(req), false);
}

int mt7996_mcu_rdd_background_enable(struct mt7996_phy *phy,
				     struct cfg80211_chan_def *chandef)
{
	struct mt7996_dev *dev = phy->dev;
	int err, region;

	if (!chandef) { /* disable offchain */
		err = mt7996_mcu_rdd_cmd(dev, RDD_STOP, MT_RX_SEL2,
					 0, 0);
		if (err)
			return err;

		return mt7996_mcu_background_chain_ctrl(phy, NULL,
				CH_SWITCH_BACKGROUND_SCAN_STOP);
	}

	err = mt7996_mcu_background_chain_ctrl(phy, chandef,
					       CH_SWITCH_BACKGROUND_SCAN_START);
	if (err)
		return err;

	switch (dev->mt76.region) {
	case NL80211_DFS_ETSI:
		region = 0;
		break;
	case NL80211_DFS_JP:
		region = 2;
		break;
	case NL80211_DFS_FCC:
	default:
		region = 1;
		break;
	}

	return mt7996_mcu_rdd_cmd(dev, RDD_START, MT_RX_SEL2,
				  0, region);
}

int mt7996_mcu_set_chan_info(struct mt7996_phy *phy, u16 tag)
{
	static const u8 ch_band[] = {
		[NL80211_BAND_2GHZ] = 0,
		[NL80211_BAND_5GHZ] = 1,
		[NL80211_BAND_6GHZ] = 2,
	};
	struct mt7996_dev *dev = phy->dev;
	struct cfg80211_chan_def *chandef = &phy->mt76->chandef;
	int freq1 = chandef->center_freq1;
	u8 band_idx = phy->mt76->band_idx;
	struct {
		/* fixed field */
		u8 __rsv[4];

		__le16 tag;
		__le16 len;
		u8 control_ch;
		u8 center_ch;
		u8 bw;
		u8 tx_path_num;
		u8 rx_path;	/* mask or num */
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
		u8 rsv1[53];
	} __packed req = {
		.tag = cpu_to_le16(tag),
		.len = cpu_to_le16(sizeof(req) - 4),
		.control_ch = chandef->chan->hw_value,
		.center_ch = ieee80211_frequency_to_channel(freq1),
		.bw = mt76_connac_chan_bw(chandef),
		.tx_path_num = hweight16(phy->mt76->chainmask),
		.rx_path = mt7996_rx_chainmask(phy) >> dev->chainshift[band_idx],
		.band_idx = band_idx,
		.channel_band = ch_band[chandef->chan->band],
	};

	if (phy->mt76->hw->conf.flags & IEEE80211_CONF_MONITOR)
		req.switch_reason = CH_SWITCH_NORMAL;
	else if (phy->mt76->hw->conf.flags & IEEE80211_CONF_OFFCHANNEL ||
		 phy->mt76->hw->conf.flags & IEEE80211_CONF_IDLE)
		req.switch_reason = CH_SWITCH_SCAN_BYPASS_DPD;
	else if (!cfg80211_reg_can_beacon(phy->mt76->hw->wiphy, chandef,
					  NL80211_IFTYPE_AP))
		req.switch_reason = CH_SWITCH_DFS;
	else
		req.switch_reason = CH_SWITCH_NORMAL;

	if (tag == UNI_CHANNEL_SWITCH)
		req.rx_path = hweight8(req.rx_path);

	if (chandef->width == NL80211_CHAN_WIDTH_80P80) {
		int freq2 = chandef->center_freq2;

		req.center_ch2 = ieee80211_frequency_to_channel(freq2);
	}

	return mt76_mcu_send_msg(&dev->mt76, MCU_WMWA_UNI_CMD(CHANNEL_SWITCH),
				 &req, sizeof(req), true);
}

static int mt7996_mcu_set_eeprom_flash(struct mt7996_dev *dev)
{
#define MAX_PAGE_IDX_MASK	GENMASK(7, 5)
#define PAGE_IDX_MASK		GENMASK(4, 2)
#define PER_PAGE_SIZE		0x400
	struct mt7996_mcu_eeprom req = {
		.tag = cpu_to_le16(UNI_EFUSE_BUFFER_MODE),
		.buffer_mode = EE_MODE_BUFFER
	};
	u16 eeprom_size = MT7996_EEPROM_SIZE;
	u8 total = DIV_ROUND_UP(eeprom_size, PER_PAGE_SIZE);
	u8 *eep = (u8 *)dev->mt76.eeprom.data;
	int eep_len, i;

	for (i = 0; i < total; i++, eep += eep_len) {
		struct sk_buff *skb;
		int ret, msg_len;

		if (i == total - 1 && !!(eeprom_size % PER_PAGE_SIZE))
			eep_len = eeprom_size % PER_PAGE_SIZE;
		else
			eep_len = PER_PAGE_SIZE;

		msg_len = sizeof(req) + eep_len;
		skb = mt76_mcu_msg_alloc(&dev->mt76, NULL, msg_len);
		if (!skb)
			return -ENOMEM;

		req.len = cpu_to_le16(msg_len - 4);
		req.format = FIELD_PREP(MAX_PAGE_IDX_MASK, total - 1) |
			     FIELD_PREP(PAGE_IDX_MASK, i) | EE_FORMAT_WHOLE;
		req.buf_len = cpu_to_le16(eep_len);

		skb_put_data(skb, &req, sizeof(req));
		skb_put_data(skb, eep, eep_len);

		ret = mt76_mcu_skb_send_msg(&dev->mt76, skb,
					    MCU_WM_UNI_CMD(EFUSE_CTRL), true);
		if (ret)
			return ret;
	}

	return 0;
}

int mt7996_mcu_set_eeprom(struct mt7996_dev *dev)
{
	struct mt7996_mcu_eeprom req = {
		.tag = cpu_to_le16(UNI_EFUSE_BUFFER_MODE),
		.len = cpu_to_le16(sizeof(req) - 4),
		.buffer_mode = EE_MODE_EFUSE,
		.format = EE_FORMAT_WHOLE
	};

	if (dev->flash_mode)
		return mt7996_mcu_set_eeprom_flash(dev);

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(EFUSE_CTRL),
				 &req, sizeof(req), true);
}

int mt7996_mcu_get_eeprom(struct mt7996_dev *dev, u32 offset)
{
	struct {
		u8 _rsv[4];

		__le16 tag;
		__le16 len;
		__le32 addr;
		__le32 valid;
		u8 data[16];
	} __packed req = {
		.tag = cpu_to_le16(UNI_EFUSE_ACCESS),
		.len = cpu_to_le16(sizeof(req) - 4),
		.addr = cpu_to_le32(round_down(offset,
				    MT7996_EEPROM_BLOCK_SIZE)),
	};
	struct sk_buff *skb;
	bool valid;
	int ret;

	ret = mt76_mcu_send_and_get_msg(&dev->mt76,
					MCU_WM_UNI_CMD_QUERY(EFUSE_CTRL),
					&req, sizeof(req), true, &skb);
	if (ret)
		return ret;

	valid = le32_to_cpu(*(__le32 *)(skb->data + 16));
	if (valid) {
		u32 addr = le32_to_cpu(*(__le32 *)(skb->data + 12));
		u8 *buf = (u8 *)dev->mt76.eeprom.data + addr;

		skb_pull(skb, 48);
		memcpy(buf, skb->data, MT7996_EEPROM_BLOCK_SIZE);
	}

	dev_kfree_skb(skb);

	return 0;
}

int mt7996_mcu_get_eeprom_free_block(struct mt7996_dev *dev, u8 *block_num)
{
	struct {
		u8 _rsv[4];

		__le16 tag;
		__le16 len;
		u8 num;
		u8 version;
		u8 die_idx;
		u8 _rsv2;
	} __packed req = {
		.tag = cpu_to_le16(UNI_EFUSE_FREE_BLOCK),
		.len = cpu_to_le16(sizeof(req) - 4),
		.version = 2,
	};
	struct sk_buff *skb;
	int ret;

	ret = mt76_mcu_send_and_get_msg(&dev->mt76, MCU_WM_UNI_CMD_QUERY(EFUSE_CTRL), &req,
					sizeof(req), true, &skb);
	if (ret)
		return ret;

	*block_num = *(u8 *)(skb->data + 8);
	dev_kfree_skb(skb);

	return 0;
}

int mt7996_mcu_get_chip_config(struct mt7996_dev *dev, u32 *cap)
{
#define NIC_CAP	3
#define UNI_EVENT_CHIP_CONFIG_EFUSE_VERSION	0x21
	struct {
		u8 _rsv[4];

		__le16 tag;
		__le16 len;
	} __packed req = {
		.tag = cpu_to_le16(NIC_CAP),
		.len = cpu_to_le16(sizeof(req) - 4),
	};
	struct sk_buff *skb;
	u8 *buf;
	int ret;

	ret = mt76_mcu_send_and_get_msg(&dev->mt76,
					MCU_WM_UNI_CMD_QUERY(CHIP_CONFIG), &req,
					sizeof(req), true, &skb);
	if (ret)
		return ret;

	/* fixed field */
	skb_pull(skb, 4);

	buf = skb->data;
	while (buf - skb->data < skb->len) {
		struct tlv *tlv = (struct tlv *)buf;

		switch (le16_to_cpu(tlv->tag)) {
		case UNI_EVENT_CHIP_CONFIG_EFUSE_VERSION:
			*cap = le32_to_cpu(*(__le32 *)(buf + sizeof(*tlv)));
			break;
		default:
			break;
		}

		buf += le16_to_cpu(tlv->len);
	}

	dev_kfree_skb(skb);

	return 0;
}

int mt7996_mcu_get_chan_mib_info(struct mt7996_phy *phy, bool chan_switch)
{
	struct {
		struct {
			u8 band;
			u8 __rsv[3];
		} hdr;
		struct {
			__le16 tag;
			__le16 len;
			__le32 offs;
		} data[4];
	} __packed req = {
		.hdr.band = phy->mt76->band_idx,
	};
	/* strict order */
	static const u32 offs[] = {
		UNI_MIB_TX_TIME,
		UNI_MIB_RX_TIME,
		UNI_MIB_OBSS_AIRTIME,
		UNI_MIB_NON_WIFI_TIME,
	};
	struct mt76_channel_state *state = phy->mt76->chan_state;
	struct mt76_channel_state *state_ts = &phy->state_ts;
	struct mt7996_dev *dev = phy->dev;
	struct mt7996_mcu_mib *res;
	struct sk_buff *skb;
	int i, ret;

	for (i = 0; i < 4; i++) {
		req.data[i].tag = cpu_to_le16(UNI_CMD_MIB_DATA);
		req.data[i].len = cpu_to_le16(sizeof(req.data[i]));
		req.data[i].offs = cpu_to_le32(offs[i]);
	}

	ret = mt76_mcu_send_and_get_msg(&dev->mt76, MCU_WM_UNI_CMD_QUERY(GET_MIB_INFO),
					&req, sizeof(req), true, &skb);
	if (ret)
		return ret;

	skb_pull(skb, sizeof(req.hdr));

	res = (struct mt7996_mcu_mib *)(skb->data);

	if (chan_switch)
		goto out;

#define __res_u64(s) le64_to_cpu(res[s].data)
	state->cc_tx += __res_u64(1) - state_ts->cc_tx;
	state->cc_bss_rx += __res_u64(2) - state_ts->cc_bss_rx;
	state->cc_rx += __res_u64(2) + __res_u64(3) - state_ts->cc_rx;
	state->cc_busy += __res_u64(0) + __res_u64(1) + __res_u64(2) + __res_u64(3) -
			  state_ts->cc_busy;

out:
	state_ts->cc_tx = __res_u64(1);
	state_ts->cc_bss_rx = __res_u64(2);
	state_ts->cc_rx = __res_u64(2) + __res_u64(3);
	state_ts->cc_busy = __res_u64(0) + __res_u64(1) + __res_u64(2) + __res_u64(3);
#undef __res_u64

	dev_kfree_skb(skb);

	return 0;
}

int mt7996_mcu_get_temperature(struct mt7996_phy *phy)
{
#define TEMPERATURE_QUERY 0
#define GET_TEMPERATURE 0
	struct {
		u8 _rsv[4];

		__le16 tag;
		__le16 len;

		u8 rsv1;
		u8 action;
		u8 band_idx;
		u8 rsv2;
	} req = {
		.tag = cpu_to_le16(TEMPERATURE_QUERY),
		.len = cpu_to_le16(sizeof(req) - 4),
		.action = GET_TEMPERATURE,
		.band_idx = phy->mt76->band_idx,
	};
	struct mt7996_mcu_thermal {
		u8 _rsv[4];

		__le16 tag;
		__le16 len;

		__le32 rsv;
		__le32 temperature;
	} __packed * res;
	struct sk_buff *skb;
	int ret;
	u32 temp;

	ret = mt76_mcu_send_and_get_msg(&phy->dev->mt76, MCU_WM_UNI_CMD(THERMAL),
					&req, sizeof(req), true, &skb);
	if (ret)
		return ret;

	res = (void *)skb->data;
	temp = le32_to_cpu(res->temperature);
	dev_kfree_skb(skb);

	return temp;
}

int mt7996_mcu_set_thermal_throttling(struct mt7996_phy *phy, u8 state)
{
	struct {
		u8 _rsv[4];

		__le16 tag;
		__le16 len;

		struct mt7996_mcu_thermal_ctrl ctrl;
	} __packed req = {
		.tag = cpu_to_le16(UNI_CMD_THERMAL_PROTECT_DUTY_CONFIG),
		.len = cpu_to_le16(sizeof(req) - 4),
		.ctrl = {
			.band_idx = phy->mt76->band_idx,
		},
	};
	int level, ret;

	/* set duty cycle and level */
	for (level = 0; level < 4; level++) {
		req.ctrl.duty.duty_level = level;
		req.ctrl.duty.duty_cycle = state;
		state /= 2;

		ret = mt76_mcu_send_msg(&phy->dev->mt76, MCU_WM_UNI_CMD(THERMAL),
					&req, sizeof(req), false);
		if (ret)
			return ret;
	}

	return 0;
}

int mt7996_mcu_set_thermal_protect(struct mt7996_phy *phy, bool enable)
{
#define SUSTAIN_PERIOD		10
	struct {
		u8 _rsv[4];

		__le16 tag;
		__le16 len;

		struct mt7996_mcu_thermal_ctrl ctrl;
		struct mt7996_mcu_thermal_enable enable;
	} __packed req = {
		.len = cpu_to_le16(sizeof(req) - 4 - sizeof(req.enable)),
		.ctrl = {
			.band_idx = phy->mt76->band_idx,
			.type.protect_type = 1,
			.type.trigger_type = 1,
		},
	};
	int ret;

	req.tag = cpu_to_le16(UNI_CMD_THERMAL_PROTECT_DISABLE);

	ret = mt76_mcu_send_msg(&phy->dev->mt76, MCU_WM_UNI_CMD(THERMAL),
				&req, sizeof(req) - sizeof(req.enable), false);
	if (ret || !enable)
		return ret;

	/* set high-temperature trigger threshold */
	req.tag = cpu_to_le16(UNI_CMD_THERMAL_PROTECT_ENABLE);
	req.enable.restore_temp = cpu_to_le32(phy->throttle_temp[0]);
	req.enable.trigger_temp = cpu_to_le32(phy->throttle_temp[1]);
	req.enable.sustain_time = cpu_to_le16(SUSTAIN_PERIOD);

	req.len = cpu_to_le16(sizeof(req) - 4);

	return mt76_mcu_send_msg(&phy->dev->mt76, MCU_WM_UNI_CMD(THERMAL),
				 &req, sizeof(req), false);
}

int mt7996_mcu_set_ser(struct mt7996_dev *dev, u8 action, u8 val, u8 band)
{
	struct {
		u8 rsv[4];

		__le16 tag;
		__le16 len;

		union {
			struct {
				__le32 mask;
			} __packed set;

			struct {
				u8 method;
				u8 band;
				u8 rsv2[2];
			} __packed trigger;
		};
	} __packed req = {
		.tag = cpu_to_le16(action),
		.len = cpu_to_le16(sizeof(req) - 4),
	};

	switch (action) {
	case UNI_CMD_SER_SET:
		req.set.mask = cpu_to_le32(val);
		break;
	case UNI_CMD_SER_TRIGGER:
		req.trigger.method = val;
		req.trigger.band = band;
		break;
	default:
		return -EINVAL;
	}

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(SER),
				 &req, sizeof(req), false);
}

int mt7996_mcu_set_txbf(struct mt7996_dev *dev, u8 action)
{
#define MT7996_BF_MAX_SIZE	sizeof(union bf_tag_tlv)
#define BF_PROCESSING	4
	struct uni_header hdr;
	struct sk_buff *skb;
	struct tlv *tlv;
	int len = sizeof(hdr) + MT7996_BF_MAX_SIZE;

	memset(&hdr, 0, sizeof(hdr));

	skb = mt76_mcu_msg_alloc(&dev->mt76, NULL, len);
	if (!skb)
		return -ENOMEM;

	skb_put_data(skb, &hdr, sizeof(hdr));

	switch (action) {
	case BF_SOUNDING_ON: {
		struct bf_sounding_on *req_snd_on;

		tlv = mt7996_mcu_add_uni_tlv(skb, action, sizeof(*req_snd_on));
		req_snd_on = (struct bf_sounding_on *)tlv;
		req_snd_on->snd_mode = BF_PROCESSING;
		break;
	}
	case BF_HW_EN_UPDATE: {
		struct bf_hw_en_status_update *req_hw_en;

		tlv = mt7996_mcu_add_uni_tlv(skb, action, sizeof(*req_hw_en));
		req_hw_en = (struct bf_hw_en_status_update *)tlv;
		req_hw_en->ebf = true;
		req_hw_en->ibf = dev->ibf;
		break;
	}
	case BF_MOD_EN_CTRL: {
		struct bf_mod_en_ctrl *req_mod_en;

		tlv = mt7996_mcu_add_uni_tlv(skb, action, sizeof(*req_mod_en));
		req_mod_en = (struct bf_mod_en_ctrl *)tlv;
		req_mod_en->bf_num = 3;
		req_mod_en->bf_bitmap = GENMASK(2, 0);
		break;
	}
	default:
		return -EINVAL;
	}

	return mt76_mcu_skb_send_msg(&dev->mt76, skb, MCU_WM_UNI_CMD(BF), true);
}

static int
mt7996_mcu_enable_obss_spr(struct mt7996_phy *phy, u16 action, u8 val)
{
	struct mt7996_dev *dev = phy->dev;
	struct {
		u8 band_idx;
		u8 __rsv[3];

		__le16 tag;
		__le16 len;

		__le32 val;
	} __packed req = {
		.band_idx = phy->mt76->band_idx,
		.tag = cpu_to_le16(action),
		.len = cpu_to_le16(sizeof(req) - 4),
		.val = cpu_to_le32(val),
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(SR),
				 &req, sizeof(req), true);
}

static int
mt7996_mcu_set_obss_spr_pd(struct mt7996_phy *phy,
			   struct ieee80211_he_obss_pd *he_obss_pd)
{
	struct mt7996_dev *dev = phy->dev;
	u8 max_th = 82, non_srg_max_th = 62;
	struct {
		u8 band_idx;
		u8 __rsv[3];

		__le16 tag;
		__le16 len;

		u8 pd_th_non_srg;
		u8 pd_th_srg;
		u8 period_offs;
		u8 rcpi_src;
		__le16 obss_pd_min;
		__le16 obss_pd_min_srg;
		u8 resp_txpwr_mode;
		u8 txpwr_restrict_mode;
		u8 txpwr_ref;
		u8 __rsv2[3];
	} __packed req = {
		.band_idx = phy->mt76->band_idx,
		.tag = cpu_to_le16(UNI_CMD_SR_SET_PARAM),
		.len = cpu_to_le16(sizeof(req) - 4),
		.obss_pd_min = cpu_to_le16(max_th),
		.obss_pd_min_srg = cpu_to_le16(max_th),
		.txpwr_restrict_mode = 2,
		.txpwr_ref = 21
	};
	int ret;

	/* disable firmware dynamical PD asjustment */
	ret = mt7996_mcu_enable_obss_spr(phy, UNI_CMD_SR_ENABLE_DPD, false);
	if (ret)
		return ret;

	if (he_obss_pd->sr_ctrl &
	    IEEE80211_HE_SPR_NON_SRG_OBSS_PD_SR_DISALLOWED)
		req.pd_th_non_srg = max_th;
	else if (he_obss_pd->sr_ctrl & IEEE80211_HE_SPR_NON_SRG_OFFSET_PRESENT)
		req.pd_th_non_srg  = max_th - he_obss_pd->non_srg_max_offset;
	else
		req.pd_th_non_srg  = non_srg_max_th;

	if (he_obss_pd->sr_ctrl & IEEE80211_HE_SPR_SRG_INFORMATION_PRESENT)
		req.pd_th_srg = max_th - he_obss_pd->max_offset;

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(SR),
				 &req, sizeof(req), true);
}

static int
mt7996_mcu_set_obss_spr_siga(struct mt7996_phy *phy, struct ieee80211_vif *vif,
			     struct ieee80211_he_obss_pd *he_obss_pd)
{
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct mt7996_dev *dev = phy->dev;
	u8 omac = mvif->mt76.omac_idx;
	struct {
		u8 band_idx;
		u8 __rsv[3];

		__le16 tag;
		__le16 len;

		u8 omac;
		u8 __rsv2[3];
		u8 flag[20];
	} __packed req = {
		.band_idx = phy->mt76->band_idx,
		.tag = cpu_to_le16(UNI_CMD_SR_SET_SIGA),
		.len = cpu_to_le16(sizeof(req) - 4),
		.omac = omac > HW_BSSID_MAX ? omac - 12 : omac,
	};
	int ret;

	if (he_obss_pd->sr_ctrl & IEEE80211_HE_SPR_HESIGA_SR_VAL15_ALLOWED)
		req.flag[req.omac] = 0xf;
	else
		return 0;

	/* switch to normal AP mode */
	ret = mt7996_mcu_enable_obss_spr(phy, UNI_CMD_SR_ENABLE_MODE, 0);
	if (ret)
		return ret;

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(SR),
				 &req, sizeof(req), true);
}

static int
mt7996_mcu_set_obss_spr_bitmap(struct mt7996_phy *phy,
			       struct ieee80211_he_obss_pd *he_obss_pd)
{
	struct mt7996_dev *dev = phy->dev;
	struct {
		u8 band_idx;
		u8 __rsv[3];

		__le16 tag;
		__le16 len;

		__le32 color_l[2];
		__le32 color_h[2];
		__le32 bssid_l[2];
		__le32 bssid_h[2];
	} __packed req = {
		.band_idx = phy->mt76->band_idx,
		.tag = cpu_to_le16(UNI_CMD_SR_SET_SRG_BITMAP),
		.len = cpu_to_le16(sizeof(req) - 4),
	};
	u32 bitmap;

	memcpy(&bitmap, he_obss_pd->bss_color_bitmap, sizeof(bitmap));
	req.color_l[req.band_idx] = cpu_to_le32(bitmap);

	memcpy(&bitmap, he_obss_pd->bss_color_bitmap + 4, sizeof(bitmap));
	req.color_h[req.band_idx] = cpu_to_le32(bitmap);

	memcpy(&bitmap, he_obss_pd->partial_bssid_bitmap, sizeof(bitmap));
	req.bssid_l[req.band_idx] = cpu_to_le32(bitmap);

	memcpy(&bitmap, he_obss_pd->partial_bssid_bitmap + 4, sizeof(bitmap));
	req.bssid_h[req.band_idx] = cpu_to_le32(bitmap);

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(SR), &req,
				 sizeof(req), true);
}

int mt7996_mcu_add_obss_spr(struct mt7996_phy *phy, struct ieee80211_vif *vif,
			    struct ieee80211_he_obss_pd *he_obss_pd)
{
	int ret;

	/* enable firmware scene detection algorithms */
	ret = mt7996_mcu_enable_obss_spr(phy, UNI_CMD_SR_ENABLE_SD,
					 sr_scene_detect);
	if (ret)
		return ret;

	/* firmware dynamically adjusts PD threshold so skip manual control */
	if (sr_scene_detect && !he_obss_pd->enable)
		return 0;

	/* enable spatial reuse */
	ret = mt7996_mcu_enable_obss_spr(phy, UNI_CMD_SR_ENABLE,
					 he_obss_pd->enable);
	if (ret)
		return ret;

	if (sr_scene_detect || !he_obss_pd->enable)
		return 0;

	ret = mt7996_mcu_enable_obss_spr(phy, UNI_CMD_SR_ENABLE_TX, true);
	if (ret)
		return ret;

	/* set SRG/non-SRG OBSS PD threshold */
	ret = mt7996_mcu_set_obss_spr_pd(phy, he_obss_pd);
	if (ret)
		return ret;

	/* Set SR prohibit */
	ret = mt7996_mcu_set_obss_spr_siga(phy, vif, he_obss_pd);
	if (ret)
		return ret;

	/* set SRG BSS color/BSSID bitmap */
	return mt7996_mcu_set_obss_spr_bitmap(phy, he_obss_pd);
}

int mt7996_mcu_update_bss_color(struct mt7996_dev *dev, struct ieee80211_vif *vif,
				struct cfg80211_he_bss_color *he_bss_color)
{
	int len = sizeof(struct bss_req_hdr) + sizeof(struct bss_color_tlv);
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct bss_color_tlv *bss_color;
	struct sk_buff *skb;
	struct tlv *tlv;

	skb = __mt7996_mcu_alloc_bss_req(&dev->mt76, &mvif->mt76, len);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	tlv = mt76_connac_mcu_add_tlv(skb, UNI_BSS_INFO_BSS_COLOR,
				      sizeof(*bss_color));
	bss_color = (struct bss_color_tlv *)tlv;
	bss_color->enable = he_bss_color->enabled;
	bss_color->color = he_bss_color->color;

	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_WMWA_UNI_CMD(BSS_INFO_UPDATE), true);
}

#define TWT_AGRT_TRIGGER	BIT(0)
#define TWT_AGRT_ANNOUNCE	BIT(1)
#define TWT_AGRT_PROTECT	BIT(2)

int mt7996_mcu_twt_agrt_update(struct mt7996_dev *dev,
			       struct mt7996_vif *mvif,
			       struct mt7996_twt_flow *flow,
			       int cmd)
{
	struct {
		/* fixed field */
		u8 bss;
		u8 _rsv[3];

		__le16 tag;
		__le16 len;
		u8 tbl_idx;
		u8 cmd;
		u8 own_mac_idx;
		u8 flowid; /* 0xff for group id */
		__le16 peer_id; /* specify the peer_id (msb=0)
				 * or group_id (msb=1)
				 */
		u8 duration; /* 256 us */
		u8 bss_idx;
		__le64 start_tsf;
		__le16 mantissa;
		u8 exponent;
		u8 is_ap;
		u8 agrt_params;
		u8 __rsv2[23];
	} __packed req = {
		.tag = cpu_to_le16(UNI_CMD_TWT_ARGT_UPDATE),
		.len = cpu_to_le16(sizeof(req) - 4),
		.tbl_idx = flow->table_id,
		.cmd = cmd,
		.own_mac_idx = mvif->mt76.omac_idx,
		.flowid = flow->id,
		.peer_id = cpu_to_le16(flow->wcid),
		.duration = flow->duration,
		.bss = mvif->mt76.idx,
		.bss_idx = mvif->mt76.idx,
		.start_tsf = cpu_to_le64(flow->tsf),
		.mantissa = flow->mantissa,
		.exponent = flow->exp,
		.is_ap = true,
	};

	if (flow->protection)
		req.agrt_params |= TWT_AGRT_PROTECT;
	if (!flow->flowtype)
		req.agrt_params |= TWT_AGRT_ANNOUNCE;
	if (flow->trigger)
		req.agrt_params |= TWT_AGRT_TRIGGER;

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(TWT),
				 &req, sizeof(req), true);
}

int mt7996_mcu_set_rts_thresh(struct mt7996_phy *phy, u32 val)
{
	struct {
		u8 band_idx;
		u8 _rsv[3];

		__le16 tag;
		__le16 len;
		__le32 len_thresh;
		__le32 pkt_thresh;
	} __packed req = {
		.band_idx = phy->mt76->band_idx,
		.tag = cpu_to_le16(UNI_BAND_CONFIG_RTS_THRESHOLD),
		.len = cpu_to_le16(sizeof(req) - 4),
		.len_thresh = cpu_to_le32(val),
		.pkt_thresh = cpu_to_le32(0x2),
	};

	return mt76_mcu_send_msg(&phy->dev->mt76, MCU_WM_UNI_CMD(BAND_CONFIG),
				 &req, sizeof(req), true);
}

int mt7996_mcu_set_radio_en(struct mt7996_phy *phy, bool enable)
{
	struct {
		u8 band_idx;
		u8 _rsv[3];

		__le16 tag;
		__le16 len;
		u8 enable;
		u8 _rsv2[3];
	} __packed req = {
		.band_idx = phy->mt76->band_idx,
		.tag = cpu_to_le16(UNI_BAND_CONFIG_RADIO_ENABLE),
		.len = cpu_to_le16(sizeof(req) - 4),
		.enable = enable,
	};

	return mt76_mcu_send_msg(&phy->dev->mt76, MCU_WM_UNI_CMD(BAND_CONFIG),
				 &req, sizeof(req), true);
}

int mt7996_mcu_rdd_cmd(struct mt7996_dev *dev, int cmd, u8 index,
		       u8 rx_sel, u8 val)
{
	struct {
		u8 _rsv[4];

		__le16 tag;
		__le16 len;

		u8 ctrl;
		u8 rdd_idx;
		u8 rdd_rx_sel;
		u8 val;
		u8 rsv[4];
	} __packed req = {
		.tag = cpu_to_le16(UNI_RDD_CTRL_PARM),
		.len = cpu_to_le16(sizeof(req) - 4),
		.ctrl = cmd,
		.rdd_idx = index,
		.rdd_rx_sel = rx_sel,
		.val = val,
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(RDD_CTRL),
				 &req, sizeof(req), true);
}

int mt7996_mcu_wtbl_update_hdr_trans(struct mt7996_dev *dev,
				     struct ieee80211_vif *vif,
				     struct ieee80211_sta *sta)
{
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct mt7996_sta *msta;
	struct sk_buff *skb;

	msta = sta ? (struct mt7996_sta *)sta->drv_priv : &mvif->sta;

	skb = __mt76_connac_mcu_alloc_sta_req(&dev->mt76, &mvif->mt76,
					      &msta->wcid,
					      MT7996_STA_UPDATE_MAX_SIZE);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	/* starec hdr trans */
	mt7996_mcu_sta_hdr_trans_tlv(dev, skb, vif, sta);
	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_WMWA_UNI_CMD(STA_REC_UPDATE), true);
}

int mt7996_mcu_set_fixed_rate_table(struct mt7996_phy *phy, u8 table_idx,
				    u16 rate_idx, bool beacon)
{
#define UNI_FIXED_RATE_TABLE_SET	0
#define SPE_IXD_SELECT_TXD		0
#define SPE_IXD_SELECT_BMC_WTBL		1
	struct mt7996_dev *dev = phy->dev;
	struct fixed_rate_table_ctrl req = {
		.tag = cpu_to_le16(UNI_FIXED_RATE_TABLE_SET),
		.len = cpu_to_le16(sizeof(req) - 4),
		.table_idx = table_idx,
		.rate_idx = cpu_to_le16(rate_idx),
		.gi = 1,
		.he_ltf = 1,
	};
	u8 band_idx = phy->mt76->band_idx;

	if (beacon) {
		req.spe_idx_sel = SPE_IXD_SELECT_TXD;
		req.spe_idx = 24 + band_idx;
		phy->beacon_rate = rate_idx;
	} else {
		req.spe_idx_sel = SPE_IXD_SELECT_BMC_WTBL;
	}

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(FIXED_RATE_TABLE),
				 &req, sizeof(req), false);
}

int mt7996_mcu_rf_regval(struct mt7996_dev *dev, u32 regidx, u32 *val, bool set)
{
	struct {
		u8 __rsv1[4];

		__le16 tag;
		__le16 len;
		__le16 idx;
		u8 __rsv2[2];
		__le32 ofs;
		__le32 data;
	} __packed *res, req = {
		.tag = cpu_to_le16(UNI_CMD_ACCESS_RF_REG_BASIC),
		.len = cpu_to_le16(sizeof(req) - 4),

		.idx = cpu_to_le16(u32_get_bits(regidx, GENMASK(31, 24))),
		.ofs = cpu_to_le32(u32_get_bits(regidx, GENMASK(23, 0))),
		.data = set ? cpu_to_le32(*val) : 0,
	};
	struct sk_buff *skb;
	int ret;

	if (set)
		return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(REG_ACCESS),
					 &req, sizeof(req), true);

	ret = mt76_mcu_send_and_get_msg(&dev->mt76,
					MCU_WM_UNI_CMD_QUERY(REG_ACCESS),
					&req, sizeof(req), true, &skb);
	if (ret)
		return ret;

	res = (void *)skb->data;
	*val = le32_to_cpu(res->data);
	dev_kfree_skb(skb);

	return 0;
}

int mt7996_mcu_trigger_assert(struct mt7996_dev *dev)
{
	struct {
		__le16 tag;
		__le16 len;
		u8 enable;
		u8 rsv[3];
	} __packed req = {
		.len = cpu_to_le16(sizeof(req) - 4),
		.enable = true,
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(ASSERT_DUMP),
				 &req, sizeof(req), false);
}

int mt7996_mcu_set_rro(struct mt7996_dev *dev, u16 tag, u16 val)
{
	struct {
		u8 __rsv1[4];
		__le16 tag;
		__le16 len;
		union {
			struct {
				u8 type;
				u8 __rsv2[3];
			} __packed platform_type;
			struct {
				u8 type;
				u8 dest;
				u8 __rsv2[2];
			} __packed bypass_mode;
			struct {
				u8 path;
				u8 __rsv2[3];
			} __packed txfree_path;
			struct {
				__le16 flush_one;
				__le16 flush_all;
				u8 __rsv2[4];
			} __packed timeout;
		};
	} __packed req = {
		.tag = cpu_to_le16(tag),
		.len = cpu_to_le16(sizeof(req) - 4),
	};

	switch (tag) {
	case UNI_RRO_SET_PLATFORM_TYPE:
		req.platform_type.type = val;
		break;
	case UNI_RRO_SET_BYPASS_MODE:
		req.bypass_mode.type = val;
		break;
	case UNI_RRO_SET_TXFREE_PATH:
		req.txfree_path.path = val;
		break;
	case UNI_RRO_SET_FLUSH_TIMEOUT:
		req.timeout.flush_one = cpu_to_le16(val);
		req.timeout.flush_all = cpu_to_le16(2 * val);
		break;
	default:
		return -EINVAL;
	}

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(RRO), &req,
				 sizeof(req), true);
}

int mt7996_mcu_get_all_sta_info(struct mt7996_phy *phy, u16 tag)
{
	struct mt7996_dev *dev = phy->dev;
	struct {
		u8 _rsv[4];

		__le16 tag;
		__le16 len;
	} __packed req = {
		.tag = cpu_to_le16(tag),
		.len = cpu_to_le16(sizeof(req) - 4),
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(ALL_STA_INFO),
				 &req, sizeof(req), false);
}

int mt7996_mcu_wed_rro_reset_sessions(struct mt7996_dev *dev, u16 id)
{
	struct {
		u8 __rsv[4];

		__le16 tag;
		__le16 len;
		__le16 session_id;
		u8 pad[4];
	} __packed req = {
		.tag = cpu_to_le16(UNI_RRO_DEL_BA_SESSION),
		.len = cpu_to_le16(sizeof(req) - 4),
		.session_id = cpu_to_le16(id),
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(RRO), &req,
				 sizeof(req), true);
}

int mt7996_mcu_set_txpower_sku(struct mt7996_phy *phy)
{
#define TX_POWER_LIMIT_TABLE_RATE	0
	struct mt7996_dev *dev = phy->dev;
	struct mt76_phy *mphy = phy->mt76;
	struct ieee80211_hw *hw = mphy->hw;
	struct tx_power_limit_table_ctrl {
		u8 __rsv1[4];

		__le16 tag;
		__le16 len;
		u8 power_ctrl_id;
		u8 power_limit_type;
		u8 band_idx;
	} __packed req = {
		.tag = cpu_to_le16(UNI_TXPOWER_POWER_LIMIT_TABLE_CTRL),
		.len = cpu_to_le16(sizeof(req) + MT7996_SKU_PATH_NUM - 4),
		.power_ctrl_id = UNI_TXPOWER_POWER_LIMIT_TABLE_CTRL,
		.power_limit_type = TX_POWER_LIMIT_TABLE_RATE,
		.band_idx = phy->mt76->band_idx,
	};
	struct mt76_power_limits la = {};
	struct sk_buff *skb;
	int i, tx_power;

	tx_power = mt7996_get_power_bound(phy, hw->conf.power_level);
	tx_power = mt76_get_rate_power_limits(mphy, mphy->chandef.chan,
					      &la, tx_power);
	mphy->txpower_cur = tx_power;

	skb = mt76_mcu_msg_alloc(&dev->mt76, NULL,
				 sizeof(req) + MT7996_SKU_PATH_NUM);
	if (!skb)
		return -ENOMEM;

	skb_put_data(skb, &req, sizeof(req));
	/* cck and ofdm */
	skb_put_data(skb, &la.cck, sizeof(la.cck));
	skb_put_data(skb, &la.ofdm, sizeof(la.ofdm));
	/* ht20 */
	skb_put_data(skb, &la.mcs[0], 8);
	/* ht40 */
	skb_put_data(skb, &la.mcs[1], 9);

	/* vht */
	for (i = 0; i < 4; i++) {
		skb_put_data(skb, &la.mcs[i], sizeof(la.mcs[i]));
		skb_put_zero(skb, 2);  /* padding */
	}

	/* he */
	skb_put_data(skb, &la.ru[0], sizeof(la.ru));
	/* eht */
	skb_put_data(skb, &la.eht[0], sizeof(la.eht));

	/* padding */
	skb_put_zero(skb, MT7996_SKU_PATH_NUM - MT7996_SKU_RATE_NUM);

	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_WM_UNI_CMD(TXPOWER), true);
}

int mt7996_mcu_cp_support(struct mt7996_dev *dev, u8 mode)
{
	__le32 cp_mode;

	if (mode < mt76_connac_lmac_mapping(IEEE80211_AC_BE) ||
	    mode > mt76_connac_lmac_mapping(IEEE80211_AC_VO))
		return -EINVAL;

	cp_mode = cpu_to_le32(mode);
	return mt76_mcu_send_msg(&dev->mt76, MCU_WA_EXT_CMD(CP_SUPPORT),
				 &cp_mode, sizeof(cp_mode), true);
}
