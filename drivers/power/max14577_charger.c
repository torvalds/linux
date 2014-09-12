/*
 * max14577_charger.c - Battery charger driver for the Maxim 14577/77836
 *
 * Copyright (C) 2013,2014 Samsung Electronics
 * Krzysztof Kozlowski <k.kozlowski@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/mfd/max14577-private.h>

struct max14577_charger {
	struct device *dev;
	struct max14577	*max14577;
	struct power_supply	charger;

	unsigned int		charging_state;
	unsigned int		battery_state;
};

/*
 * Helper function for mapping values of STATUS2/CHGTYP register on max14577
 * and max77836 chipsets to enum maxim_muic_charger_type.
 */
static enum max14577_muic_charger_type maxim_get_charger_type(
		enum maxim_device_type dev_type, u8 val) {
	switch (val) {
	case MAX14577_CHARGER_TYPE_NONE:
	case MAX14577_CHARGER_TYPE_USB:
	case MAX14577_CHARGER_TYPE_DOWNSTREAM_PORT:
	case MAX14577_CHARGER_TYPE_DEDICATED_CHG:
	case MAX14577_CHARGER_TYPE_SPECIAL_500MA:
	case MAX14577_CHARGER_TYPE_SPECIAL_1A:
		return val;
	case MAX14577_CHARGER_TYPE_DEAD_BATTERY:
	case MAX14577_CHARGER_TYPE_RESERVED:
		if (dev_type == MAXIM_DEVICE_TYPE_MAX77836)
			val |= 0x8;
		return val;
	default:
		WARN_ONCE(1, "max14577: Unsupported chgtyp register value 0x%02x", val);
		return val;
	}
}

static int max14577_get_charger_state(struct max14577_charger *chg)
{
	struct regmap *rmap = chg->max14577->regmap;
	int state = POWER_SUPPLY_STATUS_DISCHARGING;
	u8 reg_data;

	/*
	 * Charging occurs only if:
	 *  - CHGCTRL2/MBCHOSTEN == 1
	 *  - STATUS2/CGMBC == 1
	 *
	 * TODO:
	 *  - handle FULL after Top-off timer (EOC register may be off
	 *    and the charger won't be charging although MBCHOSTEN is on)
	 *  - handle properly dead-battery charging (respect timer)
	 *  - handle timers (fast-charge and prequal) /MBCCHGERR/
	 */
	max14577_read_reg(rmap, MAX14577_CHG_REG_CHG_CTRL2, &reg_data);
	if ((reg_data & CHGCTRL2_MBCHOSTEN_MASK) == 0)
		goto state_set;

	max14577_read_reg(rmap, MAX14577_CHG_REG_STATUS3, &reg_data);
	if (reg_data & STATUS3_CGMBC_MASK) {
		/* Charger or USB-cable is connected */
		if (reg_data & STATUS3_EOC_MASK)
			state = POWER_SUPPLY_STATUS_FULL;
		else
			state = POWER_SUPPLY_STATUS_CHARGING;
		goto state_set;
	}

state_set:
	chg->charging_state = state;
	return state;
}

/*
 * Supported charge types:
 *  - POWER_SUPPLY_CHARGE_TYPE_NONE
 *  - POWER_SUPPLY_CHARGE_TYPE_FAST
 */
static int max14577_get_charge_type(struct max14577_charger *chg)
{
	/*
	 * TODO: CHARGE_TYPE_TRICKLE (VCHGR_RC or EOC)?
	 * As spec says:
	 * [after reaching EOC interrupt]
	 * "When the battery is fully charged, the 30-minute (typ)
	 *  top-off timer starts. The device continues to trickle
	 *  charge the battery until the top-off timer runs out."
	 */
	if (max14577_get_charger_state(chg) == POWER_SUPPLY_STATUS_CHARGING)
		return POWER_SUPPLY_CHARGE_TYPE_FAST;
	return POWER_SUPPLY_CHARGE_TYPE_NONE;
}

