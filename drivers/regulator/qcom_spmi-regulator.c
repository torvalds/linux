/*
 * Copyright (c) 2012-2015, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/bitops.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/ktime.h>
#include <linux/regulator/driver.h>
#include <linux/regmap.h>
#include <linux/list.h>

/* Pin control enable input pins. */
#define SPMI_REGULATOR_PIN_CTRL_ENABLE_NONE		0x00
#define SPMI_REGULATOR_PIN_CTRL_ENABLE_EN0		0x01
#define SPMI_REGULATOR_PIN_CTRL_ENABLE_EN1		0x02
#define SPMI_REGULATOR_PIN_CTRL_ENABLE_EN2		0x04
#define SPMI_REGULATOR_PIN_CTRL_ENABLE_EN3		0x08
#define SPMI_REGULATOR_PIN_CTRL_ENABLE_HW_DEFAULT	0x10

/* Pin control high power mode input pins. */
#define SPMI_REGULATOR_PIN_CTRL_HPM_NONE		0x00
#define SPMI_REGULATOR_PIN_CTRL_HPM_EN0			0x01
#define SPMI_REGULATOR_PIN_CTRL_HPM_EN1			0x02
#define SPMI_REGULATOR_PIN_CTRL_HPM_EN2			0x04
#define SPMI_REGULATOR_PIN_CTRL_HPM_EN3			0x08
#define SPMI_REGULATOR_PIN_CTRL_HPM_SLEEP_B		0x10
#define SPMI_REGULATOR_PIN_CTRL_HPM_HW_DEFAULT		0x20

/*
 * Used with enable parameters to specify that hardware default register values
 * should be left unaltered.
 */
#define SPMI_REGULATOR_USE_HW_DEFAULT			2

/* Soft start strength of a voltage switch type regulator */
enum spmi_vs_soft_start_str {
	SPMI_VS_SOFT_START_STR_0P05_UA = 0,
	SPMI_VS_SOFT_START_STR_0P25_UA,
	SPMI_VS_SOFT_START_STR_0P55_UA,
	SPMI_VS_SOFT_START_STR_0P75_UA,
	SPMI_VS_SOFT_START_STR_HW_DEFAULT,
};

/**
 * struct spmi_regulator_init_data - spmi-regulator initialization data
 * @pin_ctrl_enable:        Bit mask specifying which hardware pins should be
 *				used to enable the regulator, if any
 *			    Value should be an ORing of
 *				SPMI_REGULATOR_PIN_CTRL_ENABLE_* constants.  If
 *				the bit specified by
 *				SPMI_REGULATOR_PIN_CTRL_ENABLE_HW_DEFAULT is
 *				set, then pin control enable hardware registers
 *				will not be modified.
 * @pin_ctrl_hpm:           Bit mask specifying which hardware pins should be
 *				used to force the regulator into high power
 *				mode, if any
 *			    Value should be an ORing of
 *				SPMI_REGULATOR_PIN_CTRL_HPM_* constants.  If
 *				the bit specified by
 *				SPMI_REGULATOR_PIN_CTRL_HPM_HW_DEFAULT is
 *				set, then pin control mode hardware registers
 *				will not be modified.
 * @vs_soft_start_strength: This parameter sets the soft start strength for
 *				voltage switch type regulators.  Its value
 *				should be one of SPMI_VS_SOFT_START_STR_*.  If
 *				its value is SPMI_VS_SOFT_START_STR_HW_DEFAULT,
 *				then the soft start strength will be left at its
 *				default hardware value.
 */
struct spmi_regulator_init_data {
	unsigned				pin_ctrl_enable;
	unsigned				pin_ctrl_hpm;
	enum spmi_vs_soft_start_str		vs_soft_start_strength;
};

/* These types correspond to unique register layouts. */
enum spmi_regulator_logical_type {
	SPMI_REGULATOR_LOGICAL_TYPE_SMPS,
	SPMI_REGULATOR_LOGICAL_TYPE_LDO,
	SPMI_REGULATOR_LOGICAL_TYPE_VS,
	SPMI_REGULATOR_LOGICAL_TYPE_BOOST,
	SPMI_REGULATOR_LOGICAL_TYPE_FTSMPS,
	SPMI_REGULATOR_LOGICAL_TYPE_BOOST_BYP,
	SPMI_REGULATOR_LOGICAL_TYPE_LN_LDO,
	SPMI_REGULATOR_LOGICAL_TYPE_ULT_LO_SMPS,
	SPMI_REGULATOR_LOGICAL_TYPE_ULT_HO_SMPS,
	SPMI_REGULATOR_LOGICAL_TYPE_ULT_LDO,
};

enum spmi_regulator_type {
	SPMI_REGULATOR_TYPE_BUCK		= 0x03,
	SPMI_REGULATOR_TYPE_LDO			= 0x04,
	SPMI_REGULATOR_TYPE_VS			= 0x05,
	SPMI_REGULATOR_TYPE_BOOST		= 0x1b,
	SPMI_REGULATOR_TYPE_FTS			= 0x1c,
	SPMI_REGULATOR_TYPE_BOOST_BYP		= 0x1f,
	SPMI_REGULATOR_TYPE_ULT_LDO		= 0x21,
	SPMI_REGULATOR_TYPE_ULT_BUCK		= 0x22,
};

enum spmi_regulator_subtype {
	SPMI_REGULATOR_SUBTYPE_GP_CTL		= 0x08,
	SPMI_REGULATOR_SUBTYPE_RF_CTL		= 0x09,
	SPMI_REGULATOR_SUBTYPE_N50		= 0x01,
	SPMI_REGULATOR_SUBTYPE_N150		= 0x02,
	SPMI_REGULATOR_SUBTYPE_N300		= 0x03,
	SPMI_REGULATOR_SUBTYPE_N600		= 0x04,
	SPMI_REGULATOR_SUBTYPE_N1200		= 0x05,
	SPMI_REGULATOR_SUBTYPE_N600_ST		= 0x06,
	SPMI_REGULATOR_SUBTYPE_N1200_ST		= 0x07,
	SPMI_REGULATOR_SUBTYPE_N900_ST		= 0x14,
	SPMI_REGULATOR_SUBTYPE_N300_ST		= 0x15,
	SPMI_REGULATOR_SUBTYPE_P50		= 0x08,
	SPMI_REGULATOR_SUBTYPE_P150		= 0x09,
	SPMI_REGULATOR_SUBTYPE_P300		= 0x0a,
	SPMI_REGULATOR_SUBTYPE_P600		= 0x0b,
	SPMI_REGULATOR_SUBTYPE_P1200		= 0x0c,
	SPMI_REGULATOR_SUBTYPE_LN		= 0x10,
	SPMI_REGULATOR_SUBTYPE_LV_P50		= 0x28,
	SPMI_REGULATOR_SUBTYPE_LV_P150		= 0x29,
	SPMI_REGULATOR_SUBTYPE_LV_P300		= 0x2a,
	SPMI_REGULATOR_SUBTYPE_LV_P600		= 0x2b,
	SPMI_REGULATOR_SUBTYPE_LV_P1200		= 0x2c,
	SPMI_REGULATOR_SUBTYPE_LV_P450		= 0x2d,
	SPMI_REGULATOR_SUBTYPE_LV100		= 0x01,
	SPMI_REGULATOR_SUBTYPE_LV300		= 0x02,
	SPMI_REGULATOR_SUBTYPE_MV300		= 0x08,
	SPMI_REGULATOR_SUBTYPE_MV500		= 0x09,
	SPMI_REGULATOR_SUBTYPE_HDMI		= 0x10,
	SPMI_REGULATOR_SUBTYPE_OTG		= 0x11,
	SPMI_REGULATOR_SUBTYPE_5V_BOOST		= 0x01,
	SPMI_REGULATOR_SUBTYPE_FTS_CTL		= 0x08,
	SPMI_REGULATOR_SUBTYPE_FTS2p5_CTL	= 0x09,
	SPMI_REGULATOR_SUBTYPE_BB_2A		= 0x01,
	SPMI_REGULATOR_SUBTYPE_ULT_HF_CTL1	= 0x0d,
	SPMI_REGULATOR_SUBTYPE_ULT_HF_CTL2	= 0x0e,
	SPMI_REGULATOR_SUBTYPE_ULT_HF_CTL3	= 0x0f,
	SPMI_REGULATOR_SUBTYPE_ULT_HF_CTL4	= 0x10,
};

enum spmi_common_regulator_registers {
	SPMI_COMMON_REG_DIG_MAJOR_REV		= 0x01,
	SPMI_COMMON_REG_TYPE			= 0x04,
	SPMI_COMMON_REG_SUBTYPE			= 0x05,
	SPMI_COMMON_REG_VOLTAGE_RANGE		= 0x40,
	SPMI_COMMON_REG_VOLTAGE_SET		= 0x41,
	SPMI_COMMON_REG_MODE			= 0x45,
	SPMI_COMMON_REG_ENABLE			= 0x46,
	SPMI_COMMON_REG_PULL_DOWN		= 0x48,
	SPMI_COMMON_REG_SOFT_START		= 0x4c,
	SPMI_COMMON_REG_STEP_CTRL		= 0x61,
};

enum spmi_vs_registers {
	SPMI_VS_REG_OCP				= 0x4a,
	SPMI_VS_REG_SOFT_START			= 0x4c,
};

enum spmi_boost_registers {
	SPMI_BOOST_REG_CURRENT_LIMIT		= 0x4a,
};

enum spmi_boost_byp_registers {
	SPMI_BOOST_BYP_REG_CURRENT_LIMIT	= 0x4b,
};

/* Used for indexing into ctrl_reg.  These are offets from 0x40 */
enum spmi_common_control_register_index {
	SPMI_COMMON_IDX_VOLTAGE_RANGE		= 0,
	SPMI_COMMON_IDX_VOLTAGE_SET		= 1,
	SPMI_COMMON_IDX_MODE			= 5,
	SPMI_COMMON_IDX_ENABLE			= 6,
};

