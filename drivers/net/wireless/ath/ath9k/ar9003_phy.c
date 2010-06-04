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
 * ar9003_hw_spur_mitigate - convert baseband spur frequency
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

		REG_WRITE(ah, reg, val);

		/*
		 * Determine if this is a shift register value, and insert the
		 * configured delay if so.
		 */
		if (reg >= 0x16000 && reg < 0x17000
		    && ah->config.analog_shiftreg)
			udelay(100);

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

	switch (cmd & ah->ani_function) {
	case ATH9K_ANI_NOISE_IMMUNITY_LEVEL:{
		u32 level = param;

		if (level >= ARRAY_SIZE(ah->totalSizeDesired)) {
			ath_print(common, ATH_DBG_ANI,
				  "level out of range (%u > %u)\n",
				  level,
				  (unsigned)ARRAY_SIZE(ah->totalSizeDesired));
			return false;
		}

		REG_RMW_FIELD(ah, AR_PHY_DESIRED_SZ,
			      AR_PHY_DESIRED_SZ_TOT_DES,
			      ah->totalSizeDesired[level]);
		REG_RMW_FIELD(ah, AR_PHY_AGC,
			      AR_PHY_AGC_COARSE_LOW,
			      ah->coarse_low[level]);
		REG_RMW_FIELD(ah, AR_PHY_AGC,
			      AR_PHY_AGC_COARSE_HIGH,
			      ah->coarse_high[level]);
		REG_RMW_FIELD(ah, AR_PHY_FIND_SIG,
			      AR_PHY_FIND_SIG_FIRPWR, ah->firpwr[level]);

		if (level > aniState->noiseImmunityLevel)
			ah->stats.ast_ani_niup++;
		else if (level < aniState->noiseImmunityLevel)
			ah->stats.ast_ani_nidown++;
		aniState->noiseImmunityLevel = level;
		break;
	}
	case ATH9K_ANI_OFDM_WEAK_SIGNAL_DETECTION:{
		const int m1ThreshLow[] = { 127, 50 };
		const int m2ThreshLow[] = { 127, 40 };
		const int m1Thresh[] = { 127, 0x4d };
		const int m2Thresh[] = { 127, 0x40 };
		const int m2CountThr[] = { 31, 16 };
		const int m2CountThrLow[] = { 63, 48 };
		u32 on = param ? 1 : 0;

		REG_RMW_FIELD(ah, AR_PHY_SFCORR_LOW,
			      AR_PHY_SFCORR_LOW_M1_THRESH_LOW,
			      m1ThreshLow[on]);
		REG_RMW_FIELD(ah, AR_PHY_SFCORR_LOW,
			      AR_PHY_SFCORR_LOW_M2_THRESH_LOW,
			      m2ThreshLow[on]);
		REG_RMW_FIELD(ah, AR_PHY_SFCORR,
			      AR_PHY_SFCORR_M1_THRESH, m1Thresh[on]);
		REG_RMW_FIELD(ah, AR_PHY_SFCORR,
			      AR_PHY_SFCORR_M2_THRESH, m2Thresh[on]);
		REG_RMW_FIELD(ah, AR_PHY_SFCORR,
			      AR_PHY_SFCORR_M2COUNT_THR, m2CountThr[on]);
		REG_RMW_FIELD(ah, AR_PHY_SFCORR_LOW,
			      AR_PHY_SFCORR_LOW_M2COUNT_THR_LOW,
			      m2CountThrLow[on]);

		REG_RMW_FIELD(ah, AR_PHY_SFCORR_EXT,
			      AR_PHY_SFCORR_EXT_M1_THRESH_LOW, m1ThreshLow[on]);
		REG_RMW_FIELD(ah, AR_PHY_SFCORR_EXT,
			      AR_PHY_SFCORR_EXT_M2_THRESH_LOW, m2ThreshLow[on]);
		REG_RMW_FIELD(ah, AR_PHY_SFCORR_EXT,
			      AR_PHY_SFCORR_EXT_M1_THRESH, m1Thresh[on]);
		REG_RMW_FIELD(ah, AR_PHY_SFCORR_EXT,
			      AR_PHY_SFCORR_EXT_M2_THRESH, m2Thresh[on]);

		if (on)
			REG_SET_BIT(ah, AR_PHY_SFCORR_LOW,
				    AR_PHY_SFCORR_LOW_USE_SELF_CORR_LOW);
		else
			REG_CLR_BIT(ah, AR_PHY_SFCORR_LOW,
				    AR_PHY_SFCORR_LOW_USE_SELF_CORR_LOW);

		if (!on != aniState->ofdmWeakSigDetectOff) {
			if (on)
				ah->stats.ast_ani_ofdmon++;
			else
				ah->stats.ast_ani_ofdmoff++;
			aniState->ofdmWeakSigDetectOff = !on;
		}
		break;
	}
	case ATH9K_ANI_CCK_WEAK_SIGNAL_THR:{
		const int weakSigThrCck[] = { 8, 6 };
		u32 high = param ? 1 : 0;

		REG_RMW_FIELD(ah, AR_PHY_CCK_DETECT,
			      AR_PHY_CCK_DETECT_WEAK_SIG_THR_CCK,
			      weakSigThrCck[high]);
		if (high != aniState->cckWeakSigThreshold) {
			if (high)
				ah->stats.ast_ani_cckhigh++;
			else
				ah->stats.ast_ani_ccklow++;
			aniState->cckWeakSigThreshold = high;
		}
		break;
	}
	case ATH9K_ANI_FIRSTEP_LEVEL:{
		const int firstep[] = { 0, 4, 8 };
		u32 level = param;

		if (level >= ARRAY_SIZE(firstep)) {
			ath_print(common, ATH_DBG_ANI,
				  "level out of range (%u > %u)\n",
				  level,
				  (unsigned) ARRAY_SIZE(firstep));
			return false;
		}
		REG_RMW_FIELD(ah, AR_PHY_FIND_SIG,
			      AR_PHY_FIND_SIG_FIRSTEP,
			      firstep[level]);
		if (level > aniState->firstepLevel)
			ah->stats.ast_ani_stepup++;
		else if (level < aniState->firstepLevel)
			ah->stats.ast_ani_stepdown++;
		aniState->firstepLevel = level;
		break;
	}
	case ATH9K_ANI_SPUR_IMMUNITY_LEVEL:{
		const int cycpwrThr1[] = { 2, 4, 6, 8, 10, 12, 14, 16 };
		u32 level = param;

		if (level >= ARRAY_SIZE(cycpwrThr1)) {
			ath_print(common, ATH_DBG_ANI,
				  "level out of range (%u > %u)\n",
				  level,
				  (unsigned) ARRAY_SIZE(cycpwrThr1));
			return false;
		}
		REG_RMW_FIELD(ah, AR_PHY_TIMING5,
			      AR_PHY_TIMING5_CYCPWR_THR1,
			      cycpwrThr1[level]);
		if (level > aniState->spurImmunityLevel)
			ah->stats.ast_ani_spurup++;
		else if (level < aniState->spurImmunityLevel)
			ah->stats.ast_ani_spurdown++;
		aniState->spurImmunityLevel = level;
		break;
	}
	case ATH9K_ANI_PRESENT:
		break;
	default:
		ath_print(common, ATH_DBG_ANI,
			  "invalid cmd %u\n", cmd);
		return false;
	}

	ath_print(common, ATH_DBG_ANI, "ANI parameters:\n");
	ath_print(common, ATH_DBG_ANI,
		  "noiseImmunityLevel=%d, spurImmunityLevel=%d, "
		  "ofdmWeakSigDetectOff=%d\n",
		  aniState->noiseImmunityLevel,
		  aniState->spurImmunityLevel,
		  !aniState->ofdmWeakSigDetectOff);
	ath_print(common, ATH_DBG_ANI,
		  "cckWeakSigThreshold=%d, "
		  "firstepLevel=%d, listenTime=%d\n",
		  aniState->cckWeakSigThreshold,
		  aniState->firstepLevel,
		  aniState->listenTime);
	ath_print(common, ATH_DBG_ANI,
		"cycleCount=%d, ofdmPhyErrCount=%d, cckPhyErrCount=%d\n\n",
		aniState->cycleCount,
		aniState->ofdmPhyErrCount,
		aniState->cckPhyErrCount);

	return true;
}

