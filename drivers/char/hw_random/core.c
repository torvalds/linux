/*
        Added support for the AMD Geode LX RNG
	(c) Copyright 2004-2005 Advanced Micro Devices, Inc.

	derived from

 	Hardware driver for the Intel/AMD/VIA Random Number Generators (RNG)
	(c) Copyright 2003 Red Hat Inc <jgarzik@redhat.com>

 	derived from

        Hardware driver for the AMD 768 Random Number Generator (RNG)
        (c) Copyright 2001 Red Hat Inc <alan@redhat.com>

 	derived from

	Hardware driver for Intel i810 Random Number Generator (RNG)
	Copyright 2000,2001 Jeff Garzik <jgarzik@pobox.com>
	Copyright 2000,2001 Philipp Rumpf <prumpf@mandrakesoft.com>

	Added generic RNG API
	Copyright 2006 Michael Buesch <mbuesch@freenet.de>
	Copyright 2005 (c) MontaVista Software, Inc.

	Please read Documentation/hw_random.txt for details on use.

	----------------------------------------------------------
	This software may be used and distributed according to the terms
        of the GNU General Public License, incorporated herein by reference.

 */


#include <linux/device.h>
#include <linux/hw_random.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/delay.h>
#include <asm/uaccess.h>


#define RNG_MODULE_NAME		"hw_random"
#define PFX			RNG_MODULE_NAME ": "
#define RNG_MISCDEV_MINOR	183 /* official */


static struct hwrng *current_rng;
static LIST_HEAD(rng_list);
static DEFINE_MUTEX(rng_mutex);


static inline int hwrng_init(struct hwrng *rng)
{
	if (!rng->init)
		return 0;
	return rng->init(rng);
}

static inline void hwrng_cleanup(struct hwrng *rng)
{
	if (rng && rng->cleanup)
		rng->cleanup(rng);
}

static inline int hwrng_data_present(struct hwrng *rng, int wait)
{
	if (!rng->data_present)
		return 1;
	return rng->data_present(rng, wait);
}

static inline int hwrng_data_read(struct hwrng *rng, u32 *data)
{
	return rng->data_read(rng, data);
}


static int rng_dev_open(struct inode *inode, struct file *filp)
{
	/* enforce read-only access to this chrdev */
	if ((filp->f_mode & FMODE_READ) == 0)
		return -EINVAL;
	if (filp->f_mode & FMODE_WRITE)
		return -EINVAL;
	return 0;
}

static ssize_t rng_dev_read(struct file *filp, char __user *buf,
			    size_t size, loff_t *offp)
{
	u32 data;
	ssize_t ret = 0;
	int err = 0;
	int bytes_read;

	while (size) {
		err = -ERESTARTSYS;
		if (mutex_lock_interruptible(&rng_mutex))
			goto out;
		if (!current_rng) {
			mutex_unlock(&rng_mutex);
			err = -ENODEV;
			goto out;
		}

		bytes_read = 0;
		if (hwrng_data_present(current_rng,
				       !(filp->f_flags & O_NONBLOCK)))
			bytes_read = hwrng_data_read(current_rng, &data);
		mutex_unlock(&rng_mutex);

		err = -EAGAIN;
		if (!bytes_read && (filp->f_flags & O_NONBLOCK))
			goto out;

		err = -EFAULT;
		while (bytes_read && size) {
			if (put_user((u8)data, buf++))
				goto out;
			size--;
			ret++;
			bytes_read--;
			data >>= 8;
		}

		if (need_resched())
			schedule_timeout_interruptible(1);
		err = -ERESTARTSYS;
		if (signal_pending(current))
			goto out;
	}
out:
	return ret ? : err;
}


static const struct file_operations rng_chrdev_ops = {
	.owner		= THIS_MODULE,
	.open		= rng_dev_open,
	.read		= rng_dev_read,
};

static struct miscdevice rng_miscdev = {
	.minor		= RNG_MISCDEV_MINOR,
	.name		= RNG_MODULE_NAME,
	.fops		= &rng_chrdev_ops,
};


static ssize_t hwrng_attr_current_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t len)
{
	int err;
	struct hwrng *rng;

	err = mutex_lock_interruptible(&rng_mutex);
	if (err)
		return -ERESTARTSYS;
	err = -ENODEV;
	list_for_each_entry(rng, &rng_list, list) {
		if (strcmp(rng->name, buf) == 0) {
			if (rng == current_rng) {
				err = 0;
				break;
			}
			err = hwrng_init(rng);
			if (err)
				break;
			hwrng_cleanup(current_rng);
			current_rng = rng;
			err = 0;
			break;
		}
	}
	mutex_unlock(&rng_mutex);

	return err ? : len;
}

