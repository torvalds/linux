/*
* Regulator driver for DA9055 PMIC
*
* Copyright(c) 2012 Dialog Semiconductor Ltd.
*
* Author: David Dajun Chen <dchen@diasemi.com>
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
*/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>

#include <linux/mfd/da9055/core.h>
#include <linux/mfd/da9055/reg.h>
#include <linux/mfd/da9055/pdata.h>

#define DA9055_MIN_UA		0
#define DA9055_MAX_UA		3

#define DA9055_LDO_MODE_SYNC	0
#define DA9055_LDO_MODE_SLEEP	1

#define DA9055_BUCK_MODE_SLEEP	1
#define DA9055_BUCK_MODE_SYNC	2
#define DA9055_BUCK_MODE_AUTO	3

/* DA9055 REGULATOR IDs */
#define DA9055_ID_BUCK1	0
#define DA9055_ID_BUCK2	1
#define DA9055_ID_LDO1		2
#define DA9055_ID_LDO2		3
#define DA9055_ID_LDO3		4
#define DA9055_ID_LDO4		5
#define DA9055_ID_LDO5		6
#define DA9055_ID_LDO6		7

/* DA9055 BUCK current limit */
static const int da9055_current_limits[] = { 500000, 600000, 700000, 800000 };

struct da9055_conf_reg {
	int reg;
	int sel_mask;
	int en_mask;
};

struct da9055_volt_reg {
	int reg_a;
	int reg_b;
	int sl_shift;
	int v_mask;
};

struct da9055_mode_reg {
	int reg;
	int mask;
	int shift;
};

struct da9055_regulator_info {
	struct regulator_desc reg_desc;
	struct da9055_conf_reg conf;
	struct da9055_volt_reg volt;
	struct da9055_mode_reg mode;
};

struct da9055_regulator {
	struct da9055 *da9055;
	struct da9055_regulator_info *info;
	struct regulator_dev *rdev;
	enum gpio_select reg_rselect;
};

static unsigned int da9055_buck_get_mode(struct regulator_dev *rdev)
{
	struct da9055_regulator *regulator = rdev_get_drvdata(rdev);
	struct da9055_regulator_info *info = regulator->info;
	int ret, mode = 0;

	ret = da9055_reg_read(regulator->da9055, info->mode.reg);
	if (ret < 0)
		return ret;

	switch ((ret & info->mode.mask) >> info->mode.shift) {
	case DA9055_BUCK_MODE_SYNC:
		mode = REGULATOR_MODE_FAST;
		break;
	case DA9055_BUCK_MODE_AUTO:
		mode = REGULATOR_MODE_NORMAL;
		break;
	case DA9055_BUCK_MODE_SLEEP:
		mode = REGULATOR_MODE_STANDBY;
		break;
	}

	return mode;
}

static int da9055_buck_set_mode(struct regulator_dev *rdev,
					unsigned int mode)
{
	struct da9055_regulator *regulator = rdev_get_drvdata(rdev);
	struct da9055_regulator_info *info = regulator->info;
	int val = 0;

	switch (mode) {
	case REGULATOR_MODE_FAST:
		val = DA9055_BUCK_MODE_SYNC << info->mode.shift;
		break;
	case REGULATOR_MODE_NORMAL:
		val = DA9055_BUCK_MODE_AUTO << info->mode.shift;
		break;
	case REGULATOR_MODE_STANDBY:
		val = DA9055_BUCK_MODE_SLEEP << info->mode.shift;
		break;
	}

	return da9055_reg_update(regulator->da9055, info->mode.reg,
				 info->mode.mask, val);
}

static unsigned int da9055_ldo_get_mode(struct regulator_dev *rdev)
{
	struct da9055_regulator *regulator = rdev_get_drvdata(rdev);
	struct da9055_regulator_info *info = regulator->info;
	int ret;

	ret = da9055_reg_read(regulator->da9055, info->volt.reg_b);
	if (ret < 0)
		return ret;

	if (ret >> info->volt.sl_shift)
		return REGULATOR_MODE_STANDBY;
	else
		return REGULATOR_MODE_NORMAL;
}

