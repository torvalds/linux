/*
 * Copyright (C) 2005-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/clk.h>
#include <linux/init.h>
#include <linux/platform_device.h>

#include <asm/io.h>

#include <asm/arch/board.h>
#include <asm/arch/portmux.h>
#include <asm/arch/sm.h>

#include "clock.h"
#include "pio.h"
#include "sm.h"

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

#define DEFINE_DEV(_name, _id)					\
static struct platform_device _name##_id##_device = {		\
	.name		= #_name,				\
	.id		= _id,					\
	.resource	= _name##_id##_resource,		\
	.num_resources	= ARRAY_SIZE(_name##_id##_resource),	\
}
#define DEFINE_DEV_DATA(_name, _id)				\
static struct platform_device _name##_id##_device = {		\
	.name		= #_name,				\
	.id		= _id,					\
	.dev		= {					\
		.platform_data	= &_name##_id##_data,		\
	},							\
	.resource	= _name##_id##_resource,		\
	.num_resources	= ARRAY_SIZE(_name##_id##_resource),	\
}

#define DEV_CLK(_name, devname, bus, _index)			\
static struct clk devname##_##_name = {				\
	.name		= #_name,				\
	.dev		= &devname##_device.dev,		\
	.parent		= &bus##_clk,				\
	.mode		= bus##_clk_mode,			\
	.get_rate	= bus##_clk_get_rate,			\
	.index		= _index,				\
}

enum {
	PIOA,
	PIOB,
	PIOC,
	PIOD,
};

enum {
	FUNC_A,
	FUNC_B,
};

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

	if (!(control & SM_BIT(PLLEN)))
		return 0;

	div = SM_BFEXT(PLLDIV, control) + 1;
	mul = SM_BFEXT(PLLMUL, control) + 1;

	rate = clk->parent->get_rate(clk->parent);
	rate = (rate + div / 2) / div;
	rate *= mul;

	return rate;
}

static unsigned long pll0_get_rate(struct clk *clk)
{
	u32 control;

	control = sm_readl(&system_manager, PM_PLL0);

	return pll_get_rate(clk, control);
}

static unsigned long pll1_get_rate(struct clk *clk)
{
	u32 control;

	control = sm_readl(&system_manager, PM_PLL1);

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
	struct at32_sm *sm = &system_manager;
	unsigned long flags;
	u32 mask;

	spin_lock_irqsave(&sm->lock, flags);
	mask = sm_readl(sm, PM_CPU_MASK);
	if (enabled)
		mask |= 1 << clk->index;
	else
		mask &= ~(1 << clk->index);
	sm_writel(sm, PM_CPU_MASK, mask);
	spin_unlock_irqrestore(&sm->lock, flags);
}

static unsigned long cpu_clk_get_rate(struct clk *clk)
{
	unsigned long cksel, shift = 0;

	cksel = sm_readl(&system_manager, PM_CKSEL);
	if (cksel & SM_BIT(CPUDIV))
		shift = SM_BFEXT(CPUSEL, cksel) + 1;

	return bus_clk_get_rate(clk, shift);
}

static void hsb_clk_mode(struct clk *clk, int enabled)
{
	struct at32_sm *sm = &system_manager;
	unsigned long flags;
	u32 mask;

	spin_lock_irqsave(&sm->lock, flags);
	mask = sm_readl(sm, PM_HSB_MASK);
	if (enabled)
		mask |= 1 << clk->index;
	else
		mask &= ~(1 << clk->index);
	sm_writel(sm, PM_HSB_MASK, mask);
	spin_unlock_irqrestore(&sm->lock, flags);
}

static unsigned long hsb_clk_get_rate(struct clk *clk)
{
	unsigned long cksel, shift = 0;

	cksel = sm_readl(&system_manager, PM_CKSEL);
	if (cksel & SM_BIT(HSBDIV))
		shift = SM_BFEXT(HSBSEL, cksel) + 1;

	return bus_clk_get_rate(clk, shift);
}

static void pba_clk_mode(struct clk *clk, int enabled)
{
	struct at32_sm *sm = &system_manager;
	unsigned long flags;
	u32 mask;

	spin_lock_irqsave(&sm->lock, flags);
	mask = sm_readl(sm, PM_PBA_MASK);
	if (enabled)
		mask |= 1 << clk->index;
	else
		mask &= ~(1 << clk->index);
	sm_writel(sm, PM_PBA_MASK, mask);
	spin_unlock_irqrestore(&sm->lock, flags);
}

static unsigned long pba_clk_get_rate(struct clk *clk)
{
	unsigned long cksel, shift = 0;

	cksel = sm_readl(&system_manager, PM_CKSEL);
	if (cksel & SM_BIT(PBADIV))
		shift = SM_BFEXT(PBASEL, cksel) + 1;

	return bus_clk_get_rate(clk, shift);
}

static void pbb_clk_mode(struct clk *clk, int enabled)
{
	struct at32_sm *sm = &system_manager;
	unsigned long flags;
	u32 mask;

	spin_lock_irqsave(&sm->lock, flags);
	mask = sm_readl(sm, PM_PBB_MASK);
	if (enabled)
		mask |= 1 << clk->index;
	else
		mask &= ~(1 << clk->index);
	sm_writel(sm, PM_PBB_MASK, mask);
	spin_unlock_irqrestore(&sm->lock, flags);
}

static unsigned long pbb_clk_get_rate(struct clk *clk)
{
	unsigned long cksel, shift = 0;

	cksel = sm_readl(&system_manager, PM_CKSEL);
	if (cksel & SM_BIT(PBBDIV))
		shift = SM_BFEXT(PBBSEL, cksel) + 1;

	return bus_clk_get_rate(clk, shift);
}

static struct clk cpu_clk = {
	.name		= "cpu",
	.get_rate	= cpu_clk_get_rate,
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

	BUG_ON(clk->index > 7);

	control = sm_readl(&system_manager, PM_GCCTRL + 4 * clk->index);
	if (enabled)
		control |= SM_BIT(CEN);
	else
		control &= ~SM_BIT(CEN);
	sm_writel(&system_manager, PM_GCCTRL + 4 * clk->index, control);
}

static unsigned long genclk_get_rate(struct clk *clk)
{
	u32 control;
	unsigned long div = 1;

	BUG_ON(clk->index > 7);

	if (!clk->parent)
		return 0;

	control = sm_readl(&system_manager, PM_GCCTRL + 4 * clk->index);
	if (control & SM_BIT(DIVEN))
		div = 2 * (SM_BFEXT(DIV, control) + 1);

	return clk->parent->get_rate(clk->parent) / div;
}

static long genclk_set_rate(struct clk *clk, unsigned long rate, int apply)
{
	u32 control;
	unsigned long parent_rate, actual_rate, div;

	BUG_ON(clk->index > 7);

	if (!clk->parent)
		return 0;

	parent_rate = clk->parent->get_rate(clk->parent);
	control = sm_readl(&system_manager, PM_GCCTRL + 4 * clk->index);

	if (rate > 3 * parent_rate / 4) {
		actual_rate = parent_rate;
		control &= ~SM_BIT(DIVEN);
	} else {
		div = (parent_rate + rate) / (2 * rate) - 1;
		control = SM_BFINS(DIV, div, control) | SM_BIT(DIVEN);
		actual_rate = parent_rate / (2 * (div + 1));
	}

	printk("clk %s: new rate %lu (actual rate %lu)\n",
	       clk->name, rate, actual_rate);

	if (apply)
		sm_writel(&system_manager, PM_GCCTRL + 4 * clk->index,
			  control);

	return actual_rate;
}

int genclk_set_parent(struct clk *clk, struct clk *parent)
{
	u32 control;

	BUG_ON(clk->index > 7);

	printk("clk %s: new parent %s (was %s)\n",
	       clk->name, parent->name,
	       clk->parent ? clk->parent->name : "(null)");

	control = sm_readl(&system_manager, PM_GCCTRL + 4 * clk->index);

	if (parent == &osc1 || parent == &pll1)
		control |= SM_BIT(OSCSEL);
	else if (parent == &osc0 || parent == &pll0)
		control &= ~SM_BIT(OSCSEL);
	else
		return -EINVAL;

	if (parent == &pll0 || parent == &pll1)
		control |= SM_BIT(PLLSEL);
	else
		control &= ~SM_BIT(PLLSEL);

	sm_writel(&system_manager, PM_GCCTRL + 4 * clk->index, control);
	clk->parent = parent;

	return 0;
}

/* --------------------------------------------------------------------
 *  System peripherals
 * -------------------------------------------------------------------- */
