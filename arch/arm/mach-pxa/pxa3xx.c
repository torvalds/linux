/*
 * linux/arch/arm/mach-pxa/pxa3xx.c
 *
 * code specific to pxa3xx aka Monahans
 *
 * Copyright (C) 2006 Marvell International Ltd.
 *
 * 2007-09-02: eric miao <eric.miao@marvell.com>
 *             initial version
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pm.h>
#include <linux/platform_device.h>
#include <linux/irq.h>

#include <asm/hardware.h>
#include <asm/arch/pxa3xx-regs.h>
#include <asm/arch/ohci.h>
#include <asm/arch/pm.h>
#include <asm/arch/dma.h>
#include <asm/arch/ssp.h>

#include "generic.h"
#include "devices.h"
#include "clock.h"

/* Crystal clock: 13MHz */
#define BASE_CLK	13000000

/* Ring Oscillator Clock: 60MHz */
#define RO_CLK		60000000

#define ACCR_D0CS	(1 << 26)

/* crystal frequency to static memory controller multiplier (SMCFS) */
static unsigned char smcfs_mult[8] = { 6, 0, 8, 0, 0, 16, };

/* crystal frequency to HSIO bus frequency multiplier (HSS) */
static unsigned char hss_mult[4] = { 8, 12, 16, 0 };

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

	return CLK;
}

/*
 * Return the current static memory controller clock frequency
 * in units of 10kHz
 */
unsigned int pxa3xx_get_memclk_frequency_10khz(void)
{
	unsigned long acsr;
	unsigned int smcfs, clk = 0;

	acsr = ACSR;

	smcfs = (acsr >> 23) & 0x7;
	clk = (acsr & ACCR_D0CS) ? RO_CLK : smcfs_mult[smcfs] * BASE_CLK;

	return (clk / 10000);
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

static void clk_pxa3xx_cken_enable(struct clk *clk)
{
	unsigned long mask = 1ul << (clk->cken & 0x1f);

	local_irq_disable();

	if (clk->cken < 32)
		CKENA |= mask;
	else
		CKENB |= mask;

	local_irq_enable();
}

static void clk_pxa3xx_cken_disable(struct clk *clk)
{
	unsigned long mask = 1ul << (clk->cken & 0x1f);

	local_irq_disable();

	if (clk->cken < 32)
		CKENA &= ~mask;
	else
		CKENB &= ~mask;

	local_irq_enable();
}

static const struct clkops clk_pxa3xx_cken_ops = {
	.enable		= clk_pxa3xx_cken_enable,
	.disable	= clk_pxa3xx_cken_disable,
};

static const struct clkops clk_pxa3xx_hsio_ops = {
	.enable		= clk_pxa3xx_cken_enable,
	.disable	= clk_pxa3xx_cken_disable,
	.getrate	= clk_pxa3xx_hsio_getrate,
};

#define PXA3xx_CKEN(_name, _cken, _rate, _delay, _dev)	\
	{						\
		.name	= _name,			\
		.dev	= _dev,				\
		.ops	= &clk_pxa3xx_cken_ops,		\
		.rate	= _rate,			\
		.cken	= CKEN_##_cken,			\
		.delay	= _delay,			\
	}

#define PXA3xx_CK(_name, _cken, _ops, _dev)		\
	{						\
		.name	= _name,			\
		.dev	= _dev,				\
		.ops	= _ops,				\
		.cken	= CKEN_##_cken,			\
	}

static struct clk pxa3xx_clks[] = {
	PXA3xx_CK("LCDCLK", LCD,    &clk_pxa3xx_hsio_ops, &pxa_device_fb.dev),
	PXA3xx_CK("CAMCLK", CAMERA, &clk_pxa3xx_hsio_ops, NULL),

	PXA3xx_CKEN("UARTCLK", FFUART, 14857000, 1, &pxa_device_ffuart.dev),
	PXA3xx_CKEN("UARTCLK", BTUART, 14857000, 1, &pxa_device_btuart.dev),
	PXA3xx_CKEN("UARTCLK", STUART, 14857000, 1, NULL),

	PXA3xx_CKEN("I2CCLK", I2C,  32842000, 0, &pxa_device_i2c.dev),
	PXA3xx_CKEN("UDCCLK", UDC,  48000000, 5, &pxa_device_udc.dev),
};

void __init pxa3xx_init_irq(void)
{
	/* enable CP6 access */
	u32 value;
	__asm__ __volatile__("mrc p15, 0, %0, c15, c1, 0\n": "=r"(value));
	value |= (1 << 6);
	__asm__ __volatile__("mcr p15, 0, %0, c15, c1, 0\n": :"r"(value));

	pxa_init_irq_low();
	pxa_init_irq_high();
	pxa_init_irq_gpio(128);
}

/*
 * device registration specific to PXA3xx.
 */

static struct platform_device *devices[] __initdata = {
	&pxa_device_mci,
	&pxa_device_udc,
	&pxa_device_fb,
	&pxa_device_ffuart,
	&pxa_device_btuart,
	&pxa_device_stuart,
	&pxa_device_i2c,
	&pxa_device_i2s,
	&pxa_device_ficp,
	&pxa_device_rtc,
};

static int __init pxa3xx_init(void)
{
	int ret = 0;

	if (cpu_is_pxa3xx()) {
		clks_register(pxa3xx_clks, ARRAY_SIZE(pxa3xx_clks));

		if ((ret = pxa_init_dma(32)))
			return ret;

		return platform_add_devices(devices, ARRAY_SIZE(devices));
	}
	return 0;
}

subsys_initcall(pxa3xx_init);
