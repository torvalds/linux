/*
 * twl-regulator.c -- support regulators in twl4030/twl6030 family chips
 *
 * Copyright (C) 2008 David Brownell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/i2c/twl.h>


/*
 * The TWL4030/TW5030/TPS659x0/TWL6030 family chips include power management, a
 * USB OTG transceiver, an RTC, ADC, PWM, and lots more.  Some versions
 * include an audio codec, battery charger, and more voltage regulators.
 * These chips are often used in OMAP-based systems.
 *
 * This driver implements software-based resource control for various
 * voltage regulators.  This is usually augmented with state machine
 * based control.
 */

struct twlreg_info {
	/* start of regulator's PM_RECEIVER control register bank */
	u8			base;

	/* twl resource ID, for resource control state machine */
	u8			id;

	/* voltage in mV = table[VSEL]; table_len must be a power-of-two */
	u8			table_len;
	const u16		*table;

	/* regulator specific turn-on delay */
	u16			delay;

	/* State REMAP default configuration */
	u8			remap;

	/* chip constraints on regulator behavior */
	u16			min_mV;
	u16			max_mV;

	u8			flags;

	/* used by regulator core */
	struct regulator_desc	desc;

	/* chip specific features */
	unsigned long 		features;
};


/* LDO control registers ... offset is from the base of its register bank.
 * The first three registers of all power resource banks help hardware to
 * manage the various resource groups.
 */
/* Common offset in TWL4030/6030 */
#define VREG_GRP		0
/* TWL4030 register offsets */
#define VREG_TYPE		1
#define VREG_REMAP		2
#define VREG_DEDICATED		3	/* LDO control */
/* TWL6030 register offsets */
#define VREG_TRANS		1
#define VREG_STATE		2
#define VREG_VOLTAGE		3
#define VREG_VOLTAGE_SMPS	4
#define VREG_VOLTAGE_DVS_SMPS 3      //add
/* TWL6030 Misc register offsets */
#define VREG_BC_ALL		1
#define VREG_BC_REF		2
#define VREG_BC_PROC		3
#define VREG_BC_CLK_RST		4

/* TWL6030 LDO register values for CFG_TRANS */
#define TWL6030_CFG_TRANS_STATE_MASK	0x03
#define TWL6030_CFG_TRANS_STATE_OFF	0x00
/*
 * Auto means the following:
 * SMPS:	AUTO(PWM/PFM)
 * LDO:		AMS(SLP/ACT)
 * resource:	ON
 */
#define TWL6030_CFG_TRANS_STATE_AUTO	0x01
#define TWL6030_CFG_TRANS_SLEEP_SHIFT	2

/* TWL6030 LDO register values for CFG_STATE */
#define TWL6030_CFG_STATE_OFF	0x00
#define TWL6030_CFG_STATE_ON	0x01
#define TWL6030_CFG_STATE_OFF2	0x02
#define TWL6030_CFG_STATE_SLEEP	0x03
#define TWL6030_CFG_STATE_GRP_SHIFT	5
#define TWL6030_CFG_STATE_APP_SHIFT	2
#define TWL6030_CFG_STATE_MASK		0x03
#define TWL6030_CFG_STATE_APP_MASK	(TWL6030_CFG_STATE_MASK << \
						TWL6030_CFG_STATE_APP_SHIFT)
#define TWL6030_CFG_STATE_APP(v)	(((v) & TWL6030_CFG_STATE_APP_MASK) >>\
						TWL6030_CFG_STATE_APP_SHIFT)

/* Flags for SMPS Voltage reading */
#define SMPS_OFFSET_EN		BIT(0)
#define SMPS_EXTENDED_EN	BIT(1)

/* twl6032 SMPS EPROM values */
#define TWL6030_SMPS_OFFSET		0xB0
#define TWL6030_SMPS_MULT		0xB3
#define SMPS_MULTOFFSET_SMPS4	BIT(0)
#define SMPS_MULTOFFSET_VIO	BIT(1)
#define SMPS_MULTOFFSET_SMPS3	BIT(6)



/* TWL6030 VUSB supplemental config registers */
#define TWL6030_MISC2		0xE5
#define TWL6030_CFG_LDO_PD2	0xF5

/*
 * TWL603X SMPS has 6 bits xxxx_CFG_VOLTAGE.VSEL[5:0] to configure voltages and
 * each bit combination corresponds to a particular voltage (value 63 is
 * reserved).
 */
#define TWL603X_SMPS_VSEL_MASK	0x3F
#define TWL603X_SMPS_NUMBER_VOLTAGES TWL603X_SMPS_VSEL_MASK

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

/*----------------------------------------------------------------------*/

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
/* definition for 4030 family */
#define P3_GRP_4030	BIT(7)		/* "peripherals" */
#define P2_GRP_4030	BIT(6)		/* secondary processor, modem, etc */
#define P1_GRP_4030	BIT(5)		/* CPU/Linux */
/* definition for 6030 family */
#define P3_GRP_6030	BIT(2)		/* secondary processor, modem, etc */
#define P2_GRP_6030	BIT(1)		/* "peripherals" */
#define P1_GRP_6030	BIT(0)		/* CPU/Linux */

static int twl4030reg_is_enabled(struct regulator_dev *rdev)
{
	int	state = twlreg_grp(rdev);

	if (state < 0)
		return state;

	return state & P1_GRP_4030;
}

