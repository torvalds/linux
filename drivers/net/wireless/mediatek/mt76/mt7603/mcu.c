// SPDX-License-Identifier: ISC

#include <linux/firmware.h>
#include "mt7603.h"
#include "mcu.h"
#include "eeprom.h"

#define MCU_SKB_RESERVE	8

struct mt7603_fw_trailer {
	char fw_ver[10];
	char build_date[15];
	__le32 dl_len;
} __packed;

static int
mt7603_mcu_parse_response(struct mt76_dev *mdev, int cmd,
			  struct sk_buff *skb, int seq)
{
	struct mt7603_dev *dev = container_of(mdev, struct mt7603_dev, mt76);
	struct mt7603_mcu_rxd *rxd;

	if (!skb) {
		dev_err(mdev->dev,
			"MCU message %d (seq %d) timed out\n",
			cmd, seq);
		dev->mcu_hang = MT7603_WATCHDOG_TIMEOUT;
		return -ETIMEDOUT;
	}

	rxd = (struct mt7603_mcu_rxd *)skb->data;
	if (seq != rxd->seq)
		return -EAGAIN;

	return 0;
}

static int
mt7603_mcu_skb_send_msg(struct mt76_dev *mdev, struct sk_buff *skb,
			int cmd, int *wait_seq)
{
	struct mt7603_dev *dev = container_of(mdev, struct mt7603_dev, mt76);
	int hdrlen = dev->mcu_running ? sizeof(struct mt7603_mcu_txd) : 12;
	struct mt7603_mcu_txd *txd;
	u8 seq;

	mdev->mcu.timeout = 3 * HZ;

	seq = ++mdev->mcu.msg_seq & 0xf;
	if (!seq)
		seq = ++mdev->mcu.msg_seq & 0xf;

	txd = (struct mt7603_mcu_txd *)skb_push(skb, hdrlen);

	txd->len = cpu_to_le16(skb->len);
	if (cmd == -MCU_CMD_FW_SCATTER)
		txd->pq_id = cpu_to_le16(MCU_PORT_QUEUE_FW);
	else
		txd->pq_id = cpu_to_le16(MCU_PORT_QUEUE);
	txd->pkt_type = MCU_PKT_ID;
	txd->seq = seq;

	if (cmd < 0) {
		txd->cid = -cmd;
		txd->set_query = MCU_Q_NA;
	} else {
		txd->cid = MCU_CMD_EXT_CID;
		txd->ext_cid = cmd;
		txd->set_query = MCU_Q_SET;
		txd->ext_cid_ack = 1;
	}

	if (wait_seq)
		*wait_seq = seq;

	return mt76_tx_queue_skb_raw(dev, mdev->q_mcu[MT_MCUQ_WM], skb, 0);
}

static int
mt7603_mcu_init_download(struct mt7603_dev *dev, u32 addr, u32 len)
{
	struct {
		__le32 addr;
		__le32 len;
		__le32 mode;
	} req = {
		.addr = cpu_to_le32(addr),
		.len = cpu_to_le32(len),
		.mode = cpu_to_le32(BIT(31)),
	};

	return mt76_mcu_send_msg(&dev->mt76, -MCU_CMD_TARGET_ADDRESS_LEN_REQ,
				 &req, sizeof(req), true);
}

static int
mt7603_mcu_start_firmware(struct mt7603_dev *dev, u32 addr)
{
	struct {
		__le32 override;
		__le32 addr;
	} req = {
		.override = cpu_to_le32(addr ? 1 : 0),
		.addr = cpu_to_le32(addr),
	};

	return mt76_mcu_send_msg(&dev->mt76, -MCU_CMD_FW_START_REQ, &req,
				 sizeof(req), true);
}

static int
mt7603_mcu_restart(struct mt76_dev *dev)
{
	return mt76_mcu_send_msg(dev, -MCU_CMD_RESTART_DL_REQ, NULL, 0, true);
}

