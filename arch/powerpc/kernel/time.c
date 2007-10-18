/*
 * Common time routines among all ppc machines.
 *
 * Written by Cort Dougan (cort@cs.nmt.edu) to merge
 * Paul Mackerras' version and mine for PReP and Pmac.
 * MPC8xx/MBX changes by Dan Malek (dmalek@jlc.net).
 * Converted for 64-bit by Mike Corrigan (mikejc@us.ibm.com)
 *
 * First round of bugfixes by Gabriel Paubert (paubert@iram.es)
 * to make clock more stable (2.4.0-test5). The only thing
 * that this code assumes is that the timebases have been synchronized
 * by firmware on SMP and are never stopped (never do sleep
 * on SMP then, nap and doze are OK).
 * 
 * Speeded up do_gettimeofday by getting rid of references to
 * xtime (which required locks for consistency). (mikejc@us.ibm.com)
 *
 * TODO (not necessarily in this file):
 * - improve precision and reproducibility of timebase frequency
 * measurement at boot time. (for iSeries, we calibrate the timebase
 * against the Titan chip's clock.)
 * - for astronomical applications: add a new function to get
 * non ambiguous timestamps even around leap seconds. This needs
 * a new timestamp format and a good name.
 *
 * 1997-09-10  Updated NTP code according to technical memorandum Jan '96
 *             "A Kernel Model for Precision Timekeeping" by Dave Mills
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/errno.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/timex.h>
#include <linux/kernel_stat.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/profile.h>
#include <linux/cpu.h>
#include <linux/security.h>
#include <linux/percpu.h>
#include <linux/rtc.h>
#include <linux/jiffies.h>
#include <linux/posix-timers.h>
#include <linux/irq.h>

#include <asm/io.h>
#include <asm/processor.h>
#include <asm/nvram.h>
#include <asm/cache.h>
#include <asm/machdep.h>
#include <asm/uaccess.h>
#include <asm/time.h>
#include <asm/prom.h>
#include <asm/irq.h>
#include <asm/div64.h>
#include <asm/smp.h>
#include <asm/vdso_datapage.h>
#include <asm/firmware.h>
#ifdef CONFIG_PPC_ISERIES
#include <asm/iseries/it_lp_queue.h>
#include <asm/iseries/hv_call_xm.h>
#endif

/* powerpc clocksource/clockevent code */

#include <linux/clockchips.h>
#include <linux/clocksource.h>

static cycle_t rtc_read(void);
static struct clocksource clocksource_rtc = {
	.name         = "rtc",
	.rating       = 400,
	.flags        = CLOCK_SOURCE_IS_CONTINUOUS,
	.mask         = CLOCKSOURCE_MASK(64),
	.shift        = 22,
	.mult         = 0,	/* To be filled in */
	.read         = rtc_read,
};

static cycle_t timebase_read(void);
static struct clocksource clocksource_timebase = {
	.name         = "timebase",
	.rating       = 400,
	.flags        = CLOCK_SOURCE_IS_CONTINUOUS,
	.mask         = CLOCKSOURCE_MASK(64),
	.shift        = 22,
	.mult         = 0,	/* To be filled in */
	.read         = timebase_read,
};

#define DECREMENTER_MAX	0x7fffffff

static int decrementer_set_next_event(unsigned long evt,
				      struct clock_event_device *dev);
static void decrementer_set_mode(enum clock_event_mode mode,
				 struct clock_event_device *dev);

static struct clock_event_device decrementer_clockevent = {
       .name           = "decrementer",
       .rating         = 200,
       .shift          = 16,
       .mult           = 0,	/* To be filled in */
       .irq            = 0,
       .set_next_event = decrementer_set_next_event,
       .set_mode       = decrementer_set_mode,
       .features       = CLOCK_EVT_FEAT_ONESHOT,
};

static DEFINE_PER_CPU(struct clock_event_device, decrementers);
void init_decrementer_clockevent(void);
static DEFINE_PER_CPU(u64, decrementer_next_tb);

#ifdef CONFIG_PPC_ISERIES
static unsigned long __initdata iSeries_recal_titan;
static signed long __initdata iSeries_recal_tb;

/* Forward declaration is only needed for iSereis compiles */
void __init clocksource_init(void);
#endif

#define XSEC_PER_SEC (1024*1024)

#ifdef CONFIG_PPC64
#define SCALE_XSEC(xsec, max)	(((xsec) * max) / XSEC_PER_SEC)
#else
/* compute ((xsec << 12) * max) >> 32 */
#define SCALE_XSEC(xsec, max)	mulhwu((xsec) << 12, max)
#endif

unsigned long tb_ticks_per_jiffy;
unsigned long tb_ticks_per_usec = 100; /* sane default */
EXPORT_SYMBOL(tb_ticks_per_usec);
unsigned long tb_ticks_per_sec;
EXPORT_SYMBOL(tb_ticks_per_sec);	/* for cputime_t conversions */
u64 tb_to_xs;
unsigned tb_to_us;

