// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2012 Texas Instruments
 *
 * Author: Milo(Woogyom) Kim <milo.kim@ti.com>
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/delay.h>
#include <linux/regulator/driver.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>

/* Registers : LP8720/8725 shared */
#define LP872X_GENERAL_CFG		0x00
#define LP872X_LDO1_VOUT		0x01
#define LP872X_LDO2_VOUT		0x02
#define LP872X_LDO3_VOUT		0x03
#define LP872X_LDO4_VOUT		0x04
#define LP872X_LDO5_VOUT		0x05

/* Registers : LP8720 */
#define LP8720_BUCK_VOUT1		0x06
#define LP8720_BUCK_VOUT2		0x07
#define LP8720_ENABLE			0x08

/* Registers : LP8725 */
#define LP8725_LILO1_VOUT		0x06
#define LP8725_LILO2_VOUT		0x07
#define LP8725_BUCK1_VOUT1		0x08
#define LP8725_BUCK1_VOUT2		0x09
#define LP8725_BUCK2_VOUT1		0x0A
#define LP8725_BUCK2_VOUT2		0x0B
#define LP8725_BUCK_CTRL		0x0C
#define LP8725_LDO_CTRL			0x0D

/* Mask/shift : LP8720/LP8725 shared */
#define LP872X_VOUT_M			0x1F
#define LP872X_START_DELAY_M		0xE0
#define LP872X_START_DELAY_S		5
#define LP872X_EN_LDO1_M		BIT(0)
#define LP872X_EN_LDO2_M		BIT(1)
#define LP872X_EN_LDO3_M		BIT(2)
#define LP872X_EN_LDO4_M		BIT(3)
#define LP872X_EN_LDO5_M		BIT(4)

/* Mask/shift : LP8720 */
#define LP8720_TIMESTEP_S		0		/* Addr 00h */
#define LP8720_TIMESTEP_M		BIT(0)
#define LP8720_EXT_DVS_M		BIT(2)
#define LP8720_BUCK_FPWM_S		5		/* Addr 07h */
#define LP8720_BUCK_FPWM_M		BIT(5)
#define LP8720_EN_BUCK_M		BIT(5)		/* Addr 08h */
#define LP8720_DVS_SEL_M		BIT(7)

/* Mask/shift : LP8725 */
#define LP8725_TIMESTEP_M		0xC0		/* Addr 00h */
#define LP8725_TIMESTEP_S		6
#define LP8725_BUCK1_EN_M		BIT(0)
#define LP8725_DVS1_M			BIT(2)
#define LP8725_DVS2_M			BIT(3)
#define LP8725_BUCK2_EN_M		BIT(4)
#define LP8725_BUCK_CL_M		0xC0		/* Addr 09h, 0Bh */
#define LP8725_BUCK_CL_S		6
#define LP8725_BUCK1_FPWM_S		1		/* Addr 0Ch */
#define LP8725_BUCK1_FPWM_M		BIT(1)
#define LP8725_BUCK2_FPWM_S		5
#define LP8725_BUCK2_FPWM_M		BIT(5)
#define LP8725_EN_LILO1_M		BIT(5)		/* Addr 0Dh */
#define LP8725_EN_LILO2_M		BIT(6)

/* PWM mode */
#define LP872X_FORCE_PWM		1
#define LP872X_AUTO_PWM			0

#define LP8720_NUM_REGULATORS		6
#define LP8725_NUM_REGULATORS		9
#define EXTERN_DVS_USED			0
#define MAX_DELAY			6

/* Default DVS Mode */
#define LP8720_DEFAULT_DVS		0
#define LP8725_DEFAULT_DVS		BIT(2)

/* dump registers in regmap-debugfs */
#define MAX_REGISTERS			0x0F

#define LP8720_ENABLE_DELAY		200
#define LP8725_ENABLE_DELAY		30000

enum lp872x_id {
	LP8720,
	LP8725,
};

