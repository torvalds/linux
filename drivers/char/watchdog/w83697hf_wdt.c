/*
 *	w83697hf/hg WDT driver
 *
 *	(c) Copyright 2006 Samuel Tardieu <sam@rfc1149.net>
 *	(c) Copyright 2006 Marcus Junker <junker@anduras.de>
 *
 *	Based on w83627hf_wdt.c which is based on advantechwdt.c
 *	which is based on wdt.c.
 *	Original copyright messages:
 *
 *	(c) Copyright 2003 Pádraig Brady <P@draigBrady.com>
 *
 *	(c) Copyright 2000-2001 Marek Michalkiewicz <marekm@linux.org.pl>
 *
 *	(c) Copyright 1996 Alan Cox <alan@redhat.com>, All Rights Reserved.
 *				http://www.redhat.com
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *	Neither Marcus Junker nor ANDURAS AG admit liability nor provide
 *	warranty for any of this software. This material is provided
 *	"AS-IS" and at no charge.
 */

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
#include <linux/spinlock.h>

#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/system.h>

#define WATCHDOG_NAME "w83697hf/hg WDT"
#define PFX WATCHDOG_NAME ": "
#define WATCHDOG_TIMEOUT 60		/* 60 sec default timeout */

static unsigned long wdt_is_open;
static char expect_close;
static spinlock_t io_lock;

/* You must set this - there is no sane way to probe for this board. */
static int wdt_io = 0x2e;
module_param(wdt_io, int, 0);
MODULE_PARM_DESC(wdt_io, "w83697hf/hg WDT io port (default 0x2e, 0 = autodetect)");

static int timeout = WATCHDOG_TIMEOUT;	/* in seconds */
module_param(timeout, int, 0);
MODULE_PARM_DESC(timeout, "Watchdog timeout in seconds. 1<= timeout <=255, default=" __MODULE_STRING(WATCHDOG_TIMEOUT) ".");

static int nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, int, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default=" __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

/*
 *	Kernel methods.
 */

#define W83697HF_EFER (wdt_io+0)	/* Extended Function Enable Register */
#define W83697HF_EFIR (wdt_io+0)	/* Extended Function Index Register (same as EFER) */
#define W83697HF_EFDR (wdt_io+1)	/* Extended Function Data Register */

static inline void
w83697hf_unlock(void)
{
	outb_p(0x87, W83697HF_EFER);	/* Enter extended function mode */
	outb_p(0x87, W83697HF_EFER);	/* Again according to manual */
}

static inline void
w83697hf_lock(void)
{
	outb_p(0xAA, W83697HF_EFER);	/* Leave extended function mode */
}

/*
 *	The three functions w83697hf_get_reg(), w83697hf_set_reg() and
 *	w83697hf_write_timeout() must be called with the device unlocked.
 */

static unsigned char
w83697hf_get_reg(unsigned char reg)
{
	outb_p(reg, W83697HF_EFIR);
	return inb_p(W83697HF_EFDR);
}

static void
w83697hf_set_reg(unsigned char reg, unsigned char data)
{
	outb_p(reg, W83697HF_EFIR);
	outb_p(data, W83697HF_EFDR);
}

static void
w83697hf_write_timeout(int timeout)
{
	w83697hf_set_reg(0xF4, timeout);	/* Write Timeout counter to CRF4 */
}

static void
w83697hf_select_wdt(void)
{
	w83697hf_unlock();
	w83697hf_set_reg(0x07, 0x08);	/* Switch to logic device 8 (GPIO2) */
}

static inline void
w83697hf_deselect_wdt(void)
{
	w83697hf_lock();
}

static void
w83697hf_init(void)
{
	unsigned char bbuf;

	w83697hf_select_wdt();

	bbuf = w83697hf_get_reg(0x29);
	bbuf &= ~0x60;
	bbuf |= 0x20;
	w83697hf_set_reg(0x29, bbuf);	/* Set pin 119 to WDTO# mode (= CR29, WDT0) */

	bbuf = w83697hf_get_reg(0xF3);
	bbuf &= ~0x04;
	w83697hf_set_reg(0xF3, bbuf);	/* Count mode is seconds */

	w83697hf_deselect_wdt();
}

static int
wdt_ping(void)
{
	spin_lock(&io_lock);
	w83697hf_select_wdt();

	w83697hf_write_timeout(timeout);

	w83697hf_deselect_wdt();
	spin_unlock(&io_lock);
	return 0;
}

static int
wdt_enable(void)
{
	spin_lock(&io_lock);
	w83697hf_select_wdt();

	w83697hf_write_timeout(timeout);
	w83697hf_set_reg(0x30, 1);	/* Enable timer */

	w83697hf_deselect_wdt();
	spin_unlock(&io_lock);
	return 0;
}

static int
wdt_disable(void)
{
	spin_lock(&io_lock);
	w83697hf_select_wdt();

	w83697hf_set_reg(0x30, 0);	/* Disable timer */
	w83697hf_write_timeout(0);

	w83697hf_deselect_wdt();
	spin_unlock(&io_lock);
	return 0;
}

static int
wdt_set_heartbeat(int t)
{
	if ((t < 1) || (t > 255))
		return -EINVAL;

	timeout = t;
	return 0;
}

static ssize_t
wdt_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	if (count) {
		if (!nowayout) {
			size_t i;

			expect_close = 0;

			for (i = 0; i != count; i++) {
				char c;
				if (get_user(c, buf+i))
					return -EFAULT;
				if (c == 'V')
					expect_close = 42;
			}
		}
		wdt_ping();
	}
	return count;
}

