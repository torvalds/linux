/*
 * DSM-G600 board-setup
 *
 * Copyright (C) 2006 Tower Technologies
 * Author: Alessandro Zummo <a.zummo@towertech.it>
 *
 * based ixdp425-setup.c:
 *      Copyright (C) 2003-2004 MontaVista Software, Inc.
 *
 * Author: Alessandro Zummo <a.zummo@towertech.it>
 * Maintainers: http://www.nslu2-linux.org/
 */

#include <linux/kernel.h>
#include <linux/serial.h>
#include <linux/serial_8250.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/flash.h>

static struct flash_platform_data dsmg600_flash_data = {
	.map_name		= "cfi_probe",
	.width			= 2,
};

static struct resource dsmg600_flash_resource = {
	.flags			= IORESOURCE_MEM,
};

static struct platform_device dsmg600_flash = {
	.name			= "IXP4XX-Flash",
	.id			= 0,
	.dev.platform_data	= &dsmg600_flash_data,
	.num_resources		= 1,
	.resource		= &dsmg600_flash_resource,
};

static struct ixp4xx_i2c_pins dsmg600_i2c_gpio_pins = {
	.sda_pin		= DSMG600_SDA_PIN,
	.scl_pin		= DSMG600_SCL_PIN,
};

static struct platform_device dsmg600_i2c_controller = {
	.name			= "IXP4XX-I2C",
	.id			= 0,
	.dev.platform_data	= &dsmg600_i2c_gpio_pins,
};

#ifdef CONFIG_LEDS_CLASS
static struct resource dsmg600_led_resources[] = {
	{
		.name           = "power",
		.start          = DSMG600_LED_PWR_GPIO,
		.end            = DSMG600_LED_PWR_GPIO,
		.flags          = IXP4XX_GPIO_HIGH,
	},
	{
		.name           = "wlan",
		.start		= DSMG600_LED_WLAN_GPIO,
		.end            = DSMG600_LED_WLAN_GPIO,
		.flags          = IXP4XX_GPIO_LOW,
	},
};

static struct platform_device dsmg600_leds = {
        .name                   = "IXP4XX-GPIO-LED",
        .id                     = -1,
        .num_resources          = ARRAY_SIZE(dsmg600_led_resources),
        .resource               = dsmg600_led_resources,
};
#endif

static struct resource dsmg600_uart_resources[] = {
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

static struct plat_serial8250_port dsmg600_uart_data[] = {
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

static struct platform_device dsmg600_uart = {
	.name			= "serial8250",
	.id			= PLAT8250_DEV_PLATFORM,
	.dev.platform_data	= dsmg600_uart_data,
	.num_resources		= ARRAY_SIZE(dsmg600_uart_resources),
	.resource		= dsmg600_uart_resources,
};

static struct platform_device *dsmg600_devices[] __initdata = {
	&dsmg600_i2c_controller,
	&dsmg600_flash,
};

static void dsmg600_power_off(void)
{
	/* enable the pwr cntl gpio */
	gpio_line_config(DSMG600_PO_GPIO, IXP4XX_GPIO_OUT);

	/* poweroff */
	gpio_line_set(DSMG600_PO_GPIO, IXP4XX_GPIO_HIGH);
}

static void __init dsmg600_init(void)
{
	ixp4xx_sys_init();

	/* Make sure that GPIO14 and GPIO15 are not used as clocks */
	*IXP4XX_GPIO_GPCLKR = 0;

	dsmg600_flash_resource.start = IXP4XX_EXP_BUS_BASE(0);
	dsmg600_flash_resource.end =
		IXP4XX_EXP_BUS_BASE(0) + ixp4xx_exp_bus_size - 1;

	pm_power_off = dsmg600_power_off;

	/* The UART is required on the DSM-G600 (Redboot cannot use the
	 * NIC) -- do it here so that it does *not* get removed if
	 * platform_add_devices fails!
         */
        (void)platform_device_register(&dsmg600_uart);

	platform_add_devices(dsmg600_devices, ARRAY_SIZE(dsmg600_devices));

#ifdef CONFIG_LEDS_CLASS
        /* We don't care whether or not this works. */
        (void)platform_device_register(&dsmg600_leds);
#endif
}

static void __init dsmg600_fixup(struct machine_desc *desc,
                struct tag *tags, char **cmdline, struct meminfo *mi)
{
       /* The xtal on this machine is non-standard. */
        ixp4xx_timer_freq = DSMG600_FREQ;
}

MACHINE_START(DSMG600, "D-Link DSM-G600 RevA")
	/* Maintainer: www.nslu2-linux.org */
	.phys_io	= IXP4XX_PERIPHERAL_BASE_PHYS,
	.io_pg_offst	= ((IXP4XX_PERIPHERAL_BASE_VIRT) >> 18) & 0xFFFC,
	.boot_params	= 0x00000100,
	.fixup          = dsmg600_fixup,
	.map_io		= ixp4xx_map_io,
	.init_irq	= ixp4xx_init_irq,
	.timer          = &ixp4xx_timer,
	.init_machine	= dsmg600_init,
MACHINE_END
