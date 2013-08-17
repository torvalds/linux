/*
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *      http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <asm/irq.h>

#include <plat/devs.h>

#include <mach/irqs.h>
#include <mach/map.h>
#include <mach/tmu.h>

static struct resource tmu_resource[] = {
	[0] = DEFINE_RES_MEM(S5P_PA_TMU, SZ_64K),
	[1] = DEFINE_RES_IRQ(EXYNOS5_IRQ_TMU),
};

struct platform_device exynos_device_tmu = {
	.name	= "exynos_tmu",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(tmu_resource),
	.resource	= tmu_resource,
};

static struct resource tmu_resource_5410[] = {
	[0] = DEFINE_RES_MEM(S5P_PA_TMU, SZ_16K),
	[1] = DEFINE_RES_MEM(EXYNOS5410_PA_TMU1, SZ_16K),
	[2] = DEFINE_RES_MEM(EXYNOS5410_PA_TMU2, SZ_16K),
	[3] = DEFINE_RES_MEM(EXYNOS5410_PA_TMU3, SZ_16K),
	[4] = DEFINE_RES_IRQ(EXYNOS5_IRQ_TMU),
	[5] = DEFINE_RES_IRQ(EXYNOS5410_IRQ_TMU1),
	[6] = DEFINE_RES_IRQ(EXYNOS5410_IRQ_TMU2),
	[7] = DEFINE_RES_IRQ(EXYNOS5410_IRQ_TMU3),
};

struct platform_device exynos5410_device_tmu = {
	.name	= "exynos5-tmu",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(tmu_resource_5410),
	.resource	= tmu_resource_5410,
};

int exynos_tmu_get_irqno(int num)
{
	return platform_get_irq(&exynos_device_tmu, num);
}

struct tmu_info *exynos_tmu_get_platdata(void)
{
	return platform_get_drvdata(&exynos_device_tmu);
}

void __init exynos_tmu_set_platdata(struct tmu_data *pd)
{
	struct tmu_data *npd;

	if (pd == NULL) {
		pr_err("%s: no platform data supplied\n", __func__);
		return;
	}

	npd = kmemdup(pd, sizeof(struct tmu_data), GFP_KERNEL);
	if (npd == NULL)
		pr_err("%s: no memory for platform data\n", __func__);

	exynos_device_tmu.dev.platform_data = npd;
}
