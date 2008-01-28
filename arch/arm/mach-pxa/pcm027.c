/*
 *  linux/arch/arm/mach-pxa/pcm027.c
 *  Support for the Phytec phyCORE-PXA270 CPU card (aka PCM-027).
 *
 *  Refer
 *   http://www.phytec.com/products/sbc/ARM-XScale/phyCORE-XScale-PXA270.html
 *  for additional hardware info
 *
 *  Author:	Juergen Kilb
 *  Created:	April 05, 2005
 *  Copyright:	Phytec Messtechnik GmbH
 *  e-Mail:	armlinux@phytec.de
 *
 *  based on Intel Mainstone Board
 *
 *  Copyright 2007 Juergen Beisert @ Pengutronix (j.beisert@pengutronix.de)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/mtd/physmap.h>
#include <linux/spi/spi.h>
#include <linux/leds.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/arch/hardware.h>
#include <asm/arch/pxa-regs.h>
#include <asm/arch/pxa2xx_spi.h>
#include <asm/arch/pcm027.h>
#include "generic.h"

/*
 * ABSTRACT:
 *
 * The PXA270 processor comes with a bunch of hardware on its silicon.
 * Not all of this hardware can be used at the same time and not all
 * is routed to module's connectors. Also it depends on the baseboard, what
 * kind of hardware can be used in which way.
 * -> So this file supports the main devices on the CPU card only!
 * Refer pcm990-baseboard.c how to extend this features to get a full
 * blown system with many common interfaces.
 *
 * The PCM-027 supports the following interfaces through its connectors and
 * will be used in pcm990-baseboard.c:
 *
 * - LCD support
 * - MMC support
 * - IDE/CF card
 * - FFUART
 * - BTUART
 * - IRUART
 * - AC97
 * - SSP
 * - SSP3
 *
 * Claimed GPIOs:
 * GPIO0 -> IRQ input from RTC
 * GPIO2 -> SYS_ENA*)
 * GPIO3 -> PWR_SCL
 * GPIO4 -> PWR_SDA
 * GPIO5 -> PowerCap0*)
 * GPIO6 -> PowerCap1*)
 * GPIO7 -> PowerCap2*)
 * GPIO8 -> PowerCap3*)
 * GPIO15 -> /CS1
 * GPIO20 -> /CS2
 * GPIO21 -> /CS3
 * GPIO33 -> /CS5 network controller select
 * GPIO52 -> IRQ from network controller
 * GPIO78 -> /CS2
 * GPIO80 -> /CS4
 * GPIO90 -> LED0
 * GPIO91 -> LED1
 * GPIO114 -> IRQ from CAN controller
 * GPIO117 -> SCL
 * GPIO118 -> SDA
 *
 * *) CPU internal use only
 */

/*
 * SMC91x network controller specific stuff
 */
static struct resource smc91x_resources[] = {
	[0] = {
		.start	= PCM027_ETH_PHYS + 0x300,
		.end	= PCM027_ETH_PHYS + PCM027_ETH_SIZE,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= PCM027_ETH_IRQ,
		.end	= PCM027_ETH_IRQ,
		/* note: smc91x's driver doesn't use the trigger bits yet */
		.flags	= IORESOURCE_IRQ | PCM027_ETH_IRQ_EDGE,
	}
};

static struct platform_device smc91x_device = {
	.name		= "smc91x",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(smc91x_resources),
	.resource	= smc91x_resources,
};

static struct physmap_flash_data pcm027_flash_data = {
	.width  = 4,
};

static struct resource pcm027_flash_resource = {
	.start          = PCM027_FLASH_PHYS,
	.end            = PCM027_FLASH_PHYS + PCM027_FLASH_SIZE - 1 ,
	.flags          = IORESOURCE_MEM,
};

static struct platform_device pcm027_flash = {
	.name           = "physmap-flash",
	.id             = 0,
	.dev            = {
		.platform_data  = &pcm027_flash_data,
	},
	.resource       = &pcm027_flash_resource,
	.num_resources  = 1,
};

#ifdef CONFIG_LEDS_GPIO

static struct gpio_led pcm027_led[] = {
	{
		.name = "led0:red",	/* FIXME */
		.gpio = PCM027_LED_CPU
	},
	{
		.name = "led1:green",	/* FIXME */
		.gpio = PCM027_LED_HEARD_BEAT
	},
};

static struct gpio_led_platform_data pcm027_led_data = {
	.num_leds	= ARRAY_SIZE(pcm027_led),
	.leds		= pcm027_led
};

static struct platform_device pcm027_led_dev = {
	.name		= "leds-gpio",
	.id		= 0,
	.dev		= {
		.platform_data	= &pcm027_led_data,
	},
};

#endif /* CONFIG_LEDS_GPIO */

/*
 * declare the available device resources on this board
 */
static struct platform_device *devices[] __initdata = {
	&smc91x_device,
	&pcm027_flash,
#ifdef CONFIG_LEDS_GPIO
	&pcm027_led_dev
#endif
};

/*
 * pcm027_init - breath some life into the board
 */
static void __init pcm027_init(void)
{
	/* system bus arbiter setting
	 * - Core_Park
	 * - LCD_wt:DMA_wt:CORE_Wt = 2:3:4
	 */
	ARB_CNTRL = ARB_CORE_PARK | 0x234;

	platform_add_devices(devices, ARRAY_SIZE(devices));

	/* LEDs (on demand only) */
#ifdef CONFIG_LEDS_GPIO
	pxa_gpio_mode(PCM027_LED_CPU | GPIO_OUT);
	pxa_gpio_mode(PCM027_LED_HEARD_BEAT | GPIO_OUT);
#endif /* CONFIG_LEDS_GPIO */

	/* at last call the baseboard to initialize itself */
#ifdef CONFIG_MACH_PCM990_BASEBOARD
	pcm990_baseboard_init();
#endif
}

static void __init pcm027_map_io(void)
{
	pxa_map_io();

	/* initialize sleep mode regs (wake-up sources, etc) */
	PGSR0 = 0x01308000;
	PGSR1 = 0x00CF0002;
	PGSR2 = 0x0E294000;
	PGSR3 = 0x0000C000;
	PWER  = 0x40000000 | PWER_GPIO0 | PWER_GPIO1;
	PRER  = 0x00000000;
	PFER  = 0x00000003;
}

MACHINE_START(PCM027, "Phytec Messtechnik GmbH phyCORE-PXA270")
	/* Maintainer: Pengutronix */
	.boot_params	= 0xa0000100,
	.phys_io	= 0x40000000,
	.io_pg_offst	= (io_p2v(0x40000000) >> 18) & 0xfffc,
	.map_io		= pcm027_map_io,
	.init_irq	= pxa27x_init_irq,
	.timer		= &pxa_timer,
	.init_machine	= pcm027_init,
MACHINE_END
