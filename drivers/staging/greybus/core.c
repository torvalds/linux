/*
 * Greybus "Core"
 *
 * Copyright 2014 Google Inc.
 *
 * Released under the GPLv2 only.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/types.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/device.h>

#include "greybus.h"

/* Allow greybus to be disabled at boot if needed */
static bool nogreybus;
#ifdef MODULE
module_param(nogreybus, bool, 0444);
#else
core_param(nogreybus, bool, 0444);
#endif
int greybus_disabled(void)
{
	return nogreybus;
}
EXPORT_SYMBOL_GPL(greybus_disabled);

static int greybus_match_one_id(struct greybus_device *gdev,
				const struct greybus_module_id *id)
{
	struct greybus_descriptor_module_id *module_id;
	struct greybus_descriptor_serial_number *serial_num;

	module_id = &gdev->module_id;
	serial_num = &gdev->serial_number;

	if ((id->match_flags & GREYBUS_DEVICE_ID_MATCH_VENDOR) &&
	    (id->vendor != le16_to_cpu(module_id->vendor)))
		return 0;

	if ((id->match_flags & GREYBUS_DEVICE_ID_MATCH_PRODUCT) &&
	    (id->product != le16_to_cpu(module_id->product)))
		return 0;

	if ((id->match_flags & GREYBUS_DEVICE_ID_MATCH_SERIAL) &&
	    (id->serial_number != le64_to_cpu(serial_num->serial_number)))
		return 0;

	return 1;
}

static const struct greybus_module_id *greybus_match_id(
		struct greybus_device *gdev,
		const struct greybus_module_id *id)
{
	if (id == NULL)
		return NULL;

	for (; id->vendor || id->product || id->serial_number ||
	       id->driver_info ; id++) {
		if (greybus_match_one_id(gdev, id))
			return id;
	}

	return NULL;
}

static int greybus_device_match(struct device *dev, struct device_driver *drv)
{
	struct greybus_driver *driver = to_greybus_driver(dev->driver);
	struct greybus_device *gdev = to_greybus_device(dev);
	const struct greybus_module_id *id;

	id = greybus_match_id(gdev, driver->id_table);
	if (id)
		return 1;
	/* FIXME - Dyanmic ids? */
	return 0;
}

static int greybus_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	/* struct greybus_device *gdev = to_greybus_device(dev); */

	/* FIXME - add some uevents here... */
	return 0;
}

static struct bus_type greybus_bus_type = {
	.name =		"greybus",
	.match =	greybus_device_match,
	.uevent =	greybus_uevent,
};

static int greybus_probe(struct device *dev)
{
	struct greybus_driver *driver = to_greybus_driver(dev->driver);
	struct greybus_device *gdev = to_greybus_device(dev);
	const struct greybus_module_id *id;
	int retval;

	/* match id */
	id = greybus_match_id(gdev, driver->id_table);
	if (!id)
		return -ENODEV;

	retval = driver->probe(gdev, id);
	if (retval)
		return retval;

	return 0;
}

static int greybus_remove(struct device *dev)
{
	struct greybus_driver *driver = to_greybus_driver(dev->driver);
	struct greybus_device *gdev = to_greybus_device(dev);

	driver->disconnect(gdev);
	return 0;
}

int greybus_register_driver(struct greybus_driver *driver, struct module *owner,
		const char *mod_name)
{
	int retval;

	if (greybus_disabled())
		return -ENODEV;

	driver->driver.name = driver->name;
	driver->driver.probe = greybus_probe;
	driver->driver.remove = greybus_remove;
	driver->driver.owner = owner;
	driver->driver.mod_name = mod_name;

	retval = driver_register(&driver->driver);
	if (retval)
		return retval;

	pr_info("registered new driver %s\n", driver->name);
	return 0;
}
EXPORT_SYMBOL_GPL(greybus_register_driver);

void greybus_deregister(struct greybus_driver *driver)
{
	driver_unregister(&driver->driver);
}
EXPORT_SYMBOL_GPL(greybus_deregister);


static void greybus_module_release(struct device *dev)
{
	struct greybus_device *gdev = to_greybus_device(dev);
	int i;

	for (i = 0; i < gdev->num_strings; ++i)
		kfree(gdev->string[i]);
	for (i = 0; i < gdev->num_cports; ++i)
		kfree(gdev->cport[i]);
	kfree(gdev);
}


const u8 *greybus_string(struct greybus_device *gdev, int id)
{
	int i;
	struct gdev_string *string;

	if (!gdev)
		return NULL;

	for (i = 0; i < gdev->num_strings; ++i) {
		string = gdev->string[i];
		if (string->id == id)
			return &string->string[0];
	}
	return NULL;
}

static struct device_type greybus_module_type = {
	.name =		"greybus_module",
	.release =	greybus_module_release,
};

