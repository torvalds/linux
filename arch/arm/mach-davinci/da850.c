/*
 * TI DA850/OMAP-L138 chip specific setup
 *
 * Copyright (C) 2009 Texas Instruments Incorporated - http://www.ti.com/
 *
 * Derived from: arch/arm/mach-davinci/da830.c
 * Original Copyrights follow:
 *
 * 2009 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <linux/clk-provider.h>
#include <linux/clk/davinci.h>
#include <linux/clkdev.h>
#include <linux/cpufreq.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/mfd/da8xx-cfgchip.h>
#include <linux/platform_data/clk-da8xx-cfgchip.h>
#include <linux/platform_data/clk-davinci-pll.h>
#include <linux/platform_data/gpio-davinci.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

#include <asm/mach/map.h>

#include <mach/common.h>
#include <mach/cpufreq.h>
#include <mach/cputype.h>
#include <mach/da8xx.h>
#include <mach/irqs.h>
#include <mach/pm.h>
#include <mach/time.h>

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

const short da850_i2c0_pins[] __initconst = {
	DA850_I2C0_SDA, DA850_I2C0_SCL,
	-1
};

const short da850_i2c1_pins[] __initconst = {
	DA850_I2C1_SCL, DA850_I2C1_SDA,
	-1
};

const short da850_lcdcntl_pins[] __initconst = {
	DA850_LCD_D_0, DA850_LCD_D_1, DA850_LCD_D_2, DA850_LCD_D_3,
	DA850_LCD_D_4, DA850_LCD_D_5, DA850_LCD_D_6, DA850_LCD_D_7,
	DA850_LCD_D_8, DA850_LCD_D_9, DA850_LCD_D_10, DA850_LCD_D_11,
	DA850_LCD_D_12, DA850_LCD_D_13, DA850_LCD_D_14, DA850_LCD_D_15,
	DA850_LCD_PCLK, DA850_LCD_HSYNC, DA850_LCD_VSYNC, DA850_NLCD_AC_ENB_CS,
	-1
};

const short da850_vpif_capture_pins[] __initconst = {
	DA850_VPIF_DIN0, DA850_VPIF_DIN1, DA850_VPIF_DIN2, DA850_VPIF_DIN3,
	DA850_VPIF_DIN4, DA850_VPIF_DIN5, DA850_VPIF_DIN6, DA850_VPIF_DIN7,
	DA850_VPIF_DIN8, DA850_VPIF_DIN9, DA850_VPIF_DIN10, DA850_VPIF_DIN11,
	DA850_VPIF_DIN12, DA850_VPIF_DIN13, DA850_VPIF_DIN14, DA850_VPIF_DIN15,
	DA850_VPIF_CLKIN0, DA850_VPIF_CLKIN1, DA850_VPIF_CLKIN2,
	DA850_VPIF_CLKIN3,
	-1
};

const short da850_vpif_display_pins[] __initconst = {
	DA850_VPIF_DOUT0, DA850_VPIF_DOUT1, DA850_VPIF_DOUT2, DA850_VPIF_DOUT3,
	DA850_VPIF_DOUT4, DA850_VPIF_DOUT5, DA850_VPIF_DOUT6, DA850_VPIF_DOUT7,
	DA850_VPIF_DOUT8, DA850_VPIF_DOUT9, DA850_VPIF_DOUT10,
	DA850_VPIF_DOUT11, DA850_VPIF_DOUT12, DA850_VPIF_DOUT13,
	DA850_VPIF_DOUT14, DA850_VPIF_DOUT15, DA850_VPIF_CLKO2,
	DA850_VPIF_CLKO3,
	-1
};

/* FIQ are pri 0-1; otherwise 2-7, with 7 lowest priority */
static u8 da850_default_priorities[DA850_N_CP_INTC_IRQ] = {
	[IRQ_DA8XX_COMMTX]		= 7,
	[IRQ_DA8XX_COMMRX]		= 7,
	[IRQ_DA8XX_NINT]		= 7,
	[IRQ_DA8XX_EVTOUT0]		= 7,
	[IRQ_DA8XX_EVTOUT1]		= 7,
	[IRQ_DA8XX_EVTOUT2]		= 7,
	[IRQ_DA8XX_EVTOUT3]		= 7,
	[IRQ_DA8XX_EVTOUT4]		= 7,
	[IRQ_DA8XX_EVTOUT5]		= 7,
	[IRQ_DA8XX_EVTOUT6]		= 7,
	[IRQ_DA8XX_EVTOUT7]		= 7,
	[IRQ_DA8XX_CCINT0]		= 7,
	[IRQ_DA8XX_CCERRINT]		= 7,
	[IRQ_DA8XX_TCERRINT0]		= 7,
	[IRQ_DA8XX_AEMIFINT]		= 7,
	[IRQ_DA8XX_I2CINT0]		= 7,
	[IRQ_DA8XX_MMCSDINT0]		= 7,
	[IRQ_DA8XX_MMCSDINT1]		= 7,
	[IRQ_DA8XX_ALLINT0]		= 7,
	[IRQ_DA8XX_RTC]			= 7,
	[IRQ_DA8XX_SPINT0]		= 7,
	[IRQ_DA8XX_TINT12_0]		= 7,
	[IRQ_DA8XX_TINT34_0]		= 7,
	[IRQ_DA8XX_TINT12_1]		= 7,
	[IRQ_DA8XX_TINT34_1]		= 7,
	[IRQ_DA8XX_UARTINT0]		= 7,
	[IRQ_DA8XX_KEYMGRINT]		= 7,
	[IRQ_DA850_MPUADDRERR0]		= 7,
	[IRQ_DA8XX_CHIPINT0]		= 7,
	[IRQ_DA8XX_CHIPINT1]		= 7,
	[IRQ_DA8XX_CHIPINT2]		= 7,
	[IRQ_DA8XX_CHIPINT3]		= 7,
	[IRQ_DA8XX_TCERRINT1]		= 7,
	[IRQ_DA8XX_C0_RX_THRESH_PULSE]	= 7,
	[IRQ_DA8XX_C0_RX_PULSE]		= 7,
	[IRQ_DA8XX_C0_TX_PULSE]		= 7,
	[IRQ_DA8XX_C0_MISC_PULSE]	= 7,
	[IRQ_DA8XX_C1_RX_THRESH_PULSE]	= 7,
	[IRQ_DA8XX_C1_RX_PULSE]		= 7,
	[IRQ_DA8XX_C1_TX_PULSE]		= 7,
	[IRQ_DA8XX_C1_MISC_PULSE]	= 7,
	[IRQ_DA8XX_MEMERR]		= 7,
	[IRQ_DA8XX_GPIO0]		= 7,
	[IRQ_DA8XX_GPIO1]		= 7,
	[IRQ_DA8XX_GPIO2]		= 7,
	[IRQ_DA8XX_GPIO3]		= 7,
	[IRQ_DA8XX_GPIO4]		= 7,
	[IRQ_DA8XX_GPIO5]		= 7,
	[IRQ_DA8XX_GPIO6]		= 7,
	[IRQ_DA8XX_GPIO7]		= 7,
	[IRQ_DA8XX_GPIO8]		= 7,
	[IRQ_DA8XX_I2CINT1]		= 7,
	[IRQ_DA8XX_LCDINT]		= 7,
	[IRQ_DA8XX_UARTINT1]		= 7,
	[IRQ_DA8XX_MCASPINT]		= 7,
	[IRQ_DA8XX_ALLINT1]		= 7,
	[IRQ_DA8XX_SPINT1]		= 7,
	[IRQ_DA8XX_UHPI_INT1]		= 7,
	[IRQ_DA8XX_USB_INT]		= 7,
	[IRQ_DA8XX_IRQN]		= 7,
	[IRQ_DA8XX_RWAKEUP]		= 7,
	[IRQ_DA8XX_UARTINT2]		= 7,
	[IRQ_DA8XX_DFTSSINT]		= 7,
	[IRQ_DA8XX_EHRPWM0]		= 7,
	[IRQ_DA8XX_EHRPWM0TZ]		= 7,
	[IRQ_DA8XX_EHRPWM1]		= 7,
	[IRQ_DA8XX_EHRPWM1TZ]		= 7,
	[IRQ_DA850_SATAINT]		= 7,
	[IRQ_DA850_TINTALL_2]		= 7,
	[IRQ_DA8XX_ECAP0]		= 7,
	[IRQ_DA8XX_ECAP1]		= 7,
	[IRQ_DA8XX_ECAP2]		= 7,
	[IRQ_DA850_MMCSDINT0_1]		= 7,
	[IRQ_DA850_MMCSDINT1_1]		= 7,
	[IRQ_DA850_T12CMPINT0_2]	= 7,
	[IRQ_DA850_T12CMPINT1_2]	= 7,
	[IRQ_DA850_T12CMPINT2_2]	= 7,
	[IRQ_DA850_T12CMPINT3_2]	= 7,
	[IRQ_DA850_T12CMPINT4_2]	= 7,
	[IRQ_DA850_T12CMPINT5_2]	= 7,
	[IRQ_DA850_T12CMPINT6_2]	= 7,
	[IRQ_DA850_T12CMPINT7_2]	= 7,
	[IRQ_DA850_T12CMPINT0_3]	= 7,
	[IRQ_DA850_T12CMPINT1_3]	= 7,
	[IRQ_DA850_T12CMPINT2_3]	= 7,
	[IRQ_DA850_T12CMPINT3_3]	= 7,
	[IRQ_DA850_T12CMPINT4_3]	= 7,
	[IRQ_DA850_T12CMPINT5_3]	= 7,
	[IRQ_DA850_T12CMPINT6_3]	= 7,
	[IRQ_DA850_T12CMPINT7_3]	= 7,
	[IRQ_DA850_RPIINT]		= 7,
	[IRQ_DA850_VPIFINT]		= 7,
	[IRQ_DA850_CCINT1]		= 7,
	[IRQ_DA850_CCERRINT1]		= 7,
	[IRQ_DA850_TCERRINT2]		= 7,
	[IRQ_DA850_TINTALL_3]		= 7,
	[IRQ_DA850_MCBSP0RINT]		= 7,
	[IRQ_DA850_MCBSP0XINT]		= 7,
	[IRQ_DA850_MCBSP1RINT]		= 7,
	[IRQ_DA850_MCBSP1XINT]		= 7,
	[IRQ_DA8XX_ARMCLKSTOPREQ]	= 7,
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

static struct davinci_timer_instance da850_timer_instance[4] = {
	{
		.base		= DA8XX_TIMER64P0_BASE,
		.bottom_irq	= IRQ_DA8XX_TINT12_0,
		.top_irq	= IRQ_DA8XX_TINT34_0,
	},
	{
		.base		= DA8XX_TIMER64P1_BASE,
		.bottom_irq	= IRQ_DA8XX_TINT12_1,
		.top_irq	= IRQ_DA8XX_TINT34_1,
	},
	{
		.base		= DA850_TIMER64P2_BASE,
		.bottom_irq	= IRQ_DA850_TINT12_2,
		.top_irq	= IRQ_DA850_TINT34_2,
	},
	{
		.base		= DA850_TIMER64P3_BASE,
		.bottom_irq	= IRQ_DA850_TINT12_3,
		.top_irq	= IRQ_DA850_TINT34_3,
	},
};

/*
 * T0_BOT: Timer 0, bottom		: Used for clock_event
 * T0_TOP: Timer 0, top			: Used for clocksource
 * T1_BOT, T1_TOP: Timer 1, bottom & top: Used for watchdog timer
 */
static struct davinci_timer_info da850_timer_info = {
	.timers		= da850_timer_instance,
	.clockevent_id	= T0_BOT,
	.clocksource_id	= T0_TOP,
};

#ifdef CONFIG_CPU_FREQ
/*
 * Notes:
 * According to the TRM, minimum PLLM results in maximum power savings.
 * The OPP definitions below should keep the PLLM as low as possible.
 *
 * The output of the PLLM must be between 300 to 600 MHz.
 */
struct da850_opp {
	unsigned int	freq;	/* in KHz */
	unsigned int	prediv;
	unsigned int	mult;
	unsigned int	postdiv;
	unsigned int	cvdd_min; /* in uV */
	unsigned int	cvdd_max; /* in uV */
};

static const struct da850_opp da850_opp_456 = {
	.freq		= 456000,
	.prediv		= 1,
	.mult		= 19,
	.postdiv	= 1,
	.cvdd_min	= 1300000,
	.cvdd_max	= 1350000,
};

static const struct da850_opp da850_opp_408 = {
	.freq		= 408000,
	.prediv		= 1,
	.mult		= 17,
	.postdiv	= 1,
	.cvdd_min	= 1300000,
	.cvdd_max	= 1350000,
};

static const struct da850_opp da850_opp_372 = {
	.freq		= 372000,
	.prediv		= 2,
	.mult		= 31,
	.postdiv	= 1,
	.cvdd_min	= 1200000,
	.cvdd_max	= 1320000,
};

static const struct da850_opp da850_opp_300 = {
	.freq		= 300000,
	.prediv		= 1,
	.mult		= 25,
	.postdiv	= 2,
	.cvdd_min	= 1200000,
	.cvdd_max	= 1320000,
};

static const struct da850_opp da850_opp_200 = {
	.freq		= 200000,
	.prediv		= 1,
	.mult		= 25,
	.postdiv	= 3,
	.cvdd_min	= 1100000,
	.cvdd_max	= 1160000,
};

static const struct da850_opp da850_opp_96 = {
	.freq		= 96000,
	.prediv		= 1,
	.mult		= 20,
	.postdiv	= 5,
	.cvdd_min	= 1000000,
	.cvdd_max	= 1050000,
};

#define OPP(freq) 		\
	{				\
		.driver_data = (unsigned int) &da850_opp_##freq,	\
		.frequency = freq * 1000, \
	}

static struct cpufreq_frequency_table da850_freq_table[] = {
	OPP(456),
	OPP(408),
	OPP(372),
	OPP(300),
	OPP(200),
	OPP(96),
	{
		.driver_data		= 0,
		.frequency	= CPUFREQ_TABLE_END,
	},
};

#ifdef CONFIG_REGULATOR
static int da850_set_voltage(unsigned int index);
static int da850_regulator_init(void);
#endif

static struct davinci_cpufreq_config cpufreq_info = {
	.freq_table = da850_freq_table,
#ifdef CONFIG_REGULATOR
	.init = da850_regulator_init,
	.set_voltage = da850_set_voltage,
#endif
};

#ifdef CONFIG_REGULATOR
static struct regulator *cvdd;

static int da850_set_voltage(unsigned int index)
{
	struct da850_opp *opp;

	if (!cvdd)
		return -ENODEV;

	opp = (struct da850_opp *) cpufreq_info.freq_table[index].driver_data;

	return regulator_set_voltage(cvdd, opp->cvdd_min, opp->cvdd_max);
}

static int da850_regulator_init(void)
{
	cvdd = regulator_get(NULL, "cvdd");
	if (WARN(IS_ERR(cvdd), "Unable to obtain voltage regulator for CVDD;"
					" voltage scaling unsupported\n")) {
		return PTR_ERR(cvdd);
	}

	return 0;
}
#endif

static struct platform_device da850_cpufreq_device = {
	.name			= "cpufreq-davinci",
	.dev = {
		.platform_data	= &cpufreq_info,
	},
	.id = -1,
};

unsigned int da850_max_speed = 300000;

int da850_register_cpufreq(char *async_clk)
{
	int i;

	/* cpufreq driver can help keep an "async" clock constant */
	if (async_clk)
		clk_add_alias("async", da850_cpufreq_device.name,
							async_clk, NULL);
	for (i = 0; i < ARRAY_SIZE(da850_freq_table); i++) {
		if (da850_freq_table[i].frequency <= da850_max_speed) {
			cpufreq_info.freq_table = &da850_freq_table[i];
			break;
		}
	}

	return platform_device_register(&da850_cpufreq_device);
}
#else
int __init da850_register_cpufreq(char *async_clk)
{
	return 0;
}
#endif

/* VPIF resource, platform data */
static u64 da850_vpif_dma_mask = DMA_BIT_MASK(32);

static struct resource da850_vpif_resource[] = {
	{
		.start = DA8XX_VPIF_BASE,
		.end   = DA8XX_VPIF_BASE + 0xfff,
		.flags = IORESOURCE_MEM,
	}
};

static struct platform_device da850_vpif_dev = {
	.name		= "vpif",
	.id		= -1,
	.dev		= {
		.dma_mask		= &da850_vpif_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
	.resource	= da850_vpif_resource,
	.num_resources	= ARRAY_SIZE(da850_vpif_resource),
};

static struct resource da850_vpif_display_resource[] = {
	{
		.start = IRQ_DA850_VPIFINT,
		.end   = IRQ_DA850_VPIFINT,
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
		.start = IRQ_DA850_VPIFINT,
		.end   = IRQ_DA850_VPIFINT,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = IRQ_DA850_VPIFINT,
		.end   = IRQ_DA850_VPIFINT,
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

int __init da850_register_vpif(void)
{
	return platform_device_register(&da850_vpif_dev);
}

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

static struct davinci_gpio_platform_data da850_gpio_platform_data = {
	.ngpio = 144,
};

int __init da850_register_gpio(void)
{
	return da8xx_register_gpio(&da850_gpio_platform_data);
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
	.intc_base		= DA8XX_CP_INTC_BASE,
	.intc_type		= DAVINCI_INTC_TYPE_CP_INTC,
	.intc_irq_prios		= da850_default_priorities,
	.intc_irq_num		= DA850_N_CP_INTC_IRQ,
	.timer_info		= &da850_timer_info,
	.emac_pdata		= &da8xx_emac_pdata,
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

void __init da850_init_time(void)
{
	void __iomem *pll0;
	struct regmap *cfgchip;
	struct clk *clk;

	clk_register_fixed_rate(NULL, "ref_clk", NULL, 0, DA850_REF_FREQ);

	pll0 = ioremap(DA8XX_PLL0_BASE, SZ_4K);
	cfgchip = da8xx_get_cfgchip();

	da850_pll0_init(NULL, pll0, cfgchip);

	clk = clk_get(NULL, "timer0");

	davinci_timer_init(clk);
}

static struct resource da850_pll1_resources[] = {
	{
		.start	= DA850_PLL1_BASE,
		.end	= DA850_PLL1_BASE + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct davinci_pll_platform_data da850_pll1_pdata;

static struct platform_device da850_pll1_device = {
	.name		= "da850-pll1",
	.id		= -1,
	.resource	= da850_pll1_resources,
	.num_resources	= ARRAY_SIZE(da850_pll1_resources),
	.dev		= {
		.platform_data	= &da850_pll1_pdata,
	},
};

static struct resource da850_psc0_resources[] = {
	{
		.start	= DA8XX_PSC0_BASE,
		.end	= DA8XX_PSC0_BASE + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device da850_psc0_device = {
	.name		= "da850-psc0",
	.id		= -1,
	.resource	= da850_psc0_resources,
	.num_resources	= ARRAY_SIZE(da850_psc0_resources),
};

static struct resource da850_psc1_resources[] = {
	{
		.start	= DA8XX_PSC1_BASE,
		.end	= DA8XX_PSC1_BASE + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device da850_psc1_device = {
	.name		= "da850-psc1",
	.id		= -1,
	.resource	= da850_psc1_resources,
	.num_resources	= ARRAY_SIZE(da850_psc1_resources),
};

static struct da8xx_cfgchip_clk_platform_data da850_async1_pdata;

static struct platform_device da850_async1_clksrc_device = {
	.name		= "da850-async1-clksrc",
	.id		= -1,
	.dev		= {
		.platform_data	= &da850_async1_pdata,
	},
};

static struct da8xx_cfgchip_clk_platform_data da850_async3_pdata;

static struct platform_device da850_async3_clksrc_device = {
	.name		= "da850-async3-clksrc",
	.id		= -1,
	.dev		= {
		.platform_data	= &da850_async3_pdata,
	},
};

static struct da8xx_cfgchip_clk_platform_data da850_tbclksync_pdata;

static struct platform_device da850_tbclksync_device = {
	.name		= "da830-tbclksync",
	.id		= -1,
	.dev		= {
		.platform_data	= &da850_tbclksync_pdata,
	},
};

void __init da850_register_clocks(void)
{
	/* PLL0 is registered in da850_init_time() */

	da850_pll1_pdata.cfgchip = da8xx_get_cfgchip();
	platform_device_register(&da850_pll1_device);

	da850_async1_pdata.cfgchip = da8xx_get_cfgchip();
	platform_device_register(&da850_async1_clksrc_device);

	da850_async3_pdata.cfgchip = da8xx_get_cfgchip();
	platform_device_register(&da850_async3_clksrc_device);

	platform_device_register(&da850_psc0_device);

	platform_device_register(&da850_psc1_device);

	da850_tbclksync_pdata.cfgchip = da8xx_get_cfgchip();
	platform_device_register(&da850_tbclksync_device);
}