enum lp872x_regulator_id {
	LP8720_ID_BASE,
	LP8720_ID_LDO1 = LP8720_ID_BASE,
	LP8720_ID_LDO2,
	LP8720_ID_LDO3,
	LP8720_ID_LDO4,
	LP8720_ID_LDO5,
	LP8720_ID_BUCK,

	LP8725_ID_BASE,
	LP8725_ID_LDO1 = LP8725_ID_BASE,
	LP8725_ID_LDO2,
	LP8725_ID_LDO3,
	LP8725_ID_LDO4,
	LP8725_ID_LDO5,
	LP8725_ID_LILO1,
	LP8725_ID_LILO2,
	LP8725_ID_BUCK1,
	LP8725_ID_BUCK2,

	LP872X_ID_MAX,
};

enum lp872x_dvs_sel {
	SEL_V1,
	SEL_V2,
};

struct lp872x_chip_info {
	const struct regulator_desc *rdesc;
	int num_regulators;
	enum lp872x_id chipid;
};

struct lp872x {
	struct regmap *regmap;
	struct device *dev;

	struct gpio_desc *enable_gpio;
	struct gpio_desc *dvs_gpio;

	const struct lp872x_chip_info *info;

	enum lp872x_dvs_sel vsel;
	enum gpiod_flags dvs_pin;
};

/* LP8720/LP8725 shared voltage table for LDOs */
static const unsigned int lp872x_ldo_vtbl[] = {
	1200000, 1250000, 1300000, 1350000, 1400000, 1450000, 1500000, 1550000,
	1600000, 1650000, 1700000, 1750000, 1800000, 1850000, 1900000, 2000000,
	2100000, 2200000, 2300000, 2400000, 2500000, 2600000, 2650000, 2700000,
	2750000, 2800000, 2850000, 2900000, 2950000, 3000000, 3100000, 3300000,
};

/* LP8720 LDO4 voltage table */
static const unsigned int lp8720_ldo4_vtbl[] = {
	 800000,  850000,  900000, 1000000, 1100000, 1200000, 1250000, 1300000,
	1350000, 1400000, 1450000, 1500000, 1550000, 1600000, 1650000, 1700000,
	1750000, 1800000, 1850000, 1900000, 2000000, 2100000, 2200000, 2300000,
	2400000, 2500000, 2600000, 2650000, 2700000, 2750000, 2800000, 2850000,
};

/* LP8725 LILO(Low Input Low Output) voltage table */
static const unsigned int lp8725_lilo_vtbl[] = {
	 800000,  850000,  900000,  950000, 1000000, 1050000, 1100000, 1150000,
	1200000, 1250000, 1300000, 1350000, 1400000, 1500000, 1600000, 1700000,
	1800000, 1900000, 2000000, 2100000, 2200000, 2300000, 2400000, 2500000,
	2600000, 2700000, 2800000, 2850000, 2900000, 3000000, 3100000, 3300000,
};

/* LP8720 BUCK voltage table */
#define EXT_R		0	/* external resistor divider */
static const unsigned int lp8720_buck_vtbl[] = {
	  EXT_R,  800000,  850000,  900000,  950000, 1000000, 1050000, 1100000,
	1150000, 1200000, 1250000, 1300000, 1350000, 1400000, 1450000, 1500000,
	1550000, 1600000, 1650000, 1700000, 1750000, 1800000, 1850000, 1900000,
	1950000, 2000000, 2050000, 2100000, 2150000, 2200000, 2250000, 2300000,
};

/* LP8725 BUCK voltage table */
static const unsigned int lp8725_buck_vtbl[] = {
	 800000,  850000,  900000,  950000, 1000000, 1050000, 1100000, 1150000,
	1200000, 1250000, 1300000, 1350000, 1400000, 1500000, 1600000, 1700000,
	1750000, 1800000, 1850000, 1900000, 2000000, 2100000, 2200000, 2300000,
	2400000, 2500000, 2600000, 2700000, 2800000, 2850000, 2900000, 3000000,
};

