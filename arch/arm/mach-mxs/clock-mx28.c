/*
 * Copyright (C) 2009-2010 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/clkdev.h>

#include <asm/clkdev.h>
#include <asm/div64.h>

#include <mach/mx28.h>
#include <mach/common.h>
#include <mach/clock.h>

#include "regs-clkctrl-mx28.h"

#define CLKCTRL_BASE_ADDR	MX28_IO_ADDRESS(MX28_CLKCTRL_BASE_ADDR)
#define DIGCTRL_BASE_ADDR	MX28_IO_ADDRESS(MX28_DIGCTL_BASE_ADDR)

#define PARENT_RATE_SHIFT	8

static struct clk pll2_clk;
static struct clk cpu_clk;
static struct clk emi_clk;
static struct clk saif0_clk;
static struct clk saif1_clk;
static struct clk clk32k_clk;

static int _raw_clk_enable(struct clk *clk)
{
	u32 reg;

	if (clk->enable_reg) {
		reg = __raw_readl(clk->enable_reg);
		reg &= ~(1 << clk->enable_shift);
		__raw_writel(reg, clk->enable_reg);
	}

	return 0;
}

static void _raw_clk_disable(struct clk *clk)
{
	u32 reg;

	if (clk->enable_reg) {
		reg = __raw_readl(clk->enable_reg);
		reg |= 1 << clk->enable_shift;
		__raw_writel(reg, clk->enable_reg);
	}
}

/*
 * ref_xtal_clk
 */
static unsigned long ref_xtal_clk_get_rate(struct clk *clk)
{
	return 24000000;
}

static struct clk ref_xtal_clk = {
	.get_rate = ref_xtal_clk_get_rate,
};

/*
 * pll_clk
 */
static unsigned long pll0_clk_get_rate(struct clk *clk)
{
	return 480000000;
}

static unsigned long pll1_clk_get_rate(struct clk *clk)
{
	return 480000000;
}

static unsigned long pll2_clk_get_rate(struct clk *clk)
{
	return 50000000;
}

