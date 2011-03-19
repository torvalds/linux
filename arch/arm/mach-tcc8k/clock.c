/*
 * Lowlevel clock handling for Telechips TCC8xxx SoCs
 *
 * Copyright (C) 2010 by Hans J. Koch <hjk@linutronix.de>
 *
 * Licensed under the terms of the GPL v2
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/clkdev.h>

#include <mach/clock.h>
#include <mach/irqs.h>
#include <mach/tcc8k-regs.h>

#include "common.h"

#define BCLKCTR0	(CKC_BASE + BCLKCTR0_OFFS)
#define BCLKCTR1	(CKC_BASE + BCLKCTR1_OFFS)

#define ACLKREF		(CKC_BASE + ACLKREF_OFFS)
#define ACLKUART0	(CKC_BASE + ACLKUART0_OFFS)
#define ACLKUART1	(CKC_BASE + ACLKUART1_OFFS)
#define ACLKUART2	(CKC_BASE + ACLKUART2_OFFS)
#define ACLKUART3	(CKC_BASE + ACLKUART3_OFFS)
#define ACLKUART4	(CKC_BASE + ACLKUART4_OFFS)
#define ACLKI2C		(CKC_BASE + ACLKI2C_OFFS)
#define ACLKADC		(CKC_BASE + ACLKADC_OFFS)
#define ACLKUSBH	(CKC_BASE + ACLKUSBH_OFFS)
#define ACLKLCD		(CKC_BASE + ACLKLCD_OFFS)
#define ACLKSDH0	(CKC_BASE + ACLKSDH0_OFFS)
#define ACLKSDH1	(CKC_BASE + ACLKSDH1_OFFS)
#define ACLKSPI0	(CKC_BASE + ACLKSPI0_OFFS)
#define ACLKSPI1	(CKC_BASE + ACLKSPI1_OFFS)
#define ACLKSPDIF	(CKC_BASE + ACLKSPDIF_OFFS)
#define ACLKC3DEC	(CKC_BASE + ACLKC3DEC_OFFS)
#define ACLKCAN0	(CKC_BASE + ACLKCAN0_OFFS)
#define ACLKCAN1	(CKC_BASE + ACLKCAN1_OFFS)
#define ACLKGSB0	(CKC_BASE + ACLKGSB0_OFFS)
#define ACLKGSB1	(CKC_BASE + ACLKGSB1_OFFS)
#define ACLKGSB2	(CKC_BASE + ACLKGSB2_OFFS)
#define ACLKGSB3	(CKC_BASE + ACLKGSB3_OFFS)
#define ACLKUSBH	(CKC_BASE + ACLKUSBH_OFFS)
#define ACLKTCT		(CKC_BASE + ACLKTCT_OFFS)
#define ACLKTCX		(CKC_BASE + ACLKTCX_OFFS)
#define ACLKTCZ		(CKC_BASE + ACLKTCZ_OFFS)

/* Crystal frequencies */
static unsigned long xi_rate, xti_rate;

static void __iomem *pll_cfg_addr(int pll)
{
	switch (pll) {
	case 0: return (CKC_BASE + PLL0CFG_OFFS);
	case 1: return (CKC_BASE + PLL1CFG_OFFS);
	case 2: return (CKC_BASE + PLL2CFG_OFFS);
	default:
		BUG();
	}
}

static int pll_enable(int pll, int enable)
{
	u32 reg;
	void __iomem *addr = pll_cfg_addr(pll);

	reg = __raw_readl(addr);
	if (enable)
		reg &= ~PLLxCFG_PD;
	else
		reg |= PLLxCFG_PD;

	__raw_writel(reg, addr);
	return 0;
}

static int xi_enable(int enable)
{
	u32 reg;

	reg = __raw_readl(CKC_BASE + CLKCTRL_OFFS);
	if (enable)
		reg |= CLKCTRL_XE;
	else
		reg &= ~CLKCTRL_XE;

	__raw_writel(reg, CKC_BASE + CLKCTRL_OFFS);
	return 0;
}

