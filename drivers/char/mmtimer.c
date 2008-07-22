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
#include <linux/fs.h>
#include <linux/mmtimer.h>
#include <linux/miscdevice.h>
#include <linux/posix-timers.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/math64.h>
#include <linux/smp_lock.h>

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

static long mmtimer_ioctl(struct file *file, unsigned int cmd,
						unsigned long arg);
static int mmtimer_mmap(struct file *file, struct vm_area_struct *vma);

/*
 * Period in femtoseconds (10^-15 s)
 */
static unsigned long mmtimer_femtoperiod = 0;

static const struct file_operations mmtimer_fops = {
	.owner = THIS_MODULE,
	.mmap =	mmtimer_mmap,
	.unlocked_ioctl = mmtimer_ioctl,
};

/*
 * We only have comparison registers RTC1-4 currently available per
 * node.  RTC0 is used by SAL.
 */
/* Check for an RTC interrupt pending */
static int mmtimer_int_pending(int comparator)
{
	if (HUB_L((unsigned long *)LOCAL_MMR_ADDR(SH_EVENT_OCCURRED)) &
			SH_EVENT_OCCURRED_RTC1_INT_MASK << comparator)
		return 1;
	else
		return 0;
}

/* Clear the RTC interrupt pending bit */
static void mmtimer_clr_int_pending(int comparator)
{
	HUB_S((u64 *)LOCAL_MMR_ADDR(SH_EVENT_OCCURRED_ALIAS),
		SH_EVENT_OCCURRED_RTC1_INT_MASK << comparator);
}

/* Setup timer on comparator RTC1 */
static void mmtimer_setup_int_0(int cpu, u64 expires)
{
	u64 val;

	/* Disable interrupt */
	HUB_S((u64 *)LOCAL_MMR_ADDR(SH_RTC1_INT_ENABLE), 0UL);

	/* Initialize comparator value */
	HUB_S((u64 *)LOCAL_MMR_ADDR(SH_INT_CMPB), -1L);

	/* Clear pending bit */
	mmtimer_clr_int_pending(0);

	val = ((u64)SGI_MMTIMER_VECTOR << SH_RTC1_INT_CONFIG_IDX_SHFT) |
		((u64)cpu_physical_id(cpu) <<
			SH_RTC1_INT_CONFIG_PID_SHFT);

	/* Set configuration */
	HUB_S((u64 *)LOCAL_MMR_ADDR(SH_RTC1_INT_CONFIG), val);

	/* Enable RTC interrupts */
	HUB_S((u64 *)LOCAL_MMR_ADDR(SH_RTC1_INT_ENABLE), 1UL);

	/* Initialize comparator value */
	HUB_S((u64 *)LOCAL_MMR_ADDR(SH_INT_CMPB), expires);


}

/* Setup timer on comparator RTC2 */
static void mmtimer_setup_int_1(int cpu, u64 expires)
{
	u64 val;

	HUB_S((u64 *)LOCAL_MMR_ADDR(SH_RTC2_INT_ENABLE), 0UL);

	HUB_S((u64 *)LOCAL_MMR_ADDR(SH_INT_CMPC), -1L);

	mmtimer_clr_int_pending(1);

	val = ((u64)SGI_MMTIMER_VECTOR << SH_RTC2_INT_CONFIG_IDX_SHFT) |
		((u64)cpu_physical_id(cpu) <<
			SH_RTC2_INT_CONFIG_PID_SHFT);

	HUB_S((u64 *)LOCAL_MMR_ADDR(SH_RTC2_INT_CONFIG), val);

	HUB_S((u64 *)LOCAL_MMR_ADDR(SH_RTC2_INT_ENABLE), 1UL);

	HUB_S((u64 *)LOCAL_MMR_ADDR(SH_INT_CMPC), expires);
}

