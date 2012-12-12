/*
 * OMAP Secure API infrastructure.
 *
 * Copyright (C) 2011 Texas Instruments, Inc.
 *	Santosh Shilimkar <santosh.shilimkar@ti.com>
 *
 *
 * This program is free software,you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/memblock.h>

#include <asm/cacheflush.h>
#include <asm/memblock.h>

#include <plat/omap-secure.h>
#include "omap-secure.h"

static phys_addr_t omap_secure_memblock_base;

/**
 * omap_sec_dispatcher: Routine to dispatch low power secure
 * service routines
 * @idx: The HAL API index
 * @flag: The flag indicating criticality of operation
 * @nargs: Number of valid arguments out of four.
 * @arg1, arg2, arg3 args4: Parameters passed to secure API
 *
 * Return the non-zero error value on failure.
 */
u32 omap_secure_dispatcher(u32 idx, u32 flag, u32 nargs, u32 arg1, u32 arg2,
							 u32 arg3, u32 arg4)
{
	u32 ret;
	u32 param[5];

	param[0] = nargs;
	param[1] = arg1;
	param[2] = arg2;
	param[3] = arg3;
	param[4] = arg4;

	/*
	 * Secure API needs physical address
	 * pointer for the parameters
	 */
	flush_cache_all();
	outer_clean_range(__pa(param), __pa(param + 5));
	ret = omap_smc2(idx, flag, __pa(param));

	return ret;
}

/* Allocate the memory to save secure ram */
int __init omap_secure_ram_reserve_memblock(void)
{
	u32 size = OMAP_SECURE_RAM_STORAGE;

	size = ALIGN(size, SECTION_SIZE);
	omap_secure_memblock_base = arm_memblock_steal(size, SECTION_SIZE);

	return 0;
}

phys_addr_t omap_secure_ram_mempool_base(void)
{
	return omap_secure_memblock_base;
}