static int root_clk_enable(enum root_clks src)
{
	switch (src) {
	case CLK_SRC_PLL0: return pll_enable(0, 1);
	case CLK_SRC_PLL1: return pll_enable(1, 1);
	case CLK_SRC_PLL2: return pll_enable(2, 1);
	case CLK_SRC_XI: return xi_enable(1);
	default:
		BUG();
	}
	return 0;
}

static int root_clk_disable(enum root_clks root_src)
{
	switch (root_src) {
	case CLK_SRC_PLL0: return pll_enable(0, 0);
	case CLK_SRC_PLL1: return pll_enable(1, 0);
	case CLK_SRC_PLL2: return pll_enable(2, 0);
	case CLK_SRC_XI: return xi_enable(0);
	default:
		BUG();
	}
	return 0;
}

static int enable_clk(struct clk *clk)
{
	u32 reg;

	if (clk->root_id != CLK_SRC_NOROOT)
		return root_clk_enable(clk->root_id);

	if (clk->aclkreg) {
		reg = __raw_readl(clk->aclkreg);
		reg |= ACLK_EN;
		__raw_writel(reg, clk->aclkreg);
	}
	if (clk->bclkctr) {
		reg = __raw_readl(clk->bclkctr);
		reg |= 1 << clk->bclk_shift;
		__raw_writel(reg, clk->bclkctr);
	}
	return 0;
}

static void disable_clk(struct clk *clk)
{
	u32 reg;

	if (clk->root_id != CLK_SRC_NOROOT) {
		root_clk_disable(clk->root_id);
		return;
	}

	if (clk->bclkctr) {
		reg = __raw_readl(clk->bclkctr);
		reg &= ~(1 << clk->bclk_shift);
		__raw_writel(reg, clk->bclkctr);
	}
	if (clk->aclkreg) {
		reg = __raw_readl(clk->aclkreg);
		reg &= ~ACLK_EN;
		__raw_writel(reg, clk->aclkreg);
	}
}

static unsigned long get_rate_pll(int pll)
{
	u32 reg;
	unsigned long s, m, p;
	void __iomem *addr = pll_cfg_addr(pll);

	reg = __raw_readl(addr);
	s = (reg >> 16) & 0x07;
	m = (reg >> 8) & 0xff;
	p = reg & 0x3f;

	return (m * xi_rate) / (p * (1 << s));
}

static unsigned long get_rate_pll_div(int pll)
{
	u32 reg;
	unsigned long div = 0;
	void __iomem *addr;

	switch (pll) {
	case 0:
		addr = CKC_BASE + CLKDIVC0_OFFS;
		reg = __raw_readl(addr);
		if (reg & CLKDIVC0_P0E)
			div = (reg >> 24) & 0x3f;
		break;
	case 1:
		addr = CKC_BASE + CLKDIVC0_OFFS;
		reg = __raw_readl(addr);
		if (reg & CLKDIVC0_P1E)
			div = (reg >> 16) & 0x3f;
		break;
	case 2:
		addr = CKC_BASE + CLKDIVC1_OFFS;
		reg = __raw_readl(addr);
		if (reg & CLKDIVC1_P2E)
			div = __raw_readl(addr) & 0x3f;
		break;
	}
	return get_rate_pll(pll) / (div + 1);
}

static unsigned long get_rate_xi_div(void)
{
	unsigned long div = 0;
	u32 reg = __raw_readl(CKC_BASE + CLKDIVC0_OFFS);

	if (reg & CLKDIVC0_XE)
		div = (reg >> 8) & 0x3f;

	return xi_rate / (div + 1);
}

static unsigned long get_rate_xti_div(void)
{
	unsigned long div = 0;
	u32 reg = __raw_readl(CKC_BASE + CLKDIVC0_OFFS);

	if (reg & CLKDIVC0_XTE)
		div = reg & 0x3f;

	return xti_rate / (div + 1);
}

