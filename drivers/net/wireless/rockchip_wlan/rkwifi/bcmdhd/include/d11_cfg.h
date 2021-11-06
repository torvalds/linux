/*
 * Header file for splitrx mode definitions
 * Explains different splitrx modes, macros for classify, conversion.
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

#ifndef _d11_cfg_h_
#define _d11_cfg_h_

#ifdef USE_BCMCONF_H
#include <bcmconf.h>
#else
#if defined(BCMDONGLEHOST) && !defined(WINNT)
#define D11REV_IS(var, val)		((var) == (val))
#define D11REV_GE(var, val)		((var) >= (val))
#define D11REV_GT(var, val)		((var) > (val))
#define D11REV_LT(var, val)		((var) < (val))
#define D11REV_LE(var, val)		((var) <= (val))

#define D11MINORREV_IS(var, val)	((var) == (val))
#define D11MINORREV_GE(var, val)	((var) >= (val))
#define D11MINORREV_GT(var, val)	((var) > (val))
#define D11MINORREV_LT(var, val)	((var) < (val))
#define D11MINORREV_LE(var, val)	((var) <= (val))

#define D11REV_MAJ_MIN_GE(corerev, corerev_minor, maj, min) \
	((D11REV_IS((corerev), (maj)) && D11MINORREV_GE((corerev_minor), (min))) || \
		D11REV_GT(corerev, (maj)))

#endif /* BCMDONGLEHOST */
#endif /* USE_BCMCONF_H */

#define	RXMODE0	0	/* no split */
#define	RXMODE1	1	/* descriptor split */
#define	RXMODE2	2	/* descriptor split + classification */
#define	RXMODE3	3	/* fifo split + classification */
#define	RXMODE4	4	/* fifo split + classification + hdr conversion */

#ifdef BCMSPLITRX
	extern bool _bcmsplitrx;
#if defined(ROM_ENAB_RUNTIME_CHECK) || !defined(DONGLEBUILD)
	#define BCMSPLITRX_ENAB() (_bcmsplitrx)
#elif defined(BCMSPLITRX_DISABLED)
	#define BCMSPLITRX_ENAB()	(0)
#else
	#define BCMSPLITRX_ENAB()	(1)
#endif

	extern uint8 _bcmsplitrx_mode;
#if defined(ROM_ENAB_RUNTIME_CHECK) || !defined(DONGLEBUILD)
	#define BCMSPLITRX_MODE() (_bcmsplitrx_mode)
#elif defined(BCMSPLITRX_DISABLED)
	#define BCMSPLITRX_MODE()	(0)
#else
	#define BCMSPLITRX_MODE() (_bcmsplitrx_mode)
#endif
#else
	#define BCMSPLITRX_ENAB()		(0)
	#define BCMSPLITRX_MODE()		(0)
#endif /* BCMSPLITRX */

#define SPLIT_RXMODE1()	((BCMSPLITRX_MODE() == RXMODE1))
#define SPLIT_RXMODE2()	((BCMSPLITRX_MODE() == RXMODE2))
#define SPLIT_RXMODE3()	((BCMSPLITRX_MODE() == RXMODE3))
#define SPLIT_RXMODE4()	((BCMSPLITRX_MODE() == RXMODE4))

#define PKT_CLASSIFY()	(SPLIT_RXMODE2() || SPLIT_RXMODE3() || SPLIT_RXMODE4())
#define RXFIFO_SPLIT()	(SPLIT_RXMODE3() || SPLIT_RXMODE4())
#define HDR_CONV()	(SPLIT_RXMODE4())
#define HDRCONV_PAD	2

#define FRAG_CMN_MSG_HDROOM	(16u) /* Common msg headroom required by PCIe to push txstatus */

#if defined(FMF_LIT) && !defined(FMF_LIT_DISABLED)
/* (188-4*24-16) required HEADROOM - 4 Rate info Block - CacheInfo */
#define FRAG_HEADROOM_D11REV_GE83 76u
#else
#if (defined(WLC_TXDC) && !defined(WLC_TXDC_DISABLED)) || \
	(defined(FMF_RIT) && !defined(FMF_RIT_DISABLED))
#define FRAG_HEADROOM_D11REV_GE83 92u /* (188-4*24) required HEADROOM - 4 Rate info Block */
#else
/* required HEADROOM = PTXD (24) + LIT (16) + RIT (96)
	+ max dot11hdr (44)::
	     "FC+DUR+SEQ+A1+A2+A3"(24) + QOS(2) + max("HTC(4) + AES IV(8)", WAPI IV(18))
	+ MSDU data size (22):: SFH (14) + LLC (8)
	- ETHER_HDR_LEN
 */
#define FRAG_HEADROOM_D11REV_GE83 188u
#endif /* (WLC_TXDC && !WLC_TXDC_DISABLED) || (FMF_RIT && !FMF_RIT_DISABLED) */
#endif /* defined(FMF_LIT) && !defined(FMF_LIT_DISABLED) */
#define FRAG_HEADROOM_D11REV_LT80 226u /* TXOFF + amsdu header */
#define FRAG_HEADROOM_D11REV_GE80 \
		(FRAG_HEADROOM_D11REV_GE83 + 4u) /* + TSO_HEADER_PASSTHROUGH_LENGTH(4) */

#ifdef USE_NEW_COREREV_API
#define FRAG_HEAD_ROOM(corerev) (D11REV_GE(corerev, 83) ? \
		FRAG_HEADROOM_D11REV_GE83 : D11REV_GE(corerev, 80) ? \
		FRAG_HEADROOM_D11REV_GE80 : FRAG_HEADROOM_D11REV_LT80)
#else
#define FRAG_HEAD_ROOM(sih, coreid) ((si_get_corerev(sih, coreid) >= 83) ? \
		FRAG_HEADROOM_D11REV_GE83 : ((si_get_corerev(sih, coreid) >= 80) ? \
		FRAG_HEADROOM_D11REV_GE80 : FRAG_HEADROOM_D11REV_LT80))
#endif

#endif /* _d11_cfg_h_ */
