// SPDX-License-Identifier: ISC
/* Copyright (C) 2019 MediaTek Inc.
 *
 * Author: Roy Luo <royluo@google.com>
 *         Ryder Lee <ryder.lee@mediatek.com>
 */

#include <linux/firmware.h>
#include "mt7615.h"
#include "mcu.h"
#include "mac.h"
#include "eeprom.h"

static bool prefer_offload_fw = true;
module_param(prefer_offload_fw, bool, 0644);
MODULE_PARM_DESC(prefer_offload_fw,
		 "Prefer client mode offload firmware (MT7663)");

struct mt7615_patch_hdr {
	char build_date[16];
	char platform[4];
	__be32 hw_sw_ver;
	__be32 patch_ver;
	__be16 checksum;
} __packed;

struct mt7615_fw_trailer {
	__le32 addr;
	u8 chip_id;
	u8 feature_set;
	u8 eco_code;
	char fw_ver[10];
	char build_date[15];
	__le32 len;
} __packed;

#define FW_V3_COMMON_TAILER_SIZE	36
#define FW_V3_REGION_TAILER_SIZE	40
#define FW_START_OVERRIDE		BIT(0)
#define FW_START_DLYCAL                 BIT(1)
#define FW_START_WORKING_PDA_CR4	BIT(2)

struct mt7663_fw_buf {
	__le32 crc;
	__le32 d_img_size;
	__le32 block_size;
	u8 rsv[4];
	__le32 img_dest_addr;
	__le32 img_size;
	u8 feature_set;
};

#define MT7615_PATCH_ADDRESS		0x80000
#define MT7622_PATCH_ADDRESS		0x9c000
#define MT7663_PATCH_ADDRESS		0xdc000

#define N9_REGION_NUM			2
#define CR4_REGION_NUM			1

#define IMG_CRC_LEN			4

void mt7615_mcu_fill_msg(struct mt7615_dev *dev, struct sk_buff *skb,
			 int cmd, int *wait_seq)
{
	int txd_len, mcu_cmd = FIELD_GET(__MCU_CMD_FIELD_ID, cmd);
	struct mt7615_uni_txd *uni_txd;
	struct mt7615_mcu_txd *mcu_txd;
	u8 seq, q_idx, pkt_fmt;
	__le32 *txd;
	u32 val;

	/* TODO: make dynamic based on msg type */
	dev->mt76.mcu.timeout = 20 * HZ;

	seq = ++dev->mt76.mcu.msg_seq & 0xf;
	if (!seq)
		seq = ++dev->mt76.mcu.msg_seq & 0xf;
	if (wait_seq)
		*wait_seq = seq;

	txd_len = cmd & __MCU_CMD_FIELD_UNI ? sizeof(*uni_txd) : sizeof(*mcu_txd);
	txd = (__le32 *)skb_push(skb, txd_len);

	if (cmd != MCU_CMD(FW_SCATTER)) {
		q_idx = MT_TX_MCU_PORT_RX_Q0;
		pkt_fmt = MT_TX_TYPE_CMD;
	} else {
		q_idx = MT_TX_MCU_PORT_RX_FWDL;
		pkt_fmt = MT_TX_TYPE_FW;
	}

	val = FIELD_PREP(MT_TXD0_TX_BYTES, skb->len) |
	      FIELD_PREP(MT_TXD0_P_IDX, MT_TX_PORT_IDX_MCU) |
	      FIELD_PREP(MT_TXD0_Q_IDX, q_idx);
	txd[0] = cpu_to_le32(val);

	val = MT_TXD1_LONG_FORMAT |
	      FIELD_PREP(MT_TXD1_HDR_FORMAT, MT_HDR_FORMAT_CMD) |
	      FIELD_PREP(MT_TXD1_PKT_FMT, pkt_fmt);
	txd[1] = cpu_to_le32(val);

	if (cmd & __MCU_CMD_FIELD_UNI) {
		uni_txd = (struct mt7615_uni_txd *)txd;
		uni_txd->len = cpu_to_le16(skb->len - sizeof(uni_txd->txd));
		uni_txd->option = MCU_CMD_UNI_EXT_ACK;
		uni_txd->cid = cpu_to_le16(mcu_cmd);
		uni_txd->s2d_index = MCU_S2D_H2N;
		uni_txd->pkt_type = MCU_PKT_ID;
		uni_txd->seq = seq;

		return;
	}

	mcu_txd = (struct mt7615_mcu_txd *)txd;
	mcu_txd->len = cpu_to_le16(skb->len - sizeof(mcu_txd->txd));
	mcu_txd->pq_id = cpu_to_le16(MCU_PQ_ID(MT_TX_PORT_IDX_MCU, q_idx));
	mcu_txd->s2d_index = MCU_S2D_H2N;
	mcu_txd->pkt_type = MCU_PKT_ID;
	mcu_txd->seq = seq;
	mcu_txd->cid = mcu_cmd;
	mcu_txd->ext_cid = FIELD_GET(__MCU_CMD_FIELD_EXT_ID, cmd);

	if (mcu_txd->ext_cid || (cmd & __MCU_CMD_FIELD_CE)) {
		if (cmd & __MCU_CMD_FIELD_QUERY)
			mcu_txd->set_query = MCU_Q_QUERY;
		else
			mcu_txd->set_query = MCU_Q_SET;
		mcu_txd->ext_cid_ack = !!mcu_txd->ext_cid;
	} else {
		mcu_txd->set_query = MCU_Q_NA;
	}
}
EXPORT_SYMBOL_GPL(mt7615_mcu_fill_msg);

int mt7615_mcu_parse_response(struct mt76_dev *mdev, int cmd,
			      struct sk_buff *skb, int seq)
{
	struct mt7615_mcu_rxd *rxd;
	int ret = 0;

	if (!skb) {
		dev_err(mdev->dev, "Message %08x (seq %d) timeout\n",
			cmd, seq);
		return -ETIMEDOUT;
	}

	rxd = (struct mt7615_mcu_rxd *)skb->data;
	if (seq != rxd->seq)
		return -EAGAIN;

