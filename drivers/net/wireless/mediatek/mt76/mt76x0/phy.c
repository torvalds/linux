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

#include "mt76x0.h"
#include "mcu.h"
#include "eeprom.h"
#include "trace.h"
#include "phy.h"
#include "initvals.h"
#include "initvals_phy.h"

#include <linux/etherdevice.h>

static int
mt76x0_rf_csr_wr(struct mt76x0_dev *dev, u32 offset, u8 value)
{
	int ret = 0;
	u8 bank, reg;

	if (test_bit(MT76_REMOVED, &dev->mt76.state))
		return -ENODEV;

	bank = MT_RF_BANK(offset);
	reg = MT_RF_REG(offset);

	if (WARN_ON_ONCE(reg > 64) || WARN_ON_ONCE(bank) > 8)
		return -EINVAL;

	mutex_lock(&dev->reg_atomic_mutex);

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
	trace_mt76x0_rf_write(&dev->mt76, bank, offset, value);
out:
	mutex_unlock(&dev->reg_atomic_mutex);

	if (ret < 0)
		dev_err(dev->mt76.dev, "Error: RF write %d:%d failed:%d!!\n",
			bank, reg, ret);

	return ret;
}

static int
mt76x0_rf_csr_rr(struct mt76x0_dev *dev, u32 offset)
{
	int ret = -ETIMEDOUT;
	u32 val;
	u8 bank, reg;

	if (test_bit(MT76_REMOVED, &dev->mt76.state))
		return -ENODEV;

	bank = MT_RF_BANK(offset);
	reg = MT_RF_REG(offset);

	if (WARN_ON_ONCE(reg > 64) || WARN_ON_ONCE(bank) > 8)
		return -EINVAL;

	mutex_lock(&dev->reg_atomic_mutex);

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
	    FIELD_GET(MT_RF_CSR_CFG_REG_BANK, val) == bank) {
		ret = FIELD_GET(MT_RF_CSR_CFG_DATA, val);
		trace_mt76x0_rf_read(&dev->mt76, bank, offset, ret);
	}
out:
	mutex_unlock(&dev->reg_atomic_mutex);

	if (ret < 0)
		dev_err(dev->mt76.dev, "Error: RF read %d:%d failed:%d!!\n",
			bank, reg, ret);

	return ret;
}

static int
rf_wr(struct mt76x0_dev *dev, u32 offset, u8 val)
{
	if (test_bit(MT76_STATE_MCU_RUNNING, &dev->mt76.state)) {
		struct mt76_reg_pair pair = {
			.reg = offset,
			.value = val,
		};

		return mt76_wr_rp(dev, MT_MCU_MEMMAP_RF, &pair, 1);
	} else {
		WARN_ON_ONCE(1);
		return mt76x0_rf_csr_wr(dev, offset, val);
	}
}

static int
rf_rr(struct mt76x0_dev *dev, u32 offset)
{
	int ret;
	u32 val;

	if (test_bit(MT76_STATE_MCU_RUNNING, &dev->mt76.state)) {
		struct mt76_reg_pair pair = {
			.reg = offset,
		};

		ret = mt76_rd_rp(dev, MT_MCU_MEMMAP_RF, &pair, 1);
		val = pair.value;
	} else {
		WARN_ON_ONCE(1);
		ret = val = mt76x0_rf_csr_rr(dev, offset);
	}

	return (ret < 0) ? ret : val;
}

static int
rf_rmw(struct mt76x0_dev *dev, u32 offset, u8 mask, u8 val)
{
	int ret;

	ret = rf_rr(dev, offset);
	if (ret < 0)
		return ret;
	val |= ret & ~mask;
	ret = rf_wr(dev, offset, val);
	if (ret)
		return ret;

	return val;
}

static int
rf_set(struct mt76x0_dev *dev, u32 offset, u8 val)
{
	return rf_rmw(dev, offset, 0, val);
}

#if 0
static int
rf_clear(struct mt76x0_dev *dev, u32 offset, u8 mask)
{
	return rf_rmw(dev, offset, mask, 0);
}
#endif

#define RF_RANDOM_WRITE(dev, tab)		\
	mt76_wr_rp(dev, MT_MCU_MEMMAP_RF,	\
		   tab, ARRAY_SIZE(tab))

