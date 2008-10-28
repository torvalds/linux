/*
 *      NS pc87413-wdt Watchdog Timer driver for Linux 2.6.x.x
 *
 *      This code is based on wdt.c with original copyright.
 *
 *      (C) Copyright 2006 Sven Anders, <anders@anduras.de>
 *                     and Marcus Junker, <junker@anduras.de>
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 *
 *      Neither Sven Anders, Marcus Junker nor ANDURAS AG
 *      admit liability nor provide warranty for any of this software.
 *      This material is provided "AS-IS" and at no charge.
 *
 *      Release 1.1
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/notifier.h>
#include <linux/fs.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/moduleparam.h>
#include <linux/io.h>
#include <linux/uaccess.h>

#include <asm/system.h>

/* #define DEBUG 1 */

#define DEFAULT_TIMEOUT     1		/* 1 minute */
#define MAX_TIMEOUT         255

#define VERSION             "1.1"
#define MODNAME             "pc87413 WDT"
#define PFX                 MODNAME ": "
#define DPFX                MODNAME " - DEBUG: "

#define WDT_INDEX_IO_PORT   (io+0)	/* I/O port base (index register) */
#define WDT_DATA_IO_PORT    (WDT_INDEX_IO_PORT+1)
#define SWC_LDN             0x04
#define SIOCFG2             0x22	/* Serial IO register */
#define WDCTL               0x10	/* Watchdog-Timer-Controll-Register */
#define WDTO                0x11	/* Watchdog timeout register */
#define WDCFG               0x12	/* Watchdog config register */

static int io = 0x2E;			/* Address used on Portwell Boards */

static int timeout = DEFAULT_TIMEOUT;	/* timeout value */
static unsigned long timer_enabled;	/* is the timer enabled? */

static char expect_close;		/* is the close expected? */

static DEFINE_SPINLOCK(io_lock);	/* to guard us from io races */

static int nowayout = WATCHDOG_NOWAYOUT;

/* -- Low level function ----------------------------------------*/

/* Select pins for Watchdog output */

static inline void pc87413_select_wdt_out(void)
{
	unsigned int cr_data = 0;

	/* Step 1: Select multiple pin,pin55,as WDT output */

	outb_p(SIOCFG2, WDT_INDEX_IO_PORT);

	cr_data = inb(WDT_DATA_IO_PORT);

	cr_data |= 0x80; /* Set Bit7 to 1*/
	outb_p(SIOCFG2, WDT_INDEX_IO_PORT);

	outb_p(cr_data, WDT_DATA_IO_PORT);

#ifdef DEBUG
	printk(KERN_INFO DPFX
		"Select multiple pin,pin55,as WDT output: Bit7 to 1: %d\n",
								cr_data);
#endif
}

/* Enable SWC functions */

static inline void pc87413_enable_swc(void)
{
	unsigned int cr_data = 0;

	/* Step 2: Enable SWC functions */

	outb_p(0x07, WDT_INDEX_IO_PORT);	/* Point SWC_LDN (LDN=4) */
	outb_p(SWC_LDN, WDT_DATA_IO_PORT);

	outb_p(0x30, WDT_INDEX_IO_PORT);	/* Read Index 0x30 First */
	cr_data = inb(WDT_DATA_IO_PORT);
	cr_data |= 0x01;			/* Set Bit0 to 1 */
	outb_p(0x30, WDT_INDEX_IO_PORT);
	outb_p(cr_data, WDT_DATA_IO_PORT);	/* Index0x30_bit0P1 */

#ifdef DEBUG
	printk(KERN_INFO DPFX "pc87413 - Enable SWC functions\n");
#endif
}

/* Read SWC I/O base address */

static inline unsigned int pc87413_get_swc_base(void)
{
	unsigned int  swc_base_addr = 0;
	unsigned char addr_l, addr_h = 0;

	/* Step 3: Read SWC I/O Base Address */

	outb_p(0x60, WDT_INDEX_IO_PORT);	/* Read Index 0x60 */
	addr_h = inb(WDT_DATA_IO_PORT);

	outb_p(0x61, WDT_INDEX_IO_PORT);	/* Read Index 0x61 */

	addr_l = inb(WDT_DATA_IO_PORT);

	swc_base_addr = (addr_h << 8) + addr_l;
#ifdef DEBUG
	printk(KERN_INFO DPFX
		"Read SWC I/O Base Address: low %d, high %d, res %d\n",
						addr_l, addr_h, swc_base_addr);
#endif
	return swc_base_addr;
}

/* Select Bank 3 of SWC */