static struct resource sm_resource[] = {
	PBMEM(0xfff00000),
	NAMED_IRQ(19, "eim"),
	NAMED_IRQ(20, "pm"),
	NAMED_IRQ(21, "rtc"),
};
struct platform_device at32_sm_device = {
	.name		= "sm",
	.id		= 0,
	.resource	= sm_resource,
	.num_resources	= ARRAY_SIZE(sm_resource),
};
DEV_CLK(pclk, at32_sm, pbb, 0);

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

void __init at32_add_system_devices(void)
{
	system_manager.eim_first_irq = NR_INTERNAL_IRQS;

	platform_device_register(&at32_sm_device);
	platform_device_register(&at32_intc0_device);
	platform_device_register(&smc0_device);
	platform_device_register(&pdc_device);

	platform_device_register(&pio0_device);
	platform_device_register(&pio1_device);
	platform_device_register(&pio2_device);
	platform_device_register(&pio3_device);
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
	IRQ(7),
};
DEFINE_DEV_DATA(atmel_usart, 0);
DEV_CLK(usart, atmel_usart0, pba, 4);

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
	portmux_set_func(PIOA,  8, FUNC_B);	/* RXD	*/
	portmux_set_func(PIOA,  9, FUNC_B);	/* TXD	*/
}

static inline void configure_usart1_pins(void)
{
	portmux_set_func(PIOA, 17, FUNC_A);	/* RXD	*/
	portmux_set_func(PIOA, 18, FUNC_A);	/* TXD	*/
}

