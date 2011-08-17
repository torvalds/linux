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

#include <mach/mx23.h>
#include <mach/common.h>
#include <mach/clock.h>

#include "regs-clkctrl-mx23.h"

#define CLKCTRL_BASE_ADDR	MX23_IO_ADDRESS(MX23_CLKCTRL_BASE_ADDR)
#define DIGCTRL_BASE_ADDR	MX23_IO_ADDRESS(MX23_DIGCTL_BASE_ADDR)

#define PARENT_RATE_SHIFT	8

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
static unsigned long pll_clk_get_rate(struct clk *clk)
{
	return 480000000;
}

static int pll_clk_enable(struct clk *clk)
{
	__raw_writel(BM_CLKCTRL_PLLCTRL0_POWER |
			BM_CLKCTRL_PLLCTRL0_EN_USB_CLKS,
			CLKCTRL_BASE_ADDR + HW_CLKCTRL_PLLCTRL0_SET);

	/* Only a 10us delay is need. PLLCTRL1 LOCK bitfied is only a timer
	 * and is incorrect (excessive). Per definition of the PLLCTRL0
	 * POWER field, waiting at least 10us.
	 */
	udelay(10);

	return 0;
}

static void pll_clk_disable(struct clk *clk)
{
	__raw_writel(BM_CLKCTRL_PLLCTRL0_POWER |
			BM_CLKCTRL_PLLCTRL0_EN_USB_CLKS,
			CLKCTRL_BASE_ADDR + HW_CLKCTRL_PLLCTRL0_CLR);
}

static struct clk pll_clk = {
	 .get_rate = pll_clk_get_rate,
	 .enable = pll_clk_enable,
	 .disable = pll_clk_disable,
	 .parent = &ref_xtal_clk,
};

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

_CLK_GET_RATE_REF(ref_cpu_clk, FRAC, CPU)
_CLK_GET_RATE_REF(ref_emi_clk, FRAC, EMI)
_CLK_GET_RATE_REF(ref_pix_clk, FRAC, PIX)
_CLK_GET_RATE_REF(ref_io_clk, FRAC, IO)

#define _DEFINE_CLOCK_REF(name, er, es)					\
	static struct clk name = {					\
		.enable_reg	= CLKCTRL_BASE_ADDR + HW_CLKCTRL_##er,	\
		.enable_shift	= BP_CLKCTRL_##er##_CLKGATE##es,	\
		.get_rate	= name##_get_rate,			\
		.enable		= _raw_clk_enable,			\
		.disable	= _raw_clk_disable,			\
		.parent		= &pll_clk,				\
	}

_DEFINE_CLOCK_REF(ref_cpu_clk, FRAC, CPU);
_DEFINE_CLOCK_REF(ref_emi_clk, FRAC, EMI);
_DEFINE_CLOCK_REF(ref_pix_clk, FRAC, PIX);
_DEFINE_CLOCK_REF(ref_io_clk, FRAC, IO);

/*
 * General clocks
 *
 * clk_get_rate
 */
static unsigned long rtc_clk_get_rate(struct clk *clk)
{
	/* ref_xtal_clk is implemented as the only parent */
	return clk_get_rate(clk->parent) / 768;
}

static unsigned long clk32k_clk_get_rate(struct clk *clk)
{
	return clk->parent->get_rate(clk->parent) / 750;
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
	return clk_get_rate(clk->parent) / div;				\
}

_CLK_GET_RATE1(hbus_clk, HBUS)
_CLK_GET_RATE1(xbus_clk, XBUS)
_CLK_GET_RATE1(ssp_clk, SSP)
_CLK_GET_RATE1(gpmi_clk, GPMI)
_CLK_GET_RATE1(lcdif_clk, PIX)

#define _CLK_GET_RATE_STUB(name)					\
static unsigned long name##_get_rate(struct clk *clk)			\
{									\
	return clk_get_rate(clk->parent);				\
}

_CLK_GET_RATE_STUB(uart_clk)
_CLK_GET_RATE_STUB(audio_clk)
_CLK_GET_RATE_STUB(pwm_clk)

/*
 * clk_set_rate
 */
