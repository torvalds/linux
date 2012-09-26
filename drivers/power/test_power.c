/*
 * Power supply driver for testing.
 *
 * Copyright 2010  Anton Vorontsov <cbouatmailru@gmail.com>
 *
 * Dynamic module parameter code from the Virtual Battery Driver
 * Copyright (C) 2008 Pylone, Inc.
 * By: Masashi YOKOTA <yokota@pylone.jp>
 * Originally found here:
 * http://downloads.pylone.jp/src/virtual_battery/virtual_battery-0.0.1.tar.bz2
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

static int ac_online			= 1;
static int battery_status		= POWER_SUPPLY_STATUS_DISCHARGING;
static int battery_health		= POWER_SUPPLY_HEALTH_GOOD;
static int battery_present		= 1; /* true */
static int battery_technology		= POWER_SUPPLY_TECHNOLOGY_LION;
static int battery_capacity		= 50;

static int test_power_get_ac_property(struct power_supply *psy,
				      enum power_supply_property psp,
				      union power_supply_propval *val)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = ac_online;
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
		val->intval = battery_status;
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		val->intval = POWER_SUPPLY_CHARGE_TYPE_FAST;
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = battery_health;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = battery_present;
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = battery_technology;
		break;
	case POWER_SUPPLY_PROP_CAPACITY_LEVEL:
		val->intval = POWER_SUPPLY_CAPACITY_LEVEL_NORMAL;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
	case POWER_SUPPLY_PROP_CHARGE_NOW:
		val->intval = battery_capacity;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		val->intval = 100;
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
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_NOW,
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
	ac_online = 0;
	battery_status = POWER_SUPPLY_STATUS_DISCHARGING;
	for (i = 0; i < ARRAY_SIZE(test_power_supplies); i++)
		power_supply_changed(&test_power_supplies[i]);
	pr_info("%s: 'changed' event sent, sleeping for 10 seconds...\n",
		__func__);
	ssleep(10);

	for (i = 0; i < ARRAY_SIZE(test_power_supplies); i++)
		power_supply_unregister(&test_power_supplies[i]);
}
module_exit(test_power_exit);



#define MAX_KEYLENGTH 256
struct battery_property_map {
	int value;
	char const *key;
};

static struct battery_property_map map_ac_online[] = {
	{ 1,  "on"  },
	{ 0,  "off" },
	{ -1, NULL  },
};

static struct battery_property_map map_status[] = {
	{ POWER_SUPPLY_STATUS_CHARGING,     "charging"     },
	{ POWER_SUPPLY_STATUS_DISCHARGING,  "discharging"  },
	{ POWER_SUPPLY_STATUS_NOT_CHARGING, "not-charging" },
	{ POWER_SUPPLY_STATUS_FULL,         "full"         },
	{ -1,                               NULL           },
};

static struct battery_property_map map_health[] = {
	{ POWER_SUPPLY_HEALTH_GOOD,           "good"        },
	{ POWER_SUPPLY_HEALTH_OVERHEAT,       "overheat"    },
	{ POWER_SUPPLY_HEALTH_DEAD,           "dead"        },
	{ POWER_SUPPLY_HEALTH_OVERVOLTAGE,    "overvoltage" },
	{ POWER_SUPPLY_HEALTH_UNSPEC_FAILURE, "failure"     },
	{ -1,                                 NULL          },
};

static struct battery_property_map map_present[] = {
	{ 0,  "false" },
	{ 1,  "true"  },
	{ -1, NULL    },
};

static struct battery_property_map map_technology[] = {
	{ POWER_SUPPLY_TECHNOLOGY_NiMH, "NiMH" },
	{ POWER_SUPPLY_TECHNOLOGY_LION, "LION" },
	{ POWER_SUPPLY_TECHNOLOGY_LIPO, "LIPO" },
	{ POWER_SUPPLY_TECHNOLOGY_LiFe, "LiFe" },
	{ POWER_SUPPLY_TECHNOLOGY_NiCd, "NiCd" },
	{ POWER_SUPPLY_TECHNOLOGY_LiMn, "LiMn" },
	{ -1,				NULL   },
};


static int map_get_value(struct battery_property_map *map, const char *key,
				int def_val)
{
	char buf[MAX_KEYLENGTH];
	int cr;

	strncpy(buf, key, MAX_KEYLENGTH);
	buf[MAX_KEYLENGTH-1] = '\0';

	cr = strnlen(buf, MAX_KEYLENGTH) - 1;
	if (buf[cr] == '\n')
		buf[cr] = '\0';

	while (map->key) {
		if (strncasecmp(map->key, buf, MAX_KEYLENGTH) == 0)
			return map->value;
		map++;
	}

	return def_val;
}


static const char *map_get_key(struct battery_property_map *map, int value,
				const char *def_key)
{
	while (map->key) {
		if (map->value == value)
			return map->key;
		map++;
	}

	return def_key;
}

