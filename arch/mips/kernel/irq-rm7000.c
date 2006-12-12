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

static struct irq_chip rm7k_irq_controller = {
	.typename = "RM7000",
	.ack = mask_rm7k_irq,
	.mask = mask_rm7k_irq,
	.mask_ack = mask_rm7k_irq,
	.unmask = unmask_rm7k_irq,
};

void __init rm7k_cpu_irq_init(int base)
{
	int i;

	clear_c0_intcontrol(0x00000f00);		/* Mask all */

	for (i = base; i < base + 4; i++)
		set_irq_chip_and_handler(i, &rm7k_irq_controller,
					 handle_level_irq);

	irq_base = base;
}
