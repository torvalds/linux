/* linux/drivers/media/video/samsung/fimg2d4x/fimg2d_clk.c
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

#include <linux/clk.h>
#include <linux/atomic.h>
#include <linux/sched.h>
#include <plat/cpu.h>
#include <plat/fimg2d.h>
#include "fimg2d.h"
#include "fimg2d_clk.h"

void fimg2d_clk_on(struct fimg2d_control *info)
{
	spin_lock(&info->bltlock);
	clk_enable(info->clock);
	atomic_set(&info->clkon, 1);
	spin_unlock(&info->bltlock);

	fimg2d_debug("clock enable\n");
}

void fimg2d_clk_off(struct fimg2d_control *info)
{
	spin_lock(&info->bltlock);
	atomic_set(&info->clkon, 0);
	clk_disable(info->clock);
	spin_unlock(&info->bltlock);

	fimg2d_debug("clock disable\n");
}

void fimg2d_clk_save(struct fimg2d_control *info)
{
	if (soc_is_exynos4212() || soc_is_exynos4412()) {
		struct fimg2d_platdata *pdata;
		struct clk *sclk;

		pdata = to_fimg2d_plat(info->dev);

		spin_lock(&info->bltlock);
		sclk = clk_get(info->dev, pdata->clkname);
		clk_set_rate(sclk, 50*MHZ); /* 800MHz/16=50MHz */
		spin_unlock(&info->bltlock);

		fimg2d_debug("%s clkrate=%lu\n", pdata->clkname, clk_get_rate(sclk));
	}
}

void fimg2d_clk_restore(struct fimg2d_control *info)
{
	if (soc_is_exynos4212() || soc_is_exynos4412()) {
		struct fimg2d_platdata *pdata;
		struct clk *sclk, *pclk;

		pdata = to_fimg2d_plat(info->dev);

		spin_lock(&info->bltlock);
		sclk = clk_get(info->dev, pdata->clkname);
		pclk = clk_get(NULL, "pclk_acp");
		clk_set_rate(sclk, clk_get_rate(pclk) * 2);
		spin_unlock(&info->bltlock);

		fimg2d_debug("%s(%lu) pclk_acp(%lu)\n", pdata->clkname,
				clk_get_rate(sclk), clk_get_rate(pclk));
	}
}

void fimg2d_clk_dump(struct fimg2d_control *info)
{
	struct fimg2d_platdata *pdata;
	struct clk *sclk, *pclk, *aclk;

	pdata = to_fimg2d_plat(info->dev);

	if (soc_is_exynos4212() || soc_is_exynos4412()) {
		sclk = clk_get(info->dev, pdata->clkname);
		pclk = clk_get(NULL, "pclk_acp");

		printk(KERN_INFO "%s(%lu) pclk_acp(%lu)\n",
				pdata->clkname,
				clk_get_rate(sclk), clk_get_rate(pclk));
	} else {
		aclk = clk_get(NULL, "aclk_acp");
		pclk = clk_get(NULL, "pclk_acp");

		printk(KERN_INFO "aclk_acp(%lu) pclk_acp(%lu)\n",
				clk_get_rate(aclk), clk_get_rate(pclk));
	}
}

int fimg2d_clk_setup(struct fimg2d_control *info)
{
	struct fimg2d_platdata *pdata;
	struct clk *parent, *sclk;
	int ret = 0;

	sclk = parent = NULL;
	pdata = to_fimg2d_plat(info->dev);

	if (soc_is_exynos4212() || soc_is_exynos4412()) {
		/* clock for setting parent and rate */
		parent = clk_get(info->dev, pdata->parent_clkname);
		if (IS_ERR(parent)) {
			printk(KERN_ERR "FIMG2D failed to get parent clk\n");
			ret = -ENOENT;
			goto err_clk1;
		}
		fimg2d_debug("parent clk: %s\n", pdata->parent_clkname);

		sclk = clk_get(info->dev, pdata->clkname);
		if (IS_ERR(sclk)) {
			printk(KERN_ERR "FIMG2D failed to get sclk\n");
			ret = -ENOENT;
			goto err_clk2;
		}
		fimg2d_debug("sclk: %s\n", pdata->clkname);

		if (clk_set_parent(sclk, parent))
			printk(KERN_ERR "FIMG2D failed to set parent\n");

		clk_set_rate(sclk, pdata->clkrate);
		fimg2d_debug("clkrate: %ld parent clkrate: %ld\n",
				clk_get_rate(sclk), clk_get_rate(parent));
	} else {
		fimg2d_debug("aclk_acp(%lu) pclk_acp(%lu)\n",
				clk_get_rate(clk_get(NULL, "aclk_acp")),
				clk_get_rate(clk_get(NULL, "pclk_acp")));
	}

	/* clock for gating */
	info->clock = clk_get(info->dev, pdata->gate_clkname);
	if (IS_ERR(info->clock)) {
		printk(KERN_ERR "FIMG2D failed to get gate clk\n");
		ret = -ENOENT;
		goto err_clk3;
	}
	fimg2d_debug("gate clk: %s\n", pdata->gate_clkname);
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

void fimg2d_clk_release(struct fimg2d_control *info)
{
	clk_put(info->clock);
	if (soc_is_exynos4212() || soc_is_exynos4412()) {
		struct fimg2d_platdata *pdata;
		pdata = to_fimg2d_plat(info->dev);
		clk_put(clk_get(info->dev, pdata->clkname));
		clk_put(clk_get(info->dev, pdata->parent_clkname));
	}
}
