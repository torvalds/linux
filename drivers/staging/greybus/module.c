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

static void greybus_module_release(struct device *dev)
{
	struct gb_module *gmod = to_gb_module(dev);

	kfree(gmod);
}

static struct device_type greybus_module_type = {
	.name =		"greybus_module",
	.release =	greybus_module_release,
};

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
	int retval;

	gmod = gb_module_find(hd, module_id);
	if (gmod) {
		dev_err(hd->parent, "Duplicate module id %d will not be created\n",
			module_id);
		return NULL;
	}

	gmod = kzalloc(sizeof(*gmod), GFP_KERNEL);
	if (!gmod)
		return NULL;

	gmod->hd = hd;		/* XXX refcount? */
	gmod->module_id = module_id;
	INIT_LIST_HEAD(&gmod->interfaces);

	spin_lock_irq(&gb_modules_lock);
	list_add_tail(&gmod->links, &hd->modules);
	spin_unlock_irq(&gb_modules_lock);

	gmod->dev.parent = hd->parent;
	gmod->dev.driver = NULL;
	gmod->dev.bus = &greybus_bus_type;
	gmod->dev.type = &greybus_module_type;
	gmod->dev.groups = greybus_module_groups;
	gmod->dev.dma_mask = hd->parent->dma_mask;
	device_initialize(&gmod->dev);
	dev_set_name(&gmod->dev, "%d", module_id);

	retval = device_add(&gmod->dev);
	if (retval) {
		put_device(&gmod->dev);
		return NULL;
	}

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

	/* XXX Do something with gmod->gb_tty */

	gb_interface_destroy(gmod);

	kfree(gmod->product_string);
	kfree(gmod->vendor_string);
	/* kref_put(module->hd); */

	device_del(&gmod->dev);
}

struct gb_module *gb_module_find(struct greybus_host_device *hd, u8 module_id)
{
	struct gb_module *module;

	list_for_each_entry(module, &hd->modules, links)
		if (module->module_id == module_id)
			return module;

	return NULL;
}

int
gb_module_interface_init(struct gb_module *gmod, u8 interface_id, u8 device_id)
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
	interface->device_id = device_id;

	return 0;
}
