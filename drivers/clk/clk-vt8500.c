/*
 * Clock implementation for VIA/Wondermedia SoC's
 * Copyright (C) 2012 Tony Prisk <linux@prisktech.co.nz>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/io.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/bitops.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>

/* All clocks share the same lock as none can be changed concurrently */
static DEFINE_SPINLOCK(_lock);

struct clk_device {
	struct clk_hw	hw;
	void __iomem	*div_reg;
	unsigned int	div_mask;
	void __iomem	*en_reg;
	int		en_bit;
	spinlock_t	*lock;
};

/*
 * Add new PLL_TYPE_x definitions here as required. Use the first known model
 * to support the new type as the name.
 * Add case statements to vtwm_pll_recalc_rate(), vtwm_pll_round_round() and
 * vtwm_pll_set_rate() to handle the new PLL_TYPE_x
 */

#define PLL_TYPE_VT8500		0
#define PLL_TYPE_WM8650		1
#define PLL_TYPE_WM8750		2

struct clk_pll {
	struct clk_hw	hw;
	void __iomem	*reg;
	spinlock_t	*lock;
	int		type;
};

static void __iomem *pmc_base;

#define to_clk_device(_hw) container_of(_hw, struct clk_device, hw)

#define VT8500_PMC_BUSY_MASK		0x18

static void vt8500_pmc_wait_busy(void)
{
	while (readl(pmc_base) & VT8500_PMC_BUSY_MASK)
		cpu_relax();
}

static int vt8500_dclk_enable(struct clk_hw *hw)
{
	struct clk_device *cdev = to_clk_device(hw);
	u32 en_val;
	unsigned long flags = 0;

	spin_lock_irqsave(cdev->lock, flags);

	en_val = readl(cdev->en_reg);
	en_val |= BIT(cdev->en_bit);
	writel(en_val, cdev->en_reg);

	spin_unlock_irqrestore(cdev->lock, flags);
	return 0;
}

static void vt8500_dclk_disable(struct clk_hw *hw)
{
	struct clk_device *cdev = to_clk_device(hw);
	u32 en_val;
	unsigned long flags = 0;

	spin_lock_irqsave(cdev->lock, flags);

	en_val = readl(cdev->en_reg);
	en_val &= ~BIT(cdev->en_bit);
	writel(en_val, cdev->en_reg);

	spin_unlock_irqrestore(cdev->lock, flags);
}

static int vt8500_dclk_is_enabled(struct clk_hw *hw)
{
	struct clk_device *cdev = to_clk_device(hw);
	u32 en_val = (readl(cdev->en_reg) & BIT(cdev->en_bit));

	return en_val ? 1 : 0;
}

static unsigned long vt8500_dclk_recalc_rate(struct clk_hw *hw,
				unsigned long parent_rate)
{
	struct clk_device *cdev = to_clk_device(hw);
	u32 div = readl(cdev->div_reg) & cdev->div_mask;

	/* Special case for SDMMC devices */
	if ((cdev->div_mask == 0x3F) && (div & BIT(5)))
		div = 64 * (div & 0x1f);

	/* div == 0 is actually the highest divisor */
	if (div == 0)
		div = (cdev->div_mask + 1);

	return parent_rate / div;
}

static long vt8500_dclk_round_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long *prate)
{
	struct clk_device *cdev = to_clk_device(hw);
	u32 divisor;

	if (rate == 0)
		return 0;

	divisor = *prate / rate;

	/* If prate / rate would be decimal, incr the divisor */
	if (rate * divisor < *prate)
		divisor++;

	/*
	 * If this is a request for SDMMC we have to adjust the divisor
	 * when >31 to use the fixed predivisor
	 */
	if ((cdev->div_mask == 0x3F) && (divisor > 31)) {
		divisor = 64 * ((divisor / 64) + 1);
	}

	return *prate / divisor;
}

