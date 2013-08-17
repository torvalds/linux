/* linux/arm/mach-exynos4/setup-fimd0.c
 *
 * Copyright (c) 2009-2011 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com
 *
 * Base Exynos4 FIMD 0 configuration
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

static void exynos4_fimd0_cfg_gpios(unsigned int base, unsigned int nr,
				    unsigned int cfg, s5p_gpio_drvstr_t drvstr)
{
	s3c_gpio_cfgrange_nopull(base, nr, cfg);

	for (; nr > 0; nr--, base++)
		s5p_gpio_set_drvstr(base, drvstr);
}

void exynos4_fimd0_gpio_setup_24bpp(void)
{
	unsigned int reg = 0;

	exynos4_fimd0_cfg_gpios(EXYNOS4_GPF0(0), 8, \
				S3C_GPIO_SFN(2), S5P_GPIO_DRVSTR_LV4);
	exynos4_fimd0_cfg_gpios(EXYNOS4_GPF1(0), 8, \
				S3C_GPIO_SFN(2), S5P_GPIO_DRVSTR_LV1);
	exynos4_fimd0_cfg_gpios(EXYNOS4_GPF2(0), 8, \
				S3C_GPIO_SFN(2), S5P_GPIO_DRVSTR_LV1);
	exynos4_fimd0_cfg_gpios(EXYNOS4_GPF3(0), 4, \
				S3C_GPIO_SFN(2), S5P_GPIO_DRVSTR_LV1);

	/*
	 * Set DISPLAY_CONTROL register for Display path selection.
	 *
	 * DISPLAY_CONTROL[1:0]
	 * ---------------------
	 *  00 | MIE
	 *  01 | MDINE
	 *  10 | FIMD : selected
	 *  11 | FIMD
	 */
	reg = __raw_readl(S3C_VA_SYS + 0x0210);
	reg |= (1 << 1);
	__raw_writel(reg, S3C_VA_SYS + 0x0210);
}

int __init exynos4_fimd_setup_clock(struct device *dev, const char *bus_clk,
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
