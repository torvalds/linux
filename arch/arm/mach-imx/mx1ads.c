/*
 * arch/arm/mach-imx/mx1ads.c
 *
 * Initially based on:
 *	linux-2.6.7-imx/arch/arm/mach-imx/scb9328.c
 *	Copyright (c) 2004 Sascha Hauer <sascha@saschahauer.de>
 *
 * 2004 (c) MontaVista Software, Inc.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/device.h>
#include <linux/init.h>
#include <asm/system.h>
#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/pgtable.h>
#include <asm/page.h>

#include <asm/mach/map.h>
#include <asm/mach-types.h>

#include <asm/mach/arch.h>
#include <linux/interrupt.h>
#include "generic.h"
#include <asm/serial.h>

static struct resource mx1ads_resources[] = {
	[0] = {
		.start	= IMX_CS4_VIRT,
		.end	= IMX_CS4_VIRT + 16,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= 13,
		.end	= 13,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device mx1ads_device = {
	.name		= "mx1ads",
	.num_resources	= ARRAY_SIZE(mx1ads_resources),
	.resource	= mx1ads_resources,
};

static struct platform_device *devices[] __initdata = {
	&mx1ads_device,
};

static void __init
mx1ads_init(void)
{
#ifdef CONFIG_LEDS
	imx_gpio_mode(GPIO_PORTA | GPIO_OUT | 2);
#endif
	platform_add_devices(devices, ARRAY_SIZE(devices));
}

static struct map_desc mx1ads_io_desc[] __initdata = {
	{
		.virtual	= IMX_CS0_VIRT,
		.pfn		= __phys_to_pfn(IMX_CS0_PHYS),
		.length		= IMX_CS0_SIZE,
		.type		= MT_DEVICE
	}, {
		.virtual	= IMX_CS1_VIRT,
		.pfn		= __phys_to_pfn(IMX_CS1_PHYS),
		.length		= IMX_CS1_SIZE,
		.type		= MT_DEVICE
	}, {
		.virtual	= IMX_CS2_VIRT,
		.pfn		= __phys_to_pfn(IMX_CS2_PHYS),
		.length		= IMX_CS2_SIZE,
		.type		= MT_DEVICE
	}, {
		.virtual	= IMX_CS3_VIRT,
		.pfn		= __phys_to_pfn(IMX_CS3_PHYS),
		.length		= IMX_CS3_SIZE,
		.type		= MT_DEVICE
	}, {
		.virtual	= IMX_CS4_VIRT,
		.pfn		= __phys_to_pfn(IMX_CS4_PHYS),
		.length		= IMX_CS4_SIZE,
		.type		= MT_DEVICE
	}, {
		.virtual	= IMX_CS5_VIRT,
		.pfn		= __phys_to_pfn(IMX_CS5_PHYS),
		.length		= IMX_CS5_SIZE,
		.type		= MT_DEVICE
	}
};

static void __init
mx1ads_map_io(void)
{
	imx_map_io();
	iotable_init(mx1ads_io_desc, ARRAY_SIZE(mx1ads_io_desc));
}

MACHINE_START(MX1ADS, "Motorola MX1ADS")
	/* Maintainer: Sascha Hauer, Pengutronix */
	.phys_ram	= 0x08000000,
	.phys_io	= 0x00200000,
	.io_pg_offst	= ((0xe0200000) >> 18) & 0xfffc,
	.boot_params	= 0x08000100,
	.map_io		= mx1ads_map_io,
	.init_irq	= imx_init_irq,
	.timer		= &imx_timer,
	.init_machine	= mx1ads_init,
MACHINE_END