static int vt8500_dclk_set_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long parent_rate)
{
	struct clk_device *cdev = to_clk_device(hw);
	u32 divisor;
	unsigned long flags = 0;

	if (rate == 0)
		return 0;

	divisor =  parent_rate / rate;

	/* If prate / rate would be decimal, incr the divisor */
	if (rate * divisor < *prate)
		divisor++;

	if (divisor == cdev->div_mask + 1)
		divisor = 0;

	/* SDMMC mask may need to be corrected before testing if its valid */
	if ((cdev->div_mask == 0x3F) && (divisor > 31)) {
		/*
		 * Bit 5 is a fixed /64 predivisor. If the requested divisor
		 * is >31 then correct for the fixed divisor being required.
		 */
		divisor = 0x20 + (divisor / 64);
	}

	if (divisor > cdev->div_mask) {
		pr_err("%s: invalid divisor for clock\n", __func__);
		return -EINVAL;
	}

	spin_lock_irqsave(cdev->lock, flags);

	vt8500_pmc_wait_busy();
	writel(divisor, cdev->div_reg);
	vt8500_pmc_wait_busy();

	spin_lock_irqsave(cdev->lock, flags);

	return 0;
}


static const struct clk_ops vt8500_gated_clk_ops = {
	.enable = vt8500_dclk_enable,
	.disable = vt8500_dclk_disable,
	.is_enabled = vt8500_dclk_is_enabled,
};

static const struct clk_ops vt8500_divisor_clk_ops = {
	.round_rate = vt8500_dclk_round_rate,
	.set_rate = vt8500_dclk_set_rate,
	.recalc_rate = vt8500_dclk_recalc_rate,
};

static const struct clk_ops vt8500_gated_divisor_clk_ops = {
	.enable = vt8500_dclk_enable,
	.disable = vt8500_dclk_disable,
	.is_enabled = vt8500_dclk_is_enabled,
	.round_rate = vt8500_dclk_round_rate,
	.set_rate = vt8500_dclk_set_rate,
	.recalc_rate = vt8500_dclk_recalc_rate,
};

#define CLK_INIT_GATED			BIT(0)
#define CLK_INIT_DIVISOR		BIT(1)
#define CLK_INIT_GATED_DIVISOR		(CLK_INIT_DIVISOR | CLK_INIT_GATED)

static __init void vtwm_device_clk_init(struct device_node *node)
{
	u32 en_reg, div_reg;
	struct clk *clk;
	struct clk_device *dev_clk;
	const char *clk_name = node->name;
	const char *parent_name;
	struct clk_init_data init;
	int rc;
	int clk_init_flags = 0;

	dev_clk = kzalloc(sizeof(*dev_clk), GFP_KERNEL);
	if (WARN_ON(!dev_clk))
		return;

	dev_clk->lock = &_lock;

	rc = of_property_read_u32(node, "enable-reg", &en_reg);
	if (!rc) {
		dev_clk->en_reg = pmc_base + en_reg;
		rc = of_property_read_u32(node, "enable-bit", &dev_clk->en_bit);
		if (rc) {
			pr_err("%s: enable-bit property required for gated clock\n",
								__func__);
			return;
		}
		clk_init_flags |= CLK_INIT_GATED;
	}

	rc = of_property_read_u32(node, "divisor-reg", &div_reg);
	if (!rc) {
		dev_clk->div_reg = pmc_base + div_reg;
		/*
		 * use 0x1f as the default mask since it covers
		 * almost all the clocks and reduces dts properties
		 */
		dev_clk->div_mask = 0x1f;

		of_property_read_u32(node, "divisor-mask", &dev_clk->div_mask);
		clk_init_flags |= CLK_INIT_DIVISOR;
	}

	of_property_read_string(node, "clock-output-names", &clk_name);

	switch (clk_init_flags) {
	case CLK_INIT_GATED:
		init.ops = &vt8500_gated_clk_ops;
		break;
	case CLK_INIT_DIVISOR:
		init.ops = &vt8500_divisor_clk_ops;
		break;
	case CLK_INIT_GATED_DIVISOR:
		init.ops = &vt8500_gated_divisor_clk_ops;
		break;
	default:
		pr_err("%s: Invalid clock description in device tree\n",
								__func__);
		kfree(dev_clk);
		return;
	}

	init.name = clk_name;
	init.flags = 0;
	parent_name = of_clk_get_parent_name(node, 0);
	init.parent_names = &parent_name;
	init.num_parents = 1;

	dev_clk->hw.init = &init;

	clk = clk_register(NULL, &dev_clk->hw);
	if (WARN_ON(IS_ERR(clk))) {
		kfree(dev_clk);
		return;
	}
	rc = of_clk_add_provider(node, of_clk_src_simple_get, clk);
	clk_register_clkdev(clk, clk_name, NULL);
}
CLK_OF_DECLARE(vt8500_device, "via,vt8500-device-clock", vtwm_device_clk_init);

