/*
 * Greybus bundles
 *
 * Copyright 2014-2015 Google Inc.
 * Copyright 2014-2015 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#include "greybus.h"
#include "greybus_trace.h"

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

	trace_gb_bundle_release(bundle);

	kfree(bundle->state);
	kfree(bundle->cport_desc);
	kfree(bundle);
}

#ifdef CONFIG_PM
static void gb_bundle_disable_all_connections(struct gb_bundle *bundle)
{
	struct gb_connection *connection;

	list_for_each_entry(connection, &bundle->connections, bundle_links)
		gb_connection_disable(connection);
}

static void gb_bundle_enable_all_connections(struct gb_bundle *bundle)
{
	struct gb_connection *connection;

	list_for_each_entry(connection, &bundle->connections, bundle_links)
		gb_connection_enable(connection);
}

static int gb_bundle_suspend(struct device *dev)
{
	struct gb_bundle *bundle = to_gb_bundle(dev);
	const struct dev_pm_ops *pm = dev->driver->pm;
	int ret;

	if (pm && pm->runtime_suspend) {
		ret = pm->runtime_suspend(&bundle->dev);
		if (ret)
			return ret;
	} else {
		gb_bundle_disable_all_connections(bundle);
	}

	ret = gb_control_bundle_suspend(bundle->intf->control, bundle->id);
	if (ret) {
		if (pm && pm->runtime_resume)
			ret = pm->runtime_resume(dev);
		else
			gb_bundle_enable_all_connections(bundle);

		return ret;
	}

	return 0;
}

static int gb_bundle_resume(struct device *dev)
{
	struct gb_bundle *bundle = to_gb_bundle(dev);
	const struct dev_pm_ops *pm = dev->driver->pm;
	int ret;

	ret = gb_control_bundle_resume(bundle->intf->control, bundle->id);
	if (ret)
		return ret;

	if (pm && pm->runtime_resume) {
		ret = pm->runtime_resume(dev);
		if (ret)
			return ret;
	} else {
		gb_bundle_enable_all_connections(bundle);
	}

	return 0;
}

static int gb_bundle_idle(struct device *dev)
{
	pm_runtime_mark_last_busy(dev);
	pm_request_autosuspend(dev);

	return 0;
}
#endif

static const struct dev_pm_ops gb_bundle_pm_ops = {
	SET_RUNTIME_PM_OPS(gb_bundle_suspend, gb_bundle_resume, gb_bundle_idle)
};

struct device_type greybus_bundle_type = {
	.name =		"greybus_bundle",
	.release =	gb_bundle_release,
	.pm =		&gb_bundle_pm_ops,
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

	if (bundle_id == BUNDLE_ID_NONE) {
		dev_err(&intf->dev, "can't use bundle id %u\n", bundle_id);
		return NULL;
	}

	/*
	 * Reject any attempt to reuse a bundle id.  We initialize
	 * these serially, so there's no need to worry about keeping
	 * the interface bundle list locked here.
	 */
	if (gb_bundle_find(intf, bundle_id)) {
		dev_err(&intf->dev, "duplicate bundle id %u\n", bundle_id);
		return NULL;
	}

	bundle = kzalloc(sizeof(*bundle), GFP_KERNEL);
	if (!bundle)
		return NULL;

	bundle->intf = intf;
	bundle->id = bundle_id;
	bundle->class = class;
	INIT_LIST_HEAD(&bundle->connections);

	bundle->dev.parent = &intf->dev;
	bundle->dev.bus = &greybus_bus_type;
	bundle->dev.type = &greybus_bundle_type;
	bundle->dev.groups = bundle_groups;
	bundle->dev.dma_mask = intf->dev.dma_mask;
	device_initialize(&bundle->dev);
	dev_set_name(&bundle->dev, "%s.%d", dev_name(&intf->dev), bundle_id);

	list_add(&bundle->links, &intf->bundles);

	trace_gb_bundle_create(bundle);

	return bundle;
}

int gb_bundle_add(struct gb_bundle *bundle)
{
	int ret;

	ret = device_add(&bundle->dev);
	if (ret) {
		dev_err(&bundle->dev, "failed to register bundle: %d\n", ret);
		return ret;
	}

	trace_gb_bundle_add(bundle);

	return 0;
}

/*
 * Tear down a previously set up bundle.
 */
void gb_bundle_destroy(struct gb_bundle *bundle)
{
	trace_gb_bundle_destroy(bundle);

	if (device_is_registered(&bundle->dev))
		device_del(&bundle->dev);

	list_del(&bundle->links);

	put_device(&bundle->dev);
}
