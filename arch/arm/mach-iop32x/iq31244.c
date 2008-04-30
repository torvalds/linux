/*
 * arch/arm/mach-iop32x/iq31244.c
 *
 * Board support code for the Intel EP80219 and IQ31244 platforms.
 *
 * Author: Rory Bolt <rorybolt@pacbell.net>
 * Copyright (C) 2002 Rory Bolt
 * Copyright 2003 (c) MontaVista, Software, Inc.
 * Copyright (C) 2004 Intel Corp.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include <linux/mm.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/pm.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/serial_core.h>
#include <linux/serial_8250.h>
#include <linux/mtd/physmap.h>
#include <linux/platform_device.h>
#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/pci.h>
#include <asm/mach/time.h>
#include <asm/mach-types.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/arch/time.h>

/*
 * Until March of 2007 iq31244 platforms and ep80219 platforms shared the
 * same machine id, and the processor type was used to select board type.
 * However this assumption breaks for an iq80219 board which is an iop219
 * processor on an iq31244 board.  The force_ep80219 flag has been added
 * for old boot loaders using the iq31244 machine id for an ep80219 platform.
 */
static int force_ep80219;

static int is_80219(void)
{
	extern int processor_id;
	return !!((processor_id & 0xffffffe0) == 0x69052e20);
}

static int is_ep80219(void)
{
	if (machine_is_ep80219() || force_ep80219)
		return 1;
	else
		return 0;
}


/*
 * EP80219/IQ31244 timer tick configuration.
 */
static void __init iq31244_timer_init(void)
{
	if (is_ep80219()) {
		/* 33.333 MHz crystal.  */
		iop_init_time(200000000);
	} else {
		/* 33.000 MHz crystal.  */
		iop_init_time(198000000);
	}
}

static struct sys_timer iq31244_timer = {
	.init		= iq31244_timer_init,
	.offset		= iop_gettimeoffset,
};


/*
 * IQ31244 I/O.
 */
static struct map_desc iq31244_io_desc[] __initdata = {
	{	/* on-board devices */
		.virtual	= IQ31244_UART,
		.pfn		= __phys_to_pfn(IQ31244_UART),
		.length		= 0x00100000,
		.type		= MT_DEVICE,
	},
};

void __init iq31244_map_io(void)
{
	iop3xx_map_io();
	iotable_init(iq31244_io_desc, ARRAY_SIZE(iq31244_io_desc));
}


/*
 * EP80219/IQ31244 PCI.
 */
static int __init
ep80219_pci_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	int irq;

	if (slot == 0) {
		/* CFlash */
		irq = IRQ_IOP32X_XINT1;
	} else if (slot == 1) {
		/* 82551 Pro 100 */
		irq = IRQ_IOP32X_XINT0;
	} else if (slot == 2) {
		/* PCI-X Slot */
		irq = IRQ_IOP32X_XINT3;
	} else if (slot == 3) {
		/* SATA */
		irq = IRQ_IOP32X_XINT2;
	} else {
		printk(KERN_ERR "ep80219_pci_map_irq() called for unknown "
			"device PCI:%d:%d:%d\n", dev->bus->number,
			PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn));
		irq = -1;
	}

	return irq;
}

static struct hw_pci ep80219_pci __initdata = {
	.swizzle	= pci_std_swizzle,
	.nr_controllers = 1,
	.setup		= iop3xx_pci_setup,
	.preinit	= iop3xx_pci_preinit,
	.scan		= iop3xx_pci_scan_bus,
	.map_irq	= ep80219_pci_map_irq,
};

static int __init
iq31244_pci_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	int irq;

	if (slot == 0) {
		/* CFlash */
		irq = IRQ_IOP32X_XINT1;
	} else if (slot == 1) {
		/* SATA */
		irq = IRQ_IOP32X_XINT2;
	} else if (slot == 2) {
		/* PCI-X Slot */
		irq = IRQ_IOP32X_XINT3;
	} else if (slot == 3) {
		/* 82546 GigE */
		irq = IRQ_IOP32X_XINT0;
	} else {
		printk(KERN_ERR "iq31244_pci_map_irq called for unknown "
			"device PCI:%d:%d:%d\n", dev->bus->number,
			PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn));
		irq = -1;
	}

	return irq;
}

static struct hw_pci iq31244_pci __initdata = {
	.swizzle	= pci_std_swizzle,
	.nr_controllers = 1,
	.setup		= iop3xx_pci_setup,
	.preinit	= iop3xx_pci_preinit,
	.scan		= iop3xx_pci_scan_bus,
	.map_irq	= iq31244_pci_map_irq,
};

