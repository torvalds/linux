/*
  This is part of the rtl8180-sa2400 driver
  released under the GPL (See file COPYING for details).
  Copyright (c) 2005 Andrea Merello <andreamrl@tiscali.it>

  This files contains programming code for the rtl8225
  radio frontend.

  *Many* thanks to Realtek Corp. for their great support!

*/

#include "r8180_hw.h"
#include "r8180_rtl8225.h"
#include "r8180_93cx6.h"

#include "ieee80211/dot11d.h"


static void write_rtl8225(struct net_device *dev, u8 adr, u16 data)
{
	int i;
	u16 out, select;
	u8 bit;
	u32 bangdata = (data << 4) | (adr & 0xf);
	struct r8180_priv *priv = ieee80211_priv(dev);

	out = read_nic_word(dev, RFPinsOutput) & 0xfff3;

	write_nic_word(dev, RFPinsEnable,
		(read_nic_word(dev, RFPinsEnable) | 0x7));

	select = read_nic_word(dev, RFPinsSelect);

	write_nic_word(dev, RFPinsSelect, select | 0x7 |
		((priv->card_type == USB) ? 0 : SW_CONTROL_GPIO));

	force_pci_posting(dev);
	udelay(10);

	write_nic_word(dev, RFPinsOutput, out | BB_HOST_BANG_EN);

	force_pci_posting(dev);
	udelay(2);

	write_nic_word(dev, RFPinsOutput, out);

	force_pci_posting(dev);
	udelay(10);

	for (i = 15; i >= 0; i--) {
		bit = (bangdata & (1 << i)) >> i;

		write_nic_word(dev, RFPinsOutput, bit | out);

		write_nic_word(dev, RFPinsOutput, bit | out | BB_HOST_BANG_CLK);
		write_nic_word(dev, RFPinsOutput, bit | out | BB_HOST_BANG_CLK);

		i--;
		bit = (bangdata & (1 << i)) >> i;

		write_nic_word(dev, RFPinsOutput, bit | out | BB_HOST_BANG_CLK);
		write_nic_word(dev, RFPinsOutput, bit | out | BB_HOST_BANG_CLK);

		write_nic_word(dev, RFPinsOutput, bit | out);

	}

	write_nic_word(dev, RFPinsOutput, out | BB_HOST_BANG_EN);

	force_pci_posting(dev);
	udelay(10);

	write_nic_word(dev, RFPinsOutput, out |
		((priv->card_type == USB) ? 4 : BB_HOST_BANG_EN));

	write_nic_word(dev, RFPinsSelect, select |
		((priv->card_type == USB) ? 0 : SW_CONTROL_GPIO));

	if (priv->card_type == USB)
		mdelay(2);
	else
		rtl8185_rf_pins_enable(dev);
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
	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
};

static const u8 rtl8225_gain[] = {
	0x23, 0x88, 0x7c, 0xa5,	/* -82dBm */
	0x23, 0x88, 0x7c, 0xb5,	/* -82dBm */
	0x23, 0x88, 0x7c, 0xc5,	/* -82dBm */
	0x33, 0x80, 0x79, 0xc5,	/* -78dBm */
	0x43, 0x78, 0x76, 0xc5,	/* -74dBm */
	0x53, 0x60, 0x73, 0xc5,	/* -70dBm */
	0x63, 0x58, 0x70, 0xc5,	/* -66dBm */
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
	0,
	0x0080, 0x0100, 0x0180, 0x0200, 0x0280, 0x0300, 0x0380,
	0x0400, 0x0480, 0x0500, 0x0580, 0x0600, 0x0680, 0x074A,
};

static void rtl8225_SetTXPowerLevel(struct net_device *dev, short ch)
{
	struct r8180_priv *priv = ieee80211_priv(dev);
	int GainIdx;
	int GainSetting;
	int i;
	u8 power;
	const u8 *cck_power_table;
	u8 max_cck_power_level;
	u8 max_ofdm_power_level;
	u8 min_ofdm_power_level;
	u8 cck_power_level = 0xff & priv->chtxpwr[ch];
	u8 ofdm_power_level = 0xff & priv->chtxpwr_ofdm[ch];

	if (priv->card_type == USB) {
		max_cck_power_level = 11;
		max_ofdm_power_level = 25;
		min_ofdm_power_level = 10;
	} else {
		max_cck_power_level = 35;
		max_ofdm_power_level = 35;
		min_ofdm_power_level = 0;
	}

	if (cck_power_level > max_cck_power_level)
		cck_power_level = max_cck_power_level;

	GainIdx = cck_power_level % 6;
	GainSetting = cck_power_level / 6;

	if (ch == 14)
		cck_power_table = rtl8225_tx_power_cck_ch14;
	else
		cck_power_table = rtl8225_tx_power_cck;

	write_nic_byte(dev, TX_GAIN_CCK,
		       rtl8225_tx_gain_cck_ofdm[GainSetting] >> 1);

	for (i = 0; i < 8; i++) {
		power = cck_power_table[GainIdx * 8 + i];
		write_phy_cck(dev, 0x44 + i, power);
	}

	/* FIXME Is this delay really needeed ? */
	force_pci_posting(dev);
	mdelay(1);

	if (ofdm_power_level > (max_ofdm_power_level - min_ofdm_power_level))
		ofdm_power_level = max_ofdm_power_level;
	else
		ofdm_power_level += min_ofdm_power_level;

	if (ofdm_power_level > 35)
		ofdm_power_level = 35;

	GainIdx = ofdm_power_level % 6;
	GainSetting = ofdm_power_level / 6;

	rtl8185_set_anaparam2(dev, RTL8225_ANAPARAM2_ON);

	write_phy_ofdm(dev, 2, 0x42);
	write_phy_ofdm(dev, 6, 0x00);
	write_phy_ofdm(dev, 8, 0x00);

	write_nic_byte(dev, TX_GAIN_OFDM,
		       rtl8225_tx_gain_cck_ofdm[GainSetting] >> 1);

	power = rtl8225_tx_power_ofdm[GainIdx];

	write_phy_ofdm(dev, 5, power);
	write_phy_ofdm(dev, 7, power);

	force_pci_posting(dev);
	mdelay(1);
}

