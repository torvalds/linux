/*
 * Copyright (c) 2010 Atheros Communications Inc.
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

#include "hw.h"
#include "ar9003_phy.h"

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

/**
 * ar9003_hw_set_channel - set channel on single-chip device
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
 *
 * For 5GHz channels which are 5MHz spaced,
 * Channel Frequency = (3/2) * freq_ref * (chansel[8:0] + chanfrac[16:0]/2^17)
 * (freq_ref = 40MHz)
 */
static int ar9003_hw_set_channel(struct ath_hw *ah, struct ath9k_channel *chan)
{
	u16 bMode, fracMode = 0, aModeRefSel = 0;
	u32 freq, channelSel = 0, reg32 = 0;
	struct chan_centers centers;
	int loadSynthChannel;

	ath9k_hw_get_channel_centers(ah, chan, &centers);
	freq = centers.synth_center;

	if (freq < 4800) {     /* 2 GHz, fractional mode */
		channelSel = CHANSEL_2G(freq);
		/* Set to 2G mode */
		bMode = 1;
	} else {
		channelSel = CHANSEL_5G(freq);
		/* Doubler is ON, so, divide channelSel by 2. */
		channelSel >>= 1;
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
	ah->curchan_rad_index = -1;

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
	u32 spur_freq[4] = { 2420, 2440, 2464, 2480 };
	int cur_bb_spur, negative = 0, cck_spur_freq;
	int i;

	/*
	 * Need to verify range +/- 10 MHz in control channel, otherwise spur
	 * is out-of-band and can be ignored.
	 */

	for (i = 0; i < 4; i++) {
		negative = 0;
		cur_bb_spur = spur_freq[i] - chan->channel;

		if (cur_bb_spur < 0) {
			negative = 1;
			cur_bb_spur = -cur_bb_spur;
		}
		if (cur_bb_spur < 10) {
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
				int spur_subchannel_sd)
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
	REG_RMW_FIELD(ah, AR_PHY_TIMING11,
		      AR_PHY_TIMING11_USE_SPUR_FILTER_IN_SELFCOR, 0x1);
	REG_RMW_FIELD(ah, AR_PHY_TIMING4,
		      AR_PHY_TIMING4_ENABLE_SPUR_RSSI, 0x1);
	REG_RMW_FIELD(ah, AR_PHY_SPUR_REG,
		      AR_PHY_SPUR_REG_SPUR_RSSI_THRESH, 34);
	REG_RMW_FIELD(ah, AR_PHY_SPUR_REG,
		      AR_PHY_SPUR_REG_EN_VIT_SPUR_RSSI, 1);

	if (REG_READ_FIELD(ah, AR_PHY_MODE,
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

static void ar9003_hw_spur_ofdm_work(struct ath_hw *ah,
				     struct ath9k_channel *chan,
				     int freq_offset)
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
			    spur_subchannel_sd);
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

	for (i = 0; spurChansPtr[i] && i < 5; i++) {
		freq_offset = FBIN2FREQ(spurChansPtr[i], mode) - synth_freq;
		if (abs(freq_offset) < range) {
			ar9003_hw_spur_ofdm_work(ah, chan, freq_offset);
			break;
		}
	}
}

static void ar9003_hw_spur_mitigate(struct ath_hw *ah,
				    struct ath9k_channel *chan)
{
	ar9003_hw_spur_mitigate_mrc_cck(ah, chan);
	ar9003_hw_spur_mitigate_ofdm(ah, chan);
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
	phymode = AR_PHY_GC_HT_EN | AR_PHY_GC_SINGLE_HT_LTF1 | AR_PHY_GC_WALSH |
		  AR_PHY_GC_SHORT_GI_40 | enableDacFifo;

	/* Configure baseband for dynamic 20/40 operation */
	if (IS_CHAN_HT40(chan)) {
		phymode |= AR_PHY_GC_DYN2040_EN;
		/* Configure control (primary) channel at +-10MHz */
		if ((chan->chanmode == CHANNEL_A_HT40PLUS) ||
		    (chan->chanmode == CHANNEL_G_HT40PLUS))
			phymode |= AR_PHY_GC_DYN2040_PRI_CH;

	}

	/* make sure we preserve INI settings */
	phymode |= REG_READ(ah, AR_PHY_GEN_CTRL);
	/* turn off Green Field detection for STA for now */
	phymode &= ~AR_PHY_GC_GF_DETECT_EN;

	REG_WRITE(ah, AR_PHY_GEN_CTRL, phymode);

	/* Configure MAC for 20/40 operation */
	ath9k_hw_set11nmac2040(ah);

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
	if (IS_CHAN_B(chan))
		synthDelay = (4 * synthDelay) / 22;
	else
		synthDelay /= 10;

	/* Activate the PHY (includes baseband activate + synthesizer on) */
	REG_WRITE(ah, AR_PHY_ACTIVE, AR_PHY_ACTIVE_EN);

	/*
	 * There is an issue if the AP starts the calibration before
	 * the base band timeout completes.  This could result in the
	 * rx_clear false triggering.  As a workaround we add delay an
	 * extra BASE_ACTIVATE_DELAY usecs to ensure this condition
	 * does not happen.
	 */
	udelay(synthDelay + BASE_ACTIVATE_DELAY);
}

void ar9003_hw_set_chain_masks(struct ath_hw *ah, u8 rx, u8 tx)
{
	switch (rx) {
	case 0x5:
		REG_SET_BIT(ah, AR_PHY_ANALOG_SWAP,
			    AR_PHY_SWAP_ALT_CHAIN);
	case 0x3:
	case 0x1:
	case 0x2:
	case 0x7:
		REG_WRITE(ah, AR_PHY_RX_CHAINMASK, rx);
		REG_WRITE(ah, AR_PHY_CAL_CHAINMASK, rx);
		break;
	default:
		break;
	}

	REG_WRITE(ah, AR_SELFGEN_MASK, tx);
	if (tx == 0x5) {
		REG_SET_BIT(ah, AR_PHY_ANALOG_SWAP,
			    AR_PHY_SWAP_ALT_CHAIN);
	}
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
	REG_WRITE(ah, AR_PCU_MISC_MODE2,
		  val | AR_AGG_WEP_ENABLE_FIX | AR_AGG_WEP_ENABLE);
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

		if (reg >= 0x16000 && reg < 0x17000)
			ath9k_hw_analog_shift_regwrite(ah, reg, val);
		else
			REG_WRITE(ah, reg, val);

		DO_DELAY(regWrites);
	}
}

static int ar9003_hw_process_ini(struct ath_hw *ah,
				 struct ath9k_channel *chan)
{
	struct ath_regulatory *regulatory = ath9k_hw_regulatory(ah);
	unsigned int regWrites = 0, i;
	struct ieee80211_channel *channel = chan->chan;
	u32 modesIndex, freqIndex;

	switch (chan->chanmode) {
	case CHANNEL_A:
	case CHANNEL_A_HT20:
		modesIndex = 1;
		freqIndex = 1;
		break;
	case CHANNEL_A_HT40PLUS:
	case CHANNEL_A_HT40MINUS:
		modesIndex = 2;
		freqIndex = 1;
		break;
	case CHANNEL_G:
	case CHANNEL_G_HT20:
	case CHANNEL_B:
		modesIndex = 4;
		freqIndex = 2;
		break;
	case CHANNEL_G_HT40PLUS:
	case CHANNEL_G_HT40MINUS:
		modesIndex = 3;
		freqIndex = 2;
		break;

	default:
		return -EINVAL;
	}

	for (i = 0; i < ATH_INI_NUM_SPLIT; i++) {
		ar9003_hw_prog_ini(ah, &ah->iniSOC[i], modesIndex);
		ar9003_hw_prog_ini(ah, &ah->iniMac[i], modesIndex);
		ar9003_hw_prog_ini(ah, &ah->iniBB[i], modesIndex);
		ar9003_hw_prog_ini(ah, &ah->iniRadio[i], modesIndex);
	}

	REG_WRITE_ARRAY(&ah->iniModesRxGain, 1, regWrites);
	REG_WRITE_ARRAY(&ah->iniModesTxGain, modesIndex, regWrites);

	/*
	 * For 5GHz channels requiring Fast Clock, apply
	 * different modal values.
	 */
	if (IS_CHAN_A_FAST_CLOCK(ah, chan))
		REG_WRITE_ARRAY(&ah->iniModesAdditional,
				modesIndex, regWrites);

	ar9003_hw_override_ini(ah);
	ar9003_hw_set_channel_regs(ah, chan);
	ar9003_hw_set_chain_masks(ah, ah->rxchainmask, ah->txchainmask);

	/* Set TX power */
	ah->eep_ops->set_txpower(ah, chan,
				 ath9k_regd_get_ctl(regulatory, chan),
				 channel->max_antenna_gain * 2,
				 channel->max_power * 2,
				 min((u32) MAX_RATE_POWER,
				 (u32) regulatory->power_limit));

	return 0;
}

static void ar9003_hw_set_rfmode(struct ath_hw *ah,
				 struct ath9k_channel *chan)
{
	u32 rfMode = 0;

	if (chan == NULL)
		return;

	rfMode |= (IS_CHAN_B(chan) || IS_CHAN_G(chan))
		? AR_PHY_MODE_DYNAMIC : AR_PHY_MODE_OFDM;

	if (IS_CHAN_A_FAST_CLOCK(ah, chan))
		rfMode |= (AR_PHY_MODE_DYNAMIC | AR_PHY_MODE_DYN_CCK_DISABLE);

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
	if (IS_CHAN_B(ah->curchan))
		synthDelay = (4 * synthDelay) / 22;
	else
		synthDelay /= 10;

	udelay(synthDelay + BASE_ACTIVATE_DELAY);

	REG_WRITE(ah, AR_PHY_RFBUS_REQ, 0);
}

/*
 * Set the interrupt and GPIO values so the ISR can disable RF
 * on a switch signal.  Assumes GPIO port and interrupt polarity
 * are set prior to call.
 */
static void ar9003_hw_enable_rfkill(struct ath_hw *ah)
{
	/* Connect rfsilent_bb_l to baseband */
	REG_SET_BIT(ah, AR_GPIO_INPUT_EN_VAL,
		    AR_GPIO_INPUT_EN_VAL_RFSILENT_BB);
	/* Set input mux for rfsilent_bb_l to GPIO #0 */
	REG_CLR_BIT(ah, AR_GPIO_INPUT_MUX2,
		    AR_GPIO_INPUT_MUX2_RFSILENT);

	/*
	 * Configure the desired GPIO port for input and
	 * enable baseband rf silence.
	 */
	ath9k_hw_cfg_gpio_input(ah, ah->rfkill_gpio);
	REG_SET_BIT(ah, AR_PHY_TEST, RFSILENT_BB);
}

static void ar9003_hw_set_diversity(struct ath_hw *ah, bool value)
{
	u32 v = REG_READ(ah, AR_PHY_CCK_DETECT);
	if (value)
		v |= AR_PHY_CCK_DETECT_BB_ENABLE_ANT_FAST_DIV;
	else
		v &= ~AR_PHY_CCK_DETECT_BB_ENABLE_ANT_FAST_DIV;
	REG_WRITE(ah, AR_PHY_CCK_DETECT, v);
}

static bool ar9003_hw_ani_control(struct ath_hw *ah,
				  enum ath9k_ani_cmd cmd, int param)
{
	struct ar5416AniState *aniState = ah->curani;
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath9k_channel *chan = ah->curchan;
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
		/*
		 * make register setting for default
		 * (weak sig detect ON) come from INI file
		 */
		int m1ThreshLow = on ?
			aniState->iniDef.m1ThreshLow : m1ThreshLow_off;
		int m2ThreshLow = on ?
			aniState->iniDef.m2ThreshLow : m2ThreshLow_off;
		int m1Thresh = on ?
			aniState->iniDef.m1Thresh : m1Thresh_off;
		int m2Thresh = on ?
			aniState->iniDef.m2Thresh : m2Thresh_off;
		int m2CountThr = on ?
			aniState->iniDef.m2CountThr : m2CountThr_off;
		int m2CountThrLow = on ?
			aniState->iniDef.m2CountThrLow : m2CountThrLow_off;
		int m1ThreshLowExt = on ?
			aniState->iniDef.m1ThreshLowExt : m1ThreshLowExt_off;
		int m2ThreshLowExt = on ?
			aniState->iniDef.m2ThreshLowExt : m2ThreshLowExt_off;
		int m1ThreshExt = on ?
			aniState->iniDef.m1ThreshExt : m1ThreshExt_off;
		int m2ThreshExt = on ?
			aniState->iniDef.m2ThreshExt : m2ThreshExt_off;

		REG_RMW_FIELD(ah, AR_PHY_SFCORR_LOW,
			      AR_PHY_SFCORR_LOW_M1_THRESH_LOW,
			      m1ThreshLow);
		REG_RMW_FIELD(ah, AR_PHY_SFCORR_LOW,
			      AR_PHY_SFCORR_LOW_M2_THRESH_LOW,
			      m2ThreshLow);
		REG_RMW_FIELD(ah, AR_PHY_SFCORR,
			      AR_PHY_SFCORR_M1_THRESH, m1Thresh);
		REG_RMW_FIELD(ah, AR_PHY_SFCORR,
			      AR_PHY_SFCORR_M2_THRESH, m2Thresh);
		REG_RMW_FIELD(ah, AR_PHY_SFCORR,
			      AR_PHY_SFCORR_M2COUNT_THR, m2CountThr);
		REG_RMW_FIELD(ah, AR_PHY_SFCORR_LOW,
			      AR_PHY_SFCORR_LOW_M2COUNT_THR_LOW,
			      m2CountThrLow);

		REG_RMW_FIELD(ah, AR_PHY_SFCORR_EXT,
			      AR_PHY_SFCORR_EXT_M1_THRESH_LOW, m1ThreshLowExt);
		REG_RMW_FIELD(ah, AR_PHY_SFCORR_EXT,
			      AR_PHY_SFCORR_EXT_M2_THRESH_LOW, m2ThreshLowExt);
		REG_RMW_FIELD(ah, AR_PHY_SFCORR_EXT,
			      AR_PHY_SFCORR_EXT_M1_THRESH, m1ThreshExt);
		REG_RMW_FIELD(ah, AR_PHY_SFCORR_EXT,
			      AR_PHY_SFCORR_EXT_M2_THRESH, m2ThreshExt);

		if (on)
			REG_SET_BIT(ah, AR_PHY_SFCORR_LOW,
				    AR_PHY_SFCORR_LOW_USE_SELF_CORR_LOW);
		else
			REG_CLR_BIT(ah, AR_PHY_SFCORR_LOW,
				    AR_PHY_SFCORR_LOW_USE_SELF_CORR_LOW);

		if (!on != aniState->ofdmWeakSigDetectOff) {
			ath_print(common, ATH_DBG_ANI,
				  "** ch %d: ofdm weak signal: %s=>%s\n",
				  chan->channel,
				  !aniState->ofdmWeakSigDetectOff ?
					"on" : "off",
				  on ? "on" : "off");
			if (on)
				ah->stats.ast_ani_ofdmon++;
			else
				ah->stats.ast_ani_ofdmoff++;
			aniState->ofdmWeakSigDetectOff = !on;
		}
		break;
	}
	case ATH9K_ANI_FIRSTEP_LEVEL:{
		u32 level = param;

		if (level >= ARRAY_SIZE(firstep_table)) {
			ath_print(common, ATH_DBG_ANI,
				  "ATH9K_ANI_FIRSTEP_LEVEL: level "
				  "out of range (%u > %u)\n",
				  level,
				  (unsigned) ARRAY_SIZE(firstep_table));
			return false;
		}

		/*
		 * make register setting relative to default
		 * from INI file & cap value
		 */
		value = firstep_table[level] -
			firstep_table[ATH9K_ANI_FIRSTEP_LVL_NEW] +
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
			 firstep_table[ATH9K_ANI_FIRSTEP_LVL_NEW] +
			 aniState->iniDef.firstepLow;
		if (value2 < ATH9K_SIG_FIRSTEP_SETTING_MIN)
			value2 = ATH9K_SIG_FIRSTEP_SETTING_MIN;
		if (value2 > ATH9K_SIG_FIRSTEP_SETTING_MAX)
			value2 = ATH9K_SIG_FIRSTEP_SETTING_MAX;

		REG_RMW_FIELD(ah, AR_PHY_FIND_SIG_LOW,
			      AR_PHY_FIND_SIG_LOW_FIRSTEP_LOW, value2);

		if (level != aniState->firstepLevel) {
			ath_print(common, ATH_DBG_ANI,
				  "** ch %d: level %d=>%d[def:%d] "
				  "firstep[level]=%d ini=%d\n",
				  chan->channel,
				  aniState->firstepLevel,
				  level,
				  ATH9K_ANI_FIRSTEP_LVL_NEW,
				  value,
				  aniState->iniDef.firstep);
			ath_print(common, ATH_DBG_ANI,
				  "** ch %d: level %d=>%d[def:%d] "
				  "firstep_low[level]=%d ini=%d\n",
				  chan->channel,
				  aniState->firstepLevel,
				  level,
				  ATH9K_ANI_FIRSTEP_LVL_NEW,
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
			ath_print(common, ATH_DBG_ANI,
				  "ATH9K_ANI_SPUR_IMMUNITY_LEVEL: level "
				  "out of range (%u > %u)\n",
				  level,
				  (unsigned) ARRAY_SIZE(cycpwrThr1_table));
			return false;
		}
		/*
		 * make register setting relative to default
		 * from INI file & cap value
		 */
		value = cycpwrThr1_table[level] -
			cycpwrThr1_table[ATH9K_ANI_SPUR_IMMUNE_LVL_NEW] +
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
			 cycpwrThr1_table[ATH9K_ANI_SPUR_IMMUNE_LVL_NEW] +
			 aniState->iniDef.cycpwrThr1Ext;
		if (value2 < ATH9K_SIG_SPUR_IMM_SETTING_MIN)
			value2 = ATH9K_SIG_SPUR_IMM_SETTING_MIN;
		if (value2 > ATH9K_SIG_SPUR_IMM_SETTING_MAX)
			value2 = ATH9K_SIG_SPUR_IMM_SETTING_MAX;
		REG_RMW_FIELD(ah, AR_PHY_EXT_CCA,
			      AR_PHY_EXT_CYCPWR_THR1, value2);

		if (level != aniState->spurImmunityLevel) {
			ath_print(common, ATH_DBG_ANI,
				  "** ch %d: level %d=>%d[def:%d] "
				  "cycpwrThr1[level]=%d ini=%d\n",
				  chan->channel,
				  aniState->spurImmunityLevel,
				  level,
				  ATH9K_ANI_SPUR_IMMUNE_LVL_NEW,
				  value,
				  aniState->iniDef.cycpwrThr1);
			ath_print(common, ATH_DBG_ANI,
				  "** ch %d: level %d=>%d[def:%d] "
				  "cycpwrThr1Ext[level]=%d ini=%d\n",
				  chan->channel,
				  aniState->spurImmunityLevel,
				  level,
				  ATH9K_ANI_SPUR_IMMUNE_LVL_NEW,
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
		REG_RMW_FIELD(ah, AR_PHY_MRC_CCK_CTRL,
			      AR_PHY_MRC_CCK_ENABLE, is_on);
		REG_RMW_FIELD(ah, AR_PHY_MRC_CCK_CTRL,
			      AR_PHY_MRC_CCK_MUX_REG, is_on);
		if (!is_on != aniState->mrcCCKOff) {
			ath_print(common, ATH_DBG_ANI,
				  "** ch %d: MRC CCK: %s=>%s\n",
				  chan->channel,
				  !aniState->mrcCCKOff ? "on" : "off",
				  is_on ? "on" : "off");
		if (is_on)
			ah->stats.ast_ani_ccklow++;
		else
			ah->stats.ast_ani_cckhigh++;
		aniState->mrcCCKOff = !is_on;
		}
	break;
	}
	case ATH9K_ANI_PRESENT:
		break;
	default:
		ath_print(common, ATH_DBG_ANI,
			  "invalid cmd %u\n", cmd);
		return false;
	}

	ath_print(common, ATH_DBG_ANI,
		  "ANI parameters: SI=%d, ofdmWS=%s FS=%d "
		  "MRCcck=%s listenTime=%d CC=%d listen=%d "
		  "ofdmErrs=%d cckErrs=%d\n",
		  aniState->spurImmunityLevel,
		  !aniState->ofdmWeakSigDetectOff ? "on" : "off",
		  aniState->firstepLevel,
		  !aniState->mrcCCKOff ? "on" : "off",
		  aniState->listenTime,
		  aniState->cycleCount,
		  aniState->listenTime,
		  aniState->ofdmPhyErrCount,
		  aniState->cckPhyErrCount);
	return true;
}

static void ar9003_hw_do_getnf(struct ath_hw *ah,
			      int16_t nfarray[NUM_NF_READINGS])
{
	int16_t nf;

	nf = MS(REG_READ(ah, AR_PHY_CCA_0), AR_PHY_MINCCA_PWR);
	nfarray[0] = sign_extend(nf, 9);

	nf = MS(REG_READ(ah, AR_PHY_CCA_1), AR_PHY_CH1_MINCCA_PWR);
	nfarray[1] = sign_extend(nf, 9);

	nf = MS(REG_READ(ah, AR_PHY_CCA_2), AR_PHY_CH2_MINCCA_PWR);
	nfarray[2] = sign_extend(nf, 9);

	if (!IS_CHAN_HT40(ah->curchan))
		return;

	nf = MS(REG_READ(ah, AR_PHY_EXT_CCA), AR_PHY_EXT_MINCCA_PWR);
	nfarray[3] = sign_extend(nf, 9);

	nf = MS(REG_READ(ah, AR_PHY_EXT_CCA_1), AR_PHY_CH1_EXT_MINCCA_PWR);
	nfarray[4] = sign_extend(nf, 9);

	nf = MS(REG_READ(ah, AR_PHY_EXT_CCA_2), AR_PHY_CH2_EXT_MINCCA_PWR);
	nfarray[5] = sign_extend(nf, 9);
}

static void ar9003_hw_set_nf_limits(struct ath_hw *ah)
{
	ah->nf_2g.max = AR_PHY_CCA_MAX_GOOD_VAL_9300_2GHZ;
	ah->nf_2g.min = AR_PHY_CCA_MIN_GOOD_VAL_9300_2GHZ;
	ah->nf_2g.nominal = AR_PHY_CCA_NOM_VAL_9300_2GHZ;
	ah->nf_5g.max = AR_PHY_CCA_MAX_GOOD_VAL_9300_5GHZ;
	ah->nf_5g.min = AR_PHY_CCA_MIN_GOOD_VAL_9300_5GHZ;
	ah->nf_5g.nominal = AR_PHY_CCA_NOM_VAL_9300_5GHZ;
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
	int index;
	u32 val;

	index = ath9k_hw_get_ani_channel_idx(ah, chan);
	aniState = &ah->ani[index];
	ah->curani = aniState;
	iniDef = &aniState->iniDef;

	ath_print(common, ATH_DBG_ANI,
		  "ver %d.%d opmode %u chan %d Mhz/0x%x\n",
		  ah->hw_version.macVersion,
		  ah->hw_version.macRev,
		  ah->opmode,
		  chan->channel,
		  chan->channelFlags);

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
	aniState->spurImmunityLevel = ATH9K_ANI_SPUR_IMMUNE_LVL_NEW;
	aniState->firstepLevel = ATH9K_ANI_FIRSTEP_LVL_NEW;
	aniState->ofdmWeakSigDetectOff = !ATH9K_ANI_USE_OFDM_WEAK_SIG;
	aniState->mrcCCKOff = !ATH9K_ANI_ENABLE_MRC_CCK;

	aniState->cycleCount = 0;
}

void ar9003_hw_attach_phy_ops(struct ath_hw *ah)
{
	struct ath_hw_private_ops *priv_ops = ath9k_hw_private_ops(ah);
	const u32 ar9300_cca_regs[6] = {
		AR_PHY_CCA_0,
		AR_PHY_CCA_1,
		AR_PHY_CCA_2,
		AR_PHY_EXT_CCA,
		AR_PHY_EXT_CCA_1,
		AR_PHY_EXT_CCA_2,
	};

	priv_ops->rf_set_freq = ar9003_hw_set_channel;
	priv_ops->spur_mitigate_freq = ar9003_hw_spur_mitigate;
	priv_ops->compute_pll_control = ar9003_hw_compute_pll_control;
	priv_ops->set_channel_regs = ar9003_hw_set_channel_regs;
	priv_ops->init_bb = ar9003_hw_init_bb;
	priv_ops->process_ini = ar9003_hw_process_ini;
	priv_ops->set_rfmode = ar9003_hw_set_rfmode;
	priv_ops->mark_phy_inactive = ar9003_hw_mark_phy_inactive;
	priv_ops->set_delta_slope = ar9003_hw_set_delta_slope;
	priv_ops->rfbus_req = ar9003_hw_rfbus_req;
	priv_ops->rfbus_done = ar9003_hw_rfbus_done;
	priv_ops->enable_rfkill = ar9003_hw_enable_rfkill;
	priv_ops->set_diversity = ar9003_hw_set_diversity;
	priv_ops->ani_control = ar9003_hw_ani_control;
	priv_ops->do_getnf = ar9003_hw_do_getnf;
	priv_ops->ani_cache_ini_regs = ar9003_hw_ani_cache_ini_regs;

	ar9003_hw_set_nf_limits(ah);
	memcpy(ah->nf_regs, ar9300_cca_regs, sizeof(ah->nf_regs));
}

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

		ath_print(common, ATH_DBG_RESET, "Disabled BB Watchdog\n");
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

	ath_print(common, ATH_DBG_RESET,
		  "Enabled BB Watchdog timeout (%u ms)\n",
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
	u32 rxc_pcnt = 0, rxf_pcnt = 0, txf_pcnt = 0, status;

	if (likely(!(common->debug_mask & ATH_DBG_RESET)))
		return;

	status = ah->bb_watchdog_last_status;
	ath_print(common, ATH_DBG_RESET,
		  "\n==== BB update: BB status=0x%08x ====\n", status);
	ath_print(common, ATH_DBG_RESET,
		  "** BB state: wd=%u det=%u rdar=%u rOFDM=%d "
		  "rCCK=%u tOFDM=%u tCCK=%u agc=%u src=%u **\n",
		  MS(status, AR_PHY_WATCHDOG_INFO),
		  MS(status, AR_PHY_WATCHDOG_DET_HANG),
		  MS(status, AR_PHY_WATCHDOG_RADAR_SM),
		  MS(status, AR_PHY_WATCHDOG_RX_OFDM_SM),
		  MS(status, AR_PHY_WATCHDOG_RX_CCK_SM),
		  MS(status, AR_PHY_WATCHDOG_TX_OFDM_SM),
		  MS(status, AR_PHY_WATCHDOG_TX_CCK_SM),
		  MS(status, AR_PHY_WATCHDOG_AGC_SM),
		  MS(status,AR_PHY_WATCHDOG_SRCH_SM));

	ath_print(common, ATH_DBG_RESET,
		  "** BB WD cntl: cntl1=0x%08x cntl2=0x%08x **\n",
		  REG_READ(ah, AR_PHY_WATCHDOG_CTL_1),
		  REG_READ(ah, AR_PHY_WATCHDOG_CTL_2));
	ath_print(common, ATH_DBG_RESET,
		  "** BB mode: BB_gen_controls=0x%08x **\n",
		  REG_READ(ah, AR_PHY_GEN_CTRL));

	if (ath9k_hw_GetMibCycleCountsPct(ah, &rxc_pcnt, &rxf_pcnt, &txf_pcnt))
		ath_print(common, ATH_DBG_RESET,
			  "** BB busy times: rx_clear=%d%%, "
			  "rx_frame=%d%%, tx_frame=%d%% **\n",
			  rxc_pcnt, rxf_pcnt, txf_pcnt);

	ath_print(common, ATH_DBG_RESET,
		  "==== BB update: done ====\n\n");
}
EXPORT_SYMBOL(ar9003_hw_bb_watchdog_dbg_info);
