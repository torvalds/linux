// SPDX-License-Identifier: GPL-2.0-only

/* Radio tuning for RTL8225 on RTL8187SE
 *
 * Copyright 2009 Larry Finger <Larry.Finger@lwfinger.net>
 * Copyright 2014 Andrea Merello <andrea.merello@gmail.com>
 *
 * Based on the r8180 and Realtek r8187se drivers, which are:
 * Copyright 2004-2005 Andrea Merello <andrea.merello@gmail.com>, et al.
 *
 * Also based on the rtl8187 driver, which is:
 * Copyright 2007 Michael Wu <flamingice@sourmilk.net>
 * Copyright 2007 Andrea Merello <andrea.merello@gmail.com>
 */

#include <net/mac80211.h>

#include "rtl8180.h"
#include "rtl8225se.h"

#define PFX "rtl8225 (se) "

static const u32 RF_GAIN_TABLE[] = {
	0x0096, 0x0076, 0x0056, 0x0036, 0x0016, 0x01f6, 0x01d6, 0x01b6,
	0x0196, 0x0176, 0x00F7, 0x00D7, 0x00B7, 0x0097, 0x0077, 0x0057,
	0x0037, 0x00FB, 0x00DB, 0x00BB, 0x00FF, 0x00E3, 0x00C3, 0x00A3,
	0x0083, 0x0063, 0x0043, 0x0023, 0x0003, 0x01E3, 0x01C3, 0x01A3,
	0x0183, 0x0163, 0x0143, 0x0123, 0x0103
};

static const u8 cck_ofdm_gain_settings[] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
	0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b,
	0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11,
	0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
	0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d,
	0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23,
};

static const u8 rtl8225se_tx_gain_cck_ofdm[] = {
	0x02, 0x06, 0x0e, 0x1e, 0x3e, 0x7e
};

static const u8 rtl8225se_tx_power_cck[] = {
	0x18, 0x17, 0x15, 0x11, 0x0c, 0x08, 0x04, 0x02,
	0x1b, 0x1a, 0x17, 0x13, 0x0e, 0x09, 0x04, 0x02,
	0x1f, 0x1e, 0x1a, 0x15, 0x10, 0x0a, 0x05, 0x02,
	0x22, 0x21, 0x1d, 0x18, 0x11, 0x0b, 0x06, 0x02,
	0x26, 0x25, 0x21, 0x1b, 0x14, 0x0d, 0x06, 0x03,
	0x2b, 0x2a, 0x25, 0x1e, 0x16, 0x0e, 0x07, 0x03
};

static const u8 rtl8225se_tx_power_cck_ch14[] = {
	0x18, 0x17, 0x15, 0x0c, 0x00, 0x00, 0x00, 0x00,
	0x1b, 0x1a, 0x17, 0x0e, 0x00, 0x00, 0x00, 0x00,
	0x1f, 0x1e, 0x1a, 0x0f, 0x00, 0x00, 0x00, 0x00,
	0x22, 0x21, 0x1d, 0x11, 0x00, 0x00, 0x00, 0x00,
	0x26, 0x25, 0x21, 0x13, 0x00, 0x00, 0x00, 0x00,
	0x2b, 0x2a, 0x25, 0x15, 0x00, 0x00, 0x00, 0x00
};

static const u8 rtl8225se_tx_power_ofdm[] = {
	0x80, 0x90, 0xa2, 0xb5, 0xcb, 0xe4
};

static const u32 rtl8225se_chan[] = {
	0x0080, 0x0100, 0x0180, 0x0200, 0x0280, 0x0300, 0x0380,
	0x0400, 0x0480, 0x0500, 0x0580, 0x0600, 0x0680, 0x074A,
};

static const u8 rtl8225sez2_tx_power_cck_ch14[] = {
	0x36, 0x35, 0x2e, 0x1b, 0x00, 0x00, 0x00, 0x00
};

static const u8 rtl8225sez2_tx_power_cck_B[] = {
	0x30, 0x2f, 0x29, 0x21, 0x19, 0x10, 0x08, 0x04
};

static const u8 rtl8225sez2_tx_power_cck_A[] = {
	0x33, 0x32, 0x2b, 0x23, 0x1a, 0x11, 0x08, 0x04
};

