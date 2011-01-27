
/*
 * Radio tuning for RTL8225 on RTL8180
 *
 * Copyright 2007 Michael Wu <flamingice@sourmilk.net>
 * Copyright 2007 Andrea Merello <andreamrl@tiscali.it>
 *
 * Based on the r8180 driver, which is:
 * Copyright 2005 Andrea Merello <andreamrl@tiscali.it>, et al.
 *
 * Thanks to Realtek for their support!
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <net/mac80211.h>

#include "rtl8180.h"
#include "rtl8225.h"

static void rtl8225_write(struct ieee80211_hw *dev, u8 addr, u16 data)
{
	struct rtl8180_priv *priv = dev->priv;
	u16 reg80, reg84, reg82;
	u32 bangdata;
	int i;

	bangdata = (data << 4) | (addr & 0xf);

	reg80 = rtl818x_ioread16(priv, &priv->map->RFPinsOutput) & 0xfff3;
	reg82 = rtl818x_ioread16(priv, &priv->map->RFPinsEnable);

	rtl818x_iowrite16(priv, &priv->map->RFPinsEnable, reg82 | 0x7);

	reg84 = rtl818x_ioread16(priv, &priv->map->RFPinsSelect);
	rtl818x_iowrite16(priv, &priv->map->RFPinsSelect, reg84 | 0x7 | 0x400);
	rtl818x_ioread8(priv, &priv->map->EEPROM_CMD);
	udelay(10);

	rtl818x_iowrite16(priv, &priv->map->RFPinsOutput, reg80 | (1 << 2));
	rtl818x_ioread8(priv, &priv->map->EEPROM_CMD);
	udelay(2);
	rtl818x_iowrite16(priv, &priv->map->RFPinsOutput, reg80);
	rtl818x_ioread8(priv, &priv->map->EEPROM_CMD);
	udelay(10);

	for (i = 15; i >= 0; i--) {
		u16 reg = reg80;

		if (bangdata & (1 << i))
			reg |= 1;

		if (i & 1)
			rtl818x_iowrite16(priv, &priv->map->RFPinsOutput, reg);

		rtl818x_iowrite16(priv, &priv->map->RFPinsOutput, reg | (1 << 1));
		rtl818x_iowrite16(priv, &priv->map->RFPinsOutput, reg | (1 << 1));

		if (!(i & 1))
			rtl818x_iowrite16(priv, &priv->map->RFPinsOutput, reg);
	}

	rtl818x_iowrite16(priv, &priv->map->RFPinsOutput, reg80 | (1 << 2));
	rtl818x_ioread8(priv, &priv->map->EEPROM_CMD);
	udelay(10);

	rtl818x_iowrite16(priv, &priv->map->RFPinsOutput, reg80 | (1 << 2));
	rtl818x_iowrite16(priv, &priv->map->RFPinsSelect, reg84 | 0x400);
	rtl818x_iowrite16(priv, &priv->map->RFPinsEnable, 0x1FFF);
}

static u16 rtl8225_read(struct ieee80211_hw *dev, u8 addr)
{
	struct rtl8180_priv *priv = dev->priv;
	u16 reg80, reg82, reg84, out;
	int i;

	reg80 = rtl818x_ioread16(priv, &priv->map->RFPinsOutput);
	reg82 = rtl818x_ioread16(priv, &priv->map->RFPinsEnable);
	reg84 = rtl818x_ioread16(priv, &priv->map->RFPinsSelect) | 0x400;

	reg80 &= ~0xF;

	rtl818x_iowrite16(priv, &priv->map->RFPinsEnable, reg82 | 0x000F);
	rtl818x_iowrite16(priv, &priv->map->RFPinsSelect, reg84 | 0x000F);

	rtl818x_iowrite16(priv, &priv->map->RFPinsOutput, reg80 | (1 << 2));
	rtl818x_ioread8(priv, &priv->map->EEPROM_CMD);
	udelay(4);
	rtl818x_iowrite16(priv, &priv->map->RFPinsOutput, reg80);
	rtl818x_ioread8(priv, &priv->map->EEPROM_CMD);
	udelay(5);

	for (i = 4; i >= 0; i--) {
		u16 reg = reg80 | ((addr >> i) & 1);

		if (!(i & 1)) {
			rtl818x_iowrite16(priv, &priv->map->RFPinsOutput, reg);
			rtl818x_ioread8(priv, &priv->map->EEPROM_CMD);
			udelay(1);
		}

		rtl818x_iowrite16(priv, &priv->map->RFPinsOutput,
				  reg | (1 << 1));
		rtl818x_ioread8(priv, &priv->map->EEPROM_CMD);
		udelay(2);
		rtl818x_iowrite16(priv, &priv->map->RFPinsOutput,
				  reg | (1 << 1));
		rtl818x_ioread8(priv, &priv->map->EEPROM_CMD);
		udelay(2);

		if (i & 1) {
			rtl818x_iowrite16(priv, &priv->map->RFPinsOutput, reg);
			rtl818x_ioread8(priv, &priv->map->EEPROM_CMD);
			udelay(1);
		}
	}

	rtl818x_iowrite16(priv, &priv->map->RFPinsEnable, 0x000E);
	rtl818x_iowrite16(priv, &priv->map->RFPinsSelect, 0x040E);
	rtl818x_ioread8(priv, &priv->map->EEPROM_CMD);
	rtl818x_iowrite16(priv, &priv->map->RFPinsOutput,
			  reg80 | (1 << 3) | (1 << 1));
	rtl818x_ioread8(priv, &priv->map->EEPROM_CMD);
	udelay(2);
	rtl818x_iowrite16(priv, &priv->map->RFPinsOutput,
			  reg80 | (1 << 3));
	rtl818x_ioread8(priv, &priv->map->EEPROM_CMD);
	udelay(2);
	rtl818x_iowrite16(priv, &priv->map->RFPinsOutput,
			  reg80 | (1 << 3));
	rtl818x_ioread8(priv, &priv->map->EEPROM_CMD);
	udelay(2);

	out = 0;
	for (i = 11; i >= 0; i--) {
		rtl818x_iowrite16(priv, &priv->map->RFPinsOutput,
				  reg80 | (1 << 3));
		rtl818x_ioread8(priv, &priv->map->EEPROM_CMD);
		udelay(1);
		rtl818x_iowrite16(priv, &priv->map->RFPinsOutput,
				  reg80 | (1 << 3) | (1 << 1));
		rtl818x_ioread8(priv, &priv->map->EEPROM_CMD);
		udelay(2);
		rtl818x_iowrite16(priv, &priv->map->RFPinsOutput,
				  reg80 | (1 << 3) | (1 << 1));
		rtl818x_ioread8(priv, &priv->map->EEPROM_CMD);
		udelay(2);
		rtl818x_iowrite16(priv, &priv->map->RFPinsOutput,
				  reg80 | (1 << 3) | (1 << 1));
		rtl818x_ioread8(priv, &priv->map->EEPROM_CMD);
		udelay(2);

		if (rtl818x_ioread16(priv, &priv->map->RFPinsInput) & (1 << 1))
			out |= 1 << i;

		rtl818x_iowrite16(priv, &priv->map->RFPinsOutput,
				  reg80 | (1 << 3));
		rtl818x_ioread8(priv, &priv->map->EEPROM_CMD);
		udelay(2);
	}

	rtl818x_iowrite16(priv, &priv->map->RFPinsOutput,
			  reg80 | (1 << 3) | (1 << 2));
	rtl818x_ioread8(priv, &priv->map->EEPROM_CMD);
	udelay(2);

	rtl818x_iowrite16(priv, &priv->map->RFPinsEnable, reg82);
	rtl818x_iowrite16(priv, &priv->map->RFPinsSelect, reg84);
	rtl818x_iowrite16(priv, &priv->map->RFPinsOutput, 0x03A0);

	return out;
}

static const u16 rtl8225bcd_rxgain[] = {
	0x0400, 0x0401, 0x0402, 0x0403, 0x0404, 0x0405, 0x0408, 0x0409,
	0x040a, 0x040b, 0x0502, 0x0503, 0x0504, 0x0505, 0x0540, 0x0541,
	0x0542, 0x0543, 0x0544, 0x0545, 0x0580, 0x0581, 0x0582, 0x0583,
	0x0584, 0x0585, 0x0588, 0x0589, 0x058a, 0x058b, 0x0643, 0x0644,
	0x0645, 0x0680, 0x0681, 0x0682, 0x0683, 0x0684, 0x0685, 0x0688,
	0x0689, 0x068a, 0x068b, 0x068c, 0x0742, 0x0743, 0x0744, 0x0745,
	0x0780, 0x0781, 0x0782, 0x0783, 0x0784, 0x0785, 0x0788, 0x0789,
	0x078a, 0x078b, 0x078c, 0x078d, 0x0790, 0x0791, 0x0792, 0x0793,
	0x0794, 0x0795, 0x0798, 0x0799, 0x079a, 0x079b, 0x079c, 0x079d,
	0x07a0, 0x07a1, 0x07a2, 0x07a3, 0x07a4, 0x07a5, 0x07a8, 0x07a9,
	0x07aa, 0x07ab, 0x07ac, 0x07ad, 0x07b0, 0x07b1, 0x07b2, 0x07b3,
	0x07b4, 0x07b5, 0x07b8, 0x07b9, 0x07ba, 0x07bb, 0x07bb
};

static const u8 rtl8225_agc[] = {
	0x9e, 0x9e, 0x9e, 0x9e, 0x9e, 0x9e, 0x9e, 0x9e,
	0x9d, 0x9c, 0x9b, 0x9a, 0x99, 0x98, 0x97, 0x96,
	0x95, 0x94, 0x93, 0x92, 0x91, 0x90, 0x8f, 0x8e,
	0x8d, 0x8c, 0x8b, 0x8a, 0x89, 0x88, 0x87, 0x86,
	0x85, 0x84, 0x83, 0x82, 0x81, 0x80, 0x3f, 0x3e,
	0x3d, 0x3c, 0x3b, 0x3a, 0x39, 0x38, 0x37, 0x36,
	0x35, 0x34, 0x33, 0x32, 0x31, 0x30, 0x2f, 0x2e,
	0x2d, 0x2c, 0x2b, 0x2a, 0x29, 0x28, 0x27, 0x26,
	0x25, 0x24, 0x23, 0x22, 0x21, 0x20, 0x1f, 0x1e,
	0x1d, 0x1c, 0x1b, 0x1a, 0x19, 0x18, 0x17, 0x16,
	0x15, 0x14, 0x13, 0x12, 0x11, 0x10, 0x0f, 0x0e,
	0x0d, 0x0c, 0x0b, 0x0a, 0x09, 0x08, 0x07, 0x06,
	0x05, 0x04, 0x03, 0x02, 0x01, 0x01, 0x01, 0x01,
	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01
};

static const u8 rtl8225_gain[] = {
	0x23, 0x88, 0x7c, 0xa5, /* -82dbm */
	0x23, 0x88, 0x7c, 0xb5, /* -82dbm */
	0x23, 0x88, 0x7c, 0xc5, /* -82dbm */
	0x33, 0x80, 0x79, 0xc5, /* -78dbm */
	0x43, 0x78, 0x76, 0xc5, /* -74dbm */
	0x53, 0x60, 0x73, 0xc5, /* -70dbm */
	0x63, 0x58, 0x70, 0xc5, /* -66dbm */
};

