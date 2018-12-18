/*
 * Pinctrl data for the NVIDIA Tegra124 pinmux
 *
 * Author: Ashwini Ghuge <aghuge@nvidia.com>
 *
 * Copyright (c) 2013-2014, NVIDIA CORPORATION.  All rights reserved.
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

#include <linux/init.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>

#include "pinctrl-tegra.h"

/*
 * Most pins affected by the pinmux can also be GPIOs. Define these first.
 * These must match how the GPIO driver names/numbers its pins.
 */
#define _GPIO(offset)				(offset)

#define TEGRA_PIN_CLK_32K_OUT_PA0		_GPIO(0)
#define TEGRA_PIN_UART3_CTS_N_PA1		_GPIO(1)
#define TEGRA_PIN_DAP2_FS_PA2			_GPIO(2)
#define TEGRA_PIN_DAP2_SCLK_PA3			_GPIO(3)
#define TEGRA_PIN_DAP2_DIN_PA4			_GPIO(4)
#define TEGRA_PIN_DAP2_DOUT_PA5			_GPIO(5)
#define TEGRA_PIN_SDMMC3_CLK_PA6		_GPIO(6)
#define TEGRA_PIN_SDMMC3_CMD_PA7		_GPIO(7)
#define TEGRA_PIN_PB0				_GPIO(8)
#define TEGRA_PIN_PB1				_GPIO(9)
#define TEGRA_PIN_SDMMC3_DAT3_PB4		_GPIO(12)
#define TEGRA_PIN_SDMMC3_DAT2_PB5		_GPIO(13)
#define TEGRA_PIN_SDMMC3_DAT1_PB6		_GPIO(14)
#define TEGRA_PIN_SDMMC3_DAT0_PB7		_GPIO(15)
#define TEGRA_PIN_UART3_RTS_N_PC0		_GPIO(16)
#define TEGRA_PIN_UART2_TXD_PC2			_GPIO(18)
#define TEGRA_PIN_UART2_RXD_PC3			_GPIO(19)
#define TEGRA_PIN_GEN1_I2C_SCL_PC4		_GPIO(20)
#define TEGRA_PIN_GEN1_I2C_SDA_PC5		_GPIO(21)
#define TEGRA_PIN_PC7				_GPIO(23)
#define TEGRA_PIN_PG0				_GPIO(48)
#define TEGRA_PIN_PG1				_GPIO(49)
#define TEGRA_PIN_PG2				_GPIO(50)
#define TEGRA_PIN_PG3				_GPIO(51)
#define TEGRA_PIN_PG4				_GPIO(52)
#define TEGRA_PIN_PG5				_GPIO(53)
#define TEGRA_PIN_PG6				_GPIO(54)
#define TEGRA_PIN_PG7				_GPIO(55)
#define TEGRA_PIN_PH0				_GPIO(56)
#define TEGRA_PIN_PH1				_GPIO(57)
#define TEGRA_PIN_PH2				_GPIO(58)
#define TEGRA_PIN_PH3				_GPIO(59)
#define TEGRA_PIN_PH4				_GPIO(60)
#define TEGRA_PIN_PH5				_GPIO(61)
#define TEGRA_PIN_PH6				_GPIO(62)
#define TEGRA_PIN_PH7				_GPIO(63)
#define TEGRA_PIN_PI0				_GPIO(64)
#define TEGRA_PIN_PI1				_GPIO(65)
#define TEGRA_PIN_PI2				_GPIO(66)
#define TEGRA_PIN_PI3				_GPIO(67)
#define TEGRA_PIN_PI4				_GPIO(68)
#define TEGRA_PIN_PI5				_GPIO(69)
#define TEGRA_PIN_PI6				_GPIO(70)
#define TEGRA_PIN_PI7				_GPIO(71)
#define TEGRA_PIN_PJ0				_GPIO(72)
#define TEGRA_PIN_PJ2				_GPIO(74)
#define TEGRA_PIN_UART2_CTS_N_PJ5		_GPIO(77)
#define TEGRA_PIN_UART2_RTS_N_PJ6		_GPIO(78)
#define TEGRA_PIN_PJ7				_GPIO(79)
#define TEGRA_PIN_PK0				_GPIO(80)
#define TEGRA_PIN_PK1				_GPIO(81)
#define TEGRA_PIN_PK2				_GPIO(82)
#define TEGRA_PIN_PK3				_GPIO(83)
#define TEGRA_PIN_PK4				_GPIO(84)
#define TEGRA_PIN_SPDIF_OUT_PK5			_GPIO(85)
#define TEGRA_PIN_SPDIF_IN_PK6			_GPIO(86)
#define TEGRA_PIN_PK7				_GPIO(87)
#define TEGRA_PIN_DAP1_FS_PN0			_GPIO(104)
#define TEGRA_PIN_DAP1_DIN_PN1			_GPIO(105)
#define TEGRA_PIN_DAP1_DOUT_PN2			_GPIO(106)
#define TEGRA_PIN_DAP1_SCLK_PN3			_GPIO(107)
#define TEGRA_PIN_USB_VBUS_EN0_PN4		_GPIO(108)
#define TEGRA_PIN_USB_VBUS_EN1_PN5		_GPIO(109)
#define TEGRA_PIN_HDMI_INT_PN7			_GPIO(111)
#define TEGRA_PIN_ULPI_DATA7_PO0		_GPIO(112)
#define TEGRA_PIN_ULPI_DATA0_PO1		_GPIO(113)
#define TEGRA_PIN_ULPI_DATA1_PO2		_GPIO(114)
#define TEGRA_PIN_ULPI_DATA2_PO3		_GPIO(115)
#define TEGRA_PIN_ULPI_DATA3_PO4		_GPIO(116)
#define TEGRA_PIN_ULPI_DATA4_PO5		_GPIO(117)
#define TEGRA_PIN_ULPI_DATA5_PO6		_GPIO(118)
#define TEGRA_PIN_ULPI_DATA6_PO7		_GPIO(119)
#define TEGRA_PIN_DAP3_FS_PP0			_GPIO(120)
#define TEGRA_PIN_DAP3_DIN_PP1			_GPIO(121)
#define TEGRA_PIN_DAP3_DOUT_PP2			_GPIO(122)
#define TEGRA_PIN_DAP3_SCLK_PP3			_GPIO(123)
#define TEGRA_PIN_DAP4_FS_PP4			_GPIO(124)
#define TEGRA_PIN_DAP4_DIN_PP5			_GPIO(125)
#define TEGRA_PIN_DAP4_DOUT_PP6			_GPIO(126)
#define TEGRA_PIN_DAP4_SCLK_PP7			_GPIO(127)
#define TEGRA_PIN_KB_COL0_PQ0			_GPIO(128)
#define TEGRA_PIN_KB_COL1_PQ1			_GPIO(129)
#define TEGRA_PIN_KB_COL2_PQ2			_GPIO(130)
#define TEGRA_PIN_KB_COL3_PQ3			_GPIO(131)
#define TEGRA_PIN_KB_COL4_PQ4			_GPIO(132)
#define TEGRA_PIN_KB_COL5_PQ5			_GPIO(133)
#define TEGRA_PIN_KB_COL6_PQ6			_GPIO(134)
#define TEGRA_PIN_KB_COL7_PQ7			_GPIO(135)
#define TEGRA_PIN_KB_ROW0_PR0			_GPIO(136)
#define TEGRA_PIN_KB_ROW1_PR1			_GPIO(137)
#define TEGRA_PIN_KB_ROW2_PR2			_GPIO(138)
#define TEGRA_PIN_KB_ROW3_PR3			_GPIO(139)
#define TEGRA_PIN_KB_ROW4_PR4			_GPIO(140)
#define TEGRA_PIN_KB_ROW5_PR5			_GPIO(141)
#define TEGRA_PIN_KB_ROW6_PR6			_GPIO(142)
#define TEGRA_PIN_KB_ROW7_PR7			_GPIO(143)
#define TEGRA_PIN_KB_ROW8_PS0			_GPIO(144)
#define TEGRA_PIN_KB_ROW9_PS1			_GPIO(145)
#define TEGRA_PIN_KB_ROW10_PS2			_GPIO(146)
#define TEGRA_PIN_KB_ROW11_PS3			_GPIO(147)
#define TEGRA_PIN_KB_ROW12_PS4			_GPIO(148)
#define TEGRA_PIN_KB_ROW13_PS5			_GPIO(149)
#define TEGRA_PIN_KB_ROW14_PS6			_GPIO(150)
#define TEGRA_PIN_KB_ROW15_PS7			_GPIO(151)
#define TEGRA_PIN_KB_ROW16_PT0			_GPIO(152)
#define TEGRA_PIN_KB_ROW17_PT1			_GPIO(153)
#define TEGRA_PIN_GEN2_I2C_SCL_PT5		_GPIO(157)
#define TEGRA_PIN_GEN2_I2C_SDA_PT6		_GPIO(158)
#define TEGRA_PIN_SDMMC4_CMD_PT7		_GPIO(159)
#define TEGRA_PIN_PU0				_GPIO(160)
#define TEGRA_PIN_PU1				_GPIO(161)
#define TEGRA_PIN_PU2				_GPIO(162)
#define TEGRA_PIN_PU3				_GPIO(163)
#define TEGRA_PIN_PU4				_GPIO(164)
#define TEGRA_PIN_PU5				_GPIO(165)
#define TEGRA_PIN_PU6				_GPIO(166)
#define TEGRA_PIN_PV0				_GPIO(168)
#define TEGRA_PIN_PV1				_GPIO(169)
#define TEGRA_PIN_SDMMC3_CD_N_PV2		_GPIO(170)
#define TEGRA_PIN_SDMMC1_WP_N_PV3		_GPIO(171)
#define TEGRA_PIN_DDC_SCL_PV4			_GPIO(172)
#define TEGRA_PIN_DDC_SDA_PV5			_GPIO(173)
#define TEGRA_PIN_GPIO_W2_AUD_PW2		_GPIO(178)
#define TEGRA_PIN_GPIO_W3_AUD_PW3		_GPIO(179)
#define TEGRA_PIN_DAP_MCLK1_PW4			_GPIO(180)
#define TEGRA_PIN_CLK2_OUT_PW5			_GPIO(181)
#define TEGRA_PIN_UART3_TXD_PW6			_GPIO(182)
#define TEGRA_PIN_UART3_RXD_PW7			_GPIO(183)
#define TEGRA_PIN_DVFS_PWM_PX0			_GPIO(184)
#define TEGRA_PIN_GPIO_X1_AUD_PX1		_GPIO(185)
#define TEGRA_PIN_DVFS_CLK_PX2			_GPIO(186)
#define TEGRA_PIN_GPIO_X3_AUD_PX3		_GPIO(187)
#define TEGRA_PIN_GPIO_X4_AUD_PX4		_GPIO(188)
#define TEGRA_PIN_GPIO_X5_AUD_PX5		_GPIO(189)
#define TEGRA_PIN_GPIO_X6_AUD_PX6		_GPIO(190)
#define TEGRA_PIN_GPIO_X7_AUD_PX7		_GPIO(191)
#define TEGRA_PIN_ULPI_CLK_PY0			_GPIO(192)
#define TEGRA_PIN_ULPI_DIR_PY1			_GPIO(193)
#define TEGRA_PIN_ULPI_NXT_PY2			_GPIO(194)
#define TEGRA_PIN_ULPI_STP_PY3			_GPIO(195)
#define TEGRA_PIN_SDMMC1_DAT3_PY4		_GPIO(196)
#define TEGRA_PIN_SDMMC1_DAT2_PY5		_GPIO(197)
#define TEGRA_PIN_SDMMC1_DAT1_PY6		_GPIO(198)
#define TEGRA_PIN_SDMMC1_DAT0_PY7		_GPIO(199)
#define TEGRA_PIN_SDMMC1_CLK_PZ0		_GPIO(200)
#define TEGRA_PIN_SDMMC1_CMD_PZ1		_GPIO(201)
#define TEGRA_PIN_PWR_I2C_SCL_PZ6		_GPIO(206)
#define TEGRA_PIN_PWR_I2C_SDA_PZ7		_GPIO(207)
#define TEGRA_PIN_SDMMC4_DAT0_PAA0		_GPIO(208)
#define TEGRA_PIN_SDMMC4_DAT1_PAA1		_GPIO(209)
#define TEGRA_PIN_SDMMC4_DAT2_PAA2		_GPIO(210)
#define TEGRA_PIN_SDMMC4_DAT3_PAA3		_GPIO(211)
#define TEGRA_PIN_SDMMC4_DAT4_PAA4		_GPIO(212)
#define TEGRA_PIN_SDMMC4_DAT5_PAA5		_GPIO(213)
#define TEGRA_PIN_SDMMC4_DAT6_PAA6		_GPIO(214)
#define TEGRA_PIN_SDMMC4_DAT7_PAA7		_GPIO(215)
#define TEGRA_PIN_PBB0				_GPIO(216)
#define TEGRA_PIN_CAM_I2C_SCL_PBB1		_GPIO(217)
#define TEGRA_PIN_CAM_I2C_SDA_PBB2		_GPIO(218)
#define TEGRA_PIN_PBB3				_GPIO(219)
#define TEGRA_PIN_PBB4				_GPIO(220)
#define TEGRA_PIN_PBB5				_GPIO(221)
#define TEGRA_PIN_PBB6				_GPIO(222)
#define TEGRA_PIN_PBB7				_GPIO(223)
#define TEGRA_PIN_CAM_MCLK_PCC0			_GPIO(224)
#define TEGRA_PIN_PCC1				_GPIO(225)
#define TEGRA_PIN_PCC2				_GPIO(226)
#define TEGRA_PIN_SDMMC4_CLK_PCC4		_GPIO(228)
#define TEGRA_PIN_CLK2_REQ_PCC5			_GPIO(229)
#define TEGRA_PIN_PEX_L0_RST_N_PDD1		_GPIO(233)
#define TEGRA_PIN_PEX_L0_CLKREQ_N_PDD2		_GPIO(234)
#define TEGRA_PIN_PEX_WAKE_N_PDD3		_GPIO(235)
#define TEGRA_PIN_PEX_L1_RST_N_PDD5		_GPIO(237)
#define TEGRA_PIN_PEX_L1_CLKREQ_N_PDD6		_GPIO(238)
#define TEGRA_PIN_CLK3_OUT_PEE0			_GPIO(240)
#define TEGRA_PIN_CLK3_REQ_PEE1			_GPIO(241)
#define TEGRA_PIN_DAP_MCLK1_REQ_PEE2		_GPIO(242)
#define TEGRA_PIN_HDMI_CEC_PEE3			_GPIO(243)
#define TEGRA_PIN_SDMMC3_CLK_LB_OUT_PEE4	_GPIO(244)
#define TEGRA_PIN_SDMMC3_CLK_LB_IN_PEE5		_GPIO(245)
#define TEGRA_PIN_DP_HPD_PFF0			_GPIO(248)
#define TEGRA_PIN_USB_VBUS_EN2_PFF1		_GPIO(249)
#define TEGRA_PIN_PFF2				_GPIO(250)

/* All non-GPIO pins follow */
#define NUM_GPIOS				(TEGRA_PIN_PFF2 + 1)
#define _PIN(offset)				(NUM_GPIOS + (offset))

