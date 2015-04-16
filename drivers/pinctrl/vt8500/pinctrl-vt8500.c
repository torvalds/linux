/*
 * Pinctrl data for VIA VT8500 SoC
 *
 * Copyright (c) 2013 Tony Prisk <linux@prisktech.co.nz>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/io.h>
#include <linux/module.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "pinctrl-wmt.h"

/*
 * Describe the register offsets within the GPIO memory space
 * The dedicated external GPIO's should always be listed in bank 0
 * so they are exported in the 0..31 range which is what users
 * expect.
 *
 * Do not reorder these banks as it will change the pin numbering
 */
static const struct wmt_pinctrl_bank_registers vt8500_banks[] = {
	WMT_PINCTRL_BANK(NO_REG, 0x3C, 0x5C, 0x7C, NO_REG, NO_REG),	/* 0 */
	WMT_PINCTRL_BANK(0x00, 0x20, 0x40, 0x60, NO_REG, NO_REG),	/* 1 */
	WMT_PINCTRL_BANK(0x04, 0x24, 0x44, 0x64, NO_REG, NO_REG),	/* 2 */
	WMT_PINCTRL_BANK(0x08, 0x28, 0x48, 0x68, NO_REG, NO_REG),	/* 3 */
	WMT_PINCTRL_BANK(0x0C, 0x2C, 0x4C, 0x6C, NO_REG, NO_REG),	/* 4 */
	WMT_PINCTRL_BANK(0x10, 0x30, 0x50, 0x70, NO_REG, NO_REG),	/* 5 */
	WMT_PINCTRL_BANK(0x14, 0x34, 0x54, 0x74, NO_REG, NO_REG),	/* 6 */
};

