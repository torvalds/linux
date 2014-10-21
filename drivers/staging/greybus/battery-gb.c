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
	struct gb_connection *connection;
	u8 version_major;
	u8 version_minor;

};
#define to_gb_battery(x) container_of(x, struct gb_battery, bat)

/* Version of the Greybus battery protocol we support */
#define	GB_BATTERY_VERSION_MAJOR		0x00
#define	GB_BATTERY_VERSION_MINOR		0x01

/* Greybus battery request types */
#define	GB_BATTERY_TYPE_INVALID			0x00
#define	GB_BATTERY_TYPE_PROTOCOL_VERSION	0x01
#define	GB_BATTERY_TYPE_TECHNOLOGY		0x02
#define	GB_BATTERY_TYPE_STATUS			0x03
#define	GB_BATTERY_TYPE_MAX_VOLTAGE		0x04
#define	GB_BATTERY_TYPE_CAPACITY		0x05
#define	GB_BATTERY_TYPE_TEMPERATURE		0x06
#define	GB_BATTERY_TYPE_VOLTAGE			0x07

struct gb_battery_proto_version_response {
	__u8	status;
	__u8	major;
	__u8	minor;
};

/* Should match up with battery types in linux/power_supply.h */
#define GB_BATTERY_TECH_UNKNOWN			0x0000
#define GB_BATTERY_TECH_NiMH			0x0001
#define GB_BATTERY_TECH_LION			0x0002
#define GB_BATTERY_TECH_LIPO			0x0003
#define GB_BATTERY_TECH_LiFe			0x0004
#define GB_BATTERY_TECH_NiCd			0x0005
#define GB_BATTERY_TECH_LiMn			0x0006

struct gb_battery_technology_request {
	__u8	status;
	__le32	technology;
};

/* Should match up with battery status in linux/power_supply.h */
#define GB_BATTERY_STATUS_UNKNOWN		0x0000
#define GB_BATTERY_STATUS_CHARGING		0x0001
#define GB_BATTERY_STATUS_DISCHARGING		0x0002
#define GB_BATTERY_STATUS_NOT_CHARGING		0x0003
#define GB_BATTERY_STATUS_FULL			0x0004

struct gb_battery_status_request {
	__u8	status;
	__le16	battery_status;
};

struct gb_battery_max_voltage_request {
	__u8	status;
	__le32	max_voltage;
};

struct gb_battery_capacity_request {
	__u8	status;
	__le32	capacity;
};

struct gb_battery_temperature_request {
	__u8	status;
	__le32	temperature;
};

struct gb_battery_voltage_request {
	__u8	status;
	__le32	voltage;
};


static const struct greybus_module_id id_table[] = {
	{ GREYBUS_DEVICE(0x42, 0x42) },	/* make shit up */
	{ },	/* terminating NULL entry */
};

static int battery_operation(struct gb_battery *gb, int type,
			     void *response, int response_size)
{
	struct gb_connection *connection = gb->connection;
	struct gb_operation *operation;
	struct gb_battery_technology_request *fake_request;
	u8 *local_response;
	int ret;

	local_response = kmalloc(response_size, GFP_KERNEL);
	if (!local_response)
		return -ENOMEM;

	operation = gb_operation_create(connection, type, 0, response_size);
	if (!operation) {
		kfree(local_response);
		return -ENOMEM;
	}

	/* Synchronous operation--no callback */
	ret = gb_operation_request_send(operation, NULL);
	if (ret) {
		pr_err("version operation failed (%d)\n", ret);
		goto out;
	}

	/*
	 * We only want to look at the status, and all requests have the same
	 * layout for where the status is, so cast this to a random request so
	 * we can see the status easier.
	 */
	fake_request = (struct gb_battery_technology_request *)local_response;
	if (fake_request->status) {
		gb_connection_err(connection, "version response %hhu",
			fake_request->status);
		ret = -EIO;
	} else {
		/* Good request, so copy to the caller's buffer */
		memcpy(response, local_response, response_size);
	}
out:
	gb_operation_destroy(operation);

	return ret;
}

/*
 * This request only uses the connection field, and if successful,
 * fills in the major and minor protocol version of the target.
 */