static int twl6030reg_is_enabled(struct regulator_dev *rdev)
{
	struct twlreg_info	*info = rdev_get_drvdata(rdev);
	int			grp = 0, val;

	if (!(info->features & TWL6032_SUBCLASS)) {
		grp = twlreg_read(info, TWL_MODULE_PM_RECEIVER, VREG_GRP);
		if (grp < 0)
			return grp;

		grp &= P1_GRP_6030;
		val = twlreg_read(info, TWL_MODULE_PM_RECEIVER, VREG_STATE);
		val = TWL6030_CFG_STATE_APP(val);
	} else {
		val = twlreg_read(info, TWL_MODULE_PM_RECEIVER, VREG_STATE);
		val &= TWL6030_CFG_STATE_MASK;
		grp = 1;
	}

	return grp && (val == TWL6030_CFG_STATE_ON);
}

static int twl6030reg_set_trans_state(struct regulator_dev *rdev,
				      u8 shift, u8 val)
{
	struct twlreg_info	*info = rdev_get_drvdata(rdev);
	int			rval;
	u8			mask;

	/* Read CFG_TRANS register of TWL6030 */
	rval = twlreg_read(info, TWL_MODULE_PM_RECEIVER, VREG_TRANS);

	if (rval < 0)
		return rval;

	mask = TWL6030_CFG_TRANS_STATE_MASK << shift;
	val = (val << shift) & mask;

	/* If value is already set, no need to write to reg */
	if (val == (rval & mask))
		return 0;

	rval &= ~mask;
	rval |= val;

	return twlreg_write(info, TWL_MODULE_PM_RECEIVER, VREG_TRANS, rval);
}

static int twl4030reg_enable(struct regulator_dev *rdev)
{
	struct twlreg_info	*info = rdev_get_drvdata(rdev);
	int			grp;
	int			ret;

	grp = twlreg_read(info, TWL_MODULE_PM_RECEIVER, VREG_GRP);
	if (grp < 0)
		return grp;

	grp |= P1_GRP_4030;

	ret = twlreg_write(info, TWL_MODULE_PM_RECEIVER, VREG_GRP, grp);

	udelay(info->delay);

	return ret;
}

static int twl6030reg_enable(struct regulator_dev *rdev)
{
	struct twlreg_info	*info = rdev_get_drvdata(rdev);
	int			grp = 0;
	int			ret;

	if (!(twl_class_is_6030() && (info->features & TWL6032_SUBCLASS)))
		grp = twlreg_read(info, TWL_MODULE_PM_RECEIVER, VREG_GRP);
	if (grp < 0)
		return grp;

	ret = twlreg_write(info, TWL_MODULE_PM_RECEIVER, VREG_STATE,
			grp << TWL6030_CFG_STATE_GRP_SHIFT |
			TWL6030_CFG_STATE_ON);
	/*
	 * Ensure it stays in Auto mode when we enter suspend state.
	 * (TWL6030 in sleep mode).
	 */
	if (!ret)
		ret = twl6030reg_set_trans_state(rdev,
				TWL6030_CFG_TRANS_SLEEP_SHIFT,
				TWL6030_CFG_TRANS_STATE_AUTO);
	udelay(info->delay);

	return ret;
}

static int twl4030reg_disable(struct regulator_dev *rdev)
{
	struct twlreg_info	*info = rdev_get_drvdata(rdev);
	int			grp;
	int			ret;

	grp = twlreg_read(info, TWL_MODULE_PM_RECEIVER, VREG_GRP);
	if (grp < 0)
		return grp;

	grp &= ~(P1_GRP_4030 | P2_GRP_4030 | P3_GRP_4030);

	ret = twlreg_write(info, TWL_MODULE_PM_RECEIVER, VREG_GRP, grp);

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

	/* Ensure it remains OFF when we enter suspend (TWL6030 in sleep). */
	if (!ret)
		ret = twl6030reg_set_trans_state(rdev,
				TWL6030_CFG_TRANS_SLEEP_SHIFT,
				TWL6030_CFG_TRANS_STATE_OFF);
	return ret;
}

static int twl4030reg_get_status(struct regulator_dev *rdev)
{
	int	state = twlreg_grp(rdev);

	if (state < 0)
		return state;
	state &= 0x0f;

	/* assume state != WARM_RESET; we'd not be running...  */
	if (!state)
		return REGULATOR_STATUS_OFF;
	return (state & BIT(3))
		? REGULATOR_STATUS_NORMAL
		: REGULATOR_STATUS_STANDBY;
}

