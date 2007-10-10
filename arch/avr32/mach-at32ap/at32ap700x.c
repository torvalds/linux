/*
 * Copyright (C) 2005-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/clk.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/spi/spi.h>

#include <asm/io.h>
#include <asm/irq.h>

#include <asm/arch/at32ap700x.h>
#include <asm/arch/board.h>
#include <asm/arch/portmux.h>

#include <video/atmel_lcdc.h>

#include "clock.h"
#include "hmatrix.h"
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
static u64 _name##_id##_dma_mask = DMA_32BIT_MASK;		\
static struct platform_device _name##_id##_device = {		\
	.name		= #_name,				\
	.id		= _id,					\
	.dev		= {					\
		.dma_mask = &_name##_id##_dma_mask,		\
		.coherent_dma_mask = DMA_32BIT_MASK,		\
	},							\
	.resource	= _name##_id##_resource,		\
	.num_resources	= ARRAY_SIZE(_name##_id##_resource),	\
}
#define DEFINE_DEV_DATA(_name, _id)				\
static u64 _name##_id##_dma_mask = DMA_32BIT_MASK;		\
static struct platform_device _name##_id##_device = {		\
	.name		= #_name,				\
	.id		= _id,					\
	.dev		= {					\
		.dma_mask = &_name##_id##_dma_mask,		\
		.platform_data	= &_name##_id##_data,		\
		.coherent_dma_mask = DMA_32BIT_MASK,		\
	},							\
	.resource	= _name##_id##_resource,		\
	.num_resources	= ARRAY_SIZE(_name##_id##_resource),	\
}

#define select_peripheral(pin, periph, flags)			\
	at32_select_periph(GPIO_PIN_##pin, GPIO_##periph, flags)

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

unsigned long at32ap7000_osc_rates[3] = {
	[0] = 32768,
	/* FIXME: these are ATSTK1002-specific */
	[1] = 20000000,
	[2] = 12000000,
};

static unsigned long osc_get_rate(struct clk *clk)
{
	return at32ap7000_osc_rates[clk->index];
}

static unsigned long pll_get_rate(struct clk *clk, unsigned long control)
{
	unsigned long div, mul, rate;

	if (!(control & PM_BIT(PLLEN)))
		return 0;

	div = PM_BFEXT(PLLDIV, control) + 1;
	mul = PM_BFEXT(PLLMUL, control) + 1;

	rate = clk->parent->get_rate(clk->parent);
	rate = (rate + div / 2) / div;
	rate *= mul;

	return rate;
}

static unsigned long pll0_get_rate(struct clk *clk)
{
	u32 control;

	control = pm_readl(PLL0);

	return pll_get_rate(clk, control);
}

