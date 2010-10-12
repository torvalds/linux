/*
 * Copyright (c) 2000-2005 ZyDAS Technology Corporation
 * Copyright (c) 2007-2008 Atheros Communications Inc.
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
/*                                                                      */
/*  Module Name : hpreg.c                                               */
/*                                                                      */
/*  Abstract                                                            */
/*      This module contains Regulatory Table and related function.     */
/*                                                                      */
/*  NOTES                                                               */
/*      None                                                            */
/*                                                                      */
/************************************************************************/
#include "../80211core/cprecomp.h"
#include "hpani.h"
#include "hpreg.h"
#include "hpusb.h"

#define HAL_MODE_11A_TURBO	HAL_MODE_108A
#define HAL_MODE_11G_TURBO	HAL_MODE_108G

#if 0
enum {
    /* test groups */
	FCC	       = 0x10,
	MKK	       = 0x40,
	ETSI	   = 0x30,
    SD_NO_CTL  = 0xe0,
	NO_CTL	   = 0xff,
    /* test modes */
    CTL_MODE_M = 0x0f,
    CTL_11A    = 0,
	CTL_11B	   = 1,
	CTL_11G	   = 2,
	CTL_TURBO  = 3,
	CTL_108G   = 4,
    CTL_2GHT20 = 5,
    CTL_5GHT20 = 6,
    CTL_2GHT40 = 7,
    CTL_5GHT40 = 8
};
#endif

/*
 * The following are flags for different requirements per reg domain.
 * These requirements are either inhereted from the reg domain pair or
 * from the unitary reg domain if the reg domain pair flags value is
 * 0
 */

enum {
	NO_REQ			= 0x00000000,
	DISALLOW_ADHOC_11A	= 0x00000001,
	DISALLOW_ADHOC_11A_TURB	= 0x00000002,
	NEED_NFC		= 0x00000004,

	ADHOC_PER_11D		= 0x00000008,  /* Start Ad-Hoc mode */
	ADHOC_NO_11A		= 0x00000010,

	PUBLIC_SAFETY_DOMAIN	= 0x00000020, 	/* public safety domain */
	LIMIT_FRAME_4MS 	= 0x00000040, 	/* 4msec limit on the frame length */
};

#define MKK5GHZ_FLAG1 (DISALLOW_ADHOC_11A_TURB | NEED_NFC | LIMIT_FRAME_4MS)
#define MKK5GHZ_FLAG2 (DISALLOW_ADHOC_11A | DISALLOW_ADHOC_11A_TURB | NEED_NFC | LIMIT_FRAME_4MS)

typedef enum {
	DFS_UNINIT_DOMAIN	= 0,	/* Uninitialized dfs domain */
	DFS_FCC_DOMAIN		= 1,	/* FCC3 dfs domain */
	DFS_ETSI_DOMAIN		= 2,	/* ETSI dfs domain */
} HAL_DFS_DOMAIN;

/*
 * Used to set the RegDomain bitmask which chooses which frequency
 * band specs are used.
 */

#define BMLEN 2		/* Use 2 64 bit uint for channel bitmask
			   NB: Must agree with macro below (BM) */
#define BMZERO {(u64_t) 0, (u64_t) 0}	/* BMLEN zeros */

#if 0

#define BM(_fa, _fb, _fc, _fd, _fe, _ff, _fg, _fh, _fi, _fj, _fk, _fl) \
      {((((_fa >= 0) && (_fa < 64)) ? (((u64_t) 1) << _fa) : (u64_t) 0) | \
	(((_fb >= 0) && (_fb < 64)) ? (((u64_t) 1) << _fb) : (u64_t) 0) | \
	(((_fc >= 0) && (_fc < 64)) ? (((u64_t) 1) << _fc) : (u64_t) 0) | \
	(((_fd >= 0) && (_fd < 64)) ? (((u64_t) 1) << _fd) : (u64_t) 0) | \
	(((_fe >= 0) && (_fe < 64)) ? (((u64_t) 1) << _fe) : (u64_t) 0) | \
	(((_ff >= 0) && (_ff < 64)) ? (((u64_t) 1) << _ff) : (u64_t) 0) | \
	(((_fg >= 0) && (_fg < 64)) ? (((u64_t) 1) << _fg) : (u64_t) 0) | \
	(((_fh >= 0) && (_fh < 64)) ? (((u64_t) 1) << _fh) : (u64_t) 0) | \
	(((_fi >= 0) && (_fi < 64)) ? (((u64_t) 1) << _fi) : (u64_t) 0) | \
	(((_fj >= 0) && (_fj < 64)) ? (((u64_t) 1) << _fj) : (u64_t) 0) | \
	(((_fk >= 0) && (_fk < 64)) ? (((u64_t) 1) << _fk) : (u64_t) 0) | \
	(((_fl >= 0) && (_fl < 64)) ? (((u64_t) 1) << _fl) : (u64_t) 0) | \
	       ((((_fa > 63) && (_fa < 128)) ? (((u64_t) 1) << (_fa - 64)) : (u64_t) 0) | \
		(((_fb > 63) && (_fb < 128)) ? (((u64_t) 1) << (_fb - 64)) : (u64_t) 0) | \
		(((_fc > 63) && (_fc < 128)) ? (((u64_t) 1) << (_fc - 64)) : (u64_t) 0) | \
		(((_fd > 63) && (_fd < 128)) ? (((u64_t) 1) << (_fd - 64)) : (u64_t) 0) | \
		(((_fe > 63) && (_fe < 128)) ? (((u64_t) 1) << (_fe - 64)) : (u64_t) 0) | \
		(((_ff > 63) && (_ff < 128)) ? (((u64_t) 1) << (_ff - 64)) : (u64_t) 0) | \
		(((_fg > 63) && (_fg < 128)) ? (((u64_t) 1) << (_fg - 64)) : (u64_t) 0) | \
		(((_fh > 63) && (_fh < 128)) ? (((u64_t) 1) << (_fh - 64)) : (u64_t) 0) | \
		(((_fi > 63) && (_fi < 128)) ? (((u64_t) 1) << (_fi - 64)) : (u64_t) 0) | \
		(((_fj > 63) && (_fj < 128)) ? (((u64_t) 1) << (_fj - 64)) : (u64_t) 0) | \
		(((_fk > 63) && (_fk < 128)) ? (((u64_t) 1) << (_fk - 64)) : (u64_t) 0) | \
		(((_fl > 63) && (_fl < 128)) ? (((u64_t) 1) << (_fl - 64)) : (u64_t) 0)))}

#else

#define BM(_fa, _fb, _fc, _fd, _fe, _ff, _fg, _fh, _fi, _fj, _fk, _fl) \
      {((((_fa >= 0) && (_fa < 64)) ? (((u64_t) 1) << (_fa&0x3f)) : (u64_t) 0) | \
	(((_fb >= 0) && (_fb < 64)) ? (((u64_t) 1) << (_fb&0x3f)) : (u64_t) 0) | \
	(((_fc >= 0) && (_fc < 64)) ? (((u64_t) 1) << (_fc&0x3f)) : (u64_t) 0) | \
	(((_fd >= 0) && (_fd < 64)) ? (((u64_t) 1) << (_fd&0x3f)) : (u64_t) 0) | \
	(((_fe >= 0) && (_fe < 64)) ? (((u64_t) 1) << (_fe&0x3f)) : (u64_t) 0) | \
	(((_ff >= 0) && (_ff < 64)) ? (((u64_t) 1) << (_ff&0x3f)) : (u64_t) 0) | \
	(((_fg >= 0) && (_fg < 64)) ? (((u64_t) 1) << (_fg&0x3f)) : (u64_t) 0) | \
	(((_fh >= 0) && (_fh < 64)) ? (((u64_t) 1) << (_fh&0x3f)) : (u64_t) 0) | \
	(((_fi >= 0) && (_fi < 64)) ? (((u64_t) 1) << (_fi&0x3f)) : (u64_t) 0) | \
	(((_fj >= 0) && (_fj < 64)) ? (((u64_t) 1) << (_fj&0x3f)) : (u64_t) 0) | \
	(((_fk >= 0) && (_fk < 64)) ? (((u64_t) 1) << (_fk&0x3f)) : (u64_t) 0) | \
	(((_fl >= 0) && (_fl < 64)) ? (((u64_t) 1) << (_fl&0x3f)) : (u64_t) 0) | \
	       ((((_fa > 63) && (_fa < 128)) ? (((u64_t) 1) << ((_fa - 64)&0x3f)) : (u64_t) 0) | \
		(((_fb > 63) && (_fb < 128)) ? (((u64_t) 1) << ((_fb - 64)&0x3f)) : (u64_t) 0) | \
		(((_fc > 63) && (_fc < 128)) ? (((u64_t) 1) << ((_fc - 64)&0x3f)) : (u64_t) 0) | \
		(((_fd > 63) && (_fd < 128)) ? (((u64_t) 1) << ((_fd - 64)&0x3f)) : (u64_t) 0) | \
		(((_fe > 63) && (_fe < 128)) ? (((u64_t) 1) << ((_fe - 64)&0x3f)) : (u64_t) 0) | \
		(((_ff > 63) && (_ff < 128)) ? (((u64_t) 1) << ((_ff - 64)&0x3f)) : (u64_t) 0) | \
		(((_fg > 63) && (_fg < 128)) ? (((u64_t) 1) << ((_fg - 64)&0x3f)) : (u64_t) 0) | \
		(((_fh > 63) && (_fh < 128)) ? (((u64_t) 1) << ((_fh - 64)&0x3f)) : (u64_t) 0) | \
		(((_fi > 63) && (_fi < 128)) ? (((u64_t) 1) << ((_fi - 64)&0x3f)) : (u64_t) 0) | \
		(((_fj > 63) && (_fj < 128)) ? (((u64_t) 1) << ((_fj - 64)&0x3f)) : (u64_t) 0) | \
		(((_fk > 63) && (_fk < 128)) ? (((u64_t) 1) << ((_fk - 64)&0x3f)) : (u64_t) 0) | \
		(((_fl > 63) && (_fl < 128)) ? (((u64_t) 1) << ((_fl - 64)&0x3f)) : (u64_t) 0)))}

#endif

/* Mask to check whether a domain is a multidomain or a single
   domain */

#define MULTI_DOMAIN_MASK 0xFF00


/*
 * The following describe the bit masks for different passive scan
 * capability/requirements per regdomain.
 */
#define	NO_PSCAN	0x0ULL
#define	PSCAN_FCC	0x0000000000000001ULL
#define	PSCAN_FCC_T	0x0000000000000002ULL
#define	PSCAN_ETSI	0x0000000000000004ULL
#define	PSCAN_MKK1	0x0000000000000008ULL
#define	PSCAN_MKK2	0x0000000000000010ULL
#define	PSCAN_MKKA	0x0000000000000020ULL
#define	PSCAN_MKKA_G	0x0000000000000040ULL
#define	PSCAN_ETSIA	0x0000000000000080ULL
#define	PSCAN_ETSIB	0x0000000000000100ULL
#define	PSCAN_ETSIC	0x0000000000000200ULL
#define	PSCAN_WWR	0x0000000000000400ULL
#define	PSCAN_MKKA1	0x0000000000000800ULL
#define	PSCAN_MKKA1_G	0x0000000000001000ULL
#define	PSCAN_MKKA2	0x0000000000002000ULL
#define	PSCAN_MKKA2_G	0x0000000000004000ULL
#define	PSCAN_MKK3	0x0000000000008000ULL
#define	PSCAN_DEFER	0x7FFFFFFFFFFFFFFFULL
#define	IS_ECM_CHAN	0x8000000000000000ULL

/*
 * THE following table is the mapping of regdomain pairs specified by
 * an 8 bit regdomain value to the individual unitary reg domains
 */

typedef struct reg_dmn_pair_mapping {
	u16_t regDmnEnum;	/* 16 bit reg domain pair */
	u16_t regDmn5GHz;	/* 5GHz reg domain */
	u16_t regDmn2GHz;	/* 2GHz reg domain */
	u32_t flags5GHz;		/* Requirements flags (AdHoc
					   disallow, noise floor cal needed,
					   etc) */
	u32_t flags2GHz;		/* Requirements flags (AdHoc
					   disallow, noise floor cal needed,
					   etc) */
	u64_t pscanMask;		/* Passive Scan flags which
					   can override unitary domain
					   passive scan flags.  This
					   value is used as a mask on
					   the unitary flags*/
	u16_t singleCC;		/* Country code of single country if
					   a one-on-one mapping exists */
}  REG_DMN_PAIR_MAPPING;

