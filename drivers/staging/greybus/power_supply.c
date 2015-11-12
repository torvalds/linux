/*
 * Power Supply driver for a Greybus module.
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

struct gb_power_supply {
	/*
	 * The power supply api changed in 4.1, so handle both the old
	 * and new apis in the same driver for now, until this is merged
	 * upstream, when all of these version checks can be removed.
	 */
#ifdef DRIVER_OWNS_PSY_STRUCT
	struct power_supply psy;
#define to_gb_power_supply(x) container_of(x, struct gb_power_supply, psy)
#else
	struct power_supply *psy;
	struct power_supply_desc desc;
#define to_gb_power_supply(x) power_supply_get_drvdata(x)
#endif
	// FIXME
	// we will want to keep the power supply stats in here as we will be
	// getting updates from the SVC "on the fly" so we don't have to always
	// go ask the power supply for some information. Hopefully...
	struct gb_connection *connection;

};

static int get_tech(struct gb_power_supply *gb)
{
	struct gb_power_supply_technology_response tech_response;
	u32 technology;
	int retval;

	retval = gb_operation_sync(gb->connection,
				   GB_POWER_SUPPLY_TYPE_TECHNOLOGY,
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
	case GB_POWER_SUPPLY_TECH_NiMH:
		technology = POWER_SUPPLY_TECHNOLOGY_NiMH;
		break;
	case GB_POWER_SUPPLY_TECH_LION:
		technology = POWER_SUPPLY_TECHNOLOGY_LION;
		break;
	case GB_POWER_SUPPLY_TECH_LIPO:
		technology = POWER_SUPPLY_TECHNOLOGY_LIPO;
		break;
	case GB_POWER_SUPPLY_TECH_LiFe:
		technology = POWER_SUPPLY_TECHNOLOGY_LiFe;
		break;
	case GB_POWER_SUPPLY_TECH_NiCd:
		technology = POWER_SUPPLY_TECHNOLOGY_NiCd;
		break;
	case GB_POWER_SUPPLY_TECH_LiMn:
		technology = POWER_SUPPLY_TECHNOLOGY_LiMn;
		break;
	case GB_POWER_SUPPLY_TECH_UNKNOWN:
	default:
		technology = POWER_SUPPLY_TECHNOLOGY_UNKNOWN;
		break;
	}
	return technology;
}

static int get_status(struct gb_power_supply *gb)
{
	struct gb_power_supply_status_response status_response;
	u16 psy_status;
	int retval;

	retval = gb_operation_sync(gb->connection, GB_POWER_SUPPLY_TYPE_STATUS,
				   NULL, 0,
				   &status_response, sizeof(status_response));
	if (retval)
		return retval;

	/*
	 * Map greybus values to power_supply values.  Hopefully these are
	 * "identical" which should allow gcc to optimize the code away to
	 * nothing.
	 */
	psy_status = le16_to_cpu(status_response.psy_status);
	switch (psy_status) {
	case GB_POWER_SUPPLY_STATUS_CHARGING:
		psy_status = POWER_SUPPLY_STATUS_CHARGING;
		break;
	case GB_POWER_SUPPLY_STATUS_DISCHARGING:
		psy_status = POWER_SUPPLY_STATUS_DISCHARGING;
		break;
	case GB_POWER_SUPPLY_STATUS_NOT_CHARGING:
		psy_status = POWER_SUPPLY_STATUS_NOT_CHARGING;
		break;
	case GB_POWER_SUPPLY_STATUS_FULL:
		psy_status = POWER_SUPPLY_STATUS_FULL;
		break;
	case GB_POWER_SUPPLY_STATUS_UNKNOWN:
	default:
		psy_status = POWER_SUPPLY_STATUS_UNKNOWN;
		break;
	}
	return psy_status;
}

static int get_max_voltage(struct gb_power_supply *gb)
{
	struct gb_power_supply_max_voltage_response volt_response;
	u32 max_voltage;
	int retval;

	retval = gb_operation_sync(gb->connection,
				   GB_POWER_SUPPLY_TYPE_MAX_VOLTAGE,
				   NULL, 0,
				   &volt_response, sizeof(volt_response));
	if (retval)
		return retval;

	max_voltage = le32_to_cpu(volt_response.max_voltage);
	return max_voltage;
}

