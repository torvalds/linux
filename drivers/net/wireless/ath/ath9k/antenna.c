/*
 * Copyright (c) 2012 Qualcomm Atheros, Inc.
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

#include "ath9k.h"

/*
 * AR9285
 * ======
 *
 * EEPROM has 2 4-bit fields containing the card configuration.
 *
 * antdiv_ctl1:
 * ------------
 * bb_enable_ant_div_lnadiv : 1
 * bb_ant_div_alt_gaintb    : 1
 * bb_ant_div_main_gaintb   : 1
 * bb_enable_ant_fast_div   : 1
 *
 * antdiv_ctl2:
 * -----------
 * bb_ant_div_alt_lnaconf  : 2
 * bb_ant_div_main_lnaconf : 2
 *
 * The EEPROM bits are used as follows:
 * ------------------------------------
 *
 * bb_enable_ant_div_lnadiv      - Enable LNA path rx antenna diversity/combining.
 *                                 Set in AR_PHY_MULTICHAIN_GAIN_CTL.
 *
 * bb_ant_div_[alt/main]_gaintb  - 0 -> Antenna config Alt/Main uses gaintable 0
 *                                 1 -> Antenna config Alt/Main uses gaintable 1
 *                                 Set in AR_PHY_MULTICHAIN_GAIN_CTL.
 *
 * bb_enable_ant_fast_div        - Enable fast antenna diversity.
 *                                 Set in AR_PHY_CCK_DETECT.
 *
 * bb_ant_div_[alt/main]_lnaconf - Alt/Main LNA diversity/combining input config.
 *                                 Set in AR_PHY_MULTICHAIN_GAIN_CTL.
 *                                 10=LNA1
 *                                 01=LNA2
 *                                 11=LNA1+LNA2
 *                                 00=LNA1-LNA2
 *
 * AR9485 / AR9565 / AR9331
 * ========================
 *
 * The same bits are present in the EEPROM, but the location in the
 * EEPROM is different (ant_div_control in ar9300_BaseExtension_1).
 *
 * ant_div_alt_lnaconf      ==> bit 0~1
 * ant_div_main_lnaconf     ==> bit 2~3
 * ant_div_alt_gaintb       ==> bit 4
 * ant_div_main_gaintb      ==> bit 5
 * enable_ant_div_lnadiv    ==> bit 6
 * enable_ant_fast_div      ==> bit 7
 */

static inline bool ath_is_alt_ant_ratio_better(struct ath_ant_comb *antcomb,
					       int alt_ratio, int maxdelta,
					       int mindelta, int main_rssi_avg,
					       int alt_rssi_avg, int pkt_count)
{
	if (pkt_count <= 50)
		return false;

	if (alt_rssi_avg > main_rssi_avg + mindelta)
		return true;

	if (alt_ratio >= antcomb->ant_ratio2 &&
	    alt_rssi_avg >= antcomb->low_rssi_thresh &&
	    (alt_rssi_avg > main_rssi_avg + maxdelta))
		return true;

	return false;
}

static inline bool ath_ant_div_comb_alt_check(struct ath_hw_antcomb_conf *conf,
					      struct ath_ant_comb *antcomb,
					      int alt_ratio, int alt_rssi_avg,
					      int main_rssi_avg)
{
	bool result, set1, set2;

	result = set1 = set2 = false;

	if (conf->main_lna_conf == ATH_ANT_DIV_COMB_LNA2 &&
	    conf->alt_lna_conf == ATH_ANT_DIV_COMB_LNA1)
		set1 = true;

	if (conf->main_lna_conf == ATH_ANT_DIV_COMB_LNA1 &&
	    conf->alt_lna_conf == ATH_ANT_DIV_COMB_LNA2)
		set2 = true;

	switch (conf->div_group) {
	case 0:
		if (alt_ratio > ATH_ANT_DIV_COMB_ALT_ANT_RATIO)
			result = true;
		break;
	case 1:
	case 2:
		if (alt_rssi_avg < 4 || alt_rssi_avg < antcomb->low_rssi_thresh)
			break;

		if ((set1 && (alt_rssi_avg >= (main_rssi_avg - 5))) ||
		    (set2 && (alt_rssi_avg >= (main_rssi_avg - 2))) ||
		    (alt_ratio > antcomb->ant_ratio))
			result = true;

		break;
	case 3:
		if (alt_rssi_avg < 4 || alt_rssi_avg < antcomb->low_rssi_thresh)
			break;

		if ((set1 && (alt_rssi_avg >= (main_rssi_avg - 3))) ||
		    (set2 && (alt_rssi_avg >= (main_rssi_avg + 3))) ||
		    (alt_ratio > antcomb->ant_ratio))
			result = true;

		break;
	}

	return result;
}

