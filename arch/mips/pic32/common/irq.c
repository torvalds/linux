// SPDX-License-Identifier: GPL-2.0-only
/*
 * Joshua Henderson <joshua.henderson@microchip.com>
 * Copyright (C) 2015 Microchip Technology Inc.  All rights reserved.
 */
#include <linux/init.h>
#include <linux/irqchip.h>
#include <asm/irq.h>

void __init arch_init_irq(void)
{
	irqchip_init();
}
