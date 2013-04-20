/*
 * Pinctrl data and driver for the NVIDIA Tegra114 pinmux
 *
 * Copyright (c) 2012-2013, NVIDIA CORPORATION.  All rights reserved.
 *
 * Arthur:  Pritesh Raithatha <praithatha@nvidia.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/module.h>
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
#define TEGRA_PIN_GMI_A17_PB0			_GPIO(8)
#define TEGRA_PIN_GMI_A18_PB1			_GPIO(9)
#define TEGRA_PIN_SDMMC3_DAT3_PB4		_GPIO(12)
#define TEGRA_PIN_SDMMC3_DAT2_PB5		_GPIO(13)
#define TEGRA_PIN_SDMMC3_DAT1_PB6		_GPIO(14)
#define TEGRA_PIN_SDMMC3_DAT0_PB7		_GPIO(15)
#define TEGRA_PIN_UART3_RTS_N_PC0		_GPIO(16)
#define TEGRA_PIN_UART2_TXD_PC2			_GPIO(18)
#define TEGRA_PIN_UART2_RXD_PC3			_GPIO(19)
#define TEGRA_PIN_GEN1_I2C_SCL_PC4		_GPIO(20)
#define TEGRA_PIN_GEN1_I2C_SDA_PC5		_GPIO(21)
#define TEGRA_PIN_GMI_WP_N_PC7			_GPIO(23)
#define TEGRA_PIN_GMI_AD0_PG0			_GPIO(48)
#define TEGRA_PIN_GMI_AD1_PG1			_GPIO(49)
#define TEGRA_PIN_GMI_AD2_PG2			_GPIO(50)
#define TEGRA_PIN_GMI_AD3_PG3			_GPIO(51)
#define TEGRA_PIN_GMI_AD4_PG4			_GPIO(52)
#define TEGRA_PIN_GMI_AD5_PG5			_GPIO(53)
#define TEGRA_PIN_GMI_AD6_PG6			_GPIO(54)
#define TEGRA_PIN_GMI_AD7_PG7			_GPIO(55)
#define TEGRA_PIN_GMI_AD8_PH0			_GPIO(56)
#define TEGRA_PIN_GMI_AD9_PH1			_GPIO(57)
#define TEGRA_PIN_GMI_AD10_PH2			_GPIO(58)
#define TEGRA_PIN_GMI_AD11_PH3			_GPIO(59)
#define TEGRA_PIN_GMI_AD12_PH4			_GPIO(60)
#define TEGRA_PIN_GMI_AD13_PH5			_GPIO(61)
#define TEGRA_PIN_GMI_AD14_PH6			_GPIO(62)
#define TEGRA_PIN_GMI_AD15_PH7			_GPIO(63)
#define TEGRA_PIN_GMI_WR_N_PI0			_GPIO(64)
#define TEGRA_PIN_GMI_OE_N_PI1			_GPIO(65)
#define TEGRA_PIN_GMI_CS6_N_PI3			_GPIO(67)
#define TEGRA_PIN_GMI_RST_N_PI4			_GPIO(68)
#define TEGRA_PIN_GMI_IORDY_PI5			_GPIO(69)
#define TEGRA_PIN_GMI_CS7_N_PI6			_GPIO(70)
#define TEGRA_PIN_GMI_WAIT_PI7			_GPIO(71)
#define TEGRA_PIN_GMI_CS0_N_PJ0			_GPIO(72)
#define TEGRA_PIN_GMI_CS1_N_PJ2			_GPIO(74)
#define TEGRA_PIN_GMI_DQS_P_PJ3			_GPIO(75)
#define TEGRA_PIN_UART2_CTS_N_PJ5		_GPIO(77)
#define TEGRA_PIN_UART2_RTS_N_PJ6		_GPIO(78)
#define TEGRA_PIN_GMI_A16_PJ7			_GPIO(79)
#define TEGRA_PIN_GMI_ADV_N_PK0			_GPIO(80)
#define TEGRA_PIN_GMI_CLK_PK1			_GPIO(81)
#define TEGRA_PIN_GMI_CS4_N_PK2			_GPIO(82)
#define TEGRA_PIN_GMI_CS2_N_PK3			_GPIO(83)
#define TEGRA_PIN_GMI_CS3_N_PK4			_GPIO(84)
#define TEGRA_PIN_SPDIF_OUT_PK5			_GPIO(85)
#define TEGRA_PIN_SPDIF_IN_PK6			_GPIO(86)
#define TEGRA_PIN_GMI_A19_PK7			_GPIO(87)
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
#define TEGRA_PIN_CLK1_OUT_PW4			_GPIO(180)
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
#define TEGRA_PIN_SYS_CLK_REQ_PZ5		_GPIO(205)
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
#define TEGRA_PIN_CLK3_OUT_PEE0			_GPIO(240)
#define TEGRA_PIN_CLK3_REQ_PEE1			_GPIO(241)
#define TEGRA_PIN_CLK1_REQ_PEE2			_GPIO(242)
#define TEGRA_PIN_HDMI_CEC_PEE3			_GPIO(243)
#define TEGRA_PIN_SDMMC3_CLK_LB_OUT_PEE4	_GPIO(244)
#define TEGRA_PIN_SDMMC3_CLK_LB_IN_PEE5		_GPIO(245)

/* All non-GPIO pins follow */
#define NUM_GPIOS	(TEGRA_PIN_SDMMC3_CLK_LB_IN_PEE5 + 1)
#define _PIN(offset)	(NUM_GPIOS + (offset))

/* Non-GPIO pins */
#define TEGRA_PIN_CORE_PWR_REQ			_PIN(0)
#define TEGRA_PIN_CPU_PWR_REQ			_PIN(1)
#define TEGRA_PIN_PWR_INT_N			_PIN(2)
#define TEGRA_PIN_RESET_OUT_N			_PIN(3)
#define TEGRA_PIN_OWR				_PIN(4)

