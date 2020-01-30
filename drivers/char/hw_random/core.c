/*
 * hw_random/core.c: HWRNG core API
 *
 * Copyright 2006 Michael Buesch <m@bues.ch>
 * Copyright 2005 (c) MontaVista Software, Inc.
 *
 * Please read Documentation/admin-guide/hw_random.rst for details on use.
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/hw_random.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/sched/signal.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#define RNG_MODULE_NAME		"hw_random"

static struct hwrng *current_rng;
/* the current rng has been explicitly chosen by user via sysfs */
static int cur_rng_set_by_user;
static struct task_struct *hwrng_fill;
/* list of registered rngs, sorted decending by quality */
static LIST_HEAD(rng_list);
/* Protects rng_list and current_rng */
static DEFINE_MUTEX(rng_mutex);
/* Protects rng read functions, data_avail, rng_buffer and rng_fillbuf */
static DEFINE_MUTEX(reading_mutex);
static int data_avail;
static u8 *rng_buffer, *rng_fillbuf;
static unsigned short current_quality;
static unsigned short default_quality; /* = 0; default to "off" */

module_param(current_quality, ushort, 0644);
MODULE_PARM_DESC(current_quality,
		 "current hwrng entropy estimation per 1024 bits of input");
module_param(default_quality, ushort, 0644);
MODULE_PARM_DESC(default_quality,
		 "default entropy content of hwrng per 1024 bits of input");

static void drop_current_rng(void);
static int hwrng_init(struct hwrng *rng);
static void start_khwrngd(void);

static inline int rng_get_data(struct hwrng *rng, u8 *buffer, size_t size,
			       int wait);

static size_t rng_buffer_size(void)
{
	return SMP_CACHE_BYTES < 32 ? 32 : SMP_CACHE_BYTES;
}

static void add_early_randomness(struct hwrng *rng)
{
	int bytes_read;
	size_t size = min_t(size_t, 16, rng_buffer_size());

	mutex_lock(&reading_mutex);
	bytes_read = rng_get_data(rng, rng_buffer, size, 0);
	mutex_unlock(&reading_mutex);
	if (bytes_read > 0)
		add_device_randomness(rng_buffer, bytes_read);
}

static inline void cleanup_rng(struct kref *kref)
{
	struct hwrng *rng = container_of(kref, struct hwrng, ref);

	if (rng->cleanup)
		rng->cleanup(rng);

	complete(&rng->cleanup_done);
}

static int set_current_rng(struct hwrng *rng)
{
	int err;

	BUG_ON(!mutex_is_locked(&rng_mutex));

	err = hwrng_init(rng);
	if (err)
		return err;

	drop_current_rng();
	current_rng = rng;

	return 0;
}

static void drop_current_rng(void)
{
	BUG_ON(!mutex_is_locked(&rng_mutex));
	if (!current_rng)
		return;

	/* decrease last reference for triggering the cleanup */
	kref_put(&current_rng->ref, cleanup_rng);
	current_rng = NULL;
}

/* Returns ERR_PTR(), NULL or refcounted hwrng */
static struct hwrng *get_current_rng(void)
{
	struct hwrng *rng;

	if (mutex_lock_interruptible(&rng_mutex))
		return ERR_PTR(-ERESTARTSYS);

	rng = current_rng;
	if (rng)
		kref_get(&rng->ref);

	mutex_unlock(&rng_mutex);
	return rng;
}

static void put_rng(struct hwrng *rng)
{
	/*
	 * Hold rng_mutex here so we serialize in case they set_current_rng
	 * on rng again immediately.
	 */
	mutex_lock(&rng_mutex);
	if (rng)
		kref_put(&rng->ref, cleanup_rng);
	mutex_unlock(&rng_mutex);
}

static int hwrng_init(struct hwrng *rng)
{
	if (kref_get_unless_zero(&rng->ref))
		goto skip_init;

	if (rng->init) {
		int ret;

		ret =  rng->init(rng);
		if (ret)
			return ret;
	}

	kref_init(&rng->ref);
	reinit_completion(&rng->cleanup_done);

skip_init:
	add_early_randomness(rng);

	current_quality = rng->quality ? : default_quality;
	if (current_quality > 1024)
		current_quality = 1024;

	if (current_quality == 0 && hwrng_fill)
		kthread_stop(hwrng_fill);
	if (current_quality > 0 && !hwrng_fill)
		start_khwrngd();

	return 0;
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

	BUG_ON(!mutex_is_locked(&reading_mutex));
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
	struct hwrng *rng;

	while (size) {
		rng = get_current_rng();
		if (IS_ERR(rng)) {
			err = PTR_ERR(rng);
			goto out;
		}
		if (!rng) {
			err = -ENODEV;
			goto out;
		}

		if (mutex_lock_interruptible(&reading_mutex)) {
			err = -ERESTARTSYS;
			goto out_put;
		}
		if (!data_avail) {
			bytes_read = rng_get_data(rng, rng_buffer,
				rng_buffer_size(),
				!(filp->f_flags & O_NONBLOCK));
			if (bytes_read < 0) {
				err = bytes_read;
				goto out_unlock_reading;
			}
			data_avail = bytes_read;
		}

		if (!data_avail) {
			if (filp->f_flags & O_NONBLOCK) {
				err = -EAGAIN;
				goto out_unlock_reading;
			}
		} else {
			len = data_avail;
			if (len > size)
				len = size;

			data_avail -= len;

			if (copy_to_user(buf + ret, rng_buffer + data_avail,
								len)) {
				err = -EFAULT;
				goto out_unlock_reading;
			}

			size -= len;
			ret += len;
		}

		mutex_unlock(&reading_mutex);
		put_rng(rng);

		if (need_resched())
			schedule_timeout_interruptible(1);

		if (signal_pending(current)) {
			err = -ERESTARTSYS;
			goto out;
		}
	}
out:
	return ret ? : err;

out_unlock_reading:
	mutex_unlock(&reading_mutex);
out_put:
	put_rng(rng);
	goto out;
}

