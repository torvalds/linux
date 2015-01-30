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
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>
#include <linux/platform_device.h>
#include <linux/regulator/machine.h>
#include <linux/ucb1400.h>

#include <asm/mach/arch.h>
#include <asm/mach/flash.h>
#include <asm/mach-types.h>
#include <asm/sizes.h>

#include <mach/audio.h>
#include "colibri.h"
#include "pxa27x.h"

#include "devices.h"
#include "generic.h"

/******************************************************************************
 * Evaluation board MFP
 ******************************************************************************/
#ifdef	 CONFIG_MACH_COLIBRI_EVALBOARD
static mfp_cfg_t colibri_pxa270_evalboard_pin_config[] __initdata = {
	/* MMC */
	GPIO32_MMC_CLK,
	GPIO92_MMC_DAT_0,
	GPIO109_MMC_DAT_1,
	GPIO110_MMC_DAT_2,
	GPIO111_MMC_DAT_3,
	GPIO112_MMC_CMD,
	GPIO0_GPIO,	/* SD detect */

	/* FFUART */
	GPIO39_FFUART_TXD,
	GPIO34_FFUART_RXD,

	/* UHC */
	GPIO88_USBH1_PWR,
	GPIO89_USBH1_PEN,
	GPIO119_USBH2_PWR,
	GPIO120_USBH2_PEN,

	/* PCMCIA */
	GPIO85_nPCE_1,
	GPIO54_nPCE_2,
	GPIO55_nPREG,
	GPIO50_nPIOR,
	GPIO51_nPIOW,
	GPIO49_nPWE,
	GPIO48_nPOE,
	GPIO57_nIOIS16,
	GPIO56_nPWAIT,
	GPIO104_PSKTSEL,
	GPIO53_GPIO,	/* RESET */
	GPIO83_GPIO,	/* BVD1 */
	GPIO82_GPIO,	/* BVD2 */
	GPIO1_GPIO,	/* READY */
	GPIO84_GPIO,	/* DETECT */
	GPIO107_GPIO,	/* PPEN */

	/* I2C */
	GPIO117_I2C_SCL,
	GPIO118_I2C_SDA,
};
#else
static mfp_cfg_t colibri_pxa270_evalboard_pin_config[] __initdata = {};
#endif

#ifdef	CONFIG_MACH_COLIBRI_PXA270_INCOME
static mfp_cfg_t income_pin_config[] __initdata = {
	/* MMC */
	GPIO32_MMC_CLK,
	GPIO92_MMC_DAT_0,
	GPIO109_MMC_DAT_1,
	GPIO110_MMC_DAT_2,
	GPIO111_MMC_DAT_3,
	GPIO112_MMC_CMD,
	GPIO0_GPIO,	/* SD detect */
	GPIO1_GPIO,	/* SD read-only */

	/* FFUART */
	GPIO39_FFUART_TXD,
	GPIO34_FFUART_RXD,

	/* BFUART */
	GPIO42_BTUART_RXD,
	GPIO43_BTUART_TXD,
	GPIO45_BTUART_RTS,

	/* STUART */
	GPIO46_STUART_RXD,
	GPIO47_STUART_TXD,

	/* UHC */
	GPIO88_USBH1_PWR,
	GPIO89_USBH1_PEN,

	/* LCD */
	GPIOxx_LCD_TFT_16BPP,

	/* PWM */
	GPIO16_PWM0_OUT,

	/* I2C */
	GPIO117_I2C_SCL,
	GPIO118_I2C_SDA,

	/* LED */
	GPIO54_GPIO,	/* LED A */
	GPIO55_GPIO,	/* LED B */
};
#else
static mfp_cfg_t income_pin_config[] __initdata = {};
#endif

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
		.start	= PXA_GPIO_TO_IRQ(GPIO114_COLIBRI_PXA270_ETH_IRQ),
		.end	= PXA_GPIO_TO_IRQ(GPIO114_COLIBRI_PXA270_ETH_IRQ),
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
	.irq		= PXA_GPIO_TO_IRQ(GPIO113_COLIBRI_PXA270_TS_IRQ),
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
	case COLIBRI_EVALBOARD:
		pxa2xx_mfp_config(ARRAY_AND_SIZE(
			colibri_pxa270_evalboard_pin_config));
		colibri_evalboard_init();
		break;
	case COLIBRI_PXA270_INCOME:
		pxa2xx_mfp_config(ARRAY_AND_SIZE(income_pin_config));
		colibri_pxa270_income_boardinit();
		break;
	default:
		printk(KERN_ERR "Illegal colibri_pxa270_baseboard type %d\n",
				colibri_pxa270_baseboard);
	}

	regulator_has_full_constraints();
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
	.atag_offset	= 0x100,
	.init_machine	= colibri_pxa270_init,
	.map_io		= pxa27x_map_io,
	.nr_irqs	= PXA_NR_IRQS,
	.init_irq	= pxa27x_init_irq,
	.handle_irq	= pxa27x_handle_irq,
	.init_time	= pxa_timer_init,
	.restart	= pxa_restart,
MACHINE_END

MACHINE_START(INCOME, "Income s.r.o. SH-Dmaster PXA270 SBC")
	.atag_offset	= 0x100,
	.init_machine	= colibri_pxa270_income_init,
	.map_io		= pxa27x_map_io,
	.nr_irqs	= PXA_NR_IRQS,
	.init_irq	= pxa27x_init_irq,
	.handle_irq	= pxa27x_handle_irq,
	.init_time	= pxa_timer_init,
	.restart	= pxa_restart,
MACHINE_END

