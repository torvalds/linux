// SPDX-License-Identifier: GPL-2.0
/**
 * Device connections
 *
 * Copyright (C) 2018 Intel Corporation
 * Author: Heikki Krogerus <heikki.krogerus@linux.intel.com>
 */

#include <linux/device.h>
#include <linux/property.h>

static DEFINE_MUTEX(devcon_lock);
static LIST_HEAD(devcon_list);

typedef void *(*devcon_match_fn_t)(struct device_connection *con, int ep,
				   void *data);

static void *
fwnode_graph_devcon_match(struct fwnode_handle *fwnode, const char *con_id,
			  void *data, devcon_match_fn_t match)
{
	struct device_connection con = { .id = con_id };
	struct fwnode_handle *ep;
	void *ret;

	fwnode_graph_for_each_endpoint(fwnode, ep) {
		con.fwnode = fwnode_graph_get_remote_port_parent(ep);
		if (!fwnode_device_is_available(con.fwnode))
			continue;

		ret = match(&con, -1, data);
		fwnode_handle_put(con.fwnode);
		if (ret) {
			fwnode_handle_put(ep);
			return ret;
		}
	}
	return NULL;
}

static void *
fwnode_devcon_match(struct fwnode_handle *fwnode, const char *con_id,
		    void *data, devcon_match_fn_t match)
{
	struct device_connection con = { };
	void *ret;
	int i;

	for (i = 0; ; i++) {
		con.fwnode = fwnode_find_reference(fwnode, con_id, i);
		if (IS_ERR(con.fwnode))
			break;

		ret = match(&con, -1, data);
		fwnode_handle_put(con.fwnode);
		if (ret)
			return ret;
	}

	return NULL;
}

/**
 * device_connection_find_match - Find physical connection to a device
 * @dev: Device with the connection
 * @con_id: Identifier for the connection
 * @data: Data for the match function
 * @match: Function to check and convert the connection description
 *
 * Find a connection with unique identifier @con_id between @dev and another
 * device. @match will be used to convert the connection description to data the
 * caller is expecting to be returned.
 */
void *device_connection_find_match(struct device *dev, const char *con_id,
				   void *data, devcon_match_fn_t match)
{
	struct fwnode_handle *fwnode = dev_fwnode(dev);
	const char *devname = dev_name(dev);
	struct device_connection *con;
	void *ret = NULL;
	int ep;

	if (!match)
		return NULL;

	if (fwnode) {
		ret = fwnode_graph_devcon_match(fwnode, con_id, data, match);
		if (ret)
			return ret;

		ret = fwnode_devcon_match(fwnode, con_id, data, match);
		if (ret)
			return ret;
	}

	mutex_lock(&devcon_lock);

	list_for_each_entry(con, &devcon_list, list) {
		ep = match_string(con->endpoint, 2, devname);
		if (ep < 0)
			continue;

		if (con_id && strcmp(con->id, con_id))
			continue;

		ret = match(con, !ep, data);
		if (ret)
			break;
	}

	mutex_unlock(&devcon_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(device_connection_find_match);

extern struct bus_type platform_bus_type;
extern struct bus_type pci_bus_type;
extern struct bus_type i2c_bus_type;
extern struct bus_type spi_bus_type;

static struct bus_type *generic_match_buses[] = {
	&platform_bus_type,
#ifdef CONFIG_PCI
	&pci_bus_type,
#endif
#ifdef CONFIG_I2C
	&i2c_bus_type,
#endif
#ifdef CONFIG_SPI_MASTER
	&spi_bus_type,
#endif
	NULL,
};

static int device_fwnode_match(struct device *dev, void *fwnode)
{
	return dev_fwnode(dev) == fwnode;
}

static void *device_connection_fwnode_match(struct device_connection *con)
{
	struct bus_type *bus;
	struct device *dev;

	for (bus = generic_match_buses[0]; bus; bus++) {
		dev = bus_find_device(bus, NULL, (void *)con->fwnode,
				      device_fwnode_match);
		if (dev && !strncmp(dev_name(dev), con->id, strlen(con->id)))
			return dev;

		put_device(dev);
	}
	return NULL;
}

/* This tries to find the device from the most common bus types by name. */
static void *generic_match(struct device_connection *con, int ep, void *data)
{
	struct bus_type *bus;
	struct device *dev;

	if (con->fwnode)
		return device_connection_fwnode_match(con);

	for (bus = generic_match_buses[0]; bus; bus++) {
		dev = bus_find_device_by_name(bus, NULL, con->endpoint[ep]);
		if (dev)
			return dev;
	}

	/*
	 * We only get called if a connection was found, tell the caller to
	 * wait for the other device to show up.
	 */
	return ERR_PTR(-EPROBE_DEFER);
}

/**
 * device_connection_find - Find two devices connected together
 * @dev: Device with the connection
 * @con_id: Identifier for the connection
 *
 * Find a connection with unique identifier @con_id between @dev and
 * another device. On success returns handle to the device that is connected
 * to @dev, with the reference count for the found device incremented. Returns
 * NULL if no matching connection was found, or ERR_PTR(-EPROBE_DEFER) when a
 * connection was found but the other device has not been enumerated yet.
 */
struct device *device_connection_find(struct device *dev, const char *con_id)
{
	return device_connection_find_match(dev, con_id, NULL, generic_match);
}
EXPORT_SYMBOL_GPL(device_connection_find);

/**
 * device_connection_add - Register a connection description
 * @con: The connection description to be registered
 */
void device_connection_add(struct device_connection *con)
{
	mutex_lock(&devcon_lock);
	list_add_tail(&con->list, &devcon_list);
	mutex_unlock(&devcon_lock);
}
EXPORT_SYMBOL_GPL(device_connection_add);

/**
 * device_connections_remove - Unregister connection description
 * @con: The connection description to be unregistered
 */
void device_connection_remove(struct device_connection *con)
{
	mutex_lock(&devcon_lock);
	list_del(&con->list);
	mutex_unlock(&devcon_lock);
}
EXPORT_SYMBOL_GPL(device_connection_remove);
