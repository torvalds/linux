/*
 * Old U-boot compatibility for Ebony
 *
 * Author: David Gibson <david@gibson.dropbear.id.au>
 *
 * Copyright 2007 David Gibson, IBM Corporatio.
 *   Based on cuboot-83xx.c, which is:
 * Copyright (c) 2007 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include "ops.h"
#include "stdio.h"
#include "44x.h"

#define TARGET_44x
#include "ppcboot.h"

static bd_t bd;
extern char _end[];

BSS_STACK(4096);

void platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
                   unsigned long r6, unsigned long r7)
{
	unsigned long end_of_ram = bd.bi_memstart + bd.bi_memsize;
	unsigned long avail_ram = end_of_ram - (unsigned long)_end;

	memcpy(&bd, (bd_t *)r3, sizeof(bd));
	loader_info.initrd_addr = r4;
	loader_info.initrd_size = r4 ? r5 : 0;
	loader_info.cmdline = (char *)r6;
	loader_info.cmdline_len = r7 - r6;

	simple_alloc_init(_end, avail_ram, 32, 64);

	ebony_init(&bd.bi_enetaddr, &bd.bi_enet1addr);
}
