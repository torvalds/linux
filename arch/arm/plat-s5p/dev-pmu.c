/*
 * linux/arch/arm/plat-s5p/dev-pmu.c
 *
 * Copyright (C) 2010 Samsung Electronics Co.Ltd
 * Author: Joonyoung Shim <jy0922.shim@samsung.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/platform_device.h>
#include <asm/pmu.h>
#include <mach/irqs.h>

static struct resource s5p_pmu_resource = {
	.start	= IRQ_PMU,
	.end	= IRQ_PMU,
	.flags	= IORESOURCE_IRQ,
};

struct platform_device s5p_device_pmu = {
	.name		= "arm-pmu",
	.id		= ARM_PMU_DEVICE_CPU,
	.num_resources	= 1,
	.resource	= &s5p_pmu_resource,
};

static int __init s5p_pmu_init(void)
{
	platform_device_register(&s5p_device_pmu);
	return 0;
}
arch_initcall(s5p_pmu_init);
