/* visorbus_main.c
 *
 * Copyright � 2010 - 2013 UNISYS CORPORATION
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 */

#include <linux/uuid.h>

#include "visorbus.h"
#include "visorbus_private.h"
#include "version.h"
#include "periodic_work.h"
#include "vbuschannel.h"
#include "guestlinuxdebug.h"
#include "vmcallinterface.h"

#define MYDRVNAME "visorbus"

/* module parameters */
static int visorbus_debug;
static int visorbus_forcematch;
static int visorbus_forcenomatch;
#define MAXDEVICETEST 4
static int visorbus_devicetest;
static int visorbus_debugref;
#define SERIALLOOPBACKCHANADDR (100 * 1024 * 1024)

/** This is the private data that we store for each bus device instance.
 */
struct visorbus_devdata {
	int devno;		/* this is the chipset busNo */
	struct list_head list_all;
	struct device dev;
	struct kobject kobj;
	struct visorchannel *chan;	/* channel area for bus itself */
	bool vbus_valid;
	void *vbus_hdr_info;
};

#define CURRENT_FILE_PC VISOR_BUS_PC_visorbus_main_c
#define POLLJIFFIES_TESTWORK         100
#define POLLJIFFIES_NORMALCHANNEL     10

static int visorbus_uevent(struct device *xdev, struct kobj_uevent_env *env);
static int visorbus_match(struct device *xdev, struct device_driver *xdrv);
static void fix_vbus_dev_info(struct visor_device *visordev);

/*  BUS type attributes
 *
 *  define & implement display of bus attributes under
 *  /sys/bus/visorbus.
 *
 */

static ssize_t version_show(struct bus_type *bus, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s\n", VERSION);
}

static BUS_ATTR_RO(version);

static struct attribute *visorbus_bus_attrs[] = {
	&bus_attr_version.attr,
	NULL,
};

static const struct attribute_group visorbus_bus_group = {
	.attrs = visorbus_bus_attrs,
};

static const struct attribute_group *visorbus_bus_groups[] = {
	&visorbus_bus_group,
	NULL,
};


/** This describes the TYPE of bus.
 *  (Don't confuse this with an INSTANCE of the bus.)
 */
struct bus_type visorbus_type = {
	.name = "visorbus",
	.match = visorbus_match,
	.uevent = visorbus_uevent,
	.bus_groups = visorbus_bus_groups,
};

static struct delayed_work periodic_work;

/* YES, we need 2 workqueues.
 * The reason is, workitems on the test queue may need to cancel
 * workitems on the other queue.  You will be in for trouble if you try to
 * do this with workitems queued on the same workqueue.
 */
static struct workqueue_struct *periodic_test_workqueue;
static struct workqueue_struct *periodic_dev_workqueue;
static long long bus_count;	/** number of bus instances */
static long long total_devices_created;
					/** ever-increasing */

static void chipset_bus_create(struct visorchipset_bus_info *bus_info);
static void chipset_bus_destroy(struct visorchipset_bus_info *bus_info);
static void chipset_device_create(struct visorchipset_device_info *dev_info);
static void chipset_device_destroy(struct visorchipset_device_info *dev_info);
static void chipset_device_pause(struct visorchipset_device_info *dev_info);
static void chipset_device_resume(struct visorchipset_device_info *dev_info);

/** These functions are implemented herein, and are called by the chipset
 *  driver to notify us about specific events.
 */
static struct visorchipset_busdev_notifiers chipset_notifiers = {
	.bus_create = chipset_bus_create,
	.bus_destroy = chipset_bus_destroy,
	.device_create = chipset_device_create,
	.device_destroy = chipset_device_destroy,
	.device_pause = chipset_device_pause,
	.device_resume = chipset_device_resume,
};

/** These functions are implemented in the chipset driver, and we call them
 *  herein when we want to acknowledge a specific event.
 */
static struct visorchipset_busdev_responders chipset_responders;

/* filled in with info about parent chipset driver when we register with it */
static struct ultra_vbus_deviceinfo chipset_driverinfo;
/* filled in with info about this driver, wrt it servicing client busses */
static struct ultra_vbus_deviceinfo clientbus_driverinfo;

/** list of visorbus_devdata structs, linked via .list_all */
static LIST_HEAD(list_all_bus_instances);
/** list of visor_device structs, linked via .list_all */
static LIST_HEAD(list_all_device_instances);

static int
visorbus_uevent(struct device *xdev, struct kobj_uevent_env *env)
{
	if (add_uevent_var(env, "VERSION=%s", VERSION))
		return -ENOMEM;
	return 0;
}

/* This is called automatically upon adding a visor_device (device_add), or
 * adding a visor_driver (visorbus_register_visor_driver), and returns 1 iff the
 * provided driver can control the specified device.
 */
static int
visorbus_match(struct device *xdev, struct device_driver *xdrv)
{
	uuid_le channel_type;
	int rc = 0;
	int i;
	struct visor_device *dev;
	struct visor_driver *drv;

	dev = to_visor_device(xdev);
	drv = to_visor_driver(xdrv);
	channel_type = visorchannel_get_uuid(dev->visorchannel);
	if (visorbus_forcematch) {
		rc = 1;
		goto away;
	}
	if (visorbus_forcenomatch)
		goto away;

	if (!drv->channel_types)
		goto away;
	for (i = 0;
	     (uuid_le_cmp(drv->channel_types[i].guid, NULL_UUID_LE) != 0) ||
	     (drv->channel_types[i].name);
	     i++)
		if (uuid_le_cmp(drv->channel_types[i].guid,
				channel_type) == 0) {
			rc = i + 1;
			goto away;
		}
away:
	return rc;
}

/** This is called when device_unregister() is called for the bus device
 *  instance, after all other tasks involved with destroying the device
 *  are complete.
 */
static void
visorbus_release_busdevice(struct device *xdev)
{
	struct visorbus_devdata *devdata = dev_get_drvdata(xdev);

	dev_set_drvdata(xdev, NULL);
	kfree(devdata);
	kfree(xdev);
}

/** This is called when device_unregister() is called for each child
 *  device instance.
 */
static void
visorbus_release_device(struct device *xdev)
{
	struct visor_device *dev = to_visor_device(xdev);

	if (dev->periodic_work) {
		visor_periodic_work_destroy(dev->periodic_work);
		dev->periodic_work = NULL;
	}
	if (dev->visorchannel) {
		visorchannel_destroy(dev->visorchannel);
		dev->visorchannel = NULL;
	}
	kfree(dev);
}

/* Implement publishing of device node attributes under:
 *
 *     /sys/bus/visorbus<x>/dev<y>/devmajorminor
 *
 */

#define to_devmajorminor_attr(_attr) \
	container_of(_attr, struct devmajorminor_attribute, attr)
#define to_visor_device_from_kobjdevmajorminor(obj) \
	container_of(obj, struct visor_device, kobjdevmajorminor)

struct devmajorminor_attribute {
	struct attribute attr;
	int slot;
	 ssize_t (*show)(struct visor_device *, int slot, char *buf);
	 ssize_t (*store)(struct visor_device *, int slot, const char *buf,
			  size_t count);
};

