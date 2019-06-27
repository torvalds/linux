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

#define MCU_PATCH_ADDRESS		0x80000

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

	seq = ++dev->mt76.mmio.mcu.msg_seq & 0xf;
	if (!seq)
		seq = ++dev->mt76.mmio.mcu.msg_seq & 0xf;

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

	val = FIELD_PREP(MT_TXD0_TX_BYTES, cpu_to_le16(skb->len)) |
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

	if (test_bit(MT76_STATE_MCU_RUNNING, &dev->mt76.state))
		qid = MT_TXQ_MCU;
	else
		qid = MT_TXQ_FWDL;

	return mt76_tx_queue_skb_raw(dev, qid, skb, 0);
}

static int
mt7615_mcu_msg_send(struct mt76_dev *mdev, int cmd, const void *data,
		    int len, bool wait_resp)
{
	struct mt7615_dev *dev = container_of(mdev, struct mt7615_dev, mt76);
	unsigned long expires = jiffies + 10 * HZ;
	struct mt7615_mcu_rxd *rxd;
	struct sk_buff *skb;
	int ret, seq;

	skb = mt7615_mcu_msg_alloc(data, len);
	if (!skb)
		return -ENOMEM;

	mutex_lock(&mdev->mmio.mcu.mutex);

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

		rxd = (struct mt7615_mcu_rxd *)skb->data;
		if (seq != rxd->seq)
			continue;

		if (cmd == -MCU_CMD_PATCH_SEM_CONTROL) {
			skb_pull(skb, sizeof(*rxd) - 4);
			ret = *skb->data;
		}
		dev_kfree_skb(skb);
		break;
	}

out:
	mutex_unlock(&mdev->mmio.mcu.mutex);

	return ret;
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

static int mt7615_driver_own(struct mt7615_dev *dev)
{
	mt76_wr(dev, MT_CFG_LPCR_HOST, MT_CFG_LPCR_HOST_DRV_OWN);
	if (!mt76_poll_msec(dev, MT_CFG_LPCR_HOST,
			    MT_CFG_LPCR_HOST_FW_OWN, 0, 500)) {
		dev_err(dev->mt76.dev, "Timeout for driver own\n");
		return -EIO;
	}

	return 0;
}

