// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2022 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#include <linux/debugfs.h>
#include <linux/device.h>

#include <drm/drm_accel.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_print.h>

static struct dentry *accel_debugfs_root;
static struct class *accel_class;

static char *accel_devnode(struct device *dev, umode_t *mode)
{
	return kasprintf(GFP_KERNEL, "accel/%s", dev_name(dev));
}

static int accel_sysfs_init(void)
{
	accel_class = class_create(THIS_MODULE, "accel");
	if (IS_ERR(accel_class))
		return PTR_ERR(accel_class);

	accel_class->devnode = accel_devnode;

	return 0;
}

static void accel_sysfs_destroy(void)
{
	if (IS_ERR_OR_NULL(accel_class))
		return;
	class_destroy(accel_class);
	accel_class = NULL;
}

static int accel_stub_open(struct inode *inode, struct file *filp)
{
	return -EOPNOTSUPP;
}

static const struct file_operations accel_stub_fops = {
	.owner = THIS_MODULE,
	.open = accel_stub_open,
	.llseek = noop_llseek,
};

void accel_core_exit(void)
{
	unregister_chrdev(ACCEL_MAJOR, "accel");
	debugfs_remove(accel_debugfs_root);
	accel_sysfs_destroy();
}

int __init accel_core_init(void)
{
	int ret;

	ret = accel_sysfs_init();
	if (ret < 0) {
		DRM_ERROR("Cannot create ACCEL class: %d\n", ret);
		goto error;
	}

	accel_debugfs_root = debugfs_create_dir("accel", NULL);

	ret = register_chrdev(ACCEL_MAJOR, "accel", &accel_stub_fops);
	if (ret < 0)
		DRM_ERROR("Cannot register ACCEL major: %d\n", ret);

error:
	/*
	 * Any cleanup due to errors will be done in drm_core_exit() that
	 * will call accel_core_exit()
	 */
	return ret;
}