static int twl6030reg_get_status(struct regulator_dev *rdev)
{
	struct twlreg_info	*info = rdev_get_drvdata(rdev);
	int			val;

	val = twlreg_grp(rdev);
	if (val < 0)
		return val;

	val = twlreg_read(info, TWL_MODULE_PM_RECEIVER, VREG_STATE);

	if (info->features & TWL6032_SUBCLASS)
		val &= TWL6030_CFG_STATE_MASK;
	else
		val = TWL6030_CFG_STATE_APP(val);

	switch (val) {
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

static int twl4030reg_set_mode(struct regulator_dev *rdev, unsigned mode)
{
	struct twlreg_info	*info = rdev_get_drvdata(rdev);
	unsigned		message;
	int			status;

	/* We can only set the mode through state machine commands... */
	switch (mode) {
	case REGULATOR_MODE_NORMAL:
		message = MSG_SINGULAR(DEV_GRP_P1, info->id, RES_STATE_ACTIVE);
		break;
	case REGULATOR_MODE_STANDBY:
		message = MSG_SINGULAR(DEV_GRP_P1, info->id, RES_STATE_SLEEP);
		break;
	default:
		return -EINVAL;
	}

	/* Ensure the resource is associated with some group */
	status = twlreg_grp(rdev);
	if (status < 0)
		return status;
	if (!(status & (P3_GRP_4030 | P2_GRP_4030 | P1_GRP_4030)))
		return -EACCES;

	status = twl_i2c_write_u8(TWL_MODULE_PM_MASTER,
			message >> 8, TWL4030_PM_MASTER_PB_WORD_MSB);
	if (status < 0)
		return status;

	return twl_i2c_write_u8(TWL_MODULE_PM_MASTER,
			message & 0xff, TWL4030_PM_MASTER_PB_WORD_LSB);
}

static int twl6030reg_set_mode(struct regulator_dev *rdev, unsigned mode)
{
	struct twlreg_info	*info = rdev_get_drvdata(rdev);
	int grp = 0;
	int val;

	if (!(twl_class_is_6030() && (info->features & TWL6032_SUBCLASS)))
		grp = twlreg_read(info, TWL_MODULE_PM_RECEIVER, VREG_GRP);

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

static int twl6030ldo_suspend_enable(struct regulator_dev *rdev)
{
	return twl6030reg_set_trans_state(rdev, TWL6030_CFG_TRANS_SLEEP_SHIFT,
					TWL6030_CFG_TRANS_STATE_AUTO);
}

static int twl6030ldo_suspend_disable(struct regulator_dev *rdev)
{
	return twl6030reg_set_trans_state(rdev, TWL6030_CFG_TRANS_SLEEP_SHIFT,
					TWL6030_CFG_TRANS_STATE_OFF);
}

/*----------------------------------------------------------------------*/

/*
 * Support for adjustable-voltage LDOs uses a four bit (or less) voltage
 * select field in its control register.   We use tables indexed by VSEL
 * to record voltages in milliVolts.  (Accuracy is about three percent.)
 *
 * Note that VSEL values for VAUX2 changed in twl5030 and newer silicon;
 * currently handled by listing two slightly different VAUX2 regulators,
 * only one of which will be configured.
 *
 * VSEL values documented as "TI cannot support these values" are flagged
 * in these tables as UNSUP() values; we normally won't assign them.
 *
 * VAUX3 at 3V is incorrectly listed in some TI manuals as unsupported.
 * TI are revising the twl5030/tps659x0 specs to support that 3.0V setting.
 */
#ifdef CONFIG_TWL4030_ALLOW_UNSUPPORTED
#define UNSUP_MASK	0x0000
#else
#define UNSUP_MASK	0x8000
#endif

#define UNSUP(x)	(UNSUP_MASK | (x))
#define IS_UNSUP(x)	(UNSUP_MASK & (x))
#define LDO_MV(x)	(~UNSUP_MASK & (x))


static const u16 VAUX1_VSEL_table[] = {
	UNSUP(1500), UNSUP(1800), 2500, 2800,
	3000, 3000, 3000, 3000,
};
static const u16 VAUX2_4030_VSEL_table[] = {
	UNSUP(1000), UNSUP(1000), UNSUP(1200), 1300,
	1500, 1800, UNSUP(1850), 2500,
	UNSUP(2600), 2800, UNSUP(2850), UNSUP(3000),
	UNSUP(3150), UNSUP(3150), UNSUP(3150), UNSUP(3150),
};
static const u16 VAUX2_VSEL_table[] = {
	1700, 1700, 1900, 1300,
	1500, 1800, 2000, 2500,
	2100, 2800, 2200, 2300,
	2400, 2400, 2400, 2400,
};
static const u16 VAUX3_VSEL_table[] = {
	1500, 1800, 2500, 2800,
	3000, 3000, 3000, 3000,
};
static const u16 VAUX4_VSEL_table[] = {
	700, 1000, 1200, UNSUP(1300),
	1500, 1800, UNSUP(1850), 2500,
	UNSUP(2600), 2800, UNSUP(2850), UNSUP(3000),
	UNSUP(3150), UNSUP(3150), UNSUP(3150), UNSUP(3150),
};
static const u16 VMMC1_VSEL_table[] = {
	1850, 2850, 3000, 3150,
};
static const u16 VMMC2_VSEL_table[] = {
	UNSUP(1000), UNSUP(1000), UNSUP(1200), UNSUP(1300),
	UNSUP(1500), UNSUP(1800), 1850, UNSUP(2500),
	2600, 2800, 2850, 3000,
	3150, 3150, 3150, 3150,
};
static const u16 VPLL1_VSEL_table[] = {
	1000, 1200, 1300, 1800,
	UNSUP(2800), UNSUP(3000), UNSUP(3000), UNSUP(3000),
};
static const u16 VPLL2_VSEL_table[] = {
	700, 1000, 1200, 1300,
	UNSUP(1500), 1800, UNSUP(1850), UNSUP(2500),
	UNSUP(2600), UNSUP(2800), UNSUP(2850), UNSUP(3000),
	UNSUP(3150), UNSUP(3150), UNSUP(3150), UNSUP(3150),
};
static const u16 VSIM_VSEL_table[] = {
	UNSUP(1000), UNSUP(1200), UNSUP(1300), 1800,
	2800, 3000, 3000, 3000,
};
static const u16 VDAC_VSEL_table[] = {
	1200, 1300, 1800, 1800,
};
static const u16 VDD1_VSEL_table[] = {
	800, 1450,
};
static const u16 VDD2_VSEL_table[] = {
	800, 1450, 1500,
};
static const u16 VIO_VSEL_table[] = {
	1800, 1850,
};
static const u16 VINTANA2_VSEL_table[] = {
	2500, 2750,
};

static int twl4030ldo_list_voltage(struct regulator_dev *rdev, unsigned index)
{
	struct twlreg_info	*info = rdev_get_drvdata(rdev);
	int			mV = info->table[index];

	return IS_UNSUP(mV) ? 0 : (LDO_MV(mV) * 1000);
}

static int
twl4030ldo_set_voltage(struct regulator_dev *rdev, int min_uV, int max_uV,
		       unsigned *selector)
{
	struct twlreg_info	*info = rdev_get_drvdata(rdev);
	int			vsel;

	for (vsel = 0; vsel < info->table_len; vsel++) {
		int mV = info->table[vsel];
		int uV;

		if (IS_UNSUP(mV))
			continue;
		uV = LDO_MV(mV) * 1000;

		/* REVISIT for VAUX2, first match may not be best/lowest */

		/* use the first in-range value */
		if (min_uV <= uV && uV <= max_uV) {
			*selector = vsel;
			return twlreg_write(info, TWL_MODULE_PM_RECEIVER,
							VREG_VOLTAGE, vsel);
		}
	}

	return -EDOM;
}

static int twl4030ldo_get_voltage(struct regulator_dev *rdev)
{
	struct twlreg_info	*info = rdev_get_drvdata(rdev);
	int		vsel = twlreg_read(info, TWL_MODULE_PM_RECEIVER,
								VREG_VOLTAGE);

	if (vsel < 0)
		return vsel;

	vsel &= info->table_len - 1;
	return LDO_MV(info->table[vsel]) * 1000;
}

static struct regulator_ops twl4030ldo_ops = {
	.list_voltage	= twl4030ldo_list_voltage,

	.set_voltage	= twl4030ldo_set_voltage,
	.get_voltage	= twl4030ldo_get_voltage,

	.enable		= twl4030reg_enable,
	.disable	= twl4030reg_disable,
	.is_enabled	= twl4030reg_is_enabled,

	.set_mode	= twl4030reg_set_mode,

	.get_status	= twl4030reg_get_status,
};

static int twl6030ldo_list_voltage(struct regulator_dev *rdev, unsigned index)
{
	struct twlreg_info	*info = rdev_get_drvdata(rdev);

	return ((info->min_mV + (index * 100)) * 1000);
}

static int
twl6030ldo_set_voltage(struct regulator_dev *rdev, int min_uV, int max_uV,
		       unsigned *selector)
{
	struct twlreg_info	*info = rdev_get_drvdata(rdev);
	int			vsel;

	if ((min_uV/1000 < info->min_mV) || (max_uV/1000 > info->max_mV))
		return -EDOM;

	/*
	 * Use the below formula to calculate vsel
	 * mV = 1000mv + 100mv * (vsel - 1)
	 */
	vsel = (min_uV/1000 - 1000)/100 + 1;
	*selector = vsel;
	return twlreg_write(info, TWL_MODULE_PM_RECEIVER, VREG_VOLTAGE, vsel);

}

static int twl6030ldo_get_voltage(struct regulator_dev *rdev)
{
	struct twlreg_info	*info = rdev_get_drvdata(rdev);
	int		vsel = twlreg_read(info, TWL_MODULE_PM_RECEIVER,
								VREG_VOLTAGE);

	if (vsel < 0)
		return vsel;

	/*
	 * Use the below formula to calculate vsel
	 * mV = 1000mv + 100mv * (vsel - 1)
	 */
	return (1000 + (100 * (vsel - 1))) * 1000;
}

static struct regulator_ops twl6030ldo_ops = {
	.list_voltage	= twl6030ldo_list_voltage,

	.set_voltage	= twl6030ldo_set_voltage,
	.get_voltage	= twl6030ldo_get_voltage,

	.enable		= twl6030reg_enable,
	.disable	= twl6030reg_disable,
	.is_enabled	= twl6030reg_is_enabled,

	.set_mode	= twl6030reg_set_mode,

	.get_status	= twl6030reg_get_status,

	.set_suspend_enable	= twl6030ldo_suspend_enable,
	.set_suspend_disable	= twl6030ldo_suspend_disable,
};

/*----------------------------------------------------------------------*/

/*
 * Fixed voltage LDOs don't have a VSEL field to update.
 */
static int twlfixed_list_voltage(struct regulator_dev *rdev, unsigned index)
{
	struct twlreg_info	*info = rdev_get_drvdata(rdev);

	return info->min_mV * 1000;
}

static int twlfixed_get_voltage(struct regulator_dev *rdev)
{
	struct twlreg_info	*info = rdev_get_drvdata(rdev);

	return info->min_mV * 1000;
}

static struct regulator_ops twl4030fixed_ops = {
	.list_voltage	= twlfixed_list_voltage,

	.get_voltage	= twlfixed_get_voltage,

	.enable		= twl4030reg_enable,
	.disable	= twl4030reg_disable,
	.is_enabled	= twl4030reg_is_enabled,

	.set_mode	= twl4030reg_set_mode,

	.get_status	= twl4030reg_get_status,
};

static struct regulator_ops twl6030fixed_ops = {
	.list_voltage	= twlfixed_list_voltage,

	.get_voltage	= twlfixed_get_voltage,

	.enable		= twl6030reg_enable,
	.disable	= twl6030reg_disable,
	.is_enabled	= twl6030reg_is_enabled,

	.set_mode	= twl6030reg_set_mode,

	.get_status	= twl6030reg_get_status,

	.set_suspend_enable	= twl6030ldo_suspend_enable,
	.set_suspend_disable	= twl6030ldo_suspend_disable,
};

static struct regulator_ops twl6030_fixed_resource = {
	.enable		= twl6030reg_enable,
	.disable	= twl6030reg_disable,
	.is_enabled	= twl6030reg_is_enabled,
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

static int
twl6030smps_set_voltage(struct regulator_dev *rdev, int min_uV, int max_uV,
			unsigned int *selector)
{
	struct twlreg_info	*info = rdev_get_drvdata(rdev);
	int	vsel = 0;

	switch (info->flags) {
	case 0:
		if (min_uV == 0)
			vsel = 0;
		else if ((min_uV >= 600000) && (max_uV <= 1300000)) {
			vsel = (min_uV - 600000) / 125;
			if (vsel % 100)
				vsel += 100;
			vsel /= 100;
			vsel++;
		}
		/* Values 1..57 for vsel are linear and can be calculated
		 * values 58..62 are non linear.
		 */
		else if ((min_uV > 1900000) && (max_uV >= 2100000))
			vsel = 62;
		else if ((min_uV > 1800000) && (max_uV >= 1900000))
			vsel = 61;
		else if ((min_uV > 1500000) && (max_uV >= 1800000))
			vsel = 60;
		else if ((min_uV > 1350000) && (max_uV >= 1500000))
			vsel = 59;
		else if ((min_uV > 1300000) && (max_uV >= 1350000))
			vsel = 58;
		else
			return -EINVAL;
		break;
	case SMPS_OFFSET_EN:
		if (min_uV == 0)
			vsel = 0;
		else if ((min_uV >= 700000) && (max_uV <= 1420000)) {
			vsel = (min_uV - 700000) / 125;
			if (vsel % 100)
				vsel += 100;
			vsel /= 100;
			vsel++;
		}
		/* Values 1..57 for vsel are linear and can be calculated
		 * values 58..62 are non linear.
		 */
		else if ((min_uV > 1900000) && (max_uV >= 2100000))
			vsel = 62;
		else if ((min_uV > 1800000) && (max_uV >= 1900000))
			vsel = 61;
		else if ((min_uV > 1350000) && (max_uV >= 1800000))
			vsel = 60;
		else if ((min_uV > 1350000) && (max_uV >= 1500000))
			vsel = 59;
		else if ((min_uV > 1300000) && (max_uV >= 1350000))
			vsel = 58;
		else
			return -EINVAL;
		break;
	case SMPS_EXTENDED_EN:
		if (min_uV == 0)
			vsel = 0;
		else if ((min_uV >= 1852000) && (max_uV <= 4013600)) {
			vsel = (min_uV - 1852000) / 386;
			if (vsel % 100)
				vsel += 100;
			vsel /= 100;
			vsel++;
		}
		break;
	case SMPS_OFFSET_EN|SMPS_EXTENDED_EN:
		if (min_uV == 0)
			vsel = 0;
		else if ((min_uV >= 2161000) && (max_uV <= 4321000)) {
			vsel = (min_uV - 1852000) / 386;
			if (vsel % 100)
				vsel += 100;
			vsel /= 100;
			vsel++;
		}
		break;
	}

	*selector = vsel;

	return twlreg_write(info, TWL_MODULE_PM_RECEIVER, VREG_VOLTAGE_SMPS,
							vsel);
}
//add
#if 1
static int twl6030dvssmps_set_voltage(struct regulator_dev *rdev, int min_uV, int max_uV,
			unsigned int *selector)
{
	struct twlreg_info	*info = rdev_get_drvdata(rdev);
	int	vsel = 0;

	switch (info->flags) {
	case 0:
		if (min_uV == 0)
			vsel = 0;
		else if ((min_uV >= 600000) && (max_uV <= 1300000)) {
			vsel = (min_uV - 600000) / 125;
			if (vsel % 100)
				vsel += 100;
			vsel /= 100;
			vsel++;
		}
		/* Values 1..57 for vsel are linear and can be calculated
		 * values 58..62 are non linear.
		 */
		else if ((min_uV > 1900000) && (max_uV >= 2100000))
			vsel = 62;
		else if ((min_uV > 1800000) && (max_uV >= 1900000))
			vsel = 61;
		else if ((min_uV > 1500000) && (max_uV >= 1800000))
			vsel = 60;
		else if ((min_uV > 1350000) && (max_uV >= 1500000))
			vsel = 59;
		else if ((min_uV > 1300000) && (max_uV >= 1350000))
			vsel = 58;
		else
			return -EINVAL;
		break;
	case SMPS_OFFSET_EN:
		if (min_uV == 0)
			vsel = 0;
		else if ((min_uV >= 700000) && (max_uV <= 1420000)) {
			vsel = (min_uV - 700000) / 125;
			if (vsel % 100)
				vsel += 100;
			vsel /= 100;
			vsel++;
		}
		/* Values 1..57 for vsel are linear and can be calculated
		 * values 58..62 are non linear.
		 */
		else if ((min_uV > 1900000) && (max_uV >= 2100000))
			vsel = 62;
		else if ((min_uV > 1800000) && (max_uV >= 1900000))
			vsel = 61;
		else if ((min_uV > 1350000) && (max_uV >= 1800000))
			vsel = 60;
		else if ((min_uV > 1350000) && (max_uV >= 1500000))
			vsel = 59;
		else if ((min_uV > 1300000) && (max_uV >= 1350000))
			vsel = 58;
		else
			return -EINVAL;
		break;
	case SMPS_EXTENDED_EN:
		if (min_uV == 0)
			vsel = 0;
		else if ((min_uV >= 1852000) && (max_uV <= 4013600)) {
			vsel = (min_uV - 1852000) / 386;
			if (vsel % 100)
				vsel += 100;
			vsel /= 100;
			vsel++;
		}
		break;
	case SMPS_OFFSET_EN|SMPS_EXTENDED_EN:
		if (min_uV == 0)
			vsel = 0;
		else if ((min_uV >= 2161000) && (max_uV <= 4321000)) {
			vsel = (min_uV - 1852000) / 386;
			if (vsel % 100)
				vsel += 100;
			vsel /= 100;
			vsel++;
		}
		break;
	}

	*selector = vsel;

	return twlreg_write(info, TWL_MODULE_PM_DVS, VREG_VOLTAGE_DVS_SMPS,
							vsel);
}

static int twl6030dvssmps_get_voltage_sel(struct regulator_dev *rdev)
{
	struct twlreg_info	*info = rdev_get_drvdata(rdev);

	return twlreg_read(info, TWL_MODULE_PM_DVS, VREG_VOLTAGE_DVS_SMPS);
}
#endif
static int twl6030smps_get_voltage_sel(struct regulator_dev *rdev)
{
	struct twlreg_info	*info = rdev_get_drvdata(rdev);

	return twlreg_read(info, TWL_MODULE_PM_RECEIVER, VREG_VOLTAGE_SMPS);
}

static struct regulator_ops twlsmps_ops = {
	.list_voltage		= twl6030smps_list_voltage,

	.set_voltage		= twl6030smps_set_voltage,
	.get_voltage_sel	= twl6030smps_get_voltage_sel,

	.enable			= twl6030reg_enable,
	.disable		= twl6030reg_disable,
	.is_enabled		= twl6030reg_is_enabled,

	.set_mode		= twl6030reg_set_mode,

	.get_status		= twl6030reg_get_status,

	.set_suspend_enable	= twl6030ldo_suspend_enable,
	.set_suspend_disable	= twl6030ldo_suspend_disable,
};

static struct regulator_ops twl6030_external_control_pin_ops = {
	.enable			= twl6030reg_enable,
	.disable		= twl6030reg_disable,
	.is_enabled		= twl6030reg_is_enabled,

	.set_mode		= twl6030reg_set_mode,

	.get_status		= twl6030reg_get_status,

	.set_suspend_enable	= twl6030ldo_suspend_enable,
	.set_suspend_disable	= twl6030ldo_suspend_disable,
};
//add
static struct regulator_ops twldvssmps_ops = {
	.list_voltage		= twl6030smps_list_voltage,

	.set_voltage		= twl6030dvssmps_set_voltage,
	.get_voltage_sel	= twl6030dvssmps_get_voltage_sel,

	.enable			= twl6030reg_enable,
	.disable		= twl6030reg_disable,
	.is_enabled		= twl6030reg_is_enabled,

	.set_mode		= twl6030reg_set_mode,

	.get_status		= twl6030reg_get_status,

	.set_suspend_enable	= twl6030ldo_suspend_enable,
	.set_suspend_disable	= twl6030ldo_suspend_disable,
};
/*----------------------------------------------------------------------*/

#define TWL4030_FIXED_LDO(label, offset, mVolts, num, turnon_delay, \
			remap_conf) \
		TWL_FIXED_LDO(label, offset, mVolts, num, turnon_delay, \
			remap_conf, TWL4030, twl4030fixed_ops)
#define TWL6030_FIXED_LDO(label, offset, mVolts, turnon_delay) \
		TWL_FIXED_LDO(label, offset, mVolts, 0x0, turnon_delay, \
			0x0, TWL6030, twl6030fixed_ops)

#define TWL4030_ADJUSTABLE_LDO(label, offset, num, turnon_delay, remap_conf) { \
	.base = offset, \
	.id = num, \
	.table_len = ARRAY_SIZE(label##_VSEL_table), \
	.table = label##_VSEL_table, \
	.delay = turnon_delay, \
	.remap = remap_conf, \
	.desc = { \
		.name = #label, \
		.id = TWL4030_REG_##label, \
		.n_voltages = ARRAY_SIZE(label##_VSEL_table), \
		.ops = &twl4030ldo_ops, \
		.type = REGULATOR_VOLTAGE, \
		.owner = THIS_MODULE, \
		}, \
	}

#define TWL6030_ADJUSTABLE_LDO(label, offset, min_mVolts, max_mVolts) { \
	.base = offset, \
	.min_mV = min_mVolts, \
	.max_mV = max_mVolts, \
	.desc = { \
		.name = #label, \
		.id = TWL6030_REG_##label, \
		.n_voltages = (max_mVolts - min_mVolts)/100 + 1, \
		.ops = &twl6030ldo_ops, \
		.type = REGULATOR_VOLTAGE, \
		.owner = THIS_MODULE, \
		}, \
	}

#define TWL6032_ADJUSTABLE_LDO(label, offset, min_mVolts, max_mVolts) { \
	.base = offset, \
	.min_mV = min_mVolts, \
	.max_mV = max_mVolts, \
	.desc = { \
		.name = #label, \
		.id = TWL6032_REG_##label, \
		.n_voltages = ((max_mVolts - min_mVolts)/100) + 1, \
		.ops = &twl6030ldo_ops, \
		.type = REGULATOR_VOLTAGE, \
		.owner = THIS_MODULE, \
		}, \
	}

#define TWL_FIXED_LDO(label, offset, mVolts, num, turnon_delay, remap_conf, \
		family, operations) { \
	.base = offset, \
	.id = num, \
	.min_mV = mVolts, \
	.delay = turnon_delay, \
	.remap = remap_conf, \
	.desc = { \
		.name = #label, \
		.id = family##_REG_##label, \
		.n_voltages = 1, \
		.ops = &operations, \
		.type = REGULATOR_VOLTAGE, \
		.owner = THIS_MODULE, \
		}, \
	}

#define TWL6030_FIXED_RESOURCE(label, offset, turnon_delay) { \
	.base = offset, \
	.delay = turnon_delay, \
	.desc = { \
		.name = #label, \
		.id = TWL6030_REG_##label, \
		.ops = &twl6030_fixed_resource, \
		.type = REGULATOR_VOLTAGE, \
		.owner = THIS_MODULE, \
		}, \
	}