static inline void pc87413_swc_bank3(unsigned int swc_base_addr)
{
	/* Step 4: Select Bank3 of SWC */
	outb_p(inb(swc_base_addr + 0x0f) | 0x03, swc_base_addr + 0x0f);
#ifdef DEBUG
	printk(KERN_INFO DPFX "Select Bank3 of SWC\n");
#endif
}

/* Set watchdog timeout to x minutes */

static inline void pc87413_programm_wdto(unsigned int swc_base_addr,
					 char pc87413_time)
{
	/* Step 5: Programm WDTO, Twd. */
	outb_p(pc87413_time, swc_base_addr + WDTO);
#ifdef DEBUG
	printk(KERN_INFO DPFX "Set WDTO to %d minutes\n", pc87413_time);
#endif
}

/* Enable WDEN */

static inline void pc87413_enable_wden(unsigned int swc_base_addr)
{
	/* Step 6: Enable WDEN */
	outb_p(inb(swc_base_addr + WDCTL) | 0x01, swc_base_addr + WDCTL);
#ifdef DEBUG
	printk(KERN_INFO DPFX "Enable WDEN\n");
#endif
}

/* Enable SW_WD_TREN */
static inline void pc87413_enable_sw_wd_tren(unsigned int swc_base_addr)
{
	/* Enable SW_WD_TREN */
	outb_p(inb(swc_base_addr + WDCFG) | 0x80, swc_base_addr + WDCFG);
#ifdef DEBUG
	printk(KERN_INFO DPFX "Enable SW_WD_TREN\n");
#endif
}

/* Disable SW_WD_TREN */

static inline void pc87413_disable_sw_wd_tren(unsigned int swc_base_addr)
{
	/* Disable SW_WD_TREN */
	outb_p(inb(swc_base_addr + WDCFG) & 0x7f, swc_base_addr + WDCFG);
#ifdef DEBUG
	printk(KERN_INFO DPFX "pc87413 - Disable SW_WD_TREN\n");
#endif
}

/* Enable SW_WD_TRG */

static inline void pc87413_enable_sw_wd_trg(unsigned int swc_base_addr)
{
	/* Enable SW_WD_TRG */
	outb_p(inb(swc_base_addr + WDCTL) | 0x80, swc_base_addr + WDCTL);
#ifdef DEBUG
	printk(KERN_INFO DPFX "pc87413 - Enable SW_WD_TRG\n");
#endif
}

/* Disable SW_WD_TRG */

static inline void pc87413_disable_sw_wd_trg(unsigned int swc_base_addr)
{
	/* Disable SW_WD_TRG */
	outb_p(inb(swc_base_addr + WDCTL) & 0x7f, swc_base_addr + WDCTL);
#ifdef DEBUG
	printk(KERN_INFO DPFX "Disable SW_WD_TRG\n");
#endif
}

/* -- Higher level functions ------------------------------------*/

/* Enable the watchdog */

static void pc87413_enable(void)
{
	unsigned int swc_base_addr;

	spin_lock(&io_lock);

	pc87413_select_wdt_out();
	pc87413_enable_swc();
	swc_base_addr = pc87413_get_swc_base();
	pc87413_swc_bank3(swc_base_addr);
	pc87413_programm_wdto(swc_base_addr, timeout);
	pc87413_enable_wden(swc_base_addr);
	pc87413_enable_sw_wd_tren(swc_base_addr);
	pc87413_enable_sw_wd_trg(swc_base_addr);

	spin_unlock(&io_lock);
}

/* Disable the watchdog */

static void pc87413_disable(void)
{
	unsigned int swc_base_addr;

	spin_lock(&io_lock);

	pc87413_select_wdt_out();
	pc87413_enable_swc();
	swc_base_addr = pc87413_get_swc_base();
	pc87413_swc_bank3(swc_base_addr);
	pc87413_disable_sw_wd_tren(swc_base_addr);
	pc87413_disable_sw_wd_trg(swc_base_addr);
	pc87413_programm_wdto(swc_base_addr, 0);

	spin_unlock(&io_lock);
}

/* Refresh the watchdog */

static void pc87413_refresh(void)
{
	unsigned int swc_base_addr;

	spin_lock(&io_lock);

	pc87413_select_wdt_out();
	pc87413_enable_swc();
	swc_base_addr = pc87413_get_swc_base();
	pc87413_swc_bank3(swc_base_addr);
	pc87413_disable_sw_wd_tren(swc_base_addr);
	pc87413_disable_sw_wd_trg(swc_base_addr);
	pc87413_programm_wdto(swc_base_addr, timeout);
	pc87413_enable_wden(swc_base_addr);
	pc87413_enable_sw_wd_tren(swc_base_addr);
	pc87413_enable_sw_wd_trg(swc_base_addr);

	spin_unlock(&io_lock);
}