/* Common regulator control register layout */
#define SPMI_COMMON_ENABLE_MASK			0x80
#define SPMI_COMMON_ENABLE			0x80
#define SPMI_COMMON_DISABLE			0x00
#define SPMI_COMMON_ENABLE_FOLLOW_HW_EN3_MASK	0x08
#define SPMI_COMMON_ENABLE_FOLLOW_HW_EN2_MASK	0x04
#define SPMI_COMMON_ENABLE_FOLLOW_HW_EN1_MASK	0x02
#define SPMI_COMMON_ENABLE_FOLLOW_HW_EN0_MASK	0x01
#define SPMI_COMMON_ENABLE_FOLLOW_ALL_MASK	0x0f

/* Common regulator mode register layout */
#define SPMI_COMMON_MODE_HPM_MASK		0x80
#define SPMI_COMMON_MODE_AUTO_MASK		0x40
#define SPMI_COMMON_MODE_BYPASS_MASK		0x20
#define SPMI_COMMON_MODE_FOLLOW_AWAKE_MASK	0x10
#define SPMI_COMMON_MODE_FOLLOW_HW_EN3_MASK	0x08
#define SPMI_COMMON_MODE_FOLLOW_HW_EN2_MASK	0x04
#define SPMI_COMMON_MODE_FOLLOW_HW_EN1_MASK	0x02
#define SPMI_COMMON_MODE_FOLLOW_HW_EN0_MASK	0x01
#define SPMI_COMMON_MODE_FOLLOW_ALL_MASK	0x1f

/* Common regulator pull down control register layout */
#define SPMI_COMMON_PULL_DOWN_ENABLE_MASK	0x80

/* LDO regulator current limit control register layout */
#define SPMI_LDO_CURRENT_LIMIT_ENABLE_MASK	0x80

/* LDO regulator soft start control register layout */
#define SPMI_LDO_SOFT_START_ENABLE_MASK		0x80

/* VS regulator over current protection control register layout */
#define SPMI_VS_OCP_OVERRIDE			0x01
#define SPMI_VS_OCP_NO_OVERRIDE			0x00

/* VS regulator soft start control register layout */
#define SPMI_VS_SOFT_START_ENABLE_MASK		0x80
#define SPMI_VS_SOFT_START_SEL_MASK		0x03

/* Boost regulator current limit control register layout */
#define SPMI_BOOST_CURRENT_LIMIT_ENABLE_MASK	0x80
#define SPMI_BOOST_CURRENT_LIMIT_MASK		0x07

#define SPMI_VS_OCP_DEFAULT_MAX_RETRIES		10
#define SPMI_VS_OCP_DEFAULT_RETRY_DELAY_MS	30
#define SPMI_VS_OCP_FALL_DELAY_US		90
#define SPMI_VS_OCP_FAULT_DELAY_US		20000

#define SPMI_FTSMPS_STEP_CTRL_STEP_MASK		0x18
#define SPMI_FTSMPS_STEP_CTRL_STEP_SHIFT	3
#define SPMI_FTSMPS_STEP_CTRL_DELAY_MASK	0x07
#define SPMI_FTSMPS_STEP_CTRL_DELAY_SHIFT	0

/* Clock rate in kHz of the FTSMPS regulator reference clock. */
#define SPMI_FTSMPS_CLOCK_RATE		19200

/* Minimum voltage stepper delay for each step. */
#define SPMI_FTSMPS_STEP_DELAY		8
#define SPMI_DEFAULT_STEP_DELAY		20

/*
 * The ratio SPMI_FTSMPS_STEP_MARGIN_NUM/SPMI_FTSMPS_STEP_MARGIN_DEN is used to
 * adjust the step rate in order to account for oscillator variance.
 */
#define SPMI_FTSMPS_STEP_MARGIN_NUM	4
#define SPMI_FTSMPS_STEP_MARGIN_DEN	5

/* VSET value to decide the range of ULT SMPS */
#define ULT_SMPS_RANGE_SPLIT 0x60

/**
 * struct spmi_voltage_range - regulator set point voltage mapping description
 * @min_uV:		Minimum programmable output voltage resulting from
 *			set point register value 0x00
 * @max_uV:		Maximum programmable output voltage
 * @step_uV:		Output voltage increase resulting from the set point
 *			register value increasing by 1
 * @set_point_min_uV:	Minimum allowed voltage
 * @set_point_max_uV:	Maximum allowed voltage.  This may be tweaked in order
 *			to pick which range should be used in the case of
 *			overlapping set points.
 * @n_voltages:		Number of preferred voltage set points present in this
 *			range
 * @range_sel:		Voltage range register value corresponding to this range
 *
 * The following relationships must be true for the values used in this struct:
 * (max_uV - min_uV) % step_uV == 0
 * (set_point_min_uV - min_uV) % step_uV == 0*
 * (set_point_max_uV - min_uV) % step_uV == 0*
 * n_voltages = (set_point_max_uV - set_point_min_uV) / step_uV + 1
 *
 * *Note, set_point_min_uV == set_point_max_uV == 0 is allowed in order to
 * specify that the voltage range has meaning, but is not preferred.
 */
struct spmi_voltage_range {
	int					min_uV;
	int					max_uV;
	int					step_uV;
	int					set_point_min_uV;
	int					set_point_max_uV;
	unsigned				n_voltages;
	u8					range_sel;
};

/*
 * The ranges specified in the spmi_voltage_set_points struct must be listed
 * so that range[i].set_point_max_uV < range[i+1].set_point_min_uV.
 */
struct spmi_voltage_set_points {
	struct spmi_voltage_range		*range;
	int					count;
	unsigned				n_voltages;
};

struct spmi_regulator {
	struct regulator_desc			desc;
	struct device				*dev;
	struct delayed_work			ocp_work;
	struct regmap				*regmap;
	struct spmi_voltage_set_points		*set_points;
	enum spmi_regulator_logical_type	logical_type;
	int					ocp_irq;
	int					ocp_count;
	int					ocp_max_retries;
	int					ocp_retry_delay_ms;
	int					hpm_min_load;
	int					slew_rate;
	ktime_t					vs_enable_time;
	u16					base;
	struct list_head			node;
};

struct spmi_regulator_mapping {
	enum spmi_regulator_type		type;
	enum spmi_regulator_subtype		subtype;
	enum spmi_regulator_logical_type	logical_type;
	u32					revision_min;
	u32					revision_max;
	struct regulator_ops			*ops;
	struct spmi_voltage_set_points		*set_points;
	int					hpm_min_load;
};

struct spmi_regulator_data {
	const char			*name;
	u16				base;
	const char			*supply;
	const char			*ocp;
	u16				force_type;
};

#define SPMI_VREG(_type, _subtype, _dig_major_min, _dig_major_max, \
		      _logical_type, _ops_val, _set_points_val, _hpm_min_load) \
	{ \
		.type		= SPMI_REGULATOR_TYPE_##_type, \
		.subtype	= SPMI_REGULATOR_SUBTYPE_##_subtype, \
		.revision_min	= _dig_major_min, \
		.revision_max	= _dig_major_max, \
		.logical_type	= SPMI_REGULATOR_LOGICAL_TYPE_##_logical_type, \
		.ops		= &spmi_##_ops_val##_ops, \
		.set_points	= &_set_points_val##_set_points, \
		.hpm_min_load	= _hpm_min_load, \
	}

#define SPMI_VREG_VS(_subtype, _dig_major_min, _dig_major_max) \
	{ \
		.type		= SPMI_REGULATOR_TYPE_VS, \
		.subtype	= SPMI_REGULATOR_SUBTYPE_##_subtype, \
		.revision_min	= _dig_major_min, \
		.revision_max	= _dig_major_max, \
		.logical_type	= SPMI_REGULATOR_LOGICAL_TYPE_VS, \
		.ops		= &spmi_vs_ops, \
	}

#define SPMI_VOLTAGE_RANGE(_range_sel, _min_uV, _set_point_min_uV, \
			_set_point_max_uV, _max_uV, _step_uV) \
	{ \
		.min_uV			= _min_uV, \
		.max_uV			= _max_uV, \
		.set_point_min_uV	= _set_point_min_uV, \
		.set_point_max_uV	= _set_point_max_uV, \
		.step_uV		= _step_uV, \
		.range_sel		= _range_sel, \
	}

#define DEFINE_SPMI_SET_POINTS(name) \
struct spmi_voltage_set_points name##_set_points = { \
	.range	= name##_ranges, \
	.count	= ARRAY_SIZE(name##_ranges), \
}

/*
 * These tables contain the physically available PMIC regulator voltage setpoint
 * ranges.  Where two ranges overlap in hardware, one of the ranges is trimmed
 * to ensure that the setpoints available to software are monotonically
 * increasing and unique.  The set_voltage callback functions expect these
 * properties to hold.
 */
static struct spmi_voltage_range pldo_ranges[] = {
	SPMI_VOLTAGE_RANGE(2,  750000,  750000, 1537500, 1537500, 12500),
	SPMI_VOLTAGE_RANGE(3, 1500000, 1550000, 3075000, 3075000, 25000),
	SPMI_VOLTAGE_RANGE(4, 1750000, 3100000, 4900000, 4900000, 50000),
};

static struct spmi_voltage_range nldo1_ranges[] = {
	SPMI_VOLTAGE_RANGE(2,  750000,  750000, 1537500, 1537500, 12500),
};

static struct spmi_voltage_range nldo2_ranges[] = {
	SPMI_VOLTAGE_RANGE(0,  375000,       0,       0, 1537500, 12500),
	SPMI_VOLTAGE_RANGE(1,  375000,  375000,  768750,  768750,  6250),
	SPMI_VOLTAGE_RANGE(2,  750000,  775000, 1537500, 1537500, 12500),
};

