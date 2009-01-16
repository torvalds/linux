/*
 *  Copyright (C) 2008 Sascha Hauer <s.hauer@pengutronix.de>, Pengutronix
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/math64.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>

#include <mach/clock.h>
#include <mach/hardware.h>
#include "crm_regs.h"

static int _clk_enable(struct clk *clk)
{
	unsigned int reg;

	reg = __raw_readl(clk->enable_reg);
	reg |= 1 << clk->enable_shift;
	__raw_writel(reg, clk->enable_reg);

	return 0;
}

static void _clk_disable(struct clk *clk)
{
	unsigned int reg;

	reg = __raw_readl(clk->enable_reg);
	reg &= ~(1 << clk->enable_shift);
	__raw_writel(reg, clk->enable_reg);
}

static int _clk_can_use_parent(const struct clk *clk_arr[], unsigned int size,
			       struct clk *parent)
{
	int i;

	for (i = 0; i < size; i++)
		if (parent == clk_arr[i])
			return i;

	return -EINVAL;
}

static unsigned long
_clk_simple_round_rate(struct clk *clk, unsigned long rate, unsigned int limit)
{
	int div;
	unsigned long parent_rate;

	parent_rate = clk_get_rate(clk->parent);

	div = parent_rate / rate;
	if (parent_rate % rate)
		div++;

	if (div > limit)
		div = limit;

	return parent_rate / div;
}

static unsigned long _clk_parent_round_rate(struct clk *clk, unsigned long rate)
{
	return clk->parent->round_rate(clk->parent, rate);
}

static int _clk_parent_set_rate(struct clk *clk, unsigned long rate)
{
	return clk->parent->set_rate(clk->parent, rate);
}

/*
 *  get the system pll clock in Hz
 *
 *                  mfi + mfn / (mfd +1)
 *  f = 2 * f_ref * --------------------
 *                        pd + 1
 */
static unsigned long mx1_decode_pll(unsigned int pll, u32 f_ref)
{
	unsigned long long ll;
	unsigned long quot;

	u32 mfi = (pll >> 10) & 0xf;
	u32 mfn = pll & 0x3ff;
	u32 mfd = (pll >> 16) & 0x3ff;
	u32 pd =  (pll >> 26) & 0xf;

	mfi = mfi <= 5 ? 5 : mfi;

	ll = 2 * (unsigned long long)f_ref *
		((mfi << 16) + (mfn << 16) / (mfd + 1));
	quot = (pd + 1) * (1 << 16);
	ll += quot / 2;
	do_div(ll, quot);
	return (unsigned long)ll;
}

static unsigned long clk16m_get_rate(struct clk *clk)
{
	return 16000000;
}

static struct clk clk16m = {
	.name = "CLK16M",
	.get_rate = clk16m_get_rate,
	.enable = _clk_enable,
	.enable_reg = CCM_CSCR,
	.enable_shift = CCM_CSCR_OSC_EN_SHIFT,
	.disable = _clk_disable,
};

/* in Hz */
static unsigned long clk32_rate;

static unsigned long clk32_get_rate(struct clk *clk)
{
	return clk32_rate;
}

static struct clk clk32 = {
	.name = "CLK32",
	.get_rate = clk32_get_rate,
};

static unsigned long clk32_premult_get_rate(struct clk *clk)
{
	return clk_get_rate(clk->parent) * 512;
}

static struct clk clk32_premult = {
	.name = "CLK32_premultiplier",
	.parent = &clk32,
	.get_rate = clk32_premult_get_rate,
};

static const struct clk *prem_clk_clocks[] = {
	&clk32_premult,
	&clk16m,
};

static int prem_clk_set_parent(struct clk *clk, struct clk *parent)
{
	int i;
	unsigned int reg = __raw_readl(CCM_CSCR);

	i = _clk_can_use_parent(prem_clk_clocks, ARRAY_SIZE(prem_clk_clocks),
				parent);

	switch (i) {
	case 0:
		reg &= ~CCM_CSCR_SYSTEM_SEL;
		break;
	case 1:
		reg |= CCM_CSCR_SYSTEM_SEL;
		break;
	default:
		return i;
	}

	__raw_writel(reg, CCM_CSCR);

	return 0;
}

static struct clk prem_clk = {
	.name = "prem_clk",
	.set_parent = prem_clk_set_parent,
};

static unsigned long system_clk_get_rate(struct clk *clk)
{
	return mx1_decode_pll(__raw_readl(CCM_SPCTL0),
			      clk_get_rate(clk->parent));
}

static struct clk system_clk = {
	.name = "system_clk",
	.parent = &prem_clk,
	.get_rate = system_clk_get_rate,
};