static int da9055_ldo_set_mode(struct regulator_dev *rdev, unsigned int mode)
{
	struct da9055_regulator *regulator = rdev_get_drvdata(rdev);
	struct da9055_regulator_info *info = regulator->info;
	struct da9055_volt_reg volt = info->volt;
	int val = 0;

	switch (mode) {
	case REGULATOR_MODE_NORMAL:
	case REGULATOR_MODE_FAST:
		val = DA9055_LDO_MODE_SYNC;
		break;
	case REGULATOR_MODE_STANDBY:
		val = DA9055_LDO_MODE_SLEEP;
		break;
	}

	return da9055_reg_update(regulator->da9055, volt.reg_b,
				 1 << volt.sl_shift,
				 val << volt.sl_shift);
}

static int da9055_buck_get_current_limit(struct regulator_dev *rdev)
{
	struct da9055_regulator *regulator = rdev_get_drvdata(rdev);
	struct da9055_regulator_info *info = regulator->info;
	int ret;

	ret = da9055_reg_read(regulator->da9055, DA9055_REG_BUCK_LIM);
	if (ret < 0)
		return ret;

	ret &= info->mode.mask;
	return da9055_current_limits[ret >> info->mode.shift];
}

static int da9055_buck_set_current_limit(struct regulator_dev *rdev, int min_uA,
					 int max_uA)
{
	struct da9055_regulator *regulator = rdev_get_drvdata(rdev);
	struct da9055_regulator_info *info = regulator->info;
	int i;

	for (i = ARRAY_SIZE(da9055_current_limits) - 1; i >= 0; i--) {
		if ((min_uA <= da9055_current_limits[i]) &&
		    (da9055_current_limits[i] <= max_uA))
			return da9055_reg_update(regulator->da9055,
						 DA9055_REG_BUCK_LIM,
						 info->mode.mask,
						 i << info->mode.shift);
	}

	return -EINVAL;
}

static int da9055_regulator_get_voltage_sel(struct regulator_dev *rdev)
{
	struct da9055_regulator *regulator = rdev_get_drvdata(rdev);
	struct da9055_regulator_info *info = regulator->info;
	struct da9055_volt_reg volt = info->volt;
	int ret, sel;

	/*
	 * There are two voltage register set A & B for voltage ramping but
	 * either one of then can be active therefore we first determine
	 * the active register set.
	 */
	ret = da9055_reg_read(regulator->da9055, info->conf.reg);
	if (ret < 0)
		return ret;

	ret &= info->conf.sel_mask;

	/* Get the voltage for the active register set A/B */
	if (ret == DA9055_REGUALTOR_SET_A)
		ret = da9055_reg_read(regulator->da9055, volt.reg_a);
	else
		ret = da9055_reg_read(regulator->da9055, volt.reg_b);

	if (ret < 0)
		return ret;

	sel = (ret & volt.v_mask);
	return sel;
}

static int da9055_regulator_set_voltage_sel(struct regulator_dev *rdev,
					    unsigned int selector)
{
	struct da9055_regulator *regulator = rdev_get_drvdata(rdev);
	struct da9055_regulator_info *info = regulator->info;
	int ret;

	/*
	 * Regulator register set A/B is not selected through GPIO therefore
	 * we use default register set A for voltage ramping.
	 */
	if (regulator->reg_rselect == NO_GPIO) {
		/* Select register set A */
		ret = da9055_reg_update(regulator->da9055, info->conf.reg,
					info->conf.sel_mask, DA9055_SEL_REG_A);
		if (ret < 0)
			return ret;

		/* Set the voltage */
		return da9055_reg_update(regulator->da9055, info->volt.reg_a,
					 info->volt.v_mask, selector);
	}

	/*
	 * Here regulator register set A/B is selected through GPIO.
	 * Therefore we first determine the selected register set A/B and
	 * then set the desired voltage for that register set A/B.
	 */
	ret = da9055_reg_read(regulator->da9055, info->conf.reg);
	if (ret < 0)
		return ret;

	ret &= info->conf.sel_mask;

	/* Set the voltage */
	if (ret == DA9055_REGUALTOR_SET_A)
		return da9055_reg_update(regulator->da9055, info->volt.reg_a,
					 info->volt.v_mask, selector);
	else
		return da9055_reg_update(regulator->da9055, info->volt.reg_b,
					 info->volt.v_mask, selector);
}

