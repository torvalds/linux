/*
 * Timer device implementation for SGI SN platforms.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2001-2006 Silicon Graphics, Inc.  All rights reserved.
 *
 * This driver exports an API that should be supportable by any HPET or IA-PC
 * multimedia timer.  The code below is currently specific to the SGI Altix
 * SHub RTC, however.
 *
 * 11/01/01 - jbarnes - initial revision
 * 9/10/04 - Christoph Lameter - remove interrupt support for kernel inclusion
 * 10/1/04 - Christoph Lameter - provide posix clock CLOCK_SGI_CYCLE
 * 10/13/04 - Christoph Lameter, Dimitri Sivanich - provide timer interrupt
 *		support via the posix timer interface
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/ioctl.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/mmtimer.h>
#include <linux/miscdevice.h>
#include <linux/posix-timers.h>
#include <linux/interrupt.h>

#include <asm/uaccess.h>
#include <asm/sn/addrs.h>
#include <asm/sn/intr.h>
#include <asm/sn/shub_mmr.h>
#include <asm/sn/nodepda.h>
#include <asm/sn/shubio.h>

MODULE_AUTHOR("Jesse Barnes <jbarnes@sgi.com>");
MODULE_DESCRIPTION("SGI Altix RTC Timer");
MODULE_LICENSE("GPL");

/* name of the device, usually in /dev */
#define MMTIMER_NAME "mmtimer"
#define MMTIMER_DESC "SGI Altix RTC Timer"
#define MMTIMER_VERSION "2.1"

#define RTC_BITS 55 /* 55 bits for this implementation */

extern unsigned long sn_rtc_cycles_per_second;

#define RTC_COUNTER_ADDR        ((long *)LOCAL_MMR_ADDR(SH_RTC))

#define rtc_time()              (*RTC_COUNTER_ADDR)

static int mmtimer_ioctl(struct inode *inode, struct file *file,
			 unsigned int cmd, unsigned long arg);
static int mmtimer_mmap(struct file *file, struct vm_area_struct *vma);

/*
 * Period in femtoseconds (10^-15 s)
 */
static unsigned long mmtimer_femtoperiod = 0;

static struct file_operations mmtimer_fops = {
	.owner =	THIS_MODULE,
	.mmap =		mmtimer_mmap,
	.ioctl =	mmtimer_ioctl,
};

/*
 * We only have comparison registers RTC1-4 currently available per
 * node.  RTC0 is used by SAL.
 */
#define NUM_COMPARATORS 3
/* Check for an RTC interrupt pending */
static int inline mmtimer_int_pending(int comparator)
{
	if (HUB_L((unsigned long *)LOCAL_MMR_ADDR(SH_EVENT_OCCURRED)) &
			SH_EVENT_OCCURRED_RTC1_INT_MASK << comparator)
		return 1;
	else
		return 0;
}
/* Clear the RTC interrupt pending bit */
static void inline mmtimer_clr_int_pending(int comparator)
{
	HUB_S((u64 *)LOCAL_MMR_ADDR(SH_EVENT_OCCURRED_ALIAS),
		SH_EVENT_OCCURRED_RTC1_INT_MASK << comparator);
}

/* Setup timer on comparator RTC1 */
static void inline mmtimer_setup_int_0(u64 expires)
{
	u64 val;

	/* Disable interrupt */
	HUB_S((u64 *)LOCAL_MMR_ADDR(SH_RTC1_INT_ENABLE), 0UL);

	/* Initialize comparator value */
	HUB_S((u64 *)LOCAL_MMR_ADDR(SH_INT_CMPB), -1L);

	/* Clear pending bit */
	mmtimer_clr_int_pending(0);

	val = ((u64)SGI_MMTIMER_VECTOR << SH_RTC1_INT_CONFIG_IDX_SHFT) |
		((u64)cpu_physical_id(smp_processor_id()) <<
			SH_RTC1_INT_CONFIG_PID_SHFT);

	/* Set configuration */
	HUB_S((u64 *)LOCAL_MMR_ADDR(SH_RTC1_INT_CONFIG), val);

	/* Enable RTC interrupts */
	HUB_S((u64 *)LOCAL_MMR_ADDR(SH_RTC1_INT_ENABLE), 1UL);

	/* Initialize comparator value */
	HUB_S((u64 *)LOCAL_MMR_ADDR(SH_INT_CMPB), expires);


}