#define TICKLEN_SCALE	TICK_LENGTH_SHIFT
u64 last_tick_len;	/* units are ns / 2^TICKLEN_SCALE */
u64 ticklen_to_xs;	/* 0.64 fraction */

/* If last_tick_len corresponds to about 1/HZ seconds, then
   last_tick_len << TICKLEN_SHIFT will be about 2^63. */
#define TICKLEN_SHIFT	(63 - 30 - TICKLEN_SCALE + SHIFT_HZ)

DEFINE_SPINLOCK(rtc_lock);
EXPORT_SYMBOL_GPL(rtc_lock);

static u64 tb_to_ns_scale __read_mostly;
static unsigned tb_to_ns_shift __read_mostly;
static unsigned long boot_tb __read_mostly;

struct gettimeofday_struct do_gtod;

extern struct timezone sys_tz;
static long timezone_offset;

unsigned long ppc_proc_freq;
EXPORT_SYMBOL(ppc_proc_freq);
unsigned long ppc_tb_freq;

static u64 tb_last_jiffy __cacheline_aligned_in_smp;
static DEFINE_PER_CPU(u64, last_jiffy);

#ifdef CONFIG_VIRT_CPU_ACCOUNTING
/*
 * Factors for converting from cputime_t (timebase ticks) to
 * jiffies, milliseconds, seconds, and clock_t (1/USER_HZ seconds).
 * These are all stored as 0.64 fixed-point binary fractions.
 */
u64 __cputime_jiffies_factor;
EXPORT_SYMBOL(__cputime_jiffies_factor);
u64 __cputime_msec_factor;
EXPORT_SYMBOL(__cputime_msec_factor);
u64 __cputime_sec_factor;
EXPORT_SYMBOL(__cputime_sec_factor);
u64 __cputime_clockt_factor;
EXPORT_SYMBOL(__cputime_clockt_factor);

static void calc_cputime_factors(void)
{
	struct div_result res;

	div128_by_32(HZ, 0, tb_ticks_per_sec, &res);
	__cputime_jiffies_factor = res.result_low;
	div128_by_32(1000, 0, tb_ticks_per_sec, &res);
	__cputime_msec_factor = res.result_low;
	div128_by_32(1, 0, tb_ticks_per_sec, &res);
	__cputime_sec_factor = res.result_low;
	div128_by_32(USER_HZ, 0, tb_ticks_per_sec, &res);
	__cputime_clockt_factor = res.result_low;
}

/*
 * Read the PURR on systems that have it, otherwise the timebase.
 */
static u64 read_purr(void)
{
	if (cpu_has_feature(CPU_FTR_PURR))
		return mfspr(SPRN_PURR);
	return mftb();
}

/*
 * Read the SPURR on systems that have it, otherwise the purr
 */
static u64 read_spurr(u64 purr)
{
	if (cpu_has_feature(CPU_FTR_SPURR))
		return mfspr(SPRN_SPURR);
	return purr;
}

/*
 * Account time for a transition between system, hard irq
 * or soft irq state.
 */
void account_system_vtime(struct task_struct *tsk)
{
	u64 now, nowscaled, delta, deltascaled;
	unsigned long flags;

	local_irq_save(flags);
	now = read_purr();
	delta = now - get_paca()->startpurr;
	get_paca()->startpurr = now;
	nowscaled = read_spurr(now);
	deltascaled = nowscaled - get_paca()->startspurr;
	get_paca()->startspurr = nowscaled;
	if (!in_interrupt()) {
		/* deltascaled includes both user and system time.
		 * Hence scale it based on the purr ratio to estimate
		 * the system time */
		deltascaled = deltascaled * get_paca()->system_time /
			(get_paca()->system_time + get_paca()->user_time);
		delta += get_paca()->system_time;
		get_paca()->system_time = 0;
	}
	account_system_time(tsk, 0, delta);
	get_paca()->purrdelta = delta;
	account_system_time_scaled(tsk, deltascaled);
	get_paca()->spurrdelta = deltascaled;
	local_irq_restore(flags);
}

/*
 * Transfer the user and system times accumulated in the paca
 * by the exception entry and exit code to the generic process
 * user and system time records.
 * Must be called with interrupts disabled.
 */
void account_process_vtime(struct task_struct *tsk)
{
	cputime_t utime, utimescaled;

	utime = get_paca()->user_time;
	get_paca()->user_time = 0;
	account_user_time(tsk, utime);

	/* Estimate the scaled utime by scaling the real utime based
	 * on the last spurr to purr ratio */
	utimescaled = utime * get_paca()->spurrdelta / get_paca()->purrdelta;
	get_paca()->spurrdelta = get_paca()->purrdelta = 0;
	account_user_time_scaled(tsk, utimescaled);
}

static void account_process_time(struct pt_regs *regs)
{
	int cpu = smp_processor_id();

	account_process_vtime(current);
	run_local_timers();
	if (rcu_pending(cpu))
		rcu_check_callbacks(cpu, user_mode(regs));
	scheduler_tick();
 	run_posix_cpu_timers(current);
}

