// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2025 Analog Devices, Inc.
 * ADI regulator driver for MAX77675.
 */

#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/cleanup.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/of_regulator.h>
#include <linux/bitfield.h>
#include <linux/bitops.h>

/* Register Addresses */
#define MAX77675_REG_CNFG_GLBL_A     0x00
#define MAX77675_REG_CNFG_GLBL_B     0x01
#define MAX77675_REG_INT_GLBL        0x02
#define MAX77675_REG_INTM_GLBL       0x03
#define MAX77675_REG_STAT_GLBL       0x04
#define MAX77675_REG_ERCF_GLBL       0x05
#define MAX77675_REG_CID             0x06
#define MAX77675_REG_CNFG_SBB_TOP_A  0x07
#define MAX77675_REG_CNFG_SBB0_A     0x08
#define MAX77675_REG_CNFG_SBB0_B     0x09
#define MAX77675_REG_CNFG_SBB1_A     0x0A
#define MAX77675_REG_CNFG_SBB1_B     0x0B
#define MAX77675_REG_CNFG_SBB2_A     0x0C
#define MAX77675_REG_CNFG_SBB2_B     0x0D
#define MAX77675_REG_CNFG_SBB3_A     0x0E
#define MAX77675_REG_CNFG_SBB3_B     0x0F
#define MAX77675_REG_CNFG_SBB_TOP_B  0x10

/* CNFG_GLBL_A (0x00) bit masks and shifts */
#define MAX77675_MRT_MASK           GENMASK(7, 6)    /* Manual Reset Time (bits 7:6) */
#define MAX77675_MRT_SHIFT          6
#define MAX77675_PU_DIS_BIT         BIT(5)           /* Pullup Disable (bit 5) */
#define MAX77675_PU_DIS_SHIFT       5
#define MAX77675_BIAS_LPM_BIT       BIT(4)           /* Bias Low Power Mode (bit 4) */
#define MAX77675_BIAS_LPM_SHIFT     4
#define MAX77675_SIMO_CH_DIS_BIT    BIT(3)           /* SIMO Internal Channel Disable (bit 3) */
#define MAX77675_SIMO_CH_DIS_SHIFT  3
#define MAX77675_EN_MODE_MASK       GENMASK(2, 1)    /* nEN Mode (bits 2:1) */
#define MAX77675_EN_MODE_SHIFT      1
#define MAX77675_DBEN_EN_BIT        BIT(0)           /* Debounce Enable (bit 0) */
#define MAX77675_DBEN_EN_SHIFT      0

/* CNFG_GLBL_B (0x01) */
#define MAX77675_SFT_CTRL_MASK      GENMASK(2, 0)    /* Soft Start Control */
#define MAX77675_SFT_CTRL_SHIFT     0

/* INT_GLBL (0x02) bit bits and shifts */
#define MAX77675_INT_SBB3_F_BIT     BIT(7)
#define MAX77675_INT_SBB3_F_SHIFT   7
#define MAX77675_INT_SBB2_F_BIT     BIT(6)
#define MAX77675_INT_SBB2_F_SHIFT   6
#define MAX77675_INT_SBB1_F_BIT     BIT(5)
#define MAX77675_INT_SBB1_F_SHIFT   5
#define MAX77675_INT_SBB0_F_BIT     BIT(4)
#define MAX77675_INT_SBB0_F_SHIFT   4
#define MAX77675_INT_TJAL2_R_BIT    BIT(3)
#define MAX77675_INT_TJAL2_R_SHIFT  3
#define MAX77675_INT_TJAL1_R_BIT    BIT(2)
#define MAX77675_INT_TJAL1_R_SHIFT  2
#define MAX77675_INT_EN_R_BIT       BIT(1)
#define MAX77675_INT_EN_R_SHIFT     1
#define MAX77675_INT_EN_F_BIT       BIT(0)
#define MAX77675_INT_EN_F_SHIFT     0

/* INTM_GLBL (0x03) bits and shifts */
#define MAX77675_INTM_SBB3_F_BIT    BIT(7)
#define MAX77675_INTM_SBB3_F_SHIFT  7
#define MAX77675_INTM_SBB2_F_BIT    BIT(6)
#define MAX77675_INTM_SBB2_F_SHIFT  6
#define MAX77675_INTM_SBB1_F_BIT    BIT(5)
#define MAX77675_INTM_SBB1_F_SHIFT  5
#define MAX77675_INTM_SBB0_F_BIT    BIT(4)
#define MAX77675_INTM_SBB0_F_SHIFT  4
#define MAX77675_INTM_TJAL2_R_BIT   BIT(3)
#define MAX77675_INTM_TJAL2_R_SHIFT 3
#define MAX77675_INTM_TJAL1_R_BIT   BIT(2)
#define MAX77675_INTM_TJAL1_R_SHIFT 2
#define MAX77675_INTM_EN_R_BIT      BIT(1)
#define MAX77675_INTM_EN_R_SHIFT    1
#define MAX77675_INTM_EN_F_BIT      BIT(0)
#define MAX77675_INTM_EN_F_SHIFT    0

/* STAT_GLBL (0x04) bits and shifts */
#define MAX77675_STAT_SBB3_S_BIT    BIT(7)
#define MAX77675_STAT_SBB3_S_SHIFT  7
#define MAX77675_STAT_SBB2_S_BIT    BIT(6)
#define MAX77675_STAT_SBB2_S_SHIFT  6
#define MAX77675_STAT_SBB1_S_BIT    BIT(5)
#define MAX77675_STAT_SBB1_S_SHIFT  5
#define MAX77675_STAT_SBB0_S_BIT    BIT(4)
#define MAX77675_STAT_SBB0_S_SHIFT  4
#define MAX77675_STAT_TJAL2_S_BIT   BIT(2)
#define MAX77675_STAT_TJAL2_S_SHIFT 2
#define MAX77675_STAT_TJAL1_S_BIT   BIT(1)
#define MAX77675_STAT_TJAL1_S_SHIFT 1
#define MAX77675_STAT_STAT_EN_BIT   BIT(0)
#define MAX77675_STAT_STAT_EN_SHIFT 0