/* Please keep sorted by bank/bit */
#define WMT_PIN_EXTGPIO0	WMT_PIN(0, 0)
#define WMT_PIN_EXTGPIO1	WMT_PIN(0, 1)
#define WMT_PIN_EXTGPIO2	WMT_PIN(0, 2)
#define WMT_PIN_EXTGPIO3	WMT_PIN(0, 3)
#define WMT_PIN_EXTGPIO4	WMT_PIN(0, 4)
#define WMT_PIN_EXTGPIO5	WMT_PIN(0, 5)
#define WMT_PIN_EXTGPIO6	WMT_PIN(0, 6)
#define WMT_PIN_EXTGPIO7	WMT_PIN(0, 7)
#define WMT_PIN_EXTGPIO8	WMT_PIN(0, 8)
#define WMT_PIN_UART0RTS	WMT_PIN(1, 0)
#define WMT_PIN_UART0TXD	WMT_PIN(1, 1)
#define WMT_PIN_UART0CTS	WMT_PIN(1, 2)
#define WMT_PIN_UART0RXD	WMT_PIN(1, 3)
#define WMT_PIN_UART1RTS	WMT_PIN(1, 4)
#define WMT_PIN_UART1TXD	WMT_PIN(1, 5)
#define WMT_PIN_UART1CTS	WMT_PIN(1, 6)
#define WMT_PIN_UART1RXD	WMT_PIN(1, 7)
#define WMT_PIN_SPI0CLK		WMT_PIN(1, 8)
#define WMT_PIN_SPI0SS		WMT_PIN(1, 9)
#define WMT_PIN_SPI0MISO	WMT_PIN(1, 10)
#define WMT_PIN_SPI0MOSI	WMT_PIN(1, 11)
#define WMT_PIN_SPI1CLK		WMT_PIN(1, 12)
#define WMT_PIN_SPI1SS		WMT_PIN(1, 13)
#define WMT_PIN_SPI1MISO	WMT_PIN(1, 14)
#define WMT_PIN_SPI1MOSI	WMT_PIN(1, 15)
#define WMT_PIN_SPI2CLK		WMT_PIN(1, 16)
#define WMT_PIN_SPI2SS		WMT_PIN(1, 17)
#define WMT_PIN_SPI2MISO	WMT_PIN(1, 18)
#define WMT_PIN_SPI2MOSI	WMT_PIN(1, 19)
#define WMT_PIN_SDDATA0		WMT_PIN(2, 0)
#define WMT_PIN_SDDATA1		WMT_PIN(2, 1)
#define WMT_PIN_SDDATA2		WMT_PIN(2, 2)
#define WMT_PIN_SDDATA3		WMT_PIN(2, 3)
#define WMT_PIN_MMCDATA0	WMT_PIN(2, 4)
#define WMT_PIN_MMCDATA1	WMT_PIN(2, 5)
#define WMT_PIN_MMCDATA2	WMT_PIN(2, 6)
#define WMT_PIN_MMCDATA3	WMT_PIN(2, 7)
#define WMT_PIN_SDCLK		WMT_PIN(2, 8)
#define WMT_PIN_SDWP		WMT_PIN(2, 9)
#define WMT_PIN_SDCMD		WMT_PIN(2, 10)
#define WMT_PIN_MSDATA0		WMT_PIN(2, 16)
#define WMT_PIN_MSDATA1		WMT_PIN(2, 17)
#define WMT_PIN_MSDATA2		WMT_PIN(2, 18)
#define WMT_PIN_MSDATA3		WMT_PIN(2, 19)
#define WMT_PIN_MSCLK		WMT_PIN(2, 20)
#define WMT_PIN_MSBS		WMT_PIN(2, 21)
#define WMT_PIN_MSINS		WMT_PIN(2, 22)
#define WMT_PIN_I2C0SCL		WMT_PIN(2, 24)
#define WMT_PIN_I2C0SDA		WMT_PIN(2, 25)
#define WMT_PIN_I2C1SCL		WMT_PIN(2, 26)
#define WMT_PIN_I2C1SDA		WMT_PIN(2, 27)
#define WMT_PIN_MII0RXD0	WMT_PIN(3, 0)
#define WMT_PIN_MII0RXD1	WMT_PIN(3, 1)
#define WMT_PIN_MII0RXD2	WMT_PIN(3, 2)
#define WMT_PIN_MII0RXD3	WMT_PIN(3, 3)
#define WMT_PIN_MII0RXCLK	WMT_PIN(3, 4)
#define WMT_PIN_MII0RXDV	WMT_PIN(3, 5)
#define WMT_PIN_MII0RXERR	WMT_PIN(3, 6)
#define WMT_PIN_MII0PHYRST	WMT_PIN(3, 7)
#define WMT_PIN_MII0TXD0	WMT_PIN(3, 8)
#define WMT_PIN_MII0TXD1	WMT_PIN(3, 9)
#define WMT_PIN_MII0TXD2	WMT_PIN(3, 10)
#define WMT_PIN_MII0TXD3	WMT_PIN(3, 11)
#define WMT_PIN_MII0TXCLK	WMT_PIN(3, 12)
#define WMT_PIN_MII0TXEN	WMT_PIN(3, 13)
#define WMT_PIN_MII0TXERR	WMT_PIN(3, 14)
#define WMT_PIN_MII0PHYPD	WMT_PIN(3, 15)
#define WMT_PIN_MII0COL		WMT_PIN(3, 16)
#define WMT_PIN_MII0CRS		WMT_PIN(3, 17)
#define WMT_PIN_MII0MDIO	WMT_PIN(3, 18)
#define WMT_PIN_MII0MDC		WMT_PIN(3, 19)
#define WMT_PIN_SEECS		WMT_PIN(3, 20)
#define WMT_PIN_SEECK		WMT_PIN(3, 21)
#define WMT_PIN_SEEDI		WMT_PIN(3, 22)
#define WMT_PIN_SEEDO		WMT_PIN(3, 23)
#define WMT_PIN_IDEDREQ0	WMT_PIN(3, 24)
#define WMT_PIN_IDEDREQ1	WMT_PIN(3, 25)
#define WMT_PIN_IDEIOW		WMT_PIN(3, 26)
#define WMT_PIN_IDEIOR		WMT_PIN(3, 27)
#define WMT_PIN_IDEDACK		WMT_PIN(3, 28)
#define WMT_PIN_IDEIORDY	WMT_PIN(3, 29)
#define WMT_PIN_IDEINTRQ	WMT_PIN(3, 30)
#define WMT_PIN_VDIN0		WMT_PIN(4, 0)
#define WMT_PIN_VDIN1		WMT_PIN(4, 1)
#define WMT_PIN_VDIN2		WMT_PIN(4, 2)
#define WMT_PIN_VDIN3		WMT_PIN(4, 3)
#define WMT_PIN_VDIN4		WMT_PIN(4, 4)
#define WMT_PIN_VDIN5		WMT_PIN(4, 5)
#define WMT_PIN_VDIN6		WMT_PIN(4, 6)
#define WMT_PIN_VDIN7		WMT_PIN(4, 7)
#define WMT_PIN_VDOUT0		WMT_PIN(4, 8)
#define WMT_PIN_VDOUT1		WMT_PIN(4, 9)
#define WMT_PIN_VDOUT2		WMT_PIN(4, 10)
#define WMT_PIN_VDOUT3		WMT_PIN(4, 11)
#define WMT_PIN_VDOUT4		WMT_PIN(4, 12)
#define WMT_PIN_VDOUT5		WMT_PIN(4, 13)
#define WMT_PIN_NANDCLE0	WMT_PIN(4, 14)
#define WMT_PIN_NANDCLE1	WMT_PIN(4, 15)
#define WMT_PIN_VDOUT6_7	WMT_PIN(4, 16)
#define WMT_PIN_VHSYNC		WMT_PIN(4, 17)
#define WMT_PIN_VVSYNC		WMT_PIN(4, 18)
#define WMT_PIN_TSDIN0		WMT_PIN(5, 8)
#define WMT_PIN_TSDIN1		WMT_PIN(5, 9)
#define WMT_PIN_TSDIN2		WMT_PIN(5, 10)
#define WMT_PIN_TSDIN3		WMT_PIN(5, 11)
#define WMT_PIN_TSDIN4		WMT_PIN(5, 12)
#define WMT_PIN_TSDIN5		WMT_PIN(5, 13)
#define WMT_PIN_TSDIN6		WMT_PIN(5, 14)
#define WMT_PIN_TSDIN7		WMT_PIN(5, 15)
#define WMT_PIN_TSSYNC		WMT_PIN(5, 16)
#define WMT_PIN_TSVALID		WMT_PIN(5, 17)
#define WMT_PIN_TSCLK		WMT_PIN(5, 18)
#define WMT_PIN_LCDD0		WMT_PIN(6, 0)
#define WMT_PIN_LCDD1		WMT_PIN(6, 1)
#define WMT_PIN_LCDD2		WMT_PIN(6, 2)
#define WMT_PIN_LCDD3		WMT_PIN(6, 3)
#define WMT_PIN_LCDD4		WMT_PIN(6, 4)
#define WMT_PIN_LCDD5		WMT_PIN(6, 5)
#define WMT_PIN_LCDD6		WMT_PIN(6, 6)
#define WMT_PIN_LCDD7		WMT_PIN(6, 7)
#define WMT_PIN_LCDD8		WMT_PIN(6, 8)
#define WMT_PIN_LCDD9		WMT_PIN(6, 9)
#define WMT_PIN_LCDD10		WMT_PIN(6, 10)
#define WMT_PIN_LCDD11		WMT_PIN(6, 11)
#define WMT_PIN_LCDD12		WMT_PIN(6, 12)
#define WMT_PIN_LCDD13		WMT_PIN(6, 13)
#define WMT_PIN_LCDD14		WMT_PIN(6, 14)
#define WMT_PIN_LCDD15		WMT_PIN(6, 15)
#define WMT_PIN_LCDD16		WMT_PIN(6, 16)
#define WMT_PIN_LCDD17		WMT_PIN(6, 17)
#define WMT_PIN_LCDCLK		WMT_PIN(6, 18)
#define WMT_PIN_LCDDEN		WMT_PIN(6, 19)
#define WMT_PIN_LCDLINE		WMT_PIN(6, 20)
#define WMT_PIN_LCDFRM		WMT_PIN(6, 21)
#define WMT_PIN_LCDBIAS		WMT_PIN(6, 22)

