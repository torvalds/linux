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

#include <brcmu_wifi.h>
#include <brcmu_utils.h>

#include "d11.h"
#include "pub.h"
#include "rate.h"

/* Rate info per rate: It tells whether a rate is ofdm or not and its phy_rate value */
const u8 rate_info[BRCM_MAXRATE + 1] = {
	/*  0     1     2     3     4     5     6     7     8     9 */
/*   0 */ 0x00, 0x00, 0x0a, 0x00, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00,
/*  10 */ 0x00, 0x37, 0x8b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x8f, 0x00,
/*  20 */ 0x00, 0x00, 0x6e, 0x00, 0x8a, 0x00, 0x00, 0x00, 0x00, 0x00,
/*  30 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x8e, 0x00, 0x00, 0x00,
/*  40 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x89, 0x00,
/*  50 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/*  60 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/*  70 */ 0x00, 0x00, 0x8d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/*  80 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/*  90 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x88, 0x00, 0x00, 0x00,
/* 100 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x8c
};

/* rates are in units of Kbps */
const struct brcms_mcs_info mcs_table[MCS_TABLE_SIZE] = {
	/* MCS  0: SS 1, MOD: BPSK,  CR 1/2 */
	{6500, 13500, CEIL(6500 * 10, 9), CEIL(13500 * 10, 9), 0x00,
	 BRCM_RATE_6M},
	/* MCS  1: SS 1, MOD: QPSK,  CR 1/2 */
	{13000, 27000, CEIL(13000 * 10, 9), CEIL(27000 * 10, 9), 0x08,
	 BRCM_RATE_12M},
	/* MCS  2: SS 1, MOD: QPSK,  CR 3/4 */
	{19500, 40500, CEIL(19500 * 10, 9), CEIL(40500 * 10, 9), 0x0A,
	 BRCM_RATE_18M},
	/* MCS  3: SS 1, MOD: 16QAM, CR 1/2 */
	{26000, 54000, CEIL(26000 * 10, 9), CEIL(54000 * 10, 9), 0x10,
	 BRCM_RATE_24M},
	/* MCS  4: SS 1, MOD: 16QAM, CR 3/4 */
	{39000, 81000, CEIL(39000 * 10, 9), CEIL(81000 * 10, 9), 0x12,
	 BRCM_RATE_36M},
	/* MCS  5: SS 1, MOD: 64QAM, CR 2/3 */
	{52000, 108000, CEIL(52000 * 10, 9), CEIL(108000 * 10, 9), 0x19,
	 BRCM_RATE_48M},
	/* MCS  6: SS 1, MOD: 64QAM, CR 3/4 */
	{58500, 121500, CEIL(58500 * 10, 9), CEIL(121500 * 10, 9), 0x1A,
	 BRCM_RATE_54M},
	/* MCS  7: SS 1, MOD: 64QAM, CR 5/6 */
	{65000, 135000, CEIL(65000 * 10, 9), CEIL(135000 * 10, 9), 0x1C,
	 BRCM_RATE_54M},
	/* MCS  8: SS 2, MOD: BPSK,  CR 1/2 */
	{13000, 27000, CEIL(13000 * 10, 9), CEIL(27000 * 10, 9), 0x40,
	 BRCM_RATE_6M},
	/* MCS  9: SS 2, MOD: QPSK,  CR 1/2 */
	{26000, 54000, CEIL(26000 * 10, 9), CEIL(54000 * 10, 9), 0x48,
	 BRCM_RATE_12M},
	/* MCS 10: SS 2, MOD: QPSK,  CR 3/4 */
	{39000, 81000, CEIL(39000 * 10, 9), CEIL(81000 * 10, 9), 0x4A,
	 BRCM_RATE_18M},
	/* MCS 11: SS 2, MOD: 16QAM, CR 1/2 */
	{52000, 108000, CEIL(52000 * 10, 9), CEIL(108000 * 10, 9), 0x50,
	 BRCM_RATE_24M},
	/* MCS 12: SS 2, MOD: 16QAM, CR 3/4 */
	{78000, 162000, CEIL(78000 * 10, 9), CEIL(162000 * 10, 9), 0x52,
	 BRCM_RATE_36M},
	/* MCS 13: SS 2, MOD: 64QAM, CR 2/3 */
	{104000, 216000, CEIL(104000 * 10, 9), CEIL(216000 * 10, 9), 0x59,
	 BRCM_RATE_48M},
	/* MCS 14: SS 2, MOD: 64QAM, CR 3/4 */
	{117000, 243000, CEIL(117000 * 10, 9), CEIL(243000 * 10, 9), 0x5A,
	 BRCM_RATE_54M},
	/* MCS 15: SS 2, MOD: 64QAM, CR 5/6 */
	{130000, 270000, CEIL(130000 * 10, 9), CEIL(270000 * 10, 9), 0x5C,
	 BRCM_RATE_54M},
	/* MCS 16: SS 3, MOD: BPSK,  CR 1/2 */
	{19500, 40500, CEIL(19500 * 10, 9), CEIL(40500 * 10, 9), 0x80,
	 BRCM_RATE_6M},
	/* MCS 17: SS 3, MOD: QPSK,  CR 1/2 */
	{39000, 81000, CEIL(39000 * 10, 9), CEIL(81000 * 10, 9), 0x88,
	 BRCM_RATE_12M},
	/* MCS 18: SS 3, MOD: QPSK,  CR 3/4 */
	{58500, 121500, CEIL(58500 * 10, 9), CEIL(121500 * 10, 9), 0x8A,
	 BRCM_RATE_18M},
	/* MCS 19: SS 3, MOD: 16QAM, CR 1/2 */
	{78000, 162000, CEIL(78000 * 10, 9), CEIL(162000 * 10, 9), 0x90,
	 BRCM_RATE_24M},
	/* MCS 20: SS 3, MOD: 16QAM, CR 3/4 */
	{117000, 243000, CEIL(117000 * 10, 9), CEIL(243000 * 10, 9), 0x92,
	 BRCM_RATE_36M},
	/* MCS 21: SS 3, MOD: 64QAM, CR 2/3 */
	{156000, 324000, CEIL(156000 * 10, 9), CEIL(324000 * 10, 9), 0x99,
	 BRCM_RATE_48M},
	/* MCS 22: SS 3, MOD: 64QAM, CR 3/4 */
	{175500, 364500, CEIL(175500 * 10, 9), CEIL(364500 * 10, 9), 0x9A,
	 BRCM_RATE_54M},
	/* MCS 23: SS 3, MOD: 64QAM, CR 5/6 */
	{195000, 405000, CEIL(195000 * 10, 9), CEIL(405000 * 10, 9), 0x9B,
	 BRCM_RATE_54M},
	/* MCS 24: SS 4, MOD: BPSK,  CR 1/2 */
	{26000, 54000, CEIL(26000 * 10, 9), CEIL(54000 * 10, 9), 0xC0,
	 BRCM_RATE_6M},
	/* MCS 25: SS 4, MOD: QPSK,  CR 1/2 */
	{52000, 108000, CEIL(52000 * 10, 9), CEIL(108000 * 10, 9), 0xC8,
	 BRCM_RATE_12M},
	/* MCS 26: SS 4, MOD: QPSK,  CR 3/4 */
	{78000, 162000, CEIL(78000 * 10, 9), CEIL(162000 * 10, 9), 0xCA,
	 BRCM_RATE_18M},
	/* MCS 27: SS 4, MOD: 16QAM, CR 1/2 */
	{104000, 216000, CEIL(104000 * 10, 9), CEIL(216000 * 10, 9), 0xD0,
	 BRCM_RATE_24M},
	/* MCS 28: SS 4, MOD: 16QAM, CR 3/4 */
	{156000, 324000, CEIL(156000 * 10, 9), CEIL(324000 * 10, 9), 0xD2,
	 BRCM_RATE_36M},
	/* MCS 29: SS 4, MOD: 64QAM, CR 2/3 */
	{208000, 432000, CEIL(208000 * 10, 9), CEIL(432000 * 10, 9), 0xD9,
	 BRCM_RATE_48M},
	/* MCS 30: SS 4, MOD: 64QAM, CR 3/4 */
	{234000, 486000, CEIL(234000 * 10, 9), CEIL(486000 * 10, 9), 0xDA,
	 BRCM_RATE_54M},
	/* MCS 31: SS 4, MOD: 64QAM, CR 5/6 */
	{260000, 540000, CEIL(260000 * 10, 9), CEIL(540000 * 10, 9), 0xDB,
	 BRCM_RATE_54M},
	/* MCS 32: SS 1, MOD: BPSK,  CR 1/2 */
	{0, 6000, 0, CEIL(6000 * 10, 9), 0x00, BRCM_RATE_6M},
};

/* phycfg for legacy OFDM frames: code rate, modulation scheme, spatial streams
 *   Number of spatial streams: always 1
 *   other fields: refer to table 78 of section 17.3.2.2 of the original .11a standard
 */
struct legacy_phycfg {
	u32 rate_ofdm;	/* ofdm mac rate */
	u8 tx_phy_ctl3;	/* phy ctl byte 3, code rate, modulation type, # of streams */
};

#define LEGACY_PHYCFG_TABLE_SIZE	12	/* Number of legacy_rate_cfg entries in the table */

/* In CCK mode LPPHY overloads OFDM Modulation bits with CCK Data Rate */
/* Eventually MIMOPHY would also be converted to this format */
/* 0 = 1Mbps; 1 = 2Mbps; 2 = 5.5Mbps; 3 = 11Mbps */
static const struct
legacy_phycfg legacy_phycfg_table[LEGACY_PHYCFG_TABLE_SIZE] = {
	{BRCM_RATE_1M, 0x00},	/* CCK  1Mbps,  data rate  0 */
	{BRCM_RATE_2M, 0x08},	/* CCK  2Mbps,  data rate  1 */
	{BRCM_RATE_5M5, 0x10},	/* CCK  5.5Mbps,  data rate  2 */
	{BRCM_RATE_11M, 0x18},	/* CCK  11Mbps,  data rate   3 */
	/* OFDM  6Mbps,  code rate 1/2, BPSK,   1 spatial stream */
	{BRCM_RATE_6M, 0x00},
	/* OFDM  9Mbps,  code rate 3/4, BPSK,   1 spatial stream */
	{BRCM_RATE_9M, 0x02},
	/* OFDM  12Mbps, code rate 1/2, QPSK,   1 spatial stream */
	{BRCM_RATE_12M, 0x08},
	/* OFDM  18Mbps, code rate 3/4, QPSK,   1 spatial stream */
	{BRCM_RATE_18M, 0x0A},
	/* OFDM  24Mbps, code rate 1/2, 16-QAM, 1 spatial stream */
	{BRCM_RATE_24M, 0x10},
	/* OFDM  36Mbps, code rate 3/4, 16-QAM, 1 spatial stream */
	{BRCM_RATE_36M, 0x12},
	/* OFDM  48Mbps, code rate 2/3, 64-QAM, 1 spatial stream */
	{BRCM_RATE_48M, 0x19},
	/* OFDM  54Mbps, code rate 3/4, 64-QAM, 1 spatial stream */
	{BRCM_RATE_54M, 0x1A},
};

/* Hardware rates (also encodes default basic rates) */

const wlc_rateset_t cck_ofdm_mimo_rates = {
	12,
	{			/*    1b,   2b,   5.5b, 6,    9,    11b,  12,   18,   24,   36,   48,   54 Mbps */
	 0x82, 0x84, 0x8b, 0x0c, 0x12, 0x96, 0x18, 0x24, 0x30, 0x48, 0x60,
	 0x6c},
	0x00,
	{0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00}
};

const wlc_rateset_t ofdm_mimo_rates = {
	8,
	{			/*    6b,   9,    12b,  18,   24b,  36,   48,   54 Mbps */
	 0x8c, 0x12, 0x98, 0x24, 0xb0, 0x48, 0x60, 0x6c},
	0x00,
	{0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00}
};

/* Default ratesets that include MCS32 for 40BW channels */
const wlc_rateset_t cck_ofdm_40bw_mimo_rates = {
	12,
	{			/*    1b,   2b,   5.5b, 6,    9,    11b,  12,   18,   24,   36,   48,   54 Mbps */
	 0x82, 0x84, 0x8b, 0x0c, 0x12, 0x96, 0x18, 0x24, 0x30, 0x48, 0x60,
	 0x6c},
	0x00,
	{0xff, 0xff, 0xff, 0xff, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00}
};

const wlc_rateset_t ofdm_40bw_mimo_rates = {
	8,
	{			/*    6b,   9,    12b,  18,   24b,  36,   48,   54 Mbps */
	 0x8c, 0x12, 0x98, 0x24, 0xb0, 0x48, 0x60, 0x6c},
	0x00,
	{0xff, 0xff, 0xff, 0xff, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00}
};

const wlc_rateset_t cck_ofdm_rates = {
	12,
	{			/*    1b,   2b,   5.5b, 6,    9,    11b,  12,   18,   24,   36,   48,   54 Mbps */
	 0x82, 0x84, 0x8b, 0x0c, 0x12, 0x96, 0x18, 0x24, 0x30, 0x48, 0x60,
	 0x6c},
	0x00,
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00}
};

