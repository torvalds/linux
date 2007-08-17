/*
 *	Industrial Computer Source WDT500/501 driver
 *
 *	(c) Copyright 1996-1997 Alan Cox <alan@redhat.com>, All Rights Reserved.
 *				http://www.redhat.com
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *	Neither Alan Cox nor CymruNet Ltd. admit liability nor provide
 *	warranty for any of this software. This material is provided
 *	"AS-IS" and at no charge.
 *
 *	(c) Copyright 1995    Alan Cox <alan@lxorguk.ukuu.org.uk>
 *
 *	Release 0.10.
 *
 *	Fixes
 *		Dave Gregorich	:	Modularisation and minor bugs
 *		Alan Cox	:	Added the watchdog ioctl() stuff
 *		Alan Cox	:	Fixed the reboot problem (as noted by
 *					Matt Crocker).
 *		Alan Cox	:	Added wdt= boot option
 *		Alan Cox	:	Cleaned up copy/user stuff
 *		Tim Hockin	:	Added insmod parameters, comment cleanup
 *					Parameterized timeout
 *		Tigran Aivazian	:	Restructured wdt_init() to handle failures
 *		Joel Becker	:	Added WDIOC_GET/SETTIMEOUT
 *		Matt Domsch	:	Added nowayout module option
 */

#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/fs.h>
#include <linux/ioport.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/init.h>

#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include "wd501p.h"

static unsigned long wdt_is_open;
static char expect_close;

/*
 *	Module parameters
 */

#define WD_TIMO 60			/* Default heartbeat = 60 seconds */

static int heartbeat = WD_TIMO;
static int wd_heartbeat;
module_param(heartbeat, int, 0);
MODULE_PARM_DESC(heartbeat, "Watchdog heartbeat in seconds. (0<heartbeat<65536, default=" __MODULE_STRING(WD_TIMO) ")");

static int nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, int, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default=" __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

/* You must set these - there is no sane way to probe for this board. */
static int io=0x240;
static int irq=11;

module_param(io, int, 0);
MODULE_PARM_DESC(io, "WDT io port (default=0x240)");
module_param(irq, int, 0);
MODULE_PARM_DESC(irq, "WDT irq (default=11)");

#ifdef CONFIG_WDT_501
/* Support for the Fan Tachometer on the WDT501-P */
static int tachometer;

module_param(tachometer, int, 0);
MODULE_PARM_DESC(tachometer, "WDT501-P Fan Tachometer support (0=disable, default=0)");
#endif /* CONFIG_WDT_501 */

/*
 *	Programming support
 */

static void wdt_ctr_mode(int ctr, int mode)
{
	ctr<<=6;
	ctr|=0x30;
	ctr|=(mode<<1);
	outb_p(ctr, WDT_CR);
}

static void wdt_ctr_load(int ctr, int val)
{
	outb_p(val&0xFF, WDT_COUNT0+ctr);
	outb_p(val>>8, WDT_COUNT0+ctr);
}

/**
 *	wdt_start:
 *
 *	Start the watchdog driver.
 */

static int wdt_start(void)
{
	inb_p(WDT_DC);			/* Disable watchdog */
	wdt_ctr_mode(0,3);		/* Program CTR0 for Mode 3: Square Wave Generator */
	wdt_ctr_mode(1,2);		/* Program CTR1 for Mode 2: Rate Generator */
	wdt_ctr_mode(2,0);		/* Program CTR2 for Mode 0: Pulse on Terminal Count */
	wdt_ctr_load(0, 8948);		/* Count at 100Hz */
	wdt_ctr_load(1,wd_heartbeat);	/* Heartbeat */
	wdt_ctr_load(2,65535);		/* Length of reset pulse */
	outb_p(0, WDT_DC);		/* Enable watchdog */
	return 0;
}

/**
 *	wdt_stop:
 *
 *	Stop the watchdog driver.
 */

static int wdt_stop (void)
{
	/* Turn the card off */
	inb_p(WDT_DC);			/* Disable watchdog */
	wdt_ctr_load(2,0);		/* 0 length reset pulses now */
	return 0;
}

/**
 *	wdt_ping:
 *
 *	Reload counter one with the watchdog heartbeat. We don't bother reloading
 *	the cascade counter.
 */

static int wdt_ping(void)
{
	/* Write a watchdog value */
	inb_p(WDT_DC);			/* Disable watchdog */
	wdt_ctr_mode(1,2);		/* Re-Program CTR1 for Mode 2: Rate Generator */
	wdt_ctr_load(1,wd_heartbeat);	/* Heartbeat */
	outb_p(0, WDT_DC);		/* Enable watchdog */
	return 0;
}