int mt76x0_wait_bbp_ready(struct mt76x0_dev *dev)
{
	int i = 20;
	u32 val;

	do {
		val = mt76_rr(dev, MT_BBP(CORE, 0));
		printk("BBP version %08x\n", val);
		if (val && ~val)
			break;
	} while (--i);

	if (!i) {
		dev_err(dev->mt76.dev, "Error: BBP is not ready\n");
		return -EIO;
	}

	return 0;
}

static void
mt76x0_bbp_set_ctrlch(struct mt76x0_dev *dev, enum nl80211_chan_width width,
		      u8 ctrl)
{
	int core_val, agc_val;

	switch (width) {
	case NL80211_CHAN_WIDTH_80:
		core_val = 3;
		agc_val = 7;
		break;
	case NL80211_CHAN_WIDTH_40:
		core_val = 2;
		agc_val = 3;
		break;
	default:
		core_val = 0;
		agc_val = 1;
		break;
	}

	mt76_rmw_field(dev, MT_BBP(CORE, 1), MT_BBP_CORE_R1_BW, core_val);
	mt76_rmw_field(dev, MT_BBP(AGC, 0), MT_BBP_AGC_R0_BW, agc_val);
	mt76_rmw_field(dev, MT_BBP(AGC, 0), MT_BBP_AGC_R0_CTRL_CHAN, ctrl);
	mt76_rmw_field(dev, MT_BBP(TXBE, 0), MT_BBP_TXBE_R0_CTRL_CHAN, ctrl);
}

int mt76x0_phy_get_rssi(struct mt76x0_dev *dev, struct mt76x02_rxwi *rxwi)
{
	struct mt76x0_caldata *caldata = &dev->caldata;

	return rxwi->rssi[0] + caldata->rssi_offset[0] - caldata->lna_gain;
}

static void mt76x0_vco_cal(struct mt76x0_dev *dev, u8 channel)
{
	u8 val;

	val = rf_rr(dev, MT_RF(0, 4));
	if ((val & 0x70) != 0x30)
		return;

	/*
	 * Calibration Mode - Open loop, closed loop, and amplitude:
	 * B0.R06.[0]: 1
	 * B0.R06.[3:1] bp_close_code: 100
	 * B0.R05.[7:0] bp_open_code: 0x0
	 * B0.R04.[2:0] cal_bits: 000
	 * B0.R03.[2:0] startup_time: 011
	 * B0.R03.[6:4] settle_time:
	 *  80MHz channel: 110
	 *  40MHz channel: 101
	 *  20MHz channel: 100
	 */
	val = rf_rr(dev, MT_RF(0, 6));
	val &= ~0xf;
	val |= 0x09;
	rf_wr(dev, MT_RF(0, 6), val);

	val = rf_rr(dev, MT_RF(0, 5));
	if (val != 0)
		rf_wr(dev, MT_RF(0, 5), 0x0);

	val = rf_rr(dev, MT_RF(0, 4));
	val &= ~0x07;
	rf_wr(dev, MT_RF(0, 4), val);

	val = rf_rr(dev, MT_RF(0, 3));
	val &= ~0x77;
	if (channel == 1 || channel == 7 || channel == 9 || channel >= 13) {
		val |= 0x63;
	} else if (channel == 3 || channel == 4 || channel == 10) {
		val |= 0x53;
	} else if (channel == 2 || channel == 5 || channel == 6 ||
		   channel == 8 || channel == 11 || channel == 12) {
		val |= 0x43;
	} else {
		WARN(1, "Unknown channel %u\n", channel);
		return;
	}
	rf_wr(dev, MT_RF(0, 3), val);

	/* TODO replace by mt76x0_rf_set(dev, MT_RF(0, 4), BIT(7)); */
	val = rf_rr(dev, MT_RF(0, 4));
	val = ((val & ~(0x80)) | 0x80);
	rf_wr(dev, MT_RF(0, 4), val);

	msleep(2);
}

static void
mt76x0_mac_set_ctrlch(struct mt76x0_dev *dev, bool primary_upper)
{
	mt76_rmw_field(dev, MT_TX_BAND_CFG, MT_TX_BAND_CFG_UPPER_40M,
		       primary_upper);
}

