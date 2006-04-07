/*
 *  linux/arch/x86-64/kernel/time.c
 *
 *  "High Precision Event Timer" based timekeeping.
 *
 *  Copyright (c) 1991,1992,1995  Linus Torvalds
 *  Copyright (c) 1994  Alan Modra
 *  Copyright (c) 1995  Markus Kuhn
 *  Copyright (c) 1996  Ingo Molnar
 *  Copyright (c) 1998  Andrea Arcangeli
 *  Copyright (c) 2002  Vojtech Pavlik
 *  Copyright (c) 2003  Andi Kleen
 *  RTC support code taken from arch/i386/kernel/timers/time_hpet.c
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/mc146818rtc.h>
#include <linux/time.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/sysdev.h>
#include <linux/bcd.h>
#include <linux/kallsyms.h>
#include <linux/acpi.h>
#ifdef CONFIG_ACPI
#include <acpi/achware.h>	/* for PM timer frequency */
#endif
#include <asm/8253pit.h>
#include <asm/pgtable.h>
#include <asm/vsyscall.h>
#include <asm/timex.h>
#include <asm/proto.h>
#include <asm/hpet.h>
#include <asm/sections.h>
#include <linux/cpufreq.h>
#include <linux/hpet.h>
#ifdef CONFIG_X86_LOCAL_APIC
#include <asm/apic.h>
#endif

#ifdef CONFIG_CPU_FREQ
static void cpufreq_delayed_get(void);
#endif
extern void i8254_timer_resume(void);
extern int using_apic_timer;

static char *time_init_gtod(void);

DEFINE_SPINLOCK(rtc_lock);
DEFINE_SPINLOCK(i8253_lock);

int nohpet __initdata = 0;
static int notsc __initdata = 0;

#undef HPET_HACK_ENABLE_DANGEROUS

unsigned int cpu_khz;					/* TSC clocks / usec, not used here */
static unsigned long hpet_period;			/* fsecs / HPET clock */
unsigned long hpet_tick;				/* HPET clocks / interrupt */
int hpet_use_timer;				/* Use counter of hpet for time keeping, otherwise PIT */
unsigned long vxtime_hz = PIT_TICK_RATE;
int report_lost_ticks;				/* command line option */
unsigned long long monotonic_base;

struct vxtime_data __vxtime __section_vxtime;	/* for vsyscalls */

volatile unsigned long __jiffies __section_jiffies = INITIAL_JIFFIES;
unsigned long __wall_jiffies __section_wall_jiffies = INITIAL_JIFFIES;
struct timespec __xtime __section_xtime;
struct timezone __sys_tz __section_sys_tz;

/*
 * do_gettimeoffset() returns microseconds since last timer interrupt was
 * triggered by hardware. A memory read of HPET is slower than a register read
 * of TSC, but much more reliable. It's also synchronized to the timer
 * interrupt. Note that do_gettimeoffset() may return more than hpet_tick, if a
 * timer interrupt has happened already, but vxtime.trigger wasn't updated yet.
 * This is not a problem, because jiffies hasn't updated either. They are bound
 * together by xtime_lock.
 */

static inline unsigned int do_gettimeoffset_tsc(void)
{
	unsigned long t;
	unsigned long x;
	t = get_cycles_sync();
	if (t < vxtime.last_tsc) 
		t = vxtime.last_tsc; /* hack */
	x = ((t - vxtime.last_tsc) * vxtime.tsc_quot) >> 32;
	return x;
}

static inline unsigned int do_gettimeoffset_hpet(void)
{
	/* cap counter read to one tick to avoid inconsistencies */
	unsigned long counter = hpet_readl(HPET_COUNTER) - vxtime.last;
	return (min(counter,hpet_tick) * vxtime.quot) >> 32;
}

unsigned int (*do_gettimeoffset)(void) = do_gettimeoffset_tsc;

/*
 * This version of gettimeofday() has microsecond resolution and better than
 * microsecond precision, as we're using at least a 10 MHz (usually 14.31818
 * MHz) HPET timer.
 */

void do_gettimeofday(struct timeval *tv)
{
	unsigned long seq, t;
 	unsigned int sec, usec;

	do {
		seq = read_seqbegin(&xtime_lock);

		sec = xtime.tv_sec;
		usec = xtime.tv_nsec / 1000;

		/* i386 does some correction here to keep the clock 
		   monotonous even when ntpd is fixing drift.
		   But they didn't work for me, there is a non monotonic
		   clock anyways with ntp.
		   I dropped all corrections now until a real solution can
		   be found. Note when you fix it here you need to do the same
		   in arch/x86_64/kernel/vsyscall.c and export all needed
		   variables in vmlinux.lds. -AK */ 

		t = (jiffies - wall_jiffies) * (1000000L / HZ) +
			do_gettimeoffset();
		usec += t;

	} while (read_seqretry(&xtime_lock, seq));

	tv->tv_sec = sec + usec / 1000000;
	tv->tv_usec = usec % 1000000;
}

EXPORT_SYMBOL(do_gettimeofday);

/*
 * settimeofday() first undoes the correction that gettimeofday would do
 * on the time, and then saves it. This is ugly, but has been like this for
 * ages already.
 */

