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
#ifdef BCMDRIVER
#include <osl.h>
#else
#include <assert.h>
#ifndef ASSERT
#define ASSERT(e)	assert(e)
#endif
#ifndef ASSERT_FP
#define ASSERT_FP(e)	assert(e)
#endif
#endif /* BCMDRIVER */
#include <802.11.h>
#include <802.11ax.h>
#include <bcmutils.h>

#include <bcmwifi_rspec.h>
#include <bcmwifi_rates.h>

/* TODO: Consolidate rate utility functions from wlc_rate.c and bcmwifi_monitor.c
 * into here if they're shared by non wl layer as well...
 */

/* ============================================ */
/* Moved from wlc_rate.c                        */
/* ============================================ */

/* HE mcs info */
struct ieee_80211_mcs_rate_info {
	uint8 constellation_bits;
	uint8 coding_q;
	uint8 coding_d;
	uint8 dcm_capable;	/* 1 if dcm capable */
};

static const struct ieee_80211_mcs_rate_info wlc_mcs_info[] = {
	{ 1, 1, 2, 1 }, /* MCS  0: MOD: BPSK,   CR 1/2, dcm capable */
	{ 2, 1, 2, 1 }, /* MCS  1: MOD: QPSK,   CR 1/2, dcm capable */
	{ 2, 3, 4, 0 }, /* MCS  2: MOD: QPSK,   CR 3/4, NOT dcm capable */
	{ 4, 1, 2, 1 }, /* MCS  3: MOD: 16QAM,  CR 1/2, dcm capable */
	{ 4, 3, 4, 1 }, /* MCS  4: MOD: 16QAM,  CR 3/4, dcm capable */
	{ 6, 2, 3, 0 }, /* MCS  5: MOD: 64QAM,  CR 2/3, NOT dcm capable */
	{ 6, 3, 4, 0 }, /* MCS  6: MOD: 64QAM,  CR 3/4, NOT dcm capable */
	{ 6, 5, 6, 0 }, /* MCS  7: MOD: 64QAM,  CR 5/6, NOT dcm capable */
	{ 8, 3, 4, 0 }, /* MCS  8: MOD: 256QAM, CR 3/4, NOT dcm capable */
	{ 8, 5, 6, 0 }, /* MCS  9: MOD: 256QAM, CR 5/6, NOT dcm capable */
	{ 10, 3, 4, 0 }, /* MCS 10: MOD: 1024QAM, CR 3/4, NOT dcm capable */
	{ 10, 5, 6, 0 }, /* MCS 11: MOD: 1024QAM, CR 5/6, NOT dcm capable */
#ifdef WL11BE
	/* TODO: for now EHT shares this table with HE,
	 * create a new table if needed once we know more
	 * about EHT rate calculation...
	 */
	{ 12, 3, 4, 0 }, /* MCS 12: MOD: 4096QAM, CR 3/4, NOT dcm capable */
	{ 12, 5, 6, 0 }, /* MCS 13: MOD: 4096QAM, CR 5/6, NOT dcm capable */
#endif
};

/* Nsd values Draft0.4 Table 26.63 onwards */
static const uint wlc_he_nsd[] = {
	234,	/* BW20 */
	468,	/* BW40 */
	980,	/* BW80 */
	1960,	/* BW160 */
#ifdef WL11BE
	/* TODO: for now EHT shares this table with HE,
	 * create a new table if needed once we know more
	 * about EHT rate calculation...
	 */
	2940,	/* BW240 */
	3920	/* BW320 */
#endif
};

/* Nsd values Draft3.3 Table 28-15 */
static const uint wlc_he_ru_nsd[] = {
	24,	/* 26T */
	48,	/* 52T */
	102,	/* 106T */
	234,	/* 242T/BW20 */
	468,	/* 484T/BW40 */
	980,	/* 996T/BW80 */
	1960,	/* 2*996T/BW160 */
#ifdef WL11BE
	/* TODO: for now EHT shares this table with HE,
	 * create a new table if needed once we know more
	 * about EHT rate calculation...
	 */
	2940,	/* 3*996T/BW240 */
	3920	/* 4*996T/BW320 */
#endif
};

