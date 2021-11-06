/*
 * Misc utility routines for WL and Apps
 * This header file housing the define and function prototype use by
 * both the wl driver, tools & Apps.
 *
 * Copyright (C) 2020, Broadcom.
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
 *
 * <<Broadcom-WL-IPTag/Dual:>>
 */

#ifndef	_bcmwifi_channels_h_
#define	_bcmwifi_channels_h_

/* A chanspec holds the channel number, band, bandwidth and primary 20MHz sub-band */
typedef uint16 chanspec_t;
typedef uint16 chanspec_band_t;
typedef uint16 chanspec_bw_t;
typedef uint16 chanspec_subband_t;

/* channel defines */
#define CH_80MHZ_APART                   16u
#define CH_40MHZ_APART                    8u
#define CH_20MHZ_APART                    4u
#define CH_10MHZ_APART                    2u
#define CH_5MHZ_APART                     1u    /* 2G band channels are 5 Mhz apart */
#define CH_160MHZ_APART                  (32u * CH_5MHZ_APART)	/* 32 5Mhz-spaces */

#define CH_MIN_2G_CHANNEL                 1u    /* Min channel in 2G band */
#define CH_MAX_2G_CHANNEL                14u    /* Max channel in 2G band */
#define CH_MIN_2G_40M_CHANNEL             3u    /* Min 40MHz center channel in 2G band */
#define CH_MAX_2G_40M_CHANNEL            11u    /* Max 40MHz center channel in 2G band */

#define CH_MIN_6G_CHANNEL                 1u    /* Min 20MHz channel in 6G band */
#define CH_MAX_6G_CHANNEL               253u    /* Max 20MHz channel in 6G band */
#define CH_MIN_6G_40M_CHANNEL             3u    /* Min 40MHz center channel in 6G band */
#define CH_MAX_6G_40M_CHANNEL           227u    /* Max 40MHz center channel in 6G band */
#define CH_MIN_6G_80M_CHANNEL             7u    /* Min 80MHz center channel in 6G band */
#define CH_MAX_6G_80M_CHANNEL           215u    /* Max 80MHz center channel in 6G band */
#define CH_MIN_6G_160M_CHANNEL           15u    /* Min 160MHz center channel in 6G band */
#define CH_MAX_6G_160M_CHANNEL          207u    /* Max 160MHz center channel in 6G band */
#define CH_MIN_6G_240M_CHANNEL		 23u	/* Min 240MHz center channel in 6G band */
#define CH_MAX_6G_240M_CHANNEL		167u	/* Max 240MHz center channel in 6G band */
#define CH_MIN_6G_320M_CHANNEL           31u    /* Min 320MHz center channel in 6G band */
#define CH_MAX_6G_320M_CHANNEL          199u    /* Max 320MHz center channel in 6G band */

/* maximum # channels the s/w supports */
#define MAXCHANNEL                      254u    /* max # supported channels.
						 * DO NOT MAKE > 255: channels are uint8's all over
						 */
#define MAXCHANNEL_NUM	(MAXCHANNEL - 1)	/* max channel number */

#define INVCHANNEL                      255u    /* error value for a bad channel */

/* length of channel vector bitmap is the MAXCHANNEL we want to handle rounded up to a byte */
/* The actual CHANVEC_LEN fix is leading to high static memory impact
* in all projects wherein the previous CHANVEC_LEN definition is used.
*
* Retaining the previous definition under MAXCHNL_ROM_COMPAT flag.
* All those chip porgrams where memory impact is observed need to define the same.
*/
#ifdef MAXCHNL_ROM_COMPAT
#define CHANVEC_LEN (MAXCHANNEL + (8 - 1) / 8)
#else
#define CHANVEC_LEN ((MAXCHANNEL + (8 - 1)) / 8)
#endif

/* channel bitvec */
typedef struct {
	uint8   vec[CHANVEC_LEN];   /* bitvec of channels */
} chanvec_t;

/* make sure channel num is within valid range */
#define CH_NUM_VALID_RANGE(ch_num) ((ch_num) > 0 && (ch_num) <= MAXCHANNEL_NUM)

#define CHSPEC_CTLOVLP(sp1, sp2, sep)	\
	((uint)ABS(wf_chspec_ctlchan(sp1) - wf_chspec_ctlchan(sp2)) < (uint)(sep))

/* All builds use the new 11ac ratespec/chanspec */
#undef  D11AC_IOTYPES
#define D11AC_IOTYPES

/* For contiguous channel bandwidth other than 240MHz/320Mhz */
#define WL_CHANSPEC_CHAN_MASK		0x00ffu
#define WL_CHANSPEC_CHAN_SHIFT		0u

/* For contiguous channel bandwidth >= 240MHz */
#define WL_CHANSPEC_GE240_CHAN_MASK	0x0003u
#define WL_CHANSPEC_GE240_CHAN_SHIFT	0u

/* For discontiguous channel bandwidth */
#define WL_CHANSPEC_CHAN0_MASK		0x000fu
#define WL_CHANSPEC_CHAN0_SHIFT		0u
#define WL_CHANSPEC_CHAN1_MASK		0x00f0u
#define WL_CHANSPEC_CHAN1_SHIFT		4u

