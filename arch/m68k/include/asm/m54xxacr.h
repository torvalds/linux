/*
 * Bit definitions for the MCF54xx ACR and CACR registers.
 */

#ifndef	m54xxacr_h
#define m54xxacr_h

/*
 *	Define the Cache register flags.
 */
#define CACR_DEC	0x80000000	/* Enable data cache */
#define CACR_DWP	0x40000000	/* Data write protection */
#define CACR_DESB	0x20000000	/* Enable data store buffer */
#define CACR_DDPI	0x10000000	/* Disable invalidation by CPUSHL */
#define CACR_DHCLK	0x08000000	/* Half data cache lock mode */
#define CACR_DDCM_WT	0x00000000	/* Write through cache*/
#define CACR_DDCM_CP	0x02000000	/* Copyback cache */
#define CACR_DDCM_P	0x04000000	/* No cache, precise */
#define CACR_DDCM_IMP	0x06000000	/* No cache, imprecise */
#define CACR_DCINVA	0x01000000	/* Invalidate data cache */
#define CACR_BEC	0x00080000	/* Enable branch cache */
#define CACR_BCINVA	0x00040000	/* Invalidate branch cache */
#define CACR_IEC	0x00008000	/* Enable instruction cache */
#define CACR_DNFB	0x00002000	/* Inhibited fill buffer */
#define CACR_IDPI	0x00001000	/* Disable CPUSHL */
#define CACR_IHLCK	0x00000800	/* Intruction cache half lock */
#define CACR_IDCM	0x00000400	/* Intruction cache inhibit */
#define CACR_ICINVA	0x00000100	/* Invalidate instr cache */
#define CACR_EUSP	0x00000020	/* Enable separate user a7 */

#define ACR_BASE_POS	24		/* Address Base */
#define ACR_MASK_POS	16		/* Address Mask */
#define ACR_ENABLE	0x00008000	/* Enable address */
#define ACR_USER	0x00000000	/* User mode access only */
#define ACR_SUPER	0x00002000	/* Supervisor mode only */
#define ACR_ANY		0x00004000	/* Match any access mode */
#define ACR_CM_WT	0x00000000	/* Write through mode */
#define ACR_CM_CP	0x00000020	/* Copyback mode */
#define ACR_CM_OFF_PRE	0x00000040	/* No cache, precise */
#define ACR_CM_OFF_IMP	0x00000060	/* No cache, imprecise */
#define ACR_CM		0x00000060	/* Cache mode mask */
#define ACR_SP		0x00000008	/* Supervisor protect */
#define ACR_WPROTECT	0x00000004	/* Write protect */

#define ACR_BA(x)	((x) & 0xff000000)
#define ACR_ADMSK(x)	((((x) - 1) & 0xff000000) >> 8)

#if defined(CONFIG_M5407)

#define ICACHE_SIZE 0x4000	/* instruction - 16k */
#define DCACHE_SIZE 0x2000	/* data - 8k */

#elif defined(CONFIG_M54xx)

#define ICACHE_SIZE 0x8000	/* instruction - 32k */
#define DCACHE_SIZE 0x8000	/* data - 32k */

#endif

#define CACHE_LINE_SIZE 0x0010	/* 16 bytes */
#define CACHE_WAYS 4		/* 4 ways */

#define ICACHE_SET_MASK	((ICACHE_SIZE / 64 - 1) << CACHE_WAYS)
#define DCACHE_SET_MASK	((DCACHE_SIZE / 64 - 1) << CACHE_WAYS)
#define ICACHE_MAX_ADDR	ICACHE_SET_MASK
#define DCACHE_MAX_ADDR	DCACHE_SET_MASK

/*
 *	Version 4 cores have a true harvard style separate instruction
 *	and data cache. Enable data and instruction caches, also enable write
 *	buffers and branch accelerator.
 */
/* attention : enabling CACR_DESB requires a "nop" to flush the store buffer */
/* use '+' instead of '|' for assembler's sake */

	/* Enable data cache */
	/* Enable data store buffer */
	/* outside ACRs : No cache, precise */
	/* Enable instruction+branch caches */
#if defined(CONFIG_M5407)
#define CACHE_MODE (CACR_DEC+CACR_DESB+CACR_DDCM_P+CACR_BEC+CACR_IEC)
#else
#define CACHE_MODE (CACR_DEC+CACR_DESB+CACR_DDCM_P+CACR_BEC+CACR_IEC+CACR_EUSP)
#endif
#define CACHE_INIT (CACR_DCINVA+CACR_BCINVA+CACR_ICINVA)

#if defined(CONFIG_MMU)
/*
 *	If running with the MMU enabled then we need to map the internal
 *	register region as non-cacheable. And then we map all our RAM as
 *	cacheable and supervisor access only.
 */
#define ACR0_MODE	(ACR_BA(CONFIG_MBAR)+ACR_ADMSK(0x1000000)+ \
			 ACR_ENABLE+ACR_SUPER+ACR_CM_OFF_PRE+ACR_SP)
#define ACR1_MODE	(ACR_BA(CONFIG_RAMBASE)+ACR_ADMSK(CONFIG_RAMSIZE)+ \
			 ACR_ENABLE+ACR_SUPER+ACR_SP)
#define ACR2_MODE	0
#define ACR3_MODE	(ACR_BA(CONFIG_RAMBASE)+ACR_ADMSK(CONFIG_RAMSIZE)+ \
			 ACR_ENABLE+ACR_SUPER+ACR_SP)

#else

/*
 *	For the non-MMU enabled case we map all of RAM as cacheable.
 */
#if defined(CONFIG_CACHE_COPYBACK)
#define DATA_CACHE_MODE (ACR_ENABLE+ACR_ANY+ACR_CM_CP)
#else
#define DATA_CACHE_MODE (ACR_ENABLE+ACR_ANY+ACR_CM_WT)
#endif
#define INSN_CACHE_MODE (ACR_ENABLE+ACR_ANY)

#define CACHE_INVALIDATE  (CACHE_MODE+CACR_DCINVA+CACR_BCINVA+CACR_ICINVA)
#define CACHE_INVALIDATEI (CACHE_MODE+CACR_BCINVA+CACR_ICINVA)
#define CACHE_INVALIDATED (CACHE_MODE+CACR_DCINVA)
#define ACR0_MODE	(0x000f0000+DATA_CACHE_MODE)
#define ACR1_MODE	0
#define ACR2_MODE	(0x000f0000+INSN_CACHE_MODE)
#define ACR3_MODE	0

#if ((DATA_CACHE_MODE & ACR_CM) == ACR_CM_CP)
/* Copyback cache mode must push dirty cache lines first */
#define	CACHE_PUSH
#endif

#endif /* CONFIG_MMU */
#endif	/* m54xxacr_h */
