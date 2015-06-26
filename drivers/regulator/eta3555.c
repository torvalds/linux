/*
 * Regulator driver for eta3555 DCDC chip for rk32xx
 *
 * Copyright (C) 2010, 2011 ROCKCHIP, Inc.

 * Based on eta3555.c that is work by zhangqing<zhangqing@rock-chips.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/bug.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/regulator/driver.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/mfd/core.h>

#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/of_gpio.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regmap.h>

#if 0
#define DBG(x...)	pr_info(x)
#else
#define DBG(x...)
#endif


#define DBG_ERR(x...)  pr_err(x)

#define PM_CONTROL
#define ETA3555_SPEED (200 * 1000)
#define ETA3555_NUM_REGULATORS 1

struct eta3555 {
	struct device *dev;
	struct mutex io_lock;
	struct i2c_client *i2c;
	int num_regulators;
	struct regulator_dev **rdev;
	int irq_base;
	int chip_irq;
	int sleep_gpio;
	unsigned int dcdc_slp_voltage[3];
	bool pmic_sleep;
	struct regmap *regmap;
};

struct eta3555_regulator {
	struct device		*dev;
	struct regulator_desc	*desc;
	struct regulator_dev	*rdev;
};


struct eta3555_board {
	int irq;
	int irq_base;
	struct regulator_init_data *eta3555_init_data[ETA3555_NUM_REGULATORS];
	struct device_node *of_node[ETA3555_NUM_REGULATORS];
	int sleep_gpio; /* */
	unsigned int dcdc_slp_voltage[3]; /* buckx_voltage in uV */
	bool sleep;
};

struct eta3555_regulator_subdev {
	int id;
	struct regulator_init_data *initdata;
	struct device_node *reg_node;
};

struct eta3555_platform_data {
	int ono;
	int num_regulators;
	struct eta3555_regulator_subdev *regulators;
	int sleep_gpio; /* */
	unsigned int dcdc_slp_voltage[3]; /* buckx_voltage in uV */
	bool sleep;
};

static int eta3555_reg_read(struct eta3555 *eta3555, u8 reg);
static int eta3555_set_bits(struct eta3555 *eta3555, u8 reg, u16 mask, u16 val);


#define ETA3555_BUCK1_SET_VOL_BASE 0x00
#define ETA3555_BUCK1_SLP_VOL_BASE 0x01
#define ETA3555_CONTR_REG1 0x02
/*#define ETA3555_ID1_REG 0x03*/
/*#define ETA3555_ID2_REG 0x04*/
#define ETA3555_CONTR_REG2 0x05
#define BUCK_VOL_MASK 0x3f
#define VOL_MIN_IDX 0x00
#define VOL_MAX_IDX 0x3f

const static int buck_voltage_map[] = {
	603000, 615750, 628500, 641250, 654000, 666750, 679500, 692250,
	705000, 717750, 730500, 743250, 756000, 768750, 781500, 794250,
	807000, 819750, 832500, 845250, 858000, 870750, 883500, 896250,
	909000, 921750, 934500, 947250, 960000, 972750, 985500, 998250,
	1011000, 1023750, 1036500, 1049250, 1062000, 1074750, 1087500, 1100250,
	1113000, 1125750, 1138500, 1151250, 1164000, 1176750, 1189500, 1202250,
	1215000, 1227750, 1240500, 1253250, 1266000, 1278750, 1291500, 1304250,
	1317000, 1329750, 1342500, 1355250, 1368000, 1380750, 1393500, 1406250
	/*
	 712500, 725000, 737500, 750000, 762500, 775000, 787500, 800000,
	 812500, 825000, 837500,850000, 862500,875000,887500,900000,
	 912500, 925000, 937500,950000, 962500,975000,987500,1000000,
	 1012500, 1025000, 1037500,1050000, 1062500,1075000,1087500,1100000,
	 1112500, 1125000, 1137500,1150000, 1162500,1175000,1187500,1200000,
	 1212500, 1225000, 1237500,1250000, 1262500,1275000,1287500,1300000,
	 1312500, 1325000, 1337500,1350000, 1362500,1375000,1387500,1400000,
	 1412500, 1425000, 1437500,1450000, 1462500,1475000,1487500,1500000,
*/
};