static void ar9003_hw_nf_sanitize_2g(struct ath_hw *ah, s16 *nf)
{
	struct ath_common *common = ath9k_hw_common(ah);

	if (*nf > ah->nf_2g_max) {
		ath_print(common, ATH_DBG_CALIBRATE,
			  "2 GHz NF (%d) > MAX (%d), "
			  "correcting to MAX",
			  *nf, ah->nf_2g_max);
		*nf = ah->nf_2g_max;
	} else if (*nf < ah->nf_2g_min) {
		ath_print(common, ATH_DBG_CALIBRATE,
			  "2 GHz NF (%d) < MIN (%d), "
			  "correcting to MIN",
			  *nf, ah->nf_2g_min);
		*nf = ah->nf_2g_min;
	}
}

static void ar9003_hw_nf_sanitize_5g(struct ath_hw *ah, s16 *nf)
{
	struct ath_common *common = ath9k_hw_common(ah);

	if (*nf > ah->nf_5g_max) {
		ath_print(common, ATH_DBG_CALIBRATE,
			  "5 GHz NF (%d) > MAX (%d), "
			  "correcting to MAX",
			  *nf, ah->nf_5g_max);
		*nf = ah->nf_5g_max;
	} else if (*nf < ah->nf_5g_min) {
		ath_print(common, ATH_DBG_CALIBRATE,
			  "5 GHz NF (%d) < MIN (%d), "
			  "correcting to MIN",
			  *nf, ah->nf_5g_min);
		*nf = ah->nf_5g_min;
	}
}

