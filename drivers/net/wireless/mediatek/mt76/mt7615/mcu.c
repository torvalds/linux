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

#define MT7615_PATCH_ADDRESS		0x80000
#define MT7622_PATCH_ADDRESS		0x9c000

#define N9_REGION_NUM			2
#define CR4_REGION_NUM			1

#define IMG_CRC_LEN			4

#define FW_FEATURE_SET_ENCRYPT		BIT(0)
#define FW_FEATURE_SET_KEY_IDX		GENMASK(2, 1)

#define DL_MODE_ENCRYPT			BIT(0)
#define DL_MODE_KEY_IDX			GENMASK(2, 1)
#define DL_MODE_RESET_SEC_IV		BIT(3)
#define DL_MODE_WORKING_PDA_CR4		BIT(4)
#define DL_MODE_NEED_RSP		BIT(31)

#define FW_START_OVERRIDE		BIT(0)
#define FW_START_WORKING_PDA_CR4	BIT(2)

static int __mt7615_mcu_msg_send(struct mt7615_dev *dev, struct sk_buff *skb,
				 int cmd, int *wait_seq)
{
	struct mt7615_mcu_txd *mcu_txd;
	u8 seq, q_idx, pkt_fmt;
	enum mt76_txq_id qid;
	u32 val;
	__le32 *txd;

	seq = ++dev->mt76.mcu.msg_seq & 0xf;
	if (!seq)
		seq = ++dev->mt76.mcu.msg_seq & 0xf;

	mcu_txd = (struct mt7615_mcu_txd *)skb_push(skb,
		   sizeof(struct mt7615_mcu_txd));
	memset(mcu_txd, 0, sizeof(struct mt7615_mcu_txd));

	if (cmd != -MCU_CMD_FW_SCATTER) {
		q_idx = MT_TX_MCU_PORT_RX_Q0;
		pkt_fmt = MT_TX_TYPE_CMD;
	} else {
		q_idx = MT_TX_MCU_PORT_RX_FWDL;
		pkt_fmt = MT_TX_TYPE_FW;
	}

	txd = mcu_txd->txd;

	val = FIELD_PREP(MT_TXD0_TX_BYTES, skb->len) |
	      FIELD_PREP(MT_TXD0_P_IDX, MT_TX_PORT_IDX_MCU) |
	      FIELD_PREP(MT_TXD0_Q_IDX, q_idx);
	txd[0] = cpu_to_le32(val);

	val = MT_TXD1_LONG_FORMAT |
	      FIELD_PREP(MT_TXD1_HDR_FORMAT, MT_HDR_FORMAT_CMD) |
	      FIELD_PREP(MT_TXD1_PKT_FMT, pkt_fmt);
	txd[1] = cpu_to_le32(val);

	mcu_txd->len = cpu_to_le16(skb->len - sizeof(mcu_txd->txd));
	mcu_txd->pq_id = cpu_to_le16(MCU_PQ_ID(MT_TX_PORT_IDX_MCU, q_idx));
	mcu_txd->pkt_type = MCU_PKT_ID;
	mcu_txd->seq = seq;

	if (cmd < 0) {
		mcu_txd->set_query = MCU_Q_NA;
		mcu_txd->cid = -cmd;
	} else {
		mcu_txd->cid = MCU_CMD_EXT_CID;
		mcu_txd->set_query = MCU_Q_SET;
		mcu_txd->ext_cid = cmd;
		mcu_txd->ext_cid_ack = 1;
	}
	mcu_txd->s2d_index = MCU_S2D_H2N;

	if (wait_seq)
		*wait_seq = seq;

	if (test_bit(MT76_STATE_MCU_RUNNING, &dev->mphy.state))
		qid = MT_TXQ_MCU;
	else
		qid = MT_TXQ_FWDL;

	return mt76_tx_queue_skb_raw(dev, qid, skb, 0);
}

static int
mt7615_mcu_parse_response(struct mt7615_dev *dev, int cmd,
			  struct sk_buff *skb, int seq)
{
	struct mt7615_mcu_rxd *rxd = (struct mt7615_mcu_rxd *)skb->data;
	int ret = 0;

	if (seq != rxd->seq)
		return -EAGAIN;

	switch (cmd) {
	case -MCU_CMD_PATCH_SEM_CONTROL:
		skb_pull(skb, sizeof(*rxd) - 4);
		ret = *skb->data;
		break;
	case MCU_EXT_CMD_GET_TEMP:
		skb_pull(skb, sizeof(*rxd));
		ret = le32_to_cpu(*(__le32 *)skb->data);
		break;
	default:
		break;
	}
	dev_kfree_skb(skb);

	return ret;
}

