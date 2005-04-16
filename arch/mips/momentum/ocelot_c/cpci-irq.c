/*
 * Copyright 2002 Momentum Computer
 * Author: mdharm@momenco.com
 *
 * arch/mips/momentum/ocelot_c/cpci-irq.c
 *     Interrupt routines for cpci.  Interrupt numbers are assigned from
 *     CPCI_IRQ_BASE to CPCI_IRQ_BASE+8 (8 interrupt sources).
 *
 * Note that the high-level software will need to be careful about using
 * these interrupts.  If this board is asserting a cPCI interrupt, it will
 * also see the asserted interrupt.  Care must be taken to avoid an
 * interrupt flood.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <asm/ptrace.h>
#include <linux/sched.h>
#include <linux/kernel_stat.h>
#include <asm/io.h>
#include "ocelot_c_fpga.h"

#define CPCI_IRQ_BASE	8

static inline int ls1bit8(unsigned int x)
{
        int b = 7, s;

        s =  4; if (((unsigned char)(x <<  4)) == 0) s = 0; b -= s; x <<= s;
        s =  2; if (((unsigned char)(x <<  2)) == 0) s = 0; b -= s; x <<= s;
        s =  1; if (((unsigned char)(x <<  1)) == 0) s = 0; b -= s;

        return b;
}

/* mask off an interrupt -- 0 is enable, 1 is disable */
static inline void mask_cpci_irq(unsigned int irq)
{
	uint32_t value;

	value = OCELOT_FPGA_READ(INTMASK);
	value |= 1 << (irq - CPCI_IRQ_BASE);
	OCELOT_FPGA_WRITE(value, INTMASK);

	/* read the value back to assure that it's really been written */
	value = OCELOT_FPGA_READ(INTMASK);
}

/* unmask an interrupt -- 0 is enable, 1 is disable */
static inline void unmask_cpci_irq(unsigned int irq)
{
	uint32_t value;

	value = OCELOT_FPGA_READ(INTMASK);
	value &= ~(1 << (irq - CPCI_IRQ_BASE));
	OCELOT_FPGA_WRITE(value, INTMASK);

	/* read the value back to assure that it's really been written */
	value = OCELOT_FPGA_READ(INTMASK);
}

/*
 * Enables the IRQ in the FPGA
 */
static void enable_cpci_irq(unsigned int irq)
{
	unmask_cpci_irq(irq);
}

/*
 * Initialize the IRQ in the FPGA
 */
static unsigned int startup_cpci_irq(unsigned int irq)
{
	unmask_cpci_irq(irq);
	return 0;
}

/*
 * Disables the IRQ in the FPGA
 */
static void disable_cpci_irq(unsigned int irq)
{
	mask_cpci_irq(irq);
}

/*
 * Masks and ACKs an IRQ
 */
static void mask_and_ack_cpci_irq(unsigned int irq)
{
	mask_cpci_irq(irq);
}

/*
 * End IRQ processing
 */
static void end_cpci_irq(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED|IRQ_INPROGRESS)))
		unmask_cpci_irq(irq);
}

/*
 * Interrupt handler for interrupts coming from the FPGA chip.
 * It could be built in ethernet ports etc...
 */
void ll_cpci_irq(struct pt_regs *regs)
{
	unsigned int irq_src, irq_mask;

	/* read the interrupt status registers */
	irq_src = OCELOT_FPGA_READ(INTSTAT);
	irq_mask = OCELOT_FPGA_READ(INTMASK);

	/* mask for just the interrupts we want */
	irq_src &= ~irq_mask;

	do_IRQ(ls1bit8(irq_src) + CPCI_IRQ_BASE, regs);
}

#define shutdown_cpci_irq	disable_cpci_irq

struct hw_interrupt_type cpci_irq_type = {
	"CPCI/FPGA",
	startup_cpci_irq,
	shutdown_cpci_irq,
	enable_cpci_irq,
	disable_cpci_irq,
	mask_and_ack_cpci_irq,
	end_cpci_irq,
	NULL
};

void cpci_irq_init(void)
{
	int i;

	/* Reset irq handlers pointers to NULL */
	for (i = CPCI_IRQ_BASE; i < (CPCI_IRQ_BASE + 8); i++) {
		irq_desc[i].status = IRQ_DISABLED;
		irq_desc[i].action = 0;
		irq_desc[i].depth = 2;
		irq_desc[i].handler = &cpci_irq_type;
	}
}
