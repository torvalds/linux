/* irq-mb93091.c: MB93091 FPGA interrupt handling
 *
 * Copyright (C) 2003 Red Hat, Inc. All Rights Reserved.
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
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/bitops.h>

#include <asm/io.h>
#include <asm/system.h>
#include <asm/delay.h>
#include <asm/irq.h>
#include <asm/irc-regs.h>

#define __reg16(ADDR) (*(volatile unsigned short *)(ADDR))

#define __get_IMR()	({ __reg16(0xffc00004); })
#define __set_IMR(M)	do { __reg16(0xffc00004) = (M); wmb(); } while(0)
#define __get_IFR()	({ __reg16(0xffc0000c); })
#define __clr_IFR(M)	do { __reg16(0xffc0000c) = ~(M); wmb(); } while(0)


/*
 * on-motherboard FPGA PIC operations
 */
static void frv_fpga_mask(struct irq_data *d)
{
	uint16_t imr = __get_IMR();

	imr |= 1 << (d->irq - IRQ_BASE_FPGA);

	__set_IMR(imr);
}

static void frv_fpga_ack(struct irq_data *d)
{
	__clr_IFR(1 << (d->irq - IRQ_BASE_FPGA));
}

static void frv_fpga_mask_ack(struct irq_data *d)
{
	uint16_t imr = __get_IMR();

	imr |= 1 << (d->irq - IRQ_BASE_FPGA);
	__set_IMR(imr);

	__clr_IFR(1 << (d->irq - IRQ_BASE_FPGA));
}

static void frv_fpga_unmask(struct irq_data *d)
{
	uint16_t imr = __get_IMR();

	imr &= ~(1 << (d->irq - IRQ_BASE_FPGA));

	__set_IMR(imr);
}

static struct irq_chip frv_fpga_pic = {
	.name		= "mb93091",
	.irq_ack	= frv_fpga_ack,
	.irq_mask	= frv_fpga_mask,
	.irq_mask_ack	= frv_fpga_mask_ack,
	.irq_unmask	= frv_fpga_unmask,
};

/*
 * FPGA PIC interrupt handler
 */
static irqreturn_t fpga_interrupt(int irq, void *_mask)
{
	uint16_t imr, mask = (unsigned long) _mask;

	imr = __get_IMR();
	mask = mask & ~imr & __get_IFR();

	/* poll all the triggered IRQs */
	while (mask) {
		int irq;

		asm("scan %1,gr0,%0" : "=r"(irq) : "r"(mask));
		irq = 31 - irq;
		mask &= ~(1 << irq);

		generic_handle_irq(IRQ_BASE_FPGA + irq);
	}

	return IRQ_HANDLED;
}

/*
 * define an interrupt action for each FPGA PIC output
 * - use dev_id to indicate the FPGA PIC input to output mappings
 */
static struct irqaction fpga_irq[4]  = {
	[0] = {
		.handler	= fpga_interrupt,
		.flags		= IRQF_DISABLED | IRQF_SHARED,
		.name		= "fpga.0",
		.dev_id		= (void *) 0x0028UL,
	},
	[1] = {
		.handler	= fpga_interrupt,
		.flags		= IRQF_DISABLED | IRQF_SHARED,
		.name		= "fpga.1",
		.dev_id		= (void *) 0x0050UL,
	},
	[2] = {
		.handler	= fpga_interrupt,
		.flags		= IRQF_DISABLED | IRQF_SHARED,
		.name		= "fpga.2",
		.dev_id		= (void *) 0x1c00UL,
	},
	[3] = {
		.handler	= fpga_interrupt,
		.flags		= IRQF_DISABLED | IRQF_SHARED,
		.name		= "fpga.3",
		.dev_id		= (void *) 0x6386UL,
	}
};

/*
 * initialise the motherboard FPGA's PIC
 */
void __init fpga_init(void)
{
	int irq;

	/* all PIC inputs are all set to be low-level driven, apart from the
	 * NMI button (15) which is fixed at falling-edge
	 */
	__set_IMR(0x7ffe);
	__clr_IFR(0x0000);

	for (irq = IRQ_BASE_FPGA + 1; irq <= IRQ_BASE_FPGA + 14; irq++)
		irq_set_chip_and_handler(irq, &frv_fpga_pic, handle_level_irq);

	irq_set_chip_and_handler(IRQ_FPGA_NMI, &frv_fpga_pic, handle_edge_irq);

	/* the FPGA drives the first four external IRQ inputs on the CPU PIC */
	setup_irq(IRQ_CPU_EXTERNAL0, &fpga_irq[0]);
	setup_irq(IRQ_CPU_EXTERNAL1, &fpga_irq[1]);
	setup_irq(IRQ_CPU_EXTERNAL2, &fpga_irq[2]);
	setup_irq(IRQ_CPU_EXTERNAL3, &fpga_irq[3]);
}
