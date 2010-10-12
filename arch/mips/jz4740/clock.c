/*
 *  Copyright (C) 2010, Lars-Peter Clausen <lars@metafoo.de>
 *  JZ4740 SoC clock support
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under  the terms of the GNU General  Public License as published by the
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
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/err.h>

#include <asm/mach-jz4740/clock.h>
#include <asm/mach-jz4740/base.h>

#include "clock.h"

#define JZ_REG_CLOCK_CTRL	0x00
#define JZ_REG_CLOCK_LOW_POWER	0x04
#define JZ_REG_CLOCK_PLL	0x10
#define JZ_REG_CLOCK_GATE	0x20
#define JZ_REG_CLOCK_SLEEP_CTRL	0x24
#define JZ_REG_CLOCK_I2S	0x60
#define JZ_REG_CLOCK_LCD	0x64
#define JZ_REG_CLOCK_MMC	0x68
#define JZ_REG_CLOCK_UHC	0x6C
#define JZ_REG_CLOCK_SPI	0x74

#define JZ_CLOCK_CTRL_I2S_SRC_PLL	BIT(31)
#define JZ_CLOCK_CTRL_KO_ENABLE		BIT(30)
#define JZ_CLOCK_CTRL_UDC_SRC_PLL	BIT(29)
#define JZ_CLOCK_CTRL_UDIV_MASK		0x1f800000
#define JZ_CLOCK_CTRL_CHANGE_ENABLE	BIT(22)
#define JZ_CLOCK_CTRL_PLL_HALF		BIT(21)
#define JZ_CLOCK_CTRL_LDIV_MASK		0x001f0000
#define JZ_CLOCK_CTRL_UDIV_OFFSET	23
#define JZ_CLOCK_CTRL_LDIV_OFFSET	16
#define JZ_CLOCK_CTRL_MDIV_OFFSET	12
#define JZ_CLOCK_CTRL_PDIV_OFFSET	 8
#define JZ_CLOCK_CTRL_HDIV_OFFSET	 4
#define JZ_CLOCK_CTRL_CDIV_OFFSET	 0

#define JZ_CLOCK_GATE_UART0	BIT(0)
#define JZ_CLOCK_GATE_TCU	BIT(1)
#define JZ_CLOCK_GATE_RTC	BIT(2)
#define JZ_CLOCK_GATE_I2C	BIT(3)
#define JZ_CLOCK_GATE_SPI	BIT(4)
#define JZ_CLOCK_GATE_AIC	BIT(5)
#define JZ_CLOCK_GATE_I2S	BIT(6)
#define JZ_CLOCK_GATE_MMC	BIT(7)
#define JZ_CLOCK_GATE_ADC	BIT(8)
#define JZ_CLOCK_GATE_CIM	BIT(9)
#define JZ_CLOCK_GATE_LCD	BIT(10)
#define JZ_CLOCK_GATE_UDC	BIT(11)
#define JZ_CLOCK_GATE_DMAC	BIT(12)
#define JZ_CLOCK_GATE_IPU	BIT(13)
#define JZ_CLOCK_GATE_UHC	BIT(14)
#define JZ_CLOCK_GATE_UART1	BIT(15)

#define JZ_CLOCK_I2S_DIV_MASK		0x01ff

#define JZ_CLOCK_LCD_DIV_MASK		0x01ff

#define JZ_CLOCK_MMC_DIV_MASK		0x001f

#define JZ_CLOCK_UHC_DIV_MASK		0x000f

#define JZ_CLOCK_SPI_SRC_PLL		BIT(31)
#define JZ_CLOCK_SPI_DIV_MASK		0x000f

#define JZ_CLOCK_PLL_M_MASK		0x01ff
#define JZ_CLOCK_PLL_N_MASK		0x001f
#define JZ_CLOCK_PLL_OD_MASK		0x0003
#define JZ_CLOCK_PLL_STABLE		BIT(10)
#define JZ_CLOCK_PLL_BYPASS		BIT(9)
#define JZ_CLOCK_PLL_ENABLED		BIT(8)
#define JZ_CLOCK_PLL_STABLIZE_MASK	0x000f
#define JZ_CLOCK_PLL_M_OFFSET		23
#define JZ_CLOCK_PLL_N_OFFSET		18
#define JZ_CLOCK_PLL_OD_OFFSET		16

#define JZ_CLOCK_LOW_POWER_MODE_DOZE BIT(2)
#define JZ_CLOCK_LOW_POWER_MODE_SLEEP BIT(0)

#define JZ_CLOCK_SLEEP_CTRL_SUSPEND_UHC BIT(7)
#define JZ_CLOCK_SLEEP_CTRL_ENABLE_UDC BIT(6)

static void __iomem *jz_clock_base;
static spinlock_t jz_clock_lock;
static LIST_HEAD(jz_clocks);

struct main_clk {
	struct clk clk;
	uint32_t div_offset;
};

struct divided_clk {
	struct clk clk;
	uint32_t reg;
	uint32_t mask;
};

struct static_clk {
	struct clk clk;
	unsigned long rate;
};

static uint32_t jz_clk_reg_read(int reg)
{
	return readl(jz_clock_base + reg);
}

static void jz_clk_reg_write_mask(int reg, uint32_t val, uint32_t mask)
{
	uint32_t val2;

	spin_lock(&jz_clock_lock);
	val2 = readl(jz_clock_base + reg);
	val2 &= ~mask;
	val2 |= val;
	writel(val2, jz_clock_base + reg);
	spin_unlock(&jz_clock_lock);
}

static void jz_clk_reg_set_bits(int reg, uint32_t mask)
{
	uint32_t val;

	spin_lock(&jz_clock_lock);
	val = readl(jz_clock_base + reg);
	val |= mask;
	writel(val, jz_clock_base + reg);
	spin_unlock(&jz_clock_lock);
}

static void jz_clk_reg_clear_bits(int reg, uint32_t mask)
{
	uint32_t val;

	spin_lock(&jz_clock_lock);
	val = readl(jz_clock_base + reg);
	val &= ~mask;
	writel(val, jz_clock_base + reg);
	spin_unlock(&jz_clock_lock);
}

static int jz_clk_enable_gating(struct clk *clk)
{
	if (clk->gate_bit == JZ4740_CLK_NOT_GATED)
		return -EINVAL;

	jz_clk_reg_clear_bits(JZ_REG_CLOCK_GATE, clk->gate_bit);
	return 0;
}

static int jz_clk_disable_gating(struct clk *clk)
{
	if (clk->gate_bit == JZ4740_CLK_NOT_GATED)
		return -EINVAL;

	jz_clk_reg_set_bits(JZ_REG_CLOCK_GATE, clk->gate_bit);
	return 0;
}

static int jz_clk_is_enabled_gating(struct clk *clk)
{
	if (clk->gate_bit == JZ4740_CLK_NOT_GATED)
		return 1;

	return !(jz_clk_reg_read(JZ_REG_CLOCK_GATE) & clk->gate_bit);
}

static unsigned long jz_clk_static_get_rate(struct clk *clk)
{
	return ((struct static_clk *)clk)->rate;
}

static int jz_clk_ko_enable(struct clk *clk)
{
	jz_clk_reg_set_bits(JZ_REG_CLOCK_CTRL, JZ_CLOCK_CTRL_KO_ENABLE);
	return 0;
}

static int jz_clk_ko_disable(struct clk *clk)
{
	jz_clk_reg_clear_bits(JZ_REG_CLOCK_CTRL, JZ_CLOCK_CTRL_KO_ENABLE);
	return 0;
}

static int jz_clk_ko_is_enabled(struct clk *clk)
{
	return !!(jz_clk_reg_read(JZ_REG_CLOCK_CTRL) & JZ_CLOCK_CTRL_KO_ENABLE);
}

static const int pllno[] = {1, 2, 2, 4};

static unsigned long jz_clk_pll_get_rate(struct clk *clk)
{
	uint32_t val;
	int m;
	int n;
	int od;

	val = jz_clk_reg_read(JZ_REG_CLOCK_PLL);

	if (val & JZ_CLOCK_PLL_BYPASS)
		return clk_get_rate(clk->parent);

	m = ((val >> 23) & 0x1ff) + 2;
	n = ((val >> 18) & 0x1f) + 2;
	od = (val >> 16) & 0x3;

	return ((clk_get_rate(clk->parent) / n) * m) / pllno[od];
}

static unsigned long jz_clk_pll_half_get_rate(struct clk *clk)
{
	uint32_t reg;

	reg = jz_clk_reg_read(JZ_REG_CLOCK_CTRL);
	if (reg & JZ_CLOCK_CTRL_PLL_HALF)
		return jz_clk_pll_get_rate(clk->parent);
	return jz_clk_pll_get_rate(clk->parent) >> 1;
}

static const int jz_clk_main_divs[] = {1, 2, 3, 4, 6, 8, 12, 16, 24, 32};

static unsigned long jz_clk_main_round_rate(struct clk *clk, unsigned long rate)
{
	unsigned long parent_rate = jz_clk_pll_get_rate(clk->parent);
	int div;

	div = parent_rate / rate;
	if (div > 32)
		return parent_rate / 32;
	else if (div < 1)
		return parent_rate;

	div &= (0x3 << (ffs(div) - 1));

	return parent_rate / div;
}

static unsigned long jz_clk_main_get_rate(struct clk *clk)
{
	struct main_clk *mclk = (struct main_clk *)clk;
	uint32_t div;

	div = jz_clk_reg_read(JZ_REG_CLOCK_CTRL);

	div >>= mclk->div_offset;
	div &= 0xf;

	if (div >= ARRAY_SIZE(jz_clk_main_divs))
		div = ARRAY_SIZE(jz_clk_main_divs) - 1;

	return jz_clk_pll_get_rate(clk->parent) / jz_clk_main_divs[div];
}

static int jz_clk_main_set_rate(struct clk *clk, unsigned long rate)
{
	struct main_clk *mclk = (struct main_clk *)clk;
	int i;
	int div;
	unsigned long parent_rate = jz_clk_pll_get_rate(clk->parent);

	rate = jz_clk_main_round_rate(clk, rate);

	div = parent_rate / rate;

	i = (ffs(div) - 1) << 1;
	if (i > 0 && !(div & BIT(i-1)))
		i -= 1;

	jz_clk_reg_write_mask(JZ_REG_CLOCK_CTRL, i << mclk->div_offset,
				0xf << mclk->div_offset);

	return 0;
}

static struct clk_ops jz_clk_static_ops = {
	.get_rate = jz_clk_static_get_rate,
	.enable = jz_clk_enable_gating,
	.disable = jz_clk_disable_gating,
	.is_enabled = jz_clk_is_enabled_gating,
};

static struct static_clk jz_clk_ext = {
	.clk = {
		.name = "ext",
		.gate_bit = JZ4740_CLK_NOT_GATED,
		.ops = &jz_clk_static_ops,
	},
};

static struct clk_ops jz_clk_pll_ops = {
	.get_rate = jz_clk_pll_get_rate,
};

static struct clk jz_clk_pll = {
	.name = "pll",
	.parent = &jz_clk_ext.clk,
	.ops = &jz_clk_pll_ops,
};

static struct clk_ops jz_clk_pll_half_ops = {
	.get_rate = jz_clk_pll_half_get_rate,
};

static struct clk jz_clk_pll_half = {
	.name = "pll half",
	.parent = &jz_clk_pll,
	.ops = &jz_clk_pll_half_ops,
};

static const struct clk_ops jz_clk_main_ops = {
	.get_rate = jz_clk_main_get_rate,
	.set_rate = jz_clk_main_set_rate,
	.round_rate = jz_clk_main_round_rate,
};

static struct main_clk jz_clk_cpu = {
	.clk = {
		.name = "cclk",
		.parent = &jz_clk_pll,
		.ops = &jz_clk_main_ops,
	},
	.div_offset = JZ_CLOCK_CTRL_CDIV_OFFSET,
};

static struct main_clk jz_clk_memory = {
	.clk = {
		.name = "mclk",
		.parent = &jz_clk_pll,
		.ops = &jz_clk_main_ops,
	},
	.div_offset = JZ_CLOCK_CTRL_MDIV_OFFSET,
};

static struct main_clk jz_clk_high_speed_peripheral = {
	.clk = {
		.name = "hclk",
		.parent = &jz_clk_pll,
		.ops = &jz_clk_main_ops,
	},
	.div_offset = JZ_CLOCK_CTRL_HDIV_OFFSET,
};


static struct main_clk jz_clk_low_speed_peripheral = {
	.clk = {
		.name = "pclk",
		.parent = &jz_clk_pll,
		.ops = &jz_clk_main_ops,
	},
	.div_offset = JZ_CLOCK_CTRL_PDIV_OFFSET,
};

static const struct clk_ops jz_clk_ko_ops = {
	.enable = jz_clk_ko_enable,
	.disable = jz_clk_ko_disable,
	.is_enabled = jz_clk_ko_is_enabled,
};

static struct clk jz_clk_ko = {
	.name = "cko",
	.parent = &jz_clk_memory.clk,
	.ops = &jz_clk_ko_ops,
};

static int jz_clk_spi_set_parent(struct clk *clk, struct clk *parent)
{
	if (parent == &jz_clk_pll)
		jz_clk_reg_set_bits(JZ_CLOCK_SPI_SRC_PLL, JZ_REG_CLOCK_SPI);
	else if (parent == &jz_clk_ext.clk)
		jz_clk_reg_clear_bits(JZ_CLOCK_SPI_SRC_PLL, JZ_REG_CLOCK_SPI);
	else
		return -EINVAL;

	clk->parent = parent;

	return 0;
}

static int jz_clk_i2s_set_parent(struct clk *clk, struct clk *parent)
{
	if (parent == &jz_clk_pll_half)
		jz_clk_reg_set_bits(JZ_REG_CLOCK_CTRL, JZ_CLOCK_CTRL_I2S_SRC_PLL);
	else if (parent == &jz_clk_ext.clk)
		jz_clk_reg_clear_bits(JZ_REG_CLOCK_CTRL, JZ_CLOCK_CTRL_I2S_SRC_PLL);
	else
		return -EINVAL;

	clk->parent = parent;

	return 0;
}

static int jz_clk_udc_enable(struct clk *clk)
{
	jz_clk_reg_set_bits(JZ_REG_CLOCK_SLEEP_CTRL,
			JZ_CLOCK_SLEEP_CTRL_ENABLE_UDC);

	return 0;
}

static int jz_clk_udc_disable(struct clk *clk)
{
	jz_clk_reg_clear_bits(JZ_REG_CLOCK_SLEEP_CTRL,
			JZ_CLOCK_SLEEP_CTRL_ENABLE_UDC);

	return 0;
}

static int jz_clk_udc_is_enabled(struct clk *clk)
{
	return !!(jz_clk_reg_read(JZ_REG_CLOCK_SLEEP_CTRL) &
			JZ_CLOCK_SLEEP_CTRL_ENABLE_UDC);
}

static int jz_clk_udc_set_parent(struct clk *clk, struct clk *parent)
{
	if (parent == &jz_clk_pll_half)
		jz_clk_reg_set_bits(JZ_REG_CLOCK_CTRL, JZ_CLOCK_CTRL_UDC_SRC_PLL);
	else if (parent == &jz_clk_ext.clk)
		jz_clk_reg_clear_bits(JZ_REG_CLOCK_CTRL, JZ_CLOCK_CTRL_UDC_SRC_PLL);
	else
		return -EINVAL;

	clk->parent = parent;

	return 0;
}

static int jz_clk_udc_set_rate(struct clk *clk, unsigned long rate)
{
	int div;

	if (clk->parent == &jz_clk_ext.clk)
		return -EINVAL;

	div = clk_get_rate(clk->parent) / rate - 1;

	if (div < 0)
		div = 0;
	else if (div > 63)
		div = 63;

	jz_clk_reg_write_mask(JZ_REG_CLOCK_CTRL, div << JZ_CLOCK_CTRL_UDIV_OFFSET,
				JZ_CLOCK_CTRL_UDIV_MASK);
	return 0;
}

static unsigned long jz_clk_udc_get_rate(struct clk *clk)
{
	int div;

	if (clk->parent == &jz_clk_ext.clk)
		return clk_get_rate(clk->parent);

	div = (jz_clk_reg_read(JZ_REG_CLOCK_CTRL) & JZ_CLOCK_CTRL_UDIV_MASK);
	div >>= JZ_CLOCK_CTRL_UDIV_OFFSET;
	div += 1;

	return clk_get_rate(clk->parent) / div;
}

static unsigned long jz_clk_divided_get_rate(struct clk *clk)
{
	struct divided_clk *dclk = (struct divided_clk *)clk;
	int div;

	if (clk->parent == &jz_clk_ext.clk)
		return clk_get_rate(clk->parent);

	div = (jz_clk_reg_read(dclk->reg) & dclk->mask) + 1;

	return clk_get_rate(clk->parent) / div;
}

static int jz_clk_divided_set_rate(struct clk *clk, unsigned long rate)
{
	struct divided_clk *dclk = (struct divided_clk *)clk;
	int div;

	if (clk->parent == &jz_clk_ext.clk)
		return -EINVAL;

	div = clk_get_rate(clk->parent) / rate - 1;

	if (div < 0)
		div = 0;
	else if (div > dclk->mask)
		div = dclk->mask;

	jz_clk_reg_write_mask(dclk->reg, div, dclk->mask);

	return 0;
}

static unsigned long jz_clk_ldclk_round_rate(struct clk *clk, unsigned long rate)
{
	int div;
	unsigned long parent_rate = jz_clk_pll_half_get_rate(clk->parent);

	if (rate > 150000000)
		return 150000000;

	div = parent_rate / rate;
	if (div < 1)
		div = 1;
	else if (div > 32)
		div = 32;

	return parent_rate / div;
}

static int jz_clk_ldclk_set_rate(struct clk *clk, unsigned long rate)
{
	int div;

	if (rate > 150000000)
		return -EINVAL;

	div = jz_clk_pll_half_get_rate(clk->parent) / rate - 1;
	if (div < 0)
		div = 0;
	else if (div > 31)
		div = 31;

	jz_clk_reg_write_mask(JZ_REG_CLOCK_CTRL, div << JZ_CLOCK_CTRL_LDIV_OFFSET,
				JZ_CLOCK_CTRL_LDIV_MASK);

	return 0;
}

static unsigned long jz_clk_ldclk_get_rate(struct clk *clk)
{
	int div;

	div = jz_clk_reg_read(JZ_REG_CLOCK_CTRL) & JZ_CLOCK_CTRL_LDIV_MASK;
	div >>= JZ_CLOCK_CTRL_LDIV_OFFSET;

	return jz_clk_pll_half_get_rate(clk->parent) / (div + 1);
}

static const struct clk_ops jz_clk_ops_ld = {
	.set_rate = jz_clk_ldclk_set_rate,
	.get_rate = jz_clk_ldclk_get_rate,
	.round_rate = jz_clk_ldclk_round_rate,
	.enable = jz_clk_enable_gating,
	.disable = jz_clk_disable_gating,
	.is_enabled = jz_clk_is_enabled_gating,
};

static struct clk jz_clk_ld = {
	.name = "lcd",
	.gate_bit = JZ_CLOCK_GATE_LCD,
	.parent = &jz_clk_pll_half,
	.ops = &jz_clk_ops_ld,
};

static const struct clk_ops jz_clk_i2s_ops = {
	.set_rate = jz_clk_divided_set_rate,
	.get_rate = jz_clk_divided_get_rate,
	.enable = jz_clk_enable_gating,
	.disable = jz_clk_disable_gating,
	.is_enabled = jz_clk_is_enabled_gating,
	.set_parent = jz_clk_i2s_set_parent,
};

static const struct clk_ops jz_clk_spi_ops = {
	.set_rate = jz_clk_divided_set_rate,
	.get_rate = jz_clk_divided_get_rate,
	.enable = jz_clk_enable_gating,
	.disable = jz_clk_disable_gating,
	.is_enabled = jz_clk_is_enabled_gating,
	.set_parent = jz_clk_spi_set_parent,
};

static const struct clk_ops jz_clk_divided_ops = {
	.set_rate = jz_clk_divided_set_rate,
	.get_rate = jz_clk_divided_get_rate,
	.enable = jz_clk_enable_gating,
	.disable = jz_clk_disable_gating,
	.is_enabled = jz_clk_is_enabled_gating,
};

static struct divided_clk jz4740_clock_divided_clks[] = {
	[0] = {
		.clk = {
			.name = "i2s",
			.parent = &jz_clk_ext.clk,
			.gate_bit = JZ_CLOCK_GATE_I2S,
			.ops = &jz_clk_i2s_ops,
		},
		.reg = JZ_REG_CLOCK_I2S,
		.mask = JZ_CLOCK_I2S_DIV_MASK,
	},
	[1] = {
		.clk = {
			.name = "spi",
			.parent = &jz_clk_ext.clk,
			.gate_bit = JZ_CLOCK_GATE_SPI,
			.ops = &jz_clk_spi_ops,
		},
		.reg = JZ_REG_CLOCK_SPI,
		.mask = JZ_CLOCK_SPI_DIV_MASK,
	},
	[2] = {
		.clk = {
			.name = "lcd_pclk",
			.parent = &jz_clk_pll_half,
			.gate_bit = JZ4740_CLK_NOT_GATED,
			.ops = &jz_clk_divided_ops,
		},
		.reg = JZ_REG_CLOCK_LCD,
		.mask = JZ_CLOCK_LCD_DIV_MASK,
	},
	[3] = {
		.clk = {
			.name = "mmc",
			.parent = &jz_clk_pll_half,
			.gate_bit = JZ_CLOCK_GATE_MMC,
			.ops = &jz_clk_divided_ops,
		},
		.reg = JZ_REG_CLOCK_MMC,
		.mask = JZ_CLOCK_MMC_DIV_MASK,
	},
	[4] = {
		.clk = {
			.name = "uhc",
			.parent = &jz_clk_pll_half,
			.gate_bit = JZ_CLOCK_GATE_UHC,
			.ops = &jz_clk_divided_ops,
		},
		.reg = JZ_REG_CLOCK_UHC,
		.mask = JZ_CLOCK_UHC_DIV_MASK,
	},
};

static const struct clk_ops jz_clk_udc_ops = {
	.set_parent = jz_clk_udc_set_parent,
	.set_rate = jz_clk_udc_set_rate,
	.get_rate = jz_clk_udc_get_rate,
	.enable = jz_clk_udc_enable,
	.disable = jz_clk_udc_disable,
	.is_enabled = jz_clk_udc_is_enabled,
};

static const struct clk_ops jz_clk_simple_ops = {
	.enable = jz_clk_enable_gating,
	.disable = jz_clk_disable_gating,
	.is_enabled = jz_clk_is_enabled_gating,
};

static struct clk jz4740_clock_simple_clks[] = {
	[0] = {
		.name = "udc",
		.parent = &jz_clk_ext.clk,
		.ops = &jz_clk_udc_ops,
	},
	[1] = {
		.name = "uart0",
		.parent = &jz_clk_ext.clk,
		.gate_bit = JZ_CLOCK_GATE_UART0,
		.ops = &jz_clk_simple_ops,
	},
	[2] = {
		.name = "uart1",
		.parent = &jz_clk_ext.clk,
		.gate_bit = JZ_CLOCK_GATE_UART1,
		.ops = &jz_clk_simple_ops,
	},
	[3] = {
		.name = "dma",
		.parent = &jz_clk_high_speed_peripheral.clk,
		.gate_bit = JZ_CLOCK_GATE_UART0,
		.ops = &jz_clk_simple_ops,
	},
	[4] = {
		.name = "ipu",
		.parent = &jz_clk_high_speed_peripheral.clk,
		.gate_bit = JZ_CLOCK_GATE_IPU,
		.ops = &jz_clk_simple_ops,
	},
	[5] = {
		.name = "adc",
		.parent = &jz_clk_ext.clk,
		.gate_bit = JZ_CLOCK_GATE_ADC,
		.ops = &jz_clk_simple_ops,
	},
	[6] = {
		.name = "i2c",
		.parent = &jz_clk_ext.clk,
		.gate_bit = JZ_CLOCK_GATE_I2C,
		.ops = &jz_clk_simple_ops,
	},
	[7] = {
		.name = "aic",
		.parent = &jz_clk_ext.clk,
		.gate_bit = JZ_CLOCK_GATE_AIC,
		.ops = &jz_clk_simple_ops,
	},
};

static struct static_clk jz_clk_rtc = {
	.clk = {
		.name = "rtc",
		.gate_bit = JZ_CLOCK_GATE_RTC,
		.ops = &jz_clk_static_ops,
	},
	.rate = 32768,
};

int clk_enable(struct clk *clk)
{
	if (!clk->ops->enable)
		return -EINVAL;

	return clk->ops->enable(clk);
}
EXPORT_SYMBOL_GPL(clk_enable);

void clk_disable(struct clk *clk)
{
	if (clk->ops->disable)
		clk->ops->disable(clk);
}
EXPORT_SYMBOL_GPL(clk_disable);

int clk_is_enabled(struct clk *clk)
{
	if (clk->ops->is_enabled)
		return clk->ops->is_enabled(clk);

	return 1;
}

unsigned long clk_get_rate(struct clk *clk)
{
	if (clk->ops->get_rate)
		return clk->ops->get_rate(clk);
	if (clk->parent)
		return clk_get_rate(clk->parent);

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(clk_get_rate);

int clk_set_rate(struct clk *clk, unsigned long rate)
{
	if (!clk->ops->set_rate)
		return -EINVAL;
	return clk->ops->set_rate(clk, rate);
}
EXPORT_SYMBOL_GPL(clk_set_rate);

long clk_round_rate(struct clk *clk, unsigned long rate)
{
	if (clk->ops->round_rate)
		return clk->ops->round_rate(clk, rate);

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(clk_round_rate);

int clk_set_parent(struct clk *clk, struct clk *parent)
{
	int ret;
	int enabled;

	if (!clk->ops->set_parent)
		return -EINVAL;

	enabled = clk_is_enabled(clk);
	if (enabled)
		clk_disable(clk);
	ret = clk->ops->set_parent(clk, parent);
	if (enabled)
		clk_enable(clk);

	jz4740_clock_debugfs_update_parent(clk);

	return ret;
}
EXPORT_SYMBOL_GPL(clk_set_parent);

struct clk *clk_get(struct device *dev, const char *name)
{
	struct clk *clk;

	list_for_each_entry(clk, &jz_clocks, list) {
		if (strcmp(clk->name, name) == 0)
			return clk;
	}
	return ERR_PTR(-ENXIO);
}
EXPORT_SYMBOL_GPL(clk_get);

void clk_put(struct clk *clk)
{
}
EXPORT_SYMBOL_GPL(clk_put);

static inline void clk_add(struct clk *clk)
{
	list_add_tail(&clk->list, &jz_clocks);

	jz4740_clock_debugfs_add_clk(clk);
}

static void clk_register_clks(void)
{
	size_t i;

	clk_add(&jz_clk_ext.clk);
	clk_add(&jz_clk_pll);
	clk_add(&jz_clk_pll_half);
	clk_add(&jz_clk_cpu.clk);
	clk_add(&jz_clk_high_speed_peripheral.clk);
	clk_add(&jz_clk_low_speed_peripheral.clk);
	clk_add(&jz_clk_ko);
	clk_add(&jz_clk_ld);
	clk_add(&jz_clk_rtc.clk);

	for (i = 0; i < ARRAY_SIZE(jz4740_clock_divided_clks); ++i)
		clk_add(&jz4740_clock_divided_clks[i].clk);

	for (i = 0; i < ARRAY_SIZE(jz4740_clock_simple_clks); ++i)
		clk_add(&jz4740_clock_simple_clks[i]);
}

void jz4740_clock_set_wait_mode(enum jz4740_wait_mode mode)
{
	switch (mode) {
	case JZ4740_WAIT_MODE_IDLE:
		jz_clk_reg_clear_bits(JZ_REG_CLOCK_LOW_POWER, JZ_CLOCK_LOW_POWER_MODE_SLEEP);
		break;
	case JZ4740_WAIT_MODE_SLEEP:
		jz_clk_reg_set_bits(JZ_REG_CLOCK_LOW_POWER, JZ_CLOCK_LOW_POWER_MODE_SLEEP);
		break;
	}
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

static int jz4740_clock_init(void)
{
	uint32_t val;

	jz_clock_base = ioremap(JZ4740_CPM_BASE_ADDR, 0x100);
	if (!jz_clock_base)
		return -EBUSY;

	spin_lock_init(&jz_clock_lock);

	jz_clk_ext.rate = jz4740_clock_bdata.ext_rate;
	jz_clk_rtc.rate = jz4740_clock_bdata.rtc_rate;

	val = jz_clk_reg_read(JZ_REG_CLOCK_SPI);

	if (val & JZ_CLOCK_SPI_SRC_PLL)
		jz4740_clock_divided_clks[1].clk.parent = &jz_clk_pll_half;

	val = jz_clk_reg_read(JZ_REG_CLOCK_CTRL);

	if (val & JZ_CLOCK_CTRL_I2S_SRC_PLL)
		jz4740_clock_divided_clks[0].clk.parent = &jz_clk_pll_half;

	if (val & JZ_CLOCK_CTRL_UDC_SRC_PLL)
		jz4740_clock_simple_clks[0].parent = &jz_clk_pll_half;

	jz4740_clock_debugfs_init();

	clk_register_clks();

	return 0;
}
arch_initcall(jz4740_clock_init);
