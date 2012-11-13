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
 * measurement at boot time.
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
#include <linux/export.h>
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
#include <linux/delay.h>
#include <linux/irq_work.h>
#include <asm/trace.h>

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
#include <asm/cputime.h>

/* powerpc clocksource/clockevent code */

#include <linux/clockchips.h>
#include <linux/timekeeper_internal.h>

static cycle_t rtc_read(struct clocksource *);
static struct clocksource clocksource_rtc = {
	.name         = "rtc",
	.rating       = 400,
	.flags        = CLOCK_SOURCE_IS_CONTINUOUS,
	.mask         = CLOCKSOURCE_MASK(64),
	.read         = rtc_read,
};

static cycle_t timebase_read(struct clocksource *);
static struct clocksource clocksource_timebase = {
	.name         = "timebase",
	.rating       = 400,
	.flags        = CLOCK_SOURCE_IS_CONTINUOUS,
	.mask         = CLOCKSOURCE_MASK(64),
	.read         = timebase_read,
};

#define DECREMENTER_MAX	0x7fffffff

static int decrementer_set_next_event(unsigned long evt,
				      struct clock_event_device *dev);
static void decrementer_set_mode(enum clock_event_mode mode,
				 struct clock_event_device *dev);

struct clock_event_device decrementer_clockevent = {
	.name           = "decrementer",
	.rating         = 200,
	.irq            = 0,
	.set_next_event = decrementer_set_next_event,
	.set_mode       = decrementer_set_mode,
	.features       = CLOCK_EVT_FEAT_ONESHOT,
};
EXPORT_SYMBOL(decrementer_clockevent);

DEFINE_PER_CPU(u64, decrementers_next_tb);
static DEFINE_PER_CPU(struct clock_event_device, decrementers);

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

DEFINE_SPINLOCK(rtc_lock);
EXPORT_SYMBOL_GPL(rtc_lock);

static u64 tb_to_ns_scale __read_mostly;
static unsigned tb_to_ns_shift __read_mostly;
static u64 boot_tb __read_mostly;

extern struct timezone sys_tz;
static long timezone_offset;

unsigned long ppc_proc_freq;
EXPORT_SYMBOL_GPL(ppc_proc_freq);
unsigned long ppc_tb_freq;
EXPORT_SYMBOL_GPL(ppc_tb_freq);

#ifdef CONFIG_VIRT_CPU_ACCOUNTING
/*
 * Factors for converting from cputime_t (timebase ticks) to
 * jiffies, microseconds, seconds, and clock_t (1/USER_HZ seconds).
 * These are all stored as 0.64 fixed-point binary fractions.
 */
u64 __cputime_jiffies_factor;
EXPORT_SYMBOL(__cputime_jiffies_factor);
u64 __cputime_usec_factor;
EXPORT_SYMBOL(__cputime_usec_factor);
u64 __cputime_sec_factor;
EXPORT_SYMBOL(__cputime_sec_factor);
u64 __cputime_clockt_factor;
EXPORT_SYMBOL(__cputime_clockt_factor);
DEFINE_PER_CPU(unsigned long, cputime_last_delta);
DEFINE_PER_CPU(unsigned long, cputime_scaled_last_delta);

cputime_t cputime_one_jiffy;

void (*dtl_consumer)(struct dtl_entry *, u64);

static void calc_cputime_factors(void)
{
	struct div_result res;

	div128_by_32(HZ, 0, tb_ticks_per_sec, &res);
	__cputime_jiffies_factor = res.result_low;
	div128_by_32(1000000, 0, tb_ticks_per_sec, &res);
	__cputime_usec_factor = res.result_low;
	div128_by_32(1, 0, tb_ticks_per_sec, &res);
	__cputime_sec_factor = res.result_low;
	div128_by_32(USER_HZ, 0, tb_ticks_per_sec, &res);
	__cputime_clockt_factor = res.result_low;
}

/*
 * Read the SPURR on systems that have it, otherwise the PURR,
 * or if that doesn't exist return the timebase value passed in.
 */
static u64 read_spurr(u64 tb)
{
	if (cpu_has_feature(CPU_FTR_SPURR))
		return mfspr(SPRN_SPURR);
	if (cpu_has_feature(CPU_FTR_PURR))
		return mfspr(SPRN_PURR);
	return tb;
}

#ifdef CONFIG_PPC_SPLPAR

/*
 * Scan the dispatch trace log and count up the stolen time.
 * Should be called with interrupts disabled.
 */
