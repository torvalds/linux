// SPDX-License-Identifier: GPL-2.0+
//
// max77693_charger.c - Battery charger driver for the Maxim 77693
//
// Copyright (C) 2014 Samsung Electronics
// Krzysztof Kozlowski <krzk@kernel.org>

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>
#include <linux/mfd/max77693.h>
#include <linux/mfd/max77693-common.h>
#include <linux/mfd/max77693-private.h>

#define MAX77693_CHARGER_NAME				"max77693-charger"
static const char *max77693_charger_model		= "MAX77693";
static const char *max77693_charger_manufacturer	= "Maxim Integrated";

struct max77693_charger {
	struct device		*dev;
	struct max77693_dev	*max77693;
	struct power_supply	*charger;

	u32 constant_volt;
	u32 min_system_volt;
	u32 thermal_regulation_temp;
	u32 batttery_overcurrent;
	u32 charge_input_threshold_volt;
};

static int max77693_get_charger_state(struct regmap *regmap, int *val)
{
	int ret;
	unsigned int data;

	ret = regmap_read(regmap, MAX77693_CHG_REG_CHG_DETAILS_01, &data);
	if (ret < 0)
		return ret;

	data &= CHG_DETAILS_01_CHG_MASK;
	data >>= CHG_DETAILS_01_CHG_SHIFT;

	switch (data) {
	case MAX77693_CHARGING_PREQUALIFICATION:
	case MAX77693_CHARGING_FAST_CONST_CURRENT:
	case MAX77693_CHARGING_FAST_CONST_VOLTAGE:
	case MAX77693_CHARGING_TOP_OFF:
	/* In high temp the charging current is reduced, but still charging */
	case MAX77693_CHARGING_HIGH_TEMP:
		*val = POWER_SUPPLY_STATUS_CHARGING;
		break;
	case MAX77693_CHARGING_DONE:
		*val = POWER_SUPPLY_STATUS_FULL;
		break;
	case MAX77693_CHARGING_TIMER_EXPIRED:
	case MAX77693_CHARGING_THERMISTOR_SUSPEND:
		*val = POWER_SUPPLY_STATUS_NOT_CHARGING;
		break;
	case MAX77693_CHARGING_OFF:
	case MAX77693_CHARGING_OVER_TEMP:
	case MAX77693_CHARGING_WATCHDOG_EXPIRED:
		*val = POWER_SUPPLY_STATUS_DISCHARGING;
		break;
	case MAX77693_CHARGING_RESERVED:
	default:
		*val = POWER_SUPPLY_STATUS_UNKNOWN;
	}

	return 0;
}

static int max77693_get_charge_type(struct regmap *regmap, int *val)
{
	int ret;
	unsigned int data;

	ret = regmap_read(regmap, MAX77693_CHG_REG_CHG_DETAILS_01, &data);
	if (ret < 0)
		return ret;

	data &= CHG_DETAILS_01_CHG_MASK;
	data >>= CHG_DETAILS_01_CHG_SHIFT;

	switch (data) {
	case MAX77693_CHARGING_PREQUALIFICATION:
	/*
	 * Top-off: trickle or fast? In top-off the current varies between
	 * 100 and 250 mA. It is higher than prequalification current.
	 */
	case MAX77693_CHARGING_TOP_OFF:
		*val = POWER_SUPPLY_CHARGE_TYPE_TRICKLE;
		break;
	case MAX77693_CHARGING_FAST_CONST_CURRENT:
	case MAX77693_CHARGING_FAST_CONST_VOLTAGE:
	/* In high temp the charging current is reduced, but still charging */
	case MAX77693_CHARGING_HIGH_TEMP:
		*val = POWER_SUPPLY_CHARGE_TYPE_FAST;
		break;
	case MAX77693_CHARGING_DONE:
	case MAX77693_CHARGING_TIMER_EXPIRED:
	case MAX77693_CHARGING_THERMISTOR_SUSPEND:
	case MAX77693_CHARGING_OFF:
	case MAX77693_CHARGING_OVER_TEMP:
	case MAX77693_CHARGING_WATCHDOG_EXPIRED:
		*val = POWER_SUPPLY_CHARGE_TYPE_NONE;
		break;
	case MAX77693_CHARGING_RESERVED:
	default:
		*val = POWER_SUPPLY_CHARGE_TYPE_UNKNOWN;
	}

	return 0;
}

