// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#define pr_fmt(fmt) "qti_virtio_mem: %s: " fmt, __func__

#include <linux/module.h>
#include <linux/file.h>
#include <linux/anon_inodes.h>
#include <linux/kref.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/oom.h>
#include <linux/mm.h>
#include <linux/mmzone.h>
#include <linux/vmstat.h>
#include <uapi/linux/qti_virtio_mem.h>
#include "qti_virtio_mem.h"

struct qti_virtio_mem_hint {
	struct list_head list;
	struct list_head kernel_plugged_list;
	struct kref kref;
	struct file *filp;
	s64 size;
	char name[QTI_VIRTIO_MEM_IOC_MAX_NAME_LEN];
};

#define QTI_VIRTIO_MEM_MAX_DEVS 1
static dev_t qvm_dev_no;
static struct class *qvm_class;
static struct cdev qvm_char_dev;

/* Protects qvm_hint_total and qvm_list */
static DEFINE_MUTEX(qvm_lock);
static LIST_HEAD(qvm_list);

static LIST_HEAD(qvm_kernel_plugged);
static DEFINE_MUTEX(qvm_kernel_plugged_lock);

/* Sum of all hints */
static s64 qvm_hint_total;

static uint64_t device_block_size, max_plugin_threshold;
static uint16_t kernel_plugged;

#define QVM_OOM_NOTIFY_PRIORITY	90

static int qti_virtio_mem_hint_update(struct qti_virtio_mem_hint *hint,
					s64 new_size, bool sync)
{
	int ret;
	s64 total = 0;

	mutex_lock(&qvm_lock);
	total = qvm_hint_total + new_size - hint->size;
	ret = virtio_mem_update_config_size(total, sync);
	if (ret) {
		pr_debug("Hint %s: Invalid request %llx would result in %llx\n",
			hint->name, new_size, total);
		mutex_unlock(&qvm_lock);
		return ret;
	}

	hint->size = new_size;
	qvm_hint_total = total;
	pr_debug("Hint %s: Updated size %llx, new_requested_size %llx\n",
		hint->name, hint->size, qvm_hint_total);
	mutex_unlock(&qvm_lock);
	return ret;
}

static void qti_virtio_mem_hint_kref_release(struct kref *kref)
{
	struct qti_virtio_mem_hint *hint;

	hint = container_of(kref, struct qti_virtio_mem_hint, kref);
	WARN_ON(qti_virtio_mem_hint_update(hint, 0, false));

	mutex_lock(&qvm_lock);
	list_del(&hint->list);
	mutex_unlock(&qvm_lock);
	kfree(hint);
}

static void *qti_virtio_mem_hint_create(char *name, s64 size)
{
	struct qti_virtio_mem_hint *hint;

	hint = kzalloc(sizeof(*hint), GFP_KERNEL);
	if (!hint)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&hint->list);
	kref_init(&hint->kref);
	hint->size = 0;
	if (!name || !strlen(name))
		name = "(none)";
	strscpy(hint->name, name, ARRAY_SIZE(hint->name));

	if (qti_virtio_mem_hint_update(hint, size, true)) {
		kfree(hint);
		return ERR_PTR(-EINVAL);
	}

	mutex_lock(&qvm_lock);
	list_add(&hint->list, &qvm_list);
	mutex_unlock(&qvm_lock);
	return hint;
}

static void qti_virtio_mem_hint_release(void *handle)
{
	struct qti_virtio_mem_hint *hint = handle;

	kref_put(&hint->kref, qti_virtio_mem_hint_kref_release);
}

static int qti_virtio_mem_hint_file_release(struct inode *inode, struct file *filp)
{
	qti_virtio_mem_hint_release(filp->private_data);
	return 0;
}

static const struct file_operations qti_virtio_mem_hint_fops = {
	.release = qti_virtio_mem_hint_file_release,
};

static int qti_virtio_mem_hint_create_fd(char *name, u64 size)
{
	struct qti_virtio_mem_hint *hint;
	int fd;

	hint = qti_virtio_mem_hint_create(name, size);
	if (IS_ERR(hint))
		return PTR_ERR(hint);

	hint->filp = anon_inode_getfile("virtio_mem_hint",
						&qti_virtio_mem_hint_fops,
						hint, O_RDWR);
	if (IS_ERR(hint->filp)) {
		int ret = PTR_ERR(hint->filp);

		qti_virtio_mem_hint_release(hint);
		return ret;
	}

	fd = get_unused_fd_flags(O_CLOEXEC);
	if (fd < 0) {
		fput(hint->filp);
		return fd;
	}

	fd_install(fd, hint->filp);
	return fd;
}