static unsigned long root_clk_get_rate(enum root_clks src)
{
	switch (src) {
	case CLK_SRC_PLL0: return get_rate_pll(0);
	case CLK_SRC_PLL1: return get_rate_pll(1);
	case CLK_SRC_PLL2: return get_rate_pll(2);
	case CLK_SRC_PLL0DIV: return get_rate_pll_div(0);
	case CLK_SRC_PLL1DIV: return get_rate_pll_div(1);
	case CLK_SRC_PLL2DIV: return get_rate_pll_div(2);
	case CLK_SRC_XI: return xi_rate;
	case CLK_SRC_XTI: return xti_rate;
	case CLK_SRC_XIDIV: return get_rate_xi_div();
	case CLK_SRC_XTIDIV: return get_rate_xti_div();
	default: return 0;
	}
}

static unsigned long aclk_get_rate(struct clk *clk)
{
	u32 reg;
	unsigned long div;
	unsigned int src;

	reg = __raw_readl(clk->aclkreg);
	div = reg & 0x0fff;
	src = (reg >> ACLK_SEL_SHIFT) & CLK_SRC_MASK;
	return root_clk_get_rate(src) / (div + 1);
}

static unsigned long aclk_best_div(struct clk *clk, unsigned long rate)
{
	unsigned long div, src, freq, r1, r2;

	src = __raw_readl(clk->aclkreg) >> ACLK_SEL_SHIFT;
	src &= CLK_SRC_MASK;
	freq = root_clk_get_rate(src);
	div = freq / rate + 1;
	r1 = freq / div;
	r2 = freq / (div + 1);
	if (r2 >= rate)
		return div + 1;
	if ((rate - r2) < (r1 - rate))
		return div + 1;

	return div;
}

static unsigned long aclk_round_rate(struct clk *clk, unsigned long rate)
{
	unsigned int src;

	src = __raw_readl(clk->aclkreg) >> ACLK_SEL_SHIFT;
	src &= CLK_SRC_MASK;

	return root_clk_get_rate(src) / aclk_best_div(clk, rate);
}

static int aclk_set_rate(struct clk *clk, unsigned long rate)
{
	u32 reg;

	reg = __raw_readl(clk->aclkreg) & ~ACLK_DIV_MASK;
	reg |= aclk_best_div(clk, rate);
	return 0;
}

static unsigned long get_rate_sys(struct clk *clk)
{
	unsigned int src;

	src = __raw_readl(CKC_BASE + CLKCTRL_OFFS) & CLK_SRC_MASK;
		return root_clk_get_rate(src);
}

static unsigned long get_rate_bus(struct clk *clk)
{
	unsigned int div;

	div = (__raw_readl(CKC_BASE + CLKCTRL_OFFS) >> 4) & 0xff;
	return get_rate_sys(clk) / (div + 1);
}

static unsigned long get_rate_cpu(struct clk *clk)
{
	unsigned int reg, div, fsys, fbus;

	fbus = get_rate_bus(clk);
	reg = __raw_readl(CKC_BASE + CLKCTRL_OFFS);
	if (reg & (1 << 29))
		return fbus;
	fsys = get_rate_sys(clk);
	div = (reg >> 16) & 0x0f;
	return fbus + ((fsys - fbus) * (div + 1)) / 16;
}

static unsigned long get_rate_root(struct clk *clk)
{
	return root_clk_get_rate(clk->root_id);
}

static int aclk_set_parent(struct clk *clock, struct clk *parent)
{
	u32 reg;

	if (clock->parent == parent)
		return 0;

	clock->parent = parent;

	if (!parent)
		return 0;

	if (parent->root_id == CLK_SRC_NOROOT)
		return 0;
	reg = __raw_readl(clock->aclkreg);
	reg &= ~ACLK_SEL_MASK;
	reg |= (parent->root_id << ACLK_SEL_SHIFT) & ACLK_SEL_MASK;
	__raw_writel(reg, clock->aclkreg);

	return 0;
}

#define DEFINE_ROOT_CLOCK(name, ri, p)	\
	static struct clk name = {		\
		.root_id = ri,			\
		.get_rate = get_rate_root,			\
		.enable = enable_clk,		\
		.disable = disable_clk,		\
		.parent = p,			\
	};

#define DEFINE_SPECIAL_CLOCK(name, gr, p)	\
	static struct clk name = {		\
		.root_id = CLK_SRC_NOROOT,	\
		.get_rate = gr,			\
		.parent = p,			\
	};

