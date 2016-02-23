/*
 * Copyright 2007-2009 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef __MACH_BF527_H__
#define __MACH_BF527_H__

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
#define IMASK_IVGTMR	0x0040
#define IMASK_IVGHW		0x0020

/***************************/

#define BFIN_DSUBBANKS	4
#define BFIN_DWAYS		2
#define BFIN_DLINES		64
#define BFIN_ISUBBANKS	4
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
#ifdef CONFIG_C_CDPRIO
#define V_CDPRIO 0x100
#else
#define V_CDPRIO 0x0
#endif

#define AMGCTLVAL	(V_AMBEN | V_AMCKEN | V_CDPRIO)

/**************************** Hysteresis Settings ****************************/

#ifdef CONFIG_BFIN_HYSTERESIS_CONTROL
#ifdef CONFIG_GPIO_HYST_PORTF_0_7
#define HYST_PORTF_0_7		(1 << 0)
#else
#define HYST_PORTF_0_7		(0 << 0)
#endif
#ifdef CONFIG_GPIO_HYST_PORTF_8_9
#define HYST_PORTF_8_9		(1 << 2)
#else
#define HYST_PORTF_8_9		(0 << 2)
#endif
#ifdef CONFIG_GPIO_HYST_PORTF_10
#define HYST_PORTF_10		(1 << 4)
#else
#define HYST_PORTF_10		(0 << 4)
#endif
#ifdef CONFIG_GPIO_HYST_PORTF_11
#define HYST_PORTF_11		(1 << 6)
#else
#define HYST_PORTF_11		(0 << 6)
#endif
#ifdef CONFIG_GPIO_HYST_PORTF_12_13
#define HYST_PORTF_12_13	(1 << 8)
#else
#define HYST_PORTF_12_13	(0 << 8)
#endif
#ifdef CONFIG_GPIO_HYST_PORTF_14_15
#define HYST_PORTF_14_15	(1 << 10)
#else
#define HYST_PORTF_14_15	(0 << 10)
#endif

#define HYST_PORTF_0_15	(HYST_PORTF_0_7 | HYST_PORTF_8_9 | HYST_PORTF_10 | \
		HYST_PORTF_11 | HYST_PORTF_12_13 | HYST_PORTF_14_15)

#ifdef CONFIG_GPIO_HYST_PORTG_0
#define HYST_PORTG_0		(1 << 0)
#else
#define HYST_PORTG_0		(0 << 0)
#endif
#ifdef CONFIG_GPIO_HYST_PORTG_1_4
#define HYST_PORTG_1_4		(1 << 2)
#else
#define HYST_PORTG_1_4		(0 << 2)
#endif
#ifdef CONFIG_GPIO_HYST_PORTG_5_6
#define HYST_PORTG_5_6		(1 << 4)
#else
#define HYST_PORTG_5_6		(0 << 4)
#endif
#ifdef CONFIG_GPIO_HYST_PORTG_7_8
#define HYST_PORTG_7_8		(1 << 6)
#else
#define HYST_PORTG_7_8		(0 << 6)
#endif
#ifdef CONFIG_GPIO_HYST_PORTG_9
#define HYST_PORTG_9		(1 << 8)
#else
#define HYST_PORTG_9		(0 << 8)
#endif
#ifdef CONFIG_GPIO_HYST_PORTG_10
#define HYST_PORTG_10		(1 << 10)
#else
#define HYST_PORTG_10		(0 << 10)
#endif
#ifdef CONFIG_GPIO_HYST_PORTG_11_13
#define HYST_PORTG_11_13	(1 << 12)
#else
#define HYST_PORTG_11_13	(0 << 12)
#endif
#ifdef CONFIG_GPIO_HYST_PORTG_14_15
#define HYST_PORTG_14_15	(1 << 14)
#else
#define HYST_PORTG_14_15	(0 << 14)
#endif

#define HYST_PORTG_0_15	(HYST_PORTG_0 | HYST_PORTG_1_4 | HYST_PORTG_5_6 | \
		HYST_PORTG_7_8 | HYST_PORTG_9 | HYST_PORTG_10 | \
		HYST_PORTG_11_13 | HYST_PORTG_14_15)

#ifdef CONFIG_GPIO_HYST_PORTH_0_7
#define HYST_PORTH_0_7		(1 << 0)
#else
#define HYST_PORTH_0_7		(0 << 0)
#endif
#ifdef CONFIG_GPIO_HYST_PORTH_8
#define HYST_PORTH_8		(1 << 2)
#else
#define HYST_PORTH_8		(0 << 2)
#endif
#ifdef CONFIG_GPIO_HYST_PORTH_9_15
#define HYST_PORTH_9_15		(1 << 4)
#else
#define HYST_PORTH_9_15		(0 << 4)
#endif

#define HYST_PORTH_0_15	(HYST_PORTH_0_7 | HYST_PORTH_8 | HYST_PORTH_9_15)

#ifdef CONFIG_NONEGPIO_HYST_TMR0_FS1_PPICLK
#define HYST_TMR0_FS1_PPICLK		(1 << 0)
#else
#define HYST_TMR0_FS1_PPICLK		(0 << 0)
#endif
#ifdef CONFIG_NONEGPIO_HYST_NMI_RST_BMODE
#define HYST_NMI_RST_BMODE		(1 << 2)
#else
#define HYST_NMI_RST_BMODE		(0 << 2)
#endif
#ifdef CONFIG_NONEGPIO_HYST_JTAG
#define HYST_JTAG			(1 << 4)
#else
#define HYST_JTAG			(0 << 4)
#endif

#define HYST_NONEGPIO	(HYST_TMR0_FS1_PPICLK | HYST_NMI_RST_BMODE | HYST_JTAG)
#define HYST_NONEGPIO_MASK		(0x3F)
#endif /* CONFIG_BFIN_HYSTERESIS_CONTROL */

#ifdef CONFIG_BF527
#define CPU "BF527"
#define CPUID 0x27e0
#endif
#ifdef CONFIG_BF526
#define CPU "BF526"
#define CPUID 0x27e4
#endif
#ifdef CONFIG_BF525
#define CPU "BF525"
#define CPUID 0x27e0
#endif
#ifdef CONFIG_BF524
#define CPU "BF524"
#define CPUID 0x27e4
#endif
#ifdef CONFIG_BF523
#define CPU "BF523"
#define CPUID 0x27e0
#endif
#ifdef CONFIG_BF522
#define CPU "BF522"
#define CPUID 0x27e4
#endif

#ifndef CPU
#error "Unknown CPU type - This kernel doesn't seem to be configured properly"
#endif

#endif				/* __MACH_BF527_H__  */
