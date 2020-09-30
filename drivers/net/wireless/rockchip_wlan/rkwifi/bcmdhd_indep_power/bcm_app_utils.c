/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Misc utility routines used by kernel or app-level.
 * Contents are wifi-specific, used by any kernel or app-level
 * software that might want wifi things as it grows.
 *
 * Copyright (C) 1999-2017, Broadcom Corporation
 * 
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 * 
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 * 
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: bcm_app_utils.c 623866 2016-03-09 11:58:34Z $
 */

#include <typedefs.h>

#ifdef BCMDRIVER
#include <osl.h>
#define strtoul(nptr, endptr, base) bcm_strtoul((nptr), (endptr), (base))
#define tolower(c) (bcm_isupper((c)) ? ((c) + 'a' - 'A') : (c))
#else /* BCMDRIVER */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#ifndef ASSERT
#define ASSERT(exp)
#endif
#endif /* BCMDRIVER */
#include <bcmwifi_channels.h>

#if defined(WIN32) && (defined(BCMDLL) || defined(WLMDLL))
#include <bcmstdlib.h>	/* For wl/exe/GNUmakefile.brcm_wlu and GNUmakefile.wlm_dll */
#endif

#include <bcmutils.h>
#include <wlioctl.h>
#include <wlioctl_utils.h>

#ifndef BCMDRIVER
/*	Take an array of measurments representing a single channel over time and return
	a summary. Currently implemented as a simple average but could easily evolve
	into more cpomplex alogrithms.
*/
cca_congest_channel_req_t *
cca_per_chan_summary(cca_congest_channel_req_t *input, cca_congest_channel_req_t *avg, bool percent)
{
	int sec;
	cca_congest_t totals;

	totals.duration  = 0;
	totals.congest_ibss  = 0;
	totals.congest_obss  = 0;
	totals.interference  = 0;
	avg->num_secs = 0;

	for (sec = 0; sec < input->num_secs; sec++) {
		if (input->secs[sec].duration) {
			totals.duration += input->secs[sec].duration;
			totals.congest_ibss += input->secs[sec].congest_ibss;
			totals.congest_obss += input->secs[sec].congest_obss;
			totals.interference += input->secs[sec].interference;
			avg->num_secs++;
		}
	}
	avg->chanspec = input->chanspec;

	if (!avg->num_secs || !totals.duration)
		return (avg);

	if (percent) {
		avg->secs[0].duration = totals.duration / avg->num_secs;
		avg->secs[0].congest_ibss = totals.congest_ibss * 100/totals.duration;
		avg->secs[0].congest_obss = totals.congest_obss * 100/totals.duration;
		avg->secs[0].interference = totals.interference * 100/totals.duration;
	} else {
		avg->secs[0].duration = totals.duration / avg->num_secs;
		avg->secs[0].congest_ibss = totals.congest_ibss / avg->num_secs;
		avg->secs[0].congest_obss = totals.congest_obss / avg->num_secs;
		avg->secs[0].interference = totals.interference / avg->num_secs;
	}

	return (avg);
}

static void
cca_info(uint8 *bitmap, int num_bits, int *left, int *bit_pos)
{
	int i;
	for (*left = 0, i = 0; i < num_bits; i++) {
		if (isset(bitmap, i)) {
			(*left)++;
			*bit_pos = i;
		}
	}
}

static uint8
spec_to_chan(chanspec_t chspec)
{
	uint8 center_ch, edge, primary, sb;

	center_ch = CHSPEC_CHANNEL(chspec);

	if (CHSPEC_BW_LE20(chspec)) {
		return center_ch;
	} else {
		/* the lower edge of the wide channel is half the bw from
		 * the center channel.
		 */
		if (CHSPEC_IS40(chspec)) {
			edge = center_ch - CH_20MHZ_APART;
		} else {
			/* must be 80MHz (until we support more) */
			ASSERT(CHSPEC_IS80(chspec));
			edge = center_ch - CH_40MHZ_APART;
		}

		/* find the channel number of the lowest 20MHz primary channel */
		primary = edge + CH_10MHZ_APART;

		/* select the actual subband */
		sb = (chspec & WL_CHANSPEC_CTL_SB_MASK) >> WL_CHANSPEC_CTL_SB_SHIFT;
		primary = primary + sb * CH_20MHZ_APART;

		return primary;
	}
}

/*
	Take an array of measumrements representing summaries of different channels.
	Return a recomended channel.
	Interference is evil, get rid of that first.
	Then hunt for lowest Other bss traffic.
	Don't forget that channels with low duration times may not have accurate readings.
	For the moment, do not overwrite input array.
*/
int
cca_analyze(cca_congest_channel_req_t *input[], int num_chans, uint flags, chanspec_t *answer)
{
	uint8 *bitmap = NULL;	/* 38 Max channels needs 5 bytes  = 40 */
	int i, left, winner, ret_val = 0;
	uint32 min_obss = 1 << 30;
	uint bitmap_sz;

	bitmap_sz = CEIL(num_chans, NBBY);
	bitmap = (uint8 *)malloc(bitmap_sz);
	if (bitmap == NULL) {
		printf("unable to allocate memory\n");
		return BCME_NOMEM;
	}

	memset(bitmap, 0, bitmap_sz);
	/* Initially, all channels are up for consideration */
	for (i = 0; i < num_chans; i++) {
		if (input[i]->chanspec)
			setbit(bitmap, i);
	}
	cca_info(bitmap, num_chans, &left, &i);
	if (!left) {
		ret_val = CCA_ERRNO_TOO_FEW;
		goto f_exit;
	}

	/* Filter for 2.4 GHz Band */
	if (flags & CCA_FLAG_2G_ONLY) {
		for (i = 0; i < num_chans; i++) {
			if (!CHSPEC_IS2G(input[i]->chanspec))
				clrbit(bitmap, i);
		}
	}
	cca_info(bitmap, num_chans, &left, &i);
	if (!left) {
		ret_val = CCA_ERRNO_BAND;
		goto f_exit;
	}

	/* Filter for 5 GHz Band */
	if (flags & CCA_FLAG_5G_ONLY) {
		for (i = 0; i < num_chans; i++) {
			if (!CHSPEC_IS5G(input[i]->chanspec))
				clrbit(bitmap, i);
		}
	}
	cca_info(bitmap, num_chans, &left, &i);
	if (!left) {
		ret_val = CCA_ERRNO_BAND;
		goto f_exit;
	}

	/* Filter for Duration */
	if (!(flags & CCA_FLAG_IGNORE_DURATION)) {
		for (i = 0; i < num_chans; i++) {
			if (input[i]->secs[0].duration < CCA_THRESH_MILLI)
				clrbit(bitmap, i);
		}
	}
	cca_info(bitmap, num_chans, &left, &i);
	if (!left) {
		ret_val = CCA_ERRNO_DURATION;
		goto f_exit;
	}

	/* Filter for 1 6 11 on 2.4 Band */
	if (flags &  CCA_FLAGS_PREFER_1_6_11) {
		int tmp_channel = spec_to_chan(input[i]->chanspec);
		int is2g = CHSPEC_IS2G(input[i]->chanspec);
		for (i = 0; i < num_chans; i++) {
			if (is2g && tmp_channel != 1 && tmp_channel != 6 && tmp_channel != 11)
				clrbit(bitmap, i);
		}
	}
	cca_info(bitmap, num_chans, &left, &i);
	if (!left) {
		ret_val = CCA_ERRNO_PREF_CHAN;
		goto f_exit;
	}

	/* Toss high interference interference */
	if (!(flags & CCA_FLAG_IGNORE_INTERFER)) {
		for (i = 0; i < num_chans; i++) {
			if (input[i]->secs[0].interference > CCA_THRESH_INTERFERE)
				clrbit(bitmap, i);
		}
		cca_info(bitmap, num_chans, &left, &i);
		if (!left) {
			ret_val = CCA_ERRNO_INTERFER;
			goto f_exit;
		}
	}

	/* Now find lowest obss */
	winner = 0;
	for (i = 0; i < num_chans; i++) {
		if (isset(bitmap, i) && input[i]->secs[0].congest_obss < min_obss) {
			winner = i;
			min_obss = input[i]->secs[0].congest_obss;
		}
	}
	*answer = input[winner]->chanspec;
	f_exit:
	free(bitmap);	/* free the allocated memory for bitmap */
	return ret_val;
}
#endif /* !BCMDRIVER */