static unsigned long pll1_get_rate(struct clk *clk)
{
	u32 control;

	control = pm_readl(PLL1);

	return pll_get_rate(clk, control);
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
	.get_rate	= pll1_get_rate,
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

static void pba_clk_mode(struct clk *clk, int enabled)
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

static unsigned long pba_clk_get_rate(struct clk *clk)
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

static struct resource dmaca0_resource[] = {
	{
		.start	= 0xff200000,
		.end	= 0xff20ffff,
		.flags	= IORESOURCE_MEM,
	},
	IRQ(2),
};
DEFINE_DEV(dmaca, 0);
DEV_CLK(hclk, dmaca0, hsb, 10);

/* --------------------------------------------------------------------
 * HMATRIX
 * -------------------------------------------------------------------- */

static struct clk hmatrix_clk = {
	.name		= "hmatrix_clk",
	.parent		= &pbb_clk,
	.mode		= pbb_clk_mode,
	.get_rate	= pbb_clk_get_rate,
	.index		= 2,
	.users		= 1,
};
#define HMATRIX_BASE	((void __iomem *)0xfff00800)

#define hmatrix_readl(reg)					\
	__raw_readl((HMATRIX_BASE) + HMATRIX_##reg)
#define hmatrix_writel(reg,value)				\
	__raw_writel((value), (HMATRIX_BASE) + HMATRIX_##reg)

/*
 * Set bits in the HMATRIX Special Function Register (SFR) used by the
 * External Bus Interface (EBI). This can be used to enable special
 * features like CompactFlash support, NAND Flash support, etc. on
 * certain chipselects.
 */
static inline void set_ebi_sfr_bits(u32 mask)
{
	u32 sfr;

	clk_enable(&hmatrix_clk);
	sfr = hmatrix_readl(SFR4);
	sfr |= mask;
	hmatrix_writel(SFR4, sfr);
	clk_disable(&hmatrix_clk);
}

/* --------------------------------------------------------------------
 *  System Timer/Counter (TC)
 * -------------------------------------------------------------------- */
static struct resource at32_systc0_resource[] = {
	PBMEM(0xfff00c00),
	IRQ(22),
};
struct platform_device at32_systc0_device = {
	.name		= "systc",
	.id		= 0,
	.resource	= at32_systc0_resource,
	.num_resources	= ARRAY_SIZE(at32_systc0_resource),
};
DEV_CLK(pclk, at32_systc0, pbb, 3);

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

void __init at32_add_system_devices(void)
{
	platform_device_register(&at32_pm0_device);
	platform_device_register(&at32_intc0_device);
	platform_device_register(&at32ap700x_rtc0_device);
	platform_device_register(&at32_wdt0_device);
	platform_device_register(&at32_eic0_device);
	platform_device_register(&smc0_device);
	platform_device_register(&pdc_device);
	platform_device_register(&dmaca0_device);

	platform_device_register(&at32_systc0_device);

	platform_device_register(&pio0_device);
	platform_device_register(&pio1_device);
	platform_device_register(&pio2_device);
	platform_device_register(&pio3_device);
	platform_device_register(&pio4_device);
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

static inline void configure_usart0_pins(void)
{
	select_peripheral(PA(8),  PERIPH_B, 0);	/* RXD	*/
	select_peripheral(PA(9),  PERIPH_B, 0);	/* TXD	*/
}

static inline void configure_usart1_pins(void)
{
	select_peripheral(PA(17), PERIPH_A, 0);	/* RXD	*/
	select_peripheral(PA(18), PERIPH_A, 0);	/* TXD	*/
}

static inline void configure_usart2_pins(void)
{
	select_peripheral(PB(26), PERIPH_B, 0);	/* RXD	*/
	select_peripheral(PB(27), PERIPH_B, 0);	/* TXD	*/
}

static inline void configure_usart3_pins(void)
{
	select_peripheral(PB(18), PERIPH_B, 0);	/* RXD	*/
	select_peripheral(PB(17), PERIPH_B, 0);	/* TXD	*/
}

static struct platform_device *__initdata at32_usarts[4];

void __init at32_map_usart(unsigned int hw_id, unsigned int line)
{
	struct platform_device *pdev;

	switch (hw_id) {
	case 0:
		pdev = &atmel_usart0_device;
		configure_usart0_pins();
		break;
	case 1:
		pdev = &atmel_usart1_device;
		configure_usart1_pins();
		break;
	case 2:
		pdev = &atmel_usart2_device;
		configure_usart2_pins();
		break;
	case 3:
		pdev = &atmel_usart3_device;
		configure_usart3_pins();
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
	at32_usarts[line] = pdev;
}

struct platform_device *__init at32_add_device_usart(unsigned int id)
{
	platform_device_register(at32_usarts[id]);
	return at32_usarts[id];
}

struct platform_device *atmel_default_console_device;

void __init at32_setup_serial_console(unsigned int usart_id)
{
	atmel_default_console_device = at32_usarts[usart_id];
}

/* --------------------------------------------------------------------
 *  Ethernet
 * -------------------------------------------------------------------- */

#ifdef CONFIG_CPU_AT32AP7000
static struct eth_platform_data macb0_data;
static struct resource macb0_resource[] = {
	PBMEM(0xfff01800),
	IRQ(25),
};
DEFINE_DEV_DATA(macb, 0);
DEV_CLK(hclk, macb0, hsb, 8);
DEV_CLK(pclk, macb0, pbb, 6);

static struct eth_platform_data macb1_data;
static struct resource macb1_resource[] = {
	PBMEM(0xfff01c00),
	IRQ(26),
};
DEFINE_DEV_DATA(macb, 1);
DEV_CLK(hclk, macb1, hsb, 9);
DEV_CLK(pclk, macb1, pbb, 7);

struct platform_device *__init
at32_add_device_eth(unsigned int id, struct eth_platform_data *data)
{
	struct platform_device *pdev;

	switch (id) {
	case 0:
		pdev = &macb0_device;

		select_peripheral(PC(3),  PERIPH_A, 0);	/* TXD0	*/
		select_peripheral(PC(4),  PERIPH_A, 0);	/* TXD1	*/
		select_peripheral(PC(7),  PERIPH_A, 0);	/* TXEN	*/
		select_peripheral(PC(8),  PERIPH_A, 0);	/* TXCK */
		select_peripheral(PC(9),  PERIPH_A, 0);	/* RXD0	*/
		select_peripheral(PC(10), PERIPH_A, 0);	/* RXD1	*/
		select_peripheral(PC(13), PERIPH_A, 0);	/* RXER	*/
		select_peripheral(PC(15), PERIPH_A, 0);	/* RXDV	*/
		select_peripheral(PC(16), PERIPH_A, 0);	/* MDC	*/
		select_peripheral(PC(17), PERIPH_A, 0);	/* MDIO	*/

		if (!data->is_rmii) {
			select_peripheral(PC(0),  PERIPH_A, 0);	/* COL	*/
			select_peripheral(PC(1),  PERIPH_A, 0);	/* CRS	*/
			select_peripheral(PC(2),  PERIPH_A, 0);	/* TXER	*/
			select_peripheral(PC(5),  PERIPH_A, 0);	/* TXD2	*/
			select_peripheral(PC(6),  PERIPH_A, 0);	/* TXD3 */
			select_peripheral(PC(11), PERIPH_A, 0);	/* RXD2	*/
			select_peripheral(PC(12), PERIPH_A, 0);	/* RXD3	*/
			select_peripheral(PC(14), PERIPH_A, 0);	/* RXCK	*/
			select_peripheral(PC(18), PERIPH_A, 0);	/* SPD	*/
		}
		break;

	case 1:
		pdev = &macb1_device;

		select_peripheral(PD(13), PERIPH_B, 0);		/* TXD0	*/
		select_peripheral(PD(14), PERIPH_B, 0);		/* TXD1	*/
		select_peripheral(PD(11), PERIPH_B, 0);		/* TXEN	*/
		select_peripheral(PD(12), PERIPH_B, 0);		/* TXCK */
		select_peripheral(PD(10), PERIPH_B, 0);		/* RXD0	*/
		select_peripheral(PD(6),  PERIPH_B, 0);		/* RXD1	*/
		select_peripheral(PD(5),  PERIPH_B, 0);		/* RXER	*/
		select_peripheral(PD(4),  PERIPH_B, 0);		/* RXDV	*/
		select_peripheral(PD(3),  PERIPH_B, 0);		/* MDC	*/
		select_peripheral(PD(2),  PERIPH_B, 0);		/* MDIO	*/

		if (!data->is_rmii) {
			select_peripheral(PC(19), PERIPH_B, 0);	/* COL	*/
			select_peripheral(PC(23), PERIPH_B, 0);	/* CRS	*/
			select_peripheral(PC(26), PERIPH_B, 0);	/* TXER	*/
			select_peripheral(PC(27), PERIPH_B, 0);	/* TXD2	*/
			select_peripheral(PC(28), PERIPH_B, 0);	/* TXD3 */
			select_peripheral(PC(29), PERIPH_B, 0);	/* RXD2	*/
			select_peripheral(PC(30), PERIPH_B, 0);	/* RXD3	*/
			select_peripheral(PC(24), PERIPH_B, 0);	/* RXCK	*/
			select_peripheral(PD(15), PERIPH_B, 0);	/* SPD	*/
		}
		break;

	default:
		return NULL;
	}

	memcpy(pdev->dev.platform_data, data, sizeof(struct eth_platform_data));
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

static void __init
at32_spi_setup_slaves(unsigned int bus_num, struct spi_board_info *b,
		      unsigned int n, const u8 *pins)
{
	unsigned int pin, mode;

	for (; n; n--, b++) {
		b->bus_num = bus_num;
		if (b->chip_select >= 4)
			continue;
		pin = (unsigned)b->controller_data;
		if (!pin) {
			pin = pins[b->chip_select];
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
	/*
	 * Manage the chipselects as GPIOs, normally using the same pins
	 * the SPI controller expects; but boards can use other pins.
	 */
	static u8 __initdata spi0_pins[] =
		{ GPIO_PIN_PA(3), GPIO_PIN_PA(4),
		  GPIO_PIN_PA(5), GPIO_PIN_PA(20), };
	static u8 __initdata spi1_pins[] =
		{ GPIO_PIN_PB(2), GPIO_PIN_PB(3),
		  GPIO_PIN_PB(4), GPIO_PIN_PA(27), };
	struct platform_device *pdev;

	switch (id) {
	case 0:
		pdev = &atmel_spi0_device;
		select_peripheral(PA(0),  PERIPH_A, 0);	/* MISO	 */
		select_peripheral(PA(1),  PERIPH_A, 0);	/* MOSI	 */
		select_peripheral(PA(2),  PERIPH_A, 0);	/* SCK	 */
		at32_spi_setup_slaves(0, b, n, spi0_pins);
		break;

	case 1:
		pdev = &atmel_spi1_device;
		select_peripheral(PB(0),  PERIPH_B, 0);	/* MISO  */
		select_peripheral(PB(1),  PERIPH_B, 0);	/* MOSI  */
		select_peripheral(PB(5),  PERIPH_B, 0);	/* SCK   */
		at32_spi_setup_slaves(1, b, n, spi1_pins);
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

struct platform_device *__init at32_add_device_twi(unsigned int id)
{
	struct platform_device *pdev;

	if (id != 0)
		return NULL;

	pdev = platform_device_alloc("atmel_twi", id);
	if (!pdev)
		return NULL;

	if (platform_device_add_resources(pdev, atmel_twi0_resource,
				ARRAY_SIZE(atmel_twi0_resource)))
		goto err_add_resources;

	select_peripheral(PA(6),  PERIPH_A, 0);	/* SDA	*/
	select_peripheral(PA(7),  PERIPH_A, 0);	/* SDL	*/

	atmel_twi0_pclk.dev = &pdev->dev;

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

struct platform_device *__init at32_add_device_mci(unsigned int id)
{
	struct platform_device *pdev;

	if (id != 0)
		return NULL;

	pdev = platform_device_alloc("atmel_mci", id);
	if (!pdev)
		return NULL;

	if (platform_device_add_resources(pdev, atmel_mci0_resource,
				ARRAY_SIZE(atmel_mci0_resource)))
		goto err_add_resources;

	select_peripheral(PA(10), PERIPH_A, 0);	/* CLK	 */
	select_peripheral(PA(11), PERIPH_A, 0);	/* CMD	 */
	select_peripheral(PA(12), PERIPH_A, 0);	/* DATA0 */
	select_peripheral(PA(13), PERIPH_A, 0);	/* DATA1 */
	select_peripheral(PA(14), PERIPH_A, 0);	/* DATA2 */
	select_peripheral(PA(15), PERIPH_A, 0);	/* DATA3 */

	atmel_mci0_pclk.dev = &pdev->dev;

	platform_device_add(pdev);
	return pdev;

err_add_resources:
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
DEV_CLK(hck1, atmel_lcdfb0, hsb, 7);
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
		     unsigned long fbmem_start, unsigned long fbmem_len)
{
	struct platform_device *pdev;
	struct atmel_lcdfb_info *info;
	struct fb_monspecs *monspecs;
	struct fb_videomode *modedb;
	unsigned int modedb_size;

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
		select_peripheral(PC(19), PERIPH_A, 0);	/* CC	  */
		select_peripheral(PC(20), PERIPH_A, 0);	/* HSYNC  */
		select_peripheral(PC(21), PERIPH_A, 0);	/* PCLK	  */
		select_peripheral(PC(22), PERIPH_A, 0);	/* VSYNC  */
		select_peripheral(PC(23), PERIPH_A, 0);	/* DVAL	  */
		select_peripheral(PC(24), PERIPH_A, 0);	/* MODE	  */
		select_peripheral(PC(25), PERIPH_A, 0);	/* PWR	  */
		select_peripheral(PC(26), PERIPH_A, 0);	/* DATA0  */
		select_peripheral(PC(27), PERIPH_A, 0);	/* DATA1  */
		select_peripheral(PC(28), PERIPH_A, 0);	/* DATA2  */
		select_peripheral(PC(29), PERIPH_A, 0);	/* DATA3  */
		select_peripheral(PC(30), PERIPH_A, 0);	/* DATA4  */
		select_peripheral(PC(31), PERIPH_A, 0);	/* DATA5  */
		select_peripheral(PD(0),  PERIPH_A, 0);	/* DATA6  */
		select_peripheral(PD(1),  PERIPH_A, 0);	/* DATA7  */
		select_peripheral(PD(2),  PERIPH_A, 0);	/* DATA8  */
		select_peripheral(PD(3),  PERIPH_A, 0);	/* DATA9  */
		select_peripheral(PD(4),  PERIPH_A, 0);	/* DATA10 */
		select_peripheral(PD(5),  PERIPH_A, 0);	/* DATA11 */
		select_peripheral(PD(6),  PERIPH_A, 0);	/* DATA12 */
		select_peripheral(PD(7),  PERIPH_A, 0);	/* DATA13 */
		select_peripheral(PD(8),  PERIPH_A, 0);	/* DATA14 */
		select_peripheral(PD(9),  PERIPH_A, 0);	/* DATA15 */
		select_peripheral(PD(10), PERIPH_A, 0);	/* DATA16 */
		select_peripheral(PD(11), PERIPH_A, 0);	/* DATA17 */
		select_peripheral(PD(12), PERIPH_A, 0);	/* DATA18 */
		select_peripheral(PD(13), PERIPH_A, 0);	/* DATA19 */
		select_peripheral(PD(14), PERIPH_A, 0);	/* DATA20 */
		select_peripheral(PD(15), PERIPH_A, 0);	/* DATA21 */
		select_peripheral(PD(16), PERIPH_A, 0);	/* DATA22 */
		select_peripheral(PD(17), PERIPH_A, 0);	/* DATA23 */

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

	switch (id) {
	case 0:
		pdev = &ssc0_device;
		if (flags & ATMEL_SSC_RF)
			select_peripheral(PA(21), PERIPH_A, 0);	/* RF */
		if (flags & ATMEL_SSC_RK)
			select_peripheral(PA(22), PERIPH_A, 0);	/* RK */
		if (flags & ATMEL_SSC_TK)
			select_peripheral(PA(23), PERIPH_A, 0);	/* TK */
		if (flags & ATMEL_SSC_TF)
			select_peripheral(PA(24), PERIPH_A, 0);	/* TF */
		if (flags & ATMEL_SSC_TD)
			select_peripheral(PA(25), PERIPH_A, 0);	/* TD */
		if (flags & ATMEL_SSC_RD)
			select_peripheral(PA(26), PERIPH_A, 0);	/* RD */
		break;
	case 1:
		pdev = &ssc1_device;
		if (flags & ATMEL_SSC_RF)
			select_peripheral(PA(0), PERIPH_B, 0);	/* RF */
		if (flags & ATMEL_SSC_RK)
			select_peripheral(PA(1), PERIPH_B, 0);	/* RK */
		if (flags & ATMEL_SSC_TK)
			select_peripheral(PA(2), PERIPH_B, 0);	/* TK */
		if (flags & ATMEL_SSC_TF)
			select_peripheral(PA(3), PERIPH_B, 0);	/* TF */
		if (flags & ATMEL_SSC_TD)
			select_peripheral(PA(4), PERIPH_B, 0);	/* TD */
		if (flags & ATMEL_SSC_RD)
			select_peripheral(PA(5), PERIPH_B, 0);	/* RD */
		break;
	case 2:
		pdev = &ssc2_device;
		if (flags & ATMEL_SSC_TD)
			select_peripheral(PB(13), PERIPH_A, 0);	/* TD */
		if (flags & ATMEL_SSC_RD)
			select_peripheral(PB(14), PERIPH_A, 0);	/* RD */
		if (flags & ATMEL_SSC_TK)
			select_peripheral(PB(15), PERIPH_A, 0);	/* TK */
		if (flags & ATMEL_SSC_TF)
			select_peripheral(PB(16), PERIPH_A, 0);	/* TF */
		if (flags & ATMEL_SSC_RF)
			select_peripheral(PB(17), PERIPH_A, 0);	/* RF */
		if (flags & ATMEL_SSC_RK)
			select_peripheral(PB(18), PERIPH_A, 0);	/* RK */
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

struct platform_device *__init
at32_add_device_usba(unsigned int id, struct usba_platform_data *data)
{
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
		if (platform_device_add_data(pdev, data, sizeof(*data)))
			goto out_free_pdev;

		if (data->vbus_pin != GPIO_PIN_NONE)
			at32_select_gpio(data->vbus_pin, 0);
	}

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
		GPIO_PIN_PB(25),
		GPIO_PIN_PB(26),
		GPIO_PIN_PB(27),
		GPIO_PIN_PB(28),
	};
	static bool common_pins_initialized __initdata = false;
	unsigned int extint_pin;
	int ret;

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

		select_peripheral(PE(21), PERIPH_A, 0); /* NCS4   -> OE_N  */
		set_ebi_sfr_bits(HMATRIX_BIT(CS4A));
		break;
	case 5:
		ret = platform_device_add_resources(pdev,
				at32_smc_cs5_resource,
				ARRAY_SIZE(at32_smc_cs5_resource));
		if (ret)
			return ret;

		select_peripheral(PE(22), PERIPH_A, 0); /* NCS5   -> OE_N  */
		set_ebi_sfr_bits(HMATRIX_BIT(CS5A));
		break;
	default:
		return -EINVAL;
	}

	if (!common_pins_initialized) {
		select_peripheral(PE(19), PERIPH_A, 0);	/* CFCE1  -> CS0_N */
		select_peripheral(PE(20), PERIPH_A, 0);	/* CFCE2  -> CS1_N */
		select_peripheral(PE(23), PERIPH_A, 0); /* CFRNW  -> DIR   */
		select_peripheral(PE(24), PERIPH_A, 0); /* NWAIT  <- IORDY */
		common_pins_initialized = true;
	}

	at32_select_periph(extint_pin, GPIO_PERIPH_A, AT32_GPIOF_DEGLITCH);

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

	if (data->detect_pin != GPIO_PIN_NONE)
		at32_select_gpio(data->detect_pin, AT32_GPIOF_DEGLITCH);
	if (data->reset_pin != GPIO_PIN_NONE)
		at32_select_gpio(data->reset_pin, 0);
	if (data->vcc_pin != GPIO_PIN_NONE)
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

struct platform_device *__init at32_add_device_ac97c(unsigned int id)
{
	struct platform_device *pdev;

	if (id != 0)
		return NULL;

	pdev = platform_device_alloc("atmel_ac97c", id);
	if (!pdev)
		return NULL;

	if (platform_device_add_resources(pdev, atmel_ac97c0_resource,
				ARRAY_SIZE(atmel_ac97c0_resource)))
		goto err_add_resources;

	select_peripheral(PB(20), PERIPH_B, 0);	/* SYNC	*/
	select_peripheral(PB(21), PERIPH_B, 0);	/* SDO	*/
	select_peripheral(PB(22), PERIPH_B, 0);	/* SDI	*/
	select_peripheral(PB(23), PERIPH_B, 0);	/* SCLK	*/

	atmel_ac97c0_pclk.dev = &pdev->dev;

	platform_device_add(pdev);
	return pdev;

err_add_resources:
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

struct platform_device *__init at32_add_device_abdac(unsigned int id)
{
	struct platform_device *pdev;

	if (id != 0)
		return NULL;

	pdev = platform_device_alloc("abdac", id);
	if (!pdev)
		return NULL;

	if (platform_device_add_resources(pdev, abdac0_resource,
				ARRAY_SIZE(abdac0_resource)))
		goto err_add_resources;

	select_peripheral(PB(20), PERIPH_A, 0);	/* DATA1	*/
	select_peripheral(PB(21), PERIPH_A, 0);	/* DATA0	*/
	select_peripheral(PB(22), PERIPH_A, 0);	/* DATAN1	*/
	select_peripheral(PB(23), PERIPH_A, 0);	/* DATAN0	*/

	abdac0_pclk.dev = &pdev->dev;
	abdac0_sample_clk.dev = &pdev->dev;

	platform_device_add(pdev);
	return pdev;

err_add_resources:
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

struct clk *at32_clock_list[] = {
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
	&hmatrix_clk,
	&ebi_clk,
	&hramc_clk,
	&smc0_pclk,
	&smc0_mck,
	&pdc_hclk,
	&pdc_pclk,
	&dmaca0_hclk,
	&pico_clk,
	&pio0_mck,
	&pio1_mck,
	&pio2_mck,
	&pio3_mck,
	&pio4_mck,
	&at32_systc0_pclk,
	&atmel_usart0_usart,
	&atmel_usart1_usart,
	&atmel_usart2_usart,
	&atmel_usart3_usart,
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
	&atmel_lcdfb0_hck1,
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
unsigned int at32_nr_clocks = ARRAY_SIZE(at32_clock_list);

void __init at32_portmux_init(void)
{
	at32_init_pio(&pio0_device);
	at32_init_pio(&pio1_device);
	at32_init_pio(&pio2_device);
	at32_init_pio(&pio3_device);
	at32_init_pio(&pio4_device);
}

void __init at32_clock_init(void)
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
	 * Turn on all clocks that have at least one user already, and
	 * turn off everything else. We only do this for module
	 * clocks, and even though it isn't particularly pretty to
	 * check the address of the mode function, it should do the
	 * trick...
	 */
	for (i = 0; i < ARRAY_SIZE(at32_clock_list); i++) {
		struct clk *clk = at32_clock_list[i];

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
}
