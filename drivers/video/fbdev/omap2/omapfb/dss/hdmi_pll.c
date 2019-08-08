// SPDX-License-Identifier: GPL-2.0-only
/*
 * HDMI PLL
 *
 * Copyright (C) 2013 Texas Instruments Incorporated
 */

#define DSS_SUBSYS_NAME "HDMIPLL"

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/seq_file.h>

#include <video/omapfb_dss.h>

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

void hdmi_pll_compute(struct hdmi_pll_data *pll,
	unsigned long target_tmds, struct dss_pll_clock_info *pi)
{
	unsigned long fint, clkdco, clkout;
	unsigned long target_bitclk, target_clkdco;
	unsigned long min_dco;
	unsigned n, m, mf, m2, sd;
	unsigned long clkin;
	const struct dss_pll_hw *hw = pll->pll.hw;

	clkin = clk_get_rate(pll->pll.clkin);

	DSSDBG("clkin %lu, target tmds %lu\n", clkin, target_tmds);

	target_bitclk = target_tmds * 10;

	/* Fint */
	n = DIV_ROUND_UP(clkin, hw->fint_max);
	fint = clkin / n;

	/* adjust m2 so that the clkdco will be high enough */
	min_dco = roundup(hw->clkdco_min, fint);
	m2 = DIV_ROUND_UP(min_dco, target_bitclk);
	if (m2 == 0)
		m2 = 1;

	target_clkdco = target_bitclk * m2;
	m = target_clkdco / fint;

	clkdco = fint * m;

	/* adjust clkdco with fractional mf */
	if (WARN_ON(target_clkdco - clkdco > fint))
		mf = 0;
	else
		mf = (u32)div_u64(262144ull * (target_clkdco - clkdco), fint);

	if (mf > 0)
		clkdco += (u32)div_u64((u64)mf * fint, 262144);

	clkout = clkdco / m2;

	/* sigma-delta */
	sd = DIV_ROUND_UP(fint * m, 250000000);

	DSSDBG("N = %u, M = %u, M.f = %u, M2 = %u, SD = %u\n",
		n, m, mf, m2, sd);
	DSSDBG("Fint %lu, clkdco %lu, clkout %lu\n", fint, clkdco, clkout);

	pi->n = n;
	pi->m = m;
	pi->mf = mf;
	pi->mX[0] = m2;
	pi->sd = sd;

	pi->fint = fint;
	pi->clkdco = clkdco;
	pi->clkout[0] = clkout;
}

static int hdmi_pll_enable(struct dss_pll *dsspll)
{
	struct hdmi_pll_data *pll = container_of(dsspll, struct hdmi_pll_data, pll);
	struct hdmi_wp_data *wp = pll->wp;
	u16 r = 0;

	dss_ctrl_pll_enable(DSS_PLL_HDMI, true);

	r = hdmi_wp_set_pll_pwr(wp, HDMI_PLLPWRCMD_BOTHON_ALLCLKS);
	if (r)
		return r;

	return 0;
}

static void hdmi_pll_disable(struct dss_pll *dsspll)
{
	struct hdmi_pll_data *pll = container_of(dsspll, struct hdmi_pll_data, pll);
	struct hdmi_wp_data *wp = pll->wp;

	hdmi_wp_set_pll_pwr(wp, HDMI_PLLPWRCMD_ALLOFF);

	dss_ctrl_pll_enable(DSS_PLL_HDMI, false);
}

static const struct dss_pll_ops dsi_pll_ops = {
	.enable = hdmi_pll_enable,
	.disable = hdmi_pll_disable,
	.set_config = dss_pll_write_config_type_b,
};

static const struct dss_pll_hw dss_omap4_hdmi_pll_hw = {
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

static int dsi_init_pll_data(struct platform_device *pdev, struct hdmi_pll_data *hpll)
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

	switch (omapdss_get_version()) {
	case OMAPDSS_VER_OMAP4430_ES1:
	case OMAPDSS_VER_OMAP4430_ES2:
	case OMAPDSS_VER_OMAP4:
		pll->hw = &dss_omap4_hdmi_pll_hw;
		break;

	case OMAPDSS_VER_OMAP5:
	case OMAPDSS_VER_DRA7xx:
		pll->hw = &dss_omap5_hdmi_pll_hw;
		break;

	default:
		return -ENODEV;
	}

	pll->ops = &dsi_pll_ops;

	r = dss_pll_register(pll);
	if (r)
		return r;

	return 0;
}

int hdmi_pll_init(struct platform_device *pdev, struct hdmi_pll_data *pll,
	struct hdmi_wp_data *wp)
{
	int r;
	struct resource *res;

	pll->wp = wp;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "pll");
	if (!res) {
		DSSERR("can't get PLL mem resource\n");
		return -EINVAL;
	}

	pll->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(pll->base)) {
		DSSERR("can't ioremap PLLCTRL\n");
		return PTR_ERR(pll->base);
	}

	r = dsi_init_pll_data(pdev, pll);
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
