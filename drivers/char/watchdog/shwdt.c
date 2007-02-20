/*
 * drivers/char/watchdog/shwdt.c
 *
 * Watchdog driver for integrated watchdog in the SuperH processors.
 *
 * Copyright (C) 2001, 2002, 2003 Paul Mundt <lethal@linux-sh.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * 14-Dec-2001 Matt Domsch <Matt_Domsch@dell.com>
 *     Added nowayout module option to override CONFIG_WATCHDOG_NOWAYOUT
 *
 * 19-Apr-2002 Rob Radez <rob@osinvestor.com>
 *     Added expect close support, made emulated timeout runtime changeable
 *     general cleanups, add some ioctls
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/reboot.h>
#include <linux/notifier.h>
#include <linux/ioport.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/watchdog.h>

#define PFX "shwdt: "

/*
 * Default clock division ratio is 5.25 msecs. For an additional table of
 * values, consult the asm-sh/watchdog.h. Overload this at module load
 * time.
 *
 * In order for this to work reliably we need to have HZ set to 1000 or
 * something quite higher than 100 (or we need a proper high-res timer
 * implementation that will deal with this properly), otherwise the 10ms
 * resolution of a jiffy is enough to trigger the overflow. For things like
 * the SH-4 and SH-5, this isn't necessarily that big of a problem, though
 * for the SH-2 and SH-3, this isn't recommended unless the WDT is absolutely
 * necssary.
 *
 * As a result of this timing problem, the only modes that are particularly
 * feasible are the 4096 and the 2048 divisors, which yeild 5.25 and 2.62ms
 * overflow periods respectively.
 *
 * Also, since we can't really expect userspace to be responsive enough
 * before the overflow happens, we maintain two seperate timers .. One in
 * the kernel for clearing out WOVF every 2ms or so (again, this depends on
 * HZ == 1000), and another for monitoring userspace writes to the WDT device.
 *
 * As such, we currently use a configurable heartbeat interval which defaults
 * to 30s. In this case, the userspace daemon is only responsible for periodic
 * writes to the device before the next heartbeat is scheduled. If the daemon
 * misses its deadline, the kernel timer will allow the WDT to overflow.
 */
static int clock_division_ratio = WTCSR_CKS_4096;

#define next_ping_period(cks)	msecs_to_jiffies(cks - 4)

static void sh_wdt_ping(unsigned long data);

static unsigned long shwdt_is_open;
static struct watchdog_info sh_wdt_info;
static char shwdt_expect_close;
static DEFINE_TIMER(timer, sh_wdt_ping, 0, 0);
static unsigned long next_heartbeat;

#define WATCHDOG_HEARTBEAT 30			/* 30 sec default heartbeat */
static int heartbeat = WATCHDOG_HEARTBEAT;	/* in seconds */

static int nowayout = WATCHDOG_NOWAYOUT;

/**
 * 	sh_wdt_start - Start the Watchdog
 *
 * 	Starts the watchdog.
 */
static void sh_wdt_start(void)
{
	__u8 csr;

	next_heartbeat = jiffies + (heartbeat * HZ);
	mod_timer(&timer, next_ping_period(clock_division_ratio));

	csr = sh_wdt_read_csr();
	csr |= WTCSR_WT | clock_division_ratio;
	sh_wdt_write_csr(csr);

	sh_wdt_write_cnt(0);

	/*
	 * These processors have a bit of an inconsistent initialization
	 * process.. starting with SH-3, RSTS was moved to WTCSR, and the
	 * RSTCSR register was removed.
	 *
	 * On the SH-2 however, in addition with bits being in different
	 * locations, we must deal with RSTCSR outright..
	 */
	csr = sh_wdt_read_csr();
	csr |= WTCSR_TME;
	csr &= ~WTCSR_RSTS;
	sh_wdt_write_csr(csr);

#ifdef CONFIG_CPU_SH2
	/*
	 * Whoever came up with the RSTCSR semantics must've been smoking
	 * some of the good stuff, since in addition to the WTCSR/WTCNT write
	 * brain-damage, it's managed to fuck things up one step further..
	 *
	 * If we need to clear the WOVF bit, the upper byte has to be 0xa5..
	 * but if we want to touch RSTE or RSTS, the upper byte has to be
	 * 0x5a..
	 */
	csr = sh_wdt_read_rstcsr();
	csr &= ~RSTCSR_RSTS;
	sh_wdt_write_rstcsr(csr);
#endif
}