const wlc_rateset_t gphy_legacy_rates = {
	4,
	{			/*    1b,   2b,   5.5b,  11b Mbps */
	 0x82, 0x84, 0x8b, 0x96},
	0x00,
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00}
};

const wlc_rateset_t ofdm_rates = {
	8,
	{			/*    6b,   9,    12b,  18,   24b,  36,   48,   54 Mbps */
	 0x8c, 0x12, 0x98, 0x24, 0xb0, 0x48, 0x60, 0x6c},
	0x00,
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00}
};

const wlc_rateset_t cck_rates = {
	4,
	{			/*    1b,   2b,   5.5,  11 Mbps */
	 0x82, 0x84, 0x0b, 0x16},
	0x00,
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00}
};

/* check if rateset is valid.
 * if check_brate is true, rateset without a basic rate is considered NOT valid.
 */
static bool brcms_c_rateset_valid(wlc_rateset_t *rs, bool check_brate)
{
	uint idx;

	if (!rs->count)
		return false;

	if (!check_brate)
		return true;

	/* error if no basic rates */
	for (idx = 0; idx < rs->count; idx++) {
		if (rs->rates[idx] & BRCMS_RATE_FLAG)
			return true;
	}
	return false;
}

void brcms_c_rateset_mcs_upd(wlc_rateset_t *rs, u8 txstreams)
{
	int i;
	for (i = txstreams; i < MAX_STREAMS_SUPPORTED; i++)
		rs->mcs[i] = 0;
}

