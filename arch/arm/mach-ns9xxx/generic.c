/*
 * arch/arm/mach-ns9xxx/generic.c
 *
 * Copyright (C) 2006 by Digi International Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <asm/memory.h>
#include <asm/page.h>
#include <asm/mach-types.h>
#include <asm/mach/map.h>
#include <asm/arch-ns9xxx/regs-sys.h>
#include <asm/arch-ns9xxx/regs-mem.h>
#include <asm/arch-ns9xxx/board.h>

static struct map_desc standard_io_desc[] __initdata = {
	{ /* BBus */
		.virtual = io_p2v(0x90000000),
		.pfn = __phys_to_pfn(0x90000000),
		.length = 0x00700000,
		.type = MT_DEVICE,
	}, { /* AHB */
		.virtual = io_p2v(0xa0100000),
		.pfn = __phys_to_pfn(0xa0100000),
		.length = 0x00900000,
		.type = MT_DEVICE,
	},
};

void __init ns9xxx_map_io(void)
{
	iotable_init(standard_io_desc, ARRAY_SIZE(standard_io_desc));
}

void __init ns9xxx_init_machine(void)
{
}