static const u8 rtl8225z2_threshold[] = {
        0x8d, 0x8d, 0x8d, 0x8d, 0x9d, 0xad, 0xbd,
};

static const u8 rtl8225z2_gain_bg[] = {
	0x23, 0x15, 0xa5, /* -82-1dBm */
	0x23, 0x15, 0xb5, /* -82-2dBm */
	0x23, 0x15, 0xc5, /* -82-3dBm */
	0x33, 0x15, 0xc5, /* -78dBm */
	0x43, 0x15, 0xc5, /* -74dBm */
	0x53, 0x15, 0xc5, /* -70dBm */
	0x63, 0x15, 0xc5, /* -66dBm */
};

static const u8 rtl8225z2_gain_a[] = {
	0x13, 0x27, 0x5a, /* -82dBm */
	0x23, 0x23, 0x58, /* -82dBm */
	0x33, 0x1f, 0x56, /* -82dBm */
	0x43, 0x1b, 0x54, /* -78dBm */
	0x53, 0x17, 0x51, /* -74dBm */
	0x63, 0x24, 0x4f, /* -70dBm */
	0x73, 0x0f, 0x4c, /* -66dBm */
};

static const u16 rtl8225z2_rxgain[] = {
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
	0x03aa, 0x03ab, 0x03ac, 0x03ad, 0x03b0, 0x03b1, 0x03b2, 0x03b3,
	0x03b4, 0x03b5, 0x03b8, 0x03b9, 0x03ba, 0x03bb, 0x03bb

};

static const u8 ZEBRA2_CCK_OFDM_GAIN_SETTING[] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
	0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b,
	0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11,
	0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
	0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d,
	0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23,
};

static const u8 rtl8225z2_tx_power_ofdm[] = {
	0x42, 0x00, 0x40, 0x00, 0x40
};

static const u8 rtl8225z2_tx_power_cck_ch14[] = {
	0x36, 0x35, 0x2e, 0x1b, 0x00, 0x00, 0x00, 0x00
};

static const u8 rtl8225z2_tx_power_cck[] = {
	0x36, 0x35, 0x2e, 0x25, 0x1c, 0x12, 0x09, 0x04
};

void rtl8225z2_set_gain(struct net_device *dev, short gain)
{
	const u8 *rtl8225_gain;
	struct r8180_priv *priv = ieee80211_priv(dev);
	u8 mode = priv->ieee80211->mode;

	if (mode == IEEE_B || mode == IEEE_G)
		rtl8225_gain = rtl8225z2_gain_bg;
	else
		rtl8225_gain = rtl8225z2_gain_a;

	write_phy_ofdm(dev, 0x0b, rtl8225_gain[gain * 3]);
	write_phy_ofdm(dev, 0x1b, rtl8225_gain[gain * 3 + 1]);
	write_phy_ofdm(dev, 0x1d, rtl8225_gain[gain * 3 + 2]);
	write_phy_ofdm(dev, 0x21, 0x37);
}

