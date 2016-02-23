/*
 *  Static mappings for Gemini
 *
 *  Copyright (C) 2001-2006 Storlink, Corp.
 *  Copyright (C) 2008-2009 Paulius Zaleckas <paulius.zaleckas@teltonika.lt>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <linux/mm.h>
#include <linux/init.h>

#include <asm/mach/map.h>

#include <mach/hardware.h>

/* Page table mapping for I/O region */
static struct map_desc gemini_io_desc[] __initdata = {
	{
		.virtual	= (unsigned long)IO_ADDRESS(GEMINI_GLOBAL_BASE),
		.pfn		=__phys_to_pfn(GEMINI_GLOBAL_BASE),
		.length		= SZ_512K,
		.type 		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)IO_ADDRESS(GEMINI_UART_BASE),
		.pfn		= __phys_to_pfn(GEMINI_UART_BASE),
		.length		= SZ_512K,
		.type 		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)IO_ADDRESS(GEMINI_TIMER_BASE),
		.pfn		= __phys_to_pfn(GEMINI_TIMER_BASE),
		.length		= SZ_512K,
		.type 		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)IO_ADDRESS(GEMINI_INTERRUPT_BASE),
		.pfn		= __phys_to_pfn(GEMINI_INTERRUPT_BASE),
		.length		= SZ_512K,
		.type 		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)IO_ADDRESS(GEMINI_POWER_CTRL_BASE),
		.pfn		= __phys_to_pfn(GEMINI_POWER_CTRL_BASE),
		.length		= SZ_512K,
		.type 		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)IO_ADDRESS(GEMINI_GPIO_BASE(0)),
		.pfn		= __phys_to_pfn(GEMINI_GPIO_BASE(0)),
		.length		= SZ_512K,
		.type 		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)IO_ADDRESS(GEMINI_GPIO_BASE(1)),
		.pfn		= __phys_to_pfn(GEMINI_GPIO_BASE(1)),
		.length		= SZ_512K,
		.type 		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)IO_ADDRESS(GEMINI_GPIO_BASE(2)),
		.pfn		= __phys_to_pfn(GEMINI_GPIO_BASE(2)),
		.length		= SZ_512K,
		.type 		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)IO_ADDRESS(GEMINI_FLASH_CTRL_BASE),
		.pfn		= __phys_to_pfn(GEMINI_FLASH_CTRL_BASE),
		.length		= SZ_512K,
		.type 		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)IO_ADDRESS(GEMINI_DRAM_CTRL_BASE),
		.pfn		= __phys_to_pfn(GEMINI_DRAM_CTRL_BASE),
		.length		= SZ_512K,
		.type 		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)IO_ADDRESS(GEMINI_GENERAL_DMA_BASE),
		.pfn		= __phys_to_pfn(GEMINI_GENERAL_DMA_BASE),
		.length		= SZ_512K,
		.type 		= MT_DEVICE,
	},
};

void __init gemini_map_io(void)
{
	iotable_init(gemini_io_desc, ARRAY_SIZE(gemini_io_desc));
}
