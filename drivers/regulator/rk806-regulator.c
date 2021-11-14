// SPDX-License-Identifier: GPL-2.0
/*
 * Regulator driver for Rockchip RK806
 *
 * Copyright (c) 2021 Rockchip Electronics Co., Ltd.
 *
 * Author: Xu Shengfei <xsf@rock-chips.com>
 */

#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/mfd/rk806.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include "internal.h"

static int dbg_enable;

module_param_named(dbg_level, dbg_enable, int, 0644);

#define REG_DBG(args...) \
	do { \
		if (dbg_enable) { \
			pr_info(args); \
		} \
	} while (0)

#define RK806_BUCK_MIN0		500000
#define RK806_BUCK_MAX0		1500000
#define RK806_BUCK_MIN1		1500000
#define RK806_BUCK_MAX1		3400000
#define RK806_BUCK_STP0		6250
#define RK806_BUCK_STP1		25000

#define RK806_NLDO_MIN		500000
#define RK806_NLDO_MAX		3400000
#define RK806_NLDO_STP0		1250
#define RK806_NLDO_SEL		((RK806_NLDO_MAX - RK806_NLDO_MIN) / RK806_NLDO_STP0 + 1)

#define ENABLE_MASK(id)		(BIT(id) | BIT(4 + (id)))
#define DISABLE_VAL(id)		(BIT(4 + (id)))
#define PWM_MODE_MSK		BIT(0)
#define FPWM_MODE		BIT(0)
#define AUTO_PWM_MODE		0

#define RK806_DCDC_SLP_REG_OFFSET	0x0A
#define RK806_NLDO_SLP_REG_OFFSET	0x05
#define RK806_PLDO_SLP_REG_OFFSET	0x06

#define RK806_BUCK_SEL_CNT		0xff
#define RK806_LDO_SEL_CNT		0xff

#define RK806_RAMP_RATE_4LSB_PER_1CLK	0x00/* LDO 100mV/uS buck 50mV/us */
#define RK806_RAMP_RATE_2LSB_PER_1CLK	0x01/* LDO 50mV/uS buck 25mV/us */
#define RK806_RAMP_RATE_1LSB_PER_1CLK	0x02/* LDO 25mV/uS buck 12.5mV/us */
#define RK806_RAMP_RATE_1LSB_PER_2CLK	0x03/* LDO 12.5mV/uS buck 6.25mV/us */

#define RK806_RAMP_RATE_1LSB_PER_4CLK	0x04/* LDO 6.28/2mV/uS buck 3.125mV/us */
#define RK806_RAMP_RATE_1LSB_PER_8CLK	0x05/* LDO 3.12mV/uS buck 1.56mV/us */
#define RK806_RAMP_RATE_1LSB_PER_13CLK	0x06/* LDO 1.9mV/uS buck 961mV/us */
#define RK806_RAMP_RATE_1LSB_PER_32CLK	0x07/* LDO 0.78mV/uS buck 0.39mV/us */

static int gpio_dvs_id[] = {
	BUCK1_SLP_CTR_SEL,
	BUCK2_SLP_CTR_SEL,
	BUCK3_SLP_CTR_SEL,
	BUCK4_SLP_CTR_SEL,
	BUCK5_SLP_CTR_SEL,
	BUCK6_SLP_CTR_SEL,
	BUCK7_SLP_CTR_SEL,
	BUCK8_SLP_CTR_SEL,
	BUCK9_SLP_CTR_SEL,
	BUCK10_SLP_CTR_SEL,
	NLDO1_SLP_CTR_SEL,
	NLDO2_SLP_CTR_SEL,
	NLDO3_SLP_CTR_SEL,
	NLDO4_SLP_CTR_SEL,
	NLDO5_SLP_CTR_SEL,
	PLDO1_SLP_CTR_SEL,
	PLDO2_SLP_CTR_SEL,
	PLDO3_SLP_CTR_SEL,
	PLDO4_SLP_CTR_SEL,
	PLDO5_SLP_CTR_SEL,
	PLDO6_SLP_CTR_SEL,
};

static int start_dvs_id[] = {
	BUCK1_DVS_CTR_SEL,
	BUCK2_DVS_CTR_SEL,
	BUCK3_DVS_CTR_SEL,
	BUCK4_DVS_CTR_SEL,
	BUCK5_DVS_CTR_SEL,
	BUCK6_DVS_CTR_SEL,
	BUCK7_DVS_CTR_SEL,
	BUCK8_DVS_CTR_SEL,
	BUCK9_DVS_CTR_SEL,
	BUCK10_DVS_CTR_SEL,
	NLDO1_DVS_CTR_SEL,
	NLDO2_DVS_CTR_SEL,
	NLDO3_DVS_CTR_SEL,
	NLDO4_DVS_CTR_SEL,
	NLDO5_DVS_CTR_SEL,
	PLDO1_DVS_CTR_SEL,
	PLDO2_DVS_CTR_SEL,
	PLDO3_DVS_CTR_SEL,
	PLDO4_DVS_CTR_SEL,
	PLDO5_DVS_CTR_SEL,
	PLDO6_DVS_CTR_SEL,
};

static const int rk806_buck_rate_config_field[10][2] = {
	{ BUCK1_RATE, BUCK1_RATE2 },
	{ BUCK2_RATE, BUCK2_RATE2 },
	{ BUCK3_RATE, BUCK3_RATE2 },
	{ BUCK4_RATE, BUCK4_RATE2 },
	{ BUCK5_RATE, BUCK5_RATE2 },
	{ BUCK6_RATE, BUCK6_RATE2 },
	{ BUCK7_RATE, BUCK7_RATE2 },
	{ BUCK8_RATE, BUCK8_RATE2 },
	{ BUCK9_RATE, BUCK9_RATE2 },
	{ BUCK10_RATE, BUCK10_RATE2 },
};

struct rk806_dvs_field {
	int en_reg;
	int en_bit;
	int sleep_en;
	int on_vsel;
	int sleep_vsel;
	int sleep_ctrl_sel;
};

struct rk806_dvs_status {
	int en_reg_val;
	int en_bit_val;
	int sleep_en_val;
	int on_vsel_val;
	int sleep_vsel_val;
	int sleep_ctrl_sel_val;
	int dvs_gpio_level[3];
};

struct rk806_regulator_data {
	struct device_node *dvs_dn[RK806_DVS_END][RK806_ID_END];
	struct rk806_dvs_field dvs_field[RK806_ID_END];
	struct rk806_dvs_status dvs_mode[RK806_ID_END];
	struct rk806_dvs_status sleep_mode[RK806_ID_END];

	int dvs_ctrl_mode_init[RK806_ID_END];
	int dvs_ctrl_mode[RK806_ID_END];
	int dvs_gpios[3];

	int dvs_flag[RK806_DVS_END];
	int dvs_used[RK806_DVS_END];
	int dvs_count[RK806_DVS_END];
	int dvs_init;
	int support_dvs;
	struct rk806 *rk806;
};

