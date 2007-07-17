/*
 * Watchdog driver for Cirrus Logic EP93xx family of devices.
 *
 * Copyright (c) 2004 Ray Lehtiniemi
 * Copyright (c) 2006 Tower Technologies
 * Based on ep93xx driver, bits from alim7101_wdt.c
 *
 * Authors: Ray Lehtiniemi <rayl@mail.com>,
 *	Alessandro Zummo <a.zummo@towertech.it>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 *
 * This watchdog fires after 250msec, which is a too short interval
 * for us to rely on the user space daemon alone. So we ping the
 * wdt each ~200msec and eventually stop doing it if the user space
 * daemon dies.
 *
 * TODO:
 *
 *	- Test last reset from watchdog status
 *	- Add a few missing ioctls
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/timer.h>

#include <asm/hardware.h>
#include <asm/uaccess.h>

#define WDT_VERSION	"0.3"
#define PFX		"ep93xx_wdt: "

/* default timeout (secs) */
#define WDT_TIMEOUT 30

static int nowayout = WATCHDOG_NOWAYOUT;
static int timeout = WDT_TIMEOUT;

static struct timer_list timer;
static unsigned long next_heartbeat;
static unsigned long wdt_status;
static unsigned long boot_status;

#define WDT_IN_USE		0
#define WDT_OK_TO_CLOSE		1

#define EP93XX_WDT_REG(x)	(EP93XX_WATCHDOG_BASE + (x))
#define EP93XX_WDT_WATCHDOG	EP93XX_WDT_REG(0x00)
#define EP93XX_WDT_WDSTATUS	EP93XX_WDT_REG(0x04)

/* reset the wdt every ~200ms */
#define WDT_INTERVAL (HZ/5)

static void wdt_enable(void)
{
	__raw_writew(0xaaaa, EP93XX_WDT_WATCHDOG);
}

static void wdt_disable(void)
{
	__raw_writew(0xaa55, EP93XX_WDT_WATCHDOG);
}

static inline void wdt_ping(void)
{
	__raw_writew(0x5555, EP93XX_WDT_WATCHDOG);
}

static void wdt_startup(void)
{
	next_heartbeat = jiffies + (timeout * HZ);

	wdt_enable();
	mod_timer(&timer, jiffies + WDT_INTERVAL);
}

static void wdt_shutdown(void)
{
	del_timer_sync(&timer);
	wdt_disable();
}

static void wdt_keepalive(void)
{
	/* user land ping */
	next_heartbeat = jiffies + (timeout * HZ);
}

static int ep93xx_wdt_open(struct inode *inode, struct file *file)
{
	if (test_and_set_bit(WDT_IN_USE, &wdt_status))
		return -EBUSY;

	clear_bit(WDT_OK_TO_CLOSE, &wdt_status);

	wdt_startup();

	return nonseekable_open(inode, file);
}

static ssize_t
ep93xx_wdt_write(struct file *file, const char __user *data, size_t len,
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
				else
					clear_bit(WDT_OK_TO_CLOSE, &wdt_status);
			}
		}
		wdt_keepalive();
	}

	return len;
}

static struct watchdog_info ident = {
	.options = WDIOF_CARDRESET | WDIOF_MAGICCLOSE,
	.identity = "EP93xx Watchdog",
};

static int
ep93xx_wdt_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
		 unsigned long arg)
{
	int ret = -ENOTTY;

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		ret = copy_to_user((struct watchdog_info __user *)arg, &ident,
				sizeof(ident)) ? -EFAULT : 0;
		break;

	case WDIOC_GETSTATUS:
		ret = put_user(0, (int __user *)arg);
		break;

	case WDIOC_GETBOOTSTATUS:
		ret = put_user(boot_status, (int __user *)arg);
		break;

	case WDIOC_GETTIMEOUT:
		/* actually, it is 0.250 seconds.... */
		ret = put_user(1, (int __user *)arg);
		break;

	case WDIOC_KEEPALIVE:
		wdt_keepalive();
		ret = 0;
		break;
	}
	return ret;
}

static int ep93xx_wdt_release(struct inode *inode, struct file *file)
{
	if (test_bit(WDT_OK_TO_CLOSE, &wdt_status))
		wdt_shutdown();
	else
		printk(KERN_CRIT PFX "Device closed unexpectedly - "
			"timer will not stop\n");

	clear_bit(WDT_IN_USE, &wdt_status);
	clear_bit(WDT_OK_TO_CLOSE, &wdt_status);

	return 0;
}

static const struct file_operations ep93xx_wdt_fops = {
	.owner		= THIS_MODULE,
	.write		= ep93xx_wdt_write,
	.ioctl		= ep93xx_wdt_ioctl,
	.open		= ep93xx_wdt_open,
	.release	= ep93xx_wdt_release,
};

static struct miscdevice ep93xx_wdt_miscdev = {
	.minor		= WATCHDOG_MINOR,
	.name		= "watchdog",
	.fops		= &ep93xx_wdt_fops,
};

static void ep93xx_timer_ping(unsigned long data)
{
	if (time_before(jiffies, next_heartbeat))
		wdt_ping();

	/* Re-set the timer interval */
	mod_timer(&timer, jiffies + WDT_INTERVAL);
}

static int __init ep93xx_wdt_init(void)
{
	int err;

	err = misc_register(&ep93xx_wdt_miscdev);

	boot_status = __raw_readl(EP93XX_WDT_WATCHDOG) & 0x01 ? 1 : 0;

	printk(KERN_INFO PFX "EP93XX watchdog, driver version "
		WDT_VERSION "%s\n",
		(__raw_readl(EP93XX_WDT_WATCHDOG) & 0x08)
		? " (nCS1 disable detected)" : "");

	if (timeout < 1 || timeout > 3600) {
		timeout = WDT_TIMEOUT;
		printk(KERN_INFO PFX
			"timeout value must be 1<=x<=3600, using %d\n",
			timeout);
	}

	setup_timer(&timer, ep93xx_timer_ping, 1);
	return err;
}

static void __exit ep93xx_wdt_exit(void)
{
	wdt_shutdown();
	misc_deregister(&ep93xx_wdt_miscdev);
}

module_init(ep93xx_wdt_init);
module_exit(ep93xx_wdt_exit);

module_param(nowayout, int, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started");

module_param(timeout, int, 0);
MODULE_PARM_DESC(timeout, "Watchdog timeout in seconds. (1<=timeout<=3600, default=" __MODULE_STRING(WATCHDOG_TIMEOUT) ")");

MODULE_AUTHOR("Ray Lehtiniemi <rayl@mail.com>,"
		"Alessandro Zummo <a.zummo@towertech.it>");
MODULE_DESCRIPTION("EP93xx Watchdog");
MODULE_LICENSE("GPL");
MODULE_VERSION(WDT_VERSION);
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);