#define MAX77675_STAT_STAT_EN_BIT   BIT(0)
#define MAX77675_STAT_STAT_EN_SHIFT 0

/* ERCFLAG (0x05) bits and shifts */
#define MAX77675_SFT_CRST_F_BIT     BIT(5)  /* Software Cold Reset Flag */
#define MAX77675_SFT_CRST_F_SHIFT   5
#define MAX77675_SFT_OFF_F_BIT      BIT(4)  /* Software Off Flag */
#define MAX77675_SFT_OFF_F_SHIFT    4
#define MAX77675_MRST_BIT           BIT(3)  /* Manual Reset Timer Flag */
#define MAX77675_MRST_SHIFT         3
#define MAX77675_UVLO_BIT           BIT(2)  /* Undervoltage Lockout Flag */
#define MAX77675_UVLO_SHIFT         2
#define MAX77675_OVLO_BIT           BIT(1)  /* Overvoltage Lockout Flag */
#define MAX77675_OVLO_SHIFT         1
#define MAX77675_TOVLD_BIT          BIT(0)  /* Thermal Overload Flag */
#define MAX77675_TOVLD_SHIFT        0

/* CID (0x06) bits and shifts */
#define MAX77675_CID_MASK           GENMASK(4, 0)  /* Chip Identification Code mask */
#define MAX77675_CID_SHIFT          0              /* Starts at bit 0 */

/* CNFG_SBB_TOP_A (0x07) bits and shifts */
#define MAX77675_STEP_SZ_SBB3_BIT   BIT(5)
#define MAX77675_STEP_SZ_SBB3_SHIFT 5
#define MAX77675_STEP_SZ_SBB2_BIT   BIT(4)
#define MAX77675_STEP_SZ_SBB2_SHIFT 4
#define MAX77675_STEP_SZ_SBB1_BIT   BIT(3)
#define MAX77675_STEP_SZ_SBB1_SHIFT 3
#define MAX77675_STEP_SZ_SBB0_BIT   BIT(2)
#define MAX77675_STEP_SZ_SBB0_SHIFT 2
#define MAX77675_DRV_SBB_MASK       GENMASK(1, 0)
#define MAX77675_DRV_SBB_SHIFT      0

/* CNFG_SBB0_A (0x08) bits and shifts */
#define MAX77675_TV_SBB0_MASK       GENMASK(7, 0)
#define MAX77675_TV_SBB0_SHIFT      0

/* CNFG_SBB0_B (0x09) bits and shifts */
#define MAX77675_ADE_SBB0_BIT       BIT(3)
#define MAX77675_ADE_SBB0_SHIFT     3
#define MAX77675_EN_SBB0_MASK       GENMASK(2, 0)
#define MAX77675_EN_SBB0_SHIFT      0

/* CNFG_SBB1_A (0x0A) bits and shifts */
#define MAX77675_TV_SBB1_MASK       GENMASK(7, 0)
#define MAX77675_TV_SBB1_SHIFT      0

/* CNFG_SBB1_B (0x0B) bits and shifts */
#define MAX77675_ADE_SBB1_BIT       BIT(3)
#define MAX77675_ADE_SBB1_SHIFT     3
#define MAX77675_EN_SBB1_MASK       GENMASK(2, 0)
#define MAX77675_EN_SBB1_SHIFT      0

/* CNFG_SBB2_A (0x0C) bits and shifts */
#define MAX77675_TV_SBB2_MASK       GENMASK(7, 0)
#define MAX77675_TV_SBB2_SHIFT      0

/* CNFG_SBB2_B (0x0D) bits and shifts */
#define MAX77675_ADE_SBB2_BIT       BIT(3)
#define MAX77675_ADE_SBB2_SHIFT     3
#define MAX77675_EN_SBB2_MASK       GENMASK(2, 0)
#define MAX77675_EN_SBB2_SHIFT      0

/* CNFG_SBB3_A (0x0E) bits and shifts */
#define MAX77675_TV_SBB3_MASK       GENMASK(7, 0)
#define MAX77675_TV_SBB3_SHIFT      0

/* CNFG_SBB3_B (0x0F) bits and shifts */
#define MAX77675_ADE_SBB3_BIT       BIT(3)
#define MAX77675_ADE_SBB3_SHIFT     3
#define MAX77675_EN_SBB3_MASK       GENMASK(2, 0)
#define MAX77675_EN_SBB3_SHIFT      0

#define MAX77675_EN_SBB_MASK        GENMASK(2, 0)

/* CNFG_SBB_TOP_B (0x10) bits and shifts */
#define MAX77675_DVS_SLEW_BIT       BIT(5)
#define MAX77675_DVS_SLEW_SHIFT     5
#define MAX77675_LAT_MODE_BIT       BIT(4)
#define MAX77675_LAT_MODE_SHIFT     4
#define MAX77675_SR_SBB3_BIT        BIT(3)
#define MAX77675_SR_SBB3_SHIFT      3
#define MAX77675_SR_SBB2_BIT        BIT(2)
#define MAX77675_SR_SBB2_SHIFT      2
#define MAX77675_SR_SBB1_BIT        BIT(1)
#define MAX77675_SR_SBB1_SHIFT      1
#define MAX77675_SR_SBB0_BIT        BIT(0)
#define MAX77675_SR_SBB0_SHIFT      0