static const u8 rtl8225_threshold[] = {
	0x8d, 0x8d, 0x8d, 0x8d, 0x9d, 0xad, 0xbd
};

static const u8 rtl8225_tx_gain_cck_ofdm[] = {
	0x02, 0x06, 0x0e, 0x1e, 0x3e, 0x7e
};

static const u8 rtl8225_tx_power_cck[] = {
	0x18, 0x17, 0x15, 0x11, 0x0c, 0x08, 0x04, 0x02,
	0x1b, 0x1a, 0x17, 0x13, 0x0e, 0x09, 0x04, 0x02,
	0x1f, 0x1e, 0x1a, 0x15, 0x10, 0x0a, 0x05, 0x02,
	0x22, 0x21, 0x1d, 0x18, 0x11, 0x0b, 0x06, 0x02,
	0x26, 0x25, 0x21, 0x1b, 0x14, 0x0d, 0x06, 0x03,
	0x2b, 0x2a, 0x25, 0x1e, 0x16, 0x0e, 0x07, 0x03
};

static const u8 rtl8225_tx_power_cck_ch14[] = {
	0x18, 0x17, 0x15, 0x0c, 0x00, 0x00, 0x00, 0x00,
	0x1b, 0x1a, 0x17, 0x0e, 0x00, 0x00, 0x00, 0x00,
	0x1f, 0x1e, 0x1a, 0x0f, 0x00, 0x00, 0x00, 0x00,
	0x22, 0x21, 0x1d, 0x11, 0x00, 0x00, 0x00, 0x00,
	0x26, 0x25, 0x21, 0x13, 0x00, 0x00, 0x00, 0x00,
	0x2b, 0x2a, 0x25, 0x15, 0x00, 0x00, 0x00, 0x00
};