/* PLL clock related functions */

#define to_clk_pll(_hw) container_of(_hw, struct clk_pll, hw)

/* Helper macros for PLL_VT8500 */
#define VT8500_PLL_MUL(x)	((x & 0x1F) << 1)
#define VT8500_PLL_DIV(x)	((x & 0x100) ? 1 : 2)

#define VT8500_BITS_TO_FREQ(r, m, d)					\
				((r / d) * m)

#define VT8500_BITS_TO_VAL(m, d)					\
				((d == 2 ? 0 : 0x100) | ((m >> 1) & 0x1F))

/* Helper macros for PLL_WM8650 */
#define WM8650_PLL_MUL(x)	(x & 0x3FF)
#define WM8650_PLL_DIV(x)	(((x >> 10) & 7) * (1 << ((x >> 13) & 3)))

#define WM8650_BITS_TO_FREQ(r, m, d1, d2)				\
				(r * m / (d1 * (1 << d2)))

#define WM8650_BITS_TO_VAL(m, d1, d2)					\
				((d2 << 13) | (d1 << 10) | (m & 0x3FF))

/* Helper macros for PLL_WM8750 */
#define WM8750_PLL_MUL(x)	(((x >> 16) & 0xFF) + 1)
#define WM8750_PLL_DIV(x)	((((x >> 8) & 1) + 1) * (1 << (x & 7)))

#define WM8750_BITS_TO_FREQ(r, m, d1, d2)				\
				(r * (m+1) / ((d1+1) * (1 << d2)))

#define WM8750_BITS_TO_VAL(f, m, d1, d2)				\
		((f << 24) | ((m - 1) << 16) | ((d1 - 1) << 8) | d2)


static void vt8500_find_pll_bits(unsigned long rate, unsigned long parent_rate,
				u32 *multiplier, u32 *prediv)
{
	unsigned long tclk;

	/* sanity check */
	if ((rate < parent_rate * 4) || (rate > parent_rate * 62)) {
		pr_err("%s: requested rate out of range\n", __func__);
		*multiplier = 0;
		*prediv = 1;
		return;
	}
	if (rate <= parent_rate * 31)
		/* use the prediv to double the resolution */
		*prediv = 2;
	else
		*prediv = 1;

	*multiplier = rate / (parent_rate / *prediv);
	tclk = (parent_rate / *prediv) * *multiplier;

	if (tclk != rate)
		pr_warn("%s: requested rate %lu, found rate %lu\n", __func__,
								rate, tclk);
}

static void wm8650_find_pll_bits(unsigned long rate, unsigned long parent_rate,
				u32 *multiplier, u32 *divisor1, u32 *divisor2)
{
	u32 mul, div1, div2;
	u32 best_mul, best_div1, best_div2;
	unsigned long tclk, rate_err, best_err;

	best_err = (unsigned long)-1;

	/* Find the closest match (lower or equal to requested) */
	for (div1 = 5; div1 >= 3; div1--)
		for (div2 = 3; div2 >= 0; div2--)
			for (mul = 3; mul <= 1023; mul++) {
				tclk = parent_rate * mul / (div1 * (1 << div2));
				if (tclk > rate)
					continue;
				/* error will always be +ve */
				rate_err = rate - tclk;
				if (rate_err == 0) {
					*multiplier = mul;
					*divisor1 = div1;
					*divisor2 = div2;
					return;
				}

				if (rate_err < best_err) {
					best_err = rate_err;
					best_mul = mul;
					best_div1 = div1;
					best_div2 = div2;
				}
			}

	/* if we got here, it wasn't an exact match */
	pr_warn("%s: requested rate %lu, found rate %lu\n", __func__, rate,
							rate - best_err);
	*multiplier = best_mul;
	*divisor1 = best_div1;
	*divisor2 = best_div2;
}

