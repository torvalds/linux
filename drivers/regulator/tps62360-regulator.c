/*
 * tps62360.c -- TI tps62360
 *
 * Driver for processor core supply tps62360, tps62361B, tps62362 and tps62363.
 *
 * Copyright (c) 2012, NVIDIA Corporation.
 *
 * Author: Laxman Dewangan <ldewangan@nvidia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any kind,
 * whether express or implied; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307, USA
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/regulator/of_regulator.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/tps62360.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/regmap.h>

/* Register definitions */
#define REG_VSET0		0
#define REG_VSET1		1
#define REG_VSET2		2
#define REG_VSET3		3
#define REG_CONTROL		4
#define REG_TEMP		5
#define REG_RAMPCTRL		6
#define REG_CHIPID		8

#define FORCE_PWM_ENABLE	BIT(7)

enum chips {TPS62360, TPS62361, TPS62362, TPS62363};

#define TPS62360_BASE_VOLTAGE	770000
#define TPS62360_N_VOLTAGES	64

#define TPS62361_BASE_VOLTAGE	500000
#define TPS62361_N_VOLTAGES	128

/* tps 62360 chip information */
struct tps62360_chip {
	struct device *dev;
	struct regulator_desc desc;
	struct regulator_dev *rdev;
	struct regmap *regmap;
	int vsel0_gpio;
	int vsel1_gpio;
	u8 voltage_reg_mask;
	bool en_internal_pulldn;
	bool en_discharge;
	bool valid_gpios;
	int lru_index[4];
	int curr_vset_vsel[4];
	int curr_vset_id;
};

/*
 * find_voltage_set_register: Find new voltage configuration register
 * (VSET) id.
 * The finding of the new VSET register will be based on the LRU mechanism.
 * Each VSET register will have different voltage configured . This
 * Function will look if any of the VSET register have requested voltage set
 * or not.
 *     - If it is already there then it will make that register as most
 *       recently used and return as found so that caller need not to set
 *       the VSET register but need to set the proper gpios to select this
 *       VSET register.
 *     - If requested voltage is not found then it will use the least
 *       recently mechanism to get new VSET register for new configuration
 *       and will return not_found so that caller need to set new VSET
 *       register and then gpios (both).
 */
static bool find_voltage_set_register(struct tps62360_chip *tps,
		int req_vsel, int *vset_reg_id)
{
	int i;
	bool found = false;
	int new_vset_reg = tps->lru_index[3];
	int found_index = 3;

	for (i = 0; i < 4; ++i) {
		if (tps->curr_vset_vsel[tps->lru_index[i]] == req_vsel) {
			new_vset_reg = tps->lru_index[i];
			found_index = i;
			found = true;
			goto update_lru_index;
		}
	}

update_lru_index:
	for (i = found_index; i > 0; i--)
		tps->lru_index[i] = tps->lru_index[i - 1];

	tps->lru_index[0] = new_vset_reg;
	*vset_reg_id = new_vset_reg;
	return found;
}

static int tps62360_dcdc_get_voltage_sel(struct regulator_dev *dev)
{
	struct tps62360_chip *tps = rdev_get_drvdata(dev);
	int vsel;
	unsigned int data;
	int ret;

	ret = regmap_read(tps->regmap, REG_VSET0 + tps->curr_vset_id, &data);
	if (ret < 0) {
		dev_err(tps->dev, "%s(): register %d read failed with err %d\n",
			__func__, REG_VSET0 + tps->curr_vset_id, ret);
		return ret;
	}
	vsel = (int)data & tps->voltage_reg_mask;
	return vsel;
}

static int tps62360_dcdc_set_voltage_sel(struct regulator_dev *dev,
					 unsigned selector)
{
	struct tps62360_chip *tps = rdev_get_drvdata(dev);
	int ret;
	bool found = false;
	int new_vset_id = tps->curr_vset_id;

	/*
	 * If gpios are available to select the VSET register then least
	 * recently used register for new configuration.
	 */
	if (tps->valid_gpios)
		found = find_voltage_set_register(tps, selector, &new_vset_id);

	if (!found) {
		ret = regmap_update_bits(tps->regmap, REG_VSET0 + new_vset_id,
				tps->voltage_reg_mask, selector);
		if (ret < 0) {
			dev_err(tps->dev,
				"%s(): register %d update failed with err %d\n",
				 __func__, REG_VSET0 + new_vset_id, ret);
			return ret;
		}
		tps->curr_vset_id = new_vset_id;
		tps->curr_vset_vsel[new_vset_id] = selector;
	}

	/* Select proper VSET register vio gpios */
	if (tps->valid_gpios) {
		gpio_set_value_cansleep(tps->vsel0_gpio, new_vset_id & 0x1);
		gpio_set_value_cansleep(tps->vsel1_gpio,
					(new_vset_id >> 1) & 0x1);
	}
	return 0;
}

