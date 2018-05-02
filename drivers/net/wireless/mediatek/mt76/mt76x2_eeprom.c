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

#include <asm/unaligned.h>
#include "mt76x2.h"
#include "mt76x2_eeprom.h"

#define EE_FIELD(_name, _value) [MT_EE_##_name] = (_value) | 1

static int
mt76x2_eeprom_copy(struct mt76x2_dev *dev, enum mt76x2_eeprom_field field,
		   void *dest, int len)
{
	if (field + len > dev->mt76.eeprom.size)
		return -1;

	memcpy(dest, dev->mt76.eeprom.data + field, len);
	return 0;
}

static int
mt76x2_eeprom_get_macaddr(struct mt76x2_dev *dev)
{
	void *src = dev->mt76.eeprom.data + MT_EE_MAC_ADDR;

	memcpy(dev->mt76.macaddr, src, ETH_ALEN);
	return 0;
}

static void
mt76x2_eeprom_parse_hw_cap(struct mt76x2_dev *dev)
{
	u16 val = mt76x2_eeprom_get(dev, MT_EE_NIC_CONF_0);

	switch (FIELD_GET(MT_EE_NIC_CONF_0_BOARD_TYPE, val)) {
	case BOARD_TYPE_5GHZ:
		dev->mt76.cap.has_5ghz = true;
		break;
	case BOARD_TYPE_2GHZ:
		dev->mt76.cap.has_2ghz = true;
		break;
	default:
		dev->mt76.cap.has_2ghz = true;
		dev->mt76.cap.has_5ghz = true;
		break;
	}
}

static int
mt76x2_efuse_read(struct mt76x2_dev *dev, u16 addr, u8 *data)
{
	u32 val;
	int i;

	val = mt76_rr(dev, MT_EFUSE_CTRL);
	val &= ~(MT_EFUSE_CTRL_AIN |
		 MT_EFUSE_CTRL_MODE);
	val |= FIELD_PREP(MT_EFUSE_CTRL_AIN, addr & ~0xf);
	val |= MT_EFUSE_CTRL_KICK;
	mt76_wr(dev, MT_EFUSE_CTRL, val);

	if (!mt76_poll(dev, MT_EFUSE_CTRL, MT_EFUSE_CTRL_KICK, 0, 1000))
		return -ETIMEDOUT;

	udelay(2);

	val = mt76_rr(dev, MT_EFUSE_CTRL);
	if ((val & MT_EFUSE_CTRL_AOUT) == MT_EFUSE_CTRL_AOUT) {
		memset(data, 0xff, 16);
		return 0;
	}

	for (i = 0; i < 4; i++) {
		val = mt76_rr(dev, MT_EFUSE_DATA(i));
		put_unaligned_le32(val, data + 4 * i);
	}

	return 0;
}

static int
mt76x2_get_efuse_data(struct mt76x2_dev *dev, void *buf, int len)
{
	int ret, i;

	for (i = 0; i + 16 <= len; i += 16) {
		ret = mt76x2_efuse_read(dev, i, buf + i);
		if (ret)
			return ret;
	}

	return 0;
}

static bool
mt76x2_has_cal_free_data(struct mt76x2_dev *dev, u8 *efuse)
{
	u16 *efuse_w = (u16 *) efuse;

	if (efuse_w[MT_EE_NIC_CONF_0] != 0)
		return false;

	if (efuse_w[MT_EE_XTAL_TRIM_1] == 0xffff)
		return false;

	if (efuse_w[MT_EE_TX_POWER_DELTA_BW40] != 0)
		return false;

	if (efuse_w[MT_EE_TX_POWER_0_START_2G] == 0xffff)
		return false;

	if (efuse_w[MT_EE_TX_POWER_0_GRP3_TX_POWER_DELTA] != 0)
		return false;

	if (efuse_w[MT_EE_TX_POWER_0_GRP4_TSSI_SLOPE] == 0xffff)
		return false;

	return true;
}

