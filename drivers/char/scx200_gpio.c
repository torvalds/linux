/* linux/drivers/char/scx200_gpio.c

   National Semiconductor SCx200 GPIO driver.  Allows a user space
   process to play with the GPIO pins.

   Copyright (c) 2001,2002 Christer Weinigel <wingel@nano-system.com> */

#include <linux/config.h>
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

#define NAME "scx200_gpio"
#define DEVNAME NAME

static struct platform_device *pdev;

MODULE_AUTHOR("Christer Weinigel <wingel@nano-system.com>");
MODULE_DESCRIPTION("NatSemi SCx200 GPIO Pin Driver");
MODULE_LICENSE("GPL");

static int major = 0;		/* default to dynamic major */
module_param(major, int, 0);
MODULE_PARM_DESC(major, "Major device number");

struct nsc_gpio_ops scx200_access = {
	.owner		= THIS_MODULE,
	.gpio_config	= scx200_gpio_configure,
	.gpio_dump	= nsc_gpio_dump,
	.gpio_get	= scx200_gpio_get,
	.gpio_set	= scx200_gpio_set,
	.gpio_set_high	= scx200_gpio_set_high,
	.gpio_set_low	= scx200_gpio_set_low,
	.gpio_change	= scx200_gpio_change,
	.gpio_current	= scx200_gpio_current
};

static int scx200_gpio_open(struct inode *inode, struct file *file)
{
	unsigned m = iminor(inode);
	file->private_data = &scx200_access;

	if (m > 63)
		return -EINVAL;
	return nonseekable_open(inode, file);
}

static int scx200_gpio_release(struct inode *inode, struct file *file)
{
	return 0;
}


static const struct file_operations scx200_gpio_fops = {
	.owner   = THIS_MODULE,
	.write   = nsc_gpio_write,
	.read    = nsc_gpio_read,
	.open    = scx200_gpio_open,
	.release = scx200_gpio_release,
};

struct cdev *scx200_devices;
static int num_pins = 32;

static int __init scx200_gpio_init(void)
{
	int rc, i;
	dev_t dev = MKDEV(major, 0);

	if (!scx200_gpio_present()) {
		printk(KERN_ERR NAME ": no SCx200 gpio present\n");
		return -ENODEV;
	}

	/* support dev_dbg() with pdev->dev */
	pdev = platform_device_alloc(DEVNAME, 0);
	if (!pdev)
		return -ENOMEM;

	rc = platform_device_add(pdev);
	if (rc)
		goto undo_malloc;

	/* nsc_gpio uses dev_dbg(), so needs this */
	scx200_access.dev = &pdev->dev;

	if (major)
		rc = register_chrdev_region(dev, num_pins, "scx200_gpio");
	else {
		rc = alloc_chrdev_region(&dev, 0, num_pins, "scx200_gpio");
		major = MAJOR(dev);
	}
	if (rc < 0) {
		dev_err(&pdev->dev, "SCx200 chrdev_region err: %d\n", rc);
		goto undo_platform_device_add;
	}
	scx200_devices = kzalloc(num_pins * sizeof(struct cdev), GFP_KERNEL);
	if (!scx200_devices) {
		rc = -ENOMEM;
		goto undo_chrdev_region;
	}
	for (i = 0; i < num_pins; i++) {
		struct cdev *cdev = &scx200_devices[i];
		cdev_init(cdev, &scx200_gpio_fops);
		cdev->owner = THIS_MODULE;
		rc = cdev_add(cdev, MKDEV(major, i), 1);
		/* tolerate 'minor' errors */
		if (rc)
			dev_err(&pdev->dev, "Error %d on minor %d", rc, i);
	}

	return 0; /* succeed */

undo_chrdev_region:
	unregister_chrdev_region(dev, num_pins);
undo_platform_device_add:
	platform_device_del(pdev);
undo_malloc:
	platform_device_put(pdev);

	return rc;
}

static void __exit scx200_gpio_cleanup(void)
{
	kfree(scx200_devices);
	unregister_chrdev_region(MKDEV(major, 0), num_pins);
	platform_device_unregister(pdev);
	/* kfree(pdev); */
}

module_init(scx200_gpio_init);
module_exit(scx200_gpio_cleanup);
