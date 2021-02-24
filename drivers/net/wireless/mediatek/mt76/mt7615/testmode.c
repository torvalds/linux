// SPDX-License-Identifier: ISC
/* Copyright (C) 2020 Felix Fietkau <nbd@nbd.name> */

#include "mt7615.h"
#include "eeprom.h"
#include "mcu.h"

enum {
	TM_CHANGED_TXPOWER_CTRL,
	TM_CHANGED_TXPOWER,
	TM_CHANGED_FREQ_OFFSET,

	/* must be last */
	NUM_TM_CHANGED
};


static const u8 tm_change_map[] = {
	[TM_CHANGED_TXPOWER_CTRL] = MT76_TM_ATTR_TX_POWER_CONTROL,
	[TM_CHANGED_TXPOWER] = MT76_TM_ATTR_TX_POWER,
	[TM_CHANGED_FREQ_OFFSET] = MT76_TM_ATTR_FREQ_OFFSET,
};

static const u32 reg_backup_list[] = {
	MT_WF_PHY_RFINTF3_0(0),
	MT_WF_PHY_RFINTF3_0(1),
	MT_WF_PHY_RFINTF3_0(2),
	MT_WF_PHY_RFINTF3_0(3),
	MT_ANT_SWITCH_CON(2),
	MT_ANT_SWITCH_CON(3),
	MT_ANT_SWITCH_CON(4),
	MT_ANT_SWITCH_CON(6),
	MT_ANT_SWITCH_CON(7),
	MT_ANT_SWITCH_CON(8),
};

static const struct {
	u16 wf;
	u16 reg;
} rf_backup_list[] = {
	{ 0, 0x48 },
	{ 1, 0x48 },
	{ 2, 0x48 },
	{ 3, 0x48 },
};

static int
mt7615_tm_set_tx_power(struct mt7615_phy *phy)
{
	struct mt7615_dev *dev = phy->dev;
	struct mt76_phy *mphy = phy->mt76;
	int i, ret, n_chains = hweight8(mphy->antenna_mask);
	struct cfg80211_chan_def *chandef = &mphy->chandef;
	int freq = chandef->center_freq1, len, target_chains;
	u8 *data, *eep = (u8 *)dev->mt76.eeprom.data;
	enum nl80211_band band = chandef->chan->band;
	struct sk_buff *skb;
	struct {
		u8 center_chan;
		u8 dbdc_idx;
		u8 band;
		u8 rsv;
	} __packed req_hdr = {
		.center_chan = ieee80211_frequency_to_channel(freq),
		.band = band,
		.dbdc_idx = phy != &dev->phy,
	};
	u8 *tx_power = NULL;

	if (mphy->test.state != MT76_TM_STATE_OFF)
		tx_power = mphy->test.tx_power;

	len = MT7615_EE_MAX - MT_EE_NIC_CONF_0;
	skb = mt76_mcu_msg_alloc(&dev->mt76, NULL, sizeof(req_hdr) + len);
	if (!skb)
		return -ENOMEM;

	skb_put_data(skb, &req_hdr, sizeof(req_hdr));
	data = skb_put_data(skb, eep + MT_EE_NIC_CONF_0, len);

	target_chains = mt7615_ext_pa_enabled(dev, band) ? 1 : n_chains;
	for (i = 0; i < target_chains; i++) {
		ret = mt7615_eeprom_get_target_power_index(dev, chandef->chan, i);
		if (ret < 0) {
			dev_kfree_skb(skb);
			return -EINVAL;
		}

		if (tx_power && tx_power[i])
			data[ret - MT_EE_NIC_CONF_0] = tx_power[i];
	}

	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_EXT_CMD_SET_TX_POWER_CTRL, false);
}

static void
mt7615_tm_reg_backup_restore(struct mt7615_phy *phy)
{
	struct mt7615_dev *dev = phy->dev;
	u32 *b = phy->test.reg_backup;
	int n_regs = ARRAY_SIZE(reg_backup_list);
	int n_rf_regs = ARRAY_SIZE(rf_backup_list);
	int i;

	if (phy->mt76->test.state == MT76_TM_STATE_OFF) {
		for (i = 0; i < n_regs; i++)
			mt76_wr(dev, reg_backup_list[i], b[i]);

		for (i = 0; i < n_rf_regs; i++)
			mt7615_rf_wr(dev, rf_backup_list[i].wf,
				     rf_backup_list[i].reg, b[n_regs + i]);
		return;
	}

	if (b)
		return;

	b = devm_kzalloc(dev->mt76.dev, 4 * (n_regs + n_rf_regs),
			 GFP_KERNEL);
	if (!b)
		return;

	phy->test.reg_backup = b;
	for (i = 0; i < n_regs; i++)
		b[i] = mt76_rr(dev, reg_backup_list[i]);
	for (i = 0; i < n_rf_regs; i++)
		b[n_regs + i] = mt7615_rf_rr(dev, rf_backup_list[i].wf,
					     rf_backup_list[i].reg);
}