static const u8 rtl8225sez2_tx_power_cck[] = {
	0x36, 0x35, 0x2e, 0x25, 0x1c, 0x12, 0x09, 0x04
};

static const u8 ZEBRA_AGC[] = {
	0x7E, 0x7E, 0x7E, 0x7E, 0x7D, 0x7C, 0x7B, 0x7A,
	0x79, 0x78, 0x77, 0x76, 0x75, 0x74, 0x73, 0x72,
	0x71, 0x70, 0x6F, 0x6E, 0x6D, 0x6C, 0x6B, 0x6A,
	0x69, 0x68, 0x67, 0x66, 0x65, 0x64, 0x63, 0x62,
	0x48, 0x47, 0x46, 0x45, 0x44, 0x29, 0x28, 0x27,
	0x26, 0x25, 0x24, 0x23, 0x22, 0x21, 0x08, 0x07,
	0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f,
	0x0f, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x15, 0x16,
	0x17, 0x17, 0x18, 0x18, 0x19, 0x1a, 0x1a, 0x1b,
	0x1b, 0x1c, 0x1c, 0x1d, 0x1d, 0x1d, 0x1e, 0x1e,
	0x1f, 0x1f, 0x1f, 0x20, 0x20, 0x20, 0x20, 0x21,
	0x21, 0x21, 0x22, 0x22, 0x22, 0x23, 0x23, 0x24,
	0x24, 0x25, 0x25, 0x25, 0x26, 0x26, 0x27, 0x27,
	0x2F, 0x2F, 0x2F, 0x2F, 0x2F, 0x2F, 0x2F, 0x2F
};

static const u8 OFDM_CONFIG[] = {
	0x10, 0x0F, 0x0A, 0x0C, 0x14, 0xFA, 0xFF, 0x50,
	0x00, 0x50, 0x00, 0x00, 0x00, 0x5C, 0x00, 0x00,
	0x40, 0x00, 0x40, 0x00, 0x00, 0x00, 0xA8, 0x26,
	0x32, 0x33, 0x06, 0xA5, 0x6F, 0x55, 0xC8, 0xBB,
	0x0A, 0xE1, 0x2C, 0x4A, 0x86, 0x83, 0x34, 0x00,
	0x4F, 0x24, 0x6F, 0xC2, 0x03, 0x40, 0x80, 0x00,
	0xC0, 0xC1, 0x58, 0xF1, 0x00, 0xC4, 0x90, 0x3e,
	0xD8, 0x3C, 0x7B, 0x10, 0x10
};

static void rtl8187se_three_wire_io(struct ieee80211_hw *dev, u8 *data,
				    u8 len, bool write)
{
	struct rtl8180_priv *priv = dev->priv;
	int i;
	u8 tmp;

	do {
		for (i = 0; i < 5; i++) {
			tmp = rtl818x_ioread8(priv, SW_3W_CMD1);
			if (!(tmp & 0x3))
				break;
			udelay(10);
		}
		if (i == 5)
			wiphy_err(dev->wiphy, PFX
				"CmdReg: 0x%x RE/WE bits aren't clear\n", tmp);

		tmp = rtl818x_ioread8(priv, &priv->map->rf_sw_config) | 0x02;
		rtl818x_iowrite8(priv, &priv->map->rf_sw_config, tmp);

		tmp = rtl818x_ioread8(priv, REG_ADDR1(0x84)) & 0xF7;
		rtl818x_iowrite8(priv, REG_ADDR1(0x84), tmp);
		if (write) {
			if (len == 16) {
				rtl818x_iowrite16(priv, SW_3W_DB0,
				  *(u16 *)data);
			} else if (len == 64) {
				rtl818x_iowrite32(priv, SW_3W_DB0_4,
				  *((u32 *)data));
				rtl818x_iowrite32(priv, SW_3W_DB1_4,
				  *((u32 *)(data + 4)));
			} else
				wiphy_err(dev->wiphy, PFX
					"Unimplemented length\n");
		} else {
			rtl818x_iowrite16(priv, SW_3W_DB0, *(u16 *)data);
		}
		if (write)
			tmp = 2;
		else
			tmp = 1;
		rtl818x_iowrite8(priv, SW_3W_CMD1, tmp);
		for (i = 0; i < 5; i++) {
			tmp = rtl818x_ioread8(priv, SW_3W_CMD1);
			if (!(tmp & 0x3))
				break;
			udelay(10);
		}
		rtl818x_iowrite8(priv, SW_3W_CMD1, 0);
		if (!write) {
			*((u16 *)data) = rtl818x_ioread16(priv, SI_DATA_REG);
			*((u16 *)data) &= 0x0FFF;
		}
	} while (0);
}