/* Setup timer on comparator RTC2 */
static void inline mmtimer_setup_int_1(u64 expires)
{
	u64 val;

	HUB_S((u64 *)LOCAL_MMR_ADDR(SH_RTC2_INT_ENABLE), 0UL);

	HUB_S((u64 *)LOCAL_MMR_ADDR(SH_INT_CMPC), -1L);

	mmtimer_clr_int_pending(1);

	val = ((u64)SGI_MMTIMER_VECTOR << SH_RTC2_INT_CONFIG_IDX_SHFT) |
		((u64)cpu_physical_id(smp_processor_id()) <<
			SH_RTC2_INT_CONFIG_PID_SHFT);

	HUB_S((u64 *)LOCAL_MMR_ADDR(SH_RTC2_INT_CONFIG), val);

	HUB_S((u64 *)LOCAL_MMR_ADDR(SH_RTC2_INT_ENABLE), 1UL);

	HUB_S((u64 *)LOCAL_MMR_ADDR(SH_INT_CMPC), expires);
}

/* Setup timer on comparator RTC3 */
static void inline mmtimer_setup_int_2(u64 expires)
{
	u64 val;

	HUB_S((u64 *)LOCAL_MMR_ADDR(SH_RTC3_INT_ENABLE), 0UL);

	HUB_S((u64 *)LOCAL_MMR_ADDR(SH_INT_CMPD), -1L);

	mmtimer_clr_int_pending(2);

	val = ((u64)SGI_MMTIMER_VECTOR << SH_RTC3_INT_CONFIG_IDX_SHFT) |
		((u64)cpu_physical_id(smp_processor_id()) <<
			SH_RTC3_INT_CONFIG_PID_SHFT);

	HUB_S((u64 *)LOCAL_MMR_ADDR(SH_RTC3_INT_CONFIG), val);

	HUB_S((u64 *)LOCAL_MMR_ADDR(SH_RTC3_INT_ENABLE), 1UL);

	HUB_S((u64 *)LOCAL_MMR_ADDR(SH_INT_CMPD), expires);
}

/*
 * This function must be called with interrupts disabled and preemption off
 * in order to insure that the setup succeeds in a deterministic time frame.
 * It will check if the interrupt setup succeeded.
 */
static int inline mmtimer_setup(int comparator, unsigned long expires)
{

	switch (comparator) {
	case 0:
		mmtimer_setup_int_0(expires);
		break;
	case 1:
		mmtimer_setup_int_1(expires);
		break;
	case 2:
		mmtimer_setup_int_2(expires);
		break;
	}
	/* We might've missed our expiration time */
	if (rtc_time() < expires)
		return 1;

	/*
	 * If an interrupt is already pending then its okay
	 * if not then we failed
	 */
	return mmtimer_int_pending(comparator);
}

static int inline mmtimer_disable_int(long nasid, int comparator)
{
	switch (comparator) {
	case 0:
		nasid == -1 ? HUB_S((u64 *)LOCAL_MMR_ADDR(SH_RTC1_INT_ENABLE),
			0UL) : REMOTE_HUB_S(nasid, SH_RTC1_INT_ENABLE, 0UL);
		break;
	case 1:
		nasid == -1 ? HUB_S((u64 *)LOCAL_MMR_ADDR(SH_RTC2_INT_ENABLE),
			0UL) : REMOTE_HUB_S(nasid, SH_RTC2_INT_ENABLE, 0UL);
		break;
	case 2:
		nasid == -1 ? HUB_S((u64 *)LOCAL_MMR_ADDR(SH_RTC3_INT_ENABLE),
			0UL) : REMOTE_HUB_S(nasid, SH_RTC3_INT_ENABLE, 0UL);
		break;
	default:
		return -EFAULT;
	}
	return 0;
}

