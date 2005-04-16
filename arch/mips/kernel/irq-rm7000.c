/*
 * Copyright (C) 2003 Ralf Baechle
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * Handler for RM7000 extended interrupts.  These are a non-standard
 * feature so we handle them separately from standard interrupts.
 */
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>

#include <asm/irq_cpu.h>
#include <asm/mipsregs.h>
#include <asm/system.h>

static int irq_base;

static inline void unmask_rm7k_irq(unsigned int irq)
{
	set_c0_intcontrol(0x100 << (irq - irq_base));
}

static inline void mask_rm7k_irq(unsigned int irq)
{
	clear_c0_intcontrol(0x100 << (irq - irq_base));
}

static inline void rm7k_cpu_irq_enable(unsigned int irq)
{
	unsigned long flags;

	local_irq_save(flags);
	unmask_rm7k_irq(irq);
	local_irq_restore(flags);
}

static void rm7k_cpu_irq_disable(unsigned int irq)
{
	unsigned long flags;

	local_irq_save(flags);
	mask_rm7k_irq(irq);
	local_irq_restore(flags);
}

static unsigned int rm7k_cpu_irq_startup(unsigned int irq)
{
	rm7k_cpu_irq_enable(irq);

	return 0;
}

#define	rm7k_cpu_irq_shutdown	rm7k_cpu_irq_disable

/*
 * While we ack the interrupt interrupts are disabled and thus we don't need
 * to deal with concurrency issues.  Same for rm7k_cpu_irq_end.
 */
static void rm7k_cpu_irq_ack(unsigned int irq)
{
	mask_rm7k_irq(irq);
}

static void rm7k_cpu_irq_end(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED | IRQ_INPROGRESS)))
		unmask_rm7k_irq(irq);
}

static hw_irq_controller rm7k_irq_controller = {
	"RM7000",
	rm7k_cpu_irq_startup,
	rm7k_cpu_irq_shutdown,
	rm7k_cpu_irq_enable,
	rm7k_cpu_irq_disable,
	rm7k_cpu_irq_ack,
	rm7k_cpu_irq_end,
};

void __init rm7k_cpu_irq_init(int base)
{
	int i;

	clear_c0_intcontrol(0x00000f00);		/* Mask all */

	for (i = base; i < base + 4; i++) {
		irq_desc[i].status = IRQ_DISABLED;
		irq_desc[i].action = NULL;
		irq_desc[i].depth = 1;
		irq_desc[i].handler = &rm7k_irq_controller;
	}

	irq_base = base;
}
