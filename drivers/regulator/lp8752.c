/*
 * LP8752 High Performance Power Management Unit : System Interface Driver
 *
 * Author: zhangqing <zhangqing@rock-chips.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/regmap.h>
#include <linux/uaccess.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>

#define LP8752_CTRL_BUCK0		0x02
#define LP8752_CTRL_BUCK1		0x04
#define LP8752_CTRL_BUCK2		0x06
#define LP8752_CTRL_BUCK3		0x08

#define LP8752_VOUT_BUCK0		0x0a
#define LP8752_VOUT_BUCK1		0x0c
#define LP8752_VOUT_BUCK2		0x0e
#define LP8752_VOUT_BUCK3		0x10

#define LP8752_BUCK_VSEL_MASK		0xff
#define LP8752_REG_MAX			0x2f

enum lp8752_bucks {
	LP8752_BUCK0 = 0,
	LP8752_BUCK1,
	LP8752_BUCK2,
	LP8752_BUCK3,
	LP8752_BUCK_MAX,
};

static const struct regulator_linear_range lp8752_buck_voltage_ranges[] = {
	REGULATOR_LINEAR_RANGE(500000, 0, 23, 10000),
	REGULATOR_LINEAR_RANGE(735000, 24, 157, 5000),
	REGULATOR_LINEAR_RANGE(1420000, 158, 255, 20000),
};

struct lp8752_platform_data {
	int nphase;
	struct device_node *of_node[LP8752_BUCK_MAX];
	struct regulator_desc desc;
	struct regulator_init_data *buck_data[LP8752_BUCK_MAX];
};

struct lp8752_mphase {
	int nreg;
	int buck_id[LP8752_BUCK_MAX];
};

struct lp8752_chip {
	int nphase;
	struct device *dev;
	struct regmap *regmap;
	struct lp8752_platform_data *pdata;
	struct regulator_dev *rdev[LP8752_BUCK_MAX];
};

static int lp8752_buck_set_mode(struct regulator_dev *rdev, unsigned int mode)
{
	struct lp8752_chip *pchip = rdev_get_drvdata(rdev);
	u8 msk = BIT(1);
	int ret;

	switch (mode) {
	case REGULATOR_MODE_FAST:
		/* forced pwm mode */
		ret = regmap_update_bits(pchip->regmap,
					 rdev->desc->enable_reg,
					 msk, 0x1);
		break;
	case REGULATOR_MODE_NORMAL:
		/* automatic pwm/pfm mode */
		ret = regmap_update_bits(pchip->regmap,
					 rdev->desc->enable_reg,
					 msk, 0x0);
		break;
	default:
		dev_err(pchip->dev, "error:lp8752 only support auto and pwm mode\n");
		ret = -EINVAL;
		break;
	}

	return ret;
}

static unsigned int lp8752_buck_get_mode(struct regulator_dev *rdev)
{
	struct lp8752_chip *pchip = rdev_get_drvdata(rdev);
	int ret;
	unsigned int reg;

	ret = regmap_read(pchip->regmap, rdev->desc->enable_reg, &reg);
	if (ret < 0) {
		dev_err(pchip->dev, "i2c acceess error %s\n", __func__);
		return ret;
	}

	return (reg & BIT(1)) ?  REGULATOR_MODE_FAST : REGULATOR_MODE_NORMAL;
}

static struct regulator_ops lp8752_buck_ops = {
	.map_voltage = regulator_map_voltage_linear_range,
	.list_voltage = regulator_list_voltage_linear_range,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.set_mode = lp8752_buck_set_mode,
	.get_mode = lp8752_buck_get_mode,
};

#define lp8752_rail(_id) "lp8752_buck"#_id
#define vin(_id) "vin"#_id

static const struct lp8752_mphase mphase_buck[] = {
	{ 1, { LP8752_BUCK0} },
	{ 2, { LP8752_BUCK0, LP8752_BUCK3 } },
	{ 3, { LP8752_BUCK0, LP8752_BUCK2, LP8752_BUCK3 } },
	{ 4, { LP8752_BUCK0, LP8752_BUCK1, LP8752_BUCK2, LP8752_BUCK3 } },
};

static struct of_regulator_match lp8752_reg_matches[] = {
	{ .name = "lp8752_buck0", .driver_data = (void *)0 },
	{ .name = "lp8752_buck1", .driver_data = (void *)1 },
	{ .name = "lp8752_buck2", .driver_data = (void *)2 },
	{ .name = "lp8752_buck3", .driver_data = (void *)3 },
};

static int lp8752_init_data(struct lp8752_chip *pchip)
{
	int icnt, buck_id, count;
	struct lp8752_platform_data *pdata = pchip->pdata;
	struct device_node *regs, *lp8752_np;

	lp8752_np = of_node_get(pchip->dev->of_node);
	if (!lp8752_np) {
		dev_err(pchip->dev, "Failed to find device node\n");
		return -ENODEV;
	}

	regs = of_find_node_by_name(lp8752_np, "regulators");
	if (!regs)
		return -ENODEV;

	count = of_regulator_match(pchip->dev, regs, lp8752_reg_matches,
				   LP8752_BUCK_MAX);
	of_node_put(regs);
	if ((count <= 0) || (count > LP8752_BUCK_MAX))
		return -ENODEV;

	pchip->nphase = (count - 1) & 0xf;

	/* set default data based on multi-phase config */
	for (icnt = 0; icnt < mphase_buck[pchip->nphase].nreg; icnt++) {
		buck_id = mphase_buck[pchip->nphase].buck_id[icnt];
		pdata->buck_data[buck_id] =
		lp8752_reg_matches[buck_id].init_data;
		pdata->of_node[buck_id] = lp8752_reg_matches[buck_id].of_node;
	}

	return 0;
}

