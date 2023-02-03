// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	W83977F Watchdog Timer Driver for Winbond W83977F I/O Chip
 *
 *	(c) Copyright 2005  Jose Goncalves <jose.goncalves@inov.pt>
 *
 *      Based on w83877f_wdt.c by Scott Jennings,
 *           and wdt977.c by Woody Suwalski
 *
 *			-----------------------
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/watchdog.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/uaccess.h>
#include <linux/io.h>


#define WATCHDOG_VERSION  "1.00"
#define WATCHDOG_NAME     "W83977F WDT"

#define IO_INDEX_PORT     0x3F0
#define IO_DATA_PORT      (IO_INDEX_PORT+1)

#define UNLOCK_DATA       0x87
#define LOCK_DATA         0xAA
#define DEVICE_REGISTER   0x07

#define	DEFAULT_TIMEOUT   45		/* default timeout in seconds */

static	int timeout = DEFAULT_TIMEOUT;
static	int timeoutW;			/* timeout in watchdog counter units */
static	unsigned long timer_alive;
static	int testmode;
static	char expect_close;
static	DEFINE_SPINLOCK(spinlock);

module_param(timeout, int, 0);
MODULE_PARM_DESC(timeout,
		"Watchdog timeout in seconds (15..7635), default="
				__MODULE_STRING(DEFAULT_TIMEOUT) ")");
module_param(testmode, int, 0);
MODULE_PARM_DESC(testmode, "Watchdog testmode (1 = no reboot), default=0");

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout,
		"Watchdog cannot be stopped once started (default="
				__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

/*
 * Start the watchdog
 */

static int wdt_start(void)
{
	unsigned long flags;

	spin_lock_irqsave(&spinlock, flags);

	/* Unlock the SuperIO chip */
	outb_p(UNLOCK_DATA, IO_INDEX_PORT);
	outb_p(UNLOCK_DATA, IO_INDEX_PORT);

	/*
	 * Select device Aux2 (device=8) to set watchdog regs F2, F3 and F4.
	 * F2 has the timeout in watchdog counter units.
	 * F3 is set to enable watchdog LED blink at timeout.
	 * F4 is used to just clear the TIMEOUT'ed state (bit 0).
	 */
	outb_p(DEVICE_REGISTER, IO_INDEX_PORT);
	outb_p(0x08, IO_DATA_PORT);
	outb_p(0xF2, IO_INDEX_PORT);
	outb_p(timeoutW, IO_DATA_PORT);
	outb_p(0xF3, IO_INDEX_PORT);
	outb_p(0x08, IO_DATA_PORT);
	outb_p(0xF4, IO_INDEX_PORT);
	outb_p(0x00, IO_DATA_PORT);

	/* Set device Aux2 active */
	outb_p(0x30, IO_INDEX_PORT);
	outb_p(0x01, IO_DATA_PORT);

	/*
	 * Select device Aux1 (dev=7) to set GP16 as the watchdog output
	 * (in reg E6) and GP13 as the watchdog LED output (in reg E3).
	 * Map GP16 at pin 119.
	 * In test mode watch the bit 0 on F4 to indicate "triggered" or
	 * check watchdog LED on SBC.
	 */
	outb_p(DEVICE_REGISTER, IO_INDEX_PORT);
	outb_p(0x07, IO_DATA_PORT);
	if (!testmode) {
		unsigned pin_map;

		outb_p(0xE6, IO_INDEX_PORT);
		outb_p(0x0A, IO_DATA_PORT);
		outb_p(0x2C, IO_INDEX_PORT);
		pin_map = inb_p(IO_DATA_PORT);
		pin_map |= 0x10;
		pin_map &= ~(0x20);
		outb_p(0x2C, IO_INDEX_PORT);
		outb_p(pin_map, IO_DATA_PORT);
	}
	outb_p(0xE3, IO_INDEX_PORT);
	outb_p(0x08, IO_DATA_PORT);

	/* Set device Aux1 active */
	outb_p(0x30, IO_INDEX_PORT);
	outb_p(0x01, IO_DATA_PORT);

	/* Lock the SuperIO chip */
	outb_p(LOCK_DATA, IO_INDEX_PORT);

	spin_unlock_irqrestore(&spinlock, flags);

	pr_info("activated\n");

	return 0;
}

