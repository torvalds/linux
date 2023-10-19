/* SPDX-License-Identifier: GPL-2.0 */
/****************************************************************************/

/*
 * m52xxacr.h -- ColdFire version 2 core cache support
 *
 * (C) Copyright 2010, Greg Ungerer <gerg@snapgear.com>
 */

/****************************************************************************/
#ifndef m52xxacr_h
#define m52xxacr_h
/****************************************************************************/

/*
 * All varients of the ColdFire using version 2 cores have a similar
 * cache setup. Although not absolutely identical the cache register
 * definitions are compatible for all of them. Mostly they support a
 * configurable cache memory that can be instruction only, data only,
 * or split instruction and data. The exception is the very old version 2
 * core based parts, like the 5206(e), 5249 and 5272, which are instruction
 * cache only. Cache size varies from 2k up to 16k.
 */

/*
 * Define the Cache Control register flags.
 */
#define CACR_CENB	0x80000000	/* Enable cache */
#define CACR_CDPI	0x10000000	/* Disable invalidation by CPUSHL */
#define CACR_CFRZ	0x08000000	/* Cache freeze mode */
#define CACR_CINV	0x01000000	/* Invalidate cache */
#define CACR_DISI	0x00800000	/* Disable instruction cache */
#define CACR_DISD	0x00400000	/* Disable data cache */
#define CACR_INVI	0x00200000	/* Invalidate instruction cache */
#define CACR_INVD	0x00100000	/* Invalidate data cache */
#define CACR_CEIB	0x00000400	/* Non-cachable instruction burst */
#define CACR_DCM	0x00000200	/* Default cache mode */
#define CACR_DBWE	0x00000100	/* Buffered write enable */
#define CACR_DWP	0x00000020	/* Write protection */
#define CACR_EUSP	0x00000010	/* Enable separate user a7 */

/*
 * Define the Access Control register flags.
 */
#define ACR_BASE_POS	24		/* Address Base (upper 8 bits) */
#define ACR_MASK_POS	16		/* Address Mask (next 8 bits) */
#define ACR_ENABLE	0x00008000	/* Enable this ACR */
#define ACR_USER	0x00000000	/* Allow only user accesses */
#define ACR_SUPER	0x00002000	/* Allow supervisor access only */
#define ACR_ANY		0x00004000	/* Allow any access type */
#define ACR_CENB	0x00000000	/* Caching of region enabled */
#define ACR_CDIS	0x00000040	/* Caching of region disabled */
#define ACR_BWE		0x00000020	/* Write buffer enabled */
#define ACR_WPROTECT	0x00000004	/* Write protect region */

/*
 * Set the cache controller settings we will use. On the cores that support
 * a split cache configuration we allow all the combinations at Kconfig
 * time. For those cores that only have an instruction cache we just set
 * that as on.
 */
#if defined(CONFIG_CACHE_I)
#define CACHE_TYPE	(CACR_DISD + CACR_EUSP)
#define CACHE_INVTYPEI	0
#elif defined(CONFIG_CACHE_D)
#define CACHE_TYPE	(CACR_DISI + CACR_EUSP)
#define CACHE_INVTYPED	0
#elif defined(CONFIG_CACHE_BOTH)
#define CACHE_TYPE	CACR_EUSP
#define CACHE_INVTYPEI	CACR_INVI
#define CACHE_INVTYPED	CACR_INVD
#else
/* This is the instruction cache only devices (no split cache, no eusp) */
#define CACHE_TYPE	0
#define CACHE_INVTYPEI	0
#endif

#define CACHE_INIT	(CACR_CINV + CACHE_TYPE)
#define CACHE_MODE	(CACR_CENB + CACHE_TYPE + CACR_DCM)

#define CACHE_INVALIDATE  (CACHE_MODE + CACR_CINV)
#if defined(CACHE_INVTYPEI)
#define CACHE_INVALIDATEI (CACHE_MODE + CACR_CINV + CACHE_INVTYPEI)
#endif
#if defined(CACHE_INVTYPED)
#define CACHE_INVALIDATED (CACHE_MODE + CACR_CINV + CACHE_INVTYPED)
#endif

#define ACR0_MODE	((CONFIG_RAMBASE & 0xff000000) + \
			 (0x000f0000) + \
			 (ACR_ENABLE + ACR_ANY + ACR_CENB + ACR_BWE))
#define ACR1_MODE	0

/****************************************************************************/
#endif  /* m52xxsim_h */
