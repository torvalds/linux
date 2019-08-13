// SPDX-License-Identifier: GPL-2.0
/*
 * Charging control driver for the Wilco EC
 *
 * Copyright 2019 Google LLC
 *
 * See Documentation/ABI/testing/sysfs-class-power and
 * Documentation/ABI/testing/sysfs-class-power-wilco for userspace interface
 * and other info.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/platform_data/wilco-ec.h>
#include <linux/power_supply.h>

#define DRV_NAME "wilco-charger"

/* Property IDs and related EC constants */
#define PID_CHARGE_MODE		0x0710
#define PID_CHARGE_LOWER_LIMIT	0x0711
#define PID_CHARGE_UPPER_LIMIT	0x0712

enum charge_mode {
	CHARGE_MODE_STD = 1,	/* Used for Standard */
	CHARGE_MODE_EXP = 2,	/* Express Charge, used for Fast */
	CHARGE_MODE_AC = 3,	/* Mostly AC use, used for Trickle */
	CHARGE_MODE_AUTO = 4,	/* Used for Adaptive */
	CHARGE_MODE_CUSTOM = 5,	/* Used for Custom */
};

#define CHARGE_LOWER_LIMIT_MIN	50
#define CHARGE_LOWER_LIMIT_MAX	95
#define CHARGE_UPPER_LIMIT_MIN	55
#define CHARGE_UPPER_LIMIT_MAX	100

/* Convert from POWER_SUPPLY_PROP_CHARGE_TYPE value to the EC's charge mode */
static int psp_val_to_charge_mode(int psp_val)
{
	switch (psp_val) {
	case POWER_SUPPLY_CHARGE_TYPE_TRICKLE:
		return CHARGE_MODE_AC;
	case POWER_SUPPLY_CHARGE_TYPE_FAST:
		return CHARGE_MODE_EXP;
	case POWER_SUPPLY_CHARGE_TYPE_STANDARD:
		return CHARGE_MODE_STD;
	case POWER_SUPPLY_CHARGE_TYPE_ADAPTIVE:
		return CHARGE_MODE_AUTO;
	case POWER_SUPPLY_CHARGE_TYPE_CUSTOM:
		return CHARGE_MODE_CUSTOM;
	default:
		return -EINVAL;
	}
}

/* Convert from EC's charge mode to POWER_SUPPLY_PROP_CHARGE_TYPE value */
static int charge_mode_to_psp_val(enum charge_mode mode)
{
	switch (mode) {
	case CHARGE_MODE_AC:
		return POWER_SUPPLY_CHARGE_TYPE_TRICKLE;
	case CHARGE_MODE_EXP:
		return POWER_SUPPLY_CHARGE_TYPE_FAST;
	case CHARGE_MODE_STD:
		return POWER_SUPPLY_CHARGE_TYPE_STANDARD;
	case CHARGE_MODE_AUTO:
		return POWER_SUPPLY_CHARGE_TYPE_ADAPTIVE;
	case CHARGE_MODE_CUSTOM:
		return POWER_SUPPLY_CHARGE_TYPE_CUSTOM;
	default:
		return -EINVAL;
	}
}

static enum power_supply_property wilco_charge_props[] = {
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_START_THRESHOLD,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_END_THRESHOLD,
};

static int wilco_charge_get_property(struct power_supply *psy,
				     enum power_supply_property psp,
				     union power_supply_propval *val)
{
	struct wilco_ec_device *ec = power_supply_get_drvdata(psy);
	u32 property_id;
	int ret;
	u8 raw;

	switch (psp) {
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		property_id = PID_CHARGE_MODE;
		break;
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_START_THRESHOLD:
		property_id = PID_CHARGE_LOWER_LIMIT;
		break;
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_END_THRESHOLD:
		property_id = PID_CHARGE_UPPER_LIMIT;
		break;
	default:
		return -EINVAL;
	}

	ret = wilco_ec_get_byte_property(ec, property_id, &raw);
	if (ret < 0)
		return ret;
	if (property_id == PID_CHARGE_MODE) {
		ret = charge_mode_to_psp_val(raw);
		if (ret < 0)
			return -EBADMSG;
		raw = ret;
	}
	val->intval = raw;

	return 0;
}

static int wilco_charge_set_property(struct power_supply *psy,
				     enum power_supply_property psp,
				     const union power_supply_propval *val)
{
	struct wilco_ec_device *ec = power_supply_get_drvdata(psy);
	int mode;

	switch (psp) {
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		mode = psp_val_to_charge_mode(val->intval);
		if (mode < 0)
			return -EINVAL;
		return wilco_ec_set_byte_property(ec, PID_CHARGE_MODE, mode);
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_START_THRESHOLD:
		if (val->intval < CHARGE_LOWER_LIMIT_MIN ||
		    val->intval > CHARGE_LOWER_LIMIT_MAX)
			return -EINVAL;
		return wilco_ec_set_byte_property(ec, PID_CHARGE_LOWER_LIMIT,
						  val->intval);
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_END_THRESHOLD:
		if (val->intval < CHARGE_UPPER_LIMIT_MIN ||
		    val->intval > CHARGE_UPPER_LIMIT_MAX)
			return -EINVAL;
		return wilco_ec_set_byte_property(ec, PID_CHARGE_UPPER_LIMIT,
						  val->intval);
	default:
		return -EINVAL;
	}
}

static int wilco_charge_property_is_writeable(struct power_supply *psy,
					      enum power_supply_property psp)
{
	return 1;
}

static const struct power_supply_desc wilco_ps_desc = {
	.properties		= wilco_charge_props,
	.num_properties		= ARRAY_SIZE(wilco_charge_props),
	.get_property		= wilco_charge_get_property,
	.set_property		= wilco_charge_set_property,
	.property_is_writeable	= wilco_charge_property_is_writeable,
	.name			= DRV_NAME,
	.type			= POWER_SUPPLY_TYPE_MAINS,
};

static int wilco_charge_probe(struct platform_device *pdev)
{
	struct wilco_ec_device *ec = dev_get_drvdata(pdev->dev.parent);
	struct power_supply_config psy_cfg = {};
	struct power_supply *psy;

	psy_cfg.drv_data = ec;
	psy = devm_power_supply_register(&pdev->dev, &wilco_ps_desc, &psy_cfg);

	return PTR_ERR_OR_ZERO(psy);
}

static struct platform_driver wilco_charge_driver = {
	.probe	= wilco_charge_probe,
	.driver = {
		.name = DRV_NAME,
	}
};
module_platform_driver(wilco_charge_driver);

MODULE_ALIAS("platform:" DRV_NAME);
MODULE_AUTHOR("Nick Crews <ncrews@chromium.org>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Wilco EC charge control driver");