static u32 read_rtl8225(struct net_device *dev, u8 adr)
{
	u32 data2Write = ((u32)(adr & 0x1f)) << 27;
	u32 dataRead;
	u32 mask;
	u16 oval,oval2,oval3,tmp;
	int i;
	short bit, rw;
	u8 wLength = 6;
	u8 rLength = 12;
	u8 low2high = 0;

	oval = read_nic_word(dev, RFPinsOutput);
	oval2 = read_nic_word(dev, RFPinsEnable);
	oval3 = read_nic_word(dev, RFPinsSelect);

	write_nic_word(dev, RFPinsEnable, (oval2|0xf));
	write_nic_word(dev, RFPinsSelect, (oval3|0xf));

	dataRead = 0;

	oval &= ~0xf;

	write_nic_word(dev, RFPinsOutput, oval | BB_HOST_BANG_EN ); udelay(4);

	write_nic_word(dev, RFPinsOutput, oval ); udelay(5);

	rw = 0;

	mask = (low2high) ? 0x01 : (((u32)0x01)<<(32-1));

	for (i = 0; i < wLength/2; i++) {
		bit = ((data2Write&mask) != 0) ? 1 : 0;
		write_nic_word(dev, RFPinsOutput, bit|oval | rw); udelay(1);

		write_nic_word(dev, RFPinsOutput, bit|oval | BB_HOST_BANG_CLK | rw); udelay(2);
		write_nic_word(dev, RFPinsOutput, bit|oval | BB_HOST_BANG_CLK | rw); udelay(2);

		mask = (low2high) ? (mask<<1): (mask>>1);

		if (i == 2) {
			rw = BB_HOST_BANG_RW;
			write_nic_word(dev, RFPinsOutput, bit|oval | BB_HOST_BANG_CLK | rw); udelay(2);
			write_nic_word(dev, RFPinsOutput, bit|oval | rw); udelay(2);
			break;
		}

		bit = ((data2Write&mask) != 0) ? 1: 0;

		write_nic_word(dev, RFPinsOutput, oval|bit|rw| BB_HOST_BANG_CLK); udelay(2);
		write_nic_word(dev, RFPinsOutput, oval|bit|rw| BB_HOST_BANG_CLK); udelay(2);

		write_nic_word(dev, RFPinsOutput, oval| bit |rw); udelay(1);

		mask = (low2high) ? (mask<<1) : (mask>>1);
	}

	write_nic_word(dev, RFPinsOutput, rw|oval); udelay(2);
	mask = (low2high) ? 0x01 : (((u32)0x01) << (12-1));

	/*
	 * We must set data pin to HW controled, otherwise RF can't driver it
	 * and value RF register won't be able to read back properly.
	 */
	write_nic_word(dev, RFPinsEnable, (oval2 & (~0x01)));

	for (i = 0; i < rLength; i++) {
		write_nic_word(dev, RFPinsOutput, rw|oval); udelay(1);

		write_nic_word(dev, RFPinsOutput, rw|oval|BB_HOST_BANG_CLK); udelay(2);
		write_nic_word(dev, RFPinsOutput, rw|oval|BB_HOST_BANG_CLK); udelay(2);
		write_nic_word(dev, RFPinsOutput, rw|oval|BB_HOST_BANG_CLK); udelay(2);
		tmp = read_nic_word(dev, RFPinsInput);

		dataRead |= (tmp & BB_HOST_BANG_CLK ? mask : 0);

		write_nic_word(dev, RFPinsOutput, (rw|oval)); udelay(2);

		mask = (low2high) ? (mask<<1) : (mask>>1);
	}

	write_nic_word(dev, RFPinsOutput, BB_HOST_BANG_EN|BB_HOST_BANG_RW|oval); udelay(2);

	write_nic_word(dev, RFPinsEnable, oval2);
	write_nic_word(dev, RFPinsSelect, oval3); /* Set To SW Switch */
	write_nic_word(dev, RFPinsOutput, 0x3a0);

	return dataRead;
}

short rtl8225_is_V_z2(struct net_device *dev)
{
	short vz2 = 1;

	if (read_rtl8225(dev, 8) != 0x588)
		vz2 = 0;
	else	/* reg 9 pg 1 = 24 */
		if (read_rtl8225(dev, 9) != 0x700)
			vz2 = 0;

	/* sw back to pg 0 */
	write_rtl8225(dev, 0, 0xb7);

	return vz2;
}

void rtl8225z2_rf_close(struct net_device *dev)
{
	RF_WriteReg(dev, 0x4, 0x1f);

	force_pci_posting(dev);
	mdelay(1);

	rtl8180_set_anaparam(dev, RTL8225z2_ANAPARAM_OFF);
	rtl8185_set_anaparam2(dev, RTL8225z2_ANAPARAM2_OFF);
}

/*
 * Map dBm into Tx power index according to current HW model, for example,
 * RF and PA, and current wireless mode.
 */
s8 DbmToTxPwrIdx(struct r8180_priv *priv, WIRELESS_MODE WirelessMode,
		 s32 PowerInDbm)
{
 	bool bUseDefault = true;
	s8 TxPwrIdx = 0;

	/*
	 * OFDM Power in dBm = Index * 0.5 + 0
	 * CCK Power in dBm = Index * 0.25 + 13
	 */
	if (priv->card_8185 >= VERSION_8187S_B) {
		s32 tmp = 0;

		if (WirelessMode == WIRELESS_MODE_G) {
			bUseDefault = false;
			tmp = (2 * PowerInDbm);

			if (tmp < 0)
				TxPwrIdx = 0;
			else if (tmp > 40) /* 40 means 20 dBm. */
				TxPwrIdx = 40;
			else
				TxPwrIdx = (s8)tmp;
		} else if (WirelessMode == WIRELESS_MODE_B) {
			bUseDefault = false;
			tmp = (4 * PowerInDbm) - 52;

			if(tmp < 0)
				TxPwrIdx = 0;
			else if (tmp > 28) /* 28 means 20 dBm. */
				TxPwrIdx = 28;
			else
				TxPwrIdx = (s8)tmp;
		}
	}

	/*
	 * TRUE if we want to use a default implementation.
	 * We shall set it to FALSE when we have exact translation formular
	 * for target IC. 070622, by rcnjko.
	 */
	if (bUseDefault) {
		if (PowerInDbm < 0)
			TxPwrIdx = 0;
		else if (PowerInDbm > 35)
			TxPwrIdx = 35;
		else
			TxPwrIdx = (u8)PowerInDbm;
	}

	return TxPwrIdx;
}

