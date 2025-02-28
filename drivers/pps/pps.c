// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PPS core file
 *
 * Copyright (C) 2005-2009   Rodolfo Giometti <giometti@linux.it>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/idr.h>
#include <linux/mutex.h>
#include <linux/cdev.h>
#include <linux/poll.h>
#include <linux/pps_kernel.h>
#include <linux/slab.h>

#include "kc.h"

/*
 * Local variables
 */

static int pps_major;
static struct class *pps_class;

static DEFINE_MUTEX(pps_idr_lock);
static DEFINE_IDR(pps_idr);

/*
 * Char device methods
 */

static __poll_t pps_cdev_poll(struct file *file, poll_table *wait)
{
	struct pps_device *pps = file->private_data;

	poll_wait(file, &pps->queue, wait);

	return EPOLLIN | EPOLLRDNORM;
}

static int pps_cdev_fasync(int fd, struct file *file, int on)
{
	struct pps_device *pps = file->private_data;
	return fasync_helper(fd, file, on, &pps->async_queue);
}

static int pps_cdev_pps_fetch(struct pps_device *pps, struct pps_fdata *fdata)
{
	unsigned int ev = pps->last_ev;
	int err = 0;

	/* Manage the timeout */
	if (fdata->timeout.flags & PPS_TIME_INVALID)
		err = wait_event_interruptible(pps->queue,
				ev != pps->last_ev);
	else {
		unsigned long ticks;

		dev_dbg(&pps->dev, "timeout %lld.%09d\n",
				(long long) fdata->timeout.sec,
				fdata->timeout.nsec);
		ticks = fdata->timeout.sec * HZ;
		ticks += fdata->timeout.nsec / (NSEC_PER_SEC / HZ);

		if (ticks != 0) {
			err = wait_event_interruptible_timeout(
					pps->queue,
					ev != pps->last_ev,
					ticks);
			if (err == 0)
				return -ETIMEDOUT;
		}
	}

	/* Check for pending signals */
	if (err == -ERESTARTSYS) {
		dev_dbg(&pps->dev, "pending signal caught\n");
		return -EINTR;
	}

	return 0;
}

static long pps_cdev_ioctl(struct file *file,
		unsigned int cmd, unsigned long arg)
{
	struct pps_device *pps = file->private_data;
	struct pps_kparams params;
	void __user *uarg = (void __user *) arg;
	int __user *iuarg = (int __user *) arg;
	int err;

	switch (cmd) {
	case PPS_GETPARAMS:
		dev_dbg(&pps->dev, "PPS_GETPARAMS\n");

		spin_lock_irq(&pps->lock);

		/* Get the current parameters */
		params = pps->params;

		spin_unlock_irq(&pps->lock);

		err = copy_to_user(uarg, &params, sizeof(struct pps_kparams));
		if (err)
			return -EFAULT;

		break;

	case PPS_SETPARAMS:
		dev_dbg(&pps->dev, "PPS_SETPARAMS\n");

		/* Check the capabilities */
		if (!capable(CAP_SYS_TIME))
			return -EPERM;

		err = copy_from_user(&params, uarg, sizeof(struct pps_kparams));
		if (err)
			return -EFAULT;
		if (!(params.mode & (PPS_CAPTUREASSERT | PPS_CAPTURECLEAR))) {
			dev_dbg(&pps->dev, "capture mode unspecified (%x)\n",
								params.mode);
			return -EINVAL;
		}

		/* Check for supported capabilities */
		if ((params.mode & ~pps->info.mode) != 0) {
			dev_dbg(&pps->dev, "unsupported capabilities (%x)\n",
								params.mode);
			return -EINVAL;
		}

		spin_lock_irq(&pps->lock);

		/* Save the new parameters */
		pps->params = params;

		/* Restore the read only parameters */
		if ((params.mode & (PPS_TSFMT_TSPEC | PPS_TSFMT_NTPFP)) == 0) {
			/* section 3.3 of RFC 2783 interpreted */
			dev_dbg(&pps->dev, "time format unspecified (%x)\n",
								params.mode);
			pps->params.mode |= PPS_TSFMT_TSPEC;
		}
		if (pps->info.mode & PPS_CANWAIT)
			pps->params.mode |= PPS_CANWAIT;
		pps->params.api_version = PPS_API_VERS;

		/*
		 * Clear unused fields of pps_kparams to avoid leaking
		 * uninitialized data of the PPS_SETPARAMS caller via
		 * PPS_GETPARAMS
		 */
		pps->params.assert_off_tu.flags = 0;
		pps->params.clear_off_tu.flags = 0;

		spin_unlock_irq(&pps->lock);

		break;

	case PPS_GETCAP:
		dev_dbg(&pps->dev, "PPS_GETCAP\n");

		err = put_user(pps->info.mode, iuarg);
		if (err)
			return -EFAULT;

		break;

	case PPS_FETCH: {
		struct pps_fdata fdata;

		dev_dbg(&pps->dev, "PPS_FETCH\n");

		err = copy_from_user(&fdata, uarg, sizeof(struct pps_fdata));
		if (err)
			return -EFAULT;

		err = pps_cdev_pps_fetch(pps, &fdata);
		if (err)
			return err;

		/* Return the fetched timestamp */
		spin_lock_irq(&pps->lock);

		fdata.info.assert_sequence = pps->assert_sequence;
		fdata.info.clear_sequence = pps->clear_sequence;
		fdata.info.assert_tu = pps->assert_tu;
		fdata.info.clear_tu = pps->clear_tu;
		fdata.info.current_mode = pps->current_mode;

		spin_unlock_irq(&pps->lock);

		err = copy_to_user(uarg, &fdata, sizeof(struct pps_fdata));
		if (err)
			return -EFAULT;

		break;
	}
	case PPS_KC_BIND: {
		struct pps_bind_args bind_args;

		dev_dbg(&pps->dev, "PPS_KC_BIND\n");

		/* Check the capabilities */
		if (!capable(CAP_SYS_TIME))
			return -EPERM;

		if (copy_from_user(&bind_args, uarg,
					sizeof(struct pps_bind_args)))
			return -EFAULT;

		/* Check for supported capabilities */
		if ((bind_args.edge & ~pps->info.mode) != 0) {
			dev_err(&pps->dev, "unsupported capabilities (%x)\n",
					bind_args.edge);
			return -EINVAL;
		}

		/* Validate parameters roughly */
		if (bind_args.tsformat != PPS_TSFMT_TSPEC ||
				(bind_args.edge & ~PPS_CAPTUREBOTH) != 0 ||
				bind_args.consumer != PPS_KC_HARDPPS) {
			dev_err(&pps->dev, "invalid kernel consumer bind"
					" parameters (%x)\n", bind_args.edge);
			return -EINVAL;
		}

		err = pps_kc_bind(pps, &bind_args);
		if (err < 0)
			return err;

		break;
	}
	default:
		return -ENOTTY;
	}

	return 0;
}