static int mt7615_load_patch(struct mt7615_dev *dev)
{
	const struct firmware *fw;
	const struct mt7615_patch_hdr *hdr;
	const char *firmware = MT7615_ROM_PATCH;
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

	ret = request_firmware(&fw, firmware, dev->mt76.dev);
	if (ret)
		return ret;

	if (!fw || !fw->data || fw->size < sizeof(*hdr)) {
		dev_err(dev->mt76.dev, "Invalid firmware\n");
		ret = -EINVAL;
		goto out;
	}

	hdr = (const struct mt7615_patch_hdr *)(fw->data);

	dev_info(dev->mt76.dev, "HW/SW Version: 0x%x, Build Time: %.16s\n",
		 be32_to_cpu(hdr->hw_sw_ver), hdr->build_date);

	len = fw->size - sizeof(*hdr);

	ret = mt7615_mcu_init_download(dev, MCU_PATCH_ADDRESS, len,
				       DL_MODE_NEED_RSP);
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

static u32 gen_dl_mode(u8 feature_set, bool is_cr4)
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

static int mt7615_load_ram(struct mt7615_dev *dev)
{
	const struct firmware *fw;
	const struct mt7615_fw_trailer *hdr;
	const char *n9_firmware = MT7615_FIRMWARE_N9;
	const char *cr4_firmware = MT7615_FIRMWARE_CR4;
	u32 n9_ilm_addr, offset;
	int i, ret;

	ret = request_firmware(&fw, n9_firmware, dev->mt76.dev);
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

	n9_ilm_addr = le32_to_cpu(hdr->addr);

	for (offset = 0, i = 0; i < N9_REGION_NUM; i++) {
		u32 len, addr, mode;

		len = le32_to_cpu(hdr[i].len) + IMG_CRC_LEN;
		addr = le32_to_cpu(hdr[i].addr);
		mode = gen_dl_mode(hdr[i].feature_set, false);

		ret = mt7615_mcu_init_download(dev, addr, len, mode);
		if (ret) {
			dev_err(dev->mt76.dev, "Download request failed\n");
			goto out;
		}

		ret = mt7615_mcu_send_firmware(dev, fw->data + offset, len);
		if (ret) {
			dev_err(dev->mt76.dev, "Failed to send firmware to device\n");
			goto out;
		}

		offset += len;
	}

	ret = mt7615_mcu_start_firmware(dev, n9_ilm_addr, FW_START_OVERRIDE);
	if (ret) {
		dev_err(dev->mt76.dev, "Failed to start N9 firmware\n");
		goto out;
	}

	release_firmware(fw);

	ret = request_firmware(&fw, cr4_firmware, dev->mt76.dev);
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

	for (offset = 0, i = 0; i < CR4_REGION_NUM; i++) {
		u32 len, addr, mode;

		len = le32_to_cpu(hdr[i].len) + IMG_CRC_LEN;
		addr = le32_to_cpu(hdr[i].addr);
		mode = gen_dl_mode(hdr[i].feature_set, true);

		ret = mt7615_mcu_init_download(dev, addr, len, mode);
		if (ret) {
			dev_err(dev->mt76.dev, "Download request failed\n");
			goto out;
		}

		ret = mt7615_mcu_send_firmware(dev, fw->data + offset, len);
		if (ret) {
			dev_err(dev->mt76.dev, "Failed to send firmware to device\n");
			goto out;
		}

		offset += len;
	}

	ret = mt7615_mcu_start_firmware(dev, 0, FW_START_WORKING_PDA_CR4);
	if (ret)
		dev_err(dev->mt76.dev, "Failed to start CR4 firmware\n");

out:
	release_firmware(fw);

	return ret;
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

	ret = mt7615_load_patch(dev);
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

	dev_dbg(dev->mt76.dev, "Firmware init done\n");

	return 0;
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

	ret = mt7615_load_firmware(dev);
	if (ret)
		return ret;

	set_bit(MT76_STATE_MCU_RUNNING, &dev->mt76.state);

	return 0;
}

void mt7615_mcu_exit(struct mt7615_dev *dev)
{
	__mt76_mcu_restart(&dev->mt76);
	mt76_wr(dev, MT_CFG_LPCR_HOST, MT_CFG_LPCR_HOST_FW_OWN);
	skb_queue_purge(&dev->mt76.mmio.mcu.res_q);
}

int mt7615_mcu_set_eeprom(struct mt7615_dev *dev)
{
	struct {
		u8 buffer_mode;
		u8 pad;
		u16 len;
	} __packed req_hdr = {
		.buffer_mode = 1,
		.len = __MT_EE_MAX - MT_EE_NIC_CONF_0,
	};
	int ret, len = sizeof(req_hdr) + __MT_EE_MAX - MT_EE_NIC_CONF_0;
	u8 *req, *eep = (u8 *)dev->mt76.eeprom.data;

	req = kzalloc(len, GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	memcpy(req, &req_hdr, sizeof(req_hdr));
	memcpy(req + sizeof(req_hdr), eep + MT_EE_NIC_CONF_0,
	       __MT_EE_MAX - MT_EE_NIC_CONF_0);

	ret = __mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_EFUSE_BUFFER_MODE,
				  req, len, true);
	kfree(req);

	return ret;
}

int mt7615_mcu_init_mac(struct mt7615_dev *dev)
{
	struct {
		u8 enable;
		u8 band;
		u8 rsv[2];
	} __packed req = {
		.enable = 1,
		.band = 0,
	};

	return __mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_MAC_INIT_CTRL,
				   &req, sizeof(req), true);
}

int mt7615_mcu_set_rts_thresh(struct mt7615_dev *dev, u32 val)
{
	struct {
		u8 prot_idx;
		u8 band;
		u8 rsv[2];
		__le32 len_thresh;
		__le32 pkt_thresh;
	} __packed req = {
		.prot_idx = 1,
		.band = 0,
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
		.valid = WMM_AIFS_SET | WMM_TXOP_SET,
		.aifs = params->aifs,
		.txop = cpu_to_le16(params->txop),
	};

	if (params->cw_min) {
		req.valid |= WMM_CW_MIN_SET;
		req.cw_min = params->cw_min;
	}
	if (params->cw_max) {
		req.valid |= WMM_CW_MAX_SET;
		req.cw_max = cpu_to_le16(params->cw_max);
	}

	return __mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_EDCA_UPDATE,
				   &req, sizeof(req), true);
}

