/* linux/arch/arm/mach-exynos/setup-jpeg-hx.c
 *
 * Copyright (c) 2009-2012 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com
 *
 * Base Exynos5 JPEG configuration
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

int exynos5_jpeg_hx_setup_clock(struct device *dev,
					unsigned long clk_rate)
{
	struct clk *sclk = NULL;
	struct clk *dout_jpeg = NULL;
	struct clk *mout_dpll = NULL;
	int ret;

	sclk = clk_get(dev, "aclk_300_jpeg");
	if (IS_ERR(sclk)) {
		dev_err(dev, "failed to get aclk for jpeg\n");
		goto err_clk1;
	}

	dout_jpeg = clk_get(dev, "dout_aclk_300_jpeg");

	if (IS_ERR(dout_jpeg)) {
		dev_err(dev, "failed to get dout_jpeg for jpeg\n");
		goto err_clk2;
	}

	clk_enable(dout_jpeg);

	ret = clk_set_parent(sclk, dout_jpeg);
	if (ret < 0) {
		dev_err(dev, "failed to clk_set_parent for jpeg\n");
		goto err_clk3;
	}

	mout_dpll = clk_get(dev, "mout_dpll");

	if (IS_ERR(mout_dpll)) {
		dev_err(dev, "failed to get mout_dpll for jpeg\n");
		goto err_clk3;
	}

	ret = clk_set_parent(dout_jpeg, mout_dpll);
	if (ret < 0) {
		dev_err(dev, "failed to clk_set_parent for jpeg\n");
		goto err_clk4;
	}

	ret = clk_set_rate(dout_jpeg, clk_rate);
	if (ret < 0) {
		dev_err(dev, "failed to clk_set_rate of sclk for jpeg\n");
		goto err_clk4;
	}
	dev_dbg(dev, "set jpeg aclk rate\n");

	clk_disable(dout_jpeg);
	clk_put(dout_jpeg);
	clk_put(mout_dpll);
	clk_put(sclk);
	return 0;

err_clk4:
	clk_put(mout_dpll);
err_clk3:
	clk_put(dout_jpeg);
	clk_disable(dout_jpeg);
err_clk2:
	clk_put(sclk);
err_clk1:
	return -EINVAL;
}