static REG_DMN_PAIR_MAPPING regDomainPairs[] = {
	{NO_ENUMRD,	    FCC2,	DEBUG_REG_DMN,  NO_REQ, NO_REQ, PSCAN_DEFER, 0 },
	{NULL1_WORLD,	NULL1,		WORLD,		NO_REQ, NO_REQ, PSCAN_DEFER, 0 },
	{NULL1_ETSIB,	NULL1,		ETSIB,		NO_REQ, NO_REQ, PSCAN_DEFER, 0 },
	{NULL1_ETSIC,	NULL1,		ETSIC,		NO_REQ, NO_REQ, PSCAN_DEFER, 0 },

	{FCC2_FCCA,     FCC2,		FCCA,		NO_REQ, NO_REQ, PSCAN_DEFER, 0 },
	{FCC2_WORLD,    FCC2,		WORLD,		NO_REQ, NO_REQ, PSCAN_DEFER, 0 },
	{FCC2_ETSIC,	FCC2,		ETSIC,		NO_REQ, NO_REQ, PSCAN_DEFER, 0 },
	{FCC3_FCCA,     FCC3,		FCCA,		NO_REQ, NO_REQ, PSCAN_DEFER, 0 },
	{FCC3_WORLD,    FCC3,		WORLD,		NO_REQ, NO_REQ, PSCAN_DEFER, 0 },
	{FCC4_FCCA,     FCC4,		FCCA,		DISALLOW_ADHOC_11A | DISALLOW_ADHOC_11A_TURB, NO_REQ, PSCAN_DEFER, 0 },
	{FCC5_FCCA,     FCC5,		FCCA,		DISALLOW_ADHOC_11A | DISALLOW_ADHOC_11A_TURB, NO_REQ, PSCAN_DEFER, 0 },
	{FCC6_FCCA,     FCC6,		FCCA,		NO_REQ, NO_REQ, PSCAN_DEFER, 0 },
	{FCC6_WORLD,    FCC6,		WORLD,		NO_REQ, NO_REQ, PSCAN_DEFER, 0 },

	{ETSI1_WORLD,	ETSI1,		WORLD,		DISALLOW_ADHOC_11A | DISALLOW_ADHOC_11A_TURB, NO_REQ, PSCAN_DEFER, 0 },
	{ETSI2_WORLD,	ETSI2,		WORLD,		DISALLOW_ADHOC_11A | DISALLOW_ADHOC_11A_TURB, NO_REQ, PSCAN_DEFER, 0 },
	{ETSI3_WORLD,	ETSI3,		WORLD,		DISALLOW_ADHOC_11A | DISALLOW_ADHOC_11A_TURB, NO_REQ, PSCAN_DEFER, 0 },
	{ETSI4_WORLD,	ETSI4,		WORLD,		DISALLOW_ADHOC_11A | DISALLOW_ADHOC_11A_TURB, NO_REQ, PSCAN_DEFER, 0 },
	{ETSI5_WORLD,	ETSI5,		WORLD,		DISALLOW_ADHOC_11A | DISALLOW_ADHOC_11A_TURB, NO_REQ, PSCAN_DEFER, 0 },
	{ETSI6_WORLD,	ETSI6,		WORLD,		DISALLOW_ADHOC_11A | DISALLOW_ADHOC_11A_TURB, NO_REQ, PSCAN_DEFER, 0 },

	{ETSI3_ETSIA,	ETSI3,		WORLD,		DISALLOW_ADHOC_11A | DISALLOW_ADHOC_11A_TURB, NO_REQ, PSCAN_DEFER, 0 },
	{FRANCE_RES,	ETSI3,		WORLD,		NO_REQ, NO_REQ, PSCAN_DEFER, 0 },

	{FCC1_WORLD,	FCC1,		WORLD,		NO_REQ, NO_REQ, PSCAN_DEFER, 0 },
	{FCC1_FCCA,     FCC1,		FCCA,		NO_REQ, NO_REQ, PSCAN_DEFER, 0 },
	{APL1_WORLD,	APL1,		WORLD,		NO_REQ, NO_REQ, PSCAN_DEFER, 0 },
	{APL2_WORLD,	APL2,		WORLD,		NO_REQ, NO_REQ, PSCAN_DEFER, 0 },
	{APL3_WORLD,	APL3,		WORLD,		NO_REQ, NO_REQ, PSCAN_DEFER, 0 },
	{APL4_WORLD,	APL4,		WORLD,		NO_REQ, NO_REQ, PSCAN_DEFER, 0 },
	{APL5_WORLD,	APL5,		WORLD,		NO_REQ, NO_REQ, PSCAN_DEFER, 0 },
	{APL6_WORLD,	APL6,		WORLD,		NO_REQ, NO_REQ, PSCAN_DEFER, 0 },
	{APL8_WORLD,	APL8,		WORLD,		NO_REQ, NO_REQ, PSCAN_DEFER, 0 },
	{APL9_WORLD,	APL9,		WORLD,		NO_REQ, NO_REQ, PSCAN_DEFER, 0 },

	{APL3_FCCA,     APL3,		FCCA,		NO_REQ, NO_REQ, PSCAN_DEFER, 0 },
	{APL1_ETSIC,	APL1,		ETSIC,		NO_REQ, NO_REQ, PSCAN_DEFER, 0 },
	{APL2_ETSIC,	APL2,		ETSIC,		NO_REQ, NO_REQ, PSCAN_DEFER, 0 },
	{APL2_FCCA,		APL2,		FCCA,		NO_REQ, NO_REQ, PSCAN_DEFER, 0 },
	{APL2_APLD,     APL2,		APLD,		NO_REQ, NO_REQ, PSCAN_DEFER, 0},
	{APL7_FCCA,		APL7,		FCCA,		NO_REQ, NO_REQ, PSCAN_DEFER, 0 },

	{MKK1_MKKA,     MKK1,		MKKA,		MKK5GHZ_FLAG1, NEED_NFC, PSCAN_MKK1 | PSCAN_MKKA, CTRY_JAPAN },
	{MKK1_MKKB,     MKK1,		MKKA,		MKK5GHZ_FLAG2, NEED_NFC, PSCAN_MKK1 | PSCAN_MKKA | PSCAN_MKKA_G, CTRY_JAPAN1 },
	{MKK1_FCCA,     MKK1,		FCCA,		MKK5GHZ_FLAG1, NEED_NFC, PSCAN_MKK1, CTRY_JAPAN2 },
	{MKK1_MKKA1,    MKK1,		MKKA,		MKK5GHZ_FLAG1, NEED_NFC, PSCAN_MKK1 | PSCAN_MKKA1 | PSCAN_MKKA1_G, CTRY_JAPAN4 },
	{MKK1_MKKA2,    MKK1,		MKKA,		MKK5GHZ_FLAG1, NEED_NFC, PSCAN_MKK1 | PSCAN_MKKA2 | PSCAN_MKKA2_G, CTRY_JAPAN5 },
	{MKK1_MKKC,     MKK1,		MKKC,		MKK5GHZ_FLAG1, NEED_NFC, PSCAN_MKK1, CTRY_JAPAN6 },

	/* MKK2 */
	{MKK2_MKKA,     MKK2,		MKKA,		MKK5GHZ_FLAG2, NEED_NFC, PSCAN_MKK2 | PSCAN_MKKA | PSCAN_MKKA_G, CTRY_JAPAN3 },

	/* MKK3 */
	{MKK3_MKKA,     MKK3,		MKKA,		MKK5GHZ_FLAG1, NEED_NFC, NO_PSCAN, CTRY_JAPAN25 },
	{MKK3_MKKB,     MKK3,		MKKA,		MKK5GHZ_FLAG2, NEED_NFC, PSCAN_MKKA | PSCAN_MKKA_G, CTRY_JAPAN7 },
	{MKK3_MKKA1,    MKK3,		MKKA,		MKK5GHZ_FLAG1, NEED_NFC, PSCAN_MKKA1 | PSCAN_MKKA1_G, CTRY_JAPAN26 },
	{MKK3_MKKA2,    MKK3,		MKKA,		MKK5GHZ_FLAG1, NEED_NFC, PSCAN_MKKA2 | PSCAN_MKKA2_G, CTRY_JAPAN8 },
	{MKK3_MKKC,     MKK3,		MKKC,		MKK5GHZ_FLAG1, NEED_NFC, NO_PSCAN, CTRY_JAPAN9 },
	{MKK3_FCCA,     MKK3,		FCCA,		MKK5GHZ_FLAG1, NEED_NFC, NO_PSCAN, CTRY_JAPAN27 },

	/* MKK4 */
	{MKK4_MKKB,     MKK4,		MKKA,		MKK5GHZ_FLAG2, NEED_NFC, PSCAN_MKK3 | PSCAN_MKKA | PSCAN_MKKA_G, CTRY_JAPAN10 },
	{MKK4_MKKA1,    MKK4,		MKKA,		MKK5GHZ_FLAG1, NEED_NFC, PSCAN_MKK3 | PSCAN_MKKA1 | PSCAN_MKKA1_G, CTRY_JAPAN28 },
	{MKK4_MKKA2,    MKK4,		MKKA,		MKK5GHZ_FLAG1, NEED_NFC, PSCAN_MKK3 | PSCAN_MKKA2 | PSCAN_MKKA2_G, CTRY_JAPAN11 },
	{MKK4_MKKC,     MKK4,		MKKC,		MKK5GHZ_FLAG1, NEED_NFC, PSCAN_MKK3, CTRY_JAPAN12 },
	{MKK4_FCCA,     MKK4,		FCCA,		MKK5GHZ_FLAG1, NEED_NFC, NO_PSCAN, CTRY_JAPAN29 },
	{MKK4_MKKA,     MKK4,		MKKA,		MKK5GHZ_FLAG1, NEED_NFC, PSCAN_MKK3 | PSCAN_MKKA, CTRY_JAPAN36 },

	/* MKK5 */
	{MKK5_MKKB,     MKK5,		MKKA,		MKK5GHZ_FLAG2, NEED_NFC, PSCAN_MKK3 | PSCAN_MKKA | PSCAN_MKKA_G, CTRY_JAPAN13 },
	{MKK5_MKKA2,    MKK5,		MKKA,		MKK5GHZ_FLAG1, NEED_NFC, PSCAN_MKK3 | PSCAN_MKKA2 | PSCAN_MKKA2_G, CTRY_JAPAN14 },
	{MKK5_MKKC,     MKK5,		MKKC,		MKK5GHZ_FLAG1, NEED_NFC, PSCAN_MKK3, CTRY_JAPAN15 },

	/* MKK6 */
	{MKK6_MKKB,     MKK6,		MKKA,		MKK5GHZ_FLAG2, NEED_NFC, PSCAN_MKK1 | PSCAN_MKKA | PSCAN_MKKA_G, CTRY_JAPAN16 },
	{MKK6_MKKA2,    MKK6,		MKKA,		MKK5GHZ_FLAG1, NEED_NFC, PSCAN_MKK1 | PSCAN_MKKA2 | PSCAN_MKKA2_G, CTRY_JAPAN17 },
	{MKK6_MKKC,     MKK6,		MKKC,		MKK5GHZ_FLAG1, NEED_NFC, PSCAN_MKK1, CTRY_JAPAN18 },
	{MKK6_MKKA1,    MKK6,		MKKA,		MKK5GHZ_FLAG1, NEED_NFC, PSCAN_MKKA1 | PSCAN_MKKA1_G, CTRY_JAPAN30 },
	{MKK6_FCCA,     MKK6,		FCCA,		MKK5GHZ_FLAG1, NEED_NFC, NO_PSCAN, CTRY_JAPAN31 },

	/* MKK7 */
	{MKK7_MKKB,     MKK7,		MKKA,		MKK5GHZ_FLAG2, NEED_NFC, PSCAN_MKK1 | PSCAN_MKK3 | PSCAN_MKKA | PSCAN_MKKA_G, CTRY_JAPAN19 },
	{MKK7_MKKA,     MKK7,		MKKA,		MKK5GHZ_FLAG1, NEED_NFC, PSCAN_MKK1 | PSCAN_MKK3 | PSCAN_MKKA2 | PSCAN_MKKA2_G, CTRY_JAPAN20 },
	{MKK7_MKKC,     MKK7,		MKKC,		MKK5GHZ_FLAG1, NEED_NFC, PSCAN_MKK1 | PSCAN_MKK3, CTRY_JAPAN21 },
	{MKK7_MKKA1,    MKK7,		MKKA,		MKK5GHZ_FLAG1, NEED_NFC, PSCAN_MKKA1 | PSCAN_MKKA1_G, CTRY_JAPAN32 },
	{MKK7_FCCA,     MKK7,		FCCA,		MKK5GHZ_FLAG1, NEED_NFC, NO_PSCAN, CTRY_JAPAN33 },

	/* MKK8 */
	{MKK8_MKKB,     MKK8,		MKKA,		MKK5GHZ_FLAG2, NEED_NFC, PSCAN_MKK1 | PSCAN_MKK3 | PSCAN_MKKA | PSCAN_MKKA_G, CTRY_JAPAN22 },
	{MKK8_MKKA2,    MKK8,		MKKA,		MKK5GHZ_FLAG1, NEED_NFC, PSCAN_MKK1 | PSCAN_MKK3 | PSCAN_MKKA2 | PSCAN_MKKA2_G, CTRY_JAPAN23 },
	{MKK8_MKKC,     MKK8,		MKKC,		MKK5GHZ_FLAG1, NEED_NFC, PSCAN_MKK1 | PSCAN_MKK3 , CTRY_JAPAN24 },

	/* MKK9 */
	{MKK9_MKKA,     MKK9,		MKKA,		MKK5GHZ_FLAG1, NEED_NFC, NO_PSCAN, CTRY_JAPAN34 },
	{MKK9_FCCA,     MKK9,		FCCA,		MKK5GHZ_FLAG1, NEED_NFC, NO_PSCAN, CTRY_JAPAN37 },
	{MKK9_MKKA1,    MKK9,		MKKA,		MKK5GHZ_FLAG1, NEED_NFC, PSCAN_MKKA1 | PSCAN_MKKA1_G, CTRY_JAPAN38 },
	{MKK9_MKKC,     MKK9,		MKKC,		MKK5GHZ_FLAG1, NEED_NFC, NO_PSCAN, CTRY_JAPAN39 },
	{MKK9_MKKA2,	MKK9,		MKKA,		MKK5GHZ_FLAG1, NEED_NFC, PSCAN_MKK1 | PSCAN_MKK3 | PSCAN_MKKA2 | PSCAN_MKKA2_G, CTRY_JAPAN40 },

	/* MKK10 */
	{MKK10_MKKA,	MKK10,		MKKA,		MKK5GHZ_FLAG1, NEED_NFC, NO_PSCAN, CTRY_JAPAN35 },
	{MKK10_FCCA,	MKK10,		FCCA,		MKK5GHZ_FLAG1, NEED_NFC, NO_PSCAN, CTRY_JAPAN41 },
	{MKK10_MKKA1,	MKK10,		MKKA,		MKK5GHZ_FLAG1, NEED_NFC, PSCAN_MKKA1 | PSCAN_MKKA1_G, CTRY_JAPAN42 },
	{MKK10_MKKC,	MKK10,		MKKC,		MKK5GHZ_FLAG1, NEED_NFC, NO_PSCAN, CTRY_JAPAN43 },
	{MKK10_MKKA2,	MKK10,		MKKA,		MKK5GHZ_FLAG1, NEED_NFC, PSCAN_MKK1 | PSCAN_MKK3 | PSCAN_MKKA2 | PSCAN_MKKA2_G, CTRY_JAPAN44 },

	/* MKK11 */
	{MKK11_MKKA,	MKK11,		MKKA,		MKK5GHZ_FLAG1, NEED_NFC, NO_PSCAN, CTRY_JAPAN45 },
	{MKK11_FCCA,	MKK11,		FCCA,		MKK5GHZ_FLAG1, NEED_NFC, NO_PSCAN, CTRY_JAPAN46 },
	{MKK11_MKKA1,	MKK11,		MKKA,		MKK5GHZ_FLAG1, NEED_NFC, PSCAN_MKKA1 | PSCAN_MKKA1_G, CTRY_JAPAN47 },
	{MKK11_MKKC,	MKK11,		MKKC,		MKK5GHZ_FLAG1, NEED_NFC, NO_PSCAN, CTRY_JAPAN48 },
	{MKK11_MKKA2,	MKK11,		MKKA,		MKK5GHZ_FLAG1, NEED_NFC, PSCAN_MKK1 | PSCAN_MKK3 | PSCAN_MKKA2 | PSCAN_MKKA2_G, CTRY_JAPAN49 },

	/* MKK12 */
	{MKK12_MKKA,	MKK12,		MKKA,		MKK5GHZ_FLAG1, NEED_NFC, NO_PSCAN, CTRY_JAPAN50 },
	{MKK12_FCCA,	MKK12,		FCCA,		MKK5GHZ_FLAG1, NEED_NFC, NO_PSCAN, CTRY_JAPAN51 },
	{MKK12_MKKA1,	MKK12,		MKKA,		MKK5GHZ_FLAG1, NEED_NFC, PSCAN_MKKA1 | PSCAN_MKKA1_G, CTRY_JAPAN52 },
	{MKK12_MKKC,	MKK12,		MKKC,		MKK5GHZ_FLAG1, NEED_NFC, NO_PSCAN, CTRY_JAPAN53 },
	{MKK12_MKKA2,	MKK12,		MKKA,		MKK5GHZ_FLAG1, NEED_NFC, PSCAN_MKK1 | PSCAN_MKK3 | PSCAN_MKKA2 | PSCAN_MKKA2_G, CTRY_JAPAN54 },


	/* These are super domains */
	{WOR0_WORLD,	WOR0_WORLD,	WOR0_WORLD,	NO_REQ, NO_REQ, PSCAN_DEFER, 0 },
	{WOR1_WORLD,	WOR1_WORLD,	WOR1_WORLD,	DISALLOW_ADHOC_11A | DISALLOW_ADHOC_11A_TURB, NO_REQ, PSCAN_DEFER, 0 },
	{WOR2_WORLD,	WOR2_WORLD,	WOR2_WORLD,	DISALLOW_ADHOC_11A_TURB, NO_REQ, PSCAN_DEFER, 0 },
	{WOR3_WORLD,	WOR3_WORLD,	WOR3_WORLD,	NO_REQ, NO_REQ, PSCAN_DEFER, 0 },
	{WOR4_WORLD,	WOR4_WORLD,	WOR4_WORLD,	DISALLOW_ADHOC_11A | DISALLOW_ADHOC_11A_TURB, NO_REQ, PSCAN_DEFER, 0 },
	{WOR5_ETSIC,	WOR5_ETSIC,	WOR5_ETSIC,	DISALLOW_ADHOC_11A | DISALLOW_ADHOC_11A_TURB, NO_REQ, PSCAN_DEFER, 0 },
	{WOR01_WORLD,	WOR01_WORLD,	WOR01_WORLD,	NO_REQ, NO_REQ, PSCAN_DEFER, 0 },
	{WOR02_WORLD,	WOR02_WORLD,	WOR02_WORLD,	NO_REQ, NO_REQ, PSCAN_DEFER, 0 },
	{EU1_WORLD,	    EU1_WORLD,	EU1_WORLD,	NO_REQ, NO_REQ, PSCAN_DEFER, 0 },
	{WOR9_WORLD,	WOR9_WORLD,	WOR9_WORLD,	DISALLOW_ADHOC_11A | DISALLOW_ADHOC_11A_TURB, NO_REQ, PSCAN_DEFER, 0 },
	{WORA_WORLD,	WORA_WORLD,	WORA_WORLD,	DISALLOW_ADHOC_11A | DISALLOW_ADHOC_11A_TURB, NO_REQ, PSCAN_DEFER, 0 },
};

/*
 * The following table is the master list for all different freqeuncy
 * bands with the complete matrix of all possible flags and settings
 * for each band if it is used in ANY reg domain.
 */

