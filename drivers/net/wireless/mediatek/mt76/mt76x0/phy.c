/*
 * (c) Copyright 2002-2010, Ralink Technology, Inc.
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

#include <linux/kernel.h>
#include <linux/etherdevice.h>

#include "mt76x0.h"
#include "mcu.h"
#include "eeprom.h"
#include "phy.h"
#include "initvals.h"
#include "initvals_phy.h"
#include "../mt76x02_phy.h"

static int
mt76x0_rf_csr_wr(struct mt76x02_dev *dev, u32 offset, u8 value)
{
	int ret = 0;
	u8 bank, reg;

	if (test_bit(MT76_REMOVED, &dev->mt76.state))
		return -ENODEV;

	bank = MT_RF_BANK(offset);
	reg = MT_RF_REG(offset);

	if (WARN_ON_ONCE(reg > 127) || WARN_ON_ONCE(bank > 8))
		return -EINVAL;

	mutex_lock(&dev->phy_mutex);

	if (!mt76_poll(dev, MT_RF_CSR_CFG, MT_RF_CSR_CFG_KICK, 0, 100)) {
		ret = -ETIMEDOUT;
		goto out;
	}

	mt76_wr(dev, MT_RF_CSR_CFG,
		FIELD_PREP(MT_RF_CSR_CFG_DATA, value) |
		FIELD_PREP(MT_RF_CSR_CFG_REG_BANK, bank) |
		FIELD_PREP(MT_RF_CSR_CFG_REG_ID, reg) |
		MT_RF_CSR_CFG_WR |
		MT_RF_CSR_CFG_KICK);

out:
	mutex_unlock(&dev->phy_mutex);

	if (ret < 0)
		dev_err(dev->mt76.dev, "Error: RF write %d:%d failed:%d!!\n",
			bank, reg, ret);

	return ret;
}

static int mt76x0_rf_csr_rr(struct mt76x02_dev *dev, u32 offset)
{
	int ret = -ETIMEDOUT;
	u32 val;
	u8 bank, reg;

	if (test_bit(MT76_REMOVED, &dev->mt76.state))
		return -ENODEV;

	bank = MT_RF_BANK(offset);
	reg = MT_RF_REG(offset);

	if (WARN_ON_ONCE(reg > 127) || WARN_ON_ONCE(bank > 8))
		return -EINVAL;

	mutex_lock(&dev->phy_mutex);

	if (!mt76_poll(dev, MT_RF_CSR_CFG, MT_RF_CSR_CFG_KICK, 0, 100))
		goto out;

	mt76_wr(dev, MT_RF_CSR_CFG,
		FIELD_PREP(MT_RF_CSR_CFG_REG_BANK, bank) |
		FIELD_PREP(MT_RF_CSR_CFG_REG_ID, reg) |
		MT_RF_CSR_CFG_KICK);

	if (!mt76_poll(dev, MT_RF_CSR_CFG, MT_RF_CSR_CFG_KICK, 0, 100))
		goto out;

	val = mt76_rr(dev, MT_RF_CSR_CFG);
	if (FIELD_GET(MT_RF_CSR_CFG_REG_ID, val) == reg &&
	    FIELD_GET(MT_RF_CSR_CFG_REG_BANK, val) == bank)
		ret = FIELD_GET(MT_RF_CSR_CFG_DATA, val);

out:
	mutex_unlock(&dev->phy_mutex);

	if (ret < 0)
		dev_err(dev->mt76.dev, "Error: RF read %d:%d failed:%d!!\n",
			bank, reg, ret);

	return ret;
}

static int
mt76x0_rf_wr(struct mt76x02_dev *dev, u32 offset, u8 val)
{
	if (mt76_is_usb(dev)) {
		struct mt76_reg_pair pair = {
			.reg = offset,
			.value = val,
		};

		WARN_ON_ONCE(!test_bit(MT76_STATE_MCU_RUNNING,
			     &dev->mt76.state));
		return mt76_wr_rp(dev, MT_MCU_MEMMAP_RF, &pair, 1);
	} else {
		return mt76x0_rf_csr_wr(dev, offset, val);
	}
}

static int mt76x0_rf_rr(struct mt76x02_dev *dev, u32 offset)
{
	int ret;
	u32 val;

	if (mt76_is_usb(dev)) {
		struct mt76_reg_pair pair = {
			.reg = offset,
		};

		WARN_ON_ONCE(!test_bit(MT76_STATE_MCU_RUNNING,
			     &dev->mt76.state));
		ret = mt76_rd_rp(dev, MT_MCU_MEMMAP_RF, &pair, 1);
		val = pair.value;
	} else {
		ret = val = mt76x0_rf_csr_rr(dev, offset);
	}

	return (ret < 0) ? ret : val;
}

static int
mt76x0_rf_rmw(struct mt76x02_dev *dev, u32 offset, u8 mask, u8 val)
{
	int ret;

	ret = mt76x0_rf_rr(dev, offset);
	if (ret < 0)
		return ret;

	val |= ret & ~mask;

	ret = mt76x0_rf_wr(dev, offset, val);
	return ret ? ret : val;
}

static int
mt76x0_rf_set(struct mt76x02_dev *dev, u32 offset, u8 val)
{
	return mt76x0_rf_rmw(dev, offset, 0, val);
}

static int
mt76x0_rf_clear(struct mt76x02_dev *dev, u32 offset, u8 mask)
{
	return mt76x0_rf_rmw(dev, offset, mask, 0);
}

static void
mt76x0_phy_rf_csr_wr_rp(struct mt76x02_dev *dev,
			const struct mt76_reg_pair *data,
			int n)
{
	while (n-- > 0) {
		mt76x0_rf_csr_wr(dev, data->reg, data->value);
		data++;
	}
}

#define RF_RANDOM_WRITE(dev, tab) do {					\
	if (mt76_is_mmio(dev))						\
		mt76x0_phy_rf_csr_wr_rp(dev, tab, ARRAY_SIZE(tab));	\
	else								\
		mt76_wr_rp(dev, MT_MCU_MEMMAP_RF, tab, ARRAY_SIZE(tab));\
} while (0)

int mt76x0_phy_wait_bbp_ready(struct mt76x02_dev *dev)
{
	int i = 20;
	u32 val;

	do {
		val = mt76_rr(dev, MT_BBP(CORE, 0));
		if (val && ~val)
			break;
	} while (--i);

	if (!i) {
		dev_err(dev->mt76.dev, "Error: BBP is not ready\n");
		return -EIO;
	}

	dev_dbg(dev->mt76.dev, "BBP version %08x\n", val);
	return 0;
}

static void
mt76x0_phy_set_band(struct mt76x02_dev *dev, enum nl80211_band band)
{
	switch (band) {
	case NL80211_BAND_2GHZ:
		RF_RANDOM_WRITE(dev, mt76x0_rf_2g_channel_0_tab);

		mt76x0_rf_wr(dev, MT_RF(5, 0), 0x45);
		mt76x0_rf_wr(dev, MT_RF(6, 0), 0x44);

		mt76_wr(dev, MT_TX_ALC_VGA3, 0x00050007);
		mt76_wr(dev, MT_TX0_RF_GAIN_CORR, 0x003E0002);
		break;
	case NL80211_BAND_5GHZ:
		RF_RANDOM_WRITE(dev, mt76x0_rf_5g_channel_0_tab);

		mt76x0_rf_wr(dev, MT_RF(5, 0), 0x44);
		mt76x0_rf_wr(dev, MT_RF(6, 0), 0x45);

		mt76_wr(dev, MT_TX_ALC_VGA3, 0x00000005);
		mt76_wr(dev, MT_TX0_RF_GAIN_CORR, 0x01010102);
		break;
	default:
		break;
	}
}

static void
mt76x0_phy_set_chan_rf_params(struct mt76x02_dev *dev, u8 channel, u16 rf_bw_band)
{
	const struct mt76x0_freq_item *freq_item;
	u16 rf_band = rf_bw_band & 0xff00;
	u16 rf_bw = rf_bw_band & 0x00ff;
	enum nl80211_band band;
	bool b_sdm = false;
	u32 mac_reg;
	int i;

	for (i = 0; i < ARRAY_SIZE(mt76x0_sdm_channel); i++) {
		if (channel == mt76x0_sdm_channel[i]) {
			b_sdm = true;
			break;
		}
	}

	for (i = 0; i < ARRAY_SIZE(mt76x0_frequency_plan); i++) {
		if (channel == mt76x0_frequency_plan[i].channel) {
			rf_band = mt76x0_frequency_plan[i].band;

			if (b_sdm)
				freq_item = &(mt76x0_sdm_frequency_plan[i]);
			else
				freq_item = &(mt76x0_frequency_plan[i]);

			mt76x0_rf_wr(dev, MT_RF(0, 37), freq_item->pllR37);
			mt76x0_rf_wr(dev, MT_RF(0, 36), freq_item->pllR36);
			mt76x0_rf_wr(dev, MT_RF(0, 35), freq_item->pllR35);
			mt76x0_rf_wr(dev, MT_RF(0, 34), freq_item->pllR34);
			mt76x0_rf_wr(dev, MT_RF(0, 33), freq_item->pllR33);

			mt76x0_rf_rmw(dev, MT_RF(0, 32), 0xe0,
				      freq_item->pllR32_b7b5);

			/* R32<4:0> pll_den: (Denomina - 8) */
			mt76x0_rf_rmw(dev, MT_RF(0, 32), MT_RF_PLL_DEN_MASK,
				      freq_item->pllR32_b4b0);

			/* R31<7:5> */
			mt76x0_rf_rmw(dev, MT_RF(0, 31), 0xe0,
				      freq_item->pllR31_b7b5);

			/* R31<4:0> pll_k(Nominator) */
			mt76x0_rf_rmw(dev, MT_RF(0, 31), MT_RF_PLL_K_MASK,
				      freq_item->pllR31_b4b0);

			/* R30<7> sdm_reset_n */
			if (b_sdm) {
				mt76x0_rf_clear(dev, MT_RF(0, 30),
						MT_RF_SDM_RESET_MASK);
				mt76x0_rf_set(dev, MT_RF(0, 30),
					      MT_RF_SDM_RESET_MASK);
			} else {
				mt76x0_rf_rmw(dev, MT_RF(0, 30),
					      MT_RF_SDM_RESET_MASK,
					      freq_item->pllR30_b7);
			}

			/* R30<6:2> sdmmash_prbs,sin */
			mt76x0_rf_rmw(dev, MT_RF(0, 30),
				      MT_RF_SDM_MASH_PRBS_MASK,
				      freq_item->pllR30_b6b2);

			/* R30<1> sdm_bp */
			mt76x0_rf_rmw(dev, MT_RF(0, 30), MT_RF_SDM_BP_MASK,
				      freq_item->pllR30_b1 << 1);

			/* R30<0> R29<7:0> (hex) pll_n */
			mt76x0_rf_wr(dev, MT_RF(0, 29),
				     freq_item->pll_n & 0xff);

			mt76x0_rf_rmw(dev, MT_RF(0, 30), 0x1,
				      (freq_item->pll_n >> 8) & 0x1);

			/* R28<7:6> isi_iso */
			mt76x0_rf_rmw(dev, MT_RF(0, 28), MT_RF_ISI_ISO_MASK,
				      freq_item->pllR28_b7b6);

			/* R28<5:4> pfd_dly */
			mt76x0_rf_rmw(dev, MT_RF(0, 28), MT_RF_PFD_DLY_MASK,
				      freq_item->pllR28_b5b4);

			/* R28<3:2> clksel option */
			mt76x0_rf_rmw(dev, MT_RF(0, 28), MT_RF_CLK_SEL_MASK,
				      freq_item->pllR28_b3b2);

			/* R28<1:0> R27<7:0> R26<7:0> (hex) sdm_k */
			mt76x0_rf_wr(dev, MT_RF(0, 26),
				     freq_item->pll_sdm_k & 0xff);
			mt76x0_rf_wr(dev, MT_RF(0, 27),
				     (freq_item->pll_sdm_k >> 8) & 0xff);

			mt76x0_rf_rmw(dev, MT_RF(0, 28), 0x3,
				      (freq_item->pll_sdm_k >> 16) & 0x3);

			/* R24<1:0> xo_div */
			mt76x0_rf_rmw(dev, MT_RF(0, 24), MT_RF_XO_DIV_MASK,
				      freq_item->pllR24_b1b0);

			break;
		}
	}

	for (i = 0; i < ARRAY_SIZE(mt76x0_rf_bw_switch_tab); i++) {
		if (rf_bw == mt76x0_rf_bw_switch_tab[i].bw_band) {
			mt76x0_rf_wr(dev,
				     mt76x0_rf_bw_switch_tab[i].rf_bank_reg,
				     mt76x0_rf_bw_switch_tab[i].value);
		} else if ((rf_bw == (mt76x0_rf_bw_switch_tab[i].bw_band & 0xFF)) &&
			   (rf_band & mt76x0_rf_bw_switch_tab[i].bw_band)) {
			mt76x0_rf_wr(dev,
				     mt76x0_rf_bw_switch_tab[i].rf_bank_reg,
				     mt76x0_rf_bw_switch_tab[i].value);
		}
	}

	for (i = 0; i < ARRAY_SIZE(mt76x0_rf_band_switch_tab); i++) {
		if (mt76x0_rf_band_switch_tab[i].bw_band & rf_band) {
			mt76x0_rf_wr(dev,
				     mt76x0_rf_band_switch_tab[i].rf_bank_reg,
				     mt76x0_rf_band_switch_tab[i].value);
		}
	}

	mt76_clear(dev, MT_RF_MISC, 0xc);

	band = (rf_band & RF_G_BAND) ? NL80211_BAND_2GHZ : NL80211_BAND_5GHZ;
	if (mt76x02_ext_pa_enabled(dev, band)) {
		/*
			MT_RF_MISC (offset: 0x0518)
			[2]1'b1: enable external A band PA, 1'b0: disable external A band PA
			[3]1'b1: enable external G band PA, 1'b0: disable external G band PA
		*/
		if (rf_band & RF_A_BAND)
			mt76_set(dev, MT_RF_MISC, BIT(2));
		else
			mt76_set(dev, MT_RF_MISC, BIT(3));

		/* External PA */
		for (i = 0; i < ARRAY_SIZE(mt76x0_rf_ext_pa_tab); i++)
			if (mt76x0_rf_ext_pa_tab[i].bw_band & rf_band)
				mt76x0_rf_wr(dev,
					mt76x0_rf_ext_pa_tab[i].rf_bank_reg,
					mt76x0_rf_ext_pa_tab[i].value);
	}

	if (rf_band & RF_G_BAND) {
		mt76_wr(dev, MT_TX0_RF_GAIN_ATTEN, 0x63707400);
		/* Set Atten mode = 2 For G band, Disable Tx Inc dcoc. */
		mac_reg = mt76_rr(dev, MT_TX_ALC_CFG_1);
		mac_reg &= 0x896400FF;
		mt76_wr(dev, MT_TX_ALC_CFG_1, mac_reg);
	} else {
		mt76_wr(dev, MT_TX0_RF_GAIN_ATTEN, 0x686A7800);
		/* Set Atten mode = 0 For Ext A band, Disable Tx Inc dcoc Cal. */
		mac_reg = mt76_rr(dev, MT_TX_ALC_CFG_1);
		mac_reg &= 0x890400FF;
		mt76_wr(dev, MT_TX_ALC_CFG_1, mac_reg);
	}
}

