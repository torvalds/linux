// SPDX-License-Identifier: GPL-2.0
/*
 * Marvell berlin4ct pinctrl driver
 *
 * Copyright (C) 2015 Marvell Technology Group Ltd.
 *
 * Author: Jisheng Zhang <jszhang@marvell.com>
 */

#include <linux/init.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include "berlin.h"

static const struct berlin_desc_group berlin4ct_soc_pinctrl_groups[] = {
	BERLIN_PINCTRL_GROUP("EMMC_RSTn", 0x0, 0x3, 0x00,
			BERLIN_PINCTRL_FUNCTION(0x0, "emmc"), /* RSTn */
			BERLIN_PINCTRL_FUNCTION(0x1, "gpio")), /* GPIO47 */
	BERLIN_PINCTRL_GROUP("NAND_IO0", 0x0, 0x3, 0x03,
			BERLIN_PINCTRL_FUNCTION(0x0, "nand"), /* IO0 */
			BERLIN_PINCTRL_FUNCTION(0x1, "rgmii"), /* RXD0 */
			BERLIN_PINCTRL_FUNCTION(0x2, "sd1"), /* CLK */
			BERLIN_PINCTRL_FUNCTION(0x3, "gpio")), /* GPIO0 */
	BERLIN_PINCTRL_GROUP("NAND_IO1", 0x0, 0x3, 0x06,
			BERLIN_PINCTRL_FUNCTION(0x0, "nand"), /* IO1 */
			BERLIN_PINCTRL_FUNCTION(0x1, "rgmii"), /* RXD1 */
			BERLIN_PINCTRL_FUNCTION(0x2, "sd1"), /* CDn */
			BERLIN_PINCTRL_FUNCTION(0x3, "gpio")), /* GPIO1 */
	BERLIN_PINCTRL_GROUP("NAND_IO2", 0x0, 0x3, 0x09,
			BERLIN_PINCTRL_FUNCTION(0x0, "nand"), /* IO2 */
			BERLIN_PINCTRL_FUNCTION(0x1, "rgmii"), /* RXD2 */
			BERLIN_PINCTRL_FUNCTION(0x2, "sd1"), /* DAT0 */
			BERLIN_PINCTRL_FUNCTION(0x3, "gpio")), /* GPIO2 */
	BERLIN_PINCTRL_GROUP("NAND_IO3", 0x0, 0x3, 0x0c,
			BERLIN_PINCTRL_FUNCTION(0x0, "nand"), /* IO3 */
			BERLIN_PINCTRL_FUNCTION(0x1, "rgmii"), /* RXD3 */
			BERLIN_PINCTRL_FUNCTION(0x2, "sd1"), /* DAT1 */
			BERLIN_PINCTRL_FUNCTION(0x3, "gpio")), /* GPIO3 */
	BERLIN_PINCTRL_GROUP("NAND_IO4", 0x0, 0x3, 0x0f,
			BERLIN_PINCTRL_FUNCTION(0x0, "nand"), /* IO4 */
			BERLIN_PINCTRL_FUNCTION(0x1, "rgmii"), /* RXC */
			BERLIN_PINCTRL_FUNCTION(0x2, "sd1"), /* DAT2 */
			BERLIN_PINCTRL_FUNCTION(0x3, "gpio")), /* GPIO4 */
	BERLIN_PINCTRL_GROUP("NAND_IO5", 0x0, 0x3, 0x12,
			BERLIN_PINCTRL_FUNCTION(0x0, "nand"), /* IO5 */
			BERLIN_PINCTRL_FUNCTION(0x1, "rgmii"), /* RXCTL */
			BERLIN_PINCTRL_FUNCTION(0x2, "sd1"), /* DAT3 */
			BERLIN_PINCTRL_FUNCTION(0x3, "gpio")), /* GPIO5 */
	BERLIN_PINCTRL_GROUP("NAND_IO6", 0x0, 0x3, 0x15,
			BERLIN_PINCTRL_FUNCTION(0x0, "nand"), /* IO6 */
			BERLIN_PINCTRL_FUNCTION(0x1, "rgmii"), /* MDC */
			BERLIN_PINCTRL_FUNCTION(0x2, "sd1"), /* CMD */
			BERLIN_PINCTRL_FUNCTION(0x3, "gpio")), /* GPIO6 */
	BERLIN_PINCTRL_GROUP("NAND_IO7", 0x0, 0x3, 0x18,
			BERLIN_PINCTRL_FUNCTION(0x0, "nand"), /* IO7 */
			BERLIN_PINCTRL_FUNCTION(0x1, "rgmii"), /* MDIO */
			BERLIN_PINCTRL_FUNCTION(0x2, "sd1"), /* WP */
			BERLIN_PINCTRL_FUNCTION(0x3, "gpio")), /* GPIO7 */
	BERLIN_PINCTRL_GROUP("NAND_ALE", 0x0, 0x3, 0x1b,
			BERLIN_PINCTRL_FUNCTION(0x0, "nand"), /* ALE */
			BERLIN_PINCTRL_FUNCTION(0x1, "rgmii"), /* TXD0 */
			BERLIN_PINCTRL_FUNCTION(0x3, "gpio")), /* GPIO8 */
	BERLIN_PINCTRL_GROUP("NAND_CLE", 0x4, 0x3, 0x00,
			BERLIN_PINCTRL_FUNCTION(0x0, "nand"), /* CLE */
			BERLIN_PINCTRL_FUNCTION(0x1, "rgmii"), /* TXD1 */
			BERLIN_PINCTRL_FUNCTION(0x3, "gpio")), /* GPIO9 */
	BERLIN_PINCTRL_GROUP("NAND_WEn", 0x4, 0x3, 0x03,
			BERLIN_PINCTRL_FUNCTION(0x0, "nand"), /* WEn */
			BERLIN_PINCTRL_FUNCTION(0x1, "rgmii"), /* TXD2 */
			BERLIN_PINCTRL_FUNCTION(0x3, "gpio")), /* GPIO10 */
	BERLIN_PINCTRL_GROUP("NAND_REn", 0x4, 0x3, 0x06,
			BERLIN_PINCTRL_FUNCTION(0x0, "nand"), /* REn */
			BERLIN_PINCTRL_FUNCTION(0x1, "rgmii"), /* TXD3 */
			BERLIN_PINCTRL_FUNCTION(0x3, "gpio")), /* GPIO11 */
	BERLIN_PINCTRL_GROUP("NAND_WPn", 0x4, 0x3, 0x09,
			BERLIN_PINCTRL_FUNCTION(0x0, "nand"), /* WPn */
			BERLIN_PINCTRL_FUNCTION(0x3, "gpio")), /* GPIO12 */
	BERLIN_PINCTRL_GROUP("NAND_CEn", 0x4, 0x3, 0x0c,
			BERLIN_PINCTRL_FUNCTION(0x0, "nand"), /* CEn */
			BERLIN_PINCTRL_FUNCTION(0x1, "rgmii"), /* TXC */
			BERLIN_PINCTRL_FUNCTION(0x3, "gpio")), /* GPIO13 */
	BERLIN_PINCTRL_GROUP("NAND_RDY", 0x4, 0x3, 0x0f,
			BERLIN_PINCTRL_FUNCTION(0x0, "nand"), /* RDY */
			BERLIN_PINCTRL_FUNCTION(0x1, "rgmii"), /* TXCTL */
			BERLIN_PINCTRL_FUNCTION(0x3, "gpio")), /* GPIO14 */
	BERLIN_PINCTRL_GROUP("SD0_CLK", 0x4, 0x3, 0x12,
			BERLIN_PINCTRL_FUNCTION(0x0, "gpio"), /* GPIO29 */
			BERLIN_PINCTRL_FUNCTION(0x1, "sd0"), /* CLK*/
			BERLIN_PINCTRL_FUNCTION(0x2, "sts4"), /* CLK */
			BERLIN_PINCTRL_FUNCTION(0x5, "v4g"), /* DBG8 */
			BERLIN_PINCTRL_FUNCTION(0x7, "phy")), /* DBG8 */
	BERLIN_PINCTRL_GROUP("SD0_DAT0", 0x4, 0x3, 0x15,
			BERLIN_PINCTRL_FUNCTION(0x0, "gpio"), /* GPIO30 */
			BERLIN_PINCTRL_FUNCTION(0x1, "sd0"), /* DAT0 */
			BERLIN_PINCTRL_FUNCTION(0x2, "sts4"), /* SOP */
			BERLIN_PINCTRL_FUNCTION(0x5, "v4g"), /* DBG9 */
			BERLIN_PINCTRL_FUNCTION(0x7, "phy")), /* DBG9 */
	BERLIN_PINCTRL_GROUP("SD0_DAT1", 0x4, 0x3, 0x18,
			BERLIN_PINCTRL_FUNCTION(0x0, "gpio"), /* GPIO31 */
			BERLIN_PINCTRL_FUNCTION(0x1, "sd0"), /* DAT1 */
			BERLIN_PINCTRL_FUNCTION(0x2, "sts4"), /* SD */
			BERLIN_PINCTRL_FUNCTION(0x5, "v4g"), /* DBG10 */
			BERLIN_PINCTRL_FUNCTION(0x7, "phy")), /* DBG10 */
	BERLIN_PINCTRL_GROUP("SD0_DAT2", 0x4, 0x3, 0x1b,
			BERLIN_PINCTRL_FUNCTION(0x0, "gpio"), /* GPIO32 */
			BERLIN_PINCTRL_FUNCTION(0x1, "sd0"), /* DAT2 */
			BERLIN_PINCTRL_FUNCTION(0x2, "sts4"), /* VALD */
			BERLIN_PINCTRL_FUNCTION(0x5, "v4g"), /* DBG11 */
			BERLIN_PINCTRL_FUNCTION(0x7, "phy")), /* DBG11 */
	BERLIN_PINCTRL_GROUP("SD0_DAT3", 0x8, 0x3, 0x00,
			BERLIN_PINCTRL_FUNCTION(0x0, "gpio"), /* GPIO33 */
			BERLIN_PINCTRL_FUNCTION(0x1, "sd0"), /* DAT3 */
			BERLIN_PINCTRL_FUNCTION(0x2, "sts5"), /* CLK */
			BERLIN_PINCTRL_FUNCTION(0x5, "v4g"), /* DBG12 */
			BERLIN_PINCTRL_FUNCTION(0x7, "phy")), /* DBG12 */
	BERLIN_PINCTRL_GROUP("SD0_CDn", 0x8, 0x3, 0x03,
			BERLIN_PINCTRL_FUNCTION(0x0, "gpio"), /* GPIO34 */
			BERLIN_PINCTRL_FUNCTION(0x1, "sd0"), /* CDn */
			BERLIN_PINCTRL_FUNCTION(0x2, "sts5"), /* SOP */
			BERLIN_PINCTRL_FUNCTION(0x5, "v4g"), /* DBG13 */
			BERLIN_PINCTRL_FUNCTION(0x7, "phy")), /* DBG13 */
	BERLIN_PINCTRL_GROUP("SD0_CMD", 0x8, 0x3, 0x06,
			BERLIN_PINCTRL_FUNCTION(0x0, "gpio"), /* GPIO35 */
			BERLIN_PINCTRL_FUNCTION(0x1, "sd0"), /* CMD */
			BERLIN_PINCTRL_FUNCTION(0x2, "sts5"), /* SD */
			BERLIN_PINCTRL_FUNCTION(0x5, "v4g"), /* DBG14 */
			BERLIN_PINCTRL_FUNCTION(0x7, "phy")), /* DBG14 */
	BERLIN_PINCTRL_GROUP("SD0_WP", 0x8, 0x3, 0x09,
			BERLIN_PINCTRL_FUNCTION(0x0, "gpio"), /* GPIO36 */
			BERLIN_PINCTRL_FUNCTION(0x1, "sd0"), /* WP */
			BERLIN_PINCTRL_FUNCTION(0x2, "sts5"), /* VALD */
			BERLIN_PINCTRL_FUNCTION(0x5, "v4g"), /* DBG15 */
			BERLIN_PINCTRL_FUNCTION(0x7, "phy")), /* DBG15 */
	BERLIN_PINCTRL_GROUP("STS0_CLK", 0x8, 0x3, 0x0c,
			BERLIN_PINCTRL_FUNCTION(0x0, "gpio"), /* GPIO21 */
			BERLIN_PINCTRL_FUNCTION(0x1, "sts0"), /* CLK */
			BERLIN_PINCTRL_FUNCTION(0x2, "cpupll"), /* CLKO */
			BERLIN_PINCTRL_FUNCTION(0x5, "v4g"), /* DBG0 */
			BERLIN_PINCTRL_FUNCTION(0x7, "phy")), /* DBG0 */
	BERLIN_PINCTRL_GROUP("STS0_SOP", 0x8, 0x3, 0x0f,
			BERLIN_PINCTRL_FUNCTION(0x0, "gpio"), /* GPIO22 */
			BERLIN_PINCTRL_FUNCTION(0x1, "sts0"), /* SOP */
			BERLIN_PINCTRL_FUNCTION(0x2, "syspll"), /* CLKO */
			BERLIN_PINCTRL_FUNCTION(0x5, "v4g"), /* DBG1 */
			BERLIN_PINCTRL_FUNCTION(0x7, "phy")), /* DBG1 */
	BERLIN_PINCTRL_GROUP("STS0_SD", 0x8, 0x3, 0x12,
			BERLIN_PINCTRL_FUNCTION(0x0, "gpio"), /* GPIO23 */
			BERLIN_PINCTRL_FUNCTION(0x1, "sts0"), /* SD */
			BERLIN_PINCTRL_FUNCTION(0x2, "mempll"), /* CLKO */
			BERLIN_PINCTRL_FUNCTION(0x5, "v4g"), /* DBG2 */
			BERLIN_PINCTRL_FUNCTION(0x7, "phy")), /* DBG2 */
	BERLIN_PINCTRL_GROUP("STS0_VALD", 0x8, 0x3, 0x15,
			BERLIN_PINCTRL_FUNCTION(0x0, "gpio"), /* GPIO24 */
			BERLIN_PINCTRL_FUNCTION(0x1, "sts0"), /* VALD */
			BERLIN_PINCTRL_FUNCTION(0x5, "v4g"), /* DBG3 */
			BERLIN_PINCTRL_FUNCTION(0x7, "phy")), /* DBG3 */
	BERLIN_PINCTRL_GROUP("STS1_CLK", 0x8, 0x3, 0x18,
			BERLIN_PINCTRL_FUNCTION(0x0, "gpio"), /* GPIO25 */
			BERLIN_PINCTRL_FUNCTION(0x1, "sts1"), /* CLK */
			BERLIN_PINCTRL_FUNCTION(0x2, "pwm0"),
			BERLIN_PINCTRL_FUNCTION(0x5, "v4g"), /* DBG4 */
			BERLIN_PINCTRL_FUNCTION(0x7, "phy")), /* DBG4 */
	BERLIN_PINCTRL_GROUP("STS1_SOP", 0x8, 0x3, 0x1b,
			BERLIN_PINCTRL_FUNCTION(0x0, "gpio"), /* GPIO26 */
			BERLIN_PINCTRL_FUNCTION(0x1, "sts1"), /* SOP */
			BERLIN_PINCTRL_FUNCTION(0x2, "pwm1"),
			BERLIN_PINCTRL_FUNCTION(0x5, "v4g"), /* DBG5 */
			BERLIN_PINCTRL_FUNCTION(0x7, "phy")), /* DBG5 */
	BERLIN_PINCTRL_GROUP("STS1_SD", 0xc, 0x3, 0x00,
			BERLIN_PINCTRL_FUNCTION(0x0, "gpio"), /* GPIO27 */
			BERLIN_PINCTRL_FUNCTION(0x1, "sts1"), /* SD */
			BERLIN_PINCTRL_FUNCTION(0x2, "pwm2"),
			BERLIN_PINCTRL_FUNCTION(0x5, "v4g"), /* DBG6 */
			BERLIN_PINCTRL_FUNCTION(0x7, "phy")), /* DBG6 */
	BERLIN_PINCTRL_GROUP("STS1_VALD", 0xc, 0x3, 0x03,
			BERLIN_PINCTRL_FUNCTION(0x0, "gpio"), /* GPIO28 */
			BERLIN_PINCTRL_FUNCTION(0x1, "sts1"), /* VALD */
			BERLIN_PINCTRL_FUNCTION(0x2, "pwm3"),
			BERLIN_PINCTRL_FUNCTION(0x5, "v4g"), /* DBG7 */
			BERLIN_PINCTRL_FUNCTION(0x7, "phy")), /* DBG7 */
	BERLIN_PINCTRL_GROUP("SCRD0_RST", 0xc, 0x3, 0x06,
			BERLIN_PINCTRL_FUNCTION(0x0, "gpio"), /* GPIO15 */
			BERLIN_PINCTRL_FUNCTION(0x1, "scrd0"), /* RST */
			BERLIN_PINCTRL_FUNCTION(0x3, "sd1a")), /* CLK */
	BERLIN_PINCTRL_GROUP("SCRD0_DCLK", 0xc, 0x3, 0x09,
			BERLIN_PINCTRL_FUNCTION(0x0, "gpio"), /* GPIO16 */
			BERLIN_PINCTRL_FUNCTION(0x1, "scrd0"), /* DCLK */
			BERLIN_PINCTRL_FUNCTION(0x3, "sd1a")), /* CMD */
	BERLIN_PINCTRL_GROUP("SCRD0_GPIO0", 0xc, 0x3, 0x0c,
			BERLIN_PINCTRL_FUNCTION(0x0, "gpio"), /* GPIO17 */
			BERLIN_PINCTRL_FUNCTION(0x1, "scrd0"), /* SCRD0 GPIO0 */
			BERLIN_PINCTRL_FUNCTION(0x2, "sif"), /* DIO */
			BERLIN_PINCTRL_FUNCTION(0x3, "sd1a")), /* DAT0 */
	BERLIN_PINCTRL_GROUP("SCRD0_GPIO1", 0xc, 0x3, 0x0f,
			BERLIN_PINCTRL_FUNCTION(0x0, "gpio"), /* GPIO18 */
			BERLIN_PINCTRL_FUNCTION(0x1, "scrd0"), /* SCRD0 GPIO1 */
			BERLIN_PINCTRL_FUNCTION(0x2, "sif"), /* CLK */
			BERLIN_PINCTRL_FUNCTION(0x3, "sd1a")), /* DAT1 */
	BERLIN_PINCTRL_GROUP("SCRD0_DIO", 0xc, 0x3, 0x12,
			BERLIN_PINCTRL_FUNCTION(0x0, "gpio"), /* GPIO19 */
			BERLIN_PINCTRL_FUNCTION(0x1, "scrd0"), /* DIO */
			BERLIN_PINCTRL_FUNCTION(0x2, "sif"), /* DEN */
			BERLIN_PINCTRL_FUNCTION(0x3, "sd1a")), /* DAT2 */
	BERLIN_PINCTRL_GROUP("SCRD0_CRD_PRES", 0xc, 0x3, 0x15,
			BERLIN_PINCTRL_FUNCTION(0x0, "gpio"), /* GPIO20 */
			BERLIN_PINCTRL_FUNCTION(0x1, "scrd0"), /* crd pres */
			BERLIN_PINCTRL_FUNCTION(0x3, "sd1a")), /* DAT3 */
	BERLIN_PINCTRL_GROUP("SPI1_SS0n", 0xc, 0x3, 0x18,
			BERLIN_PINCTRL_FUNCTION(0x0, "spi1"), /* SS0n */
			BERLIN_PINCTRL_FUNCTION(0x1, "gpio"), /* GPIO37 */
			BERLIN_PINCTRL_FUNCTION(0x2, "sts2")), /* CLK */
	BERLIN_PINCTRL_GROUP("SPI1_SS1n", 0xc, 0x3, 0x1b,
			BERLIN_PINCTRL_FUNCTION(0x0, "spi1"), /* SS1n */
			BERLIN_PINCTRL_FUNCTION(0x1, "gpio"), /* GPIO38 */
			BERLIN_PINCTRL_FUNCTION(0x2, "sts2"), /* SOP */
			BERLIN_PINCTRL_FUNCTION(0x4, "pwm1")),
	BERLIN_PINCTRL_GROUP("SPI1_SS2n", 0x10, 0x3, 0x00,
			BERLIN_PINCTRL_FUNCTION(0x0, "spi1"), /* SS2n */
			BERLIN_PINCTRL_FUNCTION(0x1, "gpio"), /* GPIO39 */
			BERLIN_PINCTRL_FUNCTION(0x2, "sts2"), /* SD */
			BERLIN_PINCTRL_FUNCTION(0x4, "pwm0")),
	BERLIN_PINCTRL_GROUP("SPI1_SS3n", 0x10, 0x3, 0x03,
			BERLIN_PINCTRL_FUNCTION(0x0, "spi1"), /* SS3n */
			BERLIN_PINCTRL_FUNCTION(0x1, "gpio"), /* GPIO40 */
			BERLIN_PINCTRL_FUNCTION(0x2, "sts2")), /* VALD */
	BERLIN_PINCTRL_GROUP("SPI1_SCLK", 0x10, 0x3, 0x06,
			BERLIN_PINCTRL_FUNCTION(0x0, "spi1"), /* SCLK */
			BERLIN_PINCTRL_FUNCTION(0x1, "gpio"), /* GPIO41 */
			BERLIN_PINCTRL_FUNCTION(0x2, "sts3")), /* CLK */
	BERLIN_PINCTRL_GROUP("SPI1_SDO", 0x10, 0x3, 0x09,
			BERLIN_PINCTRL_FUNCTION(0x0, "spi1"), /* SDO */
			BERLIN_PINCTRL_FUNCTION(0x1, "gpio"), /* GPIO42 */
			BERLIN_PINCTRL_FUNCTION(0x2, "sts3")), /* SOP */
	BERLIN_PINCTRL_GROUP("SPI1_SDI", 0x10, 0x3, 0x0c,
			BERLIN_PINCTRL_FUNCTION(0x0, "spi1"), /* SDI */
			BERLIN_PINCTRL_FUNCTION(0x1, "gpio"), /* GPIO43 */
			BERLIN_PINCTRL_FUNCTION(0x2, "sts3")), /* SD */
	BERLIN_PINCTRL_GROUP("USB0_DRV_VBUS", 0x10, 0x3, 0x0f,
			BERLIN_PINCTRL_FUNCTION(0x0, "gpio"), /* GPIO44 */
			BERLIN_PINCTRL_FUNCTION(0x1, "usb0"), /* VBUS */
			BERLIN_PINCTRL_FUNCTION(0x2, "sts3")), /* VALD */
	BERLIN_PINCTRL_GROUP("TW0_SCL", 0x10, 0x3, 0x12,
			BERLIN_PINCTRL_FUNCTION(0x0, "gpio"), /* GPIO45 */
			BERLIN_PINCTRL_FUNCTION(0x1, "tw0")), /* SCL */
	BERLIN_PINCTRL_GROUP("TW0_SDA", 0x10, 0x3, 0x15,
			BERLIN_PINCTRL_FUNCTION(0x0, "gpio"), /* GPIO46 */
			BERLIN_PINCTRL_FUNCTION(0x1, "tw0")), /* SDA */
};

