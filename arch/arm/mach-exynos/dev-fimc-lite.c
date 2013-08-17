/* linux/arch/arm/plat-s5p/dev-fimc-lite.c
 *
 * Copyright (c) 2011 Samsung Electronics
 *
 * Base S5P FIMC-Lite resource and device definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <mach/map.h>
#include <media/exynos_flite.h>

static struct resource exynos_flite0_resource[] = {
	[0] = DEFINE_RES_MEM(EXYNOS5_PA_FIMC_LITE0, SZ_4K),
	[1] = DEFINE_RES_IRQ(IRQ_FIMC_LITE0),
};

struct platform_device exynos_device_flite0 = {
	.name		= "exynos-fimc-lite",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(exynos_flite0_resource),
	.resource	= exynos_flite0_resource,
};

static struct resource exynos_flite1_resource[] = {
	[0] = DEFINE_RES_MEM(EXYNOS5_PA_FIMC_LITE1, SZ_4K),
	[1] = DEFINE_RES_IRQ(IRQ_FIMC_LITE1),
};

struct platform_device exynos_device_flite1 = {
	.name		= "exynos-fimc-lite",
	.id		= 1,
	.num_resources	= ARRAY_SIZE(exynos_flite1_resource),
	.resource	= exynos_flite1_resource,
};

static struct resource exynos_flite2_resource[] = {
	[0] = DEFINE_RES_MEM(EXYNOS5_PA_FIMC_LITE2, SZ_4K),
	[1] = DEFINE_RES_IRQ(IRQ_FIMC_LITE2),
};

struct platform_device exynos_device_flite2 = {
	.name		= "exynos-fimc-lite",
	.id		= 2,
	.num_resources	= ARRAY_SIZE(exynos_flite2_resource),
	.resource	= exynos_flite2_resource,
};

struct exynos_platform_flite exynos_flite0_default_data __initdata;
struct exynos_platform_flite exynos_flite1_default_data __initdata;
struct exynos_platform_flite exynos_flite2_default_data __initdata;