/*
 * Supported health statuses:
 *  - POWER_SUPPLY_HEALTH_DEAD
 *  - POWER_SUPPLY_HEALTH_GOOD
 *  - POWER_SUPPLY_HEALTH_OVERVOLTAGE
 *  - POWER_SUPPLY_HEALTH_SAFETY_TIMER_EXPIRE
 *  - POWER_SUPPLY_HEALTH_UNKNOWN
 *  - POWER_SUPPLY_HEALTH_UNSPEC_FAILURE
 */
static int max77693_get_battery_health(struct regmap *regmap, int *val)
{
	int ret;
	unsigned int data;

	ret = regmap_read(regmap, MAX77693_CHG_REG_CHG_DETAILS_01, &data);
	if (ret < 0)
		return ret;

	data &= CHG_DETAILS_01_BAT_MASK;
	data >>= CHG_DETAILS_01_BAT_SHIFT;

	switch (data) {
	case MAX77693_BATTERY_NOBAT:
		*val = POWER_SUPPLY_HEALTH_DEAD;
		break;
	case MAX77693_BATTERY_PREQUALIFICATION:
	case MAX77693_BATTERY_GOOD:
	case MAX77693_BATTERY_LOWVOLTAGE:
		*val = POWER_SUPPLY_HEALTH_GOOD;
		break;
	case MAX77693_BATTERY_TIMER_EXPIRED:
		/*
		 * Took longer to charge than expected, charging suspended.
		 * Damaged battery?
		 */
		*val = POWER_SUPPLY_HEALTH_SAFETY_TIMER_EXPIRE;
		break;
	case MAX77693_BATTERY_OVERVOLTAGE:
		*val = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
		break;
	case MAX77693_BATTERY_OVERCURRENT:
		*val = POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;
		break;
	case MAX77693_BATTERY_RESERVED:
	default:
		*val = POWER_SUPPLY_HEALTH_UNKNOWN;
		break;
	}

	return 0;
}

static int max77693_get_present(struct regmap *regmap, int *val)
{
	unsigned int data;
	int ret;

	/*
	 * Read CHG_INT_OK register. High DETBAT bit here should be
	 * equal to value 0x0 in CHG_DETAILS_01/BAT field.
	 */
	ret = regmap_read(regmap, MAX77693_CHG_REG_CHG_INT_OK, &data);
	if (ret < 0)
		return ret;

	*val = (data & CHG_INT_OK_DETBAT_MASK) ? 0 : 1;

	return 0;
}

static int max77693_get_online(struct regmap *regmap, int *val)
{
	unsigned int data;
	int ret;

	ret = regmap_read(regmap, MAX77693_CHG_REG_CHG_INT_OK, &data);
	if (ret < 0)
		return ret;

	*val = (data & CHG_INT_OK_CHGIN_MASK) ? 1 : 0;

	return 0;
}

/*
 * There are *two* current limit registers:
 * - CHGIN limit, which limits the input current from the external charger;
 * - Fast charge current limit, which limits the current going to the battery.
 */

static int max77693_get_input_current_limit(struct regmap *regmap, int *val)
{
	unsigned int data;
	int ret;

	ret = regmap_read(regmap, MAX77693_CHG_REG_CHG_CNFG_09, &data);
	if (ret < 0)
		return ret;

	data &= CHG_CNFG_09_CHGIN_ILIM_MASK;
	data >>= CHG_CNFG_09_CHGIN_ILIM_SHIFT;

	if (data <= 0x03)
		/* The first four values (0x00..0x03) are 60mA */
		*val = 60000;
	else
		*val = data * 20000; /* 20mA steps */

	return 0;
}

static int max77693_get_fast_charge_current(struct regmap *regmap, int *val)
{
	unsigned int data;
	int ret;

	ret = regmap_read(regmap, MAX77693_CHG_REG_CHG_CNFG_02, &data);
	if (ret < 0)
		return ret;

	data &= CHG_CNFG_02_CC_MASK;
	data >>= CHG_CNFG_02_CC_SHIFT;

	*val = data * 33300; /* 33.3mA steps */

	return 0;
}