/* -- File operations -------------------------------------------*/

/**
 *	pc87413_open:
 *	@inode: inode of device
 *	@file: file handle to device
 *
 */

static int pc87413_open(struct inode *inode, struct file *file)
{
	/* /dev/watchdog can only be opened once */

	if (test_and_set_bit(0, &timer_enabled))
		return -EBUSY;

	if (nowayout)
		__module_get(THIS_MODULE);

	/* Reload and activate timer */
	pc87413_refresh();

	printk(KERN_INFO MODNAME
		"Watchdog enabled. Timeout set to %d minute(s).\n", timeout);

	return nonseekable_open(inode, file);
}

/**
 *	pc87413_release:
 *	@inode: inode to board
 *	@file: file handle to board
 *
 *	The watchdog has a configurable API. There is a religious dispute
 *	between people who want their watchdog to be able to shut down and
 *	those who want to be sure if the watchdog manager dies the machine
 *	reboots. In the former case we disable the counters, in the latter
 *	case you have to open it again very soon.
 */

static int pc87413_release(struct inode *inode, struct file *file)
{
	/* Shut off the timer. */

	if (expect_close == 42) {
		pc87413_disable();
		printk(KERN_INFO MODNAME
				"Watchdog disabled, sleeping again...\n");
	} else {
		printk(KERN_CRIT MODNAME
				"Unexpected close, not stopping watchdog!\n");
		pc87413_refresh();
	}
	clear_bit(0, &timer_enabled);
	expect_close = 0;
	return 0;
}

/**
 *	pc87413_status:
 *
 *      return, if the watchdog is enabled (timeout is set...)
 */


static int pc87413_status(void)
{
	  return 0; /* currently not supported */
}

/**
 *	pc87413_write:
 *	@file: file handle to the watchdog
 *	@data: data buffer to write
 *	@len: length in bytes
 *	@ppos: pointer to the position to write. No seeks allowed
 *
 *	A write to a watchdog device is defined as a keepalive signal. Any
 *	write of data will do, as we we don't define content meaning.
 */

static ssize_t pc87413_write(struct file *file, const char __user *data,
			     size_t len, loff_t *ppos)
{
	/* See if we got the magic character 'V' and reload the timer */
	if (len) {
		if (!nowayout) {
			size_t i;

			/* reset expect flag */
			expect_close = 0;

			/* scan to see whether or not we got the
			   magic character */
			for (i = 0; i != len; i++) {
				char c;
				if (get_user(c, data + i))
					return -EFAULT;
				if (c == 'V')
					expect_close = 42;
			}
		}

		/* someone wrote to us, we should reload the timer */
		pc87413_refresh();
	}
	return len;
}

/**
 *	pc87413_ioctl:
 *	@file: file handle to the device
 *	@cmd: watchdog command
 *	@arg: argument pointer
 *
 *	The watchdog API defines a common set of functions for all watchdogs
 *	according to their available features. We only actually usefully support
 *	querying capabilities and current status.
 */

static long pc87413_ioctl(struct file *file, unsigned int cmd,
						unsigned long arg)
{
	int new_timeout;

	union {
		struct watchdog_info __user *ident;
		int __user *i;
	} uarg;

	static struct watchdog_info ident = {
		.options          = WDIOF_KEEPALIVEPING |
				    WDIOF_SETTIMEOUT |
				    WDIOF_MAGICCLOSE,
		.firmware_version = 1,
		.identity         = "PC87413(HF/F) watchdog",
	};

	uarg.i = (int __user *)arg;

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		return copy_to_user(uarg.ident, &ident,
					sizeof(ident)) ? -EFAULT : 0;
	case WDIOC_GETSTATUS:
		return put_user(pc87413_status(), uarg.i);
	case WDIOC_GETBOOTSTATUS:
		return put_user(0, uarg.i);
	case WDIOC_SETOPTIONS:
	{
		int options, retval = -EINVAL;
		if (get_user(options, uarg.i))
			return -EFAULT;
		if (options & WDIOS_DISABLECARD) {
			pc87413_disable();
			retval = 0;
		}
		if (options & WDIOS_ENABLECARD) {
			pc87413_enable();
			retval = 0;
		}
		return retval;
	}
	case WDIOC_KEEPALIVE:
		pc87413_refresh();
#ifdef DEBUG
		printk(KERN_INFO DPFX "keepalive\n");
#endif
		return 0;
	case WDIOC_SETTIMEOUT:
		if (get_user(new_timeout, uarg.i))
			return -EFAULT;
		/* the API states this is given in secs */
		new_timeout /= 60;
		if (new_timeout < 0 || new_timeout > MAX_TIMEOUT)
			return -EINVAL;
		timeout = new_timeout;
		pc87413_refresh();
		/* fall through and return the new timeout... */
	case WDIOC_GETTIMEOUT:
		new_timeout = timeout * 60;
		return put_user(new_timeout, uarg.i);
	default:
		return -ENOTTY;
	}
}