#define DEFINE_ACLOCK(name, bc, bs, ar)		\
	static struct clk name = {		\
		.root_id = CLK_SRC_NOROOT,	\
		.bclkctr = bc,			\
		.bclk_shift = bs,		\
		.aclkreg = ar,			\
		.get_rate = aclk_get_rate,	\
		.set_rate = aclk_set_rate,	\
		.round_rate = aclk_round_rate,	\
		.enable = enable_clk,		\
		.disable = disable_clk,		\
		.set_parent = aclk_set_parent,	\
	};

#define DEFINE_BCLOCK(name, bc, bs, gr, p)	\
	static struct clk name = {		\
		.root_id = CLK_SRC_NOROOT,	\
		.bclkctr = bc,			\
		.bclk_shift = bs,		\
		.get_rate = gr,			\
		.enable = enable_clk,		\
		.disable = disable_clk,		\
		.parent = p,			\
	};

DEFINE_ROOT_CLOCK(xi, CLK_SRC_XI, NULL)
DEFINE_ROOT_CLOCK(xti, CLK_SRC_XTI, NULL)
DEFINE_ROOT_CLOCK(xidiv, CLK_SRC_XIDIV, &xi)
DEFINE_ROOT_CLOCK(xtidiv, CLK_SRC_XTIDIV, &xti)
DEFINE_ROOT_CLOCK(pll0, CLK_SRC_PLL0, &xi)
DEFINE_ROOT_CLOCK(pll1, CLK_SRC_PLL1, &xi)
DEFINE_ROOT_CLOCK(pll2, CLK_SRC_PLL2, &xi)
DEFINE_ROOT_CLOCK(pll0div, CLK_SRC_PLL0DIV, &pll0)
DEFINE_ROOT_CLOCK(pll1div, CLK_SRC_PLL1DIV, &pll1)
DEFINE_ROOT_CLOCK(pll2div, CLK_SRC_PLL2DIV, &pll2)

/* The following 3 clocks are special and are initialized explicitly later */
DEFINE_SPECIAL_CLOCK(sys, get_rate_sys, NULL)
DEFINE_SPECIAL_CLOCK(bus, get_rate_bus, &sys)
DEFINE_SPECIAL_CLOCK(cpu, get_rate_cpu, &sys)

DEFINE_ACLOCK(tct, NULL, 0, ACLKTCT)
DEFINE_ACLOCK(tcx, NULL, 0, ACLKTCX)
DEFINE_ACLOCK(tcz, NULL, 0, ACLKTCZ)
DEFINE_ACLOCK(ref, NULL, 0, ACLKREF)
DEFINE_ACLOCK(uart0, BCLKCTR0, 5, ACLKUART0)
DEFINE_ACLOCK(uart1, BCLKCTR0, 23, ACLKUART1)
DEFINE_ACLOCK(uart2, BCLKCTR0, 6, ACLKUART2)
DEFINE_ACLOCK(uart3, BCLKCTR0, 8, ACLKUART3)
DEFINE_ACLOCK(uart4, BCLKCTR1, 6, ACLKUART4)
DEFINE_ACLOCK(i2c, BCLKCTR0, 7, ACLKI2C)
DEFINE_ACLOCK(adc, BCLKCTR0, 10, ACLKADC)
DEFINE_ACLOCK(usbh0, BCLKCTR0, 11, ACLKUSBH)
DEFINE_ACLOCK(lcd, BCLKCTR0, 13, ACLKLCD)
DEFINE_ACLOCK(sd0, BCLKCTR0, 17, ACLKSDH0)
DEFINE_ACLOCK(sd1, BCLKCTR1, 5, ACLKSDH1)
DEFINE_ACLOCK(spi0, BCLKCTR0, 24, ACLKSPI0)
DEFINE_ACLOCK(spi1, BCLKCTR0, 30, ACLKSPI1)
DEFINE_ACLOCK(spdif, BCLKCTR1, 2, ACLKSPDIF)
DEFINE_ACLOCK(c3dec, BCLKCTR1, 9, ACLKC3DEC)
DEFINE_ACLOCK(can0, BCLKCTR1, 10, ACLKCAN0)
DEFINE_ACLOCK(can1, BCLKCTR1, 11, ACLKCAN1)
DEFINE_ACLOCK(gsb0, BCLKCTR1, 13, ACLKGSB0)
DEFINE_ACLOCK(gsb1, BCLKCTR1, 14, ACLKGSB1)
DEFINE_ACLOCK(gsb2, BCLKCTR1, 15, ACLKGSB2)
DEFINE_ACLOCK(gsb3, BCLKCTR1, 16, ACLKGSB3)
DEFINE_ACLOCK(usbh1, BCLKCTR1, 20, ACLKUSBH)