static enum power_supply_property max77693_charger_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_MANUFACTURER,
};

static int max77693_charger_get_property(struct power_supply *psy,
			    enum power_supply_property psp,
			    union power_supply_propval *val)
{
	struct max77693_charger *chg = power_supply_get_drvdata(psy);
	struct regmap *regmap = chg->max77693->regmap;
	int ret = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		ret = max77693_get_charger_state(regmap, &val->intval);
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		ret = max77693_get_charge_type(regmap, &val->intval);
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		ret = max77693_get_battery_health(regmap, &val->intval);
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		ret = max77693_get_present(regmap, &val->intval);
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		ret = max77693_get_online(regmap, &val->intval);
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		ret = max77693_get_input_current_limit(regmap, &val->intval);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		ret = max77693_get_fast_charge_current(regmap, &val->intval);
		break;
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = max77693_charger_model;
		break;
	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = max77693_charger_manufacturer;
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

static const struct power_supply_desc max77693_charger_desc = {
	.name		= MAX77693_CHARGER_NAME,
	.type		= POWER_SUPPLY_TYPE_BATTERY,
	.properties	= max77693_charger_props,
	.num_properties	= ARRAY_SIZE(max77693_charger_props),
	.get_property	= max77693_charger_get_property,
};

static ssize_t device_attr_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count,
		int (*fn)(struct max77693_charger *, unsigned long))
{
	struct max77693_charger *chg = dev_get_drvdata(dev);
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		return ret;

	ret = fn(chg, val);
	if (ret)
		return ret;

	return count;
}

static ssize_t fast_charge_timer_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct max77693_charger *chg = dev_get_drvdata(dev);
	unsigned int data, val;
	int ret;

	ret = regmap_read(chg->max77693->regmap, MAX77693_CHG_REG_CHG_CNFG_01,
			&data);
	if (ret < 0)
		return ret;

	data &= CHG_CNFG_01_FCHGTIME_MASK;
	data >>= CHG_CNFG_01_FCHGTIME_SHIFT;
	switch (data) {
	case 0x1 ... 0x7:
		/* Starting from 4 hours, step by 2 hours */
		val = 4 + (data - 1) * 2;
		break;
	case 0x0:
	default:
		val = 0;
		break;
	}

	return sysfs_emit(buf, "%u\n", val);
}

static int max77693_set_fast_charge_timer(struct max77693_charger *chg,
		unsigned long hours)
{
	unsigned int data;

	/*
	 * 0x00 - disable
	 * 0x01 - 4h
	 * 0x02 - 6h
	 * ...
	 * 0x07 - 16h
	 * Round down odd values.
	 */
	switch (hours) {
	case 4 ... 16:
		data = (hours - 4) / 2 + 1;
		break;
	case 0:
		/* Disable */
		data = 0;
		break;
	default:
		return -EINVAL;
	}
	data <<= CHG_CNFG_01_FCHGTIME_SHIFT;

	return regmap_update_bits(chg->max77693->regmap,
			MAX77693_CHG_REG_CHG_CNFG_01,
			CHG_CNFG_01_FCHGTIME_MASK, data);
}

static ssize_t fast_charge_timer_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	return device_attr_store(dev, attr, buf, count,
			max77693_set_fast_charge_timer);
}

static ssize_t top_off_threshold_current_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct max77693_charger *chg = dev_get_drvdata(dev);
	unsigned int data, val;
	int ret;

	ret = regmap_read(chg->max77693->regmap, MAX77693_CHG_REG_CHG_CNFG_03,
			&data);
	if (ret < 0)
		return ret;

	data &= CHG_CNFG_03_TOITH_MASK;
	data >>= CHG_CNFG_03_TOITH_SHIFT;

	if (data <= 0x04)
		val = 100000 + data * 25000;
	else
		val = data * 50000;

	return sysfs_emit(buf, "%u\n", val);
}