static void
mt76x0_phy_set_chan_bbp_params(struct mt76x02_dev *dev, u16 rf_bw_band)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mt76x0_bbp_switch_tab); i++) {
		const struct mt76x0_bbp_switch_item *item = &mt76x0_bbp_switch_tab[i];
		const struct mt76_reg_pair *pair = &item->reg_pair;

		if ((rf_bw_band & item->bw_band) != rf_bw_band)
			continue;

		if (pair->reg == MT_BBP(AGC, 8)) {
			u32 val = pair->value;
			u8 gain;

			gain = FIELD_GET(MT_BBP_AGC_GAIN, val);
			gain -= dev->cal.rx.lna_gain * 2;
			val &= ~MT_BBP_AGC_GAIN;
			val |= FIELD_PREP(MT_BBP_AGC_GAIN, gain);
			mt76_wr(dev, pair->reg, val);
		} else {
			mt76_wr(dev, pair->reg, pair->value);
		}
	}
}

static void mt76x0_phy_ant_select(struct mt76x02_dev *dev)
{
	u16 ee_ant = mt76x02_eeprom_get(dev, MT_EE_ANTENNA);
	u16 nic_conf2 = mt76x02_eeprom_get(dev, MT_EE_NIC_CONF_2);
	u32 wlan, coex3, cmb;
	bool ant_div;

	wlan = mt76_rr(dev, MT_WLAN_FUN_CTRL);
	cmb = mt76_rr(dev, MT_CMB_CTRL);
	coex3 = mt76_rr(dev, MT_COEXCFG3);

	cmb   &= ~(BIT(14) | BIT(12));
	wlan  &= ~(BIT(6) | BIT(5));
	coex3 &= ~GENMASK(5, 2);

	if (ee_ant & MT_EE_ANTENNA_DUAL) {
		/* dual antenna mode */
		ant_div = !(nic_conf2 & MT_EE_NIC_CONF_2_ANT_OPT) &&
			  (nic_conf2 & MT_EE_NIC_CONF_2_ANT_DIV);
		if (ant_div)
			cmb |= BIT(12);
		else
			coex3 |= BIT(4);
		coex3 |= BIT(3);
		if (dev->mt76.cap.has_2ghz)
			wlan |= BIT(6);
	} else {
		/* sigle antenna mode */
		if (dev->mt76.cap.has_5ghz) {
			coex3 |= BIT(3) | BIT(4);
		} else {
			wlan |= BIT(6);
			coex3 |= BIT(1);
		}
	}

	if (is_mt7630(dev))
		cmb |= BIT(14) | BIT(11);

	mt76_wr(dev, MT_WLAN_FUN_CTRL, wlan);
	mt76_wr(dev, MT_CMB_CTRL, cmb);
	mt76_clear(dev, MT_COEXCFG0, BIT(2));
	mt76_wr(dev, MT_COEXCFG3, coex3);
}