static int tps62360_set_mode(struct regulator_dev *rdev, unsigned int mode)
{
	struct tps62360_chip *tps = rdev_get_drvdata(rdev);
	int i;
	int val;
	int ret;

	/* Enable force PWM mode in FAST mode only. */
	switch (mode) {
	case REGULATOR_MODE_FAST:
		val = FORCE_PWM_ENABLE;
		break;

	case REGULATOR_MODE_NORMAL:
		val = 0;
		break;

	default:
		return -EINVAL;
	}

	if (!tps->valid_gpios) {
		ret = regmap_update_bits(tps->regmap,
			REG_VSET0 + tps->curr_vset_id, FORCE_PWM_ENABLE, val);
		if (ret < 0)
			dev_err(tps->dev,
				"%s(): register %d update failed with err %d\n",
				__func__, REG_VSET0 + tps->curr_vset_id, ret);
		return ret;
	}

	/* If gpios are valid then all register set need to be control */
	for (i = 0; i < 4; ++i) {
		ret = regmap_update_bits(tps->regmap,
					REG_VSET0 + i, FORCE_PWM_ENABLE, val);
		if (ret < 0) {
			dev_err(tps->dev,
				"%s(): register %d update failed with err %d\n",
				__func__, REG_VSET0 + i, ret);
			return ret;
		}
	}
	return ret;
}

static unsigned int tps62360_get_mode(struct regulator_dev *rdev)
{
	struct tps62360_chip *tps = rdev_get_drvdata(rdev);
	unsigned int data;
	int ret;

	ret = regmap_read(tps->regmap, REG_VSET0 + tps->curr_vset_id, &data);
	if (ret < 0) {
		dev_err(tps->dev, "%s(): register %d read failed with err %d\n",
			__func__, REG_VSET0 + tps->curr_vset_id, ret);
		return ret;
	}
	return (data & FORCE_PWM_ENABLE) ?
				REGULATOR_MODE_FAST : REGULATOR_MODE_NORMAL;
}

static struct regulator_ops tps62360_dcdc_ops = {
	.get_voltage_sel	= tps62360_dcdc_get_voltage_sel,
	.set_voltage_sel	= tps62360_dcdc_set_voltage_sel,
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.set_voltage_time_sel	= regulator_set_voltage_time_sel,
	.set_mode		= tps62360_set_mode,
	.get_mode		= tps62360_get_mode,
};

static int __devinit tps62360_init_dcdc(struct tps62360_chip *tps,
		struct tps62360_regulator_platform_data *pdata)
{
	int ret;
	unsigned int ramp_ctrl;

	/* Initialize internal pull up/down control */
	if (tps->en_internal_pulldn)
		ret = regmap_write(tps->regmap, REG_CONTROL, 0xE0);
	else
		ret = regmap_write(tps->regmap, REG_CONTROL, 0x0);
	if (ret < 0) {
		dev_err(tps->dev,
			"%s(): register %d write failed with err %d\n",
			__func__, REG_CONTROL, ret);
		return ret;
	}

	/* Reset output discharge path to reduce power consumption */
	ret = regmap_update_bits(tps->regmap, REG_RAMPCTRL, BIT(2), 0);
	if (ret < 0) {
		dev_err(tps->dev,
			"%s(): register %d update failed with err %d\n",
			__func__, REG_RAMPCTRL, ret);
		return ret;
	}

	/* Get ramp value from ramp control register */
	ret = regmap_read(tps->regmap, REG_RAMPCTRL, &ramp_ctrl);
	if (ret < 0) {
		dev_err(tps->dev,
			"%s(): register %d read failed with err %d\n",
			__func__, REG_RAMPCTRL, ret);
		return ret;
	}
	ramp_ctrl = (ramp_ctrl >> 4) & 0x7;

	/* ramp mV/us = 32/(2^ramp_ctrl) */
	tps->desc.ramp_delay = DIV_ROUND_UP(32000, BIT(ramp_ctrl));
	return ret;
}

static const struct regmap_config tps62360_regmap_config = {
	.reg_bits		= 8,
	.val_bits		= 8,
	.max_register		= REG_CHIPID,
	.cache_type		= REGCACHE_RBTREE,
};

