/*
 * Copyright 2011 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef __MACH_BF609_H__
#define __MACH_BF609_H__

#define OFFSET_(x) ((x) & 0x0000FFFF)

/*some misc defines*/
#define IMASK_IVG15		0x8000
#define IMASK_IVG14		0x4000
#define IMASK_IVG13		0x2000
#define IMASK_IVG12		0x1000

#define IMASK_IVG11		0x0800
#define IMASK_IVG10		0x0400
#define IMASK_IVG9		0x0200
#define IMASK_IVG8		0x0100

#define IMASK_IVG7		0x0080
#define IMASK_IVGTMR		0x0040
#define IMASK_IVGHW		0x0020

/***************************/


#define BFIN_DSUBBANKS		4
#define BFIN_DWAYS		2
#define BFIN_DLINES		64
#define BFIN_ISUBBANKS		4
#define BFIN_IWAYS		4
#define BFIN_ILINES		32

#define WAY0_L			0x1
#define WAY1_L			0x2
#define WAY01_L			0x3
#define WAY2_L			0x4
#define WAY02_L			0x5
#define	WAY12_L			0x6
#define	WAY012_L		0x7

#define	WAY3_L			0x8
#define	WAY03_L			0x9
#define	WAY13_L			0xA
#define	WAY013_L		0xB

#define	WAY32_L			0xC
#define	WAY320_L		0xD
#define	WAY321_L		0xE
#define	WAYALL_L		0xF

#define DMC_ENABLE (2<<2)	/*yes, 2, not 1 */

/********************************* EBIU Settings ************************************/
#define AMBCTL0VAL	((CONFIG_BANK_1 << 16) | CONFIG_BANK_0)
#define AMBCTL1VAL	((CONFIG_BANK_3 << 16) | CONFIG_BANK_2)

#ifdef CONFIG_C_AMBEN_ALL
#define V_AMBEN AMBEN_ALL
#endif
#ifdef CONFIG_C_AMBEN
#define V_AMBEN 0x0
#endif
#ifdef CONFIG_C_AMBEN_B0
#define V_AMBEN AMBEN_B0
#endif
#ifdef CONFIG_C_AMBEN_B0_B1
#define V_AMBEN AMBEN_B0_B1
#endif
#ifdef CONFIG_C_AMBEN_B0_B1_B2
#define V_AMBEN AMBEN_B0_B1_B2
#endif
#ifdef CONFIG_C_AMCKEN
#define V_AMCKEN AMCKEN
#else
#define V_AMCKEN 0x0
#endif

#define AMGCTLVAL	(V_AMBEN | V_AMCKEN)

#if defined(CONFIG_BF609)
# define CPU   "BF609"
# define CPUID 0x27fe	/* temperary fake value */
#endif

#ifndef CPU
#error "Unknown CPU type - This kernel doesn't seem to be configured properly"
#endif

#endif	/* __MACH_BF609_H__  */