int do_settimeofday(struct timespec *tv)
{
	time_t wtm_sec, sec = tv->tv_sec;
	long wtm_nsec, nsec = tv->tv_nsec;

	if ((unsigned long)tv->tv_nsec >= NSEC_PER_SEC)
		return -EINVAL;

	write_seqlock_irq(&xtime_lock);

	nsec -= do_gettimeoffset() * 1000 +
		(jiffies - wall_jiffies) * (NSEC_PER_SEC/HZ);

	wtm_sec  = wall_to_monotonic.tv_sec + (xtime.tv_sec - sec);
	wtm_nsec = wall_to_monotonic.tv_nsec + (xtime.tv_nsec - nsec);

	set_normalized_timespec(&xtime, sec, nsec);
	set_normalized_timespec(&wall_to_monotonic, wtm_sec, wtm_nsec);

	ntp_clear();

	write_sequnlock_irq(&xtime_lock);
	clock_was_set();
	return 0;
}

EXPORT_SYMBOL(do_settimeofday);

unsigned long profile_pc(struct pt_regs *regs)
{
	unsigned long pc = instruction_pointer(regs);

	/* Assume the lock function has either no stack frame or only a single 
	   word.  This checks if the address on the stack looks like a kernel 
	   text address.
	   There is a small window for false hits, but in that case the tick
	   is just accounted to the spinlock function.
	   Better would be to write these functions in assembler again
	   and check exactly. */
	if (in_lock_functions(pc)) {
		char *v = *(char **)regs->rsp;
		if ((v >= _stext && v <= _etext) ||
			(v >= _sinittext && v <= _einittext) ||
			(v >= (char *)MODULES_VADDR  && v <= (char *)MODULES_END))
			return (unsigned long)v;
		return ((unsigned long *)regs->rsp)[1];
	}
	return pc;
}
EXPORT_SYMBOL(profile_pc);

/*
 * In order to set the CMOS clock precisely, set_rtc_mmss has to be called 500
 * ms after the second nowtime has started, because when nowtime is written
 * into the registers of the CMOS clock, it will jump to the next second
 * precisely 500 ms later. Check the Motorola MC146818A or Dallas DS12887 data
 * sheet for details.
 */

static void set_rtc_mmss(unsigned long nowtime)
{
	int real_seconds, real_minutes, cmos_minutes;
	unsigned char control, freq_select;

/*
 * IRQs are disabled when we're called from the timer interrupt,
 * no need for spin_lock_irqsave()
 */

	spin_lock(&rtc_lock);

/*
 * Tell the clock it's being set and stop it.
 */

	control = CMOS_READ(RTC_CONTROL);
	CMOS_WRITE(control | RTC_SET, RTC_CONTROL);

	freq_select = CMOS_READ(RTC_FREQ_SELECT);
	CMOS_WRITE(freq_select | RTC_DIV_RESET2, RTC_FREQ_SELECT);

	cmos_minutes = CMOS_READ(RTC_MINUTES);
		BCD_TO_BIN(cmos_minutes);

/*
 * since we're only adjusting minutes and seconds, don't interfere with hour
 * overflow. This avoids messing with unknown time zones but requires your RTC
 * not to be off by more than 15 minutes. Since we're calling it only when
 * our clock is externally synchronized using NTP, this shouldn't be a problem.
 */

	real_seconds = nowtime % 60;
	real_minutes = nowtime / 60;
	if (((abs(real_minutes - cmos_minutes) + 15) / 30) & 1)
		real_minutes += 30;		/* correct for half hour time zone */
	real_minutes %= 60;

	if (abs(real_minutes - cmos_minutes) >= 30) {
		printk(KERN_WARNING "time.c: can't update CMOS clock "
		       "from %d to %d\n", cmos_minutes, real_minutes);
	} else {
		BIN_TO_BCD(real_seconds);
		BIN_TO_BCD(real_minutes);
		CMOS_WRITE(real_seconds, RTC_SECONDS);
		CMOS_WRITE(real_minutes, RTC_MINUTES);
	}

/*
 * The following flags have to be released exactly in this order, otherwise the
 * DS12887 (popular MC146818A clone with integrated battery and quartz) will
 * not reset the oscillator and will not update precisely 500 ms later. You
 * won't find this mentioned in the Dallas Semiconductor data sheets, but who
 * believes data sheets anyway ... -- Markus Kuhn
 */

	CMOS_WRITE(control, RTC_CONTROL);
	CMOS_WRITE(freq_select, RTC_FREQ_SELECT);

	spin_unlock(&rtc_lock);
}


/* monotonic_clock(): returns # of nanoseconds passed since time_init()
 *		Note: This function is required to return accurate
 *		time even in the absence of multiple timer ticks.
 */
unsigned long long monotonic_clock(void)
{
	unsigned long seq;
 	u32 last_offset, this_offset, offset;
	unsigned long long base;

	if (vxtime.mode == VXTIME_HPET) {
		do {
			seq = read_seqbegin(&xtime_lock);

			last_offset = vxtime.last;
			base = monotonic_base;
			this_offset = hpet_readl(HPET_COUNTER);
		} while (read_seqretry(&xtime_lock, seq));
		offset = (this_offset - last_offset);
		offset *= (NSEC_PER_SEC/HZ) / hpet_tick;
	} else {
		do {
			seq = read_seqbegin(&xtime_lock);

			last_offset = vxtime.last_tsc;
			base = monotonic_base;
		} while (read_seqretry(&xtime_lock, seq));
		this_offset = get_cycles_sync();
		offset = (this_offset - last_offset)*1000 / cpu_khz; 
	}
	return base + offset;
}
EXPORT_SYMBOL(monotonic_clock);

