// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PPS generators core file
 *
 * Copyright (C) 2024 Rodolfo Giometti <giometti@enneenne.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/time.h>
#include <linux/timex.h>
#include <linux/uaccess.h>
#include <linux/idr.h>
#include <linux/cdev.h>
#include <linux/poll.h>
#include <linux/fs.h>
#include <linux/pps_gen_kernel.h>
#include <linux/slab.h>

/*
 * Local variables
 */

static dev_t pps_gen_devt;
static struct class *pps_gen_class;

static DEFINE_IDA(pps_gen_ida);

/*
 * Char device methods
 */

static __poll_t pps_gen_cdev_poll(struct file *file, poll_table *wait)
{
	struct pps_gen_device *pps_gen = file->private_data;

	poll_wait(file, &pps_gen->queue, wait);
	return EPOLLIN | EPOLLRDNORM;
}

static int pps_gen_cdev_fasync(int fd, struct file *file, int on)
{
	struct pps_gen_device *pps_gen = file->private_data;

	return fasync_helper(fd, file, on, &pps_gen->async_queue);
}

static long pps_gen_cdev_ioctl(struct file *file,
		unsigned int cmd, unsigned long arg)
{
	struct pps_gen_device *pps_gen = file->private_data;
	void __user *uarg = (void __user *) arg;
	unsigned int __user *uiuarg = (unsigned int __user *) arg;
	unsigned int status;
	int ret;

	switch (cmd) {
	case PPS_GEN_SETENABLE:
		dev_dbg(pps_gen->dev, "PPS_GEN_SETENABLE\n");

		ret = get_user(status, uiuarg);
		if (ret)
			return -EFAULT;

		ret = pps_gen->info.enable(pps_gen, status);
		if (ret)
			return ret;
		pps_gen->enabled = status;

		break;

	case PPS_GEN_USESYSTEMCLOCK:
		dev_dbg(pps_gen->dev, "PPS_GEN_USESYSTEMCLOCK\n");

		ret = put_user(pps_gen->info.use_system_clock, uiuarg);
		if (ret)
			return -EFAULT;

		break;

	case PPS_GEN_FETCHEVENT: {
		struct pps_gen_event info;
		unsigned int ev = pps_gen->last_ev;

		dev_dbg(pps_gen->dev, "PPS_GEN_FETCHEVENT\n");

		ret = wait_event_interruptible(pps_gen->queue,
				ev != pps_gen->last_ev);
		if (ret == -ERESTARTSYS) {
			dev_dbg(pps_gen->dev, "pending signal caught\n");
			return -EINTR;
		}

		spin_lock_irq(&pps_gen->lock);
		info.sequence = pps_gen->sequence;
		info.event = pps_gen->event;
		spin_unlock_irq(&pps_gen->lock);

		ret = copy_to_user(uarg, &info, sizeof(struct pps_gen_event));
		if (ret)
			return -EFAULT;

		break;
	}
	default:
		return -ENOTTY;
	}

	return 0;
}

static int pps_gen_cdev_open(struct inode *inode, struct file *file)
{
	struct pps_gen_device *pps_gen = container_of(inode->i_cdev,
				struct pps_gen_device, cdev);

	get_device(pps_gen->dev);
	file->private_data = pps_gen;
	return 0;
}

static int pps_gen_cdev_release(struct inode *inode, struct file *file)
{
	struct pps_gen_device *pps_gen = file->private_data;

	put_device(pps_gen->dev);
	return 0;
}

/*
 * Char device stuff
 */

static const struct file_operations pps_gen_cdev_fops = {
	.owner		= THIS_MODULE,
	.poll	   = pps_gen_cdev_poll,
	.fasync	 = pps_gen_cdev_fasync,
	.unlocked_ioctl	= pps_gen_cdev_ioctl,
	.open		= pps_gen_cdev_open,
	.release	= pps_gen_cdev_release,
};

static void pps_gen_device_destruct(struct device *dev)
{
	struct pps_gen_device *pps_gen = dev_get_drvdata(dev);

	cdev_del(&pps_gen->cdev);

	pr_debug("deallocating pps-gen%d\n", pps_gen->id);
	ida_free(&pps_gen_ida, pps_gen->id);

	kfree(dev);
	kfree(pps_gen);
}