/* offset of cntmember by sizeof(uint32) from the first cnt variable, txframe. */
#define IDX_IN_WL_CNT_VER_6_T(cntmember)		\
	((OFFSETOF(wl_cnt_ver_6_t, cntmember) - OFFSETOF(wl_cnt_ver_6_t, txframe)) / sizeof(uint32))

#define IDX_IN_WL_CNT_VER_11_T(cntmember)		\
	((OFFSETOF(wl_cnt_ver_11_t, cntmember) - OFFSETOF(wl_cnt_ver_11_t, txframe))	\
	/ sizeof(uint32))

/* Exclude version and length fields */
#define NUM_OF_CNT_IN_WL_CNT_VER_6_T	\
	((sizeof(wl_cnt_ver_6_t) - 2 * sizeof(uint16)) / sizeof(uint32))
/* Exclude macstat cnt variables. wl_cnt_ver_6_t only has 62 macstat cnt variables. */
#define NUM_OF_WLCCNT_IN_WL_CNT_VER_6_T			\
	(NUM_OF_CNT_IN_WL_CNT_VER_6_T - (WL_CNT_MCST_VAR_NUM - 2))

/* Exclude version and length fields */
#define NUM_OF_CNT_IN_WL_CNT_VER_11_T	\
	((sizeof(wl_cnt_ver_11_t) - 2 * sizeof(uint16)) / sizeof(uint32))
/* Exclude 64 macstat cnt variables. */
#define NUM_OF_WLCCNT_IN_WL_CNT_VER_11_T		\
	(NUM_OF_CNT_IN_WL_CNT_VER_11_T - WL_CNT_MCST_VAR_NUM)

