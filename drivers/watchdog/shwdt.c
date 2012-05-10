/*
 * drivers/watchdog/shwdt.c
 *
 * Watchdog driver for integrated watchdog in the SuperH processors.
 *
 * Copyright (C) 2001 - 2012  Paul Mundt <lethal@linux-sh.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * 14-Dec-2001 Matt Domsch <Matt_Domsch@dell.com>
 *     Added nowayout module option to override CONFIG_WATCHDOG_NOWAYOUT
 *
 * 19-Apr-2002 Rob Radez <rob@osinvestor.com>
 *     Added expect close support, made emulated timeout runtime changeable
 *     general cleanups, add some ioctls
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/ioport.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <asm/watchdog.h>

#define DRV_NAME "sh-wdt"

/*
 * Default clock division ratio is 5.25 msecs. For an additional table of
 * values, consult the asm-sh/watchdog.h. Overload this at module load
 * time.
 *
 * In order for this to work reliably we need to have HZ set to 1000 or
 * something quite higher than 100 (or we need a proper high-res timer
 * implementation that will deal with this properly), otherwise the 10ms
 * resolution of a jiffy is enough to trigger the overflow. For things like
 * the SH-4 and SH-5, this isn't necessarily that big of a problem, though
 * for the SH-2 and SH-3, this isn't recommended unless the WDT is absolutely
 * necssary.
 *
 * As a result of this timing problem, the only modes that are particularly
 * feasible are the 4096 and the 2048 divisors, which yield 5.25 and 2.62ms
 * overflow periods respectively.
 *
 * Also, since we can't really expect userspace to be responsive enough
 * before the overflow happens, we maintain two separate timers .. One in
 * the kernel for clearing out WOVF every 2ms or so (again, this depends on
 * HZ == 1000), and another for monitoring userspace writes to the WDT device.
 *
 * As such, we currently use a configurable heartbeat interval which defaults
 * to 30s. In this case, the userspace daemon is only responsible for periodic
 * writes to the device before the next heartbeat is scheduled. If the daemon
 * misses its deadline, the kernel timer will allow the WDT to overflow.
 */
static int clock_division_ratio = WTCSR_CKS_4096;
#define next_ping_period(cks)	(jiffies + msecs_to_jiffies(cks - 4))

static const struct watchdog_info sh_wdt_info;
static struct platform_device *sh_wdt_dev;
static DEFINE_SPINLOCK(shwdt_lock);

#define WATCHDOG_HEARTBEAT 30			/* 30 sec default heartbeat */
static int heartbeat = WATCHDOG_HEARTBEAT;	/* in seconds */
static bool nowayout = WATCHDOG_NOWAYOUT;
static unsigned long next_heartbeat;

struct sh_wdt {
	void __iomem		*base;
	struct device		*dev;

	struct timer_list	timer;

	unsigned long		enabled;
	char			expect_close;
};

static void sh_wdt_start(struct sh_wdt *wdt)
{
	unsigned long flags;
	u8 csr;

	spin_lock_irqsave(&shwdt_lock, flags);

	next_heartbeat = jiffies + (heartbeat * HZ);
	mod_timer(&wdt->timer, next_ping_period(clock_division_ratio));

	csr = sh_wdt_read_csr();
	csr |= WTCSR_WT | clock_division_ratio;
	sh_wdt_write_csr(csr);

	sh_wdt_write_cnt(0);

	/*
	 * These processors have a bit of an inconsistent initialization
	 * process.. starting with SH-3, RSTS was moved to WTCSR, and the
	 * RSTCSR register was removed.
	 *
	 * On the SH-2 however, in addition with bits being in different
	 * locations, we must deal with RSTCSR outright..
	 */
	csr = sh_wdt_read_csr();
	csr |= WTCSR_TME;
	csr &= ~WTCSR_RSTS;
	sh_wdt_write_csr(csr);

#ifdef CONFIG_CPU_SH2
	csr = sh_wdt_read_rstcsr();
	csr &= ~RSTCSR_RSTS;
	sh_wdt_write_rstcsr(csr);
#endif
	spin_unlock_irqrestore(&shwdt_lock, flags);
}

static void sh_wdt_stop(struct sh_wdt *wdt)
{
	unsigned long flags;
	u8 csr;

	spin_lock_irqsave(&shwdt_lock, flags);

	del_timer(&wdt->timer);

	csr = sh_wdt_read_csr();
	csr &= ~WTCSR_TME;
	sh_wdt_write_csr(csr);

	spin_unlock_irqrestore(&shwdt_lock, flags);
}

static inline void sh_wdt_keepalive(struct sh_wdt *wdt)
{
	unsigned long flags;

	spin_lock_irqsave(&shwdt_lock, flags);
	next_heartbeat = jiffies + (heartbeat * HZ);
	spin_unlock_irqrestore(&shwdt_lock, flags);
}

