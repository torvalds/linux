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

#include <linux/config.h>
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
#ifdef CONFIG_PPC64
#include <asm/firmware.h>
#endif
#ifdef CONFIG_PPC_ISERIES
#include <asm/iseries/it_lp_queue.h>
#include <asm/iseries/hv_call_xm.h>
#endif
#include <asm/smp.h>

/* keep track of when we need to update the rtc */
time_t last_rtc_update;
extern int piranha_simulator;
#ifdef CONFIG_PPC_ISERIES
unsigned long iSeries_recal_titan = 0;
unsigned long iSeries_recal_tb = 0; 
static unsigned long first_settimeofday = 1;
#endif

/* The decrementer counts down by 128 every 128ns on a 601. */
#define DECREMENTER_COUNT_601	(1000000000 / HZ)

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
u64 tb_to_xs;
unsigned tb_to_us;

#define TICKLEN_SCALE	(SHIFT_SCALE - 10)
u64 last_tick_len;	/* units are ns / 2^TICKLEN_SCALE */
u64 ticklen_to_xs;	/* 0.64 fraction */

/* If last_tick_len corresponds to about 1/HZ seconds, then
   last_tick_len << TICKLEN_SHIFT will be about 2^63. */
#define TICKLEN_SHIFT	(63 - 30 - TICKLEN_SCALE + SHIFT_HZ)

DEFINE_SPINLOCK(rtc_lock);
EXPORT_SYMBOL_GPL(rtc_lock);

u64 tb_to_ns_scale;
unsigned tb_to_ns_shift;

struct gettimeofday_struct do_gtod;

extern unsigned long wall_jiffies;

extern struct timezone sys_tz;
static long timezone_offset;

unsigned long ppc_proc_freq;
unsigned long ppc_tb_freq;

u64 tb_last_jiffy __cacheline_aligned_in_smp;
unsigned long tb_last_stamp;

/*
 * Note that on ppc32 this only stores the bottom 32 bits of
 * the timebase value, but that's enough to tell when a jiffy
 * has passed.
 */
DEFINE_PER_CPU(unsigned long, last_jiffy);

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

static __inline__ void timer_check_rtc(void)
{
        /*
         * update the rtc when needed, this should be performed on the
         * right fraction of a second. Half or full second ?
         * Full second works on mk48t59 clocks, others need testing.
         * Note that this update is basically only used through 
         * the adjtimex system calls. Setting the HW clock in
         * any other way is a /dev/rtc and userland business.
         * This is still wrong by -0.5/+1.5 jiffies because of the
         * timer interrupt resolution and possible delay, but here we 
         * hit a quantization limit which can only be solved by higher
         * resolution timers and decoupling time management from timer
         * interrupts. This is also wrong on the clocks
         * which require being written at the half second boundary.
         * We should have an rtc call that only sets the minutes and
         * seconds like on Intel to avoid problems with non UTC clocks.
         */
        if (ppc_md.set_rtc_time && ntp_synced() &&
	    xtime.tv_sec - last_rtc_update >= 659 &&
	    abs((xtime.tv_nsec/1000) - (1000000-1000000/HZ)) < 500000/HZ) {
		struct rtc_time tm;
		to_tm(xtime.tv_sec + 1 + timezone_offset, &tm);
		tm.tm_year -= 1900;
		tm.tm_mon -= 1;
		if (ppc_md.set_rtc_time(&tm) == 0)
			last_rtc_update = xtime.tv_sec + 1;
		else
			/* Try again one minute later */
			last_rtc_update += 60;
        }
}

/*
 * This version of gettimeofday has microsecond resolution.
 */
static inline void __do_gettimeofday(struct timeval *tv, u64 tb_val)
{
	unsigned long sec, usec;
	u64 tb_ticks, xsec;
	struct gettimeofday_vars *temp_varp;
	u64 temp_tb_to_xs, temp_stamp_xsec;

	/*
	 * These calculations are faster (gets rid of divides)
	 * if done in units of 1/2^20 rather than microseconds.
	 * The conversion to microseconds at the end is done
	 * without a divide (and in fact, without a multiply)
	 */
	temp_varp = do_gtod.varp;
	tb_ticks = tb_val - temp_varp->tb_orig_stamp;
	temp_tb_to_xs = temp_varp->tb_to_xs;
	temp_stamp_xsec = temp_varp->stamp_xsec;
	xsec = temp_stamp_xsec + mulhdu(tb_ticks, temp_tb_to_xs);
	sec = xsec / XSEC_PER_SEC;
	usec = (unsigned long)xsec & (XSEC_PER_SEC - 1);
	usec = SCALE_XSEC(usec, 1000000);

	tv->tv_sec = sec;
	tv->tv_usec = usec;
}

