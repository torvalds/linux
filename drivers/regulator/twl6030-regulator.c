// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Split TWL6030 logic from twl-regulator.c:
 * Copyright (C) 2008 David Brownell
 *
 * Copyright (C) 2016 Nicolae Rosia <nicolae.rosia@gmail.com>
 */

#include <linux/module.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/mfd/twl.h>
#include <linux/delay.h>

struct twlreg_info {
	/* start of regulator's PM_RECEIVER control register bank */
	u8			base;

	/* twl resource ID, for resource control state machine */
	u8			id;

	u8			flags;

	/* used by regulator core */
	struct regulator_desc	desc;

	/* chip specific features */
	unsigned long		features;

	/* data passed from board for external get/set voltage */
	void			*data;
};


/* LDO control registers ... offset is from the base of its register bank.
 * The first three registers of all power resource banks help hardware to
 * manage the various resource groups.
 */
/* Common offset in TWL4030/6030 */
#define VREG_GRP		0
/* TWL6030 register offsets */
#define VREG_TRANS		1
#define VREG_STATE		2
#define VREG_VOLTAGE		3
#define VREG_VOLTAGE_SMPS	4
/* TWL6030 Misc register offsets */
#define VREG_BC_ALL		1
#define VREG_BC_REF		2
#define VREG_BC_PROC		3
#define VREG_BC_CLK_RST		4

/* TWL6030 LDO register values for VREG_VOLTAGE */
#define TWL6030_VREG_VOLTAGE_WR_S   BIT(7)

/* TWL6030 LDO register values for CFG_STATE */
#define TWL6030_CFG_STATE_OFF	0x00
#define TWL6030_CFG_STATE_ON	0x01
#define TWL6030_CFG_STATE_OFF2	0x02
#define TWL6030_CFG_STATE_SLEEP	0x03
#define TWL6030_CFG_STATE_GRP_SHIFT	5
#define TWL6030_CFG_STATE_APP_SHIFT	2
#define TWL6030_CFG_STATE_APP_MASK	(0x03 << TWL6030_CFG_STATE_APP_SHIFT)
#define TWL6030_CFG_STATE_APP(v)	(((v) & TWL6030_CFG_STATE_APP_MASK) >>\
						TWL6030_CFG_STATE_APP_SHIFT)

/* Flags for SMPS Voltage reading and LDO reading*/
#define SMPS_OFFSET_EN		BIT(0)
#define SMPS_EXTENDED_EN	BIT(1)
#define TWL_6030_WARM_RESET	BIT(3)

/* twl6032 SMPS EPROM values */
#define TWL6030_SMPS_OFFSET		0xB0
#define TWL6030_SMPS_MULT		0xB3
#define SMPS_MULTOFFSET_SMPS4	BIT(0)
#define SMPS_MULTOFFSET_VIO	BIT(1)
#define SMPS_MULTOFFSET_SMPS3	BIT(6)

static inline int
twlreg_read(struct twlreg_info *info, unsigned slave_subgp, unsigned offset)
{
	u8 value;
	int status;

	status = twl_i2c_read_u8(slave_subgp,
			&value, info->base + offset);
	return (status < 0) ? status : value;
}

static inline int
twlreg_write(struct twlreg_info *info, unsigned slave_subgp, unsigned offset,
						 u8 value)
{
	return twl_i2c_write_u8(slave_subgp,
			value, info->base + offset);
}

/* generic power resource operations, which work on all regulators */
static int twlreg_grp(struct regulator_dev *rdev)
{
	return twlreg_read(rdev_get_drvdata(rdev), TWL_MODULE_PM_RECEIVER,
								 VREG_GRP);
}

/*
 * Enable/disable regulators by joining/leaving the P1 (processor) group.
 * We assume nobody else is updating the DEV_GRP registers.
 */
/* definition for 6030 family */
#define P3_GRP_6030	BIT(2)		/* secondary processor, modem, etc */
#define P2_GRP_6030	BIT(1)		/* "peripherals" */
#define P1_GRP_6030	BIT(0)		/* CPU/Linux */

