/*
 * Copyright 2002 Integrated Device Technology, Inc.
 *		All rights reserved.
 *
 * DMA register definition.
 *
 * Author : ryan.holmQVist@idt.com
 * Date	  : 20011005
 */

#ifndef _ASM_RC32434_DMA_V_H_
#define _ASM_RC32434_DMA_V_H_

#include  <asm/mach-rc32434/dma.h>
#include  <asm/mach-rc32434/rc32434.h>

#define DMA_CHAN_OFFSET		0x14
#define IS_DMA_USED(X)		(((X) & \
				(DMA_DESC_FINI | DMA_DESC_DONE | DMA_DESC_TERM)) \
				!= 0)
#define DMA_COUNT(count)	((count) & DMA_DESC_COUNT_MSK)

#define DMA_HALT_TIMEOUT	500

static inline int rc32434_halt_dma(struct dma_reg *ch)
{
	int timeout = 1;
	if (__raw_readl(&ch->dmac) & DMA_CHAN_RUN_BIT) {
		__raw_writel(0, &ch->dmac);
		for (timeout = DMA_HALT_TIMEOUT; timeout > 0; timeout--) {
			if (__raw_readl(&ch->dmas) & DMA_STAT_HALT) {
				__raw_writel(0, &ch->dmas);
				break;
			}
		}
	}

	return timeout ? 0 : 1;
}

static inline void rc32434_start_dma(struct dma_reg *ch, u32 dma_addr)
{
	__raw_writel(0, &ch->dmandptr);
	__raw_writel(dma_addr, &ch->dmadptr);
}

static inline void rc32434_chain_dma(struct dma_reg *ch, u32 dma_addr)
{
	__raw_writel(dma_addr, &ch->dmandptr);
}

#endif	/* _ASM_RC32434_DMA_V_H_ */
