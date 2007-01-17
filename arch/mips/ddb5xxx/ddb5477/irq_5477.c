/*
 * Copyright 2001 MontaVista Software Inc.
 * Author: Jun Sun, jsun@mvista.com or jsun@junsun.net
 *
 *  arch/mips/ddb5xxx/ddb5477/irq_5477.c
 *     This file defines the irq handler for Vrc5477.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

/*
 * Vrc5477 defines 32 IRQs.
 *
 * This file exports one function:
 *	vrc5477_irq_init(u32 irq_base);
 */

#include <linux/interrupt.h>
#include <linux/types.h>
#include <linux/ptrace.h>

#include <asm/debug.h>

#include <asm/ddb5xxx/ddb5xxx.h>

/* number of total irqs supported by Vrc5477 */
#define	NUM_5477_IRQ		32

static int vrc5477_irq_base = -1;


static void
vrc5477_irq_enable(unsigned int irq)
{
	db_assert(vrc5477_irq_base != -1);
	db_assert(irq >= vrc5477_irq_base);
	db_assert(irq < vrc5477_irq_base+ NUM_5477_IRQ);

	ll_vrc5477_irq_enable(irq - vrc5477_irq_base);
}

static void
vrc5477_irq_disable(unsigned int irq)
{
	db_assert(vrc5477_irq_base != -1);
	db_assert(irq >= vrc5477_irq_base);
	db_assert(irq < vrc5477_irq_base + NUM_5477_IRQ);

	ll_vrc5477_irq_disable(irq - vrc5477_irq_base);
}

static void
vrc5477_irq_ack(unsigned int irq)
{
	db_assert(vrc5477_irq_base != -1);
	db_assert(irq >= vrc5477_irq_base);
	db_assert(irq < vrc5477_irq_base+ NUM_5477_IRQ);

	/* clear the interrupt bit */
	/* some irqs require the driver to clear the sources */
	ddb_out32(DDB_INTCLR32, 1 << (irq - vrc5477_irq_base));

	/* disable interrupt - some handler will re-enable the irq
	 * and if the interrupt is leveled, we will have infinite loop
	 */
	ll_vrc5477_irq_disable(irq - vrc5477_irq_base);
}

static void
vrc5477_irq_end(unsigned int irq)
{
	db_assert(vrc5477_irq_base != -1);
	db_assert(irq >= vrc5477_irq_base);
	db_assert(irq < vrc5477_irq_base + NUM_5477_IRQ);

	if(!(irq_desc[irq].status & (IRQ_DISABLED | IRQ_INPROGRESS)))
		ll_vrc5477_irq_enable( irq - vrc5477_irq_base);
}

struct irq_chip vrc5477_irq_controller = {
	.typename = "vrc5477_irq",
	.ack = vrc5477_irq_ack,
	.mask = vrc5477_irq_disable,
	.mask_ack = vrc5477_irq_ack,
	.unmask = vrc5477_irq_enable,
	.end = vrc5477_irq_end
};

void __init vrc5477_irq_init(u32 irq_base)
{
	u32 i;

	for (i= irq_base; i< irq_base+ NUM_5477_IRQ; i++)
		set_irq_chip(i, &vrc5477_irq_controller);

	vrc5477_irq_base = irq_base;
}

void ll_vrc5477_irq_route(int vrc5477_irq, int ip)
{
	u32 reg_value;
	u32 reg_bitmask;
	u32 reg_index;

	db_assert(vrc5477_irq >= 0);
	db_assert(vrc5477_irq < NUM_5477_IRQ);
	db_assert(ip >= 0);
	db_assert((ip < 5) || (ip == 6));

	reg_index = DDB_INTCTRL0 + vrc5477_irq/8*4;
	reg_value = ddb_in32(reg_index);
	reg_bitmask = 7 << (vrc5477_irq % 8 * 4);
	reg_value &= ~reg_bitmask;
	reg_value |= ip << (vrc5477_irq % 8 * 4);
	ddb_out32(reg_index, reg_value);
}

void ll_vrc5477_irq_enable(int vrc5477_irq)
{
	u32 reg_value;
	u32 reg_bitmask;
	u32 reg_index;

	db_assert(vrc5477_irq >= 0);
	db_assert(vrc5477_irq < NUM_5477_IRQ);

	reg_index = DDB_INTCTRL0 + vrc5477_irq/8*4;
	reg_value = ddb_in32(reg_index);
	reg_bitmask = 8 << (vrc5477_irq % 8 * 4);
	db_assert((reg_value & reg_bitmask) == 0);
	ddb_out32(reg_index, reg_value | reg_bitmask);
}

void ll_vrc5477_irq_disable(int vrc5477_irq)
{
	u32 reg_value;
	u32 reg_bitmask;
	u32 reg_index;

	db_assert(vrc5477_irq >= 0);
	db_assert(vrc5477_irq < NUM_5477_IRQ);

	reg_index = DDB_INTCTRL0 + vrc5477_irq/8*4;
	reg_value = ddb_in32(reg_index);
	reg_bitmask = 8 << (vrc5477_irq % 8 * 4);

	/* we assert that the interrupt is enabled (perhaps over-zealous) */
	db_assert( (reg_value & reg_bitmask) != 0);
	ddb_out32(reg_index, reg_value & ~reg_bitmask);
}
