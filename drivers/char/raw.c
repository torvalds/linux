// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/drivers/char/raw.c
 *
 * Front-end raw character devices.  These can be bound to any block
 * devices to provide genuine Unix raw character device semantics.
 *
 * We reserve minor number 0 for a control interface.  ioctl()s on this
 * device are used to bind the other minor numbers to block devices.
 */

#include <linux/init.h>
#include <linux/fs.h>
#include <linux/major.h>
#include <linux/blkdev.h>
#include <linux/backing-dev.h>
#include <linux/module.h>
#include <linux/raw.h>
#include <linux/capability.h>
#include <linux/uio.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/gfp.h>
#include <linux/compat.h>
#include <linux/vmalloc.h>

#include <linux/uaccess.h>

struct raw_device_data {
	dev_t binding;
	struct block_device *bdev;
	int inuse;
};

static struct class *raw_class;
static struct raw_device_data *raw_devices;
static DEFINE_MUTEX(raw_mutex);
static const struct file_operations raw_ctl_fops; /* forward declaration */

static int max_raw_minors = CONFIG_MAX_RAW_DEVS;

module_param(max_raw_minors, int, 0);
MODULE_PARM_DESC(max_raw_minors, "Maximum number of raw devices (1-65536)");

/*
 * Open/close code for raw IO.
 *
 * We just rewrite the i_mapping for the /dev/raw/rawN file descriptor to
 * point at the blockdev's address_space and set the file handle to use
 * O_DIRECT.
 *
 * Set the device's soft blocksize to the minimum possible.  This gives the
 * finest possible alignment and has no adverse impact on performance.
 */
static int raw_open(struct inode *inode, struct file *filp)
{
	const int minor = iminor(inode);
	struct block_device *bdev;
	int err;

	if (minor == 0) {	/* It is the control device */
		filp->f_op = &raw_ctl_fops;
		return 0;
	}

	pr_warn_ratelimited(
		"process %s (pid %d) is using the deprecated raw device\n"
		"support will be removed in Linux 5.14.\n",
		current->comm, current->pid);

	mutex_lock(&raw_mutex);

	/*
	 * All we need to do on open is check that the device is bound.
	 */
	err = -ENODEV;
	if (!raw_devices[minor].binding)
		goto out;
	bdev = blkdev_get_by_dev(raw_devices[minor].binding,
				 filp->f_mode | FMODE_EXCL, raw_open);
	if (IS_ERR(bdev)) {
		err = PTR_ERR(bdev);
		goto out;
	}
	err = set_blocksize(bdev, bdev_logical_block_size(bdev));
	if (err)
		goto out1;
	filp->f_flags |= O_DIRECT;
	filp->f_mapping = bdev->bd_inode->i_mapping;
	if (++raw_devices[minor].inuse == 1)
		file_inode(filp)->i_mapping =
			bdev->bd_inode->i_mapping;
	filp->private_data = bdev;
	raw_devices[minor].bdev = bdev;
	mutex_unlock(&raw_mutex);
	return 0;

out1:
	blkdev_put(bdev, filp->f_mode | FMODE_EXCL);
out:
	mutex_unlock(&raw_mutex);
	return err;
}

/*
 * When the final fd which refers to this character-special node is closed, we
 * make its ->mapping point back at its own i_data.
 */
static int raw_release(struct inode *inode, struct file *filp)
{
	const int minor= iminor(inode);
	struct block_device *bdev;

	mutex_lock(&raw_mutex);
	bdev = raw_devices[minor].bdev;
	if (--raw_devices[minor].inuse == 0)
		/* Here  inode->i_mapping == bdev->bd_inode->i_mapping  */
		inode->i_mapping = &inode->i_data;
	mutex_unlock(&raw_mutex);

	blkdev_put(bdev, filp->f_mode | FMODE_EXCL);
	return 0;
}

/*
 * Forward ioctls to the underlying block device.
 */
static long
raw_ioctl(struct file *filp, unsigned int command, unsigned long arg)
{
	struct block_device *bdev = filp->private_data;
	return blkdev_ioctl(bdev, 0, command, arg);
}

static int bind_set(int number, u64 major, u64 minor)
{
	dev_t dev = MKDEV(major, minor);
	dev_t raw = MKDEV(RAW_MAJOR, number);
	struct raw_device_data *rawdev;
	int err = 0;

	if (number <= 0 || number >= max_raw_minors)
		return -EINVAL;

	if (MAJOR(dev) != major || MINOR(dev) != minor)
		return -EINVAL;

	rawdev = &raw_devices[number];

	/*
	 * This is like making block devices, so demand the
	 * same capability
	 */
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	/*
	 * For now, we don't need to check that the underlying
	 * block device is present or not: we can do that when
	 * the raw device is opened.  Just check that the
	 * major/minor numbers make sense.
	 */

	if (MAJOR(dev) == 0 && dev != 0)
		return -EINVAL;

	mutex_lock(&raw_mutex);
	if (rawdev->inuse) {
		mutex_unlock(&raw_mutex);
		return -EBUSY;
	}
	if (rawdev->binding)
		module_put(THIS_MODULE);

	rawdev->binding = dev;
	if (!dev) {
		/* unbind */
		device_destroy(raw_class, raw);
	} else {
		__module_get(THIS_MODULE);
		device_destroy(raw_class, raw);
		device_create(raw_class, NULL, raw, NULL, "raw%d", number);
	}
	mutex_unlock(&raw_mutex);
	return err;
}