static int twl6030reg_is_enabled(struct regulator_dev *rdev)
{
	struct twlreg_info	*info = rdev_get_drvdata(rdev);
	int			grp = 0, val;

	if (!(twl_class_is_6030() && (info->features & TWL6032_SUBCLASS))) {
		grp = twlreg_grp(rdev);
		if (grp < 0)
			return grp;
		grp &= P1_GRP_6030;
	} else {
		grp = 1;
	}

	val = twlreg_read(info, TWL_MODULE_PM_RECEIVER, VREG_STATE);
	val = TWL6030_CFG_STATE_APP(val);

	return grp && (val == TWL6030_CFG_STATE_ON);
}

#define PB_I2C_BUSY	BIT(0)
#define PB_I2C_BWEN	BIT(1)


static int twl6030reg_enable(struct regulator_dev *rdev)
{
	struct twlreg_info	*info = rdev_get_drvdata(rdev);
	int			grp = 0;
	int			ret;

	if (!(twl_class_is_6030() && (info->features & TWL6032_SUBCLASS)))
		grp = twlreg_grp(rdev);
	if (grp < 0)
		return grp;

	ret = twlreg_write(info, TWL_MODULE_PM_RECEIVER, VREG_STATE,
			grp << TWL6030_CFG_STATE_GRP_SHIFT |
			TWL6030_CFG_STATE_ON);
	return ret;
}

static int twl6030reg_disable(struct regulator_dev *rdev)
{
	struct twlreg_info	*info = rdev_get_drvdata(rdev);
	int			grp = 0;
	int			ret;

	if (!(twl_class_is_6030() && (info->features & TWL6032_SUBCLASS)))
		grp = P1_GRP_6030 | P2_GRP_6030 | P3_GRP_6030;

	/* For 6030, set the off state for all grps enabled */
	ret = twlreg_write(info, TWL_MODULE_PM_RECEIVER, VREG_STATE,
			(grp) << TWL6030_CFG_STATE_GRP_SHIFT |
			TWL6030_CFG_STATE_OFF);

	return ret;
}

static int twl6030reg_get_status(struct regulator_dev *rdev)
{
	struct twlreg_info	*info = rdev_get_drvdata(rdev);
	int			val;

	val = twlreg_grp(rdev);
	if (val < 0)
		return val;

	val = twlreg_read(info, TWL_MODULE_PM_RECEIVER, VREG_STATE);

	switch (TWL6030_CFG_STATE_APP(val)) {
	case TWL6030_CFG_STATE_ON:
		return REGULATOR_STATUS_NORMAL;

	case TWL6030_CFG_STATE_SLEEP:
		return REGULATOR_STATUS_STANDBY;

	case TWL6030_CFG_STATE_OFF:
	case TWL6030_CFG_STATE_OFF2:
	default:
		break;
	}

	return REGULATOR_STATUS_OFF;
}

static int twl6030reg_set_mode(struct regulator_dev *rdev, unsigned mode)
{
	struct twlreg_info	*info = rdev_get_drvdata(rdev);
	int grp = 0;
	int val;

	if (!(twl_class_is_6030() && (info->features & TWL6032_SUBCLASS)))
		grp = twlreg_grp(rdev);

	if (grp < 0)
		return grp;

	/* Compose the state register settings */
	val = grp << TWL6030_CFG_STATE_GRP_SHIFT;
	/* We can only set the mode through state machine commands... */
	switch (mode) {
	case REGULATOR_MODE_NORMAL:
		val |= TWL6030_CFG_STATE_ON;
		break;
	case REGULATOR_MODE_STANDBY:
		val |= TWL6030_CFG_STATE_SLEEP;
		break;

	default:
		return -EINVAL;
	}

	return twlreg_write(info, TWL_MODULE_PM_RECEIVER, VREG_STATE, val);
}

static int twl6030coresmps_set_voltage(struct regulator_dev *rdev, int min_uV,
	int max_uV, unsigned *selector)
{
	return -ENODEV;
}

static int twl6030coresmps_get_voltage(struct regulator_dev *rdev)
{
	return -ENODEV;
}

