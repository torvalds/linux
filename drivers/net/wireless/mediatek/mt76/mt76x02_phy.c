// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (C) 2016 Felix Fietkau <nbd@nbd.name>
 * Copyright (C) 2018 Lorenzo Bianconi <lorenzo.bianconi83@gmail.com>
 */

#include <linux/kernel.h>

#include "mt76x02.h"
#include "mt76x02_phy.h"

void mt76x02_phy_set_rxpath(struct mt76x02_dev *dev)
{
	u32 val;

	val = mt76_rr(dev, MT_BBP(AGC, 0));
	val &= ~BIT(4);

	switch (dev->mphy.chainmask & 0xf) {
	case 2:
		val |= BIT(3);
		break;
	default:
		val &= ~BIT(3);
		break;
	}

	mt76_wr(dev, MT_BBP(AGC, 0), val);
	mb();
	val = mt76_rr(dev, MT_BBP(AGC, 0));
}
EXPORT_SYMBOL_GPL(mt76x02_phy_set_rxpath);

void mt76x02_phy_set_txdac(struct mt76x02_dev *dev)
{
	int txpath;

	txpath = (dev->mphy.chainmask >> 8) & 0xf;
	switch (txpath) {
	case 2:
		mt76_set(dev, MT_BBP(TXBE, 5), 0x3);
		break;
	default:
		mt76_clear(dev, MT_BBP(TXBE, 5), 0x3);
		break;
	}
}
EXPORT_SYMBOL_GPL(mt76x02_phy_set_txdac);

static u32
mt76x02_tx_power_mask(u8 v1, u8 v2, u8 v3, u8 v4)
{
	u32 val = 0;

	val |= (v1 & (BIT(6) - 1)) << 0;
	val |= (v2 & (BIT(6) - 1)) << 8;
	val |= (v3 & (BIT(6) - 1)) << 16;
	val |= (v4 & (BIT(6) - 1)) << 24;
	return val;
}

int mt76x02_get_max_rate_power(struct mt76x02_rate_power *r)
{
	s8 ret = 0;
	int i;

	for (i = 0; i < sizeof(r->all); i++)
		ret = max(ret, r->all[i]);

	return ret;
}
EXPORT_SYMBOL_GPL(mt76x02_get_max_rate_power);

void mt76x02_limit_rate_power(struct mt76x02_rate_power *r, int limit)
{
	int i;

	for (i = 0; i < sizeof(r->all); i++)
		if (r->all[i] > limit)
			r->all[i] = limit;
}
EXPORT_SYMBOL_GPL(mt76x02_limit_rate_power);

void mt76x02_add_rate_power_offset(struct mt76x02_rate_power *r, int offset)
{
	int i;

	for (i = 0; i < sizeof(r->all); i++)
		r->all[i] += offset;
}
EXPORT_SYMBOL_GPL(mt76x02_add_rate_power_offset);

void mt76x02_phy_set_txpower(struct mt76x02_dev *dev, int txp_0, int txp_1)
{
	struct mt76x02_rate_power *t = &dev->rate_power;

	mt76_rmw_field(dev, MT_TX_ALC_CFG_0, MT_TX_ALC_CFG_0_CH_INIT_0, txp_0);
	mt76_rmw_field(dev, MT_TX_ALC_CFG_0, MT_TX_ALC_CFG_0_CH_INIT_1, txp_1);

	mt76_wr(dev, MT_TX_PWR_CFG_0,
		mt76x02_tx_power_mask(t->cck[0], t->cck[2], t->ofdm[0],
				      t->ofdm[2]));
	mt76_wr(dev, MT_TX_PWR_CFG_1,
		mt76x02_tx_power_mask(t->ofdm[4], t->ofdm[6], t->ht[0],
				      t->ht[2]));
	mt76_wr(dev, MT_TX_PWR_CFG_2,
		mt76x02_tx_power_mask(t->ht[4], t->ht[6], t->ht[8],
				      t->ht[10]));
	mt76_wr(dev, MT_TX_PWR_CFG_3,
		mt76x02_tx_power_mask(t->ht[12], t->ht[14], t->ht[0],
				      t->ht[2]));
	mt76_wr(dev, MT_TX_PWR_CFG_4,
		mt76x02_tx_power_mask(t->ht[4], t->ht[6], 0, 0));
	mt76_wr(dev, MT_TX_PWR_CFG_7,
		mt76x02_tx_power_mask(t->ofdm[7], t->vht[0], t->ht[7],
				      t->vht[1]));
	mt76_wr(dev, MT_TX_PWR_CFG_8,
		mt76x02_tx_power_mask(t->ht[14], 0, t->vht[0], t->vht[1]));
	mt76_wr(dev, MT_TX_PWR_CFG_9,
		mt76x02_tx_power_mask(t->ht[7], 0, t->vht[0], t->vht[1]));
}
EXPORT_SYMBOL_GPL(mt76x02_phy_set_txpower);

