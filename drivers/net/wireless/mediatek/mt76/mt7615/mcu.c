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

struct mt7663_fw_trailer {
	u8 chip_id;
	u8 eco_code;
	u8 n_region;
	u8 format_ver;
	u8 format_flag;
	u8 reserv[2];
	char fw_ver[10];
	char build_date[15];
	__le32 crc;
} __packed;

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

#define FW_FEATURE_SET_ENCRYPT		BIT(0)
#define FW_FEATURE_SET_KEY_IDX		GENMASK(2, 1)

#define DL_MODE_ENCRYPT			BIT(0)
#define DL_MODE_KEY_IDX			GENMASK(2, 1)
#define DL_MODE_RESET_SEC_IV		BIT(3)
#define DL_MODE_WORKING_PDA_CR4		BIT(4)
#define DL_MODE_VALID_RAM_ENTRY         BIT(5)
#define DL_MODE_NEED_RSP		BIT(31)

#define FW_START_OVERRIDE		BIT(0)
#define FW_START_WORKING_PDA_CR4	BIT(2)

void mt7615_mcu_fill_msg(struct mt7615_dev *dev, struct sk_buff *skb,
			 int cmd, int *wait_seq)
{
	int txd_len, mcu_cmd = cmd & MCU_CMD_MASK;
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

	txd_len = cmd & MCU_UNI_PREFIX ? sizeof(*uni_txd) : sizeof(*mcu_txd);
	txd = (__le32 *)skb_push(skb, txd_len);

	if (cmd != MCU_CMD_FW_SCATTER) {
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

	if (cmd & MCU_UNI_PREFIX) {
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
		if (cmd & MCU_QUERY_PREFIX)
			mcu_txd->set_query = MCU_Q_QUERY;
		else
			mcu_txd->set_query = MCU_Q_SET;
		mcu_txd->ext_cid = mcu_cmd;
		mcu_txd->ext_cid_ack = 1;
		break;
	}
}
EXPORT_SYMBOL_GPL(mt7615_mcu_fill_msg);

int mt7615_mcu_parse_response(struct mt76_dev *mdev, int cmd,
			      struct sk_buff *skb, int seq)
{
	struct mt7615_mcu_rxd *rxd;
	int ret = 0;

	if (!skb) {
		dev_err(mdev->dev, "Message %ld (seq %d) timeout\n",
			cmd & MCU_CMD_MASK, seq);
		return -ETIMEDOUT;
	}

	rxd = (struct mt7615_mcu_rxd *)skb->data;
	if (seq != rxd->seq)
		return -EAGAIN;

	switch (cmd) {
	case MCU_CMD_PATCH_SEM_CONTROL:
		skb_pull(skb, sizeof(*rxd) - 4);
		ret = *skb->data;
		break;
	case MCU_EXT_CMD_GET_TEMP:
		skb_pull(skb, sizeof(*rxd));
		ret = le32_to_cpu(*(__le32 *)skb->data);
		break;
	case MCU_EXT_CMD_RF_REG_ACCESS | MCU_QUERY_PREFIX:
		skb_pull(skb, sizeof(*rxd));
		ret = le32_to_cpu(*(__le32 *)&skb->data[8]);
		break;
	case MCU_UNI_CMD_DEV_INFO_UPDATE:
	case MCU_UNI_CMD_BSS_INFO_UPDATE:
	case MCU_UNI_CMD_STA_REC_UPDATE:
	case MCU_UNI_CMD_HIF_CTRL:
	case MCU_UNI_CMD_OFFLOAD:
	case MCU_UNI_CMD_SUSPEND: {
		struct mt7615_mcu_uni_event *event;

		skb_pull(skb, sizeof(*rxd));
		event = (struct mt7615_mcu_uni_event *)skb->data;
		ret = le32_to_cpu(event->status);
		break;
	}
	case MCU_CMD_REG_READ: {
		struct mt7615_mcu_reg_event *event;

		skb_pull(skb, sizeof(*rxd));
		event = (struct mt7615_mcu_reg_event *)skb->data;
		ret = (int)le32_to_cpu(event->val);
		break;
	}
	default:
		break;
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

	return mt76_mcu_send_msg(&dev->mt76,
				 MCU_EXT_CMD_RF_REG_ACCESS | MCU_QUERY_PREFIX,
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

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_RF_REG_ACCESS, &req,
				 sizeof(req), false);
}

static void mt7622_trigger_hif_int(struct mt7615_dev *dev, bool en)
{
	if (!is_mt7622(&dev->mt76))
		return;

	regmap_update_bits(dev->infracfg, MT_INFRACFG_MISC,
			   MT_INFRACFG_MISC_AP2CONN_WAKE,
			   !en * MT_INFRACFG_MISC_AP2CONN_WAKE);
}

static int mt7615_mcu_drv_pmctrl(struct mt7615_dev *dev)
{
	struct mt76_phy *mphy = &dev->mt76.phy;
	struct mt76_dev *mdev = &dev->mt76;
	u32 addr;
	int err;

	addr = is_mt7663(mdev) ? MT_PCIE_DOORBELL_PUSH : MT_CFG_LPCR_HOST;
	mt76_wr(dev, addr, MT_CFG_LPCR_HOST_DRV_OWN);

	mt7622_trigger_hif_int(dev, true);

	addr = is_mt7663(mdev) ? MT_CONN_HIF_ON_LPCTL : MT_CFG_LPCR_HOST;
	err = !mt76_poll_msec(dev, addr, MT_CFG_LPCR_HOST_FW_OWN, 0, 3000);

	mt7622_trigger_hif_int(dev, false);

	if (err) {
		dev_err(mdev->dev, "driver own failed\n");
		return -ETIMEDOUT;
	}

	clear_bit(MT76_STATE_PM, &mphy->state);

	return 0;
}

static int mt7615_mcu_lp_drv_pmctrl(struct mt7615_dev *dev)
{
	struct mt76_phy *mphy = &dev->mt76.phy;
	int i;

	if (!test_and_clear_bit(MT76_STATE_PM, &mphy->state))
		goto out;

	for (i = 0; i < MT7615_DRV_OWN_RETRY_COUNT; i++) {
		mt76_wr(dev, MT_PCIE_DOORBELL_PUSH, MT_CFG_LPCR_HOST_DRV_OWN);
		if (mt76_poll_msec(dev, MT_CONN_HIF_ON_LPCTL,
				   MT_CFG_LPCR_HOST_FW_OWN, 0, 50))
			break;
	}

	if (i == MT7615_DRV_OWN_RETRY_COUNT) {
		dev_err(dev->mt76.dev, "driver own failed\n");
		set_bit(MT76_STATE_PM, &mphy->state);
		return -EIO;
	}

out:
	dev->pm.last_activity = jiffies;

	return 0;
}

static int mt7615_mcu_fw_pmctrl(struct mt7615_dev *dev)
{
	struct mt76_phy *mphy = &dev->mt76.phy;
	int err = 0;
	u32 addr;

	if (test_and_set_bit(MT76_STATE_PM, &mphy->state))
		return 0;

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

	return err;
}

static void
mt7615_mcu_csa_finish(void *priv, u8 *mac, struct ieee80211_vif *vif)
{
	if (vif->csa_active)
		ieee80211_csa_finish(vif);
}

static void
mt7615_mcu_rx_radar_detected(struct mt7615_dev *dev, struct sk_buff *skb)
{
	struct mt76_phy *mphy = &dev->mt76.phy;
	struct mt7615_mcu_rdd_report *r;

	r = (struct mt7615_mcu_rdd_report *)skb->data;

	if (r->idx && dev->mt76.phy2)
		mphy = dev->mt76.phy2;

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

	wiphy_info(mt76_hw(dev)->wiphy, "%s: %s", type, data);
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
		ieee80211_iterate_active_interfaces_atomic(dev->mt76.hw,
				IEEE80211_IFACE_ITER_RESUME_ALL,
				mt7615_mcu_csa_finish, dev);
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

	if (*seq_num & BIT(7) && dev->mt76.phy2)
		mphy = dev->mt76.phy2;
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

	if (event->dbdc_band && dev->mt76.phy2)
		mphy = dev->mt76.phy2;
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
mt7615_mcu_beacon_loss_iter(void *priv, u8 *mac, struct ieee80211_vif *vif)
{
	struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;
	struct mt7615_beacon_loss_event *event = priv;

	if (mvif->idx != event->bss_idx)
		return;

	if (!(vif->driver_flags & IEEE80211_VIF_BEACON_FILTER))
		return;

	ieee80211_beacon_loss(vif);
}

static void
mt7615_mcu_beacon_loss_event(struct mt7615_dev *dev, struct sk_buff *skb)
{
	struct mt7615_beacon_loss_event *event;
	struct mt76_phy *mphy;
	u8 band_idx = 0; /* DBDC support */

	skb_pull(skb, sizeof(struct mt7615_mcu_rxd));
	event = (struct mt7615_beacon_loss_event *)skb->data;
	if (band_idx && dev->mt76.phy2)
		mphy = dev->mt76.phy2;
	else
		mphy = &dev->mt76.phy;

	ieee80211_iterate_active_interfaces_atomic(mphy->hw,
					IEEE80211_IFACE_ITER_RESUME_ALL,
					mt7615_mcu_beacon_loss_iter, event);
}

static void
mt7615_mcu_bss_event(struct mt7615_dev *dev, struct sk_buff *skb)
{
	struct mt7615_mcu_bss_event *event;
	struct mt76_phy *mphy;
	u8 band_idx = 0; /* DBDC support */

	event = (struct mt7615_mcu_bss_event *)(skb->data +
						sizeof(struct mt7615_mcu_rxd));

	if (band_idx && dev->mt76.phy2)
		mphy = dev->mt76.phy2;
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
	    rxd->eid == MCU_EVENT_ROC ||
	    !rxd->seq)
		mt7615_mcu_rx_unsolicited_event(dev, skb);
	else
		mt76_mcu_rx_event(&dev->mt76, skb);
}

static int mt7615_mcu_init_download(struct mt7615_dev *dev, u32 addr,
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

	return mt76_mcu_send_msg(&dev->mt76, MCU_CMD_TARGET_ADDRESS_LEN_REQ,
				 &req, sizeof(req), true);
}

static int
mt7615_mcu_muar_config(struct mt7615_dev *dev, struct ieee80211_vif *vif,
		       bool bssid, bool enable)
{
	struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;
	u32 idx = mvif->omac_idx - REPEATER_BSSID_START;
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

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_MUAR_UPDATE, &req,
				 sizeof(req), true);
}

static int
mt7615_mcu_add_dev(struct mt7615_dev *dev, struct ieee80211_vif *vif,
		   bool enable)
{
	struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;
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
			.omac_idx = mvif->omac_idx,
			.band_idx = mvif->band_idx,
			.tlv_num = cpu_to_le16(1),
			.is_tlv_append = 1,
		},
		.tlv = {
			.tag = cpu_to_le16(DEV_INFO_ACTIVE),
			.len = cpu_to_le16(sizeof(struct req_tlv)),
			.active = enable,
			.band_idx = mvif->band_idx,
		},
	};

	if (mvif->omac_idx >= REPEATER_BSSID_START)
		return mt7615_mcu_muar_config(dev, vif, false, enable);

	memcpy(data.tlv.omac_addr, vif->addr, ETH_ALEN);
	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_DEV_INFO_UPDATE,
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
		.omac_idx = mvif->omac_idx,
		.enable = enable,
		.wlan_idx = wcid->idx,
		.band_idx = mvif->band_idx,
	};
	struct sk_buff *skb;

	skb = ieee80211_beacon_get_template(hw, vif, &offs);
	if (!skb)
		return -EINVAL;

	if (skb->len > 512 - MT_TXD_SIZE) {
		dev_err(dev->mt76.dev, "Bcn size limit exceed\n");
		dev_kfree_skb(skb);
		return -EINVAL;
	}

	if (mvif->band_idx) {
		info = IEEE80211_SKB_CB(skb);
		info->hw_queue |= MT_TX_HW_QUEUE_EXT_PHY;
	}

	mt7615_mac_write_txwi(dev, (__le32 *)(req.pkt), skb, wcid, NULL,
			      0, NULL, true);
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

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_BCN_OFFLOAD, &req,
				 sizeof(req), true);
}