static const struct regulator_ops twl6030coresmps_ops = {
	.set_voltage	= twl6030coresmps_set_voltage,
	.get_voltage	= twl6030coresmps_get_voltage,
};

static int
twl6030ldo_set_voltage_sel(struct regulator_dev *rdev, unsigned selector)
{
	struct twlreg_info	*info = rdev_get_drvdata(rdev);

	if (info->flags & TWL_6030_WARM_RESET)
		selector |= TWL6030_VREG_VOLTAGE_WR_S;

	return twlreg_write(info, TWL_MODULE_PM_RECEIVER, VREG_VOLTAGE,
			    selector);
}

static int twl6030ldo_get_voltage_sel(struct regulator_dev *rdev)
{
	struct twlreg_info	*info = rdev_get_drvdata(rdev);
	int vsel = twlreg_read(info, TWL_MODULE_PM_RECEIVER, VREG_VOLTAGE);

	if (info->flags & TWL_6030_WARM_RESET)
		vsel &= ~TWL6030_VREG_VOLTAGE_WR_S;

	return vsel;
}

static const struct regulator_ops twl6030ldo_ops = {
	.list_voltage	= regulator_list_voltage_linear_range,

	.set_voltage_sel = twl6030ldo_set_voltage_sel,
	.get_voltage_sel = twl6030ldo_get_voltage_sel,

	.enable		= twl6030reg_enable,
	.disable	= twl6030reg_disable,
	.is_enabled	= twl6030reg_is_enabled,

	.set_mode	= twl6030reg_set_mode,

	.get_status	= twl6030reg_get_status,
};

static const struct regulator_ops twl6030fixed_ops = {
	.list_voltage	= regulator_list_voltage_linear,

	.enable		= twl6030reg_enable,
	.disable	= twl6030reg_disable,
	.is_enabled	= twl6030reg_is_enabled,

	.set_mode	= twl6030reg_set_mode,

	.get_status	= twl6030reg_get_status,
};

/*
 * SMPS status and control
 */

static int twl6030smps_list_voltage(struct regulator_dev *rdev, unsigned index)
{
	struct twlreg_info	*info = rdev_get_drvdata(rdev);

	int voltage = 0;

	switch (info->flags) {
	case SMPS_OFFSET_EN:
		voltage = 100000;
		/* fall through */
	case 0:
		switch (index) {
		case 0:
			voltage = 0;
			break;
		case 58:
			voltage = 1350 * 1000;
			break;
		case 59:
			voltage = 1500 * 1000;
			break;
		case 60:
			voltage = 1800 * 1000;
			break;
		case 61:
			voltage = 1900 * 1000;
			break;
		case 62:
			voltage = 2100 * 1000;
			break;
		default:
			voltage += (600000 + (12500 * (index - 1)));
		}
		break;
	case SMPS_EXTENDED_EN:
		switch (index) {
		case 0:
			voltage = 0;
			break;
		case 58:
			voltage = 2084 * 1000;
			break;
		case 59:
			voltage = 2315 * 1000;
			break;
		case 60:
			voltage = 2778 * 1000;
			break;
		case 61:
			voltage = 2932 * 1000;
			break;
		case 62:
			voltage = 3241 * 1000;
			break;
		default:
			voltage = (1852000 + (38600 * (index - 1)));
		}
		break;
	case SMPS_OFFSET_EN | SMPS_EXTENDED_EN:
		switch (index) {
		case 0:
			voltage = 0;
			break;
		case 58:
			voltage = 4167 * 1000;
			break;
		case 59:
			voltage = 2315 * 1000;
			break;
		case 60:
			voltage = 2778 * 1000;
			break;
		case 61:
			voltage = 2932 * 1000;
			break;
		case 62:
			voltage = 3241 * 1000;
			break;
		default:
			voltage = (2161000 + (38600 * (index - 1)));
		}
		break;
	}

	return voltage;
}