/**
 * 	sh_wdt_stop - Stop the Watchdog
 * 	Stops the watchdog.
 */
static void sh_wdt_stop(void)
{
	__u8 csr;

	del_timer(&timer);

	csr = sh_wdt_read_csr();
	csr &= ~WTCSR_TME;
	sh_wdt_write_csr(csr);
}

/**
 * 	sh_wdt_keepalive - Keep the Userspace Watchdog Alive
 * 	The Userspace watchdog got a KeepAlive: schedule the next heartbeat.
 */
static inline void sh_wdt_keepalive(void)
{
	next_heartbeat = jiffies + (heartbeat * HZ);
}

/**
 * 	sh_wdt_set_heartbeat - Set the Userspace Watchdog heartbeat
 * 	Set the Userspace Watchdog heartbeat
 */
static int sh_wdt_set_heartbeat(int t)
{
	if (unlikely((t < 1) || (t > 3600))) /* arbitrary upper limit */
		return -EINVAL;

	heartbeat = t;
	return 0;
}

/**
 * 	sh_wdt_ping - Ping the Watchdog
 *	@data: Unused
 *
 * 	Clears overflow bit, resets timer counter.
 */
static void sh_wdt_ping(unsigned long data)
{
	if (time_before(jiffies, next_heartbeat)) {
		__u8 csr;

		csr = sh_wdt_read_csr();
		csr &= ~WTCSR_IOVF;
		sh_wdt_write_csr(csr);

		sh_wdt_write_cnt(0);

		mod_timer(&timer, next_ping_period(clock_division_ratio));
	} else
		printk(KERN_WARNING PFX "Heartbeat lost! Will not ping "
		       "the watchdog\n");
}

/**
 * 	sh_wdt_open - Open the Device
 * 	@inode: inode of device
 * 	@file: file handle of device
 *
 * 	Watchdog device is opened and started.
 */
static int sh_wdt_open(struct inode *inode, struct file *file)
{
	if (test_and_set_bit(0, &shwdt_is_open))
		return -EBUSY;
	if (nowayout)
		__module_get(THIS_MODULE);

	sh_wdt_start();

	return nonseekable_open(inode, file);
}

/**
 * 	sh_wdt_close - Close the Device
 * 	@inode: inode of device
 * 	@file: file handle of device
 *
 * 	Watchdog device is closed and stopped.
 */
static int sh_wdt_close(struct inode *inode, struct file *file)
{
	if (shwdt_expect_close == 42) {
		sh_wdt_stop();
	} else {
		printk(KERN_CRIT PFX "Unexpected close, not "
		       "stopping watchdog!\n");
		sh_wdt_keepalive();
	}

	clear_bit(0, &shwdt_is_open);
	shwdt_expect_close = 0;

	return 0;
}

/**
 * 	sh_wdt_write - Write to Device
 * 	@file: file handle of device
 * 	@buf: buffer to write
 * 	@count: length of buffer
 * 	@ppos: offset
 *
 * 	Pings the watchdog on write.
 */
static ssize_t sh_wdt_write(struct file *file, const char *buf,
			    size_t count, loff_t *ppos)
{
	if (count) {
		if (!nowayout) {
			size_t i;

			shwdt_expect_close = 0;

			for (i = 0; i != count; i++) {
				char c;
				if (get_user(c, buf + i))
					return -EFAULT;
				if (c == 'V')
					shwdt_expect_close = 42;
			}
		}
		sh_wdt_keepalive();
	}

	return count;
}