static int get_version(struct gb_battery *gb)
{
	struct gb_battery_proto_version_response version_request;
	int retval;

	retval = battery_operation(gb, GB_BATTERY_TYPE_PROTOCOL_VERSION,
				   &version_request, sizeof(version_request));
	if (retval)
		return retval;

	if (version_request.major > GB_BATTERY_VERSION_MAJOR) {
		pr_err("unsupported major version (%hhu > %hhu)\n",
			version_request.major, GB_BATTERY_VERSION_MAJOR);
		return -ENOTSUPP;
	}

	gb->version_major = version_request.major;
	gb->version_minor = version_request.minor;
	return 0;
}

static int get_tech(struct gb_battery *gb)
{
	struct gb_battery_technology_request tech_request;
	u32 technology;
	int retval;

	retval = battery_operation(gb, GB_BATTERY_TYPE_TECHNOLOGY,
				   &tech_request, sizeof(tech_request));
	if (retval)
		return retval;

	/*
	 * We have a one-to-one mapping of tech types to power_supply
	 * status, so just return that value.
	 */
	technology = le32_to_cpu(tech_request.technology);
	return technology;
}

static int get_status(struct gb_battery *gb)
{
	struct gb_battery_status_request status_request;
	u16 battery_status;
	int retval;

	retval = battery_operation(gb, GB_BATTERY_TYPE_STATUS,
				   &status_request, sizeof(status_request));
	if (retval)
		return retval;

	/*
	 * We have a one-to-one mapping of battery status to power_supply
	 * status, so just return that value.
	 */
	battery_status = le16_to_cpu(status_request.battery_status);
	return battery_status;
}

static int get_max_voltage(struct gb_battery *gb)
{
	struct gb_battery_max_voltage_request volt_request;
	u32 max_voltage;
	int retval;

	retval = battery_operation(gb, GB_BATTERY_TYPE_MAX_VOLTAGE,
				   &volt_request, sizeof(volt_request));
	if (retval)
		return retval;

	max_voltage = le32_to_cpu(volt_request.max_voltage);
	return max_voltage;
}

static int get_capacity(struct gb_battery *gb)
{
	struct gb_battery_capacity_request capacity_request;
	u32 capacity;
	int retval;

	retval = battery_operation(gb, GB_BATTERY_TYPE_CAPACITY,
				   &capacity_request, sizeof(capacity_request));
	if (retval)
		return retval;

	capacity = le32_to_cpu(capacity_request.capacity);
	return capacity;
}

static int get_temp(struct gb_battery *gb)
{
	struct gb_battery_temperature_request temp_request;
	u32 temperature;
	int retval;

	retval = battery_operation(gb, GB_BATTERY_TYPE_TEMPERATURE,
				   &temp_request, sizeof(temp_request));
	if (retval)
		return retval;

	temperature = le32_to_cpu(temp_request.temperature);
	return temperature;
}

static int get_voltage(struct gb_battery *gb)
{
	struct gb_battery_voltage_request voltage_request;
	u32 voltage;
	int retval;

	retval = battery_operation(gb, GB_BATTERY_TYPE_VOLTAGE,
				   &voltage_request, sizeof(voltage_request));
	if (retval)
		return retval;

	voltage = le32_to_cpu(voltage_request.voltage);
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

int gb_battery_device_init(struct gb_connection *connection)
{
	struct gb_battery *gb;
	struct power_supply *b;
	int retval;

	gb = kzalloc(sizeof(*gb), GFP_KERNEL);
	if (!gb)
		return -ENOMEM;

	gb->connection = connection;	// FIXME refcount!

	/* Check the version */
	retval = get_version(gb);
	if (retval) {
		kfree(gb);
		return retval;
	}

	b = &gb->bat;
	// FIXME - get a better (i.e. unique) name
	// FIXME - anything else needs to be set?
	b->name			= "gb_battery";
	b->type			= POWER_SUPPLY_TYPE_BATTERY,
	b->properties		= battery_props,
	b->num_properties	= ARRAY_SIZE(battery_props),
	b->get_property		= get_property,

	retval = power_supply_register(&connection->interface->gmod->dev, b);
	if (retval) {
		kfree(gb);
		return retval;
	}

	return 0;
}

void gb_battery_disconnect(struct gb_module *gmod)
{
#if 0
	struct gb_battery *gb;

	gb = gmod->gb_battery;
	if (!gb)
		return;

	power_supply_unregister(&gb->bat);

	kfree(gb);
#endif
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
