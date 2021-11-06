/*
 * Monitor Mode routines.
 * This header file housing the Monitor Mode routines implementation.
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

#include <bcmutils.h>
#include <bcmendian.h>
#include <hndd11.h>
#include <bcmwifi_channels.h>
#include <bcmwifi_radiotap.h>
#include <bcmwifi_monitor.h>
#include <bcmwifi_rates.h>
#include <monitor.h>
#include <d11_cfg.h>

struct monitor_info {
	ratespec_t	ampdu_rspec;	/* spec value for AMPDU sniffing */
	uint16		ampdu_counter;
	uint16		amsdu_len;
	uint8*		amsdu_pkt;
	int8		headroom;
	d11_info_t	*d11_info;
	uint8		ampdu_plcp[D11_PHY_HDR_LEN];
};

struct he_ltf_gi_info {
	uint8 gi;
	uint8 ltf_size;
	uint8 num_ltf;
};

struct he_mu_ltf_mp_info {
	uint8 num_ltf;
	uint8 mid_per;
};

/*
 * su ppdu - mapping of ltf and gi values from plcp to rtap data format
 * https://www.radiotap.org/fields/HE.html
 */
static const struct he_ltf_gi_info he_plcp2ltf_gi[4] = {
	{3, 0, 7}, /* reserved, reserved, reserved */
	{0, 2, 1}, /* 0.8us, 2x, 2x */
	{1, 2, 1}, /* 1.6us, 2x, 2x */
	{2, 3, 2}  /* 3.2us, 4x, 4x */
};

/*
 * mu ppdu - mapping of ru type value from phy rxstatus to rtap data format
 * https://www.radiotap.org/fields/HE.html
 */
static const uint8 he_mu_phyrxs2ru_type[7] = {
	4, /*    26-tone RU */
	5, /*    52-tone RU */
	6, /*   106-tone RU */
	7, /*   242-tone RU */
	8, /*   484-tone RU */
	9, /*   996-tone RU */
	10 /* 2x996-tone RU */
};

/*
 * mu ppdu - doppler:1, mapping of ltf and midamble periodicity values from plcp to rtap data format
 * https://www.radiotap.org/fields/HE.html
 */
static const struct he_mu_ltf_mp_info he_mu_plcp2ltf_mp[8] = {
	{0, 0},	/* 1x, 10 */
	{1, 0},	/* 2x, 10 */
	{2, 0},	/* 4x, 10 */
	{7, 0},	/* reserved, reserved */
	{0, 1},	/* 1x, 20 */
	{1, 1},	/* 2x, 20 */
	{2, 1},	/* 4x, 20 */
	{7, 0}	/* reserved, reserved */
};

/*
 * mu ppdu - doppler:0, mapping of ltf value from plcp to rtap data format
 * https://www.radiotap.org/fields/HE.html
 */
static const uint8 he_mu_plcp2ltf[8] = {
	0, /* 1x */
	1, /* 2x */
	2, /* 4x */
	3, /* 6x */
	4, /* 8x */
	7, /* reserved */
	7, /* reserved */
	7  /* reserved */
};

/** Calculate the rate of a received frame and return it as a ratespec (monitor mode) */
static ratespec_t
BCMFASTPATH(wlc_recv_mon_compute_rspec)(monitor_info_t* info, wlc_d11rxhdr_t *wrxh, uint8 *plcp)
{
	d11rxhdr_t *rxh = &wrxh->rxhdr;
	ratespec_t rspec = 0;
	uint16 phy_ft;
	uint corerev = info->d11_info->major_revid;
	uint corerev_minor = info->d11_info->minor_revid;
	BCM_REFERENCE(corerev_minor);

	phy_ft = D11PPDU_FT(rxh, corerev);
	switch (phy_ft) {
	case FT_CCK:
		rspec = CCK_RSPEC(CCK_PHY2MAC_RATE(((cck_phy_hdr_t *)plcp)->signal));
		rspec |= WL_RSPEC_BW_20MHZ;
		break;
	case FT_OFDM:
		rspec = OFDM_RSPEC(OFDM_PHY2MAC_RATE(((ofdm_phy_hdr_t *)plcp)->rlpt[0]));
		rspec |= WL_RSPEC_BW_20MHZ;
		break;
	case FT_HT: {
		uint ht_sig1, ht_sig2;
		uint8 stbc;

		ht_sig1 = plcp[0];	/* only interested in low 8 bits */
		ht_sig2 = plcp[3] | (plcp[4] << 8); /* only interested in low 10 bits */

		rspec = HT_RSPEC((ht_sig1 & HT_SIG1_MCS_MASK));
		if (ht_sig1 & HT_SIG1_CBW) {
			/* indicate rspec is for 40 MHz mode */
			rspec |= WL_RSPEC_BW_40MHZ;
		} else {
			/* indicate rspec is for 20 MHz mode */
			rspec |= WL_RSPEC_BW_20MHZ;
		}
		if (ht_sig2 & HT_SIG2_SHORT_GI)
			rspec |= WL_RSPEC_SGI;
		if (ht_sig2 & HT_SIG2_FEC_CODING)
			rspec |= WL_RSPEC_LDPC;
		stbc = ((ht_sig2 & HT_SIG2_STBC_MASK) >> HT_SIG2_STBC_SHIFT);
		if (stbc != 0) {
			rspec |= WL_RSPEC_STBC;
		}
		break;
	}
	case FT_VHT:
		rspec = wf_vht_plcp_to_rspec(plcp);
		break;
#ifdef WL11AX
	case FT_HE:
		rspec = wf_he_plcp_to_rspec(plcp);
		break;
#endif /* WL11AX */
#ifdef WL11BE
	case FT_EHT:
		rspec = wf_eht_plcp_to_rspec(plcp);
		break;
#endif
	default:
		/* return a valid rspec if not a debug/assert build */
		rspec = OFDM_RSPEC(6) | WL_RSPEC_BW_20MHZ;
		break;
	}

	return rspec;
} /* wlc_recv_compute_rspec */