#define TIMER_OFF 0xbadcabLL

/* There is one of these for each comparator */
typedef struct mmtimer {
	spinlock_t lock ____cacheline_aligned;
	struct k_itimer *timer;
	int i;
	int cpu;
	struct tasklet_struct tasklet;
} mmtimer_t;

static mmtimer_t ** timers;

/**
 * mmtimer_ioctl - ioctl interface for /dev/mmtimer
 * @inode: inode of the device
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
 * %MMTIMER_MMAPAVAIL - Returns 1 if the registers can be mmap'd into userspace
 *
 * %MMTIMER_GETCOUNTER - Gets the current value in the counter and places it
 * in the address specified by @arg.
 */
static int mmtimer_ioctl(struct inode *inode, struct file *file,
			 unsigned int cmd, unsigned long arg)
{
	int ret = 0;

	switch (cmd) {
	case MMTIMER_GETOFFSET:	/* offset of the counter */
		/*
		 * SN RTC registers are on their own 64k page
		 */
		if(PAGE_SIZE <= (1 << 16))
			ret = (((long)RTC_COUNTER_ADDR) & (PAGE_SIZE-1)) / 8;
		else
			ret = -ENOSYS;
		break;

	case MMTIMER_GETRES: /* resolution of the clock in 10^-15 s */
		if(copy_to_user((unsigned long __user *)arg,
				&mmtimer_femtoperiod, sizeof(unsigned long)))
			return -EFAULT;
		break;

	case MMTIMER_GETFREQ: /* frequency in Hz */
		if(copy_to_user((unsigned long __user *)arg,
				&sn_rtc_cycles_per_second,
				sizeof(unsigned long)))
			return -EFAULT;
		ret = 0;
		break;

	case MMTIMER_GETBITS: /* number of bits in the clock */
		ret = RTC_BITS;
		break;

	case MMTIMER_MMAPAVAIL: /* can we mmap the clock into userspace? */
		ret = (PAGE_SIZE <= (1 << 16)) ? 1 : 0;
		break;

	case MMTIMER_GETCOUNTER:
		if(copy_to_user((unsigned long __user *)arg,
				RTC_COUNTER_ADDR, sizeof(unsigned long)))
			return -EFAULT;
		break;
	default:
		ret = -ENOSYS;
		break;
	}

	return ret;
}

/**
 * mmtimer_mmap - maps the clock's registers into userspace
 * @file: file structure for the device
 * @vma: VMA to map the registers into
 *
 * Calls remap_pfn_range() to map the clock's registers into
 * the calling process' address space.
 */
static int mmtimer_mmap(struct file *file, struct vm_area_struct *vma)
{
	unsigned long mmtimer_addr;

	if (vma->vm_end - vma->vm_start != PAGE_SIZE)
		return -EINVAL;

	if (vma->vm_flags & VM_WRITE)
		return -EPERM;

	if (PAGE_SIZE > (1 << 16))
		return -ENOSYS;

	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	mmtimer_addr = __pa(RTC_COUNTER_ADDR);
	mmtimer_addr &= ~(PAGE_SIZE - 1);
	mmtimer_addr &= 0xfffffffffffffffUL;

	if (remap_pfn_range(vma, vma->vm_start, mmtimer_addr >> PAGE_SHIFT,
					PAGE_SIZE, vma->vm_page_prot)) {
		printk(KERN_ERR "remap_pfn_range failed in mmtimer.c\n");
		return -EAGAIN;
	}

	return 0;
}

static struct miscdevice mmtimer_miscdev = {
	SGI_MMTIMER,
	MMTIMER_NAME,
	&mmtimer_fops
};

static struct timespec sgi_clock_offset;
static int sgi_clock_period;

/*
 * Posix Timer Interface
 */

static struct timespec sgi_clock_offset;
static int sgi_clock_period;

static int sgi_clock_get(clockid_t clockid, struct timespec *tp)
{
	u64 nsec;

	nsec = rtc_time() * sgi_clock_period
			+ sgi_clock_offset.tv_nsec;
	tp->tv_sec = div_long_long_rem(nsec, NSEC_PER_SEC, &tp->tv_nsec)
			+ sgi_clock_offset.tv_sec;
	return 0;
};

