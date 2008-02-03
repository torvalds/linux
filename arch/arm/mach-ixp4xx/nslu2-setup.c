/*
 * arch/arm/mach-ixp4xx/nslu2-setup.c
 *
 * NSLU2 board-setup
 *
 * based ixdp425-setup.c:
 *      Copyright (C) 2003-2004 MontaVista Software, Inc.
 *
 * Author: Mark Rakes <mrakes at mac.com>
 * Author: Rod Whitby <rod@whitby.id.au>
 * Maintainers: http://www.nslu2-linux.org/
 *
 * Fixed missing init_time in MACHINE_START kas11 10/22/04
 * Changed to conform to new style __init ixdp425 kas11 10/22/04
 */

#include <linux/kernel.h>
#include <linux/serial.h>
#include <linux/serial_8250.h>
#include <linux/leds.h>
#include <linux/i2c-gpio.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/flash.h>
#include <asm/mach/time.h>

static struct flash_platform_data nslu2_flash_data = {
	.map_name		= "cfi_probe",
	.width			= 2,
};

static struct resource nslu2_flash_resource = {
	.flags			= IORESOURCE_MEM,
};

static struct platform_device nslu2_flash = {
	.name			= "IXP4XX-Flash",
	.id			= 0,
	.dev.platform_data	= &nslu2_flash_data,
	.num_resources		= 1,
	.resource		= &nslu2_flash_resource,
};

static struct i2c_gpio_platform_data nslu2_i2c_gpio_data = {
	.sda_pin		= NSLU2_SDA_PIN,
	.scl_pin		= NSLU2_SCL_PIN,
};

#ifdef CONFIG_LEDS_IXP4XX
static struct resource nslu2_led_resources[] = {
	{
		.name		= "ready",  /* green led */
		.start		= NSLU2_LED_GRN_GPIO,
		.end		= NSLU2_LED_GRN_GPIO,
		.flags		= IXP4XX_GPIO_HIGH,
	},
	{
		.name		= "status", /* red led */
		.start		= NSLU2_LED_RED_GPIO,
		.end		= NSLU2_LED_RED_GPIO,
		.flags		= IXP4XX_GPIO_HIGH,
	},
	{
		.name		= "disk-1",
		.start		= NSLU2_LED_DISK1_GPIO,
		.end		= NSLU2_LED_DISK1_GPIO,
		.flags		= IXP4XX_GPIO_LOW,
	},
	{
		.name		= "disk-2",
		.start		= NSLU2_LED_DISK2_GPIO,
		.end		= NSLU2_LED_DISK2_GPIO,
		.flags		= IXP4XX_GPIO_LOW,
	},
};

static struct platform_device nslu2_leds = {
	.name			= "IXP4XX-GPIO-LED",
	.id			= -1,
	.num_resources		= ARRAY_SIZE(nslu2_led_resources),
	.resource		= nslu2_led_resources,
};
#endif

static struct platform_device nslu2_i2c_gpio = {
	.name			= "i2c-gpio",
	.id			= 0,
	.dev	 = {
		.platform_data	= &nslu2_i2c_gpio_data,
	},
};

static struct platform_device nslu2_beeper = {
	.name			= "ixp4xx-beeper",
	.id			= NSLU2_GPIO_BUZZ,
	.num_resources		= 0,
};

static struct resource nslu2_uart_resources[] = {
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

static struct plat_serial8250_port nslu2_uart_data[] = {
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

static struct platform_device nslu2_uart = {
	.name			= "serial8250",
	.id			= PLAT8250_DEV_PLATFORM,
	.dev.platform_data	= nslu2_uart_data,
	.num_resources		= 2,
	.resource		= nslu2_uart_resources,
};

static struct platform_device *nslu2_devices[] __initdata = {
	&nslu2_i2c_gpio,
	&nslu2_flash,
	&nslu2_beeper,
#ifdef CONFIG_LEDS_IXP4XX
	&nslu2_leds,
#endif
};

static void nslu2_power_off(void)
{
	/* This causes the box to drop the power and go dead. */

	/* enable the pwr cntl gpio */
	gpio_line_config(NSLU2_PO_GPIO, IXP4XX_GPIO_OUT);

	/* do the deed */
	gpio_line_set(NSLU2_PO_GPIO, IXP4XX_GPIO_HIGH);
}

static void __init nslu2_timer_init(void)
{
    /* The xtal on this machine is non-standard. */
    ixp4xx_timer_freq = NSLU2_FREQ;

    /* Call standard timer_init function. */
    ixp4xx_timer_init();
}

static struct sys_timer nslu2_timer = {
    .init   = nslu2_timer_init,
};

static void __init nslu2_init(void)
{
	ixp4xx_sys_init();

	nslu2_flash_resource.start = IXP4XX_EXP_BUS_BASE(0);
	nslu2_flash_resource.end =
		IXP4XX_EXP_BUS_BASE(0) + ixp4xx_exp_bus_size - 1;

	pm_power_off = nslu2_power_off;

	/*
	 * This is only useful on a modified machine, but it is valuable
	 * to have it first in order to see debug messages, and so that
	 * it does *not* get removed if platform_add_devices fails!
	 */
	(void)platform_device_register(&nslu2_uart);

	platform_add_devices(nslu2_devices, ARRAY_SIZE(nslu2_devices));
}

MACHINE_START(NSLU2, "Linksys NSLU2")
	/* Maintainer: www.nslu2-linux.org */
	.phys_io	= IXP4XX_PERIPHERAL_BASE_PHYS,
	.io_pg_offst	= ((IXP4XX_PERIPHERAL_BASE_VIRT) >> 18) & 0xFFFC,
	.boot_params	= 0x00000100,
	.map_io		= ixp4xx_map_io,
	.init_irq	= ixp4xx_init_irq,
	.timer          = &nslu2_timer,
	.init_machine	= nslu2_init,
MACHINE_END
