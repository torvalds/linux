// SPDX-License-Identifier: GPL-2.0
/*
 * r8a7779 processor support
 *
 * Copyright (C) 2011, 2013  Renesas Solutions Corp.
 * Copyright (C) 2011  Magnus Damm
 * Copyright (C) 2013  Cogent Embedded, Inc.
 */
#include <linux/init.h>
#include <linux/irqchip.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include "common.h"
#include "r8a7779.h"

#define HPBREG_BASE	0xfe700000

/* IRQ */
#define INT2SMSKCR0	0x822a0	/* Interrupt Submask Clear Register 0 */
#define INT2SMSKCR1	0x822a4	/* Interrupt Submask Clear Register 1 */
#define INT2SMSKCR2	0x822a8	/* Interrupt Submask Clear Register 2 */
#define INT2SMSKCR3	0x822ac	/* Interrupt Submask Clear Register 3 */
#define INT2SMSKCR4	0x822b0	/* Interrupt Submask Clear Register 4 */

#define INT2NTSR0	0x00060	/* Interrupt Notification Select Register 0 */
#define INT2NTSR1	0x00064	/* Interrupt Notification Select Register 1 */

static void __init r8a7779_init_irq_dt(void)
{
	void __iomem *base = ioremap(HPBREG_BASE, 0x00100000);

	irqchip_init();

	/* route all interrupts to ARM */
	writel(0xffffffff, base + INT2NTSR0);
	writel(0x3fffffff, base + INT2NTSR1);

	/* unmask all known interrupts in INTCS2 */
	writel(0xfffffff0, base + INT2SMSKCR0);
	writel(0xfff7ffff, base + INT2SMSKCR1);
	writel(0xfffbffdf, base + INT2SMSKCR2);
	writel(0xbffffffc, base + INT2SMSKCR3);
	writel(0x003fee3f, base + INT2SMSKCR4);

	iounmap(base);
}

static const char *const r8a7779_compat_dt[] __initconst = {
	"renesas,r8a7779",
	NULL,
};

DT_MACHINE_START(R8A7779_DT, "Generic R8A7779 (Flattened Device Tree)")
	.smp		= smp_ops(r8a7779_smp_ops),
	.init_irq	= r8a7779_init_irq_dt,
	.init_late	= shmobile_init_late,
	.dt_compat	= r8a7779_compat_dt,
MACHINE_END
