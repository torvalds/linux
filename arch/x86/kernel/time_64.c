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

#include <linux/clockchips.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/time.h>
#include <linux/mca.h>
#include <linux/nmi.h>

#include <asm/i8253.h>
#include <asm/hpet.h>
#include <asm/vgtod.h>
#include <asm/time.h>
#include <asm/timer.h>

volatile unsigned long __jiffies __section_jiffies = INITIAL_JIFFIES;

unsigned long profile_pc(struct pt_regs *regs)
{
	unsigned long pc = instruction_pointer(regs);

	/* Assume the lock function has either no stack frame or a copy
	   of flags from PUSHF
	   Eflags always has bits 22 and up cleared unlike kernel addresses. */
	if (!user_mode_vm(regs) && in_lock_functions(pc)) {
#ifdef CONFIG_FRAME_POINTER
		return *(unsigned long *)(regs->bp + sizeof(long));
#else
		unsigned long *sp = (unsigned long *)regs->sp;
		if (sp[0] >> 22)
			return sp[0];
		if (sp[1] >> 22)
			return sp[1];
#endif
	}
	return pc;
}
EXPORT_SYMBOL(profile_pc);

static irqreturn_t timer_interrupt(int irq, void *dev_id)
{
	inc_irq_stat(irq0_irqs);

	global_clock_event->event_handler(global_clock_event);

#ifdef CONFIG_MCA
	if (MCA_bus) {
		u8 irq_v = inb_p(0x61);       /* read the current state */
		outb_p(irq_v|0x80, 0x61);     /* reset the IRQ */
	}
#endif

	return IRQ_HANDLED;
}

/* calibrate_cpu is used on systems with fixed rate TSCs to determine
 * processor frequency */
#define TICK_COUNT 100000000
unsigned long __init calibrate_cpu(void)
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
		WARN(1, KERN_WARNING "Warning: AMD perfctrs busy ... "
		     "cpu_khz value may be incorrect.\n");
		i = 3;
		rdmsrl(MSR_K7_EVNTSEL3, evntsel3);
		wrmsrl(MSR_K7_EVNTSEL3, 0);
		rdmsrl(MSR_K7_PERFCTR3, pmc3);
	} else {
		reserve_perfctr_nmi(MSR_K7_PERFCTR0 + i);
		reserve_evntsel_nmi(MSR_K7_EVNTSEL0 + i);
	}
	local_irq_save(flags);
	/* start measuring cycles, incrementing from 0 */
	wrmsrl(MSR_K7_PERFCTR0 + i, 0);
	wrmsrl(MSR_K7_EVNTSEL0 + i, 1 << 22 | 3 << 16 | 0x76);
	rdtscl(tsc_start);
	do {
		rdmsrl(MSR_K7_PERFCTR0 + i, pmc_now);
		tsc_now = get_cycles();
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
	.handler	= timer_interrupt,
	.flags		= IRQF_DISABLED | IRQF_IRQPOLL | IRQF_NOBALANCING,
	.mask		= CPU_MASK_NONE,
	.name		= "timer"
};

void __init hpet_time_init(void)
{
	if (!hpet_enable())
		setup_pit_timer();

	irq0.mask = cpumask_of_cpu(0);
	setup_irq(0, &irq0);
}

void __init time_init(void)
{
	tsc_init();

	late_time_init = choose_time_init();
}
