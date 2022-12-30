// SPDX-License-Identifier: GPL-2.0-only
/*
 * ADP5061 I2C Programmable Linear Battery Charger
 *
 * Copyright 2018 Analog Devices Inc.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/mod_devicetable.h>
#include <linux/power_supply.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/regmap.h>

/* ADP5061 registers definition */
#define ADP5061_ID			0x00
#define ADP5061_REV			0x01
#define ADP5061_VINX_SET		0x02
#define ADP5061_TERM_SET		0x03
#define ADP5061_CHG_CURR		0x04
#define ADP5061_VOLTAGE_TH		0x05
#define ADP5061_TIMER_SET		0x06
#define ADP5061_FUNC_SET_1		0x07
#define ADP5061_FUNC_SET_2		0x08
#define ADP5061_INT_EN			0x09
#define ADP5061_INT_ACT			0x0A
#define ADP5061_CHG_STATUS_1		0x0B
#define ADP5061_CHG_STATUS_2		0x0C
#define ADP5061_FAULT			0x0D
#define ADP5061_BATTERY_SHORT		0x10
#define ADP5061_IEND			0x11

/* ADP5061_VINX_SET */
#define ADP5061_VINX_SET_ILIM_MSK		GENMASK(3, 0)
#define ADP5061_VINX_SET_ILIM_MODE(x)		(((x) & 0x0F) << 0)

/* ADP5061_TERM_SET */
#define ADP5061_TERM_SET_VTRM_MSK		GENMASK(7, 2)
#define ADP5061_TERM_SET_VTRM_MODE(x)		(((x) & 0x3F) << 2)
#define ADP5061_TERM_SET_CHG_VLIM_MSK		GENMASK(1, 0)
#define ADP5061_TERM_SET_CHG_VLIM_MODE(x)	(((x) & 0x03) << 0)

/* ADP5061_CHG_CURR */
#define ADP5061_CHG_CURR_ICHG_MSK		GENMASK(6, 2)
#define ADP5061_CHG_CURR_ICHG_MODE(x)		(((x) & 0x1F) << 2)
#define ADP5061_CHG_CURR_ITRK_DEAD_MSK		GENMASK(1, 0)
#define ADP5061_CHG_CURR_ITRK_DEAD_MODE(x)	(((x) & 0x03) << 0)

/* ADP5061_VOLTAGE_TH */
#define ADP5061_VOLTAGE_TH_DIS_RCH_MSK		BIT(7)
#define ADP5061_VOLTAGE_TH_DIS_RCH_MODE(x)	(((x) & 0x01) << 7)
#define ADP5061_VOLTAGE_TH_VRCH_MSK		GENMASK(6, 5)
#define ADP5061_VOLTAGE_TH_VRCH_MODE(x)		(((x) & 0x03) << 5)
#define ADP5061_VOLTAGE_TH_VTRK_DEAD_MSK	GENMASK(4, 3)
#define ADP5061_VOLTAGE_TH_VTRK_DEAD_MODE(x)	(((x) & 0x03) << 3)
#define ADP5061_VOLTAGE_TH_VWEAK_MSK		GENMASK(2, 0)
#define ADP5061_VOLTAGE_TH_VWEAK_MODE(x)	(((x) & 0x07) << 0)

/* ADP5061_CHG_STATUS_1 */
#define ADP5061_CHG_STATUS_1_VIN_OV(x)		(((x) >> 7) & 0x1)
#define ADP5061_CHG_STATUS_1_VIN_OK(x)		(((x) >> 6) & 0x1)
#define ADP5061_CHG_STATUS_1_VIN_ILIM(x)	(((x) >> 5) & 0x1)
#define ADP5061_CHG_STATUS_1_THERM_LIM(x)	(((x) >> 4) & 0x1)
#define ADP5061_CHG_STATUS_1_CHDONE(x)		(((x) >> 3) & 0x1)
#define ADP5061_CHG_STATUS_1_CHG_STATUS(x)	(((x) >> 0) & 0x7)

/* ADP5061_CHG_STATUS_2 */
#define ADP5061_CHG_STATUS_2_THR_STATUS(x)	(((x) >> 5) & 0x7)
#define ADP5061_CHG_STATUS_2_RCH_LIM_INFO(x)	(((x) >> 3) & 0x1)
#define ADP5061_CHG_STATUS_2_BAT_STATUS(x)	(((x) >> 0) & 0x7)

