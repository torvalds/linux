/*
 * mpc83xx_wdt.c - MPC83xx watchdog userspace interface
 *
 * Authors: Dave Updegraff <dave@cray.org>
 * 	    Kumar Gala <galak@kernel.crashing.org>
 * 		Attribution: from 83xx_wst: Florian Schirmer <jolt@tuxbox.org>
 * 				..and from sc520_wdt
 *
 * Note: it appears that you can only actually ENABLE or DISABLE the thing
 * once after POR. Once enabled, you cannot disable, and vice versa.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/watchdog.h>
#include <asm/io.h>
#include <asm/uaccess.h>

struct mpc83xx_wdt {
	__be32 res0;
	__be32 swcrr; /* System watchdog control register */
#define SWCRR_SWTC 0xFFFF0000 /* Software Watchdog Time Count. */
#define SWCRR_SWEN 0x00000004 /* Watchdog Enable bit. */
#define SWCRR_SWRI 0x00000002 /* Software Watchdog Reset/Interrupt Select bit.*/
#define SWCRR_SWPR 0x00000001 /* Software Watchdog Counter Prescale bit. */
	__be32 swcnr; /* System watchdog count register */
	u8 res1[2];
	__be16 swsrr; /* System watchdog service register */
	u8 res2[0xF0];
};

static struct mpc83xx_wdt __iomem *wd_base;

static u16 timeout = 0xffff;
module_param(timeout, ushort, 0);
MODULE_PARM_DESC(timeout, "Watchdog timeout in ticks. (0<timeout<65536, default=65535");

static int reset = 1;
module_param(reset, bool, 0);
MODULE_PARM_DESC(reset, "Watchdog Interrupt/Reset Mode. 0 = interrupt, 1 = reset");

/*
 * We always prescale, but if someone really doesn't want to they can set this
 * to 0
 */
static int prescale = 1;
static unsigned int timeout_sec;

static unsigned long wdt_is_open;
static DEFINE_SPINLOCK(wdt_spinlock);

static void mpc83xx_wdt_keepalive(void)
{
	/* Ping the WDT */
	spin_lock(&wdt_spinlock);
	out_be16(&wd_base->swsrr, 0x556c);
	out_be16(&wd_base->swsrr, 0xaa39);
	spin_unlock(&wdt_spinlock);
}

static ssize_t mpc83xx_wdt_write(struct file *file, const char __user *buf,
				 size_t count, loff_t *ppos)
{
	if (count)
		mpc83xx_wdt_keepalive();
	return count;
}

static int mpc83xx_wdt_open(struct inode *inode, struct file *file)
{
	u32 tmp = SWCRR_SWEN;
	if (test_and_set_bit(0, &wdt_is_open))
		return -EBUSY;

	/* Once we start the watchdog we can't stop it */
	__module_get(THIS_MODULE);

	/* Good, fire up the show */
	if (prescale)
		tmp |= SWCRR_SWPR;
	if (reset)
		tmp |= SWCRR_SWRI;

	tmp |= timeout << 16;

	out_be32(&wd_base->swcrr, tmp);

	return nonseekable_open(inode, file);
}

static int mpc83xx_wdt_release(struct inode *inode, struct file *file)
{
	printk(KERN_CRIT "Unexpected close, not stopping watchdog!\n");
	mpc83xx_wdt_keepalive();
	clear_bit(0, &wdt_is_open);
	return 0;
}

static int mpc83xx_wdt_ioctl(struct inode *inode, struct file *file,
				unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	int __user *p = argp;
	static struct watchdog_info ident = {
		.options = WDIOF_KEEPALIVEPING,
		.firmware_version = 1,
		.identity = "MPC83xx",
	};

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		return copy_to_user(argp, &ident, sizeof(ident)) ? -EFAULT : 0;
	case WDIOC_GETSTATUS:
	case WDIOC_GETBOOTSTATUS:
		return put_user(0, p);
	case WDIOC_KEEPALIVE:
		mpc83xx_wdt_keepalive();
		return 0;
	case WDIOC_GETTIMEOUT:
		return put_user(timeout_sec, p);
	default:
		return -ENOTTY;
	}
}

static const struct file_operations mpc83xx_wdt_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.write		= mpc83xx_wdt_write,
	.ioctl		= mpc83xx_wdt_ioctl,
	.open		= mpc83xx_wdt_open,
	.release	= mpc83xx_wdt_release,
};

static struct miscdevice mpc83xx_wdt_miscdev = {
	.minor	= WATCHDOG_MINOR,
	.name	= "watchdog",
	.fops	= &mpc83xx_wdt_fops,
};

static int __devinit mpc83xx_wdt_probe(struct platform_device *dev)
{
	struct resource *r;
	int ret;
	unsigned int *freq = dev->dev.platform_data;

	/* get a pointer to the register memory */
	r = platform_get_resource(dev, IORESOURCE_MEM, 0);

	if (!r) {
		ret = -ENODEV;
		goto err_out;
	}

	wd_base = ioremap(r->start, sizeof (struct mpc83xx_wdt));

	if (wd_base == NULL) {
		ret = -ENOMEM;
		goto err_out;
	}

	ret = misc_register(&mpc83xx_wdt_miscdev);
	if (ret) {
		printk(KERN_ERR "cannot register miscdev on minor=%d "
				"(err=%d)\n",
				WATCHDOG_MINOR, ret);
		goto err_unmap;
	}

	/* Calculate the timeout in seconds */
	if (prescale)
		timeout_sec = (timeout * 0x10000) / (*freq);
	else
		timeout_sec = timeout / (*freq);

	printk(KERN_INFO "WDT driver for MPC83xx initialized. "
		"mode:%s timeout=%d (%d seconds)\n",
		reset ? "reset":"interrupt", timeout, timeout_sec);
	return 0;

err_unmap:
	iounmap(wd_base);
err_out:
	return ret;
}

static int __devexit mpc83xx_wdt_remove(struct platform_device *dev)
{
	misc_deregister(&mpc83xx_wdt_miscdev);
	iounmap(wd_base);

	return 0;
}

static struct platform_driver mpc83xx_wdt_driver = {
	.probe		= mpc83xx_wdt_probe,
	.remove		= __devexit_p(mpc83xx_wdt_remove),
	.driver		= {
		.name	= "mpc83xx_wdt",
	},
};

static int __init mpc83xx_wdt_init(void)
{
	return platform_driver_register(&mpc83xx_wdt_driver);
}

static void __exit mpc83xx_wdt_exit(void)
{
	platform_driver_unregister(&mpc83xx_wdt_driver);
}

module_init(mpc83xx_wdt_init);
module_exit(mpc83xx_wdt_exit);

MODULE_AUTHOR("Dave Updegraff, Kumar Gala");
MODULE_DESCRIPTION("Driver for watchdog timer in MPC83xx uProcessor");
MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);