void rtl8225z2_SetTXPowerLevel(struct net_device *dev, short ch)
{
	struct r8180_priv *priv = ieee80211_priv(dev);
	u8 max_cck_power_level;
	u8 max_ofdm_power_level;
	u8 min_ofdm_power_level;
	char cck_power_level = (char)(0xff & priv->chtxpwr[ch]);
	char ofdm_power_level = (char)(0xff & priv->chtxpwr_ofdm[ch]);

	if (IS_DOT11D_ENABLE(priv->ieee80211) &&
	    IS_DOT11D_STATE_DONE(priv->ieee80211)) {
		u8 MaxTxPwrInDbm = DOT11D_GetMaxTxPwrInDbm(priv->ieee80211, ch);
		u8 CckMaxPwrIdx = DbmToTxPwrIdx(priv, WIRELESS_MODE_B, MaxTxPwrInDbm);
		u8 OfdmMaxPwrIdx = DbmToTxPwrIdx(priv, WIRELESS_MODE_G, MaxTxPwrInDbm);

		if (cck_power_level > CckMaxPwrIdx)
			cck_power_level = CckMaxPwrIdx;
		if (ofdm_power_level > OfdmMaxPwrIdx)
			ofdm_power_level = OfdmMaxPwrIdx;
	}

	max_cck_power_level = 15;
	max_ofdm_power_level = 25;
	min_ofdm_power_level = 10;

	if (cck_power_level > 35)
		cck_power_level = 35;

	write_nic_byte(dev, CCK_TXAGC,
		       (ZEBRA2_CCK_OFDM_GAIN_SETTING[(u8)cck_power_level]));
	force_pci_posting(dev);
	mdelay(1);

	if (ofdm_power_level > 35)
		ofdm_power_level = 35;

	if (priv->up == 0) {
		write_phy_ofdm(dev, 2, 0x42);
		write_phy_ofdm(dev, 5, 0x00);
		write_phy_ofdm(dev, 6, 0x40);
		write_phy_ofdm(dev, 7, 0x00);
		write_phy_ofdm(dev, 8, 0x40);
	}

	write_nic_byte(dev, OFDM_TXAGC,
		       ZEBRA2_CCK_OFDM_GAIN_SETTING[(u8)ofdm_power_level]);

	if (ofdm_power_level <= 11) {
		write_phy_ofdm(dev, 0x07, 0x5c);
		write_phy_ofdm(dev, 0x09, 0x5c);
        }

	if (ofdm_power_level <= 17) {
		write_phy_ofdm(dev, 0x07, 0x54);
		write_phy_ofdm(dev, 0x09, 0x54);
	} else {
		write_phy_ofdm(dev, 0x07, 0x50);
		write_phy_ofdm(dev, 0x09, 0x50);
	}

	force_pci_posting(dev);
	mdelay(1);
}

void rtl8225z2_rf_set_chan(struct net_device *dev, short ch)
{
	rtl8225z2_SetTXPowerLevel(dev, ch);

	RF_WriteReg(dev, 0x7, rtl8225_chan[ch]);

	if ((RF_ReadReg(dev, 0x7) & 0x0F80) != rtl8225_chan[ch])
		RF_WriteReg(dev, 0x7, rtl8225_chan[ch]);

	mdelay(1);

	force_pci_posting(dev);
	mdelay(10);
}

static void rtl8225_host_pci_init(struct net_device *dev)
{
	write_nic_word(dev, RFPinsOutput, 0x480);

	rtl8185_rf_pins_enable(dev);

	write_nic_word(dev, RFPinsSelect, 0x88 | SW_CONTROL_GPIO);

	write_nic_byte(dev, GP_ENABLE, 0);

	force_pci_posting(dev);
	mdelay(200);

	/* bit 6 is for RF on/off detection */
	write_nic_word(dev, GP_ENABLE, 0xff & (~(1 << 6)));
}