/* ADP5061_IEND */
#define ADP5061_IEND_IEND_MSK			GENMASK(7, 5)
#define ADP5061_IEND_IEND_MODE(x)		(((x) & 0x07) << 5)

#define ADP5061_NO_BATTERY	0x01
#define ADP5061_ICHG_MAX	1300 // mA

enum adp5061_chg_status {
	ADP5061_CHG_OFF,
	ADP5061_CHG_TRICKLE,
	ADP5061_CHG_FAST_CC,
	ADP5061_CHG_FAST_CV,
	ADP5061_CHG_COMPLETE,
	ADP5061_CHG_LDO_MODE,
	ADP5061_CHG_TIMER_EXP,
	ADP5061_CHG_BAT_DET,
};

static const int adp5061_chg_type[4] = {
	[ADP5061_CHG_OFF] = POWER_SUPPLY_CHARGE_TYPE_NONE,
	[ADP5061_CHG_TRICKLE] = POWER_SUPPLY_CHARGE_TYPE_TRICKLE,
	[ADP5061_CHG_FAST_CC] = POWER_SUPPLY_CHARGE_TYPE_FAST,
	[ADP5061_CHG_FAST_CV] = POWER_SUPPLY_CHARGE_TYPE_FAST,
};

static const int adp5061_vweak_th[8] = {
	2700, 2800, 2900, 3000, 3100, 3200, 3300, 3400,
};

static const int adp5061_prechg_current[4] = {
	5, 10, 20, 80,
};

static const int adp5061_vmin[4] = {
	2000, 2500, 2600, 2900,
};

static const int adp5061_const_chg_vmax[4] = {
	3200, 3400, 3700, 3800,
};

static const int adp5061_const_ichg[24] = {
	50, 100, 150, 200, 250, 300, 350, 400, 450, 500, 550, 600, 650,
	700, 750, 800, 850, 900, 950, 1000, 1050, 1100, 1200, 1300,
};

static const int adp5061_vmax[36] = {
	3800, 3820, 3840, 3860, 3880, 3900, 3920, 3940, 3960, 3980,
	4000, 4020, 4040, 4060, 4080, 4100, 4120, 4140, 4160, 4180,
	4200, 4220, 4240, 4260, 4280, 4300, 4320, 4340, 4360, 4380,
	4400, 4420, 4440, 4460, 4480, 4500,
};

static const int adp5061_in_current_lim[16] = {
	100, 150, 200, 250, 300, 400, 500, 600, 700,
	800, 900, 1000, 1200, 1500, 1800, 2100,
};

static const int adp5061_iend[8] = {
	12500, 32500, 52500, 72500, 92500, 117500, 142500, 170000,
};

struct adp5061_state {
	struct i2c_client		*client;
	struct regmap			*regmap;
	struct power_supply		*psy;
};

static int adp5061_get_array_index(const int *array, u8 size, int val)
{
	int i;

	for (i = 1; i < size; i++) {
		if (val < array[i])
			break;
	}

	return i-1;
}

static int adp5061_get_status(struct adp5061_state *st,
			      u8 *status1, u8 *status2)
{
	u8 buf[2];
	int ret;

	/* CHG_STATUS1 and CHG_STATUS2 are adjacent regs */
	ret = regmap_bulk_read(st->regmap, ADP5061_CHG_STATUS_1,
			       &buf[0], 2);
	if (ret < 0)
		return ret;

	*status1 = buf[0];
	*status2 = buf[1];

	return ret;
}

static int adp5061_get_input_current_limit(struct adp5061_state *st,
		union power_supply_propval *val)
{
	unsigned int regval;
	int mode, ret;

	ret = regmap_read(st->regmap, ADP5061_VINX_SET, &regval);
	if (ret < 0)
		return ret;

	mode = ADP5061_VINX_SET_ILIM_MODE(regval);
	val->intval = adp5061_in_current_lim[mode] * 1000;

	return ret;
}