static int max77693_set_top_off_threshold_current(struct max77693_charger *chg,
		unsigned long uamp)
{
	unsigned int data;

	if (uamp < 100000 || uamp > 350000)
		return -EINVAL;

	if (uamp <= 200000)
		data = (uamp - 100000) / 25000;
	else
		/* (200000, 350000> */
		data = uamp / 50000;

	data <<= CHG_CNFG_03_TOITH_SHIFT;

	return regmap_update_bits(chg->max77693->regmap,
			MAX77693_CHG_REG_CHG_CNFG_03,
			CHG_CNFG_03_TOITH_MASK, data);
}

static ssize_t top_off_threshold_current_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	return device_attr_store(dev, attr, buf, count,
			max77693_set_top_off_threshold_current);
}

static ssize_t top_off_timer_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct max77693_charger *chg = dev_get_drvdata(dev);
	unsigned int data, val;
	int ret;

	ret = regmap_read(chg->max77693->regmap, MAX77693_CHG_REG_CHG_CNFG_03,
			&data);
	if (ret < 0)
		return ret;

	data &= CHG_CNFG_03_TOTIME_MASK;
	data >>= CHG_CNFG_03_TOTIME_SHIFT;

	val = data * 10;

	return sysfs_emit(buf, "%u\n", val);
}

static int max77693_set_top_off_timer(struct max77693_charger *chg,
		unsigned long minutes)
{
	unsigned int data;

	if (minutes > 70)
		return -EINVAL;

	data = minutes / 10;
	data <<= CHG_CNFG_03_TOTIME_SHIFT;

	return regmap_update_bits(chg->max77693->regmap,
			MAX77693_CHG_REG_CHG_CNFG_03,
			CHG_CNFG_03_TOTIME_MASK, data);
}

static ssize_t top_off_timer_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	return device_attr_store(dev, attr, buf, count,
			max77693_set_top_off_timer);
}

static DEVICE_ATTR_RW(fast_charge_timer);
static DEVICE_ATTR_RW(top_off_threshold_current);
static DEVICE_ATTR_RW(top_off_timer);

static int max77693_set_constant_volt(struct max77693_charger *chg,
		unsigned int uvolt)
{
	unsigned int data;

	/*
	 * 0x00 - 3.650 V
	 * 0x01 - 3.675 V
	 * ...
	 * 0x1b - 4.325 V
	 * 0x1c - 4.340 V
	 * 0x1d - 4.350 V
	 * 0x1e - 4.375 V
	 * 0x1f - 4.400 V
	 */
	if (uvolt >= 3650000 && uvolt < 4340000)
		data = (uvolt - 3650000) / 25000;
	else if (uvolt >= 4340000 && uvolt < 4350000)
		data = 0x1c;
	else if (uvolt >= 4350000 && uvolt <= 4400000)
		data = 0x1d + (uvolt - 4350000) / 25000;
	else {
		dev_err(chg->dev, "Wrong value for charging constant voltage\n");
		return -EINVAL;
	}

	data <<= CHG_CNFG_04_CHGCVPRM_SHIFT;

	dev_dbg(chg->dev, "Charging constant voltage: %u (0x%x)\n", uvolt,
			data);

	return regmap_update_bits(chg->max77693->regmap,
			MAX77693_CHG_REG_CHG_CNFG_04,
			CHG_CNFG_04_CHGCVPRM_MASK, data);
}

static int max77693_set_min_system_volt(struct max77693_charger *chg,
		unsigned int uvolt)
{
	unsigned int data;

	if (uvolt < 3000000 || uvolt > 3700000) {
		dev_err(chg->dev, "Wrong value for minimum system regulation voltage\n");
		return -EINVAL;
	}

	data = (uvolt - 3000000) / 100000;

	data <<= CHG_CNFG_04_MINVSYS_SHIFT;

	dev_dbg(chg->dev, "Minimum system regulation voltage: %u (0x%x)\n",
			uvolt, data);

	return regmap_update_bits(chg->max77693->regmap,
			MAX77693_CHG_REG_CHG_CNFG_04,
			CHG_CNFG_04_MINVSYS_MASK, data);
}

