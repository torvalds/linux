/*
 * Copyright (c) 2008-2011 Atheros Communications Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/**
 * DOC: Programming Atheros 802.11n analog front end radios
 *
 * AR5416 MAC based PCI devices and AR518 MAC based PCI-Express
 * devices have either an external AR2133 analog front end radio for single
 * band 2.4 GHz communication or an AR5133 analog front end radio for dual
 * band 2.4 GHz / 5 GHz communication.
 *
 * All devices after the AR5416 and AR5418 family starting with the AR9280
 * have their analog front radios, MAC/BB and host PCIe/USB interface embedded
 * into a single-chip and require less programming.
 *
 * The following single-chips exist with a respective embedded radio:
 *
 * AR9280 - 11n dual-band 2x2 MIMO for PCIe
 * AR9281 - 11n single-band 1x2 MIMO for PCIe
 * AR9285 - 11n single-band 1x1 for PCIe
 * AR9287 - 11n single-band 2x2 MIMO for PCIe
 *
 * AR9220 - 11n dual-band 2x2 MIMO for PCI
 * AR9223 - 11n single-band 2x2 MIMO for PCI
 *
 * AR9287 - 11n single-band 1x1 MIMO for USB
 */

#include "hw.h"
#include "ar9002_phy.h"

/**
 * ar9002_hw_set_channel - set channel on single-chip device
 * @ah: atheros hardware structure
 * @chan:
 *
 * This is the function to change channel on single-chip devices, that is
 * all devices after ar9280.
 *
 * This function takes the channel value in MHz and sets
 * hardware channel value. Assumes writes have been enabled to analog bus.
 *
 * Actual Expression,
 *
 * For 2GHz channel,
 * Channel Frequency = (3/4) * freq_ref * (chansel[8:0] + chanfrac[16:0]/2^17)
 * (freq_ref = 40MHz)
 *
 * For 5GHz channel,
 * Channel Frequency = (3/2) * freq_ref * (chansel[8:0] + chanfrac[16:0]/2^10)
 * (freq_ref = 40MHz/(24>>amodeRefSel))
 */
static int ar9002_hw_set_channel(struct ath_hw *ah, struct ath9k_channel *chan)
{
	u16 bMode, fracMode, aModeRefSel = 0;
	u32 freq, ndiv, channelSel = 0, channelFrac = 0, reg32 = 0;
	struct chan_centers centers;
	u32 refDivA = 24;

	ath9k_hw_get_channel_centers(ah, chan, &centers);
	freq = centers.synth_center;

	reg32 = REG_READ(ah, AR_PHY_SYNTH_CONTROL);
	reg32 &= 0xc0000000;

	if (freq < 4800) { /* 2 GHz, fractional mode */
		u32 txctl;
		int regWrites = 0;

		bMode = 1;
		fracMode = 1;
		aModeRefSel = 0;
		channelSel = CHANSEL_2G(freq);

		if (AR_SREV_9287_11_OR_LATER(ah)) {
			if (freq == 2484) {
				/* Enable channel spreading for channel 14 */
				REG_WRITE_ARRAY(&ah->iniCckfirJapan2484,
						1, regWrites);
			} else {
				REG_WRITE_ARRAY(&ah->iniCckfirNormal,
						1, regWrites);
			}
		} else {
			txctl = REG_READ(ah, AR_PHY_CCK_TX_CTRL);
			if (freq == 2484) {
				/* Enable channel spreading for channel 14 */
				REG_WRITE(ah, AR_PHY_CCK_TX_CTRL,
					  txctl | AR_PHY_CCK_TX_CTRL_JAPAN);
			} else {
				REG_WRITE(ah, AR_PHY_CCK_TX_CTRL,
					  txctl & ~AR_PHY_CCK_TX_CTRL_JAPAN);
			}
		}
	} else {
		bMode = 0;
		fracMode = 0;

		switch (ah->eep_ops->get_eeprom(ah, EEP_FRAC_N_5G)) {
		case 0:
			if (IS_CHAN_HALF_RATE(chan) || IS_CHAN_QUARTER_RATE(chan))
				aModeRefSel = 0;
			else if ((freq % 20) == 0)
				aModeRefSel = 3;
			else if ((freq % 10) == 0)
				aModeRefSel = 2;
			if (aModeRefSel)
				break;
		case 1:
		default:
			aModeRefSel = 0;
			/*
			 * Enable 2G (fractional) mode for channels
			 * which are 5MHz spaced.
			 */
			fracMode = 1;
			refDivA = 1;
			channelSel = CHANSEL_5G(freq);

			/* RefDivA setting */
			ath9k_hw_analog_shift_rmw(ah, AR_AN_SYNTH9,
				      AR_AN_SYNTH9_REFDIVA,
				      AR_AN_SYNTH9_REFDIVA_S, refDivA);

		}

		if (!fracMode) {
			ndiv = (freq * (refDivA >> aModeRefSel)) / 60;
			channelSel = ndiv & 0x1ff;
			channelFrac = (ndiv & 0xfffffe00) * 2;
			channelSel = (channelSel << 17) | channelFrac;
		}
	}

	reg32 = reg32 |
	    (bMode << 29) |
	    (fracMode << 28) | (aModeRefSel << 26) | (channelSel);

	REG_WRITE(ah, AR_PHY_SYNTH_CONTROL, reg32);

	ah->curchan = chan;

	return 0;
}