/* filter based on hardware rateset, and sort filtered rateset with basic bit(s) preserved,
 * and check if resulting rateset is valid.
*/
bool
brcms_c_rate_hwrs_filter_sort_validate(wlc_rateset_t *rs,
				   const wlc_rateset_t *hw_rs,
				   bool check_brate, u8 txstreams)
{
	u8 rateset[BRCM_MAXRATE + 1];
	u8 r;
	uint count;
	uint i;

	memset(rateset, 0, sizeof(rateset));
	count = rs->count;

	for (i = 0; i < count; i++) {
		/* mask off "basic rate" bit, BRCMS_RATE_FLAG */
		r = (int)rs->rates[i] & BRCMS_RATE_MASK;
		if ((r > BRCM_MAXRATE) || (rate_info[r] == 0))
			continue;
		rateset[r] = rs->rates[i];	/* preserve basic bit! */
	}

	/* fill out the rates in order, looking at only supported rates */
	count = 0;
	for (i = 0; i < hw_rs->count; i++) {
		r = hw_rs->rates[i] & BRCMS_RATE_MASK;
		if (rateset[r])
			rs->rates[count++] = rateset[r];
	}

	rs->count = count;

	/* only set the mcs rate bit if the equivalent hw mcs bit is set */
	for (i = 0; i < MCSSET_LEN; i++)
		rs->mcs[i] = (rs->mcs[i] & hw_rs->mcs[i]);

	if (brcms_c_rateset_valid(rs, check_brate))
		return true;
	else
		return false;
}

