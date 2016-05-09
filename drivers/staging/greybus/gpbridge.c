/*
 * Greybus GP Bridge driver
 *
 * Copyright 2014 Google Inc.
 * Copyright 2014 Linaro Ltd.
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
#include "gpbridge.h"

struct gpbridge_host {
	struct gb_bundle *bundle;
	struct list_head devices;
};

static DEFINE_IDA(gpbridge_id);

static ssize_t protocol_id_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct gpbridge_device *gpbdev = to_gpbridge_dev(dev);

	return sprintf(buf, "0x%02x\n", gpbdev->cport_desc->protocol_id);
}
static DEVICE_ATTR_RO(protocol_id);

static struct attribute *gpbdev_attrs[] = {
	&dev_attr_protocol_id.attr,
	NULL,
};

ATTRIBUTE_GROUPS(gpbdev);

static void gpbdev_release(struct device *dev)
{
	struct gpbridge_device *gpbdev = to_gpbridge_dev(dev);

	ida_simple_remove(&gpbridge_id, gpbdev->id);
	kfree(gpbdev);
}

struct device_type greybus_gpbdev_type = {
	.name	 =	"gpbridge_device",
	.release =	gpbdev_release,
};

static int gpbdev_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	struct gpbridge_device *gpbdev = to_gpbridge_dev(dev);
	struct greybus_descriptor_cport *cport_desc = gpbdev->cport_desc;
	struct gb_bundle *bundle = gpbdev->bundle;
	struct gb_interface *intf = bundle->intf;
	struct gb_module *module = intf->module;
	struct gb_host_device *hd = intf->hd;

	if (add_uevent_var(env, "BUS=%u", hd->bus_id))
		return -ENOMEM;
	if (add_uevent_var(env, "MODULE=%u", module->module_id))
		return -ENOMEM;
	if (add_uevent_var(env, "INTERFACE=%u", intf->interface_id))
		return -ENOMEM;
	if (add_uevent_var(env, "GREYBUS_ID=%08x/%08x",
			   intf->vendor_id, intf->product_id))
		return -ENOMEM;
	if (add_uevent_var(env, "BUNDLE=%u", gpbdev->bundle->id))
		return -ENOMEM;
	if (add_uevent_var(env, "BUNDLE_CLASS=%02x", bundle->class))
		return -ENOMEM;
	if (add_uevent_var(env, "GPBDEV_ID=%u", gpbdev->id))
		return -ENOMEM;
	if (add_uevent_var(env, "PROTOCOL_ID=%02x", cport_desc->protocol_id))
		return -ENOMEM;

	return 0;
}

static const struct gpbridge_device_id *
gpbdev_match_id(struct gpbridge_device *gpbdev, struct gpbridge_driver *gpbdrv)
{
	const struct gpbridge_device_id *id = gpbdrv->id_table;

	if (!id)
		return NULL;

	for (; id->protocol_id; id++)
		if (id->protocol_id == gpbdev->cport_desc->protocol_id)
			return id;

	return NULL;
}

static int gpbdev_match(struct device *dev, struct device_driver *drv)
{
	struct gpbridge_driver *gpbdrv = to_gpbridge_driver(drv);
	struct gpbridge_device *gpbdev = to_gpbridge_dev(dev);
	const struct gpbridge_device_id *id;

	id = gpbdev_match_id(gpbdev, gpbdrv);
	if (id)
		return 1;

	return 0;
}

static int gpbdev_probe(struct device *dev)
{
	struct gpbridge_driver *gpbdrv = to_gpbridge_driver(dev->driver);
	struct gpbridge_device *gpbdev = to_gpbridge_dev(dev);
	const struct gpbridge_device_id *id;

	id = gpbdev_match_id(gpbdev, gpbdrv);
	if (!id)
		return -ENODEV;

	return gpbdrv->probe(gpbdev, id);
}

static int gpbdev_remove(struct device *dev)
{
	struct gpbridge_driver *gpbdrv = to_gpbridge_driver(dev->driver);
	struct gpbridge_device *gpbdev = to_gpbridge_dev(dev);

	gpbdrv->remove(gpbdev);
	return 0;
}

static struct bus_type gpbridge_bus_type = {
	.name =		"gpbridge",
	.match =	gpbdev_match,
	.probe =	gpbdev_probe,
	.remove =	gpbdev_remove,
	.uevent =	gpbdev_uevent,
};

int gb_gpbridge_register_driver(struct gpbridge_driver *driver,
			     struct module *owner, const char *mod_name)
{
	int retval;

	if (greybus_disabled())
		return -ENODEV;

	driver->driver.bus = &gpbridge_bus_type;
	driver->driver.name = driver->name;
	driver->driver.owner = owner;
	driver->driver.mod_name = mod_name;

	retval = driver_register(&driver->driver);
	if (retval)
		return retval;

	pr_info("registered new driver %s\n", driver->name);
	return 0;
}
EXPORT_SYMBOL_GPL(gb_gpbridge_register_driver);

void gb_gpbridge_deregister_driver(struct gpbridge_driver *driver)
{
	driver_unregister(&driver->driver);
}
EXPORT_SYMBOL_GPL(gb_gpbridge_deregister_driver);

int gb_gpbridge_get_version(struct gb_connection *connection)
{
	struct gb_protocol_version_request request;
	struct gb_protocol_version_response response;
	int retval;

	request.major = 1;
	request.minor = 0;

	retval = gb_operation_sync(connection, GB_REQUEST_TYPE_PROTOCOL_VERSION,
				   &request, sizeof(request), &response,
				   sizeof(response));
	if (retval)
		return retval;

	/* FIXME - do proper version negotiation here someday... */

	connection->module_major = response.major;
	connection->module_minor = response.minor;

	dev_dbg(&connection->hd->dev, "%s: v%u.%u\n", connection->name,
		response.major, response.minor);

	return 0;
}
EXPORT_SYMBOL_GPL(gb_gpbridge_get_version);

