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
#include <linux/io.h>

#include <asm/clkdev.h>
#include <asm/div64.h>
#include <mach/hardware.h>


struct clk {
	unsigned long	rate;
	int		users;
	int		sw_locked;
	void __iomem	*enable_reg;
	u32		enable_mask;

	unsigned long	(*get_rate)(struct clk *clk);
	int		(*set_rate)(struct clk *clk, unsigned long rate);
};


static unsigned long get_uart_rate(struct clk *clk);

static int set_keytchclk_rate(struct clk *clk, unsigned long rate);
static int set_div_rate(struct clk *clk, unsigned long rate);

static struct clk clk_uart1 = {
	.sw_locked	= 1,
	.enable_reg	= EP93XX_SYSCON_DEVCFG,
	.enable_mask	= EP93XX_SYSCON_DEVCFG_U1EN,
	.get_rate	= get_uart_rate,
};
static struct clk clk_uart2 = {
	.sw_locked	= 1,
	.enable_reg	= EP93XX_SYSCON_DEVCFG,
	.enable_mask	= EP93XX_SYSCON_DEVCFG_U2EN,
	.get_rate	= get_uart_rate,
};
static struct clk clk_uart3 = {
	.sw_locked	= 1,
	.enable_reg	= EP93XX_SYSCON_DEVCFG,
	.enable_mask	= EP93XX_SYSCON_DEVCFG_U3EN,
	.get_rate	= get_uart_rate,
};
static struct clk clk_pll1;
static struct clk clk_f;
static struct clk clk_h;
static struct clk clk_p;
static struct clk clk_pll2;
static struct clk clk_usb_host = {
	.enable_reg	= EP93XX_SYSCON_PWRCNT,
	.enable_mask	= EP93XX_SYSCON_PWRCNT_USH_EN,
};
static struct clk clk_keypad = {
	.sw_locked	= 1,
	.enable_reg	= EP93XX_SYSCON_KEYTCHCLKDIV,
	.enable_mask	= EP93XX_SYSCON_KEYTCHCLKDIV_KEN,
	.set_rate	= set_keytchclk_rate,
};
static struct clk clk_pwm = {
	.rate		= EP93XX_EXT_CLK_RATE,
};

static struct clk clk_video = {
	.sw_locked	= 1,
	.enable_reg     = EP93XX_SYSCON_VIDCLKDIV,
	.enable_mask    = EP93XX_SYSCON_CLKDIV_ENABLE,
	.set_rate	= set_div_rate,
};

/* DMA Clocks */
static struct clk clk_m2p0 = {
	.enable_reg	= EP93XX_SYSCON_PWRCNT,
	.enable_mask	= EP93XX_SYSCON_PWRCNT_DMA_M2P0,
};
static struct clk clk_m2p1 = {
	.enable_reg	= EP93XX_SYSCON_PWRCNT,
	.enable_mask	= EP93XX_SYSCON_PWRCNT_DMA_M2P1,
};
static struct clk clk_m2p2 = {
	.enable_reg	= EP93XX_SYSCON_PWRCNT,
	.enable_mask	= EP93XX_SYSCON_PWRCNT_DMA_M2P2,
};
static struct clk clk_m2p3 = {
	.enable_reg	= EP93XX_SYSCON_PWRCNT,
	.enable_mask	= EP93XX_SYSCON_PWRCNT_DMA_M2P3,
};
static struct clk clk_m2p4 = {
	.enable_reg	= EP93XX_SYSCON_PWRCNT,
	.enable_mask	= EP93XX_SYSCON_PWRCNT_DMA_M2P4,
};
static struct clk clk_m2p5 = {
	.enable_reg	= EP93XX_SYSCON_PWRCNT,
	.enable_mask	= EP93XX_SYSCON_PWRCNT_DMA_M2P5,
};
static struct clk clk_m2p6 = {
	.enable_reg	= EP93XX_SYSCON_PWRCNT,
	.enable_mask	= EP93XX_SYSCON_PWRCNT_DMA_M2P6,
};
static struct clk clk_m2p7 = {
	.enable_reg	= EP93XX_SYSCON_PWRCNT,
	.enable_mask	= EP93XX_SYSCON_PWRCNT_DMA_M2P7,
};
static struct clk clk_m2p8 = {
	.enable_reg	= EP93XX_SYSCON_PWRCNT,
	.enable_mask	= EP93XX_SYSCON_PWRCNT_DMA_M2P8,
};
static struct clk clk_m2p9 = {
	.enable_reg	= EP93XX_SYSCON_PWRCNT,
	.enable_mask	= EP93XX_SYSCON_PWRCNT_DMA_M2P9,
};
static struct clk clk_m2m0 = {
	.enable_reg	= EP93XX_SYSCON_PWRCNT,
	.enable_mask	= EP93XX_SYSCON_PWRCNT_DMA_M2M0,
};
static struct clk clk_m2m1 = {
	.enable_reg	= EP93XX_SYSCON_PWRCNT,
	.enable_mask	= EP93XX_SYSCON_PWRCNT_DMA_M2M1,
};