static void
wlc_he_su_fill_rtap_data(struct wl_rxsts *sts, uint8 *plcp)
{
	ASSERT(plcp);

	/* he ppdu format */
	sts->data1 |= WL_RXS_HEF_SIGA_PPDU_SU;

	/* bss color */
	sts->data1 |= WL_RXS_HEF_SIGA_BSS_COLOR;
	sts->data3 |= HE_PACK_RTAP_FROM_PLCP(plcp, SU, BSS_COLOR);

	/* beam change */
	sts->data1 |= WL_RXS_HEF_SIGA_BEAM_CHANGE;
	sts->data3 |= HE_PACK_RTAP_FROM_PLCP(plcp, SU, BEAM_CHANGE);

	/* ul/dl */
	sts->data1 |= WL_RXS_HEF_SIGA_DL_UL;
	sts->data3 |= HE_PACK_RTAP_FROM_PLCP(plcp, SU, DL_UL);

	/* data mcs */
	sts->data1 |= WL_RXS_HEF_SIGA_MCS;
	sts->data3 |= HE_PACK_RTAP_FROM_PLCP(plcp, SU, MCS);

	/* data dcm */
	sts->data1 |= WL_RXS_HEF_SIGA_DCM;
	sts->data3 |= HE_PACK_RTAP_FROM_PLCP(plcp, SU, DCM);

	/* coding */
	sts->data1 |= WL_RXS_HEF_SIGA_CODING;
	sts->data3 |= HE_PACK_RTAP_FROM_PLCP(plcp, SU, CODING);

	/* ldpc extra symbol segment */
	sts->data1 |= WL_RXS_HEF_SIGA_LDPC;
	sts->data3 |= HE_PACK_RTAP_FROM_PLCP(plcp, SU, LDPC);

	/* stbc */
	sts->data1 |= WL_RXS_HEF_SIGA_STBC;
	sts->data3 |= HE_PACK_RTAP_FROM_PLCP(plcp, SU, STBC);

	/* spatial reuse */
	sts->data1 |= WL_RXS_HEF_SIGA_SPATIAL_REUSE;
	sts->data4 |= HE_PACK_RTAP_FROM_PLCP(plcp, SU, SR);

	/* data bw */
	sts->data1 |= WL_RXS_HEF_SIGA_BW;
	sts->data5 |= HE_PACK_RTAP_FROM_PLCP(plcp, SU, BW);

	/* gi */
	sts->data2 |= WL_RXS_HEF_SIGA_GI;
	sts->data5 |= HE_PACK_RTAP_GI_LTF_FROM_PLCP(plcp, SU, GI, gi);

	/* ltf symbol size */
	sts->data2 |= WL_RXS_HEF_SIGA_LTF_SIZE;
	sts->data5 |= HE_PACK_RTAP_GI_LTF_FROM_PLCP(plcp, SU, LTF_SIZE, ltf_size);

	/* number of ltf symbols */
	sts->data2 |= WL_RXS_HEF_SIGA_NUM_LTF;
	sts->data5 |= HE_PACK_RTAP_GI_LTF_FROM_PLCP(plcp, SU, NUM_LTF, num_ltf);

	/* pre-fec padding factor */
	sts->data2 |= WL_RXS_HEF_SIGA_PADDING;
	sts->data5 |= HE_PACK_RTAP_FROM_PLCP(plcp, SU, PADDING);

	/* txbf */
	sts->data2 |= WL_RXS_HEF_SIGA_TXBF;
	sts->data5 |= HE_PACK_RTAP_FROM_PLCP(plcp, SU, TXBF);

	/* pe disambiguity */
	sts->data2 |= WL_RXS_HEF_SIGA_PE;
	sts->data5 |= HE_PACK_RTAP_FROM_PLCP(plcp, SU, PE);

	/*
	 * if doppler (bit:41) is set in plcp to 1 then,
	 *     - bit:25    indicates 'midamble periodicity'
	 *     - bit:23-24 indicate 'nsts'
	 *
	 * if doppler (bit:41) is set to 0 then,
	 *     - bit:23-25 indicate 'nsts'
	 */
	if (HE_EXTRACT_FROM_PLCP(plcp, SU, DOPPLER)) {
		/* doppler */
		sts->data1 |= WL_RXS_HEF_SIGA_DOPPLER;
		sts->data6 |= HE_PACK_RTAP_FROM_PLCP(plcp, SU, DOPPLER);

		/* midamble periodicity */
		sts->data2 |= WL_RXS_HEF_SIGA_MIDAMBLE;
		sts->data6 |= HE_PACK_RTAP_FROM_PLCP(plcp, SU, MIDAMBLE);

		/* nsts */
		sts->data6 |= HE_PACK_RTAP_FROM_PLCP(plcp, SU, DOPPLER_SET_NSTS);
	} else {
		/* nsts */
		sts->data6 |= HE_PACK_RTAP_FROM_PLCP(plcp, SU, DOPPLER_NOTSET_NSTS);
	}

	/* txop */
	sts->data2 |= WL_RXS_HEF_SIGA_TXOP;
	sts->data6 |= HE_PACK_RTAP_FROM_PLCP(plcp, SU, TXOP);
}