union qti_virtio_mem_ioc_arg {
	struct qti_virtio_mem_ioc_hint_create_arg hint_create;
};

static int qti_virtio_mem_ioc_hint_create(struct qti_virtio_mem_ioc_hint_create_arg *arg)
{
	int fd;

	/* Validate arguments */
	if (arg->size <= 0 || arg->reserved0 || arg->reserved1)
		return -EINVAL;

	/* ensure name is null-terminated */
	arg->name[QTI_VIRTIO_MEM_IOC_MAX_NAME_LEN - 1] = '\0';

	fd = qti_virtio_mem_hint_create_fd(arg->name, arg->size);
	if (fd < 0)
		return fd;

	arg->fd = fd;
	return 0;
}

static long qti_virtio_mem_ioctl(struct file *filp, unsigned int cmd,
			      unsigned long arg)
{
	int ret;
	unsigned int dir = _IOC_DIR(cmd);
	union qti_virtio_mem_ioc_arg ioctl_arg;

	if (_IOC_SIZE(cmd) > sizeof(ioctl_arg))
		return -EINVAL;

	if (copy_from_user(&ioctl_arg, (void __user *)arg, _IOC_SIZE(cmd)))
		return -EFAULT;

	if (!(dir & _IOC_WRITE))
		memset(&ioctl_arg, 0, sizeof(ioctl_arg));

	switch (cmd) {
	case QTI_VIRTIO_MEM_IOC_HINT_CREATE:
	{
		ret = qti_virtio_mem_ioc_hint_create(&ioctl_arg.hint_create);
		if (ret)
			return ret;
		break;
	}
	default:
		return -ENOTTY;
	}

	if (dir & _IOC_READ) {
		if (copy_to_user((void __user *)arg, &ioctl_arg,
				 _IOC_SIZE(cmd)))
			return -EFAULT;
	}

	return 0;
}

static const struct file_operations qti_virtio_mem_dev_fops = {
	.unlocked_ioctl = qti_virtio_mem_ioctl,
	.compat_ioctl = compat_ptr_ioctl,
};

static ssize_t device_block_size_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	if (!device_block_size &&
		!virtio_mem_get_device_block_size(&device_block_size))
		dev_err(dev, "failed to get virtio-mem device block size\n");

	return scnprintf(buf, PAGE_SIZE, "%d\n", device_block_size);
}

static ssize_t max_plugin_threshold_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	if (!max_plugin_threshold &&
		!virtio_mem_get_max_plugin_threshold(&max_plugin_threshold))
		dev_err(dev, "failed to get max plugin threshold\n");

	return scnprintf(buf, PAGE_SIZE, "%d\n", max_plugin_threshold);
}

static ssize_t device_block_plugged_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	uint16_t device_block_plugged;

	device_block_plugged =
			ALIGN(qvm_hint_total, device_block_size) / device_block_size;
	return scnprintf(buf, PAGE_SIZE, "%d\n", device_block_plugged);
}

static ssize_t kernel_plugged_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", kernel_plugged);
}

static ssize_t kernel_unplug_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t size)
{
	int val = 0, ret;
	uint16_t plugged_out;
	struct qti_virtio_mem_hint *hint;

	ret = kstrtoint(buf, 0, &val);
	if (ret < 0)
		return ret;

	mutex_lock(&qvm_kernel_plugged_lock);
	val = min_t(uint16_t, val, kernel_plugged);

	for (plugged_out = 0; plugged_out < val && !list_empty(&qvm_kernel_plugged);
			plugged_out++) {
		hint = list_first_entry(&qvm_kernel_plugged, struct qti_virtio_mem_hint,
				kernel_plugged_list);
		list_del(&hint->kernel_plugged_list);
		qti_virtio_mem_hint_release(hint);
		kernel_plugged--;
	}

	mutex_unlock(&qvm_kernel_plugged_lock);
	return size;
}

static DEVICE_ATTR_RO(device_block_size);
static DEVICE_ATTR_RO(max_plugin_threshold);
static DEVICE_ATTR_RO(device_block_plugged);
static DEVICE_ATTR_RO(kernel_plugged);
static DEVICE_ATTR_WO(kernel_unplug);

static struct attribute *dev_attrs[] = {
	&dev_attr_device_block_size.attr,
	&dev_attr_max_plugin_threshold.attr,
	&dev_attr_device_block_plugged.attr,
	&dev_attr_kernel_plugged.attr,
	&dev_attr_kernel_unplug.attr,
	NULL,
};