static void
mt76x0_phy_set_band(struct mt76x0_dev *dev, enum nl80211_band band)
{
	switch (band) {
	case NL80211_BAND_2GHZ:
		RF_RANDOM_WRITE(dev, mt76x0_rf_2g_channel_0_tab);

		rf_wr(dev, MT_RF(5, 0), 0x45);
		rf_wr(dev, MT_RF(6, 0), 0x44);

		mt76_set(dev, MT_TX_BAND_CFG, MT_TX_BAND_CFG_2G);
		mt76_clear(dev, MT_TX_BAND_CFG, MT_TX_BAND_CFG_5G);

		mt76_wr(dev, MT_TX_ALC_VGA3, 0x00050007);
		mt76_wr(dev, MT_TX0_RF_GAIN_CORR, 0x003E0002);
		break;
	case NL80211_BAND_5GHZ:
		RF_RANDOM_WRITE(dev, mt76x0_rf_5g_channel_0_tab);

		rf_wr(dev, MT_RF(5, 0), 0x44);
		rf_wr(dev, MT_RF(6, 0), 0x45);

		mt76_clear(dev, MT_TX_BAND_CFG, MT_TX_BAND_CFG_2G);
		mt76_set(dev, MT_TX_BAND_CFG, MT_TX_BAND_CFG_5G);

		mt76_wr(dev, MT_TX_ALC_VGA3, 0x00000005);
		mt76_wr(dev, MT_TX0_RF_GAIN_CORR, 0x01010102);
		break;
	default:
		break;
	}
}