/*
 * Stuff for accounting stolen time.
 */
struct cpu_purr_data {
	int	initialized;			/* thread is running */
	u64	tb;			/* last TB value read */
	u64	purr;			/* last PURR value read */
	u64	spurr;			/* last SPURR value read */
};

/*
 * Each entry in the cpu_purr_data array is manipulated only by its
 * "owner" cpu -- usually in the timer interrupt but also occasionally
 * in process context for cpu online.  As long as cpus do not touch
 * each others' cpu_purr_data, disabling local interrupts is
 * sufficient to serialize accesses.
 */
static DEFINE_PER_CPU(struct cpu_purr_data, cpu_purr_data);

static void snapshot_tb_and_purr(void *data)
{
	unsigned long flags;
	struct cpu_purr_data *p = &__get_cpu_var(cpu_purr_data);

	local_irq_save(flags);
	p->tb = get_tb_or_rtc();
	p->purr = mfspr(SPRN_PURR);
	wmb();
	p->initialized = 1;
	local_irq_restore(flags);
}

/*
 * Called during boot when all cpus have come up.
 */
void snapshot_timebases(void)
{
	if (!cpu_has_feature(CPU_FTR_PURR))
		return;
	on_each_cpu(snapshot_tb_and_purr, NULL, 0, 1);
}

/*
 * Must be called with interrupts disabled.
 */
void calculate_steal_time(void)
{
	u64 tb, purr;
	s64 stolen;
	struct cpu_purr_data *pme;

	if (!cpu_has_feature(CPU_FTR_PURR))
		return;
	pme = &per_cpu(cpu_purr_data, smp_processor_id());
	if (!pme->initialized)
		return;		/* this can happen in early boot */
	tb = mftb();
	purr = mfspr(SPRN_PURR);
	stolen = (tb - pme->tb) - (purr - pme->purr);
	if (stolen > 0)
		account_steal_time(current, stolen);
	pme->tb = tb;
	pme->purr = purr;
}

#ifdef CONFIG_PPC_SPLPAR
/*
 * Must be called before the cpu is added to the online map when
 * a cpu is being brought up at runtime.
 */
static void snapshot_purr(void)
{
	struct cpu_purr_data *pme;
	unsigned long flags;

	if (!cpu_has_feature(CPU_FTR_PURR))
		return;
	local_irq_save(flags);
	pme = &per_cpu(cpu_purr_data, smp_processor_id());
	pme->tb = mftb();
	pme->purr = mfspr(SPRN_PURR);
	pme->initialized = 1;
	local_irq_restore(flags);
}

#endif /* CONFIG_PPC_SPLPAR */

#else /* ! CONFIG_VIRT_CPU_ACCOUNTING */
#define calc_cputime_factors()
#define account_process_time(regs)	update_process_times(user_mode(regs))
#define calculate_steal_time()		do { } while (0)
#endif

#if !(defined(CONFIG_VIRT_CPU_ACCOUNTING) && defined(CONFIG_PPC_SPLPAR))
#define snapshot_purr()			do { } while (0)
#endif

/*
 * Called when a cpu comes up after the system has finished booting,
 * i.e. as a result of a hotplug cpu action.
 */
void snapshot_timebase(void)
{
	__get_cpu_var(last_jiffy) = get_tb_or_rtc();
	snapshot_purr();
}

void __delay(unsigned long loops)
{
	unsigned long start;
	int diff;

	if (__USE_RTC()) {
		start = get_rtcl();
		do {
			/* the RTCL register wraps at 1000000000 */
			diff = get_rtcl() - start;
			if (diff < 0)
				diff += 1000000000;
		} while (diff < loops);
	} else {
		start = get_tbl();
		while (get_tbl() - start < loops)
			HMT_low();
		HMT_medium();
	}
}
EXPORT_SYMBOL(__delay);

void udelay(unsigned long usecs)
{
	__delay(tb_ticks_per_usec * usecs);
}
EXPORT_SYMBOL(udelay);


/*
 * There are two copies of tb_to_xs and stamp_xsec so that no
 * lock is needed to access and use these values in
 * do_gettimeofday.  We alternate the copies and as long as a
 * reasonable time elapses between changes, there will never
 * be inconsistent values.  ntpd has a minimum of one minute
 * between updates.
 */
