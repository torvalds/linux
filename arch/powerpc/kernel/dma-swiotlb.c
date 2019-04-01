/*
 * Contains routines needed to support swiotlb for ppc.
 *
 * Copyright (C) 2009-2010 Freescale Semiconductor, Inc.
 * Author: Becky Bruce
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */
#include <linux/memblock.h>
#include <asm/machdep.h>
#include <asm/swiotlb.h>

unsigned int ppc_swiotlb_enable;

void __init swiotlb_detect_4g(void)
{
	if ((memblock_end_of_DRAM() - 1) > 0xffffffff)
		ppc_swiotlb_enable = 1;
}

static int __init check_swiotlb_enabled(void)
{
	if (ppc_swiotlb_enable)
		swiotlb_print_info();
	else
		swiotlb_exit();

	return 0;
}
subsys_initcall(check_swiotlb_enabled);