#define INIT_DVS_FIELD(_en_reg, _en_bit, _sleep_en, _on_vsel,	\
			_sleep_vsel, _sleep_ctrl_sel)	\
{	\
	.en_reg = _en_reg,	\
	.en_bit = _en_bit,	\
	.sleep_en = _sleep_en,	\
	.on_vsel = _on_vsel,	\
	.sleep_vsel = _sleep_vsel,	\
	.sleep_ctrl_sel = _sleep_ctrl_sel,	\
}

static const struct rk806_dvs_field rk806_dvs_field[] = {
	INIT_DVS_FIELD(POWER_EN0, BIT(0), BUCK1_SLP_EN,
		       BUCK1_ON_VSEL, BUCK1_SLP_VSEL, BUCK1_SLP_CTR_SEL),
	INIT_DVS_FIELD(POWER_EN0, BIT(1), BUCK2_SLP_EN,
		       BUCK2_ON_VSEL, BUCK2_SLP_VSEL, BUCK2_SLP_CTR_SEL),
	INIT_DVS_FIELD(POWER_EN0, BIT(2), BUCK3_SLP_EN,
		       BUCK3_ON_VSEL, BUCK3_SLP_VSEL, BUCK3_SLP_CTR_SEL),
	INIT_DVS_FIELD(POWER_EN0, BIT(3), BUCK4_SLP_EN,
		       BUCK4_ON_VSEL, BUCK4_SLP_VSEL, BUCK4_SLP_CTR_SEL),

	INIT_DVS_FIELD(POWER_EN1, BIT(0), BUCK5_SLP_EN,
		       BUCK5_ON_VSEL, BUCK5_SLP_VSEL, BUCK5_SLP_CTR_SEL),
	INIT_DVS_FIELD(POWER_EN1, BIT(1), BUCK6_SLP_EN,
		       BUCK6_ON_VSEL, BUCK6_SLP_VSEL, BUCK6_SLP_CTR_SEL),
	INIT_DVS_FIELD(POWER_EN1, BIT(2), BUCK7_SLP_EN,
		       BUCK7_ON_VSEL, BUCK7_SLP_VSEL, BUCK7_SLP_CTR_SEL),
	INIT_DVS_FIELD(POWER_EN1, BIT(3), BUCK8_SLP_EN,
		       BUCK8_ON_VSEL, BUCK8_SLP_VSEL, BUCK8_SLP_CTR_SEL),

	INIT_DVS_FIELD(POWER_EN2, BIT(0), BUCK9_SLP_EN,
		       BUCK9_ON_VSEL, BUCK9_SLP_VSEL, BUCK9_SLP_CTR_SEL),
	INIT_DVS_FIELD(POWER_EN2, BIT(1), BUCK10_SLP_EN,
		       BUCK10_ON_VSEL, BUCK10_SLP_VSEL, BUCK10_SLP_CTR_SEL),

	INIT_DVS_FIELD(POWER_EN3, BIT(0), NLDO1_SLP_EN,
		       NLDO1_ON_VSEL, NLDO1_SLP_VSEL, NLDO1_SLP_CTR_SEL),
	INIT_DVS_FIELD(POWER_EN3, BIT(1), NLDO2_SLP_EN,
		       NLDO2_ON_VSEL, NLDO2_SLP_VSEL, NLDO2_SLP_CTR_SEL),
	INIT_DVS_FIELD(POWER_EN3, BIT(2), NLDO3_SLP_EN,
		       NLDO3_ON_VSEL, NLDO3_SLP_VSEL, NLDO3_SLP_CTR_SEL),
	INIT_DVS_FIELD(POWER_EN3, BIT(3), NLDO4_SLP_EN,
		       NLDO4_ON_VSEL, NLDO4_SLP_VSEL, NLDO4_SLP_CTR_SEL),

	INIT_DVS_FIELD(POWER_EN5, BIT(2), NLDO5_SLP_EN,
		       NLDO5_ON_VSEL, NLDO5_SLP_VSEL, NLDO5_SLP_CTR_SEL),

	INIT_DVS_FIELD(POWER_EN4, BIT(0), PLDO1_SLP_EN,
		       PLDO1_ON_VSEL, PLDO1_SLP_VSEL, PLDO1_SLP_CTR_SEL),
	INIT_DVS_FIELD(POWER_EN4, BIT(1), PLDO2_SLP_EN,
		       PLDO2_ON_VSEL, PLDO2_SLP_VSEL, PLDO2_SLP_CTR_SEL),
	INIT_DVS_FIELD(POWER_EN4, BIT(2), PLDO3_SLP_EN,
		       PLDO3_ON_VSEL, PLDO3_SLP_VSEL, PLDO3_SLP_CTR_SEL),
	INIT_DVS_FIELD(POWER_EN4, BIT(3), PLDO4_SLP_EN,
		       PLDO4_ON_VSEL, PLDO4_SLP_VSEL, PLDO4_SLP_CTR_SEL),

	INIT_DVS_FIELD(POWER_EN5, BIT(0), PLDO5_SLP_EN,
		       PLDO5_ON_VSEL, PLDO5_SLP_VSEL, PLDO5_SLP_CTR_SEL),
	INIT_DVS_FIELD(POWER_EN5, BIT(1), PLDO6_SLP_EN,
		       PLDO6_ON_VSEL, PLDO6_SLP_VSEL, PLDO6_SLP_CTR_SEL),
};

static const struct linear_range rk806_buck_voltage_ranges[] = {
	REGULATOR_LINEAR_RANGE(500000, 0, 160, 6250), /* 500mV ~ 1500mV */
	REGULATOR_LINEAR_RANGE(1500000, 161, 237, 25000), /* 1500mV ~ 3400mV */
	REGULATOR_LINEAR_RANGE(3400000, 238, 255, 0),
};

static const struct linear_range rk806_ldo_voltage_ranges[] = {
	REGULATOR_LINEAR_RANGE(500000, 0, 232, 12500), /* 500mV ~ 3400mV */
	REGULATOR_LINEAR_RANGE(3400000, 233, 255, 0), /* 500mV ~ 3400mV */
};

static int get_count(int value)
{
	int count = 0;

	while (value != 0) {
		if (value % 2 == 1)
			count++;
		value >>= 1;
	}

	return count;
}