static void
mt76x0_phy_bbp_set_bw(struct mt76x02_dev *dev, enum nl80211_chan_width width)
{
	enum { BW_20 = 0, BW_40 = 1, BW_80 = 2, BW_10 = 4};
	int bw;

	switch (width) {
	default:
	case NL80211_CHAN_WIDTH_20_NOHT:
	case NL80211_CHAN_WIDTH_20:
		bw = BW_20;
		break;
	case NL80211_CHAN_WIDTH_40:
		bw = BW_40;
		break;
	case NL80211_CHAN_WIDTH_80:
		bw = BW_80;
		break;
	case NL80211_CHAN_WIDTH_10:
		bw = BW_10;
		break;
	case NL80211_CHAN_WIDTH_80P80:
	case NL80211_CHAN_WIDTH_160:
	case NL80211_CHAN_WIDTH_5:
		/* TODO error */
		return ;
	}

	mt76x02_mcu_function_select(dev, BW_SETTING, bw);
}

static void mt76x0_phy_tssi_dc_calibrate(struct mt76x02_dev *dev)
{
	struct ieee80211_channel *chan = dev->mt76.chandef.chan;
	u32 val;

	if (chan->band == NL80211_BAND_5GHZ)
		mt76x0_rf_clear(dev, MT_RF(0, 67), 0xf);

	/* bypass ADDA control */
	mt76_wr(dev, MT_RF_SETTING_0, 0x60002237);
	mt76_wr(dev, MT_RF_BYPASS_0, 0xffffffff);

	/* bbp sw reset */
	mt76_set(dev, MT_BBP(CORE, 4), BIT(0));
	usleep_range(500, 1000);
	mt76_clear(dev, MT_BBP(CORE, 4), BIT(0));

	val = (chan->band == NL80211_BAND_5GHZ) ? 0x80055 : 0x80050;
	mt76_wr(dev, MT_BBP(CORE, 34), val);

	/* enable TX with DAC0 input */
	mt76_wr(dev, MT_BBP(TXBE, 6), BIT(31));

	mt76_poll_msec(dev, MT_BBP(CORE, 34), BIT(4), 0, 200);
	dev->cal.tssi_dc = mt76_rr(dev, MT_BBP(CORE, 35)) & 0xff;

	/* stop bypass ADDA */
	mt76_wr(dev, MT_RF_BYPASS_0, 0);
	/* stop TX */
	mt76_wr(dev, MT_BBP(TXBE, 6), 0);
	/* bbp sw reset */
	mt76_set(dev, MT_BBP(CORE, 4), BIT(0));
	usleep_range(500, 1000);
	mt76_clear(dev, MT_BBP(CORE, 4), BIT(0));

	if (chan->band == NL80211_BAND_5GHZ)
		mt76x0_rf_rmw(dev, MT_RF(0, 67), 0xf, 0x4);
}