static const u8 rtl8225_tx_power_ofdm[] = {
	0x80, 0x90, 0xa2, 0xb5, 0xcb, 0xe4
};

static const u32 rtl8225_chan[] = {
	0x085c, 0x08dc, 0x095c, 0x09dc, 0x0a5c, 0x0adc, 0x0b5c,
	0x0bdc, 0x0c5c, 0x0cdc, 0x0d5c, 0x0ddc, 0x0e5c, 0x0f72
};

static void rtl8225_rf_set_tx_power(struct ieee80211_hw *dev, int channel)
{
	struct rtl8180_priv *priv = dev->priv;
	u8 cck_power, ofdm_power;
	const u8 *tmp;
	u32 reg;
	int i;

	cck_power = priv->channels[channel - 1].hw_value & 0xFF;
	ofdm_power = priv->channels[channel - 1].hw_value >> 8;

	cck_power = min(cck_power, (u8)35);
	ofdm_power = min(ofdm_power, (u8)35);

	rtl818x_iowrite8(priv, &priv->map->TX_GAIN_CCK,
			 rtl8225_tx_gain_cck_ofdm[cck_power / 6] >> 1);

	if (channel == 14)
		tmp = &rtl8225_tx_power_cck_ch14[(cck_power % 6) * 8];
	else
		tmp = &rtl8225_tx_power_cck[(cck_power % 6) * 8];

	for (i = 0; i < 8; i++)
		rtl8225_write_phy_cck(dev, 0x44 + i, *tmp++);

	msleep(1); /* FIXME: optional? */

	/* anaparam2 on */
	rtl818x_iowrite8(priv, &priv->map->EEPROM_CMD, RTL818X_EEPROM_CMD_CONFIG);
	reg = rtl818x_ioread8(priv, &priv->map->CONFIG3);
	rtl818x_iowrite8(priv, &priv->map->CONFIG3, reg | RTL818X_CONFIG3_ANAPARAM_WRITE);
	rtl818x_iowrite32(priv, &priv->map->ANAPARAM2, RTL8225_ANAPARAM2_ON);
	rtl818x_iowrite8(priv, &priv->map->CONFIG3, reg & ~RTL818X_CONFIG3_ANAPARAM_WRITE);
	rtl818x_iowrite8(priv, &priv->map->EEPROM_CMD, RTL818X_EEPROM_CMD_NORMAL);

	rtl818x_iowrite8(priv, &priv->map->TX_GAIN_OFDM,
			 rtl8225_tx_gain_cck_ofdm[ofdm_power/6] >> 1);

	tmp = &rtl8225_tx_power_ofdm[ofdm_power % 6];

	rtl8225_write_phy_ofdm(dev, 5, *tmp);
	rtl8225_write_phy_ofdm(dev, 7, *tmp);

	msleep(1);
}

