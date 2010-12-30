/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#ifndef _ASM_TILE_CACHE_H
#define _ASM_TILE_CACHE_H

#include <arch/chip.h>

/* bytes per L1 data cache line */
#define L1_CACHE_SHIFT		CHIP_L1D_LOG_LINE_SIZE()
#define L1_CACHE_BYTES		(1 << L1_CACHE_SHIFT)

/* bytes per L2 cache line */
#define L2_CACHE_SHIFT		CHIP_L2_LOG_LINE_SIZE()
#define L2_CACHE_BYTES		(1 << L2_CACHE_SHIFT)
#define L2_CACHE_ALIGN(x)	(((x)+(L2_CACHE_BYTES-1)) & -L2_CACHE_BYTES)

/*
 * TILE-Gx is fully coherent so we don't need to define ARCH_DMA_MINALIGN.
 */
#ifndef __tilegx__
#define ARCH_DMA_MINALIGN	L2_CACHE_BYTES
#endif

/* use the cache line size for the L2, which is where it counts */
#define SMP_CACHE_BYTES_SHIFT	L2_CACHE_SHIFT
#define SMP_CACHE_BYTES		L2_CACHE_BYTES
#define INTERNODE_CACHE_SHIFT   L2_CACHE_SHIFT
#define INTERNODE_CACHE_BYTES   L2_CACHE_BYTES

/* Group together read-mostly things to avoid cache false sharing */
#define __read_mostly __attribute__((__section__(".data.read_mostly")))

/*
 * Attribute for data that is kept read/write coherent until the end of
 * initialization, then bumped to read/only incoherent for performance.
 */
#define __write_once __attribute__((__section__(".w1data")))

#endif /* _ASM_TILE_CACHE_H */
