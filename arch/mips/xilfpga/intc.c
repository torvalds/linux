/*
 * Xilfpga interrupt controller setup
 *
 * Copyright (C) 2015 Imagination Technologies
 * Author: Zubair Lutfullah Kakakhel <Zubair.Kakakhel@imgtec.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */

#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/irqchip.h>

#include <asm/irq_cpu.h>


void __init arch_init_irq(void)
{
	irqchip_init();
}
