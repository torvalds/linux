/*
 * RDC321x watchdog driver
 *
 * Copyright (C) 2007 Florian Fainelli <florian@openwrt.org>
 *
 * This driver is highly inspired from the cpu5_wdt driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/timer.h>
#include <linux/completion.h>
#include <linux/jiffies.h>
#include <linux/platform_device.h>
#include <linux/watchdog.h>
#include <linux/io.h>
#include <linux/uaccess.h>

#include <asm/rdc321x_defs.h>

#define RDC_WDT_MASK	0x80000000 /* Mask */
#define RDC_WDT_EN	0x00800000 /* Enable bit */
#define RDC_WDT_WTI	0x00200000 /* Generate CPU reset/NMI/WDT on timeout */
#define RDC_WDT_RST	0x00100000 /* Reset bit */
#define RDC_WDT_WIF	0x00040000 /* WDT IRQ Flag */
#define RDC_WDT_IRT	0x00000100 /* IRQ Routing table */
#define RDC_WDT_CNT	0x00000001 /* WDT count */

#define RDC_CLS_TMR	0x80003844 /* Clear timer */

#define RDC_WDT_INTERVAL	(HZ/10+1)

static int ticks = 1000;

/* some device data */

static struct {
	struct completion stop;
	int running;
	struct timer_list timer;
	int queue;
	int default_ticks;
	unsigned long inuse;
	spinlock_t lock;
} rdc321x_wdt_device;

/* generic helper functions */

static void rdc321x_wdt_trigger(unsigned long unused)
{
	unsigned long flags;

	if (rdc321x_wdt_device.running)
		ticks--;

	/* keep watchdog alive */
	spin_lock_irqsave(&rdc321x_wdt_device.lock, flags);
	outl(RDC_WDT_EN | inl(RDC3210_CFGREG_DATA),
		RDC3210_CFGREG_DATA);
	spin_unlock_irqrestore(&rdc321x_wdt_device.lock, flags);

	/* requeue?? */
	if (rdc321x_wdt_device.queue && ticks)
		mod_timer(&rdc321x_wdt_device.timer,
				jiffies + RDC_WDT_INTERVAL);
	else {
		/* ticks doesn't matter anyway */
		complete(&rdc321x_wdt_device.stop);
	}

}

static void rdc321x_wdt_reset(void)
{
	ticks = rdc321x_wdt_device.default_ticks;
}

static void rdc321x_wdt_start(void)
{
	unsigned long flags;

	if (!rdc321x_wdt_device.queue) {
		rdc321x_wdt_device.queue = 1;

		/* Clear the timer */
		spin_lock_irqsave(&rdc321x_wdt_device.lock, flags);
		outl(RDC_CLS_TMR, RDC3210_CFGREG_ADDR);

		/* Enable watchdog and set the timeout to 81.92 us */
		outl(RDC_WDT_EN | RDC_WDT_CNT, RDC3210_CFGREG_DATA);
		spin_unlock_irqrestore(&rdc321x_wdt_device.lock, flags);

		mod_timer(&rdc321x_wdt_device.timer,
				jiffies + RDC_WDT_INTERVAL);
	}

	/* if process dies, counter is not decremented */
	rdc321x_wdt_device.running++;
}

static int rdc321x_wdt_stop(void)
{
	if (rdc321x_wdt_device.running)
		rdc321x_wdt_device.running = 0;

	ticks = rdc321x_wdt_device.default_ticks;

	return -EIO;
}

/* filesystem operations */
static int rdc321x_wdt_open(struct inode *inode, struct file *file)
{
	if (test_and_set_bit(0, &rdc321x_wdt_device.inuse))
		return -EBUSY;

	return nonseekable_open(inode, file);
}

static int rdc321x_wdt_release(struct inode *inode, struct file *file)
{
	clear_bit(0, &rdc321x_wdt_device.inuse);
	return 0;
}