static u64 scan_dispatch_log(u64 stop_tb)
{
	u64 i = local_paca->dtl_ridx;
	struct dtl_entry *dtl = local_paca->dtl_curr;
	struct dtl_entry *dtl_end = local_paca->dispatch_log_end;
	struct lppaca *vpa = local_paca->lppaca_ptr;
	u64 tb_delta;
	u64 stolen = 0;
	u64 dtb;

	if (!dtl)
		return 0;

	if (i == vpa->dtl_idx)
		return 0;
	while (i < vpa->dtl_idx) {
		if (dtl_consumer)
			dtl_consumer(dtl, i);
		dtb = dtl->timebase;
		tb_delta = dtl->enqueue_to_dispatch_time +
			dtl->ready_to_enqueue_time;
		barrier();
		if (i + N_DISPATCH_LOG < vpa->dtl_idx) {
			/* buffer has overflowed */
			i = vpa->dtl_idx - N_DISPATCH_LOG;
			dtl = local_paca->dispatch_log + (i % N_DISPATCH_LOG);
			continue;
		}
		if (dtb > stop_tb)
			break;
		stolen += tb_delta;
		++i;
		++dtl;
		if (dtl == dtl_end)
			dtl = local_paca->dispatch_log;
	}
	local_paca->dtl_ridx = i;
	local_paca->dtl_curr = dtl;
	return stolen;
}

/*
 * Accumulate stolen time by scanning the dispatch trace log.
 * Called on entry from user mode.
 */
void accumulate_stolen_time(void)
{
	u64 sst, ust;

	u8 save_soft_enabled = local_paca->soft_enabled;

	/* We are called early in the exception entry, before
	 * soft/hard_enabled are sync'ed to the expected state
	 * for the exception. We are hard disabled but the PACA
	 * needs to reflect that so various debug stuff doesn't
	 * complain
	 */
	local_paca->soft_enabled = 0;

	sst = scan_dispatch_log(local_paca->starttime_user);
	ust = scan_dispatch_log(local_paca->starttime);
	local_paca->system_time -= sst;
	local_paca->user_time -= ust;
	local_paca->stolen_time += ust + sst;

	local_paca->soft_enabled = save_soft_enabled;
}

static inline u64 calculate_stolen_time(u64 stop_tb)
{
	u64 stolen = 0;

	if (get_paca()->dtl_ridx != get_paca()->lppaca_ptr->dtl_idx) {
		stolen = scan_dispatch_log(stop_tb);
		get_paca()->system_time -= stolen;
	}

	stolen += get_paca()->stolen_time;
	get_paca()->stolen_time = 0;
	return stolen;
}

#else /* CONFIG_PPC_SPLPAR */
static inline u64 calculate_stolen_time(u64 stop_tb)
{
	return 0;
}

#endif /* CONFIG_PPC_SPLPAR */

/*
 * Account time for a transition between system, hard irq
 * or soft irq state.
 */
static u64 vtime_delta(struct task_struct *tsk,
			u64 *sys_scaled, u64 *stolen)
{
	u64 now, nowscaled, deltascaled;
	u64 udelta, delta, user_scaled;

	now = mftb();
	nowscaled = read_spurr(now);
	get_paca()->system_time += now - get_paca()->starttime;
	get_paca()->starttime = now;
	deltascaled = nowscaled - get_paca()->startspurr;
	get_paca()->startspurr = nowscaled;

	*stolen = calculate_stolen_time(now);

	delta = get_paca()->system_time;
	get_paca()->system_time = 0;
	udelta = get_paca()->user_time - get_paca()->utime_sspurr;
	get_paca()->utime_sspurr = get_paca()->user_time;

	/*
	 * Because we don't read the SPURR on every kernel entry/exit,
	 * deltascaled includes both user and system SPURR ticks.
	 * Apportion these ticks to system SPURR ticks and user
	 * SPURR ticks in the same ratio as the system time (delta)
	 * and user time (udelta) values obtained from the timebase
	 * over the same interval.  The system ticks get accounted here;
	 * the user ticks get saved up in paca->user_time_scaled to be
	 * used by account_process_tick.
	 */
	*sys_scaled = delta;
	user_scaled = udelta;
	if (deltascaled != delta + udelta) {
		if (udelta) {
			*sys_scaled = deltascaled * delta / (delta + udelta);
			user_scaled = deltascaled - *sys_scaled;
		} else {
			*sys_scaled = deltascaled;
		}
	}
	get_paca()->user_time_scaled += user_scaled;

	return delta;
}