#define TWL6030_ADJUSTABLE_SMPS(label, offset, min_mVolts, max_mVolts) { \
	.base = offset, \
	.min_mV = min_mVolts, \
	.max_mV = max_mVolts, \
	.desc = { \
		.name = #label, \
		.id = TWL6030_REG_##label, \
		.n_voltages = TWL603X_SMPS_NUMBER_VOLTAGES, \
		.ops = &twlsmps_ops, \
		.type = REGULATOR_VOLTAGE, \
		.owner = THIS_MODULE, \
		}, \
	}

#define TWL6032_ADJUSTABLE_SMPS(label, offset) { \
	.base = offset, \
	.min_mV = 600, \
	.max_mV = 2100, \
	.desc = { \
		.name = #label, \
		.id = TWL6032_REG_##label, \
		.n_voltages = TWL603X_SMPS_NUMBER_VOLTAGES, \
		.ops = &twlsmps_ops, \
		.type = REGULATOR_VOLTAGE, \
		.owner = THIS_MODULE, \
		}, \
	}

#define TWL6030_EXTERNAL_CONTROL_PIN(label, offset, turnon_delay) { \
	.base = offset, \
	.delay = turnon_delay, \
	.desc = { \
		.name = #label, \
		.id = TWL6030_REG_##label, \
		.ops = &twl6030_external_control_pin_ops, \
		.type = REGULATOR_VOLTAGE, \
		.owner = THIS_MODULE, \
		}, \
	}
	
	//add
