/*
 * File:         include/asm-blackfin/mach-bf561/bf561.h
 * Based on:
 * Author:
 *
 * Created:
 * Description:  SYSTEM MMR REGISTER AND MEMORY MAP FOR ADSP-BF561
 *
 * Modified:
 *               Copyright 2004-2006 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef __MACH_BF561_H__
#define __MACH_BF561_H__

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

/***************************
 * Blackfin Cache setup
 */


#define BFIN_ISUBBANKS	4
#define BFIN_IWAYS		4
#define BFIN_ILINES		32

#define BFIN_DSUBBANKS	4
#define BFIN_DWAYS		2
#define BFIN_DLINES		64

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

/* IAR0 BIT FIELDS */
#define	PLL_WAKEUP_BIT		0xFFFFFFFF
#define	DMA1_ERROR_BIT		0xFFFFFF0F
#define	DMA2_ERROR_BIT		0xFFFFF0FF
#define IMDMA_ERROR_BIT		0xFFFF0FFF
#define	PPI1_ERROR_BIT		0xFFF0FFFF
#define	PPI2_ERROR_BIT		0xFF0FFFFF
#define	SPORT0_ERROR_BIT	0xF0FFFFFF
#define	SPORT1_ERROR_BIT	0x0FFFFFFF
/* IAR1 BIT FIELDS */
#define	SPI_ERROR_BIT		0xFFFFFFFF
#define	UART_ERROR_BIT		0xFFFFFF0F
#define RESERVED_ERROR_BIT	0xFFFFF0FF
#define	DMA1_0_BIT		0xFFFF0FFF
#define	DMA1_1_BIT		0xFFF0FFFF
#define	DMA1_2_BIT		0xFF0FFFFF
#define	DMA1_3_BIT		0xF0FFFFFF
#define	DMA1_4_BIT		0x0FFFFFFF
/* IAR2 BIT FIELDS */
#define	DMA1_5_BIT		0xFFFFFFFF
#define	DMA1_6_BIT		0xFFFFFF0F
#define	DMA1_7_BIT		0xFFFFF0FF
#define	DMA1_8_BIT		0xFFFF0FFF
#define	DMA1_9_BIT		0xFFF0FFFF
#define	DMA1_10_BIT		0xFF0FFFFF
#define	DMA1_11_BIT		0xF0FFFFFF
#define	DMA2_0_BIT		0x0FFFFFFF
/* IAR3 BIT FIELDS */
#define	DMA2_1_BIT		0xFFFFFFFF
#define	DMA2_2_BIT		0xFFFFFF0F
#define	DMA2_3_BIT		0xFFFFF0FF
#define	DMA2_4_BIT		0xFFFF0FFF
#define	DMA2_5_BIT		0xFFF0FFFF
#define	DMA2_6_BIT		0xFF0FFFFF
#define	DMA2_7_BIT		0xF0FFFFFF
#define	DMA2_8_BIT		0x0FFFFFFF
/* IAR4 BIT FIELDS */
#define	DMA2_9_BIT		0xFFFFFFFF
#define	DMA2_10_BIT             0xFFFFFF0F
#define	DMA2_11_BIT             0xFFFFF0FF
#define TIMER0_BIT	        0xFFFF0FFF
#define TIMER1_BIT              0xFFF0FFFF
#define TIMER2_BIT              0xFF0FFFFF
#define TIMER3_BIT              0xF0FFFFFF
#define TIMER4_BIT              0x0FFFFFFF
/* IAR5 BIT FIELDS */
#define TIMER5_BIT		0xFFFFFFFF
#define TIMER6_BIT              0xFFFFFF0F
#define TIMER7_BIT              0xFFFFF0FF
#define TIMER8_BIT              0xFFFF0FFF
#define TIMER9_BIT              0xFFF0FFFF
#define TIMER10_BIT             0xFF0FFFFF
#define TIMER11_BIT             0xF0FFFFFF
#define	PROG0_INTA_BIT	        0x0FFFFFFF
/* IAR6 BIT FIELDS */
#define	PROG0_INTB_BIT		0xFFFFFFFF
#define	PROG1_INTA_BIT          0xFFFFFF0F
#define	PROG1_INTB_BIT          0xFFFFF0FF
#define	PROG2_INTA_BIT          0xFFFF0FFF
#define	PROG2_INTB_BIT          0xFFF0FFFF
#define DMA1_WRRD0_BIT          0xFF0FFFFF
#define DMA1_WRRD1_BIT          0xF0FFFFFF
#define DMA2_WRRD0_BIT          0x0FFFFFFF
/* IAR7 BIT FIELDS */
#define DMA2_WRRD1_BIT		0xFFFFFFFF
#define IMDMA_WRRD0_BIT         0xFFFFFF0F
#define IMDMA_WRRD1_BIT         0xFFFFF0FF
#define	WATCH_BIT	        0xFFFF0FFF
#define RESERVED_1_BIT	        0xFFF0FFFF
#define RESERVED_2_BIT	        0xFF0FFFFF
#define SUPPLE_0_BIT	        0xF0FFFFFF
#define SUPPLE_1_BIT	        0x0FFFFFFF

/* Miscellaneous Values */

/****************************** EBIU Settings ********************************/
#define AMBCTL0VAL	((CONFIG_BANK_1 << 16) | CONFIG_BANK_0)
#define AMBCTL1VAL	((CONFIG_BANK_3 << 16) | CONFIG_BANK_2)

#if defined(CONFIG_C_AMBEN_ALL)
#define V_AMBEN AMBEN_ALL
#elif defined(CONFIG_C_AMBEN)
#define V_AMBEN 0x0
#elif defined(CONFIG_C_AMBEN_B0)
#define V_AMBEN AMBEN_B0
#elif defined(CONFIG_C_AMBEN_B0_B1)
#define V_AMBEN AMBEN_B0_B1
#elif defined(CONFIG_C_AMBEN_B0_B1_B2)
#define V_AMBEN AMBEN_B0_B1_B2
#endif

#ifdef CONFIG_C_AMCKEN
#define V_AMCKEN AMCKEN
#else
#define V_AMCKEN 0x0
#endif

#ifdef CONFIG_C_B0PEN
#define V_B0PEN 0x10
#else
#define V_B0PEN 0x00
#endif

#ifdef CONFIG_C_B1PEN
#define V_B1PEN 0x20
#else
#define V_B1PEN 0x00
#endif

#ifdef CONFIG_C_B2PEN
#define V_B2PEN 0x40
#else
#define V_B2PEN 0x00
#endif

#ifdef CONFIG_C_B3PEN
#define V_B3PEN 0x80
#else
#define V_B3PEN 0x00
#endif

#ifdef CONFIG_C_CDPRIO
#define V_CDPRIO 0x100
#else
#define V_CDPRIO 0x0
#endif

#define AMGCTLVAL	(V_AMBEN | V_AMCKEN | V_CDPRIO | V_B0PEN | V_B1PEN | V_B2PEN | V_B3PEN | 0x0002)

#ifdef CONFIG_BF561
#define CPU "BF561"
#define CPUID 0x027bb000
#endif
#ifndef CPU
#define CPU "UNKNOWN"
#define CPUID 0x0
#endif

#endif				/* __MACH_BF561_H__  */