static ssize_t DEVMAJORMINOR_ATTR(struct visor_device *dev, int slot, char *buf)
{
	int maxdevnodes = ARRAY_SIZE(dev->devnodes) / sizeof(dev->devnodes[0]);

	if (slot < 0 || slot >= maxdevnodes)
		return 0;
	return snprintf(buf, PAGE_SIZE, "%d:%d\n",
			dev->devnodes[slot].major, dev->devnodes[slot].minor);
}

static ssize_t
devmajorminor_attr_show(struct kobject *kobj, struct attribute *attr, char *buf)
{
	struct devmajorminor_attribute *devmajorminor_attr =
	    to_devmajorminor_attr(attr);
	struct visor_device *dev = to_visor_device_from_kobjdevmajorminor(kobj);
	ssize_t ret = 0;

	if (devmajorminor_attr->show)
		ret = devmajorminor_attr->show(dev,
					       devmajorminor_attr->slot, buf);
	return ret;
}

static ssize_t
devmajorminor_attr_store(struct kobject *kobj,
			 struct attribute *attr, const char *buf, size_t count)
{
	struct devmajorminor_attribute *devmajorminor_attr =
	    to_devmajorminor_attr(attr);
	struct visor_device *dev = to_visor_device_from_kobjdevmajorminor(kobj);
	ssize_t ret = 0;

	if (devmajorminor_attr->store)
		ret = devmajorminor_attr->store(dev,
						devmajorminor_attr->slot,
						buf, count);
	return ret;
}

static int register_devmajorminor_attributes(struct visor_device *dev);

static int
devmajorminor_create_file(struct visor_device *dev, const char *name,
			  int major, int minor)
{
	int maxdevnodes = ARRAY_SIZE(dev->devnodes) / sizeof(dev->devnodes[0]);
	struct devmajorminor_attribute *myattr = NULL;
	int x = -1, rc = 0, slot = -1;

	register_devmajorminor_attributes(dev);
	for (slot = 0; slot < maxdevnodes; slot++)
		if (!dev->devnodes[slot].attr)
			break;
	if (slot == maxdevnodes) {
		rc = -ENOMEM;
		goto away;
	}
	myattr = kmalloc(sizeof(*myattr), GFP_KERNEL);
	if (!myattr) {
		rc = -ENOMEM;
		goto away;
	}
	memset(myattr, 0, sizeof(struct devmajorminor_attribute));
	myattr->show = DEVMAJORMINOR_ATTR;
	myattr->store = NULL;
	myattr->slot = slot;
	myattr->attr.name = name;
	myattr->attr.mode = S_IRUGO;
	dev->devnodes[slot].attr = myattr;
	dev->devnodes[slot].major = major;
	dev->devnodes[slot].minor = minor;
	x = sysfs_create_file(&dev->kobjdevmajorminor, &myattr->attr);
	if (x < 0) {
		rc = x;
		goto away;
	}
	kobject_uevent(&dev->device.kobj, KOBJ_ONLINE);
away:
	if (rc < 0) {
		kfree(myattr);
		myattr = NULL;
		dev->devnodes[slot].attr = NULL;
	}
	return rc;
}

static void
devmajorminor_remove_file(struct visor_device *dev, int slot)
{
	int maxdevnodes = ARRAY_SIZE(dev->devnodes) / sizeof(dev->devnodes[0]);
	struct devmajorminor_attribute *myattr = NULL;

	if (slot < 0 || slot >= maxdevnodes)
		return;
	myattr = (struct devmajorminor_attribute *)(dev->devnodes[slot].attr);
	if (myattr)
		return;
	sysfs_remove_file(&dev->kobjdevmajorminor, &myattr->attr);
	kobject_uevent(&dev->device.kobj, KOBJ_OFFLINE);
	dev->devnodes[slot].attr = NULL;
	kfree(myattr);
}

static void
devmajorminor_remove_all_files(struct visor_device *dev)
{
	int i = 0;
	int maxdevnodes = ARRAY_SIZE(dev->devnodes) / sizeof(dev->devnodes[0]);

	for (i = 0; i < maxdevnodes; i++)
		devmajorminor_remove_file(dev, i);
}

static const struct sysfs_ops devmajorminor_sysfs_ops = {
	.show = devmajorminor_attr_show,
	.store = devmajorminor_attr_store,
};

static struct kobj_type devmajorminor_kobj_type = {
	.sysfs_ops = &devmajorminor_sysfs_ops
};

static int
register_devmajorminor_attributes(struct visor_device *dev)
{
	int rc = 0, x = 0;

	if (dev->kobjdevmajorminor.parent)
		goto away;	/* already registered */
	x = kobject_init_and_add(&dev->kobjdevmajorminor,
				 &devmajorminor_kobj_type, &dev->device.kobj,
				 "devmajorminor");
	if (x < 0) {
		rc = x;
		goto away;
	}

	kobject_uevent(&dev->kobjdevmajorminor, KOBJ_ADD);

away:
	return rc;
}

static void
unregister_devmajorminor_attributes(struct visor_device *dev)
{
	if (!dev->kobjdevmajorminor.parent)
		return;		/* already unregistered */
	devmajorminor_remove_all_files(dev);

	kobject_del(&dev->kobjdevmajorminor);
	kobject_put(&dev->kobjdevmajorminor);
	dev->kobjdevmajorminor.parent = NULL;
}

/* begin implementation of specific channel attributes to appear under
* /sys/bus/visorbus<x>/dev<y>/channel
*/
static ssize_t physaddr_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct visor_device *vdev = to_visor_device(dev);

	if (!vdev->visorchannel)
		return 0;
	return snprintf(buf, PAGE_SIZE, "0x%Lx\n",
			visorchannel_get_physaddr(vdev->visorchannel));
}

static ssize_t nbytes_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct visor_device *vdev = to_visor_device(dev);

	if (!vdev->visorchannel)
		return 0;
	return snprintf(buf, PAGE_SIZE, "0x%lx\n",
			visorchannel_get_nbytes(vdev->visorchannel));
}

static ssize_t clientpartition_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct visor_device *vdev = to_visor_device(dev);

	if (!vdev->visorchannel)
		return 0;
	return snprintf(buf, PAGE_SIZE, "0x%Lx\n",
			visorchannel_get_clientpartition(vdev->visorchannel));
}

static ssize_t typeguid_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct visor_device *vdev = to_visor_device(dev);
	char s[99];

	if (!vdev->visorchannel)
		return 0;
	return snprintf(buf, PAGE_SIZE, "%s\n",
			visorchannel_id(vdev->visorchannel, s));
}

static ssize_t zoneguid_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct visor_device *vdev = to_visor_device(dev);
	char s[99];

	if (!vdev->visorchannel)
		return 0;
	return snprintf(buf, PAGE_SIZE, "%s\n",
			visorchannel_zoneid(vdev->visorchannel, s));
}

static ssize_t typename_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct visor_device *vdev = to_visor_device(dev);
	int i = 0;
	struct bus_type *xbus = dev->bus;
	struct device_driver *xdrv = dev->driver;
	struct visor_driver *drv = NULL;

	if (!vdev->visorchannel || !xbus || !xdrv)
		return 0;
	i = xbus->match(dev, xdrv);
	if (!i)
		return 0;
	drv = to_visor_driver(xdrv);
	return snprintf(buf, PAGE_SIZE, "%s\n", drv->channel_types[i - 1].name);
}