static unsigned long mcu_clk_get_rate(struct clk *clk)
{
	return mx1_decode_pll(__raw_readl(CCM_MPCTL0),
			      clk_get_rate(clk->parent));
}

static struct clk mcu_clk = {
	.name = "mcu_clk",
	.parent = &clk32_premult,
	.get_rate = mcu_clk_get_rate,
};

static unsigned long fclk_get_rate(struct clk *clk)
{
	unsigned long fclk = clk_get_rate(clk->parent);

	if (__raw_readl(CCM_CSCR) & CCM_CSCR_PRESC)
		fclk /= 2;

	return fclk;
}

static struct clk fclk = {
	.name = "fclk",
	.parent = &mcu_clk,
	.get_rate = fclk_get_rate,
};

/*
 *  get hclk ( SDRAM, CSI, Memory Stick, I2C, DMA )
 */
static unsigned long hclk_get_rate(struct clk *clk)
{
	return clk_get_rate(clk->parent) / (((__raw_readl(CCM_CSCR) &
			CCM_CSCR_BCLK_MASK) >> CCM_CSCR_BCLK_OFFSET) + 1);
}

static unsigned long hclk_round_rate(struct clk *clk, unsigned long rate)
{
	return _clk_simple_round_rate(clk, rate, 16);
}

static int hclk_set_rate(struct clk *clk, unsigned long rate)
{
	unsigned int div;
	unsigned int reg;
	unsigned long parent_rate;

	parent_rate = clk_get_rate(clk->parent);

	div = parent_rate / rate;

	if (div > 16 || div < 1 || ((parent_rate / div) != rate))
		return -EINVAL;

	div--;

	reg = __raw_readl(CCM_CSCR);
	reg &= ~CCM_CSCR_BCLK_MASK;
	reg |= div << CCM_CSCR_BCLK_OFFSET;
	__raw_writel(reg, CCM_CSCR);

	return 0;
}

static struct clk hclk = {
	.name = "hclk",
	.parent = &system_clk,
	.get_rate = hclk_get_rate,
	.round_rate = hclk_round_rate,
	.set_rate = hclk_set_rate,
};

static unsigned long clk48m_get_rate(struct clk *clk)
{
	return clk_get_rate(clk->parent) / (((__raw_readl(CCM_CSCR) &
			CCM_CSCR_USB_MASK) >> CCM_CSCR_USB_OFFSET) + 1);
}

static unsigned long clk48m_round_rate(struct clk *clk, unsigned long rate)
{
	return _clk_simple_round_rate(clk, rate, 8);
}

static int clk48m_set_rate(struct clk *clk, unsigned long rate)
{
	unsigned int div;
	unsigned int reg;
	unsigned long parent_rate;

	parent_rate = clk_get_rate(clk->parent);

	div = parent_rate / rate;

	if (div > 8 || div < 1 || ((parent_rate / div) != rate))
		return -EINVAL;

	div--;

	reg = __raw_readl(CCM_CSCR);
	reg &= ~CCM_CSCR_USB_MASK;
	reg |= div << CCM_CSCR_USB_OFFSET;
	__raw_writel(reg, CCM_CSCR);

	return 0;
}

static struct clk clk48m = {
	.name = "CLK48M",
	.parent = &system_clk,
	.get_rate = clk48m_get_rate,
	.round_rate = clk48m_round_rate,
	.set_rate = clk48m_set_rate,
};

/*
 *  get peripheral clock 1 ( UART[12], Timer[12], PWM )
 */
static unsigned long perclk1_get_rate(struct clk *clk)
{
	return clk_get_rate(clk->parent) / (((__raw_readl(CCM_PCDR) &
			CCM_PCDR_PCLK1_MASK) >> CCM_PCDR_PCLK1_OFFSET) + 1);
}

static unsigned long perclk1_round_rate(struct clk *clk, unsigned long rate)
{
	return _clk_simple_round_rate(clk, rate, 16);
}

static int perclk1_set_rate(struct clk *clk, unsigned long rate)
{
	unsigned int div;
	unsigned int reg;
	unsigned long parent_rate;

	parent_rate = clk_get_rate(clk->parent);

	div = parent_rate / rate;

	if (div > 16 || div < 1 || ((parent_rate / div) != rate))
		return -EINVAL;

	div--;

	reg = __raw_readl(CCM_PCDR);
	reg &= ~CCM_PCDR_PCLK1_MASK;
	reg |= div << CCM_PCDR_PCLK1_OFFSET;
	__raw_writel(reg, CCM_PCDR);

	return 0;
}

