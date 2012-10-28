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

static inline bool ath_is_alt_ant_ratio_better(int alt_ratio, int maxdelta,
					       int mindelta, int main_rssi_avg,
					       int alt_rssi_avg, int pkt_count)
{
	return (((alt_ratio >= ATH_ANT_DIV_COMB_ALT_ANT_RATIO2) &&
		 (alt_rssi_avg > main_rssi_avg + maxdelta)) ||
		(alt_rssi_avg > main_rssi_avg + mindelta)) && (pkt_count > 50);
}

static inline bool ath_ant_div_comb_alt_check(u8 div_group, int alt_ratio,
					      int curr_main_set, int curr_alt_set,
					      int alt_rssi_avg, int main_rssi_avg)
{
	bool result = false;
	switch (div_group) {
	case 0:
		if (alt_ratio > ATH_ANT_DIV_COMB_ALT_ANT_RATIO)
			result = true;
		break;
	case 1:
	case 2:
		if ((((curr_main_set == ATH_ANT_DIV_COMB_LNA2) &&
		      (curr_alt_set == ATH_ANT_DIV_COMB_LNA1) &&
		      (alt_rssi_avg >= (main_rssi_avg - 5))) ||
		     ((curr_main_set == ATH_ANT_DIV_COMB_LNA1) &&
		      (curr_alt_set == ATH_ANT_DIV_COMB_LNA2) &&
		      (alt_rssi_avg >= (main_rssi_avg - 2)))) &&
		    (alt_rssi_avg >= 4))
			result = true;
		else
			result = false;
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
			if (ath_is_alt_ant_ratio_better(alt_ratio,
						ATH_ANT_DIV_COMB_LNA1_DELTA_HI,
						ATH_ANT_DIV_COMB_LNA1_DELTA_LOW,
						main_rssi_avg, alt_rssi_avg,
						antcomb->total_pkt_count))
				antcomb->first_ratio = true;
			else
				antcomb->first_ratio = false;
		} else if (antcomb->main_conf == ATH_ANT_DIV_COMB_LNA2) {
			if (ath_is_alt_ant_ratio_better(alt_ratio,
						ATH_ANT_DIV_COMB_LNA1_DELTA_MID,
						ATH_ANT_DIV_COMB_LNA1_DELTA_LOW,
						main_rssi_avg, alt_rssi_avg,
						antcomb->total_pkt_count))
				antcomb->first_ratio = true;
			else
				antcomb->first_ratio = false;
		} else {
			if ((((alt_ratio >= ATH_ANT_DIV_COMB_ALT_ANT_RATIO2) &&
			      (alt_rssi_avg > main_rssi_avg +
			       ATH_ANT_DIV_COMB_LNA1_DELTA_HI)) ||
			     (alt_rssi_avg > main_rssi_avg)) &&
			    (antcomb->total_pkt_count > 50))
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

		if (antcomb->second_quick_scan_conf == ATH_ANT_DIV_COMB_LNA1)
			antcomb->rssi_lna1 = alt_rssi_avg;
		else if (antcomb->second_quick_scan_conf ==
			 ATH_ANT_DIV_COMB_LNA2)
			antcomb->rssi_lna2 = alt_rssi_avg;
		else if (antcomb->second_quick_scan_conf ==
			 ATH_ANT_DIV_COMB_LNA1_PLUS_LNA2) {
			if (antcomb->main_conf == ATH_ANT_DIV_COMB_LNA2)
				antcomb->rssi_lna2 = main_rssi_avg;
			else if (antcomb->main_conf == ATH_ANT_DIV_COMB_LNA1)
				antcomb->rssi_lna1 = main_rssi_avg;
		}

		if (antcomb->rssi_lna2 > antcomb->rssi_lna1 +
		    ATH_ANT_DIV_COMB_LNA1_LNA2_SWITCH_DELTA)
			div_ant_conf->main_lna_conf = ATH_ANT_DIV_COMB_LNA2;
		else
			div_ant_conf->main_lna_conf = ATH_ANT_DIV_COMB_LNA1;

		if (antcomb->main_conf == ATH_ANT_DIV_COMB_LNA1) {
			if (ath_is_alt_ant_ratio_better(alt_ratio,
						ATH_ANT_DIV_COMB_LNA1_DELTA_HI,
						ATH_ANT_DIV_COMB_LNA1_DELTA_LOW,
						main_rssi_avg, alt_rssi_avg,
						antcomb->total_pkt_count))
				antcomb->second_ratio = true;
			else
				antcomb->second_ratio = false;
		} else if (antcomb->main_conf == ATH_ANT_DIV_COMB_LNA2) {
			if (ath_is_alt_ant_ratio_better(alt_ratio,
						ATH_ANT_DIV_COMB_LNA1_DELTA_MID,
						ATH_ANT_DIV_COMB_LNA1_DELTA_LOW,
						main_rssi_avg, alt_rssi_avg,
						antcomb->total_pkt_count))
				antcomb->second_ratio = true;
			else
				antcomb->second_ratio = false;
		} else {
			if ((((alt_ratio >= ATH_ANT_DIV_COMB_ALT_ANT_RATIO2) &&
			      (alt_rssi_avg > main_rssi_avg +
			       ATH_ANT_DIV_COMB_LNA1_DELTA_HI)) ||
			     (alt_rssi_avg > main_rssi_avg)) &&
			    (antcomb->total_pkt_count > 50))
				antcomb->second_ratio = true;
			else
				antcomb->second_ratio = false;
		}

		/* set alt to the conf with maximun ratio */
		if (antcomb->first_ratio && antcomb->second_ratio) {
			if (antcomb->rssi_second > antcomb->rssi_third) {
				/* first alt*/
				if ((antcomb->first_quick_scan_conf ==
				    ATH_ANT_DIV_COMB_LNA1) ||
				    (antcomb->first_quick_scan_conf ==
				    ATH_ANT_DIV_COMB_LNA2))
					/* Set alt LNA1 or LNA2*/
					if (div_ant_conf->main_lna_conf ==
					    ATH_ANT_DIV_COMB_LNA2)
						div_ant_conf->alt_lna_conf =
							ATH_ANT_DIV_COMB_LNA1;
					else
						div_ant_conf->alt_lna_conf =
							ATH_ANT_DIV_COMB_LNA2;
				else
					/* Set alt to A+B or A-B */
					div_ant_conf->alt_lna_conf =
						antcomb->first_quick_scan_conf;
			} else if ((antcomb->second_quick_scan_conf ==
				   ATH_ANT_DIV_COMB_LNA1) ||
				   (antcomb->second_quick_scan_conf ==
				   ATH_ANT_DIV_COMB_LNA2)) {
				/* Set alt LNA1 or LNA2 */
				if (div_ant_conf->main_lna_conf ==
				    ATH_ANT_DIV_COMB_LNA2)
					div_ant_conf->alt_lna_conf =
						ATH_ANT_DIV_COMB_LNA1;
				else
					div_ant_conf->alt_lna_conf =
						ATH_ANT_DIV_COMB_LNA2;
			} else {
				/* Set alt to A+B or A-B */
				div_ant_conf->alt_lna_conf =
					antcomb->second_quick_scan_conf;
			}
		} else if (antcomb->first_ratio) {
			/* first alt */
			if ((antcomb->first_quick_scan_conf ==
			    ATH_ANT_DIV_COMB_LNA1) ||
			    (antcomb->first_quick_scan_conf ==
			    ATH_ANT_DIV_COMB_LNA2))
					/* Set alt LNA1 or LNA2 */
				if (div_ant_conf->main_lna_conf ==
				    ATH_ANT_DIV_COMB_LNA2)
					div_ant_conf->alt_lna_conf =
							ATH_ANT_DIV_COMB_LNA1;
				else
					div_ant_conf->alt_lna_conf =
							ATH_ANT_DIV_COMB_LNA2;
			else
				/* Set alt to A+B or A-B */
				div_ant_conf->alt_lna_conf =
						antcomb->first_quick_scan_conf;
		} else if (antcomb->second_ratio) {
				/* second alt */
			if ((antcomb->second_quick_scan_conf ==
			    ATH_ANT_DIV_COMB_LNA1) ||
			    (antcomb->second_quick_scan_conf ==
			    ATH_ANT_DIV_COMB_LNA2))
				/* Set alt LNA1 or LNA2 */
				if (div_ant_conf->main_lna_conf ==
				    ATH_ANT_DIV_COMB_LNA2)
					div_ant_conf->alt_lna_conf =
						ATH_ANT_DIV_COMB_LNA1;
				else
					div_ant_conf->alt_lna_conf =
						ATH_ANT_DIV_COMB_LNA2;
			else
				/* Set alt to A+B or A-B */
				div_ant_conf->alt_lna_conf =
						antcomb->second_quick_scan_conf;
		} else {
			/* main is largest */
			if ((antcomb->main_conf == ATH_ANT_DIV_COMB_LNA1) ||
			    (antcomb->main_conf == ATH_ANT_DIV_COMB_LNA2))
				/* Set alt LNA1 or LNA2 */
				if (div_ant_conf->main_lna_conf ==
				    ATH_ANT_DIV_COMB_LNA2)
					div_ant_conf->alt_lna_conf =
							ATH_ANT_DIV_COMB_LNA1;
				else
					div_ant_conf->alt_lna_conf =
							ATH_ANT_DIV_COMB_LNA2;
			else
				/* Set alt to A+B or A-B */
				div_ant_conf->alt_lna_conf = antcomb->main_conf;
		}
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
			if (!(antcomb->scan) &&
				(alt_ratio > ATH_ANT_DIV_COMB_ALT_ANT_RATIO))
				ant_conf->fast_div_bias = 0x1;
			else
				ant_conf->fast_div_bias = 0x2;
			break;
		case 0x12: /* LNA2 LNA1 */
			ant_conf->fast_div_bias = 0x1;
			break;
		case 0x13: /* LNA2 A+B */
			if (!(antcomb->scan) &&
				(alt_ratio > ATH_ANT_DIV_COMB_ALT_ANT_RATIO))
				ant_conf->fast_div_bias = 0x1;
			else
				ant_conf->fast_div_bias = 0x2;
			break;
		case 0x20: /* LNA1 A-B */
			if (!(antcomb->scan) &&
				(alt_ratio > ATH_ANT_DIV_COMB_ALT_ANT_RATIO))
				ant_conf->fast_div_bias = 0x1;
			else
				ant_conf->fast_div_bias = 0x2;
			break;
		case 0x21: /* LNA1 LNA2 */
			ant_conf->fast_div_bias = 0x1;
			break;
		case 0x23: /* LNA1 A+B */
			if (!(antcomb->scan) &&
				(alt_ratio > ATH_ANT_DIV_COMB_ALT_ANT_RATIO))
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
			if ((antcomb->scan == 0) &&
			    (alt_ratio > ATH_ANT_DIV_COMB_ALT_ANT_RATIO)) {
				ant_conf->fast_div_bias = 0x3f;
			} else {
				ant_conf->fast_div_bias = 0x1;
			}
			break;
		case 0x12: /* LNA2 LNA1 */
			ant_conf->fast_div_bias = 0x39;
			break;
		case 0x13: /* LNA2 A+B */
			if ((antcomb->scan == 0) &&
			    (alt_ratio > ATH_ANT_DIV_COMB_ALT_ANT_RATIO)) {
				ant_conf->fast_div_bias = 0x3f;
			} else {
				ant_conf->fast_div_bias = 0x1;
			}
			break;
		case 0x20: /* LNA1 A-B */
			if ((antcomb->scan == 0) &&
			    (alt_ratio > ATH_ANT_DIV_COMB_ALT_ANT_RATIO)) {
				ant_conf->fast_div_bias = 0x3f;
			} else {
				ant_conf->fast_div_bias = 0x4;
			}
			break;
		case 0x21: /* LNA1 LNA2 */
			ant_conf->fast_div_bias = 0x6;
			break;
		case 0x23: /* LNA1 A+B */
			if ((antcomb->scan == 0) &&
			    (alt_ratio > ATH_ANT_DIV_COMB_ALT_ANT_RATIO)) {
				ant_conf->fast_div_bias = 0x3f;
			} else {
				ant_conf->fast_div_bias = 0x6;
			}
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

void ath_ant_comb_scan(struct ath_softc *sc, struct ath_rx_status *rs)
{
	struct ath_hw_antcomb_conf div_ant_conf;
	struct ath_ant_comb *antcomb = &sc->ant_comb;
	int alt_ratio = 0, alt_rssi_avg = 0, main_rssi_avg = 0, curr_alt_set;
	int curr_main_set;
	int main_rssi = rs->rs_rssi_ctl0;
	int alt_rssi = rs->rs_rssi_ctl1;
	int rx_ant_conf,  main_ant_conf;
	bool short_scan = false;

	rx_ant_conf = (rs->rs_rssi_ctl2 >> ATH_ANT_RX_CURRENT_SHIFT) &
		       ATH_ANT_RX_MASK;
	main_ant_conf = (rs->rs_rssi_ctl2 >> ATH_ANT_RX_MAIN_SHIFT) &
			 ATH_ANT_RX_MASK;

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

	/* Short scan check */
	if (antcomb->scan && antcomb->alt_good) {
		if (time_after(jiffies, antcomb->scan_start_time +
		    msecs_to_jiffies(ATH_ANT_DIV_COMB_SHORT_SCAN_INTR)))
			short_scan = true;
		else
			if (antcomb->total_pkt_count ==
			    ATH_ANT_DIV_COMB_SHORT_SCAN_PKTCOUNT) {
				alt_ratio = ((antcomb->alt_recv_cnt * 100) /
					    antcomb->total_pkt_count);
				if (alt_ratio < ATH_ANT_DIV_COMB_ALT_ANT_RATIO)
					short_scan = true;
			}
	}

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
		if (alt_ratio > ATH_ANT_DIV_COMB_ALT_ANT_RATIO) {
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
		if (ath_ant_div_comb_alt_check(div_ant_conf.div_group,
					alt_ratio, curr_main_set, curr_alt_set,
					alt_rssi_avg, main_rssi_avg)) {
			if (curr_alt_set == ATH_ANT_DIV_COMB_LNA2) {
				/* Switch main and alt LNA */
				div_ant_conf.main_lna_conf =
						ATH_ANT_DIV_COMB_LNA2;
				div_ant_conf.alt_lna_conf  =
						ATH_ANT_DIV_COMB_LNA1;
			} else if (curr_alt_set == ATH_ANT_DIV_COMB_LNA1) {
				div_ant_conf.main_lna_conf =
						ATH_ANT_DIV_COMB_LNA1;
				div_ant_conf.alt_lna_conf  =
						ATH_ANT_DIV_COMB_LNA2;
			}

			goto div_comb_done;
		} else if ((curr_alt_set != ATH_ANT_DIV_COMB_LNA1) &&
			   (curr_alt_set != ATH_ANT_DIV_COMB_LNA2)) {
			/* Set alt to another LNA */
			if (curr_main_set == ATH_ANT_DIV_COMB_LNA2)
				div_ant_conf.alt_lna_conf =
						ATH_ANT_DIV_COMB_LNA1;
			else if (curr_main_set == ATH_ANT_DIV_COMB_LNA1)
				div_ant_conf.alt_lna_conf =
						ATH_ANT_DIV_COMB_LNA2;

			goto div_comb_done;
		}

		if ((alt_rssi_avg < (main_rssi_avg +
				     div_ant_conf.lna1_lna2_delta)))
			goto div_comb_done;
	}

	if (!antcomb->scan_not_start) {
		switch (curr_alt_set) {
		case ATH_ANT_DIV_COMB_LNA2:
			antcomb->rssi_lna2 = alt_rssi_avg;
			antcomb->rssi_lna1 = main_rssi_avg;
			antcomb->scan = true;
			/* set to A+B */
			div_ant_conf.main_lna_conf =
				ATH_ANT_DIV_COMB_LNA1;
			div_ant_conf.alt_lna_conf  =
				ATH_ANT_DIV_COMB_LNA1_PLUS_LNA2;
			break;
		case ATH_ANT_DIV_COMB_LNA1:
			antcomb->rssi_lna1 = alt_rssi_avg;
			antcomb->rssi_lna2 = main_rssi_avg;
			antcomb->scan = true;
			/* set to A+B */
			div_ant_conf.main_lna_conf = ATH_ANT_DIV_COMB_LNA2;
			div_ant_conf.alt_lna_conf  =
				ATH_ANT_DIV_COMB_LNA1_PLUS_LNA2;
			break;
		case ATH_ANT_DIV_COMB_LNA1_PLUS_LNA2:
			antcomb->rssi_add = alt_rssi_avg;
			antcomb->scan = true;
			/* set to A-B */
			div_ant_conf.alt_lna_conf =
				ATH_ANT_DIV_COMB_LNA1_MINUS_LNA2;
			break;
		case ATH_ANT_DIV_COMB_LNA1_MINUS_LNA2:
			antcomb->rssi_sub = alt_rssi_avg;
			antcomb->scan = false;
			if (antcomb->rssi_lna2 >
			    (antcomb->rssi_lna1 +
			    ATH_ANT_DIV_COMB_LNA1_LNA2_SWITCH_DELTA)) {
				/* use LNA2 as main LNA */
				if ((antcomb->rssi_add > antcomb->rssi_lna1) &&
				    (antcomb->rssi_add > antcomb->rssi_sub)) {
					/* set to A+B */
					div_ant_conf.main_lna_conf =
						ATH_ANT_DIV_COMB_LNA2;
					div_ant_conf.alt_lna_conf  =
						ATH_ANT_DIV_COMB_LNA1_PLUS_LNA2;
				} else if (antcomb->rssi_sub >
					   antcomb->rssi_lna1) {
					/* set to A-B */
					div_ant_conf.main_lna_conf =
						ATH_ANT_DIV_COMB_LNA2;
					div_ant_conf.alt_lna_conf =
						ATH_ANT_DIV_COMB_LNA1_MINUS_LNA2;
				} else {
					/* set to LNA1 */
					div_ant_conf.main_lna_conf =
						ATH_ANT_DIV_COMB_LNA2;
					div_ant_conf.alt_lna_conf =
						ATH_ANT_DIV_COMB_LNA1;
				}
			} else {
				/* use LNA1 as main LNA */
				if ((antcomb->rssi_add > antcomb->rssi_lna2) &&
				    (antcomb->rssi_add > antcomb->rssi_sub)) {
					/* set to A+B */
					div_ant_conf.main_lna_conf =
						ATH_ANT_DIV_COMB_LNA1;
					div_ant_conf.alt_lna_conf  =
						ATH_ANT_DIV_COMB_LNA1_PLUS_LNA2;
				} else if (antcomb->rssi_sub >
					   antcomb->rssi_lna1) {
					/* set to A-B */
					div_ant_conf.main_lna_conf =
						ATH_ANT_DIV_COMB_LNA1;
					div_ant_conf.alt_lna_conf =
						ATH_ANT_DIV_COMB_LNA1_MINUS_LNA2;
				} else {
					/* set to LNA2 */
					div_ant_conf.main_lna_conf =
						ATH_ANT_DIV_COMB_LNA1;
					div_ant_conf.alt_lna_conf =
						ATH_ANT_DIV_COMB_LNA2;
				}
			}
			break;
		default:
			break;
		}
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
	}

	ath_select_ant_div_from_quick_scan(antcomb, &div_ant_conf,
					   main_rssi_avg, alt_rssi_avg,
					   alt_ratio);

	antcomb->quick_scan_cnt++;

div_comb_done:
	ath_ant_div_conf_fast_divbias(&div_ant_conf, antcomb, alt_ratio);
	ath9k_hw_antdiv_comb_conf_set(sc->sc_ah, &div_ant_conf);

	antcomb->scan_start_time = jiffies;
	antcomb->total_pkt_count = 0;
	antcomb->main_total_rssi = 0;
	antcomb->alt_total_rssi = 0;
	antcomb->main_recv_cnt = 0;
	antcomb->alt_recv_cnt = 0;
}

void ath_ant_comb_update(struct ath_softc *sc)
{
	struct ath_hw *ah = sc->sc_ah;
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath_hw_antcomb_conf div_ant_conf;
	u8 lna_conf;

	ath9k_hw_antdiv_comb_conf_get(ah, &div_ant_conf);

	if (sc->ant_rx == 1)
		lna_conf = ATH_ANT_DIV_COMB_LNA1;
	else
		lna_conf = ATH_ANT_DIV_COMB_LNA2;

	div_ant_conf.main_lna_conf = lna_conf;
	div_ant_conf.alt_lna_conf = lna_conf;

	ath9k_hw_antdiv_comb_conf_set(ah, &div_ant_conf);

	if (common->antenna_diversity)
		ath9k_hw_antctrl_shared_chain_lnadiv(ah, true);
}
