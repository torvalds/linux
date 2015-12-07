/*
 * Greybus bundles
 *
 * Copyright 2014-2015 Google Inc.
 * Copyright 2014-2015 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#include "greybus.h"

static ssize_t bundle_class_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct gb_bundle *bundle = to_gb_bundle(dev);

	return sprintf(buf, "0x%02x\n", bundle->class);
}
static DEVICE_ATTR_RO(bundle_class);

static ssize_t bundle_id_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct gb_bundle *bundle = to_gb_bundle(dev);

	return sprintf(buf, "%u\n", bundle->id);
}
static DEVICE_ATTR_RO(bundle_id);

static ssize_t state_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct gb_bundle *bundle = to_gb_bundle(dev);

	if (bundle->state == NULL)
		return sprintf(buf, "\n");

	return sprintf(buf, "%s\n", bundle->state);
}

static ssize_t state_store(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t size)
{
	struct gb_bundle *bundle = to_gb_bundle(dev);

	kfree(bundle->state);
	bundle->state = kstrdup(buf, GFP_KERNEL);
	if (!bundle->state)
		return -ENOMEM;

	/* Tell userspace that the file contents changed */
	sysfs_notify(&bundle->dev.kobj, NULL, "state");

	return size;
}
static DEVICE_ATTR_RW(state);


static struct attribute *bundle_attrs[] = {
	&dev_attr_bundle_class.attr,
	&dev_attr_bundle_id.attr,
	&dev_attr_state.attr,
	NULL,
};

ATTRIBUTE_GROUPS(bundle);

static struct gb_bundle *gb_bundle_find(struct gb_interface *intf,
							u8 bundle_id)
{
	struct gb_bundle *bundle;

	list_for_each_entry(bundle, &intf->bundles, links) {
		if (bundle->id == bundle_id)
			return bundle;
	}

	return NULL;
}

static void gb_bundle_release(struct device *dev)
{
	struct gb_bundle *bundle = to_gb_bundle(dev);

	kfree(bundle->state);
	kfree(bundle);
}

struct device_type greybus_bundle_type = {
	.name =		"greybus_bundle",
	.release =	gb_bundle_release,
};

/*
 * Create a gb_bundle structure to represent a discovered
 * bundle.  Returns a pointer to the new bundle or a null
 * pointer if a failure occurs due to memory exhaustion.
 */
struct gb_bundle *gb_bundle_create(struct gb_interface *intf, u8 bundle_id,
				   u8 class)
{
	struct gb_bundle *bundle;
	int retval;

	/*
	 * Reject any attempt to reuse a bundle id.  We initialize
	 * these serially, so there's no need to worry about keeping
	 * the interface bundle list locked here.
	 */
	if (gb_bundle_find(intf, bundle_id)) {
		pr_err("duplicate bundle id %u\n", bundle_id);
		return NULL;
	}

	bundle = kzalloc(sizeof(*bundle), GFP_KERNEL);
	if (!bundle)
		return NULL;

	bundle->intf = intf;
	bundle->id = bundle_id;
	bundle->class = class;
	INIT_LIST_HEAD(&bundle->connections);

	/* Build up the bundle device structures and register it with the
	 * driver core */
	bundle->dev.parent = &intf->dev;
	bundle->dev.bus = &greybus_bus_type;
	bundle->dev.type = &greybus_bundle_type;
	bundle->dev.groups = bundle_groups;
	device_initialize(&bundle->dev);
	dev_set_name(&bundle->dev, "%s.%d", dev_name(&intf->dev), bundle_id);

	retval = device_add(&bundle->dev);
	if (retval) {
		pr_err("failed to add bundle device for id %u\n", bundle_id);
		put_device(&bundle->dev);
		return NULL;
	}

	list_add(&bundle->links, &intf->bundles);

	return bundle;
}

static void gb_bundle_connections_exit(struct gb_bundle *bundle)
{
	struct gb_connection *connection;
	struct gb_connection *next;

	list_for_each_entry_safe(connection, next, &bundle->connections,
				 bundle_links)
		gb_connection_destroy(connection);
}

/*
 * Tear down a previously set up bundle.
 */
void gb_bundle_destroy(struct gb_bundle *bundle)
{
	list_del(&bundle->links);

	gb_bundle_connections_exit(bundle);
	device_unregister(&bundle->dev);
}


