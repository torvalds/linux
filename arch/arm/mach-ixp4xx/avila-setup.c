// SPDX-License-Identifier: GPL-2.0
/*
 * arch/arm/mach-ixp4xx/avila-setup.c
 *
 * Gateworks Avila board-setup
 *
 * Author: Michael-Luke Jones <mlj28@cam.ac.uk>
 *
 * Based on ixdp-setup.c
 * Copyright (C) 2003-2005 MontaVista Software, Inc.
 *
 * Author: Deepak Saxena <dsaxena@plexity.net>
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/serial.h>
#include <linux/tty.h>
#include <linux/serial_8250.h>
#include <linux/gpio/machine.h>
#include <asm/types.h>
#include <asm/setup.h>
#include <asm/memory.h>
#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/irq.h>
#include <asm/mach/arch.h>
#include <asm/mach/flash.h>

#define AVILA_SDA_PIN	7
#define AVILA_SCL_PIN	6

static struct flash_platform_data avila_flash_data = {
	.map_name	= "cfi_probe",
	.width		= 2,
};

static struct resource avila_flash_resource = {
	.flags		= IORESOURCE_MEM,
};

static struct platform_device avila_flash = {
	.name		= "IXP4XX-Flash",
	.id		= 0,
	.dev		= {
		.platform_data = &avila_flash_data,
	},
	.num_resources	= 1,
	.resource	= &avila_flash_resource,
};

static struct gpiod_lookup_table avila_i2c_gpiod_table = {
	.dev_id		= "i2c-gpio.0",
	.table		= {
		GPIO_LOOKUP_IDX("IXP4XX_GPIO_CHIP", AVILA_SDA_PIN,
				NULL, 0, GPIO_ACTIVE_HIGH | GPIO_OPEN_DRAIN),
		GPIO_LOOKUP_IDX("IXP4XX_GPIO_CHIP", AVILA_SCL_PIN,
				NULL, 1, GPIO_ACTIVE_HIGH | GPIO_OPEN_DRAIN),
	},
};

static struct platform_device avila_i2c_gpio = {
	.name		= "i2c-gpio",
	.id		= 0,
	.dev	 = {
		.platform_data	= NULL,
	},
};

static struct resource avila_uart_resources[] = {
	{
		.start		= IXP4XX_UART1_BASE_PHYS,
		.end		= IXP4XX_UART1_BASE_PHYS + 0x0fff,
		.flags		= IORESOURCE_MEM
	},
	{
		.start		= IXP4XX_UART2_BASE_PHYS,
		.end		= IXP4XX_UART2_BASE_PHYS + 0x0fff,
		.flags		= IORESOURCE_MEM
	}
};

static struct plat_serial8250_port avila_uart_data[] = {
	{
		.mapbase	= IXP4XX_UART1_BASE_PHYS,
		.membase	= (char *)IXP4XX_UART1_BASE_VIRT + REG_OFFSET,
		.irq		= IRQ_IXP4XX_UART1,
		.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
		.uartclk	= IXP4XX_UART_XTAL,
	},
	{
		.mapbase	= IXP4XX_UART2_BASE_PHYS,
		.membase	= (char *)IXP4XX_UART2_BASE_VIRT + REG_OFFSET,
		.irq		= IRQ_IXP4XX_UART2,
		.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
		.uartclk	= IXP4XX_UART_XTAL,
	},
	{ },
};

static struct platform_device avila_uart = {
	.name			= "serial8250",
	.id			= PLAT8250_DEV_PLATFORM,
	.dev.platform_data	= avila_uart_data,
	.num_resources		= 2,
	.resource		= avila_uart_resources
};

static struct resource avila_pata_resources[] = {
	{
		.flags	= IORESOURCE_MEM
	},
	{
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= "intrq",
		.start	= IRQ_IXP4XX_GPIO12,
		.end	= IRQ_IXP4XX_GPIO12,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct ixp4xx_pata_data avila_pata_data = {
	.cs0_bits	= 0xbfff0043,
	.cs1_bits	= 0xbfff0043,
};

static struct platform_device avila_pata = {
	.name			= "pata_ixp4xx_cf",
	.id			= 0,
	.dev.platform_data      = &avila_pata_data,
	.num_resources		= ARRAY_SIZE(avila_pata_resources),
	.resource		= avila_pata_resources,
};

static struct platform_device *avila_devices[] __initdata = {
	&avila_i2c_gpio,
	&avila_flash,
	&avila_uart
};

static void __init avila_init(void)
{
	ixp4xx_sys_init();

	avila_flash_resource.start = IXP4XX_EXP_BUS_BASE(0);
	avila_flash_resource.end =
		IXP4XX_EXP_BUS_BASE(0) + ixp4xx_exp_bus_size - 1;

	gpiod_add_lookup_table(&avila_i2c_gpiod_table);

	platform_add_devices(avila_devices, ARRAY_SIZE(avila_devices));

	avila_pata_resources[0].start = IXP4XX_EXP_BUS_BASE(1);
	avila_pata_resources[0].end = IXP4XX_EXP_BUS_END(1);

	avila_pata_resources[1].start = IXP4XX_EXP_BUS_BASE(2);
	avila_pata_resources[1].end = IXP4XX_EXP_BUS_END(2);

	avila_pata_data.cs0_cfg = IXP4XX_EXP_CS1;
	avila_pata_data.cs1_cfg = IXP4XX_EXP_CS2;

	platform_device_register(&avila_pata);

}

MACHINE_START(AVILA, "Gateworks Avila Network Platform")
	/* Maintainer: Deepak Saxena <dsaxena@plexity.net> */
	.map_io		= ixp4xx_map_io,
	.init_early	= ixp4xx_init_early,
	.init_irq	= ixp4xx_init_irq,
	.init_time	= ixp4xx_timer_init,
	.atag_offset	= 0x100,
	.init_machine	= avila_init,
#if defined(CONFIG_PCI)
	.dma_zone_size	= SZ_64M,
#endif
	.restart	= ixp4xx_restart,
MACHINE_END

 /*
  * Loft is functionally equivalent to Avila except that it has a
  * different number for the maximum PCI devices.  The MACHINE
  * structure below is identical to Avila except for the comment.
  */
#ifdef CONFIG_MACH_LOFT
MACHINE_START(LOFT, "Giant Shoulder Inc Loft board")
	/* Maintainer: Tom Billman <kernel@giantshoulderinc.com> */
	.map_io		= ixp4xx_map_io,
	.init_early	= ixp4xx_init_early,
	.init_irq	= ixp4xx_init_irq,
	.init_time	= ixp4xx_timer_init,
	.atag_offset	= 0x100,
	.init_machine	= avila_init,
#if defined(CONFIG_PCI)
	.dma_zone_size	= SZ_64M,
#endif
	.restart	= ixp4xx_restart,
MACHINE_END
#endif

