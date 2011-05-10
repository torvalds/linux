/*
 * linux/arch/unicore32/include/mach/dma.h
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __MACH_PUV3_DMA_H__
#define __MACH_PUV3_DMA_H__

/*
 * The PKUnity has six internal DMA channels.
 */
#define MAX_DMA_CHANNELS	6

typedef enum {
	DMA_PRIO_HIGH = 0,
	DMA_PRIO_MEDIUM = 1,
	DMA_PRIO_LOW = 2
} puv3_dma_prio;

/*
 * DMA registration
 */

extern int puv3_request_dma(char *name,
			 puv3_dma_prio prio,
			 void (*irq_handler)(int, void *),
			 void (*err_handler)(int, void *),
			 void *data);

extern void puv3_free_dma(int dma_ch);

static inline void puv3_stop_dma(int ch)
{
	writel(readl(DMAC_CONFIG(ch)) & ~DMAC_CONFIG_EN, DMAC_CONFIG(ch));
}

static inline void puv3_resume_dma(int ch)
{
	writel(readl(DMAC_CONFIG(ch)) | DMAC_CONFIG_EN, DMAC_CONFIG(ch));
}

#endif /* __MACH_PUV3_DMA_H__ */
