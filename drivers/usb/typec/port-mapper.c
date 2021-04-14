// SPDX-License-Identifier: GPL-2.0
/*
 * USB Type-C Connector Class Port Mapping Utility
 *
 * Copyright (C) 2021, Intel Corporation
 * Author: Heikki Krogerus <heikki.krogerus@linux.intel.com>
 */

#include <linux/acpi.h>
#include <linux/usb.h>
#include <linux/usb/typec.h>

#include "class.h"

struct port_node {
	struct list_head list;
	struct device *dev;
	void *pld;
};

static int acpi_pld_match(const struct acpi_pld_info *pld1,
			  const struct acpi_pld_info *pld2)
{
	if (!pld1 || !pld2)
		return 0;

	/*
	 * To speed things up, first checking only the group_position. It seems
	 * to often have the first unique value in the _PLD.
	 */
	if (pld1->group_position == pld2->group_position)
		return !memcmp(pld1, pld2, sizeof(struct acpi_pld_info));

	return 0;
}

static void *get_pld(struct device *dev)
{
#ifdef CONFIG_ACPI
	struct acpi_pld_info *pld;
	acpi_status status;

	if (!has_acpi_companion(dev))
		return NULL;

	status = acpi_get_physical_device_location(ACPI_HANDLE(dev), &pld);
	if (ACPI_FAILURE(status))
		return NULL;

	return pld;
#else
	return NULL;
#endif
}

static void free_pld(void *pld)
{
#ifdef CONFIG_ACPI
	ACPI_FREE(pld);
#endif
}

static int __link_port(struct typec_port *con, struct port_node *node)
{
	int ret;

	ret = sysfs_create_link(&node->dev->kobj, &con->dev.kobj, "connector");
	if (ret)
		return ret;

	ret = sysfs_create_link(&con->dev.kobj, &node->dev->kobj,
				dev_name(node->dev));
	if (ret) {
		sysfs_remove_link(&node->dev->kobj, "connector");
		return ret;
	}

	list_add_tail(&node->list, &con->port_list);

	return 0;
}

static int link_port(struct typec_port *con, struct port_node *node)
{
	int ret;

	mutex_lock(&con->port_list_lock);
	ret = __link_port(con, node);
	mutex_unlock(&con->port_list_lock);

	return ret;
}

static void __unlink_port(struct typec_port *con, struct port_node *node)
{
	sysfs_remove_link(&con->dev.kobj, dev_name(node->dev));
	sysfs_remove_link(&node->dev->kobj, "connector");
	list_del(&node->list);
}

static void unlink_port(struct typec_port *con, struct port_node *node)
{
	mutex_lock(&con->port_list_lock);
	__unlink_port(con, node);
	mutex_unlock(&con->port_list_lock);
}

static struct port_node *create_port_node(struct device *port)
{
	struct port_node *node;

	node = kzalloc(sizeof(*node), GFP_KERNEL);
	if (!node)
		return ERR_PTR(-ENOMEM);

	node->dev = get_device(port);
	node->pld = get_pld(port);

	return node;
}

static void remove_port_node(struct port_node *node)
{
	put_device(node->dev);
	free_pld(node->pld);
	kfree(node);
}

static int connector_match(struct device *dev, const void *data)
{
	const struct port_node *node = data;

	if (!is_typec_port(dev))
		return 0;

	return acpi_pld_match(to_typec_port(dev)->pld, node->pld);
}

static struct device *find_connector(struct port_node *node)
{
	if (!node->pld)
		return NULL;

	return class_find_device(&typec_class, NULL, node, connector_match);
}

/**
 * typec_link_port - Link a port to its connector
 * @port: The port device
 *
 * Find the connector of @port and create symlink named "connector" for it.
 * Returns 0 on success, or errno in case of a failure.
 *
 * NOTE. The function increments the reference count of @port on success.
 */
int typec_link_port(struct device *port)
{
	struct device *connector;
	struct port_node *node;
	int ret;

	node = create_port_node(port);
	if (IS_ERR(node))
		return PTR_ERR(node);

	connector = find_connector(node);
	if (!connector) {
		ret = 0;
		goto remove_node;
	}

	ret = link_port(to_typec_port(connector), node);
	if (ret)
		goto put_connector;

	return 0;

put_connector:
	put_device(connector);
remove_node:
	remove_port_node(node);

	return ret;
}
EXPORT_SYMBOL_GPL(typec_link_port);

static int port_match_and_unlink(struct device *connector, void *port)
{
	struct port_node *node;
	struct port_node *tmp;
	int ret = 0;

	if (!is_typec_port(connector))
		return 0;

	mutex_lock(&to_typec_port(connector)->port_list_lock);
	list_for_each_entry_safe(node, tmp, &to_typec_port(connector)->port_list, list) {
		ret = node->dev == port;
		if (ret) {
			unlink_port(to_typec_port(connector), node);
			remove_port_node(node);
			put_device(connector);
			break;
		}
	}
	mutex_unlock(&to_typec_port(connector)->port_list_lock);

	return ret;
}

/**
 * typec_unlink_port - Unlink port from its connector
 * @port: The port device
 *
 * Removes the symlink "connector" and decrements the reference count of @port.
 */
void typec_unlink_port(struct device *port)
{
	class_for_each_device(&typec_class, NULL, port, port_match_and_unlink);
}
EXPORT_SYMBOL_GPL(typec_unlink_port);

static int each_port(struct device *port, void *connector)
{
	struct port_node *node;
	int ret;

	node = create_port_node(port);
	if (IS_ERR(node))
		return PTR_ERR(node);

	if (!connector_match(connector, node)) {
		remove_port_node(node);
		return 0;
	}

	ret = link_port(to_typec_port(connector), node);
	if (ret) {
		remove_port_node(node->pld);
		return ret;
	}

	get_device(connector);

	return 0;
}

int typec_link_ports(struct typec_port *con)
{
	int ret = 0;

	con->pld = get_pld(&con->dev);
	if (!con->pld)
		return 0;

	ret = usb_for_each_port(&con->dev, each_port);
	if (ret)
		typec_unlink_ports(con);

	return ret;
}

void typec_unlink_ports(struct typec_port *con)
{
	struct port_node *node;
	struct port_node *tmp;

	mutex_lock(&con->port_list_lock);

	list_for_each_entry_safe(node, tmp, &con->port_list, list) {
		__unlink_port(con, node);
		remove_port_node(node);
		put_device(&con->dev);
	}

	mutex_unlock(&con->port_list_lock);

	free_pld(con->pld);
}
