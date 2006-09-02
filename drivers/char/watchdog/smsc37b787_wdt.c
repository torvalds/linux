/*
 *	SMsC 37B787 Watchdog Timer driver for Linux 2.6.x.x
 *
 *      Based on acquirewdt.c by Alan Cox <alan@redhat.com>
 *       and some other existing drivers
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *	The authors do NOT admit liability nor provide warranty for
 *	any of this software. This material is provided "AS-IS" in
 *      the hope that it may be useful for others.
 *
 *	(C) Copyright 2003-2006  Sven Anders <anders@anduras.de>
 *
 *  History:
 *	2003 - Created version 1.0 for Linux 2.4.x.
 *	2006 - Ported to Linux 2.6, added nowayout and MAGICCLOSE
 *             features. Released version 1.1
 *
 *  Theory of operation:
 *
 *      A Watchdog Timer (WDT) is a hardware circuit that can
 *      reset the computer system in case of a software fault.
 *      You probably knew that already.
 *
 *      Usually a userspace daemon will notify the kernel WDT driver
 *      via the /dev/watchdog special device file that userspace is
 *      still alive, at regular intervals.  When such a notification
 *      occurs, the driver will usually tell the hardware watchdog
 *      that everything is in order, and that the watchdog should wait
 *      for yet another little while to reset the system.
 *      If userspace fails (RAM error, kernel bug, whatever), the
 *      notifications cease to occur, and the hardware watchdog will
 *      reset the system (causing a reboot) after the timeout occurs.
 *
 * Create device with:
 *  mknod /dev/watchdog c 10 130
 *
 * For an example userspace keep-alive daemon, see:
 *   Documentation/watchdog/watchdog.txt
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/ioport.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <linux/spinlock.h>

#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/system.h>

/* enable support for minutes as units? */
/* (does not always work correctly, so disabled by default!) */
#define SMSC_SUPPORT_MINUTES
#undef SMSC_SUPPORT_MINUTES

#define MAX_TIMEOUT     255

#define UNIT_SECOND     0
#define UNIT_MINUTE     1

#define MODNAME		"smsc37b787_wdt: "
#define VERSION         "1.1"

#define IOPORT          0x3F0
#define IOPORT_SIZE     2
#define IODEV_NO        8

static int unit = UNIT_SECOND;  /* timer's unit */
static int timeout = 60;        /* timeout value: default is 60 "units" */
static unsigned long timer_enabled = 0;   /* is the timer enabled? */

static char expect_close;       /* is the close expected? */

static spinlock_t io_lock;	/* to guard the watchdog from io races */

static int nowayout = WATCHDOG_NOWAYOUT;

/* -- Low level function ----------------------------------------*/

/* unlock the IO chip */

static inline void open_io_config(void)
{
        outb(0x55, IOPORT);
	mdelay(1);
        outb(0x55, IOPORT);
}

/* lock the IO chip */
static inline void close_io_config(void)
{
        outb(0xAA, IOPORT);
}

/* select the IO device */
static inline void select_io_device(unsigned char devno)
{
        outb(0x07, IOPORT);
        outb(devno, IOPORT+1);
}

/* write to the control register */
static inline void write_io_cr(unsigned char reg, unsigned char data)
{
        outb(reg, IOPORT);
        outb(data, IOPORT+1);
}

/* read from the control register */
static inline char read_io_cr(unsigned char reg)
{
        outb(reg, IOPORT);
        return inb(IOPORT+1);
}

/* -- Medium level functions ------------------------------------*/

static inline void gpio_bit12(unsigned char reg)
{
	// -- General Purpose I/O Bit 1.2 --
	// Bit 0,   In/Out: 0 = Output, 1 = Input
	// Bit 1,   Polarity: 0 = No Invert, 1 = Invert
	// Bit 2,   Group Enable Intr.: 0 = Disable, 1 = Enable
	// Bit 3/4, Function select: 00 = GPI/O, 01 = WDT, 10 = P17,
	//                           11 = Either Edge Triggered Intr. 2
        // Bit 5/6  (Reserved)
	// Bit 7,   Output Type: 0 = Push Pull Bit, 1 = Open Drain
        write_io_cr(0xE2, reg);
}

static inline void gpio_bit13(unsigned char reg)
{
	// -- General Purpose I/O Bit 1.3 --
	// Bit 0,  In/Out: 0 = Output, 1 = Input
	// Bit 1,  Polarity: 0 = No Invert, 1 = Invert
	// Bit 2,  Group Enable Intr.: 0 = Disable, 1 = Enable
	// Bit 3,  Function select: 0 = GPI/O, 1 = LED
        // Bit 4-6 (Reserved)
	// Bit 7,  Output Type: 0 = Push Pull Bit, 1 = Open Drain
        write_io_cr(0xE3, reg);
}

