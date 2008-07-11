/*
 * linux/arch/ia64/kernel/time.c
 *
 * Copyright (C) 1998-2003 Hewlett-Packard Co
 *	Stephane Eranian <eranian@hpl.hp.com>
 *	David Mosberger <davidm@hpl.hp.com>
 * Copyright (C) 1999 Don Dugger <don.dugger@intel.com>
 * Copyright (C) 1999-2000 VA Linux Systems
 * Copyright (C) 1999-2000 Walt Drummond <drummond@valinux.com>
 */

#include <linux/cpu.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/profile.h>
#include <linux/sched.h>
#include <linux/time.h>
#include <linux/interrupt.h>
#include <linux/efi.h>
#include <linux/timex.h>
#include <linux/clocksource.h>

#include <asm/machvec.h>
#include <asm/delay.h>
#include <asm/hw_irq.h>
#include <asm/ptrace.h>
#include <asm/sal.h>
#include <asm/sections.h>
#include <asm/system.h>

#include "fsyscall_gtod_data.h"

static cycle_t itc_get_cycles(void);

struct fsyscall_gtod_data_t fsyscall_gtod_data = {
	.lock = SEQLOCK_UNLOCKED,
};

struct itc_jitter_data_t itc_jitter_data;

volatile int time_keeper_id = 0; /* smp_processor_id() of time-keeper */

#ifdef CONFIG_IA64_DEBUG_IRQ

unsigned long last_cli_ip;
EXPORT_SYMBOL(last_cli_ip);

#endif

static struct clocksource clocksource_itc = {
	.name           = "itc",
	.rating         = 350,
	.read           = itc_get_cycles,
	.mask           = CLOCKSOURCE_MASK(64),
	.mult           = 0, /*to be calculated*/
	.shift          = 16,
	.flags          = CLOCK_SOURCE_IS_CONTINUOUS,
};
static struct clocksource *itc_clocksource;

#ifdef CONFIG_VIRT_CPU_ACCOUNTING

#include <linux/kernel_stat.h>

extern cputime_t cycle_to_cputime(u64 cyc);

/*
 * Called from the context switch with interrupts disabled, to charge all
 * accumulated times to the current process, and to prepare accounting on
 * the next process.
 */
void ia64_account_on_switch(struct task_struct *prev, struct task_struct *next)
{
	struct thread_info *pi = task_thread_info(prev);
	struct thread_info *ni = task_thread_info(next);
	cputime_t delta_stime, delta_utime;
	__u64 now;

	now = ia64_get_itc();

	delta_stime = cycle_to_cputime(pi->ac_stime + (now - pi->ac_stamp));
	account_system_time(prev, 0, delta_stime);
	account_system_time_scaled(prev, delta_stime);

	if (pi->ac_utime) {
		delta_utime = cycle_to_cputime(pi->ac_utime);
		account_user_time(prev, delta_utime);
		account_user_time_scaled(prev, delta_utime);
	}

	pi->ac_stamp = ni->ac_stamp = now;
	ni->ac_stime = ni->ac_utime = 0;
}

/*
 * Account time for a transition between system, hard irq or soft irq state.
 * Note that this function is called with interrupts enabled.
 */
void account_system_vtime(struct task_struct *tsk)
{
	struct thread_info *ti = task_thread_info(tsk);
	unsigned long flags;
	cputime_t delta_stime;
	__u64 now;

	local_irq_save(flags);

	now = ia64_get_itc();

	delta_stime = cycle_to_cputime(ti->ac_stime + (now - ti->ac_stamp));
	account_system_time(tsk, 0, delta_stime);
	account_system_time_scaled(tsk, delta_stime);
	ti->ac_stime = 0;

	ti->ac_stamp = now;

	local_irq_restore(flags);
}
EXPORT_SYMBOL_GPL(account_system_vtime);

/*
 * Called from the timer interrupt handler to charge accumulated user time
 * to the current process.  Must be called with interrupts disabled.
 */