static const struct pinctrl_pin_desc vt8500_pins[] = {
	PINCTRL_PIN(WMT_PIN_EXTGPIO0, "extgpio0"),
	PINCTRL_PIN(WMT_PIN_EXTGPIO1, "extgpio1"),
	PINCTRL_PIN(WMT_PIN_EXTGPIO2, "extgpio2"),
	PINCTRL_PIN(WMT_PIN_EXTGPIO3, "extgpio3"),
	PINCTRL_PIN(WMT_PIN_EXTGPIO4, "extgpio4"),
	PINCTRL_PIN(WMT_PIN_EXTGPIO5, "extgpio5"),
	PINCTRL_PIN(WMT_PIN_EXTGPIO6, "extgpio6"),
	PINCTRL_PIN(WMT_PIN_EXTGPIO7, "extgpio7"),
	PINCTRL_PIN(WMT_PIN_EXTGPIO8, "extgpio8"),
	PINCTRL_PIN(WMT_PIN_UART0RTS, "uart0_rts"),
	PINCTRL_PIN(WMT_PIN_UART0TXD, "uart0_txd"),
	PINCTRL_PIN(WMT_PIN_UART0CTS, "uart0_cts"),
	PINCTRL_PIN(WMT_PIN_UART0RXD, "uart0_rxd"),
	PINCTRL_PIN(WMT_PIN_UART1RTS, "uart1_rts"),
	PINCTRL_PIN(WMT_PIN_UART1TXD, "uart1_txd"),
	PINCTRL_PIN(WMT_PIN_UART1CTS, "uart1_cts"),
	PINCTRL_PIN(WMT_PIN_UART1RXD, "uart1_rxd"),
	PINCTRL_PIN(WMT_PIN_SPI0CLK, "spi0_clk"),
	PINCTRL_PIN(WMT_PIN_SPI0SS, "spi0_ss"),
	PINCTRL_PIN(WMT_PIN_SPI0MISO, "spi0_miso"),
	PINCTRL_PIN(WMT_PIN_SPI0MOSI, "spi0_mosi"),
	PINCTRL_PIN(WMT_PIN_SPI1CLK, "spi1_clk"),
	PINCTRL_PIN(WMT_PIN_SPI1SS, "spi1_ss"),
	PINCTRL_PIN(WMT_PIN_SPI1MISO, "spi1_miso"),
	PINCTRL_PIN(WMT_PIN_SPI1MOSI, "spi1_mosi"),
	PINCTRL_PIN(WMT_PIN_SPI2CLK, "spi2_clk"),
	PINCTRL_PIN(WMT_PIN_SPI2SS, "spi2_ss"),
	PINCTRL_PIN(WMT_PIN_SPI2MISO, "spi2_miso"),
	PINCTRL_PIN(WMT_PIN_SPI2MOSI, "spi2_mosi"),
	PINCTRL_PIN(WMT_PIN_SDDATA0, "sd_data0"),
	PINCTRL_PIN(WMT_PIN_SDDATA1, "sd_data1"),
	PINCTRL_PIN(WMT_PIN_SDDATA2, "sd_data2"),
	PINCTRL_PIN(WMT_PIN_SDDATA3, "sd_data3"),
	PINCTRL_PIN(WMT_PIN_MMCDATA0, "mmc_data0"),
	PINCTRL_PIN(WMT_PIN_MMCDATA1, "mmc_data1"),
	PINCTRL_PIN(WMT_PIN_MMCDATA2, "mmc_data2"),
	PINCTRL_PIN(WMT_PIN_MMCDATA3, "mmc_data3"),
	PINCTRL_PIN(WMT_PIN_SDCLK, "sd_clk"),
	PINCTRL_PIN(WMT_PIN_SDWP, "sd_wp"),
	PINCTRL_PIN(WMT_PIN_SDCMD, "sd_cmd"),
	PINCTRL_PIN(WMT_PIN_MSDATA0, "ms_data0"),
	PINCTRL_PIN(WMT_PIN_MSDATA1, "ms_data1"),
	PINCTRL_PIN(WMT_PIN_MSDATA2, "ms_data2"),
	PINCTRL_PIN(WMT_PIN_MSDATA3, "ms_data3"),
	PINCTRL_PIN(WMT_PIN_MSCLK, "ms_clk"),
	PINCTRL_PIN(WMT_PIN_MSBS, "ms_bs"),
	PINCTRL_PIN(WMT_PIN_MSINS, "ms_ins"),
	PINCTRL_PIN(WMT_PIN_I2C0SCL, "i2c0_scl"),
	PINCTRL_PIN(WMT_PIN_I2C0SDA, "i2c0_sda"),
	PINCTRL_PIN(WMT_PIN_I2C1SCL, "i2c1_scl"),
	PINCTRL_PIN(WMT_PIN_I2C1SDA, "i2c1_sda"),
	PINCTRL_PIN(WMT_PIN_MII0RXD0, "mii0_rxd0"),
	PINCTRL_PIN(WMT_PIN_MII0RXD1, "mii0_rxd1"),
	PINCTRL_PIN(WMT_PIN_MII0RXD2, "mii0_rxd2"),
	PINCTRL_PIN(WMT_PIN_MII0RXD3, "mii0_rxd3"),
	PINCTRL_PIN(WMT_PIN_MII0RXCLK, "mii0_rxclk"),
	PINCTRL_PIN(WMT_PIN_MII0RXDV, "mii0_rxdv"),
	PINCTRL_PIN(WMT_PIN_MII0RXERR, "mii0_rxerr"),
	PINCTRL_PIN(WMT_PIN_MII0PHYRST, "mii0_phyrst"),
	PINCTRL_PIN(WMT_PIN_MII0TXD0, "mii0_txd0"),
	PINCTRL_PIN(WMT_PIN_MII0TXD1, "mii0_txd1"),
	PINCTRL_PIN(WMT_PIN_MII0TXD2, "mii0_txd2"),
	PINCTRL_PIN(WMT_PIN_MII0TXD3, "mii0_txd3"),
	PINCTRL_PIN(WMT_PIN_MII0TXCLK, "mii0_txclk"),
	PINCTRL_PIN(WMT_PIN_MII0TXEN, "mii0_txen"),
	PINCTRL_PIN(WMT_PIN_MII0TXERR, "mii0_txerr"),
	PINCTRL_PIN(WMT_PIN_MII0PHYPD, "mii0_phypd"),
	PINCTRL_PIN(WMT_PIN_MII0COL, "mii0_col"),
	PINCTRL_PIN(WMT_PIN_MII0CRS, "mii0_crs"),
	PINCTRL_PIN(WMT_PIN_MII0MDIO, "mii0_mdio"),
	PINCTRL_PIN(WMT_PIN_MII0MDC, "mii0_mdc"),
	PINCTRL_PIN(WMT_PIN_SEECS, "see_cs"),
	PINCTRL_PIN(WMT_PIN_SEECK, "see_ck"),
	PINCTRL_PIN(WMT_PIN_SEEDI, "see_di"),
	PINCTRL_PIN(WMT_PIN_SEEDO, "see_do"),
	PINCTRL_PIN(WMT_PIN_IDEDREQ0, "ide_dreq0"),
	PINCTRL_PIN(WMT_PIN_IDEDREQ1, "ide_dreq1"),
	PINCTRL_PIN(WMT_PIN_IDEIOW, "ide_iow"),
	PINCTRL_PIN(WMT_PIN_IDEIOR, "ide_ior"),
	PINCTRL_PIN(WMT_PIN_IDEDACK, "ide_dack"),
	PINCTRL_PIN(WMT_PIN_IDEIORDY, "ide_iordy"),
	PINCTRL_PIN(WMT_PIN_IDEINTRQ, "ide_intrq"),
	PINCTRL_PIN(WMT_PIN_VDIN0, "vdin0"),
	PINCTRL_PIN(WMT_PIN_VDIN1, "vdin1"),
	PINCTRL_PIN(WMT_PIN_VDIN2, "vdin2"),
	PINCTRL_PIN(WMT_PIN_VDIN3, "vdin3"),
	PINCTRL_PIN(WMT_PIN_VDIN4, "vdin4"),
	PINCTRL_PIN(WMT_PIN_VDIN5, "vdin5"),
	PINCTRL_PIN(WMT_PIN_VDIN6, "vdin6"),
	PINCTRL_PIN(WMT_PIN_VDIN7, "vdin7"),
	PINCTRL_PIN(WMT_PIN_VDOUT0, "vdout0"),
	PINCTRL_PIN(WMT_PIN_VDOUT1, "vdout1"),
	PINCTRL_PIN(WMT_PIN_VDOUT2, "vdout2"),
	PINCTRL_PIN(WMT_PIN_VDOUT3, "vdout3"),
	PINCTRL_PIN(WMT_PIN_VDOUT4, "vdout4"),
	PINCTRL_PIN(WMT_PIN_VDOUT5, "vdout5"),
	PINCTRL_PIN(WMT_PIN_NANDCLE0, "nand_cle0"),
	PINCTRL_PIN(WMT_PIN_NANDCLE1, "nand_cle1"),
	PINCTRL_PIN(WMT_PIN_VDOUT6_7, "vdout6_7"),
	PINCTRL_PIN(WMT_PIN_VHSYNC, "vhsync"),
	PINCTRL_PIN(WMT_PIN_VVSYNC, "vvsync"),
	PINCTRL_PIN(WMT_PIN_TSDIN0, "tsdin0"),
	PINCTRL_PIN(WMT_PIN_TSDIN1, "tsdin1"),
	PINCTRL_PIN(WMT_PIN_TSDIN2, "tsdin2"),
	PINCTRL_PIN(WMT_PIN_TSDIN3, "tsdin3"),
	PINCTRL_PIN(WMT_PIN_TSDIN4, "tsdin4"),
	PINCTRL_PIN(WMT_PIN_TSDIN5, "tsdin5"),
	PINCTRL_PIN(WMT_PIN_TSDIN6, "tsdin6"),
	PINCTRL_PIN(WMT_PIN_TSDIN7, "tsdin7"),
	PINCTRL_PIN(WMT_PIN_TSSYNC, "tssync"),
	PINCTRL_PIN(WMT_PIN_TSVALID, "tsvalid"),
	PINCTRL_PIN(WMT_PIN_TSCLK, "tsclk"),
	PINCTRL_PIN(WMT_PIN_LCDD0, "lcd_d0"),
	PINCTRL_PIN(WMT_PIN_LCDD1, "lcd_d1"),
	PINCTRL_PIN(WMT_PIN_LCDD2, "lcd_d2"),
	PINCTRL_PIN(WMT_PIN_LCDD3, "lcd_d3"),
	PINCTRL_PIN(WMT_PIN_LCDD4, "lcd_d4"),
	PINCTRL_PIN(WMT_PIN_LCDD5, "lcd_d5"),
	PINCTRL_PIN(WMT_PIN_LCDD6, "lcd_d6"),
	PINCTRL_PIN(WMT_PIN_LCDD7, "lcd_d7"),
	PINCTRL_PIN(WMT_PIN_LCDD8, "lcd_d8"),
	PINCTRL_PIN(WMT_PIN_LCDD9, "lcd_d9"),
	PINCTRL_PIN(WMT_PIN_LCDD10, "lcd_d10"),
	PINCTRL_PIN(WMT_PIN_LCDD11, "lcd_d11"),
	PINCTRL_PIN(WMT_PIN_LCDD12, "lcd_d12"),
	PINCTRL_PIN(WMT_PIN_LCDD13, "lcd_d13"),
	PINCTRL_PIN(WMT_PIN_LCDD14, "lcd_d14"),
	PINCTRL_PIN(WMT_PIN_LCDD15, "lcd_d15"),
	PINCTRL_PIN(WMT_PIN_LCDD16, "lcd_d16"),
	PINCTRL_PIN(WMT_PIN_LCDD17, "lcd_d17"),
	PINCTRL_PIN(WMT_PIN_LCDCLK, "lcd_clk"),
	PINCTRL_PIN(WMT_PIN_LCDDEN, "lcd_den"),
	PINCTRL_PIN(WMT_PIN_LCDLINE, "lcd_line"),
	PINCTRL_PIN(WMT_PIN_LCDFRM, "lcd_frm"),
	PINCTRL_PIN(WMT_PIN_LCDBIAS, "lcd_bias"),
};