static struct gpbridge_device *gb_gpbridge_create_dev(struct gb_bundle *bundle,
				struct greybus_descriptor_cport *cport_desc)
{
	struct gpbridge_device *gpbdev;
	int retval;
	int id;

	id = ida_simple_get(&gpbridge_id, 1, 0, GFP_KERNEL);
	if (id < 0)
		return ERR_PTR(id);

	gpbdev = kzalloc(sizeof(*gpbdev), GFP_KERNEL);
	if (!gpbdev) {
		ida_simple_remove(&gpbridge_id, id);
		return ERR_PTR(-ENOMEM);
	}

	gpbdev->id = id;
	gpbdev->bundle = bundle;
	gpbdev->cport_desc = cport_desc;
	gpbdev->dev.parent = &bundle->dev;
	gpbdev->dev.bus = &gpbridge_bus_type;
	gpbdev->dev.type = &greybus_gpbdev_type;
	gpbdev->dev.groups = gpbdev_groups;
	gpbdev->dev.dma_mask = bundle->dev.dma_mask;
	dev_set_name(&gpbdev->dev, "gpb%d", id);

	retval = device_register(&gpbdev->dev);
	if (retval) {
		put_device(&gpbdev->dev);
		return ERR_PTR(retval);
	}

	return gpbdev;
}

static void gb_gpbridge_disconnect(struct gb_bundle *bundle)
{
	struct gpbridge_host *gpb_host = greybus_get_drvdata(bundle);
	struct gpbridge_device *gpbdev, *temp;

	list_for_each_entry_safe(gpbdev, temp, &gpb_host->devices, list) {
		list_del(&gpbdev->list);
		device_unregister(&gpbdev->dev);
	}

	kfree(gpb_host);
}

