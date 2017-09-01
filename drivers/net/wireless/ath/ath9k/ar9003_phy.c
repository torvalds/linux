/*
 * Copyright (c) 2010-2011 Atheros Communications Inc.
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

#include <linux/export.h>
#include "hw.h"
#include "ar9003_phy.h"

#define AR9300_OFDM_RATES	8
#define AR9300_HT_SS_RATES	8
#define AR9300_HT_DS_RATES	8
#define AR9300_HT_TS_RATES	8

#define AR9300_11NA_OFDM_SHIFT		0
#define AR9300_11NA_HT_SS_SHIFT		8
#define AR9300_11NA_HT_DS_SHIFT		16
#define AR9300_11NA_HT_TS_SHIFT		24

#define AR9300_11NG_OFDM_SHIFT		4
#define AR9300_11NG_HT_SS_SHIFT		12
#define AR9300_11NG_HT_DS_SHIFT		20
#define AR9300_11NG_HT_TS_SHIFT		28

static const int firstep_table[] =
/* level:  0   1   2   3   4   5   6   7   8  */
	{ -4, -2,  0,  2,  4,  6,  8, 10, 12 }; /* lvl 0-8, default 2 */

static const int cycpwrThr1_table[] =
/* level:  0   1   2   3   4   5   6   7   8  */
	{ -6, -4, -2,  0,  2,  4,  6,  8 };     /* lvl 0-7, default 3 */

/*
 * register values to turn OFDM weak signal detection OFF
 */
static const int m1ThreshLow_off = 127;
static const int m2ThreshLow_off = 127;
static const int m1Thresh_off = 127;
static const int m2Thresh_off = 127;
static const int m2CountThr_off =  31;
static const int m2CountThrLow_off =  63;
static const int m1ThreshLowExt_off = 127;
static const int m2ThreshLowExt_off = 127;
static const int m1ThreshExt_off = 127;
static const int m2ThreshExt_off = 127;

static const u8 ofdm2pwr[] = {
	ALL_TARGET_LEGACY_6_24,
	ALL_TARGET_LEGACY_6_24,
	ALL_TARGET_LEGACY_6_24,
	ALL_TARGET_LEGACY_6_24,
	ALL_TARGET_LEGACY_6_24,
	ALL_TARGET_LEGACY_36,
	ALL_TARGET_LEGACY_48,
	ALL_TARGET_LEGACY_54
};

static const u8 mcs2pwr_ht20[] = {
	ALL_TARGET_HT20_0_8_16,
	ALL_TARGET_HT20_1_3_9_11_17_19,
	ALL_TARGET_HT20_1_3_9_11_17_19,
	ALL_TARGET_HT20_1_3_9_11_17_19,
	ALL_TARGET_HT20_4,
	ALL_TARGET_HT20_5,
	ALL_TARGET_HT20_6,
	ALL_TARGET_HT20_7,
	ALL_TARGET_HT20_0_8_16,
	ALL_TARGET_HT20_1_3_9_11_17_19,
	ALL_TARGET_HT20_1_3_9_11_17_19,
	ALL_TARGET_HT20_1_3_9_11_17_19,
	ALL_TARGET_HT20_12,
	ALL_TARGET_HT20_13,
	ALL_TARGET_HT20_14,
	ALL_TARGET_HT20_15,
	ALL_TARGET_HT20_0_8_16,
	ALL_TARGET_HT20_1_3_9_11_17_19,
	ALL_TARGET_HT20_1_3_9_11_17_19,
	ALL_TARGET_HT20_1_3_9_11_17_19,
	ALL_TARGET_HT20_20,
	ALL_TARGET_HT20_21,
	ALL_TARGET_HT20_22,
	ALL_TARGET_HT20_23
};

static const u8 mcs2pwr_ht40[] = {
	ALL_TARGET_HT40_0_8_16,
	ALL_TARGET_HT40_1_3_9_11_17_19,
	ALL_TARGET_HT40_1_3_9_11_17_19,
	ALL_TARGET_HT40_1_3_9_11_17_19,
	ALL_TARGET_HT40_4,
	ALL_TARGET_HT40_5,
	ALL_TARGET_HT40_6,
	ALL_TARGET_HT40_7,
	ALL_TARGET_HT40_0_8_16,
	ALL_TARGET_HT40_1_3_9_11_17_19,
	ALL_TARGET_HT40_1_3_9_11_17_19,
	ALL_TARGET_HT40_1_3_9_11_17_19,
	ALL_TARGET_HT40_12,
	ALL_TARGET_HT40_13,
	ALL_TARGET_HT40_14,
	ALL_TARGET_HT40_15,
	ALL_TARGET_HT40_0_8_16,
	ALL_TARGET_HT40_1_3_9_11_17_19,
	ALL_TARGET_HT40_1_3_9_11_17_19,
	ALL_TARGET_HT40_1_3_9_11_17_19,
	ALL_TARGET_HT40_20,
	ALL_TARGET_HT40_21,
	ALL_TARGET_HT40_22,
	ALL_TARGET_HT40_23,
};

/**
 * ar9003_hw_set_channel - set channel on single-chip device
 * @ah: atheros hardware structure
 * @chan:
 *
 * This is the function to change channel on single-chip devices, that is
 * for AR9300 family of chipsets.
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
 *
 * For 5GHz channels which are 5MHz spaced,
 * Channel Frequency = (3/2) * freq_ref * (chansel[8:0] + chanfrac[16:0]/2^17)
 * (freq_ref = 40MHz)
 */
static int ar9003_hw_set_channel(struct ath_hw *ah, struct ath9k_channel *chan)
{
	u16 bMode, fracMode = 0, aModeRefSel = 0;
	u32 freq, chan_frac, div, channelSel = 0, reg32 = 0;
	struct chan_centers centers;
	int loadSynthChannel;

	ath9k_hw_get_channel_centers(ah, chan, &centers);
	freq = centers.synth_center;

	if (freq < 4800) {     /* 2 GHz, fractional mode */
		if (AR_SREV_9330(ah)) {
			if (ah->is_clk_25mhz)
				div = 75;
			else
				div = 120;

			channelSel = (freq * 4) / div;
			chan_frac = (((freq * 4) % div) * 0x20000) / div;
			channelSel = (channelSel << 17) | chan_frac;
		} else if (AR_SREV_9485(ah) || AR_SREV_9565(ah)) {
			/*
			 * freq_ref = 40 / (refdiva >> amoderefsel);
			 * where refdiva=1 and amoderefsel=0
			 * ndiv = ((chan_mhz * 4) / 3) / freq_ref;
			 * chansel = int(ndiv), chanfrac = (ndiv - chansel) * 0x20000
			 */
			channelSel = (freq * 4) / 120;
			chan_frac = (((freq * 4) % 120) * 0x20000) / 120;
			channelSel = (channelSel << 17) | chan_frac;
		} else if (AR_SREV_9340(ah)) {
			if (ah->is_clk_25mhz) {
				channelSel = (freq * 2) / 75;
				chan_frac = (((freq * 2) % 75) * 0x20000) / 75;
				channelSel = (channelSel << 17) | chan_frac;
			} else {
				channelSel = CHANSEL_2G(freq) >> 1;
			}
		} else if (AR_SREV_9550(ah) || AR_SREV_9531(ah) ||
			   AR_SREV_9561(ah)) {
			if (ah->is_clk_25mhz)
				div = 75;
			else
				div = 120;

			channelSel = (freq * 4) / div;
			chan_frac = (((freq * 4) % div) * 0x20000) / div;
			channelSel = (channelSel << 17) | chan_frac;
		} else {
			channelSel = CHANSEL_2G(freq);
		}
		/* Set to 2G mode */
		bMode = 1;
	} else {
		if ((AR_SREV_9340(ah) || AR_SREV_9550(ah) ||
		     AR_SREV_9531(ah) || AR_SREV_9561(ah)) &&
		    ah->is_clk_25mhz) {
			channelSel = freq / 75;
			chan_frac = ((freq % 75) * 0x20000) / 75;
			channelSel = (channelSel << 17) | chan_frac;
		} else {
			channelSel = CHANSEL_5G(freq);
			/* Doubler is ON, so, divide channelSel by 2. */
			channelSel >>= 1;
		}
		/* Set to 5G mode */
		bMode = 0;
	}

	/* Enable fractional mode for all channels */
	fracMode = 1;
	aModeRefSel = 0;
	loadSynthChannel = 0;

	reg32 = (bMode << 29);
	REG_WRITE(ah, AR_PHY_SYNTH_CONTROL, reg32);

	/* Enable Long shift Select for Synthesizer */
	REG_RMW_FIELD(ah, AR_PHY_65NM_CH0_SYNTH4,
		      AR_PHY_SYNTH4_LONG_SHIFT_SELECT, 1);

	/* Program Synth. setting */
	reg32 = (channelSel << 2) | (fracMode << 30) |
		(aModeRefSel << 28) | (loadSynthChannel << 31);
	REG_WRITE(ah, AR_PHY_65NM_CH0_SYNTH7, reg32);

	/* Toggle Load Synth channel bit */
	loadSynthChannel = 1;
	reg32 = (channelSel << 2) | (fracMode << 30) |
		(aModeRefSel << 28) | (loadSynthChannel << 31);
	REG_WRITE(ah, AR_PHY_65NM_CH0_SYNTH7, reg32);

	ah->curchan = chan;

	return 0;
}

/**
 * ar9003_hw_spur_mitigate_mrc_cck - convert baseband spur frequency
 * @ah: atheros hardware structure
 * @chan:
 *
 * For single-chip solutions. Converts to baseband spur frequency given the
 * input channel frequency and compute register settings below.
 *
 * Spur mitigation for MRC CCK
 */
static void ar9003_hw_spur_mitigate_mrc_cck(struct ath_hw *ah,
					    struct ath9k_channel *chan)
{
	static const u32 spur_freq[4] = { 2420, 2440, 2464, 2480 };
	int cur_bb_spur, negative = 0, cck_spur_freq;
	int i;
	int range, max_spur_cnts, synth_freq;
	u8 *spur_fbin_ptr = ar9003_get_spur_chan_ptr(ah, IS_CHAN_2GHZ(chan));

	/*
	 * Need to verify range +/- 10 MHz in control channel, otherwise spur
	 * is out-of-band and can be ignored.
	 */

	if (AR_SREV_9485(ah) || AR_SREV_9340(ah) || AR_SREV_9330(ah) ||
	    AR_SREV_9550(ah) || AR_SREV_9561(ah)) {
		if (spur_fbin_ptr[0] == 0) /* No spur */
			return;
		max_spur_cnts = 5;
		if (IS_CHAN_HT40(chan)) {
			range = 19;
			if (REG_READ_FIELD(ah, AR_PHY_GEN_CTRL,
					   AR_PHY_GC_DYN2040_PRI_CH) == 0)
				synth_freq = chan->channel + 10;
			else
				synth_freq = chan->channel - 10;
		} else {
			range = 10;
			synth_freq = chan->channel;
		}
	} else {
		range = AR_SREV_9462(ah) ? 5 : 10;
		max_spur_cnts = 4;
		synth_freq = chan->channel;
	}

	for (i = 0; i < max_spur_cnts; i++) {
		if (AR_SREV_9462(ah) && (i == 0 || i == 3))
			continue;

		negative = 0;
		if (AR_SREV_9485(ah) || AR_SREV_9340(ah) || AR_SREV_9330(ah) ||
		    AR_SREV_9550(ah) || AR_SREV_9561(ah))
			cur_bb_spur = ath9k_hw_fbin2freq(spur_fbin_ptr[i],
							 IS_CHAN_2GHZ(chan));
		else
			cur_bb_spur = spur_freq[i];

		cur_bb_spur -= synth_freq;
		if (cur_bb_spur < 0) {
			negative = 1;
			cur_bb_spur = -cur_bb_spur;
		}
		if (cur_bb_spur < range) {
			cck_spur_freq = (int)((cur_bb_spur << 19) / 11);

			if (negative == 1)
				cck_spur_freq = -cck_spur_freq;

			cck_spur_freq = cck_spur_freq & 0xfffff;

			REG_RMW_FIELD(ah, AR_PHY_AGC_CONTROL,
				      AR_PHY_AGC_CONTROL_YCOK_MAX, 0x7);
			REG_RMW_FIELD(ah, AR_PHY_CCK_SPUR_MIT,
				      AR_PHY_CCK_SPUR_MIT_SPUR_RSSI_THR, 0x7f);
			REG_RMW_FIELD(ah, AR_PHY_CCK_SPUR_MIT,
				      AR_PHY_CCK_SPUR_MIT_SPUR_FILTER_TYPE,
				      0x2);
			REG_RMW_FIELD(ah, AR_PHY_CCK_SPUR_MIT,
				      AR_PHY_CCK_SPUR_MIT_USE_CCK_SPUR_MIT,
				      0x1);
			REG_RMW_FIELD(ah, AR_PHY_CCK_SPUR_MIT,
				      AR_PHY_CCK_SPUR_MIT_CCK_SPUR_FREQ,
				      cck_spur_freq);

			return;
		}
	}

