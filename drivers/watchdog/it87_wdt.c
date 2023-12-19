// SPDX-License-Identifier: GPL-2.0-or-later
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
 *	IT8607, IT8613, IT8620, IT8622, IT8625, IT8628, IT8655, IT8665,
 *	IT8686, IT8702, IT8712, IT8716, IT8718, IT8720, IT8721, IT8726,
 *	IT8728, IT8772, IT8783 and IT8784.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/watchdog.h>

#define WATCHDOG_NAME		"IT87 WDT"

/* Defaults for Module Parameter */
#define DEFAULT_TIMEOUT		60
#define DEFAULT_TESTMODE	0
#define DEFAULT_NOWAYOUT	WATCHDOG_NOWAYOUT

/* IO Ports */
#define REG		0x2e
#define VAL		0x2f

/* Logical device Numbers LDN */
#define GPIO		0x07

/* Configuration Registers and Functions */
#define LDNREG		0x07
#define CHIPID		0x20
#define CHIPREV		0x22

/* Chip Id numbers */
#define NO_DEV_ID	0xffff
#define IT8607_ID	0x8607
#define IT8613_ID	0x8613
#define IT8620_ID	0x8620
#define IT8622_ID	0x8622
#define IT8625_ID	0x8625
#define IT8628_ID	0x8628
#define IT8655_ID	0x8655
#define IT8665_ID	0x8665
#define IT8686_ID	0x8686
#define IT8702_ID	0x8702
#define IT8705_ID	0x8705
#define IT8712_ID	0x8712
#define IT8716_ID	0x8716
#define IT8718_ID	0x8718
#define IT8720_ID	0x8720
#define IT8721_ID	0x8721
#define IT8726_ID	0x8726	/* the data sheet suggest wrongly 0x8716 */
#define IT8728_ID	0x8728
#define IT8772_ID	0x8772
#define IT8783_ID	0x8783
#define IT8784_ID	0x8784
#define IT8786_ID	0x8786

/* GPIO Configuration Registers LDN=0x07 */
#define WDTCTRL		0x71
#define WDTCFG		0x72
#define WDTVALLSB	0x73
#define WDTVALMSB	0x74

/* GPIO Bits WDTCFG */
#define WDT_TOV1	0x80
#define WDT_KRST	0x40
#define WDT_TOVE	0x20
#define WDT_PWROK	0x10 /* not in it8721 */
#define WDT_INT_MASK	0x0f

static unsigned int max_units, chip_type;

static unsigned int timeout = DEFAULT_TIMEOUT;
static int testmode = DEFAULT_TESTMODE;
static bool nowayout = DEFAULT_NOWAYOUT;

module_param(timeout, int, 0);
MODULE_PARM_DESC(timeout, "Watchdog timeout in seconds, default="
		__MODULE_STRING(DEFAULT_TIMEOUT));
module_param(testmode, int, 0);
MODULE_PARM_DESC(testmode, "Watchdog test mode (1 = no reboot), default="
		__MODULE_STRING(DEFAULT_TESTMODE));
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started, default="
		__MODULE_STRING(WATCHDOG_NOWAYOUT));

/* Superio Chip */

static inline int superio_enter(void)
{
	/*
	 * Try to reserve REG and REG + 1 for exclusive access.
	 */
	if (!request_muxed_region(REG, 2, WATCHDOG_NAME))
		return -EBUSY;

	outb(0x87, REG);
	outb(0x01, REG);
	outb(0x55, REG);
	outb(0x55, REG);
	return 0;
}

