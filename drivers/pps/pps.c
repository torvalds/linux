/*
 * PPS core file
 *
 *
 * Copyright (C) 2005-2009   Rodolfo Giometti <giometti@linux.it>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
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

static dev_t pps_devt;
static struct class *pps_class;

static DEFINE_MUTEX(pps_idr_lock);
static DEFINE_IDR(pps_idr);

/*
 * Char device methods
 */

static unsigned int pps_cdev_poll(struct file *file, poll_table *wait)
{
	struct pps_device *pps = file->private_data;

	poll_wait(file, &pps->queue, wait);

	return POLLIN | POLLRDNORM;
}

static int pps_cdev_fasync(int fd, struct file *file, int on)
{
	struct pps_device *pps = file->private_data;
	return fasync_helper(fd, file, on, &pps->async_queue);
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
		dev_dbg(pps->dev, "PPS_GETPARAMS\n");

		spin_lock_irq(&pps->lock);

		/* Get the current parameters */
		params = pps->params;

		spin_unlock_irq(&pps->lock);

		err = copy_to_user(uarg, &params, sizeof(struct pps_kparams));
		if (err)
			return -EFAULT;

		break;

	case PPS_SETPARAMS:
		dev_dbg(pps->dev, "PPS_SETPARAMS\n");

		/* Check the capabilities */
		if (!capable(CAP_SYS_TIME))
			return -EPERM;

		err = copy_from_user(&params, uarg, sizeof(struct pps_kparams));
		if (err)
			return -EFAULT;
		if (!(params.mode & (PPS_CAPTUREASSERT | PPS_CAPTURECLEAR))) {
			dev_dbg(pps->dev, "capture mode unspecified (%x)\n",
								params.mode);
			return -EINVAL;
		}

		/* Check for supported capabilities */
		if ((params.mode & ~pps->info.mode) != 0) {
			dev_dbg(pps->dev, "unsupported capabilities (%x)\n",
								params.mode);
			return -EINVAL;
		}

		spin_lock_irq(&pps->lock);

		/* Save the new parameters */
		pps->params = params;

		/* Restore the read only parameters */
		if ((params.mode & (PPS_TSFMT_TSPEC | PPS_TSFMT_NTPFP)) == 0) {
			/* section 3.3 of RFC 2783 interpreted */
			dev_dbg(pps->dev, "time format unspecified (%x)\n",
								params.mode);
			pps->params.mode |= PPS_TSFMT_TSPEC;
		}
		if (pps->info.mode & PPS_CANWAIT)
			pps->params.mode |= PPS_CANWAIT;
		pps->params.api_version = PPS_API_VERS;

		spin_unlock_irq(&pps->lock);

		break;

	case PPS_GETCAP:
		dev_dbg(pps->dev, "PPS_GETCAP\n");

		err = put_user(pps->info.mode, iuarg);
		if (err)
			return -EFAULT;

		break;

	case PPS_FETCH: {
		struct pps_fdata fdata;
		unsigned int ev;

		dev_dbg(pps->dev, "PPS_FETCH\n");

		err = copy_from_user(&fdata, uarg, sizeof(struct pps_fdata));
		if (err)
			return -EFAULT;

		ev = pps->last_ev;

		/* Manage the timeout */
		if (fdata.timeout.flags & PPS_TIME_INVALID)
			err = wait_event_interruptible(pps->queue,
					ev != pps->last_ev);
		else {
			unsigned long ticks;

			dev_dbg(pps->dev, "timeout %lld.%09d\n",
					(long long) fdata.timeout.sec,
					fdata.timeout.nsec);
			ticks = fdata.timeout.sec * HZ;
			ticks += fdata.timeout.nsec / (NSEC_PER_SEC / HZ);

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
			dev_dbg(pps->dev, "pending signal caught\n");
			return -EINTR;
		}

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

		dev_dbg(pps->dev, "PPS_KC_BIND\n");

		/* Check the capabilities */
		if (!capable(CAP_SYS_TIME))
			return -EPERM;

		if (copy_from_user(&bind_args, uarg,
					sizeof(struct pps_bind_args)))
			return -EFAULT;

		/* Check for supported capabilities */
		if ((bind_args.edge & ~pps->info.mode) != 0) {
			dev_err(pps->dev, "unsupported capabilities (%x)\n",
					bind_args.edge);
			return -EINVAL;
		}

		/* Validate parameters roughly */
		if (bind_args.tsformat != PPS_TSFMT_TSPEC ||
				(bind_args.edge & ~PPS_CAPTUREBOTH) != 0 ||
				bind_args.consumer != PPS_KC_HARDPPS) {
			dev_err(pps->dev, "invalid kernel consumer bind"
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

static int pps_cdev_open(struct inode *inode, struct file *file)
{
	struct pps_device *pps = container_of(inode->i_cdev,
						struct pps_device, cdev);
	file->private_data = pps;
	kobject_get(&pps->dev->kobj);
	return 0;
}

static int pps_cdev_release(struct inode *inode, struct file *file)
{
	struct pps_device *pps = container_of(inode->i_cdev,
						struct pps_device, cdev);
	kobject_put(&pps->dev->kobj);
	return 0;
}

/*
 * Char device stuff
 */

static const struct file_operations pps_cdev_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.poll		= pps_cdev_poll,
	.fasync		= pps_cdev_fasync,
	.unlocked_ioctl	= pps_cdev_ioctl,
	.open		= pps_cdev_open,
	.release	= pps_cdev_release,
};

static void pps_device_destruct(struct device *dev)
{
	struct pps_device *pps = dev_get_drvdata(dev);

	cdev_del(&pps->cdev);

	/* Now we can release the ID for re-use */
	pr_debug("deallocating pps%d\n", pps->id);
	mutex_lock(&pps_idr_lock);
	idr_remove(&pps_idr, pps->id);
	mutex_unlock(&pps_idr_lock);

	kfree(dev);
	kfree(pps);
}

int pps_register_cdev(struct pps_device *pps)
{
	int err;
	dev_t devt;

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
	mutex_unlock(&pps_idr_lock);

	devt = MKDEV(MAJOR(pps_devt), pps->id);

	cdev_init(&pps->cdev, &pps_cdev_fops);
	pps->cdev.owner = pps->info.owner;

	err = cdev_add(&pps->cdev, devt, 1);
	if (err) {
		pr_err("%s: failed to add char device %d:%d\n",
				pps->info.name, MAJOR(pps_devt), pps->id);
		goto free_idr;
	}
	pps->dev = device_create(pps_class, pps->info.dev, devt, pps,
							"pps%d", pps->id);
	if (IS_ERR(pps->dev)) {
		err = PTR_ERR(pps->dev);
		goto del_cdev;
	}

	/* Override the release function with our own */
	pps->dev->release = pps_device_destruct;

	pr_debug("source %s got cdev (%d:%d)\n", pps->info.name,
			MAJOR(pps_devt), pps->id);

	return 0;

del_cdev:
	cdev_del(&pps->cdev);

free_idr:
	mutex_lock(&pps_idr_lock);
	idr_remove(&pps_idr, pps->id);
out_unlock:
	mutex_unlock(&pps_idr_lock);
	return err;
}

void pps_unregister_cdev(struct pps_device *pps)
{
	pr_debug("unregistering pps%d\n", pps->id);
	pps->lookup_cookie = NULL;
	device_destroy(pps_class, pps->dev->devt);
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
	unregister_chrdev_region(pps_devt, PPS_MAX_SOURCES);
}

static int __init pps_init(void)
{
	int err;

	pps_class = class_create(THIS_MODULE, "pps");
	if (IS_ERR(pps_class)) {
		pr_err("failed to allocate class\n");
		return PTR_ERR(pps_class);
	}
	pps_class->dev_attrs = pps_attrs;

	err = alloc_chrdev_region(&pps_devt, 0, PPS_MAX_SOURCES, "pps");
	if (err < 0) {
		pr_err("failed to allocate char device region\n");
		goto remove_class;
	}

	pr_info("LinuxPPS API ver. %d registered\n", PPS_API_VERS);
	pr_info("Software ver. %s - Copyright 2005-2007 Rodolfo Giometti "
		"<giometti@linux.it>\n", PPS_VERSION);

	return 0;

remove_class:
	class_destroy(pps_class);

	return err;
}

subsys_initcall(pps_init);
module_exit(pps_exit);

MODULE_AUTHOR("Rodolfo Giometti <giometti@linux.it>");
MODULE_DESCRIPTION("LinuxPPS support (RFC 2783) - ver. " PPS_VERSION);
MODULE_LICENSE("GPL");
