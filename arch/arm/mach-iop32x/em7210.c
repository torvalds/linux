/*
 * arch/arm/mach-iop32x/em7210.c
 *
 * Board support code for the Lanner EM7210 platforms.
 *
 * Based on arch/arm/mach-iop32x/iq31244.c file.
 *
 * Copyright (C) 2007 Arnaud Patard <arnaud.patard@rtp-net.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/mm.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/pm.h>
#include <linux/serial_core.h>
#include <linux/serial_8250.h>
#include <linux/mtd/physmap.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <mach/hardware.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/pci.h>
#include <asm/mach/time.h>
#include <asm/mach-types.h>
#include <mach/time.h>

static void __init em7210_timer_init(void)
{
	/* http://www.kwaak.net/fotos/fotos-nas/slide_24.html */
	/* 33.333 MHz crystal.                                */
	iop_init_time(200000000);
}

static struct sys_timer em7210_timer = {
	.init		= em7210_timer_init,
};

/*
 * EM7210 RTC
 */
static struct i2c_board_info __initdata em7210_i2c_devices[] = {
	{
		I2C_BOARD_INFO("rs5c372a", 0x32),
	},
};

/*
 * EM7210 I/O
 */
static struct map_desc em7210_io_desc[] __initdata = {
	{	/* on-board devices */
		.virtual	= IQ31244_UART,
		.pfn		= __phys_to_pfn(IQ31244_UART),
		.length		= 0x00100000,
		.type		= MT_DEVICE,
	},
};

void __init em7210_map_io(void)
{
	iop3xx_map_io();
	iotable_init(em7210_io_desc, ARRAY_SIZE(em7210_io_desc));
}


/*
 * EM7210 PCI
 */
#define INTA	IRQ_IOP32X_XINT0
#define INTB	IRQ_IOP32X_XINT1
#define INTC	IRQ_IOP32X_XINT2
#define INTD	IRQ_IOP32X_XINT3

static int __init
em7210_pci_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	static int pci_irq_table[][4] = {
		/*
		 * PCI IDSEL/INTPIN->INTLINE
		 * A       B       C       D
		 */
		{INTB, INTB, INTB, INTB}, /* console / uart */
		{INTA, INTA, INTA, INTA}, /* 1st 82541      */
		{INTD, INTD, INTD, INTD}, /* 2nd 82541      */
		{INTC, INTC, INTC, INTC}, /* GD31244        */
		{INTD, INTA, INTA, INTA}, /* mini-PCI       */
		{INTD, INTC, INTA, INTA}, /* NEC USB        */
	};

	if (pin < 1 || pin > 4)
		return -1;

	return pci_irq_table[slot % 6][pin - 1];
}

static struct hw_pci em7210_pci __initdata = {
	.swizzle	= pci_std_swizzle,
	.nr_controllers = 1,
	.setup		= iop3xx_pci_setup,
	.preinit	= iop3xx_pci_preinit,
	.scan		= iop3xx_pci_scan_bus,
	.map_irq	= em7210_pci_map_irq,
};

static int __init em7210_pci_init(void)
{
	if (machine_is_em7210())
		pci_common_init(&em7210_pci);

	return 0;
}

subsys_initcall(em7210_pci_init);


/*
 * EM7210 Flash
 */
static struct physmap_flash_data em7210_flash_data = {
	.width		= 2,
};

static struct resource em7210_flash_resource = {
	.start		= 0xf0000000,
	.end		= 0xf1ffffff,
	.flags		= IORESOURCE_MEM,
};

static struct platform_device em7210_flash_device = {
	.name		= "physmap-flash",
	.id		= 0,
	.dev		= {
		.platform_data	= &em7210_flash_data,
	},
	.num_resources	= 1,
	.resource	= &em7210_flash_resource,
};


/*
 * EM7210 UART
 * The physical address of the serial port is 0xfe800000,
 * so it can be used for physical and virtual address.
 */
static struct plat_serial8250_port em7210_serial_port[] = {
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

static struct resource em7210_uart_resource = {
	.start		= IQ31244_UART,
	.end		= IQ31244_UART + 7,
	.flags		= IORESOURCE_MEM,
};

static struct platform_device em7210_serial_device = {
	.name		= "serial8250",
	.id		= PLAT8250_DEV_PLATFORM,
	.dev		= {
		.platform_data		= em7210_serial_port,
	},
	.num_resources	= 1,
	.resource	= &em7210_uart_resource,
};

void em7210_power_off(void)
{
	*IOP3XX_GPOE &= 0xfe;
	*IOP3XX_GPOD |= 0x01;
}

static void __init em7210_init_machine(void)
{
	platform_device_register(&em7210_serial_device);
	platform_device_register(&iop3xx_i2c0_device);
	platform_device_register(&iop3xx_i2c1_device);
	platform_device_register(&em7210_flash_device);
	platform_device_register(&iop3xx_dma_0_channel);
	platform_device_register(&iop3xx_dma_1_channel);

	i2c_register_board_info(0, em7210_i2c_devices,
		ARRAY_SIZE(em7210_i2c_devices));


	pm_power_off = em7210_power_off;
}

MACHINE_START(EM7210, "Lanner EM7210")
	.atag_offset	= 0x100,
	.map_io		= em7210_map_io,
	.init_irq	= iop32x_init_irq,
	.timer		= &em7210_timer,
	.init_machine	= em7210_init_machine,
MACHINE_END