static void
mt76x0_phy_set_chan_rf_params(struct mt76x0_dev *dev, u8 channel, u16 rf_bw_band)
{
	u16 rf_band = rf_bw_band & 0xff00;
	u16 rf_bw = rf_bw_band & 0x00ff;
	enum nl80211_band band;
	u32 mac_reg;
	u8 rf_val;
	int i;
	bool bSDM = false;
	const struct mt76x0_freq_item *freq_item;

	for (i = 0; i < ARRAY_SIZE(mt76x0_sdm_channel); i++) {
		if (channel == mt76x0_sdm_channel[i]) {
			bSDM = true;
			break;
		}
	}

	for (i = 0; i < ARRAY_SIZE(mt76x0_frequency_plan); i++) {
		if (channel == mt76x0_frequency_plan[i].channel) {
			rf_band = mt76x0_frequency_plan[i].band;

			if (bSDM)
				freq_item = &(mt76x0_sdm_frequency_plan[i]);
			else
				freq_item = &(mt76x0_frequency_plan[i]);

			rf_wr(dev, MT_RF(0, 37), freq_item->pllR37);
			rf_wr(dev, MT_RF(0, 36), freq_item->pllR36);
			rf_wr(dev, MT_RF(0, 35), freq_item->pllR35);
			rf_wr(dev, MT_RF(0, 34), freq_item->pllR34);
			rf_wr(dev, MT_RF(0, 33), freq_item->pllR33);

			rf_val = rf_rr(dev, MT_RF(0, 32));
			rf_val &= ~0xE0;
			rf_val |= freq_item->pllR32_b7b5;
			rf_wr(dev, MT_RF(0, 32), rf_val);

			/* R32<4:0> pll_den: (Denomina - 8) */
			rf_val = rf_rr(dev, MT_RF(0, 32));
			rf_val &= ~0x1F;
			rf_val |= freq_item->pllR32_b4b0;
			rf_wr(dev, MT_RF(0, 32), rf_val);

			/* R31<7:5> */
			rf_val = rf_rr(dev, MT_RF(0, 31));
			rf_val &= ~0xE0;
			rf_val |= freq_item->pllR31_b7b5;
			rf_wr(dev, MT_RF(0, 31), rf_val);

			/* R31<4:0> pll_k(Nominator) */
			rf_val = rf_rr(dev, MT_RF(0, 31));
			rf_val &= ~0x1F;
			rf_val |= freq_item->pllR31_b4b0;
			rf_wr(dev, MT_RF(0, 31), rf_val);

			/* R30<7> sdm_reset_n */
			rf_val = rf_rr(dev, MT_RF(0, 30));
			rf_val &= ~0x80;
			if (bSDM) {
				rf_wr(dev, MT_RF(0, 30), rf_val);
				rf_val |= 0x80;
				rf_wr(dev, MT_RF(0, 30), rf_val);
			} else {
				rf_val |= freq_item->pllR30_b7;
				rf_wr(dev, MT_RF(0, 30), rf_val);
			}

			/* R30<6:2> sdmmash_prbs,sin */
			rf_val = rf_rr(dev, MT_RF(0, 30));
			rf_val &= ~0x7C;
			rf_val |= freq_item->pllR30_b6b2;
			rf_wr(dev, MT_RF(0, 30), rf_val);

			/* R30<1> sdm_bp */
			rf_val = rf_rr(dev, MT_RF(0, 30));
			rf_val &= ~0x02;
			rf_val |= (freq_item->pllR30_b1 << 1);
			rf_wr(dev, MT_RF(0, 30), rf_val);

			/* R30<0> R29<7:0> (hex) pll_n */
			rf_val = freq_item->pll_n & 0x00FF;
			rf_wr(dev, MT_RF(0, 29), rf_val);

			rf_val = rf_rr(dev, MT_RF(0, 30));
			rf_val &= ~0x1;
			rf_val |= ((freq_item->pll_n >> 8) & 0x0001);
			rf_wr(dev, MT_RF(0, 30), rf_val);

			/* R28<7:6> isi_iso */
			rf_val = rf_rr(dev, MT_RF(0, 28));
			rf_val &= ~0xC0;
			rf_val |= freq_item->pllR28_b7b6;
			rf_wr(dev, MT_RF(0, 28), rf_val);

			/* R28<5:4> pfd_dly */
			rf_val = rf_rr(dev, MT_RF(0, 28));
			rf_val &= ~0x30;
			rf_val |= freq_item->pllR28_b5b4;
			rf_wr(dev, MT_RF(0, 28), rf_val);

			/* R28<3:2> clksel option */
			rf_val = rf_rr(dev, MT_RF(0, 28));
			rf_val &= ~0x0C;
			rf_val |= freq_item->pllR28_b3b2;
			rf_wr(dev, MT_RF(0, 28), rf_val);

			/* R28<1:0> R27<7:0> R26<7:0> (hex) sdm_k */
			rf_val = freq_item->pll_sdm_k & 0x000000FF;
			rf_wr(dev, MT_RF(0, 26), rf_val);

			rf_val = ((freq_item->pll_sdm_k >> 8) & 0x000000FF);
			rf_wr(dev, MT_RF(0, 27), rf_val);

			rf_val = rf_rr(dev, MT_RF(0, 28));
			rf_val &= ~0x3;
			rf_val |= ((freq_item->pll_sdm_k >> 16) & 0x0003);
			rf_wr(dev, MT_RF(0, 28), rf_val);

			/* R24<1:0> xo_div */
			rf_val = rf_rr(dev, MT_RF(0, 24));
			rf_val &= ~0x3;
			rf_val |= freq_item->pllR24_b1b0;
			rf_wr(dev, MT_RF(0, 24), rf_val);

			break;
		}
	}

	for (i = 0; i < ARRAY_SIZE(mt76x0_rf_bw_switch_tab); i++) {
		if (rf_bw == mt76x0_rf_bw_switch_tab[i].bw_band) {
			rf_wr(dev, mt76x0_rf_bw_switch_tab[i].rf_bank_reg,
				   mt76x0_rf_bw_switch_tab[i].value);
		} else if ((rf_bw == (mt76x0_rf_bw_switch_tab[i].bw_band & 0xFF)) &&
			   (rf_band & mt76x0_rf_bw_switch_tab[i].bw_band)) {
			rf_wr(dev, mt76x0_rf_bw_switch_tab[i].rf_bank_reg,
				   mt76x0_rf_bw_switch_tab[i].value);
		}
	}

	for (i = 0; i < ARRAY_SIZE(mt76x0_rf_band_switch_tab); i++) {
		if (mt76x0_rf_band_switch_tab[i].bw_band & rf_band) {
			rf_wr(dev, mt76x0_rf_band_switch_tab[i].rf_bank_reg,
				   mt76x0_rf_band_switch_tab[i].value);
		}
	}

	mac_reg = mt76_rr(dev, MT_RF_MISC);
	mac_reg &= ~0xC; /* Clear 0x518[3:2] */
	mt76_wr(dev, MT_RF_MISC, mac_reg);

	band = (rf_band & RF_G_BAND) ? NL80211_BAND_2GHZ : NL80211_BAND_5GHZ;
	if (mt76x02_ext_pa_enabled(&dev->mt76, band)) {
		/*
			MT_RF_MISC (offset: 0x0518)
			[2]1'b1: enable external A band PA, 1'b0: disable external A band PA
			[3]1'b1: enable external G band PA, 1'b0: disable external G band PA
		*/
		if (rf_band & RF_A_BAND) {
			mac_reg = mt76_rr(dev, MT_RF_MISC);
			mac_reg |= 0x4;
			mt76_wr(dev, MT_RF_MISC, mac_reg);
		} else {
			mac_reg = mt76_rr(dev, MT_RF_MISC);
			mac_reg |= 0x8;
			mt76_wr(dev, MT_RF_MISC, mac_reg);
		}

		/* External PA */
		for (i = 0; i < ARRAY_SIZE(mt76x0_rf_ext_pa_tab); i++)
			if (mt76x0_rf_ext_pa_tab[i].bw_band & rf_band)
				rf_wr(dev, mt76x0_rf_ext_pa_tab[i].rf_bank_reg,
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
mt76x0_phy_set_chan_bbp_params(struct mt76x0_dev *dev, u8 channel, u16 rf_bw_band)
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
			gain -= dev->caldata.lna_gain * 2;
			val &= ~MT_BBP_AGC_GAIN;
			val |= FIELD_PREP(MT_BBP_AGC_GAIN, gain);
			mt76_wr(dev, pair->reg, val);
		} else {
			mt76_wr(dev, pair->reg, pair->value);
		}
	}
}