/* LP8725 BUCK current limit */
static const unsigned int lp8725_buck_uA[] = {
	460000, 780000, 1050000, 1370000,
};

static int lp872x_get_timestep_usec(struct lp872x *lp)
{
	enum lp872x_id chip = lp->info->chipid;
	u32 val, mask, shift;
	int *time_usec, size, ret;
	int lp8720_time_usec[] = { 25, 50 };
	int lp8725_time_usec[] = { 32, 64, 128, 256 };

	switch (chip) {
	case LP8720:
		mask = LP8720_TIMESTEP_M;
		shift = LP8720_TIMESTEP_S;
		time_usec = &lp8720_time_usec[0];
		size = ARRAY_SIZE(lp8720_time_usec);
		break;
	case LP8725:
		mask = LP8725_TIMESTEP_M;
		shift = LP8725_TIMESTEP_S;
		time_usec = &lp8725_time_usec[0];
		size = ARRAY_SIZE(lp8725_time_usec);
		break;
	default:
		return -EINVAL;
	}

	ret = regmap_read(lp->regmap, LP872X_GENERAL_CFG, &val);
	if (ret)
		return ret;

	val = (val & mask) >> shift;
	if (val >= size)
		return -EINVAL;

	return *(time_usec + val);
}

static int lp872x_regulator_enable_time(struct regulator_dev *rdev)
{
	struct lp872x *lp = rdev_get_drvdata(rdev);
	enum lp872x_regulator_id rid = rdev_get_id(rdev);
	int time_step_us = lp872x_get_timestep_usec(lp);
	int ret;
	u32 addr, val;

	if (time_step_us < 0)
		return time_step_us;

	switch (rid) {
	case LP8720_ID_LDO1 ... LP8720_ID_BUCK:
		addr = LP872X_LDO1_VOUT + rid;
		break;
	case LP8725_ID_LDO1 ... LP8725_ID_BUCK1:
		addr = LP872X_LDO1_VOUT + rid - LP8725_ID_BASE;
		break;
	case LP8725_ID_BUCK2:
		addr = LP8725_BUCK2_VOUT1;
		break;
	default:
		return -EINVAL;
	}

	ret = regmap_read(lp->regmap, addr, &val);
	if (ret)
		return ret;

	val = (val & LP872X_START_DELAY_M) >> LP872X_START_DELAY_S;

	return val > MAX_DELAY ? 0 : val * time_step_us;
}

static void lp872x_set_dvs(struct lp872x *lp, enum lp872x_dvs_sel dvs_sel,
			struct gpio_desc *gpio)
{
	enum gpiod_flags state;

	state = dvs_sel == SEL_V1 ? GPIOD_OUT_HIGH : GPIOD_OUT_LOW;
	gpiod_set_value(gpio, state);
	lp->dvs_pin = state;
}

static u8 lp872x_select_buck_vout_addr(struct lp872x *lp,
				enum lp872x_regulator_id buck)
{
	u32 val, addr;

	if (regmap_read(lp->regmap, LP872X_GENERAL_CFG, &val))
		return 0;

	switch (buck) {
	case LP8720_ID_BUCK:
		if (val & LP8720_EXT_DVS_M) {
			addr = (lp->dvs_pin == GPIOD_OUT_HIGH) ?
				LP8720_BUCK_VOUT1 : LP8720_BUCK_VOUT2;
		} else {
			if (regmap_read(lp->regmap, LP8720_ENABLE, &val))
				return 0;

			addr = val & LP8720_DVS_SEL_M ?
				LP8720_BUCK_VOUT1 : LP8720_BUCK_VOUT2;
		}
		break;
	case LP8725_ID_BUCK1:
		if (val & LP8725_DVS1_M)
			addr = LP8725_BUCK1_VOUT1;
		else
			addr = (lp->dvs_pin == GPIOD_OUT_HIGH) ?
				LP8725_BUCK1_VOUT1 : LP8725_BUCK1_VOUT2;
		break;
	case LP8725_ID_BUCK2:
		addr =  val & LP8725_DVS2_M ?
			LP8725_BUCK2_VOUT1 : LP8725_BUCK2_VOUT2;
		break;
	default:
		return 0;
	}