static int get_dvs_mode(struct regulator_dev *rdev)
{
	struct rk806_regulator_data *pdata = rdev_get_drvdata(rdev);
	struct rk806 *rk806 = pdata->rk806;
	int rid = rdev_get_id(rdev);
	int i, j;

	if (!pdata->support_dvs)
		return RK806_DVS_NOT_SUPPORT;

	if (pdata->dvs_ctrl_mode_init[rid])
		return pdata->dvs_ctrl_mode[rid];

	for (i = 0; i < RK806_DVS_END; i++) {
		for (j = 0; j < RK806_ID_END; j++) {
			if ((pdata->dvs_dn[i][j] == NULL) ||
			    (strcmp(pdata->dvs_dn[i][j]->name, rdev->desc->name)))
				continue;

			pdata->dvs_ctrl_mode[rid] = i;
			pdata->dvs_ctrl_mode_init[rid] = 1;
			pdata->dvs_flag[i] |= BIT(rid);

			/* init dvs pin function */
			if (pdata->dvs_ctrl_mode[rid] == RK806_DVS_BY_CTR_PIN1)
				rk806_field_write(rk806, SLP1_FUN, RK806_PIN_FUN_DVS);
			else if (pdata->dvs_ctrl_mode[rid] == RK806_DVS_BY_CTR_PIN2)
				rk806_field_write(rk806, SLP2_FUN, RK806_PIN_FUN_DVS);
			else if (pdata->dvs_ctrl_mode[rid] == RK806_DVS_BY_CTR_PIN3)
				rk806_field_write(rk806, SLP3_FUN, RK806_PIN_FUN_DVS);

			/* init dvs function, dvs-pin or start bit */
			if ((pdata->dvs_ctrl_mode[rid] >= RK806_DVS_BY_CTR_PIN1) &&
			    (pdata->dvs_ctrl_mode[rid] <= RK806_DVS_BY_CTR_PIN3))
				rk806_field_write(rk806, gpio_dvs_id[rid], pdata->dvs_ctrl_mode[rid]);

			if ((pdata->dvs_ctrl_mode[rid] >= RK806_DVS_BY_CTR_START1) &&
			    (pdata->dvs_ctrl_mode[rid] <= RK806_DVS_BY_CTR_START3))
				rk806_field_write(rk806,
						  start_dvs_id[rid],
						  pdata->dvs_ctrl_mode[rid] - RK806_DVS_BY_CTR_PIN3);

			return pdata->dvs_ctrl_mode[rid];
		}
	}

	return pdata->dvs_ctrl_mode[rid];
}

static int get_gpio_id(struct regulator_dev *rdev)
{
	return get_dvs_mode(rdev) - 1;
}

static int rk806_get_reg_offset(int id)
{
	int reg_offset = 0;

	if (id >= RK806_ID_DCDC1 && id <= RK806_ID_DCDC10)
		reg_offset = RK806_DCDC_SLP_REG_OFFSET;
	else if ((id >= RK806_ID_NLDO1 && id <= RK806_ID_NLDO4) ||
		 (id == RK806_ID_NLDO5))
		reg_offset = RK806_NLDO_SLP_REG_OFFSET;
	else if (id >= RK806_ID_PLDO1 && id <= RK806_ID_PLDO6)
		reg_offset = RK806_PLDO_SLP_REG_OFFSET;

	return reg_offset;
}

static int rk806_get_read_vsel_register(struct regulator_dev *rdev)
{
	struct rk806_regulator_data *pdata = rdev_get_drvdata(rdev);
	int level, vsel_reg, pid;
	int rid = rdev_get_id(rdev);
	int mode;

	mode = get_dvs_mode(rdev);
	if (mode == RK806_DVS_NOT_SUPPORT)
		return rdev->desc->vsel_reg;

	if ((mode < RK806_DVS_BY_CTR_PIN1) || (mode > RK806_DVS_BY_CTR_PIN3)) {
		vsel_reg = rdev->desc->vsel_reg;
	} else {
		pid = get_gpio_id(rdev);
		level = gpio_get_value(pdata->dvs_gpios[pid]);

		if (level == 0)
			vsel_reg = rdev->desc->vsel_reg;
		else
			vsel_reg = rdev->desc->vsel_reg + rk806_get_reg_offset(rid);
	}

	return vsel_reg;
}

static int rk806_get_write_vsel_register(struct regulator_dev *rdev)
{
	struct rk806_regulator_data *pdata = rdev_get_drvdata(rdev);
	int level, vsel_reg, pid;
	int rid = rdev_get_id(rdev);
	int mode;

	mode = get_dvs_mode(rdev);
	if (mode == RK806_DVS_NOT_SUPPORT)
		return rdev->desc->vsel_reg;

	if ((mode < RK806_DVS_BY_CTR_PIN1) || (mode > RK806_DVS_BY_CTR_PIN3)) {
		vsel_reg = rdev->desc->vsel_reg;
	} else {
		pid = get_gpio_id(rdev);
		level = gpio_get_value(pdata->dvs_gpios[pid]);

		if (level == 1)
			vsel_reg = rdev->desc->vsel_reg;
		else
			vsel_reg = rdev->desc->vsel_reg + rk806_get_reg_offset(rid);
	}

	return vsel_reg;
}

static void rk806_do_gpio_dvs(struct regulator_dev *rdev)
{
	struct rk806_regulator_data *pdata = rdev_get_drvdata(rdev);
	char dvs_ctrl_name[7][32] = { "dvs_default",
				      "dvs_pin1_ctrl",
				      "dvs_pin2_ctrl",
				      "dvs_pin3_ctrl",
				      "start_dvs1_ctrl",
				      "start_dvs2_ctrl",
				      "start_dvs3_ctrl" };
	int rid = rdev_get_id(rdev);
	int gpio_level, pid;
	int mode, count;

	if (pdata->dvs_init == 0)
		return;

	mode = get_dvs_mode(rdev);
	if ((mode >= RK806_DVS_BY_CTR_PIN1) &&
	    (mode <= RK806_DVS_BY_CTR_PIN3)) {
		pid = get_gpio_id(rdev);
		pdata->dvs_used[mode] |= BIT(rid);
		count = get_count(pdata->dvs_used[mode]);

		if ((pdata->dvs_used[mode] != pdata->dvs_flag[mode]) ||
		    (count != pdata->dvs_count[mode]))
			return;

		pdata->dvs_used[mode] = 0;
		gpio_level = gpio_get_value(pdata->dvs_gpios[pid]);

		if (gpio_level == 0)
			gpio_set_value(pdata->dvs_gpios[pid], 1);
		else
			gpio_set_value(pdata->dvs_gpios[pid], 0);

		REG_DBG("pin: name: %s, %s\n", dvs_ctrl_name[mode], rdev->desc->name);
	}
}

static void rk806_do_soft_dvs(struct regulator_dev *rdev)
{
	struct rk806_regulator_data *pdata = rdev_get_drvdata(rdev);
	char dvs_ctrl_name[7][32] = { "dvs_default",
				      "dvs_pin1_ctrl",
				      "dvs_pin2_ctrl",
				      "dvs_pin3_ctrl",
				      "start_dvs1_ctrl",
				      "start_dvs2_ctrl",
				      "start_dvs3_ctrl" };
	struct rk806 *rk806 = pdata->rk806;
	int rid = rdev_get_id(rdev);
	int soft_mode, count;

	if (pdata->dvs_init == 0)
		return;

	soft_mode = get_dvs_mode(rdev);

	if ((soft_mode >= RK806_DVS_BY_CTR_START1) &&
	    (soft_mode <= RK806_DVS_BY_CTR_START3)) {
		pdata->dvs_used[soft_mode] |= BIT(rid);
		count = get_count(pdata->dvs_used[soft_mode]);

		if ((pdata->dvs_used[soft_mode] != pdata->dvs_flag[soft_mode]) ||
		    (count != pdata->dvs_count[soft_mode]))
			return;

		pdata->dvs_used[soft_mode] = 0;

		if (soft_mode == RK806_DVS_BY_CTR_START1)
			rk806_field_write(rk806, DVS_START1, 0x01);
		else if (soft_mode == RK806_DVS_BY_CTR_START2)
			rk806_field_write(rk806, DVS_START2, 0x01);
		else if (soft_mode == RK806_DVS_BY_CTR_START3)
			rk806_field_write(rk806, DVS_START3, 0x01);

		REG_DBG("soft:%s, %s\n", dvs_ctrl_name[soft_mode], rdev->desc->name);
	}
}