	if (cmd == MCU_CMD(PATCH_SEM_CONTROL)) {
		skb_pull(skb, sizeof(*rxd) - 4);
		ret = *skb->data;
	} else if (cmd == MCU_EXT_CMD(THERMAL_CTRL)) {
		skb_pull(skb, sizeof(*rxd));
		ret = le32_to_cpu(*(__le32 *)skb->data);
	} else if (cmd == MCU_EXT_QUERY(RF_REG_ACCESS)) {
		skb_pull(skb, sizeof(*rxd));
		ret = le32_to_cpu(*(__le32 *)&skb->data[8]);
	} else if (cmd == MCU_UNI_CMD(DEV_INFO_UPDATE) ||
		   cmd == MCU_UNI_CMD(BSS_INFO_UPDATE) ||
		   cmd == MCU_UNI_CMD(STA_REC_UPDATE) ||
		   cmd == MCU_UNI_CMD(HIF_CTRL) ||
		   cmd == MCU_UNI_CMD(OFFLOAD) ||
		   cmd == MCU_UNI_CMD(SUSPEND)) {
		struct mt76_connac_mcu_uni_event *event;

		skb_pull(skb, sizeof(*rxd));
		event = (struct mt76_connac_mcu_uni_event *)skb->data;
		ret = le32_to_cpu(event->status);
	} else if (cmd == MCU_CE_QUERY(REG_READ)) {
		struct mt76_connac_mcu_reg_event *event;

		skb_pull(skb, sizeof(*rxd));
		event = (struct mt76_connac_mcu_reg_event *)skb->data;
		ret = (int)le32_to_cpu(event->val);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(mt7615_mcu_parse_response);

static int
mt7615_mcu_send_message(struct mt76_dev *mdev, struct sk_buff *skb,
			int cmd, int *seq)
{
	struct mt7615_dev *dev = container_of(mdev, struct mt7615_dev, mt76);
	enum mt76_mcuq_id qid;

	mt7615_mcu_fill_msg(dev, skb, cmd, seq);
	if (test_bit(MT76_STATE_MCU_RUNNING, &dev->mphy.state))
		qid = MT_MCUQ_WM;
	else
		qid = MT_MCUQ_FWDL;

	return mt76_tx_queue_skb_raw(dev, dev->mt76.q_mcu[qid], skb, 0);
}

u32 mt7615_rf_rr(struct mt7615_dev *dev, u32 wf, u32 reg)
{
	struct {
		__le32 wifi_stream;
		__le32 address;
		__le32 data;
	} req = {
		.wifi_stream = cpu_to_le32(wf),
		.address = cpu_to_le32(reg),
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_QUERY(RF_REG_ACCESS),
				 &req, sizeof(req), true);
}

int mt7615_rf_wr(struct mt7615_dev *dev, u32 wf, u32 reg, u32 val)
{
	struct {
		__le32 wifi_stream;
		__le32 address;
		__le32 data;
	} req = {
		.wifi_stream = cpu_to_le32(wf),
		.address = cpu_to_le32(reg),
		.data = cpu_to_le32(val),
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(RF_REG_ACCESS),
				 &req, sizeof(req), false);
}

void mt7622_trigger_hif_int(struct mt7615_dev *dev, bool en)
{
	if (!is_mt7622(&dev->mt76))
		return;

	regmap_update_bits(dev->infracfg, MT_INFRACFG_MISC,
			   MT_INFRACFG_MISC_AP2CONN_WAKE,
			   !en * MT_INFRACFG_MISC_AP2CONN_WAKE);
}
EXPORT_SYMBOL_GPL(mt7622_trigger_hif_int);

static int mt7615_mcu_drv_pmctrl(struct mt7615_dev *dev)
{
	struct mt76_phy *mphy = &dev->mt76.phy;
	struct mt76_connac_pm *pm = &dev->pm;
	struct mt76_dev *mdev = &dev->mt76;
	u32 addr;
	int err;

	if (is_mt7663(mdev)) {
		/* Clear firmware own via N9 eint */
		mt76_wr(dev, MT_PCIE_DOORBELL_PUSH, MT_CFG_LPCR_HOST_DRV_OWN);
		mt76_poll(dev, MT_CONN_ON_MISC, MT_CFG_LPCR_HOST_FW_OWN, 0, 3000);

		addr = MT_CONN_HIF_ON_LPCTL;
	} else {
		addr = MT_CFG_LPCR_HOST;
	}

	mt76_wr(dev, addr, MT_CFG_LPCR_HOST_DRV_OWN);

	mt7622_trigger_hif_int(dev, true);

	err = !mt76_poll_msec(dev, addr, MT_CFG_LPCR_HOST_FW_OWN, 0, 3000);

	mt7622_trigger_hif_int(dev, false);

	if (err) {
		dev_err(mdev->dev, "driver own failed\n");
		return -ETIMEDOUT;
	}

	clear_bit(MT76_STATE_PM, &mphy->state);

	pm->stats.last_wake_event = jiffies;
	pm->stats.doze_time += pm->stats.last_wake_event -
			       pm->stats.last_doze_event;

	return 0;
}

static int mt7615_mcu_lp_drv_pmctrl(struct mt7615_dev *dev)
{
	struct mt76_phy *mphy = &dev->mt76.phy;
	struct mt76_connac_pm *pm = &dev->pm;
	int i, err = 0;

	mutex_lock(&pm->mutex);

	if (!test_bit(MT76_STATE_PM, &mphy->state))
		goto out;

	for (i = 0; i < MT7615_DRV_OWN_RETRY_COUNT; i++) {
		mt76_wr(dev, MT_PCIE_DOORBELL_PUSH, MT_CFG_LPCR_HOST_DRV_OWN);
		if (mt76_poll_msec(dev, MT_CONN_HIF_ON_LPCTL,
				   MT_CFG_LPCR_HOST_FW_OWN, 0, 50))
			break;
	}

	if (i == MT7615_DRV_OWN_RETRY_COUNT) {
		dev_err(dev->mt76.dev, "driver own failed\n");
		err = -EIO;
		goto out;
	}
	clear_bit(MT76_STATE_PM, &mphy->state);

	pm->stats.last_wake_event = jiffies;
	pm->stats.doze_time += pm->stats.last_wake_event -
			       pm->stats.last_doze_event;
out:
	mutex_unlock(&pm->mutex);

	return err;
}

static int mt7615_mcu_fw_pmctrl(struct mt7615_dev *dev)
{
	struct mt76_phy *mphy = &dev->mt76.phy;
	struct mt76_connac_pm *pm = &dev->pm;
	int err = 0;
	u32 addr;

	mutex_lock(&pm->mutex);

	if (mt76_connac_skip_fw_pmctrl(mphy, pm))
		goto out;

	mt7622_trigger_hif_int(dev, true);

	addr = is_mt7663(&dev->mt76) ? MT_CONN_HIF_ON_LPCTL : MT_CFG_LPCR_HOST;
	mt76_wr(dev, addr, MT_CFG_LPCR_HOST_FW_OWN);

	if (is_mt7622(&dev->mt76) &&
	    !mt76_poll_msec(dev, addr, MT_CFG_LPCR_HOST_FW_OWN,
			    MT_CFG_LPCR_HOST_FW_OWN, 3000)) {
		dev_err(dev->mt76.dev, "Timeout for firmware own\n");
		clear_bit(MT76_STATE_PM, &mphy->state);
		err = -EIO;
	}

	mt7622_trigger_hif_int(dev, false);
	if (!err) {
		pm->stats.last_doze_event = jiffies;
		pm->stats.awake_time += pm->stats.last_doze_event -
					pm->stats.last_wake_event;
	}
out:
	mutex_unlock(&pm->mutex);

	return err;
}

static void
mt7615_mcu_csa_finish(void *priv, u8 *mac, struct ieee80211_vif *vif)
{
	if (vif->bss_conf.csa_active)
		ieee80211_csa_finish(vif);
}

static void
mt7615_mcu_rx_csa_notify(struct mt7615_dev *dev, struct sk_buff *skb)
{
	struct mt7615_phy *ext_phy = mt7615_ext_phy(dev);
	struct mt76_phy *mphy = &dev->mt76.phy;
	struct mt7615_mcu_csa_notify *c;

	c = (struct mt7615_mcu_csa_notify *)skb->data;

	if (c->omac_idx > EXT_BSSID_MAX)
		return;

	if (ext_phy && ext_phy->omac_mask & BIT_ULL(c->omac_idx))
		mphy = dev->mt76.phys[MT_BAND1];

	ieee80211_iterate_active_interfaces_atomic(mphy->hw,
			IEEE80211_IFACE_ITER_RESUME_ALL,
			mt7615_mcu_csa_finish, mphy->hw);
}

static void
mt7615_mcu_rx_radar_detected(struct mt7615_dev *dev, struct sk_buff *skb)
{
	struct mt76_phy *mphy = &dev->mt76.phy;
	struct mt7615_mcu_rdd_report *r;

	r = (struct mt7615_mcu_rdd_report *)skb->data;

	if (!dev->radar_pattern.n_pulses && !r->long_detected &&
	    !r->constant_prf_detected && !r->staggered_prf_detected)
		return;

	if (r->band_idx && dev->mt76.phys[MT_BAND1])
		mphy = dev->mt76.phys[MT_BAND1];

	if (mt76_phy_dfs_state(mphy) < MT_DFS_STATE_CAC)
		return;

	ieee80211_radar_detected(mphy->hw);
	dev->hw_pattern++;
}

static void
mt7615_mcu_rx_log_message(struct mt7615_dev *dev, struct sk_buff *skb)
{
	struct mt7615_mcu_rxd *rxd = (struct mt7615_mcu_rxd *)skb->data;
	const char *data = (char *)&rxd[1];
	const char *type;

	switch (rxd->s2d_index) {
	case 0:
		type = "N9";
		break;
	case 2:
		type = "CR4";
		break;
	default:
		type = "unknown";
		break;
	}

	wiphy_info(mt76_hw(dev)->wiphy, "%s: %.*s", type,
		   (int)(skb->len - sizeof(*rxd)), data);
}

static void
mt7615_mcu_rx_ext_event(struct mt7615_dev *dev, struct sk_buff *skb)
{
	struct mt7615_mcu_rxd *rxd = (struct mt7615_mcu_rxd *)skb->data;

	switch (rxd->ext_eid) {
	case MCU_EXT_EVENT_RDD_REPORT:
		mt7615_mcu_rx_radar_detected(dev, skb);
		break;
	case MCU_EXT_EVENT_CSA_NOTIFY:
		mt7615_mcu_rx_csa_notify(dev, skb);
		break;
	case MCU_EXT_EVENT_FW_LOG_2_HOST:
		mt7615_mcu_rx_log_message(dev, skb);
		break;
	default:
		break;
	}
}

static void
mt7615_mcu_scan_event(struct mt7615_dev *dev, struct sk_buff *skb)
{
	u8 *seq_num = skb->data + sizeof(struct mt7615_mcu_rxd);
	struct mt7615_phy *phy;
	struct mt76_phy *mphy;

	if (*seq_num & BIT(7) && dev->mt76.phys[MT_BAND1])
		mphy = dev->mt76.phys[MT_BAND1];
	else
		mphy = &dev->mt76.phy;

	phy = (struct mt7615_phy *)mphy->priv;

	spin_lock_bh(&dev->mt76.lock);
	__skb_queue_tail(&phy->scan_event_list, skb);
	spin_unlock_bh(&dev->mt76.lock);

	ieee80211_queue_delayed_work(mphy->hw, &phy->scan_work,
				     MT7615_HW_SCAN_TIMEOUT);
}

static void
mt7615_mcu_roc_event(struct mt7615_dev *dev, struct sk_buff *skb)
{
	struct mt7615_roc_tlv *event;
	struct mt7615_phy *phy;
	struct mt76_phy *mphy;
	int duration;

	skb_pull(skb, sizeof(struct mt7615_mcu_rxd));
	event = (struct mt7615_roc_tlv *)skb->data;

	if (event->dbdc_band && dev->mt76.phys[MT_BAND1])
		mphy = dev->mt76.phys[MT_BAND1];
	else
		mphy = &dev->mt76.phy;

	ieee80211_ready_on_channel(mphy->hw);

	phy = (struct mt7615_phy *)mphy->priv;
	phy->roc_grant = true;
	wake_up(&phy->roc_wait);

	duration = le32_to_cpu(event->max_interval);
	mod_timer(&phy->roc_timer,
		  round_jiffies_up(jiffies + msecs_to_jiffies(duration)));
}

static void
mt7615_mcu_beacon_loss_event(struct mt7615_dev *dev, struct sk_buff *skb)
{
	struct mt76_connac_beacon_loss_event *event;
	struct mt76_phy *mphy;
	u8 band_idx = 0; /* DBDC support */

	skb_pull(skb, sizeof(struct mt7615_mcu_rxd));
	event = (struct mt76_connac_beacon_loss_event *)skb->data;
	if (band_idx && dev->mt76.phys[MT_BAND1])
		mphy = dev->mt76.phys[MT_BAND1];
	else
		mphy = &dev->mt76.phy;

	ieee80211_iterate_active_interfaces_atomic(mphy->hw,
					IEEE80211_IFACE_ITER_RESUME_ALL,
					mt76_connac_mcu_beacon_loss_iter,
					event);
}

static void
mt7615_mcu_bss_event(struct mt7615_dev *dev, struct sk_buff *skb)
{
	struct mt76_connac_mcu_bss_event *event;
	struct mt76_phy *mphy;
	u8 band_idx = 0; /* DBDC support */

	skb_pull(skb, sizeof(struct mt7615_mcu_rxd));
	event = (struct mt76_connac_mcu_bss_event *)skb->data;

	if (band_idx && dev->mt76.phys[MT_BAND1])
		mphy = dev->mt76.phys[MT_BAND1];
	else
		mphy = &dev->mt76.phy;

	if (event->is_absent)
		ieee80211_stop_queues(mphy->hw);
	else
		ieee80211_wake_queues(mphy->hw);
}

static void
mt7615_mcu_rx_unsolicited_event(struct mt7615_dev *dev, struct sk_buff *skb)
{
	struct mt7615_mcu_rxd *rxd = (struct mt7615_mcu_rxd *)skb->data;

	switch (rxd->eid) {
	case MCU_EVENT_EXT:
		mt7615_mcu_rx_ext_event(dev, skb);
		break;
	case MCU_EVENT_BSS_BEACON_LOSS:
		mt7615_mcu_beacon_loss_event(dev, skb);
		break;
	case MCU_EVENT_ROC:
		mt7615_mcu_roc_event(dev, skb);
		break;
	case MCU_EVENT_SCHED_SCAN_DONE:
	case MCU_EVENT_SCAN_DONE:
		mt7615_mcu_scan_event(dev, skb);
		return;
	case MCU_EVENT_BSS_ABSENCE:
		mt7615_mcu_bss_event(dev, skb);
		break;
	case MCU_EVENT_COREDUMP:
		mt76_connac_mcu_coredump_event(&dev->mt76, skb,
					       &dev->coredump);
		return;
	default:
		break;
	}
	dev_kfree_skb(skb);
}

void mt7615_mcu_rx_event(struct mt7615_dev *dev, struct sk_buff *skb)
{
	struct mt7615_mcu_rxd *rxd = (struct mt7615_mcu_rxd *)skb->data;

	if (rxd->ext_eid == MCU_EXT_EVENT_THERMAL_PROTECT ||
	    rxd->ext_eid == MCU_EXT_EVENT_FW_LOG_2_HOST ||
	    rxd->ext_eid == MCU_EXT_EVENT_ASSERT_DUMP ||
	    rxd->ext_eid == MCU_EXT_EVENT_PS_SYNC ||
	    rxd->eid == MCU_EVENT_BSS_BEACON_LOSS ||
	    rxd->eid == MCU_EVENT_SCHED_SCAN_DONE ||
	    rxd->eid == MCU_EVENT_BSS_ABSENCE ||
	    rxd->eid == MCU_EVENT_SCAN_DONE ||
	    rxd->eid == MCU_EVENT_COREDUMP ||
	    rxd->eid == MCU_EVENT_ROC ||
	    !rxd->seq)
		mt7615_mcu_rx_unsolicited_event(dev, skb);
	else
		mt76_mcu_rx_event(&dev->mt76, skb);
}

static int
mt7615_mcu_muar_config(struct mt7615_dev *dev, struct ieee80211_vif *vif,
		       bool bssid, bool enable)
{
	struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;
	u32 idx = mvif->mt76.omac_idx - REPEATER_BSSID_START;
	u32 mask = dev->omac_mask >> 32 & ~BIT(idx);
	const u8 *addr = vif->addr;
	struct {
		u8 mode;
		u8 force_clear;
		u8 clear_bitmap[8];
		u8 entry_count;
		u8 write;

		u8 index;
		u8 bssid;
		u8 addr[ETH_ALEN];
	} __packed req = {
		.mode = !!mask || enable,
		.entry_count = 1,
		.write = 1,

		.index = idx * 2 + bssid,
	};

	if (bssid)
		addr = vif->bss_conf.bssid;

	if (enable)
		ether_addr_copy(req.addr, addr);

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(MUAR_UPDATE),
				 &req, sizeof(req), true);
}

static int
mt7615_mcu_add_dev(struct mt7615_phy *phy, struct ieee80211_vif *vif,
		   bool enable)
{
	struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;
	struct mt7615_dev *dev = phy->dev;
	struct {
		struct req_hdr {
			u8 omac_idx;
			u8 band_idx;
			__le16 tlv_num;
			u8 is_tlv_append;
			u8 rsv[3];
		} __packed hdr;
		struct req_tlv {
			__le16 tag;
			__le16 len;
			u8 active;
			u8 band_idx;
			u8 omac_addr[ETH_ALEN];
		} __packed tlv;
	} data = {
		.hdr = {
			.omac_idx = mvif->mt76.omac_idx,
			.band_idx = mvif->mt76.band_idx,
			.tlv_num = cpu_to_le16(1),
			.is_tlv_append = 1,
		},
		.tlv = {
			.tag = cpu_to_le16(DEV_INFO_ACTIVE),
			.len = cpu_to_le16(sizeof(struct req_tlv)),
			.active = enable,
			.band_idx = mvif->mt76.band_idx,
		},
	};

	if (mvif->mt76.omac_idx >= REPEATER_BSSID_START)
		return mt7615_mcu_muar_config(dev, vif, false, enable);

	memcpy(data.tlv.omac_addr, vif->addr, ETH_ALEN);
	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(DEV_INFO_UPDATE),
				 &data, sizeof(data), true);
}