#define TWL6032_ADJUSTABLE_DVSSMPS(label, offset) { \
	.base = offset, \
	.min_mV = 600, \
	.max_mV = 2100, \
	.desc = { \
		.name = #label, \
		.id = TWL6032_REG_##label, \
		.n_voltages = TWL603X_SMPS_NUMBER_VOLTAGES, \
		.ops = &twldvssmps_ops, \
		.type = REGULATOR_VOLTAGE, \
		.owner = THIS_MODULE, \
		}, \
	}

/*
 * We list regulators here if systems need some level of
 * software control over them after boot.
 */
static struct twlreg_info twl_regs[] = {
	TWL4030_ADJUSTABLE_LDO(VAUX1, 0x17, 1, 100, 0x08),
	TWL4030_ADJUSTABLE_LDO(VAUX2_4030, 0x1b, 2, 100, 0x08),
	TWL4030_ADJUSTABLE_LDO(VAUX2, 0x1b, 2, 100, 0x08),
	TWL4030_ADJUSTABLE_LDO(VAUX3, 0x1f, 3, 100, 0x08),
	TWL4030_ADJUSTABLE_LDO(VAUX4, 0x23, 4, 100, 0x08),
	TWL4030_ADJUSTABLE_LDO(VMMC1, 0x27, 5, 100, 0x08),
	TWL4030_ADJUSTABLE_LDO(VMMC2, 0x2b, 6, 100, 0x08),
	TWL4030_ADJUSTABLE_LDO(VPLL1, 0x2f, 7, 100, 0x00),
	TWL4030_ADJUSTABLE_LDO(VPLL2, 0x33, 8, 100, 0x08),
	TWL4030_ADJUSTABLE_LDO(VSIM, 0x37, 9, 100, 0x00),
	TWL4030_ADJUSTABLE_LDO(VDAC, 0x3b, 10, 100, 0x08),
	TWL4030_FIXED_LDO(VINTANA1, 0x3f, 1500, 11, 100, 0x08),
	TWL4030_ADJUSTABLE_LDO(VINTANA2, 0x43, 12, 100, 0x08),
	TWL4030_FIXED_LDO(VINTDIG, 0x47, 1500, 13, 100, 0x08),
	TWL4030_ADJUSTABLE_LDO(VIO, 0x4b, 14, 1000, 0x08),
	TWL4030_ADJUSTABLE_LDO(VDD1, 0x55, 15, 1000, 0x08),
	TWL4030_ADJUSTABLE_LDO(VDD2, 0x63, 16, 1000, 0x08),
	TWL4030_FIXED_LDO(VUSB1V5, 0x71, 1500, 17, 100, 0x08),
	TWL4030_FIXED_LDO(VUSB1V8, 0x74, 1800, 18, 100, 0x08),
	TWL4030_FIXED_LDO(VUSB3V1, 0x77, 3100, 19, 150, 0x08),
	/* VUSBCP is managed *only* by the USB subchip */

