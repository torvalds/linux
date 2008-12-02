/*
 *  arch/arm/mach-pxa/include/mach/pxa-regs.h
 *
 *  Author:	Nicolas Pitre
 *  Created:	Jun 15, 2001
 *  Copyright:	MontaVista Software Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __PXA_REGS_H
#define __PXA_REGS_H

#include <mach/hardware.h>

/*
 * PXA Chip selects
 */

#define PXA_CS0_PHYS	0x00000000
#define PXA_CS1_PHYS	0x04000000
#define PXA_CS2_PHYS	0x08000000
#define PXA_CS3_PHYS	0x0C000000
#define PXA_CS4_PHYS	0x10000000
#define PXA_CS5_PHYS	0x14000000


/*
 * Personal Computer Memory Card International Association (PCMCIA) sockets
 */

#define PCMCIAPrtSp	0x04000000	/* PCMCIA Partition Space [byte]   */
#define PCMCIASp	(4*PCMCIAPrtSp)	/* PCMCIA Space [byte]             */
#define PCMCIAIOSp	PCMCIAPrtSp	/* PCMCIA I/O Space [byte]         */
#define PCMCIAAttrSp	PCMCIAPrtSp	/* PCMCIA Attribute Space [byte]   */
#define PCMCIAMemSp	PCMCIAPrtSp	/* PCMCIA Memory Space [byte]      */

#define PCMCIA0Sp	PCMCIASp	/* PCMCIA 0 Space [byte]           */
#define PCMCIA0IOSp	PCMCIAIOSp	/* PCMCIA 0 I/O Space [byte]       */
#define PCMCIA0AttrSp	PCMCIAAttrSp	/* PCMCIA 0 Attribute Space [byte] */
#define PCMCIA0MemSp	PCMCIAMemSp	/* PCMCIA 0 Memory Space [byte]    */

#define PCMCIA1Sp	PCMCIASp	/* PCMCIA 1 Space [byte]           */
#define PCMCIA1IOSp	PCMCIAIOSp	/* PCMCIA 1 I/O Space [byte]       */
#define PCMCIA1AttrSp	PCMCIAAttrSp	/* PCMCIA 1 Attribute Space [byte] */
#define PCMCIA1MemSp	PCMCIAMemSp	/* PCMCIA 1 Memory Space [byte]    */

#define _PCMCIA(Nb)	        	/* PCMCIA [0..1]                   */ \
                	(0x20000000 + (Nb)*PCMCIASp)
#define _PCMCIAIO(Nb)	_PCMCIA (Nb)	/* PCMCIA I/O [0..1]               */
#define _PCMCIAAttr(Nb)	        	/* PCMCIA Attribute [0..1]         */ \
                	(_PCMCIA (Nb) + 2*PCMCIAPrtSp)
#define _PCMCIAMem(Nb)	        	/* PCMCIA Memory [0..1]            */ \
                	(_PCMCIA (Nb) + 3*PCMCIAPrtSp)

#define _PCMCIA0	_PCMCIA (0)	/* PCMCIA 0                        */
#define _PCMCIA0IO	_PCMCIAIO (0)	/* PCMCIA 0 I/O                    */
#define _PCMCIA0Attr	_PCMCIAAttr (0)	/* PCMCIA 0 Attribute              */
#define _PCMCIA0Mem	_PCMCIAMem (0)	/* PCMCIA 0 Memory                 */

#define _PCMCIA1	_PCMCIA (1)	/* PCMCIA 1                        */
#define _PCMCIA1IO	_PCMCIAIO (1)	/* PCMCIA 1 I/O                    */
#define _PCMCIA1Attr	_PCMCIAAttr (1)	/* PCMCIA 1 Attribute              */
#define _PCMCIA1Mem	_PCMCIAMem (1)	/* PCMCIA 1 Memory                 */



/*
 * DMA Controller
 */
#define DCSR(x)		__REG2(0x40000000, (x) << 2)

#define DCSR_RUN	(1 << 31)	/* Run Bit (read / write) */
#define DCSR_NODESC	(1 << 30)	/* No-Descriptor Fetch (read / write) */
#define DCSR_STOPIRQEN	(1 << 29)	/* Stop Interrupt Enable (read / write) */
#define DCSR_REQPEND	(1 << 8)	/* Request Pending (read-only) */
#define DCSR_STOPSTATE	(1 << 3)	/* Stop State (read-only) */
#define DCSR_ENDINTR	(1 << 2)	/* End Interrupt (read / write) */
#define DCSR_STARTINTR	(1 << 1)	/* Start Interrupt (read / write) */
#define DCSR_BUSERR	(1 << 0)	/* Bus Error Interrupt (read / write) */