static int cpu_clk_set_rate(struct clk *clk, unsigned long rate)
{
	u32 reg, bm_busy, div_max, d, f, div, frac;
	unsigned long diff, parent_rate, calc_rate;
	int i;

	parent_rate = clk_get_rate(clk->parent);

	if (clk->parent == &ref_xtal_clk) {
		div_max = BM_CLKCTRL_CPU_DIV_XTAL >> BP_CLKCTRL_CPU_DIV_XTAL;
		bm_busy = BM_CLKCTRL_CPU_BUSY_REF_XTAL;
		div = DIV_ROUND_UP(parent_rate, rate);
		if (div == 0 || div > div_max)
			return -EINVAL;
	} else {
		div_max = BM_CLKCTRL_CPU_DIV_CPU >> BP_CLKCTRL_CPU_DIV_CPU;
		bm_busy = BM_CLKCTRL_CPU_BUSY_REF_CPU;
		rate >>= PARENT_RATE_SHIFT;
		parent_rate >>= PARENT_RATE_SHIFT;
		diff = parent_rate;
		div = frac = 1;
		for (d = 1; d <= div_max; d++) {
			f = parent_rate * 18 / d / rate;
			if ((parent_rate * 18 / d) % rate)
				f++;
			if (f < 18 || f > 35)
				continue;

			calc_rate = parent_rate * 18 / f / d;
			if (calc_rate > rate)
				continue;

			if (rate - calc_rate < diff) {
				frac = f;
				div = d;
				diff = rate - calc_rate;
			}

			if (diff == 0)
				break;
		}

		if (diff == parent_rate)
			return -EINVAL;

		reg = __raw_readl(CLKCTRL_BASE_ADDR + HW_CLKCTRL_FRAC);
		reg &= ~BM_CLKCTRL_FRAC_CPUFRAC;
		reg |= frac;
		__raw_writel(reg, CLKCTRL_BASE_ADDR + HW_CLKCTRL_FRAC);
	}

	reg = __raw_readl(CLKCTRL_BASE_ADDR + HW_CLKCTRL_CPU);
	reg &= ~BM_CLKCTRL_CPU_DIV_CPU;
	reg |= div << BP_CLKCTRL_CPU_DIV_CPU;
	__raw_writel(reg, CLKCTRL_BASE_ADDR + HW_CLKCTRL_CPU);

	for (i = 10000; i; i--)
		if (!(__raw_readl(CLKCTRL_BASE_ADDR +
					HW_CLKCTRL_CPU) & bm_busy))
			break;
	if (!i)	{
		pr_err("%s: divider writing timeout\n", __func__);
		return -ETIMEDOUT;
	}

	return 0;
}