/**
 * ar9002_hw_spur_mitigate - convert baseband spur frequency
 * @ah: atheros hardware structure
 * @chan:
 *
 * For single-chip solutions. Converts to baseband spur frequency given the
 * input channel frequency and compute register settings below.
 */
static void ar9002_hw_spur_mitigate(struct ath_hw *ah,
				    struct ath9k_channel *chan)
{
	int bb_spur = AR_NO_SPUR;
	int freq;
	int bin;
	int bb_spur_off, spur_subchannel_sd;
	int spur_freq_sd;
	int spur_delta_phase;
	int denominator;
	int tmp, newVal;
	int i;
	struct chan_centers centers;

	int cur_bb_spur;
	bool is2GHz = IS_CHAN_2GHZ(chan);

	ath9k_hw_get_channel_centers(ah, chan, &centers);
	freq = centers.synth_center;

	for (i = 0; i < AR_EEPROM_MODAL_SPURS; i++) {
		cur_bb_spur = ah->eep_ops->get_spur_channel(ah, i, is2GHz);

		if (AR_NO_SPUR == cur_bb_spur)
			break;

		if (is2GHz)
			cur_bb_spur = (cur_bb_spur / 10) + AR_BASE_FREQ_2GHZ;
		else
			cur_bb_spur = (cur_bb_spur / 10) + AR_BASE_FREQ_5GHZ;

		cur_bb_spur = cur_bb_spur - freq;

		if (IS_CHAN_HT40(chan)) {
			if ((cur_bb_spur > -AR_SPUR_FEEQ_BOUND_HT40) &&
			    (cur_bb_spur < AR_SPUR_FEEQ_BOUND_HT40)) {
				bb_spur = cur_bb_spur;
				break;
			}
		} else if ((cur_bb_spur > -AR_SPUR_FEEQ_BOUND_HT20) &&
			   (cur_bb_spur < AR_SPUR_FEEQ_BOUND_HT20)) {
			bb_spur = cur_bb_spur;
			break;
		}
	}

	if (AR_NO_SPUR == bb_spur) {
		REG_CLR_BIT(ah, AR_PHY_FORCE_CLKEN_CCK,
			    AR_PHY_FORCE_CLKEN_CCK_MRC_MUX);
		return;
	} else {
		REG_CLR_BIT(ah, AR_PHY_FORCE_CLKEN_CCK,
			    AR_PHY_FORCE_CLKEN_CCK_MRC_MUX);
	}

	bin = bb_spur * 320;

	tmp = REG_READ(ah, AR_PHY_TIMING_CTRL4(0));

