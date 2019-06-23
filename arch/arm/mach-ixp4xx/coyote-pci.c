// SPDX-License-Identifier: GPL-2.0-only
/*
 * arch/arm/mach-ixp4xx/coyote-pci.c
 *
 * PCI setup routines for ADI Engineering Coyote platform
 *
 * Copyright (C) 2002 Jungo Software Technologies.
 * Copyright (C) 2003 MontaVista Softwrae, Inc.
 *
 * Maintainer: Deepak Saxena <dsaxena@mvista.com>
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <asm/mach-types.h>
#include <mach/hardware.h>
#include <asm/irq.h>
#include <asm/mach/pci.h>

#include "irqs.h"

#define SLOT0_DEVID	14
#define SLOT1_DEVID	15

/* PCI controller GPIO to IRQ pin mappings */
#define SLOT0_INTA	6
#define SLOT1_INTA	11

void __init coyote_pci_preinit(void)
{
	irq_set_irq_type(IXP4XX_GPIO_IRQ(SLOT0_INTA), IRQ_TYPE_LEVEL_LOW);
	irq_set_irq_type(IXP4XX_GPIO_IRQ(SLOT1_INTA), IRQ_TYPE_LEVEL_LOW);
	ixp4xx_pci_preinit();
}

static int __init coyote_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	if (slot == SLOT0_DEVID)
		return IXP4XX_GPIO_IRQ(SLOT0_INTA);
	else if (slot == SLOT1_DEVID)
		return IXP4XX_GPIO_IRQ(SLOT1_INTA);
	else return -1;
}

struct hw_pci coyote_pci __initdata = {
	.nr_controllers = 1,
	.ops		= &ixp4xx_ops,
	.preinit =        coyote_pci_preinit,
	.setup =          ixp4xx_setup,
	.map_irq =        coyote_map_irq,
};

int __init coyote_pci_init(void)
{
	if (machine_is_adi_coyote())
		pci_common_init(&coyote_pci);
	return 0;
}

subsys_initcall(coyote_pci_init);
