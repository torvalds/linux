// SPDX-License-Identifier: GPL-2.0-only
/*
 * TI DA850/OMAP-L138 chip specific setup
 *
 * Copyright (C) 2009 Texas Instruments Incorporated - https://www.ti.com/
 *
 * Derived from: arch/arm/mach-davinci/da830.c
 * Original Copyrights follow:
 *
 * 2009 (c) MontaVista Software, Inc.
 */

#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/mfd/da8xx-cfgchip.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <clocksource/timer-davinci.h>

#include <asm/mach/map.h>

#include "common.h"
#include "cputype.h"
#include "da8xx.h"
#include "hardware.h"
#include "pm.h"
#include "irqs.h"
#include "mux.h"

#define DA850_PLL1_BASE		0x01e1a000
#define DA850_TIMER64P2_BASE	0x01f0c000
#define DA850_TIMER64P3_BASE	0x01f0d000

#define DA850_REF_FREQ		24000000

/*
 * Device specific mux setup
 *
 *		soc	description	mux	mode	mode	mux	dbg
 *					reg	offset	mask	mode
 */
static const struct mux_config da850_pins[] = {
#ifdef CONFIG_DAVINCI_MUX
	/* UART0 function */
	MUX_CFG(DA850, NUART0_CTS,	3,	24,	15,	2,	false)
	MUX_CFG(DA850, NUART0_RTS,	3,	28,	15,	2,	false)
	MUX_CFG(DA850, UART0_RXD,	3,	16,	15,	2,	false)
	MUX_CFG(DA850, UART0_TXD,	3,	20,	15,	2,	false)
	/* UART1 function */
	MUX_CFG(DA850, UART1_RXD,	4,	24,	15,	2,	false)
	MUX_CFG(DA850, UART1_TXD,	4,	28,	15,	2,	false)
	/* UART2 function */
	MUX_CFG(DA850, UART2_RXD,	4,	16,	15,	2,	false)
	MUX_CFG(DA850, UART2_TXD,	4,	20,	15,	2,	false)
	/* I2C1 function */
	MUX_CFG(DA850, I2C1_SCL,	4,	16,	15,	4,	false)
	MUX_CFG(DA850, I2C1_SDA,	4,	20,	15,	4,	false)
	/* I2C0 function */
	MUX_CFG(DA850, I2C0_SDA,	4,	12,	15,	2,	false)
	MUX_CFG(DA850, I2C0_SCL,	4,	8,	15,	2,	false)
	/* EMAC function */
	MUX_CFG(DA850, MII_TXEN,	2,	4,	15,	8,	false)
	MUX_CFG(DA850, MII_TXCLK,	2,	8,	15,	8,	false)
	MUX_CFG(DA850, MII_COL,		2,	12,	15,	8,	false)
	MUX_CFG(DA850, MII_TXD_3,	2,	16,	15,	8,	false)
	MUX_CFG(DA850, MII_TXD_2,	2,	20,	15,	8,	false)
	MUX_CFG(DA850, MII_TXD_1,	2,	24,	15,	8,	false)
	MUX_CFG(DA850, MII_TXD_0,	2,	28,	15,	8,	false)
	MUX_CFG(DA850, MII_RXCLK,	3,	0,	15,	8,	false)
	MUX_CFG(DA850, MII_RXDV,	3,	4,	15,	8,	false)
	MUX_CFG(DA850, MII_RXER,	3,	8,	15,	8,	false)
	MUX_CFG(DA850, MII_CRS,		3,	12,	15,	8,	false)
	MUX_CFG(DA850, MII_RXD_3,	3,	16,	15,	8,	false)
	MUX_CFG(DA850, MII_RXD_2,	3,	20,	15,	8,	false)
	MUX_CFG(DA850, MII_RXD_1,	3,	24,	15,	8,	false)
	MUX_CFG(DA850, MII_RXD_0,	3,	28,	15,	8,	false)
	MUX_CFG(DA850, MDIO_CLK,	4,	0,	15,	8,	false)
	MUX_CFG(DA850, MDIO_D,		4,	4,	15,	8,	false)
	MUX_CFG(DA850, RMII_TXD_0,	14,	12,	15,	8,	false)
	MUX_CFG(DA850, RMII_TXD_1,	14,	8,	15,	8,	false)
	MUX_CFG(DA850, RMII_TXEN,	14,	16,	15,	8,	false)
	MUX_CFG(DA850, RMII_CRS_DV,	15,	4,	15,	8,	false)
	MUX_CFG(DA850, RMII_RXD_0,	14,	24,	15,	8,	false)
	MUX_CFG(DA850, RMII_RXD_1,	14,	20,	15,	8,	false)
	MUX_CFG(DA850, RMII_RXER,	14,	28,	15,	8,	false)
	MUX_CFG(DA850, RMII_MHZ_50_CLK,	15,	0,	15,	0,	false)
	/* McASP function */
	MUX_CFG(DA850,	ACLKR,		0,	0,	15,	1,	false)
	MUX_CFG(DA850,	ACLKX,		0,	4,	15,	1,	false)
	MUX_CFG(DA850,	AFSR,		0,	8,	15,	1,	false)
	MUX_CFG(DA850,	AFSX,		0,	12,	15,	1,	false)
	MUX_CFG(DA850,	AHCLKR,		0,	16,	15,	1,	false)
	MUX_CFG(DA850,	AHCLKX,		0,	20,	15,	1,	false)
	MUX_CFG(DA850,	AMUTE,		0,	24,	15,	1,	false)
	MUX_CFG(DA850,	AXR_15,		1,	0,	15,	1,	false)
	MUX_CFG(DA850,	AXR_14,		1,	4,	15,	1,	false)
	MUX_CFG(DA850,	AXR_13,		1,	8,	15,	1,	false)
	MUX_CFG(DA850,	AXR_12,		1,	12,	15,	1,	false)
	MUX_CFG(DA850,	AXR_11,		1,	16,	15,	1,	false)
	MUX_CFG(DA850,	AXR_10,		1,	20,	15,	1,	false)
	MUX_CFG(DA850,	AXR_9,		1,	24,	15,	1,	false)
	MUX_CFG(DA850,	AXR_8,		1,	28,	15,	1,	false)
	MUX_CFG(DA850,	AXR_7,		2,	0,	15,	1,	false)
	MUX_CFG(DA850,	AXR_6,		2,	4,	15,	1,	false)
	MUX_CFG(DA850,	AXR_5,		2,	8,	15,	1,	false)
	MUX_CFG(DA850,	AXR_4,		2,	12,	15,	1,	false)
	MUX_CFG(DA850,	AXR_3,		2,	16,	15,	1,	false)
	MUX_CFG(DA850,	AXR_2,		2,	20,	15,	1,	false)
	MUX_CFG(DA850,	AXR_1,		2,	24,	15,	1,	false)
	MUX_CFG(DA850,	AXR_0,		2,	28,	15,	1,	false)
	/* LCD function */
	MUX_CFG(DA850, LCD_D_7,		16,	8,	15,	2,	false)
	MUX_CFG(DA850, LCD_D_6,		16,	12,	15,	2,	false)
	MUX_CFG(DA850, LCD_D_5,		16,	16,	15,	2,	false)
	MUX_CFG(DA850, LCD_D_4,		16,	20,	15,	2,	false)
	MUX_CFG(DA850, LCD_D_3,		16,	24,	15,	2,	false)
	MUX_CFG(DA850, LCD_D_2,		16,	28,	15,	2,	false)
	MUX_CFG(DA850, LCD_D_1,		17,	0,	15,	2,	false)
	MUX_CFG(DA850, LCD_D_0,		17,	4,	15,	2,	false)
	MUX_CFG(DA850, LCD_D_15,	17,	8,	15,	2,	false)
	MUX_CFG(DA850, LCD_D_14,	17,	12,	15,	2,	false)
	MUX_CFG(DA850, LCD_D_13,	17,	16,	15,	2,	false)
	MUX_CFG(DA850, LCD_D_12,	17,	20,	15,	2,	false)
	MUX_CFG(DA850, LCD_D_11,	17,	24,	15,	2,	false)
	MUX_CFG(DA850, LCD_D_10,	17,	28,	15,	2,	false)
	MUX_CFG(DA850, LCD_D_9,		18,	0,	15,	2,	false)
	MUX_CFG(DA850, LCD_D_8,		18,	4,	15,	2,	false)
	MUX_CFG(DA850, LCD_PCLK,	18,	24,	15,	2,	false)
	MUX_CFG(DA850, LCD_HSYNC,	19,	0,	15,	2,	false)
	MUX_CFG(DA850, LCD_VSYNC,	19,	4,	15,	2,	false)
	MUX_CFG(DA850, NLCD_AC_ENB_CS,	19,	24,	15,	2,	false)
	/* MMC/SD0 function */
	MUX_CFG(DA850, MMCSD0_DAT_0,	10,	8,	15,	2,	false)
	MUX_CFG(DA850, MMCSD0_DAT_1,	10,	12,	15,	2,	false)
	MUX_CFG(DA850, MMCSD0_DAT_2,	10,	16,	15,	2,	false)
	MUX_CFG(DA850, MMCSD0_DAT_3,	10,	20,	15,	2,	false)
	MUX_CFG(DA850, MMCSD0_CLK,	10,	0,	15,	2,	false)
	MUX_CFG(DA850, MMCSD0_CMD,	10,	4,	15,	2,	false)
	/* MMC/SD1 function */
	MUX_CFG(DA850, MMCSD1_DAT_0,	18,	8,	15,	2,	false)
	MUX_CFG(DA850, MMCSD1_DAT_1,	19,	16,	15,	2,	false)
	MUX_CFG(DA850, MMCSD1_DAT_2,	19,	12,	15,	2,	false)
	MUX_CFG(DA850, MMCSD1_DAT_3,	19,	8,	15,	2,	false)
	MUX_CFG(DA850, MMCSD1_CLK,	18,	12,	15,	2,	false)
	MUX_CFG(DA850, MMCSD1_CMD,	18,	16,	15,	2,	false)
	/* EMIF2.5/EMIFA function */
	MUX_CFG(DA850, EMA_D_7,		9,	0,	15,	1,	false)
	MUX_CFG(DA850, EMA_D_6,		9,	4,	15,	1,	false)
	MUX_CFG(DA850, EMA_D_5,		9,	8,	15,	1,	false)
	MUX_CFG(DA850, EMA_D_4,		9,	12,	15,	1,	false)
	MUX_CFG(DA850, EMA_D_3,		9,	16,	15,	1,	false)
	MUX_CFG(DA850, EMA_D_2,		9,	20,	15,	1,	false)
	MUX_CFG(DA850, EMA_D_1,		9,	24,	15,	1,	false)
	MUX_CFG(DA850, EMA_D_0,		9,	28,	15,	1,	false)
	MUX_CFG(DA850, EMA_A_1,		12,	24,	15,	1,	false)
	MUX_CFG(DA850, EMA_A_2,		12,	20,	15,	1,	false)
	MUX_CFG(DA850, NEMA_CS_3,	7,	4,	15,	1,	false)
	MUX_CFG(DA850, NEMA_CS_4,	7,	8,	15,	1,	false)
	MUX_CFG(DA850, NEMA_WE,		7,	16,	15,	1,	false)
	MUX_CFG(DA850, NEMA_OE,		7,	20,	15,	1,	false)
	MUX_CFG(DA850, EMA_A_0,		12,	28,	15,	1,	false)
	MUX_CFG(DA850, EMA_A_3,		12,	16,	15,	1,	false)
	MUX_CFG(DA850, EMA_A_4,		12,	12,	15,	1,	false)
	MUX_CFG(DA850, EMA_A_5,		12,	8,	15,	1,	false)
	MUX_CFG(DA850, EMA_A_6,		12,	4,	15,	1,	false)
	MUX_CFG(DA850, EMA_A_7,		12,	0,	15,	1,	false)
	MUX_CFG(DA850, EMA_A_8,		11,	28,	15,	1,	false)
	MUX_CFG(DA850, EMA_A_9,		11,	24,	15,	1,	false)
	MUX_CFG(DA850, EMA_A_10,	11,	20,	15,	1,	false)
	MUX_CFG(DA850, EMA_A_11,	11,	16,	15,	1,	false)
	MUX_CFG(DA850, EMA_A_12,	11,	12,	15,	1,	false)
	MUX_CFG(DA850, EMA_A_13,	11,	8,	15,	1,	false)
	MUX_CFG(DA850, EMA_A_14,	11,	4,	15,	1,	false)
	MUX_CFG(DA850, EMA_A_15,	11,	0,	15,	1,	false)
	MUX_CFG(DA850, EMA_A_16,	10,	28,	15,	1,	false)
	MUX_CFG(DA850, EMA_A_17,	10,	24,	15,	1,	false)
	MUX_CFG(DA850, EMA_A_18,	10,	20,	15,	1,	false)
	MUX_CFG(DA850, EMA_A_19,	10,	16,	15,	1,	false)
	MUX_CFG(DA850, EMA_A_20,	10,	12,	15,	1,	false)
	MUX_CFG(DA850, EMA_A_21,	10,	8,	15,	1,	false)
	MUX_CFG(DA850, EMA_A_22,	10,	4,	15,	1,	false)
	MUX_CFG(DA850, EMA_A_23,	10,	0,	15,	1,	false)
	MUX_CFG(DA850, EMA_D_8,		8,	28,	15,	1,	false)
	MUX_CFG(DA850, EMA_D_9,		8,	24,	15,	1,	false)
	MUX_CFG(DA850, EMA_D_10,	8,	20,	15,	1,	false)
	MUX_CFG(DA850, EMA_D_11,	8,	16,	15,	1,	false)
	MUX_CFG(DA850, EMA_D_12,	8,	12,	15,	1,	false)
	MUX_CFG(DA850, EMA_D_13,	8,	8,	15,	1,	false)
	MUX_CFG(DA850, EMA_D_14,	8,	4,	15,	1,	false)
	MUX_CFG(DA850, EMA_D_15,	8,	0,	15,	1,	false)
	MUX_CFG(DA850, EMA_BA_1,	5,	24,	15,	1,	false)
	MUX_CFG(DA850, EMA_CLK,		6,	0,	15,	1,	false)
	MUX_CFG(DA850, EMA_WAIT_1,	6,	24,	15,	1,	false)
	MUX_CFG(DA850, NEMA_CS_2,	7,	0,	15,	1,	false)
	/* GPIO function */
	MUX_CFG(DA850, GPIO2_4,		6,	12,	15,	8,	false)
	MUX_CFG(DA850, GPIO2_6,		6,	4,	15,	8,	false)
	MUX_CFG(DA850, GPIO2_8,		5,	28,	15,	8,	false)
	MUX_CFG(DA850, GPIO2_15,	5,	0,	15,	8,	false)
	MUX_CFG(DA850, GPIO3_12,	7,	12,	15,	8,	false)
	MUX_CFG(DA850, GPIO3_13,	7,	8,	15,	8,	false)
	MUX_CFG(DA850, GPIO4_0,		10,	28,	15,	8,	false)
	MUX_CFG(DA850, GPIO4_1,		10,	24,	15,	8,	false)
	MUX_CFG(DA850, GPIO6_9,		13,	24,	15,	8,	false)
	MUX_CFG(DA850, GPIO6_10,	13,	20,	15,	8,	false)
	MUX_CFG(DA850, GPIO6_13,	13,	8,	15,	8,	false)
	MUX_CFG(DA850, RTC_ALARM,	0,	28,	15,	2,	false)
	/* VPIF Capture */
	MUX_CFG(DA850, VPIF_DIN0,	15,	4,	15,	1,	false)
	MUX_CFG(DA850, VPIF_DIN1,	15,	0,	15,	1,	false)
	MUX_CFG(DA850, VPIF_DIN2,	14,	28,	15,	1,	false)
	MUX_CFG(DA850, VPIF_DIN3,	14,	24,	15,	1,	false)
	MUX_CFG(DA850, VPIF_DIN4,	14,	20,	15,	1,	false)
	MUX_CFG(DA850, VPIF_DIN5,	14,	16,	15,	1,	false)
	MUX_CFG(DA850, VPIF_DIN6,	14,	12,	15,	1,	false)
	MUX_CFG(DA850, VPIF_DIN7,	14,	8,	15,	1,	false)
	MUX_CFG(DA850, VPIF_DIN8,	16,	4,	15,	1,	false)
	MUX_CFG(DA850, VPIF_DIN9,	16,	0,	15,	1,	false)
	MUX_CFG(DA850, VPIF_DIN10,	15,	28,	15,	1,	false)
	MUX_CFG(DA850, VPIF_DIN11,	15,	24,	15,	1,	false)
	MUX_CFG(DA850, VPIF_DIN12,	15,	20,	15,	1,	false)
	MUX_CFG(DA850, VPIF_DIN13,	15,	16,	15,	1,	false)
	MUX_CFG(DA850, VPIF_DIN14,	15,	12,	15,	1,	false)
	MUX_CFG(DA850, VPIF_DIN15,	15,	8,	15,	1,	false)
	MUX_CFG(DA850, VPIF_CLKIN0,	14,	0,	15,	1,	false)
	MUX_CFG(DA850, VPIF_CLKIN1,	14,	4,	15,	1,	false)
	MUX_CFG(DA850, VPIF_CLKIN2,	19,	8,	15,	1,	false)
	MUX_CFG(DA850, VPIF_CLKIN3,	19,	16,	15,	1,	false)
	/* VPIF Display */
	MUX_CFG(DA850, VPIF_DOUT0,	17,	4,	15,	1,	false)
	MUX_CFG(DA850, VPIF_DOUT1,	17,	0,	15,	1,	false)
	MUX_CFG(DA850, VPIF_DOUT2,	16,	28,	15,	1,	false)
	MUX_CFG(DA850, VPIF_DOUT3,	16,	24,	15,	1,	false)
	MUX_CFG(DA850, VPIF_DOUT4,	16,	20,	15,	1,	false)
	MUX_CFG(DA850, VPIF_DOUT5,	16,	16,	15,	1,	false)
	MUX_CFG(DA850, VPIF_DOUT6,	16,	12,	15,	1,	false)
	MUX_CFG(DA850, VPIF_DOUT7,	16,	8,	15,	1,	false)
	MUX_CFG(DA850, VPIF_DOUT8,	18,	4,	15,	1,	false)
	MUX_CFG(DA850, VPIF_DOUT9,	18,	0,	15,	1,	false)
	MUX_CFG(DA850, VPIF_DOUT10,	17,	28,	15,	1,	false)
	MUX_CFG(DA850, VPIF_DOUT11,	17,	24,	15,	1,	false)
	MUX_CFG(DA850, VPIF_DOUT12,	17,	20,	15,	1,	false)
	MUX_CFG(DA850, VPIF_DOUT13,	17,	16,	15,	1,	false)
	MUX_CFG(DA850, VPIF_DOUT14,	17,	12,	15,	1,	false)
	MUX_CFG(DA850, VPIF_DOUT15,	17,	8,	15,	1,	false)
	MUX_CFG(DA850, VPIF_CLKO2,	19,	12,	15,	1,	false)
	MUX_CFG(DA850, VPIF_CLKO3,	19,	20,	15,	1,	false)
#endif
};

