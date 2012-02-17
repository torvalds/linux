/*
 * Copyright (c) 2004-2007 Reyk Floeter <reyk@openbsd.org>
 * Copyright (c) 2006-2009 Nick Kossifidis <mickflemm@gmail.com>
 * Copyright (c) 2007-2008 Jiri Slaby <jirislaby@gmail.com>
 * Copyright (c) 2008-2009 Felix Fietkau <nbd@openwrt.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
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
 *
 */

/***********************\
* PHY related functions *
\***********************/

#include <linux/delay.h>
#include <linux/slab.h>
#include <asm/unaligned.h>

#include "ath5k.h"
#include "reg.h"
#include "rfbuffer.h"
#include "rfgain.h"
#include "../regd.h"


/**
 * DOC: PHY related functions
 *
 * Here we handle the low-level functions related to baseband
 * and analog frontend (RF) parts. This is by far the most complex
 * part of the hw code so make sure you know what you are doing.
 *
 * Here is a list of what this is all about:
 *
 * - Channel setting/switching
 *
 * - Automatic Gain Control (AGC) calibration
 *
 * - Noise Floor calibration
 *
 * - I/Q imbalance calibration (QAM correction)
 *
 * - Calibration due to thermal changes (gain_F)
 *
 * - Spur noise mitigation
 *
 * - RF/PHY initialization for the various operating modes and bwmodes
 *
 * - Antenna control
 *
 * - TX power control per channel/rate/packet type
 *
 * Also have in mind we never got documentation for most of these
 * functions, what we have comes mostly from Atheros's code, reverse
 * engineering and patent docs/presentations etc.
 */


/******************\
* Helper functions *
\******************/

/**
 * ath5k_hw_radio_revision() - Get the PHY Chip revision
 * @ah: The &struct ath5k_hw
 * @band: One of enum ieee80211_band
 *
 * Returns the revision number of a 2GHz, 5GHz or single chip
 * radio.
 */
u16
ath5k_hw_radio_revision(struct ath5k_hw *ah, enum ieee80211_band band)
{
	unsigned int i;
	u32 srev;
	u16 ret;

	/*
	 * Set the radio chip access register
	 */
	switch (band) {
	case IEEE80211_BAND_2GHZ:
		ath5k_hw_reg_write(ah, AR5K_PHY_SHIFT_2GHZ, AR5K_PHY(0));
		break;
	case IEEE80211_BAND_5GHZ:
		ath5k_hw_reg_write(ah, AR5K_PHY_SHIFT_5GHZ, AR5K_PHY(0));
		break;
	default:
		return 0;
	}

	usleep_range(2000, 2500);

	/* ...wait until PHY is ready and read the selected radio revision */
	ath5k_hw_reg_write(ah, 0x00001c16, AR5K_PHY(0x34));

	for (i = 0; i < 8; i++)
		ath5k_hw_reg_write(ah, 0x00010000, AR5K_PHY(0x20));

	if (ah->ah_version == AR5K_AR5210) {
		srev = ath5k_hw_reg_read(ah, AR5K_PHY(256) >> 28) & 0xf;
		ret = (u16)ath5k_hw_bitswap(srev, 4) + 1;
	} else {
		srev = (ath5k_hw_reg_read(ah, AR5K_PHY(0x100)) >> 24) & 0xff;
		ret = (u16)ath5k_hw_bitswap(((srev & 0xf0) >> 4) |
				((srev & 0x0f) << 4), 8);
	}

	/* Reset to the 5GHz mode */
	ath5k_hw_reg_write(ah, AR5K_PHY_SHIFT_5GHZ, AR5K_PHY(0));

	return ret;
}

/**
 * ath5k_channel_ok() - Check if a channel is supported by the hw
 * @ah: The &struct ath5k_hw
 * @channel: The &struct ieee80211_channel
 *
 * Note: We don't do any regulatory domain checks here, it's just
 * a sanity check.
 */
bool
ath5k_channel_ok(struct ath5k_hw *ah, struct ieee80211_channel *channel)
{
	u16 freq = channel->center_freq;

	/* Check if the channel is in our supported range */
	if (channel->band == IEEE80211_BAND_2GHZ) {
		if ((freq >= ah->ah_capabilities.cap_range.range_2ghz_min) &&
		    (freq <= ah->ah_capabilities.cap_range.range_2ghz_max))
			return true;
	} else if (channel->band == IEEE80211_BAND_5GHZ)
		if ((freq >= ah->ah_capabilities.cap_range.range_5ghz_min) &&
		    (freq <= ah->ah_capabilities.cap_range.range_5ghz_max))
			return true;

	return false;
}

/**
 * ath5k_hw_chan_has_spur_noise() - Check if channel is sensitive to spur noise
 * @ah: The &struct ath5k_hw
 * @channel: The &struct ieee80211_channel
 */
bool
ath5k_hw_chan_has_spur_noise(struct ath5k_hw *ah,
				struct ieee80211_channel *channel)
{
	u8 refclk_freq;

	if ((ah->ah_radio == AR5K_RF5112) ||
	(ah->ah_radio == AR5K_RF5413) ||
	(ah->ah_radio == AR5K_RF2413) ||
	(ah->ah_mac_version == (AR5K_SREV_AR2417 >> 4)))
		refclk_freq = 40;
	else
		refclk_freq = 32;

	if ((channel->center_freq % refclk_freq != 0) &&
	((channel->center_freq % refclk_freq < 10) ||
	(channel->center_freq % refclk_freq > 22)))
		return true;
	else
		return false;
}

/**
 * ath5k_hw_rfb_op() - Perform an operation on the given RF Buffer
 * @ah: The &struct ath5k_hw
 * @rf_regs: The struct ath5k_rf_reg
 * @val: New value
 * @reg_id: RF register ID
 * @set: Indicate we need to swap data
 *
 * This is an internal function used to modify RF Banks before
 * writing them to AR5K_RF_BUFFER. Check out rfbuffer.h for more
 * infos.
 */
static unsigned int
ath5k_hw_rfb_op(struct ath5k_hw *ah, const struct ath5k_rf_reg *rf_regs,
					u32 val, u8 reg_id, bool set)
{
	const struct ath5k_rf_reg *rfreg = NULL;
	u8 offset, bank, num_bits, col, position;
	u16 entry;
	u32 mask, data, last_bit, bits_shifted, first_bit;
	u32 *rfb;
	s32 bits_left;
	int i;

	data = 0;
	rfb = ah->ah_rf_banks;

	for (i = 0; i < ah->ah_rf_regs_count; i++) {
		if (rf_regs[i].index == reg_id) {
			rfreg = &rf_regs[i];
			break;
		}
	}

	if (rfb == NULL || rfreg == NULL) {
		ATH5K_PRINTF("Rf register not found!\n");
		/* should not happen */
		return 0;
	}

	bank = rfreg->bank;
	num_bits = rfreg->field.len;
	first_bit = rfreg->field.pos;
	col = rfreg->field.col;

	/* first_bit is an offset from bank's
	 * start. Since we have all banks on
	 * the same array, we use this offset
	 * to mark each bank's start */
	offset = ah->ah_offset[bank];

	/* Boundary check */
	if (!(col <= 3 && num_bits <= 32 && first_bit + num_bits <= 319)) {
		ATH5K_PRINTF("invalid values at offset %u\n", offset);
		return 0;
	}

	entry = ((first_bit - 1) / 8) + offset;
	position = (first_bit - 1) % 8;

	if (set)
		data = ath5k_hw_bitswap(val, num_bits);

	for (bits_shifted = 0, bits_left = num_bits; bits_left > 0;
	     position = 0, entry++) {

		last_bit = (position + bits_left > 8) ? 8 :
					position + bits_left;

		mask = (((1 << last_bit) - 1) ^ ((1 << position) - 1)) <<
								(col * 8);

		if (set) {
			rfb[entry] &= ~mask;
			rfb[entry] |= ((data << position) << (col * 8)) & mask;
			data >>= (8 - position);
		} else {
			data |= (((rfb[entry] & mask) >> (col * 8)) >> position)
				<< bits_shifted;
			bits_shifted += last_bit - position;
		}

		bits_left -= 8 - position;
	}

	data = set ? 1 : ath5k_hw_bitswap(data, num_bits);

	return data;
}

/**
 * ath5k_hw_write_ofdm_timings() - set OFDM timings on AR5212
 * @ah: the &struct ath5k_hw
 * @channel: the currently set channel upon reset
 *
 * Write the delta slope coefficient (used on pilot tracking ?) for OFDM
 * operation on the AR5212 upon reset. This is a helper for ath5k_hw_phy_init.
 *
 * Since delta slope is floating point we split it on its exponent and
 * mantissa and provide these values on hw.
 *
 * For more infos i think this patent is related
 * "http://www.freepatentsonline.com/7184495.html"
 */
static inline int
ath5k_hw_write_ofdm_timings(struct ath5k_hw *ah,
				struct ieee80211_channel *channel)
{
	/* Get exponent and mantissa and set it */
	u32 coef_scaled, coef_exp, coef_man,
		ds_coef_exp, ds_coef_man, clock;

	BUG_ON(!(ah->ah_version == AR5K_AR5212) ||
		(channel->hw_value == AR5K_MODE_11B));

	/* Get coefficient
	 * ALGO: coef = (5 * clock / carrier_freq) / 2
	 * we scale coef by shifting clock value by 24 for
	 * better precision since we use integers */
	switch (ah->ah_bwmode) {
	case AR5K_BWMODE_40MHZ:
		clock = 40 * 2;
		break;
	case AR5K_BWMODE_10MHZ:
		clock = 40 / 2;
		break;
	case AR5K_BWMODE_5MHZ:
		clock = 40 / 4;
		break;
	default:
		clock = 40;
		break;
	}
	coef_scaled = ((5 * (clock << 24)) / 2) / channel->center_freq;

	/* Get exponent
	 * ALGO: coef_exp = 14 - highest set bit position */
	coef_exp = ilog2(coef_scaled);

	/* Doesn't make sense if it's zero*/
	if (!coef_scaled || !coef_exp)
		return -EINVAL;

	/* Note: we've shifted coef_scaled by 24 */
	coef_exp = 14 - (coef_exp - 24);


	/* Get mantissa (significant digits)
	 * ALGO: coef_mant = floor(coef_scaled* 2^coef_exp+0.5) */
	coef_man = coef_scaled +
		(1 << (24 - coef_exp - 1));

	/* Calculate delta slope coefficient exponent
	 * and mantissa (remove scaling) and set them on hw */
	ds_coef_man = coef_man >> (24 - coef_exp);
	ds_coef_exp = coef_exp - 16;

	AR5K_REG_WRITE_BITS(ah, AR5K_PHY_TIMING_3,
		AR5K_PHY_TIMING_3_DSC_MAN, ds_coef_man);
	AR5K_REG_WRITE_BITS(ah, AR5K_PHY_TIMING_3,
		AR5K_PHY_TIMING_3_DSC_EXP, ds_coef_exp);

	return 0;
}

/**
 * ath5k_hw_phy_disable() - Disable PHY
 * @ah: The &struct ath5k_hw
 */
int ath5k_hw_phy_disable(struct ath5k_hw *ah)
{
	/*Just a try M.F.*/
	ath5k_hw_reg_write(ah, AR5K_PHY_ACT_DISABLE, AR5K_PHY_ACT);

	return 0;
}

/**
 * ath5k_hw_wait_for_synth() - Wait for synth to settle
 * @ah: The &struct ath5k_hw
 * @channel: The &struct ieee80211_channel
 */
static void
ath5k_hw_wait_for_synth(struct ath5k_hw *ah,
			struct ieee80211_channel *channel)
{
	/*
	 * On 5211+ read activation -> rx delay
	 * and use it (100ns steps).
	 */
	if (ah->ah_version != AR5K_AR5210) {
		u32 delay;
		delay = ath5k_hw_reg_read(ah, AR5K_PHY_RX_DELAY) &
			AR5K_PHY_RX_DELAY_M;
		delay = (channel->hw_value == AR5K_MODE_11B) ?
			((delay << 2) / 22) : (delay / 10);
		if (ah->ah_bwmode == AR5K_BWMODE_10MHZ)
			delay = delay << 1;
		if (ah->ah_bwmode == AR5K_BWMODE_5MHZ)
			delay = delay << 2;
		/* XXX: /2 on turbo ? Let's be safe
		 * for now */
		usleep_range(100 + delay, 100 + (2 * delay));
	} else {
		usleep_range(1000, 1500);
	}
}


/**********************\
* RF Gain optimization *
\**********************/

/**
 * DOC: RF Gain optimization
 *
 * This code is used to optimize RF gain on different environments
 * (temperature mostly) based on feedback from a power detector.
 *
 * It's only used on RF5111 and RF5112, later RF chips seem to have
 * auto adjustment on hw -notice they have a much smaller BANK 7 and
 * no gain optimization ladder-.
 *
 * For more infos check out this patent doc
 * "http://www.freepatentsonline.com/7400691.html"
 *
 * This paper describes power drops as seen on the receiver due to
 * probe packets
 * "http://www.cnri.dit.ie/publications/ICT08%20-%20Practical%20Issues
 * %20of%20Power%20Control.pdf"
 *
 * And this is the MadWiFi bug entry related to the above
 * "http://madwifi-project.org/ticket/1659"
 * with various measurements and diagrams
 */

/**
 * ath5k_hw_rfgain_opt_init() - Initialize ah_gain during attach
 * @ah: The &struct ath5k_hw
 */