	REG_RMW_FIELD(ah, AR_PHY_AGC_CONTROL,
		      AR_PHY_AGC_CONTROL_YCOK_MAX, 0x5);
	REG_RMW_FIELD(ah, AR_PHY_CCK_SPUR_MIT,
		      AR_PHY_CCK_SPUR_MIT_USE_CCK_SPUR_MIT, 0x0);
	REG_RMW_FIELD(ah, AR_PHY_CCK_SPUR_MIT,
		      AR_PHY_CCK_SPUR_MIT_CCK_SPUR_FREQ, 0x0);
}

/* Clean all spur register fields */
static void ar9003_hw_spur_ofdm_clear(struct ath_hw *ah)
{
	REG_RMW_FIELD(ah, AR_PHY_TIMING4,
		      AR_PHY_TIMING4_ENABLE_SPUR_FILTER, 0);
	REG_RMW_FIELD(ah, AR_PHY_TIMING11,
		      AR_PHY_TIMING11_SPUR_FREQ_SD, 0);
	REG_RMW_FIELD(ah, AR_PHY_TIMING11,
		      AR_PHY_TIMING11_SPUR_DELTA_PHASE, 0);
	REG_RMW_FIELD(ah, AR_PHY_SFCORR_EXT,
		      AR_PHY_SFCORR_EXT_SPUR_SUBCHANNEL_SD, 0);
	REG_RMW_FIELD(ah, AR_PHY_TIMING11,
		      AR_PHY_TIMING11_USE_SPUR_FILTER_IN_AGC, 0);
	REG_RMW_FIELD(ah, AR_PHY_TIMING11,
		      AR_PHY_TIMING11_USE_SPUR_FILTER_IN_SELFCOR, 0);
	REG_RMW_FIELD(ah, AR_PHY_TIMING4,
		      AR_PHY_TIMING4_ENABLE_SPUR_RSSI, 0);
	REG_RMW_FIELD(ah, AR_PHY_SPUR_REG,
		      AR_PHY_SPUR_REG_EN_VIT_SPUR_RSSI, 0);
	REG_RMW_FIELD(ah, AR_PHY_SPUR_REG,
		      AR_PHY_SPUR_REG_ENABLE_NF_RSSI_SPUR_MIT, 0);

	REG_RMW_FIELD(ah, AR_PHY_SPUR_REG,
		      AR_PHY_SPUR_REG_ENABLE_MASK_PPM, 0);
	REG_RMW_FIELD(ah, AR_PHY_TIMING4,
		      AR_PHY_TIMING4_ENABLE_PILOT_MASK, 0);
	REG_RMW_FIELD(ah, AR_PHY_TIMING4,
		      AR_PHY_TIMING4_ENABLE_CHAN_MASK, 0);
	REG_RMW_FIELD(ah, AR_PHY_PILOT_SPUR_MASK,
		      AR_PHY_PILOT_SPUR_MASK_CF_PILOT_MASK_IDX_A, 0);
	REG_RMW_FIELD(ah, AR_PHY_SPUR_MASK_A,
		      AR_PHY_SPUR_MASK_A_CF_PUNC_MASK_IDX_A, 0);
	REG_RMW_FIELD(ah, AR_PHY_CHAN_SPUR_MASK,
		      AR_PHY_CHAN_SPUR_MASK_CF_CHAN_MASK_IDX_A, 0);
	REG_RMW_FIELD(ah, AR_PHY_PILOT_SPUR_MASK,
		      AR_PHY_PILOT_SPUR_MASK_CF_PILOT_MASK_A, 0);
	REG_RMW_FIELD(ah, AR_PHY_CHAN_SPUR_MASK,
		      AR_PHY_CHAN_SPUR_MASK_CF_CHAN_MASK_A, 0);
	REG_RMW_FIELD(ah, AR_PHY_SPUR_MASK_A,
		      AR_PHY_SPUR_MASK_A_CF_PUNC_MASK_A, 0);
	REG_RMW_FIELD(ah, AR_PHY_SPUR_REG,
		      AR_PHY_SPUR_REG_MASK_RATE_CNTL, 0);
}

static void ar9003_hw_spur_ofdm(struct ath_hw *ah,
				int freq_offset,
				int spur_freq_sd,
				int spur_delta_phase,
				int spur_subchannel_sd,
				int range,
				int synth_freq)
{
	int mask_index = 0;

	/* OFDM Spur mitigation */
	REG_RMW_FIELD(ah, AR_PHY_TIMING4,
		 AR_PHY_TIMING4_ENABLE_SPUR_FILTER, 0x1);
	REG_RMW_FIELD(ah, AR_PHY_TIMING11,
		      AR_PHY_TIMING11_SPUR_FREQ_SD, spur_freq_sd);
	REG_RMW_FIELD(ah, AR_PHY_TIMING11,
		      AR_PHY_TIMING11_SPUR_DELTA_PHASE, spur_delta_phase);
	REG_RMW_FIELD(ah, AR_PHY_SFCORR_EXT,
		      AR_PHY_SFCORR_EXT_SPUR_SUBCHANNEL_SD, spur_subchannel_sd);
	REG_RMW_FIELD(ah, AR_PHY_TIMING11,
		      AR_PHY_TIMING11_USE_SPUR_FILTER_IN_AGC, 0x1);

	if (!(AR_SREV_9565(ah) && range == 10 && synth_freq == 2437))
		REG_RMW_FIELD(ah, AR_PHY_TIMING11,
			      AR_PHY_TIMING11_USE_SPUR_FILTER_IN_SELFCOR, 0x1);

	REG_RMW_FIELD(ah, AR_PHY_TIMING4,
		      AR_PHY_TIMING4_ENABLE_SPUR_RSSI, 0x1);
	REG_RMW_FIELD(ah, AR_PHY_SPUR_REG,
		      AR_PHY_SPUR_REG_SPUR_RSSI_THRESH, 34);
	REG_RMW_FIELD(ah, AR_PHY_SPUR_REG,
		      AR_PHY_SPUR_REG_EN_VIT_SPUR_RSSI, 1);

	if (!AR_SREV_9340(ah) &&
	    REG_READ_FIELD(ah, AR_PHY_MODE,
			   AR_PHY_MODE_DYNAMIC) == 0x1)
		REG_RMW_FIELD(ah, AR_PHY_SPUR_REG,
			      AR_PHY_SPUR_REG_ENABLE_NF_RSSI_SPUR_MIT, 1);

	mask_index = (freq_offset << 4) / 5;
	if (mask_index < 0)
		mask_index = mask_index - 1;

	mask_index = mask_index & 0x7f;

	REG_RMW_FIELD(ah, AR_PHY_SPUR_REG,
		      AR_PHY_SPUR_REG_ENABLE_MASK_PPM, 0x1);
	REG_RMW_FIELD(ah, AR_PHY_TIMING4,
		      AR_PHY_TIMING4_ENABLE_PILOT_MASK, 0x1);
	REG_RMW_FIELD(ah, AR_PHY_TIMING4,
		      AR_PHY_TIMING4_ENABLE_CHAN_MASK, 0x1);
	REG_RMW_FIELD(ah, AR_PHY_PILOT_SPUR_MASK,
		      AR_PHY_PILOT_SPUR_MASK_CF_PILOT_MASK_IDX_A, mask_index);
	REG_RMW_FIELD(ah, AR_PHY_SPUR_MASK_A,
		      AR_PHY_SPUR_MASK_A_CF_PUNC_MASK_IDX_A, mask_index);
	REG_RMW_FIELD(ah, AR_PHY_CHAN_SPUR_MASK,
		      AR_PHY_CHAN_SPUR_MASK_CF_CHAN_MASK_IDX_A, mask_index);
	REG_RMW_FIELD(ah, AR_PHY_PILOT_SPUR_MASK,
		      AR_PHY_PILOT_SPUR_MASK_CF_PILOT_MASK_A, 0xc);
	REG_RMW_FIELD(ah, AR_PHY_CHAN_SPUR_MASK,
		      AR_PHY_CHAN_SPUR_MASK_CF_CHAN_MASK_A, 0xc);
	REG_RMW_FIELD(ah, AR_PHY_SPUR_MASK_A,
		      AR_PHY_SPUR_MASK_A_CF_PUNC_MASK_A, 0xa0);
	REG_RMW_FIELD(ah, AR_PHY_SPUR_REG,
		      AR_PHY_SPUR_REG_MASK_RATE_CNTL, 0xff);
}

static void ar9003_hw_spur_ofdm_9565(struct ath_hw *ah,
				     int freq_offset)
{
	int mask_index = 0;

	mask_index = (freq_offset << 4) / 5;
	if (mask_index < 0)
		mask_index = mask_index - 1;

	mask_index = mask_index & 0x7f;

	REG_RMW_FIELD(ah, AR_PHY_PILOT_SPUR_MASK,
		      AR_PHY_PILOT_SPUR_MASK_CF_PILOT_MASK_IDX_B,
		      mask_index);

	/* A == B */
	REG_RMW_FIELD(ah, AR_PHY_SPUR_MASK_B,
		      AR_PHY_SPUR_MASK_A_CF_PUNC_MASK_IDX_A,
		      mask_index);

	REG_RMW_FIELD(ah, AR_PHY_CHAN_SPUR_MASK,
		      AR_PHY_CHAN_SPUR_MASK_CF_CHAN_MASK_IDX_B,
		      mask_index);
	REG_RMW_FIELD(ah, AR_PHY_PILOT_SPUR_MASK,
		      AR_PHY_PILOT_SPUR_MASK_CF_PILOT_MASK_B, 0xe);
	REG_RMW_FIELD(ah, AR_PHY_CHAN_SPUR_MASK,
		      AR_PHY_CHAN_SPUR_MASK_CF_CHAN_MASK_B, 0xe);

	/* A == B */
	REG_RMW_FIELD(ah, AR_PHY_SPUR_MASK_B,
		      AR_PHY_SPUR_MASK_A_CF_PUNC_MASK_A, 0xa0);
}

static void ar9003_hw_spur_ofdm_work(struct ath_hw *ah,
				     struct ath9k_channel *chan,
				     int freq_offset,
				     int range,
				     int synth_freq)
{
	int spur_freq_sd = 0;
	int spur_subchannel_sd = 0;
	int spur_delta_phase = 0;

	if (IS_CHAN_HT40(chan)) {
		if (freq_offset < 0) {
			if (REG_READ_FIELD(ah, AR_PHY_GEN_CTRL,
					   AR_PHY_GC_DYN2040_PRI_CH) == 0x0)
				spur_subchannel_sd = 1;
			else
				spur_subchannel_sd = 0;

			spur_freq_sd = ((freq_offset + 10) << 9) / 11;

		} else {
			if (REG_READ_FIELD(ah, AR_PHY_GEN_CTRL,
			    AR_PHY_GC_DYN2040_PRI_CH) == 0x0)
				spur_subchannel_sd = 0;
			else
				spur_subchannel_sd = 1;

			spur_freq_sd = ((freq_offset - 10) << 9) / 11;

		}

		spur_delta_phase = (freq_offset << 17) / 5;

	} else {
		spur_subchannel_sd = 0;
		spur_freq_sd = (freq_offset << 9) /11;
		spur_delta_phase = (freq_offset << 18) / 5;
	}

	spur_freq_sd = spur_freq_sd & 0x3ff;
	spur_delta_phase = spur_delta_phase & 0xfffff;

	ar9003_hw_spur_ofdm(ah,
			    freq_offset,
			    spur_freq_sd,
			    spur_delta_phase,
			    spur_subchannel_sd,
			    range, synth_freq);
}

/* Spur mitigation for OFDM */
static void ar9003_hw_spur_mitigate_ofdm(struct ath_hw *ah,
					 struct ath9k_channel *chan)
{
	int synth_freq;
	int range = 10;
	int freq_offset = 0;
	int mode;
	u8* spurChansPtr;
	unsigned int i;
	struct ar9300_eeprom *eep = &ah->eeprom.ar9300_eep;

	if (IS_CHAN_5GHZ(chan)) {
		spurChansPtr = &(eep->modalHeader5G.spurChans[0]);
		mode = 0;
	}
	else {
		spurChansPtr = &(eep->modalHeader2G.spurChans[0]);
		mode = 1;
	}

	if (spurChansPtr[0] == 0)
		return; /* No spur in the mode */

	if (IS_CHAN_HT40(chan)) {
		range = 19;
		if (REG_READ_FIELD(ah, AR_PHY_GEN_CTRL,
				   AR_PHY_GC_DYN2040_PRI_CH) == 0x0)
			synth_freq = chan->channel - 10;
		else
			synth_freq = chan->channel + 10;
	} else {
		range = 10;
		synth_freq = chan->channel;
	}

	ar9003_hw_spur_ofdm_clear(ah);

	for (i = 0; i < AR_EEPROM_MODAL_SPURS && spurChansPtr[i]; i++) {
		freq_offset = ath9k_hw_fbin2freq(spurChansPtr[i], mode);
		freq_offset -= synth_freq;
		if (abs(freq_offset) < range) {
			ar9003_hw_spur_ofdm_work(ah, chan, freq_offset,
						 range, synth_freq);

			if (AR_SREV_9565(ah) && (i < 4)) {
				freq_offset = ath9k_hw_fbin2freq(spurChansPtr[i + 1],
								 mode);
				freq_offset -= synth_freq;
				if (abs(freq_offset) < range)
					ar9003_hw_spur_ofdm_9565(ah, freq_offset);
			}

			break;
		}
	}
}