static void ath_lnaconf_alt_good_scan(struct ath_ant_comb *antcomb,
				      struct ath_hw_antcomb_conf ant_conf,
				      int main_rssi_avg)
{
	antcomb->quick_scan_cnt = 0;

	if (ant_conf.main_lna_conf == ATH_ANT_DIV_COMB_LNA2)
		antcomb->rssi_lna2 = main_rssi_avg;
	else if (ant_conf.main_lna_conf == ATH_ANT_DIV_COMB_LNA1)
		antcomb->rssi_lna1 = main_rssi_avg;

	switch ((ant_conf.main_lna_conf << 4) | ant_conf.alt_lna_conf) {
	case 0x10: /* LNA2 A-B */
		antcomb->main_conf = ATH_ANT_DIV_COMB_LNA1_MINUS_LNA2;
		antcomb->first_quick_scan_conf =
			ATH_ANT_DIV_COMB_LNA1_PLUS_LNA2;
		antcomb->second_quick_scan_conf = ATH_ANT_DIV_COMB_LNA1;
		break;
	case 0x20: /* LNA1 A-B */
		antcomb->main_conf = ATH_ANT_DIV_COMB_LNA1_MINUS_LNA2;
		antcomb->first_quick_scan_conf =
			ATH_ANT_DIV_COMB_LNA1_PLUS_LNA2;
		antcomb->second_quick_scan_conf = ATH_ANT_DIV_COMB_LNA2;
		break;
	case 0x21: /* LNA1 LNA2 */
		antcomb->main_conf = ATH_ANT_DIV_COMB_LNA2;
		antcomb->first_quick_scan_conf =
			ATH_ANT_DIV_COMB_LNA1_MINUS_LNA2;
		antcomb->second_quick_scan_conf =
			ATH_ANT_DIV_COMB_LNA1_PLUS_LNA2;
		break;
	case 0x12: /* LNA2 LNA1 */
		antcomb->main_conf = ATH_ANT_DIV_COMB_LNA1;
		antcomb->first_quick_scan_conf =
			ATH_ANT_DIV_COMB_LNA1_MINUS_LNA2;
		antcomb->second_quick_scan_conf =
			ATH_ANT_DIV_COMB_LNA1_PLUS_LNA2;
		break;
	case 0x13: /* LNA2 A+B */
		antcomb->main_conf = ATH_ANT_DIV_COMB_LNA1_PLUS_LNA2;
		antcomb->first_quick_scan_conf =
			ATH_ANT_DIV_COMB_LNA1_MINUS_LNA2;
		antcomb->second_quick_scan_conf = ATH_ANT_DIV_COMB_LNA1;
		break;
	case 0x23: /* LNA1 A+B */
		antcomb->main_conf = ATH_ANT_DIV_COMB_LNA1_PLUS_LNA2;
		antcomb->first_quick_scan_conf =
			ATH_ANT_DIV_COMB_LNA1_MINUS_LNA2;
		antcomb->second_quick_scan_conf = ATH_ANT_DIV_COMB_LNA2;
		break;
	default:
		break;
	}
}