#define INIT_CK(dev,con,ck)					\
	{ .dev_id = dev, .con_id = con, .clk = ck }

static struct clk_lookup clocks[] = {
	INIT_CK("apb:uart1",		NULL,		&clk_uart1),
	INIT_CK("apb:uart2",		NULL,		&clk_uart2),
	INIT_CK("apb:uart3",		NULL,		&clk_uart3),
	INIT_CK(NULL,			"pll1",		&clk_pll1),
	INIT_CK(NULL,			"fclk",		&clk_f),
	INIT_CK(NULL,			"hclk",		&clk_h),
	INIT_CK(NULL,			"pclk",		&clk_p),
	INIT_CK(NULL,			"pll2",		&clk_pll2),
	INIT_CK("ep93xx-ohci",		NULL,		&clk_usb_host),
	INIT_CK("ep93xx-keypad",	NULL,		&clk_keypad),
	INIT_CK("ep93xx-fb",		NULL,		&clk_video),
	INIT_CK(NULL,			"pwm_clk",	&clk_pwm),
	INIT_CK(NULL,			"m2p0",		&clk_m2p0),
	INIT_CK(NULL,			"m2p1",		&clk_m2p1),
	INIT_CK(NULL,			"m2p2",		&clk_m2p2),
	INIT_CK(NULL,			"m2p3",		&clk_m2p3),
	INIT_CK(NULL,			"m2p4",		&clk_m2p4),
	INIT_CK(NULL,			"m2p5",		&clk_m2p5),
	INIT_CK(NULL,			"m2p6",		&clk_m2p6),
	INIT_CK(NULL,			"m2p7",		&clk_m2p7),
	INIT_CK(NULL,			"m2p8",		&clk_m2p8),
	INIT_CK(NULL,			"m2p9",		&clk_m2p9),
	INIT_CK(NULL,			"m2m0",		&clk_m2m0),
	INIT_CK(NULL,			"m2m1",		&clk_m2m1),
};


int clk_enable(struct clk *clk)
{
	if (!clk->users++ && clk->enable_reg) {
		u32 value;

		value = __raw_readl(clk->enable_reg);
		value |= clk->enable_mask;
		if (clk->sw_locked)
			ep93xx_syscon_swlocked_write(value, clk->enable_reg);
		else
			__raw_writel(value, clk->enable_reg);
	}

	return 0;
}
EXPORT_SYMBOL(clk_enable);

void clk_disable(struct clk *clk)
{
	if (!--clk->users && clk->enable_reg) {
		u32 value;

		value = __raw_readl(clk->enable_reg);
		value &= ~clk->enable_mask;
		if (clk->sw_locked)
			ep93xx_syscon_swlocked_write(value, clk->enable_reg);
		else
			__raw_writel(value, clk->enable_reg);
	}
}
EXPORT_SYMBOL(clk_disable);

static unsigned long get_uart_rate(struct clk *clk)
{
	u32 value;

	value = __raw_readl(EP93XX_SYSCON_PWRCNT);
	if (value & EP93XX_SYSCON_PWRCNT_UARTBAUD)
		return EP93XX_EXT_CLK_RATE;
	else
		return EP93XX_EXT_CLK_RATE / 2;
}

unsigned long clk_get_rate(struct clk *clk)
{
	if (clk->get_rate)
		return clk->get_rate(clk);

	return clk->rate;
}
EXPORT_SYMBOL(clk_get_rate);

static int set_keytchclk_rate(struct clk *clk, unsigned long rate)
{
	u32 val;
	u32 div_bit;

	val = __raw_readl(clk->enable_reg);

	/*
	 * The Key Matrix and ADC clocks are configured using the same
	 * System Controller register.  The clock used will be either
	 * 1/4 or 1/16 the external clock rate depending on the
	 * EP93XX_SYSCON_KEYTCHCLKDIV_KDIV/EP93XX_SYSCON_KEYTCHCLKDIV_ADIV
	 * bit being set or cleared.
	 */
	div_bit = clk->enable_mask >> 15;

	if (rate == EP93XX_KEYTCHCLK_DIV4)
		val |= div_bit;
	else if (rate == EP93XX_KEYTCHCLK_DIV16)
		val &= ~div_bit;
	else
		return -EINVAL;

	ep93xx_syscon_swlocked_write(val, clk->enable_reg);
	clk->rate = rate;
	return 0;
}