static inline void configure_usart2_pins(void)
{
	portmux_set_func(PIOB, 26, FUNC_B);	/* RXD	*/
	portmux_set_func(PIOB, 27, FUNC_B);	/* TXD	*/
}

static inline void configure_usart3_pins(void)
{
	portmux_set_func(PIOB, 18, FUNC_B);	/* RXD	*/
	portmux_set_func(PIOB, 17, FUNC_B);	/* TXD	*/
}

static struct platform_device *at32_usarts[4];

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

static struct eth_platform_data macb0_data;
static struct resource macb0_resource[] = {
	PBMEM(0xfff01800),
	IRQ(25),
};
DEFINE_DEV_DATA(macb, 0);
DEV_CLK(hclk, macb0, hsb, 8);
DEV_CLK(pclk, macb0, pbb, 6);

struct platform_device *__init
at32_add_device_eth(unsigned int id, struct eth_platform_data *data)
{
	struct platform_device *pdev;

	switch (id) {
	case 0:
		pdev = &macb0_device;

		portmux_set_func(PIOC,  3, FUNC_A);	/* TXD0	*/
		portmux_set_func(PIOC,  4, FUNC_A);	/* TXD1	*/
		portmux_set_func(PIOC,  7, FUNC_A);	/* TXEN	*/
		portmux_set_func(PIOC,  8, FUNC_A);	/* TXCK */
		portmux_set_func(PIOC,  9, FUNC_A);	/* RXD0	*/
		portmux_set_func(PIOC, 10, FUNC_A);	/* RXD1	*/
		portmux_set_func(PIOC, 13, FUNC_A);	/* RXER	*/
		portmux_set_func(PIOC, 15, FUNC_A);	/* RXDV	*/
		portmux_set_func(PIOC, 16, FUNC_A);	/* MDC	*/
		portmux_set_func(PIOC, 17, FUNC_A);	/* MDIO	*/

		if (!data->is_rmii) {
			portmux_set_func(PIOC,  0, FUNC_A);	/* COL	*/
			portmux_set_func(PIOC,  1, FUNC_A);	/* CRS	*/
			portmux_set_func(PIOC,  2, FUNC_A);	/* TXER	*/
			portmux_set_func(PIOC,  5, FUNC_A);	/* TXD2	*/
			portmux_set_func(PIOC,  6, FUNC_A);	/* TXD3 */
			portmux_set_func(PIOC, 11, FUNC_A);	/* RXD2	*/
			portmux_set_func(PIOC, 12, FUNC_A);	/* RXD3	*/
			portmux_set_func(PIOC, 14, FUNC_A);	/* RXCK	*/
			portmux_set_func(PIOC, 18, FUNC_A);	/* SPD	*/
		}
		break;

	default:
		return NULL;
	}

	memcpy(pdev->dev.platform_data, data, sizeof(struct eth_platform_data));
	platform_device_register(pdev);

	return pdev;
}