/* -- Notifier funtions -----------------------------------------*/

/**
 *	notify_sys:
 *	@this: our notifier block
 *	@code: the event being reported
 *	@unused: unused
 *
 *	Our notifier is called on system shutdowns. We want to turn the card
 *	off at reboot otherwise the machine will reboot again during memory
 *	test or worse yet during the following fsck. This would suck, in fact
 *	trust me - if it happens it does suck.
 */

static int pc87413_notify_sys(struct notifier_block *this,
			      unsigned long code,
			      void *unused)
{
	if (code == SYS_DOWN || code == SYS_HALT)
		/* Turn the card off */
		pc87413_disable();
	return NOTIFY_DONE;
}

/* -- Module's structures ---------------------------------------*/

static const struct file_operations pc87413_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.write		= pc87413_write,
	.unlocked_ioctl	= pc87413_ioctl,
	.open		= pc87413_open,
	.release	= pc87413_release,
};

static struct notifier_block pc87413_notifier = {
	.notifier_call  = pc87413_notify_sys,
};

static struct miscdevice pc87413_miscdev = {
	.minor          = WATCHDOG_MINOR,
	.name           = "watchdog",
	.fops           = &pc87413_fops,
};

/* -- Module init functions -------------------------------------*/

/**
 * 	pc87413_init: module's "constructor"
 *
 *	Set up the WDT watchdog board. All we have to do is grab the
 *	resources we require and bitch if anyone beat us to them.
 *	The open() function will actually kick the board off.
 */

static int __init pc87413_init(void)
{
	int ret;

	printk(KERN_INFO PFX "Version " VERSION " at io 0x%X\n",
							WDT_INDEX_IO_PORT);

	/* request_region(io, 2, "pc87413"); */

	ret = register_reboot_notifier(&pc87413_notifier);
	if (ret != 0) {
		printk(KERN_ERR PFX
			"cannot register reboot notifier (err=%d)\n", ret);
	}

	ret = misc_register(&pc87413_miscdev);
	if (ret != 0) {
		printk(KERN_ERR PFX "cannot register miscdev on minor=%d (err=%d)\n",
			WATCHDOG_MINOR, ret);
		unregister_reboot_notifier(&pc87413_notifier);
		return ret;
	}
	printk(KERN_INFO PFX "initialized. timeout=%d min \n", timeout);
	pc87413_enable();
	return 0;
}

/**
 *	pc87413_exit: module's "destructor"
 *
 *	Unload the watchdog. You cannot do this with any file handles open.
 *	If your watchdog is set to continue ticking on close and you unload
 *	it, well it keeps ticking. We won't get the interrupt but the board
 *	will not touch PC memory so all is fine. You just have to load a new
 *	module in 60 seconds or reboot.
 */

static void __exit pc87413_exit(void)
{
	/* Stop the timer before we leave */
	if (!nowayout) {
		pc87413_disable();
		printk(KERN_INFO MODNAME "Watchdog disabled.\n");
	}

	misc_deregister(&pc87413_miscdev);
	unregister_reboot_notifier(&pc87413_notifier);
	/* release_region(io, 2); */

	printk(KERN_INFO MODNAME " watchdog component driver removed.\n");
}

module_init(pc87413_init);
module_exit(pc87413_exit);

MODULE_AUTHOR("Sven Anders <anders@anduras.de>, Marcus Junker <junker@anduras.de>,");
MODULE_DESCRIPTION("PC87413 WDT driver");
MODULE_LICENSE("GPL");

MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);

module_param(io, int, 0);
MODULE_PARM_DESC(io, MODNAME " I/O port (default: " __MODULE_STRING(io) ").");

module_param(timeout, int, 0);
MODULE_PARM_DESC(timeout,
		"Watchdog timeout in minutes (default="
				__MODULE_STRING(timeout) ").");

module_param(nowayout, int, 0);
MODULE_PARM_DESC(nowayout,
		"Watchdog cannot be stopped once started (default="
				__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