static struct tps62360_regulator_platform_data *
	of_get_tps62360_platform_data(struct device *dev)
{
	struct tps62360_regulator_platform_data *pdata;
	struct device_node *np = dev->of_node;

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata) {
		dev_err(dev, "Memory alloc failed for platform data\n");
		return NULL;
	}

	pdata->reg_init_data = of_get_regulator_init_data(dev, dev->of_node);
	if (!pdata->reg_init_data) {
		dev_err(dev, "Not able to get OF regulator init data\n");
		return NULL;
	}

	pdata->vsel0_gpio = of_get_named_gpio(np, "vsel0-gpio", 0);
	pdata->vsel1_gpio = of_get_named_gpio(np, "vsel1-gpio", 0);

	if (of_find_property(np, "ti,vsel0-state-high", NULL))
		pdata->vsel0_def_state = 1;

	if (of_find_property(np, "ti,vsel1-state-high", NULL))
		pdata->vsel1_def_state = 1;

	if (of_find_property(np, "ti,enable-pull-down", NULL))
		pdata->en_internal_pulldn = true;

	if (of_find_property(np, "ti,enable-vout-discharge", NULL))
		pdata->en_discharge = true;

	return pdata;
}

#if defined(CONFIG_OF)
static const struct of_device_id tps62360_of_match[] = {
	 { .compatible = "ti,tps62360", .data = (void *)TPS62360},
	 { .compatible = "ti,tps62361", .data = (void *)TPS62361},
	 { .compatible = "ti,tps62362", .data = (void *)TPS62362},
	 { .compatible = "ti,tps62363", .data = (void *)TPS62363},
	{},
};
MODULE_DEVICE_TABLE(of, tps62360_of_match);
#endif

static int __devinit tps62360_probe(struct i2c_client *client,
				     const struct i2c_device_id *id)
{
	struct regulator_config config = { };
	struct tps62360_regulator_platform_data *pdata;
	struct regulator_dev *rdev;
	struct tps62360_chip *tps;
	int ret;
	int i;
	int chip_id;

	pdata = client->dev.platform_data;
	chip_id = id->driver_data;

	if (client->dev.of_node) {
		const struct of_device_id *match;
		match = of_match_device(of_match_ptr(tps62360_of_match),
				&client->dev);
		if (!match) {
			dev_err(&client->dev, "Error: No device match found\n");
			return -ENODEV;
		}
		chip_id = (int)match->data;
		if (!pdata)
			pdata = of_get_tps62360_platform_data(&client->dev);
	}

	if (!pdata) {
		dev_err(&client->dev, "%s(): Platform data not found\n",
						__func__);
		return -EIO;
	}

	tps = devm_kzalloc(&client->dev, sizeof(*tps), GFP_KERNEL);
	if (!tps) {
		dev_err(&client->dev, "%s(): Memory allocation failed\n",
						__func__);
		return -ENOMEM;
	}

	tps->en_discharge = pdata->en_discharge;
	tps->en_internal_pulldn = pdata->en_internal_pulldn;
	tps->vsel0_gpio = pdata->vsel0_gpio;
	tps->vsel1_gpio = pdata->vsel1_gpio;
	tps->dev = &client->dev;

	switch (chip_id) {
	case TPS62360:
	case TPS62362:
		tps->desc.min_uV = TPS62360_BASE_VOLTAGE;
		tps->voltage_reg_mask = 0x3F;
		tps->desc.n_voltages = TPS62360_N_VOLTAGES;
		break;
	case TPS62361:
	case TPS62363:
		tps->desc.min_uV = TPS62361_BASE_VOLTAGE;
		tps->voltage_reg_mask = 0x7F;
		tps->desc.n_voltages = TPS62361_N_VOLTAGES;
		break;
	default:
		return -ENODEV;
	}

	tps->desc.name = id->name;
	tps->desc.id = 0;
	tps->desc.ops = &tps62360_dcdc_ops;
	tps->desc.type = REGULATOR_VOLTAGE;
	tps->desc.owner = THIS_MODULE;
	tps->desc.uV_step = 10000;

	tps->regmap = devm_regmap_init_i2c(client, &tps62360_regmap_config);
	if (IS_ERR(tps->regmap)) {
		ret = PTR_ERR(tps->regmap);
		dev_err(&client->dev,
			"%s(): regmap allocation failed with err %d\n",
			__func__, ret);
		return ret;
	}
	i2c_set_clientdata(client, tps);

	tps->curr_vset_id = (pdata->vsel1_def_state & 1) * 2 +
				(pdata->vsel0_def_state & 1);
	tps->lru_index[0] = tps->curr_vset_id;
	tps->valid_gpios = false;

	if (gpio_is_valid(tps->vsel0_gpio) && gpio_is_valid(tps->vsel1_gpio)) {
		int gpio_flags;
		gpio_flags = (pdata->vsel0_def_state) ?
				GPIOF_OUT_INIT_HIGH : GPIOF_OUT_INIT_LOW;
		ret = gpio_request_one(tps->vsel0_gpio,
				gpio_flags, "tps62360-vsel0");
		if (ret) {
			dev_err(&client->dev,
				"%s(): Could not obtain vsel0 GPIO %d: %d\n",
				__func__, tps->vsel0_gpio, ret);
			goto err_gpio0;
		}

		gpio_flags = (pdata->vsel1_def_state) ?
				GPIOF_OUT_INIT_HIGH : GPIOF_OUT_INIT_LOW;
		ret = gpio_request_one(tps->vsel1_gpio,
				gpio_flags, "tps62360-vsel1");
		if (ret) {
			dev_err(&client->dev,
				"%s(): Could not obtain vsel1 GPIO %d: %d\n",
				__func__, tps->vsel1_gpio, ret);
			goto err_gpio1;
		}
		tps->valid_gpios = true;

		/*
		 * Initialize the lru index with vset_reg id
		 * The index 0 will be most recently used and
		 * set with the tps->curr_vset_id */
		for (i = 0; i < 4; ++i)
			tps->lru_index[i] = i;
		tps->lru_index[0] = tps->curr_vset_id;
		tps->lru_index[tps->curr_vset_id] = 0;
	}

	ret = tps62360_init_dcdc(tps, pdata);
	if (ret < 0) {
		dev_err(tps->dev, "%s(): Init failed with err = %d\n",
				__func__, ret);
		goto err_init;
	}

	config.dev = &client->dev;
	config.init_data = pdata->reg_init_data;
	config.driver_data = tps;
	config.of_node = client->dev.of_node;

	/* Register the regulators */
	rdev = regulator_register(&tps->desc, &config);
	if (IS_ERR(rdev)) {
		dev_err(tps->dev,
			"%s(): regulator register failed with err %s\n",
			__func__, id->name);
		ret = PTR_ERR(rdev);
		goto err_init;
	}

	tps->rdev = rdev;
	return 0;

err_init:
	if (gpio_is_valid(tps->vsel1_gpio))
		gpio_free(tps->vsel1_gpio);
err_gpio1:
	if (gpio_is_valid(tps->vsel0_gpio))
		gpio_free(tps->vsel0_gpio);
err_gpio0:
	return ret;
}