static void rtl8225_rf_set_chan(struct net_device *dev, short ch)
{
	struct r8180_priv *priv = ieee80211_priv(dev);
	short gset = (priv->ieee80211->state == IEEE80211_LINKED &&
		ieee80211_is_54g(priv->ieee80211->current_network)) ||
		priv->ieee80211->iw_mode == IW_MODE_MONITOR;

	rtl8225_SetTXPowerLevel(dev, ch);

	write_rtl8225(dev, 0x7, rtl8225_chan[ch]);

	force_pci_posting(dev);
	mdelay(10);

	if (gset) {
		write_nic_byte(dev, SIFS, 0x22);
		write_nic_byte(dev, DIFS, 0x14);
	} else {
		write_nic_byte(dev, SIFS, 0x44);
		write_nic_byte(dev, DIFS, 0x24);
	}

	if (priv->ieee80211->state == IEEE80211_LINKED &&
	    ieee80211_is_shortslot(priv->ieee80211->current_network))
		write_nic_byte(dev, SLOT, 0x9);
	else
		write_nic_byte(dev, SLOT, 0x14);

	if (gset) {
		write_nic_byte(dev, EIFS, 81);
		write_nic_byte(dev, CW_VAL, 0x73);
	} else {
		write_nic_byte(dev, EIFS, 81);
		write_nic_byte(dev, CW_VAL, 0xa5);
	}
}