static void
mt76x2_apply_cal_free_data(struct mt76x2_dev *dev, u8 *efuse)
{
#define GROUP_5G(_id)							   \
	MT_EE_TX_POWER_0_START_5G + MT_TX_POWER_GROUP_SIZE_5G * (_id),	   \
	MT_EE_TX_POWER_0_START_5G + MT_TX_POWER_GROUP_SIZE_5G * (_id) + 1, \
	MT_EE_TX_POWER_1_START_5G + MT_TX_POWER_GROUP_SIZE_5G * (_id),	   \
	MT_EE_TX_POWER_1_START_5G + MT_TX_POWER_GROUP_SIZE_5G * (_id) + 1

	static const u8 cal_free_bytes[] = {
		MT_EE_XTAL_TRIM_1,
		MT_EE_TX_POWER_EXT_PA_5G + 1,
		MT_EE_TX_POWER_0_START_2G,
		MT_EE_TX_POWER_0_START_2G + 1,
		MT_EE_TX_POWER_1_START_2G,
		MT_EE_TX_POWER_1_START_2G + 1,
		GROUP_5G(0),
		GROUP_5G(1),
		GROUP_5G(2),
		GROUP_5G(3),
		GROUP_5G(4),
		GROUP_5G(5),
		MT_EE_RF_2G_TSSI_OFF_TXPOWER,
		MT_EE_RF_2G_RX_HIGH_GAIN + 1,
		MT_EE_RF_5G_GRP0_1_RX_HIGH_GAIN,
		MT_EE_RF_5G_GRP0_1_RX_HIGH_GAIN + 1,
		MT_EE_RF_5G_GRP2_3_RX_HIGH_GAIN,
		MT_EE_RF_5G_GRP2_3_RX_HIGH_GAIN + 1,
		MT_EE_RF_5G_GRP4_5_RX_HIGH_GAIN,
		MT_EE_RF_5G_GRP4_5_RX_HIGH_GAIN + 1,
	};
	u8 *eeprom = dev->mt76.eeprom.data;
	u8 prev_grp0[4] = {
		eeprom[MT_EE_TX_POWER_0_START_5G],
		eeprom[MT_EE_TX_POWER_0_START_5G + 1],
		eeprom[MT_EE_TX_POWER_1_START_5G],
		eeprom[MT_EE_TX_POWER_1_START_5G + 1]
	};
	u16 val;
	int i;

	if (!mt76x2_has_cal_free_data(dev, efuse))
		return;

	for (i = 0; i < ARRAY_SIZE(cal_free_bytes); i++) {
		int offset = cal_free_bytes[i];

		eeprom[offset] = efuse[offset];
	}

	if (!(efuse[MT_EE_TX_POWER_0_START_5G] |
	      efuse[MT_EE_TX_POWER_0_START_5G + 1]))
		memcpy(eeprom + MT_EE_TX_POWER_0_START_5G, prev_grp0, 2);
	if (!(efuse[MT_EE_TX_POWER_1_START_5G] |
	      efuse[MT_EE_TX_POWER_1_START_5G + 1]))
		memcpy(eeprom + MT_EE_TX_POWER_1_START_5G, prev_grp0 + 2, 2);

	val = get_unaligned_le16(efuse + MT_EE_BT_RCAL_RESULT);
	if (val != 0xffff)
		eeprom[MT_EE_BT_RCAL_RESULT] = val & 0xff;

	val = get_unaligned_le16(efuse + MT_EE_BT_VCDL_CALIBRATION);
	if (val != 0xffff)
		eeprom[MT_EE_BT_VCDL_CALIBRATION + 1] = val >> 8;

	val = get_unaligned_le16(efuse + MT_EE_BT_PMUCFG);
	if (val != 0xffff)
		eeprom[MT_EE_BT_PMUCFG] = val & 0xff;
}

static int mt76x2_check_eeprom(struct mt76x2_dev *dev)
{
	u16 val = get_unaligned_le16(dev->mt76.eeprom.data);

	if (!val)
		val = get_unaligned_le16(dev->mt76.eeprom.data + MT_EE_PCI_ID);

	switch (val) {
	case 0x7662:
	case 0x7612:
		return 0;
	default:
		dev_err(dev->mt76.dev, "EEPROM data check failed: %04x\n", val);
		return -EINVAL;
	}
}