/**
 *	wdt_set_heartbeat:
 *	@t:		the new heartbeat value that needs to be set.
 *
 *	Set a new heartbeat value for the watchdog device. If the heartbeat value is
 *	incorrect we keep the old value and return -EINVAL. If successfull we
 *	return 0.
 */
static int wdt_set_heartbeat(int t)
{
	if ((t < 1) || (t > 65535))
		return -EINVAL;

	heartbeat = t;
	wd_heartbeat = t * 100;
	return 0;
}

/**
 *	wdt_get_status:
 *	@status:		the new status.
 *
 *	Extract the status information from a WDT watchdog device. There are
 *	several board variants so we have to know which bits are valid. Some
 *	bits default to one and some to zero in order to be maximally painful.
 *
 *	we then map the bits onto the status ioctl flags.
 */

static int wdt_get_status(int *status)
{
	unsigned char new_status=inb_p(WDT_SR);

	*status=0;
	if (new_status & WDC_SR_ISOI0)
		*status |= WDIOF_EXTERN1;
	if (new_status & WDC_SR_ISII1)
		*status |= WDIOF_EXTERN2;
#ifdef CONFIG_WDT_501
	if (!(new_status & WDC_SR_TGOOD))
		*status |= WDIOF_OVERHEAT;
	if (!(new_status & WDC_SR_PSUOVER))
		*status |= WDIOF_POWEROVER;
	if (!(new_status & WDC_SR_PSUUNDR))
		*status |= WDIOF_POWERUNDER;
	if (tachometer) {
		if (!(new_status & WDC_SR_FANGOOD))
			*status |= WDIOF_FANFAULT;
	}
#endif /* CONFIG_WDT_501 */
	return 0;
}

#ifdef CONFIG_WDT_501
/**
 *	wdt_get_temperature:
 *
 *	Reports the temperature in degrees Fahrenheit. The API is in
 *	farenheit. It was designed by an imperial measurement luddite.
 */

static int wdt_get_temperature(int *temperature)
{
	unsigned short c=inb_p(WDT_RT);

	*temperature = (c * 11 / 15) + 7;
	return 0;
}
#endif /* CONFIG_WDT_501 */

/**
 *	wdt_interrupt:
 *	@irq:		Interrupt number
 *	@dev_id:	Unused as we don't allow multiple devices.
 *
 *	Handle an interrupt from the board. These are raised when the status
 *	map changes in what the board considers an interesting way. That means
 *	a failure condition occurring.
 */

static irqreturn_t wdt_interrupt(int irq, void *dev_id)
{
	/*
	 *	Read the status register see what is up and
	 *	then printk it.
	 */
	unsigned char status=inb_p(WDT_SR);

	printk(KERN_CRIT "WDT status %d\n", status);

#ifdef CONFIG_WDT_501
	if (!(status & WDC_SR_TGOOD))
		printk(KERN_CRIT "Overheat alarm.(%d)\n",inb_p(WDT_RT));
	if (!(status & WDC_SR_PSUOVER))
		printk(KERN_CRIT "PSU over voltage.\n");
	if (!(status & WDC_SR_PSUUNDR))
		printk(KERN_CRIT "PSU under voltage.\n");
	if (tachometer) {
		if (!(status & WDC_SR_FANGOOD))
			printk(KERN_CRIT "Possible fan fault.\n");
	}
#endif /* CONFIG_WDT_501 */
	if (!(status & WDC_SR_WCCR))
#ifdef SOFTWARE_REBOOT
#ifdef ONLY_TESTING
		printk(KERN_CRIT "Would Reboot.\n");
#else
		printk(KERN_CRIT "Initiating system reboot.\n");
		emergency_restart();
#endif
#else
		printk(KERN_CRIT "Reset in 5ms.\n");
#endif
	return IRQ_HANDLED;
}


/**
 *	wdt_write:
 *	@file: file handle to the watchdog
 *	@buf: buffer to write (unused as data does not matter here
 *	@count: count of bytes
 *	@ppos: pointer to the position to write. No seeks allowed
 *
 *	A write to a watchdog device is defined as a keepalive signal. Any
 *	write of data will do, as we we don't define content meaning.
 */

static ssize_t wdt_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	if(count) {
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
		wdt_ping();
	}
	return count;
}

/**
 *	wdt_ioctl:
 *	@inode: inode of the device
 *	@file: file handle to the device
 *	@cmd: watchdog command
 *	@arg: argument pointer
 *
 *	The watchdog API defines a common set of functions for all watchdogs
 *	according to their available features. We only actually usefully support
 *	querying capabilities and current status.
 */