#define MAX77675_MAX_REGISTER       0x10

/* Common minimum voltage (in microvolts) */
#define MAX77675_MIN_UV             500000     // 500 mV

/* Voltage step configuration for 25mV mode */
#define MAX77675_STEP_25MV          25000      // Step size: 25 mV
#define MAX77675_MAX_UV_25MV        5500000    // Max voltage: 5.5 V
#define MAX77675_NUM_LEVELS_25MV    201        // levels = (5500mV - 500mV) / 25mV + 1

/* Voltage step configuration for 12.5mV mode */
#define MAX77675_STEP_12_5MV        12500      // Step size: 12.5 mV
#define MAX77675_MAX_UV_12_5MV      3687500    // Max voltage: 3.6875 V
#define MAX77675_NUM_LEVELS_12_5MV  255        // levels = (3687.5mV - 500mV) / 12.5mV + 1

#define MAX77675_ENABLE_OFF         0x04
#define MAX77675_ENABLE_ON          0x06

#define MAX77675_REGULATOR_AD_OFF   0x00
#define MAX77675_REGULATOR_AD_ON    BIT(3)

/* FPS source */
#define MAX77675_FPS_SLOT_0         0x0
#define MAX77675_FPS_SLOT_1         0x1
#define MAX77675_FPS_SLOT_2         0x2
#define MAX77675_FPS_SLOT_3         0x3
#define MAX77675_FPS_DEF            0x4

/* nEN Manual Reset Time Configuration (MRT) */
#define MAX77675_MRT_4S             0x0
#define MAX77675_MRT_8S             0x1
#define MAX77675_MRT_12S            0x2
#define MAX77675_MRT_16S            0x3

/* nEN Mode Configuration */
#define MAX77675_EN_PUSH_BUTTON     0x0
#define MAX77675_EN_SLIDE_SWITCH    0x1
#define MAX77675_EN_LOGIC           0x2

/* Debounce Timer Enable (DBEN_nEN) */
#define MAX77675_DBEN_100US         0x0
#define MAX77675_DBEN_30000US       0x1

/* Rising slew rate control for SBB0 when ramping up */
#define MAX77675_SR_2MV_PER_US      0x0  // 2 mV/us
#define MAX77675_SR_USE_DVS         0x1  // Use DVS slew rate setting (adi,dvs-slew-rate)

/* Latency Mode */
#define MAX77675_HIGH_LATENCY_MODE  0x0   // High latency, low quiescent current (~100us)
#define MAX77675_LOW_LATENCY_MODE   0x1   // Low latency, high quiescent current (~10us)

/* Dynamic Voltage Scaling (DVS) Slew Rate */
#define MAX77675_DVS_SLEW_5MV_PER_US   0x0  // 5 mV/us
#define MAX77675_DVS_SLEW_10MV_PER_US  0x1  // 10 mV/us

/* SIMO Buck-Boost Drive Strength (All Channels) */
#define MAX77675_DRV_SBB_STRENGTH_MAX  0x0  // Maximum drive strength (~0.6 ns transition time)
#define MAX77675_DRV_SBB_STRENGTH_HIGH 0x1  // High drive strength (~1.2 ns transition time)
#define MAX77675_DRV_SBB_STRENGTH_LOW  0x2  // Low drive strength (~1.8 ns transition time)
#define MAX77675_DRV_SBB_STRENGTH_MIN  0x3  // Minimum drive strength (~8 ns transition time)

/* Regulator ID enumeration */
enum max77675_regulator_id {
	MAX77675_ID_SBB0 = 0,
	MAX77675_ID_SBB1,
	MAX77675_ID_SBB2,
	MAX77675_ID_SBB3,
	MAX77675_ID_NUM_MAX,
};

struct max77675_regulator_sbb_setting {
	u8   fps_slot;
	bool fixed_slew_rate;
};

struct max77675_config {
	u8   en_mode;
	u8   voltage_change_latency;
	u8   drv_sbb_strength;
	u8   dvs_slew_rate;
	u8   debounce_time;
	u8   manual_reset_time;
	bool en_pullup_disable;
	bool bias_low_power_request;
	bool simo_ldo_always_on;
};

struct max77675_regulator {
	struct device *dev;
	struct regmap *regmap;
	struct max77675_config config;
	struct max77675_regulator_sbb_setting sbb_setting[MAX77675_ID_NUM_MAX];
};

static int max77675_regulator_get_fps_src(struct max77675_regulator *maxreg, int id)
{
	unsigned int reg_addr;
	unsigned int val;
	int ret;

	switch (id) {
	case MAX77675_ID_SBB0:
		reg_addr = MAX77675_REG_CNFG_SBB0_B;
		break;
	case MAX77675_ID_SBB1:
		reg_addr = MAX77675_REG_CNFG_SBB1_B;
		break;
	case MAX77675_ID_SBB2:
		reg_addr = MAX77675_REG_CNFG_SBB2_B;
		break;
	case MAX77675_ID_SBB3:
		reg_addr = MAX77675_REG_CNFG_SBB3_B;
		break;
	default:
		dev_err(maxreg->dev, "Invalid regulator id: %d\n", id);
		return -EINVAL;
	}

	ret = regmap_read(maxreg->regmap, reg_addr, &val);
	if (ret < 0) {
		dev_err(maxreg->dev, "Failed to read FPS source (reg 0x%02x): %d\n",
			reg_addr, ret);
		return ret;
	}

	return FIELD_GET(MAX77675_EN_SBB_MASK, val);
}