static void
wlc_he_dl_ofdma_fill_rtap_data(struct wl_rxsts *sts, d11rxhdr_t *rxh,
	uint8 *plcp, uint32 corerev, uint32 corerev_minor)
{
	uint8 doppler, midamble, val;
	ASSERT(rxh);
	ASSERT(plcp);

	/* he ppdu format */
	sts->data1 |= WL_RXS_HEF_SIGA_PPDU_MU;

	/* bss color */
	sts->data1 |= WL_RXS_HEF_SIGA_BSS_COLOR;
	sts->data3 |= HE_PACK_RTAP_FROM_PLCP(plcp, MU, BSS_COLOR);

	/* beam change (doesn't apply to mu ppdu) */
	sts->data1 &= ~WL_RXS_HEF_SIGA_BEAM_CHANGE;

	/* ul/dl */
	sts->data1 |= WL_RXS_HEF_SIGA_DL_UL;
	sts->data3 |= HE_PACK_RTAP_FROM_PLCP(plcp, MU, DL_UL);

	/* data mcs */
	sts->data1 |= WL_RXS_HEF_SIGA_MCS;
	sts->data3 |= HE_PACK_RTAP_FROM_PRXS(rxh, corerev, corerev_minor, MCS);

	/* data dcm */
	sts->data1 |= WL_RXS_HEF_SIGA_DCM;
	sts->data3 |= HE_PACK_RTAP_FROM_PRXS(rxh, corerev, corerev_minor, DCM);

	/* coding */
	sts->data1 |= WL_RXS_HEF_SIGA_CODING;
	sts->data3 |= HE_PACK_RTAP_FROM_PRXS(rxh, corerev, corerev_minor, CODING);

	/* ldpc extra symbol segment */
	sts->data1 |= WL_RXS_HEF_SIGA_LDPC;
	sts->data3 |= HE_PACK_RTAP_FROM_PLCP(plcp, MU, LDPC);

	/* stbc */
	sts->data1 |= WL_RXS_HEF_SIGA_STBC;
	sts->data3 |= HE_PACK_RTAP_FROM_PLCP(plcp, MU, STBC);

	/* spatial reuse */
	sts->data1 |= WL_RXS_HEF_SIGA_SPATIAL_REUSE;
	sts->data4 |= HE_PACK_RTAP_FROM_PLCP(plcp, MU, SR);

	/* sta-id */
	sts->data1 |= WL_RXS_HEF_SIGA_STA_ID;
	sts->data4 |= HE_PACK_RTAP_FROM_PRXS(rxh, corerev, corerev_minor, STAID);

	/* ru allocation */
	val = he_mu_phyrxs2ru_type[D11PPDU_RU_TYPE(rxh, corerev, corerev_minor)];
	sts->data1 |= WL_RXS_HEF_SIGA_RU_ALLOC;
	sts->data5 |= HE_PACK_RTAP_FROM_VAL(val, RU_ALLOC);

	/* doppler */
	sts->data1 |= WL_RXS_HEF_SIGA_DOPPLER;
	sts->data6 |= HE_PACK_RTAP_FROM_PLCP(plcp, MU, DOPPLER);

	doppler = HE_EXTRACT_FROM_PLCP(plcp, MU, DOPPLER);
	midamble = HE_EXTRACT_FROM_PLCP(plcp, MU, MIDAMBLE);
	if (doppler) {
		/* number of ltf symbols */
		val = he_mu_plcp2ltf_mp[midamble].num_ltf;
		sts->data2 |= WL_RXS_HEF_SIGA_NUM_LTF;
		sts->data5 |= HE_PACK_RTAP_FROM_VAL(val, NUM_LTF);

		/* midamble periodicity */
		val = he_mu_plcp2ltf_mp[midamble].mid_per;
		sts->data2 |= WL_RXS_HEF_SIGA_MIDAMBLE;
		sts->data6 |= HE_PACK_RTAP_FROM_VAL(val, MIDAMBLE);
	} else {
		/* number of ltf symbols */
		val = he_mu_plcp2ltf[midamble];
		sts->data2 |= WL_RXS_HEF_SIGA_NUM_LTF;
		sts->data5 |= HE_PACK_RTAP_FROM_VAL(val, NUM_LTF);
	}

	/* nsts */
	sts->data6 |= HE_PACK_RTAP_FROM_PRXS(rxh, corerev, corerev_minor, NSTS);

	/* gi */
	sts->data2 |= WL_RXS_HEF_SIGA_GI;
	sts->data5 |= HE_PACK_RTAP_GI_LTF_FROM_PLCP(plcp, MU, GI, gi);

	/* ltf symbol size */
	sts->data2 |= WL_RXS_HEF_SIGA_LTF_SIZE;
	sts->data5 |= HE_PACK_RTAP_GI_LTF_FROM_PLCP(plcp, MU, LTF_SIZE, ltf_size);

	/* pre-fec padding factor */
	sts->data2 |= WL_RXS_HEF_SIGA_PADDING;
	sts->data5 |= HE_PACK_RTAP_FROM_PLCP(plcp, MU, PADDING);

	/* txbf */
	sts->data2 |= WL_RXS_HEF_SIGA_TXBF;
	sts->data5 |= HE_PACK_RTAP_FROM_PRXS(rxh, corerev, corerev_minor, TXBF);

	/* pe disambiguity */
	sts->data2 |= WL_RXS_HEF_SIGA_PE;
	sts->data5 |= HE_PACK_RTAP_FROM_PLCP(plcp, MU, PE);

	/* txop */
	sts->data2 |= WL_RXS_HEF_SIGA_TXOP;
	sts->data6 |= HE_PACK_RTAP_FROM_PLCP(plcp, MU, TXOP);
}

