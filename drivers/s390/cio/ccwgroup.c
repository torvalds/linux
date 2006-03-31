/*
 *  drivers/s390/cio/ccwgroup.c
 *  bus driver for ccwgroup
 *
 *    Copyright (C) 2002 IBM Deutschland Entwicklung GmbH,
 *                       IBM Corporation
 *    Author(s): Arnd Bergmann (arndb@de.ibm.com)
 *               Cornelia Huck (cornelia.huck@de.ibm.com)
 */
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/ctype.h>
#include <linux/dcache.h>

#include <asm/semaphore.h>
#include <asm/ccwdev.h>
#include <asm/ccwgroup.h>

/* In Linux 2.4, we had a channel device layer called "chandev"
 * that did all sorts of obscure stuff for networking devices.
 * This is another driver that serves as a replacement for just
 * one of its functions, namely the translation of single subchannels
 * to devices that use multiple subchannels.
 */

/* a device matches a driver if all its slave devices match the same
 * entry of the driver */
static int
ccwgroup_bus_match (struct device * dev, struct device_driver * drv)
{
	struct ccwgroup_device *gdev;
	struct ccwgroup_driver *gdrv;

	gdev = container_of(dev, struct ccwgroup_device, dev);
	gdrv = container_of(drv, struct ccwgroup_driver, driver);

	if (gdev->creator_id == gdrv->driver_id)
		return 1;

	return 0;
}
static int
ccwgroup_uevent (struct device *dev, char **envp, int num_envp, char *buffer,
		  int buffer_size)
{
	/* TODO */
	return 0;
}

static struct bus_type ccwgroup_bus_type;

static inline void
__ccwgroup_remove_symlinks(struct ccwgroup_device *gdev)
{
	int i;
	char str[8];

	for (i = 0; i < gdev->count; i++) {
		sprintf(str, "cdev%d", i);
		sysfs_remove_link(&gdev->dev.kobj, str);
		sysfs_remove_link(&gdev->cdev[i]->dev.kobj, "group_device");
	}
	
}

/*
 * Provide an 'ungroup' attribute so the user can remove group devices no
 * longer needed or accidentially created. Saves memory :)
 */
static ssize_t
ccwgroup_ungroup_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct ccwgroup_device *gdev;

	gdev = to_ccwgroupdev(dev);

	if (gdev->state != CCWGROUP_OFFLINE)
		return -EINVAL;

	__ccwgroup_remove_symlinks(gdev);
	device_unregister(dev);

	return count;
}

static DEVICE_ATTR(ungroup, 0200, NULL, ccwgroup_ungroup_store);

static void
ccwgroup_release (struct device *dev)
{
	struct ccwgroup_device *gdev;
	int i;

	gdev = to_ccwgroupdev(dev);

	for (i = 0; i < gdev->count; i++) {
		gdev->cdev[i]->dev.driver_data = NULL;
		put_device(&gdev->cdev[i]->dev);
	}
	kfree(gdev);
}

static inline int
__ccwgroup_create_symlinks(struct ccwgroup_device *gdev)
{
	char str[8];
	int i, rc;

	for (i = 0; i < gdev->count; i++) {
		rc = sysfs_create_link(&gdev->cdev[i]->dev.kobj, &gdev->dev.kobj,
				       "group_device");
		if (rc) {
			for (--i; i >= 0; i--)
				sysfs_remove_link(&gdev->cdev[i]->dev.kobj,
						  "group_device");
			return rc;
		}
	}
	for (i = 0; i < gdev->count; i++) {
		sprintf(str, "cdev%d", i);
		rc = sysfs_create_link(&gdev->dev.kobj, &gdev->cdev[i]->dev.kobj,
				       str);
		if (rc) {
			for (--i; i >= 0; i--) {
				sprintf(str, "cdev%d", i);
				sysfs_remove_link(&gdev->dev.kobj, str);
			}
			for (i = 0; i < gdev->count; i++)
				sysfs_remove_link(&gdev->cdev[i]->dev.kobj,
						  "group_device");
			return rc;
		}
	}
	return 0;
}

/*
 * try to add a new ccwgroup device for one driver
 * argc and argv[] are a list of bus_id's of devices
 * belonging to the driver.
 */