static void rk806_regulator_sync_voltage(struct regulator_dev *rdev)
{
	int mode;

	mode = get_dvs_mode(rdev);
	if (mode == RK806_DVS_NOT_SUPPORT)
		return;

	if ((mode >= RK806_DVS_BY_CTR_PIN1) && (mode <= RK806_DVS_BY_CTR_PIN3))
		rk806_do_gpio_dvs(rdev);
	else
		rk806_do_soft_dvs(rdev);
}

static unsigned int rk806_regulator_of_map_mode(unsigned int mode)
{
	switch (mode) {
	case 1:
		return REGULATOR_MODE_FAST;
	case 2:
		return REGULATOR_MODE_NORMAL;
	default:
		return -EINVAL;
	}
}

static int rk806_set_suspend_enable_ctrl(struct regulator_dev *rdev,
					 unsigned int en)
{
	struct rk806_regulator_data *pdata = rdev_get_drvdata(rdev);
	struct rk806 *rk806 = pdata->rk806;
	int rid = rdev_get_id(rdev);
	unsigned int val;

	if (en)
		val = 1;
	else
		val = 0;

	if ((get_dvs_mode(rdev) < RK806_DVS_BY_CTR_PIN1) ||
	    (get_dvs_mode(rdev) > RK806_DVS_BY_CTR_PIN3))
		return rk806_field_write(rk806, pdata->dvs_field[rid].sleep_en, val);

	pdata->sleep_mode[rid].sleep_en_val = val;

	return 0;
}

static int rk806_set_suspend_enable(struct regulator_dev *rdev)
{
	return rk806_set_suspend_enable_ctrl(rdev, 1);
}

static int rk806_set_suspend_disable(struct regulator_dev *rdev)
{
	return rk806_set_suspend_enable_ctrl(rdev, 0);
}

static int rk806_set_mode(struct regulator_dev *rdev, unsigned int mode)
{
	int rid = rdev_get_id(rdev);
	int ctr_bit, reg;

	if (rid > RK806_ID_DCDC10)
		return 0;

	reg = RK806_POWER_FPWM_EN0 + rid / 8;
	ctr_bit = rid % 8;

	switch (mode) {
	case REGULATOR_MODE_FAST:
		return regmap_update_bits(rdev->regmap, reg,
					  PWM_MODE_MSK << ctr_bit,
					  FPWM_MODE << ctr_bit);
	case REGULATOR_MODE_NORMAL:
		return regmap_update_bits(rdev->regmap, reg,
					  PWM_MODE_MSK << ctr_bit,
					  AUTO_PWM_MODE << ctr_bit);
	default:
		dev_err(&rdev->dev, "do not support this mode\n");
		return -EINVAL;
	}

	return 0;
}

static unsigned int rk806_get_mode(struct regulator_dev *rdev)
{
	int rid = rdev_get_id(rdev);
	int ctr_bit, reg;
	unsigned int val;
	int err;

	if (rid > RK806_ID_DCDC10)
		return 0;

	reg = RK806_POWER_FPWM_EN0 + rid / 8;
	ctr_bit = rid % 8;

	err = regmap_read(rdev->regmap, reg, &val);
	if (err)
		return err;

	if ((val >> ctr_bit) & FPWM_MODE)
		return REGULATOR_MODE_FAST;
	else
		return REGULATOR_MODE_NORMAL;
}

static int rk806_regulator_sleep2dvs_mode(struct regulator_dev *rdev)
{
	struct rk806_regulator_data *pdata = rdev_get_drvdata(rdev);
	struct rk806 *rk806 = pdata->rk806;
	int rid = rdev_get_id(rdev);
	int gpio_id = get_gpio_id(rdev);
	int mode = get_dvs_mode(rdev);
	int gpio_level, j;

	/* set slp_fun NULL*/
	if (pdata->dvs_ctrl_mode[rid] == RK806_DVS_BY_CTR_PIN1)
		rk806_field_write(rk806, SLP1_FUN, RK806_DVS_BY_CTR_PIN1);
	else if (pdata->dvs_ctrl_mode[rid] == RK806_DVS_BY_CTR_PIN2)
		rk806_field_write(rk806, SLP2_FUN, RK806_DVS_BY_CTR_PIN2);
	else if (pdata->dvs_ctrl_mode[rid] == RK806_DVS_BY_CTR_PIN3)
		rk806_field_write(rk806, SLP3_FUN, RK806_DVS_BY_CTR_PIN3);

	/* 3.check the used count 1*/
	pdata->dvs_used[mode] |= BIT(rid);
	if (pdata->dvs_used[mode] != pdata->dvs_flag[mode])
		return 0;

	pdata->dvs_used[mode] = 0;
	/* 5.clear the SLP_CTRL_SEL */
	for (j = 0; j < RK806_ID_END; j++)
		if (pdata->dvs_ctrl_mode[j] == mode)
			rk806_field_write(rk806,
					  pdata->dvs_field[j].sleep_ctrl_sel,
					  pdata->dvs_ctrl_mode[j]);

	gpio_level = pdata->dvs_mode[rid].dvs_gpio_level[gpio_id];

	if (gpio_level == 1) {
		gpio_set_value(pdata->dvs_gpios[gpio_id], 1);
		rk806_field_write(rk806,
				  pdata->dvs_field[rid].on_vsel,
				  pdata->dvs_mode[rid].on_vsel_val);
		rk806_field_write(rk806,
				  pdata->dvs_field[rid].en_reg,
				  pdata->dvs_mode[rid].en_reg_val | (pdata->dvs_field[rid].en_bit << 4));
	}
	return 0;
}

static int rk806_regulator_resume(struct regulator_dev *rdev)
{
	struct rk806_regulator_data *pdata = rdev_get_drvdata(rdev);
	struct rk806 *rk806 = pdata->rk806;
	int rid = rdev_get_id(rdev);
	int j;

	if (rid == RK806_ID_DCDC1) {
		for (j = 0; j < RK806_ID_END; j++) {
			rk806_field_write(rk806,
					  pdata->dvs_field[j].sleep_ctrl_sel,
					  0x00);
			rk806_field_write(rk806,
					  pdata->dvs_field[j].sleep_vsel,
					  pdata->dvs_mode[j].sleep_vsel_val);
			rk806_field_write(rk806,
					  pdata->dvs_field[j].sleep_en,
					  pdata->dvs_mode[j].sleep_en_val);
		}
	}

	rk806_field_write(rk806, SLP1_POL, 0x00);

	if ((get_dvs_mode(rdev) >= RK806_DVS_BY_CTR_PIN1) &&
	    (get_dvs_mode(rdev) <= RK806_DVS_BY_CTR_PIN3))
		rk806_regulator_sleep2dvs_mode(rdev);

	return 0;
}

