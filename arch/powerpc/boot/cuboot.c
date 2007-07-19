/*
 * Compatibility for old (not device tree aware) U-Boot versions
 *
 * Author: Scott Wood <scottwood@freescale.com>
 * Consolidated using macros by David Gibson <david@gibson.dropbear.id.au>
 *
 * Copyright 2007 David Gibson, IBM Corporation.
 * Copyright (c) 2007 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include "ops.h"
#include "stdio.h"

#include "ppcboot.h"

extern char _end[];
extern char _dtb_start[], _dtb_end[];

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