	return addr;
}

static bool lp872x_is_valid_buck_addr(u8 addr)
{
	switch (addr) {
	case LP8720_BUCK_VOUT1:
	case LP8720_BUCK_VOUT2:
	case LP8725_BUCK1_VOUT1:
	case LP8725_BUCK1_VOUT2:
	case LP8725_BUCK2_VOUT1:
	case LP8725_BUCK2_VOUT2:
		return true;
	default:
		return false;
	}
}

static int lp872x_buck_set_voltage_sel(struct regulator_dev *rdev,
					unsigned selector)
{
	struct lp872x *lp = rdev_get_drvdata(rdev);
	enum lp872x_regulator_id buck = rdev_get_id(rdev);
	u32 addr, mask = LP872X_VOUT_M;

	if (lp->dvs_gpio)
		lp872x_set_dvs(lp, lp->vsel, lp->dvs_gpio);

	addr = lp872x_select_buck_vout_addr(lp, buck);
	if (!lp872x_is_valid_buck_addr(addr))
		return -EINVAL;

	return regmap_update_bits(lp->regmap, addr, mask, selector);
}

static int lp872x_buck_get_voltage_sel(struct regulator_dev *rdev)
{
	struct lp872x *lp = rdev_get_drvdata(rdev);
	enum lp872x_regulator_id buck = rdev_get_id(rdev);
	u32 addr, val;
	int ret;

	addr = lp872x_select_buck_vout_addr(lp, buck);
	if (!lp872x_is_valid_buck_addr(addr))
		return -EINVAL;

	ret = regmap_read(lp->regmap, addr, &val);
	if (ret)
		return ret;

	return val & LP872X_VOUT_M;
}

static int lp872x_buck_set_mode(struct regulator_dev *rdev, unsigned int mode)
{
	struct lp872x *lp = rdev_get_drvdata(rdev);
	enum lp872x_regulator_id buck = rdev_get_id(rdev);
	u32 addr, mask, shift, val;

	switch (buck) {
	case LP8720_ID_BUCK:
		addr = LP8720_BUCK_VOUT2;
		mask = LP8720_BUCK_FPWM_M;
		shift = LP8720_BUCK_FPWM_S;
		break;
	case LP8725_ID_BUCK1:
		addr = LP8725_BUCK_CTRL;
		mask = LP8725_BUCK1_FPWM_M;
		shift = LP8725_BUCK1_FPWM_S;
		break;
	case LP8725_ID_BUCK2:
		addr = LP8725_BUCK_CTRL;
		mask = LP8725_BUCK2_FPWM_M;
		shift = LP8725_BUCK2_FPWM_S;
		break;
	default:
		return -EINVAL;
	}

	if (mode == REGULATOR_MODE_FAST)
		val = LP872X_FORCE_PWM << shift;
	else if (mode == REGULATOR_MODE_NORMAL)
		val = LP872X_AUTO_PWM << shift;
	else
		return -EINVAL;

	return regmap_update_bits(lp->regmap, addr, mask, val);
}

