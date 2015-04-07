/*
 * Greybus endo code
 *
 * Copyright 2015 Google Inc.
 * Copyright 2014 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#include "greybus.h"

/* endo sysfs attributes */
static ssize_t serial_number_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct gb_endo *endo = to_gb_endo(dev);

	return sprintf(buf, "%s", &endo->svc.serial_number[0]);
}
static DEVICE_ATTR_RO(serial_number);

static ssize_t version_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct gb_endo *endo = to_gb_endo(dev);

	return sprintf(buf, "%s", &endo->svc.version[0]);
}
static DEVICE_ATTR_RO(version);

static struct attribute *endo_attrs[] = {
	&dev_attr_serial_number.attr,
	&dev_attr_version.attr,
	NULL,
};
static const struct attribute_group endo_group = {
	.attrs = endo_attrs,
	.name = "SVC",
};
static const struct attribute_group *endo_groups[] = {
	&endo_group,
	NULL,
};

static void greybus_endo_release(struct device *dev)
{
	struct gb_endo *endo = to_gb_endo(dev);

	kfree(endo);
}

struct device_type greybus_endo_type = {
	.name =		"greybus_endo",
	.release =	greybus_endo_release,
};


/*
 * Endo "types" have different module locations, these are tables based on those
 * types that list the module ids for the different locations.
 *
 * List must end with 0x00 in order to properly terminate the list.
 */
static u8 endo_0555[] = {
	0x01,
	0x03,
	0x05,
	0x07,
	0x08,
	0x0a,
	0x0c,
	0x00,
};


static int create_modules(struct gb_endo *endo)
{
	struct gb_module *module;
	u8 *endo_modules;
	int i;

	/* Depending on the endo type, create a bunch of different modules */
	switch (endo->type) {
	case 0x0555:
		endo_modules = &endo_0555[0];
		break;
	default:
		dev_err(&endo->dev, "Unknown endo type 0x%04x, aborting!",
			endo->type);
		return -EINVAL;
	}

	for (i = 0; endo_modules[i] != 0x00; ++i) {
//		module = gb_module_create(&endo->dev, endo_modules[i]);
		if (!module)
			return -EINVAL;
	}

	return 0;
}

static void remove_modules(struct gb_endo *endo)
{
	/*
	 * We really don't care how many modules have been created, or what the
	 * configuration of them are, let's just enumerate over everything in
	 * the system and delete all found modules.
	 */

}

struct gb_endo *gb_endo_create(struct greybus_host_device *hd)
{
	struct gb_endo *endo;
	int retval;

	endo = kzalloc(sizeof(*endo), GFP_KERNEL);
	if (!endo)
		return NULL;

	endo->dev.parent = hd->parent;
	endo->dev.bus = &greybus_bus_type;
	endo->dev.type = &greybus_endo_type;
	endo->dev.groups = endo_groups;
	endo->dev.dma_mask = hd->parent->dma_mask;
	device_initialize(&endo->dev);

	// FIXME - determine endo "type" from the SVC
	// Also get the version and serial number from the SVC, right now we are
	// using "fake" numbers.
	strcpy(&endo->svc.serial_number[0], "042");
	strcpy(&endo->svc.version[0], "0.0");
	endo->type = 0x0555;

	dev_set_name(&endo->dev, "endo-0x%04x", endo->type);
	retval = device_add(&endo->dev);
	if (retval) {
		dev_err(hd->parent, "failed to add endo device of type 0x%04x\n",
			endo->type);
		put_device(&endo->dev);
		kfree(endo);
		return NULL;
	}

	retval = create_modules(endo);
	if (retval) {
		gb_endo_remove(endo);
		return NULL;
	}

	return endo;
}

void gb_endo_remove(struct gb_endo *endo)
{
	if (!endo)
		return;

	/* remove all modules first */
	remove_modules(endo);

	device_unregister(&endo->dev);
}

