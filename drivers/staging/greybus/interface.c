/*
 * Greybus interface code
 *
 * Copyright 2014 Google Inc.
 * Copyright 2014 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#include "greybus.h"

/* interface sysfs attributes */
#define gb_interface_attr(field, type)					\
static ssize_t field##_show(struct device *dev,				\
			    struct device_attribute *attr,		\
			    char *buf)					\
{									\
	struct gb_interface *intf = to_gb_interface(dev);		\
	return sprintf(buf, "%"#type"\n", intf->field);			\
}									\
static DEVICE_ATTR_RO(field)

gb_interface_attr(vendor, x);
gb_interface_attr(product, x);
gb_interface_attr(unique_id, llX);
gb_interface_attr(vendor_string, s);
gb_interface_attr(product_string, s);

static struct attribute *interface_attrs[] = {
	&dev_attr_vendor.attr,
	&dev_attr_product.attr,
	&dev_attr_unique_id.attr,
	&dev_attr_vendor_string.attr,
	&dev_attr_product_string.attr,
	NULL,
};
ATTRIBUTE_GROUPS(interface);


/* XXX This could be per-host device */
static DEFINE_SPINLOCK(gb_modules_lock);

static int gb_interface_match_one_id(struct gb_interface *intf,
				     const struct greybus_interface_id *id)
{
	if ((id->match_flags & GREYBUS_ID_MATCH_VENDOR) &&
	    (id->vendor != intf->vendor))
		return 0;

	if ((id->match_flags & GREYBUS_ID_MATCH_PRODUCT) &&
	    (id->product != intf->product))
		return 0;

	if ((id->match_flags & GREYBUS_ID_MATCH_SERIAL) &&
	    (id->unique_id != intf->unique_id))
		return 0;

	return 1;
}

const struct greybus_interface_id *
gb_interface_match_id(struct gb_interface *intf,
		      const struct greybus_interface_id *id)
{
	if (id == NULL)
		return NULL;

	for (; id->vendor || id->product || id->unique_id ||
			id->driver_info; id++) {
		if (gb_interface_match_one_id(intf, id))
			return id;
	}

	return NULL;
}

struct gb_interface *gb_interface_find(struct greybus_host_device *hd,
				       u8 module_id)
{
	struct gb_interface *intf;

	list_for_each_entry(intf, &hd->interfaces, links)
		if (intf->module_id == module_id)
			return intf;

	return NULL;
}

static void greybus_interface_release(struct device *dev)
{
	struct gb_interface *intf = to_gb_interface(dev);

	kfree(intf);
}

struct device_type greybus_interface_type = {
	.name =		"greybus_interface",
	.release =	greybus_interface_release,
};

/*
 * A Greybus module represents a user-replicable component on an Ara
 * phone.  An interface is the physical connection on that module.  A
 * module may have more than one interface.
 *
 * Create a gb_interface structure to represent a discovered module.
 * The position within the Endo is encoded in the "module_id" argument.
 * Returns a pointer to the new module or a null pointer if a
 * failure occurs due to memory exhaustion.
 */
static struct gb_interface *gb_interface_create(struct greybus_host_device *hd,
						u8 module_id)
{
	struct gb_interface *intf;
	int retval;

	intf = gb_interface_find(hd, module_id);
	if (intf) {
		dev_err(hd->parent, "Duplicate module id %d will not be created\n",
			module_id);
		return NULL;
	}

	intf = kzalloc(sizeof(*intf), GFP_KERNEL);
	if (!intf)
		return NULL;

	intf->hd = hd;		/* XXX refcount? */
	intf->module_id = module_id;
	INIT_LIST_HEAD(&intf->bundles);

	intf->dev.parent = hd->parent;
	intf->dev.bus = &greybus_bus_type;
	intf->dev.type = &greybus_interface_type;
	intf->dev.groups = interface_groups;
	intf->dev.dma_mask = hd->parent->dma_mask;
	device_initialize(&intf->dev);
	dev_set_name(&intf->dev, "%d", module_id);

	retval = device_add(&intf->dev);
	if (retval) {
		pr_err("failed to add module device for id 0x%02hhx\n",
			module_id);
		put_device(&intf->dev);
		kfree(intf);
		return NULL;
	}

	spin_lock_irq(&gb_modules_lock);
	list_add_tail(&intf->links, &hd->interfaces);
	spin_unlock_irq(&gb_modules_lock);

	return intf;
}

/*
 * Tear down a previously set up module.
 */
static void gb_interface_destroy(struct gb_interface *intf)
{
	if (WARN_ON(!intf))
		return;

	spin_lock_irq(&gb_modules_lock);
	list_del(&intf->links);
	spin_unlock_irq(&gb_modules_lock);

	gb_bundle_destroy(intf);

	kfree(intf->product_string);
	kfree(intf->vendor_string);
	/* kref_put(module->hd); */

	device_del(&intf->dev);
}

/**
 * gb_add_interface
 *
 * Pass in a buffer that _should_ contain a Greybus module manifest
 * and register a greybus device structure with the kernel core.
 */
void gb_add_interface(struct greybus_host_device *hd, u8 module_id,
		      u8 *data, int size)
{
	struct gb_interface *intf;

	intf = gb_interface_create(hd, module_id);
	if (!intf) {
		dev_err(hd->parent, "failed to create interface\n");
		return;
	}

	/*
	 * Parse the manifest and build up our data structures
	 * representing what's in it.
	 */
	if (!gb_manifest_parse(intf, data, size)) {
		dev_err(hd->parent, "manifest error\n");
		goto err_parse;
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

err_parse:
	gb_interface_destroy(intf);
}

void gb_remove_interface(struct greybus_host_device *hd, u8 module_id)
{
	struct gb_interface *intf = gb_interface_find(hd, module_id);

	if (intf)
		gb_interface_destroy(intf);
	else
		dev_err(hd->parent, "interface id %d not found\n", module_id);
}

void gb_remove_interfaces(struct greybus_host_device *hd)
{
	struct gb_interface *intf, *temp;

	list_for_each_entry_safe(intf, temp, &hd->interfaces, links)
		gb_interface_destroy(intf);
}
