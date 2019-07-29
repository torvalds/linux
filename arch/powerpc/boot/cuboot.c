// SPDX-License-Identifier: GPL-2.0-only
/*
 * Compatibility for old (not device tree aware) U-Boot versions
 *
 * Author: Scott Wood <scottwood@freescale.com>
 * Consolidated using macros by David Gibson <david@gibson.dropbear.id.au>
 *
 * Copyright 2007 David Gibson, IBM Corporation.
 * Copyright (c) 2007 Freescale Semiconductor, Inc.
 */

#include "ops.h"
#include "stdio.h"

#include "ppcboot.h"

void cuboot_init(unsigned long r4, unsigned long r5,
		 unsigned long r6, unsigned long r7,
		 unsigned long end_of_ram)
{
	unsigned long avail_ram = end_of_ram - (unsigned long)_end;

	loader_info.initrd_addr = r4;
	loader_info.initrd_size = r4 ? r5 - r4 : 0;
	loader_info.cmdline = (char *)r6;
	loader_info.cmdline_len = r7 - r6;

	simple_alloc_init(_end, avail_ram - 1024*1024, 32, 64);
}
