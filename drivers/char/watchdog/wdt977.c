/*
 *	Wdt977	0.03:	A Watchdog Device for Netwinder W83977AF chip
 *
 *	(c) Copyright 1998 Rebel.com (Woody Suwalski <woody@netwinder.org>)
 *
 *			-----------------------
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *			-----------------------
 *      14-Dec-2001 Matt Domsch <Matt_Domsch@dell.com>
 *           Added nowayout module option to override CONFIG_WATCHDOG_NOWAYOUT
 *	19-Dec-2001 Woody Suwalski: Netwinder fixes, ioctl interface
 *	06-Jan-2002 Woody Suwalski: For compatibility, convert all timeouts
 *				    from minutes to seconds.
 *      07-Jul-2003 Daniele Bellucci: Audit return code of misc_register in
 *                                    nwwatchdog_init.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/init.h>
#include <linux/watchdog.h>
#include <linux/notifier.h>
#include <linux/reboot.h>

#include <asm/io.h>
#include <asm/system.h>
#include <asm/mach-types.h>
#include <asm/uaccess.h>

#define PFX "Wdt977: "
#define WATCHDOG_MINOR	130

#define	DEFAULT_TIMEOUT	60			/* default timeout in seconds */

static	int timeout = DEFAULT_TIMEOUT;
static	int timeoutM;				/* timeout in minutes */
static	unsigned long timer_alive;
static	int testmode;
static	char expect_close;

module_param(timeout, int, 0);
MODULE_PARM_DESC(timeout,"Watchdog timeout in seconds (60..15300), default=" __MODULE_STRING(DEFAULT_TIMEOUT) ")");
module_param(testmode, int, 0);
MODULE_PARM_DESC(testmode,"Watchdog testmode (1 = no reboot), default=0");

#ifdef CONFIG_WATCHDOG_NOWAYOUT
static int nowayout = 1;
#else
static int nowayout = 0;
#endif

module_param(nowayout, int, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default=CONFIG_WATCHDOG_NOWAYOUT)");

/*
 * Start the watchdog
 */

static int wdt977_start(void)
{
	/* unlock the SuperIO chip */
	outb(0x87,0x370);
	outb(0x87,0x370);

	/* select device Aux2 (device=8) and set watchdog regs F2, F3 and F4
	 * F2 has the timeout in minutes
	 * F3 could be set to the POWER LED blink (with GP17 set to PowerLed)
	 *   at timeout, and to reset timer on kbd/mouse activity (not impl.)
	 * F4 is used to just clear the TIMEOUT'ed state (bit 0)
	 */
	outb(0x07,0x370);
	outb(0x08,0x371);
	outb(0xF2,0x370);
	outb(timeoutM,0x371);
	outb(0xF3,0x370);
	outb(0x00,0x371);	/* another setting is 0E for kbd/mouse/LED */
	outb(0xF4,0x370);
	outb(0x00,0x371);

	/* at last select device Aux1 (dev=7) and set GP16 as a watchdog output */
	/* in test mode watch the bit 1 on F4 to indicate "triggered" */
	if (!testmode)
	{
		outb(0x07,0x370);
		outb(0x07,0x371);
		outb(0xE6,0x370);
		outb(0x08,0x371);
	}

	/* lock the SuperIO chip */
	outb(0xAA,0x370);

	printk(KERN_INFO PFX "activated.\n");

	return 0;
}

/*
 * Stop the watchdog
 */

static int wdt977_stop(void)
{
	/* unlock the SuperIO chip */
	outb(0x87,0x370);
	outb(0x87,0x370);

	/* select device Aux2 (device=8) and set watchdog regs F2,F3 and F4
	* F3 is reset to its default state
	* F4 can clear the TIMEOUT'ed state (bit 0) - back to default
	* We can not use GP17 as a PowerLed, as we use its usage as a RedLed
	*/
	outb(0x07,0x370);
	outb(0x08,0x371);
	outb(0xF2,0x370);
	outb(0xFF,0x371);
	outb(0xF3,0x370);
	outb(0x00,0x371);
	outb(0xF4,0x370);
	outb(0x00,0x371);
	outb(0xF2,0x370);
	outb(0x00,0x371);

	/* at last select device Aux1 (dev=7) and set GP16 as a watchdog output */
	outb(0x07,0x370);
	outb(0x07,0x371);
	outb(0xE6,0x370);
	outb(0x08,0x371);

	/* lock the SuperIO chip */
	outb(0xAA,0x370);

	printk(KERN_INFO PFX "shutdown.\n");

	return 0;
}

