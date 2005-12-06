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
#include "asm/irq.h"
#include "asm/param.h"
#include "asm/current.h"
#include "kern_util.h"
#include "user_util.h"
#include "time_user.h"
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
static unsigned long long prev_usecs;
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
		unsigned long long usecs = os_usecs();

		delta += usecs - prev_usecs;
		prev_usecs = usecs;

		/* Protect against the host clock being set backwards */
		if(delta < 0)
			delta = 0;

		ticks += (delta * HZ) / MILLION;
		delta -= (ticks * MILLION) / HZ;
#else
		ticks = 1;
#endif
	}
	else {
		prev_usecs = os_usecs();
		first_tick = 1;
	}

	while(ticks > 0){
		do_IRQ(TIMER_IRQ, regs);
		ticks--;
	}
}

void boot_timer_handler(int sig)
{
	struct pt_regs regs;

	CHOOSE_MODE((void) 
		    (UPT_SC(&regs.regs) = (struct sigcontext *) (&sig + 1)),
		    (void) (regs.regs.skas.is_user = 0));
	do_timer(&regs);
}

irqreturn_t um_timer(int irq, void *dev, struct pt_regs *regs)
{
	unsigned long flags;

	do_timer(regs);
	write_seqlock_irqsave(&xtime_lock, flags);
	timer();
	write_sequnlock_irqrestore(&xtime_lock, flags);
	return(IRQ_HANDLED);
}

long um_time(int __user *tloc)
{
	struct timeval now;

	do_gettimeofday(&now);
	if (tloc) {
 		if (put_user(now.tv_sec, tloc))
			now.tv_sec = -EFAULT;
	}
	return now.tv_sec;
}

long um_stime(int __user *tptr)
{
	int value;
	struct timespec new;

	if (get_user(value, tptr))
                return -EFAULT;
	new.tv_sec = value;
	new.tv_nsec = 0;
	do_settimeofday(&new);
	return 0;
}

void timer_handler(int sig, union uml_pt_regs *regs)
{
	local_irq_disable();
	irq_enter();
	update_process_times(CHOOSE_MODE(user_context(UPT_SP(regs)),
					 (regs)->skas.is_user));
	irq_exit();
	local_irq_enable();
	if(current_thread->cpu == 0)
		timer_irq(regs);
}

static DEFINE_SPINLOCK(timer_spinlock);

unsigned long time_lock(void)
{
	unsigned long flags;

	spin_lock_irqsave(&timer_spinlock, flags);
	return(flags);
}

void time_unlock(unsigned long flags)
{
	spin_unlock_irqrestore(&timer_spinlock, flags);
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

__initcall(timer_init);

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
