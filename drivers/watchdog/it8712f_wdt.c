// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	IT8712F "Smart Guardian" Watchdog support
 *
 *	Copyright (c) 2006-2007 Jorge Boncompte - DTI2 <jorge@dti2.net>
 *
 *	Based on info and code taken from:
 *
 *	drivers/char/watchdog/scx200_wdt.c
 *	drivers/hwmon/it87.c
 *	IT8712F EC-LPC I/O Preliminary Specification 0.8.2
 *	IT8712F EC-LPC I/O Preliminary Specification 0.9.3
 *
 *	The author(s) of this software shall not be held liable for damages
 *	of any nature resulting due to the use of this software. This
 *	software is provided AS-IS with no warranties.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/fs.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/ioport.h>

#define DEBUG
#define NAME "it8712f_wdt"

MODULE_AUTHOR("Jorge Boncompte - DTI2 <jorge@dti2.net>");
MODULE_DESCRIPTION("IT8712F Watchdog Driver");
MODULE_LICENSE("GPL");

static int max_units = 255;
static int margin = 60;		/* in seconds */
module_param(margin, int, 0);
MODULE_PARM_DESC(margin, "Watchdog margin in seconds");

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Disable watchdog shutdown on close");

static unsigned long wdt_open;
static unsigned expect_close;
static unsigned char revision;

/* Dog Food address - We use the game port address */
static unsigned short address;

#define	REG		0x2e	/* The register to read/write */
#define	VAL		0x2f	/* The value to read/write */

#define	LDN		0x07	/* Register: Logical device select */
#define	DEVID		0x20	/* Register: Device ID */
#define	DEVREV		0x22	/* Register: Device Revision */
#define ACT_REG		0x30	/* LDN Register: Activation */
#define BASE_REG	0x60	/* LDN Register: Base address */

#define IT8712F_DEVID	0x8712

#define LDN_GPIO	0x07	/* GPIO and Watch Dog Timer */
#define LDN_GAME	0x09	/* Game Port */

#define WDT_CONTROL	0x71	/* WDT Register: Control */
#define WDT_CONFIG	0x72	/* WDT Register: Configuration */
#define WDT_TIMEOUT	0x73	/* WDT Register: Timeout Value */

#define WDT_RESET_GAME	0x10	/* Reset timer on read or write to game port */
#define WDT_RESET_KBD	0x20	/* Reset timer on keyboard interrupt */
#define WDT_RESET_MOUSE	0x40	/* Reset timer on mouse interrupt */
#define WDT_RESET_CIR	0x80	/* Reset timer on consumer IR interrupt */

#define WDT_UNIT_SEC	0x80	/* If 0 in MINUTES */

#define WDT_OUT_PWROK	0x10	/* Pulse PWROK on timeout */
#define WDT_OUT_KRST	0x40	/* Pulse reset on timeout */

static int wdt_control_reg = WDT_RESET_GAME;
module_param(wdt_control_reg, int, 0);
MODULE_PARM_DESC(wdt_control_reg, "Value to write to watchdog control "
		"register. The default WDT_RESET_GAME resets the timer on "
		"game port reads that this driver generates. You can also "
		"use KBD, MOUSE or CIR if you have some external way to "
		"generate those interrupts.");

static int superio_inb(int reg)
{
	outb(reg, REG);
	return inb(VAL);
}

static void superio_outb(int val, int reg)
{
	outb(reg, REG);
	outb(val, VAL);
}

static int superio_inw(int reg)
{
	int val;
	outb(reg++, REG);
	val = inb(VAL) << 8;
	outb(reg, REG);
	val |= inb(VAL);
	return val;
}

static inline void superio_select(int ldn)
{
	outb(LDN, REG);
	outb(ldn, VAL);
}

