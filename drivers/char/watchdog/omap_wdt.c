/*
 * linux/drivers/char/watchdog/omap_wdt.c
 *
 * Watchdog driver for the TI OMAP 16xx & 24xx 32KHz (non-secure) watchdog
 *
 * Author: MontaVista Software, Inc.
 *	 <gdavis@mvista.com> or <source@mvista.com>
 *
 * 2003 (c) MontaVista Software, Inc. This file is licensed under the
 * terms of the GNU General Public License version 2. This program is
 * licensed "as is" without any warranty of any kind, whether express
 * or implied.
 *
 * History:
 *
 * 20030527: George G. Davis <gdavis@mvista.com>
 *	Initially based on linux-2.4.19-rmk7-pxa1/drivers/char/sa1100_wdt.c
 *	(c) Copyright 2000 Oleg Drokin <green@crimea.edu>
 *	Based on SoftDog driver by Alan Cox <alan@redhat.com>
 *
 * Copyright (c) 2004 Texas Instruments.
 *	1. Modified to support OMAP1610 32-KHz watchdog timer
 *	2. Ported to 2.6 kernel
 *
 * Copyright (c) 2005 David Brownell
 *	Use the driver model and standard identifiers; handle bigger timeouts.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/moduleparam.h>
#include <linux/clk.h>

#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/hardware.h>
#include <asm/bitops.h>

#include <asm/arch/prcm.h>

#include "omap_wdt.h"

static unsigned timer_margin;
module_param(timer_margin, uint, 0);
MODULE_PARM_DESC(timer_margin, "initial watchdog timeout (in seconds)");

static int omap_wdt_users;
static struct clk *armwdt_ck = NULL;
static struct clk *mpu_wdt_ick = NULL;
static struct clk *mpu_wdt_fck = NULL;

static unsigned int wdt_trgr_pattern = 0x1234;

static void omap_wdt_ping(void)
{
	/* wait for posted write to complete */
	while ((omap_readl(OMAP_WATCHDOG_WPS)) & 0x08)
		cpu_relax();
	wdt_trgr_pattern = ~wdt_trgr_pattern;
	omap_writel(wdt_trgr_pattern, (OMAP_WATCHDOG_TGR));
	/* wait for posted write to complete */
	while ((omap_readl(OMAP_WATCHDOG_WPS)) & 0x08)
		cpu_relax();
	/* reloaded WCRR from WLDR */
}

static void omap_wdt_enable(void)
{
	/* Sequence to enable the watchdog */
	omap_writel(0xBBBB, OMAP_WATCHDOG_SPR);
	while ((omap_readl(OMAP_WATCHDOG_WPS)) & 0x10)
		cpu_relax();
	omap_writel(0x4444, OMAP_WATCHDOG_SPR);
	while ((omap_readl(OMAP_WATCHDOG_WPS)) & 0x10)
		cpu_relax();
}

static void omap_wdt_disable(void)
{
	/* sequence required to disable watchdog */
	omap_writel(0xAAAA, OMAP_WATCHDOG_SPR);	/* TIMER_MODE */
	while (omap_readl(OMAP_WATCHDOG_WPS) & 0x10)
		cpu_relax();
	omap_writel(0x5555, OMAP_WATCHDOG_SPR);	/* TIMER_MODE */
	while (omap_readl(OMAP_WATCHDOG_WPS) & 0x10)
		cpu_relax();
}

static void omap_wdt_adjust_timeout(unsigned new_timeout)
{
	if (new_timeout < TIMER_MARGIN_MIN)
		new_timeout = TIMER_MARGIN_DEFAULT;
	if (new_timeout > TIMER_MARGIN_MAX)
		new_timeout = TIMER_MARGIN_MAX;
	timer_margin = new_timeout;
}

static void omap_wdt_set_timeout(void)
{
	u32 pre_margin = GET_WLDR_VAL(timer_margin);

	/* just count up at 32 KHz */
	while (omap_readl(OMAP_WATCHDOG_WPS) & 0x04)
		cpu_relax();
	omap_writel(pre_margin, OMAP_WATCHDOG_LDR);
	while (omap_readl(OMAP_WATCHDOG_WPS) & 0x04)
		cpu_relax();
}