static int mt7603_load_firmware(struct mt7603_dev *dev)
{
	const struct firmware *fw;
	const struct mt7603_fw_trailer *hdr;
	const char *firmware;
	int dl_len;
	u32 addr, val;
	int ret;

	if (is_mt7628(dev)) {
		if (mt76xx_rev(dev) == MT7628_REV_E1)
			firmware = MT7628_FIRMWARE_E1;
		else
			firmware = MT7628_FIRMWARE_E2;
	} else {
		if (mt76xx_rev(dev) < MT7603_REV_E2)
			firmware = MT7603_FIRMWARE_E1;
		else
			firmware = MT7603_FIRMWARE_E2;
	}

	ret = request_firmware(&fw, firmware, dev->mt76.dev);
	if (ret)
		return ret;

	if (!fw || !fw->data || fw->size < sizeof(*hdr)) {
		dev_err(dev->mt76.dev, "Invalid firmware\n");
		ret = -EINVAL;
		goto out;
	}

	hdr = (const struct mt7603_fw_trailer *)(fw->data + fw->size -
						 sizeof(*hdr));

	dev_info(dev->mt76.dev, "Firmware Version: %.10s\n", hdr->fw_ver);
	dev_info(dev->mt76.dev, "Build Time: %.15s\n", hdr->build_date);

	addr = mt7603_reg_map(dev, 0x50012498);
	mt76_wr(dev, addr, 0x5);
	mt76_wr(dev, addr, 0x5);
	udelay(1);

	/* switch to bypass mode */
	mt76_rmw(dev, MT_SCH_4, MT_SCH_4_FORCE_QID,
		 MT_SCH_4_BYPASS | FIELD_PREP(MT_SCH_4_FORCE_QID, 5));

	val = mt76_rr(dev, MT_TOP_MISC2);
	if (val & BIT(1)) {
		dev_info(dev->mt76.dev, "Firmware already running...\n");
		goto running;
	}

	if (!mt76_poll_msec(dev, MT_TOP_MISC2, BIT(0) | BIT(1), BIT(0), 500)) {
		dev_err(dev->mt76.dev, "Timeout waiting for ROM code to become ready\n");
		ret = -EIO;
		goto out;
	}

	dl_len = le32_to_cpu(hdr->dl_len) + 4;
	ret = mt7603_mcu_init_download(dev, MCU_FIRMWARE_ADDRESS, dl_len);
	if (ret) {
		dev_err(dev->mt76.dev, "Download request failed\n");
		goto out;
	}

	ret = mt76_mcu_send_firmware(&dev->mt76, -MCU_CMD_FW_SCATTER,
				     fw->data, dl_len);
	if (ret) {
		dev_err(dev->mt76.dev, "Failed to send firmware to device\n");
		goto out;
	}

	ret = mt7603_mcu_start_firmware(dev, MCU_FIRMWARE_ADDRESS);
	if (ret) {
		dev_err(dev->mt76.dev, "Failed to start firmware\n");
		goto out;
	}

	if (!mt76_poll_msec(dev, MT_TOP_MISC2, BIT(1), BIT(1), 500)) {
		dev_err(dev->mt76.dev, "Timeout waiting for firmware to initialize\n");
		ret = -EIO;
		goto out;
	}

running:
	mt76_clear(dev, MT_SCH_4, MT_SCH_4_FORCE_QID | MT_SCH_4_BYPASS);

	mt76_set(dev, MT_SCH_4, BIT(8));
	mt76_clear(dev, MT_SCH_4, BIT(8));

	dev->mcu_running = true;
	snprintf(dev->mt76.hw->wiphy->fw_version,
		 sizeof(dev->mt76.hw->wiphy->fw_version),
		 "%.10s-%.15s", hdr->fw_ver, hdr->build_date);
	dev_info(dev->mt76.dev, "firmware init done\n");

out:
	release_firmware(fw);

	return ret;
}

int mt7603_mcu_init(struct mt7603_dev *dev)
{
	static const struct mt76_mcu_ops mt7603_mcu_ops = {
		.headroom = sizeof(struct mt7603_mcu_txd),
		.mcu_skb_send_msg = mt7603_mcu_skb_send_msg,
		.mcu_parse_response = mt7603_mcu_parse_response,
		.mcu_restart = mt7603_mcu_restart,
	};

	dev->mt76.mcu_ops = &mt7603_mcu_ops;
	return mt7603_load_firmware(dev);
}