static long rdc321x_wdt_ioctl(struct file *file, unsigned int cmd,
				unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	unsigned int value;
	static const struct watchdog_info ident = {
		.options = WDIOF_CARDRESET,
		.identity = "RDC321x WDT",
	};
	unsigned long flags;

	switch (cmd) {
	case WDIOC_KEEPALIVE:
		rdc321x_wdt_reset();
		break;
	case WDIOC_GETSTATUS:
		/* Read the value from the DATA register */
		spin_lock_irqsave(&rdc321x_wdt_device.lock, flags);
		value = inl(RDC3210_CFGREG_DATA);
		spin_unlock_irqrestore(&rdc321x_wdt_device.lock, flags);
		if (copy_to_user(argp, &value, sizeof(int)))
			return -EFAULT;
		break;
	case WDIOC_GETSUPPORT:
		if (copy_to_user(argp, &ident, sizeof(ident)))
			return -EFAULT;
		break;
	case WDIOC_SETOPTIONS:
		if (copy_from_user(&value, argp, sizeof(int)))
			return -EFAULT;
		switch (value) {
		case WDIOS_ENABLECARD:
			rdc321x_wdt_start();
			break;
		case WDIOS_DISABLECARD:
			return rdc321x_wdt_stop();
		default:
			return -EINVAL;
		}
		break;
	default:
		return -ENOTTY;
	}
	return 0;
}

static ssize_t rdc321x_wdt_write(struct file *file, const char __user *buf,
				size_t count, loff_t *ppos)
{
	if (!count)
		return -EIO;

	rdc321x_wdt_reset();

	return count;
}

static const struct file_operations rdc321x_wdt_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.unlocked_ioctl	= rdc321x_wdt_ioctl,
	.open		= rdc321x_wdt_open,
	.write		= rdc321x_wdt_write,
	.release	= rdc321x_wdt_release,
};

static struct miscdevice rdc321x_wdt_misc = {
	.minor	= WATCHDOG_MINOR,
	.name	= "watchdog",
	.fops	= &rdc321x_wdt_fops,
};

static int __devinit rdc321x_wdt_probe(struct platform_device *pdev)
{
	int err;

	err = misc_register(&rdc321x_wdt_misc);
	if (err < 0) {
		printk(KERN_ERR PFX "watchdog misc_register failed\n");
		return err;
	}

	spin_lock_init(&rdc321x_wdt_device.lock);

	/* Reset the watchdog */
	outl(RDC_WDT_RST, RDC3210_CFGREG_DATA);

	init_completion(&rdc321x_wdt_device.stop);
	rdc321x_wdt_device.queue = 0;

	clear_bit(0, &rdc321x_wdt_device.inuse);

	setup_timer(&rdc321x_wdt_device.timer, rdc321x_wdt_trigger, 0);

	rdc321x_wdt_device.default_ticks = ticks;

	printk(KERN_INFO PFX "watchdog init success\n");

	return 0;
}

static int __devexit rdc321x_wdt_remove(struct platform_device *pdev)
{
	if (rdc321x_wdt_device.queue) {
		rdc321x_wdt_device.queue = 0;
		wait_for_completion(&rdc321x_wdt_device.stop);
	}

	misc_deregister(&rdc321x_wdt_misc);

	return 0;
}

static struct platform_driver rdc321x_wdt_driver = {
	.probe = rdc321x_wdt_probe,
	.remove = __devexit_p(rdc321x_wdt_remove),
	.driver = {
		.owner = THIS_MODULE,
		.name = "rdc321x-wdt",
	},
};

static int __init rdc321x_wdt_init(void)
{
	return platform_driver_register(&rdc321x_wdt_driver);
}

static void __exit rdc321x_wdt_exit(void)
{
	platform_driver_unregister(&rdc321x_wdt_driver);
}

module_init(rdc321x_wdt_init);
module_exit(rdc321x_wdt_exit);

MODULE_AUTHOR("Florian Fainelli <florian@openwrt.org>");
MODULE_DESCRIPTION("RDC321x watchdog driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);
