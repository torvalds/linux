/* irq-mb93093.c: MB93093 FPGA interrupt handling
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

#define __reg16(ADDR) (*(volatile unsigned short *)(__region_CS2 + (ADDR)))

#define __get_IMR()	({ __reg16(0x0a); })
#define __set_IMR(M)	do { __reg16(0x0a) = (M);  wmb(); } while(0)
#define __get_IFR()	({ __reg16(0x02); })
#define __clr_IFR(M)	do { __reg16(0x02) = ~(M); wmb(); } while(0)

static void frv_fpga_doirq(struct irq_source *source);
static void frv_fpga_control(struct irq_group *group, int irq, int on);

/*****************************************************************************/
/*
 * FPGA IRQ multiplexor
 */
static struct irq_source frv_fpga[4] = {
#define __FPGA(X, M)					\
	[X] = {						\
		.muxname	= "fpga."#X,		\
		.irqmask	= M,			\
		.doirq		= frv_fpga_doirq,	\
	}

	__FPGA(0, 0x0700),
};

static struct irq_group frv_fpga_irqs = {
	.first_irq	= IRQ_BASE_FPGA,
	.control	= frv_fpga_control,
	.sources = {
		[ 8] = &frv_fpga[0],
		[ 9] = &frv_fpga[0],
		[10] = &frv_fpga[0],
	},
};


static void frv_fpga_control(struct irq_group *group, int index, int on)
{
	uint16_t imr = __get_IMR();

	if (on)
		imr &= ~(1 << index);
	else
		imr |= 1 << index;

	__set_IMR(imr);
}

static void frv_fpga_doirq(struct irq_source *source)
{
	uint16_t mask, imr;

	imr = __get_IMR();
	mask = source->irqmask & ~imr & __get_IFR();
	if (mask) {
		__set_IMR(imr | mask);
		__clr_IFR(mask);
		distribute_irqs(&frv_fpga_irqs, mask);
		__set_IMR(imr);
	}
}

void __init fpga_init(void)
{
	__set_IMR(0x0700);
	__clr_IFR(0x0000);

	frv_irq_route_external(&frv_fpga[0], IRQ_CPU_EXTERNAL2);
	frv_irq_set_group(&frv_fpga_irqs);
}
