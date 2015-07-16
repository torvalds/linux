/*
 * clk-s2mps11.c - Clock driver for S2MPS11.
 *
 * Copyright (C) 2013,2014 Samsung Electornics
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/clkdev.h>
#include <linux/regmap.h>
#include <linux/clk-provider.h>
#include <linux/platform_device.h>
#include <linux/mfd/samsung/s2mps11.h>
#include <linux/mfd/samsung/s2mps13.h>
#include <linux/mfd/samsung/s2mps14.h>
#include <linux/mfd/samsung/s5m8767.h>
#include <linux/mfd/samsung/core.h>

#define s2mps11_name(a) (a->hw.init->name)

static struct clk **clk_table;
static struct clk_onecell_data clk_data;

enum {
	S2MPS11_CLK_AP = 0,
	S2MPS11_CLK_CP,
	S2MPS11_CLK_BT,
	S2MPS11_CLKS_NUM,
};

struct s2mps11_clk {
	struct sec_pmic_dev *iodev;
	struct device_node *clk_np;
	struct clk_hw hw;
	struct clk *clk;
	struct clk_lookup *lookup;
	u32 mask;
	unsigned int reg;
};

static struct s2mps11_clk *to_s2mps11_clk(struct clk_hw *hw)
{
	return container_of(hw, struct s2mps11_clk, hw);
}

static int s2mps11_clk_prepare(struct clk_hw *hw)
{
	struct s2mps11_clk *s2mps11 = to_s2mps11_clk(hw);
	int ret;

	ret = regmap_update_bits(s2mps11->iodev->regmap_pmic,
				 s2mps11->reg,
				 s2mps11->mask, s2mps11->mask);

	return ret;
}

static void s2mps11_clk_unprepare(struct clk_hw *hw)
{
	struct s2mps11_clk *s2mps11 = to_s2mps11_clk(hw);
	int ret;

	ret = regmap_update_bits(s2mps11->iodev->regmap_pmic, s2mps11->reg,
			   s2mps11->mask, ~s2mps11->mask);
}

static int s2mps11_clk_is_prepared(struct clk_hw *hw)
{
	int ret;
	u32 val;
	struct s2mps11_clk *s2mps11 = to_s2mps11_clk(hw);

	ret = regmap_read(s2mps11->iodev->regmap_pmic,
				s2mps11->reg, &val);
	if (ret < 0)
		return -EINVAL;

	return val & s2mps11->mask;
}

static unsigned long s2mps11_clk_recalc_rate(struct clk_hw *hw,
					     unsigned long parent_rate)
{
	return 32768;
}

static struct clk_ops s2mps11_clk_ops = {
	.prepare	= s2mps11_clk_prepare,
	.unprepare	= s2mps11_clk_unprepare,
	.is_prepared	= s2mps11_clk_is_prepared,
	.recalc_rate	= s2mps11_clk_recalc_rate,
};

static struct clk_init_data s2mps11_clks_init[S2MPS11_CLKS_NUM] = {
	[S2MPS11_CLK_AP] = {
		.name = "s2mps11_ap",
		.ops = &s2mps11_clk_ops,
		.flags = CLK_IS_ROOT,
	},
	[S2MPS11_CLK_CP] = {
		.name = "s2mps11_cp",
		.ops = &s2mps11_clk_ops,
		.flags = CLK_IS_ROOT,
	},
	[S2MPS11_CLK_BT] = {
		.name = "s2mps11_bt",
		.ops = &s2mps11_clk_ops,
		.flags = CLK_IS_ROOT,
	},
};

static struct clk_init_data s2mps13_clks_init[S2MPS11_CLKS_NUM] = {
	[S2MPS11_CLK_AP] = {
		.name = "s2mps13_ap",
		.ops = &s2mps11_clk_ops,
		.flags = CLK_IS_ROOT,
	},
	[S2MPS11_CLK_CP] = {
		.name = "s2mps13_cp",
		.ops = &s2mps11_clk_ops,
		.flags = CLK_IS_ROOT,
	},
	[S2MPS11_CLK_BT] = {
		.name = "s2mps13_bt",
		.ops = &s2mps11_clk_ops,
		.flags = CLK_IS_ROOT,
	},
};

static struct clk_init_data s2mps14_clks_init[S2MPS11_CLKS_NUM] = {
	[S2MPS11_CLK_AP] = {
		.name = "s2mps14_ap",
		.ops = &s2mps11_clk_ops,
		.flags = CLK_IS_ROOT,
	},
	[S2MPS11_CLK_BT] = {
		.name = "s2mps14_bt",
		.ops = &s2mps11_clk_ops,
		.flags = CLK_IS_ROOT,
	},
};

static struct device_node *s2mps11_clk_parse_dt(struct platform_device *pdev,
		struct clk_init_data *clks_init)
{
	struct sec_pmic_dev *iodev = dev_get_drvdata(pdev->dev.parent);
	struct device_node *clk_np;
	int i;

	if (!iodev->dev->of_node)
		return ERR_PTR(-EINVAL);

	clk_np = of_get_child_by_name(iodev->dev->of_node, "clocks");
	if (!clk_np) {
		dev_err(&pdev->dev, "could not find clock sub-node\n");
		return ERR_PTR(-EINVAL);
	}

	for (i = 0; i < S2MPS11_CLKS_NUM; i++) {
		if (!clks_init[i].name)
			continue; /* Skip clocks not present in some devices */
		of_property_read_string_index(clk_np, "clock-output-names", i,
				&clks_init[i].name);
	}

	return clk_np;
}