	/* 6030 REG with base as PMC Slave Misc : 0x0030 */
	/* Turnon-delay and remap configuration values for 6030 are not
	   verified since the specification is not public */
	TWL6030_ADJUSTABLE_LDO(VAUX1_6030, 0x54, 1000, 3300),
	TWL6030_ADJUSTABLE_LDO(VAUX2_6030, 0x58, 1000, 3300),
	TWL6030_ADJUSTABLE_LDO(VAUX3_6030, 0x5c, 1000, 3300),
	TWL6030_ADJUSTABLE_LDO(VMMC, 0x68, 1000, 3300),
	TWL6030_ADJUSTABLE_LDO(VPP, 0x6c, 1000, 3300),
	TWL6030_ADJUSTABLE_LDO(VUSIM, 0x74, 1000, 3300),
	TWL6030_FIXED_LDO(VANA, 0x50, 2100, 0),
	TWL6030_FIXED_LDO(VCXIO, 0x60, 1800, 0),
	TWL6030_FIXED_LDO(VDAC, 0x64, 1800, 0),
	TWL6030_FIXED_LDO(VUSB, 0x70, 3300, 0),
	TWL6030_FIXED_RESOURCE(CLK32KG, 0x8C, 0),
	TWL6030_FIXED_RESOURCE(CLK32KAUDIO, 0x8F, 0),
	TWL6030_ADJUSTABLE_SMPS(VDD1, 0x22, 600, 4000),
	TWL6030_ADJUSTABLE_SMPS(VDD2, 0x28, 600, 4000),
	TWL6030_ADJUSTABLE_SMPS(VDD3, 0x2e, 600, 4000),
	TWL6030_ADJUSTABLE_SMPS(VMEM, 0x34, 600, 4000),
	TWL6030_ADJUSTABLE_SMPS(V2V1, 0x1c, 1800, 2100),