void mt7603_mcu_exit(struct mt7603_dev *dev)
{
	__mt76_mcu_restart(&dev->mt76);
	skb_queue_purge(&dev->mt76.mcu.res_q);
}

int mt7603_mcu_set_eeprom(struct mt7603_dev *dev)
{
	static const u16 req_fields[] = {
#define WORD(_start)			\
		_start,			\
		_start + 1
#define GROUP_2G(_start)		\
		WORD(_start),		\
		WORD(_start + 2),	\
		WORD(_start + 4)

		MT_EE_NIC_CONF_0 + 1,
		WORD(MT_EE_NIC_CONF_1),
		MT_EE_WIFI_RF_SETTING,
		MT_EE_TX_POWER_DELTA_BW40,
		MT_EE_TX_POWER_DELTA_BW80 + 1,
		MT_EE_TX_POWER_EXT_PA_5G,
		MT_EE_TEMP_SENSOR_CAL,
		GROUP_2G(MT_EE_TX_POWER_0_START_2G),
		GROUP_2G(MT_EE_TX_POWER_1_START_2G),
		WORD(MT_EE_TX_POWER_CCK),
		WORD(MT_EE_TX_POWER_OFDM_2G_6M),
		WORD(MT_EE_TX_POWER_OFDM_2G_24M),
		WORD(MT_EE_TX_POWER_OFDM_2G_54M),
		WORD(MT_EE_TX_POWER_HT_BPSK_QPSK),
		WORD(MT_EE_TX_POWER_HT_16_64_QAM),
		WORD(MT_EE_TX_POWER_HT_64_QAM),
		MT_EE_ELAN_RX_MODE_GAIN,
		MT_EE_ELAN_RX_MODE_NF,
		MT_EE_ELAN_RX_MODE_P1DB,
		MT_EE_ELAN_BYPASS_MODE_GAIN,
		MT_EE_ELAN_BYPASS_MODE_NF,
		MT_EE_ELAN_BYPASS_MODE_P1DB,
		WORD(MT_EE_STEP_NUM_NEG_6_7),
		WORD(MT_EE_STEP_NUM_NEG_4_5),
		WORD(MT_EE_STEP_NUM_NEG_2_3),
		WORD(MT_EE_STEP_NUM_NEG_0_1),
		WORD(MT_EE_REF_STEP_24G),
		WORD(MT_EE_STEP_NUM_PLUS_1_2),
		WORD(MT_EE_STEP_NUM_PLUS_3_4),
		WORD(MT_EE_STEP_NUM_PLUS_5_6),
		MT_EE_STEP_NUM_PLUS_7,
		MT_EE_XTAL_FREQ_OFFSET,
		MT_EE_XTAL_TRIM_2_COMP,
		MT_EE_XTAL_TRIM_3_COMP,
		MT_EE_XTAL_WF_RFCAL,

		/* unknown fields below */
		WORD(0x24),
		0x34,
		0x39,
		0x3b,
		WORD(0x42),
		WORD(0x9e),
		0xf2,
		WORD(0xf8),
		0xfa,
		0x12e,
		WORD(0x130), WORD(0x132), WORD(0x134), WORD(0x136),
		WORD(0x138), WORD(0x13a), WORD(0x13c), WORD(0x13e),

#undef GROUP_2G
#undef WORD

	};
	struct req_data {
		__le16 addr;
		u8 val;
		u8 pad;
	} __packed;
	struct {
		u8 buffer_mode;
		u8 len;
		u8 pad[2];
	} req_hdr = {
		.buffer_mode = 1,
		.len = ARRAY_SIZE(req_fields) - 1,
	};
	const int size = 0xff * sizeof(struct req_data);
	u8 *req, *eep = (u8 *)dev->mt76.eeprom.data;
	int i, ret, len = sizeof(req_hdr) + size;
	struct req_data *data;

	BUILD_BUG_ON(ARRAY_SIZE(req_fields) * sizeof(*data) > size);

	req = kmalloc(len, GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	memcpy(req, &req_hdr, sizeof(req_hdr));
	data = (struct req_data *)(req + sizeof(req_hdr));
	memset(data, 0, size);
	for (i = 0; i < ARRAY_SIZE(req_fields); i++) {
		data[i].addr = cpu_to_le16(req_fields[i]);
		data[i].val = eep[req_fields[i]];
	}

	ret = mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_EFUSE_BUFFER_MODE,
				req, len, true);
	kfree(req);

	return ret;
}

