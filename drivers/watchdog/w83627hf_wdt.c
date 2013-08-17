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

static void w83627hf_select_wd_register(void)
{
	outb_p(0x87, WDT_EFER); /* Enter extended function mode */
	outb_p(0x87, WDT_EFER); /* Again according to manual */
	outb_p(0x07, WDT_EFER); /* point to logical device number reg */
	outb_p(0x08, WDT_EFDR); /* select logical device 8 (GPIO2) */
}

static void w83627hf_unselect_wd_register(void)
{
	outb_p(0xAA, WDT_EFER); /* Leave extended function mode */
}

/* tyan motherboards seem to set F5 to 0x4C ?
 * So explicitly init to appropriate value. */

static void w83627hf_init(struct watchdog_device *wdog)
{
	unsigned char t;

	w83627hf_select_wd_register();

	outb(0x20, WDT_EFER);	/* check chip version	*/
	t = inb(WDT_EFDR);
	if (t == 0x82) {	/* W83627THF		*/
		outb_p(0x2b, WDT_EFER); /* select GPIO3 */
		t = ((inb_p(WDT_EFDR) & 0xf7) | 0x04); /* select WDT0 */
		outb_p(0x2b, WDT_EFER);
		outb_p(t, WDT_EFDR);	/* set GPIO3 to WDT0 */
	} else if (t == 0x88 || t == 0xa0) {	/* W83627EHF / W83627DHG */
		outb_p(0x2d, WDT_EFER); /* select GPIO5 */
		t = inb_p(WDT_EFDR) & ~0x01; /* PIN77 -> WDT0# */
		outb_p(0x2d, WDT_EFER);
		outb_p(t, WDT_EFDR); /* set GPIO5 to WDT0 */
	}

	outb_p(0x30, WDT_EFER); /* select CR30 */
	outb_p(0x01, WDT_EFDR); /* set bit 0 to activate GPIO2 */

	outb_p(0xF6, WDT_EFER); /* Select CRF6 */
	t = inb_p(WDT_EFDR);      /* read CRF6 */
	if (t != 0) {
		pr_info("Watchdog already running. Resetting timeout to %d sec\n",
			wdog->timeout);
		outb_p(wdog->timeout, WDT_EFDR);	/* Write back to CRF6 */
	}

	outb_p(0xF5, WDT_EFER); /* Select CRF5 */
	t = inb_p(WDT_EFDR);      /* read CRF5 */
	t &= ~0x0C;               /* set second mode & disable keyboard
				    turning off watchdog */
	t |= 0x02;		  /* enable the WDTO# output low pulse
				    to the KBRST# pin (PIN60) */
	outb_p(t, WDT_EFDR);    /* Write back to CRF5 */

	outb_p(0xF7, WDT_EFER); /* Select CRF7 */
	t = inb_p(WDT_EFDR);      /* read CRF7 */
	t &= ~0xC0;               /* disable keyboard & mouse turning off
				    watchdog */
	outb_p(t, WDT_EFDR);    /* Write back to CRF7 */

	w83627hf_unselect_wd_register();
}

static int wdt_set_time(unsigned int timeout)
{
	w83627hf_select_wd_register();

	outb_p(0xF6, WDT_EFER);    /* Select CRF6 */
	outb_p(timeout, WDT_EFDR); /* Write Timeout counter to CRF6 */

	w83627hf_unselect_wd_register();

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

	w83627hf_select_wd_register();

	outb_p(0xF6, WDT_EFER);    /* Select CRF6 */
	timeleft = inb_p(WDT_EFDR); /* Read Timeout counter to CRF6 */

	w83627hf_unselect_wd_register();

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

	if (!request_region(wdt_io, 1, WATCHDOG_NAME)) {
		pr_err("I/O address 0x%04x already in use\n", wdt_io);
		return -EIO;
	}

	watchdog_init_timeout(&wdt_dev, timeout, NULL);
	watchdog_set_nowayout(&wdt_dev, nowayout);

	w83627hf_init(&wdt_dev);

	ret = register_reboot_notifier(&wdt_notifier);
	if (ret != 0) {
		pr_err("cannot register reboot notifier (err=%d)\n", ret);
		goto unreg_regions;
	}

	ret = watchdog_register_device(&wdt_dev);
	if (ret)
		goto unreg_reboot;

	pr_info("initialized. timeout=%d sec (nowayout=%d)\n",
		wdt_dev.timeout, nowayout);

	return ret;

unreg_reboot:
	unregister_reboot_notifier(&wdt_notifier);
unreg_regions:
	release_region(wdt_io, 1);
	return ret;
}

static void __exit wdt_exit(void)
{
	watchdog_unregister_device(&wdt_dev);
	unregister_reboot_notifier(&wdt_notifier);
	release_region(wdt_io, 1);
}

module_init(wdt_init);
module_exit(wdt_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Pádraig  Brady <P@draigBrady.com>");
MODULE_DESCRIPTION("w83627hf/thf WDT driver");