/* Index conversion table from wl_cnt_ver_6_t to wl_cnt_wlc_t */
static const uint8 wlcntver6t_to_wlcntwlct[NUM_OF_WLCCNT_IN_WL_CNT_VER_6_T] = {
	IDX_IN_WL_CNT_VER_6_T(txframe),
	IDX_IN_WL_CNT_VER_6_T(txbyte),
	IDX_IN_WL_CNT_VER_6_T(txretrans),
	IDX_IN_WL_CNT_VER_6_T(txerror),
	IDX_IN_WL_CNT_VER_6_T(txctl),
	IDX_IN_WL_CNT_VER_6_T(txprshort),
	IDX_IN_WL_CNT_VER_6_T(txserr),
	IDX_IN_WL_CNT_VER_6_T(txnobuf),
	IDX_IN_WL_CNT_VER_6_T(txnoassoc),
	IDX_IN_WL_CNT_VER_6_T(txrunt),
	IDX_IN_WL_CNT_VER_6_T(txchit),
	IDX_IN_WL_CNT_VER_6_T(txcmiss),
	IDX_IN_WL_CNT_VER_6_T(txuflo),
	IDX_IN_WL_CNT_VER_6_T(txphyerr),
	IDX_IN_WL_CNT_VER_6_T(txphycrs),
	IDX_IN_WL_CNT_VER_6_T(rxframe),
	IDX_IN_WL_CNT_VER_6_T(rxbyte),
	IDX_IN_WL_CNT_VER_6_T(rxerror),
	IDX_IN_WL_CNT_VER_6_T(rxctl),
	IDX_IN_WL_CNT_VER_6_T(rxnobuf),
	IDX_IN_WL_CNT_VER_6_T(rxnondata),
	IDX_IN_WL_CNT_VER_6_T(rxbadds),
	IDX_IN_WL_CNT_VER_6_T(rxbadcm),
	IDX_IN_WL_CNT_VER_6_T(rxfragerr),
	IDX_IN_WL_CNT_VER_6_T(rxrunt),
	IDX_IN_WL_CNT_VER_6_T(rxgiant),
	IDX_IN_WL_CNT_VER_6_T(rxnoscb),
	IDX_IN_WL_CNT_VER_6_T(rxbadproto),
	IDX_IN_WL_CNT_VER_6_T(rxbadsrcmac),
	IDX_IN_WL_CNT_VER_6_T(rxbadda),
	IDX_IN_WL_CNT_VER_6_T(rxfilter),
	IDX_IN_WL_CNT_VER_6_T(rxoflo),
	IDX_IN_WL_CNT_VER_6_T(rxuflo),
	IDX_IN_WL_CNT_VER_6_T(rxuflo) + 1,
	IDX_IN_WL_CNT_VER_6_T(rxuflo) + 2,
	IDX_IN_WL_CNT_VER_6_T(rxuflo) + 3,
	IDX_IN_WL_CNT_VER_6_T(rxuflo) + 4,
	IDX_IN_WL_CNT_VER_6_T(rxuflo) + 5,
	IDX_IN_WL_CNT_VER_6_T(d11cnt_txrts_off),
	IDX_IN_WL_CNT_VER_6_T(d11cnt_rxcrc_off),
	IDX_IN_WL_CNT_VER_6_T(d11cnt_txnocts_off),
	IDX_IN_WL_CNT_VER_6_T(dmade),
	IDX_IN_WL_CNT_VER_6_T(dmada),
	IDX_IN_WL_CNT_VER_6_T(dmape),
	IDX_IN_WL_CNT_VER_6_T(reset),
	IDX_IN_WL_CNT_VER_6_T(tbtt),
	IDX_IN_WL_CNT_VER_6_T(txdmawar),
	IDX_IN_WL_CNT_VER_6_T(pkt_callback_reg_fail),
	IDX_IN_WL_CNT_VER_6_T(txfrag),
	IDX_IN_WL_CNT_VER_6_T(txmulti),
	IDX_IN_WL_CNT_VER_6_T(txfail),
	IDX_IN_WL_CNT_VER_6_T(txretry),
	IDX_IN_WL_CNT_VER_6_T(txretrie),
	IDX_IN_WL_CNT_VER_6_T(rxdup),
	IDX_IN_WL_CNT_VER_6_T(txrts),
	IDX_IN_WL_CNT_VER_6_T(txnocts),
	IDX_IN_WL_CNT_VER_6_T(txnoack),
	IDX_IN_WL_CNT_VER_6_T(rxfrag),
	IDX_IN_WL_CNT_VER_6_T(rxmulti),
	IDX_IN_WL_CNT_VER_6_T(rxcrc),
	IDX_IN_WL_CNT_VER_6_T(txfrmsnt),
	IDX_IN_WL_CNT_VER_6_T(rxundec),
	IDX_IN_WL_CNT_VER_6_T(tkipmicfaill),
	IDX_IN_WL_CNT_VER_6_T(tkipcntrmsr),
	IDX_IN_WL_CNT_VER_6_T(tkipreplay),
	IDX_IN_WL_CNT_VER_6_T(ccmpfmterr),
	IDX_IN_WL_CNT_VER_6_T(ccmpreplay),
	IDX_IN_WL_CNT_VER_6_T(ccmpundec),
	IDX_IN_WL_CNT_VER_6_T(fourwayfail),
	IDX_IN_WL_CNT_VER_6_T(wepundec),
	IDX_IN_WL_CNT_VER_6_T(wepicverr),
	IDX_IN_WL_CNT_VER_6_T(decsuccess),
	IDX_IN_WL_CNT_VER_6_T(tkipicverr),
	IDX_IN_WL_CNT_VER_6_T(wepexcluded),
	IDX_IN_WL_CNT_VER_6_T(txchanrej),
	IDX_IN_WL_CNT_VER_6_T(psmwds),
	IDX_IN_WL_CNT_VER_6_T(phywatchdog),
	IDX_IN_WL_CNT_VER_6_T(prq_entries_handled),
	IDX_IN_WL_CNT_VER_6_T(prq_undirected_entries),
	IDX_IN_WL_CNT_VER_6_T(prq_bad_entries),
	IDX_IN_WL_CNT_VER_6_T(atim_suppress_count),
	IDX_IN_WL_CNT_VER_6_T(bcn_template_not_ready),
	IDX_IN_WL_CNT_VER_6_T(bcn_template_not_ready_done),
	IDX_IN_WL_CNT_VER_6_T(late_tbtt_dpc),
	IDX_IN_WL_CNT_VER_6_T(rx1mbps),
	IDX_IN_WL_CNT_VER_6_T(rx2mbps),
	IDX_IN_WL_CNT_VER_6_T(rx5mbps5),
	IDX_IN_WL_CNT_VER_6_T(rx6mbps),
	IDX_IN_WL_CNT_VER_6_T(rx9mbps),
	IDX_IN_WL_CNT_VER_6_T(rx11mbps),
	IDX_IN_WL_CNT_VER_6_T(rx12mbps),
	IDX_IN_WL_CNT_VER_6_T(rx18mbps),
	IDX_IN_WL_CNT_VER_6_T(rx24mbps),
	IDX_IN_WL_CNT_VER_6_T(rx36mbps),
	IDX_IN_WL_CNT_VER_6_T(rx48mbps),
	IDX_IN_WL_CNT_VER_6_T(rx54mbps),
	IDX_IN_WL_CNT_VER_6_T(rx108mbps),
	IDX_IN_WL_CNT_VER_6_T(rx162mbps),
	IDX_IN_WL_CNT_VER_6_T(rx216mbps),
	IDX_IN_WL_CNT_VER_6_T(rx270mbps),
	IDX_IN_WL_CNT_VER_6_T(rx324mbps),
	IDX_IN_WL_CNT_VER_6_T(rx378mbps),
	IDX_IN_WL_CNT_VER_6_T(rx432mbps),
	IDX_IN_WL_CNT_VER_6_T(rx486mbps),
	IDX_IN_WL_CNT_VER_6_T(rx540mbps),
	IDX_IN_WL_CNT_VER_6_T(rfdisable),
	IDX_IN_WL_CNT_VER_6_T(txexptime),
	IDX_IN_WL_CNT_VER_6_T(txmpdu_sgi),
	IDX_IN_WL_CNT_VER_6_T(rxmpdu_sgi),
	IDX_IN_WL_CNT_VER_6_T(txmpdu_stbc),
	IDX_IN_WL_CNT_VER_6_T(rxmpdu_stbc),
	IDX_IN_WL_CNT_VER_6_T(rxundec_mcst),
	IDX_IN_WL_CNT_VER_6_T(tkipmicfaill_mcst),
	IDX_IN_WL_CNT_VER_6_T(tkipcntrmsr_mcst),
	IDX_IN_WL_CNT_VER_6_T(tkipreplay_mcst),
	IDX_IN_WL_CNT_VER_6_T(ccmpfmterr_mcst),
	IDX_IN_WL_CNT_VER_6_T(ccmpreplay_mcst),
	IDX_IN_WL_CNT_VER_6_T(ccmpundec_mcst),
	IDX_IN_WL_CNT_VER_6_T(fourwayfail_mcst),
	IDX_IN_WL_CNT_VER_6_T(wepundec_mcst),
	IDX_IN_WL_CNT_VER_6_T(wepicverr_mcst),
	IDX_IN_WL_CNT_VER_6_T(decsuccess_mcst),
	IDX_IN_WL_CNT_VER_6_T(tkipicverr_mcst),
	IDX_IN_WL_CNT_VER_6_T(wepexcluded_mcst)
};

