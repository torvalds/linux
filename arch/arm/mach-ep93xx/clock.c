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


/*
 * The EP93xx has two external crystal oscillators.  To generate the
 * required high-frequency clocks, the processor uses two phase-locked-
 * loops (PLLs) to multiply the incoming external clock signal to much
 * higher frequencies that are then divided down by programmable dividers
 * to produce the needed clocks.  The PLLs operate independently of one
 * another.
 */
#define EP93XX_EXT_CLK_RATE	14745600
#define EP93XX_EXT_RTC_RATE	32768


struct clk {
	unsigned long	rate;
	int		users;
	int		sw_locked;
	u32		enable_reg;
	u32		enable_mask;

	unsigned long	(*get_rate)(struct clk *clk);
};


static unsigned long get_uart_rate(struct clk *clk);


static struct clk clk_uart1 = {
	.sw_locked	= 1,
	.enable_reg	= EP93XX_SYSCON_DEVICE_CONFIG,
	.enable_mask	= EP93XX_SYSCON_DEVICE_CONFIG_U1EN,
	.get_rate	= get_uart_rate,
};
static struct clk clk_uart2 = {
	.sw_locked	= 1,
	.enable_reg	= EP93XX_SYSCON_DEVICE_CONFIG,
	.enable_mask	= EP93XX_SYSCON_DEVICE_CONFIG_U2EN,
	.get_rate	= get_uart_rate,
};
static struct clk clk_uart3 = {
	.sw_locked	= 1,
	.enable_reg	= EP93XX_SYSCON_DEVICE_CONFIG,
	.enable_mask	= EP93XX_SYSCON_DEVICE_CONFIG_U3EN,
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
	INIT_CK("apb:uart1", NULL, &clk_uart1),
	INIT_CK("apb:uart2", NULL, &clk_uart2),
	INIT_CK("apb:uart3", NULL, &clk_uart3),
	INIT_CK(NULL, "pll1", &clk_pll1),
	INIT_CK(NULL, "fclk", &clk_f),
	INIT_CK(NULL, "hclk", &clk_h),
	INIT_CK(NULL, "pclk", &clk_p),
	INIT_CK(NULL, "pll2", &clk_pll2),
	INIT_CK("ep93xx-ohci", NULL, &clk_usb_host),
	INIT_CK(NULL, "m2p0", &clk_m2p0),
	INIT_CK(NULL, "m2p1", &clk_m2p1),
	INIT_CK(NULL, "m2p2", &clk_m2p2),
	INIT_CK(NULL, "m2p3", &clk_m2p3),
	INIT_CK(NULL, "m2p4", &clk_m2p4),
	INIT_CK(NULL, "m2p5", &clk_m2p5),
	INIT_CK(NULL, "m2p6", &clk_m2p6),
	INIT_CK(NULL, "m2p7", &clk_m2p7),
	INIT_CK(NULL, "m2p8", &clk_m2p8),
	INIT_CK(NULL, "m2p9", &clk_m2p9),
	INIT_CK(NULL, "m2m0", &clk_m2m0),
	INIT_CK(NULL, "m2m1", &clk_m2m1),
};


int clk_enable(struct clk *clk)
{
	if (!clk->users++ && clk->enable_reg) {
		u32 value;

		value = __raw_readl(clk->enable_reg);
		if (clk->sw_locked)
			__raw_writel(0xaa, EP93XX_SYSCON_SWLOCK);
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
		if (clk->sw_locked)
			__raw_writel(0xaa, EP93XX_SYSCON_SWLOCK);
		__raw_writel(value & ~clk->enable_mask, clk->enable_reg);
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