static int twl6030smps_map_voltage(struct regulator_dev *rdev, int min_uV,
				   int max_uV)
{
	struct twlreg_info *info = rdev_get_drvdata(rdev);
	int vsel = 0;

	switch (info->flags) {
	case 0:
		if (min_uV == 0)
			vsel = 0;
		else if ((min_uV >= 600000) && (min_uV <= 1300000)) {
			vsel = DIV_ROUND_UP(min_uV - 600000, 12500);
			vsel++;
		}
		/* Values 1..57 for vsel are linear and can be calculated
		 * values 58..62 are non linear.
		 */
		else if ((min_uV > 1900000) && (min_uV <= 2100000))
			vsel = 62;
		else if ((min_uV > 1800000) && (min_uV <= 1900000))
			vsel = 61;
		else if ((min_uV > 1500000) && (min_uV <= 1800000))
			vsel = 60;
		else if ((min_uV > 1350000) && (min_uV <= 1500000))
			vsel = 59;
		else if ((min_uV > 1300000) && (min_uV <= 1350000))
			vsel = 58;
		else
			return -EINVAL;
		break;
	case SMPS_OFFSET_EN:
		if (min_uV == 0)
			vsel = 0;
		else if ((min_uV >= 700000) && (min_uV <= 1420000)) {
			vsel = DIV_ROUND_UP(min_uV - 700000, 12500);
			vsel++;
		}
		/* Values 1..57 for vsel are linear and can be calculated
		 * values 58..62 are non linear.
		 */
		else if ((min_uV > 1900000) && (min_uV <= 2100000))
			vsel = 62;
		else if ((min_uV > 1800000) && (min_uV <= 1900000))
			vsel = 61;
		else if ((min_uV > 1500000) && (min_uV <= 1800000))
			vsel = 60;
		else if ((min_uV > 1350000) && (min_uV <= 1500000))
			vsel = 59;
		else
			return -EINVAL;
		break;
	case SMPS_EXTENDED_EN:
		if (min_uV == 0) {
			vsel = 0;
		} else if ((min_uV >= 1852000) && (max_uV <= 4013600)) {
			vsel = DIV_ROUND_UP(min_uV - 1852000, 38600);
			vsel++;
		}
		break;
	case SMPS_OFFSET_EN|SMPS_EXTENDED_EN:
		if (min_uV == 0) {
			vsel = 0;
		} else if ((min_uV >= 2161000) && (min_uV <= 4321000)) {
			vsel = DIV_ROUND_UP(min_uV - 2161000, 38600);
			vsel++;
		}
		break;
	}

	return vsel;
}

static int twl6030smps_set_voltage_sel(struct regulator_dev *rdev,
				       unsigned int selector)
{
	struct twlreg_info *info = rdev_get_drvdata(rdev);

	return twlreg_write(info, TWL_MODULE_PM_RECEIVER, VREG_VOLTAGE_SMPS,
			    selector);
}

static int twl6030smps_get_voltage_sel(struct regulator_dev *rdev)
{
	struct twlreg_info	*info = rdev_get_drvdata(rdev);

	return twlreg_read(info, TWL_MODULE_PM_RECEIVER, VREG_VOLTAGE_SMPS);
}

static const struct regulator_ops twlsmps_ops = {
	.list_voltage		= twl6030smps_list_voltage,
	.map_voltage		= twl6030smps_map_voltage,

	.set_voltage_sel	= twl6030smps_set_voltage_sel,
	.get_voltage_sel	= twl6030smps_get_voltage_sel,

	.enable			= twl6030reg_enable,
	.disable		= twl6030reg_disable,
	.is_enabled		= twl6030reg_is_enabled,

	.set_mode		= twl6030reg_set_mode,

	.get_status		= twl6030reg_get_status,
};

/*----------------------------------------------------------------------*/
static const struct regulator_linear_range twl6030ldo_linear_range[] = {
	REGULATOR_LINEAR_RANGE(0, 0, 0, 0),
	REGULATOR_LINEAR_RANGE(1000000, 1, 24, 100000),
	REGULATOR_LINEAR_RANGE(2750000, 31, 31, 0),
};

#define TWL6030_ADJUSTABLE_SMPS(label) \
static const struct twlreg_info TWL6030_INFO_##label = { \
	.desc = { \
		.name = #label, \
		.id = TWL6030_REG_##label, \
		.ops = &twl6030coresmps_ops, \
		.type = REGULATOR_VOLTAGE, \
		.owner = THIS_MODULE, \
		}, \
	}