/* Non-320/Non-240 Mhz channel sideband indication */
#define WL_CHANSPEC_CTL_SB_MASK		0x0700u
#define WL_CHANSPEC_CTL_SB_SHIFT	8u
#define WL_CHANSPEC_CTL_SB_LLL		0x0000u
#define WL_CHANSPEC_CTL_SB_LLU		0x0100u
#define WL_CHANSPEC_CTL_SB_LUL		0x0200u
#define WL_CHANSPEC_CTL_SB_LUU		0x0300u
#define WL_CHANSPEC_CTL_SB_ULL		0x0400u
#define WL_CHANSPEC_CTL_SB_ULU		0x0500u
#define WL_CHANSPEC_CTL_SB_UUL		0x0600u
#define WL_CHANSPEC_CTL_SB_UUU		0x0700u
#define WL_CHANSPEC_CTL_SB_LL		WL_CHANSPEC_CTL_SB_LLL
#define WL_CHANSPEC_CTL_SB_LU		WL_CHANSPEC_CTL_SB_LLU
#define WL_CHANSPEC_CTL_SB_UL		WL_CHANSPEC_CTL_SB_LUL
#define WL_CHANSPEC_CTL_SB_UU		WL_CHANSPEC_CTL_SB_LUU
#define WL_CHANSPEC_CTL_SB_L		WL_CHANSPEC_CTL_SB_LLL
#define WL_CHANSPEC_CTL_SB_U		WL_CHANSPEC_CTL_SB_LLU
#define WL_CHANSPEC_CTL_SB_LOWER	WL_CHANSPEC_CTL_SB_LLL
#define WL_CHANSPEC_CTL_SB_UPPER	WL_CHANSPEC_CTL_SB_LLU
#define WL_CHANSPEC_CTL_SB_NONE		WL_CHANSPEC_CTL_SB_LLL

/* channel sideband indication for frequency >= 240MHz */
#define WL_CHANSPEC_GE240_SB_MASK	0x0780u
#define WL_CHANSPEC_GE240_SB_SHIFT	7u

/* Bandwidth field */
#define WL_CHANSPEC_BW_MASK		0x3800u
#define WL_CHANSPEC_BW_SHIFT		11u
#define WL_CHANSPEC_BW_320		0x0000u
#define WL_CHANSPEC_BW_160160		0x0800u
#define WL_CHANSPEC_BW_20		0x1000u
#define WL_CHANSPEC_BW_40		0x1800u
#define WL_CHANSPEC_BW_80		0x2000u
#define WL_CHANSPEC_BW_160		0x2800u
#define WL_CHANSPEC_BW_8080		0x3000u
#define WL_CHANSPEC_BW_240		0x3800u

/* Band field */
#define WL_CHANSPEC_BAND_MASK		0xc000u
#define WL_CHANSPEC_BAND_SHIFT		14u
#define WL_CHANSPEC_BAND_2G		0x0000u
#define WL_CHANSPEC_BAND_6G		0x4000u
#define WL_CHANSPEC_BAND_4G		0x8000u
#define WL_CHANSPEC_BAND_5G		0xc000u

#define INVCHANSPEC			255u
#define MAX_CHANSPEC			0xFFFFu

#define WL_CHSPEC_BW(chspec)	((chspec & WL_CHANSPEC_BW_MASK) >> WL_CHANSPEC_BW_SHIFT)
#define MAX_BW_NUM		(uint8)(((WL_CHANSPEC_BW_MASK) >> WL_CHANSPEC_BW_SHIFT))

#define WL_CHANNEL_BAND(ch)	(((uint)(ch) <= CH_MAX_2G_CHANNEL) ? \
				 WL_CHANSPEC_BAND_2G : WL_CHANSPEC_BAND_5G)

/* channel defines */
#define LOWER_20_SB(channel)		(((channel) > CH_10MHZ_APART) ? \
					((channel) - CH_10MHZ_APART) : 0)
#define UPPER_20_SB(channel)		(((channel) < (MAXCHANNEL - CH_10MHZ_APART)) ? \
					((channel) + CH_10MHZ_APART) : 0)

/* pass a 80MHz channel number (uint8) to get respective LL, UU, LU, UL */
#define LL_20_SB(channel) (((channel) > 3 * CH_10MHZ_APART) ? ((channel) - 3 * CH_10MHZ_APART) : 0)
#define UU_20_SB(channel) (((channel) < (MAXCHANNEL - 3 * CH_10MHZ_APART)) ? \
			   ((channel) + 3 * CH_10MHZ_APART) : 0)
#define LU_20_SB(channel) LOWER_20_SB(channel)
#define UL_20_SB(channel) UPPER_20_SB(channel)

#define LOWER_40_SB(channel)		((channel) - CH_20MHZ_APART)
#define UPPER_40_SB(channel)		((channel) + CH_20MHZ_APART)

#ifndef CHSPEC_WLCBANDUNIT
#define CHSPEC_WLCBANDUNIT(chspec) \
	((CHSPEC_IS5G(chspec) || CHSPEC_IS6G(chspec)) ? BAND_5G_INDEX : BAND_2G_INDEX)
#endif
#define CH20MHZ_CHSPEC(channel)		(chanspec_t)((chanspec_t)(channel) | WL_CHANSPEC_BW_20 | \
						     WL_CHANNEL_BAND(channel))
#define NEXT_20MHZ_CHAN(channel)	(((channel) < (MAXCHANNEL - CH_20MHZ_APART)) ? \
					((channel) + CH_20MHZ_APART) : 0)
#define CH40MHZ_CHSPEC(channel, ctlsb)	(chanspec_t) \
					((channel) | (ctlsb) | WL_CHANSPEC_BW_40 | \
					 WL_CHANNEL_BAND(channel))
#define CH80MHZ_CHSPEC(channel, ctlsb)	(chanspec_t) \
					((channel) | (ctlsb) | \
					 WL_CHANSPEC_BW_80 | WL_CHANSPEC_BAND_5G)