static inline void wdt_timer_units(unsigned char new_units)
{
	// -- Watchdog timer units --
	// Bit 0-6 (Reserved)
	// Bit 7,  WDT Time-out Value Units Select
	//         (0 = Minutes, 1 = Seconds)
        write_io_cr(0xF1, new_units);
}

static inline void wdt_timeout_value(unsigned char new_timeout)
{
	// -- Watchdog Timer Time-out Value --
	// Bit 0-7 Binary coded units (0=Disabled, 1..255)
        write_io_cr(0xF2, new_timeout);
}

static inline void wdt_timer_conf(unsigned char conf)
{
	// -- Watchdog timer configuration --
	// Bit 0   Joystick enable: 0* = No Reset, 1 = Reset WDT upon Gameport I/O
	// Bit 1   Keyboard enable: 0* = No Reset, 1 = Reset WDT upon KBD Intr.
	// Bit 2   Mouse enable: 0* = No Reset, 1 = Reset WDT upon Mouse Intr.
        // Bit 3   Reset the timer
        //         (Wrong in SMsC documentation? Given as: PowerLED Timout Enabled)
	// Bit 4-7 WDT Interrupt Mapping: (0000* = Disabled,
	//            0001=IRQ1, 0010=(Invalid), 0011=IRQ3 to 1111=IRQ15)
        write_io_cr(0xF3, conf);
}

static inline void wdt_timer_ctrl(unsigned char reg)
{
	// -- Watchdog timer control --
	// Bit 0   Status Bit: 0 = Timer counting, 1 = Timeout occured
	// Bit 1   Power LED Toggle: 0 = Disable Toggle, 1 = Toggle at 1 Hz
	// Bit 2   Force Timeout: 1 = Forces WD timeout event (self-cleaning)
	// Bit 3   P20 Force Timeout enabled:
	//          0 = P20 activity does not generate the WD timeout event
	//          1 = P20 Allows rising edge of P20, from the keyboard
	//              controller, to force the WD timeout event.
	// Bit 4   (Reserved)
	// -- Soft power management --
	// Bit 5   Stop Counter: 1 = Stop software power down counter
	//            set via register 0xB8, (self-cleaning)
	//            (Upon read: 0 = Counter running, 1 = Counter stopped)
	// Bit 6   Restart Counter: 1 = Restart software power down counter
	//            set via register 0xB8, (self-cleaning)
	// Bit 7   SPOFF: 1 = Force software power down (self-cleaning)

        write_io_cr(0xF4, reg);
}

/* -- Higher level functions ------------------------------------*/

/* initialize watchdog */

static void wb_smsc_wdt_initialize(void)
{
        unsigned char old;

	spin_lock(&io_lock);
        open_io_config();
        select_io_device(IODEV_NO);

	// enable the watchdog
	gpio_bit13(0x08);  // Select pin 80 = LED not GPIO
	gpio_bit12(0x0A);  // Set pin 79 = WDT not GPIO/Output/Polarity=Invert

	// disable the timeout
        wdt_timeout_value(0);

	// reset control register
        wdt_timer_ctrl(0x00);

	// reset configuration register
	wdt_timer_conf(0x00);

	// read old (timer units) register
        old = read_io_cr(0xF1) & 0x7F;
        if (unit == UNIT_SECOND) old |= 0x80; // set to seconds

	// set the watchdog timer units
        wdt_timer_units(old);

        close_io_config();
	spin_unlock(&io_lock);
}

/* shutdown the watchdog */

static void wb_smsc_wdt_shutdown(void)
{
	spin_lock(&io_lock);
        open_io_config();
        select_io_device(IODEV_NO);

	// disable the watchdog
        gpio_bit13(0x09);
        gpio_bit12(0x09);

	// reset watchdog config register
	wdt_timer_conf(0x00);

	// reset watchdog control register
        wdt_timer_ctrl(0x00);

	// disable timeout
        wdt_timeout_value(0x00);

        close_io_config();
	spin_unlock(&io_lock);
}

/* set timeout => enable watchdog */

static void wb_smsc_wdt_set_timeout(unsigned char new_timeout)
{
	spin_lock(&io_lock);
        open_io_config();
        select_io_device(IODEV_NO);

	// set Power LED to blink, if we enable the timeout
        wdt_timer_ctrl((new_timeout == 0) ? 0x00 : 0x02);

	// set timeout value
        wdt_timeout_value(new_timeout);

        close_io_config();
	spin_unlock(&io_lock);
}

/* get timeout */

static unsigned char wb_smsc_wdt_get_timeout(void)
{
        unsigned char set_timeout;

	spin_lock(&io_lock);
        open_io_config();
        select_io_device(IODEV_NO);
        set_timeout = read_io_cr(0xF2);
        close_io_config();
	spin_unlock(&io_lock);

        return set_timeout;
}