/* Order of these names must match the above list */
static const char * const vt8500_groups[] = {
	"extgpio0",
	"extgpio1",
	"extgpio2",
	"extgpio3",
	"extgpio4",
	"extgpio5",
	"extgpio6",
	"extgpio7",
	"extgpio8",
	"uart0_rts",
	"uart0_txd",
	"uart0_cts",
	"uart0_rxd",
	"uart1_rts",
	"uart1_txd",
	"uart1_cts",
	"uart1_rxd",
	"spi0_clk",
	"spi0_ss",
	"spi0_miso",
	"spi0_mosi",
	"spi1_clk",
	"spi1_ss",
	"spi1_miso",
	"spi1_mosi",
	"spi2_clk",
	"spi2_ss",
	"spi2_miso",
	"spi2_mosi",
	"sd_data0",
	"sd_data1",
	"sd_data2",
	"sd_data3",
	"mmc_data0",
	"mmc_data1",
	"mmc_data2",
	"mmc_data3",
	"sd_clk",
	"sd_wp",
	"sd_cmd",
	"ms_data0",
	"ms_data1",
	"ms_data2",
	"ms_data3",
	"ms_clk",
	"ms_bs",
	"ms_ins",
	"i2c0_scl",
	"i2c0_sda",
	"i2c1_scl",
	"i2c1_sda",
	"mii0_rxd0",
	"mii0_rxd1",
	"mii0_rxd2",
	"mii0_rxd3",
	"mii0_rxclk",
	"mii0_rxdv",
	"mii0_rxerr",
	"mii0_phyrst",
	"mii0_txd0",
	"mii0_txd1",
	"mii0_txd2",
	"mii0_txd3",
	"mii0_txclk",
	"mii0_txen",
	"mii0_txerr",
	"mii0_phypd",
	"mii0_col",
	"mii0_crs",
	"mii0_mdio",
	"mii0_mdc",
	"see_cs",
	"see_ck",
	"see_di",
	"see_do",
	"ide_dreq0",
	"ide_dreq1",
	"ide_iow",
	"ide_ior",
	"ide_dack",
	"ide_iordy",
	"ide_intrq",
	"vdin0",
	"vdin1",
	"vdin2",
	"vdin3",
	"vdin4",
	"vdin5",
	"vdin6",
	"vdin7",
	"vdout0",
	"vdout1",
	"vdout2",
	"vdout3",
	"vdout4",
	"vdout5",
	"nand_cle0",
	"nand_cle1",
	"vdout6_7",
	"vhsync",
	"vvsync",
	"tsdin0",
	"tsdin1",
	"tsdin2",
	"tsdin3",
	"tsdin4",
	"tsdin5",
	"tsdin6",
	"tsdin7",
	"tssync",
	"tsvalid",
	"tsclk",
	"lcd_d0",
	"lcd_d1",
	"lcd_d2",
	"lcd_d3",
	"lcd_d4",
	"lcd_d5",
	"lcd_d6",
	"lcd_d7",
	"lcd_d8",
	"lcd_d9",
	"lcd_d10",
	"lcd_d11",
	"lcd_d12",
	"lcd_d13",
	"lcd_d14",
	"lcd_d15",
	"lcd_d16",
	"lcd_d17",
	"lcd_clk",
	"lcd_den",
	"lcd_line",
	"lcd_frm",
	"lcd_bias",
};