#define CH160MHZ_CHSPEC(channel, ctlsb)	(chanspec_t) \
					((channel) | (ctlsb) | \
					 WL_CHANSPEC_BW_160 | WL_CHANSPEC_BAND_5G)

/* simple MACROs to get different fields of chanspec */
#define CHSPEC_CHANNEL(chspec)	((uint8)((chspec) & WL_CHANSPEC_CHAN_MASK))
#define CHSPEC_CHAN0(chspec)	(((chspec) & WL_CHANSPEC_CHAN0_MASK) >> WL_CHANSPEC_CHAN0_SHIFT)
#define CHSPEC_CHAN1(chspec)	(((chspec) & WL_CHANSPEC_CHAN1_MASK) >> WL_CHANSPEC_CHAN1_SHIFT)
#define CHSPEC_BAND(chspec)	((chspec) & WL_CHANSPEC_BAND_MASK)
#define CHSPEC_CTL_SB(chspec)	((chspec) & WL_CHANSPEC_CTL_SB_MASK)
#define CHSPEC_BW(chspec)	((chspec) & WL_CHANSPEC_BW_MASK)
#define CHSPEC_GE240_CHAN(chspec)	(((chspec) & WL_CHANSPEC_GE240_CHAN_MASK) >> \
					WL_CHANSPEC_GE240_CHAN_SHIFT)
#define CHSPEC_GE240_SB(chspec)	((chspec) & WL_CHANSPEC_GE240_SB_MASK)

#define CHSPEC_IS20(chspec)	(((chspec) & WL_CHANSPEC_BW_MASK) == WL_CHANSPEC_BW_20)
#define CHSPEC_IS20_5G(chspec)	((((chspec) & WL_CHANSPEC_BW_MASK) == WL_CHANSPEC_BW_20) && \
				 CHSPEC_IS5G(chspec))
#ifndef CHSPEC_IS40
#define CHSPEC_IS40(chspec)	(((chspec) & WL_CHANSPEC_BW_MASK) == WL_CHANSPEC_BW_40)
#endif
#ifndef CHSPEC_IS80
#define CHSPEC_IS80(chspec)	(((chspec) & WL_CHANSPEC_BW_MASK) == WL_CHANSPEC_BW_80)
#endif
#ifndef CHSPEC_IS160
#define CHSPEC_IS160(chspec)	(((chspec) & WL_CHANSPEC_BW_MASK) == WL_CHANSPEC_BW_160)
#endif
#define CHSPEC_IS8080(chspec)	(FALSE)
#ifndef CHSPEC_IS320
#ifdef WL11BE
#define CHSPEC_IS320(chspec)	(((chspec) & WL_CHANSPEC_BW_MASK) == WL_CHANSPEC_BW_320)
#else
#define CHSPEC_IS320(chspec)	(FALSE)
#endif
#endif /* CHSPEC_IS320 */
#ifndef CHSPEC_IS240
#ifdef WL11BE
#define CHSPEC_IS240(chspec)    (((chspec) & WL_CHANSPEC_BW_MASK) == WL_CHANSPEC_BW_240)
#else
#define CHSPEC_IS240(chspec)	(FALSE)
#endif
#endif /* CHSPEC_IS240 */

/* pass a center channel and get channel offset from it by 10MHz */
#define CH_OFF_10MHZ_MULTIPLES(channel, offset)				\
((uint8) (((offset) < 0) ?						\
	  (((channel) > (WL_CHANSPEC_CHAN_MASK & ((uint16)((-(offset)) * CH_10MHZ_APART)))) ? \
	   ((channel) + (offset) * CH_10MHZ_APART) : 0) :		\
	  ((((uint16)(channel) + (uint16)(offset) * CH_10MHZ_APART) < (uint16)MAXCHANNEL) ? \
	   ((channel) + (offset) * CH_10MHZ_APART) : 0)))

uint wf_chspec_first_20_sb(chanspec_t chspec);

#if defined(WL_BW160MHZ)
/* pass a 160MHz center channel to get 20MHz subband channel numbers */
#define LLL_20_SB_160(channel)  CH_OFF_10MHZ_MULTIPLES(channel, -7)
#define LLU_20_SB_160(channel)  CH_OFF_10MHZ_MULTIPLES(channel, -5)
#define LUL_20_SB_160(channel)  CH_OFF_10MHZ_MULTIPLES(channel, -3)
#define LUU_20_SB_160(channel)  CH_OFF_10MHZ_MULTIPLES(channel, -1)
#define ULL_20_SB_160(channel)  CH_OFF_10MHZ_MULTIPLES(channel,  1)
#define ULU_20_SB_160(channel)  CH_OFF_10MHZ_MULTIPLES(channel,  3)
#define UUL_20_SB_160(channel)  CH_OFF_10MHZ_MULTIPLES(channel,  5)
#define UUU_20_SB_160(channel)  CH_OFF_10MHZ_MULTIPLES(channel,  7)

/* get lowest 20MHz sideband of a given chspec
 * (works with 20, 40, 80, 160)
 */
#define CH_FIRST_20_SB(chspec)  ((uint8) (\
		CHSPEC_IS160(chspec) ? LLL_20_SB_160(CHSPEC_CHANNEL(chspec)) : (\
			CHSPEC_IS80(chspec) ? LL_20_SB(CHSPEC_CHANNEL(chspec)) : (\
				CHSPEC_IS40(chspec) ? LOWER_20_SB(CHSPEC_CHANNEL(chspec)) : \
					CHSPEC_CHANNEL(chspec)))))

/* get upper most 20MHz sideband of a given chspec
 * (works with 20, 40, 80, 160)
 */