static u32 wm8750_get_filter(u32 parent_rate, u32 divisor1)
{
	/* calculate frequency (MHz) after pre-divisor */
	u32 freq = (parent_rate / 1000000) / (divisor1 + 1);

	if ((freq < 10) || (freq > 200))
		pr_warn("%s: PLL recommended input frequency 10..200Mhz (requested %d Mhz)\n",
				__func__, freq);

	if (freq >= 166)
		return 7;
	else if (freq >= 104)
		return 6;
	else if (freq >= 65)
		return 5;
	else if (freq >= 42)
		return 4;
	else if (freq >= 26)
		return 3;
	else if (freq >= 16)
		return 2;
	else if (freq >= 10)
		return 1;

	return 0;
}

static void wm8750_find_pll_bits(unsigned long rate, unsigned long parent_rate,
				u32 *filter, u32 *multiplier, u32 *divisor1, u32 *divisor2)
{
	u32 mul, div1, div2;
	u32 best_mul, best_div1, best_div2;
	unsigned long tclk, rate_err, best_err;

	best_err = (unsigned long)-1;

	/* Find the closest match (lower or equal to requested) */
	for (div1 = 1; div1 >= 0; div1--)
		for (div2 = 7; div2 >= 0; div2--)
			for (mul = 0; mul <= 255; mul++) {
				tclk = parent_rate * (mul + 1) / ((div1 + 1) * (1 << div2));
				if (tclk > rate)
					continue;
				/* error will always be +ve */
				rate_err = rate - tclk;
				if (rate_err == 0) {
					*filter = wm8750_get_filter(parent_rate, div1);
					*multiplier = mul;
					*divisor1 = div1;
					*divisor2 = div2;
					return;
				}

				if (rate_err < best_err) {
					best_err = rate_err;
					best_mul = mul;
					best_div1 = div1;
					best_div2 = div2;
				}
			}

	/* if we got here, it wasn't an exact match */
	pr_warn("%s: requested rate %lu, found rate %lu\n", __func__, rate,
							rate - best_err);

	*filter = wm8750_get_filter(parent_rate, best_div1);
	*multiplier = best_mul;
	*divisor1 = best_div1;
	*divisor2 = best_div2;
}

static int vtwm_pll_set_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long parent_rate)
{
	struct clk_pll *pll = to_clk_pll(hw);
	u32 filter, mul, div1, div2;
	u32 pll_val;
	unsigned long flags = 0;

	/* sanity check */

	switch (pll->type) {
	case PLL_TYPE_VT8500:
		vt8500_find_pll_bits(rate, parent_rate, &mul, &div1);
		pll_val = VT8500_BITS_TO_VAL(mul, div1);
		break;
	case PLL_TYPE_WM8650:
		wm8650_find_pll_bits(rate, parent_rate, &mul, &div1, &div2);
		pll_val = WM8650_BITS_TO_VAL(mul, div1, div2);
		break;
	case PLL_TYPE_WM8750:
		wm8750_find_pll_bits(rate, parent_rate, &filter, &mul, &div1, &div2);
		pll_val = WM8750_BITS_TO_VAL(filter, mul, div1, div2);
	default:
		pr_err("%s: invalid pll type\n", __func__);
		return 0;
	}

	spin_lock_irqsave(pll->lock, flags);

	vt8500_pmc_wait_busy();
	writel(pll_val, pll->reg);
	vt8500_pmc_wait_busy();

	spin_unlock_irqrestore(pll->lock, flags);

	return 0;
}