/*
 * Stop the watchdog
 */

static int wdt_stop(void)
{
	unsigned long flags;

	spin_lock_irqsave(&spinlock, flags);

	/* Unlock the SuperIO chip */
	outb_p(UNLOCK_DATA, IO_INDEX_PORT);
	outb_p(UNLOCK_DATA, IO_INDEX_PORT);

	/*
	 * Select device Aux2 (device=8) to set watchdog regs F2, F3 and F4.
	 * F2 is reset to its default value (watchdog timer disabled).
	 * F3 is reset to its default state.
	 * F4 clears the TIMEOUT'ed state (bit 0) - back to default.
	 */
	outb_p(DEVICE_REGISTER, IO_INDEX_PORT);
	outb_p(0x08, IO_DATA_PORT);
	outb_p(0xF2, IO_INDEX_PORT);
	outb_p(0xFF, IO_DATA_PORT);
	outb_p(0xF3, IO_INDEX_PORT);
	outb_p(0x00, IO_DATA_PORT);
	outb_p(0xF4, IO_INDEX_PORT);
	outb_p(0x00, IO_DATA_PORT);
	outb_p(0xF2, IO_INDEX_PORT);
	outb_p(0x00, IO_DATA_PORT);

	/*
	 * Select device Aux1 (dev=7) to set GP16 (in reg E6) and
	 * Gp13 (in reg E3) as inputs.
	 */
	outb_p(DEVICE_REGISTER, IO_INDEX_PORT);
	outb_p(0x07, IO_DATA_PORT);
	if (!testmode) {
		outb_p(0xE6, IO_INDEX_PORT);
		outb_p(0x01, IO_DATA_PORT);
	}
	outb_p(0xE3, IO_INDEX_PORT);
	outb_p(0x01, IO_DATA_PORT);

	/* Lock the SuperIO chip */
	outb_p(LOCK_DATA, IO_INDEX_PORT);

	spin_unlock_irqrestore(&spinlock, flags);

	pr_info("shutdown\n");

	return 0;
}

/*
 * Send a keepalive ping to the watchdog
 * This is done by simply re-writing the timeout to reg. 0xF2
 */

static int wdt_keepalive(void)
{
	unsigned long flags;

	spin_lock_irqsave(&spinlock, flags);

	/* Unlock the SuperIO chip */
	outb_p(UNLOCK_DATA, IO_INDEX_PORT);
	outb_p(UNLOCK_DATA, IO_INDEX_PORT);

	/* Select device Aux2 (device=8) to kick watchdog reg F2 */
	outb_p(DEVICE_REGISTER, IO_INDEX_PORT);
	outb_p(0x08, IO_DATA_PORT);
	outb_p(0xF2, IO_INDEX_PORT);
	outb_p(timeoutW, IO_DATA_PORT);

	/* Lock the SuperIO chip */
	outb_p(LOCK_DATA, IO_INDEX_PORT);

	spin_unlock_irqrestore(&spinlock, flags);

	return 0;
}

/*
 * Set the watchdog timeout value
 */

static int wdt_set_timeout(int t)
{
	unsigned int tmrval;

	/*
	 * Convert seconds to watchdog counter time units, rounding up.
	 * On PCM-5335 watchdog units are 30 seconds/step with 15 sec startup
	 * value. This information is supplied in the PCM-5335 manual and was
	 * checked by me on a real board. This is a bit strange because W83977f
	 * datasheet says counter unit is in minutes!
	 */
	if (t < 15)
		return -EINVAL;

	tmrval = ((t + 15) + 29) / 30;

	if (tmrval > 255)
		return -EINVAL;

	/*
	 * timeout is the timeout in seconds,
	 * timeoutW is the timeout in watchdog counter units.
	 */
	timeoutW = tmrval;
	timeout = (timeoutW * 30) - 15;
	return 0;
}