static inline void superio_exit(void)
{
	outb(0x02, REG);
	outb(0x02, VAL);
	release_region(REG, 2);
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

/* Internal function, should be called after superio_select(GPIO) */
static void _wdt_update_timeout(unsigned int t)
{
	unsigned char cfg = WDT_KRST;

	if (testmode)
		cfg = 0;

	if (t <= max_units)
		cfg |= WDT_TOV1;
	else
		t /= 60;

	if (chip_type != IT8721_ID)
		cfg |= WDT_PWROK;

	superio_outb(cfg, WDTCFG);
	superio_outb(t, WDTVALLSB);
	if (max_units > 255)
		superio_outb(t >> 8, WDTVALMSB);
}

static int wdt_update_timeout(unsigned int t)
{
	int ret;

	ret = superio_enter();
	if (ret)
		return ret;

	superio_select(GPIO);
	_wdt_update_timeout(t);
	superio_exit();

	return 0;
}

static int wdt_round_time(int t)
{
	t += 59;
	t -= t % 60;
	return t;
}

/* watchdog timer handling */

static int wdt_start(struct watchdog_device *wdd)
{
	return wdt_update_timeout(wdd->timeout);
}

static int wdt_stop(struct watchdog_device *wdd)
{
	return wdt_update_timeout(0);
}

/**
 *	wdt_set_timeout - set a new timeout value with watchdog ioctl
 *	@t: timeout value in seconds
 *
 *	The hardware device has a 8 or 16 bit watchdog timer (depends on
 *	chip version) that can be configured to count seconds or minutes.
 *
 *	Used within WDIOC_SETTIMEOUT watchdog device ioctl.
 */

static int wdt_set_timeout(struct watchdog_device *wdd, unsigned int t)
{
	int ret = 0;

	if (t > max_units)
		t = wdt_round_time(t);

	wdd->timeout = t;

	if (watchdog_hw_running(wdd))
		ret = wdt_update_timeout(t);

	return ret;
}

static const struct watchdog_info ident = {
	.options = WDIOF_SETTIMEOUT | WDIOF_MAGICCLOSE | WDIOF_KEEPALIVEPING,
	.firmware_version = 1,
	.identity = WATCHDOG_NAME,
};

static const struct watchdog_ops wdt_ops = {
	.owner = THIS_MODULE,
	.start = wdt_start,
	.stop = wdt_stop,
	.set_timeout = wdt_set_timeout,
};

static struct watchdog_device wdt_dev = {
	.info = &ident,
	.ops = &wdt_ops,
	.min_timeout = 1,
};

static int __init it87_wdt_init(void)
{
	u8  chip_rev;
	int rc;

	rc = superio_enter();
	if (rc)
		return rc;

	chip_type = superio_inw(CHIPID);
	chip_rev  = superio_inb(CHIPREV) & 0x0f;
	superio_exit();

	switch (chip_type) {
	case IT8702_ID:
		max_units = 255;
		break;
	case IT8712_ID:
		max_units = (chip_rev < 8) ? 255 : 65535;
		break;
	case IT8716_ID:
	case IT8726_ID:
		max_units = 65535;
		break;
	case IT8607_ID:
	case IT8613_ID:
	case IT8620_ID:
	case IT8622_ID:
	case IT8625_ID:
	case IT8628_ID:
	case IT8655_ID:
	case IT8665_ID:
	case IT8686_ID:
	case IT8718_ID:
	case IT8720_ID:
	case IT8721_ID:
	case IT8728_ID:
	case IT8772_ID:
	case IT8783_ID:
	case IT8784_ID:
	case IT8786_ID:
		max_units = 65535;
		break;
	case IT8705_ID:
		pr_err("Unsupported Chip found, Chip %04x Revision %02x\n",
		       chip_type, chip_rev);
		return -ENODEV;
	case NO_DEV_ID:
		pr_err("no device\n");
		return -ENODEV;
	default:
		pr_err("Unknown Chip found, Chip %04x Revision %04x\n",
		       chip_type, chip_rev);
		return -ENODEV;
	}

	rc = superio_enter();
	if (rc)
		return rc;

	superio_select(GPIO);
	superio_outb(WDT_TOV1, WDTCFG);
	superio_outb(0x00, WDTCTRL);
	superio_exit();

	if (timeout < 1 || timeout > max_units * 60) {
		timeout = DEFAULT_TIMEOUT;
		pr_warn("Timeout value out of range, use default %d sec\n",
			DEFAULT_TIMEOUT);
	}

	if (timeout > max_units)
		timeout = wdt_round_time(timeout);

	wdt_dev.timeout = timeout;
	wdt_dev.max_timeout = max_units * 60;

	watchdog_stop_on_reboot(&wdt_dev);
	rc = watchdog_register_device(&wdt_dev);
	if (rc) {
		pr_err("Cannot register watchdog device (err=%d)\n", rc);
		return rc;
	}

	pr_info("Chip IT%04x revision %d initialized. timeout=%d sec (nowayout=%d testmode=%d)\n",
		chip_type, chip_rev, timeout, nowayout, testmode);

	return 0;
}

static void __exit it87_wdt_exit(void)
{
	watchdog_unregister_device(&wdt_dev);
}

module_init(it87_wdt_init);
module_exit(it87_wdt_exit);

MODULE_AUTHOR("Oliver Schuster");
MODULE_DESCRIPTION("Hardware Watchdog Device Driver for IT87xx EC-LPC I/O");
MODULE_LICENSE("GPL");