/* Index conversion table from wl_cnt_ver_11_t to wl_cnt_wlc_t */
static const uint8 wlcntver11t_to_wlcntwlct[NUM_OF_WLCCNT_IN_WL_CNT_VER_11_T] = {
	IDX_IN_WL_CNT_VER_11_T(txframe),
	IDX_IN_WL_CNT_VER_11_T(txbyte),
	IDX_IN_WL_CNT_VER_11_T(txretrans),
	IDX_IN_WL_CNT_VER_11_T(txerror),
	IDX_IN_WL_CNT_VER_11_T(txctl),
	IDX_IN_WL_CNT_VER_11_T(txprshort),
	IDX_IN_WL_CNT_VER_11_T(txserr),
	IDX_IN_WL_CNT_VER_11_T(txnobuf),
	IDX_IN_WL_CNT_VER_11_T(txnoassoc),
	IDX_IN_WL_CNT_VER_11_T(txrunt),
	IDX_IN_WL_CNT_VER_11_T(txchit),
	IDX_IN_WL_CNT_VER_11_T(txcmiss),
	IDX_IN_WL_CNT_VER_11_T(txuflo),
	IDX_IN_WL_CNT_VER_11_T(txphyerr),
	IDX_IN_WL_CNT_VER_11_T(txphycrs),
	IDX_IN_WL_CNT_VER_11_T(rxframe),
	IDX_IN_WL_CNT_VER_11_T(rxbyte),
	IDX_IN_WL_CNT_VER_11_T(rxerror),
	IDX_IN_WL_CNT_VER_11_T(rxctl),
	IDX_IN_WL_CNT_VER_11_T(rxnobuf),
	IDX_IN_WL_CNT_VER_11_T(rxnondata),
	IDX_IN_WL_CNT_VER_11_T(rxbadds),
	IDX_IN_WL_CNT_VER_11_T(rxbadcm),
	IDX_IN_WL_CNT_VER_11_T(rxfragerr),
	IDX_IN_WL_CNT_VER_11_T(rxrunt),
	IDX_IN_WL_CNT_VER_11_T(rxgiant),
	IDX_IN_WL_CNT_VER_11_T(rxnoscb),
	IDX_IN_WL_CNT_VER_11_T(rxbadproto),
	IDX_IN_WL_CNT_VER_11_T(rxbadsrcmac),
	IDX_IN_WL_CNT_VER_11_T(rxbadda),
	IDX_IN_WL_CNT_VER_11_T(rxfilter),
	IDX_IN_WL_CNT_VER_11_T(rxoflo),
	IDX_IN_WL_CNT_VER_11_T(rxuflo),
	IDX_IN_WL_CNT_VER_11_T(rxuflo) + 1,
	IDX_IN_WL_CNT_VER_11_T(rxuflo) + 2,
	IDX_IN_WL_CNT_VER_11_T(rxuflo) + 3,
	IDX_IN_WL_CNT_VER_11_T(rxuflo) + 4,
	IDX_IN_WL_CNT_VER_11_T(rxuflo) + 5,
	IDX_IN_WL_CNT_VER_11_T(d11cnt_txrts_off),
	IDX_IN_WL_CNT_VER_11_T(d11cnt_rxcrc_off),
	IDX_IN_WL_CNT_VER_11_T(d11cnt_txnocts_off),
	IDX_IN_WL_CNT_VER_11_T(dmade),
	IDX_IN_WL_CNT_VER_11_T(dmada),
	IDX_IN_WL_CNT_VER_11_T(dmape),
	IDX_IN_WL_CNT_VER_11_T(reset),
	IDX_IN_WL_CNT_VER_11_T(tbtt),
	IDX_IN_WL_CNT_VER_11_T(txdmawar),
	IDX_IN_WL_CNT_VER_11_T(pkt_callback_reg_fail),
	IDX_IN_WL_CNT_VER_11_T(txfrag),
	IDX_IN_WL_CNT_VER_11_T(txmulti),
	IDX_IN_WL_CNT_VER_11_T(txfail),
	IDX_IN_WL_CNT_VER_11_T(txretry),
	IDX_IN_WL_CNT_VER_11_T(txretrie),
	IDX_IN_WL_CNT_VER_11_T(rxdup),
	IDX_IN_WL_CNT_VER_11_T(txrts),
	IDX_IN_WL_CNT_VER_11_T(txnocts),
	IDX_IN_WL_CNT_VER_11_T(txnoack),
	IDX_IN_WL_CNT_VER_11_T(rxfrag),
	IDX_IN_WL_CNT_VER_11_T(rxmulti),
	IDX_IN_WL_CNT_VER_11_T(rxcrc),
	IDX_IN_WL_CNT_VER_11_T(txfrmsnt),
	IDX_IN_WL_CNT_VER_11_T(rxundec),
	IDX_IN_WL_CNT_VER_11_T(tkipmicfaill),
	IDX_IN_WL_CNT_VER_11_T(tkipcntrmsr),
	IDX_IN_WL_CNT_VER_11_T(tkipreplay),
	IDX_IN_WL_CNT_VER_11_T(ccmpfmterr),
	IDX_IN_WL_CNT_VER_11_T(ccmpreplay),
	IDX_IN_WL_CNT_VER_11_T(ccmpundec),
	IDX_IN_WL_CNT_VER_11_T(fourwayfail),
	IDX_IN_WL_CNT_VER_11_T(wepundec),
	IDX_IN_WL_CNT_VER_11_T(wepicverr),
	IDX_IN_WL_CNT_VER_11_T(decsuccess),
	IDX_IN_WL_CNT_VER_11_T(tkipicverr),
	IDX_IN_WL_CNT_VER_11_T(wepexcluded),
	IDX_IN_WL_CNT_VER_11_T(txchanrej),
	IDX_IN_WL_CNT_VER_11_T(psmwds),
	IDX_IN_WL_CNT_VER_11_T(phywatchdog),
	IDX_IN_WL_CNT_VER_11_T(prq_entries_handled),
	IDX_IN_WL_CNT_VER_11_T(prq_undirected_entries),
	IDX_IN_WL_CNT_VER_11_T(prq_bad_entries),
	IDX_IN_WL_CNT_VER_11_T(atim_suppress_count),
	IDX_IN_WL_CNT_VER_11_T(bcn_template_not_ready),
	IDX_IN_WL_CNT_VER_11_T(bcn_template_not_ready_done),
	IDX_IN_WL_CNT_VER_11_T(late_tbtt_dpc),
	IDX_IN_WL_CNT_VER_11_T(rx1mbps),
	IDX_IN_WL_CNT_VER_11_T(rx2mbps),
	IDX_IN_WL_CNT_VER_11_T(rx5mbps5),
	IDX_IN_WL_CNT_VER_11_T(rx6mbps),
	IDX_IN_WL_CNT_VER_11_T(rx9mbps),
	IDX_IN_WL_CNT_VER_11_T(rx11mbps),
	IDX_IN_WL_CNT_VER_11_T(rx12mbps),
	IDX_IN_WL_CNT_VER_11_T(rx18mbps),
	IDX_IN_WL_CNT_VER_11_T(rx24mbps),
	IDX_IN_WL_CNT_VER_11_T(rx36mbps),
	IDX_IN_WL_CNT_VER_11_T(rx48mbps),
	IDX_IN_WL_CNT_VER_11_T(rx54mbps),
	IDX_IN_WL_CNT_VER_11_T(rx108mbps),
	IDX_IN_WL_CNT_VER_11_T(rx162mbps),
	IDX_IN_WL_CNT_VER_11_T(rx216mbps),
	IDX_IN_WL_CNT_VER_11_T(rx270mbps),
	IDX_IN_WL_CNT_VER_11_T(rx324mbps),
	IDX_IN_WL_CNT_VER_11_T(rx378mbps),
	IDX_IN_WL_CNT_VER_11_T(rx432mbps),
	IDX_IN_WL_CNT_VER_11_T(rx486mbps),
	IDX_IN_WL_CNT_VER_11_T(rx540mbps),
	IDX_IN_WL_CNT_VER_11_T(rfdisable),
	IDX_IN_WL_CNT_VER_11_T(txexptime),
	IDX_IN_WL_CNT_VER_11_T(txmpdu_sgi),
	IDX_IN_WL_CNT_VER_11_T(rxmpdu_sgi),
	IDX_IN_WL_CNT_VER_11_T(txmpdu_stbc),
	IDX_IN_WL_CNT_VER_11_T(rxmpdu_stbc),
	IDX_IN_WL_CNT_VER_11_T(rxundec_mcst),
	IDX_IN_WL_CNT_VER_11_T(tkipmicfaill_mcst),
	IDX_IN_WL_CNT_VER_11_T(tkipcntrmsr_mcst),
	IDX_IN_WL_CNT_VER_11_T(tkipreplay_mcst),
	IDX_IN_WL_CNT_VER_11_T(ccmpfmterr_mcst),
	IDX_IN_WL_CNT_VER_11_T(ccmpreplay_mcst),
	IDX_IN_WL_CNT_VER_11_T(ccmpundec_mcst),
	IDX_IN_WL_CNT_VER_11_T(fourwayfail_mcst),
	IDX_IN_WL_CNT_VER_11_T(wepundec_mcst),
	IDX_IN_WL_CNT_VER_11_T(wepicverr_mcst),
	IDX_IN_WL_CNT_VER_11_T(decsuccess_mcst),
	IDX_IN_WL_CNT_VER_11_T(tkipicverr_mcst),
	IDX_IN_WL_CNT_VER_11_T(wepexcluded_mcst),
	IDX_IN_WL_CNT_VER_11_T(dma_hang),
	IDX_IN_WL_CNT_VER_11_T(reinit),
	IDX_IN_WL_CNT_VER_11_T(pstatxucast),
	IDX_IN_WL_CNT_VER_11_T(pstatxnoassoc),
	IDX_IN_WL_CNT_VER_11_T(pstarxucast),
	IDX_IN_WL_CNT_VER_11_T(pstarxbcmc),
	IDX_IN_WL_CNT_VER_11_T(pstatxbcmc),
	IDX_IN_WL_CNT_VER_11_T(cso_passthrough),
	IDX_IN_WL_CNT_VER_11_T(cso_normal),
	IDX_IN_WL_CNT_VER_11_T(chained),
	IDX_IN_WL_CNT_VER_11_T(chainedsz1),
	IDX_IN_WL_CNT_VER_11_T(unchained),
	IDX_IN_WL_CNT_VER_11_T(maxchainsz),
	IDX_IN_WL_CNT_VER_11_T(currchainsz),
	IDX_IN_WL_CNT_VER_11_T(pciereset),
	IDX_IN_WL_CNT_VER_11_T(cfgrestore),
	IDX_IN_WL_CNT_VER_11_T(reinitreason),
	IDX_IN_WL_CNT_VER_11_T(reinitreason) + 1,
	IDX_IN_WL_CNT_VER_11_T(reinitreason) + 2,
	IDX_IN_WL_CNT_VER_11_T(reinitreason) + 3,
	IDX_IN_WL_CNT_VER_11_T(reinitreason) + 4,
	IDX_IN_WL_CNT_VER_11_T(reinitreason) + 5,
	IDX_IN_WL_CNT_VER_11_T(reinitreason) + 6,
	IDX_IN_WL_CNT_VER_11_T(reinitreason) + 7,
	IDX_IN_WL_CNT_VER_11_T(rxrtry),
	IDX_IN_WL_CNT_VER_11_T(rxmpdu_mu),
	IDX_IN_WL_CNT_VER_11_T(txbar),
	IDX_IN_WL_CNT_VER_11_T(rxbar),
	IDX_IN_WL_CNT_VER_11_T(txpspoll),
	IDX_IN_WL_CNT_VER_11_T(rxpspoll),
	IDX_IN_WL_CNT_VER_11_T(txnull),
	IDX_IN_WL_CNT_VER_11_T(rxnull),
	IDX_IN_WL_CNT_VER_11_T(txqosnull),
	IDX_IN_WL_CNT_VER_11_T(rxqosnull),
	IDX_IN_WL_CNT_VER_11_T(txassocreq),
	IDX_IN_WL_CNT_VER_11_T(rxassocreq),
	IDX_IN_WL_CNT_VER_11_T(txreassocreq),
	IDX_IN_WL_CNT_VER_11_T(rxreassocreq),
	IDX_IN_WL_CNT_VER_11_T(txdisassoc),
	IDX_IN_WL_CNT_VER_11_T(rxdisassoc),
	IDX_IN_WL_CNT_VER_11_T(txassocrsp),
	IDX_IN_WL_CNT_VER_11_T(rxassocrsp),
	IDX_IN_WL_CNT_VER_11_T(txreassocrsp),
	IDX_IN_WL_CNT_VER_11_T(rxreassocrsp),
	IDX_IN_WL_CNT_VER_11_T(txauth),
	IDX_IN_WL_CNT_VER_11_T(rxauth),
	IDX_IN_WL_CNT_VER_11_T(txdeauth),
	IDX_IN_WL_CNT_VER_11_T(rxdeauth),
	IDX_IN_WL_CNT_VER_11_T(txprobereq),
	IDX_IN_WL_CNT_VER_11_T(rxprobereq),
	IDX_IN_WL_CNT_VER_11_T(txprobersp),
	IDX_IN_WL_CNT_VER_11_T(rxprobersp),
	IDX_IN_WL_CNT_VER_11_T(txaction),
	IDX_IN_WL_CNT_VER_11_T(rxaction),
	IDX_IN_WL_CNT_VER_11_T(ampdu_wds),
	IDX_IN_WL_CNT_VER_11_T(txlost),
	IDX_IN_WL_CNT_VER_11_T(txdatamcast),
	IDX_IN_WL_CNT_VER_11_T(txdatabcast)
};

