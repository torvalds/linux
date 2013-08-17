/*
 *	Watchdog Timer Driver
 *	   for ITE IT87xx Environment Control - Low Pin Count Input / Output
 *
 *	(c) Copyright 2007  Oliver Schuster <olivers137@aol.com>
 *
 *	Based on softdog.c	by Alan Cox,
 *		 83977f_wdt.c	by Jose Goncalves,
 *		 it87.c		by Chris Gauthron, Jean Delvare
 *
 *	Data-sheets: Publicly available at the ITE website
 *		    http://www.ite.com.tw/
 *
 *	Support of the watchdog timers, which are available on
 *	IT8702, IT8712, IT8716, IT8718, IT8720, IT8721 and IT8726.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program; if not, write to the Free Software
 *	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
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


#define WATCHDOG_VERSION	"1.14"
#define WATCHDOG_NAME		"IT87 WDT"
#define DRIVER_VERSION		WATCHDOG_NAME " driver, v" WATCHDOG_VERSION "\n"
#define WD_MAGIC		'V'

/* Defaults for Module Parameter */
#define DEFAULT_NOGAMEPORT	0
#define DEFAULT_EXCLUSIVE	1
#define DEFAULT_TIMEOUT		60
#define DEFAULT_TESTMODE	0
#define DEFAULT_NOWAYOUT	WATCHDOG_NOWAYOUT

/* IO Ports */
#define REG		0x2e
#define VAL		0x2f

/* Logical device Numbers LDN */
#define GPIO		0x07
#define GAMEPORT	0x09
#define CIR		0x0a

/* Configuration Registers and Functions */
#define LDNREG		0x07
#define CHIPID		0x20
#define CHIPREV		0x22
#define ACTREG		0x30
#define BASEREG		0x60

/* Chip Id numbers */
#define NO_DEV_ID	0xffff
#define IT8702_ID	0x8702
#define IT8705_ID	0x8705
#define IT8712_ID	0x8712
#define IT8716_ID	0x8716
#define IT8718_ID	0x8718
#define IT8720_ID	0x8720
#define IT8721_ID	0x8721
#define IT8726_ID	0x8726	/* the data sheet suggest wrongly 0x8716 */

/* GPIO Configuration Registers LDN=0x07 */
#define WDTCTRL		0x71
#define WDTCFG		0x72
#define WDTVALLSB	0x73
#define WDTVALMSB	0x74

/* GPIO Bits WDTCTRL */
#define WDT_CIRINT	0x80
#define WDT_MOUSEINT	0x40
#define WDT_KYBINT	0x20
#define WDT_GAMEPORT	0x10 /* not in it8718, it8720, it8721 */
#define WDT_FORCE	0x02
#define WDT_ZERO	0x01

/* GPIO Bits WDTCFG */
#define WDT_TOV1	0x80
#define WDT_KRST	0x40
#define WDT_TOVE	0x20
#define WDT_PWROK	0x10 /* not in it8721 */
#define WDT_INT_MASK	0x0f

/* CIR Configuration Register LDN=0x0a */
#define CIR_ILS		0x70

/* The default Base address is not always available, we use this */
#define CIR_BASE	0x0208

/* CIR Controller */
#define CIR_DR(b)	(b)
#define CIR_IER(b)	(b + 1)
#define CIR_RCR(b)	(b + 2)
#define CIR_TCR1(b)	(b + 3)
#define CIR_TCR2(b)	(b + 4)
#define CIR_TSR(b)	(b + 5)
#define CIR_RSR(b)	(b + 6)
#define CIR_BDLR(b)	(b + 5)
#define CIR_BDHR(b)	(b + 6)
#define CIR_IIR(b)	(b + 7)

/* Default Base address of Game port */
#define GP_BASE_DEFAULT	0x0201

/* wdt_status */
#define WDTS_TIMER_RUN	0
#define WDTS_DEV_OPEN	1
#define WDTS_KEEPALIVE	2
#define WDTS_LOCKED	3
#define WDTS_USE_GP	4
#define WDTS_EXPECTED	5

static	unsigned int base, gpact, ciract, max_units, chip_type;
static	unsigned long wdt_status;

static	int nogameport = DEFAULT_NOGAMEPORT;
static	int exclusive  = DEFAULT_EXCLUSIVE;
static	int timeout    = DEFAULT_TIMEOUT;
static	int testmode   = DEFAULT_TESTMODE;
static	bool nowayout   = DEFAULT_NOWAYOUT;