void account_process_tick(struct task_struct *p, int user_tick)
{
	struct thread_info *ti = task_thread_info(p);
	cputime_t delta_utime;

	if (ti->ac_utime) {
		delta_utime = cycle_to_cputime(ti->ac_utime);
		account_user_time(p, delta_utime);
		account_user_time_scaled(p, delta_utime);
		ti->ac_utime = 0;
	}
}

#endif /* CONFIG_VIRT_CPU_ACCOUNTING */

static irqreturn_t
timer_interrupt (int irq, void *dev_id)
{
	unsigned long new_itm;

	if (unlikely(cpu_is_offline(smp_processor_id()))) {
		return IRQ_HANDLED;
	}

	platform_timer_interrupt(irq, dev_id);

	new_itm = local_cpu_data->itm_next;

	if (!time_after(ia64_get_itc(), new_itm))
		printk(KERN_ERR "Oops: timer tick before it's due (itc=%lx,itm=%lx)\n",
		       ia64_get_itc(), new_itm);

	profile_tick(CPU_PROFILING);

	while (1) {
		update_process_times(user_mode(get_irq_regs()));

		new_itm += local_cpu_data->itm_delta;

		if (smp_processor_id() == time_keeper_id) {
			/*
			 * Here we are in the timer irq handler. We have irqs locally
			 * disabled, but we don't know if the timer_bh is running on
			 * another CPU. We need to avoid to SMP race by acquiring the
			 * xtime_lock.
			 */
			write_seqlock(&xtime_lock);
			do_timer(1);
			local_cpu_data->itm_next = new_itm;
			write_sequnlock(&xtime_lock);
		} else
			local_cpu_data->itm_next = new_itm;

		if (time_after(new_itm, ia64_get_itc()))
			break;

		/*
		 * Allow IPIs to interrupt the timer loop.
		 */
		local_irq_enable();
		local_irq_disable();
	}

	do {
		/*
		 * If we're too close to the next clock tick for
		 * comfort, we increase the safety margin by
		 * intentionally dropping the next tick(s).  We do NOT
		 * update itm.next because that would force us to call
		 * do_timer() which in turn would let our clock run
		 * too fast (with the potentially devastating effect
		 * of losing monotony of time).
		 */
		while (!time_after(new_itm, ia64_get_itc() + local_cpu_data->itm_delta/2))
			new_itm += local_cpu_data->itm_delta;
		ia64_set_itm(new_itm);
		/* double check, in case we got hit by a (slow) PMI: */
	} while (time_after_eq(ia64_get_itc(), new_itm));
	return IRQ_HANDLED;
}

/*
 * Encapsulate access to the itm structure for SMP.
 */
void
ia64_cpu_local_tick (void)
{
	int cpu = smp_processor_id();
	unsigned long shift = 0, delta;

	/* arrange for the cycle counter to generate a timer interrupt: */
	ia64_set_itv(IA64_TIMER_VECTOR);

	delta = local_cpu_data->itm_delta;
	/*
	 * Stagger the timer tick for each CPU so they don't occur all at (almost) the
	 * same time:
	 */
	if (cpu) {
		unsigned long hi = 1UL << ia64_fls(cpu);
		shift = (2*(cpu - hi) + 1) * delta/hi/2;
	}
	local_cpu_data->itm_next = ia64_get_itc() + delta + shift;
	ia64_set_itm(local_cpu_data->itm_next);
}

static int nojitter;

static int __init nojitter_setup(char *str)
{
	nojitter = 1;
	printk("Jitter checking for ITC timers disabled\n");
	return 1;
}

__setup("nojitter", nojitter_setup);


