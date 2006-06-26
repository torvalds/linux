/*
 * irq.c: GT64120 Interrupt Controller
 *
 * Copyright (C) 2006, Wind River System Inc.
 * Author: Rongkai.Zhan, <rongkai.zhan@windriver.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel_stat.h>
#include <linux/module.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/timex.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/bitops.h>
#include <asm/bootinfo.h>
#include <asm/io.h>
#include <asm/bitops.h>
#include <asm/mipsregs.h>
#include <asm/system.h>
#include <asm/irq_cpu.h>
#include <asm/gt64120.h>

extern asmlinkage void handle_IRQ(void);

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
	/* enable all CPU interrupt bits. */
	set_c0_status(ST0_IM);	/* IE bit is still 0 */

	/* Install MIPS Interrupt Trap Vector */
	set_except_vector(0, handle_IRQ);

	/* IRQ 0 - 7 are for MIPS common irq_cpu controller */
	mips_cpu_irq_init(0);

	gt64120_init_pic();
}
