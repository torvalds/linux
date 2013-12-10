/*
 *	National Semiconductor PC87307/PC97307 (ala SC1200) WDT driver
 *	(c) Copyright 2002 Zwane Mwaikambo <zwane@commfireservices.com>,
 *			All Rights Reserved.
 *	Based on wdt.c and wdt977.c by Alan Cox and Woody Suwalski respectively.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *	The author(s) of this software shall not be held liable for damages
 *	of any nature resulting due to the use of this software. This
 *	software is provided AS-IS with no warranties.
 *
 *	Changelog:
 *	20020220 Zwane Mwaikambo	Code based on datasheet, no hardware.
 *	20020221 Zwane Mwaikambo	Cleanups as suggested by Jeff Garzik
 *					and Alan Cox.
 *	20020222 Zwane Mwaikambo	Added probing.
 *	20020225 Zwane Mwaikambo	Added ISAPNP support.
 *	20020412 Rob Radez		Broke out start/stop functions
 *		 <rob@osinvestor.com>	Return proper status instead of
 *					temperature warning
 *					Add WDIOC_GETBOOTSTATUS and
 *					WDIOC_SETOPTIONS ioctls
 *					Fix CONFIG_WATCHDOG_NOWAYOUT
 *	20020530 Joel Becker		Add Matt Domsch's nowayout module
 *					option
 *	20030116 Adam Belay		Updated to the latest pnp code
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/ioport.h>
#include <linux/spinlock.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <linux/pnp.h>
#include <linux/fs.h>
#include <linux/semaphore.h>
#include <linux/io.h>
#include <linux/uaccess.h>

#define SC1200_MODULE_VER	"build 20020303"
#define SC1200_MODULE_NAME	"sc1200wdt"

#define	MAX_TIMEOUT	255	/* 255 minutes */
#define PMIR		(io)	/* Power Management Index Register */
#define PMDR		(io+1)	/* Power Management Data Register */

/* Data Register indexes */
#define FER1		0x00	/* Function enable register 1 */
#define FER2		0x01	/* Function enable register 2 */
#define PMC1		0x02	/* Power Management Ctrl 1 */
#define PMC2		0x03	/* Power Management Ctrl 2 */
#define PMC3		0x04	/* Power Management Ctrl 3 */
#define WDTO		0x05	/* Watchdog timeout register */
#define	WDCF		0x06	/* Watchdog config register */
#define WDST		0x07	/* Watchdog status register */

/* WDCF bitfields - which devices assert WDO */
#define KBC_IRQ		0x01	/* Keyboard Controller */
#define MSE_IRQ		0x02	/* Mouse */
#define UART1_IRQ	0x03	/* Serial0 */
#define UART2_IRQ	0x04	/* Serial1 */
/* 5 -7 are reserved */

static int timeout = 1;
static int io = -1;
static int io_len = 2;		/* for non plug and play */
static unsigned long open_flag;
static char expect_close;
static DEFINE_SPINLOCK(sc1200wdt_lock);	/* io port access serialisation */

#if defined CONFIG_PNP
static int isapnp = 1;
static struct pnp_dev *wdt_dev;

module_param(isapnp, int, 0);
MODULE_PARM_DESC(isapnp,
	"When set to 0 driver ISA PnP support will be disabled");
#endif

module_param(io, int, 0);
MODULE_PARM_DESC(io, "io port");
module_param(timeout, int, 0);
MODULE_PARM_DESC(timeout, "range is 0-255 minutes, default is 1");

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout,
	"Watchdog cannot be stopped once started (default="
				__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");



/* Read from Data Register */
static inline void __sc1200wdt_read_data(unsigned char index,
						unsigned char *data)
{
	outb_p(index, PMIR);
	*data = inb(PMDR);
}

static void sc1200wdt_read_data(unsigned char index, unsigned char *data)
{
	spin_lock(&sc1200wdt_lock);
	__sc1200wdt_read_data(index, data);
	spin_unlock(&sc1200wdt_lock);
}

/* Write to Data Register */
static inline void __sc1200wdt_write_data(unsigned char index,
						unsigned char data)
{
	outb_p(index, PMIR);
	outb(data, PMDR);
}

static inline void sc1200wdt_write_data(unsigned char index,
						unsigned char data)
{
	spin_lock(&sc1200wdt_lock);
	__sc1200wdt_write_data(index, data);
	spin_unlock(&sc1200wdt_lock);
}