#if 0
static void
mt76x0_extra_power_over_mac(struct mt76x0_dev *dev)
{
	u32 val;

	val = ((mt76_rr(dev, MT_TX_PWR_CFG_1) & 0x00003f00) >> 8);
	val |= ((mt76_rr(dev, MT_TX_PWR_CFG_2) & 0x00003f00) << 8);
	mt76_wr(dev, MT_TX_PWR_CFG_7, val);

	/* TODO: fix VHT */
	val = ((mt76_rr(dev, MT_TX_PWR_CFG_3) & 0x0000ff00) >> 8);
	mt76_wr(dev, MT_TX_PWR_CFG_8, val);

	val = ((mt76_rr(dev, MT_TX_PWR_CFG_4) & 0x0000ff00) >> 8);
	mt76_wr(dev, MT_TX_PWR_CFG_9, val);
}

static void
mt76x0_phy_set_tx_power(struct mt76x0_dev *dev, u8 channel, u8 rf_bw_band)
{
	u32 val;
	int i;
	int bw = (rf_bw_band & RF_BW_20) ? 0 : 1;

	for (i = 0; i < 4; i++) {
		if (channel <= 14)
			val = dev->ee->tx_pwr_cfg_2g[i][bw];
		else
			val = dev->ee->tx_pwr_cfg_5g[i][bw];

		mt76_wr(dev, MT_TX_PWR_CFG_0 + 4*i, val);
	}

	mt76x0_extra_power_over_mac(dev);
}
#endif

static void
mt76x0_bbp_set_bw(struct mt76x0_dev *dev, enum nl80211_chan_width width)
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

	mt76x02_mcu_function_select(&dev->mt76, BW_SETTING, bw, false);
}