static unsigned int lp872x_buck_get_mode(struct regulator_dev *rdev)
{
	struct lp872x *lp = rdev_get_drvdata(rdev);
	enum lp872x_regulator_id buck = rdev_get_id(rdev);
	u32 addr, mask, val;
	int ret;

	switch (buck) {
	case LP8720_ID_BUCK:
		addr = LP8720_BUCK_VOUT2;
		mask = LP8720_BUCK_FPWM_M;
		break;
	case LP8725_ID_BUCK1:
		addr = LP8725_BUCK_CTRL;
		mask = LP8725_BUCK1_FPWM_M;
		break;
	case LP8725_ID_BUCK2:
		addr = LP8725_BUCK_CTRL;
		mask = LP8725_BUCK2_FPWM_M;
		break;
	default:
		return -EINVAL;
	}

	ret = regmap_read(lp->regmap, addr, &val);
	if (ret)
		return ret;

	return val & mask ? REGULATOR_MODE_FAST : REGULATOR_MODE_NORMAL;
}

static const struct regulator_ops lp872x_ldo_ops = {
	.list_voltage = regulator_list_voltage_table,
	.map_voltage = regulator_map_voltage_ascend,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.enable_time = lp872x_regulator_enable_time,
};

static const struct regulator_ops lp8720_buck_ops = {
	.list_voltage = regulator_list_voltage_table,
	.map_voltage = regulator_map_voltage_ascend,
	.set_voltage_sel = lp872x_buck_set_voltage_sel,
	.get_voltage_sel = lp872x_buck_get_voltage_sel,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.enable_time = lp872x_regulator_enable_time,
	.set_mode = lp872x_buck_set_mode,
	.get_mode = lp872x_buck_get_mode,
};

static const struct regulator_ops lp8725_buck_ops = {
	.list_voltage = regulator_list_voltage_table,
	.map_voltage = regulator_map_voltage_ascend,
	.set_voltage_sel = lp872x_buck_set_voltage_sel,
	.get_voltage_sel = lp872x_buck_get_voltage_sel,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.enable_time = lp872x_regulator_enable_time,
	.set_mode = lp872x_buck_set_mode,
	.get_mode = lp872x_buck_get_mode,
	.set_current_limit = regulator_set_current_limit_regmap,
	.get_current_limit = regulator_get_current_limit_regmap,
};

#define LP872X_LDO(_id, match, supply, _volt_table, _vsel_reg,		\
		   _enable_reg, _enable_mask) {				\
	.name = (match),						\
	.supply_name = (supply),					\
	.of_match = (match),						\
	.id = _id,							\
	.ops = &lp872x_ldo_ops,						\
	.n_voltages = ARRAY_SIZE(_volt_table),				\
	.volt_table = _volt_table,					\
	.type = REGULATOR_VOLTAGE,					\
	.owner = THIS_MODULE,						\
	.vsel_reg = _vsel_reg,						\
	.vsel_mask = LP872X_VOUT_M,					\
	.enable_reg = _enable_reg,					\
	.enable_mask = _enable_mask,					\
}

