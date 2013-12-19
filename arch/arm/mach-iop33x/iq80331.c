/*
 * arch/arm/mach-iop33x/iq80331.c
 *
 * Board support code for the Intel IQ80331 platform.
 *
 * Author: Dave Jiang <dave.jiang@intel.com>
 * Copyright (C) 2003 Intel Corp.
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
 * IQ80331 timer tick configuration.
 */
static void __init iq80331_timer_init(void)
{
	/* D-Step parts run at a higher internal bus frequency */
	if (*IOP3XX_ATURID >= 0xa)
		iop_init_time(333000000);
	else
		iop_init_time(266000000);
}


/*
 * IQ80331 PCI.
 */
static int __init
iq80331_pci_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	int irq;

	if (slot == 1 && pin == 1) {
		/* PCI-X Slot INTA */
		irq = IRQ_IOP33X_XINT1;
	} else if (slot == 1 && pin == 2) {
		/* PCI-X Slot INTB */
		irq = IRQ_IOP33X_XINT2;
	} else if (slot == 1 && pin == 3) {
		/* PCI-X Slot INTC */
		irq = IRQ_IOP33X_XINT3;
	} else if (slot == 1 && pin == 4) {
		/* PCI-X Slot INTD */
		irq = IRQ_IOP33X_XINT0;
	} else if (slot == 2) {
		/* GigE */
		irq = IRQ_IOP33X_XINT2;
	} else {
		printk(KERN_ERR "iq80331_pci_map_irq() called for unknown "
			"device PCI:%d:%d:%d\n", dev->bus->number,
			PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn));
		irq = -1;
	}

	return irq;
}

static struct hw_pci iq80331_pci __initdata = {
	.nr_controllers = 1,
	.ops		= &iop3xx_ops,
	.setup		= iop3xx_pci_setup,
	.preinit	= iop3xx_pci_preinit_cond,
	.map_irq	= iq80331_pci_map_irq,
};

static int __init iq80331_pci_init(void)
{
	if ((iop3xx_get_init_atu() == IOP3XX_INIT_ATU_ENABLE) &&
		machine_is_iq80331())
		pci_common_init(&iq80331_pci);

	return 0;
}

subsys_initcall(iq80331_pci_init);


/*
 * IQ80331 machine initialisation.
 */
static struct physmap_flash_data iq80331_flash_data = {
	.width		= 1,
};

static struct resource iq80331_flash_resource = {
	.start		= 0xc0000000,
	.end		= 0xc07fffff,
	.flags		= IORESOURCE_MEM,
};

static struct platform_device iq80331_flash_device = {
	.name		= "physmap-flash",
	.id		= 0,
	.dev		= {
		.platform_data	= &iq80331_flash_data,
	},
	.num_resources	= 1,
	.resource	= &iq80331_flash_resource,
};

static struct resource iq80331_gpio_res[] = {
	DEFINE_RES_MEM((IOP3XX_PERIPHERAL_PHYS_BASE + 0x1780), 0x10),
};

static void __init iq80331_init_machine(void)
{
	platform_device_register_simple("gpio-iop", 0,
					iq80331_gpio_res,
					ARRAY_SIZE(iq80331_gpio_res));
	platform_device_register(&iop3xx_i2c0_device);
	platform_device_register(&iop3xx_i2c1_device);
	platform_device_register(&iop33x_uart0_device);
	platform_device_register(&iop33x_uart1_device);
	platform_device_register(&iq80331_flash_device);
	platform_device_register(&iop3xx_dma_0_channel);
	platform_device_register(&iop3xx_dma_1_channel);
	platform_device_register(&iop3xx_aau_channel);
}

MACHINE_START(IQ80331, "Intel IQ80331")
	/* Maintainer: Intel Corp. */
	.atag_offset	= 0x100,
	.map_io		= iop3xx_map_io,
	.init_irq	= iop33x_init_irq,
	.init_time	= iq80331_timer_init,
	.init_machine	= iq80331_init_machine,
	.restart	= iop3xx_restart,
MACHINE_END