static void
mt76x0_phy_set_chan_pwr(struct mt76x0_dev *dev, u8 channel)
{
	static const int mt76x0_tx_pwr_ch_list[] = {
		1,2,3,4,5,6,7,8,9,10,11,12,13,14,
		36,38,40,44,46,48,52,54,56,60,62,64,
		100,102,104,108,110,112,116,118,120,124,126,128,132,134,136,140,
		149,151,153,157,159,161,165,167,169,171,173,
		42,58,106,122,155
	};
	int i;
	u32 val;

	for (i = 0; i < ARRAY_SIZE(mt76x0_tx_pwr_ch_list); i++)
		if (mt76x0_tx_pwr_ch_list[i] == channel)
			break;

	if (WARN_ON(i == ARRAY_SIZE(mt76x0_tx_pwr_ch_list)))
		return;

	val = mt76_rr(dev, MT_TX_ALC_CFG_0);
	val &= ~0x3f3f;
	val |= dev->ee->tx_pwr_per_chan[i];
	val |= 0x2f2f << 16;
	mt76_wr(dev, MT_TX_ALC_CFG_0, val);
}

static int
__mt76x0_phy_set_channel(struct mt76x0_dev *dev,
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

	mt76x0_bbp_set_bw(dev, chandef->width);
	mt76x0_bbp_set_ctrlch(dev, chandef->width, ch_group_index);
	mt76x0_mac_set_ctrlch(dev, ch_group_index & 1);

	mt76_rmw(dev, MT_EXT_CCA_CFG,
		 (MT_EXT_CCA_CFG_CCA0 |
		  MT_EXT_CCA_CFG_CCA1 |
		  MT_EXT_CCA_CFG_CCA2 |
		  MT_EXT_CCA_CFG_CCA3 |
		  MT_EXT_CCA_CFG_CCA_MASK),
		 ext_cca_chan[ch_group_index]);

	mt76x0_phy_set_band(dev, chandef->chan->band);
	mt76x0_phy_set_chan_rf_params(dev, channel, rf_bw_band);
	mt76x0_read_rx_gain(dev);

	/* set Japan Tx filter at channel 14 */
	val = mt76_rr(dev, MT_BBP(CORE, 1));
	if (channel == 14)
		val |= 0x20;
	else
		val &= ~0x20;
	mt76_wr(dev, MT_BBP(CORE, 1), val);

	mt76x0_phy_set_chan_bbp_params(dev, channel, rf_bw_band);

	/* Vendor driver don't do it */
	/* mt76x0_phy_set_tx_power(dev, channel, rf_bw_band); */

	mt76x0_vco_cal(dev, channel);
	if (scan)
		mt76x02_mcu_calibrate(&dev->mt76, MCU_CAL_RXDCOC, 1, false);

	mt76x0_phy_set_chan_pwr(dev, channel);

	dev->mt76.chandef = *chandef;
	return 0;
}

int mt76x0_phy_set_channel(struct mt76x0_dev *dev,
			   struct cfg80211_chan_def *chandef)
{
	int ret;

	mutex_lock(&dev->hw_atomic_mutex);
	ret = __mt76x0_phy_set_channel(dev, chandef);
	mutex_unlock(&dev->hw_atomic_mutex);

	return ret;
}

void mt76x0_phy_recalibrate_after_assoc(struct mt76x0_dev *dev)
{
	u32 tx_alc, reg_val;
	u8 channel = dev->mt76.chandef.chan->hw_value;
	int is_5ghz = (dev->mt76.chandef.chan->band == NL80211_BAND_5GHZ) ? 1 : 0;

	mt76x02_mcu_calibrate(&dev->mt76, MCU_CAL_R, 0, false);

	mt76x0_vco_cal(dev, channel);

	tx_alc = mt76_rr(dev, MT_TX_ALC_CFG_0);
	mt76_wr(dev, MT_TX_ALC_CFG_0, 0);
	usleep_range(500, 700);

	reg_val = mt76_rr(dev, 0x2124);
	reg_val &= 0xffffff7e;
	mt76_wr(dev, 0x2124, reg_val);

	mt76x02_mcu_calibrate(&dev->mt76, MCU_CAL_RXDCOC, 0, false);

	mt76x02_mcu_calibrate(&dev->mt76, MCU_CAL_LC, is_5ghz, false);
	mt76x02_mcu_calibrate(&dev->mt76, MCU_CAL_LOFT, is_5ghz, false);
	mt76x02_mcu_calibrate(&dev->mt76, MCU_CAL_TXIQ, is_5ghz, false);
	mt76x02_mcu_calibrate(&dev->mt76, MCU_CAL_TX_GROUP_DELAY,
			      is_5ghz, false);
	mt76x02_mcu_calibrate(&dev->mt76, MCU_CAL_RXIQ, is_5ghz, false);
	mt76x02_mcu_calibrate(&dev->mt76, MCU_CAL_RX_GROUP_DELAY,
			      is_5ghz, false);

	mt76_wr(dev, 0x2124, reg_val);
	mt76_wr(dev, MT_TX_ALC_CFG_0, tx_alc);
	msleep(100);

	mt76x02_mcu_calibrate(&dev->mt76, MCU_CAL_RXDCOC, 1, false);
}