static int da9055_regulator_set_suspend_voltage(struct regulator_dev *rdev,
						int uV)
{
	struct da9055_regulator *regulator = rdev_get_drvdata(rdev);
	struct da9055_regulator_info *info = regulator->info;
	int ret;

	/* Select register set B for suspend voltage ramping. */
	if (regulator->reg_rselect == NO_GPIO) {
		ret = da9055_reg_update(regulator->da9055, info->conf.reg,
					info->conf.sel_mask, DA9055_SEL_REG_B);
		if (ret < 0)
			return ret;
	}

	ret = regulator_map_voltage_linear(rdev, uV, uV);
	if (ret < 0)
		return ret;

	return da9055_reg_update(regulator->da9055, info->volt.reg_b,
				 info->volt.v_mask, ret);
}

static int da9055_suspend_enable(struct regulator_dev *rdev)
{
	struct da9055_regulator *regulator = rdev_get_drvdata(rdev);
	struct da9055_regulator_info *info = regulator->info;

	/* Select register set B for voltage ramping. */
	if (regulator->reg_rselect == NO_GPIO)
		return da9055_reg_update(regulator->da9055, info->conf.reg,
					info->conf.sel_mask, DA9055_SEL_REG_B);
	else
		return 0;
}

static int da9055_suspend_disable(struct regulator_dev *rdev)
{
	struct da9055_regulator *regulator = rdev_get_drvdata(rdev);
	struct da9055_regulator_info *info = regulator->info;

	/* Diselect register set B. */
	if (regulator->reg_rselect == NO_GPIO)
		return da9055_reg_update(regulator->da9055, info->conf.reg,
					info->conf.sel_mask, DA9055_SEL_REG_A);
	else
		return 0;
}

static struct regulator_ops da9055_buck_ops = {
	.get_mode = da9055_buck_get_mode,
	.set_mode = da9055_buck_set_mode,

	.get_current_limit = da9055_buck_get_current_limit,
	.set_current_limit = da9055_buck_set_current_limit,

	.get_voltage_sel = da9055_regulator_get_voltage_sel,
	.set_voltage_sel = da9055_regulator_set_voltage_sel,
	.list_voltage = regulator_list_voltage_linear,
	.map_voltage = regulator_map_voltage_linear,
	.is_enabled = regulator_is_enabled_regmap,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,

	.set_suspend_voltage = da9055_regulator_set_suspend_voltage,
	.set_suspend_enable = da9055_suspend_enable,
	.set_suspend_disable = da9055_suspend_disable,
	.set_suspend_mode = da9055_buck_set_mode,
};

static struct regulator_ops da9055_ldo_ops = {
	.get_mode = da9055_ldo_get_mode,
	.set_mode = da9055_ldo_set_mode,

	.get_voltage_sel = da9055_regulator_get_voltage_sel,
	.set_voltage_sel = da9055_regulator_set_voltage_sel,
	.list_voltage = regulator_list_voltage_linear,
	.map_voltage = regulator_map_voltage_linear,
	.is_enabled = regulator_is_enabled_regmap,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,

	.set_suspend_voltage = da9055_regulator_set_suspend_voltage,
	.set_suspend_enable = da9055_suspend_enable,
	.set_suspend_disable = da9055_suspend_disable,
	.set_suspend_mode = da9055_ldo_set_mode,

};

#define DA9055_LDO(_id, step, min, max, vbits, voffset) \
{\
	.reg_desc = {\
		.name = #_id,\
		.ops = &da9055_ldo_ops,\
		.type = REGULATOR_VOLTAGE,\
		.id = DA9055_ID_##_id,\
		.n_voltages = (max - min) / step + 1 + (voffset), \
		.enable_reg = DA9055_REG_BCORE_CONT + DA9055_ID_##_id, \
		.enable_mask = 1, \
		.min_uV = (min) * 1000,\
		.uV_step = (step) * 1000,\
		.linear_min_sel = (voffset),\
		.owner = THIS_MODULE,\
	},\
	.conf = {\
		.reg = DA9055_REG_BCORE_CONT + DA9055_ID_##_id, \
		.sel_mask = (1 << 4),\
		.en_mask = 1,\
	},\
	.volt = {\
		.reg_a = DA9055_REG_VBCORE_A + DA9055_ID_##_id, \
		.reg_b = DA9055_REG_VBCORE_B + DA9055_ID_##_id, \
		.sl_shift = 7,\
		.v_mask = (1 << (vbits)) - 1,\
	},\
}