/* calculate the rate of a rx'd frame and return it as a ratespec */
ratespec_t brcms_c_compute_rspec(struct d11rxhdr *rxh, u8 *plcp)
{
	int phy_type;
	ratespec_t rspec = PHY_TXC1_BW_20MHZ << RSPEC_BW_SHIFT;

	phy_type =
	    ((rxh->RxChan & RXS_CHAN_PHYTYPE_MASK) >> RXS_CHAN_PHYTYPE_SHIFT);

	if ((phy_type == PHY_TYPE_N) || (phy_type == PHY_TYPE_SSN) ||
	    (phy_type == PHY_TYPE_LCN) || (phy_type == PHY_TYPE_HT)) {
		switch (rxh->PhyRxStatus_0 & PRXS0_FT_MASK) {
		case PRXS0_CCK:
			rspec =
			    CCK_PHY2MAC_RATE(
				((struct cck_phy_hdr *) plcp)->signal);
			break;
		case PRXS0_OFDM:
			rspec =
			    OFDM_PHY2MAC_RATE(
				((struct ofdm_phy_hdr *) plcp)->rlpt[0]);
			break;
		case PRXS0_PREN:
			rspec = (plcp[0] & MIMO_PLCP_MCS_MASK) | RSPEC_MIMORATE;
			if (plcp[0] & MIMO_PLCP_40MHZ) {
				/* indicate rspec is for 40 MHz mode */
				rspec &= ~RSPEC_BW_MASK;
				rspec |= (PHY_TXC1_BW_40MHZ << RSPEC_BW_SHIFT);
			}
			break;
		case PRXS0_STDN:
			/* fallthru */
		default:
			/* not supported, error condition */
			break;
		}
		if (PLCP3_ISSGI(plcp[3]))
			rspec |= RSPEC_SHORT_GI;
	} else
	    if ((phy_type == PHY_TYPE_A) || (rxh->PhyRxStatus_0 & PRXS0_OFDM))
		rspec = OFDM_PHY2MAC_RATE(
				((struct ofdm_phy_hdr *) plcp)->rlpt[0]);
	else
		rspec = CCK_PHY2MAC_RATE(
				((struct cck_phy_hdr *) plcp)->signal);

	return rspec;
}