	ENABLE_REGWRITE_BUFFER(ah);

	newVal = tmp | (AR_PHY_TIMING_CTRL4_ENABLE_SPUR_RSSI |
			AR_PHY_TIMING_CTRL4_ENABLE_SPUR_FILTER |
			AR_PHY_TIMING_CTRL4_ENABLE_CHAN_MASK |
			AR_PHY_TIMING_CTRL4_ENABLE_PILOT_MASK);
	REG_WRITE(ah, AR_PHY_TIMING_CTRL4(0), newVal);

	newVal = (AR_PHY_SPUR_REG_MASK_RATE_CNTL |
		  AR_PHY_SPUR_REG_ENABLE_MASK_PPM |
		  AR_PHY_SPUR_REG_MASK_RATE_SELECT |
		  AR_PHY_SPUR_REG_ENABLE_VIT_SPUR_RSSI |
		  SM(SPUR_RSSI_THRESH, AR_PHY_SPUR_REG_SPUR_RSSI_THRESH));
	REG_WRITE(ah, AR_PHY_SPUR_REG, newVal);

	if (IS_CHAN_HT40(chan)) {
		if (bb_spur < 0) {
			spur_subchannel_sd = 1;
			bb_spur_off = bb_spur + 10;
		} else {
			spur_subchannel_sd = 0;
			bb_spur_off = bb_spur - 10;
		}
	} else {
		spur_subchannel_sd = 0;
		bb_spur_off = bb_spur;
	}

	if (IS_CHAN_HT40(chan))
		spur_delta_phase =
			((bb_spur * 262144) /
			 10) & AR_PHY_TIMING11_SPUR_DELTA_PHASE;
	else
		spur_delta_phase =
			((bb_spur * 524288) /
			 10) & AR_PHY_TIMING11_SPUR_DELTA_PHASE;

	denominator = IS_CHAN_2GHZ(chan) ? 44 : 40;
	spur_freq_sd = ((bb_spur_off * 2048) / denominator) & 0x3ff;

	newVal = (AR_PHY_TIMING11_USE_SPUR_IN_AGC |
		  SM(spur_freq_sd, AR_PHY_TIMING11_SPUR_FREQ_SD) |
		  SM(spur_delta_phase, AR_PHY_TIMING11_SPUR_DELTA_PHASE));
	REG_WRITE(ah, AR_PHY_TIMING11, newVal);

	newVal = spur_subchannel_sd << AR_PHY_SFCORR_SPUR_SUBCHNL_SD_S;
	REG_WRITE(ah, AR_PHY_SFCORR_EXT, newVal);

	ar5008_hw_cmn_spur_mitigate(ah, chan, bin);

	REGWRITE_BUFFER_FLUSH(ah);
}

static void ar9002_olc_init(struct ath_hw *ah)
{
	u32 i;

	if (!OLC_FOR_AR9280_20_LATER)
		return;

	if (OLC_FOR_AR9287_10_LATER) {
		REG_SET_BIT(ah, AR_PHY_TX_PWRCTRL9,
				AR_PHY_TX_PWRCTRL9_RES_DC_REMOVAL);
		ath9k_hw_analog_shift_rmw(ah, AR9287_AN_TXPC0,
				AR9287_AN_TXPC0_TXPCMODE,
				AR9287_AN_TXPC0_TXPCMODE_S,
				AR9287_AN_TXPC0_TXPCMODE_TEMPSENSE);
		udelay(100);
	} else {
		for (i = 0; i < AR9280_TX_GAIN_TABLE_SIZE; i++)
			ah->originalGain[i] =
				MS(REG_READ(ah, AR_PHY_TX_GAIN_TBL1 + i * 4),
						AR_PHY_TX_GAIN);
		ah->PDADCdelta = 0;
	}
}

static u32 ar9002_hw_compute_pll_control(struct ath_hw *ah,
					 struct ath9k_channel *chan)
{
	int ref_div = 5;
	int pll_div = 0x2c;
	u32 pll;