static int max77675_regulator_set_fps_src(struct max77675_regulator *maxreg, int id, u8 fps_src)
{
	unsigned int reg_addr;

	switch (id) {
	case MAX77675_ID_SBB0:
		reg_addr = MAX77675_REG_CNFG_SBB0_B;
		break;
	case MAX77675_ID_SBB1:
		reg_addr = MAX77675_REG_CNFG_SBB1_B;
		break;
	case MAX77675_ID_SBB2:
		reg_addr = MAX77675_REG_CNFG_SBB2_B;
		break;
	case MAX77675_ID_SBB3:
		reg_addr = MAX77675_REG_CNFG_SBB3_B;
		break;
	default:
		dev_err(maxreg->dev, "Invalid regulator id: %d\n", id);
		return -EINVAL;
	}

	return regmap_update_bits(maxreg->regmap, reg_addr, MAX77675_EN_SBB_MASK, fps_src);
}

static int max77675_set_sbb_slew_rate_fixed(struct max77675_regulator *maxreg, int id, bool fixed)
{
	u8 mask, value;
	u8 slew_src_ctrl_bit = fixed ? 0 : 1;

	switch (id) {
	case MAX77675_ID_SBB0:
		mask = MAX77675_SR_SBB0_BIT;
		value = FIELD_PREP(MAX77675_SR_SBB0_BIT, slew_src_ctrl_bit);
		break;

	case MAX77675_ID_SBB1:
		mask = MAX77675_SR_SBB1_BIT;
		value = FIELD_PREP(MAX77675_SR_SBB1_BIT, slew_src_ctrl_bit);
		break;

	case MAX77675_ID_SBB2:
		mask = MAX77675_SR_SBB2_BIT;
		value = FIELD_PREP(MAX77675_SR_SBB2_BIT, slew_src_ctrl_bit);
		break;

	case MAX77675_ID_SBB3:
		mask = MAX77675_SR_SBB3_BIT;
		value = FIELD_PREP(MAX77675_SR_SBB3_BIT, slew_src_ctrl_bit);
		break;

	default:
		return -EINVAL;
	}

	return regmap_update_bits(maxreg->regmap, MAX77675_REG_CNFG_SBB_TOP_B, mask, value);
}

static int max77675_init_regulator(struct max77675_regulator *maxreg, int id)
{
	struct max77675_regulator_sbb_setting *sbb_setting = &maxreg->sbb_setting[id];
	int ret;

	if (sbb_setting->fps_slot == MAX77675_FPS_DEF) {
		ret = max77675_regulator_get_fps_src(maxreg, id);
		if (ret < 0)
			return ret;

		sbb_setting->fps_slot = ret;
	} else {
		ret = max77675_regulator_set_fps_src(maxreg, id, sbb_setting->fps_slot);
		if (ret < 0)
			return ret;
	}

	ret = max77675_set_sbb_slew_rate_fixed(maxreg, id, sbb_setting->fixed_slew_rate);
	if (ret < 0)
		return ret;

	return 0;
}

static int max77675_of_parse_cb(struct device_node *np,
				const struct regulator_desc *desc,
				struct regulator_config *config)
{
	struct max77675_regulator *maxreg = config->driver_data;
	struct max77675_regulator_sbb_setting *sbb_setting = &maxreg->sbb_setting[desc->id];
	static const char * const fps_slots[] = { "slot0", "slot1", "slot2", "slot3", "default" };
	const char *fps_str;
	int slot;

	/* Parse FPS slot from DT */
	if (of_property_read_string(np, "adi,fps-slot", &fps_str)) {
		/* Property not set, use default */
		sbb_setting->fps_slot = MAX77675_FPS_DEF;
	} else {
		/* Match string to index */
		slot = match_string(fps_slots, ARRAY_SIZE(fps_slots), fps_str);
		if (slot < 0) {
			dev_dbg(maxreg->dev, "Invalid fps-slot '%s', using default\n", fps_str);
			sbb_setting->fps_slot = MAX77675_FPS_DEF;
		} else {
			sbb_setting->fps_slot = slot;
		}
	}

	/* Parse slew rate control source */
	sbb_setting->fixed_slew_rate = of_property_read_bool(np, "adi,fixed-slew-rate");

	/* Apply parsed configuration */
	return max77675_init_regulator(maxreg, desc->id);
}

static int max77675_get_error_flags(struct regulator_dev *rdev, unsigned int *flags)
{
	struct max77675_regulator *maxreg = rdev_get_drvdata(rdev);
	unsigned int int_flags;
	int id = rdev_get_id(rdev);
	int ret;

	ret = regmap_read(maxreg->regmap, MAX77675_REG_INT_GLBL, &int_flags);
	if (ret) {
		dev_err(maxreg->dev, "Failed to read INT_GLBL: %d\n", ret);
		return ret;
	}

	*flags = 0;

	switch (id) {
	case MAX77675_ID_SBB0:
		if (int_flags & MAX77675_INT_SBB0_F_BIT)
			*flags |= REGULATOR_ERROR_FAIL;
		break;
	case MAX77675_ID_SBB1:
		if (int_flags & MAX77675_INT_SBB1_F_BIT)
			*flags |= REGULATOR_ERROR_FAIL;
		break;
	case MAX77675_ID_SBB2:
		if (int_flags & MAX77675_INT_SBB2_F_BIT)
			*flags |= REGULATOR_ERROR_FAIL;
		break;
	case MAX77675_ID_SBB3:
		if (int_flags & MAX77675_INT_SBB3_F_BIT)
			*flags |= REGULATOR_ERROR_FAIL;
		break;
	default:
		dev_warn(maxreg->dev, "Unsupported regulator ID: %d\n", id);
		break;
	}

	if (int_flags & MAX77675_INT_TJAL2_R_BIT) {
		/* TJAL2 interrupt: Over-temperature condition (above 120 degree) */
		*flags |= REGULATOR_ERROR_OVER_TEMP;
	}

	return 0;
}