#define HE_RU_TO_NSD(ru_idx)	\
	(ru_idx < ARRAYSIZE(wlc_he_ru_nsd)) ? \
	wlc_he_ru_nsd[ru_idx] : 0

/* sym_len = 12.8 us. For calculation purpose, *10 */
#define HE_SYM_LEN_FACTOR		(128)

/* GI values = 0.8 , 1.6 or 3.2 us. For calculation purpose, *10 */
#define HE_GI_800us_FACTOR		(8)
#define HE_GI_1600us_FACTOR		(16)
#define HE_GI_3200us_FACTOR		(32)

/* To avoid ROM invalidation use the old macro as is... */
#ifdef WL11BE
#define HE_BW_TO_NSD(bwi) \
	((bwi) > 0u && (bwi) <= ARRAYSIZE(wlc_he_nsd)) ? \
	wlc_he_nsd[(bwi) - 1u] : 0u
#else
#define HE_BW_TO_NSD(bwi) \
	((bwi) > 0 && ((bwi) << WL_RSPEC_BW_SHIFT) <= WL_RSPEC_BW_160MHZ) ? \
	wlc_he_nsd[(bwi)-1] : 0
#endif /* WL11BE */

#define ksps		250 /* kilo symbols per sec, 4 us sym */

#ifdef WL11BE
/* Table "wlc_nsd" is derived from HT and VHT #defines below, but extended for HE
 * for rate calculation purpose at a given NSS and bandwidth combination.
 *
 * It should and can only be used in where it wants to know the relative rate in kbps
 * for a different NSS and bandwidth combination at a given mcs e.g. in fallback rate
 * search. It shouldn not and can not be used in where it calculates the absolute rate
 * i.e. the result doesn't agree with what the spec says otherwise.
 *
 * See Std 802.11-2016 "Table 21-61 VHT-MCSs for optional 160 MHz and 80+80 MHz, NSS = 8"
 * for VHT, and P802.11ax/D6.0 "Table 27-111 HE-MCSs for 2x996-tone RU, NSS = 8" for HE,
 * for 160Mhz bandwidth for resulting rate comparison.
 *
 * It's again extended for EHT 240/320Mhz bandwidth, for the same purpose.
 */
static const uint16 wlc_nsd[] = {
	52,	/* 20MHz */
	108,	/* 40MHz */
	234,	/* 80Mhz */
	468,	/* 160MHz */
	702,	/* 240MHz */
	936,	/* 320MHz */
};

#define BW_TO_NSD(bwi) \
	((bwi) > 0u && (bwi) <= ARRAYSIZE(wlc_nsd)) ? \
	wlc_nsd[(bwi) - 1u] : 0u

static uint
wf_nsd2ndbps(uint mcs, uint nss, uint nsd, bool dcm)
{
	uint Ndbps;

	/* multiply number of spatial streams,
	 * bits per number from the constellation,
	 * and coding quotient
	 */
	Ndbps = nsd * nss *
		wlc_mcs_info[mcs].coding_q * wlc_mcs_info[mcs].constellation_bits;

	/* adjust for the coding rate divisor */
	Ndbps = Ndbps / wlc_mcs_info[mcs].coding_d;

	/* take care of dcm: dcm divides R by 2. If not dcm mcs, ignore */
	if (dcm) {
		if (wlc_mcs_info[mcs].dcm_capable) {
			Ndbps >>= 1u;
		}
	}

	return Ndbps;
}
#else
/* for HT and VHT? */
#define Nsd_20MHz	52
#define Nsd_40MHz	108
#define Nsd_80MHz	234
#define Nsd_160MHz	468
#endif /* WL11BE */

