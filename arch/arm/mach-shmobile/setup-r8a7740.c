// SPDX-License-Identifier: GPL-2.0
/*
 * R8A7740 processor support
 *
 * Copyright (C) 2011  Renesas Solutions Corp.
 * Copyright (C) 2011  Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/irqchip.h>
#include <linux/irqchip/arm-gic.h>

#include <asm/mach/map.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>

#include "common.h"

/*
 * r8a7740 chip has lasting errata on MERAM buffer.
 * this is work-around for it.
 * see
 *	"Media RAM (MERAM)" on r8a7740 documentation
 */
#define MEBUFCNTR	0xFE950098
static void __init r8a7740_meram_workaround(void)
{
	void __iomem *reg;

	reg = ioremap(MEBUFCNTR, 4);
	if (reg) {
		iowrite32(0x01600164, reg);
		iounmap(reg);
	}
}

static void __init r8a7740_init_irq_of(void)
{
	void __iomem *intc_prio_base = ioremap(0xe6900010, 0x10);
	void __iomem *intc_msk_base = ioremap(0xe6900040, 0x10);
	void __iomem *pfc_inta_ctrl = ioremap(0xe605807c, 0x4);

	irqchip_init();

	/* route signals to GIC */
	iowrite32(0x0, pfc_inta_ctrl);

	/*
	 * To mask the shared interrupt to SPI 149 we must ensure to set
	 * PRIO *and* MASK. Else we run into IRQ floods when registering
	 * the intc_irqpin devices
	 */
	iowrite32(0x0, intc_prio_base + 0x0);
	iowrite32(0x0, intc_prio_base + 0x4);
	iowrite32(0x0, intc_prio_base + 0x8);
	iowrite32(0x0, intc_prio_base + 0xc);
	iowrite8(0xff, intc_msk_base + 0x0);
	iowrite8(0xff, intc_msk_base + 0x4);
	iowrite8(0xff, intc_msk_base + 0x8);
	iowrite8(0xff, intc_msk_base + 0xc);

	iounmap(intc_prio_base);
	iounmap(intc_msk_base);
	iounmap(pfc_inta_ctrl);
}

static void __init r8a7740_generic_init(void)
{
	r8a7740_meram_workaround();
}

static const char *const r8a7740_boards_compat_dt[] __initconst = {
	"renesas,r8a7740",
	NULL
};

DT_MACHINE_START(R8A7740_DT, "Generic R8A7740 (Flattened Device Tree)")
	.l2c_aux_val	= 0,
	.l2c_aux_mask	= ~0,
	.init_early	= shmobile_init_delay,
	.init_irq	= r8a7740_init_irq_of,
	.init_machine	= r8a7740_generic_init,
	.init_late	= shmobile_init_late,
	.dt_compat	= r8a7740_boards_compat_dt,
MACHINE_END