static struct spmi_voltage_range nldo3_ranges[] = {
	SPMI_VOLTAGE_RANGE(0,  375000,  375000, 1537500, 1537500, 12500),
	SPMI_VOLTAGE_RANGE(1,  375000,       0,       0, 1537500, 12500),
	SPMI_VOLTAGE_RANGE(2,  750000,       0,       0, 1537500, 12500),
};

static struct spmi_voltage_range ln_ldo_ranges[] = {
	SPMI_VOLTAGE_RANGE(1,  690000,  690000, 1110000, 1110000, 60000),
	SPMI_VOLTAGE_RANGE(0, 1380000, 1380000, 2220000, 2220000, 120000),
};

static struct spmi_voltage_range smps_ranges[] = {
	SPMI_VOLTAGE_RANGE(0,  375000,  375000, 1562500, 1562500, 12500),
	SPMI_VOLTAGE_RANGE(1, 1550000, 1575000, 3125000, 3125000, 25000),
};

static struct spmi_voltage_range ftsmps_ranges[] = {
	SPMI_VOLTAGE_RANGE(0,       0,  350000, 1275000, 1275000,  5000),
	SPMI_VOLTAGE_RANGE(1,       0, 1280000, 2040000, 2040000, 10000),
};

static struct spmi_voltage_range ftsmps2p5_ranges[] = {
	SPMI_VOLTAGE_RANGE(0,   80000,  350000, 1355000, 1355000,  5000),
	SPMI_VOLTAGE_RANGE(1,  160000, 1360000, 2200000, 2200000, 10000),
};

static struct spmi_voltage_range boost_ranges[] = {
	SPMI_VOLTAGE_RANGE(0, 4000000, 4000000, 5550000, 5550000, 50000),
};

static struct spmi_voltage_range boost_byp_ranges[] = {
	SPMI_VOLTAGE_RANGE(0, 2500000, 2500000, 5200000, 5650000, 50000),
};

static struct spmi_voltage_range ult_lo_smps_ranges[] = {
	SPMI_VOLTAGE_RANGE(0,  375000,  375000, 1562500, 1562500, 12500),
	SPMI_VOLTAGE_RANGE(1,  750000,       0,       0, 1525000, 25000),
};

static struct spmi_voltage_range ult_ho_smps_ranges[] = {
	SPMI_VOLTAGE_RANGE(0, 1550000, 1550000, 2325000, 2325000, 25000),
};

static struct spmi_voltage_range ult_nldo_ranges[] = {
	SPMI_VOLTAGE_RANGE(0,  375000,  375000, 1537500, 1537500, 12500),
};

static struct spmi_voltage_range ult_pldo_ranges[] = {
	SPMI_VOLTAGE_RANGE(0, 1750000, 1750000, 3337500, 3337500, 12500),
};

static DEFINE_SPMI_SET_POINTS(pldo);
static DEFINE_SPMI_SET_POINTS(nldo1);
static DEFINE_SPMI_SET_POINTS(nldo2);
static DEFINE_SPMI_SET_POINTS(nldo3);
static DEFINE_SPMI_SET_POINTS(ln_ldo);
static DEFINE_SPMI_SET_POINTS(smps);
static DEFINE_SPMI_SET_POINTS(ftsmps);
static DEFINE_SPMI_SET_POINTS(ftsmps2p5);
static DEFINE_SPMI_SET_POINTS(boost);
static DEFINE_SPMI_SET_POINTS(boost_byp);
static DEFINE_SPMI_SET_POINTS(ult_lo_smps);
static DEFINE_SPMI_SET_POINTS(ult_ho_smps);
static DEFINE_SPMI_SET_POINTS(ult_nldo);
static DEFINE_SPMI_SET_POINTS(ult_pldo);

static inline int spmi_vreg_read(struct spmi_regulator *vreg, u16 addr, u8 *buf,
				 int len)
{
	return regmap_bulk_read(vreg->regmap, vreg->base + addr, buf, len);
}

static inline int spmi_vreg_write(struct spmi_regulator *vreg, u16 addr,
				u8 *buf, int len)
{
	return regmap_bulk_write(vreg->regmap, vreg->base + addr, buf, len);
}

static int spmi_vreg_update_bits(struct spmi_regulator *vreg, u16 addr, u8 val,
		u8 mask)
{
	return regmap_update_bits(vreg->regmap, vreg->base + addr, mask, val);
}

static int spmi_regulator_common_is_enabled(struct regulator_dev *rdev)
{
	struct spmi_regulator *vreg = rdev_get_drvdata(rdev);
	u8 reg;

	spmi_vreg_read(vreg, SPMI_COMMON_REG_ENABLE, &reg, 1);

	return (reg & SPMI_COMMON_ENABLE_MASK) == SPMI_COMMON_ENABLE;
}

static int spmi_regulator_common_enable(struct regulator_dev *rdev)
{
	struct spmi_regulator *vreg = rdev_get_drvdata(rdev);

	return spmi_vreg_update_bits(vreg, SPMI_COMMON_REG_ENABLE,
		SPMI_COMMON_ENABLE, SPMI_COMMON_ENABLE_MASK);
}

static int spmi_regulator_vs_enable(struct regulator_dev *rdev)
{
	struct spmi_regulator *vreg = rdev_get_drvdata(rdev);

	if (vreg->ocp_irq) {
		vreg->ocp_count = 0;
		vreg->vs_enable_time = ktime_get();
	}

	return spmi_regulator_common_enable(rdev);
}

static int spmi_regulator_vs_ocp(struct regulator_dev *rdev)
{
	struct spmi_regulator *vreg = rdev_get_drvdata(rdev);
	u8 reg = SPMI_VS_OCP_OVERRIDE;

	return spmi_vreg_write(vreg, SPMI_VS_REG_OCP, &reg, 1);
}

static int spmi_regulator_common_disable(struct regulator_dev *rdev)
{
	struct spmi_regulator *vreg = rdev_get_drvdata(rdev);

	return spmi_vreg_update_bits(vreg, SPMI_COMMON_REG_ENABLE,
		SPMI_COMMON_DISABLE, SPMI_COMMON_ENABLE_MASK);
}

static int spmi_regulator_select_voltage(struct spmi_regulator *vreg,
					 int min_uV, int max_uV)
{
	const struct spmi_voltage_range *range;
	int uV = min_uV;
	int lim_min_uV, lim_max_uV, i, range_id, range_max_uV;
	int selector, voltage_sel;

	/* Check if request voltage is outside of physically settable range. */
	lim_min_uV = vreg->set_points->range[0].set_point_min_uV;
	lim_max_uV =
	  vreg->set_points->range[vreg->set_points->count - 1].set_point_max_uV;

	if (uV < lim_min_uV && max_uV >= lim_min_uV)
		uV = lim_min_uV;

	if (uV < lim_min_uV || uV > lim_max_uV) {
		dev_err(vreg->dev,
			"request v=[%d, %d] is outside possible v=[%d, %d]\n",
			 min_uV, max_uV, lim_min_uV, lim_max_uV);
		return -EINVAL;
	}

	/* Find the range which uV is inside of. */
	for (i = vreg->set_points->count - 1; i > 0; i--) {
		range_max_uV = vreg->set_points->range[i - 1].set_point_max_uV;
		if (uV > range_max_uV && range_max_uV > 0)
			break;
	}

	range_id = i;
	range = &vreg->set_points->range[range_id];

	/*
	 * Force uV to be an allowed set point by applying a ceiling function to
	 * the uV value.
	 */
	voltage_sel = DIV_ROUND_UP(uV - range->min_uV, range->step_uV);
	uV = voltage_sel * range->step_uV + range->min_uV;

	if (uV > max_uV) {
		dev_err(vreg->dev,
			"request v=[%d, %d] cannot be met by any set point; "
			"next set point: %d\n",
			min_uV, max_uV, uV);
		return -EINVAL;
	}

	selector = 0;
	for (i = 0; i < range_id; i++)
		selector += vreg->set_points->range[i].n_voltages;
	selector += (uV - range->set_point_min_uV) / range->step_uV;

	return selector;
}

static int spmi_sw_selector_to_hw(struct spmi_regulator *vreg,
				  unsigned selector, u8 *range_sel,
				  u8 *voltage_sel)
{
	const struct spmi_voltage_range *range, *end;

	range = vreg->set_points->range;
	end = range + vreg->set_points->count;

	for (; range < end; range++) {
		if (selector < range->n_voltages) {
			*voltage_sel = selector;
			*range_sel = range->range_sel;
			return 0;
		}

		selector -= range->n_voltages;
	}

	return -EINVAL;
}

static int spmi_hw_selector_to_sw(struct spmi_regulator *vreg, u8 hw_sel,
				  const struct spmi_voltage_range *range)
{
	int sw_sel = hw_sel;
	const struct spmi_voltage_range *r = vreg->set_points->range;

	while (r != range) {
		sw_sel += r->n_voltages;
		r++;
	}

	return sw_sel;
}

static const struct spmi_voltage_range *
spmi_regulator_find_range(struct spmi_regulator *vreg)
{
	u8 range_sel;
	const struct spmi_voltage_range *range, *end;

	range = vreg->set_points->range;
	end = range + vreg->set_points->count;

	spmi_vreg_read(vreg, SPMI_COMMON_REG_VOLTAGE_RANGE, &range_sel, 1);

	for (; range < end; range++)
		if (range->range_sel == range_sel)
			return range;

	return NULL;
}