void mt76x0_agc_save(struct mt76x0_dev *dev)
{
	/* Only one RX path */
	dev->agc_save = FIELD_GET(MT_BBP_AGC_GAIN, mt76_rr(dev, MT_BBP(AGC, 8)));
}

void mt76x0_agc_restore(struct mt76x0_dev *dev)
{
	mt76_rmw_field(dev, MT_BBP(AGC, 8), MT_BBP_AGC_GAIN, dev->agc_save);
}

static void mt76x0_temp_sensor(struct mt76x0_dev *dev)
{
	u8 rf_b7_73, rf_b0_66, rf_b0_67;
	int cycle, temp;
	u32 val;
	s32 sval;

	rf_b7_73 = rf_rr(dev, MT_RF(7, 73));
	rf_b0_66 = rf_rr(dev, MT_RF(0, 66));
	rf_b0_67 = rf_rr(dev, MT_RF(0, 73));

	rf_wr(dev, MT_RF(7, 73), 0x02);
	rf_wr(dev, MT_RF(0, 66), 0x23);
	rf_wr(dev, MT_RF(0, 73), 0x01);

	mt76_wr(dev, MT_BBP(CORE, 34), 0x00080055);

	for (cycle = 0; cycle < 2000; cycle++) {
		val = mt76_rr(dev, MT_BBP(CORE, 34));
		if (!(val & 0x10))
			break;
		udelay(3);
	}

	if (cycle >= 2000) {
		val &= 0x10;
		mt76_wr(dev, MT_BBP(CORE, 34), val);
		goto done;
	}

	sval = mt76_rr(dev, MT_BBP(CORE, 35)) & 0xff;
	if (!(sval & 0x80))
		sval &= 0x7f; /* Positive */
	else
		sval |= 0xffffff00; /* Negative */

	temp = (35 * (sval - dev->ee->temp_off))/ 10 + 25;

done:
	rf_wr(dev, MT_RF(7, 73), rf_b7_73);
	rf_wr(dev, MT_RF(0, 66), rf_b0_66);
	rf_wr(dev, MT_RF(0, 73), rf_b0_67);
}

static void mt76x0_dynamic_vga_tuning(struct mt76x0_dev *dev)
{
	u32 val, init_vga;

	init_vga = (dev->mt76.chandef.chan->band == NL80211_BAND_5GHZ) ? 0x54 : 0x4E;
	if (dev->avg_rssi > -60)
		init_vga -= 0x20;
	else if (dev->avg_rssi > -70)
		init_vga -= 0x10;

	val = mt76_rr(dev, MT_BBP(AGC, 8));
	val &= 0xFFFF80FF;
	val |= init_vga << 8;
	mt76_wr(dev, MT_BBP(AGC,8), val);
}

static void mt76x0_phy_calibrate(struct work_struct *work)
{
	struct mt76x0_dev *dev = container_of(work, struct mt76x0_dev,
					    cal_work.work);

	mt76x0_dynamic_vga_tuning(dev);
	mt76x0_temp_sensor(dev);

	ieee80211_queue_delayed_work(dev->mt76.hw, &dev->cal_work,
				     MT_CALIBRATE_INTERVAL);
}

void mt76x0_phy_con_cal_onoff(struct mt76x0_dev *dev,
			       struct ieee80211_bss_conf *info)
{
	/* Start/stop collecting beacon data */
	spin_lock_bh(&dev->con_mon_lock);
	ether_addr_copy(dev->ap_bssid, info->bssid);
	dev->avg_rssi = 0;
	dev->bcn_freq_off = MT_FREQ_OFFSET_INVALID;
	spin_unlock_bh(&dev->con_mon_lock);
}

