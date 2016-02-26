/*
 * Regulator driver for Active-semi act8931 PMIC chip
 *
 * Copyright (C) 2014 ROCKCHIP, Inc.
 *
 * Based on act8931.c in kernel 3.0 that is work by xhc<xhc@rock-chips.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/mutex.h>
#include <linux/bug.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/module.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/driver.h>
#include <asm/system_misc.h>

#if 0
#define DBG(x...)	printk(KERN_INFO x)
#else
#define DBG(x...)
#endif

#define ACT8931_NUM_REGULATORS  7

struct act8931 *g_act8931;

struct act8931 {
	unsigned int irq;
	unsigned int pwr_hold_gpio;
	struct device *dev;
	struct mutex io_lock;
	struct i2c_client *i2c;
	int num_regulators;
	struct regulator_dev **rdev;
};

struct act8931_board {
	int irq_gpio;
	int pwr_hold_gpio;
	bool pm_off;
	struct regulator_init_data *act8931_init_data[ACT8931_NUM_REGULATORS];
	struct device_node *of_node[ACT8931_NUM_REGULATORS];
};

static u8 act8931_reg_read(struct act8931 *act8931, u8 reg);
static int act8931_set_bits(struct act8931 *act8931, u8 reg, u16 mask, u16 val);

#define ACT8931_DCDC1 0
#define ACT8931_DCDC2 1
#define ACT8931_DCDC3 2

#define ACT8931_LDO1  3
#define ACT8931_LDO2  4
#define ACT8931_LDO3  5
#define ACT8931_LDO4  6

#define act8931_BUCK1_SET_VOL_BASE 0x20
#define act8931_BUCK2_SET_VOL_BASE 0x30
#define act8931_BUCK3_SET_VOL_BASE 0x40
#define act8931_LDO1_SET_VOL_BASE 0x50
#define act8931_LDO2_SET_VOL_BASE 0x54
#define act8931_LDO3_SET_VOL_BASE 0x60
#define act8931_LDO4_SET_VOL_BASE 0x64

#define act8931_BUCK1_CONTR_BASE 0x22
#define act8931_BUCK2_CONTR_BASE 0x32
#define act8931_BUCK3_CONTR_BASE 0x42
#define act8931_LDO1_CONTR_BASE 0x51
#define act8931_LDO2_CONTR_BASE 0x55
#define act8931_LDO3_CONTR_BASE 0x61
#define act8931_LDO4_CONTR_BASE 0x65

#define BUCK_VOL_MASK 0x3f
#define LDO_VOL_MASK 0x3f

#define VOL_MIN_IDX 0x00
#define VOL_MAX_IDX 0x3f

#define INSTAT_MASK (1<<5)
#define CHGSTAT_MASK (1<<4)
#define INDAT_MASK (1<<1)
#define CHGDAT_MASK (1<<0)

#define INCON_MASK (1<<5)
#define CHGEOCIN_MASK (1<<4)
#define INDIS_MASK (1<<1)
#define CHGEOCOUT_MASK (1<<0)

int act8931_charge_det;
EXPORT_SYMBOL(act8931_charge_det);

int act8931_charge_ok;
EXPORT_SYMBOL(act8931_charge_ok);

static const int buck_set_vol_base_addr[] = {
	act8931_BUCK1_SET_VOL_BASE,
	act8931_BUCK2_SET_VOL_BASE,
	act8931_BUCK3_SET_VOL_BASE,
};
static const int buck_contr_base_addr[] = {
	act8931_BUCK1_CONTR_BASE,
	act8931_BUCK2_CONTR_BASE,
	act8931_BUCK3_CONTR_BASE,
};
#define act8931_BUCK_SET_VOL_REG(x) (buck_set_vol_base_addr[x])
#define act8931_BUCK_CONTR_REG(x) (buck_contr_base_addr[x])

static const int ldo_set_vol_base_addr[] = {
	act8931_LDO1_SET_VOL_BASE,
	act8931_LDO2_SET_VOL_BASE,
	act8931_LDO3_SET_VOL_BASE,
	act8931_LDO4_SET_VOL_BASE,
};
static const int ldo_contr_base_addr[] = {
	act8931_LDO1_CONTR_BASE,
	act8931_LDO2_CONTR_BASE,
	act8931_LDO3_CONTR_BASE,
	act8931_LDO4_CONTR_BASE,
};
#define act8931_LDO_SET_VOL_REG(x) (ldo_set_vol_base_addr[x])
#define act8931_LDO_CONTR_REG(x) (ldo_contr_base_addr[x])

static const int buck_voltage_map[] = {
	 600, 625, 650, 675, 700, 725, 750, 775,
	 800, 825, 850, 875, 900, 925, 950, 975,
	 1000, 1025, 1050, 1075, 1100, 1125, 1150,
	 1175, 1200, 1250, 1300, 1350, 1400, 1450,
	 1500, 1550, 1600, 1650, 1700, 1750, 1800,
	 1850, 1900, 1950, 2000, 2050, 2100, 2150,
	 2200, 2250, 2300, 2350, 2400, 2500, 2600,
	 2700, 2800, 2850, 2900, 3000, 3100, 3200,
	 3300, 3400, 3500, 3600, 3700, 3800, 3900,
};

static const int ldo_voltage_map[] = {
	 600, 625, 650, 675, 700, 725, 750, 775,
	 800, 825, 850, 875, 900, 925, 950, 975,
	 1000, 1025, 1050, 1075, 1100, 1125, 1150,
	 1175, 1200, 1250, 1300, 1350, 1400, 1450,
	 1500, 1550, 1600, 1650, 1700, 1750, 1800,
	 1850, 1900, 1950, 2000, 2050, 2100, 2150,
	 2200, 2250, 2300, 2350, 2400, 2500, 2600,
	 2700, 2800, 2850, 2900, 3000, 3100, 3200,
	 3300, 3400, 3500, 3600, 3700, 3800, 3900,
};

static int act8931_ldo_list_voltage(struct regulator_dev *dev, unsigned index)
{
	if (index >= ARRAY_SIZE(ldo_voltage_map))
		return -EINVAL;
	return 1000 * ldo_voltage_map[index];
}

static int act8931_ldo_is_enabled(struct regulator_dev *dev)
{
	struct act8931 *act8931 = rdev_get_drvdata(dev);
	int ldo = rdev_get_id(dev) - ACT8931_LDO1;
	u16 val;
	u16 mask = 0x80;

	val = act8931_reg_read(act8931, act8931_LDO_CONTR_REG(ldo));
	if (val < 0)
		return val;
	val = val & ~0x7f;
	if (val & mask)
		return 1;
	else
		return 0;
}

static int act8931_ldo_enable(struct regulator_dev *dev)
{
	struct act8931 *act8931 = rdev_get_drvdata(dev);
	int ldo = rdev_get_id(dev) - ACT8931_LDO1;
	u16 mask = 0x80;

	return act8931_set_bits(act8931, act8931_LDO_CONTR_REG(ldo), mask,
				0x80);
}

static int act8931_ldo_disable(struct regulator_dev *dev)
{
	struct act8931 *act8931 = rdev_get_drvdata(dev);
	int ldo = rdev_get_id(dev) - ACT8931_LDO1;
	u16 mask = 0x80;

	return act8931_set_bits(act8931, act8931_LDO_CONTR_REG(ldo), mask, 0);
}

static int act8931_ldo_get_voltage(struct regulator_dev *dev)
{
	struct act8931 *act8931 = rdev_get_drvdata(dev);
	int ldo = rdev_get_id(dev) - ACT8931_LDO1;
	u16 reg = 0;
	int val;

	reg = act8931_reg_read(act8931, act8931_LDO_SET_VOL_REG(ldo));
	reg &= LDO_VOL_MASK;
	val = 1000 * ldo_voltage_map[reg];

	return val;
}

static int act8931_ldo_set_voltage(struct regulator_dev *dev,
				   int min_uV, int max_uV, unsigned *selector)
{
	struct act8931 *act8931 = rdev_get_drvdata(dev);
	int ldo = rdev_get_id(dev) - ACT8931_LDO1;
	int min_vol = min_uV / 1000, max_vol = max_uV / 1000;
	const int *vol_map = ldo_voltage_map;
	u16 val;
	int ret = 0;

	if (min_vol < vol_map[VOL_MIN_IDX] || min_vol > vol_map[VOL_MAX_IDX])
		return -EINVAL;

	for (val = VOL_MIN_IDX; val <= VOL_MAX_IDX; val++) {
		if (vol_map[val] >= min_vol)
			break;
	}

	if (vol_map[val] > max_vol)
		return -EINVAL;

	ret = act8931_set_bits(act8931, act8931_LDO_SET_VOL_REG(ldo),
			       LDO_VOL_MASK, val);

	return ret;
}

static unsigned int act8931_ldo_get_mode(struct regulator_dev *dev)
{
	struct act8931 *act8931 = rdev_get_drvdata(dev);
	int ldo = rdev_get_id(dev) - ACT8931_LDO1;
	u16 mask = 0xcf;
	u16 val;

	val = act8931_reg_read(act8931, act8931_LDO_CONTR_REG(ldo));
	val = val | mask;

	if (val == mask)
		return REGULATOR_MODE_NORMAL;
	else
		return REGULATOR_MODE_STANDBY;
}

static int act8931_ldo_set_mode(struct regulator_dev *dev, unsigned int mode)
{
	struct act8931 *act8931 = rdev_get_drvdata(dev);
	int ldo = rdev_get_id(dev) - ACT8931_LDO1;
	u16 mask = 0x20;

	switch (mode) {
	case REGULATOR_MODE_NORMAL:
		return act8931_set_bits(act8931, act8931_LDO_CONTR_REG(ldo),
					mask, 0);
	case REGULATOR_MODE_STANDBY:
		return act8931_set_bits(act8931, act8931_LDO_CONTR_REG(ldo),
					mask, mask);
	default:
		pr_err("pmu_act8931 only lowpower and normal mode\n");
		return -EINVAL;
	}
}

static struct regulator_ops act8931_ldo_ops = {
	.set_voltage = act8931_ldo_set_voltage,
	.get_voltage = act8931_ldo_get_voltage,
	.list_voltage = act8931_ldo_list_voltage,
	.is_enabled = act8931_ldo_is_enabled,
	.enable = act8931_ldo_enable,
	.disable = act8931_ldo_disable,
	.get_mode = act8931_ldo_get_mode,
	.set_mode = act8931_ldo_set_mode,
};

static int act8931_dcdc_list_voltage(struct regulator_dev *dev, unsigned index)
{
	if (index >= ARRAY_SIZE(buck_voltage_map))
		return -EINVAL;

	return 1000 * buck_voltage_map[index];
}

static int act8931_dcdc_is_enabled(struct regulator_dev *dev)
{
	struct act8931 *act8931 = rdev_get_drvdata(dev);
	int buck = rdev_get_id(dev) - ACT8931_DCDC1;
	u16 val;
	u16 mask = 0x80;

	val = act8931_reg_read(act8931, act8931_BUCK_CONTR_REG(buck));
	if (val < 0)
		return val;
	val = val & ~0x7f;
	if (val & mask)
		return 1;
	else
		return 0;
}

static int act8931_dcdc_enable(struct regulator_dev *dev)
{
	struct act8931 *act8931 = rdev_get_drvdata(dev);
	int buck = rdev_get_id(dev) - ACT8931_DCDC1;
	u16 mask = 0x80;

	return act8931_set_bits(act8931, act8931_BUCK_CONTR_REG(buck), mask,
				0x80);
}

static int act8931_dcdc_disable(struct regulator_dev *dev)
{
	struct act8931 *act8931 = rdev_get_drvdata(dev);
	int buck = rdev_get_id(dev) - ACT8931_DCDC1;
	u16 mask = 0x80;

	return act8931_set_bits(act8931, act8931_BUCK_CONTR_REG(buck), mask, 0);
}

static int act8931_dcdc_get_voltage(struct regulator_dev *dev)
{
	struct act8931 *act8931 = rdev_get_drvdata(dev);
	int buck = rdev_get_id(dev) - ACT8931_DCDC1;
	u16 reg = 0;
	int val;

	reg = act8931_reg_read(act8931, act8931_BUCK_SET_VOL_REG(buck));
	reg &= BUCK_VOL_MASK;
	DBG("%d\n", reg);
	val = 1000 * buck_voltage_map[reg];
	DBG("%d\n", val);

	return val;
}

static int act8931_dcdc_set_voltage(struct regulator_dev *dev,
				    int min_uV, int max_uV, unsigned *selector)
{
	struct act8931 *act8931 = rdev_get_drvdata(dev);
	int buck = rdev_get_id(dev) - ACT8931_DCDC1;
	int min_vol = min_uV / 1000, max_vol = max_uV / 1000;
	const int *vol_map = buck_voltage_map;
	u16 val;
	int ret = 0;

	DBG("%s, min_uV = %d, max_uV = %d!\n", __func__, min_uV, max_uV);
	if (min_vol < vol_map[VOL_MIN_IDX] ||
	    min_vol > vol_map[VOL_MAX_IDX])
		return -EINVAL;

	for (val = VOL_MIN_IDX; val <= VOL_MAX_IDX; val++) {
		if (vol_map[val] >= min_vol)
			break;
	}

	if (vol_map[val] > max_vol)
		pr_warn("this voltage is not support!voltage set is %d mv\n",
			vol_map[val]);

	ret = act8931_set_bits(act8931, act8931_BUCK_SET_VOL_REG(buck),
			       BUCK_VOL_MASK, val);
	ret = act8931_set_bits(act8931, act8931_BUCK_SET_VOL_REG(buck) + 0x01,
			       BUCK_VOL_MASK, val);

	return ret;
}

static unsigned int act8931_dcdc_get_mode(struct regulator_dev *dev)
{
	struct act8931 *act8931 = rdev_get_drvdata(dev);
	int buck = rdev_get_id(dev) - ACT8931_DCDC1;
	u16 mask = 0xcf;
	u16 val;

	val = act8931_reg_read(act8931, act8931_BUCK_CONTR_REG(buck));
	val = val | mask;
	if (val == mask)
		return REGULATOR_MODE_STANDBY;
	else
		return REGULATOR_MODE_NORMAL;
}

static int act8931_dcdc_set_mode(struct regulator_dev *dev, unsigned int mode)
{
	struct act8931 *act8931 = rdev_get_drvdata(dev);
	int buck = rdev_get_id(dev) - ACT8931_DCDC1;
	u16 mask = 0x20;

	switch (mode) {
	case REGULATOR_MODE_STANDBY:
		return act8931_set_bits(act8931, act8931_BUCK_CONTR_REG(buck),
					mask, 0);
	case REGULATOR_MODE_NORMAL:
		return act8931_set_bits(act8931, act8931_BUCK_CONTR_REG(buck),
					mask, mask);
	default:
		pr_err("pmu_act8931 only powersave and pwm mode\n");
		return -EINVAL;
	}
}

static struct regulator_ops act8931_dcdc_ops = {
	.set_voltage = act8931_dcdc_set_voltage,
	.get_voltage = act8931_dcdc_get_voltage,
	.list_voltage = act8931_dcdc_list_voltage,
	.is_enabled = act8931_dcdc_is_enabled,
	.enable = act8931_dcdc_enable,
	.disable = act8931_dcdc_disable,
	.get_mode = act8931_dcdc_get_mode,
	.set_mode = act8931_dcdc_set_mode,
};

static struct regulator_desc regulators[] = {
	{
		.name = "ACT_DCDC1",
		.id = 0,
		.ops = &act8931_dcdc_ops,
		.n_voltages = ARRAY_SIZE(buck_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "ACT_DCDC2",
		.id = 1,
		.ops = &act8931_dcdc_ops,
		.n_voltages = ARRAY_SIZE(buck_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "ACT_DCDC3",
		.id = 2,
		.ops = &act8931_dcdc_ops,
		.n_voltages = ARRAY_SIZE(buck_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "ACT_LDO1",
		.id = 3,
		.ops = &act8931_ldo_ops,
		.n_voltages = ARRAY_SIZE(ldo_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "ACT_LDO2",
		.id = 4,
		.ops = &act8931_ldo_ops,
		.n_voltages = ARRAY_SIZE(ldo_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "ACT_LDO3",
		.id = 5,
		.ops = &act8931_ldo_ops,
		.n_voltages = ARRAY_SIZE(ldo_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "ACT_LDO4",
		.id = 6,
		.ops = &act8931_ldo_ops,
		.n_voltages = ARRAY_SIZE(ldo_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},

};

static int act8931_i2c_read(struct i2c_client *i2c, char reg, int count,
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
	msgs[0].scl_rate = 200*1000;

	msgs[1].buf = (u8 *)dest;
	msgs[1].addr = i2c->addr;
	msgs[1].flags = i2c->flags | I2C_M_RD;
	msgs[1].len = 1;
	msgs[1].scl_rate = 200*1000;
	ret = i2c_transfer(adap, msgs, 2);

	DBG("***run in %s %d msgs[1].buf = %d\n", __func__, __LINE__,
	    *(msgs[1].buf));

	return 0;
}

static int act8931_i2c_write(struct i2c_client *i2c, char reg, int count,
			     const u16 src)
{
	struct i2c_adapter *adap;
	struct i2c_msg msg;
	char tx_buf[2];
	int ret = -1;

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
	msg.scl_rate = 200*1000;

	ret = i2c_transfer(adap, &msg, 1);

	return 0;
}

static u8 act8931_reg_read(struct act8931 *act8931, u8 reg)
{
	u16 val = 0;

	mutex_lock(&act8931->io_lock);

	act8931_i2c_read(act8931->i2c, reg, 1, &val);

	DBG("reg read 0x%02x -> 0x%02x\n", (int)reg, (unsigned)val&0xff);

	mutex_unlock(&act8931->io_lock);

	return val & 0xff;
}

static int act8931_set_bits(struct act8931 *act8931, u8 reg, u16 mask, u16 val)
{
	u16 tmp;
	int ret;

	mutex_lock(&act8931->io_lock);

	ret = act8931_i2c_read(act8931->i2c, reg, 1, &tmp);
	DBG("1 reg read 0x%02x -> 0x%02x\n", (int)reg, (unsigned)tmp&0xff);
	tmp = (tmp & ~mask) | val;
	if (ret == 0) {
		ret = act8931_i2c_write(act8931->i2c, reg, 1, tmp);
		DBG("reg write 0x%02x -> 0x%02x\n", (int)reg,
		    (unsigned)val&0xff);
	}
	act8931_i2c_read(act8931->i2c, reg, 1, &tmp);
	DBG("2 reg read 0x%02x -> 0x%02x\n", (int)reg, (unsigned)tmp&0xff);
	mutex_unlock(&act8931->io_lock);

	return ret;
}

void act8931_device_shutdown(void)
{
	int ret;
	struct act8931 *act8931 = g_act8931;

	pr_info("%s\n", __func__);

	ret = act8931_reg_read(act8931, 0x01);
	ret = act8931_set_bits(act8931, 0x01, (0x1<<5) | (0x3<<0),
			       (0x1<<5) | (0x3<<0));
	if (ret < 0)
		pr_err("act8931 set 0x01 error!\n");

	mdelay(100);
	arm_pm_restart('h', "charge");
}

#ifdef CONFIG_OF
static struct of_device_id act8931_of_match[] = {
	{ .compatible = "act,act8931"},
	{ },
};
MODULE_DEVICE_TABLE(of, act8931_of_match);
#endif

#ifdef CONFIG_OF
static struct of_regulator_match act8931_reg_matches[] = {
	{ .name = "act_dcdc1" },
	{ .name = "act_dcdc2" },
	{ .name = "act_dcdc3" },
	{ .name = "act_ldo1"  },
	{ .name = "act_ldo2"  },
	{ .name = "act_ldo3"  },
	{ .name = "act_ldo4"  },
};

static struct act8931_board *act8931_parse_dt(struct act8931 *act8931)
{
	struct act8931_board *pdata;
	struct device_node *regs;
	struct device_node *node;
	int i, count;

	node = of_node_get(act8931->dev->of_node);
	if (!node) {
		pr_err("%s: could not find pmic node\n", __func__);
		return NULL;
	}

	regs = of_get_child_by_name(node, "regulators");
	if (!regs)
		return NULL;

	count = of_regulator_match(act8931->dev, regs, act8931_reg_matches,
				   ACT8931_NUM_REGULATORS);
	of_node_put(regs);

	if ((count < 0) || (count > ACT8931_NUM_REGULATORS))
		return NULL;

	pdata = devm_kzalloc(act8931->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return NULL;

	for (i = 0; i < count; i++) {
		pdata->act8931_init_data[i] = act8931_reg_matches[i].init_data;
		pdata->of_node[i] = act8931_reg_matches[i].of_node;
	}

	pdata->irq_gpio = of_get_named_gpio(node, "gpios", 0);
	if (!gpio_is_valid(pdata->irq_gpio)) {
		pr_err("%s: invalid gpio: %d\n", __func__, pdata->irq_gpio);
		return NULL;
	}

	pdata->pwr_hold_gpio = of_get_named_gpio(node, "gpios", 1);
	if (!gpio_is_valid(pdata->pwr_hold_gpio)) {
		pr_err("%s: invalid gpio: %d\n", __func__,
		       pdata->pwr_hold_gpio);
		return NULL;
	}

	pdata->pm_off = of_property_read_bool(node,
					"act8931,system-power-controller");

	return pdata;
}
#else
static struct act8931_board *act8931_parse_dt(struct act8931 *act8931)
{
	return NULL;
}
#endif

static irqreturn_t act8931_irq_thread(int irq, void *dev_id)
{
	struct act8931 *act8931 = (struct act8931 *)dev_id;
	int ret;
	u8 val;

	val = act8931_reg_read(act8931, 0x78);
	act8931_charge_det = (val & INDAT_MASK) ? 1 : 0;
	act8931_charge_ok = (val & CHGDAT_MASK) ? 1 : 0;
	DBG(charge_det ? "connect!" : "disconnect!");
	DBG(charge_ok ? "charge ok!\n" : "charging or discharge!\n");

	/* reset related regs according to spec */
	ret = act8931_set_bits(act8931, 0x78, INSTAT_MASK | CHGSTAT_MASK,
			       INSTAT_MASK | CHGSTAT_MASK);
	if (ret < 0)
		pr_err("act8931 set 0x78 error!\n");

	return IRQ_HANDLED;
}