static void
wlc_he_dl_ofdma_fill_rtap_flag(struct wl_rxsts *sts, uint8 *plcp, uint32 corerev)
{
	ASSERT(plcp);

	/* sig-b mcs */
	sts->flag1 |= WL_RXS_HEF_SIGB_MCS_KNOWN;
	sts->flag1 |= HE_PACK_RTAP_FROM_PLCP(plcp, MU, SIGB_MCS);

	/* sig-b dcm */
	sts->flag1 |= WL_RXS_HEF_SIGB_DCM_KNOWN;
	sts->flag1 |= HE_PACK_RTAP_FROM_PLCP(plcp, MU, SIGB_DCM);

	/* sig-b compression */
	sts->flag1 |= WL_RXS_HEF_SIGB_COMP_KNOWN;
	sts->flag2 |= HE_PACK_RTAP_FROM_PLCP(plcp, MU, SIGB_COMP);

	/* # of he-sig-b symbols/mu-mimo users */
	sts->flag1 |= WL_RXS_HEF_NUM_SIGB_SYMB_KNOWN;
	sts->flag2 |= HE_PACK_RTAP_FROM_PLCP(plcp, MU, SIGB_SYM_MU_MIMO_USER);

	/* bandwidth from bandwidth field in he-sig-a */
	sts->flag2 |= WL_RXS_HEF_BW_SIGA_KNOWN;
	sts->flag2 |= HE_PACK_RTAP_FROM_PLCP(plcp, MU, BW_SIGA);

	/* preamble puncturing from bandwidth field in he-sig-a */
	sts->flag2 |= WL_RXS_HEF_PREPUNCR_SIGA_KNOWN;
	sts->flag2 |= HE_PACK_RTAP_FROM_PLCP(plcp, MU, PRE_PUNCR_SIGA);
}

static void
wlc_he_ul_ofdma_fill_rtap_data(struct wl_rxsts *sts, d11rxhdr_t *rxh, uint8 *plcp,
	uint32 corerev)
{
	ASSERT(rxh);
	ASSERT(plcp);

	BCM_REFERENCE(rxh);

	/* he ppdu format */
	sts->data1 |= WL_RXS_HEF_SIGA_PPDU_TRIG;

	/* bss color */
	sts->data1 |= WL_RXS_HEF_SIGA_BSS_COLOR;
	sts->data3 |= HE_PACK_RTAP_FROM_PLCP(plcp, TRIG, BSS_COLOR);

	/* beam change (doesn't apply to mu ppdu) */
	sts->data1 &= ~WL_RXS_HEF_SIGA_BEAM_CHANGE;

	/* ul/dl */
	sts->data1 |= WL_RXS_HEF_SIGA_DL_UL;
	sts->data3 |= HE_PACK_RTAP_FROM_VAL(1, DL_UL);

	/* txop */
	sts->data2 |= WL_RXS_HEF_SIGA_TXOP;
	sts->data6 |= HE_PACK_RTAP_FROM_PLCP(plcp, TRIG, TXOP);
}

/* recover 32bit TSF value from the 16bit TSF value */
/* assumption is time in rxh is within 65ms of the current tsf */
/* local TSF inserted in the rxh is at RxStart which is before 802.11 header */
static uint32
wlc_recover_tsf32(uint16 rxh_tsf, uint32 ts_tsf)
{
	uint16 rfdly;

	/* adjust rx dly added in RxTSFTime */
	/* comment in d11.h:
	 * BWL_PRE_PACKED_STRUCT struct d11rxhdr {
	 *	...
	 *	uint16 RxTSFTime;	RxTSFTime time of first MAC symbol + M_PHY_PLCPRX_DLY
	 *	...
	 * }
	 */

	/* TODO: add PHY type specific value here... */
	rfdly = M_BPHY_PLCPRX_DLY;

	rxh_tsf -= rfdly;

	return (((ts_tsf - rxh_tsf) & 0xFFFF0000) | rxh_tsf);
}

static uint8
wlc_vht_get_gid(uint8 *plcp)
{
	uint32 plcp0 = plcp[0] | (plcp[1] << 8);
	return (plcp0 & VHT_SIGA1_GID_MASK) >> VHT_SIGA1_GID_SHIFT;
}

static uint16
wlc_vht_get_aid(uint8 *plcp)
{
	uint32 plcp0 = plcp[0] | (plcp[1] << 8) | (plcp[2] << 16);
	return (plcp0 & VHT_SIGA1_PARTIAL_AID_MASK) >> VHT_SIGA1_PARTIAL_AID_SHIFT;
}

static bool
wlc_vht_get_txop_ps_not_allowed(uint8 *plcp)
{
	return !!(plcp[2] & (VHT_SIGA1_TXOP_PS_NOT_ALLOWED >> 16));
}

static bool
wlc_vht_get_sgi_nsym_da(uint8 *plcp)
{
	return !!(plcp[3] & VHT_SIGA2_GI_W_MOD10);
}

static bool
wlc_vht_get_ldpc_extra_symbol(uint8 *plcp)
{
	return !!(plcp[3] & VHT_SIGA2_LDPC_EXTRA_OFDM_SYM);
}

static bool
wlc_vht_get_beamformed(uint8 *plcp)
{
	return !!(plcp[4] & (VHT_SIGA2_BEAMFORM_ENABLE >> 8));
}
/* Convert htflags and mcs values to
* rate in units of 500kbps
*/
static uint16
wlc_ht_phy_get_rate(uint8 htflags, uint8 mcs)
{

	ratespec_t rspec = HT_RSPEC(mcs);

	if (htflags & WL_RXS_HTF_40)
		rspec |= WL_RSPEC_BW_40MHZ;

	if (htflags & WL_RXS_HTF_SGI)
		rspec |= WL_RSPEC_SGI;

	return RSPEC2KBPS(rspec)/500;
}

