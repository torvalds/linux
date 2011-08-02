/*
 * Intel & MS High Precision Event Timer Implementation.
 *
 * Copyright (C) 2003 Intel Corporation
 *	Venki Pallipadi
 * (c) Copyright 2004 Hewlett-Packard Development Company, L.P.
 *	Bob Picco <robert.picco@hp.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/miscdevice.h>
#include <linux/major.h>
#include <linux/ioport.h>
#include <linux/fcntl.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/mm.h>
#include <linux/proc_fs.h>
#include <linux/spinlock.h>
#include <linux/sysctl.h>
#include <linux/wait.h>
#include <linux/bcd.h>
#include <linux/seq_file.h>
#include <linux/bitops.h>
#include <linux/compat.h>
#include <linux/clocksource.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/io.h>

#include <asm/current.h>
#include <asm/system.h>
#include <asm/irq.h>
#include <asm/div64.h>

#include <linux/acpi.h>
#include <acpi/acpi_bus.h>
#include <linux/hpet.h>

/*
 * The High Precision Event Timer driver.
 * This driver is closely modelled after the rtc.c driver.
 * http://www.intel.com/hardwaredesign/hpetspec_1.pdf
 */
#define	HPET_USER_FREQ	(64)
#define	HPET_DRIFT	(500)

#define HPET_RANGE_SIZE		1024	/* from HPET spec */


/* WARNING -- don't get confused.  These macros are never used
 * to write the (single) counter, and rarely to read it.
 * They're badly named; to fix, someday.
 */
#if BITS_PER_LONG == 64
#define	write_counter(V, MC)	writeq(V, MC)
#define	read_counter(MC)	readq(MC)
#else
#define	write_counter(V, MC)	writel(V, MC)
#define	read_counter(MC)	readl(MC)
#endif

static DEFINE_MUTEX(hpet_mutex); /* replaces BKL */
static u32 hpet_nhpet, hpet_max_freq = HPET_USER_FREQ;

/* This clocksource driver currently only works on ia64 */
#ifdef CONFIG_IA64
static void __iomem *hpet_mctr;

static cycle_t read_hpet(struct clocksource *cs)
{
	return (cycle_t)read_counter((void __iomem *)hpet_mctr);
}

static struct clocksource clocksource_hpet = {
	.name		= "hpet",
	.rating		= 250,
	.read		= read_hpet,
	.mask		= CLOCKSOURCE_MASK(64),
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};
static struct clocksource *hpet_clocksource;
#endif

/* A lock for concurrent access by app and isr hpet activity. */
static DEFINE_SPINLOCK(hpet_lock);

#define	HPET_DEV_NAME	(7)

struct hpet_dev {
	struct hpets *hd_hpets;
	struct hpet __iomem *hd_hpet;
	struct hpet_timer __iomem *hd_timer;
	unsigned long hd_ireqfreq;
	unsigned long hd_irqdata;
	wait_queue_head_t hd_waitqueue;
	struct fasync_struct *hd_async_queue;
	unsigned int hd_flags;
	unsigned int hd_irq;
	unsigned int hd_hdwirq;
	char hd_name[HPET_DEV_NAME];
};

struct hpets {
	struct hpets *hp_next;
	struct hpet __iomem *hp_hpet;
	unsigned long hp_hpet_phys;
	struct clocksource *hp_clocksource;
	unsigned long long hp_tick_freq;
	unsigned long hp_delta;
	unsigned int hp_ntimer;
	unsigned int hp_which;
	struct hpet_dev hp_dev[1];
};

static struct hpets *hpets;

#define	HPET_OPEN		0x0001
#define	HPET_IE			0x0002	/* interrupt enabled */
#define	HPET_PERIODIC		0x0004
#define	HPET_SHARED_IRQ		0x0008


#ifndef readq
static inline unsigned long long readq(void __iomem *addr)
{
	return readl(addr) | (((unsigned long long)readl(addr + 4)) << 32LL);
}
#endif

#ifndef writeq
static inline void writeq(unsigned long long v, void __iomem *addr)
{
	writel(v & 0xffffffff, addr);
	writel(v >> 32, addr + 4);
}
#endif

