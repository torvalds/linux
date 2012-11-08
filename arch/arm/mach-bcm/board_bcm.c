/*
 * Copyright (C) 2012 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>

#include <asm/mach/arch.h>
#include <asm/hardware/gic.h>

#include <asm/mach/time.h>

static const struct of_device_id irq_match[] = {
	{.compatible = "arm,cortex-a9-gic", .data = gic_of_init, },
	{}
};

static void timer_init(void)
{
}

static void __init init_irq(void)
{
	of_irq_init(irq_match);
}

static void __init board_init(void)
{
	of_platform_populate(NULL, of_default_bus_match_table, NULL,
		&platform_bus);
}

static const char * const bcm11351_dt_compat[] = { "bcm,bcm11351", NULL, };

DT_MACHINE_START(BCM11351_DT, "Broadcom Application Processor")
	.init_irq = init_irq,
	.init_time = timer_init,
	.init_machine = board_init,
	.dt_compat = bcm11351_dt_compat,
	.handle_irq = gic_handle_irq,
MACHINE_END