#define CH_LAST_20_SB(chspec)  ((uint8) (\
		CHSPEC_IS160(chspec) ? UUU_20_SB_160(CHSPEC_CHANNEL(chspec)) : (\
			CHSPEC_IS80(chspec) ? UU_20_SB(CHSPEC_CHANNEL(chspec)) : (\
				CHSPEC_IS40(chspec) ? UPPER_20_SB(CHSPEC_CHANNEL(chspec)) : \
					CHSPEC_CHANNEL(chspec)))))

/* call this with chspec and a valid 20MHz sideband of this channel to get the next 20MHz sideband
 * (works with 20, 40, 80, 160)
 * resolves to 0 if called with upper most channel
 */
#define CH_NEXT_20_SB(chspec, channel)  ((uint8) (\
			((uint8) ((channel) + CH_20MHZ_APART) > CH_LAST_20_SB(chspec) ? 0 : \
				((channel) + CH_20MHZ_APART))))

#else /* WL_BW160MHZ */

#define LLL_20_SB_160(channel)  0
#define LLU_20_SB_160(channel)  0
#define LUL_20_SB_160(channel)  0
#define LUU_20_SB_160(channel)  0
#define ULL_20_SB_160(channel)  0
#define ULU_20_SB_160(channel)  0
#define UUL_20_SB_160(channel)  0
#define UUU_20_SB_160(channel)  0

/* get lowest 20MHz sideband of a given chspec
 * (works with 20, 40, 80)
 */
#define CH_FIRST_20_SB(chspec)  ((uint8) (\
			CHSPEC_IS80(chspec) ? LL_20_SB(CHSPEC_CHANNEL(chspec)) : (\
				CHSPEC_IS40(chspec) ? LOWER_20_SB(CHSPEC_CHANNEL(chspec)) : \
					CHSPEC_CHANNEL(chspec))))
/* get upper most 20MHz sideband of a given chspec
 * (works with 20, 40, 80, 160)
 */
#define CH_LAST_20_SB(chspec)  ((uint8) (\
			CHSPEC_IS80(chspec) ? UU_20_SB(CHSPEC_CHANNEL(chspec)) : (\
				CHSPEC_IS40(chspec) ? UPPER_20_SB(CHSPEC_CHANNEL(chspec)) : \
					CHSPEC_CHANNEL(chspec))))

/* call this with chspec and a valid 20MHz sideband of this channel to get the next 20MHz sideband
 * (works with 20, 40, 80, 160)
 * resolves to 0 if called with upper most channel
 */
#define CH_NEXT_20_SB(chspec, channel)  ((uint8) (\
			((uint8) ((channel) + CH_20MHZ_APART) > CH_LAST_20_SB(chspec) ? 0 : \
				((channel) + CH_20MHZ_APART))))

#endif /* WL_BW160MHZ */

/* Iterator for 20MHz side bands of a chanspec: (chanspec_t chspec, uint8 channel)
 * 'chspec' chanspec_t of interest (used in loop, better to pass a resolved value than a macro)
 * 'channel' must be a variable (not an expression).
 */
#define FOREACH_20_SB(chspec, channel) \
	for (channel = (uint8)wf_chspec_first_20_sb(chspec); channel;	\
			channel = CH_NEXT_20_SB((chspec), channel))

/* Uses iterator to populate array with all side bands involved (sorted lower to upper).
 *     'chspec' chanspec_t of interest
 *     'psb' pointer to uint8 array of enough size to hold all side bands for the given chspec
 */
#define GET_ALL_SB(chspec, psb) do { \
		uint8 channel, idx = 0; \
		chanspec_t chspec_local = chspec; \
		FOREACH_20_SB(chspec_local, channel) \
			(psb)[idx++] = channel; \
} while (0)

/* given a chanspec of any bw, tests if primary20 SB is in lower 20, 40, 80 respectively */
#define IS_CTL_IN_L20(chspec) !((chspec) & WL_CHANSPEC_CTL_SB_U) /* CTL SB is in low 20 of any 40 */
#define IS_CTL_IN_L40(chspec) !((chspec) & WL_CHANSPEC_CTL_SB_UL)	/* in low 40 of any 80 */
#define IS_CTL_IN_L80(chspec) !((chspec) & WL_CHANSPEC_CTL_SB_ULL)	/* in low 80 of 160 */

#define BW_LE40(bw)		((bw) == WL_CHANSPEC_BW_20 || ((bw) == WL_CHANSPEC_BW_40))
#define BW_LE80(bw)		(BW_LE40(bw) || ((bw) == WL_CHANSPEC_BW_80))
#define BW_LE160(bw)		(BW_LE80(bw) || ((bw) == WL_CHANSPEC_BW_160))

#define CHSPEC_IS6G(chspec)	(((chspec) & WL_CHANSPEC_BAND_MASK) == WL_CHANSPEC_BAND_6G)
#define CHSPEC_IS5G(chspec)	(((chspec) & WL_CHANSPEC_BAND_MASK) == WL_CHANSPEC_BAND_5G)
#define CHSPEC_IS2G(chspec)	(((chspec) & WL_CHANSPEC_BAND_MASK) == WL_CHANSPEC_BAND_2G)
#define CHSPEC_SB_UPPER(chspec)	\
	((((chspec) & WL_CHANSPEC_CTL_SB_MASK) == WL_CHANSPEC_CTL_SB_UPPER) && \
	(((chspec) & WL_CHANSPEC_BW_MASK) == WL_CHANSPEC_BW_40))
#define CHSPEC_SB_LOWER(chspec)	\
	((((chspec) & WL_CHANSPEC_CTL_SB_MASK) == WL_CHANSPEC_CTL_SB_LOWER) && \
	(((chspec) & WL_CHANSPEC_BW_MASK) == WL_CHANSPEC_BW_40))

