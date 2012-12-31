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

static struct resource s5p_pmu_resource[] = {
	{
		.start	= IRQ_PMU,
		.end	= IRQ_PMU,
		.flags	= IORESOURCE_IRQ,
	},
#if CONFIG_NR_CPUS > 1
	{
		.start	= IRQ_PMU_CPU1,
		.end	= IRQ_PMU_CPU1,
		.flags	= IORESOURCE_IRQ,
	},
#endif
#if CONFIG_NR_CPUS > 2
	{
		.start	= IRQ_PMU_CPU2,
		.end	= IRQ_PMU_CPU2,
		.flags	= IORESOURCE_IRQ,
	}, {
		.start	= IRQ_PMU_CPU3,
		.end	= IRQ_PMU_CPU3,
		.flags	= IORESOURCE_IRQ,
	},
#endif
};

struct platform_device s5p_device_pmu = {
	.name		= "arm-pmu",
	.id		= ARM_PMU_DEVICE_CPU,
	.num_resources	= ARRAY_SIZE(s5p_pmu_resource),
	.resource	= s5p_pmu_resource,
};

static int __init s5p_pmu_init(void)
{
	int ret;

	ret = platform_device_register(&s5p_device_pmu);
	if (ret) {
		pr_warning("s5p_pmu_init: pmu device not registered.\n");
		return ret;
	}

	return 0;
}
arch_initcall(s5p_pmu_init);