static void
bcmwifi_update_rxpwr_per_ant(monitor_pkt_rxsts_t *pkt_rxsts, wlc_d11rxhdr_t *wrxh)
{
	int i = 0;
	wlc_d11rxhdr_ext_t *wrxh_ext = (wlc_d11rxhdr_ext_t *)((uint8 *)wrxh - WLC_SWRXHDR_EXT_LEN);

	BCM_REFERENCE(wrxh_ext);

	pkt_rxsts->corenum = 0;

	for (i = 0; i < WL_RSSI_ANT_MAX; i++) {
#ifdef BCM_MON_QDBM_RSSI
		pkt_rxsts->rxpwr[i].dBm = wrxh_ext->rxpwr[i].dBm;
		pkt_rxsts->rxpwr[i].decidBm  = wrxh_ext->rxpwr[i].decidBm;
#else
		pkt_rxsts->rxpwr[i].dBm = wrxh->rxpwr[i];
		pkt_rxsts->rxpwr[i].decidBm  = 0;
#endif
		if (pkt_rxsts->rxpwr[i].dBm == 0) {
			break;
		}
		pkt_rxsts->corenum ++;
	}
}

static void
bcmwifi_parse_ampdu(monitor_info_t *info, d11rxhdr_t *rxh, uint16 subtype, ratespec_t rspec,
		uint8 *plcp, struct wl_rxsts *sts)
{
	uint32 corerev = info->d11_info->major_revid;
	uint32 corerev_minor = info->d11_info->minor_revid;
	uint32 ft = D11PPDU_FT(rxh, corerev);
	uint8 plcp_len = D11_PHY_RXPLCP_LEN(corerev);
	BCM_REFERENCE(corerev_minor);
	if ((subtype == FC_SUBTYPE_QOS_DATA) || (subtype == FC_SUBTYPE_QOS_NULL)) {
		/* A-MPDU parsing */
		switch (ft) {
		case FT_HT:
			if (WLC_IS_MIMO_PLCP_AMPDU(plcp)) {
				sts->nfrmtype |= WL_RXS_NFRM_AMPDU_FIRST;
				/* Save the rspec & plcp for later */
				info->ampdu_rspec = rspec;
				/* src & dst len are same */
				(void)memcpy_s(info->ampdu_plcp, plcp_len, plcp, plcp_len);
			} else if (!PLCP_VALID(plcp)) {
				sts->nfrmtype |= WL_RXS_NFRM_AMPDU_SUB;
				/* Use the saved rspec & plcp */
				rspec = info->ampdu_rspec;
				/* src & dst len are same */
				(void)memcpy_s(plcp, plcp_len, info->ampdu_plcp, plcp_len);
			}
			break;

		case FT_VHT:
		case FT_HE:
		case FT_EHT:
			if (PLCP_VALID(plcp) &&
				!IS_PHYRXHDR_VALID(rxh, corerev, corerev_minor)) {
				/* First MPDU:
				 * PLCP header is valid, Phy RxStatus is not valid
				 */
				sts->nfrmtype |= WL_RXS_NFRM_AMPDU_FIRST;
				/* Save the rspec & plcp for later */
				info->ampdu_rspec = rspec;
				/* src & dst len are same */
				(void)memcpy_s(info->ampdu_plcp, plcp_len, plcp, plcp_len);
				info->ampdu_counter++;
			} else if (!PLCP_VALID(plcp) &&
			           !IS_PHYRXHDR_VALID(rxh, corerev, corerev_minor)) {
				/* Sub MPDU: * PLCP header is not valid,
				 * Phy RxStatus is not valid
				 */
				sts->nfrmtype |= WL_RXS_NFRM_AMPDU_SUB;
				/* Use the saved rspec & plcp */
				rspec = info->ampdu_rspec;
				/* src & dst len are same */
				(void)memcpy_s(plcp, plcp_len, info->ampdu_plcp, plcp_len);
			} else if (PLCP_VALID(plcp) &&
			           IS_PHYRXHDR_VALID(rxh, corerev, corerev_minor)) {
				/* MPDU is not a part of A-MPDU:
				 * PLCP header is valid and Phy RxStatus is valid
				 */
				info->ampdu_counter++;
			} else {
				/* Last MPDU */
				/* done to take care of the last MPDU in A-mpdu
				 * VHT packets are considered A-mpdu
				 * Use the saved rspec
				 */
				rspec = info->ampdu_rspec;
				/* src & dst len are same */
				(void)memcpy_s(plcp, plcp_len, info->ampdu_plcp, plcp_len);
			}

			sts->ampdu_counter = info->ampdu_counter;
			break;

		case FT_OFDM:
			break;
		default:
			printf("invalid frame type: %d\n", ft);
			break;
		}
	}
}

