// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2017 Netronome Systems, Inc.
 * Copyright (C) 2019 Mellanox Technologies. All rights reserved
 */

#include <linux/device.h>
#include <linux/idr.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/rtnetlink.h>
#include <linux/slab.h>
#include <linux/sysfs.h>

#include "netdevsim.h"

static DEFINE_IDA(nsim_bus_dev_ids);
static LIST_HEAD(nsim_bus_dev_list);
static DEFINE_MUTEX(nsim_bus_dev_list_lock);
static bool nsim_bus_enable;

static struct nsim_bus_dev *to_nsim_bus_dev(struct device *dev)
{
	return container_of(dev, struct nsim_bus_dev, dev);
}

static void
nsim_bus_dev_set_vfs(struct nsim_bus_dev *nsim_bus_dev, unsigned int num_vfs)
{
	rtnl_lock();
	nsim_bus_dev->num_vfs = num_vfs;
	rtnl_unlock();
}

static int nsim_bus_dev_vfs_enable(struct nsim_bus_dev *nsim_bus_dev,
				   unsigned int num_vfs)
{
	struct nsim_dev *nsim_dev;
	int err = 0;

	if (nsim_bus_dev->max_vfs < num_vfs)
		return -ENOMEM;

	if (!nsim_bus_dev->vfconfigs)
		return -ENOMEM;
	nsim_bus_dev_set_vfs(nsim_bus_dev, num_vfs);

	nsim_dev = dev_get_drvdata(&nsim_bus_dev->dev);
	if (nsim_esw_mode_is_switchdev(nsim_dev)) {
		err = nsim_esw_switchdev_enable(nsim_dev, NULL);
		if (err)
			nsim_bus_dev_set_vfs(nsim_bus_dev, 0);
	}

	return err;
}

