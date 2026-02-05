// SPDX-License-Identifier: GPL-2.0+
//
// Copyright (c) 2012-2014 Samsung Electronics Co., Ltd
//              http://www.samsung.com

#include <dt-bindings/regulator/samsung,s2mpg10-regulator.h>
#include <linux/bug.h>
#include <linux/cleanup.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/mfd/samsung/core.h>
#include <linux/mfd/samsung/s2mpg10.h>
#include <linux/mfd/samsung/s2mpg11.h>
#include <linux/mfd/samsung/s2mps11.h>
#include <linux/mfd/samsung/s2mps13.h>
#include <linux/mfd/samsung/s2mps14.h>
#include <linux/mfd/samsung/s2mps15.h>
#include <linux/mfd/samsung/s2mpu02.h>
#include <linux/mfd/samsung/s2mpu05.h>

enum {
	S2MPG10_REGULATOR_OPS_STD,
	S2MPG10_REGULATOR_OPS_EXTCONTROL,
};

/* The highest number of possible regulators for supported devices. */
#define S2MPS_REGULATOR_MAX		S2MPS13_REGULATOR_MAX
struct s2mps11_info {
	int ramp_delay2;
	int ramp_delay34;
	int ramp_delay5;
	int ramp_delay16;
	int ramp_delay7810;
	int ramp_delay9;

	enum sec_device_type dev_type;

	/*
	 * One bit for each S2MPS11/S2MPS13/S2MPS14/S2MPU02 regulator whether
	 * the suspend mode was enabled.
	 */
	DECLARE_BITMAP(suspend_state, S2MPS_REGULATOR_MAX);
};

#define to_s2mpg10_regulator_desc(x) container_of((x), struct s2mpg10_regulator_desc, desc)

struct s2mpg10_regulator_desc {
	struct regulator_desc desc;

	/* Ramp rate during enable, valid for bucks only. */
	unsigned int enable_ramp_rate;

	/* Registers for external control of rail. */
	unsigned int pctrlsel_reg;
	unsigned int pctrlsel_mask;
	/* Populated from DT. */
	unsigned int pctrlsel_val;
};

static int get_ramp_delay(int ramp_delay)
{
	unsigned char cnt = 0;

	ramp_delay /= 6250;

	while (true) {
		ramp_delay = ramp_delay >> 1;
		if (ramp_delay == 0)
			break;
		cnt++;
	}

	if (cnt > 3)
		cnt = 3;

	return cnt;
}

static int s2mps11_regulator_set_voltage_time_sel(struct regulator_dev *rdev,
				   unsigned int old_selector,
				   unsigned int new_selector)
{
	struct s2mps11_info *s2mps11 = rdev_get_drvdata(rdev);
	int rdev_id = rdev_get_id(rdev);
	unsigned int ramp_delay = 0;
	int old_volt, new_volt;

	switch (rdev_id) {
	case S2MPS11_BUCK2:
		ramp_delay = s2mps11->ramp_delay2;
		break;
	case S2MPS11_BUCK3:
	case S2MPS11_BUCK4:
		ramp_delay = s2mps11->ramp_delay34;
		break;
	case S2MPS11_BUCK5:
		ramp_delay = s2mps11->ramp_delay5;
		break;
	case S2MPS11_BUCK6:
	case S2MPS11_BUCK1:
		ramp_delay = s2mps11->ramp_delay16;
		break;
	case S2MPS11_BUCK7:
	case S2MPS11_BUCK8:
	case S2MPS11_BUCK10:
		ramp_delay = s2mps11->ramp_delay7810;
		break;
	case S2MPS11_BUCK9:
		ramp_delay = s2mps11->ramp_delay9;
	}

	if (ramp_delay == 0)
		ramp_delay = rdev->desc->ramp_delay;

	old_volt = rdev->desc->min_uV + (rdev->desc->uV_step * old_selector);
	new_volt = rdev->desc->min_uV + (rdev->desc->uV_step * new_selector);

	return DIV_ROUND_UP(abs(new_volt - old_volt), ramp_delay);
}

static int s2mps11_set_ramp_delay(struct regulator_dev *rdev, int ramp_delay)
{
	struct s2mps11_info *s2mps11 = rdev_get_drvdata(rdev);
	unsigned int ramp_val, ramp_shift, ramp_reg = S2MPS11_REG_RAMP_BUCK;
	unsigned int ramp_enable = 1, enable_shift = 0;
	int rdev_id = rdev_get_id(rdev);
	int ret;

	switch (rdev_id) {
	case S2MPS11_BUCK1:
		if (ramp_delay > s2mps11->ramp_delay16)
			s2mps11->ramp_delay16 = ramp_delay;
		else
			ramp_delay = s2mps11->ramp_delay16;

		ramp_shift = S2MPS11_BUCK16_RAMP_SHIFT;
		break;
	case S2MPS11_BUCK2:
		enable_shift = S2MPS11_BUCK2_RAMP_EN_SHIFT;
		if (!ramp_delay) {
			ramp_enable = 0;
			break;
		}

		s2mps11->ramp_delay2 = ramp_delay;
		ramp_shift = S2MPS11_BUCK2_RAMP_SHIFT;
		ramp_reg = S2MPS11_REG_RAMP;
		break;
	case S2MPS11_BUCK3:
		enable_shift = S2MPS11_BUCK3_RAMP_EN_SHIFT;
		if (!ramp_delay) {
			ramp_enable = 0;
			break;
		}

		if (ramp_delay > s2mps11->ramp_delay34)
			s2mps11->ramp_delay34 = ramp_delay;
		else
			ramp_delay = s2mps11->ramp_delay34;

		ramp_shift = S2MPS11_BUCK34_RAMP_SHIFT;
		ramp_reg = S2MPS11_REG_RAMP;
		break;
	case S2MPS11_BUCK4:
		enable_shift = S2MPS11_BUCK4_RAMP_EN_SHIFT;
		if (!ramp_delay) {
			ramp_enable = 0;
			break;
		}

		if (ramp_delay > s2mps11->ramp_delay34)
			s2mps11->ramp_delay34 = ramp_delay;
		else
			ramp_delay = s2mps11->ramp_delay34;

		ramp_shift = S2MPS11_BUCK34_RAMP_SHIFT;
		ramp_reg = S2MPS11_REG_RAMP;
		break;
	case S2MPS11_BUCK5:
		s2mps11->ramp_delay5 = ramp_delay;
		ramp_shift = S2MPS11_BUCK5_RAMP_SHIFT;
		break;
	case S2MPS11_BUCK6:
		enable_shift = S2MPS11_BUCK6_RAMP_EN_SHIFT;
		if (!ramp_delay) {
			ramp_enable = 0;
			break;
		}

		if (ramp_delay > s2mps11->ramp_delay16)
			s2mps11->ramp_delay16 = ramp_delay;
		else
			ramp_delay = s2mps11->ramp_delay16;

		ramp_shift = S2MPS11_BUCK16_RAMP_SHIFT;
		break;
	case S2MPS11_BUCK7:
	case S2MPS11_BUCK8:
	case S2MPS11_BUCK10:
		if (ramp_delay > s2mps11->ramp_delay7810)
			s2mps11->ramp_delay7810 = ramp_delay;
		else
			ramp_delay = s2mps11->ramp_delay7810;

		ramp_shift = S2MPS11_BUCK7810_RAMP_SHIFT;
		break;
	case S2MPS11_BUCK9:
		s2mps11->ramp_delay9 = ramp_delay;
		ramp_shift = S2MPS11_BUCK9_RAMP_SHIFT;
		break;
	default:
		return 0;
	}

	if (!ramp_enable)
		goto ramp_disable;

	/* Ramp delay can be enabled/disabled only for buck[2346] */
	if ((rdev_id >= S2MPS11_BUCK2 && rdev_id <= S2MPS11_BUCK4) ||
	    rdev_id == S2MPS11_BUCK6)  {
		ret = regmap_update_bits(rdev->regmap, S2MPS11_REG_RAMP,
					 1 << enable_shift, 1 << enable_shift);
		if (ret) {
			dev_err(&rdev->dev, "failed to enable ramp rate\n");
			return ret;
		}
	}

	ramp_val = get_ramp_delay(ramp_delay);

	return regmap_update_bits(rdev->regmap, ramp_reg, 0x3 << ramp_shift,
				  ramp_val << ramp_shift);

ramp_disable:
	return regmap_update_bits(rdev->regmap, S2MPS11_REG_RAMP,
				  1 << enable_shift, 0);
}

static int s2mps11_regulator_enable(struct regulator_dev *rdev)
{
	struct s2mps11_info *s2mps11 = rdev_get_drvdata(rdev);
	int rdev_id = rdev_get_id(rdev);
	unsigned int val;

	switch (s2mps11->dev_type) {
	case S2MPS11X:
		if (test_bit(rdev_id, s2mps11->suspend_state))
			val = S2MPS14_ENABLE_SUSPEND;
		else
			val = rdev->desc->enable_mask;
		break;
	case S2MPS13X:
	case S2MPS14X:
		if (test_bit(rdev_id, s2mps11->suspend_state))
			val = S2MPS14_ENABLE_SUSPEND;
		else if (rdev->ena_pin)
			val = S2MPS14_ENABLE_EXT_CONTROL;
		else
			val = rdev->desc->enable_mask;
		break;
	case S2MPU02:
		if (test_bit(rdev_id, s2mps11->suspend_state))
			val = S2MPU02_ENABLE_SUSPEND;
		else
			val = rdev->desc->enable_mask;
		break;
	case S2MPU05:
		val = rdev->desc->enable_mask;
		break;
	default:
		return -EINVAL;
	}

	return regmap_update_bits(rdev->regmap, rdev->desc->enable_reg,
			rdev->desc->enable_mask, val);
}

static int s2mps11_regulator_set_suspend_disable(struct regulator_dev *rdev)
{
	int ret;
	unsigned int val, state;
	struct s2mps11_info *s2mps11 = rdev_get_drvdata(rdev);
	int rdev_id = rdev_get_id(rdev);

	/* Below LDO should be always on or does not support suspend mode. */
	switch (s2mps11->dev_type) {
	case S2MPS11X:
		switch (rdev_id) {
		case S2MPS11_LDO2:
		case S2MPS11_LDO36:
		case S2MPS11_LDO37:
		case S2MPS11_LDO38:
			return 0;
		default:
			state = S2MPS14_ENABLE_SUSPEND;
			break;
		}
		break;
	case S2MPS13X:
	case S2MPS14X:
		switch (rdev_id) {
		case S2MPS14_LDO3:
			return 0;
		default:
			state = S2MPS14_ENABLE_SUSPEND;
			break;
		}
		break;
	case S2MPU02:
		switch (rdev_id) {
		case S2MPU02_LDO13:
		case S2MPU02_LDO14:
		case S2MPU02_LDO15:
		case S2MPU02_LDO17:
		case S2MPU02_BUCK7:
			state = S2MPU02_DISABLE_SUSPEND;
			break;
		default:
			state = S2MPU02_ENABLE_SUSPEND;
			break;
		}
		break;
	default:
		return -EINVAL;
	}

	ret = regmap_read(rdev->regmap, rdev->desc->enable_reg, &val);
	if (ret < 0)
		return ret;

	set_bit(rdev_id, s2mps11->suspend_state);
	/*
	 * Don't enable suspend mode if regulator is already disabled because
	 * this would effectively for a short time turn on the regulator after
	 * resuming.
	 * However we still want to toggle the suspend_state bit for regulator
	 * in case if it got enabled before suspending the system.
	 */
	if (!(val & rdev->desc->enable_mask))
		return 0;

	return regmap_update_bits(rdev->regmap, rdev->desc->enable_reg,
				  rdev->desc->enable_mask, state);
}

static int s2mps11_of_parse_gpiod(struct device_node *np,
				  const char *con_id, bool optional,
				  const struct regulator_desc *desc,
				  struct regulator_config *config)
{
	struct gpio_desc *ena_gpiod;
	int ret;

	ena_gpiod = fwnode_gpiod_get_index(of_fwnode_handle(np), con_id, 0,
					   GPIOD_OUT_HIGH |
					   GPIOD_FLAGS_BIT_NONEXCLUSIVE,
					   desc->name
					   ? : dev_name(config->dev));
	if (IS_ERR(ena_gpiod)) {
		ret = PTR_ERR(ena_gpiod);

		/* Ignore all errors except probe defer. */
		if (ret == -EPROBE_DEFER)
			return ret;

		if (ret == -ENOENT) {
			if (optional)
				return 0;

			dev_info(config->dev,
				 "No entry for control GPIO for %d/%s in node %pOF\n",
				 desc->id, desc->name, np);
		} else {
			dev_warn_probe(config->dev, ret,
				       "Failed to get control GPIO for %d/%s in node %pOF\n",
				       desc->id, desc->name, np);
		}

		return 0;
	}

	dev_info(config->dev, "Using GPIO for ext-control over %d/%s\n",
		 desc->id, desc->name);

	config->ena_gpiod = ena_gpiod;

	return 0;
}

static int s2mps11_of_parse_cb(struct device_node *np,
			       const struct regulator_desc *desc,
			       struct regulator_config *config)
{
	const struct s2mps11_info *s2mps11 = config->driver_data;

	if (s2mps11->dev_type == S2MPS14X)
		switch (desc->id) {
		case S2MPS14_LDO10:
		case S2MPS14_LDO11:
		case S2MPS14_LDO12:
			break;

		default:
			return 0;
		}
	else
		return 0;

	return s2mps11_of_parse_gpiod(np, "samsung,ext-control", false, desc,
				      config);
}

static int s2mpg10_of_parse_cb(struct device_node *np,
			       const struct regulator_desc *desc,
			       struct regulator_config *config)
{
	const struct s2mps11_info *s2mps11 = config->driver_data;
	struct s2mpg10_regulator_desc *s2mpg10_desc = to_s2mpg10_regulator_desc(desc);
	static const u32 ext_control_s2mpg10[] = {
		[S2MPG10_EXTCTRL_PWREN] = S2MPG10_PCTRLSEL_PWREN,
		[S2MPG10_EXTCTRL_PWREN_MIF] = S2MPG10_PCTRLSEL_PWREN_MIF,
		[S2MPG10_EXTCTRL_AP_ACTIVE_N] = S2MPG10_PCTRLSEL_AP_ACTIVE_N,
		[S2MPG10_EXTCTRL_CPUCL1_EN] = S2MPG10_PCTRLSEL_CPUCL1_EN,
		[S2MPG10_EXTCTRL_CPUCL1_EN2] = S2MPG10_PCTRLSEL_CPUCL1_EN2,
		[S2MPG10_EXTCTRL_CPUCL2_EN] = S2MPG10_PCTRLSEL_CPUCL2_EN,
		[S2MPG10_EXTCTRL_CPUCL2_EN2] = S2MPG10_PCTRLSEL_CPUCL2_EN2,
		[S2MPG10_EXTCTRL_TPU_EN] = S2MPG10_PCTRLSEL_TPU_EN,
		[S2MPG10_EXTCTRL_TPU_EN2] = S2MPG10_PCTRLSEL_TPU_EN2,
		[S2MPG10_EXTCTRL_TCXO_ON] = S2MPG10_PCTRLSEL_TCXO_ON,
		[S2MPG10_EXTCTRL_TCXO_ON2] = S2MPG10_PCTRLSEL_TCXO_ON2,
		[S2MPG10_EXTCTRL_LDO20M_EN2] = S2MPG10_PCTRLSEL_LDO20M_EN2,
		[S2MPG10_EXTCTRL_LDO20M_EN] = S2MPG10_PCTRLSEL_LDO20M_EN,
	};
	static const u32 ext_control_s2mpg11[] = {
		[S2MPG11_EXTCTRL_PWREN] = S2MPG10_PCTRLSEL_PWREN,
		[S2MPG11_EXTCTRL_PWREN_MIF] = S2MPG10_PCTRLSEL_PWREN_MIF,
		[S2MPG11_EXTCTRL_AP_ACTIVE_N] = S2MPG10_PCTRLSEL_AP_ACTIVE_N,
		[S2MPG11_EXTCTRL_G3D_EN] = S2MPG10_PCTRLSEL_CPUCL1_EN,
		[S2MPG11_EXTCTRL_G3D_EN2] = S2MPG10_PCTRLSEL_CPUCL1_EN2,
		[S2MPG11_EXTCTRL_AOC_VDD] = S2MPG10_PCTRLSEL_CPUCL2_EN,
		[S2MPG11_EXTCTRL_AOC_RET] = S2MPG10_PCTRLSEL_CPUCL2_EN2,
		[S2MPG11_EXTCTRL_UFS_EN] = S2MPG10_PCTRLSEL_TPU_EN,
		[S2MPG11_EXTCTRL_LDO13S_EN] = S2MPG10_PCTRLSEL_TPU_EN2,
	};
	u32 ext_control;

