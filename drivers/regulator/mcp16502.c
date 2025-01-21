// SPDX-License-Identifier: GPL-2.0
//
// MCP16502 PMIC driver
//
// Copyright (C) 2018 Microchip Technology Inc. and its subsidiaries
//
// Author: Andrei Stefanescu <andrei.stefanescu@microchip.com>
//
// Inspired from tps65086-regulator.c

#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/suspend.h>
#include <linux/gpio/consumer.h>

#define VDD_LOW_SEL 0x0D
#define VDD_HIGH_SEL 0x3F

#define MCP16502_FLT		BIT(7)
#define MCP16502_DVSR		GENMASK(3, 2)
#define MCP16502_ENS		BIT(0)

/*
 * The PMIC has four sets of registers corresponding to four power modes:
 * Performance, Active, Low-power, Hibernate.
 *
 * Registers:
 * Each regulator has a register for each power mode. To access a register
 * for a specific regulator and mode BASE_* and OFFSET_* need to be added.
 *
 * Operating modes:
 * In order for the PMIC to transition to operating modes it has to be
 * controlled via GPIO lines called LPM and HPM.
 *
 * The registers are fully configurable such that you can put all regulators in
 * a low-power state while the PMIC is in Active mode. They are supposed to be
 * configured at startup and then simply transition to/from a global low-power
 * state by setting the GPIO lpm pin high/low.
 *
 * This driver keeps the PMIC in Active mode, Low-power state is set for the
 * regulators by enabling/disabling operating mode (FPWM or Auto PFM).
 *
 * The PMIC's Low-power and Hibernate modes are used during standby/suspend.
 * To enter standby/suspend the PMIC will go to Low-power mode. From there, it
 * will transition to Hibernate when the PWRHLD line is set to low by the MPU.
 */

/*
 * This function is useful for iterating over all regulators and accessing their
 * registers in a generic way or accessing a regulator device by its id.
 */
#define MCP16502_REG_BASE(i, r) ((((i) + 1) << 4) + MCP16502_REG_##r)
#define MCP16502_STAT_BASE(i) ((i) + 5)

#define MCP16502_OPMODE_ACTIVE REGULATOR_MODE_NORMAL
#define MCP16502_OPMODE_LPM REGULATOR_MODE_IDLE
#define MCP16502_OPMODE_HIB REGULATOR_MODE_STANDBY

#define MCP16502_MODE_AUTO_PFM 0
#define MCP16502_MODE_FPWM BIT(6)

#define MCP16502_VSEL 0x3F
#define MCP16502_EN BIT(7)
#define MCP16502_MODE BIT(6)

#define MCP16502_MIN_REG 0x0
#define MCP16502_MAX_REG 0x65

/**
 * enum mcp16502_reg - MCP16502 regulators's registers
 * @MCP16502_REG_A: active state register
 * @MCP16502_REG_LPM: low power mode state register
 * @MCP16502_REG_HIB: hibernate state register
 * @MCP16502_REG_HPM: high-performance mode register
 * @MCP16502_REG_SEQ: startup sequence register
 * @MCP16502_REG_CFG: configuration register
 */
enum mcp16502_reg {
	MCP16502_REG_A,
	MCP16502_REG_LPM,
	MCP16502_REG_HIB,
	MCP16502_REG_HPM,
	MCP16502_REG_SEQ,
	MCP16502_REG_CFG,
};

/* Ramp delay (uV/us) for buck1, ldo1, ldo2. */
static const unsigned int mcp16502_ramp_b1l12[] = {
	6250, 3125, 2083, 1563
};

/* Ramp delay (uV/us) for buck2, buck3, buck4. */
static const unsigned int mcp16502_ramp_b234[] = {
	3125, 1563, 1042, 781
};

static unsigned int mcp16502_of_map_mode(unsigned int mode)
{
	if (mode == REGULATOR_MODE_NORMAL || mode == REGULATOR_MODE_IDLE)
		return mode;

	return REGULATOR_MODE_INVALID;
}

