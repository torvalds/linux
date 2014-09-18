/*
 *  linux/arch/arm/mach-mmp/tavorevb.c
 *
 *  Support for the Marvell PXA910-based TavorEVB Development Platform.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  publishhed by the Free Software Foundation.
 */
#include <linux/gpio.h>
#include <linux/gpio-pxa.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/smc91x.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <mach/addr-map.h>
#include <mach/mfp-pxa910.h>
#include <mach/pxa910.h>
#include <mach/irqs.h>

#include "common.h"

static unsigned long tavorevb_pin_config[] __initdata = {
	/* UART2 */
	GPIO47_UART2_RXD,
	GPIO48_UART2_TXD,

	/* SMC */
	SM_nCS0_nCS0,
	SM_ADV_SM_ADV,
	SM_SCLK_SM_SCLK,
	SM_SCLK_SM_SCLK,
	SM_BE0_SM_BE0,
	SM_BE1_SM_BE1,

	/* DFI */
	DF_IO0_ND_IO0,
	DF_IO1_ND_IO1,
	DF_IO2_ND_IO2,
	DF_IO3_ND_IO3,
	DF_IO4_ND_IO4,
	DF_IO5_ND_IO5,
	DF_IO6_ND_IO6,
	DF_IO7_ND_IO7,
	DF_IO8_ND_IO8,
	DF_IO9_ND_IO9,
	DF_IO10_ND_IO10,
	DF_IO11_ND_IO11,
	DF_IO12_ND_IO12,
	DF_IO13_ND_IO13,
	DF_IO14_ND_IO14,
	DF_IO15_ND_IO15,
	DF_nCS0_SM_nCS2_nCS0,
	DF_ALE_SM_WEn_ND_ALE,
	DF_CLE_SM_OEn_ND_CLE,
	DF_WEn_DF_WEn,
	DF_REn_DF_REn,
	DF_RDY0_DF_RDY0,
};

static struct pxa_gpio_platform_data pxa910_gpio_pdata = {
	.irq_base	= MMP_GPIO_TO_IRQ(0),
};

static struct smc91x_platdata tavorevb_smc91x_info = {
	.flags	= SMC91X_USE_16BIT | SMC91X_NOWAIT,
};

static struct resource smc91x_resources[] = {
	[0] = {
		.start	= SMC_CS1_PHYS_BASE + 0x300,
		.end	= SMC_CS1_PHYS_BASE + 0xfffff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= MMP_GPIO_TO_IRQ(80),
		.end	= MMP_GPIO_TO_IRQ(80),
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHEDGE,
	}
};

static struct platform_device smc91x_device = {
	.name		= "smc91x",
	.id		= 0,
	.dev		= {
		.platform_data = &tavorevb_smc91x_info,
	},
	.num_resources	= ARRAY_SIZE(smc91x_resources),
	.resource	= smc91x_resources,
};

static void __init tavorevb_init(void)
{
	mfp_config(ARRAY_AND_SIZE(tavorevb_pin_config));

	/* on-chip devices */
	pxa910_add_uart(1);
	platform_device_add_data(&pxa910_device_gpio, &pxa910_gpio_pdata,
				 sizeof(struct pxa_gpio_platform_data));
	platform_device_register(&pxa910_device_gpio);

	/* off-chip devices */
	platform_device_register(&smc91x_device);
}

MACHINE_START(TAVOREVB, "PXA910 Evaluation Board (aka TavorEVB)")
	.map_io		= mmp_map_io,
	.nr_irqs	= MMP_NR_IRQS,
	.init_irq       = pxa910_init_irq,
	.init_time	= pxa910_timer_init,
	.init_machine   = tavorevb_init,
	.restart	= mmp_restart,
MACHINE_END