static int
mt7615_mcu_ctrl_pm_state(struct mt7615_dev *dev, int band, int state)
{
#define ENTER_PM_STATE	1
#define EXIT_PM_STATE	2
	struct {
		u8 pm_number;
		u8 pm_state;
		u8 bssid[ETH_ALEN];
		u8 dtim_period;
		u8 wlan_idx;
		__le16 bcn_interval;
		__le32 aid;
		__le32 rx_filter;
		u8 band_idx;
		u8 rsv[3];
		__le32 feature;
		u8 omac_idx;
		u8 wmm_idx;
		u8 bcn_loss_cnt;
		u8 bcn_sp_duration;
	} __packed req = {
		.pm_number = 5,
		.pm_state = state ? ENTER_PM_STATE : EXIT_PM_STATE,
		.band_idx = band,
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_PM_STATE_CTRL, &req,
				 sizeof(req), true);
}

static struct sk_buff *
mt7615_mcu_alloc_sta_req(struct mt7615_dev *dev, struct mt7615_vif *mvif,
			 struct mt7615_sta *msta)
{
	struct sta_req_hdr hdr = {
		.bss_idx = mvif->idx,
		.wlan_idx = msta ? msta->wcid.idx : 0,
		.muar_idx = msta ? mvif->omac_idx : 0,
		.is_tlv_append = 1,
	};
	struct sk_buff *skb;

	skb = mt76_mcu_msg_alloc(&dev->mt76, NULL, MT7615_STA_UPDATE_MAX_SIZE);
	if (!skb)
		return ERR_PTR(-ENOMEM);

	skb_put_data(skb, &hdr, sizeof(hdr));

	return skb;
}

static struct wtbl_req_hdr *
mt7615_mcu_alloc_wtbl_req(struct mt7615_dev *dev, struct mt7615_sta *msta,
			  int cmd, void *sta_wtbl, struct sk_buff **skb)
{
	struct tlv *sta_hdr = sta_wtbl;
	struct wtbl_req_hdr hdr = {
		.wlan_idx = msta->wcid.idx,
		.operation = cmd,
	};
	struct sk_buff *nskb = *skb;

	if (!nskb) {
		nskb = mt76_mcu_msg_alloc(&dev->mt76, NULL,
					  MT7615_WTBL_UPDATE_BA_SIZE);
		if (!nskb)
			return ERR_PTR(-ENOMEM);

		*skb = nskb;
	}

	if (sta_hdr)
		sta_hdr->len = cpu_to_le16(sizeof(hdr));

	return skb_put_data(nskb, &hdr, sizeof(hdr));
}