static inline void update_gtod(u64 new_tb_stamp, u64 new_stamp_xsec,
			       u64 new_tb_to_xs)
{
	unsigned temp_idx;
	struct gettimeofday_vars *temp_varp;

	temp_idx = (do_gtod.var_idx == 0);
	temp_varp = &do_gtod.vars[temp_idx];

	temp_varp->tb_to_xs = new_tb_to_xs;
	temp_varp->tb_orig_stamp = new_tb_stamp;
	temp_varp->stamp_xsec = new_stamp_xsec;
	smp_mb();
	do_gtod.varp = temp_varp;
	do_gtod.var_idx = temp_idx;

	/*
	 * tb_update_count is used to allow the userspace gettimeofday code
	 * to assure itself that it sees a consistent view of the tb_to_xs and
	 * stamp_xsec variables.  It reads the tb_update_count, then reads
	 * tb_to_xs and stamp_xsec and then reads tb_update_count again.  If
	 * the two values of tb_update_count match and are even then the
	 * tb_to_xs and stamp_xsec values are consistent.  If not, then it
	 * loops back and reads them again until this criteria is met.
	 * We expect the caller to have done the first increment of
	 * vdso_data->tb_update_count already.
	 */
	vdso_data->tb_orig_stamp = new_tb_stamp;
	vdso_data->stamp_xsec = new_stamp_xsec;
	vdso_data->tb_to_xs = new_tb_to_xs;
	vdso_data->wtom_clock_sec = wall_to_monotonic.tv_sec;
	vdso_data->wtom_clock_nsec = wall_to_monotonic.tv_nsec;
	smp_wmb();
	++(vdso_data->tb_update_count);
}

#ifdef CONFIG_SMP
unsigned long profile_pc(struct pt_regs *regs)
{
	unsigned long pc = instruction_pointer(regs);

	if (in_lock_functions(pc))
		return regs->link;

	return pc;
}
EXPORT_SYMBOL(profile_pc);
#endif

#ifdef CONFIG_PPC_ISERIES

/* 
 * This function recalibrates the timebase based on the 49-bit time-of-day
 * value in the Titan chip.  The Titan is much more accurate than the value
 * returned by the service processor for the timebase frequency.  
 */

static int __init iSeries_tb_recal(void)
{
	struct div_result divres;
	unsigned long titan, tb;

	/* Make sure we only run on iSeries */
	if (!firmware_has_feature(FW_FEATURE_ISERIES))
		return -ENODEV;

	tb = get_tb();
	titan = HvCallXm_loadTod();
	if ( iSeries_recal_titan ) {
		unsigned long tb_ticks = tb - iSeries_recal_tb;
		unsigned long titan_usec = (titan - iSeries_recal_titan) >> 12;
		unsigned long new_tb_ticks_per_sec   = (tb_ticks * USEC_PER_SEC)/titan_usec;
		unsigned long new_tb_ticks_per_jiffy = (new_tb_ticks_per_sec+(HZ/2))/HZ;
		long tick_diff = new_tb_ticks_per_jiffy - tb_ticks_per_jiffy;
		char sign = '+';		
		/* make sure tb_ticks_per_sec and tb_ticks_per_jiffy are consistent */
		new_tb_ticks_per_sec = new_tb_ticks_per_jiffy * HZ;

		if ( tick_diff < 0 ) {
			tick_diff = -tick_diff;
			sign = '-';
		}
		if ( tick_diff ) {
			if ( tick_diff < tb_ticks_per_jiffy/25 ) {
				printk( "Titan recalibrate: new tb_ticks_per_jiffy = %lu (%c%ld)\n",
						new_tb_ticks_per_jiffy, sign, tick_diff );
				tb_ticks_per_jiffy = new_tb_ticks_per_jiffy;
				tb_ticks_per_sec   = new_tb_ticks_per_sec;
				calc_cputime_factors();
				div128_by_32( XSEC_PER_SEC, 0, tb_ticks_per_sec, &divres );
				do_gtod.tb_ticks_per_sec = tb_ticks_per_sec;
				tb_to_xs = divres.result_low;
				do_gtod.varp->tb_to_xs = tb_to_xs;
				vdso_data->tb_ticks_per_sec = tb_ticks_per_sec;
				vdso_data->tb_to_xs = tb_to_xs;
			}
			else {
				printk( "Titan recalibrate: FAILED (difference > 4 percent)\n"
					"                   new tb_ticks_per_jiffy = %lu\n"
					"                   old tb_ticks_per_jiffy = %lu\n",
					new_tb_ticks_per_jiffy, tb_ticks_per_jiffy );
			}
		}
	}
	iSeries_recal_titan = titan;
	iSeries_recal_tb = tb;

	/* Called here as now we know accurate values for the timebase */
	clocksource_init();
	return 0;
}
late_initcall(iSeries_tb_recal);

/* Called from platform early init */
void __init iSeries_time_init_early(void)
{
	iSeries_recal_tb = get_tb();
	iSeries_recal_titan = HvCallXm_loadTod();
}
#endif /* CONFIG_PPC_ISERIES */

/*
 * For iSeries shared processors, we have to let the hypervisor
 * set the hardware decrementer.  We set a virtual decrementer
 * in the lppaca and call the hypervisor if the virtual
 * decrementer is less than the current value in the hardware
 * decrementer. (almost always the new decrementer value will
 * be greater than the current hardware decementer so the hypervisor
 * call will not be needed)
 */

/*
 * timer_interrupt - gets called when the decrementer overflows,
 * with interrupts disabled.
 */
