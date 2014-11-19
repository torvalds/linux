/*
 * HDMI PLL
 *
 * Copyright (C) 2013 Texas Instruments Incorporated
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#define DSS_SUBSYS_NAME "HDMIPLL"

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <video/omapdss.h>

#include "dss.h"
#include "hdmi.h"

#define HDMI_DEFAULT_REGN 16
#define HDMI_DEFAULT_REGM2 1

struct hdmi_pll_features {
	bool sys_reset;
	/* this is a hack, need to replace it with a better computation of M2 */
	bool bound_dcofreq;
	unsigned long fint_min, fint_max;
	u16 regm_max;
	unsigned long dcofreq_low_min, dcofreq_low_max;
	unsigned long dcofreq_high_min, dcofreq_high_max;
};

static const struct hdmi_pll_features *pll_feat;

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

void hdmi_pll_compute(struct hdmi_pll_data *pll, unsigned long clkin, int phy)
{
	struct hdmi_pll_info *pi = &pll->info;
	unsigned long refclk;
	u32 mf;

	/* use our funky units */
	clkin /= 10000;

	/*
	 * Input clock is predivided by N + 1
	 * out put of which is reference clk
	 */

	pi->regn = HDMI_DEFAULT_REGN;

	refclk = clkin / pi->regn;

	/* temorary hack to make sure DCO freq isn't calculated too low */
	if (pll_feat->bound_dcofreq && phy <= 65000)
		pi->regm2 = 3;
	else
		pi->regm2 = HDMI_DEFAULT_REGM2;

	/*
	 * multiplier is pixel_clk/ref_clk
	 * Multiplying by 100 to avoid fractional part removal
	 */
	pi->regm = phy * pi->regm2 / refclk;

	/*
	 * fractional multiplier is remainder of the difference between
	 * multiplier and actual phy(required pixel clock thus should be
	 * multiplied by 2^18(262144) divided by the reference clock
	 */
	mf = (phy - pi->regm / pi->regm2 * refclk) * 262144;
	pi->regmf = pi->regm2 * mf / refclk;

	/*
	 * Dcofreq should be set to 1 if required pixel clock
	 * is greater than 1000MHz
	 */
	pi->dcofreq = phy > 1000 * 100;
	pi->regsd = ((pi->regm * clkin / 10) / (pi->regn * 250) + 5) / 10;

	/* Set the reference clock to sysclk reference */
	pi->refsel = HDMI_REFSEL_SYSCLK;

	DSSDBG("M = %d Mf = %d\n", pi->regm, pi->regmf);
	DSSDBG("range = %d sd = %d\n", pi->dcofreq, pi->regsd);
}


static int hdmi_pll_config(struct hdmi_pll_data *pll)
{
	u32 r;
	struct hdmi_pll_info *fmt = &pll->info;

	/* PLL start always use manual mode */
	REG_FLD_MOD(pll->base, PLLCTRL_PLL_CONTROL, 0x0, 0, 0);

	r = hdmi_read_reg(pll->base, PLLCTRL_CFG1);
	r = FLD_MOD(r, fmt->regm, 20, 9);	/* CFG1_PLL_REGM */
	r = FLD_MOD(r, fmt->regn - 1, 8, 1);	/* CFG1_PLL_REGN */
	hdmi_write_reg(pll->base, PLLCTRL_CFG1, r);

	r = hdmi_read_reg(pll->base, PLLCTRL_CFG2);

	r = FLD_MOD(r, 0x0, 12, 12);	/* PLL_HIGHFREQ divide by 2 */
	r = FLD_MOD(r, 0x1, 13, 13);	/* PLL_REFEN */
	r = FLD_MOD(r, 0x0, 14, 14);	/* PHY_CLKINEN de-assert during locking */
	r = FLD_MOD(r, fmt->refsel, 22, 21);	/* REFSEL */

	if (fmt->dcofreq)
		r = FLD_MOD(r, 0x4, 3, 1);	/* 1000MHz and 2000MHz */
	else
		r = FLD_MOD(r, 0x2, 3, 1);	/* 500MHz and 1000MHz */

	hdmi_write_reg(pll->base, PLLCTRL_CFG2, r);

	REG_FLD_MOD(pll->base, PLLCTRL_CFG3, fmt->regsd, 17, 10);

	r = hdmi_read_reg(pll->base, PLLCTRL_CFG4);
	r = FLD_MOD(r, fmt->regm2, 24, 18);
	r = FLD_MOD(r, fmt->regmf, 17, 0);
	hdmi_write_reg(pll->base, PLLCTRL_CFG4, r);

	/* go now */
	REG_FLD_MOD(pll->base, PLLCTRL_PLL_GO, 0x1, 0, 0);

	/* wait for bit change */
	if (hdmi_wait_for_bit_change(pll->base, PLLCTRL_PLL_GO,
			0, 0, 0) != 0) {
		DSSERR("PLL GO bit not clearing\n");
		return -ETIMEDOUT;
	}

	/* Wait till the lock bit is set in PLL status */
	if (hdmi_wait_for_bit_change(pll->base,
			PLLCTRL_PLL_STATUS, 1, 1, 1) != 1) {
		DSSERR("cannot lock PLL\n");
		DSSERR("CFG1 0x%x\n",
			hdmi_read_reg(pll->base, PLLCTRL_CFG1));
		DSSERR("CFG2 0x%x\n",
			hdmi_read_reg(pll->base, PLLCTRL_CFG2));
		DSSERR("CFG4 0x%x\n",
			hdmi_read_reg(pll->base, PLLCTRL_CFG4));
		return -ETIMEDOUT;
	}

	DSSDBG("PLL locked!\n");

	return 0;
}