static int
mt7615_mcu_add_beacon_offload(struct mt7615_dev *dev,
			      struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif, bool enable)
{
	struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;
	struct mt76_wcid *wcid = &dev->mt76.global_wcid;
	struct ieee80211_mutable_offsets offs;
	struct ieee80211_tx_info *info;
	struct req {
		u8 omac_idx;
		u8 enable;
		u8 wlan_idx;
		u8 band_idx;
		u8 pkt_type;
		u8 need_pre_tbtt_int;
		__le16 csa_ie_pos;
		__le16 pkt_len;
		__le16 tim_ie_pos;
		u8 pkt[512];
		u8 csa_cnt;
		/* bss color change */
		u8 bcc_cnt;
		__le16 bcc_ie_pos;
	} __packed req = {
		.omac_idx = mvif->mt76.omac_idx,
		.enable = enable,
		.wlan_idx = wcid->idx,
		.band_idx = mvif->mt76.band_idx,
	};
	struct sk_buff *skb;

	if (!enable)
		goto out;

	skb = ieee80211_beacon_get_template(hw, vif, &offs, 0);
	if (!skb)
		return -EINVAL;

	if (skb->len > 512 - MT_TXD_SIZE) {
		dev_err(dev->mt76.dev, "Bcn size limit exceed\n");
		dev_kfree_skb(skb);
		return -EINVAL;
	}

	info = IEEE80211_SKB_CB(skb);
	info->hw_queue |= FIELD_PREP(MT_TX_HW_QUEUE_PHY, mvif->mt76.band_idx);

	mt7615_mac_write_txwi(dev, (__le32 *)(req.pkt), skb, wcid, NULL,
			      0, NULL, 0, true);
	memcpy(req.pkt + MT_TXD_SIZE, skb->data, skb->len);
	req.pkt_len = cpu_to_le16(MT_TXD_SIZE + skb->len);
	req.tim_ie_pos = cpu_to_le16(MT_TXD_SIZE + offs.tim_offset);
	if (offs.cntdwn_counter_offs[0]) {
		u16 csa_offs;

		csa_offs = MT_TXD_SIZE + offs.cntdwn_counter_offs[0] - 4;
		req.csa_ie_pos = cpu_to_le16(csa_offs);
		req.csa_cnt = skb->data[offs.cntdwn_counter_offs[0]];
	}
	dev_kfree_skb(skb);

out:
	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(BCN_OFFLOAD), &req,
				 sizeof(req), true);
}

static int
mt7615_mcu_ctrl_pm_state(struct mt7615_dev *dev, int band, int state)
{
	return mt76_connac_mcu_set_pm(&dev->mt76, band, state);
}

static int
mt7615_mcu_add_bss(struct mt7615_phy *phy, struct ieee80211_vif *vif,
		   struct ieee80211_sta *sta, bool enable)
{
	struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;
	struct mt7615_dev *dev = phy->dev;
	struct sk_buff *skb;

	if (mvif->mt76.omac_idx >= REPEATER_BSSID_START)
		mt7615_mcu_muar_config(dev, vif, true, enable);

	skb = mt76_connac_mcu_alloc_sta_req(&dev->mt76, &mvif->mt76, NULL);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	if (enable)
		mt76_connac_mcu_bss_omac_tlv(skb, vif);

	mt76_connac_mcu_bss_basic_tlv(skb, vif, sta, phy->mt76,
				      mvif->sta.wcid.idx, enable);

	if (enable && mvif->mt76.omac_idx >= EXT_BSSID_START &&
	    mvif->mt76.omac_idx < REPEATER_BSSID_START)
		mt76_connac_mcu_bss_ext_tlv(skb, &mvif->mt76);

	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_EXT_CMD(BSS_INFO_UPDATE), true);
}

static int
mt7615_mcu_wtbl_tx_ba(struct mt7615_dev *dev,
		      struct ieee80211_ampdu_params *params,
		      bool enable)
{
	struct mt7615_sta *msta = (struct mt7615_sta *)params->sta->drv_priv;
	struct mt7615_vif *mvif = msta->vif;
	struct wtbl_req_hdr *wtbl_hdr;
	struct sk_buff *skb = NULL;
	int err;

	wtbl_hdr = mt76_connac_mcu_alloc_wtbl_req(&dev->mt76, &msta->wcid,
						  WTBL_SET, NULL, &skb);
	if (IS_ERR(wtbl_hdr))
		return PTR_ERR(wtbl_hdr);

	mt76_connac_mcu_wtbl_ba_tlv(&dev->mt76, skb, params, enable, true,
				    NULL, wtbl_hdr);

	err = mt76_mcu_skb_send_msg(&dev->mt76, skb,
				    MCU_EXT_CMD(WTBL_UPDATE), true);
	if (err < 0)
		return err;

	skb = mt76_connac_mcu_alloc_sta_req(&dev->mt76, &mvif->mt76,
					    &msta->wcid);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	mt76_connac_mcu_sta_ba_tlv(skb, params, enable, true);

	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_EXT_CMD(STA_REC_UPDATE), true);
}

static int
mt7615_mcu_wtbl_rx_ba(struct mt7615_dev *dev,
		      struct ieee80211_ampdu_params *params,
		      bool enable)
{
	struct mt7615_sta *msta = (struct mt7615_sta *)params->sta->drv_priv;
	struct mt7615_vif *mvif = msta->vif;
	struct wtbl_req_hdr *wtbl_hdr;
	struct sk_buff *skb;
	int err;

	skb = mt76_connac_mcu_alloc_sta_req(&dev->mt76, &mvif->mt76,
					    &msta->wcid);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	mt76_connac_mcu_sta_ba_tlv(skb, params, enable, false);

	err = mt76_mcu_skb_send_msg(&dev->mt76, skb,
				    MCU_EXT_CMD(STA_REC_UPDATE), true);
	if (err < 0 || !enable)
		return err;

	skb = NULL;
	wtbl_hdr = mt76_connac_mcu_alloc_wtbl_req(&dev->mt76, &msta->wcid,
						  WTBL_SET, NULL, &skb);
	if (IS_ERR(wtbl_hdr))
		return PTR_ERR(wtbl_hdr);

	mt76_connac_mcu_wtbl_ba_tlv(&dev->mt76, skb, params, enable, false,
				    NULL, wtbl_hdr);

	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_EXT_CMD(WTBL_UPDATE), true);
}

static int
mt7615_mcu_wtbl_sta_add(struct mt7615_phy *phy, struct ieee80211_vif *vif,
			struct ieee80211_sta *sta, bool enable)
{
	struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;
	struct sk_buff *skb, *sskb, *wskb = NULL;
	struct mt7615_dev *dev = phy->dev;
	struct wtbl_req_hdr *wtbl_hdr;
	struct mt7615_sta *msta;
	bool new_entry = true;
	int cmd, err;

	msta = sta ? (struct mt7615_sta *)sta->drv_priv : &mvif->sta;

	sskb = mt76_connac_mcu_alloc_sta_req(&dev->mt76, &mvif->mt76,
					     &msta->wcid);
	if (IS_ERR(sskb))
		return PTR_ERR(sskb);

	if (!sta) {
		if (mvif->sta_added)
			new_entry = false;
		else
			mvif->sta_added = true;
	}
	mt76_connac_mcu_sta_basic_tlv(&dev->mt76, sskb, vif, sta, enable,
				      new_entry);
	if (enable && sta)
		mt76_connac_mcu_sta_tlv(phy->mt76, sskb, sta, vif, 0,
					MT76_STA_INFO_STATE_ASSOC);

	wtbl_hdr = mt76_connac_mcu_alloc_wtbl_req(&dev->mt76, &msta->wcid,
						  WTBL_RESET_AND_SET, NULL,
						  &wskb);
	if (IS_ERR(wtbl_hdr))
		return PTR_ERR(wtbl_hdr);

	if (enable) {
		mt76_connac_mcu_wtbl_generic_tlv(&dev->mt76, wskb, vif, sta,
						 NULL, wtbl_hdr);
		if (sta)
			mt76_connac_mcu_wtbl_ht_tlv(&dev->mt76, wskb, sta,
						    NULL, wtbl_hdr, true, true);
		mt76_connac_mcu_wtbl_hdr_trans_tlv(wskb, vif, &msta->wcid,
						   NULL, wtbl_hdr);
	}

	cmd = enable ? MCU_EXT_CMD(WTBL_UPDATE) : MCU_EXT_CMD(STA_REC_UPDATE);
	skb = enable ? wskb : sskb;

	err = mt76_mcu_skb_send_msg(&dev->mt76, skb, cmd, true);
	if (err < 0) {
		skb = enable ? sskb : wskb;
		dev_kfree_skb(skb);

		return err;
	}

	cmd = enable ? MCU_EXT_CMD(STA_REC_UPDATE) : MCU_EXT_CMD(WTBL_UPDATE);
	skb = enable ? sskb : wskb;

	return mt76_mcu_skb_send_msg(&dev->mt76, skb, cmd, true);
}

