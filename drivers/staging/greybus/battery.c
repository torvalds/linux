/*
 * Battery driver for a Greybus module.
 *
 * Copyright 2014 Google Inc.
 * Copyright 2014 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/power_supply.h>
#include "greybus.h"

struct gb_battery {
	/*
	 * The power supply api changed in 4.1, so handle both the old
	 * and new apis in the same driver for now, until this is merged
	 * upstream, when all of these version checks can be removed.
	 */
#ifdef DRIVER_OWNS_PSY_STRUCT
	struct power_supply bat;
#define to_gb_battery(x) container_of(x, struct gb_battery, bat)
#else
	struct power_supply *bat;
	struct power_supply_desc desc;
#define to_gb_battery(x) power_supply_get_drvdata(x)
#endif
	// FIXME
	// we will want to keep the battery stats in here as we will be getting
	// updates from the SVC "on the fly" so we don't have to always go ask
	// the battery for some information.  Hopefully...
	struct gb_connection *connection;
	u8 version_major;
	u8 version_minor;

};

/* Version of the Greybus battery protocol we support */
#define	GB_BATTERY_VERSION_MAJOR		0x00
#define	GB_BATTERY_VERSION_MINOR		0x01

/* Greybus battery request types */
#define	GB_BATTERY_TYPE_INVALID			0x00
#define	GB_BATTERY_TYPE_PROTOCOL_VERSION	0x01
#define	GB_BATTERY_TYPE_TECHNOLOGY		0x02
#define	GB_BATTERY_TYPE_STATUS			0x03
#define	GB_BATTERY_TYPE_MAX_VOLTAGE		0x04
#define	GB_BATTERY_TYPE_PERCENT_CAPACITY	0x05
#define	GB_BATTERY_TYPE_TEMPERATURE		0x06
#define	GB_BATTERY_TYPE_VOLTAGE			0x07
#define	GB_BATTERY_TYPE_CURRENT			0x08
#define GB_BATTERY_TYPE_CAPACITY		0x09	// TODO - POWER_SUPPLY_PROP_CURRENT_MAX
#define GB_BATTERY_TYPE_SHUTDOWN_TEMP		0x0a	// TODO - POWER_SUPPLY_PROP_TEMP_ALERT_MAX

/* Should match up with battery types in linux/power_supply.h */
#define GB_BATTERY_TECH_UNKNOWN			0x0000
#define GB_BATTERY_TECH_NiMH			0x0001
#define GB_BATTERY_TECH_LION			0x0002
#define GB_BATTERY_TECH_LIPO			0x0003
#define GB_BATTERY_TECH_LiFe			0x0004
#define GB_BATTERY_TECH_NiCd			0x0005
#define GB_BATTERY_TECH_LiMn			0x0006

struct gb_battery_technology_response {
	__le32	technology;
};

/* Should match up with battery status in linux/power_supply.h */
#define GB_BATTERY_STATUS_UNKNOWN		0x0000
#define GB_BATTERY_STATUS_CHARGING		0x0001
#define GB_BATTERY_STATUS_DISCHARGING		0x0002
#define GB_BATTERY_STATUS_NOT_CHARGING		0x0003
#define GB_BATTERY_STATUS_FULL			0x0004

struct gb_battery_status_response {
	__le16	battery_status;
};

struct gb_battery_max_voltage_response {
	__le32	max_voltage;
};

struct gb_battery_capacity_response {
	__le32	capacity;
};

struct gb_battery_temperature_response {
	__le32	temperature;
};

struct gb_battery_voltage_response {
	__le32	voltage;
};

/* Define get_version() routine */
define_get_version(gb_battery, BATTERY);

