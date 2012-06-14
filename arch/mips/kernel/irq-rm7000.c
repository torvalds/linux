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
#include <linux/irq.h>
#include <linux/kernel.h>

#include <asm/irq_cpu.h>
#include <asm/mipsregs.h>

static inline void unmask_rm7k_irq(struct irq_data *d)
{
	set_c0_intcontrol(0x100 << (d->irq - RM7K_CPU_IRQ_BASE));
}

static inline void mask_rm7k_irq(struct irq_data *d)
{
	clear_c0_intcontrol(0x100 << (d->irq - RM7K_CPU_IRQ_BASE));
}

static struct irq_chip rm7k_irq_controller = {
	.name = "RM7000",
	.irq_ack = mask_rm7k_irq,
	.irq_mask = mask_rm7k_irq,
	.irq_mask_ack = mask_rm7k_irq,
	.irq_unmask = unmask_rm7k_irq,
	.irq_eoi = unmask_rm7k_irq
};

void __init rm7k_cpu_irq_init(void)
{
	int base = RM7K_CPU_IRQ_BASE;
	int i;

	clear_c0_intcontrol(0x00000f00);		/* Mask all */

	for (i = base; i < base + 4; i++)
		irq_set_chip_and_handler(i, &rm7k_irq_controller,
					 handle_percpu_irq);
}
