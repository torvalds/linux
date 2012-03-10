/*
 * arch/arch/mach-ixp4xx/wg302v2-pci.c
 *
 * PCI setup routines for the Netgear WG302 v2 and WAG302 v2
 *
 * Copyright (C) 2007 Imre Kaloz <kaloz@openwrt.org>
 *
 * based on coyote-pci.c:
 *	Copyright (C) 2002 Jungo Software Technologies.
 *	Copyright (C) 2003 MontaVista Software, Inc.
 *
 * Maintainer: Imre Kaloz <kaloz@openwrt.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/irq.h>

#include <asm/mach-types.h>
#include <mach/hardware.h>

#include <asm/mach/pci.h>

void __init wg302v2_pci_preinit(void)
{
	irq_set_irq_type(IRQ_IXP4XX_GPIO8, IRQ_TYPE_LEVEL_LOW);
	irq_set_irq_type(IRQ_IXP4XX_GPIO9, IRQ_TYPE_LEVEL_LOW);

	ixp4xx_pci_preinit();
}

static int __init wg302v2_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	if (slot == 1)
		return IRQ_IXP4XX_GPIO8;
	else if (slot == 2)
		return IRQ_IXP4XX_GPIO9;
	else return -1;
}

struct hw_pci wg302v2_pci __initdata = {
	.nr_controllers = 1,
	.ops = &ixp4xx_ops,
	.preinit =        wg302v2_pci_preinit,
	.setup =          ixp4xx_setup,
	.map_irq =        wg302v2_map_irq,
};

int __init wg302v2_pci_init(void)
{
	if (machine_is_wg302v2())
		pci_common_init(&wg302v2_pci);
	return 0;
}

subsys_initcall(wg302v2_pci_init);
