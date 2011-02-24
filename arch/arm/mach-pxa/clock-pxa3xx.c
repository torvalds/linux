/*
 * linux/arch/arm/mach-pxa/clock-pxa3xx.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>

#include <mach/smemc.h>
#include <mach/pxa3xx-regs.h>

#include "clock.h"

/* Crystal clock: 13MHz */
#define BASE_CLK	13000000

/* Ring Oscillator Clock: 60MHz */
#define RO_CLK		60000000

#define ACCR_D0CS	(1 << 26)
#define ACCR_PCCE	(1 << 11)

/* crystal frequency to HSIO bus frequency multiplier (HSS) */
static unsigned char hss_mult[4] = { 8, 12, 16, 24 };

/*
 * Get the clock frequency as reflected by CCSR and the turbo flag.
 * We assume these values have been applied via a fcs.
 * If info is not 0 we also display the current settings.
 */
unsigned int pxa3xx_get_clk_frequency_khz(int info)
{
	unsigned long acsr, xclkcfg;
	unsigned int t, xl, xn, hss, ro, XL, XN, CLK, HSS;

	/* Read XCLKCFG register turbo bit */
	__asm__ __volatile__("mrc\tp14, 0, %0, c6, c0, 0" : "=r"(xclkcfg));
	t = xclkcfg & 0x1;

	acsr = ACSR;

	xl  = acsr & 0x1f;
	xn  = (acsr >> 8) & 0x7;
	hss = (acsr >> 14) & 0x3;

	XL = xl * BASE_CLK;
	XN = xn * XL;

	ro = acsr & ACCR_D0CS;

	CLK = (ro) ? RO_CLK : ((t) ? XN : XL);
	HSS = (ro) ? RO_CLK : hss_mult[hss] * BASE_CLK;

	if (info) {
		pr_info("RO Mode clock: %d.%02dMHz (%sactive)\n",
			RO_CLK / 1000000, (RO_CLK % 1000000) / 10000,
			(ro) ? "" : "in");
		pr_info("Run Mode clock: %d.%02dMHz (*%d)\n",
			XL / 1000000, (XL % 1000000) / 10000, xl);
		pr_info("Turbo Mode clock: %d.%02dMHz (*%d, %sactive)\n",
			XN / 1000000, (XN % 1000000) / 10000, xn,
			(t) ? "" : "in");
		pr_info("HSIO bus clock: %d.%02dMHz\n",
			HSS / 1000000, (HSS % 1000000) / 10000);
	}

	return CLK / 1000;
}

/*
 * Return the current AC97 clock frequency.
 */
static unsigned long clk_pxa3xx_ac97_getrate(struct clk *clk)
{
	unsigned long rate = 312000000;
	unsigned long ac97_div;

	ac97_div = AC97_DIV;

	/* This may loose precision for some rates but won't for the
	 * standard 24.576MHz.
	 */
	rate /= (ac97_div >> 12) & 0x7fff;
	rate *= (ac97_div & 0xfff);

	return rate;
}

/*
 * Return the current HSIO bus clock frequency
 */
static unsigned long clk_pxa3xx_hsio_getrate(struct clk *clk)
{
	unsigned long acsr;
	unsigned int hss, hsio_clk;

	acsr = ACSR;

	hss = (acsr >> 14) & 0x3;
	hsio_clk = (acsr & ACCR_D0CS) ? RO_CLK : hss_mult[hss] * BASE_CLK;

	return hsio_clk;
}

/* crystal frequency to static memory controller multiplier (SMCFS) */
static unsigned int smcfs_mult[8] = { 6, 0, 8, 0, 0, 16, };
static unsigned int df_clkdiv[4] = { 1, 2, 4, 1 };

static unsigned long clk_pxa3xx_smemc_getrate(struct clk *clk)
{
	unsigned long acsr = ACSR;
	unsigned long memclkcfg = __raw_readl(MEMCLKCFG);

	return BASE_CLK * smcfs_mult[(acsr >> 23) & 0x7] /
			df_clkdiv[(memclkcfg >> 16) & 0x3];
}

void clk_pxa3xx_cken_enable(struct clk *clk)
{
	unsigned long mask = 1ul << (clk->cken & 0x1f);

	if (clk->cken < 32)
		CKENA |= mask;
	else
		CKENB |= mask;
}

void clk_pxa3xx_cken_disable(struct clk *clk)
{
	unsigned long mask = 1ul << (clk->cken & 0x1f);

	if (clk->cken < 32)
		CKENA &= ~mask;
	else
		CKENB &= ~mask;
}

const struct clkops clk_pxa3xx_cken_ops = {
	.enable		= clk_pxa3xx_cken_enable,
	.disable	= clk_pxa3xx_cken_disable,
};

const struct clkops clk_pxa3xx_hsio_ops = {
	.enable		= clk_pxa3xx_cken_enable,
	.disable	= clk_pxa3xx_cken_disable,
	.getrate	= clk_pxa3xx_hsio_getrate,
};

const struct clkops clk_pxa3xx_ac97_ops = {
	.enable		= clk_pxa3xx_cken_enable,
	.disable	= clk_pxa3xx_cken_disable,
	.getrate	= clk_pxa3xx_ac97_getrate,
};

const struct clkops clk_pxa3xx_smemc_ops = {
	.enable		= clk_pxa3xx_cken_enable,
	.disable	= clk_pxa3xx_cken_disable,
	.getrate	= clk_pxa3xx_smemc_getrate,
};

static void clk_pout_enable(struct clk *clk)
{
	OSCC |= OSCC_PEN;
}

static void clk_pout_disable(struct clk *clk)
{
	OSCC &= ~OSCC_PEN;
}

const struct clkops clk_pxa3xx_pout_ops = {
	.enable		= clk_pout_enable,
	.disable	= clk_pout_disable,
};

#ifdef CONFIG_PM
static uint32_t cken[2];
static uint32_t accr;

static int pxa3xx_clock_suspend(struct sys_device *d, pm_message_t state)
{
	cken[0] = CKENA;
	cken[1] = CKENB;
	accr = ACCR;
	return 0;
}

static int pxa3xx_clock_resume(struct sys_device *d)
{
	ACCR = accr;
	CKENA = cken[0];
	CKENB = cken[1];
	return 0;
}
#else
#define pxa3xx_clock_suspend	NULL
#define pxa3xx_clock_resume	NULL
#endif

struct sysdev_class pxa3xx_clock_sysclass = {
	.name		= "pxa3xx-clock",
	.suspend	= pxa3xx_clock_suspend,
	.resume		= pxa3xx_clock_resume,
};

static int __init pxa3xx_clock_init(void)
{
	if (cpu_is_pxa3xx() || cpu_is_pxa95x())
		return sysdev_class_register(&pxa3xx_clock_sysclass);
	return 0;
}
postcore_initcall(pxa3xx_clock_init);
