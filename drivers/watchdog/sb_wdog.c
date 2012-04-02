/*
 * Watchdog driver for SiByte SB1 SoCs
 *
 * Copyright (C) 2007 OnStor, Inc. * Andrew Sharp <andy.sharp@lsi.com>
 *
 * This driver is intended to make the second of two hardware watchdogs
 * on the Sibyte 12XX and 11XX SoCs available to the user.  There are two
 * such devices available on the SoC, but it seems that there isn't an
 * enumeration class for watchdogs in Linux like there is for RTCs.
 * The second is used rather than the first because it uses IRQ 1,
 * thereby avoiding all that IRQ 0 problematic nonsense.
 *
 * I have not tried this driver on a 1480 processor; it might work
 * just well enough to really screw things up.
 *
 * It is a simple timer, and there is an interrupt that is raised the
 * first time the timer expires.  The second time it expires, the chip
 * is reset and there is no way to redirect that NMI.  Which could
 * be problematic in some cases where this chip is sitting on the HT
 * bus and has just taken responsibility for providing a cache block.
 * Since the reset can't be redirected to the external reset pin, it is
 * possible that other HT connected processors might hang and not reset.
 * For Linux, a soft reset would probably be even worse than a hard reset.
 * There you have it.
 *
 * The timer takes 23 bits of a 64 bit register (?) as a count value,
 * and decrements the count every microsecond, for a max value of
 * 0x7fffff usec or about 8.3ish seconds.
 *
 * This watchdog borrows some user semantics from the softdog driver,
 * in that if you close the fd, it leaves the watchdog running, unless
 * you previously wrote a 'V' to the fd, in which case it disables
 * the watchdog when you close the fd like some other drivers.
 *
 * Based on various other watchdog drivers, which are probably all
 * loosely based on something Alan Cox wrote years ago.
 *
 *	(c) Copyright 1996 Alan Cox <alan@lxorguk.ukuu.org.uk>,
 *						All Rights Reserved.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	version 1 or 2 as published by the Free Software Foundation.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/reboot.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/interrupt.h>

#include <asm/sibyte/sb1250.h>
#include <asm/sibyte/sb1250_regs.h>
#include <asm/sibyte/sb1250_int.h>
#include <asm/sibyte/sb1250_scd.h>

static DEFINE_SPINLOCK(sbwd_lock);

/*
 * set the initial count value of a timer
 *
 * wdog is the iomem address of the cfg register
 */
void sbwdog_set(char __iomem *wdog, unsigned long t)
{
	spin_lock(&sbwd_lock);
	__raw_writeb(0, wdog);
	__raw_writeq(t & 0x7fffffUL, wdog - 0x10);
	spin_unlock(&sbwd_lock);
}

/*
 * cause the timer to [re]load it's initial count and start counting
 * all over again
 *
 * wdog is the iomem address of the cfg register
 */
void sbwdog_pet(char __iomem *wdog)
{
	spin_lock(&sbwd_lock);
	__raw_writeb(__raw_readb(wdog) | 1, wdog);
	spin_unlock(&sbwd_lock);
}

static unsigned long sbwdog_gate; /* keeps it to one thread only */
static char __iomem *kern_dog = (char __iomem *)(IO_BASE + (A_SCD_WDOG_CFG_0));
static char __iomem *user_dog = (char __iomem *)(IO_BASE + (A_SCD_WDOG_CFG_1));
static unsigned long timeout = 0x7fffffUL;	/* useconds: 8.3ish secs. */
static int expect_close;

static const struct watchdog_info ident = {
	.options	= WDIOF_CARDRESET | WDIOF_SETTIMEOUT |
					WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE,
	.identity	= "SiByte Watchdog",
};

/*
 * Allow only a single thread to walk the dog
 */
static int sbwdog_open(struct inode *inode, struct file *file)
{
	nonseekable_open(inode, file);
	if (test_and_set_bit(0, &sbwdog_gate))
		return -EBUSY;
	__module_get(THIS_MODULE);

	/*
	 * Activate the timer
	 */
	sbwdog_set(user_dog, timeout);
	__raw_writeb(1, user_dog);

	return 0;
}

/*
 * Put the dog back in the kennel.
 */
static int sbwdog_release(struct inode *inode, struct file *file)
{
	if (expect_close == 42) {
		__raw_writeb(0, user_dog);
		module_put(THIS_MODULE);
	} else {
		pr_crit("%s: Unexpected close, not stopping watchdog!\n",
			ident.identity);
		sbwdog_pet(user_dog);
	}
	clear_bit(0, &sbwdog_gate);
	expect_close = 0;

	return 0;
}

/*
 * 42 - the answer
 */
static ssize_t sbwdog_write(struct file *file, const char __user *data,
			size_t len, loff_t *ppos)
{
	int i;

	if (len) {
		/*
		 * restart the timer
		 */
		expect_close = 0;

		for (i = 0; i != len; i++) {
			char c;

			if (get_user(c, data + i))
				return -EFAULT;
			if (c == 'V')
				expect_close = 42;
		}
		sbwdog_pet(user_dog);
	}

	return len;
}

