/*
 *  MachZ ZF-Logic Watchdog Timer driver for Linux
 *
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 *  The author does NOT admit liability nor provide warranty for
 *  any of this software. This material is provided "AS-IS" in
 *  the hope that it may be useful for others.
 *
 *  Author: Fernando Fuganti <fuganti@conectiva.com.br>
 *
 *  Based on sbc60xxwdt.c by Jakob Oestergaard
 *
 *
 *  We have two timers (wd#1, wd#2) driven by a 32 KHz clock with the
 *  following periods:
 *      wd#1 - 2 seconds;
 *      wd#2 - 7.2 ms;
 *  After the expiration of wd#1, it can generate a NMI, SCI, SMI, or
 *  a system RESET and it starts wd#2 that unconditionally will RESET
 *  the system when the counter reaches zero.
 *
 *  14-Dec-2001 Matt Domsch <Matt_Domsch@dell.com>
 *      Added nowayout module option to override CONFIG_WATCHDOG_NOWAYOUT
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/fs.h>
#include <linux/ioport.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/uaccess.h>


/* ports */
#define ZF_IOBASE	0x218
#define INDEX		0x218
#define DATA_B		0x219
#define DATA_W		0x21A
#define DATA_D		0x21A

/* indexes */			/* size */
#define ZFL_VERSION	0x02	/* 16   */
#define CONTROL		0x10	/* 16   */
#define STATUS		0x12	/* 8    */
#define COUNTER_1	0x0C	/* 16   */
#define COUNTER_2	0x0E	/* 8    */
#define PULSE_LEN	0x0F	/* 8    */

/* controls */
#define ENABLE_WD1	0x0001
#define ENABLE_WD2	0x0002
#define RESET_WD1	0x0010
#define RESET_WD2	0x0020
#define GEN_SCI		0x0100
#define GEN_NMI		0x0200
#define GEN_SMI		0x0400
#define GEN_RESET	0x0800


/* utilities */

#define WD1	0
#define WD2	1

#define zf_writew(port, data)  { outb(port, INDEX); outw(data, DATA_W); }
#define zf_writeb(port, data)  { outb(port, INDEX); outb(data, DATA_B); }
#define zf_get_ZFL_version()   zf_readw(ZFL_VERSION)


static unsigned short zf_readw(unsigned char port)
{
	outb(port, INDEX);
	return inw(DATA_W);
}