/* Non-GPIO pins */
#define TEGRA_PIN_CORE_PWR_REQ			_PIN(0)
#define TEGRA_PIN_CPU_PWR_REQ			_PIN(1)
#define TEGRA_PIN_PWR_INT_N			_PIN(2)
#define TEGRA_PIN_GMI_CLK_LB			_PIN(3)
#define TEGRA_PIN_RESET_OUT_N			_PIN(4)
#define TEGRA_PIN_OWR				_PIN(5)
#define TEGRA_PIN_CLK_32K_IN			_PIN(6)
#define TEGRA_PIN_JTAG_RTCK			_PIN(7)
#define TEGRA_PIN_DSI_B_CLK_P			_PIN(8)
#define TEGRA_PIN_DSI_B_CLK_N			_PIN(9)
#define TEGRA_PIN_DSI_B_D0_P			_PIN(10)
#define TEGRA_PIN_DSI_B_D0_N			_PIN(11)
#define TEGRA_PIN_DSI_B_D1_P			_PIN(12)
#define TEGRA_PIN_DSI_B_D1_N			_PIN(13)
#define TEGRA_PIN_DSI_B_D2_P			_PIN(14)
#define TEGRA_PIN_DSI_B_D2_N			_PIN(15)
#define TEGRA_PIN_DSI_B_D3_P			_PIN(16)
#define TEGRA_PIN_DSI_B_D3_N			_PIN(17)

static const struct pinctrl_pin_desc tegra124_pins[] = {
	PINCTRL_PIN(TEGRA_PIN_CLK_32K_OUT_PA0, "CLK_32K_OUT PA0"),
	PINCTRL_PIN(TEGRA_PIN_UART3_CTS_N_PA1, "UART3_CTS_N PA1"),
	PINCTRL_PIN(TEGRA_PIN_DAP2_FS_PA2, "DAP2_FS PA2"),
	PINCTRL_PIN(TEGRA_PIN_DAP2_SCLK_PA3, "DAP2_SCLK PA3"),
	PINCTRL_PIN(TEGRA_PIN_DAP2_DIN_PA4, "DAP2_DIN PA4"),
	PINCTRL_PIN(TEGRA_PIN_DAP2_DOUT_PA5, "DAP2_DOUT PA5"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC3_CLK_PA6, "SDMMC3_CLK PA6"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC3_CMD_PA7, "SDMMC3_CMD PA7"),
	PINCTRL_PIN(TEGRA_PIN_PB0, "PB0"),
	PINCTRL_PIN(TEGRA_PIN_PB1, "PB1"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC3_DAT3_PB4, "SDMMC3_DAT3 PB4"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC3_DAT2_PB5, "SDMMC3_DAT2 PB5"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC3_DAT1_PB6, "SDMMC3_DAT1 PB6"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC3_DAT0_PB7, "SDMMC3_DAT0 PB7"),
	PINCTRL_PIN(TEGRA_PIN_UART3_RTS_N_PC0, "UART3_RTS_N PC0"),
	PINCTRL_PIN(TEGRA_PIN_UART2_TXD_PC2, "UART2_TXD PC2"),
	PINCTRL_PIN(TEGRA_PIN_UART2_RXD_PC3, "UART2_RXD PC3"),
	PINCTRL_PIN(TEGRA_PIN_GEN1_I2C_SCL_PC4, "GEN1_I2C_SCL PC4"),
	PINCTRL_PIN(TEGRA_PIN_GEN1_I2C_SDA_PC5, "GEN1_I2C_SDA PC5"),
	PINCTRL_PIN(TEGRA_PIN_PC7, "PC7"),
	PINCTRL_PIN(TEGRA_PIN_PG0, "PG0"),
	PINCTRL_PIN(TEGRA_PIN_PG1, "PG1"),
	PINCTRL_PIN(TEGRA_PIN_PG2, "PG2"),
	PINCTRL_PIN(TEGRA_PIN_PG3, "PG3"),
	PINCTRL_PIN(TEGRA_PIN_PG4, "PG4"),
	PINCTRL_PIN(TEGRA_PIN_PG5, "PG5"),
	PINCTRL_PIN(TEGRA_PIN_PG6, "PG6"),
	PINCTRL_PIN(TEGRA_PIN_PG7, "PG7"),
	PINCTRL_PIN(TEGRA_PIN_PH0, "PH0"),
	PINCTRL_PIN(TEGRA_PIN_PH1, "PH1"),
	PINCTRL_PIN(TEGRA_PIN_PH2, "PH2"),
	PINCTRL_PIN(TEGRA_PIN_PH3, "PH3"),
	PINCTRL_PIN(TEGRA_PIN_PH4, "PH4"),
	PINCTRL_PIN(TEGRA_PIN_PH5, "PH5"),
	PINCTRL_PIN(TEGRA_PIN_PH6, "PH6"),
	PINCTRL_PIN(TEGRA_PIN_PH7, "PH7"),
	PINCTRL_PIN(TEGRA_PIN_PI0, "PI0"),
	PINCTRL_PIN(TEGRA_PIN_PI1, "PI1"),
	PINCTRL_PIN(TEGRA_PIN_PI2, "PI2"),
	PINCTRL_PIN(TEGRA_PIN_PI3, "PI3"),
	PINCTRL_PIN(TEGRA_PIN_PI4, "PI4"),
	PINCTRL_PIN(TEGRA_PIN_PI5, "PI5"),
	PINCTRL_PIN(TEGRA_PIN_PI6, "PI6"),
	PINCTRL_PIN(TEGRA_PIN_PI7, "PI7"),
	PINCTRL_PIN(TEGRA_PIN_PJ0, "PJ0"),
	PINCTRL_PIN(TEGRA_PIN_PJ2, "PJ2"),
	PINCTRL_PIN(TEGRA_PIN_UART2_CTS_N_PJ5, "UART2_CTS_N PJ5"),
	PINCTRL_PIN(TEGRA_PIN_UART2_RTS_N_PJ6, "UART2_RTS_N PJ6"),
	PINCTRL_PIN(TEGRA_PIN_PJ7, "PJ7"),
	PINCTRL_PIN(TEGRA_PIN_PK0, "PK0"),
	PINCTRL_PIN(TEGRA_PIN_PK1, "PK1"),
	PINCTRL_PIN(TEGRA_PIN_PK2, "PK2"),
	PINCTRL_PIN(TEGRA_PIN_PK3, "PK3"),
	PINCTRL_PIN(TEGRA_PIN_PK4, "PK4"),
	PINCTRL_PIN(TEGRA_PIN_SPDIF_OUT_PK5, "SPDIF_OUT PK5"),
	PINCTRL_PIN(TEGRA_PIN_SPDIF_IN_PK6, "SPDIF_IN PK6"),
	PINCTRL_PIN(TEGRA_PIN_PK7, "PK7"),
	PINCTRL_PIN(TEGRA_PIN_DAP1_FS_PN0, "DAP1_FS PN0"),
	PINCTRL_PIN(TEGRA_PIN_DAP1_DIN_PN1, "DAP1_DIN PN1"),
	PINCTRL_PIN(TEGRA_PIN_DAP1_DOUT_PN2, "DAP1_DOUT PN2"),
	PINCTRL_PIN(TEGRA_PIN_DAP1_SCLK_PN3, "DAP1_SCLK PN3"),
	PINCTRL_PIN(TEGRA_PIN_USB_VBUS_EN0_PN4, "USB_VBUS_EN0 PN4"),
	PINCTRL_PIN(TEGRA_PIN_USB_VBUS_EN1_PN5, "USB_VBUS_EN1 PN5"),
	PINCTRL_PIN(TEGRA_PIN_HDMI_INT_PN7, "HDMI_INT PN7"),
	PINCTRL_PIN(TEGRA_PIN_ULPI_DATA7_PO0, "ULPI_DATA7 PO0"),
	PINCTRL_PIN(TEGRA_PIN_ULPI_DATA0_PO1, "ULPI_DATA0 PO1"),
	PINCTRL_PIN(TEGRA_PIN_ULPI_DATA1_PO2, "ULPI_DATA1 PO2"),
	PINCTRL_PIN(TEGRA_PIN_ULPI_DATA2_PO3, "ULPI_DATA2 PO3"),
	PINCTRL_PIN(TEGRA_PIN_ULPI_DATA3_PO4, "ULPI_DATA3 PO4"),
	PINCTRL_PIN(TEGRA_PIN_ULPI_DATA4_PO5, "ULPI_DATA4 PO5"),
	PINCTRL_PIN(TEGRA_PIN_ULPI_DATA5_PO6, "ULPI_DATA5 PO6"),
	PINCTRL_PIN(TEGRA_PIN_ULPI_DATA6_PO7, "ULPI_DATA6 PO7"),
	PINCTRL_PIN(TEGRA_PIN_DAP3_FS_PP0, "DAP3_FS PP0"),
	PINCTRL_PIN(TEGRA_PIN_DAP3_DIN_PP1, "DAP3_DIN PP1"),
	PINCTRL_PIN(TEGRA_PIN_DAP3_DOUT_PP2, "DAP3_DOUT PP2"),
	PINCTRL_PIN(TEGRA_PIN_DAP3_SCLK_PP3, "DAP3_SCLK PP3"),
	PINCTRL_PIN(TEGRA_PIN_DAP4_FS_PP4, "DAP4_FS PP4"),
	PINCTRL_PIN(TEGRA_PIN_DAP4_DIN_PP5, "DAP4_DIN PP5"),
	PINCTRL_PIN(TEGRA_PIN_DAP4_DOUT_PP6, "DAP4_DOUT PP6"),
	PINCTRL_PIN(TEGRA_PIN_DAP4_SCLK_PP7, "DAP4_SCLK PP7"),
	PINCTRL_PIN(TEGRA_PIN_KB_COL0_PQ0, "KB_COL0 PQ0"),
	PINCTRL_PIN(TEGRA_PIN_KB_COL1_PQ1, "KB_COL1 PQ1"),
	PINCTRL_PIN(TEGRA_PIN_KB_COL2_PQ2, "KB_COL2 PQ2"),
	PINCTRL_PIN(TEGRA_PIN_KB_COL3_PQ3, "KB_COL3 PQ3"),
	PINCTRL_PIN(TEGRA_PIN_KB_COL4_PQ4, "KB_COL4 PQ4"),
	PINCTRL_PIN(TEGRA_PIN_KB_COL5_PQ5, "KB_COL5 PQ5"),
	PINCTRL_PIN(TEGRA_PIN_KB_COL6_PQ6, "KB_COL6 PQ6"),
	PINCTRL_PIN(TEGRA_PIN_KB_COL7_PQ7, "KB_COL7 PQ7"),
	PINCTRL_PIN(TEGRA_PIN_KB_ROW0_PR0, "KB_ROW0 PR0"),
	PINCTRL_PIN(TEGRA_PIN_KB_ROW1_PR1, "KB_ROW1 PR1"),
	PINCTRL_PIN(TEGRA_PIN_KB_ROW2_PR2, "KB_ROW2 PR2"),
	PINCTRL_PIN(TEGRA_PIN_KB_ROW3_PR3, "KB_ROW3 PR3"),
	PINCTRL_PIN(TEGRA_PIN_KB_ROW4_PR4, "KB_ROW4 PR4"),
	PINCTRL_PIN(TEGRA_PIN_KB_ROW5_PR5, "KB_ROW5 PR5"),
	PINCTRL_PIN(TEGRA_PIN_KB_ROW6_PR6, "KB_ROW6 PR6"),
	PINCTRL_PIN(TEGRA_PIN_KB_ROW7_PR7, "KB_ROW7 PR7"),
	PINCTRL_PIN(TEGRA_PIN_KB_ROW8_PS0, "KB_ROW8 PS0"),
	PINCTRL_PIN(TEGRA_PIN_KB_ROW9_PS1, "KB_ROW9 PS1"),
	PINCTRL_PIN(TEGRA_PIN_KB_ROW10_PS2, "KB_ROW10 PS2"),
	PINCTRL_PIN(TEGRA_PIN_KB_ROW11_PS3, "KB_ROW11 PS3"),
	PINCTRL_PIN(TEGRA_PIN_KB_ROW12_PS4, "KB_ROW12 PS4"),
	PINCTRL_PIN(TEGRA_PIN_KB_ROW13_PS5, "KB_ROW13 PS5"),
	PINCTRL_PIN(TEGRA_PIN_KB_ROW14_PS6, "KB_ROW14 PS6"),
	PINCTRL_PIN(TEGRA_PIN_KB_ROW15_PS7, "KB_ROW15 PS7"),
	PINCTRL_PIN(TEGRA_PIN_KB_ROW16_PT0, "KB_ROW16 PT0"),
	PINCTRL_PIN(TEGRA_PIN_KB_ROW17_PT1, "KB_ROW17 PT1"),
	PINCTRL_PIN(TEGRA_PIN_GEN2_I2C_SCL_PT5, "GEN2_I2C_SCL PT5"),
	PINCTRL_PIN(TEGRA_PIN_GEN2_I2C_SDA_PT6, "GEN2_I2C_SDA PT6"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC4_CMD_PT7, "SDMMC4_CMD PT7"),
	PINCTRL_PIN(TEGRA_PIN_PU0, "PU0"),
	PINCTRL_PIN(TEGRA_PIN_PU1, "PU1"),
	PINCTRL_PIN(TEGRA_PIN_PU2, "PU2"),
	PINCTRL_PIN(TEGRA_PIN_PU3, "PU3"),
	PINCTRL_PIN(TEGRA_PIN_PU4, "PU4"),
	PINCTRL_PIN(TEGRA_PIN_PU5, "PU5"),
	PINCTRL_PIN(TEGRA_PIN_PU6, "PU6"),
	PINCTRL_PIN(TEGRA_PIN_PV0, "PV0"),
	PINCTRL_PIN(TEGRA_PIN_PV1, "PV1"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC3_CD_N_PV2, "SDMMC3_CD_N PV2"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC1_WP_N_PV3, "SDMMC1_WP_N PV3"),
	PINCTRL_PIN(TEGRA_PIN_DDC_SCL_PV4, "DDC_SCL PV4"),
	PINCTRL_PIN(TEGRA_PIN_DDC_SDA_PV5, "DDC_SDA PV5"),
	PINCTRL_PIN(TEGRA_PIN_GPIO_W2_AUD_PW2, "GPIO_W2_AUD PW2"),
	PINCTRL_PIN(TEGRA_PIN_GPIO_W3_AUD_PW3, "GPIO_W3_AUD PW3"),
	PINCTRL_PIN(TEGRA_PIN_DAP_MCLK1_PW4, "DAP_MCLK1 PW4"),
	PINCTRL_PIN(TEGRA_PIN_CLK2_OUT_PW5, "CLK2_OUT PW5"),
	PINCTRL_PIN(TEGRA_PIN_UART3_TXD_PW6, "UART3_TXD PW6"),
	PINCTRL_PIN(TEGRA_PIN_UART3_RXD_PW7, "UART3_RXD PW7"),
	PINCTRL_PIN(TEGRA_PIN_DVFS_PWM_PX0, "DVFS_PWM PX0"),
	PINCTRL_PIN(TEGRA_PIN_GPIO_X1_AUD_PX1, "GPIO_X1_AUD PX1"),
	PINCTRL_PIN(TEGRA_PIN_DVFS_CLK_PX2, "DVFS_CLK PX2"),
	PINCTRL_PIN(TEGRA_PIN_GPIO_X3_AUD_PX3, "GPIO_X3_AUD PX3"),
	PINCTRL_PIN(TEGRA_PIN_GPIO_X4_AUD_PX4, "GPIO_X4_AUD PX4"),
	PINCTRL_PIN(TEGRA_PIN_GPIO_X5_AUD_PX5, "GPIO_X5_AUD PX5"),
	PINCTRL_PIN(TEGRA_PIN_GPIO_X6_AUD_PX6, "GPIO_X6_AUD PX6"),
	PINCTRL_PIN(TEGRA_PIN_GPIO_X7_AUD_PX7, "GPIO_X7_AUD PX7"),
	PINCTRL_PIN(TEGRA_PIN_ULPI_CLK_PY0, "ULPI_CLK PY0"),
	PINCTRL_PIN(TEGRA_PIN_ULPI_DIR_PY1, "ULPI_DIR PY1"),
	PINCTRL_PIN(TEGRA_PIN_ULPI_NXT_PY2, "ULPI_NXT PY2"),
	PINCTRL_PIN(TEGRA_PIN_ULPI_STP_PY3, "ULPI_STP PY3"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC1_DAT3_PY4, "SDMMC1_DAT3 PY4"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC1_DAT2_PY5, "SDMMC1_DAT2 PY5"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC1_DAT1_PY6, "SDMMC1_DAT1 PY6"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC1_DAT0_PY7, "SDMMC1_DAT0 PY7"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC1_CLK_PZ0, "SDMMC1_CLK PZ0"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC1_CMD_PZ1, "SDMMC1_CMD PZ1"),
	PINCTRL_PIN(TEGRA_PIN_PWR_I2C_SCL_PZ6, "PWR_I2C_SCL PZ6"),
	PINCTRL_PIN(TEGRA_PIN_PWR_I2C_SDA_PZ7, "PWR_I2C_SDA PZ7"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC4_DAT0_PAA0, "SDMMC4_DAT0 PAA0"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC4_DAT1_PAA1, "SDMMC4_DAT1 PAA1"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC4_DAT2_PAA2, "SDMMC4_DAT2 PAA2"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC4_DAT3_PAA3, "SDMMC4_DAT3 PAA3"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC4_DAT4_PAA4, "SDMMC4_DAT4 PAA4"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC4_DAT5_PAA5, "SDMMC4_DAT5 PAA5"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC4_DAT6_PAA6, "SDMMC4_DAT6 PAA6"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC4_DAT7_PAA7, "SDMMC4_DAT7 PAA7"),
	PINCTRL_PIN(TEGRA_PIN_PBB0, "PBB0"),
	PINCTRL_PIN(TEGRA_PIN_CAM_I2C_SCL_PBB1, "CAM_I2C_SCL PBB1"),
	PINCTRL_PIN(TEGRA_PIN_CAM_I2C_SDA_PBB2, "CAM_I2C_SDA PBB2"),
	PINCTRL_PIN(TEGRA_PIN_PBB3, "PBB3"),
	PINCTRL_PIN(TEGRA_PIN_PBB4, "PBB4"),
	PINCTRL_PIN(TEGRA_PIN_PBB5, "PBB5"),
	PINCTRL_PIN(TEGRA_PIN_PBB6, "PBB6"),
	PINCTRL_PIN(TEGRA_PIN_PBB7, "PBB7"),
	PINCTRL_PIN(TEGRA_PIN_CAM_MCLK_PCC0, "CAM_MCLK PCC0"),
	PINCTRL_PIN(TEGRA_PIN_PCC1, "PCC1"),
	PINCTRL_PIN(TEGRA_PIN_PCC2, "PCC2"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC4_CLK_PCC4, "SDMMC4_CLK PCC4"),
	PINCTRL_PIN(TEGRA_PIN_CLK2_REQ_PCC5, "CLK2_REQ PCC5"),
	PINCTRL_PIN(TEGRA_PIN_PEX_L0_RST_N_PDD1, "PEX_L0_RST_N PDD1"),
	PINCTRL_PIN(TEGRA_PIN_PEX_L0_CLKREQ_N_PDD2, "PEX_L0_CLKREQ_N PDD2"),
	PINCTRL_PIN(TEGRA_PIN_PEX_WAKE_N_PDD3, "PEX_WAKE_N PDD3"),
	PINCTRL_PIN(TEGRA_PIN_PEX_L1_RST_N_PDD5, "PEX_L1_RST_N PDD5"),
	PINCTRL_PIN(TEGRA_PIN_PEX_L1_CLKREQ_N_PDD6, "PEX_L1_CLKREQ_N PDD6"),
	PINCTRL_PIN(TEGRA_PIN_CLK3_OUT_PEE0, "CLK3_OUT PEE0"),
	PINCTRL_PIN(TEGRA_PIN_CLK3_REQ_PEE1, "CLK3_REQ PEE1"),
	PINCTRL_PIN(TEGRA_PIN_DAP_MCLK1_REQ_PEE2, "DAP_MCLK1_REQ PEE2"),
	PINCTRL_PIN(TEGRA_PIN_HDMI_CEC_PEE3, "HDMI_CEC PEE3"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC3_CLK_LB_OUT_PEE4, "SDMMC3_CLK_LB_OUT PEE4"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC3_CLK_LB_IN_PEE5, "SDMMC3_CLK_LB_IN PEE5"),
	PINCTRL_PIN(TEGRA_PIN_DP_HPD_PFF0, "DP_HPD PFF0"),
	PINCTRL_PIN(TEGRA_PIN_USB_VBUS_EN2_PFF1, "USB_VBUS_EN2 PFF1"),
	PINCTRL_PIN(TEGRA_PIN_PFF2, "PFF2"),
	PINCTRL_PIN(TEGRA_PIN_CORE_PWR_REQ, "CORE_PWR_REQ"),
	PINCTRL_PIN(TEGRA_PIN_CPU_PWR_REQ, "CPU_PWR_REQ"),
	PINCTRL_PIN(TEGRA_PIN_PWR_INT_N, "PWR_INT_N"),
	PINCTRL_PIN(TEGRA_PIN_GMI_CLK_LB, "GMI_CLK_LB"),
	PINCTRL_PIN(TEGRA_PIN_RESET_OUT_N, "RESET_OUT_N"),
	PINCTRL_PIN(TEGRA_PIN_OWR, "OWR"),
	PINCTRL_PIN(TEGRA_PIN_CLK_32K_IN, "CLK_32K_IN"),
	PINCTRL_PIN(TEGRA_PIN_JTAG_RTCK, "JTAG_RTCK"),
	PINCTRL_PIN(TEGRA_PIN_DSI_B_CLK_P, "DSI_B_CLK_P"),
	PINCTRL_PIN(TEGRA_PIN_DSI_B_CLK_N, "DSI_B_CLK_N"),
	PINCTRL_PIN(TEGRA_PIN_DSI_B_D0_P, "DSI_B_D0_P"),
	PINCTRL_PIN(TEGRA_PIN_DSI_B_D0_N, "DSI_B_D0_N"),
	PINCTRL_PIN(TEGRA_PIN_DSI_B_D1_P, "DSI_B_D1_P"),
	PINCTRL_PIN(TEGRA_PIN_DSI_B_D1_N, "DSI_B_D1_N"),
	PINCTRL_PIN(TEGRA_PIN_DSI_B_D2_P, "DSI_B_D2_P"),
	PINCTRL_PIN(TEGRA_PIN_DSI_B_D2_N, "DSI_B_D2_N"),
	PINCTRL_PIN(TEGRA_PIN_DSI_B_D3_P, "DSI_B_D3_P"),
	PINCTRL_PIN(TEGRA_PIN_DSI_B_D3_N, "DSI_B_D3_N"),
};