void mt76x02_phy_set_bw(struct mt76x02_dev *dev, int width, u8 ctrl)
{
	int core_val, agc_val;

	switch (width) {
	case NL80211_CHAN_WIDTH_80:
		core_val = 3;
		agc_val = 7;
		break;
	case NL80211_CHAN_WIDTH_40:
		core_val = 2;
		agc_val = 3;
		break;
	default:
		core_val = 0;
		agc_val = 1;
		break;
	}

	mt76_rmw_field(dev, MT_BBP(CORE, 1), MT_BBP_CORE_R1_BW, core_val);
	mt76_rmw_field(dev, MT_BBP(AGC, 0), MT_BBP_AGC_R0_BW, agc_val);
	mt76_rmw_field(dev, MT_BBP(AGC, 0), MT_BBP_AGC_R0_CTRL_CHAN, ctrl);
	mt76_rmw_field(dev, MT_BBP(TXBE, 0), MT_BBP_TXBE_R0_CTRL_CHAN, ctrl);
}
EXPORT_SYMBOL_GPL(mt76x02_phy_set_bw);

void mt76x02_phy_set_band(struct mt76x02_dev *dev, int band,
			  bool primary_upper)
{
	switch (band) {
	case NL80211_BAND_2GHZ:
		mt76_set(dev, MT_TX_BAND_CFG, MT_TX_BAND_CFG_2G);
		mt76_clear(dev, MT_TX_BAND_CFG, MT_TX_BAND_CFG_5G);
		break;
	case NL80211_BAND_5GHZ:
		mt76_clear(dev, MT_TX_BAND_CFG, MT_TX_BAND_CFG_2G);
		mt76_set(dev, MT_TX_BAND_CFG, MT_TX_BAND_CFG_5G);
		break;
	}

	mt76_rmw_field(dev, MT_TX_BAND_CFG, MT_TX_BAND_CFG_UPPER_40M,
		       primary_upper);
}
EXPORT_SYMBOL_GPL(mt76x02_phy_set_band);

bool mt76x02_phy_adjust_vga_gain(struct mt76x02_dev *dev)
{
	u8 limit = dev->cal.low_gain > 0 ? 16 : 4;
	bool ret = false;
	u32 false_cca;

	false_cca = FIELD_GET(MT_RX_STAT_1_CCA_ERRORS,
			      mt76_rr(dev, MT_RX_STAT_1));
	dev->cal.false_cca = false_cca;
	if (false_cca > 800 && dev->cal.agc_gain_adjust < limit) {
		dev->cal.agc_gain_adjust += 2;
		ret = true;
	} else if ((false_cca < 10 && dev->cal.agc_gain_adjust > 0) ||
		   (dev->cal.agc_gain_adjust >= limit && false_cca < 500)) {
		dev->cal.agc_gain_adjust -= 2;
		ret = true;
	}

	dev->cal.agc_lowest_gain = dev->cal.agc_gain_adjust >= limit;

	return ret;
}
EXPORT_SYMBOL_GPL(mt76x02_phy_adjust_vga_gain);

void mt76x02_init_agc_gain(struct mt76x02_dev *dev)
{
	dev->cal.agc_gain_init[0] = mt76_get_field(dev, MT_BBP(AGC, 8),
						   MT_BBP_AGC_GAIN);
	dev->cal.agc_gain_init[1] = mt76_get_field(dev, MT_BBP(AGC, 9),
						   MT_BBP_AGC_GAIN);
	memcpy(dev->cal.agc_gain_cur, dev->cal.agc_gain_init,
	       sizeof(dev->cal.agc_gain_cur));
	dev->cal.low_gain = -1;
	dev->cal.gain_init_done = true;
}
EXPORT_SYMBOL_GPL(mt76x02_init_agc_gain);