static int eta3555_dcdc_list_voltage(struct regulator_dev *dev, unsigned index)
{
	if (index >= ARRAY_SIZE(buck_voltage_map))
		return -EINVAL;
	return  buck_voltage_map[index];
}


static int eta3555_dcdc_is_enabled(struct regulator_dev *dev)
{
	struct eta3555 *eta3555 = rdev_get_drvdata(dev);
	u16 val;
	u16 mask = 0x80;

	val = eta3555_reg_read(eta3555, ETA3555_BUCK1_SET_VOL_BASE);
	if (val < 0)
		return val;
	val = val&~0x7f;
	if (val & mask)
		return 1;
	else
		return 0;
}


static int eta3555_dcdc_enable(struct regulator_dev *dev)
{
	struct eta3555 *eta3555 = rdev_get_drvdata(dev);
	u16 mask = 0x80;

	return eta3555_set_bits(eta3555, ETA3555_BUCK1_SET_VOL_BASE, mask,
				0x80);
}


static int eta3555_dcdc_disable(struct regulator_dev *dev)
{
	struct eta3555 *eta3555 = rdev_get_drvdata(dev);
	u16 mask = 0x80;

	return eta3555_set_bits(eta3555, ETA3555_BUCK1_SET_VOL_BASE, mask, 0);
}


static int eta3555_dcdc_get_voltage(struct regulator_dev *dev)
{
	struct eta3555 *eta3555 = rdev_get_drvdata(dev);
	u16 reg = 0;
	int val;

	reg = eta3555_reg_read(eta3555, ETA3555_BUCK1_SET_VOL_BASE);
	reg &= BUCK_VOL_MASK;
	val = buck_voltage_map[reg];
	return val;
}


static int eta3555_dcdc_set_voltage(struct regulator_dev *dev,
				    int min_uV, int max_uV,
				    unsigned *selector)
{
	struct eta3555 *eta3555 = rdev_get_drvdata(dev);
	const int *vol_map = buck_voltage_map;
	u16 val;
	int ret = 0;

	if (min_uV < vol_map[VOL_MIN_IDX] ||
	    min_uV > vol_map[VOL_MAX_IDX])
		return -EINVAL;
	for (val = VOL_MIN_IDX; val <= VOL_MAX_IDX; val++) {
		if (vol_map[val] >= min_uV)
			break;
	}
	if (vol_map[val] > max_uV) {
		DBG_ERR("WARNING:this voltage is not support!voltage set");
		DBG_ERR(" is %d mv\n", vol_map[val]);
	}
	ret = eta3555_set_bits(eta3555, ETA3555_BUCK1_SET_VOL_BASE,
			       BUCK_VOL_MASK, val);
	if (ret < 0) {
		DBG_ERR("###################WARNING:set voltage is error!");
		DBG_ERR("voltage set is %d mv %d\n", vol_map[val], ret);
	}
	return ret;
}


static unsigned int eta3555_dcdc_get_mode(struct regulator_dev *dev)
{
	struct eta3555 *eta3555 = rdev_get_drvdata(dev);
	u16 mask = 0x40;
	u16 val;

	val = eta3555_reg_read(eta3555, ETA3555_BUCK1_SET_VOL_BASE);
	if (val < 0)
		return val;
	val = val & mask;
	if (val == mask)
		return REGULATOR_MODE_FAST;
	else
		return REGULATOR_MODE_NORMAL;
}


static int eta3555_dcdc_set_mode(struct regulator_dev *dev, unsigned int mode)
{
	struct eta3555 *eta3555 = rdev_get_drvdata(dev);
	u16 mask = 0x4;

	switch (mode) {
	case REGULATOR_MODE_FAST:
		return eta3555_set_bits(eta3555, ETA3555_BUCK1_SET_VOL_BASE,
					mask, mask);
	case REGULATOR_MODE_NORMAL:
		return eta3555_set_bits(eta3555, ETA3555_BUCK1_SET_VOL_BASE,
			mask, 0);
	default:
		DBG("error:dcdc_eta3555 only auto and pwm mode\n");
		return -EINVAL;
	}
}