static noinline void handle_lost_ticks(int lost, struct pt_regs *regs)
{
	static long lost_count;
	static int warned;
	if (report_lost_ticks) {
		printk(KERN_WARNING "time.c: Lost %d timer tick(s)! ", lost);
		print_symbol("rip %s)\n", regs->rip);
	}

	if (lost_count == 1000 && !warned) {
		printk(KERN_WARNING "warning: many lost ticks.\n"
		       KERN_WARNING "Your time source seems to be instable or "
		   		"some driver is hogging interupts\n");
		print_symbol("rip %s\n", regs->rip);
		if (vxtime.mode == VXTIME_TSC && vxtime.hpet_address) {
			printk(KERN_WARNING "Falling back to HPET\n");
			if (hpet_use_timer)
				vxtime.last = hpet_readl(HPET_T0_CMP) - 
							hpet_tick;
			else
				vxtime.last = hpet_readl(HPET_COUNTER);
			vxtime.mode = VXTIME_HPET;
			do_gettimeoffset = do_gettimeoffset_hpet;
		}
		/* else should fall back to PIT, but code missing. */
		warned = 1;
	} else
		lost_count++;

#ifdef CONFIG_CPU_FREQ
	/* In some cases the CPU can change frequency without us noticing
	   Give cpufreq a change to catch up. */
	if ((lost_count+1) % 25 == 0)
		cpufreq_delayed_get();
#endif
}

void main_timer_handler(struct pt_regs *regs)
{
	static unsigned long rtc_update = 0;
	unsigned long tsc;
	int delay = 0, offset = 0, lost = 0;

/*
 * Here we are in the timer irq handler. We have irqs locally disabled (so we
 * don't need spin_lock_irqsave()) but we don't know if the timer_bh is running
 * on the other CPU, so we need a lock. We also need to lock the vsyscall
 * variables, because both do_timer() and us change them -arca+vojtech
 */

	write_seqlock(&xtime_lock);

	if (vxtime.hpet_address)
		offset = hpet_readl(HPET_COUNTER);

	if (hpet_use_timer) {
		/* if we're using the hpet timer functionality,
		 * we can more accurately know the counter value
		 * when the timer interrupt occured.
		 */
		offset = hpet_readl(HPET_T0_CMP) - hpet_tick;
		delay = hpet_readl(HPET_COUNTER) - offset;
	} else if (!pmtmr_ioport) {
		spin_lock(&i8253_lock);
		outb_p(0x00, 0x43);
		delay = inb_p(0x40);
		delay |= inb(0x40) << 8;
		spin_unlock(&i8253_lock);
		delay = LATCH - 1 - delay;
	}

	tsc = get_cycles_sync();

	if (vxtime.mode == VXTIME_HPET) {
		if (offset - vxtime.last > hpet_tick) {
			lost = (offset - vxtime.last) / hpet_tick - 1;
		}

		monotonic_base += 
			(offset - vxtime.last)*(NSEC_PER_SEC/HZ) / hpet_tick;

		vxtime.last = offset;
#ifdef CONFIG_X86_PM_TIMER
	} else if (vxtime.mode == VXTIME_PMTMR) {
		lost = pmtimer_mark_offset();
#endif
	} else {
		offset = (((tsc - vxtime.last_tsc) *
			   vxtime.tsc_quot) >> 32) - (USEC_PER_SEC / HZ);

		if (offset < 0)
			offset = 0;

		if (offset > (USEC_PER_SEC / HZ)) {
			lost = offset / (USEC_PER_SEC / HZ);
			offset %= (USEC_PER_SEC / HZ);
		}

		monotonic_base += (tsc - vxtime.last_tsc)*1000000/cpu_khz ;

		vxtime.last_tsc = tsc - vxtime.quot * delay / vxtime.tsc_quot;

		if ((((tsc - vxtime.last_tsc) *
		      vxtime.tsc_quot) >> 32) < offset)
			vxtime.last_tsc = tsc -
				(((long) offset << 32) / vxtime.tsc_quot) - 1;
	}

	if (lost > 0) {
		handle_lost_ticks(lost, regs);
		jiffies += lost;
	}

/*
 * Do the timer stuff.
 */

	do_timer(regs);
#ifndef CONFIG_SMP
	update_process_times(user_mode(regs));
#endif

/*
 * In the SMP case we use the local APIC timer interrupt to do the profiling,
 * except when we simulate SMP mode on a uniprocessor system, in that case we
 * have to call the local interrupt handler.
 */

#ifndef CONFIG_X86_LOCAL_APIC
	profile_tick(CPU_PROFILING, regs);
#else
	if (!using_apic_timer)
		smp_local_timer_interrupt(regs);
#endif

/*
 * If we have an externally synchronized Linux clock, then update CMOS clock
 * accordingly every ~11 minutes. set_rtc_mmss() will be called in the jiffy
 * closest to exactly 500 ms before the next second. If the update fails, we
 * don't care, as it'll be updated on the next turn, and the problem (time way
 * off) isn't likely to go away much sooner anyway.
 */

	if (ntp_synced() && xtime.tv_sec > rtc_update &&
		abs(xtime.tv_nsec - 500000000) <= tick_nsec / 2) {
		set_rtc_mmss(xtime.tv_sec);
		rtc_update = xtime.tv_sec + 660;
	}
 
	write_sequnlock(&xtime_lock);
}

static irqreturn_t timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	if (apic_runs_main_timer > 1)
		return IRQ_HANDLED;
	main_timer_handler(regs);
#ifdef CONFIG_X86_LOCAL_APIC
	if (using_apic_timer)
		smp_send_timer_broadcast_ipi();
#endif
	return IRQ_HANDLED;
}

static unsigned int cyc2ns_scale __read_mostly;
#define CYC2NS_SCALE_FACTOR 10 /* 2^10, carefully chosen */

