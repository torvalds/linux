/*
 * arch/arm/mach-ixp2000/enp2611.c
 *
 * Radisys ENP-2611 support.
 *
 * Created 2004 by Lennert Buytenhek from the ixdp2x01 code.  The
 * original version carries the following notices:
 *
 * Original Author: Andrzej Mialkowski <andrzej.mialkowski@intel.com>
 * Maintainer: Deepak Saxena <dsaxena@plexity.net>
 *
 * Copyright (C) 2002-2003 Intel Corp.
 * Copyright (C) 2003-2004 MontaVista Software, Inc.
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/bitops.h>
#include <linux/pci.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/serial.h>
#include <linux/tty.h>
#include <linux/serial_core.h>
#include <linux/platform_device.h>
#include <linux/io.h>

#include <asm/irq.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/system.h>
#include <mach/hardware.h>
#include <asm/mach-types.h>

#include <asm/mach/pci.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>
#include <asm/mach/time.h>
#include <asm/mach/arch.h>
#include <asm/mach/flash.h>

/*************************************************************************
 * ENP-2611 timer tick configuration
 *************************************************************************/
static void __init enp2611_timer_init(void)
{
	ixp2000_init_time(50 * 1000 * 1000);
}

static struct sys_timer enp2611_timer = {
	.init		= enp2611_timer_init,
	.offset		= ixp2000_gettimeoffset,
};


/*************************************************************************
 * ENP-2611 I/O
 *************************************************************************/
static struct map_desc enp2611_io_desc[] __initdata = {
	{
		.virtual	= ENP2611_CALEB_VIRT_BASE,
		.pfn		= __phys_to_pfn(ENP2611_CALEB_PHYS_BASE),
		.length		= ENP2611_CALEB_SIZE,
		.type		= MT_DEVICE,
	}, {
		.virtual	= ENP2611_PM3386_0_VIRT_BASE,
		.pfn		= __phys_to_pfn(ENP2611_PM3386_0_PHYS_BASE),
		.length		= ENP2611_PM3386_0_SIZE,
		.type		= MT_DEVICE,
	}, {
		.virtual	= ENP2611_PM3386_1_VIRT_BASE,
		.pfn		= __phys_to_pfn(ENP2611_PM3386_1_PHYS_BASE),
		.length		= ENP2611_PM3386_1_SIZE,
		.type		= MT_DEVICE,
	}
};

void __init enp2611_map_io(void)
{
	ixp2000_map_io();
	iotable_init(enp2611_io_desc, ARRAY_SIZE(enp2611_io_desc));
}


/*************************************************************************
 * ENP-2611 PCI
 *************************************************************************/
static int enp2611_pci_setup(int nr, struct pci_sys_data *sys)
{
	sys->mem_offset = 0xe0000000;
	ixp2000_pci_setup(nr, sys);
	return 1;
}

static void __init enp2611_pci_preinit(void)
{
	ixp2000_reg_write(IXP2000_PCI_ADDR_EXT, 0x00100000);
	ixp2000_pci_preinit();
	pcibios_setup("firmware");
}

static inline int enp2611_pci_valid_device(struct pci_bus *bus,
						unsigned int devfn)
{
	/* The 82559 ethernet controller appears at both PCI:1:0:0 and
	 * PCI:1:2:0, so let's pretend the second one isn't there.
	 */
	if (bus->number == 0x01 && devfn == 0x10)
		return 0;

	return 1;
}

static int enp2611_pci_read_config(struct pci_bus *bus, unsigned int devfn,
					int where, int size, u32 *value)
{
	if (enp2611_pci_valid_device(bus, devfn))
		return ixp2000_pci_read_config(bus, devfn, where, size, value);

	return PCIBIOS_DEVICE_NOT_FOUND;
}

static int enp2611_pci_write_config(struct pci_bus *bus, unsigned int devfn,
					int where, int size, u32 value)
{
	if (enp2611_pci_valid_device(bus, devfn))
		return ixp2000_pci_write_config(bus, devfn, where, size, value);

	return PCIBIOS_DEVICE_NOT_FOUND;
}

static struct pci_ops enp2611_pci_ops = {
	.read   = enp2611_pci_read_config,
	.write  = enp2611_pci_write_config
};

