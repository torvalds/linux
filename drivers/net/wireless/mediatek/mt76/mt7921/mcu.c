// SPDX-License-Identifier: ISC
/* Copyright (C) 2020 MediaTek Inc. */

#include <linux/firmware.h>
#include <linux/fs.h>
#include "mt7921.h"
#include "mt7921_trace.h"
#include "mcu.h"
#include "mac.h"

struct mt7921_patch_hdr {
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

struct mt7921_patch_sec {
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

struct mt7921_fw_trailer {
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

struct mt7921_fw_region {
	__le32 decomp_crc;
	__le32 decomp_len;
	__le32 decomp_blk_sz;
	u8 reserved[4];
	__le32 addr;
	__le32 len;
	u8 feature_set;
	u8 reserved1[15];
} __packed;

#define MT_STA_BFER			BIT(0)
#define MT_STA_BFEE			BIT(1)

#define FW_FEATURE_SET_ENCRYPT		BIT(0)
#define FW_FEATURE_SET_KEY_IDX		GENMASK(2, 1)
#define FW_FEATURE_ENCRY_MODE		BIT(4)
#define FW_FEATURE_OVERRIDE_ADDR	BIT(5)

#define DL_MODE_ENCRYPT			BIT(0)
#define DL_MODE_KEY_IDX			GENMASK(2, 1)
#define DL_MODE_RESET_SEC_IV		BIT(3)
#define DL_MODE_WORKING_PDA_CR4		BIT(4)
#define DL_CONFIG_ENCRY_MODE_SEL	BIT(6)
#define DL_MODE_NEED_RSP		BIT(31)

#define FW_START_OVERRIDE		BIT(0)
#define FW_START_WORKING_PDA_CR4	BIT(2)

#define PATCH_SEC_TYPE_MASK		GENMASK(15, 0)
#define PATCH_SEC_TYPE_INFO		0x2

#define to_wcid_lo(id)			FIELD_GET(GENMASK(7, 0), (u16)id)
#define to_wcid_hi(id)			FIELD_GET(GENMASK(9, 8), (u16)id)

static enum mt7921_cipher_type
mt7921_mcu_get_cipher(int cipher)
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

static u8 mt7921_mcu_chan_bw(struct cfg80211_chan_def *chandef)
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

static int
mt7921_mcu_parse_eeprom(struct mt76_dev *dev, struct sk_buff *skb)
{
	struct mt7921_mcu_eeprom_info *res;
	u8 *buf;

	if (!skb)
		return -EINVAL;

	skb_pull(skb, sizeof(struct mt7921_mcu_rxd));

	res = (struct mt7921_mcu_eeprom_info *)skb->data;
	buf = dev->eeprom.data + le32_to_cpu(res->addr);
	memcpy(buf, res->data, 16);

	return 0;
}

static int
mt7921_mcu_parse_response(struct mt76_dev *mdev, int cmd,
			  struct sk_buff *skb, int seq)
{
	struct mt7921_mcu_rxd *rxd;
	int ret = 0;

	if (!skb) {
		dev_err(mdev->dev, "Message %08x (seq %d) timeout\n",
			cmd, seq);
		mt7921_reset(mdev);

		return -ETIMEDOUT;
	}

	rxd = (struct mt7921_mcu_rxd *)skb->data;
	if (seq != rxd->seq)
		return -EAGAIN;

	switch (cmd) {
	case MCU_CMD_PATCH_SEM_CONTROL:
		skb_pull(skb, sizeof(*rxd) - 4);
		ret = *skb->data;
		break;
	case MCU_EXT_CMD_GET_TEMP:
		skb_pull(skb, sizeof(*rxd) + 4);
		ret = le32_to_cpu(*(__le32 *)skb->data);
		break;
	case MCU_EXT_CMD_EFUSE_ACCESS:
		ret = mt7921_mcu_parse_eeprom(mdev, skb);
		break;
	case MCU_UNI_CMD_DEV_INFO_UPDATE:
	case MCU_UNI_CMD_BSS_INFO_UPDATE:
	case MCU_UNI_CMD_STA_REC_UPDATE:
	case MCU_UNI_CMD_HIF_CTRL:
	case MCU_UNI_CMD_OFFLOAD:
	case MCU_UNI_CMD_SUSPEND: {
		struct mt7921_mcu_uni_event *event;

		skb_pull(skb, sizeof(*rxd));
		event = (struct mt7921_mcu_uni_event *)skb->data;
		ret = le32_to_cpu(event->status);
		break;
	}
	case MCU_CMD_REG_READ: {
		struct mt7921_mcu_reg_event *event;

		skb_pull(skb, sizeof(*rxd));
		event = (struct mt7921_mcu_reg_event *)skb->data;
		ret = (int)le32_to_cpu(event->val);
		break;
	}
	default:
		skb_pull(skb, sizeof(struct mt7921_mcu_rxd));
		break;
	}