#define DEF_REGDMN		FCC1_FCCA
#define	DEF_DMN_5		FCC1
#define	DEF_DMN_2		FCCA
#define	COUNTRY_ERD_FLAG        0x8000
#define WORLDWIDE_ROAMING_FLAG  0x4000
#define	SUPER_DOMAIN_MASK	0x0fff
#define	COUNTRY_CODE_MASK	0x03ff
#define CF_INTERFERENCE		(CHANNEL_CW_INT | CHANNEL_RADAR_INT)
#define CHANNEL_14		(2484)	/* 802.11g operation is not permitted on channel 14 */
#define IS_11G_CH14(_ch, _cf) \
	(((_ch) == CHANNEL_14) && ((_cf) == CHANNEL_G))

#define	YES	TRUE
#define	NO	FALSE

enum {
	CTRY_DEBUG	= 0x1ff,		/* debug country code */
	CTRY_DEFAULT	= 0			/* default country code */
};

typedef struct {
	HAL_CTRY_CODE	countryCode;
	HAL_REG_DOMAIN	regDmnEnum;
	const char		*isoName;
	const char		*name;
	HAL_BOOL		allow11g;
	HAL_BOOL		allow11aTurbo;
	HAL_BOOL		allow11gTurbo;
	HAL_BOOL	allow11na;      /* HT-40 allowed in 5GHz? */
	HAL_BOOL	allow11ng;      /* HT-40 allowed in 2GHz? */
	u16_t		outdoorChanStart;
} COUNTRY_CODE_TO_ENUM_RD;

static COUNTRY_CODE_TO_ENUM_RD allCountries[] = {
	{CTRY_DEBUG,       NO_ENUMRD,     "DB", "DEBUG",          YES, YES, YES, YES, YES, 7000 },
	{CTRY_DEFAULT,     DEF_REGDMN,    "NA", "NO_COUNTRY_SET", YES, YES, YES, YES, YES, 7000 },
	{CTRY_ALBANIA,     NULL1_WORLD,   "AL", "ALBANIA",        YES, NO,  YES, NO,  YES, 7000 },
	{CTRY_ALGERIA,     NULL1_WORLD,   "DZ", "ALGERIA",        YES, NO,  YES, NO,  YES, 7000 },
	{CTRY_ARGENTINA,   APL3_WORLD,    "AR", "ARGENTINA",      YES, NO,  NO,  NO,  NO,  7000 },
	{CTRY_ARMENIA,     ETSI4_WORLD,   "AM", "ARMENIA",        YES, NO,  YES, NO,  YES, 7000 },
	{CTRY_AUSTRALIA,   FCC6_WORLD,    "AU", "AUSTRALIA",      YES, YES, YES, YES, YES, 7000 },
	{CTRY_AUSTRIA,     ETSI2_WORLD,   "AT", "AUSTRIA",        YES, NO,  YES, YES, YES, 7000 },
	{CTRY_AZERBAIJAN,  ETSI4_WORLD,   "AZ", "AZERBAIJAN",     YES, YES, YES, YES, YES, 7000 },
	{CTRY_BAHRAIN,     APL6_WORLD,    "BH", "BAHRAIN",        YES, NO,  YES, NO,  YES, 7000 },
	{CTRY_BELARUS,     ETSI1_WORLD,   "BY", "BELARUS",        YES, NO,  YES, YES, YES, 7000 },
	{CTRY_BELGIUM,     ETSI1_WORLD,   "BE", "BELGIUM",        YES, NO,  YES, YES, YES, 7000 },
	{CTRY_BELIZE,      APL1_ETSIC,    "BZ", "BELIZE",         YES, YES, YES, YES, YES, 7000 },
	{CTRY_BOLIVIA,     APL1_ETSIC,    "BO", "BOLVIA",         YES, YES, YES, YES, YES, 7000 },
	{CTRY_BRAZIL,      FCC3_WORLD,    "BR", "BRAZIL",         NO,  NO,  NO,  NO,  NO,  7000 },
	{CTRY_BRUNEI_DARUSSALAM, APL1_WORLD, "BN", "BRUNEI DARUSSALAM", YES, YES, YES,  YES, YES, 7000 },
	{CTRY_BULGARIA,    ETSI6_WORLD,   "BG", "BULGARIA",       YES, NO,  YES, YES, YES, 7000 },
	{CTRY_CANADA,      FCC6_FCCA,     "CA", "CANADA",         YES, YES, YES, YES, YES, 7000 },
	{CTRY_CHILE,       APL6_WORLD,    "CL", "CHILE",          YES, YES, YES, YES, YES, 7000 },
	{CTRY_CHINA,       APL1_WORLD,    "CN", "CHINA",          YES, YES, YES, YES, YES, 7000 },
	{CTRY_COLOMBIA,    FCC1_FCCA,     "CO", "COLOMBIA",       YES, NO,  YES, NO,  YES, 7000 },
	{CTRY_COSTA_RICA,  FCC1_WORLD,    "CR", "COSTA RICA",     YES, NO,  YES, NO,  YES, 7000 },
	{CTRY_CROATIA,     ETSI3_WORLD,   "HR", "CROATIA",        YES, NO,  YES, NO,  YES, 7000 },
	{CTRY_CYPRUS,      ETSI3_WORLD,   "CY", "CYPRUS",         YES, YES, YES, YES, YES, 7000 },
	{CTRY_CZECH,       ETSI3_WORLD,   "CZ", "CZECH REPUBLIC", YES, NO, YES,  YES, YES, 7000 },
	{CTRY_DENMARK,     ETSI1_WORLD,   "DK", "DENMARK",        YES, NO,  YES, YES, YES, 7000 },
	{CTRY_DOMINICAN_REPUBLIC, FCC1_FCCA, "DO", "DOMINICAN REPUBLIC", YES, YES, YES, YES, YES, 7000 },
	{CTRY_ECUADOR,     FCC1_WORLD,    "EC", "ECUADOR",        YES, NO,  NO,  NO,  YES, 7000 },
	{CTRY_EGYPT,       ETSI3_WORLD,   "EG", "EGYPT",          YES, NO,  YES, NO,  YES, 7000 },
	{CTRY_EL_SALVADOR, FCC1_WORLD,    "SV", "EL SALVADOR",    YES, NO,  YES, NO,  YES, 7000 },
	{CTRY_ESTONIA,     ETSI1_WORLD,   "EE", "ESTONIA",        YES, NO,  YES, YES, YES, 7000 },
	{CTRY_FINLAND,     ETSI1_WORLD,   "FI", "FINLAND",        YES, NO,  YES, YES, YES, 7000 },
	{CTRY_FRANCE,      ETSI1_WORLD,   "FR", "FRANCE",         YES, NO,  YES, YES, YES, 7000 },
	{CTRY_FRANCE2,     ETSI3_WORLD,   "F2", "FRANCE_RES",     YES, NO,  YES, YES, YES, 7000 },
	{CTRY_GEORGIA,     ETSI4_WORLD,   "GE", "GEORGIA",        YES, YES, YES, YES, YES, 7000 },
	{CTRY_GERMANY,     ETSI1_WORLD,   "DE", "GERMANY",        YES, NO,  YES, YES, YES, 7000 },
	{CTRY_GREECE,      ETSI1_WORLD,   "GR", "GREECE",         YES, NO,  YES, YES, YES, 7000 },
	{CTRY_GUATEMALA,   FCC1_FCCA,     "GT", "GUATEMALA",      YES, YES, YES, YES, YES, 7000 },
	{CTRY_HONDURAS,    NULL1_WORLD,   "HN", "HONDURAS",       YES, NO,  YES, NO,  YES, 7000 },
	{CTRY_HONG_KONG,   FCC2_WORLD,    "HK", "HONG KONG",      YES, YES, YES, YES, YES, 7000 },
	{CTRY_HUNGARY,     ETSI4_WORLD,   "HU", "HUNGARY",        YES, NO,  YES, YES, YES, 7000 },
	{CTRY_ICELAND,     ETSI1_WORLD,   "IS", "ICELAND",        YES, NO,  YES, YES, YES, 7000 },
	{CTRY_INDIA,       APL6_WORLD,    "IN", "INDIA",          YES, NO,  YES, NO,  YES, 7000 },
	{CTRY_INDONESIA,   APL1_WORLD,    "ID", "INDONESIA",      YES, NO,  YES, NO,  YES, 7000 },
	{CTRY_IRAN,        APL1_WORLD,    "IR", "IRAN",           YES, YES, YES, YES, YES, 7000 },
	{CTRY_IRELAND,     ETSI1_WORLD,   "IE", "IRELAND",        YES, NO,  YES, YES, YES, 7000 },
	{CTRY_ISRAEL,      ETSI3_WORLD,   "IL", "ISRAEL",         YES, NO,  YES, NO,  YES, 7000 },
	{CTRY_ISRAEL2,     NULL1_ETSIB,   "ISR", "ISRAEL_RES",     YES, NO,  YES, NO,  YES, 7000 },
	{CTRY_ITALY,       ETSI1_WORLD,   "IT", "ITALY",          YES, NO,  YES, YES, YES, 7000 },
	{CTRY_JAMAICA,     ETSI1_WORLD,   "JM", "JAMAICA",        YES, NO,  YES, YES, YES, 7000 },
	{CTRY_JAPAN,       MKK1_MKKA,     "JP", "JAPAN",          YES, NO,  NO,  NO,  NO,  7000 },
	{CTRY_JAPAN1,      MKK1_MKKB,     "J1", "JAPAN1",         YES, NO,  NO,  NO,  NO,  7000 },
	{CTRY_JAPAN2,      MKK1_FCCA,     "J2", "JAPAN2",         YES, NO,  NO,  NO,  NO,  7000 },
	{CTRY_JAPAN3,      MKK2_MKKA,     "J3", "JAPAN3",         YES, NO,  NO,  NO,  NO,  7000 },
	{CTRY_JAPAN4,      MKK1_MKKA1,    "J4", "JAPAN4",         YES, NO,  NO,  NO,  NO,  7000 },
	{CTRY_JAPAN5,      MKK1_MKKA2,    "J5", "JAPAN5",         YES, NO,  NO,  NO,  NO,  7000 },
	{CTRY_JAPAN6,      MKK1_MKKC,     "J6", "JAPAN6",         YES, NO,  NO,  NO,  NO,  7000 },
	{CTRY_JAPAN7,      MKK3_MKKB,     "J7", "JAPAN7",         YES, NO,  NO,  NO,  NO,  7000 },
	{CTRY_JAPAN8,      MKK3_MKKA2,    "J8", "JAPAN8",         YES, NO,  NO,  NO,  NO,  7000 },
	{CTRY_JAPAN9,      MKK3_MKKC,     "J9", "JAPAN9",         YES, NO,  NO,  NO,  NO,  7000 },
	{CTRY_JAPAN10,     MKK4_MKKB,     "J10", "JAPAN10",       YES, NO,  NO,  NO,  NO,  7000 },
	{CTRY_JAPAN11,     MKK4_MKKA2,    "J11", "JAPAN11",       YES, NO,  NO,  NO,  NO,  7000 },
	{CTRY_JAPAN12,     MKK4_MKKC,     "J12", "JAPAN12",       YES, NO,  NO,  NO,  NO,  7000 },
	{CTRY_JAPAN13,     MKK5_MKKB,     "J13", "JAPAN13",       YES, NO,  NO,  NO,  NO,  7000 },
	{CTRY_JAPAN14,     MKK5_MKKA2,    "J14", "JAPAN14",       YES, NO,  NO,  NO,  NO,  7000 },
	{CTRY_JAPAN15,     MKK5_MKKC,     "J15", "JAPAN15",       YES, NO,  NO,  NO,  NO,  7000 },
	{CTRY_JAPAN16,     MKK6_MKKB,     "J16", "JAPAN16",       YES, NO,  NO,  NO,  NO,  7000 },
	{CTRY_JAPAN17,     MKK6_MKKA2,    "J17", "JAPAN17",       YES, NO,  NO,  NO,  NO,  7000 },
	{CTRY_JAPAN18,     MKK6_MKKC,     "J18", "JAPAN18",       YES, NO,  NO,  NO,  NO,  7000 },
	{CTRY_JAPAN19,     MKK7_MKKB,     "J19", "JAPAN19",       YES, NO,  NO,  NO,  NO,  7000 },
	{CTRY_JAPAN20,     MKK7_MKKA,     "J20", "JAPAN20",       YES, NO,  NO,  NO,  NO,  7000 },
	{CTRY_JAPAN21,     MKK7_MKKC,     "J21", "JAPAN21",       YES, NO,  NO,  NO,  NO,  7000 },
	{CTRY_JAPAN22,     MKK8_MKKB,     "J22", "JAPAN22",       YES, NO,  NO,  NO,  NO,  7000 },
	{CTRY_JAPAN23,     MKK8_MKKA2,    "J23", "JAPAN23",       YES, NO,  NO,  NO,  NO,  7000 },
	{CTRY_JAPAN24,     MKK8_MKKC,     "J24", "JAPAN24",       YES, NO,  NO,  NO,  NO,  7000 },
	{CTRY_JAPAN25,     MKK3_MKKA,     "J25", "JAPAN25",       YES, NO,  NO,  NO,  NO,  7000 },
	{CTRY_JAPAN26,     MKK3_MKKA1,    "J26", "JAPAN26",       YES, NO,  NO,  NO,  NO,  7000 },
	{CTRY_JAPAN27,     MKK3_FCCA,     "J27", "JAPAN27",       YES, NO,  NO,  NO,  NO,  7000 },
	{CTRY_JAPAN28,     MKK4_MKKA1,    "J28", "JAPAN28",       YES, NO,  NO,  NO,  NO,  7000 },
	{CTRY_JAPAN29,     MKK4_FCCA,     "J29", "JAPAN29",       YES, NO,  NO,  NO,  NO,  7000 },
	{CTRY_JAPAN30,     MKK6_MKKA1,    "J30", "JAPAN30",       YES, NO,  NO,  NO,  NO,  7000 },
	{CTRY_JAPAN31,     MKK6_FCCA,     "J31", "JAPAN31",       YES, NO,  NO,  NO,  NO,  7000 },
	{CTRY_JAPAN32,     MKK7_MKKA1,    "J32", "JAPAN32",       YES, NO,  NO,  NO,  NO,  7000 },
	{CTRY_JAPAN33,     MKK7_FCCA,     "J33", "JAPAN33",       YES, NO,  NO,  NO,  NO,  7000 },
	{CTRY_JAPAN34,     MKK9_MKKA,     "J34", "JAPAN34",       YES, NO,  NO,  NO,  NO,  7000 },
	{CTRY_JAPAN35,     MKK10_MKKA,    "J35", "JAPAN35",       YES, NO,  NO,  NO,  NO,  7000 },
	{CTRY_JAPAN36,     MKK4_MKKA,     "J36", "JAPAN36",       YES, NO,  NO,  NO,  NO,  7000 },
	{CTRY_JAPAN37,     MKK9_FCCA,     "J37", "JAPAN37",       YES, NO,  NO,  NO,  NO,  7000 },
	{CTRY_JAPAN38,     MKK9_MKKA1,    "J38", "JAPAN38",       YES, NO,  NO,  NO,  NO,  7000 },
	{CTRY_JAPAN39,     MKK9_MKKC,     "J39", "JAPAN39",       YES, NO,  NO,  NO,  NO,  7000 },
	{CTRY_JAPAN40,     MKK10_MKKA2,   "J40", "JAPAN40",       YES, NO,  NO,  NO,  NO,  7000 },
	{CTRY_JAPAN41,     MKK10_FCCA,    "J41", "JAPAN41",       YES, NO,  NO,  NO,  NO,  7000 },
	{CTRY_JAPAN42,     MKK10_MKKA1,   "J42", "JAPAN42",       YES, NO,  NO,  NO,  NO,  7000 },
	{CTRY_JAPAN43,     MKK10_MKKC,    "J43", "JAPAN43",       YES, NO,  NO,  NO,  NO,  7000 },
	{CTRY_JAPAN44,     MKK10_MKKA2,   "J44", "JAPAN44",       YES, NO,  NO,  NO,  NO,  7000 },
	{CTRY_JAPAN45,     MKK11_MKKA,    "J45", "JAPAN45",       YES, NO,  NO,  NO,  NO,  7000 },
	{CTRY_JAPAN46,     MKK11_FCCA,    "J46", "JAPAN46",       YES, NO,  NO,  NO,  NO,  7000 },
	{CTRY_JAPAN47,     MKK11_MKKA1,   "J47", "JAPAN47",       YES, NO,  NO,  NO,  NO,  7000 },
	{CTRY_JAPAN48,     MKK11_MKKC,    "J48", "JAPAN48",       YES, NO,  NO,  NO,  NO,  7000 },
	{CTRY_JAPAN49,     MKK11_MKKA2,   "J49", "JAPAN49",       YES, NO,  NO,  NO,  NO,  7000 },
	{CTRY_JAPAN50,     MKK12_MKKA,    "J50", "JAPAN50",       YES, NO,  NO,  NO,  NO,  7000 },
	{CTRY_JAPAN51,     MKK12_FCCA,    "J51", "JAPAN51",       YES, NO,  NO,  NO,  NO,  7000 },
	{CTRY_JAPAN52,     MKK12_MKKA1,   "J52", "JAPAN52",       YES, NO,  NO,  NO,  NO,  7000 },
	{CTRY_JAPAN53,     MKK12_MKKC,    "J53", "JAPAN53",       YES, NO,  NO,  NO,  NO,  7000 },
	{CTRY_JAPAN54,     MKK12_MKKA2,   "J54", "JAPAN54",       YES, NO,  NO,  NO,  NO,  7000 },
	{CTRY_JORDAN,      ETSI2_WORLD,   "JO", "JORDAN",         YES, NO,  YES, NO,  YES, 7000 },
	{CTRY_KAZAKHSTAN,  NULL1_WORLD,   "KZ", "KAZAKHSTAN",     YES, NO,  YES, NO,  YES, 7000 },
	{CTRY_KOREA_NORTH, APL9_WORLD,    "KP", "NORTH KOREA",    YES, NO,  NO,  YES, YES, 7000 },
	{CTRY_KOREA_ROC,   APL9_WORLD,    "KR", "KOREA REPUBLIC", YES, NO,  NO,  NO,  NO,  7000 },
	{CTRY_KOREA_ROC2,  APL2_APLD,     "K2", "KOREA REPUBLIC2", YES, NO,  NO,  NO,  NO,  7000 },
	{CTRY_KOREA_ROC3,  APL9_WORLD,    "K3", "KOREA REPUBLIC3", YES, NO,  NO,  NO,  NO,  7000 },
	{CTRY_KUWAIT,      NULL1_WORLD,   "KW", "KUWAIT",         YES, NO,  YES, NO,  YES, 7000 },
	{CTRY_LATVIA,      ETSI1_WORLD,   "LV", "LATVIA",         YES, NO,  YES, YES, YES, 7000 },
	{CTRY_LEBANON,     NULL1_WORLD,   "LB", "LEBANON",        YES, NO,  YES, NO,  YES, 7000 },
	{CTRY_LIECHTENSTEIN, ETSI1_WORLD,  "LI", "LIECHTENSTEIN",  YES, NO,  YES, YES, YES, 7000 },
	{CTRY_LITHUANIA,   ETSI1_WORLD,   "LT", "LITHUANIA",      YES, NO,  YES, YES, YES, 7000 },
	{CTRY_LUXEMBOURG,  ETSI1_WORLD,   "LU", "LUXEMBOURG",     YES, NO,  YES, YES, YES, 7000 },
	{CTRY_MACAU,       FCC2_WORLD,    "MO", "MACAU",          YES, YES, YES, YES, YES, 7000 },
	{CTRY_MACEDONIA,   NULL1_WORLD,   "MK", "MACEDONIA",      YES, NO,  YES, NO,  YES, 7000 },
	{CTRY_MALAYSIA,    APL8_WORLD,    "MY", "MALAYSIA",       NO,  NO,  NO,  NO,  NO,  7000 },
	{CTRY_MALTA,       ETSI1_WORLD,   "MT", "MALTA",          YES, NO,  YES, YES, YES, 7000 },
	{CTRY_MEXICO,      FCC1_FCCA,     "MX", "MEXICO",         YES, YES, YES, YES, YES, 7000 },
	{CTRY_MONACO,      ETSI4_WORLD,   "MC", "MONACO",         YES, YES, YES, YES, YES, 7000 },
	{CTRY_MOROCCO,     NULL1_WORLD,   "MA", "MOROCCO",        YES, NO,  YES, NO,  YES, 7000 },
	{CTRY_NETHERLANDS, ETSI1_WORLD,   "NL", "NETHERLANDS",    YES, NO,  YES, YES, YES, 7000 },
	{CTRY_NETHERLANDS_ANT, ETSI1_WORLD, "AN", "NETHERLANDS-ANTILLES", YES, NO,  YES, YES, YES, 7000 },
	{CTRY_NEW_ZEALAND, FCC2_ETSIC,    "NZ", "NEW ZEALAND",    YES, NO,  YES, NO,  YES, 7000 },
	{CTRY_NORWAY,      ETSI1_WORLD,   "NO", "NORWAY",         YES, NO,  YES, YES, YES, 7000 },
	{CTRY_OMAN,        APL6_WORLD,    "OM", "OMAN",           YES, NO,  YES, NO,  YES, 7000 },
	{CTRY_PAKISTAN,    NULL1_WORLD,   "PK", "PAKISTAN",       YES, NO,  YES, NO,  YES, 7000 },
	{CTRY_PANAMA,      FCC1_FCCA,     "PA", "PANAMA",         YES, YES, YES, YES, YES, 7000 },
	{CTRY_PERU,        APL1_WORLD,    "PE", "PERU",           YES, NO,  YES, NO,  YES, 7000 },
	{CTRY_PHILIPPINES, APL1_WORLD,    "PH", "PHILIPPINES",    YES, YES, YES, YES, YES, 7000 },
	{CTRY_POLAND,      ETSI1_WORLD,   "PL", "POLAND",         YES, NO,  YES, YES, YES, 7000 },
	{CTRY_PORTUGAL,    ETSI1_WORLD,   "PT", "PORTUGAL",       YES, NO,  YES, YES, YES, 7000 },
	{CTRY_PUERTO_RICO, FCC1_FCCA,     "PR", "PUERTO RICO",    YES, YES, YES, YES, YES, 7000 },
	{CTRY_QATAR,       NULL1_WORLD,   "QA", "QATAR",          YES, NO,  YES, NO,  YES, 7000 },
	{CTRY_ROMANIA,     NULL1_WORLD,   "RO", "ROMANIA",        YES, NO,  YES, NO,  YES, 7000 },
	{CTRY_RUSSIA,      NULL1_WORLD,   "RU", "RUSSIA",         YES, NO,  YES, NO,  YES, 7000 },
	{CTRY_SAUDI_ARABIA, NULL1_WORLD,  "SA", "SAUDI ARABIA",   YES, NO,  YES, NO,  YES, 7000 },
	{CTRY_SERBIA_MONT, ETSI1_WORLD,   "CS", "SERBIA & MONTENEGRO", YES, NO,  YES, YES,  YES, 7000 },
	{CTRY_SINGAPORE,   APL6_WORLD,    "SG", "SINGAPORE",      YES, YES, YES, YES, YES, 7000 },
	{CTRY_SLOVAKIA,    ETSI1_WORLD,   "SK", "SLOVAK REPUBLIC", YES, NO,  YES, YES, YES, 7000 },
	{CTRY_SLOVENIA,    ETSI1_WORLD,   "SI", "SLOVENIA",       YES, NO,  YES, YES, YES, 7000 },
	{CTRY_SOUTH_AFRICA, FCC3_WORLD,   "ZA", "SOUTH AFRICA",   YES, NO,  YES, NO,  YES, 7000 },
	{CTRY_SPAIN,       ETSI1_WORLD,   "ES", "SPAIN",          YES, NO,  YES, YES, YES, 7000 },
	{CTRY_SRILANKA,    FCC3_WORLD,    "LK", "SRI LANKA",      YES, NO,  YES, NO,  YES, 7000 },
	{CTRY_SWEDEN,      ETSI1_WORLD,   "SE", "SWEDEN",         YES, NO,  YES, YES, YES, 7000 },
	{CTRY_SWITZERLAND, ETSI1_WORLD,   "CH", "SWITZERLAND",    YES, NO,  YES, YES, YES, 7000 },
	{CTRY_SYRIA,       NULL1_WORLD,   "SY", "SYRIA",          YES, NO,  YES, NO,  YES, 7000 },
	{CTRY_TAIWAN,      APL3_FCCA,     "TW", "TAIWAN",         YES, YES, YES, YES, YES, 7000 },
	{CTRY_THAILAND,    NULL1_WORLD,   "TH", "THAILAND",       YES, NO,  YES, NO,  YES, 7000 },
	{CTRY_TRINIDAD_Y_TOBAGO, ETSI4_WORLD, "TT", "TRINIDAD & TOBAGO", YES, NO, YES, NO, YES, 7000 },
	{CTRY_TUNISIA,     ETSI3_WORLD,   "TN", "TUNISIA",        YES, NO,  YES, NO,  YES, 7000 },
	{CTRY_TURKEY,      ETSI3_WORLD,   "TR", "TURKEY",         YES, NO,  YES, NO,  YES, 7000 },
	{CTRY_UKRAINE,     NULL1_WORLD,   "UA", "UKRAINE",        YES, NO,  YES, NO,  YES, 7000 },
	{CTRY_UAE,         NULL1_WORLD,   "AE", "UNITED ARAB EMIRATES", YES, NO, YES, NO, YES, 7000 },
	{CTRY_UNITED_KINGDOM, ETSI1_WORLD, "GB", "UNITED KINGDOM", YES, NO,  YES, NO,  YES, 7000 },
	{CTRY_UNITED_STATES, FCC3_FCCA,   "US", "UNITED STATES",  YES, YES, YES, YES, YES, 5825 },
	{CTRY_UNITED_STATES_FCC49, FCC4_FCCA,   "PS", "UNITED STATES (PUBLIC SAFETY)",  YES, YES, YES, YES, YES, 7000 },
	{CTRY_URUGUAY,     FCC1_WORLD,    "UY", "URUGUAY",        YES, NO,  YES, NO,  YES, 7000 },
	{CTRY_UZBEKISTAN,  FCC3_FCCA,     "UZ", "UZBEKISTAN",     YES, YES, YES, YES, YES, 7000 },
	{CTRY_VENEZUELA,   APL2_ETSIC,    "VE", "VENEZUELA",      YES, NO,  YES, NO,  YES, 7000 },
	{CTRY_VIET_NAM,    NULL1_WORLD,   "VN", "VIET NAM",       YES, NO,  YES, NO,  YES, 7000 },
	{CTRY_YEMEN,       NULL1_WORLD,   "YE", "YEMEN",          YES, NO,  YES, NO,  YES, 7000 },
	{CTRY_ZIMBABWE,    NULL1_WORLD,   "ZW", "ZIMBABWE",       YES, NO,  YES, NO,  YES, 7000 }
};