static int
mt7615_mcu_msg_send(struct mt76_dev *mdev, int cmd, const void *data,
		    int len, bool wait_resp)
{
	struct mt7615_dev *dev = container_of(mdev, struct mt7615_dev, mt76);
	unsigned long expires = jiffies + 20 * HZ;
	struct sk_buff *skb;
	int ret, seq;

	skb = mt7615_mcu_msg_alloc(data, len);
	if (!skb)
		return -ENOMEM;

	mutex_lock(&mdev->mcu.mutex);

	ret = __mt7615_mcu_msg_send(dev, skb, cmd, &seq);
	if (ret)
		goto out;

	while (wait_resp) {
		skb = mt76_mcu_get_response(mdev, expires);
		if (!skb) {
			dev_err(mdev->dev, "Message %d (seq %d) timeout\n",
				cmd, seq);
			ret = -ETIMEDOUT;
			break;
		}

		ret = mt7615_mcu_parse_response(dev, cmd, skb, seq);
		if (ret != -EAGAIN)
			break;
	}

out:
	mutex_unlock(&mdev->mcu.mutex);

	return ret;
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
mt7615_mcu_rx_unsolicited_event(struct mt7615_dev *dev, struct sk_buff *skb)
{
	struct mt7615_mcu_rxd *rxd = (struct mt7615_mcu_rxd *)skb->data;

	switch (rxd->eid) {
	case MCU_EVENT_EXT:
		mt7615_mcu_rx_ext_event(dev, skb);
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

	return __mt76_mcu_send_msg(&dev->mt76, -MCU_CMD_TARGET_ADDRESS_LEN_REQ,
				   &req, sizeof(req), true);
}

static int mt7615_mcu_send_firmware(struct mt7615_dev *dev, const void *data,
				    int len)
{
	int ret = 0, cur_len;

	while (len > 0) {
		cur_len = min_t(int, 4096 - sizeof(struct mt7615_mcu_txd),
				len);

		ret = __mt76_mcu_send_msg(&dev->mt76, -MCU_CMD_FW_SCATTER,
					  data, cur_len, false);
		if (ret)
			break;

		data += cur_len;
		len -= cur_len;
		mt76_queue_tx_cleanup(dev, MT_TXQ_FWDL, false);
	}

	return ret;
}

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

	return __mt76_mcu_send_msg(&dev->mt76, -MCU_CMD_FW_START_REQ,
				   &req, sizeof(req), true);
}

static int mt7615_mcu_restart(struct mt76_dev *dev)
{
	return __mt76_mcu_send_msg(dev, -MCU_CMD_RESTART_DL_REQ, NULL,
				   0, true);
}

static int mt7615_mcu_patch_sem_ctrl(struct mt7615_dev *dev, bool get)
{
	struct {
		__le32 op;
	} req = {
		.op = cpu_to_le32(get ? PATCH_SEM_GET : PATCH_SEM_RELEASE),
	};

	return __mt76_mcu_send_msg(&dev->mt76, -MCU_CMD_PATCH_SEM_CONTROL,
				   &req, sizeof(req), true);
}

static int mt7615_mcu_start_patch(struct mt7615_dev *dev)
{
	struct {
		u8 check_crc;
		u8 reserved[3];
	} req = {
		.check_crc = 0,
	};

	return __mt76_mcu_send_msg(&dev->mt76, -MCU_CMD_PATCH_FINISH_REQ,
				   &req, sizeof(req), true);
}

static void mt7622_trigger_hif_int(struct mt7615_dev *dev, bool en)
{
	if (!is_mt7622(&dev->mt76))
		return;

	regmap_update_bits(dev->infracfg, MT_INFRACFG_MISC,
			   MT_INFRACFG_MISC_AP2CONN_WAKE,
			   !en * MT_INFRACFG_MISC_AP2CONN_WAKE);
}

static int mt7615_driver_own(struct mt7615_dev *dev)
{
	mt76_wr(dev, MT_CFG_LPCR_HOST, MT_CFG_LPCR_HOST_DRV_OWN);

	mt7622_trigger_hif_int(dev, true);
	if (!mt76_poll_msec(dev, MT_CFG_LPCR_HOST,
			    MT_CFG_LPCR_HOST_FW_OWN, 0, 3000)) {
		dev_err(dev->mt76.dev, "Timeout for driver own\n");
		return -EIO;
	}
	mt7622_trigger_hif_int(dev, false);

	return 0;
}

static int mt7615_firmware_own(struct mt7615_dev *dev)
{
	mt7622_trigger_hif_int(dev, true);

	mt76_wr(dev, MT_CFG_LPCR_HOST, MT_CFG_LPCR_HOST_FW_OWN);

	if (is_mt7622(&dev->mt76) &&
	    !mt76_poll_msec(dev, MT_CFG_LPCR_HOST,
			    MT_CFG_LPCR_HOST_FW_OWN,
			    MT_CFG_LPCR_HOST_FW_OWN, 3000)) {
		dev_err(dev->mt76.dev, "Timeout for firmware own\n");
		return -EIO;
	}
	mt7622_trigger_hif_int(dev, false);

	return 0;
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

	ret = request_firmware(&fw, name, dev->mt76.dev);
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

	ret = mt7615_mcu_send_firmware(dev, fw->data + sizeof(*hdr), len);
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

		err = mt7615_mcu_send_firmware(dev, data + offset, len);
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

	ret = mt7615_mcu_start_firmware(dev, le32_to_cpu(hdr->addr),
					FW_START_OVERRIDE);
	if (ret) {
		dev_err(dev->mt76.dev, "Failed to start N9 firmware\n");
		goto out;
	}

	snprintf(dev->mt76.hw->wiphy->fw_version,
		 sizeof(dev->mt76.hw->wiphy->fw_version),
		 "%.10s-%.15s", hdr->fw_ver, hdr->build_date);

	if (!strncmp(hdr->fw_ver, "2.0", sizeof(hdr->fw_ver)))
		dev->fw_ver = MT7615_FIRMWARE_V2;
	else
		dev->fw_ver = MT7615_FIRMWARE_V1;

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

	return __mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_FW_LOG_2_HOST,
				   &data, sizeof(data), true);
}