static ssize_t dump_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
/* TODO: replace this with debugfs code
	struct visor_device *vdev = to_visor_device(dev);
	int count = 0;
	struct seq_file *m = NULL;
	if (vdev->visorchannel == NULL)
		return 0;
	m = visor_seq_file_new_buffer(buf, PAGE_SIZE - 1);
	if (m == NULL)
		return 0;
	visorchannel_debug(vdev->visorchannel, 1, m, 0);
	count = m->count;
	visor_seq_file_done_buffer(m);
	m = NULL;
*/
	return 0;
}

static DEVICE_ATTR_RO(physaddr);
static DEVICE_ATTR_RO(nbytes);
static DEVICE_ATTR_RO(clientpartition);
static DEVICE_ATTR_RO(typeguid);
static DEVICE_ATTR_RO(zoneguid);
static DEVICE_ATTR_RO(typename);
static DEVICE_ATTR_RO(dump);

static struct attribute *channel_attrs[] = {
		&dev_attr_physaddr.attr,
		&dev_attr_nbytes.attr,
		&dev_attr_clientpartition.attr,
		&dev_attr_typeguid.attr,
		&dev_attr_zoneguid.attr,
		&dev_attr_typename.attr,
		&dev_attr_dump.attr,
};

static struct attribute_group channel_attr_grp = {
		.name = "channel",
		.attrs = channel_attrs,
};

static const struct attribute_group *visorbus_dev_groups[] = {
		&channel_attr_grp,
		NULL
};

/* end implementation of specific channel attributes */

#define to_visorbus_devdata(obj) \
	container_of(obj, struct visorbus_devdata, dev)

/*  BUS instance attributes
 *
 *  define & implement display of bus attributes under
 *  /sys/bus/visorbus/busses/visorbus<n>.
 *
 *  This is a bit hoaky because the kernel does not yet have the infrastructure
 *  to separate bus INSTANCE attributes from bus TYPE attributes...
 *  so we roll our own.  See businst.c / businst.h.
 *
 */

static ssize_t partition_handle_show(struct device *dev,
				     struct device_attribute *attr,
				     char *buf) {
	struct visor_device *vdev = to_visor_device(dev);
	u64 handle = visorchannel_get_clientpartition(vdev->visorchannel);

	return snprintf(buf, PAGE_SIZE, "0x%Lx\n", handle);
}

static ssize_t partition_guid_show(struct device *dev,
				   struct device_attribute *attr,
				   char *buf) {
	struct visor_device *vdev = to_visor_device(dev);

	return snprintf(buf, PAGE_SIZE, "{%pUb}\n", &vdev->partition_uuid);
}

static ssize_t partition_name_show(struct device *dev,
				   struct device_attribute *attr,
				   char *buf) {
	struct visor_device *vdev = to_visor_device(dev);

	return snprintf(buf, PAGE_SIZE, "%s\n", vdev->name);
}

static ssize_t channel_addr_show(struct device *dev,
				 struct device_attribute *attr,
				 char *buf) {
	struct visor_device *vdev = to_visor_device(dev);
	u64 addr = visorchannel_get_physaddr(vdev->visorchannel);

	return snprintf(buf, PAGE_SIZE, "0x%Lx\n", addr);
}

static ssize_t channel_bytes_show(struct device *dev,
				  struct device_attribute *attr,
				  char *buf) {
	struct visor_device *vdev = to_visor_device(dev);
	u64 nbytes = visorchannel_get_nbytes(vdev->visorchannel);

	return snprintf(buf, PAGE_SIZE, "0x%Lx\n", nbytes);
}

static ssize_t channel_id_show(struct device *dev,
			       struct device_attribute *attr,
			       char *buf) {
	struct visor_device *vdev = to_visor_device(dev);
	int len = 0;

	if (vdev->visorchannel) {
		visorchannel_id(vdev->visorchannel, buf);
		len = strlen(buf);
		buf[len++] = '\n';
	}
	return len;
}

static ssize_t client_bus_info_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf) {
	struct visor_device *vdev = to_visor_device(dev);
	struct visorchannel *channel = vdev->visorchannel;

	int i, x, remain = PAGE_SIZE;
	unsigned long off;
	char *p = buf;
	u8 *partition_name;
	struct ultra_vbus_deviceinfo dev_info;

	partition_name = "";
	if (channel) {
		if (vdev->name)
			partition_name = vdev->name;
		x = snprintf(p, remain,
			     "Client device / client driver info for %s partition (vbus #%ld):\n",
			     partition_name, vdev->chipset_dev_no);
		p += x;
		remain -= x;
		x = visorchannel_read(channel,
				      offsetof(struct
					       spar_vbus_channel_protocol,
					       chp_info),
				      &dev_info, sizeof(dev_info));
		if (x >= 0) {
			x = vbuschannel_devinfo_to_string(&dev_info, p,
							  remain, -1);
			p += x;
			remain -= x;
		}
		x = visorchannel_read(channel,
				      offsetof(struct
					       spar_vbus_channel_protocol,
					       bus_info),
				      &dev_info, sizeof(dev_info));
		if (x >= 0) {
			x = vbuschannel_devinfo_to_string(&dev_info, p,
							  remain, -1);
			p += x;
			remain -= x;
		}
		off = offsetof(struct spar_vbus_channel_protocol, dev_info);
		i = 0;
		while (off + sizeof(dev_info) <=
		       visorchannel_get_nbytes(channel)) {
			x = visorchannel_read(channel,
					      off, &dev_info, sizeof(dev_info));
			if (x >= 0) {
				x = vbuschannel_devinfo_to_string
				    (&dev_info, p, remain, i);
				p += x;
				remain -= x;
			}
			off += sizeof(dev_info);
			i++;
		}
	}
	return PAGE_SIZE - remain;
}

static DEVICE_ATTR_RO(partition_handle);
static DEVICE_ATTR_RO(partition_guid);
static DEVICE_ATTR_RO(partition_name);
static DEVICE_ATTR_RO(channel_addr);
static DEVICE_ATTR_RO(channel_bytes);
static DEVICE_ATTR_RO(channel_id);
static DEVICE_ATTR_RO(client_bus_info);

static struct attribute *dev_attrs[] = {
		&dev_attr_partition_handle.attr,
		&dev_attr_partition_guid.attr,
		&dev_attr_partition_name.attr,
		&dev_attr_channel_addr.attr,
		&dev_attr_channel_bytes.attr,
		&dev_attr_channel_id.attr,
		&dev_attr_client_bus_info.attr,
		NULL
};

static struct attribute_group dev_attr_grp = {
		.attrs = dev_attrs,
};

static const struct attribute_group *visorbus_groups[] = {
		&dev_attr_grp,
		NULL
};

/*  DRIVER attributes
 *
 *  define & implement display of driver attributes under
 *  /sys/bus/visorbus/drivers/<drivername>.
 *
 */

static ssize_t
DRIVER_ATTR_version(struct device_driver *xdrv, char *buf)
{
	struct visor_driver *drv = to_visor_driver(xdrv);

	return snprintf(buf, PAGE_SIZE, "%s\n", drv->version);
}