static inline void set_cyc2ns_scale(unsigned long cpu_khz)
{
	cyc2ns_scale = (1000000 << CYC2NS_SCALE_FACTOR)/cpu_khz;
}

static inline unsigned long long cycles_2_ns(unsigned long long cyc)
{
	return (cyc * cyc2ns_scale) >> CYC2NS_SCALE_FACTOR;
}

unsigned long long sched_clock(void)
{
	unsigned long a = 0;

#if 0
	/* Don't do a HPET read here. Using TSC always is much faster
	   and HPET may not be mapped yet when the scheduler first runs.
           Disadvantage is a small drift between CPUs in some configurations,
	   but that should be tolerable. */
	if (__vxtime.mode == VXTIME_HPET)
		return (hpet_readl(HPET_COUNTER) * vxtime.quot) >> 32;
#endif

	/* Could do CPU core sync here. Opteron can execute rdtsc speculatively,
	   which means it is not completely exact and may not be monotonous between
	   CPUs. But the errors should be too small to matter for scheduling
	   purposes. */

	rdtscll(a);
	return cycles_2_ns(a);
}

static unsigned long get_cmos_time(void)
{
	unsigned int year, mon, day, hour, min, sec;
	unsigned long flags;
	unsigned extyear = 0;

	spin_lock_irqsave(&rtc_lock, flags);

	do {
		sec = CMOS_READ(RTC_SECONDS);
		min = CMOS_READ(RTC_MINUTES);
		hour = CMOS_READ(RTC_HOURS);
		day = CMOS_READ(RTC_DAY_OF_MONTH);
		mon = CMOS_READ(RTC_MONTH);
		year = CMOS_READ(RTC_YEAR);
#ifdef CONFIG_ACPI
		if (acpi_fadt.revision >= FADT2_REVISION_ID &&
					acpi_fadt.century)
			extyear = CMOS_READ(acpi_fadt.century);
#endif
	} while (sec != CMOS_READ(RTC_SECONDS));

	spin_unlock_irqrestore(&rtc_lock, flags);

	/*
	 * We know that x86-64 always uses BCD format, no need to check the
	 * config register.
 	 */

	BCD_TO_BIN(sec);
	BCD_TO_BIN(min);
	BCD_TO_BIN(hour);
	BCD_TO_BIN(day);
	BCD_TO_BIN(mon);
	BCD_TO_BIN(year);

	if (extyear) {
		BCD_TO_BIN(extyear);
		year += extyear;
		printk(KERN_INFO "Extended CMOS year: %d\n", extyear);
	} else { 
		/*
		 * x86-64 systems only exists since 2002.
		 * This will work up to Dec 31, 2100
	 	 */
		year += 2000;
	}

	return mktime(year, mon, day, hour, min, sec);
}

#ifdef CONFIG_CPU_FREQ

/* Frequency scaling support. Adjust the TSC based timer when the cpu frequency
   changes.
   
   RED-PEN: On SMP we assume all CPUs run with the same frequency.  It's
   not that important because current Opteron setups do not support
   scaling on SMP anyroads.

   Should fix up last_tsc too. Currently gettimeofday in the
   first tick after the change will be slightly wrong. */

#include <linux/workqueue.h>

static unsigned int cpufreq_delayed_issched = 0;
static unsigned int cpufreq_init = 0;
static struct work_struct cpufreq_delayed_get_work;

static void handle_cpufreq_delayed_get(void *v)
{
	unsigned int cpu;
	for_each_online_cpu(cpu) {
		cpufreq_get(cpu);
	}
	cpufreq_delayed_issched = 0;
}

/* if we notice lost ticks, schedule a call to cpufreq_get() as it tries
 * to verify the CPU frequency the timing core thinks the CPU is running
 * at is still correct.
 */
static void cpufreq_delayed_get(void)
{
	static int warned;
	if (cpufreq_init && !cpufreq_delayed_issched) {
		cpufreq_delayed_issched = 1;
		if (!warned) {
			warned = 1;
			printk(KERN_DEBUG 
	"Losing some ticks... checking if CPU frequency changed.\n");
		}
		schedule_work(&cpufreq_delayed_get_work);
	}
}

static unsigned int  ref_freq = 0;
static unsigned long loops_per_jiffy_ref = 0;

static unsigned long cpu_khz_ref = 0;

static int time_cpufreq_notifier(struct notifier_block *nb, unsigned long val,
				 void *data)
{
        struct cpufreq_freqs *freq = data;
	unsigned long *lpj, dummy;

	if (cpu_has(&cpu_data[freq->cpu], X86_FEATURE_CONSTANT_TSC))
		return 0;

	lpj = &dummy;
	if (!(freq->flags & CPUFREQ_CONST_LOOPS))
#ifdef CONFIG_SMP
		lpj = &cpu_data[freq->cpu].loops_per_jiffy;
#else
		lpj = &boot_cpu_data.loops_per_jiffy;
#endif

	if (!ref_freq) {
		ref_freq = freq->old;
		loops_per_jiffy_ref = *lpj;
		cpu_khz_ref = cpu_khz;
	}
        if ((val == CPUFREQ_PRECHANGE  && freq->old < freq->new) ||
            (val == CPUFREQ_POSTCHANGE && freq->old > freq->new) ||
	    (val == CPUFREQ_RESUMECHANGE)) {
                *lpj =
		cpufreq_scale(loops_per_jiffy_ref, ref_freq, freq->new);

		cpu_khz = cpufreq_scale(cpu_khz_ref, ref_freq, freq->new);
		if (!(freq->flags & CPUFREQ_CONST_LOOPS))
			vxtime.tsc_quot = (1000L << 32) / cpu_khz;
	}
	
	set_cyc2ns_scale(cpu_khz_ref);

	return 0;
}
 
