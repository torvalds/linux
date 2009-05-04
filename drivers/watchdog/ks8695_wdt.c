/*
 * Watchdog driver for Kendin/Micrel KS8695.
 *
 * (C) 2007 Andrew Victor
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/bitops.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/watchdog.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <mach/timex.h>
#include <mach/regs-timer.h>

#define WDT_DEFAULT_TIME	5	/* seconds */
#define WDT_MAX_TIME		171	/* seconds */

static int wdt_time = WDT_DEFAULT_TIME;
static int nowayout = WATCHDOG_NOWAYOUT;

module_param(wdt_time, int, 0);
MODULE_PARM_DESC(wdt_time, "Watchdog time in seconds. (default="
					__MODULE_STRING(WDT_DEFAULT_TIME) ")");

#ifdef CONFIG_WATCHDOG_NOWAYOUT
module_param(nowayout, int, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
				__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");
#endif


static unsigned long ks8695wdt_busy;
static spinlock_t ks8695_lock;

/* ......................................................................... */

/*
 * Disable the watchdog.
 */
static inline void ks8695_wdt_stop(void)
{
	unsigned long tmcon;

	spin_lock(&ks8695_lock);
	/* disable timer0 */
	tmcon = __raw_readl(KS8695_TMR_VA + KS8695_TMCON);
	__raw_writel(tmcon & ~TMCON_T0EN, KS8695_TMR_VA + KS8695_TMCON);
	spin_unlock(&ks8695_lock);
}

/*
 * Enable and reset the watchdog.
 */
static inline void ks8695_wdt_start(void)
{
	unsigned long tmcon;
	unsigned long tval = wdt_time * CLOCK_TICK_RATE;

	spin_lock(&ks8695_lock);
	/* disable timer0 */
	tmcon = __raw_readl(KS8695_TMR_VA + KS8695_TMCON);
	__raw_writel(tmcon & ~TMCON_T0EN, KS8695_TMR_VA + KS8695_TMCON);

	/* program timer0 */
	__raw_writel(tval | T0TC_WATCHDOG, KS8695_TMR_VA + KS8695_T0TC);

	/* re-enable timer0 */
	tmcon = __raw_readl(KS8695_TMR_VA + KS8695_TMCON);
	__raw_writel(tmcon | TMCON_T0EN, KS8695_TMR_VA + KS8695_TMCON);
	spin_unlock(&ks8695_lock);
}

/*
 * Reload the watchdog timer.  (ie, pat the watchdog)
 */
static inline void ks8695_wdt_reload(void)
{
	unsigned long tmcon;

	spin_lock(&ks8695_lock);
	/* disable, then re-enable timer0 */
	tmcon = __raw_readl(KS8695_TMR_VA + KS8695_TMCON);
	__raw_writel(tmcon & ~TMCON_T0EN, KS8695_TMR_VA + KS8695_TMCON);
	__raw_writel(tmcon | TMCON_T0EN, KS8695_TMR_VA + KS8695_TMCON);
	spin_unlock(&ks8695_lock);
}

/*
 * Change the watchdog time interval.
 */
static int ks8695_wdt_settimeout(int new_time)
{
	/*
	 * All counting occurs at SLOW_CLOCK / 128 = 0.256 Hz
	 *
	 * Since WDV is a 16-bit counter, the maximum period is
	 * 65536 / 0.256 = 256 seconds.
	 */
	if ((new_time <= 0) || (new_time > WDT_MAX_TIME))
		return -EINVAL;

	/* Set new watchdog time. It will be used when
	   ks8695_wdt_start() is called. */
	wdt_time = new_time;
	return 0;
}

/* ......................................................................... */

/*
 * Watchdog device is opened, and watchdog starts running.
 */
static int ks8695_wdt_open(struct inode *inode, struct file *file)
{
	if (test_and_set_bit(0, &ks8695wdt_busy))
		return -EBUSY;

	ks8695_wdt_start();
	return nonseekable_open(inode, file);
}

/*
 * Close the watchdog device.
 * If CONFIG_WATCHDOG_NOWAYOUT is NOT defined then the watchdog is also
 *  disabled.
 */
static int ks8695_wdt_close(struct inode *inode, struct file *file)
{
	/* Disable the watchdog when file is closed */
	if (!nowayout)
		ks8695_wdt_stop();
	clear_bit(0, &ks8695wdt_busy);
	return 0;
}

static struct watchdog_info ks8695_wdt_info = {
	.identity	= "ks8695 watchdog",
	.options	= WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING,
};

/*
 * Handle commands from user-space.
 */
static long ks8695_wdt_ioctl(struct file *file, unsigned int cmd,
							unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	int __user *p = argp;
	int new_value;

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		return copy_to_user(argp, &ks8695_wdt_info,
					sizeof(ks8695_wdt_info)) ? -EFAULT : 0;
	case WDIOC_GETSTATUS:
	case WDIOC_GETBOOTSTATUS:
		return put_user(0, p);
	case WDIOC_SETOPTIONS:
		if (get_user(new_value, p))
			return -EFAULT;
		if (new_value & WDIOS_DISABLECARD)
			ks8695_wdt_stop();
		if (new_value & WDIOS_ENABLECARD)
			ks8695_wdt_start();
		return 0;
	case WDIOC_KEEPALIVE:
		ks8695_wdt_reload();	/* pat the watchdog */
		return 0;
	case WDIOC_SETTIMEOUT:
		if (get_user(new_value, p))
			return -EFAULT;
		if (ks8695_wdt_settimeout(new_value))
			return -EINVAL;
		/* Enable new time value */
		ks8695_wdt_start();
		/* Return current value */
		return put_user(wdt_time, p);
	case WDIOC_GETTIMEOUT:
		return put_user(wdt_time, p);
	default:
		return -ENOTTY;
	}
}