static ssize_t hwrng_attr_current_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	int err;
	ssize_t ret;
	const char *name = "none";

	err = mutex_lock_interruptible(&rng_mutex);
	if (err)
		return -ERESTARTSYS;
	if (current_rng)
		name = current_rng->name;
	ret = snprintf(buf, PAGE_SIZE, "%s\n", name);
	mutex_unlock(&rng_mutex);

	return ret;
}

static ssize_t hwrng_attr_available_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	int err;
	ssize_t ret = 0;
	struct hwrng *rng;

	err = mutex_lock_interruptible(&rng_mutex);
	if (err)
		return -ERESTARTSYS;
	buf[0] = '\0';
	list_for_each_entry(rng, &rng_list, list) {
		strncat(buf, rng->name, PAGE_SIZE - ret - 1);
		ret += strlen(rng->name);
		strncat(buf, " ", PAGE_SIZE - ret - 1);
		ret++;
	}
	strncat(buf, "\n", PAGE_SIZE - ret - 1);
	ret++;
	mutex_unlock(&rng_mutex);

	return ret;
}

static DEVICE_ATTR(rng_current, S_IRUGO | S_IWUSR,
		   hwrng_attr_current_show,
		   hwrng_attr_current_store);
static DEVICE_ATTR(rng_available, S_IRUGO,
		   hwrng_attr_available_show,
		   NULL);


static void unregister_miscdev(void)
{
	device_remove_file(rng_miscdev.this_device, &dev_attr_rng_available);
	device_remove_file(rng_miscdev.this_device, &dev_attr_rng_current);
	misc_deregister(&rng_miscdev);
}

static int register_miscdev(void)
{
	int err;

	err = misc_register(&rng_miscdev);
	if (err)
		goto out;
	err = device_create_file(rng_miscdev.this_device,
				 &dev_attr_rng_current);
	if (err)
		goto err_misc_dereg;
	err = device_create_file(rng_miscdev.this_device,
				 &dev_attr_rng_available);
	if (err)
		goto err_remove_current;
out:
	return err;

err_remove_current:
	device_remove_file(rng_miscdev.this_device, &dev_attr_rng_current);
err_misc_dereg:
	misc_deregister(&rng_miscdev);
	goto out;
}

int hwrng_register(struct hwrng *rng)
{
	int must_register_misc;
	int err = -EINVAL;
	struct hwrng *old_rng, *tmp;

	if (rng->name == NULL ||
	    rng->data_read == NULL)
		goto out;

	mutex_lock(&rng_mutex);

	/* Must not register two RNGs with the same name. */
	err = -EEXIST;
	list_for_each_entry(tmp, &rng_list, list) {
		if (strcmp(tmp->name, rng->name) == 0)
			goto out_unlock;
	}

	must_register_misc = (current_rng == NULL);
	old_rng = current_rng;
	if (!old_rng) {
		err = hwrng_init(rng);
		if (err)
			goto out_unlock;
		current_rng = rng;
	}
	err = 0;
	if (must_register_misc) {
		err = register_miscdev();
		if (err) {
			if (!old_rng) {
				hwrng_cleanup(rng);
				current_rng = NULL;
			}
			goto out_unlock;
		}
	}
	INIT_LIST_HEAD(&rng->list);
	list_add_tail(&rng->list, &rng_list);
out_unlock:
	mutex_unlock(&rng_mutex);
out:
	return err;
}
EXPORT_SYMBOL_GPL(hwrng_register);

void hwrng_unregister(struct hwrng *rng)
{
	int err;

	mutex_lock(&rng_mutex);

	list_del(&rng->list);
	if (current_rng == rng) {
		hwrng_cleanup(rng);
		if (list_empty(&rng_list)) {
			current_rng = NULL;
		} else {
			current_rng = list_entry(rng_list.prev, struct hwrng, list);
			err = hwrng_init(current_rng);
			if (err)
				current_rng = NULL;
		}
	}
	if (list_empty(&rng_list))
		unregister_miscdev();

	mutex_unlock(&rng_mutex);
}
EXPORT_SYMBOL_GPL(hwrng_unregister);


MODULE_DESCRIPTION("H/W Random Number Generator (RNG) driver");
MODULE_LICENSE("GPL");