static inline int superio_enter(void)
{
	/*
	 * Try to reserve REG and REG + 1 for exclusive access.
	 */
	if (!request_muxed_region(REG, 2, NAME))
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

static inline void it8712f_wdt_ping(void)
{
	if (wdt_control_reg & WDT_RESET_GAME)
		inb(address);
}

static void it8712f_wdt_update_margin(void)
{
	int config = WDT_OUT_KRST | WDT_OUT_PWROK;
	int units = margin;

	/* Switch to minutes precision if the configured margin
	 * value does not fit within the register width.
	 */
	if (units <= max_units) {
		config |= WDT_UNIT_SEC; /* else UNIT is MINUTES */
		pr_info("timer margin %d seconds\n", units);
	} else {
		units /= 60;
		pr_info("timer margin %d minutes\n", units);
	}
	superio_outb(config, WDT_CONFIG);

	if (revision >= 0x08)
		superio_outb(units >> 8, WDT_TIMEOUT + 1);
	superio_outb(units, WDT_TIMEOUT);
}

static int it8712f_wdt_get_status(void)
{
	if (superio_inb(WDT_CONTROL) & 0x01)
		return WDIOF_CARDRESET;
	else
		return 0;
}

static int it8712f_wdt_enable(void)
{
	int ret = superio_enter();
	if (ret)
		return ret;

	pr_debug("enabling watchdog timer\n");
	superio_select(LDN_GPIO);

	superio_outb(wdt_control_reg, WDT_CONTROL);

	it8712f_wdt_update_margin();

	superio_exit();

	it8712f_wdt_ping();

	return 0;
}

static int it8712f_wdt_disable(void)
{
	int ret = superio_enter();
	if (ret)
		return ret;

	pr_debug("disabling watchdog timer\n");
	superio_select(LDN_GPIO);

	superio_outb(0, WDT_CONFIG);
	superio_outb(0, WDT_CONTROL);
	if (revision >= 0x08)
		superio_outb(0, WDT_TIMEOUT + 1);
	superio_outb(0, WDT_TIMEOUT);

	superio_exit();
	return 0;
}

static int it8712f_wdt_notify(struct notifier_block *this,
		    unsigned long code, void *unused)
{
	if (code == SYS_HALT || code == SYS_POWER_OFF)
		if (!nowayout)
			it8712f_wdt_disable();

	return NOTIFY_DONE;
}

static struct notifier_block it8712f_wdt_notifier = {
	.notifier_call = it8712f_wdt_notify,
};

static ssize_t it8712f_wdt_write(struct file *file, const char __user *data,
					size_t len, loff_t *ppos)
{
	/* check for a magic close character */
	if (len) {
		size_t i;

		it8712f_wdt_ping();

		expect_close = 0;
		for (i = 0; i < len; ++i) {
			char c;
			if (get_user(c, data + i))
				return -EFAULT;
			if (c == 'V')
				expect_close = 42;
		}
	}

	return len;
}

static long it8712f_wdt_ioctl(struct file *file, unsigned int cmd,
							unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	int __user *p = argp;
	static const struct watchdog_info ident = {
		.identity = "IT8712F Watchdog",
		.firmware_version = 1,
		.options = WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING |
						WDIOF_MAGICCLOSE,
	};
	int value;
	int ret;

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		if (copy_to_user(argp, &ident, sizeof(ident)))
			return -EFAULT;
		return 0;
	case WDIOC_GETSTATUS:
		ret = superio_enter();
		if (ret)
			return ret;
		superio_select(LDN_GPIO);

		value = it8712f_wdt_get_status();

		superio_exit();

		return put_user(value, p);
	case WDIOC_GETBOOTSTATUS:
		return put_user(0, p);
	case WDIOC_KEEPALIVE:
		it8712f_wdt_ping();
		return 0;
	case WDIOC_SETTIMEOUT:
		if (get_user(value, p))
			return -EFAULT;
		if (value < 1)
			return -EINVAL;
		if (value > (max_units * 60))
			return -EINVAL;
		margin = value;
		ret = superio_enter();
		if (ret)
			return ret;
		superio_select(LDN_GPIO);

		it8712f_wdt_update_margin();

		superio_exit();
		it8712f_wdt_ping();
		/* Fall through */
	case WDIOC_GETTIMEOUT:
		if (put_user(margin, p))
			return -EFAULT;
		return 0;
	default:
		return -ENOTTY;
	}
}