static void ar9003_hw_spur_mitigate(struct ath_hw *ah,
				    struct ath9k_channel *chan)
{
	if (!AR_SREV_9565(ah))
		ar9003_hw_spur_mitigate_mrc_cck(ah, chan);
	ar9003_hw_spur_mitigate_ofdm(ah, chan);
}

static u32 ar9003_hw_compute_pll_control_soc(struct ath_hw *ah,
					     struct ath9k_channel *chan)
{
	u32 pll;

	pll = SM(0x5, AR_RTC_9300_SOC_PLL_REFDIV);

	if (chan && IS_CHAN_HALF_RATE(chan))
		pll |= SM(0x1, AR_RTC_9300_SOC_PLL_CLKSEL);
	else if (chan && IS_CHAN_QUARTER_RATE(chan))
		pll |= SM(0x2, AR_RTC_9300_SOC_PLL_CLKSEL);

	pll |= SM(0x2c, AR_RTC_9300_SOC_PLL_DIV_INT);

	return pll;
}

static u32 ar9003_hw_compute_pll_control(struct ath_hw *ah,
					 struct ath9k_channel *chan)
{
	u32 pll;

	pll = SM(0x5, AR_RTC_9300_PLL_REFDIV);

	if (chan && IS_CHAN_HALF_RATE(chan))
		pll |= SM(0x1, AR_RTC_9300_PLL_CLKSEL);
	else if (chan && IS_CHAN_QUARTER_RATE(chan))
		pll |= SM(0x2, AR_RTC_9300_PLL_CLKSEL);

	pll |= SM(0x2c, AR_RTC_9300_PLL_DIV);

	return pll;
}

static void ar9003_hw_set_channel_regs(struct ath_hw *ah,
				       struct ath9k_channel *chan)
{
	u32 phymode;
	u32 enableDacFifo = 0;

	enableDacFifo =
		(REG_READ(ah, AR_PHY_GEN_CTRL) & AR_PHY_GC_ENABLE_DAC_FIFO);

	/* Enable 11n HT, 20 MHz */
	phymode = AR_PHY_GC_HT_EN | AR_PHY_GC_SHORT_GI_40 | enableDacFifo;

	if (!AR_SREV_9561(ah))
		phymode |= AR_PHY_GC_SINGLE_HT_LTF1;

	/* Configure baseband for dynamic 20/40 operation */
	if (IS_CHAN_HT40(chan)) {
		phymode |= AR_PHY_GC_DYN2040_EN;
		/* Configure control (primary) channel at +-10MHz */
		if (IS_CHAN_HT40PLUS(chan))
			phymode |= AR_PHY_GC_DYN2040_PRI_CH;

	}

	/* make sure we preserve INI settings */
	phymode |= REG_READ(ah, AR_PHY_GEN_CTRL);
	/* turn off Green Field detection for STA for now */
	phymode &= ~AR_PHY_GC_GF_DETECT_EN;

	REG_WRITE(ah, AR_PHY_GEN_CTRL, phymode);

	/* Configure MAC for 20/40 operation */
	ath9k_hw_set11nmac2040(ah, chan);

	/* global transmit timeout (25 TUs default)*/
	REG_WRITE(ah, AR_GTXTO, 25 << AR_GTXTO_TIMEOUT_LIMIT_S);
	/* carrier sense timeout */
	REG_WRITE(ah, AR_CST, 0xF << AR_CST_TIMEOUT_LIMIT_S);
}

static void ar9003_hw_init_bb(struct ath_hw *ah,
			      struct ath9k_channel *chan)
{
	u32 synthDelay;

	/*
	 * Wait for the frequency synth to settle (synth goes on
	 * via AR_PHY_ACTIVE_EN).  Read the phy active delay register.
	 * Value is in 100ns increments.
	 */
	synthDelay = REG_READ(ah, AR_PHY_RX_DELAY) & AR_PHY_RX_DELAY_DELAY;

	/* Activate the PHY (includes baseband activate + synthesizer on) */
	REG_WRITE(ah, AR_PHY_ACTIVE, AR_PHY_ACTIVE_EN);
	ath9k_hw_synth_delay(ah, chan, synthDelay);
}

void ar9003_hw_set_chain_masks(struct ath_hw *ah, u8 rx, u8 tx)
{
	if (ah->caps.tx_chainmask == 5 || ah->caps.rx_chainmask == 5)
		REG_SET_BIT(ah, AR_PHY_ANALOG_SWAP,
			    AR_PHY_SWAP_ALT_CHAIN);

	REG_WRITE(ah, AR_PHY_RX_CHAINMASK, rx);
	REG_WRITE(ah, AR_PHY_CAL_CHAINMASK, rx);

	if ((ah->caps.hw_caps & ATH9K_HW_CAP_APM) && (tx == 0x7))
		tx = 3;

	REG_WRITE(ah, AR_SELFGEN_MASK, tx);
}

/*
 * Override INI values with chip specific configuration.
 */
static void ar9003_hw_override_ini(struct ath_hw *ah)
{
	u32 val;

	/*
	 * Set the RX_ABORT and RX_DIS and clear it only after
	 * RXE is set for MAC. This prevents frames with
	 * corrupted descriptor status.
	 */
	REG_SET_BIT(ah, AR_DIAG_SW, (AR_DIAG_RX_DIS | AR_DIAG_RX_ABORT));

	/*
	 * For AR9280 and above, there is a new feature that allows
	 * Multicast search based on both MAC Address and Key ID. By default,
	 * this feature is enabled. But since the driver is not using this
	 * feature, we switch it off; otherwise multicast search based on
	 * MAC addr only will fail.
	 */
	val = REG_READ(ah, AR_PCU_MISC_MODE2) & (~AR_ADHOC_MCAST_KEYID_ENABLE);
	val |= AR_AGG_WEP_ENABLE_FIX |
	       AR_AGG_WEP_ENABLE |
	       AR_PCU_MISC_MODE2_CFP_IGNORE;
	REG_WRITE(ah, AR_PCU_MISC_MODE2, val);

	if (AR_SREV_9462(ah) || AR_SREV_9565(ah)) {
		REG_WRITE(ah, AR_GLB_SWREG_DISCONT_MODE,
			  AR_GLB_SWREG_DISCONT_EN_BT_WLAN);

		if (REG_READ_FIELD(ah, AR_PHY_TX_IQCAL_CONTROL_0,
				   AR_PHY_TX_IQCAL_CONTROL_0_ENABLE_TXIQ_CAL))
			ah->enabled_cals |= TX_IQ_CAL;
		else
			ah->enabled_cals &= ~TX_IQ_CAL;

	}

	if (REG_READ(ah, AR_PHY_CL_CAL_CTL) & AR_PHY_CL_CAL_ENABLE)
		ah->enabled_cals |= TX_CL_CAL;
	else
		ah->enabled_cals &= ~TX_CL_CAL;

	if (AR_SREV_9340(ah) || AR_SREV_9531(ah) || AR_SREV_9550(ah) ||
	    AR_SREV_9561(ah)) {
		if (ah->is_clk_25mhz) {
			REG_WRITE(ah, AR_RTC_DERIVED_CLK, 0x17c << 1);
			REG_WRITE(ah, AR_SLP32_MODE, 0x0010f3d7);
			REG_WRITE(ah, AR_SLP32_INC, 0x0001e7ae);
		} else {
			REG_WRITE(ah, AR_RTC_DERIVED_CLK, 0x261 << 1);
			REG_WRITE(ah, AR_SLP32_MODE, 0x0010f400);
			REG_WRITE(ah, AR_SLP32_INC, 0x0001e800);
		}
		udelay(100);
	}
}

static void ar9003_hw_prog_ini(struct ath_hw *ah,
			       struct ar5416IniArray *iniArr,
			       int column)
{
	unsigned int i, regWrites = 0;

	/* New INI format: Array may be undefined (pre, core, post arrays) */
	if (!iniArr->ia_array)
		return;

	/*
	 * New INI format: Pre, core, and post arrays for a given subsystem
	 * may be modal (> 2 columns) or non-modal (2 columns). Determine if
	 * the array is non-modal and force the column to 1.
	 */
	if (column >= iniArr->ia_columns)
		column = 1;

	for (i = 0; i < iniArr->ia_rows; i++) {
		u32 reg = INI_RA(iniArr, i, 0);
		u32 val = INI_RA(iniArr, i, column);

		REG_WRITE(ah, reg, val);

		DO_DELAY(regWrites);
	}
}

static int ar9550_hw_get_modes_txgain_index(struct ath_hw *ah,
					    struct ath9k_channel *chan)
{
	int ret;

	if (IS_CHAN_2GHZ(chan)) {
		if (IS_CHAN_HT40(chan))
			return 7;
		else
			return 8;
	}

	if (chan->channel <= 5350)
		ret = 1;
	else if ((chan->channel > 5350) && (chan->channel <= 5600))
		ret = 3;
	else
		ret = 5;

	if (IS_CHAN_HT40(chan))
		ret++;

	return ret;
}

static int ar9561_hw_get_modes_txgain_index(struct ath_hw *ah,
					    struct ath9k_channel *chan)
{
	if (IS_CHAN_2GHZ(chan)) {
		if (IS_CHAN_HT40(chan))
			return 1;
		else
			return 2;
	}

	return 0;
}

static void ar9003_doubler_fix(struct ath_hw *ah)
{
	if (AR_SREV_9300(ah) || AR_SREV_9580(ah) || AR_SREV_9550(ah)) {
		REG_RMW(ah, AR_PHY_65NM_CH0_RXTX2,
			1 << AR_PHY_65NM_CH0_RXTX2_SYNTHON_MASK_S |
			1 << AR_PHY_65NM_CH0_RXTX2_SYNTHOVR_MASK_S, 0);
		REG_RMW(ah, AR_PHY_65NM_CH1_RXTX2,
			1 << AR_PHY_65NM_CH0_RXTX2_SYNTHON_MASK_S |
			1 << AR_PHY_65NM_CH0_RXTX2_SYNTHOVR_MASK_S, 0);
		REG_RMW(ah, AR_PHY_65NM_CH2_RXTX2,
			1 << AR_PHY_65NM_CH0_RXTX2_SYNTHON_MASK_S |
			1 << AR_PHY_65NM_CH0_RXTX2_SYNTHOVR_MASK_S, 0);

		udelay(200);

		REG_CLR_BIT(ah, AR_PHY_65NM_CH0_RXTX2,
			    AR_PHY_65NM_CH0_RXTX2_SYNTHON_MASK);
		REG_CLR_BIT(ah, AR_PHY_65NM_CH1_RXTX2,
			    AR_PHY_65NM_CH0_RXTX2_SYNTHON_MASK);
		REG_CLR_BIT(ah, AR_PHY_65NM_CH2_RXTX2,
			    AR_PHY_65NM_CH0_RXTX2_SYNTHON_MASK);

		udelay(1);

		REG_RMW_FIELD(ah, AR_PHY_65NM_CH0_RXTX2,
			      AR_PHY_65NM_CH0_RXTX2_SYNTHON_MASK, 1);
		REG_RMW_FIELD(ah, AR_PHY_65NM_CH1_RXTX2,
			      AR_PHY_65NM_CH0_RXTX2_SYNTHON_MASK, 1);
		REG_RMW_FIELD(ah, AR_PHY_65NM_CH2_RXTX2,
			      AR_PHY_65NM_CH0_RXTX2_SYNTHON_MASK, 1);

		udelay(200);

		REG_RMW_FIELD(ah, AR_PHY_65NM_CH0_SYNTH12,
			      AR_PHY_65NM_CH0_SYNTH12_VREFMUL3, 0xf);

		REG_RMW(ah, AR_PHY_65NM_CH0_RXTX2, 0,
			1 << AR_PHY_65NM_CH0_RXTX2_SYNTHON_MASK_S |
			1 << AR_PHY_65NM_CH0_RXTX2_SYNTHOVR_MASK_S);
		REG_RMW(ah, AR_PHY_65NM_CH1_RXTX2, 0,
			1 << AR_PHY_65NM_CH0_RXTX2_SYNTHON_MASK_S |
			1 << AR_PHY_65NM_CH0_RXTX2_SYNTHOVR_MASK_S);
		REG_RMW(ah, AR_PHY_65NM_CH2_RXTX2, 0,
			1 << AR_PHY_65NM_CH0_RXTX2_SYNTHON_MASK_S |
			1 << AR_PHY_65NM_CH0_RXTX2_SYNTHOVR_MASK_S);
	}
}

static int ar9003_hw_process_ini(struct ath_hw *ah,
				 struct ath9k_channel *chan)
{
	unsigned int regWrites = 0, i;
	u32 modesIndex;

	if (IS_CHAN_5GHZ(chan))
		modesIndex = IS_CHAN_HT40(chan) ? 2 : 1;
	else
		modesIndex = IS_CHAN_HT40(chan) ? 3 : 4;