module_param(nogameport, int, 0);
MODULE_PARM_DESC(nogameport, "Forbid the activation of game port, default="
		__MODULE_STRING(DEFAULT_NOGAMEPORT));
module_param(exclusive, int, 0);
MODULE_PARM_DESC(exclusive, "Watchdog exclusive device open, default="
		__MODULE_STRING(DEFAULT_EXCLUSIVE));
module_param(timeout, int, 0);
MODULE_PARM_DESC(timeout, "Watchdog timeout in seconds, default="
		__MODULE_STRING(DEFAULT_TIMEOUT));
module_param(testmode, int, 0);
MODULE_PARM_DESC(testmode, "Watchdog test mode (1 = no reboot), default="
		__MODULE_STRING(DEFAULT_TESTMODE));
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started, default="
		__MODULE_STRING(WATCHDOG_NOWAYOUT));

/* Superio Chip */

static inline int superio_enter(void)
{
	/*
	 * Try to reserve REG and REG + 1 for exclusive access.
	 */
	if (!request_muxed_region(REG, 2, WATCHDOG_NAME))
		return -EBUSY;

	outb(0x87, REG);
	outb(0x01, REG);
	outb(0x55, REG);
	outb(0x55, REG);
	return 0;
}

static inline void superio_exit(void)
{
	outb(0x02, REG);
	outb(0x02, VAL);
	release_region(REG, 2);
}

static inline void superio_select(int ldn)
{
	outb(LDNREG, REG);
	outb(ldn, VAL);
}

static inline int superio_inb(int reg)
{
	outb(reg, REG);
	return inb(VAL);
}

static inline void superio_outb(int val, int reg)
{
	outb(reg, REG);
	outb(val, VAL);
}

static inline int superio_inw(int reg)
{
	int val;
	outb(reg++, REG);
	val = inb(VAL) << 8;
	outb(reg, REG);
	val |= inb(VAL);
	return val;
}

static inline void superio_outw(int val, int reg)
{
	outb(reg++, REG);
	outb(val >> 8, VAL);
	outb(reg, REG);
	outb(val, VAL);
}

/* Internal function, should be called after superio_select(GPIO) */
static void wdt_update_timeout(void)
{
	unsigned char cfg = WDT_KRST;
	int tm = timeout;

	if (testmode)
		cfg = 0;

	if (tm <= max_units)
		cfg |= WDT_TOV1;
	else
		tm /= 60;

	if (chip_type != IT8721_ID)
		cfg |= WDT_PWROK;

	superio_outb(cfg, WDTCFG);
	superio_outb(tm, WDTVALLSB);
	if (max_units > 255)
		superio_outb(tm>>8, WDTVALMSB);
}

static int wdt_round_time(int t)
{
	t += 59;
	t -= t % 60;
	return t;
}

/* watchdog timer handling */

static void wdt_keepalive(void)
{
	if (test_bit(WDTS_USE_GP, &wdt_status))
		inb(base);
	else
		/* The timer reloads with around 5 msec delay */
		outb(0x55, CIR_DR(base));
	set_bit(WDTS_KEEPALIVE, &wdt_status);
}

static int wdt_start(void)
{
	int ret = superio_enter();
	if (ret)
		return ret;

	superio_select(GPIO);
	if (test_bit(WDTS_USE_GP, &wdt_status))
		superio_outb(WDT_GAMEPORT, WDTCTRL);
	else
		superio_outb(WDT_CIRINT, WDTCTRL);
	wdt_update_timeout();

	superio_exit();

	return 0;
}

static int wdt_stop(void)
{
	int ret = superio_enter();
	if (ret)
		return ret;

	superio_select(GPIO);
	superio_outb(0x00, WDTCTRL);
	superio_outb(WDT_TOV1, WDTCFG);
	superio_outb(0x00, WDTVALLSB);
	if (max_units > 255)
		superio_outb(0x00, WDTVALMSB);

	superio_exit();
	return 0;
}

/**
 *	wdt_set_timeout - set a new timeout value with watchdog ioctl
 *	@t: timeout value in seconds
 *
 *	The hardware device has a 8 or 16 bit watchdog timer (depends on
 *	chip version) that can be configured to count seconds or minutes.
 *
 *	Used within WDIOC_SETTIMEOUT watchdog device ioctl.
 */

