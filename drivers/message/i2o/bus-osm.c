/*
 *	Bus Adapter OSM
 *
 *	Copyright (C) 2005	Markus Lidel <Markus.Lidel@shadowconnect.com>
 *
 *	This program is free software; you can redistribute it and/or modify it
 *	under the terms of the GNU General Public License as published by the
 *	Free Software Foundation; either version 2 of the License, or (at your
 *	option) any later version.
 *
 *	Fixes/additions:
 *		Markus Lidel <Markus.Lidel@shadowconnect.com>
 *			initial version.
 */

#include <linux/module.h>
#include <linux/i2o.h>

#define OSM_NAME	"bus-osm"
#define OSM_VERSION	"$Rev$"
#define OSM_DESCRIPTION	"I2O Bus Adapter OSM"

static struct i2o_driver i2o_bus_driver;

/* Bus OSM class handling definition */
static struct i2o_class_id i2o_bus_class_id[] = {
	{I2O_CLASS_BUS_ADAPTER},
	{I2O_CLASS_END}
};

/**
 *	i2o_bus_scan - Scan the bus for new devices
 *	@dev: I2O device of the bus, which should be scanned
 *
 *	Scans the bus dev for new / removed devices. After the scan a new LCT
 *	will be fetched automatically.
 *
 *	Returns 0 on success or negative error code on failure.
 */
static int i2o_bus_scan(struct i2o_device *dev)
{
	struct i2o_message __iomem *msg;
	u32 m;

	m = i2o_msg_get_wait(dev->iop, &msg, I2O_TIMEOUT_MESSAGE_GET);
	if (m == I2O_QUEUE_EMPTY)
		return -ETIMEDOUT;

	writel(FIVE_WORD_MSG_SIZE | SGL_OFFSET_0, &msg->u.head[0]);
	writel(I2O_CMD_BUS_SCAN << 24 | HOST_TID << 12 | dev->lct_data.tid,
	       &msg->u.head[1]);

	return i2o_msg_post_wait(dev->iop, m, 60);
};

/**
 *	i2o_bus_store_scan - Scan the I2O Bus Adapter
 *	@d: device which should be scanned
 *
 *	Returns count.
 */
static ssize_t i2o_bus_store_scan(struct device *d, struct device_attribute *attr, const char *buf,
				  size_t count)
{
	struct i2o_device *i2o_dev = to_i2o_device(d);
	int rc;

	if ((rc = i2o_bus_scan(i2o_dev)))
		osm_warn("bus scan failed %d\n", rc);

	return count;
}

/* Bus Adapter OSM device attributes */
static DEVICE_ATTR(scan, S_IWUSR, NULL, i2o_bus_store_scan);

/**
 *	i2o_bus_probe - verify if dev is a I2O Bus Adapter device and install it
 *	@dev: device to verify if it is a I2O Bus Adapter device
 *
 *	Because we want all Bus Adapters always return 0.
 *
 *	Returns 0.
 */
static int i2o_bus_probe(struct device *dev)
{
	struct i2o_device *i2o_dev = to_i2o_device(get_device(dev));

	device_create_file(dev, &dev_attr_scan);

	osm_info("device added (TID: %03x)\n", i2o_dev->lct_data.tid);

	return 0;
};

/**
 *	i2o_bus_remove - remove the I2O Bus Adapter device from the system again
 *	@dev: I2O Bus Adapter device which should be removed
 *
 *	Always returns 0.
 */
static int i2o_bus_remove(struct device *dev)
{
	struct i2o_device *i2o_dev = to_i2o_device(dev);

	device_remove_file(dev, &dev_attr_scan);

	put_device(dev);

	osm_info("device removed (TID: %03x)\n", i2o_dev->lct_data.tid);

	return 0;
};

/* Bus Adapter OSM driver struct */
static struct i2o_driver i2o_bus_driver = {
	.name = OSM_NAME,
	.classes = i2o_bus_class_id,
	.driver = {
		   .probe = i2o_bus_probe,
		   .remove = i2o_bus_remove,
		   },
};

/**
 *	i2o_bus_init - Bus Adapter OSM initialization function
 *
 *	Only register the Bus Adapter OSM in the I2O core.
 *
 *	Returns 0 on success or negative error code on failure.
 */
static int __init i2o_bus_init(void)
{
	int rc;

	printk(KERN_INFO OSM_DESCRIPTION " v" OSM_VERSION "\n");

	/* Register Bus Adapter OSM into I2O core */
	rc = i2o_driver_register(&i2o_bus_driver);
	if (rc) {
		osm_err("Could not register Bus Adapter OSM\n");
		return rc;
	}

	return 0;
};

/**
 *	i2o_bus_exit - Bus Adapter OSM exit function
 *
 *	Unregisters Bus Adapter OSM from I2O core.
 */
static void __exit i2o_bus_exit(void)
{
	i2o_driver_unregister(&i2o_bus_driver);
};

MODULE_AUTHOR("Markus Lidel <Markus.Lidel@shadowconnect.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION(OSM_DESCRIPTION);
MODULE_VERSION(OSM_VERSION);

module_init(i2o_bus_init);
module_exit(i2o_bus_exit);