int mt7615_mcu_ctrl_pm_state(struct mt7615_dev *dev, int enter)
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
		.band_idx = 0,
	};

	return __mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_PM_STATE_CTRL,
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

static enum mt7615_cipher_type
mt7615_get_key_info(struct ieee80211_key_conf *key, u8 *key_data)
{
	if (!key || key->keylen > 32)
		return MT_CIPHER_NONE;

	memcpy(key_data, key->key, key->keylen);

	switch (key->cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
		return MT_CIPHER_WEP40;
	case WLAN_CIPHER_SUITE_WEP104:
		return MT_CIPHER_WEP104;
	case WLAN_CIPHER_SUITE_TKIP:
		/* Rx/Tx MIC keys are swapped */
		memcpy(key_data + 16, key->key + 24, 8);
		memcpy(key_data + 24, key->key + 16, 8);
		return MT_CIPHER_TKIP;
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

int mt7615_mcu_set_wtbl_key(struct mt7615_dev *dev, int wcid,
			    struct ieee80211_key_conf *key,
			    enum set_key_cmd cmd)
{
	struct {
		struct wtbl_req_hdr hdr;
		struct wtbl_sec_key key;
	} req = {
		.hdr = {
			.wlan_idx = wcid,
			.operation = WTBL_SET,
			.tlv_num = cpu_to_le16(1),
		},
		.key = {
			.tag = cpu_to_le16(WTBL_SEC_KEY),
			.len = cpu_to_le16(sizeof(struct wtbl_sec_key)),
			.add = cmd,
		},
	};

	if (cmd == SET_KEY) {
		u8 cipher;

		cipher = mt7615_get_key_info(key, req.key.key_material);
		if (cipher == MT_CIPHER_NONE)
			return -EOPNOTSUPP;

		req.key.rkv = 1;
		req.key.cipher_id = cipher;
		req.key.key_id = key->keyidx;
		req.key.key_len = key->keylen;
	} else {
		req.key.key_len = sizeof(req.key.key_material);
	}

	return __mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_WTBL_UPDATE,
				   &req, sizeof(req), true);
}

static int
mt7615_mcu_add_wtbl_bmc(struct mt7615_dev *dev,
			struct mt7615_vif *mvif)
{
	struct {
		struct wtbl_req_hdr hdr;
		struct wtbl_generic g_wtbl;
		struct wtbl_rx rx_wtbl;
	} req = {
		.hdr = {
			.wlan_idx = mvif->sta.wcid.idx,
			.operation = WTBL_RESET_AND_SET,
			.tlv_num = cpu_to_le16(2),
		},
		.g_wtbl = {
			.tag = cpu_to_le16(WTBL_GENERIC),
			.len = cpu_to_le16(sizeof(struct wtbl_generic)),
			.muar_idx = 0xe,
		},
		.rx_wtbl = {
			.tag = cpu_to_le16(WTBL_RX),
			.len = cpu_to_le16(sizeof(struct wtbl_rx)),
			.rca1 = 1,
			.rca2 = 1,
			.rv = 1,
		},
	};
	eth_broadcast_addr(req.g_wtbl.peer_addr);

	return __mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_WTBL_UPDATE,
				   &req, sizeof(req), true);
}

int mt7615_mcu_wtbl_bmc(struct mt7615_dev *dev,
			struct ieee80211_vif *vif, bool enable)
{
	struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;

	if (!enable) {
		struct wtbl_req_hdr req = {
			.wlan_idx = mvif->sta.wcid.idx,
			.operation = WTBL_RESET_AND_SET,
		};

		return __mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_WTBL_UPDATE,
					   &req, sizeof(req), true);
	}

	return mt7615_mcu_add_wtbl_bmc(dev, mvif);
}