static int pps_gen_register_cdev(struct pps_gen_device *pps_gen)
{
	int err;
	dev_t devt;

	err = ida_alloc_max(&pps_gen_ida, PPS_GEN_MAX_SOURCES - 1, GFP_KERNEL);
	if (err < 0) {
		if (err == -ENOSPC) {
			pr_err("too many PPS sources in the system\n");
			err = -EBUSY;
		}
		return err;
	}
	pps_gen->id = err;

	devt = MKDEV(MAJOR(pps_gen_devt), pps_gen->id);

	cdev_init(&pps_gen->cdev, &pps_gen_cdev_fops);
	pps_gen->cdev.owner = pps_gen->info.owner;

	err = cdev_add(&pps_gen->cdev, devt, 1);
	if (err) {
		pr_err("failed to add char device %d:%d\n",
				MAJOR(pps_gen_devt), pps_gen->id);
		goto free_ida;
	}
	pps_gen->dev = device_create(pps_gen_class, pps_gen->info.parent, devt,
					pps_gen, "pps-gen%d", pps_gen->id);
	if (IS_ERR(pps_gen->dev)) {
		err = PTR_ERR(pps_gen->dev);
		goto del_cdev;
	}
	pps_gen->dev->release = pps_gen_device_destruct;
	dev_set_drvdata(pps_gen->dev, pps_gen);

	pr_debug("generator got cdev (%d:%d)\n",
			MAJOR(pps_gen_devt), pps_gen->id);

	return 0;

del_cdev:
	cdev_del(&pps_gen->cdev);
free_ida:
	ida_free(&pps_gen_ida, pps_gen->id);
	return err;
}

static void pps_gen_unregister_cdev(struct pps_gen_device *pps_gen)
{
	pr_debug("unregistering pps-gen%d\n", pps_gen->id);
	device_destroy(pps_gen_class, pps_gen->dev->devt);
}

/*
 * Exported functions
 */

/**
 * pps_gen_register_source() - add a PPS generator in the system
 * @info: the PPS generator info struct
 *
 * This function is used to register a new PPS generator in the system.
 * When it returns successfully the new generator is up and running, and
 * it can be managed by the userspace.
 *
 * Return: the PPS generator device in case of success, and ERR_PTR(errno)
 *	 otherwise.
 */
struct pps_gen_device *pps_gen_register_source(struct pps_gen_source_info *info)
{
	struct pps_gen_device *pps_gen;
	int err;

	pps_gen = kzalloc(sizeof(struct pps_gen_device), GFP_KERNEL);
	if (pps_gen == NULL) {
		err = -ENOMEM;
		goto pps_gen_register_source_exit;
	}
	pps_gen->info = *info;
	pps_gen->enabled = false;

	init_waitqueue_head(&pps_gen->queue);
	spin_lock_init(&pps_gen->lock);

	/* Create the char device */
	err = pps_gen_register_cdev(pps_gen);
	if (err < 0) {
		pr_err(" unable to create char device\n");
		goto kfree_pps_gen;
	}

	return pps_gen;

kfree_pps_gen:
	kfree(pps_gen);

pps_gen_register_source_exit:
	pr_err("unable to register generator\n");

	return ERR_PTR(err);
}
EXPORT_SYMBOL(pps_gen_register_source);

/**
 * pps_gen_unregister_source() - remove a PPS generator from the system
 * @pps_gen: the PPS generator device to be removed
 *
 * This function is used to deregister a PPS generator from the system. When
 * called, it disables the generator so no pulses are generated anymore.
 */
void pps_gen_unregister_source(struct pps_gen_device *pps_gen)
{
	pps_gen_unregister_cdev(pps_gen);
}
EXPORT_SYMBOL(pps_gen_unregister_source);

/* pps_gen_event - register a PPS generator event into the system
 * @pps: the PPS generator device
 * @event: the event type
 * @data: userdef pointer
 *
 * This function is used by each PPS generator in order to register a new
 * PPS event into the system (it's usually called inside an IRQ handler).
 */
void pps_gen_event(struct pps_gen_device *pps_gen,
			unsigned int event, void *data)
{
	unsigned long flags;

	dev_dbg(pps_gen->dev, "PPS generator event %u\n", event);

	spin_lock_irqsave(&pps_gen->lock, flags);

	pps_gen->event = event;
	pps_gen->sequence++;

	pps_gen->last_ev++;
	wake_up_interruptible_all(&pps_gen->queue);
	kill_fasync(&pps_gen->async_queue, SIGIO, POLL_IN);

	spin_unlock_irqrestore(&pps_gen->lock, flags);
}
EXPORT_SYMBOL(pps_gen_event);

/*
 * Module stuff
 */

static void __exit pps_gen_exit(void)
{
	class_destroy(pps_gen_class);
	unregister_chrdev_region(pps_gen_devt, PPS_GEN_MAX_SOURCES);
}

static int __init pps_gen_init(void)
{
	int err;

	pps_gen_class = class_create("pps-gen");
	if (IS_ERR(pps_gen_class)) {
		pr_err("failed to allocate class\n");
		return PTR_ERR(pps_gen_class);
	}
	pps_gen_class->dev_groups = pps_gen_groups;

	err = alloc_chrdev_region(&pps_gen_devt, 0,
					PPS_GEN_MAX_SOURCES, "pps-gen");
	if (err < 0) {
		pr_err("failed to allocate char device region\n");
		goto remove_class;
	}

	return 0;

remove_class:
	class_destroy(pps_gen_class);
	return err;
}

subsys_initcall(pps_gen_init);
module_exit(pps_gen_exit);

MODULE_AUTHOR("Rodolfo Giometti <giometti@enneenne.com>");
MODULE_DESCRIPTION("LinuxPPS generators support");
MODULE_LICENSE("GPL");
