/*
 * arch/sh/cchips/voyagergx/setup.c
 *
 * Setup routines for VoyagerGX cchip.
 *
 * Copyright (C) 2003 Lineo uSolutions, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <asm/io.h>
#include <asm/voyagergx.h>

static int __init setup_voyagergx(void)
{
	unsigned long val;

	val = inl(DRAM_CTRL);
	val |= (DRAM_CTRL_CPU_COLUMN_SIZE_256	|
		DRAM_CTRL_CPU_ACTIVE_PRECHARGE	|
		DRAM_CTRL_CPU_RESET		|
		DRAM_CTRL_REFRESH_COMMAND	|
		DRAM_CTRL_BLOCK_WRITE_TIME	|
		DRAM_CTRL_BLOCK_WRITE_PRECHARGE	|
		DRAM_CTRL_ACTIVE_PRECHARGE	|
		DRAM_CTRL_RESET			|
		DRAM_CTRL_REMAIN_ACTIVE);
	outl(val, DRAM_CTRL);

	return 0;
}

module_init(setup_voyagergx);