void timer_interrupt(struct pt_regs * regs)
{
	struct pt_regs *old_regs;
	int cpu = smp_processor_id();
	struct clock_event_device *evt = &per_cpu(decrementers, cpu);
	u64 now;

	/* Ensure a positive value is written to the decrementer, or else
	 * some CPUs will continuue to take decrementer exceptions */
	set_dec(DECREMENTER_MAX);

#ifdef CONFIG_PPC32
	if (atomic_read(&ppc_n_lost_interrupts) != 0)
		do_IRQ(regs);
#endif

	now = get_tb_or_rtc();
	if (now < per_cpu(decrementer_next_tb, cpu)) {
		/* not time for this event yet */
		now = per_cpu(decrementer_next_tb, cpu) - now;
		if (now <= DECREMENTER_MAX)
			set_dec((unsigned int)now - 1);
		return;
	}
	old_regs = set_irq_regs(regs);
	irq_enter();

	calculate_steal_time();

#ifdef CONFIG_PPC_ISERIES
	if (firmware_has_feature(FW_FEATURE_ISERIES))
		get_lppaca()->int_dword.fields.decr_int = 0;
#endif

	/*
	 * We cannot disable the decrementer, so in the period
	 * between this cpu's being marked offline in cpu_online_map
	 * and calling stop-self, it is taking timer interrupts.
	 * Avoid calling into the scheduler rebalancing code if this
	 * is the case.
	 */
	if (!cpu_is_offline(cpu))
		account_process_time(regs);

	if (evt->event_handler)
		evt->event_handler(evt);
	else
		evt->set_next_event(DECREMENTER_MAX, evt);

#ifdef CONFIG_PPC_ISERIES
	if (firmware_has_feature(FW_FEATURE_ISERIES) && hvlpevent_is_pending())
		process_hvlpevents();
#endif

#ifdef CONFIG_PPC64
	/* collect purr register values often, for accurate calculations */
	if (firmware_has_feature(FW_FEATURE_SPLPAR)) {
		struct cpu_usage *cu = &__get_cpu_var(cpu_usage_array);
		cu->current_tb = mfspr(SPRN_PURR);
	}
#endif

	irq_exit();
	set_irq_regs(old_regs);
}

void wakeup_decrementer(void)
{
	unsigned long ticks;

	/*
	 * The timebase gets saved on sleep and restored on wakeup,
	 * so all we need to do is to reset the decrementer.
	 */
	ticks = tb_ticks_since(__get_cpu_var(last_jiffy));
	if (ticks < tb_ticks_per_jiffy)
		ticks = tb_ticks_per_jiffy - ticks;
	else
		ticks = 1;
	set_dec(ticks);
}

#ifdef CONFIG_SMP
void __init smp_space_timers(unsigned int max_cpus)
{
	int i;
	u64 previous_tb = per_cpu(last_jiffy, boot_cpuid);

	/* make sure tb > per_cpu(last_jiffy, cpu) for all cpus always */
	previous_tb -= tb_ticks_per_jiffy;

	for_each_possible_cpu(i) {
		if (i == boot_cpuid)
			continue;
		per_cpu(last_jiffy, i) = previous_tb;
	}
}
#endif

/*
 * Scheduler clock - returns current time in nanosec units.
 *
 * Note: mulhdu(a, b) (multiply high double unsigned) returns
 * the high 64 bits of a * b, i.e. (a * b) >> 64, where a and b
 * are 64-bit unsigned numbers.
 */
unsigned long long sched_clock(void)
{
	if (__USE_RTC())
		return get_rtc();
	return mulhdu(get_tb() - boot_tb, tb_to_ns_scale) << tb_to_ns_shift;
}

static int __init get_freq(char *name, int cells, unsigned long *val)
{
	struct device_node *cpu;
	const unsigned int *fp;
	int found = 0;

	/* The cpu node should have timebase and clock frequency properties */
	cpu = of_find_node_by_type(NULL, "cpu");

	if (cpu) {
		fp = of_get_property(cpu, name, NULL);
		if (fp) {
			found = 1;
			*val = of_read_ulong(fp, cells);
		}

		of_node_put(cpu);
	}

	return found;
}

void __init generic_calibrate_decr(void)
{
	ppc_tb_freq = DEFAULT_TB_FREQ;		/* hardcoded default */

	if (!get_freq("ibm,extended-timebase-frequency", 2, &ppc_tb_freq) &&
	    !get_freq("timebase-frequency", 1, &ppc_tb_freq)) {

		printk(KERN_ERR "WARNING: Estimating decrementer frequency "
				"(not found)\n");
	}

	ppc_proc_freq = DEFAULT_PROC_FREQ;	/* hardcoded default */

	if (!get_freq("ibm,extended-clock-frequency", 2, &ppc_proc_freq) &&
	    !get_freq("clock-frequency", 1, &ppc_proc_freq)) {

		printk(KERN_ERR "WARNING: Estimating processor frequency "
				"(not found)\n");
	}

#if defined(CONFIG_BOOKE) || defined(CONFIG_40x)
	/* Set the time base to zero */
	mtspr(SPRN_TBWL, 0);
	mtspr(SPRN_TBWU, 0);

	/* Clear any pending timer interrupts */
	mtspr(SPRN_TSR, TSR_ENW | TSR_WIS | TSR_DIS | TSR_FIS);

	/* Enable decrementer interrupt */
	mtspr(SPRN_TCR, TCR_DIE);
#endif
}

