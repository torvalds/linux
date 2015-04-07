/*
 * Greybus module code
 *
 * Copyright 2014 Google Inc.
 * Copyright 2014 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#include "greybus.h"


/* module sysfs attributes */
static ssize_t epm_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	// FIXME
	// Implement something here when we have a working control protocol
	return sprintf(buf, "1\n");
}

static ssize_t epm_store(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t size)
{
	// FIXME
	// Implement something here when we have a working control protocol
	return 0;
}
static DEVICE_ATTR_RW(epm);

static ssize_t power_control_show(struct device *dev,
				  struct device_attribute *addr, char *buf)
{
	// FIXME
	// Implement something here when we have a working control protocol
	return sprintf(buf, "1\n");
}

static ssize_t power_control_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t size)
{
	// FIXME
	// Implement something here when we have a working control protocol
	return 0;
}
static DEVICE_ATTR_RW(power_control);

static ssize_t present_show(struct device *dev,
			    struct device_attribute *addr, char *buf)
{
	// FIXME
	// Implement something here when we have a working control protocol
	return sprintf(buf, "1\n");
}

static ssize_t present_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t size)
{
	// FIXME
	// Implement something here when we have a working control protocol
	return 0;
}
static DEVICE_ATTR_RW(present);

static struct attribute *module_attrs[] = {
	&dev_attr_epm.attr,
	&dev_attr_power_control.attr,
	&dev_attr_present.attr,
	NULL,
};
ATTRIBUTE_GROUPS(module);

static void greybus_module_release(struct device *dev)
{
	struct gb_module *module = to_gb_module(dev);

	kfree(module);
}

struct device_type greybus_module_type = {
	.name =		"greybus_module",
	.release =	greybus_module_release,
};

u8 get_module_id(u8 interface_id)
{
	/*
	 * FIXME:
	 *
	 * We should be able to find it from Endo ID passed during greybus
	 * control operation, while setting up AP.
	 */
	return interface_id;
}

static int module_find(struct device *dev, void *data)
{
	struct gb_module *module;
	u8 *module_id = data;

	if (!is_gb_module(dev))
		return 0;

	module = to_gb_module(dev);
	if (module->module_id == *module_id)
		return 1;

	return 0;
}

/*
 * Search the list of modules in the system.  If one is found, return it, with
 * the reference count incremented.
 */
static struct gb_module *gb_module_find(u8 module_id)
{
	struct device *dev;
	struct gb_module *module = NULL;

	dev = bus_find_device(&greybus_bus_type, NULL,
			      &module_id, module_find);
	if (dev)
		module = to_gb_module(dev);

	return module;
}

static struct gb_module *gb_module_create(struct greybus_host_device *hd,
					  u8 module_id)
{
	struct gb_module *module;
	int retval;

	module = kzalloc(sizeof(*module), GFP_KERNEL);
	if (!module)
		return NULL;

	module->module_id = module_id;
	module->refcount = 1;
	module->dev.parent = hd->parent;
	module->dev.bus = &greybus_bus_type;
	module->dev.type = &greybus_module_type;
	module->dev.groups = module_groups;
	module->dev.dma_mask = hd->parent->dma_mask;
	device_initialize(&module->dev);
	dev_set_name(&module->dev, "%d", module_id);

	retval = device_add(&module->dev);
	if (retval) {
		pr_err("failed to add module device for id 0x%02hhx\n",
			module_id);
		put_device(&module->dev);
		kfree(module);
		return NULL;
	}

	return module;
}

struct gb_module *gb_module_find_or_create(struct greybus_host_device *hd,
					   u8 module_id)
{
	struct gb_module *module;

	module = gb_module_find(module_id);
	if (module) {
		module->refcount++;
		return module;
	}

	return gb_module_create(hd, module_id);
}

void gb_module_remove(struct gb_module *module)
{
	if (!module)
		return;

	if (!--module->refcount)
		device_unregister(&module->dev);
}

