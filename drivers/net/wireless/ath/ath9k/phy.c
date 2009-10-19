/*
 * Copyright (c) 2008-2009 Atheros Communications Inc.
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

static void ath9k_phy_modify_rx_buffer(u32 *rfBuf, u32 reg32,
				       u32 numBits, u32 firstBit,
				       u32 column);

/**
 * ath9k_hw_write_regs - ??
 *
 * @ah: atheros hardware structure
 * @freqIndex:
 * @regWrites:
 *
 * Used for both the chipsets with an external AR2133/AR5133 radios and
 * single-chip devices.
 */
void ath9k_hw_write_regs(struct ath_hw *ah, u32 freqIndex, int regWrites)
{
	REG_WRITE_ARRAY(&ah->iniBB_RfGain, freqIndex, regWrites);
}

/**
 * ath9k_hw_ar9280_set_channel - set channel on single-chip device
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
int ath9k_hw_ar9280_set_channel(struct ath_hw *ah, struct ath9k_channel *chan)
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
		channelSel = (freq * 0x10000) / 15;

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
					  txctl &~ AR_PHY_CCK_TX_CTRL_JAPAN);
			}
		}
	} else {
		bMode = 0;
		fracMode = 0;

		switch(ah->eep_ops->get_eeprom(ah, EEP_FRAC_N_5G)) {
		case 0:
			if ((freq % 20) == 0) {
				aModeRefSel = 3;
			} else if ((freq % 10) == 0) {
				aModeRefSel = 2;
			}
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
			channelSel = (freq * 0x8000) / 15;

			/* RefDivA setting */
			REG_RMW_FIELD(ah, AR_AN_SYNTH9,
				      AR_AN_SYNTH9_REFDIVA, refDivA);

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
	ah->curchan_rad_index = -1;

	return 0;
}

/**
 * ath9k_hw_9280_spur_mitigate - convert baseband spur frequency
 * @ah: atheros hardware structure
 * @chan:
 *
 * For single-chip solutions. Converts to baseband spur frequency given the
 * input channel frequency and compute register settings below.
 */
