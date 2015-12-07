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

gb_interface_attr(interface_id, u);
gb_interface_attr(vendor_id, x);
gb_interface_attr(product_id, x);
gb_interface_attr(vendor_string, s);
gb_interface_attr(product_string, s);

static struct attribute *interface_attrs[] = {
	&dev_attr_interface_id.attr,
	&dev_attr_vendor_id.attr,
	&dev_attr_product_id.attr,
	&dev_attr_vendor_string.attr,
	&dev_attr_product_string.attr,
	NULL,
};
ATTRIBUTE_GROUPS(interface);


/* XXX This could be per-host device */
static DEFINE_SPINLOCK(gb_interfaces_lock);

// FIXME, odds are you don't want to call this function, rework the caller to
// not need it please.
struct gb_interface *gb_interface_find(struct gb_host_device *hd,
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

	kfree(intf->product_string);
	kfree(intf->vendor_string);

	kfree(intf);
}

struct device_type greybus_interface_type = {
	.name =		"greybus_interface",
	.release =	gb_interface_release,
};

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
struct gb_interface *gb_interface_create(struct gb_host_device *hd,
					 u8 interface_id)
{
	struct gb_interface *intf;
	int retval;

	intf = kzalloc(sizeof(*intf), GFP_KERNEL);
	if (!intf)
		return NULL;

	intf->hd = hd;		/* XXX refcount? */
	intf->interface_id = interface_id;
	INIT_LIST_HEAD(&intf->bundles);
	INIT_LIST_HEAD(&intf->manifest_descs);

	/* Invalid device id to start with */
	intf->device_id = GB_DEVICE_ID_BAD;

	intf->dev.parent = &hd->dev;
	intf->dev.bus = &greybus_bus_type;
	intf->dev.type = &greybus_interface_type;
	intf->dev.groups = interface_groups;
	intf->dev.dma_mask = hd->dev.dma_mask;
	device_initialize(&intf->dev);
	dev_set_name(&intf->dev, "%d-%d", hd->bus_id, interface_id);

	retval = device_add(&intf->dev);
	if (retval) {
		pr_err("failed to add interface %u\n", interface_id);
		goto free_intf;
	}

	spin_lock_irq(&gb_interfaces_lock);
	list_add(&intf->links, &hd->interfaces);
	spin_unlock_irq(&gb_interfaces_lock);

	return intf;

free_intf:
	put_device(&intf->dev);
	return NULL;
}

/*
 * Tear down a previously set up interface.
 */
void gb_interface_remove(struct gb_interface *intf)
{
	struct gb_bundle *bundle;
	struct gb_bundle *next;

	spin_lock_irq(&gb_interfaces_lock);
	list_del(&intf->links);
	spin_unlock_irq(&gb_interfaces_lock);

	list_for_each_entry_safe(bundle, next, &intf->bundles, links)
		gb_bundle_destroy(bundle);

	if (intf->control)
		gb_connection_destroy(intf->control->connection);

	device_unregister(&intf->dev);
}

void gb_interfaces_remove(struct gb_host_device *hd)
{
	struct gb_interface *intf, *temp;

	list_for_each_entry_safe(intf, temp, &hd->interfaces, links)
		gb_interface_remove(intf);
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
	struct gb_connection *connection;
	int ret, size;
	void *manifest;

	intf->device_id = device_id;

	/* Establish control CPort connection */
	connection = gb_connection_create_dynamic(intf, NULL,
						GB_CONTROL_CPORT_ID,
						GREYBUS_PROTOCOL_CONTROL);
	if (!connection) {
		dev_err(&intf->dev, "failed to create control connection\n");
		return -ENOMEM;
	}

	ret = gb_connection_init(connection);
	if (ret) {
		gb_connection_destroy(connection);
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
