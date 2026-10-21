// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2026, Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/clk-provider.h>
#include <linux/clk/qcom.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>

#define QCOM_CLK_REF_EN_MASK BIT(0)

struct qcom_clk_ref_provider {
	size_t num_refs;
	struct qcom_clk_ref refs[] __counted_by(num_refs);
};

static inline struct qcom_clk_ref *to_qcom_clk_ref(struct clk_hw *hw)
{
	return container_of(hw, struct qcom_clk_ref, hw);
}

static const struct clk_parent_data qcom_clk_ref_parent_data = {
	.index = 0,
};

static int qcom_clk_ref_prepare(struct clk_hw *hw)
{
	struct qcom_clk_ref *rclk = to_qcom_clk_ref(hw);
	int ret;

	if (!rclk->desc.num_regulators)
		return 0;

	ret = regulator_bulk_enable(rclk->desc.num_regulators, rclk->regulators);
	if (ret)
		pr_err("Failed to enable regulators for %s: %d\n",
		       clk_hw_get_name(hw), ret);

	return ret;
}

static void qcom_clk_ref_unprepare(struct clk_hw *hw)
{
	struct qcom_clk_ref *rclk = to_qcom_clk_ref(hw);

	if (rclk->desc.num_regulators)
		regulator_bulk_disable(rclk->desc.num_regulators, rclk->regulators);
}

static int qcom_clk_ref_enable(struct clk_hw *hw)
{
	struct qcom_clk_ref *rclk = to_qcom_clk_ref(hw);
	int ret;

	ret = regmap_set_bits(rclk->regmap, rclk->desc.offset, QCOM_CLK_REF_EN_MASK);
	if (ret)
		return ret;

	udelay(10);

	return 0;
}

static void qcom_clk_ref_disable(struct clk_hw *hw)
{
	struct qcom_clk_ref *rclk = to_qcom_clk_ref(hw);

	regmap_clear_bits(rclk->regmap, rclk->desc.offset, QCOM_CLK_REF_EN_MASK);
	udelay(10);
}

static int qcom_clk_ref_is_enabled(struct clk_hw *hw)
{
	struct qcom_clk_ref *rclk = to_qcom_clk_ref(hw);
	u32 val;
	int ret;

	ret = regmap_read(rclk->regmap, rclk->desc.offset, &val);
	if (ret)
		return 0;

	return !!(val & QCOM_CLK_REF_EN_MASK);
}

static const struct clk_ops qcom_clk_ref_ops = {
	.prepare = qcom_clk_ref_prepare,
	.unprepare = qcom_clk_ref_unprepare,
	.enable = qcom_clk_ref_enable,
	.disable = qcom_clk_ref_disable,
	.is_enabled = qcom_clk_ref_is_enabled,
};

static int qcom_clk_ref_register(struct device *dev, struct regmap *regmap,
				 struct qcom_clk_ref *clk_refs,
				 const struct qcom_clk_ref_desc * const *descs,
				 size_t num_clk_refs)
{
	const struct qcom_clk_ref_desc *desc;
	struct clk_init_data init_data = {};
	struct qcom_clk_ref *clk_ref;
	size_t clk_idx;
	unsigned int i;
	int ret;

	for (clk_idx = 0; clk_idx < num_clk_refs; clk_idx++) {
		clk_ref = &clk_refs[clk_idx];
		desc = descs[clk_idx];

		/* Skip unpopulated indices; the array is indexed by clock ID. */
		if (!desc)
			continue;

		if (WARN_ON(!desc->name))
			continue;

		clk_ref->regmap = regmap;
		clk_ref->desc = *desc;

		if (clk_ref->desc.num_regulators) {
			clk_ref->regulators = devm_kcalloc(dev, clk_ref->desc.num_regulators,
							   sizeof(*clk_ref->regulators),
							   GFP_KERNEL);
			if (!clk_ref->regulators)
				return -ENOMEM;

			for (i = 0; i < clk_ref->desc.num_regulators; i++)
				clk_ref->regulators[i].supply =
					clk_ref->desc.regulator_names[i];

			ret = devm_regulator_bulk_get(dev, clk_ref->desc.num_regulators,
						      clk_ref->regulators);
			if (ret)
				return dev_err_probe(dev, ret,
						     "Failed to get regulators for %s\n",
						     clk_ref->desc.name);
		}

		init_data.name = clk_ref->desc.name;
		init_data.parent_data = &qcom_clk_ref_parent_data;
		init_data.num_parents = 1;
		init_data.ops = &qcom_clk_ref_ops;
		clk_ref->hw.init = &init_data;

		ret = devm_clk_hw_register(dev, &clk_ref->hw);
		if (ret)
			return ret;
	}

	return 0;
}

static struct clk_hw *qcom_clk_ref_provider_get(struct of_phandle_args *clkspec, void *data)
{
	struct qcom_clk_ref_provider *provider = data;
	unsigned int idx = clkspec->args[0];

	if (idx >= provider->num_refs)
		return ERR_PTR(-EINVAL);

	if (!provider->refs[idx].regmap)
		return ERR_PTR(-ENOENT);

	return &provider->refs[idx].hw;
}

int qcom_clk_ref_probe(struct platform_device *pdev,
		       const struct regmap_config *config,
		       const struct qcom_clk_ref_desc * const *descs,
		       size_t num_clk_refs)
{
	struct qcom_clk_ref_provider *provider;
	struct device *dev = &pdev->dev;
	struct regmap *regmap;
	void __iomem *base;
	int ret;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	regmap = devm_regmap_init_mmio(dev, base, config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	provider = devm_kzalloc(dev, struct_size(provider, refs, num_clk_refs),
				GFP_KERNEL);
	if (!provider)
		return -ENOMEM;

	provider->num_refs = num_clk_refs;

	ret = qcom_clk_ref_register(dev, regmap, provider->refs, descs,
				    provider->num_refs);
	if (ret)
		return ret;

	return devm_of_clk_add_hw_provider(dev, qcom_clk_ref_provider_get, provider);
}
EXPORT_SYMBOL_GPL(qcom_clk_ref_probe);