static int wdt_set_timeout(int t)
{
	if (t < 1 || t > max_units * 60)
		return -EINVAL;

	if (t > max_units)
		timeout = wdt_round_time(t);
	else
		timeout = t;

	if (test_bit(WDTS_TIMER_RUN, &wdt_status)) {
		int ret = superio_enter();
		if (ret)
			return ret;

		superio_select(GPIO);
		wdt_update_timeout();
		superio_exit();
	}
	return 0;
}

/**
 *	wdt_get_status - determines the status supported by watchdog ioctl
 *	@status: status returned to user space
 *
 *	The status bit of the device does not allow to distinguish
 *	between a regular system reset and a watchdog forced reset.
 *	But, in test mode it is useful, so it is supported through
 *	WDIOC_GETSTATUS watchdog ioctl. Additionally the driver
 *	reports the keepalive signal and the acception of the magic.
 *
 *	Used within WDIOC_GETSTATUS watchdog device ioctl.
 */

static int wdt_get_status(int *status)
{
	*status = 0;
	if (testmode) {
		int ret = superio_enter();
		if (ret)
			return ret;

		superio_select(GPIO);
		if (superio_inb(WDTCTRL) & WDT_ZERO) {
			superio_outb(0x00, WDTCTRL);
			clear_bit(WDTS_TIMER_RUN, &wdt_status);
			*status |= WDIOF_CARDRESET;
		}

		superio_exit();
	}
	if (test_and_clear_bit(WDTS_KEEPALIVE, &wdt_status))
		*status |= WDIOF_KEEPALIVEPING;
	if (test_bit(WDTS_EXPECTED, &wdt_status))
		*status |= WDIOF_MAGICCLOSE;
	return 0;
}

/* /dev/watchdog handling */

/**
 *	wdt_open - watchdog file_operations .open
 *	@inode: inode of the device
 *	@file: file handle to the device
 *
 *	The watchdog timer starts by opening the device.
 *
 *	Used within the file operation of the watchdog device.
 */

static int wdt_open(struct inode *inode, struct file *file)
{
	if (exclusive && test_and_set_bit(WDTS_DEV_OPEN, &wdt_status))
		return -EBUSY;
	if (!test_and_set_bit(WDTS_TIMER_RUN, &wdt_status)) {
		int ret;
		if (nowayout && !test_and_set_bit(WDTS_LOCKED, &wdt_status))
			__module_get(THIS_MODULE);

		ret = wdt_start();
		if (ret) {
			clear_bit(WDTS_LOCKED, &wdt_status);
			clear_bit(WDTS_TIMER_RUN, &wdt_status);
			clear_bit(WDTS_DEV_OPEN, &wdt_status);
			return ret;
		}
	}
	return nonseekable_open(inode, file);
}

/**
 *	wdt_release - watchdog file_operations .release
 *	@inode: inode of the device
 *	@file: file handle to the device
 *
 *	Closing the watchdog device either stops the watchdog timer
 *	or in the case, that nowayout is set or the magic character
 *	wasn't written, a critical warning about an running watchdog
 *	timer is given.
 *
 *	Used within the file operation of the watchdog device.
 */

static int wdt_release(struct inode *inode, struct file *file)
{
	if (test_bit(WDTS_TIMER_RUN, &wdt_status)) {
		if (test_and_clear_bit(WDTS_EXPECTED, &wdt_status)) {
			int ret = wdt_stop();
			if (ret) {
				/*
				 * Stop failed. Just keep the watchdog alive
				 * and hope nothing bad happens.
				 */
				set_bit(WDTS_EXPECTED, &wdt_status);
				wdt_keepalive();
				return ret;
			}
			clear_bit(WDTS_TIMER_RUN, &wdt_status);
		} else {
			wdt_keepalive();
			pr_crit("unexpected close, not stopping watchdog!\n");
		}
	}
	clear_bit(WDTS_DEV_OPEN, &wdt_status);
	return 0;
}

/**
 *	wdt_write - watchdog file_operations .write
 *	@file: file handle to the watchdog
 *	@buf: buffer to write
 *	@count: count of bytes
 *	@ppos: pointer to the position to write. No seeks allowed
 *
 *	A write to a watchdog device is defined as a keepalive signal. Any
 *	write of data will do, as we don't define content meaning.
 *
 *	Used within the file operation of the watchdog device.
 */