static int get_tech(struct gb_battery *gb)
{
	struct gb_battery_technology_response tech_response;
	u32 technology;
	int retval;

	retval = gb_operation_sync(gb->connection, GB_BATTERY_TYPE_TECHNOLOGY,
				   NULL, 0,
				   &tech_response, sizeof(tech_response));
	if (retval)
		return retval;

	/*
	 * Map greybus values to power_supply values.  Hopefully these are
	 * "identical" which should allow gcc to optimize the code away to
	 * nothing.
	 */
	technology = le32_to_cpu(tech_response.technology);
	switch (technology) {
	case GB_BATTERY_TECH_NiMH:
		technology = POWER_SUPPLY_TECHNOLOGY_NiMH;
		break;
	case GB_BATTERY_TECH_LION:
		technology = POWER_SUPPLY_TECHNOLOGY_LION;
		break;
	case GB_BATTERY_TECH_LIPO:
		technology = POWER_SUPPLY_TECHNOLOGY_LIPO;
		break;
	case GB_BATTERY_TECH_LiFe:
		technology = POWER_SUPPLY_TECHNOLOGY_LiFe;
		break;
	case GB_BATTERY_TECH_NiCd:
		technology = POWER_SUPPLY_TECHNOLOGY_NiCd;
		break;
	case GB_BATTERY_TECH_LiMn:
		technology = POWER_SUPPLY_TECHNOLOGY_LiMn;
		break;
	case GB_BATTERY_TECH_UNKNOWN:
	default:
		technology = POWER_SUPPLY_TECHNOLOGY_UNKNOWN;
		break;
	}
	return technology;
}

static int get_status(struct gb_battery *gb)
{
	struct gb_battery_status_response status_response;
	u16 battery_status;
	int retval;

	retval = gb_operation_sync(gb->connection, GB_BATTERY_TYPE_STATUS,
				   NULL, 0,
				   &status_response, sizeof(status_response));
	if (retval)
		return retval;

	/*
	 * Map greybus values to power_supply values.  Hopefully these are
	 * "identical" which should allow gcc to optimize the code away to
	 * nothing.
	 */
	battery_status = le16_to_cpu(status_response.battery_status);
	switch (battery_status) {
	case GB_BATTERY_STATUS_CHARGING:
		battery_status = POWER_SUPPLY_STATUS_CHARGING;
		break;
	case GB_BATTERY_STATUS_DISCHARGING:
		battery_status = POWER_SUPPLY_STATUS_DISCHARGING;
		break;
	case GB_BATTERY_STATUS_NOT_CHARGING:
		battery_status = POWER_SUPPLY_STATUS_NOT_CHARGING;
		break;
	case GB_BATTERY_STATUS_FULL:
		battery_status = POWER_SUPPLY_STATUS_FULL;
		break;
	case GB_BATTERY_STATUS_UNKNOWN:
	default:
		battery_status = POWER_SUPPLY_STATUS_UNKNOWN;
		break;
	}
	return battery_status;
}

static int get_max_voltage(struct gb_battery *gb)
{
	struct gb_battery_max_voltage_response volt_response;
	u32 max_voltage;
	int retval;

	retval = gb_operation_sync(gb->connection, GB_BATTERY_TYPE_MAX_VOLTAGE,
				   NULL, 0,
				   &volt_response, sizeof(volt_response));
	if (retval)
		return retval;

	max_voltage = le32_to_cpu(volt_response.max_voltage);
	return max_voltage;
}

static int get_percent_capacity(struct gb_battery *gb)
{
	struct gb_battery_capacity_response capacity_response;
	u32 capacity;
	int retval;

	retval = gb_operation_sync(gb->connection,
				   GB_BATTERY_TYPE_PERCENT_CAPACITY,
				   NULL, 0, &capacity_response,
				   sizeof(capacity_response));
	if (retval)
		return retval;

	capacity = le32_to_cpu(capacity_response.capacity);
	return capacity;
}

