/*
 * arch/arm/mach-iop32x/glantank.c
 *
 * Board support code for the GLAN Tank.
 *
 * Copyright (C) 2006, 2007 Martin Michlmayr <tbm@cyrius.com>
 * Copyright (C) 2006 Lennert Buytenhek <buytenh@wantstofly.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include <linux/mm.h>
#include <linux/init.h>
#include <linux/f75375s.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/pm.h>
#include <linux/string.h>
#include <linux/serial_core.h>
#include <linux/serial_8250.h>
#include <linux/mtd/physmap.h>
#include <linux/i2c.h>
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
#include <mach/time.h>

/*
 * GLAN Tank timer tick configuration.
 */
static void __init glantank_timer_init(void)
{
	/* 33.333 MHz crystal.  */
	iop_init_time(200000000);
}

static struct sys_timer glantank_timer = {
	.init		= glantank_timer_init,
};


/*
 * GLAN Tank I/O.
 */
static struct map_desc glantank_io_desc[] __initdata = {
	{	/* on-board devices */
		.virtual	= GLANTANK_UART,
		.pfn		= __phys_to_pfn(GLANTANK_UART),
		.length		= 0x00100000,
		.type		= MT_DEVICE
	},
};

void __init glantank_map_io(void)
{
	iop3xx_map_io();
	iotable_init(glantank_io_desc, ARRAY_SIZE(glantank_io_desc));
}


/*
 * GLAN Tank PCI.
 */
#define INTA	IRQ_IOP32X_XINT0
#define INTB	IRQ_IOP32X_XINT1
#define INTC	IRQ_IOP32X_XINT2
#define INTD	IRQ_IOP32X_XINT3

static int __init
glantank_pci_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	static int pci_irq_table[][4] = {
		/*
		 * PCI IDSEL/INTPIN->INTLINE
		 * A       B       C       D
		 */
		{INTD, INTD, INTD, INTD}, /* UART (8250) */
		{INTA, INTA, INTA, INTA}, /* Ethernet (E1000) */
		{INTB, INTB, INTB, INTB}, /* IDE (AEC6280R) */
		{INTC, INTC, INTC, INTC}, /* USB (NEC) */
	};

	BUG_ON(pin < 1 || pin > 4);

	return pci_irq_table[slot % 4][pin - 1];
}

static struct hw_pci glantank_pci __initdata = {
	.swizzle	= pci_std_swizzle,
	.nr_controllers = 1,
	.setup		= iop3xx_pci_setup,
	.preinit	= iop3xx_pci_preinit,
	.scan		= iop3xx_pci_scan_bus,
	.map_irq	= glantank_pci_map_irq,
};

static int __init glantank_pci_init(void)
{
	if (machine_is_glantank())
		pci_common_init(&glantank_pci);

	return 0;
}

subsys_initcall(glantank_pci_init);


/*
 * GLAN Tank machine initialization.
 */
static struct physmap_flash_data glantank_flash_data = {
	.width		= 2,
};

static struct resource glantank_flash_resource = {
	.start		= 0xf0000000,
	.end		= 0xf007ffff,
	.flags		= IORESOURCE_MEM,
};

static struct platform_device glantank_flash_device = {
	.name		= "physmap-flash",
	.id		= 0,
	.dev		= {
		.platform_data	= &glantank_flash_data,
	},
	.num_resources	= 1,
	.resource	= &glantank_flash_resource,
};

static struct plat_serial8250_port glantank_serial_port[] = {
	{
		.mapbase	= GLANTANK_UART,
		.membase	= (char *)GLANTANK_UART,
		.irq		= IRQ_IOP32X_XINT3,
		.flags		= UPF_SKIP_TEST,
		.iotype		= UPIO_MEM,
		.regshift	= 0,
		.uartclk	= 1843200,
	},
	{ },
};

static struct resource glantank_uart_resource = {
	.start		= GLANTANK_UART,
	.end		= GLANTANK_UART + 7,
	.flags		= IORESOURCE_MEM,
};

static struct platform_device glantank_serial_device = {
	.name		= "serial8250",
	.id		= PLAT8250_DEV_PLATFORM,
	.dev		= {
		.platform_data		= glantank_serial_port,
	},
	.num_resources	= 1,
	.resource	= &glantank_uart_resource,
};

static struct f75375s_platform_data glantank_f75375s = {
	.pwm		= { 255, 255 },
	.pwm_enable	= { 0, 0 },
};

static struct i2c_board_info __initdata glantank_i2c_devices[] = {
	{
		I2C_BOARD_INFO("rs5c372a", 0x32),
	},
	{
		I2C_BOARD_INFO("f75375", 0x2e),
		.platform_data = &glantank_f75375s,
	},
};

static void glantank_power_off(void)
{
	__raw_writeb(0x01, 0xfe8d0004);

	while (1)
		;
}

static void __init glantank_init_machine(void)
{
	platform_device_register(&iop3xx_i2c0_device);
	platform_device_register(&iop3xx_i2c1_device);
	platform_device_register(&glantank_flash_device);
	platform_device_register(&glantank_serial_device);
	platform_device_register(&iop3xx_dma_0_channel);
	platform_device_register(&iop3xx_dma_1_channel);

	i2c_register_board_info(0, glantank_i2c_devices,
		ARRAY_SIZE(glantank_i2c_devices));

	pm_power_off = glantank_power_off;
}

MACHINE_START(GLANTANK, "GLAN Tank")
	/* Maintainer: Lennert Buytenhek <buytenh@wantstofly.org> */
	.boot_params	= 0xa0000100,
	.map_io		= glantank_map_io,
	.init_irq	= iop32x_init_irq,
	.timer		= &glantank_timer,
	.init_machine	= glantank_init_machine,
MACHINE_END