static int rk806_regulator_dvs2sleep_mode(struct regulator_dev *rdev)
{
	struct rk806_regulator_data *pdata = rdev_get_drvdata(rdev);
	struct rk806 *rk806 = pdata->rk806;
	int rid = rdev_get_id(rdev);
	int gpio_id = get_gpio_id(rdev);
	int mode = get_dvs_mode(rdev);
	int gpio_level, j;

	/* 1. save  ON_VSEL/ON_EN, SLP_VSEL/SLP_EN, dvs_pin level, SLP_CTRL_SEL */
	gpio_level = gpio_get_value(pdata->dvs_gpios[gpio_id]);
	pdata->dvs_mode[rid].dvs_gpio_level[gpio_id] = gpio_level;

	if (gpio_level == 1) {
		/* 2.if dvs_pin high level, the output voltage from SLP_VSEL/SLP_EN */
		rk806_field_write(rk806,
				  pdata->dvs_field[rid].on_vsel,
				  pdata->dvs_mode[rid].sleep_vsel_val);
		rk806_field_write(rk806,
				  pdata->dvs_field[rid].en_reg,
				  pdata->dvs_mode[rid].sleep_en_val | (pdata->dvs_field[rid].en_bit << 4));
	}

	/* 3.check the used count 1 */
	pdata->dvs_used[mode] |= BIT(rid);
	if (pdata->dvs_used[mode] != pdata->dvs_flag[mode])
		return 0;

	pdata->dvs_used[mode] = 0;
	/* 4.switch to ON_VSEL/ON_EN */
	gpio_set_value(pdata->dvs_gpios[gpio_id], 0);

	/*first set SLEEP_CTRL_SEL, then set SLP_FUN */
	/* 5.clear the SLP_CTRL_SEL */
	for (j = 0; j < RK806_ID_END; j++)
		if (pdata->dvs_ctrl_mode[j] == mode)
			rk806_field_write(rk806,
					  pdata->dvs_field[j].sleep_ctrl_sel,
					  0x00);

	/* set slp_fun NULL */
	if (pdata->dvs_ctrl_mode[rid] == RK806_DVS_BY_CTR_PIN1)
		rk806_field_write(rk806, SLP1_FUN, RK806_PIN_FUN_NULL);
	else if (pdata->dvs_ctrl_mode[rid] == RK806_DVS_BY_CTR_PIN2)
		rk806_field_write(rk806, SLP2_FUN, RK806_PIN_FUN_NULL);
	else if (pdata->dvs_ctrl_mode[rid] == RK806_DVS_BY_CTR_PIN3)
		rk806_field_write(rk806, SLP3_FUN, RK806_PIN_FUN_NULL);

	if (pdata->dvs_ctrl_mode[rid] == RK806_DVS_BY_CTR_PIN1) {
		/* 6.the slp pin avctive high */
		rk806_field_write(rk806, SLP1_POL, 0x01);
		/* 7.switch to sleep function */
		rk806_field_write(rk806, SLP1_FUN, RK806_PIN_FUN_SLP);
	}

	/* 8.set the SLP_VSEL/SLP_EN */
	for (j = 0; j < RK806_ID_END; j++) {
		if (pdata->dvs_ctrl_mode[j] == mode) {
			if (pdata->sleep_mode[j].sleep_en_val != 0)
				rk806_field_write(rk806,
						  pdata->dvs_field[j].sleep_vsel,
						  pdata->sleep_mode[j].sleep_vsel_val);
			rk806_field_write(rk806,
					  pdata->dvs_field[j].sleep_en,
					  pdata->sleep_mode[j].sleep_en_val);
		}
	}

	if (rk806->pins && rk806->pins->p && rk806->pins->sleep)
		pinctrl_select_state(rk806->pins->p, rk806->pins->sleep);

	return 0;
}

static int rk806_set_suspend_voltage_range(struct regulator_dev *rdev, int uv)
{
	struct rk806_regulator_data *pdata = rdev_get_drvdata(rdev);
	int sel = regulator_map_voltage_linear_range(rdev, uv, uv);
	struct rk806 *rk806 = pdata->rk806;
	int rid = rdev_get_id(rdev);
	int reg_offset;
	unsigned int reg, j;

	if (sel < 0)
		return -EINVAL;

	pdata->dvs_mode[rid].en_reg_val = rk806_field_read(rk806,
							   pdata->dvs_field[rid].en_reg);
	pdata->dvs_mode[rid].on_vsel_val = rk806_field_read(rk806,
							    pdata->dvs_field[rid].on_vsel);
	pdata->dvs_mode[rid].sleep_vsel_val = rk806_field_read(rk806,
							       pdata->dvs_field[rid].sleep_vsel);
	pdata->dvs_mode[rid].sleep_en_val = rk806_field_read(rk806,
							     pdata->dvs_field[rid].sleep_en);
	pdata->dvs_mode[rid].sleep_ctrl_sel_val = rk806_field_read(rk806,
								   pdata->dvs_field[rid].sleep_ctrl_sel);

	if ((get_dvs_mode(rdev) < RK806_DVS_BY_CTR_PIN1) ||
	    (get_dvs_mode(rdev) > RK806_DVS_BY_CTR_PIN3)) {
		reg_offset = rk806_get_reg_offset(rid);
		reg = rdev->desc->vsel_reg + reg_offset;

		if (rid == RK806_ID_PLDO6) {
			for (j = 0; j < RK806_ID_END; j++)
				rk806_field_write(rk806,
						  pdata->dvs_field[j].sleep_ctrl_sel,
						  RK806_PIN_FUN_SLP);
			rk806_field_write(rk806, SLP1_POL, 0x01);
		}
		return regmap_update_bits(rdev->regmap, reg,
					  rdev->desc->vsel_mask,
					  sel);
	} else {
		/* power on set state */
		if (!pdata->dvs_init)
			return 0;

		pdata->sleep_mode[rid].sleep_vsel_val = sel;
		rk806_regulator_dvs2sleep_mode(rdev);

		if (rid == RK806_ID_PLDO6) {
			for (j = 0; j < RK806_ID_END; j++)
				rk806_field_write(rk806,
						  gpio_dvs_id[j],
						  RK806_PIN_FUN_SLP);
			rk806_field_write(rk806, SLP1_POL, 0x01);
		}
	}

	return 0;
}

static int rk806_get_voltage_sel_regmap(struct regulator_dev *rdev)
{
	unsigned int val;
	int vsel_reg;
	int ret;

	vsel_reg = rk806_get_read_vsel_register(rdev);

	ret = regmap_read(rdev->regmap, vsel_reg, &val);
	if (ret != 0)
		return ret;

	val &= rdev->desc->vsel_mask;
	val >>= ffs(rdev->desc->vsel_mask) - 1;

	return val;
}

