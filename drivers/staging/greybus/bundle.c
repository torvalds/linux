/*
 * Greybus bundles
 *
 * Copyright 2014 Google Inc.
 * Copyright 2014 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#include "greybus.h"

static void gb_bundle_connections_exit(struct gb_bundle *bundle);
static int gb_bundle_connections_init(struct gb_bundle *bundle);


static ssize_t device_id_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct gb_bundle *bundle = to_gb_bundle(dev);

	return sprintf(buf, "%d\n", bundle->device_id);
}
static DEVICE_ATTR_RO(device_id);

static ssize_t class_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct gb_bundle *bundle = to_gb_bundle(dev);

	return sprintf(buf, "%d\n", bundle->class);
}
static DEVICE_ATTR_RO(class);

static struct attribute *bundle_attrs[] = {
	&dev_attr_device_id.attr,
	&dev_attr_class.attr,
	NULL,
};

ATTRIBUTE_GROUPS(bundle);

static void gb_bundle_release(struct device *dev)
{
	struct gb_bundle *bundle = to_gb_bundle(dev);

	kfree(bundle);
}

struct device_type greybus_bundle_type = {
	.name =		"greybus_bundle",
	.release =	gb_bundle_release,
};

static int gb_bundle_match_one_id(struct gb_bundle *bundle,
				     const struct greybus_bundle_id *id)
{
	if ((id->match_flags & GREYBUS_ID_MATCH_VENDOR) &&
	    (id->vendor != bundle->intf->vendor))
		return 0;

	if ((id->match_flags & GREYBUS_ID_MATCH_PRODUCT) &&
	    (id->product != bundle->intf->product))
		return 0;

	if ((id->match_flags & GREYBUS_ID_MATCH_SERIAL) &&
	    (id->unique_id != bundle->intf->unique_id))
		return 0;

	if ((id->match_flags & GREYBUS_ID_MATCH_CLASS) &&
	    (id->class != bundle->class))
		return 0;

	return 1;
}

const struct greybus_bundle_id *
gb_bundle_match_id(struct gb_bundle *bundle,
		   const struct greybus_bundle_id *id)
{
	if (id == NULL)
		return NULL;

	for (; id->vendor || id->product || id->unique_id || id->class ||
	       id->driver_info; id++) {
		if (gb_bundle_match_one_id(bundle, id))
			return id;
	}

	return NULL;
}


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

	bundle = kzalloc(sizeof(*bundle), GFP_KERNEL);
	if (!bundle)
		return NULL;

	bundle->intf = intf;
	bundle->id = bundle_id;
	bundle->class = class;
	INIT_LIST_HEAD(&bundle->connections);

	/* Invalid device id to start with */
	bundle->device_id = GB_DEVICE_ID_BAD;

	/* Build up the bundle device structures and register it with the
	 * driver core */
	bundle->dev.parent = &intf->dev;
	bundle->dev.bus = &greybus_bus_type;
	bundle->dev.type = &greybus_bundle_type;
	bundle->dev.groups = bundle_groups;
	device_initialize(&bundle->dev);
	dev_set_name(&bundle->dev, "%s:%d", dev_name(&intf->dev), bundle_id);

	retval = device_add(&bundle->dev);
	if (retval) {
		pr_err("failed to add bundle device for id 0x%02hhx\n",
			bundle_id);
		put_device(&bundle->dev);
		kfree(bundle);
		return NULL;
	}

	spin_lock_irq(&gb_bundles_lock);
	list_add_tail(&bundle->links, &intf->bundles);
	spin_unlock_irq(&gb_bundles_lock);

	return bundle;
}

/*
 * Tear down a previously set up bundle.
 */
void gb_bundle_destroy(struct gb_interface *intf)
{
	LIST_HEAD(list);
	struct gb_bundle *bundle;
	struct gb_bundle *temp;

	if (WARN_ON(!intf))
		return;

	spin_lock_irq(&gb_bundles_lock);
	list_splice_init(&intf->bundles, &list);
	spin_unlock_irq(&gb_bundles_lock);

	list_for_each_entry_safe(bundle, temp, &list, links) {
		list_del(&bundle->links);
		gb_bundle_connections_exit(bundle);
		device_unregister(&bundle->dev);
	}
}

int gb_bundle_init(struct gb_bundle *bundle, u8 device_id)
{
	struct gb_interface *intf = bundle->intf;
	int ret;

	bundle->device_id = device_id;

	ret = svc_set_route_send(bundle, intf->hd);
	if (ret) {
		dev_err(intf->hd->parent, "failed to set route (%d)\n", ret);
		return ret;
	}

	ret = gb_bundle_connections_init(bundle);
	if (ret) {
		dev_err(intf->hd->parent, "interface bundle init error %d\n",
			ret);
		/* XXX clear route */
		return ret;
	}

	return 0;
}

int gb_bundles_init(struct gb_interface *intf, u8 device_id)
{
	struct gb_bundle *bundle;
	int ret = 0;

	list_for_each_entry(bundle, &intf->bundles, links) {
		ret = gb_bundle_init(bundle, device_id);
		if (ret) {
			dev_err(intf->hd->parent,
				"Failed to initialize bundle %hhu\n",
				bundle->id);
			break;
		}
	}

	return ret;
}

struct gb_bundle *gb_bundle_find(struct gb_interface *intf, u8 bundle_id)
{
	struct gb_bundle *bundle;

	spin_lock_irq(&gb_bundles_lock);
	list_for_each_entry(bundle, &intf->bundles, links)
		if (bundle->id == bundle_id) {
			spin_unlock_irq(&gb_bundles_lock);
			return bundle;
		}
	spin_unlock_irq(&gb_bundles_lock);

	return NULL;
}

static int gb_bundle_connections_init(struct gb_bundle *bundle)
{
	struct gb_connection *connection;
	int ret = 0;

	list_for_each_entry(connection, &bundle->connections, bundle_links) {
		ret = gb_connection_init(connection);
		if (ret)
			break;
	}

	return ret;
}

static void gb_bundle_connections_exit(struct gb_bundle *bundle)
{
	struct gb_connection *connection;
	struct gb_connection *next;

	list_for_each_entry_safe(connection, next, &bundle->connections,
				 bundle_links) {
		gb_connection_exit(connection);
		gb_connection_destroy(connection);
	}
}