/*
 *	Allow only one task to hold it open
 */

static int omap_wdt_open(struct inode *inode, struct file *file)
{
	if (test_and_set_bit(1, (unsigned long *)&omap_wdt_users))
		return -EBUSY;

	if (cpu_is_omap16xx())
		clk_enable(armwdt_ck);	/* Enable the clock */

	if (cpu_is_omap24xx()) {
		clk_enable(mpu_wdt_ick);    /* Enable the interface clock */
		clk_enable(mpu_wdt_fck);    /* Enable the functional clock */
	}

	/* initialize prescaler */
	while (omap_readl(OMAP_WATCHDOG_WPS) & 0x01)
		cpu_relax();
	omap_writel((1 << 5) | (PTV << 2), OMAP_WATCHDOG_CNTRL);
	while (omap_readl(OMAP_WATCHDOG_WPS) & 0x01)
		cpu_relax();

	omap_wdt_set_timeout();
	omap_wdt_enable();
	return 0;
}

static int omap_wdt_release(struct inode *inode, struct file *file)
{
	/*
	 *      Shut off the timer unless NOWAYOUT is defined.
	 */
#ifndef CONFIG_WATCHDOG_NOWAYOUT
	omap_wdt_disable();

	if (cpu_is_omap16xx()) {
		clk_disable(armwdt_ck);	/* Disable the clock */
		clk_put(armwdt_ck);
		armwdt_ck = NULL;
	}

	if (cpu_is_omap24xx()) {
		clk_disable(mpu_wdt_ick);	/* Disable the clock */
		clk_disable(mpu_wdt_fck);	/* Disable the clock */
		clk_put(mpu_wdt_ick);
		clk_put(mpu_wdt_fck);
		mpu_wdt_ick = NULL;
		mpu_wdt_fck = NULL;
	}
#else
	printk(KERN_CRIT "omap_wdt: Unexpected close, not stopping!\n");
#endif
	omap_wdt_users = 0;
	return 0;
}

static ssize_t
omap_wdt_write(struct file *file, const char __user *data,
		size_t len, loff_t *ppos)
{
	/* Refresh LOAD_TIME. */
	if (len)
		omap_wdt_ping();
	return len;
}

static int
omap_wdt_ioctl(struct inode *inode, struct file *file,
	unsigned int cmd, unsigned long arg)
{
	int new_margin;
	static struct watchdog_info ident = {
		.identity = "OMAP Watchdog",
		.options = WDIOF_SETTIMEOUT,
		.firmware_version = 0,
	};

	switch (cmd) {
	default:
		return -ENOIOCTLCMD;
	case WDIOC_GETSUPPORT:
		return copy_to_user((struct watchdog_info __user *)arg, &ident,
				sizeof(ident));
	case WDIOC_GETSTATUS:
		return put_user(0, (int __user *)arg);
	case WDIOC_GETBOOTSTATUS:
		if (cpu_is_omap16xx())
			return put_user(omap_readw(ARM_SYSST),
					(int __user *)arg);
		if (cpu_is_omap24xx())
			return put_user(omap_prcm_get_reset_sources(),
					(int __user *)arg);
	case WDIOC_KEEPALIVE:
		omap_wdt_ping();
		return 0;
	case WDIOC_SETTIMEOUT:
		if (get_user(new_margin, (int __user *)arg))
			return -EFAULT;
		omap_wdt_adjust_timeout(new_margin);

		omap_wdt_disable();
		omap_wdt_set_timeout();
		omap_wdt_enable();

		omap_wdt_ping();
		/* Fall */
	case WDIOC_GETTIMEOUT:
		return put_user(timer_margin, (int __user *)arg);
	}
}

static const struct file_operations omap_wdt_fops = {
	.owner = THIS_MODULE,
	.write = omap_wdt_write,
	.ioctl = omap_wdt_ioctl,
	.open = omap_wdt_open,
	.release = omap_wdt_release,
};

static struct miscdevice omap_wdt_miscdev = {
	.minor = WATCHDOG_MINOR,
	.name = "watchdog",
	.fops = &omap_wdt_fops
};

