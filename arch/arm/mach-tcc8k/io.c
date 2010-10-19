/*
 * linux/arch/arm/mach-tcc8k/io.c
 *
 * (C) 2009 Hans J. Koch <hjk@linutronix.de>
 *
 * derived from TCC83xx io.c
 * Copyright (C) Telechips, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>

#include <asm/mach/map.h>

#include <mach/tcc8k-regs.h>

/*
 * The machine specific code may provide the extra mapping besides the
 * default mapping provided here.
 */
static struct map_desc tcc8k_io_desc[] __initdata = {
	{
		.virtual	= (unsigned long)CS1_BASE_VIRT,
		.pfn		= __phys_to_pfn(CS1_BASE),
		.length		= CS1_SIZE,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)AHB_PERI_BASE_VIRT,
		.pfn		= __phys_to_pfn(AHB_PERI_BASE),
		.length		= AHB_PERI_SIZE,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)APB0_PERI_BASE_VIRT,
		.pfn		= __phys_to_pfn(APB0_PERI_BASE),
		.length		= APB0_PERI_SIZE,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)APB1_PERI_BASE_VIRT,
		.pfn		= __phys_to_pfn(APB1_PERI_BASE),
		.length		= APB1_PERI_SIZE,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)EXT_MEM_CTRL_BASE_VIRT,
		.pfn		= __phys_to_pfn(EXT_MEM_CTRL_BASE),
		.length		= EXT_MEM_CTRL_SIZE,
		.type		= MT_DEVICE,
	},
};

/*
 * Maps common IO regions for tcc8k.
 *
 */
void __init tcc8k_map_common_io(void)
{
	iotable_init(tcc8k_io_desc, ARRAY_SIZE(tcc8k_io_desc));
}