int
ccwgroup_create(struct device *root,
		unsigned int creator_id,
		struct ccw_driver *cdrv,
		int argc, char *argv[])
{
	struct ccwgroup_device *gdev;
	int i;
	int rc;
	int del_drvdata;

	if (argc > 256) /* disallow dumb users */
		return -EINVAL;

	gdev = kzalloc(sizeof(*gdev) + argc*sizeof(gdev->cdev[0]), GFP_KERNEL);
	if (!gdev)
		return -ENOMEM;

	atomic_set(&gdev->onoff, 0);

	del_drvdata = 0;
	for (i = 0; i < argc; i++) {
		gdev->cdev[i] = get_ccwdev_by_busid(cdrv, argv[i]);

		/* all devices have to be of the same type in
		 * order to be grouped */
		if (!gdev->cdev[i]
		    || gdev->cdev[i]->id.driver_info !=
		    gdev->cdev[0]->id.driver_info) {
			rc = -EINVAL;
			goto free_dev;
		}
		/* Don't allow a device to belong to more than one group. */
		if (gdev->cdev[i]->dev.driver_data) {
			rc = -EINVAL;
			goto free_dev;
		}
	}
	for (i = 0; i < argc; i++)
		gdev->cdev[i]->dev.driver_data = gdev;
	del_drvdata = 1;

	gdev->creator_id = creator_id;
	gdev->count = argc;
	gdev->dev = (struct device ) {
		.bus = &ccwgroup_bus_type,
		.parent = root,
		.release = ccwgroup_release,
	};

	snprintf (gdev->dev.bus_id, BUS_ID_SIZE, "%s",
			gdev->cdev[0]->dev.bus_id);

	rc = device_register(&gdev->dev);
	
	if (rc)
		goto free_dev;
	get_device(&gdev->dev);
	rc = device_create_file(&gdev->dev, &dev_attr_ungroup);

	if (rc) {
		device_unregister(&gdev->dev);
		goto error;
	}

	rc = __ccwgroup_create_symlinks(gdev);
	if (!rc) {
		put_device(&gdev->dev);
		return 0;
	}
	device_remove_file(&gdev->dev, &dev_attr_ungroup);
	device_unregister(&gdev->dev);
error:
	for (i = 0; i < argc; i++)
		if (gdev->cdev[i]) {
			put_device(&gdev->cdev[i]->dev);
			gdev->cdev[i]->dev.driver_data = NULL;
		}
	put_device(&gdev->dev);
	return rc;
free_dev:
	for (i = 0; i < argc; i++)
		if (gdev->cdev[i]) {
			put_device(&gdev->cdev[i]->dev);
			if (del_drvdata)
				gdev->cdev[i]->dev.driver_data = NULL;
		}
	kfree(gdev);
	return rc;
}

static int __init
init_ccwgroup (void)
{
	return bus_register (&ccwgroup_bus_type);
}

static void __exit
cleanup_ccwgroup (void)
{
	bus_unregister (&ccwgroup_bus_type);
}

module_init(init_ccwgroup);
module_exit(cleanup_ccwgroup);

/************************** driver stuff ******************************/

static int
ccwgroup_set_online(struct ccwgroup_device *gdev)
{
	struct ccwgroup_driver *gdrv;
	int ret;

	if (atomic_cmpxchg(&gdev->onoff, 0, 1) != 0)
		return -EAGAIN;
	if (gdev->state == CCWGROUP_ONLINE) {
		ret = 0;
		goto out;
	}
	if (!gdev->dev.driver) {
		ret = -EINVAL;
		goto out;
	}
	gdrv = to_ccwgroupdrv (gdev->dev.driver);
	if ((ret = gdrv->set_online ? gdrv->set_online(gdev) : 0))
		goto out;

	gdev->state = CCWGROUP_ONLINE;
 out:
	atomic_set(&gdev->onoff, 0);
	return ret;
}

static int
ccwgroup_set_offline(struct ccwgroup_device *gdev)
{
	struct ccwgroup_driver *gdrv;
	int ret;

	if (atomic_cmpxchg(&gdev->onoff, 0, 1) != 0)
		return -EAGAIN;
	if (gdev->state == CCWGROUP_OFFLINE) {
		ret = 0;
		goto out;
	}
	if (!gdev->dev.driver) {
		ret = -EINVAL;
		goto out;
	}
	gdrv = to_ccwgroupdrv (gdev->dev.driver);
	if ((ret = gdrv->set_offline ? gdrv->set_offline(gdev) : 0))
		goto out;

	gdev->state = CCWGROUP_OFFLINE;
 out:
	atomic_set(&gdev->onoff, 0);
	return ret;
}

