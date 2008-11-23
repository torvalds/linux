/*
 * Watchdog timer for PowerPC Book-E systems
 *
 * Author: Matthew McClintock
 * Maintainer: Kumar Gala <galak@kernel.crashing.org>
 *
 * Copyright 2005, 2008 Freescale Semiconductor Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/smp.h>
#include <linux/miscdevice.h>
#include <linux/notifier.h>
#include <linux/watchdog.h>
#include <linux/uaccess.h>

#include <asm/reg_booke.h>
#include <asm/system.h>

/* If the kernel parameter wdt=1, the watchdog will be enabled at boot.
 * Also, the wdt_period sets the watchdog timer period timeout.
 * For E500 cpus the wdt_period sets which bit changing from 0->1 will
 * trigger a watchog timeout. This watchdog timeout will occur 3 times, the
 * first time nothing will happen, the second time a watchdog exception will
 * occur, and the final time the board will reset.
 */

#ifdef	CONFIG_FSL_BOOKE
#define WDT_PERIOD_DEFAULT 63	/* Ex. wdt_period=28 bus=333Mhz,reset=~40sec */
#else
#define WDT_PERIOD_DEFAULT 3	/* Refer to the PPC40x and PPC4xx manuals */
#endif				/* for timing information */

u32 booke_wdt_enabled;
u32 booke_wdt_period = WDT_PERIOD_DEFAULT;

#ifdef	CONFIG_FSL_BOOKE
#define WDTP(x)		((((63-x)&0x3)<<30)|(((63-x)&0x3c)<<15))
#define WDTP_MASK	(WDTP(0))
#else
#define WDTP(x)		(TCR_WP(x))
#define WDTP_MASK	(TCR_WP_MASK)
#endif

static DEFINE_SPINLOCK(booke_wdt_lock);

static void __booke_wdt_ping(void *data)
{
	mtspr(SPRN_TSR, TSR_ENW|TSR_WIS);
}

static void booke_wdt_ping(void)
{
	on_each_cpu(__booke_wdt_ping, NULL, 0);
}

static void __booke_wdt_enable(void *data)
{
	u32 val;

	/* clear status before enabling watchdog */
	__booke_wdt_ping(NULL);
	val = mfspr(SPRN_TCR);
	val &= ~WDTP_MASK;
	val |= (TCR_WIE|TCR_WRC(WRC_CHIP)|WDTP(booke_wdt_period));

	mtspr(SPRN_TCR, val);
}

static ssize_t booke_wdt_write(struct file *file, const char __user *buf,
				size_t count, loff_t *ppos)
{
	booke_wdt_ping();
	return count;
}

static struct watchdog_info ident = {
	.options = WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING,
	.identity = "PowerPC Book-E Watchdog",
};

static long booke_wdt_ioctl(struct file *file,
				unsigned int cmd, unsigned long arg)
{
	u32 tmp = 0;
	u32 __user *p = (u32 __user *)arg;

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		if (copy_to_user(arg, &ident, sizeof(struct watchdog_info)))
			return -EFAULT;
	case WDIOC_GETSTATUS:
		return put_user(ident.options, p);
	case WDIOC_GETBOOTSTATUS:
		/* XXX: something is clearing TSR */
		tmp = mfspr(SPRN_TSR) & TSR_WRS(3);
		/* returns 1 if last reset was caused by the WDT */
		return (tmp ? 1 : 0);
	case WDIOC_SETOPTIONS:
		if (get_user(tmp, p))
			return -EINVAL;
		if (tmp == WDIOS_ENABLECARD) {
			booke_wdt_ping();
			break;
		} else
			return -EINVAL;
		return 0;
	case WDIOC_KEEPALIVE:
		booke_wdt_ping();
		return 0;
	case WDIOC_SETTIMEOUT:
		if (get_user(booke_wdt_period, p))
			return -EFAULT;
		mtspr(SPRN_TCR, (mfspr(SPRN_TCR) & ~WDTP_MASK) |
						WDTP(booke_wdt_period));
		return 0;
	case WDIOC_GETTIMEOUT:
		return put_user(booke_wdt_period, p);
	default:
		return -ENOTTY;
	}

	return 0;
}

static int booke_wdt_open(struct inode *inode, struct file *file)
{
	spin_lock(&booke_wdt_lock);
	if (booke_wdt_enabled == 0) {
		booke_wdt_enabled = 1;
		on_each_cpu(__booke_wdt_enable, NULL, 0);
		printk(KERN_INFO
		      "PowerPC Book-E Watchdog Timer Enabled (wdt_period=%d)\n",
				booke_wdt_period);
	}
	spin_unlock(&booke_wdt_lock);

	return nonseekable_open(inode, file);
}

static const struct file_operations booke_wdt_fops = {
	.owner = THIS_MODULE,
	.llseek = no_llseek,
	.write = booke_wdt_write,
	.unlocked_ioctl = booke_wdt_ioctl,
	.open = booke_wdt_open,
};

static struct miscdevice booke_wdt_miscdev = {
	.minor = WATCHDOG_MINOR,
	.name = "watchdog",
	.fops = &booke_wdt_fops,
};

static void __exit booke_wdt_exit(void)
{
	misc_deregister(&booke_wdt_miscdev);
}

static int __init booke_wdt_init(void)
{
	int ret = 0;

	printk(KERN_INFO "PowerPC Book-E Watchdog Timer Loaded\n");
	ident.firmware_version = cur_cpu_spec->pvr_value;

	ret = misc_register(&booke_wdt_miscdev);
	if (ret) {
		printk(KERN_CRIT "Cannot register miscdev on minor=%d: %d\n",
				WATCHDOG_MINOR, ret);
		return ret;
	}

	spin_lock(&booke_wdt_lock);
	if (booke_wdt_enabled == 1) {
		printk(KERN_INFO
		      "PowerPC Book-E Watchdog Timer Enabled (wdt_period=%d)\n",
				booke_wdt_period);
		on_each_cpu(__booke_wdt_enable, NULL, 0);
	}
	spin_unlock(&booke_wdt_lock);

	return ret;
}
device_initcall(booke_wdt_init);