static const struct regulator_ops max77675_regulator_ops = {
	.list_voltage         = regulator_list_voltage_linear,
	.enable               = regulator_enable_regmap,
	.disable              = regulator_disable_regmap,
	.is_enabled           = regulator_is_enabled_regmap,
	.map_voltage          = regulator_map_voltage_linear,
	.set_voltage_sel      = regulator_set_voltage_sel_regmap,
	.get_voltage_sel      = regulator_get_voltage_sel_regmap,
	.set_active_discharge = regulator_set_active_discharge_regmap,
	.get_error_flags      = max77675_get_error_flags,
};

static struct regulator_desc max77675_regulators[MAX77675_ID_NUM_MAX] = {
	{
		.name                  = "sbb0",
		.of_match              = of_match_ptr("sbb0"),
		.regulators_node       = of_match_ptr("regulators"),
		.of_parse_cb           = max77675_of_parse_cb,
		.id                    = MAX77675_ID_SBB0,
		.ops                   = &max77675_regulator_ops,
		.type                  = REGULATOR_VOLTAGE,
		.owner                 = THIS_MODULE,
		.n_voltages            = MAX77675_NUM_LEVELS_25MV,
		.min_uV                = MAX77675_MIN_UV,
		.uV_step               = MAX77675_STEP_25MV,
		.vsel_reg              = MAX77675_REG_CNFG_SBB0_A,
		.vsel_mask             = MAX77675_TV_SBB0_MASK,
		.enable_reg            = MAX77675_REG_CNFG_SBB0_B,
		.enable_mask           = MAX77675_EN_SBB0_MASK,
		.enable_val            = MAX77675_ENABLE_ON,
		.disable_val           = MAX77675_ENABLE_OFF,
		.active_discharge_off  = MAX77675_REGULATOR_AD_OFF,
		.active_discharge_on   = MAX77675_REGULATOR_AD_ON,
		.active_discharge_mask = MAX77675_ADE_SBB0_BIT,
		.active_discharge_reg  = MAX77675_REG_CNFG_SBB0_B,
	},
	{
		.name                  = "sbb1",
		.of_match              = of_match_ptr("sbb1"),
		.regulators_node       = of_match_ptr("regulators"),
		.of_parse_cb           = max77675_of_parse_cb,
		.id                    = MAX77675_ID_SBB1,
		.ops                   = &max77675_regulator_ops,
		.type                  = REGULATOR_VOLTAGE,
		.owner                 = THIS_MODULE,
		.n_voltages            = MAX77675_NUM_LEVELS_25MV,
		.min_uV                = MAX77675_MIN_UV,
		.uV_step               = MAX77675_STEP_25MV,
		.vsel_reg              = MAX77675_REG_CNFG_SBB1_A,
		.vsel_mask             = MAX77675_TV_SBB1_MASK,
		.enable_reg            = MAX77675_REG_CNFG_SBB1_B,
		.enable_mask           = MAX77675_EN_SBB1_MASK,
		.enable_val            = MAX77675_ENABLE_ON,
		.disable_val           = MAX77675_ENABLE_OFF,
		.active_discharge_off  = MAX77675_REGULATOR_AD_OFF,
		.active_discharge_on   = MAX77675_REGULATOR_AD_ON,
		.active_discharge_mask = MAX77675_ADE_SBB1_BIT,
		.active_discharge_reg  = MAX77675_REG_CNFG_SBB1_B,
	},
	{
		.name                  = "sbb2",
		.of_match              = of_match_ptr("sbb2"),
		.regulators_node       = of_match_ptr("regulators"),
		.of_parse_cb           = max77675_of_parse_cb,
		.id                    = MAX77675_ID_SBB2,
		.ops                   = &max77675_regulator_ops,
		.type                  = REGULATOR_VOLTAGE,
		.owner                 = THIS_MODULE,
		.n_voltages            = MAX77675_NUM_LEVELS_25MV,
		.min_uV                = MAX77675_MIN_UV,
		.uV_step               = MAX77675_STEP_25MV,
		.vsel_reg              = MAX77675_REG_CNFG_SBB2_A,
		.vsel_mask             = MAX77675_TV_SBB2_MASK,
		.enable_reg            = MAX77675_REG_CNFG_SBB2_B,
		.enable_mask           = MAX77675_EN_SBB2_MASK,
		.enable_val            = MAX77675_ENABLE_ON,
		.disable_val           = MAX77675_ENABLE_OFF,
		.active_discharge_off  = MAX77675_REGULATOR_AD_OFF,
		.active_discharge_on   = MAX77675_REGULATOR_AD_ON,
		.active_discharge_mask = MAX77675_ADE_SBB2_BIT,
		.active_discharge_reg  = MAX77675_REG_CNFG_SBB2_B,
	},
	{
		.name                  = "sbb3",
		.of_match              = of_match_ptr("sbb3"),
		.regulators_node       = of_match_ptr("regulators"),
		.of_parse_cb           = max77675_of_parse_cb,
		.id                    = MAX77675_ID_SBB3,
		.ops                   = &max77675_regulator_ops,
		.type                  = REGULATOR_VOLTAGE,
		.owner                 = THIS_MODULE,
		.n_voltages            = MAX77675_NUM_LEVELS_25MV,
		.min_uV                = MAX77675_MIN_UV,
		.uV_step               = MAX77675_STEP_25MV,
		.vsel_reg              = MAX77675_REG_CNFG_SBB3_A,
		.vsel_mask             = MAX77675_TV_SBB3_MASK,
		.enable_reg            = MAX77675_REG_CNFG_SBB3_B,
		.enable_mask           = MAX77675_EN_SBB3_MASK,
		.enable_val            = MAX77675_ENABLE_ON,
		.disable_val           = MAX77675_ENABLE_OFF,
		.active_discharge_off  = MAX77675_REGULATOR_AD_OFF,
		.active_discharge_on   = MAX77675_REGULATOR_AD_ON,
		.active_discharge_mask = MAX77675_ADE_SBB3_BIT,
		.active_discharge_reg  = MAX77675_REG_CNFG_SBB3_B,
	},
};

