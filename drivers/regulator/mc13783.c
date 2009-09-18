/*
 * Regulator Driver for Freescale MC13783 PMIC
 *
 * Copyright (C) 2008 Sascha Hauer, Pengutronix <s.hauer@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/mfd/mc13783-private.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/driver.h>
#include <linux/platform_device.h>
#include <linux/mfd/mc13783.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/err.h>

struct mc13783_regulator {
	struct regulator_desc desc;
	int reg;
	int enable_bit;
};

static struct regulator_ops mc13783_regulator_ops;

static struct mc13783_regulator mc13783_regulators[] = {
	[MC13783_SW_SW3] = {
		.desc = {
			.name	= "SW_SW3",
			.ops	= &mc13783_regulator_ops,
			.type	= REGULATOR_VOLTAGE,
			.id	= MC13783_SW_SW3,
			.owner	= THIS_MODULE,
		},
		.reg = MC13783_REG_SWITCHERS_5,
		.enable_bit = MC13783_SWCTRL_SW3_EN,
	},
	[MC13783_SW_PLL] = {
		.desc = {
			.name	= "SW_PLL",
			.ops	= &mc13783_regulator_ops,
			.type	= REGULATOR_VOLTAGE,
			.id	= MC13783_SW_PLL,
			.owner	= THIS_MODULE,
		},
		.reg = MC13783_REG_SWITCHERS_4,
		.enable_bit = MC13783_SWCTRL_PLL_EN,
	},
	[MC13783_REGU_VAUDIO] = {
		.desc = {
			.name	= "REGU_VAUDIO",
			.ops	= &mc13783_regulator_ops,
			.type	= REGULATOR_VOLTAGE,
			.id	= MC13783_REGU_VAUDIO,
			.owner	= THIS_MODULE,
		},
		.reg = MC13783_REG_REGULATOR_MODE_0,
		.enable_bit = MC13783_REGCTRL_VAUDIO_EN,
	},
	[MC13783_REGU_VIOHI] = {
		.desc = {
			.name	= "REGU_VIOHI",
			.ops	= &mc13783_regulator_ops,
			.type	= REGULATOR_VOLTAGE,
			.id	= MC13783_REGU_VIOHI,
			.owner	= THIS_MODULE,
		},
		.reg = MC13783_REG_REGULATOR_MODE_0,
		.enable_bit = MC13783_REGCTRL_VIOHI_EN,
	},
	[MC13783_REGU_VIOLO] = {
		.desc = {
			.name	= "REGU_VIOLO",
			.ops	= &mc13783_regulator_ops,
			.type	= REGULATOR_VOLTAGE,
			.id	= MC13783_REGU_VIOLO,
			.owner	= THIS_MODULE,
		},
		.reg = MC13783_REG_REGULATOR_MODE_0,
		.enable_bit = MC13783_REGCTRL_VIOLO_EN,
	},
	[MC13783_REGU_VDIG] = {
		.desc = {
			.name	= "REGU_VDIG",
			.ops	= &mc13783_regulator_ops,
			.type	= REGULATOR_VOLTAGE,
			.id	= MC13783_REGU_VDIG,
			.owner	= THIS_MODULE,
		},
		.reg = MC13783_REG_REGULATOR_MODE_0,
		.enable_bit = MC13783_REGCTRL_VDIG_EN,
	},
	[MC13783_REGU_VGEN] = {
		.desc = {
			.name	= "REGU_VGEN",
			.ops	= &mc13783_regulator_ops,
			.type	= REGULATOR_VOLTAGE,
			.id	= MC13783_REGU_VGEN,
			.owner	= THIS_MODULE,
		},
		.reg = MC13783_REG_REGULATOR_MODE_0,
		.enable_bit = MC13783_REGCTRL_VGEN_EN,
	},
	[MC13783_REGU_VRFDIG] = {
		.desc = {
			.name	= "REGU_VRFDIG",
			.ops	= &mc13783_regulator_ops,
			.type	= REGULATOR_VOLTAGE,
			.id	= MC13783_REGU_VRFDIG,
			.owner	= THIS_MODULE,
		},
		.reg = MC13783_REG_REGULATOR_MODE_0,
		.enable_bit = MC13783_REGCTRL_VRFDIG_EN,
	},
	[MC13783_REGU_VRFREF] = {
		.desc = {
			.name	= "REGU_VRFREF",
			.ops	= &mc13783_regulator_ops,
			.type	= REGULATOR_VOLTAGE,
			.id	= MC13783_REGU_VRFREF,
			.owner	= THIS_MODULE,
		},
		.reg = MC13783_REG_REGULATOR_MODE_0,
		.enable_bit = MC13783_REGCTRL_VRFREF_EN,
	},
	[MC13783_REGU_VRFCP] = {
		.desc = {
			.name	= "REGU_VRFCP",
			.ops	= &mc13783_regulator_ops,
			.type	= REGULATOR_VOLTAGE,
			.id	= MC13783_REGU_VRFCP,
			.owner	= THIS_MODULE,
		},
		.reg = MC13783_REG_REGULATOR_MODE_0,
		.enable_bit = MC13783_REGCTRL_VRFCP_EN,
	},
	[MC13783_REGU_VSIM] = {
		.desc = {
			.name	= "REGU_VSIM",
			.ops	= &mc13783_regulator_ops,
			.type	= REGULATOR_VOLTAGE,
			.id	= MC13783_REGU_VSIM,
			.owner	= THIS_MODULE,
		},
		.reg = MC13783_REG_REGULATOR_MODE_1,
		.enable_bit = MC13783_REGCTRL_VSIM_EN,
	},
	[MC13783_REGU_VESIM] = {
		.desc = {
			.name	= "REGU_VESIM",
			.ops	= &mc13783_regulator_ops,
			.type	= REGULATOR_VOLTAGE,
			.id	= MC13783_REGU_VESIM,
			.owner	= THIS_MODULE,
		},
		.reg = MC13783_REG_REGULATOR_MODE_1,
		.enable_bit = MC13783_REGCTRL_VESIM_EN,
	},
	[MC13783_REGU_VCAM] = {
		.desc = {
			.name	= "REGU_VCAM",
			.ops	= &mc13783_regulator_ops,
			.type	= REGULATOR_VOLTAGE,
			.id	= MC13783_REGU_VCAM,
			.owner	= THIS_MODULE,
		},
		.reg = MC13783_REG_REGULATOR_MODE_1,
		.enable_bit = MC13783_REGCTRL_VCAM_EN,
	},
	[MC13783_REGU_VRFBG] = {
		.desc = {
			.name	= "REGU_VRFBG",
			.ops	= &mc13783_regulator_ops,
			.type	= REGULATOR_VOLTAGE,
			.id	= MC13783_REGU_VRFBG,
			.owner	= THIS_MODULE,
		},
		.reg = MC13783_REG_REGULATOR_MODE_1,
		.enable_bit = MC13783_REGCTRL_VRFBG_EN,
	},
	[MC13783_REGU_VVIB] = {
		.desc = {
			.name	= "REGU_VVIB",
			.ops	= &mc13783_regulator_ops,
			.type	= REGULATOR_VOLTAGE,
			.id	= MC13783_REGU_VVIB,
			.owner	= THIS_MODULE,
		},
		.reg = MC13783_REG_REGULATOR_MODE_1,
		.enable_bit = MC13783_REGCTRL_VVIB_EN,
	},
	[MC13783_REGU_VRF1] = {
		.desc = {
			.name	= "REGU_VRF1",
			.ops	= &mc13783_regulator_ops,
			.type	= REGULATOR_VOLTAGE,
			.id	= MC13783_REGU_VRF1,
			.owner	= THIS_MODULE,
		},
		.reg = MC13783_REG_REGULATOR_MODE_1,
		.enable_bit = MC13783_REGCTRL_VRF1_EN,
	},
	[MC13783_REGU_VRF2] = {
		.desc = {
			.name	= "REGU_VRF2",
			.ops	= &mc13783_regulator_ops,
			.type	= REGULATOR_VOLTAGE,
			.id	= MC13783_REGU_VRF2,
			.owner	= THIS_MODULE,
		},
		.reg = MC13783_REG_REGULATOR_MODE_1,
		.enable_bit = MC13783_REGCTRL_VRF2_EN,
	},
	[MC13783_REGU_VMMC1] = {
		.desc = {
			.name	= "REGU_VMMC1",
			.ops	= &mc13783_regulator_ops,
			.type	= REGULATOR_VOLTAGE,
			.id	= MC13783_REGU_VMMC1,
			.owner	= THIS_MODULE,
		},
		.reg = MC13783_REG_REGULATOR_MODE_1,
		.enable_bit = MC13783_REGCTRL_VMMC1_EN,
	},
	[MC13783_REGU_VMMC2] = {
		.desc = {
			.name	= "REGU_VMMC2",
			.ops	= &mc13783_regulator_ops,
			.type	= REGULATOR_VOLTAGE,
			.id	= MC13783_REGU_VMMC2,
			.owner	= THIS_MODULE,
		},
		.reg = MC13783_REG_REGULATOR_MODE_1,
		.enable_bit = MC13783_REGCTRL_VMMC2_EN,
	},
	[MC13783_REGU_GPO1] = {
		.desc = {
			.name	= "REGU_GPO1",
			.ops	= &mc13783_regulator_ops,
			.type	= REGULATOR_VOLTAGE,
			.id	= MC13783_REGU_GPO1,
			.owner	= THIS_MODULE,
		},
		.reg = MC13783_REG_POWER_MISCELLANEOUS,
		.enable_bit = MC13783_REGCTRL_GPO1_EN,
	},
	[MC13783_REGU_GPO2] = {
		.desc = {
			.name	= "REGU_GPO2",
			.ops	= &mc13783_regulator_ops,
			.type	= REGULATOR_VOLTAGE,
			.id	= MC13783_REGU_GPO2,
			.owner	= THIS_MODULE,
		},
		.reg = MC13783_REG_POWER_MISCELLANEOUS,
		.enable_bit = MC13783_REGCTRL_GPO2_EN,
	},
	[MC13783_REGU_GPO3] = {
		.desc = {
			.name	= "REGU_GPO3",
			.ops	= &mc13783_regulator_ops,
			.type	= REGULATOR_VOLTAGE,
			.id	= MC13783_REGU_GPO3,
			.owner	= THIS_MODULE,
		},
		.reg = MC13783_REG_POWER_MISCELLANEOUS,
		.enable_bit = MC13783_REGCTRL_GPO3_EN,
	},
	[MC13783_REGU_GPO4] = {
		.desc = {
			.name	= "REGU_GPO4",
			.ops	= &mc13783_regulator_ops,
			.type	= REGULATOR_VOLTAGE,
			.id	= MC13783_REGU_GPO4,
			.owner	= THIS_MODULE,
		},
		.reg = MC13783_REG_POWER_MISCELLANEOUS,
		.enable_bit = MC13783_REGCTRL_GPO4_EN,
	},
};

struct mc13783_priv {
	struct regulator_desc desc[ARRAY_SIZE(mc13783_regulators)];
	struct mc13783 *mc13783;
	struct regulator_dev *regulators[0];
};

static int mc13783_enable(struct regulator_dev *rdev)
{
	struct mc13783_priv *priv = rdev_get_drvdata(rdev);
	int id = rdev_get_id(rdev);

	dev_dbg(rdev_get_dev(rdev), "%s id: %d\n", __func__, id);

	return mc13783_set_bits(priv->mc13783, mc13783_regulators[id].reg,
			mc13783_regulators[id].enable_bit,
			mc13783_regulators[id].enable_bit);
}

static int mc13783_disable(struct regulator_dev *rdev)
{
	struct mc13783_priv *priv = rdev_get_drvdata(rdev);
	int id = rdev_get_id(rdev);

	dev_dbg(rdev_get_dev(rdev), "%s id: %d\n", __func__, id);

	return mc13783_set_bits(priv->mc13783, mc13783_regulators[id].reg,
			mc13783_regulators[id].enable_bit, 0);
}

static int mc13783_is_enabled(struct regulator_dev *rdev)
{
	struct mc13783_priv *priv = rdev_get_drvdata(rdev);
	int ret, id = rdev_get_id(rdev);
	unsigned int val;

	ret = mc13783_reg_read(priv->mc13783, mc13783_regulators[id].reg, &val);
	if (ret)
		return ret;

	return (val & mc13783_regulators[id].enable_bit) != 0;
}

static struct regulator_ops mc13783_regulator_ops = {
	.enable		= mc13783_enable,
	.disable	= mc13783_disable,
	.is_enabled	= mc13783_is_enabled,
};

static int __devinit mc13783_regulator_probe(struct platform_device *pdev)
{
	struct mc13783_priv *priv;
	struct mc13783 *mc13783 = dev_get_drvdata(pdev->dev.parent);
	struct mc13783_regulator_init_data *init_data;
	int i, ret;

	dev_dbg(&pdev->dev, "mc13783_regulator_probe id %d\n", pdev->id);

	priv = kzalloc(sizeof(*priv) + mc13783->num_regulators * sizeof(void *),
			GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->mc13783 = mc13783;

	for (i = 0; i < mc13783->num_regulators; i++) {
		init_data = &mc13783->regulators[i];
		priv->regulators[i] = regulator_register(
				&mc13783_regulators[init_data->id].desc,
				&pdev->dev, init_data->init_data, priv);

		if (IS_ERR(priv->regulators[i])) {
			dev_err(&pdev->dev, "failed to register regulator %s\n",
				mc13783_regulators[i].desc.name);
			ret = PTR_ERR(priv->regulators[i]);
			goto err;
		}
	}

	platform_set_drvdata(pdev, priv);

	return 0;
err:
	while (--i >= 0)
		regulator_unregister(priv->regulators[i]);

	kfree(priv);

	return ret;
}

static int __devexit mc13783_regulator_remove(struct platform_device *pdev)
{
	struct mc13783_priv *priv = platform_get_drvdata(pdev);
	struct mc13783 *mc13783 = priv->mc13783;
	int i;

	for (i = 0; i < mc13783->num_regulators; i++)
		regulator_unregister(priv->regulators[i]);

	return 0;
}

static struct platform_driver mc13783_regulator_driver = {
	.driver	= {
		.name	= "mc13783-regulator",
		.owner	= THIS_MODULE,
	},
	.remove		= __devexit_p(mc13783_regulator_remove),
};

static int __init mc13783_regulator_init(void)
{
	return platform_driver_probe(&mc13783_regulator_driver,
			mc13783_regulator_probe);
}
subsys_initcall(mc13783_regulator_init);

static void __exit mc13783_regulator_exit(void)
{
	platform_driver_unregister(&mc13783_regulator_driver);
}
module_exit(mc13783_regulator_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sascha Hauer <s.hauer@pengutronix.de");
MODULE_DESCRIPTION("Regulator Driver for Freescale MC13783 PMIC");
MODULE_ALIAS("platform:mc13783-regulator");