static void
mt7615_tm_init(struct mt7615_phy *phy)
{
	struct mt7615_dev *dev = phy->dev;
	unsigned int total_flags = ~0;

	if (!test_bit(MT76_STATE_RUNNING, &phy->mt76->state))
		return;

	mt7615_mcu_set_sku_en(phy, phy->mt76->test.state == MT76_TM_STATE_OFF);

	mutex_unlock(&dev->mt76.mutex);
	mt7615_set_channel(phy);
	mt7615_ops.configure_filter(phy->mt76->hw, 0, &total_flags, 0);
	mutex_lock(&dev->mt76.mutex);

	mt7615_tm_reg_backup_restore(phy);
}

static void
mt7615_tm_set_rx_enable(struct mt7615_dev *dev, bool en)
{
	u32 rqcr_mask = (MT_ARB_RQCR_RX_START |
			 MT_ARB_RQCR_RXV_START |
			 MT_ARB_RQCR_RXV_R_EN |
			 MT_ARB_RQCR_RXV_T_EN) *
			(BIT(0) | BIT(MT_ARB_RQCR_BAND_SHIFT));

	if (en) {
		mt76_clear(dev, MT_ARB_SCR,
			   MT_ARB_SCR_RX0_DISABLE | MT_ARB_SCR_RX1_DISABLE);
		mt76_set(dev, MT_ARB_RQCR, rqcr_mask);
	} else {
		mt76_set(dev, MT_ARB_SCR,
			 MT_ARB_SCR_RX0_DISABLE | MT_ARB_SCR_RX1_DISABLE);
		mt76_clear(dev, MT_ARB_RQCR, rqcr_mask);
	}
}

static void
mt7615_tm_set_tx_antenna(struct mt7615_phy *phy, bool en)
{
	struct mt7615_dev *dev = phy->dev;
	struct mt76_testmode_data *td = &phy->mt76->test;
	u8 mask = td->tx_antenna_mask;
	int i;

	if (!mask)
		return;

	if (!en)
		mask = phy->mt76->chainmask;

	for (i = 0; i < 4; i++) {
		mt76_rmw_field(dev, MT_WF_PHY_RFINTF3_0(i),
			       MT_WF_PHY_RFINTF3_0_ANT,
			       (td->tx_antenna_mask & BIT(i)) ? 0 : 0xa);

	}

	/* 2.4 GHz band */
	mt76_rmw_field(dev, MT_ANT_SWITCH_CON(3), MT_ANT_SWITCH_CON_MODE(0),
		       (td->tx_antenna_mask & BIT(0)) ? 0x8 : 0x1b);
	mt76_rmw_field(dev, MT_ANT_SWITCH_CON(4), MT_ANT_SWITCH_CON_MODE(2),
		       (td->tx_antenna_mask & BIT(1)) ? 0xe : 0x1b);
	mt76_rmw_field(dev, MT_ANT_SWITCH_CON(6), MT_ANT_SWITCH_CON_MODE1(0),
		       (td->tx_antenna_mask & BIT(2)) ? 0x0 : 0xf);
	mt76_rmw_field(dev, MT_ANT_SWITCH_CON(7), MT_ANT_SWITCH_CON_MODE1(2),
		       (td->tx_antenna_mask & BIT(3)) ? 0x6 : 0xf);

	/* 5 GHz band */
	mt76_rmw_field(dev, MT_ANT_SWITCH_CON(4), MT_ANT_SWITCH_CON_MODE(1),
		       (td->tx_antenna_mask & BIT(0)) ? 0xd : 0x1b);
	mt76_rmw_field(dev, MT_ANT_SWITCH_CON(2), MT_ANT_SWITCH_CON_MODE(3),
		       (td->tx_antenna_mask & BIT(1)) ? 0x13 : 0x1b);
	mt76_rmw_field(dev, MT_ANT_SWITCH_CON(7), MT_ANT_SWITCH_CON_MODE1(1),
		       (td->tx_antenna_mask & BIT(2)) ? 0x5 : 0xf);
	mt76_rmw_field(dev, MT_ANT_SWITCH_CON(8), MT_ANT_SWITCH_CON_MODE1(3),
		       (td->tx_antenna_mask & BIT(3)) ? 0xb : 0xf);

	for (i = 0; i < 4; i++) {
		u32 val;

		val = mt7615_rf_rr(dev, i, 0x48);
		val &= ~(0x3ff << 20);
		if (td->tx_antenna_mask & BIT(i))
			val |= 3 << 20;
		else
			val |= (2 << 28) | (2 << 26) | (8 << 20);
		mt7615_rf_wr(dev, i, 0x48, val);
	}
}

