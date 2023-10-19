// SPDX-License-Identifier: GPL-2.0
/*
 *  N64 IRQ
 *
 *  Copyright (C) 2021 Lauri Kasanen
 */
#include <linux/export.h>
#include <linux/interrupt.h>
#include <linux/irq.h>

#include <asm/irq_cpu.h>

void __init arch_init_irq(void)
{
	mips_cpu_irq_init();
}
