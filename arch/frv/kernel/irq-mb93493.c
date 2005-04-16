/* irq-mb93493.c: MB93493 companion chip interrupt handler
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/irq.h>

#include <asm/io.h>
#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/delay.h>
#include <asm/irq.h>
#include <asm/irc-regs.h>
#include <asm/irq-routing.h>
#include <asm/mb93493-irqs.h>

static void frv_mb93493_doirq(struct irq_source *source);

/*****************************************************************************/
/*
 * MB93493 companion chip IRQ multiplexor
 */
static struct irq_source frv_mb93493[2] = {
	[0] = {
		.muxname		= "mb93493.0",
		.muxdata		= __region_CS3 + 0x3d0,
		.doirq			= frv_mb93493_doirq,
		.irqmask		= 0x0000,
	},
	[1] = {
		.muxname		= "mb93493.1",
		.muxdata		= __region_CS3 + 0x3d4,
		.doirq			= frv_mb93493_doirq,
		.irqmask		= 0x0000,
	},
};

static void frv_mb93493_control(struct irq_group *group, int index, int on)
{
	struct irq_source *source;
	uint32_t iqsr;

	if ((frv_mb93493[0].irqmask & (1 << index)))
		source = &frv_mb93493[0];
	else
		source = &frv_mb93493[1];

	iqsr = readl(source->muxdata);
	if (on)
		iqsr |= 1 << (index + 16);
	else
		iqsr &= ~(1 << (index + 16));

	writel(iqsr, source->muxdata);
}

static struct irq_group frv_mb93493_irqs = {
	.first_irq	= IRQ_BASE_MB93493,
	.control	= frv_mb93493_control,
};

static void frv_mb93493_doirq(struct irq_source *source)
{
	uint32_t mask = readl(source->muxdata);
	mask = mask & (mask >> 16) & 0xffff;

	if (mask)
		distribute_irqs(&frv_mb93493_irqs, mask);
}

static void __init mb93493_irq_route(int irq, int source)
{
	frv_mb93493[source].irqmask |= 1 << (irq - IRQ_BASE_MB93493);
	frv_mb93493_irqs.sources[irq - IRQ_BASE_MB93493] = &frv_mb93493[source];
}

void __init route_mb93493_irqs(void)
{
	frv_irq_route_external(&frv_mb93493[0], IRQ_CPU_MB93493_0);
	frv_irq_route_external(&frv_mb93493[1], IRQ_CPU_MB93493_1);

	frv_irq_set_group(&frv_mb93493_irqs);

	mb93493_irq_route(IRQ_MB93493_VDC,		IRQ_MB93493_VDC_ROUTE);
	mb93493_irq_route(IRQ_MB93493_VCC,		IRQ_MB93493_VCC_ROUTE);
	mb93493_irq_route(IRQ_MB93493_AUDIO_IN,		IRQ_MB93493_AUDIO_IN_ROUTE);
	mb93493_irq_route(IRQ_MB93493_I2C_0,		IRQ_MB93493_I2C_0_ROUTE);
	mb93493_irq_route(IRQ_MB93493_I2C_1,		IRQ_MB93493_I2C_1_ROUTE);
	mb93493_irq_route(IRQ_MB93493_USB,		IRQ_MB93493_USB_ROUTE);
	mb93493_irq_route(IRQ_MB93493_LOCAL_BUS,	IRQ_MB93493_LOCAL_BUS_ROUTE);
	mb93493_irq_route(IRQ_MB93493_PCMCIA,		IRQ_MB93493_PCMCIA_ROUTE);
	mb93493_irq_route(IRQ_MB93493_GPIO,		IRQ_MB93493_GPIO_ROUTE);
	mb93493_irq_route(IRQ_MB93493_AUDIO_OUT,	IRQ_MB93493_AUDIO_OUT_ROUTE);
}