static const unsigned clk_32k_out_pa0_pins[] = {
	TEGRA_PIN_CLK_32K_OUT_PA0,
};

static const unsigned uart3_cts_n_pa1_pins[] = {
	TEGRA_PIN_UART3_CTS_N_PA1,
};

static const unsigned dap2_fs_pa2_pins[] = {
	TEGRA_PIN_DAP2_FS_PA2,
};

static const unsigned dap2_sclk_pa3_pins[] = {
	TEGRA_PIN_DAP2_SCLK_PA3,
};

static const unsigned dap2_din_pa4_pins[] = {
	TEGRA_PIN_DAP2_DIN_PA4,
};

static const unsigned dap2_dout_pa5_pins[] = {
	TEGRA_PIN_DAP2_DOUT_PA5,
};

static const unsigned sdmmc3_clk_pa6_pins[] = {
	TEGRA_PIN_SDMMC3_CLK_PA6,
};

static const unsigned sdmmc3_cmd_pa7_pins[] = {
	TEGRA_PIN_SDMMC3_CMD_PA7,
};

static const unsigned pb0_pins[] = {
	TEGRA_PIN_PB0,
};

static const unsigned pb1_pins[] = {
	TEGRA_PIN_PB1,
};

static const unsigned sdmmc3_dat3_pb4_pins[] = {
	TEGRA_PIN_SDMMC3_DAT3_PB4,
};

static const unsigned sdmmc3_dat2_pb5_pins[] = {
	TEGRA_PIN_SDMMC3_DAT2_PB5,
};

static const unsigned sdmmc3_dat1_pb6_pins[] = {
	TEGRA_PIN_SDMMC3_DAT1_PB6,
};

static const unsigned sdmmc3_dat0_pb7_pins[] = {
	TEGRA_PIN_SDMMC3_DAT0_PB7,
};

static const unsigned uart3_rts_n_pc0_pins[] = {
	TEGRA_PIN_UART3_RTS_N_PC0,
};

static const unsigned uart2_txd_pc2_pins[] = {
	TEGRA_PIN_UART2_TXD_PC2,
};

static const unsigned uart2_rxd_pc3_pins[] = {
	TEGRA_PIN_UART2_RXD_PC3,
};

static const unsigned gen1_i2c_scl_pc4_pins[] = {
	TEGRA_PIN_GEN1_I2C_SCL_PC4,
};

static const unsigned gen1_i2c_sda_pc5_pins[] = {
	TEGRA_PIN_GEN1_I2C_SDA_PC5,
};

static const unsigned pc7_pins[] = {
	TEGRA_PIN_PC7,
};

static const unsigned pg0_pins[] = {
	TEGRA_PIN_PG0,
};

static const unsigned pg1_pins[] = {
	TEGRA_PIN_PG1,
};

static const unsigned pg2_pins[] = {
	TEGRA_PIN_PG2,
};

static const unsigned pg3_pins[] = {
	TEGRA_PIN_PG3,
};

static const unsigned pg4_pins[] = {
	TEGRA_PIN_PG4,
};

static const unsigned pg5_pins[] = {
	TEGRA_PIN_PG5,
};

static const unsigned pg6_pins[] = {
	TEGRA_PIN_PG6,
};

static const unsigned pg7_pins[] = {
	TEGRA_PIN_PG7,
};

static const unsigned ph0_pins[] = {
	TEGRA_PIN_PH0,
};

static const unsigned ph1_pins[] = {
	TEGRA_PIN_PH1,
};

static const unsigned ph2_pins[] = {
	TEGRA_PIN_PH2,
};

static const unsigned ph3_pins[] = {
	TEGRA_PIN_PH3,
};

static const unsigned ph4_pins[] = {
	TEGRA_PIN_PH4,
};

static const unsigned ph5_pins[] = {
	TEGRA_PIN_PH5,
};

static const unsigned ph6_pins[] = {
	TEGRA_PIN_PH6,
};

static const unsigned ph7_pins[] = {
	TEGRA_PIN_PH7,
};

static const unsigned pi0_pins[] = {
	TEGRA_PIN_PI0,
};

static const unsigned pi1_pins[] = {
	TEGRA_PIN_PI1,
};

static const unsigned pi2_pins[] = {
	TEGRA_PIN_PI2,
};

static const unsigned pi3_pins[] = {
	TEGRA_PIN_PI3,
};

static const unsigned pi4_pins[] = {
	TEGRA_PIN_PI4,
};

static const unsigned pi5_pins[] = {
	TEGRA_PIN_PI5,
};

static const unsigned pi6_pins[] = {
	TEGRA_PIN_PI6,
};

static const unsigned pi7_pins[] = {
	TEGRA_PIN_PI7,
};

static const unsigned pj0_pins[] = {
	TEGRA_PIN_PJ0,
};

static const unsigned pj2_pins[] = {
	TEGRA_PIN_PJ2,
};

static const unsigned uart2_cts_n_pj5_pins[] = {
	TEGRA_PIN_UART2_CTS_N_PJ5,
};

static const unsigned uart2_rts_n_pj6_pins[] = {
	TEGRA_PIN_UART2_RTS_N_PJ6,
};

static const unsigned pj7_pins[] = {
	TEGRA_PIN_PJ7,
};

static const unsigned pk0_pins[] = {
	TEGRA_PIN_PK0,
};

static const unsigned pk1_pins[] = {
	TEGRA_PIN_PK1,
};

static const unsigned pk2_pins[] = {
	TEGRA_PIN_PK2,
};

static const unsigned pk3_pins[] = {
	TEGRA_PIN_PK3,
};

static const unsigned pk4_pins[] = {
	TEGRA_PIN_PK4,
};

static const unsigned spdif_out_pk5_pins[] = {
	TEGRA_PIN_SPDIF_OUT_PK5,
};

static const unsigned spdif_in_pk6_pins[] = {
	TEGRA_PIN_SPDIF_IN_PK6,
};

static const unsigned pk7_pins[] = {
	TEGRA_PIN_PK7,
};

static const unsigned dap1_fs_pn0_pins[] = {
	TEGRA_PIN_DAP1_FS_PN0,
};

static const unsigned dap1_din_pn1_pins[] = {
	TEGRA_PIN_DAP1_DIN_PN1,
};

static const unsigned dap1_dout_pn2_pins[] = {
	TEGRA_PIN_DAP1_DOUT_PN2,
};

static const unsigned dap1_sclk_pn3_pins[] = {
	TEGRA_PIN_DAP1_SCLK_PN3,
};

static const unsigned usb_vbus_en0_pn4_pins[] = {
	TEGRA_PIN_USB_VBUS_EN0_PN4,
};

static const unsigned usb_vbus_en1_pn5_pins[] = {
	TEGRA_PIN_USB_VBUS_EN1_PN5,
};

static const unsigned hdmi_int_pn7_pins[] = {
	TEGRA_PIN_HDMI_INT_PN7,
};

static const unsigned ulpi_data7_po0_pins[] = {
	TEGRA_PIN_ULPI_DATA7_PO0,
};

static const unsigned ulpi_data0_po1_pins[] = {
	TEGRA_PIN_ULPI_DATA0_PO1,
};

static const unsigned ulpi_data1_po2_pins[] = {
	TEGRA_PIN_ULPI_DATA1_PO2,
};

