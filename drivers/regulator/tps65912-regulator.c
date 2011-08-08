/*
 * tps65912.c  --  TI tps65912
 *
 * Copyright 2011 Texas Instruments Inc.
 *
 * Author: Margarita Olaya Cabrera <magi@slimlogic.co.uk>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under  the terms of the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the License, or (at your
 *  option) any later version.
 *
 * This driver is based on wm8350 implementation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/mfd/tps65912.h>

/* DCDC's */
#define TPS65912_REG_DCDC1	0
#define TPS65912_REG_DCDC2	1
#define TPS65912_REG_DCDC3	2
#define TPS65912_REG_DCDC4	3

/* LDOs */
#define TPS65912_REG_LDO1	4
#define TPS65912_REG_LDO2	5
#define TPS65912_REG_LDO3	6
#define TPS65912_REG_LDO4	7
#define TPS65912_REG_LDO5	8
#define TPS65912_REG_LDO6	9
#define TPS65912_REG_LDO7	10
#define TPS65912_REG_LDO8	11
#define TPS65912_REG_LDO9	12
#define TPS65912_REG_LDO10	13

#define TPS65912_MAX_REG_ID	TPS65912_REG_LDO_10

/* Number of step-down converters available */
#define TPS65912_NUM_DCDC	4

/* Number of LDO voltage regulators  available */
#define TPS65912_NUM_LDO	10

/* Number of total regulators available */
#define TPS65912_NUM_REGULATOR		(TPS65912_NUM_DCDC + TPS65912_NUM_LDO)

#define TPS65912_REG_ENABLED	0x80
#define OP_SELREG_MASK		0x40
#define OP_SELREG_SHIFT		6

struct tps_info {
	const char *name;
};

static struct tps_info tps65912_regs[] = {
	{
		.name = "DCDC1",
	},
	{
		.name = "DCDC2",
	},
	{
		.name = "DCDC3",
	},
	{
		.name = "DCDC4",
	},
	{
		.name = "LDO1",
	},
	{
		.name = "LDO2",
	},
	{
		.name = "LDO3",
	},
	{
		.name = "LDO4",
	},
	{
		.name = "LDO5",
	},
	{
		.name = "LDO6",
	},
	{
		.name = "LDO7",
	},
	{
		.name = "LDO8",
	},
	{
		.name = "LDO9",
	},
	{
		.name = "LDO10",
	},
};

struct tps65912_reg {
	struct regulator_desc desc[TPS65912_NUM_REGULATOR];
	struct tps65912 *mfd;
	struct regulator_dev *rdev[TPS65912_NUM_REGULATOR];
	struct tps_info *info[TPS65912_NUM_REGULATOR];
	/* for read/write access */
	struct mutex io_lock;
	int mode;
	int (*get_ctrl_reg)(int);
	int dcdc1_range;
	int dcdc2_range;
	int dcdc3_range;
	int dcdc4_range;
	int pwm_mode_reg;
	int eco_reg;
};

