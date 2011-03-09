/*
 * Timer device implementation for SGI UV platform.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2009 Silicon Graphics, Inc.  All rights reserved.
 *
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/ioctl.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/mmtimer.h>
#include <linux/miscdevice.h>
#include <linux/posix-timers.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/math64.h>

#include <asm/genapic.h>
#include <asm/uv/uv_hub.h>
#include <asm/uv/bios.h>
#include <asm/uv/uv.h>

MODULE_AUTHOR("Dimitri Sivanich <sivanich@sgi.com>");
MODULE_DESCRIPTION("SGI UV Memory Mapped RTC Timer");
MODULE_LICENSE("GPL");

/* name of the device, usually in /dev */
#define UV_MMTIMER_NAME "mmtimer"
#define UV_MMTIMER_DESC "SGI UV Memory Mapped RTC Timer"
#define UV_MMTIMER_VERSION "1.0"

static long uv_mmtimer_ioctl(struct file *file, unsigned int cmd,
						unsigned long arg);
static int uv_mmtimer_mmap(struct file *file, struct vm_area_struct *vma);

/*
 * Period in femtoseconds (10^-15 s)
 */
static unsigned long uv_mmtimer_femtoperiod;

static const struct file_operations uv_mmtimer_fops = {
	.owner = THIS_MODULE,
	.mmap =	uv_mmtimer_mmap,
	.unlocked_ioctl = uv_mmtimer_ioctl,
	.llseek = noop_llseek,
};

/**
 * uv_mmtimer_ioctl - ioctl interface for /dev/uv_mmtimer
 * @file: file structure for the device
 * @cmd: command to execute
 * @arg: optional argument to command
 *
 * Executes the command specified by @cmd.  Returns 0 for success, < 0 for
 * failure.
 *
 * Valid commands:
 *
 * %MMTIMER_GETOFFSET - Should return the offset (relative to the start
 * of the page where the registers are mapped) for the counter in question.
 *
 * %MMTIMER_GETRES - Returns the resolution of the clock in femto (10^-15)
 * seconds
 *
 * %MMTIMER_GETFREQ - Copies the frequency of the clock in Hz to the address
 * specified by @arg
 *
 * %MMTIMER_GETBITS - Returns the number of bits in the clock's counter
 *
 * %MMTIMER_MMAPAVAIL - Returns 1 if registers can be mmap'd into userspace
 *
 * %MMTIMER_GETCOUNTER - Gets the current value in the counter and places it
 * in the address specified by @arg.
 */
static long uv_mmtimer_ioctl(struct file *file, unsigned int cmd,
						unsigned long arg)
{
	int ret = 0;

	switch (cmd) {
	case MMTIMER_GETOFFSET:	/* offset of the counter */
		/*
		 * Starting with HUB rev 2.0, the UV RTC register is
		 * replicated across all cachelines of it's own page.
		 * This allows faster simultaneous reads from a given socket.
		 *
		 * The offset returned is in 64 bit units.
		 */
		if (uv_get_min_hub_revision_id() == 1)
			ret = 0;
		else
			ret = ((uv_blade_processor_id() * L1_CACHE_BYTES) %
					PAGE_SIZE) / 8;
		break;

	case MMTIMER_GETRES: /* resolution of the clock in 10^-15 s */
		if (copy_to_user((unsigned long __user *)arg,
				&uv_mmtimer_femtoperiod, sizeof(unsigned long)))
			ret = -EFAULT;
		break;

	case MMTIMER_GETFREQ: /* frequency in Hz */
		if (copy_to_user((unsigned long __user *)arg,
				&sn_rtc_cycles_per_second,
				sizeof(unsigned long)))
			ret = -EFAULT;
		break;

	case MMTIMER_GETBITS: /* number of bits in the clock */
		ret = hweight64(UVH_RTC_REAL_TIME_CLOCK_MASK);
		break;

	case MMTIMER_MMAPAVAIL:
		ret = 1;
		break;

	case MMTIMER_GETCOUNTER:
		if (copy_to_user((unsigned long __user *)arg,
				(unsigned long *)uv_local_mmr_address(UVH_RTC),
				sizeof(unsigned long)))
			ret = -EFAULT;
		break;
	default:
		ret = -ENOTTY;
		break;
	}
	return ret;
}

/**
 * uv_mmtimer_mmap - maps the clock's registers into userspace
 * @file: file structure for the device
 * @vma: VMA to map the registers into
 *
 * Calls remap_pfn_range() to map the clock's registers into
 * the calling process' address space.
 */
static int uv_mmtimer_mmap(struct file *file, struct vm_area_struct *vma)
{
	unsigned long uv_mmtimer_addr;

	if (vma->vm_end - vma->vm_start != PAGE_SIZE)
		return -EINVAL;

	if (vma->vm_flags & VM_WRITE)
		return -EPERM;

	if (PAGE_SIZE > (1 << 16))
		return -ENOSYS;

	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	uv_mmtimer_addr = UV_LOCAL_MMR_BASE | UVH_RTC;
	uv_mmtimer_addr &= ~(PAGE_SIZE - 1);
	uv_mmtimer_addr &= 0xfffffffffffffffUL;

	if (remap_pfn_range(vma, vma->vm_start, uv_mmtimer_addr >> PAGE_SHIFT,
					PAGE_SIZE, vma->vm_page_prot)) {
		printk(KERN_ERR "remap_pfn_range failed in uv_mmtimer_mmap\n");
		return -EAGAIN;
	}

	return 0;
}

static struct miscdevice uv_mmtimer_miscdev = {
	MISC_DYNAMIC_MINOR,
	UV_MMTIMER_NAME,
	&uv_mmtimer_fops
};


/**
 * uv_mmtimer_init - device initialization routine
 *
 * Does initial setup for the uv_mmtimer device.
 */
static int __init uv_mmtimer_init(void)
{
	if (!is_uv_system()) {
		printk(KERN_ERR "%s: Hardware unsupported\n", UV_MMTIMER_NAME);
		return -1;
	}

	/*
	 * Sanity check the cycles/sec variable
	 */
	if (sn_rtc_cycles_per_second < 100000) {
		printk(KERN_ERR "%s: unable to determine clock frequency\n",
		       UV_MMTIMER_NAME);
		return -1;
	}

	uv_mmtimer_femtoperiod = ((unsigned long)1E15 +
				sn_rtc_cycles_per_second / 2) /
				sn_rtc_cycles_per_second;

	if (misc_register(&uv_mmtimer_miscdev)) {
		printk(KERN_ERR "%s: failed to register device\n",
		       UV_MMTIMER_NAME);
		return -1;
	}

	printk(KERN_INFO "%s: v%s, %ld MHz\n", UV_MMTIMER_DESC,
		UV_MMTIMER_VERSION,
		sn_rtc_cycles_per_second/(unsigned long)1E6);

	return 0;
}

module_init(uv_mmtimer_init);
