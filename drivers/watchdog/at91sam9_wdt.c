/*
 * Watchdog driver for Atmel AT91SAM9x processors.
 *
 * Copyright (C) 2008 Renaud CERRATO r.cerrato@til-technologies.fr
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

/*
 * The Watchdog Timer Mode Register can be only written to once. If the
 * timeout need to be set from Linux, be sure that the bootstrap or the
 * bootloader doesn't write to this register.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/watchdog.h>
#include <linux/jiffies.h>
#include <linux/timer.h>
#include <linux/bitops.h>
#include <linux/uaccess.h>

#include "at91sam9_wdt.h"

#define DRV_NAME "AT91SAM9 Watchdog"

#define wdt_read(field) \
	__raw_readl(at91wdt_private.base + field)
#define wdt_write(field, val) \
	__raw_writel((val), at91wdt_private.base + field)

/* AT91SAM9 watchdog runs a 12bit counter @ 256Hz,
 * use this to convert a watchdog
 * value from/to milliseconds.
 */
#define ms_to_ticks(t)	(((t << 8) / 1000) - 1)
#define ticks_to_ms(t)	(((t + 1) * 1000) >> 8)

/* Hardware timeout in seconds */
#define WDT_HW_TIMEOUT 2

/* Timer heartbeat (500ms) */
#define WDT_TIMEOUT	(HZ/2)

/* User land timeout */
#define WDT_HEARTBEAT 15
static int heartbeat = WDT_HEARTBEAT;
module_param(heartbeat, int, 0);
MODULE_PARM_DESC(heartbeat, "Watchdog heartbeats in seconds. "
	"(default = " __MODULE_STRING(WDT_HEARTBEAT) ")");

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started "
	"(default=" __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

static void at91_ping(unsigned long data);

static struct {
	void __iomem *base;
	unsigned long next_heartbeat;	/* the next_heartbeat for the timer */
	unsigned long open;
	char expect_close;
	struct timer_list timer;	/* The timer that pings the watchdog */
} at91wdt_private;

/* ......................................................................... */


/*
 * Reload the watchdog timer.  (ie, pat the watchdog)
 */
static inline void at91_wdt_reset(void)
{
	wdt_write(AT91_WDT_CR, AT91_WDT_KEY | AT91_WDT_WDRSTT);
}

/*
 * Timer tick
 */
static void at91_ping(unsigned long data)
{
	if (time_before(jiffies, at91wdt_private.next_heartbeat) ||
			(!nowayout && !at91wdt_private.open)) {
		at91_wdt_reset();
		mod_timer(&at91wdt_private.timer, jiffies + WDT_TIMEOUT);
	} else
		pr_crit("I will reset your machine !\n");
}

/*
 * Watchdog device is opened, and watchdog starts running.
 */
static int at91_wdt_open(struct inode *inode, struct file *file)
{
	if (test_and_set_bit(0, &at91wdt_private.open))
		return -EBUSY;

	at91wdt_private.next_heartbeat = jiffies + heartbeat * HZ;
	mod_timer(&at91wdt_private.timer, jiffies + WDT_TIMEOUT);

	return nonseekable_open(inode, file);
}

/*
 * Close the watchdog device.
 */
static int at91_wdt_close(struct inode *inode, struct file *file)
{
	clear_bit(0, &at91wdt_private.open);

	/* stop internal ping */
	if (!at91wdt_private.expect_close)
		del_timer(&at91wdt_private.timer);

	at91wdt_private.expect_close = 0;
	return 0;
}

/*
 * Set the watchdog time interval in 1/256Hz (write-once)
 * Counter is 12 bit.
 */
static int at91_wdt_settimeout(unsigned int timeout)
{
	unsigned int reg;
	unsigned int mr;

	/* Check if disabled */
	mr = wdt_read(AT91_WDT_MR);
	if (mr & AT91_WDT_WDDIS) {
		pr_err("sorry, watchdog is disabled\n");
		return -EIO;
	}

	/*
	 * All counting occurs at SLOW_CLOCK / 128 = 256 Hz
	 *
	 * Since WDV is a 12-bit counter, the maximum period is
	 * 4096 / 256 = 16 seconds.
	 */
	reg = AT91_WDT_WDRSTEN	/* causes watchdog reset */
		/* | AT91_WDT_WDRPROC	causes processor reset only */
		| AT91_WDT_WDDBGHLT	/* disabled in debug mode */
		| AT91_WDT_WDD		/* restart at any time */
		| (timeout & AT91_WDT_WDV);  /* timer value */
	wdt_write(AT91_WDT_MR, reg);

	return 0;
}