static int
mt76x2_eeprom_load(struct mt76x2_dev *dev)
{
	void *efuse;
	bool found;
	int ret;

	ret = mt76_eeprom_init(&dev->mt76, MT7662_EEPROM_SIZE);
	if (ret < 0)
		return ret;

	found = ret;
	if (found)
		found = !mt76x2_check_eeprom(dev);

	dev->mt76.otp.data = devm_kzalloc(dev->mt76.dev, MT7662_EEPROM_SIZE,
					  GFP_KERNEL);
	dev->mt76.otp.size = MT7662_EEPROM_SIZE;
	if (!dev->mt76.otp.data)
		return -ENOMEM;

	efuse = dev->mt76.otp.data;

	if (mt76x2_get_efuse_data(dev, efuse, MT7662_EEPROM_SIZE))
		goto out;

	if (found) {
		mt76x2_apply_cal_free_data(dev, efuse);
	} else {
		/* FIXME: check if efuse data is complete */
		found = true;
		memcpy(dev->mt76.eeprom.data, efuse, MT7662_EEPROM_SIZE);
	}

out:
	if (!found)
		return -ENOENT;

	return 0;
}

static inline int
mt76x2_sign_extend(u32 val, unsigned int size)
{
	bool sign = val & BIT(size - 1);

	val &= BIT(size - 1) - 1;

	return sign ? val : -val;
}

static inline int
mt76x2_sign_extend_optional(u32 val, unsigned int size)
{
	bool enable = val & BIT(size);

	return enable ? mt76x2_sign_extend(val, size) : 0;
}

static bool
field_valid(u8 val)
{
	return val != 0 && val != 0xff;
}

static void
mt76x2_set_rx_gain_group(struct mt76x2_dev *dev, u8 val)
{
	s8 *dest = dev->cal.rx.high_gain;

	if (!field_valid(val)) {
		dest[0] = 0;
		dest[1] = 0;
		return;
	}

	dest[0] = mt76x2_sign_extend(val, 4);
	dest[1] = mt76x2_sign_extend(val >> 4, 4);
}

static void
mt76x2_set_rssi_offset(struct mt76x2_dev *dev, int chain, u8 val)
{
	s8 *dest = dev->cal.rx.rssi_offset;

	if (!field_valid(val)) {
		dest[chain] = 0;
		return;
	}

	dest[chain] = mt76x2_sign_extend_optional(val, 7);
}

static enum mt76x2_cal_channel_group
mt76x2_get_cal_channel_group(int channel)
{
	if (channel >= 184 && channel <= 196)
		return MT_CH_5G_JAPAN;
	if (channel <= 48)
		return MT_CH_5G_UNII_1;
	if (channel <= 64)
		return MT_CH_5G_UNII_2;
	if (channel <= 114)
		return MT_CH_5G_UNII_2E_1;
	if (channel <= 144)
		return MT_CH_5G_UNII_2E_2;
	return MT_CH_5G_UNII_3;
}

static u8
mt76x2_get_5g_rx_gain(struct mt76x2_dev *dev, u8 channel)
{
	enum mt76x2_cal_channel_group group;

	group = mt76x2_get_cal_channel_group(channel);
	switch (group) {
	case MT_CH_5G_JAPAN:
		return mt76x2_eeprom_get(dev, MT_EE_RF_5G_GRP0_1_RX_HIGH_GAIN);
	case MT_CH_5G_UNII_1:
		return mt76x2_eeprom_get(dev, MT_EE_RF_5G_GRP0_1_RX_HIGH_GAIN) >> 8;
	case MT_CH_5G_UNII_2:
		return mt76x2_eeprom_get(dev, MT_EE_RF_5G_GRP2_3_RX_HIGH_GAIN);
	case MT_CH_5G_UNII_2E_1:
		return mt76x2_eeprom_get(dev, MT_EE_RF_5G_GRP2_3_RX_HIGH_GAIN) >> 8;
	case MT_CH_5G_UNII_2E_2:
		return mt76x2_eeprom_get(dev, MT_EE_RF_5G_GRP4_5_RX_HIGH_GAIN);
	default:
		return mt76x2_eeprom_get(dev, MT_EE_RF_5G_GRP4_5_RX_HIGH_GAIN) >> 8;
	}
}

