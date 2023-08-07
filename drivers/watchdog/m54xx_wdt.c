// SPDX-License-Identifier: GPL-2.0-only
/*
 * drivers/watchdog/m54xx_wdt.c
 *
 * Watchdog driver for ColdFire MCF547x & MCF548x processors
 * Copyright 2010 (c) Philippe De Muyter <phdm@macqel.be>
 *
 * Adapted from the IXP4xx watchdog driver, which carries these notices:
 *
 *  Author: Deepak Saxena <dsaxena@plexity.net>
 *
 *  Copyright 2004 (c) MontaVista, Software, Inc.
 *  Based on sa1100 driver, Copyright (C) 2000 Oleg Drokin <green@crimea.edu>
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/init.h>
#include <linux/bitops.h>
#include <linux/ioport.h>
#include <linux/uaccess.h>
#include <linux/io.h>

#include <asm/coldfire.h>
#include <asm/m54xxsim.h>
#include <asm/m54xxgpt.h>

static bool nowayout = WATCHDOG_NOWAYOUT;
static unsigned int heartbeat = 30;	/* (secs) Default is 0.5 minute */
static unsigned long wdt_status;

#define	WDT_IN_USE		0
#define	WDT_OK_TO_CLOSE		1

static void wdt_enable(void)
{
	unsigned int gms0;

	/* preserve GPIO usage, if any */
	gms0 = __raw_readl(MCF_GPT_GMS0);
	if (gms0 & MCF_GPT_GMS_TMS_GPIO)
		gms0 &= (MCF_GPT_GMS_TMS_GPIO | MCF_GPT_GMS_GPIO_MASK
							| MCF_GPT_GMS_OD);
	else
		gms0 = MCF_GPT_GMS_TMS_GPIO | MCF_GPT_GMS_OD;
	__raw_writel(gms0, MCF_GPT_GMS0);
	__raw_writel(MCF_GPT_GCIR_PRE(heartbeat*(MCF_BUSCLK/0xffff)) |
			MCF_GPT_GCIR_CNT(0xffff), MCF_GPT_GCIR0);
	gms0 |= MCF_GPT_GMS_OCPW(0xA5) | MCF_GPT_GMS_WDEN | MCF_GPT_GMS_CE;
	__raw_writel(gms0, MCF_GPT_GMS0);
}

static void wdt_disable(void)
{
	unsigned int gms0;

	/* disable watchdog */
	gms0 = __raw_readl(MCF_GPT_GMS0);
	gms0 &= ~(MCF_GPT_GMS_WDEN | MCF_GPT_GMS_CE);
	__raw_writel(gms0, MCF_GPT_GMS0);
}

static void wdt_keepalive(void)
{
	unsigned int gms0;

	gms0 = __raw_readl(MCF_GPT_GMS0);
	gms0 |= MCF_GPT_GMS_OCPW(0xA5);
	__raw_writel(gms0, MCF_GPT_GMS0);
}

static int m54xx_wdt_open(struct inode *inode, struct file *file)
{
	if (test_and_set_bit(WDT_IN_USE, &wdt_status))
		return -EBUSY;

	clear_bit(WDT_OK_TO_CLOSE, &wdt_status);
	wdt_enable();
	return stream_open(inode, file);
}

static ssize_t m54xx_wdt_write(struct file *file, const char *data,
						size_t len, loff_t *ppos)
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
		wdt_keepalive();
	}
	return len;
}

static const struct watchdog_info ident = {
	.options	= WDIOF_MAGICCLOSE | WDIOF_SETTIMEOUT |
				WDIOF_KEEPALIVEPING,
	.identity	= "Coldfire M54xx Watchdog",
};

static long m54xx_wdt_ioctl(struct file *file, unsigned int cmd,
							 unsigned long arg)
{
	int ret = -ENOTTY;
	int time;

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		ret = copy_to_user((struct watchdog_info *)arg, &ident,
				   sizeof(ident)) ? -EFAULT : 0;
		break;

	case WDIOC_GETSTATUS:
		ret = put_user(0, (int *)arg);
		break;

	case WDIOC_GETBOOTSTATUS:
		ret = put_user(0, (int *)arg);
		break;

	case WDIOC_KEEPALIVE:
		wdt_keepalive();
		ret = 0;
		break;

	case WDIOC_SETTIMEOUT:
		ret = get_user(time, (int *)arg);
		if (ret)
			break;

		if (time <= 0 || time > 30) {
			ret = -EINVAL;
			break;
		}

		heartbeat = time;
		wdt_enable();
		fallthrough;

	case WDIOC_GETTIMEOUT:
		ret = put_user(heartbeat, (int *)arg);
		break;
	}
	return ret;
}

static int m54xx_wdt_release(struct inode *inode, struct file *file)
{
	if (test_bit(WDT_OK_TO_CLOSE, &wdt_status))
		wdt_disable();
	else {
		pr_crit("Device closed unexpectedly - timer will not stop\n");
		wdt_keepalive();
	}
	clear_bit(WDT_IN_USE, &wdt_status);
	clear_bit(WDT_OK_TO_CLOSE, &wdt_status);

	return 0;
}


static const struct file_operations m54xx_wdt_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.write		= m54xx_wdt_write,
	.unlocked_ioctl	= m54xx_wdt_ioctl,
	.compat_ioctl	= compat_ptr_ioctl,
	.open		= m54xx_wdt_open,
	.release	= m54xx_wdt_release,
};

static struct miscdevice m54xx_wdt_miscdev = {
	.minor		= WATCHDOG_MINOR,
	.name		= "watchdog",
	.fops		= &m54xx_wdt_fops,
};

static int __init m54xx_wdt_init(void)
{
	if (!request_mem_region(MCF_GPT_GCIR0, 4, "Coldfire M54xx Watchdog")) {
		pr_warn("I/O region busy\n");
		return -EBUSY;
	}
	pr_info("driver is loaded\n");

	return misc_register(&m54xx_wdt_miscdev);
}

static void __exit m54xx_wdt_exit(void)
{
	misc_deregister(&m54xx_wdt_miscdev);
	release_mem_region(MCF_GPT_GCIR0, 4);
}

module_init(m54xx_wdt_init);
module_exit(m54xx_wdt_exit);

MODULE_AUTHOR("Philippe De Muyter <phdm@macqel.be>");
MODULE_DESCRIPTION("Coldfire M54xx Watchdog");

module_param(heartbeat, int, 0);
MODULE_PARM_DESC(heartbeat, "Watchdog heartbeat in seconds (default 30s)");

module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started");

MODULE_LICENSE("GPL");