	/*
	 * SOC, MAC, BB, RADIO initvals.
	 */
	for (i = 0; i < ATH_INI_NUM_SPLIT; i++) {
		ar9003_hw_prog_ini(ah, &ah->iniSOC[i], modesIndex);
		ar9003_hw_prog_ini(ah, &ah->iniMac[i], modesIndex);
		ar9003_hw_prog_ini(ah, &ah->iniBB[i], modesIndex);
		ar9003_hw_prog_ini(ah, &ah->iniRadio[i], modesIndex);
		if (i == ATH_INI_POST && AR_SREV_9462_20_OR_LATER(ah))
			ar9003_hw_prog_ini(ah,
					   &ah->ini_radio_post_sys2ant,
					   modesIndex);
	}

	ar9003_doubler_fix(ah);

	/*
	 * RXGAIN initvals.
	 */
	REG_WRITE_ARRAY(&ah->iniModesRxGain, 1, regWrites);

	if (AR_SREV_9462_20_OR_LATER(ah)) {
		/*
		 * CUS217 mix LNA mode.
		 */
		if (ar9003_hw_get_rx_gain_idx(ah) == 2) {
			REG_WRITE_ARRAY(&ah->ini_modes_rxgain_bb_core,
					1, regWrites);
			REG_WRITE_ARRAY(&ah->ini_modes_rxgain_bb_postamble,
					modesIndex, regWrites);
		}

		/*
		 * 5G-XLNA
		 */
		if ((ar9003_hw_get_rx_gain_idx(ah) == 2) ||
		    (ar9003_hw_get_rx_gain_idx(ah) == 3)) {
			REG_WRITE_ARRAY(&ah->ini_modes_rxgain_xlna,
					modesIndex, regWrites);
		}
	}

	if (AR_SREV_9550(ah) || AR_SREV_9561(ah))
		REG_WRITE_ARRAY(&ah->ini_modes_rx_gain_bounds, modesIndex,
				regWrites);

	if (AR_SREV_9561(ah) && (ar9003_hw_get_rx_gain_idx(ah) == 0))
		REG_WRITE_ARRAY(&ah->ini_modes_rxgain_xlna,
				modesIndex, regWrites);
	/*
	 * TXGAIN initvals.
	 */
	if (AR_SREV_9550(ah) || AR_SREV_9531(ah) || AR_SREV_9561(ah)) {
		int modes_txgain_index = 1;

		if (AR_SREV_9550(ah))
			modes_txgain_index = ar9550_hw_get_modes_txgain_index(ah, chan);

		if (AR_SREV_9561(ah))
			modes_txgain_index =
				ar9561_hw_get_modes_txgain_index(ah, chan);

		if (modes_txgain_index < 0)
			return -EINVAL;

		REG_WRITE_ARRAY(&ah->iniModesTxGain, modes_txgain_index,
				regWrites);
	} else {
		REG_WRITE_ARRAY(&ah->iniModesTxGain, modesIndex, regWrites);
	}

	/*
	 * For 5GHz channels requiring Fast Clock, apply
	 * different modal values.
	 */
	if (IS_CHAN_A_FAST_CLOCK(ah, chan))
		REG_WRITE_ARRAY(&ah->iniModesFastClock,
				modesIndex, regWrites);

	/*
	 * Clock frequency initvals.
	 */
	REG_WRITE_ARRAY(&ah->iniAdditional, 1, regWrites);

	/*
	 * JAPAN regulatory.
	 */
	if (chan->channel == 2484)
		ar9003_hw_prog_ini(ah, &ah->iniCckfirJapan2484, 1);

	ah->modes_index = modesIndex;
	ar9003_hw_override_ini(ah);
	ar9003_hw_set_channel_regs(ah, chan);
	ar9003_hw_set_chain_masks(ah, ah->rxchainmask, ah->txchainmask);
	ath9k_hw_apply_txpower(ah, chan, false);

	return 0;
}

static void ar9003_hw_set_rfmode(struct ath_hw *ah,
				 struct ath9k_channel *chan)
{
	u32 rfMode = 0;

	if (chan == NULL)
		return;

	if (IS_CHAN_2GHZ(chan))
		rfMode |= AR_PHY_MODE_DYNAMIC;
	else
		rfMode |= AR_PHY_MODE_OFDM;

	if (IS_CHAN_A_FAST_CLOCK(ah, chan))
		rfMode |= (AR_PHY_MODE_DYNAMIC | AR_PHY_MODE_DYN_CCK_DISABLE);

	if (rfMode & (AR_PHY_MODE_QUARTER | AR_PHY_MODE_HALF))
		REG_RMW_FIELD(ah, AR_PHY_FRAME_CTL,
			      AR_PHY_FRAME_CTL_CF_OVERLAP_WINDOW, 3);

	REG_WRITE(ah, AR_PHY_MODE, rfMode);
}

static void ar9003_hw_mark_phy_inactive(struct ath_hw *ah)
{
	REG_WRITE(ah, AR_PHY_ACTIVE, AR_PHY_ACTIVE_DIS);
}

static void ar9003_hw_set_delta_slope(struct ath_hw *ah,
				      struct ath9k_channel *chan)
{
	u32 coef_scaled, ds_coef_exp, ds_coef_man;
	u32 clockMhzScaled = 0x64000000;
	struct chan_centers centers;

	/*
	 * half and quarter rate can divide the scaled clock by 2 or 4
	 * scale for selected channel bandwidth
	 */
	if (IS_CHAN_HALF_RATE(chan))
		clockMhzScaled = clockMhzScaled >> 1;
	else if (IS_CHAN_QUARTER_RATE(chan))
		clockMhzScaled = clockMhzScaled >> 2;

	/*
	 * ALGO -> coef = 1e8/fcarrier*fclock/40;
	 * scaled coef to provide precision for this floating calculation
	 */
	ath9k_hw_get_channel_centers(ah, chan, &centers);
	coef_scaled = clockMhzScaled / centers.synth_center;

	ath9k_hw_get_delta_slope_vals(ah, coef_scaled, &ds_coef_man,
				      &ds_coef_exp);

	REG_RMW_FIELD(ah, AR_PHY_TIMING3,
		      AR_PHY_TIMING3_DSC_MAN, ds_coef_man);
	REG_RMW_FIELD(ah, AR_PHY_TIMING3,
		      AR_PHY_TIMING3_DSC_EXP, ds_coef_exp);

	/*
	 * For Short GI,
	 * scaled coeff is 9/10 that of normal coeff
	 */
	coef_scaled = (9 * coef_scaled) / 10;

	ath9k_hw_get_delta_slope_vals(ah, coef_scaled, &ds_coef_man,
				      &ds_coef_exp);

	/* for short gi */
	REG_RMW_FIELD(ah, AR_PHY_SGI_DELTA,
		      AR_PHY_SGI_DSC_MAN, ds_coef_man);
	REG_RMW_FIELD(ah, AR_PHY_SGI_DELTA,
		      AR_PHY_SGI_DSC_EXP, ds_coef_exp);
}

static bool ar9003_hw_rfbus_req(struct ath_hw *ah)
{
	REG_WRITE(ah, AR_PHY_RFBUS_REQ, AR_PHY_RFBUS_REQ_EN);
	return ath9k_hw_wait(ah, AR_PHY_RFBUS_GRANT, AR_PHY_RFBUS_GRANT_EN,
			     AR_PHY_RFBUS_GRANT_EN, AH_WAIT_TIMEOUT);
}

/*
 * Wait for the frequency synth to settle (synth goes on via PHY_ACTIVE_EN).
 * Read the phy active delay register. Value is in 100ns increments.
 */
static void ar9003_hw_rfbus_done(struct ath_hw *ah)
{
	u32 synthDelay = REG_READ(ah, AR_PHY_RX_DELAY) & AR_PHY_RX_DELAY_DELAY;

	ath9k_hw_synth_delay(ah, ah->curchan, synthDelay);

	REG_WRITE(ah, AR_PHY_RFBUS_REQ, 0);
}

static bool ar9003_hw_ani_control(struct ath_hw *ah,
				  enum ath9k_ani_cmd cmd, int param)
{
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath9k_channel *chan = ah->curchan;
	struct ar5416AniState *aniState = &ah->ani;
	int m1ThreshLow, m2ThreshLow;
	int m1Thresh, m2Thresh;
	int m2CountThr, m2CountThrLow;
	int m1ThreshLowExt, m2ThreshLowExt;
	int m1ThreshExt, m2ThreshExt;
	s32 value, value2;