int update_persistent_clock(struct timespec now)
{
	struct rtc_time tm;

	if (!ppc_md.set_rtc_time)
		return 0;

	to_tm(now.tv_sec + 1 + timezone_offset, &tm);
	tm.tm_year -= 1900;
	tm.tm_mon -= 1;

	return ppc_md.set_rtc_time(&tm);
}

unsigned long read_persistent_clock(void)
{
	struct rtc_time tm;
	static int first = 1;

	/* XXX this is a litle fragile but will work okay in the short term */
	if (first) {
		first = 0;
		if (ppc_md.time_init)
			timezone_offset = ppc_md.time_init();

		/* get_boot_time() isn't guaranteed to be safe to call late */
		if (ppc_md.get_boot_time)
			return ppc_md.get_boot_time() -timezone_offset;
	}
	if (!ppc_md.get_rtc_time)
		return 0;
	ppc_md.get_rtc_time(&tm);
	return mktime(tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday,
		      tm.tm_hour, tm.tm_min, tm.tm_sec);
}

/* clocksource code */
static cycle_t rtc_read(void)
{
	return (cycle_t)get_rtc();
}

static cycle_t timebase_read(void)
{
	return (cycle_t)get_tb();
}

void update_vsyscall(struct timespec *wall_time, struct clocksource *clock)
{
	u64 t2x, stamp_xsec;

	if (clock != &clocksource_timebase)
		return;

	/* Make userspace gettimeofday spin until we're done. */
	++vdso_data->tb_update_count;
	smp_mb();

	/* XXX this assumes clock->shift == 22 */
	/* 4611686018 ~= 2^(20+64-22) / 1e9 */
	t2x = (u64) clock->mult * 4611686018ULL;
	stamp_xsec = (u64) xtime.tv_nsec * XSEC_PER_SEC;
	do_div(stamp_xsec, 1000000000);
	stamp_xsec += (u64) xtime.tv_sec * XSEC_PER_SEC;
	update_gtod(clock->cycle_last, stamp_xsec, t2x);
}

void update_vsyscall_tz(void)
{
	/* Make userspace gettimeofday spin until we're done. */
	++vdso_data->tb_update_count;
	smp_mb();
	vdso_data->tz_minuteswest = sys_tz.tz_minuteswest;
	vdso_data->tz_dsttime = sys_tz.tz_dsttime;
	smp_mb();
	++vdso_data->tb_update_count;
}

void __init clocksource_init(void)
{
	struct clocksource *clock;

	if (__USE_RTC())
		clock = &clocksource_rtc;
	else
		clock = &clocksource_timebase;

	clock->mult = clocksource_hz2mult(tb_ticks_per_sec, clock->shift);

	if (clocksource_register(clock)) {
		printk(KERN_ERR "clocksource: %s is already registered\n",
		       clock->name);
		return;
	}

	printk(KERN_INFO "clocksource: %s mult[%x] shift[%d] registered\n",
	       clock->name, clock->mult, clock->shift);
}

static int decrementer_set_next_event(unsigned long evt,
				      struct clock_event_device *dev)
{
	__get_cpu_var(decrementer_next_tb) = get_tb_or_rtc() + evt;
	/* The decrementer interrupts on the 0 -> -1 transition */
	if (evt)
		--evt;
	set_dec(evt);
	return 0;
}

static void decrementer_set_mode(enum clock_event_mode mode,
				 struct clock_event_device *dev)
{
	if (mode != CLOCK_EVT_MODE_ONESHOT)
		decrementer_set_next_event(DECREMENTER_MAX, dev);
}

static void register_decrementer_clockevent(int cpu)
{
	struct clock_event_device *dec = &per_cpu(decrementers, cpu);

	*dec = decrementer_clockevent;
	dec->cpumask = cpumask_of_cpu(cpu);

	printk(KERN_INFO "clockevent: %s mult[%lx] shift[%d] cpu[%d]\n",
	       dec->name, dec->mult, dec->shift, cpu);

	clockevents_register_device(dec);
}

void init_decrementer_clockevent(void)
{
	int cpu = smp_processor_id();

	decrementer_clockevent.mult = div_sc(ppc_tb_freq, NSEC_PER_SEC,
					     decrementer_clockevent.shift);
	decrementer_clockevent.max_delta_ns =
		clockevent_delta2ns(DECREMENTER_MAX, &decrementer_clockevent);
	decrementer_clockevent.min_delta_ns = 1000;

	register_decrementer_clockevent(cpu);
}

void secondary_cpu_time_init(void)
{
	/* FIME: Should make unrelatred change to move snapshot_timebase
	 * call here ! */
	register_decrementer_clockevent(smp_processor_id());
}

