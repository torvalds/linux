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
	Copyright 2006 Michael Buesch <m@bues.ch>
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
#include <linux/miscdevice.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <asm/uaccess.h>


#define RNG_MODULE_NAME		"hw_random"
#define PFX			RNG_MODULE_NAME ": "
#define RNG_MISCDEV_MINOR	183 /* official */


static struct hwrng *current_rng;
static LIST_HEAD(rng_list);
static DEFINE_MUTEX(rng_mutex);
static int data_avail;
static u8 *rng_buffer;

static size_t rng_buffer_size(void)
{
	return SMP_CACHE_BYTES < 32 ? 32 : SMP_CACHE_BYTES;
}

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

static int rng_dev_open(struct inode *inode, struct file *filp)
{
	/* enforce read-only access to this chrdev */
	if ((filp->f_mode & FMODE_READ) == 0)
		return -EINVAL;
	if (filp->f_mode & FMODE_WRITE)
		return -EINVAL;
	return 0;
}

static inline int rng_get_data(struct hwrng *rng, u8 *buffer, size_t size,
			int wait) {
	int present;

	if (rng->read)
		return rng->read(rng, (void *)buffer, size, wait);

	if (rng->data_present)
		present = rng->data_present(rng, wait);
	else
		present = 1;

	if (present)
		return rng->data_read(rng, (u32 *)buffer);

	return 0;
}

static ssize_t rng_dev_read(struct file *filp, char __user *buf,
			    size_t size, loff_t *offp)
{
	ssize_t ret = 0;
	int err = 0;
	int bytes_read, len;

	while (size) {
		if (mutex_lock_interruptible(&rng_mutex)) {
			err = -ERESTARTSYS;
			goto out;
		}

		if (!current_rng) {
			err = -ENODEV;
			goto out_unlock;
		}

		if (!data_avail) {
			bytes_read = rng_get_data(current_rng, rng_buffer,
				rng_buffer_size(),
				!(filp->f_flags & O_NONBLOCK));
			if (bytes_read < 0) {
				err = bytes_read;
				goto out_unlock;
			}
			data_avail = bytes_read;
		}

		if (!data_avail) {
			if (filp->f_flags & O_NONBLOCK) {
				err = -EAGAIN;
				goto out_unlock;
			}
		} else {
			len = data_avail;
			if (len > size)
				len = size;

			data_avail -= len;

			if (copy_to_user(buf + ret, rng_buffer + data_avail,
								len)) {
				err = -EFAULT;
				goto out_unlock;
			}

			size -= len;
			ret += len;
		}

		mutex_unlock(&rng_mutex);

		if (need_resched())
			schedule_timeout_interruptible(1);

		if (signal_pending(current)) {
			err = -ERESTARTSYS;
			goto out;
		}
	}
out:
	return ret ? : err;
out_unlock:
	mutex_unlock(&rng_mutex);
	goto out;
}


static const struct file_operations rng_chrdev_ops = {
	.owner		= THIS_MODULE,
	.open		= rng_dev_open,
	.read		= rng_dev_read,
	.llseek		= noop_llseek,
};

static struct miscdevice rng_miscdev = {
	.minor		= RNG_MISCDEV_MINOR,
	.name		= RNG_MODULE_NAME,
	.nodename	= "hwrng",
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
	int err = -EINVAL;
	struct hwrng *old_rng, *tmp;
	unsigned char bytes[16];
	int bytes_read;

	if (rng->name == NULL ||
	    (rng->data_read == NULL && rng->read == NULL))
		goto out;

	mutex_lock(&rng_mutex);

	/* kmalloc makes this safe for virt_to_page() in virtio_rng.c */
	err = -ENOMEM;
	if (!rng_buffer) {
		rng_buffer = kmalloc(rng_buffer_size(), GFP_KERNEL);
		if (!rng_buffer)
			goto out_unlock;
	}

	/* Must not register two RNGs with the same name. */
	err = -EEXIST;
	list_for_each_entry(tmp, &rng_list, list) {
		if (strcmp(tmp->name, rng->name) == 0)
			goto out_unlock;
	}

	old_rng = current_rng;
	if (!old_rng) {
		err = hwrng_init(rng);
		if (err)
			goto out_unlock;
		current_rng = rng;
	}
	err = 0;
	if (!old_rng) {
		err = register_miscdev();
		if (err) {
			hwrng_cleanup(rng);
			current_rng = NULL;
			goto out_unlock;
		}
	}
	INIT_LIST_HEAD(&rng->list);
	list_add_tail(&rng->list, &rng_list);

	bytes_read = rng_get_data(rng, bytes, sizeof(bytes), 1);
	if (bytes_read > 0)
		add_device_randomness(bytes, bytes_read);
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

static void __exit hwrng_exit(void)
{
	mutex_lock(&rng_mutex);
	BUG_ON(current_rng);
	kfree(rng_buffer);
	mutex_unlock(&rng_mutex);
}

module_exit(hwrng_exit);

MODULE_DESCRIPTION("H/W Random Number Generator (RNG) driver");
MODULE_LICENSE("GPL");
