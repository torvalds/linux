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

static int wdt_io;
static int cr_wdt_timeout;	/* WDT timeout register */
static int cr_wdt_control;	/* WDT control register */

enum chips { w83627hf, w83627s, w83697hf, w83697ug, w83637hf, w83627thf,
	     w83687thf, w83627ehf, w83627dhg, w83627uhg, w83667hg, w83627dhg_p,
	     w83667hg_b, nct6775, nct6776, nct6779 };

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

#define W83627HF_ID		0x52
#define W83627S_ID		0x59
#define W83697HF_ID		0x60
#define W83697UG_ID		0x68
#define W83637HF_ID		0x70
#define W83627THF_ID		0x82
#define W83687THF_ID		0x85
#define W83627EHF_ID		0x88
#define W83627DHG_ID		0xa0
#define W83627UHG_ID		0xa2
#define W83667HG_ID		0xa5
#define W83627DHG_P_ID		0xb0
#define W83667HG_B_ID		0xb3
#define NCT6775_ID		0xb4
#define NCT6776_ID		0xc3
#define NCT6779_ID		0xc5

#define W83627HF_WDT_TIMEOUT	0xf6
#define W83697HF_WDT_TIMEOUT	0xf4

#define W83627HF_WDT_CONTROL	0xf5
#define W83697HF_WDT_CONTROL	0xf3

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

static int w83627hf_init(struct watchdog_device *wdog, enum chips chip)
{
	int ret;
	unsigned char t;

	ret = superio_enter();
	if (ret)
		return ret;

	superio_select(W83627HF_LD_WDT);

	/* set CR30 bit 0 to activate GPIO2 */
	t = superio_inb(0x30);
	if (!(t & 0x01))
		superio_outb(0x30, t | 0x01);

	switch (chip) {
	case w83627hf:
	case w83627s:
		t = superio_inb(0x2B) & ~0x10;
		superio_outb(0x2B, t); /* set GPIO24 to WDT0 */
		break;
	case w83697hf:
		/* Set pin 119 to WDTO# mode (= CR29, WDT0) */
		t = superio_inb(0x29) & ~0x60;
		t |= 0x20;
		superio_outb(0x29, t);
		break;
	case w83697ug:
		/* Set pin 118 to WDTO# mode */
		t = superio_inb(0x2b) & ~0x04;
		superio_outb(0x2b, t);
		break;
	case w83627thf:
		t = (superio_inb(0x2B) & ~0x08) | 0x04;
		superio_outb(0x2B, t); /* set GPIO3 to WDT0 */
		break;
	case w83627dhg:
	case w83627dhg_p:
		t = superio_inb(0x2D) & ~0x01; /* PIN77 -> WDT0# */
		superio_outb(0x2D, t); /* set GPIO5 to WDT0 */
		t = superio_inb(cr_wdt_control);
		t |= 0x02;	/* enable the WDTO# output low pulse
				 * to the KBRST# pin */
		superio_outb(cr_wdt_control, t);
		break;
	case w83637hf:
		break;
	case w83687thf:
		t = superio_inb(0x2C) & ~0x80; /* PIN47 -> WDT0# */
		superio_outb(0x2C, t);
		break;
	case w83627ehf:
	case w83627uhg:
	case w83667hg:
	case w83667hg_b:
	case nct6775:
	case nct6776:
	case nct6779:
		/*
		 * These chips have a fixed WDTO# output pin (W83627UHG),
		 * or support more than one WDTO# output pin.
		 * Don't touch its configuration, and hope the BIOS
		 * does the right thing.
		 */
		t = superio_inb(cr_wdt_control);
		t |= 0x02;	/* enable the WDTO# output low pulse
				 * to the KBRST# pin */
		superio_outb(cr_wdt_control, t);
		break;
	default:
		break;
	}

	t = superio_inb(cr_wdt_timeout);
	if (t != 0) {
		pr_info("Watchdog already running. Resetting timeout to %d sec\n",
			wdog->timeout);
		superio_outb(cr_wdt_timeout, wdog->timeout);
	}

	/* set second mode & disable keyboard turning off watchdog */
	t = superio_inb(cr_wdt_control) & ~0x0C;
	superio_outb(cr_wdt_control, t);

	/* reset trigger, disable keyboard & mouse turning off watchdog */
	t = superio_inb(0xF7) & ~0xD0;
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
	superio_outb(cr_wdt_timeout, timeout);
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
	timeleft = superio_inb(cr_wdt_timeout);
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

static int wdt_find(int addr)
{
	u8 val;
	int ret;

	cr_wdt_timeout = W83627HF_WDT_TIMEOUT;
	cr_wdt_control = W83627HF_WDT_CONTROL;

	ret = superio_enter();
	if (ret)
		return ret;
	superio_select(W83627HF_LD_WDT);
	val = superio_inb(0x20);
	switch (val) {
	case W83627HF_ID:
		ret = w83627hf;
		break;
	case W83627S_ID:
		ret = w83627s;
		break;
	case W83697HF_ID:
		ret = w83697hf;
		cr_wdt_timeout = W83697HF_WDT_TIMEOUT;
		cr_wdt_control = W83697HF_WDT_CONTROL;
		break;
	case W83697UG_ID:
		ret = w83697ug;
		cr_wdt_timeout = W83697HF_WDT_TIMEOUT;
		cr_wdt_control = W83697HF_WDT_CONTROL;
		break;
	case W83637HF_ID:
		ret = w83637hf;
		break;
	case W83627THF_ID:
		ret = w83627thf;
		break;
	case W83687THF_ID:
		ret = w83687thf;
		break;
	case W83627EHF_ID:
		ret = w83627ehf;
		break;
	case W83627DHG_ID:
		ret = w83627dhg;
		break;
	case W83627DHG_P_ID:
		ret = w83627dhg_p;
		break;
	case W83627UHG_ID:
		ret = w83627uhg;
		break;
	case W83667HG_ID:
		ret = w83667hg;
		break;
	case W83667HG_B_ID:
		ret = w83667hg_b;
		break;
	case NCT6775_ID:
		ret = nct6775;
		break;
	case NCT6776_ID:
		ret = nct6776;
		break;
	case NCT6779_ID:
		ret = nct6779;
		break;
	case 0xff:
		ret = -ENODEV;
		break;
	default:
		ret = -ENODEV;
		pr_err("Unsupported chip ID: 0x%02x\n", val);
		break;
	}
	superio_exit();
	return ret;
}

static int __init wdt_init(void)
{
	int ret;
	int chip;
	const char * const chip_name[] = {
		"W83627HF",
		"W83627S",
		"W83697HF",
		"W83697UG",
		"W83637HF",
		"W83627THF",
		"W83687THF",
		"W83627EHF",
		"W83627DHG",
		"W83627UHG",
		"W83667HG",
		"W83667DHG-P",
		"W83667HG-B",
		"NCT6775",
		"NCT6776",
		"NCT6779",
	};

	wdt_io = 0x2e;
	chip = wdt_find(0x2e);
	if (chip < 0) {
		wdt_io = 0x4e;
		chip = wdt_find(0x4e);
		if (chip < 0)
			return chip;
	}

	pr_info("WDT driver for %s Super I/O chip initialising\n",
		chip_name[chip]);

	watchdog_init_timeout(&wdt_dev, timeout, NULL);
	watchdog_set_nowayout(&wdt_dev, nowayout);

	ret = w83627hf_init(&wdt_dev, chip);
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