static int param_set_ac_online(const char *key, const struct kernel_param *kp)
{
	ac_online = map_get_value(map_ac_online, key, ac_online);
	power_supply_changed(&test_power_supplies[0]);
	return 0;
}

static int param_get_ac_online(char *buffer, const struct kernel_param *kp)
{
	strcpy(buffer, map_get_key(map_ac_online, ac_online, "unknown"));
	return strlen(buffer);
}

static int param_set_battery_status(const char *key,
					const struct kernel_param *kp)
{
	battery_status = map_get_value(map_status, key, battery_status);
	power_supply_changed(&test_power_supplies[1]);
	return 0;
}

static int param_get_battery_status(char *buffer, const struct kernel_param *kp)
{
	strcpy(buffer, map_get_key(map_status, battery_status, "unknown"));
	return strlen(buffer);
}

static int param_set_battery_health(const char *key,
					const struct kernel_param *kp)
{
	battery_health = map_get_value(map_health, key, battery_health);
	power_supply_changed(&test_power_supplies[1]);
	return 0;
}

static int param_get_battery_health(char *buffer, const struct kernel_param *kp)
{
	strcpy(buffer, map_get_key(map_health, battery_health, "unknown"));
	return strlen(buffer);
}

static int param_set_battery_present(const char *key,
					const struct kernel_param *kp)
{
	battery_present = map_get_value(map_present, key, battery_present);
	power_supply_changed(&test_power_supplies[0]);
	return 0;
}

static int param_get_battery_present(char *buffer,
					const struct kernel_param *kp)
{
	strcpy(buffer, map_get_key(map_present, battery_present, "unknown"));
	return strlen(buffer);
}

static int param_set_battery_technology(const char *key,
					const struct kernel_param *kp)
{
	battery_technology = map_get_value(map_technology, key,
						battery_technology);
	power_supply_changed(&test_power_supplies[1]);
	return 0;
}

static int param_get_battery_technology(char *buffer,
					const struct kernel_param *kp)
{
	strcpy(buffer,
		map_get_key(map_technology, battery_technology, "unknown"));
	return strlen(buffer);
}

static int param_set_battery_capacity(const char *key,
					const struct kernel_param *kp)
{
	int tmp;

	if (1 != sscanf(key, "%d", &tmp))
		return -EINVAL;

	battery_capacity = tmp;
	power_supply_changed(&test_power_supplies[1]);
	return 0;
}

#define param_get_battery_capacity param_get_int



static struct kernel_param_ops param_ops_ac_online = {
	.set = param_set_ac_online,
	.get = param_get_ac_online,
};

static struct kernel_param_ops param_ops_battery_status = {
	.set = param_set_battery_status,
	.get = param_get_battery_status,
};

static struct kernel_param_ops param_ops_battery_present = {
	.set = param_set_battery_present,
	.get = param_get_battery_present,
};

static struct kernel_param_ops param_ops_battery_technology = {
	.set = param_set_battery_technology,
	.get = param_get_battery_technology,
};

static struct kernel_param_ops param_ops_battery_health = {
	.set = param_set_battery_health,
	.get = param_get_battery_health,
};

static struct kernel_param_ops param_ops_battery_capacity = {
	.set = param_set_battery_capacity,
	.get = param_get_battery_capacity,
};


#define param_check_ac_online(name, p) __param_check(name, p, void);
#define param_check_battery_status(name, p) __param_check(name, p, void);
#define param_check_battery_present(name, p) __param_check(name, p, void);
#define param_check_battery_technology(name, p) __param_check(name, p, void);
#define param_check_battery_health(name, p) __param_check(name, p, void);
#define param_check_battery_capacity(name, p) __param_check(name, p, void);


module_param(ac_online, ac_online, 0644);
MODULE_PARM_DESC(ac_online, "AC charging state <on|off>");

module_param(battery_status, battery_status, 0644);
MODULE_PARM_DESC(battery_status,
	"battery status <charging|discharging|not-charging|full>");

module_param(battery_present, battery_present, 0644);
MODULE_PARM_DESC(battery_present,
	"battery presence state <good|overheat|dead|overvoltage|failure>");

module_param(battery_technology, battery_technology, 0644);
MODULE_PARM_DESC(battery_technology,
	"battery technology <NiMH|LION|LIPO|LiFe|NiCd|LiMn>");

module_param(battery_health, battery_health, 0644);
MODULE_PARM_DESC(battery_health,
	"battery health state <good|overheat|dead|overvoltage|failure>");

module_param(battery_capacity, battery_capacity, 0644);
MODULE_PARM_DESC(battery_capacity, "battery capacity (percentage)");


MODULE_DESCRIPTION("Power supply driver for testing");
MODULE_AUTHOR("Anton Vorontsov <cbouatmailru@gmail.com>");
MODULE_LICENSE("GPL");
