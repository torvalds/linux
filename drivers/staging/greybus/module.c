/*
 * Greybus modules
 *
 * Copyright 2014 Google Inc.
 *
 * Released under the GPLv2 only.
 */

#include "greybus.h"

/* XXX This could be per-host device */
static DEFINE_SPINLOCK(gb_modules_lock);

static int gb_module_match_one_id(struct gb_module *gmod,
				const struct greybus_module_id *id)
{
	if ((id->match_flags & GREYBUS_DEVICE_ID_MATCH_VENDOR) &&
	    (id->vendor != gmod->vendor))
		return 0;

	if ((id->match_flags & GREYBUS_DEVICE_ID_MATCH_PRODUCT) &&
	    (id->product != gmod->product))
		return 0;

	if ((id->match_flags & GREYBUS_DEVICE_ID_MATCH_SERIAL) &&
	    (id->unique_id != gmod->unique_id))
		return 0;

	return 1;
}

const struct greybus_module_id *gb_module_match_id(struct gb_module *gmod,
				const struct greybus_module_id *id)
{
	if (id == NULL)
		return NULL;

	for (; id->vendor || id->product || id->unique_id ||
			id->driver_info; id++) {
		if (gb_module_match_one_id(gmod, id))
			return id;
	}

	return NULL;
}

static void gb_module_interfaces_exit(struct gb_module *gmod)
{
	struct gb_interface *interface;
	struct gb_interface *next;

	list_for_each_entry_safe(interface, next, &gmod->interfaces, links)
		gb_interface_destroy(interface);
}

/*
 * A Greybus module represents a user-replacable component on an Ara
 * phone.
 *
 * Create a gb_module structure to represent a discovered module.
 * The position within the Endo is encoded in the "module_id" argument.
 * Returns a pointer to the new module or a null pointer if a
 * failure occurs due to memory exhaustion.
 */
struct gb_module *gb_module_create(struct greybus_host_device *hd, u8 module_id)
{
	struct gb_module *gmod;

	gmod = kzalloc(sizeof(*gmod), GFP_KERNEL);
	if (!gmod)
		return NULL;

	gmod->hd = hd;		/* XXX refcount? */
	gmod->module_id = module_id;	/* XXX check for dups */
	INIT_LIST_HEAD(&gmod->interfaces);

	spin_lock_irq(&gb_modules_lock);
	list_add_tail(&gmod->links, &hd->modules);
	spin_unlock_irq(&gb_modules_lock);

	return gmod;
}

/*
 * Tear down a previously set up module.
 */
void gb_module_destroy(struct gb_module *gmod)
{
	if (WARN_ON(!gmod))
		return;

	spin_lock_irq(&gb_modules_lock);
	list_del(&gmod->links);
	spin_unlock_irq(&gb_modules_lock);

	gb_module_interfaces_exit(gmod);
	/* XXX Do something with gmod->gb_tty */

	put_device(&gmod->dev);
	/* kfree(gmod->dev->name); */

	kfree(gmod->product_string);
	kfree(gmod->vendor_string);
	/* kref_put(module->hd); */


	kfree(gmod);
}

struct gb_module *gb_module_find(struct greybus_host_device *hd, u8 module_id)
{
	struct gb_module *module;

	list_for_each_entry(module, &hd->modules, links)
		if (module->module_id == module_id)
			return module;

	return NULL;
}

void gb_module_interfaces_init(struct gb_module *gmod)
{
	struct gb_interface *interface;
	int ret = 0;

	list_for_each_entry(interface, &gmod->interfaces, links) {
		ret = gb_interface_connections_init(interface);
		if (ret)
			dev_err(gmod->hd->parent,
				"module interface init error %d\n", ret);
	}
}