int ath5k_hw_rfgain_opt_init(struct ath5k_hw *ah)
{
	/* Initialize the gain optimization values */
	switch (ah->ah_radio) {
	case AR5K_RF5111:
		ah->ah_gain.g_step_idx = rfgain_opt_5111.go_default;
		ah->ah_gain.g_low = 20;
		ah->ah_gain.g_high = 35;
		ah->ah_gain.g_state = AR5K_RFGAIN_ACTIVE;
		break;
	case AR5K_RF5112:
		ah->ah_gain.g_step_idx = rfgain_opt_5112.go_default;
		ah->ah_gain.g_low = 20;
		ah->ah_gain.g_high = 85;
		ah->ah_gain.g_state = AR5K_RFGAIN_ACTIVE;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/**
 * ath5k_hw_request_rfgain_probe() - Request a PAPD probe packet
 * @ah: The &struct ath5k_hw
 *
 * Schedules a gain probe check on the next transmitted packet.
 * That means our next packet is going to be sent with lower
 * tx power and a Peak to Average Power Detector (PAPD) will try
 * to measure the gain.
 *
 * TODO: Force a tx packet (bypassing PCU arbitrator etc)
 * just after we enable the probe so that we don't mess with
 * standard traffic.
 */
static void
ath5k_hw_request_rfgain_probe(struct ath5k_hw *ah)
{

	/* Skip if gain calibration is inactive or
	 * we already handle a probe request */
	if (ah->ah_gain.g_state != AR5K_RFGAIN_ACTIVE)
		return;

	/* Send the packet with 2dB below max power as
	 * patent doc suggest */
	ath5k_hw_reg_write(ah, AR5K_REG_SM(ah->ah_txpower.txp_ofdm - 4,
			AR5K_PHY_PAPD_PROBE_TXPOWER) |
			AR5K_PHY_PAPD_PROBE_TX_NEXT, AR5K_PHY_PAPD_PROBE);

	ah->ah_gain.g_state = AR5K_RFGAIN_READ_REQUESTED;

}

/**
 * ath5k_hw_rf_gainf_corr() - Calculate Gain_F measurement correction
 * @ah: The &struct ath5k_hw
 *
 * Calculate Gain_F measurement correction
 * based on the current step for RF5112 rev. 2
 */
static u32
ath5k_hw_rf_gainf_corr(struct ath5k_hw *ah)
{
	u32 mix, step;
	u32 *rf;
	const struct ath5k_gain_opt *go;
	const struct ath5k_gain_opt_step *g_step;
	const struct ath5k_rf_reg *rf_regs;

	/* Only RF5112 Rev. 2 supports it */
	if ((ah->ah_radio != AR5K_RF5112) ||
	(ah->ah_radio_5ghz_revision <= AR5K_SREV_RAD_5112A))
		return 0;

	go = &rfgain_opt_5112;
	rf_regs = rf_regs_5112a;
	ah->ah_rf_regs_count = ARRAY_SIZE(rf_regs_5112a);

	g_step = &go->go_step[ah->ah_gain.g_step_idx];

	if (ah->ah_rf_banks == NULL)
		return 0;

	rf = ah->ah_rf_banks;
	ah->ah_gain.g_f_corr = 0;

	/* No VGA (Variable Gain Amplifier) override, skip */
	if (ath5k_hw_rfb_op(ah, rf_regs, 0, AR5K_RF_MIXVGA_OVR, false) != 1)
		return 0;

	/* Mix gain stepping */
	step = ath5k_hw_rfb_op(ah, rf_regs, 0, AR5K_RF_MIXGAIN_STEP, false);

	/* Mix gain override */
	mix = g_step->gos_param[0];

	switch (mix) {
	case 3:
		ah->ah_gain.g_f_corr = step * 2;
		break;
	case 2:
		ah->ah_gain.g_f_corr = (step - 5) * 2;
		break;
	case 1:
		ah->ah_gain.g_f_corr = step;
		break;
	default:
		ah->ah_gain.g_f_corr = 0;
		break;
	}

	return ah->ah_gain.g_f_corr;
}

/**
 * ath5k_hw_rf_check_gainf_readback() - Validate Gain_F feedback from detector
 * @ah: The &struct ath5k_hw
 *
 * Check if current gain_F measurement is in the range of our
 * power detector windows. If we get a measurement outside range
 * we know it's not accurate (detectors can't measure anything outside
 * their detection window) so we must ignore it.
 *
 * Returns true if readback was O.K. or false on failure
 */
static bool
ath5k_hw_rf_check_gainf_readback(struct ath5k_hw *ah)
{
	const struct ath5k_rf_reg *rf_regs;
	u32 step, mix_ovr, level[4];
	u32 *rf;

	if (ah->ah_rf_banks == NULL)
		return false;

	rf = ah->ah_rf_banks;

	if (ah->ah_radio == AR5K_RF5111) {

		rf_regs = rf_regs_5111;
		ah->ah_rf_regs_count = ARRAY_SIZE(rf_regs_5111);

		step = ath5k_hw_rfb_op(ah, rf_regs, 0, AR5K_RF_RFGAIN_STEP,
			false);

		level[0] = 0;
		level[1] = (step == 63) ? 50 : step + 4;
		level[2] = (step != 63) ? 64 : level[0];
		level[3] = level[2] + 50;

		ah->ah_gain.g_high = level[3] -
			(step == 63 ? AR5K_GAIN_DYN_ADJUST_HI_MARGIN : -5);
		ah->ah_gain.g_low = level[0] +
			(step == 63 ? AR5K_GAIN_DYN_ADJUST_LO_MARGIN : 0);
	} else {

		rf_regs = rf_regs_5112;
		ah->ah_rf_regs_count = ARRAY_SIZE(rf_regs_5112);

		mix_ovr = ath5k_hw_rfb_op(ah, rf_regs, 0, AR5K_RF_MIXVGA_OVR,
			false);

		level[0] = level[2] = 0;

		if (mix_ovr == 1) {
			level[1] = level[3] = 83;
		} else {
			level[1] = level[3] = 107;
			ah->ah_gain.g_high = 55;
		}
	}

	return (ah->ah_gain.g_current >= level[0] &&
			ah->ah_gain.g_current <= level[1]) ||
		(ah->ah_gain.g_current >= level[2] &&
			ah->ah_gain.g_current <= level[3]);
}

/**
 * ath5k_hw_rf_gainf_adjust() - Perform Gain_F adjustment
 * @ah: The &struct ath5k_hw
 *
 * Choose the right target gain based on current gain
 * and RF gain optimization ladder
 */
static s8
ath5k_hw_rf_gainf_adjust(struct ath5k_hw *ah)
{
	const struct ath5k_gain_opt *go;
	const struct ath5k_gain_opt_step *g_step;
	int ret = 0;

	switch (ah->ah_radio) {
	case AR5K_RF5111:
		go = &rfgain_opt_5111;
		break;
	case AR5K_RF5112:
		go = &rfgain_opt_5112;
		break;
	default:
		return 0;
	}

	g_step = &go->go_step[ah->ah_gain.g_step_idx];

	if (ah->ah_gain.g_current >= ah->ah_gain.g_high) {

		/* Reached maximum */
		if (ah->ah_gain.g_step_idx == 0)
			return -1;

		for (ah->ah_gain.g_target = ah->ah_gain.g_current;
				ah->ah_gain.g_target >=  ah->ah_gain.g_high &&
				ah->ah_gain.g_step_idx > 0;
				g_step = &go->go_step[ah->ah_gain.g_step_idx])
			ah->ah_gain.g_target -= 2 *
			    (go->go_step[--(ah->ah_gain.g_step_idx)].gos_gain -
			    g_step->gos_gain);

		ret = 1;
		goto done;
	}

	if (ah->ah_gain.g_current <= ah->ah_gain.g_low) {

		/* Reached minimum */
		if (ah->ah_gain.g_step_idx == (go->go_steps_count - 1))
			return -2;

		for (ah->ah_gain.g_target = ah->ah_gain.g_current;
				ah->ah_gain.g_target <= ah->ah_gain.g_low &&
				ah->ah_gain.g_step_idx < go->go_steps_count - 1;
				g_step = &go->go_step[ah->ah_gain.g_step_idx])
			ah->ah_gain.g_target -= 2 *
			    (go->go_step[++ah->ah_gain.g_step_idx].gos_gain -
			    g_step->gos_gain);

		ret = 2;
		goto done;
	}

done:
	ATH5K_DBG(ah, ATH5K_DEBUG_CALIBRATE,
		"ret %d, gain step %u, current gain %u, target gain %u\n",
		ret, ah->ah_gain.g_step_idx, ah->ah_gain.g_current,
		ah->ah_gain.g_target);

	return ret;
}

/**
 * ath5k_hw_gainf_calibrate() - Do a gain_F calibration
 * @ah: The &struct ath5k_hw
 *
 * Main callback for thermal RF gain calibration engine
 * Check for a new gain reading and schedule an adjustment
 * if needed.
 *
 * Returns one of enum ath5k_rfgain codes
 */
enum ath5k_rfgain
ath5k_hw_gainf_calibrate(struct ath5k_hw *ah)
{
	u32 data, type;
	struct ath5k_eeprom_info *ee = &ah->ah_capabilities.cap_eeprom;

	if (ah->ah_rf_banks == NULL ||
	ah->ah_gain.g_state == AR5K_RFGAIN_INACTIVE)
		return AR5K_RFGAIN_INACTIVE;

	/* No check requested, either engine is inactive
	 * or an adjustment is already requested */
	if (ah->ah_gain.g_state != AR5K_RFGAIN_READ_REQUESTED)
		goto done;

	/* Read the PAPD (Peak to Average Power Detector)
	 * register */
	data = ath5k_hw_reg_read(ah, AR5K_PHY_PAPD_PROBE);

	/* No probe is scheduled, read gain_F measurement */
	if (!(data & AR5K_PHY_PAPD_PROBE_TX_NEXT)) {
		ah->ah_gain.g_current = data >> AR5K_PHY_PAPD_PROBE_GAINF_S;
		type = AR5K_REG_MS(data, AR5K_PHY_PAPD_PROBE_TYPE);

		/* If tx packet is CCK correct the gain_F measurement
		 * by cck ofdm gain delta */
		if (type == AR5K_PHY_PAPD_PROBE_TYPE_CCK) {
			if (ah->ah_radio_5ghz_revision >= AR5K_SREV_RAD_5112A)
				ah->ah_gain.g_current +=
					ee->ee_cck_ofdm_gain_delta;
			else
				ah->ah_gain.g_current +=
					AR5K_GAIN_CCK_PROBE_CORR;
		}

		/* Further correct gain_F measurement for
		 * RF5112A radios */
		if (ah->ah_radio_5ghz_revision >= AR5K_SREV_RAD_5112A) {
			ath5k_hw_rf_gainf_corr(ah);
			ah->ah_gain.g_current =
				ah->ah_gain.g_current >= ah->ah_gain.g_f_corr ?
				(ah->ah_gain.g_current - ah->ah_gain.g_f_corr) :
				0;
		}

		/* Check if measurement is ok and if we need
		 * to adjust gain, schedule a gain adjustment,
		 * else switch back to the active state */
		if (ath5k_hw_rf_check_gainf_readback(ah) &&
		AR5K_GAIN_CHECK_ADJUST(&ah->ah_gain) &&
		ath5k_hw_rf_gainf_adjust(ah)) {
			ah->ah_gain.g_state = AR5K_RFGAIN_NEED_CHANGE;
		} else {
			ah->ah_gain.g_state = AR5K_RFGAIN_ACTIVE;
		}
	}

done:
	return ah->ah_gain.g_state;
}

/**
 * ath5k_hw_rfgain_init() - Write initial RF gain settings to hw
 * @ah: The &struct ath5k_hw
 * @band: One of enum ieee80211_band
 *
 * Write initial RF gain table to set the RF sensitivity.
 *
 * NOTE: This one works on all RF chips and has nothing to do
 * with Gain_F calibration
 */
static int
ath5k_hw_rfgain_init(struct ath5k_hw *ah, enum ieee80211_band band)
{
	const struct ath5k_ini_rfgain *ath5k_rfg;
	unsigned int i, size, index;

	switch (ah->ah_radio) {
	case AR5K_RF5111:
		ath5k_rfg = rfgain_5111;
		size = ARRAY_SIZE(rfgain_5111);
		break;
	case AR5K_RF5112:
		ath5k_rfg = rfgain_5112;
		size = ARRAY_SIZE(rfgain_5112);
		break;
	case AR5K_RF2413:
		ath5k_rfg = rfgain_2413;
		size = ARRAY_SIZE(rfgain_2413);
		break;
	case AR5K_RF2316:
		ath5k_rfg = rfgain_2316;
		size = ARRAY_SIZE(rfgain_2316);
		break;
	case AR5K_RF5413:
		ath5k_rfg = rfgain_5413;
		size = ARRAY_SIZE(rfgain_5413);
		break;
	case AR5K_RF2317:
	case AR5K_RF2425:
		ath5k_rfg = rfgain_2425;
		size = ARRAY_SIZE(rfgain_2425);
		break;
	default:
		return -EINVAL;
	}

	index = (band == IEEE80211_BAND_2GHZ) ? 1 : 0;

	for (i = 0; i < size; i++) {
		AR5K_REG_WAIT(i);
		ath5k_hw_reg_write(ah, ath5k_rfg[i].rfg_value[index],
			(u32)ath5k_rfg[i].rfg_register);
	}

	return 0;
}


/********************\
* RF Registers setup *
\********************/

/**
 * ath5k_hw_rfregs_init() - Initialize RF register settings
 * @ah: The &struct ath5k_hw
 * @channel: The &struct ieee80211_channel
 * @mode: One of enum ath5k_driver_mode
 *
 * Setup RF registers by writing RF buffer on hw. For
 * more infos on this, check out rfbuffer.h
 */
static int
ath5k_hw_rfregs_init(struct ath5k_hw *ah,
			struct ieee80211_channel *channel,
			unsigned int mode)
{
	const struct ath5k_rf_reg *rf_regs;
	const struct ath5k_ini_rfbuffer *ini_rfb;
	const struct ath5k_gain_opt *go = NULL;
	const struct ath5k_gain_opt_step *g_step;
	struct ath5k_eeprom_info *ee = &ah->ah_capabilities.cap_eeprom;
	u8 ee_mode = 0;
	u32 *rfb;
	int i, obdb = -1, bank = -1;

	switch (ah->ah_radio) {
	case AR5K_RF5111:
		rf_regs = rf_regs_5111;
		ah->ah_rf_regs_count = ARRAY_SIZE(rf_regs_5111);
		ini_rfb = rfb_5111;
		ah->ah_rf_banks_size = ARRAY_SIZE(rfb_5111);
		go = &rfgain_opt_5111;
		break;
	case AR5K_RF5112:
		if (ah->ah_radio_5ghz_revision >= AR5K_SREV_RAD_5112A) {
			rf_regs = rf_regs_5112a;
			ah->ah_rf_regs_count = ARRAY_SIZE(rf_regs_5112a);
			ini_rfb = rfb_5112a;
			ah->ah_rf_banks_size = ARRAY_SIZE(rfb_5112a);
		} else {
			rf_regs = rf_regs_5112;
			ah->ah_rf_regs_count = ARRAY_SIZE(rf_regs_5112);
			ini_rfb = rfb_5112;
			ah->ah_rf_banks_size = ARRAY_SIZE(rfb_5112);
		}
		go = &rfgain_opt_5112;
		break;
	case AR5K_RF2413:
		rf_regs = rf_regs_2413;
		ah->ah_rf_regs_count = ARRAY_SIZE(rf_regs_2413);
		ini_rfb = rfb_2413;
		ah->ah_rf_banks_size = ARRAY_SIZE(rfb_2413);
		break;
	case AR5K_RF2316:
		rf_regs = rf_regs_2316;
		ah->ah_rf_regs_count = ARRAY_SIZE(rf_regs_2316);
		ini_rfb = rfb_2316;
		ah->ah_rf_banks_size = ARRAY_SIZE(rfb_2316);
		break;
	case AR5K_RF5413:
		rf_regs = rf_regs_5413;
		ah->ah_rf_regs_count = ARRAY_SIZE(rf_regs_5413);
		ini_rfb = rfb_5413;
		ah->ah_rf_banks_size = ARRAY_SIZE(rfb_5413);
		break;
	case AR5K_RF2317:
		rf_regs = rf_regs_2425;
		ah->ah_rf_regs_count = ARRAY_SIZE(rf_regs_2425);
		ini_rfb = rfb_2317;
		ah->ah_rf_banks_size = ARRAY_SIZE(rfb_2317);
		break;
	case AR5K_RF2425:
		rf_regs = rf_regs_2425;
		ah->ah_rf_regs_count = ARRAY_SIZE(rf_regs_2425);
		if (ah->ah_mac_srev < AR5K_SREV_AR2417) {
			ini_rfb = rfb_2425;
			ah->ah_rf_banks_size = ARRAY_SIZE(rfb_2425);
		} else {
			ini_rfb = rfb_2417;
			ah->ah_rf_banks_size = ARRAY_SIZE(rfb_2417);
		}
		break;
	default:
		return -EINVAL;
	}

	/* If it's the first time we set RF buffer, allocate
	 * ah->ah_rf_banks based on ah->ah_rf_banks_size
	 * we set above */
	if (ah->ah_rf_banks == NULL) {
		ah->ah_rf_banks = kmalloc(sizeof(u32) * ah->ah_rf_banks_size,
								GFP_KERNEL);
		if (ah->ah_rf_banks == NULL) {
			ATH5K_ERR(ah, "out of memory\n");
			return -ENOMEM;
		}
	}

	/* Copy values to modify them */
	rfb = ah->ah_rf_banks;

	for (i = 0; i < ah->ah_rf_banks_size; i++) {
		if (ini_rfb[i].rfb_bank >= AR5K_MAX_RF_BANKS) {
			ATH5K_ERR(ah, "invalid bank\n");
			return -EINVAL;
		}

		/* Bank changed, write down the offset */
		if (bank != ini_rfb[i].rfb_bank) {
			bank = ini_rfb[i].rfb_bank;
			ah->ah_offset[bank] = i;
		}

		rfb[i] = ini_rfb[i].rfb_mode_data[mode];
	}

	/* Set Output and Driver bias current (OB/DB) */
	if (channel->band == IEEE80211_BAND_2GHZ) {

		if (channel->hw_value == AR5K_MODE_11B)
			ee_mode = AR5K_EEPROM_MODE_11B;
		else
			ee_mode = AR5K_EEPROM_MODE_11G;

		/* For RF511X/RF211X combination we
		 * use b_OB and b_DB parameters stored
		 * in eeprom on ee->ee_ob[ee_mode][0]
		 *
		 * For all other chips we use OB/DB for 2GHz
		 * stored in the b/g modal section just like
		 * 802.11a on ee->ee_ob[ee_mode][1] */
		if ((ah->ah_radio == AR5K_RF5111) ||
		(ah->ah_radio == AR5K_RF5112))
			obdb = 0;
		else
			obdb = 1;

		ath5k_hw_rfb_op(ah, rf_regs, ee->ee_ob[ee_mode][obdb],
						AR5K_RF_OB_2GHZ, true);

		ath5k_hw_rfb_op(ah, rf_regs, ee->ee_db[ee_mode][obdb],
						AR5K_RF_DB_2GHZ, true);

	/* RF5111 always needs OB/DB for 5GHz, even if we use 2GHz */
	} else if ((channel->band == IEEE80211_BAND_5GHZ) ||
			(ah->ah_radio == AR5K_RF5111)) {

		/* For 11a, Turbo and XR we need to choose
		 * OB/DB based on frequency range */
		ee_mode = AR5K_EEPROM_MODE_11A;
		obdb =	 channel->center_freq >= 5725 ? 3 :
			(channel->center_freq >= 5500 ? 2 :
			(channel->center_freq >= 5260 ? 1 :
			 (channel->center_freq > 4000 ? 0 : -1)));

		if (obdb < 0)
			return -EINVAL;

		ath5k_hw_rfb_op(ah, rf_regs, ee->ee_ob[ee_mode][obdb],
						AR5K_RF_OB_5GHZ, true);

		ath5k_hw_rfb_op(ah, rf_regs, ee->ee_db[ee_mode][obdb],
						AR5K_RF_DB_5GHZ, true);
	}

	g_step = &go->go_step[ah->ah_gain.g_step_idx];

	/* Set turbo mode (N/A on RF5413) */
	if ((ah->ah_bwmode == AR5K_BWMODE_40MHZ) &&
	(ah->ah_radio != AR5K_RF5413))
		ath5k_hw_rfb_op(ah, rf_regs, 1, AR5K_RF_TURBO, false);

	/* Bank Modifications (chip-specific) */
	if (ah->ah_radio == AR5K_RF5111) {

		/* Set gain_F settings according to current step */
		if (channel->hw_value != AR5K_MODE_11B) {

			AR5K_REG_WRITE_BITS(ah, AR5K_PHY_FRAME_CTL,
					AR5K_PHY_FRAME_CTL_TX_CLIP,
					g_step->gos_param[0]);

			ath5k_hw_rfb_op(ah, rf_regs, g_step->gos_param[1],
							AR5K_RF_PWD_90, true);

			ath5k_hw_rfb_op(ah, rf_regs, g_step->gos_param[2],
							AR5K_RF_PWD_84, true);

			ath5k_hw_rfb_op(ah, rf_regs, g_step->gos_param[3],
						AR5K_RF_RFGAIN_SEL, true);

			/* We programmed gain_F parameters, switch back
			 * to active state */
			ah->ah_gain.g_state = AR5K_RFGAIN_ACTIVE;

		}

		/* Bank 6/7 setup */

		ath5k_hw_rfb_op(ah, rf_regs, !ee->ee_xpd[ee_mode],
						AR5K_RF_PWD_XPD, true);

		ath5k_hw_rfb_op(ah, rf_regs, ee->ee_x_gain[ee_mode],
						AR5K_RF_XPD_GAIN, true);

		ath5k_hw_rfb_op(ah, rf_regs, ee->ee_i_gain[ee_mode],
						AR5K_RF_GAIN_I, true);

		ath5k_hw_rfb_op(ah, rf_regs, ee->ee_xpd[ee_mode],
						AR5K_RF_PLO_SEL, true);

		/* Tweak power detectors for half/quarter rate support */
		if (ah->ah_bwmode == AR5K_BWMODE_5MHZ ||
		ah->ah_bwmode == AR5K_BWMODE_10MHZ) {
			u8 wait_i;

			ath5k_hw_rfb_op(ah, rf_regs, 0x1f,
						AR5K_RF_WAIT_S, true);

			wait_i = (ah->ah_bwmode == AR5K_BWMODE_5MHZ) ?
							0x1f : 0x10;

			ath5k_hw_rfb_op(ah, rf_regs, wait_i,
						AR5K_RF_WAIT_I, true);
			ath5k_hw_rfb_op(ah, rf_regs, 3,
						AR5K_RF_MAX_TIME, true);

		}
	}

	if (ah->ah_radio == AR5K_RF5112) {

		/* Set gain_F settings according to current step */
		if (channel->hw_value != AR5K_MODE_11B) {

			ath5k_hw_rfb_op(ah, rf_regs, g_step->gos_param[0],
						AR5K_RF_MIXGAIN_OVR, true);

			ath5k_hw_rfb_op(ah, rf_regs, g_step->gos_param[1],
						AR5K_RF_PWD_138, true);

			ath5k_hw_rfb_op(ah, rf_regs, g_step->gos_param[2],
						AR5K_RF_PWD_137, true);

			ath5k_hw_rfb_op(ah, rf_regs, g_step->gos_param[3],
						AR5K_RF_PWD_136, true);

			ath5k_hw_rfb_op(ah, rf_regs, g_step->gos_param[4],
						AR5K_RF_PWD_132, true);

			ath5k_hw_rfb_op(ah, rf_regs, g_step->gos_param[5],
						AR5K_RF_PWD_131, true);

			ath5k_hw_rfb_op(ah, rf_regs, g_step->gos_param[6],
						AR5K_RF_PWD_130, true);

			/* We programmed gain_F parameters, switch back
			 * to active state */
			ah->ah_gain.g_state = AR5K_RFGAIN_ACTIVE;
		}

		/* Bank 6/7 setup */

		ath5k_hw_rfb_op(ah, rf_regs, ee->ee_xpd[ee_mode],
						AR5K_RF_XPD_SEL, true);

		if (ah->ah_radio_5ghz_revision < AR5K_SREV_RAD_5112A) {
			/* Rev. 1 supports only one xpd */
			ath5k_hw_rfb_op(ah, rf_regs,
						ee->ee_x_gain[ee_mode],
						AR5K_RF_XPD_GAIN, true);

		} else {
			u8 *pdg_curve_to_idx = ee->ee_pdc_to_idx[ee_mode];
			if (ee->ee_pd_gains[ee_mode] > 1) {
				ath5k_hw_rfb_op(ah, rf_regs,
						pdg_curve_to_idx[0],
						AR5K_RF_PD_GAIN_LO, true);
				ath5k_hw_rfb_op(ah, rf_regs,
						pdg_curve_to_idx[1],
						AR5K_RF_PD_GAIN_HI, true);
			} else {
				ath5k_hw_rfb_op(ah, rf_regs,
						pdg_curve_to_idx[0],
						AR5K_RF_PD_GAIN_LO, true);
				ath5k_hw_rfb_op(ah, rf_regs,
						pdg_curve_to_idx[0],
						AR5K_RF_PD_GAIN_HI, true);
			}

			/* Lower synth voltage on Rev 2 */
			if (ah->ah_radio == AR5K_RF5112 &&
			    (ah->ah_radio_5ghz_revision & AR5K_SREV_REV) > 0) {
				ath5k_hw_rfb_op(ah, rf_regs, 2,
						AR5K_RF_HIGH_VC_CP, true);

				ath5k_hw_rfb_op(ah, rf_regs, 2,
						AR5K_RF_MID_VC_CP, true);

				ath5k_hw_rfb_op(ah, rf_regs, 2,
						AR5K_RF_LOW_VC_CP, true);

				ath5k_hw_rfb_op(ah, rf_regs, 2,
						AR5K_RF_PUSH_UP, true);
			}

			/* Decrease power consumption on 5213+ BaseBand */
			if (ah->ah_phy_revision >= AR5K_SREV_PHY_5212A) {
				ath5k_hw_rfb_op(ah, rf_regs, 1,
						AR5K_RF_PAD2GND, true);

				ath5k_hw_rfb_op(ah, rf_regs, 1,
						AR5K_RF_XB2_LVL, true);

				ath5k_hw_rfb_op(ah, rf_regs, 1,
						AR5K_RF_XB5_LVL, true);

				ath5k_hw_rfb_op(ah, rf_regs, 1,
						AR5K_RF_PWD_167, true);

				ath5k_hw_rfb_op(ah, rf_regs, 1,
						AR5K_RF_PWD_166, true);
			}
		}

		ath5k_hw_rfb_op(ah, rf_regs, ee->ee_i_gain[ee_mode],
						AR5K_RF_GAIN_I, true);

		/* Tweak power detector for half/quarter rates */
		if (ah->ah_bwmode == AR5K_BWMODE_5MHZ ||
		ah->ah_bwmode == AR5K_BWMODE_10MHZ) {
			u8 pd_delay;

			pd_delay = (ah->ah_bwmode == AR5K_BWMODE_5MHZ) ?
							0xf : 0x8;

			ath5k_hw_rfb_op(ah, rf_regs, pd_delay,
						AR5K_RF_PD_PERIOD_A, true);
			ath5k_hw_rfb_op(ah, rf_regs, 0xf,
						AR5K_RF_PD_DELAY_A, true);

		}
	}

	if (ah->ah_radio == AR5K_RF5413 &&
	channel->band == IEEE80211_BAND_2GHZ) {

		ath5k_hw_rfb_op(ah, rf_regs, 1, AR5K_RF_DERBY_CHAN_SEL_MODE,
									true);

		/* Set optimum value for early revisions (on pci-e chips) */
		if (ah->ah_mac_srev >= AR5K_SREV_AR5424 &&
		ah->ah_mac_srev < AR5K_SREV_AR5413)
			ath5k_hw_rfb_op(ah, rf_regs, ath5k_hw_bitswap(6, 3),
						AR5K_RF_PWD_ICLOBUF_2G, true);

	}

	/* Write RF banks on hw */
	for (i = 0; i < ah->ah_rf_banks_size; i++) {
		AR5K_REG_WAIT(i);
		ath5k_hw_reg_write(ah, rfb[i], ini_rfb[i].rfb_ctrl_register);
	}

	return 0;
}


/**************************\
  PHY/RF channel functions
\**************************/

/**
 * ath5k_hw_rf5110_chan2athchan() - Convert channel freq on RF5110
 * @channel: The &struct ieee80211_channel
 *
 * Map channel frequency to IEEE channel number and convert it
 * to an internal channel value used by the RF5110 chipset.
 */
static u32
ath5k_hw_rf5110_chan2athchan(struct ieee80211_channel *channel)
{
	u32 athchan;

	athchan = (ath5k_hw_bitswap(
			(ieee80211_frequency_to_channel(
				channel->center_freq) - 24) / 2, 5)
				<< 1) | (1 << 6) | 0x1;
	return athchan;
}

/**
 * ath5k_hw_rf5110_channel() - Set channel frequency on RF5110
 * @ah: The &struct ath5k_hw
 * @channel: The &struct ieee80211_channel
 */
static int
ath5k_hw_rf5110_channel(struct ath5k_hw *ah,
		struct ieee80211_channel *channel)
{
	u32 data;

	/*
	 * Set the channel and wait
	 */
	data = ath5k_hw_rf5110_chan2athchan(channel);
	ath5k_hw_reg_write(ah, data, AR5K_RF_BUFFER);
	ath5k_hw_reg_write(ah, 0, AR5K_RF_BUFFER_CONTROL_0);
	usleep_range(1000, 1500);

	return 0;
}

/**
 * ath5k_hw_rf5111_chan2athchan() - Handle 2GHz channels on RF5111/2111
 * @ieee: IEEE channel number
 * @athchan: The &struct ath5k_athchan_2ghz
 *
 * In order to enable the RF2111 frequency converter on RF5111/2111 setups
 * we need to add some offsets and extra flags to the data values we pass
 * on to the PHY. So for every 2GHz channel this function gets called
 * to do the conversion.
 */
static int
ath5k_hw_rf5111_chan2athchan(unsigned int ieee,
		struct ath5k_athchan_2ghz *athchan)
{
	int channel;

	/* Cast this value to catch negative channel numbers (>= -19) */
	channel = (int)ieee;

	/*
	 * Map 2GHz IEEE channel to 5GHz Atheros channel
	 */
	if (channel <= 13) {
		athchan->a2_athchan = 115 + channel;
		athchan->a2_flags = 0x46;
	} else if (channel == 14) {
		athchan->a2_athchan = 124;
		athchan->a2_flags = 0x44;
	} else if (channel >= 15 && channel <= 26) {
		athchan->a2_athchan = ((channel - 14) * 4) + 132;
		athchan->a2_flags = 0x46;
	} else
		return -EINVAL;

	return 0;
}

/**
 * ath5k_hw_rf5111_channel() - Set channel frequency on RF5111/2111
 * @ah: The &struct ath5k_hw
 * @channel: The &struct ieee80211_channel
 */
static int
ath5k_hw_rf5111_channel(struct ath5k_hw *ah,
		struct ieee80211_channel *channel)
{
	struct ath5k_athchan_2ghz ath5k_channel_2ghz;
	unsigned int ath5k_channel =
		ieee80211_frequency_to_channel(channel->center_freq);
	u32 data0, data1, clock;
	int ret;

	/*
	 * Set the channel on the RF5111 radio
	 */
	data0 = data1 = 0;

	if (channel->band == IEEE80211_BAND_2GHZ) {
		/* Map 2GHz channel to 5GHz Atheros channel ID */
		ret = ath5k_hw_rf5111_chan2athchan(
			ieee80211_frequency_to_channel(channel->center_freq),
			&ath5k_channel_2ghz);
		if (ret)
			return ret;

		ath5k_channel = ath5k_channel_2ghz.a2_athchan;
		data0 = ((ath5k_hw_bitswap(ath5k_channel_2ghz.a2_flags, 8) & 0xff)
		    << 5) | (1 << 4);
	}

	if (ath5k_channel < 145 || !(ath5k_channel & 1)) {
		clock = 1;
		data1 = ((ath5k_hw_bitswap(ath5k_channel - 24, 8) & 0xff) << 2) |
			(clock << 1) | (1 << 10) | 1;
	} else {
		clock = 0;
		data1 = ((ath5k_hw_bitswap((ath5k_channel - 24) / 2, 8) & 0xff)
			<< 2) | (clock << 1) | (1 << 10) | 1;
	}

	ath5k_hw_reg_write(ah, (data1 & 0xff) | ((data0 & 0xff) << 8),
			AR5K_RF_BUFFER);
	ath5k_hw_reg_write(ah, ((data1 >> 8) & 0xff) | (data0 & 0xff00),
			AR5K_RF_BUFFER_CONTROL_3);

	return 0;
}

/**
 * ath5k_hw_rf5112_channel() - Set channel frequency on 5112 and newer
 * @ah: The &struct ath5k_hw
 * @channel: The &struct ieee80211_channel
 *
 * On RF5112/2112 and newer we don't need to do any conversion.
 * We pass the frequency value after a few modifications to the
 * chip directly.
 *
 * NOTE: Make sure channel frequency given is within our range or else
 * we might damage the chip ! Use ath5k_channel_ok before calling this one.
 */
static int
ath5k_hw_rf5112_channel(struct ath5k_hw *ah,
		struct ieee80211_channel *channel)
{
	u32 data, data0, data1, data2;
	u16 c;

	data = data0 = data1 = data2 = 0;
	c = channel->center_freq;

	/* My guess based on code:
	 * 2GHz RF has 2 synth modes, one with a Local Oscillator
	 * at 2224Hz and one with a LO at 2192Hz. IF is 1520Hz
	 * (3040/2). data0 is used to set the PLL divider and data1
	 * selects synth mode. */
	if (c < 4800) {
		/* Channel 14 and all frequencies with 2Hz spacing
		 * below/above (non-standard channels) */
		if (!((c - 2224) % 5)) {
			/* Same as (c - 2224) / 5 */
			data0 = ((2 * (c - 704)) - 3040) / 10;
			data1 = 1;
		/* Channel 1 and all frequencies with 5Hz spacing
		 * below/above (standard channels without channel 14) */
		} else if (!((c - 2192) % 5)) {
			/* Same as (c - 2192) / 5 */
			data0 = ((2 * (c - 672)) - 3040) / 10;
			data1 = 0;
		} else
			return -EINVAL;

		data0 = ath5k_hw_bitswap((data0 << 2) & 0xff, 8);
	/* This is more complex, we have a single synthesizer with
	 * 4 reference clock settings (?) based on frequency spacing
	 * and set using data2. LO is at 4800Hz and data0 is again used
	 * to set some divider.
	 *
	 * NOTE: There is an old atheros presentation at Stanford
	 * that mentions a method called dual direct conversion
	 * with 1GHz sliding IF for RF5110. Maybe that's what we
	 * have here, or an updated version. */
	} else if ((c % 5) != 2 || c > 5435) {
		if (!(c % 20) && c >= 5120) {
			data0 = ath5k_hw_bitswap(((c - 4800) / 20 << 2), 8);
			data2 = ath5k_hw_bitswap(3, 2);
		} else if (!(c % 10)) {
			data0 = ath5k_hw_bitswap(((c - 4800) / 10 << 1), 8);
			data2 = ath5k_hw_bitswap(2, 2);
		} else if (!(c % 5)) {
			data0 = ath5k_hw_bitswap((c - 4800) / 5, 8);
			data2 = ath5k_hw_bitswap(1, 2);
		} else
			return -EINVAL;
	} else {
		data0 = ath5k_hw_bitswap((10 * (c - 2 - 4800)) / 25 + 1, 8);
		data2 = ath5k_hw_bitswap(0, 2);
	}

	data = (data0 << 4) | (data1 << 1) | (data2 << 2) | 0x1001;

	ath5k_hw_reg_write(ah, data & 0xff, AR5K_RF_BUFFER);
	ath5k_hw_reg_write(ah, (data >> 8) & 0x7f, AR5K_RF_BUFFER_CONTROL_5);

	return 0;
}

/**
 * ath5k_hw_rf2425_channel() - Set channel frequency on RF2425
 * @ah: The &struct ath5k_hw
 * @channel: The &struct ieee80211_channel
 *
 * AR2425/2417 have a different 2GHz RF so code changes
 * a little bit from RF5112.
 */
static int
ath5k_hw_rf2425_channel(struct ath5k_hw *ah,
		struct ieee80211_channel *channel)
{
	u32 data, data0, data2;
	u16 c;

	data = data0 = data2 = 0;
	c = channel->center_freq;

	if (c < 4800) {
		data0 = ath5k_hw_bitswap((c - 2272), 8);
		data2 = 0;
	/* ? 5GHz ? */
	} else if ((c % 5) != 2 || c > 5435) {
		if (!(c % 20) && c < 5120)
			data0 = ath5k_hw_bitswap(((c - 4800) / 20 << 2), 8);
		else if (!(c % 10))
			data0 = ath5k_hw_bitswap(((c - 4800) / 10 << 1), 8);
		else if (!(c % 5))
			data0 = ath5k_hw_bitswap((c - 4800) / 5, 8);
		else
			return -EINVAL;
		data2 = ath5k_hw_bitswap(1, 2);
	} else {
		data0 = ath5k_hw_bitswap((10 * (c - 2 - 4800)) / 25 + 1, 8);
		data2 = ath5k_hw_bitswap(0, 2);
	}

	data = (data0 << 4) | data2 << 2 | 0x1001;

	ath5k_hw_reg_write(ah, data & 0xff, AR5K_RF_BUFFER);
	ath5k_hw_reg_write(ah, (data >> 8) & 0x7f, AR5K_RF_BUFFER_CONTROL_5);

	return 0;
}

/**
 * ath5k_hw_channel() - Set a channel on the radio chip
 * @ah: The &struct ath5k_hw
 * @channel: The &struct ieee80211_channel
 *
 * This is the main function called to set a channel on the
 * radio chip based on the radio chip version.
 */
static int
ath5k_hw_channel(struct ath5k_hw *ah,
		struct ieee80211_channel *channel)
{
	int ret;
	/*
	 * Check bounds supported by the PHY (we don't care about regulatory
	 * restrictions at this point).
	 */
	if (!ath5k_channel_ok(ah, channel)) {
		ATH5K_ERR(ah,
			"channel frequency (%u MHz) out of supported "
			"band range\n",
			channel->center_freq);
			return -EINVAL;
	}

	/*
	 * Set the channel and wait
	 */
	switch (ah->ah_radio) {
	case AR5K_RF5110:
		ret = ath5k_hw_rf5110_channel(ah, channel);
		break;
	case AR5K_RF5111:
		ret = ath5k_hw_rf5111_channel(ah, channel);
		break;
	case AR5K_RF2317:
	case AR5K_RF2425:
		ret = ath5k_hw_rf2425_channel(ah, channel);
		break;
	default:
		ret = ath5k_hw_rf5112_channel(ah, channel);
		break;
	}

	if (ret)
		return ret;

	/* Set JAPAN setting for channel 14 */
	if (channel->center_freq == 2484) {
		AR5K_REG_ENABLE_BITS(ah, AR5K_PHY_CCKTXCTL,
				AR5K_PHY_CCKTXCTL_JAPAN);
	} else {
		AR5K_REG_ENABLE_BITS(ah, AR5K_PHY_CCKTXCTL,
				AR5K_PHY_CCKTXCTL_WORLD);
	}

	ah->ah_current_channel = channel;

	return 0;
}


/*****************\
  PHY calibration
\*****************/

/**
 * DOC: PHY Calibration routines
 *
 * Noise floor calibration: When we tell the hardware to
 * perform a noise floor calibration by setting the
 * AR5K_PHY_AGCCTL_NF bit on AR5K_PHY_AGCCTL, it will periodically
 * sample-and-hold the minimum noise level seen at the antennas.
 * This value is then stored in a ring buffer of recently measured
 * noise floor values so we have a moving window of the last few
 * samples. The median of the values in the history is then loaded
 * into the hardware for its own use for RSSI and CCA measurements.
 * This type of calibration doesn't interfere with traffic.
 *
 * AGC calibration: When we tell the hardware to perform
 * an AGC (Automatic Gain Control) calibration by setting the
 * AR5K_PHY_AGCCTL_CAL, hw disconnects the antennas and does
 * a calibration on the DC offsets of ADCs. During this period
 * rx/tx gets disabled so we have to deal with it on the driver
 * part.
 *
 * I/Q calibration: When we tell the hardware to perform
 * an I/Q calibration, it tries to correct I/Q imbalance and
 * fix QAM constellation by sampling data from rxed frames.
 * It doesn't interfere with traffic.
 *
 * For more infos on AGC and I/Q calibration check out patent doc
 * #03/094463.
 */

/**
 * ath5k_hw_read_measured_noise_floor() - Read measured NF from hw
 * @ah: The &struct ath5k_hw
 */
static s32
ath5k_hw_read_measured_noise_floor(struct ath5k_hw *ah)
{
	s32 val;

	val = ath5k_hw_reg_read(ah, AR5K_PHY_NF);
	return sign_extend32(AR5K_REG_MS(val, AR5K_PHY_NF_MINCCA_PWR), 8);
}

/**
 * ath5k_hw_init_nfcal_hist() - Initialize NF calibration history buffer
 * @ah: The &struct ath5k_hw
 */
void
ath5k_hw_init_nfcal_hist(struct ath5k_hw *ah)
{
	int i;

	ah->ah_nfcal_hist.index = 0;
	for (i = 0; i < ATH5K_NF_CAL_HIST_MAX; i++)
		ah->ah_nfcal_hist.nfval[i] = AR5K_TUNE_CCA_MAX_GOOD_VALUE;
}

/**
 * ath5k_hw_update_nfcal_hist() - Update NF calibration history buffer
 * @ah: The &struct ath5k_hw
 * @noise_floor: The NF we got from hw
 */
static void ath5k_hw_update_nfcal_hist(struct ath5k_hw *ah, s16 noise_floor)
{
	struct ath5k_nfcal_hist *hist = &ah->ah_nfcal_hist;
	hist->index = (hist->index + 1) & (ATH5K_NF_CAL_HIST_MAX - 1);
	hist->nfval[hist->index] = noise_floor;
}

/**
 * ath5k_hw_get_median_noise_floor() - Get median NF from history buffer
 * @ah: The &struct ath5k_hw
 */
static s16
ath5k_hw_get_median_noise_floor(struct ath5k_hw *ah)
{
	s16 sort[ATH5K_NF_CAL_HIST_MAX];
	s16 tmp;
	int i, j;

	memcpy(sort, ah->ah_nfcal_hist.nfval, sizeof(sort));
	for (i = 0; i < ATH5K_NF_CAL_HIST_MAX - 1; i++) {
		for (j = 1; j < ATH5K_NF_CAL_HIST_MAX - i; j++) {
			if (sort[j] > sort[j - 1]) {
				tmp = sort[j];
				sort[j] = sort[j - 1];
				sort[j - 1] = tmp;
			}
		}
	}
	for (i = 0; i < ATH5K_NF_CAL_HIST_MAX; i++) {
		ATH5K_DBG(ah, ATH5K_DEBUG_CALIBRATE,
			"cal %d:%d\n", i, sort[i]);
	}
	return sort[(ATH5K_NF_CAL_HIST_MAX - 1) / 2];
}

/**
 * ath5k_hw_update_noise_floor() - Update NF on hardware
 * @ah: The &struct ath5k_hw
 *
 * This is the main function we call to perform a NF calibration,
 * it reads NF from hardware, calculates the median and updates
 * NF on hw.
 */
void
ath5k_hw_update_noise_floor(struct ath5k_hw *ah)
{
	struct ath5k_eeprom_info *ee = &ah->ah_capabilities.cap_eeprom;
	u32 val;
	s16 nf, threshold;
	u8 ee_mode;

	/* keep last value if calibration hasn't completed */
	if (ath5k_hw_reg_read(ah, AR5K_PHY_AGCCTL) & AR5K_PHY_AGCCTL_NF) {
		ATH5K_DBG(ah, ATH5K_DEBUG_CALIBRATE,
			"NF did not complete in calibration window\n");

		return;
	}

	ah->ah_cal_mask |= AR5K_CALIBRATION_NF;

	ee_mode = ath5k_eeprom_mode_from_channel(ah->ah_current_channel);

	/* completed NF calibration, test threshold */
	nf = ath5k_hw_read_measured_noise_floor(ah);
	threshold = ee->ee_noise_floor_thr[ee_mode];

	if (nf > threshold) {
		ATH5K_DBG(ah, ATH5K_DEBUG_CALIBRATE,
			"noise floor failure detected; "
			"read %d, threshold %d\n",
			nf, threshold);

		nf = AR5K_TUNE_CCA_MAX_GOOD_VALUE;
	}

	ath5k_hw_update_nfcal_hist(ah, nf);
	nf = ath5k_hw_get_median_noise_floor(ah);

	/* load noise floor (in .5 dBm) so the hardware will use it */
	val = ath5k_hw_reg_read(ah, AR5K_PHY_NF) & ~AR5K_PHY_NF_M;
	val |= (nf * 2) & AR5K_PHY_NF_M;
	ath5k_hw_reg_write(ah, val, AR5K_PHY_NF);

	AR5K_REG_MASKED_BITS(ah, AR5K_PHY_AGCCTL, AR5K_PHY_AGCCTL_NF,
		~(AR5K_PHY_AGCCTL_NF_EN | AR5K_PHY_AGCCTL_NF_NOUPDATE));

	ath5k_hw_register_timeout(ah, AR5K_PHY_AGCCTL, AR5K_PHY_AGCCTL_NF,
		0, false);

	/*
	 * Load a high max CCA Power value (-50 dBm in .5 dBm units)
	 * so that we're not capped by the median we just loaded.
	 * This will be used as the initial value for the next noise
	 * floor calibration.
	 */
	val = (val & ~AR5K_PHY_NF_M) | ((-50 * 2) & AR5K_PHY_NF_M);
	ath5k_hw_reg_write(ah, val, AR5K_PHY_NF);
	AR5K_REG_ENABLE_BITS(ah, AR5K_PHY_AGCCTL,
		AR5K_PHY_AGCCTL_NF_EN |
		AR5K_PHY_AGCCTL_NF_NOUPDATE |
		AR5K_PHY_AGCCTL_NF);

	ah->ah_noise_floor = nf;

	ah->ah_cal_mask &= ~AR5K_CALIBRATION_NF;

	ATH5K_DBG(ah, ATH5K_DEBUG_CALIBRATE,
		"noise floor calibrated: %d\n", nf);
}

/**
 * ath5k_hw_rf5110_calibrate() - Perform a PHY calibration on RF5110
 * @ah: The &struct ath5k_hw
 * @channel: The &struct ieee80211_channel
 *
 * Do a complete PHY calibration (AGC + NF + I/Q) on RF5110
 */
static int
ath5k_hw_rf5110_calibrate(struct ath5k_hw *ah,
		struct ieee80211_channel *channel)
{
	u32 phy_sig, phy_agc, phy_sat, beacon;
	int ret;

	if (!(ah->ah_cal_mask & AR5K_CALIBRATION_FULL))
		return 0;

	/*
	 * Disable beacons and RX/TX queues, wait
	 */
	AR5K_REG_ENABLE_BITS(ah, AR5K_DIAG_SW_5210,
		AR5K_DIAG_SW_DIS_TX_5210 | AR5K_DIAG_SW_DIS_RX_5210);
	beacon = ath5k_hw_reg_read(ah, AR5K_BEACON_5210);
	ath5k_hw_reg_write(ah, beacon & ~AR5K_BEACON_ENABLE, AR5K_BEACON_5210);

	usleep_range(2000, 2500);

	/*
	 * Set the channel (with AGC turned off)
	 */
	AR5K_REG_ENABLE_BITS(ah, AR5K_PHY_AGC, AR5K_PHY_AGC_DISABLE);
	udelay(10);
	ret = ath5k_hw_channel(ah, channel);

	/*
	 * Activate PHY and wait
	 */
	ath5k_hw_reg_write(ah, AR5K_PHY_ACT_ENABLE, AR5K_PHY_ACT);
	usleep_range(1000, 1500);

	AR5K_REG_DISABLE_BITS(ah, AR5K_PHY_AGC, AR5K_PHY_AGC_DISABLE);

	if (ret)
		return ret;

	/*
	 * Calibrate the radio chip
	 */

	/* Remember normal state */
	phy_sig = ath5k_hw_reg_read(ah, AR5K_PHY_SIG);
	phy_agc = ath5k_hw_reg_read(ah, AR5K_PHY_AGCCOARSE);
	phy_sat = ath5k_hw_reg_read(ah, AR5K_PHY_ADCSAT);

	/* Update radio registers */
	ath5k_hw_reg_write(ah, (phy_sig & ~(AR5K_PHY_SIG_FIRPWR)) |
		AR5K_REG_SM(-1, AR5K_PHY_SIG_FIRPWR), AR5K_PHY_SIG);

	ath5k_hw_reg_write(ah, (phy_agc & ~(AR5K_PHY_AGCCOARSE_HI |
			AR5K_PHY_AGCCOARSE_LO)) |
		AR5K_REG_SM(-1, AR5K_PHY_AGCCOARSE_HI) |
		AR5K_REG_SM(-127, AR5K_PHY_AGCCOARSE_LO), AR5K_PHY_AGCCOARSE);

	ath5k_hw_reg_write(ah, (phy_sat & ~(AR5K_PHY_ADCSAT_ICNT |
			AR5K_PHY_ADCSAT_THR)) |
		AR5K_REG_SM(2, AR5K_PHY_ADCSAT_ICNT) |
		AR5K_REG_SM(12, AR5K_PHY_ADCSAT_THR), AR5K_PHY_ADCSAT);

	udelay(20);

	AR5K_REG_ENABLE_BITS(ah, AR5K_PHY_AGC, AR5K_PHY_AGC_DISABLE);
	udelay(10);
	ath5k_hw_reg_write(ah, AR5K_PHY_RFSTG_DISABLE, AR5K_PHY_RFSTG);
	AR5K_REG_DISABLE_BITS(ah, AR5K_PHY_AGC, AR5K_PHY_AGC_DISABLE);

	usleep_range(1000, 1500);

	/*
	 * Enable calibration and wait until completion
	 */
	AR5K_REG_ENABLE_BITS(ah, AR5K_PHY_AGCCTL, AR5K_PHY_AGCCTL_CAL);

	ret = ath5k_hw_register_timeout(ah, AR5K_PHY_AGCCTL,
			AR5K_PHY_AGCCTL_CAL, 0, false);

	/* Reset to normal state */
	ath5k_hw_reg_write(ah, phy_sig, AR5K_PHY_SIG);
	ath5k_hw_reg_write(ah, phy_agc, AR5K_PHY_AGCCOARSE);
	ath5k_hw_reg_write(ah, phy_sat, AR5K_PHY_ADCSAT);

	if (ret) {
		ATH5K_ERR(ah, "calibration timeout (%uMHz)\n",
				channel->center_freq);
		return ret;
	}

	/*
	 * Re-enable RX/TX and beacons
	 */
	AR5K_REG_DISABLE_BITS(ah, AR5K_DIAG_SW_5210,
		AR5K_DIAG_SW_DIS_TX_5210 | AR5K_DIAG_SW_DIS_RX_5210);
	ath5k_hw_reg_write(ah, beacon, AR5K_BEACON_5210);

	return 0;
}

/**
 * ath5k_hw_rf511x_iq_calibrate() - Perform I/Q calibration on RF5111 and newer
 * @ah: The &struct ath5k_hw
 */
static int
ath5k_hw_rf511x_iq_calibrate(struct ath5k_hw *ah)
{
	u32 i_pwr, q_pwr;
	s32 iq_corr, i_coff, i_coffd, q_coff, q_coffd;
	int i;

	/* Skip if I/Q calibration is not needed or if it's still running */
	if (!ah->ah_iq_cal_needed)
		return -EINVAL;
	else if (ath5k_hw_reg_read(ah, AR5K_PHY_IQ) & AR5K_PHY_IQ_RUN) {
		ATH5K_DBG_UNLIMIT(ah, ATH5K_DEBUG_CALIBRATE,
				"I/Q calibration still running");
		return -EBUSY;
	}

	/* Calibration has finished, get the results and re-run */

	/* Work around for empty results which can apparently happen on 5212:
	 * Read registers up to 10 times until we get both i_pr and q_pwr */
	for (i = 0; i <= 10; i++) {
		iq_corr = ath5k_hw_reg_read(ah, AR5K_PHY_IQRES_CAL_CORR);
		i_pwr = ath5k_hw_reg_read(ah, AR5K_PHY_IQRES_CAL_PWR_I);
		q_pwr = ath5k_hw_reg_read(ah, AR5K_PHY_IQRES_CAL_PWR_Q);
		ATH5K_DBG_UNLIMIT(ah, ATH5K_DEBUG_CALIBRATE,
			"iq_corr:%x i_pwr:%x q_pwr:%x", iq_corr, i_pwr, q_pwr);
		if (i_pwr && q_pwr)
			break;
	}

	i_coffd = ((i_pwr >> 1) + (q_pwr >> 1)) >> 7;

	if (ah->ah_version == AR5K_AR5211)
		q_coffd = q_pwr >> 6;
	else
		q_coffd = q_pwr >> 7;

	/* In case i_coffd became zero, cancel calibration
	 * not only it's too small, it'll also result a divide
	 * by zero later on. */
	if (i_coffd == 0 || q_coffd < 2)
		return -ECANCELED;

	/* Protect against loss of sign bits */

	i_coff = (-iq_corr) / i_coffd;
	i_coff = clamp(i_coff, -32, 31); /* signed 6 bit */

	if (ah->ah_version == AR5K_AR5211)
		q_coff = (i_pwr / q_coffd) - 64;
	else
		q_coff = (i_pwr / q_coffd) - 128;
	q_coff = clamp(q_coff, -16, 15); /* signed 5 bit */

	ATH5K_DBG_UNLIMIT(ah, ATH5K_DEBUG_CALIBRATE,
			"new I:%d Q:%d (i_coffd:%x q_coffd:%x)",
			i_coff, q_coff, i_coffd, q_coffd);

	/* Commit new I/Q values (set enable bit last to match HAL sources) */
	AR5K_REG_WRITE_BITS(ah, AR5K_PHY_IQ, AR5K_PHY_IQ_CORR_Q_I_COFF, i_coff);
	AR5K_REG_WRITE_BITS(ah, AR5K_PHY_IQ, AR5K_PHY_IQ_CORR_Q_Q_COFF, q_coff);
	AR5K_REG_ENABLE_BITS(ah, AR5K_PHY_IQ, AR5K_PHY_IQ_CORR_ENABLE);

	/* Re-enable calibration -if we don't we'll commit
	 * the same values again and again */
	AR5K_REG_WRITE_BITS(ah, AR5K_PHY_IQ,
			AR5K_PHY_IQ_CAL_NUM_LOG_MAX, 15);
	AR5K_REG_ENABLE_BITS(ah, AR5K_PHY_IQ, AR5K_PHY_IQ_RUN);

	return 0;
}

/**
 * ath5k_hw_phy_calibrate() - Perform a PHY calibration
 * @ah: The &struct ath5k_hw
 * @channel: The &struct ieee80211_channel
 *
 * The main function we call from above to perform
 * a short or full PHY calibration based on RF chip
 * and current channel
 */
int
ath5k_hw_phy_calibrate(struct ath5k_hw *ah,
		struct ieee80211_channel *channel)
{
	int ret;

	if (ah->ah_radio == AR5K_RF5110)
		return ath5k_hw_rf5110_calibrate(ah, channel);

	ret = ath5k_hw_rf511x_iq_calibrate(ah);
	if (ret) {
		ATH5K_DBG_UNLIMIT(ah, ATH5K_DEBUG_CALIBRATE,
			"No I/Q correction performed (%uMHz)\n",
			channel->center_freq);

		/* Happens all the time if there is not much
		 * traffic, consider it normal behaviour. */
		ret = 0;
	}

	/* On full calibration do an AGC calibration and
	 * request a PAPD probe for gainf calibration if
	 * needed */
	if (ah->ah_cal_mask & AR5K_CALIBRATION_FULL) {

		AR5K_REG_ENABLE_BITS(ah, AR5K_PHY_AGCCTL,
					AR5K_PHY_AGCCTL_CAL);

		ret = ath5k_hw_register_timeout(ah, AR5K_PHY_AGCCTL,
			AR5K_PHY_AGCCTL_CAL | AR5K_PHY_AGCCTL_NF,
			0, false);
		if (ret) {
			ATH5K_ERR(ah,
				"gain calibration timeout (%uMHz)\n",
				channel->center_freq);
		}

		if ((ah->ah_radio == AR5K_RF5111 ||
			ah->ah_radio == AR5K_RF5112)
			&& (channel->hw_value != AR5K_MODE_11B))
			ath5k_hw_request_rfgain_probe(ah);
	}

	/* Update noise floor
	 * XXX: Only do this after AGC calibration */
	if (!(ah->ah_cal_mask & AR5K_CALIBRATION_NF))
		ath5k_hw_update_noise_floor(ah);

	return ret;
}


/***************************\
* Spur mitigation functions *
\***************************/

/**
 * ath5k_hw_set_spur_mitigation_filter() - Configure SPUR filter
 * @ah: The &struct ath5k_hw
 * @channel: The &struct ieee80211_channel
 *
 * This function gets called during PHY initialization to
 * configure the spur filter for the given channel. Spur is noise
 * generated due to "reflection" effects, for more information on this
 * method check out patent US7643810
 */
static void
ath5k_hw_set_spur_mitigation_filter(struct ath5k_hw *ah,
				struct ieee80211_channel *channel)
{
	struct ath5k_eeprom_info *ee = &ah->ah_capabilities.cap_eeprom;
	u32 mag_mask[4] = {0, 0, 0, 0};
	u32 pilot_mask[2] = {0, 0};
	/* Note: fbin values are scaled up by 2 */
	u16 spur_chan_fbin, chan_fbin, symbol_width, spur_detection_window;
	s32 spur_delta_phase, spur_freq_sigma_delta;
	s32 spur_offset, num_symbols_x16;
	u8 num_symbol_offsets, i, freq_band;

	/* Convert current frequency to fbin value (the same way channels
	 * are stored on EEPROM, check out ath5k_eeprom_bin2freq) and scale
	 * up by 2 so we can compare it later */
	if (channel->band == IEEE80211_BAND_2GHZ) {
		chan_fbin = (channel->center_freq - 2300) * 10;
		freq_band = AR5K_EEPROM_BAND_2GHZ;
	} else {
		chan_fbin = (channel->center_freq - 4900) * 10;
		freq_band = AR5K_EEPROM_BAND_5GHZ;
	}

	/* Check if any spur_chan_fbin from EEPROM is
	 * within our current channel's spur detection range */
	spur_chan_fbin = AR5K_EEPROM_NO_SPUR;
	spur_detection_window = AR5K_SPUR_CHAN_WIDTH;
	/* XXX: Half/Quarter channels ?*/
	if (ah->ah_bwmode == AR5K_BWMODE_40MHZ)
		spur_detection_window *= 2;

	for (i = 0; i < AR5K_EEPROM_N_SPUR_CHANS; i++) {
		spur_chan_fbin = ee->ee_spur_chans[i][freq_band];

		/* Note: mask cleans AR5K_EEPROM_NO_SPUR flag
		 * so it's zero if we got nothing from EEPROM */
		if (spur_chan_fbin == AR5K_EEPROM_NO_SPUR) {
			spur_chan_fbin &= AR5K_EEPROM_SPUR_CHAN_MASK;
			break;
		}

		if ((chan_fbin - spur_detection_window <=
		(spur_chan_fbin & AR5K_EEPROM_SPUR_CHAN_MASK)) &&
		(chan_fbin + spur_detection_window >=
		(spur_chan_fbin & AR5K_EEPROM_SPUR_CHAN_MASK))) {
			spur_chan_fbin &= AR5K_EEPROM_SPUR_CHAN_MASK;
			break;
		}
	}

	/* We need to enable spur filter for this channel */
	if (spur_chan_fbin) {
		spur_offset = spur_chan_fbin - chan_fbin;
		/*
		 * Calculate deltas:
		 * spur_freq_sigma_delta -> spur_offset / sample_freq << 21
		 * spur_delta_phase -> spur_offset / chip_freq << 11
		 * Note: Both values have 100Hz resolution
		 */
		switch (ah->ah_bwmode) {
		case AR5K_BWMODE_40MHZ:
			/* Both sample_freq and chip_freq are 80MHz */
			spur_delta_phase = (spur_offset << 16) / 25;
			spur_freq_sigma_delta = (spur_delta_phase >> 10);
			symbol_width = AR5K_SPUR_SYMBOL_WIDTH_BASE_100Hz * 2;
			break;
		case AR5K_BWMODE_10MHZ:
			/* Both sample_freq and chip_freq are 20MHz (?) */
			spur_delta_phase = (spur_offset << 18) / 25;
			spur_freq_sigma_delta = (spur_delta_phase >> 10);
			symbol_width = AR5K_SPUR_SYMBOL_WIDTH_BASE_100Hz / 2;
		case AR5K_BWMODE_5MHZ:
			/* Both sample_freq and chip_freq are 10MHz (?) */
			spur_delta_phase = (spur_offset << 19) / 25;
			spur_freq_sigma_delta = (spur_delta_phase >> 10);
			symbol_width = AR5K_SPUR_SYMBOL_WIDTH_BASE_100Hz / 4;
		default:
			if (channel->band == IEEE80211_BAND_5GHZ) {
				/* Both sample_freq and chip_freq are 40MHz */
				spur_delta_phase = (spur_offset << 17) / 25;
				spur_freq_sigma_delta =
						(spur_delta_phase >> 10);
				symbol_width =
					AR5K_SPUR_SYMBOL_WIDTH_BASE_100Hz;
			} else {
				/* sample_freq -> 40MHz chip_freq -> 44MHz
				 * (for b compatibility) */
				spur_delta_phase = (spur_offset << 17) / 25;
				spur_freq_sigma_delta =
						(spur_offset << 8) / 55;
				symbol_width =
					AR5K_SPUR_SYMBOL_WIDTH_BASE_100Hz;
			}
			break;
		}

		/* Calculate pilot and magnitude masks */

		/* Scale up spur_offset by 1000 to switch to 100HZ resolution
		 * and divide by symbol_width to find how many symbols we have
		 * Note: number of symbols is scaled up by 16 */
		num_symbols_x16 = ((spur_offset * 1000) << 4) / symbol_width;

		/* Spur is on a symbol if num_symbols_x16 % 16 is zero */
		if (!(num_symbols_x16 & 0xF))
			/* _X_ */
			num_symbol_offsets = 3;
		else
			/* _xx_ */
			num_symbol_offsets = 4;

		for (i = 0; i < num_symbol_offsets; i++) {

			/* Calculate pilot mask */
			s32 curr_sym_off =
				(num_symbols_x16 / 16) + i + 25;

			/* Pilot magnitude mask seems to be a way to
			 * declare the boundaries for our detection
			 * window or something, it's 2 for the middle
			 * value(s) where the symbol is expected to be
			 * and 1 on the boundary values */
			u8 plt_mag_map =
				(i == 0 || i == (num_symbol_offsets - 1))
								? 1 : 2;

			if (curr_sym_off >= 0 && curr_sym_off <= 32) {
				if (curr_sym_off <= 25)
					pilot_mask[0] |= 1 << curr_sym_off;
				else if (curr_sym_off >= 27)
					pilot_mask[0] |= 1 << (curr_sym_off - 1);
			} else if (curr_sym_off >= 33 && curr_sym_off <= 52)
				pilot_mask[1] |= 1 << (curr_sym_off - 33);

			/* Calculate magnitude mask (for viterbi decoder) */
			if (curr_sym_off >= -1 && curr_sym_off <= 14)
				mag_mask[0] |=
					plt_mag_map << (curr_sym_off + 1) * 2;
			else if (curr_sym_off >= 15 && curr_sym_off <= 30)
				mag_mask[1] |=
					plt_mag_map << (curr_sym_off - 15) * 2;
			else if (curr_sym_off >= 31 && curr_sym_off <= 46)
				mag_mask[2] |=
					plt_mag_map << (curr_sym_off - 31) * 2;
			else if (curr_sym_off >= 47 && curr_sym_off <= 53)
				mag_mask[3] |=
					plt_mag_map << (curr_sym_off - 47) * 2;

		}

		/* Write settings on hw to enable spur filter */
		AR5K_REG_WRITE_BITS(ah, AR5K_PHY_BIN_MASK_CTL,
					AR5K_PHY_BIN_MASK_CTL_RATE, 0xff);
		/* XXX: Self correlator also ? */
		AR5K_REG_ENABLE_BITS(ah, AR5K_PHY_IQ,
					AR5K_PHY_IQ_PILOT_MASK_EN |
					AR5K_PHY_IQ_CHAN_MASK_EN |
					AR5K_PHY_IQ_SPUR_FILT_EN);

		/* Set delta phase and freq sigma delta */
		ath5k_hw_reg_write(ah,
				AR5K_REG_SM(spur_delta_phase,
					AR5K_PHY_TIMING_11_SPUR_DELTA_PHASE) |
				AR5K_REG_SM(spur_freq_sigma_delta,
				AR5K_PHY_TIMING_11_SPUR_FREQ_SD) |
				AR5K_PHY_TIMING_11_USE_SPUR_IN_AGC,
				AR5K_PHY_TIMING_11);

		/* Write pilot masks */
		ath5k_hw_reg_write(ah, pilot_mask[0], AR5K_PHY_TIMING_7);
		AR5K_REG_WRITE_BITS(ah, AR5K_PHY_TIMING_8,
					AR5K_PHY_TIMING_8_PILOT_MASK_2,
					pilot_mask[1]);

		ath5k_hw_reg_write(ah, pilot_mask[0], AR5K_PHY_TIMING_9);
		AR5K_REG_WRITE_BITS(ah, AR5K_PHY_TIMING_10,
					AR5K_PHY_TIMING_10_PILOT_MASK_2,
					pilot_mask[1]);

		/* Write magnitude masks */
		ath5k_hw_reg_write(ah, mag_mask[0], AR5K_PHY_BIN_MASK_1);
		ath5k_hw_reg_write(ah, mag_mask[1], AR5K_PHY_BIN_MASK_2);
		ath5k_hw_reg_write(ah, mag_mask[2], AR5K_PHY_BIN_MASK_3);
		AR5K_REG_WRITE_BITS(ah, AR5K_PHY_BIN_MASK_CTL,
					AR5K_PHY_BIN_MASK_CTL_MASK_4,
					mag_mask[3]);

		ath5k_hw_reg_write(ah, mag_mask[0], AR5K_PHY_BIN_MASK2_1);
		ath5k_hw_reg_write(ah, mag_mask[1], AR5K_PHY_BIN_MASK2_2);
		ath5k_hw_reg_write(ah, mag_mask[2], AR5K_PHY_BIN_MASK2_3);
		AR5K_REG_WRITE_BITS(ah, AR5K_PHY_BIN_MASK2_4,
					AR5K_PHY_BIN_MASK2_4_MASK_4,
					mag_mask[3]);

	} else if (ath5k_hw_reg_read(ah, AR5K_PHY_IQ) &
	AR5K_PHY_IQ_SPUR_FILT_EN) {
		/* Clean up spur mitigation settings and disable filter */
		AR5K_REG_WRITE_BITS(ah, AR5K_PHY_BIN_MASK_CTL,
					AR5K_PHY_BIN_MASK_CTL_RATE, 0);
		AR5K_REG_DISABLE_BITS(ah, AR5K_PHY_IQ,
					AR5K_PHY_IQ_PILOT_MASK_EN |
					AR5K_PHY_IQ_CHAN_MASK_EN |
					AR5K_PHY_IQ_SPUR_FILT_EN);
		ath5k_hw_reg_write(ah, 0, AR5K_PHY_TIMING_11);

		/* Clear pilot masks */
		ath5k_hw_reg_write(ah, 0, AR5K_PHY_TIMING_7);
		AR5K_REG_WRITE_BITS(ah, AR5K_PHY_TIMING_8,
					AR5K_PHY_TIMING_8_PILOT_MASK_2,
					0);

		ath5k_hw_reg_write(ah, 0, AR5K_PHY_TIMING_9);
		AR5K_REG_WRITE_BITS(ah, AR5K_PHY_TIMING_10,
					AR5K_PHY_TIMING_10_PILOT_MASK_2,
					0);

		/* Clear magnitude masks */
		ath5k_hw_reg_write(ah, 0, AR5K_PHY_BIN_MASK_1);
		ath5k_hw_reg_write(ah, 0, AR5K_PHY_BIN_MASK_2);
		ath5k_hw_reg_write(ah, 0, AR5K_PHY_BIN_MASK_3);
		AR5K_REG_WRITE_BITS(ah, AR5K_PHY_BIN_MASK_CTL,
					AR5K_PHY_BIN_MASK_CTL_MASK_4,
					0);

		ath5k_hw_reg_write(ah, 0, AR5K_PHY_BIN_MASK2_1);
		ath5k_hw_reg_write(ah, 0, AR5K_PHY_BIN_MASK2_2);
		ath5k_hw_reg_write(ah, 0, AR5K_PHY_BIN_MASK2_3);
		AR5K_REG_WRITE_BITS(ah, AR5K_PHY_BIN_MASK2_4,
					AR5K_PHY_BIN_MASK2_4_MASK_4,
					0);
	}
}


/*****************\
* Antenna control *
\*****************/

/**
 * DOC: Antenna control
 *
 * Hw supports up to 14 antennas ! I haven't found any card that implements
 * that. The maximum number of antennas I've seen is up to 4 (2 for 2GHz and 2
 * for 5GHz). Antenna 1 (MAIN) should be omnidirectional, 2 (AUX)
 * omnidirectional or sectorial and antennas 3-14 sectorial (or directional).
 *
 * We can have a single antenna for RX and multiple antennas for TX.
 * RX antenna is our "default" antenna (usually antenna 1) set on
 * DEFAULT_ANTENNA register and TX antenna is set on each TX control descriptor
 * (0 for automatic selection, 1 - 14 antenna number).
 *
 * We can let hw do all the work doing fast antenna diversity for both
 * tx and rx or we can do things manually. Here are the options we have
 * (all are bits of STA_ID1 register):
 *
 * AR5K_STA_ID1_DEFAULT_ANTENNA -> When 0 is set as the TX antenna on TX
 * control descriptor, use the default antenna to transmit or else use the last
 * antenna on which we received an ACK.
 *
 * AR5K_STA_ID1_DESC_ANTENNA -> Update default antenna after each TX frame to
 * the antenna on which we got the ACK for that frame.
 *
 * AR5K_STA_ID1_RTS_DEF_ANTENNA -> Use default antenna for RTS or else use the
 * one on the TX descriptor.
 *
 * AR5K_STA_ID1_SELFGEN_DEF_ANT -> Use default antenna for self generated frames
 * (ACKs etc), or else use current antenna (the one we just used for TX).
 *
 * Using the above we support the following scenarios:
 *
 * AR5K_ANTMODE_DEFAULT -> Hw handles antenna diversity etc automatically
 *
 * AR5K_ANTMODE_FIXED_A	-> Only antenna A (MAIN) is present
 *
 * AR5K_ANTMODE_FIXED_B	-> Only antenna B (AUX) is present
 *
 * AR5K_ANTMODE_SINGLE_AP -> Sta locked on a single ap
 *
 * AR5K_ANTMODE_SECTOR_AP -> AP with tx antenna set on tx desc
 *
 * AR5K_ANTMODE_SECTOR_STA -> STA with tx antenna set on tx desc
 *
 * AR5K_ANTMODE_DEBUG Debug mode -A -> Rx, B-> Tx-
 *
 * Also note that when setting antenna to F on tx descriptor card inverts
 * current tx antenna.
 */

/**
 * ath5k_hw_set_def_antenna() - Set default rx antenna on AR5211/5212 and newer
 * @ah: The &struct ath5k_hw
 * @ant: Antenna number
 */
static void
ath5k_hw_set_def_antenna(struct ath5k_hw *ah, u8 ant)
{
	if (ah->ah_version != AR5K_AR5210)
		ath5k_hw_reg_write(ah, ant & 0x7, AR5K_DEFAULT_ANTENNA);
}

/**
 * ath5k_hw_set_fast_div() -  Enable/disable fast rx antenna diversity
 * @ah: The &struct ath5k_hw
 * @ee_mode: One of enum ath5k_driver_mode
 * @enable: True to enable, false to disable
 */
static void
ath5k_hw_set_fast_div(struct ath5k_hw *ah, u8 ee_mode, bool enable)
{
	switch (ee_mode) {
	case AR5K_EEPROM_MODE_11G:
		/* XXX: This is set to
		 * disabled on initvals !!! */
	case AR5K_EEPROM_MODE_11A:
		if (enable)
			AR5K_REG_DISABLE_BITS(ah, AR5K_PHY_AGCCTL,
					AR5K_PHY_AGCCTL_OFDM_DIV_DIS);
		else
			AR5K_REG_ENABLE_BITS(ah, AR5K_PHY_AGCCTL,
					AR5K_PHY_AGCCTL_OFDM_DIV_DIS);
		break;
	case AR5K_EEPROM_MODE_11B:
		AR5K_REG_ENABLE_BITS(ah, AR5K_PHY_AGCCTL,
					AR5K_PHY_AGCCTL_OFDM_DIV_DIS);
		break;
	default:
		return;
	}

	if (enable) {
		AR5K_REG_WRITE_BITS(ah, AR5K_PHY_RESTART,
				AR5K_PHY_RESTART_DIV_GC, 4);

		AR5K_REG_ENABLE_BITS(ah, AR5K_PHY_FAST_ANT_DIV,
					AR5K_PHY_FAST_ANT_DIV_EN);
	} else {
		AR5K_REG_WRITE_BITS(ah, AR5K_PHY_RESTART,
				AR5K_PHY_RESTART_DIV_GC, 0);

		AR5K_REG_DISABLE_BITS(ah, AR5K_PHY_FAST_ANT_DIV,
					AR5K_PHY_FAST_ANT_DIV_EN);
	}
}

/**
 * ath5k_hw_set_antenna_switch() - Set up antenna switch table
 * @ah: The &struct ath5k_hw
 * @ee_mode: One of enum ath5k_driver_mode
 *
 * Switch table comes from EEPROM and includes information on controlling
 * the 2 antenna RX attenuators
 */
void
ath5k_hw_set_antenna_switch(struct ath5k_hw *ah, u8 ee_mode)
{
	u8 ant0, ant1;

	/*
	 * In case a fixed antenna was set as default
	 * use the same switch table twice.
	 */
	if (ah->ah_ant_mode == AR5K_ANTMODE_FIXED_A)
		ant0 = ant1 = AR5K_ANT_SWTABLE_A;
	else if (ah->ah_ant_mode == AR5K_ANTMODE_FIXED_B)
		ant0 = ant1 = AR5K_ANT_SWTABLE_B;
	else {
		ant0 = AR5K_ANT_SWTABLE_A;
		ant1 = AR5K_ANT_SWTABLE_B;
	}

	/* Set antenna idle switch table */
	AR5K_REG_WRITE_BITS(ah, AR5K_PHY_ANT_CTL,
			AR5K_PHY_ANT_CTL_SWTABLE_IDLE,
			(ah->ah_ant_ctl[ee_mode][AR5K_ANT_CTL] |
			AR5K_PHY_ANT_CTL_TXRX_EN));

	/* Set antenna switch tables */
	ath5k_hw_reg_write(ah, ah->ah_ant_ctl[ee_mode][ant0],
		AR5K_PHY_ANT_SWITCH_TABLE_0);
	ath5k_hw_reg_write(ah, ah->ah_ant_ctl[ee_mode][ant1],
		AR5K_PHY_ANT_SWITCH_TABLE_1);
}

/**
 * ath5k_hw_set_antenna_mode() -  Set antenna operating mode
 * @ah: The &struct ath5k_hw
 * @ant_mode: One of enum ath5k_ant_mode
 */
void
ath5k_hw_set_antenna_mode(struct ath5k_hw *ah, u8 ant_mode)
{
	struct ieee80211_channel *channel = ah->ah_current_channel;
	bool use_def_for_tx, update_def_on_tx, use_def_for_rts, fast_div;
	bool use_def_for_sg;
	int ee_mode;
	u8 def_ant, tx_ant;
	u32 sta_id1 = 0;

	/* if channel is not initialized yet we can't set the antennas
	 * so just store the mode. it will be set on the next reset */
	if (channel == NULL) {
		ah->ah_ant_mode = ant_mode;
		return;
	}

	def_ant = ah->ah_def_ant;

	ee_mode = ath5k_eeprom_mode_from_channel(channel);
	if (ee_mode < 0) {
		ATH5K_ERR(ah,
			"invalid channel: %d\n", channel->center_freq);
		return;
	}

	switch (ant_mode) {
	case AR5K_ANTMODE_DEFAULT:
		tx_ant = 0;
		use_def_for_tx = false;
		update_def_on_tx = false;
		use_def_for_rts = false;
		use_def_for_sg = false;
		fast_div = true;
		break;
	case AR5K_ANTMODE_FIXED_A:
		def_ant = 1;
		tx_ant = 1;
		use_def_for_tx = true;
		update_def_on_tx = false;
		use_def_for_rts = true;
		use_def_for_sg = true;
		fast_div = false;
		break;
	case AR5K_ANTMODE_FIXED_B:
		def_ant = 2;
		tx_ant = 2;
		use_def_for_tx = true;
		update_def_on_tx = false;
		use_def_for_rts = true;
		use_def_for_sg = true;
		fast_div = false;
		break;
	case AR5K_ANTMODE_SINGLE_AP:
		def_ant = 1;	/* updated on tx */
		tx_ant = 0;
		use_def_for_tx = true;
		update_def_on_tx = true;
		use_def_for_rts = true;
		use_def_for_sg = true;
		fast_div = true;
		break;
	case AR5K_ANTMODE_SECTOR_AP:
		tx_ant = 1;	/* variable */
		use_def_for_tx = false;
		update_def_on_tx = false;
		use_def_for_rts = true;
		use_def_for_sg = false;
		fast_div = false;
		break;
	case AR5K_ANTMODE_SECTOR_STA:
		tx_ant = 1;	/* variable */
		use_def_for_tx = true;
		update_def_on_tx = false;
		use_def_for_rts = true;
		use_def_for_sg = false;
		fast_div = true;
		break;
	case AR5K_ANTMODE_DEBUG:
		def_ant = 1;
		tx_ant = 2;
		use_def_for_tx = false;
		update_def_on_tx = false;
		use_def_for_rts = false;
		use_def_for_sg = false;
		fast_div = false;
		break;
	default:
		return;
	}

	ah->ah_tx_ant = tx_ant;
	ah->ah_ant_mode = ant_mode;
	ah->ah_def_ant = def_ant;

	sta_id1 |= use_def_for_tx ? AR5K_STA_ID1_DEFAULT_ANTENNA : 0;
	sta_id1 |= update_def_on_tx ? AR5K_STA_ID1_DESC_ANTENNA : 0;
	sta_id1 |= use_def_for_rts ? AR5K_STA_ID1_RTS_DEF_ANTENNA : 0;
	sta_id1 |= use_def_for_sg ? AR5K_STA_ID1_SELFGEN_DEF_ANT : 0;

	AR5K_REG_DISABLE_BITS(ah, AR5K_STA_ID1, AR5K_STA_ID1_ANTENNA_SETTINGS);

	if (sta_id1)
		AR5K_REG_ENABLE_BITS(ah, AR5K_STA_ID1, sta_id1);

	ath5k_hw_set_antenna_switch(ah, ee_mode);
	/* Note: set diversity before default antenna
	 * because it won't work correctly */
	ath5k_hw_set_fast_div(ah, ee_mode, fast_div);
	ath5k_hw_set_def_antenna(ah, def_ant);
}


/****************\
* TX power setup *
\****************/

/*
 * Helper functions
 */

/**
 * ath5k_get_interpolated_value() - Get interpolated Y val between two points
 * @target: X value of the middle point
 * @x_left: X value of the left point
 * @x_right: X value of the right point
 * @y_left: Y value of the left point
 * @y_right: Y value of the right point
 */
static s16
ath5k_get_interpolated_value(s16 target, s16 x_left, s16 x_right,
					s16 y_left, s16 y_right)
{
	s16 ratio, result;

	/* Avoid divide by zero and skip interpolation
	 * if we have the same point */
	if ((x_left == x_right) || (y_left == y_right))
		return y_left;

	/*
	 * Since we use ints and not fps, we need to scale up in
	 * order to get a sane ratio value (or else we 'll eg. get
	 * always 1 instead of 1.25, 1.75 etc). We scale up by 100
	 * to have some accuracy both for 0.5 and 0.25 steps.
	 */
	ratio = ((100 * y_right - 100 * y_left) / (x_right - x_left));

	/* Now scale down to be in range */
	result = y_left + (ratio * (target - x_left) / 100);

	return result;
}

/**
 * ath5k_get_linear_pcdac_min() - Find vertical boundary (min pwr) for the
 * linear PCDAC curve
 * @stepL: Left array with y values (pcdac steps)
 * @stepR: Right array with y values (pcdac steps)
 * @pwrL: Left array with x values (power steps)
 * @pwrR: Right array with x values (power steps)
 *
 * Since we have the top of the curve and we draw the line below
 * until we reach 1 (1 pcdac step) we need to know which point
 * (x value) that is so that we don't go below x axis and have negative
 * pcdac values when creating the curve, or fill the table with zeros.
 */
static s16
ath5k_get_linear_pcdac_min(const u8 *stepL, const u8 *stepR,
				const s16 *pwrL, const s16 *pwrR)
{
	s8 tmp;
	s16 min_pwrL, min_pwrR;
	s16 pwr_i;

	/* Some vendors write the same pcdac value twice !!! */
	if (stepL[0] == stepL[1] || stepR[0] == stepR[1])
		return max(pwrL[0], pwrR[0]);

	if (pwrL[0] == pwrL[1])
		min_pwrL = pwrL[0];
	else {
		pwr_i = pwrL[0];
		do {
			pwr_i--;
			tmp = (s8) ath5k_get_interpolated_value(pwr_i,
							pwrL[0], pwrL[1],
							stepL[0], stepL[1]);
		} while (tmp > 1);

		min_pwrL = pwr_i;
	}

	if (pwrR[0] == pwrR[1])
		min_pwrR = pwrR[0];
	else {
		pwr_i = pwrR[0];
		do {
			pwr_i--;
			tmp = (s8) ath5k_get_interpolated_value(pwr_i,
							pwrR[0], pwrR[1],
							stepR[0], stepR[1]);
		} while (tmp > 1);

		min_pwrR = pwr_i;
	}

	/* Keep the right boundary so that it works for both curves */
	return max(min_pwrL, min_pwrR);
}

/**
 * ath5k_create_power_curve() - Create a Power to PDADC or PCDAC curve
 * @pmin: Minimum power value (xmin)
 * @pmax: Maximum power value (xmax)
 * @pwr: Array of power steps (x values)
 * @vpd: Array of matching PCDAC/PDADC steps (y values)
 * @num_points: Number of provided points
 * @vpd_table: Array to fill with the full PCDAC/PDADC values (y values)
 * @type: One of enum ath5k_powertable_type (eeprom.h)
 *
 * Interpolate (pwr,vpd) points to create a Power to PDADC or a
 * Power to PCDAC curve.
 *
 * Each curve has power on x axis (in 0.5dB units) and PCDAC/PDADC
 * steps (offsets) on y axis. Power can go up to 31.5dB and max
 * PCDAC/PDADC step for each curve is 64 but we can write more than
 * one curves on hw so we can go up to 128 (which is the max step we
 * can write on the final table).
 *
 * We write y values (PCDAC/PDADC steps) on hw.
 */
static void
ath5k_create_power_curve(s16 pmin, s16 pmax,
			const s16 *pwr, const u8 *vpd,
			u8 num_points,
			u8 *vpd_table, u8 type)
{
	u8 idx[2] = { 0, 1 };
	s16 pwr_i = 2 * pmin;
	int i;

	if (num_points < 2)
		return;

	/* We want the whole line, so adjust boundaries
	 * to cover the entire power range. Note that
	 * power values are already 0.25dB so no need
	 * to multiply pwr_i by 2 */
	if (type == AR5K_PWRTABLE_LINEAR_PCDAC) {
		pwr_i = pmin;
		pmin = 0;
		pmax = 63;
	}

	/* Find surrounding turning points (TPs)
	 * and interpolate between them */
	for (i = 0; (i <= (u16) (pmax - pmin)) &&
	(i < AR5K_EEPROM_POWER_TABLE_SIZE); i++) {

		/* We passed the right TP, move to the next set of TPs
		 * if we pass the last TP, extrapolate above using the last
		 * two TPs for ratio */
		if ((pwr_i > pwr[idx[1]]) && (idx[1] < num_points - 1)) {
			idx[0]++;
			idx[1]++;
		}

		vpd_table[i] = (u8) ath5k_get_interpolated_value(pwr_i,
						pwr[idx[0]], pwr[idx[1]],
						vpd[idx[0]], vpd[idx[1]]);

		/* Increase by 0.5dB
		 * (0.25 dB units) */
		pwr_i += 2;
	}
}

/**
 * ath5k_get_chan_pcal_surrounding_piers() - Get surrounding calibration piers
 * for a given channel.
 * @ah: The &struct ath5k_hw
 * @channel: The &struct ieee80211_channel
 * @pcinfo_l: The &struct ath5k_chan_pcal_info to put the left cal. pier
 * @pcinfo_r: The &struct ath5k_chan_pcal_info to put the right cal. pier
 *
 * Get the surrounding per-channel power calibration piers
 * for a given frequency so that we can interpolate between
 * them and come up with an appropriate dataset for our current
 * channel.
 */
static void
ath5k_get_chan_pcal_surrounding_piers(struct ath5k_hw *ah,
			struct ieee80211_channel *channel,
			struct ath5k_chan_pcal_info **pcinfo_l,
			struct ath5k_chan_pcal_info **pcinfo_r)
{
	struct ath5k_eeprom_info *ee = &ah->ah_capabilities.cap_eeprom;
	struct ath5k_chan_pcal_info *pcinfo;
	u8 idx_l, idx_r;
	u8 mode, max, i;
	u32 target = channel->center_freq;

	idx_l = 0;
	idx_r = 0;

	switch (channel->hw_value) {
	case AR5K_EEPROM_MODE_11A:
		pcinfo = ee->ee_pwr_cal_a;
		mode = AR5K_EEPROM_MODE_11A;
		break;
	case AR5K_EEPROM_MODE_11B:
		pcinfo = ee->ee_pwr_cal_b;
		mode = AR5K_EEPROM_MODE_11B;
		break;
	case AR5K_EEPROM_MODE_11G:
	default:
		pcinfo = ee->ee_pwr_cal_g;
		mode = AR5K_EEPROM_MODE_11G;
		break;
	}
	max = ee->ee_n_piers[mode] - 1;

	/* Frequency is below our calibrated
	 * range. Use the lowest power curve
	 * we have */
	if (target < pcinfo[0].freq) {
		idx_l = idx_r = 0;
		goto done;
	}

	/* Frequency is above our calibrated
	 * range. Use the highest power curve
	 * we have */
	if (target > pcinfo[max].freq) {
		idx_l = idx_r = max;
		goto done;
	}

	/* Frequency is inside our calibrated
	 * channel range. Pick the surrounding
	 * calibration piers so that we can
	 * interpolate */
	for (i = 0; i <= max; i++) {

		/* Frequency matches one of our calibration
		 * piers, no need to interpolate, just use
		 * that calibration pier */
		if (pcinfo[i].freq == target) {
			idx_l = idx_r = i;
			goto done;
		}

		/* We found a calibration pier that's above
		 * frequency, use this pier and the previous
		 * one to interpolate */
		if (target < pcinfo[i].freq) {
			idx_r = i;
			idx_l = idx_r - 1;
			goto done;
		}
	}

done:
	*pcinfo_l = &pcinfo[idx_l];
	*pcinfo_r = &pcinfo[idx_r];
}

/**
 * ath5k_get_rate_pcal_data() - Get the interpolated per-rate power
 * calibration data
 * @ah: The &struct ath5k_hw *ah,
 * @channel: The &struct ieee80211_channel
 * @rates: The &struct ath5k_rate_pcal_info to fill
 *
 * Get the surrounding per-rate power calibration data
 * for a given frequency and interpolate between power
 * values to set max target power supported by hw for
 * each rate on this frequency.
 */
static void
ath5k_get_rate_pcal_data(struct ath5k_hw *ah,
			struct ieee80211_channel *channel,
			struct ath5k_rate_pcal_info *rates)
{
	struct ath5k_eeprom_info *ee = &ah->ah_capabilities.cap_eeprom;
	struct ath5k_rate_pcal_info *rpinfo;
	u8 idx_l, idx_r;
	u8 mode, max, i;
	u32 target = channel->center_freq;

	idx_l = 0;
	idx_r = 0;

	switch (channel->hw_value) {
	case AR5K_MODE_11A:
		rpinfo = ee->ee_rate_tpwr_a;
		mode = AR5K_EEPROM_MODE_11A;
		break;
	case AR5K_MODE_11B:
		rpinfo = ee->ee_rate_tpwr_b;
		mode = AR5K_EEPROM_MODE_11B;
		break;
	case AR5K_MODE_11G:
	default:
		rpinfo = ee->ee_rate_tpwr_g;
		mode = AR5K_EEPROM_MODE_11G;
		break;
	}
	max = ee->ee_rate_target_pwr_num[mode] - 1;

	/* Get the surrounding calibration
	 * piers - same as above */
	if (target < rpinfo[0].freq) {
		idx_l = idx_r = 0;
		goto done;
	}

	if (target > rpinfo[max].freq) {
		idx_l = idx_r = max;
		goto done;
	}

	for (i = 0; i <= max; i++) {

		if (rpinfo[i].freq == target) {
			idx_l = idx_r = i;
			goto done;
		}

		if (target < rpinfo[i].freq) {
			idx_r = i;
			idx_l = idx_r - 1;
			goto done;
		}
	}

done:
	/* Now interpolate power value, based on the frequency */
	rates->freq = target;

	rates->target_power_6to24 =
		ath5k_get_interpolated_value(target, rpinfo[idx_l].freq,
					rpinfo[idx_r].freq,
					rpinfo[idx_l].target_power_6to24,
					rpinfo[idx_r].target_power_6to24);

	rates->target_power_36 =
		ath5k_get_interpolated_value(target, rpinfo[idx_l].freq,
					rpinfo[idx_r].freq,
					rpinfo[idx_l].target_power_36,
					rpinfo[idx_r].target_power_36);

	rates->target_power_48 =
		ath5k_get_interpolated_value(target, rpinfo[idx_l].freq,
					rpinfo[idx_r].freq,
					rpinfo[idx_l].target_power_48,
					rpinfo[idx_r].target_power_48);

	rates->target_power_54 =
		ath5k_get_interpolated_value(target, rpinfo[idx_l].freq,
					rpinfo[idx_r].freq,
					rpinfo[idx_l].target_power_54,
					rpinfo[idx_r].target_power_54);
}

/**
 * ath5k_get_max_ctl_power() - Get max edge power for a given frequency
 * @ah: the &struct ath5k_hw
 * @channel: The &struct ieee80211_channel
 *
 * Get the max edge power for this channel if
 * we have such data from EEPROM's Conformance Test
 * Limits (CTL), and limit max power if needed.
 */
static void
ath5k_get_max_ctl_power(struct ath5k_hw *ah,
			struct ieee80211_channel *channel)
{
	struct ath_regulatory *regulatory = ath5k_hw_regulatory(ah);
	struct ath5k_eeprom_info *ee = &ah->ah_capabilities.cap_eeprom;
	struct ath5k_edge_power *rep = ee->ee_ctl_pwr;
	u8 *ctl_val = ee->ee_ctl;
	s16 max_chan_pwr = ah->ah_txpower.txp_max_pwr / 4;
	s16 edge_pwr = 0;
	u8 rep_idx;
	u8 i, ctl_mode;
	u8 ctl_idx = 0xFF;
	u32 target = channel->center_freq;

	ctl_mode = ath_regd_get_band_ctl(regulatory, channel->band);

	switch (channel->hw_value) {
	case AR5K_MODE_11A:
		if (ah->ah_bwmode == AR5K_BWMODE_40MHZ)
			ctl_mode |= AR5K_CTL_TURBO;
		else
			ctl_mode |= AR5K_CTL_11A;
		break;
	case AR5K_MODE_11G:
		if (ah->ah_bwmode == AR5K_BWMODE_40MHZ)
			ctl_mode |= AR5K_CTL_TURBOG;
		else
			ctl_mode |= AR5K_CTL_11G;
		break;
	case AR5K_MODE_11B:
		ctl_mode |= AR5K_CTL_11B;
		break;
	default:
		return;
	}

	for (i = 0; i < ee->ee_ctls; i++) {
		if (ctl_val[i] == ctl_mode) {
			ctl_idx = i;
			break;
		}
	}

	/* If we have a CTL dataset available grab it and find the
	 * edge power for our frequency */
	if (ctl_idx == 0xFF)
		return;

	/* Edge powers are sorted by frequency from lower
	 * to higher. Each CTL corresponds to 8 edge power
	 * measurements. */
	rep_idx = ctl_idx * AR5K_EEPROM_N_EDGES;

	/* Don't do boundaries check because we
	 * might have more that one bands defined
	 * for this mode */

	/* Get the edge power that's closer to our
	 * frequency */
	for (i = 0; i < AR5K_EEPROM_N_EDGES; i++) {
		rep_idx += i;
		if (target <= rep[rep_idx].freq)
			edge_pwr = (s16) rep[rep_idx].edge;
	}

	if (edge_pwr)
		ah->ah_txpower.txp_max_pwr = 4 * min(edge_pwr, max_chan_pwr);
}


/*
 * Power to PCDAC table functions
 */

/**
 * DOC: Power to PCDAC table functions
 *
 * For RF5111 we have an XPD -eXternal Power Detector- curve
 * for each calibrated channel. Each curve has 0,5dB Power steps
 * on x axis and PCDAC steps (offsets) on y axis and looks like an
 * exponential function. To recreate the curve we read 11 points
 * from eeprom (eeprom.c) and interpolate here.
 *
 * For RF5112 we have 4 XPD -eXternal Power Detector- curves
 * for each calibrated channel on 0, -6, -12 and -18dBm but we only
 * use the higher (3) and the lower (0) curves. Each curve again has 0.5dB
 * power steps on x axis and PCDAC steps on y axis and looks like a
 * linear function. To recreate the curve and pass the power values
 * on hw, we get 4 points for xpd 0 (lower gain -> max power)
 * and 3 points for xpd 3 (higher gain -> lower power) from eeprom (eeprom.c)
 * and interpolate here.
 *
 * For a given channel we get the calibrated points (piers) for it or
 * -if we don't have calibration data for this specific channel- from the
 * available surrounding channels we have calibration data for, after we do a
 * linear interpolation between them. Then since we have our calibrated points
 * for this channel, we do again a linear interpolation between them to get the
 * whole curve.
 *
 * We finally write the Y values of the curve(s) (the PCDAC values) on hw
 */

/**
 * ath5k_fill_pwr_to_pcdac_table() - Fill Power to PCDAC table on RF5111
 * @ah: The &struct ath5k_hw
 * @table_min: Minimum power (x min)
 * @table_max: Maximum power (x max)
 *
 * No further processing is needed for RF5111, the only thing we have to
 * do is fill the values below and above calibration range since eeprom data
 * may not cover the entire PCDAC table.
 */
static void
ath5k_fill_pwr_to_pcdac_table(struct ath5k_hw *ah, s16* table_min,
							s16 *table_max)
{
	u8	*pcdac_out = ah->ah_txpower.txp_pd_table;
	u8	*pcdac_tmp = ah->ah_txpower.tmpL[0];
	u8	pcdac_0, pcdac_n, pcdac_i, pwr_idx, i;
	s16	min_pwr, max_pwr;

	/* Get table boundaries */
	min_pwr = table_min[0];
	pcdac_0 = pcdac_tmp[0];

	max_pwr = table_max[0];
	pcdac_n = pcdac_tmp[table_max[0] - table_min[0]];

	/* Extrapolate below minimum using pcdac_0 */
	pcdac_i = 0;
	for (i = 0; i < min_pwr; i++)
		pcdac_out[pcdac_i++] = pcdac_0;

	/* Copy values from pcdac_tmp */
	pwr_idx = min_pwr;
	for (i = 0; pwr_idx <= max_pwr &&
		    pcdac_i < AR5K_EEPROM_POWER_TABLE_SIZE; i++) {
		pcdac_out[pcdac_i++] = pcdac_tmp[i];
		pwr_idx++;
	}

	/* Extrapolate above maximum */
	while (pcdac_i < AR5K_EEPROM_POWER_TABLE_SIZE)
		pcdac_out[pcdac_i++] = pcdac_n;

}

/**
 * ath5k_combine_linear_pcdac_curves() - Combine available PCDAC Curves
 * @ah: The &struct ath5k_hw
 * @table_min: Minimum power (x min)
 * @table_max: Maximum power (x max)
 * @pdcurves: Number of pd curves
 *
 * Combine available XPD Curves and fill Linear Power to PCDAC table on RF5112
 * RFX112 can have up to 2 curves (one for low txpower range and one for
 * higher txpower range). We need to put them both on pcdac_out and place
 * them in the correct location. In case we only have one curve available
 * just fit it on pcdac_out (it's supposed to cover the entire range of
 * available pwr levels since it's always the higher power curve). Extrapolate
 * below and above final table if needed.
 */
static void
ath5k_combine_linear_pcdac_curves(struct ath5k_hw *ah, s16* table_min,
						s16 *table_max, u8 pdcurves)
{
	u8	*pcdac_out = ah->ah_txpower.txp_pd_table;
	u8	*pcdac_low_pwr;
	u8	*pcdac_high_pwr;
	u8	*pcdac_tmp;
	u8	pwr;
	s16	max_pwr_idx;
	s16	min_pwr_idx;
	s16	mid_pwr_idx = 0;
	/* Edge flag turns on the 7nth bit on the PCDAC
	 * to declare the higher power curve (force values
	 * to be greater than 64). If we only have one curve
	 * we don't need to set this, if we have 2 curves and
	 * fill the table backwards this can also be used to
	 * switch from higher power curve to lower power curve */
	u8	edge_flag;
	int	i;

	/* When we have only one curve available
	 * that's the higher power curve. If we have
	 * two curves the first is the high power curve
	 * and the next is the low power curve. */
	if (pdcurves > 1) {
		pcdac_low_pwr = ah->ah_txpower.tmpL[1];
		pcdac_high_pwr = ah->ah_txpower.tmpL[0];
		mid_pwr_idx = table_max[1] - table_min[1] - 1;
		max_pwr_idx = (table_max[0] - table_min[0]) / 2;

		/* If table size goes beyond 31.5dB, keep the
		 * upper 31.5dB range when setting tx power.
		 * Note: 126 = 31.5 dB in quarter dB steps */
		if (table_max[0] - table_min[1] > 126)
			min_pwr_idx = table_max[0] - 126;
		else
			min_pwr_idx = table_min[1];

		/* Since we fill table backwards
		 * start from high power curve */
		pcdac_tmp = pcdac_high_pwr;

		edge_flag = 0x40;
	} else {
		pcdac_low_pwr = ah->ah_txpower.tmpL[1]; /* Zeroed */
		pcdac_high_pwr = ah->ah_txpower.tmpL[0];
		min_pwr_idx = table_min[0];
		max_pwr_idx = (table_max[0] - table_min[0]) / 2;
		pcdac_tmp = pcdac_high_pwr;
		edge_flag = 0;
	}

	/* This is used when setting tx power*/
	ah->ah_txpower.txp_min_idx = min_pwr_idx / 2;

	/* Fill Power to PCDAC table backwards */
	pwr = max_pwr_idx;
	for (i = 63; i >= 0; i--) {
		/* Entering lower power range, reset
		 * edge flag and set pcdac_tmp to lower
		 * power curve.*/
		if (edge_flag == 0x40 &&
		(2 * pwr <= (table_max[1] - table_min[0]) || pwr == 0)) {
			edge_flag = 0x00;
			pcdac_tmp = pcdac_low_pwr;
			pwr = mid_pwr_idx / 2;
		}

		/* Don't go below 1, extrapolate below if we have
		 * already switched to the lower power curve -or
		 * we only have one curve and edge_flag is zero
		 * anyway */
		if (pcdac_tmp[pwr] < 1 && (edge_flag == 0x00)) {
			while (i >= 0) {
				pcdac_out[i] = pcdac_out[i + 1];
				i--;
			}
			break;
		}

		pcdac_out[i] = pcdac_tmp[pwr] | edge_flag;

		/* Extrapolate above if pcdac is greater than
		 * 126 -this can happen because we OR pcdac_out
		 * value with edge_flag on high power curve */
		if (pcdac_out[i] > 126)
			pcdac_out[i] = 126;

		/* Decrease by a 0.5dB step */
		pwr--;
	}
}

/**
 * ath5k_write_pcdac_table() - Write the PCDAC values on hw
 * @ah: The &struct ath5k_hw
 */
static void
ath5k_write_pcdac_table(struct ath5k_hw *ah)
{
	u8	*pcdac_out = ah->ah_txpower.txp_pd_table;
	int	i;

	/*
	 * Write TX power values
	 */
	for (i = 0; i < (AR5K_EEPROM_POWER_TABLE_SIZE / 2); i++) {
		ath5k_hw_reg_write(ah,
			(((pcdac_out[2 * i + 0] << 8 | 0xff) & 0xffff) << 0) |
			(((pcdac_out[2 * i + 1] << 8 | 0xff) & 0xffff) << 16),
			AR5K_PHY_PCDAC_TXPOWER(i));
	}
}


/*
 * Power to PDADC table functions
 */

/**
 * DOC: Power to PDADC table functions
 *
 * For RF2413 and later we have a Power to PDADC table (Power Detector)
 * instead of a PCDAC (Power Control) and 4 pd gain curves for each
 * calibrated channel. Each curve has power on x axis in 0.5 db steps and
 * PDADC steps on y axis and looks like an exponential function like the
 * RF5111 curve.
 *
 * To recreate the curves we read the points from eeprom (eeprom.c)
 * and interpolate here. Note that in most cases only 2 (higher and lower)
 * curves are used (like RF5112) but vendors have the opportunity to include
 * all 4 curves on eeprom. The final curve (higher power) has an extra
 * point for better accuracy like RF5112.
 *
 * The process is similar to what we do above for RF5111/5112
 */

/**
 * ath5k_combine_pwr_to_pdadc_curves() - Combine the various PDADC curves
 * @ah: The &struct ath5k_hw
 * @pwr_min: Minimum power (x min)
 * @pwr_max: Maximum power (x max)
 * @pdcurves: Number of available curves
 *
 * Combine the various pd curves and create the final Power to PDADC table
 * We can have up to 4 pd curves, we need to do a similar process
 * as we do for RF5112. This time we don't have an edge_flag but we
 * set the gain boundaries on a separate register.
 */
static void
ath5k_combine_pwr_to_pdadc_curves(struct ath5k_hw *ah,
			s16 *pwr_min, s16 *pwr_max, u8 pdcurves)
{
	u8 gain_boundaries[AR5K_EEPROM_N_PD_GAINS];
	u8 *pdadc_out = ah->ah_txpower.txp_pd_table;
	u8 *pdadc_tmp;
	s16 pdadc_0;
	u8 pdadc_i, pdadc_n, pwr_step, pdg, max_idx, table_size;
	u8 pd_gain_overlap;

	/* Note: Register value is initialized on initvals
	 * there is no feedback from hw.
	 * XXX: What about pd_gain_overlap from EEPROM ? */
	pd_gain_overlap = (u8) ath5k_hw_reg_read(ah, AR5K_PHY_TPC_RG5) &
		AR5K_PHY_TPC_RG5_PD_GAIN_OVERLAP;

	/* Create final PDADC table */
	for (pdg = 0, pdadc_i = 0; pdg < pdcurves; pdg++) {
		pdadc_tmp = ah->ah_txpower.tmpL[pdg];

		if (pdg == pdcurves - 1)
			/* 2 dB boundary stretch for last
			 * (higher power) curve */
			gain_boundaries[pdg] = pwr_max[pdg] + 4;
		else
			/* Set gain boundary in the middle
			 * between this curve and the next one */
			gain_boundaries[pdg] =
				(pwr_max[pdg] + pwr_min[pdg + 1]) / 2;

		/* Sanity check in case our 2 db stretch got out of
		 * range. */
		if (gain_boundaries[pdg] > AR5K_TUNE_MAX_TXPOWER)
			gain_boundaries[pdg] = AR5K_TUNE_MAX_TXPOWER;

		/* For the first curve (lower power)
		 * start from 0 dB */
		if (pdg == 0)
			pdadc_0 = 0;
		else
			/* For the other curves use the gain overlap */
			pdadc_0 = (gain_boundaries[pdg - 1] - pwr_min[pdg]) -
							pd_gain_overlap;

		/* Force each power step to be at least 0.5 dB */
		if ((pdadc_tmp[1] - pdadc_tmp[0]) > 1)
			pwr_step = pdadc_tmp[1] - pdadc_tmp[0];
		else
			pwr_step = 1;

		/* If pdadc_0 is negative, we need to extrapolate
		 * below this pdgain by a number of pwr_steps */
		while ((pdadc_0 < 0) && (pdadc_i < 128)) {
			s16 tmp = pdadc_tmp[0] + pdadc_0 * pwr_step;
			pdadc_out[pdadc_i++] = (tmp < 0) ? 0 : (u8) tmp;
			pdadc_0++;
		}

		/* Set last pwr level, using gain boundaries */
		pdadc_n = gain_boundaries[pdg] + pd_gain_overlap - pwr_min[pdg];
		/* Limit it to be inside pwr range */
		table_size = pwr_max[pdg] - pwr_min[pdg];
		max_idx = (pdadc_n < table_size) ? pdadc_n : table_size;

		/* Fill pdadc_out table */
		while (pdadc_0 < max_idx && pdadc_i < 128)
			pdadc_out[pdadc_i++] = pdadc_tmp[pdadc_0++];

		/* Need to extrapolate above this pdgain? */
		if (pdadc_n <= max_idx)
			continue;

		/* Force each power step to be at least 0.5 dB */
		if ((pdadc_tmp[table_size - 1] - pdadc_tmp[table_size - 2]) > 1)
			pwr_step = pdadc_tmp[table_size - 1] -
						pdadc_tmp[table_size - 2];
		else
			pwr_step = 1;

		/* Extrapolate above */
		while ((pdadc_0 < (s16) pdadc_n) &&
		(pdadc_i < AR5K_EEPROM_POWER_TABLE_SIZE * 2)) {
			s16 tmp = pdadc_tmp[table_size - 1] +
					(pdadc_0 - max_idx) * pwr_step;
			pdadc_out[pdadc_i++] = (tmp > 127) ? 127 : (u8) tmp;
			pdadc_0++;
		}
	}

	while (pdg < AR5K_EEPROM_N_PD_GAINS) {
		gain_boundaries[pdg] = gain_boundaries[pdg - 1];
		pdg++;
	}

	while (pdadc_i < AR5K_EEPROM_POWER_TABLE_SIZE * 2) {
		pdadc_out[pdadc_i] = pdadc_out[pdadc_i - 1];
		pdadc_i++;
	}

	/* Set gain boundaries */
	ath5k_hw_reg_write(ah,
		AR5K_REG_SM(pd_gain_overlap,
			AR5K_PHY_TPC_RG5_PD_GAIN_OVERLAP) |
		AR5K_REG_SM(gain_boundaries[0],
			AR5K_PHY_TPC_RG5_PD_GAIN_BOUNDARY_1) |
		AR5K_REG_SM(gain_boundaries[1],
			AR5K_PHY_TPC_RG5_PD_GAIN_BOUNDARY_2) |
		AR5K_REG_SM(gain_boundaries[2],
			AR5K_PHY_TPC_RG5_PD_GAIN_BOUNDARY_3) |
		AR5K_REG_SM(gain_boundaries[3],
			AR5K_PHY_TPC_RG5_PD_GAIN_BOUNDARY_4),
		AR5K_PHY_TPC_RG5);

	/* Used for setting rate power table */
	ah->ah_txpower.txp_min_idx = pwr_min[0];

}

/**
 * ath5k_write_pwr_to_pdadc_table() - Write the PDADC values on hw
 * @ah: The &struct ath5k_hw
 * @ee_mode: One of enum ath5k_driver_mode
 */
static void
ath5k_write_pwr_to_pdadc_table(struct ath5k_hw *ah, u8 ee_mode)
{
	struct ath5k_eeprom_info *ee = &ah->ah_capabilities.cap_eeprom;
	u8 *pdadc_out = ah->ah_txpower.txp_pd_table;
	u8 *pdg_to_idx = ee->ee_pdc_to_idx[ee_mode];
	u8 pdcurves = ee->ee_pd_gains[ee_mode];
	u32 reg;
	u8 i;

	/* Select the right pdgain curves */

	/* Clear current settings */
	reg = ath5k_hw_reg_read(ah, AR5K_PHY_TPC_RG1);
	reg &= ~(AR5K_PHY_TPC_RG1_PDGAIN_1 |
		AR5K_PHY_TPC_RG1_PDGAIN_2 |
		AR5K_PHY_TPC_RG1_PDGAIN_3 |
		AR5K_PHY_TPC_RG1_NUM_PD_GAIN);

	/*
	 * Use pd_gains curve from eeprom
	 *
	 * This overrides the default setting from initvals
	 * in case some vendors (e.g. Zcomax) don't use the default
	 * curves. If we don't honor their settings we 'll get a
	 * 5dB (1 * gain overlap ?) drop.
	 */
	reg |= AR5K_REG_SM(pdcurves, AR5K_PHY_TPC_RG1_NUM_PD_GAIN);

	switch (pdcurves) {
	case 3:
		reg |= AR5K_REG_SM(pdg_to_idx[2], AR5K_PHY_TPC_RG1_PDGAIN_3);
		/* Fall through */
	case 2:
		reg |= AR5K_REG_SM(pdg_to_idx[1], AR5K_PHY_TPC_RG1_PDGAIN_2);
		/* Fall through */
	case 1:
		reg |= AR5K_REG_SM(pdg_to_idx[0], AR5K_PHY_TPC_RG1_PDGAIN_1);
		break;
	}
	ath5k_hw_reg_write(ah, reg, AR5K_PHY_TPC_RG1);

	/*
	 * Write TX power values
	 */
	for (i = 0; i < (AR5K_EEPROM_POWER_TABLE_SIZE / 2); i++) {
		u32 val = get_unaligned_le32(&pdadc_out[4 * i]);
		ath5k_hw_reg_write(ah, val, AR5K_PHY_PDADC_TXPOWER(i));
	}
}


/*
 * Common code for PCDAC/PDADC tables
 */

/**
 * ath5k_setup_channel_powertable() - Set up power table for this channel
 * @ah: The &struct ath5k_hw
 * @channel: The &struct ieee80211_channel
 * @ee_mode: One of enum ath5k_driver_mode
 * @type: One of enum ath5k_powertable_type (eeprom.h)
 *
 * This is the main function that uses all of the above
 * to set PCDAC/PDADC table on hw for the current channel.
 * This table is used for tx power calibration on the baseband,
 * without it we get weird tx power levels and in some cases
 * distorted spectral mask
 */
static int
ath5k_setup_channel_powertable(struct ath5k_hw *ah,
			struct ieee80211_channel *channel,
			u8 ee_mode, u8 type)
{
	struct ath5k_pdgain_info *pdg_L, *pdg_R;
	struct ath5k_chan_pcal_info *pcinfo_L;
	struct ath5k_chan_pcal_info *pcinfo_R;
	struct ath5k_eeprom_info *ee = &ah->ah_capabilities.cap_eeprom;
	u8 *pdg_curve_to_idx = ee->ee_pdc_to_idx[ee_mode];
	s16 table_min[AR5K_EEPROM_N_PD_GAINS];
	s16 table_max[AR5K_EEPROM_N_PD_GAINS];
	u8 *tmpL;
	u8 *tmpR;
	u32 target = channel->center_freq;
	int pdg, i;

	/* Get surrounding freq piers for this channel */
	ath5k_get_chan_pcal_surrounding_piers(ah, channel,
						&pcinfo_L,
						&pcinfo_R);

	/* Loop over pd gain curves on
	 * surrounding freq piers by index */
	for (pdg = 0; pdg < ee->ee_pd_gains[ee_mode]; pdg++) {

		/* Fill curves in reverse order
		 * from lower power (max gain)
		 * to higher power. Use curve -> idx
		 * backmapping we did on eeprom init */
		u8 idx = pdg_curve_to_idx[pdg];

		/* Grab the needed curves by index */
		pdg_L = &pcinfo_L->pd_curves[idx];
		pdg_R = &pcinfo_R->pd_curves[idx];

		/* Initialize the temp tables */
		tmpL = ah->ah_txpower.tmpL[pdg];
		tmpR = ah->ah_txpower.tmpR[pdg];

		/* Set curve's x boundaries and create
		 * curves so that they cover the same
		 * range (if we don't do that one table
		 * will have values on some range and the
		 * other one won't have any so interpolation
		 * will fail) */
		table_min[pdg] = min(pdg_L->pd_pwr[0],
					pdg_R->pd_pwr[0]) / 2;

		table_max[pdg] = max(pdg_L->pd_pwr[pdg_L->pd_points - 1],
				pdg_R->pd_pwr[pdg_R->pd_points - 1]) / 2;

		/* Now create the curves on surrounding channels
		 * and interpolate if needed to get the final
		 * curve for this gain on this channel */
		switch (type) {
		case AR5K_PWRTABLE_LINEAR_PCDAC:
			/* Override min/max so that we don't loose
			 * accuracy (don't divide by 2) */
			table_min[pdg] = min(pdg_L->pd_pwr[0],
						pdg_R->pd_pwr[0]);

			table_max[pdg] =
				max(pdg_L->pd_pwr[pdg_L->pd_points - 1],
					pdg_R->pd_pwr[pdg_R->pd_points - 1]);

			/* Override minimum so that we don't get
			 * out of bounds while extrapolating
			 * below. Don't do this when we have 2
			 * curves and we are on the high power curve
			 * because table_min is ok in this case */
			if (!(ee->ee_pd_gains[ee_mode] > 1 && pdg == 0)) {

				table_min[pdg] =
					ath5k_get_linear_pcdac_min(pdg_L->pd_step,
								pdg_R->pd_step,
								pdg_L->pd_pwr,
								pdg_R->pd_pwr);

				/* Don't go too low because we will
				 * miss the upper part of the curve.
				 * Note: 126 = 31.5dB (max power supported)
				 * in 0.25dB units */
				if (table_max[pdg] - table_min[pdg] > 126)
					table_min[pdg] = table_max[pdg] - 126;
			}

			/* Fall through */
		case AR5K_PWRTABLE_PWR_TO_PCDAC:
		case AR5K_PWRTABLE_PWR_TO_PDADC:

			ath5k_create_power_curve(table_min[pdg],
						table_max[pdg],
						pdg_L->pd_pwr,
						pdg_L->pd_step,
						pdg_L->pd_points, tmpL, type);

			/* We are in a calibration
			 * pier, no need to interpolate
			 * between freq piers */
			if (pcinfo_L == pcinfo_R)
				continue;

			ath5k_create_power_curve(table_min[pdg],
						table_max[pdg],
						pdg_R->pd_pwr,
						pdg_R->pd_step,
						pdg_R->pd_points, tmpR, type);
			break;
		default:
			return -EINVAL;
		}

		/* Interpolate between curves
		 * of surrounding freq piers to
		 * get the final curve for this
		 * pd gain. Re-use tmpL for interpolation
		 * output */
		for (i = 0; (i < (u16) (table_max[pdg] - table_min[pdg])) &&
		(i < AR5K_EEPROM_POWER_TABLE_SIZE); i++) {
			tmpL[i] = (u8) ath5k_get_interpolated_value(target,
							(s16) pcinfo_L->freq,
							(s16) pcinfo_R->freq,
							(s16) tmpL[i],
							(s16) tmpR[i]);
		}
	}

	/* Now we have a set of curves for this
	 * channel on tmpL (x range is table_max - table_min
	 * and y values are tmpL[pdg][]) sorted in the same
	 * order as EEPROM (because we've used the backmapping).
	 * So for RF5112 it's from higher power to lower power
	 * and for RF2413 it's from lower power to higher power.
	 * For RF5111 we only have one curve. */

	/* Fill min and max power levels for this
	 * channel by interpolating the values on
	 * surrounding channels to complete the dataset */
	ah->ah_txpower.txp_min_pwr = ath5k_get_interpolated_value(target,
					(s16) pcinfo_L->freq,
					(s16) pcinfo_R->freq,
					pcinfo_L->min_pwr, pcinfo_R->min_pwr);

	ah->ah_txpower.txp_max_pwr = ath5k_get_interpolated_value(target,
					(s16) pcinfo_L->freq,
					(s16) pcinfo_R->freq,
					pcinfo_L->max_pwr, pcinfo_R->max_pwr);

	/* Fill PCDAC/PDADC table */
	switch (type) {
	case AR5K_PWRTABLE_LINEAR_PCDAC:
		/* For RF5112 we can have one or two curves
		 * and each curve covers a certain power lvl
		 * range so we need to do some more processing */
		ath5k_combine_linear_pcdac_curves(ah, table_min, table_max,
						ee->ee_pd_gains[ee_mode]);

		/* Set txp.offset so that we can
		 * match max power value with max
		 * table index */
		ah->ah_txpower.txp_offset = 64 - (table_max[0] / 2);
		break;
	case AR5K_PWRTABLE_PWR_TO_PCDAC:
		/* We are done for RF5111 since it has only
		 * one curve, just fit the curve on the table */
		ath5k_fill_pwr_to_pcdac_table(ah, table_min, table_max);

		/* No rate powertable adjustment for RF5111 */
		ah->ah_txpower.txp_min_idx = 0;
		ah->ah_txpower.txp_offset = 0;
		break;
	case AR5K_PWRTABLE_PWR_TO_PDADC:
		/* Set PDADC boundaries and fill
		 * final PDADC table */
		ath5k_combine_pwr_to_pdadc_curves(ah, table_min, table_max,
						ee->ee_pd_gains[ee_mode]);

		/* Set txp.offset, note that table_min
		 * can be negative */
		ah->ah_txpower.txp_offset = table_min[0];
		break;
	default:
		return -EINVAL;
	}

	ah->ah_txpower.txp_setup = true;

	return 0;
}

/**
 * ath5k_write_channel_powertable() - Set power table for current channel on hw
 * @ah: The &struct ath5k_hw
 * @ee_mode: One of enum ath5k_driver_mode
 * @type: One of enum ath5k_powertable_type (eeprom.h)
 */
static void
ath5k_write_channel_powertable(struct ath5k_hw *ah, u8 ee_mode, u8 type)
{
	if (type == AR5K_PWRTABLE_PWR_TO_PDADC)
		ath5k_write_pwr_to_pdadc_table(ah, ee_mode);
	else
		ath5k_write_pcdac_table(ah);
}


/**
 * DOC: Per-rate tx power setting
 *
 * This is the code that sets the desired tx power limit (below
 * maximum) on hw for each rate (we also have TPC that sets
 * power per packet type). We do that by providing an index on the
 * PCDAC/PDADC table we set up above, for each rate.
 *
 * For now we only limit txpower based on maximum tx power
 * supported by hw (what's inside rate_info) + conformance test
 * limits. We need to limit this even more, based on regulatory domain
 * etc to be safe. Normally this is done from above so we don't care
 * here, all we care is that the tx power we set will be O.K.
 * for the hw (e.g. won't create noise on PA etc).
 *
 * Rate power table contains indices to PCDAC/PDADC table (0.5dB steps -
 * x values) and is indexed as follows:
 * rates[0] - rates[7] -> OFDM rates
 * rates[8] - rates[14] -> CCK rates
 * rates[15] -> XR rates (they all have the same power)
 */

/**
 * ath5k_setup_rate_powertable() - Set up rate power table for a given tx power
 * @ah: The &struct ath5k_hw
 * @max_pwr: The maximum tx power requested in 0.5dB steps
 * @rate_info: The &struct ath5k_rate_pcal_info to fill
 * @ee_mode: One of enum ath5k_driver_mode
 */
static void
ath5k_setup_rate_powertable(struct ath5k_hw *ah, u16 max_pwr,
			struct ath5k_rate_pcal_info *rate_info,
			u8 ee_mode)
{
	unsigned int i;
	u16 *rates;

	/* max_pwr is power level we got from driver/user in 0.5dB
	 * units, switch to 0.25dB units so we can compare */
	max_pwr *= 2;
	max_pwr = min(max_pwr, (u16) ah->ah_txpower.txp_max_pwr) / 2;

	/* apply rate limits */
	rates = ah->ah_txpower.txp_rates_power_table;

	/* OFDM rates 6 to 24Mb/s */
	for (i = 0; i < 5; i++)
		rates[i] = min(max_pwr, rate_info->target_power_6to24);

	/* Rest OFDM rates */
	rates[5] = min(rates[0], rate_info->target_power_36);
	rates[6] = min(rates[0], rate_info->target_power_48);
	rates[7] = min(rates[0], rate_info->target_power_54);

	/* CCK rates */
	/* 1L */
	rates[8] = min(rates[0], rate_info->target_power_6to24);
	/* 2L */
	rates[9] = min(rates[0], rate_info->target_power_36);
	/* 2S */
	rates[10] = min(rates[0], rate_info->target_power_36);
	/* 5L */
	rates[11] = min(rates[0], rate_info->target_power_48);
	/* 5S */
	rates[12] = min(rates[0], rate_info->target_power_48);
	/* 11L */
	rates[13] = min(rates[0], rate_info->target_power_54);
	/* 11S */
	rates[14] = min(rates[0], rate_info->target_power_54);

	/* XR rates */
	rates[15] = min(rates[0], rate_info->target_power_6to24);

	/* CCK rates have different peak to average ratio
	 * so we have to tweak their power so that gainf
	 * correction works ok. For this we use OFDM to
	 * CCK delta from eeprom */
	if ((ee_mode == AR5K_EEPROM_MODE_11G) &&
	(ah->ah_phy_revision < AR5K_SREV_PHY_5212A))
		for (i = 8; i <= 15; i++)
			rates[i] -= ah->ah_txpower.txp_cck_ofdm_gainf_delta;

	/* Now that we have all rates setup use table offset to
	 * match the power range set by user with the power indices
	 * on PCDAC/PDADC table */
	for (i = 0; i < 16; i++) {
		rates[i] += ah->ah_txpower.txp_offset;
		/* Don't get out of bounds */
		if (rates[i] > 63)
			rates[i] = 63;
	}

	/* Min/max in 0.25dB units */
	ah->ah_txpower.txp_min_pwr = 2 * rates[7];
	ah->ah_txpower.txp_cur_pwr = 2 * rates[0];
	ah->ah_txpower.txp_ofdm = rates[7];
}


/**
 * ath5k_hw_txpower() - Set transmission power limit for a given channel
 * @ah: The &struct ath5k_hw
 * @channel: The &struct ieee80211_channel
 * @txpower: Requested tx power in 0.5dB steps
 *
 * Combines all of the above to set the requested tx power limit
 * on hw.
 */
static int
ath5k_hw_txpower(struct ath5k_hw *ah, struct ieee80211_channel *channel,
		 u8 txpower)
{
	struct ath5k_rate_pcal_info rate_info;
	struct ieee80211_channel *curr_channel = ah->ah_current_channel;
	int ee_mode;
	u8 type;
	int ret;

	if (txpower > AR5K_TUNE_MAX_TXPOWER) {
		ATH5K_ERR(ah, "invalid tx power: %u\n", txpower);
		return -EINVAL;
	}

	ee_mode = ath5k_eeprom_mode_from_channel(channel);
	if (ee_mode < 0) {
		ATH5K_ERR(ah,
			"invalid channel: %d\n", channel->center_freq);
		return -EINVAL;
	}

	/* Initialize TX power table */
	switch (ah->ah_radio) {
	case AR5K_RF5110:
		/* TODO */
		return 0;
	case AR5K_RF5111:
		type = AR5K_PWRTABLE_PWR_TO_PCDAC;
		break;
	case AR5K_RF5112:
		type = AR5K_PWRTABLE_LINEAR_PCDAC;
		break;
	case AR5K_RF2413:
	case AR5K_RF5413:
	case AR5K_RF2316:
	case AR5K_RF2317:
	case AR5K_RF2425:
		type = AR5K_PWRTABLE_PWR_TO_PDADC;
		break;
	default:
		return -EINVAL;
	}

	/*
	 * If we don't change channel/mode skip tx powertable calculation
	 * and use the cached one.
	 */
	if (!ah->ah_txpower.txp_setup ||
	    (channel->hw_value != curr_channel->hw_value) ||
	    (channel->center_freq != curr_channel->center_freq)) {
		/* Reset TX power values */
		memset(&ah->ah_txpower, 0, sizeof(ah->ah_txpower));
		ah->ah_txpower.txp_tpc = AR5K_TUNE_TPC_TXPOWER;

		/* Calculate the powertable */
		ret = ath5k_setup_channel_powertable(ah, channel,
							ee_mode, type);
		if (ret)
			return ret;
	}

	/* Write table on hw */
	ath5k_write_channel_powertable(ah, ee_mode, type);

	/* Limit max power if we have a CTL available */
	ath5k_get_max_ctl_power(ah, channel);

	/* FIXME: Antenna reduction stuff */

	/* FIXME: Limit power on turbo modes */

	/* FIXME: TPC scale reduction */

	/* Get surrounding channels for per-rate power table
	 * calibration */
	ath5k_get_rate_pcal_data(ah, channel, &rate_info);

	/* Setup rate power table */
	ath5k_setup_rate_powertable(ah, txpower, &rate_info, ee_mode);

	/* Write rate power table on hw */
	ath5k_hw_reg_write(ah, AR5K_TXPOWER_OFDM(3, 24) |
		AR5K_TXPOWER_OFDM(2, 16) | AR5K_TXPOWER_OFDM(1, 8) |
		AR5K_TXPOWER_OFDM(0, 0), AR5K_PHY_TXPOWER_RATE1);

	ath5k_hw_reg_write(ah, AR5K_TXPOWER_OFDM(7, 24) |
		AR5K_TXPOWER_OFDM(6, 16) | AR5K_TXPOWER_OFDM(5, 8) |
		AR5K_TXPOWER_OFDM(4, 0), AR5K_PHY_TXPOWER_RATE2);

	ath5k_hw_reg_write(ah, AR5K_TXPOWER_CCK(10, 24) |
		AR5K_TXPOWER_CCK(9, 16) | AR5K_TXPOWER_CCK(15, 8) |
		AR5K_TXPOWER_CCK(8, 0), AR5K_PHY_TXPOWER_RATE3);

	ath5k_hw_reg_write(ah, AR5K_TXPOWER_CCK(14, 24) |
		AR5K_TXPOWER_CCK(13, 16) | AR5K_TXPOWER_CCK(12, 8) |
		AR5K_TXPOWER_CCK(11, 0), AR5K_PHY_TXPOWER_RATE4);

	/* FIXME: TPC support */
	if (ah->ah_txpower.txp_tpc) {
		ath5k_hw_reg_write(ah, AR5K_PHY_TXPOWER_RATE_MAX_TPC_ENABLE |
			AR5K_TUNE_MAX_TXPOWER, AR5K_PHY_TXPOWER_RATE_MAX);

		ath5k_hw_reg_write(ah,
			AR5K_REG_MS(AR5K_TUNE_MAX_TXPOWER, AR5K_TPC_ACK) |
			AR5K_REG_MS(AR5K_TUNE_MAX_TXPOWER, AR5K_TPC_CTS) |
			AR5K_REG_MS(AR5K_TUNE_MAX_TXPOWER, AR5K_TPC_CHIRP),
			AR5K_TPC);
	} else {
		ath5k_hw_reg_write(ah, AR5K_PHY_TXPOWER_RATE_MAX |
			AR5K_TUNE_MAX_TXPOWER, AR5K_PHY_TXPOWER_RATE_MAX);
	}

	return 0;
}

/**
 * ath5k_hw_set_txpower_limit() - Set txpower limit for the current channel
 * @ah: The &struct ath5k_hw
 * @txpower: The requested tx power limit in 0.5dB steps
 *
 * This function provides access to ath5k_hw_txpower to the driver in
 * case user or an application changes it while PHY is running.
 */
int
ath5k_hw_set_txpower_limit(struct ath5k_hw *ah, u8 txpower)
{
	ATH5K_DBG(ah, ATH5K_DEBUG_TXPOWER,
		"changing txpower to %d\n", txpower);

	return ath5k_hw_txpower(ah, ah->ah_current_channel, txpower);
}


/*************\
 Init function
\*************/

/**
 * ath5k_hw_phy_init() - Initialize PHY
 * @ah: The &struct ath5k_hw
 * @channel: The @struct ieee80211_channel
 * @mode: One of enum ath5k_driver_mode
 * @fast: Try a fast channel switch instead
 *
 * This is the main function used during reset to initialize PHY
 * or do a fast channel change if possible.
 *
 * NOTE: Do not call this one from the driver, it assumes PHY is in a
 * warm reset state !
 */
int
ath5k_hw_phy_init(struct ath5k_hw *ah, struct ieee80211_channel *channel,
		      u8 mode, bool fast)
{
	struct ieee80211_channel *curr_channel;
	int ret, i;
	u32 phy_tst1;
	ret = 0;

	/*
	 * Sanity check for fast flag
	 * Don't try fast channel change when changing modulation
	 * mode/band. We check for chip compatibility on
	 * ath5k_hw_reset.
	 */
	curr_channel = ah->ah_current_channel;
	if (fast && (channel->hw_value != curr_channel->hw_value))
		return -EINVAL;

	/*
	 * On fast channel change we only set the synth parameters
	 * while PHY is running, enable calibration and skip the rest.
	 */
	if (fast) {
		AR5K_REG_ENABLE_BITS(ah, AR5K_PHY_RFBUS_REQ,
				    AR5K_PHY_RFBUS_REQ_REQUEST);
		for (i = 0; i < 100; i++) {
			if (ath5k_hw_reg_read(ah, AR5K_PHY_RFBUS_GRANT))
				break;
			udelay(5);
		}
		/* Failed */
		if (i >= 100)
			return -EIO;

		/* Set channel and wait for synth */
		ret = ath5k_hw_channel(ah, channel);
		if (ret)
			return ret;

		ath5k_hw_wait_for_synth(ah, channel);
	}

	/*
	 * Set TX power
	 *
	 * Note: We need to do that before we set
	 * RF buffer settings on 5211/5212+ so that we
	 * properly set curve indices.
	 */
	ret = ath5k_hw_txpower(ah, channel, ah->ah_txpower.txp_cur_pwr ?
			ah->ah_txpower.txp_cur_pwr / 2 : AR5K_TUNE_MAX_TXPOWER);
	if (ret)
		return ret;

	/* Write OFDM timings on 5212*/
	if (ah->ah_version == AR5K_AR5212 &&
		channel->hw_value != AR5K_MODE_11B) {

		ret = ath5k_hw_write_ofdm_timings(ah, channel);
		if (ret)
			return ret;

		/* Spur info is available only from EEPROM versions
		 * greater than 5.3, but the EEPROM routines will use
		 * static values for older versions */
		if (ah->ah_mac_srev >= AR5K_SREV_AR5424)
			ath5k_hw_set_spur_mitigation_filter(ah,
							    channel);
	}

	/* If we used fast channel switching
	 * we are done, release RF bus and
	 * fire up NF calibration.
	 *
	 * Note: Only NF calibration due to
	 * channel change, not AGC calibration
	 * since AGC is still running !
	 */
	if (fast) {
		/*
		 * Release RF Bus grant
		 */
		AR5K_REG_DISABLE_BITS(ah, AR5K_PHY_RFBUS_REQ,
				    AR5K_PHY_RFBUS_REQ_REQUEST);

		/*
		 * Start NF calibration
		 */
		AR5K_REG_ENABLE_BITS(ah, AR5K_PHY_AGCCTL,
					AR5K_PHY_AGCCTL_NF);

		return ret;
	}

	/*
	 * For 5210 we do all initialization using
	 * initvals, so we don't have to modify
	 * any settings (5210 also only supports
	 * a/aturbo modes)
	 */
	if (ah->ah_version != AR5K_AR5210) {

		/*
		 * Write initial RF gain settings
		 * This should work for both 5111/5112
		 */
		ret = ath5k_hw_rfgain_init(ah, channel->band);
		if (ret)
			return ret;

		usleep_range(1000, 1500);

		/*
		 * Write RF buffer
		 */
		ret = ath5k_hw_rfregs_init(ah, channel, mode);
		if (ret)
			return ret;

		/*Enable/disable 802.11b mode on 5111
		(enable 2111 frequency converter + CCK)*/
		if (ah->ah_radio == AR5K_RF5111) {
			if (mode == AR5K_MODE_11B)
				AR5K_REG_ENABLE_BITS(ah, AR5K_TXCFG,
				    AR5K_TXCFG_B_MODE);
			else
				AR5K_REG_DISABLE_BITS(ah, AR5K_TXCFG,
				    AR5K_TXCFG_B_MODE);
		}

	} else if (ah->ah_version == AR5K_AR5210) {
		usleep_range(1000, 1500);
		/* Disable phy and wait */
		ath5k_hw_reg_write(ah, AR5K_PHY_ACT_DISABLE, AR5K_PHY_ACT);
		usleep_range(1000, 1500);
	}

	/* Set channel on PHY */
	ret = ath5k_hw_channel(ah, channel);
	if (ret)
		return ret;

	/*
	 * Enable the PHY and wait until completion
	 * This includes BaseBand and Synthesizer
	 * activation.
	 */
	ath5k_hw_reg_write(ah, AR5K_PHY_ACT_ENABLE, AR5K_PHY_ACT);

	ath5k_hw_wait_for_synth(ah, channel);

	/*
	 * Perform ADC test to see if baseband is ready
	 * Set tx hold and check adc test register
	 */
	phy_tst1 = ath5k_hw_reg_read(ah, AR5K_PHY_TST1);
	ath5k_hw_reg_write(ah, AR5K_PHY_TST1_TXHOLD, AR5K_PHY_TST1);
	for (i = 0; i <= 20; i++) {
		if (!(ath5k_hw_reg_read(ah, AR5K_PHY_ADC_TEST) & 0x10))
			break;
		usleep_range(200, 250);
	}
	ath5k_hw_reg_write(ah, phy_tst1, AR5K_PHY_TST1);

	/*
	 * Start automatic gain control calibration
	 *
	 * During AGC calibration RX path is re-routed to
	 * a power detector so we don't receive anything.
	 *
	 * This method is used to calibrate some static offsets
	 * used together with on-the fly I/Q calibration (the
	 * one performed via ath5k_hw_phy_calibrate), which doesn't
	 * interrupt rx path.
	 *
	 * While rx path is re-routed to the power detector we also
	 * start a noise floor calibration to measure the
	 * card's noise floor (the noise we measure when we are not
	 * transmitting or receiving anything).
	 *
	 * If we are in a noisy environment, AGC calibration may time
	 * out and/or noise floor calibration might timeout.
	 */
	AR5K_REG_ENABLE_BITS(ah, AR5K_PHY_AGCCTL,
				AR5K_PHY_AGCCTL_CAL | AR5K_PHY_AGCCTL_NF);

	/* At the same time start I/Q calibration for QAM constellation
	 * -no need for CCK- */
	ah->ah_iq_cal_needed = false;
	if (!(mode == AR5K_MODE_11B)) {
		ah->ah_iq_cal_needed = true;
		AR5K_REG_WRITE_BITS(ah, AR5K_PHY_IQ,
				AR5K_PHY_IQ_CAL_NUM_LOG_MAX, 15);
		AR5K_REG_ENABLE_BITS(ah, AR5K_PHY_IQ,
				AR5K_PHY_IQ_RUN);
	}

	/* Wait for gain calibration to finish (we check for I/Q calibration
	 * during ath5k_phy_calibrate) */
	if (ath5k_hw_register_timeout(ah, AR5K_PHY_AGCCTL,
			AR5K_PHY_AGCCTL_CAL, 0, false)) {
		ATH5K_ERR(ah, "gain calibration timeout (%uMHz)\n",
			channel->center_freq);
	}

	/* Restore antenna mode */
	ath5k_hw_set_antenna_mode(ah, ah->ah_ant_mode);

	return ret;
}
