/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include <linux/ctype.h>
#include <linux/kernel.h>
#ifdef BRCM_FULLMAC
#include <linux/netdevice.h>
#endif
#include <osl.h>
#include <bcmdefs.h>
#include <bcmutils.h>
#include <bcmwifi.h>

/*
 * Verify the chanspec is using a legal set of parameters, i.e. that the
 * chanspec specified a band, bw, ctl_sb and channel and that the
 * combination could be legal given any set of circumstances.
 * RETURNS: true is the chanspec is malformed, false if it looks good.
 */
bool wf_chspec_malformed(chanspec_t chanspec)
{
	/* must be 2G or 5G band */
	if (!CHSPEC_IS5G(chanspec) && !CHSPEC_IS2G(chanspec))
		return true;
	/* must be 20 or 40 bandwidth */
	if (!CHSPEC_IS40(chanspec) && !CHSPEC_IS20(chanspec))
		return true;

	/* 20MHZ b/w must have no ctl sb, 40 must have a ctl sb */
	if (CHSPEC_IS20(chanspec)) {
		if (!CHSPEC_SB_NONE(chanspec))
			return true;
	} else {
		if (!CHSPEC_SB_UPPER(chanspec) && !CHSPEC_SB_LOWER(chanspec))
			return true;
	}

	return false;
}

/*
 * This function returns the channel number that control traffic is being sent on, for legacy
 * channels this is just the channel number, for 40MHZ channels it is the upper or lowre 20MHZ
 * sideband depending on the chanspec selected
 */
u8 wf_chspec_ctlchan(chanspec_t chspec)
{
	u8 ctl_chan;

	/* Is there a sideband ? */
	if (CHSPEC_CTL_SB(chspec) == WL_CHANSPEC_CTL_SB_NONE) {
		return CHSPEC_CHANNEL(chspec);
	} else {
		/* we only support 40MHZ with sidebands */
		ASSERT(CHSPEC_BW(chspec) == WL_CHANSPEC_BW_40);
		/* chanspec channel holds the centre frequency, use that and the
		 * side band information to reconstruct the control channel number
		 */
		if (CHSPEC_CTL_SB(chspec) == WL_CHANSPEC_CTL_SB_UPPER) {
			/* control chan is the upper 20 MHZ SB of the 40MHZ channel */
			ctl_chan = UPPER_20_SB(CHSPEC_CHANNEL(chspec));
		} else {
			ASSERT(CHSPEC_CTL_SB(chspec) ==
			       WL_CHANSPEC_CTL_SB_LOWER);
			/* control chan is the lower 20 MHZ SB of the 40MHZ channel */
			ctl_chan = LOWER_20_SB(CHSPEC_CHANNEL(chspec));
		}
	}

	return ctl_chan;
}

chanspec_t wf_chspec_ctlchspec(chanspec_t chspec)
{
	chanspec_t ctl_chspec = 0;
	u8 channel;

	ASSERT(!wf_chspec_malformed(chspec));

	/* Is there a sideband ? */
	if (CHSPEC_CTL_SB(chspec) == WL_CHANSPEC_CTL_SB_NONE) {
		return chspec;
	} else {
		if (CHSPEC_CTL_SB(chspec) == WL_CHANSPEC_CTL_SB_UPPER) {
			channel = UPPER_20_SB(CHSPEC_CHANNEL(chspec));
		} else {
			channel = LOWER_20_SB(CHSPEC_CHANNEL(chspec));
		}
		ctl_chspec =
		    channel | WL_CHANSPEC_BW_20 | WL_CHANSPEC_CTL_SB_NONE;
		ctl_chspec |= CHSPEC_BAND(chspec);
	}
	return ctl_chspec;
}

/*
 * Return the channel number for a given frequency and base frequency.
 * The returned channel number is relative to the given base frequency.
 * If the given base frequency is zero, a base frequency of 5 GHz is assumed for
 * frequencies from 5 - 6 GHz, and 2.407 GHz is assumed for 2.4 - 2.5 GHz.
 *
 * Frequency is specified in MHz.
 * The base frequency is specified as (start_factor * 500 kHz).
 * Constants WF_CHAN_FACTOR_2_4_G, WF_CHAN_FACTOR_5_G are defined for
 * 2.4 GHz and 5 GHz bands.
 *
 * The returned channel will be in the range [1, 14] in the 2.4 GHz band
 * and [0, 200] otherwise.
 * -1 is returned if the start_factor is WF_CHAN_FACTOR_2_4_G and the
 * frequency is not a 2.4 GHz channel, or if the frequency is not and even
 * multiple of 5 MHz from the base frequency to the base plus 1 GHz.
 *
 * Reference 802.11 REVma, section 17.3.8.3, and 802.11B section 18.4.6.2
 */
int wf_mhz2channel(uint freq, uint start_factor)
{
	int ch = -1;
	uint base;
	int offset;

	/* take the default channel start frequency */
	if (start_factor == 0) {
		if (freq >= 2400 && freq <= 2500)
			start_factor = WF_CHAN_FACTOR_2_4_G;
		else if (freq >= 5000 && freq <= 6000)
			start_factor = WF_CHAN_FACTOR_5_G;
	}

	if (freq == 2484 && start_factor == WF_CHAN_FACTOR_2_4_G)
		return 14;

	base = start_factor / 2;

	/* check that the frequency is in 1GHz range of the base */
	if ((freq < base) || (freq > base + 1000))
		return -1;

	offset = freq - base;
	ch = offset / 5;

	/* check that frequency is a 5MHz multiple from the base */
	if (offset != (ch * 5))
		return -1;

	/* restricted channel range check for 2.4G */
	if (start_factor == WF_CHAN_FACTOR_2_4_G && (ch < 1 || ch > 13))
		return -1;

	return ch;
}

/*
 * Return the center frequency in MHz of the given channel and base frequency.
 * The channel number is interpreted relative to the given base frequency.
 *
 * The valid channel range is [1, 14] in the 2.4 GHz band and [0, 200] otherwise.
 * The base frequency is specified as (start_factor * 500 kHz).
 * Constants WF_CHAN_FACTOR_2_4_G, WF_CHAN_FACTOR_4_G, and WF_CHAN_FACTOR_5_G
 * are defined for 2.4 GHz, 4 GHz, and 5 GHz bands.
 * The channel range of [1, 14] is only checked for a start_factor of
 * WF_CHAN_FACTOR_2_4_G (4814 = 2407 * 2).
 * Odd start_factors produce channels on .5 MHz boundaries, in which case
 * the answer is rounded down to an integral MHz.
 * -1 is returned for an out of range channel.
 *
 * Reference 802.11 REVma, section 17.3.8.3, and 802.11B section 18.4.6.2
 */
int wf_channel2mhz(uint ch, uint start_factor)
{
	int freq;

	if ((start_factor == WF_CHAN_FACTOR_2_4_G && (ch < 1 || ch > 14)) ||
	    (ch > 200))
		freq = -1;
	else if ((start_factor == WF_CHAN_FACTOR_2_4_G) && (ch == 14))
		freq = 2484;
	else
		freq = ch * 5 + start_factor / 2;

	return freq;
}