	/* 6032 are renamed compared to 6030 versions */
	TWL6032_ADJUSTABLE_LDO(LDO2, 0x54, 1000, 3300),
	TWL6032_ADJUSTABLE_LDO(LDO4, 0x58, 1000, 3300),
	TWL6032_ADJUSTABLE_LDO(LDO3, 0x5c, 1000, 3300),
	TWL6032_ADJUSTABLE_LDO(LDO5, 0x68, 1000, 3300),
	TWL6032_ADJUSTABLE_LDO(LDO1, 0x6c, 1000, 3300),
	TWL6032_ADJUSTABLE_LDO(LDO7, 0x74, 1000, 3300),
	TWL6032_ADJUSTABLE_LDO(LDO6, 0x60, 1000, 3300),
	TWL6032_ADJUSTABLE_LDO(LDOLN, 0x64, 1000, 3300),
	TWL6032_ADJUSTABLE_LDO(LDOUSB, 0x70, 1000, 3300),

	TWL6032_ADJUSTABLE_SMPS(SMPS3, 0x34),
	TWL6032_ADJUSTABLE_SMPS(SMPS4, 0x10),
	TWL6032_ADJUSTABLE_SMPS(VIO, 0x16),
	
	TWL6032_ADJUSTABLE_DVSSMPS(SMPS1, 0x22),
	TWL6032_ADJUSTABLE_DVSSMPS(SMPS2, 0x28),
	TWL6032_ADJUSTABLE_DVSSMPS(SMPS5, 0x16),
	
