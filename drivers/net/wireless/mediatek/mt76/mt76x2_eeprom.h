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

#include "mt76x2.h"

enum mt76x2_eeprom_field {
	MT_EE_CHIP_ID =				0x000,
	MT_EE_VERSION =				0x002,
	MT_EE_MAC_ADDR =			0x004,
	MT_EE_PCI_ID =				0x00A,
	MT_EE_NIC_CONF_0 =			0x034,
	MT_EE_NIC_CONF_1 =			0x036,
	MT_EE_NIC_CONF_2 =			0x042,

	MT_EE_XTAL_TRIM_1 =			0x03a,
	MT_EE_XTAL_TRIM_2 =			0x09e,

	MT_EE_LNA_GAIN =			0x044,
	MT_EE_RSSI_OFFSET_2G_0 =		0x046,
	MT_EE_RSSI_OFFSET_2G_1 =		0x048,
	MT_EE_RSSI_OFFSET_5G_0 =		0x04a,
	MT_EE_RSSI_OFFSET_5G_1 =		0x04c,

	MT_EE_TX_POWER_DELTA_BW40 =		0x050,
	MT_EE_TX_POWER_DELTA_BW80 =		0x052,

	MT_EE_TX_POWER_EXT_PA_5G =		0x054,

	MT_EE_TX_POWER_0_START_2G =		0x056,
	MT_EE_TX_POWER_1_START_2G =		0x05c,

	/* used as byte arrays */
#define MT_TX_POWER_GROUP_SIZE_5G		5
#define MT_TX_POWER_GROUPS_5G			6
	MT_EE_TX_POWER_0_START_5G =		0x062,

	MT_EE_TX_POWER_0_GRP3_TX_POWER_DELTA =	0x074,
	MT_EE_TX_POWER_0_GRP4_TSSI_SLOPE =	0x076,

	MT_EE_TX_POWER_1_START_5G =		0x080,

	MT_EE_TX_POWER_CCK =			0x0a0,
	MT_EE_TX_POWER_OFDM_2G_6M =		0x0a2,
	MT_EE_TX_POWER_OFDM_2G_24M =		0x0a4,
	MT_EE_TX_POWER_OFDM_5G_6M =		0x0b2,
	MT_EE_TX_POWER_OFDM_5G_24M =		0x0b4,
	MT_EE_TX_POWER_HT_MCS0 =		0x0a6,
	MT_EE_TX_POWER_HT_MCS4 =		0x0a8,
	MT_EE_TX_POWER_HT_MCS8 =		0x0aa,
	MT_EE_TX_POWER_HT_MCS12 =		0x0ac,
	MT_EE_TX_POWER_VHT_MCS0 =		0x0ba,
	MT_EE_TX_POWER_VHT_MCS4 =		0x0bc,
	MT_EE_TX_POWER_VHT_MCS8 =		0x0be,

	MT_EE_RF_TEMP_COMP_SLOPE_5G =		0x0f2,
	MT_EE_RF_TEMP_COMP_SLOPE_2G =		0x0f4,

	MT_EE_RF_2G_TSSI_OFF_TXPOWER =		0x0f6,
	MT_EE_RF_2G_RX_HIGH_GAIN =		0x0f8,
	MT_EE_RF_5G_GRP0_1_RX_HIGH_GAIN =	0x0fa,
	MT_EE_RF_5G_GRP2_3_RX_HIGH_GAIN =	0x0fc,
	MT_EE_RF_5G_GRP4_5_RX_HIGH_GAIN =	0x0fe,

	MT_EE_BT_RCAL_RESULT =			0x138,
	MT_EE_BT_VCDL_CALIBRATION =		0x13c,
	MT_EE_BT_PMUCFG =			0x13e,

	__MT_EE_MAX
};

#define MT_EE_NIC_CONF_0_PA_INT_2G		BIT(8)
#define MT_EE_NIC_CONF_0_PA_INT_5G		BIT(9)
#define MT_EE_NIC_CONF_0_BOARD_TYPE		GENMASK(13, 12)

#define MT_EE_NIC_CONF_1_TEMP_TX_ALC		BIT(1)
#define MT_EE_NIC_CONF_1_LNA_EXT_2G		BIT(2)
#define MT_EE_NIC_CONF_1_LNA_EXT_5G		BIT(3)
#define MT_EE_NIC_CONF_1_TX_ALC_EN		BIT(13)

#define MT_EE_NIC_CONF_2_RX_STREAM		GENMASK(3, 0)
#define MT_EE_NIC_CONF_2_TX_STREAM		GENMASK(7, 4)
#define MT_EE_NIC_CONF_2_HW_ANTDIV		BIT(8)
#define MT_EE_NIC_CONF_2_XTAL_OPTION		GENMASK(10, 9)
#define MT_EE_NIC_CONF_2_TEMP_DISABLE		BIT(11)
#define MT_EE_NIC_CONF_2_COEX_METHOD		GENMASK(15, 13)

enum mt76x2_board_type {
	BOARD_TYPE_2GHZ = 1,
	BOARD_TYPE_5GHZ = 2,
};

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

static inline int
mt76x2_eeprom_get(struct mt76x2_dev *dev, enum mt76x2_eeprom_field field)
{
	if ((field & 1) || field >= __MT_EE_MAX)
		return -1;

	return get_unaligned_le16(dev->mt76.eeprom.data + field);
}

void mt76x2_get_rate_power(struct mt76x2_dev *dev, struct mt76_rate_power *t,
			   struct ieee80211_channel *chan);
int mt76x2_get_max_rate_power(struct mt76_rate_power *r);
void mt76x2_get_power_info(struct mt76x2_dev *dev,
			   struct mt76x2_tx_power_info *t,
			   struct ieee80211_channel *chan);
int mt76x2_get_temp_comp(struct mt76x2_dev *dev, struct mt76x2_temp_comp *t);
bool mt76x2_ext_pa_enabled(struct mt76x2_dev *dev, enum nl80211_band band);
void mt76x2_read_rx_gain(struct mt76x2_dev *dev);
void mt76x2_eeprom_parse_hw_cap(struct mt76x2_dev *dev);

static inline bool
mt76x2_temp_tx_alc_enabled(struct mt76x2_dev *dev)
{
	u16 val;

	val = mt76x2_eeprom_get(dev, MT_EE_TX_POWER_EXT_PA_5G);
	if (!(val & BIT(15)))
		return false;

	return mt76x2_eeprom_get(dev, MT_EE_NIC_CONF_1) &
	       MT_EE_NIC_CONF_1_TEMP_TX_ALC;
}

static inline bool
mt76x2_tssi_enabled(struct mt76x2_dev *dev)
{
	return !mt76x2_temp_tx_alc_enabled(dev) &&
	       (mt76x2_eeprom_get(dev, MT_EE_NIC_CONF_1) &
		MT_EE_NIC_CONF_1_TX_ALC_EN);
}

static inline bool
mt76x2_has_ext_lna(struct mt76x2_dev *dev)
{
	u32 val = mt76x2_eeprom_get(dev, MT_EE_NIC_CONF_1);

	if (dev->mt76.chandef.chan->band == NL80211_BAND_2GHZ)
		return val & MT_EE_NIC_CONF_1_LNA_EXT_2G;
	else
		return val & MT_EE_NIC_CONF_1_LNA_EXT_5G;
}

#endif