static void
bcmwifi_update_rate_modulation_info(monitor_info_t *info, d11rxhdr_t *rxh, d11rxhdr_t *rxh_last,
		ratespec_t rspec, uint8* plcp, struct wl_rxsts *sts)
{
	uint32 corerev = info->d11_info->major_revid;
	uint32 corerev_minor = info->d11_info->minor_revid;

	/* prepare rate/modulation info */
	if (RSPEC_ISVHT(rspec)) {
		uint32 bw = RSPEC_BW(rspec);
		/* prepare VHT rate/modulation info */
		sts->nss = (rspec & WL_RSPEC_VHT_NSS_MASK) >> WL_RSPEC_VHT_NSS_SHIFT;
		sts->mcs = (rspec & WL_RSPEC_VHT_MCS_MASK);

		if (CHSPEC_IS80(sts->chanspec)) {
			if (bw == WL_RSPEC_BW_20MHZ) {
				switch (CHSPEC_CTL_SB(sts->chanspec)) {
				default:
				case WL_CHANSPEC_CTL_SB_LL:
					sts->bw = WL_RXS_VHT_BW_20LL;
					break;
				case WL_CHANSPEC_CTL_SB_LU:
					sts->bw = WL_RXS_VHT_BW_20LU;
					break;
				case WL_CHANSPEC_CTL_SB_UL:
					sts->bw = WL_RXS_VHT_BW_20UL;
					break;
				case WL_CHANSPEC_CTL_SB_UU:
					sts->bw = WL_RXS_VHT_BW_20UU;
					break;
				}
			} else if (bw == WL_RSPEC_BW_40MHZ) {
				switch (CHSPEC_CTL_SB(sts->chanspec)) {
				default:
				case WL_CHANSPEC_CTL_SB_L:
					sts->bw = WL_RXS_VHT_BW_40L;
					break;
				case WL_CHANSPEC_CTL_SB_U:
					sts->bw = WL_RXS_VHT_BW_40U;
					break;
				}
			} else {
				sts->bw = WL_RXS_VHT_BW_80;
			}
		} else if (CHSPEC_IS40(sts->chanspec)) {
			if (bw == WL_RSPEC_BW_20MHZ) {
				switch (CHSPEC_CTL_SB(sts->chanspec)) {
				default:
				case WL_CHANSPEC_CTL_SB_L:
					sts->bw = WL_RXS_VHT_BW_20L;
					break;
				case WL_CHANSPEC_CTL_SB_U:
					sts->bw = WL_RXS_VHT_BW_20U;
					break;
				}
			} else if (bw == WL_RSPEC_BW_40MHZ) {
				sts->bw = WL_RXS_VHT_BW_40;
			}
		} else {
			sts->bw = WL_RXS_VHT_BW_20;
		}

		if (RSPEC_ISSTBC(rspec))
			sts->vhtflags |= WL_RXS_VHTF_STBC;
		if (wlc_vht_get_txop_ps_not_allowed(plcp))
			sts->vhtflags |= WL_RXS_VHTF_TXOP_PS;
		if (RSPEC_ISSGI(rspec)) {
			sts->vhtflags |= WL_RXS_VHTF_SGI;
			if (wlc_vht_get_sgi_nsym_da(plcp))
				sts->vhtflags |= WL_RXS_VHTF_SGI_NSYM_DA;
		}
		if (RSPEC_ISLDPC(rspec)) {
			sts->coding = WL_RXS_VHTF_CODING_LDCP;
			if (wlc_vht_get_ldpc_extra_symbol(plcp)) {
				/* need to un-set for MU-MIMO */
				sts->vhtflags |= WL_RXS_VHTF_LDPC_EXTRA;
			}
		}
		if (wlc_vht_get_beamformed(plcp))
			sts->vhtflags |= WL_RXS_VHTF_BF;

		sts->gid = wlc_vht_get_gid(plcp);
		sts->aid = wlc_vht_get_aid(plcp);
		sts->datarate = RSPEC2KBPS(rspec)/500;
	} else if (RSPEC_ISHT(rspec)) {
		/* prepare HT rate/modulation info */
		sts->mcs = (rspec & WL_RSPEC_HT_MCS_MASK);

		if (CHSPEC_IS40(sts->chanspec) || CHSPEC_IS80(sts->chanspec)) {
			uint32 bw = RSPEC_BW(rspec);

			if (bw == WL_RSPEC_BW_20MHZ) {
				if (CHSPEC_CTL_SB(sts->chanspec) == WL_CHANSPEC_CTL_SB_L) {
					sts->htflags = WL_RXS_HTF_20L;
				} else {
					sts->htflags = WL_RXS_HTF_20U;
				}
			} else if (bw == WL_RSPEC_BW_40MHZ) {
				sts->htflags = WL_RXS_HTF_40;
			}
		}

		if (RSPEC_ISSGI(rspec))
			sts->htflags |= WL_RXS_HTF_SGI;
		if (RSPEC_ISLDPC(rspec))
			sts->htflags |= WL_RXS_HTF_LDPC;
		if (RSPEC_ISSTBC(rspec))
			sts->htflags |= (1 << WL_RXS_HTF_STBC_SHIFT);

		sts->datarate = wlc_ht_phy_get_rate(sts->htflags, sts->mcs);
	} else if (FALSE ||
#ifdef WL11BE
		RSPEC_ISHEEXT(rspec) ||
#else
		RSPEC_ISHE(rspec) ||
#endif
		FALSE) {
		sts->nss = (rspec & WL_RSPEC_NSS_MASK) >> WL_RSPEC_NSS_SHIFT;
		sts->mcs = (rspec & WL_RSPEC_MCS_MASK);

		if (D11PPDU_ISMU_REV80(rxh_last, corerev, corerev_minor)) {
			if (IS_PHYRXHDR_VALID(rxh_last, corerev, corerev_minor)) {
				uint16 ff_type = D11PPDU_FF_TYPE(rxh_last,
					corerev, corerev_minor);

				switch (ff_type) {
				case HE_MU_PPDU:
					wlc_he_dl_ofdma_fill_rtap_data(sts, rxh_last,
						plcp, corerev, corerev_minor);
					wlc_he_dl_ofdma_fill_rtap_flag(sts, plcp, corerev);
					break;
				case HE_TRIG_PPDU:
					wlc_he_ul_ofdma_fill_rtap_data(sts, rxh_last,
						plcp, corerev);
					break;
				default:
					/* should not have come here */
					ASSERT(0);
					break;
				}
			}
		} else {
			/* frame format is either SU or SU_RE (assumption only SU is supported) */
			wlc_he_su_fill_rtap_data(sts, plcp);
		}
	} else {
		/* round non-HT data rate to nearest 500bkps unit */
		sts->datarate = RSPEC2KBPS(rspec)/500;
	}
}

/* Convert RX hardware status to standard format and send to wl_monitor
 * assume p points to plcp header
 */