void rtl8225z2_rf_init(struct net_device *dev)
{
	struct r8180_priv *priv = ieee80211_priv(dev);
	int i;
	short channel = 1;
	u16	brsr;
	u32	data,addr;

	priv->chan = channel;

	if (priv->card_type != USB)
		rtl8225_host_pci_init(dev);

	write_nic_dword(dev, RF_TIMING, 0x000a8008);

	brsr = read_nic_word(dev, BRSR);

	write_nic_word(dev, BRSR, 0xffff);

	write_nic_dword(dev, RF_PARA, 0x100044);

	rtl8180_set_mode(dev, EPROM_CMD_CONFIG);
	write_nic_byte(dev, CONFIG3, 0x44);
	rtl8180_set_mode(dev, EPROM_CMD_NORMAL);

	rtl8185_rf_pins_enable(dev);

	write_rtl8225(dev, 0x0, 0x2bf); mdelay(1);
	write_rtl8225(dev, 0x1, 0xee0); mdelay(1);
	write_rtl8225(dev, 0x2, 0x44d); mdelay(1);
	write_rtl8225(dev, 0x3, 0x441); mdelay(1);
	write_rtl8225(dev, 0x4, 0x8c3); mdelay(1);
	write_rtl8225(dev, 0x5, 0xc72); mdelay(1);
	write_rtl8225(dev, 0x6, 0xe6);  mdelay(1);
	write_rtl8225(dev, 0x7, ((priv->card_type == USB)? 0x82a : rtl8225_chan[channel]));  mdelay(1);
	write_rtl8225(dev, 0x8, 0x3f);  mdelay(1);
	write_rtl8225(dev, 0x9, 0x335); mdelay(1);
	write_rtl8225(dev, 0xa, 0x9d4); mdelay(1);
	write_rtl8225(dev, 0xb, 0x7bb); mdelay(1);
	write_rtl8225(dev, 0xc, 0x850); mdelay(1);
	write_rtl8225(dev, 0xd, 0xcdf); mdelay(1);
	write_rtl8225(dev, 0xe, 0x2b);  mdelay(1);
	write_rtl8225(dev, 0xf, 0x114);

	mdelay(100);

	write_rtl8225(dev, 0x0, 0x1b7);

	for (i = 0; i < 95; i++) {
		write_rtl8225(dev, 0x1, (u8)(i + 1));
		write_rtl8225(dev, 0x2, rtl8225z2_rxgain[i]);
	}

	write_rtl8225(dev, 0x3, 0x80);
	write_rtl8225(dev, 0x5, 0x4);

	write_rtl8225(dev, 0x0, 0xb7);

	write_rtl8225(dev, 0x2, 0xc4d);

	if (priv->card_type == USB) {
		mdelay(200);

		write_rtl8225(dev, 0x2, 0x44d);
		mdelay(100);
	}

	/* FIXME!! rtl8187 we have to check if calibrarion
	 * is successful and eventually cal. again (repeat
	 * the two write on reg 2)
	 */
	data = read_rtl8225(dev, 6);
	if (!(data & 0x00000080)) {
		write_rtl8225(dev, 0x02, 0x0c4d);
		force_pci_posting(dev); mdelay(200);
		write_rtl8225(dev, 0x02, 0x044d);
		force_pci_posting(dev); mdelay(100);
		data = read_rtl8225(dev, 6);
		if (!(data & 0x00000080))
			DMESGW("RF Calibration Failed!!!!\n");
	}

	mdelay(200);

	write_rtl8225(dev, 0x0, 0x2bf);

	if (priv->card_type != USB)
		rtl8185_rf_pins_enable(dev);

	for (i = 0; i < 128; i++) {
		data = rtl8225_agc[i];

		addr = i + 0x80; /* enable writing AGC table */
		write_phy_ofdm(dev, 0xb, data);
		mdelay(1);

		write_phy_ofdm(dev, 0xa, addr);
		mdelay(1);
	}

	force_pci_posting(dev);
	mdelay(1);

	write_phy_ofdm(dev, 0x00, 0x01); mdelay(1);
	write_phy_ofdm(dev, 0x01, 0x02); mdelay(1);
	write_phy_ofdm(dev, 0x02, ((priv->card_type == USB) ? 0x42 : 0x62)); mdelay(1);
	write_phy_ofdm(dev, 0x03, 0x00); mdelay(1);
	write_phy_ofdm(dev, 0x04, 0x00); mdelay(1);
	write_phy_ofdm(dev, 0x05, 0x00); mdelay(1);
	write_phy_ofdm(dev, 0x06, 0x40); mdelay(1);
	write_phy_ofdm(dev, 0x07, 0x00); mdelay(1);
	write_phy_ofdm(dev, 0x08, 0x40); mdelay(1);
	write_phy_ofdm(dev, 0x09, 0xfe); mdelay(1);
	write_phy_ofdm(dev, 0x0a, 0x08); mdelay(1);
	write_phy_ofdm(dev, 0x0b, 0x80); mdelay(1);
	write_phy_ofdm(dev, 0x0c, 0x01); mdelay(1);
	write_phy_ofdm(dev, 0x0d, 0x43);
	write_phy_ofdm(dev, 0x0e, 0xd3); mdelay(1);
	write_phy_ofdm(dev, 0x0f, 0x38); mdelay(1);
	write_phy_ofdm(dev, 0x10, 0x84); mdelay(1);
	write_phy_ofdm(dev, 0x11, 0x07); mdelay(1);
	write_phy_ofdm(dev, 0x12, 0x20); mdelay(1);
	write_phy_ofdm(dev, 0x13, 0x20); mdelay(1);
	write_phy_ofdm(dev, 0x14, 0x00); mdelay(1);
	write_phy_ofdm(dev, 0x15, 0x40); mdelay(1);
	write_phy_ofdm(dev, 0x16, 0x00); mdelay(1);
	write_phy_ofdm(dev, 0x17, 0x40); mdelay(1);
	write_phy_ofdm(dev, 0x18, 0xef); mdelay(1);
	write_phy_ofdm(dev, 0x19, 0x19); mdelay(1);
	write_phy_ofdm(dev, 0x1a, 0x20); mdelay(1);
	write_phy_ofdm(dev, 0x1b, 0x15); mdelay(1);
	write_phy_ofdm(dev, 0x1c, 0x04); mdelay(1);
	write_phy_ofdm(dev, 0x1d, 0xc5); mdelay(1);
	write_phy_ofdm(dev, 0x1e, 0x95); mdelay(1);
	write_phy_ofdm(dev, 0x1f, 0x75); mdelay(1);
	write_phy_ofdm(dev, 0x20, 0x1f); mdelay(1);
	write_phy_ofdm(dev, 0x21, 0x17); mdelay(1);
	write_phy_ofdm(dev, 0x22, 0x16); mdelay(1);
	write_phy_ofdm(dev, 0x23, 0x80); mdelay(1); /* FIXME maybe not needed */
	write_phy_ofdm(dev, 0x24, 0x46); mdelay(1);
	write_phy_ofdm(dev, 0x25, 0x00); mdelay(1);
	write_phy_ofdm(dev, 0x26, 0x90); mdelay(1);
	write_phy_ofdm(dev, 0x27, 0x88); mdelay(1);

	rtl8225z2_set_gain(dev,4);

	write_phy_cck(dev, 0x0, 0x98); mdelay(1);
	write_phy_cck(dev, 0x3, 0x20); mdelay(1);
	write_phy_cck(dev, 0x4, 0x7e); mdelay(1);
	write_phy_cck(dev, 0x5, 0x12); mdelay(1);
	write_phy_cck(dev, 0x6, 0xfc); mdelay(1);
	write_phy_cck(dev, 0x7, 0x78); mdelay(1);
	write_phy_cck(dev, 0x8, 0x2e); mdelay(1);
	write_phy_cck(dev, 0x10, ((priv->card_type == USB) ? 0x9b: 0x93)); mdelay(1);
	write_phy_cck(dev, 0x11, 0x88); mdelay(1);
	write_phy_cck(dev, 0x12, 0x47); mdelay(1);
	write_phy_cck(dev, 0x13, 0xd0);
	write_phy_cck(dev, 0x19, 0x00);
	write_phy_cck(dev, 0x1a, 0xa0);
	write_phy_cck(dev, 0x1b, 0x08);
	write_phy_cck(dev, 0x40, 0x86); /* CCK Carrier Sense Threshold */
	write_phy_cck(dev, 0x41, 0x8d); mdelay(1);
	write_phy_cck(dev, 0x42, 0x15); mdelay(1);
	write_phy_cck(dev, 0x43, 0x18); mdelay(1);
	write_phy_cck(dev, 0x44, 0x36); mdelay(1);
	write_phy_cck(dev, 0x45, 0x35); mdelay(1);
	write_phy_cck(dev, 0x46, 0x2e); mdelay(1);
	write_phy_cck(dev, 0x47, 0x25); mdelay(1);
	write_phy_cck(dev, 0x48, 0x1c); mdelay(1);
	write_phy_cck(dev, 0x49, 0x12); mdelay(1);
	write_phy_cck(dev, 0x4a, 0x09); mdelay(1);
	write_phy_cck(dev, 0x4b, 0x04); mdelay(1);
	write_phy_cck(dev, 0x4c, 0x05); mdelay(1);

	write_nic_byte(dev, 0x5b, 0x0d); mdelay(1);

	rtl8225z2_SetTXPowerLevel(dev, channel);

	/* RX antenna default to A */
	write_phy_cck(dev, 0x11, 0x9b); mdelay(1);		/* B: 0xDB */
	write_phy_ofdm(dev, 0x26, 0x90); mdelay(1);		/* B: 0x10 */

	rtl8185_tx_antenna(dev, 0x03);				/* B: 0x00 */

	/* switch to high-speed 3-wire
	 * last digit. 2 for both cck and ofdm
	 */
	if (priv->card_type == USB)
		write_nic_dword(dev, 0x94, 0x3dc00002);
	else {
		write_nic_dword(dev, 0x94, 0x15c00002);
		rtl8185_rf_pins_enable(dev);
	}

	rtl8225_rf_set_chan(dev, priv->chan);
}