static void rtl8225_rf_init(struct ieee80211_hw *dev)
{
	struct rtl8180_priv *priv = dev->priv;
	int i;

	rtl8180_set_anaparam(priv, RTL8225_ANAPARAM_ON);

	/* host_pci_init */
	rtl818x_iowrite16(priv, &priv->map->RFPinsOutput, 0x0480);
	rtl818x_iowrite16(priv, &priv->map->RFPinsEnable, 0x1FFF);
	rtl818x_iowrite16(priv, &priv->map->RFPinsSelect, 0x0488);
	rtl818x_iowrite8(priv, &priv->map->GP_ENABLE, 0);
	rtl818x_ioread8(priv, &priv->map->EEPROM_CMD);
	msleep(200);	/* FIXME: ehh?? */
	rtl818x_iowrite8(priv, &priv->map->GP_ENABLE, 0xFF & ~(1 << 6));

	rtl818x_iowrite32(priv, &priv->map->RF_TIMING, 0x000a8008);

	/* TODO: check if we need really to change BRSR to do RF config */
	rtl818x_ioread16(priv, &priv->map->BRSR);
	rtl818x_iowrite16(priv, &priv->map->BRSR, 0xFFFF);
	rtl818x_iowrite32(priv, &priv->map->RF_PARA, 0x00100044);
	rtl818x_iowrite8(priv, &priv->map->EEPROM_CMD, RTL818X_EEPROM_CMD_CONFIG);
	rtl818x_iowrite8(priv, &priv->map->CONFIG3, 0x44);
	rtl818x_iowrite8(priv, &priv->map->EEPROM_CMD, RTL818X_EEPROM_CMD_NORMAL);

	rtl8225_write(dev, 0x0, 0x067);
	rtl8225_write(dev, 0x1, 0xFE0);
	rtl8225_write(dev, 0x2, 0x44D);
	rtl8225_write(dev, 0x3, 0x441);
	rtl8225_write(dev, 0x4, 0x8BE);
	rtl8225_write(dev, 0x5, 0xBF0);		/* TODO: minipci */
	rtl8225_write(dev, 0x6, 0xAE6);
	rtl8225_write(dev, 0x7, rtl8225_chan[0]);
	rtl8225_write(dev, 0x8, 0x01F);
	rtl8225_write(dev, 0x9, 0x334);
	rtl8225_write(dev, 0xA, 0xFD4);
	rtl8225_write(dev, 0xB, 0x391);
	rtl8225_write(dev, 0xC, 0x050);
	rtl8225_write(dev, 0xD, 0x6DB);
	rtl8225_write(dev, 0xE, 0x029);
	rtl8225_write(dev, 0xF, 0x914); msleep(1);

	rtl8225_write(dev, 0x2, 0xC4D); msleep(100);

	rtl8225_write(dev, 0x0, 0x127);

	for (i = 0; i < ARRAY_SIZE(rtl8225bcd_rxgain); i++) {
		rtl8225_write(dev, 0x1, i + 1);
		rtl8225_write(dev, 0x2, rtl8225bcd_rxgain[i]);
	}

	rtl8225_write(dev, 0x0, 0x027);
	rtl8225_write(dev, 0x0, 0x22F);
	rtl818x_iowrite16(priv, &priv->map->RFPinsEnable, 0x1FFF);

	for (i = 0; i < ARRAY_SIZE(rtl8225_agc); i++) {
		rtl8225_write_phy_ofdm(dev, 0xB, rtl8225_agc[i]);
		msleep(1);
		rtl8225_write_phy_ofdm(dev, 0xA, 0x80 + i);
		msleep(1);
	}

	msleep(1);

	rtl8225_write_phy_ofdm(dev, 0x00, 0x01); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x01, 0x02); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x02, 0x62); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x03, 0x00); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x04, 0x00); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x05, 0x00); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x06, 0x00); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x07, 0x00); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x08, 0x00); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x09, 0xfe); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x0a, 0x09); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x0b, 0x80); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x0c, 0x01); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x0e, 0xd3); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x0f, 0x38); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x10, 0x84); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x11, 0x03); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x12, 0x20); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x13, 0x20); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x14, 0x00); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x15, 0x40); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x16, 0x00); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x17, 0x40); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x18, 0xef); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x19, 0x19); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x1a, 0x20); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x1b, 0x76); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x1c, 0x04); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x1e, 0x95); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x1f, 0x75); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x20, 0x1f); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x21, 0x27); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x22, 0x16); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x24, 0x46); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x25, 0x20); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x26, 0x90); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x27, 0x88); msleep(1);

	rtl8225_write_phy_cck(dev, 0x00, 0x98); msleep(1);
	rtl8225_write_phy_cck(dev, 0x03, 0x20); msleep(1);
	rtl8225_write_phy_cck(dev, 0x04, 0x7e); msleep(1);
	rtl8225_write_phy_cck(dev, 0x05, 0x12); msleep(1);
	rtl8225_write_phy_cck(dev, 0x06, 0xfc); msleep(1);
	rtl8225_write_phy_cck(dev, 0x07, 0x78); msleep(1);
	rtl8225_write_phy_cck(dev, 0x08, 0x2e); msleep(1);
	rtl8225_write_phy_cck(dev, 0x10, 0x93); msleep(1);
	rtl8225_write_phy_cck(dev, 0x11, 0x88); msleep(1);
	rtl8225_write_phy_cck(dev, 0x12, 0x47); msleep(1);
	rtl8225_write_phy_cck(dev, 0x13, 0xd0);
	rtl8225_write_phy_cck(dev, 0x19, 0x00);
	rtl8225_write_phy_cck(dev, 0x1a, 0xa0);
	rtl8225_write_phy_cck(dev, 0x1b, 0x08);
	rtl8225_write_phy_cck(dev, 0x40, 0x86);
	rtl8225_write_phy_cck(dev, 0x41, 0x8d); msleep(1);
	rtl8225_write_phy_cck(dev, 0x42, 0x15); msleep(1);
	rtl8225_write_phy_cck(dev, 0x43, 0x18); msleep(1);
	rtl8225_write_phy_cck(dev, 0x44, 0x1f); msleep(1);
	rtl8225_write_phy_cck(dev, 0x45, 0x1e); msleep(1);
	rtl8225_write_phy_cck(dev, 0x46, 0x1a); msleep(1);
	rtl8225_write_phy_cck(dev, 0x47, 0x15); msleep(1);
	rtl8225_write_phy_cck(dev, 0x48, 0x10); msleep(1);
	rtl8225_write_phy_cck(dev, 0x49, 0x0a); msleep(1);
	rtl8225_write_phy_cck(dev, 0x4a, 0x05); msleep(1);
	rtl8225_write_phy_cck(dev, 0x4b, 0x02); msleep(1);
	rtl8225_write_phy_cck(dev, 0x4c, 0x05); msleep(1);

	rtl818x_iowrite8(priv, &priv->map->TESTR, 0x0D); msleep(1);

	rtl8225_rf_set_tx_power(dev, 1);

	/* RX antenna default to A */
	rtl8225_write_phy_cck(dev, 0x10, 0x9b); msleep(1);	/* B: 0xDB */
	rtl8225_write_phy_ofdm(dev, 0x26, 0x90); msleep(1);	/* B: 0x10 */

	rtl818x_iowrite8(priv, &priv->map->TX_ANTENNA, 0x03);	/* B: 0x00 */
	msleep(1);
	rtl818x_iowrite32(priv, (__le32 __iomem *)((void __iomem *)priv->map + 0x94), 0x15c00002);
	rtl818x_iowrite16(priv, &priv->map->RFPinsEnable, 0x1FFF);

	rtl8225_write(dev, 0x0c, 0x50);
	/* set OFDM initial gain */
	rtl8225_write_phy_ofdm(dev, 0x0d, rtl8225_gain[4 * 4]);
	rtl8225_write_phy_ofdm(dev, 0x23, rtl8225_gain[4 * 4 + 1]);
	rtl8225_write_phy_ofdm(dev, 0x1b, rtl8225_gain[4 * 4 + 2]);
	rtl8225_write_phy_ofdm(dev, 0x1d, rtl8225_gain[4 * 4 + 3]);
	/* set CCK threshold */
	rtl8225_write_phy_cck(dev, 0x41, rtl8225_threshold[0]);
}

