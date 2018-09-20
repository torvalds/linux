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

static bool
field_valid(u8 val)
{
	return val != 0xff;
}

static s8
field_validate(u8 val)
{
	if (!field_valid(val))
		return 0;

	return val;
}

static inline int
sign_extend(u32 val, unsigned int size)
{
	bool sign = val & BIT(size - 1);

	val &= BIT(size - 1) - 1;

	return sign ? val : -val;
}

static int
mt76x0_efuse_read(struct mt76x0_dev *dev, u16 addr, u8 *data,
		   enum mt76x0_eeprom_access_modes mode)
{
	u32 val;
	int i;

	val = mt76_rr(dev, MT_EFUSE_CTRL);
	val &= ~(MT_EFUSE_CTRL_AIN |
		 MT_EFUSE_CTRL_MODE);
	val |= FIELD_PREP(MT_EFUSE_CTRL_AIN, addr & ~0xf) |
	       FIELD_PREP(MT_EFUSE_CTRL_MODE, mode) |
	       MT_EFUSE_CTRL_KICK;
	mt76_wr(dev, MT_EFUSE_CTRL, val);

	if (!mt76_poll(dev, MT_EFUSE_CTRL, MT_EFUSE_CTRL_KICK, 0, 1000))
		return -ETIMEDOUT;

	val = mt76_rr(dev, MT_EFUSE_CTRL);
	if ((val & MT_EFUSE_CTRL_AOUT) == MT_EFUSE_CTRL_AOUT) {
		/* Parts of eeprom not in the usage map (0x80-0xc0,0xf0)
		 * will not return valid data but it's ok.
		 */
		memset(data, 0xff, 16);
		return 0;
	}

	for (i = 0; i < 4; i++) {
		val = mt76_rr(dev, MT_EFUSE_DATA(i));
		put_unaligned_le32(val, data + 4 * i);
	}

	return 0;
}