static u32 rtl8187se_rf_readreg(struct ieee80211_hw *dev, u8 addr)
{
	u32 dataread = addr & 0x0F;
	rtl8187se_three_wire_io(dev, (u8 *)&dataread, 16, 0);
	return dataread;
}

static void rtl8187se_rf_writereg(struct ieee80211_hw *dev, u8 addr, u32 data)
{
	u32 outdata = (data << 4) | (u32)(addr & 0x0F);
	rtl8187se_three_wire_io(dev, (u8 *)&outdata, 16, 1);
}


static void rtl8225se_write_zebra_agc(struct ieee80211_hw *dev)
{
	int i;

	for (i = 0; i < 128; i++) {
		rtl8225se_write_phy_ofdm(dev, 0xF, ZEBRA_AGC[i]);
		rtl8225se_write_phy_ofdm(dev, 0xE, i+0x80);
		rtl8225se_write_phy_ofdm(dev, 0xE, 0);
	}
}

static void rtl8187se_write_ofdm_config(struct ieee80211_hw *dev)
{
	/* write OFDM_CONFIG table */
	int i;

	for (i = 0; i < 60; i++)
		rtl8225se_write_phy_ofdm(dev, i, OFDM_CONFIG[i]);

}

static void rtl8225sez2_rf_set_tx_power(struct ieee80211_hw *dev, int channel)
{
	struct rtl8180_priv *priv = dev->priv;
	u8 cck_power, ofdm_power;

	cck_power = priv->channels[channel - 1].hw_value & 0xFF;
	if (cck_power > 35)
		cck_power = 35;
	rtl818x_iowrite8(priv, &priv->map->TX_GAIN_CCK,
			 cck_ofdm_gain_settings[cck_power]);

	usleep_range(1000, 5000);
	ofdm_power = priv->channels[channel - 1].hw_value >> 8;
	if (ofdm_power > 35)
		ofdm_power = 35;

	rtl818x_iowrite8(priv, &priv->map->TX_GAIN_OFDM,
			 cck_ofdm_gain_settings[ofdm_power]);
	if (ofdm_power < 12) {
		rtl8225se_write_phy_ofdm(dev, 7, 0x5C);
		rtl8225se_write_phy_ofdm(dev, 9, 0x5C);
	}
	if (ofdm_power < 18) {
		rtl8225se_write_phy_ofdm(dev, 7, 0x54);
		rtl8225se_write_phy_ofdm(dev, 9, 0x54);
	} else {
		rtl8225se_write_phy_ofdm(dev, 7, 0x50);
		rtl8225se_write_phy_ofdm(dev, 9, 0x50);
	}

	usleep_range(1000, 5000);
}

static void rtl8187se_write_rf_gain(struct ieee80211_hw *dev)
{
	int i;

	for (i = 0; i <= 36; i++) {
		rtl8187se_rf_writereg(dev, 0x01, i); mdelay(1);
		rtl8187se_rf_writereg(dev, 0x02, RF_GAIN_TABLE[i]); mdelay(1);
	}
}