static int
register_driver_attributes(struct visor_driver *drv)
{
	int rc;
	struct driver_attribute version =
	    __ATTR(version, S_IRUGO, DRIVER_ATTR_version, NULL);
	drv->version_attr = version;
	rc = driver_create_file(&drv->driver, &drv->version_attr);
	return rc;
}

static void
unregister_driver_attributes(struct visor_driver *drv)
{
	driver_remove_file(&drv->driver, &drv->version_attr);
}

/*  DEVICE attributes
 *
 *  define & implement display of device attributes under
 *  /sys/bus/visorbus/devices/<devicename>.
 *
 */

#define DEVATTR(nam, func) { \
	.attr = { .name = __stringify(nam), \
		  .mode = 0444, \
		  .owner = THIS_MODULE },	\
	.show = func, \
}

static struct device_attribute visor_device_attrs[] = {
	/* DEVATTR(channel_nbytes, DEVICE_ATTR_channel_nbytes), */
	__ATTR_NULL
};

static void
dev_periodic_work(void *xdev)
{
	struct visor_device *dev = (struct visor_device *)xdev;
	struct visor_driver *drv = to_visor_driver(dev->device.driver);

	down(&dev->visordriver_callback_lock);
	if (drv->channel_interrupt)
		drv->channel_interrupt(dev);
	up(&dev->visordriver_callback_lock);
	if (!visor_periodic_work_nextperiod(dev->periodic_work))
		put_device(&dev->device);
}

static void
dev_start_periodic_work(struct visor_device *dev)
{
	if (dev->being_removed)
		return;
	/* now up by at least 2 */
	get_device(&dev->device);
	if (!visor_periodic_work_start(dev->periodic_work))
		put_device(&dev->device);
}

static void
dev_stop_periodic_work(struct visor_device *dev)
{
	if (visor_periodic_work_stop(dev->periodic_work))
		put_device(&dev->device);
}

/** This is called automatically upon adding a visor_device (device_add), or
 *  adding a visor_driver (visorbus_register_visor_driver), but only after
 *  visorbus_match has returned 1 to indicate a successful match between
 *  driver and device.
 */
static int
visordriver_probe_device(struct device *xdev)
{
	int rc;
	struct visor_driver *drv;
	struct visor_device *dev;

	drv = to_visor_driver(xdev->driver);
	dev = to_visor_device(xdev);
	down(&dev->visordriver_callback_lock);
	dev->being_removed = false;
	/*
	 * ensure that the dev->being_removed flag is cleared before
	 * we start the probe
	 */
	wmb();
	get_device(&dev->device);
	if (!drv->probe) {
		up(&dev->visordriver_callback_lock);
		rc = -1;
		goto away;
	}
	rc = drv->probe(dev);
	if (rc < 0)
		goto away;

	fix_vbus_dev_info(dev);
	up(&dev->visordriver_callback_lock);
	rc = 0;
away:
	if (rc != 0)
		put_device(&dev->device);
	/*  We could get here more than once if the child driver module is
	 *  unloaded and re-loaded while devices are present.  That's why we
	 *  need a flag to be sure that we only respond to the device_create
	 *  once.  We cannot respond to the device_create prior to here,
	 *  because until we call drv->probe() above, the channel has not been
	 *  initialized.
	 */
	if (!dev->responded_to_device_create) {
		struct visorchipset_device_info dev_info;

		if (!visorchipset_get_device_info(dev->chipset_bus_no,
						  dev->chipset_dev_no,
						  &dev_info))
			/* hmm, what to do here */
			return rc;

		dev->responded_to_device_create = true;
		if (chipset_responders.device_create)
			(*chipset_responders.device_create)(&dev_info, rc);
	}
	return rc;
}

/** This is called when device_unregister() is called for each child device
 *  instance, to notify the appropriate visorbus_driver that the device is
 *  going away, and to decrease the reference count of the device.
 */
static int
visordriver_remove_device(struct device *xdev)
{
	int rc = 0;
	struct visor_device *dev;
	struct visor_driver *drv;

	dev = to_visor_device(xdev);
	drv = to_visor_driver(xdev->driver);
	down(&dev->visordriver_callback_lock);
	dev->being_removed = true;
	/*
	 * ensure that the dev->being_removed flag is set before we start the
	 * actual removal
	 */
	wmb();
	if (drv) {
		if (drv->remove)
			drv->remove(dev);
	}
	up(&dev->visordriver_callback_lock);
	dev_stop_periodic_work(dev);
	devmajorminor_remove_all_files(dev);

	put_device(&dev->device);

	return rc;
}

/** A particular type of visor driver calls this function to register
 *  the driver.  The caller MUST fill in the following fields within the
 *  #drv structure:
 *      name, version, owner, channel_types, probe, remove
 *
 *  Here's how the whole Linux bus / driver / device model works.
 *
 *  At system start-up, the visorbus kernel module is loaded, which registers
 *  visorbus_type as a bus type, using bus_register().
 *
 *  All kernel modules that support particular device types on a
 *  visorbus bus are loaded.  Each of these kernel modules calls
 *  visorbus_register_visor_driver() in their init functions, passing a
 *  visor_driver struct.  visorbus_register_visor_driver() in turn calls
 *  register_driver(&visor_driver.driver).  This .driver member is
 *  initialized with generic methods (like probe), whose sole responsibility
 *  is to act as a broker for the real methods, which are within the
 *  visor_driver struct.  (This is the way the subclass behavior is
 *  implemented, since visor_driver is essentially a subclass of the
 *  generic driver.)  Whenever a driver_register() happens, core bus code in
 *  the kernel does (see device_attach() in drivers/base/dd.c):
 *
 *      for each dev associated with the bus (the bus that driver is on) that
 *      does not yet have a driver
 *          if bus.match(dev,newdriver) == yes_matched  ** .match specified
 *                                                 ** during bus_register().
 *              newdriver.probe(dev)  ** for visor drivers, this will call
 *                    ** the generic driver.probe implemented in visorbus.c,
 *                    ** which in turn calls the probe specified within the
 *                    ** struct visor_driver (which was specified by the
 *                    ** actual device driver as part of
 *                    ** visorbus_register_visor_driver()).
 *
 *  The above dance also happens when a new device appears.
 *  So the question is, how are devices created within the system?
 *  Basically, just call device_add(dev).  See pci_bus_add_devices().
 *  pci_scan_device() shows an example of how to build a device struct.  It
 *  returns the newly-created struct to pci_scan_single_device(), who adds it
 *  to the list of devices at PCIBUS.devices.  That list of devices is what
 *  is traversed by pci_bus_add_devices().
 *
 */
int visorbus_register_visor_driver(struct visor_driver *drv)
{
	int rc = 0;

	drv->driver.name = drv->name;
	drv->driver.bus = &visorbus_type;
	drv->driver.probe = visordriver_probe_device;
	drv->driver.remove = visordriver_remove_device;
	drv->driver.owner = drv->owner;

	/* driver_register does this:
	 *   bus_add_driver(drv)
	 *   ->if (drv.bus)  ** (bus_type) **
	 *       driver_attach(drv)
	 *         for each dev with bus type of drv.bus
	 *           if (!dev.drv)  ** no driver assigned yet **
	 *             if (bus.match(dev,drv))  [visorbus_match]
	 *               dev.drv = drv
	 *               if (!drv.probe(dev))   [visordriver_probe_device]
	 *                 dev.drv = NULL
	 */

	rc = driver_register(&drv->driver);
	if (rc < 0)
		return rc;
	rc = register_driver_attributes(drv);
	return rc;
}
EXPORT_SYMBOL_GPL(visorbus_register_visor_driver);