static void
mt7615_tm_set_tx_frames(struct mt7615_phy *phy, bool en)
{
	struct mt7615_dev *dev = phy->dev;
	struct ieee80211_tx_info *info;
	struct sk_buff *skb = phy->mt76->test.tx_skb;

	mt7615_mcu_set_chan_info(phy, MCU_EXT_CMD_SET_RX_PATH);
	mt7615_tm_set_tx_antenna(phy, en);
	mt7615_tm_set_rx_enable(dev, !en);
	if (!en || !skb)
		return;

	info = IEEE80211_SKB_CB(skb);
	info->control.vif = phy->monitor_vif;
}

static void
mt7615_tm_update_params(struct mt7615_phy *phy, u32 changed)
{
	struct mt7615_dev *dev = phy->dev;
	struct mt76_testmode_data *td = &phy->mt76->test;
	bool en = phy->mt76->test.state != MT76_TM_STATE_OFF;

	if (changed & BIT(TM_CHANGED_TXPOWER_CTRL))
		mt7615_mcu_set_test_param(dev, MCU_ATE_SET_TX_POWER_CONTROL,
					  en, en && td->tx_power_control);
	if (changed & BIT(TM_CHANGED_FREQ_OFFSET))
		mt7615_mcu_set_test_param(dev, MCU_ATE_SET_FREQ_OFFSET,
					  en, en ? td->freq_offset : 0);
	if (changed & BIT(TM_CHANGED_TXPOWER))
		mt7615_tm_set_tx_power(phy);
}

static int
mt7615_tm_set_state(struct mt76_phy *mphy, enum mt76_testmode_state state)
{
	struct mt7615_phy *phy = mphy->priv;
	struct mt76_testmode_data *td = &mphy->test;
	enum mt76_testmode_state prev_state = td->state;

	mphy->test.state = state;

	if (prev_state == MT76_TM_STATE_TX_FRAMES)
		mt7615_tm_set_tx_frames(phy, false);
	else if (state == MT76_TM_STATE_TX_FRAMES)
		mt7615_tm_set_tx_frames(phy, true);

	if (state <= MT76_TM_STATE_IDLE)
		mt7615_tm_init(phy);

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

		mt7615_tm_update_params(phy, changed);
	}

	return 0;
}

static int
mt7615_tm_set_params(struct mt76_phy *mphy, struct nlattr **tb,
		     enum mt76_testmode_state new_state)
{
	struct mt76_testmode_data *td = &mphy->test;
	struct mt7615_phy *phy = mphy->priv;
	u32 changed = 0;
	int i;

	BUILD_BUG_ON(NUM_TM_CHANGED >= 32);

	if (new_state == MT76_TM_STATE_OFF ||
	    td->state == MT76_TM_STATE_OFF)
		return 0;

	if (td->tx_antenna_mask & ~mphy->chainmask)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(tm_change_map); i++) {
		if (tb[tm_change_map[i]])
			changed |= BIT(i);
	}

	mt7615_tm_update_params(phy, changed);

	return 0;
}

static int
mt7615_tm_dump_stats(struct mt76_phy *mphy, struct sk_buff *msg)
{
	struct mt7615_phy *phy = mphy->priv;
	void *rx, *rssi;
	int i;

	rx = nla_nest_start(msg, MT76_TM_STATS_ATTR_LAST_RX);
	if (!rx)
		return -ENOMEM;

	if (nla_put_s32(msg, MT76_TM_RX_ATTR_FREQ_OFFSET, phy->test.last_freq_offset))
		return -ENOMEM;

	rssi = nla_nest_start(msg, MT76_TM_RX_ATTR_RCPI);
	if (!rssi)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(phy->test.last_rcpi); i++)
		if (nla_put_u8(msg, i, phy->test.last_rcpi[i]))
			return -ENOMEM;

	nla_nest_end(msg, rssi);

	rssi = nla_nest_start(msg, MT76_TM_RX_ATTR_IB_RSSI);
	if (!rssi)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(phy->test.last_ib_rssi); i++)
		if (nla_put_s8(msg, i, phy->test.last_ib_rssi[i]))
			return -ENOMEM;

	nla_nest_end(msg, rssi);

	rssi = nla_nest_start(msg, MT76_TM_RX_ATTR_WB_RSSI);
	if (!rssi)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(phy->test.last_wb_rssi); i++)
		if (nla_put_s8(msg, i, phy->test.last_wb_rssi[i]))
			return -ENOMEM;

	nla_nest_end(msg, rssi);

	nla_nest_end(msg, rx);

	return 0;
}

const struct mt76_testmode_ops mt7615_testmode_ops = {
	.set_state = mt7615_tm_set_state,
	.set_params = mt7615_tm_set_params,
	.dump_stats = mt7615_tm_dump_stats,
};
