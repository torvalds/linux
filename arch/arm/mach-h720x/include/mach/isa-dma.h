/*
 * arch/arm/mach-h720x/include/mach/isa-dma.h
 *
 * Architecture DMA routes
 *
 * Copyright (C) 1997.1998 Russell King
 */
#ifndef __ASM_ARCH_DMA_H
#define __ASM_ARCH_DMA_H

#if defined (CONFIG_CPU_H7201)
#define MAX_DMA_CHANNELS	3
#elif defined (CONFIG_CPU_H7202)
#define MAX_DMA_CHANNELS	4
#else
#error processor definition missmatch
#endif

#endif /* __ASM_ARCH_DMA_H */
