/* linux/arch/arm/mach-exynos/setup-fimd1.c
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com
 *
 * Base Exynos FIMD 1 configuration
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/fb.h>
#include <linux/gpio.h>
#include <linux/clk.h>

#include <plat/fb.h>
#include <plat/gpio-cfg.h>
#include <plat/clock.h>
#include <plat/regs-fb-v4.h>

#include <mach/map.h>

void exynos5_fimd1_gpio_setup_24bpp(void)
{
	unsigned int reg;

	/*
	 * Set DISP1BLK_CFG register for Display path selection
	 *
	 * FIMD of DISP1_BLK Bypass selection : DISP1BLK_CFG[15]
	 * ---------------------
	 *  0 | MIE/MDINE
	 *  1 | FIMD : selected
	 */
	reg = __raw_readl(S3C_VA_SYS + 0x0214);
	reg |= (1 << 15);
	__raw_writel(reg, S3C_VA_SYS + 0x0214);
}

int __init exynos5_fimd1_setup_clock(struct device *dev, const char *bus_clk,
		const char *parent, unsigned long clk_rate)
{
	struct clk *clk_parent;
	struct clk *sclk;

	sclk = clk_get(dev, bus_clk);
	if (IS_ERR(sclk))
		return PTR_ERR(sclk);

	clk_parent = clk_get(NULL, parent);
	if (IS_ERR(clk_parent)) {
		clk_put(sclk);
		return PTR_ERR(clk_parent);
	}

	if (clk_set_parent(sclk, clk_parent)) {
		pr_err("Unable to set parent %s of clock %s.\n",
				clk_parent->name, sclk->name);
		clk_put(sclk);
		clk_put(clk_parent);
		return PTR_ERR(sclk);
	}

	if (!clk_rate)
		clk_rate = 134000000UL;

	if (clk_set_rate(sclk, clk_rate)) {
		pr_err("%s rate change failed: %lu\n", sclk->name, clk_rate);
		clk_put(sclk);
		clk_put(clk_parent);
		return PTR_ERR(sclk);
	}

	clk_put(sclk);
	clk_put(clk_parent);

	return 0;
}

