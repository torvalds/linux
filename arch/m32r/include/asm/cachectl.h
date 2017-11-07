/* SPDX-License-Identifier: GPL-2.0 */
/*
 * cachectl.h -- defines for M32R cache control system calls
 *
 * Copyright (C) 2003 by Kazuhiro Inaoka
 */
#ifndef	__ASM_M32R_CACHECTL
#define	__ASM_M32R_CACHECTL

/*
 * Options for cacheflush system call
 *
 * cacheflush() is currently fluch_cache_all().
 */
#define	ICACHE	(1<<0)		/* flush instruction cache        */
#define	DCACHE	(1<<1)		/* writeback and flush data cache */
#define	BCACHE	(ICACHE|DCACHE)	/* flush both caches              */

/*
 * Caching modes for the cachectl(2) call
 *
 * cachectl(2) is currently not supported and returns ENOSYS.
 */
#define CACHEABLE	0	/* make pages cacheable */
#define UNCACHEABLE	1	/* make pages uncacheable */

#endif	/* __ASM_M32R_CACHECTL */
