/*
 * Regulator driver for tps65090 power management chip.
 *
 * Copyright (c) 2012, NVIDIA CORPORATION.  All rights reserved.

 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.

 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.

 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/mfd/tps65090.h>
#include <linux/regulator/tps65090-regulator.h>

struct tps65090_regulator {
	int		id;
	/* used by regulator core */
	struct regulator_desc	desc;

	/* Device */
	struct device		*dev;
};

static struct regulator_ops tps65090_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
};

#define tps65090_REG(_id)				\
{							\
	.id		= TPS65090_ID_##_id,		\
	.desc = {					\
		.name = tps65090_rails(_id),		\
		.id = TPS65090_ID_##_id,		\
		.ops = &tps65090_ops,			\
		.type = REGULATOR_VOLTAGE,		\
		.owner = THIS_MODULE,			\
		.enable_reg = (TPS65090_ID_##_id) + 12,	\
		.enable_mask = BIT(0),			\
	},						\
}

static struct tps65090_regulator TPS65090_regulator[] = {
	tps65090_REG(DCDC1),
	tps65090_REG(DCDC2),
	tps65090_REG(DCDC3),
	tps65090_REG(FET1),
	tps65090_REG(FET2),
	tps65090_REG(FET3),
	tps65090_REG(FET4),
	tps65090_REG(FET5),
	tps65090_REG(FET6),
	tps65090_REG(FET7),
};

static inline struct tps65090_regulator *find_regulator_info(int id)
{
	struct tps65090_regulator *ri;
	int i;

	for (i = 0; i < ARRAY_SIZE(TPS65090_regulator); i++) {
		ri = &TPS65090_regulator[i];
		if (ri->desc.id == id)
			return ri;
	}
	return NULL;
}

static int __devinit tps65090_regulator_probe(struct platform_device *pdev)
{
	struct tps65090 *tps65090_mfd = dev_get_drvdata(pdev->dev.parent);
	struct tps65090_regulator *ri = NULL;
	struct regulator_config config = { };
	struct regulator_dev *rdev;
	struct tps65090_regulator_platform_data *tps_pdata;
	int id = pdev->id;

	dev_dbg(&pdev->dev, "Probing regulator %d\n", id);

	ri = find_regulator_info(id);
	if (ri == NULL) {
		dev_err(&pdev->dev, "invalid regulator ID specified\n");
		return -EINVAL;
	}
	tps_pdata = pdev->dev.platform_data;
	ri->dev = &pdev->dev;

	config.dev = &pdev->dev;
	config.init_data = &tps_pdata->regulator;
	config.driver_data = ri;
	config.regmap = tps65090_mfd->rmap;

	rdev = regulator_register(&ri->desc, &config);
	if (IS_ERR(rdev)) {
		dev_err(&pdev->dev, "failed to register regulator %s\n",
				ri->desc.name);
		return PTR_ERR(rdev);
	}

	platform_set_drvdata(pdev, rdev);
	return 0;
}

static int __devexit tps65090_regulator_remove(struct platform_device *pdev)
{
	struct regulator_dev *rdev = platform_get_drvdata(pdev);

	regulator_unregister(rdev);
	return 0;
}

static struct platform_driver tps65090_regulator_driver = {
	.driver	= {
		.name	= "tps65090-regulator",
		.owner	= THIS_MODULE,
	},
	.probe		= tps65090_regulator_probe,
	.remove		= __devexit_p(tps65090_regulator_remove),
};

static int __init tps65090_regulator_init(void)
{
	return platform_driver_register(&tps65090_regulator_driver);
}
subsys_initcall(tps65090_regulator_init);

static void __exit tps65090_regulator_exit(void)
{
	platform_driver_unregister(&tps65090_regulator_driver);
}
module_exit(tps65090_regulator_exit);

MODULE_DESCRIPTION("tps65090 regulator driver");
MODULE_AUTHOR("Venu Byravarasu <vbyravarasu@nvidia.com>");
MODULE_LICENSE("GPL v2");
