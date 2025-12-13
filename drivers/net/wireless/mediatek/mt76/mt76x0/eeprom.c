// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2014 Felix Fietkau <nbd@openwrt.org>
 * Copyright (C) 2015 Jakub Kicinski <kubakici@wp.pl>
 * Copyright (C) 2018 Stanislaw Gruszka <stf_xl@wp.pl>
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/etherdevice.h>
#include <linux/unaligned.h>
#include "mt76x0.h"
#include "eeprom.h"
#include "../mt76x02_phy.h"

#define MT_MAP_READS	DIV_ROUND_UP(MT_EFUSE_USAGE_MAP_SIZE, 16)
static int
mt76x0_efuse_physical_size_check(struct mt76x02_dev *dev)
{
	u8 data[MT_MAP_READS * 16];
	int ret, i;
	u32 start = 0, end = 0, cnt_free;

	ret = mt76x02_get_efuse_data(dev, MT_EE_USAGE_MAP_START, data,
				     sizeof(data), MT_EE_PHYSICAL_READ);
	if (ret)
		return ret;

	for (i = 0; i < MT_EFUSE_USAGE_MAP_SIZE; i++)
		if (!data[i]) {
			if (!start)
				start = MT_EE_USAGE_MAP_START + i;
			end = MT_EE_USAGE_MAP_START + i;
		}
	cnt_free = end - start + 1;

	if (MT_EFUSE_USAGE_MAP_SIZE - cnt_free < 5) {
		dev_err(dev->mt76.dev,
			"driver does not support default EEPROM\n");
		return -EINVAL;
	}

	return 0;
}

static void mt76x0_set_chip_cap(struct mt76x02_dev *dev)
{
	u16 nic_conf0 = mt76x02_eeprom_get(dev, MT_EE_NIC_CONF_0);
	u16 nic_conf1 = mt76x02_eeprom_get(dev, MT_EE_NIC_CONF_1);

	mt76x02_eeprom_parse_hw_cap(dev);
	dev_dbg(dev->mt76.dev, "2GHz %d 5GHz %d\n",
		dev->mphy.cap.has_2ghz, dev->mphy.cap.has_5ghz);

	if (dev->no_2ghz) {
		dev->mphy.cap.has_2ghz = false;
		dev_dbg(dev->mt76.dev, "mask out 2GHz support\n");
	}

	if (is_mt7630(dev)) {
		dev->mphy.cap.has_5ghz = false;
		dev_dbg(dev->mt76.dev, "mask out 5GHz support\n");
	}

	if (!mt76x02_field_valid(nic_conf1 & 0xff))
		nic_conf1 &= 0xff00;

	if (nic_conf1 & MT_EE_NIC_CONF_1_HW_RF_CTRL)
		dev_dbg(dev->mt76.dev,
			"driver does not support HW RF ctrl\n");

	if (!mt76x02_field_valid(nic_conf0 >> 8))
		return;

	if (FIELD_GET(MT_EE_NIC_CONF_0_RX_PATH, nic_conf0) > 1 ||
	    FIELD_GET(MT_EE_NIC_CONF_0_TX_PATH, nic_conf0) > 1)
		dev_err(dev->mt76.dev, "invalid tx-rx stream\n");
}

static void mt76x0_set_temp_offset(struct mt76x02_dev *dev)
{
	u8 val;

	val = mt76x02_eeprom_get(dev, MT_EE_2G_TARGET_POWER) >> 8;
	if (mt76x02_field_valid(val))
		dev->cal.rx.temp_offset = mt76x02_sign_extend(val, 8);
	else
		dev->cal.rx.temp_offset = -10;
}

static void mt76x0_set_freq_offset(struct mt76x02_dev *dev)
{
	struct mt76x02_rx_freq_cal *caldata = &dev->cal.rx;
	u8 val;

	val = mt76x02_eeprom_get(dev, MT_EE_FREQ_OFFSET);
	if (!mt76x02_field_valid(val))
		val = 0;
	caldata->freq_offset = val;

	val = mt76x02_eeprom_get(dev, MT_EE_TSSI_BOUND4) >> 8;
	if (!mt76x02_field_valid(val))
		val = 0;

	caldata->freq_offset -= mt76x02_sign_extend(val, 8);
}

