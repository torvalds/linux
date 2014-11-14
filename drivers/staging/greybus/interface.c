/*
 * Greybus interfaces
 *
 * Copyright 2014 Google Inc.
 *
 * Released under the GPLv2 only.
 */

#include "greybus.h"

static ssize_t device_id_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct gb_interface *interface = to_gb_interface(dev);

	return sprintf(buf, "%d", interface->device_id);
}
static DEVICE_ATTR_RO(device_id);

static struct attribute *interface_attrs[] = {
	&dev_attr_device_id.attr,
	NULL,
};

ATTRIBUTE_GROUPS(interface);

static void gb_interface_release(struct device *dev)
{
	struct gb_interface *interface = to_gb_interface(dev);

	kfree(interface);
}

static struct device_type greybus_interface_type = {
	.name =		"greybus_interface",
	.release =	gb_interface_release,
};


/* XXX This could be per-host device or per-module */
static DEFINE_SPINLOCK(gb_interfaces_lock);

/*
 * A Greybus interface represents a UniPro device present on a
 * module.  For Project Ara, each active Interface Block on a module
 * implements a UniPro device, and therefore a Greybus interface.  A
 * Greybus module has at least one interface, but can have two (or
 * even more).
 *
 * Create a gb_interface structure to represent a discovered
 * interface.  Returns a pointer to the new interface or a null
 * pointer if a failure occurs due to memory exhaustion.
 */
struct gb_interface *
gb_interface_create(struct gb_module *gmod, u8 interface_id)
{
	struct gb_interface *interface;
	int retval;

	interface = kzalloc(sizeof(*interface), GFP_KERNEL);
	if (!interface)
		return NULL;

	interface->gmod = gmod;
	interface->id = interface_id;
	interface->device_id = 0xff;	/* Invalid device id to start with */
	INIT_LIST_HEAD(&interface->connections);

	/* Build up the interface device structures and register it with the
	 * driver core */
	interface->dev.parent = &gmod->dev;
	interface->dev.bus = &greybus_bus_type;
	interface->dev.type = &greybus_interface_type;
	interface->dev.groups = interface_groups;
	device_initialize(&interface->dev);
	dev_set_name(&interface->dev, "%d:%d", gmod->module_id, interface_id);

	retval = device_add(&interface->dev);
	if (retval) {
		pr_err("failed to add interface device for id 0x%02hhx\n",
			interface_id);
		put_device(&interface->dev);
		kfree(interface);
		return NULL;
	}

	spin_lock_irq(&gb_interfaces_lock);
	list_add_tail(&interface->links, &gmod->interfaces);
	spin_unlock_irq(&gb_interfaces_lock);

	return interface;
}

/*
 * Tear down a previously set up interface.
 */
void gb_interface_destroy(struct gb_module *gmod)
{
	struct gb_interface *interface;
	struct gb_interface *temp;

	if (WARN_ON(!gmod))
		return;

	spin_lock_irq(&gb_interfaces_lock);
	list_for_each_entry_safe(interface, temp, &gmod->interfaces, links) {
		list_del(&interface->links);
		gb_interface_connections_exit(interface);
		device_del(&interface->dev);
	}
	spin_unlock_irq(&gb_interfaces_lock);
}

int gb_interface_init(struct gb_module *gmod, u8 interface_id, u8 device_id)
{
	struct gb_interface *interface;
	int ret;

	interface = gb_interface_find(gmod, interface_id);
	if (!interface) {
		dev_err(gmod->hd->parent, "module %hhu not found\n",
			interface_id);
		return -ENOENT;
	}
	interface->device_id = device_id;

	ret = svc_set_route_send(interface, gmod->hd);
	if (ret) {
		dev_err(gmod->hd->parent, "failed to set route (%d)\n", ret);
		return ret;
	}

	ret = gb_interface_connections_init(interface);
	if (ret) {
		dev_err(gmod->hd->parent, "module interface init error %d\n",
			ret);
		/* XXX clear route */
		return ret;
	}

	return 0;
}

struct gb_interface *gb_interface_find(struct gb_module *module,
				      u8 interface_id)
{
	struct gb_interface *interface;

	spin_lock_irq(&gb_interfaces_lock);
	list_for_each_entry(interface, &module->interfaces, links)
		if (interface->id == interface_id) {
			spin_unlock_irq(&gb_interfaces_lock);
			return interface;
		}
	spin_unlock_irq(&gb_interfaces_lock);

	return NULL;
}

int gb_interface_connections_init(struct gb_interface *interface)
{
	struct gb_connection *connection;
	int ret = 0;

	list_for_each_entry(connection, &interface->connections,
			interface_links) {
		ret = gb_connection_init(connection);
		if (ret)
			break;
	}

	return ret;
}

void gb_interface_connections_exit(struct gb_interface *interface)
{
	struct gb_connection *connection;
	struct gb_connection *next;

	list_for_each_entry_safe(connection, next, &interface->connections,
			interface_links) {
		gb_connection_exit(connection);
		gb_connection_destroy(connection);
	}
}
