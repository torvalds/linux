/*
 *	SBC8360 Watchdog driver
 *
 *	(c) Copyright 2005 Webcon, Inc.
 *
 *	Based on ib700wdt.c, which is based on advantechwdt.c which is based
 *	on acquirewdt.c which is based on wdt.c.
 *
 *	(c) Copyright 2001 Charles Howes <chowes@vsol.net>
 *
 *	Based on advantechwdt.c which is based on acquirewdt.c which
 *	is based on wdt.c.
 *
 *	(c) Copyright 2000-2001 Marek Michalkiewicz <marekm@linux.org.pl>
 *
 *	Based on acquirewdt.c which is based on wdt.c.
 *	Original copyright messages:
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
 *
 *	14-Dec-2001 Matt Domsch <Matt_Domsch@dell.com>
 *	     Added nowayout module option to override CONFIG_WATCHDOG_NOWAYOUT
 *	     Added timeout module option to override default
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/types.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/notifier.h>
#include <linux/fs.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/moduleparam.h>
#include <linux/io.h>
#include <linux/uaccess.h>


static unsigned long sbc8360_is_open;
static char expect_close;

/*
 *
 * Watchdog Timer Configuration
 *
 * The function of the watchdog timer is to reset the system automatically
 * and is defined at I/O port 0120H and 0121H.  To enable the watchdog timer
 * and allow the system to reset, write appropriate values from the table
 * below to I/O port 0120H and 0121H.  To disable the timer, write a zero
 * value to I/O port 0121H for the system to stop the watchdog function.
 *
 * The following describes how the timer should be programmed (according to
 * the vendor documentation)
 *
 * Enabling Watchdog:
 * MOV AX,000AH (enable, phase I)
 * MOV DX,0120H
 * OUT DX,AX
 * MOV AX,000BH (enable, phase II)
 * MOV DX,0120H
 * OUT DX,AX
 * MOV AX,000nH (set multiplier n, from 1-4)
 * MOV DX,0120H
 * OUT DX,AX
 * MOV AX,000mH (set base timer m, from 0-F)
 * MOV DX,0121H
 * OUT DX,AX
 *
 * Reset timer:
 * MOV AX,000mH (same as set base timer, above)
 * MOV DX,0121H
 * OUT DX,AX
 *
 * Disabling Watchdog:
 * MOV AX,0000H (a zero value)
 * MOV DX,0120H
 * OUT DX,AX
 *
 * Watchdog timeout configuration values:
 *		N
 *	M |	1	2	3	4
 *	--|----------------------------------
 *	0 |	0.5s	5s	50s	100s
 *	1 |	1s	10s	100s	200s
 *	2 |	1.5s	15s	150s	300s
 *	3 |	2s	20s	200s	400s
 *	4 |	2.5s	25s	250s	500s
 *	5 |	3s	30s	300s	600s
 *	6 |	3.5s	35s	350s	700s
 *	7 |	4s	40s	400s	800s
 *	8 |	4.5s	45s	450s	900s
 *	9 |	5s	50s	500s	1000s
 *	A |	5.5s	55s	550s	1100s
 *	B |	6s	60s	600s	1200s
 *	C |	6.5s	65s	650s	1300s
 *	D |	7s	70s	700s	1400s
 *	E |	7.5s	75s	750s	1500s
 *	F |	8s	80s	800s	1600s
 *
 * Another way to say the same things is:
 *  For N=1, Timeout = (M+1) * 0.5s
 *  For N=2, Timeout = (M+1) * 5s
 *  For N=3, Timeout = (M+1) * 50s
 *  For N=4, Timeout = (M+1) * 100s
 *
 */