static const struct file_operations rng_chrdev_ops = {
	.owner		= THIS_MODULE,
	.open		= rng_dev_open,
	.read		= rng_dev_read,
	.llseek		= noop_llseek,
};

static const struct attribute_group *rng_dev_groups[];

static struct miscdevice rng_miscdev = {
	.minor		= HWRNG_MINOR,
	.name		= RNG_MODULE_NAME,
	.nodename	= "hwrng",
	.fops		= &rng_chrdev_ops,
	.groups		= rng_dev_groups,
};

static int enable_best_rng(void)
{
	int ret = -ENODEV;

	BUG_ON(!mutex_is_locked(&rng_mutex));

	/* rng_list is sorted by quality, use the best (=first) one */
	if (!list_empty(&rng_list)) {
		struct hwrng *new_rng;

		new_rng = list_entry(rng_list.next, struct hwrng, list);
		ret = ((new_rng == current_rng) ? 0 : set_current_rng(new_rng));
		if (!ret)
			cur_rng_set_by_user = 0;
	} else {
		drop_current_rng();
		cur_rng_set_by_user = 0;
		ret = 0;
	}

	return ret;
}

static ssize_t hwrng_attr_current_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t len)
{
	int err = -ENODEV;
	struct hwrng *rng;

	err = mutex_lock_interruptible(&rng_mutex);
	if (err)
		return -ERESTARTSYS;

	if (sysfs_streq(buf, "")) {
		err = enable_best_rng();
	} else {
		list_for_each_entry(rng, &rng_list, list) {
			if (sysfs_streq(rng->name, buf)) {
				cur_rng_set_by_user = 1;
				err = set_current_rng(rng);
				break;
			}
		}
	}

	mutex_unlock(&rng_mutex);

	return err ? : len;
}

static ssize_t hwrng_attr_current_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	ssize_t ret;
	struct hwrng *rng;

	rng = get_current_rng();
	if (IS_ERR(rng))
		return PTR_ERR(rng);

	ret = snprintf(buf, PAGE_SIZE, "%s\n", rng ? rng->name : "none");
	put_rng(rng);

	return ret;
}

static ssize_t hwrng_attr_available_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	int err;
	struct hwrng *rng;

	err = mutex_lock_interruptible(&rng_mutex);
	if (err)
		return -ERESTARTSYS;
	buf[0] = '\0';
	list_for_each_entry(rng, &rng_list, list) {
		strlcat(buf, rng->name, PAGE_SIZE);
		strlcat(buf, " ", PAGE_SIZE);
	}
	strlcat(buf, "\n", PAGE_SIZE);
	mutex_unlock(&rng_mutex);

	return strlen(buf);
}

static ssize_t hwrng_attr_selected_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", cur_rng_set_by_user);
}

static DEVICE_ATTR(rng_current, S_IRUGO | S_IWUSR,
		   hwrng_attr_current_show,
		   hwrng_attr_current_store);
static DEVICE_ATTR(rng_available, S_IRUGO,
		   hwrng_attr_available_show,
		   NULL);
static DEVICE_ATTR(rng_selected, S_IRUGO,
		   hwrng_attr_selected_show,
		   NULL);

static struct attribute *rng_dev_attrs[] = {
	&dev_attr_rng_current.attr,
	&dev_attr_rng_available.attr,
	&dev_attr_rng_selected.attr,
	NULL
};

ATTRIBUTE_GROUPS(rng_dev);

static void __exit unregister_miscdev(void)
{
	misc_deregister(&rng_miscdev);
}

static int __init register_miscdev(void)
{
	return misc_register(&rng_miscdev);
}