#define DA9055_BUCK(_id, step, min, max, vbits, voffset, mbits, sbits) \
{\
	.reg_desc = {\
		.name = #_id,\
		.ops = &da9055_buck_ops,\
		.type = REGULATOR_VOLTAGE,\
		.id = DA9055_ID_##_id,\
		.n_voltages = (max - min) / step + 1 + (voffset), \
		.enable_reg = DA9055_REG_BCORE_CONT + DA9055_ID_##_id, \
		.enable_mask = 1,\
		.min_uV = (min) * 1000,\
		.uV_step = (step) * 1000,\
		.linear_min_sel = (voffset),\
		.owner = THIS_MODULE,\
	},\
	.conf = {\
		.reg = DA9055_REG_BCORE_CONT + DA9055_ID_##_id, \
		.sel_mask = (1 << 4),\
		.en_mask = 1,\
	},\
	.volt = {\
		.reg_a = DA9055_REG_VBCORE_A + DA9055_ID_##_id, \
		.reg_b = DA9055_REG_VBCORE_B + DA9055_ID_##_id, \
		.sl_shift = 7,\
		.v_mask = (1 << (vbits)) - 1,\
	},\
	.mode = {\
		.reg = DA9055_REG_BCORE_MODE,\
		.mask = (mbits),\
		.shift = (sbits),\
	},\
}

static struct da9055_regulator_info da9055_regulator_info[] = {
	DA9055_BUCK(BUCK1, 25, 725, 2075, 6, 9, 0xc, 2),
	DA9055_BUCK(BUCK2, 25, 925, 2500, 6, 0, 3, 0),
	DA9055_LDO(LDO1, 50, 900, 3300, 6, 2),
	DA9055_LDO(LDO2, 50, 900, 3300, 6, 3),
	DA9055_LDO(LDO3, 50, 900, 3300, 6, 2),
	DA9055_LDO(LDO4, 50, 900, 3300, 6, 2),
	DA9055_LDO(LDO5, 50, 900, 2750, 6, 2),
	DA9055_LDO(LDO6, 20, 900, 3300, 7, 0),
};

/*
 * Configures regulator to be controlled either through GPIO 1 or 2.
 * GPIO can control regulator state and/or select the regulator register
 * set A/B for voltage ramping.
 */
static int da9055_gpio_init(struct da9055_regulator *regulator,
			    struct regulator_config *config,
			    struct da9055_pdata *pdata, int id)
{
	struct da9055_regulator_info *info = regulator->info;
	int ret = 0;

	if (pdata->gpio_ren && pdata->gpio_ren[id]) {
		char name[18];
		int gpio_mux = pdata->gpio_ren[id];

		config->ena_gpio = pdata->ena_gpio[id];
		config->ena_gpio_flags = GPIOF_OUT_INIT_HIGH;
		config->ena_gpio_invert = 1;

		/*
		 * GPI pin is muxed with regulator to control the
		 * regulator state.
		 */
		sprintf(name, "DA9055 GPI %d", gpio_mux);
		ret = devm_gpio_request_one(config->dev, gpio_mux, GPIOF_DIR_IN,
					    name);
		if (ret < 0)
			goto err;

		/*
		 * Let the regulator know that its state is controlled
		 * through GPI.
		 */
		ret = da9055_reg_update(regulator->da9055, info->conf.reg,
					DA9055_E_GPI_MASK,
					pdata->reg_ren[id]
					<< DA9055_E_GPI_SHIFT);
		if (ret < 0)
			goto err;
	}

	if (pdata->gpio_rsel && pdata->gpio_rsel[id]) {
		char name[18];
		int gpio_mux = pdata->gpio_rsel[id];

		regulator->reg_rselect = pdata->reg_rsel[id];

		/*
		 * GPI pin is muxed with regulator to select the
		 * regulator register set A/B for voltage ramping.
		 */
		sprintf(name, "DA9055 GPI %d", gpio_mux);
		ret = devm_gpio_request_one(config->dev, gpio_mux, GPIOF_DIR_IN,
					    name);
		if (ret < 0)
			goto err;

		/*
		 * Let the regulator know that its register set A/B
		 * will be selected through GPI for voltage ramping.
		 */
		ret = da9055_reg_update(regulator->da9055, info->conf.reg,
					DA9055_V_GPI_MASK,
					pdata->reg_rsel[id]
					<< DA9055_V_GPI_SHIFT);
	}

err:
	return ret;
}