#define TWL6030_ADJUSTABLE_LDO(label, offset) \
static const struct twlreg_info TWL6030_INFO_##label = { \
	.base = offset, \
	.desc = { \
		.name = #label, \
		.id = TWL6030_REG_##label, \
		.n_voltages = 32, \
		.linear_ranges = twl6030ldo_linear_range, \
		.n_linear_ranges = ARRAY_SIZE(twl6030ldo_linear_range), \
		.ops = &twl6030ldo_ops, \
		.type = REGULATOR_VOLTAGE, \
		.owner = THIS_MODULE, \
		}, \
	}

#define TWL6032_ADJUSTABLE_LDO(label, offset) \
static const struct twlreg_info TWL6032_INFO_##label = { \
	.base = offset, \
	.desc = { \
		.name = #label, \
		.id = TWL6032_REG_##label, \
		.n_voltages = 32, \
		.linear_ranges = twl6030ldo_linear_range, \
		.n_linear_ranges = ARRAY_SIZE(twl6030ldo_linear_range), \
		.ops = &twl6030ldo_ops, \
		.type = REGULATOR_VOLTAGE, \
		.owner = THIS_MODULE, \
		}, \
	}

#define TWL6030_FIXED_LDO(label, offset, mVolts, turnon_delay) \
static const struct twlreg_info TWLFIXED_INFO_##label = { \
	.base = offset, \
	.id = 0, \
	.desc = { \
		.name = #label, \
		.id = TWL6030##_REG_##label, \
		.n_voltages = 1, \
		.ops = &twl6030fixed_ops, \
		.type = REGULATOR_VOLTAGE, \
		.owner = THIS_MODULE, \
		.min_uV = mVolts * 1000, \
		.enable_time = turnon_delay, \
		.of_map_mode = NULL, \
		}, \
	}

#define TWL6032_ADJUSTABLE_SMPS(label, offset) \
static const struct twlreg_info TWLSMPS_INFO_##label = { \
	.base = offset, \
	.desc = { \
		.name = #label, \
		.id = TWL6032_REG_##label, \
		.n_voltages = 63, \
		.ops = &twlsmps_ops, \
		.type = REGULATOR_VOLTAGE, \
		.owner = THIS_MODULE, \
		}, \
	}

/* VUSBCP is managed *only* by the USB subchip */
/* 6030 REG with base as PMC Slave Misc : 0x0030 */
/* Turnon-delay and remap configuration values for 6030 are not
   verified since the specification is not public */
TWL6030_ADJUSTABLE_SMPS(VDD1);
TWL6030_ADJUSTABLE_SMPS(VDD2);
TWL6030_ADJUSTABLE_SMPS(VDD3);
TWL6030_ADJUSTABLE_LDO(VAUX1_6030, 0x54);
TWL6030_ADJUSTABLE_LDO(VAUX2_6030, 0x58);
TWL6030_ADJUSTABLE_LDO(VAUX3_6030, 0x5c);
TWL6030_ADJUSTABLE_LDO(VMMC, 0x68);
TWL6030_ADJUSTABLE_LDO(VPP, 0x6c);
TWL6030_ADJUSTABLE_LDO(VUSIM, 0x74);
/* 6025 are renamed compared to 6030 versions */
TWL6032_ADJUSTABLE_LDO(LDO2, 0x54);
TWL6032_ADJUSTABLE_LDO(LDO4, 0x58);
TWL6032_ADJUSTABLE_LDO(LDO3, 0x5c);
TWL6032_ADJUSTABLE_LDO(LDO5, 0x68);
TWL6032_ADJUSTABLE_LDO(LDO1, 0x6c);
TWL6032_ADJUSTABLE_LDO(LDO7, 0x74);
TWL6032_ADJUSTABLE_LDO(LDO6, 0x60);
TWL6032_ADJUSTABLE_LDO(LDOLN, 0x64);
TWL6032_ADJUSTABLE_LDO(LDOUSB, 0x70);
TWL6030_FIXED_LDO(VANA, 0x50, 2100, 0);
TWL6030_FIXED_LDO(VCXIO, 0x60, 1800, 0);
TWL6030_FIXED_LDO(VDAC, 0x64, 1800, 0);
TWL6030_FIXED_LDO(VUSB, 0x70, 3300, 0);
TWL6030_FIXED_LDO(V1V8, 0x16, 1800, 0);
TWL6030_FIXED_LDO(V2V1, 0x1c, 2100, 0);
TWL6032_ADJUSTABLE_SMPS(SMPS3, 0x34);
TWL6032_ADJUSTABLE_SMPS(SMPS4, 0x10);
TWL6032_ADJUSTABLE_SMPS(VIO, 0x16);