typedef struct RegDmnFreqBand {
	u16_t	lowChannel;	/* Low channel center in MHz */
	u16_t	highChannel;	/* High Channel center in MHz */
	u8_t	powerDfs;	/* Max power (dBm) for channel
					   range when using DFS */
	u8_t	antennaMax;	/* Max allowed antenna gain */
	u8_t	channelBW;	/* Bandwidth of the channel */
	u8_t	channelSep;	/* Channel separation within
					   the band */
	u64_t	useDfs;		/* Use DFS in the RegDomain
					   if corresponding bit is set */
	u64_t	usePassScan;	/* Use Passive Scan in the RegDomain
					   if corresponding bit is set */
	u8_t	regClassId;	/* Regulatory class id */
	u8_t	useExtChanDfs;	/* Regulatory class id */
} REG_DMN_FREQ_BAND;

/* Bit masks for DFS per regdomain */

enum {
	NO_DFS   = 0x0000000000000000ULL,
	DFS_FCC3 = 0x0000000000000001ULL,
	DFS_ETSI = 0x0000000000000002ULL,
	DFS_MKK4 = 0x0000000000000004ULL,
};

/* The table of frequency bands is indexed by a bitmask.  The ordering
 * must be consistent with the enum below.  When adding a new
 * frequency band, be sure to match the location in the enum with the
 * comments
 */

/*
 * 5GHz 11A channel tags
 */

enum {
	F1_4915_4925,
	F1_4935_4945,
	F1_4920_4980,
	F1_4942_4987,
	F1_4945_4985,
	F1_4950_4980,
	F1_5035_5040,
	F1_5040_5080,
	F1_5055_5055,

	F1_5120_5240,

	F1_5170_5230,
	F2_5170_5230,

	F1_5180_5240,
	F2_5180_5240,
	F3_5180_5240,
	F4_5180_5240,
	F5_5180_5240,
	F6_5180_5240,
	F7_5180_5240,

	F1_5180_5320,

	F1_5240_5280,

	F1_5260_5280,

	F1_5260_5320,
	F2_5260_5320,
	F3_5260_5320,
	F4_5260_5320,
	F5_5260_5320,
	F6_5260_5320,
	F7_5260_5320,

	F1_5260_5700,

	F1_5280_5320,

    F1_5500_5580,

	F1_5500_5620,

	F1_5500_5700,
	F2_5500_5700,
	F3_5500_5700,
	F4_5500_5700,

    F1_5660_5700,

	F1_5745_5805,
	F2_5745_5805,
	F3_5745_5805,

	F1_5745_5825,
	F2_5745_5825,
	F3_5745_5825,
	F4_5745_5825,
	F5_5745_5825,
	F6_5745_5825,

	W1_4920_4980,
	W1_5040_5080,
	W1_5170_5230,
	W1_5180_5240,
	W1_5260_5320,
	W1_5745_5825,
	W1_5500_5700,
	W2_5260_5320,
	W2_5180_5240,
	W2_5825_5825,
};