int mt7615_mcu_add_wtbl(struct mt7615_dev *dev, struct ieee80211_vif *vif,
			struct ieee80211_sta *sta)
{
	struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;
	struct mt7615_sta *msta = (struct mt7615_sta *)sta->drv_priv;
	struct {
		struct wtbl_req_hdr hdr;
		struct wtbl_generic g_wtbl;
		struct wtbl_rx rx_wtbl;
	} req = {
		.hdr = {
			.wlan_idx = msta->wcid.idx,
			.operation = WTBL_RESET_AND_SET,
			.tlv_num = cpu_to_le16(2),
		},
		.g_wtbl = {
			.tag = cpu_to_le16(WTBL_GENERIC),
			.len = cpu_to_le16(sizeof(struct wtbl_generic)),
			.muar_idx = mvif->omac_idx,
			.qos = sta->wme,
			.partial_aid = cpu_to_le16(sta->aid),
		},
		.rx_wtbl = {
			.tag = cpu_to_le16(WTBL_RX),
			.len = cpu_to_le16(sizeof(struct wtbl_rx)),
			.rca1 = vif->type != NL80211_IFTYPE_AP,
			.rca2 = 1,
			.rv = 1,
		},
	};
	memcpy(req.g_wtbl.peer_addr, sta->addr, ETH_ALEN);

	return __mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_WTBL_UPDATE,
				   &req, sizeof(req), true);
}

int mt7615_mcu_del_wtbl(struct mt7615_dev *dev,
			struct ieee80211_sta *sta)
{
	struct mt7615_sta *msta = (struct mt7615_sta *)sta->drv_priv;
	struct wtbl_req_hdr req = {
		.wlan_idx = msta->wcid.idx,
		.operation = WTBL_RESET_AND_SET,
	};

	return __mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_WTBL_UPDATE,
				   &req, sizeof(req), true);
}

int mt7615_mcu_del_wtbl_all(struct mt7615_dev *dev)
{
	struct wtbl_req_hdr req = {
		.operation = WTBL_RESET_ALL,
	};

	return __mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_WTBL_UPDATE,
				   &req, sizeof(req), true);
}

int mt7615_mcu_set_sta_rec_bmc(struct mt7615_dev *dev,
			       struct ieee80211_vif *vif, bool en)
{
	struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;
	struct {
		struct sta_req_hdr hdr;
		struct sta_rec_basic basic;
	} req = {
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
	eth_broadcast_addr(req.basic.peer_addr);

	if (en) {
		req.basic.conn_state = CONN_STATE_PORT_SECURE;
		req.basic.extra_info = cpu_to_le16(EXTRA_INFO_VER |
						   EXTRA_INFO_NEW);
	} else {
		req.basic.conn_state = CONN_STATE_DISCONNECT;
		req.basic.extra_info = cpu_to_le16(EXTRA_INFO_VER);
	}

	return __mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_STA_REC_UPDATE,
				   &req, sizeof(req), true);
}

int mt7615_mcu_set_sta_rec(struct mt7615_dev *dev, struct ieee80211_vif *vif,
			   struct ieee80211_sta *sta, bool en)
{
	struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;
	struct mt7615_sta *msta = (struct mt7615_sta *)sta->drv_priv;

	struct {
		struct sta_req_hdr hdr;
		struct sta_rec_basic basic;
	} req = {
		.hdr = {
			.bss_idx = mvif->idx,
			.wlan_idx = msta->wcid.idx,
			.tlv_num = cpu_to_le16(1),
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
	memcpy(req.basic.peer_addr, sta->addr, ETH_ALEN);

	switch (vif->type) {
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_MESH_POINT:
		req.basic.conn_type = cpu_to_le32(CONNECTION_INFRA_STA);
		break;
	case NL80211_IFTYPE_STATION:
		req.basic.conn_type = cpu_to_le32(CONNECTION_INFRA_AP);
		break;
	default:
		WARN_ON(1);
		break;
	};

	if (en) {
		req.basic.conn_state = CONN_STATE_PORT_SECURE;
		req.basic.extra_info = cpu_to_le16(EXTRA_INFO_VER |
						   EXTRA_INFO_NEW);
	} else {
		req.basic.conn_state = CONN_STATE_DISCONNECT;
		req.basic.extra_info = cpu_to_le16(EXTRA_INFO_VER);
	}

	return __mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_STA_REC_UPDATE,
				   &req, sizeof(req), true);
}

int mt7615_mcu_set_bcn(struct mt7615_dev *dev, struct ieee80211_vif *vif,
		       int en)
{
	struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;
	struct mt76_wcid *wcid = &dev->mt76.global_wcid;
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
		/* pky_type: 0 for bcn, 1 for tim */
		.pkt_type = 0,
	};
	struct sk_buff *skb;
	u16 tim_off;

	skb = ieee80211_beacon_get_tim(mt76_hw(dev), vif, &tim_off, NULL);
	if (!skb)
		return -EINVAL;