static u8 twl_get_smps_offset(void)
{
	u8 value;

	twl_i2c_read_u8(TWL_MODULE_PM_RECEIVER, &value,
			TWL6030_SMPS_OFFSET);
	return value;
}

static u8 twl_get_smps_mult(void)
{
	u8 value;

	twl_i2c_read_u8(TWL_MODULE_PM_RECEIVER, &value,
			TWL6030_SMPS_MULT);
	return value;
}

#define TWL_OF_MATCH(comp, family, label) \
	{ \
		.compatible = comp, \
		.data = &family##_INFO_##label, \
	}

#define TWL6030_OF_MATCH(comp, label) TWL_OF_MATCH(comp, TWL6030, label)
#define TWL6032_OF_MATCH(comp, label) TWL_OF_MATCH(comp, TWL6032, label)
#define TWLFIXED_OF_MATCH(comp, label) TWL_OF_MATCH(comp, TWLFIXED, label)
#define TWLSMPS_OF_MATCH(comp, label) TWL_OF_MATCH(comp, TWLSMPS, label)

static const struct of_device_id twl_of_match[] = {
	TWL6030_OF_MATCH("ti,twl6030-vdd1", VDD1),
	TWL6030_OF_MATCH("ti,twl6030-vdd2", VDD2),
	TWL6030_OF_MATCH("ti,twl6030-vdd3", VDD3),
	TWL6030_OF_MATCH("ti,twl6030-vaux1", VAUX1_6030),
	TWL6030_OF_MATCH("ti,twl6030-vaux2", VAUX2_6030),
	TWL6030_OF_MATCH("ti,twl6030-vaux3", VAUX3_6030),
	TWL6030_OF_MATCH("ti,twl6030-vmmc", VMMC),
	TWL6030_OF_MATCH("ti,twl6030-vpp", VPP),
	TWL6030_OF_MATCH("ti,twl6030-vusim", VUSIM),
	TWL6032_OF_MATCH("ti,twl6032-ldo2", LDO2),
	TWL6032_OF_MATCH("ti,twl6032-ldo4", LDO4),
	TWL6032_OF_MATCH("ti,twl6032-ldo3", LDO3),
	TWL6032_OF_MATCH("ti,twl6032-ldo5", LDO5),
	TWL6032_OF_MATCH("ti,twl6032-ldo1", LDO1),
	TWL6032_OF_MATCH("ti,twl6032-ldo7", LDO7),
	TWL6032_OF_MATCH("ti,twl6032-ldo6", LDO6),
	TWL6032_OF_MATCH("ti,twl6032-ldoln", LDOLN),
	TWL6032_OF_MATCH("ti,twl6032-ldousb", LDOUSB),
	TWLFIXED_OF_MATCH("ti,twl6030-vana", VANA),
	TWLFIXED_OF_MATCH("ti,twl6030-vcxio", VCXIO),
	TWLFIXED_OF_MATCH("ti,twl6030-vdac", VDAC),
	TWLFIXED_OF_MATCH("ti,twl6030-vusb", VUSB),
	TWLFIXED_OF_MATCH("ti,twl6030-v1v8", V1V8),
	TWLFIXED_OF_MATCH("ti,twl6030-v2v1", V2V1),
	TWLSMPS_OF_MATCH("ti,twl6032-smps3", SMPS3),
	TWLSMPS_OF_MATCH("ti,twl6032-smps4", SMPS4),
	TWLSMPS_OF_MATCH("ti,twl6032-vio", VIO),
	{},
};
MODULE_DEVICE_TABLE(of, twl_of_match);