#define MCP16502_REGULATOR(_name, _id, _sn, _ranges, _ops, _ramp_table)	\
	[_id] = {							\
		.name			= _name,			\
		.supply_name		= #_sn,				\
		.regulators_node	= "regulators",			\
		.id			= _id,				\
		.ops			= &(_ops),			\
		.type			= REGULATOR_VOLTAGE,		\
		.owner			= THIS_MODULE,			\
		.n_voltages		= MCP16502_VSEL + 1,		\
		.linear_ranges		= _ranges,			\
		.linear_min_sel		= VDD_LOW_SEL,			\
		.n_linear_ranges	= ARRAY_SIZE(_ranges),		\
		.of_match		= _name,			\
		.of_map_mode		= mcp16502_of_map_mode,		\
		.vsel_reg		= (((_id) + 1) << 4),		\
		.vsel_mask		= MCP16502_VSEL,		\
		.enable_reg		= (((_id) + 1) << 4),		\
		.enable_mask		= MCP16502_EN,			\
		.ramp_reg		= MCP16502_REG_BASE(_id, CFG),	\
		.ramp_mask		= MCP16502_DVSR,		\
		.ramp_delay_table	= _ramp_table,			\
		.n_ramp_values		= ARRAY_SIZE(_ramp_table),	\
	}

enum {
	BUCK1 = 0,
	BUCK2,
	BUCK3,
	BUCK4,
	LDO1,
	LDO2,
	NUM_REGULATORS
};

/*
 * struct mcp16502 - PMIC representation
 * @lpm: LPM GPIO descriptor
 */
struct mcp16502 {
	struct gpio_desc *lpm;
};

/*
 * mcp16502_gpio_set_mode() - set the GPIO corresponding value
 *
 * Used to prepare transitioning into hibernate or resuming from it.
 */
static void mcp16502_gpio_set_mode(struct mcp16502 *mcp, int mode)
{
	switch (mode) {
	case MCP16502_OPMODE_ACTIVE:
		gpiod_set_value(mcp->lpm, 0);
		break;
	case MCP16502_OPMODE_LPM:
	case MCP16502_OPMODE_HIB:
		gpiod_set_value(mcp->lpm, 1);
		break;
	default:
		pr_err("%s: %d invalid\n", __func__, mode);
	}
}

/*
 * mcp16502_get_reg() - get the PMIC's state configuration register for opmode
 *
 * @rdev: the regulator whose register we are searching
 * @opmode: the PMIC's operating mode ACTIVE, Low-power, Hibernate
 */
static int mcp16502_get_state_reg(struct regulator_dev *rdev, int opmode)
{
	switch (opmode) {
	case MCP16502_OPMODE_ACTIVE:
		return MCP16502_REG_BASE(rdev_get_id(rdev), A);
	case MCP16502_OPMODE_LPM:
		return MCP16502_REG_BASE(rdev_get_id(rdev), LPM);
	case MCP16502_OPMODE_HIB:
		return MCP16502_REG_BASE(rdev_get_id(rdev), HIB);
	default:
		return -EINVAL;
	}
}

/*
 * mcp16502_get_mode() - return the current operating mode of a regulator
 *
 * Note: all functions that are not part of entering/exiting standby/suspend
 *	 use the Active mode registers.
 *
 * Note: this is different from the PMIC's operatig mode, it is the
 *	 MODE bit from the regulator's register.
 */
static unsigned int mcp16502_get_mode(struct regulator_dev *rdev)
{
	unsigned int val;
	int ret, reg;

	reg = mcp16502_get_state_reg(rdev, MCP16502_OPMODE_ACTIVE);
	if (reg < 0)
		return reg;

	ret = regmap_read(rdev->regmap, reg, &val);
	if (ret)
		return ret;

	switch (val & MCP16502_MODE) {
	case MCP16502_MODE_FPWM:
		return REGULATOR_MODE_NORMAL;
	case MCP16502_MODE_AUTO_PFM:
		return REGULATOR_MODE_IDLE;
	default:
		return REGULATOR_MODE_INVALID;
	}
}

/*
 * _mcp16502_set_mode() - helper for set_mode and set_suspend_mode
 *
 * @rdev: the regulator for which we are setting the mode
 * @mode: the regulator's mode (the one from MODE bit)
 * @opmode: the PMIC's operating mode: Active/Low-power/Hibernate
 */
