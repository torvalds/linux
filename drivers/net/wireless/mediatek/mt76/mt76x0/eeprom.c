/*
 * Copyright (C) 2014 Felix Fietkau <nbd@openwrt.org>
 * Copyright (C) 2015 Jakub Kicinski <kubakici@wp.pl>
 * Copyright (C) 2018 Stanislaw Gruszka <stf_xl@wp.pl>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/etherdevice.h>
#include <asm/unaligned.h>
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

	ret = mt76x02_get_efuse_data(&dev->mt76, MT_EE_USAGE_MAP_START,
				     data, sizeof(data), MT_EE_PHYSICAL_READ);
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
	u16 nic_conf0 = mt76x02_eeprom_get(&dev->mt76, MT_EE_NIC_CONF_0);
	u16 nic_conf1 = mt76x02_eeprom_get(&dev->mt76, MT_EE_NIC_CONF_1);

	mt76x02_eeprom_parse_hw_cap(&dev->mt76);
	dev_dbg(dev->mt76.dev, "2GHz %d 5GHz %d\n",
		dev->mt76.cap.has_2ghz, dev->mt76.cap.has_5ghz);

	if (dev->no_2ghz) {
		dev->mt76.cap.has_2ghz = false;
		dev_dbg(dev->mt76.dev, "mask out 2GHz support\n");
	}

	if (!mt76x02_field_valid(nic_conf1 & 0xff))
		nic_conf1 &= 0xff00;

	if (nic_conf1 & MT_EE_NIC_CONF_1_HW_RF_CTRL)
		dev_err(dev->mt76.dev,
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

	val = mt76x02_eeprom_get(&dev->mt76, MT_EE_2G_TARGET_POWER) >> 8;
	if (mt76x02_field_valid(val))
		dev->cal.rx.temp_offset = mt76x02_sign_extend(val, 8);
	else
		dev->cal.rx.temp_offset = -10;
}

static void mt76x0_set_freq_offset(struct mt76x02_dev *dev)
{
	struct mt76x02_rx_freq_cal *caldata = &dev->cal.rx;
	u8 val;

	val = mt76x02_eeprom_get(&dev->mt76, MT_EE_FREQ_OFFSET);
	if (!mt76x02_field_valid(val))
		val = 0;
	caldata->freq_offset = val;

	val = mt76x02_eeprom_get(&dev->mt76, MT_EE_TSSI_BOUND4) >> 8;
	if (!mt76x02_field_valid(val))
		val = 0;

	caldata->freq_offset -= mt76x02_sign_extend(val, 8);
}

void mt76x0_read_rx_gain(struct mt76x02_dev *dev)
{
	struct ieee80211_channel *chan = dev->mt76.chandef.chan;
	struct mt76x02_rx_freq_cal *caldata = &dev->cal.rx;
	s8 val, lna_5g[3], lna_2g;
	u16 rssi_offset;
	int i;

	mt76x02_get_rx_gain(&dev->mt76, chan->band, &rssi_offset,
			    &lna_2g, lna_5g);
	caldata->lna_gain = mt76x02_get_lna_gain(&dev->mt76, &lna_2g,
						 lna_5g, chan);

	for (i = 0; i < ARRAY_SIZE(caldata->rssi_offset); i++) {
		val = rssi_offset >> (8 * i);
		if (val < -10 || val > 10)
			val = 0;

		caldata->rssi_offset[i] = val;
	}
}

static s8 mt76x0_get_delta(struct mt76_dev *dev)
{
	struct cfg80211_chan_def *chandef = &dev->chandef;
	u8 val;

	if (mt76x02_tssi_enabled(dev))
		return 0;

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

void mt76x0_get_tx_power_per_rate(struct mt76x02_dev *dev)
{
	struct ieee80211_channel *chan = dev->mt76.chandef.chan;
	bool is_2ghz = chan->band == NL80211_BAND_2GHZ;
	struct mt76_rate_power *t = &dev->mt76.rate_power;
	s8 delta = mt76x0_get_delta(&dev->mt76);
	u16 val, addr;

	memset(t, 0, sizeof(*t));

	/* cck 1M, 2M, 5.5M, 11M */
	val = mt76x02_eeprom_get(&dev->mt76, MT_EE_TX_POWER_BYRATE_BASE);
	t->cck[0] = t->cck[1] = s6_to_s8(val);
	t->cck[2] = t->cck[3] = s6_to_s8(val >> 8);

	/* ofdm 6M, 9M, 12M, 18M */
	addr = is_2ghz ? MT_EE_TX_POWER_BYRATE_BASE + 2 : 0x120;
	val = mt76x02_eeprom_get(&dev->mt76, addr);
	t->ofdm[0] = t->ofdm[1] = s6_to_s8(val);
	t->ofdm[2] = t->ofdm[3] = s6_to_s8(val >> 8);

	/* ofdm 24M, 36M, 48M, 54M */
	addr = is_2ghz ? MT_EE_TX_POWER_BYRATE_BASE + 4 : 0x122;
	val = mt76x02_eeprom_get(&dev->mt76, addr);
	t->ofdm[4] = t->ofdm[5] = s6_to_s8(val);
	t->ofdm[6] = t->ofdm[7] = s6_to_s8(val >> 8);

	/* ht-vht mcs 1ss 0, 1, 2, 3 */
	addr = is_2ghz ? MT_EE_TX_POWER_BYRATE_BASE + 6 : 0x124;
	val = mt76x02_eeprom_get(&dev->mt76, addr);
	t->ht[0] = t->ht[1] = t->vht[0] = t->vht[1] = s6_to_s8(val);
	t->ht[2] = t->ht[3] = t->vht[2] = t->vht[3] = s6_to_s8(val >> 8);

	/* ht-vht mcs 1ss 4, 5, 6 */
	addr = is_2ghz ? MT_EE_TX_POWER_BYRATE_BASE + 8 : 0x126;
	val = mt76x02_eeprom_get(&dev->mt76, addr);
	t->ht[4] = t->ht[5] = t->vht[4] = t->vht[5] = s6_to_s8(val);
	t->ht[6] = t->vht[6] = s6_to_s8(val >> 8);

	/* ht-vht mcs 1ss 0, 1, 2, 3 stbc */
	addr = is_2ghz ? MT_EE_TX_POWER_BYRATE_BASE + 14 : 0xec;
	val = mt76x02_eeprom_get(&dev->mt76, addr);
	t->stbc[0] = t->stbc[1] = s6_to_s8(val);
	t->stbc[2] = t->stbc[3] = s6_to_s8(val >> 8);

	/* ht-vht mcs 1ss 4, 5, 6 stbc */
	addr = is_2ghz ? MT_EE_TX_POWER_BYRATE_BASE + 16 : 0xee;
	val = mt76x02_eeprom_get(&dev->mt76, addr);
	t->stbc[4] = t->stbc[5] = s6_to_s8(val);
	t->stbc[6] = t->stbc[7] = s6_to_s8(val >> 8);

	/* vht mcs 8, 9 5GHz */
	val = mt76x02_eeprom_get(&dev->mt76, 0x132);
	t->vht[7] = s6_to_s8(val);
	t->vht[8] = s6_to_s8(val >> 8);

	mt76x02_add_rate_power_offset(t, delta);
}

