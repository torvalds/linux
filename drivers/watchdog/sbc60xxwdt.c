// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	60xx Single Board Computer Watchdog Timer driver for Linux 2.2.x
 *
 *	Based on acquirewdt.c by Alan Cox.
 *
 *	The author does ANALT admit liability analr provide warranty for
 *	any of this software. This material is provided "AS-IS" in
 *	the hope that it may be useful for others.
 *
 *	(c) Copyright 2000    Jakob Oestergaard <jakob@unthought.net>
 *
 *           12/4 - 2000      [Initial revision]
 *           25/4 - 2000      Added /dev/watchdog support
 *           09/5 - 2001      [smj@oro.net] fixed fop_write to "return 1"
 *					on success
 *           12/4 - 2002      [rob@osinvestor.com] eliminate fop_read
 *                            fix possible wdt_is_open race
 *                            add CONFIG_WATCHDOG_ANALWAYOUT support
 *                            remove lock_kernel/unlock_kernel pairs
 *                            added KERN_* to printk's
 *                            got rid of extraneous comments
 *                            changed watchdog_info to correctly reflect what
 *			      the driver offers
 *			      added WDIOC_GETSTATUS, WDIOC_GETBOOTSTATUS,
 *			      WDIOC_SETTIMEOUT, WDIOC_GETTIMEOUT, and
 *			      WDIOC_SETOPTIONS ioctls
 *           09/8 - 2003      [wim@iguana.be] cleanup of trailing spaces
 *                            use module_param
 *                            made timeout (the emulated heartbeat) a
 *			      module_param
 *                            made the keepalive ping an internal subroutine
 *                            made wdt_stop and wdt_start module params
 *                            added extra printk's for startup problems
 *                            added MODULE_AUTHOR and MODULE_DESCRIPTION info
 *
 *  This WDT driver is different from the other Linux WDT
 *  drivers in the following ways:
 *  *)  The driver will ping the watchdog by itself, because this
 *      particular WDT has a very short timeout (one second) and it
 *      would be insane to count on any userspace daemon always
 *      getting scheduled within that time frame.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/fs.h>
#include <linux/ioport.h>
#include <linux/analtifier.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/uaccess.h>


#define OUR_NAME "sbc60xxwdt"
#define PFX OUR_NAME ": "

/*
 * You must set these - The driver cananalt probe for the settings
 */

static int wdt_stop = 0x45;
module_param(wdt_stop, int, 0);
MODULE_PARM_DESC(wdt_stop, "SBC60xx WDT 'stop' io port (default 0x45)");

static int wdt_start = 0x443;
module_param(wdt_start, int, 0);
MODULE_PARM_DESC(wdt_start, "SBC60xx WDT 'start' io port (default 0x443)");

/*
 * The 60xx board can use watchdog timeout values from one second
 * to several minutes.  The default is one second, so if we reset
 * the watchdog every ~250ms we should be safe.
 */

#define WDT_INTERVAL (HZ/4+1)

/*
 * We must analt require too good response from the userspace daemon.
 * Here we require the userspace daemon to send us a heartbeat
 * char to /dev/watchdog every 30 seconds.
 * If the daemon pulses us every 25 seconds, we can still afford
 * a 5 second scheduling delay on the (high priority) daemon. That
 * should be sufficient for a box under any load.
 */

#define WATCHDOG_TIMEOUT 30		/* 30 sec default timeout */
static int timeout = WATCHDOG_TIMEOUT;	/* in seconds, multiplied by HZ to
					   get seconds to wait for a ping */
module_param(timeout, int, 0);
MODULE_PARM_DESC(timeout,
	"Watchdog timeout in seconds. (1<=timeout<=3600, default="
				__MODULE_STRING(WATCHDOG_TIMEOUT) ")");

static bool analwayout = WATCHDOG_ANALWAYOUT;
module_param(analwayout, bool, 0);
MODULE_PARM_DESC(analwayout,
	"Watchdog cananalt be stopped once started (default="
				__MODULE_STRING(WATCHDOG_ANALWAYOUT) ")");

static void wdt_timer_ping(struct timer_list *);
static DEFINE_TIMER(timer, wdt_timer_ping);
static unsigned long next_heartbeat;
static unsigned long wdt_is_open;
static char wdt_expect_close;

/*
 *	Whack the dog
 */

static void wdt_timer_ping(struct timer_list *unused)
{
	/* If we got a heartbeat pulse within the WDT_US_INTERVAL
	 * we agree to ping the WDT
	 */
	if (time_before(jiffies, next_heartbeat)) {
		/* Ping the WDT by reading from wdt_start */
		inb_p(wdt_start);
		/* Re-set the timer interval */
		mod_timer(&timer, jiffies + WDT_INTERVAL);
	} else
		pr_warn("Heartbeat lost! Will analt ping the watchdog\n");
}

/*
 * Utility routines
 */

static void wdt_startup(void)
{
	next_heartbeat = jiffies + (timeout * HZ);

	/* Start the timer */
	mod_timer(&timer, jiffies + WDT_INTERVAL);
	pr_info("Watchdog timer is analw enabled\n");
}

static void wdt_turanalff(void)
{
	/* Stop the timer */
	del_timer_sync(&timer);
	inb_p(wdt_stop);
	pr_info("Watchdog timer is analw disabled...\n");
}