/*
 * Send a keepalive ping to the watchdog
 * This is done by simply re-writing the timeout to reg. 0xF2
 */

static int wdt977_keepalive(void)
{
	/* unlock the SuperIO chip */
	outb(0x87,0x370);
	outb(0x87,0x370);

	/* select device Aux2 (device=8) and kicks watchdog reg F2 */
	/* F2 has the timeout in minutes */
	outb(0x07,0x370);
	outb(0x08,0x371);
	outb(0xF2,0x370);
	outb(timeoutM,0x371);

	/* lock the SuperIO chip */
	outb(0xAA,0x370);

	return 0;
}

/*
 * Set the watchdog timeout value
 */

static int wdt977_set_timeout(int t)
{
	int tmrval;

	/* convert seconds to minutes, rounding up */
	tmrval = (t + 59) / 60;

	if (machine_is_netwinder()) {
		/* we have a hw bug somewhere, so each 977 minute is actually only 30sec
		 *  this limits the max timeout to half of device max of 255 minutes...
		 */
		tmrval += tmrval;
	}

	if ((tmrval < 1) || (tmrval > 255))
		return -EINVAL;

	/* timeout is the timeout in seconds, timeoutM is the timeout in minutes) */
	timeout = t;
	timeoutM = tmrval;
	return 0;
}

/*
 * Get the watchdog status
 */

static int wdt977_get_status(int *status)
{
	int new_status;

	*status=0;

	/* unlock the SuperIO chip */
	outb(0x87,0x370);
	outb(0x87,0x370);

	/* select device Aux2 (device=8) and read watchdog reg F4 */
	outb(0x07,0x370);
	outb(0x08,0x371);
	outb(0xF4,0x370);
	new_status = inb(0x371);

	/* lock the SuperIO chip */
	outb(0xAA,0x370);

	if (new_status & 1)
		*status |= WDIOF_CARDRESET;

	return 0;
}


/*
 *	/dev/watchdog handling
 */

static int wdt977_open(struct inode *inode, struct file *file)
{
	/* If the watchdog is alive we don't need to start it again */
	if( test_and_set_bit(0,&timer_alive) )
		return -EBUSY;

	if (nowayout)
		__module_get(THIS_MODULE);

	wdt977_start();
	return nonseekable_open(inode, file);
}

static int wdt977_release(struct inode *inode, struct file *file)
{
	/*
	 *	Shut off the timer.
	 * 	Lock it in if it's a module and we set nowayout
	 */
	if (expect_close == 42)
	{
		wdt977_stop();
		clear_bit(0,&timer_alive);
	} else {
		printk(KERN_CRIT PFX "Unexpected close, not stopping watchdog!\n");
		wdt977_keepalive();
	}
	expect_close = 0;
	return 0;
}


/*
 *      wdt977_write:
 *      @file: file handle to the watchdog
 *      @buf: buffer to write (unused as data does not matter here
 *      @count: count of bytes
 *      @ppos: pointer to the position to write. No seeks allowed
 *
 *      A write to a watchdog device is defined as a keepalive signal. Any
 *      write of data will do, as we we don't define content meaning.
 */

static ssize_t wdt977_write(struct file *file, const char __user *buf,
			    size_t count, loff_t *ppos)
{
	if (count) {
		if (!nowayout) {
			size_t i;

			/* In case it was set long ago */
			expect_close = 0;

			for (i = 0; i != count; i++) {
				char c;
				if (get_user(c, buf + i))
					return -EFAULT;
				if (c == 'V')
					expect_close = 42;
			}
		}

		wdt977_keepalive();
	}
	return count;
}

/*
 *      wdt977_ioctl:
 *      @inode: inode of the device
 *      @file: file handle to the device
 *      @cmd: watchdog command
 *      @arg: argument pointer
 *
 *      The watchdog API defines a common set of functions for all watchdogs
 *      according to their available features.
 */