	TWL6030_EXTERNAL_CONTROL_PIN(SYSEN, 0x83, 0),
	TWL6030_EXTERNAL_CONTROL_PIN(REGEN1, 0x7d, 0),

};

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

static int __devinit twlreg_probe(struct platform_device *pdev)
{
	int				i;
	struct twlreg_info		*info;
	struct regulator_init_data	*initdata;
	struct regulation_constraints	*c;
	struct regulator_dev		*rdev;
	int ret;

	for (i = 0, info = NULL; i < ARRAY_SIZE(twl_regs); i++) {
		if (twl_regs[i].desc.id != pdev->id)
			continue;
		info = twl_regs + i;
		break;
	}
	if (!info)
		return -ENODEV;

	initdata = pdev->dev.platform_data;
	if (!initdata)
		return -EINVAL;

	/* copy the features into regulator data */
	info->features = (unsigned long)initdata->driver_data;

	/* Constrain board-specific capabilities according to what
	 * this driver and the chip itself can actually do.
	 */
	c = &initdata->constraints;
	c->valid_modes_mask &= REGULATOR_MODE_NORMAL | REGULATOR_MODE_STANDBY;
	c->valid_ops_mask &= REGULATOR_CHANGE_VOLTAGE
				| REGULATOR_CHANGE_MODE
				| REGULATOR_CHANGE_STATUS;
	switch (pdev->id) {
	case TWL4030_REG_VIO:
	case TWL4030_REG_VDD1:
	case TWL4030_REG_VDD2:
	case TWL4030_REG_VPLL1:
	case TWL4030_REG_VINTANA1:
	case TWL4030_REG_VINTANA2:
	case TWL4030_REG_VINTDIG:
		c->always_on = true;
		break;
	case TWL6030_REG_VUSB:
		/* Program CFG_LDO_PD2 register and set VUSB bit */
		ret = twl_i2c_write_u8(TWL6030_MODULE_ID0, 0x1,
				TWL6030_CFG_LDO_PD2);
		if (ret < 0)
			return ret;

		/* Program MISC2 register and set bit VUSB_IN_VBAT */
		ret = twl_i2c_write_u8(TWL6030_MODULE_ID0, 0x10, TWL6030_MISC2);
		if (ret < 0)
			return ret;
		break;
	default:
		break;
	}

	switch (pdev->id) {
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

	rdev = regulator_register(&info->desc, &pdev->dev, initdata, info);
	if (IS_ERR(rdev)) {
		dev_err(&pdev->dev, "can't register %s, %ld\n",
				info->desc.name, PTR_ERR(rdev));
		return PTR_ERR(rdev);
	}
	platform_set_drvdata(pdev, rdev);

	if (twl_class_is_4030())
		twlreg_write(info, TWL_MODULE_PM_RECEIVER, VREG_REMAP,
						info->remap);

	/* NOTE:  many regulators support short-circuit IRQs (presentable
	 * as REGULATOR_OVER_CURRENT notifications?) configured via:
	 *  - SC_CONFIG
	 *  - SC_DETECT1 (vintana2, vmmc1/2, vaux1/2/3/4)
	 *  - SC_DETECT2 (vusb, vdac, vio, vdd1/2, vpll2)
	 *  - IT_CONFIG
	 */

	return 0;
}

static int __devexit twlreg_remove(struct platform_device *pdev)
{
	regulator_unregister(platform_get_drvdata(pdev));
	return 0;
}

MODULE_ALIAS("platform:twl_reg");

static struct platform_driver twlreg_driver = {
	.probe		= twlreg_probe,
	.remove		= __devexit_p(twlreg_remove),
	/* NOTE: short name, to work around driver model truncation of
	 * "twl_regulator.12" (and friends) to "twl_regulator.1".
	 */
	.driver.name	= "twl_reg",
	.driver.owner	= THIS_MODULE,
};

static int __init twlreg_init(void)
{
	return platform_driver_register(&twlreg_driver);
}
subsys_initcall_sync(twlreg_init);

static void __exit twlreg_exit(void)
{
	platform_driver_unregister(&twlreg_driver);
}
module_exit(twlreg_exit)

MODULE_DESCRIPTION("TWL regulator driver");
MODULE_LICENSE("GPL");