static int gb_init_subdevs(struct greybus_device *gdev,
			   const struct greybus_module_id *id)
{
	int retval;

	/* Allocate all of the different "sub device types" for this device */
	retval = gb_i2c_probe(gdev, id);
	if (retval)
		goto error_i2c;

	retval = gb_gpio_probe(gdev, id);
	if (retval)
		goto error_gpio;

	retval = gb_sdio_probe(gdev, id);
	if (retval)
		goto error_sdio;

	retval = gb_tty_probe(gdev, id);
	if (retval)
		goto error_tty;

	retval = gb_battery_probe(gdev, id);
	if (retval)
		goto error_battery;
	return 0;

error_battery:
	gb_tty_disconnect(gdev);

error_tty:
	gb_sdio_disconnect(gdev);

error_sdio:
	gb_gpio_disconnect(gdev);

error_gpio:
	gb_i2c_disconnect(gdev);

error_i2c:
	return retval;
}

static const struct greybus_module_id fake_gb_id = {
	GREYBUS_DEVICE(0x42, 0x42)
};

static int create_function(struct greybus_device *gdev,
			   struct greybus_descriptor_function *function,
			   size_t desc_size)
{
	if (desc_size != sizeof(*function)) {
		dev_err(gdev->dev.parent, "invalid function header size %zu\n",
			desc_size);
		return -EINVAL;
	}
	memcpy(&gdev->function, function, desc_size);
	return 0;
}

static int create_module_id(struct greybus_device *gdev,
			    struct greybus_descriptor_module_id *module_id,
			    size_t desc_size)
{
	if (desc_size != sizeof(*module_id)) {
		dev_err(gdev->dev.parent, "invalid module header size %zu\n",
			desc_size);
		return -EINVAL;
	}
	memcpy(&gdev->module_id, module_id, desc_size);
	return 0;
}

static int create_serial_number(struct greybus_device *gdev,
			        struct greybus_descriptor_serial_number *serial_num,
			        size_t desc_size)
{
	if (desc_size != sizeof(*serial_num)) {
		dev_err(gdev->dev.parent, "invalid serial number header size %zu\n",
			desc_size);
		return -EINVAL;
	}
	memcpy(&gdev->serial_number, serial_num, desc_size);
	return 0;
}

static int create_string(struct greybus_device *gdev,
			 struct greybus_descriptor_string *string,
			 size_t desc_size)
{
	int string_size;
	struct gdev_string *gdev_string;

	if ((gdev->num_strings + 1) >= MAX_STRINGS_PER_MODULE) {
		dev_err(gdev->dev.parent,
			"too many strings for this module!\n");
		return -EINVAL;
	}

	if (desc_size < sizeof(*string)) {
		dev_err(gdev->dev.parent, "invalid string header size %zu\n",
			desc_size);
		return -EINVAL;
	}

	string_size = le16_to_cpu(string->length);
	gdev_string = kzalloc(sizeof(*gdev_string) + string_size + 1, GFP_KERNEL);
	if (!gdev_string)
		return -ENOMEM;

	gdev_string->length = string_size;
	gdev_string->id = string->id;
	memcpy(&gdev_string->string, &string->string, string_size);

	gdev->string[gdev->num_strings] = gdev_string;
	gdev->num_strings++;

	return 0;
}

static int create_cport(struct greybus_device *gdev,
			struct greybus_descriptor_cport *cport,
			size_t desc_size)
{
	struct gdev_cport *gdev_cport;

	if ((gdev->num_cports + 1) >= MAX_CPORTS_PER_MODULE) {
		dev_err(gdev->dev.parent, "too many cports for this module!\n");
		return -EINVAL;
	}

	if (desc_size != sizeof(*cport)) {
		dev_err(gdev->dev.parent,
			"invalid serial number header size %zu\n", desc_size);
		return -EINVAL;
	}

	gdev_cport = kzalloc(sizeof(*gdev_cport), GFP_KERNEL);
	if (!gdev_cport)
		return -ENOMEM;

	gdev_cport->number = le16_to_cpu(cport->number);
	gdev_cport->size = le16_to_cpu(cport->size);
	gdev_cport->speed = cport->speed;

	gdev->cport[gdev->num_cports] = gdev_cport;
	gdev->num_cports++;

	return 0;
}

/**
 * greybus_new_module:
 *
 * Pass in a buffer that _should_ contain a Greybus module manifest
 * and spit out a greybus device structure.
 */
