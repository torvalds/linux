#ifndef _M32104UT_M32104UT_PLD_H
#define _M32104UT_M32104UT_PLD_H

/*
 * include/asm-m32r/m32104ut/m32104ut_pld.h
 *
 * Definitions for Programable Logic Device(PLD) on M32104UT board.
 * Based on m32700ut_pld.h
 *
 * Copyright (c) 2002	Takeo Takahashi
 * Copyright (c) 2005	Naoto Sugai
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file "COPYING" in the main directory of
 * this archive for more details.
 */

#if defined(CONFIG_PLAT_M32104UT)
#define PLD_PLAT_BASE		0x02c00000
#else
#error "no platform configuration"
#endif

#ifndef __ASSEMBLY__
/*
 * C functions use non-cache address.
 */
#define PLD_BASE		(PLD_PLAT_BASE /* + NONCACHE_OFFSET */)
#define __reg8			(volatile unsigned char *)
#define __reg16			(volatile unsigned short *)
#define __reg32			(volatile unsigned int *)
#else
#define PLD_BASE		(PLD_PLAT_BASE + NONCACHE_OFFSET)
#define __reg8
#define __reg16
#define __reg32
#endif /* __ASSEMBLY__ */

/* CFC */
#define	PLD_CFRSTCR		__reg16(PLD_BASE + 0x0000)
#define PLD_CFSTS		__reg16(PLD_BASE + 0x0002)
#define PLD_CFIMASK		__reg16(PLD_BASE + 0x0004)
#define PLD_CFBUFCR		__reg16(PLD_BASE + 0x0006)

/* MMC */
#define PLD_MMCCR		__reg16(PLD_BASE + 0x4000)
#define PLD_MMCMOD		__reg16(PLD_BASE + 0x4002)
#define PLD_MMCSTS		__reg16(PLD_BASE + 0x4006)
#define PLD_MMCBAUR		__reg16(PLD_BASE + 0x400a)
#define PLD_MMCCMDBCUT		__reg16(PLD_BASE + 0x400c)
#define PLD_MMCCDTBCUT		__reg16(PLD_BASE + 0x400e)
#define PLD_MMCDET		__reg16(PLD_BASE + 0x4010)
#define PLD_MMCWP		__reg16(PLD_BASE + 0x4012)
#define PLD_MMCWDATA		__reg16(PLD_BASE + 0x5000)
#define PLD_MMCRDATA		__reg16(PLD_BASE + 0x6000)
#define PLD_MMCCMDDATA		__reg16(PLD_BASE + 0x7000)
#define PLD_MMCRSPDATA		__reg16(PLD_BASE + 0x7006)

/* ICU
 *  ICUISTS:	status register
 *  ICUIREQ0: 	request register
 *  ICUIREQ1: 	request register
 *  ICUCR3:	control register for CFIREQ# interrupt
 *  ICUCR4:	control register for CFC Card insert interrupt
 *  ICUCR5:	control register for CFC Card eject interrupt
 *  ICUCR6:	control register for external interrupt
 *  ICUCR11:	control register for MMC Card insert/eject interrupt
 *  ICUCR13:	control register for SC error interrupt
 *  ICUCR14:	control register for SC receive interrupt
 *  ICUCR15:	control register for SC send interrupt
 */

#define PLD_IRQ_INT0		(M32104UT_PLD_IRQ_BASE + 0)	/* None */
#define PLD_IRQ_CFIREQ		(M32104UT_PLD_IRQ_BASE + 3)	/* CF IREQ */
#define PLD_IRQ_CFC_INSERT	(M32104UT_PLD_IRQ_BASE + 4)	/* CF Insert */
#define PLD_IRQ_CFC_EJECT	(M32104UT_PLD_IRQ_BASE + 5)	/* CF Eject */
#define PLD_IRQ_EXINT		(M32104UT_PLD_IRQ_BASE + 6)	/* EXINT */
#define PLD_IRQ_MMCCARD		(M32104UT_PLD_IRQ_BASE + 11)	/* MMC Insert/Eject */
#define PLD_IRQ_SC_ERROR	(M32104UT_PLD_IRQ_BASE + 13)	/* SC error */
#define PLD_IRQ_SC_RCV		(M32104UT_PLD_IRQ_BASE + 14)	/* SC receive */
#define PLD_IRQ_SC_SND		(M32104UT_PLD_IRQ_BASE + 15)	/* SC send */