	return ret;
}

static int
mt7921_mcu_send_message(struct mt76_dev *mdev, struct sk_buff *skb,
			int cmd, int *wait_seq)
{
	struct mt7921_dev *dev = container_of(mdev, struct mt7921_dev, mt76);
	int txd_len, mcu_cmd = cmd & MCU_CMD_MASK;
	enum mt76_mcuq_id txq = MT_MCUQ_WM;
	struct mt7921_uni_txd *uni_txd;
	struct mt7921_mcu_txd *mcu_txd;
	__le32 *txd;
	u32 val;
	u8 seq;

	switch (cmd) {
	case MCU_UNI_CMD_HIF_CTRL:
	case MCU_UNI_CMD_SUSPEND:
	case MCU_UNI_CMD_OFFLOAD:
		mdev->mcu.timeout = HZ / 3;
		break;
	default:
		mdev->mcu.timeout = 3 * HZ;
		break;
	}

	seq = ++dev->mt76.mcu.msg_seq & 0xf;
	if (!seq)
		seq = ++dev->mt76.mcu.msg_seq & 0xf;

	if (cmd == MCU_CMD_FW_SCATTER) {
		txq = MT_MCUQ_FWDL;
		goto exit;
	}

	txd_len = cmd & MCU_UNI_PREFIX ? sizeof(*uni_txd) : sizeof(*mcu_txd);
	txd = (__le32 *)skb_push(skb, txd_len);

	val = FIELD_PREP(MT_TXD0_TX_BYTES, skb->len) |
	      FIELD_PREP(MT_TXD0_PKT_FMT, MT_TX_TYPE_CMD) |
	      FIELD_PREP(MT_TXD0_Q_IDX, MT_TX_MCU_PORT_RX_Q0);
	txd[0] = cpu_to_le32(val);

	val = MT_TXD1_LONG_FORMAT |
	      FIELD_PREP(MT_TXD1_HDR_FORMAT, MT_HDR_FORMAT_CMD);
	txd[1] = cpu_to_le32(val);

	if (cmd & MCU_UNI_PREFIX) {
		uni_txd = (struct mt7921_uni_txd *)txd;
		uni_txd->len = cpu_to_le16(skb->len - sizeof(uni_txd->txd));
		uni_txd->option = MCU_CMD_UNI_EXT_ACK;
		uni_txd->cid = cpu_to_le16(mcu_cmd);
		uni_txd->s2d_index = MCU_S2D_H2N;
		uni_txd->pkt_type = MCU_PKT_ID;
		uni_txd->seq = seq;

		goto exit;
	}

	mcu_txd = (struct mt7921_mcu_txd *)txd;
	mcu_txd->len = cpu_to_le16(skb->len - sizeof(mcu_txd->txd));
	mcu_txd->pq_id = cpu_to_le16(MCU_PQ_ID(MT_TX_PORT_IDX_MCU,
					       MT_TX_MCU_PORT_RX_Q0));
	mcu_txd->pkt_type = MCU_PKT_ID;
	mcu_txd->seq = seq;

	switch (cmd & ~MCU_CMD_MASK) {
	case MCU_FW_PREFIX:
		mcu_txd->set_query = MCU_Q_NA;
		mcu_txd->cid = mcu_cmd;
		break;
	case MCU_CE_PREFIX:
		if (cmd & MCU_QUERY_MASK)
			mcu_txd->set_query = MCU_Q_QUERY;
		else
			mcu_txd->set_query = MCU_Q_SET;
		mcu_txd->cid = mcu_cmd;
		break;
	default:
		mcu_txd->cid = MCU_CMD_EXT_CID;
		if (cmd & MCU_QUERY_PREFIX || cmd == MCU_EXT_CMD_EFUSE_ACCESS)
			mcu_txd->set_query = MCU_Q_QUERY;
		else
			mcu_txd->set_query = MCU_Q_SET;
		mcu_txd->ext_cid = mcu_cmd;
		mcu_txd->ext_cid_ack = 1;
		break;
	}

	mcu_txd->s2d_index = MCU_S2D_H2N;
	WARN_ON(cmd == MCU_EXT_CMD_EFUSE_ACCESS &&
		mcu_txd->set_query != MCU_Q_QUERY);

exit:
	if (wait_seq)
		*wait_seq = seq;

	return mt76_tx_queue_skb_raw(dev, mdev->q_mcu[txq], skb, 0);
}

static void
mt7921_mcu_tx_rate_parse(struct mt76_phy *mphy,
			 struct mt7921_mcu_peer_cap *peer,
			 struct rate_info *rate, u16 r)
{
	struct ieee80211_supported_band *sband;
	u16 flags = 0;
	u8 txmode = FIELD_GET(MT_WTBL_RATE_TX_MODE, r);
	u8 gi = 0;
	u8 bw = 0;

	rate->mcs = FIELD_GET(MT_WTBL_RATE_MCS, r);
	rate->nss = FIELD_GET(MT_WTBL_RATE_NSS, r) + 1;

	switch (peer->bw) {
	case IEEE80211_STA_RX_BW_160:
		gi = peer->g16;
		break;
	case IEEE80211_STA_RX_BW_80:
		gi = peer->g8;
		break;
	case IEEE80211_STA_RX_BW_40:
		gi = peer->g4;
		break;
	default:
		gi = peer->g2;
		break;
	}

	gi = txmode >= MT_PHY_TYPE_HE_SU ?
		FIELD_GET(MT_WTBL_RATE_HE_GI, gi) :
		FIELD_GET(MT_WTBL_RATE_GI, gi);

