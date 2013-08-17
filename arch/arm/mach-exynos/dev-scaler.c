/*
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Base Scaler resource and device definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <plat/cpu.h>
#include <mach/map.h>
#include <mach/exynos-scaler.h>

struct exynos_scaler_platdata exynos5410_scaler_pd = {
	.use_pclk	= 1,
	.clk_rate	= 300 * MHZ,
};

struct exynos_scaler_platdata exynos5_scaler_pd = {
	.use_pclk	= 0,
	.clk_rate	= 400 * MHZ,
};

static struct resource exynos5_scaler0_resource[] = {
	[0] = DEFINE_RES_MEM(EXYNOS5_PA_MSCL0, SZ_4K),
	[1] = DEFINE_RES_IRQ(EXYNOS5_IRQ_MSCL0),
};

struct platform_device exynos5_device_scaler0 = {
	.name		= "exynos5-scaler",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(exynos5_scaler0_resource),
	.resource	= exynos5_scaler0_resource,
};

static struct resource exynos5_scaler1_resource[] = {
	[0] = DEFINE_RES_MEM(EXYNOS5_PA_MSCL1, SZ_4K),
	[1] = DEFINE_RES_IRQ(EXYNOS5_IRQ_MSCL1),
};

struct platform_device exynos5_device_scaler1 = {
	.name		= "exynos5-scaler",
	.id		= 1,
	.num_resources	= ARRAY_SIZE(exynos5_scaler1_resource),
	.resource	= exynos5_scaler1_resource,
};

static struct resource exynos5_scaler2_resource[] = {
	[0] = DEFINE_RES_MEM(EXYNOS5_PA_MSCL2, SZ_4K),
	[1] = DEFINE_RES_IRQ(EXYNOS5_IRQ_MSCL2),
};

struct platform_device exynos5_device_scaler2 = {
	.name		= "exynos5-scaler",
	.id		= 2,
	.num_resources	= ARRAY_SIZE(exynos5_scaler2_resource),
	.resource	= exynos5_scaler2_resource,
};