static int adp5061_set_input_current_limit(struct adp5061_state *st, int val)
{
	int index;

	/* Convert from uA to mA */
	val /= 1000;
	index = adp5061_get_array_index(adp5061_in_current_lim,
					ARRAY_SIZE(adp5061_in_current_lim),
					val);
	if (index < 0)
		return index;

	return regmap_update_bits(st->regmap, ADP5061_VINX_SET,
				  ADP5061_VINX_SET_ILIM_MSK,
				  ADP5061_VINX_SET_ILIM_MODE(index));
}

static int adp5061_set_min_voltage(struct adp5061_state *st, int val)
{
	int index;

	/* Convert from uV to mV */
	val /= 1000;
	index = adp5061_get_array_index(adp5061_vmin,
					ARRAY_SIZE(adp5061_vmin),
					val);
	if (index < 0)
		return index;

	return regmap_update_bits(st->regmap, ADP5061_VOLTAGE_TH,
				  ADP5061_VOLTAGE_TH_VTRK_DEAD_MSK,
				  ADP5061_VOLTAGE_TH_VTRK_DEAD_MODE(index));
}

static int adp5061_get_min_voltage(struct adp5061_state *st,
				   union power_supply_propval *val)
{
	unsigned int regval;
	int ret;

	ret = regmap_read(st->regmap, ADP5061_VOLTAGE_TH, &regval);
	if (ret < 0)
		return ret;

	regval = ((regval & ADP5061_VOLTAGE_TH_VTRK_DEAD_MSK) >> 3);
	val->intval = adp5061_vmin[regval] * 1000;

	return ret;
}

static int adp5061_get_chg_volt_lim(struct adp5061_state *st,
				    union power_supply_propval *val)
{
	unsigned int regval;
	int mode, ret;

	ret = regmap_read(st->regmap, ADP5061_TERM_SET, &regval);
	if (ret < 0)
		return ret;

	mode = ADP5061_TERM_SET_CHG_VLIM_MODE(regval);
	val->intval = adp5061_const_chg_vmax[mode] * 1000;

	return ret;
}

static int adp5061_get_max_voltage(struct adp5061_state *st,
				   union power_supply_propval *val)
{
	unsigned int regval;
	int ret;

	ret = regmap_read(st->regmap, ADP5061_TERM_SET, &regval);
	if (ret < 0)
		return ret;

	regval = ((regval & ADP5061_TERM_SET_VTRM_MSK) >> 2) - 0x0F;
	if (regval >= ARRAY_SIZE(adp5061_vmax))
		regval = ARRAY_SIZE(adp5061_vmax) - 1;

	val->intval = adp5061_vmax[regval] * 1000;

	return ret;
}

static int adp5061_set_max_voltage(struct adp5061_state *st, int val)
{
	int vmax_index;

	/* Convert from uV to mV */
	val /= 1000;
	if (val > 4500)
		val = 4500;

	vmax_index = adp5061_get_array_index(adp5061_vmax,
					     ARRAY_SIZE(adp5061_vmax), val);
	if (vmax_index < 0)
		return vmax_index;

	vmax_index += 0x0F;

	return regmap_update_bits(st->regmap, ADP5061_TERM_SET,
				  ADP5061_TERM_SET_VTRM_MSK,
				  ADP5061_TERM_SET_VTRM_MODE(vmax_index));
}

static int adp5061_set_const_chg_vmax(struct adp5061_state *st, int val)
{
	int index;

	/* Convert from uV to mV */
	val /= 1000;
	index = adp5061_get_array_index(adp5061_const_chg_vmax,
					ARRAY_SIZE(adp5061_const_chg_vmax),
					val);
	if (index < 0)
		return index;

	return regmap_update_bits(st->regmap, ADP5061_TERM_SET,
				  ADP5061_TERM_SET_CHG_VLIM_MSK,
				  ADP5061_TERM_SET_CHG_VLIM_MODE(index));
}

static int adp5061_set_const_chg_current(struct adp5061_state *st, int val)
{

	int index;

	/* Convert from uA to mA */
	val /= 1000;
	if (val > ADP5061_ICHG_MAX)
		val = ADP5061_ICHG_MAX;

	index = adp5061_get_array_index(adp5061_const_ichg,
					ARRAY_SIZE(adp5061_const_ichg),
					val);
	if (index < 0)
		return index;

	return regmap_update_bits(st->regmap, ADP5061_CHG_CURR,
				  ADP5061_CHG_CURR_ICHG_MSK,
				  ADP5061_CHG_CURR_ICHG_MODE(index));
}