static int wd_times[64][2] = {
	{0, 1},			/* 0  = 0.5s */
	{1, 1},			/* 1  = 1s   */
	{2, 1},			/* 2  = 1.5s */
	{3, 1},			/* 3  = 2s   */
	{4, 1},			/* 4  = 2.5s */
	{5, 1},			/* 5  = 3s   */
	{6, 1},			/* 6  = 3.5s */
	{7, 1},			/* 7  = 4s   */
	{8, 1},			/* 8  = 4.5s */
	{9, 1},			/* 9  = 5s   */
	{0xA, 1},		/* 10 = 5.5s */
	{0xB, 1},		/* 11 = 6s   */
	{0xC, 1},		/* 12 = 6.5s */
	{0xD, 1},		/* 13 = 7s   */
	{0xE, 1},		/* 14 = 7.5s */
	{0xF, 1},		/* 15 = 8s   */
	{0, 2},			/* 16 = 5s  */
	{1, 2},			/* 17 = 10s */
	{2, 2},			/* 18 = 15s */
	{3, 2},			/* 19 = 20s */
	{4, 2},			/* 20 = 25s */
	{5, 2},			/* 21 = 30s */
	{6, 2},			/* 22 = 35s */
	{7, 2},			/* 23 = 40s */
	{8, 2},			/* 24 = 45s */
	{9, 2},			/* 25 = 50s */
	{0xA, 2},		/* 26 = 55s */
	{0xB, 2},		/* 27 = 60s */
	{0xC, 2},		/* 28 = 65s */
	{0xD, 2},		/* 29 = 70s */
	{0xE, 2},		/* 30 = 75s */
	{0xF, 2},		/* 31 = 80s */
	{0, 3},			/* 32 = 50s  */
	{1, 3},			/* 33 = 100s */
	{2, 3},			/* 34 = 150s */
	{3, 3},			/* 35 = 200s */
	{4, 3},			/* 36 = 250s */
	{5, 3},			/* 37 = 300s */
	{6, 3},			/* 38 = 350s */
	{7, 3},			/* 39 = 400s */
	{8, 3},			/* 40 = 450s */
	{9, 3},			/* 41 = 500s */
	{0xA, 3},		/* 42 = 550s */
	{0xB, 3},		/* 43 = 600s */
	{0xC, 3},		/* 44 = 650s */
	{0xD, 3},		/* 45 = 700s */
	{0xE, 3},		/* 46 = 750s */
	{0xF, 3},		/* 47 = 800s */
	{0, 4},			/* 48 = 100s */
	{1, 4},			/* 49 = 200s */
	{2, 4},			/* 50 = 300s */
	{3, 4},			/* 51 = 400s */
	{4, 4},			/* 52 = 500s */
	{5, 4},			/* 53 = 600s */
	{6, 4},			/* 54 = 700s */
	{7, 4},			/* 55 = 800s */
	{8, 4},			/* 56 = 900s */
	{9, 4},			/* 57 = 1000s */
	{0xA, 4},		/* 58 = 1100s */
	{0xB, 4},		/* 59 = 1200s */
	{0xC, 4},		/* 60 = 1300s */
	{0xD, 4},		/* 61 = 1400s */
	{0xE, 4},		/* 62 = 1500s */
	{0xF, 4}		/* 63 = 1600s */
};

#define SBC8360_ENABLE 0x120
#define SBC8360_BASETIME 0x121

static int timeout = 27;
static int wd_margin = 0xB;
static int wd_multiplier = 2;
static bool nowayout = WATCHDOG_NOWAYOUT;

module_param(timeout, int, 0);
MODULE_PARM_DESC(timeout, "Index into timeout table (0-63) (default=27 (60s))");
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout,
		 "Watchdog cannot be stopped once started (default="
				__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

/*
 *	Kernel methods.
 */

/* Activate and pre-configure watchdog */
static void sbc8360_activate(void)
{
	/* Enable the watchdog */
	outb(0x0A, SBC8360_ENABLE);
	msleep_interruptible(100);
	outb(0x0B, SBC8360_ENABLE);
	msleep_interruptible(100);
	/* Set timeout multiplier */
	outb(wd_multiplier, SBC8360_ENABLE);
	msleep_interruptible(100);
	/* Nothing happens until first sbc8360_ping() */
}

/* Kernel pings watchdog */
static void sbc8360_ping(void)
{
	/* Write the base timer register */
	outb(wd_margin, SBC8360_BASETIME);
}

/* stop watchdog */
static void sbc8360_stop(void)
{
	/* De-activate the watchdog */
	outb(0, SBC8360_ENABLE);
}

/* Userspace pings kernel driver, or requests clean close */
static ssize_t sbc8360_write(struct file *file, const char __user *buf,
			     size_t count, loff_t *ppos)
{
	if (count) {
		if (!nowayout) {
			size_t i;

			/* In case it was set long ago */
			expect_close = 0;

			for (i = 0; i != count; i++) {
				char c;
				if (get_user(c, buf + i))
					return -EFAULT;
				if (c == 'V')
					expect_close = 42;
			}
		}
		sbc8360_ping();
	}
	return count;
}

