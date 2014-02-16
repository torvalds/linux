/*
 *	SoftDog:	A Software Watchdog Device
 *
 *	(c) Copyright 1996 Alan Cox <alan@lxorguk.ukuu.org.uk>,
 *							All Rights Reserved.
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
 *	Software only watchdog driver. Unlike its big brother the WDT501P
 *	driver this won't always recover a failed machine.
 *
 *  03/96: Angelo Haritsis <ah@doc.ic.ac.uk> :
 *	Modularised.
 *	Added soft_margin; use upon insmod to change the timer delay.
 *	NB: uses same minor as wdt (WATCHDOG_MINOR); we could use separate
 *	    minors.
 *
 *  19980911 Alan Cox
 *	Made SMP safe for 2.3.x
 *
 *  20011127 Joel Becker (jlbec@evilplan.org>
 *	Added soft_noboot; Allows testing the softdog trigger without
 *	requiring a recompile.
 *	Added WDIOC_GETTIMEOUT and WDIOC_SETTIMOUT.
 *
 *  20020530 Joel Becker <joel.becker@oracle.com>
 *	Added Matt Domsch's nowayout module option.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/timer.h>
#include <linux/watchdog.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>

#define TIMER_MARGIN	60		/* Default is 60 seconds */
static unsigned int soft_margin = TIMER_MARGIN;	/* in seconds */
module_param(soft_margin, uint, 0);
MODULE_PARM_DESC(soft_margin,
	"Watchdog soft_margin in seconds. (0 < soft_margin < 65536, default="
					__MODULE_STRING(TIMER_MARGIN) ")");

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout,
		"Watchdog cannot be stopped once started (default="
				__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

static int soft_noboot = 0;
module_param(soft_noboot, int, 0);
MODULE_PARM_DESC(soft_noboot,
	"Softdog action, set to 1 to ignore reboots, 0 to reboot (default=0)");

static int soft_panic;
module_param(soft_panic, int, 0);
MODULE_PARM_DESC(soft_panic,
	"Softdog action, set to 1 to panic, 0 to reboot (default=0)");

/*
 *	Our timer
 */

static void watchdog_fire(unsigned long);

static struct timer_list watchdog_ticktock =
		TIMER_INITIALIZER(watchdog_fire, 0, 0);

/*
 *	If the timer expires..
 */

static void watchdog_fire(unsigned long data)
{
	if (soft_noboot)
		pr_crit("Triggered - Reboot ignored\n");
	else if (soft_panic) {
		pr_crit("Initiating panic\n");
		panic("Software Watchdog Timer expired");
	} else {
		pr_crit("Initiating system reboot\n");
		emergency_restart();
		pr_crit("Reboot didn't ?????\n");
	}
}

/*
 *	Softdog operations
 */

static int softdog_ping(struct watchdog_device *w)
{
	mod_timer(&watchdog_ticktock, jiffies+(w->timeout*HZ));
	return 0;
}

static int softdog_stop(struct watchdog_device *w)
{
	del_timer(&watchdog_ticktock);
	return 0;
}

static int softdog_set_timeout(struct watchdog_device *w, unsigned int t)
{
	w->timeout = t;
	return 0;
}

/*
 *	Notifier for system down
 */

static int softdog_notify_sys(struct notifier_block *this, unsigned long code,
	void *unused)
{
	if (code == SYS_DOWN || code == SYS_HALT)
		/* Turn the WDT off */
		softdog_stop(NULL);
	return NOTIFY_DONE;
}

/*
 *	Kernel Interfaces
 */

static struct notifier_block softdog_notifier = {
	.notifier_call	= softdog_notify_sys,
};

static struct watchdog_info softdog_info = {
	.identity = "Software Watchdog",
	.options = WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE,
};

static struct watchdog_ops softdog_ops = {
	.owner = THIS_MODULE,
	.start = softdog_ping,
	.stop = softdog_stop,
	.set_timeout = softdog_set_timeout,
};

static struct watchdog_device softdog_dev = {
	.info = &softdog_info,
	.ops = &softdog_ops,
	.min_timeout = 1,
	.max_timeout = 0xFFFF
};

static int __init watchdog_init(void)
{
	int ret;

	/* Check that the soft_margin value is within it's range;
	   if not reset to the default */
	if (soft_margin < 1 || soft_margin > 65535) {
		pr_info("soft_margin must be 0 < soft_margin < 65536, using %d\n",
			TIMER_MARGIN);
		return -EINVAL;
	}
	softdog_dev.timeout = soft_margin;

	watchdog_set_nowayout(&softdog_dev, nowayout);

	ret = register_reboot_notifier(&softdog_notifier);
	if (ret) {
		pr_err("cannot register reboot notifier (err=%d)\n", ret);
		return ret;
	}

	ret = watchdog_register_device(&softdog_dev);
	if (ret) {
		unregister_reboot_notifier(&softdog_notifier);
		return ret;
	}

	pr_info("Software Watchdog Timer: 0.08 initialized. soft_noboot=%d soft_margin=%d sec soft_panic=%d (nowayout=%d)\n",
		soft_noboot, soft_margin, soft_panic, nowayout);

	return 0;
}

static void __exit watchdog_exit(void)
{
	watchdog_unregister_device(&softdog_dev);
	unregister_reboot_notifier(&softdog_notifier);
}

module_init(watchdog_init);
module_exit(watchdog_exit);

MODULE_AUTHOR("Alan Cox");
MODULE_DESCRIPTION("Software Watchdog Device Driver");
MODULE_LICENSE("GPL");