void mt76x0_read_rx_gain(struct mt76x02_dev *dev)
{
	struct ieee80211_channel *chan = dev->mphy.chandef.chan;
	struct mt76x02_rx_freq_cal *caldata = &dev->cal.rx;
	s8 val, lna_5g[3], lna_2g;
	u16 rssi_offset;
	int i;

	mt76x02_get_rx_gain(dev, chan->band, &rssi_offset, &lna_2g, lna_5g);
	caldata->lna_gain = mt76x02_get_lna_gain(dev, &lna_2g, lna_5g, chan);

	for (i = 0; i < ARRAY_SIZE(caldata->rssi_offset); i++) {
		val = rssi_offset >> (8 * i);
		if (val < -10 || val > 10)
			val = 0;

		caldata->rssi_offset[i] = val;
	}
}

static s8 mt76x0_get_delta(struct mt76x02_dev *dev)
{
	struct cfg80211_chan_def *chandef = &dev->mphy.chandef;
	u8 val;

	if (chandef->width == NL80211_CHAN_WIDTH_80) {
		val = mt76x02_eeprom_get(dev, MT_EE_5G_TARGET_POWER) >> 8;
	} else if (chandef->width == NL80211_CHAN_WIDTH_40) {
		u16 data;

		data = mt76x02_eeprom_get(dev, MT_EE_TX_POWER_DELTA_BW40);
		if (chandef->chan->band == NL80211_BAND_5GHZ)
			val = data >> 8;
		else
			val = data;
	} else {
		return 0;
	}

	return mt76x02_rate_power_val(val);
}

void mt76x0_get_tx_power_per_rate(struct mt76x02_dev *dev,
				  struct ieee80211_channel *chan,
				  struct mt76x02_rate_power *t)
{
	bool is_2ghz = chan->band == NL80211_BAND_2GHZ;
	u16 val, addr;
	s8 delta;

	memset(t, 0, sizeof(*t));

	/* cck 1M, 2M, 5.5M, 11M */
	val = mt76x02_eeprom_get(dev, MT_EE_TX_POWER_BYRATE_BASE);
	t->cck[0] = t->cck[1] = s6_to_s8(val);
	t->cck[2] = t->cck[3] = s6_to_s8(val >> 8);

	/* ofdm 6M, 9M, 12M, 18M */
	addr = is_2ghz ? MT_EE_TX_POWER_BYRATE_BASE + 2 : 0x120;
	val = mt76x02_eeprom_get(dev, addr);
	t->ofdm[0] = t->ofdm[1] = s6_to_s8(val);
	t->ofdm[2] = t->ofdm[3] = s6_to_s8(val >> 8);

	/* ofdm 24M, 36M, 48M, 54M */
	addr = is_2ghz ? MT_EE_TX_POWER_BYRATE_BASE + 4 : 0x122;
	val = mt76x02_eeprom_get(dev, addr);
	t->ofdm[4] = t->ofdm[5] = s6_to_s8(val);
	t->ofdm[6] = t->ofdm[7] = s6_to_s8(val >> 8);

	/* ht-vht mcs 1ss 0, 1, 2, 3 */
	addr = is_2ghz ? MT_EE_TX_POWER_BYRATE_BASE + 6 : 0x124;
	val = mt76x02_eeprom_get(dev, addr);
	t->ht[0] = t->ht[1] = s6_to_s8(val);
	t->ht[2] = t->ht[3] = s6_to_s8(val >> 8);

	/* ht-vht mcs 1ss 4, 5, 6 */
	addr = is_2ghz ? MT_EE_TX_POWER_BYRATE_BASE + 8 : 0x126;
	val = mt76x02_eeprom_get(dev, addr);
	t->ht[4] = t->ht[5] = s6_to_s8(val);
	t->ht[6] = t->ht[7] = s6_to_s8(val >> 8);

	/* vht mcs 8, 9 5GHz */
	val = mt76x02_eeprom_get(dev, 0x12c);
	t->vht[0] = s6_to_s8(val);
	t->vht[1] = s6_to_s8(val >> 8);

	delta = mt76x0_tssi_enabled(dev) ? 0 : mt76x0_get_delta(dev);
	mt76x02_add_rate_power_offset(t, delta);
}