/* Setup timer on comparator RTC3 */
static void mmtimer_setup_int_2(int cpu, u64 expires)
{
	u64 val;

	HUB_S((u64 *)LOCAL_MMR_ADDR(SH_RTC3_INT_ENABLE), 0UL);

	HUB_S((u64 *)LOCAL_MMR_ADDR(SH_INT_CMPD), -1L);

	mmtimer_clr_int_pending(2);

	val = ((u64)SGI_MMTIMER_VECTOR << SH_RTC3_INT_CONFIG_IDX_SHFT) |
		((u64)cpu_physical_id(cpu) <<
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
static int mmtimer_setup(int cpu, int comparator, unsigned long expires)
{

	switch (comparator) {
	case 0:
		mmtimer_setup_int_0(cpu, expires);
		break;
	case 1:
		mmtimer_setup_int_1(cpu, expires);
		break;
	case 2:
		mmtimer_setup_int_2(cpu, expires);
		break;
	}
	/* We might've missed our expiration time */
	if (rtc_time() <= expires)
		return 1;

	/*
	 * If an interrupt is already pending then its okay
	 * if not then we failed
	 */
	return mmtimer_int_pending(comparator);
}

static int mmtimer_disable_int(long nasid, int comparator)
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

#define COMPARATOR	1		/* The comparator to use */

#define TIMER_OFF	0xbadcabLL	/* Timer is not setup */
#define TIMER_SET	0		/* Comparator is set for this timer */

/* There is one of these for each timer */
struct mmtimer {
	struct rb_node list;
	struct k_itimer *timer;
	int cpu;
};

struct mmtimer_node {
	spinlock_t lock ____cacheline_aligned;
	struct rb_root timer_head;
	struct rb_node *next;
	struct tasklet_struct tasklet;
};
static struct mmtimer_node *timers;


/*
 * Add a new mmtimer struct to the node's mmtimer list.
 * This function assumes the struct mmtimer_node is locked.
 */
static void mmtimer_add_list(struct mmtimer *n)
{
	int nodeid = n->timer->it.mmtimer.node;
	unsigned long expires = n->timer->it.mmtimer.expires;
	struct rb_node **link = &timers[nodeid].timer_head.rb_node;
	struct rb_node *parent = NULL;
	struct mmtimer *x;

	/*
	 * Find the right place in the rbtree:
	 */
	while (*link) {
		parent = *link;
		x = rb_entry(parent, struct mmtimer, list);

		if (expires < x->timer->it.mmtimer.expires)
			link = &(*link)->rb_left;
		else
			link = &(*link)->rb_right;
	}

	/*
	 * Insert the timer to the rbtree and check whether it
	 * replaces the first pending timer
	 */
	rb_link_node(&n->list, parent, link);
	rb_insert_color(&n->list, &timers[nodeid].timer_head);

	if (!timers[nodeid].next || expires < rb_entry(timers[nodeid].next,
			struct mmtimer, list)->timer->it.mmtimer.expires)
		timers[nodeid].next = &n->list;
}

/*
 * Set the comparator for the next timer.
 * This function assumes the struct mmtimer_node is locked.
 */
static void mmtimer_set_next_timer(int nodeid)
{
	struct mmtimer_node *n = &timers[nodeid];
	struct mmtimer *x;
	struct k_itimer *t;
	int o;

restart:
	if (n->next == NULL)
		return;

	x = rb_entry(n->next, struct mmtimer, list);
	t = x->timer;
	if (!t->it.mmtimer.incr) {
		/* Not an interval timer */
		if (!mmtimer_setup(x->cpu, COMPARATOR,
					t->it.mmtimer.expires)) {
			/* Late setup, fire now */
			tasklet_schedule(&n->tasklet);
		}
		return;
	}

	/* Interval timer */
	o = 0;
	while (!mmtimer_setup(x->cpu, COMPARATOR, t->it.mmtimer.expires)) {
		unsigned long e, e1;
		struct rb_node *next;
		t->it.mmtimer.expires += t->it.mmtimer.incr << o;
		t->it_overrun += 1 << o;
		o++;
		if (o > 20) {
			printk(KERN_ALERT "mmtimer: cannot reschedule timer\n");
			t->it.mmtimer.clock = TIMER_OFF;
			n->next = rb_next(&x->list);
			rb_erase(&x->list, &n->timer_head);
			kfree(x);
			goto restart;
		}

		e = t->it.mmtimer.expires;
		next = rb_next(&x->list);

		if (next == NULL)
			continue;

		e1 = rb_entry(next, struct mmtimer, list)->
			timer->it.mmtimer.expires;
		if (e > e1) {
			n->next = next;
			rb_erase(&x->list, &n->timer_head);
			mmtimer_add_list(x);
			goto restart;
		}
	}
}

/**
 * mmtimer_ioctl - ioctl interface for /dev/mmtimer
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
static long mmtimer_ioctl(struct file *file, unsigned int cmd,
						unsigned long arg)
{
	int ret = 0;

	lock_kernel();

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
			ret = -EFAULT;
		break;

	case MMTIMER_GETFREQ: /* frequency in Hz */
		if(copy_to_user((unsigned long __user *)arg,
				&sn_rtc_cycles_per_second,
				sizeof(unsigned long)))
			ret = -EFAULT;
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
			ret = -EFAULT;
		break;
	default:
		ret = -ENOTTY;
		break;
	}
	unlock_kernel();
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
	*tp = ns_to_timespec(nsec);
	tp->tv_sec += sgi_clock_offset.tv_sec;
	return 0;
};

