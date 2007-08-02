/*
 * linux/arch/mips/tx4938/common/irq.c
 *
 * Common tx4938 irq handler
 * Copyright (C) 2000-2001 Toshiba Corporation
 *
 * 2003-2005 (c) MontaVista Software, Inc. This file is licensed under the
 * terms of the GNU General Public License version 2. This program is
 * licensed "as is" without any warranty of any kind, whether express
 * or implied.
 *
 * Support for TX4938 in 2.6 - Manish Lachwani (mlachwani@mvista.com)
 */
#include <linux/init.h>
#include <linux/interrupt.h>
#include <asm/irq_cpu.h>
#include <asm/mipsregs.h>
#include <asm/tx4938/rbtx4938.h>

void __init
tx4938_irq_init(void)
{
	mips_cpu_irq_init();
	txx9_irq_init(TX4938_IRC_REG);
	set_irq_chained_handler(TX4938_IRQ_NEST_PIC_ON_CP0, handle_simple_irq);
}

int toshiba_rbtx4938_irq_nested(int irq);

asmlinkage void plat_irq_dispatch(void)
{
	unsigned int pending = read_c0_cause() & read_c0_status();

	if (pending & STATUSF_IP7)
		do_IRQ(TX4938_IRQ_CPU_TIMER);
	else if (pending & STATUSF_IP2) {
		int irq = txx9_irq();
		if (irq == TX4938_IRQ_PIC_BEG + TX4938_IR_INT(0))
			irq = toshiba_rbtx4938_irq_nested(irq);
		if (irq >= 0)
			do_IRQ(irq);
		else
			spurious_interrupt();
	} else if (pending & STATUSF_IP1)
		do_IRQ(TX4938_IRQ_USER1);
	else if (pending & STATUSF_IP0)
		do_IRQ(TX4938_IRQ_USER0);
}
