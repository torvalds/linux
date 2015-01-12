/*
 * marzen board support - Reference DT implementation
 *
 * Copyright (C) 2011  Renesas Solutions Corp.
 * Copyright (C) 2011  Magnus Damm
 * Copyright (C) 2013  Simon Horman
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk/shmobile.h>
#include <linux/clocksource.h>
#include <linux/of_platform.h>

#include <asm/irq.h>
#include <asm/mach/arch.h>

#include "common.h"
#include "irqs.h"
#include "r8a7779.h"

static void __init marzen_init_timer(void)
{
	r8a7779_clocks_init(r8a7779_read_mode_pins());
	clocksource_of_init();
}

static void __init marzen_init(void)
{
	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
	r8a7779_init_irq_extpin_dt(1); /* IRQ1 as individual interrupt */
}

static const char *marzen_boards_compat_dt[] __initdata = {
	"renesas,marzen",
	"renesas,marzen-reference",
	NULL,
};

DT_MACHINE_START(MARZEN, "marzen")
	.smp		= smp_ops(r8a7779_smp_ops),
	.map_io		= r8a7779_map_io,
	.init_early	= shmobile_init_delay,
	.init_time	= marzen_init_timer,
	.init_irq	= r8a7779_init_irq_dt,
	.init_machine	= marzen_init,
	.init_late	= shmobile_init_late,
	.dt_compat	= marzen_boards_compat_dt,
MACHINE_END