static const unsigned ulpi_data2_po3_pins[] = {
	TEGRA_PIN_ULPI_DATA2_PO3,
};

static const unsigned ulpi_data3_po4_pins[] = {
	TEGRA_PIN_ULPI_DATA3_PO4,
};

static const unsigned ulpi_data4_po5_pins[] = {
	TEGRA_PIN_ULPI_DATA4_PO5,
};

static const unsigned ulpi_data5_po6_pins[] = {
	TEGRA_PIN_ULPI_DATA5_PO6,
};

static const unsigned ulpi_data6_po7_pins[] = {
	TEGRA_PIN_ULPI_DATA6_PO7,
};

static const unsigned dap3_fs_pp0_pins[] = {
	TEGRA_PIN_DAP3_FS_PP0,
};

static const unsigned dap3_din_pp1_pins[] = {
	TEGRA_PIN_DAP3_DIN_PP1,
};

static const unsigned dap3_dout_pp2_pins[] = {
	TEGRA_PIN_DAP3_DOUT_PP2,
};

static const unsigned dap3_sclk_pp3_pins[] = {
	TEGRA_PIN_DAP3_SCLK_PP3,
};

static const unsigned dap4_fs_pp4_pins[] = {
	TEGRA_PIN_DAP4_FS_PP4,
};

static const unsigned dap4_din_pp5_pins[] = {
	TEGRA_PIN_DAP4_DIN_PP5,
};

static const unsigned dap4_dout_pp6_pins[] = {
	TEGRA_PIN_DAP4_DOUT_PP6,
};

static const unsigned dap4_sclk_pp7_pins[] = {
	TEGRA_PIN_DAP4_SCLK_PP7,
};

static const unsigned kb_col0_pq0_pins[] = {
	TEGRA_PIN_KB_COL0_PQ0,
};

static const unsigned kb_col1_pq1_pins[] = {
	TEGRA_PIN_KB_COL1_PQ1,
};

static const unsigned kb_col2_pq2_pins[] = {
	TEGRA_PIN_KB_COL2_PQ2,
};

static const unsigned kb_col3_pq3_pins[] = {
	TEGRA_PIN_KB_COL3_PQ3,
};

static const unsigned kb_col4_pq4_pins[] = {
	TEGRA_PIN_KB_COL4_PQ4,
};

static const unsigned kb_col5_pq5_pins[] = {
	TEGRA_PIN_KB_COL5_PQ5,
};

static const unsigned kb_col6_pq6_pins[] = {
	TEGRA_PIN_KB_COL6_PQ6,
};

static const unsigned kb_col7_pq7_pins[] = {
	TEGRA_PIN_KB_COL7_PQ7,
};

static const unsigned kb_row0_pr0_pins[] = {
	TEGRA_PIN_KB_ROW0_PR0,
};

static const unsigned kb_row1_pr1_pins[] = {
	TEGRA_PIN_KB_ROW1_PR1,
};

static const unsigned kb_row2_pr2_pins[] = {
	TEGRA_PIN_KB_ROW2_PR2,
};

static const unsigned kb_row3_pr3_pins[] = {
	TEGRA_PIN_KB_ROW3_PR3,
};

static const unsigned kb_row4_pr4_pins[] = {
	TEGRA_PIN_KB_ROW4_PR4,
};

static const unsigned kb_row5_pr5_pins[] = {
	TEGRA_PIN_KB_ROW5_PR5,
};

static const unsigned kb_row6_pr6_pins[] = {
	TEGRA_PIN_KB_ROW6_PR6,
};

static const unsigned kb_row7_pr7_pins[] = {
	TEGRA_PIN_KB_ROW7_PR7,
};

static const unsigned kb_row8_ps0_pins[] = {
	TEGRA_PIN_KB_ROW8_PS0,
};

static const unsigned kb_row9_ps1_pins[] = {
	TEGRA_PIN_KB_ROW9_PS1,
};

static const unsigned kb_row10_ps2_pins[] = {
	TEGRA_PIN_KB_ROW10_PS2,
};

static const unsigned kb_row11_ps3_pins[] = {
	TEGRA_PIN_KB_ROW11_PS3,
};

static const unsigned kb_row12_ps4_pins[] = {
	TEGRA_PIN_KB_ROW12_PS4,
};

static const unsigned kb_row13_ps5_pins[] = {
	TEGRA_PIN_KB_ROW13_PS5,
};

static const unsigned kb_row14_ps6_pins[] = {
	TEGRA_PIN_KB_ROW14_PS6,
};

static const unsigned kb_row15_ps7_pins[] = {
	TEGRA_PIN_KB_ROW15_PS7,
};

static const unsigned kb_row16_pt0_pins[] = {
	TEGRA_PIN_KB_ROW16_PT0,
};

static const unsigned kb_row17_pt1_pins[] = {
	TEGRA_PIN_KB_ROW17_PT1,
};

static const unsigned gen2_i2c_scl_pt5_pins[] = {
	TEGRA_PIN_GEN2_I2C_SCL_PT5,
};

static const unsigned gen2_i2c_sda_pt6_pins[] = {
	TEGRA_PIN_GEN2_I2C_SDA_PT6,
};

static const unsigned sdmmc4_cmd_pt7_pins[] = {
	TEGRA_PIN_SDMMC4_CMD_PT7,
};

static const unsigned pu0_pins[] = {
	TEGRA_PIN_PU0,
};

static const unsigned pu1_pins[] = {
	TEGRA_PIN_PU1,
};

static const unsigned pu2_pins[] = {
	TEGRA_PIN_PU2,
};

static const unsigned pu3_pins[] = {
	TEGRA_PIN_PU3,
};

static const unsigned pu4_pins[] = {
	TEGRA_PIN_PU4,
};

static const unsigned pu5_pins[] = {
	TEGRA_PIN_PU5,
};

static const unsigned pu6_pins[] = {
	TEGRA_PIN_PU6,
};

static const unsigned pv0_pins[] = {
	TEGRA_PIN_PV0,
};

static const unsigned pv1_pins[] = {
	TEGRA_PIN_PV1,
};

static const unsigned sdmmc3_cd_n_pv2_pins[] = {
	TEGRA_PIN_SDMMC3_CD_N_PV2,
};

static const unsigned sdmmc1_wp_n_pv3_pins[] = {
	TEGRA_PIN_SDMMC1_WP_N_PV3,
};

static const unsigned ddc_scl_pv4_pins[] = {
	TEGRA_PIN_DDC_SCL_PV4,
};

static const unsigned ddc_sda_pv5_pins[] = {
	TEGRA_PIN_DDC_SDA_PV5,
};

static const unsigned gpio_w2_aud_pw2_pins[] = {
	TEGRA_PIN_GPIO_W2_AUD_PW2,
};

static const unsigned gpio_w3_aud_pw3_pins[] = {
	TEGRA_PIN_GPIO_W3_AUD_PW3,
};

static const unsigned dap_mclk1_pw4_pins[] = {
	TEGRA_PIN_DAP_MCLK1_PW4,
};

static const unsigned clk2_out_pw5_pins[] = {
	TEGRA_PIN_CLK2_OUT_PW5,
};

static const unsigned uart3_txd_pw6_pins[] = {
	TEGRA_PIN_UART3_TXD_PW6,
};

static const unsigned uart3_rxd_pw7_pins[] = {
	TEGRA_PIN_UART3_RXD_PW7,
};

static const unsigned dvfs_pwm_px0_pins[] = {
	TEGRA_PIN_DVFS_PWM_PX0,
};

static const unsigned gpio_x1_aud_px1_pins[] = {
	TEGRA_PIN_GPIO_X1_AUD_PX1,
};

static const unsigned dvfs_clk_px2_pins[] = {
	TEGRA_PIN_DVFS_CLK_PX2,
};

static const unsigned gpio_x3_aud_px3_pins[] = {
	TEGRA_PIN_GPIO_X3_AUD_PX3,
};

static const unsigned gpio_x4_aud_px4_pins[] = {
	TEGRA_PIN_GPIO_X4_AUD_PX4,
};

static const unsigned gpio_x5_aud_px5_pins[] = {
	TEGRA_PIN_GPIO_X5_AUD_PX5,
};

static const unsigned gpio_x6_aud_px6_pins[] = {
	TEGRA_PIN_GPIO_X6_AUD_PX6,
};

static const unsigned gpio_x7_aud_px7_pins[] = {
	TEGRA_PIN_GPIO_X7_AUD_PX7,
};

static const unsigned ulpi_clk_py0_pins[] = {
	TEGRA_PIN_ULPI_CLK_PY0,
};

static const unsigned ulpi_dir_py1_pins[] = {
	TEGRA_PIN_ULPI_DIR_PY1,
};

static const unsigned ulpi_nxt_py2_pins[] = {
	TEGRA_PIN_ULPI_NXT_PY2,
};

static const unsigned ulpi_stp_py3_pins[] = {
	TEGRA_PIN_ULPI_STP_PY3,
};

static const unsigned sdmmc1_dat3_py4_pins[] = {
	TEGRA_PIN_SDMMC1_DAT3_PY4,
};

static const unsigned sdmmc1_dat2_py5_pins[] = {
	TEGRA_PIN_SDMMC1_DAT2_PY5,
};

static const unsigned sdmmc1_dat1_py6_pins[] = {
	TEGRA_PIN_SDMMC1_DAT1_PY6,
};

static const unsigned sdmmc1_dat0_py7_pins[] = {
	TEGRA_PIN_SDMMC1_DAT0_PY7,
};

static const unsigned sdmmc1_clk_pz0_pins[] = {
	TEGRA_PIN_SDMMC1_CLK_PZ0,
};

static const unsigned sdmmc1_cmd_pz1_pins[] = {
	TEGRA_PIN_SDMMC1_CMD_PZ1,
};

static const unsigned pwr_i2c_scl_pz6_pins[] = {
	TEGRA_PIN_PWR_I2C_SCL_PZ6,
};

static const unsigned pwr_i2c_sda_pz7_pins[] = {
	TEGRA_PIN_PWR_I2C_SDA_PZ7,
};

static const unsigned sdmmc4_dat0_paa0_pins[] = {
	TEGRA_PIN_SDMMC4_DAT0_PAA0,
};

static const unsigned sdmmc4_dat1_paa1_pins[] = {
	TEGRA_PIN_SDMMC4_DAT1_PAA1,
};

static const unsigned sdmmc4_dat2_paa2_pins[] = {
	TEGRA_PIN_SDMMC4_DAT2_PAA2,
};

static const unsigned sdmmc4_dat3_paa3_pins[] = {
	TEGRA_PIN_SDMMC4_DAT3_PAA3,
};

static const unsigned sdmmc4_dat4_paa4_pins[] = {
	TEGRA_PIN_SDMMC4_DAT4_PAA4,
};

static const unsigned sdmmc4_dat5_paa5_pins[] = {
	TEGRA_PIN_SDMMC4_DAT5_PAA5,
};

static const unsigned sdmmc4_dat6_paa6_pins[] = {
	TEGRA_PIN_SDMMC4_DAT6_PAA6,
};

static const unsigned sdmmc4_dat7_paa7_pins[] = {
	TEGRA_PIN_SDMMC4_DAT7_PAA7,
};

static const unsigned pbb0_pins[] = {
	TEGRA_PIN_PBB0,
};

static const unsigned cam_i2c_scl_pbb1_pins[] = {
	TEGRA_PIN_CAM_I2C_SCL_PBB1,
};

static const unsigned cam_i2c_sda_pbb2_pins[] = {
	TEGRA_PIN_CAM_I2C_SDA_PBB2,
};

static const unsigned pbb3_pins[] = {
	TEGRA_PIN_PBB3,
};

static const unsigned pbb4_pins[] = {
	TEGRA_PIN_PBB4,
};

static const unsigned pbb5_pins[] = {
	TEGRA_PIN_PBB5,
};

static const unsigned pbb6_pins[] = {
	TEGRA_PIN_PBB6,
};

static const unsigned pbb7_pins[] = {
	TEGRA_PIN_PBB7,
};

static const unsigned cam_mclk_pcc0_pins[] = {
	TEGRA_PIN_CAM_MCLK_PCC0,
};

static const unsigned pcc1_pins[] = {
	TEGRA_PIN_PCC1,
};

static const unsigned pcc2_pins[] = {
	TEGRA_PIN_PCC2,
};

static const unsigned sdmmc4_clk_pcc4_pins[] = {
	TEGRA_PIN_SDMMC4_CLK_PCC4,
};

static const unsigned clk2_req_pcc5_pins[] = {
	TEGRA_PIN_CLK2_REQ_PCC5,
};

static const unsigned pex_l0_rst_n_pdd1_pins[] = {
	TEGRA_PIN_PEX_L0_RST_N_PDD1,
};

static const unsigned pex_l0_clkreq_n_pdd2_pins[] = {
	TEGRA_PIN_PEX_L0_CLKREQ_N_PDD2,
};

static const unsigned pex_wake_n_pdd3_pins[] = {
	TEGRA_PIN_PEX_WAKE_N_PDD3,
};

static const unsigned pex_l1_rst_n_pdd5_pins[] = {
	TEGRA_PIN_PEX_L1_RST_N_PDD5,
};

static const unsigned pex_l1_clkreq_n_pdd6_pins[] = {
	TEGRA_PIN_PEX_L1_CLKREQ_N_PDD6,
};

static const unsigned clk3_out_pee0_pins[] = {
	TEGRA_PIN_CLK3_OUT_PEE0,
};

static const unsigned clk3_req_pee1_pins[] = {
	TEGRA_PIN_CLK3_REQ_PEE1,
};

static const unsigned dap_mclk1_req_pee2_pins[] = {
	TEGRA_PIN_DAP_MCLK1_REQ_PEE2,
};

static const unsigned hdmi_cec_pee3_pins[] = {
	TEGRA_PIN_HDMI_CEC_PEE3,
};

static const unsigned sdmmc3_clk_lb_out_pee4_pins[] = {
	TEGRA_PIN_SDMMC3_CLK_LB_OUT_PEE4,
};

static const unsigned sdmmc3_clk_lb_in_pee5_pins[] = {
	TEGRA_PIN_SDMMC3_CLK_LB_IN_PEE5,
};

static const unsigned dp_hpd_pff0_pins[] = {
	TEGRA_PIN_DP_HPD_PFF0,
};

static const unsigned usb_vbus_en2_pff1_pins[] = {
	TEGRA_PIN_USB_VBUS_EN2_PFF1,
};

static const unsigned pff2_pins[] = {
	TEGRA_PIN_PFF2,
};

static const unsigned core_pwr_req_pins[] = {
	TEGRA_PIN_CORE_PWR_REQ,
};

static const unsigned cpu_pwr_req_pins[] = {
	TEGRA_PIN_CPU_PWR_REQ,
};

static const unsigned pwr_int_n_pins[] = {
	TEGRA_PIN_PWR_INT_N,
};

static const unsigned gmi_clk_lb_pins[] = {
	TEGRA_PIN_GMI_CLK_LB,
};

static const unsigned reset_out_n_pins[] = {
	TEGRA_PIN_RESET_OUT_N,
};

static const unsigned owr_pins[] = {
	TEGRA_PIN_OWR,
};

