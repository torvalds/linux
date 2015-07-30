/*
 * Watchdog driver for Atmel AT91RM9200 (Thunder)
 *
 *  Copyright (C) 2003 SAN People (Pty) Ltd
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/mfd/syscon/atmel-st.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/regmap.h>
#include <linux/types.h>
#include <linux/watchdog.h>
#include <linux/uaccess.h>
#include <linux/of.h>
#include <linux/of_device.h>

#define WDT_DEFAULT_TIME	5	/* seconds */
#define WDT_MAX_TIME		256	/* seconds */

static int wdt_time = WDT_DEFAULT_TIME;
static bool nowayout = WATCHDOG_NOWAYOUT;
static struct regmap *regmap_st;

module_param(wdt_time, int, 0);
MODULE_PARM_DESC(wdt_time, "Watchdog time in seconds. (default="
				__MODULE_STRING(WDT_DEFAULT_TIME) ")");

#ifdef CONFIG_WATCHDOG_NOWAYOUT
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout,
		"Watchdog cannot be stopped once started (default="
				__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");
#endif


static unsigned long at91wdt_busy;

/* ......................................................................... */

static int at91rm9200_restart(struct notifier_block *this,
					unsigned long mode, void *cmd)
{
	/*
	 * Perform a hardware reset with the use of the Watchdog timer.
	 */
	regmap_write(regmap_st, AT91_ST_WDMR,
		     AT91_ST_RSTEN | AT91_ST_EXTEN | 1);
	regmap_write(regmap_st, AT91_ST_CR, AT91_ST_WDRST);

	mdelay(2000);

	pr_emerg("Unable to restart system\n");
	return NOTIFY_DONE;
}

static struct notifier_block at91rm9200_restart_nb = {
	.notifier_call = at91rm9200_restart,
	.priority = 192,
};

/*
 * Disable the watchdog.
 */
static inline void at91_wdt_stop(void)
{
	regmap_write(regmap_st, AT91_ST_WDMR, AT91_ST_EXTEN);
}

/*
 * Enable and reset the watchdog.
 */
static inline void at91_wdt_start(void)
{
	regmap_write(regmap_st, AT91_ST_WDMR, AT91_ST_EXTEN | AT91_ST_RSTEN |
				(((65536 * wdt_time) >> 8) & AT91_ST_WDV));
	regmap_write(regmap_st, AT91_ST_CR, AT91_ST_WDRST);
}

/*
 * Reload the watchdog timer.  (ie, pat the watchdog)
 */
static inline void at91_wdt_reload(void)
{
	regmap_write(regmap_st, AT91_ST_CR, AT91_ST_WDRST);
}

/* ......................................................................... */

/*
 * Watchdog device is opened, and watchdog starts running.
 */
static int at91_wdt_open(struct inode *inode, struct file *file)
{
	if (test_and_set_bit(0, &at91wdt_busy))
		return -EBUSY;

	at91_wdt_start();
	return nonseekable_open(inode, file);
}

/*
 * Close the watchdog device.
 * If CONFIG_WATCHDOG_NOWAYOUT is NOT defined then the watchdog is also
 *  disabled.
 */
static int at91_wdt_close(struct inode *inode, struct file *file)
{
	/* Disable the watchdog when file is closed */
	if (!nowayout)
		at91_wdt_stop();

	clear_bit(0, &at91wdt_busy);
	return 0;
}

/*
 * Change the watchdog time interval.
 */
static int at91_wdt_settimeout(int new_time)
{
	/*
	 * All counting occurs at SLOW_CLOCK / 128 = 256 Hz
	 *
	 * Since WDV is a 16-bit counter, the maximum period is
	 * 65536 / 256 = 256 seconds.
	 */
	if ((new_time <= 0) || (new_time > WDT_MAX_TIME))
		return -EINVAL;

	/* Set new watchdog time. It will be used when
	   at91_wdt_start() is called. */
	wdt_time = new_time;
	return 0;
}

static const struct watchdog_info at91_wdt_info = {
	.identity	= "at91 watchdog",
	.options	= WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING,
};

/*
 * Handle commands from user-space.
 */