static long sbwdog_ioctl(struct file *file, unsigned int cmd,
						unsigned long arg)
{
	int ret = -ENOTTY;
	unsigned long time;
	void __user *argp = (void __user *)arg;
	int __user *p = argp;

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		ret = copy_to_user(argp, &ident, sizeof(ident)) ? -EFAULT : 0;
		break;

	case WDIOC_GETSTATUS:
	case WDIOC_GETBOOTSTATUS:
		ret = put_user(0, p);
		break;

	case WDIOC_KEEPALIVE:
		sbwdog_pet(user_dog);
		ret = 0;
		break;

	case WDIOC_SETTIMEOUT:
		ret = get_user(time, p);
		if (ret)
			break;

		time *= 1000000;
		if (time > 0x7fffffUL) {
			ret = -EINVAL;
			break;
		}
		timeout = time;
		sbwdog_set(user_dog, timeout);
		sbwdog_pet(user_dog);

	case WDIOC_GETTIMEOUT:
		/*
		 * get the remaining count from the ... count register
		 * which is 1*8 before the config register
		 */
		ret = put_user(__raw_readq(user_dog - 8) / 1000000, p);
		break;
	}
	return ret;
}

/*
 *	Notifier for system down
 */
static int sbwdog_notify_sys(struct notifier_block *this, unsigned long code,
								void *erf)
{
	if (code == SYS_DOWN || code == SYS_HALT) {
		/*
		 * sit and sit
		 */
		__raw_writeb(0, user_dog);
		__raw_writeb(0, kern_dog);
	}

	return NOTIFY_DONE;
}

static const struct file_operations sbwdog_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.write		= sbwdog_write,
	.unlocked_ioctl	= sbwdog_ioctl,
	.open		= sbwdog_open,
	.release	= sbwdog_release,
};

static struct miscdevice sbwdog_miscdev = {
	.minor		= WATCHDOG_MINOR,
	.name		= "watchdog",
	.fops		= &sbwdog_fops,
};

static struct notifier_block sbwdog_notifier = {
	.notifier_call	= sbwdog_notify_sys,
};

/*
 * interrupt handler
 *
 * doesn't do a whole lot for user, but oh so cleverly written so kernel
 * code can use it to re-up the watchdog, thereby saving the kernel from
 * having to create and maintain a timer, just to tickle another timer,
 * which is just so wrong.
 */
irqreturn_t sbwdog_interrupt(int irq, void *addr)
{
	unsigned long wd_init;
	char *wd_cfg_reg = (char *)addr;
	u8 cfg;

	cfg = __raw_readb(wd_cfg_reg);
	wd_init = __raw_readq(wd_cfg_reg - 8) & 0x7fffff;

	/*
	 * if it's the second watchdog timer, it's for those users
	 */
	if (wd_cfg_reg == user_dog)
		pr_crit("%s in danger of initiating system reset "
			"in %ld.%01ld seconds\n",
			ident.identity,
			wd_init / 1000000, (wd_init / 100000) % 10);
	else
		cfg |= 1;

	__raw_writeb(cfg, wd_cfg_reg);

	return IRQ_HANDLED;
}

static int __init sbwdog_init(void)
{
	int ret;

	/*
	 * register a reboot notifier
	 */
	ret = register_reboot_notifier(&sbwdog_notifier);
	if (ret) {
		pr_err("%s: cannot register reboot notifier (err=%d)\n",
		       ident.identity, ret);
		return ret;
	}

	/*
	 * get the resources
	 */

	ret = request_irq(1, sbwdog_interrupt, IRQF_SHARED,
		ident.identity, (void *)user_dog);
	if (ret) {
		pr_err("%s: failed to request irq 1 - %d\n",
		       ident.identity, ret);
		goto out;
	}

	ret = misc_register(&sbwdog_miscdev);
	if (ret == 0) {
		pr_info("%s: timeout is %ld.%ld secs\n",
			ident.identity,
			timeout / 1000000, (timeout / 100000) % 10);
		return 0;
	}
	free_irq(1, (void *)user_dog);
out:
	unregister_reboot_notifier(&sbwdog_notifier);

	return ret;
}

static void __exit sbwdog_exit(void)
{
	misc_deregister(&sbwdog_miscdev);
	free_irq(1, (void *)user_dog);
	unregister_reboot_notifier(&sbwdog_notifier);
}

module_init(sbwdog_init);
module_exit(sbwdog_exit);

MODULE_AUTHOR("Andrew Sharp <andy.sharp@lsi.com>");
MODULE_DESCRIPTION("SiByte Watchdog");

module_param(timeout, ulong, 0);
MODULE_PARM_DESC(timeout,
      "Watchdog timeout in microseconds (max/default 8388607 or 8.3ish secs)");

MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);

/*
 * example code that can be put in a platform code area to utilize the
 * first watchdog timer for the kernels own purpose.

void platform_wd_setup(void)
{
	int ret;

	ret = request_irq(1, sbwdog_interrupt, IRQF_SHARED,
		"Kernel Watchdog", IOADDR(A_SCD_WDOG_CFG_0));
	if (ret) {
		pr_crit("Watchdog IRQ zero(0) failed to be requested - %d\n", ret);
	}
}


 */
