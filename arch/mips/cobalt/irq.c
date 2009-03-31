/*
 * IRQ vector handles
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995, 1996, 1997, 2003 by Ralf Baechle
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/pci.h>

#include <asm/i8259.h>
#include <asm/irq_cpu.h>
#include <asm/irq_gt641xx.h>
#include <asm/gt64120.h>

#include <irq.h>

asmlinkage void plat_irq_dispatch(void)
{
	unsigned pending = read_c0_status() & read_c0_cause() & ST0_IM;
	int irq;

	if (pending & CAUSEF_IP2)
		gt641xx_irq_dispatch();
	else if (pending & CAUSEF_IP6) {
		irq = i8259_irq();
		if (irq < 0)
			spurious_interrupt();
		else
			do_IRQ(irq);
	} else if (pending & CAUSEF_IP3)
		do_IRQ(MIPS_CPU_IRQ_BASE + 3);
	else if (pending & CAUSEF_IP4)
		do_IRQ(MIPS_CPU_IRQ_BASE + 4);
	else if (pending & CAUSEF_IP5)
		do_IRQ(MIPS_CPU_IRQ_BASE + 5);
	else if (pending & CAUSEF_IP7)
		do_IRQ(MIPS_CPU_IRQ_BASE + 7);
	else
		spurious_interrupt();
}

static struct irqaction cascade = {
	.handler	= no_action,
	.name		= "cascade",
};

void __init arch_init_irq(void)
{
	mips_cpu_irq_init();
	gt641xx_irq_init();
	init_i8259_irqs();

	setup_irq(GT641XX_CASCADE_IRQ, &cascade);
	setup_irq(I8259_CASCADE_IRQ, &cascade);
}
