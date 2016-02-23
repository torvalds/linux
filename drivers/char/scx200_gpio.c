/* linux/drivers/char/scx200_gpio.c

   National Semiconductor SCx200 GPIO driver.  Allows a user space
   process to play with the GPIO pins.

   Copyright (c) 2001,2002 Christer Weinigel <wingel@nano-system.com> */

#include <linux/device.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#include <linux/types.h>
#include <linux/cdev.h>

#include <linux/scx200_gpio.h>
#include <linux/nsc_gpio.h>

#define DRVNAME "scx200_gpio"

static struct platform_device *pdev;

MODULE_AUTHOR("Christer Weinigel <wingel@nano-system.com>");
MODULE_DESCRIPTION("NatSemi/AMD SCx200 GPIO Pin Driver");
MODULE_LICENSE("GPL");

static int major = 0;		/* default to dynamic major */
module_param(major, int, 0);
MODULE_PARM_DESC(major, "Major device number");

#define MAX_PINS 32		/* 64 later, when known ok */

struct nsc_gpio_ops scx200_gpio_ops = {
	.owner		= THIS_MODULE,
	.gpio_config	= scx200_gpio_configure,
	.gpio_dump	= nsc_gpio_dump,
	.gpio_get	= scx200_gpio_get,
	.gpio_set	= scx200_gpio_set,
	.gpio_change	= scx200_gpio_change,
	.gpio_current	= scx200_gpio_current
};
EXPORT_SYMBOL_GPL(scx200_gpio_ops);

static int scx200_gpio_open(struct inode *inode, struct file *file)
{
	unsigned m = iminor(inode);
	file->private_data = &scx200_gpio_ops;

	if (m >= MAX_PINS)
		return -EINVAL;
	return nonseekable_open(inode, file);
}

static int scx200_gpio_release(struct inode *inode, struct file *file)
{
	return 0;
}

static const struct file_operations scx200_gpio_fileops = {
	.owner   = THIS_MODULE,
	.write   = nsc_gpio_write,
	.read    = nsc_gpio_read,
	.open    = scx200_gpio_open,
	.release = scx200_gpio_release,
	.llseek  = no_llseek,
};

static struct cdev scx200_gpio_cdev;  /* use 1 cdev for all pins */

static int __init scx200_gpio_init(void)
{
	int rc;
	dev_t devid;

	if (!scx200_gpio_present()) {
		printk(KERN_ERR DRVNAME ": no SCx200 gpio present\n");
		return -ENODEV;
	}

	/* support dev_dbg() with pdev->dev */
	pdev = platform_device_alloc(DRVNAME, 0);
	if (!pdev)
		return -ENOMEM;

	rc = platform_device_add(pdev);
	if (rc)
		goto undo_malloc;

	/* nsc_gpio uses dev_dbg(), so needs this */
	scx200_gpio_ops.dev = &pdev->dev;

	if (major) {
		devid = MKDEV(major, 0);
		rc = register_chrdev_region(devid, MAX_PINS, "scx200_gpio");
	} else {
		rc = alloc_chrdev_region(&devid, 0, MAX_PINS, "scx200_gpio");
		major = MAJOR(devid);
	}
	if (rc < 0) {
		dev_err(&pdev->dev, "SCx200 chrdev_region err: %d\n", rc);
		goto undo_platform_device_add;
	}

	cdev_init(&scx200_gpio_cdev, &scx200_gpio_fileops);
	cdev_add(&scx200_gpio_cdev, devid, MAX_PINS);

	return 0; /* succeed */

undo_platform_device_add:
	platform_device_del(pdev);
undo_malloc:
	platform_device_put(pdev);

	return rc;
}

static void __exit scx200_gpio_cleanup(void)
{
	cdev_del(&scx200_gpio_cdev);
	/* cdev_put(&scx200_gpio_cdev); */

	unregister_chrdev_region(MKDEV(major, 0), MAX_PINS);
	platform_device_unregister(pdev);
}

module_init(scx200_gpio_init);
module_exit(scx200_gpio_cleanup);