static struct notifier_block time_cpufreq_notifier_block = {
         .notifier_call  = time_cpufreq_notifier
};

static int __init cpufreq_tsc(void)
{
	INIT_WORK(&cpufreq_delayed_get_work, handle_cpufreq_delayed_get, NULL);
	if (!cpufreq_register_notifier(&time_cpufreq_notifier_block,
				       CPUFREQ_TRANSITION_NOTIFIER))
		cpufreq_init = 1;
	return 0;
}

core_initcall(cpufreq_tsc);

#endif

/*
 * calibrate_tsc() calibrates the processor TSC in a very simple way, comparing
 * it to the HPET timer of known frequency.
 */

#define TICK_COUNT 100000000

static unsigned int __init hpet_calibrate_tsc(void)
{
	int tsc_start, hpet_start;
	int tsc_now, hpet_now;
	unsigned long flags;

	local_irq_save(flags);
	local_irq_disable();

	hpet_start = hpet_readl(HPET_COUNTER);
	rdtscl(tsc_start);

	do {
		local_irq_disable();
		hpet_now = hpet_readl(HPET_COUNTER);
		tsc_now = get_cycles_sync();
		local_irq_restore(flags);
	} while ((tsc_now - tsc_start) < TICK_COUNT &&
		 (hpet_now - hpet_start) < TICK_COUNT);

	return (tsc_now - tsc_start) * 1000000000L
		/ ((hpet_now - hpet_start) * hpet_period / 1000);
}


/*
 * pit_calibrate_tsc() uses the speaker output (channel 2) of
 * the PIT. This is better than using the timer interrupt output,
 * because we can read the value of the speaker with just one inb(),
 * where we need three i/o operations for the interrupt channel.
 * We count how many ticks the TSC does in 50 ms.
 */

static unsigned int __init pit_calibrate_tsc(void)
{
	unsigned long start, end;
	unsigned long flags;

	spin_lock_irqsave(&i8253_lock, flags);

	outb((inb(0x61) & ~0x02) | 0x01, 0x61);

	outb(0xb0, 0x43);
	outb((PIT_TICK_RATE / (1000 / 50)) & 0xff, 0x42);
	outb((PIT_TICK_RATE / (1000 / 50)) >> 8, 0x42);
	start = get_cycles_sync();
	while ((inb(0x61) & 0x20) == 0);
	end = get_cycles_sync();

	spin_unlock_irqrestore(&i8253_lock, flags);
	
	return (end - start) / 50;
}

#ifdef	CONFIG_HPET
static __init int late_hpet_init(void)
{
	struct hpet_data	hd;
	unsigned int 		ntimer;

	if (!vxtime.hpet_address)
        	return 0;

	memset(&hd, 0, sizeof (hd));

	ntimer = hpet_readl(HPET_ID);
	ntimer = (ntimer & HPET_ID_NUMBER) >> HPET_ID_NUMBER_SHIFT;
	ntimer++;

	/*
	 * Register with driver.
	 * Timer0 and Timer1 is used by platform.
	 */
	hd.hd_phys_address = vxtime.hpet_address;
	hd.hd_address = (void __iomem *)fix_to_virt(FIX_HPET_BASE);
	hd.hd_nirqs = ntimer;
	hd.hd_flags = HPET_DATA_PLATFORM;
	hpet_reserve_timer(&hd, 0);
#ifdef	CONFIG_HPET_EMULATE_RTC
	hpet_reserve_timer(&hd, 1);
#endif
	hd.hd_irq[0] = HPET_LEGACY_8254;
	hd.hd_irq[1] = HPET_LEGACY_RTC;
	if (ntimer > 2) {
		struct hpet		*hpet;
		struct hpet_timer	*timer;
		int			i;

		hpet = (struct hpet *) fix_to_virt(FIX_HPET_BASE);
		timer = &hpet->hpet_timers[2];
		for (i = 2; i < ntimer; timer++, i++)
			hd.hd_irq[i] = (timer->hpet_config &
					Tn_INT_ROUTE_CNF_MASK) >>
				Tn_INT_ROUTE_CNF_SHIFT;

	}

	hpet_alloc(&hd);
	return 0;
}
fs_initcall(late_hpet_init);
#endif

static int hpet_timer_stop_set_go(unsigned long tick)
{
	unsigned int cfg;

/*
 * Stop the timers and reset the main counter.
 */

	cfg = hpet_readl(HPET_CFG);
	cfg &= ~(HPET_CFG_ENABLE | HPET_CFG_LEGACY);
	hpet_writel(cfg, HPET_CFG);
	hpet_writel(0, HPET_COUNTER);
	hpet_writel(0, HPET_COUNTER + 4);

/*
 * Set up timer 0, as periodic with first interrupt to happen at hpet_tick,
 * and period also hpet_tick.
 */
	if (hpet_use_timer) {
		hpet_writel(HPET_TN_ENABLE | HPET_TN_PERIODIC | HPET_TN_SETVAL |
		    HPET_TN_32BIT, HPET_T0_CFG);
		hpet_writel(hpet_tick, HPET_T0_CMP);
		hpet_writel(hpet_tick, HPET_T0_CMP); /* AK: why twice? */
		cfg |= HPET_CFG_LEGACY;
	}
/*
 * Go!
 */

	cfg |= HPET_CFG_ENABLE;
	hpet_writel(cfg, HPET_CFG);

	return 0;
}

