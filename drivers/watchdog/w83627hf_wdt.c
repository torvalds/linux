/*
 *	w83627hf/thf WDT driver
 *
 *	(c) Copyright 2013 Guenter Roeck
 *		converted to watchdog infrastructure
 *
 *	(c) Copyright 2007 Vlad Drukker <vlad@storewiz.com>
 *		added support for W83627THF.
 *
 *	(c) Copyright 2003,2007 Pádraig Brady <P@draigBrady.com>
 *
 *	Based on advantechwdt.c which is based on wdt.c.
 *	Original copyright messages:
 *
 *	(c) Copyright 2000-2001 Marek Michalkiewicz <marekm@linux.org.pl>
 *
 *	(c) Copyright 1996 Alan Cox <alan@lxorguk.ukuu.org.uk>,
 *						All Rights Reserved.
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
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/watchdog.h>
#include <linux/ioport.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <linux/io.h>

#define WATCHDOG_NAME "w83627hf/thf/hg/dhg WDT"
#define WATCHDOG_TIMEOUT 60		/* 60 sec default timeout */

/* You must set this - there is no sane way to probe for this board. */
static int wdt_io = 0x2E;
module_param(wdt_io, int, 0);
MODULE_PARM_DESC(wdt_io, "w83627hf/thf WDT io port (default 0x2E)");

static int timeout;			/* in seconds */
module_param(timeout, int, 0);
MODULE_PARM_DESC(timeout,
		"Watchdog timeout in seconds. 1 <= timeout <= 255, default="
				__MODULE_STRING(WATCHDOG_TIMEOUT) ".");

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout,
		"Watchdog cannot be stopped once started (default="
				__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

/*
 *	Kernel methods.
 */

#define WDT_EFER (wdt_io+0)   /* Extended Function Enable Registers */
#define WDT_EFIR (wdt_io+0)   /* Extended Function Index Register
							(same as EFER) */
#define WDT_EFDR (WDT_EFIR+1) /* Extended Function Data Register */

#define W83627HF_LD_WDT		0x08

static void superio_outb(int reg, int val)
{
	outb(reg, WDT_EFER);
	outb(val, WDT_EFDR);
}

static inline int superio_inb(int reg)
{
	outb(reg, WDT_EFER);
	return inb(WDT_EFDR);
}

static int superio_enter(void)
{
	if (!request_muxed_region(wdt_io, 2, WATCHDOG_NAME))
		return -EBUSY;

	outb_p(0x87, WDT_EFER); /* Enter extended function mode */
	outb_p(0x87, WDT_EFER); /* Again according to manual */

	return 0;
}

static void superio_select(int ld)
{
	superio_outb(0x07, ld);
}

static void superio_exit(void)
{
	outb_p(0xAA, WDT_EFER); /* Leave extended function mode */
	release_region(wdt_io, 2);
}

/* tyan motherboards seem to set F5 to 0x4C ?
 * So explicitly init to appropriate value. */

static int w83627hf_init(struct watchdog_device *wdog)
{
	int ret;
	unsigned char t;

	ret = superio_enter();
	if (ret)
		return ret;

	superio_select(W83627HF_LD_WDT);
	t = superio_inb(0x20);	/* check chip version	*/
	if (t == 0x82) {	/* W83627THF		*/
		t = (superio_inb(0x2b) & 0xf7);
		superio_outb(0x2b, t | 0x04); /* set GPIO3 to WDT0 */
	} else if (t == 0x88 || t == 0xa0) {	/* W83627EHF / W83627DHG */
		t = superio_inb(0x2d);
		superio_outb(0x2d, t & ~0x01);	/* set GPIO5 to WDT0 */
	}

	/* set CR30 bit 0 to activate GPIO2 */
	t = superio_inb(0x30);
	if (!(t & 0x01))
		superio_outb(0x30, t | 0x01);

	t = superio_inb(0xF6);
	if (t != 0) {
		pr_info("Watchdog already running. Resetting timeout to %d sec\n",
			wdog->timeout);
		superio_outb(0xF6, wdog->timeout);
	}

	/* set second mode & disable keyboard turning off watchdog */
	t = superio_inb(0xF5) & ~0x0C;
	/* enable the WDTO# output low pulse to the KBRST# pin */
	t |= 0x02;
	superio_outb(0xF5, t);

	/* disable keyboard & mouse turning off watchdog */
	t = superio_inb(0xF7) & ~0xC0;
	superio_outb(0xF7, t);

	superio_exit();

	return 0;
}