	if (skb->len > 512 - MT_TXD_SIZE) {
		dev_err(dev->mt76.dev, "Bcn size limit exceed\n");
		dev_kfree_skb(skb);
		return -EINVAL;
	}

	mt7615_mac_write_txwi(dev, (__le32 *)(req.pkt), skb, wcid, NULL,
			      0, NULL);
	memcpy(req.pkt + MT_TXD_SIZE, skb->data, skb->len);
	req.pkt_len = cpu_to_le16(MT_TXD_SIZE + skb->len);
	req.tim_ie_pos = cpu_to_le16(MT_TXD_SIZE + tim_off);

	dev_kfree_skb(skb);

	return __mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_BCN_OFFLOAD,
				   &req, sizeof(req), true);
}

int mt7615_mcu_set_tx_power(struct mt7615_dev *dev)
{
	int i, ret, n_chains = hweight8(dev->mt76.antenna_mask);
	struct cfg80211_chan_def *chandef = &dev->mt76.chandef;
	int freq = chandef->center_freq1, len, target_chains;
	u8 *req, *data, *eep = (u8 *)dev->mt76.eeprom.data;
	enum nl80211_band band = chandef->chan->band;
	struct ieee80211_hw *hw = mt76_hw(dev);
	struct {
		u8 center_chan;
		u8 dbdc_idx;
		u8 band;
		u8 rsv;
	} __packed req_hdr = {
		.center_chan = ieee80211_frequency_to_channel(freq),
		.band = band,
	};
	s8 tx_power;

	len = sizeof(req_hdr) + __MT_EE_MAX - MT_EE_NIC_CONF_0;
	req = kzalloc(len, GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	memcpy(req, &req_hdr, sizeof(req_hdr));
	data = req + sizeof(req_hdr);
	memcpy(data, eep + MT_EE_NIC_CONF_0,
	       __MT_EE_MAX - MT_EE_NIC_CONF_0);

	tx_power = hw->conf.power_level * 2;
	switch (n_chains) {
	case 4:
		tx_power -= 12;
		break;
	case 3:
		tx_power -= 8;
		break;
	case 2:
		tx_power -= 6;
		break;
	default:
		break;
	}
	tx_power = max_t(s8, tx_power, 0);
	dev->mt76.txpower_cur = tx_power;

	target_chains = mt7615_ext_pa_enabled(dev, band) ? 1 : n_chains;
	for (i = 0; i < target_chains; i++) {
		int index = -MT_EE_NIC_CONF_0;

		ret = mt7615_eeprom_get_power_index(dev, chandef->chan, i);
		if (ret < 0)
			goto out;

		index += ret;
		data[index] = min_t(u8, data[index], tx_power);
	}

	ret = __mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_SET_TX_POWER_CTRL,
				  req, len, true);
out:
	kfree(req);

	return ret;
}

int mt7615_mcu_set_channel(struct mt7615_dev *dev)
{
	struct cfg80211_chan_def *chdef = &dev->mt76.chandef;
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
	} req = {0};
	int ret;

	req.control_chan = chdef->chan->hw_value;
	req.center_chan = ieee80211_frequency_to_channel(chdef->center_freq1);
	req.tx_streams = (dev->mt76.chainmask >> 8) & 0xf;
	req.rx_streams_mask = dev->mt76.antenna_mask;
	req.switch_reason = CH_SWITCH_NORMAL;
	req.band_idx = 0;
	req.center_chan2 = ieee80211_frequency_to_channel(chdef->center_freq2);
	req.txpower_drop = 0;

	switch (dev->mt76.chandef.width) {
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
	}
	memset(req.txpower_sku, 0x3f, 49);

	ret = __mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_CHANNEL_SWITCH,
				  &req, sizeof(req), true);
	if (ret)
		return ret;

	return __mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_SET_RX_PATH,
				   &req, sizeof(req), true);
}

int mt7615_mcu_set_ht_cap(struct mt7615_dev *dev, struct ieee80211_vif *vif,
			  struct ieee80211_sta *sta)
{
	struct mt7615_sta *msta = (struct mt7615_sta *)sta->drv_priv;
	struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;
	struct wtbl_req_hdr *wtbl_hdr;
	struct sta_req_hdr *sta_hdr;
	struct wtbl_raw *wtbl_raw;
	struct sta_rec_ht *sta_ht;
	struct wtbl_ht *wtbl_ht;
	int buf_len, ret, ntlv = 2;
	u32 msk, val = 0;
	u8 *buf;