uint
wf_he_mcs_to_Ndbps(uint mcs, uint nss, uint bw, bool dcm)
{
	uint Nsd;
	uint Ndbps;

	/* find the number of complex numbers per symbol */
	Nsd = HE_BW_TO_NSD(bw >> WL_RSPEC_BW_SHIFT);

#ifdef WL11BE
	Ndbps = wf_nsd2ndbps(mcs, nss, Nsd, dcm);
#else
	/* multiply number of spatial streams,
	 * bits per number from the constellation,
	 * and coding quotient
	 */
	Ndbps = Nsd * nss *
		wlc_mcs_info[mcs].coding_q * wlc_mcs_info[mcs].constellation_bits;

	/* adjust for the coding rate divisor */
	Ndbps = Ndbps / wlc_mcs_info[mcs].coding_d;

	/* take care of dcm: dcm divides R by 2. If not dcm mcs, ignore */
	if (dcm) {
		if (wlc_mcs_info[mcs].dcm_capable) {
			Ndbps >>= 1;
		}
	}
#endif /* WL11BE */

	return Ndbps;
}

uint32
wf_he_mcs_ru_to_ndbps(uint8 mcs, uint8 nss, bool dcm, uint8 ru_index)
{
	uint32 nsd;
	uint32 ndbps;

	/* find the number of complex numbers per symbol */
	nsd = HE_RU_TO_NSD(ru_index);

#ifdef WL11BE
	ndbps = wf_nsd2ndbps(mcs, nss, nsd, dcm);
#else
	/* multiply number of spatial streams,
	 * bits per number from the constellation,
	 * and coding quotient
	 * Ndbps = Nss x Nsd x (Nbpscs x R) x (DCM/2)
	 */
	ndbps = nsd * nss *
		wlc_mcs_info[mcs].coding_q * wlc_mcs_info[mcs].constellation_bits;

	/* adjust for the coding rate divisor */
	ndbps = ndbps / wlc_mcs_info[mcs].coding_d;

	/* take care of dcm: dcm divides R by 2. If not dcm mcs, ignore */
	if (dcm && wlc_mcs_info[mcs].dcm_capable) {
		ndbps >>= 1;
	}
#endif /* WL11BE */
	return ndbps;
}

/**
 * Returns the rate in [Kbps] units for a caller supplied MCS/bandwidth/Nss/Sgi/dcm combination.
 *     'mcs' : a *single* spatial stream MCS (11ax)
 * formula as per http:
 *	WLAN&preview=/323036249/344457953/11ax_rate_table.xlsx
 * Symbol length = 12.8 usec [given as sym_len/10 below]
 * GI value = 0.8 or 1.6 or 3.2 usec [given as GI_value/10 below]
 * rate (Kbps) = (Nsd * Nbpscs * nss * (coding_q/coding_d) * 1000) / ((sym_len/10) + (GI_value/10))
 *	 Note that, for calculation purpose, following is used. [to be careful with overflows]
 * rate (Kbps) = (Nsd * Nbpscs * nss * (coding_q/coding_d) * 1000) / ((sym_len + GI_value) / 10)
 * rate (Kbps) = (Nsd * Nbpscs * nss * (coding_q/coding_d) * 1000) / (sym_len + GI_value) * 10
 */
uint
wf_he_mcs_to_rate(uint mcs, uint nss, uint bw, uint gi, bool dcm)
{
	uint rate;
	uint rate_deno;

	rate = HE_BW_TO_NSD(bw >> WL_RSPEC_BW_SHIFT);

#ifdef WL11BE
	rate = wf_nsd2ndbps(mcs, nss, rate, dcm);
#else
	/* Nbpscs: multiply by bits per number from the constellation in use */
	rate = rate * wlc_mcs_info[mcs].constellation_bits;

	/* Nss: adjust for the number of spatial streams */
	rate = rate * nss;

	/* R: adjust for the coding rate given as a quotient and divisor */
	rate = (rate * wlc_mcs_info[mcs].coding_q) / wlc_mcs_info[mcs].coding_d;

	/* take care of dcm: dcm divides R by 2. If not dcm mcs, ignore */
	if (dcm) {
		if (wlc_mcs_info[mcs].dcm_capable) {
			rate >>= 1;
		}
	}
#endif /* WL11BE */

	/* add sym len factor */
	rate_deno = HE_SYM_LEN_FACTOR;

	/* get GI for denominator */
	if (HE_IS_GI_3_2us(gi)) {
		rate_deno += HE_GI_3200us_FACTOR;
	} else if (HE_IS_GI_1_6us(gi)) {
		rate_deno += HE_GI_1600us_FACTOR;
	} else {
		/* assuming HE_GI_0_8us */
		rate_deno += HE_GI_800us_FACTOR;
	}

	/* as per above formula */
	rate *= 1000;	/* factor of 10. *100 to accommodate 2 places */
	rate /= rate_deno;
	rate *= 10; /* *100 was already done above. Splitting is done to avoid overflow. */

	return rate;
}