static int bind_get(int number, dev_t *dev)
{
	if (number <= 0 || number >= max_raw_minors)
		return -EINVAL;
	*dev = raw_devices[number].binding;
	return 0;
}

/*
 * Deal with ioctls against the raw-device control interface, to bind
 * and unbind other raw devices.
 */
static long raw_ctl_ioctl(struct file *filp, unsigned int command,
			  unsigned long arg)
{
	struct raw_config_request rq;
	dev_t dev;
	int err;

	switch (command) {
	case RAW_SETBIND:
		if (copy_from_user(&rq, (void __user *) arg, sizeof(rq)))
			return -EFAULT;

		return bind_set(rq.raw_minor, rq.block_major, rq.block_minor);

	case RAW_GETBIND:
		if (copy_from_user(&rq, (void __user *) arg, sizeof(rq)))
			return -EFAULT;

		err = bind_get(rq.raw_minor, &dev);
		if (err)
			return err;

		rq.block_major = MAJOR(dev);
		rq.block_minor = MINOR(dev);

		if (copy_to_user((void __user *)arg, &rq, sizeof(rq)))
			return -EFAULT;

		return 0;
	}

	return -EINVAL;
}

#ifdef CONFIG_COMPAT
struct raw32_config_request {
	compat_int_t	raw_minor;
	compat_u64	block_major;
	compat_u64	block_minor;
};

static long raw_ctl_compat_ioctl(struct file *file, unsigned int cmd,
				unsigned long arg)
{
	struct raw32_config_request __user *user_req = compat_ptr(arg);
	struct raw32_config_request rq;
	dev_t dev;
	int err = 0;

	switch (cmd) {
	case RAW_SETBIND:
		if (copy_from_user(&rq, user_req, sizeof(rq)))
			return -EFAULT;

		return bind_set(rq.raw_minor, rq.block_major, rq.block_minor);

	case RAW_GETBIND:
		if (copy_from_user(&rq, user_req, sizeof(rq)))
			return -EFAULT;

		err = bind_get(rq.raw_minor, &dev);
		if (err)
			return err;

		rq.block_major = MAJOR(dev);
		rq.block_minor = MINOR(dev);

		if (copy_to_user(user_req, &rq, sizeof(rq)))
			return -EFAULT;

		return 0;
	}

	return -EINVAL;
}
#endif

static const struct file_operations raw_fops = {
	.read_iter	= blkdev_read_iter,
	.write_iter	= blkdev_write_iter,
	.fsync		= blkdev_fsync,
	.open		= raw_open,
	.release	= raw_release,
	.unlocked_ioctl = raw_ioctl,
	.llseek		= default_llseek,
	.owner		= THIS_MODULE,
};

static const struct file_operations raw_ctl_fops = {
	.unlocked_ioctl = raw_ctl_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= raw_ctl_compat_ioctl,
#endif
	.open		= raw_open,
	.owner		= THIS_MODULE,
	.llseek		= noop_llseek,
};

static struct cdev raw_cdev;

static char *raw_devnode(struct device *dev, umode_t *mode)
{
	return kasprintf(GFP_KERNEL, "raw/%s", dev_name(dev));
}

static int __init raw_init(void)
{
	dev_t dev = MKDEV(RAW_MAJOR, 0);
	int ret;

	if (max_raw_minors < 1 || max_raw_minors > 65536) {
		pr_warn("raw: invalid max_raw_minors (must be between 1 and 65536), using %d\n",
			CONFIG_MAX_RAW_DEVS);
		max_raw_minors = CONFIG_MAX_RAW_DEVS;
	}

	raw_devices = vzalloc(array_size(max_raw_minors,
					 sizeof(struct raw_device_data)));
	if (!raw_devices) {
		printk(KERN_ERR "Not enough memory for raw device structures\n");
		ret = -ENOMEM;
		goto error;
	}

	ret = register_chrdev_region(dev, max_raw_minors, "raw");
	if (ret)
		goto error;

	cdev_init(&raw_cdev, &raw_fops);
	ret = cdev_add(&raw_cdev, dev, max_raw_minors);
	if (ret)
		goto error_region;
	raw_class = class_create(THIS_MODULE, "raw");
	if (IS_ERR(raw_class)) {
		printk(KERN_ERR "Error creating raw class.\n");
		cdev_del(&raw_cdev);
		ret = PTR_ERR(raw_class);
		goto error_region;
	}
	raw_class->devnode = raw_devnode;
	device_create(raw_class, NULL, MKDEV(RAW_MAJOR, 0), NULL, "rawctl");

	return 0;

error_region:
	unregister_chrdev_region(dev, max_raw_minors);
error:
	vfree(raw_devices);
	return ret;
}

static void __exit raw_exit(void)
{
	device_destroy(raw_class, MKDEV(RAW_MAJOR, 0));
	class_destroy(raw_class);
	cdev_del(&raw_cdev);
	unregister_chrdev_region(MKDEV(RAW_MAJOR, 0), max_raw_minors);
}

module_init(raw_init);
module_exit(raw_exit);
MODULE_LICENSE("GPL");