static void sc1200wdt_start(void)
{
	unsigned char reg;
	spin_lock(&sc1200wdt_lock);

	__sc1200wdt_read_data(WDCF, &reg);
	/* assert WDO when any of the following interrupts are triggered too */
	reg |= (KBC_IRQ | MSE_IRQ | UART1_IRQ | UART2_IRQ);
	__sc1200wdt_write_data(WDCF, reg);
	/* set the timeout and get the ball rolling */
	__sc1200wdt_write_data(WDTO, timeout);

	spin_unlock(&sc1200wdt_lock);
}

static void sc1200wdt_stop(void)
{
	sc1200wdt_write_data(WDTO, 0);
}

/* This returns the status of the WDO signal, inactive high. */
static inline int sc1200wdt_status(void)
{
	unsigned char ret;

	sc1200wdt_read_data(WDST, &ret);
	/* If the bit is inactive, the watchdog is enabled, so return
	 * KEEPALIVEPING which is a bit of a kludge because there's nothing
	 * else for enabled/disabled status
	 */
	return (ret & 0x01) ? 0 : WDIOF_KEEPALIVEPING;
}

static int sc1200wdt_open(struct inode *inode, struct file *file)
{
	/* allow one at a time */
	if (test_and_set_bit(0, &open_flag))
		return -EBUSY;

	if (timeout > MAX_TIMEOUT)
		timeout = MAX_TIMEOUT;

	sc1200wdt_start();
	pr_info("Watchdog enabled, timeout = %d min(s)", timeout);

	return nonseekable_open(inode, file);
}


static long sc1200wdt_ioctl(struct file *file, unsigned int cmd,
						unsigned long arg)
{
	int new_timeout;
	void __user *argp = (void __user *)arg;
	int __user *p = argp;
	static const struct watchdog_info ident = {
		.options = WDIOF_KEEPALIVEPING | WDIOF_SETTIMEOUT |
							WDIOF_MAGICCLOSE,
		.firmware_version = 0,
		.identity = "PC87307/PC97307",
	};

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		if (copy_to_user(argp, &ident, sizeof(ident)))
			return -EFAULT;
		return 0;

	case WDIOC_GETSTATUS:
		return put_user(sc1200wdt_status(), p);

	case WDIOC_GETBOOTSTATUS:
		return put_user(0, p);

	case WDIOC_SETOPTIONS:
	{
		int options, retval = -EINVAL;

		if (get_user(options, p))
			return -EFAULT;

		if (options & WDIOS_DISABLECARD) {
			sc1200wdt_stop();
			retval = 0;
		}

		if (options & WDIOS_ENABLECARD) {
			sc1200wdt_start();
			retval = 0;
		}

		return retval;
	}
	case WDIOC_KEEPALIVE:
		sc1200wdt_write_data(WDTO, timeout);
		return 0;

	case WDIOC_SETTIMEOUT:
		if (get_user(new_timeout, p))
			return -EFAULT;
		/* the API states this is given in secs */
		new_timeout /= 60;
		if (new_timeout < 0 || new_timeout > MAX_TIMEOUT)
			return -EINVAL;
		timeout = new_timeout;
		sc1200wdt_write_data(WDTO, timeout);
		/* fall through and return the new timeout */

	case WDIOC_GETTIMEOUT:
		return put_user(timeout * 60, p);

	default:
		return -ENOTTY;
	}
}


static int sc1200wdt_release(struct inode *inode, struct file *file)
{
	if (expect_close == 42) {
		sc1200wdt_stop();
		pr_info("Watchdog disabled\n");
	} else {
		sc1200wdt_write_data(WDTO, timeout);
		pr_crit("Unexpected close!, timeout = %d min(s)\n", timeout);
	}
	clear_bit(0, &open_flag);
	expect_close = 0;

	return 0;
}


static ssize_t sc1200wdt_write(struct file *file, const char __user *data,
						size_t len, loff_t *ppos)
{
	if (len) {
		if (!nowayout) {
			size_t i;

			expect_close = 0;

			for (i = 0; i != len; i++) {
				char c;

				if (get_user(c, data + i))
					return -EFAULT;
				if (c == 'V')
					expect_close = 42;
			}
		}

		sc1200wdt_write_data(WDTO, timeout);
		return len;
	}

	return 0;
}


static int sc1200wdt_notify_sys(struct notifier_block *this,
					unsigned long code, void *unused)
{
	if (code == SYS_DOWN || code == SYS_HALT)
		sc1200wdt_stop();

	return NOTIFY_DONE;
}


static struct notifier_block sc1200wdt_notifier = {
	.notifier_call =	sc1200wdt_notify_sys,
};

static const struct file_operations sc1200wdt_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.write		= sc1200wdt_write,
	.unlocked_ioctl = sc1200wdt_ioctl,
	.open		= sc1200wdt_open,
	.release	= sc1200wdt_release,
};

