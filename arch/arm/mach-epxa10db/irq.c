/*
 *  linux/arch/arm/mach-epxa10db/irq.c
 *
 *  Copyright (C) 2001 Altera Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/stddef.h>
#include <linux/timer.h>
#include <linux/list.h>
#include <asm/io.h>
#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/mach/irq.h>
#include <asm/arch/platform.h>
#include <asm/arch/int_ctrl00.h>


static void epxa_mask_irq(unsigned int irq)
{
        writel(1 << irq, INT_MC(IO_ADDRESS(EXC_INT_CTRL00_BASE)));
}

static void epxa_unmask_irq(unsigned int irq)
{
        writel(1 << irq, INT_MS(IO_ADDRESS(EXC_INT_CTRL00_BASE)));
}
 

static struct irqchip epxa_irq_chip = {
	.ack		= epxa_mask_irq,
	.mask		= epxa_mask_irq,
	.unmask		= epxa_unmask_irq,
};

static struct resource irq_resource = {
	.name	= "irq_handler",
	.start	= IO_ADDRESS(EXC_INT_CTRL00_BASE),
	.end	= IO_ADDRESS(INT_PRIORITY_FC(EXC_INT_CTRL00_BASE))+4,
};

void __init epxa10db_init_irq(void)
{
	unsigned int i;
	
	request_resource(&iomem_resource, &irq_resource);

	/*
	 * This bit sets up the interrupt controller using 
	 * the 6 PLD interrupts mode (the default) each 
	 * irqs is assigned a priority which is the same
	 * as its interrupt number. This scheme is used because 
	 * its easy, but you may want to change it depending
	 * on the contents of your PLD
	 */

	writel(3,INT_MODE(IO_ADDRESS(EXC_INT_CTRL00_BASE)));
	for (i = 0; i < NR_IRQS; i++){
		writel(i+1, INT_PRIORITY_P0(IO_ADDRESS(EXC_INT_CTRL00_BASE)) + (4*i));
		set_irq_chip(i,&epxa_irq_chip);
		set_irq_handler(i,do_level_IRQ);
		set_irq_flags(i, IRQF_VALID | IRQF_PROBE);
	}

	/* Disable all interrupts */
	writel(-1,INT_MC(IO_ADDRESS(EXC_INT_CTRL00_BASE)));

}
