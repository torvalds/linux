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
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/mfd/tps65090.h>

struct tps65090_regulator {
	struct device		*dev;
	struct regulator_desc	*desc;
	struct regulator_dev	*rdev;
};

static struct regulator_ops tps65090_ext_control_ops = {
};

static struct regulator_ops tps65090_reg_contol_ops = {
	.enable		= regulator_enable_regmap,
	.disable	= regulator_disable_regmap,
	.is_enabled	= regulator_is_enabled_regmap,
};

static struct regulator_ops tps65090_ldo_ops = {
};

#define tps65090_REG_DESC(_id, _sname, _en_reg, _ops)	\
{							\
	.name = "TPS65090_RAILS"#_id,			\
	.supply_name = _sname,				\
	.id = TPS65090_REGULATOR_##_id,			\
	.ops = &_ops,					\
	.enable_reg = _en_reg,				\
	.enable_mask = BIT(0),				\
	.type = REGULATOR_VOLTAGE,			\
	.owner = THIS_MODULE,				\
}

static struct regulator_desc tps65090_regulator_desc[] = {
	tps65090_REG_DESC(DCDC1, "vsys1",   0x0C, tps65090_reg_contol_ops),
	tps65090_REG_DESC(DCDC2, "vsys2",   0x0D, tps65090_reg_contol_ops),
	tps65090_REG_DESC(DCDC3, "vsys3",   0x0E, tps65090_reg_contol_ops),
	tps65090_REG_DESC(FET1,  "infet1",  0x0F, tps65090_reg_contol_ops),
	tps65090_REG_DESC(FET2,  "infet2",  0x10, tps65090_reg_contol_ops),
	tps65090_REG_DESC(FET3,  "infet3",  0x11, tps65090_reg_contol_ops),
	tps65090_REG_DESC(FET4,  "infet4",  0x12, tps65090_reg_contol_ops),
	tps65090_REG_DESC(FET5,  "infet5",  0x13, tps65090_reg_contol_ops),
	tps65090_REG_DESC(FET6,  "infet6",  0x14, tps65090_reg_contol_ops),
	tps65090_REG_DESC(FET7,  "infet7",  0x15, tps65090_reg_contol_ops),
	tps65090_REG_DESC(LDO1,  "vsys_l1", 0,    tps65090_ldo_ops),
	tps65090_REG_DESC(LDO2,  "vsys_l2", 0,    tps65090_ldo_ops),
};

static inline bool is_dcdc(int id)
{
	switch (id) {
	case TPS65090_REGULATOR_DCDC1:
	case TPS65090_REGULATOR_DCDC2:
	case TPS65090_REGULATOR_DCDC3:
		return true;
	default:
		return false;
	}
}

static int tps65090_config_ext_control(
	struct tps65090_regulator *ri, bool enable)
{
	int ret;
	struct device *parent = ri->dev->parent;
	unsigned int reg_en_reg = ri->desc->enable_reg;

	if (enable)
		ret = tps65090_set_bits(parent, reg_en_reg, 1);
	else
		ret =  tps65090_clr_bits(parent, reg_en_reg, 1);
	if (ret < 0)
		dev_err(ri->dev, "Error in updating reg 0x%x\n", reg_en_reg);
	return ret;
}

static int tps65090_regulator_disable_ext_control(
		struct tps65090_regulator *ri,
		struct tps65090_regulator_plat_data *tps_pdata)
{
	int ret = 0;
	struct device *parent = ri->dev->parent;
	unsigned int reg_en_reg = ri->desc->enable_reg;

	/*
	 * First enable output for internal control if require.
	 * And then disable external control.
	 */
	if (tps_pdata->reg_init_data->constraints.always_on ||
			tps_pdata->reg_init_data->constraints.boot_on) {
		ret =  tps65090_set_bits(parent, reg_en_reg, 0);
		if (ret < 0) {
			dev_err(ri->dev, "Error in set reg 0x%x\n", reg_en_reg);
			return ret;
		}
	}
	return tps65090_config_ext_control(ri, false);
}

