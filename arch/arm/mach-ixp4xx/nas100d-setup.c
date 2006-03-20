/*
 * arch/arm/mach-ixp4xx/nas100d-setup.c
 *
 * NAS 100d board-setup
 *
 * based ixdp425-setup.c:
 *      Copyright (C) 2003-2004 MontaVista Software, Inc.
 *
 * Author: Alessandro Zummo <a.zummo@towertech.it>
 * Author: Rod Whitby <rod@whitby.id.au>
 * Maintainers: http://www.nslu2-linux.org/
 *
 */

#include <linux/kernel.h>
#include <linux/serial.h>
#include <linux/serial_8250.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/flash.h>

static struct flash_platform_data nas100d_flash_data = {
	.map_name		= "cfi_probe",
	.width			= 2,
};

static struct resource nas100d_flash_resource = {
	.flags			= IORESOURCE_MEM,
};

static struct platform_device nas100d_flash = {
	.name			= "IXP4XX-Flash",
	.id			= 0,
	.dev.platform_data	= &nas100d_flash_data,
	.num_resources		= 1,
	.resource		= &nas100d_flash_resource,
};

static struct ixp4xx_i2c_pins nas100d_i2c_gpio_pins = {
	.sda_pin		= NAS100D_SDA_PIN,
	.scl_pin		= NAS100D_SCL_PIN,
};

static struct platform_device nas100d_i2c_controller = {
	.name			= "IXP4XX-I2C",
	.id			= 0,
	.dev.platform_data	= &nas100d_i2c_gpio_pins,
	.num_resources		= 0,
};

static struct resource nas100d_uart_resources[] = {
	{
		.start		= IXP4XX_UART1_BASE_PHYS,
		.end		= IXP4XX_UART1_BASE_PHYS + 0x0fff,
		.flags		= IORESOURCE_MEM,
	},
	{
		.start		= IXP4XX_UART2_BASE_PHYS,
		.end		= IXP4XX_UART2_BASE_PHYS + 0x0fff,
		.flags		= IORESOURCE_MEM,
	}
};

static struct plat_serial8250_port nas100d_uart_data[] = {
	{
		.mapbase	= IXP4XX_UART1_BASE_PHYS,
		.membase	= (char *)IXP4XX_UART1_BASE_VIRT + REG_OFFSET,
		.irq		= IRQ_IXP4XX_UART1,
		.flags		= UPF_BOOT_AUTOCONF,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
		.uartclk	= IXP4XX_UART_XTAL,
	},
	{
		.mapbase	= IXP4XX_UART2_BASE_PHYS,
		.membase	= (char *)IXP4XX_UART2_BASE_VIRT + REG_OFFSET,
		.irq		= IRQ_IXP4XX_UART2,
		.flags		= UPF_BOOT_AUTOCONF,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
		.uartclk	= IXP4XX_UART_XTAL,
	},
	{ }
};

static struct platform_device nas100d_uart = {
	.name			= "serial8250",
	.id			= PLAT8250_DEV_PLATFORM,
	.dev.platform_data	= nas100d_uart_data,
	.num_resources		= 2,
	.resource		= nas100d_uart_resources,
};

static struct platform_device *nas100d_devices[] __initdata = {
	&nas100d_i2c_controller,
	&nas100d_flash,
	&nas100d_uart,
};

static void nas100d_power_off(void)
{
	/* This causes the box to drop the power and go dead. */

	/* enable the pwr cntl gpio */
	gpio_line_config(NAS100D_PO_GPIO, IXP4XX_GPIO_OUT);

	/* do the deed */
	gpio_line_set(NAS100D_PO_GPIO, IXP4XX_GPIO_HIGH);
}

static void __init nas100d_init(void)
{
	ixp4xx_sys_init();

	/* gpio 14 and 15 are _not_ clocks */
	*IXP4XX_GPIO_GPCLKR = 0;

	nas100d_flash_resource.start = IXP4XX_EXP_BUS_BASE(0);
	nas100d_flash_resource.end =
		IXP4XX_EXP_BUS_BASE(0) + ixp4xx_exp_bus_size - 1;

	pm_power_off = nas100d_power_off;

	platform_add_devices(nas100d_devices, ARRAY_SIZE(nas100d_devices));
}

MACHINE_START(NAS100D, "Iomega NAS 100d")
	/* Maintainer: www.nslu2-linux.org */
	.phys_io	= IXP4XX_PERIPHERAL_BASE_PHYS,
	.io_pg_offst	= ((IXP4XX_PERIPHERAL_BASE_VIRT) >> 18) & 0xFFFC,
	.boot_params	= 0x00000100,
	.map_io		= ixp4xx_map_io,
	.init_irq	= ixp4xx_init_irq,
	.timer          = &ixp4xx_timer,
	.init_machine	= nas100d_init,
MACHINE_END
