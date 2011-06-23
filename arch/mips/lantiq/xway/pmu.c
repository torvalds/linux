/*
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 *
 *  Copyright (C) 2010 John Crispin <blogic@openwrt.org>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/ioport.h>

#include <lantiq_soc.h>

/* PMU - the power management unit allows us to turn part of the core
 * on and off
 */

/* the enable / disable registers */
#define LTQ_PMU_PWDCR	0x1C
#define LTQ_PMU_PWDSR	0x20

#define ltq_pmu_w32(x, y)	ltq_w32((x), ltq_pmu_membase + (y))
#define ltq_pmu_r32(x)		ltq_r32(ltq_pmu_membase + (x))

static struct resource ltq_pmu_resource = {
	.name	= "pmu",
	.start	= LTQ_PMU_BASE_ADDR,
	.end	= LTQ_PMU_BASE_ADDR + LTQ_PMU_SIZE - 1,
	.flags	= IORESOURCE_MEM,
};

static void __iomem *ltq_pmu_membase;

void ltq_pmu_enable(unsigned int module)
{
	int err = 1000000;

	ltq_pmu_w32(ltq_pmu_r32(LTQ_PMU_PWDCR) & ~module, LTQ_PMU_PWDCR);
	do {} while (--err && (ltq_pmu_r32(LTQ_PMU_PWDSR) & module));

	if (!err)
		panic("activating PMU module failed!\n");
}
EXPORT_SYMBOL(ltq_pmu_enable);

void ltq_pmu_disable(unsigned int module)
{
	ltq_pmu_w32(ltq_pmu_r32(LTQ_PMU_PWDCR) | module, LTQ_PMU_PWDCR);
}
EXPORT_SYMBOL(ltq_pmu_disable);

int __init ltq_pmu_init(void)
{
	if (insert_resource(&iomem_resource, &ltq_pmu_resource) < 0)
		panic("Failed to insert pmu memory\n");

	if (request_mem_region(ltq_pmu_resource.start,
			resource_size(&ltq_pmu_resource), "pmu") < 0)
		panic("Failed to request pmu memory\n");

	ltq_pmu_membase = ioremap_nocache(ltq_pmu_resource.start,
				resource_size(&ltq_pmu_resource));
	if (!ltq_pmu_membase)
		panic("Failed to remap pmu memory\n");
	return 0;
}

core_initcall(ltq_pmu_init);