static int max14577_get_online(struct max14577_charger *chg)
{
	struct regmap *rmap = chg->max14577->regmap;
	u8 reg_data;
	enum max14577_muic_charger_type chg_type;

	max14577_read_reg(rmap, MAX14577_MUIC_REG_STATUS2, &reg_data);
	reg_data = ((reg_data & STATUS2_CHGTYP_MASK) >> STATUS2_CHGTYP_SHIFT);
	chg_type = maxim_get_charger_type(chg->max14577->dev_type, reg_data);
	switch (chg_type) {
	case MAX14577_CHARGER_TYPE_USB:
	case MAX14577_CHARGER_TYPE_DEDICATED_CHG:
	case MAX14577_CHARGER_TYPE_SPECIAL_500MA:
	case MAX14577_CHARGER_TYPE_SPECIAL_1A:
	case MAX14577_CHARGER_TYPE_DEAD_BATTERY:
	case MAX77836_CHARGER_TYPE_SPECIAL_BIAS:
		return 1;
	case MAX14577_CHARGER_TYPE_NONE:
	case MAX14577_CHARGER_TYPE_DOWNSTREAM_PORT:
	case MAX14577_CHARGER_TYPE_RESERVED:
	case MAX77836_CHARGER_TYPE_RESERVED:
	default:
		return 0;
	}
}

/*
 * Supported health statuses:
 *  - POWER_SUPPLY_HEALTH_DEAD
 *  - POWER_SUPPLY_HEALTH_OVERVOLTAGE
 *  - POWER_SUPPLY_HEALTH_GOOD
 */
static int max14577_get_battery_health(struct max14577_charger *chg)
{
	struct regmap *rmap = chg->max14577->regmap;
	int state = POWER_SUPPLY_HEALTH_GOOD;
	u8 reg_data;
	enum max14577_muic_charger_type chg_type;

	max14577_read_reg(rmap, MAX14577_MUIC_REG_STATUS2, &reg_data);
	reg_data = ((reg_data & STATUS2_CHGTYP_MASK) >> STATUS2_CHGTYP_SHIFT);
	chg_type = maxim_get_charger_type(chg->max14577->dev_type, reg_data);
	if (chg_type == MAX14577_CHARGER_TYPE_DEAD_BATTERY) {
		state = POWER_SUPPLY_HEALTH_DEAD;
		goto state_set;
	}

	max14577_read_reg(rmap, MAX14577_CHG_REG_STATUS3, &reg_data);
	if (reg_data & STATUS3_OVP_MASK) {
		state = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
		goto state_set;
	}

state_set:
	chg->battery_state = state;
	return state;
}

/*
 * Always returns 1.
 * The max14577 chip doesn't report any status of battery presence.
 * Lets assume that it will always be used with some battery.
 */
static int max14577_get_present(struct max14577_charger *chg)
{
	return 1;
}

/*
 * Sets charger registers to proper and safe default values.
 * Some of these values are equal to defaults in MAX14577E
 * data sheet but there are minor differences.
 */
static void max14577_charger_reg_init(struct max14577_charger *chg)
{
	struct regmap *rmap = chg->max14577->regmap;
	u8 reg_data;

	/*
	 * Charger-Type Manual Detection, default off (set CHGTYPMAN to 0)
	 * Charger-Detection Enable, default on (set CHGDETEN to 1)
	 * Combined mask of CHGDETEN and CHGTYPMAN will zero the CHGTYPMAN bit
	 */
	reg_data = 0x1 << CDETCTRL1_CHGDETEN_SHIFT;
	max14577_update_reg(rmap, MAX14577_REG_CDETCTRL1,
			CDETCTRL1_CHGDETEN_MASK | CDETCTRL1_CHGTYPMAN_MASK,
			reg_data);

	/* Battery Fast-Charge Timer, set to: 6hrs */
	reg_data = 0x3 << CHGCTRL1_TCHW_SHIFT;
	max14577_write_reg(rmap, MAX14577_REG_CHGCTRL1, reg_data);

	/*
	 * Wall-Adapter Rapid Charge, default on
	 * Battery-Charger, default on
	 */
	reg_data = 0x1 << CHGCTRL2_VCHGR_RC_SHIFT;
	reg_data |= 0x1 << CHGCTRL2_MBCHOSTEN_SHIFT;
	max14577_write_reg(rmap, MAX14577_REG_CHGCTRL2, reg_data);

	/* Battery-Charger Constant Voltage (CV) Mode, set to: 4.35V */
	reg_data = 0xf << CHGCTRL3_MBCCVWRC_SHIFT;
	max14577_write_reg(rmap, MAX14577_REG_CHGCTRL3, reg_data);

	/*
	 * Fast Battery-Charge Current Low,
	 * default 200-950mA (max14577) / 100-475mA (max77836)
	 *
	 * Fast Battery-Charge Current High,
	 * set to 450mA (max14577) / 225mA (max77836)
	 */
	reg_data = 0x1 << CHGCTRL4_MBCICHWRCL_SHIFT;
	reg_data |= 0x5 << CHGCTRL4_MBCICHWRCH_SHIFT;
	max14577_write_reg(rmap, MAX14577_REG_CHGCTRL4, reg_data);

	/* End-of-Charge Current, set to 50mA (max14577) / 7.5mA (max77836) */
	reg_data = 0x0 << CHGCTRL5_EOCS_SHIFT;
	max14577_write_reg(rmap, MAX14577_REG_CHGCTRL5, reg_data);

	/* Auto Charging Stop, default off */
	reg_data = 0x0 << CHGCTRL6_AUTOSTOP_SHIFT;
	max14577_write_reg(rmap, MAX14577_REG_CHGCTRL6, reg_data);

	/* Overvoltage-Protection Threshold, set to 6.5V */
	reg_data = 0x2 << CHGCTRL7_OTPCGHCVS_SHIFT;
	max14577_write_reg(rmap, MAX14577_REG_CHGCTRL7, reg_data);
}