static void ath_ant_set_alt_ratio(struct ath_ant_comb *antcomb,
				  struct ath_hw_antcomb_conf *conf)
{
	/* set alt to the conf with maximum ratio */
	if (antcomb->first_ratio && antcomb->second_ratio) {
		if (antcomb->rssi_second > antcomb->rssi_third) {
			/* first alt*/
			if ((antcomb->first_quick_scan_conf == ATH_ANT_DIV_COMB_LNA1) ||
			    (antcomb->first_quick_scan_conf == ATH_ANT_DIV_COMB_LNA2))
				/* Set alt LNA1 or LNA2*/
				if (conf->main_lna_conf == ATH_ANT_DIV_COMB_LNA2)
					conf->alt_lna_conf = ATH_ANT_DIV_COMB_LNA1;
				else
					conf->alt_lna_conf = ATH_ANT_DIV_COMB_LNA2;
			else
				/* Set alt to A+B or A-B */
				conf->alt_lna_conf =
					antcomb->first_quick_scan_conf;
		} else if ((antcomb->second_quick_scan_conf == ATH_ANT_DIV_COMB_LNA1) ||
			   (antcomb->second_quick_scan_conf == ATH_ANT_DIV_COMB_LNA2)) {
			/* Set alt LNA1 or LNA2 */
			if (conf->main_lna_conf == ATH_ANT_DIV_COMB_LNA2)
				conf->alt_lna_conf = ATH_ANT_DIV_COMB_LNA1;
			else
				conf->alt_lna_conf = ATH_ANT_DIV_COMB_LNA2;
		} else {
			/* Set alt to A+B or A-B */
			conf->alt_lna_conf = antcomb->second_quick_scan_conf;
		}
	} else if (antcomb->first_ratio) {
		/* first alt */
		if ((antcomb->first_quick_scan_conf == ATH_ANT_DIV_COMB_LNA1) ||
		    (antcomb->first_quick_scan_conf == ATH_ANT_DIV_COMB_LNA2))
			/* Set alt LNA1 or LNA2 */
			if (conf->main_lna_conf == ATH_ANT_DIV_COMB_LNA2)
				conf->alt_lna_conf = ATH_ANT_DIV_COMB_LNA1;
			else
				conf->alt_lna_conf = ATH_ANT_DIV_COMB_LNA2;
		else
			/* Set alt to A+B or A-B */
			conf->alt_lna_conf = antcomb->first_quick_scan_conf;
	} else if (antcomb->second_ratio) {
		/* second alt */
		if ((antcomb->second_quick_scan_conf == ATH_ANT_DIV_COMB_LNA1) ||
		    (antcomb->second_quick_scan_conf == ATH_ANT_DIV_COMB_LNA2))
			/* Set alt LNA1 or LNA2 */
			if (conf->main_lna_conf == ATH_ANT_DIV_COMB_LNA2)
				conf->alt_lna_conf = ATH_ANT_DIV_COMB_LNA1;
			else
				conf->alt_lna_conf = ATH_ANT_DIV_COMB_LNA2;
		else
			/* Set alt to A+B or A-B */
			conf->alt_lna_conf = antcomb->second_quick_scan_conf;
	} else {
		/* main is largest */
		if ((antcomb->main_conf == ATH_ANT_DIV_COMB_LNA1) ||
		    (antcomb->main_conf == ATH_ANT_DIV_COMB_LNA2))
			/* Set alt LNA1 or LNA2 */
			if (conf->main_lna_conf == ATH_ANT_DIV_COMB_LNA2)
				conf->alt_lna_conf = ATH_ANT_DIV_COMB_LNA1;
			else
				conf->alt_lna_conf = ATH_ANT_DIV_COMB_LNA2;
		else
			/* Set alt to A+B or A-B */
			conf->alt_lna_conf = antcomb->main_conf;
	}
}