/* Index conversion table from wl_cnt_ver_11_t to
 * either wl_cnt_ge40mcst_v1_t or wl_cnt_lt40mcst_v1_t
 */
static const uint8 wlcntver11t_to_wlcntXX40mcstv1t[WL_CNT_MCST_VAR_NUM] = {
	IDX_IN_WL_CNT_VER_11_T(txallfrm),
	IDX_IN_WL_CNT_VER_11_T(txrtsfrm),
	IDX_IN_WL_CNT_VER_11_T(txctsfrm),
	IDX_IN_WL_CNT_VER_11_T(txackfrm),
	IDX_IN_WL_CNT_VER_11_T(txdnlfrm),
	IDX_IN_WL_CNT_VER_11_T(txbcnfrm),
	IDX_IN_WL_CNT_VER_11_T(txfunfl),
	IDX_IN_WL_CNT_VER_11_T(txfunfl) + 1,
	IDX_IN_WL_CNT_VER_11_T(txfunfl) + 2,
	IDX_IN_WL_CNT_VER_11_T(txfunfl) + 3,
	IDX_IN_WL_CNT_VER_11_T(txfunfl) + 4,
	IDX_IN_WL_CNT_VER_11_T(txfunfl) + 5,
	IDX_IN_WL_CNT_VER_11_T(txfbw),
	IDX_IN_WL_CNT_VER_11_T(txmpdu),
	IDX_IN_WL_CNT_VER_11_T(txtplunfl),
	IDX_IN_WL_CNT_VER_11_T(txphyerror),
	IDX_IN_WL_CNT_VER_11_T(pktengrxducast),
	IDX_IN_WL_CNT_VER_11_T(pktengrxdmcast),
	IDX_IN_WL_CNT_VER_11_T(rxfrmtoolong),
	IDX_IN_WL_CNT_VER_11_T(rxfrmtooshrt),
	IDX_IN_WL_CNT_VER_11_T(rxinvmachdr),
	IDX_IN_WL_CNT_VER_11_T(rxbadfcs),
	IDX_IN_WL_CNT_VER_11_T(rxbadplcp),
	IDX_IN_WL_CNT_VER_11_T(rxcrsglitch),
	IDX_IN_WL_CNT_VER_11_T(rxstrt),
	IDX_IN_WL_CNT_VER_11_T(rxdfrmucastmbss),
	IDX_IN_WL_CNT_VER_11_T(rxmfrmucastmbss),
	IDX_IN_WL_CNT_VER_11_T(rxcfrmucast),
	IDX_IN_WL_CNT_VER_11_T(rxrtsucast),
	IDX_IN_WL_CNT_VER_11_T(rxctsucast),
	IDX_IN_WL_CNT_VER_11_T(rxackucast),
	IDX_IN_WL_CNT_VER_11_T(rxdfrmocast),
	IDX_IN_WL_CNT_VER_11_T(rxmfrmocast),
	IDX_IN_WL_CNT_VER_11_T(rxcfrmocast),
	IDX_IN_WL_CNT_VER_11_T(rxrtsocast),
	IDX_IN_WL_CNT_VER_11_T(rxctsocast),
	IDX_IN_WL_CNT_VER_11_T(rxdfrmmcast),
	IDX_IN_WL_CNT_VER_11_T(rxmfrmmcast),
	IDX_IN_WL_CNT_VER_11_T(rxcfrmmcast),
	IDX_IN_WL_CNT_VER_11_T(rxbeaconmbss),
	IDX_IN_WL_CNT_VER_11_T(rxdfrmucastobss),
	IDX_IN_WL_CNT_VER_11_T(rxbeaconobss),
	IDX_IN_WL_CNT_VER_11_T(rxrsptmout),
	IDX_IN_WL_CNT_VER_11_T(bcntxcancl),
	IDX_IN_WL_CNT_VER_11_T(rxnodelim),
	IDX_IN_WL_CNT_VER_11_T(rxf0ovfl),
	IDX_IN_WL_CNT_VER_11_T(rxf1ovfl),
	IDX_IN_WL_CNT_VER_11_T(rxf2ovfl),
	IDX_IN_WL_CNT_VER_11_T(txsfovfl),
	IDX_IN_WL_CNT_VER_11_T(pmqovfl),
	IDX_IN_WL_CNT_VER_11_T(rxcgprqfrm),
	IDX_IN_WL_CNT_VER_11_T(rxcgprsqovfl),
	IDX_IN_WL_CNT_VER_11_T(txcgprsfail),
	IDX_IN_WL_CNT_VER_11_T(txcgprssuc),
	IDX_IN_WL_CNT_VER_11_T(prs_timeout),
	IDX_IN_WL_CNT_VER_11_T(rxnack),
	IDX_IN_WL_CNT_VER_11_T(frmscons),
	IDX_IN_WL_CNT_VER_11_T(txnack),
	IDX_IN_WL_CNT_VER_11_T(rxback),
	IDX_IN_WL_CNT_VER_11_T(txback),
	IDX_IN_WL_CNT_VER_11_T(bphy_rxcrsglitch),
	IDX_IN_WL_CNT_VER_11_T(rxdrop20s),
	IDX_IN_WL_CNT_VER_11_T(rxtoolate),
	IDX_IN_WL_CNT_VER_11_T(bphy_badplcp)
};