static int gb_gpbridge_probe(struct gb_bundle *bundle,
			  const struct greybus_bundle_id *id)
{
	struct gpbridge_host *gpb_host;
	struct gpbridge_device *gpbdev;
	int i;

	if (bundle->num_cports == 0)
		return -ENODEV;

	gpb_host = kzalloc(sizeof(*gpb_host), GFP_KERNEL);
	if (!gpb_host)
		return -ENOMEM;

	gpb_host->bundle = bundle;
	INIT_LIST_HEAD(&gpb_host->devices);
	greybus_set_drvdata(bundle, gpb_host);

	/*
	 * Create a bunch of children devices, one per cport, and bind the
	 * bridged phy drivers to them.
	 */
	for (i = 0; i < bundle->num_cports; ++i) {
		gpbdev = gb_gpbridge_create_dev(bundle, &bundle->cport_desc[i]);
		if (IS_ERR(gpbdev)) {
			gb_gpbridge_disconnect(bundle);
			return PTR_ERR(gpbdev);
		}
		list_add(&gpbdev->list, &gpb_host->devices);
	}

	return 0;
}

static const struct greybus_bundle_id gb_gpbridge_id_table[] = {
	{ GREYBUS_DEVICE_CLASS(GREYBUS_CLASS_BRIDGED_PHY) },
	{ GREYBUS_DEVICE_CLASS(GREYBUS_CLASS_GPIO) },
	{ GREYBUS_DEVICE_CLASS(GREYBUS_CLASS_I2C) },
	{ GREYBUS_DEVICE_CLASS(GREYBUS_CLASS_PWM) },
	{ GREYBUS_DEVICE_CLASS(GREYBUS_CLASS_SDIO) },
	{ GREYBUS_DEVICE_CLASS(GREYBUS_CLASS_SPI) },
	{ GREYBUS_DEVICE_CLASS(GREYBUS_CLASS_UART) },
	{ GREYBUS_DEVICE_CLASS(GREYBUS_CLASS_USB) },
	{ },
};
MODULE_DEVICE_TABLE(greybus, gb_gpbridge_id_table);

static struct greybus_driver gb_gpbridge_driver = {
	.name		= "gpbridge",
	.probe		= gb_gpbridge_probe,
	.disconnect	= gb_gpbridge_disconnect,
	.id_table	= gb_gpbridge_id_table,
};

static int __init gpbridge_init(void)
{
	int retval;

	retval = bus_register(&gpbridge_bus_type);
	if (retval) {
		pr_err("gpbridge bus register failed (%d)\n", retval);
		return retval;
	}

	retval = greybus_register(&gb_gpbridge_driver);
	if (retval) {
		pr_err("error registering greybus driver\n");
		goto error_gpbridge;
	}

	if (gb_uart_driver_init()) {
		pr_err("error initializing uart driver\n");
		goto error_uart;
	}
	if (gb_sdio_driver_init()) {
		pr_err("error initializing sdio driver\n");
		goto error_sdio;
	}
	if (gb_usb_driver_init()) {
		pr_err("error initializing usb driver\n");
		goto error_usb;
	}
	if (gb_spi_driver_init()) {
		pr_err("error initializing spi driver\n");
		goto error_spi;
	}

	return 0;

error_spi:
	gb_usb_driver_exit();
error_usb:
	gb_sdio_driver_exit();
error_sdio:
	gb_uart_driver_exit();
error_uart:
	greybus_deregister(&gb_gpbridge_driver);
error_gpbridge:
	bus_unregister(&gpbridge_bus_type);
	ida_destroy(&gpbridge_id);
	return retval;
}
module_init(gpbridge_init);

static void __exit gpbridge_exit(void)
{
	gb_spi_driver_exit();
	gb_usb_driver_exit();
	gb_sdio_driver_exit();
	gb_uart_driver_exit();

	greybus_deregister(&gb_gpbridge_driver);
	bus_unregister(&gpbridge_bus_type);
	ida_destroy(&gpbridge_id);
}
module_exit(gpbridge_exit);

MODULE_LICENSE("GPL v2");