	buf = kzalloc(MT7615_WTBL_UPDATE_MAX_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	wtbl_hdr = (struct wtbl_req_hdr *)buf;
	wtbl_hdr->wlan_idx = msta->wcid.idx;
	wtbl_hdr->operation = WTBL_SET;
	buf_len = sizeof(*wtbl_hdr);

	/* ht basic */
	wtbl_ht = (struct wtbl_ht *)(buf + buf_len);
	wtbl_ht->tag = cpu_to_le16(WTBL_HT);
	wtbl_ht->len = cpu_to_le16(sizeof(*wtbl_ht));
	wtbl_ht->ht = 1;
	wtbl_ht->ldpc = sta->ht_cap.cap & IEEE80211_HT_CAP_LDPC_CODING;
	wtbl_ht->af = sta->ht_cap.ampdu_factor;
	wtbl_ht->mm = sta->ht_cap.ampdu_density;
	buf_len += sizeof(*wtbl_ht);

	if (sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_20)
		val |= MT_WTBL_W5_SHORT_GI_20;
	if (sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_40)
		val |= MT_WTBL_W5_SHORT_GI_40;

	/* vht basic */
	if (sta->vht_cap.vht_supported) {
		struct wtbl_vht *wtbl_vht;

		wtbl_vht = (struct wtbl_vht *)(buf + buf_len);
		buf_len += sizeof(*wtbl_vht);
		wtbl_vht->tag = cpu_to_le16(WTBL_VHT);
		wtbl_vht->len = cpu_to_le16(sizeof(*wtbl_vht));
		wtbl_vht->ldpc = sta->vht_cap.cap & IEEE80211_VHT_CAP_RXLDPC;
		wtbl_vht->vht = 1;
		ntlv++;

		if (sta->vht_cap.cap & IEEE80211_VHT_CAP_SHORT_GI_80)
			val |= MT_WTBL_W5_SHORT_GI_80;
		if (sta->vht_cap.cap & IEEE80211_VHT_CAP_SHORT_GI_160)
			val |= MT_WTBL_W5_SHORT_GI_160;
	}

	/* smps */
	if (sta->smps_mode == IEEE80211_SMPS_DYNAMIC) {
		struct wtbl_smps *wtbl_smps;

		wtbl_smps = (struct wtbl_smps *)(buf + buf_len);
		buf_len += sizeof(*wtbl_smps);
		wtbl_smps->tag = cpu_to_le16(WTBL_SMPS);
		wtbl_smps->len = cpu_to_le16(sizeof(*wtbl_smps));
		wtbl_smps->smps = 1;
		ntlv++;
	}

	/* sgi */
	msk = MT_WTBL_W5_SHORT_GI_20 | MT_WTBL_W5_SHORT_GI_40 |
	      MT_WTBL_W5_SHORT_GI_80 | MT_WTBL_W5_SHORT_GI_160;

	wtbl_raw = (struct wtbl_raw *)(buf + buf_len);
	buf_len += sizeof(*wtbl_raw);
	wtbl_raw->tag = cpu_to_le16(WTBL_RAW_DATA);
	wtbl_raw->len = cpu_to_le16(sizeof(*wtbl_raw));
	wtbl_raw->wtbl_idx = 1;
	wtbl_raw->dw = 5;
	wtbl_raw->msk = cpu_to_le32(~msk);
	wtbl_raw->val = cpu_to_le32(val);

	wtbl_hdr->tlv_num = cpu_to_le16(ntlv);
	ret = __mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_WTBL_UPDATE,
				  buf, buf_len, true);
	if (ret)
		goto out;

	memset(buf, 0, MT7615_WTBL_UPDATE_MAX_SIZE);

	sta_hdr = (struct sta_req_hdr *)buf;
	sta_hdr->bss_idx = mvif->idx;
	sta_hdr->wlan_idx = msta->wcid.idx;
	sta_hdr->is_tlv_append = 1;
	ntlv = sta->vht_cap.vht_supported ? 2 : 1;
	sta_hdr->tlv_num = cpu_to_le16(ntlv);
	sta_hdr->muar_idx = mvif->omac_idx;
	buf_len = sizeof(*sta_hdr);

