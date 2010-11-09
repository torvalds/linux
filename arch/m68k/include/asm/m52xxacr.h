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
 * Set the cache controller settings we will use. This code is set to
 * only use the instruction cache, even on the controllers that support
 * split cache. (This setup is trying to preserve the existing behavior
 * for now, in the furture I hope to actually use the split cache mode).
 */
#if defined(CONFIG_M5206) || defined(CONFIG_M5206e) || \
    defined(CONFIG_M5249) || defined(CONFIG_M5272)
#define CACHE_INIT	(CACR_CINV)
#define CACHE_MODE	(CACR_CENB + CACR_DCM)
#else
#ifdef CONFIG_COLDFIRE_SW_A7
#define CACHE_INIT	(CACR_CINV + CACR_DISD)
#define CACHE_MODE	(CACR_CENB + CACR_DISD + CACR_DCM)
#else
#define CACHE_INIT	(CACR_CINV + CACR_DISD + CACR_EUSP)
#define CACHE_MODE	(CACR_CENB + CACR_DISD + CACR_DCM + CACR_EUSP)
#endif
#endif

#define CACHE_INVALIDATE (CACHE_MODE + CACR_CINV)

#define ACR0_MODE	((CONFIG_RAMBASE & 0xff000000) + \
			 (0x000f0000) + \
			 (ACR_ENABLE + ACR_ANY + ACR_CENB + ACR_BWE))
#define ACR1_MODE	0

/****************************************************************************/
#endif  /* m52xxsim_h */
