// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2017 Netronome Systems, Inc.
 * Copyright (C) 2019 Mellanox Technologies. All rights reserved
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/rtnetlink.h>
#include <linux/slab.h>
#include <linux/sysfs.h>

#include "netdevsim.h"

static u32 nsim_bus_dev_id;

static struct nsim_bus_dev *to_nsim_bus_dev(struct device *dev)
{
	return container_of(dev, struct nsim_bus_dev, dev);
}

static int nsim_bus_dev_vfs_enable(struct nsim_bus_dev *nsim_bus_dev,
				   unsigned int num_vfs)
{
	nsim_bus_dev->vfconfigs = kcalloc(num_vfs,
					  sizeof(struct nsim_vf_config),
					  GFP_KERNEL);
	if (!nsim_bus_dev->vfconfigs)
		return -ENOMEM;
	nsim_bus_dev->num_vfs = num_vfs;

	return 0;
}

static void nsim_bus_dev_vfs_disable(struct nsim_bus_dev *nsim_bus_dev)
{
	kfree(nsim_bus_dev->vfconfigs);
	nsim_bus_dev->vfconfigs = NULL;
	nsim_bus_dev->num_vfs = 0;
}

static ssize_t
nsim_bus_dev_numvfs_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct nsim_bus_dev *nsim_bus_dev = to_nsim_bus_dev(dev);
	unsigned int num_vfs;
	int ret;

	ret = kstrtouint(buf, 0, &num_vfs);
	if (ret)
		return ret;

	rtnl_lock();
	if (nsim_bus_dev->num_vfs == num_vfs)
		goto exit_good;
	if (nsim_bus_dev->num_vfs && num_vfs) {
		ret = -EBUSY;
		goto exit_unlock;
	}

	if (num_vfs) {
		ret = nsim_bus_dev_vfs_enable(nsim_bus_dev, num_vfs);
		if (ret)
			goto exit_unlock;
	} else {
		nsim_bus_dev_vfs_disable(nsim_bus_dev);
	}
exit_good:
	ret = count;
exit_unlock:
	rtnl_unlock();

	return ret;
}

static ssize_t
nsim_bus_dev_numvfs_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct nsim_bus_dev *nsim_bus_dev = to_nsim_bus_dev(dev);

	return sprintf(buf, "%u\n", nsim_bus_dev->num_vfs);
}

static struct device_attribute nsim_bus_dev_numvfs_attr =
	__ATTR(sriov_numvfs, 0664, nsim_bus_dev_numvfs_show,
	       nsim_bus_dev_numvfs_store);

static struct attribute *nsim_bus_dev_attrs[] = {
	&nsim_bus_dev_numvfs_attr.attr,
	NULL,
};

static const struct attribute_group nsim_bus_dev_attr_group = {
	.attrs = nsim_bus_dev_attrs,
};

static const struct attribute_group *nsim_bus_dev_attr_groups[] = {
	&nsim_bus_dev_attr_group,
	NULL,
};

static void nsim_bus_dev_release(struct device *dev)
{
	struct nsim_bus_dev *nsim_bus_dev = to_nsim_bus_dev(dev);

	nsim_bus_dev_vfs_disable(nsim_bus_dev);
}

static struct device_type nsim_bus_dev_type = {
	.groups = nsim_bus_dev_attr_groups,
	.release = nsim_bus_dev_release,
};

int nsim_num_vf(struct device *dev)
{
	struct nsim_bus_dev *nsim_bus_dev = to_nsim_bus_dev(dev);

	return nsim_bus_dev->num_vfs;
}

static struct bus_type nsim_bus = {
	.name		= DRV_NAME,
	.dev_name	= DRV_NAME,
	.num_vf		= nsim_num_vf,
};

struct nsim_bus_dev *nsim_bus_dev_new(void)
{
	struct nsim_bus_dev *nsim_bus_dev;
	int err;

	nsim_bus_dev = kzalloc(sizeof(*nsim_bus_dev), GFP_KERNEL);
	if (!nsim_bus_dev)
		return ERR_PTR(-ENOMEM);

	nsim_bus_dev->dev.id = nsim_bus_dev_id++;
	nsim_bus_dev->dev.bus = &nsim_bus;
	nsim_bus_dev->dev.type = &nsim_bus_dev_type;
	err = device_register(&nsim_bus_dev->dev);
	if (err)
		goto err_nsim_bus_dev_free;
	return nsim_bus_dev;

err_nsim_bus_dev_free:
	kfree(nsim_bus_dev);
	return ERR_PTR(err);
}

void nsim_bus_dev_del(struct nsim_bus_dev *nsim_bus_dev)
{
	device_unregister(&nsim_bus_dev->dev);
	kfree(nsim_bus_dev);
}

static struct device_driver nsim_driver = {
	.name		= DRV_NAME,
	.bus		= &nsim_bus,
	.owner		= THIS_MODULE,
};

int nsim_bus_init(void)
{
	int err;

	err = bus_register(&nsim_bus);
	if (err)
		return err;
	err = driver_register(&nsim_driver);
	if (err)
		goto err_bus_unregister;
	return 0;

err_bus_unregister:
	bus_unregister(&nsim_bus);
	return err;
}

void nsim_bus_exit(void)
{
	driver_unregister(&nsim_driver);
	bus_unregister(&nsim_bus);
}