static const u8 rtl8225z2_tx_power_cck_ch14[] = {
	0x36, 0x35, 0x2e, 0x1b, 0x00, 0x00, 0x00, 0x00
};

static const u8 rtl8225z2_tx_power_cck_B[] = {
	0x30, 0x2f, 0x29, 0x21, 0x19, 0x10, 0x08, 0x04
};

static const u8 rtl8225z2_tx_power_cck_A[] = {
	0x33, 0x32, 0x2b, 0x23, 0x1a, 0x11, 0x08, 0x04
};

static const u8 rtl8225z2_tx_power_cck[] = {
	0x36, 0x35, 0x2e, 0x25, 0x1c, 0x12, 0x09, 0x04
};

static void rtl8225z2_rf_set_tx_power(struct ieee80211_hw *dev, int channel)
{
	struct rtl8180_priv *priv = dev->priv;
	u8 cck_power, ofdm_power;
	const u8 *tmp;
	int i;

	cck_power = priv->channels[channel - 1].hw_value & 0xFF;
	ofdm_power = priv->channels[channel - 1].hw_value >> 8;

	if (channel == 14)
		tmp = rtl8225z2_tx_power_cck_ch14;
	else if (cck_power == 12)
		tmp = rtl8225z2_tx_power_cck_B;
	else if (cck_power == 13)
		tmp = rtl8225z2_tx_power_cck_A;
	else
		tmp = rtl8225z2_tx_power_cck;

	for (i = 0; i < 8; i++)
		rtl8225_write_phy_cck(dev, 0x44 + i, *tmp++);

	cck_power = min(cck_power, (u8)35);
	if (cck_power == 13 || cck_power == 14)
		cck_power = 12;
	if (cck_power >= 15)
		cck_power -= 2;

	rtl818x_iowrite8(priv, &priv->map->TX_GAIN_CCK, cck_power);
	rtl818x_ioread8(priv, &priv->map->TX_GAIN_CCK);
	msleep(1);

	ofdm_power = min(ofdm_power, (u8)35);
	rtl818x_iowrite8(priv, &priv->map->TX_GAIN_OFDM, ofdm_power);

	rtl8225_write_phy_ofdm(dev, 2, 0x62);
	rtl8225_write_phy_ofdm(dev, 5, 0x00);
	rtl8225_write_phy_ofdm(dev, 6, 0x40);
	rtl8225_write_phy_ofdm(dev, 7, 0x00);
	rtl8225_write_phy_ofdm(dev, 8, 0x40);

	msleep(1);
}

