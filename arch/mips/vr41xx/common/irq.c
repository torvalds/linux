/*
 *  Interrupt handing routines for NEC VR4100 series.
 *
 *  Copyright (C) 2005  Yoichi Yuasa <yuasa@hh.iij4u.or.jp>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/interrupt.h>
#include <linux/module.h>

#include <asm/irq_cpu.h>
#include <asm/system.h>
#include <asm/vr41xx/vr41xx.h>

typedef struct irq_cascade {
	int (*get_irq)(unsigned int, struct pt_regs *);
} irq_cascade_t;

static irq_cascade_t irq_cascade[NR_IRQS] __cacheline_aligned;

static struct irqaction cascade_irqaction = {
	.handler	= no_action,
	.mask		= CPU_MASK_NONE,
	.name		= "cascade",
};

int cascade_irq(unsigned int irq, int (*get_irq)(unsigned int, struct pt_regs *))
{
	int retval = 0;

	if (irq >= NR_IRQS)
		return -EINVAL;

	if (irq_cascade[irq].get_irq != NULL)
		free_irq(irq, NULL);

	irq_cascade[irq].get_irq = get_irq;

	if (get_irq != NULL) {
		retval = setup_irq(irq, &cascade_irqaction);
		if (retval < 0)
			irq_cascade[irq].get_irq = NULL;
	}

	return retval;
}

EXPORT_SYMBOL_GPL(cascade_irq);

asmlinkage void irq_dispatch(unsigned int irq, struct pt_regs *regs)
{
	irq_cascade_t *cascade;
	irq_desc_t *desc;

	if (irq >= NR_IRQS) {
		atomic_inc(&irq_err_count);
		return;
	}

	cascade = irq_cascade + irq;
	if (cascade->get_irq != NULL) {
		unsigned int source_irq = irq;
		desc = irq_desc + source_irq;
		desc->handler->ack(source_irq);
		irq = cascade->get_irq(irq, regs);
		if (irq < 0)
			atomic_inc(&irq_err_count);
		else
			irq_dispatch(irq, regs);
		desc->handler->end(source_irq);
	} else
		do_IRQ(irq, regs);
}

extern asmlinkage void vr41xx_handle_interrupt(void);

void __init arch_init_irq(void)
{
	mips_cpu_irq_init(MIPS_CPU_IRQ_BASE);

	set_except_vector(0, vr41xx_handle_interrupt);
}