static void ar9003_hw_nf_sanitize(struct ath_hw *ah, s16 *nf)
{
	if (IS_CHAN_2GHZ(ah->curchan))
		ar9003_hw_nf_sanitize_2g(ah, nf);
	else
		ar9003_hw_nf_sanitize_5g(ah, nf);
}

static void ar9003_hw_do_getnf(struct ath_hw *ah,
			      int16_t nfarray[NUM_NF_READINGS])
{
	struct ath_common *common = ath9k_hw_common(ah);
	int16_t nf;

	nf = MS(REG_READ(ah, AR_PHY_CCA_0), AR_PHY_MINCCA_PWR);
	if (nf & 0x100)
		nf = 0 - ((nf ^ 0x1ff) + 1);
	ar9003_hw_nf_sanitize(ah, &nf);
	ath_print(common, ATH_DBG_CALIBRATE,
		  "NF calibrated [ctl] [chain 0] is %d\n", nf);
	nfarray[0] = nf;

	nf = MS(REG_READ(ah, AR_PHY_CCA_1), AR_PHY_CH1_MINCCA_PWR);
	if (nf & 0x100)
		nf = 0 - ((nf ^ 0x1ff) + 1);
	ar9003_hw_nf_sanitize(ah, &nf);
	ath_print(common, ATH_DBG_CALIBRATE,
		  "NF calibrated [ctl] [chain 1] is %d\n", nf);
	nfarray[1] = nf;

	nf = MS(REG_READ(ah, AR_PHY_CCA_2), AR_PHY_CH2_MINCCA_PWR);
	if (nf & 0x100)
		nf = 0 - ((nf ^ 0x1ff) + 1);
	ar9003_hw_nf_sanitize(ah, &nf);
	ath_print(common, ATH_DBG_CALIBRATE,
		  "NF calibrated [ctl] [chain 2] is %d\n", nf);
	nfarray[2] = nf;

	nf = MS(REG_READ(ah, AR_PHY_EXT_CCA), AR_PHY_EXT_MINCCA_PWR);
	if (nf & 0x100)
		nf = 0 - ((nf ^ 0x1ff) + 1);
	ar9003_hw_nf_sanitize(ah, &nf);
	ath_print(common, ATH_DBG_CALIBRATE,
		  "NF calibrated [ext] [chain 0] is %d\n", nf);
	nfarray[3] = nf;

	nf = MS(REG_READ(ah, AR_PHY_EXT_CCA_1), AR_PHY_CH1_EXT_MINCCA_PWR);
	if (nf & 0x100)
		nf = 0 - ((nf ^ 0x1ff) + 1);
	ar9003_hw_nf_sanitize(ah, &nf);
	ath_print(common, ATH_DBG_CALIBRATE,
		  "NF calibrated [ext] [chain 1] is %d\n", nf);
	nfarray[4] = nf;

	nf = MS(REG_READ(ah, AR_PHY_EXT_CCA_2), AR_PHY_CH2_EXT_MINCCA_PWR);
	if (nf & 0x100)
		nf = 0 - ((nf ^ 0x1ff) + 1);
	ar9003_hw_nf_sanitize(ah, &nf);
	ath_print(common, ATH_DBG_CALIBRATE,
		  "NF calibrated [ext] [chain 2] is %d\n", nf);
	nfarray[5] = nf;
}