void do_gettimeofday(struct timeval *tv)
{
	if (__USE_RTC()) {
		/* do this the old way */
		unsigned long flags, seq;
		unsigned int sec, nsec, usec;

		do {
			seq = read_seqbegin_irqsave(&xtime_lock, flags);
			sec = xtime.tv_sec;
			nsec = xtime.tv_nsec + tb_ticks_since(tb_last_stamp);
		} while (read_seqretry_irqrestore(&xtime_lock, seq, flags));
		usec = nsec / 1000;
		while (usec >= 1000000) {
			usec -= 1000000;
			++sec;
		}
		tv->tv_sec = sec;
		tv->tv_usec = usec;
		return;
	}
	__do_gettimeofday(tv, get_tb());
}

EXPORT_SYMBOL(do_gettimeofday);

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

/*
 * When the timebase - tb_orig_stamp gets too big, we do a manipulation
 * between tb_orig_stamp and stamp_xsec. The goal here is to keep the
 * difference tb - tb_orig_stamp small enough to always fit inside a
 * 32 bits number. This is a requirement of our fast 32 bits userland
 * implementation in the vdso. If we "miss" a call to this function
 * (interrupt latency, CPU locked in a spinlock, ...) and we end up
 * with a too big difference, then the vdso will fallback to calling
 * the syscall
 */