static int
wdt_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
	  unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	int __user *p = argp;
	int new_timeout;
	static struct watchdog_info ident = {
		.options = WDIOF_KEEPALIVEPING | WDIOF_SETTIMEOUT | WDIOF_MAGICCLOSE,
		.firmware_version = 1,
		.identity = "W83697HF WDT",
	};

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		if (copy_to_user(argp, &ident, sizeof(ident)))
			return -EFAULT;
		break;

	case WDIOC_GETSTATUS:
	case WDIOC_GETBOOTSTATUS:
		return put_user(0, p);

	case WDIOC_KEEPALIVE:
		wdt_ping();
		break;

	case WDIOC_SETTIMEOUT:
		if (get_user(new_timeout, p))
			return -EFAULT;
		if (wdt_set_heartbeat(new_timeout))
			return -EINVAL;
		wdt_ping();
		/* Fall */

	case WDIOC_GETTIMEOUT:
		return put_user(timeout, p);

	case WDIOC_SETOPTIONS:
	{
		int options, retval = -EINVAL;

		if (get_user(options, p))
			return -EFAULT;

		if (options & WDIOS_DISABLECARD) {
			wdt_disable();
			retval = 0;
		}

		if (options & WDIOS_ENABLECARD) {
			wdt_enable();
			retval = 0;
		}

		return retval;
	}

	default:
		return -ENOTTY;
	}
	return 0;
}

static int
wdt_open(struct inode *inode, struct file *file)
{
	if (test_and_set_bit(0, &wdt_is_open))
		return -EBUSY;
	/*
	 *	Activate
	 */

	wdt_enable();
	return nonseekable_open(inode, file);
}

static int
wdt_close(struct inode *inode, struct file *file)
{
	if (expect_close == 42) {
		wdt_disable();
	} else {
		printk (KERN_CRIT PFX "Unexpected close, not stopping watchdog!\n");
		wdt_ping();
	}
	expect_close = 0;
	clear_bit(0, &wdt_is_open);
	return 0;
}

/*
 *	Notifier for system down
 */

static int
wdt_notify_sys(struct notifier_block *this, unsigned long code,
	void *unused)
{
	if (code == SYS_DOWN || code == SYS_HALT) {
		/* Turn the WDT off */
		wdt_disable();
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
	.release	= wdt_close,
};

static struct miscdevice wdt_miscdev = {
	.minor = WATCHDOG_MINOR,
	.name = "watchdog",
	.fops = &wdt_fops,
};

/*
 *	The WDT needs to learn about soft shutdowns in order to
 *	turn the timebomb registers off.
 */

static struct notifier_block wdt_notifier = {
	.notifier_call = wdt_notify_sys,
};

static int
w83697hf_check_wdt(void)
{
	if (!request_region(wdt_io, 2, WATCHDOG_NAME)) {
		printk (KERN_ERR PFX "I/O address 0x%x already in use\n", wdt_io);
		return -EIO;
	}

	printk (KERN_DEBUG PFX "Looking for watchdog at address 0x%x\n", wdt_io);
	w83697hf_unlock();
	if (w83697hf_get_reg(0x20) == 0x60) {
		printk (KERN_INFO PFX "watchdog found at address 0x%x\n", wdt_io);
		w83697hf_lock();
		return 0;
	}
	w83697hf_lock();	/* Reprotect in case it was a compatible device */

	printk (KERN_INFO PFX "watchdog not found at address 0x%x\n", wdt_io);
	release_region(wdt_io, 2);
	return -EIO;
}

static int w83697hf_ioports[] = { 0x2e, 0x4e, 0x00 };

static int __init
wdt_init(void)
{
	int ret, i, found = 0;

	spin_lock_init(&io_lock);

	printk (KERN_INFO PFX "WDT driver for W83697HF/HG initializing\n");

	if (wdt_io == 0) {
		/* we will autodetect the W83697HF/HG watchdog */
		for (i = 0; ((!found) && (w83697hf_ioports[i] != 0)); i++) {
			wdt_io = w83697hf_ioports[i];
			if (!w83697hf_check_wdt())
				found++;
		}
	} else {
		if (!w83697hf_check_wdt())
			found++;
	}

	if (!found) {
		printk (KERN_ERR PFX "No W83697HF/HG could be found\n");
		ret = -EIO;
		goto out;
	}

	w83697hf_init();
	wdt_disable();	/* Disable watchdog until first use */

	if (wdt_set_heartbeat(timeout)) {
		wdt_set_heartbeat(WATCHDOG_TIMEOUT);
		printk (KERN_INFO PFX "timeout value must be 1<=timeout<=255, using %d\n",
			WATCHDOG_TIMEOUT);
	}

	ret = register_reboot_notifier(&wdt_notifier);
	if (ret != 0) {
		printk (KERN_ERR PFX "cannot register reboot notifier (err=%d)\n",
			ret);
		goto unreg_regions;
	}

	ret = misc_register(&wdt_miscdev);
	if (ret != 0) {
		printk (KERN_ERR PFX "cannot register miscdev on minor=%d (err=%d)\n",
			WATCHDOG_MINOR, ret);
		goto unreg_reboot;
	}

	printk (KERN_INFO PFX "initialized. timeout=%d sec (nowayout=%d)\n",
		timeout, nowayout);

out:
	return ret;
unreg_reboot:
	unregister_reboot_notifier(&wdt_notifier);
unreg_regions:
	release_region(wdt_io, 2);
	goto out;
}

static void __exit
wdt_exit(void)
{
	misc_deregister(&wdt_miscdev);
	unregister_reboot_notifier(&wdt_notifier);
	release_region(wdt_io, 2);
}

module_init(wdt_init);
module_exit(wdt_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Marcus Junker <junker@anduras.de>, Samuel Tardieu <sam@rfc1149.net>");
MODULE_DESCRIPTION("w83697hf/hg WDT driver");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);
