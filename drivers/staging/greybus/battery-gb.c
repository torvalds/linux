/*
 * Battery driver for a Greybus module.
 *
 * Copyright 2014 Google Inc.
 *
 * Released under the GPLv2 only.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/power_supply.h>
#include "greybus.h"

struct gb_battery {
	struct power_supply bat;
	// FIXME
	// we will want to keep the battery stats in here as we will be getting
	// updates from the SVC "on the fly" so we don't have to always go ask
	// the battery for some information.  Hopefully...
	struct greybus_module *gmod;
};
#define to_gb_battery(x) container_of(x, struct gb_battery, bat)

static const struct greybus_module_id id_table[] = {
	{ GREYBUS_DEVICE(0x42, 0x42) },	/* make shit up */
	{ },	/* terminating NULL entry */
};

static int get_status(struct gb_battery *gb)
{
	// FIXME!!!
	return 0;
}

static int get_capacity(struct gb_battery *gb)
{
	// FIXME!!!
	return 0;
}

static int get_temp(struct gb_battery *gb)
{
	// FIXME!!!
	return 0;
}

static int get_voltage(struct gb_battery *gb)
{
	// FIXME!!!
	return 0;
}

static int get_property(struct power_supply *b,
			enum power_supply_property psp,
			union power_supply_propval *val)
{
	struct gb_battery *gb = to_gb_battery(b);

	switch (psp) {
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		// FIXME - guess!
		val->intval = POWER_SUPPLY_TECHNOLOGY_NiMH;
		break;

	case POWER_SUPPLY_PROP_STATUS:
		val->intval = get_status(gb);
		break;

	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		val->intval = 4700000;	// FIXME - guess???
		break;

	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = get_capacity(gb);
		break;

	case POWER_SUPPLY_PROP_TEMP:
		val->intval = get_temp(gb);
		break;

	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = get_voltage(gb);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

// FIXME - verify this list, odds are some can be removed and others added.
static enum power_supply_property battery_props[] = {
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
};

int gb_battery_probe(struct greybus_module *gmod,
		     const struct greybus_module_id *id)
{
	struct gb_battery *gb;
	struct power_supply *b;
	int retval;

	gb = kzalloc(sizeof(*gb), GFP_KERNEL);
	if (!gb)
		return -ENOMEM;

	b = &gb->bat;
	// FIXME - get a better (i.e. unique) name
	// FIXME - anything else needs to be set?
	b->name			= "gb_battery";
	b->type			= POWER_SUPPLY_TYPE_BATTERY,
	b->properties		= battery_props,
	b->num_properties	= ARRAY_SIZE(battery_props),
	b->get_property		= get_property,

	retval = power_supply_register(&gmod->dev, b);
	if (retval) {
		kfree(gb);
		return retval;
	}
	gmod->gb_battery = gb;

	return 0;
}

void gb_battery_disconnect(struct greybus_module *gmod)
{
	struct gb_battery *gb;

	gb = gmod->gb_battery;

	power_supply_unregister(&gb->bat);

	kfree(gb);
}

#if 0
static struct greybus_driver battery_gb_driver = {
	.probe =	gb_battery_probe,
	.disconnect =	gb_battery_disconnect,
	.id_table =	id_table,
};

module_greybus_driver(battery_gb_driver);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Greg Kroah-Hartman <gregkh@linuxfoundation.org>");
#endif
