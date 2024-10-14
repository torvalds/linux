// SPDX-License-Identifier: GPL-2.0-only
/*
 * Regulator driver for Rockchip RK805/RK808/RK818
 *
 * Copyright (c) 2014, Fuzhou Rockchip Electronics Co., Ltd
 * Copyright (c) 2021 Rockchip Electronics Co., Ltd.
 *
 * Author: Chris Zhong <zyw@rock-chips.com>
 * Author: Zhang Qing <zhangqing@rock-chips.com>
 * Author: Xu Shengfei <xsf@rock-chips.com>
 *
 * Copyright (C) 2016 PHYTEC Messtechnik GmbH
 *
 * Author: Wadim Egorov <w.egorov@phytec.de>
 */

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/mfd/rk808.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/gpio/consumer.h>

/* Field Definitions */
#define RK808_BUCK_VSEL_MASK	0x3f
#define RK808_BUCK4_VSEL_MASK	0xf
#define RK808_LDO_VSEL_MASK	0x1f

#define RK809_BUCK5_VSEL_MASK		0x7

#define RK817_LDO_VSEL_MASK		0x7f
#define RK817_BOOST_VSEL_MASK		0x7
#define RK817_BUCK_VSEL_MASK		0x7f

#define RK818_BUCK_VSEL_MASK		0x3f
#define RK818_BUCK4_VSEL_MASK		0x1f
#define RK818_LDO_VSEL_MASK		0x1f
#define RK818_LDO3_ON_VSEL_MASK		0xf
#define RK818_BOOST_ON_VSEL_MASK	0xe0

#define RK806_DCDC_SLP_REG_OFFSET	0x0A
#define RK806_NLDO_SLP_REG_OFFSET	0x05
#define RK806_PLDO_SLP_REG_OFFSET	0x06

#define RK806_BUCK_SEL_CNT		0xff
#define RK806_LDO_SEL_CNT		0xff

/* Ramp rate definitions for buck1 / buck2 only */
#define RK808_RAMP_RATE_OFFSET		3
#define RK808_RAMP_RATE_MASK		(3 << RK808_RAMP_RATE_OFFSET)
#define RK808_RAMP_RATE_2MV_PER_US	(0 << RK808_RAMP_RATE_OFFSET)
#define RK808_RAMP_RATE_4MV_PER_US	(1 << RK808_RAMP_RATE_OFFSET)
#define RK808_RAMP_RATE_6MV_PER_US	(2 << RK808_RAMP_RATE_OFFSET)
#define RK808_RAMP_RATE_10MV_PER_US	(3 << RK808_RAMP_RATE_OFFSET)

#define RK808_DVS2_POL		BIT(2)
#define RK808_DVS1_POL		BIT(1)

/* Offset from XXX_ON_VSEL to XXX_SLP_VSEL */
#define RK808_SLP_REG_OFFSET 1

/* Offset from XXX_ON_VSEL to XXX_DVS_VSEL */
#define RK808_DVS_REG_OFFSET 2

/* Offset from XXX_EN_REG to SLEEP_SET_OFF_XXX */
#define RK808_SLP_SET_OFF_REG_OFFSET 2

/* max steps for increase voltage of Buck1/2, equal 100mv*/
#define MAX_STEPS_ONE_TIME 8

#define ENABLE_MASK(id)			(BIT(id) | BIT(4 + (id)))
#define DISABLE_VAL(id)			(BIT(4 + (id)))

#define RK817_BOOST_DESC(_id, _match, _supply, _min, _max, _step, _vreg,\
	_vmask, _ereg, _emask, _enval, _disval, _etime, m_drop)		\
	{							\
		.name		= (_match),				\
		.supply_name	= (_supply),				\
		.of_match	= of_match_ptr(_match),			\
		.regulators_node = of_match_ptr("regulators"),		\
		.type		= REGULATOR_VOLTAGE,			\
		.id		= (_id),				\
		.n_voltages	= (((_max) - (_min)) / (_step) + 1),	\
		.owner		= THIS_MODULE,				\
		.min_uV		= (_min) * 1000,			\
		.uV_step	= (_step) * 1000,			\
		.vsel_reg	= (_vreg),				\
		.vsel_mask	= (_vmask),				\
		.enable_reg	= (_ereg),				\
		.enable_mask	= (_emask),				\
		.enable_val     = (_enval),				\
		.disable_val     = (_disval),				\
		.enable_time	= (_etime),				\
		.min_dropout_uV = (m_drop) * 1000,			\
		.ops		= &rk817_boost_ops,			\
	}

#define RK8XX_DESC_COM(_id, _match, _supply, _min, _max, _step, _vreg,	\
	_vmask, _ereg, _emask, _enval, _disval, _etime, _ops)		\
	{								\
		.name		= (_match),				\
		.supply_name	= (_supply),				\
		.of_match	= of_match_ptr(_match),			\
		.regulators_node = of_match_ptr("regulators"),		\
		.type		= REGULATOR_VOLTAGE,			\
		.id		= (_id),				\
		.n_voltages	= (((_max) - (_min)) / (_step) + 1),	\
		.owner		= THIS_MODULE,				\
		.min_uV		= (_min) * 1000,			\
		.uV_step	= (_step) * 1000,			\
		.vsel_reg	= (_vreg),				\
		.vsel_mask	= (_vmask),				\
		.enable_reg	= (_ereg),				\
		.enable_mask	= (_emask),				\
		.enable_val     = (_enval),				\
		.disable_val     = (_disval),				\
		.enable_time	= (_etime),				\
		.ops		= _ops,			\
	}

#define RK805_DESC(_id, _match, _supply, _min, _max, _step, _vreg,	\
	_vmask, _ereg, _emask, _etime)					\
	RK8XX_DESC_COM(_id, _match, _supply, _min, _max, _step, _vreg,	\
	_vmask, _ereg, _emask, 0, 0, _etime, &rk805_reg_ops)

#define RK806_REGULATOR(_name, _supply_name, _id, _ops,\
			_n_voltages, _vr, _er, _lr, ctrl_bit,\
			_rr, _rm, _rt)\
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
		.of_map_mode = rk8xx_regulator_of_map_mode,\
		.ramp_reg = _rr,\
		.ramp_mask = _rm,\
		.ramp_delay_table = _rt, \
		.n_ramp_values = ARRAY_SIZE(_rt), \
		.owner = THIS_MODULE,\
	}

#define RK8XX_DESC(_id, _match, _supply, _min, _max, _step, _vreg,	\
	_vmask, _ereg, _emask, _etime)					\
	RK8XX_DESC_COM(_id, _match, _supply, _min, _max, _step, _vreg,	\
	_vmask, _ereg, _emask, 0, 0, _etime, &rk808_reg_ops)

#define RK816_DESC(_id, _match, _supply, _min, _max, _step, _vreg,	\
	_vmask, _ereg, _emask, _disval, _etime)				\
	RK8XX_DESC_COM(_id, _match, _supply, _min, _max, _step, _vreg,	\
	_vmask, _ereg, _emask, _emask, _disval, _etime, &rk816_reg_ops)

#define RK817_DESC(_id, _match, _supply, _min, _max, _step, _vreg,	\
	_vmask, _ereg, _emask, _disval, _etime)				\
	RK8XX_DESC_COM(_id, _match, _supply, _min, _max, _step, _vreg,	\
	_vmask, _ereg, _emask, _emask, _disval, _etime, &rk817_reg_ops)

#define RKXX_DESC_SWITCH_COM(_id, _match, _supply, _ereg, _emask,	\
	_enval, _disval, _ops)						\
	{								\
		.name		= (_match),				\
		.supply_name	= (_supply),				\
		.of_match	= of_match_ptr(_match),			\
		.regulators_node = of_match_ptr("regulators"),		\
		.type		= REGULATOR_VOLTAGE,			\
		.id		= (_id),				\
		.enable_reg	= (_ereg),				\
		.enable_mask	= (_emask),				\
		.enable_val     = (_enval),				\
		.disable_val     = (_disval),				\
		.owner		= THIS_MODULE,				\
		.ops		= _ops					\
	}

#define RK817_DESC_SWITCH(_id, _match, _supply, _ereg, _emask,		\
	_disval)							\
	RKXX_DESC_SWITCH_COM(_id, _match, _supply, _ereg, _emask,	\
	_emask, _disval, &rk817_switch_ops)

#define RK8XX_DESC_SWITCH(_id, _match, _supply, _ereg, _emask)		\
	RKXX_DESC_SWITCH_COM(_id, _match, _supply, _ereg, _emask,	\
	0, 0, &rk808_switch_ops)

struct rk8xx_register_bit {
	u8 reg;
	u8 bit;
};

#define RK8XX_REG_BIT(_reg, _bit)					\
	{								\
		.reg = _reg,						\
		.bit = BIT(_bit),						\
	}

struct rk808_regulator_data {
	struct gpio_desc *dvs_gpio[2];
};

static const struct linear_range rk808_ldo3_voltage_ranges[] = {
	REGULATOR_LINEAR_RANGE(800000, 0, 13, 100000),
	REGULATOR_LINEAR_RANGE(2500000, 15, 15, 0),
};

#define RK809_BUCK5_SEL_CNT		(8)

static const struct linear_range rk809_buck5_voltage_ranges[] = {
	REGULATOR_LINEAR_RANGE(1500000, 0, 0, 0),
	REGULATOR_LINEAR_RANGE(1800000, 1, 3, 200000),
	REGULATOR_LINEAR_RANGE(2800000, 4, 5, 200000),
	REGULATOR_LINEAR_RANGE(3300000, 6, 7, 300000),
};

#define RK817_BUCK1_MIN0 500000
#define RK817_BUCK1_MAX0 1500000

#define RK817_BUCK1_MIN1 1600000
#define RK817_BUCK1_MAX1 2400000

#define RK817_BUCK3_MAX1 3400000

#define RK817_BUCK1_STP0 12500
#define RK817_BUCK1_STP1 100000

#define RK817_BUCK1_SEL0 ((RK817_BUCK1_MAX0 - RK817_BUCK1_MIN0) /\
						  RK817_BUCK1_STP0)
#define RK817_BUCK1_SEL1 ((RK817_BUCK1_MAX1 - RK817_BUCK1_MIN1) /\
						  RK817_BUCK1_STP1)

#define RK817_BUCK3_SEL1 ((RK817_BUCK3_MAX1 - RK817_BUCK1_MIN1) /\
						  RK817_BUCK1_STP1)

#define RK817_BUCK1_SEL_CNT (RK817_BUCK1_SEL0 + RK817_BUCK1_SEL1 + 1)
#define RK817_BUCK3_SEL_CNT (RK817_BUCK1_SEL0 + RK817_BUCK3_SEL1 + 1)

static const struct linear_range rk817_buck1_voltage_ranges[] = {
	REGULATOR_LINEAR_RANGE(RK817_BUCK1_MIN0, 0,
			       RK817_BUCK1_SEL0, RK817_BUCK1_STP0),
	REGULATOR_LINEAR_RANGE(RK817_BUCK1_MIN1, RK817_BUCK1_SEL0 + 1,
			       RK817_BUCK1_SEL_CNT, RK817_BUCK1_STP1),
};

static const struct linear_range rk817_buck3_voltage_ranges[] = {
	REGULATOR_LINEAR_RANGE(RK817_BUCK1_MIN0, 0,
			       RK817_BUCK1_SEL0, RK817_BUCK1_STP0),
	REGULATOR_LINEAR_RANGE(RK817_BUCK1_MIN1, RK817_BUCK1_SEL0 + 1,
			       RK817_BUCK3_SEL_CNT, RK817_BUCK1_STP1),
};

