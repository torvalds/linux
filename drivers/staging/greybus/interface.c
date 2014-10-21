/*
 * Greybus interfaces
 *
 * Copyright 2014 Google Inc.
 *
 * Released under the GPLv2 only.
 */

#include "greybus.h"

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

	interface = kzalloc(sizeof(*interface), GFP_KERNEL);
	if (!interface)
		return NULL;

	interface->gmod = gmod;		/* XXX refcount? */
	interface->id = interface_id;
	INIT_LIST_HEAD(&interface->connections);

	spin_lock_irq(&gb_interfaces_lock);
	list_add_tail(&interface->links, &gmod->interfaces);
	spin_unlock_irq(&gb_interfaces_lock);

	return interface;
}

/*
 * Tear down a previously set up interface.
 */
void gb_interface_destroy(struct gb_interface *interface)
{
	if (WARN_ON(!interface))
		return;

	spin_lock_irq(&gb_interfaces_lock);
	list_del(&interface->links);
	spin_unlock_irq(&gb_interfaces_lock);

	gb_interface_connections_exit(interface);

	/* kref_put(gmod); */
	kfree(interface);
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