	if (chan && IS_CHAN_5GHZ(chan) && !IS_CHAN_A_FAST_CLOCK(ah, chan)) {
		if (AR_SREV_9280_20(ah)) {
			ref_div = 10;
			pll_div = 0x50;
		} else {
			pll_div = 0x28;
		}
	}

	pll = SM(ref_div, AR_RTC_9160_PLL_REFDIV);
	pll |= SM(pll_div, AR_RTC_9160_PLL_DIV);

	if (chan && IS_CHAN_HALF_RATE(chan))
		pll |= SM(0x1, AR_RTC_9160_PLL_CLKSEL);
	else if (chan && IS_CHAN_QUARTER_RATE(chan))
		pll |= SM(0x2, AR_RTC_9160_PLL_CLKSEL);

	return pll;
}

static void ar9002_hw_do_getnf(struct ath_hw *ah,
			      int16_t nfarray[NUM_NF_READINGS])
{
	int16_t nf;

	nf = MS(REG_READ(ah, AR_PHY_CCA), AR9280_PHY_MINCCA_PWR);
	nfarray[0] = sign_extend32(nf, 8);

	nf = MS(REG_READ(ah, AR_PHY_EXT_CCA), AR9280_PHY_EXT_MINCCA_PWR);
	if (IS_CHAN_HT40(ah->curchan))
		nfarray[3] = sign_extend32(nf, 8);

	if (!(ah->rxchainmask & BIT(1)))
		return;

	nf = MS(REG_READ(ah, AR_PHY_CH1_CCA), AR9280_PHY_CH1_MINCCA_PWR);
	nfarray[1] = sign_extend32(nf, 8);

	nf = MS(REG_READ(ah, AR_PHY_CH1_EXT_CCA), AR9280_PHY_CH1_EXT_MINCCA_PWR);
	if (IS_CHAN_HT40(ah->curchan))
		nfarray[4] = sign_extend32(nf, 8);
}

static void ar9002_hw_set_nf_limits(struct ath_hw *ah)
{
	if (AR_SREV_9285(ah)) {
		ah->nf_2g.max = AR_PHY_CCA_MAX_GOOD_VAL_9285_2GHZ;
		ah->nf_2g.min = AR_PHY_CCA_MIN_GOOD_VAL_9285_2GHZ;
		ah->nf_2g.nominal = AR_PHY_CCA_NOM_VAL_9285_2GHZ;
	} else if (AR_SREV_9287(ah)) {
		ah->nf_2g.max = AR_PHY_CCA_MAX_GOOD_VAL_9287_2GHZ;
		ah->nf_2g.min = AR_PHY_CCA_MIN_GOOD_VAL_9287_2GHZ;
		ah->nf_2g.nominal = AR_PHY_CCA_NOM_VAL_9287_2GHZ;
	} else if (AR_SREV_9271(ah)) {
		ah->nf_2g.max = AR_PHY_CCA_MAX_GOOD_VAL_9271_2GHZ;
		ah->nf_2g.min = AR_PHY_CCA_MIN_GOOD_VAL_9271_2GHZ;
		ah->nf_2g.nominal = AR_PHY_CCA_NOM_VAL_9271_2GHZ;
	} else {
		ah->nf_2g.max = AR_PHY_CCA_MAX_GOOD_VAL_9280_2GHZ;
		ah->nf_2g.min = AR_PHY_CCA_MIN_GOOD_VAL_9280_2GHZ;
		ah->nf_2g.nominal = AR_PHY_CCA_NOM_VAL_9280_2GHZ;
		ah->nf_5g.max = AR_PHY_CCA_MAX_GOOD_VAL_9280_5GHZ;
		ah->nf_5g.min = AR_PHY_CCA_MIN_GOOD_VAL_9280_5GHZ;
		ah->nf_5g.nominal = AR_PHY_CCA_NOM_VAL_9280_5GHZ;
	}
}

static void ar9002_hw_antdiv_comb_conf_get(struct ath_hw *ah,
				   struct ath_hw_antcomb_conf *antconf)
{
	u32 regval;

