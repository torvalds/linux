/*
 * Common [OS-independent] rate management
 * 802.11 Networking Adapter Device Driver.
 *
 * Broadcom Proprietary and Confidential. Copyright (C) 2020,
 * All Rights Reserved.
 *
 * This is UNPUBLISHED PROPRIETARY SOURCE CODE of Broadcom;
 * the contents of this file may not be disclosed to third parties,
 * copied or duplicated in any form, in whole or in part, without
 * the prior written permission of Broadcom.
 *
 *
 * <<Broadcom-WL-IPTag/Proprietary:>>
 */

#include <typedefs.h>
#include <osl.h>
#include <d11.h>
#include <802.11ax.h>

#include <bcmwifi_rspec.h>
#include <bcmwifi_rates.h>

/* TODO: Consolidate rspec utility functions from wlc_rate.c and bcmwifi_monitor.c
 * into here if they're shared by non wl layer as well...
 */

/* ============================================ */
/* Moved from wlc_rate.c                        */
/* ============================================ */

/**
 * Returns the rate in [Kbps] units.
 */
static uint
wf_he_rspec_to_rate(ratespec_t rspec, uint max_mcs, uint max_nss)
{
	uint mcs = (rspec & WL_RSPEC_HE_MCS_MASK);
	uint nss = (rspec & WL_RSPEC_HE_NSS_MASK) >> WL_RSPEC_HE_NSS_SHIFT;
	bool dcm = (rspec & WL_RSPEC_DCM) != 0;
	uint bw = RSPEC_BW(rspec);
	uint gi = RSPEC_HE_LTF_GI(rspec);

	ASSERT(mcs <= max_mcs);
	ASSERT(nss <= max_nss);

	if (mcs > max_mcs) {
		return 0;
	}
	BCM_REFERENCE(max_nss);

	return wf_he_mcs_to_rate(mcs, nss, bw, gi, dcm);
} /* wf_he_rspec_to_rate */

/* take a well formed ratespec_t arg and return phy rate in [Kbps] units.
 * 'rsel' indicates if the call comes from rate selection.
 */
static uint
_wf_rspec_to_rate(ratespec_t rspec, bool rsel)
{
	uint rate = (uint)(-1);

	if (RSPEC_ISLEGACY(rspec)) {
		rate = 500 * RSPEC2RATE(rspec);
	} else if (RSPEC_ISHT(rspec)) {
		uint mcs = (rspec & WL_RSPEC_HT_MCS_MASK);

		ASSERT_FP(mcs <= 32 || IS_PROPRIETARY_11N_MCS(mcs));

		if (mcs == 32) {
			rate = wf_mcs_to_rate(mcs, 1, WL_RSPEC_BW_40MHZ, RSPEC_ISSGI(rspec));
		} else {
#if defined(WLPROPRIETARY_11N_RATES)
			uint nss = GET_11N_MCS_NSS(mcs);
			mcs = wf_get_single_stream_mcs(mcs);
#else /* this ifdef prevents ROM abandons */
			uint nss = 1 + (mcs / 8);
			mcs = mcs % 8;
#endif /* WLPROPRIETARY_11N_RATES */

			rate = wf_mcs_to_rate(mcs, nss, RSPEC_BW(rspec), RSPEC_ISSGI(rspec));
		}
	} else if (RSPEC_ISVHT(rspec)) {
		uint mcs = (rspec & WL_RSPEC_VHT_MCS_MASK);
		uint nss = (rspec & WL_RSPEC_VHT_NSS_MASK) >> WL_RSPEC_VHT_NSS_SHIFT;

		if (rsel) {
			rate = wf_mcs_to_rate(mcs, nss, RSPEC_BW(rspec), 0);
		} else {
			ASSERT_FP(mcs <= WLC_MAX_VHT_MCS);
			ASSERT_FP(nss <= 8);

			rate = wf_mcs_to_rate(mcs, nss, RSPEC_BW(rspec), RSPEC_ISSGI(rspec));
		}
	} else if (RSPEC_ISHE(rspec)) {
		rate = wf_he_rspec_to_rate(rspec, WLC_MAX_HE_MCS, 8);
	} else if (RSPEC_ISEHT(rspec)) {
		rate = wf_he_rspec_to_rate(rspec, WLC_MAX_EHT_MCS, 16);
	} else {
		ASSERT(0);
	}

	return (rate == 0) ? (uint)(-1) : rate;
}

