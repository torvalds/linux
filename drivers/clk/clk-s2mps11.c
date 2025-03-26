// SPDX-License-Identifier: GPL-2.0+
//
// clk-s2mps11.c - Clock driver for S2MPS11.
//
// Copyright (C) 2013,2014 Samsung Electornics

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

#include <dt-bindings/clock/samsung,s2mps11.h>

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

	return regmap_update_bits(s2mps11->iodev->regmap_pmic,
				 s2mps11->reg,
				 s2mps11->mask, s2mps11->mask);
}

static void s2mps11_clk_unprepare(struct clk_hw *hw)
{
	struct s2mps11_clk *s2mps11 = to_s2mps11_clk(hw);

	regmap_update_bits(s2mps11->iodev->regmap_pmic, s2mps11->reg,
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

static const struct clk_ops s2mps11_clk_ops = {
	.prepare	= s2mps11_clk_prepare,
	.unprepare	= s2mps11_clk_unprepare,
	.is_prepared	= s2mps11_clk_is_prepared,
	.recalc_rate	= s2mps11_clk_recalc_rate,
};

/* This s2mps11_clks_init tructure is common to s2mps11, s2mps13 and s2mps14 */
static struct clk_init_data s2mps11_clks_init[S2MPS11_CLKS_NUM] = {
	[S2MPS11_CLK_AP] = {
		.name = "s2mps11_ap",
		.ops = &s2mps11_clk_ops,
	},
	[S2MPS11_CLK_CP] = {
		.name = "s2mps11_cp",
		.ops = &s2mps11_clk_ops,
	},
	[S2MPS11_CLK_BT] = {
		.name = "s2mps11_bt",
		.ops = &s2mps11_clk_ops,
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

	for (i = 0; i < S2MPS11_CLKS_NUM; i++)
		of_property_read_string_index(clk_np, "clock-output-names", i,
				&clks_init[i].name);

	return clk_np;
}

static int s2mps11_clk_probe(struct platform_device *pdev)
{
	struct sec_pmic_dev *iodev = dev_get_drvdata(pdev->dev.parent);
	struct s2mps11_clk *s2mps11_clks;
	struct clk_hw_onecell_data *clk_data;
	unsigned int s2mps11_reg;
	int i, ret = 0;
	enum sec_device_type hwid = platform_get_device_id(pdev)->driver_data;

	s2mps11_clks = devm_kcalloc(&pdev->dev, S2MPS11_CLKS_NUM,
				sizeof(*s2mps11_clks), GFP_KERNEL);
	if (!s2mps11_clks)
		return -ENOMEM;

	clk_data = devm_kzalloc(&pdev->dev,
				struct_size(clk_data, hws, S2MPS11_CLKS_NUM),
				GFP_KERNEL);
	if (!clk_data)
		return -ENOMEM;

	clk_data->num = S2MPS11_CLKS_NUM;

	switch (hwid) {
	case S2MPS11X:
		s2mps11_reg = S2MPS11_REG_RTC_CTRL;
		break;
	case S2MPS13X:
		s2mps11_reg = S2MPS13_REG_RTCCTRL;
		break;
	case S2MPS14X:
		s2mps11_reg = S2MPS14_REG_RTCCTRL;
		break;
	case S5M8767X:
		s2mps11_reg = S5M8767_REG_CTRL1;
		break;
	default:
		dev_err(&pdev->dev, "Invalid device type\n");
		return -EINVAL;
	}

	/* Store clocks of_node in first element of s2mps11_clks array */
	s2mps11_clks->clk_np = s2mps11_clk_parse_dt(pdev, s2mps11_clks_init);
	if (IS_ERR(s2mps11_clks->clk_np))
		return PTR_ERR(s2mps11_clks->clk_np);

	for (i = 0; i < S2MPS11_CLKS_NUM; i++) {
		if (i == S2MPS11_CLK_CP && hwid == S2MPS14X)
			continue; /* Skip clocks not present in some devices */
		s2mps11_clks[i].iodev = iodev;
		s2mps11_clks[i].hw.init = &s2mps11_clks_init[i];
		s2mps11_clks[i].mask = 1 << i;
		s2mps11_clks[i].reg = s2mps11_reg;

		s2mps11_clks[i].clk = devm_clk_register(&pdev->dev,
							&s2mps11_clks[i].hw);
		if (IS_ERR(s2mps11_clks[i].clk)) {
			dev_err(&pdev->dev, "Fail to register : %s\n",
						s2mps11_clks_init[i].name);
			ret = PTR_ERR(s2mps11_clks[i].clk);
			goto err_reg;
		}

		s2mps11_clks[i].lookup = clkdev_hw_create(&s2mps11_clks[i].hw,
					s2mps11_clks_init[i].name, NULL);
		if (!s2mps11_clks[i].lookup) {
			ret = -ENOMEM;
			goto err_reg;
		}
		clk_data->hws[i] = &s2mps11_clks[i].hw;
	}

	of_clk_add_hw_provider(s2mps11_clks->clk_np, of_clk_hw_onecell_get,
			       clk_data);

	platform_set_drvdata(pdev, s2mps11_clks);

	return ret;

err_reg:
	of_node_put(s2mps11_clks[0].clk_np);
	while (--i >= 0)
		clkdev_drop(s2mps11_clks[i].lookup);

	return ret;
}

static void s2mps11_clk_remove(struct platform_device *pdev)
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
}

static const struct platform_device_id s2mps11_clk_id[] = {
	{ "s2mps11-clk", S2MPS11X},
	{ "s2mps13-clk", S2MPS13X},
	{ "s2mps14-clk", S2MPS14X},
	{ "s5m8767-clk", S5M8767X},
	{ },
};
MODULE_DEVICE_TABLE(platform, s2mps11_clk_id);

#ifdef CONFIG_OF
/*
 * Device is instantiated through parent MFD device and device matching is done
 * through platform_device_id.
 *
 * However if device's DT node contains proper clock compatible and driver is
 * built as a module, then the *module* matching will be done trough DT aliases.
 * This requires of_device_id table.  In the same time this will not change the
 * actual *device* matching so do not add .of_match_table.
 */
static const struct of_device_id s2mps11_dt_match[] __used = {
	{
		.compatible = "samsung,s2mps11-clk",
		.data = (void *)S2MPS11X,
	}, {
		.compatible = "samsung,s2mps13-clk",
		.data = (void *)S2MPS13X,
	}, {
		.compatible = "samsung,s2mps14-clk",
		.data = (void *)S2MPS14X,
	}, {
		.compatible = "samsung,s5m8767-clk",
		.data = (void *)S5M8767X,
	}, {
		/* Sentinel */
	},
};
MODULE_DEVICE_TABLE(of, s2mps11_dt_match);
#endif

static struct platform_driver s2mps11_clk_driver = {
	.driver = {
		.name  = "s2mps11-clk",
	},
	.probe = s2mps11_clk_probe,
	.remove = s2mps11_clk_remove,
	.id_table = s2mps11_clk_id,
};
module_platform_driver(s2mps11_clk_driver);

MODULE_DESCRIPTION("S2MPS11 Clock Driver");
MODULE_AUTHOR("Yadwinder Singh Brar <yadi.brar@samsung.com>");
MODULE_LICENSE("GPL");