static void rtl8187se_write_initial_gain(struct ieee80211_hw *dev,
					int init_gain)
{
	switch (init_gain) {
	default:
		rtl8225se_write_phy_ofdm(dev, 0x17, 0x26); mdelay(1);
		rtl8225se_write_phy_ofdm(dev, 0x24, 0x86); mdelay(1);
		rtl8225se_write_phy_ofdm(dev, 0x05, 0xFA); mdelay(1);
		break;
	case 2:
		rtl8225se_write_phy_ofdm(dev, 0x17, 0x36); mdelay(1);
		rtl8225se_write_phy_ofdm(dev, 0x24, 0x86); mdelay(1);
		rtl8225se_write_phy_ofdm(dev, 0x05, 0xFA); mdelay(1);
		break;
	case 3:
		rtl8225se_write_phy_ofdm(dev, 0x17, 0x36); mdelay(1);
		rtl8225se_write_phy_ofdm(dev, 0x24, 0x86); mdelay(1);
		rtl8225se_write_phy_ofdm(dev, 0x05, 0xFB); mdelay(1);
		break;
	case 4:
		rtl8225se_write_phy_ofdm(dev, 0x17, 0x46); mdelay(1);
		rtl8225se_write_phy_ofdm(dev, 0x24, 0x86); mdelay(1);
		rtl8225se_write_phy_ofdm(dev, 0x05, 0xFB); mdelay(1);
		break;
	case 5:
		rtl8225se_write_phy_ofdm(dev, 0x17, 0x46); mdelay(1);
		rtl8225se_write_phy_ofdm(dev, 0x24, 0x96); mdelay(1);
		rtl8225se_write_phy_ofdm(dev, 0x05, 0xFB); mdelay(1);
		break;
	case 6:
		rtl8225se_write_phy_ofdm(dev, 0x17, 0x56); mdelay(1);
		rtl8225se_write_phy_ofdm(dev, 0x24, 0x96); mdelay(1);
		rtl8225se_write_phy_ofdm(dev, 0x05, 0xFC); mdelay(1);
		break;
	case 7:
		rtl8225se_write_phy_ofdm(dev, 0x17, 0x56); mdelay(1);
		rtl8225se_write_phy_ofdm(dev, 0x24, 0xA6); mdelay(1);
		rtl8225se_write_phy_ofdm(dev, 0x05, 0xFC); mdelay(1);
		break;
	case 8:
		rtl8225se_write_phy_ofdm(dev, 0x17, 0x66); mdelay(1);
		rtl8225se_write_phy_ofdm(dev, 0x24, 0xB6); mdelay(1);
		rtl8225se_write_phy_ofdm(dev, 0x05, 0xFC); mdelay(1);
		break;
	}
}