#ifdef WL_BAND6G
#define CHSPEC2WLC_BAND(chspec) (CHSPEC_IS2G(chspec) ? WLC_BAND_2G : CHSPEC_IS5G(chspec) ? \
	WLC_BAND_5G : WLC_BAND_6G)
#else
#define CHSPEC2WLC_BAND(chspec) (CHSPEC_IS2G(chspec) ? WLC_BAND_2G : WLC_BAND_5G)
#endif

#define CHSPEC_BW_CHANGED(prev_chspec, curr_chspec) \
	(((prev_chspec) & WL_CHANSPEC_BW_MASK) != ((curr_chspec) & WL_CHANSPEC_BW_MASK))

#if (defined(WL_BAND6G) && !defined(WL_BAND6G_DISABLED))
#define CHSPEC_IS_5G_6G(chspec)		(CHSPEC_IS5G(chspec) || CHSPEC_IS6G(chspec))
#define CHSPEC_IS20_5G_6G(chspec)	((((chspec) & \
					WL_CHANSPEC_BW_MASK) == WL_CHANSPEC_BW_20) && \
					(CHSPEC_IS5G(chspec) || CHSPEC_IS6G(chspec)))
#else
#define CHSPEC_IS_5G_6G(chspec)		(CHSPEC_IS5G(chspec))
#define CHSPEC_IS20_5G_6G(chspec)	(CHSPEC_IS20_5G(chspec))
#endif

/**
 * Number of chars needed for wf_chspec_ntoa() destination character buffer.
 */
#ifdef WL11BE
#define CHANSPEC_STR_LEN    22
#else
#define CHANSPEC_STR_LEN    20
#endif

/*
 * This function returns TRUE if both the chanspec can co-exist in PHY.
 * Addition to primary20 channel, the function checks for side band for 2g 40 channels
 */
extern bool wf_chspec_coexist(chanspec_t chspec1, chanspec_t chspec2);

#define CHSPEC_IS_BW_160_WIDE(chspec) (CHSPEC_BW(chspec) == WL_CHANSPEC_BW_160 ||\
	CHSPEC_BW(chspec) == WL_CHANSPEC_BW_8080)

/* BW inequality comparisons, GE (>=), GT (>) */

#define CHSPEC_BW_GE(chspec, bw) (CHSPEC_BW(chspec) >= (bw))

#define CHSPEC_BW_GT(chspec, bw) (CHSPEC_BW(chspec) > (bw))

/* Legacy Chanspec defines
 * These are the defines for the previous format of the chanspec_t
 */
#define WL_LCHANSPEC_CHAN_MASK		0x00ff
#define WL_LCHANSPEC_CHAN_SHIFT		     0

#define WL_LCHANSPEC_CTL_SB_MASK	0x0300
#define WL_LCHANSPEC_CTL_SB_SHIFT	     8
#define WL_LCHANSPEC_CTL_SB_LOWER	0x0100
#define WL_LCHANSPEC_CTL_SB_UPPER	0x0200
#define WL_LCHANSPEC_CTL_SB_NONE	0x0300

#define WL_LCHANSPEC_BW_MASK		0x0C00
#define WL_LCHANSPEC_BW_SHIFT		    10
#define WL_LCHANSPEC_BW_10		0x0400
#define WL_LCHANSPEC_BW_20		0x0800
#define WL_LCHANSPEC_BW_40		0x0C00

#define WL_LCHANSPEC_BAND_MASK		0xf000
#define WL_LCHANSPEC_BAND_SHIFT		    12
#define WL_LCHANSPEC_BAND_5G		0x1000
#define WL_LCHANSPEC_BAND_2G		0x2000

#define LCHSPEC_CHANNEL(chspec)	((uint8)((chspec) & WL_LCHANSPEC_CHAN_MASK))
#define LCHSPEC_BAND(chspec)	((chspec) & WL_LCHANSPEC_BAND_MASK)
#define LCHSPEC_CTL_SB(chspec)	((chspec) & WL_LCHANSPEC_CTL_SB_MASK)
#define LCHSPEC_BW(chspec)	((chspec) & WL_LCHANSPEC_BW_MASK)
#define LCHSPEC_IS20(chspec)	(((chspec) & WL_LCHANSPEC_BW_MASK) == WL_LCHANSPEC_BW_20)
#define LCHSPEC_IS40(chspec)	(((chspec) & WL_LCHANSPEC_BW_MASK) == WL_LCHANSPEC_BW_40)
#define LCHSPEC_IS5G(chspec)	(((chspec) & WL_LCHANSPEC_BAND_MASK) == WL_LCHANSPEC_BAND_5G)
#define LCHSPEC_IS2G(chspec)	(((chspec) & WL_LCHANSPEC_BAND_MASK) == WL_LCHANSPEC_BAND_2G)

#define LCHSPEC_SB_UPPER(chspec)	\
	((((chspec) & WL_LCHANSPEC_CTL_SB_MASK) == WL_LCHANSPEC_CTL_SB_UPPER) && \
	(((chspec) & WL_LCHANSPEC_BW_MASK) == WL_LCHANSPEC_BW_40))
#define LCHSPEC_SB_LOWER(chspec)	\
	((((chspec) & WL_LCHANSPEC_CTL_SB_MASK) == WL_LCHANSPEC_CTL_SB_LOWER) && \
	(((chspec) & WL_LCHANSPEC_BW_MASK) == WL_LCHANSPEC_BW_40))