#if defined(CONFIG_PXA27x) || defined(CONFIG_PXA3xx)
#define DCSR_EORIRQEN	(1 << 28)       /* End of Receive Interrupt Enable (R/W) */
#define DCSR_EORJMPEN	(1 << 27)       /* Jump to next descriptor on EOR */
#define DCSR_EORSTOPEN	(1 << 26)       /* STOP on an EOR */
#define DCSR_SETCMPST	(1 << 25)       /* Set Descriptor Compare Status */
#define DCSR_CLRCMPST	(1 << 24)       /* Clear Descriptor Compare Status */
#define DCSR_CMPST	(1 << 10)       /* The Descriptor Compare Status */
#define DCSR_EORINTR	(1 << 9)        /* The end of Receive */
#endif

#define DALGN		__REG(0x400000a0)  /* DMA Alignment Register */
#define DINT		__REG(0x400000f0)  /* DMA Interrupt Register */

#define DRCMR(n)	(*(((n) < 64) ? \
			&__REG2(0x40000100, ((n) & 0x3f) << 2) : \
			&__REG2(0x40001100, ((n) & 0x3f) << 2)))

#define DRCMR_MAPVLD	(1 << 7)	/* Map Valid (read / write) */
#define DRCMR_CHLNUM	0x1f		/* mask for Channel Number (read / write) */

#define DDADR(x)	__REG2(0x40000200, (x) << 4)
#define DSADR(x)	__REG2(0x40000204, (x) << 4)
#define DTADR(x)	__REG2(0x40000208, (x) << 4)
#define DCMD(x)		__REG2(0x4000020c, (x) << 4)

#define DDADR_DESCADDR	0xfffffff0	/* Address of next descriptor (mask) */
#define DDADR_STOP	(1 << 0)	/* Stop (read / write) */

#define DCMD_INCSRCADDR	(1 << 31)	/* Source Address Increment Setting. */
#define DCMD_INCTRGADDR	(1 << 30)	/* Target Address Increment Setting. */
#define DCMD_FLOWSRC	(1 << 29)	/* Flow Control by the source. */
#define DCMD_FLOWTRG	(1 << 28)	/* Flow Control by the target. */
#define DCMD_STARTIRQEN	(1 << 22)	/* Start Interrupt Enable */
#define DCMD_ENDIRQEN	(1 << 21)	/* End Interrupt Enable */
#define DCMD_ENDIAN	(1 << 18)	/* Device Endian-ness. */
#define DCMD_BURST8	(1 << 16)	/* 8 byte burst */
#define DCMD_BURST16	(2 << 16)	/* 16 byte burst */
#define DCMD_BURST32	(3 << 16)	/* 32 byte burst */
#define DCMD_WIDTH1	(1 << 14)	/* 1 byte width */
#define DCMD_WIDTH2	(2 << 14)	/* 2 byte width (HalfWord) */
#define DCMD_WIDTH4	(3 << 14)	/* 4 byte width (Word) */
#define DCMD_LENGTH	0x01fff		/* length mask (max = 8K - 1) */

/*
 * Real Time Clock
 */

#define RCNR		__REG(0x40900000)  /* RTC Count Register */
#define RTAR		__REG(0x40900004)  /* RTC Alarm Register */
#define RTSR		__REG(0x40900008)  /* RTC Status Register */
#define RTTR		__REG(0x4090000C)  /* RTC Timer Trim Register */
#define PIAR		__REG(0x40900038)  /* Periodic Interrupt Alarm Register */

#define RTSR_PICE	(1 << 15)	/* Periodic interrupt count enable */
#define RTSR_PIALE	(1 << 14)	/* Periodic interrupt Alarm enable */
#define RTSR_HZE	(1 << 3)	/* HZ interrupt enable */
#define RTSR_ALE	(1 << 2)	/* RTC alarm interrupt enable */
#define RTSR_HZ		(1 << 1)	/* HZ rising-edge detected */
#define RTSR_AL		(1 << 0)	/* RTC alarm detected */


/*
 * OS Timer & Match Registers
 */

