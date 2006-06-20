/*
 * arch/arm/mach-at91rm9200/common.c
 *
 *  Copyright (C) 2005 SAN People
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <linux/config.h>
#include <linux/module.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <asm/hardware.h>
#include "generic.h"

static struct map_desc at91rm9200_io_desc[] __initdata = {
	{
		.virtual	= AT91_VA_BASE_SYS,
		.pfn		= __phys_to_pfn(AT91_BASE_SYS),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= AT91_VA_BASE_SPI,
		.pfn		= __phys_to_pfn(AT91_BASE_SPI),
		.length		= SZ_16K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= AT91_VA_BASE_SSC2,
		.pfn		= __phys_to_pfn(AT91_BASE_SSC2),
		.length		= SZ_16K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= AT91_VA_BASE_SSC1,
		.pfn		= __phys_to_pfn(AT91_BASE_SSC1),
		.length		= SZ_16K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= AT91_VA_BASE_SSC0,
		.pfn		= __phys_to_pfn(AT91_BASE_SSC0),
		.length		= SZ_16K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= AT91_VA_BASE_US3,
		.pfn		= __phys_to_pfn(AT91_BASE_US3),
		.length		= SZ_16K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= AT91_VA_BASE_US2,
		.pfn		= __phys_to_pfn(AT91_BASE_US2),
		.length		= SZ_16K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= AT91_VA_BASE_US1,
		.pfn		= __phys_to_pfn(AT91_BASE_US1),
		.length		= SZ_16K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= AT91_VA_BASE_US0,
		.pfn		= __phys_to_pfn(AT91_BASE_US0),
		.length		= SZ_16K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= AT91_VA_BASE_EMAC,
		.pfn		= __phys_to_pfn(AT91_BASE_EMAC),
		.length		= SZ_16K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= AT91_VA_BASE_TWI,
		.pfn		= __phys_to_pfn(AT91_BASE_TWI),
		.length		= SZ_16K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= AT91_VA_BASE_MCI,
		.pfn		= __phys_to_pfn(AT91_BASE_MCI),
		.length		= SZ_16K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= AT91_VA_BASE_UDP,
		.pfn		= __phys_to_pfn(AT91_BASE_UDP),
		.length		= SZ_16K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= AT91_VA_BASE_TCB1,
		.pfn		= __phys_to_pfn(AT91_BASE_TCB1),
		.length		= SZ_16K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= AT91_VA_BASE_TCB0,
		.pfn		= __phys_to_pfn(AT91_BASE_TCB0),
		.length		= SZ_16K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= AT91_SRAM_VIRT_BASE,
		.pfn		= __phys_to_pfn(AT91_SRAM_BASE),
		.length		= AT91_SRAM_SIZE,
		.type		= MT_DEVICE,
	},
};

void __init at91rm9200_map_io(void)
{
	iotable_init(at91rm9200_io_desc, ARRAY_SIZE(at91rm9200_io_desc));
}

