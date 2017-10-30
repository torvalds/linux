/*
 * Pinctrl data for Wondermedia WM8750 SoC
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
#include <linux/init.h>
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
static const struct wmt_pinctrl_bank_registers wm8750_banks[] = {
	WMT_PINCTRL_BANK(0x40, 0x80, 0xC0, 0x00, 0x480, 0x4C0),	/* 0 */
	WMT_PINCTRL_BANK(0x44, 0x84, 0xC4, 0x04, 0x484, 0x4C4),	/* 1 */
	WMT_PINCTRL_BANK(0x48, 0x88, 0xC8, 0x08, 0x488, 0x4C8),	/* 2 */
	WMT_PINCTRL_BANK(0x4C, 0x8C, 0xCC, 0x0C, 0x48C, 0x4CC),	/* 3 */
	WMT_PINCTRL_BANK(0x50, 0x90, 0xD0, 0x10, 0x490, 0x4D0),	/* 4 */
	WMT_PINCTRL_BANK(0x54, 0x94, 0xD4, 0x14, 0x494, 0x4D4),	/* 5 */
	WMT_PINCTRL_BANK(0x58, 0x98, 0xD8, 0x18, 0x498, 0x4D8),	/* 6 */
	WMT_PINCTRL_BANK(0x5C, 0x9C, 0xDC, 0x1C, 0x49C, 0x4DC),	/* 7 */
	WMT_PINCTRL_BANK(0x60, 0xA0, 0xE0, 0x20, 0x4A0, 0x4E0),	/* 8 */
	WMT_PINCTRL_BANK(0x70, 0xB0, 0xF0, 0x30, 0x4B0, 0x4F0),	/* 9 */
	WMT_PINCTRL_BANK(0x7C, 0xBC, 0xDC, 0x3C, 0x4BC, 0x4FC),	/* 10 */
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
#define WMT_PIN_WAKEUP0		WMT_PIN(0, 16)
#define WMT_PIN_WAKEUP1		WMT_PIN(0, 17)
#define WMT_PIN_SD0CD		WMT_PIN(0, 28)
#define WMT_PIN_VDOUT0		WMT_PIN(1, 0)
#define WMT_PIN_VDOUT1		WMT_PIN(1, 1)
#define WMT_PIN_VDOUT2		WMT_PIN(1, 2)
#define WMT_PIN_VDOUT3		WMT_PIN(1, 3)
#define WMT_PIN_VDOUT4		WMT_PIN(1, 4)
#define WMT_PIN_VDOUT5		WMT_PIN(1, 5)
#define WMT_PIN_VDOUT6		WMT_PIN(1, 6)
#define WMT_PIN_VDOUT7		WMT_PIN(1, 7)
#define WMT_PIN_VDOUT8		WMT_PIN(1, 8)
#define WMT_PIN_VDOUT9		WMT_PIN(1, 9)
#define WMT_PIN_VDOUT10		WMT_PIN(1, 10)
#define WMT_PIN_VDOUT11		WMT_PIN(1, 11)
#define WMT_PIN_VDOUT12		WMT_PIN(1, 12)
#define WMT_PIN_VDOUT13		WMT_PIN(1, 13)
#define WMT_PIN_VDOUT14		WMT_PIN(1, 14)
#define WMT_PIN_VDOUT15		WMT_PIN(1, 15)
#define WMT_PIN_VDOUT16		WMT_PIN(1, 16)
#define WMT_PIN_VDOUT17		WMT_PIN(1, 17)
#define WMT_PIN_VDOUT18		WMT_PIN(1, 18)
#define WMT_PIN_VDOUT19		WMT_PIN(1, 19)
#define WMT_PIN_VDOUT20		WMT_PIN(1, 20)
#define WMT_PIN_VDOUT21		WMT_PIN(1, 21)
#define WMT_PIN_VDOUT22		WMT_PIN(1, 22)
#define WMT_PIN_VDOUT23		WMT_PIN(1, 23)
#define WMT_PIN_VDIN0		WMT_PIN(2, 0)
#define WMT_PIN_VDIN1		WMT_PIN(2, 1)
#define WMT_PIN_VDIN2		WMT_PIN(2, 2)
#define WMT_PIN_VDIN3		WMT_PIN(2, 3)
#define WMT_PIN_VDIN4		WMT_PIN(2, 4)
#define WMT_PIN_VDIN5		WMT_PIN(2, 5)
#define WMT_PIN_VDIN6		WMT_PIN(2, 6)
#define WMT_PIN_VDIN7		WMT_PIN(2, 7)
#define WMT_PIN_SPI0_MOSI	WMT_PIN(2, 24)
#define WMT_PIN_SPI0_MISO	WMT_PIN(2, 25)
#define WMT_PIN_SPI0_SS		WMT_PIN(2, 26)
#define WMT_PIN_SPI0_CLK	WMT_PIN(2, 27)
#define WMT_PIN_SPI0_SSB	WMT_PIN(2, 28)
#define WMT_PIN_SD0CLK		WMT_PIN(3, 17)
#define WMT_PIN_SD0CMD		WMT_PIN(3, 18)
#define WMT_PIN_SD0WP		WMT_PIN(3, 19)
#define WMT_PIN_SD0DATA0	WMT_PIN(3, 20)
#define WMT_PIN_SD0DATA1	WMT_PIN(3, 21)
#define WMT_PIN_SD0DATA2	WMT_PIN(3, 22)
#define WMT_PIN_SD0DATA3	WMT_PIN(3, 23)
#define WMT_PIN_SD1DATA0	WMT_PIN(3, 24)
#define WMT_PIN_SD1DATA1	WMT_PIN(3, 25)
#define WMT_PIN_SD1DATA2	WMT_PIN(3, 26)
#define WMT_PIN_SD1DATA3	WMT_PIN(3, 27)
#define WMT_PIN_SD1DATA4	WMT_PIN(3, 28)
#define WMT_PIN_SD1DATA5	WMT_PIN(3, 29)
#define WMT_PIN_SD1DATA6	WMT_PIN(3, 30)
#define WMT_PIN_SD1DATA7	WMT_PIN(3, 31)
#define WMT_PIN_I2C0_SCL	WMT_PIN(5, 8)
#define WMT_PIN_I2C0_SDA	WMT_PIN(5, 9)
#define WMT_PIN_I2C1_SCL	WMT_PIN(5, 10)
#define WMT_PIN_I2C1_SDA	WMT_PIN(5, 11)
#define WMT_PIN_I2C2_SCL	WMT_PIN(5, 12)
#define WMT_PIN_I2C2_SDA	WMT_PIN(5, 13)
#define WMT_PIN_UART0_RTS	WMT_PIN(5, 16)
#define WMT_PIN_UART0_TXD	WMT_PIN(5, 17)
#define WMT_PIN_UART0_CTS	WMT_PIN(5, 18)
#define WMT_PIN_UART0_RXD	WMT_PIN(5, 19)
#define WMT_PIN_UART1_RTS	WMT_PIN(5, 20)
#define WMT_PIN_UART1_TXD	WMT_PIN(5, 21)
#define WMT_PIN_UART1_CTS	WMT_PIN(5, 22)
#define WMT_PIN_UART1_RXD	WMT_PIN(5, 23)
#define WMT_PIN_UART2_RTS	WMT_PIN(5, 24)
#define WMT_PIN_UART2_TXD	WMT_PIN(5, 25)
#define WMT_PIN_UART2_CTS	WMT_PIN(5, 26)
#define WMT_PIN_UART2_RXD	WMT_PIN(5, 27)
#define WMT_PIN_UART3_RTS	WMT_PIN(5, 28)
#define WMT_PIN_UART3_TXD	WMT_PIN(5, 29)
#define WMT_PIN_UART3_CTS	WMT_PIN(5, 30)
#define WMT_PIN_UART3_RXD	WMT_PIN(5, 31)
#define WMT_PIN_SD2CD		WMT_PIN(6, 0)
#define WMT_PIN_SD2DATA3	WMT_PIN(6, 1)
#define WMT_PIN_SD2DATA0	WMT_PIN(6, 2)
#define WMT_PIN_SD2WP		WMT_PIN(6, 3)
#define WMT_PIN_SD2DATA1	WMT_PIN(6, 4)
#define WMT_PIN_SD2DATA2	WMT_PIN(6, 5)
#define WMT_PIN_SD2CMD		WMT_PIN(6, 6)
#define WMT_PIN_SD2CLK		WMT_PIN(6, 7)
#define WMT_PIN_SD2PWR		WMT_PIN(6, 9)
#define WMT_PIN_SD1CLK		WMT_PIN(7, 0)
#define WMT_PIN_SD1CMD		WMT_PIN(7, 1)
#define WMT_PIN_SD1PWR		WMT_PIN(7, 10)
#define WMT_PIN_SD1WP		WMT_PIN(7, 11)
#define WMT_PIN_SD1CD		WMT_PIN(7, 12)
#define WMT_PIN_SPI0SS3		WMT_PIN(7, 24)
#define WMT_PIN_SPI0SS2		WMT_PIN(7, 25)
#define WMT_PIN_PWMOUT1		WMT_PIN(7, 26)
#define WMT_PIN_PWMOUT0		WMT_PIN(7, 27)

