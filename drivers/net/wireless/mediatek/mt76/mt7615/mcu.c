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
				 int cmd, int query, int dest, int *wait_seq)
{
	struct mt7615_mcu_txd *mcu_txd;
	u8 seq, q_idx, pkt_fmt;
	enum mt76_txq_id qid;
	u32 val;
	__le32 *txd;

	if (!skb)
		return -EINVAL;

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
		mcu_txd->cid = -cmd;
	} else {
		mcu_txd->cid = MCU_CMD_EXT_CID;
		mcu_txd->ext_cid = cmd;
		if (query != MCU_Q_NA)
			mcu_txd->ext_cid_ack = 1;
	}

	mcu_txd->set_query = query;
	mcu_txd->s2d_index = dest;

	if (wait_seq)
		*wait_seq = seq;

	if (test_bit(MT76_STATE_MCU_RUNNING, &dev->mt76.state))
		qid = MT_TXQ_MCU;
	else
		qid = MT_TXQ_FWDL;

	return mt76_tx_queue_skb_raw(dev, qid, skb, 0);
}

static int mt7615_mcu_msg_send(struct mt7615_dev *dev, struct sk_buff *skb,
			       int cmd, int query, int dest,
			       struct sk_buff **skb_ret)
{
	unsigned long expires = jiffies + 10 * HZ;
	struct mt7615_mcu_rxd *rxd;
	int ret, seq;

	mutex_lock(&dev->mt76.mmio.mcu.mutex);

	ret = __mt7615_mcu_msg_send(dev, skb, cmd, query, dest, &seq);
	if (ret)
		goto out;

	while (1) {
		skb = mt76_mcu_get_response(&dev->mt76, expires);
		if (!skb) {
			dev_err(dev->mt76.dev, "Message %d (seq %d) timeout\n",
				cmd, seq);
			ret = -ETIMEDOUT;
			break;
		}

		rxd = (struct mt7615_mcu_rxd *)skb->data;
		if (seq != rxd->seq)
			continue;

		if (skb_ret) {
			int hdr_len = sizeof(*rxd);

			if (!test_bit(MT76_STATE_MCU_RUNNING,
				      &dev->mt76.state))
				hdr_len -= 4;
			skb_pull(skb, hdr_len);
			*skb_ret = skb;
		} else {
			dev_kfree_skb(skb);
		}

		break;
	}

out:
	mutex_unlock(&dev->mt76.mmio.mcu.mutex);

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
	struct sk_buff *skb = mt7615_mcu_msg_alloc(&req, sizeof(req));

	return mt7615_mcu_msg_send(dev, skb, -MCU_CMD_TARGET_ADDRESS_LEN_REQ,
				   MCU_Q_NA, MCU_S2D_H2N, NULL);
}