static int __init omap_wdt_probe(struct platform_device *pdev)
{
	struct resource *res, *mem;
	int ret;

	/* reserve static register mappings */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENOENT;

	mem = request_mem_region(res->start, res->end - res->start + 1,
				 pdev->name);
	if (mem == NULL)
		return -EBUSY;

	platform_set_drvdata(pdev, mem);

	omap_wdt_users = 0;

	if (cpu_is_omap16xx()) {
		armwdt_ck = clk_get(&pdev->dev, "armwdt_ck");
		if (IS_ERR(armwdt_ck)) {
			ret = PTR_ERR(armwdt_ck);
			armwdt_ck = NULL;
			goto fail;
		}
	}

	if (cpu_is_omap24xx()) {
		mpu_wdt_ick = clk_get(&pdev->dev, "mpu_wdt_ick");
		if (IS_ERR(mpu_wdt_ick)) {
			ret = PTR_ERR(mpu_wdt_ick);
			mpu_wdt_ick = NULL;
			goto fail;
		}
		mpu_wdt_fck = clk_get(&pdev->dev, "mpu_wdt_fck");
		if (IS_ERR(mpu_wdt_fck)) {
			ret = PTR_ERR(mpu_wdt_fck);
			mpu_wdt_fck = NULL;
			goto fail;
		}
	}

	omap_wdt_disable();
	omap_wdt_adjust_timeout(timer_margin);

	omap_wdt_miscdev.parent = &pdev->dev;
	ret = misc_register(&omap_wdt_miscdev);
	if (ret)
		goto fail;

	pr_info("OMAP Watchdog Timer: initial timeout %d sec\n", timer_margin);

	/* autogate OCP interface clock */
	omap_writel(0x01, OMAP_WATCHDOG_SYS_CONFIG);
	return 0;

fail:
	if (armwdt_ck)
		clk_put(armwdt_ck);
	if (mpu_wdt_ick)
		clk_put(mpu_wdt_ick);
	if (mpu_wdt_fck)
		clk_put(mpu_wdt_fck);
	release_resource(mem);
	return ret;
}

static void omap_wdt_shutdown(struct platform_device *pdev)
{
	omap_wdt_disable();
}

static int omap_wdt_remove(struct platform_device *pdev)
{
	struct resource *mem = platform_get_drvdata(pdev);
	misc_deregister(&omap_wdt_miscdev);
	release_resource(mem);
	if (armwdt_ck)
		clk_put(armwdt_ck);
	if (mpu_wdt_ick)
		clk_put(mpu_wdt_ick);
	if (mpu_wdt_fck)
		clk_put(mpu_wdt_fck);
	return 0;
}

#ifdef	CONFIG_PM

/* REVISIT ... not clear this is the best way to handle system suspend; and
 * it's very inappropriate for selective device suspend (e.g. suspending this
 * through sysfs rather than by stopping the watchdog daemon).  Also, this
 * may not play well enough with NOWAYOUT...
 */

static int omap_wdt_suspend(struct platform_device *pdev, pm_message_t state)
{
	if (omap_wdt_users)
		omap_wdt_disable();
	return 0;
}

static int omap_wdt_resume(struct platform_device *pdev)
{
	if (omap_wdt_users) {
		omap_wdt_enable();
		omap_wdt_ping();
	}
	return 0;
}

#else
#define	omap_wdt_suspend	NULL
#define	omap_wdt_resume		NULL
#endif

static struct platform_driver omap_wdt_driver = {
	.probe		= omap_wdt_probe,
	.remove		= omap_wdt_remove,
	.shutdown	= omap_wdt_shutdown,
	.suspend	= omap_wdt_suspend,
	.resume		= omap_wdt_resume,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "omap_wdt",
	},
};

static int __init omap_wdt_init(void)
{
	return platform_driver_register(&omap_wdt_driver);
}

static void __exit omap_wdt_exit(void)
{
	platform_driver_unregister(&omap_wdt_driver);
}

module_init(omap_wdt_init);
module_exit(omap_wdt_exit);

MODULE_AUTHOR("George G. Davis");
MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);