void ath9k_hw_9280_spur_mitigate(struct ath_hw *ah, struct ath9k_channel *chan)
{
	int bb_spur = AR_NO_SPUR;
	int freq;
	int bin, cur_bin;
	int bb_spur_off, spur_subchannel_sd;
	int spur_freq_sd;
	int spur_delta_phase;
	int denominator;
	int upper, lower, cur_vit_mask;
	int tmp, newVal;
	int i;
	int pilot_mask_reg[4] = { AR_PHY_TIMING7, AR_PHY_TIMING8,
			  AR_PHY_PILOT_MASK_01_30, AR_PHY_PILOT_MASK_31_60
	};
	int chan_mask_reg[4] = { AR_PHY_TIMING9, AR_PHY_TIMING10,
			 AR_PHY_CHANNEL_MASK_01_30, AR_PHY_CHANNEL_MASK_31_60
	};
	int inc[4] = { 0, 100, 0, 0 };
	struct chan_centers centers;

	int8_t mask_m[123];
	int8_t mask_p[123];
	int8_t mask_amt;
	int tmp_mask;
	int cur_bb_spur;
	bool is2GHz = IS_CHAN_2GHZ(chan);

	memset(&mask_m, 0, sizeof(int8_t) * 123);
	memset(&mask_p, 0, sizeof(int8_t) * 123);

	ath9k_hw_get_channel_centers(ah, chan, &centers);
	freq = centers.synth_center;

	ah->config.spurmode = SPUR_ENABLE_EEPROM;
	for (i = 0; i < AR_EEPROM_MODAL_SPURS; i++) {
		cur_bb_spur = ah->eep_ops->get_spur_channel(ah, i, is2GHz);

		if (is2GHz)
			cur_bb_spur = (cur_bb_spur / 10) + AR_BASE_FREQ_2GHZ;
		else
			cur_bb_spur = (cur_bb_spur / 10) + AR_BASE_FREQ_5GHZ;

		if (AR_NO_SPUR == cur_bb_spur)
			break;
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

	cur_bin = -6000;
	upper = bin + 100;
	lower = bin - 100;

	for (i = 0; i < 4; i++) {
		int pilot_mask = 0;
		int chan_mask = 0;
		int bp = 0;
		for (bp = 0; bp < 30; bp++) {
			if ((cur_bin > lower) && (cur_bin < upper)) {
				pilot_mask = pilot_mask | 0x1 << bp;
				chan_mask = chan_mask | 0x1 << bp;
			}
			cur_bin += 100;
		}
		cur_bin += inc[i];
		REG_WRITE(ah, pilot_mask_reg[i], pilot_mask);
		REG_WRITE(ah, chan_mask_reg[i], chan_mask);
	}

	cur_vit_mask = 6100;
	upper = bin + 120;
	lower = bin - 120;

	for (i = 0; i < 123; i++) {
		if ((cur_vit_mask > lower) && (cur_vit_mask < upper)) {

			/* workaround for gcc bug #37014 */
			volatile int tmp_v = abs(cur_vit_mask - bin);

			if (tmp_v < 75)
				mask_amt = 1;
			else
				mask_amt = 0;
			if (cur_vit_mask < 0)
				mask_m[abs(cur_vit_mask / 100)] = mask_amt;
			else
				mask_p[cur_vit_mask / 100] = mask_amt;
		}
		cur_vit_mask -= 100;
	}

	tmp_mask = (mask_m[46] << 30) | (mask_m[47] << 28)
		| (mask_m[48] << 26) | (mask_m[49] << 24)
		| (mask_m[50] << 22) | (mask_m[51] << 20)
		| (mask_m[52] << 18) | (mask_m[53] << 16)
		| (mask_m[54] << 14) | (mask_m[55] << 12)
		| (mask_m[56] << 10) | (mask_m[57] << 8)
		| (mask_m[58] << 6) | (mask_m[59] << 4)
		| (mask_m[60] << 2) | (mask_m[61] << 0);
	REG_WRITE(ah, AR_PHY_BIN_MASK_1, tmp_mask);
	REG_WRITE(ah, AR_PHY_VIT_MASK2_M_46_61, tmp_mask);

	tmp_mask = (mask_m[31] << 28)
		| (mask_m[32] << 26) | (mask_m[33] << 24)
		| (mask_m[34] << 22) | (mask_m[35] << 20)
		| (mask_m[36] << 18) | (mask_m[37] << 16)
		| (mask_m[48] << 14) | (mask_m[39] << 12)
		| (mask_m[40] << 10) | (mask_m[41] << 8)
		| (mask_m[42] << 6) | (mask_m[43] << 4)
		| (mask_m[44] << 2) | (mask_m[45] << 0);
	REG_WRITE(ah, AR_PHY_BIN_MASK_2, tmp_mask);
	REG_WRITE(ah, AR_PHY_MASK2_M_31_45, tmp_mask);

	tmp_mask = (mask_m[16] << 30) | (mask_m[16] << 28)
		| (mask_m[18] << 26) | (mask_m[18] << 24)
		| (mask_m[20] << 22) | (mask_m[20] << 20)
		| (mask_m[22] << 18) | (mask_m[22] << 16)
		| (mask_m[24] << 14) | (mask_m[24] << 12)
		| (mask_m[25] << 10) | (mask_m[26] << 8)
		| (mask_m[27] << 6) | (mask_m[28] << 4)
		| (mask_m[29] << 2) | (mask_m[30] << 0);
	REG_WRITE(ah, AR_PHY_BIN_MASK_3, tmp_mask);
	REG_WRITE(ah, AR_PHY_MASK2_M_16_30, tmp_mask);

	tmp_mask = (mask_m[0] << 30) | (mask_m[1] << 28)
		| (mask_m[2] << 26) | (mask_m[3] << 24)
		| (mask_m[4] << 22) | (mask_m[5] << 20)
		| (mask_m[6] << 18) | (mask_m[7] << 16)
		| (mask_m[8] << 14) | (mask_m[9] << 12)
		| (mask_m[10] << 10) | (mask_m[11] << 8)
		| (mask_m[12] << 6) | (mask_m[13] << 4)
		| (mask_m[14] << 2) | (mask_m[15] << 0);
	REG_WRITE(ah, AR_PHY_MASK_CTL, tmp_mask);
	REG_WRITE(ah, AR_PHY_MASK2_M_00_15, tmp_mask);

	tmp_mask = (mask_p[15] << 28)
		| (mask_p[14] << 26) | (mask_p[13] << 24)
		| (mask_p[12] << 22) | (mask_p[11] << 20)
		| (mask_p[10] << 18) | (mask_p[9] << 16)
		| (mask_p[8] << 14) | (mask_p[7] << 12)
		| (mask_p[6] << 10) | (mask_p[5] << 8)
		| (mask_p[4] << 6) | (mask_p[3] << 4)
		| (mask_p[2] << 2) | (mask_p[1] << 0);
	REG_WRITE(ah, AR_PHY_BIN_MASK2_1, tmp_mask);
	REG_WRITE(ah, AR_PHY_MASK2_P_15_01, tmp_mask);

	tmp_mask = (mask_p[30] << 28)
		| (mask_p[29] << 26) | (mask_p[28] << 24)
		| (mask_p[27] << 22) | (mask_p[26] << 20)
		| (mask_p[25] << 18) | (mask_p[24] << 16)
		| (mask_p[23] << 14) | (mask_p[22] << 12)
		| (mask_p[21] << 10) | (mask_p[20] << 8)
		| (mask_p[19] << 6) | (mask_p[18] << 4)
		| (mask_p[17] << 2) | (mask_p[16] << 0);
	REG_WRITE(ah, AR_PHY_BIN_MASK2_2, tmp_mask);
	REG_WRITE(ah, AR_PHY_MASK2_P_30_16, tmp_mask);

	tmp_mask = (mask_p[45] << 28)
		| (mask_p[44] << 26) | (mask_p[43] << 24)
		| (mask_p[42] << 22) | (mask_p[41] << 20)
		| (mask_p[40] << 18) | (mask_p[39] << 16)
		| (mask_p[38] << 14) | (mask_p[37] << 12)
		| (mask_p[36] << 10) | (mask_p[35] << 8)
		| (mask_p[34] << 6) | (mask_p[33] << 4)
		| (mask_p[32] << 2) | (mask_p[31] << 0);
	REG_WRITE(ah, AR_PHY_BIN_MASK2_3, tmp_mask);
	REG_WRITE(ah, AR_PHY_MASK2_P_45_31, tmp_mask);

	tmp_mask = (mask_p[61] << 30) | (mask_p[60] << 28)
		| (mask_p[59] << 26) | (mask_p[58] << 24)
		| (mask_p[57] << 22) | (mask_p[56] << 20)
		| (mask_p[55] << 18) | (mask_p[54] << 16)
		| (mask_p[53] << 14) | (mask_p[52] << 12)
		| (mask_p[51] << 10) | (mask_p[50] << 8)
		| (mask_p[49] << 6) | (mask_p[48] << 4)
		| (mask_p[47] << 2) | (mask_p[46] << 0);
	REG_WRITE(ah, AR_PHY_BIN_MASK2_4, tmp_mask);
	REG_WRITE(ah, AR_PHY_MASK2_P_61_45, tmp_mask);
}

/* All code below is for non single-chip solutions */

/*
 * Fix on 2.4 GHz band for orientation sensitivity issue by increasing
 * rf_pwd_icsyndiv.
 *
 * Theoretical Rules:
 *   if 2 GHz band
 *      if forceBiasAuto
 *         if synth_freq < 2412
 *            bias = 0
 *         else if 2412 <= synth_freq <= 2422
 *            bias = 1
 *         else // synth_freq > 2422
 *            bias = 2
 *      else if forceBias > 0
 *         bias = forceBias & 7
 *      else
 *         no change, use value from ini file
 *   else
 *      no change, invalid band
 *
 *  1st Mod:
 *    2422 also uses value of 2
 *    <approved>
 *
 *  2nd Mod:
 *    Less than 2412 uses value of 0, 2412 and above uses value of 2
 */
static void ath9k_hw_force_bias(struct ath_hw *ah, u16 synth_freq)
{
	struct ath_common *common = ath9k_hw_common(ah);
	u32 tmp_reg;
	int reg_writes = 0;
	u32 new_bias = 0;

	if (!AR_SREV_5416(ah) || synth_freq >= 3000) {
		return;
	}

	BUG_ON(AR_SREV_9280_10_OR_LATER(ah));

	if (synth_freq < 2412)
		new_bias = 0;
	else if (synth_freq < 2422)
		new_bias = 1;
	else
		new_bias = 2;

	/* pre-reverse this field */
	tmp_reg = ath9k_hw_reverse_bits(new_bias, 3);

	ath_print(common, ATH_DBG_CONFIG,
		  "Force rf_pwd_icsyndiv to %1d on %4d\n",
		  new_bias, synth_freq);

	/* swizzle rf_pwd_icsyndiv */
	ath9k_phy_modify_rx_buffer(ah->analogBank6Data, tmp_reg, 3, 181, 3);

	/* write Bank 6 with new params */
	REG_WRITE_RF_ARRAY(&ah->iniBank6, ah->analogBank6Data, reg_writes);
}

/**
 * ath9k_hw_set_channel - tune to a channel on the external AR2133/AR5133 radios
 * @ah: atheros hardware stucture
 * @chan:
 *
 * For the external AR2133/AR5133 radios, takes the MHz channel value and set
 * the channel value. Assumes writes enabled to analog bus and bank6 register
 * cache in ah->analogBank6Data.
 */
int ath9k_hw_set_channel(struct ath_hw *ah, struct ath9k_channel *chan)
{
	struct ath_common *common = ath9k_hw_common(ah);
	u32 channelSel = 0;
	u32 bModeSynth = 0;
	u32 aModeRefSel = 0;
	u32 reg32 = 0;
	u16 freq;
	struct chan_centers centers;

	ath9k_hw_get_channel_centers(ah, chan, &centers);
	freq = centers.synth_center;

	if (freq < 4800) {
		u32 txctl;

		if (((freq - 2192) % 5) == 0) {
			channelSel = ((freq - 672) * 2 - 3040) / 10;
			bModeSynth = 0;
		} else if (((freq - 2224) % 5) == 0) {
			channelSel = ((freq - 704) * 2 - 3040) / 10;
			bModeSynth = 1;
		} else {
			ath_print(common, ATH_DBG_FATAL,
				  "Invalid channel %u MHz\n", freq);
			return -EINVAL;
		}

		channelSel = (channelSel << 2) & 0xff;
		channelSel = ath9k_hw_reverse_bits(channelSel, 8);

		txctl = REG_READ(ah, AR_PHY_CCK_TX_CTRL);
		if (freq == 2484) {

			REG_WRITE(ah, AR_PHY_CCK_TX_CTRL,
				  txctl | AR_PHY_CCK_TX_CTRL_JAPAN);
		} else {
			REG_WRITE(ah, AR_PHY_CCK_TX_CTRL,
				  txctl & ~AR_PHY_CCK_TX_CTRL_JAPAN);
		}

	} else if ((freq % 20) == 0 && freq >= 5120) {
		channelSel =
		    ath9k_hw_reverse_bits(((freq - 4800) / 20 << 2), 8);
		aModeRefSel = ath9k_hw_reverse_bits(1, 2);
	} else if ((freq % 10) == 0) {
		channelSel =
		    ath9k_hw_reverse_bits(((freq - 4800) / 10 << 1), 8);
		if (AR_SREV_9100(ah) || AR_SREV_9160_10_OR_LATER(ah))
			aModeRefSel = ath9k_hw_reverse_bits(2, 2);
		else
			aModeRefSel = ath9k_hw_reverse_bits(1, 2);
	} else if ((freq % 5) == 0) {
		channelSel = ath9k_hw_reverse_bits((freq - 4800) / 5, 8);
		aModeRefSel = ath9k_hw_reverse_bits(1, 2);
	} else {
		ath_print(common, ATH_DBG_FATAL,
			  "Invalid channel %u MHz\n", freq);
		return -EINVAL;
	}

	ath9k_hw_force_bias(ah, freq);
	ath9k_hw_decrease_chain_power(ah, chan);

	reg32 =
	    (channelSel << 8) | (aModeRefSel << 2) | (bModeSynth << 1) |
	    (1 << 5) | 0x1;

	REG_WRITE(ah, AR_PHY(0x37), reg32);

	ah->curchan = chan;
	ah->curchan_rad_index = -1;

	return 0;
}

/**
 * ath9k_hw_spur_mitigate - convert baseband spur frequency for external radios
 * @ah: atheros hardware structure
 * @chan:
 *
 * For non single-chip solutions. Converts to baseband spur frequency given the
 * input channel frequency and compute register settings below.
 */
void ath9k_hw_spur_mitigate(struct ath_hw *ah, struct ath9k_channel *chan)
{
	int bb_spur = AR_NO_SPUR;
	int bin, cur_bin;
	int spur_freq_sd;
	int spur_delta_phase;
	int denominator;
	int upper, lower, cur_vit_mask;
	int tmp, new;
	int i;
	int pilot_mask_reg[4] = { AR_PHY_TIMING7, AR_PHY_TIMING8,
			  AR_PHY_PILOT_MASK_01_30, AR_PHY_PILOT_MASK_31_60
	};
	int chan_mask_reg[4] = { AR_PHY_TIMING9, AR_PHY_TIMING10,
			 AR_PHY_CHANNEL_MASK_01_30, AR_PHY_CHANNEL_MASK_31_60
	};
	int inc[4] = { 0, 100, 0, 0 };

	int8_t mask_m[123];
	int8_t mask_p[123];
	int8_t mask_amt;
	int tmp_mask;
	int cur_bb_spur;
	bool is2GHz = IS_CHAN_2GHZ(chan);

	memset(&mask_m, 0, sizeof(int8_t) * 123);
	memset(&mask_p, 0, sizeof(int8_t) * 123);

	for (i = 0; i < AR_EEPROM_MODAL_SPURS; i++) {
		cur_bb_spur = ah->eep_ops->get_spur_channel(ah, i, is2GHz);
		if (AR_NO_SPUR == cur_bb_spur)
			break;
		cur_bb_spur = cur_bb_spur - (chan->channel * 10);
		if ((cur_bb_spur > -95) && (cur_bb_spur < 95)) {
			bb_spur = cur_bb_spur;
			break;
		}
	}

	if (AR_NO_SPUR == bb_spur)
		return;

	bin = bb_spur * 32;

	tmp = REG_READ(ah, AR_PHY_TIMING_CTRL4(0));
	new = tmp | (AR_PHY_TIMING_CTRL4_ENABLE_SPUR_RSSI |
		     AR_PHY_TIMING_CTRL4_ENABLE_SPUR_FILTER |
		     AR_PHY_TIMING_CTRL4_ENABLE_CHAN_MASK |
		     AR_PHY_TIMING_CTRL4_ENABLE_PILOT_MASK);

	REG_WRITE(ah, AR_PHY_TIMING_CTRL4(0), new);

	new = (AR_PHY_SPUR_REG_MASK_RATE_CNTL |
	       AR_PHY_SPUR_REG_ENABLE_MASK_PPM |
	       AR_PHY_SPUR_REG_MASK_RATE_SELECT |
	       AR_PHY_SPUR_REG_ENABLE_VIT_SPUR_RSSI |
	       SM(SPUR_RSSI_THRESH, AR_PHY_SPUR_REG_SPUR_RSSI_THRESH));
	REG_WRITE(ah, AR_PHY_SPUR_REG, new);

	spur_delta_phase = ((bb_spur * 524288) / 100) &
		AR_PHY_TIMING11_SPUR_DELTA_PHASE;

	denominator = IS_CHAN_2GHZ(chan) ? 440 : 400;
	spur_freq_sd = ((bb_spur * 2048) / denominator) & 0x3ff;

	new = (AR_PHY_TIMING11_USE_SPUR_IN_AGC |
	       SM(spur_freq_sd, AR_PHY_TIMING11_SPUR_FREQ_SD) |
	       SM(spur_delta_phase, AR_PHY_TIMING11_SPUR_DELTA_PHASE));
	REG_WRITE(ah, AR_PHY_TIMING11, new);

	cur_bin = -6000;
	upper = bin + 100;
	lower = bin - 100;

	for (i = 0; i < 4; i++) {
		int pilot_mask = 0;
		int chan_mask = 0;
		int bp = 0;
		for (bp = 0; bp < 30; bp++) {
			if ((cur_bin > lower) && (cur_bin < upper)) {
				pilot_mask = pilot_mask | 0x1 << bp;
				chan_mask = chan_mask | 0x1 << bp;
			}
			cur_bin += 100;
		}
		cur_bin += inc[i];
		REG_WRITE(ah, pilot_mask_reg[i], pilot_mask);
		REG_WRITE(ah, chan_mask_reg[i], chan_mask);
	}

	cur_vit_mask = 6100;
	upper = bin + 120;
	lower = bin - 120;

	for (i = 0; i < 123; i++) {
		if ((cur_vit_mask > lower) && (cur_vit_mask < upper)) {

			/* workaround for gcc bug #37014 */
			volatile int tmp_v = abs(cur_vit_mask - bin);

			if (tmp_v < 75)
				mask_amt = 1;
			else
				mask_amt = 0;
			if (cur_vit_mask < 0)
				mask_m[abs(cur_vit_mask / 100)] = mask_amt;
			else
				mask_p[cur_vit_mask / 100] = mask_amt;
		}
		cur_vit_mask -= 100;
	}

	tmp_mask = (mask_m[46] << 30) | (mask_m[47] << 28)
		| (mask_m[48] << 26) | (mask_m[49] << 24)
		| (mask_m[50] << 22) | (mask_m[51] << 20)
		| (mask_m[52] << 18) | (mask_m[53] << 16)
		| (mask_m[54] << 14) | (mask_m[55] << 12)
		| (mask_m[56] << 10) | (mask_m[57] << 8)
		| (mask_m[58] << 6) | (mask_m[59] << 4)
		| (mask_m[60] << 2) | (mask_m[61] << 0);
	REG_WRITE(ah, AR_PHY_BIN_MASK_1, tmp_mask);
	REG_WRITE(ah, AR_PHY_VIT_MASK2_M_46_61, tmp_mask);

	tmp_mask = (mask_m[31] << 28)
		| (mask_m[32] << 26) | (mask_m[33] << 24)
		| (mask_m[34] << 22) | (mask_m[35] << 20)
		| (mask_m[36] << 18) | (mask_m[37] << 16)
		| (mask_m[48] << 14) | (mask_m[39] << 12)
		| (mask_m[40] << 10) | (mask_m[41] << 8)
		| (mask_m[42] << 6) | (mask_m[43] << 4)
		| (mask_m[44] << 2) | (mask_m[45] << 0);
	REG_WRITE(ah, AR_PHY_BIN_MASK_2, tmp_mask);
	REG_WRITE(ah, AR_PHY_MASK2_M_31_45, tmp_mask);

	tmp_mask = (mask_m[16] << 30) | (mask_m[16] << 28)
		| (mask_m[18] << 26) | (mask_m[18] << 24)
		| (mask_m[20] << 22) | (mask_m[20] << 20)
		| (mask_m[22] << 18) | (mask_m[22] << 16)
		| (mask_m[24] << 14) | (mask_m[24] << 12)
		| (mask_m[25] << 10) | (mask_m[26] << 8)
		| (mask_m[27] << 6) | (mask_m[28] << 4)
		| (mask_m[29] << 2) | (mask_m[30] << 0);
	REG_WRITE(ah, AR_PHY_BIN_MASK_3, tmp_mask);
	REG_WRITE(ah, AR_PHY_MASK2_M_16_30, tmp_mask);

	tmp_mask = (mask_m[0] << 30) | (mask_m[1] << 28)
		| (mask_m[2] << 26) | (mask_m[3] << 24)
		| (mask_m[4] << 22) | (mask_m[5] << 20)
		| (mask_m[6] << 18) | (mask_m[7] << 16)
		| (mask_m[8] << 14) | (mask_m[9] << 12)
		| (mask_m[10] << 10) | (mask_m[11] << 8)
		| (mask_m[12] << 6) | (mask_m[13] << 4)
		| (mask_m[14] << 2) | (mask_m[15] << 0);
	REG_WRITE(ah, AR_PHY_MASK_CTL, tmp_mask);
	REG_WRITE(ah, AR_PHY_MASK2_M_00_15, tmp_mask);

	tmp_mask = (mask_p[15] << 28)
		| (mask_p[14] << 26) | (mask_p[13] << 24)
		| (mask_p[12] << 22) | (mask_p[11] << 20)
		| (mask_p[10] << 18) | (mask_p[9] << 16)
		| (mask_p[8] << 14) | (mask_p[7] << 12)
		| (mask_p[6] << 10) | (mask_p[5] << 8)
		| (mask_p[4] << 6) | (mask_p[3] << 4)
		| (mask_p[2] << 2) | (mask_p[1] << 0);
	REG_WRITE(ah, AR_PHY_BIN_MASK2_1, tmp_mask);
	REG_WRITE(ah, AR_PHY_MASK2_P_15_01, tmp_mask);

	tmp_mask = (mask_p[30] << 28)
		| (mask_p[29] << 26) | (mask_p[28] << 24)
		| (mask_p[27] << 22) | (mask_p[26] << 20)
		| (mask_p[25] << 18) | (mask_p[24] << 16)
		| (mask_p[23] << 14) | (mask_p[22] << 12)
		| (mask_p[21] << 10) | (mask_p[20] << 8)
		| (mask_p[19] << 6) | (mask_p[18] << 4)
		| (mask_p[17] << 2) | (mask_p[16] << 0);
	REG_WRITE(ah, AR_PHY_BIN_MASK2_2, tmp_mask);
	REG_WRITE(ah, AR_PHY_MASK2_P_30_16, tmp_mask);

	tmp_mask = (mask_p[45] << 28)
		| (mask_p[44] << 26) | (mask_p[43] << 24)
		| (mask_p[42] << 22) | (mask_p[41] << 20)
		| (mask_p[40] << 18) | (mask_p[39] << 16)
		| (mask_p[38] << 14) | (mask_p[37] << 12)
		| (mask_p[36] << 10) | (mask_p[35] << 8)
		| (mask_p[34] << 6) | (mask_p[33] << 4)
		| (mask_p[32] << 2) | (mask_p[31] << 0);
	REG_WRITE(ah, AR_PHY_BIN_MASK2_3, tmp_mask);
	REG_WRITE(ah, AR_PHY_MASK2_P_45_31, tmp_mask);

	tmp_mask = (mask_p[61] << 30) | (mask_p[60] << 28)
		| (mask_p[59] << 26) | (mask_p[58] << 24)
		| (mask_p[57] << 22) | (mask_p[56] << 20)
		| (mask_p[55] << 18) | (mask_p[54] << 16)
		| (mask_p[53] << 14) | (mask_p[52] << 12)
		| (mask_p[51] << 10) | (mask_p[50] << 8)
		| (mask_p[49] << 6) | (mask_p[48] << 4)
		| (mask_p[47] << 2) | (mask_p[46] << 0);
	REG_WRITE(ah, AR_PHY_BIN_MASK2_4, tmp_mask);
	REG_WRITE(ah, AR_PHY_MASK2_P_61_45, tmp_mask);
}

/**
 * ath9k_hw_rf_alloc_ext_banks - allocates banks for external radio programming
 * @ah: atheros hardware structure
 *
 * Only required for older devices with external AR2133/AR5133 radios.
 */
int ath9k_hw_rf_alloc_ext_banks(struct ath_hw *ah)
{
#define ATH_ALLOC_BANK(bank, size) do { \
		bank = kzalloc((sizeof(u32) * size), GFP_KERNEL); \
		if (!bank) { \
			ath_print(common, ATH_DBG_FATAL, \
				  "Cannot allocate RF banks\n"); \
			return -ENOMEM; \
		} \
	} while (0);

	struct ath_common *common = ath9k_hw_common(ah);

	BUG_ON(AR_SREV_9280_10_OR_LATER(ah));

	ATH_ALLOC_BANK(ah->analogBank0Data, ah->iniBank0.ia_rows);
	ATH_ALLOC_BANK(ah->analogBank1Data, ah->iniBank1.ia_rows);
	ATH_ALLOC_BANK(ah->analogBank2Data, ah->iniBank2.ia_rows);
	ATH_ALLOC_BANK(ah->analogBank3Data, ah->iniBank3.ia_rows);
	ATH_ALLOC_BANK(ah->analogBank6Data, ah->iniBank6.ia_rows);
	ATH_ALLOC_BANK(ah->analogBank6TPCData, ah->iniBank6TPC.ia_rows);
	ATH_ALLOC_BANK(ah->analogBank7Data, ah->iniBank7.ia_rows);
	ATH_ALLOC_BANK(ah->addac5416_21,
		       ah->iniAddac.ia_rows * ah->iniAddac.ia_columns);
	ATH_ALLOC_BANK(ah->bank6Temp, ah->iniBank6.ia_rows);

	return 0;
#undef ATH_ALLOC_BANK
}


