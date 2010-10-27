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
#define ACR_WPROTECT	0x00000004	/* Write protect */

#endif	/* m54xxacr_h */
