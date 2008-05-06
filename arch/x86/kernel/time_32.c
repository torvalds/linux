/*
 *  Copyright (C) 1991, 1992, 1995  Linus Torvalds
 *
 * This file contains the PC-specific time handling details:
 * reading the RTC at bootup, etc..
 * 1994-07-02    Alan Modra
 *	fixed set_rtc_mmss, fixed time.year for >= 2000, new mktime
 * 1995-03-26    Markus Kuhn
 *      fixed 500 ms bug at call to set_rtc_mmss, fixed DS12887
 *      precision CMOS clock update
 * 1996-05-03    Ingo Molnar
 *      fixed time warps in do_[slow|fast]_gettimeoffset()
 * 1997-09-10	Updated NTP code according to technical memorandum Jan '96
 *		"A Kernel Model for Precision Timekeeping" by Dave Mills
 * 1998-09-05    (Various)
 *	More robust do_fast_gettimeoffset() algorithm implemented
 *	(works with APM, Cyrix 6x86MX and Centaur C6),
 *	monotonic gettimeofday() with fast_get_timeoffset(),
 *	drift-proof precision TSC calibration on boot
 *	(C. Scott Ananian <cananian@alumni.princeton.edu>, Andrew D.
 *	Balsa <andrebalsa@altern.org>, Philip Gladstone <philip@raptor.com>;
 *	ported from 2.0.35 Jumbo-9 by Michael Krause <m.krause@tu-harburg.de>).
 * 1998-12-16    Andrea Arcangeli
 *	Fixed Jumbo-9 code in 2.1.131: do_gettimeofday was missing 1 jiffy
 *	because was not accounting lost_ticks.
 * 1998-12-24 Copyright (C) 1998  Andrea Arcangeli
 *	Fixed a xtime SMP race (we need the xtime_lock rw spinlock to
 *	serialize accesses to xtime/lost_ticks).
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/mca.h>

#include <asm/arch_hooks.h>
#include <asm/hpet.h>
#include <asm/time.h>

#include "do_timer.h"

unsigned int cpu_khz;	/* Detected as we calibrate the TSC */
EXPORT_SYMBOL(cpu_khz);

int timer_ack;

unsigned long profile_pc(struct pt_regs *regs)
{
	unsigned long pc = instruction_pointer(regs);

#ifdef CONFIG_SMP
	if (!v8086_mode(regs) && SEGMENT_IS_KERNEL_CODE(regs->cs) &&
	    in_lock_functions(pc)) {
#ifdef CONFIG_FRAME_POINTER
		return *(unsigned long *)(regs->bp + 4);
#else
		unsigned long *sp = (unsigned long *)&regs->sp;

		/* Return address is either directly at stack pointer
		   or above a saved flags. Eflags has bits 22-31 zero,
		   kernel addresses don't. */
		if (sp[0] >> 22)
			return sp[0];
		if (sp[1] >> 22)
			return sp[1];
#endif
	}
#endif
	return pc;
}
EXPORT_SYMBOL(profile_pc);

/*
 * This is the same as the above, except we _also_ save the current
 * Time Stamp Counter value at the time of the timer interrupt, so that
 * we later on can estimate the time of day more exactly.
 */
irqreturn_t timer_interrupt(int irq, void *dev_id)
{
	/* Keep nmi watchdog up to date */
	per_cpu(irq_stat, smp_processor_id()).irq0_irqs++;

#ifdef CONFIG_X86_IO_APIC
	if (timer_ack) {
		/*
		 * Subtle, when I/O APICs are used we have to ack timer IRQ
		 * manually to reset the IRR bit for do_slow_gettimeoffset().
		 * This will also deassert NMI lines for the watchdog if run
		 * on an 82489DX-based system.
		 */
		spin_lock(&i8259A_lock);
		outb(0x0c, PIC_MASTER_OCW3);
		/* Ack the IRQ; AEOI will end it automatically. */
		inb(PIC_MASTER_POLL);
		spin_unlock(&i8259A_lock);
	}
#endif

	do_timer_interrupt_hook();

	if (MCA_bus) {
		/* The PS/2 uses level-triggered interrupts.  You can't
		turn them off, nor would you want to (any attempt to
		enable edge-triggered interrupts usually gets intercepted by a
		special hardware circuit).  Hence we have to acknowledge
		the timer interrupt.  Through some incredibly stupid
		design idea, the reset for IRQ 0 is done by setting the
		high bit of the PPI port B (0x61).  Note that some PS/2s,
		notably the 55SX, work fine if this is removed.  */

		u8 irq_v = inb_p( 0x61 );	/* read the current state */
		outb_p( irq_v|0x80, 0x61 );	/* reset the IRQ */
	}

	return IRQ_HANDLED;
}

/* Duplicate of time_init() below, with hpet_enable part added */
void __init hpet_time_init(void)
{
	if (!hpet_enable())
		setup_pit_timer();
	time_init_hook();
}

/*
 * This is called directly from init code; we must delay timer setup in the
 * HPET case as we can't make the decision to turn on HPET this early in the
 * boot process.
 *
 * The chosen time_init function will usually be hpet_time_init, above, but
 * in the case of virtual hardware, an alternative function may be substituted.
 */
void __init time_init(void)
{
	tsc_init();
	late_time_init = choose_time_init();
}
