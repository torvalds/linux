/*
 * Copyright (C) 2005-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dw_dmac.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/spi/spi.h>
#include <linux/usb/atmel_usba_udc.h>

#include <mach/atmel-mci.h>
#include <linux/atmel-mci.h>

#include <asm/io.h>
#include <asm/irq.h>

#include <mach/at32ap700x.h>
#include <mach/board.h>
#include <mach/hmatrix.h>
#include <mach/portmux.h>
#include <mach/sram.h>

#include <sound/atmel-abdac.h>
#include <sound/atmel-ac97c.h>

#include <video/atmel_lcdc.h>

#include "clock.h"
#include "pio.h"
#include "pm.h"


#define PBMEM(base)					\
	{						\
		.start		= base,			\
		.end		= base + 0x3ff,		\
		.flags		= IORESOURCE_MEM,	\
	}
#define IRQ(num)					\
	{						\
		.start		= num,			\
		.end		= num,			\
		.flags		= IORESOURCE_IRQ,	\
	}
#define NAMED_IRQ(num, _name)				\
	{						\
		.start		= num,			\
		.end		= num,			\
		.name		= _name,		\
		.flags		= IORESOURCE_IRQ,	\
	}

/* REVISIT these assume *every* device supports DMA, but several
 * don't ... tc, smc, pio, rtc, watchdog, pwm, ps2, and more.
 */
#define DEFINE_DEV(_name, _id)					\
static u64 _name##_id##_dma_mask = DMA_BIT_MASK(32);		\
static struct platform_device _name##_id##_device = {		\
	.name		= #_name,				\
	.id		= _id,					\
	.dev		= {					\
		.dma_mask = &_name##_id##_dma_mask,		\
		.coherent_dma_mask = DMA_BIT_MASK(32),		\
	},							\
	.resource	= _name##_id##_resource,		\
	.num_resources	= ARRAY_SIZE(_name##_id##_resource),	\
}
#define DEFINE_DEV_DATA(_name, _id)				\
static u64 _name##_id##_dma_mask = DMA_BIT_MASK(32);		\
static struct platform_device _name##_id##_device = {		\
	.name		= #_name,				\
	.id		= _id,					\
	.dev		= {					\
		.dma_mask = &_name##_id##_dma_mask,		\
		.platform_data	= &_name##_id##_data,		\
		.coherent_dma_mask = DMA_BIT_MASK(32),		\
	},							\
	.resource	= _name##_id##_resource,		\
	.num_resources	= ARRAY_SIZE(_name##_id##_resource),	\
}

#define select_peripheral(port, pin_mask, periph, flags)	\
	at32_select_periph(GPIO_##port##_BASE, pin_mask,	\
			   GPIO_##periph, flags)

#define DEV_CLK(_name, devname, bus, _index)			\
static struct clk devname##_##_name = {				\
	.name		= #_name,				\
	.dev		= &devname##_device.dev,		\
	.parent		= &bus##_clk,				\
	.mode		= bus##_clk_mode,			\
	.get_rate	= bus##_clk_get_rate,			\
	.index		= _index,				\
}

static DEFINE_SPINLOCK(pm_lock);

static struct clk osc0;
static struct clk osc1;

static unsigned long osc_get_rate(struct clk *clk)
{
	return at32_board_osc_rates[clk->index];
}

static unsigned long pll_get_rate(struct clk *clk, unsigned long control)
{
	unsigned long div, mul, rate;

	div = PM_BFEXT(PLLDIV, control) + 1;
	mul = PM_BFEXT(PLLMUL, control) + 1;

	rate = clk->parent->get_rate(clk->parent);
	rate = (rate + div / 2) / div;
	rate *= mul;

	return rate;
}

static long pll_set_rate(struct clk *clk, unsigned long rate,
			 u32 *pll_ctrl)
{
	unsigned long mul;
	unsigned long mul_best_fit = 0;
	unsigned long div;
	unsigned long div_min;
	unsigned long div_max;
	unsigned long div_best_fit = 0;
	unsigned long base;
	unsigned long pll_in;
	unsigned long actual = 0;
	unsigned long rate_error;
	unsigned long rate_error_prev = ~0UL;
	u32 ctrl;

	/* Rate must be between 80 MHz and 200 Mhz. */
	if (rate < 80000000UL || rate > 200000000UL)
		return -EINVAL;

	ctrl = PM_BF(PLLOPT, 4);
	base = clk->parent->get_rate(clk->parent);

	/* PLL input frequency must be between 6 MHz and 32 MHz. */
	div_min = DIV_ROUND_UP(base, 32000000UL);
	div_max = base / 6000000UL;

	if (div_max < div_min)
		return -EINVAL;

	for (div = div_min; div <= div_max; div++) {
		pll_in = (base + div / 2) / div;
		mul = (rate + pll_in / 2) / pll_in;

		if (mul == 0)
			continue;

		actual = pll_in * mul;
		rate_error = abs(actual - rate);

		if (rate_error < rate_error_prev) {
			mul_best_fit = mul;
			div_best_fit = div;
			rate_error_prev = rate_error;
		}

		if (rate_error == 0)
			break;
	}

	if (div_best_fit == 0)
		return -EINVAL;

	ctrl |= PM_BF(PLLMUL, mul_best_fit - 1);
	ctrl |= PM_BF(PLLDIV, div_best_fit - 1);
	ctrl |= PM_BF(PLLCOUNT, 16);

	if (clk->parent == &osc1)
		ctrl |= PM_BIT(PLLOSC);

	*pll_ctrl = ctrl;

	return actual;
}

static unsigned long pll0_get_rate(struct clk *clk)
{
	u32 control;

	control = pm_readl(PLL0);

	return pll_get_rate(clk, control);
}

static void pll1_mode(struct clk *clk, int enabled)
{
	unsigned long timeout;
	u32 status;
	u32 ctrl;

	ctrl = pm_readl(PLL1);

	if (enabled) {
		if (!PM_BFEXT(PLLMUL, ctrl) && !PM_BFEXT(PLLDIV, ctrl)) {
			pr_debug("clk %s: failed to enable, rate not set\n",
					clk->name);
			return;
		}

		ctrl |= PM_BIT(PLLEN);
		pm_writel(PLL1, ctrl);

		/* Wait for PLL lock. */
		for (timeout = 10000; timeout; timeout--) {
			status = pm_readl(ISR);
			if (status & PM_BIT(LOCK1))
				break;
			udelay(10);
		}

		if (!(status & PM_BIT(LOCK1)))
			printk(KERN_ERR "clk %s: timeout waiting for lock\n",
					clk->name);
	} else {
		ctrl &= ~PM_BIT(PLLEN);
		pm_writel(PLL1, ctrl);
	}
}

static unsigned long pll1_get_rate(struct clk *clk)
{
	u32 control;

	control = pm_readl(PLL1);

	return pll_get_rate(clk, control);
}

static long pll1_set_rate(struct clk *clk, unsigned long rate, int apply)
{
	u32 ctrl = 0;
	unsigned long actual_rate;

	actual_rate = pll_set_rate(clk, rate, &ctrl);

	if (apply) {
		if (actual_rate != rate)
			return -EINVAL;
		if (clk->users > 0)
			return -EBUSY;
		pr_debug(KERN_INFO "clk %s: new rate %lu (actual rate %lu)\n",
				clk->name, rate, actual_rate);
		pm_writel(PLL1, ctrl);
	}

	return actual_rate;
}

static int pll1_set_parent(struct clk *clk, struct clk *parent)
{
	u32 ctrl;

	if (clk->users > 0)
		return -EBUSY;

	ctrl = pm_readl(PLL1);
	WARN_ON(ctrl & PM_BIT(PLLEN));

	if (parent == &osc0)
		ctrl &= ~PM_BIT(PLLOSC);
	else if (parent == &osc1)
		ctrl |= PM_BIT(PLLOSC);
	else
		return -EINVAL;

	pm_writel(PLL1, ctrl);
	clk->parent = parent;

	return 0;
}

/*
 * The AT32AP7000 has five primary clock sources: One 32kHz
 * oscillator, two crystal oscillators and two PLLs.
 */
static struct clk osc32k = {
	.name		= "osc32k",
	.get_rate	= osc_get_rate,
	.users		= 1,
	.index		= 0,
};
static struct clk osc0 = {
	.name		= "osc0",
	.get_rate	= osc_get_rate,
	.users		= 1,
	.index		= 1,
};
static struct clk osc1 = {
	.name		= "osc1",
	.get_rate	= osc_get_rate,
	.index		= 2,
};
static struct clk pll0 = {
	.name		= "pll0",
	.get_rate	= pll0_get_rate,
	.parent		= &osc0,
};
static struct clk pll1 = {
	.name		= "pll1",
	.mode		= pll1_mode,
	.get_rate	= pll1_get_rate,
	.set_rate	= pll1_set_rate,
	.set_parent	= pll1_set_parent,
	.parent		= &osc0,
};

/*
 * The main clock can be either osc0 or pll0.  The boot loader may
 * have chosen one for us, so we don't really know which one until we
 * have a look at the SM.
 */
static struct clk *main_clock;

/*
 * Synchronous clocks are generated from the main clock. The clocks
 * must satisfy the constraint
 *   fCPU >= fHSB >= fPB
 * i.e. each clock must not be faster than its parent.
 */
