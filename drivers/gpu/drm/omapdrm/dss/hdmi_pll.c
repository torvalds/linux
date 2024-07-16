// SPDX-License-Identifier: GPL-2.0-only
/*
 * HDMI PLL
 *
 * Copyright (C) 2013 Texas Instruments Incorporated - https://www.ti.com/
 */

#define DSS_SUBSYS_NAME "HDMIPLL"

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/seq_file.h>
#include <linux/pm_runtime.h>

#include "omapdss.h"
#include "dss.h"
#include "hdmi.h"

void hdmi_pll_dump(struct hdmi_pll_data *pll, struct seq_file *s)
{
#define DUMPPLL(r) seq_printf(s, "%-35s %08x\n", #r,\
		hdmi_read_reg(pll->base, r))

	DUMPPLL(PLLCTRL_PLL_CONTROL);
	DUMPPLL(PLLCTRL_PLL_STATUS);
	DUMPPLL(PLLCTRL_PLL_GO);
	DUMPPLL(PLLCTRL_CFG1);
	DUMPPLL(PLLCTRL_CFG2);
	DUMPPLL(PLLCTRL_CFG3);
	DUMPPLL(PLLCTRL_SSC_CFG1);
	DUMPPLL(PLLCTRL_SSC_CFG2);
	DUMPPLL(PLLCTRL_CFG4);
}

static int hdmi_pll_enable(struct dss_pll *dsspll)
{
	struct hdmi_pll_data *pll = container_of(dsspll, struct hdmi_pll_data, pll);
	struct hdmi_wp_data *wp = pll->wp;
	int r;

	r = pm_runtime_get_sync(&pll->pdev->dev);
	WARN_ON(r < 0);

	dss_ctrl_pll_enable(dsspll, true);

	r = hdmi_wp_set_pll_pwr(wp, HDMI_PLLPWRCMD_BOTHON_ALLCLKS);
	if (r)
		return r;

	return 0;
}

static void hdmi_pll_disable(struct dss_pll *dsspll)
{
	struct hdmi_pll_data *pll = container_of(dsspll, struct hdmi_pll_data, pll);
	struct hdmi_wp_data *wp = pll->wp;
	int r;

	hdmi_wp_set_pll_pwr(wp, HDMI_PLLPWRCMD_ALLOFF);

	dss_ctrl_pll_enable(dsspll, false);

	r = pm_runtime_put_sync(&pll->pdev->dev);
	WARN_ON(r < 0 && r != -ENOSYS);
}

static const struct dss_pll_ops hdmi_pll_ops = {
	.enable = hdmi_pll_enable,
	.disable = hdmi_pll_disable,
	.set_config = dss_pll_write_config_type_b,
};

static const struct dss_pll_hw dss_omap4_hdmi_pll_hw = {
	.type = DSS_PLL_TYPE_B,

	.n_max = 255,
	.m_min = 20,
	.m_max = 4095,
	.mX_max = 127,
	.fint_min = 500000,
	.fint_max = 2500000,

	.clkdco_min = 500000000,
	.clkdco_low = 1000000000,
	.clkdco_max = 2000000000,

	.n_msb = 8,
	.n_lsb = 1,
	.m_msb = 20,
	.m_lsb = 9,

	.mX_msb[0] = 24,
	.mX_lsb[0] = 18,

	.has_selfreqdco = true,
};

static const struct dss_pll_hw dss_omap5_hdmi_pll_hw = {
	.type = DSS_PLL_TYPE_B,

	.n_max = 255,
	.m_min = 20,
	.m_max = 2045,
	.mX_max = 127,
	.fint_min = 620000,
	.fint_max = 2500000,

	.clkdco_min = 750000000,
	.clkdco_low = 1500000000,
	.clkdco_max = 2500000000UL,

	.n_msb = 8,
	.n_lsb = 1,
	.m_msb = 20,
	.m_lsb = 9,

	.mX_msb[0] = 24,
	.mX_lsb[0] = 18,

	.has_selfreqdco = true,
	.has_refsel = true,
};

static int hdmi_init_pll_data(struct dss_device *dss,
			      struct platform_device *pdev,
			      struct hdmi_pll_data *hpll)
{
	struct dss_pll *pll = &hpll->pll;
	struct clk *clk;
	int r;

	clk = devm_clk_get(&pdev->dev, "sys_clk");
	if (IS_ERR(clk)) {
		DSSERR("can't get sys_clk\n");
		return PTR_ERR(clk);
	}

	pll->name = "hdmi";
	pll->id = DSS_PLL_HDMI;
	pll->base = hpll->base;
	pll->clkin = clk;

	if (hpll->wp->version == 4)
		pll->hw = &dss_omap4_hdmi_pll_hw;
	else
		pll->hw = &dss_omap5_hdmi_pll_hw;

	pll->ops = &hdmi_pll_ops;

	r = dss_pll_register(dss, pll);
	if (r)
		return r;

	return 0;
}

int hdmi_pll_init(struct dss_device *dss, struct platform_device *pdev,
		  struct hdmi_pll_data *pll, struct hdmi_wp_data *wp)
{
	int r;

	pll->pdev = pdev;
	pll->wp = wp;

	pll->base = devm_platform_ioremap_resource_byname(pdev, "pll");
	if (IS_ERR(pll->base))
		return PTR_ERR(pll->base);

	r = hdmi_init_pll_data(dss, pdev, pll);
	if (r) {
		DSSERR("failed to init HDMI PLL\n");
		return r;
	}

	return 0;
}

void hdmi_pll_uninit(struct hdmi_pll_data *hpll)
{
	struct dss_pll *pll = &hpll->pll;

	dss_pll_unregister(pll);
}
