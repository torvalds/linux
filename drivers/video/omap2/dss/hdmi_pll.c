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

	if (fmt->dcofreq) {
		/* divider programming for frequency beyond 1000Mhz */
		REG_FLD_MOD(pll->base, PLLCTRL_CFG3, fmt->regsd, 17, 10);
		r = FLD_MOD(r, 0x4, 3, 1);	/* 1000MHz and 2000MHz */
	} else {
		r = FLD_MOD(r, 0x2, 3, 1);	/* 500MHz and 1000MHz */
	}

	hdmi_write_reg(pll->base, PLLCTRL_CFG2, r);

	r = hdmi_read_reg(pll->base, PLLCTRL_CFG4);
	r = FLD_MOD(r, fmt->regm2, 24, 18);
	r = FLD_MOD(r, fmt->regmf, 17, 0);
	hdmi_write_reg(pll->base, PLLCTRL_CFG4, r);

	/* go now */
	REG_FLD_MOD(pll->base, PLLCTRL_PLL_GO, 0x1, 0, 0);

	/* wait for bit change */
	if (hdmi_wait_for_bit_change(pll->base, PLLCTRL_PLL_GO,
			0, 0, 1) != 1) {
		DSSERR("PLL GO bit not set\n");
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
	REG_FLD_MOD(pll->base, PLLCTRL_PLL_CONTROL, 0x0, 3, 3);

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

#define PLL_OFFSET	0x200
#define PLL_SIZE	0x100

int hdmi_pll_init(struct platform_device *pdev, struct hdmi_pll_data *pll)
{
	struct resource *res;
	struct resource temp_res;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "pll");
	if (!res) {
		DSSDBG("can't get PLL mem resource by name\n");
		/*
		 * if hwmod/DT doesn't have the memory resource information
		 * split into HDMI sub blocks by name, we try again by getting
		 * the platform's first resource. this code will be removed when
		 * the driver can get the mem resources by name
		 */
		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		if (!res) {
			DSSERR("can't get PLL mem resource\n");
			return -EINVAL;
		}

		temp_res.start = res->start + PLL_OFFSET;
		temp_res.end = temp_res.start + PLL_SIZE - 1;
		res = &temp_res;
	}

	pll->base = devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (!pll->base) {
		DSSERR("can't ioremap PLLCTRL\n");
		return -ENOMEM;
	}

	return 0;
}