/**
 * ath9k_hw_rf_free_ext_banks - Free memory for analog bank scratch buffers
 * @ah: atheros hardware struture
 * For the external AR2133/AR5133 radios banks.
 */
void
ath9k_hw_rf_free_ext_banks(struct ath_hw *ah)
{
#define ATH_FREE_BANK(bank) do { \
		kfree(bank); \
		bank = NULL; \
	} while (0);

	BUG_ON(AR_SREV_9280_10_OR_LATER(ah));

	ATH_FREE_BANK(ah->analogBank0Data);
	ATH_FREE_BANK(ah->analogBank1Data);
	ATH_FREE_BANK(ah->analogBank2Data);
	ATH_FREE_BANK(ah->analogBank3Data);
	ATH_FREE_BANK(ah->analogBank6Data);
	ATH_FREE_BANK(ah->analogBank6TPCData);
	ATH_FREE_BANK(ah->analogBank7Data);
	ATH_FREE_BANK(ah->addac5416_21);
	ATH_FREE_BANK(ah->bank6Temp);

#undef ATH_FREE_BANK
}

/**
 * ath9k_phy_modify_rx_buffer() - perform analog swizzling of parameters
 * @rfbuf:
 * @reg32:
 * @numBits:
 * @firstBit:
 * @column:
 *
 * Performs analog "swizzling" of parameters into their location.
 * Used on external AR2133/AR5133 radios.
 */
