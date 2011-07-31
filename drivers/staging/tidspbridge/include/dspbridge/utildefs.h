/*
 * utildefs.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Global UTIL constants and types, shared between DSP API and DSPSYS.
 *
 * Copyright (C) 2005-2006 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef UTILDEFS_
#define UTILDEFS_

/* constants taken from configmg.h */
#define UTIL_MAXMEMREGS     9
#define UTIL_MAXIOPORTS     20
#define UTIL_MAXIRQS        7
#define UTIL_MAXDMACHNLS    7

/* misc. constants */
#define UTIL_MAXARGVS       10

/* Platform specific important info */
struct util_sysinfo {
	/* Granularity of page protection; usually 1k or 4k */
	u32 dw_page_size;
	u32 dw_allocation_granularity;	/* VM granularity, usually 64K */
	u32 dw_number_of_processors;	/* Used as sanity check */
};

#endif /* UTILDEFS_ */