static int sh_wdt_set_heartbeat(int t)
{
	unsigned long flags;

	if (unlikely(t < 1 || t > 3600)) /* arbitrary upper limit */
		return -EINVAL;

	spin_lock_irqsave(&shwdt_lock, flags);
	heartbeat = t;
	spin_unlock_irqrestore(&shwdt_lock, flags);
	return 0;
}

static void sh_wdt_ping(unsigned long data)
{
	struct sh_wdt *wdt = (struct sh_wdt *)data;
	unsigned long flags;

	spin_lock_irqsave(&shwdt_lock, flags);
	if (time_before(jiffies, next_heartbeat)) {
		u8 csr;

		csr = sh_wdt_read_csr();
		csr &= ~WTCSR_IOVF;
		sh_wdt_write_csr(csr);

		sh_wdt_write_cnt(0);

		mod_timer(&wdt->timer, next_ping_period(clock_division_ratio));
	} else
		dev_warn(wdt->dev, "Heartbeat lost! Will not ping "
		         "the watchdog\n");
	spin_unlock_irqrestore(&shwdt_lock, flags);
}

static int sh_wdt_open(struct inode *inode, struct file *file)
{
	struct sh_wdt *wdt = platform_get_drvdata(sh_wdt_dev);

	if (test_and_set_bit(0, &wdt->enabled))
		return -EBUSY;
	if (nowayout)
		__module_get(THIS_MODULE);

	file->private_data = wdt;

	sh_wdt_start(wdt);

	return nonseekable_open(inode, file);
}

static int sh_wdt_close(struct inode *inode, struct file *file)
{
	struct sh_wdt *wdt = file->private_data;

	if (wdt->expect_close == 42) {
		sh_wdt_stop(wdt);
	} else {
		dev_crit(wdt->dev, "Unexpected close, not "
		         "stopping watchdog!\n");
		sh_wdt_keepalive(wdt);
	}

	clear_bit(0, &wdt->enabled);
	wdt->expect_close = 0;

	return 0;
}

static ssize_t sh_wdt_write(struct file *file, const char *buf,
			    size_t count, loff_t *ppos)
{
	struct sh_wdt *wdt = file->private_data;

	if (count) {
		if (!nowayout) {
			size_t i;

			wdt->expect_close = 0;

			for (i = 0; i != count; i++) {
				char c;
				if (get_user(c, buf + i))
					return -EFAULT;
				if (c == 'V')
					wdt->expect_close = 42;
			}
		}
		sh_wdt_keepalive(wdt);
	}

	return count;
}

static long sh_wdt_ioctl(struct file *file, unsigned int cmd,
							unsigned long arg)
{
	struct sh_wdt *wdt = file->private_data;
	int new_heartbeat;
	int options, retval = -EINVAL;

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		return copy_to_user((struct watchdog_info *)arg,
			  &sh_wdt_info, sizeof(sh_wdt_info)) ? -EFAULT : 0;
	case WDIOC_GETSTATUS:
	case WDIOC_GETBOOTSTATUS:
		return put_user(0, (int *)arg);
	case WDIOC_SETOPTIONS:
		if (get_user(options, (int *)arg))
			return -EFAULT;

		if (options & WDIOS_DISABLECARD) {
			sh_wdt_stop(wdt);
			retval = 0;
		}

		if (options & WDIOS_ENABLECARD) {
			sh_wdt_start(wdt);
			retval = 0;
		}

		return retval;
	case WDIOC_KEEPALIVE:
		sh_wdt_keepalive(wdt);
		return 0;
	case WDIOC_SETTIMEOUT:
		if (get_user(new_heartbeat, (int *)arg))
			return -EFAULT;

		if (sh_wdt_set_heartbeat(new_heartbeat))
			return -EINVAL;

		sh_wdt_keepalive(wdt);
		/* Fall */
	case WDIOC_GETTIMEOUT:
		return put_user(heartbeat, (int *)arg);
	default:
		return -ENOTTY;
	}
	return 0;
}

static const struct file_operations sh_wdt_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.write		= sh_wdt_write,
	.unlocked_ioctl	= sh_wdt_ioctl,
	.open		= sh_wdt_open,
	.release	= sh_wdt_close,
};

static const struct watchdog_info sh_wdt_info = {
	.options		= WDIOF_KEEPALIVEPING | WDIOF_SETTIMEOUT |
				  WDIOF_MAGICCLOSE,
	.firmware_version	= 1,
	.identity		= "SH WDT",
};

static struct miscdevice sh_wdt_miscdev = {
	.minor		= WATCHDOG_MINOR,
	.name		= "watchdog",
	.fops		= &sh_wdt_fops,
};