static ssize_t wdt_write(struct file *file, const char __user *buf,
			    size_t count, loff_t *ppos)
{
	if (count) {
		clear_bit(WDTS_EXPECTED, &wdt_status);
		wdt_keepalive();
	}
	if (!nowayout) {
		size_t ofs;

	/* note: just in case someone wrote the magic character long ago */
		for (ofs = 0; ofs != count; ofs++) {
			char c;
			if (get_user(c, buf + ofs))
				return -EFAULT;
			if (c == WD_MAGIC)
				set_bit(WDTS_EXPECTED, &wdt_status);
		}
	}
	return count;
}

static const struct watchdog_info ident = {
	.options = WDIOF_SETTIMEOUT | WDIOF_MAGICCLOSE | WDIOF_KEEPALIVEPING,
	.firmware_version =	1,
	.identity = WATCHDOG_NAME,
};

/**
 *	wdt_ioctl - watchdog file_operations .unlocked_ioctl
 *	@file: file handle to the device
 *	@cmd: watchdog command
 *	@arg: argument pointer
 *
 *	The watchdog API defines a common set of functions for all watchdogs
 *	according to their available features.
 *
 *	Used within the file operation of the watchdog device.
 */

static long wdt_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int rc = 0, status, new_options, new_timeout;
	union {
		struct watchdog_info __user *ident;
		int __user *i;
	} uarg;

	uarg.i = (int __user *)arg;

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		return copy_to_user(uarg.ident,
				    &ident, sizeof(ident)) ? -EFAULT : 0;

	case WDIOC_GETSTATUS:
		rc = wdt_get_status(&status);
		if (rc)
			return rc;
		return put_user(status, uarg.i);

	case WDIOC_GETBOOTSTATUS:
		return put_user(0, uarg.i);

	case WDIOC_KEEPALIVE:
		wdt_keepalive();
		return 0;

	case WDIOC_SETOPTIONS:
		if (get_user(new_options, uarg.i))
			return -EFAULT;

		switch (new_options) {
		case WDIOS_DISABLECARD:
			if (test_bit(WDTS_TIMER_RUN, &wdt_status)) {
				rc = wdt_stop();
				if (rc)
					return rc;
			}
			clear_bit(WDTS_TIMER_RUN, &wdt_status);
			return 0;

		case WDIOS_ENABLECARD:
			if (!test_and_set_bit(WDTS_TIMER_RUN, &wdt_status)) {
				rc = wdt_start();
				if (rc) {
					clear_bit(WDTS_TIMER_RUN, &wdt_status);
					return rc;
				}
			}
			return 0;

		default:
			return -EFAULT;
		}

	case WDIOC_SETTIMEOUT:
		if (get_user(new_timeout, uarg.i))
			return -EFAULT;
		rc = wdt_set_timeout(new_timeout);
	case WDIOC_GETTIMEOUT:
		if (put_user(timeout, uarg.i))
			return -EFAULT;
		return rc;

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

