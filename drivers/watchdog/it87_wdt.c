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
 *	IT8716, IT8718, IT8726 and IT8712 (J,K version).
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

#include <asm/system.h>

#define WATCHDOG_VERSION	"1.12"
#define WATCHDOG_NAME		"IT87 WDT"
#define PFX			WATCHDOG_NAME ": "
#define DRIVER_VERSION		WATCHDOG_NAME " driver, v" WATCHDOG_VERSION "\n"
#define WD_MAGIC		'V'

/* Defaults for Module Parameter */
#define DEFAULT_NOGAMEPORT	0
#define DEFAULT_EXCLUSIVE	1
#define DEFAULT_TIMEOUT 	60
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
#define CHIPREV 	0x22
#define ACTREG		0x30
#define BASEREG 	0x60

/* Chip Id numbers */
#define NO_DEV_ID	0xffff
#define IT8705_ID	0x8705
#define IT8712_ID	0x8712
#define IT8716_ID	0x8716
#define IT8718_ID	0x8718
#define IT8726_ID	0x8726	/* the data sheet suggest wrongly 0x8716 */

/* GPIO Configuration Registers LDN=0x07 */
#define WDTCTRL 	0x71
#define WDTCFG		0x72
#define WDTVALLSB	0x73
#define WDTVALMSB	0x74

/* GPIO Bits WDTCTRL */
#define WDT_CIRINT	0x80
#define WDT_MOUSEINT	0x40
#define WDT_KYBINT	0x20
#define WDT_GAMEPORT	0x10 /* not it8718 */
#define WDT_FORCE	0x02
#define WDT_ZERO	0x01

/* GPIO Bits WDTCFG */
#define WDT_TOV1	0x80
#define WDT_KRST	0x40
#define WDT_TOVE	0x20
#define WDT_PWROK	0x10
#define WDT_INT_MASK	0x0f

/* CIR Configuration Register LDN=0x0a */
#define CIR_ILS 	0x70

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

static	unsigned int base, gpact, ciract;
static	unsigned long wdt_status;
static	DEFINE_SPINLOCK(spinlock);

static	int nogameport = DEFAULT_NOGAMEPORT;
static	int exclusive  = DEFAULT_EXCLUSIVE;
static	int timeout    = DEFAULT_TIMEOUT;
static	int testmode   = DEFAULT_TESTMODE;
static	int nowayout   = DEFAULT_NOWAYOUT;

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
module_param(nowayout, int, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started, default="
		__MODULE_STRING(WATCHDOG_NOWAYOUT));

/* Superio Chip */

static inline void superio_enter(void)
{
	outb(0x87, REG);
	outb(0x01, REG);
	outb(0x55, REG);
	outb(0x55, REG);
}