/*
 *  get peripheral clock 2 ( LCD, SD, SPI[12] )
 */
static unsigned long perclk2_get_rate(struct clk *clk)
{
	return clk_get_rate(clk->parent) / (((__raw_readl(CCM_PCDR) &
			CCM_PCDR_PCLK2_MASK) >> CCM_PCDR_PCLK2_OFFSET) + 1);
}

static unsigned long perclk2_round_rate(struct clk *clk, unsigned long rate)
{
	return _clk_simple_round_rate(clk, rate, 16);
}

static int perclk2_set_rate(struct clk *clk, unsigned long rate)
{
	unsigned int div;
	unsigned int reg;
	unsigned long parent_rate;

	parent_rate = clk_get_rate(clk->parent);

	div = parent_rate / rate;

	if (div > 16 || div < 1 || ((parent_rate / div) != rate))
		return -EINVAL;

	div--;

	reg = __raw_readl(CCM_PCDR);
	reg &= ~CCM_PCDR_PCLK2_MASK;
	reg |= div << CCM_PCDR_PCLK2_OFFSET;
	__raw_writel(reg, CCM_PCDR);

	return 0;
}

/*
 *  get peripheral clock 3 ( SSI )
 */
static unsigned long perclk3_get_rate(struct clk *clk)
{
	return clk_get_rate(clk->parent) / (((__raw_readl(CCM_PCDR) &
			CCM_PCDR_PCLK3_MASK) >> CCM_PCDR_PCLK3_OFFSET) + 1);
}

static unsigned long perclk3_round_rate(struct clk *clk, unsigned long rate)
{
	return _clk_simple_round_rate(clk, rate, 128);
}

static int perclk3_set_rate(struct clk *clk, unsigned long rate)
{
	unsigned int div;
	unsigned int reg;
	unsigned long parent_rate;

	parent_rate = clk_get_rate(clk->parent);

	div = parent_rate / rate;

	if (div > 128 || div < 1 || ((parent_rate / div) != rate))
		return -EINVAL;

	div--;

	reg = __raw_readl(CCM_PCDR);
	reg &= ~CCM_PCDR_PCLK3_MASK;
	reg |= div << CCM_PCDR_PCLK3_OFFSET;
	__raw_writel(reg, CCM_PCDR);

	return 0;
}

static struct clk perclk[] = {
	{
		.name = "perclk",
		.id = 0,
		.parent = &system_clk,
		.get_rate = perclk1_get_rate,
		.round_rate = perclk1_round_rate,
		.set_rate = perclk1_set_rate,
	}, {
		.name = "perclk",
		.id = 1,
		.parent = &system_clk,
		.get_rate = perclk2_get_rate,
		.round_rate = perclk2_round_rate,
		.set_rate = perclk2_set_rate,
	}, {
		.name = "perclk",
		.id = 2,
		.parent = &system_clk,
		.get_rate = perclk3_get_rate,
		.round_rate = perclk3_round_rate,
		.set_rate = perclk3_set_rate,
	}
};

static const struct clk *clko_clocks[] = {
	&perclk[0],
	&hclk,
	&clk48m,
	&clk16m,
	&prem_clk,
	&fclk,
};

static int clko_set_parent(struct clk *clk, struct clk *parent)
{
	int i;
	unsigned int reg;

	i = _clk_can_use_parent(clko_clocks, ARRAY_SIZE(clko_clocks), parent);
	if (i < 0)
		return i;

	reg = __raw_readl(CCM_CSCR) & ~CCM_CSCR_CLKO_MASK;
	reg |= i << CCM_CSCR_CLKO_OFFSET;
	__raw_writel(reg, CCM_CSCR);

	if (clko_clocks[i]->set_rate && clko_clocks[i]->round_rate) {
		clk->set_rate = _clk_parent_set_rate;
		clk->round_rate = _clk_parent_round_rate;
	} else {
		clk->set_rate = NULL;
		clk->round_rate = NULL;
	}

	return 0;
}

static struct clk clko_clk = {
	.name = "clko_clk",
	.set_parent = clko_set_parent,
};

static struct clk dma_clk = {
	.name = "dma_clk",
	.parent = &hclk,
	.round_rate = _clk_parent_round_rate,
	.set_rate = _clk_parent_set_rate,
	.enable = _clk_enable,
	.enable_reg = SCM_GCCR,
	.enable_shift = SCM_GCCR_DMA_CLK_EN_OFFSET,
	.disable = _clk_disable,
};