static const struct berlin_desc_group berlin4ct_avio_pinctrl_groups[] = {
	BERLIN_PINCTRL_GROUP("TX_EDDC_SCL", 0x0, 0x3, 0x00,
			BERLIN_PINCTRL_FUNCTION(0x0, "gpio"), /* AVIO GPIO0 */
			BERLIN_PINCTRL_FUNCTION(0x1, "tx_eddc"), /* SCL */
			BERLIN_PINCTRL_FUNCTION(0x2, "tw1")), /* SCL */
	BERLIN_PINCTRL_GROUP("TX_EDDC_SDA", 0x0, 0x3, 0x03,
			BERLIN_PINCTRL_FUNCTION(0x0, "gpio"), /* AVIO GPIO1 */
			BERLIN_PINCTRL_FUNCTION(0x1, "tx_eddc"), /* SDA */
			BERLIN_PINCTRL_FUNCTION(0x2, "tw1")), /* SDA */
	BERLIN_PINCTRL_GROUP("I2S1_LRCKO", 0x0, 0x3, 0x06,
			BERLIN_PINCTRL_FUNCTION(0x0, "gpio"), /* AVIO GPIO2 */
			BERLIN_PINCTRL_FUNCTION(0x1, "i2s1"), /* LRCKO */
			BERLIN_PINCTRL_FUNCTION(0x3, "sts6"), /* CLK */
			BERLIN_PINCTRL_FUNCTION(0x4, "adac"), /* DBG0 */
			BERLIN_PINCTRL_FUNCTION(0x6, "sd1b"), /* CLK */
			BERLIN_PINCTRL_FUNCTION(0x7, "avio")), /* DBG0 */
	BERLIN_PINCTRL_GROUP("I2S1_BCLKO", 0x0, 0x3, 0x09,
			BERLIN_PINCTRL_FUNCTION(0x0, "gpio"), /* AVIO GPIO3 */
			BERLIN_PINCTRL_FUNCTION(0x1, "i2s1"), /* BCLKO */
			BERLIN_PINCTRL_FUNCTION(0x3, "sts6"), /* SOP */
			BERLIN_PINCTRL_FUNCTION(0x4, "adac"), /* DBG1 */
			BERLIN_PINCTRL_FUNCTION(0x6, "sd1b"), /* CMD */
			BERLIN_PINCTRL_FUNCTION(0x7, "avio")), /* DBG1 */
	BERLIN_PINCTRL_GROUP("I2S1_DO", 0x0, 0x3, 0x0c,
			BERLIN_PINCTRL_FUNCTION(0x0, "gpio"), /* AVIO GPIO4 */
			BERLIN_PINCTRL_FUNCTION(0x1, "i2s1"), /* DO */
			BERLIN_PINCTRL_FUNCTION(0x3, "sts6"), /* SD */
			BERLIN_PINCTRL_FUNCTION(0x4, "adac"), /* DBG2 */
			BERLIN_PINCTRL_FUNCTION(0x6, "sd1b"), /* DAT0 */
			BERLIN_PINCTRL_FUNCTION(0x7, "avio")), /* DBG2 */
	BERLIN_PINCTRL_GROUP("I2S1_MCLK", 0x0, 0x3, 0x0f,
			BERLIN_PINCTRL_FUNCTION(0x0, "gpio"), /* AVIO GPIO5 */
			BERLIN_PINCTRL_FUNCTION(0x1, "i2s1"), /* MCLK */
			BERLIN_PINCTRL_FUNCTION(0x3, "sts6"), /* VALD */
			BERLIN_PINCTRL_FUNCTION(0x4, "adac_test"), /* MCLK */
			BERLIN_PINCTRL_FUNCTION(0x6, "sd1b"), /* DAT1 */
			BERLIN_PINCTRL_FUNCTION(0x7, "avio")), /* DBG3 */
	BERLIN_PINCTRL_GROUP("SPDIFO", 0x0, 0x3, 0x12,
			BERLIN_PINCTRL_FUNCTION(0x0, "gpio"), /* AVIO GPIO6 */
			BERLIN_PINCTRL_FUNCTION(0x1, "spdifo"),
			BERLIN_PINCTRL_FUNCTION(0x2, "avpll"), /* CLKO */
			BERLIN_PINCTRL_FUNCTION(0x4, "adac")), /* DBG3 */
	BERLIN_PINCTRL_GROUP("I2S2_MCLK", 0x0, 0x3, 0x15,
			BERLIN_PINCTRL_FUNCTION(0x0, "gpio"), /* AVIO GPIO7 */
			BERLIN_PINCTRL_FUNCTION(0x1, "i2s2"), /* MCLK */
			BERLIN_PINCTRL_FUNCTION(0x4, "hdmi"), /* FBCLK */
			BERLIN_PINCTRL_FUNCTION(0x5, "pdm")), /* CLKO */
	BERLIN_PINCTRL_GROUP("I2S2_LRCKI", 0x0, 0x3, 0x18,
			BERLIN_PINCTRL_FUNCTION(0x0, "gpio"), /* AVIO GPIO8 */
			BERLIN_PINCTRL_FUNCTION(0x1, "i2s2"), /* LRCKI */
			BERLIN_PINCTRL_FUNCTION(0x2, "pwm0"),
			BERLIN_PINCTRL_FUNCTION(0x3, "sts7"), /* CLK */
			BERLIN_PINCTRL_FUNCTION(0x4, "adac_test"), /* LRCK */
			BERLIN_PINCTRL_FUNCTION(0x6, "sd1b")), /* DAT2 */
	BERLIN_PINCTRL_GROUP("I2S2_BCLKI", 0x0, 0x3, 0x1b,
			BERLIN_PINCTRL_FUNCTION(0x0, "gpio"), /* AVIO GPIO9 */
			BERLIN_PINCTRL_FUNCTION(0x1, "i2s2"), /* BCLKI */
			BERLIN_PINCTRL_FUNCTION(0x2, "pwm1"),
			BERLIN_PINCTRL_FUNCTION(0x3, "sts7"), /* SOP */
			BERLIN_PINCTRL_FUNCTION(0x4, "adac_test"), /* BCLK */
			BERLIN_PINCTRL_FUNCTION(0x6, "sd1b")), /* DAT3 */
	BERLIN_PINCTRL_GROUP("I2S2_DI0", 0x4, 0x3, 0x00,
			BERLIN_PINCTRL_FUNCTION(0x0, "gpio"), /* AVIO GPIO10 */
			BERLIN_PINCTRL_FUNCTION(0x1, "i2s2"), /* DI0 */
			BERLIN_PINCTRL_FUNCTION(0x2, "pwm2"),
			BERLIN_PINCTRL_FUNCTION(0x3, "sts7"), /* SD */
			BERLIN_PINCTRL_FUNCTION(0x4, "adac_test"), /* SDIN */
			BERLIN_PINCTRL_FUNCTION(0x5, "pdm"), /* DI0 */
			BERLIN_PINCTRL_FUNCTION(0x6, "sd1b")), /* CDn */
	BERLIN_PINCTRL_GROUP("I2S2_DI1", 0x4, 0x3, 0x03,
			BERLIN_PINCTRL_FUNCTION(0x0, "gpio"), /* AVIO GPIO11 */
			BERLIN_PINCTRL_FUNCTION(0x1, "i2s2"), /* DI1 */
			BERLIN_PINCTRL_FUNCTION(0x2, "pwm3"),
			BERLIN_PINCTRL_FUNCTION(0x3, "sts7"), /* VALD */
			BERLIN_PINCTRL_FUNCTION(0x4, "adac_test"), /* PWMCLK */
			BERLIN_PINCTRL_FUNCTION(0x5, "pdm"), /* DI1 */
			BERLIN_PINCTRL_FUNCTION(0x6, "sd1b")), /* WP */
};

