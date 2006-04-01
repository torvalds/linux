/*
 * The irq controller for vrc5476.
 *
 * Copyright (C) 2001 MontaVista Software Inc.
 * Author: jsun@mvista.com or jsun@junsun.net
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/types.h>
#include <linux/ptrace.h>

#include <asm/system.h>

#include <asm/ddb5xxx/ddb5xxx.h>

static int irq_base;

static void vrc5476_irq_enable(uint irq)
{
	nile4_enable_irq(irq - irq_base);
}

static void vrc5476_irq_disable(uint irq)
{
	nile4_disable_irq(irq - irq_base);
}

static unsigned int vrc5476_irq_startup(uint irq)
{
	nile4_enable_irq(irq - irq_base);
	return 0;
}

#define vrc5476_irq_shutdown	vrc5476_irq_disable

static void vrc5476_irq_ack(uint irq)
{
	nile4_clear_irq(irq - irq_base);
	nile4_disable_irq(irq - irq_base);
}

static void vrc5476_irq_end(uint irq)
{
	if(!(irq_desc[irq].status & (IRQ_DISABLED | IRQ_INPROGRESS)))
		vrc5476_irq_enable(irq);
}

static hw_irq_controller vrc5476_irq_controller = {
	.typename = "vrc5476",
	.startup = vrc5476_irq_startup,
	.shutdown = vrc5476_irq_shutdown,
	.enable = vrc5476_irq_enable,
	.disable = vrc5476_irq_disable,
	.ack = vrc5476_irq_ack,
	.end = vrc5476_irq_end
};

void __init
vrc5476_irq_init(u32 base)
{
	u32 i;

	irq_base = base;
	for (i= base; i< base + NUM_VRC5476_IRQ; i++) {
		irq_desc[i].status = IRQ_DISABLED;
		irq_desc[i].action = NULL;
		irq_desc[i].depth = 1;
		irq_desc[i].handler = &vrc5476_irq_controller;
	}
}


asmlinkage void
vrc5476_irq_dispatch(struct pt_regs *regs)
{
	u32 mask;
	int nile4_irq;

	mask = nile4_get_irq_stat(0);

	/* quick check for possible time interrupt */
	if (mask & (1 << VRC5476_IRQ_GPT)) {
		do_IRQ(VRC5476_IRQ_BASE + VRC5476_IRQ_GPT, regs);
		return;
	}

	/* check for i8259 interrupts */
	if (mask & (1 << VRC5476_I8259_CASCADE)) {
		int i8259_irq = nile4_i8259_iack();
		do_IRQ(I8259_IRQ_BASE + i8259_irq, regs);
		return;
	}

	/* regular nile4 interrupts (we should not really have any */
	for (nile4_irq = 0; mask; nile4_irq++, mask >>= 1) {
		if (mask & 1) {
			do_IRQ(VRC5476_IRQ_BASE + nile4_irq, regs);
			return;
		}
	}
	spurious_interrupt(regs);
}