static int __init it87_wdt_init(void)
{
	int rc = 0;
	int try_gameport = !nogameport;
	u8  chip_rev;
	int gp_rreq_fail = 0;

	wdt_status = 0;

	rc = superio_enter();
	if (rc)
		return rc;

	chip_type = superio_inw(CHIPID);
	chip_rev  = superio_inb(CHIPREV) & 0x0f;
	superio_exit();

	switch (chip_type) {
	case IT8702_ID:
		max_units = 255;
		break;
	case IT8712_ID:
		max_units = (chip_rev < 8) ? 255 : 65535;
		break;
	case IT8716_ID:
	case IT8726_ID:
		max_units = 65535;
		break;
	case IT8718_ID:
	case IT8720_ID:
	case IT8721_ID:
		max_units = 65535;
		try_gameport = 0;
		break;
	case IT8705_ID:
		pr_err("Unsupported Chip found, Chip %04x Revision %02x\n",
		       chip_type, chip_rev);
		return -ENODEV;
	case NO_DEV_ID:
		pr_err("no device\n");
		return -ENODEV;
	default:
		pr_err("Unknown Chip found, Chip %04x Revision %04x\n",
		       chip_type, chip_rev);
		return -ENODEV;
	}

	rc = superio_enter();
	if (rc)
		return rc;

	superio_select(GPIO);
	superio_outb(WDT_TOV1, WDTCFG);
	superio_outb(0x00, WDTCTRL);

	/* First try to get Gameport support */
	if (try_gameport) {
		superio_select(GAMEPORT);
		base = superio_inw(BASEREG);
		if (!base) {
			base = GP_BASE_DEFAULT;
			superio_outw(base, BASEREG);
		}
		gpact = superio_inb(ACTREG);
		superio_outb(0x01, ACTREG);
		if (request_region(base, 1, WATCHDOG_NAME))
			set_bit(WDTS_USE_GP, &wdt_status);
		else
			gp_rreq_fail = 1;
	}

	/* If we haven't Gameport support, try to get CIR support */
	if (!test_bit(WDTS_USE_GP, &wdt_status)) {
		if (!request_region(CIR_BASE, 8, WATCHDOG_NAME)) {
			if (gp_rreq_fail)
				pr_err("I/O Address 0x%04x and 0x%04x already in use\n",
				       base, CIR_BASE);
			else
				pr_err("I/O Address 0x%04x already in use\n",
				       CIR_BASE);
			rc = -EIO;
			goto err_out;
		}
		base = CIR_BASE;

		superio_select(CIR);
		superio_outw(base, BASEREG);
		superio_outb(0x00, CIR_ILS);
		ciract = superio_inb(ACTREG);
		superio_outb(0x01, ACTREG);
		if (gp_rreq_fail) {
			superio_select(GAMEPORT);
			superio_outb(gpact, ACTREG);
		}
	}

	if (timeout < 1 || timeout > max_units * 60) {
		timeout = DEFAULT_TIMEOUT;
		pr_warn("Timeout value out of range, use default %d sec\n",
			DEFAULT_TIMEOUT);
	}

	if (timeout > max_units)
		timeout = wdt_round_time(timeout);

	rc = register_reboot_notifier(&wdt_notifier);
	if (rc) {
		pr_err("Cannot register reboot notifier (err=%d)\n", rc);
		goto err_out_region;
	}

	rc = misc_register(&wdt_miscdev);
	if (rc) {
		pr_err("Cannot register miscdev on minor=%d (err=%d)\n",
		       wdt_miscdev.minor, rc);
		goto err_out_reboot;
	}

	/* Initialize CIR to use it as keepalive source */
	if (!test_bit(WDTS_USE_GP, &wdt_status)) {
		outb(0x00, CIR_RCR(base));
		outb(0xc0, CIR_TCR1(base));
		outb(0x5c, CIR_TCR2(base));
		outb(0x10, CIR_IER(base));
		outb(0x00, CIR_BDHR(base));
		outb(0x01, CIR_BDLR(base));
		outb(0x09, CIR_IER(base));
	}

	pr_info("Chip IT%04x revision %d initialized. timeout=%d sec (nowayout=%d testmode=%d exclusive=%d nogameport=%d)\n",
		chip_type, chip_rev, timeout,
		nowayout, testmode, exclusive, nogameport);

	superio_exit();
	return 0;

err_out_reboot:
	unregister_reboot_notifier(&wdt_notifier);
err_out_region:
	release_region(base, test_bit(WDTS_USE_GP, &wdt_status) ? 1 : 8);
	if (!test_bit(WDTS_USE_GP, &wdt_status)) {
		superio_select(CIR);
		superio_outb(ciract, ACTREG);
	}
err_out:
	if (try_gameport) {
		superio_select(GAMEPORT);
		superio_outb(gpact, ACTREG);
	}

	superio_exit();
	return rc;
}

static void __exit it87_wdt_exit(void)
{
	if (superio_enter() == 0) {
		superio_select(GPIO);
		superio_outb(0x00, WDTCTRL);
		superio_outb(0x00, WDTCFG);
		superio_outb(0x00, WDTVALLSB);
		if (max_units > 255)
			superio_outb(0x00, WDTVALMSB);
		if (test_bit(WDTS_USE_GP, &wdt_status)) {
			superio_select(GAMEPORT);
			superio_outb(gpact, ACTREG);
		} else {
			superio_select(CIR);
			superio_outb(ciract, ACTREG);
		}
		superio_exit();
	}

	misc_deregister(&wdt_miscdev);
	unregister_reboot_notifier(&wdt_notifier);
	release_region(base, test_bit(WDTS_USE_GP, &wdt_status) ? 1 : 8);
}

module_init(it87_wdt_init);
module_exit(it87_wdt_exit);

MODULE_AUTHOR("Oliver Schuster");
MODULE_DESCRIPTION("Hardware Watchdog Device Driver for IT87xx EC-LPC I/O");
MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);