int mt7615_mcu_init(struct mt7615_dev *dev)
{
	static const struct mt76_mcu_ops mt7615_mcu_ops = {
		.mcu_send_msg = mt7615_mcu_msg_send,
		.mcu_restart = mt7615_mcu_restart,
	};
	int ret;

	dev->mt76.mcu_ops = &mt7615_mcu_ops,

	ret = mt7615_driver_own(dev);
	if (ret)
		return ret;

	if (is_mt7622(&dev->mt76))
		ret = mt7622_load_firmware(dev);
	else
		ret = mt7615_load_firmware(dev);
	if (ret)
		return ret;

	mt76_queue_tx_cleanup(dev, MT_TXQ_FWDL, false);
	dev_dbg(dev->mt76.dev, "Firmware init done\n");
	set_bit(MT76_STATE_MCU_RUNNING, &dev->mphy.state);
	mt7615_mcu_fw_log_2_host(dev, 0);

	return 0;
}

void mt7615_mcu_exit(struct mt7615_dev *dev)
{
	__mt76_mcu_restart(&dev->mt76);
	mt7615_firmware_own(dev);
	skb_queue_purge(&dev->mt76.mcu.res_q);
}

int mt7615_mcu_set_eeprom(struct mt7615_dev *dev)
{
	struct {
		u8 buffer_mode;
		u8 pad;
		__le16 len;
	} __packed req_hdr = {
		.buffer_mode = 1,
	};
	int ret, len, eep_len;
	u8 *req, *eep = (u8 *)dev->mt76.eeprom.data;

	if (is_mt7622(&dev->mt76))
		eep_len = MT7622_EE_MAX - MT_EE_NIC_CONF_0;
	else
		eep_len = MT7615_EE_MAX - MT_EE_NIC_CONF_0;

	len = sizeof(req_hdr) + eep_len;
	req = kzalloc(len, GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	req_hdr.len = cpu_to_le16(eep_len);
	memcpy(req, &req_hdr, sizeof(req_hdr));
	memcpy(req + sizeof(req_hdr), eep + MT_EE_NIC_CONF_0, eep_len);

	ret = __mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_EFUSE_BUFFER_MODE,
				  req, len, true);
	kfree(req);

	return ret;
}

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

	return __mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_MAC_INIT_CTRL,
				   &req, sizeof(req), true);
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

	return __mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_PROTECT_CTRL,
				   &req, sizeof(req), true);
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

	return __mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_EDCA_UPDATE,
				   &req, sizeof(req), true);
}

int mt7615_mcu_ctrl_pm_state(struct mt7615_dev *dev, int band, int enter)
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
		.pm_state = (enter) ? ENTER_PM_STATE : EXIT_PM_STATE,
		.band_idx = band,
	};

	return __mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_PM_STATE_CTRL,
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
		bool band = !!(ext_phy->omac_mask & BIT(i));

		ADD_DBDC_ENTRY(DBDC_TYPE_BSS, i, band);
	}

	for (i = 0; i < 14; i++) {
		bool band = !!(ext_phy->omac_mask & BIT(0x11 + i));

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
	return __mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_DBDC_CTRL,
				   &req, sizeof(req), true);
}

int mt7615_mcu_set_dev_info(struct mt7615_dev *dev,
			    struct ieee80211_vif *vif, bool enable)
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

	memcpy(data.tlv.omac_addr, vif->addr, ETH_ALEN);
	return __mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_DEV_INFO_UPDATE,
				   &data, sizeof(data), true);
}

static void
mt7615_mcu_bss_info_omac_header(struct mt7615_vif *mvif, u8 *data,
				u32 conn_type)
{
	struct bss_info_omac *hdr = (struct bss_info_omac *)data;
	u8 idx;

	idx = mvif->omac_idx > EXT_BSSID_START ? HW_BSSID_0 : mvif->omac_idx;
	hdr->tag = cpu_to_le16(BSS_INFO_OMAC);
	hdr->len = cpu_to_le16(sizeof(struct bss_info_omac));
	hdr->hw_bss_idx = idx;
	hdr->omac_idx = mvif->omac_idx;
	hdr->band_idx = mvif->band_idx;
	hdr->conn_type = cpu_to_le32(conn_type);
}

static void
mt7615_mcu_bss_info_basic_header(struct ieee80211_vif *vif, u8 *data,
				 u32 net_type, u8 tx_wlan_idx,
				 bool enable)
{
	struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;
	struct bss_info_basic *hdr = (struct bss_info_basic *)data;

	hdr->tag = cpu_to_le16(BSS_INFO_BASIC);
	hdr->len = cpu_to_le16(sizeof(struct bss_info_basic));
	hdr->network_type = cpu_to_le32(net_type);
	hdr->active = enable;
	hdr->bcn_interval = cpu_to_le16(vif->bss_conf.beacon_int);
	memcpy(hdr->bssid, vif->bss_conf.bssid, ETH_ALEN);
	hdr->wmm_idx = mvif->wmm_idx;
	hdr->dtim_period = vif->bss_conf.dtim_period;
	hdr->bmc_tx_wlan_idx = tx_wlan_idx;
}

static void
mt7615_mcu_bss_info_ext_header(struct mt7615_vif *mvif, u8 *data)
{
/* SIFS 20us + 512 byte beacon tranmitted by 1Mbps (3906us) */
#define BCN_TX_ESTIMATE_TIME (4096 + 20)
	struct bss_info_ext_bss *hdr = (struct bss_info_ext_bss *)data;
	int ext_bss_idx, tsf_offset;

	ext_bss_idx = mvif->omac_idx - EXT_BSSID_START;
	if (ext_bss_idx < 0)
		return;

	hdr->tag = cpu_to_le16(BSS_INFO_EXT_BSS);
	hdr->len = cpu_to_le16(sizeof(struct bss_info_ext_bss));
	tsf_offset = ext_bss_idx * BCN_TX_ESTIMATE_TIME;
	hdr->mbss_tsf_offset = cpu_to_le32(tsf_offset);
}

