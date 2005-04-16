/*
 * arch/arch/mach-ixp4xx/ixdpg425-pci.c
 *
 * PCI setup routines for Intel IXDPG425 Platform
 *
 * Copyright (C) 2004 MontaVista Softwrae, Inc.
 *
 * Maintainer: Deepak Saxena <dsaxena@plexity.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>

#include <asm/mach-types.h>
#include <asm/hardware.h>
#include <asm/irq.h>

#include <asm/mach/pci.h>

extern void ixp4xx_pci_preinit(void);
extern int ixp4xx_setup(int nr, struct pci_sys_data *sys);
extern struct pci_bus *ixp4xx_scan_bus(int nr, struct pci_sys_data *sys);

void __init ixdpg425_pci_preinit(void)
{
	gpio_line_config(6, IXP4XX_GPIO_IN | IXP4XX_GPIO_ACTIVE_LOW);
	gpio_line_config(7, IXP4XX_GPIO_IN | IXP4XX_GPIO_ACTIVE_LOW);

	gpio_line_isr_clear(6);
	gpio_line_isr_clear(7);

	ixp4xx_pci_preinit();
}

static int __init ixdpg425_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	if (slot == 12 || slot == 13)
		return IRQ_IXP4XX_GPIO7;
	else if (slot == 14)
		return IRQ_IXP4XX_GPIO6;
	else return -1;
}

struct hw_pci ixdpg425_pci __initdata = {
	.nr_controllers = 1,
	.preinit =        ixdpg425_pci_preinit,
	.swizzle =        pci_std_swizzle,
	.setup =          ixp4xx_setup,
	.scan =           ixp4xx_scan_bus,
	.map_irq =        ixdpg425_map_irq,
};

int __init ixdpg425_pci_init(void)
{
	if (machine_is_ixdpg425())
		pci_common_init(&ixdpg425_pci);
	return 0;
}

subsys_initcall(ixdpg425_pci_init);