uint
wf_mcs_to_Ndbps(uint mcs, uint nss, uint bw)
{
	uint Nsd;
	uint Ndbps;

	/* This calculation works for 11n HT and 11ac VHT if the HT mcs values
	 * are decomposed into a base MCS = MCS % 8, and Nss = 1 + MCS / 8.
	 * That is, HT MCS 23 is a base MCS = 7, Nss = 3
	 */

	/* find the number of complex numbers per symbol */
#ifdef WL11BE
	Nsd = BW_TO_NSD(bw >> WL_RSPEC_BW_SHIFT);

	Ndbps = wf_nsd2ndbps(mcs, nss, Nsd, FALSE);
#else
	if (bw == WL_RSPEC_BW_20MHZ) {
		Nsd = Nsd_20MHz;
	} else if (bw == WL_RSPEC_BW_40MHZ) {
		Nsd = Nsd_40MHz;
	} else if (bw == WL_RSPEC_BW_80MHZ) {
		Nsd = Nsd_80MHz;
	} else if (bw == WL_RSPEC_BW_160MHZ) {
		Nsd = Nsd_160MHz;
	} else {
		Nsd = 0;
	}

	/* multiply number of spatial streams,
	 * bits per number from the constellation,
	 * and coding quotient
	 */
	Ndbps = Nsd * nss *
		wlc_mcs_info[mcs].coding_q * wlc_mcs_info[mcs].constellation_bits;

	/* adjust for the coding rate divisor */
	Ndbps = Ndbps / wlc_mcs_info[mcs].coding_d;
#endif /* WL11BE */

	return Ndbps;
}

/**
 * Returns the rate in [Kbps] units for a caller supplied MCS/bandwidth/Nss/Sgi combination.
 *     'mcs' : a *single* spatial stream MCS (11n or 11ac)
 */
uint
wf_mcs_to_rate(uint mcs, uint nss, uint bw, int sgi)
{
	uint rate;

	if (mcs == 32) {
		/* just return fixed values for mcs32 instead of trying to parametrize */
		rate = (sgi == 0) ? 6000 : 6778;
	} else {
		/* This calculation works for 11n HT, 11ac VHT and 11ax HE if the HT mcs values
		 * are decomposed into a base MCS = MCS % 8, and Nss = 1 + MCS / 8.
		 * That is, HT MCS 23 is a base MCS = 7, Nss = 3
		 */

#if defined(WLPROPRIETARY_11N_RATES)
		switch (mcs) {
		case 87:
			mcs = 8; /* MCS  8: MOD: 256QAM, CR 3/4 */
			break;
		case 88:
			mcs = 9; /* MCS  9: MOD: 256QAM, CR 5/6 */
			break;
		default:
			break;
		}
#endif /* WLPROPRIETARY_11N_RATES */

#ifdef WL11BE
		rate = wf_mcs_to_Ndbps(mcs, nss, bw);
#else
		/* find the number of complex numbers per symbol */
		if (RSPEC_IS20MHZ(bw)) {
			/* 4360 TODO: eliminate Phy const in rspec bw, then just compare
			 * as in 80 and 160 case below instead of RSPEC_IS20MHZ(bw)
			 */
			rate = Nsd_20MHz;
		} else if (RSPEC_IS40MHZ(bw)) {
			/* 4360 TODO: eliminate Phy const in rspec bw, then just compare
			 * as in 80 and 160 case below instead of RSPEC_IS40MHZ(bw)
			 */
			rate = Nsd_40MHz;
		} else if (bw == WL_RSPEC_BW_80MHZ) {
			rate = Nsd_80MHz;
		} else if (bw == WL_RSPEC_BW_160MHZ) {
			rate = Nsd_160MHz;
		} else {
			rate = 0;
		}

		/* multiply by bits per number from the constellation in use */
		rate = rate * wlc_mcs_info[mcs].constellation_bits;

		/* adjust for the number of spatial streams */
		rate = rate * nss;

		/* adjust for the coding rate given as a quotient and divisor */
		rate = (rate * wlc_mcs_info[mcs].coding_q) / wlc_mcs_info[mcs].coding_d;
#endif /* WL11BE */

		/* multiply by Kilo symbols per sec to get Kbps */
		rate = rate * ksps;

		/* adjust the symbols per sec for SGI
		 * symbol duration is 4 us without SGI, and 3.6 us with SGI,
		 * so ratio is 10 / 9
		 */
		if (sgi) {
			/* add 4 for rounding of division by 9 */
			rate = ((rate * 10) + 4) / 9;
		}
	}

	return rate;
} /* wf_mcs_to_rate */