static int sbc8360_open(struct inode *inode, struct file *file)
{
	if (test_and_set_bit(0, &sbc8360_is_open))
		return -EBUSY;
	if (nowayout)
		__module_get(THIS_MODULE);

	/* Activate and ping once to start the countdown */
	sbc8360_activate();
	sbc8360_ping();
	return nonseekable_open(inode, file);
}

static int sbc8360_close(struct inode *inode, struct file *file)
{
	if (expect_close == 42)
		sbc8360_stop();
	else
		pr_crit("SBC8360 device closed unexpectedly.  SBC8360 will not stop!\n");

	clear_bit(0, &sbc8360_is_open);
	expect_close = 0;
	return 0;
}

/*
 *	Notifier for system down
 */

static int sbc8360_notify_sys(struct notifier_block *this, unsigned long code,
			      void *unused)
{
	if (code == SYS_DOWN || code == SYS_HALT)
		sbc8360_stop();	/* Disable the SBC8360 Watchdog */

	return NOTIFY_DONE;
}

/*
 *	Kernel Interfaces
 */

static const struct file_operations sbc8360_fops = {
	.owner = THIS_MODULE,
	.llseek = no_llseek,
	.write = sbc8360_write,
	.open = sbc8360_open,
	.release = sbc8360_close,
};

static struct miscdevice sbc8360_miscdev = {
	.minor = WATCHDOG_MINOR,
	.name = "watchdog",
	.fops = &sbc8360_fops,
};

/*
 *	The SBC8360 needs to learn about soft shutdowns in order to
 *	turn the timebomb registers off.
 */

static struct notifier_block sbc8360_notifier = {
	.notifier_call = sbc8360_notify_sys,
};

static int __init sbc8360_init(void)
{
	int res;
	unsigned long int mseconds = 60000;

	if (timeout < 0 || timeout > 63) {
		pr_err("Invalid timeout index (must be 0-63)\n");
		res = -EINVAL;
		goto out;
	}

	if (!request_region(SBC8360_ENABLE, 1, "SBC8360")) {
		pr_err("ENABLE method I/O %X is not available\n",
		       SBC8360_ENABLE);
		res = -EIO;
		goto out;
	}
	if (!request_region(SBC8360_BASETIME, 1, "SBC8360")) {
		pr_err("BASETIME method I/O %X is not available\n",
		       SBC8360_BASETIME);
		res = -EIO;
		goto out_nobasetimereg;
	}

	res = register_reboot_notifier(&sbc8360_notifier);
	if (res) {
		pr_err("Failed to register reboot notifier\n");
		goto out_noreboot;
	}

	res = misc_register(&sbc8360_miscdev);
	if (res) {
		pr_err("failed to register misc device\n");
		goto out_nomisc;
	}

	wd_margin = wd_times[timeout][0];
	wd_multiplier = wd_times[timeout][1];

	if (wd_multiplier == 1)
		mseconds = (wd_margin + 1) * 500;
	else if (wd_multiplier == 2)
		mseconds = (wd_margin + 1) * 5000;
	else if (wd_multiplier == 3)
		mseconds = (wd_margin + 1) * 50000;
	else if (wd_multiplier == 4)
		mseconds = (wd_margin + 1) * 100000;

	/* My kingdom for the ability to print "0.5 seconds" in the kernel! */
	pr_info("Timeout set at %ld ms\n", mseconds);

	return 0;

out_nomisc:
	unregister_reboot_notifier(&sbc8360_notifier);
out_noreboot:
	release_region(SBC8360_BASETIME, 1);
out_nobasetimereg:
	release_region(SBC8360_ENABLE, 1);
out:
	return res;
}

static void __exit sbc8360_exit(void)
{
	misc_deregister(&sbc8360_miscdev);
	unregister_reboot_notifier(&sbc8360_notifier);
	release_region(SBC8360_ENABLE, 1);
	release_region(SBC8360_BASETIME, 1);
}

module_init(sbc8360_init);
module_exit(sbc8360_exit);

MODULE_AUTHOR("Ian E. Morgan <imorgan@webcon.ca>");
MODULE_DESCRIPTION("SBC8360 watchdog driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.01");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);

/* end of sbc8360.c */