static const u16 rtl8225z2_rxgain[] = {
	0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0008, 0x0009,
	0x000a, 0x000b, 0x0102, 0x0103, 0x0104, 0x0105, 0x0140, 0x0141,
	0x0142, 0x0143, 0x0144, 0x0145, 0x0180, 0x0181, 0x0182, 0x0183,
	0x0184, 0x0185, 0x0188, 0x0189, 0x018a, 0x018b, 0x0243, 0x0244,
	0x0245, 0x0280, 0x0281, 0x0282, 0x0283, 0x0284, 0x0285, 0x0288,
	0x0289, 0x028a, 0x028b, 0x028c, 0x0342, 0x0343, 0x0344, 0x0345,
	0x0380, 0x0381, 0x0382, 0x0383, 0x0384, 0x0385, 0x0388, 0x0389,
	0x038a, 0x038b, 0x038c, 0x038d, 0x0390, 0x0391, 0x0392, 0x0393,
	0x0394, 0x0395, 0x0398, 0x0399, 0x039a, 0x039b, 0x039c, 0x039d,
	0x03a0, 0x03a1, 0x03a2, 0x03a3, 0x03a4, 0x03a5, 0x03a8, 0x03a9,
	0x03aa, 0x03ab, 0x03ac, 0x03ad, 0x03b0, 0x03b1, 0x03b2, 0x03b3,
	0x03b4, 0x03b5, 0x03b8, 0x03b9, 0x03ba, 0x03bb, 0x03bb
};

