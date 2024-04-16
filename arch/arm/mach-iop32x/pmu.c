// SPDX-License-Identifier: GPL-2.0-only
/*
 * PMU IRQ registration for the iop3xx xscale PMU families.
 * Copyright (C) 2010 Will Deacon, ARM Ltd.
 */

#include <linux/platform_device.h>
#include "irqs.h"

static struct resource pmu_resource = {
	.start	= IRQ_IOP32X_CORE_PMU,
	.end	= IRQ_IOP32X_CORE_PMU,
	.flags	= IORESOURCE_IRQ,
};

static struct platform_device pmu_device = {
	.name		= "xscale-pmu",
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
