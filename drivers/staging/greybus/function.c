/*
 * Greybus functions
 *
 * Copyright 2014 Google Inc.
 *
 * Released under the GPLv2 only.
 */

#include "greybus.h"

/* XXX This could be per-host device or per-module or per-interface */
static DEFINE_SPINLOCK(gb_functions_lock);

/*
 * A Greybus function generically defines an entity associated with
 * a CPort within a module.  Each function has a type (e.g. i2c,
 * GPIO, etc.) that defines how it behaves and how the AP interacts
 * with it.
 *
 * Create a gb_function structure to represent a discovered
 * function.  Returns a pointer to the new function or a null
 * pointer if a failure occurs due to memory exhaustion.
 */
struct gb_function *gb_function_create(struct gb_interface *interface,
			u16 cport_id, enum greybus_function_type type)
{
	struct gb_function *function;

	function = kzalloc(sizeof(*function), GFP_KERNEL);
	if (!function)
		return NULL;

	function->interface = interface;	/* XXX refcount? */
	function->cport_id = cport_id;
	function->type = type;

	spin_lock_irq(&gb_functions_lock);
	list_add_tail(&function->links, &interface->functions);
	spin_unlock_irq(&gb_functions_lock);

	return function;
}

/*
 * Tear down a previously set up function.
 */
void gb_function_destroy(struct gb_function *function)
{
	if (WARN_ON(!function))
		return;

	spin_lock_irq(&gb_functions_lock);
	list_del(&function->links);
	spin_unlock_irq(&gb_functions_lock);

	/* kref_put(gmod); */
	kfree(function);
}