#define _CLK_SET_RATE(name, dr)						\
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
	if (reg & (1 << clk->enable_shift)) {				\
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

_CLK_SET_RATE(xbus_clk, XBUS)
_CLK_SET_RATE(ssp_clk, SSP)
_CLK_SET_RATE(gpmi_clk, GPMI)
_CLK_SET_RATE(lcdif_clk, PIX)

#define _CLK_SET_RATE_STUB(name)					\
static int name##_set_rate(struct clk *clk, unsigned long rate)		\
{									\
	return -EINVAL;							\
}

_CLK_SET_RATE_STUB(emi_clk)
_CLK_SET_RATE_STUB(uart_clk)
_CLK_SET_RATE_STUB(audio_clk)
_CLK_SET_RATE_STUB(pwm_clk)
_CLK_SET_RATE_STUB(clk32k_clk)

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
_CLK_SET_PARENT(ssp_clk, SSP)
_CLK_SET_PARENT(gpmi_clk, GPMI)
_CLK_SET_PARENT(lcdif_clk, PIX)

#define _CLK_SET_PARENT_STUB(name)					\
static int name##_set_parent(struct clk *clk, struct clk *parent)	\
{									\
	if (parent != clk->parent)					\
		return -EINVAL;						\
	else								\
		return 0;						\
}

_CLK_SET_PARENT_STUB(uart_clk)
_CLK_SET_PARENT_STUB(audio_clk)
_CLK_SET_PARENT_STUB(pwm_clk)
_CLK_SET_PARENT_STUB(clk32k_clk)

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

static struct clk rtc_clk = {
	.get_rate = rtc_clk_get_rate,
	.parent = &ref_xtal_clk,
};

/* usb_clk gate is controlled in DIGCTRL other than CLKCTRL */
static struct clk usb_clk = {
	.enable_reg = DIGCTRL_BASE_ADDR,
	.enable_shift = 2,
	.enable = _raw_clk_enable,
	.disable = _raw_clk_disable,
	.parent = &pll_clk,
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
_DEFINE_CLOCK(ssp_clk, SSP, CLKGATE, &ref_xtal_clk);
_DEFINE_CLOCK(gpmi_clk, GPMI, CLKGATE, &ref_xtal_clk);
_DEFINE_CLOCK(lcdif_clk, PIX, CLKGATE, &ref_xtal_clk);
_DEFINE_CLOCK(uart_clk, XTAL, UART_CLK_GATE, &ref_xtal_clk);
_DEFINE_CLOCK(audio_clk, XTAL, FILT_CLK24M_GATE, &ref_xtal_clk);
_DEFINE_CLOCK(pwm_clk, XTAL, PWM_CLK24M_GATE, &ref_xtal_clk);
_DEFINE_CLOCK(clk32k_clk, XTAL, TIMROT_CLK32K_GATE, &ref_xtal_clk);

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
	_REGISTER_CLOCK("mxs-auart.0", NULL, uart_clk)
	_REGISTER_CLOCK("rtc", NULL, rtc_clk)
	_REGISTER_CLOCK("mxs-dma-apbh", NULL, hbus_clk)
	_REGISTER_CLOCK("mxs-dma-apbx", NULL, xbus_clk)
	_REGISTER_CLOCK("mxs-mmc.0", NULL, ssp_clk)
	_REGISTER_CLOCK("mxs-mmc.1", NULL, ssp_clk)
	_REGISTER_CLOCK(NULL, "usb", usb_clk)
	_REGISTER_CLOCK(NULL, "audio", audio_clk)
	_REGISTER_CLOCK("mxs-pwm.0", NULL, pwm_clk)
	_REGISTER_CLOCK("mxs-pwm.1", NULL, pwm_clk)
	_REGISTER_CLOCK("mxs-pwm.2", NULL, pwm_clk)
	_REGISTER_CLOCK("mxs-pwm.3", NULL, pwm_clk)
	_REGISTER_CLOCK("mxs-pwm.4", NULL, pwm_clk)
	_REGISTER_CLOCK("imx23-fb", NULL, lcdif_clk)
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
	ssp_clk.parent = (reg & BM_CLKCTRL_CLKSEQ_BYPASS_SSP) ?
			&ref_xtal_clk : &ref_io_clk;
	gpmi_clk.parent = (reg & BM_CLKCTRL_CLKSEQ_BYPASS_GPMI) ?
			&ref_xtal_clk : &ref_io_clk;
	lcdif_clk.parent = (reg & BM_CLKCTRL_CLKSEQ_BYPASS_PIX) ?
			&ref_xtal_clk : &ref_pix_clk;

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

	reg = __raw_readl(CLKCTRL_BASE_ADDR + HW_CLKCTRL_SSP);
	reg &= ~BM_CLKCTRL_SSP_DIV_FRAC_EN;
	__raw_writel(reg, CLKCTRL_BASE_ADDR + HW_CLKCTRL_SSP);

	reg = __raw_readl(CLKCTRL_BASE_ADDR + HW_CLKCTRL_GPMI);
	reg &= ~BM_CLKCTRL_GPMI_DIV_FRAC_EN;
	__raw_writel(reg, CLKCTRL_BASE_ADDR + HW_CLKCTRL_GPMI);

	reg = __raw_readl(CLKCTRL_BASE_ADDR + HW_CLKCTRL_PIX);
	reg &= ~BM_CLKCTRL_PIX_DIV_FRAC_EN;
	__raw_writel(reg, CLKCTRL_BASE_ADDR + HW_CLKCTRL_PIX);

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
			HW_CLKCTRL_HBUS) & BM_CLKCTRL_HBUS_BUSY))
			break;
	if (!i) {
		pr_err("%s: divider writing timeout\n", __func__);
		return -ETIMEDOUT;
	}

	/* Gate off cpu clock in WFI for power saving */
	__raw_writel(BM_CLKCTRL_CPU_INTERRUPT_WAIT,
			CLKCTRL_BASE_ADDR + HW_CLKCTRL_CPU_SET);

	/*
	 * 480 MHz seems too high to be ssp clock source directly,
	 * so set frac to get a 288 MHz ref_io.
	 */
	reg = __raw_readl(CLKCTRL_BASE_ADDR + HW_CLKCTRL_FRAC);
	reg &= ~BM_CLKCTRL_FRAC_IOFRAC;
	reg |= 30 << BP_CLKCTRL_FRAC_IOFRAC;
	__raw_writel(reg, CLKCTRL_BASE_ADDR + HW_CLKCTRL_FRAC);

	return 0;
}

int __init mx23_clocks_init(void)
{
	clk_misc_init();

	/*
	 * source ssp clock from ref_io than ref_xtal,
	 * as ref_xtal only provides 24 MHz as maximum.
	 */
	clk_set_parent(&ssp_clk, &ref_io_clk);

	clk_enable(&cpu_clk);
	clk_enable(&hbus_clk);
	clk_enable(&xbus_clk);
	clk_enable(&emi_clk);
	clk_enable(&uart_clk);

	clkdev_add_table(lookups, ARRAY_SIZE(lookups));

	mxs_timer_init(&clk32k_clk, MX23_INT_TIMER0);

	return 0;
}