void ar9003_hw_set_nf_limits(struct ath_hw *ah)
{
	ah->nf_2g_max = AR_PHY_CCA_MAX_GOOD_VAL_9300_2GHZ;
	ah->nf_2g_min = AR_PHY_CCA_MIN_GOOD_VAL_9300_2GHZ;
	ah->nf_5g_max = AR_PHY_CCA_MAX_GOOD_VAL_9300_5GHZ;
	ah->nf_5g_min = AR_PHY_CCA_MIN_GOOD_VAL_9300_5GHZ;
}

/*
 * Find out which of the RX chains are enabled
 */
static u32 ar9003_hw_get_rx_chainmask(struct ath_hw *ah)
{
	u32 chain = REG_READ(ah, AR_PHY_RX_CHAINMASK);
	/*
	 * The bits [2:0] indicate the rx chain mask and are to be
	 * interpreted as follows:
	 * 00x => Only chain 0 is enabled
	 * 01x => Chain 1 and 0 enabled
	 * 1xx => Chain 2,1 and 0 enabled
	 */
	return chain & 0x7;
}

static void ar9003_hw_loadnf(struct ath_hw *ah, struct ath9k_channel *chan)
{
	struct ath9k_nfcal_hist *h;
	unsigned i, j;
	int32_t val;
	const u32 ar9300_cca_regs[6] = {
		AR_PHY_CCA_0,
		AR_PHY_CCA_1,
		AR_PHY_CCA_2,
		AR_PHY_EXT_CCA,
		AR_PHY_EXT_CCA_1,
		AR_PHY_EXT_CCA_2,
	};
	u8 chainmask, rx_chain_status;
	struct ath_common *common = ath9k_hw_common(ah);

	rx_chain_status = ar9003_hw_get_rx_chainmask(ah);

	chainmask = 0x3F;
	h = ah->nfCalHist;

	for (i = 0; i < NUM_NF_READINGS; i++) {
		if (chainmask & (1 << i)) {
			val = REG_READ(ah, ar9300_cca_regs[i]);
			val &= 0xFFFFFE00;
			val |= (((u32) (h[i].privNF) << 1) & 0x1ff);
			REG_WRITE(ah, ar9300_cca_regs[i], val);
		}
	}

	/*
	 * Load software filtered NF value into baseband internal minCCApwr
	 * variable.
	 */
	REG_CLR_BIT(ah, AR_PHY_AGC_CONTROL,
		    AR_PHY_AGC_CONTROL_ENABLE_NF);
	REG_CLR_BIT(ah, AR_PHY_AGC_CONTROL,
		    AR_PHY_AGC_CONTROL_NO_UPDATE_NF);
	REG_SET_BIT(ah, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_NF);

	/*
	 * Wait for load to complete, should be fast, a few 10s of us.
	 * The max delay was changed from an original 250us to 10000us
	 * since 250us often results in NF load timeout and causes deaf
	 * condition during stress testing 12/12/2009
	 */
	for (j = 0; j < 1000; j++) {
		if ((REG_READ(ah, AR_PHY_AGC_CONTROL) &
		     AR_PHY_AGC_CONTROL_NF) == 0)
			break;
		udelay(10);
	}

	/*
	 * We timed out waiting for the noisefloor to load, probably due to an
	 * in-progress rx. Simply return here and allow the load plenty of time
	 * to complete before the next calibration interval.  We need to avoid
	 * trying to load -50 (which happens below) while the previous load is
	 * still in progress as this can cause rx deafness. Instead by returning
	 * here, the baseband nf cal will just be capped by our present
	 * noisefloor until the next calibration timer.
	 */
	if (j == 1000) {
		ath_print(common, ATH_DBG_ANY, "Timeout while waiting for nf "
			  "to load: AR_PHY_AGC_CONTROL=0x%x\n",
			  REG_READ(ah, AR_PHY_AGC_CONTROL));
		return;
	}

	/*
	 * Restore maxCCAPower register parameter again so that we're not capped
	 * by the median we just loaded.  This will be initial (and max) value
	 * of next noise floor calibration the baseband does.
	 */
	for (i = 0; i < NUM_NF_READINGS; i++) {
		if (chainmask & (1 << i)) {
			val = REG_READ(ah, ar9300_cca_regs[i]);
			val &= 0xFFFFFE00;
			val |= (((u32) (-50) << 1) & 0x1ff);
			REG_WRITE(ah, ar9300_cca_regs[i], val);
		}
	}
}

void ar9003_hw_attach_phy_ops(struct ath_hw *ah)
{
	struct ath_hw_private_ops *priv_ops = ath9k_hw_private_ops(ah);

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
	priv_ops->loadnf = ar9003_hw_loadnf;
}
