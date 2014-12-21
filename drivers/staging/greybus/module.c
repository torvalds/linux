/*
 * Greybus module code
 *
 * Copyright 2014 Google Inc.
 * Copyright 2014 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#include "greybus.h"


/*
 * List of modules in the system.  We really should just walk the list the
 * driver core provides us, but as we have lots of different things on the same
 * "bus" at the same time, a single list of modules is simplest for now.
 */
static DEFINE_SPINLOCK(gb_modules_lock);
static LIST_HEAD(module_list);

/* module sysfs attributes */
#define gb_module_attr(field, type)					\
static ssize_t field##_show(struct device *dev,				\
			    struct device_attribute *attr,		\
			    char *buf)					\
{									\
	struct gb_module *module = to_gb_module(dev);			\
	return sprintf(buf, "%"#type"\n", module->field);		\
}									\
static DEVICE_ATTR_RO(field)

// FIXME, do we really need this attribute?
gb_module_attr(module_id, x);

static ssize_t epm_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	// FIXME, implement something here
	return sprintf(buf, "1\n");
}

static ssize_t epm_store(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t size)
{
	// FIXME, implement something here.
	return 0;
}
static DEVICE_ATTR_RW(epm);

static struct attribute *module_attrs[] = {
	&dev_attr_module_id.attr,
	&dev_attr_epm.attr,
	NULL,
};
ATTRIBUTE_GROUPS(module);

static void greybus_module_release(struct device *dev)
{
	struct gb_module *module = to_gb_module(dev);

	spin_lock(&gb_modules_lock);
	list_del(&module->list);
	spin_unlock(&gb_modules_lock);

	kfree(module);
}

struct device_type greybus_module_type = {
	.name =		"greybus_module",
	.release =	greybus_module_release,
};

/*
 * Search the list of modules in the system.  If one is found, return it, with
 * the reference count incremented.
 */
static struct gb_module *gb_module_find(u8 module_id)
{
	struct gb_module *module;

	spin_lock(&gb_modules_lock);
	list_for_each_entry(module, &module_list, list) {
		if (module->module_id == module_id) {
			get_device(&module->dev);
			goto exit;
		}
	}
	module = NULL;
exit:
	spin_unlock(&gb_modules_lock);
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

	spin_lock(&gb_modules_lock);
	list_add_tail(&module->list, &module_list);
	spin_unlock(&gb_modules_lock);

	return module;
}

struct gb_module *gb_module_find_or_create(struct greybus_host_device *hd,
					   u8 module_id)
{
	struct gb_module *module;

	module = gb_module_find(module_id);
	if (module)
		return module;

	return gb_module_create(hd, module_id);
}

