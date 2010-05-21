/*
 *  PNX833x Hardware Watchdog Driver
 *  Copyright 2008 NXP Semiconductors
 *  Daniel Laird <daniel.j.laird@nxp.com>
 *  Andre McCurdy <andre.mccurdy@nxp.com>
 *
 *  Heavily based upon - IndyDog	0.3
 *  A Hardware Watchdog Device for SGI IP22
 *
 * (c) Copyright 2002 Guido Guenther <agx@sigxcpu.org>, All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * based on softdog.c by Alan Cox <alan@redhat.com>
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <asm/mach-pnx833x/pnx833x.h>

#define PFX "pnx833x: "
#define WATCHDOG_TIMEOUT 30		/* 30 sec Maximum timeout */
#define WATCHDOG_COUNT_FREQUENCY 68000000U /* Watchdog counts at 68MHZ. */

/** CONFIG block */
#define PNX833X_CONFIG                      (0x07000U)
#define PNX833X_CONFIG_CPU_WATCHDOG         (0x54)
#define PNX833X_CONFIG_CPU_WATCHDOG_COMPARE (0x58)
#define PNX833X_CONFIG_CPU_COUNTERS_CONTROL (0x1c)

/** RESET block */
#define PNX833X_RESET                       (0x08000U)
#define PNX833X_RESET_CONFIG                (0x08)

static int pnx833x_wdt_alive;

/* Set default timeout in MHZ.*/
static int pnx833x_wdt_timeout = (WATCHDOG_TIMEOUT * WATCHDOG_COUNT_FREQUENCY);
module_param(pnx833x_wdt_timeout, int, 0);
MODULE_PARM_DESC(timeout, "Watchdog timeout in Mhz. (68Mhz clock), default="
			__MODULE_STRING(pnx833x_wdt_timeout) "(30 seconds).");

static int nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, int, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
					__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

static int start_enabled = 1;
module_param(start_enabled, int, 0);
MODULE_PARM_DESC(start_enabled, "Watchdog is started on module insertion "
				"(default=" __MODULE_STRING(start_enabled) ")");

static void pnx833x_wdt_start(void)
{
	/* Enable watchdog causing reset. */
	PNX833X_REG(PNX833X_RESET + PNX833X_RESET_CONFIG) |= 0x1;
	/* Set timeout.*/
	PNX833X_REG(PNX833X_CONFIG +
		PNX833X_CONFIG_CPU_WATCHDOG_COMPARE) = pnx833x_wdt_timeout;
	/* Enable watchdog. */
	PNX833X_REG(PNX833X_CONFIG +
				PNX833X_CONFIG_CPU_COUNTERS_CONTROL) |= 0x1;

	printk(KERN_INFO PFX "Started watchdog timer.\n");
}

static void pnx833x_wdt_stop(void)
{
	/* Disable watchdog causing reset. */
	PNX833X_REG(PNX833X_RESET + PNX833X_CONFIG) &= 0xFFFFFFFE;
	/* Disable watchdog.*/
	PNX833X_REG(PNX833X_CONFIG +
			PNX833X_CONFIG_CPU_COUNTERS_CONTROL) &= 0xFFFFFFFE;

	printk(KERN_INFO PFX "Stopped watchdog timer.\n");
}

static void pnx833x_wdt_ping(void)
{
	PNX833X_REG(PNX833X_CONFIG +
		PNX833X_CONFIG_CPU_WATCHDOG_COMPARE) = pnx833x_wdt_timeout;
}

/*
 *	Allow only one person to hold it open
 */
static int pnx833x_wdt_open(struct inode *inode, struct file *file)
{
	if (test_and_set_bit(0, &pnx833x_wdt_alive))
		return -EBUSY;

	if (nowayout)
		__module_get(THIS_MODULE);

	/* Activate timer */
	if (!start_enabled)
		pnx833x_wdt_start();

	pnx833x_wdt_ping();

	printk(KERN_INFO "Started watchdog timer.\n");

	return nonseekable_open(inode, file);
}

static int pnx833x_wdt_release(struct inode *inode, struct file *file)
{
	/* Shut off the timer.
	 * Lock it in if it's a module and we defined ...NOWAYOUT */
	if (!nowayout)
		pnx833x_wdt_stop(); /* Turn the WDT off */

	clear_bit(0, &pnx833x_wdt_alive);
	return 0;
}

static ssize_t pnx833x_wdt_write(struct file *file, const char *data, size_t len, loff_t *ppos)
{
	/* Refresh the timer. */
	if (len)
		pnx833x_wdt_ping();

	return len;
}

