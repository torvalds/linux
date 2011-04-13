/* irq.c: FRV IRQ handling
 *
 * Copyright (C) 2003, 2004, 2006 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/timex.h>
#include <linux/random.h>
#include <linux/init.h>
#include <linux/kernel_stat.h>
#include <linux/irq.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/module.h>
#include <linux/bitops.h>

#include <asm/atomic.h>
#include <asm/io.h>
#include <asm/smp.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/pgalloc.h>
#include <asm/delay.h>
#include <asm/irq.h>
#include <asm/irc-regs.h>
#include <asm/gdb-stub.h>

#define set_IRR(N,A,B,C,D) __set_IRR(N, (A << 28) | (B << 24) | (C << 20) | (D << 16))

extern void __init fpga_init(void);
#ifdef CONFIG_FUJITSU_MB93493
extern void __init mb93493_init(void);
#endif

#define __reg16(ADDR) (*(volatile unsigned short *)(ADDR))

atomic_t irq_err_count;

int arch_show_interrupts(struct seq_file *p, int prec)
{
	seq_printf(p, "%*s: ", prec, "ERR");
	seq_printf(p, "%10u\n", atomic_read(&irq_err_count));
	return 0;
}

/*
 * on-CPU PIC operations
 */
static void frv_cpupic_ack(struct irq_data *d)
{
	__clr_RC(d->irq);
	__clr_IRL();
}

static void frv_cpupic_mask(struct irq_data *d)
{
	__set_MASK(d->irq);
}

static void frv_cpupic_mask_ack(struct irq_data *d)
{
	__set_MASK(d->irq);
	__clr_RC(d->irq);
	__clr_IRL();
}

static void frv_cpupic_unmask(struct irq_data *d)
{
	__clr_MASK(d->irq);
}

static struct irq_chip frv_cpu_pic = {
	.name		= "cpu",
	.irq_ack	= frv_cpupic_ack,
	.irq_mask	= frv_cpupic_mask,
	.irq_mask_ack	= frv_cpupic_mask_ack,
	.irq_unmask	= frv_cpupic_unmask,
};

/*
 * handles all normal device IRQs
 * - registers are referred to by the __frame variable (GR28)
 * - IRQ distribution is complicated in this arch because of the many PICs, the
 *   way they work and the way they cascade
 */
asmlinkage void do_IRQ(void)
{
	irq_enter();
	generic_handle_irq(__get_IRL());
	irq_exit();
}

/*
 * handles all NMIs when not co-opted by the debugger
 * - registers are referred to by the __frame variable (GR28)
 */
asmlinkage void do_NMI(void)
{
}

/*
 * initialise the interrupt system
 */
void __init init_IRQ(void)
{
	int level;

	for (level = 1; level <= 14; level++)
		irq_set_chip_and_handler(level, &frv_cpu_pic,
					 handle_level_irq);

	irq_set_handler(IRQ_CPU_TIMER0, handle_edge_irq);

	/* set the trigger levels for internal interrupt sources
	 * - timers all falling-edge
	 * - ERR0 is rising-edge
	 * - all others are high-level
	 */
	__set_IITMR(0, 0x003f0000);	/* DMA0-3, TIMER0-2 */
	__set_IITMR(1, 0x20000000);	/* ERR0-1, UART0-1, DMA4-7 */

	/* route internal interrupts */
	set_IRR(4, IRQ_DMA3_LEVEL, IRQ_DMA2_LEVEL, IRQ_DMA1_LEVEL,
		IRQ_DMA0_LEVEL);
	set_IRR(5, 0, IRQ_TIMER2_LEVEL, IRQ_TIMER1_LEVEL, IRQ_TIMER0_LEVEL);
	set_IRR(6, IRQ_GDBSTUB_LEVEL, IRQ_GDBSTUB_LEVEL,
		IRQ_UART1_LEVEL, IRQ_UART0_LEVEL);
	set_IRR(7, IRQ_DMA7_LEVEL, IRQ_DMA6_LEVEL, IRQ_DMA5_LEVEL,
		IRQ_DMA4_LEVEL);

	/* route external interrupts */
	set_IRR(2, IRQ_XIRQ7_LEVEL, IRQ_XIRQ6_LEVEL, IRQ_XIRQ5_LEVEL,
		IRQ_XIRQ4_LEVEL);
	set_IRR(3, IRQ_XIRQ3_LEVEL, IRQ_XIRQ2_LEVEL, IRQ_XIRQ1_LEVEL,
		IRQ_XIRQ0_LEVEL);

#if defined(CONFIG_MB93091_VDK)
	__set_TM1(0x55550000);		/* XIRQ7-0 all active low */
#elif defined(CONFIG_MB93093_PDK)
	__set_TM1(0x15550000);		/* XIRQ7 active high, 6-0 all active low */
#else
#error dont know external IRQ trigger levels for this setup
#endif

	fpga_init();
#ifdef CONFIG_FUJITSU_MB93493
	mb93493_init();
#endif
}
