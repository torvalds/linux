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
#include <linux/kthread.h>
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
	return 0;

error_tty:
	gb_sdio_disconnect(gdev);

error_sdio:
	gb_gpio_disconnect(gdev);

error_gpio:
	gb_i2c_disconnect(gdev);

error_i2c:
	return retval;
}

static const struct greybus_module_id fake_gb_id =
	{ GREYBUS_DEVICE(0x42, 0x42) };

/**
 * greybus_new_device:
 *
 * Pass in a buffer that _should_ be a set of greybus descriptor fields and spit
 * out a greybus device structure.
 */
struct greybus_device *greybus_new_device(int module_number, u8 *data, int size)
{
	struct greybus_device *gdev;
	struct greybus_descriptor_block_header *block;
	struct greybus_descriptor *desc;
	int retval;
	int overall_size;
	int header_size;
	int desc_size;
	u8 version_major;
	u8 version_minor;

	/* we have to have at _least_ the block header */
	if (size <= sizeof(struct greybus_descriptor_block_header))
		return NULL;

	gdev = kzalloc(sizeof(*gdev), GFP_KERNEL);
	if (!gdev)
		return NULL;

	gdev->module_number = module_number;

	block = (struct greybus_descriptor_block_header *)data;
	overall_size = le16_to_cpu(block->size);
	if (overall_size != size) {
		pr_err("size != block header size, %d != %d\n", size,
				overall_size);
		goto error;
	}

	version_major = block->version_major;
	version_minor = block->version_minor;

	// FIXME - check version major/minor here!

	size -= sizeof(struct greybus_descriptor_block_header);
	data += sizeof(struct greybus_descriptor_block_header);
	while (size > 0) {
		desc = (struct greybus_descriptor *)data;
		desc_size = le16_to_cpu(desc->header.size);

		switch (desc->header.type) {
		case GREYBUS_TYPE_FUNCTION:
			header_size =
				sizeof(struct greybus_descriptor_function);
			if (desc_size != header_size) {
				pr_err("invalid function header size %d\n",
				       desc_size);
				goto error;
			}
			memcpy(&gdev->function, &desc->function, header_size);
			size -= header_size;
			data += header_size;
			break;

		case GREYBUS_TYPE_MODULE_ID:
			header_size =
				sizeof(struct greybus_descriptor_module_id);
			if (desc_size != header_size) {
				pr_err("invalid module header size %d\n",
				       desc_size);
				goto error;
			}
			memcpy(&gdev->module_id, &desc->module_id, header_size);
			size -= header_size;
			data += header_size;
			break;

		case GREYBUS_TYPE_SERIAL_NUMBER:
			header_size =
				sizeof(struct greybus_descriptor_serial_number);
			if (desc_size != header_size) {
				pr_err("invalid serial number header size %d\n",
				       desc_size);
				goto error;
			}
			memcpy(&gdev->serial_number, &desc->serial_number,
			       header_size);
			size -= header_size;
			data += header_size;
			break;

		case GREYBUS_TYPE_DEVICE_STRING:
		case GREYBUS_TYPE_CPORT:
		case GREYBUS_TYPE_INVALID:
		default:
			pr_err("invalid descriptor type %d\n", desc->header.type);
			goto error;
		}
#if 0
		struct greybus_descriptor_string	string;
		struct greybus_descriptor_cport		cport;
#endif
	}
	retval = gb_init_subdevs(gdev, &fake_gb_id);
	if (retval)
		goto error;
	return gdev;
error:
	kfree(gdev);
	return NULL;
}

void remove_device(struct greybus_device *gdev)
{
	/* tear down all of the "sub device types" for this device */
	gb_i2c_disconnect(gdev);
	gb_gpio_disconnect(gdev);
	gb_sdio_disconnect(gdev);
	gb_tty_disconnect(gdev);
}

static int __init gb_init(void)
{
	int retval;

	retval = gb_debugfs_init();
	if (retval)
		return retval;

	retval = bus_register(&greybus_bus_type);
	if (retval)
		goto error_bus;

	retval = gb_thread_init();
	if (retval)
		goto error_thread;

	// FIXME - more gb core init goes here

	retval = gb_tty_init();
	if (retval)
		goto error_tty;

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