static int adp5061_get_const_chg_current(struct adp5061_state *st,
		union power_supply_propval *val)
{
	unsigned int regval;
	int ret;

	ret = regmap_read(st->regmap, ADP5061_CHG_CURR, &regval);
	if (ret < 0)
		return ret;

	regval = ((regval & ADP5061_CHG_CURR_ICHG_MSK) >> 2);
	if (regval >= ARRAY_SIZE(adp5061_const_ichg))
		regval = ARRAY_SIZE(adp5061_const_ichg) - 1;

	val->intval = adp5061_const_ichg[regval] * 1000;

	return ret;
}

static int adp5061_get_prechg_current(struct adp5061_state *st,
				      union power_supply_propval *val)
{
	unsigned int regval;
	int ret;

	ret = regmap_read(st->regmap, ADP5061_CHG_CURR, &regval);
	if (ret < 0)
		return ret;

	regval &= ADP5061_CHG_CURR_ITRK_DEAD_MSK;
	val->intval = adp5061_prechg_current[regval] * 1000;

	return ret;
}

static int adp5061_set_prechg_current(struct adp5061_state *st, int val)
{
	int index;

	/* Convert from uA to mA */
	val /= 1000;
	index = adp5061_get_array_index(adp5061_prechg_current,
					ARRAY_SIZE(adp5061_prechg_current),
					val);
	if (index < 0)
		return index;

	return regmap_update_bits(st->regmap, ADP5061_CHG_CURR,
				  ADP5061_CHG_CURR_ITRK_DEAD_MSK,
				  ADP5061_CHG_CURR_ITRK_DEAD_MODE(index));
}

static int adp5061_get_vweak_th(struct adp5061_state *st,
				union power_supply_propval *val)
{
	unsigned int regval;
	int ret;

	ret = regmap_read(st->regmap, ADP5061_VOLTAGE_TH, &regval);
	if (ret < 0)
		return ret;

	regval &= ADP5061_VOLTAGE_TH_VWEAK_MSK;
	val->intval = adp5061_vweak_th[regval] * 1000;

	return ret;
}

static int adp5061_set_vweak_th(struct adp5061_state *st, int val)
{
	int index;

	/* Convert from uV to mV */
	val /= 1000;
	index = adp5061_get_array_index(adp5061_vweak_th,
					ARRAY_SIZE(adp5061_vweak_th),
					val);
	if (index < 0)
		return index;

	return regmap_update_bits(st->regmap, ADP5061_VOLTAGE_TH,
				  ADP5061_VOLTAGE_TH_VWEAK_MSK,
				  ADP5061_VOLTAGE_TH_VWEAK_MODE(index));
}

static int adp5061_get_chg_type(struct adp5061_state *st,
				union power_supply_propval *val)
{
	u8 status1, status2;
	int chg_type, ret;

	ret = adp5061_get_status(st, &status1, &status2);
	if (ret < 0)
		return ret;

	chg_type = ADP5061_CHG_STATUS_1_CHG_STATUS(status1);
	if (chg_type >= ARRAY_SIZE(adp5061_chg_type))
		val->intval = POWER_SUPPLY_STATUS_UNKNOWN;
	else
		val->intval = adp5061_chg_type[chg_type];

	return ret;
}

static int adp5061_get_charger_status(struct adp5061_state *st,
				      union power_supply_propval *val)
{
	u8 status1, status2;
	int ret;

	ret = adp5061_get_status(st, &status1, &status2);
	if (ret < 0)
		return ret;

	switch (ADP5061_CHG_STATUS_1_CHG_STATUS(status1)) {
	case ADP5061_CHG_OFF:
		val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
		break;
	case ADP5061_CHG_TRICKLE:
	case ADP5061_CHG_FAST_CC:
	case ADP5061_CHG_FAST_CV:
		val->intval = POWER_SUPPLY_STATUS_CHARGING;
		break;
	case ADP5061_CHG_COMPLETE:
		val->intval = POWER_SUPPLY_STATUS_FULL;
		break;
	case ADP5061_CHG_TIMER_EXP:
		/* The battery must be discharging if there is a charge fault */
		val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		break;
	default:
		val->intval = POWER_SUPPLY_STATUS_UNKNOWN;
	}

