// SPDX-License-Identifier: GPL-2.0-only
/*
 * Pinctrl data for Wondermedia WM8505 SoC
 *
 * Copyright (c) 2013 Tony Prisk <linux@prisktech.co.nz>
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
static const struct wmt_pinctrl_bank_registers wm8505_banks[] = {
	WMT_PINCTRL_BANK(0x64, 0x8C, 0xB4, 0xDC, NO_REG, NO_REG),	/* 0 */
	WMT_PINCTRL_BANK(0x40, 0x68, 0x90, 0xB8, NO_REG, NO_REG),	/* 1 */
	WMT_PINCTRL_BANK(0x44, 0x6C, 0x94, 0xBC, NO_REG, NO_REG),	/* 2 */
	WMT_PINCTRL_BANK(0x48, 0x70, 0x98, 0xC0, NO_REG, NO_REG),	/* 3 */
	WMT_PINCTRL_BANK(0x4C, 0x74, 0x9C, 0xC4, NO_REG, NO_REG),	/* 4 */
	WMT_PINCTRL_BANK(0x50, 0x78, 0xA0, 0xC8, NO_REG, NO_REG),	/* 5 */
	WMT_PINCTRL_BANK(0x54, 0x7C, 0xA4, 0xD0, NO_REG, NO_REG),	/* 6 */
	WMT_PINCTRL_BANK(0x58, 0x80, 0xA8, 0xD4, NO_REG, NO_REG),	/* 7 */
	WMT_PINCTRL_BANK(0x5C, 0x84, 0xAC, 0xD8, NO_REG, NO_REG),	/* 8 */
	WMT_PINCTRL_BANK(0x60, 0x88, 0xB0, 0xDC, NO_REG, NO_REG),	/* 9 */
	WMT_PINCTRL_BANK(0x500, 0x504, 0x508, 0x50C, NO_REG, NO_REG),	/* 10 */
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
#define WMT_PIN_WAKEUP2		WMT_PIN(0, 18)
#define WMT_PIN_WAKEUP3		WMT_PIN(0, 19)
#define WMT_PIN_SUSGPIO0	WMT_PIN(0, 21)
#define WMT_PIN_SDDATA0		WMT_PIN(1, 0)
#define WMT_PIN_SDDATA1		WMT_PIN(1, 1)
#define WMT_PIN_SDDATA2		WMT_PIN(1, 2)
#define WMT_PIN_SDDATA3		WMT_PIN(1, 3)
#define WMT_PIN_MMCDATA0	WMT_PIN(1, 4)
#define WMT_PIN_MMCDATA1	WMT_PIN(1, 5)
#define WMT_PIN_MMCDATA2	WMT_PIN(1, 6)
#define WMT_PIN_MMCDATA3	WMT_PIN(1, 7)
#define WMT_PIN_VDIN0		WMT_PIN(2, 0)
#define WMT_PIN_VDIN1		WMT_PIN(2, 1)
#define WMT_PIN_VDIN2		WMT_PIN(2, 2)
#define WMT_PIN_VDIN3		WMT_PIN(2, 3)
#define WMT_PIN_VDIN4		WMT_PIN(2, 4)
#define WMT_PIN_VDIN5		WMT_PIN(2, 5)
#define WMT_PIN_VDIN6		WMT_PIN(2, 6)
#define WMT_PIN_VDIN7		WMT_PIN(2, 7)
#define WMT_PIN_VDOUT0		WMT_PIN(2, 8)
#define WMT_PIN_VDOUT1		WMT_PIN(2, 9)
#define WMT_PIN_VDOUT2		WMT_PIN(2, 10)
#define WMT_PIN_VDOUT3		WMT_PIN(2, 11)
#define WMT_PIN_VDOUT4		WMT_PIN(2, 12)
#define WMT_PIN_VDOUT5		WMT_PIN(2, 13)
#define WMT_PIN_VDOUT6		WMT_PIN(2, 14)
#define WMT_PIN_VDOUT7		WMT_PIN(2, 15)
#define WMT_PIN_VDOUT8		WMT_PIN(2, 16)
#define WMT_PIN_VDOUT9		WMT_PIN(2, 17)
#define WMT_PIN_VDOUT10		WMT_PIN(2, 18)
#define WMT_PIN_VDOUT11		WMT_PIN(2, 19)
#define WMT_PIN_VDOUT12		WMT_PIN(2, 20)
#define WMT_PIN_VDOUT13		WMT_PIN(2, 21)
#define WMT_PIN_VDOUT14		WMT_PIN(2, 22)
#define WMT_PIN_VDOUT15		WMT_PIN(2, 23)
#define WMT_PIN_VDOUT16		WMT_PIN(2, 24)
#define WMT_PIN_VDOUT17		WMT_PIN(2, 25)
#define WMT_PIN_VDOUT18		WMT_PIN(2, 26)
#define WMT_PIN_VDOUT19		WMT_PIN(2, 27)
#define WMT_PIN_VDOUT20		WMT_PIN(2, 28)
#define WMT_PIN_VDOUT21		WMT_PIN(2, 29)
#define WMT_PIN_VDOUT22		WMT_PIN(2, 30)
#define WMT_PIN_VDOUT23		WMT_PIN(2, 31)
#define WMT_PIN_VHSYNC		WMT_PIN(3, 0)
#define WMT_PIN_VVSYNC		WMT_PIN(3, 1)
#define WMT_PIN_VGAHSYNC	WMT_PIN(3, 2)
#define WMT_PIN_VGAVSYNC	WMT_PIN(3, 3)
#define WMT_PIN_VDHSYNC		WMT_PIN(3, 4)
#define WMT_PIN_VDVSYNC		WMT_PIN(3, 5)
#define WMT_PIN_NORD0		WMT_PIN(4, 0)
#define WMT_PIN_NORD1		WMT_PIN(4, 1)
#define WMT_PIN_NORD2		WMT_PIN(4, 2)
#define WMT_PIN_NORD3		WMT_PIN(4, 3)
#define WMT_PIN_NORD4		WMT_PIN(4, 4)
#define WMT_PIN_NORD5		WMT_PIN(4, 5)
#define WMT_PIN_NORD6		WMT_PIN(4, 6)
#define WMT_PIN_NORD7		WMT_PIN(4, 7)
#define WMT_PIN_NORD8		WMT_PIN(4, 8)
#define WMT_PIN_NORD9		WMT_PIN(4, 9)
#define WMT_PIN_NORD10		WMT_PIN(4, 10)
#define WMT_PIN_NORD11		WMT_PIN(4, 11)
#define WMT_PIN_NORD12		WMT_PIN(4, 12)
#define WMT_PIN_NORD13		WMT_PIN(4, 13)
#define WMT_PIN_NORD14		WMT_PIN(4, 14)
#define WMT_PIN_NORD15		WMT_PIN(4, 15)
#define WMT_PIN_NORA0		WMT_PIN(5, 0)
#define WMT_PIN_NORA1		WMT_PIN(5, 1)
#define WMT_PIN_NORA2		WMT_PIN(5, 2)
#define WMT_PIN_NORA3		WMT_PIN(5, 3)
#define WMT_PIN_NORA4		WMT_PIN(5, 4)
#define WMT_PIN_NORA5		WMT_PIN(5, 5)
#define WMT_PIN_NORA6		WMT_PIN(5, 6)
#define WMT_PIN_NORA7		WMT_PIN(5, 7)
#define WMT_PIN_NORA8		WMT_PIN(5, 8)
#define WMT_PIN_NORA9		WMT_PIN(5, 9)
#define WMT_PIN_NORA10		WMT_PIN(5, 10)
#define WMT_PIN_NORA11		WMT_PIN(5, 11)
#define WMT_PIN_NORA12		WMT_PIN(5, 12)
#define WMT_PIN_NORA13		WMT_PIN(5, 13)
#define WMT_PIN_NORA14		WMT_PIN(5, 14)
#define WMT_PIN_NORA15		WMT_PIN(5, 15)
#define WMT_PIN_NORA16		WMT_PIN(5, 16)
#define WMT_PIN_NORA17		WMT_PIN(5, 17)
#define WMT_PIN_NORA18		WMT_PIN(5, 18)
#define WMT_PIN_NORA19		WMT_PIN(5, 19)
#define WMT_PIN_NORA20		WMT_PIN(5, 20)
#define WMT_PIN_NORA21		WMT_PIN(5, 21)
#define WMT_PIN_NORA22		WMT_PIN(5, 22)
#define WMT_PIN_NORA23		WMT_PIN(5, 23)
#define WMT_PIN_NORA24		WMT_PIN(5, 24)
#define WMT_PIN_AC97SDI		WMT_PIN(6, 0)
#define WMT_PIN_AC97SYNC	WMT_PIN(6, 1)
#define WMT_PIN_AC97SDO		WMT_PIN(6, 2)
#define WMT_PIN_AC97BCLK	WMT_PIN(6, 3)
#define WMT_PIN_AC97RST		WMT_PIN(6, 4)
#define WMT_PIN_SFDO		WMT_PIN(7, 0)
#define WMT_PIN_SFCS0		WMT_PIN(7, 1)
#define WMT_PIN_SFCS1		WMT_PIN(7, 2)
#define WMT_PIN_SFCLK		WMT_PIN(7, 3)
#define WMT_PIN_SFDI		WMT_PIN(7, 4)
#define WMT_PIN_SPI0CLK		WMT_PIN(8, 0)
#define WMT_PIN_SPI0MISO	WMT_PIN(8, 1)
#define WMT_PIN_SPI0MOSI	WMT_PIN(8, 2)
#define WMT_PIN_SPI0SS		WMT_PIN(8, 3)
#define WMT_PIN_SPI1CLK		WMT_PIN(8, 4)
#define WMT_PIN_SPI1MISO	WMT_PIN(8, 5)
#define WMT_PIN_SPI1MOSI	WMT_PIN(8, 6)
#define WMT_PIN_SPI1SS		WMT_PIN(8, 7)
#define WMT_PIN_SPI2CLK		WMT_PIN(8, 8)
#define WMT_PIN_SPI2MISO	WMT_PIN(8, 9)
#define WMT_PIN_SPI2MOSI	WMT_PIN(8, 10)
#define WMT_PIN_SPI2SS		WMT_PIN(8, 11)
#define WMT_PIN_UART0_RTS	WMT_PIN(9, 0)
#define WMT_PIN_UART0_TXD	WMT_PIN(9, 1)
#define WMT_PIN_UART0_CTS	WMT_PIN(9, 2)
#define WMT_PIN_UART0_RXD	WMT_PIN(9, 3)
#define WMT_PIN_UART1_RTS	WMT_PIN(9, 4)
#define WMT_PIN_UART1_TXD	WMT_PIN(9, 5)
#define WMT_PIN_UART1_CTS	WMT_PIN(9, 6)
#define WMT_PIN_UART1_RXD	WMT_PIN(9, 7)
#define WMT_PIN_UART2_RTS	WMT_PIN(9, 8)
#define WMT_PIN_UART2_TXD	WMT_PIN(9, 9)
#define WMT_PIN_UART2_CTS	WMT_PIN(9, 10)
#define WMT_PIN_UART2_RXD	WMT_PIN(9, 11)
#define WMT_PIN_UART3_RTS	WMT_PIN(9, 12)
#define WMT_PIN_UART3_TXD	WMT_PIN(9, 13)
#define WMT_PIN_UART3_CTS	WMT_PIN(9, 14)
#define WMT_PIN_UART3_RXD	WMT_PIN(9, 15)
#define WMT_PIN_I2C0SCL		WMT_PIN(10, 0)
#define WMT_PIN_I2C0SDA		WMT_PIN(10, 1)
#define WMT_PIN_I2C1SCL		WMT_PIN(10, 2)
#define WMT_PIN_I2C1SDA		WMT_PIN(10, 3)
#define WMT_PIN_I2C2SCL		WMT_PIN(10, 4)
#define WMT_PIN_I2C2SDA		WMT_PIN(10, 5)

