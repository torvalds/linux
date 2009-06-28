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


u8 rtl8225_gain[] = {
	0x23, 0x88, 0x7c, 0xa5,	/* -82dBm */
	0x23, 0x88, 0x7c, 0xb5,	/* -82dBm */
	0x23, 0x88, 0x7c, 0xc5,	/* -82dBm */
	0x33, 0x80, 0x79, 0xc5,	/* -78dBm */
	0x43, 0x78, 0x76, 0xc5,	/* -74dBm */
	0x53, 0x60, 0x73, 0xc5,	/* -70dBm */
	0x63, 0x58, 0x70, 0xc5,	/* -66dBm */
};

u32 rtl8225_chan[] = {
	0,
	0x0080, 0x0100, 0x0180, 0x0200, 0x0280, 0x0300, 0x0380,
	0x0400, 0x0480, 0x0500, 0x0580, 0x0600, 0x0680, 0x074A,
};

u16 rtl8225bcd_rxgain[] = {
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

u8 rtl8225_agc[] = {
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

u8 rtl8225_tx_gain_cck_ofdm[] = {
	0x02, 0x06, 0x0e, 0x1e, 0x3e, 0x7e
};

u8 rtl8225_tx_power_ofdm[] = {
	0x80, 0x90, 0xa2, 0xb5, 0xcb, 0xe4
};

u8 rtl8225_tx_power_cck_ch14[] = {
	0x18, 0x17, 0x15, 0x0c, 0x00, 0x00, 0x00, 0x00,
	0x1b, 0x1a, 0x17, 0x0e, 0x00, 0x00, 0x00, 0x00,
	0x1f, 0x1e, 0x1a, 0x0f, 0x00, 0x00, 0x00, 0x00,
	0x22, 0x21, 0x1d, 0x11, 0x00, 0x00, 0x00, 0x00,
	0x26, 0x25, 0x21, 0x13, 0x00, 0x00, 0x00, 0x00,
	0x2b, 0x2a, 0x25, 0x15, 0x00, 0x00, 0x00, 0x00
};

u8 rtl8225_tx_power_cck[] = {
	0x18, 0x17, 0x15, 0x11, 0x0c, 0x08, 0x04, 0x02,
	0x1b, 0x1a, 0x17, 0x13, 0x0e, 0x09, 0x04, 0x02,
	0x1f, 0x1e, 0x1a, 0x15, 0x10, 0x0a, 0x05, 0x02,
	0x22, 0x21, 0x1d, 0x18, 0x11, 0x0b, 0x06, 0x02,
	0x26, 0x25, 0x21, 0x1b, 0x14, 0x0d, 0x06, 0x03,
	0x2b, 0x2a, 0x25, 0x1e, 0x16, 0x0e, 0x07, 0x03
};

void rtl8225_set_gain(struct net_device *dev, short gain)
{
	write_phy_ofdm(dev, 0x0d, rtl8225_gain[gain * 4]);
	write_phy_ofdm(dev, 0x23, rtl8225_gain[gain * 4 + 1]);
	write_phy_ofdm(dev, 0x1b, rtl8225_gain[gain * 4 + 2]);
	write_phy_ofdm(dev, 0x1d, rtl8225_gain[gain * 4 + 3]);
}

void write_rtl8225(struct net_device *dev, u8 adr, u16 data)
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

void rtl8225_SetTXPowerLevel(struct net_device *dev, short ch)
{
	struct r8180_priv *priv = ieee80211_priv(dev);
	int GainIdx;
	int GainSetting;
	int i;
	u8 power;
	u8 *cck_power_table;
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

void rtl8225_rf_set_chan(struct net_device *dev, short ch)
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

void rtl8225_host_pci_init(struct net_device *dev)
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