static int sgi_clock_set(clockid_t clockid, struct timespec *tp)
{

	u64 nsec;
	u64 rem;

	nsec = rtc_time() * sgi_clock_period;

	sgi_clock_offset.tv_sec = tp->tv_sec - div_long_long_rem(nsec, NSEC_PER_SEC, &rem);

	if (rem <= tp->tv_nsec)
		sgi_clock_offset.tv_nsec = tp->tv_sec - rem;
	else {
		sgi_clock_offset.tv_nsec = tp->tv_sec + NSEC_PER_SEC - rem;
		sgi_clock_offset.tv_sec--;
	}
	return 0;
}

/*
 * Schedule the next periodic interrupt. This function will attempt
 * to schedule a periodic interrupt later if necessary. If the scheduling
 * of an interrupt fails then the time to skip is lengthened
 * exponentially in order to ensure that the next interrupt
 * can be properly scheduled..
 */
static int inline reschedule_periodic_timer(mmtimer_t *x)
{
	int n;
	struct k_itimer *t = x->timer;

	t->it.mmtimer.clock = x->i;
	t->it_overrun--;

	n = 0;
	do {

		t->it.mmtimer.expires += t->it.mmtimer.incr << n;
		t->it_overrun += 1 << n;
		n++;
		if (n > 20)
			return 1;

	} while (!mmtimer_setup(x->i, t->it.mmtimer.expires));

	return 0;
}

/**
 * mmtimer_interrupt - timer interrupt handler
 * @irq: irq received
 * @dev_id: device the irq came from
 * @regs: register state upon receipt of the interrupt
 *
 * Called when one of the comarators matches the counter, This
 * routine will send signals to processes that have requested
 * them.
 *
 * This interrupt is run in an interrupt context
 * by the SHUB. It is therefore safe to locally access SHub
 * registers.
 */
static irqreturn_t
mmtimer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	int i;
	unsigned long expires = 0;
	int result = IRQ_NONE;
	unsigned indx = cpu_to_node(smp_processor_id());

	/*
	 * Do this once for each comparison register
	 */
	for (i = 0; i < NUM_COMPARATORS; i++) {
		mmtimer_t *base = timers[indx] + i;
		/* Make sure this doesn't get reused before tasklet_sched */
		spin_lock(&base->lock);
		if (base->cpu == smp_processor_id()) {
			if (base->timer)
				expires = base->timer->it.mmtimer.expires;
			/* expires test won't work with shared irqs */
			if ((mmtimer_int_pending(i) > 0) ||
				(expires && (expires < rtc_time()))) {
				mmtimer_clr_int_pending(i);
				tasklet_schedule(&base->tasklet);
				result = IRQ_HANDLED;
			}
		}
		spin_unlock(&base->lock);
		expires = 0;
	}
	return result;
}

void mmtimer_tasklet(unsigned long data) {
	mmtimer_t *x = (mmtimer_t *)data;
	struct k_itimer *t = x->timer;
	unsigned long flags;

	if (t == NULL)
		return;

	/* Send signal and deal with periodic signals */
	spin_lock_irqsave(&t->it_lock, flags);
	spin_lock(&x->lock);
	/* If timer was deleted between interrupt and here, leave */
	if (t != x->timer)
		goto out;
	t->it_overrun = 0;

	if (posix_timer_event(t, 0) != 0) {

		// printk(KERN_WARNING "mmtimer: cannot deliver signal.\n");

		t->it_overrun++;
	}
	if(t->it.mmtimer.incr) {
		/* Periodic timer */
		if (reschedule_periodic_timer(x)) {
			printk(KERN_WARNING "mmtimer: unable to reschedule\n");
			x->timer = NULL;
		}
	} else {
		/* Ensure we don't false trigger in mmtimer_interrupt */
		t->it.mmtimer.expires = 0;
	}
	t->it_overrun_last = t->it_overrun;
out:
	spin_unlock(&x->lock);
	spin_unlock_irqrestore(&t->it_lock, flags);
}