void rtl8225se_rf_init(struct ieee80211_hw *dev)
{
	struct rtl8180_priv *priv = dev->priv;
	u32 rf23, rf24;
	u8 d_cut = 0;
	u8 tmp;

	/* Page 1 */
	rtl8187se_rf_writereg(dev, 0x00, 0x013F); mdelay(1);
	rf23 = rtl8187se_rf_readreg(dev, 0x08); mdelay(1);
	rf24 = rtl8187se_rf_readreg(dev, 0x09); mdelay(1);
	if (rf23 == 0x0818 && rf24 == 0x070C)
		d_cut = 1;

	wiphy_info(dev->wiphy, "RTL8225-SE version %s\n",
		d_cut ? "D" : "not-D");

	/* Page 0: reg 0 - 15 */
	rtl8187se_rf_writereg(dev, 0x00, 0x009F); mdelay(1);
	rtl8187se_rf_writereg(dev, 0x01, 0x06E0); mdelay(1);
	rtl8187se_rf_writereg(dev, 0x02, 0x004D); mdelay(1);
	rtl8187se_rf_writereg(dev, 0x03, 0x07F1); mdelay(1);
	rtl8187se_rf_writereg(dev, 0x04, 0x0975); mdelay(1);
	rtl8187se_rf_writereg(dev, 0x05, 0x0C72); mdelay(1);
	rtl8187se_rf_writereg(dev, 0x06, 0x0AE6); mdelay(1);
	rtl8187se_rf_writereg(dev, 0x07, 0x00CA); mdelay(1);
	rtl8187se_rf_writereg(dev, 0x08, 0x0E1C); mdelay(1);
	rtl8187se_rf_writereg(dev, 0x09, 0x02F0); mdelay(1);
	rtl8187se_rf_writereg(dev, 0x0A, 0x09D0); mdelay(1);
	rtl8187se_rf_writereg(dev, 0x0B, 0x01BA); mdelay(1);
	rtl8187se_rf_writereg(dev, 0x0C, 0x0640); mdelay(1);
	rtl8187se_rf_writereg(dev, 0x0D, 0x08DF); mdelay(1);
	rtl8187se_rf_writereg(dev, 0x0E, 0x0020); mdelay(1);
	rtl8187se_rf_writereg(dev, 0x0F, 0x0990); mdelay(1);
	/* page 1: reg 16-30 */
	rtl8187se_rf_writereg(dev, 0x00, 0x013F); mdelay(1);
	rtl8187se_rf_writereg(dev, 0x03, 0x0806); mdelay(1);
	rtl8187se_rf_writereg(dev, 0x04, 0x03A7); mdelay(1);
	rtl8187se_rf_writereg(dev, 0x05, 0x059B); mdelay(1);
	rtl8187se_rf_writereg(dev, 0x06, 0x0081); mdelay(1);
	rtl8187se_rf_writereg(dev, 0x07, 0x01A0); mdelay(1);
	rtl8187se_rf_writereg(dev, 0x0A, 0x0001); mdelay(1);
	rtl8187se_rf_writereg(dev, 0x0B, 0x0418); mdelay(1);
	rtl8187se_rf_writereg(dev, 0x0C, 0x0FBE); mdelay(1);
	rtl8187se_rf_writereg(dev, 0x0D, 0x0008); mdelay(1);
	if (d_cut)
		rtl8187se_rf_writereg(dev, 0x0E, 0x0807);
	else
		rtl8187se_rf_writereg(dev, 0x0E, 0x0806);
	mdelay(1);
	rtl8187se_rf_writereg(dev, 0x0F, 0x0ACC); mdelay(1);
	rtl8187se_rf_writereg(dev, 0x00, 0x01D7); mdelay(1);
	rtl8187se_rf_writereg(dev, 0x03, 0x0E00); mdelay(1);
	rtl8187se_rf_writereg(dev, 0x04, 0x0E50); mdelay(1);

	rtl8187se_write_rf_gain(dev);

	rtl8187se_rf_writereg(dev, 0x05, 0x0203); mdelay(1);
	rtl8187se_rf_writereg(dev, 0x06, 0x0200); mdelay(1);
	rtl8187se_rf_writereg(dev, 0x00, 0x0137); mdelay(11);
	rtl8187se_rf_writereg(dev, 0x0D, 0x0008); mdelay(11);
	rtl8187se_rf_writereg(dev, 0x00, 0x0037); mdelay(11);
	rtl8187se_rf_writereg(dev, 0x04, 0x0160); mdelay(11);
	rtl8187se_rf_writereg(dev, 0x07, 0x0080); mdelay(11);
	rtl8187se_rf_writereg(dev, 0x02, 0x088D); msleep(221);
	rtl8187se_rf_writereg(dev, 0x00, 0x0137); mdelay(11);
	rtl8187se_rf_writereg(dev, 0x07, 0x0000); mdelay(1);
	rtl8187se_rf_writereg(dev, 0x07, 0x0180); mdelay(1);
	rtl8187se_rf_writereg(dev, 0x07, 0x0220); mdelay(1);
	rtl8187se_rf_writereg(dev, 0x07, 0x03E0); mdelay(1);
	rtl8187se_rf_writereg(dev, 0x06, 0x00C1); mdelay(1);
	rtl8187se_rf_writereg(dev, 0x0A, 0x0001); mdelay(1);
	if (priv->xtal_cal) {
		tmp = (priv->xtal_in << 4) | (priv->xtal_out << 1) |
		      (1 << 11) | (1 << 9);
		rtl8187se_rf_writereg(dev, 0x0F, tmp);
		wiphy_info(dev->wiphy, "Xtal cal\n");
		mdelay(1);
	} else {
		wiphy_info(dev->wiphy, "NO Xtal cal\n");
		rtl8187se_rf_writereg(dev, 0x0F, 0x0ACC);
		mdelay(1);
	}
	/* page 0 */
	rtl8187se_rf_writereg(dev, 0x00, 0x00BF); mdelay(1);
	rtl8187se_rf_writereg(dev, 0x0D, 0x08DF); mdelay(1);
	rtl8187se_rf_writereg(dev, 0x02, 0x004D); mdelay(1);
	rtl8187se_rf_writereg(dev, 0x04, 0x0975); msleep(31);
	rtl8187se_rf_writereg(dev, 0x00, 0x0197); mdelay(1);
	rtl8187se_rf_writereg(dev, 0x05, 0x05AB); mdelay(1);

	rtl8187se_rf_writereg(dev, 0x00, 0x009F); mdelay(1);
	rtl8187se_rf_writereg(dev, 0x01, 0x0000); mdelay(1);
	rtl8187se_rf_writereg(dev, 0x02, 0x0000); mdelay(1);
	/* power save parameters */
	/* TODO: move to dev.c */
	rtl818x_iowrite8(priv, REG_ADDR1(0x024E),
		 rtl818x_ioread8(priv, REG_ADDR1(0x24E)) & 0x9F);
	rtl8225se_write_phy_cck(dev, 0x00, 0xC8);
	rtl8225se_write_phy_cck(dev, 0x06, 0x1C);
	rtl8225se_write_phy_cck(dev, 0x10, 0x78);
	rtl8225se_write_phy_cck(dev, 0x2E, 0xD0);
	rtl8225se_write_phy_cck(dev, 0x2F, 0x06);
	rtl8225se_write_phy_cck(dev, 0x01, 0x46);

	/* power control */
	rtl818x_iowrite8(priv, &priv->map->TX_GAIN_CCK, 0x10);
	rtl818x_iowrite8(priv, &priv->map->TX_GAIN_OFDM, 0x1B);

	rtl818x_iowrite8(priv, &priv->map->TX_ANTENNA, 0x03);
	rtl8225se_write_phy_ofdm(dev, 0x00, 0x12);

	rtl8225se_write_zebra_agc(dev);

	rtl8225se_write_phy_ofdm(dev, 0x10, 0x00);

	rtl8187se_write_ofdm_config(dev);

	/* turn on RF */
	rtl8187se_rf_writereg(dev, 0x00, 0x009F); udelay(500);
	rtl8187se_rf_writereg(dev, 0x04, 0x0972); udelay(500);
	/* turn on RF again */
	rtl8187se_rf_writereg(dev, 0x00, 0x009F); udelay(500);
	rtl8187se_rf_writereg(dev, 0x04, 0x0972); udelay(500);
	/* turn on BB */
	rtl8225se_write_phy_ofdm(dev, 0x10, 0x40);
	rtl8225se_write_phy_ofdm(dev, 0x12, 0x40);

	rtl8187se_write_initial_gain(dev, 4);
}

