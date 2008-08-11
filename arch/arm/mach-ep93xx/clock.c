/*
 * arch/arm/mach-ep93xx/clock.c
 * Clock control for Cirrus EP93xx chips.
 *
 * Copyright (C) 2006 Lennert Buytenhek <buytenh@wantstofly.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/string.h>
#include <asm/div64.h>
#include <mach/hardware.h>
#include <asm/io.h>

struct clk {
	char		*name;
	unsigned long	rate;
	int		users;
	u32		enable_reg;
	u32		enable_mask;
};

static struct clk clk_uart = {
	.name		= "UARTCLK",
	.rate		= 14745600,
};
static struct clk clk_pll1 = {
	.name		= "pll1",
};
static struct clk clk_f = {
	.name		= "fclk",
};
static struct clk clk_h = {
	.name		= "hclk",
};
static struct clk clk_p = {
	.name		= "pclk",
};
static struct clk clk_pll2 = {
	.name		= "pll2",
};
static struct clk clk_usb_host = {
	.name		= "usb_host",
	.enable_reg	= EP93XX_SYSCON_CLOCK_CONTROL,
	.enable_mask	= EP93XX_SYSCON_CLOCK_USH_EN,
};


static struct clk *clocks[] = {
	&clk_uart,
	&clk_pll1,
	&clk_f,
	&clk_h,
	&clk_p,
	&clk_pll2,
	&clk_usb_host,
};

struct clk *clk_get(struct device *dev, const char *id)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(clocks); i++) {
		if (!strcmp(clocks[i]->name, id))
			return clocks[i];
	}

	return ERR_PTR(-ENOENT);
}
EXPORT_SYMBOL(clk_get);

int clk_enable(struct clk *clk)
{
	if (!clk->users++ && clk->enable_reg) {
		u32 value;

		value = __raw_readl(clk->enable_reg);
		__raw_writel(value | clk->enable_mask, clk->enable_reg);
	}

	return 0;
}
EXPORT_SYMBOL(clk_enable);

void clk_disable(struct clk *clk)
{
	if (!--clk->users && clk->enable_reg) {
		u32 value;

		value = __raw_readl(clk->enable_reg);
		__raw_writel(value & ~clk->enable_mask, clk->enable_reg);
	}
}
EXPORT_SYMBOL(clk_disable);

unsigned long clk_get_rate(struct clk *clk)
{
	return clk->rate;
}
EXPORT_SYMBOL(clk_get_rate);

void clk_put(struct clk *clk)
{
}
EXPORT_SYMBOL(clk_put);



static char fclk_divisors[] = { 1, 2, 4, 8, 16, 1, 1, 1 };
static char hclk_divisors[] = { 1, 2, 4, 5, 6, 8, 16, 32 };
static char pclk_divisors[] = { 1, 2, 4, 8 };

/*
 * PLL rate = 14.7456 MHz * (X1FBD + 1) * (X2FBD + 1) / (X2IPD + 1) / 2^PS
 */
static unsigned long calc_pll_rate(u32 config_word)
{
	unsigned long long rate;
	int i;

	rate = 14745600;
	rate *= ((config_word >> 11) & 0x1f) + 1;		/* X1FBD */
	rate *= ((config_word >> 5) & 0x3f) + 1;		/* X2FBD */
	do_div(rate, (config_word & 0x1f) + 1);			/* X2IPD */
	for (i = 0; i < ((config_word >> 16) & 3); i++)		/* PS */
		rate >>= 1;

	return (unsigned long)rate;
}

static int __init ep93xx_clock_init(void)
{
	u32 value;

	value = __raw_readl(EP93XX_SYSCON_CLOCK_SET1);
	if (!(value & 0x00800000)) {			/* PLL1 bypassed?  */
		clk_pll1.rate = 14745600;
	} else {
		clk_pll1.rate = calc_pll_rate(value);
	}
	clk_f.rate = clk_pll1.rate / fclk_divisors[(value >> 25) & 0x7];
	clk_h.rate = clk_pll1.rate / hclk_divisors[(value >> 20) & 0x7];
	clk_p.rate = clk_h.rate / pclk_divisors[(value >> 18) & 0x3];

	value = __raw_readl(EP93XX_SYSCON_CLOCK_SET2);
	if (!(value & 0x00080000)) {			/* PLL2 bypassed?  */
		clk_pll2.rate = 14745600;
	} else if (value & 0x00040000) {		/* PLL2 enabled?  */
		clk_pll2.rate = calc_pll_rate(value);
	} else {
		clk_pll2.rate = 0;
	}
	clk_usb_host.rate = clk_pll2.rate / (((value >> 28) & 0xf) + 1);

	printk(KERN_INFO "ep93xx: PLL1 running at %ld MHz, PLL2 at %ld MHz\n",
		clk_pll1.rate / 1000000, clk_pll2.rate / 1000000);
	printk(KERN_INFO "ep93xx: FCLK %ld MHz, HCLK %ld MHz, PCLK %ld MHz\n",
		clk_f.rate / 1000000, clk_h.rate / 1000000,
		clk_p.rate / 1000000);

	return 0;
}
arch_initcall(ep93xx_clock_init);