	if (s2mps11->dev_type != S2MPG10 && s2mps11->dev_type != S2MPG11)
		return 0;

	if (of_property_read_u32(np, "samsung,ext-control", &ext_control))
		return 0;

	switch (s2mps11->dev_type) {
	case S2MPG10:
		switch (desc->id) {
		case S2MPG10_BUCK1 ... S2MPG10_BUCK7:
		case S2MPG10_BUCK10:
		case S2MPG10_LDO3 ... S2MPG10_LDO19:
			if (ext_control > S2MPG10_EXTCTRL_TCXO_ON2)
				return -EINVAL;
			break;

		case S2MPG10_LDO20:
			if (ext_control < S2MPG10_EXTCTRL_LDO20M_EN2 ||
			    ext_control > S2MPG10_EXTCTRL_LDO20M_EN)
				return -EINVAL;
			break;

		default:
			return -EINVAL;
		}

		if (ext_control > ARRAY_SIZE(ext_control_s2mpg10))
			return -EINVAL;
		ext_control = ext_control_s2mpg10[ext_control];
		break;

	case S2MPG11:
		switch (desc->id) {
		case S2MPG11_BUCK1 ... S2MPG11_BUCK3:
		case S2MPG11_BUCK5:
		case S2MPG11_BUCK8:
		case S2MPG11_BUCK9:
		case S2MPG11_BUCKD:
		case S2MPG11_BUCKA:
		case S2MPG11_LDO1:
		case S2MPG11_LDO2:
		case S2MPG11_LDO8:
		case S2MPG11_LDO13:
			if (ext_control > S2MPG11_EXTCTRL_LDO13S_EN)
				return -EINVAL;
			break;

		default:
			return -EINVAL;
		}

		if (ext_control > ARRAY_SIZE(ext_control_s2mpg11))
			return -EINVAL;
		ext_control = ext_control_s2mpg11[ext_control];
		break;

	default:
		return -EINVAL;
	}

	/*
	 * If the regulator should be configured for external control, then:
	 * 1) the PCTRLSELx register needs to be set accordingly
	 * 2) regulator_desc::enable_val needs to be:
	 *    a) updated and
	 *    b) written to the hardware
	 * 3) we switch to the ::ops that provide an empty ::enable() and no
	 *    ::disable() implementations
	 *
	 * Points 1) and 2b) will be handled in _probe(), after
	 * devm_regulator_register() returns, so that we can properly act on
	 * failures, since the regulator core ignores most return values from
	 * this parse callback.
	 */
	s2mpg10_desc->pctrlsel_val = ext_control;
	s2mpg10_desc->pctrlsel_val <<= (ffs(s2mpg10_desc->pctrlsel_mask) - 1);

	s2mpg10_desc->desc.enable_val = S2MPG10_PMIC_CTRL_ENABLE_EXT;
	s2mpg10_desc->desc.enable_val <<= (ffs(desc->enable_mask) - 1);

	++s2mpg10_desc->desc.ops;

	return s2mps11_of_parse_gpiod(np, "enable", true, desc, config);
}

static int s2mpg10_enable_ext_control(struct s2mps11_info *s2mps11,
				      struct regulator_dev *rdev)
{
	const struct s2mpg10_regulator_desc *s2mpg10_desc;
	int ret;

	switch (s2mps11->dev_type) {
	case S2MPG10:
	case S2MPG11:
		s2mpg10_desc = to_s2mpg10_regulator_desc(rdev->desc);
		break;

	default:
		return 0;
	}

	ret = regmap_update_bits(rdev_get_regmap(rdev),
				 s2mpg10_desc->pctrlsel_reg,
				 s2mpg10_desc->pctrlsel_mask,
				 s2mpg10_desc->pctrlsel_val);
	if (ret)
		return dev_err_probe(rdev_get_dev(rdev), ret,
				     "failed to configure pctrlsel for %s\n",
				     rdev->desc->name);

	/*
	 * When using external control, the enable bit of the regulator still
	 * needs to be set. The actual state will still be determined by the
	 * external signal.
	 */
	ret = regulator_enable_regmap(rdev);
	if (ret)
		return dev_err_probe(rdev_get_dev(rdev), ret,
				     "failed to enable regulator %s\n",
				     rdev->desc->name);

	return 0;
}

static int s2mpg10_regulator_enable_nop(struct regulator_dev *rdev)
{
	/*
	 * We need to provide this, otherwise the regulator core's enable on
	 * this regulator will return a failure and subsequently disable our
	 * parent regulator.
	 */
	return 0;
}

static int s2mpg10_regulator_buck_enable_time(struct regulator_dev *rdev)
{
	const struct s2mpg10_regulator_desc * const s2mpg10_desc =
		to_s2mpg10_regulator_desc(rdev->desc);
	const struct regulator_ops * const ops = rdev->desc->ops;
	int vsel, curr_uV;

	vsel = ops->get_voltage_sel(rdev);
	if (vsel < 0)
		return vsel;

	curr_uV = ops->list_voltage(rdev, vsel);
	if (curr_uV < 0)
		return curr_uV;

	return (rdev->desc->enable_time
		+ DIV_ROUND_UP(curr_uV, s2mpg10_desc->enable_ramp_rate));
}

static int s2mpg1x_regulator_buck_set_voltage_time(struct regulator_dev *rdev,
						   int old_uV, int new_uV,
						   unsigned int ramp_reg,
						   unsigned int ramp_mask)
{
	unsigned int ramp_sel, ramp_rate;
	int ret;

	if (old_uV == new_uV)
		return 0;

	ret = regmap_read(rdev->regmap, ramp_reg, &ramp_sel);
	if (ret)
		return ret;

	ramp_sel &= ramp_mask;
	ramp_sel >>= ffs(ramp_mask) - 1;
	if (ramp_sel >= rdev->desc->n_ramp_values ||
	    !rdev->desc->ramp_delay_table)
		return -EINVAL;

	ramp_rate = rdev->desc->ramp_delay_table[ramp_sel];

	return DIV_ROUND_UP(abs(new_uV - old_uV), ramp_rate);
}

static int s2mpg10_regulator_buck_set_voltage_time(struct regulator_dev *rdev,
						   int old_uV, int new_uV)
{
	unsigned int ramp_reg;

	ramp_reg = rdev->desc->ramp_reg;
	if (old_uV > new_uV)
		/* The downwards ramp is at a different offset. */
		ramp_reg += S2MPG10_PMIC_DVS_RAMP4 - S2MPG10_PMIC_DVS_RAMP1;

	return s2mpg1x_regulator_buck_set_voltage_time(rdev, old_uV, new_uV,
						       ramp_reg,
						       rdev->desc->ramp_mask);
}

static int s2mpg11_regulator_buck_set_voltage_time(struct regulator_dev *rdev,
						   int old_uV, int new_uV)
{
	unsigned int ramp_mask;

	ramp_mask = rdev->desc->ramp_mask;
	if (old_uV > new_uV)
		/* The downwards mask is at a different position. */
		ramp_mask >>= 2;

	return s2mpg1x_regulator_buck_set_voltage_time(rdev, old_uV, new_uV,
						       rdev->desc->ramp_reg,
						       ramp_mask);
}

/*
 * We assign both, ::set_voltage_time() and ::set_voltage_time_sel(), because
 * only if the latter is != NULL, the regulator core will call neither during
 * DVS if the regulator is disabled. If the latter is NULL, the core always
 * calls the ::set_voltage_time() callback, which would give incorrect results
 * if the regulator is off.
 * At the same time, we do need ::set_voltage_time() due to differing upwards
 * and downwards ramps and we can not make that code dependent on the regulator
 * enable state, as that would break regulator_set_voltage_time() which
 * expects a correct result no matter the enable state.
 */
static const struct regulator_ops s2mpg10_reg_buck_ops[] = {
	[S2MPG10_REGULATOR_OPS_STD] = {
		.list_voltage		= regulator_list_voltage_linear_range,
		.map_voltage		= regulator_map_voltage_linear_range,
		.is_enabled		= regulator_is_enabled_regmap,
		.enable			= regulator_enable_regmap,
		.disable		= regulator_disable_regmap,
		.enable_time		= s2mpg10_regulator_buck_enable_time,
		.get_voltage_sel	= regulator_get_voltage_sel_regmap,
		.set_voltage_sel	= regulator_set_voltage_sel_regmap,
		.set_voltage_time	= s2mpg10_regulator_buck_set_voltage_time,
		.set_voltage_time_sel	= regulator_set_voltage_time_sel,
		.set_ramp_delay		= regulator_set_ramp_delay_regmap,
	},
	[S2MPG10_REGULATOR_OPS_EXTCONTROL] = {
		.list_voltage		= regulator_list_voltage_linear_range,
		.map_voltage		= regulator_map_voltage_linear_range,
		.enable			= s2mpg10_regulator_enable_nop,
		.get_voltage_sel	= regulator_get_voltage_sel_regmap,
		.set_voltage_sel	= regulator_set_voltage_sel_regmap,
		.set_voltage_time	= s2mpg10_regulator_buck_set_voltage_time,
		.set_voltage_time_sel	= regulator_set_voltage_time_sel,
		.set_ramp_delay		= regulator_set_ramp_delay_regmap,
	}
};

#define s2mpg10_buck_to_ramp_mask(n) (GENMASK(1, 0) << (((n) % 4) * 2))

/*
 * The ramp_delay during enable is fixed (12.5mV/μs), while the ramp during
 * DVS can be adjusted. Linux can adjust the ramp delay via DT, in which case
 * the regulator core will modify the regulator's constraints and call our
 * .set_ramp_delay() which updates the DVS ramp in ramp_reg.
 * For enable, our .enable_time() unconditionally uses enable_ramp_rate
 * (12.5mV/μs) while our ::set_voltage_time() takes the value in ramp_reg
 * into account.
 */
#define regulator_desc_s2mpg1x_buck_cmn(_name, _id, _supply, _ops,	\
		_vrange, _vsel_reg, _vsel_mask, _en_reg, _en_mask,	\
		_r_reg, _r_mask, _r_table, _r_table_sz,			\
		_en_time) {						\
	.name		= "buck" _name,					\
	.supply_name	= _supply,					\
	.of_match	= of_match_ptr("buck" _name),			\
	.regulators_node = of_match_ptr("regulators"),			\
	.of_parse_cb	= s2mpg10_of_parse_cb,				\
	.id		= _id,						\
	.ops		= &(_ops)[0],					\
	.type		= REGULATOR_VOLTAGE,				\
	.owner		= THIS_MODULE,					\
	.linear_ranges	= _vrange,					\
	.n_linear_ranges = ARRAY_SIZE(_vrange),				\
	.n_voltages	= _vrange##_count,				\
	.vsel_reg	= _vsel_reg,					\
	.vsel_mask	= _vsel_mask,					\
	.enable_reg	= _en_reg,					\
	.enable_mask	= _en_mask,					\
	.ramp_reg	= _r_reg,					\
	.ramp_mask	= _r_mask,					\
	.ramp_delay_table = _r_table,					\
	.n_ramp_values	= _r_table_sz,					\
	.enable_time	= _en_time, /* + V/enable_ramp_rate */		\
}

#define regulator_desc_s2mpg10_buck(_num, _vrange, _r_reg)		\
	regulator_desc_s2mpg1x_buck_cmn(#_num "m", S2MPG10_BUCK##_num,	\
		"vinb"#_num "m", s2mpg10_reg_buck_ops, _vrange,		\
		S2MPG10_PMIC_B##_num##M_OUT1, GENMASK(7, 0),		\
		S2MPG10_PMIC_B##_num##M_CTRL, GENMASK(7, 6),		\
		S2MPG10_PMIC_##_r_reg,					\
		s2mpg10_buck_to_ramp_mask(S2MPG10_BUCK##_num		\
					  - S2MPG10_BUCK1),		\
		s2mpg10_buck_ramp_table,				\
		ARRAY_SIZE(s2mpg10_buck_ramp_table), 30)

#define s2mpg10_regulator_desc_buck_cm(_num, _vrange, _r_reg)		\
	.desc = regulator_desc_s2mpg10_buck(_num, _vrange, _r_reg),	\
	.enable_ramp_rate = 12500

#define s2mpg10_regulator_desc_buck_gpio(_num, _vrange, _r_reg,		\
					 _pc_reg, _pc_mask)		\
	[S2MPG10_BUCK##_num] = {					\
		s2mpg10_regulator_desc_buck_cm(_num, _vrange, _r_reg),	\
		.pctrlsel_reg = S2MPG10_PMIC_##_pc_reg,			\
		.pctrlsel_mask = _pc_mask,				\
	}

#define s2mpg10_regulator_desc_buck(_num, _vrange, _r_reg)		\
	[S2MPG10_BUCK##_num] = {					\
		s2mpg10_regulator_desc_buck_cm(_num, _vrange, _r_reg),	\
	}

/* ops for S2MPG1x LDO regulators without ramp control */
static const struct regulator_ops s2mpg10_reg_ldo_ops[] = {
	[S2MPG10_REGULATOR_OPS_STD] = {
		.list_voltage		= regulator_list_voltage_linear_range,
		.map_voltage		= regulator_map_voltage_linear_range,
		.is_enabled		= regulator_is_enabled_regmap,
		.enable			= regulator_enable_regmap,
		.disable		= regulator_disable_regmap,
		.get_voltage_sel	= regulator_get_voltage_sel_regmap,
		.set_voltage_sel	= regulator_set_voltage_sel_regmap,
		.set_voltage_time_sel	= regulator_set_voltage_time_sel,
	},
	[S2MPG10_REGULATOR_OPS_EXTCONTROL] = {
		.list_voltage		= regulator_list_voltage_linear_range,
		.map_voltage		= regulator_map_voltage_linear_range,
		.enable			= s2mpg10_regulator_enable_nop,
		.get_voltage_sel	= regulator_get_voltage_sel_regmap,
		.set_voltage_sel	= regulator_set_voltage_sel_regmap,
		.set_voltage_time_sel	= regulator_set_voltage_time_sel,
	}
};