static struct watchdog_info ident = {
	.options =		WDIOF_SETTIMEOUT |
				WDIOF_MAGICCLOSE |
				WDIOF_KEEPALIVEPING,
	.firmware_version =	1,
	.identity =		"Winbond 83977",
};

static int wdt977_ioctl(struct inode *inode, struct file *file,
	unsigned int cmd, unsigned long arg)
{
	int status;
	int new_options, retval = -EINVAL;
	int new_timeout;
	union {
		struct watchdog_info __user *ident;
		int __user *i;
	} uarg;

	uarg.i = (int __user *)arg;

	switch(cmd)
	{
	default:
		return -ENOIOCTLCMD;

	case WDIOC_GETSUPPORT:
		return copy_to_user(uarg.ident, &ident,
			sizeof(ident)) ? -EFAULT : 0;

	case WDIOC_GETSTATUS:
		wdt977_get_status(&status);
		return put_user(status, uarg.i);

	case WDIOC_GETBOOTSTATUS:
		return put_user(0, uarg.i);

	case WDIOC_KEEPALIVE:
		wdt977_keepalive();
		return 0;

	case WDIOC_SETOPTIONS:
		if (get_user (new_options, uarg.i))
			return -EFAULT;

		if (new_options & WDIOS_DISABLECARD) {
			wdt977_stop();
			retval = 0;
		}

		if (new_options & WDIOS_ENABLECARD) {
			wdt977_start();
			retval = 0;
		}

		return retval;

	case WDIOC_SETTIMEOUT:
		if (get_user(new_timeout, uarg.i))
			return -EFAULT;

		if (wdt977_set_timeout(new_timeout))
		    return -EINVAL;

		wdt977_keepalive();
		/* Fall */

	case WDIOC_GETTIMEOUT:
		return put_user(timeout, uarg.i);

	}
}

static int wdt977_notify_sys(struct notifier_block *this, unsigned long code,
	void *unused)
{
	if(code==SYS_DOWN || code==SYS_HALT)
		wdt977_stop();
	return NOTIFY_DONE;
}

static struct file_operations wdt977_fops=
{
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.write		= wdt977_write,
	.ioctl		= wdt977_ioctl,
	.open		= wdt977_open,
	.release	= wdt977_release,
};

static struct miscdevice wdt977_miscdev=
{
	.minor		= WATCHDOG_MINOR,
	.name		= "watchdog",
	.fops		= &wdt977_fops,
};

static struct notifier_block wdt977_notifier = {
	.notifier_call = wdt977_notify_sys,
};

static int __init nwwatchdog_init(void)
{
	int retval;
	if (!machine_is_netwinder())
		return -ENODEV;

	/* Check that the timeout value is within it's range ; if not reset to the default */
	if (wdt977_set_timeout(timeout)) {
		wdt977_set_timeout(DEFAULT_TIMEOUT);
		printk(KERN_INFO PFX "timeout value must be 60<timeout<15300, using %d\n",
			DEFAULT_TIMEOUT);
	}

	retval = register_reboot_notifier(&wdt977_notifier);
	if (retval) {
		printk(KERN_ERR PFX "cannot register reboot notifier (err=%d)\n",
			retval);
		return retval;
	}

	retval = misc_register(&wdt977_miscdev);
	if (retval) {
		printk(KERN_ERR PFX "cannot register miscdev on minor=%d (err=%d)\n",
			WATCHDOG_MINOR, retval);
		unregister_reboot_notifier(&wdt977_notifier);
		return retval;
	}

	printk(KERN_INFO PFX "initialized. timeout=%d sec (nowayout=%d, testmode = %i)\n",
		timeout, nowayout, testmode);

	return 0;
}

static void __exit nwwatchdog_exit(void)
{
	misc_deregister(&wdt977_miscdev);
	unregister_reboot_notifier(&wdt977_notifier);
}

module_init(nwwatchdog_init);
module_exit(nwwatchdog_exit);

MODULE_AUTHOR("Woody Suwalski <woody@netwinder.org>");
MODULE_DESCRIPTION("W83977AF Watchdog driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);
