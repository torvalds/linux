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
#include <linux/regmap.h>
#include <linux/platform_device.h>
#include <linux/clk-provider.h>
#include <linux/reset-controller.h>

#include "common.h"
#include "clk-rcg.h"
#include "clk-regmap.h"
#include "reset.h"

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

	reset = &cc->reset;
	reset->rcdev.of_node = dev->of_node;
	reset->rcdev.ops = &qcom_reset_ops;
	reset->rcdev.owner = dev->driver->owner;
	reset->rcdev.nr_resets = desc->num_resets;
	reset->regmap = regmap;
	reset->reset_map = desc->resets;
	platform_set_drvdata(pdev, &reset->rcdev);

	ret = reset_controller_register(&reset->rcdev);
	if (ret)
		of_clk_del_provider(dev->of_node);

	return ret;
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

void qcom_cc_remove(struct platform_device *pdev)
{
	of_clk_del_provider(pdev->dev.of_node);
	reset_controller_unregister(platform_get_drvdata(pdev));
}
EXPORT_SYMBOL_GPL(qcom_cc_remove);