	switch (txmode) {
	case MT_PHY_TYPE_CCK:
	case MT_PHY_TYPE_OFDM:
		if (mphy->chandef.chan->band == NL80211_BAND_5GHZ)
			sband = &mphy->sband_5g.sband;
		else
			sband = &mphy->sband_2g.sband;

		rate->legacy = sband->bitrates[rate->mcs].bitrate;
		break;
	case MT_PHY_TYPE_HT:
	case MT_PHY_TYPE_HT_GF:
		flags |= RATE_INFO_FLAGS_MCS;

		if (gi)
			flags |= RATE_INFO_FLAGS_SHORT_GI;
		break;
	case MT_PHY_TYPE_VHT:
		flags |= RATE_INFO_FLAGS_VHT_MCS;

		if (gi)
			flags |= RATE_INFO_FLAGS_SHORT_GI;
		break;
	case MT_PHY_TYPE_HE_SU:
	case MT_PHY_TYPE_HE_EXT_SU:
	case MT_PHY_TYPE_HE_TB:
	case MT_PHY_TYPE_HE_MU:
		rate->he_gi = gi;
		rate->he_dcm = FIELD_GET(MT_RA_RATE_DCM_EN, r);

		flags |= RATE_INFO_FLAGS_HE_MCS;
		break;
	default:
		break;
	}
	rate->flags = flags;

	bw = mt7921_mcu_chan_bw(&mphy->chandef) - FIELD_GET(MT_RA_RATE_BW, r);

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

static void
mt7921_mcu_tx_rate_report(struct mt7921_dev *dev, struct sk_buff *skb,
			  u16 wlan_idx)
{
	struct mt7921_mcu_wlan_info_event *wtbl_info =
		(struct mt7921_mcu_wlan_info_event *)(skb->data);
	struct rate_info rate = {};
	u8 curr_idx = wtbl_info->rate_info.rate_idx;
	u16 curr = le16_to_cpu(wtbl_info->rate_info.rate[curr_idx]);
	struct mt7921_mcu_peer_cap peer = wtbl_info->peer_cap;
	struct mt76_phy *mphy = &dev->mphy;
	struct mt7921_sta_stats *stats;
	struct mt7921_sta *msta;
	struct mt76_wcid *wcid;

	if (wlan_idx >= MT76_N_WCIDS)
		return;

	rcu_read_lock();

	wcid = rcu_dereference(dev->mt76.wcid[wlan_idx]);
	if (!wcid)
		goto out;

	msta = container_of(wcid, struct mt7921_sta, wcid);
	stats = &msta->stats;

	/* current rate */
	mt7921_mcu_tx_rate_parse(mphy, &peer, &rate, curr);
	stats->tx_rate = rate;
out:
	rcu_read_unlock();
}

static void
mt7921_mcu_scan_event(struct mt7921_dev *dev, struct sk_buff *skb)
{
	struct mt76_phy *mphy = &dev->mt76.phy;
	struct mt7921_phy *phy = (struct mt7921_phy *)mphy->priv;

	spin_lock_bh(&dev->mt76.lock);
	__skb_queue_tail(&phy->scan_event_list, skb);
	spin_unlock_bh(&dev->mt76.lock);

	ieee80211_queue_delayed_work(mphy->hw, &phy->scan_work,
				     MT7921_HW_SCAN_TIMEOUT);
}

static void
mt7921_mcu_beacon_loss_event(struct mt7921_dev *dev, struct sk_buff *skb)
{
	struct mt76_connac_beacon_loss_event *event;
	struct mt76_phy *mphy;
	u8 band_idx = 0; /* DBDC support */

	skb_pull(skb, sizeof(struct mt7921_mcu_rxd));
	event = (struct mt76_connac_beacon_loss_event *)skb->data;
	if (band_idx && dev->mt76.phy2)
		mphy = dev->mt76.phy2;
	else
		mphy = &dev->mt76.phy;

	ieee80211_iterate_active_interfaces_atomic(mphy->hw,
					IEEE80211_IFACE_ITER_RESUME_ALL,
					mt76_connac_mcu_beacon_loss_iter, event);
}

static void
mt7921_mcu_bss_event(struct mt7921_dev *dev, struct sk_buff *skb)
{
	struct mt76_phy *mphy = &dev->mt76.phy;
	struct mt76_connac_mcu_bss_event *event;

	skb_pull(skb, sizeof(struct mt7921_mcu_rxd));
	event = (struct mt76_connac_mcu_bss_event *)skb->data;
	if (event->is_absent)
		ieee80211_stop_queues(mphy->hw);
	else
		ieee80211_wake_queues(mphy->hw);
}

static void
mt7921_mcu_debug_msg_event(struct mt7921_dev *dev, struct sk_buff *skb)
{
	struct mt7921_debug_msg {
		__le16 id;
		u8 type;
		u8 flag;
		__le32 value;
		__le16 len;
		u8 content[512];
	} __packed * msg;

	skb_pull(skb, sizeof(struct mt7921_mcu_rxd));
	msg = (struct mt7921_debug_msg *)skb->data;

	if (msg->type == 3) { /* fw log */
		u16 len = min_t(u16, le16_to_cpu(msg->len), 512);
		int i;

		for (i = 0 ; i < len; i++) {
			if (!msg->content[i])
				msg->content[i] = ' ';
		}
		wiphy_info(mt76_hw(dev)->wiphy, "%.*s", len, msg->content);
	}
}

static void
mt7921_mcu_low_power_event(struct mt7921_dev *dev, struct sk_buff *skb)
{
	struct mt7921_mcu_lp_event {
		u8 state;
		u8 reserved[3];
	} __packed * event;

	skb_pull(skb, sizeof(struct mt7921_mcu_rxd));
	event = (struct mt7921_mcu_lp_event *)skb->data;

	trace_lp_event(dev, event->state);
}

static void
mt7921_mcu_rx_unsolicited_event(struct mt7921_dev *dev, struct sk_buff *skb)
{
	struct mt7921_mcu_rxd *rxd = (struct mt7921_mcu_rxd *)skb->data;

	switch (rxd->eid) {
	case MCU_EVENT_BSS_BEACON_LOSS:
		mt7921_mcu_beacon_loss_event(dev, skb);
		break;
	case MCU_EVENT_SCHED_SCAN_DONE:
	case MCU_EVENT_SCAN_DONE:
		mt7921_mcu_scan_event(dev, skb);
		return;
	case MCU_EVENT_BSS_ABSENCE:
		mt7921_mcu_bss_event(dev, skb);
		break;
	case MCU_EVENT_DBG_MSG:
		mt7921_mcu_debug_msg_event(dev, skb);
		break;
	case MCU_EVENT_COREDUMP:
		mt76_connac_mcu_coredump_event(&dev->mt76, skb,
					       &dev->coredump);
		return;
	case MCU_EVENT_LP_INFO:
		mt7921_mcu_low_power_event(dev, skb);
		break;
	default:
		break;
	}
	dev_kfree_skb(skb);
}

void mt7921_mcu_rx_event(struct mt7921_dev *dev, struct sk_buff *skb)
{
	struct mt7921_mcu_rxd *rxd = (struct mt7921_mcu_rxd *)skb->data;

	if (rxd->eid == 0x6) {
		mt76_mcu_rx_event(&dev->mt76, skb);
		return;
	}

	if (rxd->ext_eid == MCU_EXT_EVENT_RATE_REPORT ||
	    rxd->eid == MCU_EVENT_BSS_BEACON_LOSS ||
	    rxd->eid == MCU_EVENT_SCHED_SCAN_DONE ||
	    rxd->eid == MCU_EVENT_BSS_ABSENCE ||
	    rxd->eid == MCU_EVENT_SCAN_DONE ||
	    rxd->eid == MCU_EVENT_DBG_MSG ||
	    rxd->eid == MCU_EVENT_COREDUMP ||
	    rxd->eid == MCU_EVENT_LP_INFO ||
	    !rxd->seq)
		mt7921_mcu_rx_unsolicited_event(dev, skb);
	else
		mt76_mcu_rx_event(&dev->mt76, skb);
}

/** starec & wtbl **/
static int
mt7921_mcu_sta_key_tlv(struct mt7921_sta *msta, struct sk_buff *skb,
		       struct ieee80211_key_conf *key, enum set_key_cmd cmd)
{
	struct mt7921_sta_key_conf *bip = &msta->bip;
	struct sta_rec_sec *sec;
	struct tlv *tlv;
	u32 len = sizeof(*sec);

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_KEY_V2, sizeof(*sec));

