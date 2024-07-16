/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  arch/arm/include/asm/hardware/iomd.h
 *
 *  Copyright (C) 1999 Russell King
 *
 *  This file contains information out the IOMD ASIC used in the
 *  Acorn RiscPC and subsequently integrated into the CLPS7500 chips.
 */
#ifndef __ASMARM_HARDWARE_IOMD_H
#define __ASMARM_HARDWARE_IOMD_H


#ifndef __ASSEMBLY__

/*
 * We use __raw_base variants here so that we give the compiler the
 * chance to keep IOC_BASE in a register.
 */
#define iomd_readb(off)		__raw_readb(IOMD_BASE + (off))
#define iomd_readl(off)		__raw_readl(IOMD_BASE + (off))
#define iomd_writeb(val,off)	__raw_writeb(val, IOMD_BASE + (off))
#define iomd_writel(val,off)	__raw_writel(val, IOMD_BASE + (off))

#endif

#define IOMD_CONTROL	(0x000)
#define IOMD_KARTTX	(0x004)
#define IOMD_KARTRX	(0x004)
#define IOMD_KCTRL	(0x008)

#define IOMD_IRQSTATA	(0x010)
#define IOMD_IRQREQA	(0x014)
#define IOMD_IRQCLRA	(0x014)
#define IOMD_IRQMASKA	(0x018)

#define IOMD_IRQSTATB	(0x020)
#define IOMD_IRQREQB	(0x024)
#define IOMD_IRQMASKB	(0x028)

#define IOMD_FIQSTAT	(0x030)
#define IOMD_FIQREQ	(0x034)
#define IOMD_FIQMASK	(0x038)

#define IOMD_T0CNTL	(0x040)
#define IOMD_T0LTCHL	(0x040)
#define IOMD_T0CNTH	(0x044)
#define IOMD_T0LTCHH	(0x044)
#define IOMD_T0GO	(0x048)
#define IOMD_T0LATCH	(0x04c)

#define IOMD_T1CNTL	(0x050)
#define IOMD_T1LTCHL	(0x050)
#define IOMD_T1CNTH	(0x054)
#define IOMD_T1LTCHH	(0x054)
#define IOMD_T1GO	(0x058)
#define IOMD_T1LATCH	(0x05c)

#define IOMD_ROMCR0	(0x080)
#define IOMD_ROMCR1	(0x084)
#ifdef CONFIG_ARCH_RPC
#define IOMD_DRAMCR	(0x088)
#endif
#define IOMD_REFCR	(0x08C)

#define IOMD_FSIZE	(0x090)
#define IOMD_ID0	(0x094)
#define IOMD_ID1	(0x098)
#define IOMD_VERSION	(0x09C)

#ifdef CONFIG_ARCH_RPC
#define IOMD_MOUSEX	(0x0A0)
#define IOMD_MOUSEY	(0x0A4)
#endif

#ifdef CONFIG_ARCH_RPC
#define IOMD_DMATCR	(0x0C0)
#endif
#define IOMD_IOTCR	(0x0C4)
#define IOMD_ECTCR	(0x0C8)
#ifdef CONFIG_ARCH_RPC
#define IOMD_DMAEXT	(0x0CC)
#endif

#ifdef CONFIG_ARCH_RPC
#define DMA_EXT_IO0	1
#define DMA_EXT_IO1	2
#define DMA_EXT_IO2	4
#define DMA_EXT_IO3	8

#define IOMD_IO0CURA	(0x100)
#define IOMD_IO0ENDA	(0x104)
#define IOMD_IO0CURB	(0x108)
#define IOMD_IO0ENDB	(0x10C)
#define IOMD_IO0CR	(0x110)
#define IOMD_IO0ST	(0x114)

#define IOMD_IO1CURA	(0x120)
#define IOMD_IO1ENDA	(0x124)
#define IOMD_IO1CURB	(0x128)
#define IOMD_IO1ENDB	(0x12C)
#define IOMD_IO1CR	(0x130)
#define IOMD_IO1ST	(0x134)

#define IOMD_IO2CURA	(0x140)
#define IOMD_IO2ENDA	(0x144)
#define IOMD_IO2CURB	(0x148)
#define IOMD_IO2ENDB	(0x14C)
#define IOMD_IO2CR	(0x150)
#define IOMD_IO2ST	(0x154)

#define IOMD_IO3CURA	(0x160)
#define IOMD_IO3ENDA	(0x164)
#define IOMD_IO3CURB	(0x168)
#define IOMD_IO3ENDB	(0x16C)
#define IOMD_IO3CR	(0x170)
#define IOMD_IO3ST	(0x174)
#endif

#define IOMD_SD0CURA	(0x180)
#define IOMD_SD0ENDA	(0x184)
#define IOMD_SD0CURB	(0x188)
#define IOMD_SD0ENDB	(0x18C)
#define IOMD_SD0CR	(0x190)
#define IOMD_SD0ST	(0x194)

#ifdef CONFIG_ARCH_RPC
#define IOMD_SD1CURA	(0x1A0)
#define IOMD_SD1ENDA	(0x1A4)
#define IOMD_SD1CURB	(0x1A8)
#define IOMD_SD1ENDB	(0x1AC)
#define IOMD_SD1CR	(0x1B0)
#define IOMD_SD1ST	(0x1B4)
#endif

#define IOMD_CURSCUR	(0x1C0)
#define IOMD_CURSINIT	(0x1C4)

#define IOMD_VIDCUR	(0x1D0)
#define IOMD_VIDEND	(0x1D4)
#define IOMD_VIDSTART	(0x1D8)
#define IOMD_VIDINIT	(0x1DC)
#define IOMD_VIDCR	(0x1E0)

#define IOMD_DMASTAT	(0x1F0)
#define IOMD_DMAREQ	(0x1F4)
#define IOMD_DMAMASK	(0x1F8)

#define DMA_END_S	(1 << 31)
#define DMA_END_L	(1 << 30)

#define DMA_CR_C	0x80
#define DMA_CR_D	0x40
#define DMA_CR_E	0x20

#define DMA_ST_OFL	4
#define DMA_ST_INT	2
#define DMA_ST_AB	1

/*
 * DMA (MEMC) compatibility
 */
#define HALF_SAM	vram_half_sam
#define VDMA_ALIGNMENT	(HALF_SAM * 2)
#define VDMA_XFERSIZE	(HALF_SAM)
#define VDMA_INIT	IOMD_VIDINIT
#define VDMA_START	IOMD_VIDSTART
#define VDMA_END	IOMD_VIDEND

#ifndef __ASSEMBLY__
extern unsigned int vram_half_sam;
#define video_set_dma(start,end,offset)				\
do {								\
	outl (SCREEN_START + start, VDMA_START);		\
	outl (SCREEN_START + end - VDMA_XFERSIZE, VDMA_END);	\
	if (offset >= end - VDMA_XFERSIZE)			\
		offset |= 0x40000000;				\
	outl (SCREEN_START + offset, VDMA_INIT);		\
} while (0)
#endif

#endif