static int spmi_regulator_select_voltage_same_range(struct spmi_regulator *vreg,
		int min_uV, int max_uV)
{
	const struct spmi_voltage_range *range;
	int uV = min_uV;
	int i, selector;

	range = spmi_regulator_find_range(vreg);
	if (!range)
		goto different_range;

	if (uV < range->min_uV && max_uV >= range->min_uV)
		uV = range->min_uV;

	if (uV < range->min_uV || uV > range->max_uV) {
		/* Current range doesn't support the requested voltage. */
		goto different_range;
	}

	/*
	 * Force uV to be an allowed set point by applying a ceiling function to
	 * the uV value.
	 */
	uV = DIV_ROUND_UP(uV - range->min_uV, range->step_uV);
	uV = uV * range->step_uV + range->min_uV;

	if (uV > max_uV) {
		/*
		 * No set point in the current voltage range is within the
		 * requested min_uV to max_uV range.
		 */
		goto different_range;
	}

	selector = 0;
	for (i = 0; i < vreg->set_points->count; i++) {
		if (uV >= vreg->set_points->range[i].set_point_min_uV
		    && uV <= vreg->set_points->range[i].set_point_max_uV) {
			selector +=
			    (uV - vreg->set_points->range[i].set_point_min_uV)
				/ vreg->set_points->range[i].step_uV;
			break;
		}

		selector += vreg->set_points->range[i].n_voltages;
	}

	if (selector >= vreg->set_points->n_voltages)
		goto different_range;

	return selector;

different_range:
	return spmi_regulator_select_voltage(vreg, min_uV, max_uV);
}

static int spmi_regulator_common_map_voltage(struct regulator_dev *rdev,
					     int min_uV, int max_uV)
{
	struct spmi_regulator *vreg = rdev_get_drvdata(rdev);

	/*
	 * Favor staying in the current voltage range if possible.  This avoids
	 * voltage spikes that occur when changing the voltage range.
	 */
	return spmi_regulator_select_voltage_same_range(vreg, min_uV, max_uV);
}

static int
spmi_regulator_common_set_voltage(struct regulator_dev *rdev, unsigned selector)
{
	struct spmi_regulator *vreg = rdev_get_drvdata(rdev);
	int ret;
	u8 buf[2];
	u8 range_sel, voltage_sel;

	ret = spmi_sw_selector_to_hw(vreg, selector, &range_sel, &voltage_sel);
	if (ret)
		return ret;

	buf[0] = range_sel;
	buf[1] = voltage_sel;
	return spmi_vreg_write(vreg, SPMI_COMMON_REG_VOLTAGE_RANGE, buf, 2);
}

static int spmi_regulator_set_voltage_time_sel(struct regulator_dev *rdev,
		unsigned int old_selector, unsigned int new_selector)
{
	struct spmi_regulator *vreg = rdev_get_drvdata(rdev);
	const struct spmi_voltage_range *range;
	int diff_uV;

	range = spmi_regulator_find_range(vreg);
	if (!range)
		return -EINVAL;

	diff_uV = abs(new_selector - old_selector) * range->step_uV;

	return DIV_ROUND_UP(diff_uV, vreg->slew_rate);
}

static int spmi_regulator_common_get_voltage(struct regulator_dev *rdev)
{
	struct spmi_regulator *vreg = rdev_get_drvdata(rdev);
	const struct spmi_voltage_range *range;
	u8 voltage_sel;

	spmi_vreg_read(vreg, SPMI_COMMON_REG_VOLTAGE_SET, &voltage_sel, 1);

	range = spmi_regulator_find_range(vreg);
	if (!range)
		return -EINVAL;

	return spmi_hw_selector_to_sw(vreg, voltage_sel, range);
}

static int spmi_regulator_single_map_voltage(struct regulator_dev *rdev,
		int min_uV, int max_uV)
{
	struct spmi_regulator *vreg = rdev_get_drvdata(rdev);

	return spmi_regulator_select_voltage(vreg, min_uV, max_uV);
}

static int spmi_regulator_single_range_set_voltage(struct regulator_dev *rdev,
						   unsigned selector)
{
	struct spmi_regulator *vreg = rdev_get_drvdata(rdev);
	u8 sel = selector;

	/*
	 * Certain types of regulators do not have a range select register so
	 * only voltage set register needs to be written.
	 */
	return spmi_vreg_write(vreg, SPMI_COMMON_REG_VOLTAGE_SET, &sel, 1);
}

static int spmi_regulator_single_range_get_voltage(struct regulator_dev *rdev)
{
	struct spmi_regulator *vreg = rdev_get_drvdata(rdev);
	u8 selector;
	int ret;

	ret = spmi_vreg_read(vreg, SPMI_COMMON_REG_VOLTAGE_SET, &selector, 1);
	if (ret)
		return ret;

	return selector;
}

static int spmi_regulator_ult_lo_smps_set_voltage(struct regulator_dev *rdev,
						  unsigned selector)
{
	struct spmi_regulator *vreg = rdev_get_drvdata(rdev);
	int ret;
	u8 range_sel, voltage_sel;

	ret = spmi_sw_selector_to_hw(vreg, selector, &range_sel, &voltage_sel);
	if (ret)
		return ret;

	/*
	 * Calculate VSET based on range
	 * In case of range 0: voltage_sel is a 7 bit value, can be written
	 *			witout any modification.
	 * In case of range 1: voltage_sel is a 5 bit value, bits[7-5] set to
	 *			[011].
	 */
	if (range_sel == 1)
		voltage_sel |= ULT_SMPS_RANGE_SPLIT;

	return spmi_vreg_update_bits(vreg, SPMI_COMMON_REG_VOLTAGE_SET,
				     voltage_sel, 0xff);
}

static int spmi_regulator_ult_lo_smps_get_voltage(struct regulator_dev *rdev)
{
	struct spmi_regulator *vreg = rdev_get_drvdata(rdev);
	const struct spmi_voltage_range *range;
	u8 voltage_sel;

	spmi_vreg_read(vreg, SPMI_COMMON_REG_VOLTAGE_SET, &voltage_sel, 1);

	range = spmi_regulator_find_range(vreg);
	if (!range)
		return -EINVAL;

	if (range->range_sel == 1)
		voltage_sel &= ~ULT_SMPS_RANGE_SPLIT;

	return spmi_hw_selector_to_sw(vreg, voltage_sel, range);
}

static int spmi_regulator_common_list_voltage(struct regulator_dev *rdev,
			unsigned selector)
{
	struct spmi_regulator *vreg = rdev_get_drvdata(rdev);
	int uV = 0;
	int i;

	if (selector >= vreg->set_points->n_voltages)
		return 0;

	for (i = 0; i < vreg->set_points->count; i++) {
		if (selector < vreg->set_points->range[i].n_voltages) {
			uV = selector * vreg->set_points->range[i].step_uV
				+ vreg->set_points->range[i].set_point_min_uV;
			break;
		}

		selector -= vreg->set_points->range[i].n_voltages;
	}

	return uV;
}

static int
spmi_regulator_common_set_bypass(struct regulator_dev *rdev, bool enable)
{
	struct spmi_regulator *vreg = rdev_get_drvdata(rdev);
	u8 mask = SPMI_COMMON_MODE_BYPASS_MASK;
	u8 val = 0;

	if (enable)
		val = mask;

	return spmi_vreg_update_bits(vreg, SPMI_COMMON_REG_MODE, val, mask);
}

static int
spmi_regulator_common_get_bypass(struct regulator_dev *rdev, bool *enable)
{
	struct spmi_regulator *vreg = rdev_get_drvdata(rdev);
	u8 val;
	int ret;

	ret = spmi_vreg_read(vreg, SPMI_COMMON_REG_MODE, &val, 1);
	*enable = val & SPMI_COMMON_MODE_BYPASS_MASK;

	return ret;
}

static unsigned int spmi_regulator_common_get_mode(struct regulator_dev *rdev)
{
	struct spmi_regulator *vreg = rdev_get_drvdata(rdev);
	u8 reg;

	spmi_vreg_read(vreg, SPMI_COMMON_REG_MODE, &reg, 1);

	if (reg & SPMI_COMMON_MODE_HPM_MASK)
		return REGULATOR_MODE_NORMAL;

	if (reg & SPMI_COMMON_MODE_AUTO_MASK)
		return REGULATOR_MODE_FAST;

	return REGULATOR_MODE_IDLE;
}

static int
spmi_regulator_common_set_mode(struct regulator_dev *rdev, unsigned int mode)
{
	struct spmi_regulator *vreg = rdev_get_drvdata(rdev);
	u8 mask = SPMI_COMMON_MODE_HPM_MASK | SPMI_COMMON_MODE_AUTO_MASK;
	u8 val = 0;

	if (mode == REGULATOR_MODE_NORMAL)
		val = SPMI_COMMON_MODE_HPM_MASK;
	else if (mode == REGULATOR_MODE_FAST)
		val = SPMI_COMMON_MODE_AUTO_MASK;

	return spmi_vreg_update_bits(vreg, SPMI_COMMON_REG_MODE, val, mask);
}

static int
spmi_regulator_common_set_load(struct regulator_dev *rdev, int load_uA)
{
	struct spmi_regulator *vreg = rdev_get_drvdata(rdev);
	unsigned int mode;

	if (load_uA >= vreg->hpm_min_load)
		mode = REGULATOR_MODE_NORMAL;
	else
		mode = REGULATOR_MODE_IDLE;

	return spmi_regulator_common_set_mode(rdev, mode);
}

static int spmi_regulator_common_set_pull_down(struct regulator_dev *rdev)
{
	struct spmi_regulator *vreg = rdev_get_drvdata(rdev);
	unsigned int mask = SPMI_COMMON_PULL_DOWN_ENABLE_MASK;

	return spmi_vreg_update_bits(vreg, SPMI_COMMON_REG_PULL_DOWN,
				     mask, mask);
}

static int spmi_regulator_common_set_soft_start(struct regulator_dev *rdev)
{
	struct spmi_regulator *vreg = rdev_get_drvdata(rdev);
	unsigned int mask = SPMI_LDO_SOFT_START_ENABLE_MASK;

	return spmi_vreg_update_bits(vreg, SPMI_COMMON_REG_SOFT_START,
				     mask, mask);
}

