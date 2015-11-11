/* ASB2364 FPGA interrupt multiplexing
 *
 * Copyright (C) 2010 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <unit/fpga-regs.h>

/*
 * FPGA PIC operations
 */
static void asb2364_fpga_mask(struct irq_data *d)
{
	ASB2364_FPGA_REG_MASK(d->irq - NR_CPU_IRQS) = 0x0001;
	SyncExBus();
}

static void asb2364_fpga_ack(struct irq_data *d)
{
	ASB2364_FPGA_REG_IRQ(d->irq - NR_CPU_IRQS) = 0x0001;
	SyncExBus();
}

static void asb2364_fpga_mask_ack(struct irq_data *d)
{
	ASB2364_FPGA_REG_MASK(d->irq - NR_CPU_IRQS) = 0x0001;
	SyncExBus();
	ASB2364_FPGA_REG_IRQ(d->irq - NR_CPU_IRQS) = 0x0001;
	SyncExBus();
}

static void asb2364_fpga_unmask(struct irq_data *d)
{
	ASB2364_FPGA_REG_MASK(d->irq - NR_CPU_IRQS) = 0x0000;
	SyncExBus();
}

static struct irq_chip asb2364_fpga_pic = {
	.name		= "fpga",
	.irq_ack	= asb2364_fpga_ack,
	.irq_mask	= asb2364_fpga_mask,
	.irq_mask_ack	= asb2364_fpga_mask_ack,
	.irq_unmask	= asb2364_fpga_unmask,
};

/*
 * FPGA PIC interrupt handler
 */
static irqreturn_t fpga_interrupt(int irq, void *_mask)
{
	if ((ASB2364_FPGA_REG_IRQ_LAN  & 0x0001) != 0x0001)
		generic_handle_irq(FPGA_LAN_IRQ);
	if ((ASB2364_FPGA_REG_IRQ_UART & 0x0001) != 0x0001)
		generic_handle_irq(FPGA_UART_IRQ);
	if ((ASB2364_FPGA_REG_IRQ_I2C  & 0x0001) != 0x0001)
		generic_handle_irq(FPGA_I2C_IRQ);
	if ((ASB2364_FPGA_REG_IRQ_USB  & 0x0001) != 0x0001)
		generic_handle_irq(FPGA_USB_IRQ);
	if ((ASB2364_FPGA_REG_IRQ_FPGA & 0x0001) != 0x0001)
		generic_handle_irq(FPGA_FPGA_IRQ);

	return IRQ_HANDLED;
}

/*
 * Define an interrupt action for each FPGA PIC output
 */
static struct irqaction fpga_irq[]  = {
	[0] = {
		.handler	= fpga_interrupt,
		.flags		= IRQF_SHARED,
		.name		= "fpga",
	},
};

/*
 * Initialise the FPGA's PIC
 */
void __init irq_fpga_init(void)
{
	int irq;

	ASB2364_FPGA_REG_MASK_LAN  = 0x0001;
	SyncExBus();
	ASB2364_FPGA_REG_MASK_UART = 0x0001;
	SyncExBus();
	ASB2364_FPGA_REG_MASK_I2C  = 0x0001;
	SyncExBus();
	ASB2364_FPGA_REG_MASK_USB  = 0x0001;
	SyncExBus();
	ASB2364_FPGA_REG_MASK_FPGA = 0x0001;
	SyncExBus();

	for (irq = NR_CPU_IRQS; irq < NR_IRQS; irq++)
		irq_set_chip_and_handler(irq, &asb2364_fpga_pic,
					 handle_level_irq);

	/* the FPGA drives the XIRQ1 input on the CPU PIC */
	setup_irq(XIRQ1, &fpga_irq[0]);
}
