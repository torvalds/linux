/*
 *  linux/arch/arm/mach-pxa/colibri-pxa270.c
 *
 *  Support for Toradex PXA270 based Colibri module
 *  Daniel Mack <daniel@caiaq.de>
 *  Marek Vasut <marek.vasut@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>
#include <linux/platform_device.h>
#include <linux/sysdev.h>
#include <linux/ucb1400.h>

#include <asm/mach/arch.h>
#include <asm/mach/flash.h>
#include <asm/mach-types.h>
#include <asm/sizes.h>

#include <mach/audio.h>
#include <mach/colibri.h>
#include <mach/pxa27x.h>

#include "devices.h"
#include "generic.h"

/******************************************************************************
 * Pin configuration
 ******************************************************************************/
static mfp_cfg_t colibri_pxa270_pin_config[] __initdata = {
	/* Ethernet */
	GPIO78_nCS_2,	/* Ethernet CS */
	GPIO114_GPIO,	/* Ethernet IRQ */

	/* AC97 */
	GPIO28_AC97_BITCLK,
	GPIO29_AC97_SDATA_IN_0,
	GPIO30_AC97_SDATA_OUT,
	GPIO31_AC97_SYNC,
	GPIO95_AC97_nRESET,
	GPIO98_AC97_SYSCLK,
	GPIO113_GPIO,	/* Touchscreen IRQ */
};

/******************************************************************************
 * NOR Flash
 ******************************************************************************/
#if defined(CONFIG_MTD_PHYSMAP) || defined(CONFIG_MTD_PHYSMAP_MODULE)
static struct mtd_partition colibri_partitions[] = {
	{
		.name =		"Bootloader",
		.offset =	0x00000000,
		.size =		0x00040000,
		.mask_flags =	MTD_WRITEABLE	/* force read-only */
	}, {
		.name =		"Kernel",
		.offset =	0x00040000,
		.size =		0x00400000,
		.mask_flags =	0
	}, {
		.name =		"Rootfs",
		.offset =	0x00440000,
		.size =		MTDPART_SIZ_FULL,
		.mask_flags =	0
	}
};

static struct physmap_flash_data colibri_flash_data[] = {
	{
		.width		= 4,			/* bankwidth in bytes */
		.parts		= colibri_partitions,
		.nr_parts	= ARRAY_SIZE(colibri_partitions)
	}
};

static struct resource colibri_pxa270_flash_resource = {
	.start	= PXA_CS0_PHYS,
	.end	= PXA_CS0_PHYS + SZ_32M - 1,
	.flags	= IORESOURCE_MEM,
};

static struct platform_device colibri_pxa270_flash_device = {
	.name	= "physmap-flash",
	.id	= 0,
	.dev 	= {
		.platform_data = colibri_flash_data,
	},
	.resource = &colibri_pxa270_flash_resource,
	.num_resources = 1,
};

static void __init colibri_pxa270_nor_init(void)
{
	platform_device_register(&colibri_pxa270_flash_device);
}
#else
static inline void colibri_pxa270_nor_init(void) {}
#endif

/******************************************************************************
 * Ethernet
 ******************************************************************************/
#if defined(CONFIG_DM9000) || defined(CONFIG_DM9000_MODULE)
static struct resource colibri_pxa270_dm9000_resources[] = {
	{
		.start	= PXA_CS2_PHYS,
		.end	= PXA_CS2_PHYS + 3,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= PXA_CS2_PHYS + 4,
		.end	= PXA_CS2_PHYS + 4 + 500,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= gpio_to_irq(GPIO114_COLIBRI_PXA270_ETH_IRQ),
		.end	= gpio_to_irq(GPIO114_COLIBRI_PXA270_ETH_IRQ),
		.flags	= IORESOURCE_IRQ | IRQF_TRIGGER_RISING,
	},
};

static struct platform_device colibri_pxa270_dm9000_device = {
	.name		= "dm9000",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(colibri_pxa270_dm9000_resources),
	.resource	= colibri_pxa270_dm9000_resources,
};

static void __init colibri_pxa270_eth_init(void)
{
	platform_device_register(&colibri_pxa270_dm9000_device);
}
#else
static inline void colibri_pxa270_eth_init(void) {}
#endif

/******************************************************************************
 * Audio and Touchscreen
 ******************************************************************************/
#if	defined(CONFIG_TOUCHSCREEN_UCB1400) || \
	defined(CONFIG_TOUCHSCREEN_UCB1400_MODULE)
static pxa2xx_audio_ops_t colibri_pxa270_ac97_pdata = {
	.reset_gpio	= 95,
};

static struct ucb1400_pdata colibri_pxa270_ucb1400_pdata = {
	.irq		= gpio_to_irq(GPIO113_COLIBRI_PXA270_TS_IRQ),
};

static struct platform_device colibri_pxa270_ucb1400_device = {
	.name		= "ucb1400_core",
	.id		= -1,
	.dev		= {
		.platform_data = &colibri_pxa270_ucb1400_pdata,
	},
};

static void __init colibri_pxa270_tsc_init(void)
{
	pxa_set_ac97_info(&colibri_pxa270_ac97_pdata);
	platform_device_register(&colibri_pxa270_ucb1400_device);
}
#else
static inline void colibri_pxa270_tsc_init(void) {}
#endif

static int colibri_pxa270_baseboard;
core_param(colibri_pxa270_baseboard, colibri_pxa270_baseboard, int, 0444);

static void __init colibri_pxa270_init(void)
{
	pxa2xx_mfp_config(ARRAY_AND_SIZE(colibri_pxa270_pin_config));

	colibri_pxa270_nor_init();
	colibri_pxa270_eth_init();
	colibri_pxa270_tsc_init();

	switch (colibri_pxa270_baseboard) {
	case COLIBRI_PXA270_EVALBOARD:
		colibri_pxa270_evalboard_init();
		break;
	case COLIBRI_PXA270_INCOME:
		colibri_pxa270_income_boardinit();
		break;
	default:
		printk(KERN_ERR "Illegal colibri_pxa270_baseboard type %d\n",
				colibri_pxa270_baseboard);
	}
}

/* The "Income s.r.o. SH-Dmaster PXA270 SBC" board can be booted either
 * with the INCOME mach type or with COLIBRI and the kernel parameter
 * "colibri_pxa270_baseboard=1"
 */
static void __init colibri_pxa270_income_init(void)
{
	colibri_pxa270_baseboard = COLIBRI_PXA270_INCOME;
	colibri_pxa270_init();
}

MACHINE_START(COLIBRI, "Toradex Colibri PXA270")
	.phys_io	= 0x40000000,
	.io_pg_offst	= (io_p2v(0x40000000) >> 18) & 0xfffc,
	.boot_params	= COLIBRI_SDRAM_BASE + 0x100,
	.init_machine	= colibri_pxa270_init,
	.map_io		= pxa_map_io,
	.init_irq	= pxa27x_init_irq,
	.timer		= &pxa_timer,
MACHINE_END

MACHINE_START(INCOME, "Income s.r.o. SH-Dmaster PXA270 SBC")
	.phys_io	= 0x40000000,
	.io_pg_offst	= (io_p2v(0x40000000) >> 18) & 0xfffc,
	.boot_params	= 0xa0000100,
	.init_machine	= colibri_pxa270_income_init,
	.map_io		= pxa_map_io,
	.init_irq	= pxa27x_init_irq,
	.timer		= &pxa_timer,
MACHINE_END

