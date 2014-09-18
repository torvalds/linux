/*
 * PMU IRQ registration for the iop3xx xscale PMU families.
 * Copyright (C) 2010 Will Deacon, ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/platform_device.h>
#include <mach/irqs.h>

static struct resource pmu_resource = {
#ifdef CONFIG_ARCH_IOP32X
	.start	= IRQ_IOP32X_CORE_PMU,
	.end	= IRQ_IOP32X_CORE_PMU,
#endif
#ifdef CONFIG_ARCH_IOP33X
	.start	= IRQ_IOP33X_CORE_PMU,
	.end	= IRQ_IOP33X_CORE_PMU,
#endif
	.flags	= IORESOURCE_IRQ,
};

static struct platform_device pmu_device = {
	.name		= "arm-pmu",
	.id		= -1,
	.resource	= &pmu_resource,
	.num_resources	= 1,
};

static int __init iop3xx_pmu_init(void)
{
	platform_device_register(&pmu_device);
	return 0;
}

arch_initcall(iop3xx_pmu_init);
