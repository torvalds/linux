/*
 * Copyright (C) 2017 Sanechips Technology Co., Ltd.
 * Copyright 2017 Linaro Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/platform_device.h>

#include "pinctrl-zx.h"

#define TOP_REG0	0x00
#define TOP_REG1	0x04
#define TOP_REG2	0x08
#define TOP_REG3	0x0c
#define TOP_REG4	0x10
#define TOP_REG5	0x14
#define TOP_REG6	0x18
#define TOP_REG7	0x1c
#define TOP_REG8	0x20

/*
 * The pin numbering starts from AON pins with reserved ones included,
 * so that register data like offset and bit position for AON pins can
 * be calculated from pin number.
 */
enum zx296718_pin {
	/* aon_pmm_reg_0 */
	I2C3_SCL = 0,
	I2C3_SDA = 1,
	AON_RESERVED0 = 2,
	AON_RESERVED1 = 3,
	SEC_EN = 4,
	UART0_RXD = 5,
	UART0_TXD = 6,
	IR_IN = 7,
	SPI0_CLK = 8,
	SPI0_CS = 9,
	SPI0_TXD = 10,
	SPI0_RXD = 11,
	KEY_COL0 = 12,
	KEY_COL1 = 13,
	KEY_COL2 = 14,
	KEY_ROW0 = 15,

	/* aon_pmm_reg_1 */
	KEY_ROW1 = 16,
	KEY_ROW2 = 17,
	HDMI_SCL = 18,
	HDMI_SDA = 19,
	JTAG_TCK = 20,
	JTAG_TRSTN = 21,
	JTAG_TMS = 22,
	JTAG_TDI = 23,
	JTAG_TDO = 24,
	I2C0_SCL = 25,
	I2C0_SDA = 26,
	I2C1_SCL = 27,
	I2C1_SDA = 28,
	AON_RESERVED2 = 29,
	AON_RESERVED3 = 30,
	AON_RESERVED4 = 31,

	/* aon_pmm_reg_2 */
	SPI1_CLK = 32,
	SPI1_CS = 33,
	SPI1_TXD = 34,
	SPI1_RXD = 35,
	AON_RESERVED5 = 36,
	AON_RESERVED6 = 37,
	AUDIO_DET = 38,
	SPDIF_OUT = 39,
	HDMI_CEC = 40,
	HDMI_HPD = 41,
	GMAC_25M_OUT = 42,
	BOOT_SEL0 = 43,
	BOOT_SEL1 = 44,
	BOOT_SEL2 = 45,
	DEEP_SLEEP_OUT_N = 46,
	AON_RESERVED7 = 47,

	/* top_pmm_reg_0 */
	GMII_GTX_CLK = 48,
	GMII_TX_CLK = 49,
	GMII_TXD0 = 50,
	GMII_TXD1 = 51,
	GMII_TXD2 = 52,
	GMII_TXD3 = 53,
	GMII_TXD4 = 54,
	GMII_TXD5 = 55,
	GMII_TXD6 = 56,
	GMII_TXD7 = 57,
	GMII_TX_ER = 58,
	GMII_TX_EN = 59,
	GMII_RX_CLK = 60,
	GMII_RXD0 = 61,
	GMII_RXD1 = 62,
	GMII_RXD2 = 63,

	/* top_pmm_reg_1 */
	GMII_RXD3 = 64,
	GMII_RXD4 = 65,
	GMII_RXD5 = 66,
	GMII_RXD6 = 67,
	GMII_RXD7 = 68,
	GMII_RX_ER = 69,
	GMII_RX_DV = 70,
	GMII_COL = 71,
	GMII_CRS = 72,
	GMII_MDC = 73,
	GMII_MDIO = 74,
	SDIO1_CLK = 75,
	SDIO1_CMD = 76,
	SDIO1_DATA0 = 77,
	SDIO1_DATA1 = 78,
	SDIO1_DATA2 = 79,

	/* top_pmm_reg_2 */
	SDIO1_DATA3 = 80,
	SDIO1_CD = 81,
	SDIO1_WP = 82,
	USIM1_CD = 83,
	USIM1_CLK = 84,
	USIM1_RST = 85,

	/* top_pmm_reg_3 */
	USIM1_DATA = 86,
	SDIO0_CLK = 87,
	SDIO0_CMD = 88,
	SDIO0_DATA0 = 89,
	SDIO0_DATA1 = 90,
	SDIO0_DATA2 = 91,
	SDIO0_DATA3 = 92,
	SDIO0_CD = 93,
	SDIO0_WP = 94,

	/* top_pmm_reg_4 */
	TSI0_DATA0 = 95,
	SPINOR_CLK = 96,
	TSI2_DATA = 97,
	TSI2_CLK = 98,
	TSI2_SYNC = 99,
	TSI2_VALID = 100,
	SPINOR_CS = 101,
	SPINOR_DQ0 = 102,
	SPINOR_DQ1 = 103,
	SPINOR_DQ2 = 104,
	SPINOR_DQ3 = 105,
	VGA_HS = 106,
	VGA_VS = 107,
	TSI3_DATA = 108,

	/* top_pmm_reg_5 */
	TSI3_CLK = 109,
	TSI3_SYNC = 110,
	TSI3_VALID = 111,
	I2S1_WS = 112,
	I2S1_BCLK = 113,
	I2S1_MCLK = 114,
	I2S1_DIN0 = 115,
	I2S1_DOUT0 = 116,
	SPI3_CLK = 117,
	SPI3_CS = 118,
	SPI3_TXD = 119,
	NAND_LDO_MS18_SEL = 120,

	/* top_pmm_reg_6 */
	SPI3_RXD = 121,
	I2S0_MCLK = 122,
	I2S0_BCLK = 123,
	I2S0_WS = 124,
	I2S0_DIN0 = 125,
	I2S0_DOUT0 = 126,
	I2C5_SCL = 127,
	I2C5_SDA = 128,
	SPI2_CLK = 129,
	SPI2_CS = 130,
	SPI2_TXD = 131,

	/* top_pmm_reg_7 */
	SPI2_RXD = 132,
	NAND_WP_N = 133,
	NAND_PAGE_SIZE0 = 134,
	NAND_PAGE_SIZE1 = 135,
	NAND_ADDR_CYCLE = 136,
	NAND_RB0 = 137,
	NAND_RB1 = 138,
	NAND_RB2 = 139,
	NAND_RB3 = 140,

	/* top_pmm_reg_8 */
	GMAC_125M_IN = 141,
	GMAC_50M_OUT = 142,
	SPINOR_SSCLK_LOOPBACK = 143,
	SPINOR_SDIO1CLK_LOOPBACK = 144,
};