static int
mt76x0_phy_tssi_adc_calibrate(struct mt76x02_dev *dev, s16 *ltssi,
			      u8 *info)
{
	struct ieee80211_channel *chan = dev->mt76.chandef.chan;
	u32 val;

	val = (chan->band == NL80211_BAND_5GHZ) ? 0x80055 : 0x80050;
	mt76_wr(dev, MT_BBP(CORE, 34), val);

	if (!mt76_poll_msec(dev, MT_BBP(CORE, 34), BIT(4), 0, 200)) {
		mt76_clear(dev, MT_BBP(CORE, 34), BIT(4));
		return -ETIMEDOUT;
	}

	*ltssi = mt76_rr(dev, MT_BBP(CORE, 35)) & 0xff;
	if (chan->band == NL80211_BAND_5GHZ)
		*ltssi += 128;

	/* set packet info#1 mode */
	mt76_wr(dev, MT_BBP(CORE, 34), 0x80041);
	info[0] = mt76_rr(dev, MT_BBP(CORE, 35)) & 0xff;

	/* set packet info#2 mode */
	mt76_wr(dev, MT_BBP(CORE, 34), 0x80042);
	info[1] = mt76_rr(dev, MT_BBP(CORE, 35)) & 0xff;

	/* set packet info#3 mode */
	mt76_wr(dev, MT_BBP(CORE, 34), 0x80043);
	info[2] = mt76_rr(dev, MT_BBP(CORE, 35)) & 0xff;

	return 0;
}

static u8 mt76x0_phy_get_rf_pa_mode(struct mt76x02_dev *dev,
				    int index, u8 tx_rate)
{
	u32 val, reg;

	reg = (index == 1) ? MT_RF_PA_MODE_CFG1 : MT_RF_PA_MODE_CFG0;
	val = mt76_rr(dev, reg);
	return (val & (3 << (tx_rate * 2))) >> (tx_rate * 2);
}

