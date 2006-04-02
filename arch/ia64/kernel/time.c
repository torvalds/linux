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
#include <linux/config.h>

#include <linux/cpu.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/profile.h>
#include <linux/sched.h>
#include <linux/time.h>
#include <linux/interrupt.h>
#include <linux/efi.h>
#include <linux/profile.h>
#include <linux/timex.h>

#include <asm/machvec.h>
#include <asm/delay.h>
#include <asm/hw_irq.h>
#include <asm/ptrace.h>
#include <asm/sal.h>
#include <asm/sections.h>
#include <asm/system.h>

extern unsigned long wall_jiffies;

volatile int time_keeper_id = 0; /* smp_processor_id() of time-keeper */

#ifdef CONFIG_IA64_DEBUG_IRQ

unsigned long last_cli_ip;
EXPORT_SYMBOL(last_cli_ip);

#endif

static struct time_interpolator itc_interpolator = {
	.shift = 16,
	.mask = 0xffffffffffffffffLL,
	.source = TIME_SOURCE_CPU
};

static irqreturn_t
timer_interrupt (int irq, void *dev_id, struct pt_regs *regs)
{
	unsigned long new_itm;

	if (unlikely(cpu_is_offline(smp_processor_id()))) {
		return IRQ_HANDLED;
	}

	platform_timer_interrupt(irq, dev_id, regs);

	new_itm = local_cpu_data->itm_next;

	if (!time_after(ia64_get_itc(), new_itm))
		printk(KERN_ERR "Oops: timer tick before it's due (itc=%lx,itm=%lx)\n",
		       ia64_get_itc(), new_itm);

	profile_tick(CPU_PROFILING, regs);

	while (1) {
		update_process_times(user_mode(regs));

		new_itm += local_cpu_data->itm_delta;

		if (smp_processor_id() == time_keeper_id) {
			/*
			 * Here we are in the timer irq handler. We have irqs locally
			 * disabled, but we don't know if the timer_bh is running on
			 * another CPU. We need to avoid to SMP race by acquiring the
			 * xtime_lock.
			 */
			write_seqlock(&xtime_lock);
			do_timer(regs);
			local_cpu_data->itm_next = new_itm;
			write_sequnlock(&xtime_lock);
		} else
			local_cpu_data->itm_next = new_itm;

		if (time_after(new_itm, ia64_get_itc()))
			break;
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
		itc_interpolator.frequency = local_cpu_data->itc_freq;
		itc_interpolator.drift = itc_drift;
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
		if (!nojitter) itc_interpolator.jitter = 1;
#endif
		register_time_interpolator(&itc_interpolator);
	}

	/* Setup the CPU local timer tick */
	ia64_cpu_local_tick();
}

static struct irqaction timer_irqaction = {
	.handler =	timer_interrupt,
	.flags =	SA_INTERRUPT,
	.name =		"timer"
};

void __devinit ia64_disable_timer(void)
{
	ia64_set_itv(1 << 16);
}

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

static unsigned long long ia64_itc_printk_clock(void)
{
	if (ia64_get_kr(IA64_KR_PER_CPU_DATA))
		return sched_clock();
	return 0;
}

static unsigned long long ia64_default_printk_clock(void)
{
	return (unsigned long long)(jiffies_64 - INITIAL_JIFFIES) *
		(1000000000/HZ);
}

unsigned long long (*ia64_printk_clock)(void) = &ia64_default_printk_clock;

unsigned long long printk_clock(void)
{
	return ia64_printk_clock();
}

void __init
ia64_setup_printk_clock(void)
{
	if (!(sal_platform_features & IA64_SAL_PLATFORM_FEATURE_ITC_DRIFT))
		ia64_printk_clock = ia64_itc_printk_clock;
}
