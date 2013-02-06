/*
 * Copyright (C) 2010-2011 Samsung Electronics Co., Ltd.
 *
 * S5P series device definition for MIPI-CSIS
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/platform_device.h>
#include <asm/irq.h>
#include <mach/map.h>
#include <plat/devs.h>
#include <plat/cpu.h>
#include <plat/csis.h>

static struct resource s3c_csis0_resource[] = {
	[0] = {
		.start	= S5P_PA_MIPI_CSIS0,
		.end	= S5P_PA_MIPI_CSIS0 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_MIPICSI0,
		.end	= IRQ_MIPICSI0,
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device s3c_device_csis0 = {
	.name		= "s3c-csis",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(s3c_csis0_resource),
	.resource	= s3c_csis0_resource,
};

static struct s3c_platform_csis default_csis0_data __initdata = {
	.srclk_name	= "mout_mpll",
	.clk_name	= "sclk_csis",
	.clk_rate	= 166000000,
};

int fimc_clk_rate(void)
{
	if (samsung_rev() >= EXYNOS4412_REV_2_0)
		return 180000000;
	else
		return 166750000;
}

void __init s3c_csis0_set_platdata(struct s3c_platform_csis *pd)
{
	struct s3c_platform_csis *npd;

	if (!pd) {
		default_csis0_data.clk_rate = fimc_clk_rate();
		pd = &default_csis0_data;
	}

	npd = kmemdup(pd, sizeof(struct s3c_platform_csis), GFP_KERNEL);
	if (!npd) {
		printk(KERN_ERR "%s: no memory for platform data\n", __func__);
		return;
	}

	if (soc_is_exynos4212() || soc_is_exynos4412())
		npd->srclk_name = "mout_mpll_user";
	npd->cfg_gpio = s3c_csis0_cfg_gpio;
	npd->cfg_phy_global = s3c_csis0_cfg_phy_global;
	npd->clk_on = s3c_csis_clk_on;
	npd->clk_off = s3c_csis_clk_off;
	s3c_device_csis0.dev.platform_data = npd;
}
static struct resource s3c_csis1_resource[] = {
	[0] = {
		.start	= S5P_PA_MIPI_CSIS1,
		.end	= S5P_PA_MIPI_CSIS1 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_MIPICSI1,
		.end	= IRQ_MIPICSI1,
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device s3c_device_csis1 = {
	.name		= "s3c-csis",
	.id		= 1,
	.num_resources	= ARRAY_SIZE(s3c_csis1_resource),
	.resource	= s3c_csis1_resource,
};

static struct s3c_platform_csis default_csis1_data __initdata = {
	.srclk_name	= "mout_mpll",
	.clk_name	= "sclk_csis",
	.clk_rate	= 166000000,
};

void __init s3c_csis1_set_platdata(struct s3c_platform_csis *pd)
{
	struct s3c_platform_csis *npd;

	if (!pd) {
		default_csis1_data.clk_rate = fimc_clk_rate();
		pd = &default_csis1_data;
	}

	npd = kmemdup(pd, sizeof(struct s3c_platform_csis), GFP_KERNEL);
	if (!npd) {
		printk(KERN_ERR "%s: no memory for platform data\n", __func__);
		return;
	}

	if (soc_is_exynos4212() || soc_is_exynos4412())
		npd->srclk_name = "mout_mpll_user";
	npd->cfg_gpio = s3c_csis1_cfg_gpio;
	npd->cfg_phy_global = s3c_csis1_cfg_phy_global;
	npd->clk_on = s3c_csis_clk_on;
	npd->clk_off = s3c_csis_clk_off;
	s3c_device_csis1.dev.platform_data = npd;
}
