/*
 * clk-max77686.c - Clock driver for Maxim 77686
 *
 * Copyright (C) 2012 Samsung Electornics
 * Jonghwa Lee <jonghwa3.lee@samsung.com>
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/mfd/max77686.h>
#include <linux/mfd/max77686-private.h>
#include <linux/clk-provider.h>
#include <linux/mutex.h>
#include <linux/clkdev.h>

enum {
	MAX77686_CLK_AP = 0,
	MAX77686_CLK_CP,
	MAX77686_CLK_PMIC,
	MAX77686_CLKS_NUM,
};

struct max77686_clk {
	struct max77686_dev *iodev;
	u32 mask;
	struct clk_hw hw;
	struct clk_lookup *lookup;
};

static struct max77686_clk *to_max77686_clk(struct clk_hw *hw)
{
	return container_of(hw, struct max77686_clk, hw);
}

static int max77686_clk_prepare(struct clk_hw *hw)
{
	struct max77686_clk *max77686 = to_max77686_clk(hw);

	return regmap_update_bits(max77686->iodev->regmap,
				  MAX77686_REG_32KHZ, max77686->mask,
				  max77686->mask);
}

static void max77686_clk_unprepare(struct clk_hw *hw)
{
	struct max77686_clk *max77686 = to_max77686_clk(hw);

	regmap_update_bits(max77686->iodev->regmap,
		MAX77686_REG_32KHZ, max77686->mask, ~max77686->mask);
}

static int max77686_clk_is_prepared(struct clk_hw *hw)
{
	struct max77686_clk *max77686 = to_max77686_clk(hw);
	int ret;
	u32 val;

	ret = regmap_read(max77686->iodev->regmap,
				MAX77686_REG_32KHZ, &val);

	if (ret < 0)
		return -EINVAL;

	return val & max77686->mask;
}

static unsigned long max77686_recalc_rate(struct clk_hw *hw,
					  unsigned long parent_rate)
{
	return 32768;
}

static struct clk_ops max77686_clk_ops = {
	.prepare	= max77686_clk_prepare,
	.unprepare	= max77686_clk_unprepare,
	.is_prepared	= max77686_clk_is_prepared,
	.recalc_rate	= max77686_recalc_rate,
};

static struct clk_init_data max77686_clks_init[MAX77686_CLKS_NUM] = {
	[MAX77686_CLK_AP] = {
		.name = "32khz_ap",
		.ops = &max77686_clk_ops,
		.flags = CLK_IS_ROOT,
	},
	[MAX77686_CLK_CP] = {
		.name = "32khz_cp",
		.ops = &max77686_clk_ops,
		.flags = CLK_IS_ROOT,
	},
	[MAX77686_CLK_PMIC] = {
		.name = "32khz_pmic",
		.ops = &max77686_clk_ops,
		.flags = CLK_IS_ROOT,
	},
};

static struct clk *max77686_clk_register(struct device *dev,
				struct max77686_clk *max77686)
{
	struct clk *clk;
	struct clk_hw *hw = &max77686->hw;

	clk = clk_register(dev, hw);
	if (IS_ERR(clk))
		return clk;

	max77686->lookup = kzalloc(sizeof(struct clk_lookup), GFP_KERNEL);
	if (!max77686->lookup)
		return ERR_PTR(-ENOMEM);

	max77686->lookup->con_id = hw->init->name;
	max77686->lookup->clk = clk;

	clkdev_add(max77686->lookup);

	return clk;
}

static int max77686_clk_probe(struct platform_device *pdev)
{
	struct max77686_dev *iodev = dev_get_drvdata(pdev->dev.parent);
	struct max77686_clk *max77686_clks[MAX77686_CLKS_NUM];
	struct clk **clocks;
	int i, ret;

	clocks = devm_kzalloc(&pdev->dev, sizeof(struct clk *)
					* MAX77686_CLKS_NUM, GFP_KERNEL);
	if (!clocks)
		return -ENOMEM;

	for (i = 0; i < MAX77686_CLKS_NUM; i++) {
		max77686_clks[i] = devm_kzalloc(&pdev->dev,
					sizeof(struct max77686_clk), GFP_KERNEL);
		if (!max77686_clks[i])
			return -ENOMEM;
	}

	for (i = 0; i < MAX77686_CLKS_NUM; i++) {
		max77686_clks[i]->iodev = iodev;
		max77686_clks[i]->mask = 1 << i;
		max77686_clks[i]->hw.init = &max77686_clks_init[i];

		clocks[i] = max77686_clk_register(&pdev->dev, max77686_clks[i]);
		if (IS_ERR(clocks[i])) {
			ret = PTR_ERR(clocks[i]);
			dev_err(&pdev->dev, "failed to register %s\n",
				max77686_clks[i]->hw.init->name);
			goto err_clocks;
		}
	}

	platform_set_drvdata(pdev, clocks);

	if (iodev->dev->of_node) {
		struct clk_onecell_data *of_data;

		of_data = devm_kzalloc(&pdev->dev,
					sizeof(*of_data), GFP_KERNEL);
		if (!of_data) {
			ret = -ENOMEM;
			goto err_clocks;
		}

		of_data->clks = clocks;
		of_data->clk_num = MAX77686_CLKS_NUM;
		ret = of_clk_add_provider(iodev->dev->of_node,
					of_clk_src_onecell_get, of_data);
		if (ret) {
			dev_err(&pdev->dev, "failed to register OF clock provider\n");
			goto err_clocks;
		}
	}

	return 0;

err_clocks:
	for (--i; i >= 0; --i) {
		clkdev_drop(max77686_clks[i]->lookup);
		clk_unregister(max77686_clks[i]->hw.clk);
	}

	return ret;
}

static int max77686_clk_remove(struct platform_device *pdev)
{
	struct max77686_dev *iodev = dev_get_drvdata(pdev->dev.parent);
	struct clk **clocks = platform_get_drvdata(pdev);
	int i;

	if (iodev->dev->of_node)
		of_clk_del_provider(iodev->dev->of_node);

	for (i = 0; i < MAX77686_CLKS_NUM; i++) {
		struct clk_hw *hw = __clk_get_hw(clocks[i]);
		struct max77686_clk *max77686 = to_max77686_clk(hw);

		clkdev_drop(max77686->lookup);
		clk_unregister(clocks[i]);
	}
	return 0;
}

static const struct platform_device_id max77686_clk_id[] = {
	{ "max77686-clk", 0},
	{ },
};
MODULE_DEVICE_TABLE(platform, max77686_clk_id);

static struct platform_driver max77686_clk_driver = {
	.driver = {
		.name  = "max77686-clk",
		.owner = THIS_MODULE,
	},
	.probe = max77686_clk_probe,
	.remove = max77686_clk_remove,
	.id_table = max77686_clk_id,
};

static int __init max77686_clk_init(void)
{
	return platform_driver_register(&max77686_clk_driver);
}
subsys_initcall(max77686_clk_init);

static void __init max77686_clk_cleanup(void)
{
	platform_driver_unregister(&max77686_clk_driver);
}
module_exit(max77686_clk_cleanup);

MODULE_DESCRIPTION("MAXIM 77686 Clock Driver");
MODULE_AUTHOR("Jonghwa Lee <jonghwa3.lee@samsung.com>");
MODULE_LICENSE("GPL");
