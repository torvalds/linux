/*
 * omap_wdt.c
 *
 * Watchdog driver for the TI OMAP 16xx & 24xx/34xx 32KHz (non-secure) watchdog
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
#include <linux/bitops.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <mach/hardware.h>
#include <mach/prcm.h>

#include "omap_wdt.h"

static struct platform_device *omap_wdt_dev;

static unsigned timer_margin;
module_param(timer_margin, uint, 0);
MODULE_PARM_DESC(timer_margin, "initial watchdog timeout (in seconds)");

static unsigned int wdt_trgr_pattern = 0x1234;
static spinlock_t wdt_lock;

struct omap_wdt_dev {
	void __iomem    *base;          /* physical */
	struct device   *dev;
	int             omap_wdt_users;
	struct clk      *armwdt_ck;
	struct clk      *mpu_wdt_ick;
	struct clk      *mpu_wdt_fck;
	struct resource *mem;
	struct miscdevice omap_wdt_miscdev;
};

static void omap_wdt_ping(struct omap_wdt_dev *wdev)
{
	void __iomem    *base = wdev->base;
	/* wait for posted write to complete */
	while ((omap_readl(base + OMAP_WATCHDOG_WPS)) & 0x08)
		cpu_relax();
	wdt_trgr_pattern = ~wdt_trgr_pattern;
	omap_writel(wdt_trgr_pattern, (base + OMAP_WATCHDOG_TGR));
	/* wait for posted write to complete */
	while ((omap_readl(base + OMAP_WATCHDOG_WPS)) & 0x08)
		cpu_relax();
	/* reloaded WCRR from WLDR */
}

static void omap_wdt_enable(struct omap_wdt_dev *wdev)
{
	void __iomem *base;
	base = wdev->base;
	/* Sequence to enable the watchdog */
	omap_writel(0xBBBB, base + OMAP_WATCHDOG_SPR);
	while ((omap_readl(base + OMAP_WATCHDOG_WPS)) & 0x10)
		cpu_relax();
	omap_writel(0x4444, base + OMAP_WATCHDOG_SPR);
	while ((omap_readl(base + OMAP_WATCHDOG_WPS)) & 0x10)
		cpu_relax();
}

