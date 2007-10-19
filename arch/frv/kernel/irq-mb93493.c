/* irq-mb93493.c: MB93493 companion chip interrupt handler
 *
 * Copyright (C) 2006 Red Hat, Inc. All Rights Reserved.
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
#include <asm/mb93493-irqs.h>
#include <asm/mb93493-regs.h>

#define IRQ_ROUTE_ONE(X) (X##_ROUTE << (X - IRQ_BASE_MB93493))

#define IRQ_ROUTING					\
	(IRQ_ROUTE_ONE(IRQ_MB93493_VDC)		|	\
	 IRQ_ROUTE_ONE(IRQ_MB93493_VCC)		|	\
	 IRQ_ROUTE_ONE(IRQ_MB93493_AUDIO_OUT)	|	\
	 IRQ_ROUTE_ONE(IRQ_MB93493_I2C_0)	|	\
	 IRQ_ROUTE_ONE(IRQ_MB93493_I2C_1)	|	\
	 IRQ_ROUTE_ONE(IRQ_MB93493_USB)		|	\
	 IRQ_ROUTE_ONE(IRQ_MB93493_LOCAL_BUS)	|	\
	 IRQ_ROUTE_ONE(IRQ_MB93493_PCMCIA)	|	\
	 IRQ_ROUTE_ONE(IRQ_MB93493_GPIO)	|	\
	 IRQ_ROUTE_ONE(IRQ_MB93493_AUDIO_IN))

/*
 * daughter board PIC operations
 * - there is no way to ACK interrupts in the MB93493 chip
 */
static void frv_mb93493_mask(unsigned int irq)
{
	uint32_t iqsr;
	volatile void *piqsr;

	if (IRQ_ROUTING & (1 << (irq - IRQ_BASE_MB93493)))
		piqsr = __addr_MB93493_IQSR(1);
	else
		piqsr = __addr_MB93493_IQSR(0);

	iqsr = readl(piqsr);
	iqsr &= ~(1 << (irq - IRQ_BASE_MB93493 + 16));
	writel(iqsr, piqsr);
}

static void frv_mb93493_ack(unsigned int irq)
{
}

static void frv_mb93493_unmask(unsigned int irq)
{
	uint32_t iqsr;
	volatile void *piqsr;

	if (IRQ_ROUTING & (1 << (irq - IRQ_BASE_MB93493)))
		piqsr = __addr_MB93493_IQSR(1);
	else
		piqsr = __addr_MB93493_IQSR(0);

	iqsr = readl(piqsr);
	iqsr |= 1 << (irq - IRQ_BASE_MB93493 + 16);
	writel(iqsr, piqsr);
}

static struct irq_chip frv_mb93493_pic = {
	.name		= "mb93093",
	.ack		= frv_mb93493_ack,
	.mask		= frv_mb93493_mask,
	.mask_ack	= frv_mb93493_mask,
	.unmask		= frv_mb93493_unmask,
};

/*
 * MB93493 PIC interrupt handler
 */
static irqreturn_t mb93493_interrupt(int irq, void *_piqsr)
{
	volatile void *piqsr = _piqsr;
	uint32_t iqsr;

	iqsr = readl(piqsr);
	iqsr = iqsr & (iqsr >> 16) & 0xffff;

	/* poll all the triggered IRQs */
	while (iqsr) {
		int irq;

		asm("scan %1,gr0,%0" : "=r"(irq) : "r"(iqsr));
		irq = 31 - irq;
		iqsr &= ~(1 << irq);

		generic_handle_irq(IRQ_BASE_MB93493 + irq);
	}

	return IRQ_HANDLED;
}

/*
 * define an interrupt action for each MB93493 PIC output
 * - use dev_id to indicate the MB93493 PIC input to output mappings
 */
static struct irqaction mb93493_irq[2]  = {
	[0] = {
		.handler	= mb93493_interrupt,
		.flags		= IRQF_DISABLED | IRQF_SHARED,
		.mask		= CPU_MASK_NONE,
		.name		= "mb93493.0",
		.dev_id		= (void *) __addr_MB93493_IQSR(0),
	},
	[1] = {
		.handler	= mb93493_interrupt,
		.flags		= IRQF_DISABLED | IRQF_SHARED,
		.mask		= CPU_MASK_NONE,
		.name		= "mb93493.1",
		.dev_id		= (void *) __addr_MB93493_IQSR(1),
	}
};

/*
 * initialise the motherboard MB93493's PIC
 */
void __init mb93493_init(void)
{
	int irq;

	for (irq = IRQ_BASE_MB93493 + 0; irq <= IRQ_BASE_MB93493 + 10; irq++)
		set_irq_chip_and_handler(irq, &frv_mb93493_pic, handle_edge_irq);

	/* the MB93493 drives external IRQ inputs on the CPU PIC */
	setup_irq(IRQ_CPU_MB93493_0, &mb93493_irq[0]);
	setup_irq(IRQ_CPU_MB93493_1, &mb93493_irq[1]);
}