#define _CLK_ENABLE_PLL(name, r, g)					\
static int name##_enable(struct clk *clk)				\
{									\
	__raw_writel(BM_CLKCTRL_##r##CTRL0_POWER,			\
		     CLKCTRL_BASE_ADDR + HW_CLKCTRL_##r##CTRL0_SET);	\
	udelay(10);							\
									\
	if (clk == &pll2_clk)						\
		__raw_writel(BM_CLKCTRL_##r##CTRL0_##g,			\
			CLKCTRL_BASE_ADDR + HW_CLKCTRL_##r##CTRL0_CLR);	\
	else								\
		__raw_writel(BM_CLKCTRL_##r##CTRL0_##g,			\
			CLKCTRL_BASE_ADDR + HW_CLKCTRL_##r##CTRL0_SET);	\
									\
	return 0;							\
}

_CLK_ENABLE_PLL(pll0_clk, PLL0, EN_USB_CLKS)
_CLK_ENABLE_PLL(pll1_clk, PLL1, EN_USB_CLKS)
_CLK_ENABLE_PLL(pll2_clk, PLL2, CLKGATE)

#define _CLK_DISABLE_PLL(name, r, g)					\
static void name##_disable(struct clk *clk)				\
{									\
	__raw_writel(BM_CLKCTRL_##r##CTRL0_POWER,			\
		     CLKCTRL_BASE_ADDR + HW_CLKCTRL_##r##CTRL0_CLR);	\
									\
	if (clk == &pll2_clk)						\
		__raw_writel(BM_CLKCTRL_##r##CTRL0_##g,			\
			CLKCTRL_BASE_ADDR + HW_CLKCTRL_##r##CTRL0_SET);	\
	else								\
		__raw_writel(BM_CLKCTRL_##r##CTRL0_##g,			\
			CLKCTRL_BASE_ADDR + HW_CLKCTRL_##r##CTRL0_CLR);	\
									\
}

_CLK_DISABLE_PLL(pll0_clk, PLL0, EN_USB_CLKS)
_CLK_DISABLE_PLL(pll1_clk, PLL1, EN_USB_CLKS)
_CLK_DISABLE_PLL(pll2_clk, PLL2, CLKGATE)

#define _DEFINE_CLOCK_PLL(name)						\
	static struct clk name = {					\
		.get_rate	= name##_get_rate,			\
		.enable		= name##_enable,			\
		.disable	= name##_disable,			\
		.parent		= &ref_xtal_clk,			\
	}

_DEFINE_CLOCK_PLL(pll0_clk);
_DEFINE_CLOCK_PLL(pll1_clk);
_DEFINE_CLOCK_PLL(pll2_clk);

/*
 * ref_clk
 */
#define _CLK_GET_RATE_REF(name, sr, ss)					\
static unsigned long name##_get_rate(struct clk *clk)			\
{									\
	unsigned long parent_rate;					\
	u32 reg, div;							\
									\
	reg = __raw_readl(CLKCTRL_BASE_ADDR + HW_CLKCTRL_##sr);		\
	div = (reg >> BP_CLKCTRL_##sr##_##ss##FRAC) & 0x3f;		\
	parent_rate = clk_get_rate(clk->parent);			\
									\
	return SH_DIV((parent_rate >> PARENT_RATE_SHIFT) * 18,		\
			div, PARENT_RATE_SHIFT);			\
}

_CLK_GET_RATE_REF(ref_cpu_clk, FRAC0, CPU)
_CLK_GET_RATE_REF(ref_emi_clk, FRAC0, EMI)
_CLK_GET_RATE_REF(ref_io0_clk, FRAC0, IO0)
_CLK_GET_RATE_REF(ref_io1_clk, FRAC0, IO1)
_CLK_GET_RATE_REF(ref_pix_clk, FRAC1, PIX)
_CLK_GET_RATE_REF(ref_gpmi_clk, FRAC1, GPMI)

#define _DEFINE_CLOCK_REF(name, er, es)					\
	static struct clk name = {					\
		.enable_reg	= CLKCTRL_BASE_ADDR + HW_CLKCTRL_##er,	\
		.enable_shift	= BP_CLKCTRL_##er##_CLKGATE##es,	\
		.get_rate	= name##_get_rate,			\
		.enable		= _raw_clk_enable,			\
		.disable	= _raw_clk_disable,			\
		.parent		= &pll0_clk,				\
	}

_DEFINE_CLOCK_REF(ref_cpu_clk, FRAC0, CPU);
_DEFINE_CLOCK_REF(ref_emi_clk, FRAC0, EMI);
_DEFINE_CLOCK_REF(ref_io0_clk, FRAC0, IO0);
_DEFINE_CLOCK_REF(ref_io1_clk, FRAC0, IO1);
_DEFINE_CLOCK_REF(ref_pix_clk, FRAC1, PIX);
_DEFINE_CLOCK_REF(ref_gpmi_clk, FRAC1, GPMI);

/*
 * General clocks
 *
 * clk_get_rate
 */
static unsigned long lradc_clk_get_rate(struct clk *clk)
{
	return clk_get_rate(clk->parent) / 16;
}

static unsigned long rtc_clk_get_rate(struct clk *clk)
{
	/* ref_xtal_clk is implemented as the only parent */
	return clk_get_rate(clk->parent) / 768;
}

static unsigned long clk32k_clk_get_rate(struct clk *clk)
{
	return clk->parent->get_rate(clk->parent) / 750;
}

static unsigned long spdif_clk_get_rate(struct clk *clk)
{
	return clk_get_rate(clk->parent) / 4;
}

#define _CLK_GET_RATE(name, rs)						\
static unsigned long name##_get_rate(struct clk *clk)			\
{									\
	u32 reg, div;							\
									\
	reg = __raw_readl(CLKCTRL_BASE_ADDR + HW_CLKCTRL_##rs);		\
									\
	if (clk->parent == &ref_xtal_clk)				\
		div = (reg & BM_CLKCTRL_##rs##_DIV_XTAL) >>		\
			BP_CLKCTRL_##rs##_DIV_XTAL;			\
	else								\
		div = (reg & BM_CLKCTRL_##rs##_DIV_##rs) >>		\
			BP_CLKCTRL_##rs##_DIV_##rs;			\
									\
	if (!div)							\
		return -EINVAL;						\
									\
	return clk_get_rate(clk->parent) / div;				\
}

_CLK_GET_RATE(cpu_clk, CPU)
_CLK_GET_RATE(emi_clk, EMI)

#define _CLK_GET_RATE1(name, rs)					\
static unsigned long name##_get_rate(struct clk *clk)			\
{									\
	u32 reg, div;							\
									\
	reg = __raw_readl(CLKCTRL_BASE_ADDR + HW_CLKCTRL_##rs);		\
	div = (reg & BM_CLKCTRL_##rs##_DIV) >> BP_CLKCTRL_##rs##_DIV;	\
									\
	if (!div)							\
		return -EINVAL;						\
									\
	if (clk == &saif0_clk || clk == &saif1_clk)			\
		return clk_get_rate(clk->parent) >> 16 * div;		\
	else								\
		return clk_get_rate(clk->parent) / div;			\
}

_CLK_GET_RATE1(hbus_clk, HBUS)
_CLK_GET_RATE1(xbus_clk, XBUS)
_CLK_GET_RATE1(ssp0_clk, SSP0)
_CLK_GET_RATE1(ssp1_clk, SSP1)
_CLK_GET_RATE1(ssp2_clk, SSP2)
_CLK_GET_RATE1(ssp3_clk, SSP3)
_CLK_GET_RATE1(gpmi_clk, GPMI)
_CLK_GET_RATE1(lcdif_clk, DIS_LCDIF)
_CLK_GET_RATE1(saif0_clk, SAIF0)
_CLK_GET_RATE1(saif1_clk, SAIF1)

#define _CLK_GET_RATE_STUB(name)					\
static unsigned long name##_get_rate(struct clk *clk)			\
{									\
	return clk_get_rate(clk->parent);				\
}

_CLK_GET_RATE_STUB(uart_clk)
_CLK_GET_RATE_STUB(pwm_clk)
_CLK_GET_RATE_STUB(can0_clk)
_CLK_GET_RATE_STUB(can1_clk)
_CLK_GET_RATE_STUB(fec_clk)

/*
 * clk_set_rate
 */
/* fool compiler */
#define BM_CLKCTRL_CPU_DIV	0
#define BP_CLKCTRL_CPU_DIV	0
#define BM_CLKCTRL_CPU_BUSY	0

#define _CLK_SET_RATE(name, dr, fr, fs)					\
static int name##_set_rate(struct clk *clk, unsigned long rate)		\
{									\
	u32 reg, bm_busy, div_max, d, f, div, frac;			\
	unsigned long diff, parent_rate, calc_rate;			\
	int i;								\
									\
	parent_rate = clk_get_rate(clk->parent);			\
	div_max = BM_CLKCTRL_##dr##_DIV >> BP_CLKCTRL_##dr##_DIV;	\
	bm_busy = BM_CLKCTRL_##dr##_BUSY;				\
									\
	if (clk->parent == &ref_xtal_clk) {				\
		div = DIV_ROUND_UP(parent_rate, rate);			\
		if (clk == &cpu_clk) {					\
			div_max = BM_CLKCTRL_CPU_DIV_XTAL >>		\
				BP_CLKCTRL_CPU_DIV_XTAL;		\
			bm_busy = BM_CLKCTRL_CPU_BUSY_REF_XTAL;		\
		}							\
		if (div == 0 || div > div_max)				\
			return -EINVAL;					\
	} else {							\
		rate >>= PARENT_RATE_SHIFT;				\
		parent_rate >>= PARENT_RATE_SHIFT;			\
		diff = parent_rate;					\
		div = frac = 1;						\
		if (clk == &cpu_clk) {					\
			div_max = BM_CLKCTRL_CPU_DIV_CPU >>		\
				BP_CLKCTRL_CPU_DIV_CPU;			\
			bm_busy = BM_CLKCTRL_CPU_BUSY_REF_CPU;		\
		}							\
		for (d = 1; d <= div_max; d++) {			\
			f = parent_rate * 18 / d / rate;		\
			if ((parent_rate * 18 / d) % rate)		\
				f++;					\
			if (f < 18 || f > 35)				\
				continue;				\
									\
			calc_rate = parent_rate * 18 / f / d;		\
			if (calc_rate > rate)				\
				continue;				\
									\
			if (rate - calc_rate < diff) {			\
				frac = f;				\
				div = d;				\
				diff = rate - calc_rate;		\
			}						\
									\
			if (diff == 0)					\
				break;					\
		}							\
									\
		if (diff == parent_rate)				\
			return -EINVAL;					\
									\
		reg = __raw_readl(CLKCTRL_BASE_ADDR + HW_CLKCTRL_##fr);	\
		reg &= ~BM_CLKCTRL_##fr##_##fs##FRAC;			\
		reg |= frac;						\
		__raw_writel(reg, CLKCTRL_BASE_ADDR + HW_CLKCTRL_##fr);	\
	}								\
									\
	reg = __raw_readl(CLKCTRL_BASE_ADDR + HW_CLKCTRL_##dr);		\
	if (clk == &cpu_clk) {						\
		reg &= ~BM_CLKCTRL_CPU_DIV_CPU;				\
		reg |= div << BP_CLKCTRL_CPU_DIV_CPU;			\
	} else {							\
		reg &= ~BM_CLKCTRL_##dr##_DIV;				\
		reg |= div << BP_CLKCTRL_##dr##_DIV;			\
		if (reg & (1 << clk->enable_shift)) {			\
			pr_err("%s: clock is gated\n", __func__);	\
			return -EINVAL;					\
		}							\
	}								\
	__raw_writel(reg, CLKCTRL_BASE_ADDR + HW_CLKCTRL_##dr);		\
									\
	for (i = 10000; i; i--)						\
		if (!(__raw_readl(CLKCTRL_BASE_ADDR +			\
			HW_CLKCTRL_##dr) & bm_busy))			\
			break;						\
	if (!i)	{							\
		pr_err("%s: divider writing timeout\n", __func__);	\
		return -ETIMEDOUT;					\
	}								\
									\
	return 0;							\
}

_CLK_SET_RATE(cpu_clk, CPU, FRAC0, CPU)
_CLK_SET_RATE(ssp0_clk, SSP0, FRAC0, IO0)
_CLK_SET_RATE(ssp1_clk, SSP1, FRAC0, IO0)
_CLK_SET_RATE(ssp2_clk, SSP2, FRAC0, IO1)
_CLK_SET_RATE(ssp3_clk, SSP3, FRAC0, IO1)
_CLK_SET_RATE(lcdif_clk, DIS_LCDIF, FRAC1, PIX)
_CLK_SET_RATE(gpmi_clk, GPMI, FRAC1, GPMI)

#define _CLK_SET_RATE1(name, dr)					\
static int name##_set_rate(struct clk *clk, unsigned long rate)		\
{									\
	u32 reg, div_max, div;						\
	unsigned long parent_rate;					\
	int i;								\
									\
	parent_rate = clk_get_rate(clk->parent);			\
	div_max = BM_CLKCTRL_##dr##_DIV >> BP_CLKCTRL_##dr##_DIV;	\
									\
	div = DIV_ROUND_UP(parent_rate, rate);				\
	if (div == 0 || div > div_max)					\
		return -EINVAL;						\
									\
	reg = __raw_readl(CLKCTRL_BASE_ADDR + HW_CLKCTRL_##dr);		\
	reg &= ~BM_CLKCTRL_##dr##_DIV;					\
	reg |= div << BP_CLKCTRL_##dr##_DIV;				\
	if (reg | (1 << clk->enable_shift)) {				\
		pr_err("%s: clock is gated\n", __func__);		\
		return -EINVAL;						\
	}								\
	__raw_writel(reg, CLKCTRL_BASE_ADDR + HW_CLKCTRL_##dr);		\
									\
	for (i = 10000; i; i--)						\
		if (!(__raw_readl(CLKCTRL_BASE_ADDR +			\
			HW_CLKCTRL_##dr) & BM_CLKCTRL_##dr##_BUSY))	\
			break;						\
	if (!i)	{							\
		pr_err("%s: divider writing timeout\n", __func__);	\
		return -ETIMEDOUT;					\
	}								\
									\
	return 0;							\
}

_CLK_SET_RATE1(xbus_clk, XBUS)

/* saif clock uses 16 bits frac div */
#define _CLK_SET_RATE_SAIF(name, rs)					\
static int name##_set_rate(struct clk *clk, unsigned long rate)		\
{									\
	u16 div;							\
	u32 reg;							\
	u64 lrate;							\
	unsigned long parent_rate;					\
	int i;								\
									\
	parent_rate = clk_get_rate(clk->parent);			\
	if (rate > parent_rate)						\
		return -EINVAL;						\
									\
	lrate = (u64)rate << 16;					\
	do_div(lrate, parent_rate);					\
	div = (u16)lrate;						\
									\
	if (!div)							\
		return -EINVAL;						\
									\
	reg = __raw_readl(CLKCTRL_BASE_ADDR + HW_CLKCTRL_##rs);		\
	reg &= ~BM_CLKCTRL_##rs##_DIV;					\
	reg |= div << BP_CLKCTRL_##rs##_DIV;				\
	__raw_writel(reg, CLKCTRL_BASE_ADDR + HW_CLKCTRL_##rs);		\
									\
	for (i = 10000; i; i--)						\
		if (!(__raw_readl(CLKCTRL_BASE_ADDR +			\
			HW_CLKCTRL_##rs) & BM_CLKCTRL_##rs##_BUSY))	\
			break;						\
	if (!i) {							\
		pr_err("%s: divider writing timeout\n", __func__);	\
		return -ETIMEDOUT;					\
	}								\
									\
	return 0;							\
}

_CLK_SET_RATE_SAIF(saif0_clk, SAIF0)
_CLK_SET_RATE_SAIF(saif1_clk, SAIF1)

#define _CLK_SET_RATE_STUB(name)					\
static int name##_set_rate(struct clk *clk, unsigned long rate)		\
{									\
	return -EINVAL;							\
}

_CLK_SET_RATE_STUB(emi_clk)
_CLK_SET_RATE_STUB(uart_clk)
_CLK_SET_RATE_STUB(pwm_clk)
_CLK_SET_RATE_STUB(spdif_clk)
_CLK_SET_RATE_STUB(clk32k_clk)
_CLK_SET_RATE_STUB(can0_clk)
_CLK_SET_RATE_STUB(can1_clk)
_CLK_SET_RATE_STUB(fec_clk)

/*
 * clk_set_parent
 */
#define _CLK_SET_PARENT(name, bit)					\
static int name##_set_parent(struct clk *clk, struct clk *parent)	\
{									\
	if (parent != clk->parent) {					\
		__raw_writel(BM_CLKCTRL_CLKSEQ_BYPASS_##bit,		\
			 CLKCTRL_BASE_ADDR + HW_CLKCTRL_CLKSEQ_TOG);	\
		clk->parent = parent;					\
	}								\
									\
	return 0;							\
}

_CLK_SET_PARENT(cpu_clk, CPU)
_CLK_SET_PARENT(emi_clk, EMI)
_CLK_SET_PARENT(ssp0_clk, SSP0)
_CLK_SET_PARENT(ssp1_clk, SSP1)
_CLK_SET_PARENT(ssp2_clk, SSP2)
_CLK_SET_PARENT(ssp3_clk, SSP3)
_CLK_SET_PARENT(lcdif_clk, DIS_LCDIF)
_CLK_SET_PARENT(gpmi_clk, GPMI)
_CLK_SET_PARENT(saif0_clk, SAIF0)
_CLK_SET_PARENT(saif1_clk, SAIF1)

#define _CLK_SET_PARENT_STUB(name)					\
static int name##_set_parent(struct clk *clk, struct clk *parent)	\
{									\
	if (parent != clk->parent)					\
		return -EINVAL;						\
	else								\
		return 0;						\
}

_CLK_SET_PARENT_STUB(pwm_clk)
_CLK_SET_PARENT_STUB(uart_clk)
_CLK_SET_PARENT_STUB(clk32k_clk)
_CLK_SET_PARENT_STUB(spdif_clk)
_CLK_SET_PARENT_STUB(fec_clk)
_CLK_SET_PARENT_STUB(can0_clk)
_CLK_SET_PARENT_STUB(can1_clk)

/*
 * clk definition
 */
static struct clk cpu_clk = {
	.get_rate = cpu_clk_get_rate,
	.set_rate = cpu_clk_set_rate,
	.set_parent = cpu_clk_set_parent,
	.parent = &ref_cpu_clk,
};

static struct clk hbus_clk = {
	.get_rate = hbus_clk_get_rate,
	.parent = &cpu_clk,
};

static struct clk xbus_clk = {
	.get_rate = xbus_clk_get_rate,
	.set_rate = xbus_clk_set_rate,
	.parent = &ref_xtal_clk,
};

static struct clk lradc_clk = {
	.get_rate = lradc_clk_get_rate,
	.parent = &clk32k_clk,
};

static struct clk rtc_clk = {
	.get_rate = rtc_clk_get_rate,
	.parent = &ref_xtal_clk,
};

/* usb_clk gate is controlled in DIGCTRL other than CLKCTRL */
static struct clk usb0_clk = {
	.enable_reg = DIGCTRL_BASE_ADDR,
	.enable_shift = 2,
	.enable = _raw_clk_enable,
	.disable = _raw_clk_disable,
	.parent = &pll0_clk,
};

static struct clk usb1_clk = {
	.enable_reg = DIGCTRL_BASE_ADDR,
	.enable_shift = 16,
	.enable = _raw_clk_enable,
	.disable = _raw_clk_disable,
	.parent = &pll1_clk,
};

#define _DEFINE_CLOCK(name, er, es, p)					\
	static struct clk name = {					\
		.enable_reg	= CLKCTRL_BASE_ADDR + HW_CLKCTRL_##er,	\
		.enable_shift	= BP_CLKCTRL_##er##_##es,		\
		.get_rate	= name##_get_rate,			\
		.set_rate	= name##_set_rate,			\
		.set_parent	= name##_set_parent,			\
		.enable		= _raw_clk_enable,			\
		.disable	= _raw_clk_disable,			\
		.parent		= p,					\
	}

_DEFINE_CLOCK(emi_clk, EMI, CLKGATE, &ref_xtal_clk);
_DEFINE_CLOCK(ssp0_clk, SSP0, CLKGATE, &ref_xtal_clk);
_DEFINE_CLOCK(ssp1_clk, SSP1, CLKGATE, &ref_xtal_clk);
_DEFINE_CLOCK(ssp2_clk, SSP2, CLKGATE, &ref_xtal_clk);
_DEFINE_CLOCK(ssp3_clk, SSP3, CLKGATE, &ref_xtal_clk);
_DEFINE_CLOCK(lcdif_clk, DIS_LCDIF, CLKGATE, &ref_xtal_clk);
_DEFINE_CLOCK(gpmi_clk, GPMI, CLKGATE, &ref_xtal_clk);
_DEFINE_CLOCK(saif0_clk, SAIF0, CLKGATE, &ref_xtal_clk);
_DEFINE_CLOCK(saif1_clk, SAIF1, CLKGATE, &ref_xtal_clk);
_DEFINE_CLOCK(can0_clk, FLEXCAN, STOP_CAN0, &ref_xtal_clk);
_DEFINE_CLOCK(can1_clk, FLEXCAN, STOP_CAN1, &ref_xtal_clk);
_DEFINE_CLOCK(pwm_clk, XTAL, PWM_CLK24M_GATE, &ref_xtal_clk);
_DEFINE_CLOCK(uart_clk, XTAL, UART_CLK_GATE, &ref_xtal_clk);
_DEFINE_CLOCK(clk32k_clk, XTAL, TIMROT_CLK32K_GATE, &ref_xtal_clk);
_DEFINE_CLOCK(spdif_clk, SPDIF, CLKGATE, &pll0_clk);
_DEFINE_CLOCK(fec_clk, ENET, DISABLE, &hbus_clk);

#define _REGISTER_CLOCK(d, n, c) \
	{ \
		.dev_id = d, \
		.con_id = n, \
		.clk = &c, \
	},

static struct clk_lookup lookups[] = {
	/* for amba bus driver */
	_REGISTER_CLOCK("duart", "apb_pclk", xbus_clk)
	/* for amba-pl011 driver */
	_REGISTER_CLOCK("duart", NULL, uart_clk)
	_REGISTER_CLOCK("imx28-fec.0", NULL, fec_clk)
	_REGISTER_CLOCK("imx28-fec.1", NULL, fec_clk)
	_REGISTER_CLOCK("rtc", NULL, rtc_clk)
	_REGISTER_CLOCK("pll2", NULL, pll2_clk)
	_REGISTER_CLOCK(NULL, "hclk", hbus_clk)
	_REGISTER_CLOCK(NULL, "xclk", xbus_clk)
	_REGISTER_CLOCK(NULL, "can0", can0_clk)
	_REGISTER_CLOCK(NULL, "can1", can1_clk)
	_REGISTER_CLOCK(NULL, "usb0", usb0_clk)
	_REGISTER_CLOCK(NULL, "usb1", usb1_clk)
	_REGISTER_CLOCK(NULL, "pwm", pwm_clk)
	_REGISTER_CLOCK(NULL, "lradc", lradc_clk)
	_REGISTER_CLOCK(NULL, "spdif", spdif_clk)
};

static int clk_misc_init(void)
{
	u32 reg;
	int i;

	/* Fix up parent per register setting */
	reg = __raw_readl(CLKCTRL_BASE_ADDR + HW_CLKCTRL_CLKSEQ);
	cpu_clk.parent = (reg & BM_CLKCTRL_CLKSEQ_BYPASS_CPU) ?
			&ref_xtal_clk : &ref_cpu_clk;
	emi_clk.parent = (reg & BM_CLKCTRL_CLKSEQ_BYPASS_EMI) ?
			&ref_xtal_clk : &ref_emi_clk;
	ssp0_clk.parent = (reg & BM_CLKCTRL_CLKSEQ_BYPASS_SSP0) ?
			&ref_xtal_clk : &ref_io0_clk;
	ssp1_clk.parent = (reg & BM_CLKCTRL_CLKSEQ_BYPASS_SSP1) ?
			&ref_xtal_clk : &ref_io0_clk;
	ssp2_clk.parent = (reg & BM_CLKCTRL_CLKSEQ_BYPASS_SSP2) ?
			&ref_xtal_clk : &ref_io1_clk;
	ssp3_clk.parent = (reg & BM_CLKCTRL_CLKSEQ_BYPASS_SSP3) ?
			&ref_xtal_clk : &ref_io1_clk;
	lcdif_clk.parent = (reg & BM_CLKCTRL_CLKSEQ_BYPASS_DIS_LCDIF) ?
			&ref_xtal_clk : &ref_pix_clk;
	gpmi_clk.parent = (reg & BM_CLKCTRL_CLKSEQ_BYPASS_GPMI) ?
			&ref_xtal_clk : &ref_gpmi_clk;
	saif0_clk.parent = (reg & BM_CLKCTRL_CLKSEQ_BYPASS_SAIF0) ?
			&ref_xtal_clk : &pll0_clk;
	saif1_clk.parent = (reg & BM_CLKCTRL_CLKSEQ_BYPASS_SAIF1) ?
			&ref_xtal_clk : &pll0_clk;

	/* Use int div over frac when both are available */
	__raw_writel(BM_CLKCTRL_CPU_DIV_XTAL_FRAC_EN,
			CLKCTRL_BASE_ADDR + HW_CLKCTRL_CPU_CLR);
	__raw_writel(BM_CLKCTRL_CPU_DIV_CPU_FRAC_EN,
			CLKCTRL_BASE_ADDR + HW_CLKCTRL_CPU_CLR);
	__raw_writel(BM_CLKCTRL_HBUS_DIV_FRAC_EN,
			CLKCTRL_BASE_ADDR + HW_CLKCTRL_HBUS_CLR);

	reg = __raw_readl(CLKCTRL_BASE_ADDR + HW_CLKCTRL_XBUS);
	reg &= ~BM_CLKCTRL_XBUS_DIV_FRAC_EN;
	__raw_writel(reg, CLKCTRL_BASE_ADDR + HW_CLKCTRL_XBUS);

	reg = __raw_readl(CLKCTRL_BASE_ADDR + HW_CLKCTRL_SSP0);
	reg &= ~BM_CLKCTRL_SSP0_DIV_FRAC_EN;
	__raw_writel(reg, CLKCTRL_BASE_ADDR + HW_CLKCTRL_SSP0);

	reg = __raw_readl(CLKCTRL_BASE_ADDR + HW_CLKCTRL_SSP1);
	reg &= ~BM_CLKCTRL_SSP1_DIV_FRAC_EN;
	__raw_writel(reg, CLKCTRL_BASE_ADDR + HW_CLKCTRL_SSP1);

	reg = __raw_readl(CLKCTRL_BASE_ADDR + HW_CLKCTRL_SSP2);
	reg &= ~BM_CLKCTRL_SSP2_DIV_FRAC_EN;
	__raw_writel(reg, CLKCTRL_BASE_ADDR + HW_CLKCTRL_SSP2);

	reg = __raw_readl(CLKCTRL_BASE_ADDR + HW_CLKCTRL_SSP3);
	reg &= ~BM_CLKCTRL_SSP3_DIV_FRAC_EN;
	__raw_writel(reg, CLKCTRL_BASE_ADDR + HW_CLKCTRL_SSP3);

	reg = __raw_readl(CLKCTRL_BASE_ADDR + HW_CLKCTRL_GPMI);
	reg &= ~BM_CLKCTRL_GPMI_DIV_FRAC_EN;
	__raw_writel(reg, CLKCTRL_BASE_ADDR + HW_CLKCTRL_GPMI);

	reg = __raw_readl(CLKCTRL_BASE_ADDR + HW_CLKCTRL_DIS_LCDIF);
	reg &= ~BM_CLKCTRL_DIS_LCDIF_DIV_FRAC_EN;
	__raw_writel(reg, CLKCTRL_BASE_ADDR + HW_CLKCTRL_DIS_LCDIF);

	/* SAIF has to use frac div for functional operation */
	reg = __raw_readl(CLKCTRL_BASE_ADDR + HW_CLKCTRL_SAIF0);
	reg &= ~BM_CLKCTRL_SAIF0_DIV_FRAC_EN;
	__raw_writel(reg, CLKCTRL_BASE_ADDR + HW_CLKCTRL_SAIF0);

	reg = __raw_readl(CLKCTRL_BASE_ADDR + HW_CLKCTRL_SAIF1);
	reg &= ~BM_CLKCTRL_SAIF1_DIV_FRAC_EN;
	__raw_writel(reg, CLKCTRL_BASE_ADDR + HW_CLKCTRL_SAIF1);

	/*
	 * Set safe hbus clock divider. A divider of 3 ensure that
	 * the Vddd voltage required for the cpu clock is sufficiently
	 * high for the hbus clock.
	 */
	reg = __raw_readl(CLKCTRL_BASE_ADDR + HW_CLKCTRL_HBUS);
	reg &= BM_CLKCTRL_HBUS_DIV;
	reg |= 3 << BP_CLKCTRL_HBUS_DIV;
	__raw_writel(reg, CLKCTRL_BASE_ADDR + HW_CLKCTRL_HBUS);

	for (i = 10000; i; i--)
		if (!(__raw_readl(CLKCTRL_BASE_ADDR +
			HW_CLKCTRL_HBUS) & BM_CLKCTRL_HBUS_ASM_BUSY))
			break;
	if (!i) {
		pr_err("%s: divider writing timeout\n", __func__);
		return -ETIMEDOUT;
	}

	/* Gate off cpu clock in WFI for power saving */
	__raw_writel(BM_CLKCTRL_CPU_INTERRUPT_WAIT,
			CLKCTRL_BASE_ADDR + HW_CLKCTRL_CPU_SET);

	/* Extra fec clock setting */
	reg = __raw_readl(CLKCTRL_BASE_ADDR + HW_CLKCTRL_ENET);
	reg &= ~BM_CLKCTRL_ENET_SLEEP;
	reg |= BM_CLKCTRL_ENET_CLK_OUT_EN;
	__raw_writel(reg, CLKCTRL_BASE_ADDR + HW_CLKCTRL_ENET);

	return 0;
}

int __init mx28_clocks_init(void)
{
	clk_misc_init();

	clk_enable(&cpu_clk);
	clk_enable(&hbus_clk);
	clk_enable(&xbus_clk);
	clk_enable(&emi_clk);
	clk_enable(&uart_clk);

	clkdev_add_table(lookups, ARRAY_SIZE(lookups));

	mxs_timer_init(&clk32k_clk, MX28_INT_TIMER0);

	return 0;
}