static int
mt7615_mcu_wtbl_update_hdr_trans(struct mt7615_dev *dev,
				 struct ieee80211_vif *vif,
				 struct ieee80211_sta *sta)
{
	return mt76_connac_mcu_wtbl_update_hdr_trans(&dev->mt76, vif, sta);
}

static const struct mt7615_mcu_ops wtbl_update_ops = {
	.add_beacon_offload = mt7615_mcu_add_beacon_offload,
	.set_pm_state = mt7615_mcu_ctrl_pm_state,
	.add_dev_info = mt7615_mcu_add_dev,
	.add_bss_info = mt7615_mcu_add_bss,
	.add_tx_ba = mt7615_mcu_wtbl_tx_ba,
	.add_rx_ba = mt7615_mcu_wtbl_rx_ba,
	.sta_add = mt7615_mcu_wtbl_sta_add,
	.set_drv_ctrl = mt7615_mcu_drv_pmctrl,
	.set_fw_ctrl = mt7615_mcu_fw_pmctrl,
	.set_sta_decap_offload = mt7615_mcu_wtbl_update_hdr_trans,
};

static int
mt7615_mcu_sta_ba(struct mt7615_dev *dev,
		  struct ieee80211_ampdu_params *params,
		  bool enable, bool tx)
{
	struct mt7615_sta *msta = (struct mt7615_sta *)params->sta->drv_priv;
	struct mt7615_vif *mvif = msta->vif;
	struct wtbl_req_hdr *wtbl_hdr;
	struct tlv *sta_wtbl;
	struct sk_buff *skb;

	skb = mt76_connac_mcu_alloc_sta_req(&dev->mt76, &mvif->mt76,
					    &msta->wcid);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	mt76_connac_mcu_sta_ba_tlv(skb, params, enable, tx);

	sta_wtbl = mt76_connac_mcu_add_tlv(skb, STA_REC_WTBL, sizeof(struct tlv));

	wtbl_hdr = mt76_connac_mcu_alloc_wtbl_req(&dev->mt76, &msta->wcid,
						  WTBL_SET, sta_wtbl, &skb);
	if (IS_ERR(wtbl_hdr))
		return PTR_ERR(wtbl_hdr);

	mt76_connac_mcu_wtbl_ba_tlv(&dev->mt76, skb, params, enable, tx,
				    sta_wtbl, wtbl_hdr);

	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_EXT_CMD(STA_REC_UPDATE), true);
}

static int
mt7615_mcu_sta_tx_ba(struct mt7615_dev *dev,
		     struct ieee80211_ampdu_params *params,
		     bool enable)
{
	return mt7615_mcu_sta_ba(dev, params, enable, true);
}

static int
mt7615_mcu_sta_rx_ba(struct mt7615_dev *dev,
		     struct ieee80211_ampdu_params *params,
		     bool enable)
{
	return mt7615_mcu_sta_ba(dev, params, enable, false);
}

static int
__mt7615_mcu_add_sta(struct mt76_phy *phy, struct ieee80211_vif *vif,
		     struct ieee80211_sta *sta, bool enable, int cmd,
		     bool offload_fw)
{
	struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;
	struct mt76_sta_cmd_info info = {
		.sta = sta,
		.vif = vif,
		.offload_fw = offload_fw,
		.enable = enable,
		.newly = true,
		.cmd = cmd,
	};

	info.wcid = sta ? (struct mt76_wcid *)sta->drv_priv : &mvif->sta.wcid;
	return mt76_connac_mcu_sta_cmd(phy, &info);
}

static int
mt7615_mcu_add_sta(struct mt7615_phy *phy, struct ieee80211_vif *vif,
		   struct ieee80211_sta *sta, bool enable)
{
	return __mt7615_mcu_add_sta(phy->mt76, vif, sta, enable,
				    MCU_EXT_CMD(STA_REC_UPDATE), false);
}

static int
mt7615_mcu_sta_update_hdr_trans(struct mt7615_dev *dev,
				struct ieee80211_vif *vif,
				struct ieee80211_sta *sta)
{
	struct mt7615_sta *msta = (struct mt7615_sta *)sta->drv_priv;

	return mt76_connac_mcu_sta_update_hdr_trans(&dev->mt76,
						    vif, &msta->wcid,
						    MCU_EXT_CMD(STA_REC_UPDATE));
}

static const struct mt7615_mcu_ops sta_update_ops = {
	.add_beacon_offload = mt7615_mcu_add_beacon_offload,
	.set_pm_state = mt7615_mcu_ctrl_pm_state,
	.add_dev_info = mt7615_mcu_add_dev,
	.add_bss_info = mt7615_mcu_add_bss,
	.add_tx_ba = mt7615_mcu_sta_tx_ba,
	.add_rx_ba = mt7615_mcu_sta_rx_ba,
	.sta_add = mt7615_mcu_add_sta,
	.set_drv_ctrl = mt7615_mcu_drv_pmctrl,
	.set_fw_ctrl = mt7615_mcu_fw_pmctrl,
	.set_sta_decap_offload = mt7615_mcu_sta_update_hdr_trans,
};

static int
mt7615_mcu_uni_ctrl_pm_state(struct mt7615_dev *dev, int band, int state)
{
	return 0;
}

static int
mt7615_mcu_uni_add_beacon_offload(struct mt7615_dev *dev,
				  struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif,
				  bool enable)
{
	struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;
	struct mt76_wcid *wcid = &dev->mt76.global_wcid;
	struct ieee80211_mutable_offsets offs;
	struct {
		struct req_hdr {
			u8 bss_idx;
			u8 pad[3];
		} __packed hdr;
		struct bcn_content_tlv {
			__le16 tag;
			__le16 len;
			__le16 tim_ie_pos;
			__le16 csa_ie_pos;
			__le16 bcc_ie_pos;
			/* 0: disable beacon offload
			 * 1: enable beacon offload
			 * 2: update probe respond offload
			 */
			u8 enable;
			/* 0: legacy format (TXD + payload)
			 * 1: only cap field IE
			 */
			u8 type;
			__le16 pkt_len;
			u8 pkt[512];
		} __packed beacon_tlv;
	} req = {
		.hdr = {
			.bss_idx = mvif->mt76.idx,
		},
		.beacon_tlv = {
			.tag = cpu_to_le16(UNI_BSS_INFO_BCN_CONTENT),
			.len = cpu_to_le16(sizeof(struct bcn_content_tlv)),
			.enable = enable,
		},
	};
	struct sk_buff *skb;

	if (!enable)
		goto out;

	skb = ieee80211_beacon_get_template(mt76_hw(dev), vif, &offs, 0);
	if (!skb)
		return -EINVAL;

	if (skb->len > 512 - MT_TXD_SIZE) {
		dev_err(dev->mt76.dev, "beacon size limit exceed\n");
		dev_kfree_skb(skb);
		return -EINVAL;
	}

	mt7615_mac_write_txwi(dev, (__le32 *)(req.beacon_tlv.pkt), skb,
			      wcid, NULL, 0, NULL, 0, true);
	memcpy(req.beacon_tlv.pkt + MT_TXD_SIZE, skb->data, skb->len);
	req.beacon_tlv.pkt_len = cpu_to_le16(MT_TXD_SIZE + skb->len);
	req.beacon_tlv.tim_ie_pos = cpu_to_le16(MT_TXD_SIZE + offs.tim_offset);

	if (offs.cntdwn_counter_offs[0]) {
		u16 csa_offs;

		csa_offs = MT_TXD_SIZE + offs.cntdwn_counter_offs[0] - 4;
		req.beacon_tlv.csa_ie_pos = cpu_to_le16(csa_offs);
	}
	dev_kfree_skb(skb);

out:
	return mt76_mcu_send_msg(&dev->mt76, MCU_UNI_CMD(BSS_INFO_UPDATE),
				 &req, sizeof(req), true);
}

static int
mt7615_mcu_uni_add_dev(struct mt7615_phy *phy, struct ieee80211_vif *vif,
		       bool enable)
{
	struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;

	return mt76_connac_mcu_uni_add_dev(phy->mt76, vif, &mvif->sta.wcid,
					   enable);
}

static int
mt7615_mcu_uni_add_bss(struct mt7615_phy *phy, struct ieee80211_vif *vif,
		       struct ieee80211_sta *sta, bool enable)
{
	struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;

	return mt76_connac_mcu_uni_add_bss(phy->mt76, vif, &mvif->sta.wcid,
					   enable, NULL);
}

static inline int
mt7615_mcu_uni_add_sta(struct mt7615_phy *phy, struct ieee80211_vif *vif,
		       struct ieee80211_sta *sta, bool enable)
{
	return __mt7615_mcu_add_sta(phy->mt76, vif, sta, enable,
				    MCU_UNI_CMD(STA_REC_UPDATE), true);
}

static int
mt7615_mcu_uni_tx_ba(struct mt7615_dev *dev,
		     struct ieee80211_ampdu_params *params,
		     bool enable)
{
	struct mt7615_sta *sta = (struct mt7615_sta *)params->sta->drv_priv;

	return mt76_connac_mcu_sta_ba(&dev->mt76, &sta->vif->mt76, params,
				      MCU_UNI_CMD(STA_REC_UPDATE), enable,
				      true);
}

static int
mt7615_mcu_uni_rx_ba(struct mt7615_dev *dev,
		     struct ieee80211_ampdu_params *params,
		     bool enable)
{
	struct mt7615_sta *msta = (struct mt7615_sta *)params->sta->drv_priv;
	struct mt7615_vif *mvif = msta->vif;
	struct wtbl_req_hdr *wtbl_hdr;
	struct tlv *sta_wtbl;
	struct sk_buff *skb;
	int err;

	skb = mt76_connac_mcu_alloc_sta_req(&dev->mt76, &mvif->mt76,
					    &msta->wcid);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	mt76_connac_mcu_sta_ba_tlv(skb, params, enable, false);

	err = mt76_mcu_skb_send_msg(&dev->mt76, skb,
				    MCU_UNI_CMD(STA_REC_UPDATE), true);
	if (err < 0 || !enable)
		return err;

	skb = mt76_connac_mcu_alloc_sta_req(&dev->mt76, &mvif->mt76,
					    &msta->wcid);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	sta_wtbl = mt76_connac_mcu_add_tlv(skb, STA_REC_WTBL,
					   sizeof(struct tlv));

	wtbl_hdr = mt76_connac_mcu_alloc_wtbl_req(&dev->mt76, &msta->wcid,
						  WTBL_SET, sta_wtbl, &skb);
	if (IS_ERR(wtbl_hdr))
		return PTR_ERR(wtbl_hdr);

	mt76_connac_mcu_wtbl_ba_tlv(&dev->mt76, skb, params, enable, false,
				    sta_wtbl, wtbl_hdr);

	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_UNI_CMD(STA_REC_UPDATE), true);
}

static int
mt7615_mcu_sta_uni_update_hdr_trans(struct mt7615_dev *dev,
				    struct ieee80211_vif *vif,
				    struct ieee80211_sta *sta)
{
	struct mt7615_sta *msta = (struct mt7615_sta *)sta->drv_priv;

	return mt76_connac_mcu_sta_update_hdr_trans(&dev->mt76,
						    vif, &msta->wcid,
						    MCU_UNI_CMD(STA_REC_UPDATE));
}