void __devinit
ia64_init_itm (void)
{
	unsigned long platform_base_freq, itc_freq;
	struct pal_freq_ratio itc_ratio, proc_ratio;
	long status, platform_base_drift, itc_drift;

	/*
	 * According to SAL v2.6, we need to use a SAL call to determine the platform base
	 * frequency and then a PAL call to determine the frequency ratio between the ITC
	 * and the base frequency.
	 */
	status = ia64_sal_freq_base(SAL_FREQ_BASE_PLATFORM,
				    &platform_base_freq, &platform_base_drift);
	if (status != 0) {
		printk(KERN_ERR "SAL_FREQ_BASE_PLATFORM failed: %s\n", ia64_sal_strerror(status));
	} else {
		status = ia64_pal_freq_ratios(&proc_ratio, NULL, &itc_ratio);
		if (status != 0)
			printk(KERN_ERR "PAL_FREQ_RATIOS failed with status=%ld\n", status);
	}
	if (status != 0) {
		/* invent "random" values */
		printk(KERN_ERR
		       "SAL/PAL failed to obtain frequency info---inventing reasonable values\n");
		platform_base_freq = 100000000;
		platform_base_drift = -1;	/* no drift info */
		itc_ratio.num = 3;
		itc_ratio.den = 1;
	}
	if (platform_base_freq < 40000000) {
		printk(KERN_ERR "Platform base frequency %lu bogus---resetting to 75MHz!\n",
		       platform_base_freq);
		platform_base_freq = 75000000;
		platform_base_drift = -1;
	}
	if (!proc_ratio.den)
		proc_ratio.den = 1;	/* avoid division by zero */
	if (!itc_ratio.den)
		itc_ratio.den = 1;	/* avoid division by zero */

	itc_freq = (platform_base_freq*itc_ratio.num)/itc_ratio.den;

	local_cpu_data->itm_delta = (itc_freq + HZ/2) / HZ;
	printk(KERN_DEBUG "CPU %d: base freq=%lu.%03luMHz, ITC ratio=%u/%u, "
	       "ITC freq=%lu.%03luMHz", smp_processor_id(),
	       platform_base_freq / 1000000, (platform_base_freq / 1000) % 1000,
	       itc_ratio.num, itc_ratio.den, itc_freq / 1000000, (itc_freq / 1000) % 1000);

	if (platform_base_drift != -1) {
		itc_drift = platform_base_drift*itc_ratio.num/itc_ratio.den;
		printk("+/-%ldppm\n", itc_drift);
	} else {
		itc_drift = -1;
		printk("\n");
	}

	local_cpu_data->proc_freq = (platform_base_freq*proc_ratio.num)/proc_ratio.den;
	local_cpu_data->itc_freq = itc_freq;
	local_cpu_data->cyc_per_usec = (itc_freq + USEC_PER_SEC/2) / USEC_PER_SEC;
	local_cpu_data->nsec_per_cyc = ((NSEC_PER_SEC<<IA64_NSEC_PER_CYC_SHIFT)
					+ itc_freq/2)/itc_freq;

	if (!(sal_platform_features & IA64_SAL_PLATFORM_FEATURE_ITC_DRIFT)) {
#ifdef CONFIG_SMP
		/* On IA64 in an SMP configuration ITCs are never accurately synchronized.
		 * Jitter compensation requires a cmpxchg which may limit
		 * the scalability of the syscalls for retrieving time.
		 * The ITC synchronization is usually successful to within a few
		 * ITC ticks but this is not a sure thing. If you need to improve
		 * timer performance in SMP situations then boot the kernel with the
		 * "nojitter" option. However, doing so may result in time fluctuating (maybe
		 * even going backward) if the ITC offsets between the individual CPUs
		 * are too large.
		 */
		if (!nojitter)
			itc_jitter_data.itc_jitter = 1;
#endif
	} else
		/*
		 * ITC is drifty and we have not synchronized the ITCs in smpboot.c.
		 * ITC values may fluctuate significantly between processors.
		 * Clock should not be used for hrtimers. Mark itc as only
		 * useful for boot and testing.
		 *
		 * Note that jitter compensation is off! There is no point of
		 * synchronizing ITCs since they may be large differentials
		 * that change over time.
		 *
		 * The only way to fix this would be to repeatedly sync the
		 * ITCs. Until that time we have to avoid ITC.
		 */
		clocksource_itc.rating = 50;

	/* Setup the CPU local timer tick */
	ia64_cpu_local_tick();

	if (!itc_clocksource) {
		/* Sort out mult/shift values: */
		clocksource_itc.mult =
			clocksource_hz2mult(local_cpu_data->itc_freq,
						clocksource_itc.shift);
		clocksource_register(&clocksource_itc);
		itc_clocksource = &clocksource_itc;
	}
}