static long pnx833x_wdt_ioctl(struct file *file, unsigned int cmd,
							unsigned long arg)
{
	int options, new_timeout = 0;
	uint32_t timeout, timeout_left = 0;

	static const struct watchdog_info ident = {
		.options = WDIOF_KEEPALIVEPING | WDIOF_SETTIMEOUT,
		.firmware_version = 0,
		.identity = "Hardware Watchdog for PNX833x",
	};

	switch (cmd) {
	default:
		return -ENOTTY;

	case WDIOC_GETSUPPORT:
		if (copy_to_user((struct watchdog_info *)arg,
				 &ident, sizeof(ident)))
			return -EFAULT;
		return 0;

	case WDIOC_GETSTATUS:
	case WDIOC_GETBOOTSTATUS:
		return put_user(0, (int *)arg);

	case WDIOC_SETOPTIONS:
		if (get_user(options, (int *)arg))
			return -EFAULT;

		if (options & WDIOS_DISABLECARD)
			pnx833x_wdt_stop();

		if (options & WDIOS_ENABLECARD)
			pnx833x_wdt_start();

		return 0;

	case WDIOC_KEEPALIVE:
		pnx833x_wdt_ping();
		return 0;

	case WDIOC_SETTIMEOUT:
	{
		if (get_user(new_timeout, (int *)arg))
			return -EFAULT;

		pnx833x_wdt_timeout = new_timeout;
		PNX833X_REG(PNX833X_CONFIG +
			PNX833X_CONFIG_CPU_WATCHDOG_COMPARE) = new_timeout;
		return put_user(new_timeout, (int *)arg);
	}

	case WDIOC_GETTIMEOUT:
		timeout = PNX833X_REG(PNX833X_CONFIG +
					PNX833X_CONFIG_CPU_WATCHDOG_COMPARE);
		return put_user(timeout, (int *)arg);

	case WDIOC_GETTIMELEFT:
		timeout_left = PNX833X_REG(PNX833X_CONFIG +
						PNX833X_CONFIG_CPU_WATCHDOG);
		return put_user(timeout_left, (int *)arg);

	}
}

static int pnx833x_wdt_notify_sys(struct notifier_block *this,
					unsigned long code, void *unused)
{
	if (code == SYS_DOWN || code == SYS_HALT)
		pnx833x_wdt_stop(); /* Turn the WDT off */

	return NOTIFY_DONE;
}

static const struct file_operations pnx833x_wdt_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.write		= pnx833x_wdt_write,
	.unlocked_ioctl	= pnx833x_wdt_ioctl,
	.open		= pnx833x_wdt_open,
	.release	= pnx833x_wdt_release,
};

static struct miscdevice pnx833x_wdt_miscdev = {
	.minor		= WATCHDOG_MINOR,
	.name		= "watchdog",
	.fops		= &pnx833x_wdt_fops,
};

static struct notifier_block pnx833x_wdt_notifier = {
	.notifier_call = pnx833x_wdt_notify_sys,
};

static char banner[] __initdata =
	KERN_INFO PFX "Hardware Watchdog Timer for PNX833x: Version 0.1\n";

static int __init watchdog_init(void)
{
	int ret, cause;

	/* Lets check the reason for the reset.*/
	cause = PNX833X_REG(PNX833X_RESET);
	/*If bit 31 is set then watchdog was cause of reset.*/
	if (cause & 0x80000000) {
		printk(KERN_INFO PFX "The system was previously reset due to "
			"the watchdog firing - please investigate...\n");
	}

	ret = register_reboot_notifier(&pnx833x_wdt_notifier);
	if (ret) {
		printk(KERN_ERR PFX
			"cannot register reboot notifier (err=%d)\n", ret);
		return ret;
	}

	ret = misc_register(&pnx833x_wdt_miscdev);
	if (ret) {
		printk(KERN_ERR PFX
			"cannot register miscdev on minor=%d (err=%d)\n",
			WATCHDOG_MINOR, ret);
		unregister_reboot_notifier(&pnx833x_wdt_notifier);
		return ret;
	}

	printk(banner);
	if (start_enabled)
		pnx833x_wdt_start();

	return 0;
}

static void __exit watchdog_exit(void)
{
	misc_deregister(&pnx833x_wdt_miscdev);
	unregister_reboot_notifier(&pnx833x_wdt_notifier);
}

module_init(watchdog_init);
module_exit(watchdog_exit);

MODULE_AUTHOR("Daniel Laird/Andre McCurdy");
MODULE_DESCRIPTION("Hardware Watchdog Device for PNX833x");
MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);