	regval = REG_READ(ah, AR_PHY_MULTICHAIN_GAIN_CTL);
	antconf->main_lna_conf = (regval & AR_PHY_9285_ANT_DIV_MAIN_LNACONF) >>
				  AR_PHY_9285_ANT_DIV_MAIN_LNACONF_S;
	antconf->alt_lna_conf = (regval & AR_PHY_9285_ANT_DIV_ALT_LNACONF) >>
				 AR_PHY_9285_ANT_DIV_ALT_LNACONF_S;
	antconf->fast_div_bias = (regval & AR_PHY_9285_FAST_DIV_BIAS) >>
				  AR_PHY_9285_FAST_DIV_BIAS_S;
	antconf->lna1_lna2_switch_delta = -1;
	antconf->lna1_lna2_delta = -3;
	antconf->div_group = 0;
}

static void ar9002_hw_antdiv_comb_conf_set(struct ath_hw *ah,
				   struct ath_hw_antcomb_conf *antconf)
{
	u32 regval;

	regval = REG_READ(ah, AR_PHY_MULTICHAIN_GAIN_CTL);
	regval &= ~(AR_PHY_9285_ANT_DIV_MAIN_LNACONF |
		    AR_PHY_9285_ANT_DIV_ALT_LNACONF |
		    AR_PHY_9285_FAST_DIV_BIAS);
	regval |= ((antconf->main_lna_conf << AR_PHY_9285_ANT_DIV_MAIN_LNACONF_S)
		   & AR_PHY_9285_ANT_DIV_MAIN_LNACONF);
	regval |= ((antconf->alt_lna_conf << AR_PHY_9285_ANT_DIV_ALT_LNACONF_S)
		   & AR_PHY_9285_ANT_DIV_ALT_LNACONF);
	regval |= ((antconf->fast_div_bias << AR_PHY_9285_FAST_DIV_BIAS_S)
		   & AR_PHY_9285_FAST_DIV_BIAS);

	REG_WRITE(ah, AR_PHY_MULTICHAIN_GAIN_CTL, regval);
}

#ifdef CONFIG_ATH9K_BTCOEX_SUPPORT

static void ar9002_hw_set_bt_ant_diversity(struct ath_hw *ah, bool enable)
{
	struct ath_btcoex_hw *btcoex = &ah->btcoex_hw;
	u8 antdiv_ctrl1, antdiv_ctrl2;
	u32 regval;

	if (enable) {
		antdiv_ctrl1 = ATH_BT_COEX_ANTDIV_CONTROL1_ENABLE;
		antdiv_ctrl2 = ATH_BT_COEX_ANTDIV_CONTROL2_ENABLE;

		/*
		 * Don't disable BT ant to allow BB to control SWCOM.
		 */
		btcoex->bt_coex_mode2 &= (~(AR_BT_DISABLE_BT_ANT));
		REG_WRITE(ah, AR_BT_COEX_MODE2, btcoex->bt_coex_mode2);

		REG_WRITE(ah, AR_PHY_SWITCH_COM, ATH_BT_COEX_ANT_DIV_SWITCH_COM);
		REG_RMW(ah, AR_PHY_SWITCH_CHAIN_0, 0, 0xf0000000);
	} else {
		/*
		 * Disable antenna diversity, use LNA1 only.
		 */
		antdiv_ctrl1 = ATH_BT_COEX_ANTDIV_CONTROL1_FIXED_A;
		antdiv_ctrl2 = ATH_BT_COEX_ANTDIV_CONTROL2_FIXED_A;

		/*
		 * Disable BT Ant. to allow concurrent BT and WLAN receive.
		 */
		btcoex->bt_coex_mode2 |= AR_BT_DISABLE_BT_ANT;
		REG_WRITE(ah, AR_BT_COEX_MODE2, btcoex->bt_coex_mode2);

		/*
		 * Program SWCOM table to make sure RF switch always parks
		 * at BT side.
		 */
		REG_WRITE(ah, AR_PHY_SWITCH_COM, 0);
		REG_RMW(ah, AR_PHY_SWITCH_CHAIN_0, 0, 0xf0000000);
	}

	regval = REG_READ(ah, AR_PHY_MULTICHAIN_GAIN_CTL);
	regval &= (~(AR_PHY_9285_ANT_DIV_CTL_ALL));
        /*
	 * Clear ant_fast_div_bias [14:9] since for WB195,
	 * the main LNA is always LNA1.
	 */
	regval &= (~(AR_PHY_9285_FAST_DIV_BIAS));
	regval |= SM(antdiv_ctrl1, AR_PHY_9285_ANT_DIV_CTL);
	regval |= SM(antdiv_ctrl2, AR_PHY_9285_ANT_DIV_ALT_LNACONF);
	regval |= SM((antdiv_ctrl2 >> 2), AR_PHY_9285_ANT_DIV_MAIN_LNACONF);
	regval |= SM((antdiv_ctrl1 >> 1), AR_PHY_9285_ANT_DIV_ALT_GAINTB);
	regval |= SM((antdiv_ctrl1 >> 2), AR_PHY_9285_ANT_DIV_MAIN_GAINTB);
	REG_WRITE(ah, AR_PHY_MULTICHAIN_GAIN_CTL, regval);

	regval = REG_READ(ah, AR_PHY_CCK_DETECT);
	regval &= (~AR_PHY_CCK_DETECT_BB_ENABLE_ANT_FAST_DIV);
	regval |= SM((antdiv_ctrl1 >> 3), AR_PHY_CCK_DETECT_BB_ENABLE_ANT_FAST_DIV);
	REG_WRITE(ah, AR_PHY_CCK_DETECT, regval);
}

