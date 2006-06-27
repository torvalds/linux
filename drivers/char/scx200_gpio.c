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

#define NAME "scx200_gpio"
#define DEVNAME NAME

static struct platform_device *pdev;

MODULE_AUTHOR("Christer Weinigel <wingel@nano-system.com>");
MODULE_DESCRIPTION("NatSemi SCx200 GPIO Pin Driver");
MODULE_LICENSE("GPL");

static int major = 0;		/* default to dynamic major */
module_param(major, int, 0);
MODULE_PARM_DESC(major, "Major device number");

extern void scx200_gpio_dump(unsigned index);

static ssize_t scx200_gpio_write(struct file *file, const char __user *data,
				 size_t len, loff_t *ppos)
{
	unsigned m = iminor(file->f_dentry->d_inode);
	size_t i;
	int err = 0;

	for (i = 0; i < len; ++i) {
		char c;
		if (get_user(c, data + i))
			return -EFAULT;
		switch (c) {
		case '0':
			scx200_gpio_set(m, 0);
			break;
		case '1':
			scx200_gpio_set(m, 1);
			break;
		case 'O':
			printk(KERN_INFO NAME ": GPIO%d output enabled\n", m);
			scx200_gpio_configure(m, ~1, 1);
			break;
		case 'o':
			printk(KERN_INFO NAME ": GPIO%d output disabled\n", m);
			scx200_gpio_configure(m, ~1, 0);
			break;
		case 'T':
			printk(KERN_INFO NAME ": GPIO%d output is push pull\n", m);
			scx200_gpio_configure(m, ~2, 2);
			break;
		case 't':
			printk(KERN_INFO NAME ": GPIO%d output is open drain\n", m);
			scx200_gpio_configure(m, ~2, 0);
			break;
		case 'P':
			printk(KERN_INFO NAME ": GPIO%d pull up enabled\n", m);
			scx200_gpio_configure(m, ~4, 4);
			break;
		case 'p':
			printk(KERN_INFO NAME ": GPIO%d pull up disabled\n", m);
			scx200_gpio_configure(m, ~4, 0);
			break;

		case 'v':
			/* View Current pin settings */
			scx200_gpio_dump(m);
			break;
		case '\n':
			/* end of settings string, do nothing */
			break;
		default:
			printk(KERN_ERR NAME
			       ": GPIO-%2d bad setting: chr<0x%2x>\n", m,
			       (int)c);
			err++;
		}
	}
	if (err)
		return -EINVAL;	/* full string handled, report error */

	return len;
}

static ssize_t scx200_gpio_read(struct file *file, char __user *buf,
				size_t len, loff_t *ppos)
{
	unsigned m = iminor(file->f_dentry->d_inode);
	int value;

	value = scx200_gpio_get(m);
	if (put_user(value ? '1' : '0', buf))
		return -EFAULT;

	return 1;
}

static int scx200_gpio_open(struct inode *inode, struct file *file)
{
	unsigned m = iminor(inode);
	if (m > 63)
		return -EINVAL;
	return nonseekable_open(inode, file);
}

static int scx200_gpio_release(struct inode *inode, struct file *file)
{
	return 0;
}


static struct file_operations scx200_gpio_fops = {
	.owner   = THIS_MODULE,
	.write   = scx200_gpio_write,
	.read    = scx200_gpio_read,
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
	platform_device_put(pdev);
undo_malloc:
	kfree(pdev);
	return rc;
}

static void __exit scx200_gpio_cleanup(void)
{
	kfree(scx200_devices);
	unregister_chrdev_region(MKDEV(major, 0), num_pins);
	platform_device_put(pdev);
	platform_device_unregister(pdev);
	/* kfree(pdev); */
}

module_init(scx200_gpio_init);
module_exit(scx200_gpio_cleanup);