static void
mt76x0_set_rx_chains(struct mt76x0_dev *dev)
{
	u32 val;

	val = mt76_rr(dev, MT_BBP(AGC, 0));
	val &= ~(BIT(3) | BIT(4));

	if (dev->chainmask & BIT(1))
		val |= BIT(3);

	mt76_wr(dev, MT_BBP(AGC, 0), val);

	mb();
	val = mt76_rr(dev, MT_BBP(AGC, 0));
}

static void
mt76x0_set_tx_dac(struct mt76x0_dev *dev)
{
	if (dev->chainmask & BIT(1))
		mt76_set(dev, MT_BBP(TXBE, 5), 3);
	else
		mt76_clear(dev, MT_BBP(TXBE, 5), 3);
}

static void
mt76x0_rf_init(struct mt76x0_dev *dev)
{
	int i;
	u8 val;

	RF_RANDOM_WRITE(dev, mt76x0_rf_central_tab);
	RF_RANDOM_WRITE(dev, mt76x0_rf_2g_channel_0_tab);
	RF_RANDOM_WRITE(dev, mt76x0_rf_5g_channel_0_tab);
	RF_RANDOM_WRITE(dev, mt76x0_rf_vga_channel_0_tab);

	for (i = 0; i < ARRAY_SIZE(mt76x0_rf_bw_switch_tab); i++) {
		const struct mt76x0_rf_switch_item *item = &mt76x0_rf_bw_switch_tab[i];

		if (item->bw_band == RF_BW_20)
			rf_wr(dev, item->rf_bank_reg, item->value);
		else if (((RF_G_BAND | RF_BW_20) & item->bw_band) == (RF_G_BAND | RF_BW_20))
			rf_wr(dev, item->rf_bank_reg, item->value);
	}

	for (i = 0; i < ARRAY_SIZE(mt76x0_rf_band_switch_tab); i++) {
		if (mt76x0_rf_band_switch_tab[i].bw_band & RF_G_BAND) {
			rf_wr(dev,
			      mt76x0_rf_band_switch_tab[i].rf_bank_reg,
			      mt76x0_rf_band_switch_tab[i].value);
		}
	}

	/*
	   Frequency calibration
	   E1: B0.R22<6:0>: xo_cxo<6:0>
	   E2: B0.R21<0>: xo_cxo<0>, B0.R22<7:0>: xo_cxo<8:1>
	 */
	rf_wr(dev, MT_RF(0, 22), min_t(u8, dev->ee->rf_freq_off, 0xBF));
	val = rf_rr(dev, MT_RF(0, 22));

	/*
	   Reset the DAC (Set B0.R73<7>=1, then set B0.R73<7>=0, and then set B0.R73<7>) during power up.
	 */
	val = rf_rr(dev, MT_RF(0, 73));
	val |= 0x80;
	rf_wr(dev, MT_RF(0, 73), val);
	val &= ~0x80;
	rf_wr(dev, MT_RF(0, 73), val);
	val |= 0x80;
	rf_wr(dev, MT_RF(0, 73), val);

	/*
	   vcocal_en (initiate VCO calibration (reset after completion)) - It should be at the end of RF configuration.
	 */
	rf_set(dev, MT_RF(0, 4), 0x80);
}

static void mt76x0_ant_select(struct mt76x0_dev *dev)
{
	/* Single antenna mode. */
	mt76_rmw(dev, MT_WLAN_FUN_CTRL, BIT(5), BIT(6));
	mt76_clear(dev, MT_CMB_CTRL, BIT(14) | BIT(12));
	mt76_clear(dev, MT_COEXCFG0, BIT(2));
	mt76_rmw(dev, MT_COEXCFG3, BIT(5) | BIT(4) | BIT(3) | BIT(2), BIT(1));
}

void mt76x0_phy_init(struct mt76x0_dev *dev)
{
	INIT_DELAYED_WORK(&dev->cal_work, mt76x0_phy_calibrate);

	mt76x0_ant_select(dev);

	mt76x0_rf_init(dev);

	mt76x0_set_rx_chains(dev);
	mt76x0_set_tx_dac(dev);
}