/* This function is only called on the boot processor */
void __init time_init(void)
{
	unsigned long flags;
	struct div_result res;
	u64 scale, x;
	unsigned shift;

	if (__USE_RTC()) {
		/* 601 processor: dec counts down by 128 every 128ns */
		ppc_tb_freq = 1000000000;
		tb_last_jiffy = get_rtcl();
	} else {
		/* Normal PowerPC with timebase register */
		ppc_md.calibrate_decr();
		printk(KERN_DEBUG "time_init: decrementer frequency = %lu.%.6lu MHz\n",
		       ppc_tb_freq / 1000000, ppc_tb_freq % 1000000);
		printk(KERN_DEBUG "time_init: processor frequency   = %lu.%.6lu MHz\n",
		       ppc_proc_freq / 1000000, ppc_proc_freq % 1000000);
		tb_last_jiffy = get_tb();
	}

	tb_ticks_per_jiffy = ppc_tb_freq / HZ;
	tb_ticks_per_sec = ppc_tb_freq;
	tb_ticks_per_usec = ppc_tb_freq / 1000000;
	tb_to_us = mulhwu_scale_factor(ppc_tb_freq, 1000000);
	calc_cputime_factors();

	/*
	 * Calculate the length of each tick in ns.  It will not be
	 * exactly 1e9/HZ unless ppc_tb_freq is divisible by HZ.
	 * We compute 1e9 * tb_ticks_per_jiffy / ppc_tb_freq,
	 * rounded up.
	 */
	x = (u64) NSEC_PER_SEC * tb_ticks_per_jiffy + ppc_tb_freq - 1;
	do_div(x, ppc_tb_freq);
	tick_nsec = x;
	last_tick_len = x << TICKLEN_SCALE;

	/*
	 * Compute ticklen_to_xs, which is a factor which gets multiplied
	 * by (last_tick_len << TICKLEN_SHIFT) to get a tb_to_xs value.
	 * It is computed as:
	 * ticklen_to_xs = 2^N / (tb_ticks_per_jiffy * 1e9)
	 * where N = 64 + 20 - TICKLEN_SCALE - TICKLEN_SHIFT
	 * which turns out to be N = 51 - SHIFT_HZ.
	 * This gives the result as a 0.64 fixed-point fraction.
	 * That value is reduced by an offset amounting to 1 xsec per
	 * 2^31 timebase ticks to avoid problems with time going backwards
	 * by 1 xsec when we do timer_recalc_offset due to losing the
	 * fractional xsec.  That offset is equal to ppc_tb_freq/2^51
	 * since there are 2^20 xsec in a second.
	 */
	div128_by_32((1ULL << 51) - ppc_tb_freq, 0,
		     tb_ticks_per_jiffy << SHIFT_HZ, &res);
	div128_by_32(res.result_high, res.result_low, NSEC_PER_SEC, &res);
	ticklen_to_xs = res.result_low;

	/* Compute tb_to_xs from tick_nsec */
	tb_to_xs = mulhdu(last_tick_len << TICKLEN_SHIFT, ticklen_to_xs);

	/*
	 * Compute scale factor for sched_clock.
	 * The calibrate_decr() function has set tb_ticks_per_sec,
	 * which is the timebase frequency.
	 * We compute 1e9 * 2^64 / tb_ticks_per_sec and interpret
	 * the 128-bit result as a 64.64 fixed-point number.
	 * We then shift that number right until it is less than 1.0,
	 * giving us the scale factor and shift count to use in
	 * sched_clock().
	 */
	div128_by_32(1000000000, 0, tb_ticks_per_sec, &res);
	scale = res.result_low;
	for (shift = 0; res.result_high != 0; ++shift) {
		scale = (scale >> 1) | (res.result_high << 63);
		res.result_high >>= 1;
	}
	tb_to_ns_scale = scale;
	tb_to_ns_shift = shift;
	/* Save the current timebase to pretty up CONFIG_PRINTK_TIME */
	boot_tb = get_tb_or_rtc();

	write_seqlock_irqsave(&xtime_lock, flags);

	/* If platform provided a timezone (pmac), we correct the time */
        if (timezone_offset) {
		sys_tz.tz_minuteswest = -timezone_offset / 60;
		sys_tz.tz_dsttime = 0;
        }

	do_gtod.varp = &do_gtod.vars[0];
	do_gtod.var_idx = 0;
	do_gtod.varp->tb_orig_stamp = tb_last_jiffy;
	__get_cpu_var(last_jiffy) = tb_last_jiffy;
	do_gtod.varp->stamp_xsec = (u64) xtime.tv_sec * XSEC_PER_SEC;
	do_gtod.tb_ticks_per_sec = tb_ticks_per_sec;
	do_gtod.varp->tb_to_xs = tb_to_xs;
	do_gtod.tb_to_us = tb_to_us;

	vdso_data->tb_orig_stamp = tb_last_jiffy;
	vdso_data->tb_update_count = 0;
	vdso_data->tb_ticks_per_sec = tb_ticks_per_sec;
	vdso_data->stamp_xsec = (u64) xtime.tv_sec * XSEC_PER_SEC;
	vdso_data->tb_to_xs = tb_to_xs;

	time_freq = 0;

	write_sequnlock_irqrestore(&xtime_lock, flags);

	/* Register the clocksource, if we're not running on iSeries */
	if (!firmware_has_feature(FW_FEATURE_ISERIES))
		clocksource_init();

	init_decrementer_clockevent();
}