static uint16
wl_d11rx_to_rxsts(monitor_info_t* info, monitor_pkt_info_t* pkt_info, wlc_d11rxhdr_t *wrxh,
		wlc_d11rxhdr_t *wrxh_last, void *pkt, uint16 len, void* pout, uint16 pad_req)
{
	struct wl_rxsts sts;
	monitor_pkt_rxsts_t pkt_rxsts;
	ratespec_t rspec;
	uint16 chan_num;
	uint8 *plcp;
	uint8 *p = (uint8*)pkt;
	uint8 hwrxoff = 0;
	uint32 corerev = 0;
	uint32 corerev_minor = 0;
	struct dot11_header *h;
	uint16 subtype;
	d11rxhdr_t *rxh = &(wrxh->rxhdr);
	d11rxhdr_t *rxh_last = &(wrxh_last->rxhdr);
	d11_info_t* d11i = info->d11_info;
	uint8 plcp_len = 0;

	BCM_REFERENCE(chan_num);

	ASSERT(p);
	ASSERT(info);
	pkt_rxsts.rxsts = &sts;

	hwrxoff = (pkt_info->marker >> 16) & 0xff;
	corerev = d11i->major_revid;
	corerev_minor = d11i->minor_revid;
	BCM_REFERENCE(corerev_minor);

	plcp = (uint8*)p + hwrxoff;
	plcp_len = D11_PHY_RXPLCP_LEN(corerev);

	/* only non short rxstatus is expected */
	if (IS_D11RXHDRSHORT(rxh, corerev, corerev_minor)) {
		printf("short rxstatus is not expected here!\n");
		ASSERT(0);
		return 0;
	}

	if (RXHDR_GET_PAD_PRES(rxh, corerev, corerev_minor)) {
		plcp += 2;
	}

	bzero((void *)&sts, sizeof(wl_rxsts_t));

	sts.mactime = wlc_recover_tsf32(pkt_info->ts.ts_high, pkt_info->ts.ts_low);

	/* update rxpwr per antenna */
	bcmwifi_update_rxpwr_per_ant(&pkt_rxsts, wrxh);

	/* calculate rspec based on ppdu frame type */
	rspec = wlc_recv_mon_compute_rspec(info, wrxh, plcp);

	h = (struct dot11_header *)(plcp + plcp_len);
	subtype = (ltoh16(h->fc) & FC_SUBTYPE_MASK) >> FC_SUBTYPE_SHIFT;

	/* parse & cache respec for ampdu */
	bcmwifi_parse_ampdu(info, rxh, subtype, rspec, plcp, &sts);

	/* A-MSDU parsing */
	if (RXHDR_GET_AMSDU(rxh, corerev, corerev_minor)) {
		/* it's chained buffer, break it if necessary */
		sts.nfrmtype |= WL_RXS_NFRM_AMSDU_FIRST | WL_RXS_NFRM_AMSDU_SUB;
	}

	sts.signal = (pkt_info->marker >> 8) & 0xff;
	sts.noise = (int8)pkt_info->marker;
	sts.chanspec = D11RXHDR_ACCESS_VAL(rxh, corerev, corerev_minor, RxChan);

	if (wf_chspec_malformed(sts.chanspec)) {
		printf("Malformed chspec, %x\n", sts.chanspec);
		return 0;
	}

	/* 4360: is chan_num supposed to be primary or CF channel? */
	chan_num = CHSPEC_CHANNEL(sts.chanspec);

	if (PRXS5_ACPHY_DYNBWINNONHT(rxh))
		sts.vhtflags |= WL_RXS_VHTF_DYN_BW_NONHT;
	else
		sts.vhtflags &= ~WL_RXS_VHTF_DYN_BW_NONHT;

	switch (PRXS5_ACPHY_CHBWINNONHT(rxh)) {
		default: case PRXS5_ACPHY_CHBWINNONHT_20MHZ:
			sts.bw_nonht = WLC_20_MHZ;
			break;
		case PRXS5_ACPHY_CHBWINNONHT_40MHZ:
			sts.bw_nonht = WLC_40_MHZ;
			break;
		case PRXS5_ACPHY_CHBWINNONHT_80MHZ:
			sts.bw_nonht = WLC_80_MHZ;
			break;
		case PRXS5_ACPHY_CHBWINNONHT_160MHZ:
			sts.bw_nonht = WLC_160_MHZ;
			break;
	}

	/* update rate and modulation info */
	bcmwifi_update_rate_modulation_info(info, rxh, rxh_last, rspec, plcp, &sts);

	sts.pktlength = FRAMELEN(corerev, corerev_minor, rxh) - plcp_len;

	sts.phytype = WL_RXS_PHY_N;

	if (RSPEC_ISCCK(rspec)) {
		sts.encoding = WL_RXS_ENCODING_DSSS_CCK;
		sts.preamble = (PRXS_SHORTH(rxh, corerev, corerev_minor) ?
				WL_RXS_PREAMBLE_SHORT : WL_RXS_PREAMBLE_LONG);
	} else if (RSPEC_ISOFDM(rspec)) {
		sts.encoding = WL_RXS_ENCODING_OFDM;
		sts.preamble = WL_RXS_PREAMBLE_SHORT;
	} if (RSPEC_ISVHT(rspec)) {
		sts.encoding = WL_RXS_ENCODING_VHT;
	} else if (RSPEC_ISHE(rspec)) {
		sts.encoding = WL_RXS_ENCODING_HE;
	} else if (RSPEC_ISEHT(rspec)) {
		sts.encoding = WL_RXS_ENCODING_EHT;
	} else {	/* MCS rate */
		sts.encoding = WL_RXS_ENCODING_HT;
		sts.preamble = (uint32)((D11HT_MMPLCPLen(rxh) != 0) ?
			WL_RXS_PREAMBLE_HT_MM : WL_RXS_PREAMBLE_HT_GF);
	}

	/* translate error code */
	if (D11RXHDR_ACCESS_VAL(rxh, corerev, corerev_minor, RxStatus1) & RXS_DECERR)
		sts.pkterror |= WL_RXS_DECRYPT_ERR;
	if (D11RXHDR_ACCESS_VAL(rxh, corerev, corerev_minor, RxStatus1) & RXS_FCSERR)
		sts.pkterror |= WL_RXS_CRC_ERROR;

	if (RXHDR_GET_PAD_PRES(rxh, corerev, corerev_minor)) {
		p += 2; len -= 2;
	}

	p += (hwrxoff + D11_PHY_RXPLCP_LEN(corerev));
	len -= (hwrxoff + D11_PHY_RXPLCP_LEN(corerev));
	return (wl_rxsts_to_rtap(&pkt_rxsts, p, len, pout, pad_req));
}