#define PLD_ICUISTS		__reg16(PLD_BASE + 0x8002)
#define PLD_ICUISTS_VECB_MASK	(0xf000)
#define PLD_ICUISTS_VECB(x)	((x) & PLD_ICUISTS_VECB_MASK)
#define PLD_ICUISTS_ISN_MASK	(0x07c0)
#define PLD_ICUISTS_ISN(x)	((x) & PLD_ICUISTS_ISN_MASK)
#define PLD_ICUCR3		__reg16(PLD_BASE + 0x8104)
#define PLD_ICUCR4		__reg16(PLD_BASE + 0x8106)
#define PLD_ICUCR5		__reg16(PLD_BASE + 0x8108)
#define PLD_ICUCR6		__reg16(PLD_BASE + 0x810a)
#define PLD_ICUCR11		__reg16(PLD_BASE + 0x8114)
#define PLD_ICUCR13		__reg16(PLD_BASE + 0x8118)
#define PLD_ICUCR14		__reg16(PLD_BASE + 0x811a)
#define PLD_ICUCR15		__reg16(PLD_BASE + 0x811c)
#define PLD_ICUCR_IEN		(0x1000)
#define PLD_ICUCR_IREQ		(0x0100)
#define PLD_ICUCR_ISMOD00	(0x0000)	/* Low edge */
#define PLD_ICUCR_ISMOD01	(0x0010)	/* Low level */
#define PLD_ICUCR_ISMOD02	(0x0020)	/* High edge */
#define PLD_ICUCR_ISMOD03	(0x0030)	/* High level */
#define PLD_ICUCR_ILEVEL0	(0x0000)
#define PLD_ICUCR_ILEVEL1	(0x0001)
#define PLD_ICUCR_ILEVEL2	(0x0002)
#define PLD_ICUCR_ILEVEL3	(0x0003)
#define PLD_ICUCR_ILEVEL4	(0x0004)
#define PLD_ICUCR_ILEVEL5	(0x0005)
#define PLD_ICUCR_ILEVEL6	(0x0006)
#define PLD_ICUCR_ILEVEL7	(0x0007)

/* Power Control of MMC and CF */
#define PLD_CPCR		__reg16(PLD_BASE + 0x14000)
#define PLD_CPCR_CDP		0x0001

/* LED Control
 *
 * 1: DIP swich side
 * 2: Reset switch side
 */
#define PLD_IOLEDCR		__reg16(PLD_BASE + 0x14002)
#define PLD_IOLED_1_ON		0x001
#define PLD_IOLED_1_OFF		0x000
#define PLD_IOLED_2_ON		0x002
#define PLD_IOLED_2_OFF		0x000

/* DIP Switch
 *  0: Write-protect of Flash Memory (0:protected, 1:non-protected)
 *  1: -
 *  2: -
 *  3: -
 */
#define PLD_IOSWSTS		__reg16(PLD_BASE + 0x14004)
#define	PLD_IOSWSTS_IOSW2	0x0200
#define	PLD_IOSWSTS_IOSW1	0x0100
#define	PLD_IOSWSTS_IOWP0	0x0001

/* CRC */
#define PLD_CRC7DATA		__reg16(PLD_BASE + 0x18000)
#define PLD_CRC7INDATA		__reg16(PLD_BASE + 0x18002)
#define PLD_CRC16DATA		__reg16(PLD_BASE + 0x18004)
#define PLD_CRC16INDATA		__reg16(PLD_BASE + 0x18006)
#define PLD_CRC16ADATA		__reg16(PLD_BASE + 0x18008)
#define PLD_CRC16AINDATA	__reg16(PLD_BASE + 0x1800a)

/* RTC */
#define PLD_RTCCR		__reg16(PLD_BASE + 0x1c000)
#define PLD_RTCBAUR		__reg16(PLD_BASE + 0x1c002)
#define PLD_RTCWRDATA		__reg16(PLD_BASE + 0x1c004)
#define PLD_RTCRDDATA		__reg16(PLD_BASE + 0x1c006)
#define PLD_RTCRSTODT		__reg16(PLD_BASE + 0x1c008)

/* SIM Card */
#define PLD_SCCR		__reg16(PLD_BASE + 0x38000)
#define PLD_SCMOD		__reg16(PLD_BASE + 0x38004)
#define PLD_SCSTS		__reg16(PLD_BASE + 0x38006)
#define PLD_SCINTCR		__reg16(PLD_BASE + 0x38008)
#define PLD_SCBAUR		__reg16(PLD_BASE + 0x3800a)
#define PLD_SCTXB		__reg16(PLD_BASE + 0x3800c)
#define PLD_SCRXB		__reg16(PLD_BASE + 0x3800e)

#endif /* _M32104UT_M32104UT_PLD_H */