void rtl8225z2_rf_set_mode(struct net_device *dev)
{
	struct r8180_priv *priv = ieee80211_priv(dev);

	if (priv->ieee80211->mode == IEEE_A) {
		write_rtl8225(dev, 0x5, 0x1865);
		write_nic_dword(dev, RF_PARA, 0x10084);
		write_nic_dword(dev, RF_TIMING, 0xa8008);
		write_phy_ofdm(dev, 0x0, 0x0);
		write_phy_ofdm(dev, 0xa, 0x6);
		write_phy_ofdm(dev, 0xb, 0x99);
		write_phy_ofdm(dev, 0xf, 0x20);
		write_phy_ofdm(dev, 0x11, 0x7);

		rtl8225z2_set_gain(dev,4);

		write_phy_ofdm(dev,0x15, 0x40);
		write_phy_ofdm(dev,0x17, 0x40);

		write_nic_dword(dev, 0x94,0x10000000);
	} else {
		write_rtl8225(dev, 0x5, 0x1864);
		write_nic_dword(dev, RF_PARA, 0x10044);
		write_nic_dword(dev, RF_TIMING, 0xa8008);
		write_phy_ofdm(dev, 0x0, 0x1);
		write_phy_ofdm(dev, 0xa, 0x6);
		write_phy_ofdm(dev, 0xb, 0x99);
		write_phy_ofdm(dev, 0xf, 0x20);
		write_phy_ofdm(dev, 0x11, 0x7);

		rtl8225z2_set_gain(dev,4);

		write_phy_ofdm(dev,0x15, 0x40);
		write_phy_ofdm(dev,0x17, 0x40);

		write_nic_dword(dev, 0x94,0x04000002);
	}
}

#define MAX_DOZE_WAITING_TIMES_85B 		20
#define MAX_POLLING_24F_TIMES_87SE 		10
#define LPS_MAX_SLEEP_WAITING_TIMES_87SE 	5