static unsigned long bus_clk_get_rate(struct clk *clk, unsigned int shift)
{
	return main_clock->get_rate(main_clock) >> shift;
};

static void cpu_clk_mode(struct clk *clk, int enabled)
{
	unsigned long flags;
	u32 mask;

	spin_lock_irqsave(&pm_lock, flags);
	mask = pm_readl(CPU_MASK);
	if (enabled)
		mask |= 1 << clk->index;
	else
		mask &= ~(1 << clk->index);
	pm_writel(CPU_MASK, mask);
	spin_unlock_irqrestore(&pm_lock, flags);
}

static unsigned long cpu_clk_get_rate(struct clk *clk)
{
	unsigned long cksel, shift = 0;

	cksel = pm_readl(CKSEL);
	if (cksel & PM_BIT(CPUDIV))
		shift = PM_BFEXT(CPUSEL, cksel) + 1;

	return bus_clk_get_rate(clk, shift);
}

static long cpu_clk_set_rate(struct clk *clk, unsigned long rate, int apply)
{
	u32 control;
	unsigned long parent_rate, child_div, actual_rate, div;

	parent_rate = clk->parent->get_rate(clk->parent);
	control = pm_readl(CKSEL);

	if (control & PM_BIT(HSBDIV))
		child_div = 1 << (PM_BFEXT(HSBSEL, control) + 1);
	else
		child_div = 1;

	if (rate > 3 * (parent_rate / 4) || child_div == 1) {
		actual_rate = parent_rate;
		control &= ~PM_BIT(CPUDIV);
	} else {
		unsigned int cpusel;
		div = (parent_rate + rate / 2) / rate;
		if (div > child_div)
			div = child_div;
		cpusel = (div > 1) ? (fls(div) - 2) : 0;
		control = PM_BIT(CPUDIV) | PM_BFINS(CPUSEL, cpusel, control);
		actual_rate = parent_rate / (1 << (cpusel + 1));
	}

	pr_debug("clk %s: new rate %lu (actual rate %lu)\n",
			clk->name, rate, actual_rate);

	if (apply)
		pm_writel(CKSEL, control);

	return actual_rate;
}

static void hsb_clk_mode(struct clk *clk, int enabled)
{
	unsigned long flags;
	u32 mask;

	spin_lock_irqsave(&pm_lock, flags);
	mask = pm_readl(HSB_MASK);
	if (enabled)
		mask |= 1 << clk->index;
	else
		mask &= ~(1 << clk->index);
	pm_writel(HSB_MASK, mask);
	spin_unlock_irqrestore(&pm_lock, flags);
}

static unsigned long hsb_clk_get_rate(struct clk *clk)
{
	unsigned long cksel, shift = 0;

	cksel = pm_readl(CKSEL);
	if (cksel & PM_BIT(HSBDIV))
		shift = PM_BFEXT(HSBSEL, cksel) + 1;

	return bus_clk_get_rate(clk, shift);
}

void pba_clk_mode(struct clk *clk, int enabled)
{
	unsigned long flags;
	u32 mask;

	spin_lock_irqsave(&pm_lock, flags);
	mask = pm_readl(PBA_MASK);
	if (enabled)
		mask |= 1 << clk->index;
	else
		mask &= ~(1 << clk->index);
	pm_writel(PBA_MASK, mask);
	spin_unlock_irqrestore(&pm_lock, flags);
}

unsigned long pba_clk_get_rate(struct clk *clk)
{
	unsigned long cksel, shift = 0;

	cksel = pm_readl(CKSEL);
	if (cksel & PM_BIT(PBADIV))
		shift = PM_BFEXT(PBASEL, cksel) + 1;

	return bus_clk_get_rate(clk, shift);
}

static void pbb_clk_mode(struct clk *clk, int enabled)
{
	unsigned long flags;
	u32 mask;

	spin_lock_irqsave(&pm_lock, flags);
	mask = pm_readl(PBB_MASK);
	if (enabled)
		mask |= 1 << clk->index;
	else
		mask &= ~(1 << clk->index);
	pm_writel(PBB_MASK, mask);
	spin_unlock_irqrestore(&pm_lock, flags);
}

static unsigned long pbb_clk_get_rate(struct clk *clk)
{
	unsigned long cksel, shift = 0;

	cksel = pm_readl(CKSEL);
	if (cksel & PM_BIT(PBBDIV))
		shift = PM_BFEXT(PBBSEL, cksel) + 1;

	return bus_clk_get_rate(clk, shift);
}

static struct clk cpu_clk = {
	.name		= "cpu",
	.get_rate	= cpu_clk_get_rate,
	.set_rate	= cpu_clk_set_rate,
	.users		= 1,
};
static struct clk hsb_clk = {
	.name		= "hsb",
	.parent		= &cpu_clk,
	.get_rate	= hsb_clk_get_rate,
};
static struct clk pba_clk = {
	.name		= "pba",
	.parent		= &hsb_clk,
	.mode		= hsb_clk_mode,
	.get_rate	= pba_clk_get_rate,
	.index		= 1,
};
static struct clk pbb_clk = {
	.name		= "pbb",
	.parent		= &hsb_clk,
	.mode		= hsb_clk_mode,
	.get_rate	= pbb_clk_get_rate,
	.users		= 1,
	.index		= 2,
};

/* --------------------------------------------------------------------
 *  Generic Clock operations
 * -------------------------------------------------------------------- */

static void genclk_mode(struct clk *clk, int enabled)
{
	u32 control;

	control = pm_readl(GCCTRL(clk->index));
	if (enabled)
		control |= PM_BIT(CEN);
	else
		control &= ~PM_BIT(CEN);
	pm_writel(GCCTRL(clk->index), control);
}

static unsigned long genclk_get_rate(struct clk *clk)
{
	u32 control;
	unsigned long div = 1;

	control = pm_readl(GCCTRL(clk->index));
	if (control & PM_BIT(DIVEN))
		div = 2 * (PM_BFEXT(DIV, control) + 1);

	return clk->parent->get_rate(clk->parent) / div;
}

static long genclk_set_rate(struct clk *clk, unsigned long rate, int apply)
{
	u32 control;
	unsigned long parent_rate, actual_rate, div;

	parent_rate = clk->parent->get_rate(clk->parent);
	control = pm_readl(GCCTRL(clk->index));

	if (rate > 3 * parent_rate / 4) {
		actual_rate = parent_rate;
		control &= ~PM_BIT(DIVEN);
	} else {
		div = (parent_rate + rate) / (2 * rate) - 1;
		control = PM_BFINS(DIV, div, control) | PM_BIT(DIVEN);
		actual_rate = parent_rate / (2 * (div + 1));
	}

	dev_dbg(clk->dev, "clk %s: new rate %lu (actual rate %lu)\n",
		clk->name, rate, actual_rate);

	if (apply)
		pm_writel(GCCTRL(clk->index), control);

	return actual_rate;
}

int genclk_set_parent(struct clk *clk, struct clk *parent)
{
	u32 control;

	dev_dbg(clk->dev, "clk %s: new parent %s (was %s)\n",
		clk->name, parent->name, clk->parent->name);

	control = pm_readl(GCCTRL(clk->index));

	if (parent == &osc1 || parent == &pll1)
		control |= PM_BIT(OSCSEL);
	else if (parent == &osc0 || parent == &pll0)
		control &= ~PM_BIT(OSCSEL);
	else
		return -EINVAL;

	if (parent == &pll0 || parent == &pll1)
		control |= PM_BIT(PLLSEL);
	else
		control &= ~PM_BIT(PLLSEL);

	pm_writel(GCCTRL(clk->index), control);
	clk->parent = parent;

	return 0;
}

static void __init genclk_init_parent(struct clk *clk)
{
	u32 control;
	struct clk *parent;

	BUG_ON(clk->index > 7);

	control = pm_readl(GCCTRL(clk->index));
	if (control & PM_BIT(OSCSEL))
		parent = (control & PM_BIT(PLLSEL)) ? &pll1 : &osc1;
	else
		parent = (control & PM_BIT(PLLSEL)) ? &pll0 : &osc0;

	clk->parent = parent;
}

static struct dw_dma_platform_data dw_dmac0_data = {
	.nr_channels	= 3,
	.block_size	= 4095U,
	.nr_masters	= 2,
	.data_width	= { 2, 2, 0, 0 },
};

static struct resource dw_dmac0_resource[] = {
	PBMEM(0xff200000),
	IRQ(2),
};
DEFINE_DEV_DATA(dw_dmac, 0);
DEV_CLK(hclk, dw_dmac0, hsb, 10);

/* --------------------------------------------------------------------
 *  System peripherals
 * -------------------------------------------------------------------- */
static struct resource at32_pm0_resource[] = {
	{
		.start	= 0xfff00000,
		.end	= 0xfff0007f,
		.flags	= IORESOURCE_MEM,
	},
	IRQ(20),
};

static struct resource at32ap700x_rtc0_resource[] = {
	{
		.start	= 0xfff00080,
		.end	= 0xfff000af,
		.flags	= IORESOURCE_MEM,
	},
	IRQ(21),
};

static struct resource at32_wdt0_resource[] = {
	{
		.start	= 0xfff000b0,
		.end	= 0xfff000cf,
		.flags	= IORESOURCE_MEM,
	},
};