static int
mt76x0_phy_get_target_power(struct mt76x02_dev *dev, u8 tx_mode,
			    u8 *info, s8 *target_power,
			    s8 *target_pa_power)
{
	u8 tx_rate, cur_power;

	cur_power = mt76_rr(dev, MT_TX_ALC_CFG_0) & MT_TX_ALC_CFG_0_CH_INIT_0;
	switch (tx_mode) {
	case 0:
		/* cck rates */
		tx_rate = (info[0] & 0x60) >> 5;
		if (tx_rate > 3)
			return -EINVAL;

		*target_power = cur_power + dev->mt76.rate_power.cck[tx_rate];
		*target_pa_power = mt76x0_phy_get_rf_pa_mode(dev, 0, tx_rate);
		break;
	case 1: {
		u8 index;

		/* ofdm rates */
		tx_rate = (info[0] & 0xf0) >> 4;
		switch (tx_rate) {
		case 0xb:
			index = 0;
			break;
		case 0xf:
			index = 1;
			break;
		case 0xa:
			index = 2;
			break;
		case 0xe:
			index = 3;
			break;
		case 0x9:
			index = 4;
			break;
		case 0xd:
			index = 5;
			break;
		case 0x8:
			index = 6;
			break;
		case 0xc:
			index = 7;
			break;
		default:
			return -EINVAL;
		}

		*target_power = cur_power + dev->mt76.rate_power.ofdm[index];
		*target_pa_power = mt76x0_phy_get_rf_pa_mode(dev, 0, index + 4);
		break;
	}
	case 4:
		/* vht rates */
		tx_rate = info[1] & 0xf;
		if (tx_rate > 9)
			return -EINVAL;

		*target_power = cur_power + dev->mt76.rate_power.vht[tx_rate];
		*target_pa_power = mt76x0_phy_get_rf_pa_mode(dev, 1, tx_rate);
		break;
	default:
		/* ht rates */
		tx_rate = info[1] & 0x7f;
		if (tx_rate > 9)
			return -EINVAL;

		*target_power = cur_power + dev->mt76.rate_power.ht[tx_rate];
		*target_pa_power = mt76x0_phy_get_rf_pa_mode(dev, 1, tx_rate);
		break;
	}

	return 0;
}

static s16 mt76x0_phy_lin2db(u16 val)
{
	u32 mantissa = val << 4;
	int ret, data;
	s16 exp = -4;

	while (mantissa < BIT(15)) {
		mantissa <<= 1;
		if (--exp < -20)
			return -10000;
	}
	while (mantissa > 0xffff) {
		mantissa >>= 1;
		if (++exp > 20)
			return -10000;
	}

	/* s(15,0) */
	if (mantissa <= 47104)
		data = mantissa + (mantissa >> 3) + (mantissa >> 4) - 38400;
	else
		data = mantissa - (mantissa >> 3) - (mantissa >> 6) - 23040;
	data = max_t(int, 0, data);

	ret = ((15 + exp) << 15) + data;
	ret = (ret << 2) + (ret << 1) + (ret >> 6) + (ret >> 7);
	return ret >> 10;
}

static int
mt76x0_phy_get_delta_power(struct mt76x02_dev *dev, u8 tx_mode,
			   s8 target_power, s8 target_pa_power,
			   s16 ltssi)
{
	struct ieee80211_channel *chan = dev->mt76.chandef.chan;
	int tssi_target = target_power << 12, tssi_slope;
	int tssi_offset, tssi_db, ret;
	u32 data;
	u16 val;

	if (chan->band == NL80211_BAND_5GHZ) {
		u8 bound[7];
		int i, err;

		err = mt76x02_eeprom_copy(dev, MT_EE_TSSI_BOUND1, bound,
					  sizeof(bound));
		if (err < 0)
			return err;

		for (i = 0; i < ARRAY_SIZE(bound); i++) {
			if (chan->hw_value <= bound[i] || !bound[i])
				break;
		}
		val = mt76x02_eeprom_get(dev, MT_EE_TSSI_SLOPE_5G + i * 2);

		tssi_offset = val >> 8;
		if ((tssi_offset >= 64 && tssi_offset <= 127) ||
		    (tssi_offset & BIT(7)))
			tssi_offset -= BIT(8);
	} else {
		val = mt76x02_eeprom_get(dev, MT_EE_TSSI_SLOPE_2G);

		tssi_offset = val >> 8;
		if (tssi_offset & BIT(7))
			tssi_offset -= BIT(8);
	}
	tssi_slope = val & 0xff;

	switch (target_pa_power) {
	case 1:
		if (chan->band == NL80211_BAND_2GHZ)
			tssi_target += 29491; /* 3.6 * 8192 */
		/* fall through */
	case 0:
		break;
	default:
		tssi_target += 4424; /* 0.54 * 8192 */
		break;
	}

	if (!tx_mode) {
		data = mt76_rr(dev, MT_BBP(CORE, 1));
		if (is_mt7630(dev) && mt76_is_mmio(dev)) {
			int offset;

			/* 2.3 * 8192 or 1.5 * 8192 */
			offset = (data & BIT(5)) ? 18841 : 12288;
			tssi_target += offset;
		} else if (data & BIT(5)) {
			/* 0.8 * 8192 */
			tssi_target += 6554;
		}
	}

	data = mt76_rr(dev, MT_BBP(TXBE, 4));
	switch (data & 0x3) {
	case 1:
		tssi_target -= 49152; /* -6db * 8192 */
		break;
	case 2:
		tssi_target -= 98304; /* -12db * 8192 */
		break;
	case 3:
		tssi_target += 49152; /* 6db * 8192 */
		break;
	default:
		break;
	}

	tssi_db = mt76x0_phy_lin2db(ltssi - dev->cal.tssi_dc) * tssi_slope;
	if (chan->band == NL80211_BAND_5GHZ) {
		tssi_db += ((tssi_offset - 50) << 10); /* offset s4.3 */
		tssi_target -= tssi_db;
		if (ltssi > 254 && tssi_target > 0) {
			/* upper saturate */
			tssi_target = 0;
		}
	} else {
		tssi_db += (tssi_offset << 9); /* offset s3.4 */
		tssi_target -= tssi_db;
		/* upper-lower saturate */
		if ((ltssi > 126 && tssi_target > 0) ||
		    ((ltssi - dev->cal.tssi_dc) < 1 && tssi_target < 0)) {
			tssi_target = 0;
		}
	}

	if ((dev->cal.tssi_target ^ tssi_target) < 0 &&
	    dev->cal.tssi_target > -4096 && dev->cal.tssi_target < 4096 &&
	    tssi_target > -4096 && tssi_target < 4096) {
		if ((tssi_target < 0 &&
		     tssi_target + dev->cal.tssi_target > 0) ||
		    (tssi_target > 0 &&
		     tssi_target + dev->cal.tssi_target <= 0))
			tssi_target = 0;
		else
			dev->cal.tssi_target = tssi_target;
	} else {
		dev->cal.tssi_target = tssi_target;
	}

	/* make the compensate value to the nearest compensate code */
	if (tssi_target > 0)
		tssi_target += 2048;
	else
		tssi_target -= 2048;
	tssi_target >>= 12;

	ret = mt76_get_field(dev, MT_TX_ALC_CFG_1, MT_TX_ALC_CFG_1_TEMP_COMP);
	if (ret & BIT(5))
		ret -= BIT(6);
	ret += tssi_target;

	ret = min_t(int, 31, ret);
	return max_t(int, -32, ret);
}