static int hpet_init(void)
{
	unsigned int id;

	if (!vxtime.hpet_address)
		return -1;
	set_fixmap_nocache(FIX_HPET_BASE, vxtime.hpet_address);
	__set_fixmap(VSYSCALL_HPET, vxtime.hpet_address, PAGE_KERNEL_VSYSCALL_NOCACHE);

/*
 * Read the period, compute tick and quotient.
 */

	id = hpet_readl(HPET_ID);

	if (!(id & HPET_ID_VENDOR) || !(id & HPET_ID_NUMBER))
		return -1;

	hpet_period = hpet_readl(HPET_PERIOD);
	if (hpet_period < 100000 || hpet_period > 100000000)
		return -1;

	hpet_tick = (1000000000L * (USEC_PER_SEC / HZ) + hpet_period / 2) /
		hpet_period;

	hpet_use_timer = (id & HPET_ID_LEGSUP);

	return hpet_timer_stop_set_go(hpet_tick);
}

static int hpet_reenable(void)
{
	return hpet_timer_stop_set_go(hpet_tick);
}

#define PIT_MODE 0x43
#define PIT_CH0  0x40

static void __init __pit_init(int val, u8 mode)
{
	unsigned long flags;

	spin_lock_irqsave(&i8253_lock, flags);
	outb_p(mode, PIT_MODE);
	outb_p(val & 0xff, PIT_CH0);	/* LSB */
	outb_p(val >> 8, PIT_CH0);	/* MSB */
	spin_unlock_irqrestore(&i8253_lock, flags);
}

void __init pit_init(void)
{
	__pit_init(LATCH, 0x34); /* binary, mode 2, LSB/MSB, ch 0 */
}

void __init pit_stop_interrupt(void)
{
	__pit_init(0, 0x30); /* mode 0 */
}

void __init stop_timer_interrupt(void)
{
	char *name;
	if (vxtime.hpet_address) {
		name = "HPET";
		hpet_timer_stop_set_go(0);
	} else {
		name = "PIT";
		pit_stop_interrupt();
	}
	printk(KERN_INFO "timer: %s interrupt stopped.\n", name);
}

int __init time_setup(char *str)
{
	report_lost_ticks = 1;
	return 1;
}

static struct irqaction irq0 = {
	timer_interrupt, SA_INTERRUPT, CPU_MASK_NONE, "timer", NULL, NULL
};

void __init time_init(void)
{
	char *timename;
	char *gtod;

#ifdef HPET_HACK_ENABLE_DANGEROUS
        if (!vxtime.hpet_address) {
		printk(KERN_WARNING "time.c: WARNING: Enabling HPET base "
		       "manually!\n");
                outl(0x800038a0, 0xcf8);
                outl(0xff000001, 0xcfc);
                outl(0x800038a0, 0xcf8);
                vxtime.hpet_address = inl(0xcfc) & 0xfffffffe;
		printk(KERN_WARNING "time.c: WARNING: Enabled HPET "
		       "at %#lx.\n", vxtime.hpet_address);
        }
#endif
	if (nohpet)
		vxtime.hpet_address = 0;

	xtime.tv_sec = get_cmos_time();
	xtime.tv_nsec = 0;

	set_normalized_timespec(&wall_to_monotonic,
	                        -xtime.tv_sec, -xtime.tv_nsec);

	if (!hpet_init())
                vxtime_hz = (1000000000000000L + hpet_period / 2) / hpet_period;
	else
		vxtime.hpet_address = 0;

	if (hpet_use_timer) {
		cpu_khz = hpet_calibrate_tsc();
		timename = "HPET";
#ifdef CONFIG_X86_PM_TIMER
	} else if (pmtmr_ioport && !vxtime.hpet_address) {
		vxtime_hz = PM_TIMER_FREQUENCY;
		timename = "PM";
		pit_init();
		cpu_khz = pit_calibrate_tsc();
#endif
	} else {
		pit_init();
		cpu_khz = pit_calibrate_tsc();
		timename = "PIT";
	}

	vxtime.mode = VXTIME_TSC;
	gtod = time_init_gtod();

	printk(KERN_INFO "time.c: Using %ld.%06ld MHz WALL %s GTOD %s timer.\n",
	       vxtime_hz / 1000000, vxtime_hz % 1000000, timename, gtod);
	printk(KERN_INFO "time.c: Detected %d.%03d MHz processor.\n",
		cpu_khz / 1000, cpu_khz % 1000);
	vxtime.quot = (1000000L << 32) / vxtime_hz;
	vxtime.tsc_quot = (1000L << 32) / cpu_khz;
	vxtime.last_tsc = get_cycles_sync();
	setup_irq(0, &irq0);

	set_cyc2ns_scale(cpu_khz);
}

/*
 * Make an educated guess if the TSC is trustworthy and synchronized
 * over all CPUs.
 */
__cpuinit int unsynchronized_tsc(void)
{
#ifdef CONFIG_SMP
	if (oem_force_hpet_timer())
		return 1;
 	/* Intel systems are normally all synchronized. Exceptions
 	   are handled in the OEM check above. */
 	if (boot_cpu_data.x86_vendor == X86_VENDOR_INTEL)
 		return 0;
#endif
 	/* Assume multi socket systems are not synchronized */
 	return num_present_cpus() > 1;
}

/*
 * Decide what mode gettimeofday should use.
 */