static const unsigned clk_32k_in_pins[] = {
	TEGRA_PIN_CLK_32K_IN,
};

static const unsigned jtag_rtck_pins[] = {
	TEGRA_PIN_JTAG_RTCK,
};

static const unsigned drive_ao1_pins[] = {
	TEGRA_PIN_KB_ROW0_PR0,
	TEGRA_PIN_KB_ROW1_PR1,
	TEGRA_PIN_KB_ROW2_PR2,
	TEGRA_PIN_KB_ROW3_PR3,
	TEGRA_PIN_KB_ROW4_PR4,
	TEGRA_PIN_KB_ROW5_PR5,
	TEGRA_PIN_KB_ROW6_PR6,
	TEGRA_PIN_KB_ROW7_PR7,
	TEGRA_PIN_PWR_I2C_SCL_PZ6,
	TEGRA_PIN_PWR_I2C_SDA_PZ7,
};

static const unsigned drive_ao2_pins[] = {
	TEGRA_PIN_CLK_32K_OUT_PA0,
	TEGRA_PIN_CLK_32K_IN,
	TEGRA_PIN_KB_COL0_PQ0,
	TEGRA_PIN_KB_COL1_PQ1,
	TEGRA_PIN_KB_COL2_PQ2,
	TEGRA_PIN_KB_COL3_PQ3,
	TEGRA_PIN_KB_COL4_PQ4,
	TEGRA_PIN_KB_COL5_PQ5,
	TEGRA_PIN_KB_COL6_PQ6,
	TEGRA_PIN_KB_COL7_PQ7,
	TEGRA_PIN_KB_ROW8_PS0,
	TEGRA_PIN_KB_ROW9_PS1,
	TEGRA_PIN_KB_ROW10_PS2,
	TEGRA_PIN_KB_ROW11_PS3,
	TEGRA_PIN_KB_ROW12_PS4,
	TEGRA_PIN_KB_ROW13_PS5,
	TEGRA_PIN_KB_ROW14_PS6,
	TEGRA_PIN_KB_ROW15_PS7,
	TEGRA_PIN_KB_ROW16_PT0,
	TEGRA_PIN_KB_ROW17_PT1,
	TEGRA_PIN_SDMMC3_CD_N_PV2,
	TEGRA_PIN_CORE_PWR_REQ,
	TEGRA_PIN_CPU_PWR_REQ,
	TEGRA_PIN_PWR_INT_N,
};

static const unsigned drive_at1_pins[] = {
	TEGRA_PIN_PH0,
	TEGRA_PIN_PH1,
	TEGRA_PIN_PH2,
	TEGRA_PIN_PH3,
};

static const unsigned drive_at2_pins[] = {
	TEGRA_PIN_PG0,
	TEGRA_PIN_PG1,
	TEGRA_PIN_PG2,
	TEGRA_PIN_PG3,
	TEGRA_PIN_PG4,
	TEGRA_PIN_PG5,
	TEGRA_PIN_PG6,
	TEGRA_PIN_PG7,
	TEGRA_PIN_PI0,
	TEGRA_PIN_PI1,
	TEGRA_PIN_PI3,
	TEGRA_PIN_PI4,
	TEGRA_PIN_PI7,
	TEGRA_PIN_PK0,
	TEGRA_PIN_PK2,
};

static const unsigned drive_at3_pins[] = {
	TEGRA_PIN_PC7,
	TEGRA_PIN_PJ0,
};

static const unsigned drive_at4_pins[] = {
	TEGRA_PIN_PB0,
	TEGRA_PIN_PB1,
	TEGRA_PIN_PJ0,
	TEGRA_PIN_PJ7,
	TEGRA_PIN_PK7,
};

static const unsigned drive_at5_pins[] = {
	TEGRA_PIN_GEN2_I2C_SCL_PT5,
	TEGRA_PIN_GEN2_I2C_SDA_PT6,
};

static const unsigned drive_cdev1_pins[] = {
	TEGRA_PIN_DAP_MCLK1_PW4,
	TEGRA_PIN_DAP_MCLK1_REQ_PEE2,
};

static const unsigned drive_cdev2_pins[] = {
	TEGRA_PIN_CLK2_OUT_PW5,
	TEGRA_PIN_CLK2_REQ_PCC5,
};

static const unsigned drive_dap1_pins[] = {
	TEGRA_PIN_DAP1_FS_PN0,
	TEGRA_PIN_DAP1_DIN_PN1,
	TEGRA_PIN_DAP1_DOUT_PN2,
	TEGRA_PIN_DAP1_SCLK_PN3,
};

static const unsigned drive_dap2_pins[] = {
	TEGRA_PIN_DAP2_FS_PA2,
	TEGRA_PIN_DAP2_SCLK_PA3,
	TEGRA_PIN_DAP2_DIN_PA4,
	TEGRA_PIN_DAP2_DOUT_PA5,
};

static const unsigned drive_dap3_pins[] = {
	TEGRA_PIN_DAP3_FS_PP0,
	TEGRA_PIN_DAP3_DIN_PP1,
	TEGRA_PIN_DAP3_DOUT_PP2,
	TEGRA_PIN_DAP3_SCLK_PP3,
};

static const unsigned drive_dap4_pins[] = {
	TEGRA_PIN_DAP4_FS_PP4,
	TEGRA_PIN_DAP4_DIN_PP5,
	TEGRA_PIN_DAP4_DOUT_PP6,
	TEGRA_PIN_DAP4_SCLK_PP7,
};

static const unsigned drive_dbg_pins[] = {
	TEGRA_PIN_GEN1_I2C_SCL_PC4,
	TEGRA_PIN_GEN1_I2C_SDA_PC5,
	TEGRA_PIN_PU0,
	TEGRA_PIN_PU1,
	TEGRA_PIN_PU2,
	TEGRA_PIN_PU3,
	TEGRA_PIN_PU4,
	TEGRA_PIN_PU5,
	TEGRA_PIN_PU6,
};

static const unsigned drive_sdio3_pins[] = {
	TEGRA_PIN_SDMMC3_CLK_PA6,
	TEGRA_PIN_SDMMC3_CMD_PA7,
	TEGRA_PIN_SDMMC3_DAT3_PB4,
	TEGRA_PIN_SDMMC3_DAT2_PB5,
	TEGRA_PIN_SDMMC3_DAT1_PB6,
	TEGRA_PIN_SDMMC3_DAT0_PB7,
	TEGRA_PIN_SDMMC3_CLK_LB_OUT_PEE4,
	TEGRA_PIN_SDMMC3_CLK_LB_IN_PEE5,
};

static const unsigned drive_spi_pins[] = {
	TEGRA_PIN_DVFS_PWM_PX0,
	TEGRA_PIN_GPIO_X1_AUD_PX1,
	TEGRA_PIN_DVFS_CLK_PX2,
	TEGRA_PIN_GPIO_X3_AUD_PX3,
	TEGRA_PIN_GPIO_X4_AUD_PX4,
	TEGRA_PIN_GPIO_X5_AUD_PX5,
	TEGRA_PIN_GPIO_X6_AUD_PX6,
	TEGRA_PIN_GPIO_X7_AUD_PX7,
	TEGRA_PIN_GPIO_W2_AUD_PW2,
	TEGRA_PIN_GPIO_W3_AUD_PW3,
};

static const unsigned drive_uaa_pins[] = {
	TEGRA_PIN_ULPI_DATA0_PO1,
	TEGRA_PIN_ULPI_DATA1_PO2,
	TEGRA_PIN_ULPI_DATA2_PO3,
	TEGRA_PIN_ULPI_DATA3_PO4,
};

static const unsigned drive_uab_pins[] = {
	TEGRA_PIN_ULPI_DATA7_PO0,
	TEGRA_PIN_ULPI_DATA4_PO5,
	TEGRA_PIN_ULPI_DATA5_PO6,
	TEGRA_PIN_ULPI_DATA6_PO7,
	TEGRA_PIN_PV0,
	TEGRA_PIN_PV1,
};

static const unsigned drive_uart2_pins[] = {
	TEGRA_PIN_UART2_TXD_PC2,
	TEGRA_PIN_UART2_RXD_PC3,
	TEGRA_PIN_UART2_CTS_N_PJ5,
	TEGRA_PIN_UART2_RTS_N_PJ6,
};

static const unsigned drive_uart3_pins[] = {
	TEGRA_PIN_UART3_CTS_N_PA1,
	TEGRA_PIN_UART3_RTS_N_PC0,
	TEGRA_PIN_UART3_TXD_PW6,
	TEGRA_PIN_UART3_RXD_PW7,
};

static const unsigned drive_sdio1_pins[] = {
	TEGRA_PIN_SDMMC1_DAT3_PY4,
	TEGRA_PIN_SDMMC1_DAT2_PY5,
	TEGRA_PIN_SDMMC1_DAT1_PY6,
	TEGRA_PIN_SDMMC1_DAT0_PY7,
	TEGRA_PIN_SDMMC1_CLK_PZ0,
	TEGRA_PIN_SDMMC1_CMD_PZ1,
};

static const unsigned drive_ddc_pins[] = {
	TEGRA_PIN_DDC_SCL_PV4,
	TEGRA_PIN_DDC_SDA_PV5,
};

static const unsigned drive_gma_pins[] = {
	TEGRA_PIN_SDMMC4_CLK_PCC4,
	TEGRA_PIN_SDMMC4_CMD_PT7,
	TEGRA_PIN_SDMMC4_DAT0_PAA0,
	TEGRA_PIN_SDMMC4_DAT1_PAA1,
	TEGRA_PIN_SDMMC4_DAT2_PAA2,
	TEGRA_PIN_SDMMC4_DAT3_PAA3,
	TEGRA_PIN_SDMMC4_DAT4_PAA4,
	TEGRA_PIN_SDMMC4_DAT5_PAA5,
	TEGRA_PIN_SDMMC4_DAT6_PAA6,
	TEGRA_PIN_SDMMC4_DAT7_PAA7,
};

static const unsigned drive_gme_pins[] = {
	TEGRA_PIN_PBB0,
	TEGRA_PIN_CAM_I2C_SCL_PBB1,
	TEGRA_PIN_CAM_I2C_SDA_PBB2,
	TEGRA_PIN_PBB3,
	TEGRA_PIN_PCC2,
};

static const unsigned drive_gmf_pins[] = {
	TEGRA_PIN_PBB4,
	TEGRA_PIN_PBB5,
	TEGRA_PIN_PBB6,
	TEGRA_PIN_PBB7,
};

static const unsigned drive_gmg_pins[] = {
	TEGRA_PIN_CAM_MCLK_PCC0,
};

static const unsigned drive_gmh_pins[] = {
	TEGRA_PIN_PCC1,
};

static const unsigned drive_owr_pins[] = {
	TEGRA_PIN_SDMMC3_CD_N_PV2,
	TEGRA_PIN_OWR,
};

static const unsigned drive_uda_pins[] = {
	TEGRA_PIN_ULPI_CLK_PY0,
	TEGRA_PIN_ULPI_DIR_PY1,
	TEGRA_PIN_ULPI_NXT_PY2,
	TEGRA_PIN_ULPI_STP_PY3,
};

static const unsigned drive_gpv_pins[] = {
	TEGRA_PIN_PEX_L0_RST_N_PDD1,
	TEGRA_PIN_PEX_L0_CLKREQ_N_PDD2,
	TEGRA_PIN_PEX_WAKE_N_PDD3,
	TEGRA_PIN_PEX_L1_RST_N_PDD5,
	TEGRA_PIN_PEX_L1_CLKREQ_N_PDD6,
	TEGRA_PIN_USB_VBUS_EN2_PFF1,
	TEGRA_PIN_PFF2,
};

static const unsigned drive_dev3_pins[] = {
	TEGRA_PIN_CLK3_OUT_PEE0,
	TEGRA_PIN_CLK3_REQ_PEE1,
};

static const unsigned drive_cec_pins[] = {
	TEGRA_PIN_HDMI_CEC_PEE3,
};

static const unsigned drive_at6_pins[] = {
	TEGRA_PIN_PK1,
	TEGRA_PIN_PK3,
	TEGRA_PIN_PK4,
	TEGRA_PIN_PI2,
	TEGRA_PIN_PI5,
	TEGRA_PIN_PI6,
	TEGRA_PIN_PH4,
	TEGRA_PIN_PH5,
	TEGRA_PIN_PH6,
	TEGRA_PIN_PH7,
};

static const unsigned drive_dap5_pins[] = {
	TEGRA_PIN_SPDIF_IN_PK6,
	TEGRA_PIN_SPDIF_OUT_PK5,
	TEGRA_PIN_DP_HPD_PFF0,
};

static const unsigned drive_usb_vbus_en_pins[] = {
	TEGRA_PIN_USB_VBUS_EN0_PN4,
	TEGRA_PIN_USB_VBUS_EN1_PN5,
};

static const unsigned drive_ao3_pins[] = {
	TEGRA_PIN_RESET_OUT_N,
};

static const unsigned drive_ao0_pins[] = {
	TEGRA_PIN_JTAG_RTCK,
};

static const unsigned drive_hv0_pins[] = {
	TEGRA_PIN_HDMI_INT_PN7,
};

static const unsigned drive_sdio4_pins[] = {
	TEGRA_PIN_SDMMC1_WP_N_PV3,
};

static const unsigned drive_ao4_pins[] = {
	TEGRA_PIN_JTAG_RTCK,
};

static const unsigned mipi_pad_ctrl_dsi_b_pins[] = {
	TEGRA_PIN_DSI_B_CLK_P,
	TEGRA_PIN_DSI_B_CLK_N,
	TEGRA_PIN_DSI_B_D0_P,
	TEGRA_PIN_DSI_B_D0_N,
	TEGRA_PIN_DSI_B_D1_P,
	TEGRA_PIN_DSI_B_D1_N,
	TEGRA_PIN_DSI_B_D2_P,
	TEGRA_PIN_DSI_B_D2_N,
	TEGRA_PIN_DSI_B_D3_P,
	TEGRA_PIN_DSI_B_D3_N,
};