static void ath_select_ant_div_from_quick_scan(struct ath_ant_comb *antcomb,
				       struct ath_hw_antcomb_conf *div_ant_conf,
				       int main_rssi_avg, int alt_rssi_avg,
				       int alt_ratio)
{
	/* alt_good */
	switch (antcomb->quick_scan_cnt) {
	case 0:
		/* set alt to main, and alt to first conf */
		div_ant_conf->main_lna_conf = antcomb->main_conf;
		div_ant_conf->alt_lna_conf = antcomb->first_quick_scan_conf;
		break;
	case 1:
		/* set alt to main, and alt to first conf */
		div_ant_conf->main_lna_conf = antcomb->main_conf;
		div_ant_conf->alt_lna_conf = antcomb->second_quick_scan_conf;
		antcomb->rssi_first = main_rssi_avg;
		antcomb->rssi_second = alt_rssi_avg;

		if (antcomb->main_conf == ATH_ANT_DIV_COMB_LNA1) {
			/* main is LNA1 */
			if (ath_is_alt_ant_ratio_better(antcomb, alt_ratio,
						ATH_ANT_DIV_COMB_LNA1_DELTA_HI,
						ATH_ANT_DIV_COMB_LNA1_DELTA_LOW,
						main_rssi_avg, alt_rssi_avg,
						antcomb->total_pkt_count))
				antcomb->first_ratio = true;
			else
				antcomb->first_ratio = false;
		} else if (antcomb->main_conf == ATH_ANT_DIV_COMB_LNA2) {
			if (ath_is_alt_ant_ratio_better(antcomb, alt_ratio,
						ATH_ANT_DIV_COMB_LNA1_DELTA_MID,
						ATH_ANT_DIV_COMB_LNA1_DELTA_LOW,
						main_rssi_avg, alt_rssi_avg,
						antcomb->total_pkt_count))
				antcomb->first_ratio = true;
			else
				antcomb->first_ratio = false;
		} else {
			if (ath_is_alt_ant_ratio_better(antcomb, alt_ratio,
						ATH_ANT_DIV_COMB_LNA1_DELTA_HI,
						0,
						main_rssi_avg, alt_rssi_avg,
						antcomb->total_pkt_count))
				antcomb->first_ratio = true;
			else
				antcomb->first_ratio = false;
		}
		break;
	case 2:
		antcomb->alt_good = false;
		antcomb->scan_not_start = false;
		antcomb->scan = false;
		antcomb->rssi_first = main_rssi_avg;
		antcomb->rssi_third = alt_rssi_avg;

		switch(antcomb->second_quick_scan_conf) {
		case ATH_ANT_DIV_COMB_LNA1:
			antcomb->rssi_lna1 = alt_rssi_avg;
			break;
		case ATH_ANT_DIV_COMB_LNA2:
			antcomb->rssi_lna2 = alt_rssi_avg;
			break;
		case ATH_ANT_DIV_COMB_LNA1_PLUS_LNA2:
			if (antcomb->main_conf == ATH_ANT_DIV_COMB_LNA2)
				antcomb->rssi_lna2 = main_rssi_avg;
			else if (antcomb->main_conf == ATH_ANT_DIV_COMB_LNA1)
				antcomb->rssi_lna1 = main_rssi_avg;
			break;
		default:
			break;
		}

		if (antcomb->rssi_lna2 > antcomb->rssi_lna1 +
		    div_ant_conf->lna1_lna2_switch_delta)
			div_ant_conf->main_lna_conf = ATH_ANT_DIV_COMB_LNA2;
		else
			div_ant_conf->main_lna_conf = ATH_ANT_DIV_COMB_LNA1;

		if (antcomb->main_conf == ATH_ANT_DIV_COMB_LNA1) {
			if (ath_is_alt_ant_ratio_better(antcomb, alt_ratio,
						ATH_ANT_DIV_COMB_LNA1_DELTA_HI,
						ATH_ANT_DIV_COMB_LNA1_DELTA_LOW,
						main_rssi_avg, alt_rssi_avg,
						antcomb->total_pkt_count))
				antcomb->second_ratio = true;
			else
				antcomb->second_ratio = false;
		} else if (antcomb->main_conf == ATH_ANT_DIV_COMB_LNA2) {
			if (ath_is_alt_ant_ratio_better(antcomb, alt_ratio,
						ATH_ANT_DIV_COMB_LNA1_DELTA_MID,
						ATH_ANT_DIV_COMB_LNA1_DELTA_LOW,
						main_rssi_avg, alt_rssi_avg,
						antcomb->total_pkt_count))
				antcomb->second_ratio = true;
			else
				antcomb->second_ratio = false;
		} else {
			if (ath_is_alt_ant_ratio_better(antcomb, alt_ratio,
						ATH_ANT_DIV_COMB_LNA1_DELTA_HI,
						0,
						main_rssi_avg, alt_rssi_avg,
						antcomb->total_pkt_count))
				antcomb->second_ratio = true;
			else
				antcomb->second_ratio = false;
		}

		ath_ant_set_alt_ratio(antcomb, div_ant_conf);

		break;
	default:
		break;
	}
}