#endif

static void ar9002_hw_spectral_scan_config(struct ath_hw *ah,
				    struct ath_spec_scan *param)
{
	u32 repeat_bit;
	u8 count;

	if (!param->enabled) {
		REG_CLR_BIT(ah, AR_PHY_SPECTRAL_SCAN,
			    AR_PHY_SPECTRAL_SCAN_ENABLE);
		return;
	}
	REG_SET_BIT(ah, AR_PHY_RADAR_0, AR_PHY_RADAR_0_FFT_ENA);
	REG_SET_BIT(ah, AR_PHY_SPECTRAL_SCAN, AR_PHY_SPECTRAL_SCAN_ENABLE);

	if (AR_SREV_9280(ah))
		repeat_bit = AR_PHY_SPECTRAL_SCAN_SHORT_REPEAT;
	else
		repeat_bit = AR_PHY_SPECTRAL_SCAN_SHORT_REPEAT_KIWI;

	if (param->short_repeat)
		REG_SET_BIT(ah, AR_PHY_SPECTRAL_SCAN, repeat_bit);
	else
		REG_CLR_BIT(ah, AR_PHY_SPECTRAL_SCAN, repeat_bit);

	/* on AR92xx, the highest bit of count will make the the chip send
	 * spectral samples endlessly. Check if this really was intended,
	 * and fix otherwise.
	 */
	count = param->count;
	if (param->endless) {
		if (AR_SREV_9280(ah))
			count = 0x80;
		else
			count = 0;
	} else if (count & 0x80)
		count = 0x7f;
	else if (!count)
		count = 1;

	if (AR_SREV_9280(ah)) {
		REG_RMW_FIELD(ah, AR_PHY_SPECTRAL_SCAN,
			      AR_PHY_SPECTRAL_SCAN_COUNT, count);
	} else {
		REG_RMW_FIELD(ah, AR_PHY_SPECTRAL_SCAN,
			      AR_PHY_SPECTRAL_SCAN_COUNT_KIWI, count);
		REG_SET_BIT(ah, AR_PHY_SPECTRAL_SCAN,
			    AR_PHY_SPECTRAL_SCAN_PHYERR_MASK_SELECT);
	}

