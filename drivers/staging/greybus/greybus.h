/*
 * Greybus driver and device API
 *
 * Copyright 2014 Google Inc.
 *
 * Released under the GPLv2 only.
 */

#ifndef __LINUX_GREYBUS_H
#define __LINUX_GREYBUS_H

#ifdef __KERNEL__

#include <linux/list.h>
#include <linux/device.h>
#include <linux/module.h>
#include "greybus_id.h"


#define GREYBUS_DEVICE_ID_MATCH_DEVICE \
		(GREYBUS_DEVICE_ID_MATCH_VENDOR | GREYBUS_DEVICE_ID_MATCH_PRODUCT)

#define GREYBUS_DEVICE(vendor, product)				\
	.match_flags	= GREYBUS_DEVICE_ID_MATCH_DEVICE,	\
	.wVendor	= (vendor),				\
	.wProduct	= (product),

#define GREYBUS_DEVICE_SERIAL(serial)				\
	.match_flags	= GREYBUS_DEVICE_ID_MATCH_SERIAL,	\
	.lSerial	= (serial),


struct greybus_descriptor {
	__u16	wVendor;
	__u16	wProduct;
	__u64	lSerialNumber;
};

struct greybus_device {
	struct device dev;
	struct greybus_descriptor descriptor;
};
#define to_greybus_device(d) container_of(d, struct greybus_device, dev)

struct greybus_driver {
	const char *name;

	int (*probe) (struct greybus_device *gdev,
		      const struct greybus_device_id *id);
	void (*disconnect) (struct greybus_device *gdev);

	int (*suspend) (struct greybus_device *gdev, pm_message_t message);
	int (*resume) (struct greybus_device *gdev);

	const struct greybus_device_id *id_table;

	struct device_driver driver;
};
#define to_greybus_driver(d) container_of(d, struct greybus_driver, driver)

static inline void greybus_set_drvdata(struct greybus_device *gdev, void *data)
{
        dev_set_drvdata(&gdev->dev, data);
}

static inline void *greybus_get_drvdata(struct greybus_device *gdev)
{
        return dev_get_drvdata(&gdev->dev);
}

/* Don't call these directly, use the module_greybus_driver() macro instead */
int greybus_register_driver(struct greybus_driver *driver,
			    struct module *module, const char *mod_name);
void greybus_deregister(struct greybus_driver *driver);

/* define to get proper THIS_MODULE and KBUILD_MODNAME values */
#define greybus_register(driver) \
	greybus_register_driver(driver, THIS_MODULE, KBUILD_MODNAME)

/**
 * module_greybus_driver() - Helper macro for registering a Greybus driver
 * @__greybus_driver: greybus_driver structure
 *
 * Helper macro for Greybus drivers to set up proper module init / exit
 * functions.  Replaces module_init() and module_exit() and keeps people from
 * printing pointless things to the kernel log when their driver is loaded.
 */
#define module_greybus_driver(__greybus_driver)	\
	module_driver(__greybus_driver, greybus_register, greybus_deregister)

extern struct bus_type greybus_bus_type;

int greybus_disabled(void);


#endif /* __KERNEL__ */
#endif /* __LINUX_GREYBUS_H */