static REG_DMN_FREQ_BAND regDmn5GhzFreq[] = {
	{ 4915, 4925, 23, 0, 10, 5, NO_DFS, PSCAN_MKK2, 16, 0 },		/* F1_4915_4925 */
	{ 4935, 4945, 23, 0, 10, 5, NO_DFS, PSCAN_MKK2, 16, 0 },		/* F1_4935_4945 */
	{ 4920, 4980, 23, 0, 20, 20, NO_DFS, PSCAN_MKK2, 7, 0 },		/* F1_4920_4980 */
	{ 4942, 4987, 27, 6, 5,  5, DFS_FCC3, PSCAN_FCC, 0, 0 },		/* F1_4942_4987 */
	{ 4945, 4985, 30, 6, 10, 5, DFS_FCC3, PSCAN_FCC, 0, 0 },		/* F1_4945_4985 */
	{ 4950, 4980, 33, 6, 20, 5, DFS_FCC3, PSCAN_FCC, 0, 0 },		/* F1_4950_4980 */
	{ 5035, 5040, 23, 0, 10, 5, NO_DFS, PSCAN_MKK2, 12, 0 },		/* F1_5035_5040 */
	{ 5040, 5080, 23, 0, 20, 20, NO_DFS, PSCAN_MKK2, 2, 0 },		/* F1_5040_5080 */
	{ 5055, 5055, 23, 0, 10, 5, NO_DFS, PSCAN_MKK2, 12, 0 },		/* F1_5055_5055 */

	{ 5120, 5240, 5,  6, 20, 20, NO_DFS, NO_PSCAN, 0, 0 },			/* F1_5120_5240 */

	{ 5170, 5230, 23, 0, 20, 20, NO_DFS, PSCAN_MKK1 | PSCAN_MKK2, 1, 0 },	/* F1_5170_5230 */
	{ 5170, 5230, 20, 0, 20, 20, NO_DFS, PSCAN_MKK1 | PSCAN_MKK2, 1, 0 },	/* F2_5170_5230 */

	{ 5180, 5240, 15, 0, 20, 20, NO_DFS, PSCAN_FCC | PSCAN_ETSI, 0, 0 },	/* F1_5180_5240 */
	{ 5180, 5240, 17, 6, 20, 20, NO_DFS, PSCAN_FCC, 1, 0 },				/* F2_5180_5240 */
	{ 5180, 5240, 18, 0, 20, 20, NO_DFS, PSCAN_FCC | PSCAN_ETSI, 0, 0 },	/* F3_5180_5240 */
	{ 5180, 5240, 20, 0, 20, 20, NO_DFS, PSCAN_FCC | PSCAN_ETSI, 0, 0 },	/* F4_5180_5240 */
	{ 5180, 5240, 23, 0, 20, 20, NO_DFS, PSCAN_FCC | PSCAN_ETSI, 0, 0 },	/* F5_5180_5240 */
	{ 5180, 5240, 23, 6, 20, 20, NO_DFS, PSCAN_FCC, 0, 0 },				/* F6_5180_5240 */
  { 5180, 5240, 23, 6, 20, 20, NO_DFS, NO_PSCAN, 0 },           /* F7_5180_5240 */

	{ 5180, 5320, 20, 6, 20, 20, DFS_ETSI, PSCAN_ETSI, 0, 0 },			/* F1_5180_5320 */

	{ 5240, 5280, 23, 0, 20, 20, DFS_FCC3, PSCAN_FCC | PSCAN_ETSI, 0, 0 },	/* F1_5240_5280 */

	{ 5260, 5280, 23, 0, 20, 20, DFS_FCC3 | DFS_ETSI, PSCAN_FCC | PSCAN_ETSI, 0, 0 },	/* F1_5260_5280 */

	{ 5260, 5320, 18, 0, 20, 20, DFS_FCC3 | DFS_ETSI, PSCAN_FCC | PSCAN_ETSI, 0, 0 },	/* F1_5260_5320 */

	{ 5260, 5320, 20, 0, 20, 20, DFS_FCC3 | DFS_ETSI | DFS_MKK4, PSCAN_FCC | PSCAN_ETSI | PSCAN_MKK3 , 0, 0 },
											/* F2_5260_5320 */

	{ 5260, 5320, 20, 6, 20, 20, DFS_FCC3 | DFS_ETSI, PSCAN_FCC, 2, 0 },	/* F3_5260_5320 */
	{ 5260, 5320, 23, 6, 20, 20, DFS_FCC3 | DFS_ETSI, PSCAN_FCC, 2, 0 },	/* F4_5260_5320 */
	{ 5260, 5320, 23, 6, 20, 20, DFS_FCC3 | DFS_ETSI, PSCAN_FCC, 0, 0 },	/* F5_5260_5320 */
	{ 5260, 5320, 30, 0, 20, 20, NO_DFS, NO_PSCAN, 0, 0 },				/* F6_5260_5320 */
	{ 5260, 5320, 17, 6, 20, 20, DFS_ETSI, PSCAN_ETSI, 0, 0 },				/* F7_5260_5320 */

	{ 5260, 5700, 5,  6, 20, 20, DFS_FCC3 | DFS_ETSI, NO_PSCAN, 0, 0 },		/* F1_5260_5700 */

	{ 5280, 5320, 17, 6, 20, 20, DFS_FCC3 | DFS_ETSI, PSCAN_FCC, 0, 0 },	/* F1_5280_5320 */

    { 5500, 5580, 23, 6, 20, 20, DFS_FCC3, PSCAN_FCC, 0},                           /* F1_5500_5580 */

	{ 5500, 5620, 30, 6, 20, 20, DFS_ETSI, PSCAN_ETSI, 0, 0 },				/* F1_5500_5620 */

	{ 5500, 5700, 20, 6, 20, 20, DFS_FCC3 | DFS_ETSI, PSCAN_FCC, 4, 0 },		/* F1_5500_5700 */
	{ 5500, 5700, 27, 0, 20, 20, DFS_FCC3 | DFS_ETSI, PSCAN_FCC | PSCAN_ETSI, 0, 0 },	/* F2_5500_5700 */
	{ 5500, 5700, 30, 0, 20, 20, DFS_FCC3 | DFS_ETSI, PSCAN_FCC | PSCAN_ETSI, 0, 0 },	/* F3_5500_5700 */
	{ 5500, 5700, 20, 0, 20, 20, DFS_FCC3 | DFS_ETSI | DFS_MKK4, PSCAN_MKK3 | PSCAN_FCC, 0, 0 },
											/* F4_5500_5700 */

    { 5660, 5700, 23, 6, 20, 20, DFS_FCC3, PSCAN_FCC, 0},                           /* F1_5660_5700 */

	{ 5745, 5805, 23, 0, 20, 20, NO_DFS, NO_PSCAN, 0, 0 },				/* F1_5745_5805 */
	{ 5745, 5805, 30, 6, 20, 20, NO_DFS, NO_PSCAN, 0, 0 },				/* F2_5745_5805 */
	{ 5745, 5805, 30, 6, 20, 20, DFS_ETSI, PSCAN_ETSI, 0, 0 },				/* F3_5745_5805 */
	{ 5745, 5825, 5,  6, 20, 20, NO_DFS, NO_PSCAN, 0, 0 },				/* F1_5745_5825 */
	{ 5745, 5825, 17, 0, 20, 20, NO_DFS, NO_PSCAN, 0, 0 },				/* F2_5745_5825 */
	{ 5745, 5825, 20, 0, 20, 20, DFS_ETSI, NO_PSCAN, 0, 0 },				/* F3_5745_5825 */
	{ 5745, 5825, 30, 0, 20, 20, NO_DFS, NO_PSCAN, 0, 0 },				/* F4_5745_5825 */
	{ 5745, 5825, 30, 6, 20, 20, NO_DFS, NO_PSCAN, 3, 0 },			/* F5_5745_5825 */
	{ 5745, 5825, 30, 6, 20, 20, NO_DFS, NO_PSCAN, 0, 0 },				/* F6_5745_5825 */

	/*
	 * Below are the world roaming channels
	 * All WWR domains have no power limit, instead use the card's CTL
	 * or max power settings.
	 */
	{ 4920, 4980, 30, 0, 20, 20, NO_DFS, PSCAN_WWR, 0, 0 },				/* W1_4920_4980 */
	{ 5040, 5080, 30, 0, 20, 20, NO_DFS, PSCAN_WWR, 0 },				/* W1_5040_5080 */
	{ 5170, 5230, 30, 0, 20, 20, DFS_FCC3 | DFS_ETSI, PSCAN_WWR, 0, 0 },		/* W1_5170_5230 */
	{ 5180, 5240, 30, 0, 20, 20, DFS_FCC3 | DFS_ETSI, PSCAN_WWR, 0, 0 },		/* W1_5180_5240 */
	{ 5260, 5320, 30, 0, 20, 20, DFS_FCC3 | DFS_ETSI, PSCAN_WWR, 0, 0 },		/* W1_5260_5320 */
	{ 5745, 5825, 30, 0, 20, 20, NO_DFS, PSCAN_WWR, 0, 0 },				/* W1_5745_5825 */
	{ 5500, 5700, 30, 0, 20, 20, DFS_FCC3 | DFS_ETSI, PSCAN_WWR, 0, 0 },		/* W1_5500_5700 */
	{ 5260, 5320, 30, 0, 20, 20, NO_DFS, NO_PSCAN,  0, 0 },				/* W2_5260_5320 */
	{ 5180, 5240, 30, 0, 20, 20, NO_DFS, NO_PSCAN,  0, 0 },				/* W2_5180_5240 */
	{ 5825, 5825, 30, 0, 20, 20, NO_DFS, PSCAN_WWR, 0, 0 },				/* W2_5825_5825 */
};
/*
 * 5GHz Turbo (dynamic & static) tags
 */

enum {
	T1_5130_5210,
	T1_5250_5330,
	T1_5370_5490,
	T1_5530_5650,

	T1_5150_5190,
	T1_5230_5310,
	T1_5350_5470,
	T1_5510_5670,

	T1_5200_5240,
	T2_5200_5240,
	T1_5210_5210,
	T2_5210_5210,

	T1_5280_5280,
	T2_5280_5280,
	T1_5250_5250,
	T1_5290_5290,
	T1_5250_5290,
	T2_5250_5290,

	T1_5540_5660,
	T1_5760_5800,
	T2_5760_5800,

	T1_5765_5805,

	WT1_5210_5250,
	WT1_5290_5290,
	WT1_5540_5660,
	WT1_5760_5800,
};

/*
 * 2GHz 11b channel tags
 */
enum {
	F1_2312_2372,
	F2_2312_2372,

	F1_2412_2472,
	F2_2412_2472,
	F3_2412_2472,

	F1_2412_2462,
	F2_2412_2462,

	F1_2432_2442,

	F1_2457_2472,

	F1_2467_2472,

	F1_2484_2484,
	F2_2484_2484,

	F1_2512_2732,

	W1_2312_2372,
	W1_2412_2412,
	W1_2417_2432,
	W1_2437_2442,
	W1_2447_2457,
	W1_2462_2462,
	W1_2467_2467,
	W2_2467_2467,
	W1_2472_2472,
	W2_2472_2472,
	W1_2484_2484,
	W2_2484_2484,
};


/*
 * 2GHz 11g channel tags
 */

enum {
	G1_2312_2372,
	G2_2312_2372,

	G1_2412_2472,
	G2_2412_2472,
	G3_2412_2472,

	G1_2412_2462,
	G2_2412_2462,

	G1_2432_2442,

	G1_2457_2472,

	G1_2512_2732,

	G1_2467_2472 ,

	WG1_2312_2372,
	WG1_2412_2412,
	WG1_2417_2432,
	WG1_2437_2442,
	WG1_2447_2457,
	WG1_2462_2462,
	WG1_2467_2467,
	WG2_2467_2467,
	WG1_2472_2472,
	WG2_2472_2472,

};
static REG_DMN_FREQ_BAND regDmn2Ghz11gFreq[] = {
	{ 2312, 2372, 5,  6, 20, 5, NO_DFS, NO_PSCAN, 0, 0},	/* G1_2312_2372 */
	{ 2312, 2372, 20, 0, 20, 5, NO_DFS, NO_PSCAN, 0, 0},	/* G2_2312_2372 */

	{ 2412, 2472, 5,  6, 20, 5, NO_DFS, NO_PSCAN, 0, 0},	/* G1_2412_2472 */
	{ 2412, 2472, 20, 0, 20, 5,  NO_DFS, PSCAN_MKKA_G, 0, 0},	/* G2_2412_2472 */
	{ 2412, 2472, 30, 0, 20, 5, NO_DFS, NO_PSCAN, 0, 0},	/* G3_2412_2472 */

	{ 2412, 2462, 27, 6, 20, 5, NO_DFS, NO_PSCAN, 0, 0},	/* G1_2412_2462 */
	{ 2412, 2462, 20, 0, 20, 5, NO_DFS, PSCAN_MKKA_G, 0, 0},	/* G2_2412_2462 */
	{ 2432, 2442, 20, 0, 20, 5, NO_DFS, NO_PSCAN, 0, 0},	/* G1_2432_2442 */

	{ 2457, 2472, 20, 0, 20, 5, NO_DFS, NO_PSCAN, 0, 0},	/* G1_2457_2472 */

	{ 2512, 2732, 5,  6, 20, 5, NO_DFS, NO_PSCAN, 0, 0},	/* G1_2512_2732 */

	{ 2467, 2472, 20, 0, 20, 5, NO_DFS, PSCAN_MKKA2 | PSCAN_MKKA, 0, 0 }, /* G1_2467_2472 */

	/*
	 * WWR open up the power to 20dBm
	 */

	{ 2312, 2372, 20, 0, 20, 5, NO_DFS, NO_PSCAN, 0, 0},	/* WG1_2312_2372 */
	{ 2412, 2412, 20, 0, 20, 5, NO_DFS, NO_PSCAN, 0, 0},	/* WG1_2412_2412 */
	{ 2417, 2432, 20, 0, 20, 5, NO_DFS, NO_PSCAN, 0, 0},	/* WG1_2417_2432 */
	{ 2437, 2442, 20, 0, 20, 5, NO_DFS, NO_PSCAN, 0, 0},	/* WG1_2437_2442 */
	{ 2447, 2457, 20, 0, 20, 5, NO_DFS, NO_PSCAN, 0, 0},	/* WG1_2447_2457 */
	{ 2462, 2462, 20, 0, 20, 5, NO_DFS, NO_PSCAN, 0, 0},	/* WG1_2462_2462 */
	{ 2467, 2467, 20, 0, 20, 5, NO_DFS, PSCAN_WWR | IS_ECM_CHAN, 0, 0}, /* WG1_2467_2467 */
	{ 2467, 2467, 20, 0, 20, 5, NO_DFS, NO_PSCAN | IS_ECM_CHAN, 0, 0},	/* WG2_2467_2467 */
	{ 2472, 2472, 20, 0, 20, 5, NO_DFS, PSCAN_WWR | IS_ECM_CHAN, 0, 0}, /* WG1_2472_2472 */
	{ 2472, 2472, 20, 0, 20, 5, NO_DFS, NO_PSCAN | IS_ECM_CHAN, 0, 0},	/* WG2_2472_2472 */
};
/*
 * 2GHz Dynamic turbo tags
 */

enum {
	T1_2312_2372,
	T1_2437_2437,
	T2_2437_2437,
	T3_2437_2437,
	T1_2512_2732
};

/*
 * 2GHz 11n frequency tags
 */
enum {
    NG1_2422_2452,
    NG2_2422_2452,
    NG3_2422_2452,

    NG_DEMO_ALL_CHANNELS,
};

/*
 * 5GHz 11n frequency tags
 */
enum {
    NA1_5190_5230,
    NA2_5190_5230,
    NA3_5190_5230,
    NA4_5190_5230,
    NA5_5190_5230,

    NA1_5270_5270,

    NA1_5270_5310,
    NA2_5270_5310,
    NA3_5270_5310,
    NA4_5270_5310,

    NA1_5310_5310,

    NA1_5510_5630,

    NA1_5510_5670,
    NA2_5510_5670,
    NA3_5510_5670,

    NA1_5755_5795,
    NA2_5755_5795,
    NA3_5755_5795,
    NA4_5755_5795,
    NA5_5755_5795,

    NA1_5795_5795,

    NA_DEMO_ALL_CHANNELS,
};

typedef struct regDomain {
	u16_t regDmnEnum;	/* value from EnumRd table */
	u8_t conformanceTestLimit;
	u64_t dfsMask;	/* DFS bitmask for 5Ghz tables */
	u64_t pscan;	/* Bitmask for passive scan */
	u32_t flags;	/* Requirement flags (AdHoc disallow, noise
				   floor cal needed, etc) */
	u64_t chan11a[BMLEN];/* 128 bit bitmask for channel/band
				   selection */
	u64_t chan11a_turbo[BMLEN];/* 128 bit bitmask for channel/band
				   selection */
	u64_t chan11a_dyn_turbo[BMLEN]; /* 128 bit bitmask for channel/band
					       selection */
	u64_t chan11b[BMLEN];/* 128 bit bitmask for channel/band
				   selection */
	u64_t chan11g[BMLEN];/* 128 bit bitmask for channel/band
				   selection */
	u64_t chan11g_turbo[BMLEN];/* 128 bit bitmask for channel/band
					  selection */
	u64_t chan11ng[BMLEN];/* 128 bit bitmask for 11n in 2GHz */
	u64_t chan11na[BMLEN];/* 128 bit bitmask for 11n in 5GHz */
} REG_DOMAIN;

