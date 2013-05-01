/*
 * based on the mips/cachectl.h
 *
 * Copyright 2010 Analog Devices Inc.
 * Copyright (C) 1994, 1995, 1996 by Ralf Baechle
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef	_ASM_CACHECTL
#define	_ASM_CACHECTL

/*
 * Options for cacheflush system call
 */
#define	ICACHE	(1<<0)		/* flush instruction cache        */
#define	DCACHE	(1<<1)		/* writeback and flush data cache */
#define	BCACHE	(ICACHE|DCACHE)	/* flush both caches              */

#endif	/* _ASM_CACHECTL */
