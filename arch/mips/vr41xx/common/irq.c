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
#include <linux/export.h>
#include <linux/interrupt.h>
#include <linux/irq.h>

#include <asm/irq_cpu.h>
#include <asm/vr41xx/irq.h>

typedef struct irq_cascade {
	int (*get_irq)(unsigned int);
} irq_cascade_t;

static irq_cascade_t irq_cascade[NR_IRQS] __cacheline_aligned;

static struct irqaction cascade_irqaction = {
	.handler	= no_action,
	.name		= "cascade",
	.flags		= IRQF_NO_THREAD,
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

	if (irq >= NR_IRQS) {
		atomic_inc(&irq_err_count);
		return;
	}

	cascade = irq_cascade + irq;
	if (cascade->get_irq != NULL) {
		struct irq_desc *desc = irq_to_desc(irq);
		struct irq_data *idata = irq_desc_get_irq_data(desc);
		struct irq_chip *chip = irq_desc_get_chip(desc);
		int ret;

		if (chip->irq_mask_ack)
			chip->irq_mask_ack(idata);
		else {
			chip->irq_mask(idata);
			chip->irq_ack(idata);
		}
		ret = cascade->get_irq(irq);
		irq = ret;
		if (ret < 0)
			atomic_inc(&irq_err_count);
		else
			irq_dispatch(irq);
		if (!irqd_irq_disabled(idata) && chip->irq_unmask)
			chip->irq_unmask(idata);
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