static const struct pinctrl_pin_desc wm8750_pins[] = {
	PINCTRL_PIN(WMT_PIN_EXTGPIO0, "extgpio0"),
	PINCTRL_PIN(WMT_PIN_EXTGPIO1, "extgpio1"),
	PINCTRL_PIN(WMT_PIN_EXTGPIO2, "extgpio2"),
	PINCTRL_PIN(WMT_PIN_EXTGPIO3, "extgpio3"),
	PINCTRL_PIN(WMT_PIN_EXTGPIO4, "extgpio4"),
	PINCTRL_PIN(WMT_PIN_EXTGPIO5, "extgpio5"),
	PINCTRL_PIN(WMT_PIN_EXTGPIO6, "extgpio6"),
	PINCTRL_PIN(WMT_PIN_EXTGPIO7, "extgpio7"),
	PINCTRL_PIN(WMT_PIN_WAKEUP0, "wakeup0"),
	PINCTRL_PIN(WMT_PIN_WAKEUP1, "wakeup1"),
	PINCTRL_PIN(WMT_PIN_SD0CD, "sd0_cd"),
	PINCTRL_PIN(WMT_PIN_VDOUT0, "vdout0"),
	PINCTRL_PIN(WMT_PIN_VDOUT1, "vdout1"),
	PINCTRL_PIN(WMT_PIN_VDOUT2, "vdout2"),
	PINCTRL_PIN(WMT_PIN_VDOUT3, "vdout3"),
	PINCTRL_PIN(WMT_PIN_VDOUT4, "vdout4"),
	PINCTRL_PIN(WMT_PIN_VDOUT5, "vdout5"),
	PINCTRL_PIN(WMT_PIN_VDOUT6, "vdout6"),
	PINCTRL_PIN(WMT_PIN_VDOUT7, "vdout7"),
	PINCTRL_PIN(WMT_PIN_VDOUT8, "vdout8"),
	PINCTRL_PIN(WMT_PIN_VDOUT9, "vdout9"),
	PINCTRL_PIN(WMT_PIN_VDOUT10, "vdout10"),
	PINCTRL_PIN(WMT_PIN_VDOUT11, "vdout11"),
	PINCTRL_PIN(WMT_PIN_VDOUT12, "vdout12"),
	PINCTRL_PIN(WMT_PIN_VDOUT13, "vdout13"),
	PINCTRL_PIN(WMT_PIN_VDOUT14, "vdout14"),
	PINCTRL_PIN(WMT_PIN_VDOUT15, "vdout15"),
	PINCTRL_PIN(WMT_PIN_VDOUT16, "vdout16"),
	PINCTRL_PIN(WMT_PIN_VDOUT17, "vdout17"),
	PINCTRL_PIN(WMT_PIN_VDOUT18, "vdout18"),
	PINCTRL_PIN(WMT_PIN_VDOUT19, "vdout19"),
	PINCTRL_PIN(WMT_PIN_VDOUT20, "vdout20"),
	PINCTRL_PIN(WMT_PIN_VDOUT21, "vdout21"),
	PINCTRL_PIN(WMT_PIN_VDOUT22, "vdout22"),
	PINCTRL_PIN(WMT_PIN_VDOUT23, "vdout23"),
	PINCTRL_PIN(WMT_PIN_VDIN0, "vdin0"),
	PINCTRL_PIN(WMT_PIN_VDIN1, "vdin1"),
	PINCTRL_PIN(WMT_PIN_VDIN2, "vdin2"),
	PINCTRL_PIN(WMT_PIN_VDIN3, "vdin3"),
	PINCTRL_PIN(WMT_PIN_VDIN4, "vdin4"),
	PINCTRL_PIN(WMT_PIN_VDIN5, "vdin5"),
	PINCTRL_PIN(WMT_PIN_VDIN6, "vdin6"),
	PINCTRL_PIN(WMT_PIN_VDIN7, "vdin7"),
	PINCTRL_PIN(WMT_PIN_SPI0_MOSI, "spi0_mosi"),
	PINCTRL_PIN(WMT_PIN_SPI0_MISO, "spi0_miso"),
	PINCTRL_PIN(WMT_PIN_SPI0_SS, "spi0_ss"),
	PINCTRL_PIN(WMT_PIN_SPI0_CLK, "spi0_clk"),
	PINCTRL_PIN(WMT_PIN_SPI0_SSB, "spi0_ssb"),
	PINCTRL_PIN(WMT_PIN_SD0CLK, "sd0_clk"),
	PINCTRL_PIN(WMT_PIN_SD0CMD, "sd0_cmd"),
	PINCTRL_PIN(WMT_PIN_SD0WP, "sd0_wp"),
	PINCTRL_PIN(WMT_PIN_SD0DATA0, "sd0_data0"),
	PINCTRL_PIN(WMT_PIN_SD0DATA1, "sd0_data1"),
	PINCTRL_PIN(WMT_PIN_SD0DATA2, "sd0_data2"),
	PINCTRL_PIN(WMT_PIN_SD0DATA3, "sd0_data3"),
	PINCTRL_PIN(WMT_PIN_SD1DATA0, "sd1_data0"),
	PINCTRL_PIN(WMT_PIN_SD1DATA1, "sd1_data1"),
	PINCTRL_PIN(WMT_PIN_SD1DATA2, "sd1_data2"),
	PINCTRL_PIN(WMT_PIN_SD1DATA3, "sd1_data3"),
	PINCTRL_PIN(WMT_PIN_SD1DATA4, "sd1_data4"),
	PINCTRL_PIN(WMT_PIN_SD1DATA5, "sd1_data5"),
	PINCTRL_PIN(WMT_PIN_SD1DATA6, "sd1_data6"),
	PINCTRL_PIN(WMT_PIN_SD1DATA7, "sd1_data7"),
	PINCTRL_PIN(WMT_PIN_I2C0_SCL, "i2c0_scl"),
	PINCTRL_PIN(WMT_PIN_I2C0_SDA, "i2c0_sda"),
	PINCTRL_PIN(WMT_PIN_I2C1_SCL, "i2c1_scl"),
	PINCTRL_PIN(WMT_PIN_I2C1_SDA, "i2c1_sda"),
	PINCTRL_PIN(WMT_PIN_I2C2_SCL, "i2c2_scl"),
	PINCTRL_PIN(WMT_PIN_I2C2_SDA, "i2c2_sda"),
	PINCTRL_PIN(WMT_PIN_UART0_RTS, "uart0_rts"),
	PINCTRL_PIN(WMT_PIN_UART0_TXD, "uart0_txd"),
	PINCTRL_PIN(WMT_PIN_UART0_CTS, "uart0_cts"),
	PINCTRL_PIN(WMT_PIN_UART0_RXD, "uart0_rxd"),
	PINCTRL_PIN(WMT_PIN_UART1_RTS, "uart1_rts"),
	PINCTRL_PIN(WMT_PIN_UART1_TXD, "uart1_txd"),
	PINCTRL_PIN(WMT_PIN_UART1_CTS, "uart1_cts"),
	PINCTRL_PIN(WMT_PIN_UART1_RXD, "uart1_rxd"),
	PINCTRL_PIN(WMT_PIN_UART2_RTS, "uart2_rts"),
	PINCTRL_PIN(WMT_PIN_UART2_TXD, "uart2_txd"),
	PINCTRL_PIN(WMT_PIN_UART2_CTS, "uart2_cts"),
	PINCTRL_PIN(WMT_PIN_UART2_RXD, "uart2_rxd"),
	PINCTRL_PIN(WMT_PIN_UART3_RTS, "uart3_rts"),
	PINCTRL_PIN(WMT_PIN_UART3_TXD, "uart3_txd"),
	PINCTRL_PIN(WMT_PIN_UART3_CTS, "uart3_cts"),
	PINCTRL_PIN(WMT_PIN_UART3_RXD, "uart3_rxd"),
	PINCTRL_PIN(WMT_PIN_SD2CD, "sd2_cd"),
	PINCTRL_PIN(WMT_PIN_SD2DATA3, "sd2_data3"),
	PINCTRL_PIN(WMT_PIN_SD2DATA0, "sd2_data0"),
	PINCTRL_PIN(WMT_PIN_SD2WP, "sd2_wp"),
	PINCTRL_PIN(WMT_PIN_SD2DATA1, "sd2_data1"),
	PINCTRL_PIN(WMT_PIN_SD2DATA2, "sd2_data2"),
	PINCTRL_PIN(WMT_PIN_SD2CMD, "sd2_cmd"),
	PINCTRL_PIN(WMT_PIN_SD2CLK, "sd2_clk"),
	PINCTRL_PIN(WMT_PIN_SD2PWR, "sd2_pwr"),
	PINCTRL_PIN(WMT_PIN_SD1CLK, "sd1_clk"),
	PINCTRL_PIN(WMT_PIN_SD1CMD, "sd1_cmd"),
	PINCTRL_PIN(WMT_PIN_SD1PWR, "sd1_pwr"),
	PINCTRL_PIN(WMT_PIN_SD1WP, "sd1_wp"),
	PINCTRL_PIN(WMT_PIN_SD1CD, "sd1_cd"),
	PINCTRL_PIN(WMT_PIN_SPI0SS3, "spi0_ss3"),
	PINCTRL_PIN(WMT_PIN_SPI0SS2, "spi0_ss2"),
	PINCTRL_PIN(WMT_PIN_PWMOUT1, "pwmout1"),
	PINCTRL_PIN(WMT_PIN_PWMOUT0, "pwmout0"),
};

