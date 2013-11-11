/*
 *	AMD Elan SC520 processor Watchdog Timer driver
 *
 *	Based on acquirewdt.c by Alan Cox,
 *	     and sbc60xxwdt.c by Jakob Oestergaard <jakob@unthought.net>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *	The authors do NOT admit liability nor provide warranty for
 *	any of this software. This material is provided "AS-IS" in
 *	the hope that it may be useful for others.
 *
 *	(c) Copyright 2001    Scott Jennings <linuxdrivers@oro.net>
 *           9/27 - 2001      [Initial release]
 *
 *	Additional fixes Alan Cox
 *	-	Fixed formatting
 *	-	Removed debug printks
 *	-	Fixed SMP built kernel deadlock
 *	-	Switched to private locks not lock_kernel
 *	-	Used ioremap/writew/readw
 *	-	Added NOWAYOUT support
 *	4/12 - 2002 Changes by Rob Radez <rob@osinvestor.com>
 *	-	Change comments
 *	-	Eliminate fop_llseek
 *	-	Change CONFIG_WATCHDOG_NOWAYOUT semantics
 *	-	Add KERN_* tags to printks
 *	-	fix possible wdt_is_open race
 *	-	Report proper capabilities in watchdog_info
 *	-	Add WDIOC_{GETSTATUS, GETBOOTSTATUS, SETTIMEOUT,
 *		GETTIMEOUT, SETOPTIONS} ioctls
 *	09/8 - 2003 Changes by Wim Van Sebroeck <wim@iguana.be>
 *	-	cleanup of trailing spaces
 *	-	added extra printk's for startup problems
 *	-	use module_param
 *	-	made timeout (the emulated heartbeat) a module_param
 *	-	made the keepalive ping an internal subroutine
 *	3/27 - 2004 Changes by Sean Young <sean@mess.org>
 *	-	set MMCR_BASE to 0xfffef000
 *	-	CBAR does not need to be read
 *	-	removed debugging printks
 *
 *  This WDT driver is different from most other Linux WDT
 *  drivers in that the driver will ping the watchdog by itself,
 *  because this particular WDT has a very short timeout (1.6
 *  seconds) and it would be insane to count on any userspace
 *  daemon always getting scheduled within that time frame.
 *
 *  This driver uses memory mapped IO, and spinlock.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/timer.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/fs.h>
#include <linux/ioport.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/io.h>
#include <linux/uaccess.h>


/*
 * The AMD Elan SC520 timeout value is 492us times a power of 2 (0-7)
 *
 *   0: 492us    2: 1.01s    4: 4.03s   6: 16.22s
 *   1: 503ms    3: 2.01s    5: 8.05s   7: 32.21s
 *
 * We will program the SC520 watchdog for a timeout of 2.01s.
 * If we reset the watchdog every ~250ms we should be safe.
 */

#define WDT_INTERVAL (HZ/4+1)

/*
 * We must not require too good response from the userspace daemon.
 * Here we require the userspace daemon to send us a heartbeat
 * char to /dev/watchdog every 30 seconds.
 */

#define WATCHDOG_TIMEOUT 30		/* 30 sec default timeout */
/* in seconds, will be multiplied by HZ to get seconds to wait for a ping */
static int timeout = WATCHDOG_TIMEOUT;
module_param(timeout, int, 0);
MODULE_PARM_DESC(timeout,
	"Watchdog timeout in seconds. (1 <= timeout <= 3600, default="
				__MODULE_STRING(WATCHDOG_TIMEOUT) ")");

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout,
		"Watchdog cannot be stopped once started (default="
				__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

/*
 * AMD Elan SC520 - Watchdog Timer Registers
 */
#define MMCR_BASE	0xfffef000	/* The default base address */
#define OFFS_WDTMRCTL	0xCB0	/* Watchdog Timer Control Register */

/* WDT Control Register bit definitions */
#define WDT_EXP_SEL_01	0x0001	/* [01] Time-out = 496 us (with 33 Mhz clk). */
#define WDT_EXP_SEL_02	0x0002	/* [02] Time-out = 508 ms (with 33 Mhz clk). */
#define WDT_EXP_SEL_03	0x0004	/* [03] Time-out = 1.02 s (with 33 Mhz clk). */
#define WDT_EXP_SEL_04	0x0008	/* [04] Time-out = 2.03 s (with 33 Mhz clk). */
#define WDT_EXP_SEL_05	0x0010	/* [05] Time-out = 4.07 s (with 33 Mhz clk). */
#define WDT_EXP_SEL_06	0x0020	/* [06] Time-out = 8.13 s (with 33 Mhz clk). */
#define WDT_EXP_SEL_07	0x0040	/* [07] Time-out = 16.27s (with 33 Mhz clk). */
#define WDT_EXP_SEL_08	0x0080	/* [08] Time-out = 32.54s (with 33 Mhz clk). */
#define WDT_IRQ_FLG	0x1000	/* [12] Interrupt Request Flag */
#define WDT_WRST_ENB	0x4000	/* [14] Watchdog Timer Reset Enable */
#define WDT_ENB		0x8000	/* [15] Watchdog Timer Enable */