#define LP8720_LDO(ids, match, supply, _volt_table)			\
	LP872X_LDO(LP8720_ID_##ids, match, supply, _volt_table,		\
		   LP872X_##ids##_VOUT, LP8720_ENABLE, LP872X_EN_##ids##_M)

#define LP8725_LDO(ids, match, supply)					\
	LP872X_LDO(LP8725_ID_##ids, match, supply, lp872x_ldo_vtbl,	\
		   LP872X_##ids##_VOUT, LP8725_LDO_CTRL, LP872X_EN_##ids##_M)

#define LP8725_LILO(ids, match, supply)					\
	LP872X_LDO(LP8725_ID_##ids, match, supply, lp8725_lilo_vtbl,	\
		   LP8725_##ids##_VOUT, LP8725_LDO_CTRL, LP8725_EN_##ids##_M)

#define LP8725_BUCK(ids, match) {					\
	.name = (match),						\
	.supply_name = "vinb",						\
	.of_match = (match),						\
	.id = LP8725_ID_##ids,						\
	.ops = &lp8725_buck_ops,					\
	.n_voltages = ARRAY_SIZE(lp8725_buck_vtbl),			\
	.volt_table = lp8725_buck_vtbl,					\
	.type = REGULATOR_VOLTAGE,					\
	.owner = THIS_MODULE,						\
	.enable_reg = LP872X_GENERAL_CFG,				\
	.enable_mask = LP8725_##ids##_EN_M,				\
	.curr_table = lp8725_buck_uA,					\
	.n_current_limits = ARRAY_SIZE(lp8725_buck_uA),			\
	.csel_reg = LP8725_##ids##_VOUT2,				\
	.csel_mask = LP8725_BUCK_CL_M,					\
}

static const struct regulator_desc lp8720_regulator_desc[] = {
	LP8720_LDO(LDO1, "ldo1", "vbatt", lp872x_ldo_vtbl),
	LP8720_LDO(LDO2, "ldo2", "vin1", lp872x_ldo_vtbl),
	LP8720_LDO(LDO3, "ldo3", "vin1", lp872x_ldo_vtbl),
	LP8720_LDO(LDO4, "ldo4", "vin2", lp8720_ldo4_vtbl),
	LP8720_LDO(LDO5, "ldo5", "vin2", lp872x_ldo_vtbl),
	{
		.name = "buck",
		.supply_name = "vinb",
		.of_match = of_match_ptr("buck"),
		.id = LP8720_ID_BUCK,
		.ops = &lp8720_buck_ops,
		.n_voltages = ARRAY_SIZE(lp8720_buck_vtbl),
		.volt_table = lp8720_buck_vtbl,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
		.enable_reg = LP8720_ENABLE,
		.enable_mask = LP8720_EN_BUCK_M,
	},
};

static const struct regulator_desc lp8725_regulator_desc[] = {
	LP8725_LDO(LDO1, "ldo1", "vin1"),
	LP8725_LDO(LDO2, "ldo2", "vin2"),
	LP8725_LDO(LDO3, "ldo3", "vin2"),
	LP8725_LDO(LDO4, "ldo4", "vin3"),
	LP8725_LDO(LDO5, "ldo5", "vin3"),
	LP8725_LILO(LILO1, "lilo1", "vinlilo1"),
	LP8725_LILO(LILO2, "lilo2", "vinlilo2"),
	LP8725_BUCK(BUCK1, "buck1"),
	LP8725_BUCK(BUCK2, "buck2"),
};

static int lp872x_init_dvs(struct lp872x *lp)
{
	struct device *dev = lp->dev;
	u32 mask[] = { LP8720_EXT_DVS_M, LP8725_DVS1_M | LP8725_DVS2_M };
	u32 default_dvs_mode[] = { LP8720_DEFAULT_DVS, LP8725_DEFAULT_DVS };
	u8 state = 1;
	u8 vsel = SEL_V1;

	device_property_read_u8(dev, "ti,dvs-vsel", &vsel);
	lp->vsel = vsel;

	device_property_read_u8(dev, "ti,dvs-state", &state);
	lp->dvs_pin = state ? GPIOD_OUT_HIGH : GPIOD_OUT_LOW;

	lp->dvs_gpio = devm_gpiod_get_optional(dev, "dvs", lp->dvs_pin);
	if (IS_ERR(lp->dvs_gpio))
		return dev_err_probe(dev, PTR_ERR(lp->dvs_gpio),
				     "Failed to request DVS GPIO\n");

	if (!lp->dvs_gpio)
		return regmap_update_bits(lp->regmap, LP872X_GENERAL_CFG,
					  mask[lp->info->chipid],
					  default_dvs_mode[lp->info->chipid]);

	return 0;
}

static int lp872x_regulator_register(struct lp872x *lp)
{
	const struct regulator_desc *desc = lp->info->rdesc;
	int num_regulators = lp->info->num_regulators;
	struct regulator_config cfg = { };
	struct device *dev = lp->dev;
	struct regulator_dev *rdev;

	cfg.dev = dev;
	cfg.driver_data = lp;
	cfg.regmap = lp->regmap;

	for (int i = 0; i < num_regulators; i++) {
		rdev = devm_regulator_register(dev, &desc[i], &cfg);
		if (IS_ERR(rdev)) {
			dev_err(dev, "regulator register err");
			return PTR_ERR(rdev);
		}
	}

	return 0;
}

static const struct regmap_config lp872x_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = MAX_REGISTERS,
};

static int lp872x_probe(struct i2c_client *cl)
{
	struct device *dev = &cl->dev;
	const char *in_supply_name;
	struct lp872x *lp;
	int ret;

	lp = devm_kzalloc(dev, sizeof(*lp), GFP_KERNEL);
	if (!lp)
		return -ENOMEM;

	lp->info = i2c_get_match_data(cl);
	if (!lp->info)
		return -ENODEV;

	lp->regmap = devm_regmap_init_i2c(cl, &lp872x_regmap_config);
	if (IS_ERR(lp->regmap))
		return dev_err_probe(dev, PTR_ERR(lp->regmap),
				     "Failed to init regmap\n");

	/* LDO0 parent supplies chip itself */
	in_supply_name = lp->info->rdesc[0].supply_name;
	ret = devm_regulator_get_enable(dev, in_supply_name);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to enable supply regulator\n");

	lp->dev = dev;
	i2c_set_clientdata(cl, lp);

	/* Always set enable GPIO high. */
	lp->enable_gpio = devm_gpiod_get_optional(dev, "enable", GPIOD_OUT_HIGH);
	if (IS_ERR(lp->enable_gpio))
		return dev_err_probe(dev, PTR_ERR(lp->enable_gpio),
				     "Failed to request gpio\n");

	/* Each chip has a different enable delay. */
	if (lp->info->chipid == LP8720)
		usleep_range(LP8720_ENABLE_DELAY, 1.5 * LP8720_ENABLE_DELAY);
	else
		usleep_range(LP8725_ENABLE_DELAY, 1.5 * LP8725_ENABLE_DELAY);

	if (device_property_present(dev, "ti,general-config")) {
		u8 general_config;

		ret = device_property_read_u8(dev, "ti,general-config",
					      &general_config);
		if (ret)
			return dev_err_probe(dev, ret, "Failed to read general config\n");

		ret = regmap_write(lp->regmap, LP872X_GENERAL_CFG, general_config);
		if (ret)
			return ret;
	}

	ret = lp872x_init_dvs(lp);
	if (ret)
		return ret;

	return lp872x_regulator_register(lp);
}

static const struct lp872x_chip_info lp8720_data = {
	.rdesc = lp8720_regulator_desc,
	.num_regulators = LP8720_NUM_REGULATORS,
	.chipid = LP8720,
};

static const struct lp872x_chip_info lp8725_data = {
	.rdesc = lp8725_regulator_desc,
	.num_regulators = LP8725_NUM_REGULATORS,
	.chipid = LP8725,
};

static const struct of_device_id lp872x_dt_ids[] = {
	{ .compatible = "ti,lp8720", .data = &lp8720_data },
	{ .compatible = "ti,lp8725", .data = &lp8725_data },
	{ }
};
MODULE_DEVICE_TABLE(of, lp872x_dt_ids);

static const struct i2c_device_id lp872x_ids[] = {
	{ .name = "lp8720", .driver_data = (kernel_ulong_t)&lp8720_data },
	{ .name = "lp8725", .driver_data = (kernel_ulong_t)&lp8725_data },
	{ }
};
MODULE_DEVICE_TABLE(i2c, lp872x_ids);

static struct i2c_driver lp872x_driver = {
	.driver = {
		.name = "lp872x",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table = lp872x_dt_ids,
	},
	.probe = lp872x_probe,
	.id_table = lp872x_ids,
};

module_i2c_driver(lp872x_driver);

MODULE_DESCRIPTION("TI/National Semiconductor LP872x PMU Regulator Driver");
MODULE_AUTHOR("Milo Kim");
MODULE_LICENSE("GPL");
