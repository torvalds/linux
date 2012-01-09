/*
 * arch/arm/mach-iop33x/iq80332.c
 *
 * Board support code for the Intel IQ80332 platform.
 *
 * Author: Dave Jiang <dave.jiang@intel.com>
 * Copyright (C) 2004 Intel Corp.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include <linux/mm.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/string.h>
#include <linux/serial_core.h>
#include <linux/serial_8250.h>
#include <linux/mtd/physmap.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <mach/hardware.h>
#include <asm/irq.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/pci.h>
#include <asm/mach/time.h>
#include <asm/mach-types.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <mach/time.h>

/*
 * IQ80332 timer tick configuration.
 */
static void __init iq80332_timer_init(void)
{
	/* D-Step parts and the iop333 run at a higher internal bus frequency */
	if (*IOP3XX_ATURID >= 0xa || *IOP3XX_ATUDID == 0x374)
		iop_init_time(333000000);
	else
		iop_init_time(266000000);
}

static struct sys_timer iq80332_timer = {
	.init		= iq80332_timer_init,
};


/*
 * IQ80332 PCI.
 */
static int __init
iq80332_pci_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	int irq;

	if (slot == 4 && pin == 1) {
		/* PCI-X Slot INTA */
		irq = IRQ_IOP33X_XINT0;
	} else if (slot == 4 && pin == 2) {
		/* PCI-X Slot INTB */
		irq = IRQ_IOP33X_XINT1;
	} else if (slot == 4 && pin == 3) {
		/* PCI-X Slot INTC */
		irq = IRQ_IOP33X_XINT2;
	} else if (slot == 4 && pin == 4) {
		/* PCI-X Slot INTD */
		irq = IRQ_IOP33X_XINT3;
	} else if (slot == 6) {
		/* GigE */
		irq = IRQ_IOP33X_XINT2;
	} else {
		printk(KERN_ERR "iq80332_pci_map_irq() called for unknown "
			"device PCI:%d:%d:%d\n", dev->bus->number,
			PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn));
		irq = -1;
	}

	return irq;
}

static struct hw_pci iq80332_pci __initdata = {
	.swizzle	= pci_std_swizzle,
	.nr_controllers = 1,
	.setup		= iop3xx_pci_setup,
	.preinit	= iop3xx_pci_preinit_cond,
	.scan		= iop3xx_pci_scan_bus,
	.map_irq	= iq80332_pci_map_irq,
};

static int __init iq80332_pci_init(void)
{
	if ((iop3xx_get_init_atu() == IOP3XX_INIT_ATU_ENABLE) &&
		machine_is_iq80332())
		pci_common_init(&iq80332_pci);

	return 0;
}

subsys_initcall(iq80332_pci_init);


/*
 * IQ80332 machine initialisation.
 */
static struct physmap_flash_data iq80332_flash_data = {
	.width		= 1,
};

static struct resource iq80332_flash_resource = {
	.start		= 0xc0000000,
	.end		= 0xc07fffff,
	.flags		= IORESOURCE_MEM,
};

static struct platform_device iq80332_flash_device = {
	.name		= "physmap-flash",
	.id		= 0,
	.dev		= {
		.platform_data	= &iq80332_flash_data,
	},
	.num_resources	= 1,
	.resource	= &iq80332_flash_resource,
};

static void __init iq80332_init_machine(void)
{
	platform_device_register(&iop3xx_i2c0_device);
	platform_device_register(&iop3xx_i2c1_device);
	platform_device_register(&iop33x_uart0_device);
	platform_device_register(&iop33x_uart1_device);
	platform_device_register(&iq80332_flash_device);
	platform_device_register(&iop3xx_dma_0_channel);
	platform_device_register(&iop3xx_dma_1_channel);
	platform_device_register(&iop3xx_aau_channel);
}

MACHINE_START(IQ80332, "Intel IQ80332")
	/* Maintainer: Intel Corp. */
	.atag_offset	= 0x100,
	.map_io		= iop3xx_map_io,
	.init_irq	= iop33x_init_irq,
	.timer		= &iq80332_timer,
	.init_machine	= iq80332_init_machine,
MACHINE_END