void mt76x2_read_rx_gain(struct mt76x2_dev *dev)
{
	struct ieee80211_channel *chan = dev->mt76.chandef.chan;
	int channel = chan->hw_value;
	s8 lna_5g[3], lna_2g;
	u8 lna;
	u16 val;

	if (chan->band == NL80211_BAND_2GHZ)
		val = mt76x2_eeprom_get(dev, MT_EE_RF_2G_RX_HIGH_GAIN) >> 8;
	else
		val = mt76x2_get_5g_rx_gain(dev, channel);

	mt76x2_set_rx_gain_group(dev, val);

	if (chan->band == NL80211_BAND_2GHZ) {
		val = mt76x2_eeprom_get(dev, MT_EE_RSSI_OFFSET_2G_0);
		mt76x2_set_rssi_offset(dev, 0, val);
		mt76x2_set_rssi_offset(dev, 1, val >> 8);
	} else {
		val = mt76x2_eeprom_get(dev, MT_EE_RSSI_OFFSET_5G_0);
		mt76x2_set_rssi_offset(dev, 0, val);
		mt76x2_set_rssi_offset(dev, 1, val >> 8);
	}

	val = mt76x2_eeprom_get(dev, MT_EE_LNA_GAIN);
	lna_2g = val & 0xff;
	lna_5g[0] = val >> 8;

	val = mt76x2_eeprom_get(dev, MT_EE_RSSI_OFFSET_2G_1);
	lna_5g[1] = val >> 8;

	val = mt76x2_eeprom_get(dev, MT_EE_RSSI_OFFSET_5G_1);
	lna_5g[2] = val >> 8;

	if (!field_valid(lna_5g[1]))
		lna_5g[1] = lna_5g[0];

	if (!field_valid(lna_5g[2]))
		lna_5g[2] = lna_5g[0];

	dev->cal.rx.mcu_gain =  (lna_2g & 0xff);
	dev->cal.rx.mcu_gain |= (lna_5g[0] & 0xff) << 8;
	dev->cal.rx.mcu_gain |= (lna_5g[1] & 0xff) << 16;
	dev->cal.rx.mcu_gain |= (lna_5g[2] & 0xff) << 24;

	val = mt76x2_eeprom_get(dev, MT_EE_NIC_CONF_1);
	if (val & MT_EE_NIC_CONF_1_LNA_EXT_2G)
		lna_2g = 0;
	if (val & MT_EE_NIC_CONF_1_LNA_EXT_5G)
		memset(lna_5g, 0, sizeof(lna_5g));

	if (chan->band == NL80211_BAND_2GHZ)
		lna = lna_2g;
	else if (channel <= 64)
		lna = lna_5g[0];
	else if (channel <= 128)
		lna = lna_5g[1];
	else
		lna = lna_5g[2];

	if (lna == 0xff)
		lna = 0;

	dev->cal.rx.lna_gain = mt76x2_sign_extend(lna, 8);
}

static s8
mt76x2_rate_power_val(u8 val)
{
	if (!field_valid(val))
		return 0;

	return mt76x2_sign_extend_optional(val, 7);
}

void mt76x2_get_rate_power(struct mt76x2_dev *dev, struct mt76_rate_power *t,
			   struct ieee80211_channel *chan)
{
	bool is_5ghz;
	u16 val;

	is_5ghz = chan->band == NL80211_BAND_5GHZ;

	memset(t, 0, sizeof(*t));

	val = mt76x2_eeprom_get(dev, MT_EE_TX_POWER_CCK);
	t->cck[0] = t->cck[1] = mt76x2_rate_power_val(val);
	t->cck[2] = t->cck[3] = mt76x2_rate_power_val(val >> 8);

	if (is_5ghz)
		val = mt76x2_eeprom_get(dev, MT_EE_TX_POWER_OFDM_5G_6M);
	else
		val = mt76x2_eeprom_get(dev, MT_EE_TX_POWER_OFDM_2G_6M);
	t->ofdm[0] = t->ofdm[1] = mt76x2_rate_power_val(val);
	t->ofdm[2] = t->ofdm[3] = mt76x2_rate_power_val(val >> 8);

	if (is_5ghz)
		val = mt76x2_eeprom_get(dev, MT_EE_TX_POWER_OFDM_5G_24M);
	else
		val = mt76x2_eeprom_get(dev, MT_EE_TX_POWER_OFDM_2G_24M);
	t->ofdm[4] = t->ofdm[5] = mt76x2_rate_power_val(val);
	t->ofdm[6] = t->ofdm[7] = mt76x2_rate_power_val(val >> 8);