static int eta3555_dcdc_set_voltage_time_sel(struct regulator_dev *dev,
					     unsigned int old_selector,
					     unsigned int new_selector)
{
	int old_volt;
	int new_volt;

	old_volt = eta3555_dcdc_list_voltage(dev, old_selector);
	if (old_volt < 0)
		return old_volt;
	new_volt = eta3555_dcdc_list_voltage(dev, new_selector);
	if (new_volt < 0)
		return new_volt;
	return DIV_ROUND_UP(abs(old_volt - new_volt)*4, 10000);
}


static int eta3555_dcdc_suspend_enable(struct regulator_dev *dev)
{
	struct eta3555 *eta3555 = rdev_get_drvdata(dev);
	u16 mask = 0x80;

	return eta3555_set_bits(eta3555, ETA3555_BUCK1_SLP_VOL_BASE,
				mask, 0x80);
}


static int eta3555_dcdc_suspend_disable(struct regulator_dev *dev)
{
	struct eta3555 *eta3555 = rdev_get_drvdata(dev);
	u16 mask = 0x80;

	return eta3555_set_bits(eta3555, ETA3555_BUCK1_SLP_VOL_BASE, mask,
				0);
}

static int eta3555_dcdc_set_sleep_voltage(struct regulator_dev *dev,
					  int uV)
{
	struct eta3555 *eta3555 = rdev_get_drvdata(dev);
	const int *vol_map = buck_voltage_map;
	u16 val;
	int ret = 0;

	if (uV < vol_map[VOL_MIN_IDX] ||
	    uV > vol_map[VOL_MAX_IDX])
		return -EINVAL;
	for (val = VOL_MIN_IDX; val <= VOL_MAX_IDX; val++) {
		if (vol_map[val] >= uV)
			break;
	}
	if (vol_map[val] > uV) {
		DBG("WARNING:this voltage is not support!voltage set")
		DBG(" is %d mv\n", vol_map[val]);
	}
	ret = eta3555_set_bits(eta3555, ETA3555_BUCK1_SLP_VOL_BASE,
			       BUCK_VOL_MASK, val);
	return ret;
}


static int eta3555_dcdc_set_suspend_mode(struct regulator_dev *dev,
					 unsigned int mode)
{
	struct eta3555 *eta3555 = rdev_get_drvdata(dev);
	u16 mask = 0x40;

	switch (mode) {
	case REGULATOR_MODE_FAST:
		return eta3555_set_bits(eta3555, ETA3555_BUCK1_SLP_VOL_BASE,
					mask, mask);
	case REGULATOR_MODE_NORMAL:
		return eta3555_set_bits(eta3555, ETA3555_BUCK1_SLP_VOL_BASE,
					mask, 0);
	default:
		DBG_ERR("error:dcdc_eta3555 only auto and pwm mode\n");
		return -EINVAL;
	}
}

static struct regulator_ops eta3555_dcdc_ops = {
	.set_voltage = eta3555_dcdc_set_voltage,
	.get_voltage = eta3555_dcdc_get_voltage,
	.list_voltage = eta3555_dcdc_list_voltage,
	.is_enabled = eta3555_dcdc_is_enabled,
	.enable = eta3555_dcdc_enable,
	.disable = eta3555_dcdc_disable,
	.get_mode = eta3555_dcdc_get_mode,
	.set_mode = eta3555_dcdc_set_mode,
	.set_suspend_voltage = eta3555_dcdc_set_sleep_voltage,
	.set_suspend_enable = eta3555_dcdc_suspend_enable,
	.set_suspend_disable = eta3555_dcdc_suspend_disable,
	.set_suspend_mode = eta3555_dcdc_set_suspend_mode,
	.set_voltage_time_sel = eta3555_dcdc_set_voltage_time_sel,
};