static REG_DOMAIN regDomains[] = {

	{DEBUG_REG_DMN, FCC, NO_DFS, NO_PSCAN, NO_REQ,
	 BM(F1_5120_5240, F1_5260_5700, F1_5745_5825, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BM(T1_5130_5210, T1_5250_5330, T1_5370_5490, T1_5530_5650, T1_5150_5190, T1_5230_5310, T1_5350_5470, T1_5510_5670, -1, -1, -1, -1),
	 BM(T1_5200_5240, T1_5280_5280, T1_5540_5660, T1_5765_5805, -1, -1, -1, -1, -1, -1, -1, -1),
	 BM(F1_2312_2372, F1_2412_2472, F1_2484_2484, F1_2512_2732, -1, -1, -1, -1, -1, -1, -1, -1),
	 BM(G1_2312_2372, G1_2412_2472, G1_2512_2732, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BM(T1_2312_2372, T1_2437_2437, T1_2512_2732, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BM(NG_DEMO_ALL_CHANNELS, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BM(NA_DEMO_ALL_CHANNELS, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1)},

	{APL1, ETSI, NO_DFS, NO_PSCAN, NO_REQ,
	 BM(F4_5745_5825, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BM(NA4_5755_5795, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1)},

	{APL2, ETSI, NO_DFS, NO_PSCAN, NO_REQ,
	 BM(F1_5745_5805, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BM(NA3_5755_5795, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1)},

	{APL3, FCC, NO_DFS, NO_PSCAN, NO_REQ,
	 BM(F1_5280_5320, F2_5745_5805, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BM(NA1_5310_5310, NA4_5755_5795, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1)},

	{APL4, ETSI, NO_DFS, NO_PSCAN, NO_REQ,
	 BM(F4_5180_5240,  F3_5745_5825, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BM(NA4_5190_5230, NA2_5755_5795, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1)},

	{APL5, ETSI, NO_DFS, NO_PSCAN, NO_REQ,
	 BM(F2_5745_5825, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BM(NA1_5755_5795, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1)},

	{APL6, ETSI, DFS_ETSI, PSCAN_FCC_T | PSCAN_FCC , NO_REQ,
	 BM(F4_5180_5240, F2_5260_5320, F3_5745_5825, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BM(T2_5210_5210, T1_5250_5290, T1_5760_5800, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BM(NA4_5190_5230, NA2_5270_5310, NA2_5755_5795, -1, -1, -1, -1, -1, -1, -1, -1, -1)},

	{APL7, FCC, NO_DFS, PSCAN_FCC_T | PSCAN_FCC , NO_REQ,
	 BM(F7_5260_5320, F4_5500_5700, F3_5745_5805, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BM(NA1_5310_5310, NA2_5755_5795, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1)},
	{APL8, ETSI, NO_DFS, NO_PSCAN, DISALLOW_ADHOC_11A|DISALLOW_ADHOC_11A_TURB,
	 BM(F6_5260_5320, F4_5745_5825, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BM(NA4_5270_5310, NA4_5755_5795, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1)},

	{APL9, ETSI, DFS_ETSI, PSCAN_ETSI, DISALLOW_ADHOC_11A|DISALLOW_ADHOC_11A_TURB,
	 BM(F1_5180_5320, F1_5500_5620, F3_5745_5805, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BM(NA4_5190_5230, NA2_5270_5310, NA1_5510_5630, NA4_5755_5795, -1, -1, -1, -1, -1, -1, -1, -1)},

	{ETSI1, ETSI, DFS_ETSI, PSCAN_ETSI, DISALLOW_ADHOC_11A | DISALLOW_ADHOC_11A_TURB,
	 BM(W2_5180_5240, F2_5260_5320, F2_5500_5700, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BM(NA4_5190_5230, NA2_5270_5310, NA2_5510_5670, -1, -1, -1, -1, -1, -1, -1, -1, -1)},

	{ETSI2, ETSI, DFS_ETSI, PSCAN_ETSI, DISALLOW_ADHOC_11A | DISALLOW_ADHOC_11A_TURB,
	 BM(F3_5180_5240, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BM(NA3_5190_5230, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1)},

	{ETSI3, ETSI, DFS_ETSI, PSCAN_ETSI, DISALLOW_ADHOC_11A | DISALLOW_ADHOC_11A_TURB,
	 BM(W2_5180_5240, F2_5260_5320, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BM(NA4_5190_5230, NA2_5270_5310, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1)},

	{ETSI4, ETSI, DFS_ETSI, PSCAN_ETSI, DISALLOW_ADHOC_11A | DISALLOW_ADHOC_11A_TURB,
	 BM(F3_5180_5240, F1_5260_5320, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BM(NA3_5190_5230, NA1_5270_5310, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1)},

	{ETSI5, ETSI, DFS_ETSI, PSCAN_ETSI, DISALLOW_ADHOC_11A | DISALLOW_ADHOC_11A_TURB,
	 BM(F1_5180_5240, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BM(NA1_5190_5230, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1)},

	{ETSI6, ETSI, DFS_ETSI, PSCAN_ETSI, DISALLOW_ADHOC_11A | DISALLOW_ADHOC_11A_TURB,
	 BM(F5_5180_5240, F1_5260_5280, F3_5500_5700, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BM(NA5_5190_5230, NA1_5270_5270, NA3_5510_5670, -1, -1, -1, -1, -1, -1, -1, -1, -1)},

	{FCC1, FCC, NO_DFS, NO_PSCAN, NO_REQ,
	 BM(F2_5180_5240, F4_5260_5320, F5_5745_5825, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BM(T1_5210_5210, T2_5250_5290, T2_5760_5800, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BM(T1_5200_5240, T1_5280_5280, T1_5765_5805, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BM(NA2_5190_5230, NA3_5270_5310, NA4_5755_5795, -1, -1, -1, -1, -1, -1, -1, -1, -1)},

	{FCC2, FCC, NO_DFS, NO_PSCAN, NO_REQ,
	 BM(F6_5180_5240, F5_5260_5320, F6_5745_5825, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BM(-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BM(T2_5200_5240, T1_5280_5280, T1_5765_5805, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BM(NA5_5190_5230, NA3_5270_5310, NA4_5755_5795, -1, -1, -1, -1, -1, -1, -1, -1, -1)},

	{FCC3, FCC, DFS_FCC3, PSCAN_FCC | PSCAN_FCC_T, NO_REQ,
	 BM(F2_5180_5240, F3_5260_5320, F1_5500_5700, F5_5745_5825, -1, -1, -1, -1, -1, -1, -1, -1),
	 BM(T1_5210_5210, T1_5250_5250, T1_5290_5290, T2_5760_5800, -1, -1, -1, -1, -1, -1, -1, -1),
	 BM(T1_5200_5240, T2_5280_5280, T1_5540_5660, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BM(NA2_5190_5230, NA2_5270_5310, NA3_5510_5670, NA4_5755_5795, -1, -1, -1, -1, -1, -1, -1, -1)},

	{FCC4, FCC, DFS_FCC3, PSCAN_FCC | PSCAN_FCC_T, NO_REQ,
	 BM(F1_4942_4987, F1_4945_4985, F1_4950_4980, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO},

	{FCC5, FCC, NO_DFS, NO_PSCAN, NO_REQ,
	 BM(F2_5180_5240, F5_5745_5825, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BM(NA2_5190_5230, NA4_5755_5795, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1)},

    {FCC6, FCC, DFS_FCC3, PSCAN_FCC, NO_REQ,
	 BM(F7_5180_5240, F5_5260_5320, F1_5500_5580, F1_5660_5700, F6_5745_5825, -1, -1, -1, -1, -1, -1, -1),
	 BM(-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BM(T2_5200_5240, T1_5280_5280, T1_5765_5805, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BMZERO,
	 BMZERO,
     BMZERO,
     BMZERO,
	 BM(NA5_5190_5230, NA5_5755_5795, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1)},

	{MKK1, MKK, NO_DFS, PSCAN_MKK1, DISALLOW_ADHOC_11A_TURB,
	 BM(F1_5170_5230, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO},

	{MKK2, MKK, NO_DFS, PSCAN_MKK2, DISALLOW_ADHOC_11A_TURB,
	 BM(F1_4915_4925, F1_4935_4945, F1_4920_4980, F1_5035_5040, F1_5055_5055, F1_5040_5080, F1_5170_5230, -1, -1, -1, -1, -1),
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO},

	/* UNI-1 even */
	{MKK3, MKK, NO_DFS, PSCAN_MKK3, DISALLOW_ADHOC_11A_TURB,
	 BM(F4_5180_5240, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BM(NA4_5190_5230, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1)},

	/* UNI-1 even + UNI-2 */
	{MKK4, MKK, DFS_MKK4, PSCAN_MKK3, DISALLOW_ADHOC_11A_TURB,
	 BM(F4_5180_5240, F2_5260_5320, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BM(NA4_5190_5230, NA2_5270_5310, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1)},

	/* UNI-1 even + UNI-2 + mid-band */
	{MKK5, MKK, DFS_MKK4, PSCAN_MKK3, DISALLOW_ADHOC_11A_TURB,
	 BM(F4_5180_5240, F2_5260_5320, F4_5500_5700, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BM(NA4_5190_5230, NA2_5270_5310, NA1_5510_5670, -1, -1, -1, -1, -1, -1, -1, -1, -1)},

	/* UNI-1 odd + even */
	{MKK6, MKK, DFS_MKK4, PSCAN_MKK1, DISALLOW_ADHOC_11A_TURB,
	 BM(F2_5170_5230, F4_5180_5240, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BM(NA4_5190_5230, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1)},

	/* UNI-1 odd + UNI-1 even + UNI-2 */
	{MKK7, MKK, DFS_MKK4, PSCAN_MKK1 | PSCAN_MKK3 , DISALLOW_ADHOC_11A_TURB,
	 BM(F2_5170_5230, F4_5180_5240, F2_5260_5320, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BM(NA4_5190_5230, NA2_5270_5310, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1)},

	/* UNI-1 odd + UNI-1 even + UNI-2 + mid-band */
	{MKK8, MKK, DFS_MKK4, PSCAN_MKK1 | PSCAN_MKK3 , DISALLOW_ADHOC_11A_TURB,
	 BM(F2_5170_5230, F4_5180_5240, F2_5260_5320, F4_5500_5700, -1, -1, -1, -1, -1, -1, -1, -1),
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BM(NA4_5190_5230, NA2_5270_5310, NA1_5510_5670, -1, -1, -1, -1, -1, -1, -1, -1, -1)},

    /* UNI-1 even + 4.9 GHZ */
    {MKK9, MKK, NO_DFS, NO_PSCAN, DISALLOW_ADHOC_11A_TURB,
     BM(F1_4915_4925, F1_4935_4945, F1_4920_4980, F1_5035_5040, F1_5055_5055, F1_5040_5080, F4_5180_5240, -1, -1, -1, -1, -1),
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO},

    /* UNI-1 even + UNI-2 + 4.9 GHZ */
	{MKK10, MKK, DFS_MKK4, PSCAN_MKK3, DISALLOW_ADHOC_11A_TURB,
	 BM(F1_4915_4925, F1_4935_4945, F1_4920_4980, F1_5035_5040, F1_5055_5055, F1_5040_5080, F4_5180_5240, F2_5260_5320, -1, -1, -1, -1),
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO},

	/* UNI-1 even + UNI-2 + 4.9 GHZ + mid-band */
	{MKK11, MKK, DFS_MKK4, PSCAN_MKK3, DISALLOW_ADHOC_11A_TURB,
	 BM(F1_4915_4925, F1_4935_4945, F1_4920_4980, F1_5035_5040, F1_5055_5055, F1_5040_5080, F4_5180_5240, F2_5260_5320, F4_5500_5700, -1, -1, -1),
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO},

	/* UNI-1 even + UNI-1 odd + UNI-2 + 4.9 GHZ + mid-band */
	{MKK12, MKK, DFS_MKK4, PSCAN_MKK3, DISALLOW_ADHOC_11A_TURB,
	 BM(F1_4915_4925, F1_4935_4945, F1_4920_4980, F1_5035_5040, F1_5055_5055, F1_5040_5080, F1_5170_5230, F4_5180_5240, F2_5260_5320, F4_5500_5700, -1, -1),
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO},

	/* Defined here to use when 2G channels are authorised for country K2 */
	{APLD, NO_CTL, NO_DFS, NO_PSCAN, NO_REQ,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BM(F2_2312_2372, F2_2412_2472, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BM(G2_2312_2372, G2_2412_2472, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BMZERO,
	 BMZERO,
	 BMZERO},

	{ETSIA, NO_CTL, NO_DFS, PSCAN_ETSIA, DISALLOW_ADHOC_11A | DISALLOW_ADHOC_11A_TURB,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BM(F1_2457_2472, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BM(G1_2457_2472, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BM(T2_2437_2437, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BMZERO,
	 BMZERO},

	{ETSIB, ETSI, NO_DFS, PSCAN_ETSIB, DISALLOW_ADHOC_11A | DISALLOW_ADHOC_11A_TURB,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BM(F1_2432_2442, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BM(G1_2432_2442, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BM(T2_2437_2437, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BMZERO,
	 BMZERO},

	{ETSIC, ETSI, NO_DFS, PSCAN_ETSIC, DISALLOW_ADHOC_11A | DISALLOW_ADHOC_11A_TURB,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BM(F3_2412_2472, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BM(G3_2412_2472, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BM(T2_2437_2437, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BMZERO,
	 BMZERO},

	{FCCA, FCC, NO_DFS, NO_PSCAN, NO_REQ,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BM(F1_2412_2462, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BM(G1_2412_2462, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BM(T2_2437_2437, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BM(NG2_2422_2452, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BMZERO},

	{MKKA, MKK, NO_DFS, PSCAN_MKKA | PSCAN_MKKA_G | PSCAN_MKKA1 | PSCAN_MKKA1_G | PSCAN_MKKA2 | PSCAN_MKKA2_G, DISALLOW_ADHOC_11A_TURB,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BM(F2_2412_2462, F1_2467_2472, F2_2484_2484, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BM(G2_2412_2462, G1_2467_2472, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BM(T2_2437_2437, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BM(NG1_2422_2452, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BMZERO},

	{MKKC, MKK, NO_DFS, NO_PSCAN, NO_REQ,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BM(F2_2412_2472, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BM(G2_2412_2472, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BM(T2_2437_2437, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BM(NG1_2422_2452, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BMZERO},

	{WORLD, ETSI, NO_DFS, NO_PSCAN, NO_REQ,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BM(F2_2412_2472, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BM(G2_2412_2472, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BM(T2_2437_2437, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BM(NG1_2422_2452, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BMZERO},

	{WOR0_WORLD, NO_CTL, DFS_FCC3 | DFS_ETSI, PSCAN_WWR, ADHOC_PER_11D,
	 BM(W1_5260_5320, W1_5180_5240, W1_5170_5230, W1_5745_5825, W1_5500_5700, -1, -1, -1, -1, -1, -1, -1),
	 BM(WT1_5210_5250, WT1_5290_5290, WT1_5760_5800, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BMZERO,
	 BM(W1_2412_2412, W1_2437_2442, W1_2462_2462, W1_2472_2472, W1_2417_2432, W1_2447_2457, W1_2467_2467, W1_2484_2484, -1, -1, -1, -1),
	 BM(WG1_2412_2412, WG1_2437_2442, WG1_2462_2462, WG1_2472_2472, WG1_2417_2432, WG1_2447_2457, WG1_2467_2467, -1, -1, -1, -1, -1),
	 BM(T3_2437_2437, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BMZERO,
	 BMZERO},

	{WOR01_WORLD, NO_CTL, DFS_FCC3 | DFS_ETSI, PSCAN_WWR, ADHOC_PER_11D,
	 BM(W1_5260_5320, W1_5180_5240, W1_5170_5230, W1_5745_5825, W1_5500_5700, -1, -1, -1, -1, -1, -1, -1),
	 BM(WT1_5210_5250, WT1_5290_5290, WT1_5760_5800, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BMZERO,
	 BM(W1_2412_2412, W1_2437_2442, W1_2462_2462, W1_2417_2432, W1_2447_2457, -1, -1, -1, -1, -1, -1, -1),
	 BM(WG1_2412_2412, WG1_2437_2442, WG1_2462_2462, WG1_2417_2432, WG1_2447_2457, -1, -1, -1, -1, -1, -1, -1),
	 BM(T3_2437_2437, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BMZERO,
	 BMZERO},

	{WOR02_WORLD, NO_CTL, DFS_FCC3 | DFS_ETSI, PSCAN_WWR, ADHOC_PER_11D,
	 BM(W1_5260_5320, W1_5180_5240, W1_5170_5230, W1_5745_5825, W1_5500_5700, -1, -1, -1, -1, -1, -1, -1),
	 BM(WT1_5210_5250, WT1_5290_5290, WT1_5760_5800, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BMZERO,
	 BM(W1_2412_2412, W1_2437_2442, W1_2462_2462, W1_2472_2472, W1_2417_2432, W1_2447_2457, W1_2467_2467, -1, -1, -1, -1, -1),
	 BM(WG1_2412_2412, WG1_2437_2442, WG1_2462_2462, WG1_2472_2472, WG1_2417_2432, WG1_2447_2457, WG1_2467_2467, -1, -1, -1, -1, -1),
	 BM(T3_2437_2437, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BMZERO,
	 BMZERO},

	{EU1_WORLD, NO_CTL, DFS_FCC3 | DFS_ETSI, PSCAN_WWR, ADHOC_PER_11D,
	 BM(W1_5260_5320, W1_5180_5240, W1_5170_5230, W1_5745_5825, W1_5500_5700, -1, -1, -1, -1, -1, -1, -1),
	 BM(WT1_5210_5250, WT1_5290_5290, WT1_5760_5800, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BMZERO,
	 BM(W1_2412_2412, W1_2437_2442, W1_2462_2462, W2_2472_2472, W1_2417_2432, W1_2447_2457, W2_2467_2467, -1, -1, -1, -1, -1),
	 BM(WG1_2412_2412, WG1_2437_2442, WG1_2462_2462, WG2_2472_2472, WG1_2417_2432, WG1_2447_2457, WG2_2467_2467, -1, -1, -1, -1, -1),
	 BM(T3_2437_2437, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BMZERO,
	 BMZERO},

	{WOR1_WORLD, NO_CTL, DFS_FCC3 | DFS_ETSI, PSCAN_WWR, ADHOC_NO_11A,
	 BM(W1_5260_5320, W1_5180_5240, W1_5170_5230, W1_5745_5825, W1_5500_5700, -1, -1, -1, -1, -1, -1, -1),
	 BMZERO,
	 BMZERO,
	 BM(W1_2412_2412, W1_2437_2442, W1_2462_2462, W1_2472_2472, W1_2417_2432, W1_2447_2457, W1_2467_2467, W1_2484_2484, -1, -1, -1, -1),
	 BM(WG1_2412_2412, WG1_2437_2442, WG1_2462_2462, WG1_2472_2472, WG1_2417_2432, WG1_2447_2457, WG1_2467_2467, -1, -1, -1, -1, -1),
	 BM(T3_2437_2437, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BMZERO,
	 BMZERO},

	{WOR2_WORLD, NO_CTL, DFS_FCC3 | DFS_ETSI, PSCAN_WWR, ADHOC_NO_11A,
	 BM(W1_5260_5320, W1_5180_5240, W1_5170_5230, W1_5745_5825, W1_5500_5700, -1, -1, -1, -1, -1, -1, -1),
	 BM(WT1_5210_5250, WT1_5290_5290, WT1_5760_5800, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BMZERO,
	 BM(W1_2412_2412, W1_2437_2442, W1_2462_2462, W1_2472_2472, W1_2417_2432, W1_2447_2457, W1_2467_2467, W1_2484_2484, -1, -1, -1, -1),
	 BM(WG1_2412_2412, WG1_2437_2442, WG1_2462_2462, WG1_2472_2472, WG1_2417_2432, WG1_2447_2457, WG1_2467_2467, -1, -1, -1, -1, -1),
	 BM(T3_2437_2437, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BMZERO,
	 BMZERO},

	{WOR3_WORLD, NO_CTL, DFS_FCC3 | DFS_ETSI, PSCAN_WWR, ADHOC_PER_11D,
	 BM(W1_5260_5320, W1_5180_5240, W1_5170_5230, W1_5745_5825, -1, -1, -1, -1, -1, -1, -1, -1),
	 BM(WT1_5210_5250, WT1_5290_5290, WT1_5760_5800, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BMZERO,
	 BM(W1_2412_2412, W1_2437_2442, W1_2462_2462, W1_2472_2472, W1_2417_2432, W1_2447_2457, W1_2467_2467, -1, -1, -1, -1, -1),
	 BM(WG1_2412_2412, WG1_2437_2442, WG1_2462_2462, WG1_2472_2472, WG1_2417_2432, WG1_2447_2457, WG1_2467_2467, -1, -1, -1, -1, -1),
	 BM(T3_2437_2437, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BMZERO,
	 BMZERO},

	{WOR4_WORLD, NO_CTL, DFS_FCC3, PSCAN_WWR, ADHOC_NO_11A,
	 BM(W2_5260_5320, W2_5180_5240, F2_5745_5805, W2_5825_5825, -1, -1, -1, -1, -1, -1, -1, -1),
	 BM(WT1_5210_5250, WT1_5290_5290, WT1_5760_5800, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BMZERO,
	 BM(W1_2412_2412, W1_2437_2442, W1_2462_2462, W1_2417_2432, W1_2447_2457, -1, -1, -1, -1, -1, -1, -1),
	 BM(WG1_2412_2412, WG1_2437_2442, WG1_2462_2462, WG1_2417_2432, WG1_2447_2457, -1, -1, -1, -1, -1, -1, -1),
	 BM(T3_2437_2437, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BMZERO,
	 BMZERO},

	{WOR5_ETSIC, NO_CTL, DFS_FCC3 | DFS_ETSI, PSCAN_WWR, ADHOC_NO_11A,
	 BM(W1_5260_5320, W2_5180_5240, F6_5745_5825, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BMZERO,
	 BMZERO,
	 BM(W1_2412_2412, W1_2437_2442, W1_2462_2462, W2_2472_2472, W1_2417_2432, W1_2447_2457, W2_2467_2467, -1, -1, -1, -1, -1),
	 BM(WG1_2412_2412, WG1_2437_2442, WG1_2462_2462, WG1_2472_2472, WG1_2417_2432, WG1_2447_2457, WG1_2467_2467, -1, -1, -1, -1, -1),
	 BM(T3_2437_2437, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BMZERO,
	 BMZERO},

	{WOR9_WORLD, NO_CTL, DFS_FCC3 | DFS_ETSI, PSCAN_WWR, ADHOC_NO_11A,
	 BM(W1_5260_5320, W1_5180_5240, W1_5745_5825, W1_5500_5700, -1, -1, -1, -1, -1, -1, -1, -1),
	 BM(WT1_5210_5250, WT1_5290_5290, WT1_5760_5800, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BMZERO,
	 BM(W1_2412_2412, W1_2437_2442, W1_2462_2462, W1_2417_2432, W1_2447_2457, -1, -1, -1, -1, -1, -1, -1),
	 BM(WG1_2412_2412, WG1_2437_2442, WG1_2462_2462, WG1_2417_2432, WG1_2447_2457, -1, -1, -1, -1, -1, -1, -1),
	 BM(T3_2437_2437, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BMZERO,
	 BMZERO},

	{WORA_WORLD, NO_CTL, DFS_FCC3 | DFS_ETSI, PSCAN_WWR, ADHOC_NO_11A,
	 BM(W1_5260_5320, W1_5180_5240, W1_5745_5825, W1_5500_5700, -1, -1, -1, -1, -1, -1, -1, -1),
	 BMZERO,
	 BMZERO,
	 BM(W1_2412_2412, W1_2437_2442, W1_2462_2462, W1_2472_2472, W1_2417_2432, W1_2447_2457, W1_2467_2467, -1, -1, -1, -1, -1),
	 BM(WG1_2412_2412, WG1_2437_2442, WG1_2462_2462, WG1_2472_2472, WG1_2417_2432, WG1_2447_2457, WG1_2467_2467, -1, -1, -1, -1, -1),
	 BM(T3_2437_2437, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1),
	 BMZERO,
	 BMZERO},

	{NULL1, NO_CTL, NO_DFS, NO_PSCAN, NO_REQ,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO,
	 BMZERO},
};

struct cmode {
	u16_t	mode;
	u32_t	flags;
};

static const struct cmode modes[] = {
	{ HAL_MODE_TURBO,	CHANNEL_ST},	/* TURBO means 11a Static Turbo */
	{ HAL_MODE_11A,		CHANNEL_A},
	{ HAL_MODE_11B,		CHANNEL_B},
	{ HAL_MODE_11G,		CHANNEL_G},
	{ HAL_MODE_11G_TURBO,	CHANNEL_108G},
	{ HAL_MODE_11A_TURBO,	CHANNEL_108A},
	{ HAL_MODE_11NA,	CHANNEL_A_HT40},
	{ HAL_MODE_11NA,	CHANNEL_A_HT20},
	{ HAL_MODE_11NG,	CHANNEL_G_HT40},
	{ HAL_MODE_11NG,	CHANNEL_G_HT20},
};

/*
 * Return the Wireless Mode Regulatory Domain based
 * on the country code and the wireless mode.
 */
u8_t GetWmRD(u16_t regionCode, u16_t channelFlag, REG_DOMAIN *rd)
{
	s16_t i, found, regDmn;
	u64_t flags = NO_REQ;
	REG_DMN_PAIR_MAPPING *regPair = NULL;

	for (i = 0, found = 0; (i < ARRAY_SIZE(regDomainPairs)) && (!found); i++) {
		if (regDomainPairs[i].regDmnEnum == regionCode) {
			regPair = &regDomainPairs[i];
			found = 1;
		}
	}
	if (!found) {
		zm_debug_msg1("Failed to find reg domain pair ", regionCode);
		return FALSE;
	}

	if (channelFlag & ZM_REG_FLAG_CHANNEL_2GHZ) {
		regDmn = regPair->regDmn2GHz;
		flags = regPair->flags2GHz;
	} else {
		regDmn = regPair->regDmn5GHz;
		flags = regPair->flags5GHz;
	}

	/*
	 * We either started with a unitary reg domain or we've found the
	 * unitary reg domain of the pair
	 */

	for (i = 0 ; i < ARRAY_SIZE(regDomains) ; i++) {
		if (regDomains[i].regDmnEnum == regDmn) {
			if (rd != NULL) {
					zfMemoryCopy((u8_t *)rd, (u8_t *)&regDomains[i],
					sizeof(REG_DOMAIN));
			}
		}
	}
	rd->pscan &= regPair->pscanMask;
	rd->flags = (u32_t)flags;
	return TRUE;
}

/*
 * Test to see if the bitmask array is all zeros
 */
u8_t isChanBitMaskZero(u64_t *bitmask)
{
	u16_t i;

	for (i = 0; i < BMLEN; i++) {
		if (bitmask[i] != 0)
			return FALSE;
	}
	return TRUE;
}

u8_t IS_BIT_SET(u32_t bit, u64_t *bitmask)
{
	u32_t byteOffset, bitnum;
	u64_t val;

	byteOffset = bit/64;
	bitnum = bit - byteOffset*64;
	val = ((u64_t) 1) << bitnum;
	if (bitmask[byteOffset] & val)
		return TRUE;
	else
		return FALSE;
}


void zfHpGetRegulationTable(zdev_t *dev, u16_t regionCode, u16_t c_lo, u16_t c_hi)
{
	REG_DOMAIN rd5GHz, rd2GHz;
	const struct cmode *cm;
	s16_t next = 0, b;
	struct zsHpPriv *hpPriv;

	zmw_get_wlan_dev(dev);
	hpPriv = wd->hpPrivate;

	zmw_declare_for_critical_section();

	if (!GetWmRD(regionCode, ~ZM_REG_FLAG_CHANNEL_2GHZ, &rd5GHz)) {
		zm_debug_msg1("couldn't find unitary 5GHz reg domain for Region Code ", regionCode);
		return;
	}
	if (!GetWmRD(regionCode, ZM_REG_FLAG_CHANNEL_2GHZ, &rd2GHz)) {
		zm_debug_msg1("couldn't find unitary 2GHz reg domain for Region Code ", regionCode);
		return;
	}
	if (wd->regulationTable.regionCode == regionCode) {
		zm_debug_msg1("current region code is the same with Region Code ", regionCode);
	return;
	} else
		wd->regulationTable.regionCode = regionCode;

	next = 0;

	zmw_enter_critical_section(dev);

	for (cm = modes; cm < &modes[ARRAY_SIZE(modes)]; cm++) {
		u16_t c;
		u64_t *channelBM = NULL;
		REG_DOMAIN *rd = NULL;
		REG_DMN_FREQ_BAND *fband = NULL, *freqs = NULL;

		switch (cm->mode) {
		case HAL_MODE_TURBO:
			/* we don't have turbo mode so we disable it
			//zm_debug_msg0("CWY - HAL_MODE_TURBO"); */
			channelBM = NULL;
			/* rd = &rd5GHz;
			   channelBM = rd->chan11a_turbo;
			   freqs = &regDmn5GhzTurboFreq[0];
			   ctl = rd->conformanceTestLimit | CTL_TURBO; */
			break;
		case HAL_MODE_11A:
			if ((hpPriv->OpFlags & 0x1) != 0) {
				rd = &rd5GHz;
				channelBM = rd->chan11a;
				freqs = &regDmn5GhzFreq[0];
				c_lo = 4920; /* from channel 184 */
				c_hi = 5825; /* to   channel 165 */
				/* ctl = rd->conformanceTestLimit;
				   zm_debug_msg2("CWY - HAL_MODE_11A, channelBM = 0x", *channelBM); */
			}
			/* else
				channelBM = NULL;
			*/
			break;
		case HAL_MODE_11B:
			/* Disable 11B mode because it only has difference with 11G in PowerDFS Data,
			   and we don't use this now.
			   zm_debug_msg0("CWY - HAL_MODE_11B"); */
			channelBM = NULL;
			/* rd = &rd2GHz;
			   channelBM = rd->chan11b;
			   freqs = &regDmn2GhzFreq[0];
			   ctl = rd->conformanceTestLimit | CTL_11B;
			   zm_debug_msg2("CWY - HAL_MODE_11B, channelBM = 0x", *channelBM); */
			break;
		case HAL_MODE_11G:
			if ((hpPriv->OpFlags & 0x2) != 0) {
				rd = &rd2GHz;
				channelBM = rd->chan11g;
				freqs = &regDmn2Ghz11gFreq[0];
				c_lo = 2412;	/* from channel  1 */
				/* c_hi = 2462;	to   channel 11 */
				c_hi = 2472;	/* to   channel 13 */
				/* ctl = rd->conformanceTestLimit | CTL_11G; */
				/* zm_debug_msg2("CWY - HAL_MODE_11G, channelBM = 0x", *channelBM); */
			}
			/* else
				channelBM = NULL;
			*/
			break;
		case HAL_MODE_11G_TURBO:
			/* we don't have turbo mode so we disable it
			   zm_debug_msg0("CWY - HAL_MODE_11G_TURBO"); */
			channelBM = NULL;
			/* rd = &rd2GHz;
			   channelBM = rd->chan11g_turbo;
			   freqs = &regDmn2Ghz11gTurboFreq[0];
			   ctl = rd->conformanceTestLimit | CTL_108G; */
			break;
		case HAL_MODE_11A_TURBO:
			/* we don't have turbo mode so we disable it
			   zm_debug_msg0("CWY - HAL_MODE_11A_TURBO"); */
			channelBM = NULL;
			/* rd = &rd5GHz;
			   channelBM = rd->chan11a_dyn_turbo;
			   freqs = &regDmn5GhzTurboFreq[0];
			   ctl = rd->conformanceTestLimit | CTL_108G; */
			break;
		default:
			zm_debug_msg1("Unkonwn HAL mode ", cm->mode);
			continue;
		}

		if (channelBM == NULL) {
		    /* zm_debug_msg0("CWY - channelBM is NULL"); */
		    continue;
		}

		if (isChanBitMaskZero(channelBM)) {
			/* zm_debug_msg0("CWY - BitMask is Zero"); */
			continue;
		}

		/* RAY:Is it ok?? */
		if (freqs == NULL)
			continue;

		for (b = 0 ; b < 64*BMLEN ; b++) {
			if (IS_BIT_SET(b, channelBM)) {
				fband = &freqs[b];

				/* zm_debug_msg1("CWY - lowChannel = ", fband->lowChannel);
				   zm_debug_msg1("CWY - highChannel = ", fband->highChannel);
				   zm_debug_msg1("CWY - channelSep = ", fband->channelSep); */
				for (c = fband->lowChannel; c <= fband->highChannel;
				     c += fband->channelSep) {
					ZM_HAL_CHANNEL icv;

					/* Disable all DFS channel */
					if ((hpPriv->disableDfsCh == 0) || (!(fband->useDfs & rd->dfsMask))) {
						if (fband->channelBW < 20) {
							/**************************************************************/
							/*                                                            */
							/*   Temporary discard channel that BW < 20MHz (5 or 10MHz)   */
							/*   Our architecture does not implemnt it !!!                */
							/*                                                            */
							/**************************************************************/
							continue;
						}
						if ((c >= c_lo) && (c <= c_hi)) {
							icv.channel = c;
							icv.channelFlags = cm->flags;
							icv.maxRegTxPower = fband->powerDfs;
							if (fband->usePassScan & rd->pscan)
								icv.channelFlags |= ZM_REG_FLAG_CHANNEL_PASSIVE;
							else
								icv.channelFlags &= ~ZM_REG_FLAG_CHANNEL_PASSIVE;
							if (fband->useDfs & rd->dfsMask)
								icv.privFlags = ZM_REG_FLAG_CHANNEL_DFS;
							else
								icv.privFlags = 0;

							/* For now disable radar for FCC3 */
							if (fband->useDfs & rd->dfsMask & DFS_FCC3) {
								icv.privFlags &= ~ZM_REG_FLAG_CHANNEL_DFS;
								icv.privFlags |= ZM_REG_FLAG_CHANNEL_DFS_CLEAR;
							}

							if (rd->flags & LIMIT_FRAME_4MS)
								icv.privFlags |= ZM_REG_FLAG_CHANNEL_DFS_CLEAR;

							icv.minTxPower = 0;
							icv.maxTxPower = 0;

							zm_assert(next < 60);

							wd->regulationTable.allowChannel[next++] = icv;
						}
					}
				}
			}
		}
	}
	wd->regulationTable.allowChannelCnt = next;

	#if 0
	{
		/* debug print */
		u32_t i;
		DbgPrint("\n-------------------------------------------\n");
		DbgPrint("zfHpGetRegulationTable print all channel info regincode = 0x%x\n", wd->regulationTable.regionCode);
		DbgPrint("index  channel  channelFlags   maxRegTxPower  privFlags  useDFS\n");

		for (i = 0 ; i < wd->regulationTable.allowChannelCnt ; i++) {
			DbgPrint("%02d       %d         %04x           %02d        %x     %x\n", i,
			wd->regulationTable.allowChannel[i].channel,
			wd->regulationTable.allowChannel[i].channelFlags,
			wd->regulationTable.allowChannel[i].maxRegTxPower,
			wd->regulationTable.allowChannel[i].privFlags,
			wd->regulationTable.allowChannel[i].privFlags & ZM_REG_FLAG_CHANNEL_DFS);
		}
	}
	#endif

	zmw_leave_critical_section(dev);
}

void zfHpGetRegulationTablefromRegionCode(zdev_t *dev, u16_t regionCode)
{
	u16_t c_lo = 2000, c_hi = 6000; /* default channel is all enable */
	u8_t isoName[3] = {'N', 'A', 0};

	zfCoreSetIsoName(dev, isoName);

	zfHpGetRegulationTable(dev, regionCode, c_lo, c_hi);
}

void zfHpGetRegulationTablefromCountry(zdev_t *dev, u16_t CountryCode)
{
	u16_t i;
	u16_t c_lo = 2000, c_hi = 6000; /* default channel is all enable */
	u16_t RegDomain;

	zmw_get_wlan_dev(dev);

	zmw_declare_for_critical_section();

	for (i = 0; i < ARRAY_SIZE(allCountries); i++) {
		if (CountryCode == allCountries[i].countryCode) {
			RegDomain = allCountries[i].regDmnEnum;

			/* read the ACU country code from EEPROM */
			zfCoreSetIsoName(dev, (u8_t *)allCountries[i].isoName);

			/* zm_debug_msg_s("CWY - Country Name = ", allCountries[i].name); */

			if (wd->regulationTable.regionCode != RegDomain) {
				/* zm_debug_msg0("CWY - Change regulatory table"); */
				zfHpGetRegulationTable(dev, RegDomain, c_lo, c_hi);
			}
			return;
		}
	}
	zm_debug_msg1("Invalid CountryCode = ", CountryCode);
}

u8_t zfHpGetRegulationTablefromISO(zdev_t *dev, u8_t *countryInfo, u8_t length)
{
	u16_t i;
	u16_t RegDomain;
	u16_t c_lo = 2000, c_hi = 6000; /* default channel is all enable */
	/* u8_t strLen = 2; */

	zmw_get_wlan_dev(dev);

	zmw_declare_for_critical_section();

	if (countryInfo[4] != 0x20) {
		/* with (I)ndoor/(O)utdoor info
		strLen = 3; */
	}
	/* zm_debug_msg_s("Desired iso name = ", isoName); */
	for (i = 0; i < ARRAY_SIZE(allCountries); i++) {
		/* zm_debug_msg_s("Current iso name = ", allCountries[i].isoName); */
		if (zfMemoryIsEqual((u8_t *)allCountries[i].isoName, (u8_t *)&countryInfo[2], length-1)) {
			/* DbgPrint("Set current iso name = %s\n", allCountries[i].isoName); */
			/* zm_debug_msg0("iso name hit!!"); */

			RegDomain = allCountries[i].regDmnEnum;

			if (wd->regulationTable.regionCode != RegDomain)
				zfHpGetRegulationTable(dev, RegDomain, c_lo, c_hi);
			/*
			while (index < (countryInfo[1]+2)) {
				if (countryInfo[index] <= 14) {
					// calculate 2.4GHz low boundary channel frequency
					ch = countryInfo[index];
					if ( ch == 14 )
						c_lo = ZM_CH_G_14;
					else
						c_lo = ZM_CH_G_1 + (ch - 1) * 5;
					// calculate 2.4GHz high boundary channel frequency
					ch = countryInfo[index] + countryInfo[index + 1] - 1;
					if ( ch == 14 )
						c_hi = ZM_CH_G_14;
					else
						c_hi = ZM_CH_G_1 + (ch - 1) * 5;
				} else {
					// calculate 5GHz low boundary channel frequency
					ch = countryInfo[index];
					if ( (ch >= 184)&&(ch <= 196) )
						c_lo = 4000 + ch*5;
					else
						c_lo = 5000 + ch*5;
					// calculate 5GHz high boundary channel frequency
					ch = countryInfo[index] + countryInfo[index + 1] - 1;
					if ( (ch >= 184)&&(ch <= 196) )
						c_hi = 4000 + ch*5;
					else
						c_hi = 5000 + ch*5;
				}

			zfHpGetRegulationTable(dev, RegDomain, c_lo, c_hi);

			index+=3;
			}
			*/
			return 0;
		}
	}
	/* zm_debug_msg_s("Invalid iso name = ", &countryInfo[2]); */
	return 1;
}

const char *zfHpGetisoNamefromregionCode(zdev_t *dev, u16_t regionCode)
{
	u16_t i;

	for (i = 0; i < ARRAY_SIZE(allCountries); i++) {
		if (allCountries[i].regDmnEnum == regionCode)
			return allCountries[i].isoName;
	}
	/* no matching item, return default */
	return allCountries[0].isoName;
}

u16_t zfHpGetRegionCodeFromIsoName(zdev_t *dev, u8_t *countryIsoName)
{
	u16_t i;
	u16_t regionCode;

	/* if no matching item, return default */
	regionCode = DEF_REGDMN;

	for (i = 0; i < ARRAY_SIZE(allCountries); i++) {
		if (zfMemoryIsEqual((u8_t *)allCountries[i].isoName, countryIsoName, 2)) {
			regionCode = allCountries[i].regDmnEnum;
		break;
		}
	}

	return regionCode;
}

/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfHpDeleteAllowChannel      */
/*      Delete Allow Channel.                                           */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev  : device pointer                                           */
/*      freq : frequency                                                */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      0 : success                                                     */
/*      other : fail                                                    */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Chao-Wen Yang         ZyDAS Technology Corporation    2007.3    */
/*                                                                      */
/************************************************************************/
u16_t zfHpDeleteAllowChannel(zdev_t *dev, u16_t freq)
{
	u16_t i, bandIndex = 0;
	u16_t dfs5GBand[][2] = { {5150, 5240}, {5260, 5350}, {5450, 5700}, {5725, 5825} };

	zmw_get_wlan_dev(dev);
	/* Find which band does this frequency belong */
	for (i = 0; i < 4; i++) {
		if ((freq >= dfs5GBand[i][0]) && (freq <= dfs5GBand[i][1]))
			bandIndex = i + 1;
	}

	if (bandIndex == 0) {
		/* 2.4G, don't care */
		return 0;
	} else
		bandIndex--;
	/* Set all channels in this band to passive scan */
	for (i = 0; i < wd->regulationTable.allowChannelCnt; i++) {
		if ((wd->regulationTable.allowChannel[i].channel >= dfs5GBand[bandIndex][0]) &&
			(wd->regulationTable.allowChannel[i].channel <= dfs5GBand[bandIndex][1])) {
			/* if channel is not passive, set it to be passive and mark it */
			if ((wd->regulationTable.allowChannel[i].channelFlags &
				ZM_REG_FLAG_CHANNEL_PASSIVE) == 0) {
				wd->regulationTable.allowChannel[i].channelFlags |=
				(ZM_REG_FLAG_CHANNEL_PASSIVE | ZM_REG_FLAG_CHANNEL_CSA);
			}
		}
	}

	return 0;
}

u16_t zfHpAddAllowChannel(zdev_t *dev, u16_t freq)
{
	u16_t i, j, arrayIndex;

	zmw_get_wlan_dev(dev);

	for (i = 0; i < wd->regulationTable.allowChannelCnt; i++) {
		if (wd->regulationTable.allowChannel[i].channel == freq)
			break;
	}

	if (i == wd->regulationTable.allowChannelCnt) {
		for (j = 0; j < wd->regulationTable.allowChannelCnt; j++) {
			if (wd->regulationTable.allowChannel[j].channel > freq)
				break;
		}

		/* zm_debug_msg1("CWY - add frequency = ", freq);
		zm_debug_msg1("CWY - channel array index = ", j); */

		arrayIndex = j;

		if (arrayIndex < wd->regulationTable.allowChannelCnt) {
			for (j = wd->regulationTable.allowChannelCnt; j > arrayIndex; j--)
				wd->regulationTable.allowChannel[j] = wd->regulationTable.allowChannel[j - 1];
		}
	wd->regulationTable.allowChannel[arrayIndex].channel = freq;

	wd->regulationTable.allowChannelCnt++;
	}

	return 0;
}

u16_t zfHpIsDfsChannelNCS(zdev_t *dev, u16_t freq)
{
	u8_t flag = ZM_REG_FLAG_CHANNEL_DFS;
	u16_t i;
	zmw_get_wlan_dev(dev);

	for (i = 0; i < wd->regulationTable.allowChannelCnt; i++) {
		/* DbgPrint("DFS:freq=%d, chan=%d", freq, wd->regulationTable.allowChannel[i].channel); */
		if (wd->regulationTable.allowChannel[i].channel == freq) {
			flag = wd->regulationTable.allowChannel[i].privFlags;
			break; }
	}

	return flag & (ZM_REG_FLAG_CHANNEL_DFS|ZM_REG_FLAG_CHANNEL_DFS_CLEAR);
}

u16_t zfHpIsDfsChannel(zdev_t *dev, u16_t freq)
{
	u8_t flag = ZM_REG_FLAG_CHANNEL_DFS;
	u16_t i;
	zmw_get_wlan_dev(dev);

	zmw_declare_for_critical_section();

	zmw_enter_critical_section(dev);

	for (i = 0; i < wd->regulationTable.allowChannelCnt; i++) {
		/* DbgPrint("DFS:freq=%d, chan=%d", freq, wd->regulationTable.allowChannel[i].channel); */
		if (wd->regulationTable.allowChannel[i].channel == freq) {
			flag = wd->regulationTable.allowChannel[i].privFlags;
			break;
		}
	}

	zmw_leave_critical_section(dev);

	return flag & (ZM_REG_FLAG_CHANNEL_DFS|ZM_REG_FLAG_CHANNEL_DFS_CLEAR);
}

u16_t zfHpIsAllowedChannel(zdev_t *dev, u16_t freq)
{
	u16_t i;
	zmw_get_wlan_dev(dev);

	for (i = 0; i < wd->regulationTable.allowChannelCnt; i++) {
		if (wd->regulationTable.allowChannel[i].channel == freq)
			return 1;
	}

	return 0;
}

u16_t zfHpFindFirstNonDfsChannel(zdev_t *dev, u16_t aBand)
{
	u16_t chan = 2412;
	u16_t i;
	zmw_get_wlan_dev(dev);

	zmw_declare_for_critical_section();

	zmw_enter_critical_section(dev);

	for (i = 0; i < wd->regulationTable.allowChannelCnt; i++) {
		if ((wd->regulationTable.allowChannel[i].privFlags & ZM_REG_FLAG_CHANNEL_DFS) != 0) {
			if (aBand) {
				if (wd->regulationTable.allowChannel[i].channel > 3000) {
					chan = wd->regulationTable.allowChannel[i].channel;
					break;
				}
			} else {
				if (wd->regulationTable.allowChannel[i].channel < 3000) {
					chan = wd->regulationTable.allowChannel[i].channel;
					break;
				}
			}
		}
	}

	zmw_leave_critical_section(dev);

	return chan;
}


/* porting from ACU */
/* save RegulatoryDomain in hpriv */
u8_t zfHpGetRegulatoryDomain(zdev_t *dev)
{
	zmw_get_wlan_dev(dev);

	switch (wd->regulationTable.regionCode) {
	case NO_ENUMRD:
		return 0;
		break;
	case FCC1_FCCA:
	case FCC1_WORLD:
	case FCC4_FCCA:
	case FCC5_FCCA:
	case FCC2_WORLD:
	case FCC2_ETSIC:
	case FCC3_FCCA:
	case FCC3_WORLD:
	case FCC1:
	case FCC2:
	case FCC3:
	case FCC4:
	case FCC5:
	case FCCA:
		return 0x10;/* WG_AMERICAS DOT11_REG_DOMAIN_FCC  United States */
		break;

	case FCC2_FCCA:
		return 0x20;/* DOT11_REG_DOMAIN_DOC  Canada */
		break;

	case ETSI1_WORLD:
	case ETSI3_ETSIA:
	case ETSI2_WORLD:
	case ETSI3_WORLD:
	case ETSI4_WORLD:
	case ETSI4_ETSIC:
	case ETSI5_WORLD:
	case ETSI6_WORLD:
	case ETSI_RESERVED:
	case ETSI1:
	case ETSI2:
	case ETSI3:
	case ETSI4:
	case ETSI5:
	case ETSI6:
	case ETSIA:
	case ETSIB:
	case ETSIC:
		return 0x30;/* WG_EMEA DOT11_REG_DOMAIN_ETSI  Most of Europe */
		break;

	case MKK1_MKKA:
	case MKK1_MKKB:
	case MKK2_MKKA:
	case MKK1_FCCA:
	case MKK1_MKKA1:
	case MKK1_MKKA2:
	case MKK1_MKKC:
	case MKK3_MKKB:
	case MKK3_MKKA2:
	case MKK3_MKKC:
	case MKK4_MKKB:
	case MKK4_MKKA2:
	case MKK4_MKKC:
	case MKK5_MKKB:
	case MKK5_MKKA2:
	case MKK5_MKKC:
	case MKK6_MKKB:
	case MKK6_MKKA2:
	case MKK6_MKKC:
	case MKK7_MKKB:
	case MKK7_MKKA:
	case MKK7_MKKC:
	case MKK8_MKKB:
	case MKK8_MKKA2:
	case MKK8_MKKC:
	case MKK6_MKKA1:
	case MKK6_FCCA:
	case MKK7_MKKA1:
	case MKK7_FCCA:
	case MKK9_FCCA:
	case MKK9_MKKA1:
	case MKK9_MKKC:
	case MKK9_MKKA2:
	case MKK10_FCCA:
	case MKK10_MKKA1:
	case MKK10_MKKC:
	case MKK10_MKKA2:
	case MKK11_MKKA:
	case MKK11_FCCA:
	case MKK11_MKKA1:
	case MKK11_MKKC:
	case MKK11_MKKA2:
	case MKK12_MKKA:
	case MKK12_FCCA:
	case MKK12_MKKA1:
	case MKK12_MKKC:
	case MKK12_MKKA2:
	case MKK3_MKKA:
	case MKK3_MKKA1:
	case MKK3_FCCA:
	case MKK4_MKKA:
	case MKK4_MKKA1:
	case MKK4_FCCA:
	case MKK9_MKKA:
	case MKK10_MKKA:
	case MKK1:
	case MKK2:
	case MKK3:
	case MKK4:
	case MKK5:
	case MKK6:
	case MKK7:
	case MKK8:
	case MKK9:
	case MKK10:
	case MKK11:
	case MKK12:
	case MKKA:
	case MKKC:
		return 0x40;/* WG_JAPAN DOT11_REG_DOMAIN_MKK  Japan */
		break;

	default:
		break;
	}

	return 0xFF; /* Didn't input RegDmn by mean to distinguish by customer */
}

void zfHpDisableDfsChannel(zdev_t *dev, u8_t disableFlag)
{
	struct zsHpPriv *hpPriv;

	zmw_get_wlan_dev(dev);
	hpPriv = wd->hpPrivate;
	hpPriv->disableDfsCh = disableFlag;
	return;
}