static int vt8500_pinctrl_probe(struct platform_device *pdev)
{
	struct wmt_pinctrl_data *data;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data) {
		dev_err(&pdev->dev, "failed to allocate data\n");
		return -ENOMEM;
	}

	data->banks = vt8500_banks;
	data->nbanks = ARRAY_SIZE(vt8500_banks);
	data->pins = vt8500_pins;
	data->npins = ARRAY_SIZE(vt8500_pins);
	data->groups = vt8500_groups;
	data->ngroups = ARRAY_SIZE(vt8500_groups);

	return wmt_pinctrl_probe(pdev, data);
}

static int vt8500_pinctrl_remove(struct platform_device *pdev)
{
	return wmt_pinctrl_remove(pdev);
}

static const struct of_device_id wmt_pinctrl_of_match[] = {
	{ .compatible = "via,vt8500-pinctrl" },
	{ /* sentinel */ },
};

static struct platform_driver wmt_pinctrl_driver = {
	.probe	= vt8500_pinctrl_probe,
	.remove	= vt8500_pinctrl_remove,
	.driver = {
		.name	= "pinctrl-vt8500",
		.of_match_table	= wmt_pinctrl_of_match,
	},
};

module_platform_driver(wmt_pinctrl_driver);

MODULE_AUTHOR("Tony Prisk <linux@prisktech.co.nz>");
MODULE_DESCRIPTION("VIA VT8500 Pincontrol driver");
MODULE_LICENSE("GPL v2");
MODULE_DEVICE_TABLE(of, wmt_pinctrl_of_match);
