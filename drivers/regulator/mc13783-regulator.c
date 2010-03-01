/*
 * Regulator Driver for Freescale MC13783 PMIC
 *
 * Copyright (C) 2008 Sascha Hauer, Pengutronix <s.hauer@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/mfd/mc13783.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/driver.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/err.h>

#define MC13783_REG_SWITCHERS4			28
#define MC13783_REG_SWITCHERS4_PLLEN			(1 << 18)

#define MC13783_REG_SWITCHERS5			29
#define MC13783_REG_SWITCHERS5_SW3EN			(1 << 20)

#define MC13783_REG_REGULATORMODE0		32
#define MC13783_REG_REGULATORMODE0_VAUDIOEN		(1 << 0)
#define MC13783_REG_REGULATORMODE0_VIOHIEN		(1 << 3)
#define MC13783_REG_REGULATORMODE0_VIOLOEN		(1 << 6)
#define MC13783_REG_REGULATORMODE0_VDIGEN		(1 << 9)
#define MC13783_REG_REGULATORMODE0_VGENEN		(1 << 12)
#define MC13783_REG_REGULATORMODE0_VRFDIGEN		(1 << 15)
#define MC13783_REG_REGULATORMODE0_VRFREFEN		(1 << 18)
#define MC13783_REG_REGULATORMODE0_VRFCPEN		(1 << 21)

#define MC13783_REG_REGULATORMODE1		33
#define MC13783_REG_REGULATORMODE1_VSIMEN		(1 << 0)
#define MC13783_REG_REGULATORMODE1_VESIMEN		(1 << 3)
#define MC13783_REG_REGULATORMODE1_VCAMEN		(1 << 6)
#define MC13783_REG_REGULATORMODE1_VRFBGEN		(1 << 9)
#define MC13783_REG_REGULATORMODE1_VVIBEN		(1 << 11)
#define MC13783_REG_REGULATORMODE1_VRF1EN		(1 << 12)
#define MC13783_REG_REGULATORMODE1_VRF2EN		(1 << 15)
#define MC13783_REG_REGULATORMODE1_VMMC1EN		(1 << 18)
#define MC13783_REG_REGULATORMODE1_VMMC2EN		(1 << 21)

#define MC13783_REG_POWERMISC			34
#define MC13783_REG_POWERMISC_GPO1EN			(1 << 6)
#define MC13783_REG_POWERMISC_GPO2EN			(1 << 8)
#define MC13783_REG_POWERMISC_GPO3EN			(1 << 10)
#define MC13783_REG_POWERMISC_GPO4EN			(1 << 12)

struct mc13783_regulator {
	struct regulator_desc desc;
	int reg;
	int enable_bit;
};

static struct regulator_ops mc13783_regulator_ops;

#define MC13783_DEFINE(prefix, _name, _reg)				\
	[MC13783_ ## prefix ## _ ## _name] = {				\
		.desc = {						\
			.name = #prefix "_" #_name,			\
			.ops = &mc13783_regulator_ops,			\
			.type = REGULATOR_VOLTAGE,			\
			.id = MC13783_ ## prefix ## _ ## _name,		\
			.owner = THIS_MODULE,				\
		},							\
		.reg = MC13783_REG_ ## _reg,				\
		.enable_bit = MC13783_REG_ ## _reg ## _ ## _name ## EN,	\
	}

#define MC13783_DEFINE_SW(_name, _reg) MC13783_DEFINE(SW, _name, _reg)
#define MC13783_DEFINE_REGU(_name, _reg) MC13783_DEFINE(REGU, _name, _reg)

static struct mc13783_regulator mc13783_regulators[] = {
	MC13783_DEFINE_SW(SW3, SWITCHERS5),
	MC13783_DEFINE_SW(PLL, SWITCHERS4),

	MC13783_DEFINE_REGU(VAUDIO, REGULATORMODE0),
	MC13783_DEFINE_REGU(VIOHI, REGULATORMODE0),
	MC13783_DEFINE_REGU(VIOLO, REGULATORMODE0),
	MC13783_DEFINE_REGU(VDIG, REGULATORMODE0),
	MC13783_DEFINE_REGU(VGEN, REGULATORMODE0),
	MC13783_DEFINE_REGU(VRFDIG, REGULATORMODE0),
	MC13783_DEFINE_REGU(VRFREF, REGULATORMODE0),
	MC13783_DEFINE_REGU(VRFCP, REGULATORMODE0),
	MC13783_DEFINE_REGU(VSIM, REGULATORMODE1),
	MC13783_DEFINE_REGU(VESIM, REGULATORMODE1),
	MC13783_DEFINE_REGU(VCAM, REGULATORMODE1),
	MC13783_DEFINE_REGU(VRFBG, REGULATORMODE1),
	MC13783_DEFINE_REGU(VVIB, REGULATORMODE1),
	MC13783_DEFINE_REGU(VRF1, REGULATORMODE1),
	MC13783_DEFINE_REGU(VRF2, REGULATORMODE1),
	MC13783_DEFINE_REGU(VMMC1, REGULATORMODE1),
	MC13783_DEFINE_REGU(VMMC2, REGULATORMODE1),
	MC13783_DEFINE_REGU(GPO1, POWERMISC),
	MC13783_DEFINE_REGU(GPO2, POWERMISC),
	MC13783_DEFINE_REGU(GPO3, POWERMISC),
	MC13783_DEFINE_REGU(GPO4, POWERMISC),
};

struct mc13783_regulator_priv {
	struct mc13783 *mc13783;
	struct regulator_dev *regulators[];
};

static int mc13783_regulator_enable(struct regulator_dev *rdev)
{
	struct mc13783_regulator_priv *priv = rdev_get_drvdata(rdev);
	int id = rdev_get_id(rdev);
	int ret;

	dev_dbg(rdev_get_dev(rdev), "%s id: %d\n", __func__, id);

	mc13783_lock(priv->mc13783);
	ret = mc13783_reg_rmw(priv->mc13783, mc13783_regulators[id].reg,
			mc13783_regulators[id].enable_bit,
			mc13783_regulators[id].enable_bit);
	mc13783_unlock(priv->mc13783);

	return ret;
}

static int mc13783_regulator_disable(struct regulator_dev *rdev)
{
	struct mc13783_regulator_priv *priv = rdev_get_drvdata(rdev);
	int id = rdev_get_id(rdev);
	int ret;

	dev_dbg(rdev_get_dev(rdev), "%s id: %d\n", __func__, id);

	mc13783_lock(priv->mc13783);
	ret = mc13783_reg_rmw(priv->mc13783, mc13783_regulators[id].reg,
			mc13783_regulators[id].enable_bit, 0);
	mc13783_unlock(priv->mc13783);

	return ret;
}

static int mc13783_regulator_is_enabled(struct regulator_dev *rdev)
{
	struct mc13783_regulator_priv *priv = rdev_get_drvdata(rdev);
	int ret, id = rdev_get_id(rdev);
	unsigned int val;

	mc13783_lock(priv->mc13783);
	ret = mc13783_reg_read(priv->mc13783, mc13783_regulators[id].reg, &val);
	mc13783_unlock(priv->mc13783);

	if (ret)
		return ret;

	return (val & mc13783_regulators[id].enable_bit) != 0;
}

static struct regulator_ops mc13783_regulator_ops = {
	.enable = mc13783_regulator_enable,
	.disable = mc13783_regulator_disable,
	.is_enabled = mc13783_regulator_is_enabled,
};

static int __devinit mc13783_regulator_probe(struct platform_device *pdev)
{
	struct mc13783_regulator_priv *priv;
	struct mc13783 *mc13783 = dev_get_drvdata(pdev->dev.parent);
	struct mc13783_regulator_platform_data *pdata =
		dev_get_platdata(&pdev->dev);
	struct mc13783_regulator_init_data *init_data;
	int i, ret;

	dev_dbg(&pdev->dev, "mc13783_regulator_probe id %d\n", pdev->id);

	priv = kzalloc(sizeof(*priv) +
			pdata->num_regulators * sizeof(priv->regulators[0]),
			GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->mc13783 = mc13783;

	for (i = 0; i < pdata->num_regulators; i++) {
		init_data = &pdata->regulators[i];
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
	struct mc13783_regulator_priv *priv = platform_get_drvdata(pdev);
	struct mc13783_regulator_platform_data *pdata =
		dev_get_platdata(&pdev->dev);
	int i;

	for (i = 0; i < pdata->num_regulators; i++)
		regulator_unregister(priv->regulators[i]);

	return 0;
}

static struct platform_driver mc13783_regulator_driver = {
	.driver	= {
		.name	= "mc13783-regulator",
		.owner	= THIS_MODULE,
	},
	.remove		= __devexit_p(mc13783_regulator_remove),
	.probe		= mc13783_regulator_probe,
};

static int __init mc13783_regulator_init(void)
{
	return platform_driver_register(&mc13783_regulator_driver);
}
subsys_initcall(mc13783_regulator_init);

static void __exit mc13783_regulator_exit(void)
{
	platform_driver_unregister(&mc13783_regulator_driver);
}
module_exit(mc13783_regulator_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Sascha Hauer <s.hauer@pengutronix.de");
MODULE_DESCRIPTION("Regulator Driver for Freescale MC13783 PMIC");
MODULE_ALIAS("platform:mc13783-regulator");