/** A particular type of visor driver calls this function to unregister
 *  the driver, i.e., within its module_exit function.
 */
void
visorbus_unregister_visor_driver(struct visor_driver *drv)
{
	unregister_driver_attributes(drv);
	driver_unregister(&drv->driver);
}
EXPORT_SYMBOL_GPL(visorbus_unregister_visor_driver);

int
visorbus_read_channel(struct visor_device *dev, unsigned long offset,
		      void *dest, unsigned long nbytes)
{
	return visorchannel_read(dev->visorchannel, offset, dest, nbytes);
}
EXPORT_SYMBOL_GPL(visorbus_read_channel);

int
visorbus_write_channel(struct visor_device *dev, unsigned long offset,
		       void *src, unsigned long nbytes)
{
	return visorchannel_write(dev->visorchannel, offset, src, nbytes);
}
EXPORT_SYMBOL_GPL(visorbus_write_channel);

int
visorbus_clear_channel(struct visor_device *dev, unsigned long offset, u8 ch,
		       unsigned long nbytes)
{
	return visorchannel_clear(dev->visorchannel, offset, ch, nbytes);
}
EXPORT_SYMBOL_GPL(visorbus_clear_channel);

int
visorbus_registerdevnode(struct visor_device *dev,
			 const char *name, int major, int minor)
{
	return devmajorminor_create_file(dev, name, major, minor);
}
EXPORT_SYMBOL_GPL(visorbus_registerdevnode);

/** We don't really have a real interrupt, so for now we just call the
 *  interrupt function periodically...
 */
void
visorbus_enable_channel_interrupts(struct visor_device *dev)
{
	dev_start_periodic_work(dev);
}
EXPORT_SYMBOL_GPL(visorbus_enable_channel_interrupts);

void
visorbus_disable_channel_interrupts(struct visor_device *dev)
{
	dev_stop_periodic_work(dev);
}
EXPORT_SYMBOL_GPL(visorbus_disable_channel_interrupts);

/** This is how everything starts from the device end.
 *  This function is called when a channel first appears via a ControlVM
 *  message.  In response, this function allocates a visor_device to
 *  correspond to the new channel, and attempts to connect it the appropriate
 *  driver.  If the appropriate driver is found, the visor_driver.probe()
 *  function for that driver will be called, and will be passed the new
 *  visor_device that we just created.
 *
 *  It's ok if the appropriate driver is not yet loaded, because in that case
 *  the new device struct will just stick around in the bus' list of devices.
 *  When the appropriate driver calls visorbus_register_visor_driver(), the
 *  visor_driver.probe() for the new driver will be called with the new
 *  device.
 */
static int
create_visor_device(struct visorbus_devdata *devdata,
		    struct visorchipset_device_info *dev_info,
		    u64 partition_handle)
{
	int rc = -1;
	struct visor_device *dev = NULL;
	bool gotten = false, registered1 = false, registered2 = false;
	u32 chipset_bus_no = dev_info->bus_no;
	u32 chipset_dev_no = dev_info->dev_no;

	POSTCODE_LINUX_4(DEVICE_CREATE_ENTRY_PC, chipset_dev_no, chipset_bus_no,
			 POSTCODE_SEVERITY_INFO);
	dev = kmalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		POSTCODE_LINUX_3(DEVICE_CREATE_FAILURE_PC, chipset_dev_no,
				 DIAG_SEVERITY_ERR);
		goto away;
	}

	memset(dev, 0, sizeof(struct visor_device));
	dev->visorchannel = dev_info->visorchannel;
	dev->channel_type_guid = dev_info->channel_type_guid;
	dev->chipset_bus_no = chipset_bus_no;
	dev->chipset_dev_no = chipset_dev_no;
	dev->device.parent = &devdata->dev;
	sema_init(&dev->visordriver_callback_lock, 1);	/* unlocked */
	dev->device.bus = &visorbus_type;
	dev->device.groups = visorbus_dev_groups;
	device_initialize(&dev->device);
	dev->device.release = visorbus_release_device;
	/* keep a reference just for us (now 2) */
	get_device(&dev->device);
	gotten = true;
	dev->periodic_work =
		visor_periodic_work_create(POLLJIFFIES_NORMALCHANNEL,
					   periodic_dev_workqueue,
					   dev_periodic_work,
					   dev, dev_name(&dev->device));
	if (!dev->periodic_work) {
		POSTCODE_LINUX_3(DEVICE_CREATE_FAILURE_PC, chipset_dev_no,
				 DIAG_SEVERITY_ERR);
		goto away;
	}

	/* bus_id must be a unique name with respect to this bus TYPE
	 * (NOT bus instance).  That's why we need to include the bus
	 * number within the name.
	 */
	dev_set_name(&dev->device, "vbus%u:dev%u",
		     chipset_bus_no, chipset_dev_no);

	/*  device_add does this:
	 *    bus_add_device(dev)
	 *    ->device_attach(dev)
	 *      ->for each driver drv registered on the bus that dev is on
	 *          if (dev.drv)  **  device already has a driver **
	 *            ** not sure we could ever get here... **
	 *          else
	 *            if (bus.match(dev,drv)) [visorbus_match]
	 *              dev.drv = drv
	 *              if (!drv.probe(dev))  [visordriver_probe_device]
	 *                dev.drv = NULL
	 *
	 *  Note that device_add does NOT fail if no driver failed to
	 *  claim the device.  The device will be linked onto
	 *  bus_type.klist_devices regardless (use bus_for_each_dev).
	 */
	rc = device_add(&dev->device);
	if (rc < 0) {
		POSTCODE_LINUX_3(DEVICE_ADD_PC, chipset_bus_no,
				 DIAG_SEVERITY_ERR);
		goto away;
	}

	/* note: device_register is simply device_initialize + device_add */
	registered1 = true;

	rc = register_devmajorminor_attributes(dev);
	if (rc < 0) {
		POSTCODE_LINUX_3(DEVICE_REGISTER_FAILURE_PC, chipset_dev_no,
				 DIAG_SEVERITY_ERR);
		goto away;
	}

	registered2 = true;
	rc = 0;

away:
	if (rc < 0) {
		if (registered2)
			unregister_devmajorminor_attributes(dev);
		if (gotten)
			put_device(&dev->device);
		kfree(dev);
	} else {
		total_devices_created++;
		list_add_tail(&dev->list_all, &list_all_device_instances);
	}
	return rc;
}

static void
remove_visor_device(struct visor_device *dev)
{
	list_del(&dev->list_all);
	unregister_devmajorminor_attributes(dev);
	put_device(&dev->device);
	device_unregister(&dev->device);
}

static struct visor_device *
find_visor_device_by_channel(struct visorchannel *channel)
{
	struct list_head *listentry, *listtmp;

	list_for_each_safe(listentry, listtmp, &list_all_device_instances) {
		struct visor_device *dev = list_entry(listentry,
						      struct visor_device,
						      list_all);
		if (dev->visorchannel == channel)
			return dev;
	}
	return NULL;
}

