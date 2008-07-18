/*
 *	NANO7240 SBC Watchdog device driver
 *
 *	Based on w83877f.c by Scott Jennings,
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License version 2 as
 *	published by the Free Software Foundation;
 *
 *	Software distributed under the License is distributed on an "AS IS"
 *	basis, WITHOUT WARRANTY OF ANY KIND, either express or
 *	implied. See the License for the specific language governing
 *	rights and limitations under the License.
 *
 *	(c) Copyright 2007  Gilles GIGAN <gilles.gigan@jcu.edu.au>
 *
 */

#include <linux/fs.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/miscdevice.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/types.h>
#include <linux/watchdog.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <asm/atomic.h>
#include <asm/system.h>

#define SBC7240_PREFIX "sbc7240_wdt: "

#define SBC7240_ENABLE_PORT		0x443
#define SBC7240_DISABLE_PORT		0x043
#define SBC7240_SET_TIMEOUT_PORT	SBC7240_ENABLE_PORT
#define SBC7240_MAGIC_CHAR		'V'

#define SBC7240_TIMEOUT		30
#define SBC7240_MAX_TIMEOUT		255
static int timeout = SBC7240_TIMEOUT;	/* in seconds */
module_param(timeout, int, 0);
MODULE_PARM_DESC(timeout, "Watchdog timeout in seconds. (1<=timeout<="
		 __MODULE_STRING(SBC7240_MAX_TIMEOUT) ", default="
		 __MODULE_STRING(SBC7240_TIMEOUT) ")");

static int nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, int, 0);
MODULE_PARM_DESC(nowayout, "Disable watchdog when closing device file");

#define SBC7240_OPEN_STATUS_BIT		0
#define SBC7240_ENABLED_STATUS_BIT	1
#define SBC7240_EXPECT_CLOSE_STATUS_BIT	2
static unsigned long wdt_status;

/*
 * Utility routines
 */

static void wdt_disable(void)
{
	/* disable the watchdog */
	if (test_and_clear_bit(SBC7240_ENABLED_STATUS_BIT, &wdt_status)) {
		inb_p(SBC7240_DISABLE_PORT);
		printk(KERN_INFO SBC7240_PREFIX
		       "Watchdog timer is now disabled.\n");
	}
}

static void wdt_enable(void)
{
	/* enable the watchdog */
	if (!test_and_set_bit(SBC7240_ENABLED_STATUS_BIT, &wdt_status)) {
		inb_p(SBC7240_ENABLE_PORT);
		printk(KERN_INFO SBC7240_PREFIX
		       "Watchdog timer is now enabled.\n");
	}
}

static int wdt_set_timeout(int t)
{
	if (t < 1 || t > SBC7240_MAX_TIMEOUT) {
		printk(KERN_ERR SBC7240_PREFIX
		       "timeout value must be 1<=x<=%d\n",
		       SBC7240_MAX_TIMEOUT);
		return -1;
	}
	/* set the timeout */
	outb_p((unsigned)t, SBC7240_SET_TIMEOUT_PORT);
	timeout = t;
	printk(KERN_INFO SBC7240_PREFIX "timeout set to %d seconds\n", t);
	return 0;
}

/* Whack the dog */
static inline void wdt_keepalive(void)
{
	if (test_bit(SBC7240_ENABLED_STATUS_BIT, &wdt_status))
		inb_p(SBC7240_ENABLE_PORT);
}

/*
 * /dev/watchdog handling
 */
static ssize_t fop_write(struct file *file, const char __user *buf,
			 size_t count, loff_t *ppos)
{
	size_t i;
	char c;

	if (count) {
		if (!nowayout) {
			clear_bit(SBC7240_EXPECT_CLOSE_STATUS_BIT,
				&wdt_status);

			/* is there a magic char ? */
			for (i = 0; i != count; i++) {
				if (get_user(c, buf + i))
					return -EFAULT;
				if (c == SBC7240_MAGIC_CHAR) {
					set_bit(SBC7240_EXPECT_CLOSE_STATUS_BIT,
						&wdt_status);
					break;
				}
			}
		}

		wdt_keepalive();
	}

	return count;
}

static int fop_open(struct inode *inode, struct file *file)
{
	if (test_and_set_bit(SBC7240_OPEN_STATUS_BIT, &wdt_status))
		return -EBUSY;

	wdt_enable();

	return nonseekable_open(inode, file);
}

static int fop_close(struct inode *inode, struct file *file)
{
	if (test_and_clear_bit(SBC7240_EXPECT_CLOSE_STATUS_BIT, &wdt_status)
	    || !nowayout) {
		wdt_disable();
	} else {
		printk(KERN_CRIT SBC7240_PREFIX
		       "Unexpected close, not stopping watchdog!\n");
		wdt_keepalive();
	}

	clear_bit(SBC7240_OPEN_STATUS_BIT, &wdt_status);
	return 0;
}