#ifdef CONFIG_COMPAT
static long pps_cdev_compat_ioctl(struct file *file,
		unsigned int cmd, unsigned long arg)
{
	struct pps_device *pps = file->private_data;
	void __user *uarg = (void __user *) arg;

	cmd = _IOC(_IOC_DIR(cmd), _IOC_TYPE(cmd), _IOC_NR(cmd), sizeof(void *));

	if (cmd == PPS_FETCH) {
		struct pps_fdata_compat compat;
		struct pps_fdata fdata;
		int err;

		dev_dbg(&pps->dev, "PPS_FETCH\n");

		err = copy_from_user(&compat, uarg, sizeof(struct pps_fdata_compat));
		if (err)
			return -EFAULT;

		memcpy(&fdata.timeout, &compat.timeout,
					sizeof(struct pps_ktime_compat));

		err = pps_cdev_pps_fetch(pps, &fdata);
		if (err)
			return err;

		/* Return the fetched timestamp */
		spin_lock_irq(&pps->lock);

		compat.info.assert_sequence = pps->assert_sequence;
		compat.info.clear_sequence = pps->clear_sequence;
		compat.info.current_mode = pps->current_mode;

		memcpy(&compat.info.assert_tu, &pps->assert_tu,
				sizeof(struct pps_ktime_compat));
		memcpy(&compat.info.clear_tu, &pps->clear_tu,
				sizeof(struct pps_ktime_compat));

		spin_unlock_irq(&pps->lock);

		return copy_to_user(uarg, &compat,
				sizeof(struct pps_fdata_compat)) ? -EFAULT : 0;
	}

	return pps_cdev_ioctl(file, cmd, arg);
}
#else
#define pps_cdev_compat_ioctl	NULL
#endif

static struct pps_device *pps_idr_get(unsigned long id)
{
	struct pps_device *pps;

	mutex_lock(&pps_idr_lock);
	pps = idr_find(&pps_idr, id);
	if (pps)
		get_device(&pps->dev);

	mutex_unlock(&pps_idr_lock);
	return pps;
}

static int pps_cdev_open(struct inode *inode, struct file *file)
{
	struct pps_device *pps = pps_idr_get(iminor(inode));

	if (!pps)
		return -ENODEV;

	file->private_data = pps;
	return 0;
}

static int pps_cdev_release(struct inode *inode, struct file *file)
{
	struct pps_device *pps = file->private_data;

	WARN_ON(pps->id != iminor(inode));
	put_device(&pps->dev);
	return 0;
}

/*
 * Char device stuff
 */

static const struct file_operations pps_cdev_fops = {
	.owner		= THIS_MODULE,
	.poll		= pps_cdev_poll,
	.fasync		= pps_cdev_fasync,
	.compat_ioctl	= pps_cdev_compat_ioctl,
	.unlocked_ioctl	= pps_cdev_ioctl,
	.open		= pps_cdev_open,
	.release	= pps_cdev_release,
};