static long vtwm_pll_round_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long *prate)
{
	struct clk_pll *pll = to_clk_pll(hw);
	u32 filter, mul, div1, div2;
	long round_rate;

	switch (pll->type) {
	case PLL_TYPE_VT8500:
		vt8500_find_pll_bits(rate, *prate, &mul, &div1);
		round_rate = VT8500_BITS_TO_FREQ(*prate, mul, div1);
		break;
	case PLL_TYPE_WM8650:
		wm8650_find_pll_bits(rate, *prate, &mul, &div1, &div2);
		round_rate = WM8650_BITS_TO_FREQ(*prate, mul, div1, div2);
		break;
	case PLL_TYPE_WM8750:
		wm8750_find_pll_bits(rate, *prate, &filter, &mul, &div1, &div2);
		round_rate = WM8750_BITS_TO_FREQ(*prate, mul, div1, div2);
	default:
		round_rate = 0;
	}

	return round_rate;
}

static unsigned long vtwm_pll_recalc_rate(struct clk_hw *hw,
				unsigned long parent_rate)
{
	struct clk_pll *pll = to_clk_pll(hw);
	u32 pll_val = readl(pll->reg);
	unsigned long pll_freq;

	switch (pll->type) {
	case PLL_TYPE_VT8500:
		pll_freq = parent_rate * VT8500_PLL_MUL(pll_val);
		pll_freq /= VT8500_PLL_DIV(pll_val);
		break;
	case PLL_TYPE_WM8650:
		pll_freq = parent_rate * WM8650_PLL_MUL(pll_val);
		pll_freq /= WM8650_PLL_DIV(pll_val);
		break;
	case PLL_TYPE_WM8750:
		pll_freq = parent_rate * WM8750_PLL_MUL(pll_val);
		pll_freq /= WM8750_PLL_DIV(pll_val);
		break;
	default:
		pll_freq = 0;
	}

	return pll_freq;
}

const struct clk_ops vtwm_pll_ops = {
	.round_rate = vtwm_pll_round_rate,
	.set_rate = vtwm_pll_set_rate,
	.recalc_rate = vtwm_pll_recalc_rate,
};

static __init void vtwm_pll_clk_init(struct device_node *node, int pll_type)
{
	u32 reg;
	struct clk *clk;
	struct clk_pll *pll_clk;
	const char *clk_name = node->name;
	const char *parent_name;
	struct clk_init_data init;
	int rc;

	rc = of_property_read_u32(node, "reg", &reg);
	if (WARN_ON(rc))
		return;

	pll_clk = kzalloc(sizeof(*pll_clk), GFP_KERNEL);
	if (WARN_ON(!pll_clk))
		return;

	pll_clk->reg = pmc_base + reg;
	pll_clk->lock = &_lock;
	pll_clk->type = pll_type;

	of_property_read_string(node, "clock-output-names", &clk_name);

	init.name = clk_name;
	init.ops = &vtwm_pll_ops;
	init.flags = 0;
	parent_name = of_clk_get_parent_name(node, 0);
	init.parent_names = &parent_name;
	init.num_parents = 1;

	pll_clk->hw.init = &init;

	clk = clk_register(NULL, &pll_clk->hw);
	if (WARN_ON(IS_ERR(clk))) {
		kfree(pll_clk);
		return;
	}
	rc = of_clk_add_provider(node, of_clk_src_simple_get, clk);
	clk_register_clkdev(clk, clk_name, NULL);
}


/* Wrappers for initialization functions */

static void __init vt8500_pll_init(struct device_node *node)
{
	vtwm_pll_clk_init(node, PLL_TYPE_VT8500);
}
CLK_OF_DECLARE(vt8500_pll, "via,vt8500-pll-clock", vt8500_pll_init);

static void __init wm8650_pll_init(struct device_node *node)
{
	vtwm_pll_clk_init(node, PLL_TYPE_WM8650);
}
CLK_OF_DECLARE(wm8650_pll, "wm,wm8650-pll-clock", wm8650_pll_init);

static void __init wm8750_pll_init(struct device_node *node)
{
	vtwm_pll_clk_init(node, PLL_TYPE_WM8750);
}
CLK_OF_DECLARE(wm8750_pll, "wm,wm8750-pll-clock", wm8750_pll_init);

void __init vtwm_clk_init(void __iomem *base)
{
	if (!base)
		return;

	pmc_base = base;

	of_clk_init(NULL);
}