static void ath9k_phy_modify_rx_buffer(u32 *rfBuf, u32 reg32,
				       u32 numBits, u32 firstBit,
				       u32 column)
{
	u32 tmp32, mask, arrayEntry, lastBit;
	int32_t bitPosition, bitsLeft;

	tmp32 = ath9k_hw_reverse_bits(reg32, numBits);
	arrayEntry = (firstBit - 1) / 8;
	bitPosition = (firstBit - 1) % 8;
	bitsLeft = numBits;
	while (bitsLeft > 0) {
		lastBit = (bitPosition + bitsLeft > 8) ?
		    8 : bitPosition + bitsLeft;
		mask = (((1 << lastBit) - 1) ^ ((1 << bitPosition) - 1)) <<
		    (column * 8);
		rfBuf[arrayEntry] &= ~mask;
		rfBuf[arrayEntry] |= ((tmp32 << bitPosition) <<
				      (column * 8)) & mask;
		bitsLeft -= 8 - bitPosition;
		tmp32 = tmp32 >> (8 - bitPosition);
		bitPosition = 0;
		arrayEntry++;
	}
}

/* *
 * ath9k_hw_set_rf_regs - programs rf registers based on EEPROM
 * @ah: atheros hardware structure
 * @chan:
 * @modesIndex:
 *
 * Used for the external AR2133/AR5133 radios.
 *
 * Reads the EEPROM header info from the device structure and programs
 * all rf registers. This routine requires access to the analog
 * rf device. This is not required for single-chip devices.
 */