static int tps65912_get_range(struct tps65912_reg *pmic, int id)
{
	struct tps65912 *mfd = pmic->mfd;

	if (id > TPS65912_REG_DCDC4)
		return 0;

	switch (id) {
	case TPS65912_REG_DCDC1:
		pmic->dcdc1_range = tps65912_reg_read(mfd,
							TPS65912_DCDC1_LIMIT);
		if (pmic->dcdc1_range < 0)
			return pmic->dcdc1_range;
		pmic->dcdc1_range = (pmic->dcdc1_range &
			DCDC_LIMIT_RANGE_MASK) >> DCDC_LIMIT_RANGE_SHIFT;
		return pmic->dcdc1_range;
	case TPS65912_REG_DCDC2:
		pmic->dcdc2_range = tps65912_reg_read(mfd,
							TPS65912_DCDC2_LIMIT);
		if (pmic->dcdc2_range < 0)
			return pmic->dcdc2_range;
		pmic->dcdc2_range = (pmic->dcdc2_range &
			DCDC_LIMIT_RANGE_MASK) >> DCDC_LIMIT_RANGE_SHIFT;
		return pmic->dcdc2_range;
	case TPS65912_REG_DCDC3:
		pmic->dcdc3_range = tps65912_reg_read(mfd,
							TPS65912_DCDC3_LIMIT);
		if (pmic->dcdc3_range < 0)
			return pmic->dcdc3_range;
		pmic->dcdc3_range = (pmic->dcdc3_range &
			DCDC_LIMIT_RANGE_MASK) >> DCDC_LIMIT_RANGE_SHIFT;
		return pmic->dcdc3_range;
	case TPS65912_REG_DCDC4:
		pmic->dcdc4_range = tps65912_reg_read(mfd,
							TPS65912_DCDC4_LIMIT);
		if (pmic->dcdc4_range < 0)
			return pmic->dcdc4_range;
		pmic->dcdc4_range = (pmic->dcdc4_range &
			DCDC_LIMIT_RANGE_MASK) >> DCDC_LIMIT_RANGE_SHIFT;
		return pmic->dcdc4_range;
	default:
		return 0;
	}
}

static unsigned long tps65912_vsel_to_uv_range0(u8 vsel)
{
	unsigned long uv;

	uv = ((vsel * 12500) + 500000);
	return uv;
}

static unsigned long tps65912_vsel_to_uv_range1(u8 vsel)
{
	unsigned long uv;

	 uv = ((vsel * 12500) + 700000);
	return uv;
}

static unsigned long tps65912_vsel_to_uv_range2(u8 vsel)
{
	unsigned long uv;

	uv = ((vsel * 25000) + 500000);
	return uv;
}

static unsigned long tps65912_vsel_to_uv_range3(u8 vsel)
{
	unsigned long uv;

	if (vsel == 0x3f)
		uv = 3800000;
	else
		uv = ((vsel * 50000) + 500000);

	return uv;
}

static unsigned long tps65912_vsel_to_uv_ldo(u8 vsel)
{
	unsigned long uv = 0;

	if (vsel <= 32)
		uv = ((vsel * 25000) + 800000);
	else if (vsel > 32 && vsel <= 60)
		uv = (((vsel - 32) * 50000) + 1600000);
	else if (vsel > 60)
		uv = (((vsel - 60) * 100000) + 3000000);

	return uv;
}

static int tps65912_get_ctrl_register(int id)
{
	switch (id) {
	case TPS65912_REG_DCDC1:
		return TPS65912_DCDC1_AVS;
	case TPS65912_REG_DCDC2:
		return TPS65912_DCDC2_AVS;
	case TPS65912_REG_DCDC3:
		return TPS65912_DCDC3_AVS;
	case TPS65912_REG_DCDC4:
		return TPS65912_DCDC4_AVS;
	case TPS65912_REG_LDO1:
		return TPS65912_LDO1_AVS;
	case TPS65912_REG_LDO2:
		return TPS65912_LDO2_AVS;
	case TPS65912_REG_LDO3:
		return TPS65912_LDO3_AVS;
	case TPS65912_REG_LDO4:
		return TPS65912_LDO4_AVS;
	case TPS65912_REG_LDO5:
		return TPS65912_LDO5;
	case TPS65912_REG_LDO6:
		return TPS65912_LDO6;
	case TPS65912_REG_LDO7:
		return TPS65912_LDO7;
	case TPS65912_REG_LDO8:
		return TPS65912_LDO8;
	case TPS65912_REG_LDO9:
		return TPS65912_LDO9;
	case TPS65912_REG_LDO10:
		return TPS65912_LDO10;
	default:
		return -EINVAL;
	}
}