void mt76x0_get_power_info(struct mt76x02_dev *dev, u8 *info)
{
	struct mt76x0_chan_map {
		u8 chan;
		u8 offset;
	} chan_map[] = {
		{   2,  0 }, {   4,  1 }, {   6,  2 }, {   8,  3 },
		{  10,  4 }, {  12,  5 }, {  14,  6 }, {  38,  0 },
		{  44,  1 }, {  48,  2 }, {  54,  3 }, {  60,  4 },
		{  64,  5 }, { 102,  6 }, { 108,  7 }, { 112,  8 },
		{ 118,  9 }, { 124, 10 }, { 128, 11 }, { 134, 12 },
		{ 140, 13 }, { 151, 14 }, { 157, 15 }, { 161, 16 },
		{ 167, 17 }, { 171, 18 }, { 173, 19 },
	};
	struct ieee80211_channel *chan = dev->mt76.chandef.chan;
	u8 offset, addr;
	u16 data;
	int i;

	for (i = 0; i < ARRAY_SIZE(chan_map); i++) {
		if (chan_map[i].chan <= chan->hw_value) {
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
		case 58:
			offset = 8;
			break;
		case 106:
			offset = 14;
			break;
		case 112:
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

	data = mt76x02_eeprom_get(&dev->mt76, addr);

	info[0] = data;
	if (!info[0] || info[0] > 0x3f)
		info[0] = 5;

	info[1] = data >> 8;
	if (!info[1] || info[1] > 0x3f)
		info[1] = 5;
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

	return mt76x02_get_efuse_data(&dev->mt76, 0, dev->mt76.eeprom.data,
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

	data = mt76x02_eeprom_get(&dev->mt76, MT_EE_VERSION);
	version = data >> 8;
	fae = data;

	if (version > MT76X0U_EE_MAX_VER)
		dev_warn(dev->mt76.dev,
			 "Warning: unsupported EEPROM version %02hhx\n",
			 version);
	dev_info(dev->mt76.dev, "EEPROM ver:%02hhx fae:%02hhx\n",
		 version, fae);

	mt76x02_mac_setaddr(&dev->mt76,
			    dev->mt76.eeprom.data + MT_EE_MAC_ADDR);
	mt76x0_set_chip_cap(dev);
	mt76x0_set_freq_offset(dev);
	mt76x0_set_temp_offset(dev);

	dev->mt76.chainmask = 0x0101;

	return 0;
}

MODULE_LICENSE("Dual BSD/GPL");