int mt7615_mcu_set_bss_info(struct mt7615_dev *dev,
			    struct ieee80211_vif *vif, int en)
{
	struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;
	struct req_hdr {
		u8 bss_idx;
		u8 rsv0;
		__le16 tlv_num;
		u8 is_tlv_append;
		u8 rsv1[3];
	} __packed;
	int len = sizeof(struct req_hdr) + sizeof(struct bss_info_basic);
	int ret, i, features = BIT(BSS_INFO_BASIC), ntlv = 1;
	u32 conn_type = 0, net_type = NETWORK_INFRA;
	u8 *buf, *data, tx_wlan_idx = 0;
	struct req_hdr *hdr;

	if (en) {
		len += sizeof(struct bss_info_omac);
		features |= BIT(BSS_INFO_OMAC);
		if (mvif->omac_idx > EXT_BSSID_START) {
			len += sizeof(struct bss_info_ext_bss);
			features |= BIT(BSS_INFO_EXT_BSS);
			ntlv++;
		}
		ntlv++;
	}

	switch (vif->type) {
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_MESH_POINT:
		tx_wlan_idx = mvif->sta.wcid.idx;
		conn_type = CONNECTION_INFRA_AP;
		break;
	case NL80211_IFTYPE_STATION: {
		/* TODO: enable BSS_INFO_UAPSD & BSS_INFO_PM */
		if (en) {
			struct ieee80211_sta *sta;
			struct mt7615_sta *msta;

			rcu_read_lock();
			sta = ieee80211_find_sta(vif, vif->bss_conf.bssid);
			if (!sta) {
				rcu_read_unlock();
				return -EINVAL;
			}

			msta = (struct mt7615_sta *)sta->drv_priv;
			tx_wlan_idx = msta->wcid.idx;
			rcu_read_unlock();
		}
		conn_type = CONNECTION_INFRA_STA;
		break;
	}
	case NL80211_IFTYPE_ADHOC:
		conn_type = CONNECTION_IBSS_ADHOC;
		tx_wlan_idx = mvif->sta.wcid.idx;
		net_type = NETWORK_IBSS;
		break;
	default:
		WARN_ON(1);
		break;
	}

	buf = kzalloc(len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	hdr = (struct req_hdr *)buf;
	hdr->bss_idx = mvif->idx;
	hdr->tlv_num = cpu_to_le16(ntlv);
	hdr->is_tlv_append = 1;

	data = buf + sizeof(*hdr);
	for (i = 0; i < BSS_INFO_MAX_NUM; i++) {
		int tag = ffs(features & BIT(i)) - 1;

		switch (tag) {
		case BSS_INFO_OMAC:
			mt7615_mcu_bss_info_omac_header(mvif, data,
							conn_type);
			data += sizeof(struct bss_info_omac);
			break;
		case BSS_INFO_BASIC:
			mt7615_mcu_bss_info_basic_header(vif, data, net_type,
							 tx_wlan_idx, en);
			data += sizeof(struct bss_info_basic);
			break;
		case BSS_INFO_EXT_BSS:
			mt7615_mcu_bss_info_ext_header(mvif, data);
			data += sizeof(struct bss_info_ext_bss);
			break;
		default:
			break;
		}
	}

	ret = __mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_BSS_INFO_UPDATE,
				  buf, len, true);
	kfree(buf);

	return ret;
}

int mt7615_mcu_del_wtbl_all(struct mt7615_dev *dev)
{
	struct wtbl_req_hdr req = {
		.operation = WTBL_RESET_ALL,
	};

	return __mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_WTBL_UPDATE,
				   &req, sizeof(req), true);
}

static int
mt7615_mcu_send_sta_rec(struct mt7615_dev *dev, u8 *req, u8 *wreq,
			u8 wlen, bool enable)
{
	bool is_v1 = (dev->fw_ver == MT7615_FIRMWARE_V1);
	u32 slen = is_v1 ? wreq - req : wreq - req + wlen;
	int ret;

	if (is_v1 && !enable) {
		ret = __mt76_mcu_send_msg(&dev->mt76,
					  MCU_EXT_CMD_STA_REC_UPDATE,
					  req, slen, true);
		if (ret)
			return ret;

		return __mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_WTBL_UPDATE,
					   wreq, wlen, true);
	}

	if (is_v1) {
		ret = __mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_WTBL_UPDATE,
					  wreq, wlen, true);
		if (ret)
			return ret;
	}

	return __mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_STA_REC_UPDATE,
				   req, slen, true);
}

int mt7615_mcu_set_bmc(struct mt7615_dev *dev,
		       struct ieee80211_vif *vif, bool en)
{
	struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;
	struct {
		struct sta_req_hdr hdr;
		struct sta_rec_basic basic;
		u8 buf[MT7615_WTBL_UPDATE_MAX_SIZE];
	} __packed req = {
		.hdr = {
			.bss_idx = mvif->idx,
			.wlan_idx = mvif->sta.wcid.idx,
			.tlv_num = cpu_to_le16(1),
			.is_tlv_append = 1,
			.muar_idx = mvif->omac_idx,
		},
		.basic = {
			.tag = cpu_to_le16(STA_REC_BASIC),
			.len = cpu_to_le16(sizeof(struct sta_rec_basic)),
			.conn_type = cpu_to_le32(CONNECTION_INFRA_BC),
		},
	};
	struct sta_rec_wtbl *wtbl = NULL;
	struct wtbl_req_hdr *wtbl_hdr;
	struct wtbl_generic *wtbl_g;
	struct wtbl_rx *wtbl_rx;
	u8 *buf = req.buf;