	val = mt76x2_eeprom_get(dev, MT_EE_TX_POWER_HT_MCS0);
	t->ht[0] = t->ht[1] = mt76x2_rate_power_val(val);
	t->ht[2] = t->ht[3] = mt76x2_rate_power_val(val >> 8);

	val = mt76x2_eeprom_get(dev, MT_EE_TX_POWER_HT_MCS4);
	t->ht[4] = t->ht[5] = mt76x2_rate_power_val(val);
	t->ht[6] = t->ht[7] = mt76x2_rate_power_val(val >> 8);

	val = mt76x2_eeprom_get(dev, MT_EE_TX_POWER_HT_MCS8);
	t->ht[8] = t->ht[9] = mt76x2_rate_power_val(val);
	t->ht[10] = t->ht[11] = mt76x2_rate_power_val(val >> 8);

	val = mt76x2_eeprom_get(dev, MT_EE_TX_POWER_HT_MCS12);
	t->ht[12] = t->ht[13] = mt76x2_rate_power_val(val);
	t->ht[14] = t->ht[15] = mt76x2_rate_power_val(val >> 8);

	val = mt76x2_eeprom_get(dev, MT_EE_TX_POWER_VHT_MCS0);
	t->vht[0] = t->vht[1] = mt76x2_rate_power_val(val);
	t->vht[2] = t->vht[3] = mt76x2_rate_power_val(val >> 8);

	val = mt76x2_eeprom_get(dev, MT_EE_TX_POWER_VHT_MCS4);
	t->vht[4] = t->vht[5] = mt76x2_rate_power_val(val);
	t->vht[6] = t->vht[7] = mt76x2_rate_power_val(val >> 8);

	val = mt76x2_eeprom_get(dev, MT_EE_TX_POWER_VHT_MCS8);
	if (!is_5ghz)
		val >>= 8;
	t->vht[8] = t->vht[9] = mt76x2_rate_power_val(val >> 8);
}

int mt76x2_get_max_rate_power(struct mt76_rate_power *r)
{
	int i;
	s8 ret = 0;

	for (i = 0; i < sizeof(r->all); i++)
		ret = max(ret, r->all[i]);

	return ret;
}

static void
mt76x2_get_power_info_2g(struct mt76x2_dev *dev, struct mt76x2_tx_power_info *t,
		         struct ieee80211_channel *chan, int chain, int offset)
{
	int channel = chan->hw_value;
	int delta_idx;
	u8 data[6];
	u16 val;

	if (channel < 6)
		delta_idx = 3;
	else if (channel < 11)
		delta_idx = 4;
	else
		delta_idx = 5;

	mt76x2_eeprom_copy(dev, offset, data, sizeof(data));

	t->chain[chain].tssi_slope = data[0];
	t->chain[chain].tssi_offset = data[1];
	t->chain[chain].target_power = data[2];
	t->chain[chain].delta = mt76x2_sign_extend_optional(data[delta_idx], 7);

	val = mt76x2_eeprom_get(dev, MT_EE_RF_2G_TSSI_OFF_TXPOWER);
	t->target_power = val >> 8;
}

static void
mt76x2_get_power_info_5g(struct mt76x2_dev *dev, struct mt76x2_tx_power_info *t,
		         struct ieee80211_channel *chan, int chain, int offset)
{
	int channel = chan->hw_value;
	enum mt76x2_cal_channel_group group;
	int delta_idx;
	u16 val;
	u8 data[5];

	group = mt76x2_get_cal_channel_group(channel);
	offset += group * MT_TX_POWER_GROUP_SIZE_5G;

	if (channel >= 192)
		delta_idx = 4;
	else if (channel >= 184)
		delta_idx = 3;
	else if (channel < 44)
		delta_idx = 3;
	else if (channel < 52)
		delta_idx = 4;
	else if (channel < 58)
		delta_idx = 3;
	else if (channel < 98)
		delta_idx = 4;
	else if (channel < 106)
		delta_idx = 3;
	else if (channel < 116)
		delta_idx = 4;
	else if (channel < 130)
		delta_idx = 3;
	else if (channel < 149)
		delta_idx = 4;
	else if (channel < 157)
		delta_idx = 3;
	else
		delta_idx = 4;

	mt76x2_eeprom_copy(dev, offset, data, sizeof(data));