static const unsigned int rk808_buck1_2_ramp_table[] = {
	2000, 4000, 6000, 10000
};

/* RK817/RK809/RK816 (buck 1/2 only) */
static const unsigned int rk817_buck1_4_ramp_table[] = {
	3000, 6300, 12500, 25000
};

static int rk806_set_mode_dcdc(struct regulator_dev *rdev, unsigned int mode)
{
	int rid = rdev_get_id(rdev);
	int ctr_bit, reg;

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
		dev_err(rdev_get_dev(rdev), "mode unsupported: %u\n", mode);
		return -EINVAL;
	}

	return 0;
}

static unsigned int rk806_get_mode_dcdc(struct regulator_dev *rdev)
{
	int rid = rdev_get_id(rdev);
	int ctr_bit, reg;
	unsigned int val;
	int err;

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

static const struct rk8xx_register_bit rk806_dcdc_rate2[] = {
	RK8XX_REG_BIT(0xEB, 0),
	RK8XX_REG_BIT(0xEB, 1),
	RK8XX_REG_BIT(0xEB, 2),
	RK8XX_REG_BIT(0xEB, 3),
	RK8XX_REG_BIT(0xEB, 4),
	RK8XX_REG_BIT(0xEB, 5),
	RK8XX_REG_BIT(0xEB, 6),
	RK8XX_REG_BIT(0xEB, 7),
	RK8XX_REG_BIT(0xEA, 0),
	RK8XX_REG_BIT(0xEA, 1),
};

static const unsigned int rk806_ramp_delay_table_dcdc[] = {
	50000, 25000, 12500, 6250, 3125, 1560, 961, 390
};

static int rk806_set_ramp_delay_dcdc(struct regulator_dev *rdev, int ramp_delay)
{
	int rid = rdev_get_id(rdev);
	int regval, ramp_value, ret;

	ret = regulator_find_closest_bigger(ramp_delay, rdev->desc->ramp_delay_table,
					    rdev->desc->n_ramp_values, &ramp_value);
	if (ret) {
		dev_warn(rdev_get_dev(rdev),
			 "Can't set ramp-delay %u, setting %u\n", ramp_delay,
			 rdev->desc->ramp_delay_table[ramp_value]);
	}

	regval = ramp_value << (ffs(rdev->desc->ramp_mask) - 1);

	ret = regmap_update_bits(rdev->regmap, rdev->desc->ramp_reg,
				 rdev->desc->ramp_mask, regval);
	if (ret)
		return ret;

	/*
	 * The above is effectively a copy of regulator_set_ramp_delay_regmap(),
	 * but that only stores the lower 2 bits for rk806 DCDC ramp. The MSB must
	 * be stored in a separate register, so this open codes the implementation
	 * to have access to the ramp_value.
	 */

	regval = (ramp_value >> 2) & 0x1 ? rk806_dcdc_rate2[rid].bit : 0;
	return regmap_update_bits(rdev->regmap, rk806_dcdc_rate2[rid].reg,
				  rk806_dcdc_rate2[rid].bit,
				  regval);
}

static const unsigned int rk806_ramp_delay_table_ldo[] = {
	100000, 50000, 25000, 12500, 6280, 3120, 1900, 780
};

static int rk806_set_suspend_voltage_range(struct regulator_dev *rdev, int reg_offset, int uv)
{
	int sel = regulator_map_voltage_linear_range(rdev, uv, uv);
	unsigned int reg;

	if (sel < 0)
		return -EINVAL;

	reg = rdev->desc->vsel_reg + reg_offset;

	return regmap_update_bits(rdev->regmap, reg, rdev->desc->vsel_mask, sel);
}

static int rk806_set_suspend_voltage_range_dcdc(struct regulator_dev *rdev, int uv)
{
	return rk806_set_suspend_voltage_range(rdev, RK806_DCDC_SLP_REG_OFFSET, uv);
}

static int rk806_set_suspend_voltage_range_nldo(struct regulator_dev *rdev, int uv)
{
	return rk806_set_suspend_voltage_range(rdev, RK806_NLDO_SLP_REG_OFFSET, uv);
}

static int rk806_set_suspend_voltage_range_pldo(struct regulator_dev *rdev, int uv)
{
	return rk806_set_suspend_voltage_range(rdev, RK806_PLDO_SLP_REG_OFFSET, uv);
}

static int rk808_buck1_2_get_voltage_sel_regmap(struct regulator_dev *rdev)
{
	struct rk808_regulator_data *pdata = rdev_get_drvdata(rdev);
	int id = rdev_get_id(rdev);
	struct gpio_desc *gpio = pdata->dvs_gpio[id];
	unsigned int val;
	int ret;

	if (!gpio || gpiod_get_value(gpio) == 0)
		return regulator_get_voltage_sel_regmap(rdev);

	ret = regmap_read(rdev->regmap,
			  rdev->desc->vsel_reg + RK808_DVS_REG_OFFSET,
			  &val);
	if (ret != 0)
		return ret;

	val &= rdev->desc->vsel_mask;
	val >>= ffs(rdev->desc->vsel_mask) - 1;

	return val;
}

static int rk808_buck1_2_i2c_set_voltage_sel(struct regulator_dev *rdev,
					     unsigned sel)
{
	int ret, delta_sel;
	unsigned int old_sel, tmp, val, mask = rdev->desc->vsel_mask;

	ret = regmap_read(rdev->regmap, rdev->desc->vsel_reg, &val);
	if (ret != 0)
		return ret;

	tmp = val & ~mask;
	old_sel = val & mask;
	old_sel >>= ffs(mask) - 1;
	delta_sel = sel - old_sel;

	/*
	 * If directly modify the register to change the voltage, we will face
	 * the risk of overshoot. Put it into a multi-step, can effectively
	 * avoid this problem, a step is 100mv here.
	 */
	while (delta_sel > MAX_STEPS_ONE_TIME) {
		old_sel += MAX_STEPS_ONE_TIME;
		val = old_sel << (ffs(mask) - 1);
		val |= tmp;

		/*
		 * i2c is 400kHz (2.5us per bit) and we must transmit _at least_
		 * 3 bytes (24 bits) plus start and stop so 26 bits.  So we've
		 * got more than 65 us between each voltage change and thus
		 * won't ramp faster than ~1500 uV / us.
		 */
		ret = regmap_write(rdev->regmap, rdev->desc->vsel_reg, val);
		delta_sel = sel - old_sel;
	}

	sel <<= ffs(mask) - 1;
	val = tmp | sel;
	ret = regmap_write(rdev->regmap, rdev->desc->vsel_reg, val);

	/*
	 * When we change the voltage register directly, the ramp rate is about
	 * 100000uv/us, wait 1us to make sure the target voltage to be stable,
	 * so we needn't wait extra time after that.
	 */
	udelay(1);

	return ret;
}

static int rk808_buck1_2_set_voltage_sel(struct regulator_dev *rdev,
					 unsigned sel)
{
	struct rk808_regulator_data *pdata = rdev_get_drvdata(rdev);
	int id = rdev_get_id(rdev);
	struct gpio_desc *gpio = pdata->dvs_gpio[id];
	unsigned int reg = rdev->desc->vsel_reg;
	unsigned old_sel;
	int ret, gpio_level;

	if (!gpio)
		return rk808_buck1_2_i2c_set_voltage_sel(rdev, sel);

	gpio_level = gpiod_get_value(gpio);
	if (gpio_level == 0) {
		reg += RK808_DVS_REG_OFFSET;
		ret = regmap_read(rdev->regmap, rdev->desc->vsel_reg, &old_sel);
	} else {
		ret = regmap_read(rdev->regmap,
				  reg + RK808_DVS_REG_OFFSET,
				  &old_sel);
	}

	if (ret != 0)
		return ret;

	sel <<= ffs(rdev->desc->vsel_mask) - 1;
	sel |= old_sel & ~rdev->desc->vsel_mask;

	ret = regmap_write(rdev->regmap, reg, sel);
	if (ret)
		return ret;

	gpiod_set_value(gpio, !gpio_level);

	return ret;
}

static int rk808_buck1_2_set_voltage_time_sel(struct regulator_dev *rdev,
				       unsigned int old_selector,
				       unsigned int new_selector)
{
	struct rk808_regulator_data *pdata = rdev_get_drvdata(rdev);
	int id = rdev_get_id(rdev);
	struct gpio_desc *gpio = pdata->dvs_gpio[id];

	/* if there is no dvs1/2 pin, we don't need wait extra time here. */
	if (!gpio)
		return 0;

	return regulator_set_voltage_time_sel(rdev, old_selector, new_selector);
}

static int rk808_set_suspend_voltage(struct regulator_dev *rdev, int uv)
{
	unsigned int reg;
	int sel = regulator_map_voltage_linear(rdev, uv, uv);

	if (sel < 0)
		return -EINVAL;

	reg = rdev->desc->vsel_reg + RK808_SLP_REG_OFFSET;

	return regmap_update_bits(rdev->regmap, reg,
				  rdev->desc->vsel_mask,
				  sel);
}

static int rk808_set_suspend_voltage_range(struct regulator_dev *rdev, int uv)
{
	unsigned int reg;
	int sel = regulator_map_voltage_linear_range(rdev, uv, uv);
	int ret;

	if (sel < 0)
		return -EINVAL;

	reg = rdev->desc->vsel_reg + RK808_SLP_REG_OFFSET;

	ret = regmap_update_bits(rdev->regmap, reg,
				 rdev->desc->vsel_mask,
				 sel);
	if (ret)
		return ret;

	if (rdev->desc->apply_bit)
		ret = regmap_update_bits(rdev->regmap, rdev->desc->apply_reg,
					 rdev->desc->apply_bit,
					 rdev->desc->apply_bit);

	return ret;
}

static int rk805_set_suspend_enable(struct regulator_dev *rdev)
{
	unsigned int reg;

	reg = rdev->desc->enable_reg + RK808_SLP_SET_OFF_REG_OFFSET;

	return regmap_update_bits(rdev->regmap, reg,
				  rdev->desc->enable_mask,
				  rdev->desc->enable_mask);
}

static int rk805_set_suspend_disable(struct regulator_dev *rdev)
{
	unsigned int reg;

	reg = rdev->desc->enable_reg + RK808_SLP_SET_OFF_REG_OFFSET;

	return regmap_update_bits(rdev->regmap, reg,
				  rdev->desc->enable_mask,
				  0);
}

static const struct rk8xx_register_bit rk806_suspend_bits[] = {
	RK8XX_REG_BIT(RK806_POWER_SLP_EN0, 0),
	RK8XX_REG_BIT(RK806_POWER_SLP_EN0, 1),
	RK8XX_REG_BIT(RK806_POWER_SLP_EN0, 2),
	RK8XX_REG_BIT(RK806_POWER_SLP_EN0, 3),
	RK8XX_REG_BIT(RK806_POWER_SLP_EN0, 4),
	RK8XX_REG_BIT(RK806_POWER_SLP_EN0, 5),
	RK8XX_REG_BIT(RK806_POWER_SLP_EN0, 6),
	RK8XX_REG_BIT(RK806_POWER_SLP_EN0, 7),
	RK8XX_REG_BIT(RK806_POWER_SLP_EN1, 6),
	RK8XX_REG_BIT(RK806_POWER_SLP_EN1, 7),
	RK8XX_REG_BIT(RK806_POWER_SLP_EN1, 0),
	RK8XX_REG_BIT(RK806_POWER_SLP_EN1, 1),
	RK8XX_REG_BIT(RK806_POWER_SLP_EN1, 2),
	RK8XX_REG_BIT(RK806_POWER_SLP_EN1, 3),
	RK8XX_REG_BIT(RK806_POWER_SLP_EN1, 4),
	RK8XX_REG_BIT(RK806_POWER_SLP_EN2, 1),
	RK8XX_REG_BIT(RK806_POWER_SLP_EN2, 2),
	RK8XX_REG_BIT(RK806_POWER_SLP_EN2, 3),
	RK8XX_REG_BIT(RK806_POWER_SLP_EN2, 4),
	RK8XX_REG_BIT(RK806_POWER_SLP_EN2, 5),
	RK8XX_REG_BIT(RK806_POWER_SLP_EN2, 0),
};

static int rk806_set_suspend_enable(struct regulator_dev *rdev)
{
	int rid = rdev_get_id(rdev);

	return regmap_update_bits(rdev->regmap, rk806_suspend_bits[rid].reg,
				  rk806_suspend_bits[rid].bit,
				  rk806_suspend_bits[rid].bit);
}

static int rk806_set_suspend_disable(struct regulator_dev *rdev)
{
	int rid = rdev_get_id(rdev);

	return regmap_update_bits(rdev->regmap, rk806_suspend_bits[rid].reg,
				  rk806_suspend_bits[rid].bit, 0);
}

static int rk808_set_suspend_enable(struct regulator_dev *rdev)
{
	unsigned int reg;

	reg = rdev->desc->enable_reg + RK808_SLP_SET_OFF_REG_OFFSET;

	return regmap_update_bits(rdev->regmap, reg,
				  rdev->desc->enable_mask,
				  0);
}

static int rk808_set_suspend_disable(struct regulator_dev *rdev)
{
	unsigned int reg;

	reg = rdev->desc->enable_reg + RK808_SLP_SET_OFF_REG_OFFSET;

	return regmap_update_bits(rdev->regmap, reg,
				  rdev->desc->enable_mask,
				  rdev->desc->enable_mask);
}

static const struct rk8xx_register_bit rk816_suspend_bits[] = {
	RK8XX_REG_BIT(RK818_SLEEP_SET_OFF_REG1, 0),
	RK8XX_REG_BIT(RK818_SLEEP_SET_OFF_REG1, 1),
	RK8XX_REG_BIT(RK818_SLEEP_SET_OFF_REG1, 2),
	RK8XX_REG_BIT(RK818_SLEEP_SET_OFF_REG1, 3),
	RK8XX_REG_BIT(RK818_SLEEP_SET_OFF_REG2, 0),
	RK8XX_REG_BIT(RK818_SLEEP_SET_OFF_REG2, 1),
	RK8XX_REG_BIT(RK818_SLEEP_SET_OFF_REG2, 2),
	RK8XX_REG_BIT(RK818_SLEEP_SET_OFF_REG2, 3),
	RK8XX_REG_BIT(RK818_SLEEP_SET_OFF_REG2, 4),
	RK8XX_REG_BIT(RK818_SLEEP_SET_OFF_REG2, 5),
	RK8XX_REG_BIT(RK818_SLEEP_SET_OFF_REG1, 5),
	RK8XX_REG_BIT(RK818_SLEEP_SET_OFF_REG1, 6),
};

static int rk816_set_suspend_enable(struct regulator_dev *rdev)
{
	int rid = rdev_get_id(rdev);

	return regmap_update_bits(rdev->regmap, rk816_suspend_bits[rid].reg,
				  rk816_suspend_bits[rid].bit,
				  rk816_suspend_bits[rid].bit);
}

static int rk816_set_suspend_disable(struct regulator_dev *rdev)
{
	int rid = rdev_get_id(rdev);

	return regmap_update_bits(rdev->regmap, rk816_suspend_bits[rid].reg,
				  rk816_suspend_bits[rid].bit, 0);
}

static int rk817_set_suspend_enable_ctrl(struct regulator_dev *rdev,
					 unsigned int en)
{
	unsigned int reg;
	int id = rdev_get_id(rdev);
	unsigned int id_slp, msk, val;

	if (id >= RK817_ID_DCDC1 && id <= RK817_ID_DCDC4)
		id_slp = id;
	else if (id >= RK817_ID_LDO1 && id <= RK817_ID_LDO8)
		id_slp = 8 + (id - RK817_ID_LDO1);
	else if (id >= RK817_ID_LDO9 && id <= RK809_ID_SW2)
		id_slp = 4 + (id - RK817_ID_LDO9);
	else
		return -EINVAL;

	reg = RK817_POWER_SLP_EN_REG(id_slp / 8);

	msk = BIT(id_slp % 8);
	if (en)
		val = msk;
	else
		val = 0;

	return regmap_update_bits(rdev->regmap, reg, msk, val);
}

static int rk817_set_suspend_enable(struct regulator_dev *rdev)
{
	return rk817_set_suspend_enable_ctrl(rdev, 1);
}

static int rk817_set_suspend_disable(struct regulator_dev *rdev)
{
	return rk817_set_suspend_enable_ctrl(rdev, 0);
}

static int rk8xx_set_suspend_mode(struct regulator_dev *rdev, unsigned int mode)
{
	unsigned int reg;

	reg = rdev->desc->vsel_reg + RK808_SLP_REG_OFFSET;

	switch (mode) {
	case REGULATOR_MODE_FAST:
		return regmap_update_bits(rdev->regmap, reg,
					  PWM_MODE_MSK, FPWM_MODE);
	case REGULATOR_MODE_NORMAL:
		return regmap_update_bits(rdev->regmap, reg,
					  PWM_MODE_MSK, AUTO_PWM_MODE);
	default:
		dev_err(&rdev->dev, "do not support this mode\n");
		return -EINVAL;
	}

	return 0;
}

static int rk8xx_set_mode(struct regulator_dev *rdev, unsigned int mode)
{
	switch (mode) {
	case REGULATOR_MODE_FAST:
		return regmap_update_bits(rdev->regmap, rdev->desc->vsel_reg,
					  PWM_MODE_MSK, FPWM_MODE);
	case REGULATOR_MODE_NORMAL:
		return regmap_update_bits(rdev->regmap, rdev->desc->vsel_reg,
					  PWM_MODE_MSK, AUTO_PWM_MODE);
	default:
		dev_err(&rdev->dev, "do not support this mode\n");
		return -EINVAL;
	}

	return 0;
}

static unsigned int rk8xx_get_mode(struct regulator_dev *rdev)
{
	unsigned int val;
	int err;

	err = regmap_read(rdev->regmap, rdev->desc->vsel_reg, &val);
	if (err)
		return err;

	if (val & FPWM_MODE)
		return REGULATOR_MODE_FAST;
	else
		return REGULATOR_MODE_NORMAL;
}

static int rk8xx_is_enabled_wmsk_regmap(struct regulator_dev *rdev)
{
	unsigned int val;
	int ret;

	ret = regmap_read(rdev->regmap, rdev->desc->enable_reg, &val);
	if (ret != 0)
		return ret;

	/* add write mask bit */
	val |= (rdev->desc->enable_mask & 0xf0);
	val &= rdev->desc->enable_mask;

	if (rdev->desc->enable_is_inverted) {
		if (rdev->desc->enable_val)
			return val != rdev->desc->enable_val;
		return (val == 0);
	}
	if (rdev->desc->enable_val)
		return val == rdev->desc->enable_val;
	return val != 0;
}

static unsigned int rk8xx_regulator_of_map_mode(unsigned int mode)
{
	switch (mode) {
	case 1:
		return REGULATOR_MODE_FAST;
	case 2:
		return REGULATOR_MODE_NORMAL;
	default:
		return REGULATOR_MODE_INVALID;
	}
}

static const struct regulator_ops rk805_reg_ops = {
	.list_voltage           = regulator_list_voltage_linear,
	.map_voltage            = regulator_map_voltage_linear,
	.get_voltage_sel        = regulator_get_voltage_sel_regmap,
	.set_voltage_sel        = regulator_set_voltage_sel_regmap,
	.enable                 = regulator_enable_regmap,
	.disable                = regulator_disable_regmap,
	.is_enabled             = regulator_is_enabled_regmap,
	.set_suspend_voltage    = rk808_set_suspend_voltage,
	.set_suspend_enable     = rk805_set_suspend_enable,
	.set_suspend_disable    = rk805_set_suspend_disable,
};

static const struct regulator_ops rk805_switch_ops = {
	.enable                 = regulator_enable_regmap,
	.disable                = regulator_disable_regmap,
	.is_enabled             = regulator_is_enabled_regmap,
	.set_suspend_enable     = rk805_set_suspend_enable,
	.set_suspend_disable    = rk805_set_suspend_disable,
};

static const struct regulator_ops rk806_ops_dcdc = {
	.list_voltage		= regulator_list_voltage_linear_range,
	.map_voltage		= regulator_map_voltage_linear_range,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel        = regulator_set_voltage_sel_regmap,
	.set_voltage_time_sel	= regulator_set_voltage_time_sel,
	.set_mode		= rk806_set_mode_dcdc,
	.get_mode		= rk806_get_mode_dcdc,

	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= rk8xx_is_enabled_wmsk_regmap,

	.set_suspend_mode	= rk806_set_mode_dcdc,
	.set_ramp_delay		= rk806_set_ramp_delay_dcdc,

	.set_suspend_voltage	= rk806_set_suspend_voltage_range_dcdc,
	.set_suspend_enable	= rk806_set_suspend_enable,
	.set_suspend_disable	= rk806_set_suspend_disable,
};

static const struct regulator_ops rk806_ops_nldo = {
	.list_voltage		= regulator_list_voltage_linear_range,
	.map_voltage		= regulator_map_voltage_linear_range,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel        = regulator_set_voltage_sel_regmap,
	.set_voltage_time_sel	= regulator_set_voltage_time_sel,

	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= regulator_is_enabled_regmap,

	.set_ramp_delay		= regulator_set_ramp_delay_regmap,

	.set_suspend_voltage	= rk806_set_suspend_voltage_range_nldo,
	.set_suspend_enable	= rk806_set_suspend_enable,
	.set_suspend_disable	= rk806_set_suspend_disable,
};

static const struct regulator_ops rk806_ops_pldo = {
	.list_voltage		= regulator_list_voltage_linear_range,
	.map_voltage		= regulator_map_voltage_linear_range,

	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel        = regulator_set_voltage_sel_regmap,
	.set_voltage_time_sel	= regulator_set_voltage_time_sel,

	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= regulator_is_enabled_regmap,

	.set_ramp_delay		= regulator_set_ramp_delay_regmap,

	.set_suspend_voltage	= rk806_set_suspend_voltage_range_pldo,
	.set_suspend_enable	= rk806_set_suspend_enable,
	.set_suspend_disable	= rk806_set_suspend_disable,
};

static const struct regulator_ops rk808_buck1_2_ops = {
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.get_voltage_sel	= rk808_buck1_2_get_voltage_sel_regmap,
	.set_voltage_sel	= rk808_buck1_2_set_voltage_sel,
	.set_voltage_time_sel	= rk808_buck1_2_set_voltage_time_sel,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= regulator_is_enabled_regmap,
	.set_ramp_delay		= regulator_set_ramp_delay_regmap,
	.set_suspend_voltage	= rk808_set_suspend_voltage,
	.set_suspend_enable	= rk808_set_suspend_enable,
	.set_suspend_disable	= rk808_set_suspend_disable,
};

static const struct regulator_ops rk808_reg_ops = {
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= regulator_is_enabled_regmap,
	.set_suspend_voltage	= rk808_set_suspend_voltage,
	.set_suspend_enable	= rk808_set_suspend_enable,
	.set_suspend_disable	= rk808_set_suspend_disable,
};

static const struct regulator_ops rk808_reg_ops_ranges = {
	.list_voltage		= regulator_list_voltage_linear_range,
	.map_voltage		= regulator_map_voltage_linear_range,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= regulator_is_enabled_regmap,
	.set_suspend_voltage	= rk808_set_suspend_voltage_range,
	.set_suspend_enable	= rk808_set_suspend_enable,
	.set_suspend_disable	= rk808_set_suspend_disable,
};

static const struct regulator_ops rk808_switch_ops = {
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= regulator_is_enabled_regmap,
	.set_suspend_enable	= rk808_set_suspend_enable,
	.set_suspend_disable	= rk808_set_suspend_disable,
};

static const struct linear_range rk805_buck_1_2_voltage_ranges[] = {
	REGULATOR_LINEAR_RANGE(712500, 0, 59, 12500),
	REGULATOR_LINEAR_RANGE(1800000, 60, 62, 200000),
	REGULATOR_LINEAR_RANGE(2300000, 63, 63, 0),
};

static const struct regulator_ops rk809_buck5_ops_range = {
	.list_voltage		= regulator_list_voltage_linear_range,
	.map_voltage		= regulator_map_voltage_linear_range,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.set_voltage_time_sel	= regulator_set_voltage_time_sel,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= rk8xx_is_enabled_wmsk_regmap,
	.set_suspend_voltage	= rk808_set_suspend_voltage_range,
	.set_suspend_enable	= rk817_set_suspend_enable,
	.set_suspend_disable	= rk817_set_suspend_disable,
};

static const struct regulator_ops rk816_buck1_2_ops_ranges = {
	.list_voltage		= regulator_list_voltage_linear_range,
	.map_voltage		= regulator_map_voltage_linear_range,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.set_voltage_time_sel	= regulator_set_voltage_time_sel,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= regulator_is_enabled_regmap,
	.set_mode		= rk8xx_set_mode,
	.get_mode		= rk8xx_get_mode,
	.set_suspend_mode	= rk8xx_set_suspend_mode,
	.set_ramp_delay		= regulator_set_ramp_delay_regmap,
	.set_suspend_voltage	= rk808_set_suspend_voltage_range,
	.set_suspend_enable	= rk816_set_suspend_enable,
	.set_suspend_disable	= rk816_set_suspend_disable,
};

static const struct regulator_ops rk816_buck4_ops_ranges = {
	.list_voltage		= regulator_list_voltage_linear_range,
	.map_voltage		= regulator_map_voltage_linear_range,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.set_voltage_time_sel	= regulator_set_voltage_time_sel,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= regulator_is_enabled_regmap,
	.set_mode		= rk8xx_set_mode,
	.get_mode		= rk8xx_get_mode,
	.set_suspend_mode	= rk8xx_set_suspend_mode,
	.set_suspend_voltage	= rk808_set_suspend_voltage_range,
	.set_suspend_enable	= rk816_set_suspend_enable,
	.set_suspend_disable	= rk816_set_suspend_disable,
};

static const struct regulator_ops rk816_reg_ops = {
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= rk8xx_is_enabled_wmsk_regmap,
	.set_suspend_voltage	= rk808_set_suspend_voltage,
	.set_suspend_enable	= rk816_set_suspend_enable,
	.set_suspend_disable	= rk816_set_suspend_disable,
};

static const struct regulator_ops rk817_reg_ops = {
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= rk8xx_is_enabled_wmsk_regmap,
	.set_suspend_voltage	= rk808_set_suspend_voltage,
	.set_suspend_enable	= rk817_set_suspend_enable,
	.set_suspend_disable	= rk817_set_suspend_disable,
};

static const struct regulator_ops rk817_boost_ops = {
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= rk8xx_is_enabled_wmsk_regmap,
	.set_suspend_enable	= rk817_set_suspend_enable,
	.set_suspend_disable	= rk817_set_suspend_disable,
};

static const struct regulator_ops rk817_buck_ops_range = {
	.list_voltage		= regulator_list_voltage_linear_range,
	.map_voltage		= regulator_map_voltage_linear_range,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.set_voltage_time_sel	= regulator_set_voltage_time_sel,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= rk8xx_is_enabled_wmsk_regmap,
	.set_mode		= rk8xx_set_mode,
	.get_mode		= rk8xx_get_mode,
	.set_suspend_mode	= rk8xx_set_suspend_mode,
	.set_ramp_delay		= regulator_set_ramp_delay_regmap,
	.set_suspend_voltage	= rk808_set_suspend_voltage_range,
	.set_suspend_enable	= rk817_set_suspend_enable,
	.set_suspend_disable	= rk817_set_suspend_disable,
};

static const struct regulator_ops rk817_switch_ops = {
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= rk8xx_is_enabled_wmsk_regmap,
	.set_suspend_enable	= rk817_set_suspend_enable,
	.set_suspend_disable	= rk817_set_suspend_disable,
};

static const struct regulator_desc rk805_reg[] = {
	{
		.name = "DCDC_REG1",
		.supply_name = "vcc1",
		.of_match = of_match_ptr("DCDC_REG1"),
		.regulators_node = of_match_ptr("regulators"),
		.id = RK805_ID_DCDC1,
		.ops = &rk808_reg_ops_ranges,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = 64,
		.linear_ranges = rk805_buck_1_2_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(rk805_buck_1_2_voltage_ranges),
		.vsel_reg = RK805_BUCK1_ON_VSEL_REG,
		.vsel_mask = RK818_BUCK_VSEL_MASK,
		.enable_reg = RK805_DCDC_EN_REG,
		.enable_mask = BIT(0),
		.owner = THIS_MODULE,
	}, {
		.name = "DCDC_REG2",
		.supply_name = "vcc2",
		.of_match = of_match_ptr("DCDC_REG2"),
		.regulators_node = of_match_ptr("regulators"),
		.id = RK805_ID_DCDC2,
		.ops = &rk808_reg_ops_ranges,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = 64,
		.linear_ranges = rk805_buck_1_2_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(rk805_buck_1_2_voltage_ranges),
		.vsel_reg = RK805_BUCK2_ON_VSEL_REG,
		.vsel_mask = RK818_BUCK_VSEL_MASK,
		.enable_reg = RK805_DCDC_EN_REG,
		.enable_mask = BIT(1),
		.owner = THIS_MODULE,
	}, {
		.name = "DCDC_REG3",
		.supply_name = "vcc3",
		.of_match = of_match_ptr("DCDC_REG3"),
		.regulators_node = of_match_ptr("regulators"),
		.id = RK805_ID_DCDC3,
		.ops = &rk805_switch_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = 1,
		.enable_reg = RK805_DCDC_EN_REG,
		.enable_mask = BIT(2),
		.owner = THIS_MODULE,
	},

	RK805_DESC(RK805_ID_DCDC4, "DCDC_REG4", "vcc4", 800, 3400, 100,
		RK805_BUCK4_ON_VSEL_REG, RK818_BUCK4_VSEL_MASK,
		RK805_DCDC_EN_REG, BIT(3), 0),

	RK805_DESC(RK805_ID_LDO1, "LDO_REG1", "vcc5", 800, 3400, 100,
		RK805_LDO1_ON_VSEL_REG, RK818_LDO_VSEL_MASK, RK805_LDO_EN_REG,
		BIT(0), 400),
	RK805_DESC(RK805_ID_LDO2, "LDO_REG2", "vcc5", 800, 3400, 100,
		RK805_LDO2_ON_VSEL_REG, RK818_LDO_VSEL_MASK, RK805_LDO_EN_REG,
		BIT(1), 400),
	RK805_DESC(RK805_ID_LDO3, "LDO_REG3", "vcc6", 800, 3400, 100,
		RK805_LDO3_ON_VSEL_REG, RK818_LDO_VSEL_MASK, RK805_LDO_EN_REG,
		BIT(2), 400),
};

static const struct linear_range rk806_buck_voltage_ranges[] = {
	REGULATOR_LINEAR_RANGE(500000, 0, 159, 6250), /* 500mV ~ 1500mV */
	REGULATOR_LINEAR_RANGE(1500000, 160, 235, 25000), /* 1500mV ~ 3400mV */
	REGULATOR_LINEAR_RANGE(3400000, 236, 255, 0),
};

static const struct linear_range rk806_ldo_voltage_ranges[] = {
	REGULATOR_LINEAR_RANGE(500000, 0, 231, 12500), /* 500mV ~ 3400mV */
	REGULATOR_LINEAR_RANGE(3400000, 232, 255, 0),
};

static const struct regulator_desc rk806_reg[] = {
	RK806_REGULATOR("dcdc-reg1", "vcc1", RK806_ID_DCDC1, rk806_ops_dcdc,
			RK806_BUCK_SEL_CNT, RK806_BUCK1_ON_VSEL,
			RK806_POWER_EN0, rk806_buck_voltage_ranges, 0,
			RK806_BUCK1_CONFIG, 0xc0, rk806_ramp_delay_table_dcdc),
	RK806_REGULATOR("dcdc-reg2", "vcc2", RK806_ID_DCDC2, rk806_ops_dcdc,
			RK806_BUCK_SEL_CNT, RK806_BUCK2_ON_VSEL,
			RK806_POWER_EN0, rk806_buck_voltage_ranges, 1,
			RK806_BUCK2_CONFIG, 0xc0, rk806_ramp_delay_table_dcdc),
	RK806_REGULATOR("dcdc-reg3", "vcc3", RK806_ID_DCDC3, rk806_ops_dcdc,
			RK806_BUCK_SEL_CNT, RK806_BUCK3_ON_VSEL,
			RK806_POWER_EN0, rk806_buck_voltage_ranges, 2,
			RK806_BUCK3_CONFIG, 0xc0, rk806_ramp_delay_table_dcdc),
	RK806_REGULATOR("dcdc-reg4", "vcc4", RK806_ID_DCDC4, rk806_ops_dcdc,
			RK806_BUCK_SEL_CNT, RK806_BUCK4_ON_VSEL,
			RK806_POWER_EN0, rk806_buck_voltage_ranges, 3,
			RK806_BUCK4_CONFIG, 0xc0, rk806_ramp_delay_table_dcdc),

	RK806_REGULATOR("dcdc-reg5", "vcc5", RK806_ID_DCDC5, rk806_ops_dcdc,
			RK806_BUCK_SEL_CNT, RK806_BUCK5_ON_VSEL,
			RK806_POWER_EN1, rk806_buck_voltage_ranges, 0,
			RK806_BUCK5_CONFIG, 0xc0, rk806_ramp_delay_table_dcdc),
	RK806_REGULATOR("dcdc-reg6", "vcc6", RK806_ID_DCDC6, rk806_ops_dcdc,
			RK806_BUCK_SEL_CNT, RK806_BUCK6_ON_VSEL,
			RK806_POWER_EN1, rk806_buck_voltage_ranges, 1,
			RK806_BUCK6_CONFIG, 0xc0, rk806_ramp_delay_table_dcdc),
	RK806_REGULATOR("dcdc-reg7", "vcc7", RK806_ID_DCDC7, rk806_ops_dcdc,
			RK806_BUCK_SEL_CNT, RK806_BUCK7_ON_VSEL,
			RK806_POWER_EN1, rk806_buck_voltage_ranges, 2,
			RK806_BUCK7_CONFIG, 0xc0, rk806_ramp_delay_table_dcdc),
	RK806_REGULATOR("dcdc-reg8", "vcc8", RK806_ID_DCDC8, rk806_ops_dcdc,
			RK806_BUCK_SEL_CNT, RK806_BUCK8_ON_VSEL,
			RK806_POWER_EN1, rk806_buck_voltage_ranges, 3,
			RK806_BUCK8_CONFIG, 0xc0, rk806_ramp_delay_table_dcdc),

	RK806_REGULATOR("dcdc-reg9", "vcc9", RK806_ID_DCDC9, rk806_ops_dcdc,
			RK806_BUCK_SEL_CNT, RK806_BUCK9_ON_VSEL,
			RK806_POWER_EN2, rk806_buck_voltage_ranges, 0,
			RK806_BUCK9_CONFIG, 0xc0, rk806_ramp_delay_table_dcdc),
	RK806_REGULATOR("dcdc-reg10", "vcc10", RK806_ID_DCDC10, rk806_ops_dcdc,
			RK806_BUCK_SEL_CNT, RK806_BUCK10_ON_VSEL,
			RK806_POWER_EN2, rk806_buck_voltage_ranges, 1,
			RK806_BUCK10_CONFIG, 0xc0, rk806_ramp_delay_table_dcdc),

	RK806_REGULATOR("nldo-reg1", "vcc13", RK806_ID_NLDO1, rk806_ops_nldo,
			RK806_LDO_SEL_CNT, RK806_NLDO1_ON_VSEL,
			RK806_POWER_EN3, rk806_ldo_voltage_ranges, 0,
			0xEA, 0x38, rk806_ramp_delay_table_ldo),
	RK806_REGULATOR("nldo-reg2", "vcc13", RK806_ID_NLDO2, rk806_ops_nldo,
			RK806_LDO_SEL_CNT, RK806_NLDO2_ON_VSEL,
			RK806_POWER_EN3, rk806_ldo_voltage_ranges, 1,
			0xEA, 0x38, rk806_ramp_delay_table_ldo),
	RK806_REGULATOR("nldo-reg3", "vcc13", RK806_ID_NLDO3, rk806_ops_nldo,
			RK806_LDO_SEL_CNT, RK806_NLDO3_ON_VSEL,
			RK806_POWER_EN3, rk806_ldo_voltage_ranges, 2,
			0xEA, 0x38, rk806_ramp_delay_table_ldo),
	RK806_REGULATOR("nldo-reg4", "vcc14", RK806_ID_NLDO4, rk806_ops_nldo,
			RK806_LDO_SEL_CNT, RK806_NLDO4_ON_VSEL,
			RK806_POWER_EN3, rk806_ldo_voltage_ranges, 3,
			0xEA, 0x38, rk806_ramp_delay_table_ldo),

	RK806_REGULATOR("nldo-reg5", "vcc14", RK806_ID_NLDO5, rk806_ops_nldo,
			RK806_LDO_SEL_CNT, RK806_NLDO5_ON_VSEL,
			RK806_POWER_EN5, rk806_ldo_voltage_ranges, 2,
			0xEA, 0x38, rk806_ramp_delay_table_ldo),

	RK806_REGULATOR("pldo-reg1", "vcc11", RK806_ID_PLDO1, rk806_ops_pldo,
			RK806_LDO_SEL_CNT, RK806_PLDO1_ON_VSEL,
			RK806_POWER_EN4, rk806_ldo_voltage_ranges, 1,
			0xEA, 0x38, rk806_ramp_delay_table_ldo),
	RK806_REGULATOR("pldo-reg2", "vcc11", RK806_ID_PLDO2, rk806_ops_pldo,
			RK806_LDO_SEL_CNT, RK806_PLDO2_ON_VSEL,
			RK806_POWER_EN4, rk806_ldo_voltage_ranges, 2,
			0xEA, 0x38, rk806_ramp_delay_table_ldo),
	RK806_REGULATOR("pldo-reg3", "vcc11", RK806_ID_PLDO3, rk806_ops_pldo,
			RK806_LDO_SEL_CNT, RK806_PLDO3_ON_VSEL,
			RK806_POWER_EN4, rk806_ldo_voltage_ranges, 3,
			0xEA, 0x38, rk806_ramp_delay_table_ldo),

	RK806_REGULATOR("pldo-reg4", "vcc12", RK806_ID_PLDO4, rk806_ops_pldo,
			RK806_LDO_SEL_CNT, RK806_PLDO4_ON_VSEL,
			RK806_POWER_EN5, rk806_ldo_voltage_ranges, 0,
			0xEA, 0x38, rk806_ramp_delay_table_ldo),
	RK806_REGULATOR("pldo-reg5", "vcc12", RK806_ID_PLDO5, rk806_ops_pldo,
			RK806_LDO_SEL_CNT, RK806_PLDO5_ON_VSEL,
			RK806_POWER_EN5, rk806_ldo_voltage_ranges, 1,
			0xEA, 0x38, rk806_ramp_delay_table_ldo),

	RK806_REGULATOR("pldo-reg6", "vcca", RK806_ID_PLDO6, rk806_ops_pldo,
			RK806_LDO_SEL_CNT, RK806_PLDO6_ON_VSEL,
			RK806_POWER_EN4, rk806_ldo_voltage_ranges, 0,
			0xEA, 0x38, rk806_ramp_delay_table_ldo),
};


static const struct regulator_desc rk808_reg[] = {
	{
		.name = "DCDC_REG1",
		.supply_name = "vcc1",
		.of_match = of_match_ptr("DCDC_REG1"),
		.regulators_node = of_match_ptr("regulators"),
		.id = RK808_ID_DCDC1,
		.ops = &rk808_buck1_2_ops,
		.type = REGULATOR_VOLTAGE,
		.min_uV = 712500,
		.uV_step = 12500,
		.n_voltages = 64,
		.vsel_reg = RK808_BUCK1_ON_VSEL_REG,
		.vsel_mask = RK808_BUCK_VSEL_MASK,
		.enable_reg = RK808_DCDC_EN_REG,
		.enable_mask = BIT(0),
		.ramp_reg = RK808_BUCK1_CONFIG_REG,
		.ramp_mask = RK808_RAMP_RATE_MASK,
		.ramp_delay_table = rk808_buck1_2_ramp_table,
		.n_ramp_values = ARRAY_SIZE(rk808_buck1_2_ramp_table),
		.owner = THIS_MODULE,
	}, {
		.name = "DCDC_REG2",
		.supply_name = "vcc2",
		.of_match = of_match_ptr("DCDC_REG2"),
		.regulators_node = of_match_ptr("regulators"),
		.id = RK808_ID_DCDC2,
		.ops = &rk808_buck1_2_ops,
		.type = REGULATOR_VOLTAGE,
		.min_uV = 712500,
		.uV_step = 12500,
		.n_voltages = 64,
		.vsel_reg = RK808_BUCK2_ON_VSEL_REG,
		.vsel_mask = RK808_BUCK_VSEL_MASK,
		.enable_reg = RK808_DCDC_EN_REG,
		.enable_mask = BIT(1),
		.ramp_reg = RK808_BUCK2_CONFIG_REG,
		.ramp_mask = RK808_RAMP_RATE_MASK,
		.ramp_delay_table = rk808_buck1_2_ramp_table,
		.n_ramp_values = ARRAY_SIZE(rk808_buck1_2_ramp_table),
		.owner = THIS_MODULE,
	}, {
		.name = "DCDC_REG3",
		.supply_name = "vcc3",
		.of_match = of_match_ptr("DCDC_REG3"),
		.regulators_node = of_match_ptr("regulators"),
		.id = RK808_ID_DCDC3,
		.ops = &rk808_switch_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = 1,
		.enable_reg = RK808_DCDC_EN_REG,
		.enable_mask = BIT(2),
		.owner = THIS_MODULE,
	},
	RK8XX_DESC(RK808_ID_DCDC4, "DCDC_REG4", "vcc4", 1800, 3300, 100,
		RK808_BUCK4_ON_VSEL_REG, RK808_BUCK4_VSEL_MASK,
		RK808_DCDC_EN_REG, BIT(3), 0),
	RK8XX_DESC(RK808_ID_LDO1, "LDO_REG1", "vcc6", 1800, 3400, 100,
		RK808_LDO1_ON_VSEL_REG, RK808_LDO_VSEL_MASK, RK808_LDO_EN_REG,
		BIT(0), 400),
	RK8XX_DESC(RK808_ID_LDO2, "LDO_REG2", "vcc6", 1800, 3400, 100,
		RK808_LDO2_ON_VSEL_REG, RK808_LDO_VSEL_MASK, RK808_LDO_EN_REG,
		BIT(1), 400),
	{
		.name = "LDO_REG3",
		.supply_name = "vcc7",
		.of_match = of_match_ptr("LDO_REG3"),
		.regulators_node = of_match_ptr("regulators"),
		.id = RK808_ID_LDO3,
		.ops = &rk808_reg_ops_ranges,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = 16,
		.linear_ranges = rk808_ldo3_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(rk808_ldo3_voltage_ranges),
		.vsel_reg = RK808_LDO3_ON_VSEL_REG,
		.vsel_mask = RK808_BUCK4_VSEL_MASK,
		.enable_reg = RK808_LDO_EN_REG,
		.enable_mask = BIT(2),
		.enable_time = 400,
		.owner = THIS_MODULE,
	},
	RK8XX_DESC(RK808_ID_LDO4, "LDO_REG4", "vcc9", 1800, 3400, 100,
		RK808_LDO4_ON_VSEL_REG, RK808_LDO_VSEL_MASK, RK808_LDO_EN_REG,
		BIT(3), 400),
	RK8XX_DESC(RK808_ID_LDO5, "LDO_REG5", "vcc9", 1800, 3400, 100,
		RK808_LDO5_ON_VSEL_REG, RK808_LDO_VSEL_MASK, RK808_LDO_EN_REG,
		BIT(4), 400),
	RK8XX_DESC(RK808_ID_LDO6, "LDO_REG6", "vcc10", 800, 2500, 100,
		RK808_LDO6_ON_VSEL_REG, RK808_LDO_VSEL_MASK, RK808_LDO_EN_REG,
		BIT(5), 400),
	RK8XX_DESC(RK808_ID_LDO7, "LDO_REG7", "vcc7", 800, 2500, 100,
		RK808_LDO7_ON_VSEL_REG, RK808_LDO_VSEL_MASK, RK808_LDO_EN_REG,
		BIT(6), 400),
	RK8XX_DESC(RK808_ID_LDO8, "LDO_REG8", "vcc11", 1800, 3400, 100,
		RK808_LDO8_ON_VSEL_REG, RK808_LDO_VSEL_MASK, RK808_LDO_EN_REG,
		BIT(7), 400),
	RK8XX_DESC_SWITCH(RK808_ID_SWITCH1, "SWITCH_REG1", "vcc8",
		RK808_DCDC_EN_REG, BIT(5)),
	RK8XX_DESC_SWITCH(RK808_ID_SWITCH2, "SWITCH_REG2", "vcc12",
		RK808_DCDC_EN_REG, BIT(6)),
};

static const struct regulator_desc rk809_reg[] = {
	{
		.name = "DCDC_REG1",
		.supply_name = "vcc1",
		.of_match = of_match_ptr("DCDC_REG1"),
		.regulators_node = of_match_ptr("regulators"),
		.id = RK817_ID_DCDC1,
		.ops = &rk817_buck_ops_range,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = RK817_BUCK1_SEL_CNT + 1,
		.linear_ranges = rk817_buck1_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(rk817_buck1_voltage_ranges),
		.vsel_reg = RK817_BUCK1_ON_VSEL_REG,
		.vsel_mask = RK817_BUCK_VSEL_MASK,
		.enable_reg = RK817_POWER_EN_REG(0),
		.enable_mask = ENABLE_MASK(RK817_ID_DCDC1),
		.enable_val = ENABLE_MASK(RK817_ID_DCDC1),
		.disable_val = DISABLE_VAL(RK817_ID_DCDC1),
		.ramp_reg = RK817_BUCK_CONFIG_REG(RK817_ID_DCDC1),
		.ramp_mask = RK817_RAMP_RATE_MASK,
		.ramp_delay_table = rk817_buck1_4_ramp_table,
		.n_ramp_values = ARRAY_SIZE(rk817_buck1_4_ramp_table),
		.of_map_mode = rk8xx_regulator_of_map_mode,
		.owner = THIS_MODULE,
	}, {
		.name = "DCDC_REG2",
		.supply_name = "vcc2",
		.of_match = of_match_ptr("DCDC_REG2"),
		.regulators_node = of_match_ptr("regulators"),
		.id = RK817_ID_DCDC2,
		.ops = &rk817_buck_ops_range,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = RK817_BUCK1_SEL_CNT + 1,
		.linear_ranges = rk817_buck1_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(rk817_buck1_voltage_ranges),
		.vsel_reg = RK817_BUCK2_ON_VSEL_REG,
		.vsel_mask = RK817_BUCK_VSEL_MASK,
		.enable_reg = RK817_POWER_EN_REG(0),
		.enable_mask = ENABLE_MASK(RK817_ID_DCDC2),
		.enable_val = ENABLE_MASK(RK817_ID_DCDC2),
		.disable_val = DISABLE_VAL(RK817_ID_DCDC2),
		.ramp_reg = RK817_BUCK_CONFIG_REG(RK817_ID_DCDC2),
		.ramp_mask = RK817_RAMP_RATE_MASK,
		.ramp_delay_table = rk817_buck1_4_ramp_table,
		.n_ramp_values = ARRAY_SIZE(rk817_buck1_4_ramp_table),
		.of_map_mode = rk8xx_regulator_of_map_mode,
		.owner = THIS_MODULE,
	}, {
		.name = "DCDC_REG3",
		.supply_name = "vcc3",
		.of_match = of_match_ptr("DCDC_REG3"),
		.regulators_node = of_match_ptr("regulators"),
		.id = RK817_ID_DCDC3,
		.ops = &rk817_buck_ops_range,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = RK817_BUCK1_SEL_CNT + 1,
		.linear_ranges = rk817_buck1_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(rk817_buck1_voltage_ranges),
		.vsel_reg = RK817_BUCK3_ON_VSEL_REG,
		.vsel_mask = RK817_BUCK_VSEL_MASK,
		.apply_reg = RK817_POWER_CONFIG,
		.apply_bit = RK817_BUCK3_FB_RES_INTER,
		.enable_reg = RK817_POWER_EN_REG(0),
		.enable_mask = ENABLE_MASK(RK817_ID_DCDC3),
		.enable_val = ENABLE_MASK(RK817_ID_DCDC3),
		.disable_val = DISABLE_VAL(RK817_ID_DCDC3),
		.ramp_reg = RK817_BUCK_CONFIG_REG(RK817_ID_DCDC3),
		.ramp_mask = RK817_RAMP_RATE_MASK,
		.ramp_delay_table = rk817_buck1_4_ramp_table,
		.n_ramp_values = ARRAY_SIZE(rk817_buck1_4_ramp_table),
		.of_map_mode = rk8xx_regulator_of_map_mode,
		.owner = THIS_MODULE,
	}, {
		.name = "DCDC_REG4",
		.supply_name = "vcc4",
		.of_match = of_match_ptr("DCDC_REG4"),
		.regulators_node = of_match_ptr("regulators"),
		.id = RK817_ID_DCDC4,
		.ops = &rk817_buck_ops_range,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = RK817_BUCK3_SEL_CNT + 1,
		.linear_ranges = rk817_buck3_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(rk817_buck3_voltage_ranges),
		.vsel_reg = RK817_BUCK4_ON_VSEL_REG,
		.vsel_mask = RK817_BUCK_VSEL_MASK,
		.enable_reg = RK817_POWER_EN_REG(0),
		.enable_mask = ENABLE_MASK(RK817_ID_DCDC4),
		.enable_val = ENABLE_MASK(RK817_ID_DCDC4),
		.disable_val = DISABLE_VAL(RK817_ID_DCDC4),
		.ramp_reg = RK817_BUCK_CONFIG_REG(RK817_ID_DCDC4),
		.ramp_mask = RK817_RAMP_RATE_MASK,
		.ramp_delay_table = rk817_buck1_4_ramp_table,
		.n_ramp_values = ARRAY_SIZE(rk817_buck1_4_ramp_table),
		.of_map_mode = rk8xx_regulator_of_map_mode,
		.owner = THIS_MODULE,
	},
	{
		.name = "DCDC_REG5",
		.supply_name = "vcc9",
		.of_match = of_match_ptr("DCDC_REG5"),
		.regulators_node = of_match_ptr("regulators"),
		.id = RK809_ID_DCDC5,
		.ops = &rk809_buck5_ops_range,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = RK809_BUCK5_SEL_CNT,
		.linear_ranges = rk809_buck5_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(rk809_buck5_voltage_ranges),
		.vsel_reg = RK809_BUCK5_CONFIG(0),
		.vsel_mask = RK809_BUCK5_VSEL_MASK,
		.enable_reg = RK817_POWER_EN_REG(3),
		.enable_mask = ENABLE_MASK(1),
		.enable_val = ENABLE_MASK(1),
		.disable_val = DISABLE_VAL(1),
		.of_map_mode = rk8xx_regulator_of_map_mode,
		.owner = THIS_MODULE,
	},
	RK817_DESC(RK817_ID_LDO1, "LDO_REG1", "vcc5", 600, 3400, 25,
		   RK817_LDO_ON_VSEL_REG(0), RK817_LDO_VSEL_MASK,
		   RK817_POWER_EN_REG(1), ENABLE_MASK(0),
		   DISABLE_VAL(0), 400),
	RK817_DESC(RK817_ID_LDO2, "LDO_REG2", "vcc5", 600, 3400, 25,
		   RK817_LDO_ON_VSEL_REG(1), RK817_LDO_VSEL_MASK,
		   RK817_POWER_EN_REG(1), ENABLE_MASK(1),
		   DISABLE_VAL(1), 400),
	RK817_DESC(RK817_ID_LDO3, "LDO_REG3", "vcc5", 600, 3400, 25,
		   RK817_LDO_ON_VSEL_REG(2), RK817_LDO_VSEL_MASK,
		   RK817_POWER_EN_REG(1), ENABLE_MASK(2),
		   DISABLE_VAL(2), 400),
	RK817_DESC(RK817_ID_LDO4, "LDO_REG4", "vcc6", 600, 3400, 25,
		   RK817_LDO_ON_VSEL_REG(3), RK817_LDO_VSEL_MASK,
		   RK817_POWER_EN_REG(1), ENABLE_MASK(3),
		   DISABLE_VAL(3), 400),
	RK817_DESC(RK817_ID_LDO5, "LDO_REG5", "vcc6", 600, 3400, 25,
		   RK817_LDO_ON_VSEL_REG(4), RK817_LDO_VSEL_MASK,
		   RK817_POWER_EN_REG(2), ENABLE_MASK(0),
		   DISABLE_VAL(0), 400),
	RK817_DESC(RK817_ID_LDO6, "LDO_REG6", "vcc6", 600, 3400, 25,
		   RK817_LDO_ON_VSEL_REG(5), RK817_LDO_VSEL_MASK,
		   RK817_POWER_EN_REG(2), ENABLE_MASK(1),
		   DISABLE_VAL(1), 400),
	RK817_DESC(RK817_ID_LDO7, "LDO_REG7", "vcc7", 600, 3400, 25,
		   RK817_LDO_ON_VSEL_REG(6), RK817_LDO_VSEL_MASK,
		   RK817_POWER_EN_REG(2), ENABLE_MASK(2),
		   DISABLE_VAL(2), 400),
	RK817_DESC(RK817_ID_LDO8, "LDO_REG8", "vcc7", 600, 3400, 25,
		   RK817_LDO_ON_VSEL_REG(7), RK817_LDO_VSEL_MASK,
		   RK817_POWER_EN_REG(2), ENABLE_MASK(3),
		   DISABLE_VAL(3), 400),
	RK817_DESC(RK817_ID_LDO9, "LDO_REG9", "vcc7", 600, 3400, 25,
		   RK817_LDO_ON_VSEL_REG(8), RK817_LDO_VSEL_MASK,
		   RK817_POWER_EN_REG(3), ENABLE_MASK(0),
		   DISABLE_VAL(0), 400),
	RK817_DESC_SWITCH(RK809_ID_SW1, "SWITCH_REG1", "vcc9",
			  RK817_POWER_EN_REG(3), ENABLE_MASK(2),
			  DISABLE_VAL(2)),
	RK817_DESC_SWITCH(RK809_ID_SW2, "SWITCH_REG2", "vcc8",
			  RK817_POWER_EN_REG(3), ENABLE_MASK(3),
			  DISABLE_VAL(3)),
};

static const struct linear_range rk816_buck_4_voltage_ranges[] = {
	REGULATOR_LINEAR_RANGE(800000, 0, 26, 100000),
	REGULATOR_LINEAR_RANGE(3500000, 27, 31, 0),
};

static const struct regulator_desc rk816_reg[] = {
	{
		.name = "dcdc1",
		.supply_name = "vcc1",
		.of_match = of_match_ptr("dcdc1"),
		.regulators_node = of_match_ptr("regulators"),
		.id = RK816_ID_DCDC1,
		.ops = &rk816_buck1_2_ops_ranges,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = 64,
		.linear_ranges = rk805_buck_1_2_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(rk805_buck_1_2_voltage_ranges),
		.vsel_reg = RK818_BUCK1_ON_VSEL_REG,
		.vsel_mask = RK818_BUCK_VSEL_MASK,
		.apply_reg = RK816_DCDC_EN_REG2,
		.apply_bit = RK816_BUCK_DVS_CONFIRM,
		.enable_reg = RK816_DCDC_EN_REG1,
		.enable_mask = BIT(4) | BIT(0),
		.enable_val = BIT(4) | BIT(0),
		.disable_val = BIT(4),
		.ramp_reg = RK818_BUCK1_CONFIG_REG,
		.ramp_mask = RK808_RAMP_RATE_MASK,
		.ramp_delay_table = rk817_buck1_4_ramp_table,
		.n_ramp_values = ARRAY_SIZE(rk817_buck1_4_ramp_table),
		.of_map_mode = rk8xx_regulator_of_map_mode,
		.owner = THIS_MODULE,
	}, {
		.name = "dcdc2",
		.supply_name = "vcc2",
		.of_match = of_match_ptr("dcdc2"),
		.regulators_node = of_match_ptr("regulators"),
		.id = RK816_ID_DCDC2,
		.ops = &rk816_buck1_2_ops_ranges,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = 64,
		.linear_ranges = rk805_buck_1_2_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(rk805_buck_1_2_voltage_ranges),
		.vsel_reg = RK818_BUCK2_ON_VSEL_REG,
		.vsel_mask = RK818_BUCK_VSEL_MASK,
		.apply_reg = RK816_DCDC_EN_REG2,
		.apply_bit = RK816_BUCK_DVS_CONFIRM,
		.enable_reg = RK816_DCDC_EN_REG1,
		.enable_mask = BIT(5) | BIT(1),
		.enable_val = BIT(5) | BIT(1),
		.disable_val = BIT(5),
		.ramp_reg = RK818_BUCK2_CONFIG_REG,
		.ramp_mask = RK808_RAMP_RATE_MASK,
		.ramp_delay_table = rk817_buck1_4_ramp_table,
		.n_ramp_values = ARRAY_SIZE(rk817_buck1_4_ramp_table),
		.of_map_mode = rk8xx_regulator_of_map_mode,
		.owner = THIS_MODULE,
	}, {
		.name = "dcdc3",
		.supply_name = "vcc3",
		.of_match = of_match_ptr("dcdc3"),
		.regulators_node = of_match_ptr("regulators"),
		.id = RK816_ID_DCDC3,
		.ops = &rk808_switch_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = 1,
		.enable_reg = RK816_DCDC_EN_REG1,
		.enable_mask = BIT(6) | BIT(2),
		.enable_val =  BIT(6) | BIT(2),
		.disable_val = BIT(6),
		.of_map_mode = rk8xx_regulator_of_map_mode,
		.owner = THIS_MODULE,
	}, {
		.name = "dcdc4",
		.supply_name = "vcc4",
		.of_match = of_match_ptr("dcdc4"),
		.regulators_node = of_match_ptr("regulators"),
		.id = RK816_ID_DCDC4,
		.ops = &rk816_buck4_ops_ranges,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = 32,
		.linear_ranges = rk816_buck_4_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(rk816_buck_4_voltage_ranges),
		.vsel_reg = RK818_BUCK4_ON_VSEL_REG,
		.vsel_mask = RK818_BUCK4_VSEL_MASK,
		.enable_reg = RK816_DCDC_EN_REG1,
		.enable_mask = BIT(7) | BIT(3),
		.enable_val = BIT(7) | BIT(3),
		.disable_val = BIT(7),
		.of_map_mode = rk8xx_regulator_of_map_mode,
		.owner = THIS_MODULE,
	},
	RK816_DESC(RK816_ID_LDO1, "ldo1", "vcc5", 800, 3400, 100,
		   RK818_LDO1_ON_VSEL_REG, RK818_LDO_VSEL_MASK,
		   RK816_LDO_EN_REG1, ENABLE_MASK(0), DISABLE_VAL(0), 400),
	RK816_DESC(RK816_ID_LDO2, "ldo2", "vcc5", 800, 3400, 100,
		   RK818_LDO2_ON_VSEL_REG, RK818_LDO_VSEL_MASK,
		   RK816_LDO_EN_REG1, ENABLE_MASK(1), DISABLE_VAL(1), 400),
	RK816_DESC(RK816_ID_LDO3, "ldo3", "vcc5", 800, 3400, 100,
		   RK818_LDO3_ON_VSEL_REG, RK818_LDO_VSEL_MASK,
		   RK816_LDO_EN_REG1, ENABLE_MASK(2), DISABLE_VAL(2), 400),
	RK816_DESC(RK816_ID_LDO4, "ldo4", "vcc6", 800, 3400, 100,
		   RK818_LDO4_ON_VSEL_REG, RK818_LDO_VSEL_MASK,
		   RK816_LDO_EN_REG1, ENABLE_MASK(3), DISABLE_VAL(3), 400),
	RK816_DESC(RK816_ID_LDO5, "ldo5", "vcc6", 800, 3400, 100,
		   RK818_LDO5_ON_VSEL_REG, RK818_LDO_VSEL_MASK,
		   RK816_LDO_EN_REG2, ENABLE_MASK(0), DISABLE_VAL(0), 400),
	RK816_DESC(RK816_ID_LDO6, "ldo6", "vcc6", 800, 3400, 100,
		   RK818_LDO6_ON_VSEL_REG, RK818_LDO_VSEL_MASK,
		   RK816_LDO_EN_REG2, ENABLE_MASK(1), DISABLE_VAL(1), 400),
};

static const struct regulator_desc rk817_reg[] = {
	{
		.name = "DCDC_REG1",
		.supply_name = "vcc1",
		.of_match = of_match_ptr("DCDC_REG1"),
		.regulators_node = of_match_ptr("regulators"),
		.id = RK817_ID_DCDC1,
		.ops = &rk817_buck_ops_range,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = RK817_BUCK1_SEL_CNT + 1,
		.linear_ranges = rk817_buck1_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(rk817_buck1_voltage_ranges),
		.vsel_reg = RK817_BUCK1_ON_VSEL_REG,
		.vsel_mask = RK817_BUCK_VSEL_MASK,
		.enable_reg = RK817_POWER_EN_REG(0),
		.enable_mask = ENABLE_MASK(RK817_ID_DCDC1),
		.enable_val = ENABLE_MASK(RK817_ID_DCDC1),
		.disable_val = DISABLE_VAL(RK817_ID_DCDC1),
		.ramp_reg = RK817_BUCK_CONFIG_REG(RK817_ID_DCDC1),
		.ramp_mask = RK817_RAMP_RATE_MASK,
		.ramp_delay_table = rk817_buck1_4_ramp_table,
		.n_ramp_values = ARRAY_SIZE(rk817_buck1_4_ramp_table),
		.of_map_mode = rk8xx_regulator_of_map_mode,
		.owner = THIS_MODULE,
	}, {
		.name = "DCDC_REG2",
		.supply_name = "vcc2",
		.of_match = of_match_ptr("DCDC_REG2"),
		.regulators_node = of_match_ptr("regulators"),
		.id = RK817_ID_DCDC2,
		.ops = &rk817_buck_ops_range,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = RK817_BUCK1_SEL_CNT + 1,
		.linear_ranges = rk817_buck1_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(rk817_buck1_voltage_ranges),
		.vsel_reg = RK817_BUCK2_ON_VSEL_REG,
		.vsel_mask = RK817_BUCK_VSEL_MASK,
		.enable_reg = RK817_POWER_EN_REG(0),
		.enable_mask = ENABLE_MASK(RK817_ID_DCDC2),
		.enable_val = ENABLE_MASK(RK817_ID_DCDC2),
		.disable_val = DISABLE_VAL(RK817_ID_DCDC2),
		.ramp_reg = RK817_BUCK_CONFIG_REG(RK817_ID_DCDC2),
		.ramp_mask = RK817_RAMP_RATE_MASK,
		.ramp_delay_table = rk817_buck1_4_ramp_table,
		.n_ramp_values = ARRAY_SIZE(rk817_buck1_4_ramp_table),
		.of_map_mode = rk8xx_regulator_of_map_mode,
		.owner = THIS_MODULE,
	}, {
		.name = "DCDC_REG3",
		.supply_name = "vcc3",
		.of_match = of_match_ptr("DCDC_REG3"),
		.regulators_node = of_match_ptr("regulators"),
		.id = RK817_ID_DCDC3,
		.ops = &rk817_buck_ops_range,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = RK817_BUCK1_SEL_CNT + 1,
		.linear_ranges = rk817_buck1_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(rk817_buck1_voltage_ranges),
		.vsel_reg = RK817_BUCK3_ON_VSEL_REG,
		.vsel_mask = RK817_BUCK_VSEL_MASK,
		.enable_reg = RK817_POWER_EN_REG(0),
		.enable_mask = ENABLE_MASK(RK817_ID_DCDC3),
		.enable_val = ENABLE_MASK(RK817_ID_DCDC3),
		.disable_val = DISABLE_VAL(RK817_ID_DCDC3),
		.ramp_reg = RK817_BUCK_CONFIG_REG(RK817_ID_DCDC3),
		.ramp_mask = RK817_RAMP_RATE_MASK,
		.ramp_delay_table = rk817_buck1_4_ramp_table,
		.n_ramp_values = ARRAY_SIZE(rk817_buck1_4_ramp_table),
		.of_map_mode = rk8xx_regulator_of_map_mode,
		.owner = THIS_MODULE,
	}, {
		.name = "DCDC_REG4",
		.supply_name = "vcc4",
		.of_match = of_match_ptr("DCDC_REG4"),
		.regulators_node = of_match_ptr("regulators"),
		.id = RK817_ID_DCDC4,
		.ops = &rk817_buck_ops_range,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = RK817_BUCK3_SEL_CNT + 1,
		.linear_ranges = rk817_buck3_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(rk817_buck3_voltage_ranges),
		.vsel_reg = RK817_BUCK4_ON_VSEL_REG,
		.vsel_mask = RK817_BUCK_VSEL_MASK,
		.enable_reg = RK817_POWER_EN_REG(0),
		.enable_mask = ENABLE_MASK(RK817_ID_DCDC4),
		.enable_val = ENABLE_MASK(RK817_ID_DCDC4),
		.disable_val = DISABLE_VAL(RK817_ID_DCDC4),
		.ramp_reg = RK817_BUCK_CONFIG_REG(RK817_ID_DCDC4),
		.ramp_mask = RK817_RAMP_RATE_MASK,
		.ramp_delay_table = rk817_buck1_4_ramp_table,
		.n_ramp_values = ARRAY_SIZE(rk817_buck1_4_ramp_table),
		.of_map_mode = rk8xx_regulator_of_map_mode,
		.owner = THIS_MODULE,
	},
	RK817_DESC(RK817_ID_LDO1, "LDO_REG1", "vcc5", 600, 3400, 25,
		   RK817_LDO_ON_VSEL_REG(0), RK817_LDO_VSEL_MASK,
		   RK817_POWER_EN_REG(1), ENABLE_MASK(0),
		   DISABLE_VAL(0), 400),
	RK817_DESC(RK817_ID_LDO2, "LDO_REG2", "vcc5", 600, 3400, 25,
		   RK817_LDO_ON_VSEL_REG(1), RK817_LDO_VSEL_MASK,
		   RK817_POWER_EN_REG(1), ENABLE_MASK(1),
		   DISABLE_VAL(1), 400),
	RK817_DESC(RK817_ID_LDO3, "LDO_REG3", "vcc5", 600, 3400, 25,
		   RK817_LDO_ON_VSEL_REG(2), RK817_LDO_VSEL_MASK,
		   RK817_POWER_EN_REG(1), ENABLE_MASK(2),
		   DISABLE_VAL(2), 400),
	RK817_DESC(RK817_ID_LDO4, "LDO_REG4", "vcc6", 600, 3400, 25,
		   RK817_LDO_ON_VSEL_REG(3), RK817_LDO_VSEL_MASK,
		   RK817_POWER_EN_REG(1), ENABLE_MASK(3),
		   DISABLE_VAL(3), 400),
	RK817_DESC(RK817_ID_LDO5, "LDO_REG5", "vcc6", 600, 3400, 25,
		   RK817_LDO_ON_VSEL_REG(4), RK817_LDO_VSEL_MASK,
		   RK817_POWER_EN_REG(2), ENABLE_MASK(0),
		   DISABLE_VAL(0), 400),
	RK817_DESC(RK817_ID_LDO6, "LDO_REG6", "vcc6", 600, 3400, 25,
		   RK817_LDO_ON_VSEL_REG(5), RK817_LDO_VSEL_MASK,
		   RK817_POWER_EN_REG(2), ENABLE_MASK(1),
		   DISABLE_VAL(1), 400),
	RK817_DESC(RK817_ID_LDO7, "LDO_REG7", "vcc7", 600, 3400, 25,
		   RK817_LDO_ON_VSEL_REG(6), RK817_LDO_VSEL_MASK,
		   RK817_POWER_EN_REG(2), ENABLE_MASK(2),
		   DISABLE_VAL(2), 400),
	RK817_DESC(RK817_ID_LDO8, "LDO_REG8", "vcc7", 600, 3400, 25,
		   RK817_LDO_ON_VSEL_REG(7), RK817_LDO_VSEL_MASK,
		   RK817_POWER_EN_REG(2), ENABLE_MASK(3),
		   DISABLE_VAL(3), 400),
	RK817_DESC(RK817_ID_LDO9, "LDO_REG9", "vcc7", 600, 3400, 25,
		   RK817_LDO_ON_VSEL_REG(8), RK817_LDO_VSEL_MASK,
		   RK817_POWER_EN_REG(3), ENABLE_MASK(0),
		   DISABLE_VAL(0), 400),
	RK817_BOOST_DESC(RK817_ID_BOOST, "BOOST", "vcc8", 4700, 5400, 100,
			 RK817_BOOST_OTG_CFG, RK817_BOOST_VSEL_MASK,
			 RK817_POWER_EN_REG(3), ENABLE_MASK(1), ENABLE_MASK(1),
		   DISABLE_VAL(1), 400, 3500 - 5400),
	RK817_DESC_SWITCH(RK817_ID_BOOST_OTG_SW, "OTG_SWITCH", "vcc9",
			  RK817_POWER_EN_REG(3), ENABLE_MASK(2),
			  DISABLE_VAL(2)),
};

static const struct regulator_desc rk818_reg[] = {
	{
		.name = "DCDC_REG1",
		.supply_name = "vcc1",
		.of_match = of_match_ptr("DCDC_REG1"),
		.regulators_node = of_match_ptr("regulators"),
		.id = RK818_ID_DCDC1,
		.ops = &rk808_reg_ops,
		.type = REGULATOR_VOLTAGE,
		.min_uV = 712500,
		.uV_step = 12500,
		.n_voltages = 64,
		.vsel_reg = RK818_BUCK1_ON_VSEL_REG,
		.vsel_mask = RK818_BUCK_VSEL_MASK,
		.enable_reg = RK818_DCDC_EN_REG,
		.enable_mask = BIT(0),
		.owner = THIS_MODULE,
	}, {
		.name = "DCDC_REG2",
		.supply_name = "vcc2",
		.of_match = of_match_ptr("DCDC_REG2"),
		.regulators_node = of_match_ptr("regulators"),
		.id = RK818_ID_DCDC2,
		.ops = &rk808_reg_ops,
		.type = REGULATOR_VOLTAGE,
		.min_uV = 712500,
		.uV_step = 12500,
		.n_voltages = 64,
		.vsel_reg = RK818_BUCK2_ON_VSEL_REG,
		.vsel_mask = RK818_BUCK_VSEL_MASK,
		.enable_reg = RK818_DCDC_EN_REG,
		.enable_mask = BIT(1),
		.owner = THIS_MODULE,
	}, {
		.name = "DCDC_REG3",
		.supply_name = "vcc3",
		.of_match = of_match_ptr("DCDC_REG3"),
		.regulators_node = of_match_ptr("regulators"),
		.id = RK818_ID_DCDC3,
		.ops = &rk808_switch_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = 1,
		.enable_reg = RK818_DCDC_EN_REG,
		.enable_mask = BIT(2),
		.owner = THIS_MODULE,
	},
	RK8XX_DESC(RK818_ID_DCDC4, "DCDC_REG4", "vcc4", 1800, 3600, 100,
		RK818_BUCK4_ON_VSEL_REG, RK818_BUCK4_VSEL_MASK,
		RK818_DCDC_EN_REG, BIT(3), 0),
	RK8XX_DESC(RK818_ID_BOOST, "DCDC_BOOST", "boost", 4700, 5400, 100,
		RK818_BOOST_LDO9_ON_VSEL_REG, RK818_BOOST_ON_VSEL_MASK,
		RK818_DCDC_EN_REG, BIT(4), 0),
	RK8XX_DESC(RK818_ID_LDO1, "LDO_REG1", "vcc6", 1800, 3400, 100,
		RK818_LDO1_ON_VSEL_REG, RK818_LDO_VSEL_MASK, RK818_LDO_EN_REG,
		BIT(0), 400),
	RK8XX_DESC(RK818_ID_LDO2, "LDO_REG2", "vcc6", 1800, 3400, 100,
		RK818_LDO2_ON_VSEL_REG, RK818_LDO_VSEL_MASK, RK818_LDO_EN_REG,
		BIT(1), 400),
	{
		.name = "LDO_REG3",
		.supply_name = "vcc7",
		.of_match = of_match_ptr("LDO_REG3"),
		.regulators_node = of_match_ptr("regulators"),
		.id = RK818_ID_LDO3,
		.ops = &rk808_reg_ops_ranges,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = 16,
		.linear_ranges = rk808_ldo3_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(rk808_ldo3_voltage_ranges),
		.vsel_reg = RK818_LDO3_ON_VSEL_REG,
		.vsel_mask = RK818_LDO3_ON_VSEL_MASK,
		.enable_reg = RK818_LDO_EN_REG,
		.enable_mask = BIT(2),
		.enable_time = 400,
		.owner = THIS_MODULE,
	},
	RK8XX_DESC(RK818_ID_LDO4, "LDO_REG4", "vcc8", 1800, 3400, 100,
		RK818_LDO4_ON_VSEL_REG, RK818_LDO_VSEL_MASK, RK818_LDO_EN_REG,
		BIT(3), 400),
	RK8XX_DESC(RK818_ID_LDO5, "LDO_REG5", "vcc7", 1800, 3400, 100,
		RK818_LDO5_ON_VSEL_REG, RK818_LDO_VSEL_MASK, RK818_LDO_EN_REG,
		BIT(4), 400),
	RK8XX_DESC(RK818_ID_LDO6, "LDO_REG6", "vcc8", 800, 2500, 100,
		RK818_LDO6_ON_VSEL_REG, RK818_LDO_VSEL_MASK, RK818_LDO_EN_REG,
		BIT(5), 400),
	RK8XX_DESC(RK818_ID_LDO7, "LDO_REG7", "vcc7", 800, 2500, 100,
		RK818_LDO7_ON_VSEL_REG, RK818_LDO_VSEL_MASK, RK818_LDO_EN_REG,
		BIT(6), 400),
	RK8XX_DESC(RK818_ID_LDO8, "LDO_REG8", "vcc8", 1800, 3400, 100,
		RK818_LDO8_ON_VSEL_REG, RK818_LDO_VSEL_MASK, RK818_LDO_EN_REG,
		BIT(7), 400),
	RK8XX_DESC(RK818_ID_LDO9, "LDO_REG9", "vcc9", 1800, 3400, 100,
		RK818_BOOST_LDO9_ON_VSEL_REG, RK818_LDO_VSEL_MASK,
		RK818_DCDC_EN_REG, BIT(5), 400),
	RK8XX_DESC_SWITCH(RK818_ID_SWITCH, "SWITCH_REG", "vcc9",
		RK818_DCDC_EN_REG, BIT(6)),
	RK8XX_DESC_SWITCH(RK818_ID_HDMI_SWITCH, "HDMI_SWITCH", "h_5v",
		RK818_H5V_EN_REG, BIT(0)),
	RK8XX_DESC_SWITCH(RK818_ID_OTG_SWITCH, "OTG_SWITCH", "usb",
		RK818_DCDC_EN_REG, BIT(7)),
};

static int rk808_regulator_dt_parse_pdata(struct device *dev,
				   struct regmap *map,
				   struct rk808_regulator_data *pdata)
{
	struct device_node *np;
	int tmp, ret = 0, i;

	np = of_get_child_by_name(dev->of_node, "regulators");
	if (!np)
		return -ENXIO;

	for (i = 0; i < ARRAY_SIZE(pdata->dvs_gpio); i++) {
		pdata->dvs_gpio[i] =
			devm_gpiod_get_index_optional(dev, "dvs", i,
						      GPIOD_OUT_LOW);
		if (IS_ERR(pdata->dvs_gpio[i])) {
			ret = PTR_ERR(pdata->dvs_gpio[i]);
			dev_err(dev, "failed to get dvs%d gpio (%d)\n", i, ret);
			goto dt_parse_end;
		}

		if (!pdata->dvs_gpio[i]) {
			dev_dbg(dev, "there is no dvs%d gpio\n", i);
			continue;
		}

		tmp = i ? RK808_DVS2_POL : RK808_DVS1_POL;
		ret = regmap_update_bits(map, RK808_IO_POL_REG, tmp,
				gpiod_is_active_low(pdata->dvs_gpio[i]) ?
				0 : tmp);
	}

dt_parse_end:
	of_node_put(np);
	return ret;
}

static int rk808_regulator_probe(struct platform_device *pdev)
{
	struct rk808 *rk808 = dev_get_drvdata(pdev->dev.parent);
	struct regulator_config config = {};
	struct regulator_dev *rk808_rdev;
	struct rk808_regulator_data *pdata;
	const struct regulator_desc *regulators;
	struct regmap *regmap;
	int ret, i, nregulators;

	pdev->dev.of_node = pdev->dev.parent->of_node;
	pdev->dev.of_node_reused = true;

	regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!regmap)
		return -ENODEV;

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	switch (rk808->variant) {
	case RK805_ID:
		regulators = rk805_reg;
		nregulators = RK805_NUM_REGULATORS;
		break;
	case RK806_ID:
		regulators = rk806_reg;
		nregulators = ARRAY_SIZE(rk806_reg);
		break;
	case RK808_ID:
		/* DVS0/1 GPIOs are supported on the RK808 only */
		ret = rk808_regulator_dt_parse_pdata(&pdev->dev, regmap, pdata);
		if (ret < 0)
			return ret;

		regulators = rk808_reg;
		nregulators = RK808_NUM_REGULATORS;
		break;
	case RK809_ID:
		regulators = rk809_reg;
		nregulators = RK809_NUM_REGULATORS;
		break;
	case RK816_ID:
		regulators = rk816_reg;
		nregulators = ARRAY_SIZE(rk816_reg);
		break;
	case RK817_ID:
		regulators = rk817_reg;
		nregulators = RK817_NUM_REGULATORS;
		break;
	case RK818_ID:
		regulators = rk818_reg;
		nregulators = RK818_NUM_REGULATORS;
		break;
	default:
		dev_err(&pdev->dev, "unsupported RK8XX ID %lu\n",
			rk808->variant);
		return -EINVAL;
	}

	platform_set_drvdata(pdev, pdata);

	config.dev = &pdev->dev;
	config.driver_data = pdata;
	config.regmap = regmap;

	/* Instantiate the regulators */
	for (i = 0; i < nregulators; i++) {
		rk808_rdev = devm_regulator_register(&pdev->dev,
						     &regulators[i], &config);
		if (IS_ERR(rk808_rdev))
			return dev_err_probe(&pdev->dev, PTR_ERR(rk808_rdev),
					     "failed to register %d regulator\n", i);
	}

	return 0;
}

static struct platform_driver rk808_regulator_driver = {
	.probe = rk808_regulator_probe,
	.driver = {
		.name = "rk808-regulator",
		.probe_type = PROBE_FORCE_SYNCHRONOUS,
	},
};

module_platform_driver(rk808_regulator_driver);

MODULE_DESCRIPTION("regulator driver for the RK805/RK808/RK818 series PMICs");
MODULE_AUTHOR("Tony xie <tony.xie@rock-chips.com>");
MODULE_AUTHOR("Chris Zhong <zyw@rock-chips.com>");
MODULE_AUTHOR("Zhang Qing <zhangqing@rock-chips.com>");
MODULE_AUTHOR("Wadim Egorov <w.egorov@phytec.de>");
MODULE_AUTHOR("Xu Shengfei <xsf@rock-chips.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:rk808-regulator");