	eth_broadcast_addr(req.basic.peer_addr);

	if (dev->fw_ver > MT7615_FIRMWARE_V1) {
		req.hdr.tlv_num = cpu_to_le16(2);
		wtbl = (struct sta_rec_wtbl *)buf;
		wtbl->tag = cpu_to_le16(STA_REC_WTBL);
		buf += sizeof(*wtbl);
	}

	wtbl_hdr = (struct wtbl_req_hdr *)buf;
	buf += sizeof(*wtbl_hdr);
	wtbl_hdr->wlan_idx = mvif->sta.wcid.idx;
	wtbl_hdr->operation = WTBL_RESET_AND_SET;

	if (en) {
		req.basic.conn_state = CONN_STATE_PORT_SECURE;
		req.basic.extra_info = cpu_to_le16(EXTRA_INFO_VER |
						   EXTRA_INFO_NEW);
	} else {
		req.basic.conn_state = CONN_STATE_DISCONNECT;
		req.basic.extra_info = cpu_to_le16(EXTRA_INFO_VER);
		goto out;
	}

	wtbl_g = (struct wtbl_generic *)buf;
	buf += sizeof(*wtbl_g);
	wtbl_g->tag = cpu_to_le16(WTBL_GENERIC);
	wtbl_g->len = cpu_to_le16(sizeof(*wtbl_g));
	wtbl_g->muar_idx = 0xe;
	eth_broadcast_addr(wtbl_g->peer_addr);

	wtbl_rx = (struct wtbl_rx *)buf;
	buf += sizeof(*wtbl_rx);
	wtbl_rx->tag = cpu_to_le16(WTBL_RX);
	wtbl_rx->len = cpu_to_le16(sizeof(*wtbl_rx));
	wtbl_rx->rv = 1;
	wtbl_rx->rca1 = 1;
	wtbl_rx->rca2 = 1;

	wtbl_hdr->tlv_num = cpu_to_le16(2);

out:
	if (wtbl)
		wtbl->len = cpu_to_le16(buf - (u8 *)wtbl_hdr);

	return mt7615_mcu_send_sta_rec(dev, (u8 *)&req, (u8 *)wtbl_hdr,
				       buf - (u8 *)wtbl_hdr, en);
}

int mt7615_mcu_set_sta(struct mt7615_dev *dev, struct ieee80211_vif *vif,
		       struct ieee80211_sta *sta, bool en)
{
	struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;
	struct mt7615_sta *msta = (struct mt7615_sta *)sta->drv_priv;

	struct {
		struct sta_req_hdr hdr;
		struct sta_rec_basic basic;
		u8 buf[MT7615_WTBL_UPDATE_MAX_SIZE];
	} __packed req = {
		.hdr = {
			.bss_idx = mvif->idx,
			.wlan_idx = msta->wcid.idx,
			.is_tlv_append = 1,
			.muar_idx = mvif->omac_idx,
		},
		.basic = {
			.tag = cpu_to_le16(STA_REC_BASIC),
			.len = cpu_to_le16(sizeof(struct sta_rec_basic)),
			.qos = sta->wme,
			.aid = cpu_to_le16(sta->aid),
		},
	};
	struct sta_rec_wtbl *wtbl = NULL;
	struct wtbl_req_hdr *wtbl_hdr;
	struct wtbl_generic *wtbl_g;
	struct wtbl_rx *wtbl_rx;
	u8 *buf = req.buf;
	u8 wtlv = 0, stlv = 1;

	memcpy(req.basic.peer_addr, sta->addr, ETH_ALEN);

	switch (vif->type) {
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_MESH_POINT:
		req.basic.conn_type = cpu_to_le32(CONNECTION_INFRA_STA);
		break;
	case NL80211_IFTYPE_STATION:
		req.basic.conn_type = cpu_to_le32(CONNECTION_INFRA_AP);
		break;
	case NL80211_IFTYPE_ADHOC:
		req.basic.conn_type = cpu_to_le32(CONNECTION_IBSS_ADHOC);
		break;
	default:
		WARN_ON(1);
		break;
	}

	if (en) {
		req.basic.conn_state = CONN_STATE_PORT_SECURE;
		req.basic.extra_info = cpu_to_le16(EXTRA_INFO_VER |
						   EXTRA_INFO_NEW);

		/* sta_rec ht */
		if (sta->ht_cap.ht_supported) {
			struct sta_rec_ht *sta_ht;

			sta_ht = (struct sta_rec_ht *)buf;
			buf += sizeof(*sta_ht);
			sta_ht->tag = cpu_to_le16(STA_REC_HT);
			sta_ht->len = cpu_to_le16(sizeof(*sta_ht));
			sta_ht->ht_cap = cpu_to_le16(sta->ht_cap.cap);
			stlv++;

			/* sta_rec vht */
			if (sta->vht_cap.vht_supported) {
				struct sta_rec_vht *sta_vht;

				sta_vht = (struct sta_rec_vht *)buf;
				buf += sizeof(*sta_vht);
				sta_vht->tag = cpu_to_le16(STA_REC_VHT);
				sta_vht->len = cpu_to_le16(sizeof(*sta_vht));
				sta_vht->vht_cap =
					cpu_to_le32(sta->vht_cap.cap);
				sta_vht->vht_rx_mcs_map =
					sta->vht_cap.vht_mcs.rx_mcs_map;
				sta_vht->vht_tx_mcs_map =
					sta->vht_cap.vht_mcs.tx_mcs_map;
				stlv++;
			}
		}
	} else {
		req.basic.conn_state = CONN_STATE_DISCONNECT;
		req.basic.extra_info = cpu_to_le16(EXTRA_INFO_VER);
	}

