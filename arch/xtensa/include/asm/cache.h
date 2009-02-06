/*
 * include/asm-xtensa/cache.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * (C) 2001 - 2005 Tensilica Inc.
 */

#ifndef _XTENSA_CACHE_H
#define _XTENSA_CACHE_H

#include <variant/core.h>

#define L1_CACHE_SHIFT	XCHAL_DCACHE_LINEWIDTH
#define L1_CACHE_BYTES	XCHAL_DCACHE_LINESIZE
#define SMP_CACHE_BYTES	L1_CACHE_BYTES

#define DCACHE_WAY_SIZE	(XCHAL_DCACHE_SIZE/XCHAL_DCACHE_WAYS)
#define ICACHE_WAY_SIZE	(XCHAL_ICACHE_SIZE/XCHAL_ICACHE_WAYS)
#define DCACHE_WAY_SHIFT (XCHAL_DCACHE_SETWIDTH + XCHAL_DCACHE_LINEWIDTH)
#define ICACHE_WAY_SHIFT (XCHAL_ICACHE_SETWIDTH + XCHAL_ICACHE_LINEWIDTH)

/* Maximum cache size per way. */
#if DCACHE_WAY_SIZE >= ICACHE_WAY_SIZE
# define CACHE_WAY_SIZE DCACHE_WAY_SIZE
#else
# define CACHE_WAY_SIZE ICACHE_WAY_SIZE
#endif


#endif	/* _XTENSA_CACHE_H */
