/*
 * Copyright (c) 2013-2014, The Linux Foundation. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/export.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/platform_device.h>
#include <linux/clk-provider.h>
#include <linux/reset-controller.h>
#include <linux/of.h>

#include "common.h"
#include "clk-rcg.h"
#include "clk-regmap.h"
#include "reset.h"
#include "gdsc.h"

struct qcom_cc {
	struct qcom_reset_controller reset;
	struct clk_onecell_data data;
	struct clk *clks[];
};

const
struct freq_tbl *qcom_find_freq(const struct freq_tbl *f, unsigned long rate)
{
	if (!f)
		return NULL;

	for (; f->freq; f++)
		if (rate <= f->freq)
			return f;

	/* Default to our fastest rate */
	return f - 1;
}
EXPORT_SYMBOL_GPL(qcom_find_freq);

int qcom_find_src_index(struct clk_hw *hw, const struct parent_map *map, u8 src)
{
	int i, num_parents = clk_hw_get_num_parents(hw);

	for (i = 0; i < num_parents; i++)
		if (src == map[i].src)
			return i;

	return -ENOENT;
}
EXPORT_SYMBOL_GPL(qcom_find_src_index);

struct regmap *
qcom_cc_map(struct platform_device *pdev, const struct qcom_cc_desc *desc)
{
	void __iomem *base;
	struct resource *res;
	struct device *dev = &pdev->dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(dev, res);
	if (IS_ERR(base))
		return ERR_CAST(base);

	return devm_regmap_init_mmio(dev, base, desc->config);
}
EXPORT_SYMBOL_GPL(qcom_cc_map);

static void qcom_cc_del_clk_provider(void *data)
{
	of_clk_del_provider(data);
}

static void qcom_cc_reset_unregister(void *data)
{
	reset_controller_unregister(data);
}

static void qcom_cc_gdsc_unregister(void *data)
{
	gdsc_unregister(data);
}

/*
 * Backwards compatibility with old DTs. Register a pass-through factor 1/1
 * clock to translate 'path' clk into 'name' clk and regsiter the 'path'
 * clk as a fixed rate clock if it isn't present.
 */
static int _qcom_cc_register_board_clk(struct device *dev, const char *path,
				       const char *name, unsigned long rate,
				       bool add_factor)
{
	struct device_node *node = NULL;
	struct device_node *clocks_node;
	struct clk_fixed_factor *factor;
	struct clk_fixed_rate *fixed;
	struct clk *clk;
	struct clk_init_data init_data = { };

	clocks_node = of_find_node_by_path("/clocks");
	if (clocks_node)
		node = of_find_node_by_name(clocks_node, path);
	of_node_put(clocks_node);

	if (!node) {
		fixed = devm_kzalloc(dev, sizeof(*fixed), GFP_KERNEL);
		if (!fixed)
			return -EINVAL;

		fixed->fixed_rate = rate;
		fixed->hw.init = &init_data;

		init_data.name = path;
		init_data.flags = CLK_IS_ROOT;
		init_data.ops = &clk_fixed_rate_ops;

		clk = devm_clk_register(dev, &fixed->hw);
		if (IS_ERR(clk))
			return PTR_ERR(clk);
	}
	of_node_put(node);

	if (add_factor) {
		factor = devm_kzalloc(dev, sizeof(*factor), GFP_KERNEL);
		if (!factor)
			return -EINVAL;

		factor->mult = factor->div = 1;
		factor->hw.init = &init_data;

		init_data.name = name;
		init_data.parent_names = &path;
		init_data.num_parents = 1;
		init_data.flags = 0;
		init_data.ops = &clk_fixed_factor_ops;

		clk = devm_clk_register(dev, &factor->hw);
		if (IS_ERR(clk))
			return PTR_ERR(clk);
	}

	return 0;
}

int qcom_cc_register_board_clk(struct device *dev, const char *path,
			       const char *name, unsigned long rate)
{
	bool add_factor = true;
	struct device_node *node;

	/* The RPM clock driver will add the factor clock if present */
	if (IS_ENABLED(CONFIG_QCOM_RPMCC)) {
		node = of_find_compatible_node(NULL, NULL, "qcom,rpmcc");
		if (of_device_is_available(node))
			add_factor = false;
		of_node_put(node);
	}

	return _qcom_cc_register_board_clk(dev, path, name, rate, add_factor);
}
EXPORT_SYMBOL_GPL(qcom_cc_register_board_clk);

int qcom_cc_register_sleep_clk(struct device *dev)
{
	return _qcom_cc_register_board_clk(dev, "sleep_clk", "sleep_clk_src",
					   32768, true);
}
EXPORT_SYMBOL_GPL(qcom_cc_register_sleep_clk);

int qcom_cc_really_probe(struct platform_device *pdev,
			 const struct qcom_cc_desc *desc, struct regmap *regmap)
{
	int i, ret;
	struct device *dev = &pdev->dev;
	struct clk *clk;
	struct clk_onecell_data *data;
	struct clk **clks;
	struct qcom_reset_controller *reset;
	struct qcom_cc *cc;
	size_t num_clks = desc->num_clks;
	struct clk_regmap **rclks = desc->clks;

	cc = devm_kzalloc(dev, sizeof(*cc) + sizeof(*clks) * num_clks,
			  GFP_KERNEL);
	if (!cc)
		return -ENOMEM;

	clks = cc->clks;
	data = &cc->data;
	data->clks = clks;
	data->clk_num = num_clks;

	for (i = 0; i < num_clks; i++) {
		if (!rclks[i]) {
			clks[i] = ERR_PTR(-ENOENT);
			continue;
		}
		clk = devm_clk_register_regmap(dev, rclks[i]);
		if (IS_ERR(clk))
			return PTR_ERR(clk);
		clks[i] = clk;
	}

	ret = of_clk_add_provider(dev->of_node, of_clk_src_onecell_get, data);
	if (ret)
		return ret;

	devm_add_action(dev, qcom_cc_del_clk_provider, pdev->dev.of_node);

	reset = &cc->reset;
	reset->rcdev.of_node = dev->of_node;
	reset->rcdev.ops = &qcom_reset_ops;
	reset->rcdev.owner = dev->driver->owner;
	reset->rcdev.nr_resets = desc->num_resets;
	reset->regmap = regmap;
	reset->reset_map = desc->resets;

	ret = reset_controller_register(&reset->rcdev);
	if (ret)
		return ret;

	devm_add_action(dev, qcom_cc_reset_unregister, &reset->rcdev);

	if (desc->gdscs && desc->num_gdscs) {
		ret = gdsc_register(dev, desc->gdscs, desc->num_gdscs,
				    &reset->rcdev, regmap);
		if (ret)
			return ret;
	}

	devm_add_action(dev, qcom_cc_gdsc_unregister, dev);


	return 0;
}
EXPORT_SYMBOL_GPL(qcom_cc_really_probe);

int qcom_cc_probe(struct platform_device *pdev, const struct qcom_cc_desc *desc)
{
	struct regmap *regmap;

	regmap = qcom_cc_map(pdev, desc);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	return qcom_cc_really_probe(pdev, desc, regmap);
}
EXPORT_SYMBOL_GPL(qcom_cc_probe);

MODULE_LICENSE("GPL v2");