/* copy rateset src to dst as-is (no masking or sorting) */
void brcms_c_rateset_copy(const wlc_rateset_t *src, wlc_rateset_t *dst)
{
	memcpy(dst, src, sizeof(wlc_rateset_t));
}

/*
 * Copy and selectively filter one rateset to another.
 * 'basic_only' means only copy basic rates.
 * 'rates' indicates cck (11b) and ofdm rates combinations.
 *    - 0: cck and ofdm
 *    - 1: cck only
 *    - 2: ofdm only
 * 'xmask' is the copy mask (typically 0x7f or 0xff).
 */
void
brcms_c_rateset_filter(wlc_rateset_t *src, wlc_rateset_t *dst, bool basic_only,
		   u8 rates, uint xmask, bool mcsallow)
{
	uint i;
	uint r;
	uint count;

	count = 0;
	for (i = 0; i < src->count; i++) {
		r = src->rates[i];
		if (basic_only && !(r & BRCMS_RATE_FLAG))
			continue;
		if (rates == BRCMS_RATES_CCK && IS_OFDM((r & BRCMS_RATE_MASK)))
			continue;
		if (rates == BRCMS_RATES_OFDM && IS_CCK((r & BRCMS_RATE_MASK)))
			continue;
		dst->rates[count++] = r & xmask;
	}
	dst->count = count;
	dst->htphy_membership = src->htphy_membership;

	if (mcsallow && rates != BRCMS_RATES_CCK)
		memcpy(&dst->mcs[0], &src->mcs[0], MCSSET_LEN);
	else
		brcms_c_rateset_mcs_clear(dst);
}