bool ath9k_hw_set_rf_regs(struct ath_hw *ah, struct ath9k_channel *chan,
			  u16 modesIndex)
{
	u32 eepMinorRev;
	u32 ob5GHz = 0, db5GHz = 0;
	u32 ob2GHz = 0, db2GHz = 0;
	int regWrites = 0;

	/*
	 * Software does not need to program bank data
	 * for single chip devices, that is AR9280 or anything
	 * after that.
	 */
	if (AR_SREV_9280_10_OR_LATER(ah))
		return true;

	/* Setup rf parameters */
	eepMinorRev = ah->eep_ops->get_eeprom(ah, EEP_MINOR_REV);

	/* Setup Bank 0 Write */
	RF_BANK_SETUP(ah->analogBank0Data, &ah->iniBank0, 1);

	/* Setup Bank 1 Write */
	RF_BANK_SETUP(ah->analogBank1Data, &ah->iniBank1, 1);

	/* Setup Bank 2 Write */
	RF_BANK_SETUP(ah->analogBank2Data, &ah->iniBank2, 1);

	/* Setup Bank 6 Write */
	RF_BANK_SETUP(ah->analogBank3Data, &ah->iniBank3,
		      modesIndex);
	{
		int i;
		for (i = 0; i < ah->iniBank6TPC.ia_rows; i++) {
			ah->analogBank6Data[i] =
			    INI_RA(&ah->iniBank6TPC, i, modesIndex);
		}
	}

	/* Only the 5 or 2 GHz OB/DB need to be set for a mode */
	if (eepMinorRev >= 2) {
		if (IS_CHAN_2GHZ(chan)) {
			ob2GHz = ah->eep_ops->get_eeprom(ah, EEP_OB_2);
			db2GHz = ah->eep_ops->get_eeprom(ah, EEP_DB_2);
			ath9k_phy_modify_rx_buffer(ah->analogBank6Data,
						   ob2GHz, 3, 197, 0);
			ath9k_phy_modify_rx_buffer(ah->analogBank6Data,
						   db2GHz, 3, 194, 0);
		} else {
			ob5GHz = ah->eep_ops->get_eeprom(ah, EEP_OB_5);
			db5GHz = ah->eep_ops->get_eeprom(ah, EEP_DB_5);
			ath9k_phy_modify_rx_buffer(ah->analogBank6Data,
						   ob5GHz, 3, 203, 0);
			ath9k_phy_modify_rx_buffer(ah->analogBank6Data,
						   db5GHz, 3, 200, 0);
		}
	}

	/* Setup Bank 7 Setup */
	RF_BANK_SETUP(ah->analogBank7Data, &ah->iniBank7, 1);

	/* Write Analog registers */
	REG_WRITE_RF_ARRAY(&ah->iniBank0, ah->analogBank0Data,
			   regWrites);
	REG_WRITE_RF_ARRAY(&ah->iniBank1, ah->analogBank1Data,
			   regWrites);
	REG_WRITE_RF_ARRAY(&ah->iniBank2, ah->analogBank2Data,
			   regWrites);
	REG_WRITE_RF_ARRAY(&ah->iniBank3, ah->analogBank3Data,
			   regWrites);
	REG_WRITE_RF_ARRAY(&ah->iniBank6TPC, ah->analogBank6Data,
			   regWrites);
	REG_WRITE_RF_ARRAY(&ah->iniBank7, ah->analogBank7Data,
			   regWrites);

	return true;
}