static struct attribute_group dev_group = {
	.attrs = dev_attrs,
};

static inline unsigned long get_zone_free_pages(enum zone_type zone_class)
{
	return zone_page_state(&NODE_DATA(numa_node_id())->node_zones[zone_class],
		NR_FREE_PAGES);
}

static int qvm_oom_notify(struct notifier_block *self,
		unsigned long dummy, void *parm)
{
	unsigned long *freed = parm;
	struct qti_virtio_mem_hint *hint;
	unsigned long free_pages;
	struct zone *z;

	z = &NODE_DATA(numa_node_id())->node_zones[ZONE_MOVABLE];
	free_pages = get_zone_free_pages(ZONE_MOVABLE);

	/* add a block only if movable zone is exhausted */
	if ((free_pages > high_wmark_pages(z) + device_block_size / PAGE_SIZE) ||
	    (qvm_hint_total >= max_plugin_threshold))
		return NOTIFY_OK;

	pr_info("comm: %s totalram_pages: %lu Normal free_pages: %lu Movable free_pages: %lu\n",
			current->comm, totalram_pages(), get_zone_free_pages(ZONE_NORMAL),
			get_zone_free_pages(ZONE_MOVABLE));

	hint = qti_virtio_mem_hint_create("qvm_oom_notifier", device_block_size);
	if (IS_ERR(hint)) {
		pr_err("failed to add memory\n");
		return NOTIFY_OK;
	}

	mutex_lock(&qvm_kernel_plugged_lock);
	list_add(&hint->kernel_plugged_list, &qvm_kernel_plugged);
	*freed += device_block_size / PAGE_SIZE;
	kernel_plugged++;
	mutex_unlock(&qvm_kernel_plugged_lock);

	return NOTIFY_OK;
}

static struct notifier_block qvm_oom_nb = {
		.notifier_call = qvm_oom_notify,
		.priority = QVM_OOM_NOTIFY_PRIORITY,
};

static int __init qti_virtio_mem_init(void)
{
	int ret;
	struct device *dev;

	ret = alloc_chrdev_region(&qvm_dev_no, 0, QTI_VIRTIO_MEM_MAX_DEVS,
				  "qti_virtio_mem");
	if (ret < 0)
		goto err_chrdev_region;

	qvm_class = class_create(THIS_MODULE, "qti_virtio_mem");
	if (IS_ERR(qvm_class)) {
		ret = PTR_ERR(qvm_class);
		goto err_class_create;
	}

	cdev_init(&qvm_char_dev, &qti_virtio_mem_dev_fops);
	ret = cdev_add(&qvm_char_dev, qvm_dev_no, 1);
	if (ret < 0)
		goto err_cdev_add;

	dev = device_create(qvm_class, NULL, qvm_dev_no, NULL,
				  "qti_virtio_mem");
	if (IS_ERR(dev)) {
		ret = PTR_ERR(dev);
		goto err_dev_create;
	}

	if (virtio_mem_get_device_block_size(&device_block_size))
		dev_err(dev, "failed to get virtio-mem device block size\n");

	if (virtio_mem_get_max_plugin_threshold(&max_plugin_threshold))
		dev_err(dev, "failed to get max plugin threshold\n");

	ret = sysfs_create_group(&dev->kobj, &dev_group);
	if (ret < 0) {
		dev_err(dev, "failed to create sysfs group\n");
		goto err_dev_create;
	}

	ret = register_oom_notifier(&qvm_oom_nb);
	if (ret < 0) {
		dev_err(dev, "Failed to register to oom notifier\n");
		goto err_dev_create;
	}

	return 0;
err_dev_create:
	cdev_del(&qvm_char_dev);
err_cdev_add:
	class_destroy(qvm_class);
err_class_create:
	unregister_chrdev_region(qvm_dev_no, QTI_VIRTIO_MEM_MAX_DEVS);
err_chrdev_region:
	return ret;
}
module_init(qti_virtio_mem_init);

static void __exit qti_virtio_mem_exit(void)
{
	WARN(!list_empty(&qvm_list), "Unloading module with nonzero hint objects\n");

	unregister_oom_notifier(&qvm_oom_nb);
	device_destroy(qvm_class, qvm_dev_no);
	cdev_del(&qvm_char_dev);
	class_destroy(qvm_class);
	unregister_chrdev_region(qvm_dev_no, QTI_VIRTIO_MEM_MAX_DEVS);
}
module_exit(qti_virtio_mem_exit);