/* This function needs update to handle MU frame PLCP as well (MCS is conveyed via VHT-SIGB
 * field in case of MU frames). Currently this support needs to be added in uCode to communicate
 * MCS information for an MU frame
 *
 *	For VHT frame:
 *		bit 0-3 mcs index
 *		bit 6-4 nsts for VHT
 *		bit 7:	 1 for VHT
 *	Note: bit 7 is used to indicate to the rate sel the mcs is a non HT mcs!
 *
 * Essentially it's the NSS:MCS portions of the rspec
 */
uint8
wf_vht_plcp_to_rate(uint8 *plcp)
{
	uint8 rate, gid;
	uint nss;
	uint32 plcp0 = plcp[0] + (plcp[1] << 8); /* don't need plcp[2] */

	gid = (plcp0 & VHT_SIGA1_GID_MASK) >> VHT_SIGA1_GID_SHIFT;
	if (gid > VHT_SIGA1_GID_TO_AP && gid < VHT_SIGA1_GID_NOT_TO_AP) {
		/* for MU packet we hacked Signal Tail field in VHT-SIG-A2 to save nss and mcs,
		 * copy from murate in d11 rx header.
		 * nss = bit 18:19 (for 11ac 2 bits to indicate maximum 4 nss)
		 * mcs = 20:23
		 */
		rate = (plcp[5] & 0xF0) >> 4;
		nss = ((plcp[5] & 0x0C) >> 2) + 1;
	} else {
		rate = (plcp[3] >> VHT_SIGA2_MCS_SHIFT);
		nss = ((plcp0 & VHT_SIGA1_NSTS_SHIFT_MASK_USER0) >>
			VHT_SIGA1_NSTS_SHIFT) + 1;
		if (plcp0 & VHT_SIGA1_STBC)
			nss = nss >> 1;
	}
	rate |= ((nss << WL_RSPEC_VHT_NSS_SHIFT) | WF_NON_HT_MCS);

	return rate;
}

/**
 * Function for computing NSS:MCS from HE SU PLCP or
 * MCS:LTF-GI from HE MU PLCP
 *
 * based on rev3.10 :
 * https://docs.google.com/spreadsheets/d/
 * 1eP6ZCRrtnF924ds1R-XmbcH0IdQ0WNJpS1-FHmWeb9g/edit#gid=1492656555
 *
 *	For HE SU frame:
 *		bit 0-3 mcs index
 *		bit 6-4 nsts for HE
 *		bit 7:	 1 for HE
 *	Note: bit 7 is used to indicate to the rate sel the mcs is a non HT mcs!
 *	Essentially it's the NSS:MCS portions of the rspec
 *
 *	For HE MU frame:
 *		bit 0-3 mcs index
 *		bit 4-5 LTF-GI value
 *		bit 6 STBC
 *	Essentially it's the MCS and LTF-GI portion of the rspec
 */
/* Macros to be used for calculating rate from PLCP */
#define HE_SU_PLCP2RATE_MCS_MASK	0x0F
#define HE_SU_PLCP2RATE_MCS_SHIFT	0
#define HE_SU_PLCP2RATE_NSS_MASK	0x70
#define HE_SU_PLCP2RATE_NSS_SHIFT	4
#define HE_MU_PLCP2RATE_LTF_GI_MASK	0x30
#define HE_MU_PLCP2RATE_LTF_GI_SHIFT	4
#define HE_MU_PLCP2RATE_STBC_MASK	0x40
#define HE_MU_PLCP2RATE_STBC_SHIFT	6