#define OSMR0		__REG(0x40A00000)  /* */
#define OSMR1		__REG(0x40A00004)  /* */
#define OSMR2		__REG(0x40A00008)  /* */
#define OSMR3		__REG(0x40A0000C)  /* */
#define OSMR4		__REG(0x40A00080)  /* */
#define OSCR		__REG(0x40A00010)  /* OS Timer Counter Register */
#define OSCR4		__REG(0x40A00040)  /* OS Timer Counter Register */
#define OMCR4		__REG(0x40A000C0)  /* */
#define OSSR		__REG(0x40A00014)  /* OS Timer Status Register */
#define OWER		__REG(0x40A00018)  /* OS Timer Watchdog Enable Register */
#define OIER		__REG(0x40A0001C)  /* OS Timer Interrupt Enable Register */

#define OSSR_M3		(1 << 3)	/* Match status channel 3 */
#define OSSR_M2		(1 << 2)	/* Match status channel 2 */
#define OSSR_M1		(1 << 1)	/* Match status channel 1 */
#define OSSR_M0		(1 << 0)	/* Match status channel 0 */

#define OWER_WME	(1 << 0)	/* Watchdog Match Enable */

#define OIER_E3		(1 << 3)	/* Interrupt enable channel 3 */
#define OIER_E2		(1 << 2)	/* Interrupt enable channel 2 */
#define OIER_E1		(1 << 1)	/* Interrupt enable channel 1 */
#define OIER_E0		(1 << 0)	/* Interrupt enable channel 0 */


/*
 * Interrupt Controller
 */

#define ICIP		__REG(0x40D00000)  /* Interrupt Controller IRQ Pending Register */
#define ICMR		__REG(0x40D00004)  /* Interrupt Controller Mask Register */
#define ICLR		__REG(0x40D00008)  /* Interrupt Controller Level Register */
#define ICFP		__REG(0x40D0000C)  /* Interrupt Controller FIQ Pending Register */
#define ICPR		__REG(0x40D00010)  /* Interrupt Controller Pending Register */
#define ICCR		__REG(0x40D00014)  /* Interrupt Controller Control Register */

#define ICIP2		__REG(0x40D0009C)  /* Interrupt Controller IRQ Pending Register 2 */
#define ICMR2		__REG(0x40D000A0)  /* Interrupt Controller Mask Register 2 */
#define ICLR2		__REG(0x40D000A4)  /* Interrupt Controller Level Register 2 */
#define ICFP2		__REG(0x40D000A8)  /* Interrupt Controller FIQ Pending Register 2 */
#define ICPR2		__REG(0x40D000AC)  /* Interrupt Controller Pending Register 2 */

/*
 * General Purpose I/O
 */

#define GPLR0		__REG(0x40E00000)  /* GPIO Pin-Level Register GPIO<31:0> */
#define GPLR1		__REG(0x40E00004)  /* GPIO Pin-Level Register GPIO<63:32> */
#define GPLR2		__REG(0x40E00008)  /* GPIO Pin-Level Register GPIO<80:64> */

#define GPDR0		__REG(0x40E0000C)  /* GPIO Pin Direction Register GPIO<31:0> */
#define GPDR1		__REG(0x40E00010)  /* GPIO Pin Direction Register GPIO<63:32> */
#define GPDR2		__REG(0x40E00014)  /* GPIO Pin Direction Register GPIO<80:64> */

#define GPSR0		__REG(0x40E00018)  /* GPIO Pin Output Set Register GPIO<31:0> */
#define GPSR1		__REG(0x40E0001C)  /* GPIO Pin Output Set Register GPIO<63:32> */
#define GPSR2		__REG(0x40E00020)  /* GPIO Pin Output Set Register GPIO<80:64> */

#define GPCR0		__REG(0x40E00024)  /* GPIO Pin Output Clear Register GPIO<31:0> */
#define GPCR1		__REG(0x40E00028)  /* GPIO Pin Output Clear Register GPIO <63:32> */
#define GPCR2		__REG(0x40E0002C)  /* GPIO Pin Output Clear Register GPIO <80:64> */

#define GRER0		__REG(0x40E00030)  /* GPIO Rising-Edge Detect Register GPIO<31:0> */
#define GRER1		__REG(0x40E00034)  /* GPIO Rising-Edge Detect Register GPIO<63:32> */
#define GRER2		__REG(0x40E00038)  /* GPIO Rising-Edge Detect Register GPIO<80:64> */