static void rtl8225z2_rf_init(struct ieee80211_hw *dev)
{
	struct rtl8180_priv *priv = dev->priv;
	int i;

	rtl8180_set_anaparam(priv, RTL8225_ANAPARAM_ON);

	/* host_pci_init */
	rtl818x_iowrite16(priv, &priv->map->RFPinsOutput, 0x0480);
	rtl818x_iowrite16(priv, &priv->map->RFPinsEnable, 0x1FFF);
	rtl818x_iowrite16(priv, &priv->map->RFPinsSelect, 0x0488);
	rtl818x_iowrite8(priv, &priv->map->GP_ENABLE, 0);
	rtl818x_ioread8(priv, &priv->map->EEPROM_CMD);
	msleep(200);	/* FIXME: ehh?? */
	rtl818x_iowrite8(priv, &priv->map->GP_ENABLE, 0xFF & ~(1 << 6));

	rtl818x_iowrite32(priv, &priv->map->RF_TIMING, 0x00088008);

	/* TODO: check if we need really to change BRSR to do RF config */
	rtl818x_ioread16(priv, &priv->map->BRSR);
	rtl818x_iowrite16(priv, &priv->map->BRSR, 0xFFFF);
	rtl818x_iowrite32(priv, &priv->map->RF_PARA, 0x00100044);
	rtl818x_iowrite8(priv, &priv->map->EEPROM_CMD, RTL818X_EEPROM_CMD_CONFIG);
	rtl818x_iowrite8(priv, &priv->map->CONFIG3, 0x44);
	rtl818x_iowrite8(priv, &priv->map->EEPROM_CMD, RTL818X_EEPROM_CMD_NORMAL);

	rtl818x_iowrite16(priv, &priv->map->RFPinsEnable, 0x1FFF);

	rtl8225_write(dev, 0x0, 0x0B7); msleep(1);
	rtl8225_write(dev, 0x1, 0xEE0); msleep(1);
	rtl8225_write(dev, 0x2, 0x44D); msleep(1);
	rtl8225_write(dev, 0x3, 0x441); msleep(1);
	rtl8225_write(dev, 0x4, 0x8C3); msleep(1);
	rtl8225_write(dev, 0x5, 0xC72); msleep(1);
	rtl8225_write(dev, 0x6, 0x0E6); msleep(1);
	rtl8225_write(dev, 0x7, 0x82A); msleep(1);
	rtl8225_write(dev, 0x8, 0x03F); msleep(1);
	rtl8225_write(dev, 0x9, 0x335); msleep(1);
	rtl8225_write(dev, 0xa, 0x9D4); msleep(1);
	rtl8225_write(dev, 0xb, 0x7BB); msleep(1);
	rtl8225_write(dev, 0xc, 0x850); msleep(1);
	rtl8225_write(dev, 0xd, 0xCDF); msleep(1);
	rtl8225_write(dev, 0xe, 0x02B); msleep(1);
	rtl8225_write(dev, 0xf, 0x114); msleep(100);

	if (!(rtl8225_read(dev, 6) & (1 << 7))) {
		rtl8225_write(dev, 0x02, 0x0C4D);
		msleep(200);
		rtl8225_write(dev, 0x02, 0x044D);
		msleep(100);
		/* TODO: readd calibration failure message when the calibration
		   check works */
	}

	rtl8225_write(dev, 0x0, 0x1B7);
	rtl8225_write(dev, 0x3, 0x002);
	rtl8225_write(dev, 0x5, 0x004);

	for (i = 0; i < ARRAY_SIZE(rtl8225z2_rxgain); i++) {
		rtl8225_write(dev, 0x1, i + 1);
		rtl8225_write(dev, 0x2, rtl8225z2_rxgain[i]);
	}

	rtl8225_write(dev, 0x0, 0x0B7); msleep(100);
	rtl8225_write(dev, 0x2, 0xC4D);

	msleep(200);
	rtl8225_write(dev, 0x2, 0x44D);
	msleep(100);

	rtl8225_write(dev, 0x00, 0x2BF);
	rtl8225_write(dev, 0xFF, 0xFFFF);

	rtl818x_iowrite16(priv, &priv->map->RFPinsEnable, 0x1FFF);

	for (i = 0; i < ARRAY_SIZE(rtl8225_agc); i++) {
		rtl8225_write_phy_ofdm(dev, 0xB, rtl8225_agc[i]);
		msleep(1);
		rtl8225_write_phy_ofdm(dev, 0xA, 0x80 + i);
		msleep(1);
	}

	msleep(1);

	rtl8225_write_phy_ofdm(dev, 0x00, 0x01); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x01, 0x02); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x02, 0x62); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x03, 0x00); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x04, 0x00); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x05, 0x00); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x06, 0x40); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x07, 0x00); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x08, 0x40); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x09, 0xfe); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x0a, 0x09); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x18, 0xef); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x0b, 0x80); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x0c, 0x01); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x0d, 0x43);
	rtl8225_write_phy_ofdm(dev, 0x0e, 0xd3); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x0f, 0x38); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x10, 0x84); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x11, 0x06); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x12, 0x20); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x13, 0x20); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x14, 0x00); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x15, 0x40); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x16, 0x00); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x17, 0x40); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x18, 0xef); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x19, 0x19); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x1a, 0x20); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x1b, 0x11); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x1c, 0x04); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x1d, 0xc5); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x1e, 0xb3); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x1f, 0x75); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x20, 0x1f); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x21, 0x27); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x22, 0x16); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x23, 0x80); msleep(1); /* FIXME: not needed? */
	rtl8225_write_phy_ofdm(dev, 0x24, 0x46); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x25, 0x20); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x26, 0x90); msleep(1);
	rtl8225_write_phy_ofdm(dev, 0x27, 0x88); msleep(1);

	rtl8225_write_phy_cck(dev, 0x00, 0x98); msleep(1);
	rtl8225_write_phy_cck(dev, 0x03, 0x20); msleep(1);
	rtl8225_write_phy_cck(dev, 0x04, 0x7e); msleep(1);
	rtl8225_write_phy_cck(dev, 0x05, 0x12); msleep(1);
	rtl8225_write_phy_cck(dev, 0x06, 0xfc); msleep(1);
	rtl8225_write_phy_cck(dev, 0x07, 0x78); msleep(1);
	rtl8225_write_phy_cck(dev, 0x08, 0x2e); msleep(1);
	rtl8225_write_phy_cck(dev, 0x10, 0x93); msleep(1);
	rtl8225_write_phy_cck(dev, 0x11, 0x88); msleep(1);
	rtl8225_write_phy_cck(dev, 0x12, 0x47); msleep(1);
	rtl8225_write_phy_cck(dev, 0x13, 0xd0);
	rtl8225_write_phy_cck(dev, 0x19, 0x00);
	rtl8225_write_phy_cck(dev, 0x1a, 0xa0);
	rtl8225_write_phy_cck(dev, 0x1b, 0x08);
	rtl8225_write_phy_cck(dev, 0x40, 0x86);
	rtl8225_write_phy_cck(dev, 0x41, 0x8a); msleep(1);
	rtl8225_write_phy_cck(dev, 0x42, 0x15); msleep(1);
	rtl8225_write_phy_cck(dev, 0x43, 0x18); msleep(1);
	rtl8225_write_phy_cck(dev, 0x44, 0x36); msleep(1);
	rtl8225_write_phy_cck(dev, 0x45, 0x35); msleep(1);
	rtl8225_write_phy_cck(dev, 0x46, 0x2e); msleep(1);
	rtl8225_write_phy_cck(dev, 0x47, 0x25); msleep(1);
	rtl8225_write_phy_cck(dev, 0x48, 0x1c); msleep(1);
	rtl8225_write_phy_cck(dev, 0x49, 0x12); msleep(1);
	rtl8225_write_phy_cck(dev, 0x4a, 0x09); msleep(1);
	rtl8225_write_phy_cck(dev, 0x4b, 0x04); msleep(1);
	rtl8225_write_phy_cck(dev, 0x4c, 0x05); msleep(1);

	rtl818x_iowrite8(priv, (u8 __iomem *)((void __iomem *)priv->map + 0x5B), 0x0D); msleep(1);

	rtl8225z2_rf_set_tx_power(dev, 1);

	/* RX antenna default to A */
	rtl8225_write_phy_cck(dev, 0x10, 0x9b); msleep(1);	/* B: 0xDB */
	rtl8225_write_phy_ofdm(dev, 0x26, 0x90); msleep(1);	/* B: 0x10 */

	rtl818x_iowrite8(priv, &priv->map->TX_ANTENNA, 0x03);	/* B: 0x00 */
	msleep(1);
	rtl818x_iowrite32(priv, (__le32 __iomem *)((void __iomem *)priv->map + 0x94), 0x15c00002);
	rtl818x_iowrite16(priv, &priv->map->RFPinsEnable, 0x1FFF);
}

