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
	return scnprintf(buf, PAGE_SIZE, "%"#type"\n", intf->field);	\
}									\
static DEVICE_ATTR_RO(field)

gb_interface_attr(device_id, d);
gb_interface_attr(vendor, x);
gb_interface_attr(product, x);
gb_interface_attr(unique_id, llX);
gb_interface_attr(vendor_string, s);
gb_interface_attr(product_string, s);

static struct attribute *interface_attrs[] = {
	&dev_attr_device_id.attr,
	&dev_attr_vendor.attr,
	&dev_attr_product.attr,
	&dev_attr_unique_id.attr,
	&dev_attr_vendor_string.attr,
	&dev_attr_product_string.attr,
	NULL,
};
ATTRIBUTE_GROUPS(interface);


/* XXX This could be per-host device */
static DEFINE_SPINLOCK(gb_interfaces_lock);

// FIXME, odds are you don't want to call this function, rework the caller to
// not need it please.
struct gb_interface *gb_interface_find(struct greybus_host_device *hd,
				       u8 interface_id)
{
	struct gb_interface *intf;

	list_for_each_entry(intf, &hd->interfaces, links)
		if (intf->interface_id == interface_id)
			return intf;

	return NULL;
}

static void gb_interface_release(struct device *dev)
{
	struct gb_interface *intf = to_gb_interface(dev);

	kfree(intf);
}

struct device_type greybus_interface_type = {
	.name =		"greybus_interface",
	.release =	gb_interface_release,
};

/*
 * Create kernel structures corresponding to a bundle and connection for
 * managing control/svc CPort.
 */
int gb_create_bundle_connection(struct gb_interface *intf, u8 class)
{
	struct gb_bundle *bundle;
	u32 ida_start, ida_end;
	u8 bundle_id, protocol_id;
	u16 cport_id;

	if (class == GREYBUS_CLASS_CONTROL) {
		protocol_id = GREYBUS_PROTOCOL_CONTROL;
		bundle_id = GB_CONTROL_BUNDLE_ID;
		cport_id = GB_CONTROL_CPORT_ID;
		ida_start = 0;
		ida_end = CPORT_ID_MAX;
	} else if (class == GREYBUS_CLASS_SVC) {
		protocol_id = GREYBUS_PROTOCOL_SVC;
		bundle_id = GB_SVC_BUNDLE_ID;
		cport_id = GB_SVC_CPORT_ID;
		ida_start = GB_SVC_CPORT_ID;
		ida_end = GB_SVC_CPORT_ID + 1;
	} else {
		WARN_ON(1);
		return -EINVAL;
	}

	bundle = gb_bundle_create(intf, bundle_id, class);
	if (!bundle)
		return -EINVAL;

	if (!gb_connection_create_range(bundle->intf->hd, bundle, &bundle->dev,
					cport_id, protocol_id, ida_start,
					ida_end))
		return -EINVAL;

	return 0;
}

/*
 * A Greybus module represents a user-replaceable component on an Ara
 * phone.  An interface is the physical connection on that module.  A
 * module may have more than one interface.
 *
 * Create a gb_interface structure to represent a discovered interface.
 * The position of interface within the Endo is encoded in "interface_id"
 * argument.
 *
 * Returns a pointer to the new interfce or a null pointer if a
 * failure occurs due to memory exhaustion.
 */