/* Order of these names must match the above list */
static const char * const wm8750_groups[] = {
	"extgpio0",
	"extgpio1",
	"extgpio2",
	"extgpio3",
	"extgpio4",
	"extgpio5",
	"extgpio6",
	"extgpio7",
	"wakeup0",
	"wakeup1",
	"sd0_cd",
	"vdout0",
	"vdout1",
	"vdout2",
	"vdout3",
	"vdout4",
	"vdout5",
	"vdout6",
	"vdout7",
	"vdout8",
	"vdout9",
	"vdout10",
	"vdout11",
	"vdout12",
	"vdout13",
	"vdout14",
	"vdout15",
	"vdout16",
	"vdout17",
	"vdout18",
	"vdout19",
	"vdout20",
	"vdout21",
	"vdout22",
	"vdout23",
	"vdin0",
	"vdin1",
	"vdin2",
	"vdin3",
	"vdin4",
	"vdin5",
	"vdin6",
	"vdin7",
	"spi0_mosi",
	"spi0_miso",
	"spi0_ss",
	"spi0_clk",
	"spi0_ssb",
	"sd0_clk",
	"sd0_cmd",
	"sd0_wp",
	"sd0_data0",
	"sd0_data1",
	"sd0_data2",
	"sd0_data3",
	"sd1_data0",
	"sd1_data1",
	"sd1_data2",
	"sd1_data3",
	"sd1_data4",
	"sd1_data5",
	"sd1_data6",
	"sd1_data7",
	"i2c0_scl",
	"i2c0_sda",
	"i2c1_scl",
	"i2c1_sda",
	"i2c2_scl",
	"i2c2_sda",
	"uart0_rts",
	"uart0_txd",
	"uart0_cts",
	"uart0_rxd",
	"uart1_rts",
	"uart1_txd",
	"uart1_cts",
	"uart1_rxd",
	"uart2_rts",
	"uart2_txd",
	"uart2_cts",
	"uart2_rxd",
	"uart3_rts",
	"uart3_txd",
	"uart3_cts",
	"uart3_rxd",
	"sd2_cd",
	"sd2_data3",
	"sd2_data0",
	"sd2_wp",
	"sd2_data1",
	"sd2_data2",
	"sd2_cmd",
	"sd2_clk",
	"sd2_pwr",
	"sd1_clk",
	"sd1_cmd",
	"sd1_pwr",
	"sd1_wp",
	"sd1_cd",
	"spi0_ss3",
	"spi0_ss2",
	"pwmout1",
	"pwmout0",
};

static int wm8750_pinctrl_probe(struct platform_device *pdev)
{
	struct wmt_pinctrl_data *data;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data) {
		dev_err(&pdev->dev, "failed to allocate data\n");
		return -ENOMEM;
	}

	data->banks = wm8750_banks;
	data->nbanks = ARRAY_SIZE(wm8750_banks);
	data->pins = wm8750_pins;
	data->npins = ARRAY_SIZE(wm8750_pins);
	data->groups = wm8750_groups;
	data->ngroups = ARRAY_SIZE(wm8750_groups);

	return wmt_pinctrl_probe(pdev, data);
}

static const struct of_device_id wmt_pinctrl_of_match[] = {
	{ .compatible = "wm,wm8750-pinctrl" },
	{ /* sentinel */ },
};

static struct platform_driver wmt_pinctrl_driver = {
	.probe	= wm8750_pinctrl_probe,
	.driver = {
		.name	= "pinctrl-wm8750",
		.of_match_table	= wmt_pinctrl_of_match,
		.suppress_bind_attrs = true,
	},
};
builtin_platform_driver(wmt_pinctrl_driver);