static int spmi_regulator_set_ilim(struct regulator_dev *rdev, int ilim_uA)
{
	struct spmi_regulator *vreg = rdev_get_drvdata(rdev);
	enum spmi_regulator_logical_type type = vreg->logical_type;
	unsigned int current_reg;
	u8 reg;
	u8 mask = SPMI_BOOST_CURRENT_LIMIT_MASK |
		  SPMI_BOOST_CURRENT_LIMIT_ENABLE_MASK;
	int max = (SPMI_BOOST_CURRENT_LIMIT_MASK + 1) * 500;

	if (type == SPMI_REGULATOR_LOGICAL_TYPE_BOOST)
		current_reg = SPMI_BOOST_REG_CURRENT_LIMIT;
	else
		current_reg = SPMI_BOOST_BYP_REG_CURRENT_LIMIT;

	if (ilim_uA > max || ilim_uA <= 0)
		return -EINVAL;

	reg = (ilim_uA - 1) / 500;
	reg |= SPMI_BOOST_CURRENT_LIMIT_ENABLE_MASK;

	return spmi_vreg_update_bits(vreg, current_reg, reg, mask);
}

static int spmi_regulator_vs_clear_ocp(struct spmi_regulator *vreg)
{
	int ret;

	ret = spmi_vreg_update_bits(vreg, SPMI_COMMON_REG_ENABLE,
		SPMI_COMMON_DISABLE, SPMI_COMMON_ENABLE_MASK);

	vreg->vs_enable_time = ktime_get();

	ret = spmi_vreg_update_bits(vreg, SPMI_COMMON_REG_ENABLE,
		SPMI_COMMON_ENABLE, SPMI_COMMON_ENABLE_MASK);

	return ret;
}

static void spmi_regulator_vs_ocp_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct spmi_regulator *vreg
		= container_of(dwork, struct spmi_regulator, ocp_work);

	spmi_regulator_vs_clear_ocp(vreg);
}

static irqreturn_t spmi_regulator_vs_ocp_isr(int irq, void *data)
{
	struct spmi_regulator *vreg = data;
	ktime_t ocp_irq_time;
	s64 ocp_trigger_delay_us;

	ocp_irq_time = ktime_get();
	ocp_trigger_delay_us = ktime_us_delta(ocp_irq_time,
						vreg->vs_enable_time);

	/*
	 * Reset the OCP count if there is a large delay between switch enable
	 * and when OCP triggers.  This is indicative of a hotplug event as
	 * opposed to a fault.
	 */
	if (ocp_trigger_delay_us > SPMI_VS_OCP_FAULT_DELAY_US)
		vreg->ocp_count = 0;

	/* Wait for switch output to settle back to 0 V after OCP triggered. */
	udelay(SPMI_VS_OCP_FALL_DELAY_US);

	vreg->ocp_count++;

	if (vreg->ocp_count == 1) {
		/* Immediately clear the over current condition. */
		spmi_regulator_vs_clear_ocp(vreg);
	} else if (vreg->ocp_count <= vreg->ocp_max_retries) {
		/* Schedule the over current clear task to run later. */
		schedule_delayed_work(&vreg->ocp_work,
			msecs_to_jiffies(vreg->ocp_retry_delay_ms) + 1);
	} else {
		dev_err(vreg->dev,
			"OCP triggered %d times; no further retries\n",
			vreg->ocp_count);
	}

	return IRQ_HANDLED;
}

static struct regulator_ops spmi_smps_ops = {
	.enable			= spmi_regulator_common_enable,
	.disable		= spmi_regulator_common_disable,
	.is_enabled		= spmi_regulator_common_is_enabled,
	.set_voltage_sel	= spmi_regulator_common_set_voltage,
	.set_voltage_time_sel	= spmi_regulator_set_voltage_time_sel,
	.get_voltage_sel	= spmi_regulator_common_get_voltage,
	.map_voltage		= spmi_regulator_common_map_voltage,
	.list_voltage		= spmi_regulator_common_list_voltage,
	.set_mode		= spmi_regulator_common_set_mode,
	.get_mode		= spmi_regulator_common_get_mode,
	.set_load		= spmi_regulator_common_set_load,
	.set_pull_down		= spmi_regulator_common_set_pull_down,
};

static struct regulator_ops spmi_ldo_ops = {
	.enable			= spmi_regulator_common_enable,
	.disable		= spmi_regulator_common_disable,
	.is_enabled		= spmi_regulator_common_is_enabled,
	.set_voltage_sel	= spmi_regulator_common_set_voltage,
	.get_voltage_sel	= spmi_regulator_common_get_voltage,
	.map_voltage		= spmi_regulator_common_map_voltage,
	.list_voltage		= spmi_regulator_common_list_voltage,
	.set_mode		= spmi_regulator_common_set_mode,
	.get_mode		= spmi_regulator_common_get_mode,
	.set_load		= spmi_regulator_common_set_load,
	.set_bypass		= spmi_regulator_common_set_bypass,
	.get_bypass		= spmi_regulator_common_get_bypass,
	.set_pull_down		= spmi_regulator_common_set_pull_down,
	.set_soft_start		= spmi_regulator_common_set_soft_start,
};

static struct regulator_ops spmi_ln_ldo_ops = {
	.enable			= spmi_regulator_common_enable,
	.disable		= spmi_regulator_common_disable,
	.is_enabled		= spmi_regulator_common_is_enabled,
	.set_voltage_sel	= spmi_regulator_common_set_voltage,
	.get_voltage_sel	= spmi_regulator_common_get_voltage,
	.map_voltage		= spmi_regulator_common_map_voltage,
	.list_voltage		= spmi_regulator_common_list_voltage,
	.set_bypass		= spmi_regulator_common_set_bypass,
	.get_bypass		= spmi_regulator_common_get_bypass,
};

static struct regulator_ops spmi_vs_ops = {
	.enable			= spmi_regulator_vs_enable,
	.disable		= spmi_regulator_common_disable,
	.is_enabled		= spmi_regulator_common_is_enabled,
	.set_pull_down		= spmi_regulator_common_set_pull_down,
	.set_soft_start		= spmi_regulator_common_set_soft_start,
	.set_over_current_protection = spmi_regulator_vs_ocp,
	.set_mode		= spmi_regulator_common_set_mode,
	.get_mode		= spmi_regulator_common_get_mode,
};

static struct regulator_ops spmi_boost_ops = {
	.enable			= spmi_regulator_common_enable,
	.disable		= spmi_regulator_common_disable,
	.is_enabled		= spmi_regulator_common_is_enabled,
	.set_voltage_sel	= spmi_regulator_single_range_set_voltage,
	.get_voltage_sel	= spmi_regulator_single_range_get_voltage,
	.map_voltage		= spmi_regulator_single_map_voltage,
	.list_voltage		= spmi_regulator_common_list_voltage,
	.set_input_current_limit = spmi_regulator_set_ilim,
};

static struct regulator_ops spmi_ftsmps_ops = {
	.enable			= spmi_regulator_common_enable,
	.disable		= spmi_regulator_common_disable,
	.is_enabled		= spmi_regulator_common_is_enabled,
	.set_voltage_sel	= spmi_regulator_common_set_voltage,
	.set_voltage_time_sel	= spmi_regulator_set_voltage_time_sel,
	.get_voltage_sel	= spmi_regulator_common_get_voltage,
	.map_voltage		= spmi_regulator_common_map_voltage,
	.list_voltage		= spmi_regulator_common_list_voltage,
	.set_mode		= spmi_regulator_common_set_mode,
	.get_mode		= spmi_regulator_common_get_mode,
	.set_load		= spmi_regulator_common_set_load,
	.set_pull_down		= spmi_regulator_common_set_pull_down,
};

static struct regulator_ops spmi_ult_lo_smps_ops = {
	.enable			= spmi_regulator_common_enable,
	.disable		= spmi_regulator_common_disable,
	.is_enabled		= spmi_regulator_common_is_enabled,
	.set_voltage_sel	= spmi_regulator_ult_lo_smps_set_voltage,
	.set_voltage_time_sel	= spmi_regulator_set_voltage_time_sel,
	.get_voltage_sel	= spmi_regulator_ult_lo_smps_get_voltage,
	.list_voltage		= spmi_regulator_common_list_voltage,
	.set_mode		= spmi_regulator_common_set_mode,
	.get_mode		= spmi_regulator_common_get_mode,
	.set_load		= spmi_regulator_common_set_load,
	.set_pull_down		= spmi_regulator_common_set_pull_down,
};

static struct regulator_ops spmi_ult_ho_smps_ops = {
	.enable			= spmi_regulator_common_enable,
	.disable		= spmi_regulator_common_disable,
	.is_enabled		= spmi_regulator_common_is_enabled,
	.set_voltage_sel	= spmi_regulator_single_range_set_voltage,
	.set_voltage_time_sel	= spmi_regulator_set_voltage_time_sel,
	.get_voltage_sel	= spmi_regulator_single_range_get_voltage,
	.map_voltage		= spmi_regulator_single_map_voltage,
	.list_voltage		= spmi_regulator_common_list_voltage,
	.set_mode		= spmi_regulator_common_set_mode,
	.get_mode		= spmi_regulator_common_get_mode,
	.set_load		= spmi_regulator_common_set_load,
	.set_pull_down		= spmi_regulator_common_set_pull_down,
};

static struct regulator_ops spmi_ult_ldo_ops = {
	.enable			= spmi_regulator_common_enable,
	.disable		= spmi_regulator_common_disable,
	.is_enabled		= spmi_regulator_common_is_enabled,
	.set_voltage_sel	= spmi_regulator_single_range_set_voltage,
	.get_voltage_sel	= spmi_regulator_single_range_get_voltage,
	.map_voltage		= spmi_regulator_single_map_voltage,
	.list_voltage		= spmi_regulator_common_list_voltage,
	.set_mode		= spmi_regulator_common_set_mode,
	.get_mode		= spmi_regulator_common_get_mode,
	.set_load		= spmi_regulator_common_set_load,
	.set_bypass		= spmi_regulator_common_set_bypass,
	.get_bypass		= spmi_regulator_common_get_bypass,
	.set_pull_down		= spmi_regulator_common_set_pull_down,
	.set_soft_start		= spmi_regulator_common_set_soft_start,
};

