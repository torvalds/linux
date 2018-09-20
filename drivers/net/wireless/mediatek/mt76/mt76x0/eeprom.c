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

#include <linux/of.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/etherdevice.h>
#include <asm/unaligned.h>
#include "mt76x0.h"
#include "eeprom.h"

#define MT_MAP_READS	DIV_ROUND_UP(MT_EFUSE_USAGE_MAP_SIZE, 16)
static int
mt76x0_efuse_physical_size_check(struct mt76x0_dev *dev)
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

static void
mt76x0_set_chip_cap(struct mt76x0_dev *dev, u8 *eeprom)
{
	enum mt76x2_board_type { BOARD_TYPE_2GHZ = 1, BOARD_TYPE_5GHZ = 2 };
	u16 nic_conf0 = get_unaligned_le16(eeprom + MT_EE_NIC_CONF_0);
	u16 nic_conf1 = get_unaligned_le16(eeprom + MT_EE_NIC_CONF_1);

	dev_dbg(dev->mt76.dev, "NIC_CONF0: %04x NIC_CONF1: %04x\n", nic_conf0, nic_conf1);

	switch (FIELD_GET(MT_EE_NIC_CONF_0_BOARD_TYPE, nic_conf0)) {
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

	dev_dbg(dev->mt76.dev, "Has 2GHZ %d 5GHZ %d\n",
		dev->mt76.cap.has_2ghz, dev->mt76.cap.has_5ghz);

	if (!mt76x02_field_valid(nic_conf1 & 0xff))
		nic_conf1 &= 0xff00;

	if (nic_conf1 & MT_EE_NIC_CONF_1_HW_RF_CTRL)
		dev_err(dev->mt76.dev,
			"Error: this driver does not support HW RF ctrl\n");

	if (!mt76x02_field_valid(nic_conf0 >> 8))
		return;

	if (FIELD_GET(MT_EE_NIC_CONF_0_RX_PATH, nic_conf0) > 1 ||
	    FIELD_GET(MT_EE_NIC_CONF_0_TX_PATH, nic_conf0) > 1)
		dev_err(dev->mt76.dev,
			"Error: device has more than 1 RX/TX stream!\n");
}

static void
mt76x0_set_temp_offset(struct mt76x0_dev *dev, u8 *eeprom)
{
	u8 temp = eeprom[MT_EE_TEMP_OFFSET];

	if (mt76x02_field_valid(temp))
		dev->ee->temp_off = mt76x02_sign_extend(temp, 8);
	else
		dev->ee->temp_off = -10;
}

static void
mt76x0_set_rf_freq_off(struct mt76x0_dev *dev, u8 *eeprom)
{
	u8 comp;

	comp = eeprom[MT_EE_FREQ_OFFSET_COMPENSATION];
	if (!mt76x02_field_valid(comp))
		comp = 0;

	dev->ee->rf_freq_off = eeprom[MT_EE_FREQ_OFFSET];
	if (!mt76x02_field_valid(dev->ee->rf_freq_off))
		dev->ee->rf_freq_off = 0;

	if (comp & BIT(7))
		dev->ee->rf_freq_off -= comp & 0x7f;
	else
		dev->ee->rf_freq_off += comp;
}

void mt76x0_read_rx_gain(struct mt76x0_dev *dev)
{
	struct ieee80211_channel *chan = dev->mt76.chandef.chan;
	struct mt76x0_caldata *caldata = &dev->caldata;
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

static u32
calc_bw40_power_rate(u32 value, int delta)
{
	u32 ret = 0;
	int i, tmp;

	for (i = 0; i < 4; i++) {
		tmp = s6_to_int((value >> i*8) & 0xff) + delta;
		ret |= (u32)(int_to_s6(tmp)) << i*8;
	}

	return ret;
}

static s8
get_delta(u8 val)
{
	s8 ret;

	if (!mt76x02_field_valid(val) || !(val & BIT(7)))
		return 0;

	ret = val & 0x1f;
	if (ret > 8)
		ret = 8;
	if (val & BIT(6))
		ret = -ret;

	return ret;
}

static void
mt76x0_set_tx_power_per_rate(struct mt76x0_dev *dev, u8 *eeprom)
{
	s8 bw40_delta_2g, bw40_delta_5g;
	u32 val;
	int i;

	bw40_delta_2g = get_delta(eeprom[MT_EE_TX_POWER_DELTA_BW40]);
	bw40_delta_5g = get_delta(eeprom[MT_EE_TX_POWER_DELTA_BW40 + 1]);

	for (i = 0; i < 5; i++) {
		val = get_unaligned_le32(eeprom + MT_EE_TX_POWER_BYRATE(i));

		/* Skip last 16 bits. */
		if (i == 4)
			val &= 0x0000ffff;

		dev->ee->tx_pwr_cfg_2g[i][0] = val;
		dev->ee->tx_pwr_cfg_2g[i][1] = calc_bw40_power_rate(val, bw40_delta_2g);
	}

	/* Reading per rate tx power for 5 GHz band is a bit more complex. Note
	 * we mix 16 bit and 32 bit reads and sometimes do shifts.
	 */
	val = get_unaligned_le16(eeprom + 0x120);
	val <<= 16;
	dev->ee->tx_pwr_cfg_5g[0][0] = val;
	dev->ee->tx_pwr_cfg_5g[0][1] = calc_bw40_power_rate(val, bw40_delta_5g);

	val = get_unaligned_le32(eeprom + 0x122);
	dev->ee->tx_pwr_cfg_5g[1][0] = val;
	dev->ee->tx_pwr_cfg_5g[1][1] = calc_bw40_power_rate(val, bw40_delta_5g);

	val = get_unaligned_le16(eeprom + 0x126);
	dev->ee->tx_pwr_cfg_5g[2][0] = val;
	dev->ee->tx_pwr_cfg_5g[2][1] = calc_bw40_power_rate(val, bw40_delta_5g);

	val = get_unaligned_le16(eeprom + 0xec);
	val <<= 16;
	dev->ee->tx_pwr_cfg_5g[3][0] = val;
	dev->ee->tx_pwr_cfg_5g[3][1] = calc_bw40_power_rate(val, bw40_delta_5g);

	val = get_unaligned_le16(eeprom + 0xee);
	dev->ee->tx_pwr_cfg_5g[4][0] = val;
	dev->ee->tx_pwr_cfg_5g[4][1] = calc_bw40_power_rate(val, bw40_delta_5g);
}

static void
mt76x0_set_tx_power_per_chan(struct mt76x0_dev *dev, u8 *eeprom)
{
	int i;
	u8 tx_pwr;

	for (i = 0; i < 14; i++) {
		tx_pwr = eeprom[MT_EE_TX_POWER_DELTA_BW80 + i];
		if (tx_pwr <= 0x3f && tx_pwr > 0)
			dev->ee->tx_pwr_per_chan[i] = tx_pwr;
		else
			dev->ee->tx_pwr_per_chan[i] = 5;
	}

	for (i = 0; i < 40; i++) {
		tx_pwr = eeprom[MT_EE_TX_POWER_0_GRP4_TSSI_SLOPE + 2 + i];
		if (tx_pwr <= 0x3f && tx_pwr > 0)
			dev->ee->tx_pwr_per_chan[14 + i] = tx_pwr;
		else
			dev->ee->tx_pwr_per_chan[14 + i] = 5;
	}

	dev->ee->tx_pwr_per_chan[54] = dev->ee->tx_pwr_per_chan[22];
	dev->ee->tx_pwr_per_chan[55] = dev->ee->tx_pwr_per_chan[28];
	dev->ee->tx_pwr_per_chan[56] = dev->ee->tx_pwr_per_chan[34];
	dev->ee->tx_pwr_per_chan[57] = dev->ee->tx_pwr_per_chan[44];
}

int
mt76x0_eeprom_init(struct mt76x0_dev *dev)
{
	u8 *eeprom;
	int ret;

	ret = mt76x0_efuse_physical_size_check(dev);
	if (ret)
		return ret;

	ret = mt76_eeprom_init(&dev->mt76, MT76X0_EEPROM_SIZE);
	if (ret < 0)
		return ret;

	dev->ee = devm_kzalloc(dev->mt76.dev, sizeof(*dev->ee), GFP_KERNEL);
	if (!dev->ee)
		return -ENOMEM;

	eeprom = kmalloc(MT76X0_EEPROM_SIZE, GFP_KERNEL);
	if (!eeprom)
		return -ENOMEM;

	ret = mt76x02_get_efuse_data(&dev->mt76, 0, eeprom,
				     MT76X0_EEPROM_SIZE, MT_EE_READ);
	if (ret)
		goto out;

	if (eeprom[MT_EE_VERSION + 1] > MT76X0U_EE_MAX_VER)
		dev_warn(dev->mt76.dev,
			 "Warning: unsupported EEPROM version %02hhx\n",
			 eeprom[MT_EE_VERSION + 1]);
	dev_info(dev->mt76.dev, "EEPROM ver:%02hhx fae:%02hhx\n",
		 eeprom[MT_EE_VERSION + 1], eeprom[MT_EE_VERSION]);

	mt76x02_mac_setaddr(&dev->mt76, eeprom + MT_EE_MAC_ADDR);
	mt76x0_set_chip_cap(dev, eeprom);
	mt76x0_set_rf_freq_off(dev, eeprom);
	mt76x0_set_temp_offset(dev, eeprom);
	dev->chainmask = 0x0101;

	mt76x0_set_tx_power_per_rate(dev, eeprom);
	mt76x0_set_tx_power_per_chan(dev, eeprom);

out:
	kfree(eeprom);
	return ret;
}

MODULE_LICENSE("Dual BSD/GPL");