static struct regulator_desc regulators[] = {
	{
		.name = "ETA_DCDC1",
		.id = 0,
		.ops = &eta3555_dcdc_ops,
		.n_voltages = ARRAY_SIZE(buck_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
};

static int eta3555_i2c_read(struct i2c_client *i2c, char reg, int count,
			    u16 *dest)
{
	int ret;
	struct i2c_adapter *adap;
	struct i2c_msg msgs[2];

	if (!i2c)
		return ret;
	if (count != 1)
		return -EIO;
	adap = i2c->adapter;
	msgs[0].addr = i2c->addr;
	msgs[0].buf = &reg;
	msgs[0].flags = i2c->flags;
	msgs[0].len = 1;
	msgs[0].scl_rate = ETA3555_SPEED;
	msgs[1].buf = (u8 *)dest;
	msgs[1].addr = i2c->addr;
	msgs[1].flags = i2c->flags | I2C_M_RD;
	msgs[1].len = 1;
	msgs[1].scl_rate = ETA3555_SPEED;
	ret = i2c_transfer(adap, msgs, 2);
	DBG("***run in %s %d msgs[1].buf = %d\n", __func__,
	    __LINE__, *(msgs[1].buf));
	return ret;
}

static int eta3555_i2c_write(struct i2c_client *i2c, char reg,
			     int count, const u16 src)
{
	int ret = -1;
	struct i2c_adapter *adap;
	struct i2c_msg msg;
	char tx_buf[2];

	if (!i2c)
		return ret;
	if (count != 1)
		return -EIO;
	adap = i2c->adapter;
	tx_buf[0] = reg;
	tx_buf[1] = src;
	msg.addr = i2c->addr;
	msg.buf = &tx_buf[0];
	msg.len = 1 + 1;
	msg.flags = i2c->flags;
	msg.scl_rate = ETA3555_SPEED;
	ret = i2c_transfer(adap, &msg, 1);
	return ret;
}

static int eta3555_reg_read(struct eta3555 *eta3555, u8 reg)
{
	u16 val = 0;
	int ret;

	mutex_lock(&eta3555->io_lock);
	ret = eta3555_i2c_read(eta3555->i2c, reg, 1, &val);
	DBG("reg read 0x%02x -> 0x%02x\n", (int)reg, (unsigned)val&0xff);
	if (ret < 0) {
		mutex_unlock(&eta3555->io_lock);
		return ret;
	}
	mutex_unlock(&eta3555->io_lock);
	return val & 0xff;
}

static int eta3555_set_bits(struct eta3555 *eta3555, u8 reg, u16 mask, u16 val)
{
	u16 tmp;
	int ret;

	mutex_lock(&eta3555->io_lock);
	ret = eta3555_i2c_read(eta3555->i2c, reg, 1, &tmp);
	DBG("1 reg read 0x%02x -> 0x%02x\n", (int)reg, (unsigned)tmp&0xff);
	if (ret < 0) {
		mutex_unlock(&eta3555->io_lock);
		return ret;
	}
	tmp = (tmp & ~mask) | val;
	ret = eta3555_i2c_write(eta3555->i2c, reg, 1, tmp);
	DBG("reg write 0x%02x -> 0x%02x\n", (int)reg, (unsigned)val & 0xff);
	if (ret < 0) {
		mutex_unlock(&eta3555->io_lock);
		return ret;
	}
	ret = eta3555_i2c_read(eta3555->i2c, reg, 1, &tmp);
	DBG("2 reg read 0x%02x -> 0x%02x\n", (int)reg, (unsigned)tmp & 0xff);
	if (ret < 0) {
		mutex_unlock(&eta3555->io_lock);
		return ret;
	}
	mutex_unlock(&eta3555->io_lock);
	return 0;
}

#ifdef CONFIG_OF
static struct of_device_id eta3555_of_match[] = {
	{ .compatible = "eta3555"},
	{ },
};
MODULE_DEVICE_TABLE(of, eta3555_of_match);
#endif
#ifdef CONFIG_OF
static struct of_regulator_match eta3555_reg_matches[] = {
	{ .name = "eta_dcdc1", .driver_data = (void *)0},
};

static struct eta3555_board *eta3555_parse_dt(struct eta3555 *eta3555)
{
	struct eta3555_board *pdata;
	struct device_node *regs;
	struct device_node *eta3555_np;
	int count;

