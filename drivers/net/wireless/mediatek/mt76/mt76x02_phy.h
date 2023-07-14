/* SPDX-License-Identifier: ISC */
/*
 * Copyright (C) 2018 Lorenzo Bianconi <lorenzo.bianconi83@gmail.com>
 */

#ifndef __MT76x02_PHY_H
#define __MT76x02_PHY_H

#include "mt76x02_regs.h"

static inline int
mt76x02_get_rssi_gain_thresh(struct mt76x02_dev *dev)
{
	switch (dev->mphy.chandef.width) {
	case NL80211_CHAN_WIDTH_80:
		return -62;
	case NL80211_CHAN_WIDTH_40:
		return -65;
	default:
		return -68;
	}
}

static inline int
mt76x02_get_low_rssi_gain_thresh(struct mt76x02_dev *dev)
{
	switch (dev->mphy.chandef.width) {
	case NL80211_CHAN_WIDTH_80:
		return -76;
	case NL80211_CHAN_WIDTH_40:
		return -79;
	default:
		return -82;
	}
}

void mt76x02_add_rate_power_offset(struct mt76x02_rate_power *r, int offset);
void mt76x02_phy_set_txpower(struct mt76x02_dev *dev, int txp_0, int txp_2);
void mt76x02_limit_rate_power(struct mt76x02_rate_power *r, int limit);
int mt76x02_get_max_rate_power(struct mt76x02_rate_power *r);
void mt76x02_phy_set_rxpath(struct mt76x02_dev *dev);
void mt76x02_phy_set_txdac(struct mt76x02_dev *dev);
void mt76x02_phy_set_bw(struct mt76x02_dev *dev, int width, u8 ctrl);
void mt76x02_phy_set_band(struct mt76x02_dev *dev, int band,
			  bool primary_upper);
bool mt76x02_phy_adjust_vga_gain(struct mt76x02_dev *dev);
void mt76x02_init_agc_gain(struct mt76x02_dev *dev);

#endif /* __MT76x02_PHY_H */
