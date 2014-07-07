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

#include "common.h"

#define FUDGE 2

/* Calculate the modulo of a 64 bit TSF snapshot with a TU divisor */
static u32 ath9k_mod_tsf64_tu(u64 tsf, u32 div_tu)
{
	u32 tsf_mod, tsf_hi, tsf_lo, mod_hi, mod_lo;

	tsf_mod = tsf & (BIT(10) - 1);
	tsf_hi = tsf >> 32;
	tsf_lo = ((u32) tsf) >> 10;

	mod_hi = tsf_hi % div_tu;
	mod_lo = ((mod_hi << 22) + tsf_lo) % div_tu;

	return (mod_lo << 10) | tsf_mod;
}

static u32 ath9k_get_next_tbtt(struct ath_hw *ah, u64 tsf,
			       unsigned int interval)
{
	unsigned int offset;

	tsf += TU_TO_USEC(FUDGE + ah->config.sw_beacon_response_time);
	offset = ath9k_mod_tsf64_tu(tsf, interval);

	return (u32) tsf + TU_TO_USEC(interval) - offset;
}

/*
 * This sets up the beacon timers according to the timestamp of the last
 * received beacon and the current TSF, configures PCF and DTIM
 * handling, programs the sleep registers so the hardware will wakeup in
 * time to receive beacons, and configures the beacon miss handling so
 * we'll receive a BMISS interrupt when we stop seeing beacons from the AP
 * we've associated with.
 */
int ath9k_cmn_beacon_config_sta(struct ath_hw *ah,
				 struct ath_beacon_config *conf,
				 struct ath9k_beacon_state *bs)
{
	struct ath_common *common = ath9k_hw_common(ah);
	int dtim_intval;
	u64 tsf;

	/* No need to configure beacon if we are not associated */
	if (!test_bit(ATH_OP_PRIM_STA_VIF, &common->op_flags)) {
		ath_dbg(common, BEACON,
			"STA is not yet associated..skipping beacon config\n");
		return -EPERM;
	}

	memset(bs, 0, sizeof(*bs));
	conf->intval = conf->beacon_interval;

	/*
	 * Setup dtim parameters according to
	 * last beacon we received (which may be none).
	 */
	dtim_intval = conf->intval * conf->dtim_period;

	/*
	 * Pull nexttbtt forward to reflect the current
	 * TSF and calculate dtim state for the result.
	 */
	tsf = ath9k_hw_gettsf64(ah);
	conf->nexttbtt = ath9k_get_next_tbtt(ah, tsf, conf->intval);

	bs->bs_intval = TU_TO_USEC(conf->intval);
	bs->bs_dtimperiod = conf->dtim_period * bs->bs_intval;
	bs->bs_nexttbtt = conf->nexttbtt;
	bs->bs_nextdtim = conf->nexttbtt;
	if (conf->dtim_period > 1)
		bs->bs_nextdtim = ath9k_get_next_tbtt(ah, tsf, dtim_intval);

	/*
	 * Calculate the number of consecutive beacons to miss* before taking
	 * a BMISS interrupt. The configuration is specified in TU so we only
	 * need calculate based	on the beacon interval.  Note that we clamp the
	 * result to at most 15 beacons.
	 */
	bs->bs_bmissthreshold = DIV_ROUND_UP(conf->bmiss_timeout, conf->intval);
	if (bs->bs_bmissthreshold > 15)
		bs->bs_bmissthreshold = 15;
	else if (bs->bs_bmissthreshold <= 0)
		bs->bs_bmissthreshold = 1;

	/*
	 * Calculate sleep duration. The configuration is given in ms.
	 * We ensure a multiple of the beacon period is used. Also, if the sleep
	 * duration is greater than the DTIM period then it makes senses
	 * to make it a multiple of that.
	 *
	 * XXX fixed at 100ms
	 */

	bs->bs_sleepduration = TU_TO_USEC(roundup(IEEE80211_MS_TO_TU(100),
						 conf->intval));
	if (bs->bs_sleepduration > bs->bs_dtimperiod)
		bs->bs_sleepduration = bs->bs_dtimperiod;

	/* TSF out of range threshold fixed at 1 second */
	bs->bs_tsfoor_threshold = ATH9K_TSFOOR_THRESHOLD;

	ath_dbg(common, BEACON, "bmiss: %u sleep: %u\n",
		bs->bs_bmissthreshold, bs->bs_sleepduration);
	return 0;
}
EXPORT_SYMBOL(ath9k_cmn_beacon_config_sta);

void ath9k_cmn_beacon_config_adhoc(struct ath_hw *ah,
				   struct ath_beacon_config *conf)
{
	struct ath_common *common = ath9k_hw_common(ah);

	conf->intval = TU_TO_USEC(conf->beacon_interval);

	if (conf->ibss_creator)
		conf->nexttbtt = conf->intval;
	else
		conf->nexttbtt = ath9k_get_next_tbtt(ah, ath9k_hw_gettsf64(ah),
					       conf->beacon_interval);

	if (conf->enable_beacon)
		ah->imask |= ATH9K_INT_SWBA;
	else
		ah->imask &= ~ATH9K_INT_SWBA;

	ath_dbg(common, BEACON,
		"IBSS (%s) nexttbtt: %u intval: %u conf_intval: %u\n",
		(conf->enable_beacon) ? "Enable" : "Disable",
		conf->nexttbtt, conf->intval, conf->beacon_interval);
}
EXPORT_SYMBOL(ath9k_cmn_beacon_config_adhoc);

/*
 * For multi-bss ap support beacons are either staggered evenly over N slots or
 * burst together.  For the former arrange for the SWBA to be delivered for each
 * slot. Slots that are not occupied will generate nothing.
 */
void ath9k_cmn_beacon_config_ap(struct ath_hw *ah,
				struct ath_beacon_config *conf,
				unsigned int bc_buf)
{
	struct ath_common *common = ath9k_hw_common(ah);

	/* NB: the beacon interval is kept internally in TU's */
	conf->intval = TU_TO_USEC(conf->beacon_interval);
	conf->intval /= bc_buf;
	conf->nexttbtt = ath9k_get_next_tbtt(ah, ath9k_hw_gettsf64(ah),
				       conf->beacon_interval);

	if (conf->enable_beacon)
		ah->imask |= ATH9K_INT_SWBA;
	else
		ah->imask &= ~ATH9K_INT_SWBA;

	ath_dbg(common, BEACON,
		"AP (%s) nexttbtt: %u intval: %u conf_intval: %u\n",
		(conf->enable_beacon) ? "Enable" : "Disable",
		conf->nexttbtt, conf->intval, conf->beacon_interval);
}
EXPORT_SYMBOL(ath9k_cmn_beacon_config_ap);