/* Maximum possible digital major revision value */
#define INF 0xFF

static const struct spmi_regulator_mapping supported_regulators[] = {
	/*           type subtype dig_min dig_max ltype ops setpoints hpm_min */
	SPMI_VREG(BUCK,  GP_CTL,   0, INF, SMPS,   smps,   smps,   100000),
	SPMI_VREG(LDO,   N300,     0, INF, LDO,    ldo,    nldo1,   10000),
	SPMI_VREG(LDO,   N600,     0,   0, LDO,    ldo,    nldo2,   10000),
	SPMI_VREG(LDO,   N1200,    0,   0, LDO,    ldo,    nldo2,   10000),
	SPMI_VREG(LDO,   N600,     1, INF, LDO,    ldo,    nldo3,   10000),
	SPMI_VREG(LDO,   N1200,    1, INF, LDO,    ldo,    nldo3,   10000),
	SPMI_VREG(LDO,   N600_ST,  0,   0, LDO,    ldo,    nldo2,   10000),
	SPMI_VREG(LDO,   N1200_ST, 0,   0, LDO,    ldo,    nldo2,   10000),
	SPMI_VREG(LDO,   N600_ST,  1, INF, LDO,    ldo,    nldo3,   10000),
	SPMI_VREG(LDO,   N1200_ST, 1, INF, LDO,    ldo,    nldo3,   10000),
	SPMI_VREG(LDO,   P50,      0, INF, LDO,    ldo,    pldo,     5000),
	SPMI_VREG(LDO,   P150,     0, INF, LDO,    ldo,    pldo,    10000),
	SPMI_VREG(LDO,   P300,     0, INF, LDO,    ldo,    pldo,    10000),
	SPMI_VREG(LDO,   P600,     0, INF, LDO,    ldo,    pldo,    10000),
	SPMI_VREG(LDO,   P1200,    0, INF, LDO,    ldo,    pldo,    10000),
	SPMI_VREG(LDO,   LN,       0, INF, LN_LDO, ln_ldo, ln_ldo,      0),
	SPMI_VREG(LDO,   LV_P50,   0, INF, LDO,    ldo,    pldo,     5000),
	SPMI_VREG(LDO,   LV_P150,  0, INF, LDO,    ldo,    pldo,    10000),
	SPMI_VREG(LDO,   LV_P300,  0, INF, LDO,    ldo,    pldo,    10000),
	SPMI_VREG(LDO,   LV_P600,  0, INF, LDO,    ldo,    pldo,    10000),
	SPMI_VREG(LDO,   LV_P1200, 0, INF, LDO,    ldo,    pldo,    10000),
	SPMI_VREG_VS(LV100,        0, INF),
	SPMI_VREG_VS(LV300,        0, INF),
	SPMI_VREG_VS(MV300,        0, INF),
	SPMI_VREG_VS(MV500,        0, INF),
	SPMI_VREG_VS(HDMI,         0, INF),
	SPMI_VREG_VS(OTG,          0, INF),
	SPMI_VREG(BOOST, 5V_BOOST, 0, INF, BOOST,  boost,  boost,       0),
	SPMI_VREG(FTS,   FTS_CTL,  0, INF, FTSMPS, ftsmps, ftsmps, 100000),
	SPMI_VREG(FTS, FTS2p5_CTL, 0, INF, FTSMPS, ftsmps, ftsmps2p5, 100000),
	SPMI_VREG(BOOST_BYP, BB_2A, 0, INF, BOOST_BYP, boost, boost_byp, 0),
	SPMI_VREG(ULT_BUCK, ULT_HF_CTL1, 0, INF, ULT_LO_SMPS, ult_lo_smps,
						ult_lo_smps,   100000),
	SPMI_VREG(ULT_BUCK, ULT_HF_CTL2, 0, INF, ULT_LO_SMPS, ult_lo_smps,
						ult_lo_smps,   100000),
	SPMI_VREG(ULT_BUCK, ULT_HF_CTL3, 0, INF, ULT_LO_SMPS, ult_lo_smps,
						ult_lo_smps,   100000),
	SPMI_VREG(ULT_BUCK, ULT_HF_CTL4, 0, INF, ULT_HO_SMPS, ult_ho_smps,
						ult_ho_smps,   100000),
	SPMI_VREG(ULT_LDO, N300_ST, 0, INF, ULT_LDO, ult_ldo, ult_nldo, 10000),
	SPMI_VREG(ULT_LDO, N600_ST, 0, INF, ULT_LDO, ult_ldo, ult_nldo, 10000),
	SPMI_VREG(ULT_LDO, N900_ST, 0, INF, ULT_LDO, ult_ldo, ult_nldo, 10000),
	SPMI_VREG(ULT_LDO, N1200_ST, 0, INF, ULT_LDO, ult_ldo, ult_nldo, 10000),
	SPMI_VREG(ULT_LDO, LV_P150,  0, INF, ULT_LDO, ult_ldo, ult_pldo, 10000),
	SPMI_VREG(ULT_LDO, LV_P300,  0, INF, ULT_LDO, ult_ldo, ult_pldo, 10000),
	SPMI_VREG(ULT_LDO, LV_P450,  0, INF, ULT_LDO, ult_ldo, ult_pldo, 10000),
	SPMI_VREG(ULT_LDO, P600,     0, INF, ULT_LDO, ult_ldo, ult_pldo, 10000),
	SPMI_VREG(ULT_LDO, P150,     0, INF, ULT_LDO, ult_ldo, ult_pldo, 10000),
	SPMI_VREG(ULT_LDO, P50,     0, INF, ULT_LDO, ult_ldo, ult_pldo, 5000),
};

static void spmi_calculate_num_voltages(struct spmi_voltage_set_points *points)
{
	unsigned int n;
	struct spmi_voltage_range *range = points->range;

	for (; range < points->range + points->count; range++) {
		n = 0;
		if (range->set_point_max_uV) {
			n = range->set_point_max_uV - range->set_point_min_uV;
			n = (n / range->step_uV) + 1;
		}
		range->n_voltages = n;
		points->n_voltages += n;
	}
}

static int spmi_regulator_match(struct spmi_regulator *vreg, u16 force_type)
{
	const struct spmi_regulator_mapping *mapping;
	int ret, i;
	u32 dig_major_rev;
	u8 version[SPMI_COMMON_REG_SUBTYPE - SPMI_COMMON_REG_DIG_MAJOR_REV + 1];
	u8 type, subtype;

	ret = spmi_vreg_read(vreg, SPMI_COMMON_REG_DIG_MAJOR_REV, version,
		ARRAY_SIZE(version));
	if (ret) {
		dev_dbg(vreg->dev, "could not read version registers\n");
		return ret;
	}
	dig_major_rev	= version[SPMI_COMMON_REG_DIG_MAJOR_REV
					- SPMI_COMMON_REG_DIG_MAJOR_REV];
	if (!force_type) {
		type		= version[SPMI_COMMON_REG_TYPE -
					  SPMI_COMMON_REG_DIG_MAJOR_REV];
		subtype		= version[SPMI_COMMON_REG_SUBTYPE -
					  SPMI_COMMON_REG_DIG_MAJOR_REV];
	} else {
		type = force_type >> 8;
		subtype = force_type;
	}

	for (i = 0; i < ARRAY_SIZE(supported_regulators); i++) {
		mapping = &supported_regulators[i];
		if (mapping->type == type && mapping->subtype == subtype
		    && mapping->revision_min <= dig_major_rev
		    && mapping->revision_max >= dig_major_rev)
			goto found;
	}

	dev_err(vreg->dev,
		"unsupported regulator: name=%s type=0x%02X, subtype=0x%02X, dig major rev=0x%02X\n",
		vreg->desc.name, type, subtype, dig_major_rev);

	return -ENODEV;

found:
	vreg->logical_type	= mapping->logical_type;
	vreg->set_points	= mapping->set_points;
	vreg->hpm_min_load	= mapping->hpm_min_load;
	vreg->desc.ops		= mapping->ops;

	if (mapping->set_points) {
		if (!mapping->set_points->n_voltages)
			spmi_calculate_num_voltages(mapping->set_points);
		vreg->desc.n_voltages = mapping->set_points->n_voltages;
	}

	return 0;
}

static int spmi_regulator_init_slew_rate(struct spmi_regulator *vreg)
{
	int ret;
	u8 reg = 0;
	int step, delay, slew_rate, step_delay;
	const struct spmi_voltage_range *range;

	ret = spmi_vreg_read(vreg, SPMI_COMMON_REG_STEP_CTRL, &reg, 1);
	if (ret) {
		dev_err(vreg->dev, "spmi read failed, ret=%d\n", ret);
		return ret;
	}

	range = spmi_regulator_find_range(vreg);
	if (!range)
		return -EINVAL;

	switch (vreg->logical_type) {
	case SPMI_REGULATOR_LOGICAL_TYPE_FTSMPS:
		step_delay = SPMI_FTSMPS_STEP_DELAY;
		break;
	default:
		step_delay = SPMI_DEFAULT_STEP_DELAY;
		break;
	}

	step = reg & SPMI_FTSMPS_STEP_CTRL_STEP_MASK;
	step >>= SPMI_FTSMPS_STEP_CTRL_STEP_SHIFT;

	delay = reg & SPMI_FTSMPS_STEP_CTRL_DELAY_MASK;
	delay >>= SPMI_FTSMPS_STEP_CTRL_DELAY_SHIFT;

	/* slew_rate has units of uV/us */
	slew_rate = SPMI_FTSMPS_CLOCK_RATE * range->step_uV * (1 << step);
	slew_rate /= 1000 * (step_delay << delay);
	slew_rate *= SPMI_FTSMPS_STEP_MARGIN_NUM;
	slew_rate /= SPMI_FTSMPS_STEP_MARGIN_DEN;

	/* Ensure that the slew rate is greater than 0 */
	vreg->slew_rate = max(slew_rate, 1);

	return ret;
}

