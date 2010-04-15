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
static void ar9003_hw_spur_mitigate(struct ath_hw *ah,
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

static u32 ar9003_hw_compute_pll_control(struct ath_hw *ah,
					 struct ath9k_channel *chan)
{
	u32 pll;

	pll = SM(0x5, AR_RTC_9300_PLL_REFDIV);

	if (chan && IS_CHAN_HALF_RATE(chan))
		pll |= SM(0x1, AR_RTC_9300_PLL_CLKSEL);
	else if (chan && IS_CHAN_QUARTER_RATE(chan))
		pll |= SM(0x2, AR_RTC_9300_PLL_CLKSEL);

	if (chan && IS_CHAN_5GHZ(chan)) {
		pll |= SM(0x28, AR_RTC_9300_PLL_DIV);

		/*
		 * When doing fast clock, set PLL to 0x142c
		 */
		if (IS_CHAN_A_5MHZ_SPACED(chan))
			pll = 0x142c;
	} else
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
	/* TODO */
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
	if (IS_CHAN_A_5MHZ_SPACED(chan))
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
	/* TODO */
}

static void ar9003_hw_mark_phy_inactive(struct ath_hw *ah)
{
	/* TODO */
}

static void ar9003_hw_set_delta_slope(struct ath_hw *ah,
				      struct ath9k_channel *chan)
{
	/* TODO */
}

static bool ar9003_hw_rfbus_req(struct ath_hw *ah)
{
	/* TODO */
	return false;
}

static void ar9003_hw_rfbus_done(struct ath_hw *ah)
{
	/* TODO */
}

static void ar9003_hw_enable_rfkill(struct ath_hw *ah)
{
	/* TODO */
}

static void ar9003_hw_set_diversity(struct ath_hw *ah, bool value)
{
	/* TODO */
}

static bool ar9003_hw_ani_control(struct ath_hw *ah,
				  enum ath9k_ani_cmd cmd, int param)
{
	/* TODO */
	return false;
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
}