static struct resource at32_eic0_resource[] = {
	{
		.start	= 0xfff00100,
		.end	= 0xfff0013f,
		.flags	= IORESOURCE_MEM,
	},
	IRQ(19),
};

DEFINE_DEV(at32_pm, 0);
DEFINE_DEV(at32ap700x_rtc, 0);
DEFINE_DEV(at32_wdt, 0);
DEFINE_DEV(at32_eic, 0);

/*
 * Peripheral clock for PM, RTC, WDT and EIC. PM will ensure that this
 * is always running.
 */
static struct clk at32_pm_pclk = {
	.name		= "pclk",
	.dev		= &at32_pm0_device.dev,
	.parent		= &pbb_clk,
	.mode		= pbb_clk_mode,
	.get_rate	= pbb_clk_get_rate,
	.users		= 1,
	.index		= 0,
};

static struct resource intc0_resource[] = {
	PBMEM(0xfff00400),
};
struct platform_device at32_intc0_device = {
	.name		= "intc",
	.id		= 0,
	.resource	= intc0_resource,
	.num_resources	= ARRAY_SIZE(intc0_resource),
};
DEV_CLK(pclk, at32_intc0, pbb, 1);

static struct clk ebi_clk = {
	.name		= "ebi",
	.parent		= &hsb_clk,
	.mode		= hsb_clk_mode,
	.get_rate	= hsb_clk_get_rate,
	.users		= 1,
};
static struct clk hramc_clk = {
	.name		= "hramc",
	.parent		= &hsb_clk,
	.mode		= hsb_clk_mode,
	.get_rate	= hsb_clk_get_rate,
	.users		= 1,
	.index		= 3,
};
static struct clk sdramc_clk = {
	.name		= "sdramc_clk",
	.parent		= &pbb_clk,
	.mode		= pbb_clk_mode,
	.get_rate	= pbb_clk_get_rate,
	.users		= 1,
	.index		= 14,
};

static struct resource smc0_resource[] = {
	PBMEM(0xfff03400),
};
DEFINE_DEV(smc, 0);
DEV_CLK(pclk, smc0, pbb, 13);
DEV_CLK(mck, smc0, hsb, 0);

static struct platform_device pdc_device = {
	.name		= "pdc",
	.id		= 0,
};
DEV_CLK(hclk, pdc, hsb, 4);
DEV_CLK(pclk, pdc, pba, 16);

static struct clk pico_clk = {
	.name		= "pico",
	.parent		= &cpu_clk,
	.mode		= cpu_clk_mode,
	.get_rate	= cpu_clk_get_rate,
	.users		= 1,
};

/* --------------------------------------------------------------------
 * HMATRIX
 * -------------------------------------------------------------------- */

struct clk at32_hmatrix_clk = {
	.name		= "hmatrix_clk",
	.parent		= &pbb_clk,
	.mode		= pbb_clk_mode,
	.get_rate	= pbb_clk_get_rate,
	.index		= 2,
	.users		= 1,
};

/*
 * Set bits in the HMATRIX Special Function Register (SFR) used by the
 * External Bus Interface (EBI). This can be used to enable special
 * features like CompactFlash support, NAND Flash support, etc. on
 * certain chipselects.
 */
static inline void set_ebi_sfr_bits(u32 mask)
{
	hmatrix_sfr_set_bits(HMATRIX_SLAVE_EBI, mask);
}

/* --------------------------------------------------------------------
 *  Timer/Counter (TC)
 * -------------------------------------------------------------------- */

static struct resource at32_tcb0_resource[] = {
	PBMEM(0xfff00c00),
	IRQ(22),
};
static struct platform_device at32_tcb0_device = {
	.name		= "atmel_tcb",
	.id		= 0,
	.resource	= at32_tcb0_resource,
	.num_resources	= ARRAY_SIZE(at32_tcb0_resource),
};
DEV_CLK(t0_clk, at32_tcb0, pbb, 3);

static struct resource at32_tcb1_resource[] = {
	PBMEM(0xfff01000),
	IRQ(23),
};
static struct platform_device at32_tcb1_device = {
	.name		= "atmel_tcb",
	.id		= 1,
	.resource	= at32_tcb1_resource,
	.num_resources	= ARRAY_SIZE(at32_tcb1_resource),
};
DEV_CLK(t0_clk, at32_tcb1, pbb, 4);

/* --------------------------------------------------------------------
 *  PIO
 * -------------------------------------------------------------------- */

static struct resource pio0_resource[] = {
	PBMEM(0xffe02800),
	IRQ(13),
};
DEFINE_DEV(pio, 0);
DEV_CLK(mck, pio0, pba, 10);

static struct resource pio1_resource[] = {
	PBMEM(0xffe02c00),
	IRQ(14),
};
DEFINE_DEV(pio, 1);
DEV_CLK(mck, pio1, pba, 11);

static struct resource pio2_resource[] = {
	PBMEM(0xffe03000),
	IRQ(15),
};
DEFINE_DEV(pio, 2);
DEV_CLK(mck, pio2, pba, 12);

static struct resource pio3_resource[] = {
	PBMEM(0xffe03400),
	IRQ(16),
};
DEFINE_DEV(pio, 3);
DEV_CLK(mck, pio3, pba, 13);

static struct resource pio4_resource[] = {
	PBMEM(0xffe03800),
	IRQ(17),
};
DEFINE_DEV(pio, 4);
DEV_CLK(mck, pio4, pba, 14);

static int __init system_device_init(void)
{
	platform_device_register(&at32_pm0_device);
	platform_device_register(&at32_intc0_device);
	platform_device_register(&at32ap700x_rtc0_device);
	platform_device_register(&at32_wdt0_device);
	platform_device_register(&at32_eic0_device);
	platform_device_register(&smc0_device);
	platform_device_register(&pdc_device);
	platform_device_register(&dw_dmac0_device);

	platform_device_register(&at32_tcb0_device);
	platform_device_register(&at32_tcb1_device);

	platform_device_register(&pio0_device);
	platform_device_register(&pio1_device);
	platform_device_register(&pio2_device);
	platform_device_register(&pio3_device);
	platform_device_register(&pio4_device);

	return 0;
}
core_initcall(system_device_init);

/* --------------------------------------------------------------------
 *  PSIF
 * -------------------------------------------------------------------- */
static struct resource atmel_psif0_resource[] __initdata = {
	{
		.start	= 0xffe03c00,
		.end	= 0xffe03cff,
		.flags	= IORESOURCE_MEM,
	},
	IRQ(18),
};
static struct clk atmel_psif0_pclk = {
	.name		= "pclk",
	.parent		= &pba_clk,
	.mode		= pba_clk_mode,
	.get_rate	= pba_clk_get_rate,
	.index		= 15,
};

static struct resource atmel_psif1_resource[] __initdata = {
	{
		.start	= 0xffe03d00,
		.end	= 0xffe03dff,
		.flags	= IORESOURCE_MEM,
	},
	IRQ(18),
};
static struct clk atmel_psif1_pclk = {
	.name		= "pclk",
	.parent		= &pba_clk,
	.mode		= pba_clk_mode,
	.get_rate	= pba_clk_get_rate,
	.index		= 15,
};

struct platform_device *__init at32_add_device_psif(unsigned int id)
{
	struct platform_device *pdev;
	u32 pin_mask;

	if (!(id == 0 || id == 1))
		return NULL;

	pdev = platform_device_alloc("atmel_psif", id);
	if (!pdev)
		return NULL;

	switch (id) {
	case 0:
		pin_mask  = (1 << 8) | (1 << 9); /* CLOCK & DATA */

		if (platform_device_add_resources(pdev, atmel_psif0_resource,
					ARRAY_SIZE(atmel_psif0_resource)))
			goto err_add_resources;
		atmel_psif0_pclk.dev = &pdev->dev;
		select_peripheral(PIOA, pin_mask, PERIPH_A, 0);
		break;
	case 1:
		pin_mask  = (1 << 11) | (1 << 12); /* CLOCK & DATA */

		if (platform_device_add_resources(pdev, atmel_psif1_resource,
					ARRAY_SIZE(atmel_psif1_resource)))
			goto err_add_resources;
		atmel_psif1_pclk.dev = &pdev->dev;
		select_peripheral(PIOB, pin_mask, PERIPH_A, 0);
		break;
	default:
		return NULL;
	}

	platform_device_add(pdev);
	return pdev;

err_add_resources:
	platform_device_put(pdev);
	return NULL;
}

/* --------------------------------------------------------------------
 *  USART
 * -------------------------------------------------------------------- */

static struct atmel_uart_data atmel_usart0_data = {
	.use_dma_tx	= 1,
	.use_dma_rx	= 1,
};
static struct resource atmel_usart0_resource[] = {
	PBMEM(0xffe00c00),
	IRQ(6),
};
DEFINE_DEV_DATA(atmel_usart, 0);
DEV_CLK(usart, atmel_usart0, pba, 3);

static struct atmel_uart_data atmel_usart1_data = {
	.use_dma_tx	= 1,
	.use_dma_rx	= 1,
};
static struct resource atmel_usart1_resource[] = {
	PBMEM(0xffe01000),
	IRQ(7),
};
DEFINE_DEV_DATA(atmel_usart, 1);
DEV_CLK(usart, atmel_usart1, pba, 4);

