/*
 * Copyright (C) 2014 Felix Fietkau <nbd@openwrt.org>
 * Copyright (C) 2015 Jakub Kicinski <kubakici@wp.pl>
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
#include "mt7601u.h"
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

static int
mt7601u_efuse_read(struct mt7601u_dev *dev, u16 addr, u8 *data,
		   enum mt7601u_eeprom_access_modes mode)
{
	u32 val;
	int i;

	val = mt76_rr(dev, MT_EFUSE_CTRL);
	val &= ~(MT_EFUSE_CTRL_AIN |
		 MT_EFUSE_CTRL_MODE);
	val |= MT76_SET(MT_EFUSE_CTRL_AIN, addr & ~0xf) |
	       MT76_SET(MT_EFUSE_CTRL_MODE, mode) |
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

static int
mt7601u_efuse_physical_size_check(struct mt7601u_dev *dev)
{
	const int map_reads = DIV_ROUND_UP(MT_EFUSE_USAGE_MAP_SIZE, 16);
	u8 data[map_reads * 16];
	int ret, i;
	u32 start = 0, end = 0, cnt_free;

	for (i = 0; i < map_reads; i++) {
		ret = mt7601u_efuse_read(dev, MT_EE_USAGE_MAP_START + i * 16,
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
		dev_err(dev->dev, "Error: your device needs default EEPROM file and this driver doesn't support it!\n");
		return -EINVAL;
	}

	return 0;
}

static bool
mt7601u_has_tssi(struct mt7601u_dev *dev, u8 *eeprom)
{
	u16 nic_conf1 = get_unaligned_le16(eeprom + MT_EE_NIC_CONF_1);

	return ~nic_conf1 && (nic_conf1 & MT_EE_NIC_CONF_1_TX_ALC_EN);
}

static void
mt7601u_set_chip_cap(struct mt7601u_dev *dev, u8 *eeprom)
{
	u16 nic_conf0 = get_unaligned_le16(eeprom + MT_EE_NIC_CONF_0);
	u16 nic_conf1 = get_unaligned_le16(eeprom + MT_EE_NIC_CONF_1);

	if (!field_valid(nic_conf1 & 0xff))
		nic_conf1 &= 0xff00;

	dev->ee->tssi_enabled = mt7601u_has_tssi(dev, eeprom) &&
				!(nic_conf1 & MT_EE_NIC_CONF_1_TEMP_TX_ALC);

	if (nic_conf1 & MT_EE_NIC_CONF_1_HW_RF_CTRL)
		dev_err(dev->dev,
			"Error: this driver does not support HW RF ctrl\n");

	if (!field_valid(nic_conf0 >> 8))
		return;

	if (MT76_GET(MT_EE_NIC_CONF_0_RX_PATH, nic_conf0) > 1 ||
	    MT76_GET(MT_EE_NIC_CONF_0_TX_PATH, nic_conf0) > 1)
		dev_err(dev->dev,
			"Error: device has more than 1 RX/TX stream!\n");
}

static int
mt7601u_set_macaddr(struct mt7601u_dev *dev, const u8 *eeprom)
{
	const void *src = eeprom + MT_EE_MAC_ADDR;

	ether_addr_copy(dev->macaddr, src);

	if (!is_valid_ether_addr(dev->macaddr)) {
		eth_random_addr(dev->macaddr);
		dev_info(dev->dev,
			 "Invalid MAC address, using random address %pM\n",
			 dev->macaddr);
	}

	mt76_wr(dev, MT_MAC_ADDR_DW0, get_unaligned_le32(dev->macaddr));
	mt76_wr(dev, MT_MAC_ADDR_DW1, get_unaligned_le16(dev->macaddr + 4) |
		MT76_SET(MT_MAC_ADDR_DW1_U2ME_MASK, 0xff));

	return 0;
}

static void mt7601u_set_channel_target_power(struct mt7601u_dev *dev,
					     u8 *eeprom, u8 max_pwr)
{
	u8 trgt_pwr = eeprom[MT_EE_TX_TSSI_TARGET_POWER];

	if (trgt_pwr > max_pwr || !trgt_pwr) {
		dev_warn(dev->dev, "Error: EEPROM trgt power invalid %hhx!\n",
			 trgt_pwr);
		trgt_pwr = 0x20;
	}

	memset(dev->ee->chan_pwr, trgt_pwr, sizeof(dev->ee->chan_pwr));
}

static void
mt7601u_set_channel_power(struct mt7601u_dev *dev, u8 *eeprom)
{
	u32 i, val;
	u8 max_pwr;

	val = mt7601u_rr(dev, MT_TX_ALC_CFG_0);
	max_pwr = MT76_GET(MT_TX_ALC_CFG_0_LIMIT_0, val);

	if (mt7601u_has_tssi(dev, eeprom)) {
		mt7601u_set_channel_target_power(dev, eeprom, max_pwr);
		return;
	}

	for (i = 0; i < 14; i++) {
		s8 power = field_validate(eeprom[MT_EE_TX_POWER_OFFSET + i]);

		if (power > max_pwr || power < 0)
			power = MT7601U_DEFAULT_TX_POWER;

		dev->ee->chan_pwr[i] = power;
	}
}

static void
mt7601u_set_country_reg(struct mt7601u_dev *dev, u8 *eeprom)
{
	/* Note: - region 31 is not valid for mt7601u (see rtmp_init.c)
	 *	 - comments in rtmp_def.h are incorrect (see rt_channel.c)
	 */
	static const struct reg_channel_bounds chan_bounds[] = {
		/* EEPROM country regions 0 - 7 */
		{  1, 11 },	{  1, 13 },	{ 10,  2 },	{ 10,  4 },
		{ 14,  1 },	{  1, 14 },	{  3,  7 },	{  5,  9 },
		/* EEPROM country regions 32 - 33 */
		{  1, 11 },	{  1, 14 }
	};
	u8 val = eeprom[MT_EE_COUNTRY_REGION];
	int idx = -1;

	if (val < 8)
		idx = val;
	if (val > 31 && val < 33)
		idx = val - 32 + 8;

	if (idx != -1)
		dev_info(dev->dev,
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
mt7601u_set_rf_freq_off(struct mt7601u_dev *dev, u8 *eeprom)
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
mt7601u_set_rssi_offset(struct mt7601u_dev *dev, u8 *eeprom)
{
	int i;
	s8 *rssi_offset = dev->ee->rssi_offset;

	for (i = 0; i < 2; i++) {
		rssi_offset[i] = eeprom[MT_EE_RSSI_OFFSET + i];

		if (rssi_offset[i] < -10 || rssi_offset[i] > 10) {
			dev_warn(dev->dev,
				 "Warning: EEPROM RSSI is invalid %02hhx\n",
				 rssi_offset[i]);
			rssi_offset[i] = 0;
		}
	}
}

static void
mt7601u_extra_power_over_mac(struct mt7601u_dev *dev)
{
	u32 val;

	val = ((mt7601u_rr(dev, MT_TX_PWR_CFG_1) & 0x0000ff00) >> 8);
	val |= ((mt7601u_rr(dev, MT_TX_PWR_CFG_2) & 0x0000ff00) << 8);
	mt7601u_wr(dev, MT_TX_PWR_CFG_7, val);

	val = ((mt7601u_rr(dev, MT_TX_PWR_CFG_4) & 0x0000ff00) >> 8);
	mt7601u_wr(dev, MT_TX_PWR_CFG_9, val);
}

static void
mt7601u_set_power_rate(struct power_per_rate *rate, s8 delta, u8 value)
{
	/* Invalid? Note: vendor driver does not handle this */
	if (value == 0xff)
		return;

	rate->raw = s6_validate(value);
	rate->bw20 = s6_to_int(value);
	/* Note: vendor driver does cap the value to s6 right away */
	rate->bw40 = rate->bw20 + delta;
}

static void
mt7601u_save_power_rate(struct mt7601u_dev *dev, s8 delta, u32 val, int i)
{
	struct mt7601u_rate_power *t = &dev->ee->power_rate_table;

	switch (i) {
	case 0:
		mt7601u_set_power_rate(&t->cck[0], delta, (val >> 0) & 0xff);
		mt7601u_set_power_rate(&t->cck[1], delta, (val >> 8) & 0xff);
		/* Save cck bw20 for fixups of channel 14 */
		dev->ee->real_cck_bw20[0] = t->cck[0].bw20;
		dev->ee->real_cck_bw20[1] = t->cck[1].bw20;

		mt7601u_set_power_rate(&t->ofdm[0], delta, (val >> 16) & 0xff);
		mt7601u_set_power_rate(&t->ofdm[1], delta, (val >> 24) & 0xff);
		break;
	case 1:
		mt7601u_set_power_rate(&t->ofdm[2], delta, (val >> 0) & 0xff);
		mt7601u_set_power_rate(&t->ofdm[3], delta, (val >> 8) & 0xff);
		mt7601u_set_power_rate(&t->ht[0], delta, (val >> 16) & 0xff);
		mt7601u_set_power_rate(&t->ht[1], delta, (val >> 24) & 0xff);
		break;
	case 2:
		mt7601u_set_power_rate(&t->ht[2], delta, (val >> 0) & 0xff);
		mt7601u_set_power_rate(&t->ht[3], delta, (val >> 8) & 0xff);
		break;
	}
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
mt7601u_config_tx_power_per_rate(struct mt7601u_dev *dev, u8 *eeprom)
{
	u32 val;
	s8 bw40_delta;
	int i;

	bw40_delta = get_delta(eeprom[MT_EE_TX_POWER_DELTA_BW40]);

	for (i = 0; i < 5; i++) {
		val = get_unaligned_le32(eeprom + MT_EE_TX_POWER_BYRATE(i));

		mt7601u_save_power_rate(dev, bw40_delta, val, i);

		if (~val)
			mt7601u_wr(dev, MT_TX_PWR_CFG_0 + i * 4, val);
	}

	mt7601u_extra_power_over_mac(dev);
}

static void
mt7601u_init_tssi_params(struct mt7601u_dev *dev, u8 *eeprom)
{
	struct tssi_data *d = &dev->ee->tssi_data;

	if (!dev->ee->tssi_enabled)
		return;

	d->slope = eeprom[MT_EE_TX_TSSI_SLOPE];
	d->tx0_delta_offset = eeprom[MT_EE_TX_TSSI_OFFSET] * 1024;
	d->offset[0] = eeprom[MT_EE_TX_TSSI_OFFSET_GROUP];
	d->offset[1] = eeprom[MT_EE_TX_TSSI_OFFSET_GROUP + 1];
	d->offset[2] = eeprom[MT_EE_TX_TSSI_OFFSET_GROUP + 2];
}

int
mt7601u_eeprom_init(struct mt7601u_dev *dev)
{
	u8 *eeprom;
	int i, ret;

	ret = mt7601u_efuse_physical_size_check(dev);
	if (ret)
		return ret;

	dev->ee = devm_kzalloc(dev->dev, sizeof(*dev->ee), GFP_KERNEL);
	if (!dev->ee)
		return -ENOMEM;

	eeprom = kmalloc(MT7601U_EEPROM_SIZE, GFP_KERNEL);
	if (!eeprom)
		return -ENOMEM;

	for (i = 0; i + 16 <= MT7601U_EEPROM_SIZE; i += 16) {
		ret = mt7601u_efuse_read(dev, i, eeprom + i, MT_EE_READ);
		if (ret)
			goto out;
	}

	if (eeprom[MT_EE_VERSION_EE] > MT7601U_EE_MAX_VER)
		dev_warn(dev->dev,
			 "Warning: unsupported EEPROM version %02hhx\n",
			 eeprom[MT_EE_VERSION_EE]);
	dev_info(dev->dev, "EEPROM ver:%02hhx fae:%02hhx\n",
		 eeprom[MT_EE_VERSION_EE], eeprom[MT_EE_VERSION_FAE]);

	mt7601u_set_macaddr(dev, eeprom);
	mt7601u_set_chip_cap(dev, eeprom);
	mt7601u_set_channel_power(dev, eeprom);
	mt7601u_set_country_reg(dev, eeprom);
	mt7601u_set_rf_freq_off(dev, eeprom);
	mt7601u_set_rssi_offset(dev, eeprom);
	dev->ee->ref_temp = eeprom[MT_EE_REF_TEMP];
	dev->ee->lna_gain = eeprom[MT_EE_LNA_GAIN];

	mt7601u_config_tx_power_per_rate(dev, eeprom);

	mt7601u_init_tssi_params(dev, eeprom);
out:
	kfree(eeprom);
	return ret;
}