static irqreturn_t hpet_interrupt(int irq, void *data)
{
	struct hpet_dev *devp;
	unsigned long isr;

	devp = data;
	isr = 1 << (devp - devp->hd_hpets->hp_dev);

	if ((devp->hd_flags & HPET_SHARED_IRQ) &&
	    !(isr & readl(&devp->hd_hpet->hpet_isr)))
		return IRQ_NONE;

	spin_lock(&hpet_lock);
	devp->hd_irqdata++;

	/*
	 * For non-periodic timers, increment the accumulator.
	 * This has the effect of treating non-periodic like periodic.
	 */
	if ((devp->hd_flags & (HPET_IE | HPET_PERIODIC)) == HPET_IE) {
		unsigned long m, t, mc, base, k;
		struct hpet __iomem *hpet = devp->hd_hpet;
		struct hpets *hpetp = devp->hd_hpets;

		t = devp->hd_ireqfreq;
		m = read_counter(&devp->hd_timer->hpet_compare);
		mc = read_counter(&hpet->hpet_mc);
		/* The time for the next interrupt would logically be t + m,
		 * however, if we are very unlucky and the interrupt is delayed
		 * for longer than t then we will completely miss the next
		 * interrupt if we set t + m and an application will hang.
		 * Therefore we need to make a more complex computation assuming
		 * that there exists a k for which the following is true:
		 * k * t + base < mc + delta
		 * (k + 1) * t + base > mc + delta
		 * where t is the interval in hpet ticks for the given freq,
		 * base is the theoretical start value 0 < base < t,
		 * mc is the main counter value at the time of the interrupt,
		 * delta is the time it takes to write the a value to the
		 * comparator.
		 * k may then be computed as (mc - base + delta) / t .
		 */
		base = mc % t;
		k = (mc - base + hpetp->hp_delta) / t;
		write_counter(t * (k + 1) + base,
			      &devp->hd_timer->hpet_compare);
	}

	if (devp->hd_flags & HPET_SHARED_IRQ)
		writel(isr, &devp->hd_hpet->hpet_isr);
	spin_unlock(&hpet_lock);

	wake_up_interruptible(&devp->hd_waitqueue);

	kill_fasync(&devp->hd_async_queue, SIGIO, POLL_IN);

	return IRQ_HANDLED;
}

static void hpet_timer_set_irq(struct hpet_dev *devp)
{
	unsigned long v;
	int irq, gsi;
	struct hpet_timer __iomem *timer;

	spin_lock_irq(&hpet_lock);
	if (devp->hd_hdwirq) {
		spin_unlock_irq(&hpet_lock);
		return;
	}

	timer = devp->hd_timer;

	/* we prefer level triggered mode */
	v = readl(&timer->hpet_config);
	if (!(v & Tn_INT_TYPE_CNF_MASK)) {
		v |= Tn_INT_TYPE_CNF_MASK;
		writel(v, &timer->hpet_config);
	}
	spin_unlock_irq(&hpet_lock);

	v = (readq(&timer->hpet_config) & Tn_INT_ROUTE_CAP_MASK) >>
				 Tn_INT_ROUTE_CAP_SHIFT;

	/*
	 * In PIC mode, skip IRQ0-4, IRQ6-9, IRQ12-15 which is always used by
	 * legacy device. In IO APIC mode, we skip all the legacy IRQS.
	 */
	if (acpi_irq_model == ACPI_IRQ_MODEL_PIC)
		v &= ~0xf3df;
	else
		v &= ~0xffff;

	for_each_set_bit(irq, &v, HPET_MAX_IRQ) {
		if (irq >= nr_irqs) {
			irq = HPET_MAX_IRQ;
			break;
		}

		gsi = acpi_register_gsi(NULL, irq, ACPI_LEVEL_SENSITIVE,
					ACPI_ACTIVE_LOW);
		if (gsi > 0)
			break;

		/* FIXME: Setup interrupt source table */
	}

	if (irq < HPET_MAX_IRQ) {
		spin_lock_irq(&hpet_lock);
		v = readl(&timer->hpet_config);
		v |= irq << Tn_INT_ROUTE_CNF_SHIFT;
		writel(v, &timer->hpet_config);
		devp->hd_hdwirq = gsi;
		spin_unlock_irq(&hpet_lock);
	}
	return;
}

