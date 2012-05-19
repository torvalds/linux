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

#include <asm/dma-register.h>
#include <cpu/dma-register.h>
#include <cpu/dma.h>

/* DMAOR contorl: The DMAOR access size is different by CPU.*/
#if defined(CONFIG_CPU_SUBTYPE_SH7723)	|| \
    defined(CONFIG_CPU_SUBTYPE_SH7724)	|| \
    defined(CONFIG_CPU_SUBTYPE_SH7780)	|| \
    defined(CONFIG_CPU_SUBTYPE_SH7785)
#define dmaor_read_reg(n) \
    (n ? __raw_readw(SH_DMAC_BASE1 + DMAOR) \
	: __raw_readw(SH_DMAC_BASE0 + DMAOR))
#define dmaor_write_reg(n, data) \
    (n ? __raw_writew(data, SH_DMAC_BASE1 + DMAOR) \
    : __raw_writew(data, SH_DMAC_BASE0 + DMAOR))
#else /* Other CPU */
#define dmaor_read_reg(n) __raw_readw(SH_DMAC_BASE0 + DMAOR)
#define dmaor_write_reg(n, data) __raw_writew(data, SH_DMAC_BASE0 + DMAOR)
#endif

static int dmte_irq_map[] __maybe_unused = {
#if (CONFIG_NR_ONCHIP_DMA_CHANNELS >= 4)
    DMTE0_IRQ,
    DMTE0_IRQ + 1,
    DMTE0_IRQ + 2,
    DMTE0_IRQ + 3,
#endif
#if (CONFIG_NR_ONCHIP_DMA_CHANNELS >= 6)
    DMTE4_IRQ,
    DMTE4_IRQ + 1,
#endif
#if (CONFIG_NR_ONCHIP_DMA_CHANNELS >= 8)
    DMTE6_IRQ,
    DMTE6_IRQ + 1,
#endif
#if (CONFIG_NR_ONCHIP_DMA_CHANNELS >= 12)
    DMTE8_IRQ,
    DMTE9_IRQ,
    DMTE10_IRQ,
    DMTE11_IRQ,
#endif
};

/*
 * Define the default configuration for dual address memory-memory transfer.
 * The 0x400 value represents auto-request, external->external.
 */
#define RS_DUAL	(DM_INC | SM_INC | 0x400 | TS_INDEX2VAL(XMIT_SZ_32BIT))

/* DMA base address */
static u32 dma_base_addr[] __maybe_unused = {
#if (CONFIG_NR_ONCHIP_DMA_CHANNELS >= 4)
	SH_DMAC_BASE0 + 0x00,	/* channel 0 */
	SH_DMAC_BASE0 + 0x10,
	SH_DMAC_BASE0 + 0x20,
	SH_DMAC_BASE0 + 0x30,
#endif
#if (CONFIG_NR_ONCHIP_DMA_CHANNELS >= 6)
	SH_DMAC_BASE0 + 0x50,
	SH_DMAC_BASE0 + 0x60,
#endif
#if (CONFIG_NR_ONCHIP_DMA_CHANNELS >= 8)
	SH_DMAC_BASE1 + 0x00,
	SH_DMAC_BASE1 + 0x10,
#endif
#if (CONFIG_NR_ONCHIP_DMA_CHANNELS >= 12)
	SH_DMAC_BASE1 + 0x20,
	SH_DMAC_BASE1 + 0x30,
	SH_DMAC_BASE1 + 0x50,
	SH_DMAC_BASE1 + 0x60, /* channel 11 */
#endif
};

#endif /* __DMA_SH_H */