/*
 * Pat the watchdog whenever device is written to.
 */
static ssize_t ks8695_wdt_write(struct file *file, const char *data,
						size_t len, loff_t *ppos)
{
	ks8695_wdt_reload();		/* pat the watchdog */
	return len;
}

/* ......................................................................... */

static const struct file_operations ks8695wdt_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.unlocked_ioctl	= ks8695_wdt_ioctl,
	.open		= ks8695_wdt_open,
	.release	= ks8695_wdt_close,
	.write		= ks8695_wdt_write,
};

static struct miscdevice ks8695wdt_miscdev = {
	.minor		= WATCHDOG_MINOR,
	.name		= "watchdog",
	.fops		= &ks8695wdt_fops,
};

static int __devinit ks8695wdt_probe(struct platform_device *pdev)
{
	int res;

	if (ks8695wdt_miscdev.parent)
		return -EBUSY;
	ks8695wdt_miscdev.parent = &pdev->dev;

	res = misc_register(&ks8695wdt_miscdev);
	if (res)
		return res;

	printk(KERN_INFO "KS8695 Watchdog Timer enabled (%d seconds%s)\n",
				wdt_time, nowayout ? ", nowayout" : "");
	return 0;
}

static int __devexit ks8695wdt_remove(struct platform_device *pdev)
{
	int res;

	res = misc_deregister(&ks8695wdt_miscdev);
	if (!res)
		ks8695wdt_miscdev.parent = NULL;

	return res;
}

static void ks8695wdt_shutdown(struct platform_device *pdev)
{
	ks8695_wdt_stop();
}

#ifdef CONFIG_PM

static int ks8695wdt_suspend(struct platform_device *pdev, pm_message_t message)
{
	ks8695_wdt_stop();
	return 0;
}

static int ks8695wdt_resume(struct platform_device *pdev)
{
	if (ks8695wdt_busy)
		ks8695_wdt_start();
	return 0;
}

#else
#define ks8695wdt_suspend NULL
#define ks8695wdt_resume	NULL
#endif

static struct platform_driver ks8695wdt_driver = {
	.probe		= ks8695wdt_probe,
	.remove		= __devexit_p(ks8695wdt_remove),
	.shutdown	= ks8695wdt_shutdown,
	.suspend	= ks8695wdt_suspend,
	.resume		= ks8695wdt_resume,
	.driver		= {
		.name	= "ks8695_wdt",
		.owner	= THIS_MODULE,
	},
};

static int __init ks8695_wdt_init(void)
{
	spin_lock_init(&ks8695_lock);
	/* Check that the heartbeat value is within range;
	   if not reset to the default */
	if (ks8695_wdt_settimeout(wdt_time)) {
		ks8695_wdt_settimeout(WDT_DEFAULT_TIME);
		pr_info("ks8695_wdt: wdt_time value must be 1 <= wdt_time <= %i, using %d\n",
							wdt_time, WDT_MAX_TIME);
	}
	return platform_driver_register(&ks8695wdt_driver);
}

static void __exit ks8695_wdt_exit(void)
{
	platform_driver_unregister(&ks8695wdt_driver);
}

module_init(ks8695_wdt_init);
module_exit(ks8695_wdt_exit);

MODULE_AUTHOR("Andrew Victor");
MODULE_DESCRIPTION("Watchdog driver for KS8695");
MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);
MODULE_ALIAS("platform:ks8695_wdt");
