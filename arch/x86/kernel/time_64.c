/*
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
#include <linux/clockchips.h>

#ifdef CONFIG_ACPI
#include <acpi/achware.h>	/* for PM timer frequency */
#include <acpi/acpi_bus.h>
#endif
#include <asm/i8253.h>
#include <asm/pgtable.h>
#include <asm/vsyscall.h>
#include <asm/timex.h>
#include <asm/proto.h>
#include <asm/hpet.h>
#include <asm/sections.h>
#include <linux/hpet.h>
#include <asm/apic.h>
#include <asm/hpet.h>
#include <asm/mpspec.h>
#include <asm/nmi.h>
#include <asm/vgtod.h>

DEFINE_SPINLOCK(rtc_lock);
EXPORT_SYMBOL(rtc_lock);

volatile unsigned long __jiffies __section_jiffies = INITIAL_JIFFIES;

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

static int set_rtc_mmss(unsigned long nowtime)
{
	int retval = 0;
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
		retval = -1;
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

	return retval;
}

int update_persistent_clock(struct timespec now)
{
	return set_rtc_mmss(now.tv_sec);
}

static irqreturn_t timer_event_interrupt(int irq, void *dev_id)
{
	add_pda(irq0_irqs, 1);

	global_clock_event->event_handler(global_clock_event);

	return IRQ_HANDLED;
}

unsigned long read_persistent_clock(void)
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

/* calibrate_cpu is used on systems with fixed rate TSCs to determine
 * processor frequency */
#define TICK_COUNT 100000000
static unsigned int __init tsc_calibrate_cpu_khz(void)
{
	int tsc_start, tsc_now;
	int i, no_ctr_free;
	unsigned long evntsel3 = 0, pmc3 = 0, pmc_now = 0;
	unsigned long flags;

	for (i = 0; i < 4; i++)
		if (avail_to_resrv_perfctr_nmi_bit(i))
			break;
	no_ctr_free = (i == 4);
	if (no_ctr_free) {
		i = 3;
		rdmsrl(MSR_K7_EVNTSEL3, evntsel3);
		wrmsrl(MSR_K7_EVNTSEL3, 0);
		rdmsrl(MSR_K7_PERFCTR3, pmc3);
	} else {
		reserve_perfctr_nmi(MSR_K7_PERFCTR0 + i);
		reserve_evntsel_nmi(MSR_K7_EVNTSEL0 + i);
	}
	local_irq_save(flags);
	/* start meauring cycles, incrementing from 0 */
	wrmsrl(MSR_K7_PERFCTR0 + i, 0);
	wrmsrl(MSR_K7_EVNTSEL0 + i, 1 << 22 | 3 << 16 | 0x76);
	rdtscl(tsc_start);
	do {
		rdmsrl(MSR_K7_PERFCTR0 + i, pmc_now);
		tsc_now = get_cycles_sync();
	} while ((tsc_now - tsc_start) < TICK_COUNT);

	local_irq_restore(flags);
	if (no_ctr_free) {
		wrmsrl(MSR_K7_EVNTSEL3, 0);
		wrmsrl(MSR_K7_PERFCTR3, pmc3);
		wrmsrl(MSR_K7_EVNTSEL3, evntsel3);
	} else {
		release_perfctr_nmi(MSR_K7_PERFCTR0 + i);
		release_evntsel_nmi(MSR_K7_EVNTSEL0 + i);
	}

	return pmc_now * tsc_khz / (tsc_now - tsc_start);
}

static struct irqaction irq0 = {
	.handler	= timer_event_interrupt,
	.flags		= IRQF_DISABLED | IRQF_IRQPOLL | IRQF_NOBALANCING,
	.mask		= CPU_MASK_NONE,
	.name		= "timer"
};

void __init time_init(void)
{
	if (!hpet_enable())
		setup_pit_timer();

	setup_irq(0, &irq0);

	tsc_calibrate();

	cpu_khz = tsc_khz;
	if (cpu_has(&boot_cpu_data, X86_FEATURE_CONSTANT_TSC) &&
		boot_cpu_data.x86_vendor == X86_VENDOR_AMD &&
		boot_cpu_data.x86 == 16)
		cpu_khz = tsc_calibrate_cpu_khz();

	if (unsynchronized_tsc())
		mark_tsc_unstable("TSCs unsynchronized");

	if (cpu_has(&boot_cpu_data, X86_FEATURE_RDTSCP))
		vgetcpu_mode = VGETCPU_RDTSCP;
	else
		vgetcpu_mode = VGETCPU_LSL;

	printk(KERN_INFO "time.c: Detected %d.%03d MHz processor.\n",
		cpu_khz / 1000, cpu_khz % 1000);
	init_tsc_clocksource();
}