	/* wtbl */
	if (dev->fw_ver > MT7615_FIRMWARE_V1) {
		wtbl = (struct sta_rec_wtbl *)buf;
		wtbl->tag = cpu_to_le16(STA_REC_WTBL);
		buf += sizeof(*wtbl);
		stlv++;
	}

	wtbl_hdr = (struct wtbl_req_hdr *)buf;
	buf += sizeof(*wtbl_hdr);
	wtbl_hdr->wlan_idx = msta->wcid.idx;
	wtbl_hdr->operation = WTBL_RESET_AND_SET;

	if (!en)
		goto out;

	wtbl_g = (struct wtbl_generic *)buf;
	buf += sizeof(*wtbl_g);
	wtbl_g->tag = cpu_to_le16(WTBL_GENERIC);
	wtbl_g->len = cpu_to_le16(sizeof(*wtbl_g));
	wtbl_g->muar_idx = mvif->omac_idx;
	wtbl_g->qos = sta->wme;
	wtbl_g->partial_aid = cpu_to_le16(sta->aid);
	memcpy(wtbl_g->peer_addr, sta->addr, ETH_ALEN);
	wtlv++;

	wtbl_rx = (struct wtbl_rx *)buf;
	buf += sizeof(*wtbl_rx);
	wtbl_rx->tag = cpu_to_le16(WTBL_RX);
	wtbl_rx->len = cpu_to_le16(sizeof(*wtbl_rx));
	wtbl_rx->rv = 1;
	wtbl_rx->rca1 = vif->type != NL80211_IFTYPE_AP;
	wtbl_rx->rca2 = 1;
	wtlv++;

	/* wtbl ht */
	if (sta->ht_cap.ht_supported) {
		struct wtbl_ht *wtbl_ht;
		struct wtbl_raw *wtbl_raw;
		u32 val = 0, msk;

		wtbl_ht = (struct wtbl_ht *)buf;
		buf += sizeof(*wtbl_ht);
		wtbl_ht->tag = cpu_to_le16(WTBL_HT);
		wtbl_ht->len = cpu_to_le16(sizeof(*wtbl_ht));
		wtbl_ht->ht = 1;
		wtbl_ht->ldpc = sta->ht_cap.cap & IEEE80211_HT_CAP_LDPC_CODING;
		wtbl_ht->af = sta->ht_cap.ampdu_factor;
		wtbl_ht->mm = sta->ht_cap.ampdu_density;
		wtlv++;

		/* wtbl vht */
		if (sta->vht_cap.vht_supported) {
			struct wtbl_vht *wtbl_vht;

			wtbl_vht = (struct wtbl_vht *)buf;
			buf += sizeof(*wtbl_vht);
			wtbl_vht->tag = cpu_to_le16(WTBL_VHT);
			wtbl_vht->len = cpu_to_le16(sizeof(*wtbl_vht));
			wtbl_vht->vht = 1;
			wtbl_vht->ldpc = sta->vht_cap.cap &
					 IEEE80211_VHT_CAP_RXLDPC;
			wtlv++;

			if (sta->vht_cap.cap & IEEE80211_VHT_CAP_SHORT_GI_80)
				val |= MT_WTBL_W5_SHORT_GI_80;
			if (sta->vht_cap.cap & IEEE80211_VHT_CAP_SHORT_GI_160)
				val |= MT_WTBL_W5_SHORT_GI_160;
		}

		/* wtbl smps */
		if (sta->smps_mode == IEEE80211_SMPS_DYNAMIC) {
			struct wtbl_smps *wtbl_smps;

			wtbl_smps = (struct wtbl_smps *)buf;
			buf += sizeof(*wtbl_smps);
			wtbl_smps->tag = cpu_to_le16(WTBL_SMPS);
			wtbl_smps->len = cpu_to_le16(sizeof(*wtbl_smps));
			wtbl_smps->smps = 1;
			wtlv++;
		}

		/* sgi */
		msk = MT_WTBL_W5_SHORT_GI_20 | MT_WTBL_W5_SHORT_GI_40 |
			MT_WTBL_W5_SHORT_GI_80 | MT_WTBL_W5_SHORT_GI_160;

		if (sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_20)
			val |= MT_WTBL_W5_SHORT_GI_20;
		if (sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_40)
			val |= MT_WTBL_W5_SHORT_GI_40;

		wtbl_raw = (struct wtbl_raw *)buf;
		buf += sizeof(*wtbl_raw);
		wtbl_raw->tag = cpu_to_le16(WTBL_RAW_DATA);
		wtbl_raw->len = cpu_to_le16(sizeof(*wtbl_raw));
		wtbl_raw->wtbl_idx = 1;
		wtbl_raw->dw = 5;
		wtbl_raw->msk = cpu_to_le32(~msk);
		wtbl_raw->val = cpu_to_le32(val);
		wtlv++;
	}

out:
	if (wtbl)
		wtbl->len = cpu_to_le16(buf - (u8 *)wtbl_hdr);