static struct atmel_uart_data atmel_usart2_data = {
	.use_dma_tx	= 1,
	.use_dma_rx	= 1,
};
static struct resource atmel_usart2_resource[] = {
	PBMEM(0xffe01400),
	IRQ(8),
};
DEFINE_DEV_DATA(atmel_usart, 2);
DEV_CLK(usart, atmel_usart2, pba, 5);

static struct atmel_uart_data atmel_usart3_data = {
	.use_dma_tx	= 1,
	.use_dma_rx	= 1,
};
static struct resource atmel_usart3_resource[] = {
	PBMEM(0xffe01800),
	IRQ(9),
};
DEFINE_DEV_DATA(atmel_usart, 3);
DEV_CLK(usart, atmel_usart3, pba, 6);

static inline void configure_usart0_pins(int flags)
{
	u32 pin_mask = (1 << 8) | (1 << 9); /* RXD & TXD */
	if (flags & ATMEL_USART_RTS)	pin_mask |= (1 << 6);
	if (flags & ATMEL_USART_CTS)	pin_mask |= (1 << 7);
	if (flags & ATMEL_USART_CLK)	pin_mask |= (1 << 10);

	select_peripheral(PIOA, pin_mask, PERIPH_B, AT32_GPIOF_PULLUP);
}

static inline void configure_usart1_pins(int flags)
{
	u32 pin_mask = (1 << 17) | (1 << 18); /* RXD & TXD */
	if (flags & ATMEL_USART_RTS)	pin_mask |= (1 << 19);
	if (flags & ATMEL_USART_CTS)	pin_mask |= (1 << 20);
	if (flags & ATMEL_USART_CLK)	pin_mask |= (1 << 16);

	select_peripheral(PIOA, pin_mask, PERIPH_A, AT32_GPIOF_PULLUP);
}

static inline void configure_usart2_pins(int flags)
{
	u32 pin_mask = (1 << 26) | (1 << 27); /* RXD & TXD */
	if (flags & ATMEL_USART_RTS)	pin_mask |= (1 << 30);
	if (flags & ATMEL_USART_CTS)	pin_mask |= (1 << 29);
	if (flags & ATMEL_USART_CLK)	pin_mask |= (1 << 28);

	select_peripheral(PIOB, pin_mask, PERIPH_B, AT32_GPIOF_PULLUP);
}

static inline void configure_usart3_pins(int flags)
{
	u32 pin_mask = (1 << 18) | (1 << 17); /* RXD & TXD */
	if (flags & ATMEL_USART_RTS)	pin_mask |= (1 << 16);
	if (flags & ATMEL_USART_CTS)	pin_mask |= (1 << 15);
	if (flags & ATMEL_USART_CLK)	pin_mask |= (1 << 19);

	select_peripheral(PIOB, pin_mask, PERIPH_B, AT32_GPIOF_PULLUP);
}

static struct platform_device *__initdata at32_usarts[4];

void __init at32_map_usart(unsigned int hw_id, unsigned int line, int flags)
{
	struct platform_device *pdev;
	struct atmel_uart_data *pdata;

	switch (hw_id) {
	case 0:
		pdev = &atmel_usart0_device;
		configure_usart0_pins(flags);
		break;
	case 1:
		pdev = &atmel_usart1_device;
		configure_usart1_pins(flags);
		break;
	case 2:
		pdev = &atmel_usart2_device;
		configure_usart2_pins(flags);
		break;
	case 3:
		pdev = &atmel_usart3_device;
		configure_usart3_pins(flags);
		break;
	default:
		return;
	}

	if (PXSEG(pdev->resource[0].start) == P4SEG) {
		/* Addresses in the P4 segment are permanently mapped 1:1 */
		struct atmel_uart_data *data = pdev->dev.platform_data;
		data->regs = (void __iomem *)pdev->resource[0].start;
	}

	pdev->id = line;
	pdata = pdev->dev.platform_data;
	pdata->num = line;
	at32_usarts[line] = pdev;
}

struct platform_device *__init at32_add_device_usart(unsigned int id)
{
	platform_device_register(at32_usarts[id]);
	return at32_usarts[id];
}

void __init at32_setup_serial_console(unsigned int usart_id)
{
#ifdef CONFIG_SERIAL_ATMEL
	atmel_default_console_device = at32_usarts[usart_id];
#endif
}

/* --------------------------------------------------------------------
 *  Ethernet
 * -------------------------------------------------------------------- */

#ifdef CONFIG_CPU_AT32AP7000
static struct macb_platform_data macb0_data;
static struct resource macb0_resource[] = {
	PBMEM(0xfff01800),
	IRQ(25),
};
DEFINE_DEV_DATA(macb, 0);
DEV_CLK(hclk, macb0, hsb, 8);
DEV_CLK(pclk, macb0, pbb, 6);

static struct macb_platform_data macb1_data;
static struct resource macb1_resource[] = {
	PBMEM(0xfff01c00),
	IRQ(26),
};
DEFINE_DEV_DATA(macb, 1);
DEV_CLK(hclk, macb1, hsb, 9);
DEV_CLK(pclk, macb1, pbb, 7);

struct platform_device *__init
at32_add_device_eth(unsigned int id, struct macb_platform_data *data)
{
	struct platform_device *pdev;
	u32 pin_mask;

	switch (id) {
	case 0:
		pdev = &macb0_device;

		pin_mask  = (1 << 3);	/* TXD0 */
		pin_mask |= (1 << 4);	/* TXD1 */
		pin_mask |= (1 << 7);	/* TXEN */
		pin_mask |= (1 << 8);	/* TXCK */
		pin_mask |= (1 << 9);	/* RXD0 */
		pin_mask |= (1 << 10);	/* RXD1 */
		pin_mask |= (1 << 13);	/* RXER */
		pin_mask |= (1 << 15);	/* RXDV */
		pin_mask |= (1 << 16);	/* MDC  */
		pin_mask |= (1 << 17);	/* MDIO */

		if (!data->is_rmii) {
			pin_mask |= (1 << 0);	/* COL  */
			pin_mask |= (1 << 1);	/* CRS  */
			pin_mask |= (1 << 2);	/* TXER */
			pin_mask |= (1 << 5);	/* TXD2 */
			pin_mask |= (1 << 6);	/* TXD3 */
			pin_mask |= (1 << 11);	/* RXD2 */
			pin_mask |= (1 << 12);	/* RXD3 */
			pin_mask |= (1 << 14);	/* RXCK */
#ifndef CONFIG_BOARD_MIMC200
			pin_mask |= (1 << 18);	/* SPD  */
#endif
		}

		select_peripheral(PIOC, pin_mask, PERIPH_A, 0);

		break;

	case 1:
		pdev = &macb1_device;

		pin_mask  = (1 << 13);	/* TXD0 */
		pin_mask |= (1 << 14);	/* TXD1 */
		pin_mask |= (1 << 11);	/* TXEN */
		pin_mask |= (1 << 12);	/* TXCK */
		pin_mask |= (1 << 10);	/* RXD0 */
		pin_mask |= (1 << 6);	/* RXD1 */
		pin_mask |= (1 << 5);	/* RXER */
		pin_mask |= (1 << 4);	/* RXDV */
		pin_mask |= (1 << 3);	/* MDC  */
		pin_mask |= (1 << 2);	/* MDIO */

#ifndef CONFIG_BOARD_MIMC200
		if (!data->is_rmii)
			pin_mask |= (1 << 15);	/* SPD  */
#endif

		select_peripheral(PIOD, pin_mask, PERIPH_B, 0);

		if (!data->is_rmii) {
			pin_mask  = (1 << 19);	/* COL  */
			pin_mask |= (1 << 23);	/* CRS  */
			pin_mask |= (1 << 26);	/* TXER */
			pin_mask |= (1 << 27);	/* TXD2 */
			pin_mask |= (1 << 28);	/* TXD3 */
			pin_mask |= (1 << 29);	/* RXD2 */
			pin_mask |= (1 << 30);	/* RXD3 */
			pin_mask |= (1 << 24);	/* RXCK */

			select_peripheral(PIOC, pin_mask, PERIPH_B, 0);
		}
		break;

	default:
		return NULL;
	}

	memcpy(pdev->dev.platform_data, data, sizeof(struct macb_platform_data));
	platform_device_register(pdev);

	return pdev;
}
#endif

/* --------------------------------------------------------------------
 *  SPI
 * -------------------------------------------------------------------- */
static struct resource atmel_spi0_resource[] = {
	PBMEM(0xffe00000),
	IRQ(3),
};
DEFINE_DEV(atmel_spi, 0);
DEV_CLK(spi_clk, atmel_spi0, pba, 0);

static struct resource atmel_spi1_resource[] = {
	PBMEM(0xffe00400),
	IRQ(4),
};
DEFINE_DEV(atmel_spi, 1);
DEV_CLK(spi_clk, atmel_spi1, pba, 1);