/* Support property from charger */
static enum power_supply_property max14577_charger_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_MANUFACTURER,
};

static const char * const model_names[] = {
	[MAXIM_DEVICE_TYPE_UNKNOWN]	= "MAX14577-like",
	[MAXIM_DEVICE_TYPE_MAX14577]	= "MAX14577",
	[MAXIM_DEVICE_TYPE_MAX77836]	= "MAX77836",
};
static const char *manufacturer = "Maxim Integrated";

static int max14577_charger_get_property(struct power_supply *psy,
			    enum power_supply_property psp,
			    union power_supply_propval *val)
{
	struct max14577_charger *chg = container_of(psy,
						  struct max14577_charger,
						  charger);
	int ret = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = max14577_get_charger_state(chg);
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		val->intval = max14577_get_charge_type(chg);
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = max14577_get_battery_health(chg);
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = max14577_get_present(chg);
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = max14577_get_online(chg);
		break;
	case POWER_SUPPLY_PROP_MODEL_NAME:
		BUILD_BUG_ON(ARRAY_SIZE(model_names) != MAXIM_DEVICE_TYPE_NUM);
		val->strval = model_names[chg->max14577->dev_type];
		break;
	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = manufacturer;
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

static int max14577_charger_probe(struct platform_device *pdev)
{
	struct max14577_charger *chg;
	struct max14577 *max14577 = dev_get_drvdata(pdev->dev.parent);
	int ret;

	chg = devm_kzalloc(&pdev->dev, sizeof(*chg), GFP_KERNEL);
	if (!chg)
		return -ENOMEM;

	platform_set_drvdata(pdev, chg);
	chg->dev = &pdev->dev;
	chg->max14577 = max14577;

	max14577_charger_reg_init(chg);

	chg->charger.name = "max14577-charger",
	chg->charger.type = POWER_SUPPLY_TYPE_BATTERY,
	chg->charger.properties = max14577_charger_props,
	chg->charger.num_properties = ARRAY_SIZE(max14577_charger_props),
	chg->charger.get_property = max14577_charger_get_property,

	ret = power_supply_register(&pdev->dev, &chg->charger);
	if (ret) {
		dev_err(&pdev->dev, "failed: power supply register\n");
		return ret;
	}

	return 0;
}

static int max14577_charger_remove(struct platform_device *pdev)
{
	struct max14577_charger *chg = platform_get_drvdata(pdev);

	power_supply_unregister(&chg->charger);

	return 0;
}

static const struct platform_device_id max14577_charger_id[] = {
	{ "max14577-charger", MAXIM_DEVICE_TYPE_MAX14577, },
	{ "max77836-charger", MAXIM_DEVICE_TYPE_MAX77836, },
	{ }
};
MODULE_DEVICE_TABLE(platform, max14577_charger_id);

static struct platform_driver max14577_charger_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "max14577-charger",
	},
	.probe		= max14577_charger_probe,
	.remove		= max14577_charger_remove,
	.id_table	= max14577_charger_id,
};
module_platform_driver(max14577_charger_driver);

MODULE_AUTHOR("Krzysztof Kozlowski <k.kozlowski@samsung.com>");
MODULE_DESCRIPTION("Maxim 14577/77836 charger driver");
MODULE_LICENSE("GPL");