static const struct pinctrl_pin_desc wm8505_pins[] = {
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
	PINCTRL_PIN(WMT_PIN_WAKEUP2, "wakeup2"),
	PINCTRL_PIN(WMT_PIN_WAKEUP3, "wakeup3"),
	PINCTRL_PIN(WMT_PIN_SUSGPIO0, "susgpio0"),
	PINCTRL_PIN(WMT_PIN_SDDATA0, "sd_data0"),
	PINCTRL_PIN(WMT_PIN_SDDATA1, "sd_data1"),
	PINCTRL_PIN(WMT_PIN_SDDATA2, "sd_data2"),
	PINCTRL_PIN(WMT_PIN_SDDATA3, "sd_data3"),
	PINCTRL_PIN(WMT_PIN_MMCDATA0, "mmc_data0"),
	PINCTRL_PIN(WMT_PIN_MMCDATA1, "mmc_data1"),
	PINCTRL_PIN(WMT_PIN_MMCDATA2, "mmc_data2"),
	PINCTRL_PIN(WMT_PIN_MMCDATA3, "mmc_data3"),
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
	PINCTRL_PIN(WMT_PIN_VHSYNC, "v_hsync"),
	PINCTRL_PIN(WMT_PIN_VVSYNC, "v_vsync"),
	PINCTRL_PIN(WMT_PIN_VGAHSYNC, "vga_hsync"),
	PINCTRL_PIN(WMT_PIN_VGAVSYNC, "vga_vsync"),
	PINCTRL_PIN(WMT_PIN_VDHSYNC, "vd_hsync"),
	PINCTRL_PIN(WMT_PIN_VDVSYNC, "vd_vsync"),
	PINCTRL_PIN(WMT_PIN_NORD0, "nor_d0"),
	PINCTRL_PIN(WMT_PIN_NORD1, "nor_d1"),
	PINCTRL_PIN(WMT_PIN_NORD2, "nor_d2"),
	PINCTRL_PIN(WMT_PIN_NORD3, "nor_d3"),
	PINCTRL_PIN(WMT_PIN_NORD4, "nor_d4"),
	PINCTRL_PIN(WMT_PIN_NORD5, "nor_d5"),
	PINCTRL_PIN(WMT_PIN_NORD6, "nor_d6"),
	PINCTRL_PIN(WMT_PIN_NORD7, "nor_d7"),
	PINCTRL_PIN(WMT_PIN_NORD8, "nor_d8"),
	PINCTRL_PIN(WMT_PIN_NORD9, "nor_d9"),
	PINCTRL_PIN(WMT_PIN_NORD10, "nor_d10"),
	PINCTRL_PIN(WMT_PIN_NORD11, "nor_d11"),
	PINCTRL_PIN(WMT_PIN_NORD12, "nor_d12"),
	PINCTRL_PIN(WMT_PIN_NORD13, "nor_d13"),
	PINCTRL_PIN(WMT_PIN_NORD14, "nor_d14"),
	PINCTRL_PIN(WMT_PIN_NORD15, "nor_d15"),
	PINCTRL_PIN(WMT_PIN_NORA0, "nor_a0"),
	PINCTRL_PIN(WMT_PIN_NORA1, "nor_a1"),
	PINCTRL_PIN(WMT_PIN_NORA2, "nor_a2"),
	PINCTRL_PIN(WMT_PIN_NORA3, "nor_a3"),
	PINCTRL_PIN(WMT_PIN_NORA4, "nor_a4"),
	PINCTRL_PIN(WMT_PIN_NORA5, "nor_a5"),
	PINCTRL_PIN(WMT_PIN_NORA6, "nor_a6"),
	PINCTRL_PIN(WMT_PIN_NORA7, "nor_a7"),
	PINCTRL_PIN(WMT_PIN_NORA8, "nor_a8"),
	PINCTRL_PIN(WMT_PIN_NORA9, "nor_a9"),
	PINCTRL_PIN(WMT_PIN_NORA10, "nor_a10"),
	PINCTRL_PIN(WMT_PIN_NORA11, "nor_a11"),
	PINCTRL_PIN(WMT_PIN_NORA12, "nor_a12"),
	PINCTRL_PIN(WMT_PIN_NORA13, "nor_a13"),
	PINCTRL_PIN(WMT_PIN_NORA14, "nor_a14"),
	PINCTRL_PIN(WMT_PIN_NORA15, "nor_a15"),
	PINCTRL_PIN(WMT_PIN_NORA16, "nor_a16"),
	PINCTRL_PIN(WMT_PIN_NORA17, "nor_a17"),
	PINCTRL_PIN(WMT_PIN_NORA18, "nor_a18"),
	PINCTRL_PIN(WMT_PIN_NORA19, "nor_a19"),
	PINCTRL_PIN(WMT_PIN_NORA20, "nor_a20"),
	PINCTRL_PIN(WMT_PIN_NORA21, "nor_a21"),
	PINCTRL_PIN(WMT_PIN_NORA22, "nor_a22"),
	PINCTRL_PIN(WMT_PIN_NORA23, "nor_a23"),
	PINCTRL_PIN(WMT_PIN_NORA24, "nor_a24"),
	PINCTRL_PIN(WMT_PIN_AC97SDI, "ac97_sdi"),
	PINCTRL_PIN(WMT_PIN_AC97SYNC, "ac97_sync"),
	PINCTRL_PIN(WMT_PIN_AC97SDO, "ac97_sdo"),
	PINCTRL_PIN(WMT_PIN_AC97BCLK, "ac97_bclk"),
	PINCTRL_PIN(WMT_PIN_AC97RST, "ac97_rst"),
	PINCTRL_PIN(WMT_PIN_SFDO, "sf_do"),
	PINCTRL_PIN(WMT_PIN_SFCS0, "sf_cs0"),
	PINCTRL_PIN(WMT_PIN_SFCS1, "sf_cs1"),
	PINCTRL_PIN(WMT_PIN_SFCLK, "sf_clk"),
	PINCTRL_PIN(WMT_PIN_SFDI, "sf_di"),
	PINCTRL_PIN(WMT_PIN_SPI0CLK, "spi0_clk"),
	PINCTRL_PIN(WMT_PIN_SPI0MISO, "spi0_miso"),
	PINCTRL_PIN(WMT_PIN_SPI0MOSI, "spi0_mosi"),
	PINCTRL_PIN(WMT_PIN_SPI0SS, "spi0_ss"),
	PINCTRL_PIN(WMT_PIN_SPI1CLK, "spi1_clk"),
	PINCTRL_PIN(WMT_PIN_SPI1MISO, "spi1_miso"),
	PINCTRL_PIN(WMT_PIN_SPI1MOSI, "spi1_mosi"),
	PINCTRL_PIN(WMT_PIN_SPI1SS, "spi1_ss"),
	PINCTRL_PIN(WMT_PIN_SPI2CLK, "spi2_clk"),
	PINCTRL_PIN(WMT_PIN_SPI2MISO, "spi2_miso"),
	PINCTRL_PIN(WMT_PIN_SPI2MOSI, "spi2_mosi"),
	PINCTRL_PIN(WMT_PIN_SPI2SS, "spi2_ss"),
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
	PINCTRL_PIN(WMT_PIN_I2C0SCL, "i2c0_scl"),
	PINCTRL_PIN(WMT_PIN_I2C0SDA, "i2c0_sda"),
	PINCTRL_PIN(WMT_PIN_I2C1SCL, "i2c1_scl"),
	PINCTRL_PIN(WMT_PIN_I2C1SDA, "i2c1_sda"),
	PINCTRL_PIN(WMT_PIN_I2C2SCL, "i2c2_scl"),
	PINCTRL_PIN(WMT_PIN_I2C2SDA, "i2c2_sda"),
};