void __init
at32_spi_setup_slaves(unsigned int bus_num, struct spi_board_info *b, unsigned int n)
{
	/*
	 * Manage the chipselects as GPIOs, normally using the same pins
	 * the SPI controller expects; but boards can use other pins.
	 */
	static u8 __initdata spi_pins[][4] = {
		{ GPIO_PIN_PA(3), GPIO_PIN_PA(4),
		  GPIO_PIN_PA(5), GPIO_PIN_PA(20) },
		{ GPIO_PIN_PB(2), GPIO_PIN_PB(3),
		  GPIO_PIN_PB(4), GPIO_PIN_PA(27) },
	};
	unsigned int pin, mode;

	/* There are only 2 SPI controllers */
	if (bus_num > 1)
		return;

	for (; n; n--, b++) {
		b->bus_num = bus_num;
		if (b->chip_select >= 4)
			continue;
		pin = (unsigned)b->controller_data;
		if (!pin) {
			pin = spi_pins[bus_num][b->chip_select];
			b->controller_data = (void *)pin;
		}
		mode = AT32_GPIOF_OUTPUT;
		if (!(b->mode & SPI_CS_HIGH))
			mode |= AT32_GPIOF_HIGH;
		at32_select_gpio(pin, mode);
	}
}

struct platform_device *__init
at32_add_device_spi(unsigned int id, struct spi_board_info *b, unsigned int n)
{
	struct platform_device *pdev;
	u32 pin_mask;

	switch (id) {
	case 0:
		pdev = &atmel_spi0_device;
		pin_mask  = (1 << 1) | (1 << 2);	/* MOSI & SCK */

		/* pullup MISO so a level is always defined */
		select_peripheral(PIOA, (1 << 0), PERIPH_A, AT32_GPIOF_PULLUP);
		select_peripheral(PIOA, pin_mask, PERIPH_A, 0);

		at32_spi_setup_slaves(0, b, n);
		break;

	case 1:
		pdev = &atmel_spi1_device;
		pin_mask  = (1 << 1) | (1 << 5);	/* MOSI */

		/* pullup MISO so a level is always defined */
		select_peripheral(PIOB, (1 << 0), PERIPH_B, AT32_GPIOF_PULLUP);
		select_peripheral(PIOB, pin_mask, PERIPH_B, 0);

		at32_spi_setup_slaves(1, b, n);
		break;

	default:
		return NULL;
	}

	spi_register_board_info(b, n);
	platform_device_register(pdev);
	return pdev;
}

/* --------------------------------------------------------------------
 *  TWI
 * -------------------------------------------------------------------- */
static struct resource atmel_twi0_resource[] __initdata = {
	PBMEM(0xffe00800),
	IRQ(5),
};
static struct clk atmel_twi0_pclk = {
	.name		= "twi_pclk",
	.parent		= &pba_clk,
	.mode		= pba_clk_mode,
	.get_rate	= pba_clk_get_rate,
	.index		= 2,
};

struct platform_device *__init at32_add_device_twi(unsigned int id,
						    struct i2c_board_info *b,
						    unsigned int n)
{
	struct platform_device *pdev;
	u32 pin_mask;

	if (id != 0)
		return NULL;

	pdev = platform_device_alloc("atmel_twi", id);
	if (!pdev)
		return NULL;

	if (platform_device_add_resources(pdev, atmel_twi0_resource,
				ARRAY_SIZE(atmel_twi0_resource)))
		goto err_add_resources;

	pin_mask  = (1 << 6) | (1 << 7);	/* SDA & SDL */

	select_peripheral(PIOA, pin_mask, PERIPH_A, 0);

	atmel_twi0_pclk.dev = &pdev->dev;

	if (b)
		i2c_register_board_info(id, b, n);

	platform_device_add(pdev);
	return pdev;

err_add_resources:
	platform_device_put(pdev);
	return NULL;
}

/* --------------------------------------------------------------------
 * MMC
 * -------------------------------------------------------------------- */
static struct resource atmel_mci0_resource[] __initdata = {
	PBMEM(0xfff02400),
	IRQ(28),
};
static struct clk atmel_mci0_pclk = {
	.name		= "mci_clk",
	.parent		= &pbb_clk,
	.mode		= pbb_clk_mode,
	.get_rate	= pbb_clk_get_rate,
	.index		= 9,
};

struct platform_device *__init
at32_add_device_mci(unsigned int id, struct mci_platform_data *data)
{
	struct platform_device		*pdev;
	struct mci_dma_data	        *slave;
	u32				pioa_mask;
	u32				piob_mask;

	if (id != 0 || !data)
		return NULL;

	/* Must have at least one usable slot */
	if (!data->slot[0].bus_width && !data->slot[1].bus_width)
		return NULL;

	pdev = platform_device_alloc("atmel_mci", id);
	if (!pdev)
		goto fail;

	if (platform_device_add_resources(pdev, atmel_mci0_resource,
				ARRAY_SIZE(atmel_mci0_resource)))
		goto fail;

	slave = kzalloc(sizeof(struct mci_dma_data), GFP_KERNEL);
	if (!slave)
		goto fail;

	slave->sdata.dma_dev = &dw_dmac0_device.dev;
	slave->sdata.cfg_hi = (DWC_CFGH_SRC_PER(0)
				| DWC_CFGH_DST_PER(1));
	slave->sdata.cfg_lo &= ~(DWC_CFGL_HS_DST_POL
				| DWC_CFGL_HS_SRC_POL);

	data->dma_slave = slave;

	if (platform_device_add_data(pdev, data,
				sizeof(struct mci_platform_data)))
		goto fail_free;

	/* CLK line is common to both slots */
	pioa_mask = 1 << 10;

	switch (data->slot[0].bus_width) {
	case 4:
		pioa_mask |= 1 << 13;		/* DATA1 */
		pioa_mask |= 1 << 14;		/* DATA2 */
		pioa_mask |= 1 << 15;		/* DATA3 */
		/* fall through */
	case 1:
		pioa_mask |= 1 << 11;		/* CMD	 */
		pioa_mask |= 1 << 12;		/* DATA0 */

		if (gpio_is_valid(data->slot[0].detect_pin))
			at32_select_gpio(data->slot[0].detect_pin, 0);
		if (gpio_is_valid(data->slot[0].wp_pin))
			at32_select_gpio(data->slot[0].wp_pin, 0);
		break;
	case 0:
		/* Slot is unused */
		break;
	default:
		goto fail_free;
	}

	select_peripheral(PIOA, pioa_mask, PERIPH_A, 0);
	piob_mask = 0;

	switch (data->slot[1].bus_width) {
	case 4:
		piob_mask |= 1 <<  8;		/* DATA1 */
		piob_mask |= 1 <<  9;		/* DATA2 */
		piob_mask |= 1 << 10;		/* DATA3 */
		/* fall through */
	case 1:
		piob_mask |= 1 <<  6;		/* CMD	 */
		piob_mask |= 1 <<  7;		/* DATA0 */
		select_peripheral(PIOB, piob_mask, PERIPH_B, 0);

		if (gpio_is_valid(data->slot[1].detect_pin))
			at32_select_gpio(data->slot[1].detect_pin, 0);
		if (gpio_is_valid(data->slot[1].wp_pin))
			at32_select_gpio(data->slot[1].wp_pin, 0);
		break;
	case 0:
		/* Slot is unused */
		break;
	default:
		if (!data->slot[0].bus_width)
			goto fail_free;

		data->slot[1].bus_width = 0;
		break;
	}

	atmel_mci0_pclk.dev = &pdev->dev;

	platform_device_add(pdev);
	return pdev;

fail_free:
	kfree(slave);
fail:
	data->dma_slave = NULL;
	platform_device_put(pdev);
	return NULL;
}

/* --------------------------------------------------------------------
 *  LCDC
 * -------------------------------------------------------------------- */
#if defined(CONFIG_CPU_AT32AP7000) || defined(CONFIG_CPU_AT32AP7002)
static struct atmel_lcdfb_info atmel_lcdfb0_data;
static struct resource atmel_lcdfb0_resource[] = {
	{
		.start		= 0xff000000,
		.end		= 0xff000fff,
		.flags		= IORESOURCE_MEM,
	},
	IRQ(1),
	{
		/* Placeholder for pre-allocated fb memory */
		.start		= 0x00000000,
		.end		= 0x00000000,
		.flags		= 0,
	},
};
DEFINE_DEV_DATA(atmel_lcdfb, 0);
DEV_CLK(hclk, atmel_lcdfb0, hsb, 7);
static struct clk atmel_lcdfb0_pixclk = {
	.name		= "lcdc_clk",
	.dev		= &atmel_lcdfb0_device.dev,
	.mode		= genclk_mode,
	.get_rate	= genclk_get_rate,
	.set_rate	= genclk_set_rate,
	.set_parent	= genclk_set_parent,
	.index		= 7,
};