static const struct mt7615_mcu_ops uni_update_ops = {
	.add_beacon_offload = mt7615_mcu_uni_add_beacon_offload,
	.set_pm_state = mt7615_mcu_uni_ctrl_pm_state,
	.add_dev_info = mt7615_mcu_uni_add_dev,
	.add_bss_info = mt7615_mcu_uni_add_bss,
	.add_tx_ba = mt7615_mcu_uni_tx_ba,
	.add_rx_ba = mt7615_mcu_uni_rx_ba,
	.sta_add = mt7615_mcu_uni_add_sta,
	.set_drv_ctrl = mt7615_mcu_lp_drv_pmctrl,
	.set_fw_ctrl = mt7615_mcu_fw_pmctrl,
	.set_sta_decap_offload = mt7615_mcu_sta_uni_update_hdr_trans,
};

int mt7615_mcu_restart(struct mt76_dev *dev)
{
	return mt76_mcu_send_msg(dev, MCU_CMD(RESTART_DL_REQ), NULL, 0, true);
}
EXPORT_SYMBOL_GPL(mt7615_mcu_restart);

static int mt7615_load_patch(struct mt7615_dev *dev, u32 addr, const char *name)
{
	const struct mt7615_patch_hdr *hdr;
	const struct firmware *fw = NULL;
	int len, ret, sem;

	ret = firmware_request_nowarn(&fw, name, dev->mt76.dev);
	if (ret)
		return ret;

	if (!fw || !fw->data || fw->size < sizeof(*hdr)) {
		dev_err(dev->mt76.dev, "Invalid firmware\n");
		ret = -EINVAL;
		goto release_fw;
	}

	sem = mt76_connac_mcu_patch_sem_ctrl(&dev->mt76, true);
	switch (sem) {
	case PATCH_IS_DL:
		goto release_fw;
	case PATCH_NOT_DL_SEM_SUCCESS:
		break;
	default:
		dev_err(dev->mt76.dev, "Failed to get patch semaphore\n");
		ret = -EAGAIN;
		goto release_fw;
	}

	hdr = (const struct mt7615_patch_hdr *)(fw->data);

	dev_info(dev->mt76.dev, "HW/SW Version: 0x%x, Build Time: %.16s\n",
		 be32_to_cpu(hdr->hw_sw_ver), hdr->build_date);

	len = fw->size - sizeof(*hdr);

	ret = mt76_connac_mcu_init_download(&dev->mt76, addr, len,
					    DL_MODE_NEED_RSP);
	if (ret) {
		dev_err(dev->mt76.dev, "Download request failed\n");
		goto out;
	}

	ret = mt76_mcu_send_firmware(&dev->mt76, MCU_CMD(FW_SCATTER),
				     fw->data + sizeof(*hdr), len);
	if (ret) {
		dev_err(dev->mt76.dev, "Failed to send firmware to device\n");
		goto out;
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
		break;
	}

release_fw:
	release_firmware(fw);

	return ret;
}

static int
mt7615_mcu_send_ram_firmware(struct mt7615_dev *dev,
			     const struct mt7615_fw_trailer *hdr,
			     const u8 *data, bool is_cr4)
{
	int n_region = is_cr4 ? CR4_REGION_NUM : N9_REGION_NUM;
	int err, i, offset = 0;
	u32 len, addr, mode;

	for (i = 0; i < n_region; i++) {
		mode = mt76_connac_mcu_gen_dl_mode(&dev->mt76,
						   hdr[i].feature_set, is_cr4);
		len = le32_to_cpu(hdr[i].len) + IMG_CRC_LEN;
		addr = le32_to_cpu(hdr[i].addr);

		err = mt76_connac_mcu_init_download(&dev->mt76, addr, len,
						    mode);
		if (err) {
			dev_err(dev->mt76.dev, "Download request failed\n");
			return err;
		}

		err = mt76_mcu_send_firmware(&dev->mt76, MCU_CMD(FW_SCATTER),
					     data + offset, len);
		if (err) {
			dev_err(dev->mt76.dev, "Failed to send firmware to device\n");
			return err;
		}

		offset += len;
	}

	return 0;
}

static int mt7615_load_n9(struct mt7615_dev *dev, const char *name)
{
	const struct mt7615_fw_trailer *hdr;
	const struct firmware *fw;
	int ret;

	ret = request_firmware(&fw, name, dev->mt76.dev);
	if (ret)
		return ret;

	if (!fw || !fw->data || fw->size < N9_REGION_NUM * sizeof(*hdr)) {
		dev_err(dev->mt76.dev, "Invalid firmware\n");
		ret = -EINVAL;
		goto out;
	}

	hdr = (const struct mt7615_fw_trailer *)(fw->data + fw->size -
					N9_REGION_NUM * sizeof(*hdr));

	dev_info(dev->mt76.dev, "N9 Firmware Version: %.10s, Build Time: %.15s\n",
		 hdr->fw_ver, hdr->build_date);

	ret = mt7615_mcu_send_ram_firmware(dev, hdr, fw->data, false);
	if (ret)
		goto out;

	ret = mt76_connac_mcu_start_firmware(&dev->mt76,
					     le32_to_cpu(hdr->addr),
					     FW_START_OVERRIDE);
	if (ret) {
		dev_err(dev->mt76.dev, "Failed to start N9 firmware\n");
		goto out;
	}

	snprintf(dev->mt76.hw->wiphy->fw_version,
		 sizeof(dev->mt76.hw->wiphy->fw_version),
		 "%.10s-%.15s", hdr->fw_ver, hdr->build_date);

	if (!is_mt7615(&dev->mt76)) {
		dev->fw_ver = MT7615_FIRMWARE_V2;
		dev->mcu_ops = &sta_update_ops;
	} else {
		dev->fw_ver = MT7615_FIRMWARE_V1;
		dev->mcu_ops = &wtbl_update_ops;
	}

out:
	release_firmware(fw);
	return ret;
}

static int mt7615_load_cr4(struct mt7615_dev *dev, const char *name)
{
	const struct mt7615_fw_trailer *hdr;
	const struct firmware *fw;
	int ret;

	ret = request_firmware(&fw, name, dev->mt76.dev);
	if (ret)
		return ret;

	if (!fw || !fw->data || fw->size < CR4_REGION_NUM * sizeof(*hdr)) {
		dev_err(dev->mt76.dev, "Invalid firmware\n");
		ret = -EINVAL;
		goto out;
	}

	hdr = (const struct mt7615_fw_trailer *)(fw->data + fw->size -
					CR4_REGION_NUM * sizeof(*hdr));

	dev_info(dev->mt76.dev, "CR4 Firmware Version: %.10s, Build Time: %.15s\n",
		 hdr->fw_ver, hdr->build_date);

	ret = mt7615_mcu_send_ram_firmware(dev, hdr, fw->data, true);
	if (ret)
		goto out;

	ret = mt76_connac_mcu_start_firmware(&dev->mt76, 0,
					     FW_START_WORKING_PDA_CR4);
	if (ret) {
		dev_err(dev->mt76.dev, "Failed to start CR4 firmware\n");
		goto out;
	}

out:
	release_firmware(fw);

	return ret;
}

static int mt7615_load_ram(struct mt7615_dev *dev)
{
	int ret;

	ret = mt7615_load_n9(dev, MT7615_FIRMWARE_N9);
	if (ret)
		return ret;

	return mt7615_load_cr4(dev, MT7615_FIRMWARE_CR4);
}

static int mt7615_load_firmware(struct mt7615_dev *dev)
{
	int ret;
	u32 val;

	val = mt76_get_field(dev, MT_TOP_MISC2, MT_TOP_MISC2_FW_STATE);

	if (val != FW_STATE_FW_DOWNLOAD) {
		dev_err(dev->mt76.dev, "Firmware is not ready for download\n");
		return -EIO;
	}

	ret = mt7615_load_patch(dev, MT7615_PATCH_ADDRESS, MT7615_ROM_PATCH);
	if (ret)
		return ret;

	ret = mt7615_load_ram(dev);
	if (ret)
		return ret;

	if (!mt76_poll_msec(dev, MT_TOP_MISC2, MT_TOP_MISC2_FW_STATE,
			    FIELD_PREP(MT_TOP_MISC2_FW_STATE,
				       FW_STATE_RDY), 500)) {
		dev_err(dev->mt76.dev, "Timeout for initializing firmware\n");
		return -EIO;
	}

	return 0;
}

static int mt7622_load_firmware(struct mt7615_dev *dev)
{
	int ret;
	u32 val;

	mt76_set(dev, MT_WPDMA_GLO_CFG, MT_WPDMA_GLO_CFG_BYPASS_TX_SCH);

	val = mt76_get_field(dev, MT_TOP_OFF_RSV, MT_TOP_OFF_RSV_FW_STATE);
	if (val != FW_STATE_FW_DOWNLOAD) {
		dev_err(dev->mt76.dev, "Firmware is not ready for download\n");
		return -EIO;
	}

	ret = mt7615_load_patch(dev, MT7622_PATCH_ADDRESS, MT7622_ROM_PATCH);
	if (ret)
		return ret;

	ret = mt7615_load_n9(dev, MT7622_FIRMWARE_N9);
	if (ret)
		return ret;

	if (!mt76_poll_msec(dev, MT_TOP_OFF_RSV, MT_TOP_OFF_RSV_FW_STATE,
			    FIELD_PREP(MT_TOP_OFF_RSV_FW_STATE,
				       FW_STATE_NORMAL_TRX), 1500)) {
		dev_err(dev->mt76.dev, "Timeout for initializing firmware\n");
		return -EIO;
	}

	mt76_clear(dev, MT_WPDMA_GLO_CFG, MT_WPDMA_GLO_CFG_BYPASS_TX_SCH);

	return 0;
}

int mt7615_mcu_fw_log_2_host(struct mt7615_dev *dev, u8 ctrl)
{
	struct {
		u8 ctrl_val;
		u8 pad[3];
	} data = {
		.ctrl_val = ctrl
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(FW_LOG_2_HOST),
				 &data, sizeof(data), true);
}

static int mt7615_mcu_cal_cache_apply(struct mt7615_dev *dev)
{
	struct {
		bool cache_enable;
		u8 pad[3];
	} data = {
		.cache_enable = true
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(CAL_CACHE), &data,
				 sizeof(data), false);
}