static int act8931_i2c_probe(struct i2c_client *i2c,
			     const struct i2c_device_id *id)
{
	const struct of_device_id *match;
	struct act8931 *act8931;
	struct act8931_board *pdata;
	struct regulator_init_data *reg_data;
	struct regulator_config config = { };
	const char *rail_name = NULL;
	struct regulator_dev *rdev;
	u8 val;
	int i, ret;

	pr_info("%s,line=%d\n", __func__, __LINE__);

	if (i2c->dev.of_node) {
		match = of_match_device(act8931_of_match, &i2c->dev);
		if (!match) {
			pr_err("Failed to find matching dt id\n");
			return -EINVAL;
		}
	}

	act8931 = devm_kzalloc(&i2c->dev, sizeof(struct act8931), GFP_KERNEL);
	if (act8931 == NULL) {
		ret = -ENOMEM;
		goto err;
	}
	act8931->i2c = i2c;
	act8931->dev = &i2c->dev;
	g_act8931 = act8931;

	mutex_init(&act8931->io_lock);

	ret = act8931_reg_read(act8931, 0x22);
	if ((ret < 0) || (ret == 0xff)) {
		pr_err("The device is not act8931\n");
		return 0;
	}

	if (act8931->dev->of_node)
		pdata = act8931_parse_dt(act8931);

	ret = act8931_reg_read(act8931, 0x01);
	if (ret < 0)
		goto err;
	ret = act8931_set_bits(act8931, 0x01, (0x1<<5) | (0x1<<0), (0x1<<0));
	if (ret < 0) {
		pr_err("act8931 set 0x01 error!\n");
		goto err;
	}

	/* Initialize charge status */
	val = act8931_reg_read(act8931, 0x78);
	act8931_charge_det = (val & INDAT_MASK) ? 1 : 0;
	act8931_charge_ok = (val & CHGDAT_MASK) ? 1 : 0;
	DBG(charge_det ? "connect!" : "disconnect!");
	DBG(charge_ok ? "charge ok!\n" : "charging or discharge!\n");

	ret = act8931_set_bits(act8931, 0x78, INSTAT_MASK | CHGSTAT_MASK,
			       INSTAT_MASK | CHGSTAT_MASK);
	if (ret < 0) {
		pr_err("act8931 set 0x78 error!\n");
		goto err;
	}

	ret = act8931_set_bits(act8931, 0x79, INCON_MASK | CHGEOCIN_MASK
			       | INDIS_MASK | CHGEOCOUT_MASK, INCON_MASK
			       | CHGEOCIN_MASK | INDIS_MASK | CHGEOCOUT_MASK);
	if (ret < 0) {
		pr_err("act8931 set 0x79 error!\n");
		goto err;
	}

	if (pdata) {
		act8931->num_regulators = ACT8931_NUM_REGULATORS;
		act8931->rdev = devm_kcalloc(act8931->dev,
					     ACT8931_NUM_REGULATORS,
					     sizeof(struct regulator_dev *),
					     GFP_KERNEL);
		if (!act8931->rdev)
			return -ENOMEM;

		/* Instantiate the regulators */
		for (i = 0; i < ACT8931_NUM_REGULATORS; i++) {
			reg_data = pdata->act8931_init_data[i];
			if (!reg_data)
				continue;
			if (reg_data->constraints.name)
				rail_name = reg_data->constraints.name;
			else
				rail_name = regulators[i].name;
			reg_data->supply_regulator = rail_name;

			config.dev = act8931->dev;
			config.driver_data = act8931;
			if (act8931->dev->of_node)
				config.of_node = pdata->of_node[i];
			config.init_data = reg_data;

			rdev = regulator_register(&regulators[i], &config);
			if (IS_ERR(rdev)) {
				pr_err("failed to register %d regulator\n", i);
				continue;
			}
			act8931->rdev[i] = rdev;
		}
	}

	if (pdata->pm_off && !pm_power_off)
		pm_power_off = act8931_device_shutdown;

	act8931->pwr_hold_gpio = pdata->pwr_hold_gpio;
	if (act8931->pwr_hold_gpio) {
		ret = gpio_request(act8931->pwr_hold_gpio, "act8931 pmic_hold");
		if (ret < 0) {
			pr_err("Failed to request gpio %d with ret %d\n",
			       act8931->pwr_hold_gpio, ret);
			goto err;
		}
		gpio_direction_output(act8931->pwr_hold_gpio, 1);
		ret = gpio_get_value(act8931->pwr_hold_gpio);
		pr_info("%s: act8931_pmic_hold=%x\n", __func__, ret);
	}

	ret = gpio_request(pdata->irq_gpio, "act8931 irq");
	if (ret) {
		pr_err("act8931 irq_gpio request fail\n");
		gpio_free(pdata->irq_gpio);
		goto err;
	}
	gpio_direction_input(pdata->irq_gpio);

	act8931->irq = gpio_to_irq(pdata->irq_gpio);
	ret = request_threaded_irq(act8931->irq, NULL, act8931_irq_thread,
				   IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
				   i2c->dev.driver->name, act8931);
	if (ret < 0) {
		pr_err("request act8931 irq fail\n");
		goto err;
	}
	enable_irq_wake(act8931->irq);

	i2c_set_clientdata(i2c, act8931);

	return 0;

err:
	return ret;
}

