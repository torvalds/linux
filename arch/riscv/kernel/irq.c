/*
 * Copyright (C) 2012 Regents of the University of California
 * Copyright (C) 2017 SiFive
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 */

#include <linux/interrupt.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>

#ifdef CONFIG_RISCV_INTC
#include <linux/irqchip/irq-riscv-intc.h>
#endif

void __init init_IRQ(void)
{
	irqchip_init();
}

asmlinkage void __irq_entry do_IRQ(unsigned int cause, struct pt_regs *regs)
{
#ifdef CONFIG_RISCV_INTC
	/*
	 * FIXME: We don't want a direct call to riscv_intc_irq here.  The plan
	 * is to put an IRQ domain here and let the interrupt controller
	 * register with that, but I poked around the arm64 code a bit and
	 * there might be a better way to do it (ie, something fully generic).
	 */
	riscv_intc_irq(cause, regs);
#endif
}
