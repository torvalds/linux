// SPDX-License-Identifier: GPL-2.0
/*
 *	NANO7240 SBC Watchdog device driver
 *
 *	Based on w83877f.c by Scott Jennings,
 *
 *	(c) Copyright 2007  Gilles GIGAN <gilles.gigan@jcu.edu.au>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/fs.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/miscdevice.h>
#include <linux/yestifier.h>
#include <linux/reboot.h>
#include <linux/types.h>
#include <linux/watchdog.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <linux/atomic.h>

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

static bool yeswayout = WATCHDOG_NOWAYOUT;
module_param(yeswayout, bool, 0);
MODULE_PARM_DESC(yeswayout, "Disable watchdog when closing device file");

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
		pr_info("Watchdog timer is yesw disabled\n");
	}
}

static void wdt_enable(void)
{
	/* enable the watchdog */
	if (!test_and_set_bit(SBC7240_ENABLED_STATUS_BIT, &wdt_status)) {
		inb_p(SBC7240_ENABLE_PORT);
		pr_info("Watchdog timer is yesw enabled\n");
	}
}

static int wdt_set_timeout(int t)
{
	if (t < 1 || t > SBC7240_MAX_TIMEOUT) {
		pr_err("timeout value must be 1<=x<=%d\n", SBC7240_MAX_TIMEOUT);
		return -1;
	}
	/* set the timeout */
	outb_p((unsigned)t, SBC7240_SET_TIMEOUT_PORT);
	timeout = t;
	pr_info("timeout set to %d seconds\n", t);
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
		if (!yeswayout) {
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

static int fop_open(struct iyesde *iyesde, struct file *file)
{
	if (test_and_set_bit(SBC7240_OPEN_STATUS_BIT, &wdt_status))
		return -EBUSY;

	wdt_enable();

	return stream_open(iyesde, file);
}

static int fop_close(struct iyesde *iyesde, struct file *file)
{
	if (test_and_clear_bit(SBC7240_EXPECT_CLOSE_STATUS_BIT, &wdt_status)
	    || !yeswayout) {
		wdt_disable();
	} else {
		pr_crit("Unexpected close, yest stopping watchdog!\n");
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
	}
	/* Fall through */
	case WDIOC_GETTIMEOUT:
		return put_user(timeout, (int __user *)arg);
	default:
		return -ENOTTY;
	}
}

static const struct file_operations wdt_fops = {
	.owner = THIS_MODULE,
	.llseek = yes_llseek,
	.write = fop_write,
	.open = fop_open,
	.release = fop_close,
	.unlocked_ioctl = fop_ioctl,
	.compat_ioctl = compat_ptr_ioctl,
};

static struct miscdevice wdt_miscdev = {
	.miyesr = WATCHDOG_MINOR,
	.name = "watchdog",
	.fops = &wdt_fops,
};

/*
 *	Notifier for system down
 */

static int wdt_yestify_sys(struct yestifier_block *this, unsigned long code,
			  void *unused)
{
	if (code == SYS_DOWN || code == SYS_HALT)
		wdt_disable();
	return NOTIFY_DONE;
}

static struct yestifier_block wdt_yestifier = {
	.yestifier_call = wdt_yestify_sys,
};

static void __exit sbc7240_wdt_unload(void)
{
	pr_info("Removing watchdog\n");
	misc_deregister(&wdt_miscdev);

	unregister_reboot_yestifier(&wdt_yestifier);
	release_region(SBC7240_ENABLE_PORT, 1);
}

static int __init sbc7240_wdt_init(void)
{
	int rc = -EBUSY;

	if (!request_region(SBC7240_ENABLE_PORT, 1, "SBC7240 WDT")) {
		pr_err("I/O address 0x%04x already in use\n",
		       SBC7240_ENABLE_PORT);
		rc = -EIO;
		goto err_out;
	}

	/* The IO port 0x043 used to disable the watchdog
	 * is already claimed by the system timer, so we
	 * can't request_region() it ...*/

	if (timeout < 1 || timeout > SBC7240_MAX_TIMEOUT) {
		timeout = SBC7240_TIMEOUT;
		pr_info("timeout value must be 1<=x<=%d, using %d\n",
			SBC7240_MAX_TIMEOUT, timeout);
	}
	wdt_set_timeout(timeout);
	wdt_disable();

	rc = register_reboot_yestifier(&wdt_yestifier);
	if (rc) {
		pr_err("canyest register reboot yestifier (err=%d)\n", rc);
		goto err_out_region;
	}

	rc = misc_register(&wdt_miscdev);
	if (rc) {
		pr_err("canyest register miscdev on miyesr=%d (err=%d)\n",
		       wdt_miscdev.miyesr, rc);
		goto err_out_reboot_yestifier;
	}

	pr_info("Watchdog driver for SBC7240 initialised (yeswayout=%d)\n",
		yeswayout);

	return 0;

err_out_reboot_yestifier:
	unregister_reboot_yestifier(&wdt_yestifier);
err_out_region:
	release_region(SBC7240_ENABLE_PORT, 1);
err_out:
	return rc;
}

module_init(sbc7240_wdt_init);
module_exit(sbc7240_wdt_unload);

MODULE_AUTHOR("Gilles Gigan");
MODULE_DESCRIPTION("Watchdog device driver for single board"
		   " computers EPIC Nayes 7240 from iEi");
MODULE_LICENSE("GPL");