/* ops for S2MPG1x LDO regulators that have ramp control */
static const struct regulator_ops s2mpg10_reg_ldo_ramp_ops[] = {
	[S2MPG10_REGULATOR_OPS_STD] = {
		.list_voltage		= regulator_list_voltage_linear_range,
		.map_voltage		= regulator_map_voltage_linear_range,
		.is_enabled		= regulator_is_enabled_regmap,
		.enable			= regulator_enable_regmap,
		.disable		= regulator_disable_regmap,
		.get_voltage_sel	= regulator_get_voltage_sel_regmap,
		.set_voltage_sel	= regulator_set_voltage_sel_regmap,
		.set_voltage_time_sel	= regulator_set_voltage_time_sel,
		.set_ramp_delay		= regulator_set_ramp_delay_regmap,
	},
	[S2MPG10_REGULATOR_OPS_EXTCONTROL] = {
		.list_voltage		= regulator_list_voltage_linear_range,
		.map_voltage		= regulator_map_voltage_linear_range,
		.enable			= s2mpg10_regulator_enable_nop,
		.get_voltage_sel	= regulator_get_voltage_sel_regmap,
		.set_voltage_sel	= regulator_set_voltage_sel_regmap,
		.set_voltage_time_sel	= regulator_set_voltage_time_sel,
		.set_ramp_delay		= regulator_set_ramp_delay_regmap,
	}
};

#define regulator_desc_s2mpg1x_ldo_cmn(_name, _id, _supply, _ops,	\
		_vrange, _vsel_reg, _vsel_mask, _en_reg, _en_mask,	\
		_ramp_delay, _r_reg, _r_mask, _r_table,	_r_table_sz) {	\
	.name		= "ldo" _name,					\
	.supply_name	= _supply,					\
	.of_match	= of_match_ptr("ldo" _name),			\
	.regulators_node = of_match_ptr("regulators"),			\
	.of_parse_cb	= s2mpg10_of_parse_cb,				\
	.id		= _id,						\
	.ops		= &(_ops)[0],					\
	.type		= REGULATOR_VOLTAGE,				\
	.owner		= THIS_MODULE,					\
	.linear_ranges	= _vrange,					\
	.n_linear_ranges = ARRAY_SIZE(_vrange),				\
	.n_voltages	= _vrange##_count,				\
	.vsel_reg	= _vsel_reg,					\
	.vsel_mask	= _vsel_mask,					\
	.enable_reg	= _en_reg,					\
	.enable_mask	= _en_mask,					\
	.ramp_delay	= _ramp_delay,					\
	.ramp_reg	= _r_reg,					\
	.ramp_mask	= _r_mask,					\
	.ramp_delay_table = _r_table,					\
	.n_ramp_values	= _r_table_sz,					\
	.enable_time	= 130, /* startup 20+-10 + ramp 30..100μs */	\
}

#define s2mpg10_regulator_desc_ldo_cmn(_num, _supply, _ops, _vrange,	\
		_vsel_reg_sfx, _vsel_mask, _en_reg, _en_mask,		\
		_ramp_delay, _r_reg, _r_mask, _r_table,	_r_table_sz,	\
		_pc_reg, _pc_mask)					\
	[S2MPG10_LDO##_num] = {						\
		.desc = regulator_desc_s2mpg1x_ldo_cmn(#_num "m",	\
				S2MPG10_LDO##_num, _supply, _ops,	\
				_vrange,				\
				S2MPG10_PMIC_L##_num##M_##_vsel_reg_sfx, \
				_vsel_mask,				\
				S2MPG10_PMIC_##_en_reg, _en_mask,	\
				_ramp_delay, _r_reg, _r_mask, _r_table,	\
				_r_table_sz),				\
		.pctrlsel_reg = _pc_reg,				\
		.pctrlsel_mask = _pc_mask,				\
	}

/* standard LDO via LxM_CTRL */
#define s2mpg10_regulator_desc_ldo(_num, _supply, _vrange)		\
	s2mpg10_regulator_desc_ldo_cmn(_num, _supply,			\
		s2mpg10_reg_ldo_ops, _vrange, CTRL, GENMASK(5, 0),	\
		L##_num##M_CTRL, BIT(7),				\
		0, 0, 0, NULL, 0,					\
		0, 0)

/* standard LDO but possibly GPIO controlled */
#define s2mpg10_regulator_desc_ldo_gpio(_num, _supply, _vrange,		\
					_pc_reg, _pc_mask)		\
	s2mpg10_regulator_desc_ldo_cmn(_num, _supply,			\
		s2mpg10_reg_ldo_ops, _vrange, CTRL, GENMASK(5, 0),	\
		L##_num##M_CTRL, GENMASK(7, 6),				\
		0, 0, 0, NULL, 0,					\
		S2MPG10_PMIC_##_pc_reg, _pc_mask)

/* LDO with ramp support and possibly GPIO controlled */
#define s2mpg10_regulator_desc_ldo_ramp(_num, _supply, _vrange,		\
		_en_mask, _r_reg, _pc_reg, _pc_mask)			\
	s2mpg10_regulator_desc_ldo_cmn(_num, _supply,			\
		s2mpg10_reg_ldo_ramp_ops, _vrange, CTRL1, GENMASK(6, 0), \
		LDO_CTRL2, _en_mask,					\
		6250, S2MPG10_PMIC_##_r_reg, GENMASK(1, 0),		\
		s2mpg10_ldo_ramp_table,					\
		ARRAY_SIZE(s2mpg10_ldo_ramp_table),			\
		S2MPG10_PMIC_##_pc_reg, _pc_mask)

#define S2MPG10_VOLTAGE_RANGE(_prefix, _idx, _offs_uV, _min_uV,		\
			      _max_uV, _step_uV)			\
static const struct linear_range _prefix##_vranges##_idx[] = {		\
	REGULATOR_LINEAR_VRANGE(_offs_uV, _min_uV, _max_uV, _step_uV)	\
};									\
static const unsigned int _prefix##_vranges##_idx##_count =		\
	((((_max_uV) - (_offs_uV)) / (_step_uV)) + 1)

/* voltage range for s2mpg10 BUCK 1, 2, 3, 4, 5, 7, 8, 9, 10 */
S2MPG10_VOLTAGE_RANGE(s2mpg10_buck, 1, 200000, 450000, 1300000, STEP_6_25_MV);

/* voltage range for s2mpg10 BUCK 6 */
S2MPG10_VOLTAGE_RANGE(s2mpg10_buck, 6, 200000, 450000, 1350000, STEP_6_25_MV);

static const unsigned int s2mpg10_buck_ramp_table[] = {
	6250, 12500, 25000
};

/* voltage range for s2mpg10 LDO 1, 11, 12 */
S2MPG10_VOLTAGE_RANGE(s2mpg10_ldo, 1, 300000, 700000, 1300000, STEP_12_5_MV);

/* voltage range for s2mpg10 LDO 2, 4, 9, 14, 18, 19, 20, 23, 25, 29, 30, 31 */
S2MPG10_VOLTAGE_RANGE(s2mpg10_ldo, 2, 700000, 1600000, 1950000, STEP_25_MV);

/* voltage range for s2mpg10 LDO 3, 5, 6, 8, 16, 17, 24, 28 */
S2MPG10_VOLTAGE_RANGE(s2mpg10_ldo, 3, 725000, 725000, 1300000, STEP_12_5_MV);

/* voltage range for s2mpg10 LDO 7 */
S2MPG10_VOLTAGE_RANGE(s2mpg10_ldo, 7, 300000, 450000, 1300000, STEP_12_5_MV);

/* voltage range for s2mpg10 LDO 13, 15 */
S2MPG10_VOLTAGE_RANGE(s2mpg10_ldo, 13, 300000, 450000, 950000, STEP_12_5_MV);

/* voltage range for s2mpg10 LDO 10 */
S2MPG10_VOLTAGE_RANGE(s2mpg10_ldo, 10, 1800000, 1800000, 3350000, STEP_25_MV);

/* voltage range for s2mpg10 LDO 21, 22, 26, 27 */
S2MPG10_VOLTAGE_RANGE(s2mpg10_ldo, 21, 1800000, 2500000, 3300000, STEP_25_MV);

/* possible ramp values for s2mpg10 LDO 1, 7, 11, 12, 13, 15 */
static const unsigned int s2mpg10_ldo_ramp_table[] = {
	6250, 12500
};

static const struct s2mpg10_regulator_desc s2mpg10_regulators[] = {
	s2mpg10_regulator_desc_buck_gpio(1, s2mpg10_buck_vranges1, DVS_RAMP1,
					 PCTRLSEL1, GENMASK(3, 0)),
	s2mpg10_regulator_desc_buck_gpio(2, s2mpg10_buck_vranges1, DVS_RAMP1,
					 PCTRLSEL1, GENMASK(7, 4)),
	s2mpg10_regulator_desc_buck_gpio(3, s2mpg10_buck_vranges1, DVS_RAMP1,
					 PCTRLSEL2, GENMASK(3, 0)),
	s2mpg10_regulator_desc_buck_gpio(4, s2mpg10_buck_vranges1, DVS_RAMP1,
					 PCTRLSEL2, GENMASK(7, 4)),
	s2mpg10_regulator_desc_buck_gpio(5, s2mpg10_buck_vranges1, DVS_RAMP2,
					 PCTRLSEL3, GENMASK(3, 0)),
	s2mpg10_regulator_desc_buck_gpio(6, s2mpg10_buck_vranges6, DVS_RAMP2,
					 PCTRLSEL3, GENMASK(7, 4)),
	s2mpg10_regulator_desc_buck_gpio(7, s2mpg10_buck_vranges1, DVS_RAMP2,
					 PCTRLSEL4, GENMASK(3, 0)),
	s2mpg10_regulator_desc_buck(8, s2mpg10_buck_vranges1, DVS_RAMP2),
	s2mpg10_regulator_desc_buck(9, s2mpg10_buck_vranges1, DVS_RAMP3),
	s2mpg10_regulator_desc_buck_gpio(10, s2mpg10_buck_vranges1, DVS_RAMP3,
					 PCTRLSEL4, GENMASK(7, 4)),
	/*
	 * Standard LDO via LxM_CTRL but non-standard (greater) V-range and with
	 * ramp support.
	 */
	s2mpg10_regulator_desc_ldo_cmn(1, "vinl3m", s2mpg10_reg_ldo_ramp_ops,
				       s2mpg10_ldo_vranges1,
				       CTRL, GENMASK(6, 0),
				       L1M_CTRL, BIT(7),
				       6250, S2MPG10_PMIC_DVS_RAMP6,
				       GENMASK(5, 4), s2mpg10_ldo_ramp_table,
				       ARRAY_SIZE(s2mpg10_ldo_ramp_table),
				       0, 0),
	s2mpg10_regulator_desc_ldo(2, "vinl9m", s2mpg10_ldo_vranges2),
	s2mpg10_regulator_desc_ldo_gpio(3, "vinl4m", s2mpg10_ldo_vranges3,
					PCTRLSEL5, GENMASK(3, 0)),
	s2mpg10_regulator_desc_ldo_gpio(4, "vinl9m", s2mpg10_ldo_vranges2,
					PCTRLSEL5, GENMASK(7, 4)),
	s2mpg10_regulator_desc_ldo_gpio(5, "vinl3m", s2mpg10_ldo_vranges3,
					PCTRLSEL6, GENMASK(3, 0)),
	s2mpg10_regulator_desc_ldo_gpio(6, "vinl7m", s2mpg10_ldo_vranges3,
					PCTRLSEL6, GENMASK(7, 4)),
	/*
	 * Ramp support, possibly GPIO controlled, non-standard (greater) V-
	 * range and enable reg & mask.
	 */
	s2mpg10_regulator_desc_ldo_cmn(7, "vinl3m", s2mpg10_reg_ldo_ramp_ops,
				       s2mpg10_ldo_vranges7,
				       CTRL, GENMASK(6, 0),
				       LDO_CTRL1, GENMASK(4, 3),
				       6250, S2MPG10_PMIC_DVS_RAMP6,
				       GENMASK(7, 6), s2mpg10_ldo_ramp_table,
				       ARRAY_SIZE(s2mpg10_ldo_ramp_table),
				       S2MPG10_PMIC_PCTRLSEL7, GENMASK(3, 0)),
	s2mpg10_regulator_desc_ldo_gpio(8, "vinl4m", s2mpg10_ldo_vranges3,
					PCTRLSEL7, GENMASK(7, 4)),
	s2mpg10_regulator_desc_ldo_gpio(9, "vinl10m", s2mpg10_ldo_vranges2,
					PCTRLSEL8, GENMASK(3, 0)),
	s2mpg10_regulator_desc_ldo_gpio(10, "vinl15m", s2mpg10_ldo_vranges10,
					PCTRLSEL8, GENMASK(7, 4)),
	s2mpg10_regulator_desc_ldo_ramp(11, "vinl7m", s2mpg10_ldo_vranges1,
					GENMASK(1, 0), DVS_SYNC_CTRL3,
					PCTRLSEL9, GENMASK(3, 0)),
	s2mpg10_regulator_desc_ldo_ramp(12, "vinl8m", s2mpg10_ldo_vranges1,
					GENMASK(3, 2), DVS_SYNC_CTRL4,
					PCTRLSEL9, GENMASK(7, 4)),
	s2mpg10_regulator_desc_ldo_ramp(13, "vinl1m", s2mpg10_ldo_vranges13,
					GENMASK(5, 4), DVS_SYNC_CTRL5,
					PCTRLSEL10, GENMASK(3, 0)),
	s2mpg10_regulator_desc_ldo_gpio(14, "vinl10m", s2mpg10_ldo_vranges2,
					PCTRLSEL10, GENMASK(7, 4)),
	s2mpg10_regulator_desc_ldo_ramp(15, "vinl2m", s2mpg10_ldo_vranges13,
					GENMASK(7, 6), DVS_SYNC_CTRL6,
					PCTRLSEL11, GENMASK(3, 0)),
	s2mpg10_regulator_desc_ldo_gpio(16, "vinl5m", s2mpg10_ldo_vranges3,
					PCTRLSEL11, GENMASK(7, 4)),
	s2mpg10_regulator_desc_ldo_gpio(17, "vinl6m", s2mpg10_ldo_vranges3,
					PCTRLSEL12, GENMASK(3, 0)),
	s2mpg10_regulator_desc_ldo_gpio(18, "vinl10m", s2mpg10_ldo_vranges2,
					PCTRLSEL12, GENMASK(7, 4)),
	s2mpg10_regulator_desc_ldo_gpio(19, "vinl10m", s2mpg10_ldo_vranges2,
					PCTRLSEL13, GENMASK(3, 0)),
	s2mpg10_regulator_desc_ldo_gpio(20, "vinl10m", s2mpg10_ldo_vranges2,
					PCTRLSEL13, GENMASK(7, 4)),
	s2mpg10_regulator_desc_ldo(21, "vinl14m", s2mpg10_ldo_vranges21),
	s2mpg10_regulator_desc_ldo(22, "vinl15m", s2mpg10_ldo_vranges21),
	s2mpg10_regulator_desc_ldo(23, "vinl11m", s2mpg10_ldo_vranges2),
	s2mpg10_regulator_desc_ldo(24, "vinl7m", s2mpg10_ldo_vranges3),
	s2mpg10_regulator_desc_ldo(25, "vinl10m", s2mpg10_ldo_vranges2),
	s2mpg10_regulator_desc_ldo(26, "vinl15m", s2mpg10_ldo_vranges21),
	s2mpg10_regulator_desc_ldo(27, "vinl15m", s2mpg10_ldo_vranges21),
	s2mpg10_regulator_desc_ldo(28, "vinl7m", s2mpg10_ldo_vranges3),
	s2mpg10_regulator_desc_ldo(29, "vinl12m", s2mpg10_ldo_vranges2),
	s2mpg10_regulator_desc_ldo(30, "vinl13m", s2mpg10_ldo_vranges2),
	s2mpg10_regulator_desc_ldo(31, "vinl11m", s2mpg10_ldo_vranges2)
};