	sta_ht = (struct sta_rec_ht *)(buf + buf_len);
	sta_ht->tag = cpu_to_le16(STA_REC_HT);
	sta_ht->len = cpu_to_le16(sizeof(*sta_ht));
	sta_ht->ht_cap = cpu_to_le16(sta->ht_cap.cap);
	buf_len += sizeof(*sta_ht);

	if (sta->vht_cap.vht_supported) {
		struct sta_rec_vht *sta_vht;

		sta_vht = (struct sta_rec_vht *)(buf + buf_len);
		buf_len += sizeof(*sta_vht);
		sta_vht->tag = cpu_to_le16(STA_REC_VHT);
		sta_vht->len = cpu_to_le16(sizeof(*sta_vht));
		sta_vht->vht_cap = cpu_to_le32(sta->vht_cap.cap);
		sta_vht->vht_rx_mcs_map =
			cpu_to_le16(sta->vht_cap.vht_mcs.rx_mcs_map);
		sta_vht->vht_tx_mcs_map =
			cpu_to_le16(sta->vht_cap.vht_mcs.tx_mcs_map);
	}

	ret = __mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_STA_REC_UPDATE,
				  buf, buf_len, true);
out:
	kfree(buf);

	return ret;
}

int mt7615_mcu_set_tx_ba(struct mt7615_dev *dev,
			 struct ieee80211_ampdu_params *params,
			 bool add)
{
	struct mt7615_sta *msta = (struct mt7615_sta *)params->sta->drv_priv;
	struct mt7615_vif *mvif = msta->vif;
	struct {
		struct wtbl_req_hdr hdr;
		struct wtbl_ba ba;
	} wtbl_req = {
		.hdr = {
			.wlan_idx = msta->wcid.idx,
			.operation = WTBL_SET,
			.tlv_num = cpu_to_le16(1),
		},
		.ba = {
			.tag = cpu_to_le16(WTBL_BA),
			.len = cpu_to_le16(sizeof(struct wtbl_ba)),
			.tid = params->tid,
			.ba_type = MT_BA_TYPE_ORIGINATOR,
			.sn = add ? cpu_to_le16(params->ssn) : 0,
			.ba_en = add,
		},
	};
	struct {
		struct sta_req_hdr hdr;
		struct sta_rec_ba ba;
	} sta_req = {
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
	int ret;

	if (add) {
		u8 idx, ba_range[] = { 4, 8, 12, 24, 36, 48, 54, 64 };

		for (idx = 7; idx > 0; idx--) {
			if (params->buf_size >= ba_range[idx])
				break;
		}

		wtbl_req.ba.ba_winsize_idx = idx;
	}

	ret = __mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_WTBL_UPDATE,
				  &wtbl_req, sizeof(wtbl_req), true);
	if (ret)
		return ret;

	return __mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_STA_REC_UPDATE,
				   &sta_req, sizeof(sta_req), true);
}

int mt7615_mcu_set_rx_ba(struct mt7615_dev *dev,
			 struct ieee80211_ampdu_params *params,
			 bool add)
{
	struct mt7615_sta *msta = (struct mt7615_sta *)params->sta->drv_priv;
	struct mt7615_vif *mvif = msta->vif;
	struct {
		struct wtbl_req_hdr hdr;
		struct wtbl_ba ba;
	} wtbl_req = {
		.hdr = {
			.wlan_idx = msta->wcid.idx,
			.operation = WTBL_SET,
			.tlv_num = cpu_to_le16(1),
		},
		.ba = {
			.tag = cpu_to_le16(WTBL_BA),
			.len = cpu_to_le16(sizeof(struct wtbl_ba)),
			.tid = params->tid,
			.ba_type = MT_BA_TYPE_RECIPIENT,
			.rst_ba_tid = params->tid,
			.rst_ba_sel = RST_BA_MAC_TID_MATCH,
			.rst_ba_sb = 1,
		},
	};
	struct {
		struct sta_req_hdr hdr;
		struct sta_rec_ba ba;
	} sta_req = {
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
	int ret;

	memcpy(wtbl_req.ba.peer_addr, params->sta->addr, ETH_ALEN);

	ret = __mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_STA_REC_UPDATE,
				  &sta_req, sizeof(sta_req), true);
	if (ret || !add)
		return ret;

	return __mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_WTBL_UPDATE,
				   &wtbl_req, sizeof(wtbl_req), true);
}