static int hpet_open(struct inode *inode, struct file *file)
{
	struct hpet_dev *devp;
	struct hpets *hpetp;
	int i;

	if (file->f_mode & FMODE_WRITE)
		return -EINVAL;

	mutex_lock(&hpet_mutex);
	spin_lock_irq(&hpet_lock);

	for (devp = NULL, hpetp = hpets; hpetp && !devp; hpetp = hpetp->hp_next)
		for (i = 0; i < hpetp->hp_ntimer; i++)
			if (hpetp->hp_dev[i].hd_flags & HPET_OPEN)
				continue;
			else {
				devp = &hpetp->hp_dev[i];
				break;
			}

	if (!devp) {
		spin_unlock_irq(&hpet_lock);
		mutex_unlock(&hpet_mutex);
		return -EBUSY;
	}

	file->private_data = devp;
	devp->hd_irqdata = 0;
	devp->hd_flags |= HPET_OPEN;
	spin_unlock_irq(&hpet_lock);
	mutex_unlock(&hpet_mutex);

	hpet_timer_set_irq(devp);

	return 0;
}

static ssize_t
hpet_read(struct file *file, char __user *buf, size_t count, loff_t * ppos)
{
	DECLARE_WAITQUEUE(wait, current);
	unsigned long data;
	ssize_t retval;
	struct hpet_dev *devp;

	devp = file->private_data;
	if (!devp->hd_ireqfreq)
		return -EIO;

	if (count < sizeof(unsigned long))
		return -EINVAL;

	add_wait_queue(&devp->hd_waitqueue, &wait);

	for ( ; ; ) {
		set_current_state(TASK_INTERRUPTIBLE);

		spin_lock_irq(&hpet_lock);
		data = devp->hd_irqdata;
		devp->hd_irqdata = 0;
		spin_unlock_irq(&hpet_lock);

		if (data)
			break;
		else if (file->f_flags & O_NONBLOCK) {
			retval = -EAGAIN;
			goto out;
		} else if (signal_pending(current)) {
			retval = -ERESTARTSYS;
			goto out;
		}
		schedule();
	}

	retval = put_user(data, (unsigned long __user *)buf);
	if (!retval)
		retval = sizeof(unsigned long);
out:
	__set_current_state(TASK_RUNNING);
	remove_wait_queue(&devp->hd_waitqueue, &wait);

	return retval;
}

static unsigned int hpet_poll(struct file *file, poll_table * wait)
{
	unsigned long v;
	struct hpet_dev *devp;

	devp = file->private_data;

	if (!devp->hd_ireqfreq)
		return 0;

	poll_wait(file, &devp->hd_waitqueue, wait);

	spin_lock_irq(&hpet_lock);
	v = devp->hd_irqdata;
	spin_unlock_irq(&hpet_lock);

	if (v != 0)
		return POLLIN | POLLRDNORM;

	return 0;
}

static int hpet_mmap(struct file *file, struct vm_area_struct *vma)
{
#ifdef	CONFIG_HPET_MMAP
	struct hpet_dev *devp;
	unsigned long addr;

	if (((vma->vm_end - vma->vm_start) != PAGE_SIZE) || vma->vm_pgoff)
		return -EINVAL;

	devp = file->private_data;
	addr = devp->hd_hpets->hp_hpet_phys;

	if (addr & (PAGE_SIZE - 1))
		return -ENOSYS;

	vma->vm_flags |= VM_IO;
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	if (io_remap_pfn_range(vma, vma->vm_start, addr >> PAGE_SHIFT,
					PAGE_SIZE, vma->vm_page_prot)) {
		printk(KERN_ERR "%s: io_remap_pfn_range failed\n",
			__func__);
		return -EAGAIN;
	}

	return 0;
#else
	return -ENOSYS;
#endif
}

static int hpet_fasync(int fd, struct file *file, int on)
{
	struct hpet_dev *devp;

	devp = file->private_data;

	if (fasync_helper(fd, file, on, &devp->hd_async_queue) >= 0)
		return 0;
	else
		return -EIO;
}