static int sgi_timer_create(struct k_itimer *timer)
{
	/* Insure that a newly created timer is off */
	timer->it.mmtimer.clock = TIMER_OFF;
	return 0;
}

/* This does not really delete a timer. It just insures
 * that the timer is not active
 *
 * Assumption: it_lock is already held with irq's disabled
 */
static int sgi_timer_del(struct k_itimer *timr)
{
	int i = timr->it.mmtimer.clock;
	cnodeid_t nodeid = timr->it.mmtimer.node;
	mmtimer_t *t = timers[nodeid] + i;
	unsigned long irqflags;

	if (i != TIMER_OFF) {
		spin_lock_irqsave(&t->lock, irqflags);
		mmtimer_disable_int(cnodeid_to_nasid(nodeid),i);
		t->timer = NULL;
		timr->it.mmtimer.clock = TIMER_OFF;
		timr->it.mmtimer.expires = 0;
		spin_unlock_irqrestore(&t->lock, irqflags);
	}
	return 0;
}

#define timespec_to_ns(x) ((x).tv_nsec + (x).tv_sec * NSEC_PER_SEC)
#define ns_to_timespec(ts, nsec) (ts).tv_sec = div_long_long_rem(nsec, NSEC_PER_SEC, &(ts).tv_nsec)

/* Assumption: it_lock is already held with irq's disabled */
static void sgi_timer_get(struct k_itimer *timr, struct itimerspec *cur_setting)
{

	if (timr->it.mmtimer.clock == TIMER_OFF) {
		cur_setting->it_interval.tv_nsec = 0;
		cur_setting->it_interval.tv_sec = 0;
		cur_setting->it_value.tv_nsec = 0;
		cur_setting->it_value.tv_sec =0;
		return;
	}

	ns_to_timespec(cur_setting->it_interval, timr->it.mmtimer.incr * sgi_clock_period);
	ns_to_timespec(cur_setting->it_value, (timr->it.mmtimer.expires - rtc_time())* sgi_clock_period);
	return;
}


static int sgi_timer_set(struct k_itimer *timr, int flags,
	struct itimerspec * new_setting,
	struct itimerspec * old_setting)
{

	int i;
	unsigned long when, period, irqflags;
	int err = 0;
	cnodeid_t nodeid;
	mmtimer_t *base;

	if (old_setting)
		sgi_timer_get(timr, old_setting);

	sgi_timer_del(timr);
	when = timespec_to_ns(new_setting->it_value);
	period = timespec_to_ns(new_setting->it_interval);

	if (when == 0)
		/* Clear timer */
		return 0;

	if (flags & TIMER_ABSTIME) {
		struct timespec n;
		unsigned long now;

		getnstimeofday(&n);
		now = timespec_to_ns(n);
		if (when > now)
			when -= now;
		else
			/* Fire the timer immediately */
			when = 0;
	}

	/*
	 * Convert to sgi clock period. Need to keep rtc_time() as near as possible
	 * to getnstimeofday() in order to be as faithful as possible to the time
	 * specified.
	 */
	when = (when + sgi_clock_period - 1) / sgi_clock_period + rtc_time();
	period = (period + sgi_clock_period - 1)  / sgi_clock_period;

	/*
	 * We are allocating a local SHub comparator. If we would be moved to another
	 * cpu then another SHub may be local to us. Prohibit that by switching off
	 * preemption.
	 */
	preempt_disable();

	nodeid =  cpu_to_node(smp_processor_id());
retry:
	/* Don't use an allocated timer, or a deleted one that's pending */
	for(i = 0; i< NUM_COMPARATORS; i++) {
		base = timers[nodeid] + i;
		if (!base->timer && !base->tasklet.state) {
			break;
		}
	}

	if (i == NUM_COMPARATORS) {
		preempt_enable();
		return -EBUSY;
	}

	spin_lock_irqsave(&base->lock, irqflags);