/* take a well formed ratespec_t 'rspec' and return phy rate in [Kbps] units */
uint
wf_rspec_to_rate(ratespec_t rspec)
{
	return _wf_rspec_to_rate(rspec, FALSE);
}

/* take a well formed ratespec_t 'rspec' and return phy rate in [Kbps] units,
 * FOR RATE SELECTION ONLY, WHICH USES LEGACY, HT, AND VHT RATES, AND VHT MCS
 * COULD BE BIGGER THAN WLC_MAX_VHT_MCS!
 */
uint
wf_rspec_to_rate_rsel(ratespec_t rspec)
{
	return _wf_rspec_to_rate(rspec, TRUE);
}

#ifdef BCMDBG
/* Return the rate in 500Kbps units if the rspec is legacy rate, assert otherwise */
uint
wf_rspec_to_rate_legacy(ratespec_t rspec)
{
	ASSERT(RSPEC_ISLEGACY(rspec));

	return rspec & WL_RSPEC_LEGACY_RATE_MASK;
}
#endif

/**
 * Function for computing RSPEC from EHT PLCP
 *
 * TODO: add link to the HW spec.
 */
ratespec_t
wf_eht_plcp_to_rspec(uint8 *plcp)
{
	ASSERT(!"wf_eht_plcp_to_rspec: not implemented!");
	return 0;
}

/**
 * Function for computing RSPEC from HE PLCP
 *
 * based on rev3.10 :
 * https://docs.google.com/spreadsheets/d/
 * 1eP6ZCRrtnF924ds1R-XmbcH0IdQ0WNJpS1-FHmWeb9g/edit#gid=1492656555
 */
ratespec_t
wf_he_plcp_to_rspec(uint8 *plcp)
{
	uint8 rate;
	uint8 nss;
	uint8 bw;
	uint8 gi;
	ratespec_t rspec;

	/* HE plcp - 6 B */
	uint32 plcp0;
	uint16 plcp1;

	ASSERT(plcp);

	plcp0 = ((plcp[3] << 24) | (plcp[2] << 16) | (plcp[1] << 8) | plcp[0]);
	plcp1 = ((plcp[5] << 8) | plcp[4]);

	/* TBD: only SU supported now */
	rate = (plcp0 & HE_SU_RE_SIGA_MCS_MASK) >> HE_SU_RE_SIGA_MCS_SHIFT;
	/* PLCP contains (NSTS - 1) while RSPEC stores NSTS */
	nss = ((plcp0 & HE_SU_RE_SIGA_NSTS_MASK) >> HE_SU_RE_SIGA_NSTS_SHIFT) + 1;
	rspec = HE_RSPEC(rate, nss);

	/* GI info comes from CP/LTF */
	gi = (plcp0 & HE_SU_RE_SIGA_GI_LTF_MASK) >> HE_SU_RE_SIGA_GI_LTF_SHIFT;
	rspec |= HE_GI_TO_RSPEC(gi);

	/* b19-b20 of plcp indicate bandwidth in the format (2-bit):
	 *	0 for 20M, 1 for 40M, 2 for 80M, and 3 for 80p80/160M
	 * SW store this BW in rspec format (3-bit):
	 *	1 for 20M, 2 for 40M, 3 for 80M, and 4 for 80p80/160M
	 */
	bw = ((plcp0 & HE_SU_SIGA_BW_MASK) >> HE_SU_SIGA_BW_SHIFT) + 1;
	rspec |= (bw << WL_RSPEC_BW_SHIFT);

	if (plcp1 & HE_SU_RE_SIGA_BEAMFORM_MASK)
		rspec |= WL_RSPEC_TXBF;
	if (plcp1 & HE_SU_RE_SIGA_CODING_MASK)
		rspec |= WL_RSPEC_LDPC;
	if (plcp1 & HE_SU_RE_SIGA_STBC_MASK)
		rspec |= WL_RSPEC_STBC;
	if (plcp0 & HE_SU_RE_SIGA_DCM_MASK)
		rspec |= WL_RSPEC_DCM;

	return rspec;
}