static bool max77675_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MAX77675_REG_CNFG_GLBL_B:
		/* This register can be updated by an internal state machine */
	case MAX77675_REG_INT_GLBL:
	case MAX77675_REG_STAT_GLBL:
	case MAX77675_REG_ERCF_GLBL:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config max77675_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = MAX77675_MAX_REGISTER,
	.cache_type = REGCACHE_MAPLE,
	.volatile_reg = max77675_volatile_reg,
};

static int max77675_apply_config(struct max77675_regulator *maxreg)
{
	const struct max77675_config *cfg = &maxreg->config;
	int ret;

	/* Set EN pin mode */
	ret = regmap_update_bits(maxreg->regmap, MAX77675_REG_CNFG_GLBL_A,
				 MAX77675_EN_MODE_MASK,
				 FIELD_PREP(MAX77675_EN_MODE_MASK, cfg->en_mode));
	if (ret) {
		dev_err(maxreg->dev, "Failed to set EN mode: %d\n", ret);
		return ret;
	}

	/* Set the latency between output voltage change and SBBx voltage ramp start */
	ret = regmap_update_bits(maxreg->regmap, MAX77675_REG_CNFG_SBB_TOP_B,
				 MAX77675_LAT_MODE_BIT,
				 FIELD_PREP(MAX77675_LAT_MODE_BIT, cfg->voltage_change_latency));
	if (ret) {
		dev_err(maxreg->dev, "Failed to set latency mode: %d\n", ret);
		return ret;
	}

	/* Set drive strength */
	ret = regmap_update_bits(maxreg->regmap, MAX77675_REG_CNFG_SBB_TOP_A,
				 MAX77675_DRV_SBB_MASK,
				 FIELD_PREP(MAX77675_DRV_SBB_MASK, cfg->drv_sbb_strength));
	if (ret) {
		dev_err(maxreg->dev, "Failed to set drive strength: %d\n", ret);
		return ret;
	}

	/* Set DVS slew rate */
	ret = regmap_update_bits(maxreg->regmap, MAX77675_REG_CNFG_SBB_TOP_B,
				 MAX77675_DVS_SLEW_BIT,
				 FIELD_PREP(MAX77675_DVS_SLEW_BIT, cfg->dvs_slew_rate));
	if (ret) {
		dev_err(maxreg->dev, "Failed to set DVS slew rate: %d\n", ret);
		return ret;
	}

	/* Set debounce time for EN pin */
	ret = regmap_update_bits(maxreg->regmap, MAX77675_REG_CNFG_GLBL_A,
				 MAX77675_DBEN_EN_BIT,
				 FIELD_PREP(MAX77675_DBEN_EN_BIT, cfg->debounce_time));
	if (ret) {
		dev_err(maxreg->dev, "Failed to set EN debounce time: %d\n", ret);
		return ret;
	}

	/* Set manual reset time (MRT) for EN pin */
	ret = regmap_update_bits(maxreg->regmap, MAX77675_REG_CNFG_GLBL_A,
				 MAX77675_MRT_MASK,
				 FIELD_PREP(MAX77675_MRT_MASK, cfg->manual_reset_time));
	if (ret) {
		dev_err(maxreg->dev, "Failed to set manual reset time: %d\n", ret);
		return ret;
	}

	/* Enable or disable internal pull-up resistor on EN pin */
	ret = regmap_update_bits(maxreg->regmap, MAX77675_REG_CNFG_GLBL_A,
				 MAX77675_PU_DIS_BIT,
				 FIELD_PREP(MAX77675_PU_DIS_BIT, cfg->en_pullup_disable));
	if (ret) {
		dev_err(maxreg->dev, "Failed to set EN pull-up disable: %d\n", ret);
		return ret;
	}

	/* Request main bias to enter low-power mode */
	ret = regmap_update_bits(maxreg->regmap, MAX77675_REG_CNFG_GLBL_A,
				 MAX77675_BIAS_LPM_BIT,
				 FIELD_PREP(MAX77675_BIAS_LPM_BIT, cfg->bias_low_power_request));
	if (ret) {
		dev_err(maxreg->dev, "Failed to set bias low-power request: %d\n", ret);
		return ret;
	}

	/* Force SIMO internal LDO to always supply 1.8V */
	ret = regmap_update_bits(maxreg->regmap, MAX77675_REG_CNFG_GLBL_A,
				 MAX77675_SIMO_CH_DIS_BIT,
				 FIELD_PREP(MAX77675_SIMO_CH_DIS_BIT, cfg->simo_ldo_always_on));
	if (ret) {
		dev_err(maxreg->dev, "Failed to set SIMO internal LDO always-on: %d\n", ret);
		return ret;
	}

	return 0;
}

static int max77675_parse_en_mode(struct device *dev,
				  struct device_node *np,
				  u8 *en_mode)
{
	static const char * const en_modes[] = {"push-button", "slide-switch", "logic"};
	const char *str;
	int index;

	*en_mode = MAX77675_EN_SLIDE_SWITCH;

	if (of_property_read_string(np, "adi,en-mode", &str))
		return 0;

	index = match_string(en_modes, ARRAY_SIZE(en_modes), str);
	if (index < 0) {
		dev_err(dev, "Invalid 'adi,en-mode' value '%s'\n", str);
		return -EINVAL;
	}

	*en_mode = index;

	return 0;
}

