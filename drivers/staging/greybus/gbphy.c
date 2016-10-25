/*
 * Greybus Bridged-Phy Bus driver
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
#include "gbphy.h"

#define GB_GBPHY_AUTOSUSPEND_MS	3000

struct gbphy_host {
	struct gb_bundle *bundle;
	struct list_head devices;
};

static DEFINE_IDA(gbphy_id);

static ssize_t protocol_id_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct gbphy_device *gbphy_dev = to_gbphy_dev(dev);

	return sprintf(buf, "0x%02x\n", gbphy_dev->cport_desc->protocol_id);
}
static DEVICE_ATTR_RO(protocol_id);

static struct attribute *gbphy_dev_attrs[] = {
	&dev_attr_protocol_id.attr,
	NULL,
};

ATTRIBUTE_GROUPS(gbphy_dev);

static void gbphy_dev_release(struct device *dev)
{
	struct gbphy_device *gbphy_dev = to_gbphy_dev(dev);

	ida_simple_remove(&gbphy_id, gbphy_dev->id);
	kfree(gbphy_dev);
}

#ifdef CONFIG_PM
static int gb_gbphy_idle(struct device *dev)
{
	pm_runtime_mark_last_busy(dev);
	pm_request_autosuspend(dev);
	return 0;
}
#endif

static const struct dev_pm_ops gb_gbphy_pm_ops = {
	SET_RUNTIME_PM_OPS(pm_generic_runtime_suspend,
			   pm_generic_runtime_resume,
			   gb_gbphy_idle)
};

static struct device_type greybus_gbphy_dev_type = {
	.name	 =	"gbphy_device",
	.release =	gbphy_dev_release,
	.pm	=	&gb_gbphy_pm_ops,
};

static int gbphy_dev_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	struct gbphy_device *gbphy_dev = to_gbphy_dev(dev);
	struct greybus_descriptor_cport *cport_desc = gbphy_dev->cport_desc;
	struct gb_bundle *bundle = gbphy_dev->bundle;
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
	if (add_uevent_var(env, "BUNDLE=%u", gbphy_dev->bundle->id))
		return -ENOMEM;
	if (add_uevent_var(env, "BUNDLE_CLASS=%02x", bundle->class))
		return -ENOMEM;
	if (add_uevent_var(env, "GBPHY=%u", gbphy_dev->id))
		return -ENOMEM;
	if (add_uevent_var(env, "PROTOCOL_ID=%02x", cport_desc->protocol_id))
		return -ENOMEM;

	return 0;
}

static const struct gbphy_device_id *
gbphy_dev_match_id(struct gbphy_device *gbphy_dev, struct gbphy_driver *gbphy_drv)
{
	const struct gbphy_device_id *id = gbphy_drv->id_table;

	if (!id)
		return NULL;

	for (; id->protocol_id; id++)
		if (id->protocol_id == gbphy_dev->cport_desc->protocol_id)
			return id;

	return NULL;
}

static int gbphy_dev_match(struct device *dev, struct device_driver *drv)
{
	struct gbphy_driver *gbphy_drv = to_gbphy_driver(drv);
	struct gbphy_device *gbphy_dev = to_gbphy_dev(dev);
	const struct gbphy_device_id *id;

	id = gbphy_dev_match_id(gbphy_dev, gbphy_drv);
	if (id)
		return 1;

	return 0;
}

static int gbphy_dev_probe(struct device *dev)
{
	struct gbphy_driver *gbphy_drv = to_gbphy_driver(dev->driver);
	struct gbphy_device *gbphy_dev = to_gbphy_dev(dev);
	const struct gbphy_device_id *id;
	int ret;

	id = gbphy_dev_match_id(gbphy_dev, gbphy_drv);
	if (!id)
		return -ENODEV;

	/* for old kernels we need get_sync to resume parent devices */
	ret = gb_pm_runtime_get_sync(gbphy_dev->bundle);
	if (ret < 0)
		return ret;

	pm_runtime_set_autosuspend_delay(dev, GB_GBPHY_AUTOSUSPEND_MS);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_get_noresume(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	/*
	 * Drivers should call put on the gbphy dev before returning
	 * from probe if they support runtime pm.
	 */
	ret = gbphy_drv->probe(gbphy_dev, id);
	if (ret) {
		pm_runtime_disable(dev);
		pm_runtime_set_suspended(dev);
		pm_runtime_put_noidle(dev);
		pm_runtime_dont_use_autosuspend(dev);
	}

	gb_pm_runtime_put_autosuspend(gbphy_dev->bundle);

	return ret;
}

static int gbphy_dev_remove(struct device *dev)
{
	struct gbphy_driver *gbphy_drv = to_gbphy_driver(dev->driver);
	struct gbphy_device *gbphy_dev = to_gbphy_dev(dev);

	gbphy_drv->remove(gbphy_dev);

	pm_runtime_disable(dev);
	pm_runtime_set_suspended(dev);
	pm_runtime_put_noidle(dev);
	pm_runtime_dont_use_autosuspend(dev);

	return 0;
}

static struct bus_type gbphy_bus_type = {
	.name =		"gbphy",
	.match =	gbphy_dev_match,
	.probe =	gbphy_dev_probe,
	.remove =	gbphy_dev_remove,
	.uevent =	gbphy_dev_uevent,
};