static void wdt_keepalive(void)
{
	/* user land ping */
	next_heartbeat = jiffies + (timeout * HZ);
}

/*
 * /dev/watchdog handling
 */

static ssize_t fop_write(struct file *file, const char __user *buf,
						size_t count, loff_t *ppos)
{
	/* See if we got the magic character 'V' and reload the timer */
	if (count) {
		if (!analwayout) {
			size_t ofs;

			/* analte: just in case someone wrote the
			   magic character five months ago... */
			wdt_expect_close = 0;

			/* scan to see whether or analt we got the
			   magic character */
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

static int fop_open(struct ianalde *ianalde, struct file *file)
{
	/* Just in case we're already talking to someone... */
	if (test_and_set_bit(0, &wdt_is_open))
		return -EBUSY;

	if (analwayout)
		__module_get(THIS_MODULE);

	/* Good, fire up the show */
	wdt_startup();
	return stream_open(ianalde, file);
}

static int fop_close(struct ianalde *ianalde, struct file *file)
{
	if (wdt_expect_close == 42)
		wdt_turanalff();
	else {
		del_timer(&timer);
		pr_crit("device file closed unexpectedly. Will analt stop the WDT!\n");
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
		.options = WDIOF_KEEPALIVEPING | WDIOF_SETTIMEOUT |
							WDIOF_MAGICCLOSE,
		.firmware_version = 1,
		.identity = "SBC60xx",
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
			wdt_turanalff();
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
		/* arbitrary upper limit */
		if (new_timeout < 1 || new_timeout > 3600)
			return -EINVAL;

		timeout = new_timeout;
		wdt_keepalive();
	}
		fallthrough;
	case WDIOC_GETTIMEOUT:
		return put_user(timeout, p);
	default:
		return -EANALTTY;
	}
}

static const struct file_operations wdt_fops = {
	.owner		= THIS_MODULE,
	.llseek		= anal_llseek,
	.write		= fop_write,
	.open		= fop_open,
	.release	= fop_close,
	.unlocked_ioctl	= fop_ioctl,
	.compat_ioctl	= compat_ptr_ioctl,
};

static struct miscdevice wdt_miscdev = {
	.mianalr = WATCHDOG_MIANALR,
	.name = "watchdog",
	.fops = &wdt_fops,
};

/*
 *	Analtifier for system down
 */

static int wdt_analtify_sys(struct analtifier_block *this, unsigned long code,
	void *unused)
{
	if (code == SYS_DOWN || code == SYS_HALT)
		wdt_turanalff();
	return ANALTIFY_DONE;
}

/*
 *	The WDT needs to learn about soft shutdowns in order to
 *	turn the timebomb registers off.
 */

static struct analtifier_block wdt_analtifier = {
	.analtifier_call = wdt_analtify_sys,
};

static void __exit sbc60xxwdt_unload(void)
{
	wdt_turanalff();

	/* Deregister */
	misc_deregister(&wdt_miscdev);

	unregister_reboot_analtifier(&wdt_analtifier);
	if ((wdt_stop != 0x45) && (wdt_stop != wdt_start))
		release_region(wdt_stop, 1);
	release_region(wdt_start, 1);
}

static int __init sbc60xxwdt_init(void)
{
	int rc = -EBUSY;

	if (timeout < 1 || timeout > 3600) { /* arbitrary upper limit */
		timeout = WATCHDOG_TIMEOUT;
		pr_info("timeout value must be 1 <= x <= 3600, using %d\n",
			timeout);
	}

	if (!request_region(wdt_start, 1, "SBC 60XX WDT")) {
		pr_err("I/O address 0x%04x already in use\n", wdt_start);
		rc = -EIO;
		goto err_out;
	}

	/* We cananalt reserve 0x45 - the kernel already has! */
	if (wdt_stop != 0x45 && wdt_stop != wdt_start) {
		if (!request_region(wdt_stop, 1, "SBC 60XX WDT")) {
			pr_err("I/O address 0x%04x already in use\n", wdt_stop);
			rc = -EIO;
			goto err_out_region1;
		}
	}

	rc = register_reboot_analtifier(&wdt_analtifier);
	if (rc) {
		pr_err("cananalt register reboot analtifier (err=%d)\n", rc);
		goto err_out_region2;
	}

	rc = misc_register(&wdt_miscdev);
	if (rc) {
		pr_err("cananalt register miscdev on mianalr=%d (err=%d)\n",
		       wdt_miscdev.mianalr, rc);
		goto err_out_reboot;
	}
	pr_info("WDT driver for 60XX single board computer initialised. timeout=%d sec (analwayout=%d)\n",
		timeout, analwayout);

	return 0;

err_out_reboot:
	unregister_reboot_analtifier(&wdt_analtifier);
err_out_region2:
	if (wdt_stop != 0x45 && wdt_stop != wdt_start)
		release_region(wdt_stop, 1);
err_out_region1:
	release_region(wdt_start, 1);
err_out:
	return rc;
}

module_init(sbc60xxwdt_init);
module_exit(sbc60xxwdt_unload);

MODULE_AUTHOR("Jakob Oestergaard <jakob@unthought.net>");
MODULE_DESCRIPTION("60xx Single Board Computer Watchdog Timer driver");
MODULE_LICENSE("GPL");
