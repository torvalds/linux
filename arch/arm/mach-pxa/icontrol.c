/*
 * linux/arch/arm/mach-pxa/icontrol.c
 *
 * Support for the iControl and SafeTcam platforms from TMT Services
 * using the Embedian MXM-8x10 Computer on Module
 *
 * Copyright (C) 2009 TMT Services & Supplies (Pty) Ltd.
 *
 * 2010-01-21 Hennie van der Merve <hvdmerwe@tmtservies.co.za>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include <mach/pxa320.h>
#include <mach/mxm8x10.h>

#include <linux/spi/spi.h>
#include <linux/spi/pxa2xx_spi.h>
#include <linux/can/platform/mcp251x.h>

#include "generic.h"

#define ICONTROL_MCP251x_nCS1	(15)
#define ICONTROL_MCP251x_nCS2	(16)
#define ICONTROL_MCP251x_nCS3	(17)
#define ICONTROL_MCP251x_nCS4	(24)

#define ICONTROL_MCP251x_nIRQ1	(74)
#define ICONTROL_MCP251x_nIRQ2	(75)
#define ICONTROL_MCP251x_nIRQ3	(76)
#define ICONTROL_MCP251x_nIRQ4	(77)

static struct pxa2xx_spi_chip mcp251x_chip_info1 = {
	.tx_threshold   = 8,
	.rx_threshold   = 128,
	.dma_burst_size = 8,
	.timeout        = 235,
	.gpio_cs        = ICONTROL_MCP251x_nCS1
};

static struct pxa2xx_spi_chip mcp251x_chip_info2 = {
	.tx_threshold   = 8,
	.rx_threshold   = 128,
	.dma_burst_size = 8,
	.timeout        = 235,
	.gpio_cs        = ICONTROL_MCP251x_nCS2
};

static struct pxa2xx_spi_chip mcp251x_chip_info3 = {
	.tx_threshold   = 8,
	.rx_threshold   = 128,
	.dma_burst_size = 8,
	.timeout        = 235,
	.gpio_cs        = ICONTROL_MCP251x_nCS3
};

static struct pxa2xx_spi_chip mcp251x_chip_info4 = {
	.tx_threshold   = 8,
	.rx_threshold   = 128,
	.dma_burst_size = 8,
	.timeout        = 235,
	.gpio_cs        = ICONTROL_MCP251x_nCS4
};

static struct mcp251x_platform_data mcp251x_info = {
	.oscillator_frequency = 16E6,
};

static struct spi_board_info mcp251x_board_info[] = {
	{
		.modalias        = "mcp2515",
		.max_speed_hz    = 6500000,
		.bus_num         = 3,
		.chip_select     = 0,
		.platform_data   = &mcp251x_info,
		.controller_data = &mcp251x_chip_info1,
		.irq             = PXA_GPIO_TO_IRQ(ICONTROL_MCP251x_nIRQ1)
	},
	{
		.modalias        = "mcp2515",
		.max_speed_hz    = 6500000,
		.bus_num         = 3,
		.chip_select     = 1,
		.platform_data   = &mcp251x_info,
		.controller_data = &mcp251x_chip_info2,
		.irq             = PXA_GPIO_TO_IRQ(ICONTROL_MCP251x_nIRQ2)
	},
	{
		.modalias        = "mcp2515",
		.max_speed_hz    = 6500000,
		.bus_num         = 4,
		.chip_select     = 0,
		.platform_data   = &mcp251x_info,
		.controller_data = &mcp251x_chip_info3,
		.irq             = PXA_GPIO_TO_IRQ(ICONTROL_MCP251x_nIRQ3)
	},
	{
		.modalias        = "mcp2515",
		.max_speed_hz    = 6500000,
		.bus_num         = 4,
		.chip_select     = 1,
		.platform_data   = &mcp251x_info,
		.controller_data = &mcp251x_chip_info4,
		.irq             = PXA_GPIO_TO_IRQ(ICONTROL_MCP251x_nIRQ4)
	}
};

static struct pxa2xx_spi_master pxa_ssp3_spi_master_info = {
	.clock_enable   = CKEN_SSP3,
	.num_chipselect = 2,
	.enable_dma     = 1
};

static struct pxa2xx_spi_master pxa_ssp4_spi_master_info = {
	.clock_enable   = CKEN_SSP4,
	.num_chipselect = 2,
	.enable_dma     = 1
};

struct platform_device pxa_spi_ssp3 = {
	.name          = "pxa2xx-spi",
	.id            = 3,
	.dev           = {
		.platform_data = &pxa_ssp3_spi_master_info,
	}
};

struct platform_device pxa_spi_ssp4 = {
	.name          = "pxa2xx-spi",
	.id            = 4,
	.dev           = {
		.platform_data = &pxa_ssp4_spi_master_info,
	}
};

static struct platform_device *icontrol_spi_devices[] __initdata = {
	&pxa_spi_ssp3,
	&pxa_spi_ssp4,
};

static mfp_cfg_t mfp_can_cfg[] __initdata = {
	/* CAN CS lines */
	GPIO15_GPIO,
	GPIO16_GPIO,
	GPIO17_GPIO,
	GPIO24_GPIO,

	/* SPI (SSP3) lines */
	GPIO89_SSP3_SCLK,
	GPIO91_SSP3_TXD,
	GPIO92_SSP3_RXD,

	/* SPI (SSP4) lines */
	GPIO93_SSP4_SCLK,
	GPIO95_SSP4_TXD,
	GPIO96_SSP4_RXD,

	/* CAN nIRQ lines */
	GPIO74_GPIO | MFP_LPM_EDGE_RISE,
	GPIO75_GPIO | MFP_LPM_EDGE_RISE,
	GPIO76_GPIO | MFP_LPM_EDGE_RISE,
	GPIO77_GPIO | MFP_LPM_EDGE_RISE
};

static void __init icontrol_can_init(void)
{
	pxa3xx_mfp_config(ARRAY_AND_SIZE(mfp_can_cfg));
	platform_add_devices(ARRAY_AND_SIZE(icontrol_spi_devices));
	spi_register_board_info(ARRAY_AND_SIZE(mcp251x_board_info));
}

static void __init icontrol_init(void)
{
	mxm_8x10_barebones_init();
	mxm_8x10_usb_host_init();
	mxm_8x10_mmc_init();

	icontrol_can_init();
}

MACHINE_START(ICONTROL, "iControl/SafeTcam boards using Embedian MXM-8x10 CoM")
	.atag_offset	= 0x100,
	.map_io		= pxa3xx_map_io,
	.nr_irqs	= PXA_NR_IRQS,
	.init_irq	= pxa3xx_init_irq,
	.handle_irq	= pxa3xx_handle_irq,
	.init_time	= pxa_timer_init,
	.init_machine	= icontrol_init,
	.restart	= pxa_restart,
MACHINE_END
