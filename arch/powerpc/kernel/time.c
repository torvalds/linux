// SPDX-License-Identifier: GPL-2.0-or-later
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
 */

#include <linux/errno.h>
#include <linux/export.h>
#include <linux/sched.h>
#include <linux/sched/clock.h>
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
#include <linux/of_clk.h>
#include <linux/suspend.h>
#include <linux/sched/cputime.h>
#include <linux/sched/clock.h>
#include <linux/processor.h>
#include <asm/trace.h>

#include <asm/interrupt.h>
#include <asm/io.h>
#include <asm/nvram.h>
#include <asm/cache.h>
#include <asm/machdep.h>
#include <linux/uaccess.h>
#include <asm/time.h>
#include <asm/prom.h>
#include <asm/irq.h>
#include <asm/div64.h>
#include <asm/smp.h>
#include <asm/vdso_datapage.h>
#include <asm/firmware.h>
#include <asm/asm-prototypes.h>

/* powerpc clocksource/clockevent code */

#include <linux/clockchips.h>
#include <linux/timekeeper_internal.h>

static u64 timebase_read(struct clocksource *);
static struct clocksource clocksource_timebase = {
	.name         = "timebase",
	.rating       = 400,
	.flags        = CLOCK_SOURCE_IS_CONTINUOUS,
	.mask         = CLOCKSOURCE_MASK(64),
	.read         = timebase_read,
	.vdso_clock_mode	= VDSO_CLOCKMODE_ARCHTIMER,
};

#define DECREMENTER_DEFAULT_MAX 0x7FFFFFFF
u64 decrementer_max = DECREMENTER_DEFAULT_MAX;

static int decrementer_set_next_event(unsigned long evt,
				      struct clock_event_device *dev);
static int decrementer_shutdown(struct clock_event_device *evt);

struct clock_event_device decrementer_clockevent = {
	.name			= "decrementer",
	.rating			= 200,
	.irq			= 0,
	.set_next_event		= decrementer_set_next_event,
	.set_state_oneshot_stopped = decrementer_shutdown,
	.set_state_shutdown	= decrementer_shutdown,
	.tick_resume		= decrementer_shutdown,
	.features		= CLOCK_EVT_FEAT_ONESHOT |
				  CLOCK_EVT_FEAT_C3STOP,
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

bool tb_invalid;

#ifdef CONFIG_VIRT_CPU_ACCOUNTING_NATIVE
/*
 * Factor for converting from cputime_t (timebase ticks) to
 * microseconds. This is stored as 0.64 fixed-point binary fraction.
 */
u64 __cputime_usec_factor;
EXPORT_SYMBOL(__cputime_usec_factor);

#ifdef CONFIG_PPC_SPLPAR
void (*dtl_consumer)(struct dtl_entry *, u64);
#endif

static void calc_cputime_factors(void)
{
	struct div_result res;

	div128_by_32(1000000, 0, tb_ticks_per_sec, &res);
	__cputime_usec_factor = res.result_low;
}

/*
 * Read the SPURR on systems that have it, otherwise the PURR,
 * or if that doesn't exist return the timebase value passed in.
 */
static inline unsigned long read_spurr(unsigned long tb)
{
	if (cpu_has_feature(CPU_FTR_SPURR))
		return mfspr(SPRN_SPURR);
	if (cpu_has_feature(CPU_FTR_PURR))
		return mfspr(SPRN_PURR);
	return tb;
}

#ifdef CONFIG_PPC_SPLPAR

#include <asm/dtl.h>

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

