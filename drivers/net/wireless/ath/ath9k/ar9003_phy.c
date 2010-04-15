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
	/* TODO */
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
	/* TODO */
}

static void ar9003_hw_init_bb(struct ath_hw *ah,
			      struct ath9k_channel *chan)
{
	/* TODO */
}

static int ar9003_hw_process_ini(struct ath_hw *ah,
				 struct ath9k_channel *chan)
{
	/* TODO */
	return -1;
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
}