static struct clk csi_clk = {
	.name = "csi_clk",
	.parent = &hclk,
	.round_rate = _clk_parent_round_rate,
	.set_rate = _clk_parent_set_rate,
	.enable = _clk_enable,
	.enable_reg = SCM_GCCR,
	.enable_shift = SCM_GCCR_CSI_CLK_EN_OFFSET,
	.disable = _clk_disable,
};

static struct clk mma_clk = {
	.name = "mma_clk",
	.parent = &hclk,
	.round_rate = _clk_parent_round_rate,
	.set_rate = _clk_parent_set_rate,
	.enable = _clk_enable,
	.enable_reg = SCM_GCCR,
	.enable_shift = SCM_GCCR_MMA_CLK_EN_OFFSET,
	.disable = _clk_disable,
};

static struct clk usbd_clk = {
	.name = "usbd_clk",
	.parent = &clk48m,
	.round_rate = _clk_parent_round_rate,
	.set_rate = _clk_parent_set_rate,
	.enable = _clk_enable,
	.enable_reg = SCM_GCCR,
	.enable_shift = SCM_GCCR_USBD_CLK_EN_OFFSET,
	.disable = _clk_disable,
};

static struct clk gpt_clk = {
	.name = "gpt_clk",
	.parent = &perclk[0],
	.round_rate = _clk_parent_round_rate,
	.set_rate = _clk_parent_set_rate,
};

static struct clk uart_clk = {
	.name = "uart_clk",
	.parent = &perclk[0],
	.round_rate = _clk_parent_round_rate,
	.set_rate = _clk_parent_set_rate,
};

static struct clk i2c_clk = {
	.name = "i2c_clk",
	.parent = &hclk,
	.round_rate = _clk_parent_round_rate,
	.set_rate = _clk_parent_set_rate,
};

static struct clk spi_clk = {
	.name = "spi_clk",
	.parent = &perclk[1],
	.round_rate = _clk_parent_round_rate,
	.set_rate = _clk_parent_set_rate,
};

static struct clk sdhc_clk = {
	.name = "sdhc_clk",
	.parent = &perclk[1],
	.round_rate = _clk_parent_round_rate,
	.set_rate = _clk_parent_set_rate,
};

static struct clk lcdc_clk = {
	.name = "lcdc_clk",
	.parent = &perclk[1],
	.round_rate = _clk_parent_round_rate,
	.set_rate = _clk_parent_set_rate,
};

static struct clk mshc_clk = {
	.name = "mshc_clk",
	.parent = &hclk,
	.round_rate = _clk_parent_round_rate,
	.set_rate = _clk_parent_set_rate,
};

static struct clk ssi_clk = {
	.name = "ssi_clk",
	.parent = &perclk[2],
	.round_rate = _clk_parent_round_rate,
	.set_rate = _clk_parent_set_rate,
};

static struct clk rtc_clk = {
	.name = "rtc_clk",
	.parent = &clk32,
};

static struct clk *mxc_clks[] = {
	&clk16m,
	&clk32,
	&clk32_premult,
	&prem_clk,
	&system_clk,
	&mcu_clk,
	&fclk,
	&hclk,
	&clk48m,
	&perclk[0],
	&perclk[1],
	&perclk[2],
	&clko_clk,
	&dma_clk,
	&csi_clk,
	&mma_clk,
	&usbd_clk,
	&gpt_clk,
	&uart_clk,
	&i2c_clk,
	&spi_clk,
	&sdhc_clk,
	&lcdc_clk,
	&mshc_clk,
	&ssi_clk,
	&rtc_clk,
};

int __init mxc_clocks_init(unsigned long fref)
{
	struct clk **clkp;
	unsigned int reg;

	/* disable clocks we are able to */
	__raw_writel(0, SCM_GCCR);

	clk32_rate = fref;
	reg = __raw_readl(CCM_CSCR);

	/* detect clock reference for system PLL */
	if (reg & CCM_CSCR_SYSTEM_SEL) {
		prem_clk.parent = &clk16m;
	} else {
		/* ensure that oscillator is disabled */
		reg &= ~(1 << CCM_CSCR_OSC_EN_SHIFT);
		__raw_writel(reg, CCM_CSCR);
		prem_clk.parent = &clk32_premult;
	}

	/* detect reference for CLKO */
	reg = (reg & CCM_CSCR_CLKO_MASK) >> CCM_CSCR_CLKO_OFFSET;
	clko_clk.parent = (struct clk *)clko_clocks[reg];

	for (clkp = mxc_clks; clkp < mxc_clks + ARRAY_SIZE(mxc_clks); clkp++)
		clk_register(*clkp);

	clk_enable(&hclk);
	clk_enable(&fclk);

	return 0;
}
