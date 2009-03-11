/*
 * linux/arch/arm/mach-omap1/board-voiceblue.c
 *
 * Modified from board-generic.c
 *
 * Copyright (C) 2004 2N Telekomunikace, Ladislav Michl <michl@2n.cz>
 *
 * Code for OMAP5910 based VoiceBlue board (VoIP to GSM gateway).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/serial_8250.h>
#include <linux/serial_reg.h>

#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/flash.h>
#include <asm/mach/map.h>

#include <mach/common.h>
#include <mach/gpio.h>
#include <mach/mux.h>
#include <mach/tc.h>
#include <mach/usb.h>

static struct plat_serial8250_port voiceblue_ports[] = {
	{
		.mapbase	= (unsigned long)(OMAP_CS1_PHYS + 0x40000),
		.irq		= OMAP_GPIO_IRQ(12),
		.flags		= UPF_BOOT_AUTOCONF | UPF_IOREMAP,
		.iotype		= UPIO_MEM,
		.regshift	= 1,
		.uartclk	= 3686400,
	},
	{
		.mapbase	= (unsigned long)(OMAP_CS1_PHYS + 0x50000),
		.irq		= OMAP_GPIO_IRQ(13),
		.flags		= UPF_BOOT_AUTOCONF | UPF_IOREMAP,
		.iotype		= UPIO_MEM,
		.regshift	= 1,
		.uartclk	= 3686400,
	},
	{
		.mapbase	= (unsigned long)(OMAP_CS1_PHYS + 0x60000),
		.irq		= OMAP_GPIO_IRQ(14),
		.flags		= UPF_BOOT_AUTOCONF | UPF_IOREMAP,
		.iotype		= UPIO_MEM,
		.regshift	= 1,
		.uartclk	= 3686400,
	},
	{
		.mapbase	= (unsigned long)(OMAP_CS1_PHYS + 0x70000),
		.irq		= OMAP_GPIO_IRQ(15),
		.flags		= UPF_BOOT_AUTOCONF | UPF_IOREMAP,
		.iotype		= UPIO_MEM,
		.regshift	= 1,
		.uartclk	= 3686400,
	},
	{ },
};

static struct platform_device serial_device = {
	.name			= "serial8250",
	.id			= PLAT8250_DEV_PLATFORM1,
	.dev			= {
		.platform_data	= voiceblue_ports,
	},
};

static int __init ext_uart_init(void)
{
	return platform_device_register(&serial_device);
}
arch_initcall(ext_uart_init);

static struct flash_platform_data voiceblue_flash_data = {
	.map_name	= "cfi_probe",
	.width		= 2,
};

static struct resource voiceblue_flash_resource = {
	.start	= OMAP_CS0_PHYS,
	.end	= OMAP_CS0_PHYS + SZ_32M - 1,
	.flags	= IORESOURCE_MEM,
};

static struct platform_device voiceblue_flash_device = {
	.name		= "omapflash",
	.id		= 0,
	.dev		= {
		.platform_data	= &voiceblue_flash_data,
	},
	.num_resources	= 1,
	.resource	= &voiceblue_flash_resource,
};

static struct resource voiceblue_smc91x_resources[] = {
	[0] = {
		.start	= OMAP_CS2_PHYS + 0x300,
		.end	= OMAP_CS2_PHYS + 0x300 + 16,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= OMAP_GPIO_IRQ(8),
		.end	= OMAP_GPIO_IRQ(8),
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHEDGE,
	},
};

static struct platform_device voiceblue_smc91x_device = {
	.name		= "smc91x",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(voiceblue_smc91x_resources),
	.resource	= voiceblue_smc91x_resources,
};

static struct platform_device *voiceblue_devices[] __initdata = {
	&voiceblue_flash_device,
	&voiceblue_smc91x_device,
};

static struct omap_usb_config voiceblue_usb_config __initdata = {
	.hmc_mode	= 3,
	.register_host	= 1,
	.register_dev   = 1,
	.pins[0]	= 2,
	.pins[1]	= 6,
	.pins[2]	= 6,
};

static struct omap_uart_config voiceblue_uart_config __initdata = {
	.enabled_uarts = ((1 << 0) | (1 << 1) | (1 << 2)),
};

static struct omap_board_config_kernel voiceblue_config[] = {
	{ OMAP_TAG_USB,  &voiceblue_usb_config },
	{ OMAP_TAG_UART, &voiceblue_uart_config },
};

static void __init voiceblue_init_irq(void)
{
	omap1_init_common_hw();
	omap_init_irq();
	omap_gpio_init();
}

static void __init voiceblue_init(void)
{
	/* Watchdog */
	gpio_request(0, "Watchdog");
	/* smc91x reset */
	gpio_request(7, "SMC91x reset");
	gpio_direction_output(7, 1);
	udelay(2);	/* wait at least 100ns */
	gpio_set_value(7, 0);
	mdelay(50);	/* 50ms until PHY ready */
	/* smc91x interrupt pin */
	gpio_request(8, "SMC91x irq");
	/* 16C554 reset*/
	gpio_request(6, "16C554 reset");
	gpio_direction_output(6, 0);
	/* 16C554 interrupt pins */
	gpio_request(12, "16C554 irq");
	gpio_request(13, "16C554 irq");
	gpio_request(14, "16C554 irq");
	gpio_request(15, "16C554 irq");
	set_irq_type(gpio_to_irq(12), IRQ_TYPE_EDGE_RISING);
	set_irq_type(gpio_to_irq(13), IRQ_TYPE_EDGE_RISING);
	set_irq_type(gpio_to_irq(14), IRQ_TYPE_EDGE_RISING);
	set_irq_type(gpio_to_irq(15), IRQ_TYPE_EDGE_RISING);

	platform_add_devices(voiceblue_devices, ARRAY_SIZE(voiceblue_devices));
	omap_board_config = voiceblue_config;
	omap_board_config_size = ARRAY_SIZE(voiceblue_config);
	omap_serial_init();
	omap_register_i2c_bus(1, 100, NULL, 0);

	/* There is a good chance board is going up, so enable power LED
	 * (it is connected through invertor) */
	omap_writeb(0x00, OMAP_LPG1_LCR);
	omap_writeb(0x00, OMAP_LPG1_PMR);	/* Disable clock */
}

