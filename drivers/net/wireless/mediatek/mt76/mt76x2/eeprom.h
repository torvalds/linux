/*
 * Copyright (C) 2016 Felix Fietkau <nbd@nbd.name>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef __MT76x2_EEPROM_H
#define __MT76x2_EEPROM_H

#include "../mt76x02_eeprom.h"

enum mt76x2_cal_channel_group {
	MT_CH_5G_JAPAN,
	MT_CH_5G_UNII_1,
	MT_CH_5G_UNII_2,
	MT_CH_5G_UNII_2E_1,
	MT_CH_5G_UNII_2E_2,
	MT_CH_5G_UNII_3,
	__MT_CH_MAX
};

struct mt76x2_tx_power_info {
	u8 target_power;

	s8 delta_bw40;
	s8 delta_bw80;

	struct {
		s8 tssi_slope;
		s8 tssi_offset;
		s8 target_power;
		s8 delta;
	} chain[MT_MAX_CHAINS];
};

struct mt76x2_temp_comp {
	u8 temp_25_ref;
	int lower_bound; /* J */
	int upper_bound; /* J */
	unsigned int high_slope; /* J / dB */
	unsigned int low_slope; /* J / dB */
};

void mt76x2_get_rate_power(struct mt76x02_dev *dev, struct mt76_rate_power *t,
			   struct ieee80211_channel *chan);
void mt76x2_get_power_info(struct mt76x02_dev *dev,
			   struct mt76x2_tx_power_info *t,
			   struct ieee80211_channel *chan);
int mt76x2_get_temp_comp(struct mt76x02_dev *dev, struct mt76x2_temp_comp *t);
void mt76x2_read_rx_gain(struct mt76x02_dev *dev);

static inline bool
mt76x2_has_ext_lna(struct mt76x02_dev *dev)
{
	u32 val = mt76x02_eeprom_get(dev, MT_EE_NIC_CONF_1);

	if (dev->mt76.chandef.chan->band == NL80211_BAND_2GHZ)
		return val & MT_EE_NIC_CONF_1_LNA_EXT_2G;
	else
		return val & MT_EE_NIC_CONF_1_LNA_EXT_5G;
}

#endif