static int max77693_set_thermal_regulation_temp(struct max77693_charger *chg,
		unsigned int cels)
{
	unsigned int data;

	switch (cels) {
	case 70:
	case 85:
	case 100:
	case 115:
		data = (cels - 70) / 15;
		break;
	default:
		dev_err(chg->dev, "Wrong value for thermal regulation loop temperature\n");
		return -EINVAL;
	}

	data <<= CHG_CNFG_07_REGTEMP_SHIFT;

	dev_dbg(chg->dev, "Thermal regulation loop temperature: %u (0x%x)\n",
			cels, data);

	return regmap_update_bits(chg->max77693->regmap,
			MAX77693_CHG_REG_CHG_CNFG_07,
			CHG_CNFG_07_REGTEMP_MASK, data);
}

static int max77693_set_batttery_overcurrent(struct max77693_charger *chg,
		unsigned int uamp)
{
	unsigned int data;

	if (uamp && (uamp < 2000000 || uamp > 3500000)) {
		dev_err(chg->dev, "Wrong value for battery overcurrent\n");
		return -EINVAL;
	}

	if (uamp)
		data = ((uamp - 2000000) / 250000) + 1;
	else
		data = 0; /* disable */

	data <<= CHG_CNFG_12_B2SOVRC_SHIFT;

	dev_dbg(chg->dev, "Battery overcurrent: %u (0x%x)\n", uamp, data);

	return regmap_update_bits(chg->max77693->regmap,
			MAX77693_CHG_REG_CHG_CNFG_12,
			CHG_CNFG_12_B2SOVRC_MASK, data);
}

static int max77693_set_charge_input_threshold_volt(struct max77693_charger *chg,
		unsigned int uvolt)
{
	unsigned int data;

	switch (uvolt) {
	case 4300000:
		data = 0x0;
		break;
	case 4700000:
	case 4800000:
	case 4900000:
		data = ((uvolt - 4700000) / 100000) + 1;
		break;
	default:
		dev_err(chg->dev, "Wrong value for charge input voltage regulation threshold\n");
		return -EINVAL;
	}

	data <<= CHG_CNFG_12_VCHGINREG_SHIFT;

	dev_dbg(chg->dev, "Charge input voltage regulation threshold: %u (0x%x)\n",
			uvolt, data);

	return regmap_update_bits(chg->max77693->regmap,
			MAX77693_CHG_REG_CHG_CNFG_12,
			CHG_CNFG_12_VCHGINREG_MASK, data);
}

/*
 * Sets charger registers to proper and safe default values.
 */
static int max77693_reg_init(struct max77693_charger *chg)
{
	int ret;
	unsigned int data;

	/* Unlock charger register protection */
	data = (0x3 << CHG_CNFG_06_CHGPROT_SHIFT);
	ret = regmap_update_bits(chg->max77693->regmap,
				MAX77693_CHG_REG_CHG_CNFG_06,
				CHG_CNFG_06_CHGPROT_MASK, data);
	if (ret) {
		dev_err(chg->dev, "Error unlocking registers: %d\n", ret);
		return ret;
	}

	ret = max77693_set_fast_charge_timer(chg, DEFAULT_FAST_CHARGE_TIMER);
	if (ret)
		return ret;

	ret = max77693_set_top_off_threshold_current(chg,
			DEFAULT_TOP_OFF_THRESHOLD_CURRENT);
	if (ret)
		return ret;

	ret = max77693_set_top_off_timer(chg, DEFAULT_TOP_OFF_TIMER);
	if (ret)
		return ret;

	ret = max77693_set_constant_volt(chg, chg->constant_volt);
	if (ret)
		return ret;

	ret = max77693_set_min_system_volt(chg, chg->min_system_volt);
	if (ret)
		return ret;

	ret = max77693_set_thermal_regulation_temp(chg,
			chg->thermal_regulation_temp);
	if (ret)
		return ret;

	ret = max77693_set_batttery_overcurrent(chg, chg->batttery_overcurrent);
	if (ret)
		return ret;

	return max77693_set_charge_input_threshold_volt(chg,
			chg->charge_input_threshold_volt);
}

