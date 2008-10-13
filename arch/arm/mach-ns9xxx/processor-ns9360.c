/*
 * arch/arm/mach-ns9xxx/processor-ns9360.c
 *
 * Copyright (C) 2007 by Digi International Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/slab.h>

#include <asm/page.h>
#include <asm/mach/map.h>
#include <mach/processor-ns9360.h>
#include <mach/regs-sys-ns9360.h>

void ns9360_reset(char mode)
{
	u32 reg;

	reg = __raw_readl(SYS_PLL) >> 16;
	REGSET(reg, SYS_PLL, SWC, YES);
	__raw_writel(reg, SYS_PLL);
}

#define CRYSTAL 29491200 /* Hz */
unsigned long ns9360_systemclock(void)
{
	u32 pll = __raw_readl(SYS_PLL);
	return CRYSTAL * (REGGETIM(pll, SYS_PLL, ND) + 1)
		>> REGGETIM(pll, SYS_PLL, FS);
}

static struct map_desc ns9360_io_desc[] __initdata = {
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

void __init ns9360_map_io(void)
{
	iotable_init(ns9360_io_desc, ARRAY_SIZE(ns9360_io_desc));
}