static irqreturn_t da9055_ldo5_6_oc_irq(int irq, void *data)
{
	struct da9055_regulator *regulator = data;

	regulator_notifier_call_chain(regulator->rdev,
				      REGULATOR_EVENT_OVER_CURRENT, NULL);

	return IRQ_HANDLED;
}

static inline struct da9055_regulator_info *find_regulator_info(int id)
{
	struct da9055_regulator_info *info;
	int i;

	for (i = 0; i < ARRAY_SIZE(da9055_regulator_info); i++) {
		info = &da9055_regulator_info[i];
		if (info->reg_desc.id == id)
			return info;
	}

	return NULL;
}

static int da9055_regulator_probe(struct platform_device *pdev)
{
	struct regulator_config config = { };
	struct da9055_regulator *regulator;
	struct da9055 *da9055 = dev_get_drvdata(pdev->dev.parent);
	struct da9055_pdata *pdata = dev_get_platdata(da9055->dev);
	int ret, irq;

	if (pdata == NULL || pdata->regulators[pdev->id] == NULL)
		return -ENODEV;

	regulator = devm_kzalloc(&pdev->dev, sizeof(struct da9055_regulator),
				 GFP_KERNEL);
	if (!regulator)
		return -ENOMEM;

	regulator->info = find_regulator_info(pdev->id);
	if (regulator->info == NULL) {
		dev_err(&pdev->dev, "invalid regulator ID specified\n");
		return -EINVAL;
	}

	regulator->da9055 = da9055;
	config.dev = &pdev->dev;
	config.driver_data = regulator;
	config.regmap = da9055->regmap;

	if (pdata && pdata->regulators)
		config.init_data = pdata->regulators[pdev->id];

	ret = da9055_gpio_init(regulator, &config, pdata, pdev->id);
	if (ret < 0)
		return ret;

	regulator->rdev = devm_regulator_register(&pdev->dev,
						  &regulator->info->reg_desc,
						  &config);
	if (IS_ERR(regulator->rdev)) {
		dev_err(&pdev->dev, "Failed to register regulator %s\n",
			regulator->info->reg_desc.name);
		return PTR_ERR(regulator->rdev);
	}

	/* Only LDO 5 and 6 has got the over current interrupt */
	if (pdev->id == DA9055_ID_LDO5 || pdev->id ==  DA9055_ID_LDO6) {
		irq = platform_get_irq_byname(pdev, "REGULATOR");
		irq = regmap_irq_get_virq(da9055->irq_data, irq);
		ret = devm_request_threaded_irq(&pdev->dev, irq, NULL,
						da9055_ldo5_6_oc_irq,
						IRQF_TRIGGER_HIGH |
						IRQF_ONESHOT |
						IRQF_PROBE_SHARED,
						pdev->name, regulator);
		if (ret != 0) {
			if (ret != -EBUSY) {
				dev_err(&pdev->dev,
				"Failed to request Regulator IRQ %d: %d\n",
				irq, ret);
				return ret;
			}
		}
	}

	platform_set_drvdata(pdev, regulator);

	return 0;
}

static struct platform_driver da9055_regulator_driver = {
	.probe = da9055_regulator_probe,
	.driver = {
		.name = "da9055-regulator",
		.owner = THIS_MODULE,
	},
};

static int __init da9055_regulator_init(void)
{
	return platform_driver_register(&da9055_regulator_driver);
}
subsys_initcall(da9055_regulator_init);

static void __exit da9055_regulator_exit(void)
{
	platform_driver_unregister(&da9055_regulator_driver);
}
module_exit(da9055_regulator_exit);

MODULE_AUTHOR("David Dajun Chen <dchen@diasemi.com>");
MODULE_DESCRIPTION("Power Regulator driver for Dialog DA9055 PMIC");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:da9055-regulator");