	switch (cmd & ah->ani_function) {
	case ATH9K_ANI_OFDM_WEAK_SIGNAL_DETECTION:{
		/*
		 * on == 1 means ofdm weak signal detection is ON
		 * on == 1 is the default, for less noise immunity
		 *
		 * on == 0 means ofdm weak signal detection is OFF
		 * on == 0 means more noise imm
		 */
		u32 on = param ? 1 : 0;

		if (AR_SREV_9462(ah) || AR_SREV_9565(ah))
			goto skip_ws_det;

		m1ThreshLow = on ?
			aniState->iniDef.m1ThreshLow : m1ThreshLow_off;
		m2ThreshLow = on ?
			aniState->iniDef.m2ThreshLow : m2ThreshLow_off;
		m1Thresh = on ?
			aniState->iniDef.m1Thresh : m1Thresh_off;
		m2Thresh = on ?
			aniState->iniDef.m2Thresh : m2Thresh_off;
		m2CountThr = on ?
			aniState->iniDef.m2CountThr : m2CountThr_off;
		m2CountThrLow = on ?
			aniState->iniDef.m2CountThrLow : m2CountThrLow_off;
		m1ThreshLowExt = on ?
			aniState->iniDef.m1ThreshLowExt : m1ThreshLowExt_off;
		m2ThreshLowExt = on ?
			aniState->iniDef.m2ThreshLowExt : m2ThreshLowExt_off;
		m1ThreshExt = on ?
			aniState->iniDef.m1ThreshExt : m1ThreshExt_off;
		m2ThreshExt = on ?
			aniState->iniDef.m2ThreshExt : m2ThreshExt_off;

		REG_RMW_FIELD(ah, AR_PHY_SFCORR_LOW,
			      AR_PHY_SFCORR_LOW_M1_THRESH_LOW,
			      m1ThreshLow);
		REG_RMW_FIELD(ah, AR_PHY_SFCORR_LOW,
			      AR_PHY_SFCORR_LOW_M2_THRESH_LOW,
			      m2ThreshLow);
		REG_RMW_FIELD(ah, AR_PHY_SFCORR,
			      AR_PHY_SFCORR_M1_THRESH,
			      m1Thresh);
		REG_RMW_FIELD(ah, AR_PHY_SFCORR,
			      AR_PHY_SFCORR_M2_THRESH,
			      m2Thresh);
		REG_RMW_FIELD(ah, AR_PHY_SFCORR,
			      AR_PHY_SFCORR_M2COUNT_THR,
			      m2CountThr);
		REG_RMW_FIELD(ah, AR_PHY_SFCORR_LOW,
			      AR_PHY_SFCORR_LOW_M2COUNT_THR_LOW,
			      m2CountThrLow);
		REG_RMW_FIELD(ah, AR_PHY_SFCORR_EXT,
			      AR_PHY_SFCORR_EXT_M1_THRESH_LOW,
			      m1ThreshLowExt);
		REG_RMW_FIELD(ah, AR_PHY_SFCORR_EXT,
			      AR_PHY_SFCORR_EXT_M2_THRESH_LOW,
			      m2ThreshLowExt);
		REG_RMW_FIELD(ah, AR_PHY_SFCORR_EXT,
			      AR_PHY_SFCORR_EXT_M1_THRESH,
			      m1ThreshExt);
		REG_RMW_FIELD(ah, AR_PHY_SFCORR_EXT,
			      AR_PHY_SFCORR_EXT_M2_THRESH,
			      m2ThreshExt);
skip_ws_det:
		if (on)
			REG_SET_BIT(ah, AR_PHY_SFCORR_LOW,
				    AR_PHY_SFCORR_LOW_USE_SELF_CORR_LOW);
		else
			REG_CLR_BIT(ah, AR_PHY_SFCORR_LOW,
				    AR_PHY_SFCORR_LOW_USE_SELF_CORR_LOW);

		if (on != aniState->ofdmWeakSigDetect) {
			ath_dbg(common, ANI,
				"** ch %d: ofdm weak signal: %s=>%s\n",
				chan->channel,
				aniState->ofdmWeakSigDetect ?
				"on" : "off",
				on ? "on" : "off");
			if (on)
				ah->stats.ast_ani_ofdmon++;
			else
				ah->stats.ast_ani_ofdmoff++;
			aniState->ofdmWeakSigDetect = on;
		}
		break;
	}
	case ATH9K_ANI_FIRSTEP_LEVEL:{
		u32 level = param;

		if (level >= ARRAY_SIZE(firstep_table)) {
			ath_dbg(common, ANI,
				"ATH9K_ANI_FIRSTEP_LEVEL: level out of range (%u > %zu)\n",
				level, ARRAY_SIZE(firstep_table));
			return false;
		}

		/*
		 * make register setting relative to default
		 * from INI file & cap value
		 */
		value = firstep_table[level] -
			firstep_table[ATH9K_ANI_FIRSTEP_LVL] +
			aniState->iniDef.firstep;
		if (value < ATH9K_SIG_FIRSTEP_SETTING_MIN)
			value = ATH9K_SIG_FIRSTEP_SETTING_MIN;
		if (value > ATH9K_SIG_FIRSTEP_SETTING_MAX)
			value = ATH9K_SIG_FIRSTEP_SETTING_MAX;
		REG_RMW_FIELD(ah, AR_PHY_FIND_SIG,
			      AR_PHY_FIND_SIG_FIRSTEP,
			      value);
		/*
		 * we need to set first step low register too
		 * make register setting relative to default
		 * from INI file & cap value
		 */
		value2 = firstep_table[level] -
			 firstep_table[ATH9K_ANI_FIRSTEP_LVL] +
			 aniState->iniDef.firstepLow;
		if (value2 < ATH9K_SIG_FIRSTEP_SETTING_MIN)
			value2 = ATH9K_SIG_FIRSTEP_SETTING_MIN;
		if (value2 > ATH9K_SIG_FIRSTEP_SETTING_MAX)
			value2 = ATH9K_SIG_FIRSTEP_SETTING_MAX;

		REG_RMW_FIELD(ah, AR_PHY_FIND_SIG_LOW,
			      AR_PHY_FIND_SIG_LOW_FIRSTEP_LOW, value2);

		if (level != aniState->firstepLevel) {
			ath_dbg(common, ANI,
				"** ch %d: level %d=>%d[def:%d] firstep[level]=%d ini=%d\n",
				chan->channel,
				aniState->firstepLevel,
				level,
				ATH9K_ANI_FIRSTEP_LVL,
				value,
				aniState->iniDef.firstep);
			ath_dbg(common, ANI,
				"** ch %d: level %d=>%d[def:%d] firstep_low[level]=%d ini=%d\n",
				chan->channel,
				aniState->firstepLevel,
				level,
				ATH9K_ANI_FIRSTEP_LVL,
				value2,
				aniState->iniDef.firstepLow);
			if (level > aniState->firstepLevel)
				ah->stats.ast_ani_stepup++;
			else if (level < aniState->firstepLevel)
				ah->stats.ast_ani_stepdown++;
			aniState->firstepLevel = level;
		}
		break;
	}
	case ATH9K_ANI_SPUR_IMMUNITY_LEVEL:{
		u32 level = param;

		if (level >= ARRAY_SIZE(cycpwrThr1_table)) {
			ath_dbg(common, ANI,
				"ATH9K_ANI_SPUR_IMMUNITY_LEVEL: level out of range (%u > %zu)\n",
				level, ARRAY_SIZE(cycpwrThr1_table));
			return false;
		}
		/*
		 * make register setting relative to default
		 * from INI file & cap value
		 */
		value = cycpwrThr1_table[level] -
			cycpwrThr1_table[ATH9K_ANI_SPUR_IMMUNE_LVL] +
			aniState->iniDef.cycpwrThr1;
		if (value < ATH9K_SIG_SPUR_IMM_SETTING_MIN)
			value = ATH9K_SIG_SPUR_IMM_SETTING_MIN;
		if (value > ATH9K_SIG_SPUR_IMM_SETTING_MAX)
			value = ATH9K_SIG_SPUR_IMM_SETTING_MAX;
		REG_RMW_FIELD(ah, AR_PHY_TIMING5,
			      AR_PHY_TIMING5_CYCPWR_THR1,
			      value);

		/*
		 * set AR_PHY_EXT_CCA for extension channel
		 * make register setting relative to default
		 * from INI file & cap value
		 */
		value2 = cycpwrThr1_table[level] -
			 cycpwrThr1_table[ATH9K_ANI_SPUR_IMMUNE_LVL] +
			 aniState->iniDef.cycpwrThr1Ext;
		if (value2 < ATH9K_SIG_SPUR_IMM_SETTING_MIN)
			value2 = ATH9K_SIG_SPUR_IMM_SETTING_MIN;
		if (value2 > ATH9K_SIG_SPUR_IMM_SETTING_MAX)
			value2 = ATH9K_SIG_SPUR_IMM_SETTING_MAX;
		REG_RMW_FIELD(ah, AR_PHY_EXT_CCA,
			      AR_PHY_EXT_CYCPWR_THR1, value2);

		if (level != aniState->spurImmunityLevel) {
			ath_dbg(common, ANI,
				"** ch %d: level %d=>%d[def:%d] cycpwrThr1[level]=%d ini=%d\n",
				chan->channel,
				aniState->spurImmunityLevel,
				level,
				ATH9K_ANI_SPUR_IMMUNE_LVL,
				value,
				aniState->iniDef.cycpwrThr1);
			ath_dbg(common, ANI,
				"** ch %d: level %d=>%d[def:%d] cycpwrThr1Ext[level]=%d ini=%d\n",
				chan->channel,
				aniState->spurImmunityLevel,
				level,
				ATH9K_ANI_SPUR_IMMUNE_LVL,
				value2,
				aniState->iniDef.cycpwrThr1Ext);
			if (level > aniState->spurImmunityLevel)
				ah->stats.ast_ani_spurup++;
			else if (level < aniState->spurImmunityLevel)
				ah->stats.ast_ani_spurdown++;
			aniState->spurImmunityLevel = level;
		}
		break;
	}
	case ATH9K_ANI_MRC_CCK:{
		/*
		 * is_on == 1 means MRC CCK ON (default, less noise imm)
		 * is_on == 0 means MRC CCK is OFF (more noise imm)
		 */
		bool is_on = param ? 1 : 0;

		if (ah->caps.rx_chainmask == 1)
			break;

		REG_RMW_FIELD(ah, AR_PHY_MRC_CCK_CTRL,
			      AR_PHY_MRC_CCK_ENABLE, is_on);
		REG_RMW_FIELD(ah, AR_PHY_MRC_CCK_CTRL,
			      AR_PHY_MRC_CCK_MUX_REG, is_on);
		if (is_on != aniState->mrcCCK) {
			ath_dbg(common, ANI, "** ch %d: MRC CCK: %s=>%s\n",
				chan->channel,
				aniState->mrcCCK ? "on" : "off",
				is_on ? "on" : "off");
		if (is_on)
			ah->stats.ast_ani_ccklow++;
		else
			ah->stats.ast_ani_cckhigh++;
		aniState->mrcCCK = is_on;
		}
	break;
	}
	default:
		ath_dbg(common, ANI, "invalid cmd %u\n", cmd);
		return false;
	}

	ath_dbg(common, ANI,
		"ANI parameters: SI=%d, ofdmWS=%s FS=%d MRCcck=%s listenTime=%d ofdmErrs=%d cckErrs=%d\n",
		aniState->spurImmunityLevel,
		aniState->ofdmWeakSigDetect ? "on" : "off",
		aniState->firstepLevel,
		aniState->mrcCCK ? "on" : "off",
		aniState->listenTime,
		aniState->ofdmPhyErrCount,
		aniState->cckPhyErrCount);
	return true;
}

static void ar9003_hw_do_getnf(struct ath_hw *ah,
			      int16_t nfarray[NUM_NF_READINGS])
{
#define AR_PHY_CH_MINCCA_PWR	0x1FF00000
#define AR_PHY_CH_MINCCA_PWR_S	20
#define AR_PHY_CH_EXT_MINCCA_PWR 0x01FF0000
#define AR_PHY_CH_EXT_MINCCA_PWR_S 16

	int16_t nf;
	int i;

	for (i = 0; i < AR9300_MAX_CHAINS; i++) {
		if (ah->rxchainmask & BIT(i)) {
			nf = MS(REG_READ(ah, ah->nf_regs[i]),
					 AR_PHY_CH_MINCCA_PWR);
			nfarray[i] = sign_extend32(nf, 8);

			if (IS_CHAN_HT40(ah->curchan)) {
				u8 ext_idx = AR9300_MAX_CHAINS + i;

				nf = MS(REG_READ(ah, ah->nf_regs[ext_idx]),
						 AR_PHY_CH_EXT_MINCCA_PWR);
				nfarray[ext_idx] = sign_extend32(nf, 8);
			}
		}
	}
}

static void ar9003_hw_set_nf_limits(struct ath_hw *ah)
{
	ah->nf_2g.max = AR_PHY_CCA_MAX_GOOD_VAL_9300_2GHZ;
	ah->nf_2g.min = AR_PHY_CCA_MIN_GOOD_VAL_9300_2GHZ;
	ah->nf_2g.nominal = AR_PHY_CCA_NOM_VAL_9300_2GHZ;
	ah->nf_5g.max = AR_PHY_CCA_MAX_GOOD_VAL_9300_5GHZ;
	ah->nf_5g.min = AR_PHY_CCA_MIN_GOOD_VAL_9300_5GHZ;
	ah->nf_5g.nominal = AR_PHY_CCA_NOM_VAL_9300_5GHZ;

	if (AR_SREV_9330(ah))
		ah->nf_2g.nominal = AR_PHY_CCA_NOM_VAL_9330_2GHZ;

	if (AR_SREV_9462(ah) || AR_SREV_9565(ah)) {
		ah->nf_2g.min = AR_PHY_CCA_MIN_GOOD_VAL_9462_2GHZ;
		ah->nf_2g.nominal = AR_PHY_CCA_NOM_VAL_9462_2GHZ;
		ah->nf_5g.min = AR_PHY_CCA_MIN_GOOD_VAL_9462_5GHZ;
		ah->nf_5g.nominal = AR_PHY_CCA_NOM_VAL_9462_5GHZ;
	}
}

/*
 * Initialize the ANI register values with default (ini) values.
 * This routine is called during a (full) hardware reset after
 * all the registers are initialised from the INI.
 */
static void ar9003_hw_ani_cache_ini_regs(struct ath_hw *ah)
{
	struct ar5416AniState *aniState;
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath9k_channel *chan = ah->curchan;
	struct ath9k_ani_default *iniDef;
	u32 val;

	aniState = &ah->ani;
	iniDef = &aniState->iniDef;

	ath_dbg(common, ANI, "ver %d.%d opmode %u chan %d Mhz\n",
		ah->hw_version.macVersion,
		ah->hw_version.macRev,
		ah->opmode,
		chan->channel);

	val = REG_READ(ah, AR_PHY_SFCORR);
	iniDef->m1Thresh = MS(val, AR_PHY_SFCORR_M1_THRESH);
	iniDef->m2Thresh = MS(val, AR_PHY_SFCORR_M2_THRESH);
	iniDef->m2CountThr = MS(val, AR_PHY_SFCORR_M2COUNT_THR);

	val = REG_READ(ah, AR_PHY_SFCORR_LOW);
	iniDef->m1ThreshLow = MS(val, AR_PHY_SFCORR_LOW_M1_THRESH_LOW);
	iniDef->m2ThreshLow = MS(val, AR_PHY_SFCORR_LOW_M2_THRESH_LOW);
	iniDef->m2CountThrLow = MS(val, AR_PHY_SFCORR_LOW_M2COUNT_THR_LOW);

	val = REG_READ(ah, AR_PHY_SFCORR_EXT);
	iniDef->m1ThreshExt = MS(val, AR_PHY_SFCORR_EXT_M1_THRESH);
	iniDef->m2ThreshExt = MS(val, AR_PHY_SFCORR_EXT_M2_THRESH);
	iniDef->m1ThreshLowExt = MS(val, AR_PHY_SFCORR_EXT_M1_THRESH_LOW);
	iniDef->m2ThreshLowExt = MS(val, AR_PHY_SFCORR_EXT_M2_THRESH_LOW);
	iniDef->firstep = REG_READ_FIELD(ah,
					 AR_PHY_FIND_SIG,
					 AR_PHY_FIND_SIG_FIRSTEP);
	iniDef->firstepLow = REG_READ_FIELD(ah,
					    AR_PHY_FIND_SIG_LOW,
					    AR_PHY_FIND_SIG_LOW_FIRSTEP_LOW);
	iniDef->cycpwrThr1 = REG_READ_FIELD(ah,
					    AR_PHY_TIMING5,
					    AR_PHY_TIMING5_CYCPWR_THR1);
	iniDef->cycpwrThr1Ext = REG_READ_FIELD(ah,
					       AR_PHY_EXT_CCA,
					       AR_PHY_EXT_CYCPWR_THR1);

	/* these levels just got reset to defaults by the INI */
	aniState->spurImmunityLevel = ATH9K_ANI_SPUR_IMMUNE_LVL;
	aniState->firstepLevel = ATH9K_ANI_FIRSTEP_LVL;
	aniState->ofdmWeakSigDetect = true;
	aniState->mrcCCK = true;
}

static void ar9003_hw_set_radar_params(struct ath_hw *ah,
				       struct ath_hw_radar_conf *conf)
{
	unsigned int regWrites = 0;
	u32 radar_0 = 0, radar_1;

	if (!conf) {
		REG_CLR_BIT(ah, AR_PHY_RADAR_0, AR_PHY_RADAR_0_ENA);
		return;
	}

