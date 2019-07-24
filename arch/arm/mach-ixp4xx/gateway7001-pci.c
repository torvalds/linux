// SPDX-License-Identifier: GPL-2.0-only
/*
 * arch/arch/mach-ixp4xx/gateway7001-pci.c
 *
 * PCI setup routines for Gateway 7001
 *
 * Copyright (C) 2007 Imre Kaloz <kaloz@openwrt.org>
 *
 * based on coyote-pci.c:
 *	Copyright (C) 2002 Jungo Software Technologies.
 *	Copyright (C) 2003 MontaVista Softwrae, Inc.
 *
 * Maintainer: Imre Kaloz <kaloz@openwrt.org>
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/irq.h>

#include <asm/mach-types.h>
#include <mach/hardware.h>

#include <asm/mach/pci.h>

#include "irqs.h"

void __init gateway7001_pci_preinit(void)
{
	irq_set_irq_type(IRQ_IXP4XX_GPIO10, IRQ_TYPE_LEVEL_LOW);
	irq_set_irq_type(IRQ_IXP4XX_GPIO11, IRQ_TYPE_LEVEL_LOW);

	ixp4xx_pci_preinit();
}

static int __init gateway7001_map_irq(const struct pci_dev *dev, u8 slot,
	u8 pin)
{
	if (slot == 1)
		return IRQ_IXP4XX_GPIO11;
	else if (slot == 2)
		return IRQ_IXP4XX_GPIO10;
	else return -1;
}

struct hw_pci gateway7001_pci __initdata = {
	.nr_controllers = 1,
	.ops		= &ixp4xx_ops,
	.preinit =        gateway7001_pci_preinit,
	.setup =          ixp4xx_setup,
	.map_irq =        gateway7001_map_irq,
};

int __init gateway7001_pci_init(void)
{
	if (machine_is_gateway7001())
		pci_common_init(&gateway7001_pci);
	return 0;
}

subsys_initcall(gateway7001_pci_init);
