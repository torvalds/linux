/*
 *  linux/arch/arm/mach-pxa/saar.c
 *
 *  Support for the Marvell PXA930 Handheld Platform (aka SAAR)
 *
 *  Copyright (C) 2007-2008 Marvell International Ltd.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  publishhed by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/smc91x.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <mach/hardware.h>
#include <mach/pxa3xx-regs.h>
#include <mach/mfp-pxa930.h>

#include "devices.h"
#include "generic.h"

/* SAAR MFP configurations */
static mfp_cfg_t saar_mfp_cfg[] __initdata = {
	/* Ethernet */
	DF_nCS1_nCS3,
	GPIO97_GPIO,
};

#define SAAR_ETH_PHYS	(0x14000000)

static struct resource smc91x_resources[] = {
	[0] = {
		.start	= (SAAR_ETH_PHYS + 0x300),
		.end	= (SAAR_ETH_PHYS + 0xfffff),
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= gpio_to_irq(mfp_to_gpio(MFP_PIN_GPIO97)),
		.end	= gpio_to_irq(mfp_to_gpio(MFP_PIN_GPIO97)),
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHEDGE,
	}
};

static struct smc91x_platdata saar_smc91x_info = {
	.flags	= SMC91X_USE_16BIT | SMC91X_NOWAIT | SMC91X_USE_DMA,
};

static struct platform_device smc91x_device = {
	.name		= "smc91x",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(smc91x_resources),
	.resource	= smc91x_resources,
	.dev		= {
		.platform_data = &saar_smc91x_info,
	},
};

static void __init saar_init(void)
{
	/* initialize MFP configurations */
	pxa3xx_mfp_config(ARRAY_AND_SIZE(saar_mfp_cfg));

	platform_device_register(&smc91x_device);
}

MACHINE_START(SAAR, "PXA930 Handheld Platform (aka SAAR)")
	/* Maintainer: Eric Miao <eric.miao@marvell.com> */
	.phys_io        = 0x40000000,
	.boot_params    = 0xa0000100,
	.io_pg_offst    = (io_p2v(0x40000000) >> 18) & 0xfffc,
	.map_io         = pxa_map_io,
	.init_irq       = pxa3xx_init_irq,
	.timer          = &pxa_timer,
	.init_machine   = saar_init,
MACHINE_END
