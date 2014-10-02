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
	INIT_LIST_HEAD(&interface->functions);

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

	/* kref_put(gmod); */
	kfree(interface);
}