	return ret;
}

static int adp5061_get_battery_status(struct adp5061_state *st,
				      union power_supply_propval *val)
{
	u8 status1, status2;
	int ret;

	ret = adp5061_get_status(st, &status1, &status2);
	if (ret < 0)
		return ret;

	switch (ADP5061_CHG_STATUS_2_BAT_STATUS(status2)) {
	case 0x0: /* Battery monitor off */
	case 0x1: /* No battery */
		val->intval = POWER_SUPPLY_CAPACITY_LEVEL_UNKNOWN;
		break;
	case 0x2: /* VBAT < VTRK */
		val->intval = POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL;
		break;
	case 0x3: /* VTRK < VBAT_SNS < VWEAK */
		val->intval = POWER_SUPPLY_CAPACITY_LEVEL_LOW;
		break;
	case 0x4: /* VBAT_SNS > VWEAK */
		val->intval = POWER_SUPPLY_CAPACITY_LEVEL_NORMAL;
		break;
	default:
		val->intval = POWER_SUPPLY_CAPACITY_LEVEL_UNKNOWN;
		break;
	}

	return ret;
}

static int adp5061_get_termination_current(struct adp5061_state *st,
					   union power_supply_propval *val)
{
	unsigned int regval;
	int ret;

	ret = regmap_read(st->regmap, ADP5061_IEND, &regval);
	if (ret < 0)
		return ret;

	regval = (regval & ADP5061_IEND_IEND_MSK) >> 5;
	val->intval = adp5061_iend[regval];

	return ret;
}

static int adp5061_set_termination_current(struct adp5061_state *st, int val)
{
	int index;

	index = adp5061_get_array_index(adp5061_iend,
					ARRAY_SIZE(adp5061_iend),
					val);
	if (index < 0)
		return index;

	return regmap_update_bits(st->regmap, ADP5061_IEND,
				  ADP5061_IEND_IEND_MSK,
				  ADP5061_IEND_IEND_MODE(index));
}

static int adp5061_get_property(struct power_supply *psy,
				enum power_supply_property psp,
				union power_supply_propval *val)
{
	struct adp5061_state *st = power_supply_get_drvdata(psy);
	u8 status1, status2;
	int mode, ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
		ret = adp5061_get_status(st, &status1, &status2);
		if (ret < 0)
			return ret;

		mode = ADP5061_CHG_STATUS_2_BAT_STATUS(status2);
		if (mode == ADP5061_NO_BATTERY)
			val->intval = 0;
		else
			val->intval = 1;
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		return adp5061_get_chg_type(st, val);
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		/* This property is used to indicate the input current
		 * limit into VINx (ILIM)
		 */
		return adp5061_get_input_current_limit(st, val);
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		/* This property is used to indicate the termination
		 * voltage (VTRM)
		 */
		return adp5061_get_max_voltage(st, val);
	case POWER_SUPPLY_PROP_VOLTAGE_MIN:
		/*
		 * This property is used to indicate the trickle to fast
		 * charge threshold (VTRK_DEAD)
		 */
		return adp5061_get_min_voltage(st, val);
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX:
		/* This property is used to indicate the charging
		 * voltage limit (CHG_VLIM)
		 */
		return adp5061_get_chg_volt_lim(st, val);
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		/*
		 * This property is used to indicate the value of the constant
		 * current charge (ICHG)
		 */
		return adp5061_get_const_chg_current(st, val);
	case POWER_SUPPLY_PROP_PRECHARGE_CURRENT:
		/*
		 * This property is used to indicate the value of the trickle
		 * and weak charge currents (ITRK_DEAD)
		 */
		return adp5061_get_prechg_current(st, val);
	case POWER_SUPPLY_PROP_VOLTAGE_AVG:
		/*
		 * This property is used to set the VWEAK threshold
		 * bellow this value, weak charge mode is entered
		 * above this value, fast chargerge mode is entered
		 */
		return adp5061_get_vweak_th(st, val);
	case POWER_SUPPLY_PROP_STATUS:
		/*
		 * Indicate the charger status in relation to power
		 * supply status property
		 */
		return adp5061_get_charger_status(st, val);
	case POWER_SUPPLY_PROP_CAPACITY_LEVEL:
		/*
		 * Indicate the battery status in relation to power
		 * supply capacity level property
		 */
		return adp5061_get_battery_status(st, val);
	case POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT:
		/* Indicate the values of the termination current */
		return adp5061_get_termination_current(st, val);
	default:
		return -EINVAL;
	}

	return 0;
}