static void pps_device_destruct(struct device *dev)
{
	struct pps_device *pps = dev_get_drvdata(dev);

	pr_debug("deallocating pps%d\n", pps->id);
	kfree(pps);
}

int pps_register_cdev(struct pps_device *pps)
{
	int err;

	mutex_lock(&pps_idr_lock);
	/*
	 * Get new ID for the new PPS source.  After idr_alloc() calling
	 * the new source will be freely available into the kernel.
	 */
	err = idr_alloc(&pps_idr, pps, 0, PPS_MAX_SOURCES, GFP_KERNEL);
	if (err < 0) {
		if (err == -ENOSPC) {
			pr_err("%s: too many PPS sources in the system\n",
			       pps->info.name);
			err = -EBUSY;
		}
		goto out_unlock;
	}
	pps->id = err;

	pps->dev.class = pps_class;
	pps->dev.parent = pps->info.dev;
	pps->dev.devt = MKDEV(pps_major, pps->id);
	dev_set_drvdata(&pps->dev, pps);
	dev_set_name(&pps->dev, "pps%d", pps->id);
	err = device_register(&pps->dev);
	if (err)
		goto free_idr;

	/* Override the release function with our own */
	pps->dev.release = pps_device_destruct;

	pr_debug("source %s got cdev (%d:%d)\n", pps->info.name, pps_major,
		 pps->id);

	get_device(&pps->dev);
	mutex_unlock(&pps_idr_lock);
	return 0;

free_idr:
	idr_remove(&pps_idr, pps->id);
	put_device(&pps->dev);
out_unlock:
	mutex_unlock(&pps_idr_lock);
	return err;
}

void pps_unregister_cdev(struct pps_device *pps)
{
	pr_debug("unregistering pps%d\n", pps->id);
	pps->lookup_cookie = NULL;
	device_destroy(pps_class, pps->dev.devt);

	/* Now we can release the ID for re-use */
	mutex_lock(&pps_idr_lock);
	idr_remove(&pps_idr, pps->id);
	put_device(&pps->dev);
	mutex_unlock(&pps_idr_lock);
}

/*
 * Look up a pps device by magic cookie.
 * The cookie is usually a pointer to some enclosing device, but this
 * code doesn't care; you should never be dereferencing it.
 *
 * This is a bit of a kludge that is currently used only by the PPS
 * serial line discipline.  It may need to be tweaked when a second user
 * is found.
 *
 * There is no function interface for setting the lookup_cookie field.
 * It's initialized to NULL when the pps device is created, and if a
 * client wants to use it, just fill it in afterward.
 *
 * The cookie is automatically set to NULL in pps_unregister_source()
 * so that it will not be used again, even if the pps device cannot
 * be removed from the idr due to pending references holding the minor
 * number in use.
 *
 * Since pps_idr holds a reference to the device, the returned
 * pps_device is guaranteed to be valid until pps_unregister_cdev() is
 * called on it. But after calling pps_unregister_cdev(), it may be
 * freed at any time.
 */
struct pps_device *pps_lookup_dev(void const *cookie)
{
	struct pps_device *pps;
	unsigned id;

	rcu_read_lock();
	idr_for_each_entry(&pps_idr, pps, id)
		if (cookie == pps->lookup_cookie)
			break;
	rcu_read_unlock();
	return pps;
}
EXPORT_SYMBOL(pps_lookup_dev);

/*
 * Module stuff
 */

static void __exit pps_exit(void)
{
	class_destroy(pps_class);
	__unregister_chrdev(pps_major, 0, PPS_MAX_SOURCES, "pps");
}

static int __init pps_init(void)
{
	pps_class = class_create("pps");
	if (IS_ERR(pps_class)) {
		pr_err("failed to allocate class\n");
		return PTR_ERR(pps_class);
	}
	pps_class->dev_groups = pps_groups;

	pps_major = __register_chrdev(0, 0, PPS_MAX_SOURCES, "pps",
				      &pps_cdev_fops);
	if (pps_major < 0) {
		pr_err("failed to allocate char device region\n");
		goto remove_class;
	}

	pr_info("LinuxPPS API ver. %d registered\n", PPS_API_VERS);
	pr_info("Software ver. %s - Copyright 2005-2007 Rodolfo Giometti "
		"<giometti@linux.it>\n", PPS_VERSION);

	return 0;

remove_class:
	class_destroy(pps_class);
	return pps_major;
}

subsys_initcall(pps_init);
module_exit(pps_exit);

MODULE_AUTHOR("Rodolfo Giometti <giometti@linux.it>");
MODULE_DESCRIPTION("LinuxPPS support (RFC 2783) - ver. " PPS_VERSION);
MODULE_LICENSE("GPL");