/*
 * Get the watchdog status
 */

static int wdt_get_status(int *status)
{
	int new_status;
	unsigned long flags;

	spin_lock_irqsave(&spinlock, flags);

	/* Unlock the SuperIO chip */
	outb_p(UNLOCK_DATA, IO_INDEX_PORT);
	outb_p(UNLOCK_DATA, IO_INDEX_PORT);

	/* Select device Aux2 (device=8) to read watchdog reg F4 */
	outb_p(DEVICE_REGISTER, IO_INDEX_PORT);
	outb_p(0x08, IO_DATA_PORT);
	outb_p(0xF4, IO_INDEX_PORT);
	new_status = inb_p(IO_DATA_PORT);

	/* Lock the SuperIO chip */
	outb_p(LOCK_DATA, IO_INDEX_PORT);

	spin_unlock_irqrestore(&spinlock, flags);

	*status = 0;
	if (new_status & 1)
		*status |= WDIOF_CARDRESET;

	return 0;
}


/*
 *	/dev/watchdog handling
 */

static int wdt_open(struct inode *inode, struct file *file)
{
	/* If the watchdog is alive we don't need to start it again */
	if (test_and_set_bit(0, &timer_alive))
		return -EBUSY;

	if (nowayout)
		__module_get(THIS_MODULE);

	wdt_start();
	return stream_open(inode, file);
}

static int wdt_release(struct inode *inode, struct file *file)
{
	/*
	 * Shut off the timer.
	 * Lock it in if it's a module and we set nowayout
	 */
	if (expect_close == 42) {
		wdt_stop();
		clear_bit(0, &timer_alive);
	} else {
		wdt_keepalive();
		pr_crit("unexpected close, not stopping watchdog!\n");
	}
	expect_close = 0;
	return 0;
}

/*
 *      wdt_write:
 *      @file: file handle to the watchdog
 *      @buf: buffer to write (unused as data does not matter here
 *      @count: count of bytes
 *      @ppos: pointer to the position to write. No seeks allowed
 *
 *      A write to a watchdog device is defined as a keepalive signal. Any
 *      write of data will do, as we don't define content meaning.
 */

static ssize_t wdt_write(struct file *file, const char __user *buf,
			    size_t count, loff_t *ppos)
{
	/* See if we got the magic character 'V' and reload the timer */
	if (count) {
		if (!nowayout) {
			size_t ofs;

			/* note: just in case someone wrote the
			   magic character long ago */
			expect_close = 0;

			/* scan to see whether or not we got the
			   magic character */
			for (ofs = 0; ofs != count; ofs++) {
				char c;
				if (get_user(c, buf + ofs))
					return -EFAULT;
				if (c == 'V')
					expect_close = 42;
			}
		}

		/* someone wrote to us, we should restart timer */
		wdt_keepalive();
	}
	return count;
}

/*
 *      wdt_ioctl:
 *      @inode: inode of the device
 *      @file: file handle to the device
 *      @cmd: watchdog command
 *      @arg: argument pointer
 *
 *      The watchdog API defines a common set of functions for all watchdogs
 *      according to their available features.
 */

static const struct watchdog_info ident = {
	.options = WDIOF_SETTIMEOUT | WDIOF_MAGICCLOSE | WDIOF_KEEPALIVEPING,
	.firmware_version =	1,
	.identity = WATCHDOG_NAME,
};