static int _mcp16502_set_mode(struct regulator_dev *rdev, unsigned int mode,
			      unsigned int op_mode)
{
	int val;
	int reg;

	reg = mcp16502_get_state_reg(rdev, op_mode);
	if (reg < 0)
		return reg;

	switch (mode) {
	case REGULATOR_MODE_NORMAL:
		val = MCP16502_MODE_FPWM;
		break;
	case REGULATOR_MODE_IDLE:
		val = MCP16502_MODE_AUTO_PFM;
		break;
	default:
		return -EINVAL;
	}

	reg = regmap_update_bits(rdev->regmap, reg, MCP16502_MODE, val);
	return reg;
}

/*
 * mcp16502_set_mode() - regulator_ops set_mode
 */
static int mcp16502_set_mode(struct regulator_dev *rdev, unsigned int mode)
{
	return _mcp16502_set_mode(rdev, mode, MCP16502_OPMODE_ACTIVE);
}

/*
 * mcp16502_get_status() - regulator_ops get_status
 */
static int mcp16502_get_status(struct regulator_dev *rdev)
{
	int ret;
	unsigned int val;

	ret = regmap_read(rdev->regmap, MCP16502_STAT_BASE(rdev_get_id(rdev)),
			  &val);
	if (ret)
		return ret;

	if (val & MCP16502_FLT)
		return REGULATOR_STATUS_ERROR;
	else if (val & MCP16502_ENS)
		return REGULATOR_STATUS_ON;
	else if (!(val & MCP16502_ENS))
		return REGULATOR_STATUS_OFF;

	return REGULATOR_STATUS_UNDEFINED;
}

static int mcp16502_set_voltage_time_sel(struct regulator_dev *rdev,
					 unsigned int old_sel,
					 unsigned int new_sel)
{
	static const u8 us_ramp[] = { 8, 16, 24, 32 };
	int id = rdev_get_id(rdev);
	unsigned int uV_delta, val;
	int ret;

	ret = regmap_read(rdev->regmap, MCP16502_REG_BASE(id, CFG), &val);
	if (ret)
		return ret;

	val = (val & MCP16502_DVSR) >> 2;
	uV_delta = abs(new_sel * rdev->desc->linear_ranges->step -
		       old_sel * rdev->desc->linear_ranges->step);
	switch (id) {
	case BUCK1:
	case LDO1:
	case LDO2:
		ret = DIV_ROUND_CLOSEST(uV_delta * us_ramp[val],
					mcp16502_ramp_b1l12[val]);
		break;

	case BUCK2:
	case BUCK3:
	case BUCK4:
		ret = DIV_ROUND_CLOSEST(uV_delta * us_ramp[val],
					mcp16502_ramp_b234[val]);
		break;

	default:
		return -EINVAL;
	}

	return ret;
}

#ifdef CONFIG_SUSPEND
/*
 * mcp16502_suspend_get_target_reg() - get the reg of the target suspend PMIC
 *				       mode
 */
static int mcp16502_suspend_get_target_reg(struct regulator_dev *rdev)
{
	switch (pm_suspend_target_state) {
	case PM_SUSPEND_STANDBY:
		return mcp16502_get_state_reg(rdev, MCP16502_OPMODE_LPM);
	case PM_SUSPEND_ON:
	case PM_SUSPEND_MEM:
		return mcp16502_get_state_reg(rdev, MCP16502_OPMODE_HIB);
	default:
		dev_err(&rdev->dev, "invalid suspend target: %d\n",
			pm_suspend_target_state);
	}

	return -EINVAL;
}

/*
 * mcp16502_set_suspend_voltage() - regulator_ops set_suspend_voltage
 */
static int mcp16502_set_suspend_voltage(struct regulator_dev *rdev, int uV)
{
	int sel = regulator_map_voltage_linear_range(rdev, uV, uV);
	int reg = mcp16502_suspend_get_target_reg(rdev);

	if (sel < 0)
		return sel;

	if (reg < 0)
		return reg;

	return regmap_update_bits(rdev->regmap, reg, MCP16502_VSEL, sel);
}

/*
 * mcp16502_set_suspend_mode() - regulator_ops set_suspend_mode
 */
static int mcp16502_set_suspend_mode(struct regulator_dev *rdev,
				     unsigned int mode)
{
	switch (pm_suspend_target_state) {
	case PM_SUSPEND_STANDBY:
		return _mcp16502_set_mode(rdev, mode, MCP16502_OPMODE_LPM);
	case PM_SUSPEND_ON:
	case PM_SUSPEND_MEM:
		return _mcp16502_set_mode(rdev, mode, MCP16502_OPMODE_HIB);
	default:
		dev_err(&rdev->dev, "invalid suspend target: %d\n",
			pm_suspend_target_state);
	}

	return -EINVAL;
}