static void ath_ant_div_conf_fast_divbias(struct ath_hw_antcomb_conf *ant_conf,
					  struct ath_ant_comb *antcomb,
					  int alt_ratio)
{
	ant_conf->main_gaintb = 0;
	ant_conf->alt_gaintb = 0;

	if (ant_conf->div_group == 0) {
		/* Adjust the fast_div_bias based on main and alt lna conf */
		switch ((ant_conf->main_lna_conf << 4) |
				ant_conf->alt_lna_conf) {
		case 0x01: /* A-B LNA2 */
			ant_conf->fast_div_bias = 0x3b;
			break;
		case 0x02: /* A-B LNA1 */
			ant_conf->fast_div_bias = 0x3d;
			break;
		case 0x03: /* A-B A+B */
			ant_conf->fast_div_bias = 0x1;
			break;
		case 0x10: /* LNA2 A-B */
			ant_conf->fast_div_bias = 0x7;
			break;
		case 0x12: /* LNA2 LNA1 */
			ant_conf->fast_div_bias = 0x2;
			break;
		case 0x13: /* LNA2 A+B */
			ant_conf->fast_div_bias = 0x7;
			break;
		case 0x20: /* LNA1 A-B */
			ant_conf->fast_div_bias = 0x6;
			break;
		case 0x21: /* LNA1 LNA2 */
			ant_conf->fast_div_bias = 0x0;
			break;
		case 0x23: /* LNA1 A+B */
			ant_conf->fast_div_bias = 0x6;
			break;
		case 0x30: /* A+B A-B */
			ant_conf->fast_div_bias = 0x1;
			break;
		case 0x31: /* A+B LNA2 */
			ant_conf->fast_div_bias = 0x3b;
			break;
		case 0x32: /* A+B LNA1 */
			ant_conf->fast_div_bias = 0x3d;
			break;
		default:
			break;
		}
	} else if (ant_conf->div_group == 1) {
		/* Adjust the fast_div_bias based on main and alt_lna_conf */
		switch ((ant_conf->main_lna_conf << 4) |
			ant_conf->alt_lna_conf) {
		case 0x01: /* A-B LNA2 */
			ant_conf->fast_div_bias = 0x1;
			break;
		case 0x02: /* A-B LNA1 */
			ant_conf->fast_div_bias = 0x1;
			break;
		case 0x03: /* A-B A+B */
			ant_conf->fast_div_bias = 0x1;
			break;
		case 0x10: /* LNA2 A-B */
			if (!(antcomb->scan) &&
			    (alt_ratio > ATH_ANT_DIV_COMB_ALT_ANT_RATIO))
				ant_conf->fast_div_bias = 0x3f;
			else
				ant_conf->fast_div_bias = 0x1;
			break;
		case 0x12: /* LNA2 LNA1 */
			ant_conf->fast_div_bias = 0x1;
			break;
		case 0x13: /* LNA2 A+B */
			if (!(antcomb->scan) &&
			    (alt_ratio > ATH_ANT_DIV_COMB_ALT_ANT_RATIO))
				ant_conf->fast_div_bias = 0x3f;
			else
				ant_conf->fast_div_bias = 0x1;
			break;
		case 0x20: /* LNA1 A-B */
			if (!(antcomb->scan) &&
			    (alt_ratio > ATH_ANT_DIV_COMB_ALT_ANT_RATIO))
				ant_conf->fast_div_bias = 0x3f;
			else
				ant_conf->fast_div_bias = 0x1;
			break;
		case 0x21: /* LNA1 LNA2 */
			ant_conf->fast_div_bias = 0x1;
			break;
		case 0x23: /* LNA1 A+B */
			if (!(antcomb->scan) &&
			    (alt_ratio > ATH_ANT_DIV_COMB_ALT_ANT_RATIO))
				ant_conf->fast_div_bias = 0x3f;
			else
				ant_conf->fast_div_bias = 0x1;
			break;
		case 0x30: /* A+B A-B */
			ant_conf->fast_div_bias = 0x1;
			break;
		case 0x31: /* A+B LNA2 */
			ant_conf->fast_div_bias = 0x1;
			break;
		case 0x32: /* A+B LNA1 */
			ant_conf->fast_div_bias = 0x1;
			break;
		default:
			break;
		}
	} else if (ant_conf->div_group == 2) {
		/* Adjust the fast_div_bias based on main and alt_lna_conf */
		switch ((ant_conf->main_lna_conf << 4) |
				ant_conf->alt_lna_conf) {
		case 0x01: /* A-B LNA2 */
			ant_conf->fast_div_bias = 0x1;
			break;
		case 0x02: /* A-B LNA1 */
			ant_conf->fast_div_bias = 0x1;
			break;
		case 0x03: /* A-B A+B */
			ant_conf->fast_div_bias = 0x1;
			break;
		case 0x10: /* LNA2 A-B */
			if (!antcomb->scan && (alt_ratio > antcomb->ant_ratio))
				ant_conf->fast_div_bias = 0x1;
			else
				ant_conf->fast_div_bias = 0x2;
			break;
		case 0x12: /* LNA2 LNA1 */
			ant_conf->fast_div_bias = 0x1;
			break;
		case 0x13: /* LNA2 A+B */
			if (!antcomb->scan && (alt_ratio > antcomb->ant_ratio))
				ant_conf->fast_div_bias = 0x1;
			else
				ant_conf->fast_div_bias = 0x2;
			break;
		case 0x20: /* LNA1 A-B */
			if (!antcomb->scan && (alt_ratio > antcomb->ant_ratio))
				ant_conf->fast_div_bias = 0x1;
			else
				ant_conf->fast_div_bias = 0x2;
			break;
		case 0x21: /* LNA1 LNA2 */
			ant_conf->fast_div_bias = 0x1;
			break;
		case 0x23: /* LNA1 A+B */
			if (!antcomb->scan && (alt_ratio > antcomb->ant_ratio))
				ant_conf->fast_div_bias = 0x1;
			else
				ant_conf->fast_div_bias = 0x2;
			break;
		case 0x30: /* A+B A-B */
			ant_conf->fast_div_bias = 0x1;
			break;
		case 0x31: /* A+B LNA2 */
			ant_conf->fast_div_bias = 0x1;
			break;
		case 0x32: /* A+B LNA1 */
			ant_conf->fast_div_bias = 0x1;
			break;
		default:
			break;
		}

		if (antcomb->fast_div_bias)
			ant_conf->fast_div_bias = antcomb->fast_div_bias;
	} else if (ant_conf->div_group == 3) {
		switch ((ant_conf->main_lna_conf << 4) |
			ant_conf->alt_lna_conf) {
		case 0x01: /* A-B LNA2 */
			ant_conf->fast_div_bias = 0x1;
			break;
		case 0x02: /* A-B LNA1 */
			ant_conf->fast_div_bias = 0x39;
			break;
		case 0x03: /* A-B A+B */
			ant_conf->fast_div_bias = 0x1;
			break;
		case 0x10: /* LNA2 A-B */
			ant_conf->fast_div_bias = 0x2;
			break;
		case 0x12: /* LNA2 LNA1 */
			ant_conf->fast_div_bias = 0x3f;
			break;
		case 0x13: /* LNA2 A+B */
			ant_conf->fast_div_bias = 0x2;
			break;
		case 0x20: /* LNA1 A-B */
			ant_conf->fast_div_bias = 0x3;
			break;
		case 0x21: /* LNA1 LNA2 */
			ant_conf->fast_div_bias = 0x3;
			break;
		case 0x23: /* LNA1 A+B */
			ant_conf->fast_div_bias = 0x3;
			break;
		case 0x30: /* A+B A-B */
			ant_conf->fast_div_bias = 0x1;
			break;
		case 0x31: /* A+B LNA2 */
			ant_conf->fast_div_bias = 0x6;
			break;
		case 0x32: /* A+B LNA1 */
			ant_conf->fast_div_bias = 0x1;
			break;
		default:
			break;
		}
	}
}

