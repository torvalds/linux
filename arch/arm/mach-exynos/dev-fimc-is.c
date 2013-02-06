/* linux/arch/arm/plat-s5p/dev-fimc_is.c
 *
 * Copyright (c) 2011 Samsung Electronics
 *
 * Base FIMC-IS resource and device definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <mach/map.h>
#include <mach/regs-clock.h>
#include <media/exynos_fimc_is.h>

#if defined(CONFIG_ARCH_EXYNOS4)
static struct resource exynos4_fimc_is_resource[] = {
	[0] = {
		.start	= EXYNOS4_PA_FIMC_IS,
		.end	= EXYNOS4_PA_FIMC_IS + SZ_2M + SZ_256K + SZ_128K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_FIMC_IS0,
		.end	= IRQ_FIMC_IS0,
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		.start	= IRQ_FIMC_IS1,
		.end	= IRQ_FIMC_IS1,
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device exynos4_device_fimc_is = {
	.name		= "exynos4-fimc-is",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(exynos4_fimc_is_resource),
	.resource	= exynos4_fimc_is_resource,
};
#endif

#if defined(CONFIG_ARCH_EXYNOS5)
static struct resource exynos5_fimc_is_resource[] = {
	[0] = {
		.start	= EXYNOS5_PA_FIMC_IS,
		.end	= EXYNOS5_PA_FIMC_IS + SZ_2M + SZ_256K + SZ_128K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_ARMISP_GIC,
		.end	= IRQ_ARMISP_GIC,
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		.start	= IRQ_ISP_GIC,
		.end	= IRQ_ISP_GIC,
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device exynos5_device_fimc_is = {
	.name		= "exynos5-fimc-is",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(exynos5_fimc_is_resource),
	.resource	= exynos5_fimc_is_resource,
};
#endif

struct exynos4_platform_fimc_is exynos4_fimc_is_default_data __initdata = {
	.hw_ver = 15,
};

#if defined(CONFIG_ARCH_EXYNOS4)
void __init exynos4_fimc_is_set_platdata(struct exynos4_platform_fimc_is *pd)
{
	struct exynos4_platform_fimc_is *npd;

	if (!pd)
		pd = &exynos4_fimc_is_default_data;

	npd = kmemdup(pd, sizeof(struct exynos4_platform_fimc_is), GFP_KERNEL);

	if (!npd) {
		printk(KERN_ERR "%s: no memory for platform data\n", __func__);
	} else {
		if (!npd->cfg_gpio)
			npd->cfg_gpio = exynos_fimc_is_cfg_gpio;
		if (!npd->clk_cfg)
			npd->clk_cfg = exynos_fimc_is_cfg_clk;
		if (!npd->clk_on)
			npd->clk_on = exynos_fimc_is_clk_on;
		if (!npd->clk_off)
			npd->clk_off = exynos_fimc_is_clk_off;
		if (!npd->clk_get)
			npd->clk_get = exynos_fimc_is_clk_get;
		if (!npd->clk_put)
			npd->clk_put = exynos_fimc_is_clk_put;

		exynos4_device_fimc_is.dev.platform_data = npd;
	}
}
#endif

#if defined(CONFIG_ARCH_EXYNOS5)
void __init exynos5_fimc_is_set_platdata(struct exynos5_platform_fimc_is *pd)
{
	struct exynos5_platform_fimc_is *npd;

	if (!pd)
		pd = (struct exynos5_platform_fimc_is *)&exynos4_fimc_is_default_data;

	npd = kmemdup(pd, sizeof(struct exynos5_platform_fimc_is), GFP_KERNEL);

	if (!npd) {
		printk(KERN_ERR "%s: no memory for platform data\n", __func__);
	} else {

		if (!npd->cfg_gpio)
			npd->cfg_gpio = exynos5_fimc_is_cfg_gpio;
		if (!npd->clk_cfg)
			npd->clk_cfg = exynos5_fimc_is_cfg_clk;
		if (!npd->clk_on)
			npd->clk_on = exynos5_fimc_is_clk_on;
		if (!npd->clk_off)
			npd->clk_off = exynos5_fimc_is_clk_off;

		exynos5_device_fimc_is.dev.platform_data = npd;
	}
}
#endif