static int hpet_release(struct inode *inode, struct file *file)
{
	struct hpet_dev *devp;
	struct hpet_timer __iomem *timer;
	int irq = 0;

	devp = file->private_data;
	timer = devp->hd_timer;

	spin_lock_irq(&hpet_lock);

	writeq((readq(&timer->hpet_config) & ~Tn_INT_ENB_CNF_MASK),
	       &timer->hpet_config);

	irq = devp->hd_irq;
	devp->hd_irq = 0;

	devp->hd_ireqfreq = 0;

	if (devp->hd_flags & HPET_PERIODIC
	    && readq(&timer->hpet_config) & Tn_TYPE_CNF_MASK) {
		unsigned long v;

		v = readq(&timer->hpet_config);
		v ^= Tn_TYPE_CNF_MASK;
		writeq(v, &timer->hpet_config);
	}

	devp->hd_flags &= ~(HPET_OPEN | HPET_IE | HPET_PERIODIC);
	spin_unlock_irq(&hpet_lock);

	if (irq)
		free_irq(irq, devp);

	file->private_data = NULL;
	return 0;
}

static int hpet_ioctl_ieon(struct hpet_dev *devp)
{
	struct hpet_timer __iomem *timer;
	struct hpet __iomem *hpet;
	struct hpets *hpetp;
	int irq;
	unsigned long g, v, t, m;
	unsigned long flags, isr;

	timer = devp->hd_timer;
	hpet = devp->hd_hpet;
	hpetp = devp->hd_hpets;

	if (!devp->hd_ireqfreq)
		return -EIO;

	spin_lock_irq(&hpet_lock);

	if (devp->hd_flags & HPET_IE) {
		spin_unlock_irq(&hpet_lock);
		return -EBUSY;
	}

	devp->hd_flags |= HPET_IE;

	if (readl(&timer->hpet_config) & Tn_INT_TYPE_CNF_MASK)
		devp->hd_flags |= HPET_SHARED_IRQ;
	spin_unlock_irq(&hpet_lock);

	irq = devp->hd_hdwirq;

	if (irq) {
		unsigned long irq_flags;

		if (devp->hd_flags & HPET_SHARED_IRQ) {
			/*
			 * To prevent the interrupt handler from seeing an
			 * unwanted interrupt status bit, program the timer
			 * so that it will not fire in the near future ...
			 */
			writel(readl(&timer->hpet_config) & ~Tn_TYPE_CNF_MASK,
			       &timer->hpet_config);
			write_counter(read_counter(&hpet->hpet_mc),
				      &timer->hpet_compare);
			/* ... and clear any left-over status. */
			isr = 1 << (devp - devp->hd_hpets->hp_dev);
			writel(isr, &hpet->hpet_isr);
		}

		sprintf(devp->hd_name, "hpet%d", (int)(devp - hpetp->hp_dev));
		irq_flags = devp->hd_flags & HPET_SHARED_IRQ
						? IRQF_SHARED : IRQF_DISABLED;
		if (request_irq(irq, hpet_interrupt, irq_flags,
				devp->hd_name, (void *)devp)) {
			printk(KERN_ERR "hpet: IRQ %d is not free\n", irq);
			irq = 0;
		}
	}

	if (irq == 0) {
		spin_lock_irq(&hpet_lock);
		devp->hd_flags ^= HPET_IE;
		spin_unlock_irq(&hpet_lock);
		return -EIO;
	}

	devp->hd_irq = irq;
	t = devp->hd_ireqfreq;
	v = readq(&timer->hpet_config);

	/* 64-bit comparators are not yet supported through the ioctls,
	 * so force this into 32-bit mode if it supports both modes
	 */
	g = v | Tn_32MODE_CNF_MASK | Tn_INT_ENB_CNF_MASK;

	if (devp->hd_flags & HPET_PERIODIC) {
		g |= Tn_TYPE_CNF_MASK;
		v |= Tn_TYPE_CNF_MASK | Tn_VAL_SET_CNF_MASK;
		writeq(v, &timer->hpet_config);
		local_irq_save(flags);

		/*
		 * NOTE: First we modify the hidden accumulator
		 * register supported by periodic-capable comparators.
		 * We never want to modify the (single) counter; that
		 * would affect all the comparators. The value written
		 * is the counter value when the first interrupt is due.
		 */
		m = read_counter(&hpet->hpet_mc);
		write_counter(t + m + hpetp->hp_delta, &timer->hpet_compare);
		/*
		 * Then we modify the comparator, indicating the period
		 * for subsequent interrupt.
		 */
		write_counter(t, &timer->hpet_compare);
	} else {
		local_irq_save(flags);
		m = read_counter(&hpet->hpet_mc);
		write_counter(t + m + hpetp->hp_delta, &timer->hpet_compare);
	}

	if (devp->hd_flags & HPET_SHARED_IRQ) {
		isr = 1 << (devp - devp->hd_hpets->hp_dev);
		writel(isr, &hpet->hpet_isr);
	}
	writeq(g, &timer->hpet_config);
	local_irq_restore(flags);

	return 0;
}

