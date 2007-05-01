/*
 *  arch/mips/emma2rh/markeins/irq.c
 *      This file defines the irq handler for EMMA2RH.
 *
 *  Copyright (C) NEC Electronics Corporation 2004-2006
 *
 *  This file is based on the arch/mips/ddb5xxx/ddb5477/irq.c
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
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/delay.h>

#include <asm/irq_cpu.h>
#include <asm/system.h>
#include <asm/mipsregs.h>
#include <asm/debug.h>
#include <asm/addrspace.h>
#include <asm/bootinfo.h>

#include <asm/emma2rh/emma2rh.h>

/*
 * IRQ mapping
 *
 *  0-7: 8 CPU interrupts
 *	0 -	software interrupt 0
 *	1 - 	software interrupt 1
 *	2 - 	most Vrc5477 interrupts are routed to this pin
 *	3 - 	(optional) some other interrupts routed to this pin for debugg
 *	4 - 	not used
 *	5 - 	not used
 *	6 - 	not used
 *	7 - 	cpu timer (used by default)
 *
 */

extern void emma2rh_sw_irq_init(u32 base);
extern void emma2rh_gpio_irq_init(u32 base);
extern void emma2rh_irq_init(u32 base);
extern void emma2rh_irq_dispatch(void);

static struct irqaction irq_cascade = {
	   .handler = no_action,
	   .flags = 0,
	   .mask = CPU_MASK_NONE,
	   .name = "cascade",
	   .dev_id = NULL,
	   .next = NULL,
};

void __init arch_init_irq(void)
{
	u32 reg;

	db_run(printk("markeins_irq_setup invoked.\n"));

	/* by default, interrupts are disabled. */
	emma2rh_out32(EMMA2RH_BHIF_INT_EN_0, 0);
	emma2rh_out32(EMMA2RH_BHIF_INT_EN_1, 0);
	emma2rh_out32(EMMA2RH_BHIF_INT_EN_2, 0);
	emma2rh_out32(EMMA2RH_BHIF_INT1_EN_0, 0);
	emma2rh_out32(EMMA2RH_BHIF_INT1_EN_1, 0);
	emma2rh_out32(EMMA2RH_BHIF_INT1_EN_2, 0);
	emma2rh_out32(EMMA2RH_BHIF_SW_INT_EN, 0);

	clear_c0_status(0xff00);
	set_c0_status(0x0400);

#define GPIO_PCI (0xf<<15)
	/* setup GPIO interrupt for PCI interface */
	/* direction input */
	reg = emma2rh_in32(EMMA2RH_GPIO_DIR);
	emma2rh_out32(EMMA2RH_GPIO_DIR, reg & ~GPIO_PCI);
	/* disable interrupt */
	reg = emma2rh_in32(EMMA2RH_GPIO_INT_MASK);
	emma2rh_out32(EMMA2RH_GPIO_INT_MASK, reg & ~GPIO_PCI);
	/* level triggerd */
	reg = emma2rh_in32(EMMA2RH_GPIO_INT_MODE);
	emma2rh_out32(EMMA2RH_GPIO_INT_MODE, reg | GPIO_PCI);
	reg = emma2rh_in32(EMMA2RH_GPIO_INT_CND_A);
	emma2rh_out32(EMMA2RH_GPIO_INT_CND_A, reg & (~GPIO_PCI));
	/* interrupt clear */
	emma2rh_out32(EMMA2RH_GPIO_INT_ST, ~GPIO_PCI);

	/* init all controllers */
	emma2rh_irq_init(EMMA2RH_IRQ_BASE);
	emma2rh_sw_irq_init(EMMA2RH_SW_IRQ_BASE);
	emma2rh_gpio_irq_init(EMMA2RH_GPIO_IRQ_BASE);
	mips_cpu_irq_init();

	/* setup cascade interrupts */
	setup_irq(EMMA2RH_IRQ_BASE + EMMA2RH_SW_CASCADE, &irq_cascade);
	setup_irq(EMMA2RH_IRQ_BASE + EMMA2RH_GPIO_CASCADE, &irq_cascade);
	setup_irq(CPU_IRQ_BASE + CPU_EMMA2RH_CASCADE, &irq_cascade);
}

asmlinkage void plat_irq_dispatch(void)
{
        unsigned int pending = read_c0_status() & read_c0_cause() & ST0_IM;

	if (pending & STATUSF_IP7)
		do_IRQ(CPU_IRQ_BASE + 7);
	else if (pending & STATUSF_IP2)
		emma2rh_irq_dispatch();
	else if (pending & STATUSF_IP1)
		do_IRQ(CPU_IRQ_BASE + 1);
	else if (pending & STATUSF_IP0)
		do_IRQ(CPU_IRQ_BASE + 0);
	else
		spurious_interrupt();
}