static struct tlv *
mt7615_mcu_add_nested_tlv(struct sk_buff *skb, int tag, int len,
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
mt7615_mcu_add_tlv(struct sk_buff *skb, int tag, int len)
{
	return mt7615_mcu_add_nested_tlv(skb, tag, len, skb->data, NULL);
}

static int
mt7615_mcu_bss_basic_tlv(struct sk_buff *skb, struct ieee80211_vif *vif,
			 struct ieee80211_sta *sta, bool enable)
{
	struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;
	u32 type = vif->p2p ? NETWORK_P2P : NETWORK_INFRA;
	struct bss_info_basic *bss;
	u8 wlan_idx = mvif->sta.wcid.idx;
	struct tlv *tlv;

	tlv = mt7615_mcu_add_tlv(skb, BSS_INFO_BASIC, sizeof(*bss));

	switch (vif->type) {
	case NL80211_IFTYPE_MESH_POINT:
	case NL80211_IFTYPE_AP:
		break;
	case NL80211_IFTYPE_STATION:
		/* TODO: enable BSS_INFO_UAPSD & BSS_INFO_PM */
		if (enable && sta) {
			struct mt7615_sta *msta;

			msta = (struct mt7615_sta *)sta->drv_priv;
			wlan_idx = msta->wcid.idx;
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
	memcpy(bss->bssid, vif->bss_conf.bssid, ETH_ALEN);
	bss->bcn_interval = cpu_to_le16(vif->bss_conf.beacon_int);
	bss->network_type = cpu_to_le32(type);
	bss->dtim_period = vif->bss_conf.dtim_period;
	bss->bmc_tx_wlan_idx = wlan_idx;
	bss->wmm_idx = mvif->wmm_idx;
	bss->active = enable;

	return 0;
}

static void
mt7615_mcu_bss_omac_tlv(struct sk_buff *skb, struct ieee80211_vif *vif)
{
	struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;
	struct bss_info_omac *omac;
	struct tlv *tlv;
	u32 type = 0;
	u8 idx;

	tlv = mt7615_mcu_add_tlv(skb, BSS_INFO_OMAC, sizeof(*omac));

	switch (vif->type) {
	case NL80211_IFTYPE_MESH_POINT:
	case NL80211_IFTYPE_AP:
		if (vif->p2p)
			type = CONNECTION_P2P_GO;
		else
			type = CONNECTION_INFRA_AP;
		break;
	case NL80211_IFTYPE_STATION:
		if (vif->p2p)
			type = CONNECTION_P2P_GC;
		else
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

/* SIFS 20us + 512 byte beacon tranmitted by 1Mbps (3906us) */
#define BCN_TX_ESTIMATE_TIME (4096 + 20)
static void
mt7615_mcu_bss_ext_tlv(struct sk_buff *skb, struct mt7615_vif *mvif)
{
	struct bss_info_ext_bss *ext;
	int ext_bss_idx, tsf_offset;
	struct tlv *tlv;

	ext_bss_idx = mvif->omac_idx - EXT_BSSID_START;
	if (ext_bss_idx < 0)
		return;

	tlv = mt7615_mcu_add_tlv(skb, BSS_INFO_EXT_BSS, sizeof(*ext));

	ext = (struct bss_info_ext_bss *)tlv;
	tsf_offset = ext_bss_idx * BCN_TX_ESTIMATE_TIME;
	ext->mbss_tsf_offset = cpu_to_le32(tsf_offset);
}

static void
mt7615_mcu_sta_ba_tlv(struct sk_buff *skb,
		      struct ieee80211_ampdu_params *params,
		      bool enable, bool tx)
{
	struct sta_rec_ba *ba;
	struct tlv *tlv;

	tlv = mt7615_mcu_add_tlv(skb, STA_REC_BA, sizeof(*ba));

	ba = (struct sta_rec_ba *)tlv;
	ba->ba_type = tx ? MT_BA_TYPE_ORIGINATOR : MT_BA_TYPE_RECIPIENT,
	ba->winsize = cpu_to_le16(params->buf_size);
	ba->ssn = cpu_to_le16(params->ssn);
	ba->ba_en = enable << params->tid;
	ba->amsdu = params->amsdu;
	ba->tid = params->tid;
}

static void
mt7615_mcu_sta_basic_tlv(struct sk_buff *skb, struct ieee80211_vif *vif,
			 struct ieee80211_sta *sta, bool enable)
{
	struct sta_rec_basic *basic;
	struct tlv *tlv;
	int conn_type;

	tlv = mt7615_mcu_add_tlv(skb, STA_REC_BASIC, sizeof(*basic));

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

static void
mt7615_mcu_sta_ht_tlv(struct sk_buff *skb, struct ieee80211_sta *sta)
{
	struct tlv *tlv;

	if (sta->ht_cap.ht_supported) {
		struct sta_rec_ht *ht;

		tlv = mt7615_mcu_add_tlv(skb, STA_REC_HT, sizeof(*ht));
		ht = (struct sta_rec_ht *)tlv;
		ht->ht_cap = cpu_to_le16(sta->ht_cap.cap);
	}
	if (sta->vht_cap.vht_supported) {
		struct sta_rec_vht *vht;

		tlv = mt7615_mcu_add_tlv(skb, STA_REC_VHT, sizeof(*vht));
		vht = (struct sta_rec_vht *)tlv;
		vht->vht_rx_mcs_map = sta->vht_cap.vht_mcs.rx_mcs_map;
		vht->vht_tx_mcs_map = sta->vht_cap.vht_mcs.tx_mcs_map;
		vht->vht_cap = cpu_to_le32(sta->vht_cap.cap);
	}
}

static void
mt7615_mcu_sta_uapsd(struct sk_buff *skb, struct ieee80211_vif *vif,
		     struct ieee80211_sta *sta)
{
	struct sta_rec_uapsd *uapsd;
	struct tlv *tlv;

	if (vif->type != NL80211_IFTYPE_AP || !sta->wme)
		return;

	tlv = mt7615_mcu_add_tlv(skb, STA_REC_APPS, sizeof(*uapsd));
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
mt7615_mcu_wtbl_ba_tlv(struct sk_buff *skb,
		       struct ieee80211_ampdu_params *params,
		       bool enable, bool tx, void *sta_wtbl,
		       void *wtbl_tlv)
{
	struct wtbl_ba *ba;
	struct tlv *tlv;

	tlv = mt7615_mcu_add_nested_tlv(skb, WTBL_BA, sizeof(*ba),
					wtbl_tlv, sta_wtbl);

	ba = (struct wtbl_ba *)tlv;
	ba->tid = params->tid;

	if (tx) {
		ba->ba_type = MT_BA_TYPE_ORIGINATOR;
		ba->sn = enable ? cpu_to_le16(params->ssn) : 0;
		ba->ba_winsize = cpu_to_le16(params->buf_size);
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

static void
mt7615_mcu_wtbl_generic_tlv(struct sk_buff *skb, struct ieee80211_vif *vif,
			    struct ieee80211_sta *sta, void *sta_wtbl,
			    void *wtbl_tlv)
{
	struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;
	struct wtbl_generic *generic;
	struct wtbl_rx *rx;
	struct wtbl_spe *spe;
	struct tlv *tlv;

	tlv = mt7615_mcu_add_nested_tlv(skb, WTBL_GENERIC, sizeof(*generic),
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

	tlv = mt7615_mcu_add_nested_tlv(skb, WTBL_RX, sizeof(*rx),
					wtbl_tlv, sta_wtbl);

	rx = (struct wtbl_rx *)tlv;
	rx->rca1 = sta ? vif->type != NL80211_IFTYPE_AP : 1;
	rx->rca2 = 1;
	rx->rv = 1;

	tlv = mt7615_mcu_add_nested_tlv(skb, WTBL_SPE, sizeof(*spe),
					wtbl_tlv, sta_wtbl);
	spe = (struct wtbl_spe *)tlv;
	spe->spe_idx = 24;
}

static void
mt7615_mcu_wtbl_ht_tlv(struct sk_buff *skb, struct ieee80211_sta *sta,
		       void *sta_wtbl, void *wtbl_tlv)
{
	struct tlv *tlv;
	struct wtbl_ht *ht = NULL;
	u32 flags = 0;

	if (sta->ht_cap.ht_supported) {
		tlv = mt7615_mcu_add_nested_tlv(skb, WTBL_HT, sizeof(*ht),
						wtbl_tlv, sta_wtbl);
		ht = (struct wtbl_ht *)tlv;
		ht->ldpc = !!(sta->ht_cap.cap & IEEE80211_HT_CAP_LDPC_CODING);
		ht->af = sta->ht_cap.ampdu_factor;
		ht->mm = sta->ht_cap.ampdu_density;
		ht->ht = 1;

		if (sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_20)
			flags |= MT_WTBL_W5_SHORT_GI_20;
		if (sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_40)
			flags |= MT_WTBL_W5_SHORT_GI_40;
	}

	if (sta->vht_cap.vht_supported) {
		struct wtbl_vht *vht;
		u8 af;

		tlv = mt7615_mcu_add_nested_tlv(skb, WTBL_VHT, sizeof(*vht),
						wtbl_tlv, sta_wtbl);
		vht = (struct wtbl_vht *)tlv;
		vht->ldpc = !!(sta->vht_cap.cap & IEEE80211_VHT_CAP_RXLDPC);
		vht->vht = 1;

		af = (sta->vht_cap.cap &
		      IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK) >>
		      IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_SHIFT;

		if (ht)
		    ht->af = max(ht->af, af);

		if (sta->vht_cap.cap & IEEE80211_VHT_CAP_SHORT_GI_80)
			flags |= MT_WTBL_W5_SHORT_GI_80;
		if (sta->vht_cap.cap & IEEE80211_VHT_CAP_SHORT_GI_160)
			flags |= MT_WTBL_W5_SHORT_GI_160;
	}

	/* wtbl smps */
	if (sta->smps_mode == IEEE80211_SMPS_DYNAMIC) {
		struct wtbl_smps *smps;

		tlv = mt7615_mcu_add_nested_tlv(skb, WTBL_SMPS, sizeof(*smps),
						wtbl_tlv, sta_wtbl);
		smps = (struct wtbl_smps *)tlv;
		smps->smps = 1;
	}

	if (sta->ht_cap.ht_supported) {
		/* sgi */
		u32 msk = MT_WTBL_W5_SHORT_GI_20 | MT_WTBL_W5_SHORT_GI_40 |
			  MT_WTBL_W5_SHORT_GI_80 | MT_WTBL_W5_SHORT_GI_160;
		struct wtbl_raw *raw;

		tlv = mt7615_mcu_add_nested_tlv(skb, WTBL_RAW_DATA,
						sizeof(*raw), wtbl_tlv,
						sta_wtbl);
		raw = (struct wtbl_raw *)tlv;
		raw->val = cpu_to_le32(flags);
		raw->msk = cpu_to_le32(~msk);
		raw->wtbl_idx = 1;
		raw->dw = 5;
	}
}

static int
mt7615_mcu_add_bss(struct mt7615_phy *phy, struct ieee80211_vif *vif,
		   struct ieee80211_sta *sta, bool enable)
{
	struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;
	struct mt7615_dev *dev = phy->dev;
	struct sk_buff *skb;

	if (mvif->omac_idx >= REPEATER_BSSID_START)
		mt7615_mcu_muar_config(dev, vif, true, enable);

	skb = mt7615_mcu_alloc_sta_req(dev, mvif, NULL);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	if (enable)
		mt7615_mcu_bss_omac_tlv(skb, vif);

	mt7615_mcu_bss_basic_tlv(skb, vif, sta, enable);

	if (enable && mvif->omac_idx >= EXT_BSSID_START &&
	    mvif->omac_idx < REPEATER_BSSID_START)
		mt7615_mcu_bss_ext_tlv(skb, mvif);

	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_EXT_CMD_BSS_INFO_UPDATE, true);
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

	wtbl_hdr = mt7615_mcu_alloc_wtbl_req(dev, msta, WTBL_SET, NULL, &skb);
	if (IS_ERR(wtbl_hdr))
		return PTR_ERR(wtbl_hdr);

	mt7615_mcu_wtbl_ba_tlv(skb, params, enable, true, NULL, wtbl_hdr);

	err = mt76_mcu_skb_send_msg(&dev->mt76, skb, MCU_EXT_CMD_WTBL_UPDATE,
				    true);
	if (err < 0)
		return err;

	skb = mt7615_mcu_alloc_sta_req(dev, mvif, msta);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	mt7615_mcu_sta_ba_tlv(skb, params, enable, true);

	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_EXT_CMD_STA_REC_UPDATE, true);
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

	skb = mt7615_mcu_alloc_sta_req(dev, mvif, msta);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	mt7615_mcu_sta_ba_tlv(skb, params, enable, false);

	err = mt76_mcu_skb_send_msg(&dev->mt76, skb,
				    MCU_EXT_CMD_STA_REC_UPDATE, true);
	if (err < 0 || !enable)
		return err;

	skb = NULL;
	wtbl_hdr = mt7615_mcu_alloc_wtbl_req(dev, msta, WTBL_SET, NULL, &skb);
	if (IS_ERR(wtbl_hdr))
		return PTR_ERR(wtbl_hdr);

	mt7615_mcu_wtbl_ba_tlv(skb, params, enable, false, NULL, wtbl_hdr);

	return mt76_mcu_skb_send_msg(&dev->mt76, skb, MCU_EXT_CMD_WTBL_UPDATE,
				     true);
}

static int
mt7615_mcu_wtbl_sta_add(struct mt7615_dev *dev, struct ieee80211_vif *vif,
			struct ieee80211_sta *sta, bool enable)
{
	struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;
	struct sk_buff *skb, *sskb, *wskb = NULL;
	struct wtbl_req_hdr *wtbl_hdr;
	struct mt7615_sta *msta;
	int cmd, err;

	msta = sta ? (struct mt7615_sta *)sta->drv_priv : &mvif->sta;

	sskb = mt7615_mcu_alloc_sta_req(dev, mvif, msta);
	if (IS_ERR(sskb))
		return PTR_ERR(sskb);

	mt7615_mcu_sta_basic_tlv(sskb, vif, sta, enable);
	if (enable && sta) {
		mt7615_mcu_sta_ht_tlv(sskb, sta);
		mt7615_mcu_sta_uapsd(sskb, vif, sta);
	}

	wtbl_hdr = mt7615_mcu_alloc_wtbl_req(dev, msta, WTBL_RESET_AND_SET,
					     NULL, &wskb);
	if (IS_ERR(wtbl_hdr))
		return PTR_ERR(wtbl_hdr);

	if (enable) {
		mt7615_mcu_wtbl_generic_tlv(wskb, vif, sta, NULL, wtbl_hdr);
		if (sta)
			mt7615_mcu_wtbl_ht_tlv(wskb, sta, NULL, wtbl_hdr);
	}

	cmd = enable ? MCU_EXT_CMD_WTBL_UPDATE : MCU_EXT_CMD_STA_REC_UPDATE;
	skb = enable ? wskb : sskb;

	err = mt76_mcu_skb_send_msg(&dev->mt76, skb, cmd, true);
	if (err < 0) {
		skb = enable ? sskb : wskb;
		dev_kfree_skb(skb);

		return err;
	}

	cmd = enable ? MCU_EXT_CMD_STA_REC_UPDATE : MCU_EXT_CMD_WTBL_UPDATE;
	skb = enable ? sskb : wskb;

	return mt76_mcu_skb_send_msg(&dev->mt76, skb, cmd, true);
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

	skb = mt7615_mcu_alloc_sta_req(dev, mvif, msta);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	mt7615_mcu_sta_ba_tlv(skb, params, enable, tx);

	sta_wtbl = mt7615_mcu_add_tlv(skb, STA_REC_WTBL, sizeof(struct tlv));

	wtbl_hdr = mt7615_mcu_alloc_wtbl_req(dev, msta, WTBL_SET, sta_wtbl,
					     &skb);
	mt7615_mcu_wtbl_ba_tlv(skb, params, enable, tx, sta_wtbl, wtbl_hdr);

	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_EXT_CMD_STA_REC_UPDATE, true);
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
mt7615_mcu_add_sta_cmd(struct mt7615_dev *dev, struct ieee80211_vif *vif,
		       struct ieee80211_sta *sta, bool enable, int cmd)
{
	struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;
	struct wtbl_req_hdr *wtbl_hdr;
	struct mt7615_sta *msta;
	struct tlv *sta_wtbl;
	struct sk_buff *skb;

	msta = sta ? (struct mt7615_sta *)sta->drv_priv : &mvif->sta;

	skb = mt7615_mcu_alloc_sta_req(dev, mvif, msta);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	mt7615_mcu_sta_basic_tlv(skb, vif, sta, enable);
	if (enable && sta) {
		mt7615_mcu_sta_ht_tlv(skb, sta);
		mt7615_mcu_sta_uapsd(skb, vif, sta);
	}

	sta_wtbl = mt7615_mcu_add_tlv(skb, STA_REC_WTBL, sizeof(struct tlv));

	wtbl_hdr = mt7615_mcu_alloc_wtbl_req(dev, msta, WTBL_RESET_AND_SET,
					     sta_wtbl, &skb);
	if (enable) {
		mt7615_mcu_wtbl_generic_tlv(skb, vif, sta, sta_wtbl, wtbl_hdr);
		if (sta)
			mt7615_mcu_wtbl_ht_tlv(skb, sta, sta_wtbl, wtbl_hdr);
	}

	return mt76_mcu_skb_send_msg(&dev->mt76, skb, cmd, true);
}

static int
mt7615_mcu_add_sta(struct mt7615_dev *dev, struct ieee80211_vif *vif,
		   struct ieee80211_sta *sta, bool enable)
{
	return mt7615_mcu_add_sta_cmd(dev, vif, sta, enable,
				      MCU_EXT_CMD_STA_REC_UPDATE);
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
};

static int
mt7615_mcu_uni_add_dev(struct mt7615_dev *dev,
		       struct ieee80211_vif *vif, bool enable)
{
	struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;
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
		struct mt7615_bss_basic_tlv basic;
	} basic_req = {
		.hdr = {
			.bss_idx = mvif->idx,
		},
		.basic = {
			.tag = cpu_to_le16(UNI_BSS_INFO_BASIC),
			.len = cpu_to_le16(sizeof(struct mt7615_bss_basic_tlv)),
			.omac_idx = mvif->omac_idx,
			.band_idx = mvif->band_idx,
			.wmm_idx = mvif->wmm_idx,
			.active = enable,
			.bmc_tx_wlan_idx = cpu_to_le16(mvif->sta.wcid.idx),
			.sta_idx = cpu_to_le16(mvif->sta.wcid.idx),
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

	err = mt76_mcu_send_msg(&dev->mt76, cmd, data, len, true);
	if (err < 0)
		return err;

	cmd = enable ? MCU_UNI_CMD_BSS_INFO_UPDATE : MCU_UNI_CMD_DEV_INFO_UPDATE;
	data = enable ? (void *)&basic_req : (void *)&dev_req;
	len = enable ? sizeof(basic_req) : sizeof(dev_req);

	return mt76_mcu_send_msg(&dev->mt76, cmd, data, len, true);
}

static int
mt7615_mcu_uni_ctrl_pm_state(struct mt7615_dev *dev, int band, int state)
{
	return 0;
}

static int
mt7615_mcu_uni_add_bss(struct mt7615_phy *phy, struct ieee80211_vif *vif,
		       struct ieee80211_sta *sta, bool enable)
{
	struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;
	struct cfg80211_chan_def *chandef = &phy->mt76->chandef;
	int freq1 = chandef->center_freq1, freq2 = chandef->center_freq2;
	struct mt7615_dev *dev = phy->dev;
	struct {
		struct {
			u8 bss_idx;
			u8 pad[3];
		} __packed hdr;
		struct mt7615_bss_basic_tlv basic;
		struct mt7615_bss_qos_tlv qos;
	} basic_req = {
		.hdr = {
			.bss_idx = mvif->idx,
		},
		.basic = {
			.tag = cpu_to_le16(UNI_BSS_INFO_BASIC),
			.len = cpu_to_le16(sizeof(struct mt7615_bss_basic_tlv)),
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
			.len = cpu_to_le16(sizeof(struct mt7615_bss_qos_tlv)),
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
			.tx_streams = hweight8(phy->mt76->antenna_mask),
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
	basic_req.basic.bmc_tx_wlan_idx = cpu_to_le16(mvif->sta.wcid.idx);
	basic_req.basic.sta_idx = cpu_to_le16(mvif->sta.wcid.idx);
	basic_req.basic.conn_state = !enable;

	err = mt76_mcu_send_msg(&dev->mt76, MCU_UNI_CMD_BSS_INFO_UPDATE,
				&basic_req, sizeof(basic_req), true);
	if (err < 0)
		return err;

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

	return mt76_mcu_send_msg(&dev->mt76, MCU_UNI_CMD_BSS_INFO_UPDATE,
				 &rlm_req, sizeof(rlm_req), true);
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
			/* 0: enable beacon offload
			 * 1: disable beacon offload
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
			.bss_idx = mvif->idx,
		},
		.beacon_tlv = {
			.tag = cpu_to_le16(UNI_BSS_INFO_BCN_CONTENT),
			.len = cpu_to_le16(sizeof(struct bcn_content_tlv)),
			.enable = enable,
		},
	};
	struct sk_buff *skb;

	skb = ieee80211_beacon_get_template(mt76_hw(dev), vif, &offs);
	if (!skb)
		return -EINVAL;

	if (skb->len > 512 - MT_TXD_SIZE) {
		dev_err(dev->mt76.dev, "beacon size limit exceed\n");
		dev_kfree_skb(skb);
		return -EINVAL;
	}

	mt7615_mac_write_txwi(dev, (__le32 *)(req.beacon_tlv.pkt), skb,
			      wcid, NULL, 0, NULL, true);
	memcpy(req.beacon_tlv.pkt + MT_TXD_SIZE, skb->data, skb->len);
	req.beacon_tlv.pkt_len = cpu_to_le16(MT_TXD_SIZE + skb->len);
	req.beacon_tlv.tim_ie_pos = cpu_to_le16(MT_TXD_SIZE + offs.tim_offset);

	if (offs.cntdwn_counter_offs[0]) {
		u16 csa_offs;

		csa_offs = MT_TXD_SIZE + offs.cntdwn_counter_offs[0] - 4;
		req.beacon_tlv.csa_ie_pos = cpu_to_le16(csa_offs);
	}
	dev_kfree_skb(skb);

	return mt76_mcu_send_msg(&dev->mt76, MCU_UNI_CMD_BSS_INFO_UPDATE,
				 &req, sizeof(req), true);
}

static int
mt7615_mcu_uni_tx_ba(struct mt7615_dev *dev,
		     struct ieee80211_ampdu_params *params,
		     bool enable)
{
	struct mt7615_sta *msta = (struct mt7615_sta *)params->sta->drv_priv;
	struct mt7615_vif *mvif = msta->vif;
	struct wtbl_req_hdr *wtbl_hdr;
	struct tlv *sta_wtbl;
	struct sk_buff *skb;
	int err;

	skb = mt7615_mcu_alloc_sta_req(dev, mvif, msta);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	sta_wtbl = mt7615_mcu_add_tlv(skb, STA_REC_WTBL, sizeof(struct tlv));

	wtbl_hdr = mt7615_mcu_alloc_wtbl_req(dev, msta, WTBL_SET, sta_wtbl,
					     &skb);
	if (IS_ERR(wtbl_hdr))
		return PTR_ERR(wtbl_hdr);

	mt7615_mcu_wtbl_ba_tlv(skb, params, enable, true, sta_wtbl,
			       wtbl_hdr);

	err =  mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_UNI_CMD_STA_REC_UPDATE, true);
	if (err < 0)
		return err;

	skb = mt7615_mcu_alloc_sta_req(dev, mvif, msta);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	mt7615_mcu_sta_ba_tlv(skb, params, enable, true);

	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_UNI_CMD_STA_REC_UPDATE, true);
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

	skb = mt7615_mcu_alloc_sta_req(dev, mvif, msta);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	mt7615_mcu_sta_ba_tlv(skb, params, enable, false);

	err = mt76_mcu_skb_send_msg(&dev->mt76, skb,
				    MCU_UNI_CMD_STA_REC_UPDATE, true);
	if (err < 0 || !enable)
		return err;

	skb = mt7615_mcu_alloc_sta_req(dev, mvif, msta);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	sta_wtbl = mt7615_mcu_add_tlv(skb, STA_REC_WTBL, sizeof(struct tlv));

	wtbl_hdr = mt7615_mcu_alloc_wtbl_req(dev, msta, WTBL_SET, sta_wtbl,
					     &skb);
	if (IS_ERR(wtbl_hdr))
		return PTR_ERR(wtbl_hdr);

	mt7615_mcu_wtbl_ba_tlv(skb, params, enable, false, sta_wtbl,
			       wtbl_hdr);

	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_UNI_CMD_STA_REC_UPDATE, true);
}

static int
mt7615_mcu_uni_add_sta(struct mt7615_dev *dev, struct ieee80211_vif *vif,
		       struct ieee80211_sta *sta, bool enable)
{
	return mt7615_mcu_add_sta_cmd(dev, vif, sta, enable,
				      MCU_UNI_CMD_STA_REC_UPDATE);
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
};

static int mt7615_mcu_start_firmware(struct mt7615_dev *dev, u32 addr,
				     u32 option)
{
	struct {
		__le32 option;
		__le32 addr;
	} req = {
		.option = cpu_to_le32(option),
		.addr = cpu_to_le32(addr),
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_CMD_FW_START_REQ, &req,
				 sizeof(req), true);
}

int mt7615_mcu_restart(struct mt76_dev *dev)
{
	return mt76_mcu_send_msg(dev, MCU_CMD_RESTART_DL_REQ, NULL, 0, true);
}
EXPORT_SYMBOL_GPL(mt7615_mcu_restart);

static int mt7615_mcu_patch_sem_ctrl(struct mt7615_dev *dev, bool get)
{
	struct {
		__le32 op;
	} req = {
		.op = cpu_to_le32(get ? PATCH_SEM_GET : PATCH_SEM_RELEASE),
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_CMD_PATCH_SEM_CONTROL, &req,
				 sizeof(req), true);
}

static int mt7615_mcu_start_patch(struct mt7615_dev *dev)
{
	struct {
		u8 check_crc;
		u8 reserved[3];
	} req = {
		.check_crc = 0,
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_CMD_PATCH_FINISH_REQ, &req,
				 sizeof(req), true);
}

static int mt7615_load_patch(struct mt7615_dev *dev, u32 addr, const char *name)
{
	const struct mt7615_patch_hdr *hdr;
	const struct firmware *fw = NULL;
	int len, ret, sem;

	sem = mt7615_mcu_patch_sem_ctrl(dev, 1);
	switch (sem) {
	case PATCH_IS_DL:
		return 0;
	case PATCH_NOT_DL_SEM_SUCCESS:
		break;
	default:
		dev_err(dev->mt76.dev, "Failed to get patch semaphore\n");
		return -EAGAIN;
	}

	ret = firmware_request_nowarn(&fw, name, dev->mt76.dev);
	if (ret)
		goto out;

	if (!fw || !fw->data || fw->size < sizeof(*hdr)) {
		dev_err(dev->mt76.dev, "Invalid firmware\n");
		ret = -EINVAL;
		goto out;
	}

	hdr = (const struct mt7615_patch_hdr *)(fw->data);

	dev_info(dev->mt76.dev, "HW/SW Version: 0x%x, Build Time: %.16s\n",
		 be32_to_cpu(hdr->hw_sw_ver), hdr->build_date);

	len = fw->size - sizeof(*hdr);

	ret = mt7615_mcu_init_download(dev, addr, len, DL_MODE_NEED_RSP);
	if (ret) {
		dev_err(dev->mt76.dev, "Download request failed\n");
		goto out;
	}

	ret = mt76_mcu_send_firmware(&dev->mt76, MCU_CMD_FW_SCATTER,
				     fw->data + sizeof(*hdr), len);
	if (ret) {
		dev_err(dev->mt76.dev, "Failed to send firmware to device\n");
		goto out;
	}

	ret = mt7615_mcu_start_patch(dev);
	if (ret)
		dev_err(dev->mt76.dev, "Failed to start patch\n");

out:
	release_firmware(fw);

	sem = mt7615_mcu_patch_sem_ctrl(dev, 0);
	switch (sem) {
	case PATCH_REL_SEM_SUCCESS:
		break;
	default:
		ret = -EAGAIN;
		dev_err(dev->mt76.dev, "Failed to release patch semaphore\n");
		break;
	}

	return ret;
}

static u32 mt7615_mcu_gen_dl_mode(u8 feature_set, bool is_cr4)
{
	u32 ret = 0;

	ret |= (feature_set & FW_FEATURE_SET_ENCRYPT) ?
	       (DL_MODE_ENCRYPT | DL_MODE_RESET_SEC_IV) : 0;
	ret |= FIELD_PREP(DL_MODE_KEY_IDX,
			  FIELD_GET(FW_FEATURE_SET_KEY_IDX, feature_set));
	ret |= DL_MODE_NEED_RSP;
	ret |= is_cr4 ? DL_MODE_WORKING_PDA_CR4 : 0;

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
		mode = mt7615_mcu_gen_dl_mode(hdr[i].feature_set, is_cr4);
		len = le32_to_cpu(hdr[i].len) + IMG_CRC_LEN;
		addr = le32_to_cpu(hdr[i].addr);

		err = mt7615_mcu_init_download(dev, addr, len, mode);
		if (err) {
			dev_err(dev->mt76.dev, "Download request failed\n");
			return err;
		}

		err = mt76_mcu_send_firmware(&dev->mt76, MCU_CMD_FW_SCATTER,
					     data + offset, len);
		if (err) {
			dev_err(dev->mt76.dev, "Failed to send firmware to device\n");
			return err;
		}

		offset += len;
	}

	return 0;
}

static const struct wiphy_wowlan_support mt7615_wowlan_support = {
	.flags = WIPHY_WOWLAN_MAGIC_PKT | WIPHY_WOWLAN_DISCONNECT |
		 WIPHY_WOWLAN_SUPPORTS_GTK_REKEY | WIPHY_WOWLAN_NET_DETECT,
	.n_patterns = 1,
	.pattern_min_len = 1,
	.pattern_max_len = MT7615_WOW_PATTEN_MAX_LEN,
	.max_nd_match_sets = 10,
};

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

	ret = mt7615_mcu_start_firmware(dev, le32_to_cpu(hdr->addr),
					FW_START_OVERRIDE);
	if (ret) {
		dev_err(dev->mt76.dev, "Failed to start N9 firmware\n");
		goto out;
	}

	snprintf(dev->mt76.hw->wiphy->fw_version,
		 sizeof(dev->mt76.hw->wiphy->fw_version),
		 "%.10s-%.15s", hdr->fw_ver, hdr->build_date);

	if (!is_mt7615(&dev->mt76) &&
	    !strncmp(hdr->fw_ver, "2.0", sizeof(hdr->fw_ver))) {
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

	ret = mt7615_mcu_start_firmware(dev, 0, FW_START_WORKING_PDA_CR4);
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
				       FW_STATE_CR4_RDY), 500)) {
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

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_FW_LOG_2_HOST, &data,
				 sizeof(data), true);
}

static int mt7663_load_n9(struct mt7615_dev *dev, const char *name)
{
	u32 offset = 0, override_addr = 0, flag = FW_START_DLYCAL;
	const struct mt7663_fw_trailer *hdr;
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

	hdr = (const struct mt7663_fw_trailer *)(fw->data + fw->size -
						 FW_V3_COMMON_TAILER_SIZE);

	dev_info(dev->mt76.dev, "N9 Firmware Version: %.10s, Build Time: %.15s\n",
		 hdr->fw_ver, hdr->build_date);
	dev_info(dev->mt76.dev, "Region number: 0x%x\n", hdr->n_region);

	base_addr = fw->data + fw->size - FW_V3_COMMON_TAILER_SIZE;
	for (i = 0; i < hdr->n_region; i++) {
		u32 shift = (hdr->n_region - i) * FW_V3_REGION_TAILER_SIZE;
		u32 len, addr, mode;

		dev_info(dev->mt76.dev, "Parsing tailer Region: %d\n", i);

		buf = (const struct mt7663_fw_buf *)(base_addr - shift);
		mode = mt7615_mcu_gen_dl_mode(buf->feature_set, false);
		addr = le32_to_cpu(buf->img_dest_addr);
		len = le32_to_cpu(buf->img_size);

		ret = mt7615_mcu_init_download(dev, addr, len, mode);
		if (ret) {
			dev_err(dev->mt76.dev, "Download request failed\n");
			goto out;
		}

		ret = mt76_mcu_send_firmware(&dev->mt76, MCU_CMD_FW_SCATTER,
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

	ret = mt7615_mcu_start_firmware(dev, override_addr, flag);
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
		dev->mt76.hw->wiphy->wowlan = &mt7615_wowlan_support;
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
		.mcu_restart = mt7615_mcu_restart,
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
	mt7615_mcu_fw_log_2_host(dev, 0);

	return 0;
}
EXPORT_SYMBOL_GPL(mt7615_mcu_init);

void mt7615_mcu_exit(struct mt7615_dev *dev)
{
	__mt76_mcu_restart(&dev->mt76);
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
				     MCU_EXT_CMD_EFUSE_BUFFER_MODE, true);
}
EXPORT_SYMBOL_GPL(mt7615_mcu_set_eeprom);

int mt7615_mcu_set_mac_enable(struct mt7615_dev *dev, int band, bool enable)
{
	struct {
		u8 enable;
		u8 band;
		u8 rsv[2];
	} __packed req = {
		.enable = enable,
		.band = band,
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_MAC_INIT_CTRL, &req,
				 sizeof(req), true);
}

int mt7615_mcu_set_rts_thresh(struct mt7615_phy *phy, u32 val)
{
	struct mt7615_dev *dev = phy->dev;
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

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_PROTECT_CTRL, &req,
				 sizeof(req), true);
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

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_EDCA_UPDATE, &req,
				 sizeof(req), true);
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
	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_DBDC_CTRL, &req,
				 sizeof(req), true);
}

int mt7615_mcu_del_wtbl_all(struct mt7615_dev *dev)
{
	struct wtbl_req_hdr req = {
		.operation = WTBL_RESET_ALL,
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_WTBL_UPDATE, &req,
				 sizeof(req), true);
}
EXPORT_SYMBOL_GPL(mt7615_mcu_del_wtbl_all);

int mt7615_mcu_rdd_cmd(struct mt7615_dev *dev,
		       enum mt7615_rdd_cmd cmd, u8 index,
		       u8 rx_sel, u8 val)
{
	struct {
		u8 ctrl;
		u8 rdd_idx;
		u8 rdd_rx_sel;
		u8 val;
		u8 rsv[4];
	} req = {
		.ctrl = cmd,
		.rdd_idx = index,
		.rdd_rx_sel = rx_sel,
		.val = val,
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_SET_RDD_CTRL, &req,
				 sizeof(req), true);
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

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_SET_RDD_TH, &req,
				 sizeof(req), true);
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

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_SET_RDD_TH, &req,
				 sizeof(req), true);
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

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_SET_RDD_TH, &req,
				 sizeof(req), true);
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

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_SET_RDD_PATTERN,
				 &req, sizeof(req), false);
}

static void mt7615_mcu_set_txpower_sku(struct mt7615_phy *phy, u8 *sku)
{
	struct mt76_phy *mphy = phy->mt76;
	struct ieee80211_hw *hw = mphy->hw;
	int n_chains = hweight8(mphy->antenna_mask);
	int tx_power;
	int i;

	tx_power = hw->conf.power_level * 2 -
		   mt76_tx_power_nss_delta(n_chains);
	mphy->txpower_cur = tx_power;

	for (i = 0; i < MT_SKU_1SS_DELTA; i++)
		sku[i] = tx_power;

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
		.rx_streams_mask = phy->chainmask,
		.center_chan2 = ieee80211_frequency_to_channel(freq2),
	};

	if (dev->mt76.hw->conf.flags & IEEE80211_CONF_OFFCHANNEL)
		req.switch_reason = CH_SWITCH_SCAN_BYPASS_DPD;
	else if ((chandef->chan->flags & IEEE80211_CHAN_RADAR) &&
		 chandef->chan->dfs_state != NL80211_DFS_AVAILABLE)
		req.switch_reason = CH_SWITCH_DFS;
	else
		req.switch_reason = CH_SWITCH_NORMAL;

	req.band_idx = phy != &dev->phy;
	req.bw = mt7615_mcu_chan_bw(chandef);

	if (mt76_testmode_enabled(&dev->mt76))
		memset(req.txpower_sku, 0x3f, 49);
	else
		mt7615_mcu_set_txpower_sku(phy, req.txpower_sku);

	return mt76_mcu_send_msg(&dev->mt76, cmd, &req, sizeof(req), true);
}

int mt7615_mcu_get_temperature(struct mt7615_dev *dev, int index)
{
	struct {
		u8 action;
		u8 rsv[3];
	} req = {
		.action = index,
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_GET_TEMP, &req,
				 sizeof(req), true);
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

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_ATE_CTRL, &req,
				 sizeof(req), false);
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
				 MCU_EXT_CMD_TX_POWER_FEATURE_CTRL, &req,
				 sizeof(req), true);
}

int mt7615_mcu_set_vif_ps(struct mt7615_dev *dev, struct ieee80211_vif *vif)
{
	struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;
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
		return -ENOTSUPP;

	return mt76_mcu_send_msg(&dev->mt76, MCU_CMD_SET_PS_PROFILE, &req,
				 sizeof(req), false);
}

int mt7615_mcu_set_channel_domain(struct mt7615_phy *phy)
{
	struct mt76_phy *mphy = phy->mt76;
	struct mt7615_dev *dev = phy->dev;
	struct mt7615_mcu_channel_domain {
		__le32 country_code; /* regulatory_request.alpha2 */
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
		.n_2ch = mphy->sband_2g.sband.n_channels,
		.n_5ch = mphy->sband_5g.sband.n_channels,
	};
	struct mt7615_mcu_chan {
		__le16 hw_value;
		__le16 pad;
		__le32 flags;
	} __packed;
	int i, n_channels = hdr.n_2ch + hdr.n_5ch;
	int len = sizeof(hdr) + n_channels * sizeof(struct mt7615_mcu_chan);
	struct sk_buff *skb;

	if (!mt7615_firmware_offload(dev))
		return 0;

	skb = mt76_mcu_msg_alloc(&dev->mt76, NULL, len);
	if (!skb)
		return -ENOMEM;

	skb_put_data(skb, &hdr, sizeof(hdr));

	for (i = 0; i < n_channels; i++) {
		struct ieee80211_channel *chan;
		struct mt7615_mcu_chan channel;

		if (i < hdr.n_2ch)
			chan = &mphy->sband_2g.sband.channels[i];
		else
			chan = &mphy->sband_5g.sband.channels[i - hdr.n_2ch];

		channel.hw_value = cpu_to_le16(chan->hw_value);
		channel.flags = cpu_to_le32(chan->flags);
		channel.pad = 0;

		skb_put_data(skb, &channel, sizeof(channel));
	}

	return mt76_mcu_skb_send_msg(&dev->mt76, skb, MCU_CMD_SET_CHAN_DOMAIN,
				     false);
}

#define MT7615_SCAN_CHANNEL_TIME	60
int mt7615_mcu_hw_scan(struct mt7615_phy *phy, struct ieee80211_vif *vif,
		       struct ieee80211_scan_request *scan_req)
{
	struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;
	struct cfg80211_scan_request *sreq = &scan_req->req;
	int n_ssids = 0, err, i, duration = MT7615_SCAN_CHANNEL_TIME;
	int ext_channels_num = max_t(int, sreq->n_channels - 32, 0);
	struct ieee80211_channel **scan_list = sreq->channels;
	struct mt7615_dev *dev = phy->dev;
	bool ext_phy = phy != &dev->phy;
	struct mt7615_mcu_scan_channel *chan;
	struct mt7615_hw_scan_req *req;
	struct sk_buff *skb;

	/* fall-back to sw-scan */
	if (!mt7615_firmware_offload(dev))
		return 1;

	skb = mt76_mcu_msg_alloc(&dev->mt76, NULL, sizeof(*req));
	if (!skb)
		return -ENOMEM;

	set_bit(MT76_HW_SCANNING, &phy->mt76->state);
	mvif->scan_seq_num = (mvif->scan_seq_num + 1) & 0x7f;

	req = (struct mt7615_hw_scan_req *)skb_put(skb, sizeof(*req));

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

	err = mt76_mcu_skb_send_msg(&dev->mt76, skb, MCU_CMD_START_HW_SCAN,
				    false);
	if (err < 0)
		clear_bit(MT76_HW_SCANNING, &phy->mt76->state);

	return err;
}

int mt7615_mcu_cancel_hw_scan(struct mt7615_phy *phy,
			      struct ieee80211_vif *vif)
{
	struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;
	struct mt7615_dev *dev = phy->dev;
	struct {
		u8 seq_num;
		u8 is_ext_channel;
		u8 rsv[2];
	} __packed req = {
		.seq_num = mvif->scan_seq_num,
	};

	if (test_and_clear_bit(MT76_HW_SCANNING, &phy->mt76->state)) {
		struct cfg80211_scan_info info = {
			.aborted = true,
		};

		ieee80211_scan_completed(phy->mt76->hw, &info);
	}

	return mt76_mcu_send_msg(&dev->mt76, MCU_CMD_CANCEL_HW_SCAN, &req,
				 sizeof(req), false);
}

int mt7615_mcu_sched_scan_req(struct mt7615_phy *phy,
			      struct ieee80211_vif *vif,
			      struct cfg80211_sched_scan_request *sreq)
{
	struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;
	struct ieee80211_channel **scan_list = sreq->channels;
	struct mt7615_dev *dev = phy->dev;
	bool ext_phy = phy != &dev->phy;
	struct mt7615_mcu_scan_channel *chan;
	struct mt7615_sched_scan_req *req;
	struct cfg80211_match_set *match;
	struct cfg80211_ssid *ssid;
	struct sk_buff *skb;
	int i;

	if (!mt7615_firmware_offload(dev))
		return -ENOTSUPP;

	skb = mt76_mcu_msg_alloc(&dev->mt76, NULL,
				 sizeof(*req) + sreq->ie_len);
	if (!skb)
		return -ENOMEM;

	mvif->scan_seq_num = (mvif->scan_seq_num + 1) & 0x7f;

	req = (struct mt7615_sched_scan_req *)skb_put(skb, sizeof(*req));
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

	return mt76_mcu_skb_send_msg(&dev->mt76, skb, MCU_CMD_SCHED_SCAN_REQ,
				     false);
}

int mt7615_mcu_sched_scan_enable(struct mt7615_phy *phy,
				 struct ieee80211_vif *vif,
				 bool enable)
{
	struct mt7615_dev *dev = phy->dev;
	struct {
		u8 active; /* 0: enabled 1: disabled */
		u8 rsv[3];
	} __packed req = {
		.active = !enable,
	};

	if (!mt7615_firmware_offload(dev))
		return -ENOTSUPP;

	if (enable)
		set_bit(MT76_HW_SCHED_SCANNING, &phy->mt76->state);
	else
		clear_bit(MT76_HW_SCHED_SCANNING, &phy->mt76->state);

	return mt76_mcu_send_msg(&dev->mt76, MCU_CMD_SCHED_SCAN_ENABLE, &req,
				 sizeof(req), false);
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
		.dbdc_en = !!dev->mt76.phy2,
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
	ret = mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_RXDCOC_CAL, &req,
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
		.dbdc_en = !!dev->mt76.phy2,
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
	ret = mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_TXDPD_CAL, &req,
				sizeof(req), true);

	if ((chandef->width == NL80211_CHAN_WIDTH_80P80 ||
	     chandef->width == NL80211_CHAN_WIDTH_160) && !req.is_freq2) {
		req.is_freq2 = true;
		center_freq = freq2;
		goto again;
	}

	return ret;
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
		.bss_idx = mvif->idx,
		.aid = cpu_to_le16(vif->bss_conf.aid),
		.dtim_period = vif->bss_conf.dtim_period,
		.bcn_interval = cpu_to_le16(vif->bss_conf.beacon_int),
	};
	struct {
		u8 bss_idx;
		u8 pad[3];
	} req_hdr = {
		.bss_idx = mvif->idx,
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

#ifdef CONFIG_PM
int mt7615_mcu_set_hif_suspend(struct mt7615_dev *dev, bool suspend)
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

	if (mt76_is_mmio(&dev->mt76))
		req.hdr.hif_type = 2;
	else if (mt76_is_usb(&dev->mt76))
		req.hdr.hif_type = 1;
	else if (mt76_is_sdio(&dev->mt76))
		req.hdr.hif_type = 0;

	return mt76_mcu_send_msg(&dev->mt76, MCU_UNI_CMD_HIF_CTRL, &req,
				 sizeof(req), true);
}
EXPORT_SYMBOL_GPL(mt7615_mcu_set_hif_suspend);

static int
mt7615_mcu_set_wow_ctrl(struct mt7615_phy *phy, struct ieee80211_vif *vif,
			bool suspend, struct cfg80211_wowlan *wowlan)
{
	struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;
	struct mt7615_dev *dev = phy->dev;
	struct {
		struct {
			u8 bss_idx;
			u8 pad[3];
		} __packed hdr;
		struct mt7615_wow_ctrl_tlv wow_ctrl_tlv;
		struct mt7615_wow_gpio_param_tlv gpio_tlv;
	} req = {
		.hdr = {
			.bss_idx = mvif->idx,
		},
		.wow_ctrl_tlv = {
			.tag = cpu_to_le16(UNI_SUSPEND_WOW_CTRL),
			.len = cpu_to_le16(sizeof(struct mt7615_wow_ctrl_tlv)),
			.cmd = suspend ? 1 : 2,
		},
		.gpio_tlv = {
			.tag = cpu_to_le16(UNI_SUSPEND_WOW_GPIO_PARAM),
			.len = cpu_to_le16(sizeof(struct mt7615_wow_gpio_param_tlv)),
			.gpio_pin = 0xff, /* follow fw about GPIO pin */
		},
	};

	if (wowlan->magic_pkt)
		req.wow_ctrl_tlv.trigger |= BIT(0);
	if (wowlan->disconnect)
		req.wow_ctrl_tlv.trigger |= BIT(2);
	if (wowlan->nd_config) {
		mt7615_mcu_sched_scan_req(phy, vif, wowlan->nd_config);
		req.wow_ctrl_tlv.trigger |= BIT(5);
		mt7615_mcu_sched_scan_enable(phy, vif, suspend);
	}

	if (mt76_is_mmio(&dev->mt76))
		req.wow_ctrl_tlv.wakeup_hif = WOW_PCIE;
	else if (mt76_is_usb(&dev->mt76))
		req.wow_ctrl_tlv.wakeup_hif = WOW_USB;
	else if (mt76_is_sdio(&dev->mt76))
		req.wow_ctrl_tlv.wakeup_hif = WOW_GPIO;

	return mt76_mcu_send_msg(&dev->mt76, MCU_UNI_CMD_SUSPEND, &req,
				 sizeof(req), true);
}

static int
mt7615_mcu_set_wow_pattern(struct mt7615_dev *dev,
			   struct ieee80211_vif *vif,
			   u8 index, bool enable,
			   struct cfg80211_pkt_pattern *pattern)
{
	struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;
	struct mt7615_wow_pattern_tlv *ptlv;
	struct sk_buff *skb;
	struct req_hdr {
		u8 bss_idx;
		u8 pad[3];
	} __packed hdr = {
		.bss_idx = mvif->idx,
	};

	skb = mt76_mcu_msg_alloc(&dev->mt76, NULL,
				 sizeof(hdr) + sizeof(*ptlv));
	if (!skb)
		return -ENOMEM;

	skb_put_data(skb, &hdr, sizeof(hdr));
	ptlv = (struct mt7615_wow_pattern_tlv *)skb_put(skb, sizeof(*ptlv));
	ptlv->tag = cpu_to_le16(UNI_SUSPEND_WOW_PATTERN);
	ptlv->len = cpu_to_le16(sizeof(*ptlv));
	ptlv->data_len = pattern->pattern_len;
	ptlv->enable = enable;
	ptlv->index = index;

	memcpy(ptlv->pattern, pattern->pattern, pattern->pattern_len);
	memcpy(ptlv->mask, pattern->mask, pattern->pattern_len / 8);

	return mt76_mcu_skb_send_msg(&dev->mt76, skb, MCU_UNI_CMD_SUSPEND,
				     true);
}

static int
mt7615_mcu_set_suspend_mode(struct mt7615_dev *dev,
			    struct ieee80211_vif *vif,
			    bool enable, u8 mdtim, bool wow_suspend)
{
	struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;
	struct {
		struct {
			u8 bss_idx;
			u8 pad[3];
		} __packed hdr;
		struct mt7615_suspend_tlv suspend_tlv;
	} req = {
		.hdr = {
			.bss_idx = mvif->idx,
		},
		.suspend_tlv = {
			.tag = cpu_to_le16(UNI_SUSPEND_MODE_SETTING),
			.len = cpu_to_le16(sizeof(struct mt7615_suspend_tlv)),
			.enable = enable,
			.mdtim = mdtim,
			.wow_suspend = wow_suspend,
		},
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_UNI_CMD_SUSPEND, &req,
				 sizeof(req), true);
}

static int
mt7615_mcu_set_gtk_rekey(struct mt7615_dev *dev,
			 struct ieee80211_vif *vif,
			 bool suspend)
{
	struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;
	struct {
		struct {
			u8 bss_idx;
			u8 pad[3];
		} __packed hdr;
		struct mt7615_gtk_rekey_tlv gtk_tlv;
	} __packed req = {
		.hdr = {
			.bss_idx = mvif->idx,
		},
		.gtk_tlv = {
			.tag = cpu_to_le16(UNI_OFFLOAD_OFFLOAD_GTK_REKEY),
			.len = cpu_to_le16(sizeof(struct mt7615_gtk_rekey_tlv)),
			.rekey_mode = !suspend,
		},
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_UNI_CMD_OFFLOAD, &req,
				 sizeof(req), true);
}

static int
mt7615_mcu_set_arp_filter(struct mt7615_dev *dev, struct ieee80211_vif *vif,
			  bool suspend)
{
	struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;
	struct {
		struct {
			u8 bss_idx;
			u8 pad[3];
		} __packed hdr;
		struct mt7615_arpns_tlv arpns;
	} req = {
		.hdr = {
			.bss_idx = mvif->idx,
		},
		.arpns = {
			.tag = cpu_to_le16(UNI_OFFLOAD_OFFLOAD_ARP),
			.len = cpu_to_le16(sizeof(struct mt7615_arpns_tlv)),
			.mode = suspend,
		},
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_UNI_CMD_OFFLOAD, &req,
				 sizeof(req), true);
}

void mt7615_mcu_set_suspend_iter(void *priv, u8 *mac,
				 struct ieee80211_vif *vif)
{
	struct mt7615_phy *phy = priv;
	bool suspend = test_bit(MT76_STATE_SUSPEND, &phy->mt76->state);
	struct ieee80211_hw *hw = phy->mt76->hw;
	struct cfg80211_wowlan *wowlan = hw->wiphy->wowlan_config;
	int i;

	mt7615_mcu_set_gtk_rekey(phy->dev, vif, suspend);
	mt7615_mcu_set_arp_filter(phy->dev, vif, suspend);

	mt7615_mcu_set_suspend_mode(phy->dev, vif, suspend, 1, true);

	for (i = 0; i < wowlan->n_patterns; i++)
		mt7615_mcu_set_wow_pattern(phy->dev, vif, i, suspend,
					   &wowlan->patterns[i]);
	mt7615_mcu_set_wow_ctrl(phy, vif, suspend, wowlan);
}

static void
mt7615_mcu_key_iter(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		    struct ieee80211_sta *sta, struct ieee80211_key_conf *key,
		    void *data)
{
	struct mt7615_gtk_rekey_tlv *gtk_tlv = data;
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

int mt7615_mcu_update_gtk_rekey(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif,
				struct cfg80211_gtk_rekey_data *key)
{
	struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;
	struct mt7615_dev *dev = mt7615_hw_dev(hw);
	struct mt7615_gtk_rekey_tlv *gtk_tlv;
	struct sk_buff *skb;
	struct {
		u8 bss_idx;
		u8 pad[3];
	} __packed hdr = {
		.bss_idx = mvif->idx,
	};

	skb = mt76_mcu_msg_alloc(&dev->mt76, NULL,
				 sizeof(hdr) + sizeof(*gtk_tlv));
	if (!skb)
		return -ENOMEM;

	skb_put_data(skb, &hdr, sizeof(hdr));
	gtk_tlv = (struct mt7615_gtk_rekey_tlv *)skb_put(skb,
							 sizeof(*gtk_tlv));
	gtk_tlv->tag = cpu_to_le16(UNI_OFFLOAD_OFFLOAD_GTK_REKEY);
	gtk_tlv->len = cpu_to_le16(sizeof(*gtk_tlv));
	gtk_tlv->rekey_mode = 2;
	gtk_tlv->option = 1;

	rcu_read_lock();
	ieee80211_iter_keys_rcu(hw, vif, mt7615_mcu_key_iter, gtk_tlv);
	rcu_read_unlock();

	memcpy(gtk_tlv->kek, key->kek, NL80211_KEK_LEN);
	memcpy(gtk_tlv->kck, key->kck, NL80211_KCK_LEN);
	memcpy(gtk_tlv->replay_ctr, key->replay_ctr, NL80211_REPLAY_CTR_LEN);

	return mt76_mcu_skb_send_msg(&dev->mt76, skb, MCU_UNI_CMD_OFFLOAD,
				     true);
}
#endif /* CONFIG_PM */

int mt7615_mcu_set_roc(struct mt7615_phy *phy, struct ieee80211_vif *vif,
		       struct ieee80211_channel *chan, int duration)
{
	struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;
	struct mt7615_dev *dev = phy->dev;
	struct mt7615_roc_tlv req = {
		.bss_idx = mvif->idx,
		.active = !chan,
		.max_interval = cpu_to_le32(duration),
		.primary_chan = chan ? chan->hw_value : 0,
		.band = chan ? chan->band : 0,
		.req_type = 2,
	};

	phy->roc_grant = false;

	return mt76_mcu_send_msg(&dev->mt76, MCU_CMD_SET_ROC, &req,
				 sizeof(req), false);
}

int mt7615_mcu_update_arp_filter(struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif,
				 struct ieee80211_bss_conf *info)
{
	struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;
	struct mt7615_dev *dev = mt7615_hw_dev(hw);
	struct sk_buff *skb;
	int i, len = min_t(int, info->arp_addr_cnt,
			   IEEE80211_BSS_ARP_ADDR_LIST_LEN);
	struct {
		struct {
			u8 bss_idx;
			u8 pad[3];
		} __packed hdr;
		struct mt7615_arpns_tlv arp;
	} req_hdr = {
		.hdr = {
			.bss_idx = mvif->idx,
		},
		.arp = {
			.tag = cpu_to_le16(UNI_OFFLOAD_OFFLOAD_ARP),
			.len = cpu_to_le16(sizeof(struct mt7615_arpns_tlv)),
			.ips_num = len,
			.mode = 2,  /* update */
			.option = 1,
		},
	};

	if (!mt7615_firmware_offload(dev))
		return 0;

	skb = mt76_mcu_msg_alloc(&dev->mt76, NULL,
				 sizeof(req_hdr) + len * sizeof(__be32));
	if (!skb)
		return -ENOMEM;

	skb_put_data(skb, &req_hdr, sizeof(req_hdr));
	for (i = 0; i < len; i++) {
		u8 *addr = (u8 *)skb_put(skb, sizeof(__be32));

		memcpy(addr, &info->arp_addr_list[i], sizeof(__be32));
	}

	return mt76_mcu_skb_send_msg(&dev->mt76, skb, MCU_UNI_CMD_OFFLOAD,
				     true);
}

int mt7615_mcu_set_p2p_oppps(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif)
{
	struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;
	int ct_window = vif->bss_conf.p2p_noa_attr.oppps_ctwindow;
	struct mt7615_dev *dev = mt7615_hw_dev(hw);
	struct {
		__le32 ct_win;
		u8 bss_idx;
		u8 rsv[3];
	} __packed req = {
		.ct_win = cpu_to_le32(ct_window),
		.bss_idx = mvif->idx,
	};

	if (!mt7615_firmware_offload(dev))
		return -ENOTSUPP;

	return mt76_mcu_send_msg(&dev->mt76, MCU_CMD_SET_P2P_OPPPS, &req,
				 sizeof(req), false);
}

u32 mt7615_mcu_reg_rr(struct mt76_dev *dev, u32 offset)
{
	struct {
		__le32 addr;
		__le32 val;
	} __packed req = {
		.addr = cpu_to_le32(offset),
	};

	return mt76_mcu_send_msg(dev, MCU_CMD_REG_READ, &req, sizeof(req),
				 true);
}
EXPORT_SYMBOL_GPL(mt7615_mcu_reg_rr);

void mt7615_mcu_reg_wr(struct mt76_dev *dev, u32 offset, u32 val)
{
	struct {
		__le32 addr;
		__le32 val;
	} __packed req = {
		.addr = cpu_to_le32(offset),
		.val = cpu_to_le32(val),
	};

	mt76_mcu_send_msg(dev, MCU_CMD_REG_WRITE, &req, sizeof(req), false);
}
EXPORT_SYMBOL_GPL(mt7615_mcu_reg_wr);
