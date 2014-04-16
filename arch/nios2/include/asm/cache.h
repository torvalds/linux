/*
 * Copyright (C) 2004 Microtronix Datacom Ltd.
 *
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
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
