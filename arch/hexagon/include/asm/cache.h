/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Cache definitions for the Hexagon architecture
 *
 * Copyright (c) 2010-2011,2014 The Linux Foundation. All rights reserved.
 */

#ifndef __ASM_CACHE_H
#define __ASM_CACHE_H

/* Bytes per L1 cache line */
#define L1_CACHE_SHIFT		(5)
#define L1_CACHE_BYTES		(1 << L1_CACHE_SHIFT)

#define ARCH_DMA_MINALIGN	L1_CACHE_BYTES

#define __cacheline_aligned	__aligned(L1_CACHE_BYTES)
#define ____cacheline_aligned	__aligned(L1_CACHE_BYTES)

/* See http://lwn.net/Articles/262554/ */
#define __read_mostly

#endif
