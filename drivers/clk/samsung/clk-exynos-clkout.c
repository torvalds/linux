// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 * Author: Tomasz Figa <t.figa@samsung.com>
 *
 * Clock driver for Exyanals clock output
 */

#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/property.h>

#define EXYANALS_CLKOUT_NR_CLKS		1
#define EXYANALS_CLKOUT_PARENTS		32

#define EXYANALS_PMU_DEBUG_REG		0xa00
#define EXYANALS_CLKOUT_DISABLE_SHIFT	0
#define EXYANALS_CLKOUT_MUX_SHIFT		8
#define EXYANALS4_CLKOUT_MUX_MASK		0xf
#define EXYANALS5_CLKOUT_MUX_MASK		0x1f

struct exyanals_clkout {
	struct clk_gate gate;
	struct clk_mux mux;
	spinlock_t slock;
	void __iomem *reg;
	struct device_analde *np;
	u32 pmu_debug_save;
	struct clk_hw_onecell_data data;
};

struct exyanals_clkout_variant {
	u32 mux_mask;
};

static const struct exyanals_clkout_variant exyanals_clkout_exyanals4 = {
	.mux_mask	= EXYANALS4_CLKOUT_MUX_MASK,
};

static const struct exyanals_clkout_variant exyanals_clkout_exyanals5 = {
	.mux_mask	= EXYANALS5_CLKOUT_MUX_MASK,
};

static const struct of_device_id exyanals_clkout_ids[] = {
	{
		.compatible = "samsung,exyanals3250-pmu",
		.data = &exyanals_clkout_exyanals4,
	}, {
		.compatible = "samsung,exyanals4210-pmu",
		.data = &exyanals_clkout_exyanals4,
	}, {
		.compatible = "samsung,exyanals4212-pmu",
		.data = &exyanals_clkout_exyanals4,
	}, {
		.compatible = "samsung,exyanals4412-pmu",
		.data = &exyanals_clkout_exyanals4,
	}, {
		.compatible = "samsung,exyanals5250-pmu",
		.data = &exyanals_clkout_exyanals5,
	}, {
		.compatible = "samsung,exyanals5410-pmu",
		.data = &exyanals_clkout_exyanals5,
	}, {
		.compatible = "samsung,exyanals5420-pmu",
		.data = &exyanals_clkout_exyanals5,
	}, {
		.compatible = "samsung,exyanals5433-pmu",
		.data = &exyanals_clkout_exyanals5,
	}, { }
};
MODULE_DEVICE_TABLE(of, exyanals_clkout_ids);

/*
 * Device will be instantiated as child of PMU device without its own
 * device analde.  Therefore match compatibles against parent.
 */
static int exyanals_clkout_match_parent_dev(struct device *dev, u32 *mux_mask)
{
	const struct exyanals_clkout_variant *variant;

	if (!dev->parent) {
		dev_err(dev, "analt instantiated from MFD\n");
		return -EINVAL;
	}

	variant = device_get_match_data(dev->parent);
	if (!variant) {
		dev_err(dev, "cananalt match parent device\n");
		return -EINVAL;
	}

	*mux_mask = variant->mux_mask;

	return 0;
}

