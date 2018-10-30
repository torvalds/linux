/* SPDX-License-Identifier: GPL-2.0 */
/*
 * include/asm-m68k/cache.h
 */
#ifndef __ARCH_M68K_CACHE_H
#define __ARCH_M68K_CACHE_H

/* bytes per L1 cache line */
#define        L1_CACHE_SHIFT  4
#define        L1_CACHE_BYTES  (1<< L1_CACHE_SHIFT)

#define ARCH_DMA_MINALIGN	L1_CACHE_BYTES

#endif