static const struct regulator_ops s2mpg11_reg_buck_ops[] = {
	[S2MPG10_REGULATOR_OPS_STD] = {
		.list_voltage		= regulator_list_voltage_linear_range,
		.map_voltage		= regulator_map_voltage_linear_range,
		.is_enabled		= regulator_is_enabled_regmap,
		.enable			= regulator_enable_regmap,
		.disable		= regulator_disable_regmap,
		.get_voltage_sel	= regulator_get_voltage_sel_regmap,
		.set_voltage_sel	= regulator_set_voltage_sel_regmap,
		.set_voltage_time	= s2mpg11_regulator_buck_set_voltage_time,
		.set_voltage_time_sel	= regulator_set_voltage_time_sel,
		.enable_time		= s2mpg10_regulator_buck_enable_time,
		.set_ramp_delay		= regulator_set_ramp_delay_regmap,
	},
	[S2MPG10_REGULATOR_OPS_EXTCONTROL] = {
		.list_voltage		= regulator_list_voltage_linear_range,
		.map_voltage		= regulator_map_voltage_linear_range,
		.enable			= s2mpg10_regulator_enable_nop,
		.get_voltage_sel	= regulator_get_voltage_sel_regmap,
		.set_voltage_sel	= regulator_set_voltage_sel_regmap,
		.set_voltage_time	= s2mpg11_regulator_buck_set_voltage_time,
		.set_voltage_time_sel	= regulator_set_voltage_time_sel,
		.enable_time		= s2mpg10_regulator_buck_enable_time,
		.set_ramp_delay		= regulator_set_ramp_delay_regmap,
	}
};

#define s2mpg11_buck_to_ramp_mask(n) (GENMASK(3, 2) << (((n) % 2) * 4))

#define regulator_desc_s2mpg11_buckx(_name, _id, _supply, _vrange,	\
		_vsel_reg, _en_reg, _en_mask, _r_reg)			\
	regulator_desc_s2mpg1x_buck_cmn(_name, _id, _supply,		\
		s2mpg11_reg_buck_ops,  _vrange,				\
		S2MPG11_PMIC_##_vsel_reg, GENMASK(7, 0),		\
		S2MPG11_PMIC_##_en_reg, _en_mask,			\
		S2MPG11_PMIC_##_r_reg,					\
		s2mpg11_buck_to_ramp_mask(_id - S2MPG11_BUCK1),		\
		s2mpg10_buck_ramp_table,				\
		ARRAY_SIZE(s2mpg10_buck_ramp_table), 30)

#define s2mpg11_regulator_desc_buck_xm(_num, _vrange, _vsel_reg_sfx,	\
				       _en_mask, _r_reg, _en_rrate)	\
	.desc = regulator_desc_s2mpg11_buckx(#_num"s",			\
				S2MPG11_BUCK##_num, "vinb"#_num"s",	\
				_vrange,				\
				B##_num##S_##_vsel_reg_sfx,		\
				B##_num##S_CTRL, _en_mask,		\
				_r_reg),				\
	.enable_ramp_rate = _en_rrate

#define s2mpg11_regulator_desc_buck_cm(_num, _vrange, _vsel_reg_sfx,	\
				       _en_mask, _r_reg)		\
	[S2MPG11_BUCK##_num] = {					\
		s2mpg11_regulator_desc_buck_xm(_num, _vrange,		\
			_vsel_reg_sfx, _en_mask, _r_reg, 12500),	\
	}

#define s2mpg11_regulator_desc_buckn_cm_gpio(_num, _vrange,		\
		_vsel_reg_sfx, _en_mask, _r_reg, _pc_reg, _pc_mask)	\
	[S2MPG11_BUCK##_num] = {					\
		s2mpg11_regulator_desc_buck_xm(_num, _vrange,		\
			_vsel_reg_sfx, _en_mask, _r_reg, 12500),	\
		.pctrlsel_reg = S2MPG11_PMIC_##_pc_reg,			\
		.pctrlsel_mask = _pc_mask,				\
	}

#define s2mpg11_regulator_desc_buck_vm(_num, _vrange, _vsel_reg_sfx,	\
				       _en_mask, _r_reg)		\
	[S2MPG11_BUCK##_num] = {					\
		s2mpg11_regulator_desc_buck_xm(_num, _vrange,		\
			_vsel_reg_sfx, _en_mask, _r_reg, 25000),	\
	}

#define s2mpg11_regulator_desc_bucka(_num, _num_lower, _r_reg,		\
				     _pc_reg, _pc_mask)			\
	[S2MPG11_BUCK##_num] = {					\
		.desc = regulator_desc_s2mpg11_buckx(#_num_lower,	\
				S2MPG11_BUCK##_num, "vinb"#_num_lower,	\
				s2mpg11_buck_vranges##_num_lower,	\
				BUCK##_num##_OUT,			\
				BUCK##_num##_CTRL, GENMASK(7, 6),	\
				_r_reg),				\
		.enable_ramp_rate = 25000,				\
		.pctrlsel_reg = S2MPG11_PMIC_##_pc_reg,			\
		.pctrlsel_mask = _pc_mask,				\
	}

#define s2mpg11_regulator_desc_buckboost()				\
	[S2MPG11_BUCKBOOST] = {						\
		.desc = regulator_desc_s2mpg1x_buck_cmn("boost",	\
				S2MPG11_BUCKBOOST, "vinbb",		\
				s2mpg10_reg_ldo_ops,			\
				s2mpg11_buck_vrangesboost,		\
				S2MPG11_PMIC_BB_OUT1, GENMASK(6, 0),	\
				S2MPG11_PMIC_BB_CTRL, BIT(7),		\
				0, 0, NULL, 0, 35),			\
		.enable_ramp_rate = 17500,				\
	}

#define s2mpg11_regulator_desc_ldo_cmn(_num, _supply, _ops,		\
		_vrange, _vsel_reg_sfx, _vsel_mask, _en_reg, _en_mask,	\
		_ramp_delay, _r_reg, _r_mask, _r_table, _r_table_sz,	\
		_pc_reg, _pc_mask)					\
	[S2MPG11_LDO##_num] = {						\
		.desc = regulator_desc_s2mpg1x_ldo_cmn(#_num "s",	\
				S2MPG11_LDO##_num, _supply, _ops,	\
				_vrange,				\
				S2MPG11_PMIC_L##_num##S_##_vsel_reg_sfx, \
				_vsel_mask,				\
				S2MPG11_PMIC_##_en_reg, _en_mask,	\
				_ramp_delay, _r_reg, _r_mask, _r_table,	\
				_r_table_sz),				\
		.pctrlsel_reg = _pc_reg,				\
		.pctrlsel_mask = _pc_mask,				\
	}

/* standard LDO via LxM_CTRL */
#define s2mpg11_regulator_desc_ldo(_num, _supply, _vrange)		\
	s2mpg11_regulator_desc_ldo_cmn(_num, _supply,			\
		s2mpg10_reg_ldo_ops, _vrange, CTRL, GENMASK(5, 0),	\
		L##_num##S_CTRL, BIT(7),				\
		0, 0, 0, NULL, 0,					\
		0, 0)

/* standard LDO but possibly GPIO controlled */
#define s2mpg11_regulator_desc_ldo_gpio(_num, _supply, _vrange,		\
					_pc_reg, _pc_mask)		\
	s2mpg11_regulator_desc_ldo_cmn(_num, _supply,			\
		s2mpg10_reg_ldo_ops, _vrange, CTRL, GENMASK(5, 0),	\
		L##_num##S_CTRL, GENMASK(7, 6),				\
		0, 0, 0, NULL, 0,					\
		S2MPG11_PMIC_##_pc_reg, _pc_mask)

/* LDO with ramp support and possibly GPIO controlled */
#define s2mpg11_regulator_desc_ldo_ramp(_num, _supply, _vrange,		\
		_en_mask, _r_reg, _pc_reg, _pc_mask)			\
	s2mpg11_regulator_desc_ldo_cmn(_num, _supply,			\
		s2mpg10_reg_ldo_ramp_ops, _vrange, CTRL1, GENMASK(6, 0), \
		LDO_CTRL1, _en_mask,					\
		6250, S2MPG11_PMIC_##_r_reg, GENMASK(1, 0),		\
		s2mpg10_ldo_ramp_table,					\
		ARRAY_SIZE(s2mpg10_ldo_ramp_table),			\
		S2MPG11_PMIC_##_pc_reg, _pc_mask)

/* voltage range for s2mpg11 BUCK 1, 2, 3, 4, 8, 9, 10 */
S2MPG10_VOLTAGE_RANGE(s2mpg11_buck, 1, 200000, 450000, 1300000, STEP_6_25_MV);

/* voltage range for s2mpg11 BUCK 5 */
S2MPG10_VOLTAGE_RANGE(s2mpg11_buck, 5, 200000, 400000, 1300000, STEP_6_25_MV);

/* voltage range for s2mpg11 BUCK 6 */
S2MPG10_VOLTAGE_RANGE(s2mpg11_buck, 6, 200000, 1000000, 1500000, STEP_6_25_MV);

/* voltage range for s2mpg11 BUCK 7 */
S2MPG10_VOLTAGE_RANGE(s2mpg11_buck, 7, 600000, 1500000, 2200000, STEP_12_5_MV);

/* voltage range for s2mpg11 BUCK D */
S2MPG10_VOLTAGE_RANGE(s2mpg11_buck, d, 600000, 2400000, 3300000, STEP_12_5_MV);

/* voltage range for s2mpg11 BUCK A */
S2MPG10_VOLTAGE_RANGE(s2mpg11_buck, a, 600000, 1700000, 2100000, STEP_12_5_MV);

/* voltage range for s2mpg11 BUCK BOOST */
S2MPG10_VOLTAGE_RANGE(s2mpg11_buck, boost,
		      2600000, 3000000, 3600000, STEP_12_5_MV);

/* voltage range for s2mpg11 LDO 1, 2 */
S2MPG10_VOLTAGE_RANGE(s2mpg11_ldo, 1, 300000, 450000, 950000, STEP_12_5_MV);

/* voltage range for s2mpg11 LDO 3, 7, 10, 11, 12, 14, 15 */
S2MPG10_VOLTAGE_RANGE(s2mpg11_ldo, 3, 700000, 1600000, 1950000, STEP_25_MV);

/* voltage range for s2mpg11 LDO 4, 6 */
S2MPG10_VOLTAGE_RANGE(s2mpg11_ldo, 4, 1800000, 2500000, 3300000, STEP_25_MV);

/* voltage range for s2mpg11 LDO 5 */
S2MPG10_VOLTAGE_RANGE(s2mpg11_ldo, 5, 1600000, 1600000, 1950000, STEP_12_5_MV);

/* voltage range for s2mpg11 LDO 8 */
S2MPG10_VOLTAGE_RANGE(s2mpg11_ldo, 8, 979600, 1130400, 1281200, 5800);

/* voltage range for s2mpg11 LDO 9 */
S2MPG10_VOLTAGE_RANGE(s2mpg11_ldo, 9, 725000, 725000, 1300000, STEP_12_5_MV);

/* voltage range for s2mpg11 LDO 13 */
S2MPG10_VOLTAGE_RANGE(s2mpg11_ldo, 13, 1800000, 1800000, 3350000, STEP_25_MV);

static const struct s2mpg10_regulator_desc s2mpg11_regulators[] = {
	s2mpg11_regulator_desc_buckboost(),
	s2mpg11_regulator_desc_buckn_cm_gpio(1, s2mpg11_buck_vranges1,
					     OUT1, GENMASK(7, 6), DVS_RAMP1,
					     PCTRLSEL1, GENMASK(3, 0)),
	s2mpg11_regulator_desc_buckn_cm_gpio(2, s2mpg11_buck_vranges1,
					     OUT1, GENMASK(7, 6), DVS_RAMP1,
					     PCTRLSEL1, GENMASK(7, 4)),
	s2mpg11_regulator_desc_buckn_cm_gpio(3, s2mpg11_buck_vranges1,
					     OUT1, GENMASK(7, 6), DVS_RAMP2,
					     PCTRLSEL2, GENMASK(3, 0)),
	s2mpg11_regulator_desc_buck_cm(4, s2mpg11_buck_vranges1,
				       OUT, BIT(7), DVS_RAMP2),
	s2mpg11_regulator_desc_buckn_cm_gpio(5, s2mpg11_buck_vranges5,
					     OUT, GENMASK(7, 6), DVS_RAMP3,
					     PCTRLSEL2, GENMASK(7, 4)),
	s2mpg11_regulator_desc_buck_cm(6, s2mpg11_buck_vranges6,
				       OUT1, BIT(7), DVS_RAMP3),
	s2mpg11_regulator_desc_buck_vm(7, s2mpg11_buck_vranges7,
				       OUT1, BIT(7), DVS_RAMP4),
	s2mpg11_regulator_desc_buckn_cm_gpio(8, s2mpg11_buck_vranges1,
					     OUT1, GENMASK(7, 6), DVS_RAMP4,
					     PCTRLSEL3, GENMASK(3, 0)),
	s2mpg11_regulator_desc_buckn_cm_gpio(9, s2mpg11_buck_vranges1,
					     OUT1, GENMASK(7, 6), DVS_RAMP5,
					     PCTRLSEL3, GENMASK(7, 4)),
	s2mpg11_regulator_desc_buck_cm(10, s2mpg11_buck_vranges1,
				       OUT, BIT(7), DVS_RAMP5),
	s2mpg11_regulator_desc_bucka(D, d, DVS_RAMP6, PCTRLSEL4, GENMASK(3, 0)),
	s2mpg11_regulator_desc_bucka(A, a, DVS_RAMP6, PCTRLSEL4, GENMASK(7, 4)),
	s2mpg11_regulator_desc_ldo_ramp(1, "vinl1s", s2mpg11_ldo_vranges1,
					GENMASK(5, 4), DVS_SYNC_CTRL1,
					PCTRLSEL5, GENMASK(3, 0)),
	s2mpg11_regulator_desc_ldo_ramp(2, "vinl1s", s2mpg11_ldo_vranges1,
					GENMASK(7, 6), DVS_SYNC_CTRL2,
					PCTRLSEL5, GENMASK(7, 4)),
	s2mpg11_regulator_desc_ldo(3, "vinl3s", s2mpg11_ldo_vranges3),
	s2mpg11_regulator_desc_ldo(4, "vinl5s", s2mpg11_ldo_vranges4),
	s2mpg11_regulator_desc_ldo(5, "vinl3s", s2mpg11_ldo_vranges5),
	s2mpg11_regulator_desc_ldo(6, "vinl5s", s2mpg11_ldo_vranges4),
	s2mpg11_regulator_desc_ldo(7, "vinl3s", s2mpg11_ldo_vranges3),
	s2mpg11_regulator_desc_ldo_gpio(8, "vinl2s", s2mpg11_ldo_vranges8,
					PCTRLSEL6, GENMASK(3, 0)),
	s2mpg11_regulator_desc_ldo(9, "vinl2s", s2mpg11_ldo_vranges9),
	s2mpg11_regulator_desc_ldo(10, "vinl4s", s2mpg11_ldo_vranges3),
	s2mpg11_regulator_desc_ldo(11, "vinl4s", s2mpg11_ldo_vranges3),
	s2mpg11_regulator_desc_ldo(12, "vinl4s", s2mpg11_ldo_vranges3),
	s2mpg11_regulator_desc_ldo_gpio(13, "vinl6s", s2mpg11_ldo_vranges13,
					PCTRLSEL6, GENMASK(7, 4)),
	s2mpg11_regulator_desc_ldo(14, "vinl4s", s2mpg11_ldo_vranges3),
	s2mpg11_regulator_desc_ldo(15, "vinl3s", s2mpg11_ldo_vranges3)
};

static const struct regulator_ops s2mps11_ldo_ops = {
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= s2mps11_regulator_enable,
	.disable		= regulator_disable_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.set_voltage_time_sel	= regulator_set_voltage_time_sel,
	.set_suspend_disable	= s2mps11_regulator_set_suspend_disable,
};

static const struct regulator_ops s2mps11_buck_ops = {
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= s2mps11_regulator_enable,
	.disable		= regulator_disable_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.set_voltage_time_sel	= s2mps11_regulator_set_voltage_time_sel,
	.set_ramp_delay		= s2mps11_set_ramp_delay,
	.set_suspend_disable	= s2mps11_regulator_set_suspend_disable,
};

#define regulator_desc_s2mps11_ldo(num, step) {		\
	.name		= "LDO"#num,			\
	.id		= S2MPS11_LDO##num,		\
	.of_match	= of_match_ptr("LDO"#num),	\
	.regulators_node = of_match_ptr("regulators"),	\
	.ops		= &s2mps11_ldo_ops,		\
	.type		= REGULATOR_VOLTAGE,		\
	.owner		= THIS_MODULE,			\
	.ramp_delay	= RAMP_DELAY_12_MVUS,		\
	.min_uV		= MIN_800_MV,			\
	.uV_step	= step,				\
	.n_voltages	= S2MPS11_LDO_N_VOLTAGES,	\
	.vsel_reg	= S2MPS11_REG_L1CTRL + num - 1,	\
	.vsel_mask	= S2MPS11_LDO_VSEL_MASK,	\
	.enable_reg	= S2MPS11_REG_L1CTRL + num - 1,	\
	.enable_mask	= S2MPS11_ENABLE_MASK		\
}

#define regulator_desc_s2mps11_buck1_4(num) {			\
	.name		= "BUCK"#num,				\
	.id		= S2MPS11_BUCK##num,			\
	.of_match	= of_match_ptr("BUCK"#num),		\
	.regulators_node = of_match_ptr("regulators"),		\
	.ops		= &s2mps11_buck_ops,			\
	.type		= REGULATOR_VOLTAGE,			\
	.owner		= THIS_MODULE,				\
	.min_uV		= MIN_650_MV,				\
	.uV_step	= STEP_6_25_MV,				\
	.linear_min_sel	= 8,					\
	.n_voltages	= S2MPS11_BUCK12346_N_VOLTAGES,		\
	.ramp_delay	= S2MPS11_RAMP_DELAY,			\
	.vsel_reg	= S2MPS11_REG_B1CTRL2 + (num - 1) * 2,	\
	.vsel_mask	= S2MPS11_BUCK_VSEL_MASK,		\
	.enable_reg	= S2MPS11_REG_B1CTRL1 + (num - 1) * 2,	\
	.enable_mask	= S2MPS11_ENABLE_MASK			\
}

#define regulator_desc_s2mps11_buck5 {				\
	.name		= "BUCK5",				\
	.id		= S2MPS11_BUCK5,			\
	.of_match	= of_match_ptr("BUCK5"),		\
	.regulators_node = of_match_ptr("regulators"),		\
	.ops		= &s2mps11_buck_ops,			\
	.type		= REGULATOR_VOLTAGE,			\
	.owner		= THIS_MODULE,				\
	.min_uV		= MIN_650_MV,				\
	.uV_step	= STEP_6_25_MV,				\
	.linear_min_sel	= 8,					\
	.n_voltages	= S2MPS11_BUCK5_N_VOLTAGES,		\
	.ramp_delay	= S2MPS11_RAMP_DELAY,			\
	.vsel_reg	= S2MPS11_REG_B5CTRL2,			\
	.vsel_mask	= S2MPS11_BUCK_VSEL_MASK,		\
	.enable_reg	= S2MPS11_REG_B5CTRL1,			\
	.enable_mask	= S2MPS11_ENABLE_MASK			\
}

