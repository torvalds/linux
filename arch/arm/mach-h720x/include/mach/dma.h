/*
 * arch/arm/mach-h720x/include/mach/dma.h
 *
 * Architecture DMA routes
 *
 * Copyright (C) 1997.1998 Russell King
 */
#ifndef __ASM_ARCH_DMA_H
#define __ASM_ARCH_DMA_H

/*
 * This is the maximum DMA address that can be DMAd to.
 * There should not be more than (0xd0000000 - 0xc0000000)
 * bytes of RAM.
 */
#define MAX_DMA_ADDRESS		0xd0000000

#if defined (CONFIG_CPU_H7201)
#define MAX_DMA_CHANNELS	3
#elif defined (CONFIG_CPU_H7202)
#define MAX_DMA_CHANNELS	4
#else
#error processor definition missmatch
#endif

#endif /* __ASM_ARCH_DMA_H */