static int it8712f_wdt_open(struct inode *inode, struct file *file)
{
	int ret;
	/* only allow one at a time */
	if (test_and_set_bit(0, &wdt_open))
		return -EBUSY;

	ret = it8712f_wdt_enable();
	if (ret)
		return ret;
	return stream_open(inode, file);
}

static int it8712f_wdt_release(struct inode *inode, struct file *file)
{
	if (expect_close != 42) {
		pr_warn("watchdog device closed unexpectedly, will not disable the watchdog timer\n");
	} else if (!nowayout) {
		if (it8712f_wdt_disable())
			pr_warn("Watchdog disable failed\n");
	}
	expect_close = 0;
	clear_bit(0, &wdt_open);

	return 0;
}

static const struct file_operations it8712f_wdt_fops = {
	.owner = THIS_MODULE,
	.llseek = no_llseek,
	.write = it8712f_wdt_write,
	.unlocked_ioctl = it8712f_wdt_ioctl,
	.open = it8712f_wdt_open,
	.release = it8712f_wdt_release,
};

static struct miscdevice it8712f_wdt_miscdev = {
	.minor = WATCHDOG_MINOR,
	.name = "watchdog",
	.fops = &it8712f_wdt_fops,
};

static int __init it8712f_wdt_find(unsigned short *address)
{
	int err = -ENODEV;
	int chip_type;
	int ret = superio_enter();
	if (ret)
		return ret;

	chip_type = superio_inw(DEVID);
	if (chip_type != IT8712F_DEVID)
		goto exit;

	superio_select(LDN_GAME);
	superio_outb(1, ACT_REG);
	if (!(superio_inb(ACT_REG) & 0x01)) {
		pr_err("Device not activated, skipping\n");
		goto exit;
	}

	*address = superio_inw(BASE_REG);
	if (*address == 0) {
		pr_err("Base address not set, skipping\n");
		goto exit;
	}

	err = 0;
	revision = superio_inb(DEVREV) & 0x0f;

	/* Later revisions have 16-bit values per datasheet 0.9.1 */
	if (revision >= 0x08)
		max_units = 65535;

	if (margin > (max_units * 60))
		margin = (max_units * 60);

	pr_info("Found IT%04xF chip revision %d - using DogFood address 0x%x\n",
		chip_type, revision, *address);

exit:
	superio_exit();
	return err;
}

static int __init it8712f_wdt_init(void)
{
	int err = 0;

	if (it8712f_wdt_find(&address))
		return -ENODEV;

	if (!request_region(address, 1, "IT8712F Watchdog")) {
		pr_warn("watchdog I/O region busy\n");
		return -EBUSY;
	}

	err = it8712f_wdt_disable();
	if (err) {
		pr_err("unable to disable watchdog timer\n");
		goto out;
	}

	err = register_reboot_notifier(&it8712f_wdt_notifier);
	if (err) {
		pr_err("unable to register reboot notifier\n");
		goto out;
	}

	err = misc_register(&it8712f_wdt_miscdev);
	if (err) {
		pr_err("cannot register miscdev on minor=%d (err=%d)\n",
		       WATCHDOG_MINOR, err);
		goto reboot_out;
	}

	return 0;


reboot_out:
	unregister_reboot_notifier(&it8712f_wdt_notifier);
out:
	release_region(address, 1);
	return err;
}

static void __exit it8712f_wdt_exit(void)
{
	misc_deregister(&it8712f_wdt_miscdev);
	unregister_reboot_notifier(&it8712f_wdt_notifier);
	release_region(address, 1);
}

module_init(it8712f_wdt_init);
module_exit(it8712f_wdt_exit);