/* converts Hz to number of timer ticks */
static inline unsigned long hpet_time_div(struct hpets *hpets,
					  unsigned long dis)
{
	unsigned long long m;

	m = hpets->hp_tick_freq + (dis >> 1);
	do_div(m, dis);
	return (unsigned long)m;
}

static int
hpet_ioctl_common(struct hpet_dev *devp, int cmd, unsigned long arg,
		  struct hpet_info *info)
{
	struct hpet_timer __iomem *timer;
	struct hpet __iomem *hpet;
	struct hpets *hpetp;
	int err;
	unsigned long v;

	switch (cmd) {
	case HPET_IE_OFF:
	case HPET_INFO:
	case HPET_EPI:
	case HPET_DPI:
	case HPET_IRQFREQ:
		timer = devp->hd_timer;
		hpet = devp->hd_hpet;
		hpetp = devp->hd_hpets;
		break;
	case HPET_IE_ON:
		return hpet_ioctl_ieon(devp);
	default:
		return -EINVAL;
	}

	err = 0;

	switch (cmd) {
	case HPET_IE_OFF:
		if ((devp->hd_flags & HPET_IE) == 0)
			break;
		v = readq(&timer->hpet_config);
		v &= ~Tn_INT_ENB_CNF_MASK;
		writeq(v, &timer->hpet_config);
		if (devp->hd_irq) {
			free_irq(devp->hd_irq, devp);
			devp->hd_irq = 0;
		}
		devp->hd_flags ^= HPET_IE;
		break;
	case HPET_INFO:
		{
			memset(info, 0, sizeof(*info));
			if (devp->hd_ireqfreq)
				info->hi_ireqfreq =
					hpet_time_div(hpetp, devp->hd_ireqfreq);
			info->hi_flags =
			    readq(&timer->hpet_config) & Tn_PER_INT_CAP_MASK;
			info->hi_hpet = hpetp->hp_which;
			info->hi_timer = devp - hpetp->hp_dev;
			break;
		}
	case HPET_EPI:
		v = readq(&timer->hpet_config);
		if ((v & Tn_PER_INT_CAP_MASK) == 0) {
			err = -ENXIO;
			break;
		}
		devp->hd_flags |= HPET_PERIODIC;
		break;
	case HPET_DPI:
		v = readq(&timer->hpet_config);
		if ((v & Tn_PER_INT_CAP_MASK) == 0) {
			err = -ENXIO;
			break;
		}
		if (devp->hd_flags & HPET_PERIODIC &&
		    readq(&timer->hpet_config) & Tn_TYPE_CNF_MASK) {
			v = readq(&timer->hpet_config);
			v ^= Tn_TYPE_CNF_MASK;
			writeq(v, &timer->hpet_config);
		}
		devp->hd_flags &= ~HPET_PERIODIC;
		break;
	case HPET_IRQFREQ:
		if ((arg > hpet_max_freq) &&
		    !capable(CAP_SYS_RESOURCE)) {
			err = -EACCES;
			break;
		}

		if (!arg) {
			err = -EINVAL;
			break;
		}

		devp->hd_ireqfreq = hpet_time_div(hpetp, arg);
	}

	return err;
}

static long
hpet_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct hpet_info info;
	int err;

	mutex_lock(&hpet_mutex);
	err = hpet_ioctl_common(file->private_data, cmd, arg, &info);
	mutex_unlock(&hpet_mutex);

	if ((cmd == HPET_INFO) && !err &&
	    (copy_to_user((void __user *)arg, &info, sizeof(info))))
		err = -EFAULT;

	return err;
}

