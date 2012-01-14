/*
 *  Port on Texas Instruments TMS320C6x architecture
 *
 *  Copyright (C) 2005, 2006, 2009, 2010 Texas Instruments Incorporated
 *  Author: Aurelien Jacquiot (aurelien.jacquiot@jaluna.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */
#ifndef _ASM_C6X_CACHE_H
#define _ASM_C6X_CACHE_H

#include <linux/irqflags.h>

/*
 * Cache line size
 */
#define L1D_CACHE_BYTES   64
#define L1P_CACHE_BYTES   32
#define L2_CACHE_BYTES	  128

/*
 * L2 used as cache
 */
#define L2MODE_SIZE	  L2MODE_256K_CACHE

/*
 * For practical reasons the L1_CACHE_BYTES defines should not be smaller than
 * the L2 line size
 */
#define L1_CACHE_BYTES        L2_CACHE_BYTES

#define L2_CACHE_ALIGN_LOW(x) \
	(((x) & ~(L2_CACHE_BYTES - 1)))
#define L2_CACHE_ALIGN_UP(x) \
	(((x) + (L2_CACHE_BYTES - 1)) & ~(L2_CACHE_BYTES - 1))
#define L2_CACHE_ALIGN_CNT(x) \
	(((x) + (sizeof(int) - 1)) & ~(sizeof(int) - 1))

#define ARCH_DMA_MINALIGN	L1_CACHE_BYTES
#define ARCH_SLAB_MINALIGN	L1_CACHE_BYTES

/*
 * This is the granularity of hardware cacheability control.
 */
#define CACHEABILITY_ALIGN	0x01000000

/*
 * Align a physical address to MAR regions
 */
#define CACHE_REGION_START(v) \
	(((u32) (v)) & ~(CACHEABILITY_ALIGN - 1))
#define CACHE_REGION_END(v) \
	(((u32) (v) + (CACHEABILITY_ALIGN - 1)) & ~(CACHEABILITY_ALIGN - 1))

extern void __init c6x_cache_init(void);

extern void enable_caching(unsigned long start, unsigned long end);
extern void disable_caching(unsigned long start, unsigned long end);

extern void L1_cache_off(void);
extern void L1_cache_on(void);

extern void L1P_cache_global_invalidate(void);
extern void L1D_cache_global_invalidate(void);
extern void L1D_cache_global_writeback(void);
extern void L1D_cache_global_writeback_invalidate(void);
extern void L2_cache_set_mode(unsigned int mode);
extern void L2_cache_global_writeback_invalidate(void);
extern void L2_cache_global_writeback(void);

extern void L1P_cache_block_invalidate(unsigned int start, unsigned int end);
extern void L1D_cache_block_invalidate(unsigned int start, unsigned int end);
extern void L1D_cache_block_writeback_invalidate(unsigned int start,
						 unsigned int end);
extern void L1D_cache_block_writeback(unsigned int start, unsigned int end);
extern void L2_cache_block_invalidate(unsigned int start, unsigned int end);
extern void L2_cache_block_writeback(unsigned int start, unsigned int end);
extern void L2_cache_block_writeback_invalidate(unsigned int start,
						unsigned int end);
extern void L2_cache_block_invalidate_nowait(unsigned int start,
					     unsigned int end);
extern void L2_cache_block_writeback_nowait(unsigned int start,
					    unsigned int end);

extern void L2_cache_block_writeback_invalidate_nowait(unsigned int start,
						       unsigned int end);

#endif /* _ASM_C6X_CACHE_H */