static void mt76x0_phy_tssi_calibrate(struct mt76x02_dev *dev)
{
	s8 target_power, target_pa_power;
	u8 tssi_info[3], tx_mode;
	s16 ltssi;
	s8 val;

	if (mt76x0_phy_tssi_adc_calibrate(dev, &ltssi, tssi_info) < 0)
		return;

	tx_mode = tssi_info[0] & 0x7;
	if (mt76x0_phy_get_target_power(dev, tx_mode, tssi_info,
					&target_power, &target_pa_power) < 0)
		return;

	val = mt76x0_phy_get_delta_power(dev, tx_mode, target_power,
					 target_pa_power, ltssi);
	mt76_rmw_field(dev, MT_TX_ALC_CFG_1, MT_TX_ALC_CFG_1_TEMP_COMP, val);
}

void mt76x0_phy_set_txpower(struct mt76x02_dev *dev)
{
	struct mt76_rate_power *t = &dev->mt76.rate_power;
	s8 info;

	mt76x0_get_tx_power_per_rate(dev, dev->mt76.chandef.chan, t);
	mt76x0_get_power_info(dev, dev->mt76.chandef.chan, &info);

	mt76x02_add_rate_power_offset(t, info);
	mt76x02_limit_rate_power(t, dev->mt76.txpower_conf);
	dev->mt76.txpower_cur = mt76x02_get_max_rate_power(t);
	mt76x02_add_rate_power_offset(t, -info);

	dev->target_power = info;
	mt76x02_phy_set_txpower(dev, info, info);
}

void mt76x0_phy_calibrate(struct mt76x02_dev *dev, bool power_on)
{
	struct ieee80211_channel *chan = dev->mt76.chandef.chan;
	int is_5ghz = (chan->band == NL80211_BAND_5GHZ) ? 1 : 0;
	u32 val, tx_alc, reg_val;

	if (is_mt7630(dev))
		return;

	if (power_on) {
		mt76x02_mcu_calibrate(dev, MCU_CAL_R, 0);
		mt76x02_mcu_calibrate(dev, MCU_CAL_VCO, chan->hw_value);
		usleep_range(10, 20);

		if (mt76x0_tssi_enabled(dev)) {
			mt76_wr(dev, MT_MAC_SYS_CTRL,
				MT_MAC_SYS_CTRL_ENABLE_RX);
			mt76x0_phy_tssi_dc_calibrate(dev);
			mt76_wr(dev, MT_MAC_SYS_CTRL,
				MT_MAC_SYS_CTRL_ENABLE_TX |
				MT_MAC_SYS_CTRL_ENABLE_RX);
		}
	}

	tx_alc = mt76_rr(dev, MT_TX_ALC_CFG_0);
	mt76_wr(dev, MT_TX_ALC_CFG_0, 0);
	usleep_range(500, 700);

	reg_val = mt76_rr(dev, MT_BBP(IBI, 9));
	mt76_wr(dev, MT_BBP(IBI, 9), 0xffffff7e);

	if (is_5ghz) {
		if (chan->hw_value < 100)
			val = 0x701;
		else if (chan->hw_value < 140)
			val = 0x801;
		else
			val = 0x901;
	} else {
		val = 0x600;
	}

	mt76x02_mcu_calibrate(dev, MCU_CAL_FULL, val);
	msleep(350);
	mt76x02_mcu_calibrate(dev, MCU_CAL_LC, is_5ghz);
	usleep_range(15000, 20000);

	mt76_wr(dev, MT_BBP(IBI, 9), reg_val);
	mt76_wr(dev, MT_TX_ALC_CFG_0, tx_alc);
	mt76x02_mcu_calibrate(dev, MCU_CAL_RXDCOC, 1);
}
EXPORT_SYMBOL_GPL(mt76x0_phy_calibrate);

int mt76x0_phy_set_channel(struct mt76x02_dev *dev,
			   struct cfg80211_chan_def *chandef)
{
	u32 ext_cca_chan[4] = {
		[0] = FIELD_PREP(MT_EXT_CCA_CFG_CCA0, 0) |
		      FIELD_PREP(MT_EXT_CCA_CFG_CCA1, 1) |
		      FIELD_PREP(MT_EXT_CCA_CFG_CCA2, 2) |
		      FIELD_PREP(MT_EXT_CCA_CFG_CCA3, 3) |
		      FIELD_PREP(MT_EXT_CCA_CFG_CCA_MASK, BIT(0)),
		[1] = FIELD_PREP(MT_EXT_CCA_CFG_CCA0, 1) |
		      FIELD_PREP(MT_EXT_CCA_CFG_CCA1, 0) |
		      FIELD_PREP(MT_EXT_CCA_CFG_CCA2, 2) |
		      FIELD_PREP(MT_EXT_CCA_CFG_CCA3, 3) |
		      FIELD_PREP(MT_EXT_CCA_CFG_CCA_MASK, BIT(1)),
		[2] = FIELD_PREP(MT_EXT_CCA_CFG_CCA0, 2) |
		      FIELD_PREP(MT_EXT_CCA_CFG_CCA1, 3) |
		      FIELD_PREP(MT_EXT_CCA_CFG_CCA2, 1) |
		      FIELD_PREP(MT_EXT_CCA_CFG_CCA3, 0) |
		      FIELD_PREP(MT_EXT_CCA_CFG_CCA_MASK, BIT(2)),
		[3] = FIELD_PREP(MT_EXT_CCA_CFG_CCA0, 3) |
		      FIELD_PREP(MT_EXT_CCA_CFG_CCA1, 2) |
		      FIELD_PREP(MT_EXT_CCA_CFG_CCA2, 1) |
		      FIELD_PREP(MT_EXT_CCA_CFG_CCA3, 0) |
		      FIELD_PREP(MT_EXT_CCA_CFG_CCA_MASK, BIT(3)),
	};
	bool scan = test_bit(MT76_SCANNING, &dev->mt76.state);
	int ch_group_index, freq, freq1;
	u8 channel;
	u32 val;
	u16 rf_bw_band;