#ifdef CONFIG_COMPAT
struct compat_hpet_info {
	compat_ulong_t hi_ireqfreq;	/* Hz */
	compat_ulong_t hi_flags;	/* information */
	unsigned short hi_hpet;
	unsigned short hi_timer;
};

static long
hpet_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct hpet_info info;
	int err;

	mutex_lock(&hpet_mutex);
	err = hpet_ioctl_common(file->private_data, cmd, arg, &info);
	mutex_unlock(&hpet_mutex);

	if ((cmd == HPET_INFO) && !err) {
		struct compat_hpet_info __user *u = compat_ptr(arg);
		if (put_user(info.hi_ireqfreq, &u->hi_ireqfreq) ||
		    put_user(info.hi_flags, &u->hi_flags) ||
		    put_user(info.hi_hpet, &u->hi_hpet) ||
		    put_user(info.hi_timer, &u->hi_timer))
			err = -EFAULT;
	}

	return err;
}
#endif

static const struct file_operations hpet_fops = {
	.owner = THIS_MODULE,
	.llseek = no_llseek,
	.read = hpet_read,
	.poll = hpet_poll,
	.unlocked_ioctl = hpet_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = hpet_compat_ioctl,
#endif
	.open = hpet_open,
	.release = hpet_release,
	.fasync = hpet_fasync,
	.mmap = hpet_mmap,
};

static int hpet_is_known(struct hpet_data *hdp)
{
	struct hpets *hpetp;

	for (hpetp = hpets; hpetp; hpetp = hpetp->hp_next)
		if (hpetp->hp_hpet_phys == hdp->hd_phys_address)
			return 1;

	return 0;
}

static ctl_table hpet_table[] = {
	{
	 .procname = "max-user-freq",
	 .data = &hpet_max_freq,
	 .maxlen = sizeof(int),
	 .mode = 0644,
	 .proc_handler = proc_dointvec,
	 },
	{}
};

static ctl_table hpet_root[] = {
	{
	 .procname = "hpet",
	 .maxlen = 0,
	 .mode = 0555,
	 .child = hpet_table,
	 },
	{}
};

static ctl_table dev_root[] = {
	{
	 .procname = "dev",
	 .maxlen = 0,
	 .mode = 0555,
	 .child = hpet_root,
	 },
	{}
};

static struct ctl_table_header *sysctl_header;

/*
 * Adjustment for when arming the timer with
 * initial conditions.  That is, main counter
 * ticks expired before interrupts are enabled.
 */
#define	TICK_CALIBRATE	(1000UL)

static unsigned long __hpet_calibrate(struct hpets *hpetp)
{
	struct hpet_timer __iomem *timer = NULL;
	unsigned long t, m, count, i, flags, start;
	struct hpet_dev *devp;
	int j;
	struct hpet __iomem *hpet;

	for (j = 0, devp = hpetp->hp_dev; j < hpetp->hp_ntimer; j++, devp++)
		if ((devp->hd_flags & HPET_OPEN) == 0) {
			timer = devp->hd_timer;
			break;
		}

	if (!timer)
		return 0;

	hpet = hpetp->hp_hpet;
	t = read_counter(&timer->hpet_compare);

	i = 0;
	count = hpet_time_div(hpetp, TICK_CALIBRATE);

	local_irq_save(flags);

	start = read_counter(&hpet->hpet_mc);

	do {
		m = read_counter(&hpet->hpet_mc);
		write_counter(t + m + hpetp->hp_delta, &timer->hpet_compare);
	} while (i++, (m - start) < count);

	local_irq_restore(flags);

	return (m - start) / i;
}

static unsigned long hpet_calibrate(struct hpets *hpetp)
{
	unsigned long ret = -1;
	unsigned long tmp;

	/*
	 * Try to calibrate until return value becomes stable small value.
	 * If SMI interruption occurs in calibration loop, the return value
	 * will be big. This avoids its impact.
	 */
	for ( ; ; ) {
		tmp = __hpet_calibrate(hpetp);
		if (ret <= tmp)
			break;
		ret = tmp;
	}

	return ret;
}