/* --------------------------------------------------------------------
 *  SPI
 * -------------------------------------------------------------------- */
static struct resource spi0_resource[] = {
	PBMEM(0xffe00000),
	IRQ(3),
};
DEFINE_DEV(spi, 0);
DEV_CLK(mck, spi0, pba, 0);

struct platform_device *__init at32_add_device_spi(unsigned int id)
{
	struct platform_device *pdev;

	switch (id) {
	case 0:
		pdev = &spi0_device;
		portmux_set_func(PIOA,  0, FUNC_A);	/* MISO	 */
		portmux_set_func(PIOA,  1, FUNC_A);	/* MOSI	 */
		portmux_set_func(PIOA,  2, FUNC_A);	/* SCK	 */
		portmux_set_func(PIOA,  3, FUNC_A);	/* NPCS0 */
		portmux_set_func(PIOA,  4, FUNC_A);	/* NPCS1 */
		portmux_set_func(PIOA,  5, FUNC_A);	/* NPCS2 */
		break;

	default:
		return NULL;
	}

	platform_device_register(pdev);
	return pdev;
}

/* --------------------------------------------------------------------
 *  LCDC
 * -------------------------------------------------------------------- */
static struct lcdc_platform_data lcdc0_data;
static struct resource lcdc0_resource[] = {
	{
		.start		= 0xff000000,
		.end		= 0xff000fff,
		.flags		= IORESOURCE_MEM,
	},
	IRQ(1),
};
DEFINE_DEV_DATA(lcdc, 0);
DEV_CLK(hclk, lcdc0, hsb, 7);
static struct clk lcdc0_pixclk = {
	.name		= "pixclk",
	.dev		= &lcdc0_device.dev,
	.mode		= genclk_mode,
	.get_rate	= genclk_get_rate,
	.set_rate	= genclk_set_rate,
	.set_parent	= genclk_set_parent,
	.index		= 7,
};

