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
	struct greybus_descriptor_module *module;

	module = &gmod->module;

	if ((id->match_flags & GREYBUS_DEVICE_ID_MATCH_VENDOR) &&
	    (id->vendor != le16_to_cpu(module->vendor)))
		return 0;

	if ((id->match_flags & GREYBUS_DEVICE_ID_MATCH_PRODUCT) &&
	    (id->product != le16_to_cpu(module->product)))
		return 0;

	if ((id->match_flags & GREYBUS_DEVICE_ID_MATCH_SERIAL) &&
	    (id->serial_number != le64_to_cpu(module->serial_number)))
		return 0;

	return 1;
}

const struct greybus_module_id *gb_module_match_id(struct gb_module *gmod,
				const struct greybus_module_id *id)
{
	if (id == NULL)
		return NULL;

	for (; id->vendor || id->product || id->serial_number ||
			id->driver_info; id++) {
		if (gb_module_match_one_id(gmod, id))
			return id;
	}

	return NULL;
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
	struct gb_module *module;

	module = kzalloc(sizeof(*module), GFP_KERNEL);
	if (!module)
		return NULL;

	module->hd = hd;		/* XXX refcount? */
	module->module_id = module_id;

	spin_lock_irq(&gb_modules_lock);
	list_add_tail(&module->links, &hd->modules);
	spin_unlock_irq(&gb_modules_lock);

	return module;
}

/*
 * Tear down a previously set up module.
 */
void gb_module_destroy(struct gb_module *module)
{
	if (WARN_ON(!module))
		return;

	spin_lock_irq(&gb_modules_lock);
	list_del(&module->links);
	spin_unlock_irq(&gb_modules_lock);

	/* kref_put(module->hd); */

	kfree(module);
}
