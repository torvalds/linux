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
#include <linux/spinlock.h>
#include <linux/watchdog.h>
#include <linux/pm_runtime.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/err.h>
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

#define WATCHDOG_HEARTBEAT 30			/* 30 sec default heartbeat */
static int heartbeat = WATCHDOG_HEARTBEAT;	/* in seconds */
static bool nowayout = WATCHDOG_NOWAYOUT;
static unsigned long next_heartbeat;

struct sh_wdt {
	void __iomem		*base;
	struct device		*dev;
	struct clk		*clk;
	spinlock_t		lock;

	struct timer_list	timer;
};

static int sh_wdt_start(struct watchdog_device *wdt_dev)
{
	struct sh_wdt *wdt = watchdog_get_drvdata(wdt_dev);
	unsigned long flags;
	u8 csr;

	pm_runtime_get_sync(wdt->dev);
	clk_enable(wdt->clk);

	spin_lock_irqsave(&wdt->lock, flags);

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
	spin_unlock_irqrestore(&wdt->lock, flags);

	return 0;
}

static int sh_wdt_stop(struct watchdog_device *wdt_dev)
{
	struct sh_wdt *wdt = watchdog_get_drvdata(wdt_dev);
	unsigned long flags;
	u8 csr;

	spin_lock_irqsave(&wdt->lock, flags);

	del_timer(&wdt->timer);

	csr = sh_wdt_read_csr();
	csr &= ~WTCSR_TME;
	sh_wdt_write_csr(csr);

	spin_unlock_irqrestore(&wdt->lock, flags);

	clk_disable(wdt->clk);
	pm_runtime_put_sync(wdt->dev);

	return 0;
}

static int sh_wdt_keepalive(struct watchdog_device *wdt_dev)
{
	struct sh_wdt *wdt = watchdog_get_drvdata(wdt_dev);
	unsigned long flags;

	spin_lock_irqsave(&wdt->lock, flags);
	next_heartbeat = jiffies + (heartbeat * HZ);
	spin_unlock_irqrestore(&wdt->lock, flags);

	return 0;
}

static int sh_wdt_set_heartbeat(struct watchdog_device *wdt_dev, unsigned t)
{
	struct sh_wdt *wdt = watchdog_get_drvdata(wdt_dev);
	unsigned long flags;

	if (unlikely(t < 1 || t > 3600)) /* arbitrary upper limit */
		return -EINVAL;

	spin_lock_irqsave(&wdt->lock, flags);
	heartbeat = t;
	wdt_dev->timeout = t;
	spin_unlock_irqrestore(&wdt->lock, flags);

	return 0;
}

static void sh_wdt_ping(unsigned long data)
{
	struct sh_wdt *wdt = (struct sh_wdt *)data;
	unsigned long flags;

	spin_lock_irqsave(&wdt->lock, flags);
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
	spin_unlock_irqrestore(&wdt->lock, flags);
}

static const struct watchdog_info sh_wdt_info = {
	.options		= WDIOF_KEEPALIVEPING | WDIOF_SETTIMEOUT |
				  WDIOF_MAGICCLOSE,
	.firmware_version	= 1,
	.identity		= "SH WDT",
};

static const struct watchdog_ops sh_wdt_ops = {
	.owner		= THIS_MODULE,
	.start		= sh_wdt_start,
	.stop		= sh_wdt_stop,
	.ping		= sh_wdt_keepalive,
	.set_timeout	= sh_wdt_set_heartbeat,
};

static struct watchdog_device sh_wdt_dev = {
	.info	= &sh_wdt_info,
	.ops	= &sh_wdt_ops,
};

static int sh_wdt_probe(struct platform_device *pdev)
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

	wdt = devm_kzalloc(&pdev->dev, sizeof(struct sh_wdt), GFP_KERNEL);
	if (unlikely(!wdt))
		return -ENOMEM;

	wdt->dev = &pdev->dev;

	wdt->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(wdt->clk)) {
		/*
		 * Clock framework support is optional, continue on
		 * anyways if we don't find a matching clock.
		 */
		wdt->clk = NULL;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	wdt->base = devm_ioremap_resource(wdt->dev, res);
	if (IS_ERR(wdt->base))
		return PTR_ERR(wdt->base);

	watchdog_set_nowayout(&sh_wdt_dev, nowayout);
	watchdog_set_drvdata(&sh_wdt_dev, wdt);
	sh_wdt_dev.parent = &pdev->dev;

	spin_lock_init(&wdt->lock);

	rc = sh_wdt_set_heartbeat(&sh_wdt_dev, heartbeat);
	if (unlikely(rc)) {
		/* Default timeout if invalid */
		sh_wdt_set_heartbeat(&sh_wdt_dev, WATCHDOG_HEARTBEAT);

		dev_warn(&pdev->dev,
			 "heartbeat value must be 1<=x<=3600, using %d\n",
			 sh_wdt_dev.timeout);
	}

	dev_info(&pdev->dev, "configured with heartbeat=%d sec (nowayout=%d)\n",
		 sh_wdt_dev.timeout, nowayout);

	rc = watchdog_register_device(&sh_wdt_dev);
	if (unlikely(rc)) {
		dev_err(&pdev->dev, "Can't register watchdog (err=%d)\n", rc);
		return rc;
	}

	setup_timer(&wdt->timer, sh_wdt_ping, (unsigned long)wdt);
	wdt->timer.expires	= next_ping_period(clock_division_ratio);

	dev_info(&pdev->dev, "initialized.\n");

	pm_runtime_enable(&pdev->dev);

	return 0;
}

static int sh_wdt_remove(struct platform_device *pdev)
{
	watchdog_unregister_device(&sh_wdt_dev);

	pm_runtime_disable(&pdev->dev);

	return 0;
}

static void sh_wdt_shutdown(struct platform_device *pdev)
{
	sh_wdt_stop(&sh_wdt_dev);
}

static struct platform_driver sh_wdt_driver = {
	.driver		= {
		.name	= DRV_NAME,
	},

	.probe		= sh_wdt_probe,
	.remove		= sh_wdt_remove,
	.shutdown	= sh_wdt_shutdown,
};

static int __init sh_wdt_init(void)
{
	if (unlikely(clock_division_ratio < 0x5 ||
		     clock_division_ratio > 0x7)) {
		clock_division_ratio = WTCSR_CKS_4096;

		pr_info("divisor must be 0x5<=x<=0x7, using %d\n",
			clock_division_ratio);
	}

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
