/*
 *  Copyright (C) 2010, Lars-Peter Clausen <lars@metafoo.de>
 *  JZ4740 SoC clock support
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under  the terms of the GNU General	 Public License as published by the
 *  Free Software Foundation;  either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/err.h>

#include <asm/mach-jz4740/clock.h>
#include <asm/mach-jz4740/base.h>

#include "clock.h"

#define JZ_REG_CLOCK_PLL	0x10
#define JZ_REG_CLOCK_GATE	0x20

#define JZ_CLOCK_GATE_UART0	BIT(0)
#define JZ_CLOCK_GATE_TCU	BIT(1)
#define JZ_CLOCK_GATE_UDC	BIT(11)
#define JZ_CLOCK_GATE_DMAC	BIT(12)

#define JZ_CLOCK_PLL_STABLE		BIT(10)
#define JZ_CLOCK_PLL_ENABLED		BIT(8)

static void __iomem *jz_clock_base;

static uint32_t jz_clk_reg_read(int reg)
{
	return readl(jz_clock_base + reg);
}

static void jz_clk_reg_set_bits(int reg, uint32_t mask)
{
	uint32_t val;

	val = readl(jz_clock_base + reg);
	val |= mask;
	writel(val, jz_clock_base + reg);
}

static void jz_clk_reg_clear_bits(int reg, uint32_t mask)
{
	uint32_t val;

	val = readl(jz_clock_base + reg);
	val &= ~mask;
	writel(val, jz_clock_base + reg);
}

void jz4740_clock_udc_disable_auto_suspend(void)
{
	jz_clk_reg_clear_bits(JZ_REG_CLOCK_GATE, JZ_CLOCK_GATE_UDC);
}
EXPORT_SYMBOL_GPL(jz4740_clock_udc_disable_auto_suspend);

void jz4740_clock_udc_enable_auto_suspend(void)
{
	jz_clk_reg_set_bits(JZ_REG_CLOCK_GATE, JZ_CLOCK_GATE_UDC);
}
EXPORT_SYMBOL_GPL(jz4740_clock_udc_enable_auto_suspend);

void jz4740_clock_suspend(void)
{
	jz_clk_reg_set_bits(JZ_REG_CLOCK_GATE,
		JZ_CLOCK_GATE_TCU | JZ_CLOCK_GATE_DMAC | JZ_CLOCK_GATE_UART0);

	jz_clk_reg_clear_bits(JZ_REG_CLOCK_PLL, JZ_CLOCK_PLL_ENABLED);
}

void jz4740_clock_resume(void)
{
	uint32_t pll;

	jz_clk_reg_set_bits(JZ_REG_CLOCK_PLL, JZ_CLOCK_PLL_ENABLED);

	do {
		pll = jz_clk_reg_read(JZ_REG_CLOCK_PLL);
	} while (!(pll & JZ_CLOCK_PLL_STABLE));

	jz_clk_reg_clear_bits(JZ_REG_CLOCK_GATE,
		JZ_CLOCK_GATE_TCU | JZ_CLOCK_GATE_DMAC | JZ_CLOCK_GATE_UART0);
}

int jz4740_clock_init(void)
{
	jz_clock_base = ioremap(JZ4740_CPM_BASE_ADDR, 0x100);
	if (!jz_clock_base)
		return -EBUSY;

	return 0;
}