static long at91_wdt_ioctl(struct file *file,
					unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	int __user *p = argp;
	int new_value;

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		return copy_to_user(argp, &at91_wdt_info,
				sizeof(at91_wdt_info)) ? -EFAULT : 0;
	case WDIOC_GETSTATUS:
	case WDIOC_GETBOOTSTATUS:
		return put_user(0, p);
	case WDIOC_SETOPTIONS:
		if (get_user(new_value, p))
			return -EFAULT;
		if (new_value & WDIOS_DISABLECARD)
			at91_wdt_stop();
		if (new_value & WDIOS_ENABLECARD)
			at91_wdt_start();
		return 0;
	case WDIOC_KEEPALIVE:
		at91_wdt_reload();	/* pat the watchdog */
		return 0;
	case WDIOC_SETTIMEOUT:
		if (get_user(new_value, p))
			return -EFAULT;
		if (at91_wdt_settimeout(new_value))
			return -EINVAL;
		/* Enable new time value */
		at91_wdt_start();
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
static ssize_t at91_wdt_write(struct file *file, const char *data,
						size_t len, loff_t *ppos)
{
	at91_wdt_reload();		/* pat the watchdog */
	return len;
}

/* ......................................................................... */

static const struct file_operations at91wdt_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.unlocked_ioctl	= at91_wdt_ioctl,
	.open		= at91_wdt_open,
	.release	= at91_wdt_close,
	.write		= at91_wdt_write,
};

static struct miscdevice at91wdt_miscdev = {
	.minor		= WATCHDOG_MINOR,
	.name		= "watchdog",
	.fops		= &at91wdt_fops,
};

static int at91wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device *parent;
	int res;

	if (at91wdt_miscdev.parent)
		return -EBUSY;
	at91wdt_miscdev.parent = &pdev->dev;

	parent = dev->parent;
	if (!parent) {
		dev_err(dev, "no parent\n");
		return -ENODEV;
	}

	regmap_st = syscon_node_to_regmap(parent->of_node);
	if (!regmap_st)
		return -ENODEV;

	res = misc_register(&at91wdt_miscdev);
	if (res)
		return res;

	res = register_restart_handler(&at91rm9200_restart_nb);
	if (res)
		dev_warn(dev, "failed to register restart handler\n");

	pr_info("AT91 Watchdog Timer enabled (%d seconds%s)\n",
		wdt_time, nowayout ? ", nowayout" : "");
	return 0;
}

static int at91wdt_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int res;

	res = unregister_restart_handler(&at91rm9200_restart_nb);
	if (res)
		dev_warn(dev, "failed to unregister restart handler\n");

	misc_deregister(&at91wdt_miscdev);
	at91wdt_miscdev.parent = NULL;

	return res;
}

static void at91wdt_shutdown(struct platform_device *pdev)
{
	at91_wdt_stop();
}

#ifdef CONFIG_PM

static int at91wdt_suspend(struct platform_device *pdev, pm_message_t message)
{
	at91_wdt_stop();
	return 0;
}

static int at91wdt_resume(struct platform_device *pdev)
{
	if (at91wdt_busy)
		at91_wdt_start();
	return 0;
}

#else
#define at91wdt_suspend NULL
#define at91wdt_resume	NULL
#endif

static const struct of_device_id at91_wdt_dt_ids[] = {
	{ .compatible = "atmel,at91rm9200-wdt" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, at91_wdt_dt_ids);

static struct platform_driver at91wdt_driver = {
	.probe		= at91wdt_probe,
	.remove		= at91wdt_remove,
	.shutdown	= at91wdt_shutdown,
	.suspend	= at91wdt_suspend,
	.resume		= at91wdt_resume,
	.driver		= {
		.name	= "atmel_st_watchdog",
		.of_match_table = at91_wdt_dt_ids,
	},
};

static int __init at91_wdt_init(void)
{
	/* Check that the heartbeat value is within range;
	   if not reset to the default */
	if (at91_wdt_settimeout(wdt_time)) {
		at91_wdt_settimeout(WDT_DEFAULT_TIME);
		pr_info("wdt_time value must be 1 <= wdt_time <= 256, using %d\n",
			wdt_time);
	}

	return platform_driver_register(&at91wdt_driver);
}

static void __exit at91_wdt_exit(void)
{
	platform_driver_unregister(&at91wdt_driver);
}

module_init(at91_wdt_init);
module_exit(at91_wdt_exit);

MODULE_AUTHOR("Andrew Victor");
MODULE_DESCRIPTION("Watchdog driver for Atmel AT91RM9200");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:atmel_st_watchdog");
