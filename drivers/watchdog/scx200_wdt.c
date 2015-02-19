/* drivers/char/watchdog/scx200_wdt.c

   National Semiconductor SCx200 Watchdog support

   Copyright (c) 2001,2002 Christer Weinigel <wingel@nano-system.com>

   Some code taken from:
   National Semiconductor PC87307/PC97307 (ala SC1200) WDT driver
   (c) Copyright 2002 Zwane Mwaikambo <zwane@commfireservices.com>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The author(s) of this software shall not be held liable for damages
   of any nature resulting due to the use of this software. This
   software is provided AS-IS with no warranties. */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/fs.h>
#include <linux/ioport.h>
#include <linux/scx200.h>
#include <linux/uaccess.h>
#include <linux/io.h>

#define DEBUG

MODULE_AUTHOR("Christer Weinigel <wingel@nano-system.com>");
MODULE_DESCRIPTION("NatSemi SCx200 Watchdog Driver");
MODULE_LICENSE("GPL");

static int margin = 60;		/* in seconds */
module_param(margin, int, 0);
MODULE_PARM_DESC(margin, "Watchdog margin in seconds");

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Disable watchdog shutdown on close");

static u16 wdto_restart;
static char expect_close;
static unsigned long open_lock;
static DEFINE_SPINLOCK(scx_lock);

/* Bits of the WDCNFG register */
#define W_ENABLE 0x00fa		/* Enable watchdog */
#define W_DISABLE 0x0000	/* Disable watchdog */

/* The scaling factor for the timer, this depends on the value of W_ENABLE */
#define W_SCALE (32768/1024)

static void scx200_wdt_ping(void)
{
	spin_lock(&scx_lock);
	outw(wdto_restart, scx200_cb_base + SCx200_WDT_WDTO);
	spin_unlock(&scx_lock);
}

static void scx200_wdt_update_margin(void)
{
	pr_info("timer margin %d seconds\n", margin);
	wdto_restart = margin * W_SCALE;
}

static void scx200_wdt_enable(void)
{
	pr_debug("enabling watchdog timer, wdto_restart = %d\n", wdto_restart);

	spin_lock(&scx_lock);
	outw(0, scx200_cb_base + SCx200_WDT_WDTO);
	outb(SCx200_WDT_WDSTS_WDOVF, scx200_cb_base + SCx200_WDT_WDSTS);
	outw(W_ENABLE, scx200_cb_base + SCx200_WDT_WDCNFG);
	spin_unlock(&scx_lock);

	scx200_wdt_ping();
}

static void scx200_wdt_disable(void)
{
	pr_debug("disabling watchdog timer\n");

	spin_lock(&scx_lock);
	outw(0, scx200_cb_base + SCx200_WDT_WDTO);
	outb(SCx200_WDT_WDSTS_WDOVF, scx200_cb_base + SCx200_WDT_WDSTS);
	outw(W_DISABLE, scx200_cb_base + SCx200_WDT_WDCNFG);
	spin_unlock(&scx_lock);
}

static int scx200_wdt_open(struct inode *inode, struct file *file)
{
	/* only allow one at a time */
	if (test_and_set_bit(0, &open_lock))
		return -EBUSY;
	scx200_wdt_enable();

	return nonseekable_open(inode, file);
}

static int scx200_wdt_release(struct inode *inode, struct file *file)
{
	if (expect_close != 42)
		pr_warn("watchdog device closed unexpectedly, will not disable the watchdog timer\n");
	else if (!nowayout)
		scx200_wdt_disable();
	expect_close = 0;
	clear_bit(0, &open_lock);

	return 0;
}

static int scx200_wdt_notify_sys(struct notifier_block *this,
				      unsigned long code, void *unused)
{
	if (code == SYS_HALT || code == SYS_POWER_OFF)
		if (!nowayout)
			scx200_wdt_disable();

	return NOTIFY_DONE;
}

