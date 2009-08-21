/*
 *  Interrupt handing routines for NEC VR4100 series.
 *
 *  Copyright (C) 2005-2007  Yoichi Yuasa <yuasa@linux-mips.org>
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
#include <asm/vr41xx/irq.h>

typedef struct irq_cascade {
	int (*get_irq)(unsigned int);
} irq_cascade_t;

static irq_cascade_t irq_cascade[NR_IRQS] __cacheline_aligned;

static struct irqaction cascade_irqaction = {
	.handler	= no_action,
	.name		= "cascade",
};

int cascade_irq(unsigned int irq, int (*get_irq)(unsigned int))
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

static void irq_dispatch(unsigned int irq)
{
	irq_cascade_t *cascade;
	struct irq_desc *desc;

	if (irq >= NR_IRQS) {
		atomic_inc(&irq_err_count);
		return;
	}

	cascade = irq_cascade + irq;
	if (cascade->get_irq != NULL) {
		unsigned int source_irq = irq;
		int ret;
		desc = irq_desc + source_irq;
		if (desc->chip->mask_ack)
			desc->chip->mask_ack(source_irq);
		else {
			desc->chip->mask(source_irq);
			desc->chip->ack(source_irq);
		}
		ret = cascade->get_irq(irq);
		irq = ret;
		if (ret < 0)
			atomic_inc(&irq_err_count);
		else
			irq_dispatch(irq);
		if (!(desc->status & IRQ_DISABLED) && desc->chip->unmask)
			desc->chip->unmask(source_irq);
	} else
		do_IRQ(irq);
}

asmlinkage void plat_irq_dispatch(void)
{
	unsigned int pending = read_c0_cause() & read_c0_status() & ST0_IM;

	if (pending & CAUSEF_IP7)
		do_IRQ(TIMER_IRQ);
	else if (pending & 0x7800) {
		if (pending & CAUSEF_IP3)
			irq_dispatch(INT1_IRQ);
		else if (pending & CAUSEF_IP4)
			irq_dispatch(INT2_IRQ);
		else if (pending & CAUSEF_IP5)
			irq_dispatch(INT3_IRQ);
		else if (pending & CAUSEF_IP6)
			irq_dispatch(INT4_IRQ);
	} else if (pending & CAUSEF_IP2)
		irq_dispatch(INT0_IRQ);
	else if (pending & CAUSEF_IP0)
		do_IRQ(MIPS_SOFTINT0_IRQ);
	else if (pending & CAUSEF_IP1)
		do_IRQ(MIPS_SOFTINT1_IRQ);
	else
		spurious_interrupt();
}

void __init arch_init_irq(void)
{
	mips_cpu_irq_init();
}
