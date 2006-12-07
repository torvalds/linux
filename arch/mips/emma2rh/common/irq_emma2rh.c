/*
 *  arch/mips/emma2rh/common/irq_emma2rh.c
 *      This file defines the irq handler for EMMA2RH.
 *
 *  Copyright (C) NEC Electronics Corporation 2005-2006
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

/*
 * EMMA2RH defines 64 IRQs.
 *
 * This file exports one function:
 *	emma2rh_irq_init(u32 irq_base);
 */

#include <linux/interrupt.h>
#include <linux/types.h>
#include <linux/ptrace.h>

#include <asm/debug.h>

#include <asm/emma2rh/emma2rh.h>

/* number of total irqs supported by EMMA2RH */
#define	NUM_EMMA2RH_IRQ		96

static int emma2rh_irq_base = -1;

void ll_emma2rh_irq_enable(int);
void ll_emma2rh_irq_disable(int);

static void emma2rh_irq_enable(unsigned int irq)
{
	ll_emma2rh_irq_enable(irq - emma2rh_irq_base);
}

static void emma2rh_irq_disable(unsigned int irq)
{
	ll_emma2rh_irq_disable(irq - emma2rh_irq_base);
}

struct irq_chip emma2rh_irq_controller = {
	.typename = "emma2rh_irq",
	.ack = emma2rh_irq_disable,
	.mask = emma2rh_irq_disable,
	.mask_ack = emma2rh_irq_disable,
	.unmask = emma2rh_irq_enable,
};

void emma2rh_irq_init(u32 irq_base)
{
	u32 i;

	for (i = irq_base; i < irq_base + NUM_EMMA2RH_IRQ; i++)
		set_irq_chip_and_handler(i, &emma2rh_irq_controller,
					 handle_level_irq);

	emma2rh_irq_base = irq_base;
}

void ll_emma2rh_irq_enable(int emma2rh_irq)
{
	u32 reg_value;
	u32 reg_bitmask;
	u32 reg_index;

	reg_index = EMMA2RH_BHIF_INT_EN_0
	    + (EMMA2RH_BHIF_INT_EN_1 - EMMA2RH_BHIF_INT_EN_0)
	    * (emma2rh_irq / 32);
	reg_value = emma2rh_in32(reg_index);
	reg_bitmask = 0x1 << (emma2rh_irq % 32);
	db_assert((reg_value & reg_bitmask) == 0);
	emma2rh_out32(reg_index, reg_value | reg_bitmask);
}

void ll_emma2rh_irq_disable(int emma2rh_irq)
{
	u32 reg_value;
	u32 reg_bitmask;
	u32 reg_index;

	reg_index = EMMA2RH_BHIF_INT_EN_0
	    + (EMMA2RH_BHIF_INT_EN_1 - EMMA2RH_BHIF_INT_EN_0)
	    * (emma2rh_irq / 32);
	reg_value = emma2rh_in32(reg_index);
	reg_bitmask = 0x1 << (emma2rh_irq % 32);
	db_assert((reg_value & reg_bitmask) != 0);
	emma2rh_out32(reg_index, reg_value & ~reg_bitmask);
}
