/*
 * Copyright (C) 2000 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "linux/kernel.h"
#include "linux/module.h"
#include "linux/unistd.h"
#include "linux/stddef.h"
#include "linux/spinlock.h"
#include "linux/time.h"
#include "linux/sched.h"
#include "linux/interrupt.h"
#include "linux/init.h"
#include "linux/delay.h"
#include "linux/hrtimer.h"
#include "asm/irq.h"
#include "asm/param.h"
#include "asm/current.h"
#include "kern_util.h"
#include "user_util.h"
#include "mode.h"
#include "os.h"

int hz(void)
{
	return(HZ);
}

/*
 * Scheduler clock - returns current time in nanosec units.
 */
unsigned long long sched_clock(void)
{
	return (unsigned long long)jiffies_64 * (1000000000 / HZ);
}

/* Changed at early boot */
int timer_irq_inited = 0;

static int first_tick;
static unsigned long long prev_nsecs;
#ifdef CONFIG_UML_REAL_TIME_CLOCK
static long long delta;   		/* Deviation per interval */
#endif

void timer_irq(union uml_pt_regs *regs)
{
	unsigned long long ticks = 0;

	if(!timer_irq_inited){
		/* This is to ensure that ticks don't pile up when
		 * the timer handler is suspended */
		first_tick = 0;
		return;
	}

	if(first_tick){
#ifdef CONFIG_UML_REAL_TIME_CLOCK
		/* We've had 1 tick */
		unsigned long long nsecs = os_nsecs();

		delta += nsecs - prev_nsecs;
		prev_nsecs = nsecs;

		/* Protect against the host clock being set backwards */
		if(delta < 0)
			delta = 0;

		ticks += (delta * HZ) / BILLION;
		delta -= (ticks * BILLION) / HZ;
#else
		ticks = 1;
#endif
	}
	else {
		prev_nsecs = os_nsecs();
		first_tick = 1;
	}

	while(ticks > 0){
		do_IRQ(TIMER_IRQ, regs);
		ticks--;
	}
}


void time_init_kern(void)
{
	long long nsecs;

	nsecs = os_nsecs();
	set_normalized_timespec(&wall_to_monotonic, -nsecs / BILLION,
				-nsecs % BILLION);
}

void do_boot_timer_handler(struct sigcontext * sc)
{
	struct pt_regs regs;

	CHOOSE_MODE((void) (UPT_SC(&regs.regs) = sc),
		    (void) (regs.regs.skas.is_user = 0));
	do_timer(&regs);
}

static DEFINE_SPINLOCK(timer_spinlock);

static unsigned long long local_offset = 0;

static inline unsigned long long get_time(void)
{
	unsigned long long nsecs;
	unsigned long flags;

	spin_lock_irqsave(&timer_spinlock, flags);
	nsecs = os_nsecs();
	nsecs += local_offset;
	spin_unlock_irqrestore(&timer_spinlock, flags);

	return nsecs;
}

irqreturn_t um_timer(int irq, void *dev, struct pt_regs *regs)
{
	unsigned long long nsecs;
	unsigned long flags;

	do_timer(regs);

	write_seqlock_irqsave(&xtime_lock, flags);
	nsecs = get_time() + local_offset;
	xtime.tv_sec = nsecs / NSEC_PER_SEC;
	xtime.tv_nsec = nsecs - xtime.tv_sec * NSEC_PER_SEC;
	write_sequnlock_irqrestore(&xtime_lock, flags);

	return(IRQ_HANDLED);
}

long um_time(int __user *tloc)
{
	long ret = get_time() / NSEC_PER_SEC;

	if((tloc != NULL) && put_user(ret, tloc))
		return -EFAULT;

	return ret;
}

void do_gettimeofday(struct timeval *tv)
{
	unsigned long long nsecs = get_time();

	tv->tv_sec = nsecs / NSEC_PER_SEC;
	/* Careful about calculations here - this was originally done as
	 * (nsecs - tv->tv_sec * NSEC_PER_SEC) / NSEC_PER_USEC
	 * which gave bogus (> 1000000) values.  Dunno why, suspect gcc
	 * (4.0.0) miscompiled it, or there's a subtle 64/32-bit conversion
	 * problem that I missed.
	 */
	nsecs -= tv->tv_sec * NSEC_PER_SEC;
	tv->tv_usec = (unsigned long) nsecs / NSEC_PER_USEC;
}

static inline void set_time(unsigned long long nsecs)
{
	unsigned long long now;
	unsigned long flags;

	spin_lock_irqsave(&timer_spinlock, flags);
	now = os_nsecs();
	local_offset = nsecs - now;
	spin_unlock_irqrestore(&timer_spinlock, flags);

	clock_was_set();
}

long um_stime(int __user *tptr)
{
	int value;

	if (get_user(value, tptr))
                return -EFAULT;

	set_time((unsigned long long) value * NSEC_PER_SEC);

	return 0;
}

int do_settimeofday(struct timespec *tv)
{
	set_time((unsigned long long) tv->tv_sec * NSEC_PER_SEC + tv->tv_nsec);

	return 0;
}

void timer_handler(int sig, union uml_pt_regs *regs)
{
	local_irq_disable();
	irq_enter();
	update_process_times(CHOOSE_MODE(
	                     (UPT_SC(regs) && user_context(UPT_SP(regs))),
			     (regs)->skas.is_user));
	irq_exit();
	local_irq_enable();
	if(current_thread->cpu == 0)
		timer_irq(regs);
}

int __init timer_init(void)
{
	int err;

	user_time_init();
	err = request_irq(TIMER_IRQ, um_timer, SA_INTERRUPT, "timer", NULL);
	if(err != 0)
		printk(KERN_ERR "timer_init : request_irq failed - "
		       "errno = %d\n", -err);
	timer_irq_inited = 1;
	return(0);
}

arch_initcall(timer_init);