struct platform_device *__init
at32_add_device_lcdc(unsigned int id, struct atmel_lcdfb_info *data,
		     unsigned long fbmem_start, unsigned long fbmem_len,
		     u64 pin_mask)
{
	struct platform_device *pdev;
	struct atmel_lcdfb_info *info;
	struct fb_monspecs *monspecs;
	struct fb_videomode *modedb;
	unsigned int modedb_size;
	u32 portc_mask, portd_mask, porte_mask;

	/*
	 * Do a deep copy of the fb data, monspecs and modedb. Make
	 * sure all allocations are done before setting up the
	 * portmux.
	 */
	monspecs = kmemdup(data->default_monspecs,
			   sizeof(struct fb_monspecs), GFP_KERNEL);
	if (!monspecs)
		return NULL;

	modedb_size = sizeof(struct fb_videomode) * monspecs->modedb_len;
	modedb = kmemdup(monspecs->modedb, modedb_size, GFP_KERNEL);
	if (!modedb)
		goto err_dup_modedb;
	monspecs->modedb = modedb;

	switch (id) {
	case 0:
		pdev = &atmel_lcdfb0_device;

		if (pin_mask == 0ULL)
			/* Default to "full" lcdc control signals and 24bit */
			pin_mask = ATMEL_LCDC_PRI_24BIT | ATMEL_LCDC_PRI_CONTROL;

		/* LCDC on port C */
		portc_mask = pin_mask & 0xfff80000;
		select_peripheral(PIOC, portc_mask, PERIPH_A, 0);

		/* LCDC on port D */
		portd_mask = pin_mask & 0x0003ffff;
		select_peripheral(PIOD, portd_mask, PERIPH_A, 0);

		/* LCDC on port E */
		porte_mask = (pin_mask >> 32) & 0x0007ffff;
		select_peripheral(PIOE, porte_mask, PERIPH_B, 0);

		clk_set_parent(&atmel_lcdfb0_pixclk, &pll0);
		clk_set_rate(&atmel_lcdfb0_pixclk, clk_get_rate(&pll0));
		break;

	default:
		goto err_invalid_id;
	}

	if (fbmem_len) {
		pdev->resource[2].start = fbmem_start;
		pdev->resource[2].end = fbmem_start + fbmem_len - 1;
		pdev->resource[2].flags = IORESOURCE_MEM;
	}

	info = pdev->dev.platform_data;
	memcpy(info, data, sizeof(struct atmel_lcdfb_info));
	info->default_monspecs = monspecs;

	pdev->name = "at32ap-lcdfb";

	platform_device_register(pdev);
	return pdev;

err_invalid_id:
	kfree(modedb);
err_dup_modedb:
	kfree(monspecs);
	return NULL;
}
#endif

/* --------------------------------------------------------------------
 *  PWM
 * -------------------------------------------------------------------- */
static struct resource atmel_pwm0_resource[] __initdata = {
	PBMEM(0xfff01400),
	IRQ(24),
};
static struct clk atmel_pwm0_mck = {
	.name		= "pwm_clk",
	.parent		= &pbb_clk,
	.mode		= pbb_clk_mode,
	.get_rate	= pbb_clk_get_rate,
	.index		= 5,
};

struct platform_device *__init at32_add_device_pwm(u32 mask)
{
	struct platform_device *pdev;
	u32 pin_mask;

	if (!mask)
		return NULL;

	pdev = platform_device_alloc("atmel_pwm", 0);
	if (!pdev)
		return NULL;

	if (platform_device_add_resources(pdev, atmel_pwm0_resource,
				ARRAY_SIZE(atmel_pwm0_resource)))
		goto out_free_pdev;

	if (platform_device_add_data(pdev, &mask, sizeof(mask)))
		goto out_free_pdev;

	pin_mask = 0;
	if (mask & (1 << 0))
		pin_mask |= (1 << 28);
	if (mask & (1 << 1))
		pin_mask |= (1 << 29);
	if (pin_mask > 0)
		select_peripheral(PIOA, pin_mask, PERIPH_A, 0);

	pin_mask = 0;
	if (mask & (1 << 2))
		pin_mask |= (1 << 21);
	if (mask & (1 << 3))
		pin_mask |= (1 << 22);
	if (pin_mask > 0)
		select_peripheral(PIOA, pin_mask, PERIPH_B, 0);

	atmel_pwm0_mck.dev = &pdev->dev;

	platform_device_add(pdev);

	return pdev;

out_free_pdev:
	platform_device_put(pdev);
	return NULL;
}

/* --------------------------------------------------------------------
 *  SSC
 * -------------------------------------------------------------------- */
static struct resource ssc0_resource[] = {
	PBMEM(0xffe01c00),
	IRQ(10),
};
DEFINE_DEV(ssc, 0);
DEV_CLK(pclk, ssc0, pba, 7);

static struct resource ssc1_resource[] = {
	PBMEM(0xffe02000),
	IRQ(11),
};
DEFINE_DEV(ssc, 1);
DEV_CLK(pclk, ssc1, pba, 8);

static struct resource ssc2_resource[] = {
	PBMEM(0xffe02400),
	IRQ(12),
};
DEFINE_DEV(ssc, 2);
DEV_CLK(pclk, ssc2, pba, 9);

struct platform_device *__init
at32_add_device_ssc(unsigned int id, unsigned int flags)
{
	struct platform_device *pdev;
	u32 pin_mask = 0;

	switch (id) {
	case 0:
		pdev = &ssc0_device;
		if (flags & ATMEL_SSC_RF)
			pin_mask |= (1 << 21);	/* RF */
		if (flags & ATMEL_SSC_RK)
			pin_mask |= (1 << 22);	/* RK */
		if (flags & ATMEL_SSC_TK)
			pin_mask |= (1 << 23);	/* TK */
		if (flags & ATMEL_SSC_TF)
			pin_mask |= (1 << 24);	/* TF */
		if (flags & ATMEL_SSC_TD)
			pin_mask |= (1 << 25);	/* TD */
		if (flags & ATMEL_SSC_RD)
			pin_mask |= (1 << 26);	/* RD */

		if (pin_mask > 0)
			select_peripheral(PIOA, pin_mask, PERIPH_A, 0);

		break;
	case 1:
		pdev = &ssc1_device;
		if (flags & ATMEL_SSC_RF)
			pin_mask |= (1 << 0);	/* RF */
		if (flags & ATMEL_SSC_RK)
			pin_mask |= (1 << 1);	/* RK */
		if (flags & ATMEL_SSC_TK)
			pin_mask |= (1 << 2);	/* TK */
		if (flags & ATMEL_SSC_TF)
			pin_mask |= (1 << 3);	/* TF */
		if (flags & ATMEL_SSC_TD)
			pin_mask |= (1 << 4);	/* TD */
		if (flags & ATMEL_SSC_RD)
			pin_mask |= (1 << 5);	/* RD */

		if (pin_mask > 0)
			select_peripheral(PIOA, pin_mask, PERIPH_B, 0);

		break;
	case 2:
		pdev = &ssc2_device;
		if (flags & ATMEL_SSC_TD)
			pin_mask |= (1 << 13);	/* TD */
		if (flags & ATMEL_SSC_RD)
			pin_mask |= (1 << 14);	/* RD */
		if (flags & ATMEL_SSC_TK)
			pin_mask |= (1 << 15);	/* TK */
		if (flags & ATMEL_SSC_TF)
			pin_mask |= (1 << 16);	/* TF */
		if (flags & ATMEL_SSC_RF)
			pin_mask |= (1 << 17);	/* RF */
		if (flags & ATMEL_SSC_RK)
			pin_mask |= (1 << 18);	/* RK */

		if (pin_mask > 0)
			select_peripheral(PIOB, pin_mask, PERIPH_A, 0);

		break;
	default:
		return NULL;
	}

	platform_device_register(pdev);
	return pdev;
}

/* --------------------------------------------------------------------
 *  USB Device Controller
 * -------------------------------------------------------------------- */
static struct resource usba0_resource[] __initdata = {
	{
		.start		= 0xff300000,
		.end		= 0xff3fffff,
		.flags		= IORESOURCE_MEM,
	}, {
		.start		= 0xfff03000,
		.end		= 0xfff033ff,
		.flags		= IORESOURCE_MEM,
	},
	IRQ(31),
};
static struct clk usba0_pclk = {
	.name		= "pclk",
	.parent		= &pbb_clk,
	.mode		= pbb_clk_mode,
	.get_rate	= pbb_clk_get_rate,
	.index		= 12,
};
static struct clk usba0_hclk = {
	.name		= "hclk",
	.parent		= &hsb_clk,
	.mode		= hsb_clk_mode,
	.get_rate	= hsb_clk_get_rate,
	.index		= 6,
};

#define EP(nam, idx, maxpkt, maxbk, dma, isoc)			\
	[idx] = {						\
		.name		= nam,				\
		.index		= idx,				\
		.fifo_size	= maxpkt,			\
		.nr_banks	= maxbk,			\
		.can_dma	= dma,				\
		.can_isoc	= isoc,				\
	}

static struct usba_ep_data at32_usba_ep[] __initdata = {
	EP("ep0",     0,   64, 1, 0, 0),
	EP("ep1",     1,  512, 2, 1, 1),
	EP("ep2",     2,  512, 2, 1, 1),
	EP("ep3-int", 3,   64, 3, 1, 0),
	EP("ep4-int", 4,   64, 3, 1, 0),
	EP("ep5",     5, 1024, 3, 1, 1),
	EP("ep6",     6, 1024, 3, 1, 1),
};

#undef EP

struct platform_device *__init
at32_add_device_usba(unsigned int id, struct usba_platform_data *data)
{
	/*
	 * pdata doesn't have room for any endpoints, so we need to
	 * append room for the ones we need right after it.
	 */
	struct {
		struct usba_platform_data pdata;
		struct usba_ep_data ep[7];
	} usba_data;
	struct platform_device *pdev;

	if (id != 0)
		return NULL;

	pdev = platform_device_alloc("atmel_usba_udc", 0);
	if (!pdev)
		return NULL;

	if (platform_device_add_resources(pdev, usba0_resource,
					  ARRAY_SIZE(usba0_resource)))
		goto out_free_pdev;

	if (data) {
		usba_data.pdata.vbus_pin = data->vbus_pin;
		usba_data.pdata.vbus_pin_inverted = data->vbus_pin_inverted;
	} else {
		usba_data.pdata.vbus_pin = -EINVAL;
		usba_data.pdata.vbus_pin_inverted = -EINVAL;
	}

	data = &usba_data.pdata;
	data->num_ep = ARRAY_SIZE(at32_usba_ep);
	memcpy(data->ep, at32_usba_ep, sizeof(at32_usba_ep));

	if (platform_device_add_data(pdev, data, sizeof(usba_data)))
		goto out_free_pdev;

	if (gpio_is_valid(data->vbus_pin))
		at32_select_gpio(data->vbus_pin, 0);

	usba0_pclk.dev = &pdev->dev;
	usba0_hclk.dev = &pdev->dev;

	platform_device_add(pdev);

	return pdev;

out_free_pdev:
	platform_device_put(pdev);
	return NULL;
}