/* disable watchdog */

static void wb_smsc_wdt_disable(void)
{
        // set the timeout to 0 to disable the watchdog
        wb_smsc_wdt_set_timeout(0);
}

/* enable watchdog by setting the current timeout */

static void wb_smsc_wdt_enable(void)
{
        // set the current timeout...
        wb_smsc_wdt_set_timeout(timeout);
}

/* reset the timer */

static void wb_smsc_wdt_reset_timer(void)
{
	spin_lock(&io_lock);
        open_io_config();
        select_io_device(IODEV_NO);

	// reset the timer
	wdt_timeout_value(timeout);
	wdt_timer_conf(0x08);

        close_io_config();
	spin_unlock(&io_lock);
}

/* return, if the watchdog is enabled (timeout is set...) */

static int wb_smsc_wdt_status(void)
{
	return (wb_smsc_wdt_get_timeout() == 0) ? 0 : WDIOF_KEEPALIVEPING;
}


/* -- File operations -------------------------------------------*/

/* open => enable watchdog and set initial timeout */

static int wb_smsc_wdt_open(struct inode *inode, struct file *file)
{
	/* /dev/watchdog can only be opened once */

	if (test_and_set_bit(0, &timer_enabled))
		return -EBUSY;

	if (nowayout)
		__module_get(THIS_MODULE);

	/* Reload and activate timer */
	wb_smsc_wdt_enable();

	printk(KERN_INFO MODNAME "Watchdog enabled. Timeout set to %d %s.\n", timeout, (unit == UNIT_SECOND) ? "second(s)" : "minute(s)");

	return nonseekable_open(inode, file);
}

/* close => shut off the timer */

static int wb_smsc_wdt_release(struct inode *inode, struct file *file)
{
	/* Shut off the timer. */

	if (expect_close == 42) {
	        wb_smsc_wdt_disable();
		printk(KERN_INFO MODNAME "Watchdog disabled, sleeping again...\n");
	} else {
		printk(KERN_CRIT MODNAME "Unexpected close, not stopping watchdog!\n");
		wb_smsc_wdt_reset_timer();
	}

	clear_bit(0, &timer_enabled);
	expect_close = 0;
	return 0;
}

/* write => update the timer to keep the machine alive */

static ssize_t wb_smsc_wdt_write(struct file *file, const char __user *data,
				 size_t len, loff_t *ppos)
{
	/* See if we got the magic character 'V' and reload the timer */
	if (len) {
		if (!nowayout) {
			size_t i;

			/* reset expect flag */
			expect_close = 0;

			/* scan to see whether or not we got the magic character */
			for (i = 0; i != len; i++) {
				char c;
				if (get_user(c, data+i))
					return -EFAULT;
				if (c == 'V')
					expect_close = 42;
			}
		}

		/* someone wrote to us, we should reload the timer */
		wb_smsc_wdt_reset_timer();
	}
	return len;
}

/* ioctl => control interface */

static int wb_smsc_wdt_ioctl(struct inode *inode, struct file *file,
			     unsigned int cmd, unsigned long arg)
{
	int new_timeout;

	union {
		struct watchdog_info __user *ident;
		int __user *i;
	} uarg;

	static struct watchdog_info ident = {
		.options = 		WDIOF_KEEPALIVEPING |
		                        WDIOF_SETTIMEOUT |
					WDIOF_MAGICCLOSE,
		.firmware_version =	0,
		.identity = 		"SMsC 37B787 Watchdog"
	};

	uarg.i = (int __user *)arg;

	switch (cmd) {
		default:
			return -ENOTTY;

		case WDIOC_GETSUPPORT:
			return copy_to_user(uarg.ident, &ident,
				sizeof(ident)) ? -EFAULT : 0;

		case WDIOC_GETSTATUS:
			return put_user(wb_smsc_wdt_status(), uarg.i);

		case WDIOC_GETBOOTSTATUS:
			return put_user(0, uarg.i);

		case WDIOC_KEEPALIVE:
			wb_smsc_wdt_reset_timer();
			return 0;

		case WDIOC_SETTIMEOUT:
			if (get_user(new_timeout, uarg.i))
				return -EFAULT;

			// the API states this is given in secs
			if (unit == UNIT_MINUTE)
			  new_timeout /= 60;

			if (new_timeout < 0 || new_timeout > MAX_TIMEOUT)
				return -EINVAL;

			timeout = new_timeout;
			wb_smsc_wdt_set_timeout(timeout);

			// fall through and return the new timeout...

		case WDIOC_GETTIMEOUT:

		        new_timeout = timeout;

			if (unit == UNIT_MINUTE)
			  new_timeout *= 60;

			return put_user(new_timeout, uarg.i);

		case WDIOC_SETOPTIONS:
		{
			int options, retval = -EINVAL;

			if (get_user(options, uarg.i))
				return -EFAULT;

			if (options & WDIOS_DISABLECARD) {
				wb_smsc_wdt_disable();
				retval = 0;
			}

			if (options & WDIOS_ENABLECARD) {
				wb_smsc_wdt_enable();
				retval = 0;
			}

			return retval;
		}
	}
}