#define LCHSPEC_CREATE(chan, band, bw, sb)  ((uint16)((chan) | (sb) | (bw) | (band)))

#define CH20MHZ_LCHSPEC(channel) \
	(chanspec_t)((chanspec_t)(channel) | WL_LCHANSPEC_BW_20 | \
	WL_LCHANSPEC_CTL_SB_NONE | (((channel) <= CH_MAX_2G_CHANNEL) ? \
	WL_LCHANSPEC_BAND_2G : WL_LCHANSPEC_BAND_5G))

#define GET_ALL_EXT wf_get_all_ext

/*
 * WF_CHAN_FACTOR_* constants are used to calculate channel frequency
 * given a channel number.
 * chan_freq = chan_factor * 500Mhz + chan_number * 5
 */

/**
 * Channel Factor for the starting frequence of 2.4 GHz channels.
 * The value corresponds to 2407 MHz.
 */
#define WF_CHAN_FACTOR_2_4_G		4814u	/* 2.4 GHz band, 2407 MHz */

/**
 * Channel Factor for the starting frequence of 4.9 GHz channels.
 * The value corresponds to 4000 MHz.
 */
#define WF_CHAN_FACTOR_4_G		8000u	/* 4.9 GHz band for Japan */

/**
 * Channel Factor for the starting frequence of 5 GHz channels.
 * The value corresponds to 5000 MHz.
 */
#define WF_CHAN_FACTOR_5_G		10000u	/* 5   GHz band, 5000 MHz */

/**
 * Channel Factor for the starting frequence of 6 GHz channels.
 * The value corresponds to 5940 MHz.
 */
#define WF_CHAN_FACTOR_6_G		11900u	/* 6   GHz band, 5950 MHz */

#define WLC_2G_25MHZ_OFFSET		5	/* 2.4GHz band channel offset */

/**
 *  No of sub-band value of the specified Mhz chanspec
 */
#define WF_NUM_SIDEBANDS_40MHZ   2u
#define WF_NUM_SIDEBANDS_80MHZ   4u
#define WF_NUM_SIDEBANDS_160MHZ  8u

/**
 * Return the chanspec bandwidth in MHz
 */
uint wf_bw_chspec_to_mhz(chanspec_t chspec);

/**
 * Return the bandwidth string for a given chanspec
 */
const char *wf_chspec_to_bw_str(chanspec_t chspec);

/**
 * Convert chanspec to ascii string, or formats hex of an invalid chanspec.
 */
char * wf_chspec_ntoa_ex(chanspec_t chspec, char *buf);

/**
 * Convert chanspec to ascii string, or returns NULL on error.
 */
char * wf_chspec_ntoa(chanspec_t chspec, char *buf);

/**
 * Convert ascii string to chanspec
 */
chanspec_t wf_chspec_aton(const char *a);

/**
 * Verify the chanspec fields are valid for a chanspec_t
 */
bool wf_chspec_malformed(chanspec_t chanspec);

/**
 * Verify the chanspec specifies a valid channel according to 802.11.
 */
bool wf_chspec_valid(chanspec_t chanspec);

/**
 * Verify that the channel is a valid 20MHz channel according to 802.11.
 */
bool wf_valid_20MHz_chan(uint channel, chanspec_band_t band);

/**
 * Verify that the center channel is a valid 40MHz center channel according to 802.11.
 */
bool wf_valid_40MHz_center_chan(uint center_channel, chanspec_band_t band);

/**
 * Verify that the center channel is a valid 80MHz center channel according to 802.11.
 */
bool wf_valid_80MHz_center_chan(uint center_channel, chanspec_band_t band);

/**
 * Verify that the center channel is a valid 160MHz center channel according to 802.11.
 */
bool wf_valid_160MHz_center_chan(uint center_channel, chanspec_band_t band);

/**
 * Verify that the center channel is a valid 240MHz center channel according to 802.11.
 */
bool wf_valid_240MHz_center_chan(uint center_channel, chanspec_band_t band);

/**
 * Verify that the center channel is a valid 320MHz center channel according to 802.11.
 */
bool wf_valid_320MHz_center_chan(uint center_channel, chanspec_band_t band);

/**
 * Create a 20MHz chanspec for the given band.
 */
chanspec_t wf_create_20MHz_chspec(uint channel, chanspec_band_t band);

/**
 * Returns the chanspec for a 40MHz channel given the primary 20MHz channel number,
 * the center channel number, and the band.
 */
chanspec_t wf_create_40MHz_chspec(uint primary_channel, uint center_channel,
                                  chanspec_band_t band);

/**
 * Returns the chanspec for a 40MHz channel given the primary 20MHz channel number,
 * the sub-band for the primary 20MHz channel, and the band.
 */
chanspec_t wf_create_40MHz_chspec_primary_sb(uint primary_channel,
                                             chanspec_subband_t primary_subband,
                                             chanspec_band_t band);
/**
 * Returns the chanspec for an 80MHz channel given the primary 20MHz channel number,
 * the center channel number, and the band.
 */
chanspec_t wf_create_80MHz_chspec(uint primary_channel, uint center_channel,
                                  chanspec_band_t band);

/**
 * Returns the chanspec for an 160MHz channel given the primary 20MHz channel number,
 * the center channel number, and the band.
 */
chanspec_t wf_create_160MHz_chspec(uint primary_channel, uint center_channel,
                                   chanspec_band_t band);

/**
 * Returns the chanspec for an 240MHz channel given the primary 20MHz channel number,
 * the center channel number, and the band.
 */
chanspec_t wf_create_240MHz_chspec(uint primary_channel, uint center_channel,
                                   chanspec_band_t band);

