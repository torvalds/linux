// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2005-2017 Andes Technology Corporation

#include <linux/irqchip.h>

void __init init_IRQ(void)
{
	irqchip_init();
}
