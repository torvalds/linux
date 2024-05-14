/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2004 Microtronix Datacom Ltd.
 *
 * All rights reserved.
 */

#ifndef _ASM_NIOS2_CACHE_H
#define _ASM_NIOS2_CACHE_H

#define NIOS2_DCACHE_SIZE	CONFIG_NIOS2_DCACHE_SIZE
#define NIOS2_ICACHE_SIZE	CONFIG_NIOS2_ICACHE_SIZE
#define NIOS2_DCACHE_LINE_SIZE	CONFIG_NIOS2_DCACHE_LINE_SIZE
#define NIOS2_ICACHE_LINE_SHIFT	5
#define NIOS2_ICACHE_LINE_SIZE	(1 << NIOS2_ICACHE_LINE_SHIFT)

/* bytes per L1 cache line */
#define L1_CACHE_SHIFT		NIOS2_ICACHE_LINE_SHIFT
#define L1_CACHE_BYTES		NIOS2_ICACHE_LINE_SIZE

#define ARCH_DMA_MINALIGN	L1_CACHE_BYTES

#define __cacheline_aligned
#define ____cacheline_aligned

#endif
