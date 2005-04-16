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

#define __reg16(ADDR) (*(volatile unsigned short *)(ADDR))

#define __get_IMR()	({ __reg16(0xffc00004); })
#define __set_IMR(M)	do { __reg16(0xffc00004) = (M); wmb(); } while(0)
#define __get_IFR()	({ __reg16(0xffc0000c); })
#define __clr_IFR(M)	do { __reg16(0xffc0000c) = ~(M); wmb(); } while(0)

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

	__FPGA(0, 0x0028),
	__FPGA(1, 0x0050),
	__FPGA(2, 0x1c00),
	__FPGA(3, 0x6386),
};

static struct irq_group frv_fpga_irqs = {
	.first_irq	= IRQ_BASE_FPGA,
	.control	= frv_fpga_control,
	.sources = {
		[ 1] = &frv_fpga[3],
		[ 2] = &frv_fpga[3],
		[ 3] = &frv_fpga[0],
		[ 4] = &frv_fpga[1],
		[ 5] = &frv_fpga[0],
		[ 6] = &frv_fpga[1],
		[ 7] = &frv_fpga[3],
		[ 8] = &frv_fpga[3],
		[ 9] = &frv_fpga[3],
		[10] = &frv_fpga[2],
		[11] = &frv_fpga[2],
		[12] = &frv_fpga[2],
		[13] = &frv_fpga[3],
		[14] = &frv_fpga[3],
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
	__set_IMR(0x7ffe);
	__clr_IFR(0x0000);

	frv_irq_route_external(&frv_fpga[0], IRQ_CPU_EXTERNAL0);
	frv_irq_route_external(&frv_fpga[1], IRQ_CPU_EXTERNAL1);
	frv_irq_route_external(&frv_fpga[2], IRQ_CPU_EXTERNAL2);
	frv_irq_route_external(&frv_fpga[3], IRQ_CPU_EXTERNAL3);
	frv_irq_set_group(&frv_fpga_irqs);
}