static int max77675_parse_voltage_change_latency(struct device *dev,
						 struct device_node *np,
						 u8 *latency_mode)
{
	u32 val;

	*latency_mode = MAX77675_HIGH_LATENCY_MODE;

	if (!of_property_read_u32(np, "adi,voltage-change-latency-us", &val)) {
		switch (val) {
		case 10:
			*latency_mode = MAX77675_LOW_LATENCY_MODE;
			break;
		case 100:
			*latency_mode = MAX77675_HIGH_LATENCY_MODE;
			break;
		default:
			dev_err(dev, "Invalid voltage-change-latency-us value: %u\n", val);
			return -EINVAL;
		}
	}

	return 0;
}

static int max77675_parse_manual_reset_time(struct device *dev,
					    struct device_node *np,
					    u8 *reset_time)
{
	u32 val;

	*reset_time = MAX77675_MRT_4S;

	if (!of_property_read_u32(np, "reset-time-sec", &val)) {
		switch (val) {
		case 4:
			*reset_time = MAX77675_MRT_4S;
			break;
		case 8:
			*reset_time = MAX77675_MRT_8S;
			break;
		case 12:
			*reset_time = MAX77675_MRT_12S;
			break;
		case 16:
			*reset_time = MAX77675_MRT_16S;
			break;
		default:
			dev_err(dev, "Invalid reset-time-sec value: %u\n", val);
			return -EINVAL;
		}
	}

	return 0;
}

static int max77675_parse_dvs_slew_rate(struct device *dev, struct device_node *np, u8 *slew_rate)
{
	u32 val;

	/* Set default: 5 mV/us */
	*slew_rate = MAX77675_DVS_SLEW_5MV_PER_US;

	if (!of_property_read_u32(np, "adi,dvs-slew-rate-mv-per-us", &val)) {
		switch (val) {
		case 5:
			*slew_rate = MAX77675_DVS_SLEW_5MV_PER_US;
			break;
		case 10:
			*slew_rate = MAX77675_DVS_SLEW_10MV_PER_US;
			break;
		default:
			dev_err(dev, "Invalid dvs-slew-rate-mv-per-us value: %u\n", val);
			return -EINVAL;
		}
	}

	return 0;
}

static int max77675_parse_drv_sbb_strength(struct device *dev, struct device_node *np, u8 *strength)
{
	static const char * const strength_names[] = {"max", "high", "low", "min"};
	const char *str;
	int index;

	/* Set default: maximum drive strength */
	*strength = MAX77675_DRV_SBB_STRENGTH_MAX;

	if (of_property_read_string(np, "adi,drv-sbb-strength", &str))
		return 0;

	index = match_string(strength_names, ARRAY_SIZE(strength_names), str);
	if (index < 0) {
		dev_err(dev, "Invalid 'adi,drv-sbb-strength' value: '%s'\n", str);
		return -EINVAL;
	}

	*strength = index;

	return 0;
}

static int max77675_parse_debounce_time_us(struct device *dev,
					   struct device_node *np,
					   u8 *debounce_time)
{
	u32 val;

	*debounce_time = MAX77675_DBEN_100US;

	if (!of_property_read_u32(np, "input-debounce", &val)) {
		switch (val) {
		case 100:
			*debounce_time = MAX77675_DBEN_100US;
			break;
		case 30000:
			*debounce_time = MAX77675_DBEN_30000US;
			break;
		default:
			dev_err(dev, "Invalid input-debounce value: %u\n", val);
			return -EINVAL;
		}
	}

	return 0;
}

static int max77675_parse_config(struct max77675_regulator *maxreg)
{
	struct device_node *np = maxreg->dev->of_node;
	struct max77675_config *cfg = &maxreg->config;
	int ret;

	/* EN pin mode */
	ret = max77675_parse_en_mode(maxreg->dev, np, &cfg->en_mode);
	if (ret < 0)
		return ret;

	/* voltage change latency */
	ret = max77675_parse_voltage_change_latency(maxreg->dev, np, &cfg->voltage_change_latency);
	if (ret < 0)
		return ret;

	/* drive strength */
	ret = max77675_parse_drv_sbb_strength(maxreg->dev, np, &cfg->drv_sbb_strength);
	if (ret < 0)
		return ret;

	/* dvs slew rate */
	ret = max77675_parse_dvs_slew_rate(maxreg->dev, np, &cfg->dvs_slew_rate);
	if (ret < 0)
		return ret;

	/* Debounce time for EN pin */
	ret = max77675_parse_debounce_time_us(maxreg->dev, np, &cfg->debounce_time);
	if (ret < 0)
		return ret;

	/* Manual reset time for EN pin */
	ret = max77675_parse_manual_reset_time(maxreg->dev, np, &cfg->manual_reset_time);
	if (ret < 0)
		return ret;

	/* Disable internal pull-up resistor on EN pin */
	cfg->en_pullup_disable = of_property_read_bool(np, "bias-disable");

	/* Request low-power mode for main bias */
	cfg->bias_low_power_request = of_property_read_bool(np, "adi,bias-low-power-request");

	/* Force internal LDO to always supply 1.8V */
	cfg->simo_ldo_always_on = of_property_read_bool(np, "adi,simo-ldo-always-on");

	return ret;
}