	REG_RMW_FIELD(ah, AR_PHY_SPECTRAL_SCAN,
		      AR_PHY_SPECTRAL_SCAN_PERIOD, param->period);
	REG_RMW_FIELD(ah, AR_PHY_SPECTRAL_SCAN,
		      AR_PHY_SPECTRAL_SCAN_FFT_PERIOD, param->fft_period);

	return;
}

static void ar9002_hw_spectral_scan_trigger(struct ath_hw *ah)
{
	REG_SET_BIT(ah, AR_PHY_SPECTRAL_SCAN, AR_PHY_SPECTRAL_SCAN_ENABLE);
	/* Activate spectral scan */
	REG_SET_BIT(ah, AR_PHY_SPECTRAL_SCAN,
		    AR_PHY_SPECTRAL_SCAN_ACTIVE);
}

static void ar9002_hw_spectral_scan_wait(struct ath_hw *ah)
{
	struct ath_common *common = ath9k_hw_common(ah);

	/* Poll for spectral scan complete */
	if (!ath9k_hw_wait(ah, AR_PHY_SPECTRAL_SCAN,
			   AR_PHY_SPECTRAL_SCAN_ACTIVE,
			   0, AH_WAIT_TIMEOUT)) {
		ath_err(common, "spectral scan wait failed\n");
		return;
	}
}

static void ar9002_hw_tx99_start(struct ath_hw *ah, u32 qnum)
{
	REG_SET_BIT(ah, 0x9864, 0x7f000);
	REG_SET_BIT(ah, 0x9924, 0x7f00fe);
	REG_CLR_BIT(ah, AR_DIAG_SW, AR_DIAG_RX_DIS);
	REG_WRITE(ah, AR_CR, AR_CR_RXD);
	REG_WRITE(ah, AR_DLCL_IFS(qnum), 0);
	REG_WRITE(ah, AR_D_GBL_IFS_SIFS, 20);
	REG_WRITE(ah, AR_D_GBL_IFS_EIFS, 20);
	REG_WRITE(ah, AR_D_FPCTL, 0x10|qnum);
	REG_WRITE(ah, AR_TIME_OUT, 0x00000400);
	REG_WRITE(ah, AR_DRETRY_LIMIT(qnum), 0xffffffff);
	REG_SET_BIT(ah, AR_QMISC(qnum), AR_Q_MISC_DCU_EARLY_TERM_REQ);
}

static void ar9002_hw_tx99_stop(struct ath_hw *ah)
{
	REG_SET_BIT(ah, AR_DIAG_SW, AR_DIAG_RX_DIS);
}

void ar9002_hw_attach_phy_ops(struct ath_hw *ah)
{
	struct ath_hw_private_ops *priv_ops = ath9k_hw_private_ops(ah);
	struct ath_hw_ops *ops = ath9k_hw_ops(ah);

	priv_ops->set_rf_regs = NULL;
	priv_ops->rf_set_freq = ar9002_hw_set_channel;
	priv_ops->spur_mitigate_freq = ar9002_hw_spur_mitigate;
	priv_ops->olc_init = ar9002_olc_init;
	priv_ops->compute_pll_control = ar9002_hw_compute_pll_control;
	priv_ops->do_getnf = ar9002_hw_do_getnf;

	ops->antdiv_comb_conf_get = ar9002_hw_antdiv_comb_conf_get;
	ops->antdiv_comb_conf_set = ar9002_hw_antdiv_comb_conf_set;
	ops->spectral_scan_config = ar9002_hw_spectral_scan_config;
	ops->spectral_scan_trigger = ar9002_hw_spectral_scan_trigger;
	ops->spectral_scan_wait = ar9002_hw_spectral_scan_wait;

#ifdef CONFIG_ATH9K_BTCOEX_SUPPORT
	ops->set_bt_ant_diversity = ar9002_hw_set_bt_ant_diversity;
#endif
	ops->tx99_start = ar9002_hw_tx99_start;
	ops->tx99_stop = ar9002_hw_tx99_stop;

	ar9002_hw_set_nf_limits(ah);
}