/**
 * 	sh_wdt_mmap - map WDT/CPG registers into userspace
 * 	@file: file structure for the device
 * 	@vma: VMA to map the registers into
 *
 * 	A simple mmap() implementation for the corner cases where the counter
 * 	needs to be mapped in userspace directly. Due to the relatively small
 * 	size of the area, neighbouring registers not necessarily tied to the
 * 	CPG will also be accessible through the register page, so this remains
 * 	configurable for users that really know what they're doing.
 *
 *	Additionaly, the register page maps in the CPG register base relative
 *	to the nearest page-aligned boundary, which requires that userspace do
 *	the appropriate CPU subtype math for calculating the page offset for
 *	the counter value.
 */
static int sh_wdt_mmap(struct file *file, struct vm_area_struct *vma)
{
	int ret = -ENOSYS;

#ifdef CONFIG_SH_WDT_MMAP
	unsigned long addr;

	/* Only support the simple cases where we map in a register page. */
	if (((vma->vm_end - vma->vm_start) != PAGE_SIZE) || vma->vm_pgoff)
		return -EINVAL;

	/*
	 * Pick WTCNT as the start, it's usually the first register after the
	 * FRQCR, and neither one are generally page-aligned out of the box.
	 */
	addr = WTCNT & ~(PAGE_SIZE - 1);

	vma->vm_flags |= VM_IO;
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	if (io_remap_pfn_range(vma, vma->vm_start, addr >> PAGE_SHIFT,
			       PAGE_SIZE, vma->vm_page_prot)) {
		printk(KERN_ERR PFX "%s: io_remap_pfn_range failed\n",
		       __FUNCTION__);
		return -EAGAIN;
	}

	ret = 0;
#endif

	return ret;
}

/**
 * 	sh_wdt_ioctl - Query Device
 * 	@inode: inode of device
 * 	@file: file handle of device
 * 	@cmd: watchdog command
 * 	@arg: argument
 *
 * 	Query basic information from the device or ping it, as outlined by the
 * 	watchdog API.
 */
static int sh_wdt_ioctl(struct inode *inode, struct file *file,
			unsigned int cmd, unsigned long arg)
{
	int new_heartbeat;
	int options, retval = -EINVAL;

	switch (cmd) {
		case WDIOC_GETSUPPORT:
			return copy_to_user((struct watchdog_info *)arg,
					  &sh_wdt_info,
					  sizeof(sh_wdt_info)) ? -EFAULT : 0;
		case WDIOC_GETSTATUS:
		case WDIOC_GETBOOTSTATUS:
			return put_user(0, (int *)arg);
		case WDIOC_KEEPALIVE:
			sh_wdt_keepalive();
			return 0;
		case WDIOC_SETTIMEOUT:
			if (get_user(new_heartbeat, (int *)arg))
				return -EFAULT;

			if (sh_wdt_set_heartbeat(new_heartbeat))
				return -EINVAL;

			sh_wdt_keepalive();
			/* Fall */
		case WDIOC_GETTIMEOUT:
			return put_user(heartbeat, (int *)arg);
		case WDIOC_SETOPTIONS:
			if (get_user(options, (int *)arg))
				return -EFAULT;

			if (options & WDIOS_DISABLECARD) {
				sh_wdt_stop();
				retval = 0;
			}

			if (options & WDIOS_ENABLECARD) {
				sh_wdt_start();
				retval = 0;
			}

			return retval;
		default:
			return -ENOTTY;
	}

	return 0;
}

/**
 * 	sh_wdt_notify_sys - Notifier Handler
 * 	@this: notifier block
 * 	@code: notifier event
 * 	@unused: unused
 *
 * 	Handles specific events, such as turning off the watchdog during a
 * 	shutdown event.
 */