#define regulator_desc_s2mps11_buck67810(num, min, step, min_sel, voltages) {	\
	.name		= "BUCK"#num,				\
	.id		= S2MPS11_BUCK##num,			\
	.of_match	= of_match_ptr("BUCK"#num),		\
	.regulators_node = of_match_ptr("regulators"),		\
	.ops		= &s2mps11_buck_ops,			\
	.type		= REGULATOR_VOLTAGE,			\
	.owner		= THIS_MODULE,				\
	.min_uV		= min,					\
	.uV_step	= step,					\
	.linear_min_sel	= min_sel,				\
	.n_voltages	= voltages,				\
	.ramp_delay	= S2MPS11_RAMP_DELAY,			\
	.vsel_reg	= S2MPS11_REG_B6CTRL2 + (num - 6) * 2,	\
	.vsel_mask	= S2MPS11_BUCK_VSEL_MASK,		\
	.enable_reg	= S2MPS11_REG_B6CTRL1 + (num - 6) * 2,	\
	.enable_mask	= S2MPS11_ENABLE_MASK			\
}

#define regulator_desc_s2mps11_buck9 {				\
	.name		= "BUCK9",				\
	.id		= S2MPS11_BUCK9,			\
	.of_match	= of_match_ptr("BUCK9"),		\
	.regulators_node = of_match_ptr("regulators"),		\
	.ops		= &s2mps11_buck_ops,			\
	.type		= REGULATOR_VOLTAGE,			\
	.owner		= THIS_MODULE,				\
	.min_uV		= MIN_3000_MV,				\
	.uV_step	= STEP_25_MV,				\
	.n_voltages	= S2MPS11_BUCK9_N_VOLTAGES,		\
	.ramp_delay	= S2MPS11_RAMP_DELAY,			\
	.vsel_reg	= S2MPS11_REG_B9CTRL2,			\
	.vsel_mask	= S2MPS11_BUCK9_VSEL_MASK,		\
	.enable_reg	= S2MPS11_REG_B9CTRL1,			\
	.enable_mask	= S2MPS11_ENABLE_MASK			\
}

static const struct regulator_desc s2mps11_regulators[] = {
	regulator_desc_s2mps11_ldo(1, STEP_25_MV),
	regulator_desc_s2mps11_ldo(2, STEP_50_MV),
	regulator_desc_s2mps11_ldo(3, STEP_50_MV),
	regulator_desc_s2mps11_ldo(4, STEP_50_MV),
	regulator_desc_s2mps11_ldo(5, STEP_50_MV),
	regulator_desc_s2mps11_ldo(6, STEP_25_MV),
	regulator_desc_s2mps11_ldo(7, STEP_50_MV),
	regulator_desc_s2mps11_ldo(8, STEP_50_MV),
	regulator_desc_s2mps11_ldo(9, STEP_50_MV),
	regulator_desc_s2mps11_ldo(10, STEP_50_MV),
	regulator_desc_s2mps11_ldo(11, STEP_25_MV),
	regulator_desc_s2mps11_ldo(12, STEP_50_MV),
	regulator_desc_s2mps11_ldo(13, STEP_50_MV),
	regulator_desc_s2mps11_ldo(14, STEP_50_MV),
	regulator_desc_s2mps11_ldo(15, STEP_50_MV),
	regulator_desc_s2mps11_ldo(16, STEP_50_MV),
	regulator_desc_s2mps11_ldo(17, STEP_50_MV),
	regulator_desc_s2mps11_ldo(18, STEP_50_MV),
	regulator_desc_s2mps11_ldo(19, STEP_50_MV),
	regulator_desc_s2mps11_ldo(20, STEP_50_MV),
	regulator_desc_s2mps11_ldo(21, STEP_50_MV),
	regulator_desc_s2mps11_ldo(22, STEP_25_MV),
	regulator_desc_s2mps11_ldo(23, STEP_25_MV),
	regulator_desc_s2mps11_ldo(24, STEP_50_MV),
	regulator_desc_s2mps11_ldo(25, STEP_50_MV),
	regulator_desc_s2mps11_ldo(26, STEP_50_MV),
	regulator_desc_s2mps11_ldo(27, STEP_25_MV),
	regulator_desc_s2mps11_ldo(28, STEP_50_MV),
	regulator_desc_s2mps11_ldo(29, STEP_50_MV),
	regulator_desc_s2mps11_ldo(30, STEP_50_MV),
	regulator_desc_s2mps11_ldo(31, STEP_50_MV),
	regulator_desc_s2mps11_ldo(32, STEP_50_MV),
	regulator_desc_s2mps11_ldo(33, STEP_50_MV),
	regulator_desc_s2mps11_ldo(34, STEP_50_MV),
	regulator_desc_s2mps11_ldo(35, STEP_25_MV),
	regulator_desc_s2mps11_ldo(36, STEP_50_MV),
	regulator_desc_s2mps11_ldo(37, STEP_50_MV),
	regulator_desc_s2mps11_ldo(38, STEP_50_MV),
	regulator_desc_s2mps11_buck1_4(1),
	regulator_desc_s2mps11_buck1_4(2),
	regulator_desc_s2mps11_buck1_4(3),
	regulator_desc_s2mps11_buck1_4(4),
	regulator_desc_s2mps11_buck5,
	regulator_desc_s2mps11_buck67810(6, MIN_650_MV, STEP_6_25_MV, 8,
					 S2MPS11_BUCK12346_N_VOLTAGES),
	regulator_desc_s2mps11_buck67810(7, MIN_750_MV, STEP_12_5_MV, 0,
					 S2MPS11_BUCK7810_N_VOLTAGES),
	regulator_desc_s2mps11_buck67810(8, MIN_750_MV, STEP_12_5_MV, 0,
					 S2MPS11_BUCK7810_N_VOLTAGES),
	regulator_desc_s2mps11_buck9,
	regulator_desc_s2mps11_buck67810(10, MIN_750_MV, STEP_12_5_MV, 0,
					 S2MPS11_BUCK7810_N_VOLTAGES),
};

static const struct regulator_ops s2mps14_reg_ops;

#define regulator_desc_s2mps13_ldo(num, min, step, min_sel) {	\
	.name		= "LDO"#num,				\
	.id		= S2MPS13_LDO##num,			\
	.of_match	= of_match_ptr("LDO"#num),		\
	.regulators_node = of_match_ptr("regulators"),		\
	.ops		= &s2mps14_reg_ops,			\
	.type		= REGULATOR_VOLTAGE,			\
	.owner		= THIS_MODULE,				\
	.min_uV		= min,					\
	.uV_step	= step,					\
	.linear_min_sel	= min_sel,				\
	.n_voltages	= S2MPS14_LDO_N_VOLTAGES,		\
	.vsel_reg	= S2MPS13_REG_L1CTRL + num - 1,		\
	.vsel_mask	= S2MPS14_LDO_VSEL_MASK,		\
	.enable_reg	= S2MPS13_REG_L1CTRL + num - 1,		\
	.enable_mask	= S2MPS14_ENABLE_MASK			\
}

#define regulator_desc_s2mps13_buck(num, min, step, min_sel) {	\
	.name		= "BUCK"#num,				\
	.id		= S2MPS13_BUCK##num,			\
	.of_match	= of_match_ptr("BUCK"#num),		\
	.regulators_node = of_match_ptr("regulators"),		\
	.ops		= &s2mps14_reg_ops,			\
	.type		= REGULATOR_VOLTAGE,			\
	.owner		= THIS_MODULE,				\
	.min_uV		= min,					\
	.uV_step	= step,					\
	.linear_min_sel	= min_sel,				\
	.n_voltages	= S2MPS14_BUCK_N_VOLTAGES,		\
	.ramp_delay	= S2MPS13_BUCK_RAMP_DELAY,		\
	.vsel_reg	= S2MPS13_REG_B1OUT + (num - 1) * 2,	\
	.vsel_mask	= S2MPS14_BUCK_VSEL_MASK,		\
	.enable_reg	= S2MPS13_REG_B1CTRL + (num - 1) * 2,	\
	.enable_mask	= S2MPS14_ENABLE_MASK			\
}

#define regulator_desc_s2mps13_buck7(num, min, step, min_sel) {	\
	.name		= "BUCK"#num,				\
	.id		= S2MPS13_BUCK##num,			\
	.of_match	= of_match_ptr("BUCK"#num),		\
	.regulators_node = of_match_ptr("regulators"),		\
	.ops		= &s2mps14_reg_ops,			\
	.type		= REGULATOR_VOLTAGE,			\
	.owner		= THIS_MODULE,				\
	.min_uV		= min,					\
	.uV_step	= step,					\
	.linear_min_sel	= min_sel,				\
	.n_voltages	= S2MPS14_BUCK_N_VOLTAGES,		\
	.ramp_delay	= S2MPS13_BUCK_RAMP_DELAY,		\
	.vsel_reg	= S2MPS13_REG_B1OUT + (num) * 2 - 1,	\
	.vsel_mask	= S2MPS14_BUCK_VSEL_MASK,		\
	.enable_reg	= S2MPS13_REG_B1CTRL + (num - 1) * 2,	\
	.enable_mask	= S2MPS14_ENABLE_MASK			\
}

#define regulator_desc_s2mps13_buck8_10(num, min, step, min_sel) {	\
	.name		= "BUCK"#num,				\
	.id		= S2MPS13_BUCK##num,			\
	.of_match	= of_match_ptr("BUCK"#num),		\
	.regulators_node = of_match_ptr("regulators"),		\
	.ops		= &s2mps14_reg_ops,			\
	.type		= REGULATOR_VOLTAGE,			\
	.owner		= THIS_MODULE,				\
	.min_uV		= min,					\
	.uV_step	= step,					\
	.linear_min_sel	= min_sel,				\
	.n_voltages	= S2MPS14_BUCK_N_VOLTAGES,		\
	.ramp_delay	= S2MPS13_BUCK_RAMP_DELAY,		\
	.vsel_reg	= S2MPS13_REG_B1OUT + (num) * 2 - 1,	\
	.vsel_mask	= S2MPS14_BUCK_VSEL_MASK,		\
	.enable_reg	= S2MPS13_REG_B1CTRL + (num) * 2 - 1,	\
	.enable_mask	= S2MPS14_ENABLE_MASK			\
}