DEFINE_BCLOCK(dai0, BCLKCTR0, 0, NULL, NULL)
DEFINE_BCLOCK(pic, BCLKCTR0, 1, NULL, NULL)
DEFINE_BCLOCK(tc, BCLKCTR0, 2, NULL, NULL)
DEFINE_BCLOCK(gpio, BCLKCTR0, 3, NULL, NULL)
DEFINE_BCLOCK(usbd, BCLKCTR0, 4, NULL, NULL)
DEFINE_BCLOCK(ecc, BCLKCTR0, 9, NULL, NULL)
DEFINE_BCLOCK(gdma0, BCLKCTR0, 12, NULL, NULL)
DEFINE_BCLOCK(rtc, BCLKCTR0, 15, NULL, NULL)
DEFINE_BCLOCK(nfc, BCLKCTR0, 16, NULL, NULL)
DEFINE_BCLOCK(g2d, BCLKCTR0, 18, NULL, NULL)
DEFINE_BCLOCK(gdma1, BCLKCTR0, 22, NULL, NULL)
DEFINE_BCLOCK(mscl, BCLKCTR0, 25, NULL, NULL)
DEFINE_BCLOCK(bdma, BCLKCTR1, 0, NULL, NULL)
DEFINE_BCLOCK(adma0, BCLKCTR1, 1, NULL, NULL)
DEFINE_BCLOCK(scfg, BCLKCTR1, 3, NULL, NULL)
DEFINE_BCLOCK(cid, BCLKCTR1, 4, NULL, NULL)
DEFINE_BCLOCK(dai1, BCLKCTR1, 7, NULL, NULL)
DEFINE_BCLOCK(adma1, BCLKCTR1, 8, NULL, NULL)
DEFINE_BCLOCK(gps, BCLKCTR1, 12, NULL, NULL)
DEFINE_BCLOCK(gdma2, BCLKCTR1, 17, NULL, NULL)
DEFINE_BCLOCK(gdma3, BCLKCTR1, 18, NULL, NULL)
DEFINE_BCLOCK(ddrc, BCLKCTR1, 19, NULL, NULL)

#define _REGISTER_CLOCK(d, n, c) \
	{ \
		.dev_id = d, \
		.con_id = n, \
		.clk = &c, \
	},