static struct miscdevice sc1200wdt_miscdev = {
	.minor		= WATCHDOG_MINOR,
	.name		= "watchdog",
	.fops		= &sc1200wdt_fops,
};


static int __init sc1200wdt_probe(void)
{
	/* The probe works by reading the PMC3 register's default value of 0x0e
	 * there is one caveat, if the device disables the parallel port or any
	 * of the UARTs we won't be able to detect it.
	 * NB. This could be done with accuracy by reading the SID registers,
	 * but we don't have access to those io regions.
	 */

	unsigned char reg;

	sc1200wdt_read_data(PMC3, &reg);
	reg &= 0x0f;		/* we don't want the UART busy bits */
	return (reg == 0x0e) ? 0 : -ENODEV;
}


#if defined CONFIG_PNP

static struct pnp_device_id scl200wdt_pnp_devices[] = {
	/* National Semiconductor PC87307/PC97307 watchdog component */
	{.id = "NSC0800", .driver_data = 0},
	{.id = ""},
};

static int scl200wdt_pnp_probe(struct pnp_dev *dev,
					const struct pnp_device_id *dev_id)
{
	/* this driver only supports one card at a time */
	if (wdt_dev || !isapnp)
		return -EBUSY;

	wdt_dev = dev;
	io = pnp_port_start(wdt_dev, 0);
	io_len = pnp_port_len(wdt_dev, 0);

	if (!request_region(io, io_len, SC1200_MODULE_NAME)) {
		pr_err("Unable to register IO port %#x\n", io);
		return -EBUSY;
	}

	pr_info("PnP device found at io port %#x/%d\n", io, io_len);
	return 0;
}

static void scl200wdt_pnp_remove(struct pnp_dev *dev)
{
	if (wdt_dev) {
		release_region(io, io_len);
		wdt_dev = NULL;
	}
}

static struct pnp_driver scl200wdt_pnp_driver = {
	.name		= "scl200wdt",
	.id_table	= scl200wdt_pnp_devices,
	.probe		= scl200wdt_pnp_probe,
	.remove		= scl200wdt_pnp_remove,
};

#endif /* CONFIG_PNP */


static int __init sc1200wdt_init(void)
{
	int ret;

	pr_info("%s\n", SC1200_MODULE_VER);

#if defined CONFIG_PNP
	if (isapnp) {
		ret = pnp_register_driver(&scl200wdt_pnp_driver);
		if (ret)
			goto out_clean;
	}
#endif

	if (io == -1) {
		pr_err("io parameter must be specified\n");
		ret = -EINVAL;
		goto out_pnp;
	}

#if defined CONFIG_PNP
	/* now that the user has specified an IO port and we haven't detected
	 * any devices, disable pnp support */
	isapnp = 0;
	pnp_unregister_driver(&scl200wdt_pnp_driver);
#endif

	if (!request_region(io, io_len, SC1200_MODULE_NAME)) {
		pr_err("Unable to register IO port %#x\n", io);
		ret = -EBUSY;
		goto out_pnp;
	}

	ret = sc1200wdt_probe();
	if (ret)
		goto out_io;

	ret = register_reboot_notifier(&sc1200wdt_notifier);
	if (ret) {
		pr_err("Unable to register reboot notifier err = %d\n", ret);
		goto out_io;
	}

	ret = misc_register(&sc1200wdt_miscdev);
	if (ret) {
		pr_err("Unable to register miscdev on minor %d\n",
		       WATCHDOG_MINOR);
		goto out_rbt;
	}

	/* ret = 0 */

out_clean:
	return ret;

out_rbt:
	unregister_reboot_notifier(&sc1200wdt_notifier);

out_io:
	release_region(io, io_len);

out_pnp:
#if defined CONFIG_PNP
	if (isapnp)
		pnp_unregister_driver(&scl200wdt_pnp_driver);
#endif
	goto out_clean;
}


static void __exit sc1200wdt_exit(void)
{
	misc_deregister(&sc1200wdt_miscdev);
	unregister_reboot_notifier(&sc1200wdt_notifier);

#if defined CONFIG_PNP
	if (isapnp)
		pnp_unregister_driver(&scl200wdt_pnp_driver);
	else
#endif
	release_region(io, io_len);
}

module_init(sc1200wdt_init);
module_exit(sc1200wdt_exit);

MODULE_AUTHOR("Zwane Mwaikambo <zwane@commfireservices.com>");
MODULE_DESCRIPTION(
	"Driver for National Semiconductor PC87307/PC97307 watchdog component");
MODULE_LICENSE("GPL");