static void ath_ant_try_scan(struct ath_ant_comb *antcomb,
			     struct ath_hw_antcomb_conf *conf,
			     int curr_alt_set, int alt_rssi_avg,
			     int main_rssi_avg)
{
	switch (curr_alt_set) {
	case ATH_ANT_DIV_COMB_LNA2:
		antcomb->rssi_lna2 = alt_rssi_avg;
		antcomb->rssi_lna1 = main_rssi_avg;
		antcomb->scan = true;
		/* set to A+B */
		conf->main_lna_conf = ATH_ANT_DIV_COMB_LNA1;
		conf->alt_lna_conf = ATH_ANT_DIV_COMB_LNA1_PLUS_LNA2;
		break;
	case ATH_ANT_DIV_COMB_LNA1:
		antcomb->rssi_lna1 = alt_rssi_avg;
		antcomb->rssi_lna2 = main_rssi_avg;
		antcomb->scan = true;
		/* set to A+B */
		conf->main_lna_conf = ATH_ANT_DIV_COMB_LNA2;
		conf->alt_lna_conf = ATH_ANT_DIV_COMB_LNA1_PLUS_LNA2;
		break;
	case ATH_ANT_DIV_COMB_LNA1_PLUS_LNA2:
		antcomb->rssi_add = alt_rssi_avg;
		antcomb->scan = true;
		/* set to A-B */
		conf->alt_lna_conf = ATH_ANT_DIV_COMB_LNA1_MINUS_LNA2;
		break;
	case ATH_ANT_DIV_COMB_LNA1_MINUS_LNA2:
		antcomb->rssi_sub = alt_rssi_avg;
		antcomb->scan = false;
		if (antcomb->rssi_lna2 >
		    (antcomb->rssi_lna1 + conf->lna1_lna2_switch_delta)) {
			/* use LNA2 as main LNA */
			if ((antcomb->rssi_add > antcomb->rssi_lna1) &&
			    (antcomb->rssi_add > antcomb->rssi_sub)) {
				/* set to A+B */
				conf->main_lna_conf = ATH_ANT_DIV_COMB_LNA2;
				conf->alt_lna_conf = ATH_ANT_DIV_COMB_LNA1_PLUS_LNA2;
			} else if (antcomb->rssi_sub >
				   antcomb->rssi_lna1) {
				/* set to A-B */
				conf->main_lna_conf = ATH_ANT_DIV_COMB_LNA2;
				conf->alt_lna_conf = ATH_ANT_DIV_COMB_LNA1_MINUS_LNA2;
			} else {
				/* set to LNA1 */
				conf->main_lna_conf = ATH_ANT_DIV_COMB_LNA2;
				conf->alt_lna_conf = ATH_ANT_DIV_COMB_LNA1;
			}
		} else {
			/* use LNA1 as main LNA */
			if ((antcomb->rssi_add > antcomb->rssi_lna2) &&
			    (antcomb->rssi_add > antcomb->rssi_sub)) {
				/* set to A+B */
				conf->main_lna_conf = ATH_ANT_DIV_COMB_LNA1;
				conf->alt_lna_conf = ATH_ANT_DIV_COMB_LNA1_PLUS_LNA2;
			} else if (antcomb->rssi_sub >
				   antcomb->rssi_lna2) {
				/* set to A-B */
				conf->main_lna_conf = ATH_ANT_DIV_COMB_LNA1;
				conf->alt_lna_conf = ATH_ANT_DIV_COMB_LNA1_MINUS_LNA2;
			} else {
				/* set to LNA2 */
				conf->main_lna_conf = ATH_ANT_DIV_COMB_LNA1;
				conf->alt_lna_conf = ATH_ANT_DIV_COMB_LNA2;
			}
		}
		break;
	default:
		break;
	}
}