static const struct pinctrl_pin_desc zx296718_pins[] = {
	/* aon_pmm_reg_0 */
	AON_PIN(I2C3_SCL, TOP_REG2, 18, 2, 0x48, 0,
		AON_MUX(0x0, "ANMI"),		/* anmi */
		AON_MUX(0x1, "AGPIO"),		/* agpio29 */
		AON_MUX(0x2, "nonAON"),		/* pin0 */
		AON_MUX(0x3, "EXT_INT"),	/* int4 */
		TOP_MUX(0x0, "I2C3"),		/* scl */
		TOP_MUX(0x1, "SPI2"),		/* txd */
		TOP_MUX(0x2, "I2S1")),		/* din0 */
	AON_PIN(I2C3_SDA, TOP_REG2, 20, 2, 0x48, 9,
		AON_MUX(0x0, "WD"),		/* rst_b */
		AON_MUX(0x1, "AGPIO"),		/* agpio30 */
		AON_MUX(0x2, "nonAON"),		/* pin1 */
		AON_MUX(0x3, "EXT_INT"),	/* int5 */
		TOP_MUX(0x0, "I2C3"),		/* sda */
		TOP_MUX(0x1, "SPI2"),		/* rxd */
		TOP_MUX(0x2, "I2S0")),		/* mclk */
	ZX_RESERVED(AON_RESERVED0),
	ZX_RESERVED(AON_RESERVED1),
	AON_PIN(SEC_EN, TOP_REG3, 5, 1, 0x50, 0,
		AON_MUX(0x0, "SEC"),		/* en */
		AON_MUX(0x1, "AGPIO"),		/* agpio28 */
		AON_MUX(0x2, "nonAON"),		/* pin3 */
		AON_MUX(0x3, "EXT_INT"),	/* int7 */
		TOP_MUX(0x0, "I2C2"),		/* sda */
		TOP_MUX(0x1, "SPI2")),		/* cs */
	AON_PIN(UART0_RXD, 0, 0, 0, 0x50, 9,
		AON_MUX(0x0, "UART0"),		/* rxd */
		AON_MUX(0x1, "AGPIO"),		/* agpio20 */
		AON_MUX(0x2, "nonAON")),	/* pin34 */
	AON_PIN(UART0_TXD, 0, 0, 0, 0x50, 18,
		AON_MUX(0x0, "UART0"),		/* txd */
		AON_MUX(0x1, "AGPIO"),		/* agpio21 */
		AON_MUX(0x2, "nonAON")),	/* pin32 */
	AON_PIN(IR_IN, 0, 0, 0, 0x64, 0,
		AON_MUX(0x0, "IR"),		/* in */
		AON_MUX(0x1, "AGPIO"),		/* agpio0 */
		AON_MUX(0x2, "nonAON")),	/* pin27 */
	AON_PIN(SPI0_CLK, TOP_REG3, 16, 1, 0x64, 9,
		AON_MUX(0x0, "EXT_INT"),	/* int0 */
		AON_MUX(0x1, "AGPIO"),		/* agpio23 */
		AON_MUX(0x2, "nonAON"),		/* pin5 */
		AON_MUX(0x3, "PCU"),		/* test6 */
		TOP_MUX(0x0, "SPI0"),		/* clk */
		TOP_MUX(0x1, "ISP")),		/* flash_trig */
	AON_PIN(SPI0_CS, TOP_REG3, 17, 1, 0x64, 18,
		AON_MUX(0x0, "EXT_INT"),	/* int1 */
		AON_MUX(0x1, "AGPIO"),		/* agpio24 */
		AON_MUX(0x2, "nonAON"),		/* pin6 */
		AON_MUX(0x3, "PCU"),		/* test0 */
		TOP_MUX(0x0, "SPI0"),		/* cs */
		TOP_MUX(0x1, "ISP")),		/* prelight_trig */
	AON_PIN(SPI0_TXD, TOP_REG3, 18, 1, 0x68, 0,
		AON_MUX(0x0, "EXT_INT"),	/* int2 */
		AON_MUX(0x1, "AGPIO"),		/* agpio25 */
		AON_MUX(0x2, "nonAON"),		/* pin7 */
		AON_MUX(0x3, "PCU"),		/* test1 */
		TOP_MUX(0x0, "SPI0"),		/* txd */
		TOP_MUX(0x1, "ISP")),		/* shutter_trig */
	AON_PIN(SPI0_RXD, TOP_REG3, 19, 1, 0x68, 9,
		AON_MUX(0x0, "EXT_INT"),	/* int3 */
		AON_MUX(0x1, "AGPIO"),		/* agpio26 */
		AON_MUX(0x2, "nonAON"),		/* pin8 */
		AON_MUX(0x3, "PCU"),		/* test2 */
		TOP_MUX(0x0, "SPI0"),		/* rxd */
		TOP_MUX(0x1, "ISP")),		/* shutter_open */
	AON_PIN(KEY_COL0, TOP_REG3, 20, 1, 0x68, 18,
		AON_MUX(0x0, "KEY"),		/* col0 */
		AON_MUX(0x1, "AGPIO"),		/* agpio5 */
		AON_MUX(0x2, "nonAON"),		/* pin9 */
		AON_MUX(0x3, "PCU"),		/* test3 */
		TOP_MUX(0x0, "UART3"),		/* rxd */
		TOP_MUX(0x1, "I2S0")),		/* din1 */
	AON_PIN(KEY_COL1, TOP_REG3, 21, 2, 0x6c, 0,
		AON_MUX(0x0, "KEY"),		/* col1 */
		AON_MUX(0x1, "AGPIO"),		/* agpio6 */
		AON_MUX(0x2, "nonAON"),		/* pin10 */
		TOP_MUX(0x0, "UART3"),		/* txd */
		TOP_MUX(0x1, "I2S0"),		/* din2 */
		TOP_MUX(0x2, "VGA")),		/* scl */
	AON_PIN(KEY_COL2, TOP_REG3, 23, 2, 0x6c, 9,
		AON_MUX(0x0, "KEY"),		/* col2 */
		AON_MUX(0x1, "AGPIO"),		/* agpio7 */
		AON_MUX(0x2, "nonAON"),		/* pin11 */
		TOP_MUX(0x0, "PWM"),		/* out1 */
		TOP_MUX(0x1, "I2S0"),		/* din3 */
		TOP_MUX(0x2, "VGA")),		/* sda */
	AON_PIN(KEY_ROW0, 0, 0, 0, 0x6c, 18,
		AON_MUX(0x0, "KEY"),		/* row0 */
		AON_MUX(0x1, "AGPIO"),		/* agpio8 */
		AON_MUX(0x2, "nonAON"),		/* pin33 */
		AON_MUX(0x3, "WD")),		/* rst_b */

	/* aon_pmm_reg_1 */
	AON_PIN(KEY_ROW1, TOP_REG3, 25, 2, 0x70, 0,
		AON_MUX(0x0, "KEY"),		/* row1 */
		AON_MUX(0x1, "AGPIO"),		/* agpio9 */
		AON_MUX(0x2, "nonAON"),		/* pin12 */
		TOP_MUX(0x0, "LCD"),		/* port0 lcd_te */
		TOP_MUX(0x1, "I2S0"),		/* dout2 */
		TOP_MUX(0x2, "PWM"),		/* out2 */
		TOP_MUX(0x3, "VGA")),		/* hs1 */
	AON_PIN(KEY_ROW2, TOP_REG3, 27, 2, 0x70, 9,
		AON_MUX(0x0, "KEY"),		/* row2 */
		AON_MUX(0x1, "AGPIO"),		/* agpio10 */
		AON_MUX(0x2, "nonAON"),		/* pin13 */
		TOP_MUX(0x0, "LCD"),		/* port1 lcd_te */
		TOP_MUX(0x1, "I2S0"),		/* dout3 */
		TOP_MUX(0x2, "PWM"),		/* out3 */
		TOP_MUX(0x3, "VGA")),		/* vs1 */
	AON_PIN(HDMI_SCL, TOP_REG3, 29, 1, 0x70, 18,
		AON_MUX(0x0, "PCU"),		/* test7 */
		AON_MUX(0x1, "AGPIO"),		/* agpio3 */
		AON_MUX(0x2, "nonAON"),		/* pin14 */
		TOP_MUX(0x0, "HDMI"),		/* scl */
		TOP_MUX(0x1, "UART3")),		/* rxd */
	AON_PIN(HDMI_SDA, TOP_REG3, 30, 1, 0x74, 0,
		AON_MUX(0x0, "PCU"),		/* test8 */
		AON_MUX(0x1, "AGPIO"),		/* agpio4 */
		AON_MUX(0x2, "nonAON"),		/* pin15 */
		TOP_MUX(0x0, "HDMI"),		/* sda */
		TOP_MUX(0x1, "UART3")),		/* txd */
	AON_PIN(JTAG_TCK, TOP_REG7, 3, 1, 0x78, 18,
		AON_MUX(0x0, "JTAG"),		/* tck */
		AON_MUX(0x1, "AGPIO"),		/* agpio11 */
		AON_MUX(0x2, "nonAON"),		/* pin22 */
		AON_MUX(0x3, "EXT_INT"),	/* int4 */
		TOP_MUX(0x0, "SPI4"),		/* clk */
		TOP_MUX(0x1, "UART1")),		/* rxd */
	AON_PIN(JTAG_TRSTN, TOP_REG7, 4, 1, 0xac, 0,
		AON_MUX(0x0, "JTAG"),		/* trstn */
		AON_MUX(0x1, "AGPIO"),		/* agpio12 */
		AON_MUX(0x2, "nonAON"),		/* pin23 */
		AON_MUX(0x3, "EXT_INT"),	/* int5 */
		TOP_MUX(0x0, "SPI4"),		/* cs */
		TOP_MUX(0x1, "UART1")),		/* txd */
	AON_PIN(JTAG_TMS, TOP_REG7, 5, 1, 0xac, 9,
		AON_MUX(0x0, "JTAG"),		/* tms */
		AON_MUX(0x1, "AGPIO"),		/* agpio13 */
		AON_MUX(0x2, "nonAON"),		/* pin24 */
		AON_MUX(0x3, "EXT_INT"),	/* int6 */
		TOP_MUX(0x0, "SPI4"),		/* txd */
		TOP_MUX(0x1, "UART2")),		/* rxd */
	AON_PIN(JTAG_TDI, TOP_REG7, 6, 1, 0xac, 18,
		AON_MUX(0x0, "JTAG"),		/* tdi */
		AON_MUX(0x1, "AGPIO"),		/* agpio14 */
		AON_MUX(0x2, "nonAON"),		/* pin25 */
		AON_MUX(0x3, "EXT_INT"),	/* int7 */
		TOP_MUX(0x0, "SPI4"),		/* rxd */
		TOP_MUX(0x1, "UART2")),		/* txd */
	AON_PIN(JTAG_TDO, 0, 0, 0, 0xb0, 0,
		AON_MUX(0x0, "JTAG"),		/* tdo */
		AON_MUX(0x1, "AGPIO"),		/* agpio15 */
		AON_MUX(0x2, "nonAON")),	/* pin26 */
	AON_PIN(I2C0_SCL, 0, 0, 0, 0xb0, 9,
		AON_MUX(0x0, "I2C0"),		/* scl */
		AON_MUX(0x1, "AGPIO"),		/* agpio16 */
		AON_MUX(0x2, "nonAON")),	/* pin28 */
	AON_PIN(I2C0_SDA, 0, 0, 0, 0xb0, 18,
		AON_MUX(0x0, "I2C0"),		/* sda */
		AON_MUX(0x1, "AGPIO"),		/* agpio17 */
		AON_MUX(0x2, "nonAON")),	/* pin29 */
	AON_PIN(I2C1_SCL, TOP_REG8, 4, 1, 0xb4, 0,
		AON_MUX(0x0, "I2C1"),		/* scl */
		AON_MUX(0x1, "AGPIO"),		/* agpio18 */
		AON_MUX(0x2, "nonAON"),		/* pin30 */
		TOP_MUX(0x0, "LCD")),		/* port0 lcd_te */
	AON_PIN(I2C1_SDA, TOP_REG8, 5, 1, 0xb4, 9,
		AON_MUX(0x0, "I2C1"),		/* sda */
		AON_MUX(0x1, "AGPIO"),		/* agpio19 */
		AON_MUX(0x2, "nonAON"),		/* pin31 */
		TOP_MUX(0x0, "LCD")),		/* port1 lcd_te */
	ZX_RESERVED(AON_RESERVED2),
	ZX_RESERVED(AON_RESERVED3),
	ZX_RESERVED(AON_RESERVED4),

	/* aon_pmm_reg_2 */
	AON_PIN(SPI1_CLK, TOP_REG2, 6, 3, 0x40, 9,
		AON_MUX(0x0, "EXT_INT"),	/* int0 */
		AON_MUX(0x1, "PCU"),		/* test12 */
		AON_MUX(0x2, "nonAON"),		/* pin39 */
		TOP_MUX(0x0, "SPI1"),		/* clk */
		TOP_MUX(0x1, "PCM"),		/* clk */
		TOP_MUX(0x2, "BGPIO"),		/* gpio35 */
		TOP_MUX(0x3, "I2C4"),		/* scl */
		TOP_MUX(0x4, "I2S1"),		/* mclk */
		TOP_MUX(0x5, "ISP")),		/* flash_trig */
	AON_PIN(SPI1_CS, TOP_REG2, 9, 3, 0x40, 18,
		AON_MUX(0x0, "EXT_INT"),	/* int1 */
		AON_MUX(0x1, "PCU"),		/* test13 */
		AON_MUX(0x2, "nonAON"),		/* pin40 */
		TOP_MUX(0x0, "SPI1"),		/* cs */
		TOP_MUX(0x1, "PCM"),		/* fs */
		TOP_MUX(0x2, "BGPIO"),		/* gpio36 */
		TOP_MUX(0x3, "I2C4"),		/* sda */
		TOP_MUX(0x4, "I2S1"),		/* bclk */
		TOP_MUX(0x5, "ISP")),		/* prelight_trig */
	AON_PIN(SPI1_TXD, TOP_REG2, 12, 3, 0x44, 0,
		AON_MUX(0x0, "EXT_INT"),	/* int2 */
		AON_MUX(0x1, "PCU"),		/* test14 */
		AON_MUX(0x2, "nonAON"),		/* pin41 */
		TOP_MUX(0x0, "SPI1"),		/* txd */
		TOP_MUX(0x1, "PCM"),		/* txd */
		TOP_MUX(0x2, "BGPIO"),		/* gpio37 */
		TOP_MUX(0x3, "UART5"),		/* rxd */
		TOP_MUX(0x4, "I2S1"),		/* ws */
		TOP_MUX(0x5, "ISP")),		/* shutter_trig */
	AON_PIN(SPI1_RXD, TOP_REG2, 15, 3, 0x44, 9,
		AON_MUX(0x0, "EXT_INT"),	/* int3 */
		AON_MUX(0x1, "PCU"),		/* test15 */
		AON_MUX(0x2, "nonAON"),		/* pin42 */
		TOP_MUX(0x0, "SPI1"),		/* rxd */
		TOP_MUX(0x1, "PCM"),		/* rxd */
		TOP_MUX(0x2, "BGPIO"),		/* gpio38 */
		TOP_MUX(0x3, "UART5"),		/* txd */
		TOP_MUX(0x4, "I2S1"),		/* dout0 */
		TOP_MUX(0x5, "ISP")),		/* shutter_open */
	ZX_RESERVED(AON_RESERVED5),
	ZX_RESERVED(AON_RESERVED6),
	AON_PIN(AUDIO_DET, TOP_REG3, 3, 2, 0x48, 18,
		AON_MUX(0x0, "PCU"),		/* test4 */
		AON_MUX(0x1, "AGPIO"),		/* agpio27 */
		AON_MUX(0x2, "nonAON"),		/* pin2 */
		AON_MUX(0x3, "EXT_INT"),	/* int16 */
		TOP_MUX(0x0, "AUDIO"),		/* detect */
		TOP_MUX(0x1, "I2C2"),		/* scl */
		TOP_MUX(0x2, "SPI2")),		/* clk */
	AON_PIN(SPDIF_OUT, TOP_REG3, 14, 2, 0x78, 9,
		AON_MUX(0x0, "PCU"),		/* test5 */
		AON_MUX(0x1, "AGPIO"),		/* agpio22 */
		AON_MUX(0x2, "nonAON"),		/* pin4 */
		TOP_MUX(0x0, "SPDIF"),		/* out */
		TOP_MUX(0x1, "PWM"),		/* out0 */
		TOP_MUX(0x2, "ISP")),		/* fl_trig */
	AON_PIN(HDMI_CEC, 0, 0, 0, 0x74, 9,
		AON_MUX(0x0, "PCU"),		/* test9 */
		AON_MUX(0x1, "AGPIO"),		/* agpio1 */
		AON_MUX(0x2, "nonAON")),	/* pin16 */
	AON_PIN(HDMI_HPD, 0, 0, 0, 0x74, 18,
		AON_MUX(0x0, "PCU"),		/* test10 */
		AON_MUX(0x1, "AGPIO"),		/* agpio2 */
		AON_MUX(0x2, "nonAON")),	/* pin17 */
	AON_PIN(GMAC_25M_OUT, 0, 0, 0, 0x78, 0,
		AON_MUX(0x0, "PCU"),		/* test11 */
		AON_MUX(0x1, "AGPIO"),		/* agpio31 */
		AON_MUX(0x2, "nonAON")),	/* pin43 */
	AON_PIN(BOOT_SEL0, 0, 0, 0, 0xc0, 9,
		AON_MUX(0x0, "BOOT"),		/* sel0 */
		AON_MUX(0x1, "AGPIO"),		/* agpio18 */
		AON_MUX(0x2, "nonAON")),	/* pin18 */
	AON_PIN(BOOT_SEL1, 0, 0, 0, 0xc0, 18,
		AON_MUX(0x0, "BOOT"),		/* sel1 */
		AON_MUX(0x1, "AGPIO"),		/* agpio19 */
		AON_MUX(0x2, "nonAON")),	/* pin19 */
	AON_PIN(BOOT_SEL2, 0, 0, 0, 0xc4, 0,
		AON_MUX(0x0, "BOOT"),		/* sel2 */
		AON_MUX(0x1, "AGPIO"),		/* agpio20 */
		AON_MUX(0x2, "nonAON")),	/* pin20 */
	AON_PIN(DEEP_SLEEP_OUT_N, 0, 0, 0, 0xc4, 9,
		AON_MUX(0x0, "DEEPSLP"),	/* deep sleep out_n */
		AON_MUX(0x1, "AGPIO"),		/* agpio21 */
		AON_MUX(0x2, "nonAON")),	/* pin21 */
	ZX_RESERVED(AON_RESERVED7),

	/* top_pmm_reg_0 */
	TOP_PIN(GMII_GTX_CLK, TOP_REG0, 0, 2, 0x10, 0,
		TOP_MUX(0x0, "GMII"),		/* gtx_clk */
		TOP_MUX(0x1, "DVI0"),		/* clk */
		TOP_MUX(0x2, "BGPIO")),		/* gpio0 */
	TOP_PIN(GMII_TX_CLK, TOP_REG0, 2, 2, 0x10, 9,
		TOP_MUX(0x0, "GMII"),		/* tx_clk */
		TOP_MUX(0x1, "DVI0"),		/* vs */
		TOP_MUX(0x2, "BGPIO")),		/* gpio1 */
	TOP_PIN(GMII_TXD0, TOP_REG0, 4, 2, 0x10, 18,
		TOP_MUX(0x0, "GMII"),		/* txd0 */
		TOP_MUX(0x1, "DVI0"),		/* hs */
		TOP_MUX(0x2, "BGPIO")),		/* gpio2 */
	TOP_PIN(GMII_TXD1, TOP_REG0, 6, 2, 0x14, 0,
		TOP_MUX(0x0, "GMII"),		/* txd1 */
		TOP_MUX(0x1, "DVI0"),		/* d0 */
		TOP_MUX(0x2, "BGPIO")),		/* gpio3 */
	TOP_PIN(GMII_TXD2, TOP_REG0, 8, 2, 0x14, 9,
		TOP_MUX(0x0, "GMII"),		/* txd2 */
		TOP_MUX(0x1, "DVI0"),		/* d1 */
		TOP_MUX(0x2, "BGPIO")),		/* gpio4 */
	TOP_PIN(GMII_TXD3, TOP_REG0, 10, 2, 0x14, 18,
		TOP_MUX(0x0, "GMII"),		/* txd3 */
		TOP_MUX(0x1, "DVI0"),		/* d2 */
		TOP_MUX(0x2, "BGPIO")),		/* gpio5 */
	TOP_PIN(GMII_TXD4, TOP_REG0, 12, 2, 0x18, 0,
		TOP_MUX(0x0, "GMII"),		/* txd4 */
		TOP_MUX(0x1, "DVI0"),		/* d3 */
		TOP_MUX(0x2, "BGPIO")),		/* gpio6 */
	TOP_PIN(GMII_TXD5, TOP_REG0, 14, 2, 0x18, 9,
		TOP_MUX(0x0, "GMII"),		/* txd5 */
		TOP_MUX(0x1, "DVI0"),		/* d4 */
		TOP_MUX(0x2, "BGPIO")),		/* gpio7 */
	TOP_PIN(GMII_TXD6, TOP_REG0, 16, 2, 0x18, 18,
		TOP_MUX(0x0, "GMII"),		/* txd6 */
		TOP_MUX(0x1, "DVI0"),		/* d5 */
		TOP_MUX(0x2, "BGPIO")),		/* gpio8 */
	TOP_PIN(GMII_TXD7, TOP_REG0, 18, 2, 0x1c, 0,
		TOP_MUX(0x0, "GMII"),		/* txd7 */
		TOP_MUX(0x1, "DVI0"),		/* d6 */
		TOP_MUX(0x2, "BGPIO")),		/* gpio9 */
	TOP_PIN(GMII_TX_ER, TOP_REG0, 20, 2, 0x1c, 9,
		TOP_MUX(0x0, "GMII"),		/* tx_er */
		TOP_MUX(0x1, "DVI0"),		/* d7 */
		TOP_MUX(0x2, "BGPIO")),		/* gpio10 */
	TOP_PIN(GMII_TX_EN, TOP_REG0, 22, 2, 0x1c, 18,
		TOP_MUX(0x0, "GMII"),		/* tx_en */
		TOP_MUX(0x1, "DVI0"),		/* d8 */
		TOP_MUX(0x3, "BGPIO")),		/* gpio11 */
	TOP_PIN(GMII_RX_CLK, TOP_REG0, 24, 2, 0x20, 0,
		TOP_MUX(0x0, "GMII"),		/* rx_clk */
		TOP_MUX(0x1, "DVI0"),		/* d9 */
		TOP_MUX(0x3, "BGPIO")),		/* gpio12 */
	TOP_PIN(GMII_RXD0, TOP_REG0, 26, 2, 0x20, 9,
		TOP_MUX(0x0, "GMII"),		/* rxd0 */
		TOP_MUX(0x1, "DVI0"),		/* d10 */
		TOP_MUX(0x3, "BGPIO")),		/* gpio13 */
	TOP_PIN(GMII_RXD1, TOP_REG0, 28, 2, 0x20, 18,
		TOP_MUX(0x0, "GMII"),		/* rxd1 */
		TOP_MUX(0x1, "DVI0"),		/* d11 */
		TOP_MUX(0x2, "BGPIO")),		/* gpio14 */
	TOP_PIN(GMII_RXD2, TOP_REG0, 30, 2, 0x24, 0,
		TOP_MUX(0x0, "GMII"),		/* rxd2 */
		TOP_MUX(0x1, "DVI1"),		/* clk */
		TOP_MUX(0x2, "BGPIO")),		/* gpio15 */

	/* top_pmm_reg_1 */
	TOP_PIN(GMII_RXD3, TOP_REG1, 0, 2, 0x24, 9,
		TOP_MUX(0x0, "GMII"),		/* rxd3 */
		TOP_MUX(0x1, "DVI1"),		/* hs */
		TOP_MUX(0x2, "BGPIO")),		/* gpio16 */
	TOP_PIN(GMII_RXD4, TOP_REG1, 2, 2, 0x24, 18,
		TOP_MUX(0x0, "GMII"),		/* rxd4 */
		TOP_MUX(0x1, "DVI1"),		/* vs */
		TOP_MUX(0x2, "BGPIO")),		/* gpio17 */
	TOP_PIN(GMII_RXD5, TOP_REG1, 4, 2, 0x28, 0,
		TOP_MUX(0x0, "GMII"),		/* rxd5 */
		TOP_MUX(0x1, "DVI1"),		/* d0 */
		TOP_MUX(0x2, "BGPIO"),		/* gpio18 */
		TOP_MUX(0x3, "TSI0")),		/* dat0 */
	TOP_PIN(GMII_RXD6, TOP_REG1, 6, 2, 0x28, 9,
		TOP_MUX(0x0, "GMII"),		/* rxd6 */
		TOP_MUX(0x1, "DVI1"),		/* d1 */
		TOP_MUX(0x2, "BGPIO"),		/* gpio19 */
		TOP_MUX(0x3, "TSI0")),		/* clk */
	TOP_PIN(GMII_RXD7, TOP_REG1, 8, 2, 0x28, 18,
		TOP_MUX(0x0, "GMII"),		/* rxd7 */
		TOP_MUX(0x1, "DVI1"),		/* d2 */
		TOP_MUX(0x2, "BGPIO"),		/* gpio20 */
		TOP_MUX(0x3, "TSI0")),		/* sync */
	TOP_PIN(GMII_RX_ER, TOP_REG1, 10, 2, 0x2c, 0,
		TOP_MUX(0x0, "GMII"),		/* rx_er */
		TOP_MUX(0x1, "DVI1"),		/* d3 */
		TOP_MUX(0x2, "BGPIO"),		/* gpio21 */
		TOP_MUX(0x3, "TSI0")),		/* valid */
	TOP_PIN(GMII_RX_DV, TOP_REG1, 12, 2, 0x2c, 9,
		TOP_MUX(0x0, "GMII"),		/* rx_dv */
		TOP_MUX(0x1, "DVI1"),		/* d4 */
		TOP_MUX(0x2, "BGPIO"),		/* gpio22 */
		TOP_MUX(0x3, "TSI1")),		/* dat0 */
	TOP_PIN(GMII_COL, TOP_REG1, 14, 2, 0x2c, 18,
		TOP_MUX(0x0, "GMII"),		/* col */
		TOP_MUX(0x1, "DVI1"),		/* d5 */
		TOP_MUX(0x2, "BGPIO"),		/* gpio23 */
		TOP_MUX(0x3, "TSI1")),		/* clk */
	TOP_PIN(GMII_CRS, TOP_REG1, 16, 2, 0x30, 0,
		TOP_MUX(0x0, "GMII"),		/* crs */
		TOP_MUX(0x1, "DVI1"),		/* d6 */
		TOP_MUX(0x2, "BGPIO"),		/* gpio24 */
		TOP_MUX(0x3, "TSI1")),		/* sync */
	TOP_PIN(GMII_MDC, TOP_REG1, 18, 2, 0x30, 9,
		TOP_MUX(0x0, "GMII"),		/* mdc */
		TOP_MUX(0x1, "DVI1"),		/* d7 */
		TOP_MUX(0x2, "BGPIO"),		/* gpio25 */
		TOP_MUX(0x3, "TSI1")),		/* valid */
	TOP_PIN(GMII_MDIO, TOP_REG1, 20, 1, 0x30, 18,
		TOP_MUX(0x0, "GMII"),		/* mdio */
		TOP_MUX(0x2, "BGPIO")),		/* gpio26 */
	TOP_PIN(SDIO1_CLK, TOP_REG1, 21, 2, 0x34, 18,
		TOP_MUX(0x0, "SDIO1"),		/* clk */
		TOP_MUX(0x1, "USIM0"),		/* clk */
		TOP_MUX(0x2, "BGPIO"),		/* gpio27 */
		TOP_MUX(0x3, "SPINOR")),	/* clk */
	TOP_PIN(SDIO1_CMD, TOP_REG1, 23, 2, 0x38, 0,
		TOP_MUX(0x0, "SDIO1"),		/* cmd */
		TOP_MUX(0x1, "USIM0"),		/* cd */
		TOP_MUX(0x2, "BGPIO"),		/* gpio28 */
		TOP_MUX(0x3, "SPINOR")),	/* cs */
	TOP_PIN(SDIO1_DATA0, TOP_REG1, 25, 2, 0x38, 9,
		TOP_MUX(0x0, "SDIO1"),		/* dat0 */
		TOP_MUX(0x1, "USIM0"),		/* rst */
		TOP_MUX(0x2, "BGPIO"),		/* gpio29 */
		TOP_MUX(0x3, "SPINOR")),	/* dq0 */
	TOP_PIN(SDIO1_DATA1, TOP_REG1, 27, 2, 0x38, 18,
		TOP_MUX(0x0, "SDIO1"),		/* dat1 */
		TOP_MUX(0x1, "USIM0"),		/* data */
		TOP_MUX(0x2, "BGPIO"),		/* gpio30 */
		TOP_MUX(0x3, "SPINOR")),	/* dq1 */
	TOP_PIN(SDIO1_DATA2, TOP_REG1, 29, 2, 0x3c, 0,
		TOP_MUX(0x0, "SDIO1"),		/* dat2 */
		TOP_MUX(0x1, "BGPIO"),		/* gpio31 */
		TOP_MUX(0x2, "SPINOR")),	/* dq2 */

	/* top_pmm_reg_2 */
	TOP_PIN(SDIO1_DATA3, TOP_REG2, 0, 2, 0x3c, 9,
		TOP_MUX(0x0, "SDIO1"),		/* dat3 */
		TOP_MUX(0x1, "BGPIO"),		/* gpio32 */
		TOP_MUX(0x2, "SPINOR")),	/* dq3 */
	TOP_PIN(SDIO1_CD, TOP_REG2, 2, 2, 0x3c, 18,
		TOP_MUX(0x0, "SDIO1"),		/* cd */
		TOP_MUX(0x1, "BGPIO"),		/* gpio33 */
		TOP_MUX(0x2, "ISP")),		/* fl_trig */
	TOP_PIN(SDIO1_WP, TOP_REG2, 4, 2, 0x40, 0,
		TOP_MUX(0x0, "SDIO1"),		/* wp */
		TOP_MUX(0x1, "BGPIO"),		/* gpio34 */
		TOP_MUX(0x2, "ISP")),		/* ref_clk */
	TOP_PIN(USIM1_CD, TOP_REG2, 22, 3, 0x44, 18,
		TOP_MUX(0x0, "USIM1"),		/* cd */
		TOP_MUX(0x1, "UART4"),		/* rxd */
		TOP_MUX(0x2, "BGPIO"),		/* gpio39 */
		TOP_MUX(0x3, "SPI3"),		/* clk */
		TOP_MUX(0x4, "I2S0"),		/* bclk */
		TOP_MUX(0x5, "B_DVI0")),	/* d8 */
	TOP_PIN(USIM1_CLK, TOP_REG2, 25, 3, 0x4c, 18,
		TOP_MUX(0x0, "USIM1"),		/* clk */
		TOP_MUX(0x1, "UART4"),		/* txd */
		TOP_MUX(0x2, "BGPIO"),		/* gpio40 */
		TOP_MUX(0x3, "SPI3"),		/* cs */
		TOP_MUX(0x4, "I2S0"),		/* ws */
		TOP_MUX(0x5, "B_DVI0")),	/* d9 */
	TOP_PIN(USIM1_RST, TOP_REG2, 28, 3, 0x4c, 0,
		TOP_MUX(0x0, "USIM1"),		/* rst */
		TOP_MUX(0x1, "UART4"),		/* cts */
		TOP_MUX(0x2, "BGPIO"),		/* gpio41 */
		TOP_MUX(0x3, "SPI3"),		/* txd */
		TOP_MUX(0x4, "I2S0"),		/* dout0 */
		TOP_MUX(0x5, "B_DVI0")),	/* d10 */

	/* top_pmm_reg_3 */
	TOP_PIN(USIM1_DATA, TOP_REG3, 0, 3, 0x4c, 9,
		TOP_MUX(0x0, "USIM1"),		/* dat */
		TOP_MUX(0x1, "UART4"),		/* rst */
		TOP_MUX(0x2, "BGPIO"),		/* gpio42 */
		TOP_MUX(0x3, "SPI3"),		/* rxd */
		TOP_MUX(0x4, "I2S0"),		/* din0 */
		TOP_MUX(0x5, "B_DVI0")),	/* d11 */
	TOP_PIN(SDIO0_CLK, TOP_REG3, 6, 1, 0x58, 0,
		TOP_MUX(0x0, "SDIO0"),		/* clk */
		TOP_MUX(0x1, "GPIO")),		/* gpio43 */
	TOP_PIN(SDIO0_CMD, TOP_REG3, 7, 1, 0x58, 9,
		TOP_MUX(0x0, "SDIO0"),		/* cmd */
		TOP_MUX(0x1, "GPIO")),		/* gpio44 */
	TOP_PIN(SDIO0_DATA0, TOP_REG3, 8, 1, 0x58, 18,
		TOP_MUX(0x0, "SDIO0"),		/* dat0 */
		TOP_MUX(0x1, "GPIO")),		/* gpio45 */
	TOP_PIN(SDIO0_DATA1, TOP_REG3, 9, 1, 0x5c, 0,
		TOP_MUX(0x0, "SDIO0"),		/* dat1 */
		TOP_MUX(0x1, "GPIO")),		/* gpio46 */
	TOP_PIN(SDIO0_DATA2, TOP_REG3, 10, 1, 0x5c, 9,
		TOP_MUX(0x0, "SDIO0"),		/* dat2 */
		TOP_MUX(0x1, "GPIO")),		/* gpio47 */
	TOP_PIN(SDIO0_DATA3, TOP_REG3, 11, 1, 0x5c, 18,
		TOP_MUX(0x0, "SDIO0"),		/* dat3 */
		TOP_MUX(0x1, "GPIO")),		/* gpio48 */
	TOP_PIN(SDIO0_CD, TOP_REG3, 12, 1, 0x60, 0,
		TOP_MUX(0x0, "SDIO0"),		/* cd */
		TOP_MUX(0x1, "GPIO")),		/* gpio49 */
	TOP_PIN(SDIO0_WP, TOP_REG3, 13, 1, 0x60, 9,
		TOP_MUX(0x0, "SDIO0"),		/* wp */
		TOP_MUX(0x1, "GPIO")),		/* gpio50 */

	/* top_pmm_reg_4 */
	TOP_PIN(TSI0_DATA0, TOP_REG4, 0, 2, 0x60, 18,
		TOP_MUX(0x0, "TSI0"),		/* dat0 */
		TOP_MUX(0x1, "LCD"),		/* clk */
		TOP_MUX(0x2, "BGPIO")),		/* gpio51 */
	TOP_PIN(SPINOR_CLK, TOP_REG4, 2, 2, 0xa8, 18,
		TOP_MUX(0x0, "SPINOR"),		/* clk */
		TOP_MUX(0x1, "TSI0"),		/* dat1 */
		TOP_MUX(0x2, "LCD"),		/* dat0 */
		TOP_MUX(0x3, "BGPIO")),		/* gpio52 */
	TOP_PIN(TSI2_DATA, TOP_REG4, 4, 2, 0x7c, 0,
		TOP_MUX(0x0, "TSI2"),		/* dat */
		TOP_MUX(0x1, "TSI0"),		/* dat2 */
		TOP_MUX(0x2, "LCD"),		/* dat1 */
		TOP_MUX(0x3, "BGPIO")),		/* gpio53 */
	TOP_PIN(TSI2_CLK, TOP_REG4, 6, 2, 0x7c, 9,
		TOP_MUX(0x0, "TSI2"),		/* clk */
		TOP_MUX(0x1, "TSI0"),		/* dat3 */
		TOP_MUX(0x2, "LCD"),		/* dat2 */
		TOP_MUX(0x3, "BGPIO")),		/* gpio54 */
	TOP_PIN(TSI2_SYNC, TOP_REG4, 8, 2, 0x7c, 18,
		TOP_MUX(0x0, "TSI2"),		/* sync */
		TOP_MUX(0x1, "TSI0"),		/* dat4 */
		TOP_MUX(0x2, "LCD"),		/* dat3 */
		TOP_MUX(0x3, "BGPIO")),		/* gpio55 */
	TOP_PIN(TSI2_VALID, TOP_REG4, 10, 2, 0x80, 0,
		TOP_MUX(0x0, "TSI2"),		/* valid */
		TOP_MUX(0x1, "TSI0"),		/* dat5 */
		TOP_MUX(0x2, "LCD"),		/* dat4 */
		TOP_MUX(0x3, "BGPIO")),		/* gpio56 */
	TOP_PIN(SPINOR_CS, TOP_REG4, 12, 2, 0x80, 9,
		TOP_MUX(0x0, "SPINOR"),		/* cs */
		TOP_MUX(0x1, "TSI0"),		/* dat6 */
		TOP_MUX(0x2, "LCD"),		/* dat5 */
		TOP_MUX(0x3, "BGPIO")),		/* gpio57 */
	TOP_PIN(SPINOR_DQ0, TOP_REG4, 14, 2, 0x80, 18,
		TOP_MUX(0x0, "SPINOR"),		/* dq0 */
		TOP_MUX(0x1, "TSI0"),		/* dat7 */
		TOP_MUX(0x2, "LCD"),		/* dat6 */
		TOP_MUX(0x3, "BGPIO")),		/* gpio58 */
	TOP_PIN(SPINOR_DQ1, TOP_REG4, 16, 2, 0x84, 0,
		TOP_MUX(0x0, "SPINOR"),		/* dq1 */
		TOP_MUX(0x1, "TSI0"),		/* clk */
		TOP_MUX(0x2, "LCD"),		/* dat7 */
		TOP_MUX(0x3, "BGPIO")),		/* gpio59 */
	TOP_PIN(SPINOR_DQ2, TOP_REG4, 18, 2, 0x84, 9,
		TOP_MUX(0x0, "SPINOR"),		/* dq2 */
		TOP_MUX(0x1, "TSI0"),		/* sync */
		TOP_MUX(0x2, "LCD"),		/* dat8 */
		TOP_MUX(0x3, "BGPIO")),		/* gpio60 */
	TOP_PIN(SPINOR_DQ3, TOP_REG4, 20, 2, 0x84, 18,
		TOP_MUX(0x0, "SPINOR"),		/* dq3 */
		TOP_MUX(0x1, "TSI0"),		/* valid */
		TOP_MUX(0x2, "LCD"),		/* dat9 */
		TOP_MUX(0x3, "BGPIO")),		/* gpio61 */
	TOP_PIN(VGA_HS, TOP_REG4, 22, 3, 0x88, 0,
		TOP_MUX(0x0, "VGA"),		/* hs */
		TOP_MUX(0x1, "TSI1"),		/* dat0 */
		TOP_MUX(0x2, "LCD"),		/* dat10 */
		TOP_MUX(0x3, "BGPIO"),		/* gpio62 */
		TOP_MUX(0x4, "I2S1"),		/* din1 */
		TOP_MUX(0x5, "B_DVI0")),	/* clk */
	TOP_PIN(VGA_VS, TOP_REG4, 25, 3, 0x88, 9,
		TOP_MUX(0x0, "VGA"),		/* vs0 */
		TOP_MUX(0x1, "TSI1"),		/* dat1 */
		TOP_MUX(0x2, "LCD"),		/* dat11 */
		TOP_MUX(0x3, "BGPIO"),		/* gpio63 */
		TOP_MUX(0x4, "I2S1"),		/* din2 */
		TOP_MUX(0x5, "B_DVI0")),	/* vs */
	TOP_PIN(TSI3_DATA, TOP_REG4, 28, 3, 0x88, 18,
		TOP_MUX(0x0, "TSI3"),		/* dat */
		TOP_MUX(0x1, "TSI1"),		/* dat2 */
		TOP_MUX(0x2, "LCD"),		/* dat12 */
		TOP_MUX(0x3, "BGPIO"),		/* gpio64 */
		TOP_MUX(0x4, "I2S1"),		/* din3 */
		TOP_MUX(0x5, "B_DVI0")),	/* hs */

	/* top_pmm_reg_5 */
	TOP_PIN(TSI3_CLK, TOP_REG5, 0, 3, 0x8c, 0,
		TOP_MUX(0x0, "TSI3"),		/* clk */
		TOP_MUX(0x1, "TSI1"),		/* dat3 */
		TOP_MUX(0x2, "LCD"),		/* dat13 */
		TOP_MUX(0x3, "BGPIO"),		/* gpio65 */
		TOP_MUX(0x4, "I2S1"),		/* dout1 */
		TOP_MUX(0x5, "B_DVI0")),	/* d0 */
	TOP_PIN(TSI3_SYNC, TOP_REG5, 3, 3, 0x8c, 9,
		TOP_MUX(0x0, "TSI3"),		/* sync */
		TOP_MUX(0x1, "TSI1"),		/* dat4 */
		TOP_MUX(0x2, "LCD"),		/* dat14 */
		TOP_MUX(0x3, "BGPIO"),		/* gpio66 */
		TOP_MUX(0x4, "I2S1"),		/* dout2 */
		TOP_MUX(0x5, "B_DVI0")),	/* d1 */
	TOP_PIN(TSI3_VALID, TOP_REG5, 6, 3, 0x8c, 18,
		TOP_MUX(0x0, "TSI3"),		/* valid */
		TOP_MUX(0x1, "TSI1"),		/* dat5 */
		TOP_MUX(0x2, "LCD"),		/* dat15 */
		TOP_MUX(0x3, "BGPIO"),		/* gpio67 */
		TOP_MUX(0x4, "I2S1"),		/* dout3 */
		TOP_MUX(0x5, "B_DVI0")),	/* d2 */
	TOP_PIN(I2S1_WS, TOP_REG5, 9, 3, 0x90, 0,
		TOP_MUX(0x0, "I2S1"),		/* ws */
		TOP_MUX(0x1, "TSI1"),		/* dat6 */
		TOP_MUX(0x2, "LCD"),		/* dat16 */
		TOP_MUX(0x3, "BGPIO"),		/* gpio68 */
		TOP_MUX(0x4, "VGA"),		/* scl */
		TOP_MUX(0x5, "B_DVI0")),	/* d3 */
	TOP_PIN(I2S1_BCLK, TOP_REG5, 12, 3, 0x90, 9,
		TOP_MUX(0x0, "I2S1"),		/* bclk */
		TOP_MUX(0x1, "TSI1"),		/* dat7 */
		TOP_MUX(0x2, "LCD"),		/* dat17 */
		TOP_MUX(0x3, "BGPIO"),		/* gpio69 */
		TOP_MUX(0x4, "VGA"),		/* sda */
		TOP_MUX(0x5, "B_DVI0")),	/* d4 */
	TOP_PIN(I2S1_MCLK, TOP_REG5, 15, 2, 0x90, 18,
		TOP_MUX(0x0, "I2S1"),		/* mclk */
		TOP_MUX(0x1, "TSI1"),		/* clk */
		TOP_MUX(0x2, "LCD"),		/* dat18 */
		TOP_MUX(0x3, "BGPIO")),		/* gpio70 */
	TOP_PIN(I2S1_DIN0, TOP_REG5, 17, 2, 0x94, 0,
		TOP_MUX(0x0, "I2S1"),		/* din0 */
		TOP_MUX(0x1, "TSI1"),		/* sync */
		TOP_MUX(0x2, "LCD"),		/* dat19 */
		TOP_MUX(0x3, "BGPIO")),		/* gpio71 */
	TOP_PIN(I2S1_DOUT0, TOP_REG5, 19, 2, 0x94, 9,
		TOP_MUX(0x0, "I2S1"),		/* dout0 */
		TOP_MUX(0x1, "TSI1"),		/* valid */
		TOP_MUX(0x2, "LCD"),		/* dat20 */
		TOP_MUX(0x3, "BGPIO")),		/* gpio72 */
	TOP_PIN(SPI3_CLK, TOP_REG5, 21, 3, 0x94, 18,
		TOP_MUX(0x0, "SPI3"),		/* clk */
		TOP_MUX(0x1, "TSO1"),		/* clk */
		TOP_MUX(0x2, "LCD"),		/* dat21 */
		TOP_MUX(0x3, "BGPIO"),		/* gpio73 */
		TOP_MUX(0x4, "UART5"),		/* rxd */
		TOP_MUX(0x5, "PCM"),		/* fs */
		TOP_MUX(0x6, "I2S0"),		/* din1 */
		TOP_MUX(0x7, "B_DVI0")),	/* d5 */
	TOP_PIN(SPI3_CS, TOP_REG5, 24, 3, 0x98, 0,
		TOP_MUX(0x0, "SPI3"),		/* cs */
		TOP_MUX(0x1, "TSO1"),		/* dat0 */
		TOP_MUX(0x2, "LCD"),		/* dat22 */
		TOP_MUX(0x3, "BGPIO"),		/* gpio74 */
		TOP_MUX(0x4, "UART5"),		/* txd */
		TOP_MUX(0x5, "PCM"),		/* clk */
		TOP_MUX(0x6, "I2S0"),		/* din2 */
		TOP_MUX(0x7, "B_DVI0")),	/* d6 */
	TOP_PIN(SPI3_TXD, TOP_REG5, 27, 3, 0x98, 9,
		TOP_MUX(0x0, "SPI3"),		/* txd */
		TOP_MUX(0x1, "TSO1"),		/* dat1 */
		TOP_MUX(0x2, "LCD"),		/* dat23 */
		TOP_MUX(0x3, "BGPIO"),		/* gpio75 */
		TOP_MUX(0x4, "UART5"),		/* cts */
		TOP_MUX(0x5, "PCM"),		/* txd */
		TOP_MUX(0x6, "I2S0"),		/* din3 */
		TOP_MUX(0x7, "B_DVI0")),	/* d7 */
	TOP_PIN(NAND_LDO_MS18_SEL, TOP_REG5, 30, 1, 0xe4, 0,
		TOP_MUX(0x0, "NAND"),		/* ldo_ms18_sel */
		TOP_MUX(0x1, "BGPIO")),		/* gpio99 */

	/* top_pmm_reg_6 */
	TOP_PIN(SPI3_RXD, TOP_REG6, 0, 3, 0x98, 18,
		TOP_MUX(0x0, "SPI3"),		/* rxd */
		TOP_MUX(0x1, "TSO1"),		/* dat2 */
		TOP_MUX(0x2, "LCD"),		/* stvu_vsync */
		TOP_MUX(0x3, "BGPIO"),		/* gpio76 */
		TOP_MUX(0x4, "UART5"),		/* rts */
		TOP_MUX(0x5, "PCM"),		/* rxd */
		TOP_MUX(0x6, "I2S0"),		/* dout1 */
		TOP_MUX(0x7, "B_DVI1")),	/* clk */
	TOP_PIN(I2S0_MCLK, TOP_REG6, 3, 3, 0x9c, 0,
		TOP_MUX(0x0, "I2S0"),		/* mclk */
		TOP_MUX(0x1, "TSO1"),		/* dat3 */
		TOP_MUX(0x2, "LCD"),		/* stvd */
		TOP_MUX(0x3, "BGPIO"),		/* gpio77 */
		TOP_MUX(0x4, "USIM0"),		/* cd */
		TOP_MUX(0x5, "B_DVI1")),	/* vs */
	TOP_PIN(I2S0_BCLK, TOP_REG6, 6, 3, 0x9c, 9,
		TOP_MUX(0x0, "I2S0"),		/* bclk */
		TOP_MUX(0x1, "TSO1"),		/* dat4 */
		TOP_MUX(0x2, "LCD"),		/* sthl_hsync */
		TOP_MUX(0x3, "BGPIO"),		/* gpio78 */
		TOP_MUX(0x4, "USIM0"),		/* clk */
		TOP_MUX(0x5, "B_DVI1")),	/* hs */
	TOP_PIN(I2S0_WS, TOP_REG6, 9, 3, 0x9c, 18,
		TOP_MUX(0x0, "I2S0"),		/* ws */
		TOP_MUX(0x1, "TSO1"),		/* dat5 */
		TOP_MUX(0x2, "LCD"),		/* sthr */
		TOP_MUX(0x3, "BGPIO"),		/* gpio79 */
		TOP_MUX(0x4, "USIM0"),		/* rst */
		TOP_MUX(0x5, "B_DVI1")),	/* d0 */
	TOP_PIN(I2S0_DIN0, TOP_REG6, 12, 3, 0xa0, 0,
		TOP_MUX(0x0, "I2S0"),		/* din0 */
		TOP_MUX(0x1, "TSO1"),		/* dat6 */
		TOP_MUX(0x2, "LCD"),		/* oev_dataen */
		TOP_MUX(0x3, "BGPIO"),		/* gpio80 */
		TOP_MUX(0x4, "USIM0"),		/* dat */
		TOP_MUX(0x5, "B_DVI1")),	/* d1 */
	TOP_PIN(I2S0_DOUT0, TOP_REG6, 15, 2, 0xa0, 9,
		TOP_MUX(0x0, "I2S0"),		/* dout0 */
		TOP_MUX(0x1, "TSO1"),		/* dat7 */
		TOP_MUX(0x2, "LCD"),		/* ckv */
		TOP_MUX(0x3, "BGPIO")),		/* gpio81 */
	TOP_PIN(I2C5_SCL, TOP_REG6, 17, 3, 0xa0, 18,
		TOP_MUX(0x0, "I2C5"),		/* scl */
		TOP_MUX(0x1, "TSO1"),		/* sync */
		TOP_MUX(0x2, "LCD"),		/* ld */
		TOP_MUX(0x3, "BGPIO"),		/* gpio82 */
		TOP_MUX(0x4, "PWM"),		/* out2 */
		TOP_MUX(0x5, "I2S0"),		/* dout2 */
		TOP_MUX(0x6, "B_DVI1")),	/* d2 */
	TOP_PIN(I2C5_SDA, TOP_REG6, 20, 3, 0xa4, 0,
		TOP_MUX(0x0, "I2C5"),		/* sda */
		TOP_MUX(0x1, "TSO1"),		/* vld */
		TOP_MUX(0x2, "LCD"),		/* pol */
		TOP_MUX(0x3, "BGPIO"),		/* gpio83 */
		TOP_MUX(0x4, "PWM"),		/* out3 */
		TOP_MUX(0x5, "I2S0"),		/* dout3 */
		TOP_MUX(0x6, "B_DVI1")),	/* d3 */
	TOP_PIN(SPI2_CLK, TOP_REG6, 23, 3, 0xa4, 9,
		TOP_MUX(0x0, "SPI2"),		/* clk */
		TOP_MUX(0x1, "TSO0"),		/* clk */
		TOP_MUX(0x2, "LCD"),		/* degsl */
		TOP_MUX(0x3, "BGPIO"),		/* gpio84 */
		TOP_MUX(0x4, "I2C4"),		/* scl */
		TOP_MUX(0x5, "B_DVI1")),	/* d4 */
	TOP_PIN(SPI2_CS, TOP_REG6, 26, 3, 0xa4, 18,
		TOP_MUX(0x0, "SPI2"),		/* cs */
		TOP_MUX(0x1, "TSO0"),		/* data */
		TOP_MUX(0x2, "LCD"),		/* rev */
		TOP_MUX(0x3, "BGPIO"),		/* gpio85 */
		TOP_MUX(0x4, "I2C4"),		/* sda */
		TOP_MUX(0x5, "B_DVI1")),	/* d5 */
	TOP_PIN(SPI2_TXD, TOP_REG6, 29, 3, 0xa8, 0,
		TOP_MUX(0x0, "SPI2"),		/* txd */
		TOP_MUX(0x1, "TSO0"),		/* sync */
		TOP_MUX(0x2, "LCD"),		/* u_d */
		TOP_MUX(0x3, "BGPIO"),		/* gpio86 */
		TOP_MUX(0x4, "I2C4"),		/* scl */
		TOP_MUX(0x5, "B_DVI1")),	/* d6 */

	/* top_pmm_reg_7 */
	TOP_PIN(SPI2_RXD, TOP_REG7, 0, 3, 0xa8, 9,
		TOP_MUX(0x0, "SPI2"),		/* rxd */
		TOP_MUX(0x1, "TSO0"),		/* vld */
		TOP_MUX(0x2, "LCD"),		/* r_l */
		TOP_MUX(0x3, "BGPIO"),		/* gpio87 */
		TOP_MUX(0x4, "I2C3"),		/* sda */
		TOP_MUX(0x5, "B_DVI1")),	/* d7 */
	TOP_PIN(NAND_WP_N, TOP_REG7, 7, 3, 0x54, 9,
		TOP_MUX(0x0, "NAND"),		/* wp */
		TOP_MUX(0x1, "PWM"),		/* out2 */
		TOP_MUX(0x2, "SPI2"),		/* clk */
		TOP_MUX(0x3, "BGPIO"),		/* gpio88 */
		TOP_MUX(0x4, "TSI0"),		/* dat0 */
		TOP_MUX(0x5, "I2S1")),		/* din1 */
	TOP_PIN(NAND_PAGE_SIZE0, TOP_REG7, 10, 3, 0xb8, 0,
		TOP_MUX(0x0, "NAND"),		/* boot_pagesize0 */
		TOP_MUX(0x1, "PWM"),		/* out3 */
		TOP_MUX(0x2, "SPI2"),		/* cs */
		TOP_MUX(0x3, "BGPIO"),		/* gpio89 */
		TOP_MUX(0x4, "TSI0"),		/* clk */
		TOP_MUX(0x5, "I2S1")),		/* din2 */
	TOP_PIN(NAND_PAGE_SIZE1, TOP_REG7, 13, 3, 0xb8, 9,
		TOP_MUX(0x0, "NAND"),		/* boot_pagesize1 */
		TOP_MUX(0x1, "I2C4"),		/* scl */
		TOP_MUX(0x2, "SPI2"),		/* txd */
		TOP_MUX(0x3, "BGPIO"),		/* gpio90 */
		TOP_MUX(0x4, "TSI0"),		/* sync */
		TOP_MUX(0x5, "I2S1")),		/* din3 */
	TOP_PIN(NAND_ADDR_CYCLE, TOP_REG7, 16, 3, 0xb8, 18,
		TOP_MUX(0x0, "NAND"),		/* boot_addr_cycles */
		TOP_MUX(0x1, "I2C4"),		/* sda */
		TOP_MUX(0x2, "SPI2"),		/* rxd */
		TOP_MUX(0x3, "BGPIO"),		/* gpio91 */
		TOP_MUX(0x4, "TSI0"),		/* valid */
		TOP_MUX(0x5, "I2S1")),		/* dout1 */
	TOP_PIN(NAND_RB0, TOP_REG7, 19, 3, 0xbc, 0,
		TOP_MUX(0x0, "NAND"),		/* rdy_busy0 */
		TOP_MUX(0x1, "I2C2"),		/* scl */
		TOP_MUX(0x2, "USIM0"),		/* cd */
		TOP_MUX(0x3, "BGPIO"),		/* gpio92 */
		TOP_MUX(0x4, "TSI1")),		/* data0 */
	TOP_PIN(NAND_RB1, TOP_REG7, 22, 3, 0xbc, 9,
		TOP_MUX(0x0, "NAND"),		/* rdy_busy1 */
		TOP_MUX(0x1, "I2C2"),		/* sda */
		TOP_MUX(0x2, "USIM0"),		/* clk */
		TOP_MUX(0x3, "BGPIO"),		/* gpio93 */
		TOP_MUX(0x4, "TSI1")),		/* clk */
	TOP_PIN(NAND_RB2, TOP_REG7, 25, 3, 0xbc, 18,
		TOP_MUX(0x0, "NAND"),		/* rdy_busy2 */
		TOP_MUX(0x1, "UART5"),		/* rxd */
		TOP_MUX(0x2, "USIM0"),		/* rst */
		TOP_MUX(0x3, "BGPIO"),		/* gpio94 */
		TOP_MUX(0x4, "TSI1"),		/* sync */
		TOP_MUX(0x4, "I2S1")),		/* dout2 */
	TOP_PIN(NAND_RB3, TOP_REG7, 28, 3, 0x54, 18,
		TOP_MUX(0x0, "NAND"),		/* rdy_busy3 */
		TOP_MUX(0x1, "UART5"),		/* txd */
		TOP_MUX(0x2, "USIM0"),		/* dat */
		TOP_MUX(0x3, "BGPIO"),		/* gpio95 */
		TOP_MUX(0x4, "TSI1"),		/* valid */
		TOP_MUX(0x4, "I2S1")),		/* dout3 */

	/* top_pmm_reg_8 */
	TOP_PIN(GMAC_125M_IN, TOP_REG8, 0, 2, 0x34, 0,
		TOP_MUX(0x0, "GMII"),		/* 125m_in */
		TOP_MUX(0x1, "USB2"),		/* 0_drvvbus */
		TOP_MUX(0x2, "ISP"),		/* ref_clk */
		TOP_MUX(0x3, "BGPIO")),		/* gpio96 */
	TOP_PIN(GMAC_50M_OUT, TOP_REG8, 2, 2, 0x34, 9,
		TOP_MUX(0x0, "GMII"),		/* 50m_out */
		TOP_MUX(0x1, "USB2"),		/* 1_drvvbus */
		TOP_MUX(0x2, "BGPIO"),		/* gpio97 */
		TOP_MUX(0x3, "USB2")),		/* 0_drvvbus */
	TOP_PIN(SPINOR_SSCLK_LOOPBACK, TOP_REG8, 6, 1, 0xc8, 9,
		TOP_MUX(0x0, "SPINOR")),	/* sdio1_clk_i */
	TOP_PIN(SPINOR_SDIO1CLK_LOOPBACK, TOP_REG8, 7, 1, 0xc8, 18,
		TOP_MUX(0x0, "SPINOR")),	/* ssclk_i */
};

static struct zx_pinctrl_soc_info zx296718_pinctrl_info = {
	.pins = zx296718_pins,
	.npins = ARRAY_SIZE(zx296718_pins),
};

static int zx296718_pinctrl_probe(struct platform_device *pdev)
{
	return zx_pinctrl_init(pdev, &zx296718_pinctrl_info);
}

static const struct of_device_id zx296718_pinctrl_match[] = {
	{ .compatible = "zte,zx296718-pmm", },
	{}
};
MODULE_DEVICE_TABLE(of, zx296718_pinctrl_match);

static struct platform_driver zx296718_pinctrl_driver = {
	.probe  = zx296718_pinctrl_probe,
	.driver = {
		.name = "zx296718-pinctrl",
		.of_match_table = zx296718_pinctrl_match,
	},
};
builtin_platform_driver(zx296718_pinctrl_driver);

MODULE_DESCRIPTION("ZTE ZX296718 pinctrl driver");
MODULE_LICENSE("GPL");
