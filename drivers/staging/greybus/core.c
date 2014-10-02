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

static int greybus_module_match(struct device *dev, struct device_driver *drv)
{
	struct greybus_driver *driver = to_greybus_driver(dev->driver);
	struct gb_module *gmod = to_gb_module(dev);
	const struct greybus_module_id *id;

	id = gb_module_match_id(gmod, driver->id_table);
	if (id)
		return 1;
	/* FIXME - Dyanmic ids? */
	return 0;
}

static int greybus_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	/* struct gb_module *gmod = to_gb_module(dev); */

	/* FIXME - add some uevents here... */
	return 0;
}

static struct bus_type greybus_bus_type = {
	.name =		"greybus",
	.match =	greybus_module_match,
	.uevent =	greybus_uevent,
};

static int greybus_probe(struct device *dev)
{
	struct greybus_driver *driver = to_greybus_driver(dev->driver);
	struct gb_module *gmod = to_gb_module(dev);
	const struct greybus_module_id *id;
	int retval;

	/* match id */
	id = gb_module_match_id(gmod, driver->id_table);
	if (!id)
		return -ENODEV;

	retval = driver->probe(gmod, id);
	if (retval)
		return retval;

	return 0;
}

static int greybus_remove(struct device *dev)
{
	struct greybus_driver *driver = to_greybus_driver(dev->driver);
	struct gb_module *gmod = to_gb_module(dev);

	driver->disconnect(gmod);
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
	struct gb_module *gmod = to_gb_module(dev);
	int i;

	for (i = 0; i < gmod->num_strings; ++i)
		kfree(gmod->string[i]);
	kfree(gmod);
}


static struct device_type greybus_module_type = {
	.name =		"greybus_module",
	.release =	greybus_module_release,
};

/* XXX
 * This needs to be driven by the list of functions that the
 * manifest says are present.
 */
static int gb_init_subdevs(struct gb_module *gmod,
			   const struct greybus_module_id *id)
{
	int retval;

	/* Allocate all of the different "sub device types" for this device */

	/* XXX
	 * Decide what exactly we should get supplied for the i2c
	 * probe, and then work that back to what should be present
	 * in the manifest.
	 */
	retval = gb_i2c_probe(gmod, id);
	if (retval)
		goto error_i2c;

	retval = gb_gpio_probe(gmod, id);
	if (retval)
		goto error_gpio;

	retval = gb_sdio_probe(gmod, id);
	if (retval)
		goto error_sdio;

	retval = gb_tty_probe(gmod, id);
	if (retval)
		goto error_tty;

	retval = gb_battery_probe(gmod, id);
	if (retval)
		goto error_battery;
	return 0;

error_battery:
	gb_tty_disconnect(gmod);

error_tty:
	gb_sdio_disconnect(gmod);

error_sdio:
	gb_gpio_disconnect(gmod);

error_gpio:
	gb_i2c_disconnect(gmod);

error_i2c:
	return retval;
}

static const struct greybus_module_id fake_greybus_module_id = {
	GREYBUS_DEVICE(0x42, 0x42)
};

static int create_string(struct gb_module *gmod,
			 struct greybus_descriptor_string *string,
			 size_t desc_size)
{
	int string_size;
	struct gmod_string *gmod_string;

	if (gmod->num_strings == MAX_STRINGS_PER_MODULE) {
		dev_err(gmod->dev.parent,
			"too many strings for this module!\n");
		return -EINVAL;
	}

	if (desc_size < sizeof(*string)) {
		dev_err(gmod->dev.parent, "invalid string header size %zu\n",
			desc_size);
		return -EINVAL;
	}

	string_size = string->length;
	gmod_string = kzalloc(sizeof(*gmod_string) + string_size + 1, GFP_KERNEL);
	if (!gmod_string)
		return -ENOMEM;

	gmod_string->length = string_size;
	gmod_string->id = string->id;
	memcpy(&gmod_string->string, &string->string, string_size);

	gmod->string[gmod->num_strings] = gmod_string;
	gmod->num_strings++;

	return 0;
}

static int create_cport(struct gb_module *gmod,
			struct greybus_descriptor_cport *cport,
			size_t desc_size)
{
	if (gmod->num_cports == MAX_CPORTS_PER_MODULE) {
		dev_err(gmod->dev.parent, "too many cports for this module!\n");
		return -EINVAL;
	}

	if (desc_size != sizeof(*cport)) {
		dev_err(gmod->dev.parent,
			"invalid cport descriptor size %zu\n", desc_size);
		return -EINVAL;
	}

	gmod->cport_ids[gmod->num_cports] = le16_to_cpu(cport->id);
	gmod->num_cports++;

	return 0;
}