/* For mcst offsets that were not used. (2 Pads) */
#define INVALID_MCST_IDX ((uint8)(-1))
/* Index conversion table from wl_cnt_ver_11_t to wl_cnt_v_le10_mcst_t */
static const uint8 wlcntver11t_to_wlcntvle10mcstt[WL_CNT_MCST_VAR_NUM] = {
	IDX_IN_WL_CNT_VER_11_T(txallfrm),
	IDX_IN_WL_CNT_VER_11_T(txrtsfrm),
	IDX_IN_WL_CNT_VER_11_T(txctsfrm),
	IDX_IN_WL_CNT_VER_11_T(txackfrm),
	IDX_IN_WL_CNT_VER_11_T(txdnlfrm),
	IDX_IN_WL_CNT_VER_11_T(txbcnfrm),
	IDX_IN_WL_CNT_VER_11_T(txfunfl),
	IDX_IN_WL_CNT_VER_11_T(txfunfl) + 1,
	IDX_IN_WL_CNT_VER_11_T(txfunfl) + 2,
	IDX_IN_WL_CNT_VER_11_T(txfunfl) + 3,
	IDX_IN_WL_CNT_VER_11_T(txfunfl) + 4,
	IDX_IN_WL_CNT_VER_11_T(txfunfl) + 5,
	IDX_IN_WL_CNT_VER_11_T(txfbw),
	INVALID_MCST_IDX,
	IDX_IN_WL_CNT_VER_11_T(txtplunfl),
	IDX_IN_WL_CNT_VER_11_T(txphyerror),
	IDX_IN_WL_CNT_VER_11_T(pktengrxducast),
	IDX_IN_WL_CNT_VER_11_T(pktengrxdmcast),
	IDX_IN_WL_CNT_VER_11_T(rxfrmtoolong),
	IDX_IN_WL_CNT_VER_11_T(rxfrmtooshrt),
	IDX_IN_WL_CNT_VER_11_T(rxinvmachdr),
	IDX_IN_WL_CNT_VER_11_T(rxbadfcs),
	IDX_IN_WL_CNT_VER_11_T(rxbadplcp),
	IDX_IN_WL_CNT_VER_11_T(rxcrsglitch),
	IDX_IN_WL_CNT_VER_11_T(rxstrt),
	IDX_IN_WL_CNT_VER_11_T(rxdfrmucastmbss),
	IDX_IN_WL_CNT_VER_11_T(rxmfrmucastmbss),
	IDX_IN_WL_CNT_VER_11_T(rxcfrmucast),
	IDX_IN_WL_CNT_VER_11_T(rxrtsucast),
	IDX_IN_WL_CNT_VER_11_T(rxctsucast),
	IDX_IN_WL_CNT_VER_11_T(rxackucast),
	IDX_IN_WL_CNT_VER_11_T(rxdfrmocast),
	IDX_IN_WL_CNT_VER_11_T(rxmfrmocast),
	IDX_IN_WL_CNT_VER_11_T(rxcfrmocast),
	IDX_IN_WL_CNT_VER_11_T(rxrtsocast),
	IDX_IN_WL_CNT_VER_11_T(rxctsocast),
	IDX_IN_WL_CNT_VER_11_T(rxdfrmmcast),
	IDX_IN_WL_CNT_VER_11_T(rxmfrmmcast),
	IDX_IN_WL_CNT_VER_11_T(rxcfrmmcast),
	IDX_IN_WL_CNT_VER_11_T(rxbeaconmbss),
	IDX_IN_WL_CNT_VER_11_T(rxdfrmucastobss),
	IDX_IN_WL_CNT_VER_11_T(rxbeaconobss),
	IDX_IN_WL_CNT_VER_11_T(rxrsptmout),
	IDX_IN_WL_CNT_VER_11_T(bcntxcancl),
	INVALID_MCST_IDX,
	IDX_IN_WL_CNT_VER_11_T(rxf0ovfl),
	IDX_IN_WL_CNT_VER_11_T(rxf1ovfl),
	IDX_IN_WL_CNT_VER_11_T(rxf2ovfl),
	IDX_IN_WL_CNT_VER_11_T(txsfovfl),
	IDX_IN_WL_CNT_VER_11_T(pmqovfl),
	IDX_IN_WL_CNT_VER_11_T(rxcgprqfrm),
	IDX_IN_WL_CNT_VER_11_T(rxcgprsqovfl),
	IDX_IN_WL_CNT_VER_11_T(txcgprsfail),
	IDX_IN_WL_CNT_VER_11_T(txcgprssuc),
	IDX_IN_WL_CNT_VER_11_T(prs_timeout),
	IDX_IN_WL_CNT_VER_11_T(rxnack),
	IDX_IN_WL_CNT_VER_11_T(frmscons),
	IDX_IN_WL_CNT_VER_11_T(txnack),
	IDX_IN_WL_CNT_VER_11_T(rxback),
	IDX_IN_WL_CNT_VER_11_T(txback),
	IDX_IN_WL_CNT_VER_11_T(bphy_rxcrsglitch),
	IDX_IN_WL_CNT_VER_11_T(rxdrop20s),
	IDX_IN_WL_CNT_VER_11_T(rxtoolate),
	IDX_IN_WL_CNT_VER_11_T(bphy_badplcp)
};