static int get_temp(struct gb_battery *gb)
{
	struct gb_battery_temperature_response temp_response;
	u32 temperature;
	int retval;

	retval = gb_operation_sync(gb->connection, GB_BATTERY_TYPE_TEMPERATURE,
				   NULL, 0,
				   &temp_response, sizeof(temp_response));
	if (retval)
		return retval;

	temperature = le32_to_cpu(temp_response.temperature);
	return temperature;
}

static int get_voltage(struct gb_battery *gb)
{
	struct gb_battery_voltage_response voltage_response;
	u32 voltage;
	int retval;

	retval = gb_operation_sync(gb->connection, GB_BATTERY_TYPE_VOLTAGE,
				   NULL, 0,
				   &voltage_response, sizeof(voltage_response));
	if (retval)
		return retval;

	voltage = le32_to_cpu(voltage_response.voltage);
	return voltage;
}

static int get_property(struct power_supply *b,
			enum power_supply_property psp,
			union power_supply_propval *val)
{
	struct gb_battery *gb = to_gb_battery(b);

	switch (psp) {
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = get_tech(gb);
		break;

	case POWER_SUPPLY_PROP_STATUS:
		val->intval = get_status(gb);
		break;

	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		val->intval = get_max_voltage(gb);
		break;

	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = get_percent_capacity(gb);
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

	return (val->intval < 0) ? val->intval : 0;
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

#ifdef DRIVER_OWNS_PSY_STRUCT
static int init_and_register(struct gb_connection *connection,
			     struct gb_battery *gb)
{
	// FIXME - get a better (i.e. unique) name
	// FIXME - anything else needs to be set?
	gb->bat.name		= "gb_battery";
	gb->bat.type		= POWER_SUPPLY_TYPE_BATTERY;
	gb->bat.properties	= battery_props;
	gb->bat.num_properties	= ARRAY_SIZE(battery_props);
	gb->bat.get_property	= get_property;

	return power_supply_register(&connection->bundle->intf->dev, &gb->bat);
}
#else
static int init_and_register(struct gb_connection *connection,
			     struct gb_battery *gb)
{
	struct power_supply_config cfg = {};

	cfg.drv_data = gb;

	// FIXME - get a better (i.e. unique) name
	// FIXME - anything else needs to be set?
	gb->desc.name		= "gb_battery";
	gb->desc.type		= POWER_SUPPLY_TYPE_BATTERY;
	gb->desc.properties	= battery_props;
	gb->desc.num_properties	= ARRAY_SIZE(battery_props);
	gb->desc.get_property	= get_property;

	gb->bat = power_supply_register(&connection->bundle->intf->dev,
					&gb->desc, &cfg);
	if (IS_ERR(gb->bat))
		return PTR_ERR(gb->bat);

	return 0;
}
#endif

static int gb_battery_connection_init(struct gb_connection *connection)
{
	struct gb_battery *gb;
	int retval;

	gb = kzalloc(sizeof(*gb), GFP_KERNEL);
	if (!gb)
		return -ENOMEM;

	gb->connection = connection;
	connection->private = gb;

	/* Check the version */
	retval = get_version(gb);
	if (retval)
		goto out;
	retval = init_and_register(connection, gb);
out:
	if (retval)
		kfree(gb);

	return retval;
}

static void gb_battery_connection_exit(struct gb_connection *connection)
{
	struct gb_battery *gb = connection->private;

#ifdef DRIVER_OWNS_PSY_STRUCT
	power_supply_unregister(&gb->bat);
#else
	power_supply_unregister(gb->bat);
#endif
	kfree(gb);
}

static struct gb_protocol battery_protocol = {
	.name			= "battery",
	.id			= GREYBUS_PROTOCOL_BATTERY,
	.major			= 0,
	.minor			= 1,
	.connection_init	= gb_battery_connection_init,
	.connection_exit	= gb_battery_connection_exit,
	.request_recv		= NULL,	/* no incoming requests */
};

gb_protocol_driver(&battery_protocol);

MODULE_LICENSE("GPL v2");
