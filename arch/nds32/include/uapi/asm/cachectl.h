/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (C) 1994, 1995, 1996 by Ralf Baechle
// Copyright (C) 2005-2017 Andes Technology Corporation
#ifndef	_ASM_CACHECTL
#define	_ASM_CACHECTL

/*
 * Options for cacheflush system call
 */
#define	ICACHE	0		/* flush instruction cache        */
#define	DCACHE	1		/* writeback and flush data cache */
#define	BCACHE	2		/* flush instruction cache + writeback and flush data cache */

#endif /* _ASM_CACHECTL */