void vtime_account_system(struct task_struct *tsk)
{
	u64 delta, sys_scaled, stolen;

	delta = vtime_delta(tsk, &sys_scaled, &stolen);
	account_system_time(tsk, 0, delta, sys_scaled);
	if (stolen)
		account_steal_time(stolen);
}

void vtime_account_idle(struct task_struct *tsk)
{
	u64 delta, sys_scaled, stolen;

	delta = vtime_delta(tsk, &sys_scaled, &stolen);
	account_idle_time(delta + stolen);
}

/*
 * Transfer the user time accumulated in the paca
 * by the exception entry and exit code to the generic
 * process user time records.
 * Must be called with interrupts disabled.
 * Assumes that vtime_account_system/idle() has been called
 * recently (i.e. since the last entry from usermode) so that
 * get_paca()->user_time_scaled is up to date.
 */
void vtime_account_user(struct task_struct *tsk)
{
	cputime_t utime, utimescaled;

	utime = get_paca()->user_time;
	utimescaled = get_paca()->user_time_scaled;
	get_paca()->user_time = 0;
	get_paca()->user_time_scaled = 0;
	get_paca()->utime_sspurr = 0;
	account_user_time(tsk, utime, utimescaled);
}

#else /* ! CONFIG_VIRT_CPU_ACCOUNTING */
#define calc_cputime_factors()
#endif

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

#ifdef CONFIG_IRQ_WORK

/*
 * 64-bit uses a byte in the PACA, 32-bit uses a per-cpu variable...
 */
#ifdef CONFIG_PPC64
static inline unsigned long test_irq_work_pending(void)
{
	unsigned long x;

	asm volatile("lbz %0,%1(13)"
		: "=r" (x)
		: "i" (offsetof(struct paca_struct, irq_work_pending)));
	return x;
}

static inline void set_irq_work_pending_flag(void)
{
	asm volatile("stb %0,%1(13)" : :
		"r" (1),
		"i" (offsetof(struct paca_struct, irq_work_pending)));
}

static inline void clear_irq_work_pending(void)
{
	asm volatile("stb %0,%1(13)" : :
		"r" (0),
		"i" (offsetof(struct paca_struct, irq_work_pending)));
}

#else /* 32-bit */

DEFINE_PER_CPU(u8, irq_work_pending);

#define set_irq_work_pending_flag()	__get_cpu_var(irq_work_pending) = 1
#define test_irq_work_pending()		__get_cpu_var(irq_work_pending)
#define clear_irq_work_pending()	__get_cpu_var(irq_work_pending) = 0

#endif /* 32 vs 64 bit */

void arch_irq_work_raise(void)
{
	preempt_disable();
	set_irq_work_pending_flag();
	set_dec(1);
	preempt_enable();
}

#else  /* CONFIG_IRQ_WORK */

#define test_irq_work_pending()	0
#define clear_irq_work_pending()

#endif /* CONFIG_IRQ_WORK */

/*
 * timer_interrupt - gets called when the decrementer overflows,
 * with interrupts disabled.
 */
void timer_interrupt(struct pt_regs * regs)
{
	struct pt_regs *old_regs;
	u64 *next_tb = &__get_cpu_var(decrementers_next_tb);
	struct clock_event_device *evt = &__get_cpu_var(decrementers);
	u64 now;

	/* Ensure a positive value is written to the decrementer, or else
	 * some CPUs will continue to take decrementer exceptions.
	 */
	set_dec(DECREMENTER_MAX);

	/* Some implementations of hotplug will get timer interrupts while
	 * offline, just ignore these
	 */
	if (!cpu_online(smp_processor_id()))
		return;

	/* Conditionally hard-enable interrupts now that the DEC has been
	 * bumped to its maximum value
	 */
	may_hard_irq_enable();

	__get_cpu_var(irq_stat).timer_irqs++;

#if defined(CONFIG_PPC32) && defined(CONFIG_PMAC)
	if (atomic_read(&ppc_n_lost_interrupts) != 0)
		do_IRQ(regs);
#endif

	old_regs = set_irq_regs(regs);
	irq_enter();

	trace_timer_interrupt_entry(regs);

	if (test_irq_work_pending()) {
		clear_irq_work_pending();
		irq_work_run();
	}

	now = get_tb_or_rtc();
	if (now >= *next_tb) {
		*next_tb = ~(u64)0;
		if (evt->event_handler)
			evt->event_handler(evt);
	} else {
		now = *next_tb - now;
		if (now <= DECREMENTER_MAX)
			set_dec((int)now);
	}

#ifdef CONFIG_PPC64
	/* collect purr register values often, for accurate calculations */
	if (firmware_has_feature(FW_FEATURE_SPLPAR)) {
		struct cpu_usage *cu = &__get_cpu_var(cpu_usage_array);
		cu->current_tb = mfspr(SPRN_PURR);
	}
#endif

	trace_timer_interrupt_exit(regs);

	irq_exit();
	set_irq_regs(old_regs);
}