#define FEBRUARY	2
#define	STARTOFTIME	1970
#define SECDAY		86400L
#define SECYR		(SECDAY * 365)
#define	leapyear(year)		((year) % 4 == 0 && \
				 ((year) % 100 != 0 || (year) % 400 == 0))
#define	days_in_year(a) 	(leapyear(a) ? 366 : 365)
#define	days_in_month(a) 	(month_days[(a) - 1])

static int month_days[12] = {
	31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

/*
 * This only works for the Gregorian calendar - i.e. after 1752 (in the UK)
 */
void GregorianDay(struct rtc_time * tm)
{
	int leapsToDate;
	int lastYear;
	int day;
	int MonthOffset[] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };

	lastYear = tm->tm_year - 1;

	/*
	 * Number of leap corrections to apply up to end of last year
	 */
	leapsToDate = lastYear / 4 - lastYear / 100 + lastYear / 400;

	/*
	 * This year is a leap year if it is divisible by 4 except when it is
	 * divisible by 100 unless it is divisible by 400
	 *
	 * e.g. 1904 was a leap year, 1900 was not, 1996 is, and 2000 was
	 */
	day = tm->tm_mon > 2 && leapyear(tm->tm_year);

	day += lastYear*365 + leapsToDate + MonthOffset[tm->tm_mon-1] +
		   tm->tm_mday;

	tm->tm_wday = day % 7;
}

void to_tm(int tim, struct rtc_time * tm)
{
	register int    i;
	register long   hms, day;

	day = tim / SECDAY;
	hms = tim % SECDAY;

	/* Hours, minutes, seconds are easy */
	tm->tm_hour = hms / 3600;
	tm->tm_min = (hms % 3600) / 60;
	tm->tm_sec = (hms % 3600) % 60;

	/* Number of years in days */
	for (i = STARTOFTIME; day >= days_in_year(i); i++)
		day -= days_in_year(i);
	tm->tm_year = i;

	/* Number of months in days left */
	if (leapyear(tm->tm_year))
		days_in_month(FEBRUARY) = 29;
	for (i = 1; day >= days_in_month(i); i++)
		day -= days_in_month(i);
	days_in_month(FEBRUARY) = 28;
	tm->tm_mon = i;

	/* Days are what is left over (+1) from all that. */
	tm->tm_mday = day + 1;

	/*
	 * Determine the day of week
	 */
	GregorianDay(tm);
}

/* Auxiliary function to compute scaling factors */
/* Actually the choice of a timebase running at 1/4 the of the bus
 * frequency giving resolution of a few tens of nanoseconds is quite nice.
 * It makes this computation very precise (27-28 bits typically) which
 * is optimistic considering the stability of most processor clock
 * oscillators and the precision with which the timebase frequency
 * is measured but does not harm.
 */
unsigned mulhwu_scale_factor(unsigned inscale, unsigned outscale)
{
        unsigned mlt=0, tmp, err;
        /* No concern for performance, it's done once: use a stupid
         * but safe and compact method to find the multiplier.
         */
  
        for (tmp = 1U<<31; tmp != 0; tmp >>= 1) {
                if (mulhwu(inscale, mlt|tmp) < outscale)
			mlt |= tmp;
        }
  
        /* We might still be off by 1 for the best approximation.
         * A side effect of this is that if outscale is too large
         * the returned value will be zero.
         * Many corner cases have been checked and seem to work,
         * some might have been forgotten in the test however.
         */
  
        err = inscale * (mlt+1);
        if (err <= inscale/2)
		mlt++;
        return mlt;
}

/*
 * Divide a 128-bit dividend by a 32-bit divisor, leaving a 128 bit
 * result.
 */
void div128_by_32(u64 dividend_high, u64 dividend_low,
		  unsigned divisor, struct div_result *dr)
{
	unsigned long a, b, c, d;
	unsigned long w, x, y, z;
	u64 ra, rb, rc;

	a = dividend_high >> 32;
	b = dividend_high & 0xffffffff;
	c = dividend_low >> 32;
	d = dividend_low & 0xffffffff;

	w = a / divisor;
	ra = ((u64)(a - (w * divisor)) << 32) + b;

	rb = ((u64) do_div(ra, divisor) << 32) + c;
	x = ra;

	rc = ((u64) do_div(rb, divisor) << 32) + d;
	y = rb;

	do_div(rc, divisor);
	z = rc;

	dr->result_high = ((u64)w << 32) + x;
	dr->result_low  = ((u64)y << 32) + z;

}