static const struct watchdog_info ident = {
	.options = WDIOF_KEEPALIVEPING|
		   WDIOF_SETTIMEOUT|
		   WDIOF_MAGICCLOSE,
	.firmware_version = 1,
	.identity = "SBC7240",
};


static long fop_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case WDIOC_GETSUPPORT:
		return copy_to_user((void __user *)arg, &ident, sizeof(ident))
						 ? -EFAULT : 0;
	case WDIOC_GETSTATUS:
	case WDIOC_GETBOOTSTATUS:
		return put_user(0, (int __user *)arg);
	case WDIOC_SETOPTIONS:
	{
		int options;
		int retval = -EINVAL;

		if (get_user(options, (int __user *)arg))
			return -EFAULT;

		if (options & WDIOS_DISABLECARD) {
			wdt_disable();
			retval = 0;
		}

		if (options & WDIOS_ENABLECARD) {
			wdt_enable();
			retval = 0;
		}

		return retval;
	}
	case WDIOC_KEEPALIVE:
		wdt_keepalive();
		return 0;
	case WDIOC_SETTIMEOUT:
	{
		int new_timeout;

		if (get_user(new_timeout, (int __user *)arg))
			return -EFAULT;

		if (wdt_set_timeout(new_timeout))
			return -EINVAL;

		/* Fall through */
	}
	case WDIOC_GETTIMEOUT:
		return put_user(timeout, (int __user *)arg);
	default:
		return -ENOTTY;
	}
}

static const struct file_operations wdt_fops = {
	.owner = THIS_MODULE,
	.llseek = no_llseek,
	.write = fop_write,
	.open = fop_open,
	.release = fop_close,
	.unlocked_ioctl = fop_ioctl,
};

static struct miscdevice wdt_miscdev = {
	.minor = WATCHDOG_MINOR,
	.name = "watchdog",
	.fops = &wdt_fops,
};

/*
 *	Notifier for system down
 */

static int wdt_notify_sys(struct notifier_block *this, unsigned long code,
			  void *unused)
{
	if (code == SYS_DOWN || code == SYS_HALT)
		wdt_disable();
	return NOTIFY_DONE;
}

static struct notifier_block wdt_notifier = {
	.notifier_call = wdt_notify_sys,
};

static void __exit sbc7240_wdt_unload(void)
{
	printk(KERN_INFO SBC7240_PREFIX "Removing watchdog\n");
	misc_deregister(&wdt_miscdev);

	unregister_reboot_notifier(&wdt_notifier);
	release_region(SBC7240_ENABLE_PORT, 1);
}

static int __init sbc7240_wdt_init(void)
{
	int rc = -EBUSY;

	if (!request_region(SBC7240_ENABLE_PORT, 1, "SBC7240 WDT")) {
		printk(KERN_ERR SBC7240_PREFIX
		       "I/O address 0x%04x already in use\n",
		       SBC7240_ENABLE_PORT);
		rc = -EIO;
		goto err_out;
	}

	/* The IO port 0x043 used to disable the watchdog
	 * is already claimed by the system timer, so we
	 * cant request_region() it ...*/

	if (timeout < 1 || timeout > SBC7240_MAX_TIMEOUT) {
		timeout = SBC7240_TIMEOUT;
		printk(KERN_INFO SBC7240_PREFIX
		       "timeout value must be 1<=x<=%d, using %d\n",
		       SBC7240_MAX_TIMEOUT, timeout);
	}
	wdt_set_timeout(timeout);
	wdt_disable();

	rc = register_reboot_notifier(&wdt_notifier);
	if (rc) {
		printk(KERN_ERR SBC7240_PREFIX
		       "cannot register reboot notifier (err=%d)\n", rc);
		goto err_out_region;
	}

	rc = misc_register(&wdt_miscdev);
	if (rc) {
		printk(KERN_ERR SBC7240_PREFIX
		       "cannot register miscdev on minor=%d (err=%d)\n",
		       wdt_miscdev.minor, rc);
		goto err_out_reboot_notifier;
	}

	printk(KERN_INFO SBC7240_PREFIX
	       "Watchdog driver for SBC7240 initialised (nowayout=%d)\n",
	       nowayout);

	return 0;

err_out_reboot_notifier:
	unregister_reboot_notifier(&wdt_notifier);
err_out_region:
	release_region(SBC7240_ENABLE_PORT, 1);
err_out:
	return rc;
}

module_init(sbc7240_wdt_init);
module_exit(sbc7240_wdt_unload);

MODULE_AUTHOR("Gilles Gigan");
MODULE_DESCRIPTION("Watchdog device driver for single board"
		   " computers EPIC Nano 7240 from iEi");
MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);

