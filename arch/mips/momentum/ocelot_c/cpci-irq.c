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
 * Interrupt handler for interrupts coming from the FPGA chip.
 * It could be built in ethernet ports etc...
 */
void ll_cpci_irq(void)
{
	unsigned int irq_src, irq_mask;

	/* read the interrupt status registers */
	irq_src = OCELOT_FPGA_READ(INTSTAT);
	irq_mask = OCELOT_FPGA_READ(INTMASK);

	/* mask for just the interrupts we want */
	irq_src &= ~irq_mask;

	do_IRQ(ls1bit8(irq_src) + CPCI_IRQ_BASE);
}

struct irq_chip cpci_irq_type = {
	.name = "CPCI/FPGA",
	.ack = mask_cpci_irq,
	.mask = mask_cpci_irq,
	.mask_ack = mask_cpci_irq,
	.unmask = unmask_cpci_irq,
};

void cpci_irq_init(void)
{
	int i;

	for (i = CPCI_IRQ_BASE; i < (CPCI_IRQ_BASE + 8); i++)
		set_irq_chip_and_handler(i, &cpci_irq_type, handle_level_irq);
}
