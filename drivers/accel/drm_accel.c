// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2022 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/idr.h>

#include <drm/drm_accel.h>
#include <drm/drm_debugfs.h>
#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_print.h>

static DEFINE_SPINLOCK(accel_minor_lock);
static struct idr accel_minors_idr;

static struct dentry *accel_debugfs_root;

static struct device_type accel_sysfs_device_minor = {
	.name = "accel_minor"
};

static char *accel_devnode(const struct device *dev, umode_t *mode)
{
	return kasprintf(GFP_KERNEL, "accel/%s", dev_name(dev));
}

static const struct class accel_class = {
	.name = "accel",
	.devnode = accel_devnode,
};

static int accel_sysfs_init(void)
{
	return class_register(&accel_class);
}

static void accel_sysfs_destroy(void)
{
	class_unregister(&accel_class);
}

static int accel_name_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_minor *minor = node->minor;
	struct drm_device *dev = minor->dev;
	struct drm_master *master;

	mutex_lock(&dev->master_mutex);
	master = dev->master;
	seq_printf(m, "%s", dev->driver->name);
	if (dev->dev)
		seq_printf(m, " dev=%s", dev_name(dev->dev));
	if (master && master->unique)
		seq_printf(m, " master=%s", master->unique);
	if (dev->unique)
		seq_printf(m, " unique=%s", dev->unique);
	seq_puts(m, "\n");
	mutex_unlock(&dev->master_mutex);

	return 0;
}

static const struct drm_info_list accel_debugfs_list[] = {
	{"name", accel_name_info, 0}
};
#define ACCEL_DEBUGFS_ENTRIES ARRAY_SIZE(accel_debugfs_list)

/**
 * accel_debugfs_init() - Initialize debugfs for device
 * @dev: Pointer to the device instance.
 *
 * This function creates a root directory for the device in debugfs.
 */
void accel_debugfs_init(struct drm_device *dev)
{
	drm_debugfs_dev_init(dev, accel_debugfs_root);
}

/**
 * accel_debugfs_register() - Register debugfs for device
 * @dev: Pointer to the device instance.
 *
 * Creates common files for accelerators.
 */
void accel_debugfs_register(struct drm_device *dev)
{
	struct drm_minor *minor = dev->accel;

	minor->debugfs_root = dev->debugfs_root;

	drm_debugfs_create_files(accel_debugfs_list, ACCEL_DEBUGFS_ENTRIES,
				 dev->debugfs_root, minor);
}

/**
 * accel_set_device_instance_params() - Set some device parameters for accel device
 * @kdev: Pointer to the device instance.
 * @index: The minor's index
 *
 * This function creates the dev_t of the device using the accel major and
 * the device's minor number. In addition, it sets the class and type of the
 * device instance to the accel sysfs class and device type, respectively.
 */
void accel_set_device_instance_params(struct device *kdev, int index)
{
	kdev->devt = MKDEV(ACCEL_MAJOR, index);
	kdev->class = &accel_class;
	kdev->type = &accel_sysfs_device_minor;
}

/**
 * accel_minor_alloc() - Allocates a new accel minor
 *
 * This function access the accel minors idr and allocates from it
 * a new id to represent a new accel minor
 *
 * Return: A new id on success or error code in case idr_alloc failed
 */
int accel_minor_alloc(void)
{
	unsigned long flags;
	int r;

	spin_lock_irqsave(&accel_minor_lock, flags);
	r = idr_alloc(&accel_minors_idr, NULL, 0, ACCEL_MAX_MINORS, GFP_NOWAIT);
	spin_unlock_irqrestore(&accel_minor_lock, flags);

	return r;
}

/**
 * accel_minor_remove() - Remove an accel minor
 * @index: The minor id to remove.
 *
 * This function access the accel minors idr and removes from
 * it the member with the id that is passed to this function.
 */