static int rk806_set_voltage(struct regulator_dev *rdev,
			     int req_min_uV, int req_max_uV,
			     unsigned int *selector)
{
	int vsel_reg;
	int mode;
	int ret;
	int sel;

	ret = regulator_map_voltage_linear_range(rdev, req_min_uV, req_max_uV);
	if (ret >= 0) {
		*selector = ret;
		sel = ret;
	} else {
		return -EINVAL;
	}

	vsel_reg = rk806_get_write_vsel_register(rdev);

	sel <<= ffs(rdev->desc->vsel_mask) - 1;

	ret = regmap_update_bits(rdev->regmap, vsel_reg,
				 rdev->desc->vsel_mask, sel);

	mode = get_dvs_mode(rdev);

	if (mode == RK806_DVS_NOT_SUPPORT)
		return ret;

	if ((mode >= RK806_DVS_BY_CTR_PIN1) &&
	    (mode <= RK806_DVS_BY_CTR_PIN3))
		rk806_do_gpio_dvs(rdev);
	else
		rk806_do_soft_dvs(rdev);

	return ret;
}

static int rk806_regulator_is_enabled_regmap(struct regulator_dev *rdev)
{
	struct rk806_regulator_data *pdata = rdev_get_drvdata(rdev);
	struct rk806 *rk806 = pdata->rk806;
	int rid = rdev_get_id(rdev);
	int gpio_level, pid;
	unsigned int val;
	int mode;

	mode = get_dvs_mode(rdev);

	if ((mode >= RK806_DVS_BY_CTR_PIN1) &&
	    (mode <= RK806_DVS_BY_CTR_PIN3)) {
		pid = get_gpio_id(rdev);
		gpio_level = gpio_get_value(pdata->dvs_gpios[pid]);

		if (gpio_level == 1) {
			return rk806_field_read(rk806, pdata->dvs_field[rid].sleep_en);
		} else {
			val = rk806_field_read(rk806,  pdata->dvs_field[rid].en_reg);
			return val & rdev->desc->enable_val;
		}
	}

	val = rk806_field_read(rk806,  pdata->dvs_field[rid].en_reg);

	return val & rdev->desc->enable_val;
}

static int rk806_regulator_enable_regmap(struct regulator_dev *rdev)
{
	struct rk806_regulator_data *pdata = rdev_get_drvdata(rdev);
	struct rk806 *rk806 = pdata->rk806;
	int rid = rdev_get_id(rdev);
	int gpio_level, pid;
	int mode;

	mode = get_dvs_mode(rdev);

	if ((mode >= RK806_DVS_BY_CTR_PIN1) &&
	    (mode <= RK806_DVS_BY_CTR_PIN3)) {
		pid = get_gpio_id(rdev);
		gpio_level = gpio_get_value(pdata->dvs_gpios[pid]);

		if (gpio_level == 1)
			return rk806_field_write(rk806,
						 pdata->dvs_field[rid].sleep_en,
						 0x01);
		else
			return rk806_field_write(rk806,
						 pdata->dvs_field[rid].en_reg,
						 rdev->desc->enable_val);
	}

	return rk806_field_write(rk806,
				 pdata->dvs_field[rid].en_reg,
				 rdev->desc->enable_val);
}

static int rk806_regulator_disable_regmap(struct regulator_dev *rdev)
{
	struct rk806_regulator_data *pdata = rdev_get_drvdata(rdev);
	struct rk806 *rk806 = pdata->rk806;
	int rid = rdev_get_id(rdev);
	int gpio_level, pid;
	int mode;

	mode = get_dvs_mode(rdev);

	if ((mode >= RK806_DVS_BY_CTR_PIN1) &&
	    (mode <= RK806_DVS_BY_CTR_PIN3)) {
		pid = get_gpio_id(rdev);
		gpio_level = gpio_get_value(pdata->dvs_gpios[pid]);

		if (gpio_level == 1)
			return rk806_field_write(rk806,
						 pdata->dvs_field[rid].sleep_en,
						 0x00);
		else
			return rk806_field_write(rk806,
						 pdata->dvs_field[rid].en_reg,
						 rdev->desc->disable_val);
	}

	return rk806_field_write(rk806,
				 pdata->dvs_field[rid].en_reg,
				 rdev->desc->disable_val);
}

static int rk806_set_ramp_delay(struct regulator_dev *rdev, int ramp_delay)
{
	unsigned int ramp_value = RK806_RAMP_RATE_2LSB_PER_1CLK;
	struct rk806_regulator_data *pdata = rdev_get_drvdata(rdev);
	struct rk806 *rk806 = pdata->rk806;
	int rid = rdev_get_id(rdev);

	if (rid <= RK806_ID_DCDC10) {
		switch (ramp_delay) {
		case 1 ... 390:
			ramp_value = RK806_RAMP_RATE_1LSB_PER_32CLK;
			break;
		case 391 ... 961:
			ramp_value = RK806_RAMP_RATE_1LSB_PER_13CLK;
			break;
		case 962 ... 1560:
			ramp_value = RK806_RAMP_RATE_1LSB_PER_8CLK;
			break;
		case 1561 ... 3125:
			ramp_value = RK806_RAMP_RATE_1LSB_PER_4CLK;
			break;
		case 3126 ... 6250:
			ramp_value = RK806_RAMP_RATE_1LSB_PER_2CLK;
			break;
		case 6251 ... 12500:
			ramp_value = RK806_RAMP_RATE_1LSB_PER_1CLK;
			break;
		case 12501 ... 25000:
			ramp_value = RK806_RAMP_RATE_2LSB_PER_1CLK;
			break;
		case 25001 ... 50000: /* 50mV/us */
			ramp_value = RK806_RAMP_RATE_4LSB_PER_1CLK;
			break;
		default:
			pr_warn("%s ramp_delay: %d not supported, setting 10000\n",
				rdev->desc->name, ramp_delay);
		}

		rk806_field_write(rk806,
				  rk806_buck_rate_config_field[rid][0],
				  ramp_value & 0x03);
		return rk806_field_write(rk806,
					 rk806_buck_rate_config_field[rid][1],
					 (ramp_value & 0x4) >> 2);
	} else {
		switch (ramp_delay) {
		case 1 ... 780:
			ramp_value = RK806_RAMP_RATE_1LSB_PER_32CLK;
			break;
		case 781 ... 1900:
			ramp_value = RK806_RAMP_RATE_1LSB_PER_13CLK;
			break;
		case 1901 ... 3120:
			ramp_value = RK806_RAMP_RATE_1LSB_PER_8CLK;
			break;
		case 3121 ... 6280:
			ramp_value = RK806_RAMP_RATE_1LSB_PER_4CLK;
			break;
		case 6281 ... 12500:
			ramp_value = RK806_RAMP_RATE_1LSB_PER_2CLK;
			break;
		case 12501 ... 25000:
			ramp_value = RK806_RAMP_RATE_1LSB_PER_1CLK;
			break;
		case 25001 ... 50000:
			ramp_value = RK806_RAMP_RATE_2LSB_PER_1CLK;
			break;
		case 50001 ... 100000:
			ramp_value = RK806_RAMP_RATE_4LSB_PER_1CLK;
			break;
		default:
			pr_warn("%s ramp_delay: %d not supported, setting 10000\n",
				rdev->desc->name, ramp_delay);
		}
		return rk806_field_write(rk806, LDO_RATE, ramp_value);
	}
}

