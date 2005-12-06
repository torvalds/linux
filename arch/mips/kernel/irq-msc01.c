/*
 * Copyright (c) 2004 MIPS Inc
 * Author: chris@mips.com
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <asm/ptrace.h>
#include <linux/sched.h>
#include <linux/kernel_stat.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/msc01_ic.h>

static unsigned long _icctrl_msc;
#define MSC01_IC_REG_BASE	_icctrl_msc

#define MSCIC_WRITE(reg, data)	do { *(volatile u32 *)(reg) = data; } while (0)
#define MSCIC_READ(reg, data)	do { data = *(volatile u32 *)(reg); } while (0)

static unsigned int irq_base;

/* mask off an interrupt */
static inline void mask_msc_irq(unsigned int irq)
{
	if (irq < (irq_base + 32))
		MSCIC_WRITE(MSC01_IC_DISL, 1<<(irq - irq_base));
	else
		MSCIC_WRITE(MSC01_IC_DISH, 1<<(irq - irq_base - 32));
}

/* unmask an interrupt */
static inline void unmask_msc_irq(unsigned int irq)
{
	if (irq < (irq_base + 32))
		MSCIC_WRITE(MSC01_IC_ENAL, 1<<(irq - irq_base));
	else
		MSCIC_WRITE(MSC01_IC_ENAH, 1<<(irq - irq_base - 32));
}

/*
 * Enables the IRQ on SOC-it
 */
static void enable_msc_irq(unsigned int irq)
{
	unmask_msc_irq(irq);
}

/*
 * Initialize the IRQ on SOC-it
 */
static unsigned int startup_msc_irq(unsigned int irq)
{
	unmask_msc_irq(irq);
	return 0;
}

/*
 * Disables the IRQ on SOC-it
 */
static void disable_msc_irq(unsigned int irq)
{
	mask_msc_irq(irq);
}

/*
 * Masks and ACKs an IRQ
 */
static void level_mask_and_ack_msc_irq(unsigned int irq)
{
	mask_msc_irq(irq);
	if (!cpu_has_veic)
		MSCIC_WRITE(MSC01_IC_EOI, 0);
}

/*
 * Masks and ACKs an IRQ
 */
static void edge_mask_and_ack_msc_irq(unsigned int irq)
{
	mask_msc_irq(irq);
	if (!cpu_has_veic)
		MSCIC_WRITE(MSC01_IC_EOI, 0);
	else {
		u32 r;
		MSCIC_READ(MSC01_IC_SUP+irq*8, r);
		MSCIC_WRITE(MSC01_IC_SUP+irq*8, r | ~MSC01_IC_SUP_EDGE_BIT);
		MSCIC_WRITE(MSC01_IC_SUP+irq*8, r);
	}
}

/*
 * End IRQ processing
 */
static void end_msc_irq(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED|IRQ_INPROGRESS)))
		unmask_msc_irq(irq);
}

/*
 * Interrupt handler for interrupts coming from SOC-it.
 */
void ll_msc_irq(struct pt_regs *regs)
{
 	unsigned int irq;

	/* read the interrupt vector register */
	MSCIC_READ(MSC01_IC_VEC, irq);
	if (irq < 64)
		do_IRQ(irq + irq_base, regs);
	else {
		/* Ignore spurious interrupt */
	}
}

void
msc_bind_eic_interrupt (unsigned int irq, unsigned int set)
{
	MSCIC_WRITE(MSC01_IC_RAMW,
		    (irq<<MSC01_IC_RAMW_ADDR_SHF) | (set<<MSC01_IC_RAMW_DATA_SHF));
}

#define shutdown_msc_irq	disable_msc_irq

struct hw_interrupt_type msc_levelirq_type = {
	.typename = "SOC-it-Level",
	.startup = startup_msc_irq,
	.shutdown = shutdown_msc_irq,
	.enable = enable_msc_irq,
	.disable = disable_msc_irq,
	.ack = level_mask_and_ack_msc_irq,
	.end = end_msc_irq,
};

struct hw_interrupt_type msc_edgeirq_type = {
	.typename = "SOC-it-Edge",
	.startup =startup_msc_irq,
	.shutdown = shutdown_msc_irq,
	.enable = enable_msc_irq,
	.disable = disable_msc_irq,
	.ack = edge_mask_and_ack_msc_irq,
	.end = end_msc_irq,
};


void __init init_msc_irqs(unsigned int base, msc_irqmap_t *imp, int nirq)
{
	extern void (*board_bind_eic_interrupt)(unsigned int irq, unsigned int regset);

	_icctrl_msc = (unsigned long) ioremap (MIPS_MSC01_IC_REG_BASE, 0x40000);

	/* Reset interrupt controller - initialises all registers to 0 */
	MSCIC_WRITE(MSC01_IC_RST, MSC01_IC_RST_RST_BIT);

	board_bind_eic_interrupt = &msc_bind_eic_interrupt;

	for (; nirq >= 0; nirq--, imp++) {
		int n = imp->im_irq;

		switch (imp->im_type) {
		case MSC01_IRQ_EDGE:
			irq_desc[base+n].handler = &msc_edgeirq_type;
			if (cpu_has_veic)
				MSCIC_WRITE(MSC01_IC_SUP+n*8, MSC01_IC_SUP_EDGE_BIT);
			else
				MSCIC_WRITE(MSC01_IC_SUP+n*8, MSC01_IC_SUP_EDGE_BIT | imp->im_lvl);
			break;
		case MSC01_IRQ_LEVEL:
			irq_desc[base+n].handler = &msc_levelirq_type;
			if (cpu_has_veic)
				MSCIC_WRITE(MSC01_IC_SUP+n*8, 0);
			else
				MSCIC_WRITE(MSC01_IC_SUP+n*8, imp->im_lvl);
		}
	}

	irq_base = base;

	MSCIC_WRITE(MSC01_IC_GENA, MSC01_IC_GENA_GENA_BIT);	/* Enable interrupt generation */

}