	DBG("%s,line=%d\n",  __func__, __LINE__);
	eta3555_np = of_node_get(eta3555->dev->of_node);
	if (!eta3555_np) {
		DBG_ERR("could not find pmic sub-node\n");
		return NULL;
	}
	regs = of_find_node_by_name(eta3555_np, "regulators");
	if (!regs)
		return NULL;
	count = of_regulator_match(eta3555->dev, regs, eta3555_reg_matches,
				   ETA3555_NUM_REGULATORS);
	of_node_put(regs);
	pdata = devm_kzalloc(eta3555->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return NULL;
	pdata->eta3555_init_data[0] = eta3555_reg_matches[0].init_data;
	pdata->of_node[0] = eta3555_reg_matches[0].of_node;
	return pdata;
}

#else
static struct eta3555_board *eta3555_parse_dt(struct i2c_client *i2c)
{
	return NULL;
}
#endif

static int eta3555_i2c_probe(struct i2c_client *i2c,
			     const struct i2c_device_id *id)
{
	struct eta3555 *eta3555;
	struct eta3555_board *pdev;
	const struct of_device_id *match;
	struct regulator_config config = { };
	struct regulator_dev *eta_rdev;
	struct regulator_init_data *reg_data;
	const char *rail_name = NULL;
	int ret;

	DBG("%s, line=%d\n",  __func__, __LINE__);
	if (i2c->dev.of_node) {
		match = of_match_device(eta3555_of_match, &i2c->dev);
		if (!match) {
			DBG_ERR("Failed to find matching dt id\n");
			return -EINVAL;
		}
	}

	eta3555 = devm_kzalloc(&i2c->dev, sizeof(struct eta3555),
			       GFP_KERNEL);
	if (eta3555 == NULL) {
		ret = -ENOMEM;
		goto err;
	}
	eta3555->i2c = i2c;
	eta3555->dev = &i2c->dev;
	i2c_set_clientdata(i2c, eta3555);
	mutex_init(&eta3555->io_lock);
	if (eta3555->dev->of_node)
		pdev = eta3555_parse_dt(eta3555);

	if (pdev) {
		eta3555->num_regulators = ETA3555_NUM_REGULATORS;
		eta3555->rdev = kcalloc(ETA3555_NUM_REGULATORS,
					sizeof(struct regulator_dev *),
					GFP_KERNEL);
		if (!eta3555->rdev)
			return -ENOMEM;
		/* Instantiate the regulators */
		reg_data = pdev->eta3555_init_data[0];
		config.dev = eta3555->dev;
		config.driver_data = eta3555;
		if (eta3555->dev->of_node)
			config.of_node = pdev->of_node[0];
			if (reg_data && reg_data->constraints.name)
				rail_name = reg_data->constraints.name;
			else
				rail_name = regulators[0].name;
			reg_data->supply_regulator = rail_name;

		config.init_data = reg_data;
		eta_rdev = regulator_register(&regulators[0], &config);
		if (IS_ERR(eta_rdev)) {
			DBG_ERR("failed to register eta3555\n");
			goto err;
		}
		eta3555->rdev[0] = eta_rdev;
	}
	return 0;
err:
	return ret;
}

static int  eta3555_i2c_remove(struct i2c_client *i2c)
{
	struct eta3555 *eta3555 = i2c_get_clientdata(i2c);

	if (eta3555->rdev[0])
		regulator_unregister(eta3555->rdev[0]);
	i2c_set_clientdata(i2c, NULL);
	return 0;
}

static const struct i2c_device_id eta3555_i2c_id[] = {
	{ "eta3555", 0 },
};

MODULE_DEVICE_TABLE(i2c, eta3555_i2c_id);

static struct i2c_driver eta3555_i2c_driver = {
	.driver = {
		.name = "eta3555",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(eta3555_of_match),
	},
	.probe    = eta3555_i2c_probe,
	.remove   = eta3555_i2c_remove,
	.id_table = eta3555_i2c_id,
};

static int __init eta3555_module_init(void)
{
	int ret;

	ret = i2c_add_driver(&eta3555_i2c_driver);
	if (ret != 0)
		pr_err("Failed to register I2C driver: %d\n", ret);
	return ret;
}
subsys_initcall_sync(eta3555_module_init);

static void __exit eta3555_module_exit(void)
{
	i2c_del_driver(&eta3555_i2c_driver);
}
module_exit(eta3555_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("zhangqing <zhangqing@rock-chips.com>");
MODULE_DESCRIPTION("eta3555 PMIC driver");