static int tps65912_get_dcdc_sel_register(struct tps65912_reg *pmic, int id)
{
	struct tps65912 *mfd = pmic->mfd;
	int opvsel = 0, sr = 0;
	u8 reg = 0;

	if (id < TPS65912_REG_DCDC1 || id > TPS65912_REG_DCDC4)
		return -EINVAL;

	switch (id) {
	case TPS65912_REG_DCDC1:
		opvsel = tps65912_reg_read(mfd, TPS65912_DCDC1_OP);
		sr = ((opvsel & OP_SELREG_MASK) >> OP_SELREG_SHIFT);
		if (sr)
			reg = TPS65912_DCDC1_AVS;
		else
			reg = TPS65912_DCDC1_OP;
		break;
	case TPS65912_REG_DCDC2:
		opvsel = tps65912_reg_read(mfd, TPS65912_DCDC2_OP);
		sr = (opvsel & OP_SELREG_MASK) >> OP_SELREG_SHIFT;
		if (sr)
			reg = TPS65912_DCDC2_AVS;
		else
			reg = TPS65912_DCDC2_OP;
		break;
	case TPS65912_REG_DCDC3:
		opvsel = tps65912_reg_read(mfd, TPS65912_DCDC3_OP);
		sr = (opvsel & OP_SELREG_MASK) >> OP_SELREG_SHIFT;
		if (sr)
			reg = TPS65912_DCDC3_AVS;
		else
			reg = TPS65912_DCDC3_OP;
		break;
	case TPS65912_REG_DCDC4:
		opvsel = tps65912_reg_read(mfd, TPS65912_DCDC4_OP);
		sr = (opvsel & OP_SELREG_MASK) >> OP_SELREG_SHIFT;
		if (sr)
			reg = TPS65912_DCDC4_AVS;
		else
			reg = TPS65912_DCDC4_OP;
		break;
	}
	return reg;
}

static int tps65912_get_ldo_sel_register(struct tps65912_reg *pmic, int id)
{
	struct tps65912 *mfd = pmic->mfd;
	int opvsel = 0, sr = 0;
	u8 reg = 0;

	if (id < TPS65912_REG_LDO1 || id > TPS65912_REG_LDO10)
		return -EINVAL;

	switch (id) {
	case TPS65912_REG_LDO1:
		opvsel = tps65912_reg_read(mfd, TPS65912_LDO1_OP);
		sr = (opvsel & OP_SELREG_MASK) >> OP_SELREG_SHIFT;
		if (sr)
			reg = TPS65912_LDO1_AVS;
		else
			reg = TPS65912_LDO1_OP;
		break;
	case TPS65912_REG_LDO2:
		opvsel = tps65912_reg_read(mfd, TPS65912_LDO2_OP);
		sr = (opvsel & OP_SELREG_MASK) >> OP_SELREG_SHIFT;
		if (sr)
			reg = TPS65912_LDO2_AVS;
		else
			reg = TPS65912_LDO2_OP;
		break;
	case TPS65912_REG_LDO3:
		opvsel = tps65912_reg_read(mfd, TPS65912_LDO3_OP);
		sr = (opvsel & OP_SELREG_MASK) >> OP_SELREG_SHIFT;
		if (sr)
			reg = TPS65912_LDO3_AVS;
		else
			reg = TPS65912_LDO3_OP;
		break;
	case TPS65912_REG_LDO4:
		opvsel = tps65912_reg_read(mfd, TPS65912_LDO4_OP);
		sr = (opvsel & OP_SELREG_MASK) >> OP_SELREG_SHIFT;
		if (sr)
			reg = TPS65912_LDO4_AVS;
		else
			reg = TPS65912_LDO4_OP;
		break;
	case TPS65912_REG_LDO5:
		reg = TPS65912_LDO5;
		break;
	case TPS65912_REG_LDO6:
		reg = TPS65912_LDO6;
		break;
	case TPS65912_REG_LDO7:
		reg = TPS65912_LDO7;
		break;
	case TPS65912_REG_LDO8:
		reg = TPS65912_LDO8;
		break;
	case TPS65912_REG_LDO9:
		reg = TPS65912_LDO9;
		break;
	case TPS65912_REG_LDO10:
		reg = TPS65912_LDO10;
		break;
	}

	return reg;
}