static int
init_vbus_channel(struct visorchannel *chan)
{
	int rc = -1;
	unsigned long allocated_bytes = visorchannel_get_nbytes(chan);
	struct spar_vbus_channel_protocol *x =
		kmalloc(sizeof(struct spar_vbus_channel_protocol),
			GFP_KERNEL);

	POSTCODE_LINUX_3(VBUS_CHANNEL_ENTRY_PC, rc, POSTCODE_SEVERITY_INFO);

	if (x) {
		POSTCODE_LINUX_2(MALLOC_FAILURE_PC, POSTCODE_SEVERITY_ERR);
		goto away;
	}
	if (visorchannel_clear(chan, 0, 0, allocated_bytes) < 0) {
		POSTCODE_LINUX_2(VBUS_CHANNEL_FAILURE_PC,
				 POSTCODE_SEVERITY_ERR);
		goto away;
	}
	if (visorchannel_read
	    (chan, 0, x, sizeof(struct spar_vbus_channel_protocol)) < 0) {
		POSTCODE_LINUX_2(VBUS_CHANNEL_FAILURE_PC,
				 POSTCODE_SEVERITY_ERR);
		goto away;
	}
	if (!SPAR_VBUS_CHANNEL_OK_SERVER(allocated_bytes)) {
		POSTCODE_LINUX_2(VBUS_CHANNEL_FAILURE_PC,
				 POSTCODE_SEVERITY_ERR);
		goto away;
	}

	if (visorchannel_write
	    (chan, 0, x, sizeof(struct spar_vbus_channel_protocol)) < 0) {
		POSTCODE_LINUX_3(VBUS_CHANNEL_FAILURE_PC, chan,
				 POSTCODE_SEVERITY_ERR);
		goto away;
	}

	POSTCODE_LINUX_3(VBUS_CHANNEL_EXIT_PC, chan, POSTCODE_SEVERITY_INFO);
	rc = 0;

away:
	kfree(x);
	x = NULL;
	return rc;
}

static int
get_vbus_header_info(struct visorchannel *chan,
		     struct spar_vbus_headerinfo *hdr_info)
{
	int rc = -1;

	if (!SPAR_VBUS_CHANNEL_OK_CLIENT(visorchannel_get_header(chan)))
		goto away;
	if (visorchannel_read(chan, sizeof(struct channel_header), hdr_info,
			      sizeof(*hdr_info)) < 0) {
		goto away;
	}
	if (hdr_info->struct_bytes < sizeof(struct spar_vbus_headerinfo))
		goto away;
	if (hdr_info->device_info_struct_bytes <
	    sizeof(struct ultra_vbus_deviceinfo)) {
		goto away;
	}
	rc = 0;
away:
	return rc;
}

/* Write the contents of <info> to the struct
 * spar_vbus_channel_protocol.chp_info. */

static int
write_vbus_chp_info(struct visorchannel *chan,
		    struct spar_vbus_headerinfo *hdr_info,
		    struct ultra_vbus_deviceinfo *info)
{
	int off = sizeof(struct channel_header) + hdr_info->chp_info_offset;

	if (hdr_info->chp_info_offset == 0)
			return -1;

	if (visorchannel_write(chan, off, info, sizeof(*info)) < 0)
			return -1;
	return 0;
}

/* Write the contents of <info> to the struct
 * spar_vbus_channel_protocol.bus_info. */

static int
write_vbus_bus_info(struct visorchannel *chan,
		    struct spar_vbus_headerinfo *hdr_info,
		    struct ultra_vbus_deviceinfo *info)
{
	int off = sizeof(struct channel_header) + hdr_info->bus_info_offset;

	if (hdr_info->bus_info_offset == 0)
			return -1;

	if (visorchannel_write(chan, off, info, sizeof(*info)) < 0)
			return -1;
	return 0;
}

/* Write the contents of <info> to the
 * struct spar_vbus_channel_protocol.dev_info[<devix>].
 */
static int
write_vbus_dev_info(struct visorchannel *chan,
		    struct spar_vbus_headerinfo *hdr_info,
		    struct ultra_vbus_deviceinfo *info, int devix)
{
	int off =
	    (sizeof(struct channel_header) + hdr_info->dev_info_offset) +
	    (hdr_info->device_info_struct_bytes * devix);

	if (hdr_info->dev_info_offset == 0)
			return -1;

	if (visorchannel_write(chan, off, info, sizeof(*info)) < 0)
			return -1;
	return 0;
}

/* For a child device just created on a client bus, fill in
 * information about the driver that is controlling this device into
 * the the appropriate slot within the vbus channel of the bus
 * instance.
 */
static void
fix_vbus_dev_info(struct visor_device *visordev)
{
	int i;
	struct visorchipset_bus_info bus_info;
	struct visorbus_devdata *devdata = NULL;
	struct visor_driver *visordrv;
	int bus_no = visordev->chipset_bus_no;
	int dev_no = visordev->chipset_dev_no;
	struct ultra_vbus_deviceinfo dev_info;
	const char *chan_type_name = NULL;
	struct spar_vbus_headerinfo *hdr_info;

	if (!visordev->device.driver)
			return;

	hdr_info = (struct spar_vbus_headerinfo *)visordev->vbus_hdr_info;

	visordrv = to_visor_driver(visordev->device.driver);
	if (!visorchipset_get_bus_info(bus_no, &bus_info))
			return;

	devdata = (struct visorbus_devdata *)(bus_info.bus_driver_context);
	if (!devdata)
			return;

	if (!hdr_info)
			return;

	/* Within the list of device types (by GUID) that the driver
	 * says it supports, find out which one of those types matches
	 * the type of this device, so that we can include the device
	 * type name
	 */
	for (i = 0; visordrv->channel_types[i].name; i++) {
		if (memcmp(&visordrv->channel_types[i].guid,
			   &visordev->channel_type_guid,
			   sizeof(visordrv->channel_types[i].guid)) == 0) {
			chan_type_name = visordrv->channel_types[i].name;
			break;
		}
	}

	bus_device_info_init(&dev_info, chan_type_name,
			     visordrv->name, visordrv->version,
			     visordrv->vertag);
	write_vbus_dev_info(devdata->chan, hdr_info, &dev_info, dev_no);

	/* Re-write bus+chipset info, because it is possible that this
	* was previously written by our evil counterpart, virtpci.
	*/
	write_vbus_chp_info(devdata->chan, hdr_info, &chipset_driverinfo);
	write_vbus_bus_info(devdata->chan, hdr_info, &clientbus_driverinfo);
}

/** Create a device instance for the visor bus itself.
 */