static int __init iq31244_pci_init(void)
{
	if (is_ep80219())
		pci_common_init(&ep80219_pci);
	else if (machine_is_iq31244()) {
		if (is_80219()) {
			printk("note: iq31244 board type has been selected\n");
			printk("note: to select ep80219 operation:\n");
			printk("\t1/ specify \"force_ep80219\" on the kernel"
				" command line\n");
			printk("\t2/ update boot loader to pass"
				" the ep80219 id: %d\n", MACH_TYPE_EP80219);
		}
		pci_common_init(&iq31244_pci);
	}

	return 0;
}

subsys_initcall(iq31244_pci_init);


/*
 * IQ31244 machine initialisation.
 */
static struct physmap_flash_data iq31244_flash_data = {
	.width		= 2,
};

static struct resource iq31244_flash_resource = {
	.start		= 0xf0000000,
	.end		= 0xf07fffff,
	.flags		= IORESOURCE_MEM,
};

static struct platform_device iq31244_flash_device = {
	.name		= "physmap-flash",
	.id		= 0,
	.dev		= {
		.platform_data	= &iq31244_flash_data,
	},
	.num_resources	= 1,
	.resource	= &iq31244_flash_resource,
};

static struct plat_serial8250_port iq31244_serial_port[] = {
	{
		.mapbase	= IQ31244_UART,
		.membase	= (char *)IQ31244_UART,
		.irq		= IRQ_IOP32X_XINT1,
		.flags		= UPF_SKIP_TEST,
		.iotype		= UPIO_MEM,
		.regshift	= 0,
		.uartclk	= 1843200,
	},
	{ },
};

static struct resource iq31244_uart_resource = {
	.start		= IQ31244_UART,
	.end		= IQ31244_UART + 7,
	.flags		= IORESOURCE_MEM,
};

static struct platform_device iq31244_serial_device = {
	.name		= "serial8250",
	.id		= PLAT8250_DEV_PLATFORM,
	.dev		= {
		.platform_data		= iq31244_serial_port,
	},
	.num_resources	= 1,
	.resource	= &iq31244_uart_resource,
};

/*
 * This function will send a SHUTDOWN_COMPLETE message to the PIC
 * controller over I2C.  We are not using the i2c subsystem since
 * we are going to power off and it may be removed
 */
void ep80219_power_off(void)
{
	/*
	 * Send the Address byte w/ the start condition
	 */
	*IOP3XX_IDBR1 = 0x60;
	*IOP3XX_ICR1 = 0xE9;
	mdelay(1);

	/*
	 * Send the START_MSG byte w/ no start or stop condition
	 */
	*IOP3XX_IDBR1 = 0x0F;
	*IOP3XX_ICR1 = 0xE8;
	mdelay(1);

	/*
	 * Send the SHUTDOWN_COMPLETE Message ID byte w/ no start or
	 * stop condition
	 */
	*IOP3XX_IDBR1 = 0x03;
	*IOP3XX_ICR1 = 0xE8;
	mdelay(1);

	/*
	 * Send an ignored byte w/ stop condition
	 */
	*IOP3XX_IDBR1 = 0x00;
	*IOP3XX_ICR1 = 0xEA;

	while (1)
		;
}

static void __init iq31244_init_machine(void)
{
	platform_device_register(&iop3xx_i2c0_device);
	platform_device_register(&iop3xx_i2c1_device);
	platform_device_register(&iq31244_flash_device);
	platform_device_register(&iq31244_serial_device);
	platform_device_register(&iop3xx_dma_0_channel);
	platform_device_register(&iop3xx_dma_1_channel);

	if (is_ep80219())
		pm_power_off = ep80219_power_off;

	if (!is_80219())
		platform_device_register(&iop3xx_aau_channel);
}

static int __init force_ep80219_setup(char *str)
{
	force_ep80219 = 1;
	return 1;
}

__setup("force_ep80219", force_ep80219_setup);

MACHINE_START(IQ31244, "Intel IQ31244")
	/* Maintainer: Intel Corp. */
	.phys_io	= IQ31244_UART,
	.io_pg_offst	= ((IQ31244_UART) >> 18) & 0xfffc,
	.boot_params	= 0xa0000100,
	.map_io		= iq31244_map_io,
	.init_irq	= iop32x_init_irq,
	.timer		= &iq31244_timer,
	.init_machine	= iq31244_init_machine,
MACHINE_END

/* There should have been an ep80219 machine identifier from the beginning.
 * Boot roms older than March 2007 do not know the ep80219 machine id.  Pass
 * "force_ep80219" on the kernel command line, otherwise iq31244 operation
 * will be selected.
 */
MACHINE_START(EP80219, "Intel EP80219")
	/* Maintainer: Intel Corp. */
	.phys_io	= IQ31244_UART,
	.io_pg_offst	= ((IQ31244_UART) >> 18) & 0xfffc,
	.boot_params	= 0xa0000100,
	.map_io		= iq31244_map_io,
	.init_irq	= iop32x_init_irq,
	.timer		= &iq31244_timer,
	.init_machine	= iq31244_init_machine,
MACHINE_END