static long wdt_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int status;
	int new_options, retval = -EINVAL;
	int new_timeout;
	union {
		struct watchdog_info __user *ident;
		int __user *i;
	} uarg;

	uarg.i = (int __user *)arg;

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		return copy_to_user(uarg.ident, &ident,
						sizeof(ident)) ? -EFAULT : 0;

	case WDIOC_GETSTATUS:
		wdt_get_status(&status);
		return put_user(status, uarg.i);

	case WDIOC_GETBOOTSTATUS:
		return put_user(0, uarg.i);

	case WDIOC_SETOPTIONS:
		if (get_user(new_options, uarg.i))
			return -EFAULT;

		if (new_options & WDIOS_DISABLECARD) {
			wdt_stop();
			retval = 0;
		}

		if (new_options & WDIOS_ENABLECARD) {
			wdt_start();
			retval = 0;
		}

		return retval;

	case WDIOC_KEEPALIVE:
		wdt_keepalive();
		return 0;

	case WDIOC_SETTIMEOUT:
		if (get_user(new_timeout, uarg.i))
			return -EFAULT;

		if (wdt_set_timeout(new_timeout))
			return -EINVAL;

		wdt_keepalive();
		fallthrough;

	case WDIOC_GETTIMEOUT:
		return put_user(timeout, uarg.i);

	default:
		return -ENOTTY;

	}
}

static int wdt_notify_sys(struct notifier_block *this, unsigned long code,
	void *unused)
{
	if (code == SYS_DOWN || code == SYS_HALT)
		wdt_stop();
	return NOTIFY_DONE;
}

static const struct file_operations wdt_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.write		= wdt_write,
	.unlocked_ioctl	= wdt_ioctl,
	.compat_ioctl	= compat_ptr_ioctl,
	.open		= wdt_open,
	.release	= wdt_release,
};

static struct miscdevice wdt_miscdev = {
	.minor		= WATCHDOG_MINOR,
	.name		= "watchdog",
	.fops		= &wdt_fops,
};

static struct notifier_block wdt_notifier = {
	.notifier_call = wdt_notify_sys,
};

static int __init w83977f_wdt_init(void)
{
	int rc;

	pr_info("driver v%s\n", WATCHDOG_VERSION);

	/*
	 * Check that the timeout value is within it's range;
	 * if not reset to the default
	 */
	if (wdt_set_timeout(timeout)) {
		wdt_set_timeout(DEFAULT_TIMEOUT);
		pr_info("timeout value must be 15 <= timeout <= 7635, using %d\n",
			DEFAULT_TIMEOUT);
	}

	if (!request_region(IO_INDEX_PORT, 2, WATCHDOG_NAME)) {
		pr_err("I/O address 0x%04x already in use\n", IO_INDEX_PORT);
		rc = -EIO;
		goto err_out;
	}

	rc = register_reboot_notifier(&wdt_notifier);
	if (rc) {
		pr_err("cannot register reboot notifier (err=%d)\n", rc);
		goto err_out_region;
	}

	rc = misc_register(&wdt_miscdev);
	if (rc) {
		pr_err("cannot register miscdev on minor=%d (err=%d)\n",
		       wdt_miscdev.minor, rc);
		goto err_out_reboot;
	}

	pr_info("initialized. timeout=%d sec (nowayout=%d testmode=%d)\n",
		timeout, nowayout, testmode);

	return 0;

err_out_reboot:
	unregister_reboot_notifier(&wdt_notifier);
err_out_region:
	release_region(IO_INDEX_PORT, 2);
err_out:
	return rc;
}

static void __exit w83977f_wdt_exit(void)
{
	wdt_stop();
	misc_deregister(&wdt_miscdev);
	unregister_reboot_notifier(&wdt_notifier);
	release_region(IO_INDEX_PORT, 2);
}

module_init(w83977f_wdt_init);
module_exit(w83977f_wdt_exit);

MODULE_AUTHOR("Jose Goncalves <jose.goncalves@inov.pt>");
MODULE_DESCRIPTION("Driver for watchdog timer in W83977F I/O chip");
MODULE_LICENSE("GPL");