static void tps65090_configure_regulator_config(
		struct tps65090_regulator_plat_data *tps_pdata,
		struct regulator_config *config)
{
	if (gpio_is_valid(tps_pdata->gpio)) {
		int gpio_flag = GPIOF_OUT_INIT_LOW;

		if (tps_pdata->reg_init_data->constraints.always_on ||
				tps_pdata->reg_init_data->constraints.boot_on)
			gpio_flag = GPIOF_OUT_INIT_HIGH;

		config->ena_gpio = tps_pdata->gpio;
		config->ena_gpio_flags = gpio_flag;
	}
}

static int tps65090_regulator_probe(struct platform_device *pdev)
{
	struct tps65090 *tps65090_mfd = dev_get_drvdata(pdev->dev.parent);
	struct tps65090_regulator *ri = NULL;
	struct regulator_config config = { };
	struct regulator_dev *rdev;
	struct tps65090_regulator_plat_data *tps_pdata;
	struct tps65090_regulator *pmic;
	struct tps65090_platform_data *tps65090_pdata;
	int num;
	int ret;

	dev_dbg(&pdev->dev, "Probing regulator\n");

	tps65090_pdata = dev_get_platdata(pdev->dev.parent);
	if (!tps65090_pdata) {
		dev_err(&pdev->dev, "Platform data missing\n");
		return -EINVAL;
	}

	pmic = devm_kzalloc(&pdev->dev, TPS65090_REGULATOR_MAX * sizeof(*pmic),
			GFP_KERNEL);
	if (!pmic) {
		dev_err(&pdev->dev, "mem alloc for pmic failed\n");
		return -ENOMEM;
	}

	for (num = 0; num < TPS65090_REGULATOR_MAX; num++) {
		tps_pdata = tps65090_pdata->reg_pdata[num];

		ri = &pmic[num];
		ri->dev = &pdev->dev;
		ri->desc = &tps65090_regulator_desc[num];

		/*
		 * TPS5090 DCDC support the control from external digital input.
		 * Configure it as per platform data.
		 */
		if (tps_pdata && is_dcdc(num) && tps_pdata->reg_init_data) {
			if (tps_pdata->enable_ext_control) {
				tps65090_configure_regulator_config(
						tps_pdata, &config);
				ri->desc->ops = &tps65090_ext_control_ops;
			} else {
				ret = tps65090_regulator_disable_ext_control(
						ri, tps_pdata);
				if (ret < 0) {
					dev_err(&pdev->dev,
						"failed disable ext control\n");
					goto scrub;
				}
			}
		}

		config.dev = &pdev->dev;
		config.driver_data = ri;
		config.regmap = tps65090_mfd->rmap;
		if (tps_pdata)
			config.init_data = tps_pdata->reg_init_data;
		else
			config.init_data = NULL;

		rdev = regulator_register(ri->desc, &config);
		if (IS_ERR(rdev)) {
			dev_err(&pdev->dev, "failed to register regulator %s\n",
				ri->desc->name);
			ret = PTR_ERR(rdev);
			goto scrub;
		}
		ri->rdev = rdev;

		/* Enable external control if it is require */
		if (tps_pdata && is_dcdc(num) && tps_pdata->reg_init_data &&
				tps_pdata->enable_ext_control) {
			ret = tps65090_config_ext_control(ri, true);
			if (ret < 0) {
				/* Increment num to get unregister rdev */
				num++;
				goto scrub;
			}
		}
	}

	platform_set_drvdata(pdev, pmic);
	return 0;

scrub:
	while (--num >= 0) {
		ri = &pmic[num];
		regulator_unregister(ri->rdev);
	}
	return ret;
}

static int tps65090_regulator_remove(struct platform_device *pdev)
{
	struct tps65090_regulator *pmic = platform_get_drvdata(pdev);
	struct tps65090_regulator *ri;
	int num;

	for (num = 0; num < TPS65090_REGULATOR_MAX; ++num) {
		ri = &pmic[num];
		regulator_unregister(ri->rdev);
	}
	return 0;
}

static struct platform_driver tps65090_regulator_driver = {
	.driver	= {
		.name	= "tps65090-pmic",
		.owner	= THIS_MODULE,
	},
	.probe		= tps65090_regulator_probe,
	.remove		= tps65090_regulator_remove,
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