bool SetZebraRFPowerState8185(struct net_device *dev,
			      RT_RF_POWER_STATE eRFPowerState)
{
	struct r8180_priv *priv = ieee80211_priv(dev);
	u8			btCR9346, btConfig3;
	bool bActionAllowed = true, bTurnOffBB = true;
	u8			u1bTmp;
	int			i;
	bool		bResult = true;
	u8			QueueID;

	if (priv->SetRFPowerStateInProgress == true)
		return false;

	priv->SetRFPowerStateInProgress = true;

	btCR9346 = read_nic_byte(dev, CR9346);
	write_nic_byte(dev, CR9346, (btCR9346 | 0xC0));

	btConfig3 = read_nic_byte(dev, CONFIG3);
	write_nic_byte(dev, CONFIG3, (btConfig3 | CONFIG3_PARM_En));

	switch (priv->rf_chip) {
	case RF_ZEBRA2:
		switch (eRFPowerState) {
		case eRfOn:
			RF_WriteReg(dev,0x4,0x9FF);

			write_nic_dword(dev, ANAPARAM, ANAPARM_ON);
			write_nic_dword(dev, ANAPARAM2, ANAPARM2_ON);

			write_nic_byte(dev, CONFIG4, priv->RFProgType);

			/* turn on CCK and OFDM */
			u1bTmp = read_nic_byte(dev, 0x24E);
			write_nic_byte(dev, 0x24E, (u1bTmp & (~(BIT5 | BIT6))));
			break;
		case eRfSleep:
			break;
		case eRfOff:
			break;
		default:
			bResult = false;
			break;
		}
		break;
	case RF_ZEBRA4:
		switch (eRFPowerState) {
		case eRfOn:
			write_nic_word(dev, 0x37C, 0x00EC);

			/* turn on AFE */
			write_nic_byte(dev, 0x54, 0x00);
			write_nic_byte(dev, 0x62, 0x00);

			/* turn on RF */
			RF_WriteReg(dev, 0x0, 0x009f); udelay(500);
			RF_WriteReg(dev, 0x4, 0x0972); udelay(500);

			/* turn on RF again */
			RF_WriteReg(dev, 0x0, 0x009f); udelay(500);
			RF_WriteReg(dev, 0x4, 0x0972); udelay(500);

			/* turn on BB */
			write_phy_ofdm(dev,0x10,0x40);
			write_phy_ofdm(dev,0x12,0x40);

			/* Avoid power down at init time. */
			write_nic_byte(dev, CONFIG4, priv->RFProgType);

			u1bTmp = read_nic_byte(dev, 0x24E);
			write_nic_byte(dev, 0x24E, (u1bTmp & (~(BIT5 | BIT6))));
			break;
		case eRfSleep:
			for (QueueID = 0, i = 0; QueueID < 6;) {
				if (get_curr_tx_free_desc(dev, QueueID) == priv->txringcount) {
					QueueID++;
					continue;
				} else {
					priv->TxPollingTimes ++;
					if (priv->TxPollingTimes >= LPS_MAX_SLEEP_WAITING_TIMES_87SE) {
						bActionAllowed = false;
						break;
					} else
						udelay(10);
				}
			}

			if (bActionAllowed) {
				/* turn off BB RXIQ matrix to cut off rx signal */
				write_phy_ofdm(dev, 0x10, 0x00);
				write_phy_ofdm(dev, 0x12, 0x00);

				/* turn off RF */
				RF_WriteReg(dev, 0x4, 0x0000);
				RF_WriteReg(dev, 0x0, 0x0000);

				/* turn off AFE except PLL */
				write_nic_byte(dev, 0x62, 0xff);
				write_nic_byte(dev, 0x54, 0xec);

				mdelay(1);

				{
					int i = 0;
					while (true) {
						u8 tmp24F = read_nic_byte(dev, 0x24f);

						if ((tmp24F == 0x01) || (tmp24F == 0x09)) {
							bTurnOffBB = true;
							break;
						} else {
							udelay(10);
							i++;
							priv->TxPollingTimes++;

							if (priv->TxPollingTimes >= LPS_MAX_SLEEP_WAITING_TIMES_87SE) {
								bTurnOffBB = false;
								break;
							} else
								udelay(10);
						}
					}
				}

				if (bTurnOffBB) {
					/* turn off BB */
					u1bTmp = read_nic_byte(dev, 0x24E);
					write_nic_byte(dev, 0x24E, (u1bTmp | BIT5 | BIT6));

					/* turn off AFE PLL */
					write_nic_byte(dev, 0x54, 0xFC);
					write_nic_word(dev, 0x37C, 0x00FC);
				}
			}
			break;
		case eRfOff:
			for (QueueID = 0, i = 0; QueueID < 6;) {
				if (get_curr_tx_free_desc(dev, QueueID) == priv->txringcount) {
					QueueID++;
					continue;
				} else {
					udelay(10);
					i++;
				}

				if (i >= MAX_DOZE_WAITING_TIMES_85B)
					break;
			}

			/* turn off BB RXIQ matrix to cut off rx signal */
			write_phy_ofdm(dev, 0x10, 0x00);
			write_phy_ofdm(dev, 0x12, 0x00);

			/* turn off RF */
			RF_WriteReg(dev, 0x4, 0x0000);
			RF_WriteReg(dev, 0x0, 0x0000);

			/* turn off AFE except PLL */
			write_nic_byte(dev, 0x62, 0xff);
			write_nic_byte(dev, 0x54, 0xec);

			mdelay(1);

			{
				int i = 0;

				while (true)
				{
					u8 tmp24F = read_nic_byte(dev, 0x24f);

					if ((tmp24F == 0x01) || (tmp24F == 0x09)) {
						bTurnOffBB = true;
						break;
					} else {
						bTurnOffBB = false;
						udelay(10);
						i++;
					}

					if (i > MAX_POLLING_24F_TIMES_87SE)
						break;
				}
			}

			if (bTurnOffBB) {
				/* turn off BB */
				u1bTmp = read_nic_byte(dev, 0x24E);
				write_nic_byte(dev, 0x24E, (u1bTmp | BIT5 | BIT6));

				/* turn off AFE PLL (80M) */
				write_nic_byte(dev, 0x54, 0xFC);
				write_nic_word(dev, 0x37C, 0x00FC);
			}
			break;
		default:
			bResult = false;
			printk("SetZebraRFPowerState8185(): unknow state to set: 0x%X!!!\n", eRFPowerState);
			break;
		}
		break;
	}

	btConfig3 &= ~(CONFIG3_PARM_En);
	write_nic_byte(dev, CONFIG3, btConfig3);

	btCR9346 &= ~(0xC0);
	write_nic_byte(dev, CR9346, btCR9346);

	if (bResult && bActionAllowed)
		priv->eRFPowerState = eRFPowerState;

	priv->SetRFPowerStateInProgress = false;

	return bResult && bActionAllowed;
}

void rtl8225z4_rf_sleep(struct net_device *dev)
{
	MgntActSet_RF_State(dev, eRfSleep, RF_CHANGE_BY_PS);
}

void rtl8225z4_rf_wakeup(struct net_device *dev)
{
	MgntActSet_RF_State(dev, eRfOn, RF_CHANGE_BY_PS);
}