static int adp5061_set_property(struct power_supply *psy,
				enum power_supply_property psp,
				const union power_supply_propval *val)
{
	struct adp5061_state *st = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		return adp5061_set_input_current_limit(st, val->intval);
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		return adp5061_set_max_voltage(st, val->intval);
	case POWER_SUPPLY_PROP_VOLTAGE_MIN:
		return adp5061_set_min_voltage(st, val->intval);
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX:
		return adp5061_set_const_chg_vmax(st, val->intval);
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		return adp5061_set_const_chg_current(st, val->intval);
	case POWER_SUPPLY_PROP_PRECHARGE_CURRENT:
		return adp5061_set_prechg_current(st, val->intval);
	case POWER_SUPPLY_PROP_VOLTAGE_AVG:
		return adp5061_set_vweak_th(st, val->intval);
	case POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT:
		return adp5061_set_termination_current(st, val->intval);
	default:
		return -EINVAL;
	}

	return 0;
}

static int adp5061_prop_writeable(struct power_supply *psy,
				  enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
	case POWER_SUPPLY_PROP_VOLTAGE_MIN:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
	case POWER_SUPPLY_PROP_PRECHARGE_CURRENT:
	case POWER_SUPPLY_PROP_VOLTAGE_AVG:
	case POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT:
		return 1;
	default:
		return 0;
	}
}

static enum power_supply_property adp5061_props[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_MIN,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
	POWER_SUPPLY_PROP_PRECHARGE_CURRENT,
	POWER_SUPPLY_PROP_VOLTAGE_AVG,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
	POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT,
};

static const struct regmap_config adp5061_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static const struct power_supply_desc adp5061_desc = {
	.name			= "adp5061",
	.type			= POWER_SUPPLY_TYPE_USB,
	.get_property		= adp5061_get_property,
	.set_property		= adp5061_set_property,
	.property_is_writeable	= adp5061_prop_writeable,
	.properties		= adp5061_props,
	.num_properties		= ARRAY_SIZE(adp5061_props),
};

static int adp5061_probe(struct i2c_client *client)
{
	struct power_supply_config psy_cfg = {};
	struct adp5061_state *st;

	st = devm_kzalloc(&client->dev, sizeof(*st), GFP_KERNEL);
	if (!st)
		return -ENOMEM;

	st->client = client;
	st->regmap = devm_regmap_init_i2c(client,
					  &adp5061_regmap_config);
	if (IS_ERR(st->regmap)) {
		dev_err(&client->dev, "Failed to initialize register map\n");
		return -EINVAL;
	}

	i2c_set_clientdata(client, st);
	psy_cfg.drv_data = st;

	st->psy = devm_power_supply_register(&client->dev,
					     &adp5061_desc,
					     &psy_cfg);

	if (IS_ERR(st->psy)) {
		dev_err(&client->dev, "Failed to register power supply\n");
		return PTR_ERR(st->psy);
	}

	return 0;
}

static const struct i2c_device_id adp5061_id[] = {
	{ "adp5061", 0},
	{ }
};
MODULE_DEVICE_TABLE(i2c, adp5061_id);

static struct i2c_driver adp5061_driver = {
	.driver = {
		.name = KBUILD_MODNAME,
	},
	.probe_new = adp5061_probe,
	.id_table = adp5061_id,
};
module_i2c_driver(adp5061_driver);

MODULE_DESCRIPTION("Analog Devices adp5061 battery charger driver");
MODULE_AUTHOR("Stefan Popa <stefan.popa@analog.com>");
MODULE_LICENSE("GPL v2");