#define GFER0		__REG(0x40E0003C)  /* GPIO Falling-Edge Detect Register GPIO<31:0> */
#define GFER1		__REG(0x40E00040)  /* GPIO Falling-Edge Detect Register GPIO<63:32> */
#define GFER2		__REG(0x40E00044)  /* GPIO Falling-Edge Detect Register GPIO<80:64> */

#define GEDR0		__REG(0x40E00048)  /* GPIO Edge Detect Status Register GPIO<31:0> */
#define GEDR1		__REG(0x40E0004C)  /* GPIO Edge Detect Status Register GPIO<63:32> */
#define GEDR2		__REG(0x40E00050)  /* GPIO Edge Detect Status Register GPIO<80:64> */

#define GAFR0_L		__REG(0x40E00054)  /* GPIO Alternate Function Select Register GPIO<15:0> */
#define GAFR0_U		__REG(0x40E00058)  /* GPIO Alternate Function Select Register GPIO<31:16> */
#define GAFR1_L		__REG(0x40E0005C)  /* GPIO Alternate Function Select Register GPIO<47:32> */
#define GAFR1_U		__REG(0x40E00060)  /* GPIO Alternate Function Select Register GPIO<63:48> */
#define GAFR2_L		__REG(0x40E00064)  /* GPIO Alternate Function Select Register GPIO<79:64> */
#define GAFR2_U		__REG(0x40E00068)  /* GPIO Alternate Function Select Register GPIO<95-80> */
#define GAFR3_L		__REG(0x40E0006C)  /* GPIO Alternate Function Select Register GPIO<111:96> */
#define GAFR3_U		__REG(0x40E00070)  /* GPIO Alternate Function Select Register GPIO<127:112> */

#define GPLR3		__REG(0x40E00100)  /* GPIO Pin-Level Register GPIO<127:96> */
#define GPDR3		__REG(0x40E0010C)  /* GPIO Pin Direction Register GPIO<127:96> */
#define GPSR3		__REG(0x40E00118)  /* GPIO Pin Output Set Register GPIO<127:96> */
#define GPCR3		__REG(0x40E00124)  /* GPIO Pin Output Clear Register GPIO<127:96> */
#define GRER3		__REG(0x40E00130)  /* GPIO Rising-Edge Detect Register GPIO<127:96> */
#define GFER3		__REG(0x40E0013C)  /* GPIO Falling-Edge Detect Register GPIO<127:96> */
#define GEDR3		__REG(0x40E00148)  /* GPIO Edge Detect Status Register GPIO<127:96> */

/* More handy macros.  The argument is a literal GPIO number. */

#define GPIO_bit(x)	(1 << ((x) & 0x1f))

#define _GPLR(x)	__REG2(0x40E00000, ((x) & 0x60) >> 3)
#define _GPDR(x)	__REG2(0x40E0000C, ((x) & 0x60) >> 3)
#define _GPSR(x)	__REG2(0x40E00018, ((x) & 0x60) >> 3)
#define _GPCR(x)	__REG2(0x40E00024, ((x) & 0x60) >> 3)
#define _GRER(x)	__REG2(0x40E00030, ((x) & 0x60) >> 3)
#define _GFER(x)	__REG2(0x40E0003C, ((x) & 0x60) >> 3)
#define _GEDR(x)	__REG2(0x40E00048, ((x) & 0x60) >> 3)
#define _GAFR(x)	__REG2(0x40E00054, ((x) & 0x70) >> 2)

#define GPLR(x) 	(*((((x) & 0x7f) < 96) ? &_GPLR(x) : &GPLR3))
#define GPDR(x)		(*((((x) & 0x7f) < 96) ? &_GPDR(x) : &GPDR3))
#define GPSR(x)		(*((((x) & 0x7f) < 96) ? &_GPSR(x) : &GPSR3))
#define GPCR(x)		(*((((x) & 0x7f) < 96) ? &_GPCR(x) : &GPCR3))
#define GRER(x)		(*((((x) & 0x7f) < 96) ? &_GRER(x) : &GRER3))
#define GFER(x)		(*((((x) & 0x7f) < 96) ? &_GFER(x) : &GFER3))
#define GEDR(x)		(*((((x) & 0x7f) < 96) ? &_GEDR(x) : &GEDR3))
#define GAFR(x)		(*((((x) & 0x7f) < 96) ? &_GAFR(x) : \
			 ((((x) & 0x7f) < 112) ? &GAFR3_L : &GAFR3_U)))

#endif