static struct notifier_block scx200_wdt_notifier = {
	.notifier_call = scx200_wdt_notify_sys,
};

static ssize_t scx200_wdt_write(struct file *file, const char __user *data,
				     size_t len, loff_t *ppos)
{
	/* check for a magic close character */
	if (len) {
		size_t i;

		scx200_wdt_ping();

		expect_close = 0;
		for (i = 0; i < len; ++i) {
			char c;
			if (get_user(c, data + i))
				return -EFAULT;
			if (c == 'V')
				expect_close = 42;
		}

		return len;
	}

	return 0;
}

static long scx200_wdt_ioctl(struct file *file, unsigned int cmd,
							unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	int __user *p = argp;
	static const struct watchdog_info ident = {
		.identity = "NatSemi SCx200 Watchdog",
		.firmware_version = 1,
		.options = WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING |
						WDIOF_MAGICCLOSE,
	};
	int new_margin;

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		if (copy_to_user(argp, &ident, sizeof(ident)))
			return -EFAULT;
		return 0;
	case WDIOC_GETSTATUS:
	case WDIOC_GETBOOTSTATUS:
		if (put_user(0, p))
			return -EFAULT;
		return 0;
	case WDIOC_KEEPALIVE:
		scx200_wdt_ping();
		return 0;
	case WDIOC_SETTIMEOUT:
		if (get_user(new_margin, p))
			return -EFAULT;
		if (new_margin < 1)
			return -EINVAL;
		margin = new_margin;
		scx200_wdt_update_margin();
		scx200_wdt_ping();
	case WDIOC_GETTIMEOUT:
		if (put_user(margin, p))
			return -EFAULT;
		return 0;
	default:
		return -ENOTTY;
	}
}

static const struct file_operations scx200_wdt_fops = {
	.owner = THIS_MODULE,
	.llseek = no_llseek,
	.write = scx200_wdt_write,
	.unlocked_ioctl = scx200_wdt_ioctl,
	.open = scx200_wdt_open,
	.release = scx200_wdt_release,
};

static struct miscdevice scx200_wdt_miscdev = {
	.minor = WATCHDOG_MINOR,
	.name = "watchdog",
	.fops = &scx200_wdt_fops,
};

static int __init scx200_wdt_init(void)
{
	int r;

	pr_debug("NatSemi SCx200 Watchdog Driver\n");

	/* check that we have found the configuration block */
	if (!scx200_cb_present())
		return -ENODEV;

	if (!request_region(scx200_cb_base + SCx200_WDT_OFFSET,
			    SCx200_WDT_SIZE,
			    "NatSemi SCx200 Watchdog")) {
		pr_warn("watchdog I/O region busy\n");
		return -EBUSY;
	}

	scx200_wdt_update_margin();
	scx200_wdt_disable();

	r = register_reboot_notifier(&scx200_wdt_notifier);
	if (r) {
		pr_err("unable to register reboot notifier\n");
		release_region(scx200_cb_base + SCx200_WDT_OFFSET,
				SCx200_WDT_SIZE);
		return r;
	}

	r = misc_register(&scx200_wdt_miscdev);
	if (r) {
		unregister_reboot_notifier(&scx200_wdt_notifier);
		release_region(scx200_cb_base + SCx200_WDT_OFFSET,
				SCx200_WDT_SIZE);
		return r;
	}

	return 0;
}

static void __exit scx200_wdt_cleanup(void)
{
	misc_deregister(&scx200_wdt_miscdev);
	unregister_reboot_notifier(&scx200_wdt_notifier);
	release_region(scx200_cb_base + SCx200_WDT_OFFSET,
		       SCx200_WDT_SIZE);
}

module_init(scx200_wdt_init);
module_exit(scx200_wdt_cleanup);

/*
    Local variables:
	compile-command: "make -k -C ../.. SUBDIRS=drivers/char modules"
	c-basic-offset: 8
    End:
*/