	radar_0 |= AR_PHY_RADAR_0_ENA | AR_PHY_RADAR_0_FFT_ENA;
	radar_0 |= SM(conf->fir_power, AR_PHY_RADAR_0_FIRPWR);
	radar_0 |= SM(conf->radar_rssi, AR_PHY_RADAR_0_RRSSI);
	radar_0 |= SM(conf->pulse_height, AR_PHY_RADAR_0_HEIGHT);
	radar_0 |= SM(conf->pulse_rssi, AR_PHY_RADAR_0_PRSSI);
	radar_0 |= SM(conf->pulse_inband, AR_PHY_RADAR_0_INBAND);

	radar_1 = REG_READ(ah, AR_PHY_RADAR_1);
	radar_1 &= ~(AR_PHY_RADAR_1_MAXLEN | AR_PHY_RADAR_1_RELSTEP_THRESH |
		     AR_PHY_RADAR_1_RELPWR_THRESH);
	radar_1 |= AR_PHY_RADAR_1_MAX_RRSSI;
	radar_1 |= AR_PHY_RADAR_1_BLOCK_CHECK;
	radar_1 |= SM(conf->pulse_maxlen, AR_PHY_RADAR_1_MAXLEN);
	radar_1 |= SM(conf->pulse_inband_step, AR_PHY_RADAR_1_RELSTEP_THRESH);
	radar_1 |= SM(conf->radar_inband, AR_PHY_RADAR_1_RELPWR_THRESH);

	REG_WRITE(ah, AR_PHY_RADAR_0, radar_0);
	REG_WRITE(ah, AR_PHY_RADAR_1, radar_1);
	if (conf->ext_channel)
		REG_SET_BIT(ah, AR_PHY_RADAR_EXT, AR_PHY_RADAR_EXT_ENA);
	else
		REG_CLR_BIT(ah, AR_PHY_RADAR_EXT, AR_PHY_RADAR_EXT_ENA);

	if (AR_SREV_9300(ah) || AR_SREV_9340(ah) || AR_SREV_9580(ah)) {
		REG_WRITE_ARRAY(&ah->ini_dfs,
				IS_CHAN_HT40(ah->curchan) ? 2 : 1, regWrites);
	}
}

static void ar9003_hw_set_radar_conf(struct ath_hw *ah)
{
	struct ath_hw_radar_conf *conf = &ah->radar_conf;

	conf->fir_power = -28;
	conf->radar_rssi = 0;
	conf->pulse_height = 10;
	conf->pulse_rssi = 15;
	conf->pulse_inband = 8;
	conf->pulse_maxlen = 255;
	conf->pulse_inband_step = 12;
	conf->radar_inband = 8;
}

static void ar9003_hw_antdiv_comb_conf_get(struct ath_hw *ah,
					   struct ath_hw_antcomb_conf *antconf)
{
	u32 regval;

	regval = REG_READ(ah, AR_PHY_MC_GAIN_CTRL);
	antconf->main_lna_conf = (regval & AR_PHY_ANT_DIV_MAIN_LNACONF) >>
				  AR_PHY_ANT_DIV_MAIN_LNACONF_S;
	antconf->alt_lna_conf = (regval & AR_PHY_ANT_DIV_ALT_LNACONF) >>
				 AR_PHY_ANT_DIV_ALT_LNACONF_S;
	antconf->fast_div_bias = (regval & AR_PHY_ANT_FAST_DIV_BIAS) >>
				  AR_PHY_ANT_FAST_DIV_BIAS_S;

	if (AR_SREV_9330_11(ah)) {
		antconf->lna1_lna2_switch_delta = -1;
		antconf->lna1_lna2_delta = -9;
		antconf->div_group = 1;
	} else if (AR_SREV_9485(ah)) {
		antconf->lna1_lna2_switch_delta = -1;
		antconf->lna1_lna2_delta = -9;
		antconf->div_group = 2;
	} else if (AR_SREV_9565(ah)) {
		antconf->lna1_lna2_switch_delta = 3;
		antconf->lna1_lna2_delta = -9;
		antconf->div_group = 3;
	} else {
		antconf->lna1_lna2_switch_delta = -1;
		antconf->lna1_lna2_delta = -3;
		antconf->div_group = 0;
	}
}

static void ar9003_hw_antdiv_comb_conf_set(struct ath_hw *ah,
				   struct ath_hw_antcomb_conf *antconf)
{
	u32 regval;

	regval = REG_READ(ah, AR_PHY_MC_GAIN_CTRL);
	regval &= ~(AR_PHY_ANT_DIV_MAIN_LNACONF |
		    AR_PHY_ANT_DIV_ALT_LNACONF |
		    AR_PHY_ANT_FAST_DIV_BIAS |
		    AR_PHY_ANT_DIV_MAIN_GAINTB |
		    AR_PHY_ANT_DIV_ALT_GAINTB);
	regval |= ((antconf->main_lna_conf << AR_PHY_ANT_DIV_MAIN_LNACONF_S)
		   & AR_PHY_ANT_DIV_MAIN_LNACONF);
	regval |= ((antconf->alt_lna_conf << AR_PHY_ANT_DIV_ALT_LNACONF_S)
		   & AR_PHY_ANT_DIV_ALT_LNACONF);
	regval |= ((antconf->fast_div_bias << AR_PHY_ANT_FAST_DIV_BIAS_S)
		   & AR_PHY_ANT_FAST_DIV_BIAS);
	regval |= ((antconf->main_gaintb << AR_PHY_ANT_DIV_MAIN_GAINTB_S)
		   & AR_PHY_ANT_DIV_MAIN_GAINTB);
	regval |= ((antconf->alt_gaintb << AR_PHY_ANT_DIV_ALT_GAINTB_S)
		   & AR_PHY_ANT_DIV_ALT_GAINTB);

	REG_WRITE(ah, AR_PHY_MC_GAIN_CTRL, regval);
}

#ifdef CONFIG_ATH9K_BTCOEX_SUPPORT

static void ar9003_hw_set_bt_ant_diversity(struct ath_hw *ah, bool enable)
{
	struct ath9k_hw_capabilities *pCap = &ah->caps;
	u8 ant_div_ctl1;
	u32 regval;

	if (!AR_SREV_9485(ah) && !AR_SREV_9565(ah))
		return;

	if (AR_SREV_9485(ah)) {
		regval = ar9003_hw_ant_ctrl_common_2_get(ah,
						 IS_CHAN_2GHZ(ah->curchan));
		if (enable) {
			regval &= ~AR_SWITCH_TABLE_COM2_ALL;
			regval |= ah->config.ant_ctrl_comm2g_switch_enable;
		}
		REG_RMW_FIELD(ah, AR_PHY_SWITCH_COM_2,
			      AR_SWITCH_TABLE_COM2_ALL, regval);
	}

	ant_div_ctl1 = ah->eep_ops->get_eeprom(ah, EEP_ANT_DIV_CTL1);

	/*
	 * Set MAIN/ALT LNA conf.
	 * Set MAIN/ALT gain_tb.
	 */
	regval = REG_READ(ah, AR_PHY_MC_GAIN_CTRL);
	regval &= (~AR_ANT_DIV_CTRL_ALL);
	regval |= (ant_div_ctl1 & 0x3f) << AR_ANT_DIV_CTRL_ALL_S;
	REG_WRITE(ah, AR_PHY_MC_GAIN_CTRL, regval);

	if (AR_SREV_9485_11_OR_LATER(ah)) {
		/*
		 * Enable LNA diversity.
		 */
		regval = REG_READ(ah, AR_PHY_MC_GAIN_CTRL);
		regval &= ~AR_PHY_ANT_DIV_LNADIV;
		regval |= ((ant_div_ctl1 >> 6) & 0x1) << AR_PHY_ANT_DIV_LNADIV_S;
		if (enable)
			regval |= AR_ANT_DIV_ENABLE;

		REG_WRITE(ah, AR_PHY_MC_GAIN_CTRL, regval);

		/*
		 * Enable fast antenna diversity.
		 */
		regval = REG_READ(ah, AR_PHY_CCK_DETECT);
		regval &= ~AR_FAST_DIV_ENABLE;
		regval |= ((ant_div_ctl1 >> 7) & 0x1) << AR_FAST_DIV_ENABLE_S;
		if (enable)
			regval |= AR_FAST_DIV_ENABLE;

		REG_WRITE(ah, AR_PHY_CCK_DETECT, regval);

		if (pCap->hw_caps & ATH9K_HW_CAP_ANT_DIV_COMB) {
			regval = REG_READ(ah, AR_PHY_MC_GAIN_CTRL);
			regval &= (~(AR_PHY_ANT_DIV_MAIN_LNACONF |
				     AR_PHY_ANT_DIV_ALT_LNACONF |
				     AR_PHY_ANT_DIV_ALT_GAINTB |
				     AR_PHY_ANT_DIV_MAIN_GAINTB));
			/*
			 * Set MAIN to LNA1 and ALT to LNA2 at the
			 * beginning.
			 */
			regval |= (ATH_ANT_DIV_COMB_LNA1 <<
				   AR_PHY_ANT_DIV_MAIN_LNACONF_S);
			regval |= (ATH_ANT_DIV_COMB_LNA2 <<
				   AR_PHY_ANT_DIV_ALT_LNACONF_S);
			REG_WRITE(ah, AR_PHY_MC_GAIN_CTRL, regval);
		}
	} else if (AR_SREV_9565(ah)) {
		if (enable) {
			REG_SET_BIT(ah, AR_PHY_MC_GAIN_CTRL,
				    AR_ANT_DIV_ENABLE);
			REG_SET_BIT(ah, AR_PHY_MC_GAIN_CTRL,
				    (1 << AR_PHY_ANT_SW_RX_PROT_S));
			REG_SET_BIT(ah, AR_PHY_CCK_DETECT,
				    AR_FAST_DIV_ENABLE);
			REG_SET_BIT(ah, AR_PHY_RESTART,
				    AR_PHY_RESTART_ENABLE_DIV_M2FLAG);
			REG_SET_BIT(ah, AR_BTCOEX_WL_LNADIV,
				    AR_BTCOEX_WL_LNADIV_FORCE_ON);
		} else {
			REG_CLR_BIT(ah, AR_PHY_MC_GAIN_CTRL,
				    AR_ANT_DIV_ENABLE);
			REG_CLR_BIT(ah, AR_PHY_MC_GAIN_CTRL,
				    (1 << AR_PHY_ANT_SW_RX_PROT_S));
			REG_CLR_BIT(ah, AR_PHY_CCK_DETECT,
				    AR_FAST_DIV_ENABLE);
			REG_CLR_BIT(ah, AR_PHY_RESTART,
				    AR_PHY_RESTART_ENABLE_DIV_M2FLAG);
			REG_CLR_BIT(ah, AR_BTCOEX_WL_LNADIV,
				    AR_BTCOEX_WL_LNADIV_FORCE_ON);

			regval = REG_READ(ah, AR_PHY_MC_GAIN_CTRL);
			regval &= ~(AR_PHY_ANT_DIV_MAIN_LNACONF |
				    AR_PHY_ANT_DIV_ALT_LNACONF |
				    AR_PHY_ANT_DIV_MAIN_GAINTB |
				    AR_PHY_ANT_DIV_ALT_GAINTB);
			regval |= (ATH_ANT_DIV_COMB_LNA1 <<
				   AR_PHY_ANT_DIV_MAIN_LNACONF_S);
			regval |= (ATH_ANT_DIV_COMB_LNA2 <<
				   AR_PHY_ANT_DIV_ALT_LNACONF_S);
			REG_WRITE(ah, AR_PHY_MC_GAIN_CTRL, regval);
		}
	}
}

#endif

static int ar9003_hw_fast_chan_change(struct ath_hw *ah,
				      struct ath9k_channel *chan,
				      u8 *ini_reloaded)
{
	unsigned int regWrites = 0;
	u32 modesIndex, txgain_index;

	if (IS_CHAN_5GHZ(chan))
		modesIndex = IS_CHAN_HT40(chan) ? 2 : 1;
	else
		modesIndex = IS_CHAN_HT40(chan) ? 3 : 4;

	txgain_index = AR_SREV_9531(ah) ? 1 : modesIndex;

	if (modesIndex == ah->modes_index) {
		*ini_reloaded = false;
		goto set_rfmode;
	}

	ar9003_hw_prog_ini(ah, &ah->iniSOC[ATH_INI_POST], modesIndex);
	ar9003_hw_prog_ini(ah, &ah->iniMac[ATH_INI_POST], modesIndex);
	ar9003_hw_prog_ini(ah, &ah->iniBB[ATH_INI_POST], modesIndex);
	ar9003_hw_prog_ini(ah, &ah->iniRadio[ATH_INI_POST], modesIndex);

	if (AR_SREV_9462_20_OR_LATER(ah))
		ar9003_hw_prog_ini(ah, &ah->ini_radio_post_sys2ant,
				   modesIndex);

	REG_WRITE_ARRAY(&ah->iniModesTxGain, txgain_index, regWrites);

	if (AR_SREV_9462_20_OR_LATER(ah)) {
		/*
		 * CUS217 mix LNA mode.
		 */
		if (ar9003_hw_get_rx_gain_idx(ah) == 2) {
			REG_WRITE_ARRAY(&ah->ini_modes_rxgain_bb_core,
					1, regWrites);
			REG_WRITE_ARRAY(&ah->ini_modes_rxgain_bb_postamble,
					modesIndex, regWrites);
		}
	}

	/*
	 * For 5GHz channels requiring Fast Clock, apply
	 * different modal values.
	 */
	if (IS_CHAN_A_FAST_CLOCK(ah, chan))
		REG_WRITE_ARRAY(&ah->iniModesFastClock, modesIndex, regWrites);

	if (AR_SREV_9565(ah))
		REG_WRITE_ARRAY(&ah->iniModesFastClock, 1, regWrites);

	/*
	 * JAPAN regulatory.
	 */
	if (chan->channel == 2484)
		ar9003_hw_prog_ini(ah, &ah->iniCckfirJapan2484, 1);

	ah->modes_index = modesIndex;
	*ini_reloaded = true;

set_rfmode:
	ar9003_hw_set_rfmode(ah, chan);
	return 0;
}

