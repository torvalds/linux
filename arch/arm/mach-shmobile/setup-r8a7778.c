// SPDX-License-Identifier: GPL-2.0
/*
 * r8a7778 processor support
 *
 * Copyright (C) 2013  Renesas Solutions Corp.
 * Copyright (C) 2013  Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 * Copyright (C) 2013  Cogent Embedded, Inc.
 */

#include <linux/io.h>
#include <linux/irqchip.h>

#include <asm/mach/arch.h>

#include "common.h"

#define INT2SMSKCR0	0x82288 /* 0xfe782288 */
#define INT2SMSKCR1	0x8228c /* 0xfe78228c */

#define INT2NTSR0	0x00018 /* 0xfe700018 */
#define INT2NTSR1	0x0002c /* 0xfe70002c */

static void __init r8a7778_init_irq_dt(void)
{
	void __iomem *base = ioremap(0xfe700000, 0x00100000);

	BUG_ON(!base);

	irqchip_init();

	/* route all interrupts to ARM */
	__raw_writel(0x73ffffff, base + INT2NTSR0);
	__raw_writel(0xffffffff, base + INT2NTSR1);

	/* unmask all known interrupts in INTCS2 */
	__raw_writel(0x08330773, base + INT2SMSKCR0);
	__raw_writel(0x00311110, base + INT2SMSKCR1);

	iounmap(base);
}

static const char *const r8a7778_compat_dt[] __initconst = {
	"renesas,r8a7778",
	NULL,
};

DT_MACHINE_START(R8A7778_DT, "Generic R8A7778 (Flattened Device Tree)")
	.init_early	= shmobile_init_delay,
	.init_irq	= r8a7778_init_irq_dt,
	.init_late	= shmobile_init_late,
	.dt_compat	= r8a7778_compat_dt,
MACHINE_END