static struct visorbus_devdata *
create_bus_instance(struct visorchipset_bus_info *bus_info)
{
	struct visorbus_devdata *rc = NULL;
	struct visorbus_devdata *devdata = NULL;
	int id = bus_info->bus_no;
	struct spar_vbus_headerinfo *hdr_info;

	POSTCODE_LINUX_2(BUS_CREATE_ENTRY_PC, POSTCODE_SEVERITY_INFO);
	devdata = kzalloc(sizeof(*devdata), GFP_KERNEL);
	if (!devdata) {
		POSTCODE_LINUX_2(MALLOC_FAILURE_PC, POSTCODE_SEVERITY_ERR);
		rc = NULL;
		goto away;
	}

	hdr_info = kzalloc(sizeof(*hdr_info), GFP_KERNEL);
	if (!hdr_info) {
		rc = NULL;
		goto away_mem;
	}

	dev_set_name(&devdata->dev, "visorbus%d", id);
	devdata->dev.bus = &visorbus_type;
	devdata->dev.groups = visorbus_groups;
	devdata->dev.release = visorbus_release_busdevice;
	if (device_register(&devdata->dev) < 0) {
		POSTCODE_LINUX_3(DEVICE_CREATE_FAILURE_PC, id,
				 POSTCODE_SEVERITY_ERR);
		rc = NULL;
		goto away_mem2;
	}
	devdata->devno = id;
	devdata->chan = bus_info->visorchannel;
	if (bus_info->flags.server) {
		init_vbus_channel(devdata->chan);
	} else {
		if (get_vbus_header_info(devdata->chan, hdr_info) >= 0) {
			devdata->vbus_hdr_info = (void *)hdr_info;
			write_vbus_chp_info(devdata->chan, hdr_info,
					    &chipset_driverinfo);
			write_vbus_bus_info(devdata->chan, hdr_info,
					    &clientbus_driverinfo);
		}
	}
	bus_count++;
	list_add_tail(&devdata->list_all, &list_all_bus_instances);
	if (id == 0)
			devdata = devdata;	/* for testing ONLY */
	dev_set_drvdata(&devdata->dev, devdata);
	rc = devdata;
	return rc;

away_mem2:
	kfree(hdr_info);
away_mem:
	kfree(devdata);
away:
	return rc;
}

/** Remove a device instance for the visor bus itself.
 */
static void
remove_bus_instance(struct visorbus_devdata *devdata)
{
	/* Note that this will result in the release method for
	 * devdata->dev being called, which will call
	 * visorbus_release_busdevice().  This has something to do with
	 * the put_device() done in device_unregister(), but I have never
	 * successfully been able to trace thru the code to see where/how
	 * release() gets called.  But I know it does.
	 */
	bus_count--;
	if (devdata->chan) {
		visorchannel_destroy(devdata->chan);
		devdata->chan = NULL;
	}
	kfree(devdata->vbus_hdr_info);
	list_del(&devdata->list_all);
	device_unregister(&devdata->dev);
}

/** Create and register the one-and-only one instance of
 *  the visor bus type (visorbus_type).
 */
static int
create_bus_type(void)
{
	int rc = 0;

	visorbus_type.dev_attrs = visor_device_attrs;
	rc = bus_register(&visorbus_type);
	return rc;
}

/** Remove the one-and-only one instance of the visor bus type (visorbus_type).
 */
static void
remove_bus_type(void)
{
	bus_unregister(&visorbus_type);
}

/** Remove all child visor bus device instances.
 */
static void
remove_all_visor_devices(void)
{
	struct list_head *listentry, *listtmp;

	list_for_each_safe(listentry, listtmp, &list_all_device_instances) {
		struct visor_device *dev = list_entry(listentry,
						      struct visor_device,
						      list_all);
		remove_visor_device(dev);
	}
}

static bool entered_testing_mode;
static struct visorchannel *test_channel_infos[MAXDEVICETEST];
static unsigned long test_bus_nos[MAXDEVICETEST];
static unsigned long test_dev_nos[MAXDEVICETEST];

static void
chipset_bus_create(struct visorchipset_bus_info *bus_info)
{
	struct visorbus_devdata *devdata;
	int rc = -1;
	u32 bus_no = bus_info->bus_no;

	POSTCODE_LINUX_3(BUS_CREATE_ENTRY_PC, bus_no, POSTCODE_SEVERITY_INFO);
	devdata = create_bus_instance(bus_info);
	if (!devdata)
		goto away;
	if (!visorchipset_set_bus_context(bus_info, devdata))
		goto away;
	POSTCODE_LINUX_3(BUS_CREATE_EXIT_PC, bus_no, POSTCODE_SEVERITY_INFO);
	rc = 0;
away:
	if (rc < 0) {
		POSTCODE_LINUX_3(BUS_CREATE_FAILURE_PC, bus_no,
				 POSTCODE_SEVERITY_ERR);
		return;
	}
	POSTCODE_LINUX_3(CHIPSET_INIT_SUCCESS_PC, bus_no,
			 POSTCODE_SEVERITY_INFO);
	if (chipset_responders.bus_create)
		(*chipset_responders.bus_create) (bus_info, rc);
}

static void
chipset_bus_destroy(struct visorchipset_bus_info *bus_info)
{
	struct visorbus_devdata *devdata;
	int rc = -1;

	devdata = (struct visorbus_devdata *)(bus_info->bus_driver_context);
	if (!devdata)
		goto away;
	remove_bus_instance(devdata);
	if (!visorchipset_set_bus_context(bus_info, NULL))
		goto away;
	rc = 0;
away:
	if (rc < 0)
		return;
	if (chipset_responders.bus_destroy)
		(*chipset_responders.bus_destroy)(bus_info, rc);
}

static void
chipset_device_create(struct visorchipset_device_info *dev_info)
{
	struct visorchipset_bus_info bus_info;
	struct visorbus_devdata *devdata = NULL;
	int rc = -1;
	u32 bus_no = dev_info->bus_no;
	u32 dev_no = dev_info->dev_no;

	POSTCODE_LINUX_4(DEVICE_CREATE_ENTRY_PC, dev_no, bus_no,
			 POSTCODE_SEVERITY_INFO);

	if (entered_testing_mode)
		return;
	if (!visorchipset_get_bus_info(bus_no, &bus_info))
		goto away;
	if (visorbus_devicetest)
		if (total_devices_created < MAXDEVICETEST) {
			test_channel_infos[total_devices_created] =
			    dev_info->visorchannel;
			test_bus_nos[total_devices_created] = bus_no;
			test_dev_nos[total_devices_created] = dev_no;
		}
	POSTCODE_LINUX_4(DEVICE_CREATE_EXIT_PC, dev_no, bus_no,
			 POSTCODE_SEVERITY_INFO);
	rc = 0;
away:
	if (rc < 0) {
		POSTCODE_LINUX_4(DEVICE_CREATE_FAILURE_PC, dev_no, bus_no,
				 POSTCODE_SEVERITY_ERR);
		return;
	}
	devdata = (struct visorbus_devdata *)(bus_info.bus_driver_context);
	rc = create_visor_device(devdata, dev_info, bus_info.partition_handle);
	POSTCODE_LINUX_4(DEVICE_CREATE_SUCCESS_PC, dev_no, bus_no,
			 POSTCODE_SEVERITY_INFO);
	if (rc < 0)
		if (chipset_responders.device_create)
			(*chipset_responders.device_create)(dev_info, rc);
}