/* Index conversion table from wl_cnt_ver_6_t to wl_cnt_v_le10_mcst_t */
static const uint8 wlcntver6t_to_wlcntvle10mcstt[WL_CNT_MCST_VAR_NUM] = {
	IDX_IN_WL_CNT_VER_6_T(txallfrm),
	IDX_IN_WL_CNT_VER_6_T(txrtsfrm),
	IDX_IN_WL_CNT_VER_6_T(txctsfrm),
	IDX_IN_WL_CNT_VER_6_T(txackfrm),
	IDX_IN_WL_CNT_VER_6_T(txdnlfrm),
	IDX_IN_WL_CNT_VER_6_T(txbcnfrm),
	IDX_IN_WL_CNT_VER_6_T(txfunfl),
	IDX_IN_WL_CNT_VER_6_T(txfunfl) + 1,
	IDX_IN_WL_CNT_VER_6_T(txfunfl) + 2,
	IDX_IN_WL_CNT_VER_6_T(txfunfl) + 3,
	IDX_IN_WL_CNT_VER_6_T(txfunfl) + 4,
	IDX_IN_WL_CNT_VER_6_T(txfunfl) + 5,
	IDX_IN_WL_CNT_VER_6_T(txfbw),
	INVALID_MCST_IDX,
	IDX_IN_WL_CNT_VER_6_T(txtplunfl),
	IDX_IN_WL_CNT_VER_6_T(txphyerror),
	IDX_IN_WL_CNT_VER_6_T(pktengrxducast),
	IDX_IN_WL_CNT_VER_6_T(pktengrxdmcast),
	IDX_IN_WL_CNT_VER_6_T(rxfrmtoolong),
	IDX_IN_WL_CNT_VER_6_T(rxfrmtooshrt),
	IDX_IN_WL_CNT_VER_6_T(rxinvmachdr),
	IDX_IN_WL_CNT_VER_6_T(rxbadfcs),
	IDX_IN_WL_CNT_VER_6_T(rxbadplcp),
	IDX_IN_WL_CNT_VER_6_T(rxcrsglitch),
	IDX_IN_WL_CNT_VER_6_T(rxstrt),
	IDX_IN_WL_CNT_VER_6_T(rxdfrmucastmbss),
	IDX_IN_WL_CNT_VER_6_T(rxmfrmucastmbss),
	IDX_IN_WL_CNT_VER_6_T(rxcfrmucast),
	IDX_IN_WL_CNT_VER_6_T(rxrtsucast),
	IDX_IN_WL_CNT_VER_6_T(rxctsucast),
	IDX_IN_WL_CNT_VER_6_T(rxackucast),
	IDX_IN_WL_CNT_VER_6_T(rxdfrmocast),
	IDX_IN_WL_CNT_VER_6_T(rxmfrmocast),
	IDX_IN_WL_CNT_VER_6_T(rxcfrmocast),
	IDX_IN_WL_CNT_VER_6_T(rxrtsocast),
	IDX_IN_WL_CNT_VER_6_T(rxctsocast),
	IDX_IN_WL_CNT_VER_6_T(rxdfrmmcast),
	IDX_IN_WL_CNT_VER_6_T(rxmfrmmcast),
	IDX_IN_WL_CNT_VER_6_T(rxcfrmmcast),
	IDX_IN_WL_CNT_VER_6_T(rxbeaconmbss),
	IDX_IN_WL_CNT_VER_6_T(rxdfrmucastobss),
	IDX_IN_WL_CNT_VER_6_T(rxbeaconobss),
	IDX_IN_WL_CNT_VER_6_T(rxrsptmout),
	IDX_IN_WL_CNT_VER_6_T(bcntxcancl),
	INVALID_MCST_IDX,
	IDX_IN_WL_CNT_VER_6_T(rxf0ovfl),
	IDX_IN_WL_CNT_VER_6_T(rxf1ovfl),
	IDX_IN_WL_CNT_VER_6_T(rxf2ovfl),
	IDX_IN_WL_CNT_VER_6_T(txsfovfl),
	IDX_IN_WL_CNT_VER_6_T(pmqovfl),
	IDX_IN_WL_CNT_VER_6_T(rxcgprqfrm),
	IDX_IN_WL_CNT_VER_6_T(rxcgprsqovfl),
	IDX_IN_WL_CNT_VER_6_T(txcgprsfail),
	IDX_IN_WL_CNT_VER_6_T(txcgprssuc),
	IDX_IN_WL_CNT_VER_6_T(prs_timeout),
	IDX_IN_WL_CNT_VER_6_T(rxnack),
	IDX_IN_WL_CNT_VER_6_T(frmscons),
	IDX_IN_WL_CNT_VER_6_T(txnack),
	IDX_IN_WL_CNT_VER_6_T(rxback),
	IDX_IN_WL_CNT_VER_6_T(txback),
	IDX_IN_WL_CNT_VER_6_T(bphy_rxcrsglitch),
	IDX_IN_WL_CNT_VER_6_T(rxdrop20s),
	IDX_IN_WL_CNT_VER_6_T(rxtoolate),
	IDX_IN_WL_CNT_VER_6_T(bphy_badplcp)
};

/* copy wlc layer counters from old type cntbuf to wl_cnt_wlc_t type. */
static int
wl_copy_wlccnt(uint16 cntver, uint32 *dst, uint32 *src, uint8 src_max_idx)
{
	uint i;
	if (dst == NULL || src == NULL) {
		return BCME_ERROR;
	}

	/* Init wlccnt with invalid value. Unchanged value will not be printed out */
	for (i = 0; i < (sizeof(wl_cnt_wlc_t) / sizeof(uint32)); i++) {
		dst[i] = INVALID_CNT_VAL;
	}

	if (cntver == WL_CNT_VERSION_6) {
		for (i = 0; i < NUM_OF_WLCCNT_IN_WL_CNT_VER_6_T; i++) {
			if (wlcntver6t_to_wlcntwlct[i] >= src_max_idx) {
				/* src buffer does not have counters from here */
				break;
			}
			dst[i] = src[wlcntver6t_to_wlcntwlct[i]];
		}
	} else {
		for (i = 0; i < NUM_OF_WLCCNT_IN_WL_CNT_VER_11_T; i++) {
			if (wlcntver11t_to_wlcntwlct[i] >= src_max_idx) {
				/* src buffer does not have counters from here */
				break;
			}
			dst[i] = src[wlcntver11t_to_wlcntwlct[i]];
		}
	}
	return BCME_OK;
}