static const struct regulator_desc s2mps13_regulators[] = {
	regulator_desc_s2mps13_ldo(1,  MIN_800_MV,  STEP_12_5_MV, 0x00),
	regulator_desc_s2mps13_ldo(2,  MIN_1400_MV, STEP_50_MV,   0x0C),
	regulator_desc_s2mps13_ldo(3,  MIN_1000_MV, STEP_25_MV,   0x08),
	regulator_desc_s2mps13_ldo(4,  MIN_800_MV,  STEP_12_5_MV, 0x00),
	regulator_desc_s2mps13_ldo(5,  MIN_800_MV,  STEP_12_5_MV, 0x00),
	regulator_desc_s2mps13_ldo(6,  MIN_800_MV,  STEP_12_5_MV, 0x00),
	regulator_desc_s2mps13_ldo(7,  MIN_1000_MV, STEP_25_MV,   0x08),
	regulator_desc_s2mps13_ldo(8,  MIN_1000_MV, STEP_25_MV,   0x08),
	regulator_desc_s2mps13_ldo(9,  MIN_1000_MV, STEP_25_MV,   0x08),
	regulator_desc_s2mps13_ldo(10, MIN_1400_MV, STEP_50_MV,   0x0C),
	regulator_desc_s2mps13_ldo(11, MIN_800_MV,  STEP_25_MV,   0x10),
	regulator_desc_s2mps13_ldo(12, MIN_800_MV,  STEP_25_MV,   0x10),
	regulator_desc_s2mps13_ldo(13, MIN_800_MV,  STEP_25_MV,   0x10),
	regulator_desc_s2mps13_ldo(14, MIN_800_MV,  STEP_12_5_MV, 0x00),
	regulator_desc_s2mps13_ldo(15, MIN_800_MV,  STEP_12_5_MV, 0x00),
	regulator_desc_s2mps13_ldo(16, MIN_1400_MV, STEP_50_MV,   0x0C),
	regulator_desc_s2mps13_ldo(17, MIN_1400_MV, STEP_50_MV,   0x0C),
	regulator_desc_s2mps13_ldo(18, MIN_1000_MV, STEP_25_MV,   0x08),
	regulator_desc_s2mps13_ldo(19, MIN_1000_MV, STEP_25_MV,   0x08),
	regulator_desc_s2mps13_ldo(20, MIN_1400_MV, STEP_50_MV,   0x0C),
	regulator_desc_s2mps13_ldo(21, MIN_1000_MV, STEP_25_MV,   0x08),
	regulator_desc_s2mps13_ldo(22, MIN_1000_MV, STEP_25_MV,   0x08),
	regulator_desc_s2mps13_ldo(23, MIN_800_MV,  STEP_12_5_MV, 0x00),
	regulator_desc_s2mps13_ldo(24, MIN_800_MV,  STEP_12_5_MV, 0x00),
	regulator_desc_s2mps13_ldo(25, MIN_1400_MV, STEP_50_MV,   0x0C),
	regulator_desc_s2mps13_ldo(26, MIN_1400_MV, STEP_50_MV,   0x0C),
	regulator_desc_s2mps13_ldo(27, MIN_1400_MV, STEP_50_MV,   0x0C),
	regulator_desc_s2mps13_ldo(28, MIN_1000_MV, STEP_25_MV,   0x08),
	regulator_desc_s2mps13_ldo(29, MIN_1400_MV, STEP_50_MV,   0x0C),
	regulator_desc_s2mps13_ldo(30, MIN_1400_MV, STEP_50_MV,   0x0C),
	regulator_desc_s2mps13_ldo(31, MIN_1000_MV, STEP_25_MV,   0x08),
	regulator_desc_s2mps13_ldo(32, MIN_1000_MV, STEP_25_MV,   0x08),
	regulator_desc_s2mps13_ldo(33, MIN_1400_MV, STEP_50_MV,   0x0C),
	regulator_desc_s2mps13_ldo(34, MIN_1000_MV, STEP_25_MV,   0x08),
	regulator_desc_s2mps13_ldo(35, MIN_1400_MV, STEP_50_MV,   0x0C),
	regulator_desc_s2mps13_ldo(36, MIN_800_MV,  STEP_12_5_MV, 0x00),
	regulator_desc_s2mps13_ldo(37, MIN_1000_MV, STEP_25_MV,   0x08),
	regulator_desc_s2mps13_ldo(38, MIN_1400_MV, STEP_50_MV,   0x0C),
	regulator_desc_s2mps13_ldo(39, MIN_1000_MV, STEP_25_MV,   0x08),
	regulator_desc_s2mps13_ldo(40, MIN_1400_MV, STEP_50_MV,   0x0C),
	regulator_desc_s2mps13_buck(1,  MIN_500_MV,  STEP_6_25_MV, 0x10),
	regulator_desc_s2mps13_buck(2,  MIN_500_MV,  STEP_6_25_MV, 0x10),
	regulator_desc_s2mps13_buck(3,  MIN_500_MV,  STEP_6_25_MV, 0x10),
	regulator_desc_s2mps13_buck(4,  MIN_500_MV,  STEP_6_25_MV, 0x10),
	regulator_desc_s2mps13_buck(5,  MIN_500_MV,  STEP_6_25_MV, 0x10),
	regulator_desc_s2mps13_buck(6,  MIN_500_MV,  STEP_6_25_MV, 0x10),
	regulator_desc_s2mps13_buck7(7,  MIN_500_MV,  STEP_6_25_MV, 0x10),
	regulator_desc_s2mps13_buck8_10(8,  MIN_1000_MV, STEP_12_5_MV, 0x20),
	regulator_desc_s2mps13_buck8_10(9,  MIN_1000_MV, STEP_12_5_MV, 0x20),
	regulator_desc_s2mps13_buck8_10(10, MIN_500_MV,  STEP_6_25_MV, 0x10),
};

static const struct regulator_ops s2mps14_reg_ops = {
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= s2mps11_regulator_enable,
	.disable		= regulator_disable_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.set_voltage_time_sel	= regulator_set_voltage_time_sel,
	.set_suspend_disable	= s2mps11_regulator_set_suspend_disable,
};

#define regulator_desc_s2mps14_ldo(num, min, step) {	\
	.name		= "LDO"#num,			\
	.id		= S2MPS14_LDO##num,		\
	.of_match	= of_match_ptr("LDO"#num),	\
	.regulators_node = of_match_ptr("regulators"),	\
	.of_parse_cb	= s2mps11_of_parse_cb,		\
	.ops		= &s2mps14_reg_ops,		\
	.type		= REGULATOR_VOLTAGE,		\
	.owner		= THIS_MODULE,			\
	.min_uV		= min,				\
	.uV_step	= step,				\
	.n_voltages	= S2MPS14_LDO_N_VOLTAGES,	\
	.vsel_reg	= S2MPS14_REG_L1CTRL + num - 1,	\
	.vsel_mask	= S2MPS14_LDO_VSEL_MASK,	\
	.enable_reg	= S2MPS14_REG_L1CTRL + num - 1,	\
	.enable_mask	= S2MPS14_ENABLE_MASK		\
}

#define regulator_desc_s2mps14_buck(num, min, step, min_sel) {	\
	.name		= "BUCK"#num,				\
	.id		= S2MPS14_BUCK##num,			\
	.of_match	= of_match_ptr("BUCK"#num),		\
	.regulators_node = of_match_ptr("regulators"),		\
	.of_parse_cb	= s2mps11_of_parse_cb,			\
	.ops		= &s2mps14_reg_ops,			\
	.type		= REGULATOR_VOLTAGE,			\
	.owner		= THIS_MODULE,				\
	.min_uV		= min,					\
	.uV_step	= step,					\
	.n_voltages	= S2MPS14_BUCK_N_VOLTAGES,		\
	.linear_min_sel = min_sel,				\
	.ramp_delay	= S2MPS14_BUCK_RAMP_DELAY,		\
	.vsel_reg	= S2MPS14_REG_B1CTRL2 + (num - 1) * 2,	\
	.vsel_mask	= S2MPS14_BUCK_VSEL_MASK,		\
	.enable_reg	= S2MPS14_REG_B1CTRL1 + (num - 1) * 2,	\
	.enable_mask	= S2MPS14_ENABLE_MASK			\
}

static const struct regulator_desc s2mps14_regulators[] = {
	regulator_desc_s2mps14_ldo(1, MIN_800_MV, STEP_12_5_MV),
	regulator_desc_s2mps14_ldo(2, MIN_800_MV, STEP_12_5_MV),
	regulator_desc_s2mps14_ldo(3, MIN_800_MV, STEP_25_MV),
	regulator_desc_s2mps14_ldo(4, MIN_800_MV, STEP_25_MV),
	regulator_desc_s2mps14_ldo(5, MIN_800_MV, STEP_12_5_MV),
	regulator_desc_s2mps14_ldo(6, MIN_800_MV, STEP_12_5_MV),
	regulator_desc_s2mps14_ldo(7, MIN_800_MV, STEP_25_MV),
	regulator_desc_s2mps14_ldo(8, MIN_1800_MV, STEP_25_MV),
	regulator_desc_s2mps14_ldo(9, MIN_800_MV, STEP_12_5_MV),
	regulator_desc_s2mps14_ldo(10, MIN_800_MV, STEP_12_5_MV),
	regulator_desc_s2mps14_ldo(11, MIN_800_MV, STEP_25_MV),
	regulator_desc_s2mps14_ldo(12, MIN_1800_MV, STEP_25_MV),
	regulator_desc_s2mps14_ldo(13, MIN_1800_MV, STEP_25_MV),
	regulator_desc_s2mps14_ldo(14, MIN_1800_MV, STEP_25_MV),
	regulator_desc_s2mps14_ldo(15, MIN_1800_MV, STEP_25_MV),
	regulator_desc_s2mps14_ldo(16, MIN_1800_MV, STEP_25_MV),
	regulator_desc_s2mps14_ldo(17, MIN_1800_MV, STEP_25_MV),
	regulator_desc_s2mps14_ldo(18, MIN_1800_MV, STEP_25_MV),
	regulator_desc_s2mps14_ldo(19, MIN_800_MV, STEP_25_MV),
	regulator_desc_s2mps14_ldo(20, MIN_800_MV, STEP_25_MV),
	regulator_desc_s2mps14_ldo(21, MIN_800_MV, STEP_25_MV),
	regulator_desc_s2mps14_ldo(22, MIN_800_MV, STEP_12_5_MV),
	regulator_desc_s2mps14_ldo(23, MIN_800_MV, STEP_25_MV),
	regulator_desc_s2mps14_ldo(24, MIN_1800_MV, STEP_25_MV),
	regulator_desc_s2mps14_ldo(25, MIN_1800_MV, STEP_25_MV),
	regulator_desc_s2mps14_buck(1, MIN_600_MV, STEP_6_25_MV,
				    S2MPS14_BUCK1235_START_SEL),
	regulator_desc_s2mps14_buck(2, MIN_600_MV, STEP_6_25_MV,
				    S2MPS14_BUCK1235_START_SEL),
	regulator_desc_s2mps14_buck(3, MIN_600_MV, STEP_6_25_MV,
				    S2MPS14_BUCK1235_START_SEL),
	regulator_desc_s2mps14_buck(4, MIN_1400_MV, STEP_12_5_MV,
				    S2MPS14_BUCK4_START_SEL),
	regulator_desc_s2mps14_buck(5, MIN_600_MV, STEP_6_25_MV,
				    S2MPS14_BUCK1235_START_SEL),
};

static const struct regulator_ops s2mps15_reg_ldo_ops = {
	.list_voltage		= regulator_list_voltage_linear_range,
	.map_voltage		= regulator_map_voltage_linear_range,
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
};

static const struct regulator_ops s2mps15_reg_buck_ops = {
	.list_voltage		= regulator_list_voltage_linear_range,
	.map_voltage		= regulator_map_voltage_linear_range,
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.set_voltage_time_sel	= regulator_set_voltage_time_sel,
};

#define regulator_desc_s2mps15_ldo(num, range) {	\
	.name		= "LDO"#num,			\
	.id		= S2MPS15_LDO##num,		\
	.of_match	= of_match_ptr("LDO"#num),	\
	.regulators_node = of_match_ptr("regulators"),	\
	.ops		= &s2mps15_reg_ldo_ops,		\
	.type		= REGULATOR_VOLTAGE,		\
	.owner		= THIS_MODULE,			\
	.linear_ranges	= range,			\
	.n_linear_ranges = ARRAY_SIZE(range),		\
	.n_voltages	= S2MPS15_LDO_N_VOLTAGES,	\
	.vsel_reg	= S2MPS15_REG_L1CTRL + num - 1,	\
	.vsel_mask	= S2MPS15_LDO_VSEL_MASK,	\
	.enable_reg	= S2MPS15_REG_L1CTRL + num - 1,	\
	.enable_mask	= S2MPS15_ENABLE_MASK		\
}

#define regulator_desc_s2mps15_buck(num, range) {			\
	.name		= "BUCK"#num,					\
	.id		= S2MPS15_BUCK##num,				\
	.of_match	= of_match_ptr("BUCK"#num),			\
	.regulators_node = of_match_ptr("regulators"),			\
	.ops		= &s2mps15_reg_buck_ops,			\
	.type		= REGULATOR_VOLTAGE,				\
	.owner		= THIS_MODULE,					\
	.linear_ranges	= range,					\
	.n_linear_ranges = ARRAY_SIZE(range),				\
	.ramp_delay	= 12500,					\
	.n_voltages	= S2MPS15_BUCK_N_VOLTAGES,			\
	.vsel_reg	= S2MPS15_REG_B1CTRL2 + ((num - 1) * 2),	\
	.vsel_mask	= S2MPS15_BUCK_VSEL_MASK,			\
	.enable_reg	= S2MPS15_REG_B1CTRL1 + ((num - 1) * 2),	\
	.enable_mask	= S2MPS15_ENABLE_MASK				\
}

/* voltage range for s2mps15 LDO 3, 5, 15, 16, 18, 20, 23 and 27 */
static const struct linear_range s2mps15_ldo_voltage_ranges1[] = {
	REGULATOR_LINEAR_RANGE(1000000, 0xc, 0x38, 25000),
};

/* voltage range for s2mps15 LDO 2, 6, 14, 17, 19, 21, 24 and 25 */
static const struct linear_range s2mps15_ldo_voltage_ranges2[] = {
	REGULATOR_LINEAR_RANGE(1800000, 0x0, 0x3f, 25000),
};

/* voltage range for s2mps15 LDO 4, 11, 12, 13, 22 and 26 */
static const struct linear_range s2mps15_ldo_voltage_ranges3[] = {
	REGULATOR_LINEAR_RANGE(700000, 0x0, 0x34, 12500),
};

