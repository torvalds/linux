/*
 * Copyright (C) 2003 Ralf Baechle
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * Handler for RM9000 extended interrupts.  These are a non-standard
 * feature so we handle them separately from standard interrupts.
 */
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include <asm/irq_cpu.h>
#include <asm/mipsregs.h>
#include <asm/system.h>

static int irq_base;

static inline void unmask_rm9k_irq(unsigned int irq)
{
	set_c0_intcontrol(0x1000 << (irq - irq_base));
}

static inline void mask_rm9k_irq(unsigned int irq)
{
	clear_c0_intcontrol(0x1000 << (irq - irq_base));
}

static inline void rm9k_cpu_irq_enable(unsigned int irq)
{
	unsigned long flags;

	local_irq_save(flags);
	unmask_rm9k_irq(irq);
	local_irq_restore(flags);
}

static void rm9k_cpu_irq_disable(unsigned int irq)
{
	unsigned long flags;

	local_irq_save(flags);
	mask_rm9k_irq(irq);
	local_irq_restore(flags);
}

static unsigned int rm9k_cpu_irq_startup(unsigned int irq)
{
	rm9k_cpu_irq_enable(irq);

	return 0;
}

#define	rm9k_cpu_irq_shutdown	rm9k_cpu_irq_disable

/*
 * Performance counter interrupts are global on all processors.
 */
static void local_rm9k_perfcounter_irq_startup(void *args)
{
	unsigned int irq = (unsigned int) args;

	rm9k_cpu_irq_enable(irq);
}

static unsigned int rm9k_perfcounter_irq_startup(unsigned int irq)
{
	on_each_cpu(local_rm9k_perfcounter_irq_startup, (void *) irq, 0, 1);

	return 0;
}

static void local_rm9k_perfcounter_irq_shutdown(void *args)
{
	unsigned int irq = (unsigned int) args;
	unsigned long flags;

	local_irq_save(flags);
	mask_rm9k_irq(irq);
	local_irq_restore(flags);
}

static void rm9k_perfcounter_irq_shutdown(unsigned int irq)
{
	on_each_cpu(local_rm9k_perfcounter_irq_shutdown, (void *) irq, 0, 1);
}


/*
 * While we ack the interrupt interrupts are disabled and thus we don't need
 * to deal with concurrency issues.  Same for rm9k_cpu_irq_end.
 */
static void rm9k_cpu_irq_ack(unsigned int irq)
{
	mask_rm9k_irq(irq);
}

static void rm9k_cpu_irq_end(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED | IRQ_INPROGRESS)))
		unmask_rm9k_irq(irq);
}

static hw_irq_controller rm9k_irq_controller = {
	.typename = "RM9000",
	.startup = rm9k_cpu_irq_startup,
	.shutdown = rm9k_cpu_irq_shutdown,
	.enable = rm9k_cpu_irq_enable,
	.disable = rm9k_cpu_irq_disable,
	.ack = rm9k_cpu_irq_ack,
	.end = rm9k_cpu_irq_end,
};

static hw_irq_controller rm9k_perfcounter_irq = {
	.typename = "RM9000",
	.startup = rm9k_perfcounter_irq_startup,
	.shutdown = rm9k_perfcounter_irq_shutdown,
	.enable = rm9k_cpu_irq_enable,
	.disable = rm9k_cpu_irq_disable,
	.ack = rm9k_cpu_irq_ack,
	.end = rm9k_cpu_irq_end,
};

unsigned int rm9000_perfcount_irq;

EXPORT_SYMBOL(rm9000_perfcount_irq);

void __init rm9k_cpu_irq_init(int base)
{
	int i;

	clear_c0_intcontrol(0x0000f000);		/* Mask all */

	for (i = base; i < base + 4; i++) {
		irq_desc[i].status = IRQ_DISABLED;
		irq_desc[i].action = NULL;
		irq_desc[i].depth = 1;
		irq_desc[i].handler = &rm9k_irq_controller;
	}

	rm9000_perfcount_irq = base + 1;
	irq_desc[rm9000_perfcount_irq].handler = &rm9k_perfcounter_irq;

	irq_base = base;
}