enum tegra_mux {
	TEGRA_MUX_BLINK,
	TEGRA_MUX_CCLA,
	TEGRA_MUX_CEC,
	TEGRA_MUX_CLDVFS,
	TEGRA_MUX_CLK,
	TEGRA_MUX_CLK12,
	TEGRA_MUX_CPU,
	TEGRA_MUX_CSI,
	TEGRA_MUX_DAP,
	TEGRA_MUX_DAP1,
	TEGRA_MUX_DAP2,
	TEGRA_MUX_DEV3,
	TEGRA_MUX_DISPLAYA,
	TEGRA_MUX_DISPLAYA_ALT,
	TEGRA_MUX_DISPLAYB,
	TEGRA_MUX_DP,
	TEGRA_MUX_DSI_B,
	TEGRA_MUX_DTV,
	TEGRA_MUX_EXTPERIPH1,
	TEGRA_MUX_EXTPERIPH2,
	TEGRA_MUX_EXTPERIPH3,
	TEGRA_MUX_GMI,
	TEGRA_MUX_GMI_ALT,
	TEGRA_MUX_HDA,
	TEGRA_MUX_HSI,
	TEGRA_MUX_I2C1,
	TEGRA_MUX_I2C2,
	TEGRA_MUX_I2C3,
	TEGRA_MUX_I2C4,
	TEGRA_MUX_I2CPWR,
	TEGRA_MUX_I2S0,
	TEGRA_MUX_I2S1,
	TEGRA_MUX_I2S2,
	TEGRA_MUX_I2S3,
	TEGRA_MUX_I2S4,
	TEGRA_MUX_IRDA,
	TEGRA_MUX_KBC,
	TEGRA_MUX_OWR,
	TEGRA_MUX_PE,
	TEGRA_MUX_PE0,
	TEGRA_MUX_PE1,
	TEGRA_MUX_PMI,
	TEGRA_MUX_PWM0,
	TEGRA_MUX_PWM1,
	TEGRA_MUX_PWM2,
	TEGRA_MUX_PWM3,
	TEGRA_MUX_PWRON,
	TEGRA_MUX_RESET_OUT_N,
	TEGRA_MUX_RSVD1,
	TEGRA_MUX_RSVD2,
	TEGRA_MUX_RSVD3,
	TEGRA_MUX_RSVD4,
	TEGRA_MUX_RTCK,
	TEGRA_MUX_SATA,
	TEGRA_MUX_SDMMC1,
	TEGRA_MUX_SDMMC2,
	TEGRA_MUX_SDMMC3,
	TEGRA_MUX_SDMMC4,
	TEGRA_MUX_SOC,
	TEGRA_MUX_SPDIF,
	TEGRA_MUX_SPI1,
	TEGRA_MUX_SPI2,
	TEGRA_MUX_SPI3,
	TEGRA_MUX_SPI4,
	TEGRA_MUX_SPI5,
	TEGRA_MUX_SPI6,
	TEGRA_MUX_SYS,
	TEGRA_MUX_TMDS,
	TEGRA_MUX_TRACE,
	TEGRA_MUX_UARTA,
	TEGRA_MUX_UARTB,
	TEGRA_MUX_UARTC,
	TEGRA_MUX_UARTD,
	TEGRA_MUX_ULPI,
	TEGRA_MUX_USB,
	TEGRA_MUX_VGP1,
	TEGRA_MUX_VGP2,
	TEGRA_MUX_VGP3,
	TEGRA_MUX_VGP4,
	TEGRA_MUX_VGP5,
	TEGRA_MUX_VGP6,
	TEGRA_MUX_VI,
	TEGRA_MUX_VI_ALT1,
	TEGRA_MUX_VI_ALT3,
	TEGRA_MUX_VIMCLK2,
	TEGRA_MUX_VIMCLK2_ALT,
};

#define FUNCTION(fname)					\
	{						\
		.name = #fname,				\
	}

static struct tegra_function tegra124_functions[] = {
	FUNCTION(blink),
	FUNCTION(ccla),
	FUNCTION(cec),
	FUNCTION(cldvfs),
	FUNCTION(clk),
	FUNCTION(clk12),
	FUNCTION(cpu),
	FUNCTION(csi),
	FUNCTION(dap),
	FUNCTION(dap1),
	FUNCTION(dap2),
	FUNCTION(dev3),
	FUNCTION(displaya),
	FUNCTION(displaya_alt),
	FUNCTION(displayb),
	FUNCTION(dp),
	FUNCTION(dsi_b),
	FUNCTION(dtv),
	FUNCTION(extperiph1),
	FUNCTION(extperiph2),
	FUNCTION(extperiph3),
	FUNCTION(gmi),
	FUNCTION(gmi_alt),
	FUNCTION(hda),
	FUNCTION(hsi),
	FUNCTION(i2c1),
	FUNCTION(i2c2),
	FUNCTION(i2c3),
	FUNCTION(i2c4),
	FUNCTION(i2cpwr),
	FUNCTION(i2s0),
	FUNCTION(i2s1),
	FUNCTION(i2s2),
	FUNCTION(i2s3),
	FUNCTION(i2s4),
	FUNCTION(irda),
	FUNCTION(kbc),
	FUNCTION(owr),
	FUNCTION(pe),
	FUNCTION(pe0),
	FUNCTION(pe1),
	FUNCTION(pmi),
	FUNCTION(pwm0),
	FUNCTION(pwm1),
	FUNCTION(pwm2),
	FUNCTION(pwm3),
	FUNCTION(pwron),
	FUNCTION(reset_out_n),
	FUNCTION(rsvd1),
	FUNCTION(rsvd2),
	FUNCTION(rsvd3),
	FUNCTION(rsvd4),
	FUNCTION(rtck),
	FUNCTION(sata),
	FUNCTION(sdmmc1),
	FUNCTION(sdmmc2),
	FUNCTION(sdmmc3),
	FUNCTION(sdmmc4),
	FUNCTION(soc),
	FUNCTION(spdif),
	FUNCTION(spi1),
	FUNCTION(spi2),
	FUNCTION(spi3),
	FUNCTION(spi4),
	FUNCTION(spi5),
	FUNCTION(spi6),
	FUNCTION(sys),
	FUNCTION(tmds),
	FUNCTION(trace),
	FUNCTION(uarta),
	FUNCTION(uartb),
	FUNCTION(uartc),
	FUNCTION(uartd),
	FUNCTION(ulpi),
	FUNCTION(usb),
	FUNCTION(vgp1),
	FUNCTION(vgp2),
	FUNCTION(vgp3),
	FUNCTION(vgp4),
	FUNCTION(vgp5),
	FUNCTION(vgp6),
	FUNCTION(vi),
	FUNCTION(vi_alt1),
	FUNCTION(vi_alt3),
	FUNCTION(vimclk2),
	FUNCTION(vimclk2_alt),
};

#define DRV_PINGROUP_REG_A		0x868	/* bank 0 */
#define PINGROUP_REG_A			0x3000	/* bank 1 */
#define MIPI_PAD_CTRL_PINGROUP_REG_A	0x820	/* bank 2 */

#define DRV_PINGROUP_REG(r)		((r) - DRV_PINGROUP_REG_A)
#define PINGROUP_REG(r)			((r) - PINGROUP_REG_A)
#define MIPI_PAD_CTRL_PINGROUP_REG_Y(r)	((r) - MIPI_PAD_CTRL_PINGROUP_REG_A)

#define PINGROUP_BIT_Y(b)		(b)
#define PINGROUP_BIT_N(b)		(-1)