static void ar9003_hw_spectral_scan_config(struct ath_hw *ah,
					   struct ath_spec_scan *param)
{
	u8 count;

	if (!param->enabled) {
		REG_CLR_BIT(ah, AR_PHY_SPECTRAL_SCAN,
			    AR_PHY_SPECTRAL_SCAN_ENABLE);
		return;
	}

	REG_SET_BIT(ah, AR_PHY_RADAR_0, AR_PHY_RADAR_0_FFT_ENA);
	REG_SET_BIT(ah, AR_PHY_SPECTRAL_SCAN, AR_PHY_SPECTRAL_SCAN_ENABLE);

	/* on AR93xx and newer, count = 0 will make the the chip send
	 * spectral samples endlessly. Check if this really was intended,
	 * and fix otherwise.
	 */
	count = param->count;
	if (param->endless)
		count = 0;
	else if (param->count == 0)
		count = 1;

	if (param->short_repeat)
		REG_SET_BIT(ah, AR_PHY_SPECTRAL_SCAN,
			    AR_PHY_SPECTRAL_SCAN_SHORT_REPEAT);
	else
		REG_CLR_BIT(ah, AR_PHY_SPECTRAL_SCAN,
			    AR_PHY_SPECTRAL_SCAN_SHORT_REPEAT);

	REG_RMW_FIELD(ah, AR_PHY_SPECTRAL_SCAN,
		      AR_PHY_SPECTRAL_SCAN_COUNT, count);
	REG_RMW_FIELD(ah, AR_PHY_SPECTRAL_SCAN,
		      AR_PHY_SPECTRAL_SCAN_PERIOD, param->period);
	REG_RMW_FIELD(ah, AR_PHY_SPECTRAL_SCAN,
		      AR_PHY_SPECTRAL_SCAN_FFT_PERIOD, param->fft_period);

	return;
}

static void ar9003_hw_spectral_scan_trigger(struct ath_hw *ah)
{
	/* Activate spectral scan */
	REG_SET_BIT(ah, AR_PHY_SPECTRAL_SCAN,
		    AR_PHY_SPECTRAL_SCAN_ACTIVE);
}

static void ar9003_hw_spectral_scan_wait(struct ath_hw *ah)
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

static void ar9003_hw_tx99_start(struct ath_hw *ah, u32 qnum)
{
	REG_SET_BIT(ah, AR_PHY_TEST, PHY_AGC_CLR);
	REG_CLR_BIT(ah, AR_DIAG_SW, AR_DIAG_RX_DIS);
	REG_WRITE(ah, AR_CR, AR_CR_RXD);
	REG_WRITE(ah, AR_DLCL_IFS(qnum), 0);
	REG_WRITE(ah, AR_D_GBL_IFS_SIFS, 20); /* 50 OK */
	REG_WRITE(ah, AR_D_GBL_IFS_EIFS, 20);
	REG_WRITE(ah, AR_TIME_OUT, 0x00000400);
	REG_WRITE(ah, AR_DRETRY_LIMIT(qnum), 0xffffffff);
	REG_SET_BIT(ah, AR_QMISC(qnum), AR_Q_MISC_DCU_EARLY_TERM_REQ);
}

static void ar9003_hw_tx99_stop(struct ath_hw *ah)
{
	REG_CLR_BIT(ah, AR_PHY_TEST, PHY_AGC_CLR);
	REG_SET_BIT(ah, AR_DIAG_SW, AR_DIAG_RX_DIS);
}

static void ar9003_hw_tx99_set_txpower(struct ath_hw *ah, u8 txpower)
{
	static s16 p_pwr_array[ar9300RateSize] = { 0 };
	unsigned int i;

	if (txpower <= MAX_RATE_POWER) {
		for (i = 0; i < ar9300RateSize; i++)
			p_pwr_array[i] = txpower;
	} else {
		for (i = 0; i < ar9300RateSize; i++)
			p_pwr_array[i] = MAX_RATE_POWER;
	}

	REG_WRITE(ah, 0xa458, 0);

	REG_WRITE(ah, 0xa3c0,
		  ATH9K_POW_SM(p_pwr_array[ALL_TARGET_LEGACY_6_24], 24) |
		  ATH9K_POW_SM(p_pwr_array[ALL_TARGET_LEGACY_6_24], 16) |
		  ATH9K_POW_SM(p_pwr_array[ALL_TARGET_LEGACY_6_24],  8) |
		  ATH9K_POW_SM(p_pwr_array[ALL_TARGET_LEGACY_6_24],  0));
	REG_WRITE(ah, 0xa3c4,
		  ATH9K_POW_SM(p_pwr_array[ALL_TARGET_LEGACY_54],  24) |
		  ATH9K_POW_SM(p_pwr_array[ALL_TARGET_LEGACY_48],  16) |
		  ATH9K_POW_SM(p_pwr_array[ALL_TARGET_LEGACY_36],   8) |
		  ATH9K_POW_SM(p_pwr_array[ALL_TARGET_LEGACY_6_24], 0));
	REG_WRITE(ah, 0xa3c8,
		  ATH9K_POW_SM(p_pwr_array[ALL_TARGET_LEGACY_1L_5L], 24) |
		  ATH9K_POW_SM(p_pwr_array[ALL_TARGET_LEGACY_1L_5L], 16) |
		  ATH9K_POW_SM(p_pwr_array[ALL_TARGET_LEGACY_1L_5L],  0));
	REG_WRITE(ah, 0xa3cc,
		  ATH9K_POW_SM(p_pwr_array[ALL_TARGET_LEGACY_11S],   24) |
		  ATH9K_POW_SM(p_pwr_array[ALL_TARGET_LEGACY_11L],   16) |
		  ATH9K_POW_SM(p_pwr_array[ALL_TARGET_LEGACY_5S],     8) |
		  ATH9K_POW_SM(p_pwr_array[ALL_TARGET_LEGACY_1L_5L],  0));
	REG_WRITE(ah, 0xa3d0,
		  ATH9K_POW_SM(p_pwr_array[ALL_TARGET_HT20_5],  24) |
		  ATH9K_POW_SM(p_pwr_array[ALL_TARGET_HT20_4],  16) |
		  ATH9K_POW_SM(p_pwr_array[ALL_TARGET_HT20_1_3_9_11_17_19], 8)|
		  ATH9K_POW_SM(p_pwr_array[ALL_TARGET_HT20_0_8_16], 0));
	REG_WRITE(ah, 0xa3d4,
		  ATH9K_POW_SM(p_pwr_array[ALL_TARGET_HT20_13], 24) |
		  ATH9K_POW_SM(p_pwr_array[ALL_TARGET_HT20_12], 16) |
		  ATH9K_POW_SM(p_pwr_array[ALL_TARGET_HT20_7],   8) |
		  ATH9K_POW_SM(p_pwr_array[ALL_TARGET_HT20_6],   0));
	REG_WRITE(ah, 0xa3e4,
		  ATH9K_POW_SM(p_pwr_array[ALL_TARGET_HT20_21], 24) |
		  ATH9K_POW_SM(p_pwr_array[ALL_TARGET_HT20_20], 16) |
		  ATH9K_POW_SM(p_pwr_array[ALL_TARGET_HT20_15],  8) |
		  ATH9K_POW_SM(p_pwr_array[ALL_TARGET_HT20_14],  0));
	REG_WRITE(ah, 0xa3e8,
		  ATH9K_POW_SM(p_pwr_array[ALL_TARGET_HT40_23], 24) |
		  ATH9K_POW_SM(p_pwr_array[ALL_TARGET_HT40_22], 16) |
		  ATH9K_POW_SM(p_pwr_array[ALL_TARGET_HT20_23],  8) |
		  ATH9K_POW_SM(p_pwr_array[ALL_TARGET_HT20_22],  0));
	REG_WRITE(ah, 0xa3d8,
		  ATH9K_POW_SM(p_pwr_array[ALL_TARGET_HT40_5], 24) |
		  ATH9K_POW_SM(p_pwr_array[ALL_TARGET_HT40_4], 16) |
		  ATH9K_POW_SM(p_pwr_array[ALL_TARGET_HT40_1_3_9_11_17_19], 8) |
		  ATH9K_POW_SM(p_pwr_array[ALL_TARGET_HT40_0_8_16], 0));
	REG_WRITE(ah, 0xa3dc,
		  ATH9K_POW_SM(p_pwr_array[ALL_TARGET_HT40_13], 24) |
		  ATH9K_POW_SM(p_pwr_array[ALL_TARGET_HT40_12], 16) |
		  ATH9K_POW_SM(p_pwr_array[ALL_TARGET_HT40_7],   8) |
		  ATH9K_POW_SM(p_pwr_array[ALL_TARGET_HT40_6],   0));
	REG_WRITE(ah, 0xa3ec,
		  ATH9K_POW_SM(p_pwr_array[ALL_TARGET_HT40_21], 24) |
		  ATH9K_POW_SM(p_pwr_array[ALL_TARGET_HT40_20], 16) |
		  ATH9K_POW_SM(p_pwr_array[ALL_TARGET_HT40_15],  8) |
		  ATH9K_POW_SM(p_pwr_array[ALL_TARGET_HT40_14],  0));
}

static void ar9003_hw_init_txpower_cck(struct ath_hw *ah, u8 *rate_array)
{
	ah->tx_power[0] = rate_array[ALL_TARGET_LEGACY_1L_5L];
	ah->tx_power[1] = rate_array[ALL_TARGET_LEGACY_1L_5L];
	ah->tx_power[2] = min(rate_array[ALL_TARGET_LEGACY_1L_5L],
			      rate_array[ALL_TARGET_LEGACY_5S]);
	ah->tx_power[3] = min(rate_array[ALL_TARGET_LEGACY_11L],
			      rate_array[ALL_TARGET_LEGACY_11S]);
}

static void ar9003_hw_init_txpower_ofdm(struct ath_hw *ah, u8 *rate_array,
					int offset)
{
	int i, j;

	for (i = offset; i < offset + AR9300_OFDM_RATES; i++) {
		/* OFDM rate to power table idx */
		j = ofdm2pwr[i - offset];
		ah->tx_power[i] = rate_array[j];
	}
}

static void ar9003_hw_init_txpower_ht(struct ath_hw *ah, u8 *rate_array,
				      int ss_offset, int ds_offset,
				      int ts_offset, bool is_40)
{
	int i, j, mcs_idx = 0;
	const u8 *mcs2pwr = (is_40) ? mcs2pwr_ht40 : mcs2pwr_ht20;

	for (i = ss_offset; i < ss_offset + AR9300_HT_SS_RATES; i++) {
		j = mcs2pwr[mcs_idx];
		ah->tx_power[i] = rate_array[j];
		mcs_idx++;
	}

	for (i = ds_offset; i < ds_offset + AR9300_HT_DS_RATES; i++) {
		j = mcs2pwr[mcs_idx];
		ah->tx_power[i] = rate_array[j];
		mcs_idx++;
	}

	for (i = ts_offset; i < ts_offset + AR9300_HT_TS_RATES; i++) {
		j = mcs2pwr[mcs_idx];
		ah->tx_power[i] = rate_array[j];
		mcs_idx++;
	}
}

static void ar9003_hw_init_txpower_stbc(struct ath_hw *ah, int ss_offset,
					int ds_offset, int ts_offset)
{
	memcpy(&ah->tx_power_stbc[ss_offset], &ah->tx_power[ss_offset],
	       AR9300_HT_SS_RATES);
	memcpy(&ah->tx_power_stbc[ds_offset], &ah->tx_power[ds_offset],
	       AR9300_HT_DS_RATES);
	memcpy(&ah->tx_power_stbc[ts_offset], &ah->tx_power[ts_offset],
	       AR9300_HT_TS_RATES);
}