	freq = chandef->chan->center_freq;
	freq1 = chandef->center_freq1;
	channel = chandef->chan->hw_value;
	rf_bw_band = (channel <= 14) ? RF_G_BAND : RF_A_BAND;
	dev->mt76.chandef = *chandef;

	switch (chandef->width) {
	case NL80211_CHAN_WIDTH_40:
		if (freq1 > freq)
			ch_group_index = 0;
		else
			ch_group_index = 1;
		channel += 2 - ch_group_index * 4;
		rf_bw_band |= RF_BW_40;
		break;
	case NL80211_CHAN_WIDTH_80:
		ch_group_index = (freq - freq1 + 30) / 20;
		if (WARN_ON(ch_group_index < 0 || ch_group_index > 3))
			ch_group_index = 0;
		channel += 6 - ch_group_index * 4;
		rf_bw_band |= RF_BW_80;
		break;
	default:
		ch_group_index = 0;
		rf_bw_band |= RF_BW_20;
		break;
	}

	if (mt76_is_usb(dev)) {
		mt76x0_phy_bbp_set_bw(dev, chandef->width);
	} else {
		if (chandef->width == NL80211_CHAN_WIDTH_80 ||
		    chandef->width == NL80211_CHAN_WIDTH_40)
			val = 0x201;
		else
			val = 0x601;
		mt76_wr(dev, MT_TX_SW_CFG0, val);
	}
	mt76x02_phy_set_bw(dev, chandef->width, ch_group_index);
	mt76x02_phy_set_band(dev, chandef->chan->band,
			     ch_group_index & 1);

	mt76_rmw(dev, MT_EXT_CCA_CFG,
		 (MT_EXT_CCA_CFG_CCA0 |
		  MT_EXT_CCA_CFG_CCA1 |
		  MT_EXT_CCA_CFG_CCA2 |
		  MT_EXT_CCA_CFG_CCA3 |
		  MT_EXT_CCA_CFG_CCA_MASK),
		 ext_cca_chan[ch_group_index]);

	mt76x0_phy_set_band(dev, chandef->chan->band);
	mt76x0_phy_set_chan_rf_params(dev, channel, rf_bw_band);

	/* set Japan Tx filter at channel 14 */
	if (channel == 14)
		mt76_set(dev, MT_BBP(CORE, 1), 0x20);
	else
		mt76_clear(dev, MT_BBP(CORE, 1), 0x20);

	mt76x0_read_rx_gain(dev);
	mt76x0_phy_set_chan_bbp_params(dev, rf_bw_band);

	/* enable vco */
	mt76x0_rf_set(dev, MT_RF(0, 4), BIT(7));
	if (scan) {
		mt76x02_edcca_init(dev, false);
		return 0;
	}

	mt76x02_init_agc_gain(dev);
	mt76x0_phy_calibrate(dev, false);
	mt76x0_phy_set_txpower(dev);

	mt76x02_edcca_init(dev, true);

	ieee80211_queue_delayed_work(dev->mt76.hw, &dev->cal_work,
				     MT_CALIBRATE_INTERVAL);

	return 0;
}

static void mt76x0_phy_temp_sensor(struct mt76x02_dev *dev)
{
	u8 rf_b7_73, rf_b0_66, rf_b0_67;
	s8 val;

	rf_b7_73 = mt76x0_rf_rr(dev, MT_RF(7, 73));
	rf_b0_66 = mt76x0_rf_rr(dev, MT_RF(0, 66));
	rf_b0_67 = mt76x0_rf_rr(dev, MT_RF(0, 67));

	mt76x0_rf_wr(dev, MT_RF(7, 73), 0x02);
	mt76x0_rf_wr(dev, MT_RF(0, 66), 0x23);
	mt76x0_rf_wr(dev, MT_RF(0, 67), 0x01);

	mt76_wr(dev, MT_BBP(CORE, 34), 0x00080055);
	if (!mt76_poll_msec(dev, MT_BBP(CORE, 34), BIT(4), 0, 200)) {
		mt76_clear(dev, MT_BBP(CORE, 34), BIT(4));
		goto done;
	}

	val = mt76_rr(dev, MT_BBP(CORE, 35));
	val = (35 * (val - dev->cal.rx.temp_offset)) / 10 + 25;

	if (abs(val - dev->cal.temp_vco) > 20) {
		mt76x02_mcu_calibrate(dev, MCU_CAL_VCO,
				      dev->mt76.chandef.chan->hw_value);
		dev->cal.temp_vco = val;
	}
	if (abs(val - dev->cal.temp) > 30) {
		mt76x0_phy_calibrate(dev, false);
		dev->cal.temp = val;
	}

done:
	mt76x0_rf_wr(dev, MT_RF(7, 73), rf_b7_73);
	mt76x0_rf_wr(dev, MT_RF(0, 66), rf_b0_66);
	mt76x0_rf_wr(dev, MT_RF(0, 67), rf_b0_67);
}

static void mt76x0_phy_set_gain_val(struct mt76x02_dev *dev)
{
	u8 gain = dev->cal.agc_gain_cur[0] - dev->cal.agc_gain_adjust;

	mt76_rmw_field(dev, MT_BBP(AGC, 8), MT_BBP_AGC_GAIN, gain);

	if ((dev->mt76.chandef.chan->flags & IEEE80211_CHAN_RADAR) &&
	    !is_mt7630(dev))
		mt76x02_phy_dfs_adjust_agc(dev);
}