static int mt7603_mcu_set_tx_power(struct mt7603_dev *dev)
{
	struct {
		u8 center_channel;
		u8 tssi;
		u8 temp_comp;
		u8 target_power[2];
		u8 rate_power_delta[14];
		u8 bw_power_delta;
		u8 ch_power_delta[6];
		u8 temp_comp_power[17];
		u8 reserved;
	} req = {
		.center_channel = dev->mphy.chandef.chan->hw_value,
#define EEP_VAL(n) ((u8 *)dev->mt76.eeprom.data)[n]
		.tssi = EEP_VAL(MT_EE_NIC_CONF_1 + 1),
		.temp_comp = EEP_VAL(MT_EE_NIC_CONF_1),
		.target_power = {
			EEP_VAL(MT_EE_TX_POWER_0_START_2G + 2),
			EEP_VAL(MT_EE_TX_POWER_1_START_2G + 2)
		},
		.bw_power_delta = EEP_VAL(MT_EE_TX_POWER_DELTA_BW40),
		.ch_power_delta = {
			EEP_VAL(MT_EE_TX_POWER_0_START_2G + 3),
			EEP_VAL(MT_EE_TX_POWER_0_START_2G + 4),
			EEP_VAL(MT_EE_TX_POWER_0_START_2G + 5),
			EEP_VAL(MT_EE_TX_POWER_1_START_2G + 3),
			EEP_VAL(MT_EE_TX_POWER_1_START_2G + 4),
			EEP_VAL(MT_EE_TX_POWER_1_START_2G + 5)
		},
#undef EEP_VAL
	};
	u8 *eep = (u8 *)dev->mt76.eeprom.data;

	memcpy(req.rate_power_delta, eep + MT_EE_TX_POWER_CCK,
	       sizeof(req.rate_power_delta));

	memcpy(req.temp_comp_power, eep + MT_EE_STEP_NUM_NEG_6_7,
	       sizeof(req.temp_comp_power));

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_SET_TX_POWER_CTRL,
				 &req, sizeof(req), true);
}

int mt7603_mcu_set_channel(struct mt7603_dev *dev)
{
	struct cfg80211_chan_def *chandef = &dev->mphy.chandef;
	struct ieee80211_hw *hw = mt76_hw(dev);
	int n_chains = hweight8(dev->mphy.antenna_mask);
	struct {
		u8 control_chan;
		u8 center_chan;
		u8 bw;
		u8 tx_streams;
		u8 rx_streams;
		u8 _res0[7];
		u8 txpower[21];
		u8 _res1[3];
	} req = {
		.control_chan = chandef->chan->hw_value,
		.center_chan = chandef->chan->hw_value,
		.bw = MT_BW_20,
		.tx_streams = n_chains,
		.rx_streams = n_chains,
	};
	s8 tx_power;
	int i, ret;

	if (dev->mphy.chandef.width == NL80211_CHAN_WIDTH_40) {
		req.bw = MT_BW_40;
		if (chandef->center_freq1 > chandef->chan->center_freq)
			req.center_chan += 2;
		else
			req.center_chan -= 2;
	}

	tx_power = hw->conf.power_level * 2;
	if (dev->mphy.antenna_mask == 3)
		tx_power -= 6;
	tx_power = min(tx_power, dev->tx_power_limit);

	dev->mphy.txpower_cur = tx_power;

	for (i = 0; i < ARRAY_SIZE(req.txpower); i++)
		req.txpower[i] = tx_power;

	ret = mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_CHANNEL_SWITCH, &req,
				sizeof(req), true);
	if (ret)
		return ret;

	return mt7603_mcu_set_tx_power(dev);
}