static ssize_t
ccwgroup_online_store (struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct ccwgroup_device *gdev;
	struct ccwgroup_driver *gdrv;
	unsigned int value;
	int ret;

	gdev = to_ccwgroupdev(dev);
	if (!dev->driver)
		return count;

	gdrv = to_ccwgroupdrv (gdev->dev.driver);
	if (!try_module_get(gdrv->owner))
		return -EINVAL;

	value = simple_strtoul(buf, 0, 0);
	ret = count;
	if (value == 1)
		ccwgroup_set_online(gdev);
	else if (value == 0)
		ccwgroup_set_offline(gdev);
	else
		ret = -EINVAL;
	module_put(gdrv->owner);
	return ret;
}

static ssize_t
ccwgroup_online_show (struct device *dev, struct device_attribute *attr, char *buf)
{
	int online;

	online = (to_ccwgroupdev(dev)->state == CCWGROUP_ONLINE);

	return sprintf(buf, online ? "1\n" : "0\n");
}

static DEVICE_ATTR(online, 0644, ccwgroup_online_show, ccwgroup_online_store);

static int
ccwgroup_probe (struct device *dev)
{
	struct ccwgroup_device *gdev;
	struct ccwgroup_driver *gdrv;

	int ret;

	gdev = to_ccwgroupdev(dev);
	gdrv = to_ccwgroupdrv(dev->driver);

	if ((ret = device_create_file(dev, &dev_attr_online)))
		return ret;

	pr_debug("%s: device %s\n", __func__, gdev->dev.bus_id);
	ret = gdrv->probe ? gdrv->probe(gdev) : -ENODEV;
	if (ret)
		device_remove_file(dev, &dev_attr_online);

	return ret;
}

static int
ccwgroup_remove (struct device *dev)
{
	struct ccwgroup_device *gdev;
	struct ccwgroup_driver *gdrv;

	gdev = to_ccwgroupdev(dev);
	gdrv = to_ccwgroupdrv(dev->driver);

	pr_debug("%s: device %s\n", __func__, gdev->dev.bus_id);

	device_remove_file(dev, &dev_attr_online);

	if (gdrv && gdrv->remove)
		gdrv->remove(gdev);
	return 0;
}

static struct bus_type ccwgroup_bus_type = {
	.name   = "ccwgroup",
	.match  = ccwgroup_bus_match,
	.uevent = ccwgroup_uevent,
	.probe  = ccwgroup_probe,
	.remove = ccwgroup_remove,
};

int
ccwgroup_driver_register (struct ccwgroup_driver *cdriver)
{
	/* register our new driver with the core */
	cdriver->driver = (struct device_driver) {
		.bus = &ccwgroup_bus_type,
		.name = cdriver->name,
	};

	return driver_register(&cdriver->driver);
}

static int
__ccwgroup_driver_unregister_device(struct device *dev, void *data)
{
	__ccwgroup_remove_symlinks(to_ccwgroupdev(dev));
	device_unregister(dev);
	put_device(dev);
	return 0;
}

void
ccwgroup_driver_unregister (struct ccwgroup_driver *cdriver)
{
	/* We don't want ccwgroup devices to live longer than their driver. */
	get_driver(&cdriver->driver);
	driver_for_each_device(&cdriver->driver, NULL, NULL,
			       __ccwgroup_driver_unregister_device);
	put_driver(&cdriver->driver);
	driver_unregister(&cdriver->driver);
}

int
ccwgroup_probe_ccwdev(struct ccw_device *cdev)
{
	return 0;
}

static inline struct ccwgroup_device *
__ccwgroup_get_gdev_by_cdev(struct ccw_device *cdev)
{
	struct ccwgroup_device *gdev;

	if (cdev->dev.driver_data) {
		gdev = (struct ccwgroup_device *)cdev->dev.driver_data;
		if (get_device(&gdev->dev)) {
			if (device_is_registered(&gdev->dev))
				return gdev;
			put_device(&gdev->dev);
		}
		return NULL;
	}
	return NULL;
}

void
ccwgroup_remove_ccwdev(struct ccw_device *cdev)
{
	struct ccwgroup_device *gdev;

	/* Ignore offlining errors, device is gone anyway. */
	ccw_device_set_offline(cdev);
	/* If one of its devices is gone, the whole group is done for. */
	gdev = __ccwgroup_get_gdev_by_cdev(cdev);
	if (gdev) {
		__ccwgroup_remove_symlinks(gdev);
		device_unregister(&gdev->dev);
		put_device(&gdev->dev);
	}
}

MODULE_LICENSE("GPL");
EXPORT_SYMBOL(ccwgroup_driver_register);
EXPORT_SYMBOL(ccwgroup_driver_unregister);
EXPORT_SYMBOL(ccwgroup_create);
EXPORT_SYMBOL(ccwgroup_probe_ccwdev);
EXPORT_SYMBOL(ccwgroup_remove_ccwdev);