static int mt7663_load_n9(struct mt7615_dev *dev, const char *name)
{
	u32 offset = 0, override_addr = 0, flag = FW_START_DLYCAL;
	const struct mt76_connac2_fw_trailer *hdr;
	const struct mt7663_fw_buf *buf;
	const struct firmware *fw;
	const u8 *base_addr;
	int i, ret;

	ret = request_firmware(&fw, name, dev->mt76.dev);
	if (ret)
		return ret;

	if (!fw || !fw->data || fw->size < FW_V3_COMMON_TAILER_SIZE) {
		dev_err(dev->mt76.dev, "Invalid firmware\n");
		ret = -EINVAL;
		goto out;
	}

	hdr = (const void *)(fw->data + fw->size - FW_V3_COMMON_TAILER_SIZE);
	dev_info(dev->mt76.dev, "N9 Firmware Version: %.10s, Build Time: %.15s\n",
		 hdr->fw_ver, hdr->build_date);
	dev_info(dev->mt76.dev, "Region number: 0x%x\n", hdr->n_region);

	base_addr = fw->data + fw->size - FW_V3_COMMON_TAILER_SIZE;
	for (i = 0; i < hdr->n_region; i++) {
		u32 shift = (hdr->n_region - i) * FW_V3_REGION_TAILER_SIZE;
		u32 len, addr, mode;

		dev_info(dev->mt76.dev, "Parsing tailer Region: %d\n", i);

		buf = (const struct mt7663_fw_buf *)(base_addr - shift);
		mode = mt76_connac_mcu_gen_dl_mode(&dev->mt76,
						   buf->feature_set, false);
		addr = le32_to_cpu(buf->img_dest_addr);
		len = le32_to_cpu(buf->img_size);

		ret = mt76_connac_mcu_init_download(&dev->mt76, addr, len,
						    mode);
		if (ret) {
			dev_err(dev->mt76.dev, "Download request failed\n");
			goto out;
		}

		ret = mt76_mcu_send_firmware(&dev->mt76, MCU_CMD(FW_SCATTER),
					     fw->data + offset, len);
		if (ret) {
			dev_err(dev->mt76.dev, "Failed to send firmware\n");
			goto out;
		}

		offset += le32_to_cpu(buf->img_size);
		if (buf->feature_set & DL_MODE_VALID_RAM_ENTRY) {
			override_addr = le32_to_cpu(buf->img_dest_addr);
			dev_info(dev->mt76.dev, "Region %d, override_addr = 0x%08x\n",
				 i, override_addr);
		}
	}

	if (override_addr)
		flag |= FW_START_OVERRIDE;

	dev_info(dev->mt76.dev, "override_addr = 0x%08x, option = %d\n",
		 override_addr, flag);

	ret = mt76_connac_mcu_start_firmware(&dev->mt76, override_addr, flag);
	if (ret) {
		dev_err(dev->mt76.dev, "Failed to start N9 firmware\n");
		goto out;
	}

	snprintf(dev->mt76.hw->wiphy->fw_version,
		 sizeof(dev->mt76.hw->wiphy->fw_version),
		 "%.10s-%.15s", hdr->fw_ver, hdr->build_date);

out:
	release_firmware(fw);

	return ret;
}

static int
mt7663_load_rom_patch(struct mt7615_dev *dev, const char **n9_firmware)
{
	const char *selected_rom, *secondary_rom = MT7663_ROM_PATCH;
	const char *primary_rom = MT7663_OFFLOAD_ROM_PATCH;
	int ret;

	if (!prefer_offload_fw) {
		secondary_rom = MT7663_OFFLOAD_ROM_PATCH;
		primary_rom = MT7663_ROM_PATCH;
	}
	selected_rom = primary_rom;

	ret = mt7615_load_patch(dev, MT7663_PATCH_ADDRESS, primary_rom);
	if (ret) {
		dev_info(dev->mt76.dev, "%s not found, switching to %s",
			 primary_rom, secondary_rom);
		ret = mt7615_load_patch(dev, MT7663_PATCH_ADDRESS,
					secondary_rom);
		if (ret) {
			dev_err(dev->mt76.dev, "failed to load %s",
				secondary_rom);
			return ret;
		}
		selected_rom = secondary_rom;
	}

	if (!strcmp(selected_rom, MT7663_OFFLOAD_ROM_PATCH)) {
		*n9_firmware = MT7663_OFFLOAD_FIRMWARE_N9;
		dev->fw_ver = MT7615_FIRMWARE_V3;
		dev->mcu_ops = &uni_update_ops;
	} else {
		*n9_firmware = MT7663_FIRMWARE_N9;
		dev->fw_ver = MT7615_FIRMWARE_V2;
		dev->mcu_ops = &sta_update_ops;
	}

	return 0;
}

int __mt7663_load_firmware(struct mt7615_dev *dev)
{
	const char *n9_firmware;
	int ret;

	ret = mt76_get_field(dev, MT_CONN_ON_MISC, MT_TOP_MISC2_FW_N9_RDY);
	if (ret) {
		dev_dbg(dev->mt76.dev, "Firmware is already download\n");
		return -EIO;
	}

	ret = mt7663_load_rom_patch(dev, &n9_firmware);
	if (ret)
		return ret;

	ret = mt7663_load_n9(dev, n9_firmware);
	if (ret)
		return ret;

	if (!mt76_poll_msec(dev, MT_CONN_ON_MISC, MT_TOP_MISC2_FW_N9_RDY,
			    MT_TOP_MISC2_FW_N9_RDY, 1500)) {
		ret = mt76_get_field(dev, MT_CONN_ON_MISC,
				     MT7663_TOP_MISC2_FW_STATE);
		dev_err(dev->mt76.dev, "Timeout for initializing firmware\n");
		return -EIO;
	}

#ifdef CONFIG_PM
	if (mt7615_firmware_offload(dev))
		dev->mt76.hw->wiphy->wowlan = &mt76_connac_wowlan_support;
#endif /* CONFIG_PM */

	dev_dbg(dev->mt76.dev, "Firmware init done\n");

	return 0;
}
EXPORT_SYMBOL_GPL(__mt7663_load_firmware);

static int mt7663_load_firmware(struct mt7615_dev *dev)
{
	int ret;

	mt76_set(dev, MT_WPDMA_GLO_CFG, MT_WPDMA_GLO_CFG_BYPASS_TX_SCH);

	ret = __mt7663_load_firmware(dev);
	if (ret)
		return ret;

	mt76_clear(dev, MT_WPDMA_GLO_CFG, MT_WPDMA_GLO_CFG_BYPASS_TX_SCH);

	return 0;
}

int mt7615_mcu_init(struct mt7615_dev *dev)
{
	static const struct mt76_mcu_ops mt7615_mcu_ops = {
		.headroom = sizeof(struct mt7615_mcu_txd),
		.mcu_skb_send_msg = mt7615_mcu_send_message,
		.mcu_parse_response = mt7615_mcu_parse_response,
	};
	int ret;

	dev->mt76.mcu_ops = &mt7615_mcu_ops,

	ret = mt7615_mcu_drv_pmctrl(dev);
	if (ret)
		return ret;

	switch (mt76_chip(&dev->mt76)) {
	case 0x7622:
		ret = mt7622_load_firmware(dev);
		break;
	case 0x7663:
		ret = mt7663_load_firmware(dev);
		break;
	default:
		ret = mt7615_load_firmware(dev);
		break;
	}
	if (ret)
		return ret;

	mt76_queue_tx_cleanup(dev, dev->mt76.q_mcu[MT_MCUQ_FWDL], false);
	dev_dbg(dev->mt76.dev, "Firmware init done\n");
	set_bit(MT76_STATE_MCU_RUNNING, &dev->mphy.state);

	if (dev->dbdc_support) {
		ret = mt7615_mcu_cal_cache_apply(dev);
		if (ret)
			return ret;
	}

	return mt7615_mcu_fw_log_2_host(dev, 0);
}
EXPORT_SYMBOL_GPL(mt7615_mcu_init);

void mt7615_mcu_exit(struct mt7615_dev *dev)
{
	mt7615_mcu_restart(&dev->mt76);
	mt7615_mcu_set_fw_ctrl(dev);
	skb_queue_purge(&dev->mt76.mcu.res_q);
}
EXPORT_SYMBOL_GPL(mt7615_mcu_exit);

int mt7615_mcu_set_eeprom(struct mt7615_dev *dev)
{
	struct {
		u8 buffer_mode;
		u8 content_format;
		__le16 len;
	} __packed req_hdr = {
		.buffer_mode = 1,
	};
	u8 *eep = (u8 *)dev->mt76.eeprom.data;
	struct sk_buff *skb;
	int eep_len, offset;

	switch (mt76_chip(&dev->mt76)) {
	case 0x7622:
		eep_len = MT7622_EE_MAX - MT_EE_NIC_CONF_0;
		offset = MT_EE_NIC_CONF_0;
		break;
	case 0x7663:
		eep_len = MT7663_EE_MAX - MT_EE_CHIP_ID;
		req_hdr.content_format = 1;
		offset = MT_EE_CHIP_ID;
		break;
	default:
		eep_len = MT7615_EE_MAX - MT_EE_NIC_CONF_0;
		offset = MT_EE_NIC_CONF_0;
		break;
	}

	req_hdr.len = cpu_to_le16(eep_len);

	skb = mt76_mcu_msg_alloc(&dev->mt76, NULL, sizeof(req_hdr) + eep_len);
	if (!skb)
		return -ENOMEM;

	skb_put_data(skb, &req_hdr, sizeof(req_hdr));
	skb_put_data(skb, eep + offset, eep_len);

	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_EXT_CMD(EFUSE_BUFFER_MODE), true);
}

int mt7615_mcu_set_wmm(struct mt7615_dev *dev, u8 queue,
		       const struct ieee80211_tx_queue_params *params)
{
#define WMM_AIFS_SET	BIT(0)
#define WMM_CW_MIN_SET	BIT(1)
#define WMM_CW_MAX_SET	BIT(2)
#define WMM_TXOP_SET	BIT(3)
#define WMM_PARAM_SET	(WMM_AIFS_SET | WMM_CW_MIN_SET | \
			 WMM_CW_MAX_SET | WMM_TXOP_SET)
	struct req_data {
		u8 number;
		u8 rsv[3];
		u8 queue;
		u8 valid;
		u8 aifs;
		u8 cw_min;
		__le16 cw_max;
		__le16 txop;
	} __packed req = {
		.number = 1,
		.queue = queue,
		.valid = WMM_PARAM_SET,
		.aifs = params->aifs,
		.cw_min = 5,
		.cw_max = cpu_to_le16(10),
		.txop = cpu_to_le16(params->txop),
	};

	if (params->cw_min)
		req.cw_min = fls(params->cw_min);
	if (params->cw_max)
		req.cw_max = cpu_to_le16(fls(params->cw_max));

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(EDCA_UPDATE),
				 &req, sizeof(req), true);
}

int mt7615_mcu_set_dbdc(struct mt7615_dev *dev)
{
	struct mt7615_phy *ext_phy = mt7615_ext_phy(dev);
	struct dbdc_entry {
		u8 type;
		u8 index;
		u8 band;
		u8 _rsv;
	};
	struct {
		u8 enable;
		u8 num;
		u8 _rsv[2];
		struct dbdc_entry entry[64];
	} req = {
		.enable = !!ext_phy,
	};
	int i;

	if (!ext_phy)
		goto out;

#define ADD_DBDC_ENTRY(_type, _idx, _band)		\
	do { \
		req.entry[req.num].type = _type;		\
		req.entry[req.num].index = _idx;		\
		req.entry[req.num++].band = _band;		\
	} while (0)

	for (i = 0; i < 4; i++) {
		bool band = !!(ext_phy->omac_mask & BIT_ULL(i));

		ADD_DBDC_ENTRY(DBDC_TYPE_BSS, i, band);
	}

	for (i = 0; i < 14; i++) {
		bool band = !!(ext_phy->omac_mask & BIT_ULL(0x11 + i));

		ADD_DBDC_ENTRY(DBDC_TYPE_MBSS, i, band);
	}

	ADD_DBDC_ENTRY(DBDC_TYPE_MU, 0, 1);

	for (i = 0; i < 3; i++)
		ADD_DBDC_ENTRY(DBDC_TYPE_BF, i, 1);

	ADD_DBDC_ENTRY(DBDC_TYPE_WMM, 0, 0);
	ADD_DBDC_ENTRY(DBDC_TYPE_WMM, 1, 0);
	ADD_DBDC_ENTRY(DBDC_TYPE_WMM, 2, 1);
	ADD_DBDC_ENTRY(DBDC_TYPE_WMM, 3, 1);

	ADD_DBDC_ENTRY(DBDC_TYPE_MGMT, 0, 0);
	ADD_DBDC_ENTRY(DBDC_TYPE_MGMT, 1, 1);

out:
	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(DBDC_CTRL), &req,
				 sizeof(req), true);
}