/**
 * ath9k_hw_decrease_chain_power()
 *
 * @ah: atheros hardware structure
 * @chan:
 *
 * Only used on the AR5416 and AR5418 with the external AR2133/AR5133 radios.
 *
 * Sets a chain internal RF path to the lowest output power. Any
 * further writes to bank6 after this setting will override these
 * changes. Thus this function must be the last function in the
 * sequence to modify bank 6.
 *
 * This function must be called after ar5416SetRfRegs() which is
 * called from ath9k_hw_process_ini() due to swizzling of bank 6.
 * Depends on ah->analogBank6Data being initialized by
 * ath9k_hw_set_rf_regs()
 *
 * Additional additive reduction in power -
 * change chain's switch table so chain's tx state is actually the rx
 * state value. May produce different results in 2GHz/5GHz as well as
 * board to board but in general should be a reduction.
 *
 * Activated by #ifdef ALTER_SWITCH.  Not tried yet.  If so, must be
 * called after ah->eep_ops->set_board_values() due to RMW of
 * PHY_SWITCH_CHAIN_0.
 */
void
ath9k_hw_decrease_chain_power(struct ath_hw *ah, struct ath9k_channel *chan)
{
	int i, regWrites = 0;
	u32 bank6SelMask;
	u32 *bank6Temp = ah->bank6Temp;

	BUG_ON(AR_SREV_9280_10_OR_LATER(ah));

	switch (ah->config.diversity_control) {
	case ATH9K_ANT_FIXED_A:
		bank6SelMask =
		    (ah->config.antenna_switch_swap & ANTSWAP_AB) ?
			REDUCE_CHAIN_0 : /* swapped, reduce chain 0 */
			REDUCE_CHAIN_1; /* normal, select chain 1/2 to reduce */
		break;
	case ATH9K_ANT_FIXED_B:
		bank6SelMask =
		    (ah->config.antenna_switch_swap & ANTSWAP_AB) ?
			REDUCE_CHAIN_1 : /* swapped, reduce chain 1/2 */
			REDUCE_CHAIN_0; /* normal, select chain 0 to reduce */
		break;
	case ATH9K_ANT_VARIABLE:
		return; /* do not change anything */
		break;
	default:
		return; /* do not change anything */
		break;
	}

	for (i = 0; i < ah->iniBank6.ia_rows; i++)
		bank6Temp[i] = ah->analogBank6Data[i];

	/* Write Bank 5 to switch Bank 6 write to selected chain only */
	REG_WRITE(ah, AR_PHY_BASE + 0xD8, bank6SelMask);

	/*
	 * Modify Bank6 selected chain to use lowest amplification.
	 * Modifies the parameters to a value of 1.
	 * Depends on existing bank 6 values to be cached in
	 * ah->analogBank6Data
	 */
	ath9k_phy_modify_rx_buffer(bank6Temp, 1, 1, 189, 0);
	ath9k_phy_modify_rx_buffer(bank6Temp, 1, 1, 190, 0);
	ath9k_phy_modify_rx_buffer(bank6Temp, 1, 1, 191, 0);
	ath9k_phy_modify_rx_buffer(bank6Temp, 1, 1, 192, 0);
	ath9k_phy_modify_rx_buffer(bank6Temp, 1, 1, 193, 0);
	ath9k_phy_modify_rx_buffer(bank6Temp, 1, 1, 222, 0);
	ath9k_phy_modify_rx_buffer(bank6Temp, 1, 1, 245, 0);
	ath9k_phy_modify_rx_buffer(bank6Temp, 1, 1, 246, 0);
	ath9k_phy_modify_rx_buffer(bank6Temp, 1, 1, 247, 0);

	REG_WRITE_RF_ARRAY(&ah->iniBank6, bank6Temp, regWrites);

	REG_WRITE(ah, AR_PHY_BASE + 0xD8, 0x00000053);
#ifdef ALTER_SWITCH
	REG_WRITE(ah, PHY_SWITCH_CHAIN_0,
		  (REG_READ(ah, PHY_SWITCH_CHAIN_0) & ~0x38)
		  | ((REG_READ(ah, PHY_SWITCH_CHAIN_0) >> 3) & 0x38));
#endif
}