ratespec_t
wf_vht_plcp_to_rspec(uint8 *plcp)
{
	uint8 rate;
	uint vht_sig_a1, vht_sig_a2;
	ratespec_t rspec;

	ASSERT(plcp);

	rate = wf_vht_plcp_to_rate(plcp) & ~WF_NON_HT_MCS;

	vht_sig_a1 = plcp[0] | (plcp[1] << 8);
	vht_sig_a2 = plcp[3] | (plcp[4] << 8);

	rspec = VHT_RSPEC((rate & WL_RSPEC_VHT_MCS_MASK),
		(rate >> WL_RSPEC_VHT_NSS_SHIFT));
#if ((((VHT_SIGA1_20MHZ_VAL + 1) << WL_RSPEC_BW_SHIFT)  != WL_RSPEC_BW_20MHZ) || \
	(((VHT_SIGA1_40MHZ_VAL + 1) << WL_RSPEC_BW_SHIFT)  != WL_RSPEC_BW_40MHZ) || \
	(((VHT_SIGA1_80MHZ_VAL + 1) << WL_RSPEC_BW_SHIFT)  != WL_RSPEC_BW_80MHZ) || \
	(((VHT_SIGA1_160MHZ_VAL + 1) << WL_RSPEC_BW_SHIFT)  != WL_RSPEC_BW_160MHZ))
#error "VHT SIGA BW mapping to RSPEC BW needs correction"
#endif
	rspec |= ((vht_sig_a1 & VHT_SIGA1_160MHZ_VAL) + 1) << WL_RSPEC_BW_SHIFT;
	if (vht_sig_a1 & VHT_SIGA1_STBC)
		rspec |= WL_RSPEC_STBC;
	if (vht_sig_a2 & VHT_SIGA2_GI_SHORT)
		rspec |= WL_RSPEC_SGI;
	if (vht_sig_a2 & VHT_SIGA2_CODING_LDPC)
		rspec |= WL_RSPEC_LDPC;

	return rspec;
}

ratespec_t
wf_ht_plcp_to_rspec(uint8 *plcp)
{
	return HT_RSPEC(plcp[0] & MIMO_PLCP_MCS_MASK);
}

/* ============================================ */
/* Moved from wlc_rate_def.c                    */
/* ============================================ */

/**
 * Rate info per rate: tells for *pre* 802.11n rates whether a given rate is OFDM or not and its
 * phy_rate value. Table index is a rate in [500Kbps] units, from 0 to 54Mbps.
 * Contents of a table element:
 *     d[7] : 1=OFDM rate, 0=DSSS/CCK rate
 *     d[3:0] if DSSS/CCK rate:
 *         index into the 'M_RATE_TABLE_B' table maintained by ucode in shm
 *     d[3:0] if OFDM rate: encode rate per 802.11a-1999 sec 17.3.4.1, with lsb transmitted first.
 *         index into the 'M_RATE_TABLE_A' table maintained by ucode in shm
 */
/* Note: make this table 128 elements so the result of (rspec & 0x7f) can be safely
 * used as the index into this table...
 */
const uint8 rate_info[128] = {
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
/* 100 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x8c,
/* ------------- guard ------------ */                          0x00,
/* 110 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 120 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