static int wdt_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
	unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	int __user *p = argp;
	int new_heartbeat;
	int status;

	static struct watchdog_info ident = {
		.options =		WDIOF_SETTIMEOUT|
					WDIOF_MAGICCLOSE|
					WDIOF_KEEPALIVEPING,
		.firmware_version =	1,
		.identity =		"WDT500/501",
	};

	/* Add options according to the card we have */
	ident.options |= (WDIOF_EXTERN1|WDIOF_EXTERN2);
#ifdef CONFIG_WDT_501
	ident.options |= (WDIOF_OVERHEAT|WDIOF_POWERUNDER|WDIOF_POWEROVER);
	if (tachometer)
		ident.options |= WDIOF_FANFAULT;
#endif /* CONFIG_WDT_501 */

	switch(cmd)
	{
		default:
			return -ENOTTY;
		case WDIOC_GETSUPPORT:
			return copy_to_user(argp, &ident, sizeof(ident))?-EFAULT:0;

		case WDIOC_GETSTATUS:
			wdt_get_status(&status);
			return put_user(status, p);
		case WDIOC_GETBOOTSTATUS:
			return put_user(0, p);
		case WDIOC_KEEPALIVE:
			wdt_ping();
			return 0;
		case WDIOC_SETTIMEOUT:
			if (get_user(new_heartbeat, p))
				return -EFAULT;

			if (wdt_set_heartbeat(new_heartbeat))
				return -EINVAL;

			wdt_ping();
			/* Fall */
		case WDIOC_GETTIMEOUT:
			return put_user(heartbeat, p);
	}
}

/**
 *	wdt_open:
 *	@inode: inode of device
 *	@file: file handle to device
 *
 *	The watchdog device has been opened. The watchdog device is single
 *	open and on opening we load the counters. Counter zero is a 100Hz
 *	cascade, into counter 1 which downcounts to reboot. When the counter
 *	triggers counter 2 downcounts the length of the reset pulse which
 *	set set to be as long as possible.
 */

static int wdt_open(struct inode *inode, struct file *file)
{
	if(test_and_set_bit(0, &wdt_is_open))
		return -EBUSY;
	/*
	 *	Activate
	 */
	wdt_start();
	return nonseekable_open(inode, file);
}

/**
 *	wdt_release:
 *	@inode: inode to board
 *	@file: file handle to board
 *
 *	The watchdog has a configurable API. There is a religious dispute
 *	between people who want their watchdog to be able to shut down and
 *	those who want to be sure if the watchdog manager dies the machine
 *	reboots. In the former case we disable the counters, in the latter
 *	case you have to open it again very soon.
 */

static int wdt_release(struct inode *inode, struct file *file)
{
	if (expect_close == 42) {
		wdt_stop();
		clear_bit(0, &wdt_is_open);
	} else {
		printk(KERN_CRIT "wdt: WDT device closed unexpectedly.  WDT will not stop!\n");
		wdt_ping();
	}
	expect_close = 0;
	return 0;
}

#ifdef CONFIG_WDT_501
/**
 *	wdt_temp_read:
 *	@file: file handle to the watchdog board
 *	@buf: buffer to write 1 byte into
 *	@count: length of buffer
 *	@ptr: offset (no seek allowed)
 *
 *	Temp_read reports the temperature in degrees Fahrenheit. The API is in
 *	farenheit. It was designed by an imperial measurement luddite.
 */

static ssize_t wdt_temp_read(struct file *file, char __user *buf, size_t count, loff_t *ptr)
{
	int temperature;

	if (wdt_get_temperature(&temperature))
		return -EFAULT;

	if (copy_to_user (buf, &temperature, 1))
		return -EFAULT;

	return 1;
}

/**
 *	wdt_temp_open:
 *	@inode: inode of device
 *	@file: file handle to device
 *
 *	The temperature device has been opened.
 */

static int wdt_temp_open(struct inode *inode, struct file *file)
{
	return nonseekable_open(inode, file);
}

/**
 *	wdt_temp_release:
 *	@inode: inode to board
 *	@file: file handle to board
 *
 *	The temperature device has been closed.
 */

static int wdt_temp_release(struct inode *inode, struct file *file)
{
	return 0;
}
#endif /* CONFIG_WDT_501 */

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

static int wdt_notify_sys(struct notifier_block *this, unsigned long code,
	void *unused)
{
	if(code==SYS_DOWN || code==SYS_HALT) {
		/* Turn the card off */
		wdt_stop();
	}
	return NOTIFY_DONE;
}

/*
 *	Kernel Interfaces
 */