/* voltage range for s2mps15 LDO 7, 8, 9 and 10 */
static const struct linear_range s2mps15_ldo_voltage_ranges4[] = {
	REGULATOR_LINEAR_RANGE(700000, 0x10, 0x20, 25000),
};

/* voltage range for s2mps15 LDO 1 */
static const struct linear_range s2mps15_ldo_voltage_ranges5[] = {
	REGULATOR_LINEAR_RANGE(500000, 0x0, 0x20, 12500),
};

/* voltage range for s2mps15 BUCK 1, 2, 3, 4, 5, 6 and 7 */
static const struct linear_range s2mps15_buck_voltage_ranges1[] = {
	REGULATOR_LINEAR_RANGE(500000, 0x20, 0xc0, 6250),
};

/* voltage range for s2mps15 BUCK 8, 9 and 10 */
static const struct linear_range s2mps15_buck_voltage_ranges2[] = {
	REGULATOR_LINEAR_RANGE(1000000, 0x20, 0x78, 12500),
};

static const struct regulator_desc s2mps15_regulators[] = {
	regulator_desc_s2mps15_ldo(1, s2mps15_ldo_voltage_ranges5),
	regulator_desc_s2mps15_ldo(2, s2mps15_ldo_voltage_ranges2),
	regulator_desc_s2mps15_ldo(3, s2mps15_ldo_voltage_ranges1),
	regulator_desc_s2mps15_ldo(4, s2mps15_ldo_voltage_ranges3),
	regulator_desc_s2mps15_ldo(5, s2mps15_ldo_voltage_ranges1),
	regulator_desc_s2mps15_ldo(6, s2mps15_ldo_voltage_ranges2),
	regulator_desc_s2mps15_ldo(7, s2mps15_ldo_voltage_ranges4),
	regulator_desc_s2mps15_ldo(8, s2mps15_ldo_voltage_ranges4),
	regulator_desc_s2mps15_ldo(9, s2mps15_ldo_voltage_ranges4),
	regulator_desc_s2mps15_ldo(10, s2mps15_ldo_voltage_ranges4),
	regulator_desc_s2mps15_ldo(11, s2mps15_ldo_voltage_ranges3),
	regulator_desc_s2mps15_ldo(12, s2mps15_ldo_voltage_ranges3),
	regulator_desc_s2mps15_ldo(13, s2mps15_ldo_voltage_ranges3),
	regulator_desc_s2mps15_ldo(14, s2mps15_ldo_voltage_ranges2),
	regulator_desc_s2mps15_ldo(15, s2mps15_ldo_voltage_ranges1),
	regulator_desc_s2mps15_ldo(16, s2mps15_ldo_voltage_ranges1),
	regulator_desc_s2mps15_ldo(17, s2mps15_ldo_voltage_ranges2),
	regulator_desc_s2mps15_ldo(18, s2mps15_ldo_voltage_ranges1),
	regulator_desc_s2mps15_ldo(19, s2mps15_ldo_voltage_ranges2),
	regulator_desc_s2mps15_ldo(20, s2mps15_ldo_voltage_ranges1),
	regulator_desc_s2mps15_ldo(21, s2mps15_ldo_voltage_ranges2),
	regulator_desc_s2mps15_ldo(22, s2mps15_ldo_voltage_ranges3),
	regulator_desc_s2mps15_ldo(23, s2mps15_ldo_voltage_ranges1),
	regulator_desc_s2mps15_ldo(24, s2mps15_ldo_voltage_ranges2),
	regulator_desc_s2mps15_ldo(25, s2mps15_ldo_voltage_ranges2),
	regulator_desc_s2mps15_ldo(26, s2mps15_ldo_voltage_ranges3),
	regulator_desc_s2mps15_ldo(27, s2mps15_ldo_voltage_ranges1),
	regulator_desc_s2mps15_buck(1, s2mps15_buck_voltage_ranges1),
	regulator_desc_s2mps15_buck(2, s2mps15_buck_voltage_ranges1),
	regulator_desc_s2mps15_buck(3, s2mps15_buck_voltage_ranges1),
	regulator_desc_s2mps15_buck(4, s2mps15_buck_voltage_ranges1),
	regulator_desc_s2mps15_buck(5, s2mps15_buck_voltage_ranges1),
	regulator_desc_s2mps15_buck(6, s2mps15_buck_voltage_ranges1),
	regulator_desc_s2mps15_buck(7, s2mps15_buck_voltage_ranges1),
	regulator_desc_s2mps15_buck(8, s2mps15_buck_voltage_ranges2),
	regulator_desc_s2mps15_buck(9, s2mps15_buck_voltage_ranges2),
	regulator_desc_s2mps15_buck(10, s2mps15_buck_voltage_ranges2),
};

static int s2mps14_pmic_enable_ext_control(struct s2mps11_info *s2mps11,
					   struct regulator_dev *rdev)
{
	int ret = regmap_update_bits(rdev->regmap, rdev->desc->enable_reg,
				     rdev->desc->enable_mask,
				     S2MPS14_ENABLE_EXT_CONTROL);
	if (ret < 0)
		return dev_err_probe(rdev_get_dev(rdev), ret,
				     "failed to enable GPIO control over %d/%s\n",
				     rdev->desc->id, rdev->desc->name);
	return 0;
}

static int s2mpu02_set_ramp_delay(struct regulator_dev *rdev, int ramp_delay)
{
	unsigned int ramp_val, ramp_shift, ramp_reg;
	int rdev_id = rdev_get_id(rdev);

	switch (rdev_id) {
	case S2MPU02_BUCK1:
		ramp_shift = S2MPU02_BUCK1_RAMP_SHIFT;
		break;
	case S2MPU02_BUCK2:
		ramp_shift = S2MPU02_BUCK2_RAMP_SHIFT;
		break;
	case S2MPU02_BUCK3:
		ramp_shift = S2MPU02_BUCK3_RAMP_SHIFT;
		break;
	case S2MPU02_BUCK4:
		ramp_shift = S2MPU02_BUCK4_RAMP_SHIFT;
		break;
	default:
		return 0;
	}
	ramp_reg = S2MPU02_REG_RAMP1;
	ramp_val = get_ramp_delay(ramp_delay);

	return regmap_update_bits(rdev->regmap, ramp_reg,
				  S2MPU02_BUCK1234_RAMP_MASK << ramp_shift,
				  ramp_val << ramp_shift);
}

static const struct regulator_ops s2mpu02_ldo_ops = {
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= s2mps11_regulator_enable,
	.disable		= regulator_disable_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.set_voltage_time_sel	= regulator_set_voltage_time_sel,
	.set_suspend_disable	= s2mps11_regulator_set_suspend_disable,
};

static const struct regulator_ops s2mpu02_buck_ops = {
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= s2mps11_regulator_enable,
	.disable		= regulator_disable_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.set_voltage_time_sel	= regulator_set_voltage_time_sel,
	.set_suspend_disable	= s2mps11_regulator_set_suspend_disable,
	.set_ramp_delay		= s2mpu02_set_ramp_delay,
};

#define regulator_desc_s2mpu02_ldo1(num) {		\
	.name		= "LDO"#num,			\
	.id		= S2MPU02_LDO##num,		\
	.of_match	= of_match_ptr("LDO"#num),	\
	.regulators_node = of_match_ptr("regulators"),	\
	.ops		= &s2mpu02_ldo_ops,		\
	.type		= REGULATOR_VOLTAGE,		\
	.owner		= THIS_MODULE,			\
	.min_uV		= S2MPU02_LDO_MIN_900MV,	\
	.uV_step	= S2MPU02_LDO_STEP_12_5MV,	\
	.linear_min_sel	= S2MPU02_LDO_GROUP1_START_SEL,	\
	.n_voltages	= S2MPU02_LDO_N_VOLTAGES,	\
	.vsel_reg	= S2MPU02_REG_L1CTRL,		\
	.vsel_mask	= S2MPU02_LDO_VSEL_MASK,	\
	.enable_reg	= S2MPU02_REG_L1CTRL,		\
	.enable_mask	= S2MPU02_ENABLE_MASK		\
}
#define regulator_desc_s2mpu02_ldo2(num) {		\
	.name		= "LDO"#num,			\
	.id		= S2MPU02_LDO##num,		\
	.of_match	= of_match_ptr("LDO"#num),	\
	.regulators_node = of_match_ptr("regulators"),	\
	.ops		= &s2mpu02_ldo_ops,		\
	.type		= REGULATOR_VOLTAGE,		\
	.owner		= THIS_MODULE,			\
	.min_uV		= S2MPU02_LDO_MIN_1050MV,	\
	.uV_step	= S2MPU02_LDO_STEP_25MV,	\
	.linear_min_sel	= S2MPU02_LDO_GROUP2_START_SEL,	\
	.n_voltages	= S2MPU02_LDO_N_VOLTAGES,	\
	.vsel_reg	= S2MPU02_REG_L2CTRL1,		\
	.vsel_mask	= S2MPU02_LDO_VSEL_MASK,	\
	.enable_reg	= S2MPU02_REG_L2CTRL1,		\
	.enable_mask	= S2MPU02_ENABLE_MASK		\
}
#define regulator_desc_s2mpu02_ldo3(num) {		\
	.name		= "LDO"#num,			\
	.id		= S2MPU02_LDO##num,		\
	.of_match	= of_match_ptr("LDO"#num),	\
	.regulators_node = of_match_ptr("regulators"),	\
	.ops		= &s2mpu02_ldo_ops,		\
	.type		= REGULATOR_VOLTAGE,		\
	.owner		= THIS_MODULE,			\
	.min_uV		= S2MPU02_LDO_MIN_900MV,	\
	.uV_step	= S2MPU02_LDO_STEP_12_5MV,	\
	.linear_min_sel	= S2MPU02_LDO_GROUP1_START_SEL,	\
	.n_voltages	= S2MPU02_LDO_N_VOLTAGES,	\
	.vsel_reg	= S2MPU02_REG_L3CTRL + num - 3,	\
	.vsel_mask	= S2MPU02_LDO_VSEL_MASK,	\
	.enable_reg	= S2MPU02_REG_L3CTRL + num - 3,	\
	.enable_mask	= S2MPU02_ENABLE_MASK		\
}
#define regulator_desc_s2mpu02_ldo4(num) {		\
	.name		= "LDO"#num,			\
	.id		= S2MPU02_LDO##num,		\
	.of_match	= of_match_ptr("LDO"#num),	\
	.regulators_node = of_match_ptr("regulators"),	\
	.ops		= &s2mpu02_ldo_ops,		\
	.type		= REGULATOR_VOLTAGE,		\
	.owner		= THIS_MODULE,			\
	.min_uV		= S2MPU02_LDO_MIN_1050MV,	\
	.uV_step	= S2MPU02_LDO_STEP_25MV,	\
	.linear_min_sel	= S2MPU02_LDO_GROUP2_START_SEL,	\
	.n_voltages	= S2MPU02_LDO_N_VOLTAGES,	\
	.vsel_reg	= S2MPU02_REG_L3CTRL + num - 3,	\
	.vsel_mask	= S2MPU02_LDO_VSEL_MASK,	\
	.enable_reg	= S2MPU02_REG_L3CTRL + num - 3,	\
	.enable_mask	= S2MPU02_ENABLE_MASK		\
}
#define regulator_desc_s2mpu02_ldo5(num) {		\
	.name		= "LDO"#num,			\
	.id		= S2MPU02_LDO##num,		\
	.of_match	= of_match_ptr("LDO"#num),	\
	.regulators_node = of_match_ptr("regulators"),	\
	.ops		= &s2mpu02_ldo_ops,		\
	.type		= REGULATOR_VOLTAGE,		\
	.owner		= THIS_MODULE,			\
	.min_uV		= S2MPU02_LDO_MIN_1600MV,	\
	.uV_step	= S2MPU02_LDO_STEP_50MV,	\
	.linear_min_sel	= S2MPU02_LDO_GROUP3_START_SEL,	\
	.n_voltages	= S2MPU02_LDO_N_VOLTAGES,	\
	.vsel_reg	= S2MPU02_REG_L3CTRL + num - 3,	\
	.vsel_mask	= S2MPU02_LDO_VSEL_MASK,	\
	.enable_reg	= S2MPU02_REG_L3CTRL + num - 3,	\
	.enable_mask	= S2MPU02_ENABLE_MASK		\
}

#define regulator_desc_s2mpu02_buck1234(num) {			\
	.name		= "BUCK"#num,				\
	.id		= S2MPU02_BUCK##num,			\
	.of_match	= of_match_ptr("BUCK"#num),		\
	.regulators_node = of_match_ptr("regulators"),		\
	.ops		= &s2mpu02_buck_ops,			\
	.type		= REGULATOR_VOLTAGE,			\
	.owner		= THIS_MODULE,				\
	.min_uV		= S2MPU02_BUCK1234_MIN_600MV,		\
	.uV_step	= S2MPU02_BUCK1234_STEP_6_25MV,		\
	.n_voltages	= S2MPU02_BUCK_N_VOLTAGES,		\
	.linear_min_sel = S2MPU02_BUCK1234_START_SEL,		\
	.ramp_delay	= S2MPU02_BUCK_RAMP_DELAY,		\
	.vsel_reg	= S2MPU02_REG_B1CTRL2 + (num - 1) * 2,	\
	.vsel_mask	= S2MPU02_BUCK_VSEL_MASK,		\
	.enable_reg	= S2MPU02_REG_B1CTRL1 + (num - 1) * 2,	\
	.enable_mask	= S2MPU02_ENABLE_MASK			\
}
#define regulator_desc_s2mpu02_buck5(num) {			\
	.name		= "BUCK"#num,				\
	.id		= S2MPU02_BUCK##num,			\
	.of_match	= of_match_ptr("BUCK"#num),		\
	.regulators_node = of_match_ptr("regulators"),		\
	.ops		= &s2mpu02_ldo_ops,			\
	.type		= REGULATOR_VOLTAGE,			\
	.owner		= THIS_MODULE,				\
	.min_uV		= S2MPU02_BUCK5_MIN_1081_25MV,		\
	.uV_step	= S2MPU02_BUCK5_STEP_6_25MV,		\
	.n_voltages	= S2MPU02_BUCK_N_VOLTAGES,		\
	.linear_min_sel = S2MPU02_BUCK5_START_SEL,		\
	.ramp_delay	= S2MPU02_BUCK_RAMP_DELAY,		\
	.vsel_reg	= S2MPU02_REG_B5CTRL2,			\
	.vsel_mask	= S2MPU02_BUCK_VSEL_MASK,		\
	.enable_reg	= S2MPU02_REG_B5CTRL1,			\
	.enable_mask	= S2MPU02_ENABLE_MASK			\
}
#define regulator_desc_s2mpu02_buck6(num) {			\
	.name		= "BUCK"#num,				\
	.id		= S2MPU02_BUCK##num,			\
	.of_match	= of_match_ptr("BUCK"#num),		\
	.regulators_node = of_match_ptr("regulators"),		\
	.ops		= &s2mpu02_ldo_ops,			\
	.type		= REGULATOR_VOLTAGE,			\
	.owner		= THIS_MODULE,				\
	.min_uV		= S2MPU02_BUCK6_MIN_1700MV,		\
	.uV_step	= S2MPU02_BUCK6_STEP_2_50MV,		\
	.n_voltages	= S2MPU02_BUCK_N_VOLTAGES,		\
	.linear_min_sel = S2MPU02_BUCK6_START_SEL,		\
	.ramp_delay	= S2MPU02_BUCK_RAMP_DELAY,		\
	.vsel_reg	= S2MPU02_REG_B6CTRL2,			\
	.vsel_mask	= S2MPU02_BUCK_VSEL_MASK,		\
	.enable_reg	= S2MPU02_REG_B6CTRL1,			\
	.enable_mask	= S2MPU02_ENABLE_MASK			\
}
#define regulator_desc_s2mpu02_buck7(num) {			\
	.name		= "BUCK"#num,				\
	.id		= S2MPU02_BUCK##num,			\
	.of_match	= of_match_ptr("BUCK"#num),		\
	.regulators_node = of_match_ptr("regulators"),		\
	.ops		= &s2mpu02_ldo_ops,			\
	.type		= REGULATOR_VOLTAGE,			\
	.owner		= THIS_MODULE,				\
	.min_uV		= S2MPU02_BUCK7_MIN_900MV,		\
	.uV_step	= S2MPU02_BUCK7_STEP_6_25MV,		\
	.n_voltages	= S2MPU02_BUCK_N_VOLTAGES,		\
	.linear_min_sel = S2MPU02_BUCK7_START_SEL,		\
	.ramp_delay	= S2MPU02_BUCK_RAMP_DELAY,		\
	.vsel_reg	= S2MPU02_REG_B7CTRL2,			\
	.vsel_mask	= S2MPU02_BUCK_VSEL_MASK,		\
	.enable_reg	= S2MPU02_REG_B7CTRL1,			\
	.enable_mask	= S2MPU02_ENABLE_MASK			\
}