/**
 * gb_add_module
 *
 * Pass in a buffer that _should_ contain a Greybus module manifest
 * and register a greybus device structure with the kernel core.
 */
void gb_add_module(struct greybus_host_device *hd, u8 module_id,
		   u8 *data, int size)
{
	struct gb_module *gmod;
	struct greybus_manifest *manifest;
	int retval;

	/*
	 * Parse the manifest and build up our data structures
	 * representing what's in it.
	 */
	gmod = gb_manifest_parse(data, size);
	if (!gmod) {
		dev_err(hd->parent, "manifest error\n");
		return;
	}

	gmod->dev.parent = hd->parent;
	gmod->dev.driver = NULL;
	gmod->dev.bus = &greybus_bus_type;
	gmod->dev.type = &greybus_module_type;
	gmod->dev.groups = greybus_module_groups;
	gmod->dev.dma_mask = hd->parent->dma_mask;
	device_initialize(&gmod->dev);
	dev_set_name(&gmod->dev, "%d", module_id);

	size -= sizeof(manifest->header);
	data += sizeof(manifest->header);
	while (size > 0) {
		struct greybus_descriptor *desc;
		u16 desc_size;
		size_t data_size;

		if (size < sizeof(desc->header)) {
			dev_err(hd->parent, "remaining size %d too small\n",
				size);
			goto error;
		}
		desc = (struct greybus_descriptor *)data;
		desc_size = le16_to_cpu(desc->header.size);
		if (size < desc_size) {
			dev_err(hd->parent, "descriptor size %d too big\n",
				desc_size);
			goto error;
		}
		data_size = (size_t)desc_size - sizeof(desc->header);

		switch (le16_to_cpu(desc->header.type)) {
		case GREYBUS_TYPE_DEVICE:
			break;

		case GREYBUS_TYPE_MODULE:
			break;

		case GREYBUS_TYPE_STRING:
			retval = create_string(gmod, &desc->string, data_size);
			break;

		case GREYBUS_TYPE_CPORT:
			retval = create_cport(gmod, &desc->cport, data_size);
			break;

		case GREYBUS_TYPE_INVALID:
		default:
			dev_err(hd->parent, "invalid descriptor type %d\n",
				desc->header.type);
			goto error;
		}
		if (retval)
			goto error;
		size -= desc_size;
		data += desc_size;
	}

	retval = gb_init_subdevs(gmod, &fake_greybus_module_id);
	if (retval)
		goto error;

	// FIXME device_add(&gmod->dev);

	//return gmod;
	return;
error:
	put_device(&gmod->dev);
	greybus_module_release(&gmod->dev);
}

void gb_remove_module(struct greybus_host_device *hd, u8 module_id)
{
	// FIXME should be the remove_device call...
}

void greybus_remove_device(struct gb_module *gmod)
{
	/* tear down all of the "sub device types" for this device */
	gb_i2c_disconnect(gmod);
	gb_gpio_disconnect(gmod);
	gb_sdio_disconnect(gmod);
	gb_tty_disconnect(gmod);
	gb_battery_disconnect(gmod);

	// FIXME - device_remove(&gmod->dev);
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
	hd->parent = parent;
	hd->driver = driver;
	INIT_LIST_HEAD(&hd->modules);

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

	retval = gb_ap_init();
	if (retval) {
		pr_err("gb_ap_init failed\n");
		goto error_ap;
	}

	retval = gb_gbuf_init();
	if (retval) {
		pr_err("gb_gbuf_init failed\n");
		goto error_gbuf;
	}

	retval = gb_tty_init();
	if (retval) {
		pr_err("gb_tty_init failed\n");
		goto error_tty;
	}

	return 0;

error_tty:
	gb_gbuf_exit();

error_gbuf:
	gb_ap_exit();

error_ap:
	bus_unregister(&greybus_bus_type);

error_bus:
	gb_debugfs_cleanup();

	return retval;
}

static void __exit gb_exit(void)
{
	gb_tty_exit();
	gb_gbuf_exit();
	gb_ap_exit();
	bus_unregister(&greybus_bus_type);
	gb_debugfs_cleanup();
}

module_init(gb_init);
module_exit(gb_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Greg Kroah-Hartman <gregkh@linuxfoundation.org>");
