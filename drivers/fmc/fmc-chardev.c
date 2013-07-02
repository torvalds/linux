/*
 * Copyright (C) 2012 CERN (www.cern.ch)
 * Author: Alessandro Rubini <rubini@gnudd.com>
 *
 * Released according to the GNU GPL, version 2 or any later version.
 *
 * This work is part of the White Rabbit project, a research effort led
 * by CERN, the European Institute for Nuclear Research.
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/spinlock.h>
#include <linux/fmc.h>
#include <linux/uaccess.h>

static LIST_HEAD(fc_devices);
static DEFINE_SPINLOCK(fc_lock);

struct fc_instance {
	struct list_head list;
	struct fmc_device *fmc;
	struct miscdevice misc;
};

/* at open time, we must identify our device */
static int fc_open(struct inode *ino, struct file *f)
{
	struct fmc_device *fmc;
	struct fc_instance *fc;
	int minor = iminor(ino);

	list_for_each_entry(fc, &fc_devices, list)
		if (fc->misc.minor == minor)
			break;
	if (fc->misc.minor != minor)
		return -ENODEV;
	fmc = fc->fmc;
	if (try_module_get(fmc->owner) == 0)
		return -ENODEV;

	f->private_data = fmc;
	return 0;
}

static int fc_release(struct inode *ino, struct file *f)
{
	struct fmc_device *fmc = f->private_data;
	module_put(fmc->owner);
	return 0;
}

/* read and write are simple after the default llseek has been used */
static ssize_t fc_read(struct file *f, char __user *buf, size_t count,
		       loff_t *offp)
{
	struct fmc_device *fmc = f->private_data;
	unsigned long addr;
	uint32_t val;

	if (count < sizeof(val))
		return -EINVAL;
	count = sizeof(val);

	addr = *offp;
	if (addr > fmc->memlen)
		return -ESPIPE; /* Illegal seek */
	val = fmc_readl(fmc, addr);
	if (copy_to_user(buf, &val, count))
		return -EFAULT;
	*offp += count;
	return count;
}

static ssize_t fc_write(struct file *f, const char __user *buf, size_t count,
			loff_t *offp)
{
	struct fmc_device *fmc = f->private_data;
	unsigned long addr;
	uint32_t val;

	if (count < sizeof(val))
		return -EINVAL;
	count = sizeof(val);

	addr = *offp;
	if (addr > fmc->memlen)
		return -ESPIPE; /* Illegal seek */
	if (copy_from_user(&val, buf, count))
		return -EFAULT;
	fmc_writel(fmc, val, addr);
	*offp += count;
	return count;
}

static const struct file_operations fc_fops = {
	.owner = THIS_MODULE,
	.open = fc_open,
	.release = fc_release,
	.llseek = generic_file_llseek,
	.read = fc_read,
	.write = fc_write,
};


/* Device part .. */
static int fc_probe(struct fmc_device *fmc);
static int fc_remove(struct fmc_device *fmc);

static struct fmc_driver fc_drv = {
	.version = FMC_VERSION,
	.driver.name = KBUILD_MODNAME,
	.probe = fc_probe,
	.remove = fc_remove,
	/* no table: we want to match everything */
};

/* We accept the generic busid parameter */
FMC_PARAM_BUSID(fc_drv);

/* probe and remove must allocate and release a misc device */
static int fc_probe(struct fmc_device *fmc)
{
	int ret;
	int index = 0;

	struct fc_instance *fc;

	if (fmc->op->validate)
		index = fmc->op->validate(fmc, &fc_drv);
	if (index < 0)
		return -EINVAL; /* not our device: invalid */

	/* Create a char device: we want to create it anew */
	fc = kzalloc(sizeof(*fc), GFP_KERNEL);
	if (!fc)
		return -ENOMEM;
	fc->fmc = fmc;
	fc->misc.minor = MISC_DYNAMIC_MINOR;
	fc->misc.fops = &fc_fops;
	fc->misc.name = kstrdup(dev_name(&fmc->dev), GFP_KERNEL);

	spin_lock(&fc_lock);
	ret = misc_register(&fc->misc);
	if (ret < 0)
		goto err_unlock;
	list_add(&fc->list, &fc_devices);
	spin_unlock(&fc_lock);
	dev_info(&fc->fmc->dev, "Created misc device \"%s\"\n",
		 fc->misc.name);
	return 0;

err_unlock:
	spin_unlock(&fc_lock);
	kfree(fc->misc.name);
	kfree(fc);
	return ret;
}

static int fc_remove(struct fmc_device *fmc)
{
	struct fc_instance *fc;

	list_for_each_entry(fc, &fc_devices, list)
		if (fc->fmc == fmc)
			break;
	if (fc->fmc != fmc) {
		dev_err(&fmc->dev, "remove called but not found\n");
		return -ENODEV;
	}

	spin_lock(&fc_lock);
	list_del(&fc->list);
	misc_deregister(&fc->misc);
	kfree(fc->misc.name);
	kfree(fc);
	spin_unlock(&fc_lock);

	return 0;
}


static int fc_init(void)
{
	int ret;

	ret = fmc_driver_register(&fc_drv);
	return ret;
}

static void fc_exit(void)
{
	fmc_driver_unregister(&fc_drv);
}

module_init(fc_init);
module_exit(fc_exit);

MODULE_LICENSE("GPL");
