/*
 * Watchdog timer for PowerPC Book-E systems
 *
 * Author: Matthew McClintock
 * Maintainer: Kumar Gala <galak@kernel.crashing.org>
 *
 * Copyright 2005, 2008, 2010-2011 Freescale Semiconductor Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/smp.h>
#include <linux/miscdevice.h>
#include <linux/notifier.h>
#include <linux/watchdog.h>
#include <linux/uaccess.h>

#include <asm/reg_booke.h>
#include <asm/time.h>
#include <asm/div64.h>

/* If the kernel parameter wdt=1, the watchdog will be enabled at boot.
 * Also, the wdt_period sets the watchdog timer period timeout.
 * For E500 cpus the wdt_period sets which bit changing from 0->1 will
 * trigger a watchog timeout. This watchdog timeout will occur 3 times, the
 * first time nothing will happen, the second time a watchdog exception will
 * occur, and the final time the board will reset.
 */

u32 booke_wdt_enabled;
u32 booke_wdt_period = CONFIG_BOOKE_WDT_DEFAULT_TIMEOUT;

#ifdef	CONFIG_FSL_BOOKE
#define WDTP(x)		((((x)&0x3)<<30)|(((x)&0x3c)<<15))
#define WDTP_MASK	(WDTP(0x3f))
#else
#define WDTP(x)		(TCR_WP(x))
#define WDTP_MASK	(TCR_WP_MASK)
#endif

static DEFINE_SPINLOCK(booke_wdt_lock);

/* For the specified period, determine the number of seconds
 * corresponding to the reset time.  There will be a watchdog
 * exception at approximately 3/5 of this time.
 *
 * The formula to calculate this is given by:
 * 2.5 * (2^(63-period+1)) / timebase_freq
 *
 * In order to simplify things, we assume that period is
 * at least 1.  This will still result in a very long timeout.
 */
static unsigned long long period_to_sec(unsigned int period)
{
	unsigned long long tmp = 1ULL << (64 - period);
	unsigned long tmp2 = ppc_tb_freq;

	/* tmp may be a very large number and we don't want to overflow,
	 * so divide the timebase freq instead of multiplying tmp
	 */
	tmp2 = tmp2 / 5 * 2;

	do_div(tmp, tmp2);
	return tmp;
}

/*
 * This procedure will find the highest period which will give a timeout
 * greater than the one required. e.g. for a bus speed of 66666666 and
 * and a parameter of 2 secs, then this procedure will return a value of 38.
 */
static unsigned int sec_to_period(unsigned int secs)
{
	unsigned int period;
	for (period = 63; period > 0; period--) {
		if (period_to_sec(period) >= secs)
			return period;
	}
	return 0;
}

static void __booke_wdt_set(void *data)
{
	u32 val;

	val = mfspr(SPRN_TCR);
	val &= ~WDTP_MASK;
	val |= WDTP(booke_wdt_period);

	mtspr(SPRN_TCR, val);
}

static void booke_wdt_set(void)
{
	on_each_cpu(__booke_wdt_set, NULL, 0);
}

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

/**
 * booke_wdt_disable - disable the watchdog on the given CPU
 *
 * This function is called on each CPU.  It disables the watchdog on that CPU.
 *
 * TCR[WRC] cannot be changed once it has been set to non-zero, but we can
 * effectively disable the watchdog by setting its period to the maximum value.
 */
static void __booke_wdt_disable(void *data)
{
	u32 val;

	val = mfspr(SPRN_TCR);
	val &= ~(TCR_WIE | WDTP_MASK);
	mtspr(SPRN_TCR, val);

	/* clear status to make sure nothing is pending */
	__booke_wdt_ping(NULL);

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
		if (copy_to_user((void *)arg, &ident, sizeof(ident)))
			return -EFAULT;
	case WDIOC_GETSTATUS:
		return put_user(0, p);
	case WDIOC_GETBOOTSTATUS:
		/* XXX: something is clearing TSR */
		tmp = mfspr(SPRN_TSR) & TSR_WRS(3);
		/* returns CARDRESET if last reset was caused by the WDT */
		return (tmp ? WDIOF_CARDRESET : 0);
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
		if (get_user(tmp, p))
			return -EFAULT;
#ifdef	CONFIG_FSL_BOOKE
		/* period of 1 gives the largest possible timeout */
		if (tmp > period_to_sec(1))
			return -EINVAL;
		booke_wdt_period = sec_to_period(tmp);
#else
		booke_wdt_period = tmp;
#endif
		booke_wdt_set();
		/* Fall */
	case WDIOC_GETTIMEOUT:
#ifdef	CONFIG_FSL_BOOKE
		return put_user(period_to_sec(booke_wdt_period), p);
#else
		return put_user(booke_wdt_period, p);
#endif
	default:
		return -ENOTTY;
	}

	return 0;
}

/* wdt_is_active stores wether or not the /dev/watchdog device is opened */
static unsigned long wdt_is_active;

static int booke_wdt_open(struct inode *inode, struct file *file)
{
	/* /dev/watchdog can only be opened once */
	if (test_and_set_bit(0, &wdt_is_active))
		return -EBUSY;

	spin_lock(&booke_wdt_lock);
	if (booke_wdt_enabled == 0) {
		booke_wdt_enabled = 1;
		on_each_cpu(__booke_wdt_enable, NULL, 0);
		pr_debug("watchdog enabled (timeout = %llu sec)\n",
			 period_to_sec(booke_wdt_period));
	}
	spin_unlock(&booke_wdt_lock);

	return nonseekable_open(inode, file);
}

static int booke_wdt_release(struct inode *inode, struct file *file)
{
#ifndef CONFIG_WATCHDOG_NOWAYOUT
	/* Normally, the watchdog is disabled when /dev/watchdog is closed, but
	 * if CONFIG_WATCHDOG_NOWAYOUT is defined, then it means that the
	 * watchdog should remain enabled.  So we disable it only if
	 * CONFIG_WATCHDOG_NOWAYOUT is not defined.
	 */
	on_each_cpu(__booke_wdt_disable, NULL, 0);
	booke_wdt_enabled = 0;
	pr_debug("watchdog disabled\n");
#endif

	clear_bit(0, &wdt_is_active);

	return 0;
}

static const struct file_operations booke_wdt_fops = {
	.owner = THIS_MODULE,
	.llseek = no_llseek,
	.write = booke_wdt_write,
	.unlocked_ioctl = booke_wdt_ioctl,
	.open = booke_wdt_open,
	.release = booke_wdt_release,
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

	pr_info("powerpc book-e watchdog driver loaded\n");
	ident.firmware_version = cur_cpu_spec->pvr_value;

	ret = misc_register(&booke_wdt_miscdev);
	if (ret) {
		pr_err("cannot register device (minor=%u, ret=%i)\n",
		       WATCHDOG_MINOR, ret);
		return ret;
	}

	spin_lock(&booke_wdt_lock);
	if (booke_wdt_enabled == 1) {
		pr_info("watchdog enabled (timeout = %llu sec)\n",
			period_to_sec(booke_wdt_period));
		on_each_cpu(__booke_wdt_enable, NULL, 0);
	}
	spin_unlock(&booke_wdt_lock);

	return ret;
}

module_init(booke_wdt_init);
module_exit(booke_wdt_exit);

MODULE_DESCRIPTION("PowerPC Book-E watchdog driver");
MODULE_LICENSE("GPL");
