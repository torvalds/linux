/*
 * arch/arm/mach-ixp4xx/ixdp425-setup.c
 *
 * IXDP425/IXCDP1100 board-setup 
 *
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

#include <asm/types.h>
#include <asm/setup.h>
#include <asm/memory.h>
#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <asm/irq.h>
#include <asm/mach/arch.h>
#include <asm/mach/flash.h>

static struct flash_platform_data ixdp425_flash_data = {
	.map_name	= "cfi_probe",
	.width		= 2,
};

static struct resource ixdp425_flash_resource = {
	.start		= IXDP425_FLASH_BASE,
	.end		= IXDP425_FLASH_BASE + IXDP425_FLASH_SIZE - 1,
	.flags		= IORESOURCE_MEM,
};

static struct platform_device ixdp425_flash = {
	.name		= "IXP4XX-Flash",
	.id		= 0,
	.dev		= {
		.platform_data = &ixdp425_flash_data,
	},
	.num_resources	= 1,
	.resource	= &ixdp425_flash_resource,
};

static struct ixp4xx_i2c_pins ixdp425_i2c_gpio_pins = {
	.sda_pin	= IXDP425_SDA_PIN,
	.scl_pin	= IXDP425_SCL_PIN,
};

static struct platform_device ixdp425_i2c_controller = {
	.name		= "IXP4XX-I2C",
	.id		= 0,
	.dev		= {
		.platform_data = &ixdp425_i2c_gpio_pins,
	},
	.num_resources	= 0
};

static struct resource ixdp425_uart_resources[] = {
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

static struct plat_serial8250_port ixdp425_uart_data[] = {
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
		.irq		= IRQ_IXP4XX_UART1,
		.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
		.uartclk	= IXP4XX_UART_XTAL,
	},
	{ },
};

static struct platform_device ixdp425_uart = {
	.name			= "serial8250",
	.id			= PLAT8250_DEV_PLATFORM,
	.dev.platform_data	= ixdp425_uart_data,
	.num_resources		= 2,
	.resource		= ixdp425_uart_resources
};

static struct platform_device *ixdp425_devices[] __initdata = {
	&ixdp425_i2c_controller,
	&ixdp425_flash,
	&ixdp425_uart
};


static void __init ixdp425_init(void)
{
	ixp4xx_sys_init();

	/*
	 * IXP465 has 32MB window
	 */
	if (machine_is_ixdp465()) {
		ixdp425_flash_resource.end += IXDP425_FLASH_SIZE;
	}

	platform_add_devices(ixdp425_devices, ARRAY_SIZE(ixdp425_devices));
}

#ifdef CONFIG_ARCH_IXDP425
MACHINE_START(IXDP425, "Intel IXDP425 Development Platform")
	/* Maintainer: MontaVista Software, Inc. */
	.phys_ram	= PHYS_OFFSET,
	.phys_io	= IXP4XX_PERIPHERAL_BASE_PHYS,
	.io_pg_offst	= ((IXP4XX_PERIPHERAL_BASE_VIRT) >> 18) & 0xfffc,
	.map_io		= ixp4xx_map_io,
	.init_irq	= ixp4xx_init_irq,
	.timer		= &ixp4xx_timer,
	.boot_params	= 0x0100,
	.init_machine	= ixdp425_init,
MACHINE_END
#endif

#ifdef CONFIG_MACH_IXDP465
MACHINE_START(IXDP465, "Intel IXDP465 Development Platform")
	/* Maintainer: MontaVista Software, Inc. */
	.phys_ram	= PHYS_OFFSET,
	.phys_io	= IXP4XX_PERIPHERAL_BASE_PHYS,
	.io_pg_offst	= ((IXP4XX_PERIPHERAL_BASE_VIRT) >> 18) & 0xfffc,
	.map_io		= ixp4xx_map_io,
	.init_irq	= ixp4xx_init_irq,
	.timer		= &ixp4xx_timer,
	.boot_params	= 0x0100,
	.init_machine	= ixdp425_init,
MACHINE_END
#endif

#ifdef CONFIG_ARCH_PRPMC1100
MACHINE_START(IXCDP1100, "Intel IXCDP1100 Development Platform")
	/* Maintainer: MontaVista Software, Inc. */
	.phys_ram	= PHYS_OFFSET,
	.phys_io	= IXP4XX_PERIPHERAL_BASE_PHYS,
	.io_pg_offst	= ((IXP4XX_PERIPHERAL_BASE_VIRT) >> 18) & 0xfffc,
	.map_io		= ixp4xx_map_io,
	.init_irq	= ixp4xx_init_irq,
	.timer		= &ixp4xx_timer,
	.boot_params	= 0x0100,
	.init_machine	= ixdp425_init,
MACHINE_END
#endif

/*
 * Avila is functionally equivalent to IXDP425 except that it adds
 * a CF IDE slot hanging off the expansion bus. When we have a 
 * driver for IXP4xx CF IDE with driver model support we'll move
 * Avila to it's own setup file.
 */
#ifdef CONFIG_ARCH_AVILA
MACHINE_START(AVILA, "Gateworks Avila Network Platform")
	/* Maintainer: Deepak Saxena <dsaxena@plexity.net> */
	.phys_ram	= PHYS_OFFSET,
	.phys_io	= IXP4XX_PERIPHERAL_BASE_PHYS,
	.io_pg_offst	= ((IXP4XX_PERIPHERAL_BASE_VIRT) >> 18) & 0xfffc,
	.map_io		= ixp4xx_map_io,
	.init_irq	= ixp4xx_init_irq,
	.timer		= &ixp4xx_timer,
	.boot_params	= 0x0100,
	.init_machine	= ixdp425_init,
MACHINE_END
#endif