/*
 * mcp16502_set_suspend_enable() - regulator_ops set_suspend_enable
 */
static int mcp16502_set_suspend_enable(struct regulator_dev *rdev)
{
	int reg = mcp16502_suspend_get_target_reg(rdev);

	if (reg < 0)
		return reg;

	return regmap_update_bits(rdev->regmap, reg, MCP16502_EN, MCP16502_EN);
}

/*
 * mcp16502_set_suspend_disable() - regulator_ops set_suspend_disable
 */
static int mcp16502_set_suspend_disable(struct regulator_dev *rdev)
{
	int reg = mcp16502_suspend_get_target_reg(rdev);

	if (reg < 0)
		return reg;

	return regmap_update_bits(rdev->regmap, reg, MCP16502_EN, 0);
}
#endif /* CONFIG_SUSPEND */

static const struct regulator_ops mcp16502_buck_ops = {
	.list_voltage			= regulator_list_voltage_linear_range,
	.map_voltage			= regulator_map_voltage_linear_range,
	.get_voltage_sel		= regulator_get_voltage_sel_regmap,
	.set_voltage_sel		= regulator_set_voltage_sel_regmap,
	.enable				= regulator_enable_regmap,
	.disable			= regulator_disable_regmap,
	.is_enabled			= regulator_is_enabled_regmap,
	.get_status			= mcp16502_get_status,
	.set_voltage_time_sel		= mcp16502_set_voltage_time_sel,
	.set_ramp_delay			= regulator_set_ramp_delay_regmap,

	.set_mode			= mcp16502_set_mode,
	.get_mode			= mcp16502_get_mode,

#ifdef CONFIG_SUSPEND
	.set_suspend_voltage		= mcp16502_set_suspend_voltage,
	.set_suspend_mode		= mcp16502_set_suspend_mode,
	.set_suspend_enable		= mcp16502_set_suspend_enable,
	.set_suspend_disable		= mcp16502_set_suspend_disable,
#endif /* CONFIG_SUSPEND */
};

/*
 * LDOs cannot change operating modes.
 */
static const struct regulator_ops mcp16502_ldo_ops = {
	.list_voltage			= regulator_list_voltage_linear_range,
	.map_voltage			= regulator_map_voltage_linear_range,
	.get_voltage_sel		= regulator_get_voltage_sel_regmap,
	.set_voltage_sel		= regulator_set_voltage_sel_regmap,
	.enable				= regulator_enable_regmap,
	.disable			= regulator_disable_regmap,
	.is_enabled			= regulator_is_enabled_regmap,
	.get_status			= mcp16502_get_status,
	.set_voltage_time_sel		= mcp16502_set_voltage_time_sel,
	.set_ramp_delay			= regulator_set_ramp_delay_regmap,

#ifdef CONFIG_SUSPEND
	.set_suspend_voltage		= mcp16502_set_suspend_voltage,
	.set_suspend_enable		= mcp16502_set_suspend_enable,
	.set_suspend_disable		= mcp16502_set_suspend_disable,
#endif /* CONFIG_SUSPEND */
};

static const struct of_device_id mcp16502_ids[] = {
	{ .compatible = "microchip,mcp16502", },
	{}
};
MODULE_DEVICE_TABLE(of, mcp16502_ids);

static const struct linear_range b1l12_ranges[] = {
	REGULATOR_LINEAR_RANGE(1200000, VDD_LOW_SEL, VDD_HIGH_SEL, 50000),
};

static const struct linear_range b234_ranges[] = {
	REGULATOR_LINEAR_RANGE(600000, VDD_LOW_SEL, VDD_HIGH_SEL, 25000),
};

