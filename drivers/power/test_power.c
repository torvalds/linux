/*
 * Power supply driver for testing.
 *
 * Copyright 2010  Anton Vorontsov <cbouatmailru@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/power_supply.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/vermagic.h>

static int test_power_ac_online = 1;
static int test_power_battery_status = POWER_SUPPLY_STATUS_CHARGING;

static int test_power_get_ac_property(struct power_supply *psy,
				      enum power_supply_property psp,
				      union power_supply_propval *val)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = test_power_ac_online;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int test_power_get_battery_property(struct power_supply *psy,
					   enum power_supply_property psp,
					   union power_supply_propval *val)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = "Test battery";
		break;
	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = "Linux";
		break;
	case POWER_SUPPLY_PROP_SERIAL_NUMBER:
		val->strval = UTS_RELEASE;
		break;
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = test_power_battery_status;
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		val->intval = POWER_SUPPLY_CHARGE_TYPE_FAST;
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = POWER_SUPPLY_HEALTH_GOOD;
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;
	case POWER_SUPPLY_PROP_CAPACITY_LEVEL:
		val->intval = POWER_SUPPLY_CAPACITY_LEVEL_NORMAL;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = 50;
		break;
	case POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG:
	case POWER_SUPPLY_PROP_TIME_TO_FULL_NOW:
		val->intval = 3600;
		break;
	default:
		pr_info("%s: some properties deliberately report errors.\n",
			__func__);
		return -EINVAL;
	}
	return 0;
}

static enum power_supply_property test_power_ac_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
};

static enum power_supply_property test_power_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_EMPTY,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG,
	POWER_SUPPLY_PROP_TIME_TO_FULL_NOW,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_SERIAL_NUMBER,
};

static char *test_power_ac_supplied_to[] = {
	"test_battery",
};

static struct power_supply test_power_supplies[] = {
	{
		.name = "test_ac",
		.type = POWER_SUPPLY_TYPE_MAINS,
		.supplied_to = test_power_ac_supplied_to,
		.num_supplicants = ARRAY_SIZE(test_power_ac_supplied_to),
		.properties = test_power_ac_props,
		.num_properties = ARRAY_SIZE(test_power_ac_props),
		.get_property = test_power_get_ac_property,
	}, {
		.name = "test_battery",
		.type = POWER_SUPPLY_TYPE_BATTERY,
		.properties = test_power_battery_props,
		.num_properties = ARRAY_SIZE(test_power_battery_props),
		.get_property = test_power_get_battery_property,
	},
};

static int __init test_power_init(void)
{
	int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(test_power_supplies); i++) {
		ret = power_supply_register(NULL, &test_power_supplies[i]);
		if (ret) {
			pr_err("%s: failed to register %s\n", __func__,
				test_power_supplies[i].name);
			goto failed;
		}
	}

	return 0;
failed:
	while (--i >= 0)
		power_supply_unregister(&test_power_supplies[i]);
	return ret;
}
module_init(test_power_init);

static void __exit test_power_exit(void)
{
	int i;

	/* Let's see how we handle changes... */
	test_power_ac_online = 0;
	test_power_battery_status = POWER_SUPPLY_STATUS_DISCHARGING;
	for (i = 0; i < ARRAY_SIZE(test_power_supplies); i++)
		power_supply_changed(&test_power_supplies[i]);
	pr_info("%s: 'changed' event sent, sleeping for 10 seconds...\n",
		__func__);
	ssleep(10);

	for (i = 0; i < ARRAY_SIZE(test_power_supplies); i++)
		power_supply_unregister(&test_power_supplies[i]);
}
module_exit(test_power_exit);

MODULE_DESCRIPTION("Power supply driver for testing");
MODULE_AUTHOR("Anton Vorontsov <cbouatmailru@gmail.com>");
MODULE_LICENSE("GPL");