static int max77675_init_event(struct max77675_regulator *maxreg)
{
	unsigned int ercflag, int_glbl;
	int ret;

	ret = regmap_read(maxreg->regmap, MAX77675_REG_ERCF_GLBL, &ercflag);
	if (ret) {
		dev_err(maxreg->dev, "Failed to read CID register: %d\n", ret);
		return ret;
	}

	ret = regmap_read(maxreg->regmap, MAX77675_REG_INT_GLBL, &int_glbl);
	if (ret) {
		dev_err(maxreg->dev, "Failed to read INT_GLBL register: %d\n", ret);
		return ret;
	}

	if (ercflag & MAX77675_SFT_CRST_F_BIT)
		dev_dbg(maxreg->dev, "Software Cold Reset Flag is set\n");

	if (ercflag & MAX77675_SFT_OFF_F_BIT)
		dev_dbg(maxreg->dev, "Software Off Flag is set\n");

	if (ercflag & MAX77675_MRST_BIT)
		dev_dbg(maxreg->dev, "Manual Reset Timer Flag is set\n");

	if (ercflag & MAX77675_UVLO_BIT)
		dev_dbg(maxreg->dev, "Undervoltage Lockout Flag is set\n");

	if (ercflag & MAX77675_OVLO_BIT)
		dev_dbg(maxreg->dev, "Overvoltage Lockout Flag is set\n");

	if (ercflag & MAX77675_TOVLD_BIT)
		dev_dbg(maxreg->dev, "Thermal Overload Flag is set\n");

	if (int_glbl & MAX77675_INT_SBB3_F_BIT)
		dev_dbg(maxreg->dev, "SBB3 Channel Fault Interrupt occurred\n");

	if (int_glbl & MAX77675_INT_SBB2_F_BIT)
		dev_dbg(maxreg->dev, "SBB2 Channel Fault Interrupt occurred\n");

	if (int_glbl & MAX77675_INT_SBB1_F_BIT)
		dev_dbg(maxreg->dev, "SBB1 Channel Fault Interrupt occurred\n");

	if (int_glbl & MAX77675_INT_SBB0_F_BIT)
		dev_dbg(maxreg->dev, "SBB0 Channel Fault Interrupt occurred\n");

	if (int_glbl & MAX77675_INT_TJAL2_R_BIT)
		dev_dbg(maxreg->dev, "Thermal Alarm 2 Rising Interrupt occurred\n");

	if (int_glbl & MAX77675_INT_TJAL1_R_BIT)
		dev_dbg(maxreg->dev, "Thermal Alarm 1 Rising Interrupt occurred\n");

	if (int_glbl & MAX77675_INT_EN_R_BIT)
		dev_dbg(maxreg->dev, "nEN Rising Edge Interrupt occurred\n");

	if (int_glbl & MAX77675_INT_EN_F_BIT)
		dev_dbg(maxreg->dev, "nEN Falling Edge Interrupt occurred\n");

	return 0;
}

static int max77675_regulator_probe(struct i2c_client *client)
{
	struct max77675_regulator *maxreg;
	struct regulator_config config = {};
	int i, ret;

	maxreg = devm_kzalloc(&client->dev, sizeof(*maxreg), GFP_KERNEL);
	if (!maxreg)
		return -ENOMEM;

	maxreg->dev = &client->dev;

	maxreg->regmap = devm_regmap_init_i2c(client, &max77675_regmap_config);
	if (IS_ERR(maxreg->regmap))
		return dev_err_probe(maxreg->dev,
				     PTR_ERR(maxreg->regmap),
				     "Failed to init regmap\n");

	ret = max77675_init_event(maxreg);
	if (ret < 0)
		return dev_err_probe(maxreg->dev, ret, "Failed to init event\n");

	ret = max77675_parse_config(maxreg);
	if (ret < 0)
		return dev_err_probe(maxreg->dev, ret, "Failed to parse config\n");

	ret = max77675_apply_config(maxreg);
	if (ret < 0)
		return dev_err_probe(maxreg->dev, ret, "Failed to apply config\n");

	config.dev = &client->dev;
	config.regmap = maxreg->regmap;
	config.driver_data = maxreg;

	struct device_node *regulators_np __free(device_node) =
		of_get_child_by_name(client->dev.of_node, "regulators");
	if (!regulators_np) {
		dev_err(maxreg->dev, "No 'regulators' subnode found in DT\n");
		return -EINVAL;
	}

	for (i = 0; i < MAX77675_ID_NUM_MAX; i++) {
		const struct regulator_desc *desc = &max77675_regulators[i];
		struct regulator_dev *rdev;

		struct device_node *child_np __free(device_node) =
			of_get_child_by_name(regulators_np, desc->name);
		if (!child_np) {
			dev_warn(maxreg->dev, "No DT node for regulator %s\n", desc->name);
			continue;
		}

		config.of_node = child_np;

		rdev = devm_regulator_register(&client->dev, desc, &config);
		if (IS_ERR(rdev)) {
			return dev_err_probe(maxreg->dev, PTR_ERR(rdev),
				"Failed to register regulator %d (%s)\n",
				i, desc->name);
		}
	}

	return 0;
}

static const struct i2c_device_id max77675_i2c_id[] = {
	{ "max77675", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max77675_i2c_id);

static const struct of_device_id __maybe_unused max77675_of_match[] = {
	{ .compatible = "adi,max77675", },
	{ }
};
MODULE_DEVICE_TABLE(of, max77675_of_match);

static struct i2c_driver max77675_regulator_driver = {
	.driver = {
		.name = "max77675",
		.of_match_table = of_match_ptr(max77675_of_match),
	},
	.probe = max77675_regulator_probe,
	.id_table = max77675_i2c_id,
};

module_i2c_driver(max77675_regulator_driver);

MODULE_DESCRIPTION("MAX77675 Regulator Driver");
MODULE_AUTHOR("Joan Na <joan.na@analog.com>");
MODULE_LICENSE("GPL");