#ifndef MONITOR_DNGL_CONV
/* Collect AMSDU subframe packets */
static uint16
wl_monitor_amsdu(monitor_info_t* info, monitor_pkt_info_t* pkt_info, wlc_d11rxhdr_t *wrxh,
	wlc_d11rxhdr_t *wrxh_last, void *pkt, uint16 len, void* pout, uint16* offset)
{
	uint8 *p = pkt;
	uint8 hwrxoff = (pkt_info->marker >> 16) & 0xff;
	uint16 frame_len = 0;
	uint16 aggtype = (wrxh->rxhdr.lt80.RxStatus2 & RXS_AGGTYPE_MASK) >> RXS_AGGTYPE_SHIFT;

	switch (aggtype) {
	case RXS_AMSDU_FIRST:
	case RXS_AMSDU_N_ONE:
		/* Flush any previously collected */
		if (info->amsdu_len) {
			info->amsdu_len = 0;
		}

		info->headroom = MAX_RADIOTAP_SIZE - D11_PHY_RXPLCP_LEN(corerev) - hwrxoff;
		info->headroom -= (wrxh->rxhdr.lt80.RxStatus1 & RXS_PBPRES) ? 2 : 0;

		/* Save the new starting AMSDU subframe */
		info->amsdu_len = len;
		info->amsdu_pkt = (uint8*)pout + (info->headroom > 0 ?
			info->headroom : 0);

		memcpy(info->amsdu_pkt, p, len);

		if (aggtype == RXS_AMSDU_N_ONE) {
			/* all-in-one AMSDU subframe */
			frame_len = wl_d11rx_to_rxsts(info, pkt_info, wrxh, wrxh, p,
				len, info->amsdu_pkt - info->headroom, 0);

			*offset = ABS(info->headroom);
			frame_len += *offset;

			info->amsdu_len = 0;
		}
		break;

	case RXS_AMSDU_INTERMEDIATE:
	case RXS_AMSDU_LAST:
	default:
		/* Check for previously collected */
		if (info->amsdu_len) {
			/* Append next AMSDU subframe */
			p += hwrxoff; len -= hwrxoff;

			if (wrxh->rxhdr.lt80.RxStatus1 & RXS_PBPRES) {
				p += 2;	len -= 2;
			}

			memcpy(info->amsdu_pkt + info->amsdu_len, p, len);
			info->amsdu_len += len;

			/* complete AMSDU frame */
			if (aggtype == RXS_AMSDU_LAST) {
				frame_len = wl_d11rx_to_rxsts(info, pkt_info, wrxh, wrxh,
					info->amsdu_pkt, info->amsdu_len,
					info->amsdu_pkt - info->headroom, 0);

				*offset = ABS(info->headroom);
				frame_len += *offset;

				info->amsdu_len = 0;
			}
		}
		break;
	}

	return frame_len;
}
#endif /* MONITOR_DNGL_CONV */

uint16 bcmwifi_monitor_create(monitor_info_t** info)
{
	*info = MALLOCZ(NULL, sizeof(struct monitor_info));
	if ((*info) == NULL) {
		return FALSE;
	}

	(*info)->d11_info = MALLOCZ(NULL, sizeof(struct d11_info));
	if ((*info)->d11_info == NULL) {
		goto fail;
	}

	return TRUE;

fail:
	bcmwifi_monitor_delete(*info);

	return FALSE;
}

void
bcmwifi_set_corerev_major(monitor_info_t* info, int8 corerev)
{
	d11_info_t* d11i = info->d11_info;
	d11i->major_revid = corerev;
}

void
bcmwifi_set_corerev_minor(monitor_info_t* info, int8 corerev)
{
	d11_info_t* d11i = info->d11_info;
	d11i->minor_revid = corerev;
}

void
bcmwifi_monitor_delete(monitor_info_t* info)
{
	if (info == NULL) {
		return;
	}

	if (info->d11_info != NULL) {
		MFREE(NULL, info->d11_info, sizeof(struct d11_info));
	}

	MFREE(NULL, info, sizeof(struct monitor_info));
}

uint16
bcmwifi_monitor(monitor_info_t* info, monitor_pkt_info_t* pkt_info, void *pkt, uint16 len,
		void* pout, uint16* offset, uint16 pad_req, void *wrxh_in, void *wrxh_last)
{
	wlc_d11rxhdr_t *wrxh;
	int hdr_ext_offset = 0;

#ifdef MONITOR_DNGL_CONV
	wrxh = (wlc_d11rxhdr_t *)wrxh_in;
	if (info == NULL) {
		return 0;
	}
#else

#ifdef BCM_MON_QDBM_RSSI
	hdr_ext_offset  = WLC_SWRXHDR_EXT_LEN;
#endif
	/* move beyond the extension, if any */
	pkt = (void *)((uint8 *)pkt + hdr_ext_offset);
	wrxh = (wlc_d11rxhdr_t *)pkt;

	if ((wrxh->rxhdr.lt80.RxStatus2 & htol16(RXS_AMSDU_MASK))) {
		/* Need to add support for AMSDU */
		return wl_monitor_amsdu(info, pkt_info, wrxh, wrxh_last, pkt, len, pout, offset);
	} else
#endif /* NO MONITOR_DNGL_CONV */
	{
		info->amsdu_len = 0; /* reset amsdu */
		*offset = 0;
		return wl_d11rx_to_rxsts(info, pkt_info, wrxh, wrxh_last,
		                         pkt, len - hdr_ext_offset, pout, pad_req);
	}
}