static int __devinit sh_wdt_probe(struct platform_device *pdev)
{
	struct sh_wdt *wdt;
	struct resource *res;
	int rc;

	/*
	 * As this driver only covers the global watchdog case, reject
	 * any attempts to register per-CPU watchdogs.
	 */
	if (pdev->id != -1)
		return -EINVAL;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (unlikely(!res))
		return -EINVAL;

	if (!devm_request_mem_region(&pdev->dev, res->start,
				     resource_size(res), DRV_NAME))
		return -EBUSY;

	wdt = devm_kzalloc(&pdev->dev, sizeof(struct sh_wdt), GFP_KERNEL);
	if (unlikely(!wdt)) {
		rc = -ENOMEM;
		goto out_release;
	}

	wdt->dev = &pdev->dev;

	wdt->base = devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (unlikely(!wdt->base)) {
		rc = -ENXIO;
		goto out_err;
	}

	sh_wdt_miscdev.parent = wdt->dev;

	rc = misc_register(&sh_wdt_miscdev);
	if (unlikely(rc)) {
		dev_err(&pdev->dev,
			"Can't register miscdev on minor=%d (err=%d)\n",
						sh_wdt_miscdev.minor, rc);
		goto out_unmap;
	}

	init_timer(&wdt->timer);
	wdt->timer.function	= sh_wdt_ping;
	wdt->timer.data		= (unsigned long)wdt;
	wdt->timer.expires	= next_ping_period(clock_division_ratio);

	platform_set_drvdata(pdev, wdt);
	sh_wdt_dev = pdev;

	dev_info(&pdev->dev, "initialized.\n");

	return 0;

out_unmap:
	devm_iounmap(&pdev->dev, wdt->base);
out_err:
	devm_kfree(&pdev->dev, wdt);
out_release:
	devm_release_mem_region(&pdev->dev, res->start, resource_size(res));

	return rc;
}

static int __devexit sh_wdt_remove(struct platform_device *pdev)
{
	struct sh_wdt *wdt = platform_get_drvdata(pdev);
	struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	platform_set_drvdata(pdev, NULL);

	misc_deregister(&sh_wdt_miscdev);

	sh_wdt_dev = NULL;

	devm_release_mem_region(&pdev->dev, res->start, resource_size(res));
	devm_iounmap(&pdev->dev, wdt->base);
	devm_kfree(&pdev->dev, wdt);

	return 0;
}

static void sh_wdt_shutdown(struct platform_device *pdev)
{
	struct sh_wdt *wdt = platform_get_drvdata(pdev);

	sh_wdt_stop(wdt);
}

static struct platform_driver sh_wdt_driver = {
	.driver		= {
		.name	= DRV_NAME,
		.owner	= THIS_MODULE,
	},

	.probe		= sh_wdt_probe,
	.remove		= __devexit_p(sh_wdt_remove),
	.shutdown	= sh_wdt_shutdown,
};

static int __init sh_wdt_init(void)
{
	int rc;

	if (unlikely(clock_division_ratio < 0x5 ||
		     clock_division_ratio > 0x7)) {
		clock_division_ratio = WTCSR_CKS_4096;

		pr_info("divisor must be 0x5<=x<=0x7, using %d\n",
			clock_division_ratio);
	}

	rc = sh_wdt_set_heartbeat(heartbeat);
	if (unlikely(rc)) {
		heartbeat = WATCHDOG_HEARTBEAT;

		pr_info("heartbeat value must be 1<=x<=3600, using %d\n",
			heartbeat);
	}

	pr_info("configured with heartbeat=%d sec (nowayout=%d)\n",
		heartbeat, nowayout);

	return platform_driver_register(&sh_wdt_driver);
}

static void __exit sh_wdt_exit(void)
{
	platform_driver_unregister(&sh_wdt_driver);
}
module_init(sh_wdt_init);
module_exit(sh_wdt_exit);

MODULE_AUTHOR("Paul Mundt <lethal@linux-sh.org>");
MODULE_DESCRIPTION("SuperH watchdog driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRV_NAME);
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);

module_param(clock_division_ratio, int, 0);
MODULE_PARM_DESC(clock_division_ratio,
	"Clock division ratio. Valid ranges are from 0x5 (1.31ms) "
	"to 0x7 (5.25ms). (default=" __MODULE_STRING(WTCSR_CKS_4096) ")");

module_param(heartbeat, int, 0);
MODULE_PARM_DESC(heartbeat,
	"Watchdog heartbeat in seconds. (1 <= heartbeat <= 3600, default="
				__MODULE_STRING(WATCHDOG_HEARTBEAT) ")");

module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout,
	"Watchdog cannot be stopped once started (default="
				__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");