struct platform_device *__init
at32_add_device_lcdc(unsigned int id, struct lcdc_platform_data *data)
{
	struct platform_device *pdev;

	switch (id) {
	case 0:
		pdev = &lcdc0_device;
		portmux_set_func(PIOC, 19, FUNC_A);	/* CC	  */
		portmux_set_func(PIOC, 20, FUNC_A);	/* HSYNC  */
		portmux_set_func(PIOC, 21, FUNC_A);	/* PCLK	  */
		portmux_set_func(PIOC, 22, FUNC_A);	/* VSYNC  */
		portmux_set_func(PIOC, 23, FUNC_A);	/* DVAL	  */
		portmux_set_func(PIOC, 24, FUNC_A);	/* MODE	  */
		portmux_set_func(PIOC, 25, FUNC_A);	/* PWR	  */
		portmux_set_func(PIOC, 26, FUNC_A);	/* DATA0  */
		portmux_set_func(PIOC, 27, FUNC_A);	/* DATA1  */
		portmux_set_func(PIOC, 28, FUNC_A);	/* DATA2  */
		portmux_set_func(PIOC, 29, FUNC_A);	/* DATA3  */
		portmux_set_func(PIOC, 30, FUNC_A);	/* DATA4  */
		portmux_set_func(PIOC, 31, FUNC_A);	/* DATA5  */
		portmux_set_func(PIOD,  0, FUNC_A);	/* DATA6  */
		portmux_set_func(PIOD,  1, FUNC_A);	/* DATA7  */
		portmux_set_func(PIOD,  2, FUNC_A);	/* DATA8  */
		portmux_set_func(PIOD,  3, FUNC_A);	/* DATA9  */
		portmux_set_func(PIOD,  4, FUNC_A);	/* DATA10 */
		portmux_set_func(PIOD,  5, FUNC_A);	/* DATA11 */
		portmux_set_func(PIOD,  6, FUNC_A);	/* DATA12 */
		portmux_set_func(PIOD,  7, FUNC_A);	/* DATA13 */
		portmux_set_func(PIOD,  8, FUNC_A);	/* DATA14 */
		portmux_set_func(PIOD,  9, FUNC_A);	/* DATA15 */
		portmux_set_func(PIOD, 10, FUNC_A);	/* DATA16 */
		portmux_set_func(PIOD, 11, FUNC_A);	/* DATA17 */
		portmux_set_func(PIOD, 12, FUNC_A);	/* DATA18 */
		portmux_set_func(PIOD, 13, FUNC_A);	/* DATA19 */
		portmux_set_func(PIOD, 14, FUNC_A);	/* DATA20 */
		portmux_set_func(PIOD, 15, FUNC_A);	/* DATA21 */
		portmux_set_func(PIOD, 16, FUNC_A);	/* DATA22 */
		portmux_set_func(PIOD, 17, FUNC_A);	/* DATA23 */

		clk_set_parent(&lcdc0_pixclk, &pll0);
		clk_set_rate(&lcdc0_pixclk, clk_get_rate(&pll0));
		break;

	default:
		return NULL;
	}

	memcpy(pdev->dev.platform_data, data,
	       sizeof(struct lcdc_platform_data));

	platform_device_register(pdev);
	return pdev;
}

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
	&at32_sm_pclk,
	&at32_intc0_pclk,
	&ebi_clk,
	&hramc_clk,
	&smc0_pclk,
	&smc0_mck,
	&pdc_hclk,
	&pdc_pclk,
	&pico_clk,
	&pio0_mck,
	&pio1_mck,
	&pio2_mck,
	&pio3_mck,
	&atmel_usart0_usart,
	&atmel_usart1_usart,
	&atmel_usart2_usart,
	&atmel_usart3_usart,
	&macb0_hclk,
	&macb0_pclk,
	&spi0_mck,
	&lcdc0_hclk,
	&lcdc0_pixclk,
};
unsigned int at32_nr_clocks = ARRAY_SIZE(at32_clock_list);

void __init at32_portmux_init(void)
{
	at32_init_pio(&pio0_device);
	at32_init_pio(&pio1_device);
	at32_init_pio(&pio2_device);
	at32_init_pio(&pio3_device);
}

void __init at32_clock_init(void)
{
	struct at32_sm *sm = &system_manager;
	u32 cpu_mask = 0, hsb_mask = 0, pba_mask = 0, pbb_mask = 0;
	int i;

	if (sm_readl(sm, PM_MCCTRL) & SM_BIT(PLLSEL))
		main_clock = &pll0;
	else
		main_clock = &osc0;

	if (sm_readl(sm, PM_PLL0) & SM_BIT(PLLOSC))
		pll0.parent = &osc1;
	if (sm_readl(sm, PM_PLL1) & SM_BIT(PLLOSC))
		pll1.parent = &osc1;

	/*
	 * Turn on all clocks that have at least one user already, and
	 * turn off everything else. We only do this for module
	 * clocks, and even though it isn't particularly pretty to
	 * check the address of the mode function, it should do the
	 * trick...
	 */
	for (i = 0; i < ARRAY_SIZE(at32_clock_list); i++) {
		struct clk *clk = at32_clock_list[i];

		if (clk->mode == &cpu_clk_mode)
			cpu_mask |= 1 << clk->index;
		else if (clk->mode == &hsb_clk_mode)
			hsb_mask |= 1 << clk->index;
		else if (clk->mode == &pba_clk_mode)
			pba_mask |= 1 << clk->index;
		else if (clk->mode == &pbb_clk_mode)
			pbb_mask |= 1 << clk->index;
	}

	sm_writel(sm, PM_CPU_MASK, cpu_mask);
	sm_writel(sm, PM_HSB_MASK, hsb_mask);
	sm_writel(sm, PM_PBA_MASK, pba_mask);
	sm_writel(sm, PM_PBB_MASK, pbb_mask);
}