static int get_percent_capacity(struct gb_power_supply *gb)
{
	struct gb_power_supply_capacity_response capacity_response;
	u32 capacity;
	int retval;

	retval = gb_operation_sync(gb->connection,
				   GB_POWER_SUPPLY_TYPE_PERCENT_CAPACITY,
				   NULL, 0, &capacity_response,
				   sizeof(capacity_response));
	if (retval)
		return retval;

	capacity = le32_to_cpu(capacity_response.capacity);
	return capacity;
}

static int get_temp(struct gb_power_supply *gb)
{
	struct gb_power_supply_temperature_response temp_response;
	u32 temperature;
	int retval;

	retval = gb_operation_sync(gb->connection,
				   GB_POWER_SUPPLY_TYPE_TEMPERATURE,
				   NULL, 0,
				   &temp_response, sizeof(temp_response));
	if (retval)
		return retval;

	temperature = le32_to_cpu(temp_response.temperature);
	return temperature;
}

static int get_voltage(struct gb_power_supply *gb)
{
	struct gb_power_supply_voltage_response voltage_response;
	u32 voltage;
	int retval;

	retval = gb_operation_sync(gb->connection, GB_POWER_SUPPLY_TYPE_VOLTAGE,
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
	struct gb_power_supply *gb = to_gb_power_supply(b);

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
static enum power_supply_property psy_props[] = {
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
	gb->psy.name		= "gb_battery";
	gb->psy.type		= POWER_SUPPLY_TYPE_BATTERY;
	gb->psy.properties	= psy_props;
	gb->psy.num_properties	= ARRAY_SIZE(psy_props);
	gb->psy.get_property	= get_property;

	return power_supply_register(&connection->bundle->dev, &gb->psy);
}
#else
static int init_and_register(struct gb_connection *connection,
			     struct gb_power_supply *gb)
{
	struct power_supply_config cfg = {};

	cfg.drv_data = gb;

	// FIXME - get a better (i.e. unique) name
	// FIXME - anything else needs to be set?
	gb->desc.name		= "gb_battery";
	gb->desc.type		= POWER_SUPPLY_TYPE_BATTERY;
	gb->desc.properties	= psy_props;
	gb->desc.num_properties	= ARRAY_SIZE(psy_props);
	gb->desc.get_property	= get_property;

	gb->psy = power_supply_register(&connection->bundle->dev,
					&gb->desc, &cfg);
	if (IS_ERR(gb->psy))
		return PTR_ERR(gb->psy);

	return 0;
}
#endif

static int gb_power_supply_connection_init(struct gb_connection *connection)
{
	struct gb_power_supply *gb;
	int retval;

	gb = kzalloc(sizeof(*gb), GFP_KERNEL);
	if (!gb)
		return -ENOMEM;

	gb->connection = connection;
	connection->private = gb;

	retval = init_and_register(connection, gb);
	if (retval)
		kfree(gb);

	return retval;
}

static void gb_power_supply_connection_exit(struct gb_connection *connection)
{
	struct gb_power_supply *gb = connection->private;

#ifdef DRIVER_OWNS_PSY_STRUCT
	power_supply_unregister(&gb->psy);
#else
	power_supply_unregister(gb->psy);
#endif
	kfree(gb);
}

static struct gb_protocol power_supply_protocol = {
	.name			= "power_supply",
	.id			= GREYBUS_PROTOCOL_POWER_SUPPLY,
	.major			= GB_POWER_SUPPLY_VERSION_MAJOR,
	.minor			= GB_POWER_SUPPLY_VERSION_MINOR,
	.connection_init	= gb_power_supply_connection_init,
	.connection_exit	= gb_power_supply_connection_exit,
	.request_recv		= NULL,	/* no incoming requests */
};

gb_protocol_driver(&power_supply_protocol);

MODULE_LICENSE("GPL v2");