static cycle_t itc_get_cycles(void)
{
	u64 lcycle, now, ret;

	if (!itc_jitter_data.itc_jitter)
		return get_cycles();

	lcycle = itc_jitter_data.itc_lastcycle;
	now = get_cycles();
	if (lcycle && time_after(lcycle, now))
		return lcycle;

	/*
	 * Keep track of the last timer value returned.
	 * In an SMP environment, you could lose out in contention of
	 * cmpxchg. If so, your cmpxchg returns new value which the
	 * winner of contention updated to. Use the new value instead.
	 */
	ret = cmpxchg(&itc_jitter_data.itc_lastcycle, lcycle, now);
	if (unlikely(ret != lcycle))
		return ret;

	return now;
}


static struct irqaction timer_irqaction = {
	.handler =	timer_interrupt,
	.flags =	IRQF_DISABLED | IRQF_IRQPOLL,
	.name =		"timer"
};

void __init
time_init (void)
{
	register_percpu_irq(IA64_TIMER_VECTOR, &timer_irqaction);
	efi_gettimeofday(&xtime);
	ia64_init_itm();

	/*
	 * Initialize wall_to_monotonic such that adding it to xtime will yield zero, the
	 * tv_nsec field must be normalized (i.e., 0 <= nsec < NSEC_PER_SEC).
	 */
	set_normalized_timespec(&wall_to_monotonic, -xtime.tv_sec, -xtime.tv_nsec);
}

/*
 * Generic udelay assumes that if preemption is allowed and the thread
 * migrates to another CPU, that the ITC values are synchronized across
 * all CPUs.
 */
static void
ia64_itc_udelay (unsigned long usecs)
{
	unsigned long start = ia64_get_itc();
	unsigned long end = start + usecs*local_cpu_data->cyc_per_usec;

	while (time_before(ia64_get_itc(), end))
		cpu_relax();
}

void (*ia64_udelay)(unsigned long usecs) = &ia64_itc_udelay;

void
udelay (unsigned long usecs)
{
	(*ia64_udelay)(usecs);
}
EXPORT_SYMBOL(udelay);

/* IA64 doesn't cache the timezone */
void update_vsyscall_tz(void)
{
}

void update_vsyscall(struct timespec *wall, struct clocksource *c)
{
        unsigned long flags;

        write_seqlock_irqsave(&fsyscall_gtod_data.lock, flags);

        /* copy fsyscall clock data */
        fsyscall_gtod_data.clk_mask = c->mask;
        fsyscall_gtod_data.clk_mult = c->mult;
        fsyscall_gtod_data.clk_shift = c->shift;
        fsyscall_gtod_data.clk_fsys_mmio = c->fsys_mmio;
        fsyscall_gtod_data.clk_cycle_last = c->cycle_last;

	/* copy kernel time structures */
        fsyscall_gtod_data.wall_time.tv_sec = wall->tv_sec;
        fsyscall_gtod_data.wall_time.tv_nsec = wall->tv_nsec;
        fsyscall_gtod_data.monotonic_time.tv_sec = wall_to_monotonic.tv_sec
							+ wall->tv_sec;
        fsyscall_gtod_data.monotonic_time.tv_nsec = wall_to_monotonic.tv_nsec
							+ wall->tv_nsec;

	/* normalize */
	while (fsyscall_gtod_data.monotonic_time.tv_nsec >= NSEC_PER_SEC) {
		fsyscall_gtod_data.monotonic_time.tv_nsec -= NSEC_PER_SEC;
		fsyscall_gtod_data.monotonic_time.tv_sec++;
	}

        write_sequnlock_irqrestore(&fsyscall_gtod_data.lock, flags);
}