static unsigned long calc_clk_div(unsigned long rate, int *psel, int *esel,
				  int *pdiv, int *div)
{
	unsigned long max_rate, best_rate = 0,
		actual_rate = 0, mclk_rate = 0, rate_err = -1;
	int i, found = 0, __div = 0, __pdiv = 0;

	/* Don't exceed the maximum rate */
	max_rate = max(max(clk_pll1.rate / 4, clk_pll2.rate / 4),
		       (unsigned long)EP93XX_EXT_CLK_RATE / 4);
	rate = min(rate, max_rate);

	/*
	 * Try the two pll's and the external clock
	 * Because the valid predividers are 2, 2.5 and 3, we multiply
	 * all the clocks by 2 to avoid floating point math.
	 *
	 * This is based on the algorithm in the ep93xx raster guide:
	 * http://be-a-maverick.com/en/pubs/appNote/AN269REV1.pdf
	 *
	 */
	for (i = 0; i < 3; i++) {
		if (i == 0)
			mclk_rate = EP93XX_EXT_CLK_RATE * 2;
		else if (i == 1)
			mclk_rate = clk_pll1.rate * 2;
		else if (i == 2)
			mclk_rate = clk_pll2.rate * 2;

		/* Try each predivider value */
		for (__pdiv = 4; __pdiv <= 6; __pdiv++) {
			__div = mclk_rate / (rate * __pdiv);
			if (__div < 2 || __div > 127)
				continue;

			actual_rate = mclk_rate / (__pdiv * __div);

			if (!found || abs(actual_rate - rate) < rate_err) {
				*pdiv = __pdiv - 3;
				*div = __div;
				*psel = (i == 2);
				*esel = (i != 0);
				best_rate = actual_rate;
				rate_err = abs(actual_rate - rate);
				found = 1;
			}
		}
	}

	if (!found)
		return 0;

	return best_rate;
}

static int set_div_rate(struct clk *clk, unsigned long rate)
{
	unsigned long actual_rate;
	int psel = 0, esel = 0, pdiv = 0, div = 0;
	u32 val;

	actual_rate = calc_clk_div(rate, &psel, &esel, &pdiv, &div);
	if (actual_rate == 0)
		return -EINVAL;
	clk->rate = actual_rate;

	/* Clear the esel, psel, pdiv and div bits */
	val = __raw_readl(clk->enable_reg);
	val &= ~0x7fff;

	/* Set the new esel, psel, pdiv and div bits for the new clock rate */
	val |= (esel ? EP93XX_SYSCON_CLKDIV_ESEL : 0) |
		(psel ? EP93XX_SYSCON_CLKDIV_PSEL : 0) |
		(pdiv << EP93XX_SYSCON_CLKDIV_PDIV_SHIFT) | div;
	ep93xx_syscon_swlocked_write(val, clk->enable_reg);
	return 0;
}

int clk_set_rate(struct clk *clk, unsigned long rate)
{
	if (clk->set_rate)
		return clk->set_rate(clk, rate);

	return -EINVAL;
}
EXPORT_SYMBOL(clk_set_rate);


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

	rate = EP93XX_EXT_CLK_RATE;
	rate *= ((config_word >> 11) & 0x1f) + 1;		/* X1FBD */
	rate *= ((config_word >> 5) & 0x3f) + 1;		/* X2FBD */
	do_div(rate, (config_word & 0x1f) + 1);			/* X2IPD */
	for (i = 0; i < ((config_word >> 16) & 3); i++)		/* PS */
		rate >>= 1;

	return (unsigned long)rate;
}

static void __init ep93xx_dma_clock_init(void)
{
	clk_m2p0.rate = clk_h.rate;
	clk_m2p1.rate = clk_h.rate;
	clk_m2p2.rate = clk_h.rate;
	clk_m2p3.rate = clk_h.rate;
	clk_m2p4.rate = clk_h.rate;
	clk_m2p5.rate = clk_h.rate;
	clk_m2p6.rate = clk_h.rate;
	clk_m2p7.rate = clk_h.rate;
	clk_m2p8.rate = clk_h.rate;
	clk_m2p9.rate = clk_h.rate;
	clk_m2m0.rate = clk_h.rate;
	clk_m2m1.rate = clk_h.rate;
}

static int __init ep93xx_clock_init(void)
{
	u32 value;
	int i;

	value = __raw_readl(EP93XX_SYSCON_CLOCK_SET1);
	if (!(value & 0x00800000)) {			/* PLL1 bypassed?  */
		clk_pll1.rate = EP93XX_EXT_CLK_RATE;
	} else {
		clk_pll1.rate = calc_pll_rate(value);
	}
	clk_f.rate = clk_pll1.rate / fclk_divisors[(value >> 25) & 0x7];
	clk_h.rate = clk_pll1.rate / hclk_divisors[(value >> 20) & 0x7];
	clk_p.rate = clk_h.rate / pclk_divisors[(value >> 18) & 0x3];
	ep93xx_dma_clock_init();

	value = __raw_readl(EP93XX_SYSCON_CLOCK_SET2);
	if (!(value & 0x00080000)) {			/* PLL2 bypassed?  */
		clk_pll2.rate = EP93XX_EXT_CLK_RATE;
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

	for (i = 0; i < ARRAY_SIZE(clocks); i++)
		clkdev_add(&clocks[i]);
	return 0;
}
arch_initcall(ep93xx_clock_init);