#ifdef CONFIG_OF
static int max77693_dt_init(struct device *dev, struct max77693_charger *chg)
{
	struct device_node *np = dev->of_node;

	if (!np) {
		dev_err(dev, "no charger OF node\n");
		return -EINVAL;
	}

	if (of_property_read_u32(np, "maxim,constant-microvolt",
			&chg->constant_volt))
		chg->constant_volt = DEFAULT_CONSTANT_VOLT;

	if (of_property_read_u32(np, "maxim,min-system-microvolt",
			&chg->min_system_volt))
		chg->min_system_volt = DEFAULT_MIN_SYSTEM_VOLT;

	if (of_property_read_u32(np, "maxim,thermal-regulation-celsius",
			&chg->thermal_regulation_temp))
		chg->thermal_regulation_temp = DEFAULT_THERMAL_REGULATION_TEMP;

	if (of_property_read_u32(np, "maxim,battery-overcurrent-microamp",
			&chg->batttery_overcurrent))
		chg->batttery_overcurrent = DEFAULT_BATTERY_OVERCURRENT;

	if (of_property_read_u32(np, "maxim,charge-input-threshold-microvolt",
			&chg->charge_input_threshold_volt))
		chg->charge_input_threshold_volt =
			DEFAULT_CHARGER_INPUT_THRESHOLD_VOLT;

	return 0;
}
#else /* CONFIG_OF */
static int max77693_dt_init(struct device *dev, struct max77693_charger *chg)
{
	return 0;
}
#endif /* CONFIG_OF */

static int max77693_charger_probe(struct platform_device *pdev)
{
	struct max77693_charger *chg;
	struct power_supply_config psy_cfg = {};
	struct max77693_dev *max77693 = dev_get_drvdata(pdev->dev.parent);
	int ret;

	chg = devm_kzalloc(&pdev->dev, sizeof(*chg), GFP_KERNEL);
	if (!chg)
		return -ENOMEM;

	platform_set_drvdata(pdev, chg);
	chg->dev = &pdev->dev;
	chg->max77693 = max77693;

	ret = max77693_dt_init(&pdev->dev, chg);
	if (ret)
		return ret;

	ret = max77693_reg_init(chg);
	if (ret)
		return ret;

	psy_cfg.drv_data = chg;

	ret = device_create_file(&pdev->dev, &dev_attr_fast_charge_timer);
	if (ret) {
		dev_err(&pdev->dev, "failed: create fast charge timer sysfs entry\n");
		goto err;
	}

	ret = device_create_file(&pdev->dev,
			&dev_attr_top_off_threshold_current);
	if (ret) {
		dev_err(&pdev->dev, "failed: create top off current sysfs entry\n");
		goto err;
	}

	ret = device_create_file(&pdev->dev, &dev_attr_top_off_timer);
	if (ret) {
		dev_err(&pdev->dev, "failed: create top off timer sysfs entry\n");
		goto err;
	}

	chg->charger = devm_power_supply_register(&pdev->dev,
						  &max77693_charger_desc,
						  &psy_cfg);
	if (IS_ERR(chg->charger)) {
		dev_err(&pdev->dev, "failed: power supply register\n");
		ret = PTR_ERR(chg->charger);
		goto err;
	}

	return 0;

err:
	device_remove_file(&pdev->dev, &dev_attr_top_off_timer);
	device_remove_file(&pdev->dev, &dev_attr_top_off_threshold_current);
	device_remove_file(&pdev->dev, &dev_attr_fast_charge_timer);

	return ret;
}

static void max77693_charger_remove(struct platform_device *pdev)
{
	device_remove_file(&pdev->dev, &dev_attr_top_off_timer);
	device_remove_file(&pdev->dev, &dev_attr_top_off_threshold_current);
	device_remove_file(&pdev->dev, &dev_attr_fast_charge_timer);
}

static const struct platform_device_id max77693_charger_id[] = {
	{ "max77693-charger", 0, },
	{ }
};
MODULE_DEVICE_TABLE(platform, max77693_charger_id);

static struct platform_driver max77693_charger_driver = {
	.driver = {
		.name	= "max77693-charger",
	},
	.probe		= max77693_charger_probe,
	.remove		= max77693_charger_remove,
	.id_table	= max77693_charger_id,
};
module_platform_driver(max77693_charger_driver);

MODULE_AUTHOR("Krzysztof Kozlowski <krzk@kernel.org>");
MODULE_DESCRIPTION("Maxim 77693 charger driver");
MODULE_LICENSE("GPL");