static const struct regulator_desc s2mpu02_regulators[] = {
	regulator_desc_s2mpu02_ldo1(1),
	regulator_desc_s2mpu02_ldo2(2),
	regulator_desc_s2mpu02_ldo4(3),
	regulator_desc_s2mpu02_ldo5(4),
	regulator_desc_s2mpu02_ldo4(5),
	regulator_desc_s2mpu02_ldo3(6),
	regulator_desc_s2mpu02_ldo3(7),
	regulator_desc_s2mpu02_ldo4(8),
	regulator_desc_s2mpu02_ldo5(9),
	regulator_desc_s2mpu02_ldo3(10),
	regulator_desc_s2mpu02_ldo4(11),
	regulator_desc_s2mpu02_ldo5(12),
	regulator_desc_s2mpu02_ldo5(13),
	regulator_desc_s2mpu02_ldo5(14),
	regulator_desc_s2mpu02_ldo5(15),
	regulator_desc_s2mpu02_ldo5(16),
	regulator_desc_s2mpu02_ldo4(17),
	regulator_desc_s2mpu02_ldo5(18),
	regulator_desc_s2mpu02_ldo3(19),
	regulator_desc_s2mpu02_ldo4(20),
	regulator_desc_s2mpu02_ldo5(21),
	regulator_desc_s2mpu02_ldo5(22),
	regulator_desc_s2mpu02_ldo5(23),
	regulator_desc_s2mpu02_ldo4(24),
	regulator_desc_s2mpu02_ldo5(25),
	regulator_desc_s2mpu02_ldo4(26),
	regulator_desc_s2mpu02_ldo5(27),
	regulator_desc_s2mpu02_ldo5(28),
	regulator_desc_s2mpu02_buck1234(1),
	regulator_desc_s2mpu02_buck1234(2),
	regulator_desc_s2mpu02_buck1234(3),
	regulator_desc_s2mpu02_buck1234(4),
	regulator_desc_s2mpu02_buck5(5),
	regulator_desc_s2mpu02_buck6(6),
	regulator_desc_s2mpu02_buck7(7),
};

#define regulator_desc_s2mpu05_ldo_reg(num, min, step, reg) {	\
	.name		= "ldo"#num,				\
	.id		= S2MPU05_LDO##num,			\
	.of_match	= of_match_ptr("ldo"#num),		\
	.regulators_node = of_match_ptr("regulators"),		\
	.ops		= &s2mpu02_ldo_ops,			\
	.type		= REGULATOR_VOLTAGE,			\
	.owner		= THIS_MODULE,				\
	.min_uV		= min,					\
	.uV_step	= step,					\
	.n_voltages	= S2MPU05_LDO_N_VOLTAGES,		\
	.vsel_reg	= reg,					\
	.vsel_mask	= S2MPU05_LDO_VSEL_MASK,		\
	.enable_reg	= reg,					\
	.enable_mask	= S2MPU05_ENABLE_MASK,			\
	.enable_time	= S2MPU05_ENABLE_TIME_LDO		\
}

#define regulator_desc_s2mpu05_ldo(num, reg, min, step) \
	regulator_desc_s2mpu05_ldo_reg(num, min, step, S2MPU05_REG_L##num##reg)

#define regulator_desc_s2mpu05_ldo1(num, reg) \
	regulator_desc_s2mpu05_ldo(num, reg, S2MPU05_LDO_MIN1, S2MPU05_LDO_STEP1)

#define regulator_desc_s2mpu05_ldo2(num, reg) \
	regulator_desc_s2mpu05_ldo(num, reg, S2MPU05_LDO_MIN1, S2MPU05_LDO_STEP2)

#define regulator_desc_s2mpu05_ldo3(num, reg) \
	regulator_desc_s2mpu05_ldo(num, reg, S2MPU05_LDO_MIN2, S2MPU05_LDO_STEP2)

#define regulator_desc_s2mpu05_ldo4(num, reg) \
	regulator_desc_s2mpu05_ldo(num, reg, S2MPU05_LDO_MIN3, S2MPU05_LDO_STEP2)

#define regulator_desc_s2mpu05_buck(num, which) {	\
	.name		= "buck"#num,			\
	.id		= S2MPU05_BUCK##num,		\
	.of_match	= of_match_ptr("buck"#num),	\
	.regulators_node = of_match_ptr("regulators"),	\
	.ops		= &s2mpu02_buck_ops,		\
	.type		= REGULATOR_VOLTAGE,		\
	.owner		= THIS_MODULE,			\
	.min_uV		= S2MPU05_BUCK_MIN##which,	\
	.uV_step	= S2MPU05_BUCK_STEP##which,	\
	.n_voltages	= S2MPU05_BUCK_N_VOLTAGES,	\
	.vsel_reg	= S2MPU05_REG_B##num##CTRL2,	\
	.vsel_mask	= S2MPU05_BUCK_VSEL_MASK,	\
	.enable_reg	= S2MPU05_REG_B##num##CTRL1,	\
	.enable_mask	= S2MPU05_ENABLE_MASK,		\
	.enable_time	= S2MPU05_ENABLE_TIME_BUCK##num	\
}

#define regulator_desc_s2mpu05_buck123(num) regulator_desc_s2mpu05_buck(num, 1)
#define regulator_desc_s2mpu05_buck45(num) regulator_desc_s2mpu05_buck(num, 2)

static const struct regulator_desc s2mpu05_regulators[] = {
	regulator_desc_s2mpu05_ldo4(1, CTRL),
	regulator_desc_s2mpu05_ldo3(2, CTRL),
	regulator_desc_s2mpu05_ldo2(3, CTRL),
	regulator_desc_s2mpu05_ldo1(4, CTRL),
	regulator_desc_s2mpu05_ldo1(5, CTRL),
	regulator_desc_s2mpu05_ldo1(6, CTRL),
	regulator_desc_s2mpu05_ldo2(7, CTRL),
	regulator_desc_s2mpu05_ldo3(8, CTRL),
	regulator_desc_s2mpu05_ldo4(9, CTRL1),
	regulator_desc_s2mpu05_ldo4(10, CTRL),
	/* LDOs 11-24 are used for CP. They aren't documented. */
	regulator_desc_s2mpu05_ldo2(25, CTRL),
	regulator_desc_s2mpu05_ldo3(26, CTRL),
	regulator_desc_s2mpu05_ldo2(27, CTRL),
	regulator_desc_s2mpu05_ldo3(28, CTRL),
	regulator_desc_s2mpu05_ldo3(29, CTRL),
	regulator_desc_s2mpu05_ldo2(30, CTRL),
	regulator_desc_s2mpu05_ldo3(31, CTRL),
	regulator_desc_s2mpu05_ldo3(32, CTRL),
	regulator_desc_s2mpu05_ldo3(33, CTRL),
	regulator_desc_s2mpu05_ldo3(34, CTRL),
	regulator_desc_s2mpu05_ldo3(35, CTRL),
	regulator_desc_s2mpu05_buck123(1),
	regulator_desc_s2mpu05_buck123(2),
	regulator_desc_s2mpu05_buck123(3),
	regulator_desc_s2mpu05_buck45(4),
	regulator_desc_s2mpu05_buck45(5),
};

static int s2mps11_handle_ext_control(struct s2mps11_info *s2mps11,
				      struct regulator_dev *rdev)
{
	int ret;

	switch (s2mps11->dev_type) {
	case S2MPS14X:
		if (!rdev->ena_pin)
			return 0;

		ret = s2mps14_pmic_enable_ext_control(s2mps11, rdev);
		break;

	case S2MPG10:
	case S2MPG11:
		/*
		 * If desc.enable_val is != 0, then external control was
		 * requested. We can not test s2mpg10_desc::ext_control,
		 * because 0 is a valid value.
		 */
		if (!rdev->desc->enable_val)
			return 0;

		ret = s2mpg10_enable_ext_control(s2mps11, rdev);
		break;

	default:
		return 0;
	}

	return ret;
}

static int s2mps11_pmic_probe(struct platform_device *pdev)
{
	struct sec_pmic_dev *iodev = dev_get_drvdata(pdev->dev.parent);
	struct regulator_config config = { };
	struct s2mps11_info *s2mps11;
	unsigned int rdev_num;
	int i, ret;
	const struct regulator_desc *regulators;
	const struct s2mpg10_regulator_desc *s2mpg1x_regulators = NULL;

	s2mps11 = devm_kzalloc(&pdev->dev, sizeof(struct s2mps11_info),
				GFP_KERNEL);
	if (!s2mps11)
		return -ENOMEM;

	s2mps11->dev_type = platform_get_device_id(pdev)->driver_data;
	switch (s2mps11->dev_type) {
	case S2MPG10:
		rdev_num = ARRAY_SIZE(s2mpg10_regulators);
		s2mpg1x_regulators = s2mpg10_regulators;
		BUILD_BUG_ON(ARRAY_SIZE(s2mpg10_regulators) > S2MPS_REGULATOR_MAX);
		break;
	case S2MPG11:
		rdev_num = ARRAY_SIZE(s2mpg11_regulators);
		s2mpg1x_regulators = s2mpg11_regulators;
		BUILD_BUG_ON(ARRAY_SIZE(s2mpg11_regulators) > S2MPS_REGULATOR_MAX);
		break;
	case S2MPS11X:
		rdev_num = ARRAY_SIZE(s2mps11_regulators);
		regulators = s2mps11_regulators;
		BUILD_BUG_ON(ARRAY_SIZE(s2mps11_regulators) > S2MPS_REGULATOR_MAX);
		break;
	case S2MPS13X:
		rdev_num = ARRAY_SIZE(s2mps13_regulators);
		regulators = s2mps13_regulators;
		BUILD_BUG_ON(ARRAY_SIZE(s2mps13_regulators) > S2MPS_REGULATOR_MAX);
		break;
	case S2MPS14X:
		rdev_num = ARRAY_SIZE(s2mps14_regulators);
		regulators = s2mps14_regulators;
		BUILD_BUG_ON(ARRAY_SIZE(s2mps14_regulators) > S2MPS_REGULATOR_MAX);
		break;
	case S2MPS15X:
		rdev_num = ARRAY_SIZE(s2mps15_regulators);
		regulators = s2mps15_regulators;
		BUILD_BUG_ON(ARRAY_SIZE(s2mps15_regulators) > S2MPS_REGULATOR_MAX);
		break;
	case S2MPU02:
		rdev_num = ARRAY_SIZE(s2mpu02_regulators);
		regulators = s2mpu02_regulators;
		BUILD_BUG_ON(ARRAY_SIZE(s2mpu02_regulators) > S2MPS_REGULATOR_MAX);
		break;
	case S2MPU05:
		rdev_num = ARRAY_SIZE(s2mpu05_regulators);
		regulators = s2mpu05_regulators;
		BUILD_BUG_ON(ARRAY_SIZE(s2mpu05_regulators) > S2MPS_REGULATOR_MAX);
		break;
	default:
		return dev_err_probe(&pdev->dev, -ENODEV,
				     "Unsupported device type %d\n",
				     s2mps11->dev_type);
	}

	if (s2mpg1x_regulators) {
		size_t regulators_sz = rdev_num * sizeof(*s2mpg1x_regulators);

		s2mpg1x_regulators = devm_kmemdup(&pdev->dev,
						  s2mpg1x_regulators,
						  regulators_sz,
						  GFP_KERNEL);
		if (!s2mpg1x_regulators)
			return -ENOMEM;
	}

	device_set_of_node_from_dev(&pdev->dev, pdev->dev.parent);

	platform_set_drvdata(pdev, s2mps11);

	config.dev = &pdev->dev;
	config.regmap = iodev->regmap_pmic;
	config.driver_data = s2mps11;
	for (i = 0; i < rdev_num; i++) {
		const struct regulator_desc *rdesc;
		struct regulator_dev *regulator;

		if (s2mpg1x_regulators)
			rdesc = &s2mpg1x_regulators[i].desc;
		else
			rdesc = &regulators[i];

		regulator = devm_regulator_register(&pdev->dev,
						    rdesc, &config);
		if (IS_ERR(regulator))
			return dev_err_probe(&pdev->dev, PTR_ERR(regulator),
					     "regulator init failed for %d/%s\n",
					     rdesc->id, rdesc->name);

		ret = s2mps11_handle_ext_control(s2mps11, regulator);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static const struct platform_device_id s2mps11_pmic_id[] = {
	{ "s2mpg10-regulator", S2MPG10},
	{ "s2mpg11-regulator", S2MPG11},
	{ "s2mps11-regulator", S2MPS11X},
	{ "s2mps13-regulator", S2MPS13X},
	{ "s2mps14-regulator", S2MPS14X},
	{ "s2mps15-regulator", S2MPS15X},
	{ "s2mpu02-regulator", S2MPU02},
	{ "s2mpu05-regulator", S2MPU05},
	{ },
};
MODULE_DEVICE_TABLE(platform, s2mps11_pmic_id);

static struct platform_driver s2mps11_pmic_driver = {
	.driver = {
		.name = "s2mps11-pmic",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
	.probe = s2mps11_pmic_probe,
	.id_table = s2mps11_pmic_id,
};

module_platform_driver(s2mps11_pmic_driver);

/* Module information */
MODULE_AUTHOR("Sangbeom Kim <sbkim73@samsung.com>");
MODULE_DESCRIPTION("Samsung S2MPS11/14/15/S2MPU02/05 Regulator Driver");
MODULE_LICENSE("GPL");
