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
 *  Copyright (c) 2002,2006  Vojtech Pavlik
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
#include <linux/notifier.h>
#include <linux/cpu.h>
#include <linux/kallsyms.h>
#include <linux/acpi.h>
#ifdef CONFIG_ACPI
#include <acpi/achware.h>	/* for PM timer frequency */
#include <acpi/acpi_bus.h>
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
#include <asm/apic.h>
#include <asm/hpet.h>

#ifdef CONFIG_CPU_FREQ
extern void cpufreq_delayed_get(void);
#endif
extern void i8254_timer_resume(void);
extern int using_apic_timer;

static char *timename = NULL;

DEFINE_SPINLOCK(rtc_lock);
EXPORT_SYMBOL(rtc_lock);
DEFINE_SPINLOCK(i8253_lock);

unsigned long vxtime_hz = PIT_TICK_RATE;
int report_lost_ticks;				/* command line option */
unsigned long long monotonic_base;

struct vxtime_data __vxtime __section_vxtime;	/* for vsyscalls */

volatile unsigned long __jiffies __section_jiffies = INITIAL_JIFFIES;
struct timespec __xtime __section_xtime;
struct timezone __sys_tz __section_sys_tz;

unsigned int (*do_gettimeoffset)(void) = do_gettimeoffset_tsc;

/*
 * This version of gettimeofday() has microsecond resolution and better than
 * microsecond precision, as we're using at least a 10 MHz (usually 14.31818
 * MHz) HPET timer.
 */

void do_gettimeofday(struct timeval *tv)
{
	unsigned long seq;
 	unsigned int sec, usec;

	do {
		seq = read_seqbegin(&xtime_lock);

		sec = xtime.tv_sec;
		usec = xtime.tv_nsec / NSEC_PER_USEC;

		/* i386 does some correction here to keep the clock 
		   monotonous even when ntpd is fixing drift.
		   But they didn't work for me, there is a non monotonic
		   clock anyways with ntp.
		   I dropped all corrections now until a real solution can
		   be found. Note when you fix it here you need to do the same
		   in arch/x86_64/kernel/vsyscall.c and export all needed
		   variables in vmlinux.lds. -AK */ 
		usec += do_gettimeoffset();

	} while (read_seqretry(&xtime_lock, seq));

	tv->tv_sec = sec + usec / USEC_PER_SEC;
	tv->tv_usec = usec % USEC_PER_SEC;
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

	nsec -= do_gettimeoffset() * NSEC_PER_USEC;

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

	/* Assume the lock function has either no stack frame or a copy
	   of eflags from PUSHF
	   Eflags always has bits 22 and up cleared unlike kernel addresses. */
	if (!user_mode(regs) && in_lock_functions(pc)) {
		unsigned long *sp = (unsigned long *)regs->rsp;
		if (sp[0] >> 22)
			return sp[0];
		if (sp[1] >> 22)
			return sp[1];
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
extern unsigned long long cycles_2_ns(unsigned long long cyc);
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
		offset *= NSEC_PER_TICK / hpet_tick;
	} else {
		do {
			seq = read_seqbegin(&xtime_lock);

			last_offset = vxtime.last_tsc;
			base = monotonic_base;
		} while (read_seqretry(&xtime_lock, seq));
		this_offset = get_cycles_sync();
		offset = cycles_2_ns(this_offset - last_offset);
	}
	return base + offset;
}
EXPORT_SYMBOL(monotonic_clock);