MODULE_AUTHOR("Fernando Fuganti <fuganti@conectiva.com.br>");
MODULE_DESCRIPTION("MachZ ZF-Logic Watchdog driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout,
		"Watchdog cannot be stopped once started (default="
				__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

#define PFX "machzwd"

static const struct watchdog_info zf_info = {
	.options		= WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE,
	.firmware_version	= 1,
	.identity		= "ZF-Logic watchdog",
};


/*
 * action refers to action taken when watchdog resets
 * 0 = GEN_RESET
 * 1 = GEN_SMI
 * 2 = GEN_NMI
 * 3 = GEN_SCI
 * defaults to GEN_RESET (0)
 */
static int action;
module_param(action, int, 0);
MODULE_PARM_DESC(action, "after watchdog resets, generate: "
				"0 = RESET(*)  1 = SMI  2 = NMI  3 = SCI");

static void zf_ping(unsigned long data);

static int zf_action = GEN_RESET;
static unsigned long zf_is_open;
static char zf_expect_close;
static DEFINE_SPINLOCK(zf_port_lock);
static DEFINE_TIMER(zf_timer, zf_ping, 0, 0);
static unsigned long next_heartbeat;


/* timeout for user land heart beat (10 seconds) */
#define ZF_USER_TIMEO (HZ*10)

/* timeout for hardware watchdog (~500ms) */
#define ZF_HW_TIMEO (HZ/2)

/* number of ticks on WD#1 (driven by a 32KHz clock, 2s) */
#define ZF_CTIMEOUT 0xffff

#ifndef ZF_DEBUG
#define dprintk(format, args...)
#else
#define dprintk(format, args...)					\
	pr_debug(":%s:%d: " format, __func__, __LINE__ , ## args)
#endif


static inline void zf_set_status(unsigned char new)
{
	zf_writeb(STATUS, new);
}


/* CONTROL register functions */

static inline unsigned short zf_get_control(void)
{
	return zf_readw(CONTROL);
}

static inline void zf_set_control(unsigned short new)
{
	zf_writew(CONTROL, new);
}


/* WD#? counter functions */
/*
 *	Just set counter value
 */

static inline void zf_set_timer(unsigned short new, unsigned char n)
{
	switch (n) {
	case WD1:
		zf_writew(COUNTER_1, new);
	case WD2:
		zf_writeb(COUNTER_2, new > 0xff ? 0xff : new);
	default:
		return;
	}
}

/*
 * stop hardware timer
 */
static void zf_timer_off(void)
{
	unsigned int ctrl_reg = 0;
	unsigned long flags;

	/* stop internal ping */
	del_timer_sync(&zf_timer);

	spin_lock_irqsave(&zf_port_lock, flags);
	/* stop watchdog timer */
	ctrl_reg = zf_get_control();
	ctrl_reg |= (ENABLE_WD1|ENABLE_WD2);	/* disable wd1 and wd2 */
	ctrl_reg &= ~(ENABLE_WD1|ENABLE_WD2);
	zf_set_control(ctrl_reg);
	spin_unlock_irqrestore(&zf_port_lock, flags);

	pr_info("Watchdog timer is now disabled\n");
}


/*
 * start hardware timer
 */
static void zf_timer_on(void)
{
	unsigned int ctrl_reg = 0;
	unsigned long flags;

	spin_lock_irqsave(&zf_port_lock, flags);

	zf_writeb(PULSE_LEN, 0xff);

	zf_set_timer(ZF_CTIMEOUT, WD1);

	/* user land ping */
	next_heartbeat = jiffies + ZF_USER_TIMEO;

	/* start the timer for internal ping */
	mod_timer(&zf_timer, jiffies + ZF_HW_TIMEO);

	/* start watchdog timer */
	ctrl_reg = zf_get_control();
	ctrl_reg |= (ENABLE_WD1|zf_action);
	zf_set_control(ctrl_reg);
	spin_unlock_irqrestore(&zf_port_lock, flags);

	pr_info("Watchdog timer is now enabled\n");
}


static void zf_ping(unsigned long data)
{
	unsigned int ctrl_reg = 0;
	unsigned long flags;

	zf_writeb(COUNTER_2, 0xff);

	if (time_before(jiffies, next_heartbeat)) {
		dprintk("time_before: %ld\n", next_heartbeat - jiffies);
		/*
		 * reset event is activated by transition from 0 to 1 on
		 * RESET_WD1 bit and we assume that it is already zero...
		 */

		spin_lock_irqsave(&zf_port_lock, flags);
		ctrl_reg = zf_get_control();
		ctrl_reg |= RESET_WD1;
		zf_set_control(ctrl_reg);

		/* ...and nothing changes until here */
		ctrl_reg &= ~(RESET_WD1);
		zf_set_control(ctrl_reg);
		spin_unlock_irqrestore(&zf_port_lock, flags);

		mod_timer(&zf_timer, jiffies + ZF_HW_TIMEO);
	} else
		pr_crit("I will reset your machine\n");
}

static ssize_t zf_write(struct file *file, const char __user *buf, size_t count,
								loff_t *ppos)
{
	/* See if we got the magic character */
	if (count) {
		/*
		 * no need to check for close confirmation
		 * no way to disable watchdog ;)
		 */
		if (!nowayout) {
			size_t ofs;
			/*
			 * note: just in case someone wrote the magic character
			 * five months ago...
			 */
			zf_expect_close = 0;

			/* now scan */
			for (ofs = 0; ofs != count; ofs++) {
				char c;
				if (get_user(c, buf + ofs))
					return -EFAULT;
				if (c == 'V') {
					zf_expect_close = 42;
					dprintk("zf_expect_close = 42\n");
				}
			}
		}

		/*
		 * Well, anyhow someone wrote to us,
		 * we should return that favour
		 */
		next_heartbeat = jiffies + ZF_USER_TIMEO;
		dprintk("user ping at %ld\n", jiffies);
	}
	return count;
}

static long zf_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	int __user *p = argp;
	switch (cmd) {
	case WDIOC_GETSUPPORT:
		if (copy_to_user(argp, &zf_info, sizeof(zf_info)))
			return -EFAULT;
		break;
	case WDIOC_GETSTATUS:
	case WDIOC_GETBOOTSTATUS:
		return put_user(0, p);
	case WDIOC_KEEPALIVE:
		zf_ping(0);
		break;
	default:
		return -ENOTTY;
	}
	return 0;
}

static int zf_open(struct inode *inode, struct file *file)
{
	if (test_and_set_bit(0, &zf_is_open))
		return -EBUSY;
	if (nowayout)
		__module_get(THIS_MODULE);
	zf_timer_on();
	return nonseekable_open(inode, file);
}

static int zf_close(struct inode *inode, struct file *file)
{
	if (zf_expect_close == 42)
		zf_timer_off();
	else {
		del_timer(&zf_timer);
		pr_err("device file closed unexpectedly. Will not stop the WDT!\n");
	}
	clear_bit(0, &zf_is_open);
	zf_expect_close = 0;
	return 0;
}

/*
 * Notifier for system down
 */

static int zf_notify_sys(struct notifier_block *this, unsigned long code,
								void *unused)
{
	if (code == SYS_DOWN || code == SYS_HALT)
		zf_timer_off();
	return NOTIFY_DONE;
}

static const struct file_operations zf_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.write		= zf_write,
	.unlocked_ioctl = zf_ioctl,
	.open		= zf_open,
	.release	= zf_close,
};