static const struct watchdog_info at91_wdt_info = {
	.identity	= DRV_NAME,
	.options	= WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING |
						WDIOF_MAGICCLOSE,
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

	case WDIOC_KEEPALIVE:
		at91wdt_private.next_heartbeat = jiffies + heartbeat * HZ;
		return 0;

	case WDIOC_SETTIMEOUT:
		if (get_user(new_value, p))
			return -EFAULT;

		heartbeat = new_value;
		at91wdt_private.next_heartbeat = jiffies + heartbeat * HZ;

		return put_user(new_value, p);  /* return current value */

	case WDIOC_GETTIMEOUT:
		return put_user(heartbeat, p);
	}
	return -ENOTTY;
}

/*
 * Pat the watchdog whenever device is written to.
 */
static ssize_t at91_wdt_write(struct file *file, const char *data, size_t len,
								loff_t *ppos)
{
	if (!len)
		return 0;

	/* Scan for magic character */
	if (!nowayout) {
		size_t i;

		at91wdt_private.expect_close = 0;

		for (i = 0; i < len; i++) {
			char c;
			if (get_user(c, data + i))
				return -EFAULT;
			if (c == 'V') {
				at91wdt_private.expect_close = 42;
				break;
			}
		}
	}

	at91wdt_private.next_heartbeat = jiffies + heartbeat * HZ;

	return len;
}

/* ......................................................................... */

static const struct file_operations at91wdt_fops = {
	.owner			= THIS_MODULE,
	.llseek			= no_llseek,
	.unlocked_ioctl	= at91_wdt_ioctl,
	.open			= at91_wdt_open,
	.release		= at91_wdt_close,
	.write			= at91_wdt_write,
};

static struct miscdevice at91wdt_miscdev = {
	.minor		= WATCHDOG_MINOR,
	.name		= "watchdog",
	.fops		= &at91wdt_fops,
};

static int __init at91wdt_probe(struct platform_device *pdev)
{
	struct resource	*r;
	int res;

	if (at91wdt_miscdev.parent)
		return -EBUSY;
	at91wdt_miscdev.parent = &pdev->dev;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r)
		return -ENODEV;
	at91wdt_private.base = ioremap(r->start, resource_size(r));
	if (!at91wdt_private.base) {
		dev_err(&pdev->dev, "failed to map registers, aborting.\n");
		return -ENOMEM;
	}

	/* Set watchdog */
	res = at91_wdt_settimeout(ms_to_ticks(WDT_HW_TIMEOUT * 1000));
	if (res)
		return res;

	res = misc_register(&at91wdt_miscdev);
	if (res)
		return res;

	at91wdt_private.next_heartbeat = jiffies + heartbeat * HZ;
	setup_timer(&at91wdt_private.timer, at91_ping, 0);
	mod_timer(&at91wdt_private.timer, jiffies + WDT_TIMEOUT);

	pr_info("enabled (heartbeat=%d sec, nowayout=%d)\n",
		heartbeat, nowayout);

	return 0;
}

static int __exit at91wdt_remove(struct platform_device *pdev)
{
	int res;

	res = misc_deregister(&at91wdt_miscdev);
	if (!res)
		at91wdt_miscdev.parent = NULL;

	return res;
}

static struct platform_driver at91wdt_driver = {
	.remove		= __exit_p(at91wdt_remove),
	.driver		= {
		.name	= "at91_wdt",
		.owner	= THIS_MODULE,
	},
};

static int __init at91sam_wdt_init(void)
{
	return platform_driver_probe(&at91wdt_driver, at91wdt_probe);
}

static void __exit at91sam_wdt_exit(void)
{
	platform_driver_unregister(&at91wdt_driver);
}

module_init(at91sam_wdt_init);
module_exit(at91sam_wdt_exit);

MODULE_AUTHOR("Renaud CERRATO <r.cerrato@til-technologies.fr>");
MODULE_DESCRIPTION("Watchdog driver for Atmel AT91SAM9x processors");
MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);