/* -- Notifier funtions -----------------------------------------*/

static int wb_smsc_wdt_notify_sys(struct notifier_block *this, unsigned long code, void *unused)
{
	if (code == SYS_DOWN || code == SYS_HALT)
	{
                // set timeout to 0, to avoid possible race-condition
	        timeout = 0;
		wb_smsc_wdt_disable();
	}
	return NOTIFY_DONE;
}

/* -- Module's structures ---------------------------------------*/

static struct file_operations wb_smsc_wdt_fops =
{
	.owner          = THIS_MODULE,
	.llseek		= no_llseek,
	.write		= wb_smsc_wdt_write,
	.ioctl		= wb_smsc_wdt_ioctl,
	.open		= wb_smsc_wdt_open,
	.release	= wb_smsc_wdt_release,
};

static struct notifier_block wb_smsc_wdt_notifier =
{
	.notifier_call  = wb_smsc_wdt_notify_sys,
};

static struct miscdevice wb_smsc_wdt_miscdev =
{
	.minor		= WATCHDOG_MINOR,
	.name		= "watchdog",
	.fops		= &wb_smsc_wdt_fops,
};

/* -- Module init functions -------------------------------------*/

/* module's "constructor" */

static int __init wb_smsc_wdt_init(void)
{
	int ret;

	spin_lock_init(&io_lock);

	printk("SMsC 37B787 watchdog component driver " VERSION " initialising...\n");

	if (!request_region(IOPORT, IOPORT_SIZE, "SMsC 37B787 watchdog")) {
		printk(KERN_ERR MODNAME "Unable to register IO port %#x\n", IOPORT);
		ret = -EBUSY;
		goto out_pnp;
	}

        // set new maximum, if it's too big
        if (timeout > MAX_TIMEOUT)
               timeout = MAX_TIMEOUT;

        // init the watchdog timer
        wb_smsc_wdt_initialize();

	ret = register_reboot_notifier(&wb_smsc_wdt_notifier);
	if (ret) {
		printk(KERN_ERR MODNAME "Unable to register reboot notifier err = %d\n", ret);
		goto out_io;
	}

	ret = misc_register(&wb_smsc_wdt_miscdev);
	if (ret) {
		printk(KERN_ERR MODNAME "Unable to register miscdev on minor %d\n", WATCHDOG_MINOR);
		goto out_rbt;
	}

	// output info
	printk(KERN_INFO MODNAME "Timeout set to %d %s.\n", timeout, (unit == UNIT_SECOND) ? "second(s)" : "minute(s)");
	printk(KERN_INFO MODNAME "Watchdog initialized and sleeping (nowayout=%d)...\n", nowayout);

	// ret = 0

out_clean:
	return ret;

out_rbt:
	unregister_reboot_notifier(&wb_smsc_wdt_notifier);

out_io:
	release_region(IOPORT, IOPORT_SIZE);

out_pnp:
	goto out_clean;
}

/* module's "destructor" */

static void __exit wb_smsc_wdt_exit(void)
{
	/* Stop the timer before we leave */
	if (!nowayout)
	{
		wb_smsc_wdt_shutdown();
		printk(KERN_INFO MODNAME "Watchdog disabled.\n");
	}

	misc_deregister(&wb_smsc_wdt_miscdev);
	unregister_reboot_notifier(&wb_smsc_wdt_notifier);
	release_region(IOPORT, IOPORT_SIZE);

	printk("SMsC 37B787 watchdog component driver removed.\n");
}

module_init(wb_smsc_wdt_init);
module_exit(wb_smsc_wdt_exit);

MODULE_AUTHOR("Sven Anders <anders@anduras.de>");
MODULE_DESCRIPTION("Driver for SMsC 37B787 watchdog component (Version " VERSION ")");
MODULE_LICENSE("GPL");

MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);

#ifdef SMSC_SUPPORT_MINUTES
module_param(unit, int, 0);
MODULE_PARM_DESC(unit, "set unit to use, 0=seconds or 1=minutes, default is 0");
#endif

module_param(timeout, int, 0);
MODULE_PARM_DESC(timeout, "range is 1-255 units, default is 60");

module_param(nowayout, int, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default=CONFIG_WATCHDOG_NOWAYOUT)");