/**
 * tps62360_remove - tps62360 driver i2c remove handler
 * @client: i2c driver client device structure
 *
 * Unregister TPS driver as an i2c client device driver
 */
static int __devexit tps62360_remove(struct i2c_client *client)
{
	struct tps62360_chip *tps = i2c_get_clientdata(client);

	if (gpio_is_valid(tps->vsel1_gpio))
		gpio_free(tps->vsel1_gpio);

	if (gpio_is_valid(tps->vsel0_gpio))
		gpio_free(tps->vsel0_gpio);

	regulator_unregister(tps->rdev);
	return 0;
}

static void tps62360_shutdown(struct i2c_client *client)
{
	struct tps62360_chip *tps = i2c_get_clientdata(client);
	int st;

	if (!tps->en_discharge)
		return;

	/* Configure the output discharge path */
	st = regmap_update_bits(tps->regmap, REG_RAMPCTRL, BIT(2), BIT(2));
	if (st < 0)
		dev_err(tps->dev,
			"%s(): register %d update failed with err %d\n",
			__func__, REG_RAMPCTRL, st);
}

static const struct i2c_device_id tps62360_id[] = {
	{.name = "tps62360", .driver_data = TPS62360},
	{.name = "tps62361", .driver_data = TPS62361},
	{.name = "tps62362", .driver_data = TPS62362},
	{.name = "tps62363", .driver_data = TPS62363},
	{},
};

MODULE_DEVICE_TABLE(i2c, tps62360_id);

static struct i2c_driver tps62360_i2c_driver = {
	.driver = {
		.name = "tps62360",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(tps62360_of_match),
	},
	.probe = tps62360_probe,
	.remove = __devexit_p(tps62360_remove),
	.shutdown = tps62360_shutdown,
	.id_table = tps62360_id,
};

static int __init tps62360_init(void)
{
	return i2c_add_driver(&tps62360_i2c_driver);
}
subsys_initcall(tps62360_init);

static void __exit tps62360_cleanup(void)
{
	i2c_del_driver(&tps62360_i2c_driver);
}
module_exit(tps62360_cleanup);

MODULE_AUTHOR("Laxman Dewangan <ldewangan@nvidia.com>");
MODULE_DESCRIPTION("TPS6236x voltage regulator driver");
MODULE_LICENSE("GPL v2");