static int sh_wdt_notify_sys(struct notifier_block *this,
			     unsigned long code, void *unused)
{
	if (code == SYS_DOWN || code == SYS_HALT)
		sh_wdt_stop();

	return NOTIFY_DONE;
}

static const struct file_operations sh_wdt_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.write		= sh_wdt_write,
	.ioctl		= sh_wdt_ioctl,
	.open		= sh_wdt_open,
	.release	= sh_wdt_close,
	.mmap		= sh_wdt_mmap,
};

static struct watchdog_info sh_wdt_info = {
	.options		= WDIOF_KEEPALIVEPING | WDIOF_SETTIMEOUT |
				  WDIOF_MAGICCLOSE,
	.firmware_version	= 1,
	.identity		= "SH WDT",
};

static struct notifier_block sh_wdt_notifier = {
	.notifier_call		= sh_wdt_notify_sys,
};

static struct miscdevice sh_wdt_miscdev = {
	.minor		= WATCHDOG_MINOR,
	.name		= "watchdog",
	.fops		= &sh_wdt_fops,
};

/**
 * 	sh_wdt_init - Initialize module
 * 	Registers the device and notifier handler. Actual device
 * 	initialization is handled by sh_wdt_open().
 */
static int __init sh_wdt_init(void)
{
	int rc;

	if ((clock_division_ratio < 0x5) || (clock_division_ratio > 0x7)) {
		clock_division_ratio = WTCSR_CKS_4096;
		printk(KERN_INFO PFX "clock_division_ratio value must "
		       "be 0x5<=x<=0x7, using %d\n", clock_division_ratio);
	}

	rc = sh_wdt_set_heartbeat(heartbeat);
	if (unlikely(rc)) {
		heartbeat = WATCHDOG_HEARTBEAT;
		printk(KERN_INFO PFX "heartbeat value must "
		       "be 1<=x<=3600, using %d\n", heartbeat);
	}

	rc = register_reboot_notifier(&sh_wdt_notifier);
	if (unlikely(rc)) {
		printk(KERN_ERR PFX "Can't register reboot notifier (err=%d)\n",
		       rc);
		return rc;
	}

	rc = misc_register(&sh_wdt_miscdev);
	if (unlikely(rc)) {
		printk(KERN_ERR PFX "Can't register miscdev on "
		       "minor=%d (err=%d)\n", sh_wdt_miscdev.minor, rc);
		unregister_reboot_notifier(&sh_wdt_notifier);
		return rc;
	}

	printk(KERN_INFO PFX "initialized. heartbeat=%d sec (nowayout=%d)\n",
		heartbeat, nowayout);

	return 0;
}

/**
 * 	sh_wdt_exit - Deinitialize module
 * 	Unregisters the device and notifier handler. Actual device
 * 	deinitialization is handled by sh_wdt_close().
 */
static void __exit sh_wdt_exit(void)
{
	misc_deregister(&sh_wdt_miscdev);
	unregister_reboot_notifier(&sh_wdt_notifier);
}

MODULE_AUTHOR("Paul Mundt <lethal@linux-sh.org>");
MODULE_DESCRIPTION("SuperH watchdog driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);

module_param(clock_division_ratio, int, 0);
MODULE_PARM_DESC(clock_division_ratio, "Clock division ratio. Valid ranges are from 0x5 (1.31ms) to 0x7 (5.25ms). (default=" __MODULE_STRING(clock_division_ratio) ")");

module_param(heartbeat, int, 0);
MODULE_PARM_DESC(heartbeat, "Watchdog heartbeat in seconds. (1<=heartbeat<=3600, default=" __MODULE_STRING(WATCHDOG_HEARTBEAT) ")");

module_param(nowayout, int, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default=" __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

module_init(sh_wdt_init);
module_exit(sh_wdt_exit);