	wtbl_hdr->tlv_num = cpu_to_le16(wtlv);
	req.hdr.tlv_num = cpu_to_le16(stlv);

	return mt7615_mcu_send_sta_rec(dev, (u8 *)&req, (u8 *)wtbl_hdr,
				       buf - (u8 *)wtbl_hdr, en);
}

int mt7615_mcu_set_bcn(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		       int en)
{
	struct mt7615_dev *dev = mt7615_hw_dev(hw);
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
		.enable = en,
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
			      0, NULL);
	memcpy(req.pkt + MT_TXD_SIZE, skb->data, skb->len);
	req.pkt_len = cpu_to_le16(MT_TXD_SIZE + skb->len);
	req.tim_ie_pos = cpu_to_le16(MT_TXD_SIZE + offs.tim_offset);
	if (offs.csa_counter_offs[0]) {
		u16 csa_offs;

		csa_offs = MT_TXD_SIZE + offs.csa_counter_offs[0] - 4;
		req.csa_ie_pos = cpu_to_le16(csa_offs);
		req.csa_cnt = skb->data[offs.csa_counter_offs[0]];
	}
	dev_kfree_skb(skb);

	return __mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_BCN_OFFLOAD,
				   &req, sizeof(req), true);
}

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

	return __mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_SET_RDD_CTRL,
				   &req, sizeof(req), true);
}

int mt7615_mcu_set_fcc5_lpn(struct mt7615_dev *dev, int val)
{
	struct {
		u16 tag;
		u16 min_lpn;
	} req = {
		.tag = 0x1,
		.min_lpn = val,
	};

	return __mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_SET_RDD_TH,
				   &req, sizeof(req), true);
}

int mt7615_mcu_set_pulse_th(struct mt7615_dev *dev,
			    const struct mt7615_dfs_pulse *pulse)
{
	struct {
		u16 tag;
		struct mt7615_dfs_pulse pulse;
	} req = {
		.tag = 0x3,
	};

	memcpy(&req.pulse, pulse, sizeof(*pulse));

	return __mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_SET_RDD_TH,
				   &req, sizeof(req), true);
}

int mt7615_mcu_set_radar_th(struct mt7615_dev *dev, int index,
			    const struct mt7615_dfs_pattern *pattern)
{
	struct {
		u16 tag;
		u16 radar_type;
		struct mt7615_dfs_pattern pattern;
	} req = {
		.tag = 0x2,
		.radar_type = index,
	};

	memcpy(&req.pattern, pattern, sizeof(*pattern));

	return __mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_SET_RDD_TH,
				   &req, sizeof(req), true);
}