	if (base->timer || base->tasklet.state != 0) {
		spin_unlock_irqrestore(&base->lock, irqflags);
		goto retry;
	}
	base->timer = timr;
	base->cpu = smp_processor_id();

	timr->it.mmtimer.clock = i;
	timr->it.mmtimer.node = nodeid;
	timr->it.mmtimer.incr = period;
	timr->it.mmtimer.expires = when;

	if (period == 0) {
		if (!mmtimer_setup(i, when)) {
			mmtimer_disable_int(-1, i);
			posix_timer_event(timr, 0);
			timr->it.mmtimer.expires = 0;
		}
	} else {
		timr->it.mmtimer.expires -= period;
		if (reschedule_periodic_timer(base))
			err = -EINVAL;
	}

	spin_unlock_irqrestore(&base->lock, irqflags);

	preempt_enable();

	return err;
}

static struct k_clock sgi_clock = {
	.res = 0,
	.clock_set = sgi_clock_set,
	.clock_get = sgi_clock_get,
	.timer_create = sgi_timer_create,
	.nsleep = do_posix_clock_nonanosleep,
	.timer_set = sgi_timer_set,
	.timer_del = sgi_timer_del,
	.timer_get = sgi_timer_get
};

/**
 * mmtimer_init - device initialization routine
 *
 * Does initial setup for the mmtimer device.
 */
static int __init mmtimer_init(void)
{
	unsigned i;
	cnodeid_t node, maxn = -1;

	if (!ia64_platform_is("sn2"))
		return 0;

	/*
	 * Sanity check the cycles/sec variable
	 */
	if (sn_rtc_cycles_per_second < 100000) {
		printk(KERN_ERR "%s: unable to determine clock frequency\n",
		       MMTIMER_NAME);
		return -1;
	}

	mmtimer_femtoperiod = ((unsigned long)1E15 + sn_rtc_cycles_per_second /
			       2) / sn_rtc_cycles_per_second;

	if (request_irq(SGI_MMTIMER_VECTOR, mmtimer_interrupt, SA_PERCPU_IRQ, MMTIMER_NAME, NULL)) {
		printk(KERN_WARNING "%s: unable to allocate interrupt.",
			MMTIMER_NAME);
		return -1;
	}

	strcpy(mmtimer_miscdev.devfs_name, MMTIMER_NAME);
	if (misc_register(&mmtimer_miscdev)) {
		printk(KERN_ERR "%s: failed to register device\n",
		       MMTIMER_NAME);
		return -1;
	}

	/* Get max numbered node, calculate slots needed */
	for_each_online_node(node) {
		maxn = node;
	}
	maxn++;

	/* Allocate list of node ptrs to mmtimer_t's */
	timers = kmalloc(sizeof(mmtimer_t *)*maxn, GFP_KERNEL);
	if (timers == NULL) {
		printk(KERN_ERR "%s: failed to allocate memory for device\n",
				MMTIMER_NAME);
		return -1;
	}

	/* Allocate mmtimer_t's for each online node */
	for_each_online_node(node) {
		timers[node] = kmalloc_node(sizeof(mmtimer_t)*NUM_COMPARATORS, GFP_KERNEL, node);
		if (timers[node] == NULL) {
			printk(KERN_ERR "%s: failed to allocate memory for device\n",
				MMTIMER_NAME);
			return -1;
		}
		for (i=0; i< NUM_COMPARATORS; i++) {
			mmtimer_t * base = timers[node] + i;

			spin_lock_init(&base->lock);
			base->timer = NULL;
			base->cpu = 0;
			base->i = i;
			tasklet_init(&base->tasklet, mmtimer_tasklet,
				(unsigned long) (base));
		}
	}

	sgi_clock_period = sgi_clock.res = NSEC_PER_SEC / sn_rtc_cycles_per_second;
	register_posix_clock(CLOCK_SGI_CYCLE, &sgi_clock);

	printk(KERN_INFO "%s: v%s, %ld MHz\n", MMTIMER_DESC, MMTIMER_VERSION,
	       sn_rtc_cycles_per_second/(unsigned long)1E6);

	return 0;
}

module_init(mmtimer_init);

