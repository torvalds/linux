/*
 * Copyright (C) 2014 Texas Instruments Incorporated - http://www.ti.com/
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/sched.h>

#include "omapdss.h"
#include "dss.h"

struct dss_video_pll {
	struct dss_pll pll;

	struct device *dev;

	void __iomem *clkctrl_base;
};

#define REG_MOD(reg, val, start, end) \
	writel_relaxed(FLD_MOD(readl_relaxed(reg), val, start, end), reg)

static void dss_dpll_enable_scp_clk(struct dss_video_pll *vpll)
{
	REG_MOD(vpll->clkctrl_base, 1, 14, 14); /* CIO_CLK_ICG */
}

static void dss_dpll_disable_scp_clk(struct dss_video_pll *vpll)
{
	REG_MOD(vpll->clkctrl_base, 0, 14, 14); /* CIO_CLK_ICG */
}

static void dss_dpll_power_enable(struct dss_video_pll *vpll)
{
	REG_MOD(vpll->clkctrl_base, 2, 31, 30); /* PLL_POWER_ON_ALL */

	/*
	 * DRA7x PLL CTRL's PLL_PWR_STATUS seems to always return 0,
	 * so we have to use fixed delay here.
	 */
	msleep(1);
}

static void dss_dpll_power_disable(struct dss_video_pll *vpll)
{
	REG_MOD(vpll->clkctrl_base, 0, 31, 30);	/* PLL_POWER_OFF */
}

static int dss_video_pll_enable(struct dss_pll *pll)
{
	struct dss_video_pll *vpll = container_of(pll, struct dss_video_pll, pll);
	int r;

	r = dss_runtime_get(pll->dss);
	if (r)
		return r;

	dss_ctrl_pll_enable(pll, true);

	dss_dpll_enable_scp_clk(vpll);

	r = dss_pll_wait_reset_done(pll);
	if (r)
		goto err_reset;

	dss_dpll_power_enable(vpll);

	return 0;

err_reset:
	dss_dpll_disable_scp_clk(vpll);
	dss_ctrl_pll_enable(pll, false);
	dss_runtime_put(pll->dss);

	return r;
}

static void dss_video_pll_disable(struct dss_pll *pll)
{
	struct dss_video_pll *vpll = container_of(pll, struct dss_video_pll, pll);

	dss_dpll_power_disable(vpll);

	dss_dpll_disable_scp_clk(vpll);

	dss_ctrl_pll_enable(pll, false);

	dss_runtime_put(pll->dss);
}

static const struct dss_pll_ops dss_pll_ops = {
	.enable = dss_video_pll_enable,
	.disable = dss_video_pll_disable,
	.set_config = dss_pll_write_config_type_a,
};

static const struct dss_pll_hw dss_dra7_video_pll_hw = {
	.type = DSS_PLL_TYPE_A,

	.n_max = (1 << 8) - 1,
	.m_max = (1 << 12) - 1,
	.mX_max = (1 << 5) - 1,
	.fint_min = 500000,
	.fint_max = 2500000,
	.clkdco_max = 1800000000,

	.n_msb = 8,
	.n_lsb = 1,
	.m_msb = 20,
	.m_lsb = 9,

	.mX_msb[0] = 25,
	.mX_lsb[0] = 21,
	.mX_msb[1] = 30,
	.mX_lsb[1] = 26,
	.mX_msb[2] = 4,
	.mX_lsb[2] = 0,
	.mX_msb[3] = 9,
	.mX_lsb[3] = 5,

	.has_refsel = true,

	.errata_i886 = true,
	.errata_i932 = true,
};

struct dss_pll *dss_video_pll_init(struct dss_device *dss,
				   struct platform_device *pdev, int id,
				   struct regulator *regulator)
{
	const char * const reg_name[] = { "pll1", "pll2" };
	const char * const clkctrl_name[] = { "pll1_clkctrl", "pll2_clkctrl" };
	const char * const clkin_name[] = { "video1_clk", "video2_clk" };

	struct resource *res;
	struct dss_video_pll *vpll;
	void __iomem *pll_base, *clkctrl_base;
	struct clk *clk;
	struct dss_pll *pll;
	int r;

	/* PLL CONTROL */

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, reg_name[id]);
	pll_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(pll_base))
		return ERR_CAST(pll_base);

	/* CLOCK CONTROL */

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
		clkctrl_name[id]);
	clkctrl_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(clkctrl_base))
		return ERR_CAST(clkctrl_base);

	/* CLKIN */

	clk = devm_clk_get(&pdev->dev, clkin_name[id]);
	if (IS_ERR(clk)) {
		DSSERR("can't get video pll clkin\n");
		return ERR_CAST(clk);
	}

	vpll = devm_kzalloc(&pdev->dev, sizeof(*vpll), GFP_KERNEL);
	if (!vpll)
		return ERR_PTR(-ENOMEM);

	vpll->dev = &pdev->dev;
	vpll->clkctrl_base = clkctrl_base;

	pll = &vpll->pll;

	pll->name = id == 0 ? "video0" : "video1";
	pll->id = id == 0 ? DSS_PLL_VIDEO1 : DSS_PLL_VIDEO2;
	pll->clkin = clk;
	pll->regulator = regulator;
	pll->base = pll_base;
	pll->hw = &dss_dra7_video_pll_hw;
	pll->ops = &dss_pll_ops;

	r = dss_pll_register(dss, pll);
	if (r)
		return ERR_PTR(r);

	return pll;
}

void dss_video_pll_uninit(struct dss_pll *pll)
{
	dss_pll_unregister(pll);
}