static inline void superio_exit(void)
{
	outb(0x02, REG);
	outb(0x02, VAL);
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

static void wdt_start(void)
{
	unsigned long flags;

	spin_lock_irqsave(&spinlock, flags);
	superio_enter();

	superio_select(GPIO);
	if (test_bit(WDTS_USE_GP, &wdt_status))
		superio_outb(WDT_GAMEPORT, WDTCTRL);
	else
		superio_outb(WDT_CIRINT, WDTCTRL);
	if (!testmode)
		superio_outb(WDT_TOV1 | WDT_KRST | WDT_PWROK, WDTCFG);
	else
		superio_outb(WDT_TOV1, WDTCFG);
	superio_outb(timeout>>8, WDTVALMSB);
	superio_outb(timeout, WDTVALLSB);

	superio_exit();
	spin_unlock_irqrestore(&spinlock, flags);
}

static void wdt_stop(void)
{
	unsigned long flags;

	spin_lock_irqsave(&spinlock, flags);
	superio_enter();

	superio_select(GPIO);
	superio_outb(0x00, WDTCTRL);
	superio_outb(WDT_TOV1, WDTCFG);
	superio_outb(0x00, WDTVALMSB);
	superio_outb(0x00, WDTVALLSB);

	superio_exit();
	spin_unlock_irqrestore(&spinlock, flags);
}

/**
 *	wdt_set_timeout - set a new timeout value with watchdog ioctl
 *	@t: timeout value in seconds
 *
 *	The hardware device has a 16 bit watchdog timer, thus the
 *	timeout time ranges between 1 and 65535 seconds.
 *
 *	Used within WDIOC_SETTIMEOUT watchdog device ioctl.
 */

static int wdt_set_timeout(int t)
{
	unsigned long flags;

	if (t < 1 || t > 65535)
		return -EINVAL;

	timeout = t;

	spin_lock_irqsave(&spinlock, flags);
	if (test_bit(WDTS_TIMER_RUN, &wdt_status)) {
		superio_enter();

		superio_select(GPIO);
		superio_outb(t>>8, WDTVALMSB);
		superio_outb(t, WDTVALLSB);

		superio_exit();
	}
	spin_unlock_irqrestore(&spinlock, flags);
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
	unsigned long flags;

	*status = 0;
	if (testmode) {
		spin_lock_irqsave(&spinlock, flags);
		superio_enter();
		superio_select(GPIO);
		if (superio_inb(WDTCTRL) & WDT_ZERO) {
			superio_outb(0x00, WDTCTRL);
			clear_bit(WDTS_TIMER_RUN, &wdt_status);
			*status |= WDIOF_CARDRESET;
		}

		superio_exit();
		spin_unlock_irqrestore(&spinlock, flags);
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
		if (nowayout && !test_and_set_bit(WDTS_LOCKED, &wdt_status))
			__module_get(THIS_MODULE);
		wdt_start();
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
			wdt_stop();
			clear_bit(WDTS_TIMER_RUN, &wdt_status);
		} else {
			wdt_keepalive();
			printk(KERN_CRIT PFX
			       "unexpected close, not stopping watchdog!\n");
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

static struct watchdog_info ident = {
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
		wdt_get_status(&status);
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
			if (test_bit(WDTS_TIMER_RUN, &wdt_status))
				wdt_stop();
			clear_bit(WDTS_TIMER_RUN, &wdt_status);
			return 0;

		case WDIOS_ENABLECARD:
			if (!test_and_set_bit(WDTS_TIMER_RUN, &wdt_status))
				wdt_start();
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
	u16 chip_type;
	u8  chip_rev;
	unsigned long flags;

	spin_lock_irqsave(&spinlock, flags);
	superio_enter();
	chip_type = superio_inw(CHIPID);
	chip_rev  = superio_inb(CHIPREV) & 0x0f;
	superio_exit();
	spin_unlock_irqrestore(&spinlock, flags);

	switch (chip_type) {
	case IT8716_ID:
	case IT8718_ID:
	case IT8726_ID:
		break;
	case IT8712_ID:
		if (chip_rev > 7)
			break;
	case IT8705_ID:
		printk(KERN_ERR PFX
		       "Unsupported Chip found, Chip %04x Revision %02x\n",
		       chip_type, chip_rev);
		return -ENODEV;
	case NO_DEV_ID:
		printk(KERN_ERR PFX "no device\n");
		return -ENODEV;
	default:
		printk(KERN_ERR PFX
		       "Unknown Chip found, Chip %04x Revision %04x\n",
		       chip_type, chip_rev);
		return -ENODEV;
	}

	spin_lock_irqsave(&spinlock, flags);
	superio_enter();

	superio_select(GPIO);
	superio_outb(WDT_TOV1, WDTCFG);
	superio_outb(0x00, WDTCTRL);

	/* First try to get Gameport support */
	if (chip_type != IT8718_ID && !nogameport) {
		superio_select(GAMEPORT);
		base = superio_inw(BASEREG);
		if (!base) {
			base = GP_BASE_DEFAULT;
			superio_outw(base, BASEREG);
		}
		gpact = superio_inb(ACTREG);
		superio_outb(0x01, ACTREG);
		superio_exit();
		spin_unlock_irqrestore(&spinlock, flags);
		if (request_region(base, 1, WATCHDOG_NAME))
			set_bit(WDTS_USE_GP, &wdt_status);
		else
			rc = -EIO;
	} else {
		superio_exit();
		spin_unlock_irqrestore(&spinlock, flags);
	}

	/* If we haven't Gameport support, try to get CIR support */
	if (!test_bit(WDTS_USE_GP, &wdt_status)) {
		if (!request_region(CIR_BASE, 8, WATCHDOG_NAME)) {
			if (rc == -EIO)
				printk(KERN_ERR PFX
					"I/O Address 0x%04x and 0x%04x"
					" already in use\n", base, CIR_BASE);
			else
				printk(KERN_ERR PFX
					"I/O Address 0x%04x already in use\n",
					CIR_BASE);
			rc = -EIO;
			goto err_out;
		}
		base = CIR_BASE;
		spin_lock_irqsave(&spinlock, flags);
		superio_enter();

		superio_select(CIR);
		superio_outw(base, BASEREG);
		superio_outb(0x00, CIR_ILS);
		ciract = superio_inb(ACTREG);
		superio_outb(0x01, ACTREG);
		if (rc == -EIO) {
			superio_select(GAMEPORT);
			superio_outb(gpact, ACTREG);
		}

		superio_exit();
		spin_unlock_irqrestore(&spinlock, flags);
	}

	if (timeout < 1 || timeout > 65535) {
		timeout = DEFAULT_TIMEOUT;
		printk(KERN_WARNING PFX
		       "Timeout value out of range, use default %d sec\n",
		       DEFAULT_TIMEOUT);
	}

	rc = register_reboot_notifier(&wdt_notifier);
	if (rc) {
		printk(KERN_ERR PFX
		       "Cannot register reboot notifier (err=%d)\n", rc);
		goto err_out_region;
	}

	rc = misc_register(&wdt_miscdev);
	if (rc) {
		printk(KERN_ERR PFX
		       "Cannot register miscdev on minor=%d (err=%d)\n",
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

	printk(KERN_INFO PFX "Chip it%04x revision %d initialized. "
		"timeout=%d sec (nowayout=%d testmode=%d exclusive=%d "
		"nogameport=%d)\n", chip_type, chip_rev, timeout,
		nowayout, testmode, exclusive, nogameport);

	return 0;

err_out_reboot:
	unregister_reboot_notifier(&wdt_notifier);
err_out_region:
	release_region(base, test_bit(WDTS_USE_GP, &wdt_status) ? 1 : 8);
	if (!test_bit(WDTS_USE_GP, &wdt_status)) {
		spin_lock_irqsave(&spinlock, flags);
		superio_enter();
		superio_select(CIR);
		superio_outb(ciract, ACTREG);
		superio_exit();
		spin_unlock_irqrestore(&spinlock, flags);
	}
err_out:
	if (chip_type != IT8718_ID && !nogameport) {
		spin_lock_irqsave(&spinlock, flags);
		superio_enter();
		superio_select(GAMEPORT);
		superio_outb(gpact, ACTREG);
		superio_exit();
		spin_unlock_irqrestore(&spinlock, flags);
	}

	return rc;
}

static void __exit it87_wdt_exit(void)
{
	unsigned long flags;
	int nolock;

	nolock = !spin_trylock_irqsave(&spinlock, flags);
	superio_enter();
	superio_select(GPIO);
	superio_outb(0x00, WDTCTRL);
	superio_outb(0x00, WDTCFG);
	superio_outb(0x00, WDTVALMSB);
	superio_outb(0x00, WDTVALLSB);
	if (test_bit(WDTS_USE_GP, &wdt_status)) {
		superio_select(GAMEPORT);
		superio_outb(gpact, ACTREG);
	} else {
		superio_select(CIR);
		superio_outb(ciract, ACTREG);
	}
	superio_exit();
	if (!nolock)
		spin_unlock_irqrestore(&spinlock, flags);

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