static const struct berlin_desc_group berlin4ct_sysmgr_pinctrl_groups[] = {
	BERLIN_PINCTRL_GROUP("SM_TW2_SCL", 0x0, 0x3, 0x00,
			BERLIN_PINCTRL_FUNCTION(0x0, "gpio"), /* SM GPIO19 */
			BERLIN_PINCTRL_FUNCTION(0x1, "tw2")), /* SCL */
	BERLIN_PINCTRL_GROUP("SM_TW2_SDA", 0x0, 0x3, 0x03,
			BERLIN_PINCTRL_FUNCTION(0x0, "gpio"), /* SM GPIO20 */
			BERLIN_PINCTRL_FUNCTION(0x1, "tw2")), /* SDA */
	BERLIN_PINCTRL_GROUP("SM_TW3_SCL", 0x0, 0x3, 0x06,
			BERLIN_PINCTRL_FUNCTION(0x0, "gpio"), /* SM GPIO21 */
			BERLIN_PINCTRL_FUNCTION(0x1, "tw3")), /* SCL */
	BERLIN_PINCTRL_GROUP("SM_TW3_SDA", 0x0, 0x3, 0x09,
			BERLIN_PINCTRL_FUNCTION(0x0, "gpio"), /* SM GPIO22 */
			BERLIN_PINCTRL_FUNCTION(0x1, "tw3")), /* SDA */
	BERLIN_PINCTRL_GROUP("SM_TMS", 0x0, 0x3, 0x0c,
			BERLIN_PINCTRL_FUNCTION(0x0, "jtag"), /* TMS */
			BERLIN_PINCTRL_FUNCTION(0x1, "gpio"), /* SM GPIO0 */
			BERLIN_PINCTRL_FUNCTION(0x2, "pwm0")),
	BERLIN_PINCTRL_GROUP("SM_TDI", 0x0, 0x3, 0x0f,
			BERLIN_PINCTRL_FUNCTION(0x0, "jtag"), /* TDI */
			BERLIN_PINCTRL_FUNCTION(0x1, "gpio"), /* SM GPIO1 */
			BERLIN_PINCTRL_FUNCTION(0x2, "pwm1")),
	BERLIN_PINCTRL_GROUP("SM_TDO", 0x0, 0x3, 0x12,
			BERLIN_PINCTRL_FUNCTION(0x0, "jtag"), /* TDO */
			BERLIN_PINCTRL_FUNCTION(0x1, "gpio")), /* SM GPIO2 */
	BERLIN_PINCTRL_GROUP("SM_URT0_TXD", 0x0, 0x3, 0x15,
			BERLIN_PINCTRL_FUNCTION(0x0, "uart0"), /* TXD */
			BERLIN_PINCTRL_FUNCTION(0x1, "gpio")), /* SM GPIO3 */
	BERLIN_PINCTRL_GROUP("SM_URT0_RXD", 0x0, 0x3, 0x18,
			BERLIN_PINCTRL_FUNCTION(0x0, "uart0"), /* RXD */
			BERLIN_PINCTRL_FUNCTION(0x1, "gpio")), /* SM GPIO4 */
	BERLIN_PINCTRL_GROUP("SM_URT1_TXD", 0x0, 0x3, 0x1b,
			BERLIN_PINCTRL_FUNCTION(0x0, "gpio"), /* SM GPIO5 */
			BERLIN_PINCTRL_FUNCTION(0x1, "uart1"), /* TXD */
			BERLIN_PINCTRL_FUNCTION(0x2, "eth1"), /* RXCLK */
			BERLIN_PINCTRL_FUNCTION(0x3, "pwm2"),
			BERLIN_PINCTRL_FUNCTION(0x4, "timer0"),
			BERLIN_PINCTRL_FUNCTION(0x5, "clk_25m")),
	BERLIN_PINCTRL_GROUP("SM_URT1_RXD", 0x4, 0x3, 0x00,
			BERLIN_PINCTRL_FUNCTION(0x0, "gpio"), /* SM GPIO6 */
			BERLIN_PINCTRL_FUNCTION(0x1, "uart1"), /* RXD */
			BERLIN_PINCTRL_FUNCTION(0x3, "pwm3"),
			BERLIN_PINCTRL_FUNCTION(0x4, "timer1")),
	BERLIN_PINCTRL_GROUP("SM_SPI2_SS0n", 0x4, 0x3, 0x03,
			BERLIN_PINCTRL_FUNCTION(0x0, "spi2"), /* SS0 n*/
			BERLIN_PINCTRL_FUNCTION(0x1, "gpio")), /* SM GPIO7 */
	BERLIN_PINCTRL_GROUP("SM_SPI2_SS1n", 0x4, 0x3, 0x06,
			BERLIN_PINCTRL_FUNCTION(0x0, "gpio"), /* SM GPIO8 */
			BERLIN_PINCTRL_FUNCTION(0x1, "spi2")), /* SS1n */
	BERLIN_PINCTRL_GROUP("SM_SPI2_SS2n", 0x4, 0x3, 0x09,
			BERLIN_PINCTRL_FUNCTION(0x0, "gpio"), /* SM GPIO9 */
			BERLIN_PINCTRL_FUNCTION(0x1, "spi2"), /* SS2n */
			BERLIN_PINCTRL_FUNCTION(0x2, "eth1"), /* MDC */
			BERLIN_PINCTRL_FUNCTION(0x3, "pwm0"),
			BERLIN_PINCTRL_FUNCTION(0x4, "timer0"),
			BERLIN_PINCTRL_FUNCTION(0x5, "clk_25m")),
	BERLIN_PINCTRL_GROUP("SM_SPI2_SS3n", 0x4, 0x3, 0x0c,
			BERLIN_PINCTRL_FUNCTION(0x0, "gpio"), /* SM GPIO10 */
			BERLIN_PINCTRL_FUNCTION(0x1, "spi2"), /* SS3n */
			BERLIN_PINCTRL_FUNCTION(0x2, "eth1"), /* MDIO */
			BERLIN_PINCTRL_FUNCTION(0x3, "pwm1"),
			BERLIN_PINCTRL_FUNCTION(0x4, "timer1")),
	BERLIN_PINCTRL_GROUP("SM_SPI2_SDO", 0x4, 0x3, 0x0f,
			BERLIN_PINCTRL_FUNCTION(0x0, "spi2"), /* SDO */
			BERLIN_PINCTRL_FUNCTION(0x1, "gpio")), /* SM GPIO11 */
	BERLIN_PINCTRL_GROUP("SM_SPI2_SDI", 0x4, 0x3, 0x12,
			BERLIN_PINCTRL_FUNCTION(0x0, "spi2"), /* SDI */
			BERLIN_PINCTRL_FUNCTION(0x1, "gpio")), /* SM GPIO12 */
	BERLIN_PINCTRL_GROUP("SM_SPI2_SCLK", 0x4, 0x3, 0x15,
			BERLIN_PINCTRL_FUNCTION(0x0, "spi2"), /* SCLK */
			BERLIN_PINCTRL_FUNCTION(0x1, "gpio")), /* SM GPIO13 */
	BERLIN_PINCTRL_GROUP("SM_FE_LED0", 0x4, 0x3, 0x18,
			BERLIN_PINCTRL_FUNCTION(0x0, "gpio"), /* SM GPIO14 */
			BERLIN_PINCTRL_FUNCTION(0x2, "led")), /* LED0 */
	BERLIN_PINCTRL_GROUP("SM_FE_LED1", 0x4, 0x3, 0x1b,
			BERLIN_PINCTRL_FUNCTION(0x0, "pwr"),
			BERLIN_PINCTRL_FUNCTION(0x1, "gpio"), /* SM GPIO 15 */
			BERLIN_PINCTRL_FUNCTION(0x2, "led")), /* LED1 */
	BERLIN_PINCTRL_GROUP("SM_FE_LED2", 0x8, 0x3, 0x00,
			BERLIN_PINCTRL_FUNCTION(0x0, "gpio"), /* SM GPIO16 */
			BERLIN_PINCTRL_FUNCTION(0x2, "led")), /* LED2 */
	BERLIN_PINCTRL_GROUP("SM_HDMI_HPD", 0x8, 0x3, 0x03,
			BERLIN_PINCTRL_FUNCTION(0x0, "gpio"), /* SM GPIO17 */
			BERLIN_PINCTRL_FUNCTION(0x1, "hdmi")), /* HPD */
	BERLIN_PINCTRL_GROUP("SM_HDMI_CEC", 0x8, 0x3, 0x06,
			BERLIN_PINCTRL_FUNCTION(0x0, "gpio"), /* SM GPIO18 */
			BERLIN_PINCTRL_FUNCTION(0x1, "hdmi")), /* CEC */
};