int mt7615_mcu_rdd_send_pattern(struct mt7615_dev *dev)
{
	struct {
		u8 pulse_num;
		u8 rsv[3];
		struct {
			u32 start_time;
			u16 width;
			s16 power;
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
		req.pattern[i].width = dev->radar_pattern.width;
		req.pattern[i].power = dev->radar_pattern.power;
		req.pattern[i].start_time = start_time +
					    i * dev->radar_pattern.period;
	}

	return __mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_SET_RDD_PATTERN,
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

	switch (chandef->width) {
	case NL80211_CHAN_WIDTH_40:
		req.bw = CMD_CBW_40MHZ;
		break;
	case NL80211_CHAN_WIDTH_80:
		req.bw = CMD_CBW_80MHZ;
		break;
	case NL80211_CHAN_WIDTH_80P80:
		req.bw = CMD_CBW_8080MHZ;
		break;
	case NL80211_CHAN_WIDTH_160:
		req.bw = CMD_CBW_160MHZ;
		break;
	case NL80211_CHAN_WIDTH_5:
		req.bw = CMD_CBW_5MHZ;
		break;
	case NL80211_CHAN_WIDTH_10:
		req.bw = CMD_CBW_10MHZ;
		break;
	case NL80211_CHAN_WIDTH_20_NOHT:
	case NL80211_CHAN_WIDTH_20:
	default:
		req.bw = CMD_CBW_20MHZ;
		break;
	}

	mt7615_mcu_set_txpower_sku(phy, req.txpower_sku);

	return __mt76_mcu_send_msg(&dev->mt76, cmd, &req, sizeof(req), true);
}

int mt7615_mcu_set_tx_ba(struct mt7615_dev *dev,
			 struct ieee80211_ampdu_params *params,
			 bool add)
{
	struct mt7615_sta *msta = (struct mt7615_sta *)params->sta->drv_priv;
	struct mt7615_vif *mvif = msta->vif;
	struct {
		struct sta_req_hdr hdr;
		struct sta_rec_ba ba;
		u8 buf[MT7615_WTBL_UPDATE_MAX_SIZE];
	} __packed req = {
		.hdr = {
			.bss_idx = mvif->idx,
			.wlan_idx = msta->wcid.idx,
			.tlv_num = cpu_to_le16(1),
			.is_tlv_append = 1,
			.muar_idx = mvif->omac_idx,
		},
		.ba = {
			.tag = cpu_to_le16(STA_REC_BA),
			.len = cpu_to_le16(sizeof(struct sta_rec_ba)),
			.tid = params->tid,
			.ba_type = MT_BA_TYPE_ORIGINATOR,
			.amsdu = params->amsdu,
			.ba_en = add << params->tid,
			.ssn = cpu_to_le16(params->ssn),
			.winsize = cpu_to_le16(params->buf_size),
		},
	};
	struct sta_rec_wtbl *wtbl = NULL;
	struct wtbl_req_hdr *wtbl_hdr;
	struct wtbl_ba *wtbl_ba;
	u8 *buf = req.buf;

	if (dev->fw_ver > MT7615_FIRMWARE_V1) {
		req.hdr.tlv_num = cpu_to_le16(2);
		wtbl = (struct sta_rec_wtbl *)buf;
		wtbl->tag = cpu_to_le16(STA_REC_WTBL);
		buf += sizeof(*wtbl);
	}

	wtbl_hdr = (struct wtbl_req_hdr *)buf;
	buf += sizeof(*wtbl_hdr);
	wtbl_hdr->wlan_idx = msta->wcid.idx;
	wtbl_hdr->operation = WTBL_SET;
	wtbl_hdr->tlv_num = cpu_to_le16(1);

	wtbl_ba = (struct wtbl_ba *)buf;
	buf += sizeof(*wtbl_ba);
	wtbl_ba->tag = cpu_to_le16(WTBL_BA);
	wtbl_ba->len = cpu_to_le16(sizeof(*wtbl_ba));
	wtbl_ba->tid = params->tid;
	wtbl_ba->ba_type = MT_BA_TYPE_ORIGINATOR;
	wtbl_ba->sn = add ? cpu_to_le16(params->ssn) : 0;
	wtbl_ba->ba_en = add;

	if (add) {
		u8 idx, ba_range[] = { 4, 8, 12, 24, 36, 48, 54, 64 };

		for (idx = 7; idx > 0; idx--) {
			if (params->buf_size >= ba_range[idx])
				break;
		}

		wtbl_ba->ba_winsize_idx = idx;
	}

	if (wtbl)
		wtbl->len = cpu_to_le16(buf - (u8 *)wtbl_hdr);

	return mt7615_mcu_send_sta_rec(dev, (u8 *)&req, (u8 *)wtbl_hdr,
				       buf - (u8 *)wtbl_hdr, true);
}

int mt7615_mcu_set_rx_ba(struct mt7615_dev *dev,
			 struct ieee80211_ampdu_params *params,
			 bool add)
{
	struct mt7615_sta *msta = (struct mt7615_sta *)params->sta->drv_priv;
	struct mt7615_vif *mvif = msta->vif;
	struct {
		struct sta_req_hdr hdr;
		struct sta_rec_ba ba;
		u8 buf[MT7615_WTBL_UPDATE_MAX_SIZE];
	} __packed req = {
		.hdr = {
			.bss_idx = mvif->idx,
			.wlan_idx = msta->wcid.idx,
			.tlv_num = cpu_to_le16(1),
			.is_tlv_append = 1,
			.muar_idx = mvif->omac_idx,
		},
		.ba = {
			.tag = cpu_to_le16(STA_REC_BA),
			.len = cpu_to_le16(sizeof(struct sta_rec_ba)),
			.tid = params->tid,
			.ba_type = MT_BA_TYPE_RECIPIENT,
			.amsdu = params->amsdu,
			.ba_en = add << params->tid,
			.ssn = cpu_to_le16(params->ssn),
			.winsize = cpu_to_le16(params->buf_size),
		},
	};
	struct sta_rec_wtbl *wtbl = NULL;
	struct wtbl_req_hdr *wtbl_hdr;
	struct wtbl_ba *wtbl_ba;
	u8 *buf = req.buf;

	if (dev->fw_ver > MT7615_FIRMWARE_V1) {
		req.hdr.tlv_num = cpu_to_le16(2);
		wtbl = (struct sta_rec_wtbl *)buf;
		wtbl->tag = cpu_to_le16(STA_REC_WTBL);
		buf += sizeof(*wtbl);
	}

	wtbl_hdr = (struct wtbl_req_hdr *)buf;
	buf += sizeof(*wtbl_hdr);
	wtbl_hdr->wlan_idx = msta->wcid.idx;
	wtbl_hdr->operation = WTBL_SET;
	wtbl_hdr->tlv_num = cpu_to_le16(1);

	wtbl_ba = (struct wtbl_ba *)buf;
	buf += sizeof(*wtbl_ba);
	wtbl_ba->tag = cpu_to_le16(WTBL_BA);
	wtbl_ba->len = cpu_to_le16(sizeof(*wtbl_ba));
	wtbl_ba->tid = params->tid;
	wtbl_ba->ba_type = MT_BA_TYPE_RECIPIENT;
	wtbl_ba->rst_ba_tid = params->tid;
	wtbl_ba->rst_ba_sel = RST_BA_MAC_TID_MATCH;
	wtbl_ba->rst_ba_sb = 1;

	memcpy(wtbl_ba->peer_addr, params->sta->addr, ETH_ALEN);

	if (wtbl)
		wtbl->len = cpu_to_le16(buf - (u8 *)wtbl_hdr);

	return mt7615_mcu_send_sta_rec(dev, (u8 *)&req, (u8 *)wtbl_hdr,
				       buf - (u8 *)wtbl_hdr, add);
}

int mt7615_mcu_get_temperature(struct mt7615_dev *dev, int index)
{
	struct {
		u8 action;
		u8 rsv[3];
	} req = {
		.action = index,
	};

	return __mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_GET_TEMP, &req,
				   sizeof(req), true);
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

	return __mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_TX_POWER_FEATURE_CTRL, &req,
				   sizeof(req), true);
}