int mt7615_mcu_del_wtbl_all(struct mt7615_dev *dev)
{
	struct wtbl_req_hdr req = {
		.operation = WTBL_RESET_ALL,
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(WTBL_UPDATE),
				 &req, sizeof(req), true);
}

int mt7615_mcu_set_fcc5_lpn(struct mt7615_dev *dev, int val)
{
	struct {
		__le16 tag;
		__le16 min_lpn;
	} req = {
		.tag = cpu_to_le16(0x1),
		.min_lpn = cpu_to_le16(val),
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(SET_RADAR_TH),
				 &req, sizeof(req), true);
}

int mt7615_mcu_set_pulse_th(struct mt7615_dev *dev,
			    const struct mt7615_dfs_pulse *pulse)
{
	struct {
		__le16 tag;
		__le32 max_width;	/* us */
		__le32 max_pwr;		/* dbm */
		__le32 min_pwr;		/* dbm */
		__le32 min_stgr_pri;	/* us */
		__le32 max_stgr_pri;	/* us */
		__le32 min_cr_pri;	/* us */
		__le32 max_cr_pri;	/* us */
	} req = {
		.tag = cpu_to_le16(0x3),
#define __req_field(field) .field = cpu_to_le32(pulse->field)
		__req_field(max_width),
		__req_field(max_pwr),
		__req_field(min_pwr),
		__req_field(min_stgr_pri),
		__req_field(max_stgr_pri),
		__req_field(min_cr_pri),
		__req_field(max_cr_pri),
#undef  __req_field
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(SET_RADAR_TH),
				 &req, sizeof(req), true);
}

int mt7615_mcu_set_radar_th(struct mt7615_dev *dev, int index,
			    const struct mt7615_dfs_pattern *pattern)
{
	struct {
		__le16 tag;
		__le16 radar_type;
		u8 enb;
		u8 stgr;
		u8 min_crpn;
		u8 max_crpn;
		u8 min_crpr;
		u8 min_pw;
		u8 max_pw;
		__le32 min_pri;
		__le32 max_pri;
		u8 min_crbn;
		u8 max_crbn;
		u8 min_stgpn;
		u8 max_stgpn;
		u8 min_stgpr;
	} req = {
		.tag = cpu_to_le16(0x2),
		.radar_type = cpu_to_le16(index),
#define __req_field_u8(field) .field = pattern->field
#define __req_field_u32(field) .field = cpu_to_le32(pattern->field)
		__req_field_u8(enb),
		__req_field_u8(stgr),
		__req_field_u8(min_crpn),
		__req_field_u8(max_crpn),
		__req_field_u8(min_crpr),
		__req_field_u8(min_pw),
		__req_field_u8(max_pw),
		__req_field_u32(min_pri),
		__req_field_u32(max_pri),
		__req_field_u8(min_crbn),
		__req_field_u8(max_crbn),
		__req_field_u8(min_stgpn),
		__req_field_u8(max_stgpn),
		__req_field_u8(min_stgpr),
#undef __req_field_u8
#undef __req_field_u32
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(SET_RADAR_TH),
				 &req, sizeof(req), true);
}

int mt7615_mcu_rdd_send_pattern(struct mt7615_dev *dev)
{
	struct {
		u8 pulse_num;
		u8 rsv[3];
		struct {
			__le32 start_time;
			__le16 width;
			__le16 power;
		} pattern[32];
	} req = {
		.pulse_num = dev->radar_pattern.n_pulses,
	};
	u32 start_time = ktime_to_ms(ktime_get_boottime());
	int i;

	if (dev->radar_pattern.n_pulses > ARRAY_SIZE(req.pattern))
		return -EINVAL;

	/* TODO: add some noise here */
	for (i = 0; i < dev->radar_pattern.n_pulses; i++) {
		u32 ts = start_time + i * dev->radar_pattern.period;

		req.pattern[i].width = cpu_to_le16(dev->radar_pattern.width);
		req.pattern[i].power = cpu_to_le16(dev->radar_pattern.power);
		req.pattern[i].start_time = cpu_to_le32(ts);
	}

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(SET_RDD_PATTERN),
				 &req, sizeof(req), false);
}

static void mt7615_mcu_set_txpower_sku(struct mt7615_phy *phy, u8 *sku)
{
	struct mt76_phy *mphy = phy->mt76;
	struct ieee80211_hw *hw = mphy->hw;
	struct mt76_power_limits limits;
	s8 *limits_array = (s8 *)&limits;
	int n_chains = hweight8(mphy->antenna_mask);
	int tx_power = hw->conf.power_level * 2;
	int i;
	static const u8 sku_mapping[] = {
#define SKU_FIELD(_type, _field) \
		[MT_SKU_##_type] = offsetof(struct mt76_power_limits, _field)
		SKU_FIELD(CCK_1_2, cck[0]),
		SKU_FIELD(CCK_55_11, cck[2]),
		SKU_FIELD(OFDM_6_9, ofdm[0]),
		SKU_FIELD(OFDM_12_18, ofdm[2]),
		SKU_FIELD(OFDM_24_36, ofdm[4]),
		SKU_FIELD(OFDM_48, ofdm[6]),
		SKU_FIELD(OFDM_54, ofdm[7]),
		SKU_FIELD(HT20_0_8, mcs[0][0]),
		SKU_FIELD(HT20_32, ofdm[0]),
		SKU_FIELD(HT20_1_2_9_10, mcs[0][1]),
		SKU_FIELD(HT20_3_4_11_12, mcs[0][3]),
		SKU_FIELD(HT20_5_13, mcs[0][5]),
		SKU_FIELD(HT20_6_14, mcs[0][6]),
		SKU_FIELD(HT20_7_15, mcs[0][7]),
		SKU_FIELD(HT40_0_8, mcs[1][0]),
		SKU_FIELD(HT40_32, ofdm[0]),
		SKU_FIELD(HT40_1_2_9_10, mcs[1][1]),
		SKU_FIELD(HT40_3_4_11_12, mcs[1][3]),
		SKU_FIELD(HT40_5_13, mcs[1][5]),
		SKU_FIELD(HT40_6_14, mcs[1][6]),
		SKU_FIELD(HT40_7_15, mcs[1][7]),
		SKU_FIELD(VHT20_0, mcs[0][0]),
		SKU_FIELD(VHT20_1_2, mcs[0][1]),
		SKU_FIELD(VHT20_3_4, mcs[0][3]),
		SKU_FIELD(VHT20_5_6, mcs[0][5]),
		SKU_FIELD(VHT20_7, mcs[0][7]),
		SKU_FIELD(VHT20_8, mcs[0][8]),
		SKU_FIELD(VHT20_9, mcs[0][9]),
		SKU_FIELD(VHT40_0, mcs[1][0]),
		SKU_FIELD(VHT40_1_2, mcs[1][1]),
		SKU_FIELD(VHT40_3_4, mcs[1][3]),
		SKU_FIELD(VHT40_5_6, mcs[1][5]),
		SKU_FIELD(VHT40_7, mcs[1][7]),
		SKU_FIELD(VHT40_8, mcs[1][8]),
		SKU_FIELD(VHT40_9, mcs[1][9]),
		SKU_FIELD(VHT80_0, mcs[2][0]),
		SKU_FIELD(VHT80_1_2, mcs[2][1]),
		SKU_FIELD(VHT80_3_4, mcs[2][3]),
		SKU_FIELD(VHT80_5_6, mcs[2][5]),
		SKU_FIELD(VHT80_7, mcs[2][7]),
		SKU_FIELD(VHT80_8, mcs[2][8]),
		SKU_FIELD(VHT80_9, mcs[2][9]),
		SKU_FIELD(VHT160_0, mcs[3][0]),
		SKU_FIELD(VHT160_1_2, mcs[3][1]),
		SKU_FIELD(VHT160_3_4, mcs[3][3]),
		SKU_FIELD(VHT160_5_6, mcs[3][5]),
		SKU_FIELD(VHT160_7, mcs[3][7]),
		SKU_FIELD(VHT160_8, mcs[3][8]),
		SKU_FIELD(VHT160_9, mcs[3][9]),
#undef SKU_FIELD
	};

	tx_power = mt76_get_sar_power(mphy, mphy->chandef.chan, tx_power);
	tx_power -= mt76_tx_power_nss_delta(n_chains);
	tx_power = mt76_get_rate_power_limits(mphy, mphy->chandef.chan,
					      &limits, tx_power);
	mphy->txpower_cur = tx_power;

	if (is_mt7663(mphy->dev)) {
		memset(sku, tx_power, MT_SKU_4SS_DELTA + 1);
		return;
	}

	for (i = 0; i < MT_SKU_1SS_DELTA; i++)
		sku[i] = limits_array[sku_mapping[i]];

	for (i = 0; i < 4; i++) {
		int delta = 0;

		if (i < n_chains - 1)
			delta = mt76_tx_power_nss_delta(n_chains) -
				mt76_tx_power_nss_delta(i + 1);
		sku[MT_SKU_1SS_DELTA + i] = delta;
	}
}

static u8 mt7615_mcu_chan_bw(struct cfg80211_chan_def *chandef)
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

int mt7615_mcu_set_chan_info(struct mt7615_phy *phy, int cmd)
{
	struct mt7615_dev *dev = phy->dev;
	struct cfg80211_chan_def *chandef = &phy->mt76->chandef;
	int freq1 = chandef->center_freq1, freq2 = chandef->center_freq2;
	struct {
		u8 control_chan;
		u8 center_chan;
		u8 bw;
		u8 tx_streams;
		u8 rx_streams_mask;
		u8 switch_reason;
		u8 band_idx;
		/* for 80+80 only */
		u8 center_chan2;
		__le16 cac_case;
		u8 channel_band;
		u8 rsv0;
		__le32 outband_freq;
		u8 txpower_drop;
		u8 rsv1[3];
		u8 txpower_sku[53];
		u8 rsv2[3];
	} req = {
		.control_chan = chandef->chan->hw_value,
		.center_chan = ieee80211_frequency_to_channel(freq1),
		.tx_streams = hweight8(phy->mt76->antenna_mask),
		.rx_streams_mask = phy->mt76->chainmask,
		.center_chan2 = ieee80211_frequency_to_channel(freq2),
	};

	if (cmd == MCU_EXT_CMD(SET_RX_PATH) ||
	    dev->mt76.hw->conf.flags & IEEE80211_CONF_MONITOR)
		req.switch_reason = CH_SWITCH_NORMAL;
	else if (phy->mt76->hw->conf.flags & IEEE80211_CONF_OFFCHANNEL)
		req.switch_reason = CH_SWITCH_SCAN_BYPASS_DPD;
	else if (!cfg80211_reg_can_beacon(phy->mt76->hw->wiphy, chandef,
					  NL80211_IFTYPE_AP))
		req.switch_reason = CH_SWITCH_DFS;
	else
		req.switch_reason = CH_SWITCH_NORMAL;

	req.band_idx = phy != &dev->phy;
	req.bw = mt7615_mcu_chan_bw(chandef);

	if (mt76_testmode_enabled(phy->mt76))
		memset(req.txpower_sku, 0x3f, 49);
	else
		mt7615_mcu_set_txpower_sku(phy, req.txpower_sku);

	return mt76_mcu_send_msg(&dev->mt76, cmd, &req, sizeof(req), true);
}

int mt7615_mcu_get_temperature(struct mt7615_dev *dev)
{
	struct {
		u8 action;
		u8 rsv[3];
	} req = {};

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(THERMAL_CTRL),
				 &req, sizeof(req), true);
}