/* --------------------------------------------------------------------
 * IDE / CompactFlash
 * -------------------------------------------------------------------- */
#if defined(CONFIG_CPU_AT32AP7000) || defined(CONFIG_CPU_AT32AP7001)
static struct resource at32_smc_cs4_resource[] __initdata = {
	{
		.start	= 0x04000000,
		.end	= 0x07ffffff,
		.flags	= IORESOURCE_MEM,
	},
	IRQ(~0UL), /* Magic IRQ will be overridden */
};
static struct resource at32_smc_cs5_resource[] __initdata = {
	{
		.start	= 0x20000000,
		.end	= 0x23ffffff,
		.flags	= IORESOURCE_MEM,
	},
	IRQ(~0UL), /* Magic IRQ will be overridden */
};

static int __init at32_init_ide_or_cf(struct platform_device *pdev,
		unsigned int cs, unsigned int extint)
{
	static unsigned int extint_pin_map[4] __initdata = {
		(1 << 25),
		(1 << 26),
		(1 << 27),
		(1 << 28),
	};
	static bool common_pins_initialized __initdata = false;
	unsigned int extint_pin;
	int ret;
	u32 pin_mask;

	if (extint >= ARRAY_SIZE(extint_pin_map))
		return -EINVAL;
	extint_pin = extint_pin_map[extint];

	switch (cs) {
	case 4:
		ret = platform_device_add_resources(pdev,
				at32_smc_cs4_resource,
				ARRAY_SIZE(at32_smc_cs4_resource));
		if (ret)
			return ret;

		/* NCS4   -> OE_N  */
		select_peripheral(PIOE, (1 << 21), PERIPH_A, 0);
		hmatrix_sfr_set_bits(HMATRIX_SLAVE_EBI, HMATRIX_EBI_CF0_ENABLE);
		break;
	case 5:
		ret = platform_device_add_resources(pdev,
				at32_smc_cs5_resource,
				ARRAY_SIZE(at32_smc_cs5_resource));
		if (ret)
			return ret;

		/* NCS5   -> OE_N  */
		select_peripheral(PIOE, (1 << 22), PERIPH_A, 0);
		hmatrix_sfr_set_bits(HMATRIX_SLAVE_EBI, HMATRIX_EBI_CF1_ENABLE);
		break;
	default:
		return -EINVAL;
	}

	if (!common_pins_initialized) {
		pin_mask  = (1 << 19);	/* CFCE1  -> CS0_N */
		pin_mask |= (1 << 20);	/* CFCE2  -> CS1_N */
		pin_mask |= (1 << 23);	/* CFRNW  -> DIR   */
		pin_mask |= (1 << 24);	/* NWAIT  <- IORDY */

		select_peripheral(PIOE, pin_mask, PERIPH_A, 0);

		common_pins_initialized = true;
	}

	select_peripheral(PIOB, extint_pin, PERIPH_A, AT32_GPIOF_DEGLITCH);

	pdev->resource[1].start = EIM_IRQ_BASE + extint;
	pdev->resource[1].end = pdev->resource[1].start;

	return 0;
}

struct platform_device *__init
at32_add_device_ide(unsigned int id, unsigned int extint,
		    struct ide_platform_data *data)
{
	struct platform_device *pdev;

	pdev = platform_device_alloc("at32_ide", id);
	if (!pdev)
		goto fail;

	if (platform_device_add_data(pdev, data,
				sizeof(struct ide_platform_data)))
		goto fail;

	if (at32_init_ide_or_cf(pdev, data->cs, extint))
		goto fail;

	platform_device_add(pdev);
	return pdev;

fail:
	platform_device_put(pdev);
	return NULL;
}

struct platform_device *__init
at32_add_device_cf(unsigned int id, unsigned int extint,
		    struct cf_platform_data *data)
{
	struct platform_device *pdev;

	pdev = platform_device_alloc("at32_cf", id);
	if (!pdev)
		goto fail;

	if (platform_device_add_data(pdev, data,
				sizeof(struct cf_platform_data)))
		goto fail;

	if (at32_init_ide_or_cf(pdev, data->cs, extint))
		goto fail;

	if (gpio_is_valid(data->detect_pin))
		at32_select_gpio(data->detect_pin, AT32_GPIOF_DEGLITCH);
	if (gpio_is_valid(data->reset_pin))
		at32_select_gpio(data->reset_pin, 0);
	if (gpio_is_valid(data->vcc_pin))
		at32_select_gpio(data->vcc_pin, 0);
	/* READY is used as extint, so we can't select it as gpio */

	platform_device_add(pdev);
	return pdev;

fail:
	platform_device_put(pdev);
	return NULL;
}
#endif

/* --------------------------------------------------------------------
 * NAND Flash / SmartMedia
 * -------------------------------------------------------------------- */
static struct resource smc_cs3_resource[] __initdata = {
	{
		.start	= 0x0c000000,
		.end	= 0x0fffffff,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= 0xfff03c00,
		.end	= 0xfff03fff,
		.flags	= IORESOURCE_MEM,
	},
};

struct platform_device *__init
at32_add_device_nand(unsigned int id, struct atmel_nand_data *data)
{
	struct platform_device *pdev;

	if (id != 0 || !data)
		return NULL;

	pdev = platform_device_alloc("atmel_nand", id);
	if (!pdev)
		goto fail;

	if (platform_device_add_resources(pdev, smc_cs3_resource,
				ARRAY_SIZE(smc_cs3_resource)))
		goto fail;

	/* For at32ap7000, we use the reset workaround for nand driver */
	data->need_reset_workaround = true;

	if (platform_device_add_data(pdev, data,
				sizeof(struct atmel_nand_data)))
		goto fail;

	hmatrix_sfr_set_bits(HMATRIX_SLAVE_EBI, HMATRIX_EBI_NAND_ENABLE);
	if (data->enable_pin)
		at32_select_gpio(data->enable_pin,
				AT32_GPIOF_OUTPUT | AT32_GPIOF_HIGH);
	if (data->rdy_pin)
		at32_select_gpio(data->rdy_pin, 0);
	if (data->det_pin)
		at32_select_gpio(data->det_pin, 0);

	platform_device_add(pdev);
	return pdev;

fail:
	platform_device_put(pdev);
	return NULL;
}

/* --------------------------------------------------------------------
 * AC97C
 * -------------------------------------------------------------------- */
static struct resource atmel_ac97c0_resource[] __initdata = {
	PBMEM(0xfff02800),
	IRQ(29),
};
static struct clk atmel_ac97c0_pclk = {
	.name		= "pclk",
	.parent		= &pbb_clk,
	.mode		= pbb_clk_mode,
	.get_rate	= pbb_clk_get_rate,
	.index		= 10,
};

struct platform_device *__init
at32_add_device_ac97c(unsigned int id, struct ac97c_platform_data *data,
		      unsigned int flags)
{
	struct platform_device		*pdev;
	struct dw_dma_slave		*rx_dws;
	struct dw_dma_slave		*tx_dws;
	struct ac97c_platform_data	_data;
	u32				pin_mask;

	if (id != 0)
		return NULL;

	pdev = platform_device_alloc("atmel_ac97c", id);
	if (!pdev)
		return NULL;

	if (platform_device_add_resources(pdev, atmel_ac97c0_resource,
				ARRAY_SIZE(atmel_ac97c0_resource)))
		goto out_free_resources;

	if (!data) {
		data = &_data;
		memset(data, 0, sizeof(struct ac97c_platform_data));
		data->reset_pin = -ENODEV;
	}

	rx_dws = &data->rx_dws;
	tx_dws = &data->tx_dws;

	/* Check if DMA slave interface for capture should be configured. */
	if (flags & AC97C_CAPTURE) {
		rx_dws->dma_dev = &dw_dmac0_device.dev;
		rx_dws->cfg_hi = DWC_CFGH_SRC_PER(3);
		rx_dws->cfg_lo &= ~(DWC_CFGL_HS_DST_POL | DWC_CFGL_HS_SRC_POL);
		rx_dws->src_master = 0;
		rx_dws->dst_master = 1;
	}

	/* Check if DMA slave interface for playback should be configured. */
	if (flags & AC97C_PLAYBACK) {
		tx_dws->dma_dev = &dw_dmac0_device.dev;
		tx_dws->cfg_hi = DWC_CFGH_DST_PER(4);
		tx_dws->cfg_lo &= ~(DWC_CFGL_HS_DST_POL | DWC_CFGL_HS_SRC_POL);
		tx_dws->src_master = 0;
		tx_dws->dst_master = 1;
	}

	if (platform_device_add_data(pdev, data,
				sizeof(struct ac97c_platform_data)))
		goto out_free_resources;

	/* SDO | SYNC | SCLK | SDI */
	pin_mask = (1 << 20) | (1 << 21) | (1 << 22) | (1 << 23);

	select_peripheral(PIOB, pin_mask, PERIPH_B, 0);

	if (gpio_is_valid(data->reset_pin))
		at32_select_gpio(data->reset_pin, AT32_GPIOF_OUTPUT
				| AT32_GPIOF_HIGH);

	atmel_ac97c0_pclk.dev = &pdev->dev;

	platform_device_add(pdev);
	return pdev;

out_free_resources:
	platform_device_put(pdev);
	return NULL;
}