static void __init voiceblue_map_io(void)
{
	omap1_map_common_io();
}

#define MACHINE_PANICED		1
#define MACHINE_REBOOTING	2
#define MACHINE_REBOOT		4
static unsigned long machine_state;

static int panic_event(struct notifier_block *this, unsigned long event,
	 void *ptr)
{
	if (test_and_set_bit(MACHINE_PANICED, &machine_state))
		return NOTIFY_DONE;

	/* Flash power LED */
	omap_writeb(0x78, OMAP_LPG1_LCR);
	omap_writeb(0x01, OMAP_LPG1_PMR);	/* Enable clock */

	return NOTIFY_DONE;
}

static struct notifier_block panic_block = {
	.notifier_call	= panic_event,
};

static int __init voiceblue_setup(void)
{
	/* Setup panic notifier */
	atomic_notifier_chain_register(&panic_notifier_list, &panic_block);

	return 0;
}
postcore_initcall(voiceblue_setup);

static int wdt_gpio_state;

void voiceblue_wdt_enable(void)
{
	gpio_direction_output(0, 0);
	gpio_set_value(0, 1);
	gpio_set_value(0, 0);
	wdt_gpio_state = 0;
}

void voiceblue_wdt_disable(void)
{
	gpio_set_value(0, 0);
	gpio_set_value(0, 1);
	gpio_set_value(0, 0);
	gpio_direction_input(0);
}

void voiceblue_wdt_ping(void)
{
	if (test_bit(MACHINE_REBOOT, &machine_state))
		return;

	wdt_gpio_state = !wdt_gpio_state;
	gpio_set_value(0, wdt_gpio_state);
}

void voiceblue_reset(void)
{
	set_bit(MACHINE_REBOOT, &machine_state);
	voiceblue_wdt_enable();
	while (1) ;
}

EXPORT_SYMBOL(voiceblue_wdt_enable);
EXPORT_SYMBOL(voiceblue_wdt_disable);
EXPORT_SYMBOL(voiceblue_wdt_ping);

MACHINE_START(VOICEBLUE, "VoiceBlue OMAP5910")
	/* Maintainer: Ladislav Michl <michl@2n.cz> */
	.phys_io	= 0xfff00000,
	.io_pg_offst	= ((0xfef00000) >> 18) & 0xfffc,
	.boot_params	= 0x10000100,
	.map_io		= voiceblue_map_io,
	.init_irq	= voiceblue_init_irq,
	.init_machine	= voiceblue_init,
	.timer		= &omap_timer,
MACHINE_END