static const struct regulator_desc mcp16502_desc[] = {
	/* MCP16502_REGULATOR(_name, _id, _sn, _ranges, _ops, _ramp_table) */
	MCP16502_REGULATOR("VDD_IO", BUCK1, pvin1, b1l12_ranges, mcp16502_buck_ops,
			   mcp16502_ramp_b1l12),
	MCP16502_REGULATOR("VDD_DDR", BUCK2, pvin2, b234_ranges, mcp16502_buck_ops,
			   mcp16502_ramp_b234),
	MCP16502_REGULATOR("VDD_CORE", BUCK3, pvin3, b234_ranges, mcp16502_buck_ops,
			   mcp16502_ramp_b234),
	MCP16502_REGULATOR("VDD_OTHER", BUCK4, pvin4, b234_ranges, mcp16502_buck_ops,
			   mcp16502_ramp_b234),
	MCP16502_REGULATOR("LDO1", LDO1, lvin, b1l12_ranges, mcp16502_ldo_ops,
			   mcp16502_ramp_b1l12),
	MCP16502_REGULATOR("LDO2", LDO2, lvin, b1l12_ranges, mcp16502_ldo_ops,
			   mcp16502_ramp_b1l12)
};

static const struct regmap_range mcp16502_ranges[] = {
	regmap_reg_range(MCP16502_MIN_REG, MCP16502_MAX_REG)
};

static const struct regmap_access_table mcp16502_yes_reg_table = {
	.yes_ranges = mcp16502_ranges,
	.n_yes_ranges = ARRAY_SIZE(mcp16502_ranges),
};

static const struct regmap_config mcp16502_regmap_config = {
	.reg_bits	= 8,
	.val_bits	= 8,
	.max_register	= MCP16502_MAX_REG,
	.cache_type	= REGCACHE_NONE,
	.rd_table	= &mcp16502_yes_reg_table,
	.wr_table	= &mcp16502_yes_reg_table,
};

static int mcp16502_probe(struct i2c_client *client)
{
	struct regulator_config config = { };
	struct regulator_dev *rdev;
	struct device *dev;
	struct mcp16502 *mcp;
	struct regmap *rmap;
	int i, ret;

	dev = &client->dev;
	config.dev = dev;

	mcp = devm_kzalloc(dev, sizeof(*mcp), GFP_KERNEL);
	if (!mcp)
		return -ENOMEM;

	rmap = devm_regmap_init_i2c(client, &mcp16502_regmap_config);
	if (IS_ERR(rmap)) {
		ret = PTR_ERR(rmap);
		dev_err(dev, "regmap init failed: %d\n", ret);
		return ret;
	}

	i2c_set_clientdata(client, mcp);
	config.regmap = rmap;
	config.driver_data = mcp;

	mcp->lpm = devm_gpiod_get_optional(dev, "lpm", GPIOD_OUT_LOW);
	if (IS_ERR(mcp->lpm)) {
		dev_err(dev, "failed to get lpm pin: %ld\n", PTR_ERR(mcp->lpm));
		return PTR_ERR(mcp->lpm);
	}

	for (i = 0; i < NUM_REGULATORS; i++) {
		rdev = devm_regulator_register(dev, &mcp16502_desc[i], &config);
		if (IS_ERR(rdev)) {
			dev_err(dev,
				"failed to register %s regulator %ld\n",
				mcp16502_desc[i].name, PTR_ERR(rdev));
			return PTR_ERR(rdev);
		}
	}

	mcp16502_gpio_set_mode(mcp, MCP16502_OPMODE_ACTIVE);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int mcp16502_suspend_noirq(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct mcp16502 *mcp = i2c_get_clientdata(client);

	mcp16502_gpio_set_mode(mcp, MCP16502_OPMODE_LPM);

	return 0;
}

static int mcp16502_resume_noirq(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct mcp16502 *mcp = i2c_get_clientdata(client);

	mcp16502_gpio_set_mode(mcp, MCP16502_OPMODE_ACTIVE);

	return 0;
}
#endif

#ifdef CONFIG_PM
static const struct dev_pm_ops mcp16502_pm_ops = {
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(mcp16502_suspend_noirq,
				      mcp16502_resume_noirq)
};
#endif
static const struct i2c_device_id mcp16502_i2c_id[] = {
	{ "mcp16502" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, mcp16502_i2c_id);

static struct i2c_driver mcp16502_drv = {
	.probe		= mcp16502_probe,
	.driver		= {
		.name	= "mcp16502-regulator",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table	= mcp16502_ids,
#ifdef CONFIG_PM
		.pm = &mcp16502_pm_ops,
#endif
	},
	.id_table	= mcp16502_i2c_id,
};

module_i2c_driver(mcp16502_drv);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("MCP16502 PMIC driver");
MODULE_AUTHOR("Andrei Stefanescu andrei.stefanescu@microchip.com");