static const struct regulator_ops rk806_ops_dcdc = {
	.list_voltage		= regulator_list_voltage_linear_range,
	.map_voltage		= regulator_map_voltage_linear_range,

	.get_voltage_sel	= rk806_get_voltage_sel_regmap,
	.set_voltage		= rk806_set_voltage,
	.set_voltage_time_sel	= regulator_set_voltage_time_sel,
	.set_mode		= rk806_set_mode,
	.get_mode		= rk806_get_mode,

	.enable			= rk806_regulator_enable_regmap,
	.disable		= rk806_regulator_disable_regmap,
	.is_enabled		= rk806_regulator_is_enabled_regmap,

	.set_suspend_mode	= rk806_set_mode,
	.set_ramp_delay		= rk806_set_ramp_delay,

	.set_suspend_voltage	= rk806_set_suspend_voltage_range,
	.resume			= rk806_regulator_resume,
	.set_suspend_enable	= rk806_set_suspend_enable,
	.set_suspend_disable	= rk806_set_suspend_disable,
};

static const struct regulator_ops rk806_ops_ldo = {
	.list_voltage		= regulator_list_voltage_linear_range,
	.map_voltage		= regulator_map_voltage_linear_range,

	.get_voltage_sel	= rk806_get_voltage_sel_regmap,
	.set_voltage		= rk806_set_voltage,
	.set_voltage_time_sel	= regulator_set_voltage_time_sel,

	.enable			= rk806_regulator_enable_regmap,
	.disable		= rk806_regulator_disable_regmap,
	.is_enabled		= rk806_regulator_is_enabled_regmap,

	.set_suspend_mode	= rk806_set_mode,
	.set_ramp_delay		= rk806_set_ramp_delay,

	.set_suspend_voltage	= rk806_set_suspend_voltage_range,
	.resume			= rk806_regulator_resume,
	.set_suspend_enable	= rk806_set_suspend_enable,
	.set_suspend_disable	= rk806_set_suspend_disable,
};

#define RK806_REGULATOR(_name, _supply_name, _id, _ops,\
			_n_voltages, _vr, _er, _lr, ctrl_bit)\
[_id] = {\
		.name = _name,\
		.supply_name = _supply_name,\
		.of_match = of_match_ptr(_name),\
		.regulators_node = of_match_ptr("regulators"),\
		.id = _id,\
		.ops = &_ops,\
		.type = REGULATOR_VOLTAGE,\
		.n_voltages = _n_voltages,\
		.linear_ranges = _lr,\
		.n_linear_ranges = ARRAY_SIZE(_lr),\
		.vsel_reg = _vr,\
		.vsel_mask = 0xff,\
		.enable_reg = _er,\
		.enable_mask = ENABLE_MASK(ctrl_bit),\
		.enable_val = ENABLE_MASK(ctrl_bit),\
		.disable_val = DISABLE_VAL(ctrl_bit),\
		.of_map_mode = rk806_regulator_of_map_mode,\
		.owner = THIS_MODULE,\
	}

static const struct regulator_desc rk806_regulators[] = {
	RK806_REGULATOR("DCDC_REG1", "vcc1", RK806_ID_DCDC1, rk806_ops_dcdc,
			RK806_BUCK_SEL_CNT, RK806_BUCK1_ON_VSEL,
			RK806_POWER_EN0, rk806_buck_voltage_ranges, 0),
	RK806_REGULATOR("DCDC_REG2", "vcc2", RK806_ID_DCDC2, rk806_ops_dcdc,
			RK806_BUCK_SEL_CNT, RK806_BUCK2_ON_VSEL,
			RK806_POWER_EN0, rk806_buck_voltage_ranges, 1),
	RK806_REGULATOR("DCDC_REG3", "vcc3", RK806_ID_DCDC3, rk806_ops_dcdc,
			RK806_BUCK_SEL_CNT, RK806_BUCK3_ON_VSEL,
			RK806_POWER_EN0, rk806_buck_voltage_ranges, 2),
	RK806_REGULATOR("DCDC_REG4", "vcc4", RK806_ID_DCDC4, rk806_ops_dcdc,
			RK806_BUCK_SEL_CNT, RK806_BUCK4_ON_VSEL,
			RK806_POWER_EN0, rk806_buck_voltage_ranges, 3),

	RK806_REGULATOR("DCDC_REG5", "vcc5", RK806_ID_DCDC5, rk806_ops_dcdc,
			RK806_BUCK_SEL_CNT, RK806_BUCK5_ON_VSEL,
			RK806_POWER_EN1, rk806_buck_voltage_ranges, 0),
	RK806_REGULATOR("DCDC_REG6", "vcc6", RK806_ID_DCDC6, rk806_ops_dcdc,
			RK806_BUCK_SEL_CNT, RK806_BUCK6_ON_VSEL,
			RK806_POWER_EN1, rk806_buck_voltage_ranges, 1),
	RK806_REGULATOR("DCDC_REG7", "vcc7", RK806_ID_DCDC7, rk806_ops_dcdc,
			RK806_BUCK_SEL_CNT, RK806_BUCK7_ON_VSEL,
			RK806_POWER_EN1, rk806_buck_voltage_ranges, 2),
	RK806_REGULATOR("DCDC_REG8", "vcc8", RK806_ID_DCDC8, rk806_ops_dcdc,
			RK806_BUCK_SEL_CNT, RK806_BUCK8_ON_VSEL,
			RK806_POWER_EN1, rk806_buck_voltage_ranges, 3),

	RK806_REGULATOR("DCDC_REG9", "vcc9", RK806_ID_DCDC9, rk806_ops_dcdc,
			RK806_BUCK_SEL_CNT, RK806_BUCK9_ON_VSEL,
			RK806_POWER_EN2, rk806_buck_voltage_ranges, 0),
	RK806_REGULATOR("DCDC_REG10", "vcc10", RK806_ID_DCDC10, rk806_ops_dcdc,
			RK806_BUCK_SEL_CNT, RK806_BUCK10_ON_VSEL,
			RK806_POWER_EN2, rk806_buck_voltage_ranges, 1),

	RK806_REGULATOR("NLDO_REG1", "vcc13", RK806_ID_NLDO1, rk806_ops_ldo,
			RK806_LDO_SEL_CNT, RK806_NLDO1_ON_VSEL,
			RK806_POWER_EN3, rk806_ldo_voltage_ranges, 0),
	RK806_REGULATOR("NLDO_REG2", "vcc13", RK806_ID_NLDO2, rk806_ops_ldo,
			RK806_LDO_SEL_CNT, RK806_NLDO2_ON_VSEL,
			RK806_POWER_EN3, rk806_ldo_voltage_ranges, 1),
	RK806_REGULATOR("NLDO_REG3", "vcc13", RK806_ID_NLDO3, rk806_ops_ldo,
			RK806_LDO_SEL_CNT, RK806_NLDO3_ON_VSEL,
			RK806_POWER_EN3, rk806_ldo_voltage_ranges, 2),
	RK806_REGULATOR("NLDO_REG4", "vcc14", RK806_ID_NLDO4, rk806_ops_ldo,
			RK806_LDO_SEL_CNT, RK806_NLDO4_ON_VSEL,
			RK806_POWER_EN3, rk806_ldo_voltage_ranges, 3),

	RK806_REGULATOR("NLDO_REG5", "vcc14", RK806_ID_NLDO5, rk806_ops_ldo,
			RK806_LDO_SEL_CNT, RK806_NLDO5_ON_VSEL,
			RK806_POWER_EN5, rk806_ldo_voltage_ranges, 2),

	RK806_REGULATOR("PLDO_REG1", "vcc11", RK806_ID_PLDO1, rk806_ops_ldo,
			RK806_LDO_SEL_CNT, RK806_PLDO1_ON_VSEL,
			RK806_POWER_EN4, rk806_ldo_voltage_ranges, 0),
	RK806_REGULATOR("PLDO_REG2", "vcc11", RK806_ID_PLDO2, rk806_ops_ldo,
			RK806_LDO_SEL_CNT, RK806_PLDO2_ON_VSEL,
			RK806_POWER_EN4, rk806_ldo_voltage_ranges, 1),
	RK806_REGULATOR("PLDO_REG3", "vcc11", RK806_ID_PLDO3, rk806_ops_ldo,
			RK806_LDO_SEL_CNT, RK806_PLDO3_ON_VSEL,
			RK806_POWER_EN4, rk806_ldo_voltage_ranges, 2),
	RK806_REGULATOR("PLDO_REG4", "vcc12", RK806_ID_PLDO4, rk806_ops_ldo,
			RK806_LDO_SEL_CNT, RK806_PLDO4_ON_VSEL,
			RK806_POWER_EN4, rk806_ldo_voltage_ranges, 3),

	RK806_REGULATOR("PLDO_REG5", "vcc12", RK806_ID_PLDO5, rk806_ops_ldo,
			RK806_LDO_SEL_CNT, RK806_PLDO5_ON_VSEL,
			RK806_POWER_EN5, rk806_ldo_voltage_ranges, 0),
	RK806_REGULATOR("PLDO_REG6", "vcca", RK806_ID_PLDO6, rk806_ops_ldo,
			RK806_LDO_SEL_CNT, RK806_PLDO6_ON_VSEL,
			RK806_POWER_EN5, rk806_ldo_voltage_ranges, 1),
};