void rtl8225se_rf_stop(struct ieee80211_hw *dev)
{
	/* checked for 8187se */
	struct rtl8180_priv *priv = dev->priv;

	/* turn off BB RXIQ matrix to cut off rx signal */
	rtl8225se_write_phy_ofdm(dev, 0x10, 0x00);
	rtl8225se_write_phy_ofdm(dev, 0x12, 0x00);
	/* turn off RF */
	rtl8187se_rf_writereg(dev, 0x04, 0x0000);
	rtl8187se_rf_writereg(dev, 0x00, 0x0000);

	usleep_range(1000, 5000);
	/* turn off A/D and D/A */
	rtl8180_set_anaparam(priv, RTL8225SE_ANAPARAM_OFF);
	rtl8180_set_anaparam2(priv, RTL8225SE_ANAPARAM2_OFF);
}

void rtl8225se_rf_set_channel(struct ieee80211_hw *dev,
				   struct ieee80211_conf *conf)
{
	int chan =
		ieee80211_frequency_to_channel(conf->chandef.chan->center_freq);

	rtl8225sez2_rf_set_tx_power(dev, chan);
	rtl8187se_rf_writereg(dev, 0x7, rtl8225se_chan[chan - 1]);
	if ((rtl8187se_rf_readreg(dev, 0x7) & 0x0F80) !=
		rtl8225se_chan[chan - 1])
		rtl8187se_rf_writereg(dev, 0x7, rtl8225se_chan[chan - 1]);
	usleep_range(10000, 20000);
}

static const struct rtl818x_rf_ops rtl8225se_ops = {
	.name		= "rtl8225-se",
	.init		= rtl8225se_rf_init,
	.stop		= rtl8225se_rf_stop,
	.set_chan	= rtl8225se_rf_set_channel,
};

const struct rtl818x_rf_ops *rtl8187se_detect_rf(struct ieee80211_hw *dev)
{
	return &rtl8225se_ops;
}