__init static char *time_init_gtod(void)
{
	char *timetype;

	if (unsynchronized_tsc())
		notsc = 1;
	if (vxtime.hpet_address && notsc) {
		timetype = hpet_use_timer ? "HPET" : "PIT/HPET";
		if (hpet_use_timer)
			vxtime.last = hpet_readl(HPET_T0_CMP) - hpet_tick;
		else
			vxtime.last = hpet_readl(HPET_COUNTER);
		vxtime.mode = VXTIME_HPET;
		do_gettimeoffset = do_gettimeoffset_hpet;
#ifdef CONFIG_X86_PM_TIMER
	/* Using PM for gettimeofday is quite slow, but we have no other
	   choice because the TSC is too unreliable on some systems. */
	} else if (pmtmr_ioport && !vxtime.hpet_address && notsc) {
		timetype = "PM";
		do_gettimeoffset = do_gettimeoffset_pm;
		vxtime.mode = VXTIME_PMTMR;
		sysctl_vsyscall = 0;
		printk(KERN_INFO "Disabling vsyscall due to use of PM timer\n");
#endif
	} else {
		timetype = hpet_use_timer ? "HPET/TSC" : "PIT/TSC";
		vxtime.mode = VXTIME_TSC;
	}
	return timetype;
}

__setup("report_lost_ticks", time_setup);

static long clock_cmos_diff;
static unsigned long sleep_start;

/*
 * sysfs support for the timer.
 */

static int timer_suspend(struct sys_device *dev, pm_message_t state)
{
	/*
	 * Estimate time zone so that set_time can update the clock
	 */
	long cmos_time =  get_cmos_time();

	clock_cmos_diff = -cmos_time;
	clock_cmos_diff += get_seconds();
	sleep_start = cmos_time;
	return 0;
}

static int timer_resume(struct sys_device *dev)
{
	unsigned long flags;
	unsigned long sec;
	unsigned long ctime = get_cmos_time();
	unsigned long sleep_length = (ctime - sleep_start) * HZ;

	if (vxtime.hpet_address)
		hpet_reenable();
	else
		i8254_timer_resume();

	sec = ctime + clock_cmos_diff;
	write_seqlock_irqsave(&xtime_lock,flags);
	xtime.tv_sec = sec;
	xtime.tv_nsec = 0;
	if (vxtime.mode == VXTIME_HPET) {
		if (hpet_use_timer)
			vxtime.last = hpet_readl(HPET_T0_CMP) - hpet_tick;
		else
			vxtime.last = hpet_readl(HPET_COUNTER);
#ifdef CONFIG_X86_PM_TIMER
	} else if (vxtime.mode == VXTIME_PMTMR) {
		pmtimer_resume();
#endif
	} else
		vxtime.last_tsc = get_cycles_sync();
	write_sequnlock_irqrestore(&xtime_lock,flags);
	jiffies += sleep_length;
	wall_jiffies += sleep_length;
	monotonic_base += sleep_length * (NSEC_PER_SEC/HZ);
	touch_softlockup_watchdog();
	return 0;
}

static struct sysdev_class timer_sysclass = {
	.resume = timer_resume,
	.suspend = timer_suspend,
	set_kset_name("timer"),
};

/* XXX this driverfs stuff should probably go elsewhere later -john */
static struct sys_device device_timer = {
	.id	= 0,
	.cls	= &timer_sysclass,
};

static int time_init_device(void)
{
	int error = sysdev_class_register(&timer_sysclass);
	if (!error)
		error = sysdev_register(&device_timer);
	return error;
}

device_initcall(time_init_device);

#ifdef CONFIG_HPET_EMULATE_RTC
/* HPET in LegacyReplacement Mode eats up RTC interrupt line. When, HPET
 * is enabled, we support RTC interrupt functionality in software.
 * RTC has 3 kinds of interrupts:
 * 1) Update Interrupt - generate an interrupt, every sec, when RTC clock
 *    is updated
 * 2) Alarm Interrupt - generate an interrupt at a specific time of day
 * 3) Periodic Interrupt - generate periodic interrupt, with frequencies
 *    2Hz-8192Hz (2Hz-64Hz for non-root user) (all freqs in powers of 2)
 * (1) and (2) above are implemented using polling at a frequency of
 * 64 Hz. The exact frequency is a tradeoff between accuracy and interrupt
 * overhead. (DEFAULT_RTC_INT_FREQ)
 * For (3), we use interrupts at 64Hz or user specified periodic
 * frequency, whichever is higher.
 */
#include <linux/rtc.h>

#define DEFAULT_RTC_INT_FREQ 	64
#define RTC_NUM_INTS 		1

static unsigned long UIE_on;
static unsigned long prev_update_sec;

static unsigned long AIE_on;
static struct rtc_time alarm_time;

static unsigned long PIE_on;
static unsigned long PIE_freq = DEFAULT_RTC_INT_FREQ;
static unsigned long PIE_count;

static unsigned long hpet_rtc_int_freq; /* RTC interrupt frequency */
static unsigned int hpet_t1_cmp; /* cached comparator register */

int is_hpet_enabled(void)
{
	return vxtime.hpet_address != 0;
}

/*
 * Timer 1 for RTC, we do not use periodic interrupt feature,
 * even if HPET supports periodic interrupts on Timer 1.
 * The reason being, to set up a periodic interrupt in HPET, we need to
 * stop the main counter. And if we do that everytime someone diables/enables
 * RTC, we will have adverse effect on main kernel timer running on Timer 0.
 * So, for the time being, simulate the periodic interrupt in software.
 *
 * hpet_rtc_timer_init() is called for the first time and during subsequent
 * interuppts reinit happens through hpet_rtc_timer_reinit().
 */