int hpet_alloc(struct hpet_data *hdp)
{
	u64 cap, mcfg;
	struct hpet_dev *devp;
	u32 i, ntimer;
	struct hpets *hpetp;
	size_t siz;
	struct hpet __iomem *hpet;
	static struct hpets *last;
	unsigned long period;
	unsigned long long temp;
	u32 remainder;

	/*
	 * hpet_alloc can be called by platform dependent code.
	 * If platform dependent code has allocated the hpet that
	 * ACPI has also reported, then we catch it here.
	 */
	if (hpet_is_known(hdp)) {
		printk(KERN_DEBUG "%s: duplicate HPET ignored\n",
			__func__);
		return 0;
	}

	siz = sizeof(struct hpets) + ((hdp->hd_nirqs - 1) *
				      sizeof(struct hpet_dev));

	hpetp = kzalloc(siz, GFP_KERNEL);

	if (!hpetp)
		return -ENOMEM;

	hpetp->hp_which = hpet_nhpet++;
	hpetp->hp_hpet = hdp->hd_address;
	hpetp->hp_hpet_phys = hdp->hd_phys_address;

	hpetp->hp_ntimer = hdp->hd_nirqs;

	for (i = 0; i < hdp->hd_nirqs; i++)
		hpetp->hp_dev[i].hd_hdwirq = hdp->hd_irq[i];

	hpet = hpetp->hp_hpet;

	cap = readq(&hpet->hpet_cap);

	ntimer = ((cap & HPET_NUM_TIM_CAP_MASK) >> HPET_NUM_TIM_CAP_SHIFT) + 1;

	if (hpetp->hp_ntimer != ntimer) {
		printk(KERN_WARNING "hpet: number irqs doesn't agree"
		       " with number of timers\n");
		kfree(hpetp);
		return -ENODEV;
	}

	if (last)
		last->hp_next = hpetp;
	else
		hpets = hpetp;

	last = hpetp;

	period = (cap & HPET_COUNTER_CLK_PERIOD_MASK) >>
		HPET_COUNTER_CLK_PERIOD_SHIFT; /* fs, 10^-15 */
	temp = 1000000000000000uLL; /* 10^15 femtoseconds per second */
	temp += period >> 1; /* round */
	do_div(temp, period);
	hpetp->hp_tick_freq = temp; /* ticks per second */

	printk(KERN_INFO "hpet%d: at MMIO 0x%lx, IRQ%s",
		hpetp->hp_which, hdp->hd_phys_address,
		hpetp->hp_ntimer > 1 ? "s" : "");
	for (i = 0; i < hpetp->hp_ntimer; i++)
		printk("%s %d", i > 0 ? "," : "", hdp->hd_irq[i]);
	printk("\n");

	temp = hpetp->hp_tick_freq;
	remainder = do_div(temp, 1000000);
	printk(KERN_INFO
		"hpet%u: %u comparators, %d-bit %u.%06u MHz counter\n",
		hpetp->hp_which, hpetp->hp_ntimer,
		cap & HPET_COUNTER_SIZE_MASK ? 64 : 32,
		(unsigned) temp, remainder);

	mcfg = readq(&hpet->hpet_config);
	if ((mcfg & HPET_ENABLE_CNF_MASK) == 0) {
		write_counter(0L, &hpet->hpet_mc);
		mcfg |= HPET_ENABLE_CNF_MASK;
		writeq(mcfg, &hpet->hpet_config);
	}

	for (i = 0, devp = hpetp->hp_dev; i < hpetp->hp_ntimer; i++, devp++) {
		struct hpet_timer __iomem *timer;

		timer = &hpet->hpet_timers[devp - hpetp->hp_dev];

		devp->hd_hpets = hpetp;
		devp->hd_hpet = hpet;
		devp->hd_timer = timer;

		/*
		 * If the timer was reserved by platform code,
		 * then make timer unavailable for opens.
		 */
		if (hdp->hd_state & (1 << i)) {
			devp->hd_flags = HPET_OPEN;
			continue;
		}

		init_waitqueue_head(&devp->hd_waitqueue);
	}

	hpetp->hp_delta = hpet_calibrate(hpetp);

/* This clocksource driver currently only works on ia64 */
#ifdef CONFIG_IA64
	if (!hpet_clocksource) {
		hpet_mctr = (void __iomem *)&hpetp->hp_hpet->hpet_mc;
		CLKSRC_FSYS_MMIO_SET(clocksource_hpet.fsys_mmio, hpet_mctr);
		clocksource_register_hz(&clocksource_hpet, hpetp->hp_tick_freq);
		hpetp->hp_clocksource = &clocksource_hpet;
		hpet_clocksource = &clocksource_hpet;
	}
#endif

	return 0;
}