void mt76x0_get_power_info(struct mt76x02_dev *dev,
			   struct ieee80211_channel *chan, s8 *tp)
{
	static const struct mt76x0_chan_map {
		u8 chan;
		u8 offset;
	} chan_map[] = {
		{   2,  0 }, {   4,  2 }, {   6,  4 }, {   8,  6 },
		{  10,  8 }, {  12, 10 }, {  14, 12 }, {  38,  0 },
		{  44,  2 }, {  48,  4 }, {  54,  6 }, {  60,  8 },
		{  64, 10 }, { 102, 12 }, { 108, 14 }, { 112, 16 },
		{ 118, 18 }, { 124, 20 }, { 128, 22 }, { 134, 24 },
		{ 140, 26 }, { 151, 28 }, { 157, 30 }, { 161, 32 },
		{ 167, 34 }, { 171, 36 }, { 175, 38 },
	};
	u8 offset, addr;
	int i, idx = 0;
	u16 data;

	if (mt76x0_tssi_enabled(dev)) {
		s8 target_power;

		if (chan->band == NL80211_BAND_5GHZ)
			data = mt76x02_eeprom_get(dev, MT_EE_5G_TARGET_POWER);
		else
			data = mt76x02_eeprom_get(dev, MT_EE_2G_TARGET_POWER);
		target_power = (data & 0xff) - dev->rate_power.ofdm[7];
		*tp = target_power + mt76x0_get_delta(dev);

		return;
	}

	for (i = 0; i < ARRAY_SIZE(chan_map); i++) {
		if (chan->hw_value <= chan_map[i].chan) {
			idx = (chan->hw_value == chan_map[i].chan);
			offset = chan_map[i].offset;
			break;
		}
	}
	if (i == ARRAY_SIZE(chan_map))
		offset = chan_map[0].offset;

	if (chan->band == NL80211_BAND_2GHZ) {
		addr = MT_EE_TX_POWER_DELTA_BW80 + offset;
	} else {
		switch (chan->hw_value) {
		case 42:
			offset = 2;
			break;
		case 58:
			offset = 8;
			break;
		case 106:
			offset = 14;
			break;
		case 122:
			offset = 20;
			break;
		case 155:
			offset = 30;
			break;
		default:
			break;
		}
		addr = MT_EE_TX_POWER_0_GRP4_TSSI_SLOPE + 2 + offset;
	}

	data = mt76x02_eeprom_get(dev, addr);
	*tp = data >> (8 * idx);
	if (*tp < 0 || *tp > 0x3f)
		*tp = 5;
}

static int mt76x0_check_eeprom(struct mt76x02_dev *dev)
{
	u16 val;

	val = get_unaligned_le16(dev->mt76.eeprom.data);
	if (!val)
		val = get_unaligned_le16(dev->mt76.eeprom.data +
					 MT_EE_PCI_ID);

	switch (val) {
	case 0x7650:
	case 0x7610:
		return 0;
	default:
		dev_err(dev->mt76.dev, "EEPROM data check failed: %04x\n",
			val);
		return -EINVAL;
	}
}

static int mt76x0_load_eeprom(struct mt76x02_dev *dev)
{
	int found;

	found = mt76_eeprom_init(&dev->mt76, MT76X0_EEPROM_SIZE);
	if (found < 0)
		return found;

	if (found && !mt76x0_check_eeprom(dev))
		return 0;

	found = mt76x0_efuse_physical_size_check(dev);
	if (found < 0)
		return found;

	return mt76x02_get_efuse_data(dev, 0, dev->mt76.eeprom.data,
				      MT76X0_EEPROM_SIZE, MT_EE_READ);
}

int mt76x0_eeprom_init(struct mt76x02_dev *dev)
{
	u8 version, fae;
	u16 data;
	int err;

	err = mt76x0_load_eeprom(dev);
	if (err < 0)
		return err;

	data = mt76x02_eeprom_get(dev, MT_EE_VERSION);
	version = data >> 8;
	fae = data;

	if (version > MT76X0U_EE_MAX_VER)
		dev_warn(dev->mt76.dev,
			 "Warning: unsupported EEPROM version %02hhx\n",
			 version);
	dev_info(dev->mt76.dev, "EEPROM ver:%02hhx fae:%02hhx\n",
		 version, fae);

	memcpy(dev->mphy.macaddr, (u8 *)dev->mt76.eeprom.data + MT_EE_MAC_ADDR,
	       ETH_ALEN);

	err = mt76_eeprom_override(&dev->mphy);
	if (err)
		return err;

	mt76x02_mac_setaddr(dev, dev->mphy.macaddr);

	mt76x0_set_chip_cap(dev);
	mt76x0_set_freq_offset(dev);
	mt76x0_set_temp_offset(dev);

	return 0;
}

MODULE_DESCRIPTION("MediaTek MT76x EEPROM helpers");
MODULE_LICENSE("Dual BSD/GPL");