static const struct file_operations wdt_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.write		= wdt_write,
	.ioctl		= wdt_ioctl,
	.open		= wdt_open,
	.release	= wdt_release,
};

static struct miscdevice wdt_miscdev = {
	.minor	= WATCHDOG_MINOR,
	.name	= "watchdog",
	.fops	= &wdt_fops,
};

#ifdef CONFIG_WDT_501
static const struct file_operations wdt_temp_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.read		= wdt_temp_read,
	.open		= wdt_temp_open,
	.release	= wdt_temp_release,
};

static struct miscdevice temp_miscdev = {
	.minor	= TEMP_MINOR,
	.name	= "temperature",
	.fops	= &wdt_temp_fops,
};
#endif /* CONFIG_WDT_501 */

/*
 *	The WDT card needs to learn about soft shutdowns in order to
 *	turn the timebomb registers off.
 */

static struct notifier_block wdt_notifier = {
	.notifier_call = wdt_notify_sys,
};

/**
 *	cleanup_module:
 *
 *	Unload the watchdog. You cannot do this with any file handles open.
 *	If your watchdog is set to continue ticking on close and you unload
 *	it, well it keeps ticking. We won't get the interrupt but the board
 *	will not touch PC memory so all is fine. You just have to load a new
 *	module in 60 seconds or reboot.
 */

static void __exit wdt_exit(void)
{
	misc_deregister(&wdt_miscdev);
#ifdef CONFIG_WDT_501
	misc_deregister(&temp_miscdev);
#endif /* CONFIG_WDT_501 */
	unregister_reboot_notifier(&wdt_notifier);
	free_irq(irq, NULL);
	release_region(io,8);
}

/**
 * 	wdt_init:
 *
 *	Set up the WDT watchdog board. All we have to do is grab the
 *	resources we require and bitch if anyone beat us to them.
 *	The open() function will actually kick the board off.
 */

static int __init wdt_init(void)
{
	int ret;

	/* Check that the heartbeat value is within it's range ; if not reset to the default */
	if (wdt_set_heartbeat(heartbeat)) {
		wdt_set_heartbeat(WD_TIMO);
		printk(KERN_INFO "wdt: heartbeat value must be 0<heartbeat<65536, using %d\n",
			WD_TIMO);
	}

	if (!request_region(io, 8, "wdt501p")) {
		printk(KERN_ERR "wdt: I/O address 0x%04x already in use\n", io);
		ret = -EBUSY;
		goto out;
	}

	ret = request_irq(irq, wdt_interrupt, IRQF_DISABLED, "wdt501p", NULL);
	if(ret) {
		printk(KERN_ERR "wdt: IRQ %d is not free.\n", irq);
		goto outreg;
	}

	ret = register_reboot_notifier(&wdt_notifier);
	if(ret) {
		printk(KERN_ERR "wdt: cannot register reboot notifier (err=%d)\n", ret);
		goto outirq;
	}

#ifdef CONFIG_WDT_501
	ret = misc_register(&temp_miscdev);
	if (ret) {
		printk(KERN_ERR "wdt: cannot register miscdev on minor=%d (err=%d)\n",
			TEMP_MINOR, ret);
		goto outrbt;
	}
#endif /* CONFIG_WDT_501 */

	ret = misc_register(&wdt_miscdev);
	if (ret) {
		printk(KERN_ERR "wdt: cannot register miscdev on minor=%d (err=%d)\n",
			WATCHDOG_MINOR, ret);
		goto outmisc;
	}

	ret = 0;
	printk(KERN_INFO "WDT500/501-P driver 0.10 at 0x%04x (Interrupt %d). heartbeat=%d sec (nowayout=%d)\n",
		io, irq, heartbeat, nowayout);
#ifdef CONFIG_WDT_501
	printk(KERN_INFO "wdt: Fan Tachometer is %s\n", (tachometer ? "Enabled" : "Disabled"));
#endif /* CONFIG_WDT_501 */

out:
	return ret;

outmisc:
#ifdef CONFIG_WDT_501
	misc_deregister(&temp_miscdev);
outrbt:
#endif /* CONFIG_WDT_501 */
	unregister_reboot_notifier(&wdt_notifier);
outirq:
	free_irq(irq, NULL);
outreg:
	release_region(io,8);
	goto out;
}

module_init(wdt_init);
module_exit(wdt_exit);

MODULE_AUTHOR("Alan Cox");
MODULE_DESCRIPTION("Driver for ISA ICS watchdog cards (WDT500/501)");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);
MODULE_ALIAS_MISCDEV(TEMP_MINOR);
MODULE_LICENSE("GPL");
