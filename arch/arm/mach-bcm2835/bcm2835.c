/*
 * Copyright (C) 2010 Broadcom
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/init.h>
#include <linux/of_platform.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>
#include <asm/exception.h>

#include <mach/bcm2835_soc.h>

static struct map_desc io_map __initdata = {
	.virtual = BCM2835_PERIPH_VIRT,
	.pfn = __phys_to_pfn(BCM2835_PERIPH_PHYS),
	.length = BCM2835_PERIPH_SIZE,
	.type = MT_DEVICE
};

void __init bcm2835_map_io(void)
{
	iotable_init(&io_map, 1);
}

void __init bcm2835_init_irq(void)
{
}

asmlinkage void __exception_irq_entry bcm2835_handle_irq(struct pt_regs *regs)
{
}

void __init bcm2835_init(void)
{
	int ret;

	ret = of_platform_populate(NULL, of_default_bus_match_table, NULL,
				   NULL);
	if (ret) {
		pr_err("of_platform_populate failed: %d\n", ret);
		BUG();
	}
}

static void __init bcm2835_timer_init(void)
{
}

struct sys_timer bcm2835_timer = {
	.init = bcm2835_timer_init
};

static const char * const bcm2835_compat[] = {
	"brcm,bcm2835",
	NULL
};

DT_MACHINE_START(BCM2835, "BCM2835")
	.map_io = bcm2835_map_io,
	.init_irq = bcm2835_init_irq,
	.handle_irq = bcm2835_handle_irq,
	.init_machine = bcm2835_init,
	.timer = &bcm2835_timer,
	.dt_compat = bcm2835_compat
MACHINE_END