static __u16 __iomem *wdtmrctl;

static void wdt_timer_ping(unsigned long);
static DEFINE_TIMER(timer, wdt_timer_ping, 0, 0);
static unsigned long next_heartbeat;
static unsigned long wdt_is_open;
static char wdt_expect_close;
static DEFINE_SPINLOCK(wdt_spinlock);

/*
 *	Whack the dog
 */

static void wdt_timer_ping(unsigned long data)
{
	/* If we got a heartbeat pulse within the WDT_US_INTERVAL
	 * we agree to ping the WDT
	 */
	if (time_before(jiffies, next_heartbeat)) {
		/* Ping the WDT */
		spin_lock(&wdt_spinlock);
		writew(0xAAAA, wdtmrctl);
		writew(0x5555, wdtmrctl);
		spin_unlock(&wdt_spinlock);

		/* Re-set the timer interval */
		mod_timer(&timer, jiffies + WDT_INTERVAL);
	} else
		pr_warn("Heartbeat lost! Will not ping the watchdog\n");
}

/*
 *	Utility routines
 */

static void wdt_config(int writeval)
{
	__u16 dummy;
	unsigned long flags;

	/* buy some time (ping) */
	spin_lock_irqsave(&wdt_spinlock, flags);
	dummy = readw(wdtmrctl);	/* ensure write synchronization */
	writew(0xAAAA, wdtmrctl);
	writew(0x5555, wdtmrctl);
	/* unlock WDT = make WDT configuration register writable one time */
	writew(0x3333, wdtmrctl);
	writew(0xCCCC, wdtmrctl);
	/* write WDT configuration register */
	writew(writeval, wdtmrctl);
	spin_unlock_irqrestore(&wdt_spinlock, flags);
}

static int wdt_startup(void)
{
	next_heartbeat = jiffies + (timeout * HZ);

	/* Start the timer */
	mod_timer(&timer, jiffies + WDT_INTERVAL);

	/* Start the watchdog */
	wdt_config(WDT_ENB | WDT_WRST_ENB | WDT_EXP_SEL_04);

	pr_info("Watchdog timer is now enabled\n");
	return 0;
}

static int wdt_turnoff(void)
{
	/* Stop the timer */
	del_timer(&timer);

	/* Stop the watchdog */
	wdt_config(0);

	pr_info("Watchdog timer is now disabled...\n");
	return 0;
}

static int wdt_keepalive(void)
{
	/* user land ping */
	next_heartbeat = jiffies + (timeout * HZ);
	return 0;
}

static int wdt_set_heartbeat(int t)
{
	if ((t < 1) || (t > 3600))	/* arbitrary upper limit */
		return -EINVAL;

	timeout = t;
	return 0;
}

/*
 *	/dev/watchdog handling
 */

static ssize_t fop_write(struct file *file, const char __user *buf,
						size_t count, loff_t *ppos)
{
	/* See if we got the magic character 'V' and reload the timer */
	if (count) {
		if (!nowayout) {
			size_t ofs;

			/* note: just in case someone wrote the magic character
			 * five months ago... */
			wdt_expect_close = 0;

			/* now scan */
			for (ofs = 0; ofs != count; ofs++) {
				char c;
				if (get_user(c, buf + ofs))
					return -EFAULT;
				if (c == 'V')
					wdt_expect_close = 42;
			}
		}

		/* Well, anyhow someone wrote to us, we should
		   return that favour */
		wdt_keepalive();
	}
	return count;
}

static int fop_open(struct inode *inode, struct file *file)
{
	/* Just in case we're already talking to someone... */
	if (test_and_set_bit(0, &wdt_is_open))
		return -EBUSY;
	if (nowayout)
		__module_get(THIS_MODULE);

	/* Good, fire up the show */
	wdt_startup();
	return nonseekable_open(inode, file);
}

