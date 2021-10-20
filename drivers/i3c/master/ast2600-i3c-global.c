// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 ASPEED Technology Inc.
 *
 * Author: Dylan Hung <dylan_hung@aspeedtech.com>
 * Based on a work from: Ryan Chen <ryan_chen@aspeedtech.com>
 */
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/of_address.h>
#include <linux/io.h>
#include <linux/reset.h>
#include <linux/delay.h>
#include <linux/slab.h>

#define I3CG_REG0(x)			((x * 0x10) + 0x10)
#define I3CG_REG0_SDA_PULLUP_EN_MASK	GENMASK(29, 28)
#define I3CG_REG0_SDA_PULLUP_EN_2K	(0x1 << 28)
#define I3CG_REG0_SDA_PULLUP_EN_750	(0x2 << 28)
#define I3CG_REG0_SDA_PULLUP_EN_545	(0x3 << 28)

#define I3CG_REG1(x)			((x * 0x10) + 0x14)
#define I3CG_REG1_I2C_MODE		BIT(0)
#define I3CG_REG1_TEST_MODE		BIT(1)
#define I3CG_REG1_ACT_MODE_MASK		GENMASK(3, 2)
#define I3CG_REG1_ACT_MODE(x)		(((x) << 2) & I3CG_REG1_ACT_MODE_MASK)
#define I3CG_REG1_PENDING_INT_MASK	GENMASK(7, 4)
#define I3CG_REG1_PENDING_INT(x)	(((x) << 4) & I3CG_REG1_PENDING_INT_MASK)
#define I3CG_REG1_SA_MASK		GENMASK(14, 8)
#define I3CG_REG1_SA(x)			(((x) << 8) & I3CG_REG1_SA_MASK)
#define I3CG_REG1_SA_EN			BIT(15)
#define I3CG_REG1_INST_ID_MASK		GENMASK(19, 16)
#define I3CG_REG1_INST_ID(x)		(((x) << 16) & I3CG_REG1_INST_ID_MASK)

struct aspeed_i3c_global {
	void __iomem *regs;
	struct reset_control *rst;
};

static const struct of_device_id aspeed_i3c_of_match[] = {
	{ .compatible = "aspeed,ast2600-i3c-global", },
	{},
};
MODULE_DEVICE_TABLE(of, aspeed_i3c_of_match);

static u32 pullup_resistor_ohm_to_reg(u32 ohm)
{
	switch (ohm) {
	case 545:
		return I3CG_REG0_SDA_PULLUP_EN_545;
	case 750:
		return I3CG_REG0_SDA_PULLUP_EN_750;
	case 2000:
	default:
		return I3CG_REG0_SDA_PULLUP_EN_2K;
	}
}

static int aspeed_i3c_global_probe(struct platform_device *pdev)
{
	struct aspeed_i3c_global *i3cg;
	u32 reg0, reg1, num_i3cs;
	u32 *pullup_resistors;
	int i, ret;

	i3cg = devm_kzalloc(&pdev->dev, sizeof(*i3cg), GFP_KERNEL);
	if (!i3cg)
		return -ENOMEM;

	i3cg->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(i3cg->regs))
		return -ENOMEM;

	i3cg->rst = devm_reset_control_get(&pdev->dev, NULL);
	if (IS_ERR(i3cg->rst)) {
		dev_err(&pdev->dev,
			"missing or invalid reset controller device tree entry");
		return PTR_ERR(i3cg->rst);
	}

	reset_control_assert(i3cg->rst);
	udelay(3);
	reset_control_deassert(i3cg->rst);

	ret = of_property_read_u32(pdev->dev.of_node, "num-i3cs", &num_i3cs);
	if (ret < 0) {
		dev_err(&pdev->dev, "unable to get number of i3c controllers");
		return -ENOMEM;
	}

	pullup_resistors = kcalloc(num_i3cs, sizeof(u32), GFP_KERNEL);
	if (!pullup_resistors)
		return -ENOMEM;

	ret = of_property_read_u32_array(pdev->dev.of_node, "pull-up-resistors",
					 pullup_resistors, num_i3cs);
	if (ret < 0) {
		dev_warn(&pdev->dev,
			 "use 2K Ohm SDA pull up resistor by default");
	}

	reg1 = I3CG_REG1_ACT_MODE(1) | I3CG_REG1_PENDING_INT(0xc) |
	       I3CG_REG1_SA(0x74);

	for (i = 0; i < num_i3cs; i++) {
		reg0 = readl(i3cg->regs + I3CG_REG0(i));
		reg0 &= ~I3CG_REG0_SDA_PULLUP_EN_MASK;
		reg0 |= pullup_resistor_ohm_to_reg(pullup_resistors[i]);
		writel(reg0, i3cg->regs + I3CG_REG0(i));

		reg1 &= ~I3CG_REG1_INST_ID_MASK;
		reg1 |= I3CG_REG1_INST_ID(i);
		writel(reg1, i3cg->regs + I3CG_REG1(i));
	}

	kfree(pullup_resistors);

	return 0;
}

static struct platform_driver aspeed_i3c_driver = {
	.probe  = aspeed_i3c_global_probe,
	.driver = {
		.name = KBUILD_MODNAME,
		.of_match_table = of_match_ptr(aspeed_i3c_of_match),
	},
};

//static int __init aspeed_i3c_global_init(void)
//{
//	return platform_driver_register(&aspeed_i3c_driver);
//}
//postcore_initcall(aspeed_i3c_global_init);
module_platform_driver(aspeed_i3c_driver);

MODULE_AUTHOR("Ryan Chen <ryan_chen@aspeedtech.com>");
MODULE_AUTHOR("Dylan Hung <dylan_hung@aspeedtech.com>");
MODULE_DESCRIPTION("ASPEED I3C Global Driver");
MODULE_LICENSE("GPL v2");
