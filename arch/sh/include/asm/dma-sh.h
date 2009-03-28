/*
 * arch/sh/include/asm/dma-sh.h
 *
 * Copyright (C) 2000  Takashi YOSHII
 * Copyright (C) 2003  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#ifndef __DMA_SH_H
#define __DMA_SH_H

#include <asm/dma.h>
#include <cpu/dma.h>

/* DMAOR contorl: The DMAOR access size is different by CPU.*/
#if defined(CONFIG_CPU_SUBTYPE_SH7723)	|| \
    defined(CONFIG_CPU_SUBTYPE_SH7780)	|| \
    defined(CONFIG_CPU_SUBTYPE_SH7785)
#define dmaor_read_reg(n) \
    (n ? ctrl_inw(SH_DMAC_BASE1 + DMAOR) \
	: ctrl_inw(SH_DMAC_BASE0 + DMAOR))
#define dmaor_write_reg(n, data) \
    (n ? ctrl_outw(data, SH_DMAC_BASE1 + DMAOR) \
    : ctrl_outw(data, SH_DMAC_BASE0 + DMAOR))
#else /* Other CPU */
#define dmaor_read_reg(n) ctrl_inw(SH_DMAC_BASE0 + DMAOR)
#define dmaor_write_reg(n, data) ctrl_outw(data, SH_DMAC_BASE0 + DMAOR)
#endif

static int dmte_irq_map[] __maybe_unused = {
#if (MAX_DMA_CHANNELS >= 4)
    DMTE0_IRQ,
    DMTE0_IRQ + 1,
    DMTE0_IRQ + 2,
    DMTE0_IRQ + 3,
#endif
#if (MAX_DMA_CHANNELS >= 6)
    DMTE4_IRQ,
    DMTE4_IRQ + 1,
#endif
#if (MAX_DMA_CHANNELS >= 8)
    DMTE6_IRQ,
    DMTE6_IRQ + 1,
#endif
#if (MAX_DMA_CHANNELS >= 12)
    DMTE8_IRQ,
    DMTE9_IRQ,
    DMTE10_IRQ,
    DMTE11_IRQ,
#endif
};

/* Definitions for the SuperH DMAC */
#define REQ_L	0x00000000
#define REQ_E	0x00080000
#define RACK_H	0x00000000
#define RACK_L	0x00040000
#define ACK_R	0x00000000
#define ACK_W	0x00020000
#define ACK_H	0x00000000
#define ACK_L	0x00010000
#define DM_INC	0x00004000
#define DM_DEC	0x00008000
#define SM_INC	0x00001000
#define SM_DEC	0x00002000
#define RS_IN	0x00000200
#define RS_OUT	0x00000300
#define TS_BLK	0x00000040
#define TM_BUR	0x00000020
#define CHCR_DE 0x00000001
#define CHCR_TE 0x00000002
#define CHCR_IE 0x00000004

/* DMAOR definitions */
#define DMAOR_AE	0x00000004
#define DMAOR_NMIF	0x00000002
#define DMAOR_DME	0x00000001

/*
 * Define the default configuration for dual address memory-memory transfer.
 * The 0x400 value represents auto-request, external->external.
 */
#define RS_DUAL	(DM_INC | SM_INC | 0x400 | TS_32)

/* DMA base address */
static u32 dma_base_addr[] __maybe_unused = {
#if (MAX_DMA_CHANNELS >= 4)
	SH_DMAC_BASE0 + 0x00,	/* channel 0 */
	SH_DMAC_BASE0 + 0x10,
	SH_DMAC_BASE0 + 0x20,
	SH_DMAC_BASE0 + 0x30,
#endif
#if (MAX_DMA_CHANNELS >= 6)
	SH_DMAC_BASE0 + 0x50,
	SH_DMAC_BASE0 + 0x60,
#endif
#if (MAX_DMA_CHANNELS >= 8)
	SH_DMAC_BASE1 + 0x00,
	SH_DMAC_BASE1 + 0x10,
#endif
#if (MAX_DMA_CHANNELS >= 12)
	SH_DMAC_BASE1 + 0x20,
	SH_DMAC_BASE1 + 0x30,
	SH_DMAC_BASE1 + 0x50,
	SH_DMAC_BASE1 + 0x60, /* channel 11 */
#endif
};

/* DMA register */
#define SAR     0x00
#define DAR     0x04
#define TCR     0x08
#define CHCR    0x0C
#define DMAOR	0x40

#endif /* __DMA_SH_H */