static void
mt76x0_phy_update_channel_gain(struct mt76x02_dev *dev)
{
	bool gain_change;
	u8 gain_delta;
	int low_gain;

	dev->cal.avg_rssi_all = mt76_get_min_avg_rssi(&dev->mt76);
	if (!dev->cal.avg_rssi_all)
		dev->cal.avg_rssi_all = -75;

	low_gain = (dev->cal.avg_rssi_all > mt76x02_get_rssi_gain_thresh(dev)) +
		   (dev->cal.avg_rssi_all > mt76x02_get_low_rssi_gain_thresh(dev));

	gain_change = dev->cal.low_gain < 0 ||
		      (dev->cal.low_gain & 2) ^ (low_gain & 2);
	dev->cal.low_gain = low_gain;

	if (!gain_change) {
		if (mt76x02_phy_adjust_vga_gain(dev))
			mt76x0_phy_set_gain_val(dev);
		return;
	}

	dev->cal.agc_gain_adjust = (low_gain == 2) ? 0 : 10;
	gain_delta = (low_gain == 2) ? 10 : 0;

	dev->cal.agc_gain_cur[0] = dev->cal.agc_gain_init[0] - gain_delta;
	mt76x0_phy_set_gain_val(dev);

	/* clear false CCA counters */
	mt76_rr(dev, MT_RX_STAT_1);
}

static void mt76x0_phy_calibration_work(struct work_struct *work)
{
	struct mt76x02_dev *dev = container_of(work, struct mt76x02_dev,
					       cal_work.work);

	mt76x0_phy_update_channel_gain(dev);
	if (mt76x0_tssi_enabled(dev))
		mt76x0_phy_tssi_calibrate(dev);
	else
		mt76x0_phy_temp_sensor(dev);

	ieee80211_queue_delayed_work(dev->mt76.hw, &dev->cal_work,
				     4 * MT_CALIBRATE_INTERVAL);
}

static void mt76x0_rf_patch_reg_array(struct mt76x02_dev *dev,
				      const struct mt76_reg_pair *rp, int len)
{
	int i;

	for (i = 0; i < len; i++) {
		u32 reg = rp[i].reg;
		u8 val = rp[i].value;

		switch (reg) {
		case MT_RF(0, 3):
			if (mt76_is_mmio(dev)) {
				if (is_mt7630(dev))
					val = 0x70;
				else
					val = 0x63;
			} else {
				val = 0x73;
			}
			break;
		case MT_RF(0, 21):
			if (is_mt7610e(dev))
				val = 0x10;
			else
				val = 0x12;
			break;
		case MT_RF(5, 2):
			if (is_mt7630(dev))
				val = 0x1d;
			else if (is_mt7610e(dev))
				val = 0x00;
			else
				val = 0x0c;
			break;
		default:
			break;
		}
		mt76x0_rf_wr(dev, reg, val);
	}
}

static void mt76x0_phy_rf_init(struct mt76x02_dev *dev)
{
	int i;
	u8 val;

	mt76x0_rf_patch_reg_array(dev, mt76x0_rf_central_tab,
				  ARRAY_SIZE(mt76x0_rf_central_tab));
	mt76x0_rf_patch_reg_array(dev, mt76x0_rf_2g_channel_0_tab,
				  ARRAY_SIZE(mt76x0_rf_2g_channel_0_tab));
	RF_RANDOM_WRITE(dev, mt76x0_rf_5g_channel_0_tab);
	RF_RANDOM_WRITE(dev, mt76x0_rf_vga_channel_0_tab);

	for (i = 0; i < ARRAY_SIZE(mt76x0_rf_bw_switch_tab); i++) {
		const struct mt76x0_rf_switch_item *item = &mt76x0_rf_bw_switch_tab[i];

		if (item->bw_band == RF_BW_20)
			mt76x0_rf_wr(dev, item->rf_bank_reg, item->value);
		else if (((RF_G_BAND | RF_BW_20) & item->bw_band) == (RF_G_BAND | RF_BW_20))
			mt76x0_rf_wr(dev, item->rf_bank_reg, item->value);
	}

	for (i = 0; i < ARRAY_SIZE(mt76x0_rf_band_switch_tab); i++) {
		if (mt76x0_rf_band_switch_tab[i].bw_band & RF_G_BAND) {
			mt76x0_rf_wr(dev,
				     mt76x0_rf_band_switch_tab[i].rf_bank_reg,
				     mt76x0_rf_band_switch_tab[i].value);
		}
	}

	/*
	   Frequency calibration
	   E1: B0.R22<6:0>: xo_cxo<6:0>
	   E2: B0.R21<0>: xo_cxo<0>, B0.R22<7:0>: xo_cxo<8:1>
	 */
	mt76x0_rf_wr(dev, MT_RF(0, 22),
		     min_t(u8, dev->cal.rx.freq_offset, 0xbf));
	val = mt76x0_rf_rr(dev, MT_RF(0, 22));

	/* Reset procedure DAC during power-up:
	 * - set B0.R73<7>
	 * - clear B0.R73<7>
	 * - set B0.R73<7>
	 */
	mt76x0_rf_set(dev, MT_RF(0, 73), BIT(7));
	mt76x0_rf_clear(dev, MT_RF(0, 73), BIT(7));
	mt76x0_rf_set(dev, MT_RF(0, 73), BIT(7));

	/* vcocal_en: initiate VCO calibration (reset after completion)) */
	mt76x0_rf_set(dev, MT_RF(0, 4), 0x80);
}

void mt76x0_phy_init(struct mt76x02_dev *dev)
{
	INIT_DELAYED_WORK(&dev->cal_work, mt76x0_phy_calibration_work);

	mt76x0_phy_ant_select(dev);
	mt76x0_phy_rf_init(dev);
	mt76x02_phy_set_rxpath(dev);
	mt76x02_phy_set_txdac(dev);
}