int hpet_rtc_timer_init(void)
{
	unsigned int cfg, cnt;
	unsigned long flags;

	if (!is_hpet_enabled())
		return 0;
	/*
	 * Set the counter 1 and enable the interrupts.
	 */
	if (PIE_on && (PIE_freq > DEFAULT_RTC_INT_FREQ))
		hpet_rtc_int_freq = PIE_freq;
	else
		hpet_rtc_int_freq = DEFAULT_RTC_INT_FREQ;

	local_irq_save(flags);
	cnt = hpet_readl(HPET_COUNTER);
	cnt += ((hpet_tick*HZ)/hpet_rtc_int_freq);
	hpet_writel(cnt, HPET_T1_CMP);
	hpet_t1_cmp = cnt;
	local_irq_restore(flags);

	cfg = hpet_readl(HPET_T1_CFG);
	cfg &= ~HPET_TN_PERIODIC;
	cfg |= HPET_TN_ENABLE | HPET_TN_32BIT;
	hpet_writel(cfg, HPET_T1_CFG);

	return 1;
}

static void hpet_rtc_timer_reinit(void)
{
	unsigned int cfg, cnt;

	if (unlikely(!(PIE_on | AIE_on | UIE_on))) {
		cfg = hpet_readl(HPET_T1_CFG);
		cfg &= ~HPET_TN_ENABLE;
		hpet_writel(cfg, HPET_T1_CFG);
		return;
	}

	if (PIE_on && (PIE_freq > DEFAULT_RTC_INT_FREQ))
		hpet_rtc_int_freq = PIE_freq;
	else
		hpet_rtc_int_freq = DEFAULT_RTC_INT_FREQ;

	/* It is more accurate to use the comparator value than current count.*/
	cnt = hpet_t1_cmp;
	cnt += hpet_tick*HZ/hpet_rtc_int_freq;
	hpet_writel(cnt, HPET_T1_CMP);
	hpet_t1_cmp = cnt;
}

/*
 * The functions below are called from rtc driver.
 * Return 0 if HPET is not being used.
 * Otherwise do the necessary changes and return 1.
 */
int hpet_mask_rtc_irq_bit(unsigned long bit_mask)
{
	if (!is_hpet_enabled())
		return 0;

	if (bit_mask & RTC_UIE)
		UIE_on = 0;
	if (bit_mask & RTC_PIE)
		PIE_on = 0;
	if (bit_mask & RTC_AIE)
		AIE_on = 0;

	return 1;
}

int hpet_set_rtc_irq_bit(unsigned long bit_mask)
{
	int timer_init_reqd = 0;

	if (!is_hpet_enabled())
		return 0;

	if (!(PIE_on | AIE_on | UIE_on))
		timer_init_reqd = 1;

	if (bit_mask & RTC_UIE) {
		UIE_on = 1;
	}
	if (bit_mask & RTC_PIE) {
		PIE_on = 1;
		PIE_count = 0;
	}
	if (bit_mask & RTC_AIE) {
		AIE_on = 1;
	}

	if (timer_init_reqd)
		hpet_rtc_timer_init();

	return 1;
}

int hpet_set_alarm_time(unsigned char hrs, unsigned char min, unsigned char sec)
{
	if (!is_hpet_enabled())
		return 0;

	alarm_time.tm_hour = hrs;
	alarm_time.tm_min = min;
	alarm_time.tm_sec = sec;

	return 1;
}

int hpet_set_periodic_freq(unsigned long freq)
{
	if (!is_hpet_enabled())
		return 0;

	PIE_freq = freq;
	PIE_count = 0;

	return 1;
}

int hpet_rtc_dropped_irq(void)
{
	if (!is_hpet_enabled())
		return 0;

	return 1;
}

irqreturn_t hpet_rtc_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct rtc_time curr_time;
	unsigned long rtc_int_flag = 0;
	int call_rtc_interrupt = 0;

	hpet_rtc_timer_reinit();

	if (UIE_on | AIE_on) {
		rtc_get_rtc_time(&curr_time);
	}
	if (UIE_on) {
		if (curr_time.tm_sec != prev_update_sec) {
			/* Set update int info, call real rtc int routine */
			call_rtc_interrupt = 1;
			rtc_int_flag = RTC_UF;
			prev_update_sec = curr_time.tm_sec;
		}
	}
	if (PIE_on) {
		PIE_count++;
		if (PIE_count >= hpet_rtc_int_freq/PIE_freq) {
			/* Set periodic int info, call real rtc int routine */
			call_rtc_interrupt = 1;
			rtc_int_flag |= RTC_PF;
			PIE_count = 0;
		}
	}
	if (AIE_on) {
		if ((curr_time.tm_sec == alarm_time.tm_sec) &&
		    (curr_time.tm_min == alarm_time.tm_min) &&
		    (curr_time.tm_hour == alarm_time.tm_hour)) {
			/* Set alarm int info, call real rtc int routine */
			call_rtc_interrupt = 1;
			rtc_int_flag |= RTC_AF;
		}
	}
	if (call_rtc_interrupt) {
		rtc_int_flag |= (RTC_IRQF | (RTC_NUM_INTS << 8));
		rtc_interrupt(rtc_int_flag, dev_id, regs);
	}
	return IRQ_HANDLED;
}
#endif

static int __init nohpet_setup(char *s) 
{ 
	nohpet = 1;
	return 1;
} 

__setup("nohpet", nohpet_setup);

int __init notsc_setup(char *s)
{
	notsc = 1;
	return 1;
}

__setup("notsc", notsc_setup);