static int spmi_regulator_init_registers(struct spmi_regulator *vreg,
				const struct spmi_regulator_init_data *data)
{
	int ret;
	enum spmi_regulator_logical_type type;
	u8 ctrl_reg[8], reg, mask;

	type = vreg->logical_type;

	ret = spmi_vreg_read(vreg, SPMI_COMMON_REG_VOLTAGE_RANGE, ctrl_reg, 8);
	if (ret)
		return ret;

	/* Set up enable pin control. */
	if ((type == SPMI_REGULATOR_LOGICAL_TYPE_SMPS
	     || type == SPMI_REGULATOR_LOGICAL_TYPE_LDO
	     || type == SPMI_REGULATOR_LOGICAL_TYPE_VS)
	    && !(data->pin_ctrl_enable
			& SPMI_REGULATOR_PIN_CTRL_ENABLE_HW_DEFAULT)) {
		ctrl_reg[SPMI_COMMON_IDX_ENABLE] &=
			~SPMI_COMMON_ENABLE_FOLLOW_ALL_MASK;
		ctrl_reg[SPMI_COMMON_IDX_ENABLE] |=
		    data->pin_ctrl_enable & SPMI_COMMON_ENABLE_FOLLOW_ALL_MASK;
	}

	/* Set up mode pin control. */
	if ((type == SPMI_REGULATOR_LOGICAL_TYPE_SMPS
	    || type == SPMI_REGULATOR_LOGICAL_TYPE_LDO)
		&& !(data->pin_ctrl_hpm
			& SPMI_REGULATOR_PIN_CTRL_HPM_HW_DEFAULT)) {
		ctrl_reg[SPMI_COMMON_IDX_MODE] &=
			~SPMI_COMMON_MODE_FOLLOW_ALL_MASK;
		ctrl_reg[SPMI_COMMON_IDX_MODE] |=
			data->pin_ctrl_hpm & SPMI_COMMON_MODE_FOLLOW_ALL_MASK;
	}

	if (type == SPMI_REGULATOR_LOGICAL_TYPE_VS
	   && !(data->pin_ctrl_hpm & SPMI_REGULATOR_PIN_CTRL_HPM_HW_DEFAULT)) {
		ctrl_reg[SPMI_COMMON_IDX_MODE] &=
			~SPMI_COMMON_MODE_FOLLOW_AWAKE_MASK;
		ctrl_reg[SPMI_COMMON_IDX_MODE] |=
		       data->pin_ctrl_hpm & SPMI_COMMON_MODE_FOLLOW_AWAKE_MASK;
	}

	if ((type == SPMI_REGULATOR_LOGICAL_TYPE_ULT_LO_SMPS
		|| type == SPMI_REGULATOR_LOGICAL_TYPE_ULT_HO_SMPS
		|| type == SPMI_REGULATOR_LOGICAL_TYPE_ULT_LDO)
		&& !(data->pin_ctrl_hpm
			& SPMI_REGULATOR_PIN_CTRL_HPM_HW_DEFAULT)) {
		ctrl_reg[SPMI_COMMON_IDX_MODE] &=
			~SPMI_COMMON_MODE_FOLLOW_AWAKE_MASK;
		ctrl_reg[SPMI_COMMON_IDX_MODE] |=
		       data->pin_ctrl_hpm & SPMI_COMMON_MODE_FOLLOW_AWAKE_MASK;
	}

	/* Write back any control register values that were modified. */
	ret = spmi_vreg_write(vreg, SPMI_COMMON_REG_VOLTAGE_RANGE, ctrl_reg, 8);
	if (ret)
		return ret;

	/* Set soft start strength and over current protection for VS. */
	if (type == SPMI_REGULATOR_LOGICAL_TYPE_VS) {
		if (data->vs_soft_start_strength
				!= SPMI_VS_SOFT_START_STR_HW_DEFAULT) {
			reg = data->vs_soft_start_strength
				& SPMI_VS_SOFT_START_SEL_MASK;
			mask = SPMI_VS_SOFT_START_SEL_MASK;
			return spmi_vreg_update_bits(vreg,
						     SPMI_VS_REG_SOFT_START,
						     reg, mask);
		}
	}

	return 0;
}

static void spmi_regulator_get_dt_config(struct spmi_regulator *vreg,
		struct device_node *node, struct spmi_regulator_init_data *data)
{
	/*
	 * Initialize configuration parameters to use hardware default in case
	 * no value is specified via device tree.
	 */
	data->pin_ctrl_enable	    = SPMI_REGULATOR_PIN_CTRL_ENABLE_HW_DEFAULT;
	data->pin_ctrl_hpm	    = SPMI_REGULATOR_PIN_CTRL_HPM_HW_DEFAULT;
	data->vs_soft_start_strength	= SPMI_VS_SOFT_START_STR_HW_DEFAULT;

	/* These bindings are optional, so it is okay if they aren't found. */
	of_property_read_u32(node, "qcom,ocp-max-retries",
		&vreg->ocp_max_retries);
	of_property_read_u32(node, "qcom,ocp-retry-delay",
		&vreg->ocp_retry_delay_ms);
	of_property_read_u32(node, "qcom,pin-ctrl-enable",
		&data->pin_ctrl_enable);
	of_property_read_u32(node, "qcom,pin-ctrl-hpm", &data->pin_ctrl_hpm);
	of_property_read_u32(node, "qcom,vs-soft-start-strength",
		&data->vs_soft_start_strength);
}

static unsigned int spmi_regulator_of_map_mode(unsigned int mode)
{
	if (mode == 1)
		return REGULATOR_MODE_NORMAL;
	if (mode == 2)
		return REGULATOR_MODE_FAST;

	return REGULATOR_MODE_IDLE;
}

static int spmi_regulator_of_parse(struct device_node *node,
			    const struct regulator_desc *desc,
			    struct regulator_config *config)
{
	struct spmi_regulator_init_data data = { };
	struct spmi_regulator *vreg = config->driver_data;
	struct device *dev = config->dev;
	int ret;

	spmi_regulator_get_dt_config(vreg, node, &data);

	if (!vreg->ocp_max_retries)
		vreg->ocp_max_retries = SPMI_VS_OCP_DEFAULT_MAX_RETRIES;
	if (!vreg->ocp_retry_delay_ms)
		vreg->ocp_retry_delay_ms = SPMI_VS_OCP_DEFAULT_RETRY_DELAY_MS;

	ret = spmi_regulator_init_registers(vreg, &data);
	if (ret) {
		dev_err(dev, "common initialization failed, ret=%d\n", ret);
		return ret;
	}

	switch (vreg->logical_type) {
	case SPMI_REGULATOR_LOGICAL_TYPE_FTSMPS:
	case SPMI_REGULATOR_LOGICAL_TYPE_ULT_LO_SMPS:
	case SPMI_REGULATOR_LOGICAL_TYPE_ULT_HO_SMPS:
	case SPMI_REGULATOR_LOGICAL_TYPE_SMPS:
		ret = spmi_regulator_init_slew_rate(vreg);
		if (ret)
			return ret;
	default:
		break;
	}

	if (vreg->logical_type != SPMI_REGULATOR_LOGICAL_TYPE_VS)
		vreg->ocp_irq = 0;

	if (vreg->ocp_irq) {
		ret = devm_request_irq(dev, vreg->ocp_irq,
			spmi_regulator_vs_ocp_isr, IRQF_TRIGGER_RISING, "ocp",
			vreg);
		if (ret < 0) {
			dev_err(dev, "failed to request irq %d, ret=%d\n",
				vreg->ocp_irq, ret);
			return ret;
		}

		INIT_DELAYED_WORK(&vreg->ocp_work, spmi_regulator_vs_ocp_work);
	}

	return 0;
}

static const struct spmi_regulator_data pm8941_regulators[] = {
	{ "s1", 0x1400, "vdd_s1", },
	{ "s2", 0x1700, "vdd_s2", },
	{ "s3", 0x1a00, "vdd_s3", },
	{ "s4", 0xa000, },
	{ "l1", 0x4000, "vdd_l1_l3", },
	{ "l2", 0x4100, "vdd_l2_lvs_1_2_3", },
	{ "l3", 0x4200, "vdd_l1_l3", },
	{ "l4", 0x4300, "vdd_l4_l11", },
	{ "l5", 0x4400, "vdd_l5_l7", NULL, 0x0410 },
	{ "l6", 0x4500, "vdd_l6_l12_l14_l15", },
	{ "l7", 0x4600, "vdd_l5_l7", NULL, 0x0410 },
	{ "l8", 0x4700, "vdd_l8_l16_l18_19", },
	{ "l9", 0x4800, "vdd_l9_l10_l17_l22", },
	{ "l10", 0x4900, "vdd_l9_l10_l17_l22", },
	{ "l11", 0x4a00, "vdd_l4_l11", },
	{ "l12", 0x4b00, "vdd_l6_l12_l14_l15", },
	{ "l13", 0x4c00, "vdd_l13_l20_l23_l24", },
	{ "l14", 0x4d00, "vdd_l6_l12_l14_l15", },
	{ "l15", 0x4e00, "vdd_l6_l12_l14_l15", },
	{ "l16", 0x4f00, "vdd_l8_l16_l18_19", },
	{ "l17", 0x5000, "vdd_l9_l10_l17_l22", },
	{ "l18", 0x5100, "vdd_l8_l16_l18_19", },
	{ "l19", 0x5200, "vdd_l8_l16_l18_19", },
	{ "l20", 0x5300, "vdd_l13_l20_l23_l24", },
	{ "l21", 0x5400, "vdd_l21", },
	{ "l22", 0x5500, "vdd_l9_l10_l17_l22", },
	{ "l23", 0x5600, "vdd_l13_l20_l23_l24", },
	{ "l24", 0x5700, "vdd_l13_l20_l23_l24", },
	{ "lvs1", 0x8000, "vdd_l2_lvs_1_2_3", },
	{ "lvs2", 0x8100, "vdd_l2_lvs_1_2_3", },
	{ "lvs3", 0x8200, "vdd_l2_lvs_1_2_3", },
	{ "5vs1", 0x8300, "vin_5vs", "ocp-5vs1", },
	{ "5vs2", 0x8400, "vin_5vs", "ocp-5vs2", },
	{ }
};