#define lp8752_buck_desc(_id)\
{\
	.name = lp8752_rail(_id),\
	.id   = LP8752_BUCK##_id,\
	.supply_name = vin(_id),\
	.ops  = &lp8752_buck_ops,\
	.n_voltages = LP8752_BUCK_VSEL_MASK + 1,\
	.linear_ranges = lp8752_buck_voltage_ranges,\
	.n_linear_ranges = ARRAY_SIZE(lp8752_buck_voltage_ranges),\
	.type = REGULATOR_VOLTAGE,\
	.enable_reg = LP8752_CTRL_BUCK##_id,\
	.enable_mask = BIT(7),\
	.vsel_reg = LP8752_VOUT_BUCK##_id,\
	.vsel_mask = LP8752_BUCK_VSEL_MASK,\
	.enable_time = 400,\
	.owner = THIS_MODULE,\
}

static struct regulator_desc lp8752_reg_desc[] = {
	lp8752_buck_desc(0),
	lp8752_buck_desc(1),
	lp8752_buck_desc(2),
	lp8752_buck_desc(3),
};

static int lp8752_regulator_init(struct lp8752_chip *pchip)
{
	int ret, icnt, buck_id;
	struct lp8752_platform_data *pdata = pchip->pdata;
	struct regulator_config rconfig = { };

	rconfig.regmap = pchip->regmap;
	rconfig.dev = pchip->dev;
	rconfig.driver_data = pchip;

	for (icnt = 0; icnt < mphase_buck[pchip->nphase].nreg; icnt++) {
		buck_id = mphase_buck[pchip->nphase].buck_id[icnt];
		rconfig.init_data = pdata->buck_data[buck_id];
		rconfig.of_node = pdata->of_node[buck_id];
		pchip->rdev[buck_id] =
		devm_regulator_register(pchip->dev,
					&lp8752_reg_desc[buck_id],
					&rconfig);
		if (IS_ERR(pchip->rdev[buck_id])) {
			ret = PTR_ERR(pchip->rdev[buck_id]);
			pchip->rdev[buck_id] = NULL;
			dev_err(pchip->dev, "regulator init failed: buck %d\n",
				buck_id);
			return ret;
		}
	}

	return 0;
}

static const struct regmap_config lp8752_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = LP8752_REG_MAX,
};

static int lp8752_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int ret;
	struct lp8752_chip *pchip;
	struct lp8752_platform_data *pdata = dev_get_platdata(&client->dev);

	pchip = devm_kzalloc(&client->dev,
			     sizeof(struct lp8752_chip), GFP_KERNEL);
	if (!pchip)
		return -ENOMEM;

	pchip->dev = &client->dev;
	pchip->regmap = devm_regmap_init_i2c(client, &lp8752_regmap);
	if (IS_ERR(pchip->regmap)) {
		ret = PTR_ERR(pchip->regmap);
		dev_err(&client->dev, "fail to allocate regmap %d\n", ret);
		return ret;
	}
	i2c_set_clientdata(client, pchip);

	ret = regmap_update_bits(pchip->regmap,
				 LP8752_CTRL_BUCK0,
				 (1 << 0), 0);
	ret = regmap_update_bits(pchip->regmap,
				 LP8752_CTRL_BUCK2,
				 (1 << 0), 0);

	if (!pdata) {
		pchip->pdata = devm_kzalloc(pchip->dev,
					    sizeof(struct lp8752_platform_data),
					    GFP_KERNEL);
		if (!pchip->pdata)
			return -ENOMEM;

		ret = lp8752_init_data(pchip);
		if (ret < 0) {
			dev_err(&client->dev, "fail to initialize chip\n");
			return ret;
		}
	} else {
		pchip->pdata = pdata;
		pchip->nphase = pdata->nphase;
	}

	lp8752_regulator_init(pchip);

	return 0;
}

static const struct of_device_id lp8752_of_match[] = {
	{ .compatible = "ti,lp8752", },
	{},
};
MODULE_DEVICE_TABLE(of, lp8752_of_match);

static const struct i2c_device_id lp8752_id[] = {
	{"lp8752", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, lp8752_id);

static struct i2c_driver lp8752_i2c_driver = {
	.probe = lp8752_probe,
	.id_table = lp8752_id,
	.driver = {
		.name = "lp8752",
		.of_match_table = of_match_ptr(lp8752_of_match),
	},
};

module_i2c_driver(lp8752_i2c_driver);

MODULE_DESCRIPTION("Texas Instruments lp8752 driver");
MODULE_AUTHOR("Zhang Qing <zhangqing@rock-chips.com>");
MODULE_LICENSE("GPL v2");