	t->chain[chain].tssi_slope = data[0];
	t->chain[chain].tssi_offset = data[1];
	t->chain[chain].target_power = data[2];
	t->chain[chain].delta = mt76x2_sign_extend_optional(data[delta_idx], 7);

	val = mt76x2_eeprom_get(dev, MT_EE_RF_2G_RX_HIGH_GAIN);
	t->target_power = val & 0xff;
}

void mt76x2_get_power_info(struct mt76x2_dev *dev,
			   struct mt76x2_tx_power_info *t,
			   struct ieee80211_channel *chan)
{
	u16 bw40, bw80;

	memset(t, 0, sizeof(*t));

	bw40 = mt76x2_eeprom_get(dev, MT_EE_TX_POWER_DELTA_BW40);
	bw80 = mt76x2_eeprom_get(dev, MT_EE_TX_POWER_DELTA_BW80);

	if (chan->band == NL80211_BAND_5GHZ) {
		bw40 >>= 8;
		mt76x2_get_power_info_5g(dev, t, chan, 0,
					 MT_EE_TX_POWER_0_START_5G);
		mt76x2_get_power_info_5g(dev, t, chan, 1,
					 MT_EE_TX_POWER_1_START_5G);
	} else {
		mt76x2_get_power_info_2g(dev, t, chan, 0,
					 MT_EE_TX_POWER_0_START_2G);
		mt76x2_get_power_info_2g(dev, t, chan, 1,
					 MT_EE_TX_POWER_1_START_2G);
	}

	if (mt76x2_tssi_enabled(dev) || !field_valid(t->target_power))
		t->target_power = t->chain[0].target_power;

	t->delta_bw40 = mt76x2_rate_power_val(bw40);
	t->delta_bw80 = mt76x2_rate_power_val(bw80);
}

int mt76x2_get_temp_comp(struct mt76x2_dev *dev, struct mt76x2_temp_comp *t)
{
	enum nl80211_band band = dev->mt76.chandef.chan->band;
	u16 val, slope;
	u8 bounds;

	memset(t, 0, sizeof(*t));

	val = mt76x2_eeprom_get(dev, MT_EE_NIC_CONF_1);
	if (!(val & MT_EE_NIC_CONF_1_TEMP_TX_ALC))
		return -EINVAL;

	if (!mt76x2_ext_pa_enabled(dev, band))
		return -EINVAL;

	val = mt76x2_eeprom_get(dev, MT_EE_TX_POWER_EXT_PA_5G) >> 8;
	if (!(val & BIT(7)))
		return -EINVAL;

	t->temp_25_ref = val & 0x7f;
	if (band == NL80211_BAND_5GHZ) {
		slope = mt76x2_eeprom_get(dev, MT_EE_RF_TEMP_COMP_SLOPE_5G);
		bounds = mt76x2_eeprom_get(dev, MT_EE_TX_POWER_EXT_PA_5G);
	} else {
		slope = mt76x2_eeprom_get(dev, MT_EE_RF_TEMP_COMP_SLOPE_2G);
		bounds = mt76x2_eeprom_get(dev, MT_EE_TX_POWER_DELTA_BW80) >> 8;
	}

	t->high_slope = slope & 0xff;
	t->low_slope = slope >> 8;
	t->lower_bound = 0 - (bounds & 0xf);
	t->upper_bound = (bounds >> 4) & 0xf;

	return 0;
}

bool mt76x2_ext_pa_enabled(struct mt76x2_dev *dev, enum nl80211_band band)
{
	u16 conf0 = mt76x2_eeprom_get(dev, MT_EE_NIC_CONF_0);

	if (band == NL80211_BAND_5GHZ)
		return !(conf0 & MT_EE_NIC_CONF_0_PA_INT_5G);
	else
		return !(conf0 & MT_EE_NIC_CONF_0_PA_INT_2G);
}

int mt76x2_eeprom_init(struct mt76x2_dev *dev)
{
	int ret;

	ret = mt76x2_eeprom_load(dev);
	if (ret)
		return ret;

	mt76x2_eeprom_parse_hw_cap(dev);
	mt76x2_eeprom_get_macaddr(dev);
	mt76_eeprom_override(&dev->mt76);
	dev->mt76.macaddr[0] &= ~BIT(1);

	return 0;
}