/*
 * Hypervisor decrementer interrupts shouldn't occur but are sometimes
 * left pending on exit from a KVM guest.  We don't need to do anything
 * to clear them, as they are edge-triggered.
 */
void hdec_interrupt(struct pt_regs *regs)
{
}

#ifdef CONFIG_SUSPEND
static void generic_suspend_disable_irqs(void)
{
	/* Disable the decrementer, so that it doesn't interfere
	 * with suspending.
	 */

	set_dec(DECREMENTER_MAX);
	local_irq_disable();
	set_dec(DECREMENTER_MAX);
}

static void generic_suspend_enable_irqs(void)
{
	local_irq_enable();
}

/* Overrides the weak version in kernel/power/main.c */
void arch_suspend_disable_irqs(void)
{
	if (ppc_md.suspend_disable_irqs)
		ppc_md.suspend_disable_irqs();
	generic_suspend_disable_irqs();
}

/* Overrides the weak version in kernel/power/main.c */
void arch_suspend_enable_irqs(void)
{
	generic_suspend_enable_irqs();
	if (ppc_md.suspend_enable_irqs)
		ppc_md.suspend_enable_irqs();
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

/* should become __cpuinit when secondary_cpu_time_init also is */
void start_cpu_decrementer(void)
{
#if defined(CONFIG_BOOKE) || defined(CONFIG_40x)
	/* Clear any pending timer interrupts */
	mtspr(SPRN_TSR, TSR_ENW | TSR_WIS | TSR_DIS | TSR_FIS);

	/* Enable decrementer interrupt */
	mtspr(SPRN_TCR, TCR_DIE);
#endif /* defined(CONFIG_BOOKE) || defined(CONFIG_40x) */
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

static void __read_persistent_clock(struct timespec *ts)
{
	struct rtc_time tm;
	static int first = 1;

	ts->tv_nsec = 0;
	/* XXX this is a litle fragile but will work okay in the short term */
	if (first) {
		first = 0;
		if (ppc_md.time_init)
			timezone_offset = ppc_md.time_init();

		/* get_boot_time() isn't guaranteed to be safe to call late */
		if (ppc_md.get_boot_time) {
			ts->tv_sec = ppc_md.get_boot_time() - timezone_offset;
			return;
		}
	}
	if (!ppc_md.get_rtc_time) {
		ts->tv_sec = 0;
		return;
	}
	ppc_md.get_rtc_time(&tm);

	ts->tv_sec = mktime(tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday,
			    tm.tm_hour, tm.tm_min, tm.tm_sec);
}

void read_persistent_clock(struct timespec *ts)
{
	__read_persistent_clock(ts);

	/* Sanitize it in case real time clock is set below EPOCH */
	if (ts->tv_sec < 0) {
		ts->tv_sec = 0;
		ts->tv_nsec = 0;
	}
		
}

/* clocksource code */
static cycle_t rtc_read(struct clocksource *cs)
{
	return (cycle_t)get_rtc();
}

static cycle_t timebase_read(struct clocksource *cs)
{
	return (cycle_t)get_tb();
}

void update_vsyscall_old(struct timespec *wall_time, struct timespec *wtm,
			struct clocksource *clock, u32 mult)
{
	u64 new_tb_to_xs, new_stamp_xsec;
	u32 frac_sec;

	if (clock != &clocksource_timebase)
		return;

	/* Make userspace gettimeofday spin until we're done. */
	++vdso_data->tb_update_count;
	smp_mb();

	/* 19342813113834067 ~= 2^(20+64) / 1e9 */
	new_tb_to_xs = (u64) mult * (19342813113834067ULL >> clock->shift);
	new_stamp_xsec = (u64) wall_time->tv_nsec * XSEC_PER_SEC;
	do_div(new_stamp_xsec, 1000000000);
	new_stamp_xsec += (u64) wall_time->tv_sec * XSEC_PER_SEC;

	BUG_ON(wall_time->tv_nsec >= NSEC_PER_SEC);
	/* this is tv_nsec / 1e9 as a 0.32 fraction */
	frac_sec = ((u64) wall_time->tv_nsec * 18446744073ULL) >> 32;

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
	vdso_data->tb_orig_stamp = clock->cycle_last;
	vdso_data->stamp_xsec = new_stamp_xsec;
	vdso_data->tb_to_xs = new_tb_to_xs;
	vdso_data->wtom_clock_sec = wtm->tv_sec;
	vdso_data->wtom_clock_nsec = wtm->tv_nsec;
	vdso_data->stamp_xtime = *wall_time;
	vdso_data->stamp_sec_fraction = frac_sec;
	smp_wmb();
	++(vdso_data->tb_update_count);
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

static void __init clocksource_init(void)
{
	struct clocksource *clock;

	if (__USE_RTC())
		clock = &clocksource_rtc;
	else
		clock = &clocksource_timebase;

	if (clocksource_register_hz(clock, tb_ticks_per_sec)) {
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
	__get_cpu_var(decrementers_next_tb) = get_tb_or_rtc() + evt;
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
	dec->cpumask = cpumask_of(cpu);

	printk_once(KERN_DEBUG "clockevent: %s mult[%x] shift[%d] cpu[%d]\n",
		    dec->name, dec->mult, dec->shift, cpu);

	clockevents_register_device(dec);
}

static void __init init_decrementer_clockevent(void)
{
	int cpu = smp_processor_id();

	clockevents_calc_mult_shift(&decrementer_clockevent, ppc_tb_freq, 4);

	decrementer_clockevent.max_delta_ns =
		clockevent_delta2ns(DECREMENTER_MAX, &decrementer_clockevent);
	decrementer_clockevent.min_delta_ns =
		clockevent_delta2ns(2, &decrementer_clockevent);

	register_decrementer_clockevent(cpu);
}

void secondary_cpu_time_init(void)
{
	/* Start the decrementer on CPUs that have manual control
	 * such as BookE
	 */
	start_cpu_decrementer();

	/* FIME: Should make unrelatred change to move snapshot_timebase
	 * call here ! */
	register_decrementer_clockevent(smp_processor_id());
}

/* This function is only called on the boot processor */
void __init time_init(void)
{
	struct div_result res;
	u64 scale;
	unsigned shift;

	if (__USE_RTC()) {
		/* 601 processor: dec counts down by 128 every 128ns */
		ppc_tb_freq = 1000000000;
	} else {
		/* Normal PowerPC with timebase register */
		ppc_md.calibrate_decr();
		printk(KERN_DEBUG "time_init: decrementer frequency = %lu.%.6lu MHz\n",
		       ppc_tb_freq / 1000000, ppc_tb_freq % 1000000);
		printk(KERN_DEBUG "time_init: processor frequency   = %lu.%.6lu MHz\n",
		       ppc_proc_freq / 1000000, ppc_proc_freq % 1000000);
	}

	tb_ticks_per_jiffy = ppc_tb_freq / HZ;
	tb_ticks_per_sec = ppc_tb_freq;
	tb_ticks_per_usec = ppc_tb_freq / 1000000;
	calc_cputime_factors();
	setup_cputime_one_jiffy();

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

	/* If platform provided a timezone (pmac), we correct the time */
	if (timezone_offset) {
		sys_tz.tz_minuteswest = -timezone_offset / 60;
		sys_tz.tz_dsttime = 0;
	}

	vdso_data->tb_update_count = 0;
	vdso_data->tb_ticks_per_sec = tb_ticks_per_sec;

	/* Start the decrementer on CPUs that have manual control
	 * such as BookE
	 */
	start_cpu_decrementer();

	/* Register the clocksource */
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

/* We don't need to calibrate delay, we use the CPU timebase for that */
void calibrate_delay(void)
{
	/* Some generic code (such as spinlock debug) use loops_per_jiffy
	 * as the number of __delay(1) in a jiffy, so make it so
	 */
	loops_per_jiffy = tb_ticks_per_jiffy;
}

static int __init rtc_init(void)
{
	struct platform_device *pdev;

	if (!ppc_md.get_rtc_time)
		return -ENODEV;

	pdev = platform_device_register_simple("rtc-generic", -1, NULL, 0);
	if (IS_ERR(pdev))
		return PTR_ERR(pdev);

	return 0;
}

module_init(rtc_init);