void accel_minor_remove(int index)
{
	unsigned long flags;

	spin_lock_irqsave(&accel_minor_lock, flags);
	idr_remove(&accel_minors_idr, index);
	spin_unlock_irqrestore(&accel_minor_lock, flags);
}

/**
 * accel_minor_replace() - Replace minor pointer in accel minors idr.
 * @minor: Pointer to the new minor.
 * @index: The minor id to replace.
 *
 * This function access the accel minors idr structure and replaces the pointer
 * that is associated with an existing id. Because the minor pointer can be
 * NULL, we need to explicitly pass the index.
 *
 * Return: 0 for success, negative value for error
 */
void accel_minor_replace(struct drm_minor *minor, int index)
{
	unsigned long flags;

	spin_lock_irqsave(&accel_minor_lock, flags);
	idr_replace(&accel_minors_idr, minor, index);
	spin_unlock_irqrestore(&accel_minor_lock, flags);
}

/*
 * Looks up the given minor-ID and returns the respective DRM-minor object. The
 * refence-count of the underlying device is increased so you must release this
 * object with accel_minor_release().
 *
 * The object can be only a drm_minor that represents an accel device.
 *
 * As long as you hold this minor, it is guaranteed that the object and the
 * minor->dev pointer will stay valid! However, the device may get unplugged and
 * unregistered while you hold the minor.
 */
static struct drm_minor *accel_minor_acquire(unsigned int minor_id)
{
	struct drm_minor *minor;
	unsigned long flags;

	spin_lock_irqsave(&accel_minor_lock, flags);
	minor = idr_find(&accel_minors_idr, minor_id);
	if (minor)
		drm_dev_get(minor->dev);
	spin_unlock_irqrestore(&accel_minor_lock, flags);

	if (!minor) {
		return ERR_PTR(-ENODEV);
	} else if (drm_dev_is_unplugged(minor->dev)) {
		drm_dev_put(minor->dev);
		return ERR_PTR(-ENODEV);
	}

	return minor;
}

static void accel_minor_release(struct drm_minor *minor)
{
	drm_dev_put(minor->dev);
}

/**
 * accel_open - open method for ACCEL file
 * @inode: device inode
 * @filp: file pointer.
 *
 * This function must be used by drivers as their &file_operations.open method.
 * It looks up the correct ACCEL device and instantiates all the per-file
 * resources for it. It also calls the &drm_driver.open driver callback.
 *
 * Return: 0 on success or negative errno value on failure.
 */
int accel_open(struct inode *inode, struct file *filp)
{
	struct drm_device *dev;
	struct drm_minor *minor;
	int retcode;

	minor = accel_minor_acquire(iminor(inode));
	if (IS_ERR(minor))
		return PTR_ERR(minor);

	dev = minor->dev;

	atomic_fetch_inc(&dev->open_count);

	/* share address_space across all char-devs of a single device */
	filp->f_mapping = dev->anon_inode->i_mapping;

	retcode = drm_open_helper(filp, minor);
	if (retcode)
		goto err_undo;

	return 0;

err_undo:
	atomic_dec(&dev->open_count);
	accel_minor_release(minor);
	return retcode;
}
EXPORT_SYMBOL_GPL(accel_open);

static int accel_stub_open(struct inode *inode, struct file *filp)
{
	const struct file_operations *new_fops;
	struct drm_minor *minor;
	int err;

	minor = accel_minor_acquire(iminor(inode));
	if (IS_ERR(minor))
		return PTR_ERR(minor);

	new_fops = fops_get(minor->dev->driver->fops);
	if (!new_fops) {
		err = -ENODEV;
		goto out;
	}

	replace_fops(filp, new_fops);
	if (filp->f_op->open)
		err = filp->f_op->open(inode, filp);
	else
		err = 0;

out:
	accel_minor_release(minor);

	return err;
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
	idr_destroy(&accel_minors_idr);
}

int __init accel_core_init(void)
{
	int ret;

	idr_init(&accel_minors_idr);

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