/* select rateset for a given phy_type and bandtype and filter it, sort it
 * and fill rs_tgt with result
 */
void
brcms_c_rateset_default(wlc_rateset_t *rs_tgt, const wlc_rateset_t *rs_hw,
		    uint phy_type, int bandtype, bool cck_only, uint rate_mask,
		    bool mcsallow, u8 bw, u8 txstreams)
{
	const wlc_rateset_t *rs_dflt;
	wlc_rateset_t rs_sel;
	if ((PHYTYPE_IS(phy_type, PHY_TYPE_HT)) ||
	    (PHYTYPE_IS(phy_type, PHY_TYPE_N)) ||
	    (PHYTYPE_IS(phy_type, PHY_TYPE_LCN)) ||
	    (PHYTYPE_IS(phy_type, PHY_TYPE_SSN))) {
		if (BAND_5G(bandtype)) {
			rs_dflt = (bw == BRCMS_20_MHZ ?
				   &ofdm_mimo_rates : &ofdm_40bw_mimo_rates);
		} else {
			rs_dflt = (bw == BRCMS_20_MHZ ?
				   &cck_ofdm_mimo_rates :
				   &cck_ofdm_40bw_mimo_rates);
		}
	} else if (PHYTYPE_IS(phy_type, PHY_TYPE_LP)) {
		rs_dflt = (BAND_5G(bandtype)) ? &ofdm_rates : &cck_ofdm_rates;
	} else if (PHYTYPE_IS(phy_type, PHY_TYPE_A)) {
		rs_dflt = &ofdm_rates;
	} else if (PHYTYPE_IS(phy_type, PHY_TYPE_G)) {
		rs_dflt = &cck_ofdm_rates;
	} else {
		/* should not happen, error condition */
		rs_dflt = &cck_rates;	/* force cck */
	}

	/* if hw rateset is not supplied, assign selected rateset to it */
	if (!rs_hw)
		rs_hw = rs_dflt;

	brcms_c_rateset_copy(rs_dflt, &rs_sel);
	brcms_c_rateset_mcs_upd(&rs_sel, txstreams);
	brcms_c_rateset_filter(&rs_sel, rs_tgt, false,
			   cck_only ? BRCMS_RATES_CCK : BRCMS_RATES_CCK_OFDM,
			   rate_mask, mcsallow);
	brcms_c_rate_hwrs_filter_sort_validate(rs_tgt, rs_hw, false,
					   mcsallow ? txstreams : 1);
}

s16 brcms_c_rate_legacy_phyctl(uint rate)
{
	uint i;
	for (i = 0; i < LEGACY_PHYCFG_TABLE_SIZE; i++)
		if (rate == legacy_phycfg_table[i].rate_ofdm)
			return legacy_phycfg_table[i].tx_phy_ctl3;

	return -1;
}

void brcms_c_rateset_mcs_clear(wlc_rateset_t *rateset)
{
	uint i;
	for (i = 0; i < MCSSET_LEN; i++)
		rateset->mcs[i] = 0;
}

void brcms_c_rateset_mcs_build(wlc_rateset_t *rateset, u8 txstreams)
{
	memcpy(&rateset->mcs[0], &cck_ofdm_mimo_rates.mcs[0], MCSSET_LEN);
	brcms_c_rateset_mcs_upd(rateset, txstreams);
}

/* Based on bandwidth passed, allow/disallow MCS 32 in the rateset */
void brcms_c_rateset_bw_mcs_filter(wlc_rateset_t *rateset, u8 bw)
{
	if (bw == BRCMS_40_MHZ)
		setbit(rateset->mcs, 32);
	else
		clrbit(rateset->mcs, 32);
}
