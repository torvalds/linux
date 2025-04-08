// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2017, The Linux Foundation. All rights reserved.
 */

#include <linux/bitops.h>
#include <linux/cleanup.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/log2.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/types.h>

#define REG_DIV_CTL1			0x43
#define DIV_CTL1_DIV_FACTOR_MASK	GENMASK(2, 0)

#define REG_EN_CTL			0x46
#define REG_EN_MASK			BIT(7)

struct clkdiv {
	struct regmap		*regmap;
	u16			base;
	spinlock_t		lock;

	struct clk_hw		hw;
	unsigned int		cxo_period_ns;
};

static inline struct clkdiv *to_clkdiv(struct clk_hw *hw)
{
	return container_of(hw, struct clkdiv, hw);
}

static inline unsigned int div_factor_to_div(unsigned int div_factor)
{
	if (!div_factor)
		div_factor = 1;

	return 1 << (div_factor - 1);
}

static inline unsigned int div_to_div_factor(unsigned int div)
{
	return min(ilog2(div) + 1, 7);
}

static bool is_spmi_pmic_clkdiv_enabled(struct clkdiv *clkdiv)
{
	unsigned int val = 0;

	regmap_read(clkdiv->regmap, clkdiv->base + REG_EN_CTL, &val);

	return val & REG_EN_MASK;
}

static int
__spmi_pmic_clkdiv_set_enable_state(struct clkdiv *clkdiv, bool enable,
				    unsigned int div_factor)
{
	int ret;
	unsigned int ns = clkdiv->cxo_period_ns;
	unsigned int div = div_factor_to_div(div_factor);

	ret = regmap_update_bits(clkdiv->regmap, clkdiv->base + REG_EN_CTL,
				 REG_EN_MASK, enable ? REG_EN_MASK : 0);
	if (ret)
		return ret;

	if (enable)
		ndelay((2 + 3 * div) * ns);
	else
		ndelay(3 * div * ns);

	return 0;
}

static int spmi_pmic_clkdiv_set_enable_state(struct clkdiv *clkdiv, bool enable)
{
	unsigned int div_factor;

	regmap_read(clkdiv->regmap, clkdiv->base + REG_DIV_CTL1, &div_factor);
	div_factor &= DIV_CTL1_DIV_FACTOR_MASK;

	return __spmi_pmic_clkdiv_set_enable_state(clkdiv, enable, div_factor);
}

static int clk_spmi_pmic_div_enable(struct clk_hw *hw)
{
	struct clkdiv *clkdiv = to_clkdiv(hw);
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&clkdiv->lock, flags);
	ret = spmi_pmic_clkdiv_set_enable_state(clkdiv, true);
	spin_unlock_irqrestore(&clkdiv->lock, flags);

	return ret;
}

static void clk_spmi_pmic_div_disable(struct clk_hw *hw)
{
	struct clkdiv *clkdiv = to_clkdiv(hw);
	unsigned long flags;

	spin_lock_irqsave(&clkdiv->lock, flags);
	spmi_pmic_clkdiv_set_enable_state(clkdiv, false);
	spin_unlock_irqrestore(&clkdiv->lock, flags);
}

static long clk_spmi_pmic_div_round_rate(struct clk_hw *hw, unsigned long rate,
					 unsigned long *parent_rate)
{
	unsigned int div, div_factor;

	div = DIV_ROUND_UP(*parent_rate, rate);
	div_factor = div_to_div_factor(div);
	div = div_factor_to_div(div_factor);

	return *parent_rate / div;
}

static unsigned long
clk_spmi_pmic_div_recalc_rate(struct clk_hw *hw, unsigned long parent_rate)
{
	struct clkdiv *clkdiv = to_clkdiv(hw);
	unsigned int div_factor;

	regmap_read(clkdiv->regmap, clkdiv->base + REG_DIV_CTL1, &div_factor);
	div_factor &= DIV_CTL1_DIV_FACTOR_MASK;

	return parent_rate / div_factor_to_div(div_factor);
}

static int clk_spmi_pmic_div_set_rate(struct clk_hw *hw, unsigned long rate,
				      unsigned long parent_rate)
{
	struct clkdiv *clkdiv = to_clkdiv(hw);
	unsigned int div_factor = div_to_div_factor(parent_rate / rate);
	bool enabled;
	int ret;

	guard(spinlock_irqsave)(&clkdiv->lock);

	enabled = is_spmi_pmic_clkdiv_enabled(clkdiv);
	if (enabled) {
		ret = spmi_pmic_clkdiv_set_enable_state(clkdiv, false);
		if (ret)
			return ret;
	}

	ret = regmap_update_bits(clkdiv->regmap, clkdiv->base + REG_DIV_CTL1,
				 DIV_CTL1_DIV_FACTOR_MASK, div_factor);
	if (ret)
		return ret;

	if (enabled)
		ret = __spmi_pmic_clkdiv_set_enable_state(clkdiv, true,
							  div_factor);
	return ret;
}

