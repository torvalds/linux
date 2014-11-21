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

struct gb_module *gb_module_find(struct greybus_host_device *hd, u8 module_id)
{
	struct gb_module *module;

	list_for_each_entry(module, &hd->modules, links)
		if (module->module_id == module_id)
			return module;

	return NULL;
}

static void greybus_module_release(struct device *dev)
{
	struct gb_module *gmod = to_gb_module(dev);

	kfree(gmod);
}

struct device_type greybus_module_type = {
	.name =		"greybus_module",
	.release =	greybus_module_release,
};

/*
 * A Greybus module represents a user-replicable component on an Ara
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

	gmod->dev.parent = hd->parent;
	gmod->dev.bus = &greybus_bus_type;
	gmod->dev.type = &greybus_module_type;
	gmod->dev.groups = greybus_module_groups;
	gmod->dev.dma_mask = hd->parent->dma_mask;
	device_initialize(&gmod->dev);
	dev_set_name(&gmod->dev, "%d", module_id);

	retval = device_add(&gmod->dev);
	if (retval) {
		pr_err("failed to add module device for id 0x%02hhx\n",
			module_id);
		put_device(&gmod->dev);
		kfree(gmod);
		return NULL;
	}

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

	/* XXX Do something with gmod->gb_tty */

	gb_interface_destroy(gmod);

	kfree(gmod->product_string);
	kfree(gmod->vendor_string);
	/* kref_put(module->hd); */

	device_del(&gmod->dev);
}

/**
 * gb_add_module
 *
 * Pass in a buffer that _should_ contain a Greybus module manifest
 * and register a greybus device structure with the kernel core.
 */
void gb_add_module(struct greybus_host_device *hd, u8 module_id,
		   u8 *data, int size)
{
	struct gb_module *gmod;

	gmod = gb_module_create(hd, module_id);
	if (!gmod) {
		dev_err(hd->parent, "failed to create module\n");
		return;
	}

	/*
	 * Parse the manifest and build up our data structures
	 * representing what's in it.
	 */
	if (!gb_manifest_parse(gmod, data, size)) {
		dev_err(hd->parent, "manifest error\n");
		goto err_module;
	}

	/*
	 * XXX
	 * We've successfully parsed the manifest.  Now we need to
	 * allocate CPort Id's for connecting to the CPorts found on
	 * other modules.  For each of these, establish a connection
	 * between the local and remote CPorts (including
	 * configuring the switch to allow them to communicate).
	 */

	return;

err_module:
	gb_module_destroy(gmod);
}

void gb_remove_module(struct greybus_host_device *hd, u8 module_id)
{
	struct gb_module *gmod = gb_module_find(hd, module_id);

	if (gmod)
		gb_module_destroy(gmod);
	else
		dev_err(hd->parent, "module id %d not found\n", module_id);
}

void gb_remove_modules(struct greybus_host_device *hd)
{
	struct gb_module *gmod, *temp;

	list_for_each_entry_safe(gmod, temp, &hd->modules, links)
		gb_module_destroy(gmod);
}