static int sgi_clock_set(clockid_t clockid, struct timespec *tp)
{

	u64 nsec;
	u32 rem;

	nsec = rtc_time() * sgi_clock_period;

	sgi_clock_offset.tv_sec = tp->tv_sec - div_u64_rem(nsec, NSEC_PER_SEC, &rem);

	if (rem <= tp->tv_nsec)
		sgi_clock_offset.tv_nsec = tp->tv_sec - rem;
	else {
		sgi_clock_offset.tv_nsec = tp->tv_sec + NSEC_PER_SEC - rem;
		sgi_clock_offset.tv_sec--;
	}
	return 0;
}

/**
 * mmtimer_interrupt - timer interrupt handler
 * @irq: irq received
 * @dev_id: device the irq came from
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
mmtimer_interrupt(int irq, void *dev_id)
{
	unsigned long expires = 0;
	int result = IRQ_NONE;
	unsigned indx = cpu_to_node(smp_processor_id());
	struct mmtimer *base;

	spin_lock(&timers[indx].lock);
	base = rb_entry(timers[indx].next, struct mmtimer, list);
	if (base == NULL) {
		spin_unlock(&timers[indx].lock);
		return result;
	}

	if (base->cpu == smp_processor_id()) {
		if (base->timer)
			expires = base->timer->it.mmtimer.expires;
		/* expires test won't work with shared irqs */
		if ((mmtimer_int_pending(COMPARATOR) > 0) ||
			(expires && (expires <= rtc_time()))) {
			mmtimer_clr_int_pending(COMPARATOR);
			tasklet_schedule(&timers[indx].tasklet);
			result = IRQ_HANDLED;
		}
	}
	spin_unlock(&timers[indx].lock);
	return result;
}

static void mmtimer_tasklet(unsigned long data)
{
	int nodeid = data;
	struct mmtimer_node *mn = &timers[nodeid];
	struct mmtimer *x = rb_entry(mn->next, struct mmtimer, list);
	struct k_itimer *t;
	unsigned long flags;

	/* Send signal and deal with periodic signals */
	spin_lock_irqsave(&mn->lock, flags);
	if (!mn->next)
		goto out;

	x = rb_entry(mn->next, struct mmtimer, list);
	t = x->timer;

	if (t->it.mmtimer.clock == TIMER_OFF)
		goto out;

	t->it_overrun = 0;

	mn->next = rb_next(&x->list);
	rb_erase(&x->list, &mn->timer_head);

	if (posix_timer_event(t, 0) != 0)
		t->it_overrun++;

	if(t->it.mmtimer.incr) {
		t->it.mmtimer.expires += t->it.mmtimer.incr;
		mmtimer_add_list(x);
	} else {
		/* Ensure we don't false trigger in mmtimer_interrupt */
		t->it.mmtimer.clock = TIMER_OFF;
		t->it.mmtimer.expires = 0;
		kfree(x);
	}
	/* Set comparator for next timer, if there is one */
	mmtimer_set_next_timer(nodeid);

	t->it_overrun_last = t->it_overrun;
out:
	spin_unlock_irqrestore(&mn->lock, flags);
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
	cnodeid_t nodeid = timr->it.mmtimer.node;
	unsigned long irqflags;

	spin_lock_irqsave(&timers[nodeid].lock, irqflags);
	if (timr->it.mmtimer.clock != TIMER_OFF) {
		unsigned long expires = timr->it.mmtimer.expires;
		struct rb_node *n = timers[nodeid].timer_head.rb_node;
		struct mmtimer *uninitialized_var(t);
		int r = 0;

		timr->it.mmtimer.clock = TIMER_OFF;
		timr->it.mmtimer.expires = 0;

		while (n) {
			t = rb_entry(n, struct mmtimer, list);
			if (t->timer == timr)
				break;

			if (expires < t->timer->it.mmtimer.expires)
				n = n->rb_left;
			else
				n = n->rb_right;
		}

		if (!n) {
			spin_unlock_irqrestore(&timers[nodeid].lock, irqflags);
			return 0;
		}

		if (timers[nodeid].next == n) {
			timers[nodeid].next = rb_next(n);
			r = 1;
		}

		rb_erase(n, &timers[nodeid].timer_head);
		kfree(t);

		if (r) {
			mmtimer_disable_int(cnodeid_to_nasid(nodeid),
				COMPARATOR);
			mmtimer_set_next_timer(nodeid);
		}
	}
	spin_unlock_irqrestore(&timers[nodeid].lock, irqflags);
	return 0;
}

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

	cur_setting->it_interval = ns_to_timespec(timr->it.mmtimer.incr * sgi_clock_period);
	cur_setting->it_value = ns_to_timespec((timr->it.mmtimer.expires - rtc_time()) * sgi_clock_period);
}