static void rtl8225_rf_stop(struct ieee80211_hw *dev)
{
	struct rtl8180_priv *priv = dev->priv;
	u8 reg;

	rtl8225_write(dev, 0x4, 0x1f); msleep(1);

	rtl818x_iowrite8(priv, &priv->map->EEPROM_CMD, RTL818X_EEPROM_CMD_CONFIG);
	reg = rtl818x_ioread8(priv, &priv->map->CONFIG3);
	rtl818x_iowrite8(priv, &priv->map->CONFIG3, reg | RTL818X_CONFIG3_ANAPARAM_WRITE);
	rtl818x_iowrite32(priv, &priv->map->ANAPARAM2, RTL8225_ANAPARAM2_OFF);
	rtl818x_iowrite32(priv, &priv->map->ANAPARAM, RTL8225_ANAPARAM_OFF);
	rtl818x_iowrite8(priv, &priv->map->CONFIG3, reg & ~RTL818X_CONFIG3_ANAPARAM_WRITE);
	rtl818x_iowrite8(priv, &priv->map->EEPROM_CMD, RTL818X_EEPROM_CMD_NORMAL);
}

static void rtl8225_rf_set_channel(struct ieee80211_hw *dev,
				   struct ieee80211_conf *conf)
{
	struct rtl8180_priv *priv = dev->priv;
	int chan = ieee80211_frequency_to_channel(conf->channel->center_freq);

	if (priv->rf->init == rtl8225_rf_init)
		rtl8225_rf_set_tx_power(dev, chan);
	else
		rtl8225z2_rf_set_tx_power(dev, chan);

	rtl8225_write(dev, 0x7, rtl8225_chan[chan - 1]);
	msleep(10);
}

static void rtl8225_rf_conf_erp(struct ieee80211_hw *dev,
				struct ieee80211_bss_conf *info)
{
	struct rtl8180_priv *priv = dev->priv;

	if (info->use_short_slot) {
		rtl818x_iowrite8(priv, &priv->map->SLOT, 0x9);
		rtl818x_iowrite8(priv, &priv->map->SIFS, 0x22);
		rtl818x_iowrite8(priv, &priv->map->DIFS, 0x14);
		rtl818x_iowrite8(priv, &priv->map->EIFS, 81);
		rtl818x_iowrite8(priv, &priv->map->CW_VAL, 0x73);
	} else {
		rtl818x_iowrite8(priv, &priv->map->SLOT, 0x14);
		rtl818x_iowrite8(priv, &priv->map->SIFS, 0x44);
		rtl818x_iowrite8(priv, &priv->map->DIFS, 0x24);
		rtl818x_iowrite8(priv, &priv->map->EIFS, 81);
		rtl818x_iowrite8(priv, &priv->map->CW_VAL, 0xa5);
	}
}

static const struct rtl818x_rf_ops rtl8225_ops = {
	.name		= "rtl8225",
	.init		= rtl8225_rf_init,
	.stop		= rtl8225_rf_stop,
	.set_chan	= rtl8225_rf_set_channel,
	.conf_erp	= rtl8225_rf_conf_erp,
};

static const struct rtl818x_rf_ops rtl8225z2_ops = {
	.name		= "rtl8225z2",
	.init		= rtl8225z2_rf_init,
	.stop		= rtl8225_rf_stop,
	.set_chan	= rtl8225_rf_set_channel,
	.conf_erp	= rtl8225_rf_conf_erp,
};

const struct rtl818x_rf_ops * rtl8180_detect_rf(struct ieee80211_hw *dev)
{
	struct rtl8180_priv *priv = dev->priv;
	u16 reg8, reg9;

	rtl818x_iowrite16(priv, &priv->map->RFPinsOutput, 0x0480);
	rtl818x_iowrite16(priv, &priv->map->RFPinsSelect, 0x0488);
	rtl818x_iowrite16(priv, &priv->map->RFPinsEnable, 0x1FFF);
	rtl818x_ioread8(priv, &priv->map->EEPROM_CMD);
	msleep(100);

	rtl8225_write(dev, 0, 0x1B7);

	reg8 = rtl8225_read(dev, 8);
	reg9 = rtl8225_read(dev, 9);

	rtl8225_write(dev, 0, 0x0B7);

	if (reg8 != 0x588 || reg9 != 0x700)
		return &rtl8225_ops;

	return &rtl8225z2_ops;
}