struct greybus_device *greybus_new_module(struct device *parent,
					  int module_number, u8 *data, int size)
{
	struct greybus_device *gdev;
	struct greybus_manifest *manifest;
	int retval;
	int overall_size;
	u8 version_major;
	u8 version_minor;

	/* we have to have at _least_ the manifest header */
	if (size <= sizeof(manifest->header))
		return NULL;

	gdev = kzalloc(sizeof(*gdev), GFP_KERNEL);
	if (!gdev)
		return NULL;

	gdev->module_number = module_number;
	gdev->dev.parent = parent;
	gdev->dev.driver = NULL;
	gdev->dev.bus = &greybus_bus_type;
	gdev->dev.type = &greybus_module_type;
	gdev->dev.groups = greybus_module_groups;
	gdev->dev.dma_mask = parent->dma_mask;
	device_initialize(&gdev->dev);
	dev_set_name(&gdev->dev, "%d", module_number);

	manifest = (struct greybus_manifest *)data;
	overall_size = le16_to_cpu(manifest->header.size);
	if (overall_size != size) {
		dev_err(parent, "size != manifest header size, %d != %d\n",
			size, overall_size);
		goto error;
	}

	version_major = manifest->header.version_major;
	version_minor = manifest->header.version_minor;

	// FIXME - check version major/minor here!

	size -= sizeof(manifest->header);
	data += sizeof(manifest->header);
	while (size > 0) {
		struct greybus_descriptor *desc;
		u16 desc_size;
		size_t data_size;

		if (size < sizeof(desc->header)) {
			dev_err(parent, "remaining size %d too small\n", size);
			goto error;
		}
		desc = (struct greybus_descriptor *)data;
		desc_size = le16_to_cpu(desc->header.size);
		if (size < desc_size) {
			dev_err(parent, "descriptor size %d too big\n",
				desc_size);
			goto error;
		}
		data_size = (size_t)desc_size - sizeof(desc->header);

		switch (le16_to_cpu(desc->header.type)) {
		case GREYBUS_TYPE_FUNCTION:
			retval = create_function(gdev, &desc->function,
						 data_size);
			break;

		case GREYBUS_TYPE_MODULE_ID:
			retval = create_module_id(gdev, &desc->module_id,
						  data_size);
			break;

		case GREYBUS_TYPE_SERIAL_NUMBER:
			retval = create_serial_number(gdev,
						      &desc->serial_number,
						      data_size);
			break;

		case GREYBUS_TYPE_STRING:
			retval = create_string(gdev, &desc->string, data_size);
			break;

		case GREYBUS_TYPE_CPORT:
			retval = create_cport(gdev, &desc->cport, data_size);
			break;

		case GREYBUS_TYPE_INVALID:
		default:
			dev_err(parent, "invalid descriptor type %d\n",
				desc->header.type);
			goto error;
		}
		if (retval)
			goto error;
		size -= desc_size;
		data += desc_size;
	}

	retval = gb_init_subdevs(gdev, &fake_gb_id);
	if (retval)
		goto error;

	// FIXME device_add(&gdev->dev);


	return gdev;
error:
	put_device(&gdev->dev);
	greybus_module_release(&gdev->dev);
	return NULL;
}

void greybus_remove_device(struct greybus_device *gdev)
{
	/* tear down all of the "sub device types" for this device */
	gb_i2c_disconnect(gdev);
	gb_gpio_disconnect(gdev);
	gb_sdio_disconnect(gdev);
	gb_tty_disconnect(gdev);
	gb_battery_disconnect(gdev);

	// FIXME - device_remove(&gdev->dev);
}

static DEFINE_MUTEX(hd_mutex);

static void free_hd(struct kref *kref)
{
	struct greybus_host_device *hd;

	hd = container_of(kref, struct greybus_host_device, kref);

	kfree(hd);
}

struct greybus_host_device *greybus_create_hd(struct greybus_host_driver *driver,
					      struct device *parent)
{
	struct greybus_host_device *hd;

	hd = kzalloc(sizeof(*hd) + driver->hd_priv_size, GFP_KERNEL);
	if (!hd)
		return NULL;

	kref_init(&hd->kref);

	return hd;
}
EXPORT_SYMBOL_GPL(greybus_create_hd);

void greybus_remove_hd(struct greybus_host_device *hd)
{
	kref_put_mutex(&hd->kref, free_hd, &hd_mutex);
}
EXPORT_SYMBOL_GPL(greybus_remove_hd);


static int __init gb_init(void)
{
	int retval;

	retval = gb_debugfs_init();
	if (retval) {
		pr_err("debugfs failed\n");
		return retval;
	}

	retval = bus_register(&greybus_bus_type);
	if (retval) {
		pr_err("bus_register failed\n");
		goto error_bus;
	}

	retval = gb_thread_init();
	if (retval) {
		pr_err("gb_thread_init failed\n");
		goto error_thread;
	}

	// FIXME - more gb core init goes here

	retval = gb_tty_init();
	if (retval) {
		pr_err("gb_tty_init failed\n");
		goto error_tty;
	}

	return 0;

error_tty:
	gb_thread_destroy();

error_thread:
	bus_unregister(&greybus_bus_type);

error_bus:
	gb_debugfs_cleanup();

	return retval;
}

static void __exit gb_exit(void)
{
	gb_tty_exit();
	bus_unregister(&greybus_bus_type);
	gb_debugfs_cleanup();
}

module_init(gb_init);
module_exit(gb_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Greg Kroah-Hartman <gregkh@linuxfoundation.org>");