static int sgi_timer_set(struct k_itimer *timr, int flags,
	struct itimerspec * new_setting,
	struct itimerspec * old_setting)
{
	unsigned long when, period, irqflags;
	int err = 0;
	cnodeid_t nodeid;
	struct mmtimer *base;
	struct rb_node *n;

	if (old_setting)
		sgi_timer_get(timr, old_setting);

	sgi_timer_del(timr);
	when = timespec_to_ns(&new_setting->it_value);
	period = timespec_to_ns(&new_setting->it_interval);

	if (when == 0)
		/* Clear timer */
		return 0;

	base = kmalloc(sizeof(struct mmtimer), GFP_KERNEL);
	if (base == NULL)
		return -ENOMEM;

	if (flags & TIMER_ABSTIME) {
		struct timespec n;
		unsigned long now;

		getnstimeofday(&n);
		now = timespec_to_ns(&n);
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

	/* Lock the node timer structure */
	spin_lock_irqsave(&timers[nodeid].lock, irqflags);

	base->timer = timr;
	base->cpu = smp_processor_id();

	timr->it.mmtimer.clock = TIMER_SET;
	timr->it.mmtimer.node = nodeid;
	timr->it.mmtimer.incr = period;
	timr->it.mmtimer.expires = when;

	n = timers[nodeid].next;

	/* Add the new struct mmtimer to node's timer list */
	mmtimer_add_list(base);

	if (timers[nodeid].next == n) {
		/* No need to reprogram comparator for now */
		spin_unlock_irqrestore(&timers[nodeid].lock, irqflags);
		preempt_enable();
		return err;
	}

	/* We need to reprogram the comparator */
	if (n)
		mmtimer_disable_int(cnodeid_to_nasid(nodeid), COMPARATOR);

	mmtimer_set_next_timer(nodeid);

	/* Unlock the node timer structure */
	spin_unlock_irqrestore(&timers[nodeid].lock, irqflags);

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
	cnodeid_t node, maxn = -1;

	if (!ia64_platform_is("sn2"))
		return 0;

	/*
	 * Sanity check the cycles/sec variable
	 */
	if (sn_rtc_cycles_per_second < 100000) {
		printk(KERN_ERR "%s: unable to determine clock frequency\n",
		       MMTIMER_NAME);
		goto out1;
	}

	mmtimer_femtoperiod = ((unsigned long)1E15 + sn_rtc_cycles_per_second /
			       2) / sn_rtc_cycles_per_second;

	if (request_irq(SGI_MMTIMER_VECTOR, mmtimer_interrupt, IRQF_PERCPU, MMTIMER_NAME, NULL)) {
		printk(KERN_WARNING "%s: unable to allocate interrupt.",
			MMTIMER_NAME);
		goto out1;
	}

	if (misc_register(&mmtimer_miscdev)) {
		printk(KERN_ERR "%s: failed to register device\n",
		       MMTIMER_NAME);
		goto out2;
	}

	/* Get max numbered node, calculate slots needed */
	for_each_online_node(node) {
		maxn = node;
	}
	maxn++;

	/* Allocate list of node ptrs to mmtimer_t's */
	timers = kzalloc(sizeof(struct mmtimer_node)*maxn, GFP_KERNEL);
	if (timers == NULL) {
		printk(KERN_ERR "%s: failed to allocate memory for device\n",
				MMTIMER_NAME);
		goto out3;
	}

	/* Initialize struct mmtimer's for each online node */
	for_each_online_node(node) {
		spin_lock_init(&timers[node].lock);
		tasklet_init(&timers[node].tasklet, mmtimer_tasklet,
			(unsigned long) node);
	}

	sgi_clock_period = sgi_clock.res = NSEC_PER_SEC / sn_rtc_cycles_per_second;
	register_posix_clock(CLOCK_SGI_CYCLE, &sgi_clock);

	printk(KERN_INFO "%s: v%s, %ld MHz\n", MMTIMER_DESC, MMTIMER_VERSION,
	       sn_rtc_cycles_per_second/(unsigned long)1E6);

	return 0;

out3:
	kfree(timers);
	misc_deregister(&mmtimer_miscdev);
out2:
	free_irq(SGI_MMTIMER_VECTOR, NULL);
out1:
	return -1;
}

module_init(mmtimer_init);