static acpi_status hpet_resources(struct acpi_resource *res, void *data)
{
	struct hpet_data *hdp;
	acpi_status status;
	struct acpi_resource_address64 addr;

	hdp = data;

	status = acpi_resource_to_address64(res, &addr);

	if (ACPI_SUCCESS(status)) {
		hdp->hd_phys_address = addr.minimum;
		hdp->hd_address = ioremap(addr.minimum, addr.address_length);

		if (hpet_is_known(hdp)) {
			iounmap(hdp->hd_address);
			return AE_ALREADY_EXISTS;
		}
	} else if (res->type == ACPI_RESOURCE_TYPE_FIXED_MEMORY32) {
		struct acpi_resource_fixed_memory32 *fixmem32;

		fixmem32 = &res->data.fixed_memory32;
		if (!fixmem32)
			return AE_NO_MEMORY;

		hdp->hd_phys_address = fixmem32->address;
		hdp->hd_address = ioremap(fixmem32->address,
						HPET_RANGE_SIZE);

		if (hpet_is_known(hdp)) {
			iounmap(hdp->hd_address);
			return AE_ALREADY_EXISTS;
		}
	} else if (res->type == ACPI_RESOURCE_TYPE_EXTENDED_IRQ) {
		struct acpi_resource_extended_irq *irqp;
		int i, irq;

		irqp = &res->data.extended_irq;

		for (i = 0; i < irqp->interrupt_count; i++) {
			irq = acpi_register_gsi(NULL, irqp->interrupts[i],
				      irqp->triggering, irqp->polarity);
			if (irq < 0)
				return AE_ERROR;

			hdp->hd_irq[hdp->hd_nirqs] = irq;
			hdp->hd_nirqs++;
		}
	}

	return AE_OK;
}

static int hpet_acpi_add(struct acpi_device *device)
{
	acpi_status result;
	struct hpet_data data;

	memset(&data, 0, sizeof(data));

	result =
	    acpi_walk_resources(device->handle, METHOD_NAME__CRS,
				hpet_resources, &data);

	if (ACPI_FAILURE(result))
		return -ENODEV;

	if (!data.hd_address || !data.hd_nirqs) {
		if (data.hd_address)
			iounmap(data.hd_address);
		printk("%s: no address or irqs in _CRS\n", __func__);
		return -ENODEV;
	}

	return hpet_alloc(&data);
}

static int hpet_acpi_remove(struct acpi_device *device, int type)
{
	/* XXX need to unregister clocksource, dealloc mem, etc */
	return -EINVAL;
}

static const struct acpi_device_id hpet_device_ids[] = {
	{"PNP0103", 0},
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, hpet_device_ids);

static struct acpi_driver hpet_acpi_driver = {
	.name = "hpet",
	.ids = hpet_device_ids,
	.ops = {
		.add = hpet_acpi_add,
		.remove = hpet_acpi_remove,
		},
};

static struct miscdevice hpet_misc = { HPET_MINOR, "hpet", &hpet_fops };

static int __init hpet_init(void)
{
	int result;

	result = misc_register(&hpet_misc);
	if (result < 0)
		return -ENODEV;

	sysctl_header = register_sysctl_table(dev_root);

	result = acpi_bus_register_driver(&hpet_acpi_driver);
	if (result < 0) {
		if (sysctl_header)
			unregister_sysctl_table(sysctl_header);
		misc_deregister(&hpet_misc);
		return result;
	}

	return 0;
}

static void __exit hpet_exit(void)
{
	acpi_bus_unregister_driver(&hpet_acpi_driver);

	if (sysctl_header)
		unregister_sysctl_table(sysctl_header);
	misc_deregister(&hpet_misc);

	return;
}

module_init(hpet_init);
module_exit(hpet_exit);
MODULE_AUTHOR("Bob Picco <Robert.Picco@hp.com>");
MODULE_LICENSE("GPL");