static void omap_wdt_disable(struct omap_wdt_dev *wdev)
{
	void __iomem *base;
	base = wdev->base;
	/* sequence required to disable watchdog */
	omap_writel(0xAAAA, base + OMAP_WATCHDOG_SPR);	/* TIMER_MODE */
	while (omap_readl(base + OMAP_WATCHDOG_WPS) & 0x10)
		cpu_relax();
	omap_writel(0x5555, base + OMAP_WATCHDOG_SPR);	/* TIMER_MODE */
	while (omap_readl(base + OMAP_WATCHDOG_WPS) & 0x10)
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

static void omap_wdt_set_timeout(struct omap_wdt_dev *wdev)
{
	u32 pre_margin = GET_WLDR_VAL(timer_margin);
	void __iomem *base;
	base = wdev->base;

	/* just count up at 32 KHz */
	while (omap_readl(base + OMAP_WATCHDOG_WPS) & 0x04)
		cpu_relax();
	omap_writel(pre_margin, base + OMAP_WATCHDOG_LDR);
	while (omap_readl(base + OMAP_WATCHDOG_WPS) & 0x04)
		cpu_relax();
}

/*
 *	Allow only one task to hold it open
 */

static int omap_wdt_open(struct inode *inode, struct file *file)
{
	struct omap_wdt_dev *wdev;
	void __iomem *base;
	wdev = platform_get_drvdata(omap_wdt_dev);
	base = wdev->base;
	if (test_and_set_bit(1, (unsigned long *)&(wdev->omap_wdt_users)))
		return -EBUSY;

	if (cpu_is_omap16xx())
		clk_enable(wdev->armwdt_ck);	/* Enable the clock */

	if (cpu_is_omap24xx() || cpu_is_omap34xx()) {
		clk_enable(wdev->mpu_wdt_ick);    /* Enable the interface clock */
		clk_enable(wdev->mpu_wdt_fck);    /* Enable the functional clock */
	}

	/* initialize prescaler */
	while (omap_readl(base + OMAP_WATCHDOG_WPS) & 0x01)
		cpu_relax();
	omap_writel((1 << 5) | (PTV << 2), base + OMAP_WATCHDOG_CNTRL);
	while (omap_readl(base + OMAP_WATCHDOG_WPS) & 0x01)
		cpu_relax();

	file->private_data = (void *) wdev;

	omap_wdt_set_timeout(wdev);
	omap_wdt_enable(wdev);
	return nonseekable_open(inode, file);
}

static int omap_wdt_release(struct inode *inode, struct file *file)
{
	struct omap_wdt_dev *wdev;
	wdev = file->private_data;
	/*
	 *      Shut off the timer unless NOWAYOUT is defined.
	 */
#ifndef CONFIG_WATCHDOG_NOWAYOUT

	omap_wdt_disable(wdev);

	if (cpu_is_omap16xx())
		clk_disable(wdev->armwdt_ck);	/* Disable the clock */

	if (cpu_is_omap24xx() || cpu_is_omap34xx()) {
		clk_disable(wdev->mpu_wdt_ick);	/* Disable the clock */
		clk_disable(wdev->mpu_wdt_fck);	/* Disable the clock */
	}
#else
	printk(KERN_CRIT "omap_wdt: Unexpected close, not stopping!\n");
#endif
	wdev->omap_wdt_users = 0;
	return 0;
}

static ssize_t omap_wdt_write(struct file *file, const char __user *data,
		size_t len, loff_t *ppos)
{
	struct omap_wdt_dev *wdev;
	wdev = file->private_data;
	/* Refresh LOAD_TIME. */
	if (len) {
		spin_lock(&wdt_lock);
		omap_wdt_ping(wdev);
		spin_unlock(&wdt_lock);
	}
	return len;
}

static long omap_wdt_ioctl(struct file *file, unsigned int cmd,
						unsigned long arg)
{
	struct omap_wdt_dev *wdev;
	int new_margin;
	static const struct watchdog_info ident = {
		.identity = "OMAP Watchdog",
		.options = WDIOF_SETTIMEOUT,
		.firmware_version = 0,
	};
	wdev = file->private_data;

	switch (cmd) {
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
		spin_lock(&wdt_lock);
		omap_wdt_ping(wdev);
		spin_unlock(&wdt_lock);
		return 0;
	case WDIOC_SETTIMEOUT:
		if (get_user(new_margin, (int __user *)arg))
			return -EFAULT;
		omap_wdt_adjust_timeout(new_margin);

		spin_lock(&wdt_lock);
		omap_wdt_disable(wdev);
		omap_wdt_set_timeout(wdev);
		omap_wdt_enable(wdev);

		omap_wdt_ping(wdev);
		spin_unlock(&wdt_lock);
		/* Fall */
	case WDIOC_GETTIMEOUT:
		return put_user(timer_margin, (int __user *)arg);
	default:
		return -ENOTTY;
	}
}

static const struct file_operations omap_wdt_fops = {
	.owner = THIS_MODULE,
	.write = omap_wdt_write,
	.unlocked_ioctl = omap_wdt_ioctl,
	.open = omap_wdt_open,
	.release = omap_wdt_release,
};


static int __init omap_wdt_probe(struct platform_device *pdev)
{
	struct resource *res, *mem;
	int ret;
	struct omap_wdt_dev *wdev;

	/* reserve static register mappings */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENOENT;

	if (omap_wdt_dev)
		return -EBUSY;

	mem = request_mem_region(res->start, res->end - res->start + 1,
				 pdev->name);
	if (mem == NULL)
		return -EBUSY;

	wdev = kzalloc(sizeof(struct omap_wdt_dev), GFP_KERNEL);
	if (!wdev) {
		ret = -ENOMEM;
		goto fail;
	}
	wdev->omap_wdt_users = 0;
	wdev->mem = mem;

	if (cpu_is_omap16xx()) {
		wdev->armwdt_ck = clk_get(&pdev->dev, "armwdt_ck");
		if (IS_ERR(wdev->armwdt_ck)) {
			ret = PTR_ERR(wdev->armwdt_ck);
			wdev->armwdt_ck = NULL;
			goto fail;
		}
	}

	if (cpu_is_omap24xx()) {
		wdev->mpu_wdt_ick = clk_get(&pdev->dev, "mpu_wdt_ick");
		if (IS_ERR(wdev->mpu_wdt_ick)) {
			ret = PTR_ERR(wdev->mpu_wdt_ick);
			wdev->mpu_wdt_ick = NULL;
			goto fail;
		}
		wdev->mpu_wdt_fck = clk_get(&pdev->dev, "mpu_wdt_fck");
		if (IS_ERR(wdev->mpu_wdt_fck)) {
			ret = PTR_ERR(wdev->mpu_wdt_fck);
			wdev->mpu_wdt_fck = NULL;
			goto fail;
		}
	}

	if (cpu_is_omap34xx()) {
		wdev->mpu_wdt_ick = clk_get(&pdev->dev, "wdt2_ick");
		if (IS_ERR(wdev->mpu_wdt_ick)) {
			ret = PTR_ERR(wdev->mpu_wdt_ick);
			wdev->mpu_wdt_ick = NULL;
			goto fail;
		}
		wdev->mpu_wdt_fck = clk_get(&pdev->dev, "wdt2_fck");
		if (IS_ERR(wdev->mpu_wdt_fck)) {
			ret = PTR_ERR(wdev->mpu_wdt_fck);
			wdev->mpu_wdt_fck = NULL;
			goto fail;
		}
	}
	wdev->base = (void __iomem *) (mem->start);
	platform_set_drvdata(pdev, wdev);

	omap_wdt_disable(wdev);
	omap_wdt_adjust_timeout(timer_margin);

	wdev->omap_wdt_miscdev.parent = &pdev->dev;
	wdev->omap_wdt_miscdev.minor = WATCHDOG_MINOR;
	wdev->omap_wdt_miscdev.name = "watchdog";
	wdev->omap_wdt_miscdev.fops = &omap_wdt_fops;

	ret = misc_register(&(wdev->omap_wdt_miscdev));
	if (ret)
		goto fail;

	pr_info("OMAP Watchdog Timer Rev 0x%02x: initial timeout %d sec\n",
		omap_readl(wdev->base + OMAP_WATCHDOG_REV) & 0xFF,
		timer_margin);

	/* autogate OCP interface clock */
	omap_writel(0x01, wdev->base + OMAP_WATCHDOG_SYS_CONFIG);

	omap_wdt_dev = pdev;

	return 0;

fail:
	if (wdev) {
		platform_set_drvdata(pdev, NULL);
		if (wdev->armwdt_ck)
			clk_put(wdev->armwdt_ck);
		if (wdev->mpu_wdt_ick)
			clk_put(wdev->mpu_wdt_ick);
		if (wdev->mpu_wdt_fck)
			clk_put(wdev->mpu_wdt_fck);
		kfree(wdev);
	}
	if (mem) {
		release_mem_region(res->start, res->end - res->start + 1);
	}
	return ret;
}

static void omap_wdt_shutdown(struct platform_device *pdev)
{
	struct omap_wdt_dev *wdev;
	wdev = platform_get_drvdata(pdev);

	if (wdev->omap_wdt_users)
		omap_wdt_disable(wdev);
}

static int omap_wdt_remove(struct platform_device *pdev)
{
	struct omap_wdt_dev *wdev;
	wdev = platform_get_drvdata(pdev);
	struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	if (!res)
		return -ENOENT;

	misc_deregister(&(wdev->omap_wdt_miscdev));
	release_mem_region(res->start, res->end - res->start + 1);
	platform_set_drvdata(pdev, NULL);
	if (wdev->armwdt_ck) {
		clk_put(wdev->armwdt_ck);
		wdev->armwdt_ck = NULL;
	}
	if (wdev->mpu_wdt_ick) {
		clk_put(wdev->mpu_wdt_ick);
		wdev->mpu_wdt_ick = NULL;
	}
	if (wdev->mpu_wdt_fck) {
		clk_put(wdev->mpu_wdt_fck);
		wdev->mpu_wdt_fck = NULL;
	}
	kfree(wdev);
	omap_wdt_dev = NULL;
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
	struct omap_wdt_dev *wdev;
	wdev = platform_get_drvdata(pdev);
	if (wdev->omap_wdt_users)
		omap_wdt_disable(wdev);
	return 0;
}

static int omap_wdt_resume(struct platform_device *pdev)
{
	struct omap_wdt_dev *wdev;
	wdev = platform_get_drvdata(pdev);
	if (wdev->omap_wdt_users) {
		omap_wdt_enable(wdev);
		omap_wdt_ping(wdev);
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
	spin_lock_init(&wdt_lock);
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
MODULE_ALIAS("platform:omap_wdt");