static int tps65912_get_mode_regiters(struct tps65912_reg *pmic, int id)
{
	switch (id) {
	case TPS65912_REG_DCDC1:
		pmic->pwm_mode_reg = TPS65912_DCDC1_CTRL;
		pmic->eco_reg = TPS65912_DCDC1_AVS;
		break;
	case TPS65912_REG_DCDC2:
		pmic->pwm_mode_reg = TPS65912_DCDC2_CTRL;
		pmic->eco_reg = TPS65912_DCDC2_AVS;
		break;
	case TPS65912_REG_DCDC3:
		pmic->pwm_mode_reg = TPS65912_DCDC3_CTRL;
		pmic->eco_reg = TPS65912_DCDC3_AVS;
		break;
	case TPS65912_REG_DCDC4:
		pmic->pwm_mode_reg = TPS65912_DCDC4_CTRL;
		pmic->eco_reg = TPS65912_DCDC4_AVS;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int tps65912_reg_is_enabled(struct regulator_dev *dev)
{
	struct tps65912_reg *pmic = rdev_get_drvdata(dev);
	struct tps65912 *mfd = pmic->mfd;
	int reg, value, id = rdev_get_id(dev);

	if (id < TPS65912_REG_DCDC1 || id > TPS65912_REG_LDO10)
		return -EINVAL;

	reg = pmic->get_ctrl_reg(id);
	if (reg < 0)
		return reg;

	value = tps65912_reg_read(mfd, reg);
	if (value < 0)
		return value;

	return value & TPS65912_REG_ENABLED;
}

static int tps65912_reg_enable(struct regulator_dev *dev)
{
	struct tps65912_reg *pmic = rdev_get_drvdata(dev);
	struct tps65912 *mfd = pmic->mfd;
	int id = rdev_get_id(dev);
	int reg;

	if (id < TPS65912_REG_DCDC1 || id > TPS65912_REG_LDO10)
		return -EINVAL;

	reg = pmic->get_ctrl_reg(id);
	if (reg < 0)
		return reg;

	return tps65912_set_bits(mfd, reg, TPS65912_REG_ENABLED);
}

static int tps65912_reg_disable(struct regulator_dev *dev)
{
	struct tps65912_reg *pmic = rdev_get_drvdata(dev);
	struct tps65912 *mfd = pmic->mfd;
	int id = rdev_get_id(dev), reg;

	reg = pmic->get_ctrl_reg(id);
	if (reg < 0)
		return reg;

	return tps65912_clear_bits(mfd, reg, TPS65912_REG_ENABLED);
}

static int tps65912_set_mode(struct regulator_dev *dev, unsigned int mode)
{
	struct tps65912_reg *pmic = rdev_get_drvdata(dev);
	struct tps65912 *mfd = pmic->mfd;
	int pwm_mode, eco, id = rdev_get_id(dev);

	tps65912_get_mode_regiters(pmic, id);

	pwm_mode = tps65912_reg_read(mfd, pmic->pwm_mode_reg);
	eco = tps65912_reg_read(mfd, pmic->eco_reg);

	pwm_mode &= DCDCCTRL_DCDC_MODE_MASK;
	eco &= DCDC_AVS_ECO_MASK;

	switch (mode) {
	case REGULATOR_MODE_FAST:
		/* Verify if mode alredy set */
		if (pwm_mode && !eco)
			break;
		tps65912_set_bits(mfd, pmic->pwm_mode_reg, DCDCCTRL_DCDC_MODE_MASK);
		tps65912_clear_bits(mfd, pmic->eco_reg, DCDC_AVS_ECO_MASK);
		break;
	case REGULATOR_MODE_NORMAL:
	case REGULATOR_MODE_IDLE:
		if (!pwm_mode && !eco)
			break;
		tps65912_clear_bits(mfd, pmic->pwm_mode_reg, DCDCCTRL_DCDC_MODE_MASK);
		tps65912_clear_bits(mfd, pmic->eco_reg, DCDC_AVS_ECO_MASK);
		break;
	case REGULATOR_MODE_STANDBY:
		if (!pwm_mode && eco)
			break;
		tps65912_clear_bits(mfd, pmic->pwm_mode_reg, DCDCCTRL_DCDC_MODE_MASK);
		tps65912_set_bits(mfd, pmic->eco_reg, DCDC_AVS_ECO_MASK);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static unsigned int tps65912_get_mode(struct regulator_dev *dev)
{
	struct tps65912_reg *pmic = rdev_get_drvdata(dev);
	struct tps65912 *mfd = pmic->mfd;
	int pwm_mode, eco, mode = 0, id = rdev_get_id(dev);

	tps65912_get_mode_regiters(pmic, id);

	pwm_mode = tps65912_reg_read(mfd, pmic->pwm_mode_reg);
	eco = tps65912_reg_read(mfd, pmic->eco_reg);

	pwm_mode &= DCDCCTRL_DCDC_MODE_MASK;
	eco &= DCDC_AVS_ECO_MASK;

	if (pwm_mode && !eco)
		mode = REGULATOR_MODE_FAST;
	else if (!pwm_mode && !eco)
		mode = REGULATOR_MODE_NORMAL;
	else if (!pwm_mode && eco)
		mode = REGULATOR_MODE_STANDBY;

	return mode;
}

static int tps65912_get_voltage_dcdc(struct regulator_dev *dev)
{
	struct tps65912_reg *pmic = rdev_get_drvdata(dev);
	struct tps65912 *mfd = pmic->mfd;
	int id = rdev_get_id(dev), voltage = 0, range;
	int opvsel = 0, avsel = 0, sr, vsel;

	switch (id) {
	case TPS65912_REG_DCDC1:
		opvsel = tps65912_reg_read(mfd, TPS65912_DCDC1_OP);
		avsel = tps65912_reg_read(mfd, TPS65912_DCDC1_AVS);
		range = pmic->dcdc1_range;
		break;
	case TPS65912_REG_DCDC2:
		opvsel = tps65912_reg_read(mfd, TPS65912_DCDC2_OP);
		avsel = tps65912_reg_read(mfd, TPS65912_DCDC2_AVS);
		range = pmic->dcdc2_range;
		break;
	case TPS65912_REG_DCDC3:
		opvsel = tps65912_reg_read(mfd, TPS65912_DCDC3_OP);
		avsel = tps65912_reg_read(mfd, TPS65912_DCDC3_AVS);
		range = pmic->dcdc3_range;
		break;
	case TPS65912_REG_DCDC4:
		opvsel = tps65912_reg_read(mfd, TPS65912_DCDC4_OP);
		avsel = tps65912_reg_read(mfd, TPS65912_DCDC4_AVS);
		range = pmic->dcdc4_range;
		break;
	default:
		return -EINVAL;
	}

	sr = (opvsel & OP_SELREG_MASK) >> OP_SELREG_SHIFT;
	if (sr)
		vsel = avsel;
	else
		vsel = opvsel;
	vsel &= 0x3F;

	switch (range) {
	case 0:
		/* 0.5 - 1.2875V in 12.5mV steps */
		voltage = tps65912_vsel_to_uv_range0(vsel);
		break;
	case 1:
		/* 0.7 - 1.4875V in 12.5mV steps */
		voltage = tps65912_vsel_to_uv_range1(vsel);
		break;
	case 2:
		/* 0.5 - 2.075V in 25mV steps */
		voltage = tps65912_vsel_to_uv_range2(vsel);
		break;
	case 3:
		/* 0.5 - 3.8V in 50mV steps */
		voltage = tps65912_vsel_to_uv_range3(vsel);
		break;
	}
	return voltage;
}

static int tps65912_set_voltage_dcdc(struct regulator_dev *dev,
						unsigned selector)
{
	struct tps65912_reg *pmic = rdev_get_drvdata(dev);
	struct tps65912 *mfd = pmic->mfd;
	int id = rdev_get_id(dev);
	int value;
	u8 reg;

	reg = tps65912_get_dcdc_sel_register(pmic, id);
	value = tps65912_reg_read(mfd, reg);
	value &= 0xC0;
	return tps65912_reg_write(mfd, reg, selector | value);
}

static int tps65912_get_voltage_ldo(struct regulator_dev *dev)
{
	struct tps65912_reg *pmic = rdev_get_drvdata(dev);
	struct tps65912 *mfd = pmic->mfd;
	int id = rdev_get_id(dev);
	int vsel = 0;
	u8 reg;

	reg = tps65912_get_ldo_sel_register(pmic, id);
	vsel = tps65912_reg_read(mfd, reg);
	vsel &= 0x3F;

	return tps65912_vsel_to_uv_ldo(vsel);
}

static int tps65912_set_voltage_ldo(struct regulator_dev *dev,
						unsigned selector)
{
	struct tps65912_reg *pmic = rdev_get_drvdata(dev);
	struct tps65912 *mfd = pmic->mfd;
	int id = rdev_get_id(dev), reg, value;

	reg = tps65912_get_ldo_sel_register(pmic, id);
	value = tps65912_reg_read(mfd, reg);
	value &= 0xC0;
	return tps65912_reg_write(mfd, reg, selector | value);
}

static int tps65912_list_voltage_dcdc(struct regulator_dev *dev,
					unsigned selector)
{
	struct tps65912_reg *pmic = rdev_get_drvdata(dev);
	int range, voltage = 0, id = rdev_get_id(dev);

	switch (id) {
	case TPS65912_REG_DCDC1:
		range = pmic->dcdc1_range;
		break;
	case TPS65912_REG_DCDC2:
		range = pmic->dcdc2_range;
		break;
	case TPS65912_REG_DCDC3:
		range = pmic->dcdc3_range;
		break;
	case TPS65912_REG_DCDC4:
		range = pmic->dcdc4_range;
		break;
	default:
		return -EINVAL;
	}

	switch (range) {
	case 0:
		/* 0.5 - 1.2875V in 12.5mV steps */
		voltage = tps65912_vsel_to_uv_range0(selector);
		break;
	case 1:
		/* 0.7 - 1.4875V in 12.5mV steps */
		voltage = tps65912_vsel_to_uv_range1(selector);
		break;
	case 2:
		/* 0.5 - 2.075V in 25mV steps */
		voltage = tps65912_vsel_to_uv_range2(selector);
		break;
	case 3:
		/* 0.5 - 3.8V in 50mV steps */
		voltage = tps65912_vsel_to_uv_range3(selector);
		break;
	}
	return voltage;
}

static int tps65912_list_voltage_ldo(struct regulator_dev *dev,
					unsigned selector)
{
	int ldo = rdev_get_id(dev);

	if (ldo < TPS65912_REG_LDO1 || ldo > TPS65912_REG_LDO10)
		return -EINVAL;

	return tps65912_vsel_to_uv_ldo(selector);
}

/* Operations permitted on DCDCx */
static struct regulator_ops tps65912_ops_dcdc = {
	.is_enabled = tps65912_reg_is_enabled,
	.enable = tps65912_reg_enable,
	.disable = tps65912_reg_disable,
	.set_mode = tps65912_set_mode,
	.get_mode = tps65912_get_mode,
	.get_voltage = tps65912_get_voltage_dcdc,
	.set_voltage_sel = tps65912_set_voltage_dcdc,
	.list_voltage = tps65912_list_voltage_dcdc,
};

/* Operations permitted on LDOx */
static struct regulator_ops tps65912_ops_ldo = {
	.is_enabled = tps65912_reg_is_enabled,
	.enable = tps65912_reg_enable,
	.disable = tps65912_reg_disable,
	.get_voltage = tps65912_get_voltage_ldo,
	.set_voltage_sel = tps65912_set_voltage_ldo,
	.list_voltage = tps65912_list_voltage_ldo,
};

static __devinit int tps65912_probe(struct platform_device *pdev)
{
	struct tps65912 *tps65912 = dev_get_drvdata(pdev->dev.parent);
	struct tps_info *info;
	struct regulator_init_data *reg_data;
	struct regulator_dev *rdev;
	struct tps65912_reg *pmic;
	struct tps65912_board *pmic_plat_data;
	int i, err;

	pmic_plat_data = dev_get_platdata(tps65912->dev);
	if (!pmic_plat_data)
		return -EINVAL;

	reg_data = pmic_plat_data->tps65912_pmic_init_data;

	pmic = kzalloc(sizeof(*pmic), GFP_KERNEL);
	if (!pmic)
		return -ENOMEM;

	mutex_init(&pmic->io_lock);
	pmic->mfd = tps65912;
	platform_set_drvdata(pdev, pmic);

	pmic->get_ctrl_reg = &tps65912_get_ctrl_register;
	info = tps65912_regs;

	for (i = 0; i < TPS65912_NUM_REGULATOR; i++, info++, reg_data++) {
		int range = 0;
		/* Register the regulators */
		pmic->info[i] = info;

		pmic->desc[i].name = info->name;
		pmic->desc[i].id = i;
		pmic->desc[i].n_voltages = 64;
		pmic->desc[i].ops = (i > TPS65912_REG_DCDC4 ?
			&tps65912_ops_ldo : &tps65912_ops_dcdc);
		pmic->desc[i].type = REGULATOR_VOLTAGE;
		pmic->desc[i].owner = THIS_MODULE;
		range = tps65912_get_range(pmic, i);
		rdev = regulator_register(&pmic->desc[i],
					tps65912->dev, reg_data, pmic);
		if (IS_ERR(rdev)) {
			dev_err(tps65912->dev,
				"failed to register %s regulator\n",
				pdev->name);
			err = PTR_ERR(rdev);
			goto err;
		}

		/* Save regulator for cleanup */
		pmic->rdev[i] = rdev;
	}
	return 0;

err:
	while (--i >= 0)
		regulator_unregister(pmic->rdev[i]);

	kfree(pmic);
	return err;
}

static int __devexit tps65912_remove(struct platform_device *pdev)
{
	struct tps65912_reg *tps65912_reg = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < TPS65912_NUM_REGULATOR; i++)
		regulator_unregister(tps65912_reg->rdev[i]);

	kfree(tps65912_reg);
	return 0;
}

static struct platform_driver tps65912_driver = {
	.driver = {
		.name = "tps65912-pmic",
		.owner = THIS_MODULE,
	},
	.probe = tps65912_probe,
	.remove = __devexit_p(tps65912_remove),
};

/**
 * tps65912_init
 *
 * Module init function
 */
static int __init tps65912_init(void)
{
	return platform_driver_register(&tps65912_driver);
}
subsys_initcall(tps65912_init);

/**
 * tps65912_cleanup
 *
 * Module exit function
 */
static void __exit tps65912_cleanup(void)
{
	platform_driver_unregister(&tps65912_driver);
}
module_exit(tps65912_cleanup);

MODULE_AUTHOR("Margarita Olaya Cabrera <magi@slimlogic.co.uk>");
MODULE_DESCRIPTION("TPS65912 voltage regulator driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:tps65912-pmic");