struct gb_interface *gb_interface_create(struct greybus_host_device *hd,
					 u8 interface_id)
{
	struct gb_module *module;
	struct gb_interface *intf;
	int retval;

	intf = gb_interface_find(hd, interface_id);
	if (intf) {
		dev_err(hd->parent, "Duplicate interface with interface-id: %d will not be created\n",
			interface_id);
		return NULL;
	}

	module = gb_module_find(hd, endo_get_module_id(hd->endo, interface_id));
	if (!module)
		return NULL;

	intf = kzalloc(sizeof(*intf), GFP_KERNEL);
	if (!intf)
		goto put_module;

	intf->hd = hd;		/* XXX refcount? */
	intf->module = module;
	intf->interface_id = interface_id;
	INIT_LIST_HEAD(&intf->bundles);
	INIT_LIST_HEAD(&intf->manifest_descs);

	/* Invalid device id to start with */
	intf->device_id = GB_DEVICE_ID_BAD;

	intf->dev.parent = &module->dev;
	intf->dev.bus = &greybus_bus_type;
	intf->dev.type = &greybus_interface_type;
	intf->dev.groups = interface_groups;
	intf->dev.dma_mask = hd->parent->dma_mask;
	device_initialize(&intf->dev);
	dev_set_name(&intf->dev, "%s:%d", dev_name(&module->dev), interface_id);

	retval = device_add(&intf->dev);
	if (retval) {
		pr_err("failed to add interface device for id 0x%02hhx\n",
		       interface_id);
		goto free_intf;
	}

	spin_lock_irq(&gb_interfaces_lock);
	list_add(&intf->links, &hd->interfaces);
	spin_unlock_irq(&gb_interfaces_lock);

	return intf;

free_intf:
	put_device(&intf->dev);
	kfree(intf);
put_module:
	put_device(&module->dev);
	return NULL;
}

/*
 * Tear down a previously set up module.
 */
static void interface_destroy(struct gb_interface *intf)
{
	struct gb_module *module;
	struct gb_bundle *bundle;
	struct gb_bundle *next;

	if (WARN_ON(!intf))
		return;

	spin_lock_irq(&gb_interfaces_lock);
	list_del(&intf->links);
	spin_unlock_irq(&gb_interfaces_lock);

	list_for_each_entry_safe(bundle, next, &intf->bundles, links)
		gb_bundle_destroy(bundle);

	kfree(intf->product_string);
	kfree(intf->vendor_string);

	module = intf->module;
	device_unregister(&intf->dev);
	put_device(&module->dev);
}

/**
 * gb_interface_init
 *
 * Create connection for control CPort and then request/parse manifest.
 * Finally initialize all the bundles to set routes via SVC and initialize all
 * connections.
 */
int gb_interface_init(struct gb_interface *intf, u8 device_id)
{
	int ret, size;
	void *manifest;

	intf->device_id = device_id;

	/* Establish control CPort connection */
	ret = gb_create_bundle_connection(intf, GREYBUS_CLASS_CONTROL);
	if (ret) {
		dev_err(&intf->dev, "Failed to create control CPort connection (%d)\n", ret);
		return ret;
	}

	/* Get manifest size using control protocol on CPort */
	size = gb_control_get_manifest_size_operation(intf);
	if (size <= 0) {
		dev_err(&intf->dev, "%s: Failed to get manifest size (%d)\n",
			__func__, size);
		if (size)
			return size;
		else
			return -EINVAL;
	}

	manifest = kmalloc(size, GFP_KERNEL);
	if (!manifest)
		return -ENOMEM;

	/* Get manifest using control protocol on CPort */
	ret = gb_control_get_manifest_operation(intf, manifest, size);
	if (ret) {
		dev_err(&intf->dev, "%s: Failed to get manifest\n", __func__);
		goto free_manifest;
	}

	/*
	 * Parse the manifest and build up our data structures representing
	 * what's in it.
	 */
	if (!gb_manifest_parse(intf, manifest, size)) {
		dev_err(&intf->dev, "%s: Failed to parse manifest\n", __func__);
		ret = -EINVAL;
		goto free_manifest;
	}

	/*
	 * XXX
	 * We've successfully parsed the manifest.  Now we need to
	 * allocate CPort Id's for connecting to the CPorts found on
	 * other modules.  For each of these, establish a connection
	 * between the local and remote CPorts (including
	 * configuring the switch to allow them to communicate).
	 */

free_manifest:
	kfree(manifest);
	return ret;
}

void gb_interface_remove(struct greybus_host_device *hd, u8 interface_id)
{
	struct gb_interface *intf = gb_interface_find(hd, interface_id);

	if (intf)
		interface_destroy(intf);
	else
		dev_err(hd->parent, "interface id %d not found\n",
			interface_id);
}

void gb_interfaces_remove(struct greybus_host_device *hd)
{
	struct gb_interface *intf, *temp;

	list_for_each_entry_safe(intf, temp, &hd->interfaces, links)
		interface_destroy(intf);
}