	sec = (struct sta_rec_sec *)tlv;
	sec->add = cmd;

	if (cmd == SET_KEY) {
		struct sec_key *sec_key;
		u8 cipher;

		cipher = mt7921_mcu_get_cipher(key->cipher);
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

int mt7921_mcu_add_key(struct mt7921_dev *dev, struct ieee80211_vif *vif,
		       struct mt7921_sta *msta, struct ieee80211_key_conf *key,
		       enum set_key_cmd cmd)
{
	struct mt7921_vif *mvif = (struct mt7921_vif *)vif->drv_priv;
	struct sk_buff *skb;
	int ret;

	skb = mt76_connac_mcu_alloc_sta_req(&dev->mt76, &mvif->mt76,
					    &msta->wcid);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	ret = mt7921_mcu_sta_key_tlv(msta, skb, key, cmd);
	if (ret)
		return ret;

	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_UNI_CMD_STA_REC_UPDATE, true);
}

int mt7921_mcu_uni_tx_ba(struct mt7921_dev *dev,
			 struct ieee80211_ampdu_params *params,
			 bool enable)
{
	struct mt7921_sta *msta = (struct mt7921_sta *)params->sta->drv_priv;

	if (enable && !params->amsdu)
		msta->wcid.amsdu = false;

	return mt76_connac_mcu_sta_ba(&dev->mt76, &msta->vif->mt76, params,
				      enable, true);
}

int mt7921_mcu_uni_rx_ba(struct mt7921_dev *dev,
			 struct ieee80211_ampdu_params *params,
			 bool enable)
{
	struct mt7921_sta *msta = (struct mt7921_sta *)params->sta->drv_priv;

	return mt76_connac_mcu_sta_ba(&dev->mt76, &msta->vif->mt76, params,
				      enable, false);
}

static int mt7921_mcu_restart(struct mt76_dev *dev)
{
	struct {
		u8 power_mode;
		u8 rsv[3];
	} req = {
		.power_mode = 1,
	};

	return mt76_mcu_send_msg(dev, MCU_CMD_NIC_POWER_CTRL, &req,
				 sizeof(req), false);
}

static int mt7921_driver_own(struct mt7921_dev *dev)
{
	u32 reg = mt7921_reg_map_l1(dev, MT_TOP_LPCR_HOST_BAND0);

	mt76_wr(dev, reg, MT_TOP_LPCR_HOST_DRV_OWN);
	if (!mt76_poll_msec(dev, reg, MT_TOP_LPCR_HOST_FW_OWN,
			    0, 500)) {
		dev_err(dev->mt76.dev, "Timeout for driver own\n");
		return -EIO;
	}

	return 0;
}

static int mt7921_load_patch(struct mt7921_dev *dev)
{
	const struct mt7921_patch_hdr *hdr;
	const struct firmware *fw = NULL;
	int i, ret, sem;

	sem = mt76_connac_mcu_patch_sem_ctrl(&dev->mt76, true);
	switch (sem) {
	case PATCH_IS_DL:
		return 0;
	case PATCH_NOT_DL_SEM_SUCCESS:
		break;
	default:
		dev_err(dev->mt76.dev, "Failed to get patch semaphore\n");
		return -EAGAIN;
	}

	ret = request_firmware(&fw, MT7921_ROM_PATCH, dev->mt76.dev);
	if (ret)
		goto out;

	if (!fw || !fw->data || fw->size < sizeof(*hdr)) {
		dev_err(dev->mt76.dev, "Invalid firmware\n");
		ret = -EINVAL;
		goto out;
	}

	hdr = (const struct mt7921_patch_hdr *)(fw->data);

	dev_info(dev->mt76.dev, "HW/SW Version: 0x%x, Build Time: %.16s\n",
		 be32_to_cpu(hdr->hw_sw_ver), hdr->build_date);

	for (i = 0; i < be32_to_cpu(hdr->desc.n_region); i++) {
		struct mt7921_patch_sec *sec;
		const u8 *dl;
		u32 len, addr;

		sec = (struct mt7921_patch_sec *)(fw->data + sizeof(*hdr) +
						  i * sizeof(*sec));
		if ((be32_to_cpu(sec->type) & PATCH_SEC_TYPE_MASK) !=
		    PATCH_SEC_TYPE_INFO) {
			ret = -EINVAL;
			goto out;
		}

		addr = be32_to_cpu(sec->info.addr);
		len = be32_to_cpu(sec->info.len);
		dl = fw->data + be32_to_cpu(sec->offs);

		ret = mt76_connac_mcu_init_download(&dev->mt76, addr, len,
						    DL_MODE_NEED_RSP);
		if (ret) {
			dev_err(dev->mt76.dev, "Download request failed\n");
			goto out;
		}

		ret = mt76_mcu_send_firmware(&dev->mt76, MCU_CMD_FW_SCATTER,
					     dl, len);
		if (ret) {
			dev_err(dev->mt76.dev, "Failed to send patch\n");
			goto out;
		}
	}

	ret = mt76_connac_mcu_start_patch(&dev->mt76);
	if (ret)
		dev_err(dev->mt76.dev, "Failed to start patch\n");

out:
	sem = mt76_connac_mcu_patch_sem_ctrl(&dev->mt76, false);
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

static u32 mt7921_mcu_gen_dl_mode(u8 feature_set, bool is_wa)
{
	u32 ret = 0;

	ret |= (feature_set & FW_FEATURE_SET_ENCRYPT) ?
	       (DL_MODE_ENCRYPT | DL_MODE_RESET_SEC_IV) : 0;
	ret |= (feature_set & FW_FEATURE_ENCRY_MODE) ?
	       DL_CONFIG_ENCRY_MODE_SEL : 0;
	ret |= FIELD_PREP(DL_MODE_KEY_IDX,
			  FIELD_GET(FW_FEATURE_SET_KEY_IDX, feature_set));
	ret |= DL_MODE_NEED_RSP;
	ret |= is_wa ? DL_MODE_WORKING_PDA_CR4 : 0;

	return ret;
}

static int
mt7921_mcu_send_ram_firmware(struct mt7921_dev *dev,
			     const struct mt7921_fw_trailer *hdr,
			     const u8 *data, bool is_wa)
{
	int i, offset = 0;
	u32 override = 0, option = 0;

	for (i = 0; i < hdr->n_region; i++) {
		const struct mt7921_fw_region *region;
		int err;
		u32 len, addr, mode;

		region = (const struct mt7921_fw_region *)((const u8 *)hdr -
			 (hdr->n_region - i) * sizeof(*region));
		mode = mt7921_mcu_gen_dl_mode(region->feature_set, is_wa);
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

		err = mt76_mcu_send_firmware(&dev->mt76, MCU_CMD_FW_SCATTER,
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

	return mt76_connac_mcu_start_firmware(&dev->mt76, override, option);
}

static int mt7921_load_ram(struct mt7921_dev *dev)
{
	const struct mt7921_fw_trailer *hdr;
	const struct firmware *fw;
	int ret;

	ret = request_firmware(&fw, MT7921_FIRMWARE_WM, dev->mt76.dev);
	if (ret)
		return ret;

	if (!fw || !fw->data || fw->size < sizeof(*hdr)) {
		dev_err(dev->mt76.dev, "Invalid firmware\n");
		ret = -EINVAL;
		goto out;
	}

	hdr = (const struct mt7921_fw_trailer *)(fw->data + fw->size -
					sizeof(*hdr));

	dev_info(dev->mt76.dev, "WM Firmware Version: %.10s, Build Time: %.15s\n",
		 hdr->fw_ver, hdr->build_date);

	ret = mt7921_mcu_send_ram_firmware(dev, hdr, fw->data, false);
	if (ret) {
		dev_err(dev->mt76.dev, "Failed to start WM firmware\n");
		goto out;
	}

	snprintf(dev->mt76.hw->wiphy->fw_version,
		 sizeof(dev->mt76.hw->wiphy->fw_version),
		 "%.10s-%.15s", hdr->fw_ver, hdr->build_date);

out:
	release_firmware(fw);

	return ret;
}

static int mt7921_load_firmware(struct mt7921_dev *dev)
{
	int ret;

	ret = mt76_get_field(dev, MT_CONN_ON_MISC, MT_TOP_MISC2_FW_N9_RDY);
	if (ret) {
		dev_dbg(dev->mt76.dev, "Firmware is already download\n");
		return -EIO;
	}

	ret = mt7921_load_patch(dev);
	if (ret)
		return ret;

	ret = mt7921_load_ram(dev);
	if (ret)
		return ret;

	if (!mt76_poll_msec(dev, MT_CONN_ON_MISC, MT_TOP_MISC2_FW_N9_RDY,
			    MT_TOP_MISC2_FW_N9_RDY, 1500)) {
		dev_err(dev->mt76.dev, "Timeout for initializing firmware\n");

		return -EIO;
	}

	mt76_queue_tx_cleanup(dev, dev->mt76.q_mcu[MT_MCUQ_FWDL], false);

#ifdef CONFIG_PM
	dev->mt76.hw->wiphy->wowlan = &mt76_connac_wowlan_support;
#endif /* CONFIG_PM */

	clear_bit(MT76_STATE_PM, &dev->mphy.state);

	dev_err(dev->mt76.dev, "Firmware init done\n");

	return 0;
}

int mt7921_mcu_fw_log_2_host(struct mt7921_dev *dev, u8 ctrl)
{
	struct {
		u8 ctrl_val;
		u8 pad[3];
	} data = {
		.ctrl_val = ctrl
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_CMD_FWLOG_2_HOST, &data,
				 sizeof(data), false);
}

int mt7921_run_firmware(struct mt7921_dev *dev)
{
	int err;

	err = mt7921_driver_own(dev);
	if (err)
		return err;

	err = mt7921_load_firmware(dev);
	if (err)
		return err;

	set_bit(MT76_STATE_MCU_RUNNING, &dev->mphy.state);
	mt7921_mcu_fw_log_2_host(dev, 1);

	return 0;
}

int mt7921_mcu_init(struct mt7921_dev *dev)
{
	static const struct mt76_mcu_ops mt7921_mcu_ops = {
		.headroom = sizeof(struct mt7921_mcu_txd),
		.mcu_skb_send_msg = mt7921_mcu_send_message,
		.mcu_parse_response = mt7921_mcu_parse_response,
		.mcu_restart = mt7921_mcu_restart,
	};

	dev->mt76.mcu_ops = &mt7921_mcu_ops;

	return mt7921_run_firmware(dev);
}

void mt7921_mcu_exit(struct mt7921_dev *dev)
{
	mt7921_wfsys_reset(dev);
	skb_queue_purge(&dev->mt76.mcu.res_q);
}

int mt7921_mcu_set_tx(struct mt7921_dev *dev, struct ieee80211_vif *vif)
{
#define WMM_AIFS_SET		BIT(0)
#define WMM_CW_MIN_SET		BIT(1)
#define WMM_CW_MAX_SET		BIT(2)
#define WMM_TXOP_SET		BIT(3)
#define WMM_PARAM_SET		GENMASK(3, 0)
#define TX_CMD_MODE		1
	struct edca {
		u8 queue;
		u8 set;
		u8 aifs;
		u8 cw_min;
		__le16 cw_max;
		__le16 txop;
	};
	struct mt7921_mcu_tx {
		u8 total;
		u8 action;
		u8 valid;
		u8 mode;

		struct edca edca[IEEE80211_NUM_ACS];
	} __packed req = {
		.valid = true,
		.mode = TX_CMD_MODE,
		.total = IEEE80211_NUM_ACS,
	};
	struct mt7921_vif *mvif = (struct mt7921_vif *)vif->drv_priv;
	int ac;

	for (ac = 0; ac < IEEE80211_NUM_ACS; ac++) {
		struct ieee80211_tx_queue_params *q = &mvif->queue_params[ac];
		struct edca *e = &req.edca[ac];

		e->set = WMM_PARAM_SET;
		e->queue = ac + mvif->mt76.wmm_idx * MT7921_MAX_WMM_SETS;
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
	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_EDCA_UPDATE, &req,
				 sizeof(req), true);
}

int mt7921_mcu_set_chan_info(struct mt7921_phy *phy, int cmd)
{
	struct mt7921_dev *dev = phy->dev;
	struct cfg80211_chan_def *chandef = &phy->mt76->chandef;
	int freq1 = chandef->center_freq1;
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
		.bw = mt7921_mcu_chan_bw(chandef),
		.tx_streams_num = hweight8(phy->mt76->antenna_mask),
		.rx_streams = phy->mt76->antenna_mask,
		.band_idx = phy != &dev->phy,
		.channel_band = chandef->chan->band,
	};

	if (dev->mt76.hw->conf.flags & IEEE80211_CONF_OFFCHANNEL)
		req.switch_reason = CH_SWITCH_SCAN_BYPASS_DPD;
	else if ((chandef->chan->flags & IEEE80211_CHAN_RADAR) &&
		 chandef->chan->dfs_state != NL80211_DFS_AVAILABLE)
		req.switch_reason = CH_SWITCH_DFS;
	else
		req.switch_reason = CH_SWITCH_NORMAL;

	if (cmd == MCU_EXT_CMD_CHANNEL_SWITCH)
		req.rx_streams = hweight8(req.rx_streams);

	if (chandef->width == NL80211_CHAN_WIDTH_80P80) {
		int freq2 = chandef->center_freq2;

		req.center_ch2 = ieee80211_frequency_to_channel(freq2);
	}

	return mt76_mcu_send_msg(&dev->mt76, cmd, &req, sizeof(req), true);
}

int mt7921_mcu_set_eeprom(struct mt7921_dev *dev)
{
	struct req_hdr {
		u8 buffer_mode;
		u8 format;
		__le16 len;
	} __packed req = {
		.buffer_mode = EE_MODE_EFUSE,
		.format = EE_FORMAT_WHOLE,
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_EFUSE_BUFFER_MODE,
				 &req, sizeof(req), true);
}

int mt7921_mcu_get_eeprom(struct mt7921_dev *dev, u32 offset)
{
	struct mt7921_mcu_eeprom_info req = {
		.addr = cpu_to_le32(round_down(offset, 16)),
	};
	struct mt7921_mcu_eeprom_info *res;
	struct sk_buff *skb;
	int ret;
	u8 *buf;

	ret = mt76_mcu_send_and_get_msg(&dev->mt76, MCU_EXT_CMD_EFUSE_ACCESS, &req,
					sizeof(req), true, &skb);
	if (ret)
		return ret;

	res = (struct mt7921_mcu_eeprom_info *)skb->data;
	buf = dev->mt76.eeprom.data + le32_to_cpu(res->addr);
	memcpy(buf, res->data, 16);
	dev_kfree_skb(skb);

	return 0;
}

u32 mt7921_get_wtbl_info(struct mt7921_dev *dev, u32 wlan_idx)
{
	struct mt7921_mcu_wlan_info wtbl_info = {
		.wlan_idx = cpu_to_le32(wlan_idx),
	};
	struct sk_buff *skb;
	int ret;

	ret = mt76_mcu_send_and_get_msg(&dev->mt76, MCU_CMD_GET_WTBL,
					&wtbl_info, sizeof(wtbl_info), true,
					&skb);
	if (ret)
		return ret;

	mt7921_mcu_tx_rate_report(dev, skb, wlan_idx);
	dev_kfree_skb(skb);

	return 0;
}

int mt7921_mcu_uni_bss_ps(struct mt7921_dev *dev, struct ieee80211_vif *vif)
{
	struct mt7921_vif *mvif = (struct mt7921_vif *)vif->drv_priv;
	struct {
		struct {
			u8 bss_idx;
			u8 pad[3];
		} __packed hdr;
		struct ps_tlv {
			__le16 tag;
			__le16 len;
			u8 ps_state; /* 0: device awake
				      * 1: static power save
				      * 2: dynamic power saving
				      * 3: enter TWT power saving
				      * 4: leave TWT power saving
				      */
			u8 pad[3];
		} __packed ps;
	} __packed ps_req = {
		.hdr = {
			.bss_idx = mvif->mt76.idx,
		},
		.ps = {
			.tag = cpu_to_le16(UNI_BSS_INFO_PS),
			.len = cpu_to_le16(sizeof(struct ps_tlv)),
			.ps_state = vif->bss_conf.ps ? 2 : 0,
		},
	};

	if (vif->type != NL80211_IFTYPE_STATION)
		return -EOPNOTSUPP;

	return mt76_mcu_send_msg(&dev->mt76, MCU_UNI_CMD_BSS_INFO_UPDATE,
				 &ps_req, sizeof(ps_req), true);
}

int mt7921_mcu_uni_bss_bcnft(struct mt7921_dev *dev, struct ieee80211_vif *vif,
			     bool enable)
{
	struct mt7921_vif *mvif = (struct mt7921_vif *)vif->drv_priv;
	struct {
		struct {
			u8 bss_idx;
			u8 pad[3];
		} __packed hdr;
		struct bcnft_tlv {
			__le16 tag;
			__le16 len;
			__le16 bcn_interval;
			u8 dtim_period;
			u8 pad;
		} __packed bcnft;
	} __packed bcnft_req = {
		.hdr = {
			.bss_idx = mvif->mt76.idx,
		},
		.bcnft = {
			.tag = cpu_to_le16(UNI_BSS_INFO_BCNFT),
			.len = cpu_to_le16(sizeof(struct bcnft_tlv)),
			.bcn_interval = cpu_to_le16(vif->bss_conf.beacon_int),
			.dtim_period = vif->bss_conf.dtim_period,
		},
	};

	if (vif->type != NL80211_IFTYPE_STATION)
		return 0;

	return mt76_mcu_send_msg(&dev->mt76, MCU_UNI_CMD_BSS_INFO_UPDATE,
				 &bcnft_req, sizeof(bcnft_req), true);
}

int mt7921_mcu_set_bss_pm(struct mt7921_dev *dev, struct ieee80211_vif *vif,
			  bool enable)
{
	struct mt7921_vif *mvif = (struct mt7921_vif *)vif->drv_priv;
	struct {
		u8 bss_idx;
		u8 dtim_period;
		__le16 aid;
		__le16 bcn_interval;
		__le16 atim_window;
		u8 uapsd;
		u8 bmc_delivered_ac;
		u8 bmc_triggered_ac;
		u8 pad;
	} req = {
		.bss_idx = mvif->mt76.idx,
		.aid = cpu_to_le16(vif->bss_conf.aid),
		.dtim_period = vif->bss_conf.dtim_period,
		.bcn_interval = cpu_to_le16(vif->bss_conf.beacon_int),
	};
	struct {
		u8 bss_idx;
		u8 pad[3];
	} req_hdr = {
		.bss_idx = mvif->mt76.idx,
	};
	int err;

	if (vif->type != NL80211_IFTYPE_STATION)
		return 0;

	err = mt76_mcu_send_msg(&dev->mt76, MCU_CMD_SET_BSS_ABORT, &req_hdr,
				sizeof(req_hdr), false);
	if (err < 0 || !enable)
		return err;

	return mt76_mcu_send_msg(&dev->mt76, MCU_CMD_SET_BSS_CONNECTED, &req,
				 sizeof(req), false);
}

int mt7921_mcu_sta_add(struct mt7921_dev *dev, struct ieee80211_sta *sta,
		       struct ieee80211_vif *vif, bool enable)
{
	struct mt7921_vif *mvif = (struct mt7921_vif *)vif->drv_priv;
	int rssi = -ewma_rssi_read(&mvif->rssi);
	struct mt76_sta_cmd_info info = {
		.sta = sta,
		.vif = vif,
		.enable = enable,
		.cmd = MCU_UNI_CMD_STA_REC_UPDATE,
		.rcpi = to_rcpi(rssi),
	};
	struct mt7921_sta *msta;

	msta = sta ? (struct mt7921_sta *)sta->drv_priv : NULL;
	info.wcid = msta ? &msta->wcid : &mvif->sta.wcid;

	return mt76_connac_mcu_add_sta_cmd(&dev->mphy, &info);
}

int mt7921_mcu_drv_pmctrl(struct mt7921_dev *dev)
{
	struct mt76_phy *mphy = &dev->mt76.phy;
	struct mt76_connac_pm *pm = &dev->pm;
	int i, err = 0;

	mutex_lock(&pm->mutex);

	if (!test_bit(MT76_STATE_PM, &mphy->state))
		goto out;

	for (i = 0; i < MT7921_DRV_OWN_RETRY_COUNT; i++) {
		mt76_wr(dev, MT_CONN_ON_LPCTL, PCIE_LPCR_HOST_CLR_OWN);
		if (mt76_poll_msec(dev, MT_CONN_ON_LPCTL,
				   PCIE_LPCR_HOST_OWN_SYNC, 0, 50))
			break;
	}

	if (i == MT7921_DRV_OWN_RETRY_COUNT) {
		dev_err(dev->mt76.dev, "driver own failed\n");
		err = -EIO;
		goto out;
	}

	mt7921_wpdma_reinit_cond(dev);
	clear_bit(MT76_STATE_PM, &mphy->state);

	pm->stats.last_wake_event = jiffies;
	pm->stats.doze_time += pm->stats.last_wake_event -
			       pm->stats.last_doze_event;
out:
	mutex_unlock(&pm->mutex);

	if (err)
		mt7921_reset(&dev->mt76);

	return err;
}

int mt7921_mcu_fw_pmctrl(struct mt7921_dev *dev)
{
	struct mt76_phy *mphy = &dev->mt76.phy;
	struct mt76_connac_pm *pm = &dev->pm;
	int i, err = 0;

	mutex_lock(&pm->mutex);

	if (mt76_connac_skip_fw_pmctrl(mphy, pm))
		goto out;

	for (i = 0; i < MT7921_DRV_OWN_RETRY_COUNT; i++) {
		mt76_wr(dev, MT_CONN_ON_LPCTL, PCIE_LPCR_HOST_SET_OWN);
		if (mt76_poll_msec(dev, MT_CONN_ON_LPCTL,
				   PCIE_LPCR_HOST_OWN_SYNC, 4, 50))
			break;
	}

	if (i == MT7921_DRV_OWN_RETRY_COUNT) {
		dev_err(dev->mt76.dev, "firmware own failed\n");
		clear_bit(MT76_STATE_PM, &mphy->state);
		err = -EIO;
	}

	pm->stats.last_doze_event = jiffies;
	pm->stats.awake_time += pm->stats.last_doze_event -
				pm->stats.last_wake_event;
out:
	mutex_unlock(&pm->mutex);

	if (err)
		mt7921_reset(&dev->mt76);

	return err;
}

void
mt7921_pm_interface_iter(void *priv, u8 *mac, struct ieee80211_vif *vif)
{
	struct mt7921_phy *phy = priv;
	struct mt7921_dev *dev = phy->dev;
	int ret;

	if (dev->pm.enable)
		ret = mt7921_mcu_uni_bss_bcnft(dev, vif, true);
	else
		ret = mt7921_mcu_set_bss_pm(dev, vif, false);

	if (ret)
		return;

	if (dev->pm.enable) {
		vif->driver_flags |= IEEE80211_VIF_BEACON_FILTER;
		mt76_set(dev, MT_WF_RFCR(0), MT_WF_RFCR_DROP_OTHER_BEACON);
	} else {
		vif->driver_flags &= ~IEEE80211_VIF_BEACON_FILTER;
		mt76_clear(dev, MT_WF_RFCR(0), MT_WF_RFCR_DROP_OTHER_BEACON);
	}
}

int mt7921_get_txpwr_info(struct mt7921_dev *dev, struct mt7921_txpwr *txpwr)
{
	struct mt7921_txpwr_event *event;
	struct mt7921_txpwr_req req = {
		.dbdc_idx = 0,
	};
	struct sk_buff *skb;
	int ret;

	ret = mt76_mcu_send_and_get_msg(&dev->mt76, MCU_CMD_GET_TXPWR,
					&req, sizeof(req), true, &skb);
	if (ret)
		return ret;

	event = (struct mt7921_txpwr_event *)skb->data;
	WARN_ON(skb->len != le16_to_cpu(event->len));
	memcpy(txpwr, &event->txpwr, sizeof(event->txpwr));

	dev_kfree_skb(skb);

	return 0;
}
