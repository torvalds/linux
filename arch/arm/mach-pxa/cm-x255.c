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
#include <linux/gpio.h>

#include <linux/spi/spi.h>

#include <asm/mach/arch.h>
#include <asm/mach-types.h>
#include <asm/mach/map.h>

#include <mach/pxa2xx-regs.h>
#include <mach/mfp-pxa25x.h>
#include <mach/pxa2xx_spi.h>
#include <mach/bitfield.h>

#include "generic.h"

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
	GPIO58_LCD_LDD_0,
	GPIO59_LCD_LDD_1,
	GPIO60_LCD_LDD_2,
	GPIO61_LCD_LDD_3,
	GPIO62_LCD_LDD_4,
	GPIO63_LCD_LDD_5,
	GPIO64_LCD_LDD_6,
	GPIO65_LCD_LDD_7,
	GPIO66_LCD_LDD_8,
	GPIO67_LCD_LDD_9,
	GPIO68_LCD_LDD_10,
	GPIO69_LCD_LDD_11,
	GPIO70_LCD_LDD_12,
	GPIO71_LCD_LDD_13,
	GPIO72_LCD_LDD_14,
	GPIO73_LCD_LDD_15,
	GPIO74_LCD_FCLK,
	GPIO75_LCD_LCLK,
	GPIO76_LCD_PCLK,
	GPIO77_LCD_BIAS,

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

void __init cmx255_init(void)
{
	pxa2xx_mfp_config(ARRAY_AND_SIZE(cmx255_pin_config));

	cmx255_init_rtc();
}