static struct map_desc da850_io_desc[] = {
	{
		.virtual	= IO_VIRT,
		.pfn		= __phys_to_pfn(IO_PHYS),
		.length		= IO_SIZE,
		.type		= MT_DEVICE
	},
	{
		.virtual	= DA8XX_CP_INTC_VIRT,
		.pfn		= __phys_to_pfn(DA8XX_CP_INTC_BASE),
		.length		= DA8XX_CP_INTC_SIZE,
		.type		= MT_DEVICE
	},
};

/* Contents of JTAG ID register used to identify exact cpu type */
static struct davinci_id da850_ids[] = {
	{
		.variant	= 0x0,
		.part_no	= 0xb7d1,
		.manufacturer	= 0x017,	/* 0x02f >> 1 */
		.cpu_id		= DAVINCI_CPU_ID_DA850,
		.name		= "da850/omap-l138",
	},
	{
		.variant	= 0x1,
		.part_no	= 0xb7d1,
		.manufacturer	= 0x017,	/* 0x02f >> 1 */
		.cpu_id		= DAVINCI_CPU_ID_DA850,
		.name		= "da850/omap-l138/am18x",
	},
};

/* VPIF resource, platform data */
static u64 da850_vpif_dma_mask = DMA_BIT_MASK(32);

static struct resource da850_vpif_display_resource[] = {
	{
		.start = DAVINCI_INTC_IRQ(IRQ_DA850_VPIFINT),
		.end   = DAVINCI_INTC_IRQ(IRQ_DA850_VPIFINT),
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device da850_vpif_display_dev = {
	.name		= "vpif_display",
	.id		= -1,
	.dev		= {
		.dma_mask		= &da850_vpif_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
	.resource       = da850_vpif_display_resource,
	.num_resources  = ARRAY_SIZE(da850_vpif_display_resource),
};

static struct resource da850_vpif_capture_resource[] = {
	{
		.start = DAVINCI_INTC_IRQ(IRQ_DA850_VPIFINT),
		.end   = DAVINCI_INTC_IRQ(IRQ_DA850_VPIFINT),
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = DAVINCI_INTC_IRQ(IRQ_DA850_VPIFINT),
		.end   = DAVINCI_INTC_IRQ(IRQ_DA850_VPIFINT),
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device da850_vpif_capture_dev = {
	.name		= "vpif_capture",
	.id		= -1,
	.dev		= {
		.dma_mask		= &da850_vpif_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
	.resource       = da850_vpif_capture_resource,
	.num_resources  = ARRAY_SIZE(da850_vpif_capture_resource),
};

int __init da850_register_vpif_display(struct vpif_display_config
						*display_config)
{
	da850_vpif_display_dev.dev.platform_data = display_config;
	return platform_device_register(&da850_vpif_display_dev);
}

int __init da850_register_vpif_capture(struct vpif_capture_config
							*capture_config)
{
	da850_vpif_capture_dev.dev.platform_data = capture_config;
	return platform_device_register(&da850_vpif_capture_dev);
}

static const struct davinci_soc_info davinci_soc_info_da850 = {
	.io_desc		= da850_io_desc,
	.io_desc_num		= ARRAY_SIZE(da850_io_desc),
	.jtag_id_reg		= DA8XX_SYSCFG0_BASE + DA8XX_JTAG_ID_REG,
	.ids			= da850_ids,
	.ids_num		= ARRAY_SIZE(da850_ids),
	.pinmux_base		= DA8XX_SYSCFG0_BASE + 0x120,
	.pinmux_pins		= da850_pins,
	.pinmux_pins_num	= ARRAY_SIZE(da850_pins),
	.sram_dma		= DA8XX_SHARED_RAM_BASE,
	.sram_len		= SZ_128K,
};

void __init da850_init(void)
{
	davinci_common_init(&davinci_soc_info_da850);

	da8xx_syscfg0_base = ioremap(DA8XX_SYSCFG0_BASE, SZ_4K);
	if (WARN(!da8xx_syscfg0_base, "Unable to map syscfg0 module"))
		return;

	da8xx_syscfg1_base = ioremap(DA8XX_SYSCFG1_BASE, SZ_4K);
	WARN(!da8xx_syscfg1_base, "Unable to map syscfg1 module");
}
