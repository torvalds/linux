/*
 * Greybus interface block code
 *
 * Copyright 2014 Google Inc.
 * Copyright 2014 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#include "greybus.h"

/* interface block sysfs attributes */
#define gb_ib_attr(field, type)					\
static ssize_t field##_show(struct device *dev,		\
				     struct device_attribute *attr,	\
				     char *buf)				\
{									\
	struct gb_interface_block *gb_ib = to_gb_interface_block(dev);	\
	return sprintf(buf, "%"#type"\n", gb_ib->field);		\
}									\
static DEVICE_ATTR_RO(field)

gb_ib_attr(vendor, x);
gb_ib_attr(product, x);
gb_ib_attr(unique_id, llX);
gb_ib_attr(vendor_string, s);
gb_ib_attr(product_string, s);

static struct attribute *interface_block_attrs[] = {
	&dev_attr_vendor.attr,
	&dev_attr_product.attr,
	&dev_attr_unique_id.attr,
	&dev_attr_vendor_string.attr,
	&dev_attr_product_string.attr,
	NULL,
};
ATTRIBUTE_GROUPS(interface_block);


/* XXX This could be per-host device */
static DEFINE_SPINLOCK(gb_modules_lock);

static int gb_ib_match_one_id(struct gb_interface_block *gb_ib,
			      const struct greybus_interface_block_id *id)
{
	if ((id->match_flags & GREYBUS_ID_MATCH_VENDOR) &&
	    (id->vendor != gb_ib->vendor))
		return 0;

	if ((id->match_flags & GREYBUS_ID_MATCH_PRODUCT) &&
	    (id->product != gb_ib->product))
		return 0;

	if ((id->match_flags & GREYBUS_ID_MATCH_SERIAL) &&
	    (id->unique_id != gb_ib->unique_id))
		return 0;

	return 1;
}

const struct greybus_interface_block_id *
gb_ib_match_id(struct gb_interface_block *gb_ib,
	       const struct greybus_interface_block_id *id)
{
	if (id == NULL)
		return NULL;

	for (; id->vendor || id->product || id->unique_id ||
			id->driver_info; id++) {
		if (gb_ib_match_one_id(gb_ib, id))
			return id;
	}

	return NULL;
}

struct gb_interface_block *gb_ib_find(struct greybus_host_device *hd, u8 module_id)
{
	struct gb_interface_block *gb_ib;

	list_for_each_entry(gb_ib, &hd->modules, links)
		if (gb_ib->module_id == module_id)
			return gb_ib;

	return NULL;
}

static void greybus_ib_release(struct device *dev)
{
	struct gb_interface_block *gb_ib = to_gb_interface_block(dev);

	kfree(gb_ib);
}

struct device_type greybus_interface_block_type = {
	.name =		"greybus_interface_block",
	.release =	greybus_ib_release,
};

/*
 * A Greybus module represents a user-replicable component on an Ara
 * phone.  An interface block is the physical connection on that module.  A
 * module may have more than one interface block.
 *
 * Create a gb_interface_block structure to represent a discovered module.
 * The position within the Endo is encoded in the "module_id" argument.
 * Returns a pointer to the new module or a null pointer if a
 * failure occurs due to memory exhaustion.
 */
static struct gb_interface_block *gb_ib_create(struct greybus_host_device *hd,
					       u8 module_id)
{
	struct gb_interface_block *gb_ib;
	int retval;

	gb_ib = gb_ib_find(hd, module_id);
	if (gb_ib) {
		dev_err(hd->parent, "Duplicate module id %d will not be created\n",
			module_id);
		return NULL;
	}

	gb_ib = kzalloc(sizeof(*gb_ib), GFP_KERNEL);
	if (!gb_ib)
		return NULL;

	gb_ib->hd = hd;		/* XXX refcount? */
	gb_ib->module_id = module_id;
	INIT_LIST_HEAD(&gb_ib->interfaces);

	gb_ib->dev.parent = hd->parent;
	gb_ib->dev.bus = &greybus_bus_type;
	gb_ib->dev.type = &greybus_interface_block_type;
	gb_ib->dev.groups = interface_block_groups;
	gb_ib->dev.dma_mask = hd->parent->dma_mask;
	device_initialize(&gb_ib->dev);
	dev_set_name(&gb_ib->dev, "%d", module_id);

	retval = device_add(&gb_ib->dev);
	if (retval) {
		pr_err("failed to add module device for id 0x%02hhx\n",
			module_id);
		put_device(&gb_ib->dev);
		kfree(gb_ib);
		return NULL;
	}

	spin_lock_irq(&gb_modules_lock);
	list_add_tail(&gb_ib->links, &hd->modules);
	spin_unlock_irq(&gb_modules_lock);

	return gb_ib;
}

/*
 * Tear down a previously set up module.
 */
static void gb_ib_destroy(struct gb_interface_block *gb_ib)
{
	if (WARN_ON(!gb_ib))
		return;

	spin_lock_irq(&gb_modules_lock);
	list_del(&gb_ib->links);
	spin_unlock_irq(&gb_modules_lock);

	gb_interface_destroy(gb_ib);

	kfree(gb_ib->product_string);
	kfree(gb_ib->vendor_string);
	/* kref_put(module->hd); */

	device_del(&gb_ib->dev);
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
	struct gb_interface_block *gb_ib;

	gb_ib = gb_ib_create(hd, module_id);
	if (!gb_ib) {
		dev_err(hd->parent, "failed to create interface block\n");
		return;
	}

	/*
	 * Parse the manifest and build up our data structures
	 * representing what's in it.
	 */
	if (!gb_manifest_parse(gb_ib, data, size)) {
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
	gb_ib_destroy(gb_ib);
}

void gb_remove_module(struct greybus_host_device *hd, u8 module_id)
{
	struct gb_interface_block *gb_ib = gb_ib_find(hd, module_id);

	if (gb_ib)
		gb_ib_destroy(gb_ib);
	else
		dev_err(hd->parent, "interface block id %d not found\n", module_id);
}

void gb_remove_modules(struct greybus_host_device *hd)
{
	struct gb_interface_block *gb_ib, *temp;

	list_for_each_entry_safe(gb_ib, temp, &hd->modules, links)
		gb_ib_destroy(gb_ib);
}
