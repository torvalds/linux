/* linux/arch/arm/mach-exynos/setup-csis.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *	http://www.samsung.com/
 *
 * EXYNOS4 - Base MIPI-CSI2 gpio configuration
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/gpio.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <plat/clock.h>
#include <plat/gpio-cfg.h>
#include <mach/regs-gpio.h>
#include <plat/map-s5p.h>
#include <mach/map.h>
#include <mach/regs-clock.h>
#include <plat/csis.h>

#ifdef DEBUG
#define dbg(fmt, args...) \
	printk(KERN_INFO "%s:%d: " fmt "\n", __func__, __LINE__, ##args)
#else
#define dbg(fmt, args...)
#endif

struct platform_device; /* don't need the contents */

void s3c_csis0_cfg_gpio(void) { }
void s3c_csis1_cfg_gpio(void) { }

void s3c_csis0_cfg_phy_global(int on)
{
	u32 cfg;

	if (on) {
		/* MIPI D-PHY Power Enable */
		cfg = __raw_readl(EXYNOS4_MIPI_CONTROL0);
		cfg |= EXYNOS4_MIPI_DPHY_S_RESETN;
		__raw_writel(cfg, EXYNOS4_MIPI_CONTROL0);

		cfg = __raw_readl(EXYNOS4_MIPI_CONTROL0);
		cfg |= EXYNOS4_MIPI_DPHY_EN;
		__raw_writel(cfg, EXYNOS4_MIPI_CONTROL0);

		dbg(KERN_INFO "csis0 on\n");
	} else {
		/* MIPI Power Disable */
		cfg = __raw_readl(EXYNOS4_MIPI_CONTROL0);
		cfg &= ~EXYNOS4_MIPI_DPHY_S_RESETN;

		if (!(cfg & EXYNOS4_MIPI_DPHY_M_RESETN))
			cfg &= ~EXYNOS4_MIPI_DPHY_EN;

		__raw_writel(cfg, EXYNOS4_MIPI_CONTROL0);

		dbg(KERN_INFO "csis0 off\n");
	}
}
void s3c_csis1_cfg_phy_global(int on)
{
	u32 cfg;

	if (on) {
		/* MIPI D-PHY Power Enable */
		cfg = __raw_readl(EXYNOS4_MIPI_CONTROL1);
		cfg |= EXYNOS4_MIPI_DPHY_S_RESETN;
		__raw_writel(cfg, EXYNOS4_MIPI_CONTROL1);

		cfg = __raw_readl(EXYNOS4_MIPI_CONTROL1);
		cfg |= EXYNOS4_MIPI_DPHY_EN;
		__raw_writel(cfg, EXYNOS4_MIPI_CONTROL1);

		dbg(KERN_INFO "csis1 on\n");
	} else {
		/* MIPI Power Disable */
		cfg = __raw_readl(EXYNOS4_MIPI_CONTROL1);
		cfg &= ~EXYNOS4_MIPI_DPHY_S_RESETN;

		if (!(cfg & EXYNOS4_MIPI_DPHY_M_RESETN))
			cfg &= ~EXYNOS4_MIPI_DPHY_EN;

		__raw_writel(cfg, EXYNOS4_MIPI_CONTROL1);

		dbg(KERN_INFO "csis1 off\n");
	}
}
int s3c_csis_clk_on(struct platform_device *pdev, struct clk **clk)
{
	struct s3c_platform_csis *pdata;
	struct clk *sclk_csis = NULL;
	struct clk *mout_mpll = NULL;

	pdata = to_csis_plat(&pdev->dev);

	/* mout_mpll */
	mout_mpll = clk_get(&pdev->dev, pdata->srclk_name);
	if (IS_ERR(mout_mpll)) {
		dev_err(&pdev->dev, "failed to get mout_mpll\n");
		goto err_clk1;
	}

	/* sclk_csis */
	sclk_csis = clk_get(&pdev->dev, pdata->clk_name);
	if (IS_ERR(sclk_csis)) {
		dev_err(&pdev->dev, "failed to get sclk_csis\n");
		goto err_clk2;
	}

	if (clk_set_parent(sclk_csis, mout_mpll)) {
		dev_err(&pdev->dev, "Unable to set parent %s of clock %s.\n",
				mout_mpll->name, sclk_csis->name);
		goto err_clk3;
	}

	if (clk_set_rate(sclk_csis, pdata->clk_rate)) {
		dev_err(&pdev->dev, "%s rate change failed: %lu\n",
				sclk_csis->name, pdata->clk_rate);
		goto err_clk3;
	}

	/* csis */
	*clk = clk_get(&pdev->dev, "csis");
	if (IS_ERR(*clk)) {
		dev_err(&pdev->dev, "failed to get csis clock\n");
		goto err_clk3;
	}
	/* clock enable for csis */
	clk_enable(*clk);
	clk_enable(sclk_csis);

	return 0;

err_clk3:
	clk_put(sclk_csis);

err_clk2:
	clk_put(mout_mpll);

err_clk1:
	return -EINVAL;
}
int s3c_csis_clk_off(struct platform_device *pdev, struct clk **clk)
{
	clk_disable(*clk);
	clk_put(*clk);

	*clk = NULL;

	return 0;
}
