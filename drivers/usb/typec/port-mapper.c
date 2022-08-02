// SPDX-License-Identifier: GPL-2.0
/*
 * USB Type-C Connector Class Port Mapping Utility
 *
 * Copyright (C) 2021, Intel Corporation
 * Author: Heikki Krogerus <heikki.krogerus@linux.intel.com>
 */

#include <linux/acpi.h>
#include <linux/component.h>

#include "class.h"

static int typec_aggregate_bind(struct device *dev)
{
	return component_bind_all(dev, NULL);
}

static void typec_aggregate_unbind(struct device *dev)
{
	component_unbind_all(dev, NULL);
}

static const struct component_master_ops typec_aggregate_ops = {
	.bind = typec_aggregate_bind,
	.unbind = typec_aggregate_unbind,
};

struct each_port_arg {
	struct typec_port *port;
	struct component_match *match;
};

static int typec_port_compare(struct device *dev, void *fwnode)
{
	return device_match_fwnode(dev, fwnode);
}

static int typec_port_match(struct device *dev, void *data)
{
	struct acpi_device *adev = to_acpi_device(dev);
	struct each_port_arg *arg = data;
	struct acpi_device *con_adev;

	con_adev = ACPI_COMPANION(&arg->port->dev);
	if (con_adev == adev)
		return 0;

	if (con_adev->pld_crc == adev->pld_crc)
		component_match_add(&arg->port->dev, &arg->match, typec_port_compare,
				    acpi_fwnode_handle(adev));
	return 0;
}

int typec_link_ports(struct typec_port *con)
{
	struct each_port_arg arg = { .port = con, .match = NULL };

	if (!has_acpi_companion(&con->dev))
		return 0;

	acpi_bus_for_each_dev(typec_port_match, &arg);
	if (!arg.match)
		return 0;

	/*
	 * REVISIT: Now each connector can have only a single component master.
	 * So far only the USB ports connected to the USB Type-C connector share
	 * the _PLD with it, but if there one day is something else (like maybe
	 * the DisplayPort ACPI device object) that also shares the _PLD with
	 * the connector, every one of those needs to have its own component
	 * master, because each different type of component needs to be bind to
	 * the connector independently of the other components. That requires
	 * improvements to the component framework. Right now you can only have
	 * one master per device.
	 */
	return component_master_add_with_match(&con->dev, &typec_aggregate_ops, arg.match);
}

void typec_unlink_ports(struct typec_port *con)
{
	if (has_acpi_companion(&con->dev))
		component_master_del(&con->dev, &typec_aggregate_ops);
}