static void
chipset_device_destroy(struct visorchipset_device_info *dev_info)
{
	struct visor_device *dev;
	int rc = -1;

	if (entered_testing_mode)
		return;
	dev = find_visor_device_by_channel(dev_info->visorchannel);
	if (!dev)
		goto away;
	rc = 0;
away:
	if (rc < 0)
			return;

	if (chipset_responders.device_destroy)
		(*chipset_responders.device_destroy) (dev_info, rc);
	remove_visor_device(dev);
}

/* This is the callback function specified for a function driver, to
 * be called when a pending "pause device" operation has been
 * completed.
 */
static void
pause_state_change_complete(struct visor_device *dev, int status,
			    void *info)
{
	struct visorchipset_device_info *dev_info = info;

	if (!dev->pausing)
			return;

	dev->pausing = false;
	if (!chipset_responders.device_pause) /* this can never happen! */
			return;

	/* Notify the chipset driver that the pause is complete, which
	* will presumably want to send some sort of response to the
	* initiator. */
	(*chipset_responders.device_pause) (dev_info, status);
}

/* This is the callback function specified for a function driver, to
 * be called when a pending "resume device" operation has been
 * completed.
 */
static void
resume_state_change_complete(struct visor_device *dev, int status,
			     void *info)
{
	struct visorchipset_device_info *dev_info = info;

	if (!dev->resuming)
			return;

	dev->resuming = false;
	if (!chipset_responders.device_resume) /* this can never happen! */
			return;

	/* Notify the chipset driver that the resume is complete,
	 * which will presumably want to send some sort of response to
	 * the initiator. */
	(*chipset_responders.device_resume) (dev_info, status);
}

/* Tell the subordinate function driver for a specific device to pause
 * or resume that device.  Result is returned asynchronously via a
 * callback function.
 */
static void
initiate_chipset_device_pause_resume(struct visorchipset_device_info *dev_info,
				     bool is_pause)
{
	struct visor_device *dev = NULL;
	int rc = -1, x;
	struct visor_driver *drv = NULL;
	void (*notify_func)(struct visorchipset_device_info *dev_info,
			    int response) = NULL;

	if (is_pause)
		notify_func = chipset_responders.device_pause;
	else
		notify_func = chipset_responders.device_resume;
	if (!notify_func)
			goto away;

	dev = find_visor_device_by_channel(dev_info->visorchannel);
	if (!dev)
			goto away;

	drv = to_visor_driver(dev->device.driver);
	if (!drv)
			goto away;

	if (dev->pausing || dev->resuming)
			goto away;

	/* Note that even though both drv->pause() and drv->resume
	 * specify a callback function, it is NOT necessary for us to
	 * increment our local module usage count.  Reason is, there
	 * is already a linkage dependency between child function
	 * drivers and visorbus, so it is already IMPOSSIBLE to unload
	 * visorbus while child function drivers are still running.
	 */
	if (is_pause) {
		if (!drv->pause)
				goto away;

		dev->pausing = true;
		x = drv->pause(dev, pause_state_change_complete,
			       (void *)dev_info);
	} else {
		/* This should be done at BUS resume time, but an
		 * existing problem prevents us from ever getting a bus
		 * resume...  This hack would fail to work should we
		 * ever have a bus that contains NO devices, since we
		 * would never even get here in that case. */
		fix_vbus_dev_info(dev);
		if (!drv->resume)
				goto away;

		dev->resuming = true;
		x = drv->resume(dev, resume_state_change_complete,
				(void *)dev_info);
	}
	if (x < 0) {
		if (is_pause)
			dev->pausing = false;
		else
			dev->resuming = false;
		goto away;
	}
	rc = 0;
away:
	if (rc < 0) {
		if (notify_func)
				(*notify_func)(dev_info, rc);
	}
}

static void
chipset_device_pause(struct visorchipset_device_info *dev_info)
{
	initiate_chipset_device_pause_resume(dev_info, true);
}

static void
chipset_device_resume(struct visorchipset_device_info *dev_info)
{
	initiate_chipset_device_pause_resume(dev_info, false);
}

struct channel_size_info {
	uuid_le guid;
	unsigned long min_size;
	unsigned long max_size;
};

int
visorbus_init(void)
{
	int rc = 0;

	POSTCODE_LINUX_3(DRIVER_ENTRY_PC, rc, POSTCODE_SEVERITY_INFO);
	bus_device_info_init(&clientbus_driverinfo,
			     "clientbus", "visorbus",
			     VERSION, NULL);

	/* process module options */

	if (visorbus_devicetest > MAXDEVICETEST)
			visorbus_devicetest = MAXDEVICETEST;

	rc = create_bus_type();
	if (rc < 0) {
		POSTCODE_LINUX_2(BUS_CREATE_ENTRY_PC, DIAG_SEVERITY_ERR);
		goto away;
	}

	periodic_dev_workqueue = create_singlethread_workqueue("visorbus_dev");
	if (!periodic_dev_workqueue) {
		POSTCODE_LINUX_2(CREATE_WORKQUEUE_PC, DIAG_SEVERITY_ERR);
		rc = -ENOMEM;
		goto away;
	}

	/* This enables us to receive notifications when devices appear for
	 * which this service partition is to be a server for.
	 */
	visorchipset_register_busdev(&chipset_notifiers,
				     &chipset_responders,
				     &chipset_driverinfo);

	rc = 0;

away:
	if (rc)
			POSTCODE_LINUX_3(CHIPSET_INIT_FAILURE_PC, rc,
					 POSTCODE_SEVERITY_ERR);
	return rc;
}

void
visorbus_exit(void)
{
	struct list_head *listentry, *listtmp;

	visorchipset_register_busdev(NULL, NULL, NULL);
	remove_all_visor_devices();

	flush_workqueue(periodic_dev_workqueue); /* better not be any work! */
	destroy_workqueue(periodic_dev_workqueue);
	periodic_dev_workqueue = NULL;

	if (periodic_test_workqueue) {
		cancel_delayed_work(&periodic_work);
		flush_workqueue(periodic_test_workqueue);
		destroy_workqueue(periodic_test_workqueue);
		periodic_test_workqueue = NULL;
	}

	list_for_each_safe(listentry, listtmp, &list_all_bus_instances) {
		struct visorbus_devdata *devdata = list_entry(listentry,
							      struct
							      visorbus_devdata,
							      list_all);
		remove_bus_instance(devdata);
	}
	remove_bus_type();
}

module_param_named(debug, visorbus_debug, int, S_IRUGO);
MODULE_PARM_DESC(visorbus_debug, "1 to debug");

module_param_named(forcematch, visorbus_forcematch, int, S_IRUGO);
MODULE_PARM_DESC(visorbus_forcematch,
		 "1 to force a successful dev <--> drv match");

module_param_named(forcenomatch, visorbus_forcenomatch, int, S_IRUGO);
MODULE_PARM_DESC(visorbus_forcenomatch,
		 "1 to force an UNsuccessful dev <--> drv match");

module_param_named(devicetest, visorbus_devicetest, int, S_IRUGO);
MODULE_PARM_DESC(visorbus_devicetest,
		 "non-0 to just test device creation and destruction");

module_param_named(debugref, visorbus_debugref, int, S_IRUGO);
MODULE_PARM_DESC(visorbus_debugref, "1 to debug reference counting");

MODULE_AUTHOR("Unisys");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Supervisor bus driver for service partition: ver " VERSION);
MODULE_VERSION(VERSION);