static int twlreg_probe(struct platform_device *pdev)
{
	int id;
	struct twlreg_info		*info;
	const struct twlreg_info	*template;
	struct regulator_init_data	*initdata;
	struct regulation_constraints	*c;
	struct regulator_dev		*rdev;
	struct regulator_config		config = { };
	struct device_node		*np = pdev->dev.of_node;

	template = of_device_get_match_data(&pdev->dev);
	if (!template)
		return -ENODEV;

	id = template->desc.id;
	initdata = of_get_regulator_init_data(&pdev->dev, np, &template->desc);
	if (!initdata)
		return -EINVAL;

	info = devm_kmemdup(&pdev->dev, template, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	/* Constrain board-specific capabilities according to what
	 * this driver and the chip itself can actually do.
	 */
	c = &initdata->constraints;
	c->valid_modes_mask &= REGULATOR_MODE_NORMAL | REGULATOR_MODE_STANDBY;
	c->valid_ops_mask &= REGULATOR_CHANGE_VOLTAGE
				| REGULATOR_CHANGE_MODE
				| REGULATOR_CHANGE_STATUS;

	switch (id) {
	case TWL6032_REG_SMPS3:
		if (twl_get_smps_mult() & SMPS_MULTOFFSET_SMPS3)
			info->flags |= SMPS_EXTENDED_EN;
		if (twl_get_smps_offset() & SMPS_MULTOFFSET_SMPS3)
			info->flags |= SMPS_OFFSET_EN;
		break;
	case TWL6032_REG_SMPS4:
		if (twl_get_smps_mult() & SMPS_MULTOFFSET_SMPS4)
			info->flags |= SMPS_EXTENDED_EN;
		if (twl_get_smps_offset() & SMPS_MULTOFFSET_SMPS4)
			info->flags |= SMPS_OFFSET_EN;
		break;
	case TWL6032_REG_VIO:
		if (twl_get_smps_mult() & SMPS_MULTOFFSET_VIO)
			info->flags |= SMPS_EXTENDED_EN;
		if (twl_get_smps_offset() & SMPS_MULTOFFSET_VIO)
			info->flags |= SMPS_OFFSET_EN;
		break;
	}

	if (of_get_property(np, "ti,retain-on-reset", NULL))
		info->flags |= TWL_6030_WARM_RESET;

	config.dev = &pdev->dev;
	config.init_data = initdata;
	config.driver_data = info;
	config.of_node = np;

	rdev = devm_regulator_register(&pdev->dev, &info->desc, &config);
	if (IS_ERR(rdev)) {
		dev_err(&pdev->dev, "can't register %s, %ld\n",
				info->desc.name, PTR_ERR(rdev));
		return PTR_ERR(rdev);
	}
	platform_set_drvdata(pdev, rdev);

	/* NOTE:  many regulators support short-circuit IRQs (presentable
	 * as REGULATOR_OVER_CURRENT notifications?) configured via:
	 *  - SC_CONFIG
	 *  - SC_DETECT1 (vintana2, vmmc1/2, vaux1/2/3/4)
	 *  - SC_DETECT2 (vusb, vdac, vio, vdd1/2, vpll2)
	 *  - IT_CONFIG
	 */

	return 0;
}

MODULE_ALIAS("platform:twl6030_reg");

static struct platform_driver twlreg_driver = {
	.probe		= twlreg_probe,
	/* NOTE: short name, to work around driver model truncation of
	 * "twl_regulator.12" (and friends) to "twl_regulator.1".
	 */
	.driver  = {
		.name  = "twl6030_reg",
		.of_match_table = of_match_ptr(twl_of_match),
	},
};

static int __init twlreg_init(void)
{
	return platform_driver_register(&twlreg_driver);
}
subsys_initcall(twlreg_init);

static void __exit twlreg_exit(void)
{
	platform_driver_unregister(&twlreg_driver);
}
module_exit(twlreg_exit)

MODULE_DESCRIPTION("TWL6030 regulator driver");
MODULE_LICENSE("GPL");