/* --------------------------------------------------------------------
 * ABDAC
 * -------------------------------------------------------------------- */
static struct resource abdac0_resource[] __initdata = {
	PBMEM(0xfff02000),
	IRQ(27),
};
static struct clk abdac0_pclk = {
	.name		= "pclk",
	.parent		= &pbb_clk,
	.mode		= pbb_clk_mode,
	.get_rate	= pbb_clk_get_rate,
	.index		= 8,
};
static struct clk abdac0_sample_clk = {
	.name		= "sample_clk",
	.mode		= genclk_mode,
	.get_rate	= genclk_get_rate,
	.set_rate	= genclk_set_rate,
	.set_parent	= genclk_set_parent,
	.index		= 6,
};

struct platform_device *__init
at32_add_device_abdac(unsigned int id, struct atmel_abdac_pdata *data)
{
	struct platform_device	*pdev;
	struct dw_dma_slave	*dws;
	u32			pin_mask;

	if (id != 0 || !data)
		return NULL;

	pdev = platform_device_alloc("atmel_abdac", id);
	if (!pdev)
		return NULL;

	if (platform_device_add_resources(pdev, abdac0_resource,
				ARRAY_SIZE(abdac0_resource)))
		goto out_free_resources;

	dws = &data->dws;

	dws->dma_dev = &dw_dmac0_device.dev;
	dws->cfg_hi = DWC_CFGH_DST_PER(2);
	dws->cfg_lo &= ~(DWC_CFGL_HS_DST_POL | DWC_CFGL_HS_SRC_POL);
	dws->src_master = 0;
	dws->dst_master = 1;

	if (platform_device_add_data(pdev, data,
				sizeof(struct atmel_abdac_pdata)))
		goto out_free_resources;

	pin_mask  = (1 << 20) | (1 << 22);	/* DATA1 & DATAN1 */
	pin_mask |= (1 << 21) | (1 << 23);	/* DATA0 & DATAN0 */

	select_peripheral(PIOB, pin_mask, PERIPH_A, 0);

	abdac0_pclk.dev = &pdev->dev;
	abdac0_sample_clk.dev = &pdev->dev;

	platform_device_add(pdev);
	return pdev;

out_free_resources:
	platform_device_put(pdev);
	return NULL;
}

/* --------------------------------------------------------------------
 *  GCLK
 * -------------------------------------------------------------------- */
static struct clk gclk0 = {
	.name		= "gclk0",
	.mode		= genclk_mode,
	.get_rate	= genclk_get_rate,
	.set_rate	= genclk_set_rate,
	.set_parent	= genclk_set_parent,
	.index		= 0,
};
static struct clk gclk1 = {
	.name		= "gclk1",
	.mode		= genclk_mode,
	.get_rate	= genclk_get_rate,
	.set_rate	= genclk_set_rate,
	.set_parent	= genclk_set_parent,
	.index		= 1,
};
static struct clk gclk2 = {
	.name		= "gclk2",
	.mode		= genclk_mode,
	.get_rate	= genclk_get_rate,
	.set_rate	= genclk_set_rate,
	.set_parent	= genclk_set_parent,
	.index		= 2,
};
static struct clk gclk3 = {
	.name		= "gclk3",
	.mode		= genclk_mode,
	.get_rate	= genclk_get_rate,
	.set_rate	= genclk_set_rate,
	.set_parent	= genclk_set_parent,
	.index		= 3,
};
static struct clk gclk4 = {
	.name		= "gclk4",
	.mode		= genclk_mode,
	.get_rate	= genclk_get_rate,
	.set_rate	= genclk_set_rate,
	.set_parent	= genclk_set_parent,
	.index		= 4,
};

static __initdata struct clk *init_clocks[] = {
	&osc32k,
	&osc0,
	&osc1,
	&pll0,
	&pll1,
	&cpu_clk,
	&hsb_clk,
	&pba_clk,
	&pbb_clk,
	&at32_pm_pclk,
	&at32_intc0_pclk,
	&at32_hmatrix_clk,
	&ebi_clk,
	&hramc_clk,
	&sdramc_clk,
	&smc0_pclk,
	&smc0_mck,
	&pdc_hclk,
	&pdc_pclk,
	&dw_dmac0_hclk,
	&pico_clk,
	&pio0_mck,
	&pio1_mck,
	&pio2_mck,
	&pio3_mck,
	&pio4_mck,
	&at32_tcb0_t0_clk,
	&at32_tcb1_t0_clk,
	&atmel_psif0_pclk,
	&atmel_psif1_pclk,
	&atmel_usart0_usart,
	&atmel_usart1_usart,
	&atmel_usart2_usart,
	&atmel_usart3_usart,
	&atmel_pwm0_mck,
#if defined(CONFIG_CPU_AT32AP7000)
	&macb0_hclk,
	&macb0_pclk,
	&macb1_hclk,
	&macb1_pclk,
#endif
	&atmel_spi0_spi_clk,
	&atmel_spi1_spi_clk,
	&atmel_twi0_pclk,
	&atmel_mci0_pclk,
#if defined(CONFIG_CPU_AT32AP7000) || defined(CONFIG_CPU_AT32AP7002)
	&atmel_lcdfb0_hclk,
	&atmel_lcdfb0_pixclk,
#endif
	&ssc0_pclk,
	&ssc1_pclk,
	&ssc2_pclk,
	&usba0_hclk,
	&usba0_pclk,
	&atmel_ac97c0_pclk,
	&abdac0_pclk,
	&abdac0_sample_clk,
	&gclk0,
	&gclk1,
	&gclk2,
	&gclk3,
	&gclk4,
};

void __init setup_platform(void)
{
	u32 cpu_mask = 0, hsb_mask = 0, pba_mask = 0, pbb_mask = 0;
	int i;

	if (pm_readl(MCCTRL) & PM_BIT(PLLSEL)) {
		main_clock = &pll0;
		cpu_clk.parent = &pll0;
	} else {
		main_clock = &osc0;
		cpu_clk.parent = &osc0;
	}

	if (pm_readl(PLL0) & PM_BIT(PLLOSC))
		pll0.parent = &osc1;
	if (pm_readl(PLL1) & PM_BIT(PLLOSC))
		pll1.parent = &osc1;

	genclk_init_parent(&gclk0);
	genclk_init_parent(&gclk1);
	genclk_init_parent(&gclk2);
	genclk_init_parent(&gclk3);
	genclk_init_parent(&gclk4);
#if defined(CONFIG_CPU_AT32AP7000) || defined(CONFIG_CPU_AT32AP7002)
	genclk_init_parent(&atmel_lcdfb0_pixclk);
#endif
	genclk_init_parent(&abdac0_sample_clk);

	/*
	 * Build initial dynamic clock list by registering all clocks
	 * from the array.
	 * At the same time, turn on all clocks that have at least one
	 * user already, and turn off everything else. We only do this
	 * for module clocks, and even though it isn't particularly
	 * pretty to  check the address of the mode function, it should
	 * do the trick...
	 */
	for (i = 0; i < ARRAY_SIZE(init_clocks); i++) {
		struct clk *clk = init_clocks[i];

		/* first, register clock */
		at32_clk_register(clk);

		if (clk->users == 0)
			continue;

		if (clk->mode == &cpu_clk_mode)
			cpu_mask |= 1 << clk->index;
		else if (clk->mode == &hsb_clk_mode)
			hsb_mask |= 1 << clk->index;
		else if (clk->mode == &pba_clk_mode)
			pba_mask |= 1 << clk->index;
		else if (clk->mode == &pbb_clk_mode)
			pbb_mask |= 1 << clk->index;
	}

	pm_writel(CPU_MASK, cpu_mask);
	pm_writel(HSB_MASK, hsb_mask);
	pm_writel(PBA_MASK, pba_mask);
	pm_writel(PBB_MASK, pbb_mask);

	/* Initialize the port muxes */
	at32_init_pio(&pio0_device);
	at32_init_pio(&pio1_device);
	at32_init_pio(&pio2_device);
	at32_init_pio(&pio3_device);
	at32_init_pio(&pio4_device);
}

struct gen_pool *sram_pool;

static int __init sram_init(void)
{
	struct gen_pool *pool;

	/* 1KiB granularity */
	pool = gen_pool_create(10, -1);
	if (!pool)
		goto fail;

	if (gen_pool_add(pool, 0x24000000, 0x8000, -1))
		goto err_pool_add;

	sram_pool = pool;
	return 0;

err_pool_add:
	gen_pool_destroy(pool);
fail:
	pr_err("Failed to create SRAM pool\n");
	return -ENOMEM;
}
core_initcall(sram_init);
