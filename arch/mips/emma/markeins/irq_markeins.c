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
#include <asm/emma/emma2rh.h>

void ll_emma2rh_sw_irq_enable(int reg);
void ll_emma2rh_sw_irq_disable(int reg);
void ll_emma2rh_gpio_irq_enable(int reg);
void ll_emma2rh_gpio_irq_disable(int reg);

static void emma2rh_sw_irq_enable(unsigned int irq)
{
	ll_emma2rh_sw_irq_enable(irq - EMMA2RH_SW_IRQ_BASE);
}

static void emma2rh_sw_irq_disable(unsigned int irq)
{
	ll_emma2rh_sw_irq_disable(irq - EMMA2RH_SW_IRQ_BASE);
}

struct irq_chip emma2rh_sw_irq_controller = {
	.name = "emma2rh_sw_irq",
	.ack = emma2rh_sw_irq_disable,
	.mask = emma2rh_sw_irq_disable,
	.mask_ack = emma2rh_sw_irq_disable,
	.unmask = emma2rh_sw_irq_enable,
};

void emma2rh_sw_irq_init(void)
{
	u32 i;

	for (i = 0; i < NUM_EMMA2RH_IRQ_SW; i++)
		set_irq_chip_and_handler(EMMA2RH_SW_IRQ_BASE + i,
					 &emma2rh_sw_irq_controller,
					 handle_level_irq);
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
	ll_emma2rh_gpio_irq_enable(irq - EMMA2RH_GPIO_IRQ_BASE);
}

static void emma2rh_gpio_irq_disable(unsigned int irq)
{
	ll_emma2rh_gpio_irq_disable(irq - EMMA2RH_GPIO_IRQ_BASE);
}

static void emma2rh_gpio_irq_ack(unsigned int irq)
{
	irq -= EMMA2RH_GPIO_IRQ_BASE;
	emma2rh_out32(EMMA2RH_GPIO_INT_ST, ~(1 << irq));
	ll_emma2rh_gpio_irq_disable(irq);
}

static void emma2rh_gpio_irq_end(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED | IRQ_INPROGRESS)))
		ll_emma2rh_gpio_irq_enable(irq - EMMA2RH_GPIO_IRQ_BASE);
}

struct irq_chip emma2rh_gpio_irq_controller = {
	.name = "emma2rh_gpio_irq",
	.ack = emma2rh_gpio_irq_ack,
	.mask = emma2rh_gpio_irq_disable,
	.mask_ack = emma2rh_gpio_irq_ack,
	.unmask = emma2rh_gpio_irq_enable,
	.end = emma2rh_gpio_irq_end,
};

void emma2rh_gpio_irq_init(void)
{
	u32 i;

	for (i = 0; i < NUM_EMMA2RH_IRQ_GPIO; i++)
		set_irq_chip(EMMA2RH_GPIO_IRQ_BASE + i,
			     &emma2rh_gpio_irq_controller);
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