static struct pci_bus * __init enp2611_pci_scan_bus(int nr,
						struct pci_sys_data *sys)
{
	return pci_scan_bus(sys->busnr, &enp2611_pci_ops, sys);
}

static int __init enp2611_pci_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	int irq;

	if (dev->bus->number == 0 && PCI_SLOT(dev->devfn) == 0) {
		/* IXP2400. */
		irq = IRQ_IXP2000_PCIA;
	} else if (dev->bus->number == 0 && PCI_SLOT(dev->devfn) == 1) {
		/* 21555 non-transparent bridge.  */
		irq = IRQ_IXP2000_PCIB;
	} else if (dev->bus->number == 0 && PCI_SLOT(dev->devfn) == 4) {
		/* PCI2050B transparent bridge.  */
		irq = -1;
	} else if (dev->bus->number == 1 && PCI_SLOT(dev->devfn) == 0) {
		/* 82559 ethernet.  */
		irq = IRQ_IXP2000_PCIA;
	} else if (dev->bus->number == 1 && PCI_SLOT(dev->devfn) == 1) {
		/* SPI-3 option board.  */
		irq = IRQ_IXP2000_PCIB;
	} else {
		printk(KERN_ERR "enp2611_pci_map_irq() called for unknown "
				"device PCI:%d:%d:%d\n", dev->bus->number,
				PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn));
		irq = -1;
	}

	return irq;
}

struct hw_pci enp2611_pci __initdata = {
	.nr_controllers	= 1,
	.setup		= enp2611_pci_setup,
	.preinit	= enp2611_pci_preinit,
	.scan		= enp2611_pci_scan_bus,
	.map_irq	= enp2611_pci_map_irq,
};

int __init enp2611_pci_init(void)
{
	if (machine_is_enp2611())
		pci_common_init(&enp2611_pci);

	return 0;
}

subsys_initcall(enp2611_pci_init);


/*************************************************************************
 * ENP-2611 Machine Initialization
 *************************************************************************/
static struct flash_platform_data enp2611_flash_platform_data = {
	.map_name	= "cfi_probe",
	.width		= 1,
};

static struct ixp2000_flash_data enp2611_flash_data = {
	.platform_data	= &enp2611_flash_platform_data,
	.nr_banks	= 1
};

static struct resource enp2611_flash_resource = {
	.start		= 0xc4000000,
	.end		= 0xc4000000 + 0x00ffffff,
	.flags		= IORESOURCE_MEM,
};

static struct platform_device enp2611_flash = {
	.name		= "IXP2000-Flash",
	.id		= 0,
	.dev		= {
		.platform_data = &enp2611_flash_data,
	},
	.num_resources	= 1,
	.resource	= &enp2611_flash_resource,
};

static struct ixp2000_i2c_pins enp2611_i2c_gpio_pins = {
	.sda_pin	= ENP2611_GPIO_SDA,
	.scl_pin	= ENP2611_GPIO_SCL,
};

static struct platform_device enp2611_i2c_controller = {
	.name		= "IXP2000-I2C",
	.id		= 0,
	.dev		= {
		.platform_data = &enp2611_i2c_gpio_pins
	},
	.num_resources	= 0
};

static struct platform_device *enp2611_devices[] __initdata = {
	&enp2611_flash,
	&enp2611_i2c_controller
};

static void __init enp2611_init_machine(void)
{
	platform_add_devices(enp2611_devices, ARRAY_SIZE(enp2611_devices));
	ixp2000_uart_init();
}


MACHINE_START(ENP2611, "Radisys ENP-2611 PCI network processor board")
	/* Maintainer: Lennert Buytenhek <buytenh@wantstofly.org> */
	.phys_io	= IXP2000_UART_PHYS_BASE,
	.io_pg_offst	= ((IXP2000_UART_VIRT_BASE) >> 18) & 0xfffc,
	.boot_params	= 0x00000100,
	.map_io		= enp2611_map_io,
	.init_irq	= ixp2000_init_irq,
	.timer		= &enp2611_timer,
	.init_machine	= enp2611_init_machine,
MACHINE_END