static int wdt_set_time(unsigned int timeout)
{
	int ret;

	ret = superio_enter();
	if (ret)
		return ret;

	superio_select(W83627HF_LD_WDT);
	superio_outb(0xF6, timeout);
	superio_exit();

	return 0;
}

static int wdt_start(struct watchdog_device *wdog)
{
	return wdt_set_time(wdog->timeout);
}

static int wdt_stop(struct watchdog_device *wdog)
{
	return wdt_set_time(0);
}

static int wdt_set_timeout(struct watchdog_device *wdog, unsigned int timeout)
{
	wdog->timeout = timeout;

	return 0;
}

static unsigned int wdt_get_time(struct watchdog_device *wdog)
{
	unsigned int timeleft;
	int ret;

	ret = superio_enter();
	if (ret)
		return 0;

	superio_select(W83627HF_LD_WDT);
	timeleft = superio_inb(0xF6);
	superio_exit();

	return timeleft;
}

/*
 *	Notifier for system down
 */
static int wdt_notify_sys(struct notifier_block *this, unsigned long code,
	void *unused)
{
	if (code == SYS_DOWN || code == SYS_HALT)
		wdt_set_time(0);	/* Turn the WDT off */

	return NOTIFY_DONE;
}

/*
 *	Kernel Interfaces
 */

static struct watchdog_info wdt_info = {
	.options = WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE,
	.identity = "W83627HF Watchdog",
};

static struct watchdog_ops wdt_ops = {
	.owner = THIS_MODULE,
	.start = wdt_start,
	.stop = wdt_stop,
	.set_timeout = wdt_set_timeout,
	.get_timeleft = wdt_get_time,
};

static struct watchdog_device wdt_dev = {
	.info = &wdt_info,
	.ops = &wdt_ops,
	.timeout = WATCHDOG_TIMEOUT,
	.min_timeout = 1,
	.max_timeout = 255,
};

/*
 *	The WDT needs to learn about soft shutdowns in order to
 *	turn the timebomb registers off.
 */

static struct notifier_block wdt_notifier = {
	.notifier_call = wdt_notify_sys,
};

static int __init wdt_init(void)
{
	int ret;

	pr_info("WDT driver for the Winbond(TM) W83627HF/THF/HG/DHG Super I/O chip initialising\n");

	watchdog_init_timeout(&wdt_dev, timeout, NULL);
	watchdog_set_nowayout(&wdt_dev, nowayout);

	ret = w83627hf_init(&wdt_dev);
	if (ret) {
		pr_err("failed to initialize watchdog (err=%d)\n", ret);
		return ret;
	}

	ret = register_reboot_notifier(&wdt_notifier);
	if (ret != 0) {
		pr_err("cannot register reboot notifier (err=%d)\n", ret);
		return ret;
	}

	ret = watchdog_register_device(&wdt_dev);
	if (ret)
		goto unreg_reboot;

	pr_info("initialized. timeout=%d sec (nowayout=%d)\n",
		wdt_dev.timeout, nowayout);

	return ret;

unreg_reboot:
	unregister_reboot_notifier(&wdt_notifier);
	return ret;
}

static void __exit wdt_exit(void)
{
	watchdog_unregister_device(&wdt_dev);
	unregister_reboot_notifier(&wdt_notifier);
}

module_init(wdt_init);
module_exit(wdt_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Pádraig  Brady <P@draigBrady.com>");
MODULE_DESCRIPTION("w83627hf/thf WDT driver");