#define MT_MAP_READS	DIV_ROUND_UP(MT_EFUSE_USAGE_MAP_SIZE, 16)
static int
mt76x0_efuse_physical_size_check(struct mt76x0_dev *dev)
{
	u8 data[MT_MAP_READS * 16];
	int ret, i;
	u32 start = 0, end = 0, cnt_free;

	for (i = 0; i < MT_MAP_READS; i++) {
		ret = mt76x0_efuse_read(dev, MT_EE_USAGE_MAP_START + i * 16,
					 data + i * 16, MT_EE_PHYSICAL_READ);
		if (ret)
			return ret;
	}

	for (i = 0; i < MT_EFUSE_USAGE_MAP_SIZE; i++)
		if (!data[i]) {
			if (!start)
				start = MT_EE_USAGE_MAP_START + i;
			end = MT_EE_USAGE_MAP_START + i;
		}
	cnt_free = end - start + 1;

	if (MT_EFUSE_USAGE_MAP_SIZE - cnt_free < 5) {
		dev_err(dev->mt76.dev, "Error: your device needs default EEPROM file and this driver doesn't support it!\n");
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

	if (!field_valid(nic_conf1 & 0xff))
		nic_conf1 &= 0xff00;

	if (nic_conf1 & MT_EE_NIC_CONF_1_HW_RF_CTRL)
		dev_err(dev->mt76.dev,
			"Error: this driver does not support HW RF ctrl\n");

	if (!field_valid(nic_conf0 >> 8))
		return;

	if (FIELD_GET(MT_EE_NIC_CONF_0_RX_PATH, nic_conf0) > 1 ||
	    FIELD_GET(MT_EE_NIC_CONF_0_TX_PATH, nic_conf0) > 1)
		dev_err(dev->mt76.dev,
			"Error: device has more than 1 RX/TX stream!\n");

	dev->ee->pa_type = FIELD_GET(MT_EE_NIC_CONF_0_PA_TYPE, nic_conf0);
	dev_dbg(dev->mt76.dev, "PA Type %d\n", dev->ee->pa_type);
}

static int
mt76x0_set_macaddr(struct mt76x0_dev *dev, const u8 *eeprom)
{
	const void *src = eeprom + MT_EE_MAC_ADDR;
	u8 *dst = dev->mt76.macaddr;

	ether_addr_copy(dev->mt76.macaddr, src);

	if (!is_valid_ether_addr(dst)) {
		eth_random_addr(dst);
		dev_info(dev->mt76.dev,
			 "Invalid MAC address, using random address %pM\n",
			 dst);
	}

	mt76_wr(dev, MT_MAC_ADDR_DW0, get_unaligned_le32(dst));
	mt76_wr(dev, MT_MAC_ADDR_DW1, get_unaligned_le16(dst + 4) |
		FIELD_PREP(MT_MAC_ADDR_DW1_U2ME_MASK, 0xff));

	return 0;
}

static void
mt76x0_set_temp_offset(struct mt76x0_dev *dev, u8 *eeprom)
{
	u8 temp = eeprom[MT_EE_TEMP_OFFSET];

	if (field_valid(temp))
		dev->ee->temp_off = sign_extend(temp, 8);
	else
		dev->ee->temp_off = -10;
}

static void
mt76x0_set_country_reg(struct mt76x0_dev *dev, u8 *eeprom)
{
	/* Note: - region 31 is not valid for mt76x0 (see rtmp_init.c)
	 *	 - comments in rtmp_def.h are incorrect (see rt_channel.c)
	 */
	static const struct reg_channel_bounds chan_bounds[] = {
		/* EEPROM country regions 0 - 7 */
		{  1, 11 },	{  1, 13 },	{ 10,  2 },	{ 10,  4 },
		{ 14,  1 },	{  1, 14 },	{  3,  7 },	{  5,  9 },
		/* EEPROM country regions 32 - 33 */
		{  1, 11 },	{  1, 14 }
	};
	u8 val = eeprom[MT_EE_COUNTRY_REGION_2GHZ];
	int idx = -1;

	dev_dbg(dev->mt76.dev, "REG 2GHZ %u REG 5GHZ %u\n", val, eeprom[MT_EE_COUNTRY_REGION_5GHZ]);
	if (val < 8)
		idx = val;
	if (val > 31 && val < 33)
		idx = val - 32 + 8;

	if (idx != -1)
		dev_info(dev->mt76.dev,
			 "EEPROM country region %02hhx (channels %hhd-%hhd)\n",
			 val, chan_bounds[idx].start,
			 chan_bounds[idx].start + chan_bounds[idx].num - 1);
	else
		idx = 5; /* channels 1 - 14 */

	dev->ee->reg = chan_bounds[idx];

	/* TODO: country region 33 is special - phy should be set to B-mode
	 *	 before entering channel 14 (see sta/connect.c)
	 */
}

static void
mt76x0_set_rf_freq_off(struct mt76x0_dev *dev, u8 *eeprom)
{
	u8 comp;

	dev->ee->rf_freq_off = field_validate(eeprom[MT_EE_FREQ_OFFSET]);
	comp = field_validate(eeprom[MT_EE_FREQ_OFFSET_COMPENSATION]);

	if (comp & BIT(7))
		dev->ee->rf_freq_off -= comp & 0x7f;
	else
		dev->ee->rf_freq_off += comp;
}

static void
mt76x0_set_lna_gain(struct mt76x0_dev *dev, u8 *eeprom)
{
	u8 gain;

	dev->ee->lna_gain_2ghz = eeprom[MT_EE_LNA_GAIN];
	dev->ee->lna_gain_5ghz[0] = eeprom[MT_EE_LNA_GAIN + 1];

	gain = eeprom[MT_EE_LNA_GAIN_5GHZ_1];
	if (gain == 0xff || gain == 0)
		dev->ee->lna_gain_5ghz[1] = dev->ee->lna_gain_5ghz[0];
	else
		dev->ee->lna_gain_5ghz[1] = gain;

	gain = eeprom[MT_EE_LNA_GAIN_5GHZ_2];
	if (gain == 0xff || gain == 0)
		dev->ee->lna_gain_5ghz[2] = dev->ee->lna_gain_5ghz[0];
	else
		dev->ee->lna_gain_5ghz[2] = gain;
}

static void
mt76x0_set_rssi_offset(struct mt76x0_dev *dev, u8 *eeprom)
{
	int i;
	s8 *rssi_offset = dev->ee->rssi_offset_2ghz;

	for (i = 0; i < 2; i++) {
		rssi_offset[i] = eeprom[MT_EE_RSSI_OFFSET_2G_0 + i];

		if (rssi_offset[i] < -10 || rssi_offset[i] > 10) {
			dev_warn(dev->mt76.dev,
				 "Warning: EEPROM RSSI is invalid %02hhx\n",
				 rssi_offset[i]);
			rssi_offset[i] = 0;
		}
	}

	rssi_offset = dev->ee->rssi_offset_5ghz;

	for (i = 0; i < 3; i++) {
		rssi_offset[i] = eeprom[MT_EE_RSSI_OFFSET_5G_0 + i];

		if (rssi_offset[i] < -10 || rssi_offset[i] > 10) {
			dev_warn(dev->mt76.dev,
				 "Warning: EEPROM RSSI is invalid %02hhx\n",
				 rssi_offset[i]);
			rssi_offset[i] = 0;
		}
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

	if (!field_valid(val) || !(val & BIT(7)))
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
	int i, ret;

	ret = mt76x0_efuse_physical_size_check(dev);
	if (ret)
		return ret;

	dev->ee = devm_kzalloc(dev->mt76.dev, sizeof(*dev->ee), GFP_KERNEL);
	if (!dev->ee)
		return -ENOMEM;

	eeprom = kmalloc(MT76X0_EEPROM_SIZE, GFP_KERNEL);
	if (!eeprom)
		return -ENOMEM;

	for (i = 0; i + 16 <= MT76X0_EEPROM_SIZE; i += 16) {
		ret = mt76x0_efuse_read(dev, i, eeprom + i, MT_EE_READ);
		if (ret)
			goto out;
	}

	if (eeprom[MT_EE_VERSION + 1] > MT76X0U_EE_MAX_VER)
		dev_warn(dev->mt76.dev,
			 "Warning: unsupported EEPROM version %02hhx\n",
			 eeprom[MT_EE_VERSION + 1]);
	dev_info(dev->mt76.dev, "EEPROM ver:%02hhx fae:%02hhx\n",
		 eeprom[MT_EE_VERSION + 1], eeprom[MT_EE_VERSION]);

	mt76x0_set_macaddr(dev, eeprom);
	mt76x0_set_chip_cap(dev, eeprom);
	mt76x0_set_country_reg(dev, eeprom);
	mt76x0_set_rf_freq_off(dev, eeprom);
	mt76x0_set_temp_offset(dev, eeprom);
	mt76x0_set_lna_gain(dev, eeprom);
	mt76x0_set_rssi_offset(dev, eeprom);
	dev->chainmask = 0x0101;

	mt76x0_set_tx_power_per_rate(dev, eeprom);
	mt76x0_set_tx_power_per_chan(dev, eeprom);

out:
	kfree(eeprom);
	return ret;
}

MODULE_LICENSE("Dual BSD/GPL");