static struct clk_lookup lookups[] = {
	_REGISTER_CLOCK(NULL, "bus", bus)
	_REGISTER_CLOCK(NULL, "cpu", cpu)
	_REGISTER_CLOCK(NULL, "tct", tct)
	_REGISTER_CLOCK(NULL, "tcx", tcx)
	_REGISTER_CLOCK(NULL, "tcz", tcz)
	_REGISTER_CLOCK(NULL, "ref", ref)
	_REGISTER_CLOCK(NULL, "dai0", dai0)
	_REGISTER_CLOCK(NULL, "pic", pic)
	_REGISTER_CLOCK(NULL, "tc", tc)
	_REGISTER_CLOCK(NULL, "gpio", gpio)
	_REGISTER_CLOCK(NULL, "usbd", usbd)
	_REGISTER_CLOCK("tcc-uart.0", NULL, uart0)
	_REGISTER_CLOCK("tcc-uart.2", NULL, uart2)
	_REGISTER_CLOCK("tcc-i2c", NULL, i2c)
	_REGISTER_CLOCK("tcc-uart.3", NULL, uart3)
	_REGISTER_CLOCK(NULL, "ecc", ecc)
	_REGISTER_CLOCK(NULL, "adc", adc)
	_REGISTER_CLOCK("tcc-usbh.0", "usb", usbh0)
	_REGISTER_CLOCK(NULL, "gdma0", gdma0)
	_REGISTER_CLOCK(NULL, "lcd", lcd)
	_REGISTER_CLOCK(NULL, "rtc", rtc)
	_REGISTER_CLOCK(NULL, "nfc", nfc)
	_REGISTER_CLOCK("tcc-mmc.0", NULL, sd0)
	_REGISTER_CLOCK(NULL, "g2d", g2d)
	_REGISTER_CLOCK(NULL, "gdma1", gdma1)
	_REGISTER_CLOCK("tcc-uart.1", NULL, uart1)
	_REGISTER_CLOCK("tcc-spi.0", NULL, spi0)
	_REGISTER_CLOCK(NULL, "mscl", mscl)
	_REGISTER_CLOCK("tcc-spi.1", NULL, spi1)
	_REGISTER_CLOCK(NULL, "bdma", bdma)
	_REGISTER_CLOCK(NULL, "adma0", adma0)
	_REGISTER_CLOCK(NULL, "spdif", spdif)
	_REGISTER_CLOCK(NULL, "scfg", scfg)
	_REGISTER_CLOCK(NULL, "cid", cid)
	_REGISTER_CLOCK("tcc-mmc.1", NULL, sd1)
	_REGISTER_CLOCK("tcc-uart.4", NULL, uart4)
	_REGISTER_CLOCK(NULL, "dai1", dai1)
	_REGISTER_CLOCK(NULL, "adma1", adma1)
	_REGISTER_CLOCK(NULL, "c3dec", c3dec)
	_REGISTER_CLOCK("tcc-can.0", NULL, can0)
	_REGISTER_CLOCK("tcc-can.1", NULL, can1)
	_REGISTER_CLOCK(NULL, "gps", gps)
	_REGISTER_CLOCK("tcc-gsb.0", NULL, gsb0)
	_REGISTER_CLOCK("tcc-gsb.1", NULL, gsb1)
	_REGISTER_CLOCK("tcc-gsb.2", NULL, gsb2)
	_REGISTER_CLOCK("tcc-gsb.3", NULL, gsb3)
	_REGISTER_CLOCK(NULL, "gdma2", gdma2)
	_REGISTER_CLOCK(NULL, "gdma3", gdma3)
	_REGISTER_CLOCK(NULL, "ddrc", ddrc)
	_REGISTER_CLOCK("tcc-usbh.1", "usb", usbh1)
};

static struct clk *root_clk_by_index(enum root_clks src)
{
	switch (src) {
	case CLK_SRC_PLL0: return &pll0;
	case CLK_SRC_PLL1: return &pll1;
	case CLK_SRC_PLL2: return &pll2;
	case CLK_SRC_PLL0DIV: return &pll0div;
	case CLK_SRC_PLL1DIV: return &pll1div;
	case CLK_SRC_PLL2DIV: return &pll2div;
	case CLK_SRC_XI: return &xi;
	case CLK_SRC_XTI: return &xti;
	case CLK_SRC_XIDIV: return &xidiv;
	case CLK_SRC_XTIDIV: return &xtidiv;
	default: return NULL;
	}
}

static void find_aclk_parent(struct clk *clk)
{
	unsigned int src;
	struct clk *clock;

	if (!clk->aclkreg)
		return;

	src = __raw_readl(clk->aclkreg) >> ACLK_SEL_SHIFT;
	src &= CLK_SRC_MASK;

	clock = root_clk_by_index(src);
	if (!clock)
		return;

	clk->parent = clock;
	clk->set_parent = aclk_set_parent;
}

void __init tcc_clocks_init(unsigned long xi_freq, unsigned long xti_freq)
{
	int i;

	xi_rate = xi_freq;
	xti_rate = xti_freq;

	/* fixup parents and add the clock */
	for (i = 0; i < ARRAY_SIZE(lookups); i++) {
		find_aclk_parent(lookups[i].clk);
		clkdev_add(&lookups[i]);
	}
	tcc8k_timer_init(&tcz, (void __iomem *)TIMER_BASE, INT_TC32);
}