int mt7615_mcu_set_test_param(struct mt7615_dev *dev, u8 param, bool test_mode,
			      u32 val)
{
	struct {
		u8 test_mode_en;
		u8 param_idx;
		u8 _rsv[2];

		__le32 value;

		u8 pad[8];
	} req = {
		.test_mode_en = test_mode,
		.param_idx = param,
		.value = cpu_to_le32(val),
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(ATE_CTRL),
				 &req, sizeof(req), false);
}

int mt7615_mcu_set_sku_en(struct mt7615_phy *phy, bool enable)
{
	struct mt7615_dev *dev = phy->dev;
	struct {
		u8 format_id;
		u8 sku_enable;
		u8 band_idx;
		u8 rsv;
	} req = {
		.format_id = 0,
		.band_idx = phy != &dev->phy,
		.sku_enable = enable,
	};

	return mt76_mcu_send_msg(&dev->mt76,
				 MCU_EXT_CMD(TX_POWER_FEATURE_CTRL),
				 &req, sizeof(req), true);
}

static int mt7615_find_freq_idx(const u16 *freqs, int n_freqs, u16 cur)
{
	int i;

	for (i = 0; i < n_freqs; i++)
		if (cur == freqs[i])
			return i;

	return -1;
}

static int mt7615_dcoc_freq_idx(u16 freq, u8 bw)
{
	static const u16 freq_list[] = {
		4980, 5805, 5905, 5190,
		5230, 5270, 5310, 5350,
		5390, 5430, 5470, 5510,
		5550, 5590, 5630, 5670,
		5710, 5755, 5795, 5835,
		5875, 5210, 5290, 5370,
		5450, 5530, 5610, 5690,
		5775, 5855
	};
	static const u16 freq_bw40[] = {
		5190, 5230, 5270, 5310,
		5350, 5390, 5430, 5470,
		5510, 5550, 5590, 5630,
		5670, 5710, 5755, 5795,
		5835, 5875
	};
	int offset_2g = ARRAY_SIZE(freq_list);
	int idx;

	if (freq < 4000) {
		if (freq < 2427)
			return offset_2g;
		if (freq < 2442)
			return offset_2g + 1;
		if (freq < 2457)
			return offset_2g + 2;

		return offset_2g + 3;
	}

	switch (bw) {
	case NL80211_CHAN_WIDTH_80:
	case NL80211_CHAN_WIDTH_80P80:
	case NL80211_CHAN_WIDTH_160:
		break;
	default:
		idx = mt7615_find_freq_idx(freq_bw40, ARRAY_SIZE(freq_bw40),
					   freq + 10);
		if (idx >= 0) {
			freq = freq_bw40[idx];
			break;
		}

		idx = mt7615_find_freq_idx(freq_bw40, ARRAY_SIZE(freq_bw40),
					   freq - 10);
		if (idx >= 0) {
			freq = freq_bw40[idx];
			break;
		}
		fallthrough;
	case NL80211_CHAN_WIDTH_40:
		idx = mt7615_find_freq_idx(freq_bw40, ARRAY_SIZE(freq_bw40),
					   freq);
		if (idx >= 0)
			break;

		return -1;

	}

	return mt7615_find_freq_idx(freq_list, ARRAY_SIZE(freq_list), freq);
}

int mt7615_mcu_apply_rx_dcoc(struct mt7615_phy *phy)
{
	struct mt7615_dev *dev = phy->dev;
	struct cfg80211_chan_def *chandef = &phy->mt76->chandef;
	int freq2 = chandef->center_freq2;
	int ret;
	struct {
		u8 direction;
		u8 runtime_calibration;
		u8 _rsv[2];

		__le16 center_freq;
		u8 bw;
		u8 band;
		u8 is_freq2;
		u8 success;
		u8 dbdc_en;

		u8 _rsv2;

		struct {
			__le32 sx0_i_lna[4];
			__le32 sx0_q_lna[4];

			__le32 sx2_i_lna[4];
			__le32 sx2_q_lna[4];
		} dcoc_data[4];
	} req = {
		.direction = 1,

		.bw = mt7615_mcu_chan_bw(chandef),
		.band = chandef->center_freq1 > 4000,
		.dbdc_en = !!dev->mt76.phys[MT_BAND1],
	};
	u16 center_freq = chandef->center_freq1;
	int freq_idx;
	u8 *eep = dev->mt76.eeprom.data;

	if (!(eep[MT_EE_CALDATA_FLASH] & MT_EE_CALDATA_FLASH_RX_CAL))
		return 0;

	if (chandef->width == NL80211_CHAN_WIDTH_160) {
		freq2 = center_freq + 40;
		center_freq -= 40;
	}

again:
	req.runtime_calibration = 1;
	freq_idx = mt7615_dcoc_freq_idx(center_freq, chandef->width);
	if (freq_idx < 0)
		goto out;

	memcpy(req.dcoc_data, eep + MT7615_EEPROM_DCOC_OFFSET +
			      freq_idx * MT7615_EEPROM_DCOC_SIZE,
	       sizeof(req.dcoc_data));
	req.runtime_calibration = 0;

out:
	req.center_freq = cpu_to_le16(center_freq);
	ret = mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(RXDCOC_CAL), &req,
				sizeof(req), true);

	if ((chandef->width == NL80211_CHAN_WIDTH_80P80 ||
	     chandef->width == NL80211_CHAN_WIDTH_160) && !req.is_freq2) {
		req.is_freq2 = true;
		center_freq = freq2;
		goto again;
	}

	return ret;
}

static int mt7615_dpd_freq_idx(u16 freq, u8 bw)
{
	static const u16 freq_list[] = {
		4920, 4940, 4960, 4980,
		5040, 5060, 5080, 5180,
		5200, 5220, 5240, 5260,
		5280, 5300, 5320, 5340,
		5360, 5380, 5400, 5420,
		5440, 5460, 5480, 5500,
		5520, 5540, 5560, 5580,
		5600, 5620, 5640, 5660,
		5680, 5700, 5720, 5745,
		5765, 5785, 5805, 5825,
		5845, 5865, 5885, 5905
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

	if (bw != NL80211_CHAN_WIDTH_20) {
		idx = mt7615_find_freq_idx(freq_list, ARRAY_SIZE(freq_list),
					   freq + 10);
		if (idx >= 0)
			return idx;

		idx = mt7615_find_freq_idx(freq_list, ARRAY_SIZE(freq_list),
					   freq - 10);
		if (idx >= 0)
			return idx;
	}

	return mt7615_find_freq_idx(freq_list, ARRAY_SIZE(freq_list), freq);
}


int mt7615_mcu_apply_tx_dpd(struct mt7615_phy *phy)
{
	struct mt7615_dev *dev = phy->dev;
	struct cfg80211_chan_def *chandef = &phy->mt76->chandef;
	int freq2 = chandef->center_freq2;
	int ret;
	struct {
		u8 direction;
		u8 runtime_calibration;
		u8 _rsv[2];

		__le16 center_freq;
		u8 bw;
		u8 band;
		u8 is_freq2;
		u8 success;
		u8 dbdc_en;

		u8 _rsv2;

		struct {
			struct {
				u32 dpd_g0;
				u8 data[32];
			} wf0, wf1;

			struct {
				u32 dpd_g0_prim;
				u32 dpd_g0_sec;
				u8 data_prim[32];
				u8 data_sec[32];
			} wf2, wf3;
		} dpd_data;
	} req = {
		.direction = 1,

		.bw = mt7615_mcu_chan_bw(chandef),
		.band = chandef->center_freq1 > 4000,
		.dbdc_en = !!dev->mt76.phys[MT_BAND1],
	};
	u16 center_freq = chandef->center_freq1;
	int freq_idx;
	u8 *eep = dev->mt76.eeprom.data;

	if (!(eep[MT_EE_CALDATA_FLASH] & MT_EE_CALDATA_FLASH_TX_DPD))
		return 0;

	if (chandef->width == NL80211_CHAN_WIDTH_160) {
		freq2 = center_freq + 40;
		center_freq -= 40;
	}

again:
	req.runtime_calibration = 1;
	freq_idx = mt7615_dpd_freq_idx(center_freq, chandef->width);
	if (freq_idx < 0)
		goto out;

	memcpy(&req.dpd_data, eep + MT7615_EEPROM_TXDPD_OFFSET +
			      freq_idx * MT7615_EEPROM_TXDPD_SIZE,
	       sizeof(req.dpd_data));
	req.runtime_calibration = 0;

out:
	req.center_freq = cpu_to_le16(center_freq);
	ret = mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(TXDPD_CAL),
				&req, sizeof(req), true);

	if ((chandef->width == NL80211_CHAN_WIDTH_80P80 ||
	     chandef->width == NL80211_CHAN_WIDTH_160) && !req.is_freq2) {
		req.is_freq2 = true;
		center_freq = freq2;
		goto again;
	}

	return ret;
}

int mt7615_mcu_set_rx_hdr_trans_blacklist(struct mt7615_dev *dev)
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

int mt7615_mcu_set_bss_pm(struct mt7615_dev *dev, struct ieee80211_vif *vif,
			  bool enable)
{
	struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;
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
		.aid = cpu_to_le16(vif->cfg.aid),
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

	err = mt76_mcu_send_msg(&dev->mt76, MCU_CE_CMD(SET_BSS_ABORT),
				&req_hdr, sizeof(req_hdr), false);
	if (err < 0 || !enable)
		return err;

	return mt76_mcu_send_msg(&dev->mt76, MCU_CE_CMD(SET_BSS_CONNECTED),
				 &req, sizeof(req), false);
}

int mt7615_mcu_set_roc(struct mt7615_phy *phy, struct ieee80211_vif *vif,
		       struct ieee80211_channel *chan, int duration)
{
	struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;
	struct mt7615_dev *dev = phy->dev;
	struct mt7615_roc_tlv req = {
		.bss_idx = mvif->mt76.idx,
		.active = !chan,
		.max_interval = cpu_to_le32(duration),
		.primary_chan = chan ? chan->hw_value : 0,
		.band = chan ? chan->band : 0,
		.req_type = 2,
	};

	phy->roc_grant = false;

	return mt76_mcu_send_msg(&dev->mt76, MCU_CE_CMD(SET_ROC),
				 &req, sizeof(req), false);
}