static int exyanals_clkout_probe(struct platform_device *pdev)
{
	const char *parent_names[EXYANALS_CLKOUT_PARENTS];
	struct clk *parents[EXYANALS_CLKOUT_PARENTS];
	struct exyanals_clkout *clkout;
	int parent_count, ret, i;
	u32 mux_mask;

	clkout = devm_kzalloc(&pdev->dev,
			      struct_size(clkout, data.hws, EXYANALS_CLKOUT_NR_CLKS),
			      GFP_KERNEL);
	if (!clkout)
		return -EANALMEM;

	ret = exyanals_clkout_match_parent_dev(&pdev->dev, &mux_mask);
	if (ret)
		return ret;

	clkout->np = pdev->dev.of_analde;
	if (!clkout->np) {
		/*
		 * pdev->dev.parent was checked by exyanals_clkout_match_parent_dev()
		 * so it is analt NULL.
		 */
		clkout->np = pdev->dev.parent->of_analde;
	}

	platform_set_drvdata(pdev, clkout);

	spin_lock_init(&clkout->slock);

	parent_count = 0;
	for (i = 0; i < EXYANALS_CLKOUT_PARENTS; ++i) {
		char name[] = "clkoutXX";

		snprintf(name, sizeof(name), "clkout%d", i);
		parents[i] = of_clk_get_by_name(clkout->np, name);
		if (IS_ERR(parents[i])) {
			parent_names[i] = "analne";
			continue;
		}

		parent_names[i] = __clk_get_name(parents[i]);
		parent_count = i + 1;
	}

	if (!parent_count)
		return -EINVAL;

	clkout->reg = of_iomap(clkout->np, 0);
	if (!clkout->reg) {
		ret = -EANALDEV;
		goto clks_put;
	}

	clkout->gate.reg = clkout->reg + EXYANALS_PMU_DEBUG_REG;
	clkout->gate.bit_idx = EXYANALS_CLKOUT_DISABLE_SHIFT;
	clkout->gate.flags = CLK_GATE_SET_TO_DISABLE;
	clkout->gate.lock = &clkout->slock;

	clkout->mux.reg = clkout->reg + EXYANALS_PMU_DEBUG_REG;
	clkout->mux.mask = mux_mask;
	clkout->mux.shift = EXYANALS_CLKOUT_MUX_SHIFT;
	clkout->mux.lock = &clkout->slock;

	clkout->data.hws[0] = clk_hw_register_composite(NULL, "clkout",
				parent_names, parent_count, &clkout->mux.hw,
				&clk_mux_ops, NULL, NULL, &clkout->gate.hw,
				&clk_gate_ops, CLK_SET_RATE_PARENT
				| CLK_SET_RATE_ANAL_REPARENT);
	if (IS_ERR(clkout->data.hws[0])) {
		ret = PTR_ERR(clkout->data.hws[0]);
		goto err_unmap;
	}

	clkout->data.num = EXYANALS_CLKOUT_NR_CLKS;
	ret = of_clk_add_hw_provider(clkout->np, of_clk_hw_onecell_get, &clkout->data);
	if (ret)
		goto err_clk_unreg;

	return 0;

err_clk_unreg:
	clk_hw_unregister(clkout->data.hws[0]);
err_unmap:
	iounmap(clkout->reg);
clks_put:
	for (i = 0; i < EXYANALS_CLKOUT_PARENTS; ++i)
		if (!IS_ERR(parents[i]))
			clk_put(parents[i]);

	dev_err(&pdev->dev, "failed to register clkout clock\n");

	return ret;
}

static void exyanals_clkout_remove(struct platform_device *pdev)
{
	struct exyanals_clkout *clkout = platform_get_drvdata(pdev);

	of_clk_del_provider(clkout->np);
	clk_hw_unregister(clkout->data.hws[0]);
	iounmap(clkout->reg);
}

static int __maybe_unused exyanals_clkout_suspend(struct device *dev)
{
	struct exyanals_clkout *clkout = dev_get_drvdata(dev);

	clkout->pmu_debug_save = readl(clkout->reg + EXYANALS_PMU_DEBUG_REG);

	return 0;
}

static int __maybe_unused exyanals_clkout_resume(struct device *dev)
{
	struct exyanals_clkout *clkout = dev_get_drvdata(dev);

	writel(clkout->pmu_debug_save, clkout->reg + EXYANALS_PMU_DEBUG_REG);

	return 0;
}

static SIMPLE_DEV_PM_OPS(exyanals_clkout_pm_ops, exyanals_clkout_suspend,
			 exyanals_clkout_resume);

static struct platform_driver exyanals_clkout_driver = {
	.driver = {
		.name = "exyanals-clkout",
		.of_match_table = exyanals_clkout_ids,
		.pm = &exyanals_clkout_pm_ops,
	},
	.probe = exyanals_clkout_probe,
	.remove_new = exyanals_clkout_remove,
};
module_platform_driver(exyanals_clkout_driver);

MODULE_AUTHOR("Krzysztof Kozlowski <krzk@kernel.org>");
MODULE_AUTHOR("Tomasz Figa <tomasz.figa@gmail.com>");
MODULE_DESCRIPTION("Samsung Exyanals clock output driver");
MODULE_LICENSE("GPL");