static const struct clk_ops clk_spmi_pmic_div_ops = {
	.enable = clk_spmi_pmic_div_enable,
	.disable = clk_spmi_pmic_div_disable,
	.set_rate = clk_spmi_pmic_div_set_rate,
	.recalc_rate = clk_spmi_pmic_div_recalc_rate,
	.round_rate = clk_spmi_pmic_div_round_rate,
};

struct spmi_pmic_div_clk_cc {
	int		nclks;
	struct clkdiv	clks[] __counted_by(nclks);
};

static struct clk_hw *
spmi_pmic_div_clk_hw_get(struct of_phandle_args *clkspec, void *data)
{
	struct spmi_pmic_div_clk_cc *cc = data;
	int idx = clkspec->args[0] - 1; /* Start at 1 instead of 0 */

	if (idx < 0 || idx >= cc->nclks) {
		pr_err("%s: index value %u is invalid; allowed range [1, %d]\n",
		       __func__, clkspec->args[0], cc->nclks);
		return ERR_PTR(-EINVAL);
	}

	return &cc->clks[idx].hw;
}

static int spmi_pmic_clkdiv_probe(struct platform_device *pdev)
{
	struct spmi_pmic_div_clk_cc *cc;
	struct clk_init_data init = {};
	struct clkdiv *clkdiv;
	struct clk *cxo;
	struct regmap *regmap;
	struct device *dev = &pdev->dev;
	struct device_node *of_node = dev->of_node;
	struct clk_parent_data parent_data = { .index = 0, };
	int nclks, i, ret, cxo_hz;
	char name[20];
	u32 start;

	ret = of_property_read_u32(of_node, "reg", &start);
	if (ret < 0) {
		dev_err(dev, "reg property reading failed\n");
		return ret;
	}

	regmap = dev_get_regmap(dev->parent, NULL);
	if (!regmap) {
		dev_err(dev, "Couldn't get parent's regmap\n");
		return -EINVAL;
	}

	ret = of_property_read_u32(of_node, "qcom,num-clkdivs", &nclks);
	if (ret < 0) {
		dev_err(dev, "qcom,num-clkdivs property reading failed, ret=%d\n",
			ret);
		return ret;
	}

	if (!nclks)
		return -EINVAL;

	cc = devm_kzalloc(dev, struct_size(cc, clks, nclks), GFP_KERNEL);
	if (!cc)
		return -ENOMEM;
	cc->nclks = nclks;

	cxo = clk_get(dev, "xo");
	if (IS_ERR(cxo)) {
		ret = PTR_ERR(cxo);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "failed to get xo clock\n");
		return ret;
	}
	cxo_hz = clk_get_rate(cxo);
	clk_put(cxo);

	init.name = name;
	init.parent_data = &parent_data;
	init.num_parents = 1;
	init.ops = &clk_spmi_pmic_div_ops;

	for (i = 0, clkdiv = cc->clks; i < nclks; i++) {
		snprintf(name, sizeof(name), "div_clk%d", i + 1);

		spin_lock_init(&clkdiv[i].lock);
		clkdiv[i].base = start + i * 0x100;
		clkdiv[i].regmap = regmap;
		clkdiv[i].cxo_period_ns = NSEC_PER_SEC / cxo_hz;
		clkdiv[i].hw.init = &init;

		ret = devm_clk_hw_register(dev, &clkdiv[i].hw);
		if (ret)
			return ret;
	}

	return devm_of_clk_add_hw_provider(dev, spmi_pmic_div_clk_hw_get, cc);
}

static const struct of_device_id spmi_pmic_clkdiv_match_table[] = {
	{ .compatible = "qcom,spmi-clkdiv" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, spmi_pmic_clkdiv_match_table);

static struct platform_driver spmi_pmic_clkdiv_driver = {
	.driver		= {
		.name	= "qcom,spmi-pmic-clkdiv",
		.of_match_table = spmi_pmic_clkdiv_match_table,
	},
	.probe		= spmi_pmic_clkdiv_probe,
};
module_platform_driver(spmi_pmic_clkdiv_driver);

MODULE_DESCRIPTION("QCOM SPMI PMIC clkdiv driver");
MODULE_LICENSE("GPL v2");