/**
 * Returns the chanspec for an 320MHz channel given the primary 20MHz channel number,
 * the center channel number, and the band.
 */
chanspec_t wf_create_320MHz_chspec(uint primary_channel, uint center_channel,
                                   chanspec_band_t band);

/**
 * Returns the chanspec for an 80+80MHz channel given the primary 20MHz channel number,
 * the center channel numbers for each frequency segment, and the band.
 */
chanspec_t wf_create_8080MHz_chspec(uint primary_channel, uint chan0, uint chan1,
                                    chanspec_band_t band);

/**
 * Returns the chanspec for an 160+160MHz channel given the primary 20MHz channel number,
 * the center channel numbers for each frequency segment, and the band.
 */
chanspec_t wf_create_160160MHz_chspec(uint primary_channel, uint chan0, uint chan1,
                                      chanspec_band_t band);
/**
 * Returns the chanspec given the primary 20MHz channel number,
 * the center channel number, channel width, and the band.
 *
 * The channel width must be 20, 40, 80, or 160 MHz.
 */
chanspec_t wf_create_chspec(uint primary_channel, uint center_channel,
                            chanspec_bw_t bw, chanspec_band_t band);

/**
 * Returns the chanspec given the primary 20MHz channel number,
 * channel width, and the band.
 */
chanspec_t wf_create_chspec_from_primary(uint primary_channel, chanspec_bw_t bw,
                                         chanspec_band_t band);

/**
 * Returns the chanspec given the index of primary 20MHz channel within whole
 * channel, the center channel number, channel width, and the band.
 *
 * The channel width must be 20, 40, 80, or 160 MHz.
 */
chanspec_t wf_create_chspec_sb(uint sb, uint center_channel, chanspec_bw_t bw,
                               chanspec_band_t band);

/**
 * Returns the chanspec for an 160+160MHz channel given the index of primary 20MHz
 * channel within whole channel pair (0-3 if within chan0, 4-7 if within chan1),
 * the center channel numbers for each frequency segment, and the band.
 */
chanspec_t wf_create_160160MHz_chspec_sb(uint sb, uint chan0, uint chan1,
                                         chanspec_band_t band);

/**
 * Return the primary 20MHz channel.
 */
uint8 wf_chspec_primary20_chan(chanspec_t chspec);

/* alias for old function name */
#define wf_chspec_ctlchan(c) wf_chspec_primary20_chan(c)

/**
 * Return the primary 20MHz chanspec of a given chanspec
 */
chanspec_t wf_chspec_primary20_chspec(chanspec_t chspec);

/* alias for old function name */
#define wf_chspec_ctlchspec(c) wf_chspec_primary20_chspec(c)

/**
 * Return the primary 40MHz chanspec for a 40MHz or wider channel
 */
chanspec_t wf_chspec_primary40_chspec(chanspec_t chspec);

/**
 * Return the channel number for a given frequency and base frequency
 */
int wf_mhz2channel(uint freq, uint start_factor);

/**
 * Return the center frequency in MHz of the given channel and base frequency.
 */
int wf_channel2mhz(uint channel, uint start_factor);

/**
 * Returns the chanspec 80Mhz channel corresponding to the following input
 * parameters
 *
 *	primary_channel - primary 20Mhz channel
 *	center_channel   - center frequecny of the 80Mhz channel
 *
 * The center_channel can be one of {42, 58, 106, 122, 138, 155}
 *
 * returns INVCHANSPEC in case of error
 */
extern chanspec_t wf_chspec_80(uint8 center_channel, uint8 primary_channel);

/**
 * Convert ctl chan and bw to chanspec
 *
 * @param	ctl_ch		channel
 * @param	bw	        bandwidth
 *
 * @return	> 0 if successful or 0 otherwise
 *
 */
extern uint16 wf_channel2chspec(uint ctl_ch, uint bw);

/*
 * Returns the 80+80 MHz chanspec corresponding to the following input parameters
 *
 *    primary_20mhz - Primary 20 MHz channel
 *    chan0_80MHz - center channel number of one frequency segment
 *    chan1_80MHz - center channel number of the other frequency segment
 *
 * Parameters chan0_80MHz and chan1_80MHz are channel numbers in {42, 58, 106, 122, 138, 155}.
 * The primary channel must be contained in one of the 80MHz channels. This routine
 * will determine which frequency segment is the primary 80 MHz segment.
 *
 * Returns INVCHANSPEC in case of error.
 *
 * Refer to 802.11-2016 section 22.3.14 "Channelization".
 */
extern chanspec_t wf_chspec_get8080_chspec(uint8 primary_20mhz,
	uint8 chan0_80Mhz, uint8 chan1_80Mhz);

/**
 * Returns the center channel of the primary 80 MHz sub-band of the provided chanspec
 *
 * @param	chspec    input chanspec
 *
 * @return	center channel number of the primary 80MHz sub-band of the input.
 *		Will return the center channel of an input 80MHz chspec.
 *		Will return INVCHANNEL if the chspec is malformed or less than 80MHz bw.
 */
extern uint8 wf_chspec_primary80_channel(chanspec_t chanspec);

/**
 * Returns the center channel of the secondary 80 MHz sub-band of the provided chanspec
 *
 * @param	chspec    input chanspec
 *
 * @return	center channel number of the secondary 80MHz sub-band of the input.
 *		Will return INVCHANNEL if the chspec is malformed or bw is not greater than 80MHz.
 */
extern uint8 wf_chspec_secondary80_channel(chanspec_t chanspec);