static bool ath_ant_try_switch(struct ath_hw_antcomb_conf *div_ant_conf,
			       struct ath_ant_comb *antcomb,
			       int alt_ratio, int alt_rssi_avg,
			       int main_rssi_avg, int curr_main_set,
			       int curr_alt_set)
{
	bool ret = false;

	if (ath_ant_div_comb_alt_check(div_ant_conf, antcomb, alt_ratio,
				       alt_rssi_avg, main_rssi_avg)) {
		if (curr_alt_set == ATH_ANT_DIV_COMB_LNA2) {
			/*
			 * Switch main and alt LNA.
			 */
			div_ant_conf->main_lna_conf = ATH_ANT_DIV_COMB_LNA2;
			div_ant_conf->alt_lna_conf = ATH_ANT_DIV_COMB_LNA1;
		} else if (curr_alt_set == ATH_ANT_DIV_COMB_LNA1) {
			div_ant_conf->main_lna_conf = ATH_ANT_DIV_COMB_LNA1;
			div_ant_conf->alt_lna_conf = ATH_ANT_DIV_COMB_LNA2;
		}

		ret = true;
	} else if ((curr_alt_set != ATH_ANT_DIV_COMB_LNA1) &&
		   (curr_alt_set != ATH_ANT_DIV_COMB_LNA2)) {
		/*
		  Set alt to another LNA.
		*/
		if (curr_main_set == ATH_ANT_DIV_COMB_LNA2)
			div_ant_conf->alt_lna_conf = ATH_ANT_DIV_COMB_LNA1;
		else if (curr_main_set == ATH_ANT_DIV_COMB_LNA1)
			div_ant_conf->alt_lna_conf = ATH_ANT_DIV_COMB_LNA2;

		ret = true;
	}

	return ret;
}

static bool ath_ant_short_scan_check(struct ath_ant_comb *antcomb)
{
	int alt_ratio;

	if (!antcomb->scan || !antcomb->alt_good)
		return false;

	if (time_after(jiffies, antcomb->scan_start_time +
		       msecs_to_jiffies(ATH_ANT_DIV_COMB_SHORT_SCAN_INTR)))
		return true;

	if (antcomb->total_pkt_count == ATH_ANT_DIV_COMB_SHORT_SCAN_PKTCOUNT) {
		alt_ratio = ((antcomb->alt_recv_cnt * 100) /
			     antcomb->total_pkt_count);
		if (alt_ratio < antcomb->ant_ratio)
			return true;
	}

	return false;
}

