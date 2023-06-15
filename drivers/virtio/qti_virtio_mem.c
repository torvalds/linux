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
#include <uapi/linux/qti_virtio_mem.h>
#include "qti_virtio_mem.h"

struct qti_virtio_mem_hint {
	struct list_head list;
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
/* Sum of all hints */
static s64 qvm_hint_total;

void qvm_update_plugged_size(uint64_t size)
{
	qvm_hint_total = (s64)size;
}

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

	device_destroy(qvm_class, qvm_dev_no);
	cdev_del(&qvm_char_dev);
	class_destroy(qvm_class);
	unregister_chrdev_region(qvm_dev_no, QTI_VIRTIO_MEM_MAX_DEVS);
}
module_exit(qti_virtio_mem_exit);