static int mt7615_mcu_send_firmware(struct mt7615_dev *dev, const void *data,
				    int len)
{
	struct sk_buff *skb;
	int ret = 0;

	while (len > 0) {
		int cur_len = min_t(int, 4096 - sizeof(struct mt7615_mcu_txd),
				    len);

		skb = mt7615_mcu_msg_alloc(data, cur_len);
		if (!skb)
			return -ENOMEM;

		ret = __mt7615_mcu_msg_send(dev, skb, -MCU_CMD_FW_SCATTER,
					    MCU_Q_NA, MCU_S2D_H2N, NULL);
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
	struct sk_buff *skb = mt7615_mcu_msg_alloc(&req, sizeof(req));

	return mt7615_mcu_msg_send(dev, skb, -MCU_CMD_FW_START_REQ,
				   MCU_Q_NA, MCU_S2D_H2N, NULL);
}

static int mt7615_mcu_restart(struct mt7615_dev *dev)
{
	struct sk_buff *skb = mt7615_mcu_msg_alloc(NULL, 0);

	return mt7615_mcu_msg_send(dev, skb, -MCU_CMD_RESTART_DL_REQ,
				   MCU_Q_NA, MCU_S2D_H2N, NULL);
}

static int mt7615_mcu_patch_sem_ctrl(struct mt7615_dev *dev, bool get)
{
	struct {
		__le32 operation;
	} req = {
		.operation = cpu_to_le32(get ? PATCH_SEM_GET :
					 PATCH_SEM_RELEASE),
	};
	struct event {
		u8 status;
		u8 reserved[3];
	} *resp;
	struct sk_buff *skb = mt7615_mcu_msg_alloc(&req, sizeof(req));
	struct sk_buff *skb_ret;
	int ret;

	ret = mt7615_mcu_msg_send(dev, skb, -MCU_CMD_PATCH_SEM_CONTROL,
				  MCU_Q_NA, MCU_S2D_H2N, &skb_ret);
	if (ret)
		goto out;

	resp = (struct event *)(skb_ret->data);
	ret = resp->status;
	dev_kfree_skb(skb_ret);

out:
	return ret;
}

static int mt7615_mcu_start_patch(struct mt7615_dev *dev)
{
	struct {
		u8 check_crc;
		u8 reserved[3];
	} req = {
		.check_crc = 0,
	};
	struct sk_buff *skb = mt7615_mcu_msg_alloc(&req, sizeof(req));

	return mt7615_mcu_msg_send(dev, skb, -MCU_CMD_PATCH_FINISH_REQ,
				   MCU_Q_NA, MCU_S2D_H2N, NULL);
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
	int ret;

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
	mt7615_mcu_restart(dev);
	mt76_wr(dev, MT_CFG_LPCR_HOST, MT_CFG_LPCR_HOST_FW_OWN);
	skb_queue_purge(&dev->mt76.mmio.mcu.res_q);
}

int mt7615_mcu_set_eeprom(struct mt7615_dev *dev)
{
	struct req_data {
		u8 val;
	} __packed;
	struct {
		u8 buffer_mode;
		u8 pad;
		u16 len;
	} __packed req_hdr = {
		.buffer_mode = 1,
		.len = __MT_EE_MAX - MT_EE_NIC_CONF_0,
	};
	struct sk_buff *skb;
	struct req_data *data;
	const int size = (__MT_EE_MAX - MT_EE_NIC_CONF_0) *
			 sizeof(struct req_data);
	u8 *eep = (u8 *)dev->mt76.eeprom.data;
	u16 off;

	skb = mt7615_mcu_msg_alloc(NULL, size + sizeof(req_hdr));
	memcpy(skb_put(skb, sizeof(req_hdr)), &req_hdr, sizeof(req_hdr));
	data = (struct req_data *)skb_put(skb, size);
	memset(data, 0, size);

	for (off = MT_EE_NIC_CONF_0; off < __MT_EE_MAX; off++)
		data[off - MT_EE_NIC_CONF_0].val = eep[off];

	return mt7615_mcu_msg_send(dev, skb, MCU_EXT_CMD_EFUSE_BUFFER_MODE,
				   MCU_Q_SET, MCU_S2D_H2N, NULL);
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
	struct sk_buff *skb = mt7615_mcu_msg_alloc(&req, sizeof(req));

	return mt7615_mcu_msg_send(dev, skb, MCU_EXT_CMD_MAC_INIT_CTRL,
				   MCU_Q_SET, MCU_S2D_H2N, NULL);
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
	struct sk_buff *skb = mt7615_mcu_msg_alloc(&req, sizeof(req));

	return mt7615_mcu_msg_send(dev, skb, MCU_EXT_CMD_PROTECT_CTRL,
				   MCU_Q_SET, MCU_S2D_H2N, NULL);
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
	struct sk_buff *skb;

	if (params->cw_min) {
		req.valid |= WMM_CW_MIN_SET;
		req.cw_min = params->cw_min;
	}
	if (params->cw_max) {
		req.valid |= WMM_CW_MAX_SET;
		req.cw_max = cpu_to_le16(params->cw_max);
	}

	skb = mt7615_mcu_msg_alloc(&req, sizeof(req));
	return mt7615_mcu_msg_send(dev, skb, MCU_EXT_CMD_EDCA_UPDATE,
				   MCU_Q_SET, MCU_S2D_H2N, NULL);
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
	struct sk_buff *skb = mt7615_mcu_msg_alloc(&req, sizeof(req));

	return mt7615_mcu_msg_send(dev, skb, MCU_EXT_CMD_PM_STATE_CTRL,
				   MCU_Q_SET, MCU_S2D_H2N, NULL);
}

static int __mt7615_mcu_set_dev_info(struct mt7615_dev *dev,
				     struct dev_info *dev_info)
{
	struct req_hdr {
		u8 omac_idx;
		u8 band_idx;
		__le16 tlv_num;
		u8 is_tlv_append;
		u8 rsv[3];
	} __packed req_hdr = {0};
	struct req_tlv {
		__le16 tag;
		__le16 len;
		u8 active;
		u8 band_idx;
		u8 omac_addr[ETH_ALEN];
	} __packed;
	struct sk_buff *skb;
	u16 tlv_num = 0;

	skb = mt7615_mcu_msg_alloc(NULL, sizeof(req_hdr) +
				   sizeof(struct req_tlv));
	skb_reserve(skb, sizeof(req_hdr));

	if (dev_info->feature & BIT(DEV_INFO_ACTIVE)) {
		struct req_tlv req_tlv = {
			.tag = cpu_to_le16(DEV_INFO_ACTIVE),
			.len = cpu_to_le16(sizeof(req_tlv)),
			.active = dev_info->enable,
			.band_idx = dev_info->band_idx,
		};
		memcpy(req_tlv.omac_addr, dev_info->omac_addr, ETH_ALEN);
		memcpy(skb_put(skb, sizeof(req_tlv)), &req_tlv,
		       sizeof(req_tlv));
		tlv_num++;
	}

	req_hdr.omac_idx = dev_info->omac_idx;
	req_hdr.band_idx = dev_info->band_idx;
	req_hdr.tlv_num = cpu_to_le16(tlv_num);
	req_hdr.is_tlv_append = tlv_num ? 1 : 0;

	memcpy(skb_push(skb, sizeof(req_hdr)), &req_hdr, sizeof(req_hdr));

	return mt7615_mcu_msg_send(dev, skb, MCU_EXT_CMD_DEV_INFO_UPDATE,
				   MCU_Q_SET, MCU_S2D_H2N, NULL);
}

int mt7615_mcu_set_dev_info(struct mt7615_dev *dev, struct ieee80211_vif *vif,
			    int en)
{
	struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;
	struct dev_info dev_info = {0};

	dev_info.omac_idx = mvif->omac_idx;
	memcpy(dev_info.omac_addr, vif->addr, ETH_ALEN);
	dev_info.band_idx = mvif->band_idx;
	dev_info.enable = en;
	dev_info.feature = BIT(DEV_INFO_ACTIVE);

	return __mt7615_mcu_set_dev_info(dev, &dev_info);
}

static void bss_info_omac_handler (struct mt7615_dev *dev,
				   struct bss_info *bss_info,
				   struct sk_buff *skb)
{
	struct bss_info_omac tlv = {0};

	tlv.tag = cpu_to_le16(BSS_INFO_OMAC);
	tlv.len = cpu_to_le16(sizeof(tlv));
	tlv.hw_bss_idx = (bss_info->omac_idx > EXT_BSSID_START) ?
			 HW_BSSID_0 : bss_info->omac_idx;
	tlv.omac_idx = bss_info->omac_idx;
	tlv.band_idx = bss_info->band_idx;
	tlv.conn_type = cpu_to_le32(bss_info->conn_type);

	memcpy(skb_put(skb, sizeof(tlv)), &tlv, sizeof(tlv));
}

static void bss_info_basic_handler (struct mt7615_dev *dev,
				    struct bss_info *bss_info,
				    struct sk_buff *skb)
{
	struct bss_info_basic tlv = {0};

	tlv.tag = cpu_to_le16(BSS_INFO_BASIC);
	tlv.len = cpu_to_le16(sizeof(tlv));
	tlv.network_type = cpu_to_le32(bss_info->network_type);
	tlv.active = bss_info->enable;
	tlv.bcn_interval = cpu_to_le16(bss_info->bcn_interval);
	memcpy(tlv.bssid, bss_info->bssid, ETH_ALEN);
	tlv.wmm_idx = bss_info->wmm_idx;
	tlv.dtim_period = bss_info->dtim_period;
	tlv.bmc_tx_wlan_idx = bss_info->bmc_tx_wlan_idx;

	memcpy(skb_put(skb, sizeof(tlv)), &tlv, sizeof(tlv));
}

static void bss_info_ext_bss_handler (struct mt7615_dev *dev,
				      struct bss_info *bss_info,
				      struct sk_buff *skb)
{
/* SIFS 20us + 512 byte beacon tranmitted by 1Mbps (3906us) */
#define BCN_TX_ESTIMATE_TIME (4096 + 20)
	struct bss_info_ext_bss tlv = {0};
	int ext_bss_idx;

	ext_bss_idx = bss_info->omac_idx - EXT_BSSID_START;

	if (ext_bss_idx < 0)
		return;

	tlv.tag = cpu_to_le16(BSS_INFO_EXT_BSS);
	tlv.len = cpu_to_le16(sizeof(tlv));
	tlv.mbss_tsf_offset = ext_bss_idx * BCN_TX_ESTIMATE_TIME;

	memcpy(skb_put(skb, sizeof(tlv)), &tlv, sizeof(tlv));
}

static struct bss_info_tag_handler bss_info_tag_handler[] = {
	{BSS_INFO_OMAC, sizeof(struct bss_info_omac), bss_info_omac_handler},
	{BSS_INFO_BASIC, sizeof(struct bss_info_basic), bss_info_basic_handler},
	{BSS_INFO_RF_CH, sizeof(struct bss_info_rf_ch), NULL},
	{BSS_INFO_PM, 0, NULL},
	{BSS_INFO_UAPSD, 0, NULL},
	{BSS_INFO_ROAM_DETECTION, 0, NULL},
	{BSS_INFO_LQ_RM, 0, NULL},
	{BSS_INFO_EXT_BSS, sizeof(struct bss_info_ext_bss), bss_info_ext_bss_handler},
	{BSS_INFO_BMC_INFO, 0, NULL},
	{BSS_INFO_SYNC_MODE, 0, NULL},
	{BSS_INFO_RA, 0, NULL},
	{BSS_INFO_MAX_NUM, 0, NULL},
};

static int __mt7615_mcu_set_bss_info(struct mt7615_dev *dev,
				     struct bss_info *bss_info)
{
	struct req_hdr {
		u8 bss_idx;
		u8 rsv0;
		__le16 tlv_num;
		u8 is_tlv_append;
		u8 rsv1[3];
	} __packed req_hdr = {0};
	struct sk_buff *skb;
	u16 tlv_num = 0;
	u32 size = 0;
	int i;

	for (i = 0; i < BSS_INFO_MAX_NUM; i++)
		if ((BIT(bss_info_tag_handler[i].tag) & bss_info->feature) &&
		    bss_info_tag_handler[i].handler) {
			tlv_num++;
			size += bss_info_tag_handler[i].len;
		}

	skb = mt7615_mcu_msg_alloc(NULL, sizeof(req_hdr) + size);

	req_hdr.bss_idx = bss_info->bss_idx;
	req_hdr.tlv_num = cpu_to_le16(tlv_num);
	req_hdr.is_tlv_append = tlv_num ? 1 : 0;

	memcpy(skb_put(skb, sizeof(req_hdr)), &req_hdr, sizeof(req_hdr));

	for (i = 0; i < BSS_INFO_MAX_NUM; i++)
		if ((BIT(bss_info_tag_handler[i].tag) & bss_info->feature) &&
		    bss_info_tag_handler[i].handler)
			bss_info_tag_handler[i].handler(dev, bss_info, skb);

	return mt7615_mcu_msg_send(dev, skb, MCU_EXT_CMD_BSS_INFO_UPDATE,
				   MCU_Q_SET, MCU_S2D_H2N, NULL);
}

static void bss_info_convert_vif_type(enum nl80211_iftype type,
				      u32 *network_type, u32 *conn_type)
{
	switch (type) {
	case NL80211_IFTYPE_AP:
		if (network_type)
			*network_type = NETWORK_INFRA;
		if (conn_type)
			*conn_type = CONNECTION_INFRA_AP;
		break;
	case NL80211_IFTYPE_STATION:
		if (network_type)
			*network_type = NETWORK_INFRA;
		if (conn_type)
			*conn_type = CONNECTION_INFRA_STA;
		break;
	default:
		WARN_ON(1);
		break;
	};
}

int mt7615_mcu_set_bss_info(struct mt7615_dev *dev, struct ieee80211_vif *vif,
			    int en)
{
	struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;
	struct bss_info bss_info = {0};
	u8 bmc_tx_wlan_idx = 0;
	u32 network_type = 0, conn_type = 0;

	if (vif->type == NL80211_IFTYPE_AP) {
		bmc_tx_wlan_idx = mvif->sta.wcid.idx;
	} else if (vif->type == NL80211_IFTYPE_STATION) {
		/* find the unicast entry for sta mode bmc tx */
		struct ieee80211_sta *ap_sta;
		struct mt7615_sta *msta;

		rcu_read_lock();

		ap_sta = ieee80211_find_sta(vif, vif->bss_conf.bssid);
		if (!ap_sta) {
			rcu_read_unlock();
			return -EINVAL;
		}

		msta = (struct mt7615_sta *)ap_sta->drv_priv;
		bmc_tx_wlan_idx = msta->wcid.idx;

		rcu_read_unlock();
	} else {
		WARN_ON(1);
	}

	bss_info_convert_vif_type(vif->type, &network_type, &conn_type);

	bss_info.bss_idx = mvif->idx;
	memcpy(bss_info.bssid, vif->bss_conf.bssid, ETH_ALEN);
	bss_info.omac_idx = mvif->omac_idx;
	bss_info.band_idx = mvif->band_idx;
	bss_info.bmc_tx_wlan_idx = bmc_tx_wlan_idx;
	bss_info.wmm_idx = mvif->wmm_idx;
	bss_info.network_type = network_type;
	bss_info.conn_type = conn_type;
	bss_info.bcn_interval = vif->bss_conf.beacon_int;
	bss_info.dtim_period = vif->bss_conf.dtim_period;
	bss_info.enable = en;
	bss_info.feature = BIT(BSS_INFO_BASIC);
	if (en) {
		bss_info.feature |= BIT(BSS_INFO_OMAC);
		if (mvif->omac_idx > EXT_BSSID_START)
			bss_info.feature |= BIT(BSS_INFO_EXT_BSS);
	}

	return __mt7615_mcu_set_bss_info(dev, &bss_info);
}

static int
__mt7615_mcu_set_wtbl(struct mt7615_dev *dev, int wlan_idx,
		      int operation, int ntlv, void *buf,
		      int buf_len)
{
	struct req_hdr {
		u8 wlan_idx;
		u8 operation;
		__le16 tlv_num;
		u8 rsv[4];
	} __packed req_hdr = {
		.wlan_idx = wlan_idx,
		.operation = operation,
		.tlv_num = cpu_to_le16(ntlv),
	};
	struct sk_buff *skb;

	skb = mt7615_mcu_msg_alloc(NULL, sizeof(req_hdr) + buf_len);
	memcpy(skb_put(skb, sizeof(req_hdr)), &req_hdr, sizeof(req_hdr));

	if (buf && buf_len)
		memcpy(skb_put(skb, buf_len), buf, buf_len);

	return mt7615_mcu_msg_send(dev, skb, MCU_EXT_CMD_WTBL_UPDATE,
				   MCU_Q_SET, MCU_S2D_H2N, NULL);
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
	struct wtbl_sec_key wtbl_sec_key = {0};
	int buf_len = sizeof(struct wtbl_sec_key);
	u8 cipher;

	wtbl_sec_key.tag = cpu_to_le16(WTBL_SEC_KEY);
	wtbl_sec_key.len = cpu_to_le16(buf_len);
	wtbl_sec_key.add = cmd;

	if (cmd == SET_KEY) {
		cipher = mt7615_get_key_info(key, wtbl_sec_key.key_material);
		if (cipher == MT_CIPHER_NONE && key)
			return -EOPNOTSUPP;

		wtbl_sec_key.cipher_id = cipher;
		wtbl_sec_key.key_id = key->keyidx;
		wtbl_sec_key.key_len = key->keylen;
	} else {
		wtbl_sec_key.key_len = sizeof(wtbl_sec_key.key_material);
	}

	return __mt7615_mcu_set_wtbl(dev, wcid, WTBL_SET, 1,
				     &wtbl_sec_key, buf_len);
}

int mt7615_mcu_add_wtbl_bmc(struct mt7615_dev *dev, struct ieee80211_vif *vif)
{
	struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;
	struct wtbl_generic *wtbl_generic;
	struct wtbl_rx *wtbl_rx;
	int buf_len, ret;
	u8 *buf;

	buf = kzalloc(MT7615_WTBL_UPDATE_MAX_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	wtbl_generic = (struct wtbl_generic *)buf;
	buf_len = sizeof(*wtbl_generic);
	wtbl_generic->tag = cpu_to_le16(WTBL_GENERIC);
	wtbl_generic->len = cpu_to_le16(buf_len);
	eth_broadcast_addr(wtbl_generic->peer_addr);
	wtbl_generic->muar_idx = 0xe;

	wtbl_rx = (struct wtbl_rx *)(buf + buf_len);
	buf_len += sizeof(*wtbl_rx);
	wtbl_rx->tag = cpu_to_le16(WTBL_RX);
	wtbl_rx->len = cpu_to_le16(sizeof(*wtbl_rx));
	wtbl_rx->rca1 = 1;
	wtbl_rx->rca2 = 1;
	wtbl_rx->rv = 1;

	ret = __mt7615_mcu_set_wtbl(dev, mvif->sta.wcid.idx,
				    WTBL_RESET_AND_SET, 2, buf,
				    buf_len);

	kfree(buf);
	return ret;
}

int mt7615_mcu_del_wtbl_bmc(struct mt7615_dev *dev, struct ieee80211_vif *vif)
{
	struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;

	return __mt7615_mcu_set_wtbl(dev, mvif->sta.wcid.idx,
				     WTBL_RESET_AND_SET, 0, NULL, 0);
}

int mt7615_mcu_add_wtbl(struct mt7615_dev *dev, struct ieee80211_vif *vif,
			struct ieee80211_sta *sta)
{
	struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;
	struct mt7615_sta *msta = (struct mt7615_sta *)sta->drv_priv;
	struct wtbl_generic *wtbl_generic;
	struct wtbl_rx *wtbl_rx;
	int buf_len, ret;
	u8 *buf;

	buf = kzalloc(MT7615_WTBL_UPDATE_MAX_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	wtbl_generic = (struct wtbl_generic *)buf;
	buf_len = sizeof(*wtbl_generic);
	wtbl_generic->tag = cpu_to_le16(WTBL_GENERIC);
	wtbl_generic->len = cpu_to_le16(buf_len);
	memcpy(wtbl_generic->peer_addr, sta->addr, ETH_ALEN);
	wtbl_generic->muar_idx = mvif->omac_idx;
	wtbl_generic->qos = sta->wme;
	wtbl_generic->partial_aid = cpu_to_le16(sta->aid);

	wtbl_rx = (struct wtbl_rx *)(buf + buf_len);
	buf_len += sizeof(*wtbl_rx);
	wtbl_rx->tag = cpu_to_le16(WTBL_RX);
	wtbl_rx->len = cpu_to_le16(sizeof(*wtbl_rx));
	wtbl_rx->rca1 = (vif->type == NL80211_IFTYPE_AP) ? 0 : 1;
	wtbl_rx->rca2 = 1;
	wtbl_rx->rv = 1;

	ret = __mt7615_mcu_set_wtbl(dev, msta->wcid.idx, WTBL_RESET_AND_SET,
				    2, buf, buf_len);

	kfree(buf);
	return ret;
}

int mt7615_mcu_del_wtbl(struct mt7615_dev *dev, struct ieee80211_vif *vif,
			struct ieee80211_sta *sta)
{
	struct mt7615_sta *msta = (struct mt7615_sta *)sta->drv_priv;

	return __mt7615_mcu_set_wtbl(dev, msta->wcid.idx,
				     WTBL_RESET_AND_SET, 0, NULL, 0);
}

int mt7615_mcu_del_wtbl_all(struct mt7615_dev *dev)
{
	return __mt7615_mcu_set_wtbl(dev, 0, WTBL_RESET_ALL, 0, NULL, 0);
}

static int __mt7615_mcu_set_sta_rec(struct mt7615_dev *dev, int bss_idx,
				    int wlan_idx, int muar_idx, void *buf,
				    int buf_len)
{
	struct req_hdr {
		u8 bss_idx;
		u8 wlan_idx;
		__le16 tlv_num;
		u8 is_tlv_append;
		u8 muar_idx;
		u8 rsv[2];
	} __packed req_hdr = {0};
	struct tlv {
		__le16 tag;
		__le16 len;
		u8 buf[0];
	} __packed;
	struct sk_buff *skb;
	u16 tlv_num = 0;
	int offset = 0;

	while (offset < buf_len) {
		struct tlv *tlv = (struct tlv *)((u8 *)buf + offset);

		tlv_num++;
		offset += tlv->len;
	}

	skb = mt7615_mcu_msg_alloc(NULL, sizeof(req_hdr) + buf_len);

	req_hdr.bss_idx = bss_idx;
	req_hdr.wlan_idx = wlan_idx;
	req_hdr.tlv_num = cpu_to_le16(tlv_num);
	req_hdr.is_tlv_append = tlv_num ? 1 : 0;
	req_hdr.muar_idx = muar_idx;

	memcpy(skb_put(skb, sizeof(req_hdr)), &req_hdr, sizeof(req_hdr));

	if (buf && buf_len)
		memcpy(skb_put(skb, buf_len), buf, buf_len);

	return mt7615_mcu_msg_send(dev, skb, MCU_EXT_CMD_STA_REC_UPDATE,
				   MCU_Q_SET, MCU_S2D_H2N, NULL);
}

int mt7615_mcu_set_sta_rec_bmc(struct mt7615_dev *dev,
			       struct ieee80211_vif *vif, bool en)
{
	struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;
	struct sta_rec_basic sta_rec_basic = {0};
	int buf_len = sizeof(struct sta_rec_basic);

	sta_rec_basic.tag = cpu_to_le16(STA_REC_BASIC);
	sta_rec_basic.len = cpu_to_le16(buf_len);
	sta_rec_basic.conn_type = cpu_to_le32(CONNECTION_INFRA_BC);
	eth_broadcast_addr(sta_rec_basic.peer_addr);
	if (en) {
		sta_rec_basic.conn_state = CONN_STATE_PORT_SECURE;
		sta_rec_basic.extra_info =
			cpu_to_le16(EXTRA_INFO_VER | EXTRA_INFO_NEW);
	} else {
		sta_rec_basic.conn_state = CONN_STATE_DISCONNECT;
		sta_rec_basic.extra_info = cpu_to_le16(EXTRA_INFO_VER);
	}

	return __mt7615_mcu_set_sta_rec(dev, mvif->idx, mvif->sta.wcid.idx,
					mvif->omac_idx, &sta_rec_basic,
					buf_len);
}

static void sta_rec_convert_vif_type(enum nl80211_iftype type, u32 *conn_type)
{
	switch (type) {
	case NL80211_IFTYPE_AP:
		if (conn_type)
			*conn_type = CONNECTION_INFRA_STA;
		break;
	case NL80211_IFTYPE_STATION:
		if (conn_type)
			*conn_type = CONNECTION_INFRA_AP;
		break;
	default:
		WARN_ON(1);
		break;
	};
}

int mt7615_mcu_set_sta_rec(struct mt7615_dev *dev, struct ieee80211_vif *vif,
			   struct ieee80211_sta *sta, bool en)
{
	struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;
	struct mt7615_sta *msta = (struct mt7615_sta *)sta->drv_priv;
	struct sta_rec_basic sta_rec_basic = {0};
	int buf_len = sizeof(struct sta_rec_basic);
	u32 conn_type = 0;

	sta_rec_convert_vif_type(vif->type, &conn_type);

	sta_rec_basic.tag = cpu_to_le16(STA_REC_BASIC);
	sta_rec_basic.len = cpu_to_le16(buf_len);
	sta_rec_basic.conn_type = cpu_to_le32(conn_type);
	sta_rec_basic.qos = sta->wme;
	sta_rec_basic.aid = cpu_to_le16(sta->aid);
	memcpy(sta_rec_basic.peer_addr, sta->addr, ETH_ALEN);

	if (en) {
		sta_rec_basic.conn_state = CONN_STATE_PORT_SECURE;
		sta_rec_basic.extra_info =
			cpu_to_le16(EXTRA_INFO_VER | EXTRA_INFO_NEW);
	} else {
		sta_rec_basic.conn_state = CONN_STATE_DISCONNECT;
		sta_rec_basic.extra_info = cpu_to_le16(EXTRA_INFO_VER);
	}

	return __mt7615_mcu_set_sta_rec(dev, mvif->idx, msta->wcid.idx,
					mvif->omac_idx, &sta_rec_basic,
					buf_len);
}

int mt7615_mcu_set_bcn(struct mt7615_dev *dev, struct ieee80211_vif *vif,
		       int en)
{
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
	} __packed req = {0};
	struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;
	struct mt76_wcid *wcid = &dev->mt76.global_wcid;
	struct sk_buff *skb;
	u16 tim_off, tim_len;

	skb = ieee80211_beacon_get_tim(mt76_hw(dev), vif, &tim_off, &tim_len);

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

	req.omac_idx = mvif->omac_idx;
	req.enable = en;
	req.wlan_idx = wcid->idx;
	req.band_idx = mvif->band_idx;
	/* pky_type: 0 for bcn, 1 for tim */
	req.pkt_type = 0;
	req.pkt_len = cpu_to_le16(MT_TXD_SIZE + skb->len);
	req.tim_ie_pos = cpu_to_le16(MT_TXD_SIZE + tim_off);

	dev_kfree_skb(skb);
	skb = mt7615_mcu_msg_alloc(&req, sizeof(req));

	return mt7615_mcu_msg_send(dev, skb, MCU_EXT_CMD_BCN_OFFLOAD,
				   MCU_Q_SET, MCU_S2D_H2N, NULL);
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
	struct sk_buff *skb;
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

	skb = mt7615_mcu_msg_alloc(&req, sizeof(req));
	ret = mt7615_mcu_msg_send(dev, skb, MCU_EXT_CMD_CHANNEL_SWITCH,
				  MCU_Q_SET, MCU_S2D_H2N, NULL);
	if (ret)
		return ret;

	skb = mt7615_mcu_msg_alloc(&req, sizeof(req));
	return mt7615_mcu_msg_send(dev, skb, MCU_EXT_CMD_SET_RX_PATH,
				   MCU_Q_SET, MCU_S2D_H2N, NULL);
}

int mt7615_mcu_set_ht_cap(struct mt7615_dev *dev, struct ieee80211_vif *vif,
			  struct ieee80211_sta *sta)
{
	struct mt7615_sta *msta = (struct mt7615_sta *)sta->drv_priv;
	struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;
	struct wtbl_ht *wtbl_ht;
	struct wtbl_raw *wtbl_raw;
	struct sta_rec_ht *sta_rec_ht;
	int buf_len, ret, ntlv = 2;
	u32 msk, val = 0;
	u8 *buf;

	buf = kzalloc(MT7615_WTBL_UPDATE_MAX_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	/* ht basic */
	buf_len = sizeof(*wtbl_ht);
	wtbl_ht = (struct wtbl_ht *)buf;
	wtbl_ht->tag = cpu_to_le16(WTBL_HT);
	wtbl_ht->len = cpu_to_le16(sizeof(*wtbl_ht));
	wtbl_ht->ht = 1;
	wtbl_ht->ldpc = sta->ht_cap.cap & IEEE80211_HT_CAP_LDPC_CODING;
	wtbl_ht->af = sta->ht_cap.ampdu_factor;
	wtbl_ht->mm = sta->ht_cap.ampdu_density;

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

	ret = __mt7615_mcu_set_wtbl(dev, msta->wcid.idx, WTBL_SET, ntlv,
				    buf, buf_len);
	if (ret) {
		kfree(buf);
		return ret;
	}

	memset(buf, 0, MT7615_WTBL_UPDATE_MAX_SIZE);

	buf_len = sizeof(*sta_rec_ht);
	sta_rec_ht = (struct sta_rec_ht *)buf;
	sta_rec_ht->tag = cpu_to_le16(STA_REC_HT);
	sta_rec_ht->len = cpu_to_le16(sizeof(*sta_rec_ht));
	sta_rec_ht->ht_cap = cpu_to_le16(sta->ht_cap.cap);

	if (sta->vht_cap.vht_supported) {
		struct sta_rec_vht *sta_rec_vht;

		sta_rec_vht = (struct sta_rec_vht *)(buf + buf_len);
		buf_len += sizeof(*sta_rec_vht);
		sta_rec_vht->tag = cpu_to_le16(STA_REC_VHT);
		sta_rec_vht->len = cpu_to_le16(sizeof(*sta_rec_vht));
		sta_rec_vht->vht_cap = cpu_to_le32(sta->vht_cap.cap);
		sta_rec_vht->vht_rx_mcs_map =
			cpu_to_le16(sta->vht_cap.vht_mcs.rx_mcs_map);
		sta_rec_vht->vht_tx_mcs_map =
			cpu_to_le16(sta->vht_cap.vht_mcs.tx_mcs_map);
	}

	ret = __mt7615_mcu_set_sta_rec(dev, mvif->idx, msta->wcid.idx,
				       mvif->omac_idx, buf, buf_len);
	kfree(buf);
	return ret;
}

int mt7615_mcu_set_tx_ba(struct mt7615_dev *dev,
			 struct ieee80211_ampdu_params *params,
			 bool add)
{
	struct ieee80211_sta *sta = params->sta;
	struct mt7615_sta *msta = (struct mt7615_sta *)sta->drv_priv;
	struct mt7615_vif *mvif = msta->vif;
	u8 ba_range[8] = {4, 8, 12, 24, 36, 48, 54, 64};
	u16 tid = params->tid;
	u16 ba_size = params->buf_size;
	u16 ssn = params->ssn;
	struct wtbl_ba wtbl_ba = {0};
	struct sta_rec_ba sta_rec_ba = {0};
	int ret, buf_len;

	buf_len = sizeof(struct wtbl_ba);

	wtbl_ba.tag = cpu_to_le16(WTBL_BA);
	wtbl_ba.len = cpu_to_le16(buf_len);
	wtbl_ba.tid = tid;
	wtbl_ba.ba_type = MT_BA_TYPE_ORIGINATOR;

	if (add) {
		u8 idx;

		for (idx = 7; idx > 0; idx--) {
			if (ba_size >= ba_range[idx])
				break;
		}

		wtbl_ba.sn = cpu_to_le16(ssn);
		wtbl_ba.ba_en = 1;
		wtbl_ba.ba_winsize_idx = idx;
	}

	ret = __mt7615_mcu_set_wtbl(dev, msta->wcid.idx, WTBL_SET, 1,
				    &wtbl_ba, buf_len);
	if (ret)
		return ret;

	buf_len = sizeof(struct sta_rec_ba);

	sta_rec_ba.tag = cpu_to_le16(STA_REC_BA);
	sta_rec_ba.len = cpu_to_le16(buf_len);
	sta_rec_ba.tid = tid;
	sta_rec_ba.ba_type = MT_BA_TYPE_ORIGINATOR;
	sta_rec_ba.amsdu = params->amsdu;
	sta_rec_ba.ba_en = add << tid;
	sta_rec_ba.ssn = cpu_to_le16(ssn);
	sta_rec_ba.winsize = cpu_to_le16(ba_size);

	return __mt7615_mcu_set_sta_rec(dev, mvif->idx, msta->wcid.idx,
					mvif->omac_idx, &sta_rec_ba, buf_len);
}

int mt7615_mcu_set_rx_ba(struct mt7615_dev *dev,
			 struct ieee80211_ampdu_params *params,
			 bool add)
{
	struct ieee80211_sta *sta = params->sta;
	struct mt7615_sta *msta = (struct mt7615_sta *)sta->drv_priv;
	struct mt7615_vif *mvif = msta->vif;
	u16 tid = params->tid;
	struct wtbl_ba wtbl_ba = {0};
	struct sta_rec_ba sta_rec_ba = {0};
	int ret, buf_len;

	buf_len = sizeof(struct sta_rec_ba);

	sta_rec_ba.tag = cpu_to_le16(STA_REC_BA);
	sta_rec_ba.len = cpu_to_le16(buf_len);
	sta_rec_ba.tid = tid;
	sta_rec_ba.ba_type = MT_BA_TYPE_RECIPIENT;
	sta_rec_ba.amsdu = params->amsdu;
	sta_rec_ba.ba_en = add << tid;
	sta_rec_ba.ssn = cpu_to_le16(params->ssn);
	sta_rec_ba.winsize = cpu_to_le16(params->buf_size);

	ret = __mt7615_mcu_set_sta_rec(dev, mvif->idx, msta->wcid.idx,
				       mvif->omac_idx, &sta_rec_ba, buf_len);
	if (ret || !add)
		return ret;

	buf_len = sizeof(struct wtbl_ba);

	wtbl_ba.tag = cpu_to_le16(WTBL_BA);
	wtbl_ba.len = cpu_to_le16(buf_len);
	wtbl_ba.tid = tid;
	wtbl_ba.ba_type = MT_BA_TYPE_RECIPIENT;
	memcpy(wtbl_ba.peer_addr, sta->addr, ETH_ALEN);
	wtbl_ba.rst_ba_tid = tid;
	wtbl_ba.rst_ba_sel = RST_BA_MAC_TID_MATCH;
	wtbl_ba.rst_ba_sb = 1;

	return  __mt7615_mcu_set_wtbl(dev, msta->wcid.idx, WTBL_SET,
				      1, &wtbl_ba, buf_len);
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