/* Order of these names must match the above list */
static const char * const wm8505_groups[] = {
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
	"wakeup2",
	"wakeup3",
	"susgpio0",
	"sd_data0",
	"sd_data1",
	"sd_data2",
	"sd_data3",
	"mmc_data0",
	"mmc_data1",
	"mmc_data2",
	"mmc_data3",
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
	"v_hsync",
	"v_vsync",
	"vga_hsync",
	"vga_vsync",
	"vd_hsync",
	"vd_vsync",
	"nor_d0",
	"nor_d1",
	"nor_d2",
	"nor_d3",
	"nor_d4",
	"nor_d5",
	"nor_d6",
	"nor_d7",
	"nor_d8",
	"nor_d9",
	"nor_d10",
	"nor_d11",
	"nor_d12",
	"nor_d13",
	"nor_d14",
	"nor_d15",
	"nor_a0",
	"nor_a1",
	"nor_a2",
	"nor_a3",
	"nor_a4",
	"nor_a5",
	"nor_a6",
	"nor_a7",
	"nor_a8",
	"nor_a9",
	"nor_a10",
	"nor_a11",
	"nor_a12",
	"nor_a13",
	"nor_a14",
	"nor_a15",
	"nor_a16",
	"nor_a17",
	"nor_a18",
	"nor_a19",
	"nor_a20",
	"nor_a21",
	"nor_a22",
	"nor_a23",
	"nor_a24",
	"ac97_sdi",
	"ac97_sync",
	"ac97_sdo",
	"ac97_bclk",
	"ac97_rst",
	"sf_do",
	"sf_cs0",
	"sf_cs1",
	"sf_clk",
	"sf_di",
	"spi0_clk",
	"spi0_miso",
	"spi0_mosi",
	"spi0_ss",
	"spi1_clk",
	"spi1_miso",
	"spi1_mosi",
	"spi1_ss",
	"spi2_clk",
	"spi2_miso",
	"spi2_mosi",
	"spi2_ss",
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
	"i2c0_scl",
	"i2c0_sda",
	"i2c1_scl",
	"i2c1_sda",
	"i2c2_scl",
	"i2c2_sda",
};

static int wm8505_pinctrl_probe(struct platform_device *pdev)
{
	struct wmt_pinctrl_data *data;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->banks = wm8505_banks;
	data->nbanks = ARRAY_SIZE(wm8505_banks);
	data->pins = wm8505_pins;
	data->npins = ARRAY_SIZE(wm8505_pins);
	data->groups = wm8505_groups;
	data->ngroups = ARRAY_SIZE(wm8505_groups);

	return wmt_pinctrl_probe(pdev, data);
}

static const struct of_device_id wmt_pinctrl_of_match[] = {
	{ .compatible = "wm,wm8505-pinctrl" },
	{ /* sentinel */ },
};

static struct platform_driver wmt_pinctrl_driver = {
	.probe	= wm8505_pinctrl_probe,
	.driver = {
		.name	= "pinctrl-wm8505",
		.of_match_table	= wmt_pinctrl_of_match,
		.suppress_bind_attrs = true,
	},
};
builtin_platform_driver(wmt_pinctrl_driver);