static noinline void handle_lost_ticks(int lost)
{
	static long lost_count;
	static int warned;
	if (report_lost_ticks) {
		printk(KERN_WARNING "time.c: Lost %d timer tick(s)! ", lost);
		print_symbol("rip %s)\n", get_irq_regs()->rip);
	}

	if (lost_count == 1000 && !warned) {
		printk(KERN_WARNING "warning: many lost ticks.\n"
		       KERN_WARNING "Your time source seems to be instable or "
		   		"some driver is hogging interupts\n");
		print_symbol("rip %s\n", get_irq_regs()->rip);
		if (vxtime.mode == VXTIME_TSC && hpet_address) {
			printk(KERN_WARNING "Falling back to HPET\n");
			if (hpet_use_timer)
				vxtime.last = hpet_readl(HPET_T0_CMP) - 
							hpet_tick;
			else
				vxtime.last = hpet_readl(HPET_COUNTER);
			vxtime.mode = VXTIME_HPET;
			vxtime.hpet_address = hpet_address;
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

void main_timer_handler(void)
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

	if (hpet_address)
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
			(offset - vxtime.last) * NSEC_PER_TICK / hpet_tick;

		vxtime.last = offset;
#ifdef CONFIG_X86_PM_TIMER
	} else if (vxtime.mode == VXTIME_PMTMR) {
		lost = pmtimer_mark_offset();
#endif
	} else {
		offset = (((tsc - vxtime.last_tsc) *
			   vxtime.tsc_quot) >> US_SCALE) - USEC_PER_TICK;

		if (offset < 0)
			offset = 0;

		if (offset > USEC_PER_TICK) {
			lost = offset / USEC_PER_TICK;
			offset %= USEC_PER_TICK;
		}

		monotonic_base += cycles_2_ns(tsc - vxtime.last_tsc);

		vxtime.last_tsc = tsc - vxtime.quot * delay / vxtime.tsc_quot;

		if ((((tsc - vxtime.last_tsc) *
		      vxtime.tsc_quot) >> US_SCALE) < offset)
			vxtime.last_tsc = tsc -
				(((long) offset << US_SCALE) / vxtime.tsc_quot) - 1;
	}

	if (lost > 0)
		handle_lost_ticks(lost);
	else
		lost = 0;

/*
 * Do the timer stuff.
 */

	do_timer(lost + 1);
#ifndef CONFIG_SMP
	update_process_times(user_mode(get_irq_regs()));
#endif

/*
 * In the SMP case we use the local APIC timer interrupt to do the profiling,
 * except when we simulate SMP mode on a uniprocessor system, in that case we
 * have to call the local interrupt handler.
 */

	if (!using_apic_timer)
		smp_local_timer_interrupt();

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

static irqreturn_t timer_interrupt(int irq, void *dev_id)
{
	if (apic_runs_main_timer > 1)
		return IRQ_HANDLED;
	main_timer_handler();
	if (using_apic_timer)
		smp_send_timer_broadcast_ipi();
	return IRQ_HANDLED;
}

static unsigned long get_cmos_time(void)
{
	unsigned int year, mon, day, hour, min, sec;
	unsigned long flags;
	unsigned century = 0;

	spin_lock_irqsave(&rtc_lock, flags);

	do {
		sec = CMOS_READ(RTC_SECONDS);
		min = CMOS_READ(RTC_MINUTES);
		hour = CMOS_READ(RTC_HOURS);
		day = CMOS_READ(RTC_DAY_OF_MONTH);
		mon = CMOS_READ(RTC_MONTH);
		year = CMOS_READ(RTC_YEAR);
#ifdef CONFIG_ACPI
		if (acpi_gbl_FADT.header.revision >= FADT2_REVISION_ID &&
					acpi_gbl_FADT.century)
			century = CMOS_READ(acpi_gbl_FADT.century);
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

	if (century) {
		BCD_TO_BIN(century);
		year += century * 100;
		printk(KERN_INFO "Extended CMOS year: %d\n", century * 100);
	} else { 
		/*
		 * x86-64 systems only exists since 2002.
		 * This will work up to Dec 31, 2100
	 	 */
		year += 2000;
	}

	return mktime(year, mon, day, hour, min, sec);
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
	if (hpet_address) {
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
	timer_interrupt, IRQF_DISABLED, CPU_MASK_NONE, "timer", NULL, NULL
};

void __init time_init(void)
{
	if (nohpet)
		hpet_address = 0;
	xtime.tv_sec = get_cmos_time();
	xtime.tv_nsec = 0;

	set_normalized_timespec(&wall_to_monotonic,
	                        -xtime.tv_sec, -xtime.tv_nsec);

	if (!hpet_arch_init())
                vxtime_hz = (FSEC_PER_SEC + hpet_period / 2) / hpet_period;
	else
		hpet_address = 0;

	if (hpet_use_timer) {
		/* set tick_nsec to use the proper rate for HPET */
	  	tick_nsec = TICK_NSEC_HPET;
		cpu_khz = hpet_calibrate_tsc();
		timename = "HPET";
#ifdef CONFIG_X86_PM_TIMER
	} else if (pmtmr_ioport && !hpet_address) {
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
	vxtime.quot = (USEC_PER_SEC << US_SCALE) / vxtime_hz;
	vxtime.tsc_quot = (USEC_PER_MSEC << US_SCALE) / cpu_khz;
	vxtime.last_tsc = get_cycles_sync();
	set_cyc2ns_scale(cpu_khz);
	setup_irq(0, &irq0);

#ifndef CONFIG_SMP
	time_init_gtod();
#endif
}

/*
 * Decide what mode gettimeofday should use.
 */
void time_init_gtod(void)
{
	char *timetype;

	if (unsynchronized_tsc())
		notsc = 1;

	if (cpu_has(&boot_cpu_data, X86_FEATURE_RDTSCP))
		vgetcpu_mode = VGETCPU_RDTSCP;
	else
		vgetcpu_mode = VGETCPU_LSL;

	if (hpet_address && notsc) {
		timetype = hpet_use_timer ? "HPET" : "PIT/HPET";
		if (hpet_use_timer)
			vxtime.last = hpet_readl(HPET_T0_CMP) - hpet_tick;
		else
			vxtime.last = hpet_readl(HPET_COUNTER);
		vxtime.mode = VXTIME_HPET;
		vxtime.hpet_address = hpet_address;
		do_gettimeoffset = do_gettimeoffset_hpet;
#ifdef CONFIG_X86_PM_TIMER
	/* Using PM for gettimeofday is quite slow, but we have no other
	   choice because the TSC is too unreliable on some systems. */
	} else if (pmtmr_ioport && !hpet_address && notsc) {
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

	printk(KERN_INFO "time.c: Using %ld.%06ld MHz WALL %s GTOD %s timer.\n",
	       vxtime_hz / 1000000, vxtime_hz % 1000000, timename, timetype);
	printk(KERN_INFO "time.c: Detected %d.%03d MHz processor.\n",
		cpu_khz / 1000, cpu_khz % 1000);
	vxtime.quot = (USEC_PER_SEC << US_SCALE) / vxtime_hz;
	vxtime.tsc_quot = (USEC_PER_MSEC << US_SCALE) / cpu_khz;
	vxtime.last_tsc = get_cycles_sync();

	set_cyc2ns_scale(cpu_khz);
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
	long sleep_length = (ctime - sleep_start) * HZ;

	if (sleep_length < 0) {
		printk(KERN_WARNING "Time skew detected in timer resume!\n");
		/* The time after the resume must not be earlier than the time
		 * before the suspend or some nasty things will happen
		 */
		sleep_length = 0;
		ctime = sleep_start;
	}
	if (hpet_address)
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
