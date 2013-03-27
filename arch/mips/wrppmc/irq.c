/*
 * irq.c: GT64120 Interrupt Controller
 *
 * Copyright (C) 2006, Wind River System Inc.
 * Author: Rongkai.Zhan, <rongkai.zhan@windriver.com>
 *
 * This program is free software; you can redistribute	it and/or modify it
 * under  the terms of	the GNU General	 Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <linux/hardirq.h>
#include <linux/init.h>
#include <linux/irq.h>

#include <asm/gt64120.h>
#include <asm/irq_cpu.h>
#include <asm/mipsregs.h>

asmlinkage void plat_irq_dispatch(void)
{
	unsigned int pending = read_c0_status() & read_c0_cause() & ST0_IM;

	if (pending & STATUSF_IP7)
		do_IRQ(WRPPMC_MIPS_TIMER_IRQ);	/* CPU Compare/Count internal timer */
	else if (pending & STATUSF_IP6)
		do_IRQ(WRPPMC_UART16550_IRQ);	/* UART 16550 port */
	else if (pending & STATUSF_IP3)
		do_IRQ(WRPPMC_PCI_INTA_IRQ);	/* PCI INT_A */
	else
		spurious_interrupt();
}

/**
 * Initialize GT64120 Interrupt Controller
 */
void gt64120_init_pic(void)
{
	/* clear CPU Interrupt Cause Registers */
	GT_WRITE(GT_INTRCAUSE_OFS, (0x1F << 21));
	GT_WRITE(GT_HINTRCAUSE_OFS, 0x00);

	/* Disable all interrupts from GT64120 bridge chip */
	GT_WRITE(GT_INTRMASK_OFS, 0x00);
	GT_WRITE(GT_HINTRMASK_OFS, 0x00);
	GT_WRITE(GT_PCI0_ICMASK_OFS, 0x00);
	GT_WRITE(GT_PCI0_HICMASK_OFS, 0x00);
}

void __init arch_init_irq(void)
{
	/* IRQ 0 - 7 are for MIPS common irq_cpu controller */
	mips_cpu_irq_init();

	gt64120_init_pic();
}