static const struct spmi_regulator_data pm8841_regulators[] = {
	{ "s1", 0x1400, "vdd_s1", },
	{ "s2", 0x1700, "vdd_s2", NULL, 0x1c08 },
	{ "s3", 0x1a00, "vdd_s3", },
	{ "s4", 0x1d00, "vdd_s4", NULL, 0x1c08 },
	{ "s5", 0x2000, "vdd_s5", NULL, 0x1c08 },
	{ "s6", 0x2300, "vdd_s6", NULL, 0x1c08 },
	{ "s7", 0x2600, "vdd_s7", NULL, 0x1c08 },
	{ "s8", 0x2900, "vdd_s8", NULL, 0x1c08 },
	{ }
};

static const struct spmi_regulator_data pm8916_regulators[] = {
	{ "s1", 0x1400, "vdd_s1", },
	{ "s2", 0x1700, "vdd_s2", },
	{ "s3", 0x1a00, "vdd_s3", },
	{ "s4", 0x1d00, "vdd_s4", },
	{ "l1", 0x4000, "vdd_l1_l3", },
	{ "l2", 0x4100, "vdd_l2", },
	{ "l3", 0x4200, "vdd_l1_l3", },
	{ "l4", 0x4300, "vdd_l4_l5_l6", },
	{ "l5", 0x4400, "vdd_l4_l5_l6", },
	{ "l6", 0x4500, "vdd_l4_l5_l6", },
	{ "l7", 0x4600, "vdd_l7", },
	{ "l8", 0x4700, "vdd_l8_l11_l14_l15_l16", },
	{ "l9", 0x4800, "vdd_l9_l10_l12_l13_l17_l18", },
	{ "l10", 0x4900, "vdd_l9_l10_l12_l13_l17_l18", },
	{ "l11", 0x4a00, "vdd_l8_l11_l14_l15_l16", },
	{ "l12", 0x4b00, "vdd_l9_l10_l12_l13_l17_l18", },
	{ "l13", 0x4c00, "vdd_l9_l10_l12_l13_l17_l18", },
	{ "l14", 0x4d00, "vdd_l8_l11_l14_l15_l16", },
	{ "l15", 0x4e00, "vdd_l8_l11_l14_l15_l16", },
	{ "l16", 0x4f00, "vdd_l8_l11_l14_l15_l16", },
	{ "l17", 0x5000, "vdd_l9_l10_l12_l13_l17_l18", },
	{ "l18", 0x5100, "vdd_l9_l10_l12_l13_l17_l18", },
	{ }
};

static const struct spmi_regulator_data pm8994_regulators[] = {
	{ "s1", 0x1400, "vdd_s1", },
	{ "s2", 0x1700, "vdd_s2", },
	{ "s3", 0x1a00, "vdd_s3", },
	{ "s4", 0x1d00, "vdd_s4", },
	{ "s5", 0x2000, "vdd_s5", },
	{ "s6", 0x2300, "vdd_s6", },
	{ "s7", 0x2600, "vdd_s7", },
	{ "s8", 0x2900, "vdd_s8", },
	{ "s9", 0x2c00, "vdd_s9", },
	{ "s10", 0x2f00, "vdd_s10", },
	{ "s11", 0x3200, "vdd_s11", },
	{ "s12", 0x3500, "vdd_s12", },
	{ "l1", 0x4000, "vdd_l1", },
	{ "l2", 0x4100, "vdd_l2_l26_l28", },
	{ "l3", 0x4200, "vdd_l3_l11", },
	{ "l4", 0x4300, "vdd_l4_l27_l31", },
	{ "l5", 0x4400, "vdd_l5_l7", },
	{ "l6", 0x4500, "vdd_l6_l12_l32", },
	{ "l7", 0x4600, "vdd_l5_l7", },
	{ "l8", 0x4700, "vdd_l8_l16_l30", },
	{ "l9", 0x4800, "vdd_l9_l10_l18_l22", },
	{ "l10", 0x4900, "vdd_l9_l10_l18_l22", },
	{ "l11", 0x4a00, "vdd_l3_l11", },
	{ "l12", 0x4b00, "vdd_l6_l12_l32", },
	{ "l13", 0x4c00, "vdd_l13_l19_l23_l24", },
	{ "l14", 0x4d00, "vdd_l14_l15", },
	{ "l15", 0x4e00, "vdd_l14_l15", },
	{ "l16", 0x4f00, "vdd_l8_l16_l30", },
	{ "l17", 0x5000, "vdd_l17_l29", },
	{ "l18", 0x5100, "vdd_l9_l10_l18_l22", },
	{ "l19", 0x5200, "vdd_l13_l19_l23_l24", },
	{ "l20", 0x5300, "vdd_l20_l21", },
	{ "l21", 0x5400, "vdd_l20_l21", },
	{ "l22", 0x5500, "vdd_l9_l10_l18_l22", },
	{ "l23", 0x5600, "vdd_l13_l19_l23_l24", },
	{ "l24", 0x5700, "vdd_l13_l19_l23_l24", },
	{ "l25", 0x5800, "vdd_l25", },
	{ "l26", 0x5900, "vdd_l2_l26_l28", },
	{ "l27", 0x5a00, "vdd_l4_l27_l31", },
	{ "l28", 0x5b00, "vdd_l2_l26_l28", },
	{ "l29", 0x5c00, "vdd_l17_l29", },
	{ "l30", 0x5d00, "vdd_l8_l16_l30", },
	{ "l31", 0x5e00, "vdd_l4_l27_l31", },
	{ "l32", 0x5f00, "vdd_l6_l12_l32", },
	{ "lvs1", 0x8000, "vdd_lvs_1_2", },
	{ "lvs2", 0x8100, "vdd_lvs_1_2", },
	{ }
};

static const struct of_device_id qcom_spmi_regulator_match[] = {
	{ .compatible = "qcom,pm8841-regulators", .data = &pm8841_regulators },
	{ .compatible = "qcom,pm8916-regulators", .data = &pm8916_regulators },
	{ .compatible = "qcom,pm8941-regulators", .data = &pm8941_regulators },
	{ .compatible = "qcom,pm8994-regulators", .data = &pm8994_regulators },
	{ }
};
MODULE_DEVICE_TABLE(of, qcom_spmi_regulator_match);

static int qcom_spmi_regulator_probe(struct platform_device *pdev)
{
	const struct spmi_regulator_data *reg;
	const struct of_device_id *match;
	struct regulator_config config = { };
	struct regulator_dev *rdev;
	struct spmi_regulator *vreg;
	struct regmap *regmap;
	const char *name;
	struct device *dev = &pdev->dev;
	int ret;
	struct list_head *vreg_list;

	vreg_list = devm_kzalloc(dev, sizeof(*vreg_list), GFP_KERNEL);
	if (!vreg_list)
		return -ENOMEM;
	INIT_LIST_HEAD(vreg_list);
	platform_set_drvdata(pdev, vreg_list);

	regmap = dev_get_regmap(dev->parent, NULL);
	if (!regmap)
		return -ENODEV;

	match = of_match_device(qcom_spmi_regulator_match, &pdev->dev);
	if (!match)
		return -ENODEV;

	for (reg = match->data; reg->name; reg++) {
		vreg = devm_kzalloc(dev, sizeof(*vreg), GFP_KERNEL);
		if (!vreg)
			return -ENOMEM;

		vreg->dev = dev;
		vreg->base = reg->base;
		vreg->regmap = regmap;

		if (reg->ocp) {
			vreg->ocp_irq = platform_get_irq_byname(pdev, reg->ocp);
			if (vreg->ocp_irq < 0) {
				ret = vreg->ocp_irq;
				goto err;
			}
		}

		vreg->desc.id = -1;
		vreg->desc.owner = THIS_MODULE;
		vreg->desc.type = REGULATOR_VOLTAGE;
		vreg->desc.name = name = reg->name;
		vreg->desc.supply_name = reg->supply;
		vreg->desc.of_match = reg->name;
		vreg->desc.of_parse_cb = spmi_regulator_of_parse;
		vreg->desc.of_map_mode = spmi_regulator_of_map_mode;

		ret = spmi_regulator_match(vreg, reg->force_type);
		if (ret)
			continue;

		config.dev = dev;
		config.driver_data = vreg;
		rdev = devm_regulator_register(dev, &vreg->desc, &config);
		if (IS_ERR(rdev)) {
			dev_err(dev, "failed to register %s\n", name);
			ret = PTR_ERR(rdev);
			goto err;
		}

		INIT_LIST_HEAD(&vreg->node);
		list_add(&vreg->node, vreg_list);
	}

	return 0;

err:
	list_for_each_entry(vreg, vreg_list, node)
		if (vreg->ocp_irq)
			cancel_delayed_work_sync(&vreg->ocp_work);
	return ret;
}

static int qcom_spmi_regulator_remove(struct platform_device *pdev)
{
	struct spmi_regulator *vreg;
	struct list_head *vreg_list = platform_get_drvdata(pdev);

	list_for_each_entry(vreg, vreg_list, node)
		if (vreg->ocp_irq)
			cancel_delayed_work_sync(&vreg->ocp_work);

	return 0;
}

static struct platform_driver qcom_spmi_regulator_driver = {
	.driver		= {
		.name	= "qcom-spmi-regulator",
		.of_match_table = qcom_spmi_regulator_match,
	},
	.probe		= qcom_spmi_regulator_probe,
	.remove		= qcom_spmi_regulator_remove,
};
module_platform_driver(qcom_spmi_regulator_driver);

MODULE_DESCRIPTION("Qualcomm SPMI PMIC regulator driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:qcom-spmi-regulator");