void nsim_bus_dev_vfs_disable(struct nsim_bus_dev *nsim_bus_dev)
{
	struct nsim_dev *nsim_dev;

	nsim_bus_dev_set_vfs(nsim_bus_dev, 0);
	nsim_dev = dev_get_drvdata(&nsim_bus_dev->dev);
	if (nsim_esw_mode_is_switchdev(nsim_dev))
		nsim_esw_legacy_enable(nsim_dev, NULL);
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

	mutex_lock(&nsim_bus_dev->vfs_lock);
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
	mutex_unlock(&nsim_bus_dev->vfs_lock);

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

ssize_t nsim_bus_dev_max_vfs_read(struct file *file,
				  char __user *data,
				  size_t count, loff_t *ppos)
{
	struct nsim_bus_dev *nsim_bus_dev = file->private_data;
	char buf[11];
	ssize_t len;

	len = snprintf(buf, sizeof(buf), "%u\n", nsim_bus_dev->max_vfs);
	if (len < 0)
		return len;

	return simple_read_from_buffer(data, count, ppos, buf, len);
}

ssize_t nsim_bus_dev_max_vfs_write(struct file *file,
				   const char __user *data,
				   size_t count, loff_t *ppos)
{
	struct nsim_bus_dev *nsim_bus_dev = file->private_data;
	struct nsim_vf_config *vfconfigs;
	ssize_t ret;
	char buf[10];
	u32 val;

	if (*ppos != 0)
		return 0;

	if (count >= sizeof(buf))
		return -ENOSPC;

	mutex_lock(&nsim_bus_dev->vfs_lock);
	/* Reject if VFs are configured */
	if (nsim_bus_dev->num_vfs) {
		ret = -EBUSY;
		goto unlock;
	}

	ret = copy_from_user(buf, data, count);
	if (ret) {
		ret = -EFAULT;
		goto unlock;
	}

	buf[count] = '\0';
	ret = kstrtouint(buf, 10, &val);
	if (ret) {
		ret = -EIO;
		goto unlock;
	}

	/* max_vfs limited by the maximum number of provided port indexes */
	if (val > NSIM_DEV_VF_PORT_INDEX_MAX - NSIM_DEV_VF_PORT_INDEX_BASE) {
		ret = -ERANGE;
		goto unlock;
	}

	vfconfigs = kcalloc(val, sizeof(struct nsim_vf_config), GFP_KERNEL | __GFP_NOWARN);
	if (!vfconfigs) {
		ret = -ENOMEM;
		goto unlock;
	}

	kfree(nsim_bus_dev->vfconfigs);
	nsim_bus_dev->vfconfigs = vfconfigs;
	nsim_bus_dev->max_vfs = val;
	*ppos += count;
	ret = count;
unlock:
	mutex_unlock(&nsim_bus_dev->vfs_lock);
	return ret;
}

static ssize_t
new_port_store(struct device *dev, struct device_attribute *attr,
	       const char *buf, size_t count)
{
	struct nsim_bus_dev *nsim_bus_dev = to_nsim_bus_dev(dev);
	unsigned int port_index;
	int ret;

	/* Prevent to use nsim_bus_dev before initialization. */
	if (!smp_load_acquire(&nsim_bus_dev->init))
		return -EBUSY;
	ret = kstrtouint(buf, 0, &port_index);
	if (ret)
		return ret;

	if (!mutex_trylock(&nsim_bus_dev->nsim_bus_reload_lock))
		return -EBUSY;

	if (nsim_bus_dev->in_reload) {
		mutex_unlock(&nsim_bus_dev->nsim_bus_reload_lock);
		return -EBUSY;
	}

	ret = nsim_dev_port_add(nsim_bus_dev, NSIM_DEV_PORT_TYPE_PF, port_index);
	mutex_unlock(&nsim_bus_dev->nsim_bus_reload_lock);
	return ret ? ret : count;
}

static struct device_attribute nsim_bus_dev_new_port_attr = __ATTR_WO(new_port);

static ssize_t
del_port_store(struct device *dev, struct device_attribute *attr,
	       const char *buf, size_t count)
{
	struct nsim_bus_dev *nsim_bus_dev = to_nsim_bus_dev(dev);
	unsigned int port_index;
	int ret;

	/* Prevent to use nsim_bus_dev before initialization. */
	if (!smp_load_acquire(&nsim_bus_dev->init))
		return -EBUSY;
	ret = kstrtouint(buf, 0, &port_index);
	if (ret)
		return ret;

	if (!mutex_trylock(&nsim_bus_dev->nsim_bus_reload_lock))
		return -EBUSY;

	if (nsim_bus_dev->in_reload) {
		mutex_unlock(&nsim_bus_dev->nsim_bus_reload_lock);
		return -EBUSY;
	}

	ret = nsim_dev_port_del(nsim_bus_dev, NSIM_DEV_PORT_TYPE_PF, port_index);
	mutex_unlock(&nsim_bus_dev->nsim_bus_reload_lock);
	return ret ? ret : count;
}

static struct device_attribute nsim_bus_dev_del_port_attr = __ATTR_WO(del_port);

static struct attribute *nsim_bus_dev_attrs[] = {
	&nsim_bus_dev_numvfs_attr.attr,
	&nsim_bus_dev_new_port_attr.attr,
	&nsim_bus_dev_del_port_attr.attr,
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
}

static struct device_type nsim_bus_dev_type = {
	.groups = nsim_bus_dev_attr_groups,
	.release = nsim_bus_dev_release,
};

static struct nsim_bus_dev *
nsim_bus_dev_new(unsigned int id, unsigned int port_count, unsigned int num_queues);

static ssize_t
new_device_store(struct bus_type *bus, const char *buf, size_t count)
{
	unsigned int id, port_count, num_queues;
	struct nsim_bus_dev *nsim_bus_dev;
	int err;

	err = sscanf(buf, "%u %u %u", &id, &port_count, &num_queues);
	switch (err) {
	case 1:
		port_count = 1;
		fallthrough;
	case 2:
		num_queues = 1;
		fallthrough;
	case 3:
		if (id > INT_MAX) {
			pr_err("Value of \"id\" is too big.\n");
			return -EINVAL;
		}
		break;
	default:
		pr_err("Format for adding new device is \"id port_count num_queues\" (uint uint unit).\n");
		return -EINVAL;
	}

	mutex_lock(&nsim_bus_dev_list_lock);
	/* Prevent to use resource before initialization. */
	if (!smp_load_acquire(&nsim_bus_enable)) {
		err = -EBUSY;
		goto err;
	}

	nsim_bus_dev = nsim_bus_dev_new(id, port_count, num_queues);
	if (IS_ERR(nsim_bus_dev)) {
		err = PTR_ERR(nsim_bus_dev);
		goto err;
	}

	/* Allow using nsim_bus_dev */
	smp_store_release(&nsim_bus_dev->init, true);

	list_add_tail(&nsim_bus_dev->list, &nsim_bus_dev_list);
	mutex_unlock(&nsim_bus_dev_list_lock);

	return count;
err:
	mutex_unlock(&nsim_bus_dev_list_lock);
	return err;
}
static BUS_ATTR_WO(new_device);

static void nsim_bus_dev_del(struct nsim_bus_dev *nsim_bus_dev);

static ssize_t
del_device_store(struct bus_type *bus, const char *buf, size_t count)
{
	struct nsim_bus_dev *nsim_bus_dev, *tmp;
	unsigned int id;
	int err;

	err = sscanf(buf, "%u", &id);
	switch (err) {
	case 1:
		if (id > INT_MAX) {
			pr_err("Value of \"id\" is too big.\n");
			return -EINVAL;
		}
		break;
	default:
		pr_err("Format for deleting device is \"id\" (uint).\n");
		return -EINVAL;
	}

	err = -ENOENT;
	mutex_lock(&nsim_bus_dev_list_lock);
	/* Prevent to use resource before initialization. */
	if (!smp_load_acquire(&nsim_bus_enable)) {
		mutex_unlock(&nsim_bus_dev_list_lock);
		return -EBUSY;
	}
	list_for_each_entry_safe(nsim_bus_dev, tmp, &nsim_bus_dev_list, list) {
		if (nsim_bus_dev->dev.id != id)
			continue;
		list_del(&nsim_bus_dev->list);
		nsim_bus_dev_del(nsim_bus_dev);
		err = 0;
		break;
	}
	mutex_unlock(&nsim_bus_dev_list_lock);
	return !err ? count : err;
}
static BUS_ATTR_WO(del_device);

static struct attribute *nsim_bus_attrs[] = {
	&bus_attr_new_device.attr,
	&bus_attr_del_device.attr,
	NULL
};
ATTRIBUTE_GROUPS(nsim_bus);

static int nsim_bus_probe(struct device *dev)
{
	struct nsim_bus_dev *nsim_bus_dev = to_nsim_bus_dev(dev);

	return nsim_dev_probe(nsim_bus_dev);
}

static void nsim_bus_remove(struct device *dev)
{
	struct nsim_bus_dev *nsim_bus_dev = to_nsim_bus_dev(dev);

	nsim_dev_remove(nsim_bus_dev);
}

static int nsim_num_vf(struct device *dev)
{
	struct nsim_bus_dev *nsim_bus_dev = to_nsim_bus_dev(dev);

	return nsim_bus_dev->num_vfs;
}

static struct bus_type nsim_bus = {
	.name		= DRV_NAME,
	.dev_name	= DRV_NAME,
	.bus_groups	= nsim_bus_groups,
	.probe		= nsim_bus_probe,
	.remove		= nsim_bus_remove,
	.num_vf		= nsim_num_vf,
};

#define NSIM_BUS_DEV_MAX_VFS 4

static struct nsim_bus_dev *
nsim_bus_dev_new(unsigned int id, unsigned int port_count, unsigned int num_queues)
{
	struct nsim_bus_dev *nsim_bus_dev;
	int err;

	nsim_bus_dev = kzalloc(sizeof(*nsim_bus_dev), GFP_KERNEL);
	if (!nsim_bus_dev)
		return ERR_PTR(-ENOMEM);

	err = ida_alloc_range(&nsim_bus_dev_ids, id, id, GFP_KERNEL);
	if (err < 0)
		goto err_nsim_bus_dev_free;
	nsim_bus_dev->dev.id = err;
	nsim_bus_dev->dev.bus = &nsim_bus;
	nsim_bus_dev->dev.type = &nsim_bus_dev_type;
	nsim_bus_dev->port_count = port_count;
	nsim_bus_dev->num_queues = num_queues;
	nsim_bus_dev->initial_net = current->nsproxy->net_ns;
	nsim_bus_dev->max_vfs = NSIM_BUS_DEV_MAX_VFS;
	mutex_init(&nsim_bus_dev->nsim_bus_reload_lock);
	mutex_init(&nsim_bus_dev->vfs_lock);
	/* Disallow using nsim_bus_dev */
	smp_store_release(&nsim_bus_dev->init, false);

	nsim_bus_dev->vfconfigs = kcalloc(nsim_bus_dev->max_vfs,
					  sizeof(struct nsim_vf_config),
					  GFP_KERNEL | __GFP_NOWARN);
	if (!nsim_bus_dev->vfconfigs) {
		err = -ENOMEM;
		goto err_nsim_bus_dev_id_free;
	}

	err = device_register(&nsim_bus_dev->dev);
	if (err)
		goto err_nsim_vfs_free;

	return nsim_bus_dev;

err_nsim_vfs_free:
	kfree(nsim_bus_dev->vfconfigs);
err_nsim_bus_dev_id_free:
	ida_free(&nsim_bus_dev_ids, nsim_bus_dev->dev.id);
err_nsim_bus_dev_free:
	kfree(nsim_bus_dev);
	return ERR_PTR(err);
}

static void nsim_bus_dev_del(struct nsim_bus_dev *nsim_bus_dev)
{
	/* Disallow using nsim_bus_dev */
	smp_store_release(&nsim_bus_dev->init, false);
	device_unregister(&nsim_bus_dev->dev);
	ida_free(&nsim_bus_dev_ids, nsim_bus_dev->dev.id);
	kfree(nsim_bus_dev->vfconfigs);
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
	/* Allow using resources */
	smp_store_release(&nsim_bus_enable, true);
	return 0;

err_bus_unregister:
	bus_unregister(&nsim_bus);
	return err;
}

void nsim_bus_exit(void)
{
	struct nsim_bus_dev *nsim_bus_dev, *tmp;

	/* Disallow using resources */
	smp_store_release(&nsim_bus_enable, false);

	mutex_lock(&nsim_bus_dev_list_lock);
	list_for_each_entry_safe(nsim_bus_dev, tmp, &nsim_bus_dev_list, list) {
		list_del(&nsim_bus_dev->list);
		nsim_bus_dev_del(nsim_bus_dev);
	}
	mutex_unlock(&nsim_bus_dev_list_lock);

	driver_unregister(&nsim_driver);
	bus_unregister(&nsim_bus);
}