/**
 * Returns the chanspec for the primary 80MHz sub-band of an 160MHz or 80+80 channel
 *
 * @param	chspec    input chanspec
 *
 * @return	An 80MHz chanspec describing the primary 80MHz sub-band of the input.
 *		Will return an input 80MHz chspec as is.
 *		Will return INVCHANSPEC if the chspec is malformed or less than 80MHz bw.
 */
extern chanspec_t wf_chspec_primary80_chspec(chanspec_t chspec);

/**
 * Returns the chanspec for the secondary 80MHz sub-band of an 160MHz or 80+80 channel
 * The sideband in the chanspec is always set to WL_CHANSPEC_CTL_SB_LL since this sub-band
 * does not contain the primary 20MHz channel.
 *
 * @param	chspec    input chanspec
 *
 * @return	An 80MHz chanspec describing the secondary 80MHz sub-band of the input.
 *		Will return INVCHANSPEC if the chspec is malformed or bw is not greater than 80MHz.
 */
extern chanspec_t wf_chspec_secondary80_chspec(chanspec_t chspec);

/**
 * Returns the center channel of the primary 160MHz sub-band of the provided chanspec
 *
 * @param	chspec    input chanspec
 *
 * @return	center channel number of the primary 160MHz sub-band of the input.
 *		Will return the center channel of an input 160MHz chspec.
 *		Will return INVCHANNEL if the chspec is malformed or less than 160MHz bw.
 */
extern uint8 wf_chspec_primary160_channel(chanspec_t chanspec);

/**
 * Returns the chanspec for the primary 160MHz sub-band of an 320MHz channel
 *
 * @param	chspec    input chanspec
 *
 * @return	An 160MHz chanspec describing the primary 160MHz sub-band of the input.
 *		Will return an input 160MHz chspec as is.
 *		Will return INVCHANSPEC if the chspec is malformed or less than 160MHz bw.
 */
extern chanspec_t wf_chspec_primary160_chspec(chanspec_t chspec);

/*
 * For 160MHz or 80P80 chanspec, set ch[0]/ch[1] to be the low/high 80 Mhz channels
 *
 * For 20/40/80MHz chanspec, set ch[0] to be the center freq, and chan[1]=-1
 */
extern void wf_chspec_get_80p80_channels(chanspec_t chspec, uint8 *ch);

/* wf_chanspec_iter_... iterator API is deprecated. Use wlc_clm_chanspec_iter_... API instead */

struct wf_iter_range {
	uint8 start;
	uint8 end;
};

/* Internal structure for wf_chanspec_iter_* functions.
 * Do not directly access the members. Only use the related
 * functions to query and manipulate the structure.
 */
typedef struct chanspec_iter {
	uint8 state;
	chanspec_t chanspec;
	chanspec_band_t band;
	chanspec_bw_t bw;
	struct wf_iter_range range;
	union {
		uint8 range_id;
		struct {
			uint8 ch0;
			uint8 ch1;
		};
	};
} wf_chanspec_iter_t;

/**
 * Initialize a chanspec iteration structure.
 * The parameters define the set of chanspecs to generate in the iteration.
 * After initialization wf_chanspec_iter_current() will return the first chanspec
 * in the set. A call to wf_chanspec_iter_next() will advance the interation
 * to the next chanspec in the set.
 *
 * Example use:
 *      wf_chanspec_iter_t iter;
 *      chanspec_t chanspec;
 *
 *      wf_chanspec_iter_init(&iter, band, bw);
 *
 *      while (wf_chanspec_iter_next(&iter, &chanspec)) {
 *              ... do some work ...
 *      }
 *
 * @param iter  pointer to a wf_chanspec_iter_t structure to initialize
 * @param band  chanspec_band_t value specifying the band of interest
 * @param bw    chanspec_bw_t value specifying the bandwidth of interest,
 *              or INVCHANSPEC to specify all bandwidths
 *
 * @return a success value, FALSE on error, or TRUE if OK
 */
bool wf_chanspec_iter_init(wf_chanspec_iter_t *iter, chanspec_band_t band, chanspec_bw_t bw);

/**
 * Advance the iteration to the next chanspec in the set.
 *
 * @param iter    pointer to a wf_chanspec_iter_t structure
 * @param chspec  pointer to storage for the next chanspec. Return value will be INVCHANSPEC
 *                if the iteration ended. Pass in NULL if return value is not desired.
 *
 * @return a success value, TRUE if there was another chanspec in the iteration, FALSE if not
 */
bool wf_chanspec_iter_next(wf_chanspec_iter_t *iter, chanspec_t *chspec);

/**
 * Return the current chanspec of the iteration.
 *
 * @param iter  pointer to a wf_chanspec_iter_t structure
 *
 * @return the current chanspec_t
 */
chanspec_t wf_chanspec_iter_current(wf_chanspec_iter_t *iter);

/* Populates array with all 20MHz side bands of a given chanspec_t in the following order:
 *		primary20, ext20, two ext40s, four ext80s.
 *     'chspec' is the chanspec of interest
 *     'pext' must point to an uint8 array of long enough to hold all side bands of the given chspec
 *
 * Works with 20, 40, 80 and 160MHz chspec
 */

extern void wf_get_all_ext(chanspec_t chspec, uint8 *chan_ptr);

/*
 * Given two chanspecs, returns true if they overlap.
 * (Overlap: At least one 20MHz subband is common between the two chanspecs provided)
 */
extern bool wf_chspec_overlap(chanspec_t chspec0, chanspec_t chspec1);

extern uint8 channel_bw_to_width(chanspec_t chspec);

uint8 wf_chspec_320_id2cch(chanspec_t chanspec);

uint8 wf_chspec_240_id2cch(chanspec_t chanspec);

#endif	/* _bcmwifi_channels_h_ */