static const struct pinctrl_pin_desc  tegra114_pins[] = {
	PINCTRL_PIN(TEGRA_PIN_CLK_32K_OUT_PA0, "CLK_32K_OUT PA0"),
	PINCTRL_PIN(TEGRA_PIN_UART3_CTS_N_PA1, "UART3_CTS_N PA1"),
	PINCTRL_PIN(TEGRA_PIN_DAP2_FS_PA2, "DAP2_FS PA2"),
	PINCTRL_PIN(TEGRA_PIN_DAP2_SCLK_PA3, "DAP2_SCLK PA3"),
	PINCTRL_PIN(TEGRA_PIN_DAP2_DIN_PA4, "DAP2_DIN PA4"),
	PINCTRL_PIN(TEGRA_PIN_DAP2_DOUT_PA5, "DAP2_DOUT PA5"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC3_CLK_PA6, "SDMMC3_CLK PA6"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC3_CMD_PA7, "SDMMC3_CMD PA7"),
	PINCTRL_PIN(TEGRA_PIN_GMI_A17_PB0, "GMI_A17 PB0"),
	PINCTRL_PIN(TEGRA_PIN_GMI_A18_PB1, "GMI_A18 PB1"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC3_DAT3_PB4, "SDMMC3_DAT3 PB4"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC3_DAT2_PB5, "SDMMC3_DAT2 PB5"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC3_DAT1_PB6, "SDMMC3_DAT1 PB6"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC3_DAT0_PB7, "SDMMC3_DAT0 PB7"),
	PINCTRL_PIN(TEGRA_PIN_UART3_RTS_N_PC0, "UART3_RTS_N PC0"),
	PINCTRL_PIN(TEGRA_PIN_UART2_TXD_PC2, "UART2_TXD PC2"),
	PINCTRL_PIN(TEGRA_PIN_UART2_RXD_PC3, "UART2_RXD PC3"),
	PINCTRL_PIN(TEGRA_PIN_GEN1_I2C_SCL_PC4, "GEN1_I2C_SCL PC4"),
	PINCTRL_PIN(TEGRA_PIN_GEN1_I2C_SDA_PC5, "GEN1_I2C_SDA PC5"),
	PINCTRL_PIN(TEGRA_PIN_GMI_WP_N_PC7, "GMI_WP_N PC7"),
	PINCTRL_PIN(TEGRA_PIN_GMI_AD0_PG0, "GMI_AD0 PG0"),
	PINCTRL_PIN(TEGRA_PIN_GMI_AD1_PG1, "GMI_AD1 PG1"),
	PINCTRL_PIN(TEGRA_PIN_GMI_AD2_PG2, "GMI_AD2 PG2"),
	PINCTRL_PIN(TEGRA_PIN_GMI_AD3_PG3, "GMI_AD3 PG3"),
	PINCTRL_PIN(TEGRA_PIN_GMI_AD4_PG4, "GMI_AD4 PG4"),
	PINCTRL_PIN(TEGRA_PIN_GMI_AD5_PG5, "GMI_AD5 PG5"),
	PINCTRL_PIN(TEGRA_PIN_GMI_AD6_PG6, "GMI_AD6 PG6"),
	PINCTRL_PIN(TEGRA_PIN_GMI_AD7_PG7, "GMI_AD7 PG7"),
	PINCTRL_PIN(TEGRA_PIN_GMI_AD8_PH0, "GMI_AD8 PH0"),
	PINCTRL_PIN(TEGRA_PIN_GMI_AD9_PH1, "GMI_AD9 PH1"),
	PINCTRL_PIN(TEGRA_PIN_GMI_AD10_PH2, "GMI_AD10 PH2"),
	PINCTRL_PIN(TEGRA_PIN_GMI_AD11_PH3, "GMI_AD11 PH3"),
	PINCTRL_PIN(TEGRA_PIN_GMI_AD12_PH4, "GMI_AD12 PH4"),
	PINCTRL_PIN(TEGRA_PIN_GMI_AD13_PH5, "GMI_AD13 PH5"),
	PINCTRL_PIN(TEGRA_PIN_GMI_AD14_PH6, "GMI_AD14 PH6"),
	PINCTRL_PIN(TEGRA_PIN_GMI_AD15_PH7, "GMI_AD15 PH7"),
	PINCTRL_PIN(TEGRA_PIN_GMI_WR_N_PI0, "GMI_WR_N PI0"),
	PINCTRL_PIN(TEGRA_PIN_GMI_OE_N_PI1, "GMI_OE_N PI1"),
	PINCTRL_PIN(TEGRA_PIN_GMI_CS6_N_PI3, "GMI_CS6_N PI3"),
	PINCTRL_PIN(TEGRA_PIN_GMI_RST_N_PI4, "GMI_RST_N PI4"),
	PINCTRL_PIN(TEGRA_PIN_GMI_IORDY_PI5, "GMI_IORDY PI5"),
	PINCTRL_PIN(TEGRA_PIN_GMI_CS7_N_PI6, "GMI_CS7_N PI6"),
	PINCTRL_PIN(TEGRA_PIN_GMI_WAIT_PI7, "GMI_WAIT PI7"),
	PINCTRL_PIN(TEGRA_PIN_GMI_CS0_N_PJ0, "GMI_CS0_N PJ0"),
	PINCTRL_PIN(TEGRA_PIN_GMI_CS1_N_PJ2, "GMI_CS1_N PJ2"),
	PINCTRL_PIN(TEGRA_PIN_GMI_DQS_P_PJ3, "GMI_DQS_P PJ3"),
	PINCTRL_PIN(TEGRA_PIN_UART2_CTS_N_PJ5, "UART2_CTS_N PJ5"),
	PINCTRL_PIN(TEGRA_PIN_UART2_RTS_N_PJ6, "UART2_RTS_N PJ6"),
	PINCTRL_PIN(TEGRA_PIN_GMI_A16_PJ7, "GMI_A16 PJ7"),
	PINCTRL_PIN(TEGRA_PIN_GMI_ADV_N_PK0, "GMI_ADV_N PK0"),
	PINCTRL_PIN(TEGRA_PIN_GMI_CLK_PK1, "GMI_CLK PK1"),
	PINCTRL_PIN(TEGRA_PIN_GMI_CS4_N_PK2, "GMI_CS4_N PK2"),
	PINCTRL_PIN(TEGRA_PIN_GMI_CS2_N_PK3, "GMI_CS2_N PK3"),
	PINCTRL_PIN(TEGRA_PIN_GMI_CS3_N_PK4, "GMI_CS3_N PK4"),
	PINCTRL_PIN(TEGRA_PIN_SPDIF_OUT_PK5, "SPDIF_OUT PK5"),
	PINCTRL_PIN(TEGRA_PIN_SPDIF_IN_PK6, "SPDIF_IN PK6"),
	PINCTRL_PIN(TEGRA_PIN_GMI_A19_PK7, "GMI_A19 PK7"),
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
	PINCTRL_PIN(TEGRA_PIN_CLK1_OUT_PW4, "CLK1_OUT PW4"),
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
	PINCTRL_PIN(TEGRA_PIN_SYS_CLK_REQ_PZ5, "SYS_CLK_REQ PZ5"),
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
	PINCTRL_PIN(TEGRA_PIN_CLK3_OUT_PEE0, "CLK3_OUT PEE0"),
	PINCTRL_PIN(TEGRA_PIN_CLK3_REQ_PEE1, "CLK3_REQ PEE1"),
	PINCTRL_PIN(TEGRA_PIN_CLK1_REQ_PEE2, "CLK1_REQ PEE2"),
	PINCTRL_PIN(TEGRA_PIN_HDMI_CEC_PEE3, "HDMI_CEC PEE3"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC3_CLK_LB_OUT_PEE4, "SDMMC3_CLK_LB_OUT PEE4"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC3_CLK_LB_IN_PEE5, "SDMMC3_CLK_LB_IN PEE5"),
	PINCTRL_PIN(TEGRA_PIN_CORE_PWR_REQ, "CORE_PWR_REQ"),
	PINCTRL_PIN(TEGRA_PIN_CPU_PWR_REQ, "CPU_PWR_REQ"),
	PINCTRL_PIN(TEGRA_PIN_OWR, "OWR"),
	PINCTRL_PIN(TEGRA_PIN_PWR_INT_N, "PWR_INT_N"),
	PINCTRL_PIN(TEGRA_PIN_RESET_OUT_N, "RESET_OUT_N"),
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

static const unsigned gmi_a17_pb0_pins[] = {
	TEGRA_PIN_GMI_A17_PB0,
};

static const unsigned gmi_a18_pb1_pins[] = {
	TEGRA_PIN_GMI_A18_PB1,
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

static const unsigned gmi_wp_n_pc7_pins[] = {
	TEGRA_PIN_GMI_WP_N_PC7,
};

static const unsigned gmi_ad0_pg0_pins[] = {
	TEGRA_PIN_GMI_AD0_PG0,
};

static const unsigned gmi_ad1_pg1_pins[] = {
	TEGRA_PIN_GMI_AD1_PG1,
};

static const unsigned gmi_ad2_pg2_pins[] = {
	TEGRA_PIN_GMI_AD2_PG2,
};

static const unsigned gmi_ad3_pg3_pins[] = {
	TEGRA_PIN_GMI_AD3_PG3,
};

static const unsigned gmi_ad4_pg4_pins[] = {
	TEGRA_PIN_GMI_AD4_PG4,
};

static const unsigned gmi_ad5_pg5_pins[] = {
	TEGRA_PIN_GMI_AD5_PG5,
};

static const unsigned gmi_ad6_pg6_pins[] = {
	TEGRA_PIN_GMI_AD6_PG6,
};

static const unsigned gmi_ad7_pg7_pins[] = {
	TEGRA_PIN_GMI_AD7_PG7,
};

static const unsigned gmi_ad8_ph0_pins[] = {
	TEGRA_PIN_GMI_AD8_PH0,
};

static const unsigned gmi_ad9_ph1_pins[] = {
	TEGRA_PIN_GMI_AD9_PH1,
};

static const unsigned gmi_ad10_ph2_pins[] = {
	TEGRA_PIN_GMI_AD10_PH2,
};

static const unsigned gmi_ad11_ph3_pins[] = {
	TEGRA_PIN_GMI_AD11_PH3,
};

static const unsigned gmi_ad12_ph4_pins[] = {
	TEGRA_PIN_GMI_AD12_PH4,
};

static const unsigned gmi_ad13_ph5_pins[] = {
	TEGRA_PIN_GMI_AD13_PH5,
};

static const unsigned gmi_ad14_ph6_pins[] = {
	TEGRA_PIN_GMI_AD14_PH6,
};

static const unsigned gmi_ad15_ph7_pins[] = {
	TEGRA_PIN_GMI_AD15_PH7,
};

static const unsigned gmi_wr_n_pi0_pins[] = {
	TEGRA_PIN_GMI_WR_N_PI0,
};

static const unsigned gmi_oe_n_pi1_pins[] = {
	TEGRA_PIN_GMI_OE_N_PI1,
};

static const unsigned gmi_cs6_n_pi3_pins[] = {
	TEGRA_PIN_GMI_CS6_N_PI3,
};

static const unsigned gmi_rst_n_pi4_pins[] = {
	TEGRA_PIN_GMI_RST_N_PI4,
};

static const unsigned gmi_iordy_pi5_pins[] = {
	TEGRA_PIN_GMI_IORDY_PI5,
};

static const unsigned gmi_cs7_n_pi6_pins[] = {
	TEGRA_PIN_GMI_CS7_N_PI6,
};

static const unsigned gmi_wait_pi7_pins[] = {
	TEGRA_PIN_GMI_WAIT_PI7,
};

static const unsigned gmi_cs0_n_pj0_pins[] = {
	TEGRA_PIN_GMI_CS0_N_PJ0,
};

static const unsigned gmi_cs1_n_pj2_pins[] = {
	TEGRA_PIN_GMI_CS1_N_PJ2,
};

static const unsigned gmi_dqs_p_pj3_pins[] = {
	TEGRA_PIN_GMI_DQS_P_PJ3,
};

static const unsigned uart2_cts_n_pj5_pins[] = {
	TEGRA_PIN_UART2_CTS_N_PJ5,
};

static const unsigned uart2_rts_n_pj6_pins[] = {
	TEGRA_PIN_UART2_RTS_N_PJ6,
};

static const unsigned gmi_a16_pj7_pins[] = {
	TEGRA_PIN_GMI_A16_PJ7,
};

static const unsigned gmi_adv_n_pk0_pins[] = {
	TEGRA_PIN_GMI_ADV_N_PK0,
};

static const unsigned gmi_clk_pk1_pins[] = {
	TEGRA_PIN_GMI_CLK_PK1,
};

static const unsigned gmi_cs4_n_pk2_pins[] = {
	TEGRA_PIN_GMI_CS4_N_PK2,
};

static const unsigned gmi_cs2_n_pk3_pins[] = {
	TEGRA_PIN_GMI_CS2_N_PK3,
};

static const unsigned gmi_cs3_n_pk4_pins[] = {
	TEGRA_PIN_GMI_CS3_N_PK4,
};

static const unsigned spdif_out_pk5_pins[] = {
	TEGRA_PIN_SPDIF_OUT_PK5,
};

static const unsigned spdif_in_pk6_pins[] = {
	TEGRA_PIN_SPDIF_IN_PK6,
};

static const unsigned gmi_a19_pk7_pins[] = {
	TEGRA_PIN_GMI_A19_PK7,
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

static const unsigned clk1_out_pw4_pins[] = {
	TEGRA_PIN_CLK1_OUT_PW4,
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

static const unsigned sys_clk_req_pz5_pins[] = {
	TEGRA_PIN_SYS_CLK_REQ_PZ5,
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

static const unsigned clk3_out_pee0_pins[] = {
	TEGRA_PIN_CLK3_OUT_PEE0,
};

static const unsigned clk3_req_pee1_pins[] = {
	TEGRA_PIN_CLK3_REQ_PEE1,
};

static const unsigned clk1_req_pee2_pins[] = {
	TEGRA_PIN_CLK1_REQ_PEE2,
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

static const unsigned core_pwr_req_pins[] = {
	TEGRA_PIN_CORE_PWR_REQ,
};

static const unsigned cpu_pwr_req_pins[] = {
	TEGRA_PIN_CPU_PWR_REQ,
};

static const unsigned owr_pins[] = {
	TEGRA_PIN_OWR,
};

static const unsigned pwr_int_n_pins[] = {
	TEGRA_PIN_PWR_INT_N,
};

static const unsigned reset_out_n_pins[] = {
	TEGRA_PIN_RESET_OUT_N,
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
	TEGRA_PIN_SYS_CLK_REQ_PZ5,
	TEGRA_PIN_CORE_PWR_REQ,
	TEGRA_PIN_CPU_PWR_REQ,
	TEGRA_PIN_RESET_OUT_N,
};

static const unsigned drive_at1_pins[] = {
	TEGRA_PIN_GMI_AD8_PH0,
	TEGRA_PIN_GMI_AD9_PH1,
	TEGRA_PIN_GMI_AD10_PH2,
	TEGRA_PIN_GMI_AD11_PH3,
	TEGRA_PIN_GMI_AD12_PH4,
	TEGRA_PIN_GMI_AD13_PH5,
	TEGRA_PIN_GMI_AD14_PH6,
	TEGRA_PIN_GMI_AD15_PH7,

	TEGRA_PIN_GMI_IORDY_PI5,
	TEGRA_PIN_GMI_CS7_N_PI6,
};

static const unsigned drive_at2_pins[] = {
	TEGRA_PIN_GMI_AD0_PG0,
	TEGRA_PIN_GMI_AD1_PG1,
	TEGRA_PIN_GMI_AD2_PG2,
	TEGRA_PIN_GMI_AD3_PG3,
	TEGRA_PIN_GMI_AD4_PG4,
	TEGRA_PIN_GMI_AD5_PG5,
	TEGRA_PIN_GMI_AD6_PG6,
	TEGRA_PIN_GMI_AD7_PG7,

	TEGRA_PIN_GMI_WR_N_PI0,
	TEGRA_PIN_GMI_OE_N_PI1,
	TEGRA_PIN_GMI_CS6_N_PI3,
	TEGRA_PIN_GMI_RST_N_PI4,
	TEGRA_PIN_GMI_WAIT_PI7,

	TEGRA_PIN_GMI_DQS_P_PJ3,

	TEGRA_PIN_GMI_ADV_N_PK0,
	TEGRA_PIN_GMI_CLK_PK1,
	TEGRA_PIN_GMI_CS4_N_PK2,
	TEGRA_PIN_GMI_CS2_N_PK3,
	TEGRA_PIN_GMI_CS3_N_PK4,
};

static const unsigned drive_at3_pins[] = {
	TEGRA_PIN_GMI_WP_N_PC7,
	TEGRA_PIN_GMI_CS0_N_PJ0,
};

static const unsigned drive_at4_pins[] = {
	TEGRA_PIN_GMI_A17_PB0,
	TEGRA_PIN_GMI_A18_PB1,
	TEGRA_PIN_GMI_CS1_N_PJ2,
	TEGRA_PIN_GMI_A16_PJ7,
	TEGRA_PIN_GMI_A19_PK7,
};

static const unsigned drive_at5_pins[] = {
	TEGRA_PIN_GEN2_I2C_SCL_PT5,
	TEGRA_PIN_GEN2_I2C_SDA_PT6,
};

static const unsigned drive_cdev1_pins[] = {
	TEGRA_PIN_CLK1_OUT_PW4,
	TEGRA_PIN_CLK1_REQ_PEE2,
};

static const unsigned drive_cdev2_pins[] = {
	TEGRA_PIN_CLK2_OUT_PW5,
	TEGRA_PIN_CLK2_REQ_PCC5,
	TEGRA_PIN_SDMMC1_WP_N_PV3,
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
};

static const unsigned drive_uda_pins[] = {
	TEGRA_PIN_ULPI_CLK_PY0,
	TEGRA_PIN_ULPI_DIR_PY1,
	TEGRA_PIN_ULPI_NXT_PY2,
	TEGRA_PIN_ULPI_STP_PY3,
};

static const unsigned drive_dev3_pins[] = {
	TEGRA_PIN_CLK3_OUT_PEE0,
	TEGRA_PIN_CLK3_REQ_PEE1,
};

enum tegra_mux {
	TEGRA_MUX_BLINK,
	TEGRA_MUX_CEC,
	TEGRA_MUX_CLDVFS,
	TEGRA_MUX_CLK12,
	TEGRA_MUX_CPU,
	TEGRA_MUX_DAP,
	TEGRA_MUX_DAP1,
	TEGRA_MUX_DAP2,
	TEGRA_MUX_DEV3,
	TEGRA_MUX_DISPLAYA,
	TEGRA_MUX_DISPLAYA_ALT,
	TEGRA_MUX_DISPLAYB,
	TEGRA_MUX_DTV,
	TEGRA_MUX_EMC_DLL,
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
	TEGRA_MUX_NAND,
	TEGRA_MUX_NAND_ALT,
	TEGRA_MUX_OWR,
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
	TEGRA_MUX_SYSCLK,
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
};

static const char * const blink_groups[] = {
	"clk_32k_out_pa0",
};

static const char * const cec_groups[] = {
	"hdmi_cec_pee3",
};

static const char * const cldvfs_groups[] = {
	"gmi_ad9_ph1",
	"gmi_ad10_ph2",
	"kb_row7_pr7",
	"kb_row8_ps0",
	"dvfs_pwm_px0",
	"dvfs_clk_px2",
};

static const char * const clk12_groups[] = {
	"sdmmc1_wp_n_pv3",
	"sdmmc1_clk_pz0",
};

static const char * const cpu_groups[] = {
	"cpu_pwr_req",
};

static const char * const dap_groups[] = {
	"clk1_req_pee2",
	"clk2_req_pcc5",
};

static const char * const dap1_groups[] = {
	"clk1_req_pee2",
};

static const char * const dap2_groups[] = {
	"clk1_out_pw4",
	"gpio_x4_aud_px4",
};

static const char * const dev3_groups[] = {
	"clk3_req_pee1",
};

static const char * const displaya_groups[] = {
	"dap3_fs_pp0",
	"dap3_din_pp1",
	"dap3_dout_pp2",
	"dap3_sclk_pp3",
	"uart3_rts_n_pc0",
	"pu3",
	"pu4",
	"pu5",
	"pbb3",
	"pbb4",
	"pbb5",
	"pbb6",
	"kb_row3_pr3",
	"kb_row4_pr4",
	"kb_row5_pr5",
	"kb_row6_pr6",
	"kb_col3_pq3",
	"sdmmc3_dat2_pb5",
};

static const char * const displaya_alt_groups[] = {
	"kb_row6_pr6",
};

static const char * const displayb_groups[] = {
	"dap3_fs_pp0",
	"dap3_din_pp1",
	"dap3_dout_pp2",
	"dap3_sclk_pp3",
	"pu3",
	"pu4",
	"pu5",
	"pu6",
	"pbb3",
	"pbb4",
	"pbb5",
	"pbb6",
	"kb_row3_pr3",
	"kb_row4_pr4",
	"kb_row5_pr5",
	"kb_row6_pr6",
	"sdmmc3_dat3_pb4",
};

static const char * const dtv_groups[] = {
	"uart3_cts_n_pa1",
	"uart3_rts_n_pc0",
	"dap4_fs_pp4",
	"dap4_dout_pp6",
	"gmi_wait_pi7",
	"gmi_ad8_ph0",
	"gmi_ad14_ph6",
	"gmi_ad15_ph7",
};

static const char * const emc_dll_groups[] = {
	"kb_col0_pq0",
	"kb_col1_pq1",
};

static const char * const extperiph1_groups[] = {
	"clk1_out_pw4",
};

static const char * const extperiph2_groups[] = {
	"clk2_out_pw5",
};

static const char * const extperiph3_groups[] = {
	"clk3_out_pee0",
};

static const char * const gmi_groups[] = {
	"gmi_wp_n_pc7",

	"gmi_ad0_pg0",
	"gmi_ad1_pg1",
	"gmi_ad2_pg2",
	"gmi_ad3_pg3",
	"gmi_ad4_pg4",
	"gmi_ad5_pg5",
	"gmi_ad6_pg6",
	"gmi_ad7_pg7",
	"gmi_ad8_ph0",
	"gmi_ad9_ph1",
	"gmi_ad10_ph2",
	"gmi_ad11_ph3",
	"gmi_ad12_ph4",
	"gmi_ad13_ph5",
	"gmi_ad14_ph6",
	"gmi_ad15_ph7",
	"gmi_wr_n_pi0",
	"gmi_oe_n_pi1",
	"gmi_cs6_n_pi3",
	"gmi_rst_n_pi4",
	"gmi_iordy_pi5",
	"gmi_cs7_n_pi6",
	"gmi_wait_pi7",
	"gmi_cs0_n_pj0",
	"gmi_cs1_n_pj2",
	"gmi_dqs_p_pj3",
	"gmi_adv_n_pk0",
	"gmi_clk_pk1",
	"gmi_cs4_n_pk2",
	"gmi_cs2_n_pk3",
	"gmi_cs3_n_pk4",
	"gmi_a16_pj7",
	"gmi_a17_pb0",
	"gmi_a18_pb1",
	"gmi_a19_pk7",
	"gen2_i2c_scl_pt5",
	"gen2_i2c_sda_pt6",
	"sdmmc4_dat0_paa0",
	"sdmmc4_dat1_paa1",
	"sdmmc4_dat2_paa2",
	"sdmmc4_dat3_paa3",
	"sdmmc4_dat4_paa4",
	"sdmmc4_dat5_paa5",
	"sdmmc4_dat6_paa6",
	"sdmmc4_dat7_paa7",
	"sdmmc4_clk_pcc4",
	"sdmmc4_cmd_pt7",
	"dap1_fs_pn0",
	"dap1_din_pn1",
	"dap1_dout_pn2",
	"dap1_sclk_pn3",
};

static const char * const gmi_alt_groups[] = {
	"gmi_wp_n_pc7",
	"gmi_cs3_n_pk4",
	"gmi_a16_pj7",
};

static const char * const hda_groups[] = {
	"dap1_fs_pn0",
	"dap1_din_pn1",
	"dap1_dout_pn2",
	"dap1_sclk_pn3",
	"dap2_fs_pa2",
	"dap2_sclk_pa3",
	"dap2_din_pa4",
	"dap2_dout_pa5",
};

static const char * const hsi_groups[] = {
	"ulpi_data0_po1",
	"ulpi_data1_po2",
	"ulpi_data2_po3",
	"ulpi_data3_po4",
	"ulpi_data4_po5",
	"ulpi_data5_po6",
	"ulpi_data6_po7",
	"ulpi_data7_po0",
};

static const char * const i2c1_groups[] = {
	"gen1_i2c_scl_pc4",
	"gen1_i2c_sda_pc5",
	"gpio_w2_aud_pw2",
	"gpio_w3_aud_pw3",
};

static const char * const i2c2_groups[] = {
	"gen2_i2c_scl_pt5",
	"gen2_i2c_sda_pt6",
};

static const char * const i2c3_groups[] = {
	"cam_i2c_scl_pbb1",
	"cam_i2c_sda_pbb2",
};

static const char * const i2c4_groups[] = {
	"ddc_scl_pv4",
	"ddc_sda_pv5",
};

static const char * const i2cpwr_groups[] = {
	"pwr_i2c_scl_pz6",
	"pwr_i2c_sda_pz7",
};

static const char * const i2s0_groups[] = {
	"dap1_fs_pn0",
	"dap1_din_pn1",
	"dap1_dout_pn2",
	"dap1_sclk_pn3",
};

static const char * const i2s1_groups[] = {
	"dap2_fs_pa2",
	"dap2_sclk_pa3",
	"dap2_din_pa4",
	"dap2_dout_pa5",
};

static const char * const i2s2_groups[] = {
	"dap3_fs_pp0",
	"dap3_din_pp1",
	"dap3_dout_pp2",
	"dap3_sclk_pp3",
};

static const char * const i2s3_groups[] = {
	"dap4_fs_pp4",
	"dap4_din_pp5",
	"dap4_dout_pp6",
	"dap4_sclk_pp7",
};

static const char * const i2s4_groups[] = {
	"pcc1",
	"pbb0",
	"pbb7",
	"pcc2",
};

static const char * const irda_groups[] = {
	"uart2_rxd_pc3",
	"uart2_txd_pc2",
};

static const char * const kbc_groups[] = {
	"kb_row0_pr0",
	"kb_row1_pr1",
	"kb_row2_pr2",
	"kb_row3_pr3",
	"kb_row4_pr4",
	"kb_row5_pr5",
	"kb_row6_pr6",
	"kb_row7_pr7",
	"kb_row8_ps0",
	"kb_row9_ps1",
	"kb_row10_ps2",
	"kb_col0_pq0",
	"kb_col1_pq1",
	"kb_col2_pq2",
	"kb_col3_pq3",
	"kb_col4_pq4",
	"kb_col5_pq5",
	"kb_col6_pq6",
	"kb_col7_pq7",
};

static const char * const nand_groups[] = {
	"gmi_wp_n_pc7",
	"gmi_wait_pi7",
	"gmi_adv_n_pk0",
	"gmi_clk_pk1",
	"gmi_cs0_n_pj0",
	"gmi_cs1_n_pj2",
	"gmi_cs2_n_pk3",
	"gmi_cs3_n_pk4",
	"gmi_cs4_n_pk2",
	"gmi_cs6_n_pi3",
	"gmi_cs7_n_pi6",
	"gmi_ad0_pg0",
	"gmi_ad1_pg1",
	"gmi_ad2_pg2",
	"gmi_ad3_pg3",
	"gmi_ad4_pg4",
	"gmi_ad5_pg5",
	"gmi_ad6_pg6",
	"gmi_ad7_pg7",
	"gmi_ad8_ph0",
	"gmi_ad9_ph1",
	"gmi_ad10_ph2",
	"gmi_ad11_ph3",
	"gmi_ad12_ph4",
	"gmi_ad13_ph5",
	"gmi_ad14_ph6",
	"gmi_ad15_ph7",
	"gmi_wr_n_pi0",
	"gmi_oe_n_pi1",
	"gmi_dqs_p_pj3",
	"gmi_rst_n_pi4",
};

static const char * const nand_alt_groups[] = {
	"gmi_cs6_n_pi3",
	"gmi_cs7_n_pi6",
	"gmi_rst_n_pi4",
};

static const char * const owr_groups[] = {
	"pu0",
	"kb_col4_pq4",
	"owr",
	"sdmmc3_cd_n_pv2",
};

static const char * const pmi_groups[] = {
	"pwr_int_n",
};

static const char * const pwm0_groups[] = {
	"sdmmc1_dat2_py5",
	"uart3_rts_n_pc0",
	"pu3",
	"gmi_ad8_ph0",
	"sdmmc3_dat3_pb4",
};

static const char * const pwm1_groups[] = {
	"sdmmc1_dat1_py6",
	"pu4",
	"gmi_ad9_ph1",
	"sdmmc3_dat2_pb5",
};

static const char * const pwm2_groups[] = {
	"pu5",
	"gmi_ad10_ph2",
	"kb_col3_pq3",
	"sdmmc3_dat1_pb6",
};

static const char * const pwm3_groups[] = {
	"pu6",
	"gmi_ad11_ph3",
	"sdmmc3_cmd_pa7",
};

static const char * const pwron_groups[] = {
	"core_pwr_req",
};

static const char * const reset_out_n_groups[] = {
	"reset_out_n",
};

static const char * const rsvd1_groups[] = {
	"pv1",
	"hdmi_int_pn7",
	"pu1",
	"pu2",
	"gmi_wp_n_pc7",
	"gmi_adv_n_pk0",
	"gmi_cs0_n_pj0",
	"gmi_cs1_n_pj2",
	"gmi_ad0_pg0",
	"gmi_ad1_pg1",
	"gmi_ad2_pg2",
	"gmi_ad3_pg3",
	"gmi_ad4_pg4",
	"gmi_ad5_pg5",
	"gmi_ad6_pg6",
	"gmi_ad7_pg7",
	"gmi_wr_n_pi0",
	"gmi_oe_n_pi1",
	"gpio_x4_aud_px4",
	"gpio_x5_aud_px5",
	"gpio_x7_aud_px7",

	"reset_out_n",
};

static const char * const rsvd2_groups[] = {
	"pv0",
	"pv1",
	"sdmmc1_dat0_py7",
	"clk2_out_pw5",
	"clk2_req_pcc5",
	"hdmi_int_pn7",
	"ddc_scl_pv4",
	"ddc_sda_pv5",
	"uart3_txd_pw6",
	"uart3_rxd_pw7",
	"gen1_i2c_scl_pc4",
	"gen1_i2c_sda_pc5",
	"dap4_fs_pp4",
	"dap4_din_pp5",
	"dap4_dout_pp6",
	"dap4_sclk_pp7",
	"clk3_out_pee0",
	"clk3_req_pee1",
	"gmi_iordy_pi5",
	"gmi_a17_pb0",
	"gmi_a18_pb1",
	"gen2_i2c_scl_pt5",
	"gen2_i2c_sda_pt6",
	"sdmmc4_clk_pcc4",
	"sdmmc4_cmd_pt7",
	"sdmmc4_dat7_paa7",
	"pcc1",
	"pbb7",
	"pcc2",
	"pwr_i2c_scl_pz6",
	"pwr_i2c_sda_pz7",
	"kb_row0_pr0",
	"kb_row1_pr1",
	"kb_row2_pr2",
	"kb_row7_pr7",
	"kb_row8_ps0",
	"kb_row9_ps1",
	"kb_row10_ps2",
	"kb_col1_pq1",
	"kb_col2_pq2",
	"kb_col5_pq5",
	"kb_col6_pq6",
	"kb_col7_pq7",
	"sys_clk_req_pz5",
	"core_pwr_req",
	"cpu_pwr_req",
	"pwr_int_n",
	"owr",
	"spdif_out_pk5",
	"gpio_x1_aud_px1",
	"sdmmc3_clk_pa6",
	"sdmmc3_dat0_pb7",
	"gpio_w2_aud_pw2",
	"usb_vbus_en0_pn4",
	"usb_vbus_en1_pn5",
	"sdmmc3_clk_lb_out_pee4",
	"sdmmc3_clk_lb_in_pee5",
	"reset_out_n",
};

static const char * const rsvd3_groups[] = {
	"pv0",
	"pv1",
	"sdmmc1_clk_pz0",
	"clk2_out_pw5",
	"clk2_req_pcc5",
	"hdmi_int_pn7",
	"ddc_scl_pv4",
	"ddc_sda_pv5",
	"uart2_rts_n_pj6",
	"uart2_cts_n_pj5",
	"uart3_txd_pw6",
	"uart3_rxd_pw7",
	"pu0",
	"pu1",
	"pu2",
	"gen1_i2c_scl_pc4",
	"gen1_i2c_sda_pc5",
	"dap4_din_pp5",
	"dap4_sclk_pp7",
	"clk3_out_pee0",
	"clk3_req_pee1",
	"pcc1",
	"cam_i2c_scl_pbb1",
	"cam_i2c_sda_pbb2",
	"pbb7",
	"pcc2",
	"pwr_i2c_scl_pz6",
	"pwr_i2c_sda_pz7",
	"kb_row0_pr0",
	"kb_row1_pr1",
	"kb_row2_pr2",
	"kb_row3_pr3",
	"kb_row9_ps1",
	"kb_row10_ps2",
	"clk_32k_out_pa0",
	"sys_clk_req_pz5",
	"core_pwr_req",
	"cpu_pwr_req",
	"pwr_int_n",
	"owr",
	"clk1_req_pee2",
	"clk1_out_pw4",
	"spdif_out_pk5",
	"spdif_in_pk6",
	"dap2_fs_pa2",
	"dap2_sclk_pa3",
	"dap2_din_pa4",
	"dap2_dout_pa5",
	"dvfs_pwm_px0",
	"gpio_x1_aud_px1",
	"gpio_x3_aud_px3",
	"dvfs_clk_px2",
	"sdmmc3_clk_pa6",
	"sdmmc3_dat0_pb7",
	"hdmi_cec_pee3",
	"sdmmc3_cd_n_pv2",
	"usb_vbus_en0_pn4",
	"usb_vbus_en1_pn5",
	"sdmmc3_clk_lb_out_pee4",
	"sdmmc3_clk_lb_in_pee5",
	"reset_out_n",
};

static const char * const rsvd4_groups[] = {
	"pv0",
	"pv1",
	"sdmmc1_clk_pz0",
	"clk2_out_pw5",
	"clk2_req_pcc5",
	"hdmi_int_pn7",
	"ddc_scl_pv4",
	"ddc_sda_pv5",
	"pu0",
	"pu1",
	"pu2",
	"gen1_i2c_scl_pc4",
	"gen1_i2c_sda_pc5",
	"dap4_fs_pp4",
	"dap4_din_pp5",
	"dap4_dout_pp6",
	"dap4_sclk_pp7",
	"clk3_out_pee0",
	"clk3_req_pee1",
	"gmi_ad0_pg0",
	"gmi_ad1_pg1",
	"gmi_ad2_pg2",
	"gmi_ad3_pg3",
	"gmi_ad4_pg4",
	"gmi_ad12_ph4",
	"gmi_ad13_ph5",
	"gmi_rst_n_pi4",
	"gen2_i2c_scl_pt5",
	"gen2_i2c_sda_pt6",
	"sdmmc4_clk_pcc4",
	"sdmmc4_cmd_pt7",
	"sdmmc4_dat0_paa0",
	"sdmmc4_dat1_paa1",
	"sdmmc4_dat2_paa2",
	"sdmmc4_dat3_paa3",
	"sdmmc4_dat4_paa4",
	"sdmmc4_dat5_paa5",
	"sdmmc4_dat6_paa6",
	"sdmmc4_dat7_paa7",
	"cam_mclk_pcc0",
	"pcc1",
	"cam_i2c_scl_pbb1",
	"cam_i2c_sda_pbb2",
	"pbb3",
	"pbb4",
	"pbb5",
	"pbb6",
	"pbb7",
	"pcc2",
	"pwr_i2c_scl_pz6",
	"pwr_i2c_sda_pz7",
	"kb_row0_pr0",
	"kb_row1_pr1",
	"kb_row2_pr2",
	"kb_col2_pq2",
	"kb_col5_pq5",
	"kb_col6_pq6",
	"kb_col7_pq7",
	"clk_32k_out_pa0",
	"sys_clk_req_pz5",
	"core_pwr_req",
	"cpu_pwr_req",
	"pwr_int_n",
	"owr",
	"dap1_fs_pn0",
	"dap1_din_pn1",
	"dap1_dout_pn2",
	"dap1_sclk_pn3",
	"clk1_req_pee2",
	"clk1_out_pw4",
	"spdif_in_pk6",
	"spdif_out_pk5",
	"dap2_fs_pa2",
	"dap2_sclk_pa3",
	"dap2_din_pa4",
	"dap2_dout_pa5",
	"dvfs_pwm_px0",
	"gpio_x1_aud_px1",
	"gpio_x3_aud_px3",
	"dvfs_clk_px2",
	"gpio_x5_aud_px5",
	"gpio_x6_aud_px6",
	"gpio_x7_aud_px7",
	"sdmmc3_cd_n_pv2",
	"usb_vbus_en0_pn4",
	"usb_vbus_en1_pn5",
	"sdmmc3_clk_lb_in_pee5",
	"sdmmc3_clk_lb_out_pee4",
};

static const char * const sdmmc1_groups[] = {

	"sdmmc1_clk_pz0",
	"sdmmc1_cmd_pz1",
	"sdmmc1_dat3_py4",
	"sdmmc1_dat2_py5",
	"sdmmc1_dat1_py6",
	"sdmmc1_dat0_py7",
	"uart3_cts_n_pa1",
	"kb_col5_pq5",
	"sdmmc1_wp_n_pv3",
};

static const char * const sdmmc2_groups[] = {
	"gmi_iordy_pi5",
	"gmi_clk_pk1",
	"gmi_cs2_n_pk3",
	"gmi_cs3_n_pk4",
	"gmi_cs7_n_pi6",
	"gmi_ad12_ph4",
	"gmi_ad13_ph5",
	"gmi_ad14_ph6",
	"gmi_ad15_ph7",
	"gmi_dqs_p_pj3",
};

static const char * const sdmmc3_groups[] = {
	"kb_col4_pq4",
	"sdmmc3_clk_pa6",
	"sdmmc3_cmd_pa7",
	"sdmmc3_dat0_pb7",
	"sdmmc3_dat1_pb6",
	"sdmmc3_dat2_pb5",
	"sdmmc3_dat3_pb4",
	"hdmi_cec_pee3",
	"sdmmc3_cd_n_pv2",
	"sdmmc3_clk_lb_in_pee5",
	"sdmmc3_clk_lb_out_pee4",
};

static const char * const sdmmc4_groups[] = {
	"sdmmc4_clk_pcc4",
	"sdmmc4_cmd_pt7",
	"sdmmc4_dat0_paa0",
	"sdmmc4_dat1_paa1",
	"sdmmc4_dat2_paa2",
	"sdmmc4_dat3_paa3",
	"sdmmc4_dat4_paa4",
	"sdmmc4_dat5_paa5",
	"sdmmc4_dat6_paa6",
	"sdmmc4_dat7_paa7",
};

static const char * const soc_groups[] = {
	"gmi_cs1_n_pj2",
	"gmi_oe_n_pi1",
	"clk_32k_out_pa0",
	"hdmi_cec_pee3",
};

static const char * const spdif_groups[] = {
	"sdmmc1_cmd_pz1",
	"sdmmc1_dat3_py4",
	"uart2_rxd_pc3",
	"uart2_txd_pc2",
	"spdif_in_pk6",
	"spdif_out_pk5",
};

static const char * const spi1_groups[] = {
	"ulpi_clk_py0",
	"ulpi_dir_py1",
	"ulpi_nxt_py2",
	"ulpi_stp_py3",
	"gpio_x3_aud_px3",
	"gpio_x4_aud_px4",
	"gpio_x5_aud_px5",
	"gpio_x6_aud_px6",
	"gpio_x7_aud_px7",
	"gpio_w3_aud_pw3",
};

static const char * const spi2_groups[] = {
	"ulpi_data4_po5",
	"ulpi_data5_po6",
	"ulpi_data6_po7",
	"ulpi_data7_po0",
	"kb_row4_pr4",
	"kb_row5_pr5",
	"kb_col0_pq0",
	"kb_col1_pq1",
	"kb_col2_pq2",
	"kb_col6_pq6",
	"kb_col7_pq7",
	"gpio_x4_aud_px4",
	"gpio_x5_aud_px5",
	"gpio_x6_aud_px6",
	"gpio_x7_aud_px7",
	"gpio_w2_aud_pw2",
	"gpio_w3_aud_pw3",
};

static const char * const spi3_groups[] = {
	"ulpi_data0_po1",
	"ulpi_data1_po2",
	"ulpi_data2_po3",
	"ulpi_data3_po4",
	"sdmmc4_dat0_paa0",
	"sdmmc4_dat1_paa1",
	"sdmmc4_dat2_paa2",
	"sdmmc4_dat3_paa3",
	"sdmmc4_dat4_paa4",
	"sdmmc4_dat5_paa5",
	"sdmmc4_dat6_paa6",
	"sdmmc3_clk_pa6",
	"sdmmc3_cmd_pa7",
	"sdmmc3_dat0_pb7",
	"sdmmc3_dat1_pb6",
	"sdmmc3_dat2_pb5",
	"sdmmc3_dat3_pb4",
};

static const char * const spi4_groups[] = {
	"sdmmc1_cmd_pz1",
	"sdmmc1_dat3_py4",
	"sdmmc1_dat2_py5",
	"sdmmc1_dat1_py6",
	"sdmmc1_dat0_py7",
	"uart2_rxd_pc3",
	"uart2_txd_pc2",
	"uart2_rts_n_pj6",
	"uart2_cts_n_pj5",
	"uart3_txd_pw6",
	"uart3_rxd_pw7",
	"uart3_cts_n_pa1",
	"gmi_wait_pi7",
	"gmi_cs6_n_pi3",
	"gmi_ad5_pg5",
	"gmi_ad6_pg6",
	"gmi_ad7_pg7",
	"gmi_a19_pk7",
	"gmi_wr_n_pi0",
	"sdmmc1_wp_n_pv3",
};

static const char * const spi5_groups[] = {
	"ulpi_clk_py0",
	"ulpi_dir_py1",
	"ulpi_nxt_py2",
	"ulpi_stp_py3",
	"dap3_fs_pp0",
	"dap3_din_pp1",
	"dap3_dout_pp2",
	"dap3_sclk_pp3",
};

static const char * const spi6_groups[] = {
	"dvfs_pwm_px0",
	"gpio_x1_aud_px1",
	"gpio_x3_aud_px3",
	"dvfs_clk_px2",
	"gpio_x6_aud_px6",
	"gpio_w2_aud_pw2",
	"gpio_w3_aud_pw3",
};

static const char * const sysclk_groups[] = {
	"sys_clk_req_pz5",
};

static const char * const trace_groups[] = {
	"gmi_iordy_pi5",
	"gmi_adv_n_pk0",
	"gmi_clk_pk1",
	"gmi_cs2_n_pk3",
	"gmi_cs4_n_pk2",
	"gmi_a16_pj7",
	"gmi_a17_pb0",
	"gmi_a18_pb1",
	"gmi_a19_pk7",
	"gmi_dqs_p_pj3",
};

static const char * const uarta_groups[] = {
	"ulpi_data0_po1",
	"ulpi_data1_po2",
	"ulpi_data2_po3",
	"ulpi_data3_po4",
	"ulpi_data4_po5",
	"ulpi_data5_po6",
	"ulpi_data6_po7",
	"ulpi_data7_po0",
	"sdmmc1_cmd_pz1",
	"sdmmc1_dat3_py4",
	"sdmmc1_dat2_py5",
	"sdmmc1_dat1_py6",
	"sdmmc1_dat0_py7",
	"uart2_rxd_pc3",
	"uart2_txd_pc2",
	"uart2_rts_n_pj6",
	"uart2_cts_n_pj5",
	"pu0",
	"pu1",
	"pu2",
	"pu3",
	"pu4",
	"pu5",
	"pu6",
	"kb_row7_pr7",
	"kb_row8_ps0",
	"kb_row9_ps1",
	"kb_row10_ps2",
	"kb_col3_pq3",
	"kb_col4_pq4",
	"sdmmc3_cmd_pa7",
	"sdmmc3_dat1_pb6",
	"sdmmc1_wp_n_pv3",
};

static const char * const uartb_groups[] = {
	"uart2_rts_n_pj6",
	"uart2_cts_n_pj5",
};

static const char * const uartc_groups[] = {
	"uart3_txd_pw6",
	"uart3_rxd_pw7",
	"uart3_cts_n_pa1",
	"uart3_rts_n_pc0",
};

static const char * const uartd_groups[] = {
	"ulpi_clk_py0",
	"ulpi_dir_py1",
	"ulpi_nxt_py2",
	"ulpi_stp_py3",
	"gmi_a16_pj7",
	"gmi_a17_pb0",
	"gmi_a18_pb1",
	"gmi_a19_pk7",
};

static const char * const ulpi_groups[] = {
	"ulpi_data0_po1",
	"ulpi_data1_po2",
	"ulpi_data2_po3",
	"ulpi_data3_po4",
	"ulpi_data4_po5",
	"ulpi_data5_po6",
	"ulpi_data6_po7",
	"ulpi_data7_po0",
	"ulpi_clk_py0",
	"ulpi_dir_py1",
	"ulpi_nxt_py2",
	"ulpi_stp_py3",
};

static const char * const usb_groups[] = {
	"pv0",
	"pu6",
	"gmi_cs0_n_pj0",
	"gmi_cs4_n_pk2",
	"gmi_ad11_ph3",
	"kb_col0_pq0",
	"spdif_in_pk6",
	"usb_vbus_en0_pn4",
	"usb_vbus_en1_pn5",
};

static const char * const vgp1_groups[] = {
	"cam_i2c_scl_pbb1",
};

static const char * const vgp2_groups[] = {
	"cam_i2c_sda_pbb2",
};

static const char * const vgp3_groups[] = {
	"pbb3",
};

static const char * const vgp4_groups[] = {
	"pbb4",
};

static const char * const vgp5_groups[] = {
	"pbb5",
};

static const char * const vgp6_groups[] = {
	"pbb6",
};

static const char * const vi_groups[] = {
	"cam_mclk_pcc0",
	"pbb0",
};

static const char * const vi_alt1_groups[] = {
	"cam_mclk_pcc0",
	"pbb0",
};

static const char * const vi_alt3_groups[] = {
	"cam_mclk_pcc0",
	"pbb0",
};

#define FUNCTION(fname)					\
	{						\
		.name = #fname,				\
		.groups = fname##_groups,		\
		.ngroups = ARRAY_SIZE(fname##_groups),	\
	}

static const struct tegra_function  tegra114_functions[] = {
	FUNCTION(blink),
	FUNCTION(cec),
	FUNCTION(cldvfs),
	FUNCTION(clk12),
	FUNCTION(cpu),
	FUNCTION(dap),
	FUNCTION(dap1),
	FUNCTION(dap2),
	FUNCTION(dev3),
	FUNCTION(displaya),
	FUNCTION(displaya_alt),
	FUNCTION(displayb),
	FUNCTION(dtv),
	FUNCTION(emc_dll),
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
	FUNCTION(nand),
	FUNCTION(nand_alt),
	FUNCTION(owr),
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
	FUNCTION(sysclk),
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
};

#define DRV_PINGROUP_REG_START			0x868	/* bank 0 */
#define PINGROUP_REG_START			0x3000	/* bank 1 */

#define PINGROUP_REG_Y(r)			((r) - PINGROUP_REG_START)
#define PINGROUP_REG_N(r)			-1

#define PINGROUP(pg_name, f0, f1, f2, f3, f_safe, r, od, ior, rcv_sel)	\
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
		.func_safe = TEGRA_MUX_##f_safe,			\
		.mux_reg = PINGROUP_REG_Y(r),				\
		.mux_bank = 1,						\
		.mux_bit = 0,						\
		.pupd_reg = PINGROUP_REG_Y(r),				\
		.pupd_bank = 1,						\
		.pupd_bit = 2,						\
		.tri_reg = PINGROUP_REG_Y(r),				\
		.tri_bank = 1,						\
		.tri_bit = 4,						\
		.einput_reg = PINGROUP_REG_Y(r),			\
		.einput_bank = 1,					\
		.einput_bit = 5,					\
		.odrain_reg = PINGROUP_REG_##od(r),			\
		.odrain_bank = 1,					\
		.odrain_bit = 6,					\
		.lock_reg = PINGROUP_REG_Y(r),				\
		.lock_bank = 1,						\
		.lock_bit = 7,						\
		.ioreset_reg = PINGROUP_REG_##ior(r),			\
		.ioreset_bank = 1,					\
		.ioreset_bit = 8,					\
		.rcv_sel_reg = PINGROUP_REG_##rcv_sel(r),		\
		.rcv_sel_bank = 1,					\
		.rcv_sel_bit = 9,					\
		.drv_reg = -1,						\
		.drvtype_reg = -1,					\
	}

#define DRV_PINGROUP_DVRTYPE_Y(r) ((r) - DRV_PINGROUP_REG_START)
#define DRV_PINGROUP_DVRTYPE_N(r) -1

#define DRV_PINGROUP(pg_name, r, hsm_b, schmitt_b, lpmd_b,		\
			drvdn_b, drvdn_w, drvup_b, drvup_w,		\
			slwr_b, slwr_w, slwf_b, slwf_w,			\
			drvtype)					\
	{								\
		.name = "drive_" #pg_name,				\
		.pins = drive_##pg_name##_pins,				\
		.npins = ARRAY_SIZE(drive_##pg_name##_pins),		\
		.mux_reg = -1,						\
		.pupd_reg = -1,						\
		.tri_reg = -1,						\
		.einput_reg = -1,					\
		.odrain_reg = -1,					\
		.lock_reg = -1,						\
		.ioreset_reg = -1,					\
		.rcv_sel_reg = -1,					\
		.drv_reg = DRV_PINGROUP_DVRTYPE_Y(r),			\
		.drv_bank = 0,						\
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
		.drvtype_reg = DRV_PINGROUP_DVRTYPE_##drvtype(r),	\
		.drvtype_bank = 0,					\
		.drvtype_bit = 6,					\
	}

static const struct tegra_pingroup tegra114_groups[] = {
	/*       pg_name,                f0,         f1,         f2,           f3,          safe,     r,      od, ior, rcv_sel */
	/* FIXME: Fill in correct data in safe column */
	PINGROUP(ulpi_data0_po1,         SPI3,       HSI,        UARTA,        ULPI,        ULPI,     0x3000,  N,  N,  N),
	PINGROUP(ulpi_data1_po2,         SPI3,       HSI,        UARTA,        ULPI,        ULPI,     0x3004,  N,  N,  N),
	PINGROUP(ulpi_data2_po3,         SPI3,       HSI,        UARTA,        ULPI,        ULPI,     0x3008,  N,  N,  N),
	PINGROUP(ulpi_data3_po4,         SPI3,       HSI,        UARTA,        ULPI,        ULPI,     0x300c,  N,  N,  N),
	PINGROUP(ulpi_data4_po5,         SPI2,       HSI,        UARTA,        ULPI,        ULPI,     0x3010,  N,  N,  N),
	PINGROUP(ulpi_data5_po6,         SPI2,       HSI,        UARTA,        ULPI,        ULPI,     0x3014,  N,  N,  N),
	PINGROUP(ulpi_data6_po7,         SPI2,       HSI,        UARTA,        ULPI,        ULPI,     0x3018,  N,  N,  N),
	PINGROUP(ulpi_data7_po0,         SPI2,       HSI,        UARTA,        ULPI,        ULPI,     0x301c,  N,  N,  N),
	PINGROUP(ulpi_clk_py0,           SPI1,       SPI5,       UARTD,        ULPI,        ULPI,     0x3020,  N,  N,  N),
	PINGROUP(ulpi_dir_py1,           SPI1,       SPI5,       UARTD,        ULPI,        ULPI,     0x3024,  N,  N,  N),
	PINGROUP(ulpi_nxt_py2,           SPI1,       SPI5,       UARTD,        ULPI,        ULPI,     0x3028,  N,  N,  N),
	PINGROUP(ulpi_stp_py3,           SPI1,       SPI5,       UARTD,        ULPI,        ULPI,     0x302c,  N,  N,  N),
	PINGROUP(dap3_fs_pp0,            I2S2,       SPI5,       DISPLAYA,     DISPLAYB,    I2S2,     0x3030,  N,  N,  N),
	PINGROUP(dap3_din_pp1,           I2S2,       SPI5,       DISPLAYA,     DISPLAYB,    I2S2,     0x3034,  N,  N,  N),
	PINGROUP(dap3_dout_pp2,          I2S2,       SPI5,       DISPLAYA,     DISPLAYB,    I2S2,     0x3038,  N,  N,  N),
	PINGROUP(dap3_sclk_pp3,          I2S2,       SPI5,       DISPLAYA,     DISPLAYB,    I2S2,     0x303c,  N,  N,  N),
	PINGROUP(pv0,                    USB,        RSVD2,      RSVD3,        RSVD4,       RSVD4,    0x3040,  N,  N,  N),
	PINGROUP(pv1,                    RSVD1,      RSVD2,      RSVD3,        RSVD4,       RSVD4,    0x3044,  N,  N,  N),
	PINGROUP(sdmmc1_clk_pz0,         SDMMC1,     CLK12,      RSVD3,        RSVD4,       RSVD4,    0x3048,  N,  N,  N),
	PINGROUP(sdmmc1_cmd_pz1,         SDMMC1,     SPDIF,      SPI4,         UARTA,       SDMMC1,   0x304c,  N,  N,  N),
	PINGROUP(sdmmc1_dat3_py4,        SDMMC1,     SPDIF,      SPI4,         UARTA,       SDMMC1,   0x3050,  N,  N,  N),
	PINGROUP(sdmmc1_dat2_py5,        SDMMC1,     PWM0,       SPI4,         UARTA,       SDMMC1,   0x3054,  N,  N,  N),
	PINGROUP(sdmmc1_dat1_py6,        SDMMC1,     PWM1,       SPI4,         UARTA,       SDMMC1,   0x3058,  N,  N,  N),
	PINGROUP(sdmmc1_dat0_py7,        SDMMC1,     RSVD2,      SPI4,         UARTA,       RSVD2,    0x305c,  N,  N,  N),
	PINGROUP(clk2_out_pw5,           EXTPERIPH2, RSVD2,      RSVD3,        RSVD4,       RSVD4,    0x3068,  N,  N,  N),
	PINGROUP(clk2_req_pcc5,          DAP,        RSVD2,      RSVD3,        RSVD4,       RSVD4,    0x306c,  N,  N,  N),
	PINGROUP(hdmi_int_pn7,           RSVD1,      RSVD2,      RSVD3,        RSVD4,       RSVD4,    0x3110,  N,  N,  Y),
	PINGROUP(ddc_scl_pv4,            I2C4,       RSVD2,      RSVD3,        RSVD4,       RSVD4,    0x3114,  N,  N,  Y),
	PINGROUP(ddc_sda_pv5,            I2C4,       RSVD2,      RSVD3,        RSVD4,       RSVD4,    0x3118,  N,  N,  Y),
	PINGROUP(uart2_rxd_pc3,          IRDA,       SPDIF,      UARTA,        SPI4,        IRDA,     0x3164,  N,  N,  N),
	PINGROUP(uart2_txd_pc2,          IRDA,       SPDIF,      UARTA,        SPI4,        IRDA,     0x3168,  N,  N,  N),
	PINGROUP(uart2_rts_n_pj6,        UARTA,      UARTB,      RSVD3,        SPI4,        RSVD3,    0x316c,  N,  N,  N),
	PINGROUP(uart2_cts_n_pj5,        UARTA,      UARTB,      RSVD3,        SPI4,        RSVD3,    0x3170,  N,  N,  N),
	PINGROUP(uart3_txd_pw6,          UARTC,      RSVD2,      RSVD3,        SPI4,        RSVD3,    0x3174,  N,  N,  N),
	PINGROUP(uart3_rxd_pw7,          UARTC,      RSVD2,      RSVD3,        SPI4,        RSVD3,    0x3178,  N,  N,  N),
	PINGROUP(uart3_cts_n_pa1,        UARTC,      SDMMC1,     DTV,          SPI4,        UARTC,    0x317c,  N,  N,  N),
	PINGROUP(uart3_rts_n_pc0,        UARTC,      PWM0,       DTV,          DISPLAYA,    UARTC,    0x3180,  N,  N,  N),
	PINGROUP(pu0,                    OWR,        UARTA,      RSVD3,        RSVD4,       RSVD4,    0x3184,  N,  N,  N),
	PINGROUP(pu1,                    RSVD1,      UARTA,      RSVD3,        RSVD4,       RSVD4,    0x3188,  N,  N,  N),
	PINGROUP(pu2,                    RSVD1,      UARTA,      RSVD3,        RSVD4,       RSVD4,    0x318c,  N,  N,  N),
	PINGROUP(pu3,                    PWM0,       UARTA,      DISPLAYA,     DISPLAYB,    PWM0,     0x3190,  N,  N,  N),
	PINGROUP(pu4,                    PWM1,       UARTA,      DISPLAYA,     DISPLAYB,    PWM1,     0x3194,  N,  N,  N),
	PINGROUP(pu5,                    PWM2,       UARTA,      DISPLAYA,     DISPLAYB,    PWM2,     0x3198,  N,  N,  N),
	PINGROUP(pu6,                    PWM3,       UARTA,      USB,          DISPLAYB,    PWM3,     0x319c,  N,  N,  N),
	PINGROUP(gen1_i2c_sda_pc5,       I2C1,       RSVD2,      RSVD3,        RSVD4,       RSVD4,    0x31a0,  Y,  N,  N),
	PINGROUP(gen1_i2c_scl_pc4,       I2C1,       RSVD2,      RSVD3,        RSVD4,       RSVD4,    0x31a4,  Y,  N,  N),
	PINGROUP(dap4_fs_pp4,            I2S3,       RSVD2,      DTV,          RSVD4,       RSVD4,    0x31a8,  N,  N,  N),
	PINGROUP(dap4_din_pp5,           I2S3,       RSVD2,      RSVD3,        RSVD4,       RSVD4,    0x31ac,  N,  N,  N),
	PINGROUP(dap4_dout_pp6,          I2S3,       RSVD2,      DTV,          RSVD4,       RSVD4,    0x31b0,  N,  N,  N),
	PINGROUP(dap4_sclk_pp7,          I2S3,       RSVD2,      RSVD3,        RSVD4,       RSVD4,    0x31b4,  N,  N,  N),
	PINGROUP(clk3_out_pee0,          EXTPERIPH3, RSVD2,      RSVD3,        RSVD4,       RSVD4,    0x31b8,  N,  N,  N),
	PINGROUP(clk3_req_pee1,          DEV3,       RSVD2,      RSVD3,        RSVD4,       RSVD4,    0x31bc,  N,  N,  N),
	PINGROUP(gmi_wp_n_pc7,           RSVD1,      NAND,       GMI,          GMI_ALT,     RSVD1,    0x31c0,  N,  N,  N),
	PINGROUP(gmi_iordy_pi5,          SDMMC2,     RSVD2,      GMI,          TRACE,       RSVD2,    0x31c4,  N,  N,  N),
	PINGROUP(gmi_wait_pi7,           SPI4,       NAND,       GMI,          DTV,         NAND,     0x31c8,  N,  N,  N),
	PINGROUP(gmi_adv_n_pk0,          RSVD1,      NAND,       GMI,          TRACE,       RSVD1,    0x31cc,  N,  N,  N),
	PINGROUP(gmi_clk_pk1,            SDMMC2,     NAND,       GMI,          TRACE,       GMI,      0x31d0,  N,  N,  N),
	PINGROUP(gmi_cs0_n_pj0,          RSVD1,      NAND,       GMI,          USB,         RSVD1,    0x31d4,  N,  N,  N),
	PINGROUP(gmi_cs1_n_pj2,          RSVD1,      NAND,       GMI,          SOC,         RSVD1,    0x31d8,  N,  N,  N),
	PINGROUP(gmi_cs2_n_pk3,          SDMMC2,     NAND,       GMI,          TRACE,       GMI,      0x31dc,  N,  N,  N),
	PINGROUP(gmi_cs3_n_pk4,          SDMMC2,     NAND,       GMI,          GMI_ALT,     GMI,      0x31e0,  N,  N,  N),
	PINGROUP(gmi_cs4_n_pk2,          USB,        NAND,       GMI,          TRACE,       GMI,      0x31e4,  N,  N,  N),
	PINGROUP(gmi_cs6_n_pi3,          NAND,       NAND_ALT,   GMI,          SPI4,        NAND,     0x31e8,  N,  N,  N),
	PINGROUP(gmi_cs7_n_pi6,          NAND,       NAND_ALT,   GMI,          SDMMC2,      NAND,     0x31ec,  N,  N,  N),
	PINGROUP(gmi_ad0_pg0,            RSVD1,      NAND,       GMI,          RSVD4,       RSVD4,    0x31f0,  N,  N,  N),
	PINGROUP(gmi_ad1_pg1,            RSVD1,      NAND,       GMI,          RSVD4,       RSVD4,    0x31f4,  N,  N,  N),
	PINGROUP(gmi_ad2_pg2,            RSVD1,      NAND,       GMI,          RSVD4,       RSVD4,    0x31f8,  N,  N,  N),
	PINGROUP(gmi_ad3_pg3,            RSVD1,      NAND,       GMI,          RSVD4,       RSVD4,    0x31fc,  N,  N,  N),
	PINGROUP(gmi_ad4_pg4,            RSVD1,      NAND,       GMI,          RSVD4,       RSVD4,    0x3200,  N,  N,  N),
	PINGROUP(gmi_ad5_pg5,            RSVD1,      NAND,       GMI,          SPI4,        RSVD1,    0x3204,  N,  N,  N),
	PINGROUP(gmi_ad6_pg6,            RSVD1,      NAND,       GMI,          SPI4,        RSVD1,    0x3208,  N,  N,  N),
	PINGROUP(gmi_ad7_pg7,            RSVD1,      NAND,       GMI,          SPI4,        RSVD1,    0x320c,  N,  N,  N),
	PINGROUP(gmi_ad8_ph0,            PWM0,       NAND,       GMI,          DTV,         GMI,      0x3210,  N,  N,  N),
	PINGROUP(gmi_ad9_ph1,            PWM1,       NAND,       GMI,          CLDVFS,      GMI,      0x3214,  N,  N,  N),
	PINGROUP(gmi_ad10_ph2,           PWM2,       NAND,       GMI,          CLDVFS,      GMI,      0x3218,  N,  N,  N),
	PINGROUP(gmi_ad11_ph3,           PWM3,       NAND,       GMI,          USB,         GMI,      0x321c,  N,  N,  N),
	PINGROUP(gmi_ad12_ph4,           SDMMC2,     NAND,       GMI,          RSVD4,       RSVD4,    0x3220,  N,  N,  N),
	PINGROUP(gmi_ad13_ph5,           SDMMC2,     NAND,       GMI,          RSVD4,       RSVD4,    0x3224,  N,  N,  N),
	PINGROUP(gmi_ad14_ph6,           SDMMC2,     NAND,       GMI,          DTV,         GMI,      0x3228,  N,  N,  N),
	PINGROUP(gmi_ad15_ph7,           SDMMC2,     NAND,       GMI,          DTV,         GMI,      0x322c,  N,  N,  N),
	PINGROUP(gmi_a16_pj7,            UARTD,      TRACE,      GMI,          GMI_ALT,     GMI,      0x3230,  N,  N,  N),
	PINGROUP(gmi_a17_pb0,            UARTD,      RSVD2,      GMI,          TRACE,       RSVD2,    0x3234,  N,  N,  N),
	PINGROUP(gmi_a18_pb1,            UARTD,      RSVD2,      GMI,          TRACE,       RSVD2,    0x3238,  N,  N,  N),
	PINGROUP(gmi_a19_pk7,            UARTD,      SPI4,       GMI,          TRACE,       GMI,      0x323c,  N,  N,  N),
	PINGROUP(gmi_wr_n_pi0,           RSVD1,      NAND,       GMI,          SPI4,        RSVD1,    0x3240,  N,  N,  N),
	PINGROUP(gmi_oe_n_pi1,           RSVD1,      NAND,       GMI,          SOC,         RSVD1,    0x3244,  N,  N,  N),
	PINGROUP(gmi_dqs_p_pj3,          SDMMC2,     NAND,       GMI,          TRACE,       NAND,     0x3248,  N,  N,  N),
	PINGROUP(gmi_rst_n_pi4,          NAND,       NAND_ALT,   GMI,          RSVD4,       RSVD4,    0x324c,  N,  N,  N),
	PINGROUP(gen2_i2c_scl_pt5,       I2C2,       RSVD2,      GMI,          RSVD4,       RSVD4,    0x3250,  Y,  N,  N),
	PINGROUP(gen2_i2c_sda_pt6,       I2C2,       RSVD2,      GMI,          RSVD4,       RSVD4,    0x3254,  Y,  N,  N),
	PINGROUP(sdmmc4_clk_pcc4,        SDMMC4,     RSVD2,      GMI,          RSVD4,       RSVD4,    0x3258,  N,  Y,  N),
	PINGROUP(sdmmc4_cmd_pt7,         SDMMC4,     RSVD2,      GMI,          RSVD4,       RSVD4,    0x325c,  N,  Y,  N),
	PINGROUP(sdmmc4_dat0_paa0,       SDMMC4,     SPI3,       GMI,          RSVD4,       RSVD4,    0x3260,  N,  Y,  N),
	PINGROUP(sdmmc4_dat1_paa1,       SDMMC4,     SPI3,       GMI,          RSVD4,       RSVD4,    0x3264,  N,  Y,  N),
	PINGROUP(sdmmc4_dat2_paa2,       SDMMC4,     SPI3,       GMI,          RSVD4,       RSVD4,    0x3268,  N,  Y,  N),
	PINGROUP(sdmmc4_dat3_paa3,       SDMMC4,     SPI3,       GMI,          RSVD4,       RSVD4,    0x326c,  N,  Y,  N),
	PINGROUP(sdmmc4_dat4_paa4,       SDMMC4,     SPI3,       GMI,          RSVD4,       RSVD4,    0x3270,  N,  Y,  N),
	PINGROUP(sdmmc4_dat5_paa5,       SDMMC4,     SPI3,       GMI,          RSVD4,       RSVD4,    0x3274,  N,  Y,  N),
	PINGROUP(sdmmc4_dat6_paa6,       SDMMC4,     SPI3,       GMI,          RSVD4,       RSVD4,    0x3278,  N,  Y,  N),
	PINGROUP(sdmmc4_dat7_paa7,       SDMMC4,     RSVD2,      GMI,          RSVD4,       RSVD4,    0x327c,  N,  Y,  N),
	PINGROUP(cam_mclk_pcc0,          VI,         VI_ALT1,    VI_ALT3,      RSVD4,       RSVD4,    0x3284,  N,  N,  N),
	PINGROUP(pcc1,                   I2S4,       RSVD2,      RSVD3,        RSVD4,       RSVD4,    0x3288,  N,  N,  N),
	PINGROUP(pbb0,                   I2S4,       VI,         VI_ALT1,      VI_ALT3,     I2S4,     0x328c,  N,  N,  N),
	PINGROUP(cam_i2c_scl_pbb1,       VGP1,       I2C3,       RSVD3,        RSVD4,       RSVD4,    0x3290,  Y,  N,  N),
	PINGROUP(cam_i2c_sda_pbb2,       VGP2,       I2C3,       RSVD3,        RSVD4,       RSVD4,    0x3294,  Y,  N,  N),
	PINGROUP(pbb3,                   VGP3,       DISPLAYA,   DISPLAYB,     RSVD4,       RSVD4,    0x3298,  N,  N,  N),
	PINGROUP(pbb4,                   VGP4,       DISPLAYA,   DISPLAYB,     RSVD4,       RSVD4,    0x329c,  N,  N,  N),
	PINGROUP(pbb5,                   VGP5,       DISPLAYA,   DISPLAYB,     RSVD4,       RSVD4,    0x32a0,  N,  N,  N),
	PINGROUP(pbb6,                   VGP6,       DISPLAYA,   DISPLAYB,     RSVD4,       RSVD4,    0x32a4,  N,  N,  N),
	PINGROUP(pbb7,                   I2S4,       RSVD2,      RSVD3,        RSVD4,       RSVD4,    0x32a8,  N,  N,  N),
	PINGROUP(pcc2,                   I2S4,       RSVD2,      RSVD3,        RSVD4,       RSVD4,    0x32ac,  N,  N,  N),
	PINGROUP(pwr_i2c_scl_pz6,        I2CPWR,     RSVD2,      RSVD3,        RSVD4,       RSVD4,    0x32b4,  Y,  N,  N),
	PINGROUP(pwr_i2c_sda_pz7,        I2CPWR,     RSVD2,      RSVD3,        RSVD4,       RSVD4,    0x32b8,  Y,  N,  N),
	PINGROUP(kb_row0_pr0,            KBC,        RSVD2,      RSVD3,        RSVD4,       RSVD4,    0x32bc,  N,  N,  N),
	PINGROUP(kb_row1_pr1,            KBC,        RSVD2,      RSVD3,        RSVD4,       RSVD4,    0x32c0,  N,  N,  N),
	PINGROUP(kb_row2_pr2,            KBC,        RSVD2,      RSVD3,        RSVD4,       RSVD4,    0x32c4,  N,  N,  N),
	PINGROUP(kb_row3_pr3,            KBC,        DISPLAYA,   RSVD3,        DISPLAYB,    RSVD3,    0x32c8,  N,  N,  N),
	PINGROUP(kb_row4_pr4,            KBC,        DISPLAYA,   SPI2,         DISPLAYB,    KBC,      0x32cc,  N,  N,  N),
	PINGROUP(kb_row5_pr5,            KBC,        DISPLAYA,   SPI2,         DISPLAYB,    KBC,      0x32d0,  N,  N,  N),
	PINGROUP(kb_row6_pr6,            KBC,        DISPLAYA,   DISPLAYA_ALT, DISPLAYB,    KBC,      0x32d4,  N,  N,  N),
	PINGROUP(kb_row7_pr7,            KBC,        RSVD2,      CLDVFS,       UARTA,       RSVD2,    0x32d8,  N,  N,  N),
	PINGROUP(kb_row8_ps0,            KBC,        RSVD2,      CLDVFS,       UARTA,       RSVD2,    0x32dc,  N,  N,  N),
	PINGROUP(kb_row9_ps1,            KBC,        RSVD2,      RSVD3,        UARTA,       RSVD3,    0x32e0,  N,  N,  N),
	PINGROUP(kb_row10_ps2,           KBC,        RSVD2,      RSVD3,        UARTA,       RSVD3,    0x32e4,  N,  N,  N),
	PINGROUP(kb_col0_pq0,            KBC,        USB,        SPI2,         EMC_DLL,     KBC,      0x32fc,  N,  N,  N),
	PINGROUP(kb_col1_pq1,            KBC,        RSVD2,      SPI2,         EMC_DLL,     RSVD2,    0x3300,  N,  N,  N),
	PINGROUP(kb_col2_pq2,            KBC,        RSVD2,      SPI2,         RSVD4,       RSVD2,    0x3304,  N,  N,  N),
	PINGROUP(kb_col3_pq3,            KBC,        DISPLAYA,   PWM2,         UARTA,       KBC,      0x3308,  N,  N,  N),
	PINGROUP(kb_col4_pq4,            KBC,        OWR,        SDMMC3,       UARTA,       KBC,      0x330c,  N,  N,  N),
	PINGROUP(kb_col5_pq5,            KBC,        RSVD2,      SDMMC1,       RSVD4,       RSVD4,    0x3310,  N,  N,  N),
	PINGROUP(kb_col6_pq6,            KBC,        RSVD2,      SPI2,         RSVD4,       RSVD4,    0x3314,  N,  N,  N),
	PINGROUP(kb_col7_pq7,            KBC,        RSVD2,      SPI2,         RSVD4,       RSVD4,    0x3318,  N,  N,  N),
	PINGROUP(clk_32k_out_pa0,        BLINK,      SOC,        RSVD3,        RSVD4,       RSVD4,    0x331c,  N,  N,  N),
	PINGROUP(sys_clk_req_pz5,        SYSCLK,     RSVD2,      RSVD3,        RSVD4,       RSVD4,    0x3320,  N,  N,  N),
	PINGROUP(core_pwr_req,           PWRON,      RSVD2,      RSVD3,        RSVD4,       RSVD4,    0x3324,  N,  N,  N),
	PINGROUP(cpu_pwr_req,            CPU,        RSVD2,      RSVD3,        RSVD4,       RSVD4,    0x3328,  N,  N,  N),
	PINGROUP(pwr_int_n,              PMI,        RSVD2,      RSVD3,        RSVD4,       RSVD4,    0x332c,  N,  N,  N),
	PINGROUP(owr,                    OWR,        RSVD2,      RSVD3,        RSVD4,       RSVD4,    0x3334,  N,  N,  Y),
	PINGROUP(dap1_fs_pn0,            I2S0,       HDA,        GMI,          RSVD4,       RSVD4,    0x3338,  N,  N,  N),
	PINGROUP(dap1_din_pn1,           I2S0,       HDA,        GMI,          RSVD4,       RSVD4,    0x333c,  N,  N,  N),
	PINGROUP(dap1_dout_pn2,          I2S0,       HDA,        GMI,          RSVD4,       RSVD4,    0x3340,  N,  N,  N),
	PINGROUP(dap1_sclk_pn3,          I2S0,       HDA,        GMI,          RSVD4,       RSVD4,    0x3344,  N,  N,  N),
	PINGROUP(clk1_req_pee2,          DAP,        DAP1,       RSVD3,        RSVD4,       RSVD4,    0x3348,  N,  N,  N),
	PINGROUP(clk1_out_pw4,           EXTPERIPH1, DAP2,       RSVD3,        RSVD4,       RSVD4,    0x334c,  N,  N,  N),
	PINGROUP(spdif_in_pk6,           SPDIF,      USB,        RSVD3,        RSVD4,       RSVD4,    0x3350,  N,  N,  N),
	PINGROUP(spdif_out_pk5,          SPDIF,      RSVD2,      RSVD3,        RSVD4,       RSVD4,    0x3354,  N,  N,  N),
	PINGROUP(dap2_fs_pa2,            I2S1,       HDA,        RSVD3,        RSVD4,       RSVD4,    0x3358,  N,  N,  N),
	PINGROUP(dap2_din_pa4,           I2S1,       HDA,        RSVD3,        RSVD4,       RSVD4,    0x335c,  N,  N,  N),
	PINGROUP(dap2_dout_pa5,          I2S1,       HDA,        RSVD3,        RSVD4,       RSVD4,    0x3360,  N,  N,  N),
	PINGROUP(dap2_sclk_pa3,          I2S1,       HDA,        RSVD3,        RSVD4,       RSVD4,    0x3364,  N,  N,  N),
	PINGROUP(dvfs_pwm_px0,           SPI6,       CLDVFS,     RSVD3,        RSVD4,       RSVD4,    0x3368,  N,  N,  N),
	PINGROUP(gpio_x1_aud_px1,        SPI6,       RSVD2,      RSVD3,        RSVD4,       RSVD4,    0x336c,  N,  N,  N),
	PINGROUP(gpio_x3_aud_px3,        SPI6,       SPI1,       RSVD3,        RSVD4,       RSVD4,    0x3370,  N,  N,  N),
	PINGROUP(dvfs_clk_px2,           SPI6,       CLDVFS,     RSVD3,        RSVD4,       RSVD4,    0x3374,  N,  N,  N),
	PINGROUP(gpio_x4_aud_px4,        RSVD1,      SPI1,       SPI2,         DAP2,        RSVD1,    0x3378,  N,  N,  N),
	PINGROUP(gpio_x5_aud_px5,        RSVD1,      SPI1,       SPI2,         RSVD4,       RSVD1,    0x337c,  N,  N,  N),
	PINGROUP(gpio_x6_aud_px6,        SPI6,       SPI1,       SPI2,         RSVD4,       RSVD4,    0x3380,  N,  N,  N),
	PINGROUP(gpio_x7_aud_px7,        RSVD1,      SPI1,       SPI2,         RSVD4,       RSVD4,    0x3384,  N,  N,  N),
	PINGROUP(sdmmc3_clk_pa6,         SDMMC3,     RSVD2,      RSVD3,        SPI3,        RSVD3,    0x3390,  N,  N,  N),
	PINGROUP(sdmmc3_cmd_pa7,         SDMMC3,     PWM3,       UARTA,        SPI3,        SDMMC3,   0x3394,  N,  N,  N),
	PINGROUP(sdmmc3_dat0_pb7,        SDMMC3,     RSVD2,      RSVD3,        SPI3,        RSVD3,    0x3398,  N,  N,  N),
	PINGROUP(sdmmc3_dat1_pb6,        SDMMC3,     PWM2,       UARTA,        SPI3,        SDMMC3,   0x339c,  N,  N,  N),
	PINGROUP(sdmmc3_dat2_pb5,        SDMMC3,     PWM1,       DISPLAYA,     SPI3,        SDMMC3,   0x33a0,  N,  N,  N),
	PINGROUP(sdmmc3_dat3_pb4,        SDMMC3,     PWM0,       DISPLAYB,     SPI3,        SDMMC3,   0x33a4,  N,  N,  N),
	PINGROUP(hdmi_cec_pee3,          CEC,        SDMMC3,     RSVD3,        SOC,         RSVD3,    0x33e0,  Y,  N,  N),
	PINGROUP(sdmmc1_wp_n_pv3,        SDMMC1,     CLK12,      SPI4,         UARTA,       SDMMC1,   0x33e4,  N,  N,  N),
	PINGROUP(sdmmc3_cd_n_pv2,        SDMMC3,     OWR,        RSVD3,        RSVD4,       RSVD4,    0x33e8,  N,  N,  N),
	PINGROUP(gpio_w2_aud_pw2,        SPI6,       RSVD2,      SPI2,         I2C1,        RSVD2,    0x33ec,  N,  N,  N),
	PINGROUP(gpio_w3_aud_pw3,        SPI6,       SPI1,       SPI2,         I2C1,        SPI6,     0x33f0,  N,  N,  N),
	PINGROUP(usb_vbus_en0_pn4,       USB,        RSVD2,      RSVD3,        RSVD4,       RSVD4,    0x33f4,  Y,  N,  N),
	PINGROUP(usb_vbus_en1_pn5,       USB,        RSVD2,      RSVD3,        RSVD4,       RSVD4,    0x33f8,  Y,  N,  N),
	PINGROUP(sdmmc3_clk_lb_in_pee5,  SDMMC3,     RSVD2,      RSVD3,        RSVD4,       RSVD4,    0x33fc,  N,  N,  N),
	PINGROUP(sdmmc3_clk_lb_out_pee4, SDMMC3,     RSVD2,      RSVD3,        RSVD4,       RSVD4,    0x3400,  N,  N,  N),
	PINGROUP(reset_out_n,            RSVD1,      RSVD2,      RSVD3,        RESET_OUT_N, RSVD3,    0x3408,  N,  N,  N),

	/* pg_name, r, hsm_b, schmitt_b, lpmd_b, drvdn_b, drvdn_w, drvup_b, drvup_w, slwr_b, slwr_w, slwf_b, slwf_w, drvtype */
	DRV_PINGROUP(ao1,   0x868,  2,  3,  4,  12,  5,  20,  5,  28,  2,  30,  2,  N),
	DRV_PINGROUP(ao2,   0x86c,  2,  3,  4,  12,  5,  20,  5,  28,  2,  30,  2,  N),
	DRV_PINGROUP(at1,   0x870,  2,  3,  4,  12,  7,  20,  7,  28,  2,  30,  2,  Y),
	DRV_PINGROUP(at2,   0x874,  2,  3,  4,  12,  7,  20,  7,  28,  2,  30,  2,  Y),
	DRV_PINGROUP(at3,   0x878,  2,  3,  4,  12,  7,  20,  7,  28,  2,  30,  2,  Y),
	DRV_PINGROUP(at4,   0x87c,  2,  3,  4,  12,  7,  20,  7,  28,  2,  30,  2,  Y),
	DRV_PINGROUP(at5,   0x880,  2,  3,  4,  14,  5,  19,  5,  28,  2,  30,  2,  N),
	DRV_PINGROUP(cdev1, 0x884,  2,  3,  4,  12,  5,  20,  5,  28,  2,  30,  2,  N),
	DRV_PINGROUP(cdev2, 0x888,  2,  3,  4,  12,  5,  20,  5,  28,  2,  30,  2,  N),
	DRV_PINGROUP(dap1,  0x890,  2,  3,  4,  12,  5,  20,  5,  28,  2,  30,  2,  N),
	DRV_PINGROUP(dap2,  0x894,  2,  3,  4,  12,  5,  20,  5,  28,  2,  30,  2,  N),
	DRV_PINGROUP(dap3,  0x898,  2,  3,  4,  12,  5,  20,  5,  28,  2,  30,  2,  N),
	DRV_PINGROUP(dap4,  0x89c,  2,  3,  4,  12,  5,  20,  5,  28,  2,  30,  2,  N),
	DRV_PINGROUP(dbg,   0x8a0,  2,  3,  4,  12,  5,  20,  5,  28,  2,  30,  2,  N),
	DRV_PINGROUP(sdio3, 0x8b0,  2,  3, -1,  12,  7,  20,  7,  28,  2,  30,  2,  N),
	DRV_PINGROUP(spi,   0x8b4,  2,  3,  4,  12,  5,  20,  5,  28,  2,  30,  2,  N),
	DRV_PINGROUP(uaa,   0x8b8,  2,  3,  4,  12,  5,  20,  5,  28,  2,  30,  2,  N),
	DRV_PINGROUP(uab,   0x8bc,  2,  3,  4,  12,  5,  20,  5,  28,  2,  30,  2,  N),
	DRV_PINGROUP(uart2, 0x8c0,  2,  3,  4,  12,  5,  20,  5,  28,  2,  30,  2,  N),
	DRV_PINGROUP(uart3, 0x8c4,  2,  3,  4,  12,  5,  20,  5,  28,  2,  30,  2,  N),
	DRV_PINGROUP(sdio1, 0x8ec,  2,  3, -1,  12,  7,  20,  7,  28,  2,  30,  2,  N),
	DRV_PINGROUP(ddc,   0x8fc,  2,  3,  4,  12,  5,  20,  5,  28,  2,  30,  2,  N),
	DRV_PINGROUP(gma,   0x900,  2,  3,  4,  14,  5,  20,  5,  28,  2,  30,  2,  Y),
	DRV_PINGROUP(gme,   0x910,  2,  3,  4,  14,  5,  19,  5,  28,  2,  30,  2,  N),
	DRV_PINGROUP(gmf,   0x914,  2,  3,  4,  14,  5,  19,  5,  28,  2,  30,  2,  N),
	DRV_PINGROUP(gmg,   0x918,  2,  3,  4,  14,  5,  19,  5,  28,  2,  30,  2,  N),
	DRV_PINGROUP(gmh,   0x91c,  2,  3,  4,  14,  5,  19,  5,  28,  2,  30,  2,  N),
	DRV_PINGROUP(owr,   0x920,  2,  3,  4,  12,  5,  20,  5,  28,  2,  30,  2,  N),
	DRV_PINGROUP(uda,   0x924,  2,  3,  4,  12,  5,  20,  5,  28,  2,  30,  2,  N),
};

static const struct tegra_pinctrl_soc_data tegra114_pinctrl = {
	.ngpios = NUM_GPIOS,
	.pins = tegra114_pins,
	.npins = ARRAY_SIZE(tegra114_pins),
	.functions = tegra114_functions,
	.nfunctions = ARRAY_SIZE(tegra114_functions),
	.groups = tegra114_groups,
	.ngroups = ARRAY_SIZE(tegra114_groups),
};

static int tegra114_pinctrl_probe(struct platform_device *pdev)
{
	return tegra_pinctrl_probe(pdev, &tegra114_pinctrl);
}

static struct of_device_id tegra114_pinctrl_of_match[] = {
	{ .compatible = "nvidia,tegra114-pinmux", },
	{ },
};
MODULE_DEVICE_TABLE(of, tegra114_pinctrl_of_match);

static struct platform_driver tegra114_pinctrl_driver = {
	.driver = {
		.name = "tegra114-pinctrl",
		.owner = THIS_MODULE,
		.of_match_table = tegra114_pinctrl_of_match,
	},
	.probe = tegra114_pinctrl_probe,
	.remove = tegra_pinctrl_remove,
};
module_platform_driver(tegra114_pinctrl_driver);

MODULE_ALIAS("platform:tegra114-pinctrl");
MODULE_AUTHOR("Pritesh Raithatha <praithatha@nvidia.com>");
MODULE_DESCRIPTION("NVIDIA Tegra114 pincontrol driver");
MODULE_LICENSE("GPL v2");