uint8
wf_he_plcp_to_rate(uint8 *plcp, bool is_mu)
{
	uint8 rate = 0;
	uint8 nss = 0;
	uint32 plcp0 = 0;
	uint32 plcp1 = 0;
	uint8 he_ltf_gi;
	uint8 stbc;

	ASSERT(plcp);

	BCM_REFERENCE(nss);
	BCM_REFERENCE(he_ltf_gi);

	plcp0 = ((plcp[3] << 24) | (plcp[2] << 16) | (plcp[1] << 8) | plcp[0]);
	plcp1 = ((plcp[5] << 8) | plcp[4]);

	if (!is_mu) {
		/* For SU frames return rate in MCS:NSS format */
		rate = ((plcp0 & HE_SU_RE_SIGA_MCS_MASK) >> HE_SU_RE_SIGA_MCS_SHIFT);
		nss = ((plcp0 & HE_SU_RE_SIGA_NSTS_MASK) >> HE_SU_RE_SIGA_NSTS_SHIFT) + 1;
		rate |= ((nss << HE_SU_PLCP2RATE_NSS_SHIFT) | WF_NON_HT_MCS);
	} else {
		/* For MU frames return rate in MCS:LTF-GI format */
		rate = (plcp0 & HE_MU_SIGA_SIGB_MCS_MASK) >> HE_MU_SIGA_SIGB_MCS_SHIFT;
		he_ltf_gi = (plcp0 & HE_MU_SIGA_GI_LTF_MASK) >> HE_MU_SIGA_GI_LTF_SHIFT;
		stbc = (plcp1 & HE_MU_SIGA_STBC_MASK) >> HE_MU_SIGA_STBC_SHIFT;

		/* LTF-GI shall take the same position as NSS */
		rate |= (he_ltf_gi << HE_MU_PLCP2RATE_LTF_GI_SHIFT);

		/* STBC needs to be filled in bit 6 */
		rate |= (stbc << HE_MU_PLCP2RATE_STBC_SHIFT);
	}

	return rate;
}

/**
 * Function for computing NSS:MCS from EHT SU PLCP or
 * MCS:LTF-GI from EHT MU PLCP
 *
 * TODO: add link to the HW spec.
 * FIXME: do we really need to support mu?
 */
uint8
wf_eht_plcp_to_rate(uint8 *plcp, bool is_mu)
{
	BCM_REFERENCE(plcp);
	BCM_REFERENCE(is_mu);
	ASSERT(!"wf_eht_plcp_to_rate: not implemented!");
	return 0;
}

/* ============================================ */
/* Moved from wlc_rate_def.c                    */
/* ============================================ */

/**
 * Some functions require a single stream MCS as an input parameter. Given an MCS, this function
 * returns the single spatial stream MCS equivalent.
 */
uint8
wf_get_single_stream_mcs(uint mcs)
{
	if (mcs < 32) {
		return mcs % 8;
	}
	switch (mcs) {
	case 32:
		return 32;
	case 87:
	case 99:
	case 101:
		return 87;	/* MCS 87: SS 1, MOD: 256QAM, CR 3/4 */
	default:
		return 88;	/* MCS 88: SS 1, MOD: 256QAM, CR 5/6 */
	}
}

/* ============================================ */
/* Moved from wlc_phy_iovar.c                   */
/* ============================================ */

const uint8 plcp_ofdm_rate_tbl[] = {
	DOT11_RATE_48M, /* 8: 48Mbps */
	DOT11_RATE_24M, /* 9: 24Mbps */
	DOT11_RATE_12M, /* A: 12Mbps */
	DOT11_RATE_6M,  /* B:  6Mbps */
	DOT11_RATE_54M, /* C: 54Mbps */
	DOT11_RATE_36M, /* D: 36Mbps */
	DOT11_RATE_18M, /* E: 18Mbps */
	DOT11_RATE_9M   /* F:  9Mbps */
};
