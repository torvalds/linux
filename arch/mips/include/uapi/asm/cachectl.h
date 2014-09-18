/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994, 1995, 1996 by Ralf Baechle
 */
#ifndef _ASM_CACHECTL
#define _ASM_CACHECTL

/*
 * Options for cacheflush system call
 */
#define ICACHE	(1<<0)		/* flush instruction cache	  */
#define DCACHE	(1<<1)		/* writeback and flush data cache */
#define BCACHE	(ICACHE|DCACHE) /* flush both caches		  */

/*
 * Caching modes for the cachectl(2) call
 *
 * cachectl(2) is currently not supported and returns ENOSYS.
 */
#define CACHEABLE	0	/* make pages cacheable */
#define UNCACHEABLE	1	/* make pages uncacheable */

#endif	/* _ASM_CACHECTL */