void ar9003_hw_init_rate_txpower(struct ath_hw *ah, u8 *rate_array,
				 struct ath9k_channel *chan)
{
	if (IS_CHAN_5GHZ(chan)) {
		ar9003_hw_init_txpower_ofdm(ah, rate_array,
					    AR9300_11NA_OFDM_SHIFT);
		if (IS_CHAN_HT20(chan) || IS_CHAN_HT40(chan)) {
			ar9003_hw_init_txpower_ht(ah, rate_array,
						  AR9300_11NA_HT_SS_SHIFT,
						  AR9300_11NA_HT_DS_SHIFT,
						  AR9300_11NA_HT_TS_SHIFT,
						  IS_CHAN_HT40(chan));
			ar9003_hw_init_txpower_stbc(ah,
						    AR9300_11NA_HT_SS_SHIFT,
						    AR9300_11NA_HT_DS_SHIFT,
						    AR9300_11NA_HT_TS_SHIFT);
		}
	} else {
		ar9003_hw_init_txpower_cck(ah, rate_array);
		ar9003_hw_init_txpower_ofdm(ah, rate_array,
					    AR9300_11NG_OFDM_SHIFT);
		if (IS_CHAN_HT20(chan) || IS_CHAN_HT40(chan)) {
			ar9003_hw_init_txpower_ht(ah, rate_array,
						  AR9300_11NG_HT_SS_SHIFT,
						  AR9300_11NG_HT_DS_SHIFT,
						  AR9300_11NG_HT_TS_SHIFT,
						  IS_CHAN_HT40(chan));
			ar9003_hw_init_txpower_stbc(ah,
						    AR9300_11NG_HT_SS_SHIFT,
						    AR9300_11NG_HT_DS_SHIFT,
						    AR9300_11NG_HT_TS_SHIFT);
		}
	}
}

void ar9003_hw_attach_phy_ops(struct ath_hw *ah)
{
	struct ath_hw_private_ops *priv_ops = ath9k_hw_private_ops(ah);
	struct ath_hw_ops *ops = ath9k_hw_ops(ah);
	static const u32 ar9300_cca_regs[6] = {
		AR_PHY_CCA_0,
		AR_PHY_CCA_1,
		AR_PHY_CCA_2,
		AR_PHY_EXT_CCA,
		AR_PHY_EXT_CCA_1,
		AR_PHY_EXT_CCA_2,
	};

	priv_ops->rf_set_freq = ar9003_hw_set_channel;
	priv_ops->spur_mitigate_freq = ar9003_hw_spur_mitigate;

	if (AR_SREV_9340(ah) || AR_SREV_9550(ah) || AR_SREV_9531(ah) ||
	    AR_SREV_9561(ah))
		priv_ops->compute_pll_control = ar9003_hw_compute_pll_control_soc;
	else
		priv_ops->compute_pll_control = ar9003_hw_compute_pll_control;

	priv_ops->set_channel_regs = ar9003_hw_set_channel_regs;
	priv_ops->init_bb = ar9003_hw_init_bb;
	priv_ops->process_ini = ar9003_hw_process_ini;
	priv_ops->set_rfmode = ar9003_hw_set_rfmode;
	priv_ops->mark_phy_inactive = ar9003_hw_mark_phy_inactive;
	priv_ops->set_delta_slope = ar9003_hw_set_delta_slope;
	priv_ops->rfbus_req = ar9003_hw_rfbus_req;
	priv_ops->rfbus_done = ar9003_hw_rfbus_done;
	priv_ops->ani_control = ar9003_hw_ani_control;
	priv_ops->do_getnf = ar9003_hw_do_getnf;
	priv_ops->ani_cache_ini_regs = ar9003_hw_ani_cache_ini_regs;
	priv_ops->set_radar_params = ar9003_hw_set_radar_params;
	priv_ops->fast_chan_change = ar9003_hw_fast_chan_change;

	ops->antdiv_comb_conf_get = ar9003_hw_antdiv_comb_conf_get;
	ops->antdiv_comb_conf_set = ar9003_hw_antdiv_comb_conf_set;
	ops->spectral_scan_config = ar9003_hw_spectral_scan_config;
	ops->spectral_scan_trigger = ar9003_hw_spectral_scan_trigger;
	ops->spectral_scan_wait = ar9003_hw_spectral_scan_wait;

#ifdef CONFIG_ATH9K_BTCOEX_SUPPORT
	ops->set_bt_ant_diversity = ar9003_hw_set_bt_ant_diversity;
#endif
	ops->tx99_start = ar9003_hw_tx99_start;
	ops->tx99_stop = ar9003_hw_tx99_stop;
	ops->tx99_set_txpower = ar9003_hw_tx99_set_txpower;

	ar9003_hw_set_nf_limits(ah);
	ar9003_hw_set_radar_conf(ah);
	memcpy(ah->nf_regs, ar9300_cca_regs, sizeof(ah->nf_regs));
}

/*
 * Baseband Watchdog signatures:
 *
 * 0x04000539: BB hang when operating in HT40 DFS Channel.
 *             Full chip reset is not required, but a recovery
 *             mechanism is needed.
 *
 * 0x1300000a: Related to CAC deafness.
 *             Chip reset is not required.
 *
 * 0x0400000a: Related to CAC deafness.
 *             Full chip reset is required.
 *
 * 0x04000b09: RX state machine gets into an illegal state
 *             when a packet with unsupported rate is received.
 *             Full chip reset is required and PHY_RESTART has
 *             to be disabled.
 *
 * 0x04000409: Packet stuck on receive.
 *             Full chip reset is required for all chips except AR9340.
 */

/*
 * ar9003_hw_bb_watchdog_check(): Returns true if a chip reset is required.
 */
bool ar9003_hw_bb_watchdog_check(struct ath_hw *ah)
{
	u32 val;

	switch(ah->bb_watchdog_last_status) {
	case 0x04000539:
		val = REG_READ(ah, AR_PHY_RADAR_0);
		val &= (~AR_PHY_RADAR_0_FIRPWR);
		val |= SM(0x7f, AR_PHY_RADAR_0_FIRPWR);
		REG_WRITE(ah, AR_PHY_RADAR_0, val);
		udelay(1);
		val = REG_READ(ah, AR_PHY_RADAR_0);
		val &= ~AR_PHY_RADAR_0_FIRPWR;
		val |= SM(AR9300_DFS_FIRPWR, AR_PHY_RADAR_0_FIRPWR);
		REG_WRITE(ah, AR_PHY_RADAR_0, val);

		return false;
	case 0x1300000a:
		return false;
	case 0x0400000a:
	case 0x04000b09:
		return true;
	case 0x04000409:
		if (AR_SREV_9340(ah) || AR_SREV_9531(ah))
			return false;
		else
			return true;
	default:
		/*
		 * For any other unknown signatures, do a
		 * full chip reset.
		 */
		return true;
	}
}
EXPORT_SYMBOL(ar9003_hw_bb_watchdog_check);

void ar9003_hw_bb_watchdog_config(struct ath_hw *ah)
{
	struct ath_common *common = ath9k_hw_common(ah);
	u32 idle_tmo_ms = ah->bb_watchdog_timeout_ms;
	u32 val, idle_count;

	if (!idle_tmo_ms) {
		/* disable IRQ, disable chip-reset for BB panic */
		REG_WRITE(ah, AR_PHY_WATCHDOG_CTL_2,
			  REG_READ(ah, AR_PHY_WATCHDOG_CTL_2) &
			  ~(AR_PHY_WATCHDOG_RST_ENABLE |
			    AR_PHY_WATCHDOG_IRQ_ENABLE));

		/* disable watchdog in non-IDLE mode, disable in IDLE mode */
		REG_WRITE(ah, AR_PHY_WATCHDOG_CTL_1,
			  REG_READ(ah, AR_PHY_WATCHDOG_CTL_1) &
			  ~(AR_PHY_WATCHDOG_NON_IDLE_ENABLE |
			    AR_PHY_WATCHDOG_IDLE_ENABLE));

		ath_dbg(common, RESET, "Disabled BB Watchdog\n");
		return;
	}

	/* enable IRQ, disable chip-reset for BB watchdog */
	val = REG_READ(ah, AR_PHY_WATCHDOG_CTL_2) & AR_PHY_WATCHDOG_CNTL2_MASK;
	REG_WRITE(ah, AR_PHY_WATCHDOG_CTL_2,
		  (val | AR_PHY_WATCHDOG_IRQ_ENABLE) &
		  ~AR_PHY_WATCHDOG_RST_ENABLE);

	/* bound limit to 10 secs */
	if (idle_tmo_ms > 10000)
		idle_tmo_ms = 10000;

	/*
	 * The time unit for watchdog event is 2^15 44/88MHz cycles.
	 *
	 * For HT20 we have a time unit of 2^15/44 MHz = .74 ms per tick
	 * For HT40 we have a time unit of 2^15/88 MHz = .37 ms per tick
	 *
	 * Given we use fast clock now in 5 GHz, these time units should
	 * be common for both 2 GHz and 5 GHz.
	 */
	idle_count = (100 * idle_tmo_ms) / 74;
	if (ah->curchan && IS_CHAN_HT40(ah->curchan))
		idle_count = (100 * idle_tmo_ms) / 37;

	/*
	 * enable watchdog in non-IDLE mode, disable in IDLE mode,
	 * set idle time-out.
	 */
	REG_WRITE(ah, AR_PHY_WATCHDOG_CTL_1,
		  AR_PHY_WATCHDOG_NON_IDLE_ENABLE |
		  AR_PHY_WATCHDOG_IDLE_MASK |
		  (AR_PHY_WATCHDOG_NON_IDLE_MASK & (idle_count << 2)));

	ath_dbg(common, RESET, "Enabled BB Watchdog timeout (%u ms)\n",
		idle_tmo_ms);
}

void ar9003_hw_bb_watchdog_read(struct ath_hw *ah)
{
	/*
	 * we want to avoid printing in ISR context so we save the
	 * watchdog status to be printed later in bottom half context.
	 */
	ah->bb_watchdog_last_status = REG_READ(ah, AR_PHY_WATCHDOG_STATUS);

	/*
	 * the watchdog timer should reset on status read but to be sure
	 * sure we write 0 to the watchdog status bit.
	 */
	REG_WRITE(ah, AR_PHY_WATCHDOG_STATUS,
		  ah->bb_watchdog_last_status & ~AR_PHY_WATCHDOG_STATUS_CLR);
}

void ar9003_hw_bb_watchdog_dbg_info(struct ath_hw *ah)
{
	struct ath_common *common = ath9k_hw_common(ah);
	u32 status;

	if (likely(!(common->debug_mask & ATH_DBG_RESET)))
		return;

	status = ah->bb_watchdog_last_status;
	ath_dbg(common, RESET,
		"\n==== BB update: BB status=0x%08x ====\n", status);
	ath_dbg(common, RESET,
		"** BB state: wd=%u det=%u rdar=%u rOFDM=%d rCCK=%u tOFDM=%u tCCK=%u agc=%u src=%u **\n",
		MS(status, AR_PHY_WATCHDOG_INFO),
		MS(status, AR_PHY_WATCHDOG_DET_HANG),
		MS(status, AR_PHY_WATCHDOG_RADAR_SM),
		MS(status, AR_PHY_WATCHDOG_RX_OFDM_SM),
		MS(status, AR_PHY_WATCHDOG_RX_CCK_SM),
		MS(status, AR_PHY_WATCHDOG_TX_OFDM_SM),
		MS(status, AR_PHY_WATCHDOG_TX_CCK_SM),
		MS(status, AR_PHY_WATCHDOG_AGC_SM),
		MS(status, AR_PHY_WATCHDOG_SRCH_SM));

	ath_dbg(common, RESET, "** BB WD cntl: cntl1=0x%08x cntl2=0x%08x **\n",
		REG_READ(ah, AR_PHY_WATCHDOG_CTL_1),
		REG_READ(ah, AR_PHY_WATCHDOG_CTL_2));
	ath_dbg(common, RESET, "** BB mode: BB_gen_controls=0x%08x **\n",
		REG_READ(ah, AR_PHY_GEN_CTRL));

#define PCT(_field) (common->cc_survey._field * 100 / common->cc_survey.cycles)
	if (common->cc_survey.cycles)
		ath_dbg(common, RESET,
			"** BB busy times: rx_clear=%d%%, rx_frame=%d%%, tx_frame=%d%% **\n",
			PCT(rx_busy), PCT(rx_frame), PCT(tx_frame));

	ath_dbg(common, RESET, "==== BB update: done ====\n\n");
}
EXPORT_SYMBOL(ar9003_hw_bb_watchdog_dbg_info);

void ar9003_hw_disable_phy_restart(struct ath_hw *ah)
{
	u8 result;
	u32 val;

	/* While receiving unsupported rate frame rx state machine
	 * gets into a state 0xb and if phy_restart happens in that
	 * state, BB would go hang. If RXSM is in 0xb state after
	 * first bb panic, ensure to disable the phy_restart.
	 */
	result = MS(ah->bb_watchdog_last_status, AR_PHY_WATCHDOG_RX_OFDM_SM);

	if ((result == 0xb) || ah->bb_hang_rx_ofdm) {
		ah->bb_hang_rx_ofdm = true;
		val = REG_READ(ah, AR_PHY_RESTART);
		val &= ~AR_PHY_RESTART_ENA;
		REG_WRITE(ah, AR_PHY_RESTART, val);
	}
}
EXPORT_SYMBOL(ar9003_hw_disable_phy_restart);
