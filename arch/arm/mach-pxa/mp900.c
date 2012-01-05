/*
 *  linux/arch/arm/mach-pxa/mp900.c
 *
 *  Support for the NEC MobilePro900/C platform
 *
 *  Based on mach-pxa/gumstix.c
 *
 *  2007, 2008 Kristoffer Ericson <kristoffer.ericson@gmail.com>
 *  2007, 2008 Michael Petchkovsky <mkpetch@internode.on.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/usb/isp116x.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include <mach/pxa25x.h>
#include "generic.h"

static void isp116x_pfm_delay(struct device *dev, int delay)
{

	/* 400Mhz PXA2 = 2.5ns / instruction */

	int cyc = delay / 10;

	/* 4 Instructions = 4 x 2.5ns = 10ns */
	__asm__ volatile ("0:\n"
		"subs %0, %1, #1\n"
		"bge 0b\n"
		:"=r" (cyc)
		:"0"(cyc)
	);
}

static struct isp116x_platform_data isp116x_pfm_data = {
	.remote_wakeup_enable = 1,
	.delay = isp116x_pfm_delay,
};

static struct resource isp116x_pfm_resources[] = {
	[0] =	{
		.start	= 0x0d000000,
		.end	= 0x0d000000 + 1,
		.flags	= IORESOURCE_MEM,
		},
	[1] =	{
		.start  = 0x0d000000 + 4,
		.end	= 0x0d000000 + 5,
		.flags  = IORESOURCE_MEM,
		},
	[2] =	{
		.start	= 61,
		.end	= 61,
		.flags	= IORESOURCE_IRQ,
		},
};

static struct platform_device mp900c_dummy_device = {
	.name		= "mp900c_dummy",
	.id		= -1,
};

static struct platform_device mp900c_usb = {
	.name		= "isp116x-hcd",
	.num_resources	= ARRAY_SIZE(isp116x_pfm_resources),
	.resource	= isp116x_pfm_resources,
	.dev.platform_data = &isp116x_pfm_data,
};

static struct platform_device *devices[] __initdata = {
	&mp900c_dummy_device,
	&mp900c_usb,
};

static void __init mp900c_init(void)
{
	printk(KERN_INFO "MobilePro 900/C machine init\n");
	pxa_set_ffuart_info(NULL);
	pxa_set_btuart_info(NULL);
	pxa_set_stuart_info(NULL);
	platform_add_devices(devices, ARRAY_SIZE(devices));
}

/* Maintainer - Michael Petchkovsky <mkpetch@internode.on.net> */
MACHINE_START(NEC_MP900, "MobilePro900/C")
	.atag_offset	= 0x220100,
	.timer		= &pxa_timer,
	.map_io		= pxa25x_map_io,
	.init_irq	= pxa25x_init_irq,
	.handle_irq	= pxa25x_handle_irq,
	.init_machine	= mp900c_init,
	.restart	= pxa_restart,
MACHINE_END

