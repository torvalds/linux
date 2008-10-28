/*
 * arch/arm/mach-l7200/include/mach/dma.h
 *
 * Copyright (C) 2000 Steve Hill (sjhill@cotw.com)
 *
 * Changelog:
 *  08-29-2000	SJH	Created
 */
#ifndef __ASM_ARCH_DMA_H
#define __ASM_ARCH_DMA_H

/* DMA is not yet implemented! It should be the same as acorn, copy over.. */

/*
 * This is the maximum DMA address that can be DMAd to.
 * There should not be more than (0xd0000000 - 0xc0000000)
 * bytes of RAM.
 */
#define MAX_DMA_ADDRESS         0xd0000000

#define DMA_S0                  0

#endif /* _ASM_ARCH_DMA_H */