/* copy macstat counters from old type cntbuf to wl_cnt_v_le10_mcst_t type. */
static int
wl_copy_macstat_upto_ver10(uint16 cntver, uint32 *dst, uint32 *src)
{
	uint i;

	if (dst == NULL || src == NULL) {
		return BCME_ERROR;
	}

	if (cntver == WL_CNT_VERSION_6) {
		for (i = 0; i < WL_CNT_MCST_VAR_NUM; i++) {
			if (wlcntver6t_to_wlcntvle10mcstt[i] == INVALID_MCST_IDX) {
				/* This mcst counter does not exist in wl_cnt_ver_6_t */
				dst[i] = INVALID_CNT_VAL;
			} else {
				dst[i] = src[wlcntver6t_to_wlcntvle10mcstt[i]];
			}
		}
	} else {
		for (i = 0; i < WL_CNT_MCST_VAR_NUM; i++) {
			if (wlcntver11t_to_wlcntvle10mcstt[i] == INVALID_MCST_IDX) {
				/* This mcst counter does not exist in wl_cnt_ver_11_t */
				dst[i] = INVALID_CNT_VAL;
			} else {
				dst[i] = src[wlcntver11t_to_wlcntvle10mcstt[i]];
			}
		}
	}
	return BCME_OK;
}

static int
wl_copy_macstat_ver11(uint32 *dst, uint32 *src)
{
	uint i;

	if (dst == NULL || src == NULL) {
		return BCME_ERROR;
	}

	for (i = 0; i < WL_CNT_MCST_VAR_NUM; i++) {
		dst[i] = src[wlcntver11t_to_wlcntXX40mcstv1t[i]];
	}
	return BCME_OK;
}

/**
 * Translate non-xtlv 'wl counters' IOVar buffer received by old driver/FW to xtlv format.
 * Parameters:
 *	cntbuf: pointer to non-xtlv 'wl counters' IOVar buffer received by old driver/FW.
 *		Newly translated xtlv format is written to this pointer.
 *	buflen: length of the "cntbuf" without any padding.
 *	corerev: chip core revision of the driver/FW.
 */
int
wl_cntbuf_to_xtlv_format(void *ctx, void *cntbuf, int buflen, uint32 corerev)
{
	wl_cnt_wlc_t *wlccnt = NULL;
	uint32 *macstat = NULL;
	xtlv_desc_t xtlv_desc[3];
	uint16 mcst_xtlv_id;
	int res = BCME_OK;
	wl_cnt_info_t *cntinfo = cntbuf;
	void *xtlvbuf_p = cntinfo->data;
	uint16 ver = cntinfo->version;
	uint16 xtlvbuflen = (uint16)buflen;
	uint16 src_max_idx;
#ifdef BCMDRIVER
	osl_t *osh = ctx;
#else
	BCM_REFERENCE(ctx);
#endif

	if (ver >= WL_CNT_VERSION_XTLV) {
		/* Already in xtlv format. */
		goto exit;
	}

#ifdef BCMDRIVER
	wlccnt = MALLOC(osh, sizeof(*wlccnt));
	macstat = MALLOC(osh, WL_CNT_MCST_STRUCT_SZ);
#else
	wlccnt = (wl_cnt_wlc_t *)malloc(sizeof(*wlccnt));
	macstat = (uint32 *)malloc(WL_CNT_MCST_STRUCT_SZ);
#endif
	if (!wlccnt || !macstat) {
		printf("%s: malloc fail!\n", __FUNCTION__);
		res = BCME_NOMEM;
		goto exit;
	}

	/* Check if the max idx in the struct exceeds the boundary of uint8 */
	if (NUM_OF_CNT_IN_WL_CNT_VER_6_T > ((uint8)(-1) + 1) ||
		NUM_OF_CNT_IN_WL_CNT_VER_11_T > ((uint8)(-1) + 1)) {
		printf("wlcntverXXt_to_wlcntwlct and src_max_idx need"
			" to be of uint16 instead of uint8\n");
		res = BCME_ERROR;
		goto exit;
	}

	/* Exclude version and length fields in either wlc_cnt_ver_6_t or wlc_cnt_ver_11_t */
	src_max_idx = (cntinfo->datalen - OFFSETOF(wl_cnt_info_t, data)) / sizeof(uint32);
	if (src_max_idx > (uint8)(-1)) {
		printf("wlcntverXXt_to_wlcntwlct and src_max_idx need"
			" to be of uint16 instead of uint8\n"
			"Try updating wl utility to the latest.\n");
		src_max_idx = (uint8)(-1);
	}

	/* Copy wlc layer counters to wl_cnt_wlc_t */
	res = wl_copy_wlccnt(ver, (uint32 *)wlccnt, (uint32 *)cntinfo->data, (uint8)src_max_idx);
	if (res != BCME_OK) {
		printf("wl_copy_wlccnt fail!\n");
		goto exit;
	}

	/* Copy macstat counters to wl_cnt_wlc_t */
	if (ver == WL_CNT_VERSION_11) {
		res = wl_copy_macstat_ver11(macstat, (uint32 *)cntinfo->data);
		if (res != BCME_OK) {
			printf("wl_copy_macstat_ver11 fail!\n");
			goto exit;
		}
		if (corerev >= 40) {
			mcst_xtlv_id = WL_CNT_XTLV_GE40_UCODE_V1;
		} else {
			mcst_xtlv_id = WL_CNT_XTLV_LT40_UCODE_V1;
		}
	} else {
		res = wl_copy_macstat_upto_ver10(ver, macstat, (uint32 *)cntinfo->data);
		if (res != BCME_OK) {
			printf("wl_copy_macstat_upto_ver10 fail!\n");
			goto exit;
		}
		mcst_xtlv_id = WL_CNT_XTLV_CNTV_LE10_UCODE;
	}

	xtlv_desc[0].type = WL_CNT_XTLV_WLC;
	xtlv_desc[0].len = sizeof(*wlccnt);
	xtlv_desc[0].ptr = wlccnt;

	xtlv_desc[1].type = mcst_xtlv_id;
	xtlv_desc[1].len = WL_CNT_MCST_STRUCT_SZ;
	xtlv_desc[1].ptr = macstat;

	xtlv_desc[2].type = 0;
	xtlv_desc[2].len = 0;
	xtlv_desc[2].ptr = NULL;

	memset(cntbuf, 0, buflen);

	res = bcm_pack_xtlv_buf_from_mem(&xtlvbuf_p, &xtlvbuflen,
		xtlv_desc, BCM_XTLV_OPTION_ALIGN32);
	cntinfo->datalen = (buflen - xtlvbuflen);
exit:
#ifdef BCMDRIVER
	if (wlccnt) {
		MFREE(osh, wlccnt, sizeof(*wlccnt));
	}
	if (macstat) {
		MFREE(osh, macstat, WL_CNT_MCST_STRUCT_SZ);
	}
#else
	if (wlccnt) {
		free(wlccnt);
	}
	if (macstat) {
		free(macstat);
	}
#endif
	return res;
}
