/*
 *  linux/arch/arm/mach-mmp/flint.c
 *
 *  Support for the Marvell Flint Development Platform.
 *
 *  Copyright (C) 2009 Marvell International Ltd.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  publishhed by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/smc91x.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <mach/addr-map.h>
#include <mach/mfp-mmp2.h>
#include <mach/mmp2.h>
#include <mach/irqs.h>

#include "common.h"

#define FLINT_NR_IRQS	(MMP_NR_IRQS + 48)

static unsigned long flint_pin_config[] __initdata = {
	/* UART1 */
	GPIO45_UART1_RXD,
	GPIO46_UART1_TXD,

	/* UART2 */
	GPIO47_UART2_RXD,
	GPIO48_UART2_TXD,

	/* SMC */
	GPIO151_SMC_SCLK,
	GPIO145_SMC_nCS0,
	GPIO146_SMC_nCS1,
	GPIO152_SMC_BE0,
	GPIO153_SMC_BE1,
	GPIO154_SMC_IRQ,
	GPIO113_SMC_RDY,

	/*Ethernet*/
	GPIO155_GPIO,

	/* DFI */
	GPIO168_DFI_D0,
	GPIO167_DFI_D1,
	GPIO166_DFI_D2,
	GPIO165_DFI_D3,
	GPIO107_DFI_D4,
	GPIO106_DFI_D5,
	GPIO105_DFI_D6,
	GPIO104_DFI_D7,
	GPIO111_DFI_D8,
	GPIO164_DFI_D9,
	GPIO163_DFI_D10,
	GPIO162_DFI_D11,
	GPIO161_DFI_D12,
	GPIO110_DFI_D13,
	GPIO109_DFI_D14,
	GPIO108_DFI_D15,
	GPIO143_ND_nCS0,
	GPIO144_ND_nCS1,
	GPIO147_ND_nWE,
	GPIO148_ND_nRE,
	GPIO150_ND_ALE,
	GPIO149_ND_CLE,
	GPIO112_ND_RDY0,
	GPIO160_ND_RDY1,
};

static struct smc91x_platdata flint_smc91x_info = {
	.flags  = SMC91X_USE_16BIT | SMC91X_NOWAIT,
};

static struct resource smc91x_resources[] = {
	[0] = {
		.start  = SMC_CS1_PHYS_BASE + 0x300,
		.end    = SMC_CS1_PHYS_BASE + 0xfffff,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = MMP_GPIO_TO_IRQ(155),
		.end    = MMP_GPIO_TO_IRQ(155),
		.flags  = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHEDGE,
	}
};

static struct platform_device smc91x_device = {
	.name           = "smc91x",
	.id             = 0,
	.dev            = {
		.platform_data = &flint_smc91x_info,
	},
	.num_resources  = ARRAY_SIZE(smc91x_resources),
	.resource       = smc91x_resources,
};

static void __init flint_init(void)
{
	mfp_config(ARRAY_AND_SIZE(flint_pin_config));

	/* on-chip devices */
	mmp2_add_uart(1);
	mmp2_add_uart(2);
	platform_device_register(&mmp2_device_gpio);

	/* off-chip devices */
	platform_device_register(&smc91x_device);
}

MACHINE_START(FLINT, "Flint Development Platform")
	.map_io		= mmp_map_io,
	.nr_irqs	= FLINT_NR_IRQS,
	.init_irq       = mmp2_init_irq,
	.init_time	= mmp2_timer_init,
	.init_machine   = flint_init,
	.restart	= mmp_restart,
MACHINE_END
