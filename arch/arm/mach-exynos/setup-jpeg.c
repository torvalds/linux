/* linux/arch/arm/mach-exynos/setup-jpeg.c
 *
 * Copyright (c) 2009-2011 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com
 *
 * Base Exynos4 JPEG configuration
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/platform_device.h>

#include <plat/gpio-cfg.h>
#include <plat/clock.h>

#include <mach/regs-clock.h>
#include <mach/map.h>

int __init exynos4_jpeg_setup_clock(struct device *dev,
					unsigned long clk_rate)
{
	struct clk *sclk = NULL;
	struct clk *mout_jpeg = NULL;
	struct clk *mout_mpll = NULL;
	int ret;

	sclk = clk_get(dev, "aclk_clk_jpeg");
	if (IS_ERR(sclk)) {
		dev_err(dev, "failed to get aclk for jpeg\n");
		goto err_clk1;
	}

	mout_jpeg = clk_get(dev, "mout_jpeg0");

	if (IS_ERR(mout_jpeg)) {
		dev_err(dev, "failed to get mout_jpeg0 for jpeg\n");
		goto err_clk2;
	}

	ret = clk_set_parent(sclk, mout_jpeg);
	if (ret < 0) {
		dev_err(dev, "failed to clk_set_parent for jpeg\n");
		goto err_clk2;
	}

	mout_mpll = clk_get(dev, "mout_mpll_user");

	if (IS_ERR(mout_mpll)) {
		dev_err(dev, "failed to get mout_mpll for jpeg\n");
		goto err_clk2;
	}

	ret = clk_set_parent(mout_jpeg, mout_mpll);
	if (ret < 0) {
		dev_err(dev, "failed to clk_set_parent for jpeg\n");
		goto err_clk2;
	}

	ret = clk_set_rate(sclk, clk_rate);
	if (ret < 0) {
		dev_err(dev, "failed to clk_set_rate of sclk for jpeg\n");
		goto err_clk2;
	}
	dev_dbg(dev, "set jpeg aclk rate\n");

	clk_put(mout_jpeg);
	clk_put(mout_mpll);

	ret = clk_enable(sclk);
	if (ret < 0) {
		dev_err(dev, "failed to clk_enable of aclk for jpeg\n");
		goto err_clk2;
	}

	return 0;

err_clk2:
	clk_put(mout_mpll);
err_clk1:
	clk_put(sclk);

	return -EINVAL;
}

int __init exynos5_jpeg_setup_clock(struct device *dev,
					unsigned long clk_rate)
{
	struct clk *sclk;

	sclk = clk_get(dev, "sclk_jpeg");
	if (IS_ERR(sclk))
		return PTR_ERR(sclk);

	if (!clk_rate)
		clk_rate = 150000000UL;

	if (clk_set_rate(sclk, clk_rate)) {
		pr_err("%s rate change failed: %lu\n", sclk->name, clk_rate);
		clk_put(sclk);
		return PTR_ERR(sclk);
	}

	clk_put(sclk);

	return 0;
}