#define PINGROUP(pg_name, f0, f1, f2, f3, r, od, ior, rcv_sel)		\
	{								\
		.name = #pg_name,					\
		.pins = pg_name##_pins,					\
		.npins = ARRAY_SIZE(pg_name##_pins),			\
		.funcs = {						\
			TEGRA_MUX_##f0,					\
			TEGRA_MUX_##f1,					\
			TEGRA_MUX_##f2,					\
			TEGRA_MUX_##f3,					\
		},							\
		.mux_reg = PINGROUP_REG(r),				\
		.mux_bank = 1,						\
		.mux_bit = 0,						\
		.pupd_reg = PINGROUP_REG(r),				\
		.pupd_bank = 1,						\
		.pupd_bit = 2,						\
		.tri_reg = PINGROUP_REG(r),				\
		.tri_bank = 1,						\
		.tri_bit = 4,						\
		.einput_bit = 5,					\
		.odrain_bit = PINGROUP_BIT_##od(6),			\
		.lock_bit = 7,						\
		.ioreset_bit = PINGROUP_BIT_##ior(8),			\
		.rcv_sel_bit = PINGROUP_BIT_##rcv_sel(9),		\
		.parked_bit = -1,					\
		.drv_reg = -1,						\
	}

#define DRV_PINGROUP(pg_name, r, hsm_b, schmitt_b, lpmd_b, drvdn_b,	\
		     drvdn_w, drvup_b, drvup_w, slwr_b, slwr_w,		\
		     slwf_b, slwf_w, drvtype)				\
	{								\
		.name = "drive_" #pg_name,				\
		.pins = drive_##pg_name##_pins,				\
		.npins = ARRAY_SIZE(drive_##pg_name##_pins),		\
		.mux_reg = -1,						\
		.pupd_reg = -1,						\
		.tri_reg = -1,						\
		.einput_bit = -1,					\
		.odrain_bit = -1,					\
		.lock_bit = -1,						\
		.ioreset_bit = -1,					\
		.rcv_sel_bit = -1,					\
		.drv_reg = DRV_PINGROUP_REG(r),				\
		.drv_bank = 0,						\
		.parked_bit = -1,					\
		.hsm_bit = hsm_b,					\
		.schmitt_bit = schmitt_b,				\
		.lpmd_bit = lpmd_b,					\
		.drvdn_bit = drvdn_b,					\
		.drvdn_width = drvdn_w,					\
		.drvup_bit = drvup_b,					\
		.drvup_width = drvup_w,					\
		.slwr_bit = slwr_b,					\
		.slwr_width = slwr_w,					\
		.slwf_bit = slwf_b,					\
		.slwf_width = slwf_w,					\
		.drvtype_bit = PINGROUP_BIT_##drvtype(6),		\
	}

#define MIPI_PAD_CTRL_PINGROUP(pg_name, r, b, f0, f1)			\
	{								\
		.name = "mipi_pad_ctrl_" #pg_name,			\
		.pins = mipi_pad_ctrl_##pg_name##_pins,			\
		.npins = ARRAY_SIZE(mipi_pad_ctrl_##pg_name##_pins),	\
		.funcs = {						\
			TEGRA_MUX_ ## f0,				\
			TEGRA_MUX_ ## f1,				\
			TEGRA_MUX_RSVD3,				\
			TEGRA_MUX_RSVD4,				\
		},							\
		.mux_reg = MIPI_PAD_CTRL_PINGROUP_REG_Y(r),		\
		.mux_bank = 2,						\
		.mux_bit = b,						\
		.pupd_reg = -1,						\
		.tri_reg = -1,						\
		.einput_bit = -1,					\
		.odrain_bit = -1,					\
		.lock_bit = -1,						\
		.ioreset_bit = -1,					\
		.rcv_sel_bit = -1,					\
		.drv_reg = -1,						\
	}

static const struct tegra_pingroup tegra124_groups[] = {
	/*       pg_name,                f0,         f1,         f2,           f3,          r,      od, ior, rcv_sel */
	PINGROUP(ulpi_data0_po1,         SPI3,       HSI,        UARTA,        ULPI,        0x3000, N,   N,  N),
	PINGROUP(ulpi_data1_po2,         SPI3,       HSI,        UARTA,        ULPI,        0x3004, N,   N,  N),
	PINGROUP(ulpi_data2_po3,         SPI3,       HSI,        UARTA,        ULPI,        0x3008, N,   N,  N),
	PINGROUP(ulpi_data3_po4,         SPI3,       HSI,        UARTA,        ULPI,        0x300c, N,   N,  N),
	PINGROUP(ulpi_data4_po5,         SPI2,       HSI,        UARTA,        ULPI,        0x3010, N,   N,  N),
	PINGROUP(ulpi_data5_po6,         SPI2,       HSI,        UARTA,        ULPI,        0x3014, N,   N,  N),
	PINGROUP(ulpi_data6_po7,         SPI2,       HSI,        UARTA,        ULPI,        0x3018, N,   N,  N),
	PINGROUP(ulpi_data7_po0,         SPI2,       HSI,        UARTA,        ULPI,        0x301c, N,   N,  N),
	PINGROUP(ulpi_clk_py0,           SPI1,       SPI5,       UARTD,        ULPI,        0x3020, N,   N,  N),
	PINGROUP(ulpi_dir_py1,           SPI1,       SPI5,       UARTD,        ULPI,        0x3024, N,   N,  N),
	PINGROUP(ulpi_nxt_py2,           SPI1,       SPI5,       UARTD,        ULPI,        0x3028, N,   N,  N),
	PINGROUP(ulpi_stp_py3,           SPI1,       SPI5,       UARTD,        ULPI,        0x302c, N,   N,  N),
	PINGROUP(dap3_fs_pp0,            I2S2,       SPI5,       DISPLAYA,     DISPLAYB,    0x3030, N,   N,  N),
	PINGROUP(dap3_din_pp1,           I2S2,       SPI5,       DISPLAYA,     DISPLAYB,    0x3034, N,   N,  N),
	PINGROUP(dap3_dout_pp2,          I2S2,       SPI5,       DISPLAYA,     RSVD4,       0x3038, N,   N,  N),
	PINGROUP(dap3_sclk_pp3,          I2S2,       SPI5,       RSVD3,        DISPLAYB,    0x303c, N,   N,  N),
	PINGROUP(pv0,                    RSVD1,      RSVD2,      RSVD3,        RSVD4,       0x3040, N,   N,  N),
	PINGROUP(pv1,                    RSVD1,      RSVD2,      RSVD3,        RSVD4,       0x3044, N,   N,  N),
	PINGROUP(sdmmc1_clk_pz0,         SDMMC1,     CLK12,      RSVD3,        RSVD4,       0x3048, N,   N,  N),
	PINGROUP(sdmmc1_cmd_pz1,         SDMMC1,     SPDIF,      SPI4,         UARTA,       0x304c, N,   N,  N),
	PINGROUP(sdmmc1_dat3_py4,        SDMMC1,     SPDIF,      SPI4,         UARTA,       0x3050, N,   N,  N),
	PINGROUP(sdmmc1_dat2_py5,        SDMMC1,     PWM0,       SPI4,         UARTA,       0x3054, N,   N,  N),
	PINGROUP(sdmmc1_dat1_py6,        SDMMC1,     PWM1,       SPI4,         UARTA,       0x3058, N,   N,  N),
	PINGROUP(sdmmc1_dat0_py7,        SDMMC1,     RSVD2,      SPI4,         UARTA,       0x305c, N,   N,  N),
	PINGROUP(clk2_out_pw5,           EXTPERIPH2, RSVD2,      RSVD3,        RSVD4,       0x3068, N,   N,  N),
	PINGROUP(clk2_req_pcc5,          DAP,        RSVD2,      RSVD3,        RSVD4,       0x306c, N,   N,  N),
	PINGROUP(hdmi_int_pn7,           RSVD1,      RSVD2,      RSVD3,        RSVD4,       0x3110, N,   N,  Y),
	PINGROUP(ddc_scl_pv4,            I2C4,       RSVD2,      RSVD3,        RSVD4,       0x3114, N,   N,  Y),
	PINGROUP(ddc_sda_pv5,            I2C4,       RSVD2,      RSVD3,        RSVD4,       0x3118, N,   N,  Y),
	PINGROUP(uart2_rxd_pc3,          IRDA,       SPDIF,      UARTA,        SPI4,        0x3164, N,   N,  N),
	PINGROUP(uart2_txd_pc2,          IRDA,       SPDIF,      UARTA,        SPI4,        0x3168, N,   N,  N),
	PINGROUP(uart2_rts_n_pj6,        UARTA,      UARTB,      GMI,          SPI4,        0x316c, N,   N,  N),
	PINGROUP(uart2_cts_n_pj5,        UARTA,      UARTB,      GMI,          SPI4,        0x3170, N,   N,  N),
	PINGROUP(uart3_txd_pw6,          UARTC,      RSVD2,      GMI,          SPI4,        0x3174, N,   N,  N),
	PINGROUP(uart3_rxd_pw7,          UARTC,      RSVD2,      GMI,          SPI4,        0x3178, N,   N,  N),
	PINGROUP(uart3_cts_n_pa1,        UARTC,      SDMMC1,     DTV,          GMI,         0x317c, N,   N,  N),
	PINGROUP(uart3_rts_n_pc0,        UARTC,      PWM0,       DTV,          GMI,         0x3180, N,   N,  N),
	PINGROUP(pu0,                    OWR,        UARTA,      GMI,          RSVD4,       0x3184, N,   N,  N),
	PINGROUP(pu1,                    RSVD1,      UARTA,      GMI,          RSVD4,       0x3188, N,   N,  N),
	PINGROUP(pu2,                    RSVD1,      UARTA,      GMI,          RSVD4,       0x318c, N,   N,  N),
	PINGROUP(pu3,                    PWM0,       UARTA,      GMI,          DISPLAYB,    0x3190, N,   N,  N),
	PINGROUP(pu4,                    PWM1,       UARTA,      GMI,          DISPLAYB,    0x3194, N,   N,  N),
	PINGROUP(pu5,                    PWM2,       UARTA,      GMI,          DISPLAYB,    0x3198, N,   N,  N),
	PINGROUP(pu6,                    PWM3,       UARTA,      RSVD3,        GMI,         0x319c, N,   N,  N),
	PINGROUP(gen1_i2c_sda_pc5,       I2C1,       RSVD2,      RSVD3,        RSVD4,       0x31a0, Y,   N,  N),
	PINGROUP(gen1_i2c_scl_pc4,       I2C1,       RSVD2,      RSVD3,        RSVD4,       0x31a4, Y,   N,  N),
	PINGROUP(dap4_fs_pp4,            I2S3,       GMI,        DTV,          RSVD4,       0x31a8, N,   N,  N),
	PINGROUP(dap4_din_pp5,           I2S3,       GMI,        RSVD3,        RSVD4,       0x31ac, N,   N,  N),
	PINGROUP(dap4_dout_pp6,          I2S3,       GMI,        DTV,          RSVD4,       0x31b0, N,   N,  N),
	PINGROUP(dap4_sclk_pp7,          I2S3,       GMI,        RSVD3,        RSVD4,       0x31b4, N,   N,  N),
	PINGROUP(clk3_out_pee0,          EXTPERIPH3, RSVD2,      RSVD3,        RSVD4,       0x31b8, N,   N,  N),
	PINGROUP(clk3_req_pee1,          DEV3,       RSVD2,      RSVD3,        RSVD4,       0x31bc, N,   N,  N),
	PINGROUP(pc7,                    RSVD1,      RSVD2,      GMI,          GMI_ALT,     0x31c0, N,   N,  N),
	PINGROUP(pi5,                    SDMMC2,     RSVD2,      GMI,          RSVD4,       0x31c4, N,   N,  N),
	PINGROUP(pi7,                    RSVD1,      TRACE,      GMI,          DTV,         0x31c8, N,   N,  N),
	PINGROUP(pk0,                    RSVD1,      SDMMC3,     GMI,          SOC,         0x31cc, N,   N,  N),
	PINGROUP(pk1,                    SDMMC2,     TRACE,      GMI,          RSVD4,       0x31d0, N,   N,  N),
	PINGROUP(pj0,                    RSVD1,      RSVD2,      GMI,          USB,         0x31d4, N,   N,  N),
	PINGROUP(pj2,                    RSVD1,      RSVD2,      GMI,          SOC,         0x31d8, N,   N,  N),
	PINGROUP(pk3,                    SDMMC2,     TRACE,      GMI,          CCLA,        0x31dc, N,   N,  N),
	PINGROUP(pk4,                    SDMMC2,     RSVD2,      GMI,          GMI_ALT,     0x31e0, N,   N,  N),
	PINGROUP(pk2,                    RSVD1,      RSVD2,      GMI,          RSVD4,       0x31e4, N,   N,  N),
	PINGROUP(pi3,                    RSVD1,      RSVD2,      GMI,          SPI4,        0x31e8, N,   N,  N),
	PINGROUP(pi6,                    RSVD1,      RSVD2,      GMI,          SDMMC2,      0x31ec, N,   N,  N),
	PINGROUP(pg0,                    RSVD1,      RSVD2,      GMI,          RSVD4,       0x31f0, N,   N,  N),
	PINGROUP(pg1,                    RSVD1,      RSVD2,      GMI,          RSVD4,       0x31f4, N,   N,  N),
	PINGROUP(pg2,                    RSVD1,      TRACE,      GMI,          RSVD4,       0x31f8, N,   N,  N),
	PINGROUP(pg3,                    RSVD1,      TRACE,      GMI,          RSVD4,       0x31fc, N,   N,  N),
	PINGROUP(pg4,                    RSVD1,      TMDS,       GMI,          SPI4,        0x3200, N,   N,  N),
	PINGROUP(pg5,                    RSVD1,      RSVD2,      GMI,          SPI4,        0x3204, N,   N,  N),
	PINGROUP(pg6,                    RSVD1,      RSVD2,      GMI,          SPI4,        0x3208, N,   N,  N),
	PINGROUP(pg7,                    RSVD1,      RSVD2,      GMI,          SPI4,        0x320c, N,   N,  N),
	PINGROUP(ph0,                    PWM0,       TRACE,      GMI,          DTV,         0x3210, N,   N,  N),
	PINGROUP(ph1,                    PWM1,       TMDS,       GMI,          DISPLAYA,    0x3214, N,   N,  N),
	PINGROUP(ph2,                    PWM2,       TMDS,       GMI,          CLDVFS,      0x3218, N,   N,  N),
	PINGROUP(ph3,                    PWM3,       SPI4,       GMI,          CLDVFS,      0x321c, N,   N,  N),
	PINGROUP(ph4,                    SDMMC2,     RSVD2,      GMI,          RSVD4,       0x3220, N,   N,  N),
	PINGROUP(ph5,                    SDMMC2,     RSVD2,      GMI,          RSVD4,       0x3224, N,   N,  N),
	PINGROUP(ph6,                    SDMMC2,     TRACE,      GMI,          DTV,         0x3228, N,   N,  N),
	PINGROUP(ph7,                    SDMMC2,     TRACE,      GMI,          DTV,         0x322c, N,   N,  N),
	PINGROUP(pj7,                    UARTD,      RSVD2,      GMI,          GMI_ALT,     0x3230, N,   N,  N),
	PINGROUP(pb0,                    UARTD,      RSVD2,      GMI,          RSVD4,       0x3234, N,   N,  N),
	PINGROUP(pb1,                    UARTD,      RSVD2,      GMI,          RSVD4,       0x3238, N,   N,  N),
	PINGROUP(pk7,                    UARTD,      RSVD2,      GMI,          RSVD4,       0x323c, N,   N,  N),
	PINGROUP(pi0,                    RSVD1,      RSVD2,      GMI,          RSVD4,       0x3240, N,   N,  N),
	PINGROUP(pi1,                    RSVD1,      RSVD2,      GMI,          RSVD4,       0x3244, N,   N,  N),
	PINGROUP(pi2,                    SDMMC2,     TRACE,      GMI,          RSVD4,       0x3248, N,   N,  N),
	PINGROUP(pi4,                    SPI4,       TRACE,      GMI,          DISPLAYA,    0x324c, N,   N,  N),
	PINGROUP(gen2_i2c_scl_pt5,       I2C2,       RSVD2,      GMI,          RSVD4,       0x3250, Y,   N,  N),
	PINGROUP(gen2_i2c_sda_pt6,       I2C2,       RSVD2,      GMI,          RSVD4,       0x3254, Y,   N,  N),
	PINGROUP(sdmmc4_clk_pcc4,        SDMMC4,     RSVD2,      GMI,          RSVD4,       0x3258, N,   Y,  N),
	PINGROUP(sdmmc4_cmd_pt7,         SDMMC4,     RSVD2,      GMI,          RSVD4,       0x325c, N,   Y,  N),
	PINGROUP(sdmmc4_dat0_paa0,       SDMMC4,     SPI3,       GMI,          RSVD4,       0x3260, N,   Y,  N),
	PINGROUP(sdmmc4_dat1_paa1,       SDMMC4,     SPI3,       GMI,          RSVD4,       0x3264, N,   Y,  N),
	PINGROUP(sdmmc4_dat2_paa2,       SDMMC4,     SPI3,       GMI,          RSVD4,       0x3268, N,   Y,  N),
	PINGROUP(sdmmc4_dat3_paa3,       SDMMC4,     SPI3,       GMI,          RSVD4,       0x326c, N,   Y,  N),
	PINGROUP(sdmmc4_dat4_paa4,       SDMMC4,     SPI3,       GMI,          RSVD4,       0x3270, N,   Y,  N),
	PINGROUP(sdmmc4_dat5_paa5,       SDMMC4,     SPI3,       RSVD3,        RSVD4,       0x3274, N,   Y,  N),
	PINGROUP(sdmmc4_dat6_paa6,       SDMMC4,     SPI3,       GMI,          RSVD4,       0x3278, N,   Y,  N),
	PINGROUP(sdmmc4_dat7_paa7,       SDMMC4,     RSVD2,      GMI,          RSVD4,       0x327c, N,   Y,  N),
	PINGROUP(cam_mclk_pcc0,          VI,         VI_ALT1,    VI_ALT3,      SDMMC2,      0x3284, N,   N,  N),
	PINGROUP(pcc1,                   I2S4,       RSVD2,      RSVD3,        SDMMC2,      0x3288, N,   N,  N),
	PINGROUP(pbb0,                   VGP6,       VIMCLK2,    SDMMC2,       VIMCLK2_ALT, 0x328c, N,   N,  N),
	PINGROUP(cam_i2c_scl_pbb1,       VGP1,       I2C3,       RSVD3,        SDMMC2,      0x3290, Y,   N,  N),
	PINGROUP(cam_i2c_sda_pbb2,       VGP2,       I2C3,       RSVD3,        SDMMC2,      0x3294, Y,   N,  N),
	PINGROUP(pbb3,                   VGP3,       DISPLAYA,   DISPLAYB,     SDMMC2,      0x3298, N,   N,  N),
	PINGROUP(pbb4,                   VGP4,       DISPLAYA,   DISPLAYB,     SDMMC2,      0x329c, N,   N,  N),
	PINGROUP(pbb5,                   VGP5,       DISPLAYA,   RSVD3,        SDMMC2,      0x32a0, N,   N,  N),
	PINGROUP(pbb6,                   I2S4,       RSVD2,      DISPLAYB,     SDMMC2,      0x32a4, N,   N,  N),
	PINGROUP(pbb7,                   I2S4,       RSVD2,      RSVD3,        SDMMC2,      0x32a8, N,   N,  N),
	PINGROUP(pcc2,                   I2S4,       RSVD2,      SDMMC3,       SDMMC2,      0x32ac, N,   N,  N),
	PINGROUP(jtag_rtck,              RTCK,       RSVD2,      RSVD3,        RSVD4,       0x32b0, N,   N,  N),
	PINGROUP(pwr_i2c_scl_pz6,        I2CPWR,     RSVD2,      RSVD3,        RSVD4,       0x32b4, Y,   N,  N),
	PINGROUP(pwr_i2c_sda_pz7,        I2CPWR,     RSVD2,      RSVD3,        RSVD4,       0x32b8, Y,   N,  N),
	PINGROUP(kb_row0_pr0,            KBC,        RSVD2,      RSVD3,        RSVD4,       0x32bc, N,   N,  N),
	PINGROUP(kb_row1_pr1,            KBC,        RSVD2,      RSVD3,        RSVD4,       0x32c0, N,   N,  N),
	PINGROUP(kb_row2_pr2,            KBC,        RSVD2,      RSVD3,        RSVD4,       0x32c4, N,   N,  N),
	PINGROUP(kb_row3_pr3,            KBC,        DISPLAYA,   SYS,          DISPLAYB,    0x32c8, N,   N,  N),
	PINGROUP(kb_row4_pr4,            KBC,        DISPLAYA,   RSVD3,        DISPLAYB,    0x32cc, N,   N,  N),
	PINGROUP(kb_row5_pr5,            KBC,        DISPLAYA,   RSVD3,        DISPLAYB,    0x32d0, N,   N,  N),
	PINGROUP(kb_row6_pr6,            KBC,        DISPLAYA,   DISPLAYA_ALT, DISPLAYB,    0x32d4, N,   N,  N),
	PINGROUP(kb_row7_pr7,            KBC,        RSVD2,      CLDVFS,       UARTA,       0x32d8, N,   N,  N),
	PINGROUP(kb_row8_ps0,            KBC,        RSVD2,      CLDVFS,       UARTA,       0x32dc, N,   N,  N),
	PINGROUP(kb_row9_ps1,            KBC,        RSVD2,      RSVD3,        UARTA,       0x32e0, N,   N,  N),
	PINGROUP(kb_row10_ps2,           KBC,        RSVD2,      RSVD3,        UARTA,       0x32e4, N,   N,  N),
	PINGROUP(kb_row11_ps3,           KBC,        RSVD2,      RSVD3,        IRDA,        0x32e8, N,   N,  N),
	PINGROUP(kb_row12_ps4,           KBC,        RSVD2,      RSVD3,        IRDA,        0x32ec, N,   N,  N),
	PINGROUP(kb_row13_ps5,           KBC,        RSVD2,      SPI2,         RSVD4,       0x32f0, N,   N,  N),
	PINGROUP(kb_row14_ps6,           KBC,        RSVD2,      SPI2,         RSVD4,       0x32f4, N,   N,  N),
	PINGROUP(kb_row15_ps7,           KBC,        SOC,        RSVD3,        RSVD4,       0x32f8, N,   N,  N),
	PINGROUP(kb_col0_pq0,            KBC,        RSVD2,      SPI2,         RSVD4,       0x32fc, N,   N,  N),
	PINGROUP(kb_col1_pq1,            KBC,        RSVD2,      SPI2,         RSVD4,       0x3300, N,   N,  N),
	PINGROUP(kb_col2_pq2,            KBC,        RSVD2,      SPI2,         RSVD4,       0x3304, N,   N,  N),
	PINGROUP(kb_col3_pq3,            KBC,        DISPLAYA,   PWM2,         UARTA,       0x3308, N,   N,  N),
	PINGROUP(kb_col4_pq4,            KBC,        OWR,        SDMMC3,       UARTA,       0x330c, N,   N,  N),
	PINGROUP(kb_col5_pq5,            KBC,        RSVD2,      SDMMC3,       RSVD4,       0x3310, N,   N,  N),
	PINGROUP(kb_col6_pq6,            KBC,        RSVD2,      SPI2,         UARTD,       0x3314, N,   N,  N),
	PINGROUP(kb_col7_pq7,            KBC,        RSVD2,      SPI2,         UARTD,       0x3318, N,   N,  N),
	PINGROUP(clk_32k_out_pa0,        BLINK,      SOC,        RSVD3,        RSVD4,       0x331c, N,   N,  N),
	PINGROUP(core_pwr_req,           PWRON,      RSVD2,      RSVD3,        RSVD4,       0x3324, N,   N,  N),
	PINGROUP(cpu_pwr_req,            CPU,        RSVD2,      RSVD3,        RSVD4,       0x3328, N,   N,  N),
	PINGROUP(pwr_int_n,              PMI,        RSVD2,      RSVD3,        RSVD4,       0x332c, N,   N,  N),
	PINGROUP(clk_32k_in,             CLK,        RSVD2,      RSVD3,        RSVD4,       0x3330, N,   N,  N),
	PINGROUP(owr,                    OWR,        RSVD2,      RSVD3,        RSVD4,       0x3334, N,   N,  Y),
	PINGROUP(dap1_fs_pn0,            I2S0,       HDA,        GMI,          RSVD4,       0x3338, N,   N,  N),
	PINGROUP(dap1_din_pn1,           I2S0,       HDA,        GMI,          RSVD4,       0x333c, N,   N,  N),
	PINGROUP(dap1_dout_pn2,          I2S0,       HDA,        GMI,          SATA,        0x3340, N,   N,  N),
	PINGROUP(dap1_sclk_pn3,          I2S0,       HDA,        GMI,          RSVD4,       0x3344, N,   N,  N),
	PINGROUP(dap_mclk1_req_pee2,     DAP,        DAP1,       SATA,         RSVD4,       0x3348, N,   N,  N),
	PINGROUP(dap_mclk1_pw4,          EXTPERIPH1, DAP2,       RSVD3,        RSVD4,       0x334c, N,   N,  N),
	PINGROUP(spdif_in_pk6,           SPDIF,      RSVD2,      RSVD3,        I2C3,        0x3350, N,   N,  N),
	PINGROUP(spdif_out_pk5,          SPDIF,      RSVD2,      RSVD3,        I2C3,        0x3354, N,   N,  N),
	PINGROUP(dap2_fs_pa2,            I2S1,       HDA,        GMI,          RSVD4,       0x3358, N,   N,  N),
	PINGROUP(dap2_din_pa4,           I2S1,       HDA,        GMI,          RSVD4,       0x335c, N,   N,  N),
	PINGROUP(dap2_dout_pa5,          I2S1,       HDA,        GMI,          RSVD4,       0x3360, N,   N,  N),
	PINGROUP(dap2_sclk_pa3,          I2S1,       HDA,        GMI,          RSVD4,       0x3364, N,   N,  N),
	PINGROUP(dvfs_pwm_px0,           SPI6,       CLDVFS,     GMI,          RSVD4,       0x3368, N,   N,  N),
	PINGROUP(gpio_x1_aud_px1,        SPI6,       RSVD2,      GMI,          RSVD4,       0x336c, N,   N,  N),
	PINGROUP(gpio_x3_aud_px3,        SPI6,       SPI1,       GMI,          RSVD4,       0x3370, N,   N,  N),
	PINGROUP(dvfs_clk_px2,           SPI6,       CLDVFS,     GMI,          RSVD4,       0x3374, N,   N,  N),
	PINGROUP(gpio_x4_aud_px4,        GMI,        SPI1,       SPI2,         DAP2,        0x3378, N,   N,  N),
	PINGROUP(gpio_x5_aud_px5,        GMI,        SPI1,       SPI2,         RSVD4,       0x337c, N,   N,  N),
	PINGROUP(gpio_x6_aud_px6,        SPI6,       SPI1,       SPI2,         GMI,         0x3380, N,   N,  N),
	PINGROUP(gpio_x7_aud_px7,        RSVD1,      SPI1,       SPI2,         RSVD4,       0x3384, N,   N,  N),
	PINGROUP(sdmmc3_clk_pa6,         SDMMC3,     RSVD2,      RSVD3,        SPI3,        0x3390, N,   N,  N),
	PINGROUP(sdmmc3_cmd_pa7,         SDMMC3,     PWM3,       UARTA,        SPI3,        0x3394, N,   N,  N),
	PINGROUP(sdmmc3_dat0_pb7,        SDMMC3,     RSVD2,      RSVD3,        SPI3,        0x3398, N,   N,  N),
	PINGROUP(sdmmc3_dat1_pb6,        SDMMC3,     PWM2,       UARTA,        SPI3,        0x339c, N,   N,  N),
	PINGROUP(sdmmc3_dat2_pb5,        SDMMC3,     PWM1,       DISPLAYA,     SPI3,        0x33a0, N,   N,  N),
	PINGROUP(sdmmc3_dat3_pb4,        SDMMC3,     PWM0,       DISPLAYB,     SPI3,        0x33a4, N,   N,  N),
	PINGROUP(pex_l0_rst_n_pdd1,      PE0,        RSVD2,      RSVD3,        RSVD4,       0x33bc, N,   N,  N),
	PINGROUP(pex_l0_clkreq_n_pdd2,   PE0,        RSVD2,      RSVD3,        RSVD4,       0x33c0, N,   N,  N),
	PINGROUP(pex_wake_n_pdd3,        PE,         RSVD2,      RSVD3,        RSVD4,       0x33c4, N,   N,  N),
	PINGROUP(pex_l1_rst_n_pdd5,      PE1,        RSVD2,      RSVD3,        RSVD4,       0x33cc, N,   N,  N),
	PINGROUP(pex_l1_clkreq_n_pdd6,   PE1,        RSVD2,      RSVD3,        RSVD4,       0x33d0, N,   N,  N),
	PINGROUP(hdmi_cec_pee3,          CEC,        RSVD2,      RSVD3,        RSVD4,       0x33e0, Y,   N,  N),
	PINGROUP(sdmmc1_wp_n_pv3,        SDMMC1,     CLK12,      SPI4,         UARTA,       0x33e4, N,   N,  N),
	PINGROUP(sdmmc3_cd_n_pv2,        SDMMC3,     OWR,        RSVD3,        RSVD4,       0x33e8, N,   N,  N),
	PINGROUP(gpio_w2_aud_pw2,        SPI6,       RSVD2,      SPI2,         I2C1,        0x33ec, N,   N,  N),
	PINGROUP(gpio_w3_aud_pw3,        SPI6,       SPI1,       SPI2,         I2C1,        0x33f0, N,   N,  N),
	PINGROUP(usb_vbus_en0_pn4,       USB,        RSVD2,      RSVD3,        RSVD4,       0x33f4, Y,   N,  N),
	PINGROUP(usb_vbus_en1_pn5,       USB,        RSVD2,      RSVD3,        RSVD4,       0x33f8, Y,   N,  N),
	PINGROUP(sdmmc3_clk_lb_in_pee5,  SDMMC3,     RSVD2,      RSVD3,        RSVD4,       0x33fc, N,   N,  N),
	PINGROUP(sdmmc3_clk_lb_out_pee4, SDMMC3,     RSVD2,      RSVD3,        RSVD4,       0x3400, N,   N,  N),
	PINGROUP(gmi_clk_lb,             SDMMC2,     RSVD2,      GMI,          RSVD4,       0x3404, N,   N,  N),
	PINGROUP(reset_out_n,            RSVD1,      RSVD2,      RSVD3,        RESET_OUT_N, 0x3408, N,   N,  N),
	PINGROUP(kb_row16_pt0,           KBC,        RSVD2,      RSVD3,        UARTC,       0x340c, N,   N,  N),
	PINGROUP(kb_row17_pt1,           KBC,        RSVD2,      RSVD3,        UARTC,       0x3410, N,   N,  N),
	PINGROUP(usb_vbus_en2_pff1,      USB,        RSVD2,      RSVD3,        RSVD4,       0x3414, Y,   N,  N),
	PINGROUP(pff2,                   SATA,       RSVD2,      RSVD3,        RSVD4,       0x3418, Y,   N,  N),
	PINGROUP(dp_hpd_pff0,            DP,         RSVD2,      RSVD3,        RSVD4,       0x3430, N,   N,  N),

	/* pg_name, r, hsm_b, schmitt_b, lpmd_b, drvdn_b, drvdn_w, drvup_b, drvup_w, slwr_b, slwr_w, slwf_b, slwf_w, drvtype */
	DRV_PINGROUP(ao1,         0x868,  2,  3,  4,  12,  5,  20,  5,  28,  2,  30,  2,  N),
	DRV_PINGROUP(ao2,         0x86c,  2,  3,  4,  12,  5,  20,  5,  28,  2,  30,  2,  N),
	DRV_PINGROUP(at1,         0x870,  2,  3,  4,  12,  7,  20,  7,  28,  2,  30,  2,  Y),
	DRV_PINGROUP(at2,         0x874,  2,  3,  4,  12,  7,  20,  7,  28,  2,  30,  2,  Y),
	DRV_PINGROUP(at3,         0x878,  2,  3,  4,  12,  7,  20,  7,  28,  2,  30,  2,  Y),
	DRV_PINGROUP(at4,         0x87c,  2,  3,  4,  12,  7,  20,  7,  28,  2,  30,  2,  Y),
	DRV_PINGROUP(at5,         0x880,  2,  3,  4,  14,  5,  19,  5,  28,  2,  30,  2,  N),
	DRV_PINGROUP(cdev1,       0x884,  2,  3,  4,  12,  5,  20,  5,  28,  2,  30,  2,  N),
	DRV_PINGROUP(cdev2,       0x888,  2,  3,  4,  12,  5,  20,  5,  28,  2,  30,  2,  N),
	DRV_PINGROUP(dap1,        0x890,  2,  3,  4,  12,  5,  20,  5,  28,  2,  30,  2,  N),
	DRV_PINGROUP(dap2,        0x894,  2,  3,  4,  12,  5,  20,  5,  28,  2,  30,  2,  N),
	DRV_PINGROUP(dap3,        0x898,  2,  3,  4,  12,  5,  20,  5,  28,  2,  30,  2,  N),
	DRV_PINGROUP(dap4,        0x89c,  2,  3,  4,  12,  5,  20,  5,  28,  2,  30,  2,  N),
	DRV_PINGROUP(dbg,         0x8a0,  2,  3,  4,  12,  5,  20,  5,  28,  2,  30,  2,  N),
	DRV_PINGROUP(sdio3,       0x8b0,  2,  3, -1,  12,  7,  20,  7,  28,  2,  30,  2,  N),
	DRV_PINGROUP(spi,         0x8b4,  2,  3,  4,  12,  5,  20,  5,  28,  2,  30,  2,  N),
	DRV_PINGROUP(uaa,         0x8b8,  2,  3,  4,  12,  5,  20,  5,  28,  2,  30,  2,  N),
	DRV_PINGROUP(uab,         0x8bc,  2,  3,  4,  12,  5,  20,  5,  28,  2,  30,  2,  N),
	DRV_PINGROUP(uart2,       0x8c0,  2,  3,  4,  12,  5,  20,  5,  28,  2,  30,  2,  N),
	DRV_PINGROUP(uart3,       0x8c4,  2,  3,  4,  12,  5,  20,  5,  28,  2,  30,  2,  N),
	DRV_PINGROUP(sdio1,       0x8ec,  2,  3, -1,  12,  7,  20,  7,  28,  2,  30,  2,  N),
	DRV_PINGROUP(ddc,         0x8fc,  2,  3,  4,  12,  5,  20,  5,  28,  2,  30,  2,  N),
	DRV_PINGROUP(gma,         0x900,  2,  3,  4,  14,  5,  20,  5,  28,  2,  30,  2,  Y),
	DRV_PINGROUP(gme,         0x910,  2,  3,  4,  14,  5,  19,  5,  28,  2,  30,  2,  N),
	DRV_PINGROUP(gmf,         0x914,  2,  3,  4,  14,  5,  19,  5,  28,  2,  30,  2,  N),
	DRV_PINGROUP(gmg,         0x918,  2,  3,  4,  14,  5,  19,  5,  28,  2,  30,  2,  N),
	DRV_PINGROUP(gmh,         0x91c,  2,  3,  4,  14,  5,  19,  5,  28,  2,  30,  2,  N),
	DRV_PINGROUP(owr,         0x920,  2,  3,  4,  12,  5,  20,  5,  28,  2,  30,  2,  N),
	DRV_PINGROUP(uda,         0x924,  2,  3,  4,  12,  5,  20,  5,  28,  2,  30,  2,  N),
	DRV_PINGROUP(gpv,         0x928,  2,  3,  4,  12,  5,  20,  5,  28,  2,  30,  2,  N),
	DRV_PINGROUP(dev3,        0x92c,  2,  3,  4,  12,  5,  20,  5,  28,  2,  30,  2,  N),
	DRV_PINGROUP(cec,         0x938,  2,  3,  4,  12,  5,  20,  5,  28,  2,  30,  2,  N),
	DRV_PINGROUP(at6,         0x994,  2,  3,  4,  12,  7,  20,  7,  28,  2,  30,  2,  Y),
	DRV_PINGROUP(dap5,        0x998,  2,  3,  4,  12,  5,  20,  5,  28,  2,  30,  2,  N),
	DRV_PINGROUP(usb_vbus_en, 0x99c,  2,  3,  4,  12,  5,  20,  5,  28,  2,  30,  2,  N),
	DRV_PINGROUP(ao3,         0x9a8,  2,  3,  4,  12,  5,  -1, -1,  28,  2,  -1, -1,  N),
	DRV_PINGROUP(ao0,         0x9b0,  2,  3,  4,  12,  5,  20,  5,  28,  2,  30,  2,  N),
	DRV_PINGROUP(hv0,         0x9b4,  2,  3,  4,  12,  5,  -1, -1,  28,  2,  -1, -1,  N),
	DRV_PINGROUP(sdio4,       0x9c4,  2,  3,  4,  12,  5,  20,  5,  28,  2,  30,  2,  N),
	DRV_PINGROUP(ao4,         0x9c8,  2,  3,  4,  12,  7,  20,  7,  28,  2,  30,  2,  Y),

	/*                     pg_name, r,     b, f0,  f1 */
	MIPI_PAD_CTRL_PINGROUP(dsi_b,   0x820, 1, CSI, DSI_B),
};

static const struct tegra_pinctrl_soc_data tegra124_pinctrl = {
	.ngpios = NUM_GPIOS,
	.gpio_compatible = "nvidia,tegra30-gpio",
	.pins = tegra124_pins,
	.npins = ARRAY_SIZE(tegra124_pins),
	.functions = tegra124_functions,
	.nfunctions = ARRAY_SIZE(tegra124_functions),
	.groups = tegra124_groups,
	.ngroups = ARRAY_SIZE(tegra124_groups),
	.hsm_in_mux = false,
	.schmitt_in_mux = false,
	.drvtype_in_mux = false,
};

static int tegra124_pinctrl_probe(struct platform_device *pdev)
{
	return tegra_pinctrl_probe(pdev, &tegra124_pinctrl);
}

static const struct of_device_id tegra124_pinctrl_of_match[] = {
	{ .compatible = "nvidia,tegra124-pinmux", },
	{ },
};

static struct platform_driver tegra124_pinctrl_driver = {
	.driver = {
		.name = "tegra124-pinctrl",
		.of_match_table = tegra124_pinctrl_of_match,
	},
	.probe = tegra124_pinctrl_probe,
};

static int __init tegra124_pinctrl_init(void)
{
	return platform_driver_register(&tegra124_pinctrl_driver);
}
arch_initcall(tegra124_pinctrl_init);