void ath_ant_comb_scan(struct ath_softc *sc, struct ath_rx_status *rs)
{
	struct ath_hw_antcomb_conf div_ant_conf;
	struct ath_ant_comb *antcomb = &sc->ant_comb;
	int alt_ratio = 0, alt_rssi_avg = 0, main_rssi_avg = 0, curr_alt_set;
	int curr_main_set;
	int main_rssi = rs->rs_rssi_ctl[0];
	int alt_rssi = rs->rs_rssi_ctl[1];
	int rx_ant_conf,  main_ant_conf;
	bool short_scan = false, ret;

	rx_ant_conf = (rs->rs_rssi_ctl[2] >> ATH_ANT_RX_CURRENT_SHIFT) &
		       ATH_ANT_RX_MASK;
	main_ant_conf = (rs->rs_rssi_ctl[2] >> ATH_ANT_RX_MAIN_SHIFT) &
			 ATH_ANT_RX_MASK;

	if (alt_rssi >= antcomb->low_rssi_thresh) {
		antcomb->ant_ratio = ATH_ANT_DIV_COMB_ALT_ANT_RATIO;
		antcomb->ant_ratio2 = ATH_ANT_DIV_COMB_ALT_ANT_RATIO2;
	} else {
		antcomb->ant_ratio = ATH_ANT_DIV_COMB_ALT_ANT_RATIO_LOW_RSSI;
		antcomb->ant_ratio2 = ATH_ANT_DIV_COMB_ALT_ANT_RATIO2_LOW_RSSI;
	}

	/* Record packet only when both main_rssi and  alt_rssi is positive */
	if (main_rssi > 0 && alt_rssi > 0) {
		antcomb->total_pkt_count++;
		antcomb->main_total_rssi += main_rssi;
		antcomb->alt_total_rssi  += alt_rssi;

		if (main_ant_conf == rx_ant_conf)
			antcomb->main_recv_cnt++;
		else
			antcomb->alt_recv_cnt++;
	}

	if (main_ant_conf == rx_ant_conf) {
		ANT_STAT_INC(sc, ANT_MAIN, recv_cnt);
		ANT_LNA_INC(sc, ANT_MAIN, rx_ant_conf);
	} else {
		ANT_STAT_INC(sc, ANT_ALT, recv_cnt);
		ANT_LNA_INC(sc, ANT_ALT, rx_ant_conf);
	}

	/* Short scan check */
	short_scan = ath_ant_short_scan_check(antcomb);

	if (((antcomb->total_pkt_count < ATH_ANT_DIV_COMB_MAX_PKTCOUNT) ||
	     rs->rs_moreaggr) && !short_scan)
		return;

	if (antcomb->total_pkt_count) {
		alt_ratio = ((antcomb->alt_recv_cnt * 100) /
			     antcomb->total_pkt_count);
		main_rssi_avg = (antcomb->main_total_rssi /
				 antcomb->total_pkt_count);
		alt_rssi_avg = (antcomb->alt_total_rssi /
				 antcomb->total_pkt_count);
	}

	ath9k_hw_antdiv_comb_conf_get(sc->sc_ah, &div_ant_conf);
	curr_alt_set = div_ant_conf.alt_lna_conf;
	curr_main_set = div_ant_conf.main_lna_conf;
	antcomb->count++;

	if (antcomb->count == ATH_ANT_DIV_COMB_MAX_COUNT) {
		if (alt_ratio > antcomb->ant_ratio) {
			ath_lnaconf_alt_good_scan(antcomb, div_ant_conf,
						  main_rssi_avg);
			antcomb->alt_good = true;
		} else {
			antcomb->alt_good = false;
		}

		antcomb->count = 0;
		antcomb->scan = true;
		antcomb->scan_not_start = true;
	}

	if (!antcomb->scan) {
		ret = ath_ant_try_switch(&div_ant_conf, antcomb, alt_ratio,
					 alt_rssi_avg, main_rssi_avg,
					 curr_main_set, curr_alt_set);
		if (ret)
			goto div_comb_done;
	}

	if (!antcomb->scan &&
	    (alt_rssi_avg < (main_rssi_avg + div_ant_conf.lna1_lna2_delta)))
		goto div_comb_done;

	if (!antcomb->scan_not_start) {
		ath_ant_try_scan(antcomb, &div_ant_conf, curr_alt_set,
				 alt_rssi_avg, main_rssi_avg);
	} else {
		if (!antcomb->alt_good) {
			antcomb->scan_not_start = false;
			/* Set alt to another LNA */
			if (curr_main_set == ATH_ANT_DIV_COMB_LNA2) {
				div_ant_conf.main_lna_conf =
					ATH_ANT_DIV_COMB_LNA2;
				div_ant_conf.alt_lna_conf =
					ATH_ANT_DIV_COMB_LNA1;
			} else if (curr_main_set == ATH_ANT_DIV_COMB_LNA1) {
				div_ant_conf.main_lna_conf =
					ATH_ANT_DIV_COMB_LNA1;
				div_ant_conf.alt_lna_conf =
					ATH_ANT_DIV_COMB_LNA2;
			}
			goto div_comb_done;
		}
		ath_select_ant_div_from_quick_scan(antcomb, &div_ant_conf,
						   main_rssi_avg, alt_rssi_avg,
						   alt_ratio);
		antcomb->quick_scan_cnt++;
	}

div_comb_done:
	ath_ant_div_conf_fast_divbias(&div_ant_conf, antcomb, alt_ratio);
	ath9k_hw_antdiv_comb_conf_set(sc->sc_ah, &div_ant_conf);
	ath9k_debug_stat_ant(sc, &div_ant_conf, main_rssi_avg, alt_rssi_avg);

	antcomb->scan_start_time = jiffies;
	antcomb->total_pkt_count = 0;
	antcomb->main_total_rssi = 0;
	antcomb->alt_total_rssi = 0;
	antcomb->main_recv_cnt = 0;
	antcomb->alt_recv_cnt = 0;
}