static int fop_close(struct inode *inode, struct file *file)
{
	if (wdt_expect_close == 42)
		wdt_turnoff();
	else {
		pr_crit("Unexpected close, not stopping watchdog!\n");
		wdt_keepalive();
	}
	clear_bit(0, &wdt_is_open);
	wdt_expect_close = 0;
	return 0;
}

static long fop_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	int __user *p = argp;
	static const struct watchdog_info ident = {
		.options = WDIOF_KEEPALIVEPING | WDIOF_SETTIMEOUT
							| WDIOF_MAGICCLOSE,
		.firmware_version = 1,
		.identity = "SC520",
	};

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		return copy_to_user(argp, &ident, sizeof(ident)) ? -EFAULT : 0;
	case WDIOC_GETSTATUS:
	case WDIOC_GETBOOTSTATUS:
		return put_user(0, p);
	case WDIOC_SETOPTIONS:
	{
		int new_options, retval = -EINVAL;

		if (get_user(new_options, p))
			return -EFAULT;

		if (new_options & WDIOS_DISABLECARD) {
			wdt_turnoff();
			retval = 0;
		}

		if (new_options & WDIOS_ENABLECARD) {
			wdt_startup();
			retval = 0;
		}

		return retval;
	}
	case WDIOC_KEEPALIVE:
		wdt_keepalive();
		return 0;
	case WDIOC_SETTIMEOUT:
	{
		int new_timeout;

		if (get_user(new_timeout, p))
			return -EFAULT;

		if (wdt_set_heartbeat(new_timeout))
			return -EINVAL;

		wdt_keepalive();
		/* Fall through */
	}
	case WDIOC_GETTIMEOUT:
		return put_user(timeout, p);
	default:
		return -ENOTTY;
	}
}

static const struct file_operations wdt_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.write		= fop_write,
	.open		= fop_open,
	.release	= fop_close,
	.unlocked_ioctl	= fop_ioctl,
};

static struct miscdevice wdt_miscdev = {
	.minor	= WATCHDOG_MINOR,
	.name	= "watchdog",
	.fops	= &wdt_fops,
};

/*
 *	Notifier for system down
 */

static int wdt_notify_sys(struct notifier_block *this, unsigned long code,
	void *unused)
{
	if (code == SYS_DOWN || code == SYS_HALT)
		wdt_turnoff();
	return NOTIFY_DONE;
}

/*
 *	The WDT needs to learn about soft shutdowns in order to
 *	turn the timebomb registers off.
 */

static struct notifier_block wdt_notifier = {
	.notifier_call = wdt_notify_sys,
};

static void __exit sc520_wdt_unload(void)
{
	if (!nowayout)
		wdt_turnoff();

	/* Deregister */
	misc_deregister(&wdt_miscdev);
	unregister_reboot_notifier(&wdt_notifier);
	iounmap(wdtmrctl);
}

static int __init sc520_wdt_init(void)
{
	int rc = -EBUSY;

	/* Check that the timeout value is within it's range ;
	   if not reset to the default */
	if (wdt_set_heartbeat(timeout)) {
		wdt_set_heartbeat(WATCHDOG_TIMEOUT);
		pr_info("timeout value must be 1 <= timeout <= 3600, using %d\n",
			WATCHDOG_TIMEOUT);
	}

	wdtmrctl = ioremap(MMCR_BASE + OFFS_WDTMRCTL, 2);
	if (!wdtmrctl) {
		pr_err("Unable to remap memory\n");
		rc = -ENOMEM;
		goto err_out_region2;
	}

	rc = register_reboot_notifier(&wdt_notifier);
	if (rc) {
		pr_err("cannot register reboot notifier (err=%d)\n", rc);
		goto err_out_ioremap;
	}

	rc = misc_register(&wdt_miscdev);
	if (rc) {
		pr_err("cannot register miscdev on minor=%d (err=%d)\n",
		       WATCHDOG_MINOR, rc);
		goto err_out_notifier;
	}

	pr_info("WDT driver for SC520 initialised. timeout=%d sec (nowayout=%d)\n",
		timeout, nowayout);

	return 0;

err_out_notifier:
	unregister_reboot_notifier(&wdt_notifier);
err_out_ioremap:
	iounmap(wdtmrctl);
err_out_region2:
	return rc;
}

module_init(sc520_wdt_init);
module_exit(sc520_wdt_unload);

MODULE_AUTHOR("Scott and Bill Jennings");
MODULE_DESCRIPTION(
	"Driver for watchdog timer in AMD \"Elan\" SC520 uProcessor");
MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);