static __inline__ void timer_recalc_offset(u64 cur_tb)
{
	unsigned long offset;
	u64 new_stamp_xsec;
	u64 tlen, t2x;
	u64 tb, xsec_old, xsec_new;
	struct gettimeofday_vars *varp;

	if (__USE_RTC())
		return;
	tlen = current_tick_length();
	offset = cur_tb - do_gtod.varp->tb_orig_stamp;
	if (tlen == last_tick_len && offset < 0x80000000u)
		return;
	if (tlen != last_tick_len) {
		t2x = mulhdu(tlen << TICKLEN_SHIFT, ticklen_to_xs);
		last_tick_len = tlen;
	} else
		t2x = do_gtod.varp->tb_to_xs;
	new_stamp_xsec = (u64) xtime.tv_nsec * XSEC_PER_SEC;
	do_div(new_stamp_xsec, 1000000000);
	new_stamp_xsec += (u64) xtime.tv_sec * XSEC_PER_SEC;

	++vdso_data->tb_update_count;
	smp_mb();

	/*
	 * Make sure time doesn't go backwards for userspace gettimeofday.
	 */
	tb = get_tb();
	varp = do_gtod.varp;
	xsec_old = mulhdu(tb - varp->tb_orig_stamp, varp->tb_to_xs)
		+ varp->stamp_xsec;
	xsec_new = mulhdu(tb - cur_tb, t2x) + new_stamp_xsec;
	if (xsec_new < xsec_old)
		new_stamp_xsec += xsec_old - xsec_new;

	update_gtod(cur_tb, new_stamp_xsec, t2x);
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

static void iSeries_tb_recal(void)
{
	struct div_result divres;
	unsigned long titan, tb;
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
}
#endif

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
	int next_dec;
	int cpu = smp_processor_id();
	unsigned long ticks;

#ifdef CONFIG_PPC32
	if (atomic_read(&ppc_n_lost_interrupts) != 0)
		do_IRQ(regs);
#endif

	irq_enter();

	profile_tick(CPU_PROFILING, regs);

#ifdef CONFIG_PPC_ISERIES
	get_lppaca()->int_dword.fields.decr_int = 0;
#endif

	while ((ticks = tb_ticks_since(per_cpu(last_jiffy, cpu)))
	       >= tb_ticks_per_jiffy) {
		/* Update last_jiffy */
		per_cpu(last_jiffy, cpu) += tb_ticks_per_jiffy;
		/* Handle RTCL overflow on 601 */
		if (__USE_RTC() && per_cpu(last_jiffy, cpu) >= 1000000000)
			per_cpu(last_jiffy, cpu) -= 1000000000;

		/*
		 * We cannot disable the decrementer, so in the period
		 * between this cpu's being marked offline in cpu_online_map
		 * and calling stop-self, it is taking timer interrupts.
		 * Avoid calling into the scheduler rebalancing code if this
		 * is the case.
		 */
		if (!cpu_is_offline(cpu))
			update_process_times(user_mode(regs));

		/*
		 * No need to check whether cpu is offline here; boot_cpuid
		 * should have been fixed up by now.
		 */
		if (cpu != boot_cpuid)
			continue;

		write_seqlock(&xtime_lock);
		tb_last_jiffy += tb_ticks_per_jiffy;
		tb_last_stamp = per_cpu(last_jiffy, cpu);
		do_timer(regs);
		timer_recalc_offset(tb_last_jiffy);
		timer_check_rtc();
		write_sequnlock(&xtime_lock);
	}
	
	next_dec = tb_ticks_per_jiffy - ticks;
	set_dec(next_dec);

#ifdef CONFIG_PPC_ISERIES
	if (hvlpevent_is_pending())
		process_hvlpevents(regs);
#endif

#ifdef CONFIG_PPC64
	/* collect purr register values often, for accurate calculations */
	if (firmware_has_feature(FW_FEATURE_SPLPAR)) {
		struct cpu_usage *cu = &__get_cpu_var(cpu_usage_array);
		cu->current_tb = mfspr(SPRN_PURR);
	}
#endif

	irq_exit();
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
	unsigned long offset = tb_ticks_per_jiffy / max_cpus;
	unsigned long previous_tb = per_cpu(last_jiffy, boot_cpuid);

	/* make sure tb > per_cpu(last_jiffy, cpu) for all cpus always */
	previous_tb -= tb_ticks_per_jiffy;
	for_each_cpu(i) {
		if (i != boot_cpuid) {
			previous_tb += offset;
			per_cpu(last_jiffy, i) = previous_tb;
		}
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
	return mulhdu(get_tb(), tb_to_ns_scale) << tb_to_ns_shift;
}

int do_settimeofday(struct timespec *tv)
{
	time_t wtm_sec, new_sec = tv->tv_sec;
	long wtm_nsec, new_nsec = tv->tv_nsec;
	unsigned long flags;
	u64 new_xsec;
	unsigned long tb_delta;

	if ((unsigned long)tv->tv_nsec >= NSEC_PER_SEC)
		return -EINVAL;

	write_seqlock_irqsave(&xtime_lock, flags);

	/*
	 * Updating the RTC is not the job of this code. If the time is
	 * stepped under NTP, the RTC will be updated after STA_UNSYNC
	 * is cleared.  Tools like clock/hwclock either copy the RTC
	 * to the system time, in which case there is no point in writing
	 * to the RTC again, or write to the RTC but then they don't call
	 * settimeofday to perform this operation.
	 */
#ifdef CONFIG_PPC_ISERIES
	if (first_settimeofday) {
		iSeries_tb_recal();
		first_settimeofday = 0;
	}
#endif

	/* Make userspace gettimeofday spin until we're done. */
	++vdso_data->tb_update_count;
	smp_mb();

	/*
	 * Subtract off the number of nanoseconds since the
	 * beginning of the last tick.
	 * Note that since we don't increment jiffies_64 anywhere other
	 * than in do_timer (since we don't have a lost tick problem),
	 * wall_jiffies will always be the same as jiffies,
	 * and therefore the (jiffies - wall_jiffies) computation
	 * has been removed.
	 */
	tb_delta = tb_ticks_since(tb_last_stamp);
	tb_delta = mulhdu(tb_delta, do_gtod.varp->tb_to_xs); /* in xsec */
	new_nsec -= SCALE_XSEC(tb_delta, 1000000000);

	wtm_sec  = wall_to_monotonic.tv_sec + (xtime.tv_sec - new_sec);
	wtm_nsec = wall_to_monotonic.tv_nsec + (xtime.tv_nsec - new_nsec);

 	set_normalized_timespec(&xtime, new_sec, new_nsec);
	set_normalized_timespec(&wall_to_monotonic, wtm_sec, wtm_nsec);

	/* In case of a large backwards jump in time with NTP, we want the 
	 * clock to be updated as soon as the PLL is again in lock.
	 */
	last_rtc_update = new_sec - 658;

	ntp_clear();

	new_xsec = xtime.tv_nsec;
	if (new_xsec != 0) {
		new_xsec *= XSEC_PER_SEC;
		do_div(new_xsec, NSEC_PER_SEC);
	}
	new_xsec += (u64)xtime.tv_sec * XSEC_PER_SEC;
	update_gtod(tb_last_jiffy, new_xsec, do_gtod.varp->tb_to_xs);

	vdso_data->tz_minuteswest = sys_tz.tz_minuteswest;
	vdso_data->tz_dsttime = sys_tz.tz_dsttime;

	write_sequnlock_irqrestore(&xtime_lock, flags);
	clock_was_set();
	return 0;
}

EXPORT_SYMBOL(do_settimeofday);

void __init generic_calibrate_decr(void)
{
	struct device_node *cpu;
	unsigned int *fp;
	int node_found;

	/*
	 * The cpu node should have a timebase-frequency property
	 * to tell us the rate at which the decrementer counts.
	 */
	cpu = of_find_node_by_type(NULL, "cpu");

	ppc_tb_freq = DEFAULT_TB_FREQ;		/* hardcoded default */
	node_found = 0;
	if (cpu) {
		fp = (unsigned int *)get_property(cpu, "timebase-frequency",
						  NULL);
		if (fp) {
			node_found = 1;
			ppc_tb_freq = *fp;
		}
	}
	if (!node_found)
		printk(KERN_ERR "WARNING: Estimating decrementer frequency "
				"(not found)\n");

	ppc_proc_freq = DEFAULT_PROC_FREQ;
	node_found = 0;
	if (cpu) {
		fp = (unsigned int *)get_property(cpu, "clock-frequency",
						  NULL);
		if (fp) {
			node_found = 1;
			ppc_proc_freq = *fp;
		}
	}
#ifdef CONFIG_BOOKE
	/* Set the time base to zero */
	mtspr(SPRN_TBWL, 0);
	mtspr(SPRN_TBWU, 0);

	/* Clear any pending timer interrupts */
	mtspr(SPRN_TSR, TSR_ENW | TSR_WIS | TSR_DIS | TSR_FIS);

	/* Enable decrementer interrupt */
	mtspr(SPRN_TCR, TCR_DIE);
#endif
	if (!node_found)
		printk(KERN_ERR "WARNING: Estimating processor frequency "
				"(not found)\n");

	of_node_put(cpu);
}

unsigned long get_boot_time(void)
{
	struct rtc_time tm;

	if (ppc_md.get_boot_time)
		return ppc_md.get_boot_time();
	if (!ppc_md.get_rtc_time)
		return 0;
	ppc_md.get_rtc_time(&tm);
	return mktime(tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday,
		      tm.tm_hour, tm.tm_min, tm.tm_sec);
}

/* This function is only called on the boot processor */
void __init time_init(void)
{
	unsigned long flags;
	unsigned long tm = 0;
	struct div_result res;
	u64 scale, x;
	unsigned shift;

        if (ppc_md.time_init != NULL)
                timezone_offset = ppc_md.time_init();

	if (__USE_RTC()) {
		/* 601 processor: dec counts down by 128 every 128ns */
		ppc_tb_freq = 1000000000;
		tb_last_stamp = get_rtcl();
		tb_last_jiffy = tb_last_stamp;
	} else {
		/* Normal PowerPC with timebase register */
		ppc_md.calibrate_decr();
		printk(KERN_INFO "time_init: decrementer frequency = %lu.%.6lu MHz\n",
		       ppc_tb_freq / 1000000, ppc_tb_freq % 1000000);
		printk(KERN_INFO "time_init: processor frequency   = %lu.%.6lu MHz\n",
		       ppc_proc_freq / 1000000, ppc_proc_freq % 1000000);
		tb_last_stamp = tb_last_jiffy = get_tb();
	}

	tb_ticks_per_jiffy = ppc_tb_freq / HZ;
	tb_ticks_per_sec = ppc_tb_freq;
	tb_ticks_per_usec = ppc_tb_freq / 1000000;
	tb_to_us = mulhwu_scale_factor(ppc_tb_freq, 1000000);

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

#ifdef CONFIG_PPC_ISERIES
	if (!piranha_simulator)
#endif
		tm = get_boot_time();

	write_seqlock_irqsave(&xtime_lock, flags);

	/* If platform provided a timezone (pmac), we correct the time */
        if (timezone_offset) {
		sys_tz.tz_minuteswest = -timezone_offset / 60;
		sys_tz.tz_dsttime = 0;
		tm -= timezone_offset;
        }

	xtime.tv_sec = tm;
	xtime.tv_nsec = 0;
	do_gtod.varp = &do_gtod.vars[0];
	do_gtod.var_idx = 0;
	do_gtod.varp->tb_orig_stamp = tb_last_jiffy;
	__get_cpu_var(last_jiffy) = tb_last_stamp;
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

	last_rtc_update = xtime.tv_sec;
	set_normalized_timespec(&wall_to_monotonic,
	                        -xtime.tv_sec, -xtime.tv_nsec);
	write_sequnlock_irqrestore(&xtime_lock, flags);

	/* Not exact, but the timer interrupt takes care of this */
	set_dec(tb_ticks_per_jiffy);
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
