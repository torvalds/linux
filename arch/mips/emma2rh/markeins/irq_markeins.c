/*
 *  arch/mips/emma2rh/markeins/irq_markeins.c
 *      This file defines the irq handler for Mark-eins.
 *
 *  Copyright (C) NEC Electronics Corporation 2004-2006
 *
 *  This file is based on the arch/mips/ddb5xxx/ddb5477/irq_5477.c
 *
 *	Copyright 2001 MontaVista Software Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/types.h>
#include <linux/ptrace.h>

#include <asm/debug.h>
#include <asm/emma2rh/emma2rh.h>

static int emma2rh_sw_irq_base = -1;
static int emma2rh_gpio_irq_base = -1;

void ll_emma2rh_sw_irq_enable(int reg);
void ll_emma2rh_sw_irq_disable(int reg);
void ll_emma2rh_gpio_irq_enable(int reg);
void ll_emma2rh_gpio_irq_disable(int reg);

static void emma2rh_sw_irq_enable(unsigned int irq)
{
	ll_emma2rh_sw_irq_enable(irq - emma2rh_sw_irq_base);
}

static void emma2rh_sw_irq_disable(unsigned int irq)
{
	ll_emma2rh_sw_irq_disable(irq - emma2rh_sw_irq_base);
}

static unsigned int emma2rh_sw_irq_startup(unsigned int irq)
{
	emma2rh_sw_irq_enable(irq);
	return 0;
}

#define emma2rh_sw_irq_shutdown emma2rh_sw_irq_disable

static void emma2rh_sw_irq_ack(unsigned int irq)
{
	ll_emma2rh_sw_irq_disable(irq - emma2rh_sw_irq_base);
}

static void emma2rh_sw_irq_end(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED | IRQ_INPROGRESS)))
		ll_emma2rh_sw_irq_enable(irq - emma2rh_sw_irq_base);
}

struct irq_chip emma2rh_sw_irq_controller = {
	.typename = "emma2rh_sw_irq",
	.startup = emma2rh_sw_irq_startup,
	.shutdown = emma2rh_sw_irq_shutdown,
	.enable = emma2rh_sw_irq_enable,
	.disable = emma2rh_sw_irq_disable,
	.ack = emma2rh_sw_irq_ack,
	.end = emma2rh_sw_irq_end,
	.set_affinity = NULL,
};

void emma2rh_sw_irq_init(u32 irq_base)
{
	u32 i;

	for (i = irq_base; i < irq_base + NUM_EMMA2RH_IRQ_SW; i++) {
		irq_desc[i].status = IRQ_DISABLED;
		irq_desc[i].action = NULL;
		irq_desc[i].depth = 2;
		irq_desc[i].chip = &emma2rh_sw_irq_controller;
	}

	emma2rh_sw_irq_base = irq_base;
}

void ll_emma2rh_sw_irq_enable(int irq)
{
	u32 reg;

	db_assert(irq >= 0);
	db_assert(irq < NUM_EMMA2RH_IRQ_SW);

	reg = emma2rh_in32(EMMA2RH_BHIF_SW_INT_EN);
	reg |= 1 << irq;
	emma2rh_out32(EMMA2RH_BHIF_SW_INT_EN, reg);
}

void ll_emma2rh_sw_irq_disable(int irq)
{
	u32 reg;

	db_assert(irq >= 0);
	db_assert(irq < 32);

	reg = emma2rh_in32(EMMA2RH_BHIF_SW_INT_EN);
	reg &= ~(1 << irq);
	emma2rh_out32(EMMA2RH_BHIF_SW_INT_EN, reg);
}

static void emma2rh_gpio_irq_enable(unsigned int irq)
{
	ll_emma2rh_gpio_irq_enable(irq - emma2rh_gpio_irq_base);
}

static void emma2rh_gpio_irq_disable(unsigned int irq)
{
	ll_emma2rh_gpio_irq_disable(irq - emma2rh_gpio_irq_base);
}

static unsigned int emma2rh_gpio_irq_startup(unsigned int irq)
{
	emma2rh_gpio_irq_enable(irq);
	return 0;
}

#define emma2rh_gpio_irq_shutdown emma2rh_gpio_irq_disable

static void emma2rh_gpio_irq_ack(unsigned int irq)
{
	irq -= emma2rh_gpio_irq_base;
	emma2rh_out32(EMMA2RH_GPIO_INT_ST, ~(1 << irq));
	ll_emma2rh_gpio_irq_disable(irq);
}

static void emma2rh_gpio_irq_end(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED | IRQ_INPROGRESS)))
		ll_emma2rh_gpio_irq_enable(irq - emma2rh_gpio_irq_base);
}

struct irq_chip emma2rh_gpio_irq_controller = {
	.typename = "emma2rh_gpio_irq",
	.startup = emma2rh_gpio_irq_startup,
	.shutdown = emma2rh_gpio_irq_shutdown,
	.enable = emma2rh_gpio_irq_enable,
	.disable = emma2rh_gpio_irq_disable,
	.ack = emma2rh_gpio_irq_ack,
	.end = emma2rh_gpio_irq_end,
	.set_affinity = NULL,
};

void emma2rh_gpio_irq_init(u32 irq_base)
{
	u32 i;

	for (i = irq_base; i < irq_base + NUM_EMMA2RH_IRQ_GPIO; i++) {
		irq_desc[i].status = IRQ_DISABLED;
		irq_desc[i].action = NULL;
		irq_desc[i].depth = 2;
		irq_desc[i].chip = &emma2rh_gpio_irq_controller;
	}

	emma2rh_gpio_irq_base = irq_base;
}

void ll_emma2rh_gpio_irq_enable(int irq)
{
	u32 reg;

	db_assert(irq >= 0);
	db_assert(irq < NUM_EMMA2RH_IRQ_GPIO);

	reg = emma2rh_in32(EMMA2RH_GPIO_INT_MASK);
	reg |= 1 << irq;
	emma2rh_out32(EMMA2RH_GPIO_INT_MASK, reg);
}

void ll_emma2rh_gpio_irq_disable(int irq)
{
	u32 reg;

	db_assert(irq >= 0);
	db_assert(irq < NUM_EMMA2RH_IRQ_GPIO);

	reg = emma2rh_in32(EMMA2RH_GPIO_INT_MASK);
	reg &= ~(1 << irq);
	emma2rh_out32(EMMA2RH_GPIO_INT_MASK, reg);
}
