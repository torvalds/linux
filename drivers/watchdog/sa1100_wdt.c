/*
 *	Watchdog driver for the SA11x0/PXA2xx
 *
 *	(c) Copyright 2000 Oleg Drokin <green@crimea.edu>
 *	    Based on SoftDog driver by Alan Cox <alan@lxorguk.ukuu.org.uk>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *	Neither Oleg Drokin nor iXcelerator.com admit liability nor provide
 *	warranty for any of this software. This material is provided
 *	"AS-IS" and at no charge.
 *
 *	(c) Copyright 2000           Oleg Drokin <green@crimea.edu>
 *
 *	27/11/2000 Initial release
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/init.h>
#include <linux/bitops.h>
#include <linux/uaccess.h>
#include <linux/timex.h>

#ifdef CONFIG_ARCH_PXA
#include <mach/regs-ost.h>
#endif

#include <mach/reset.h>
#include <mach/hardware.h>

static unsigned long oscr_freq;
static unsigned long sa1100wdt_users;
static int pre_margin;
static int boot_status;

/*
 *	Allow only one person to hold it open
 */
static int sa1100dog_open(struct inode *inode, struct file *file)
{
	if (test_and_set_bit(1, &sa1100wdt_users))
		return -EBUSY;

	/* Activate SA1100 Watchdog timer */
	OSMR3 = OSCR + pre_margin;
	OSSR = OSSR_M3;
	OWER = OWER_WME;
	OIER |= OIER_E3;
	return nonseekable_open(inode, file);
}

/*
 * The watchdog cannot be disabled.
 *
 * Previous comments suggested that turning off the interrupt by
 * clearing OIER[E3] would prevent the watchdog timing out but this
 * does not appear to be true (at least on the PXA255).
 */
static int sa1100dog_release(struct inode *inode, struct file *file)
{
	printk(KERN_CRIT "WATCHDOG: Device closed - timer will not stop\n");
	clear_bit(1, &sa1100wdt_users);
	return 0;
}

static ssize_t sa1100dog_write(struct file *file, const char __user *data,
						size_t len, loff_t *ppos)
{
	if (len)
		/* Refresh OSMR3 timer. */
		OSMR3 = OSCR + pre_margin;
	return len;
}

static const struct watchdog_info ident = {
	.options	= WDIOF_CARDRESET | WDIOF_SETTIMEOUT
				| WDIOF_KEEPALIVEPING,
	.identity	= "SA1100/PXA255 Watchdog",
};

static long sa1100dog_ioctl(struct file *file, unsigned int cmd,
							unsigned long arg)
{
	int ret = -ENOTTY;
	int time;
	void __user *argp = (void __user *)arg;
	int __user *p = argp;

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		ret = copy_to_user(argp, &ident,
				   sizeof(ident)) ? -EFAULT : 0;
		break;

	case WDIOC_GETSTATUS:
		ret = put_user(0, p);
		break;

	case WDIOC_GETBOOTSTATUS:
		ret = put_user(boot_status, p);
		break;

	case WDIOC_KEEPALIVE:
		OSMR3 = OSCR + pre_margin;
		ret = 0;
		break;

	case WDIOC_SETTIMEOUT:
		ret = get_user(time, p);
		if (ret)
			break;

		if (time <= 0 || time > 255) {
			ret = -EINVAL;
			break;
		}

		pre_margin = oscr_freq * time;
		OSMR3 = OSCR + pre_margin;
		/*fall through*/

	case WDIOC_GETTIMEOUT:
		ret = put_user(pre_margin / oscr_freq, p);
		break;
	}
	return ret;
}

static const struct file_operations sa1100dog_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.write		= sa1100dog_write,
	.unlocked_ioctl	= sa1100dog_ioctl,
	.open		= sa1100dog_open,
	.release	= sa1100dog_release,
};

static struct miscdevice sa1100dog_miscdev = {
	.minor		= WATCHDOG_MINOR,
	.name		= "watchdog",
	.fops		= &sa1100dog_fops,
};

static int margin __initdata = 60;		/* (secs) Default is 1 minute */

static int __init sa1100dog_init(void)
{
	int ret;

	oscr_freq = get_clock_tick_rate();

	/*
	 * Read the reset status, and save it for later.  If
	 * we suspend, RCSR will be cleared, and the watchdog
	 * reset reason will be lost.
	 */
	boot_status = (reset_status & RESET_STATUS_WATCHDOG) ?
				WDIOF_CARDRESET : 0;
	pre_margin = oscr_freq * margin;

	ret = misc_register(&sa1100dog_miscdev);
	if (ret == 0)
		printk(KERN_INFO
			"SA1100/PXA2xx Watchdog Timer: timer margin %d sec\n",
						margin);
	return ret;
}

static void __exit sa1100dog_exit(void)
{
	misc_deregister(&sa1100dog_miscdev);
}

module_init(sa1100dog_init);
module_exit(sa1100dog_exit);

MODULE_AUTHOR("Oleg Drokin <green@crimea.edu>");
MODULE_DESCRIPTION("SA1100/PXA2xx Watchdog");

module_param(margin, int, 0);
MODULE_PARM_DESC(margin, "Watchdog margin in seconds (default 60s)");

MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);
