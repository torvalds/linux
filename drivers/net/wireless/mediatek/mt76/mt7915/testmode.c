// SPDX-License-Identifier: ISC
/* Copyright (C) 2020 MediaTek Inc. */

#include "mt7915.h"
#include "mac.h"
#include "mcu.h"
#include "testmode.h"

enum {
	TM_CHANGED_TXPOWER,
	TM_CHANGED_FREQ_OFFSET,

	/* must be last */
	NUM_TM_CHANGED
};

static const u8 tm_change_map[] = {
	[TM_CHANGED_TXPOWER] = MT76_TM_ATTR_TX_POWER,
	[TM_CHANGED_FREQ_OFFSET] = MT76_TM_ATTR_FREQ_OFFSET,
};

struct reg_band {
	u32 band[2];
};

#define REG_BAND(_reg) \
	{ .band[0] = MT_##_reg(0), .band[1] = MT_##_reg(1) }
#define REG_BAND_IDX(_reg, _idx) \
	{ .band[0] = MT_##_reg(0, _idx), .band[1] = MT_##_reg(1, _idx) }

static const struct reg_band reg_backup_list[] = {
	REG_BAND_IDX(AGG_PCR0, 0),
	REG_BAND_IDX(AGG_PCR0, 1),
	REG_BAND_IDX(AGG_AWSCR0, 0),
	REG_BAND_IDX(AGG_AWSCR0, 1),
	REG_BAND_IDX(AGG_AWSCR0, 2),
	REG_BAND_IDX(AGG_AWSCR0, 3),
	REG_BAND(AGG_MRCR),
	REG_BAND(TMAC_TFCR0),
	REG_BAND(TMAC_TCR0),
	REG_BAND(AGG_ATCR1),
	REG_BAND(AGG_ATCR3),
	REG_BAND(TMAC_TRCR0),
	REG_BAND(TMAC_ICR0),
	REG_BAND_IDX(ARB_DRNGR0, 0),
	REG_BAND_IDX(ARB_DRNGR0, 1),
	REG_BAND(WF_RFCR),
	REG_BAND(WF_RFCR1),
};

static int
mt7915_tm_set_tx_power(struct mt7915_phy *phy)
{
	struct mt7915_dev *dev = phy->dev;
	struct mt76_phy *mphy = phy->mt76;
	struct cfg80211_chan_def *chandef = &mphy->chandef;
	int freq = chandef->center_freq1;
	int ret;
	struct {
		u8 format_id;
		u8 dbdc_idx;
		s8 tx_power;
		u8 ant_idx;	/* Only 0 is valid */
		u8 center_chan;
		u8 rsv[3];
	} __packed req = {
		.format_id = 0xf,
		.dbdc_idx = phy != &dev->phy,
		.center_chan = ieee80211_frequency_to_channel(freq),
	};
	u8 *tx_power = NULL;

	if (dev->mt76.test.state != MT76_TM_STATE_OFF)
		tx_power = dev->mt76.test.tx_power;

	/* Tx power of the other antennas are the same as antenna 0 */
	if (tx_power && tx_power[0])
		req.tx_power = tx_power[0];

	ret = mt76_mcu_send_msg(&dev->mt76,
				MCU_EXT_CMD_TX_POWER_FEATURE_CTRL,
				&req, sizeof(req), false);

	return ret;
}

static int
mt7915_tm_set_freq_offset(struct mt7915_dev *dev, bool en, u32 val)
{
	struct mt7915_tm_cmd req = {
		.testmode_en = en,
		.param_idx = MCU_ATE_SET_FREQ_OFFSET,
		.param.freq.freq_offset = cpu_to_le32(val),
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_ATE_CTRL, &req,
				 sizeof(req), false);
}

static int
mt7915_tm_mode_ctrl(struct mt7915_dev *dev, bool enable)
{
	struct {
		u8 format_id;
		bool enable;
		u8 rsv[2];
	} __packed req = {
		.format_id = 0x6,
		.enable = enable,
	};

	return mt76_mcu_send_msg(&dev->mt76,
				 MCU_EXT_CMD_TX_POWER_FEATURE_CTRL,
				 &req, sizeof(req), false);
}

static int
mt7915_tm_set_trx(struct mt7915_dev *dev, struct mt7915_phy *phy,
		  int type, bool en)
{
	struct mt7915_tm_cmd req = {
		.testmode_en = 1,
		.param_idx = MCU_ATE_SET_TRX,
		.param.trx.type = type,
		.param.trx.enable = en,
		.param.trx.band = phy != &dev->phy,
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD_ATE_CTRL, &req,
				 sizeof(req), false);
}

static void
mt7915_tm_reg_backup_restore(struct mt7915_dev *dev, struct mt7915_phy *phy)
{
	int n_regs = ARRAY_SIZE(reg_backup_list);
	bool ext_phy = phy != &dev->phy;
	u32 *b = dev->test.reg_backup;
	int i;

	if (dev->mt76.test.state == MT76_TM_STATE_OFF) {
		for (i = 0; i < n_regs; i++)
			mt76_wr(dev, reg_backup_list[i].band[ext_phy], b[i]);
		return;
	}

	if (b)
		return;

	b = devm_kzalloc(dev->mt76.dev, 4 * n_regs, GFP_KERNEL);
	if (!b)
		return;

	dev->test.reg_backup = b;
	for (i = 0; i < n_regs; i++)
		b[i] = mt76_rr(dev, reg_backup_list[i].band[ext_phy]);

	mt76_clear(dev, MT_AGG_PCR0(ext_phy, 0), MT_AGG_PCR0_MM_PROT |
		   MT_AGG_PCR0_GF_PROT | MT_AGG_PCR0_ERP_PROT |
		   MT_AGG_PCR0_VHT_PROT | MT_AGG_PCR0_BW20_PROT |
		   MT_AGG_PCR0_BW40_PROT | MT_AGG_PCR0_BW80_PROT);
	mt76_set(dev, MT_AGG_PCR0(ext_phy, 0), MT_AGG_PCR0_PTA_WIN_DIS);

	mt76_wr(dev, MT_AGG_PCR0(ext_phy, 1), MT_AGG_PCR1_RTS0_NUM_THRES |
		MT_AGG_PCR1_RTS0_LEN_THRES);

	mt76_clear(dev, MT_AGG_MRCR(ext_phy), MT_AGG_MRCR_BAR_CNT_LIMIT |
		   MT_AGG_MRCR_LAST_RTS_CTS_RN | MT_AGG_MRCR_RTS_FAIL_LIMIT |
		   MT_AGG_MRCR_TXCMD_RTS_FAIL_LIMIT);

	mt76_rmw(dev, MT_AGG_MRCR(ext_phy), MT_AGG_MRCR_RTS_FAIL_LIMIT |
		 MT_AGG_MRCR_TXCMD_RTS_FAIL_LIMIT,
		 FIELD_PREP(MT_AGG_MRCR_RTS_FAIL_LIMIT, 1) |
		 FIELD_PREP(MT_AGG_MRCR_TXCMD_RTS_FAIL_LIMIT, 1));

	mt76_wr(dev, MT_TMAC_TFCR0(ext_phy), 0);
	mt76_clear(dev, MT_TMAC_TCR0(ext_phy), MT_TMAC_TCR0_TBTT_STOP_CTRL);

	/* config rx filter for testmode rx */
	mt76_wr(dev, MT_WF_RFCR(ext_phy), 0xcf70a);
	mt76_wr(dev, MT_WF_RFCR1(ext_phy), 0);
}

static void
mt7915_tm_init(struct mt7915_dev *dev)
{
	bool en = !(dev->mt76.test.state == MT76_TM_STATE_OFF);

	if (!test_bit(MT76_STATE_RUNNING, &dev->phy.mt76->state))
		return;

	mt7915_tm_mode_ctrl(dev, en);
	mt7915_tm_reg_backup_restore(dev, &dev->phy);
	mt7915_tm_set_trx(dev, &dev->phy, TM_MAC_TXRX, !en);
}

static void
mt7915_tm_set_tx_frames(struct mt7915_dev *dev, bool en)
{
	static const u8 spe_idx_map[] = {0, 0, 1, 0, 3, 2, 4, 0,
					 9, 8, 6, 10, 16, 12, 18, 0};
	struct sk_buff *skb = dev->mt76.test.tx_skb;
	struct ieee80211_tx_info *info;

	mt7915_tm_set_trx(dev, &dev->phy, TM_MAC_RX_RXV, false);

	if (en) {
		u8 tx_ant = dev->mt76.test.tx_antenna_mask;

		mutex_unlock(&dev->mt76.mutex);
		mt7915_set_channel(&dev->phy);
		mutex_lock(&dev->mt76.mutex);

		mt7915_mcu_set_chan_info(&dev->phy, MCU_EXT_CMD_SET_RX_PATH);
		dev->test.spe_idx = spe_idx_map[tx_ant];
	}

	mt7915_tm_set_trx(dev, &dev->phy, TM_MAC_TX, en);

	if (!en || !skb)
		return;

	info = IEEE80211_SKB_CB(skb);
	info->control.vif = dev->phy.monitor_vif;
}

static void
mt7915_tm_set_rx_frames(struct mt7915_dev *dev, bool en)
{
	if (en) {
		mutex_unlock(&dev->mt76.mutex);
		mt7915_set_channel(&dev->phy);
		mutex_lock(&dev->mt76.mutex);

		mt7915_mcu_set_chan_info(&dev->phy, MCU_EXT_CMD_SET_RX_PATH);
	}

	mt7915_tm_set_trx(dev, &dev->phy, TM_MAC_RX_RXV, en);
}

static void
mt7915_tm_update_params(struct mt7915_dev *dev, u32 changed)
{
	struct mt76_testmode_data *td = &dev->mt76.test;
	bool en = dev->mt76.test.state != MT76_TM_STATE_OFF;

	if (changed & BIT(TM_CHANGED_FREQ_OFFSET))
		mt7915_tm_set_freq_offset(dev, en, en ? td->freq_offset : 0);
	if (changed & BIT(TM_CHANGED_TXPOWER))
		mt7915_tm_set_tx_power(&dev->phy);
}

static int
mt7915_tm_set_state(struct mt76_dev *mdev, enum mt76_testmode_state state)
{
	struct mt7915_dev *dev = container_of(mdev, struct mt7915_dev, mt76);
	struct mt76_testmode_data *td = &mdev->test;
	enum mt76_testmode_state prev_state = td->state;

	mdev->test.state = state;

	if (prev_state == MT76_TM_STATE_TX_FRAMES)
		mt7915_tm_set_tx_frames(dev, false);
	else if (state == MT76_TM_STATE_TX_FRAMES)
		mt7915_tm_set_tx_frames(dev, true);
	else if (prev_state == MT76_TM_STATE_RX_FRAMES)
		mt7915_tm_set_rx_frames(dev, false);
	else if (state == MT76_TM_STATE_RX_FRAMES)
		mt7915_tm_set_rx_frames(dev, true);
	else if (prev_state == MT76_TM_STATE_OFF || state == MT76_TM_STATE_OFF)
		mt7915_tm_init(dev);

	if ((state == MT76_TM_STATE_IDLE &&
	     prev_state == MT76_TM_STATE_OFF) ||
	    (state == MT76_TM_STATE_OFF &&
	     prev_state == MT76_TM_STATE_IDLE)) {
		u32 changed = 0;
		int i;

		for (i = 0; i < ARRAY_SIZE(tm_change_map); i++) {
			u16 cur = tm_change_map[i];

			if (td->param_set[cur / 32] & BIT(cur % 32))
				changed |= BIT(i);
		}

		mt7915_tm_update_params(dev, changed);
	}

	return 0;
}

static int
mt7915_tm_set_params(struct mt76_dev *mdev, struct nlattr **tb,
		     enum mt76_testmode_state new_state)
{
	struct mt7915_dev *dev = container_of(mdev, struct mt7915_dev, mt76);
	struct mt76_testmode_data *td = &dev->mt76.test;
	u32 changed = 0;
	int i;

	BUILD_BUG_ON(NUM_TM_CHANGED >= 32);

	if (new_state == MT76_TM_STATE_OFF ||
	    td->state == MT76_TM_STATE_OFF)
		return 0;

	if (td->tx_antenna_mask & ~dev->phy.chainmask)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(tm_change_map); i++) {
		if (tb[tm_change_map[i]])
			changed |= BIT(i);
	}

	mt7915_tm_update_params(dev, changed);

	return 0;
}

static int
mt7915_tm_dump_stats(struct mt76_dev *mdev, struct sk_buff *msg)
{
	struct mt7915_dev *dev = container_of(mdev, struct mt7915_dev, mt76);
	void *rx, *rssi;
	int i;

	rx = nla_nest_start(msg, MT76_TM_STATS_ATTR_LAST_RX);
	if (!rx)
		return -ENOMEM;

	if (nla_put_s32(msg, MT76_TM_RX_ATTR_FREQ_OFFSET, dev->test.last_freq_offset))
		return -ENOMEM;

	rssi = nla_nest_start(msg, MT76_TM_RX_ATTR_RCPI);
	if (!rssi)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(dev->test.last_rcpi); i++)
		if (nla_put_u8(msg, i, dev->test.last_rcpi[i]))
			return -ENOMEM;

	nla_nest_end(msg, rssi);

	rssi = nla_nest_start(msg, MT76_TM_RX_ATTR_IB_RSSI);
	if (!rssi)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(dev->test.last_ib_rssi); i++)
		if (nla_put_s8(msg, i, dev->test.last_ib_rssi[i]))
			return -ENOMEM;

	nla_nest_end(msg, rssi);

	rssi = nla_nest_start(msg, MT76_TM_RX_ATTR_WB_RSSI);
	if (!rssi)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(dev->test.last_wb_rssi); i++)
		if (nla_put_s8(msg, i, dev->test.last_wb_rssi[i]))
			return -ENOMEM;

	nla_nest_end(msg, rssi);

	if (nla_put_u8(msg, MT76_TM_RX_ATTR_SNR, dev->test.last_snr))
		return -ENOMEM;

	nla_nest_end(msg, rx);

	return 0;
}

const struct mt76_testmode_ops mt7915_testmode_ops = {
	.set_state = mt7915_tm_set_state,
	.set_params = mt7915_tm_set_params,
	.dump_stats = mt7915_tm_dump_stats,
};
