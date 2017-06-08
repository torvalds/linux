/*
 * linux/arch/arm/mach-pxa/cm-x255.c
 *
 * Copyright (C) 2007, 2008 CompuLab, Ltd.
 * Mike Rapoport <mike@compulab.co.il>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/platform_device.h>
#include <linux/irq.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>
#include <linux/mtd/nand-gpio.h>

#include <linux/spi/spi.h>
#include <linux/spi/pxa2xx_spi.h>

#include <asm/mach/arch.h>
#include <asm/mach-types.h>
#include <asm/mach/map.h>

#include "pxa25x.h"

#include "generic.h"

#define GPIO_NAND_CS	(5)
#define GPIO_NAND_ALE	(4)
#define GPIO_NAND_CLE	(3)
#define GPIO_NAND_RB	(10)

static unsigned long cmx255_pin_config[] = {
	/* AC'97 */
	GPIO28_AC97_BITCLK,
	GPIO29_AC97_SDATA_IN_0,
	GPIO30_AC97_SDATA_OUT,
	GPIO31_AC97_SYNC,

	/* BTUART */
	GPIO42_BTUART_RXD,
	GPIO43_BTUART_TXD,
	GPIO44_BTUART_CTS,
	GPIO45_BTUART_RTS,

	/* STUART */
	GPIO46_STUART_RXD,
	GPIO47_STUART_TXD,

	/* LCD */
	GPIOxx_LCD_TFT_16BPP,

	/* SSP1 */
	GPIO23_SSP1_SCLK,
	GPIO24_SSP1_SFRM,
	GPIO25_SSP1_TXD,
	GPIO26_SSP1_RXD,

	/* SSP2 */
	GPIO81_SSP2_CLK_OUT,
	GPIO82_SSP2_FRM_OUT,
	GPIO83_SSP2_TXD,
	GPIO84_SSP2_RXD,

	/* PC Card */
	GPIO48_nPOE,
	GPIO49_nPWE,
	GPIO50_nPIOR,
	GPIO51_nPIOW,
	GPIO52_nPCE_1,
	GPIO53_nPCE_2,
	GPIO54_nPSKTSEL,
	GPIO55_nPREG,
	GPIO56_nPWAIT,
	GPIO57_nIOIS16,

	/* SDRAM and local bus */
	GPIO15_nCS_1,
	GPIO78_nCS_2,
	GPIO79_nCS_3,
	GPIO80_nCS_4,
	GPIO33_nCS_5,
	GPIO18_RDY,

	/* GPIO */
	GPIO0_GPIO	| WAKEUP_ON_EDGE_BOTH,
	GPIO9_GPIO,				/* PC card reset */

	/* NAND controls */
	GPIO5_GPIO	| MFP_LPM_DRIVE_HIGH,	/* NAND CE# */
	GPIO4_GPIO	| MFP_LPM_DRIVE_LOW,	/* NAND ALE */
	GPIO3_GPIO	| MFP_LPM_DRIVE_LOW,	/* NAND CLE */
	GPIO10_GPIO,				/* NAND Ready/Busy */

	/* interrupts */
	GPIO22_GPIO,	/* DM9000 interrupt */
};

#if defined(CONFIG_SPI_PXA2XX)
static struct pxa2xx_spi_master pxa_ssp_master_info = {
	.num_chipselect	= 1,
};

static struct spi_board_info spi_board_info[] __initdata = {
	[0] = {
		.modalias	= "rtc-max6902",
		.max_speed_hz	= 1000000,
		.bus_num	= 1,
		.chip_select	= 0,
	},
};

static void __init cmx255_init_rtc(void)
{
	pxa2xx_set_spi_info(1, &pxa_ssp_master_info);
	spi_register_board_info(ARRAY_AND_SIZE(spi_board_info));
}
#else
static inline void cmx255_init_rtc(void) {}
#endif

#if defined(CONFIG_MTD_PHYSMAP) || defined(CONFIG_MTD_PHYSMAP_MODULE)
static struct mtd_partition cmx255_nor_partitions[] = {
	{
		.name		= "ARMmon",
		.size		= 0x00030000,
		.offset		= 0,
		.mask_flags	= MTD_WRITEABLE  /* force read-only */
	} , {
		.name		= "ARMmon setup block",
		.size		= 0x00010000,
		.offset		= MTDPART_OFS_APPEND,
		.mask_flags	= MTD_WRITEABLE  /* force read-only */
	} , {
		.name		= "kernel",
		.size		= 0x00160000,
		.offset		= MTDPART_OFS_APPEND,
	} , {
		.name		= "ramdisk",
		.size		= MTDPART_SIZ_FULL,
		.offset		= MTDPART_OFS_APPEND
	}
};

static struct physmap_flash_data cmx255_nor_flash_data[] = {
	{
		.width		= 2,	/* bankwidth in bytes */
		.parts		= cmx255_nor_partitions,
		.nr_parts	= ARRAY_SIZE(cmx255_nor_partitions)
	}
};

static struct resource cmx255_nor_resource = {
	.start	= PXA_CS0_PHYS,
	.end	= PXA_CS0_PHYS + SZ_8M - 1,
	.flags	= IORESOURCE_MEM,
};

static struct platform_device cmx255_nor = {
	.name	= "physmap-flash",
	.id	= -1,
	.dev	= {
		.platform_data = cmx255_nor_flash_data,
	},
	.resource = &cmx255_nor_resource,
	.num_resources = 1,
};

static void __init cmx255_init_nor(void)
{
	platform_device_register(&cmx255_nor);
}
#else
static inline void cmx255_init_nor(void) {}
#endif

#if defined(CONFIG_MTD_NAND_GPIO) || defined(CONFIG_MTD_NAND_GPIO_MODULE)
static struct resource cmx255_nand_resource[] = {
	[0] = {
		.start = PXA_CS1_PHYS,
		.end   = PXA_CS1_PHYS + 11,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = PXA_CS5_PHYS,
		.end   = PXA_CS5_PHYS + 3,
		.flags = IORESOURCE_MEM,
	},
};

static struct mtd_partition cmx255_nand_parts[] = {
	[0] = {
		.name	= "cmx255-nand",
		.size	= MTDPART_SIZ_FULL,
		.offset	= 0,
	},
};

static struct gpio_nand_platdata cmx255_nand_platdata = {
	.gpio_nce = GPIO_NAND_CS,
	.gpio_cle = GPIO_NAND_CLE,
	.gpio_ale = GPIO_NAND_ALE,
	.gpio_rdy = GPIO_NAND_RB,
	.gpio_nwp = -1,
	.parts = cmx255_nand_parts,
	.num_parts = ARRAY_SIZE(cmx255_nand_parts),
	.chip_delay = 25,
};

static struct platform_device cmx255_nand = {
	.name		= "gpio-nand",
	.num_resources	= ARRAY_SIZE(cmx255_nand_resource),
	.resource	= cmx255_nand_resource,
	.id		= -1,
	.dev		= {
		.platform_data = &cmx255_nand_platdata,
	}
};

static void __init cmx255_init_nand(void)
{
	platform_device_register(&cmx255_nand);
}
#else
static inline void cmx255_init_nand(void) {}
#endif

void __init cmx255_init(void)
{
	pxa2xx_mfp_config(ARRAY_AND_SIZE(cmx255_pin_config));

	cmx255_init_rtc();
	cmx255_init_nor();
	cmx255_init_nand();
}
