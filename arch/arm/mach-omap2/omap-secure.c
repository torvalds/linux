/*
 * OMAP Secure API infrastructure.
 *
 * Copyright (C) 2011 Texas Instruments, Inc.
 *	Santosh Shilimkar <santosh.shilimkar@ti.com>
 * Copyright (C) 2012 Ivaylo Dimitrov <freemangordon@abv.bg>
 * Copyright (C) 2013 Pali Rohár <pali.rohar@gmail.com>
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

#if defined(CONFIG_ARCH_OMAP3) && defined(CONFIG_PM)
u32 omap3_save_secure_ram(void __iomem *addr, int size)
{
	u32 ret;
	u32 param[5];

	if (size != OMAP3_SAVE_SECURE_RAM_SZ)
		return OMAP3_SAVE_SECURE_RAM_SZ;

	param[0] = 4;		/* Number of arguments */
	param[1] = __pa(addr);	/* Physical address for saving */
	param[2] = 0;
	param[3] = 1;
	param[4] = 1;

	ret = save_secure_ram_context(__pa(param));

	return ret;
}
#endif

/**
 * rx51_secure_dispatcher: Routine to dispatch secure PPA API calls
 * @idx: The PPA API index
 * @process: Process ID
 * @flag: The flag indicating criticality of operation
 * @nargs: Number of valid arguments out of four.
 * @arg1, arg2, arg3 args4: Parameters passed to secure API
 *
 * Return the non-zero error value on failure.
 *
 * NOTE: rx51_secure_dispatcher differs from omap_secure_dispatcher because
 *       it calling omap_smc3() instead omap_smc2() and param[0] is nargs+1
 */
u32 rx51_secure_dispatcher(u32 idx, u32 process, u32 flag, u32 nargs,
			   u32 arg1, u32 arg2, u32 arg3, u32 arg4)
{
	u32 ret;
	u32 param[5];

	param[0] = nargs+1; /* RX-51 needs number of arguments + 1 */
	param[1] = arg1;
	param[2] = arg2;
	param[3] = arg3;
	param[4] = arg4;

	/*
	 * Secure API needs physical address
	 * pointer for the parameters
	 */
	local_irq_disable();
	local_fiq_disable();
	flush_cache_all();
	outer_clean_range(__pa(param), __pa(param + 5));
	ret = omap_smc3(idx, process, flag, __pa(param));
	flush_cache_all();
	local_fiq_enable();
	local_irq_enable();

	return ret;
}

/**
 * rx51_secure_update_aux_cr: Routine to modify the contents of Auxiliary Control Register
 *  @set_bits: bits to set in ACR
 *  @clr_bits: bits to clear in ACR
 *
 * Return the non-zero error value on failure.
*/
u32 rx51_secure_update_aux_cr(u32 set_bits, u32 clear_bits)
{
	u32 acr;

	/* Read ACR */
	asm volatile ("mrc p15, 0, %0, c1, c0, 1" : "=r" (acr));
	acr &= ~clear_bits;
	acr |= set_bits;

	return rx51_secure_dispatcher(RX51_PPA_WRITE_ACR,
				      0,
				      FLAG_START_CRITICAL,
				      1, acr, 0, 0, 0);
}

/**
 * rx51_secure_rng_call: Routine for HW random generator
 */
u32 rx51_secure_rng_call(u32 ptr, u32 count, u32 flag)
{
	return rx51_secure_dispatcher(RX51_PPA_HWRNG,
				      0,
				      NO_FLAG,
				      3, ptr, count, flag, 0);
}
