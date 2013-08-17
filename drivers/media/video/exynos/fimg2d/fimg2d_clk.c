/* linux/drivers/media/video/exynos/fimg2d/fimg2d_clk.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *	http://www.samsung.com/
 *
 * Samsung Graphics 2D driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/err.h>
#include <linux/clk.h>
#include <linux/atomic.h>
#include <linux/sched.h>
#include <plat/cpu.h>
#include <plat/fimg2d.h>
#include "fimg2d.h"
#include "fimg2d_clk.h"

void fimg2d_clk_on(struct fimg2d_control *ctrl)
{
	clk_enable(ctrl->clock);
}

void fimg2d_clk_off(struct fimg2d_control *ctrl)
{
	clk_disable(ctrl->clock);
}

int fimg2d_clk_setup(struct fimg2d_control *ctrl)
{
	struct fimg2d_platdata *pdata;
	struct clk *parent, *sclk;
	int ret = 0;

	sclk = parent = NULL;
	pdata = to_fimg2d_plat(ctrl->dev);

	if (ip_is_g2d_5g() || ip_is_g2d_5a()) {
		fimg2d_info("aclk_acp(%lu) pclk_acp(%lu)\n",
				clk_get_rate(clk_get(NULL, "aclk_acp")),
				clk_get_rate(clk_get(NULL, "pclk_acp")));
	} else {
		parent = clk_get(ctrl->dev, pdata->parent_clkname);
		if (IS_ERR(parent)) {
			fimg2d_err("failed to get parent clk\n");
			ret = -ENOENT;
			goto err_clk1;
		}
		fimg2d_info("parent clk: %s\n", pdata->parent_clkname);

		sclk = clk_get(ctrl->dev, pdata->clkname);
		if (IS_ERR(sclk)) {
			fimg2d_err("failed to get sclk\n");
			ret = -ENOENT;
			goto err_clk2;
		}
		fimg2d_info("sclk: %s\n", pdata->clkname);

		if (clk_set_parent(sclk, parent))
			fimg2d_err("failed to set parent\n");

		clk_set_rate(sclk, pdata->clkrate);
		fimg2d_info("clkrate: %ld parent clkrate: %ld\n",
				clk_get_rate(sclk), clk_get_rate(parent));
	}

	/* clock for gating */
	ctrl->clock = clk_get(ctrl->dev, pdata->gate_clkname);
	if (IS_ERR(ctrl->clock)) {
		fimg2d_err("failed to get gate clk\n");
		ret = -ENOENT;
		goto err_clk3;
	}
	fimg2d_info("gate clk: %s\n", pdata->gate_clkname);
	return ret;

err_clk3:
	if (sclk)
		clk_put(sclk);

err_clk2:
	if (parent)
		clk_put(parent);

err_clk1:
	return ret;
}

void fimg2d_clk_release(struct fimg2d_control *ctrl)
{
	clk_put(ctrl->clock);
	if (ip_is_g2d_4p()) {
		struct fimg2d_platdata *pdata;
		pdata = to_fimg2d_plat(ctrl->dev);
		clk_put(clk_get(ctrl->dev, pdata->clkname));
		clk_put(clk_get(ctrl->dev, pdata->parent_clkname));
	}
}