static int hwrng_fillfn(void *unused)
{
	long rc;

	while (!kthread_should_stop()) {
		struct hwrng *rng;

		rng = get_current_rng();
		if (IS_ERR(rng) || !rng)
			break;
		mutex_lock(&reading_mutex);
		rc = rng_get_data(rng, rng_fillbuf,
				  rng_buffer_size(), 1);
		mutex_unlock(&reading_mutex);
		put_rng(rng);
		if (rc <= 0) {
			pr_warn("hwrng: no data available\n");
			msleep_interruptible(10000);
			continue;
		}
		/* Outside lock, sure, but y'know: randomness. */
		add_hwgenerator_randomness((void *)rng_fillbuf, rc,
					   rc * current_quality * 8 >> 10);
	}
	hwrng_fill = NULL;
	return 0;
}

static void start_khwrngd(void)
{
	hwrng_fill = kthread_run(hwrng_fillfn, NULL, "hwrng");
	if (IS_ERR(hwrng_fill)) {
		pr_err("hwrng_fill thread creation failed\n");
		hwrng_fill = NULL;
	}
}

int hwrng_register(struct hwrng *rng)
{
	int err = -EINVAL;
	struct hwrng *old_rng, *tmp;
	struct list_head *rng_list_ptr;

	if (!rng->name || (!rng->data_read && !rng->read))
		goto out;

	mutex_lock(&rng_mutex);
	/* Must not register two RNGs with the same name. */
	err = -EEXIST;
	list_for_each_entry(tmp, &rng_list, list) {
		if (strcmp(tmp->name, rng->name) == 0)
			goto out_unlock;
	}

	init_completion(&rng->cleanup_done);
	complete(&rng->cleanup_done);

	/* rng_list is sorted by decreasing quality */
	list_for_each(rng_list_ptr, &rng_list) {
		tmp = list_entry(rng_list_ptr, struct hwrng, list);
		if (tmp->quality < rng->quality)
			break;
	}
	list_add_tail(&rng->list, rng_list_ptr);

	old_rng = current_rng;
	err = 0;
	if (!old_rng ||
	    (!cur_rng_set_by_user && rng->quality > old_rng->quality)) {
		/*
		 * Set new rng as current as the new rng source
		 * provides better entropy quality and was not
		 * chosen by userspace.
		 */
		err = set_current_rng(rng);
		if (err)
			goto out_unlock;
	}

	if (old_rng && !rng->init) {
		/*
		 * Use a new device's input to add some randomness to
		 * the system.  If this rng device isn't going to be
		 * used right away, its init function hasn't been
		 * called yet; so only use the randomness from devices
		 * that don't need an init callback.
		 */
		add_early_randomness(rng);
	}

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
		err = enable_best_rng();
		if (err) {
			drop_current_rng();
			cur_rng_set_by_user = 0;
		}
	}

	if (list_empty(&rng_list)) {
		mutex_unlock(&rng_mutex);
		if (hwrng_fill)
			kthread_stop(hwrng_fill);
	} else
		mutex_unlock(&rng_mutex);

	wait_for_completion(&rng->cleanup_done);
}
EXPORT_SYMBOL_GPL(hwrng_unregister);

static void devm_hwrng_release(struct device *dev, void *res)
{
	hwrng_unregister(*(struct hwrng **)res);
}

static int devm_hwrng_match(struct device *dev, void *res, void *data)
{
	struct hwrng **r = res;

	if (WARN_ON(!r || !*r))
		return 0;

	return *r == data;
}

int devm_hwrng_register(struct device *dev, struct hwrng *rng)
{
	struct hwrng **ptr;
	int error;

	ptr = devres_alloc(devm_hwrng_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return -ENOMEM;

	error = hwrng_register(rng);
	if (error) {
		devres_free(ptr);
		return error;
	}

	*ptr = rng;
	devres_add(dev, ptr);
	return 0;
}
EXPORT_SYMBOL_GPL(devm_hwrng_register);

void devm_hwrng_unregister(struct device *dev, struct hwrng *rng)
{
	devres_release(dev, devm_hwrng_release, devm_hwrng_match, rng);
}
EXPORT_SYMBOL_GPL(devm_hwrng_unregister);

static int __init hwrng_modinit(void)
{
	int ret = -ENOMEM;

	/* kmalloc makes this safe for virt_to_page() in virtio_rng.c */
	rng_buffer = kmalloc(rng_buffer_size(), GFP_KERNEL);
	if (!rng_buffer)
		return -ENOMEM;

	rng_fillbuf = kmalloc(rng_buffer_size(), GFP_KERNEL);
	if (!rng_fillbuf) {
		kfree(rng_buffer);
		return -ENOMEM;
	}

	ret = register_miscdev();
	if (ret) {
		kfree(rng_fillbuf);
		kfree(rng_buffer);
	}

	return ret;
}

static void __exit hwrng_modexit(void)
{
	mutex_lock(&rng_mutex);
	BUG_ON(current_rng);
	kfree(rng_buffer);
	kfree(rng_fillbuf);
	mutex_unlock(&rng_mutex);

	unregister_miscdev();
}

module_init(hwrng_modinit);
module_exit(hwrng_modexit);

MODULE_DESCRIPTION("H/W Random Number Generator (RNG) driver");
MODULE_LICENSE("GPL");