static struct miscdevice zf_miscdev = {
	.minor = WATCHDOG_MINOR,
	.name = "watchdog",
	.fops = &zf_fops,
};


/*
 * The device needs to learn about soft shutdowns in order to
 * turn the timebomb registers off.
 */
static struct notifier_block zf_notifier = {
	.notifier_call = zf_notify_sys,
};

static void __init zf_show_action(int act)
{
	static const char * const str[] = { "RESET", "SMI", "NMI", "SCI" };

	pr_info("Watchdog using action = %s\n", str[act]);
}

static int __init zf_init(void)
{
	int ret;

	pr_info("MachZ ZF-Logic Watchdog driver initializing\n");

	ret = zf_get_ZFL_version();
	if (!ret || ret == 0xffff) {
		pr_warn("no ZF-Logic found\n");
		return -ENODEV;
	}

	if (action <= 3 && action >= 0)
		zf_action = zf_action >> action;
	else
		action = 0;

	zf_show_action(action);

	if (!request_region(ZF_IOBASE, 3, "MachZ ZFL WDT")) {
		pr_err("cannot reserve I/O ports at %d\n", ZF_IOBASE);
		ret = -EBUSY;
		goto no_region;
	}

	ret = register_reboot_notifier(&zf_notifier);
	if (ret) {
		pr_err("can't register reboot notifier (err=%d)\n", ret);
		goto no_reboot;
	}

	ret = misc_register(&zf_miscdev);
	if (ret) {
		pr_err("can't misc_register on minor=%d\n", WATCHDOG_MINOR);
		goto no_misc;
	}

	zf_set_status(0);
	zf_set_control(0);

	return 0;

no_misc:
	unregister_reboot_notifier(&zf_notifier);
no_reboot:
	release_region(ZF_IOBASE, 3);
no_region:
	return ret;
}


static void __exit zf_exit(void)
{
	zf_timer_off();

	misc_deregister(&zf_miscdev);
	unregister_reboot_notifier(&zf_notifier);
	release_region(ZF_IOBASE, 3);
}

module_init(zf_init);
module_exit(zf_exit);