static int act8931_i2c_remove(struct i2c_client *i2c)
{
	struct act8931 *act8931 = i2c_get_clientdata(i2c);
	int i;

	for (i = 0; i < act8931->num_regulators; i++)
		if (act8931->rdev[i])
			regulator_unregister(act8931->rdev[i]);
	i2c_set_clientdata(i2c, NULL);

	return 0;
}

static const struct i2c_device_id act8931_i2c_id[] = {
	{ "act8931", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, act8931_i2c_id);

static struct i2c_driver act8931_i2c_driver = {
	.driver = {
		.name = "act8931",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(act8931_of_match),
	},
	.probe    = act8931_i2c_probe,
	.remove   = act8931_i2c_remove,
	.id_table = act8931_i2c_id,
};

static int __init act8931_module_init(void)
{
	int ret;

	ret = i2c_add_driver(&act8931_i2c_driver);
	if (ret != 0)
		pr_err("Failed to register I2C driver: %d\n", ret);
	return ret;
}
subsys_initcall_sync(act8931_module_init);

static void __exit act8931_module_exit(void)
{
	i2c_del_driver(&act8931_i2c_driver);
}
module_exit(act8931_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("dkl <dkl@rock-chips.com>");
MODULE_DESCRIPTION("act8931 PMIC driver");