	if (i == be64_to_cpu(vpa->dtl_idx))
		return 0;
	while (i < be64_to_cpu(vpa->dtl_idx)) {
		dtb = be64_to_cpu(dtl->timebase);
		tb_delta = be32_to_cpu(dtl->enqueue_to_dispatch_time) +
			be32_to_cpu(dtl->ready_to_enqueue_time);
		barrier();
		if (i + N_DISPATCH_LOG < be64_to_cpu(vpa->dtl_idx)) {
			/* buffer has overflowed */
			i = be64_to_cpu(vpa->dtl_idx) - N_DISPATCH_LOG;
			dtl = local_paca->dispatch_log + (i % N_DISPATCH_LOG);
			continue;
		}
		if (dtb > stop_tb)
			break;
		if (dtl_consumer)
			dtl_consumer(dtl, i);
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
void notrace accumulate_stolen_time(void)
{
	u64 sst, ust;
	struct cpu_accounting_data *acct = &local_paca->accounting;

	sst = scan_dispatch_log(acct->starttime_user);
	ust = scan_dispatch_log(acct->starttime);
	acct->stime -= sst;
	acct->utime -= ust;
	acct->steal_time += ust + sst;
}

static inline u64 calculate_stolen_time(u64 stop_tb)
{
	if (!firmware_has_feature(FW_FEATURE_SPLPAR))
		return 0;

	if (get_paca()->dtl_ridx != be64_to_cpu(get_lppaca()->dtl_idx))
		return scan_dispatch_log(stop_tb);

	return 0;
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
static unsigned long vtime_delta_scaled(struct cpu_accounting_data *acct,
					unsigned long now, unsigned long stime)
{
	unsigned long stime_scaled = 0;
#ifdef CONFIG_ARCH_HAS_SCALED_CPUTIME
	unsigned long nowscaled, deltascaled;
	unsigned long utime, utime_scaled;

	nowscaled = read_spurr(now);
	deltascaled = nowscaled - acct->startspurr;
	acct->startspurr = nowscaled;
	utime = acct->utime - acct->utime_sspurr;
	acct->utime_sspurr = acct->utime;

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
	stime_scaled = stime;
	utime_scaled = utime;
	if (deltascaled != stime + utime) {
		if (utime) {
			stime_scaled = deltascaled * stime / (stime + utime);
			utime_scaled = deltascaled - stime_scaled;
		} else {
			stime_scaled = deltascaled;
		}
	}
	acct->utime_scaled += utime_scaled;
#endif

	return stime_scaled;
}

static unsigned long vtime_delta(struct cpu_accounting_data *acct,
				 unsigned long *stime_scaled,
				 unsigned long *steal_time)
{
	unsigned long now, stime;

	WARN_ON_ONCE(!irqs_disabled());

	now = mftb();
	stime = now - acct->starttime;
	acct->starttime = now;

	*stime_scaled = vtime_delta_scaled(acct, now, stime);

	*steal_time = calculate_stolen_time(now);

	return stime;
}

static void vtime_delta_kernel(struct cpu_accounting_data *acct,
			       unsigned long *stime, unsigned long *stime_scaled)
{
	unsigned long steal_time;

	*stime = vtime_delta(acct, stime_scaled, &steal_time);
	*stime -= min(*stime, steal_time);
	acct->steal_time += steal_time;
}

void vtime_account_kernel(struct task_struct *tsk)
{
	struct cpu_accounting_data *acct = get_accounting(tsk);
	unsigned long stime, stime_scaled;

	vtime_delta_kernel(acct, &stime, &stime_scaled);

	if (tsk->flags & PF_VCPU) {
		acct->gtime += stime;
#ifdef CONFIG_ARCH_HAS_SCALED_CPUTIME
		acct->utime_scaled += stime_scaled;
#endif
	} else {
		acct->stime += stime;
#ifdef CONFIG_ARCH_HAS_SCALED_CPUTIME
		acct->stime_scaled += stime_scaled;
#endif
	}
}
EXPORT_SYMBOL_GPL(vtime_account_kernel);

void vtime_account_idle(struct task_struct *tsk)
{
	unsigned long stime, stime_scaled, steal_time;
	struct cpu_accounting_data *acct = get_accounting(tsk);

	stime = vtime_delta(acct, &stime_scaled, &steal_time);
	acct->idle_time += stime + steal_time;
}

static void vtime_account_irq_field(struct cpu_accounting_data *acct,
				    unsigned long *field)
{
	unsigned long stime, stime_scaled;

	vtime_delta_kernel(acct, &stime, &stime_scaled);
	*field += stime;
#ifdef CONFIG_ARCH_HAS_SCALED_CPUTIME
	acct->stime_scaled += stime_scaled;
#endif
}

void vtime_account_softirq(struct task_struct *tsk)
{
	struct cpu_accounting_data *acct = get_accounting(tsk);
	vtime_account_irq_field(acct, &acct->softirq_time);
}

void vtime_account_hardirq(struct task_struct *tsk)
{
	struct cpu_accounting_data *acct = get_accounting(tsk);
	vtime_account_irq_field(acct, &acct->hardirq_time);
}

static void vtime_flush_scaled(struct task_struct *tsk,
			       struct cpu_accounting_data *acct)
{
#ifdef CONFIG_ARCH_HAS_SCALED_CPUTIME
	if (acct->utime_scaled)
		tsk->utimescaled += cputime_to_nsecs(acct->utime_scaled);
	if (acct->stime_scaled)
		tsk->stimescaled += cputime_to_nsecs(acct->stime_scaled);

	acct->utime_scaled = 0;
	acct->utime_sspurr = 0;
	acct->stime_scaled = 0;
#endif
}

/*
 * Account the whole cputime accumulated in the paca
 * Must be called with interrupts disabled.
 * Assumes that vtime_account_kernel/idle() has been called
 * recently (i.e. since the last entry from usermode) so that
 * get_paca()->user_time_scaled is up to date.
 */
void vtime_flush(struct task_struct *tsk)
{
	struct cpu_accounting_data *acct = get_accounting(tsk);

	if (acct->utime)
		account_user_time(tsk, cputime_to_nsecs(acct->utime));

	if (acct->gtime)
		account_guest_time(tsk, cputime_to_nsecs(acct->gtime));

	if (IS_ENABLED(CONFIG_PPC_SPLPAR) && acct->steal_time) {
		account_steal_time(cputime_to_nsecs(acct->steal_time));
		acct->steal_time = 0;
	}

	if (acct->idle_time)
		account_idle_time(cputime_to_nsecs(acct->idle_time));

	if (acct->stime)
		account_system_index_time(tsk, cputime_to_nsecs(acct->stime),
					  CPUTIME_SYSTEM);

	if (acct->hardirq_time)
		account_system_index_time(tsk, cputime_to_nsecs(acct->hardirq_time),
					  CPUTIME_IRQ);
	if (acct->softirq_time)
		account_system_index_time(tsk, cputime_to_nsecs(acct->softirq_time),
					  CPUTIME_SOFTIRQ);

	vtime_flush_scaled(tsk, acct);

	acct->utime = 0;
	acct->gtime = 0;
	acct->idle_time = 0;
	acct->stime = 0;
	acct->hardirq_time = 0;
	acct->softirq_time = 0;
}

#else /* ! CONFIG_VIRT_CPU_ACCOUNTING_NATIVE */
#define calc_cputime_factors()
#endif

void __delay(unsigned long loops)
{
	unsigned long start;

	spin_begin();
	if (tb_invalid) {
		/*
		 * TB is in error state and isn't ticking anymore.
		 * HMI handler was unable to recover from TB error.
		 * Return immediately, so that kernel won't get stuck here.
		 */
		spin_cpu_relax();
	} else {
		start = mftb();
		while (mftb() - start < loops)
			spin_cpu_relax();
	}
	spin_end();
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

#define set_irq_work_pending_flag()	__this_cpu_write(irq_work_pending, 1)
#define test_irq_work_pending()		__this_cpu_read(irq_work_pending)
#define clear_irq_work_pending()	__this_cpu_write(irq_work_pending, 0)

#endif /* 32 vs 64 bit */

void arch_irq_work_raise(void)
{
	/*
	 * 64-bit code that uses irq soft-mask can just cause an immediate
	 * interrupt here that gets soft masked, if this is called under
	 * local_irq_disable(). It might be possible to prevent that happening
	 * by noticing interrupts are disabled and setting decrementer pending
	 * to be replayed when irqs are enabled. The problem there is that
	 * tracing can call irq_work_raise, including in code that does low
	 * level manipulations of irq soft-mask state (e.g., trace_hardirqs_on)
	 * which could get tangled up if we're messing with the same state
	 * here.
	 */
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
DEFINE_INTERRUPT_HANDLER_ASYNC(timer_interrupt)
{
	struct clock_event_device *evt = this_cpu_ptr(&decrementers);
	u64 *next_tb = this_cpu_ptr(&decrementers_next_tb);
	struct pt_regs *old_regs;
	u64 now;

	/*
	 * Some implementations of hotplug will get timer interrupts while
	 * offline, just ignore these.
	 */
	if (unlikely(!cpu_online(smp_processor_id()))) {
		set_dec(decrementer_max);
		return;
	}

	/* Ensure a positive value is written to the decrementer, or else
	 * some CPUs will continue to take decrementer exceptions. When the
	 * PPC_WATCHDOG (decrementer based) is configured, keep this at most
	 * 31 bits, which is about 4 seconds on most systems, which gives
	 * the watchdog a chance of catching timer interrupt hard lockups.
	 */
	if (IS_ENABLED(CONFIG_PPC_WATCHDOG))
		set_dec(0x7fffffff);
	else
		set_dec(decrementer_max);

	/* Conditionally hard-enable interrupts now that the DEC has been
	 * bumped to its maximum value
	 */
	may_hard_irq_enable();


#if defined(CONFIG_PPC32) && defined(CONFIG_PPC_PMAC)
	if (atomic_read(&ppc_n_lost_interrupts) != 0)
		do_IRQ(regs);
#endif

	old_regs = set_irq_regs(regs);

	trace_timer_interrupt_entry(regs);

	if (test_irq_work_pending()) {
		clear_irq_work_pending();
		irq_work_run();
	}

	now = get_tb();
	if (now >= *next_tb) {
		*next_tb = ~(u64)0;
		if (evt->event_handler)
			evt->event_handler(evt);
		__this_cpu_inc(irq_stat.timer_irqs_event);
	} else {
		now = *next_tb - now;
		if (now <= decrementer_max)
			set_dec(now);
		/* We may have raced with new irq work */
		if (test_irq_work_pending())
			set_dec(1);
		__this_cpu_inc(irq_stat.timer_irqs_others);
	}

	trace_timer_interrupt_exit(regs);

	set_irq_regs(old_regs);
}
EXPORT_SYMBOL(timer_interrupt);

#ifdef CONFIG_GENERIC_CLOCKEVENTS_BROADCAST
void timer_broadcast_interrupt(void)
{
	u64 *next_tb = this_cpu_ptr(&decrementers_next_tb);

	*next_tb = ~(u64)0;
	tick_receive_broadcast();
	__this_cpu_inc(irq_stat.broadcast_irqs_event);
}
#endif

#ifdef CONFIG_SUSPEND
static void generic_suspend_disable_irqs(void)
{
	/* Disable the decrementer, so that it doesn't interfere
	 * with suspending.
	 */

	set_dec(decrementer_max);
	local_irq_disable();
	set_dec(decrementer_max);
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

unsigned long long tb_to_ns(unsigned long long ticks)
{
	return mulhdu(ticks, tb_to_ns_scale) << tb_to_ns_shift;
}
EXPORT_SYMBOL_GPL(tb_to_ns);

/*
 * Scheduler clock - returns current time in nanosec units.
 *
 * Note: mulhdu(a, b) (multiply high double unsigned) returns
 * the high 64 bits of a * b, i.e. (a * b) >> 64, where a and b
 * are 64-bit unsigned numbers.
 */
notrace unsigned long long sched_clock(void)
{
	return mulhdu(get_tb() - boot_tb, tb_to_ns_scale) << tb_to_ns_shift;
}


#ifdef CONFIG_PPC_PSERIES

/*
 * Running clock - attempts to give a view of time passing for a virtualised
 * kernels.
 * Uses the VTB register if available otherwise a next best guess.
 */
unsigned long long running_clock(void)
{
	/*
	 * Don't read the VTB as a host since KVM does not switch in host
	 * timebase into the VTB when it takes a guest off the CPU, reading the
	 * VTB would result in reading 'last switched out' guest VTB.
	 *
	 * Host kernels are often compiled with CONFIG_PPC_PSERIES checked, it
	 * would be unsafe to rely only on the #ifdef above.
	 */
	if (firmware_has_feature(FW_FEATURE_LPAR) &&
	    cpu_has_feature(CPU_FTR_ARCH_207S))
		return mulhdu(get_vtb() - boot_tb, tb_to_ns_scale) << tb_to_ns_shift;

	/*
	 * This is a next best approximation without a VTB.
	 * On a host which is running bare metal there should never be any stolen
	 * time and on a host which doesn't do any virtualisation TB *should* equal
	 * VTB so it makes no difference anyway.
	 */
	return local_clock() - kcpustat_this_cpu->cpustat[CPUTIME_STEAL];
}
#endif

static int __init get_freq(char *name, int cells, unsigned long *val)
{
	struct device_node *cpu;
	const __be32 *fp;
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

static void start_cpu_decrementer(void)
{
#if defined(CONFIG_BOOKE) || defined(CONFIG_40x)
	unsigned int tcr;

	/* Clear any pending timer interrupts */
	mtspr(SPRN_TSR, TSR_ENW | TSR_WIS | TSR_DIS | TSR_FIS);

	tcr = mfspr(SPRN_TCR);
	/*
	 * The watchdog may have already been enabled by u-boot. So leave
	 * TRC[WP] (Watchdog Period) alone.
	 */
	tcr &= TCR_WP_MASK;	/* Clear all bits except for TCR[WP] */
	tcr |= TCR_DIE;		/* Enable decrementer */
	mtspr(SPRN_TCR, tcr);
#endif
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

int update_persistent_clock64(struct timespec64 now)
{
	struct rtc_time tm;

	if (!ppc_md.set_rtc_time)
		return -ENODEV;

	rtc_time64_to_tm(now.tv_sec + 1 + timezone_offset, &tm);

	return ppc_md.set_rtc_time(&tm);
}

static void __read_persistent_clock(struct timespec64 *ts)
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

	ts->tv_sec = rtc_tm_to_time64(&tm);
}

void read_persistent_clock64(struct timespec64 *ts)
{
	__read_persistent_clock(ts);

	/* Sanitize it in case real time clock is set below EPOCH */
	if (ts->tv_sec < 0) {
		ts->tv_sec = 0;
		ts->tv_nsec = 0;
	}
		
}

/* clocksource code */
static notrace u64 timebase_read(struct clocksource *cs)
{
	return (u64)get_tb();
}

static void __init clocksource_init(void)
{
	struct clocksource *clock = &clocksource_timebase;

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
	__this_cpu_write(decrementers_next_tb, get_tb() + evt);
	set_dec(evt);

	/* We may have raced with new irq work */
	if (test_irq_work_pending())
		set_dec(1);

	return 0;
}

static int decrementer_shutdown(struct clock_event_device *dev)
{
	decrementer_set_next_event(decrementer_max, dev);
	return 0;
}

static void register_decrementer_clockevent(int cpu)
{
	struct clock_event_device *dec = &per_cpu(decrementers, cpu);

	*dec = decrementer_clockevent;
	dec->cpumask = cpumask_of(cpu);

	clockevents_config_and_register(dec, ppc_tb_freq, 2, decrementer_max);

	printk_once(KERN_DEBUG "clockevent: %s mult[%x] shift[%d] cpu[%d]\n",
		    dec->name, dec->mult, dec->shift, cpu);

	/* Set values for KVM, see kvm_emulate_dec() */
	decrementer_clockevent.mult = dec->mult;
	decrementer_clockevent.shift = dec->shift;
}

static void enable_large_decrementer(void)
{
	if (!cpu_has_feature(CPU_FTR_ARCH_300))
		return;

	if (decrementer_max <= DECREMENTER_DEFAULT_MAX)
		return;

	/*
	 * If we're running as the hypervisor we need to enable the LD manually
	 * otherwise firmware should have done it for us.
	 */
	if (cpu_has_feature(CPU_FTR_HVMODE))
		mtspr(SPRN_LPCR, mfspr(SPRN_LPCR) | LPCR_LD);
}

static void __init set_decrementer_max(void)
{
	struct device_node *cpu;
	u32 bits = 32;

	/* Prior to ISAv3 the decrementer is always 32 bit */
	if (!cpu_has_feature(CPU_FTR_ARCH_300))
		return;

	cpu = of_find_node_by_type(NULL, "cpu");

	if (of_property_read_u32(cpu, "ibm,dec-bits", &bits) == 0) {
		if (bits > 64 || bits < 32) {
			pr_warn("time_init: firmware supplied invalid ibm,dec-bits");
			bits = 32;
		}

		/* calculate the signed maximum given this many bits */
		decrementer_max = (1ul << (bits - 1)) - 1;
	}

	of_node_put(cpu);

	pr_info("time_init: %u bit decrementer (max: %llx)\n",
		bits, decrementer_max);
}

static void __init init_decrementer_clockevent(void)
{
	register_decrementer_clockevent(smp_processor_id());
}

void secondary_cpu_time_init(void)
{
	/* Enable and test the large decrementer for this cpu */
	enable_large_decrementer();

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

	/* Normal PowerPC with timebase register */
	ppc_md.calibrate_decr();
	printk(KERN_DEBUG "time_init: decrementer frequency = %lu.%.6lu MHz\n",
	       ppc_tb_freq / 1000000, ppc_tb_freq % 1000000);
	printk(KERN_DEBUG "time_init: processor frequency   = %lu.%.6lu MHz\n",
	       ppc_proc_freq / 1000000, ppc_proc_freq % 1000000);

	tb_ticks_per_jiffy = ppc_tb_freq / HZ;
	tb_ticks_per_sec = ppc_tb_freq;
	tb_ticks_per_usec = ppc_tb_freq / 1000000;
	calc_cputime_factors();

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
	boot_tb = get_tb();

	/* If platform provided a timezone (pmac), we correct the time */
	if (timezone_offset) {
		sys_tz.tz_minuteswest = -timezone_offset / 60;
		sys_tz.tz_dsttime = 0;
	}

	vdso_data->tb_ticks_per_sec = tb_ticks_per_sec;

	/* initialise and enable the large decrementer (if we have one) */
	set_decrementer_max();
	enable_large_decrementer();

	/* Start the decrementer on CPUs that have manual control
	 * such as BookE
	 */
	start_cpu_decrementer();

	/* Register the clocksource */
	clocksource_init();

	init_decrementer_clockevent();
	tick_setup_hrtimer_broadcast();

	of_clk_init(NULL);
	enable_sched_clock_irqtime();
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

#if IS_ENABLED(CONFIG_RTC_DRV_GENERIC)
static int rtc_generic_get_time(struct device *dev, struct rtc_time *tm)
{
	ppc_md.get_rtc_time(tm);
	return 0;
}

static int rtc_generic_set_time(struct device *dev, struct rtc_time *tm)
{
	if (!ppc_md.set_rtc_time)
		return -EOPNOTSUPP;

	if (ppc_md.set_rtc_time(tm) < 0)
		return -EOPNOTSUPP;

	return 0;
}

static const struct rtc_class_ops rtc_generic_ops = {
	.read_time = rtc_generic_get_time,
	.set_time = rtc_generic_set_time,
};

static int __init rtc_init(void)
{
	struct platform_device *pdev;

	if (!ppc_md.get_rtc_time)
		return -ENODEV;

	pdev = platform_device_register_data(NULL, "rtc-generic", -1,
					     &rtc_generic_ops,
					     sizeof(rtc_generic_ops));

	return PTR_ERR_OR_ZERO(pdev);
}

device_initcall(rtc_init);
#endif
