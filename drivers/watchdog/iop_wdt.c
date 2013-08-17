/*
 * drivers/char/watchdog/iop_wdt.c
 *
 * WDT driver for Intel I/O Processors
 * Copyright (C) 2005, Intel Corporation.
 *
 * Based on ixp4xx driver, Copyright 2004 (c) MontaVista, Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 *
 *	Curt E Bruns <curt.e.bruns@intel.com>
 *	Peter Milne <peter.milne@d-tacq.com>
 *	Dan Williams <dan.j.williams@intel.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/uaccess.h>
#include <mach/hardware.h>

static bool nowayout = WATCHDOG_NOWAYOUT;
static unsigned long wdt_status;
static unsigned long boot_status;
static DEFINE_SPINLOCK(wdt_lock);

#define WDT_IN_USE		0
#define WDT_OK_TO_CLOSE		1
#define WDT_ENABLED		2

static unsigned long iop_watchdog_timeout(void)
{
	return (0xffffffffUL / get_iop_tick_rate());
}

/**
 * wdt_supports_disable - determine if we are accessing a iop13xx watchdog
 * or iop3xx by whether it has a disable command
 */
static int wdt_supports_disable(void)
{
	int can_disable;

	if (IOP_WDTCR_EN_ARM != IOP_WDTCR_DIS_ARM)
		can_disable = 1;
	else
		can_disable = 0;

	return can_disable;
}

static void wdt_enable(void)
{
	/* Arm and enable the Timer to starting counting down from 0xFFFF.FFFF
	 * Takes approx. 10.7s to timeout
	 */
	spin_lock(&wdt_lock);
	write_wdtcr(IOP_WDTCR_EN_ARM);
	write_wdtcr(IOP_WDTCR_EN);
	spin_unlock(&wdt_lock);
}

/* returns 0 if the timer was successfully disabled */
static int wdt_disable(void)
{
	/* Stop Counting */
	if (wdt_supports_disable()) {
		spin_lock(&wdt_lock);
		write_wdtcr(IOP_WDTCR_DIS_ARM);
		write_wdtcr(IOP_WDTCR_DIS);
		clear_bit(WDT_ENABLED, &wdt_status);
		spin_unlock(&wdt_lock);
		pr_info("Disabled\n");
		return 0;
	} else
		return 1;
}

static int iop_wdt_open(struct inode *inode, struct file *file)
{
	if (test_and_set_bit(WDT_IN_USE, &wdt_status))
		return -EBUSY;

	clear_bit(WDT_OK_TO_CLOSE, &wdt_status);
	wdt_enable();
	set_bit(WDT_ENABLED, &wdt_status);
	return nonseekable_open(inode, file);
}

static ssize_t iop_wdt_write(struct file *file, const char *data, size_t len,
		  loff_t *ppos)
{
	if (len) {
		if (!nowayout) {
			size_t i;

			clear_bit(WDT_OK_TO_CLOSE, &wdt_status);

			for (i = 0; i != len; i++) {
				char c;

				if (get_user(c, data + i))
					return -EFAULT;
				if (c == 'V')
					set_bit(WDT_OK_TO_CLOSE, &wdt_status);
			}
		}
		wdt_enable();
	}
	return len;
}

static const struct watchdog_info ident = {
	.options = WDIOF_CARDRESET | WDIOF_MAGICCLOSE | WDIOF_KEEPALIVEPING,
	.identity = "iop watchdog",
};

static long iop_wdt_ioctl(struct file *file,
				unsigned int cmd, unsigned long arg)
{
	int options;
	int ret = -ENOTTY;
	int __user *argp = (int __user *)arg;

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		if (copy_to_user(argp, &ident, sizeof(ident)))
			ret = -EFAULT;
		else
			ret = 0;
		break;

	case WDIOC_GETSTATUS:
		ret = put_user(0, argp);
		break;

	case WDIOC_GETBOOTSTATUS:
		ret = put_user(boot_status, argp);
		break;

	case WDIOC_SETOPTIONS:
		if (get_user(options, (int *)arg))
			return -EFAULT;

		if (options & WDIOS_DISABLECARD) {
			if (!nowayout) {
				if (wdt_disable() == 0) {
					set_bit(WDT_OK_TO_CLOSE, &wdt_status);
					ret = 0;
				} else
					ret = -ENXIO;
			} else
				ret = 0;
		}
		if (options & WDIOS_ENABLECARD) {
			wdt_enable();
			ret = 0;
		}
		break;

	case WDIOC_KEEPALIVE:
		wdt_enable();
		ret = 0;
		break;

	case WDIOC_GETTIMEOUT:
		ret = put_user(iop_watchdog_timeout(), argp);
		break;
	}
	return ret;
}

static int iop_wdt_release(struct inode *inode, struct file *file)
{
	int state = 1;
	if (test_bit(WDT_OK_TO_CLOSE, &wdt_status))
		if (test_bit(WDT_ENABLED, &wdt_status))
			state = wdt_disable();

	/* if the timer is not disabled reload and notify that we are still
	 * going down
	 */
	if (state != 0) {
		wdt_enable();
		pr_crit("Device closed unexpectedly - reset in %lu seconds\n",
			iop_watchdog_timeout());
	}

	clear_bit(WDT_IN_USE, &wdt_status);
	clear_bit(WDT_OK_TO_CLOSE, &wdt_status);

	return 0;
}

static const struct file_operations iop_wdt_fops = {
	.owner = THIS_MODULE,
	.llseek = no_llseek,
	.write = iop_wdt_write,
	.unlocked_ioctl = iop_wdt_ioctl,
	.open = iop_wdt_open,
	.release = iop_wdt_release,
};

static struct miscdevice iop_wdt_miscdev = {
	.minor = WATCHDOG_MINOR,
	.name = "watchdog",
	.fops = &iop_wdt_fops,
};

static int __init iop_wdt_init(void)
{
	int ret;

	/* check if the reset was caused by the watchdog timer */
	boot_status = (read_rcsr() & IOP_RCSR_WDT) ? WDIOF_CARDRESET : 0;

	/* Configure Watchdog Timeout to cause an Internal Bus (IB) Reset
	 * NOTE: An IB Reset will Reset both cores in the IOP342
	 */
	write_wdtsr(IOP13XX_WDTCR_IB_RESET);

	/* Register after we have the device set up so we cannot race
	   with an open */
	ret = misc_register(&iop_wdt_miscdev);
	if (ret == 0)
		pr_info("timeout %lu sec\n", iop_watchdog_timeout());

	return ret;
}

static void __exit iop_wdt_exit(void)
{
	misc_deregister(&iop_wdt_miscdev);
}

module_init(iop_wdt_init);
module_exit(iop_wdt_exit);

module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started");

MODULE_AUTHOR("Curt E Bruns <curt.e.bruns@intel.com>");
MODULE_DESCRIPTION("iop watchdog timer driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);