static const struct berlin_pinctrl_desc berlin4ct_soc_pinctrl_data = {
	.groups = berlin4ct_soc_pinctrl_groups,
	.ngroups = ARRAY_SIZE(berlin4ct_soc_pinctrl_groups),
};

static const struct berlin_pinctrl_desc berlin4ct_avio_pinctrl_data = {
	.groups = berlin4ct_avio_pinctrl_groups,
	.ngroups = ARRAY_SIZE(berlin4ct_avio_pinctrl_groups),
};

static const struct berlin_pinctrl_desc berlin4ct_sysmgr_pinctrl_data = {
	.groups = berlin4ct_sysmgr_pinctrl_groups,
	.ngroups = ARRAY_SIZE(berlin4ct_sysmgr_pinctrl_groups),
};

static const struct of_device_id berlin4ct_pinctrl_match[] = {
	{
		.compatible = "marvell,berlin4ct-soc-pinctrl",
		.data = &berlin4ct_soc_pinctrl_data,
	},
	{
		.compatible = "marvell,berlin4ct-avio-pinctrl",
		.data = &berlin4ct_avio_pinctrl_data,
	},
	{
		.compatible = "marvell,berlin4ct-system-pinctrl",
		.data = &berlin4ct_sysmgr_pinctrl_data,
	},
	{}
};

static int berlin4ct_pinctrl_probe(struct platform_device *pdev)
{
	const struct of_device_id *match =
		of_match_device(berlin4ct_pinctrl_match, &pdev->dev);
	struct regmap_config *rmconfig;
	struct regmap *regmap;
	struct resource *res;
	void __iomem *base;

	rmconfig = devm_kzalloc(&pdev->dev, sizeof(*rmconfig), GFP_KERNEL);
	if (!rmconfig)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	rmconfig->reg_bits = 32,
	rmconfig->val_bits = 32,
	rmconfig->reg_stride = 4,
	rmconfig->max_register = resource_size(res);

	regmap = devm_regmap_init_mmio(&pdev->dev, base, rmconfig);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	return berlin_pinctrl_probe_regmap(pdev, match->data, regmap);
}

static struct platform_driver berlin4ct_pinctrl_driver = {
	.probe	= berlin4ct_pinctrl_probe,
	.driver	= {
		.name = "berlin4ct-pinctrl",
		.of_match_table = berlin4ct_pinctrl_match,
	},
};
builtin_platform_driver(berlin4ct_pinctrl_driver);