void mt7615_mcu_set_rates(struct mt7615_dev *dev, struct mt7615_sta *sta,
			  struct ieee80211_tx_rate *probe_rate,
			  struct ieee80211_tx_rate *rates)
{
	int wcid = sta->wcid.idx;
	u32 addr = MT_WTBL_BASE + wcid * MT_WTBL_ENTRY_SIZE;
	bool stbc = false;
	int n_rates = sta->n_rates;
	u8 bw, bw_prev, bw_idx = 0;
	u16 val[4];
	u16 probe_val;
	u32 w5, w27;
	int i;

	if (!mt76_poll(dev, MT_WTBL_UPDATE, MT_WTBL_UPDATE_BUSY, 0, 5000))
		return;

	for (i = n_rates; i < 4; i++)
		rates[i] = rates[n_rates - 1];

	val[0] = mt7615_mac_tx_rate_val(dev, &rates[0], stbc, &bw);
	bw_prev = bw;

	if (probe_rate) {
		probe_val = mt7615_mac_tx_rate_val(dev, probe_rate, stbc, &bw);
		if (bw)
			bw_idx = 1;
		else
			bw_prev = 0;
	} else {
		probe_val = val[0];
	}

	val[1] = mt7615_mac_tx_rate_val(dev, &rates[1], stbc, &bw);
	if (bw_prev) {
		bw_idx = 3;
		bw_prev = bw;
	}

	val[2] = mt7615_mac_tx_rate_val(dev, &rates[2], stbc, &bw);
	if (bw_prev) {
		bw_idx = 5;
		bw_prev = bw;
	}

	val[3] = mt7615_mac_tx_rate_val(dev, &rates[3], stbc, &bw);
	if (bw_prev)
		bw_idx = 7;

	w27 = mt76_rr(dev, addr + 27 * 4);
	w27 &= ~MT_WTBL_W27_CC_BW_SEL;
	w27 |= FIELD_PREP(MT_WTBL_W27_CC_BW_SEL, bw);

	w5 = mt76_rr(dev, addr + 5 * 4);
	w5 &= ~(MT_WTBL_W5_BW_CAP | MT_WTBL_W5_CHANGE_BW_RATE);
	w5 |= FIELD_PREP(MT_WTBL_W5_BW_CAP, bw) |
	      FIELD_PREP(MT_WTBL_W5_CHANGE_BW_RATE, bw_idx ? bw_idx - 1 : 7);

	mt76_wr(dev, MT_WTBL_RIUCR0, w5);

	mt76_wr(dev, MT_WTBL_RIUCR1,
		FIELD_PREP(MT_WTBL_RIUCR1_RATE0, probe_val) |
		FIELD_PREP(MT_WTBL_RIUCR1_RATE1, val[0]) |
		FIELD_PREP(MT_WTBL_RIUCR1_RATE2_LO, val[0]));

	mt76_wr(dev, MT_WTBL_RIUCR2,
		FIELD_PREP(MT_WTBL_RIUCR2_RATE2_HI, val[0] >> 8) |
		FIELD_PREP(MT_WTBL_RIUCR2_RATE3, val[1]) |
		FIELD_PREP(MT_WTBL_RIUCR2_RATE4, val[1]) |
		FIELD_PREP(MT_WTBL_RIUCR2_RATE5_LO, val[2]));

	mt76_wr(dev, MT_WTBL_RIUCR3,
		FIELD_PREP(MT_WTBL_RIUCR3_RATE5_HI, val[2] >> 4) |
		FIELD_PREP(MT_WTBL_RIUCR3_RATE6, val[2]) |
		FIELD_PREP(MT_WTBL_RIUCR3_RATE7, val[3]));

	mt76_wr(dev, MT_WTBL_UPDATE,
		FIELD_PREP(MT_WTBL_UPDATE_WLAN_IDX, wcid) |
		MT_WTBL_UPDATE_RATE_UPDATE |
		MT_WTBL_UPDATE_TX_COUNT_CLEAR);

	mt76_wr(dev, addr + 27 * 4, w27);

	if (!(sta->wcid.tx_info & MT_WCID_TX_INFO_SET))
		mt76_poll(dev, MT_WTBL_UPDATE, MT_WTBL_UPDATE_BUSY, 0, 5000);

	sta->rate_count = 2 * MT7615_RATE_RETRY * n_rates;
	sta->wcid.tx_info |= MT_WCID_TX_INFO_SET;
}