static void rk806_regulator_dt_parse_pdata(struct rk806 *rk806,
					   struct regmap *map,
					   struct rk806_regulator_data *pdata)

{
	char dvs_ctrl_name[7][32] = { "dvs_default",
				      "dvs_pin1_ctrl",
				      "dvs_pin2_ctrl",
				      "dvs_pin3_ctrl",
				      "start_dvs1_ctrl",
				      "start_dvs2_ctrl",
				      "start_dvs3_ctrl" };
	char dvs_pin_name[3][30] = { "rk806,pmic-dvs-gpio1",
				     "rk806,pmic-dvs-gpio2",
				     "rk806,pmic-dvs-gpio3" };
	struct device_node *np = rk806->dev->of_node;
	struct device_node *dn;
	int i, j, gpio;

	pdata->support_dvs = 0;

	for (j = 1; j < RK806_DVS_END; j++) {
		if (device_property_present(rk806->dev, dvs_ctrl_name[j])) {
			REG_DBG("%s:\n", dvs_ctrl_name[j]);
			for (i = 0;
			     (dn = of_parse_phandle(np, dvs_ctrl_name[j], i));
			     i++) {
				REG_DBG("\t%s\n", dn->name);
				pdata->support_dvs = 1;
				pdata->dvs_dn[j][i] = dn;
				pdata->dvs_count[j]++;

				of_node_put(dn);
				if (i >= RK806_ID_END)
					break;
			}
		}
	}

	if (!pdata->support_dvs)
		return;

	for (i = 0; i < 3; i++) {
		gpio = of_get_named_gpio(np, dvs_pin_name[i], i);
		if (!gpio_is_valid(gpio)) {
			dev_err(rk806->dev, "invalid gpio[%d]: %d\n", i, gpio);
			break;
		}

		pdata->dvs_gpios[i] = gpio;
	}

	for (i = 0; i < 3; i++) {
		if (pdata->dvs_gpios[i] >= 0) {
			gpio_request(pdata->dvs_gpios[i], dvs_pin_name[i]);
			gpio_direction_output(pdata->dvs_gpios[i], 0);
		}
	}
}

static void rk806_init_dvs_mode(struct regulator_dev *rdev,
				struct rk806_regulator_data *pdata)
{
	int rid;
	int i, j;

	if (!pdata->support_dvs)
		return;

	for (i = 0; i < RK806_DVS_END; i++) {
		for (j = 0; j < RK806_ID_END; j++) {
			pdata->dvs_ctrl_mode_init[j] = 1;
			if (!pdata->dvs_dn[i][j])
				break;
			if (strcmp(pdata->dvs_dn[i][j]->name, rdev->desc->name))
				continue;

			rid = rdev_get_id(rdev);
			pdata->dvs_ctrl_mode[rid] = i;
			pdata->dvs_flag[i] |= BIT(rid);
			pdata->dvs_init = 1;
			pdata->dvs_field[j] = rk806_dvs_field[j];
			rk806_regulator_sync_voltage(rdev);
			return;
		}
	}
}

static int rk806_regulator_probe(struct platform_device *pdev)
{
	struct rk806 *rk806 = dev_get_drvdata(pdev->dev.parent);
	struct rk806_regulator_data *pdata;
	struct regulator_config config = { };
	struct regulator_dev *rdev;
	int i;

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	rk806_regulator_dt_parse_pdata(rk806, rk806->regmap, pdata);

	pdata->rk806 = rk806;
	platform_set_drvdata(pdev, pdata);

	config.dev = &pdev->dev;
	config.driver_data = pdata;
	config.dev->of_node = rk806->dev->of_node;
	config.regmap = rk806->regmap;

	for (i = 0; i < ARRAY_SIZE(rk806_regulators); i++) {
		rdev = devm_regulator_register(&pdev->dev,
					       &rk806_regulators[i],
					       &config);
		if (IS_ERR(rdev)) {
			dev_err(rk806->dev, "failed to register %s regulator\n",
				pdev->name);
			return PTR_ERR(rdev);
		}

		rk806_init_dvs_mode(rdev, pdata);
	}

	return 0;
}

static const struct platform_device_id rk806_regulator_id_table[] = {
	{ "rk806-regulator", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, rk806_regulator_id_table);

static struct platform_driver rk806_regulator_driver = {
	.driver = {
		.name = "rk806-regulator",
	},
	.probe = rk806_regulator_probe,
	.id_table = rk806_regulator_id_table,
};
module_platform_driver(rk806_regulator_driver);

MODULE_AUTHOR("Xu Shengfei <xsf@rock-chips.com>");
MODULE_DESCRIPTION("rk806 voltage regulator driver");
MODULE_LICENSE("GPL v2");