int gb_gbphy_register_driver(struct gbphy_driver *driver,
			     struct module *owner, const char *mod_name)
{
	int retval;

	if (greybus_disabled())
		return -ENODEV;

	driver->driver.bus = &gbphy_bus_type;
	driver->driver.name = driver->name;
	driver->driver.owner = owner;
	driver->driver.mod_name = mod_name;

	retval = driver_register(&driver->driver);
	if (retval)
		return retval;

	pr_info("registered new driver %s\n", driver->name);
	return 0;
}
EXPORT_SYMBOL_GPL(gb_gbphy_register_driver);

void gb_gbphy_deregister_driver(struct gbphy_driver *driver)
{
	driver_unregister(&driver->driver);
}
EXPORT_SYMBOL_GPL(gb_gbphy_deregister_driver);

static struct gbphy_device *gb_gbphy_create_dev(struct gb_bundle *bundle,
				struct greybus_descriptor_cport *cport_desc)
{
	struct gbphy_device *gbphy_dev;
	int retval;
	int id;

	id = ida_simple_get(&gbphy_id, 1, 0, GFP_KERNEL);
	if (id < 0)
		return ERR_PTR(id);

	gbphy_dev = kzalloc(sizeof(*gbphy_dev), GFP_KERNEL);
	if (!gbphy_dev) {
		ida_simple_remove(&gbphy_id, id);
		return ERR_PTR(-ENOMEM);
	}

	gbphy_dev->id = id;
	gbphy_dev->bundle = bundle;
	gbphy_dev->cport_desc = cport_desc;
	gbphy_dev->dev.parent = &bundle->dev;
	gbphy_dev->dev.bus = &gbphy_bus_type;
	gbphy_dev->dev.type = &greybus_gbphy_dev_type;
	gbphy_dev->dev.groups = gbphy_dev_groups;
	gbphy_dev->dev.dma_mask = bundle->dev.dma_mask;
	dev_set_name(&gbphy_dev->dev, "gbphy%d", id);

	retval = device_register(&gbphy_dev->dev);
	if (retval) {
		put_device(&gbphy_dev->dev);
		return ERR_PTR(retval);
	}

	return gbphy_dev;
}

static void gb_gbphy_disconnect(struct gb_bundle *bundle)
{
	struct gbphy_host *gbphy_host = greybus_get_drvdata(bundle);
	struct gbphy_device *gbphy_dev, *temp;
	int ret;

	ret = gb_pm_runtime_get_sync(bundle);
	if (ret < 0)
		gb_pm_runtime_get_noresume(bundle);

	list_for_each_entry_safe(gbphy_dev, temp, &gbphy_host->devices, list) {
		list_del(&gbphy_dev->list);
		device_unregister(&gbphy_dev->dev);
	}

	kfree(gbphy_host);
}

static int gb_gbphy_probe(struct gb_bundle *bundle,
			  const struct greybus_bundle_id *id)
{
	struct gbphy_host *gbphy_host;
	struct gbphy_device *gbphy_dev;
	int i;

	if (bundle->num_cports == 0)
		return -ENODEV;

	gbphy_host = kzalloc(sizeof(*gbphy_host), GFP_KERNEL);
	if (!gbphy_host)
		return -ENOMEM;

	gbphy_host->bundle = bundle;
	INIT_LIST_HEAD(&gbphy_host->devices);
	greybus_set_drvdata(bundle, gbphy_host);

	/*
	 * Create a bunch of children devices, one per cport, and bind the
	 * bridged phy drivers to them.
	 */
	for (i = 0; i < bundle->num_cports; ++i) {
		gbphy_dev = gb_gbphy_create_dev(bundle, &bundle->cport_desc[i]);
		if (IS_ERR(gbphy_dev)) {
			gb_gbphy_disconnect(bundle);
			return PTR_ERR(gbphy_dev);
		}
		list_add(&gbphy_dev->list, &gbphy_host->devices);
	}

	gb_pm_runtime_put_autosuspend(bundle);

	return 0;
}

static const struct greybus_bundle_id gb_gbphy_id_table[] = {
	{ GREYBUS_DEVICE_CLASS(GREYBUS_CLASS_BRIDGED_PHY) },
	{ },
};
MODULE_DEVICE_TABLE(greybus, gb_gbphy_id_table);

static struct greybus_driver gb_gbphy_driver = {
	.name		= "gbphy",
	.probe		= gb_gbphy_probe,
	.disconnect	= gb_gbphy_disconnect,
	.id_table	= gb_gbphy_id_table,
};

static int __init gbphy_init(void)
{
	int retval;

	retval = bus_register(&gbphy_bus_type);
	if (retval) {
		pr_err("gbphy bus register failed (%d)\n", retval);
		return retval;
	}

	retval = greybus_register(&gb_gbphy_driver);
	if (retval) {
		pr_err("error registering greybus driver\n");
		goto error_gbphy;
	}

	return 0;

error_gbphy:
	bus_unregister(&gbphy_bus_type);
	ida_destroy(&gbphy_id);
	return retval;
}
module_init(gbphy_init);

static void __exit gbphy_exit(void)
{
	greybus_deregister(&gb_gbphy_driver);
	bus_unregister(&gbphy_bus_type);
	ida_destroy(&gbphy_id);
}
module_exit(gbphy_exit);

MODULE_LICENSE("GPL v2");
