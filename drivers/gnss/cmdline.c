// SPDX-License-Identifier: GPL-2.0
/*
 * Test driver for GNSS. This driver requires the serdev binding and protocol
 * type to be specified on the module command line.
 *
 * Copyright 2019 Google LLC
 */

#include <linux/device.h>
#include <linux/gnss.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/serdev.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "serial.h"

#define GNSS_CMDLINE_MODULE_NAME "gnss-cmdline"

#define gnss_cmdline_err(...) \
	pr_err(GNSS_CMDLINE_MODULE_NAME ": " __VA_ARGS__)

static char *serdev;
module_param(serdev, charp, 0644);
MODULE_PARM_DESC(serdev, "serial device to wrap");

static int type;
module_param(type, int, 0644);
MODULE_PARM_DESC(serdev, "GNSS protocol type (see 'enum gnss_type')");

static struct serdev_device *serdev_device;

static int name_match(struct device *dev, void *data)
{
	return strstr(dev_name(dev), data) != NULL;
}

static int __init gnss_cmdline_init(void)
{
	struct device *serial_dev, *port_dev, *serdev_dev;
	char *driver_name, *port_name, *serdev_name;
	char *serdev_dup, *serdev_dup_sep;
	struct gnss_serial *gserial;
	int err = -ENODEV;

	/* User did not set the serdev module parameter */
	if (!serdev)
		return 0;

	if (type < 0 || type >= GNSS_TYPE_COUNT) {
		gnss_cmdline_err("invalid gnss type '%d'\n", type);
		return -EINVAL;
	}

	serdev_dup = serdev_dup_sep = kstrdup(serdev, GFP_KERNEL);
	if (!serdev_dup)
		return -ENOMEM;

	driver_name = strsep(&serdev_dup_sep, "/");
	if (!driver_name) {
		gnss_cmdline_err("driver name missing\n");
		goto err_free_serdev_dup;
	}

	port_name = strsep(&serdev_dup_sep, "/");
	if (!port_name) {
		gnss_cmdline_err("port name missing\n");
		goto err_free_serdev_dup;
	}

	serdev_name = strsep(&serdev_dup_sep, "/");
	if (!serdev_name) {
		gnss_cmdline_err("serdev name missing\n");
		goto err_free_serdev_dup;
	}

	/* Find the driver device instance (e.g. serial8250) */
	serial_dev = bus_find_device_by_name(&platform_bus_type,
					     NULL, driver_name);
	if (!serial_dev) {
		gnss_cmdline_err("no device '%s'\n", driver_name);
		goto err_free_serdev_dup;
	}

	/* Find the port device instance (e.g. serial0) */
	port_dev = device_find_child(serial_dev, port_name, name_match);
	if (!port_dev) {
		gnss_cmdline_err("no port '%s'\n", port_name);
		goto err_free_serdev_dup;
	}

	/* Find the serdev device instance (e.g. serial0-0) */
	serdev_dev = device_find_child(port_dev, serdev_name, name_match);
	if (!serdev_dev) {
		gnss_cmdline_err("no serdev '%s'\n", serdev_name);
		goto err_free_serdev_dup;
	}

	gserial = gnss_serial_allocate(to_serdev_device(serdev_dev), 0);
	if (IS_ERR(gserial)) {
		err = PTR_ERR(gserial);
		goto err_free_serdev_dup;
	}

	gserial->gdev->type = type;

	err = gnss_serial_register(gserial);
	if (err) {
		gnss_serial_free(gserial);
		goto err_free_serdev_dup;
	}

	serdev_device = to_serdev_device(serdev_dev);
	err = 0;
err_free_serdev_dup:
	kfree(serdev_dup);
	return err;
}

static void __exit gnss_cmdline_exit(void)
{
	struct gnss_serial *gserial;

	if (!serdev_device)
		return;

	gserial = serdev_device_get_drvdata(serdev_device);

	gnss_serial_deregister(gserial);
	gnss_serial_free(gserial);
}

module_init(gnss_cmdline_init);
module_exit(gnss_cmdline_exit);

MODULE_AUTHOR("Alistair Delva <adelva@google.com>");
MODULE_DESCRIPTION("GNSS command line driver");
MODULE_LICENSE("GPL v2");
