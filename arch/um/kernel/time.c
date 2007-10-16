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

#ifdef CONFIG_UML_REAL_TIME_CLOCK
static unsigned long long prev_nsecs[NR_CPUS];
static long long delta[NR_CPUS];		/* Deviation per interval */
#endif

void timer_irq(union uml_pt_regs *regs)
{
	unsigned long long ticks = 0;
#ifdef CONFIG_UML_REAL_TIME_CLOCK
	int c = cpu();
	if(prev_nsecs[c]){
		/* We've had 1 tick */
		unsigned long long nsecs = os_nsecs();

		delta[c] += nsecs - prev_nsecs[c];
		prev_nsecs[c] = nsecs;

		/* Protect against the host clock being set backwards */
		if(delta[c] < 0)
			delta[c] = 0;

		ticks += (delta[c] * HZ) / BILLION;
		delta[c] -= (ticks * BILLION) / HZ;
	}
	else prev_nsecs[c] = os_nsecs();
#else
	ticks = 1;
#endif
	while(ticks > 0){
		do_IRQ(TIMER_IRQ, regs);
		ticks--;
	}
}

/* Protects local_offset */
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

irqreturn_t um_timer(int irq, void *dev)
{
	unsigned long long nsecs;
	unsigned long flags;

	write_seqlock_irqsave(&xtime_lock, flags);

	do_timer(1);

#ifdef CONFIG_UML_REAL_TIME_CLOCK
	nsecs = get_time();
#else
	nsecs = (unsigned long long) xtime.tv_sec * BILLION + xtime.tv_nsec +
		BILLION / HZ;
#endif
	xtime.tv_sec = nsecs / NSEC_PER_SEC;
	xtime.tv_nsec = nsecs - xtime.tv_sec * NSEC_PER_SEC;

	write_sequnlock_irqrestore(&xtime_lock, flags);

	return IRQ_HANDLED;
}

static void register_timer(void)
{
	int err;

	err = request_irq(TIMER_IRQ, um_timer, IRQF_DISABLED, "timer", NULL);
	if(err != 0)
		printk(KERN_ERR "register_timer : request_irq failed - "
		       "errno = %d\n", -err);

	err = set_interval(1);
	if(err != 0)
		printk(KERN_ERR "register_timer : set_interval failed - "
		       "errno = %d\n", -err);
}

extern void (*late_time_init)(void);

void time_init(void)
{
	long long nsecs;

	nsecs = os_nsecs();
	set_normalized_timespec(&wall_to_monotonic, -nsecs / BILLION,
				-nsecs % BILLION);
	set_normalized_timespec(&xtime, nsecs / BILLION, nsecs % BILLION);
	late_time_init = register_timer;
}

void do_gettimeofday(struct timeval *tv)
{
#ifdef CONFIG_UML_REAL_TIME_CLOCK
	unsigned long long nsecs = get_time();
#else
	unsigned long long nsecs = (unsigned long long) xtime.tv_sec * BILLION +
		xtime.tv_nsec;
#endif
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

int do_settimeofday(struct timespec *tv)
{
	set_time((unsigned long long) tv->tv_sec * NSEC_PER_SEC + tv->tv_nsec);

	return 0;
}

void timer_handler(int sig, union uml_pt_regs *regs)
{
	if(current_thread->cpu == 0)
		timer_irq(regs);
	local_irq_disable();
	irq_enter();
	update_process_times((regs)->skas.is_user);
	irq_exit();
	local_irq_enable();
}
