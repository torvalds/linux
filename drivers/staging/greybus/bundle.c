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

/* XXX This could be per-host device or per-module */
static DEFINE_SPINLOCK(gb_bundles_lock);

static int __bundle_bind_protocols(struct device *dev, void *data)
{
	struct gb_bundle *bundle;
	struct gb_connection *connection;

	if (!is_gb_bundle(dev))
		return 0;

	bundle = to_gb_bundle(dev);

	list_for_each_entry(connection, &bundle->connections, bundle_links) {
		gb_connection_bind_protocol(connection);
	}

	return 0;
}

/*
 * Walk all bundles in the system, and see if any connections are not bound to a
 * specific prototcol.  If they are not, then try to find one for it and bind it
 * to it.
 *
 * This is called after registering a new protocol.
 */
void gb_bundle_bind_protocols(void)
{
	bus_for_each_dev(&greybus_bus_type, NULL, NULL,
			 __bundle_bind_protocols);
}

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
		pr_err("duplicate bundle id 0x%02x\n", bundle_id);
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
		pr_err("failed to add bundle device for id 0x%02x\n",
			bundle_id);
		put_device(&bundle->dev);
		return NULL;
	}

	spin_lock_irq(&gb_bundles_lock);
	list_add(&bundle->links, &intf->bundles);
	spin_unlock_irq(&gb_bundles_lock);

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
	spin_lock_irq(&gb_bundles_lock);
	list_del(&bundle->links);
	spin_unlock_irq(&gb_bundles_lock);

	gb_bundle_connections_exit(bundle);
	device_unregister(&bundle->dev);
}

struct gb_bundle *gb_bundle_find(struct gb_interface *intf, u8 bundle_id)
{
	struct gb_bundle *bundle;

	spin_lock_irq(&gb_bundles_lock);
	list_for_each_entry(bundle, &intf->bundles, links)
		if (bundle->id == bundle_id)
			goto found;
	bundle = NULL;
found:
	spin_unlock_irq(&gb_bundles_lock);

	return bundle;
}