static int hdmi_pll_reset(struct hdmi_pll_data *pll)
{
	/* SYSRESET  controlled by power FSM */
	REG_FLD_MOD(pll->base, PLLCTRL_PLL_CONTROL, pll_feat->sys_reset, 3, 3);

	/* READ 0x0 reset is in progress */
	if (hdmi_wait_for_bit_change(pll->base, PLLCTRL_PLL_STATUS, 0, 0, 1)
			!= 1) {
		DSSERR("Failed to sysreset PLL\n");
		return -ETIMEDOUT;
	}

	return 0;
}

int hdmi_pll_enable(struct hdmi_pll_data *pll, struct hdmi_wp_data *wp)
{
	u16 r = 0;

	r = hdmi_wp_set_pll_pwr(wp, HDMI_PLLPWRCMD_ALLOFF);
	if (r)
		return r;

	r = hdmi_wp_set_pll_pwr(wp, HDMI_PLLPWRCMD_BOTHON_ALLCLKS);
	if (r)
		return r;

	r = hdmi_pll_reset(pll);
	if (r)
		return r;

	r = hdmi_pll_config(pll);
	if (r)
		return r;

	return 0;
}

void hdmi_pll_disable(struct hdmi_pll_data *pll, struct hdmi_wp_data *wp)
{
	hdmi_wp_set_pll_pwr(wp, HDMI_PLLPWRCMD_ALLOFF);
}

static const struct hdmi_pll_features omap44xx_pll_feats = {
	.sys_reset		=	false,
	.bound_dcofreq		=	false,
	.fint_min		=	500000,
	.fint_max		=	2500000,
	.regm_max		=	4095,
	.dcofreq_low_min	=	500000000,
	.dcofreq_low_max	=	1000000000,
	.dcofreq_high_min	=	1000000000,
	.dcofreq_high_max	=	2000000000,
};

static const struct hdmi_pll_features omap54xx_pll_feats = {
	.sys_reset		=	true,
	.bound_dcofreq		=	true,
	.fint_min		=	620000,
	.fint_max		=	2500000,
	.regm_max		=	2046,
	.dcofreq_low_min	=	750000000,
	.dcofreq_low_max	=	1500000000,
	.dcofreq_high_min	=	1250000000,
	.dcofreq_high_max	=	2500000000UL,
};

static int hdmi_pll_init_features(struct platform_device *pdev)
{
	struct hdmi_pll_features *dst;
	const struct hdmi_pll_features *src;

	dst = devm_kzalloc(&pdev->dev, sizeof(*dst), GFP_KERNEL);
	if (!dst) {
		dev_err(&pdev->dev, "Failed to allocate HDMI PHY Features\n");
		return -ENOMEM;
	}

	switch (omapdss_get_version()) {
	case OMAPDSS_VER_OMAP4430_ES1:
	case OMAPDSS_VER_OMAP4430_ES2:
	case OMAPDSS_VER_OMAP4:
		src = &omap44xx_pll_feats;
		break;

	case OMAPDSS_VER_OMAP5:
		src = &omap54xx_pll_feats;
		break;

	default:
		return -ENODEV;
	}

	memcpy(dst, src, sizeof(*dst));
	pll_feat = dst;

	return 0;
}

int hdmi_pll_init(struct platform_device *pdev, struct hdmi_pll_data *pll)
{
	int r;
	struct resource *res;

	r = hdmi_pll_init_features(pdev);
	if (r)
		return r;

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

	return 0;
}