static int s2mps11_clk_probe(struct platform_device *pdev)
{
	struct sec_pmic_dev *iodev = dev_get_drvdata(pdev->dev.parent);
	struct s2mps11_clk *s2mps11_clks, *s2mps11_clk;
	unsigned int s2mps11_reg;
	struct clk_init_data *clks_init;
	int i, ret = 0;

	s2mps11_clks = devm_kzalloc(&pdev->dev, sizeof(*s2mps11_clk) *
					S2MPS11_CLKS_NUM, GFP_KERNEL);
	if (!s2mps11_clks)
		return -ENOMEM;

	s2mps11_clk = s2mps11_clks;

	clk_table = devm_kzalloc(&pdev->dev, sizeof(struct clk *) *
				 S2MPS11_CLKS_NUM, GFP_KERNEL);
	if (!clk_table)
		return -ENOMEM;

	switch(platform_get_device_id(pdev)->driver_data) {
	case S2MPS11X:
		s2mps11_reg = S2MPS11_REG_RTC_CTRL;
		clks_init = s2mps11_clks_init;
		break;
	case S2MPS13X:
		s2mps11_reg = S2MPS13_REG_RTCCTRL;
		clks_init = s2mps13_clks_init;
		break;
	case S2MPS14X:
		s2mps11_reg = S2MPS14_REG_RTCCTRL;
		clks_init = s2mps14_clks_init;
		break;
	case S5M8767X:
		s2mps11_reg = S5M8767_REG_CTRL1;
		clks_init = s2mps11_clks_init;
		break;
	default:
		dev_err(&pdev->dev, "Invalid device type\n");
		return -EINVAL;
	}

	/* Store clocks of_node in first element of s2mps11_clks array */
	s2mps11_clks->clk_np = s2mps11_clk_parse_dt(pdev, clks_init);
	if (IS_ERR(s2mps11_clks->clk_np))
		return PTR_ERR(s2mps11_clks->clk_np);

	for (i = 0; i < S2MPS11_CLKS_NUM; i++, s2mps11_clk++) {
		if (!clks_init[i].name)
			continue; /* Skip clocks not present in some devices */
		s2mps11_clk->iodev = iodev;
		s2mps11_clk->hw.init = &clks_init[i];
		s2mps11_clk->mask = 1 << i;
		s2mps11_clk->reg = s2mps11_reg;

		s2mps11_clk->clk = devm_clk_register(&pdev->dev,
							&s2mps11_clk->hw);
		if (IS_ERR(s2mps11_clk->clk)) {
			dev_err(&pdev->dev, "Fail to register : %s\n",
						s2mps11_name(s2mps11_clk));
			ret = PTR_ERR(s2mps11_clk->clk);
			goto err_reg;
		}

		s2mps11_clk->lookup = clkdev_create(s2mps11_clk->clk,
					s2mps11_name(s2mps11_clk), NULL);
		if (!s2mps11_clk->lookup) {
			ret = -ENOMEM;
			goto err_lup;
		}
	}

	for (i = 0; i < S2MPS11_CLKS_NUM; i++) {
		/* Skip clocks not present on S2MPS14 */
		if (!clks_init[i].name)
			continue;
		clk_table[i] = s2mps11_clks[i].clk;
	}

	clk_data.clks = clk_table;
	clk_data.clk_num = S2MPS11_CLKS_NUM;
	of_clk_add_provider(s2mps11_clks->clk_np, of_clk_src_onecell_get,
			&clk_data);

	platform_set_drvdata(pdev, s2mps11_clks);

	return ret;
err_lup:
	devm_clk_unregister(&pdev->dev, s2mps11_clk->clk);
err_reg:
	while (s2mps11_clk > s2mps11_clks) {
		if (s2mps11_clk->lookup) {
			clkdev_drop(s2mps11_clk->lookup);
			devm_clk_unregister(&pdev->dev, s2mps11_clk->clk);
		}
		s2mps11_clk--;
	}

	return ret;
}

static int s2mps11_clk_remove(struct platform_device *pdev)
{
	struct s2mps11_clk *s2mps11_clks = platform_get_drvdata(pdev);
	int i;

	of_clk_del_provider(s2mps11_clks[0].clk_np);
	/* Drop the reference obtained in s2mps11_clk_parse_dt */
	of_node_put(s2mps11_clks[0].clk_np);

	for (i = 0; i < S2MPS11_CLKS_NUM; i++) {
		/* Skip clocks not present on S2MPS14 */
		if (!s2mps11_clks[i].lookup)
			continue;
		clkdev_drop(s2mps11_clks[i].lookup);
	}

	return 0;
}

static const struct platform_device_id s2mps11_clk_id[] = {
	{ "s2mps11-clk", S2MPS11X},
	{ "s2mps13-clk", S2MPS13X},
	{ "s2mps14-clk", S2MPS14X},
	{ "s5m8767-clk", S5M8767X},
	{ },
};
MODULE_DEVICE_TABLE(platform, s2mps11_clk_id);

static struct platform_driver s2mps11_clk_driver = {
	.driver = {
		.name  = "s2mps11-clk",
	},
	.probe = s2mps11_clk_probe,
	.remove = s2mps11_clk_remove,
	.id_table = s2mps11_clk_id,
};

static int __init s2mps11_clk_init(void)
{
	return platform_driver_register(&s2mps11_clk_driver);
}
subsys_initcall(s2mps11_clk_init);

static void __init s2mps11_clk_cleanup(void)
{
	platform_driver_unregister(&s2mps11_clk_driver);
}
module_exit(s2mps11_clk_cleanup);

MODULE_DESCRIPTION("S2MPS11 Clock Driver");
MODULE_AUTHOR("Yadwinder Singh Brar <yadi.brar@samsung.com>");
MODULE_LICENSE("GPL");
