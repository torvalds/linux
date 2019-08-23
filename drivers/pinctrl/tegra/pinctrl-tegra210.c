// SPDX-License-Identifier: GPL-2.0-only
/*
 * Pinctrl data for the NVIDIA Tegra210 pinmux
 *
 * Copyright (c) 2015, NVIDIA CORPORATION.  All rights reserved.
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

#define TEGRA_PIN_PEX_L0_RST_N_PA0		_GPIO(0)
#define TEGRA_PIN_PEX_L0_CLKREQ_N_PA1		_GPIO(1)
#define TEGRA_PIN_PEX_WAKE_N_PA2		_GPIO(2)
#define TEGRA_PIN_PEX_L1_RST_N_PA3		_GPIO(3)
#define TEGRA_PIN_PEX_L1_CLKREQ_N_PA4		_GPIO(4)
#define TEGRA_PIN_SATA_LED_ACTIVE_PA5		_GPIO(5)
#define TEGRA_PIN_PA6				_GPIO(6)
#define TEGRA_PIN_DAP1_FS_PB0			_GPIO(8)
#define TEGRA_PIN_DAP1_DIN_PB1			_GPIO(9)
#define TEGRA_PIN_DAP1_DOUT_PB2			_GPIO(10)
#define TEGRA_PIN_DAP1_SCLK_PB3			_GPIO(11)
#define TEGRA_PIN_SPI2_MOSI_PB4			_GPIO(12)
#define TEGRA_PIN_SPI2_MISO_PB5			_GPIO(13)
#define TEGRA_PIN_SPI2_SCK_PB6			_GPIO(14)
#define TEGRA_PIN_SPI2_CS0_PB7			_GPIO(15)
#define TEGRA_PIN_SPI1_MOSI_PC0			_GPIO(16)
#define TEGRA_PIN_SPI1_MISO_PC1			_GPIO(17)
#define TEGRA_PIN_SPI1_SCK_PC2			_GPIO(18)
#define TEGRA_PIN_SPI1_CS0_PC3			_GPIO(19)
#define TEGRA_PIN_SPI1_CS1_PC4			_GPIO(20)
#define TEGRA_PIN_SPI4_SCK_PC5			_GPIO(21)
#define TEGRA_PIN_SPI4_CS0_PC6			_GPIO(22)
#define TEGRA_PIN_SPI4_MOSI_PC7			_GPIO(23)
#define TEGRA_PIN_SPI4_MISO_PD0			_GPIO(24)
#define TEGRA_PIN_UART3_TX_PD1			_GPIO(25)
#define TEGRA_PIN_UART3_RX_PD2			_GPIO(26)
#define TEGRA_PIN_UART3_RTS_PD3			_GPIO(27)
#define TEGRA_PIN_UART3_CTS_PD4			_GPIO(28)
#define TEGRA_PIN_DMIC1_CLK_PE0			_GPIO(32)
#define TEGRA_PIN_DMIC1_DAT_PE1			_GPIO(33)
#define TEGRA_PIN_DMIC2_CLK_PE2			_GPIO(34)
#define TEGRA_PIN_DMIC2_DAT_PE3			_GPIO(35)
#define TEGRA_PIN_DMIC3_CLK_PE4			_GPIO(36)
#define TEGRA_PIN_DMIC3_DAT_PE5			_GPIO(37)
#define TEGRA_PIN_PE6				_GPIO(38)
#define TEGRA_PIN_PE7				_GPIO(39)
#define TEGRA_PIN_GEN3_I2C_SCL_PF0		_GPIO(40)
#define TEGRA_PIN_GEN3_I2C_SDA_PF1		_GPIO(41)
#define TEGRA_PIN_UART2_TX_PG0			_GPIO(48)
#define TEGRA_PIN_UART2_RX_PG1			_GPIO(49)
#define TEGRA_PIN_UART2_RTS_PG2			_GPIO(50)
#define TEGRA_PIN_UART2_CTS_PG3			_GPIO(51)
#define TEGRA_PIN_WIFI_EN_PH0			_GPIO(56)
#define TEGRA_PIN_WIFI_RST_PH1			_GPIO(57)
#define TEGRA_PIN_WIFI_WAKE_AP_PH2		_GPIO(58)
#define TEGRA_PIN_AP_WAKE_BT_PH3		_GPIO(59)
#define TEGRA_PIN_BT_RST_PH4			_GPIO(60)
#define TEGRA_PIN_BT_WAKE_AP_PH5		_GPIO(61)
#define TEGRA_PIN_PH6				_GPIO(62)
#define TEGRA_PIN_AP_WAKE_NFC_PH7		_GPIO(63)
#define TEGRA_PIN_NFC_EN_PI0			_GPIO(64)
#define TEGRA_PIN_NFC_INT_PI1			_GPIO(65)
#define TEGRA_PIN_GPS_EN_PI2			_GPIO(66)
#define TEGRA_PIN_GPS_RST_PI3			_GPIO(67)
#define TEGRA_PIN_UART4_TX_PI4			_GPIO(68)
#define TEGRA_PIN_UART4_RX_PI5			_GPIO(69)
#define TEGRA_PIN_UART4_RTS_PI6			_GPIO(70)
#define TEGRA_PIN_UART4_CTS_PI7			_GPIO(71)
#define TEGRA_PIN_GEN1_I2C_SDA_PJ0		_GPIO(72)
#define TEGRA_PIN_GEN1_I2C_SCL_PJ1		_GPIO(73)
#define TEGRA_PIN_GEN2_I2C_SCL_PJ2		_GPIO(74)
#define TEGRA_PIN_GEN2_I2C_SDA_PJ3		_GPIO(75)
#define TEGRA_PIN_DAP4_FS_PJ4			_GPIO(76)
#define TEGRA_PIN_DAP4_DIN_PJ5			_GPIO(77)
#define TEGRA_PIN_DAP4_DOUT_PJ6			_GPIO(78)
#define TEGRA_PIN_DAP4_SCLK_PJ7			_GPIO(79)
#define TEGRA_PIN_PK0				_GPIO(80)
#define TEGRA_PIN_PK1				_GPIO(81)
#define TEGRA_PIN_PK2				_GPIO(82)
#define TEGRA_PIN_PK3				_GPIO(83)
#define TEGRA_PIN_PK4				_GPIO(84)
#define TEGRA_PIN_PK5				_GPIO(85)
#define TEGRA_PIN_PK6				_GPIO(86)
#define TEGRA_PIN_PK7				_GPIO(87)
#define TEGRA_PIN_PL0				_GPIO(88)
#define TEGRA_PIN_PL1				_GPIO(89)
#define TEGRA_PIN_SDMMC1_CLK_PM0		_GPIO(96)
#define TEGRA_PIN_SDMMC1_CMD_PM1		_GPIO(97)
#define TEGRA_PIN_SDMMC1_DAT3_PM2		_GPIO(98)
#define TEGRA_PIN_SDMMC1_DAT2_PM3		_GPIO(99)
#define TEGRA_PIN_SDMMC1_DAT1_PM4		_GPIO(100)
#define TEGRA_PIN_SDMMC1_DAT0_PM5		_GPIO(101)
#define TEGRA_PIN_SDMMC3_CLK_PP0		_GPIO(120)
#define TEGRA_PIN_SDMMC3_CMD_PP1		_GPIO(121)
#define TEGRA_PIN_SDMMC3_DAT3_PP2		_GPIO(122)
#define TEGRA_PIN_SDMMC3_DAT2_PP3		_GPIO(123)
#define TEGRA_PIN_SDMMC3_DAT1_PP4		_GPIO(124)
#define TEGRA_PIN_SDMMC3_DAT0_PP5		_GPIO(125)
#define TEGRA_PIN_CAM1_MCLK_PS0			_GPIO(144)
#define TEGRA_PIN_CAM2_MCLK_PS1			_GPIO(145)
#define TEGRA_PIN_CAM_I2C_SCL_PS2		_GPIO(146)
#define TEGRA_PIN_CAM_I2C_SDA_PS3		_GPIO(147)
#define TEGRA_PIN_CAM_RST_PS4			_GPIO(148)
#define TEGRA_PIN_CAM_AF_EN_PS5			_GPIO(149)
#define TEGRA_PIN_CAM_FLASH_EN_PS6		_GPIO(150)
#define TEGRA_PIN_CAM1_PWDN_PS7			_GPIO(151)
#define TEGRA_PIN_CAM2_PWDN_PT0			_GPIO(152)
#define TEGRA_PIN_CAM1_STROBE_PT1		_GPIO(153)
#define TEGRA_PIN_UART1_TX_PU0			_GPIO(160)
#define TEGRA_PIN_UART1_RX_PU1			_GPIO(161)
#define TEGRA_PIN_UART1_RTS_PU2			_GPIO(162)
#define TEGRA_PIN_UART1_CTS_PU3			_GPIO(163)
#define TEGRA_PIN_LCD_BL_PWM_PV0		_GPIO(168)
#define TEGRA_PIN_LCD_BL_EN_PV1			_GPIO(169)
#define TEGRA_PIN_LCD_RST_PV2			_GPIO(170)
#define TEGRA_PIN_LCD_GPIO1_PV3			_GPIO(171)
#define TEGRA_PIN_LCD_GPIO2_PV4			_GPIO(172)
#define TEGRA_PIN_AP_READY_PV5			_GPIO(173)
#define TEGRA_PIN_TOUCH_RST_PV6			_GPIO(174)
#define TEGRA_PIN_TOUCH_CLK_PV7			_GPIO(175)
#define TEGRA_PIN_MODEM_WAKE_AP_PX0		_GPIO(184)
#define TEGRA_PIN_TOUCH_INT_PX1			_GPIO(185)
#define TEGRA_PIN_MOTION_INT_PX2		_GPIO(186)
#define TEGRA_PIN_ALS_PROX_INT_PX3		_GPIO(187)
#define TEGRA_PIN_TEMP_ALERT_PX4		_GPIO(188)
#define TEGRA_PIN_BUTTON_POWER_ON_PX5		_GPIO(189)
#define TEGRA_PIN_BUTTON_VOL_UP_PX6		_GPIO(190)
#define TEGRA_PIN_BUTTON_VOL_DOWN_PX7		_GPIO(191)
#define TEGRA_PIN_BUTTON_SLIDE_SW_PY0		_GPIO(192)
#define TEGRA_PIN_BUTTON_HOME_PY1		_GPIO(193)
#define TEGRA_PIN_LCD_TE_PY2			_GPIO(194)
#define TEGRA_PIN_PWR_I2C_SCL_PY3		_GPIO(195)
#define TEGRA_PIN_PWR_I2C_SDA_PY4		_GPIO(196)
#define TEGRA_PIN_CLK_32K_OUT_PY5		_GPIO(197)
#define TEGRA_PIN_PZ0				_GPIO(200)
#define TEGRA_PIN_PZ1				_GPIO(201)
#define TEGRA_PIN_PZ2				_GPIO(202)
#define TEGRA_PIN_PZ3				_GPIO(203)
#define TEGRA_PIN_PZ4				_GPIO(204)
#define TEGRA_PIN_PZ5				_GPIO(205)
#define TEGRA_PIN_DAP2_FS_PAA0			_GPIO(208)
#define TEGRA_PIN_DAP2_SCLK_PAA1		_GPIO(209)
#define TEGRA_PIN_DAP2_DIN_PAA2			_GPIO(210)
#define TEGRA_PIN_DAP2_DOUT_PAA3		_GPIO(211)
#define TEGRA_PIN_AUD_MCLK_PBB0			_GPIO(216)
#define TEGRA_PIN_DVFS_PWM_PBB1			_GPIO(217)
#define TEGRA_PIN_DVFS_CLK_PBB2			_GPIO(218)
#define TEGRA_PIN_GPIO_X1_AUD_PBB3		_GPIO(219)
#define TEGRA_PIN_GPIO_X3_AUD_PBB4		_GPIO(220)
#define TEGRA_PIN_HDMI_CEC_PCC0			_GPIO(224)
#define TEGRA_PIN_HDMI_INT_DP_HPD_PCC1		_GPIO(225)
#define TEGRA_PIN_SPDIF_OUT_PCC2		_GPIO(226)
#define TEGRA_PIN_SPDIF_IN_PCC3			_GPIO(227)
#define TEGRA_PIN_USB_VBUS_EN0_PCC4		_GPIO(228)
#define TEGRA_PIN_USB_VBUS_EN1_PCC5		_GPIO(229)
#define TEGRA_PIN_DP_HPD0_PCC6			_GPIO(230)
#define TEGRA_PIN_PCC7				_GPIO(231)
#define TEGRA_PIN_SPI2_CS1_PDD0			_GPIO(232)
#define TEGRA_PIN_QSPI_SCK_PEE0			_GPIO(240)
#define TEGRA_PIN_QSPI_CS_N_PEE1		_GPIO(241)
#define TEGRA_PIN_QSPI_IO0_PEE2			_GPIO(242)
#define TEGRA_PIN_QSPI_IO1_PEE3			_GPIO(243)
#define TEGRA_PIN_QSPI_IO2_PEE4			_GPIO(244)
#define TEGRA_PIN_QSPI_IO3_PEE5			_GPIO(245)

/* All non-GPIO pins follow */
#define NUM_GPIOS				(TEGRA_PIN_QSPI_IO3_PEE5 + 1)
#define _PIN(offset)				(NUM_GPIOS + (offset))

/* Non-GPIO pins */
#define TEGRA_PIN_CORE_PWR_REQ			_PIN(0)
#define TEGRA_PIN_CPU_PWR_REQ			_PIN(1)
#define TEGRA_PIN_PWR_INT_N			_PIN(2)
#define TEGRA_PIN_CLK_32K_IN			_PIN(3)
#define TEGRA_PIN_JTAG_RTCK			_PIN(4)
#define TEGRA_PIN_BATT_BCL			_PIN(5)
#define TEGRA_PIN_CLK_REQ			_PIN(6)
#define TEGRA_PIN_SHUTDOWN			_PIN(7)

static const struct pinctrl_pin_desc tegra210_pins[] = {
	PINCTRL_PIN(TEGRA_PIN_PEX_L0_RST_N_PA0, "PEX_L0_RST_N PA0"),
	PINCTRL_PIN(TEGRA_PIN_PEX_L0_CLKREQ_N_PA1, "PEX_L0_CLKREQ_N PA1"),
	PINCTRL_PIN(TEGRA_PIN_PEX_WAKE_N_PA2, "PEX_WAKE_N PA2"),
	PINCTRL_PIN(TEGRA_PIN_PEX_L1_RST_N_PA3, "PEX_L1_RST_N PA3"),
	PINCTRL_PIN(TEGRA_PIN_PEX_L1_CLKREQ_N_PA4, "PEX_L1_CLKREQ_N PA4"),
	PINCTRL_PIN(TEGRA_PIN_SATA_LED_ACTIVE_PA5, "SATA_LED_ACTIVE PA5"),
	PINCTRL_PIN(TEGRA_PIN_PA6, "PA6"),
	PINCTRL_PIN(TEGRA_PIN_DAP1_FS_PB0, "DAP1_FS PB0"),
	PINCTRL_PIN(TEGRA_PIN_DAP1_DIN_PB1, "DAP1_DIN PB1"),
	PINCTRL_PIN(TEGRA_PIN_DAP1_DOUT_PB2, "DAP1_DOUT PB2"),
	PINCTRL_PIN(TEGRA_PIN_DAP1_SCLK_PB3, "DAP1_SCLK PB3"),
	PINCTRL_PIN(TEGRA_PIN_SPI2_MOSI_PB4, "SPI2_MOSI PB4"),
	PINCTRL_PIN(TEGRA_PIN_SPI2_MISO_PB5, "SPI2_MISO PB5"),
	PINCTRL_PIN(TEGRA_PIN_SPI2_SCK_PB6, "SPI2_SCK PB6"),
	PINCTRL_PIN(TEGRA_PIN_SPI2_CS0_PB7, "SPI2_CS0 PB7"),
	PINCTRL_PIN(TEGRA_PIN_SPI1_MOSI_PC0, "SPI1_MOSI PC0"),
	PINCTRL_PIN(TEGRA_PIN_SPI1_MISO_PC1, "SPI1_MISO PC1"),
	PINCTRL_PIN(TEGRA_PIN_SPI1_SCK_PC2, "SPI1_SCK PC2"),
	PINCTRL_PIN(TEGRA_PIN_SPI1_CS0_PC3, "SPI1_CS0 PC3"),
	PINCTRL_PIN(TEGRA_PIN_SPI1_CS1_PC4, "SPI1_CS1 PC4"),
	PINCTRL_PIN(TEGRA_PIN_SPI4_SCK_PC5, "SPI4_SCK PC5"),
	PINCTRL_PIN(TEGRA_PIN_SPI4_CS0_PC6, "SPI4_CS0 PC6"),
	PINCTRL_PIN(TEGRA_PIN_SPI4_MOSI_PC7, "SPI4_MOSI PC7"),
	PINCTRL_PIN(TEGRA_PIN_SPI4_MISO_PD0, "SPI4_MISO PD0"),
	PINCTRL_PIN(TEGRA_PIN_UART3_TX_PD1, "UART3_TX PD1"),
	PINCTRL_PIN(TEGRA_PIN_UART3_RX_PD2, "UART3_RX PD2"),
	PINCTRL_PIN(TEGRA_PIN_UART3_RTS_PD3, "UART3_RTS PD3"),
	PINCTRL_PIN(TEGRA_PIN_UART3_CTS_PD4, "UART3_CTS PD4"),
	PINCTRL_PIN(TEGRA_PIN_DMIC1_CLK_PE0, "DMIC1_CLK PE0"),
	PINCTRL_PIN(TEGRA_PIN_DMIC1_DAT_PE1, "DMIC1_DAT PE1"),
	PINCTRL_PIN(TEGRA_PIN_DMIC2_CLK_PE2, "DMIC2_CLK PE2"),
	PINCTRL_PIN(TEGRA_PIN_DMIC2_DAT_PE3, "DMIC2_DAT PE3"),
	PINCTRL_PIN(TEGRA_PIN_DMIC3_CLK_PE4, "DMIC3_CLK PE4"),
	PINCTRL_PIN(TEGRA_PIN_DMIC3_DAT_PE5, "DMIC3_DAT PE5"),
	PINCTRL_PIN(TEGRA_PIN_PE6, "PE6"),
	PINCTRL_PIN(TEGRA_PIN_PE7, "PE7"),
	PINCTRL_PIN(TEGRA_PIN_GEN3_I2C_SCL_PF0, "GEN3_I2C_SCL PF0"),
	PINCTRL_PIN(TEGRA_PIN_GEN3_I2C_SDA_PF1, "GEN3_I2C_SDA PF1"),
	PINCTRL_PIN(TEGRA_PIN_UART2_TX_PG0, "UART2_TX PG0"),
	PINCTRL_PIN(TEGRA_PIN_UART2_RX_PG1, "UART2_RX PG1"),
	PINCTRL_PIN(TEGRA_PIN_UART2_RTS_PG2, "UART2_RTS PG2"),
	PINCTRL_PIN(TEGRA_PIN_UART2_CTS_PG3, "UART2_CTS PG3"),
	PINCTRL_PIN(TEGRA_PIN_WIFI_EN_PH0, "WIFI_EN PH0"),
	PINCTRL_PIN(TEGRA_PIN_WIFI_RST_PH1, "WIFI_RST PH1"),
	PINCTRL_PIN(TEGRA_PIN_WIFI_WAKE_AP_PH2, "WIFI_WAKE_AP PH2"),
	PINCTRL_PIN(TEGRA_PIN_AP_WAKE_BT_PH3, "AP_WAKE_BT PH3"),
	PINCTRL_PIN(TEGRA_PIN_BT_RST_PH4, "BT_RST PH4"),
	PINCTRL_PIN(TEGRA_PIN_BT_WAKE_AP_PH5, "BT_WAKE_AP PH5"),
	PINCTRL_PIN(TEGRA_PIN_PH6, "PH6"),
	PINCTRL_PIN(TEGRA_PIN_AP_WAKE_NFC_PH7, "AP_WAKE_NFC PH7"),
	PINCTRL_PIN(TEGRA_PIN_NFC_EN_PI0, "NFC_EN PI0"),
	PINCTRL_PIN(TEGRA_PIN_NFC_INT_PI1, "NFC_INT PI1"),
	PINCTRL_PIN(TEGRA_PIN_GPS_EN_PI2, "GPS_EN PI2"),
	PINCTRL_PIN(TEGRA_PIN_GPS_RST_PI3, "GPS_RST PI3"),
	PINCTRL_PIN(TEGRA_PIN_UART4_TX_PI4, "UART4_TX PI4"),
	PINCTRL_PIN(TEGRA_PIN_UART4_RX_PI5, "UART4_RX PI5"),
	PINCTRL_PIN(TEGRA_PIN_UART4_RTS_PI6, "UART4_RTS PI6"),
	PINCTRL_PIN(TEGRA_PIN_UART4_CTS_PI7, "UART4_CTS PI7"),
	PINCTRL_PIN(TEGRA_PIN_GEN1_I2C_SDA_PJ0, "GEN1_I2C_SDA PJ0"),
	PINCTRL_PIN(TEGRA_PIN_GEN1_I2C_SCL_PJ1, "GEN1_I2C_SCL PJ1"),
	PINCTRL_PIN(TEGRA_PIN_GEN2_I2C_SCL_PJ2, "GEN2_I2C_SCL PJ2"),
	PINCTRL_PIN(TEGRA_PIN_GEN2_I2C_SDA_PJ3, "GEN2_I2C_SDA PJ3"),
	PINCTRL_PIN(TEGRA_PIN_DAP4_FS_PJ4, "DAP4_FS PJ4"),
	PINCTRL_PIN(TEGRA_PIN_DAP4_DIN_PJ5, "DAP4_DIN PJ5"),
	PINCTRL_PIN(TEGRA_PIN_DAP4_DOUT_PJ6, "DAP4_DOUT PJ6"),
	PINCTRL_PIN(TEGRA_PIN_DAP4_SCLK_PJ7, "DAP4_SCLK PJ7"),
	PINCTRL_PIN(TEGRA_PIN_PK0, "PK0"),
	PINCTRL_PIN(TEGRA_PIN_PK1, "PK1"),
	PINCTRL_PIN(TEGRA_PIN_PK2, "PK2"),
	PINCTRL_PIN(TEGRA_PIN_PK3, "PK3"),
	PINCTRL_PIN(TEGRA_PIN_PK4, "PK4"),
	PINCTRL_PIN(TEGRA_PIN_PK5, "PK5"),
	PINCTRL_PIN(TEGRA_PIN_PK6, "PK6"),
	PINCTRL_PIN(TEGRA_PIN_PK7, "PK7"),
	PINCTRL_PIN(TEGRA_PIN_PL0, "PL0"),
	PINCTRL_PIN(TEGRA_PIN_PL1, "PL1"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC1_CLK_PM0, "SDMMC1_CLK PM0"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC1_CMD_PM1, "SDMMC1_CMD PM1"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC1_DAT3_PM2, "SDMMC1_DAT3 PM2"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC1_DAT2_PM3, "SDMMC1_DAT2 PM3"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC1_DAT1_PM4, "SDMMC1_DAT1 PM4"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC1_DAT0_PM5, "SDMMC1_DAT0 PM5"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC3_CLK_PP0, "SDMMC3_CLK PP0"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC3_CMD_PP1, "SDMMC3_CMD PP1"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC3_DAT3_PP2, "SDMMC3_DAT3 PP2"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC3_DAT2_PP3, "SDMMC3_DAT2 PP3"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC3_DAT1_PP4, "SDMMC3_DAT1 PP4"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC3_DAT0_PP5, "SDMMC3_DAT0 PP5"),
	PINCTRL_PIN(TEGRA_PIN_CAM1_MCLK_PS0, "CAM1_MCLK PS0"),
	PINCTRL_PIN(TEGRA_PIN_CAM2_MCLK_PS1, "CAM2_MCLK PS1"),
	PINCTRL_PIN(TEGRA_PIN_CAM_I2C_SCL_PS2, "CAM_I2C_SCL PS2"),
	PINCTRL_PIN(TEGRA_PIN_CAM_I2C_SDA_PS3, "CAM_I2C_SDA PS3"),
	PINCTRL_PIN(TEGRA_PIN_CAM_RST_PS4, "CAM_RST PS4"),
	PINCTRL_PIN(TEGRA_PIN_CAM_AF_EN_PS5, "CAM_AF_EN PS5"),
	PINCTRL_PIN(TEGRA_PIN_CAM_FLASH_EN_PS6, "CAM_FLASH_EN PS6"),
	PINCTRL_PIN(TEGRA_PIN_CAM1_PWDN_PS7, "CAM1_PWDN PS7"),
	PINCTRL_PIN(TEGRA_PIN_CAM2_PWDN_PT0, "CAM2_PWDN PT0"),
	PINCTRL_PIN(TEGRA_PIN_CAM1_STROBE_PT1, "CAM1_STROBE PT1"),
	PINCTRL_PIN(TEGRA_PIN_UART1_TX_PU0, "UART1_TX PU0"),
	PINCTRL_PIN(TEGRA_PIN_UART1_RX_PU1, "UART1_RX PU1"),
	PINCTRL_PIN(TEGRA_PIN_UART1_RTS_PU2, "UART1_RTS PU2"),
	PINCTRL_PIN(TEGRA_PIN_UART1_CTS_PU3, "UART1_CTS PU3"),
	PINCTRL_PIN(TEGRA_PIN_LCD_BL_PWM_PV0, "LCD_BL_PWM PV0"),
	PINCTRL_PIN(TEGRA_PIN_LCD_BL_EN_PV1, "LCD_BL_EN PV1"),
	PINCTRL_PIN(TEGRA_PIN_LCD_RST_PV2, "LCD_RST PV2"),
	PINCTRL_PIN(TEGRA_PIN_LCD_GPIO1_PV3, "LCD_GPIO1 PV3"),
	PINCTRL_PIN(TEGRA_PIN_LCD_GPIO2_PV4, "LCD_GPIO2 PV4"),
	PINCTRL_PIN(TEGRA_PIN_AP_READY_PV5, "AP_READY PV5"),
	PINCTRL_PIN(TEGRA_PIN_TOUCH_RST_PV6, "TOUCH_RST PV6"),
	PINCTRL_PIN(TEGRA_PIN_TOUCH_CLK_PV7, "TOUCH_CLK PV7"),
	PINCTRL_PIN(TEGRA_PIN_MODEM_WAKE_AP_PX0, "MODEM_WAKE_AP PX0"),
	PINCTRL_PIN(TEGRA_PIN_TOUCH_INT_PX1, "TOUCH_INT PX1"),
	PINCTRL_PIN(TEGRA_PIN_MOTION_INT_PX2, "MOTION_INT PX2"),
	PINCTRL_PIN(TEGRA_PIN_ALS_PROX_INT_PX3, "ALS_PROX_INT PX3"),
	PINCTRL_PIN(TEGRA_PIN_TEMP_ALERT_PX4, "TEMP_ALERT PX4"),
	PINCTRL_PIN(TEGRA_PIN_BUTTON_POWER_ON_PX5, "BUTTON_POWER_ON PX5"),
	PINCTRL_PIN(TEGRA_PIN_BUTTON_VOL_UP_PX6, "BUTTON_VOL_UP PX6"),
	PINCTRL_PIN(TEGRA_PIN_BUTTON_VOL_DOWN_PX7, "BUTTON_VOL_DOWN PX7"),
	PINCTRL_PIN(TEGRA_PIN_BUTTON_SLIDE_SW_PY0, "BUTTON_SLIDE_SW PY0"),
	PINCTRL_PIN(TEGRA_PIN_BUTTON_HOME_PY1, "BUTTON_HOME PY1"),
	PINCTRL_PIN(TEGRA_PIN_LCD_TE_PY2, "LCD_TE PY2"),
	PINCTRL_PIN(TEGRA_PIN_PWR_I2C_SCL_PY3, "PWR_I2C_SCL PY3"),
	PINCTRL_PIN(TEGRA_PIN_PWR_I2C_SDA_PY4, "PWR_I2C_SDA PY4"),
	PINCTRL_PIN(TEGRA_PIN_CLK_32K_OUT_PY5, "CLK_32K_OUT PY5"),
	PINCTRL_PIN(TEGRA_PIN_PZ0, "PZ0"),
	PINCTRL_PIN(TEGRA_PIN_PZ1, "PZ1"),
	PINCTRL_PIN(TEGRA_PIN_PZ2, "PZ2"),
	PINCTRL_PIN(TEGRA_PIN_PZ3, "PZ3"),
	PINCTRL_PIN(TEGRA_PIN_PZ4, "PZ4"),
	PINCTRL_PIN(TEGRA_PIN_PZ5, "PZ5"),
	PINCTRL_PIN(TEGRA_PIN_DAP2_FS_PAA0, "DAP2_FS PAA0"),
	PINCTRL_PIN(TEGRA_PIN_DAP2_SCLK_PAA1, "DAP2_SCLK PAA1"),
	PINCTRL_PIN(TEGRA_PIN_DAP2_DIN_PAA2, "DAP2_DIN PAA2"),
	PINCTRL_PIN(TEGRA_PIN_DAP2_DOUT_PAA3, "DAP2_DOUT PAA3"),
	PINCTRL_PIN(TEGRA_PIN_AUD_MCLK_PBB0, "AUD_MCLK PBB0"),
	PINCTRL_PIN(TEGRA_PIN_DVFS_PWM_PBB1, "DVFS_PWM PBB1"),
	PINCTRL_PIN(TEGRA_PIN_DVFS_CLK_PBB2, "DVFS_CLK PBB2"),
	PINCTRL_PIN(TEGRA_PIN_GPIO_X1_AUD_PBB3, "GPIO_X1_AUD PBB3"),
	PINCTRL_PIN(TEGRA_PIN_GPIO_X3_AUD_PBB4, "GPIO_X3_AUD PBB4"),
	PINCTRL_PIN(TEGRA_PIN_HDMI_CEC_PCC0, "HDMI_CEC PCC0"),
	PINCTRL_PIN(TEGRA_PIN_HDMI_INT_DP_HPD_PCC1, "HDMI_INT_DP_HPD PCC1"),
	PINCTRL_PIN(TEGRA_PIN_SPDIF_OUT_PCC2, "SPDIF_OUT PCC2"),
	PINCTRL_PIN(TEGRA_PIN_SPDIF_IN_PCC3, "SPDIF_IN PCC3"),
	PINCTRL_PIN(TEGRA_PIN_USB_VBUS_EN0_PCC4, "USB_VBUS_EN0 PCC4"),
	PINCTRL_PIN(TEGRA_PIN_USB_VBUS_EN1_PCC5, "USB_VBUS_EN1 PCC5"),
	PINCTRL_PIN(TEGRA_PIN_DP_HPD0_PCC6, "DP_HPD0 PCC6"),
	PINCTRL_PIN(TEGRA_PIN_PCC7, "PCC7"),
	PINCTRL_PIN(TEGRA_PIN_SPI2_CS1_PDD0, "SPI2_CS1 PDD0"),
	PINCTRL_PIN(TEGRA_PIN_QSPI_SCK_PEE0, "QSPI_SCK PEE0"),
	PINCTRL_PIN(TEGRA_PIN_QSPI_CS_N_PEE1, "QSPI_CS_N PEE1"),
	PINCTRL_PIN(TEGRA_PIN_QSPI_IO0_PEE2, "QSPI_IO0 PEE2"),
	PINCTRL_PIN(TEGRA_PIN_QSPI_IO1_PEE3, "QSPI_IO1 PEE3"),
	PINCTRL_PIN(TEGRA_PIN_QSPI_IO2_PEE4, "QSPI_IO2 PEE4"),
	PINCTRL_PIN(TEGRA_PIN_QSPI_IO3_PEE5, "QSPI_IO3 PEE5"),
	PINCTRL_PIN(TEGRA_PIN_CORE_PWR_REQ, "CORE_PWR_REQ"),
	PINCTRL_PIN(TEGRA_PIN_CPU_PWR_REQ, "CPU_PWR_REQ"),
	PINCTRL_PIN(TEGRA_PIN_PWR_INT_N, "PWR_INT_N"),
	PINCTRL_PIN(TEGRA_PIN_CLK_32K_IN, "CLK_32K_IN"),
	PINCTRL_PIN(TEGRA_PIN_JTAG_RTCK, "JTAG_RTCK"),
	PINCTRL_PIN(TEGRA_PIN_BATT_BCL, "BATT_BCL"),
	PINCTRL_PIN(TEGRA_PIN_CLK_REQ, "CLK_REQ"),
	PINCTRL_PIN(TEGRA_PIN_SHUTDOWN, "SHUTDOWN"),
};

static const unsigned pex_l0_rst_n_pa0_pins[] = {
	TEGRA_PIN_PEX_L0_RST_N_PA0,
};

static const unsigned pex_l0_clkreq_n_pa1_pins[] = {
	TEGRA_PIN_PEX_L0_CLKREQ_N_PA1,
};

static const unsigned pex_wake_n_pa2_pins[] = {
	TEGRA_PIN_PEX_WAKE_N_PA2,
};

static const unsigned pex_l1_rst_n_pa3_pins[] = {
	TEGRA_PIN_PEX_L1_RST_N_PA3,
};

static const unsigned pex_l1_clkreq_n_pa4_pins[] = {
	TEGRA_PIN_PEX_L1_CLKREQ_N_PA4,
};

static const unsigned sata_led_active_pa5_pins[] = {
	TEGRA_PIN_SATA_LED_ACTIVE_PA5,
};

static const unsigned pa6_pins[] = {
	TEGRA_PIN_PA6,
};

static const unsigned dap1_fs_pb0_pins[] = {
	TEGRA_PIN_DAP1_FS_PB0,
};

static const unsigned dap1_din_pb1_pins[] = {
	TEGRA_PIN_DAP1_DIN_PB1,
};

static const unsigned dap1_dout_pb2_pins[] = {
	TEGRA_PIN_DAP1_DOUT_PB2,
};

static const unsigned dap1_sclk_pb3_pins[] = {
	TEGRA_PIN_DAP1_SCLK_PB3,
};

static const unsigned spi2_mosi_pb4_pins[] = {
	TEGRA_PIN_SPI2_MOSI_PB4,
};

static const unsigned spi2_miso_pb5_pins[] = {
	TEGRA_PIN_SPI2_MISO_PB5,
};

static const unsigned spi2_sck_pb6_pins[] = {
	TEGRA_PIN_SPI2_SCK_PB6,
};

static const unsigned spi2_cs0_pb7_pins[] = {
	TEGRA_PIN_SPI2_CS0_PB7,
};

static const unsigned spi1_mosi_pc0_pins[] = {
	TEGRA_PIN_SPI1_MOSI_PC0,
};

static const unsigned spi1_miso_pc1_pins[] = {
	TEGRA_PIN_SPI1_MISO_PC1,
};

static const unsigned spi1_sck_pc2_pins[] = {
	TEGRA_PIN_SPI1_SCK_PC2,
};

static const unsigned spi1_cs0_pc3_pins[] = {
	TEGRA_PIN_SPI1_CS0_PC3,
};

static const unsigned spi1_cs1_pc4_pins[] = {
	TEGRA_PIN_SPI1_CS1_PC4,
};

static const unsigned spi4_sck_pc5_pins[] = {
	TEGRA_PIN_SPI4_SCK_PC5,
};

static const unsigned spi4_cs0_pc6_pins[] = {
	TEGRA_PIN_SPI4_CS0_PC6,
};

static const unsigned spi4_mosi_pc7_pins[] = {
	TEGRA_PIN_SPI4_MOSI_PC7,
};

static const unsigned spi4_miso_pd0_pins[] = {
	TEGRA_PIN_SPI4_MISO_PD0,
};

static const unsigned uart3_tx_pd1_pins[] = {
	TEGRA_PIN_UART3_TX_PD1,
};

static const unsigned uart3_rx_pd2_pins[] = {
	TEGRA_PIN_UART3_RX_PD2,
};

static const unsigned uart3_rts_pd3_pins[] = {
	TEGRA_PIN_UART3_RTS_PD3,
};

static const unsigned uart3_cts_pd4_pins[] = {
	TEGRA_PIN_UART3_CTS_PD4,
};

static const unsigned dmic1_clk_pe0_pins[] = {
	TEGRA_PIN_DMIC1_CLK_PE0,
};

static const unsigned dmic1_dat_pe1_pins[] = {
	TEGRA_PIN_DMIC1_DAT_PE1,
};

static const unsigned dmic2_clk_pe2_pins[] = {
	TEGRA_PIN_DMIC2_CLK_PE2,
};

static const unsigned dmic2_dat_pe3_pins[] = {
	TEGRA_PIN_DMIC2_DAT_PE3,
};

static const unsigned dmic3_clk_pe4_pins[] = {
	TEGRA_PIN_DMIC3_CLK_PE4,
};

static const unsigned dmic3_dat_pe5_pins[] = {
	TEGRA_PIN_DMIC3_DAT_PE5,
};

static const unsigned pe6_pins[] = {
	TEGRA_PIN_PE6,
};

static const unsigned pe7_pins[] = {
	TEGRA_PIN_PE7,
};

static const unsigned gen3_i2c_scl_pf0_pins[] = {
	TEGRA_PIN_GEN3_I2C_SCL_PF0,
};

static const unsigned gen3_i2c_sda_pf1_pins[] = {
	TEGRA_PIN_GEN3_I2C_SDA_PF1,
};

static const unsigned uart2_tx_pg0_pins[] = {
	TEGRA_PIN_UART2_TX_PG0,
};

static const unsigned uart2_rx_pg1_pins[] = {
	TEGRA_PIN_UART2_RX_PG1,
};

static const unsigned uart2_rts_pg2_pins[] = {
	TEGRA_PIN_UART2_RTS_PG2,
};

static const unsigned uart2_cts_pg3_pins[] = {
	TEGRA_PIN_UART2_CTS_PG3,
};

static const unsigned wifi_en_ph0_pins[] = {
	TEGRA_PIN_WIFI_EN_PH0,
};

static const unsigned wifi_rst_ph1_pins[] = {
	TEGRA_PIN_WIFI_RST_PH1,
};

static const unsigned wifi_wake_ap_ph2_pins[] = {
	TEGRA_PIN_WIFI_WAKE_AP_PH2,
};

static const unsigned ap_wake_bt_ph3_pins[] = {
	TEGRA_PIN_AP_WAKE_BT_PH3,
};

static const unsigned bt_rst_ph4_pins[] = {
	TEGRA_PIN_BT_RST_PH4,
};

static const unsigned bt_wake_ap_ph5_pins[] = {
	TEGRA_PIN_BT_WAKE_AP_PH5,
};

static const unsigned ph6_pins[] = {
	TEGRA_PIN_PH6,
};

static const unsigned ap_wake_nfc_ph7_pins[] = {
	TEGRA_PIN_AP_WAKE_NFC_PH7,
};

static const unsigned nfc_en_pi0_pins[] = {
	TEGRA_PIN_NFC_EN_PI0,
};

static const unsigned nfc_int_pi1_pins[] = {
	TEGRA_PIN_NFC_INT_PI1,
};

static const unsigned gps_en_pi2_pins[] = {
	TEGRA_PIN_GPS_EN_PI2,
};

static const unsigned gps_rst_pi3_pins[] = {
	TEGRA_PIN_GPS_RST_PI3,
};

static const unsigned uart4_tx_pi4_pins[] = {
	TEGRA_PIN_UART4_TX_PI4,
};

static const unsigned uart4_rx_pi5_pins[] = {
	TEGRA_PIN_UART4_RX_PI5,
};

static const unsigned uart4_rts_pi6_pins[] = {
	TEGRA_PIN_UART4_RTS_PI6,
};

static const unsigned uart4_cts_pi7_pins[] = {
	TEGRA_PIN_UART4_CTS_PI7,
};

static const unsigned gen1_i2c_sda_pj0_pins[] = {
	TEGRA_PIN_GEN1_I2C_SDA_PJ0,
};

static const unsigned gen1_i2c_scl_pj1_pins[] = {
	TEGRA_PIN_GEN1_I2C_SCL_PJ1,
};

static const unsigned gen2_i2c_scl_pj2_pins[] = {
	TEGRA_PIN_GEN2_I2C_SCL_PJ2,
};

static const unsigned gen2_i2c_sda_pj3_pins[] = {
	TEGRA_PIN_GEN2_I2C_SDA_PJ3,
};

static const unsigned dap4_fs_pj4_pins[] = {
	TEGRA_PIN_DAP4_FS_PJ4,
};

static const unsigned dap4_din_pj5_pins[] = {
	TEGRA_PIN_DAP4_DIN_PJ5,
};

static const unsigned dap4_dout_pj6_pins[] = {
	TEGRA_PIN_DAP4_DOUT_PJ6,
};

static const unsigned dap4_sclk_pj7_pins[] = {
	TEGRA_PIN_DAP4_SCLK_PJ7,
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

static const unsigned pk5_pins[] = {
	TEGRA_PIN_PK5,
};

static const unsigned pk6_pins[] = {
	TEGRA_PIN_PK6,
};

static const unsigned pk7_pins[] = {
	TEGRA_PIN_PK7,
};

static const unsigned pl0_pins[] = {
	TEGRA_PIN_PL0,
};

static const unsigned pl1_pins[] = {
	TEGRA_PIN_PL1,
};

static const unsigned sdmmc1_clk_pm0_pins[] = {
	TEGRA_PIN_SDMMC1_CLK_PM0,
};

static const unsigned sdmmc1_cmd_pm1_pins[] = {
	TEGRA_PIN_SDMMC1_CMD_PM1,
};

static const unsigned sdmmc1_dat3_pm2_pins[] = {
	TEGRA_PIN_SDMMC1_DAT3_PM2,
};

static const unsigned sdmmc1_dat2_pm3_pins[] = {
	TEGRA_PIN_SDMMC1_DAT2_PM3,
};

static const unsigned sdmmc1_dat1_pm4_pins[] = {
	TEGRA_PIN_SDMMC1_DAT1_PM4,
};

static const unsigned sdmmc1_dat0_pm5_pins[] = {
	TEGRA_PIN_SDMMC1_DAT0_PM5,
};

static const unsigned sdmmc3_clk_pp0_pins[] = {
	TEGRA_PIN_SDMMC3_CLK_PP0,
};

static const unsigned sdmmc3_cmd_pp1_pins[] = {
	TEGRA_PIN_SDMMC3_CMD_PP1,
};

static const unsigned sdmmc3_dat3_pp2_pins[] = {
	TEGRA_PIN_SDMMC3_DAT3_PP2,
};

static const unsigned sdmmc3_dat2_pp3_pins[] = {
	TEGRA_PIN_SDMMC3_DAT2_PP3,
};

static const unsigned sdmmc3_dat1_pp4_pins[] = {
	TEGRA_PIN_SDMMC3_DAT1_PP4,
};

static const unsigned sdmmc3_dat0_pp5_pins[] = {
	TEGRA_PIN_SDMMC3_DAT0_PP5,
};

static const unsigned cam1_mclk_ps0_pins[] = {
	TEGRA_PIN_CAM1_MCLK_PS0,
};

static const unsigned cam2_mclk_ps1_pins[] = {
	TEGRA_PIN_CAM2_MCLK_PS1,
};

static const unsigned cam_i2c_scl_ps2_pins[] = {
	TEGRA_PIN_CAM_I2C_SCL_PS2,
};

static const unsigned cam_i2c_sda_ps3_pins[] = {
	TEGRA_PIN_CAM_I2C_SDA_PS3,
};

static const unsigned cam_rst_ps4_pins[] = {
	TEGRA_PIN_CAM_RST_PS4,
};

static const unsigned cam_af_en_ps5_pins[] = {
	TEGRA_PIN_CAM_AF_EN_PS5,
};

static const unsigned cam_flash_en_ps6_pins[] = {
	TEGRA_PIN_CAM_FLASH_EN_PS6,
};

static const unsigned cam1_pwdn_ps7_pins[] = {
	TEGRA_PIN_CAM1_PWDN_PS7,
};

static const unsigned cam2_pwdn_pt0_pins[] = {
	TEGRA_PIN_CAM2_PWDN_PT0,
};

static const unsigned cam1_strobe_pt1_pins[] = {
	TEGRA_PIN_CAM1_STROBE_PT1,
};

static const unsigned uart1_tx_pu0_pins[] = {
	TEGRA_PIN_UART1_TX_PU0,
};

static const unsigned uart1_rx_pu1_pins[] = {
	TEGRA_PIN_UART1_RX_PU1,
};

static const unsigned uart1_rts_pu2_pins[] = {
	TEGRA_PIN_UART1_RTS_PU2,
};

static const unsigned uart1_cts_pu3_pins[] = {
	TEGRA_PIN_UART1_CTS_PU3,
};

static const unsigned lcd_bl_pwm_pv0_pins[] = {
	TEGRA_PIN_LCD_BL_PWM_PV0,
};

static const unsigned lcd_bl_en_pv1_pins[] = {
	TEGRA_PIN_LCD_BL_EN_PV1,
};

static const unsigned lcd_rst_pv2_pins[] = {
	TEGRA_PIN_LCD_RST_PV2,
};

static const unsigned lcd_gpio1_pv3_pins[] = {
	TEGRA_PIN_LCD_GPIO1_PV3,
};

static const unsigned lcd_gpio2_pv4_pins[] = {
	TEGRA_PIN_LCD_GPIO2_PV4,
};

static const unsigned ap_ready_pv5_pins[] = {
	TEGRA_PIN_AP_READY_PV5,
};

static const unsigned touch_rst_pv6_pins[] = {
	TEGRA_PIN_TOUCH_RST_PV6,
};

static const unsigned touch_clk_pv7_pins[] = {
	TEGRA_PIN_TOUCH_CLK_PV7,
};

static const unsigned modem_wake_ap_px0_pins[] = {
	TEGRA_PIN_MODEM_WAKE_AP_PX0,
};

static const unsigned touch_int_px1_pins[] = {
	TEGRA_PIN_TOUCH_INT_PX1,
};

static const unsigned motion_int_px2_pins[] = {
	TEGRA_PIN_MOTION_INT_PX2,
};

static const unsigned als_prox_int_px3_pins[] = {
	TEGRA_PIN_ALS_PROX_INT_PX3,
};

static const unsigned temp_alert_px4_pins[] = {
	TEGRA_PIN_TEMP_ALERT_PX4,
};

static const unsigned button_power_on_px5_pins[] = {
	TEGRA_PIN_BUTTON_POWER_ON_PX5,
};

static const unsigned button_vol_up_px6_pins[] = {
	TEGRA_PIN_BUTTON_VOL_UP_PX6,
};

static const unsigned button_vol_down_px7_pins[] = {
	TEGRA_PIN_BUTTON_VOL_DOWN_PX7,
};

static const unsigned button_slide_sw_py0_pins[] = {
	TEGRA_PIN_BUTTON_SLIDE_SW_PY0,
};

static const unsigned button_home_py1_pins[] = {
	TEGRA_PIN_BUTTON_HOME_PY1,
};

static const unsigned lcd_te_py2_pins[] = {
	TEGRA_PIN_LCD_TE_PY2,
};

static const unsigned pwr_i2c_scl_py3_pins[] = {
	TEGRA_PIN_PWR_I2C_SCL_PY3,
};

static const unsigned pwr_i2c_sda_py4_pins[] = {
	TEGRA_PIN_PWR_I2C_SDA_PY4,
};

static const unsigned clk_32k_out_py5_pins[] = {
	TEGRA_PIN_CLK_32K_OUT_PY5,
};

static const unsigned pz0_pins[] = {
	TEGRA_PIN_PZ0,
};

static const unsigned pz1_pins[] = {
	TEGRA_PIN_PZ1,
};

static const unsigned pz2_pins[] = {
	TEGRA_PIN_PZ2,
};

static const unsigned pz3_pins[] = {
	TEGRA_PIN_PZ3,
};

static const unsigned pz4_pins[] = {
	TEGRA_PIN_PZ4,
};

static const unsigned pz5_pins[] = {
	TEGRA_PIN_PZ5,
};

static const unsigned dap2_fs_paa0_pins[] = {
	TEGRA_PIN_DAP2_FS_PAA0,
};

static const unsigned dap2_sclk_paa1_pins[] = {
	TEGRA_PIN_DAP2_SCLK_PAA1,
};

static const unsigned dap2_din_paa2_pins[] = {
	TEGRA_PIN_DAP2_DIN_PAA2,
};

static const unsigned dap2_dout_paa3_pins[] = {
	TEGRA_PIN_DAP2_DOUT_PAA3,
};

static const unsigned aud_mclk_pbb0_pins[] = {
	TEGRA_PIN_AUD_MCLK_PBB0,
};

static const unsigned dvfs_pwm_pbb1_pins[] = {
	TEGRA_PIN_DVFS_PWM_PBB1,
};

static const unsigned dvfs_clk_pbb2_pins[] = {
	TEGRA_PIN_DVFS_CLK_PBB2,
};

static const unsigned gpio_x1_aud_pbb3_pins[] = {
	TEGRA_PIN_GPIO_X1_AUD_PBB3,
};

static const unsigned gpio_x3_aud_pbb4_pins[] = {
	TEGRA_PIN_GPIO_X3_AUD_PBB4,
};

static const unsigned hdmi_cec_pcc0_pins[] = {
	TEGRA_PIN_HDMI_CEC_PCC0,
};

static const unsigned hdmi_int_dp_hpd_pcc1_pins[] = {
	TEGRA_PIN_HDMI_INT_DP_HPD_PCC1,
};

static const unsigned spdif_out_pcc2_pins[] = {
	TEGRA_PIN_SPDIF_OUT_PCC2,
};

static const unsigned spdif_in_pcc3_pins[] = {
	TEGRA_PIN_SPDIF_IN_PCC3,
};

static const unsigned usb_vbus_en0_pcc4_pins[] = {
	TEGRA_PIN_USB_VBUS_EN0_PCC4,
};

static const unsigned usb_vbus_en1_pcc5_pins[] = {
	TEGRA_PIN_USB_VBUS_EN1_PCC5,
};

static const unsigned dp_hpd0_pcc6_pins[] = {
	TEGRA_PIN_DP_HPD0_PCC6,
};

static const unsigned pcc7_pins[] = {
	TEGRA_PIN_PCC7,
};

static const unsigned spi2_cs1_pdd0_pins[] = {
	TEGRA_PIN_SPI2_CS1_PDD0,
};

static const unsigned qspi_sck_pee0_pins[] = {
	TEGRA_PIN_QSPI_SCK_PEE0,
};

static const unsigned qspi_cs_n_pee1_pins[] = {
	TEGRA_PIN_QSPI_CS_N_PEE1,
};

static const unsigned qspi_io0_pee2_pins[] = {
	TEGRA_PIN_QSPI_IO0_PEE2,
};

static const unsigned qspi_io1_pee3_pins[] = {
	TEGRA_PIN_QSPI_IO1_PEE3,
};

static const unsigned qspi_io2_pee4_pins[] = {
	TEGRA_PIN_QSPI_IO2_PEE4,
};

static const unsigned qspi_io3_pee5_pins[] = {
	TEGRA_PIN_QSPI_IO3_PEE5,
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

static const unsigned clk_32k_in_pins[] = {
	TEGRA_PIN_CLK_32K_IN,
};

static const unsigned jtag_rtck_pins[] = {
	TEGRA_PIN_JTAG_RTCK,
};

static const unsigned batt_bcl_pins[] = {
	TEGRA_PIN_BATT_BCL,
};

static const unsigned clk_req_pins[] = {
	TEGRA_PIN_CLK_REQ,
};

static const unsigned shutdown_pins[] = {
	TEGRA_PIN_SHUTDOWN,
};

static const unsigned drive_pa6_pins[] = {
	TEGRA_PIN_PA6,
};

static const unsigned drive_pcc7_pins[] = {
	TEGRA_PIN_PCC7,
};

static const unsigned drive_pe6_pins[] = {
	TEGRA_PIN_PE6,
};

static const unsigned drive_pe7_pins[] = {
	TEGRA_PIN_PE7,
};

static const unsigned drive_ph6_pins[] = {
	TEGRA_PIN_PH6,
};

static const unsigned drive_pk0_pins[] = {
	TEGRA_PIN_PK0,
};

static const unsigned drive_pk1_pins[] = {
	TEGRA_PIN_PK1,
};

static const unsigned drive_pk2_pins[] = {
	TEGRA_PIN_PK2,
};

static const unsigned drive_pk3_pins[] = {
	TEGRA_PIN_PK3,
};

static const unsigned drive_pk4_pins[] = {
	TEGRA_PIN_PK4,
};

static const unsigned drive_pk5_pins[] = {
	TEGRA_PIN_PK5,
};

static const unsigned drive_pk6_pins[] = {
	TEGRA_PIN_PK6,
};

static const unsigned drive_pk7_pins[] = {
	TEGRA_PIN_PK7,
};

static const unsigned drive_pl0_pins[] = {
	TEGRA_PIN_PL0,
};

static const unsigned drive_pl1_pins[] = {
	TEGRA_PIN_PL1,
};

static const unsigned drive_pz0_pins[] = {
	TEGRA_PIN_PZ0,
};

static const unsigned drive_pz1_pins[] = {
	TEGRA_PIN_PZ1,
};

static const unsigned drive_pz2_pins[] = {
	TEGRA_PIN_PZ2,
};

static const unsigned drive_pz3_pins[] = {
	TEGRA_PIN_PZ3,
};

static const unsigned drive_pz4_pins[] = {
	TEGRA_PIN_PZ4,
};

static const unsigned drive_pz5_pins[] = {
	TEGRA_PIN_PZ5,
};

static const unsigned drive_sdmmc1_pins[] = {
	TEGRA_PIN_SDMMC1_CLK_PM0,
	TEGRA_PIN_SDMMC1_CMD_PM1,
	TEGRA_PIN_SDMMC1_DAT3_PM2,
	TEGRA_PIN_SDMMC1_DAT2_PM3,
	TEGRA_PIN_SDMMC1_DAT1_PM4,
	TEGRA_PIN_SDMMC1_DAT0_PM5,
};

static const unsigned drive_sdmmc2_pins[] = {
};

static const unsigned drive_sdmmc3_pins[] = {
	TEGRA_PIN_SDMMC3_CLK_PP0,
	TEGRA_PIN_SDMMC3_CMD_PP1,
	TEGRA_PIN_SDMMC3_DAT3_PP2,
	TEGRA_PIN_SDMMC3_DAT2_PP3,
	TEGRA_PIN_SDMMC3_DAT1_PP4,
	TEGRA_PIN_SDMMC3_DAT0_PP5,
};

static const unsigned drive_sdmmc4_pins[] = {
};

enum tegra_mux {
	TEGRA_MUX_AUD,
	TEGRA_MUX_BCL,
	TEGRA_MUX_BLINK,
	TEGRA_MUX_CCLA,
	TEGRA_MUX_CEC,
	TEGRA_MUX_CLDVFS,
	TEGRA_MUX_CLK,
	TEGRA_MUX_CORE,
	TEGRA_MUX_CPU,
	TEGRA_MUX_DISPLAYA,
	TEGRA_MUX_DISPLAYB,
	TEGRA_MUX_DMIC1,
	TEGRA_MUX_DMIC2,
	TEGRA_MUX_DMIC3,
	TEGRA_MUX_DP,
	TEGRA_MUX_DTV,
	TEGRA_MUX_EXTPERIPH3,
	TEGRA_MUX_I2C1,
	TEGRA_MUX_I2C2,
	TEGRA_MUX_I2C3,
	TEGRA_MUX_I2CPMU,
	TEGRA_MUX_I2CVI,
	TEGRA_MUX_I2S1,
	TEGRA_MUX_I2S2,
	TEGRA_MUX_I2S3,
	TEGRA_MUX_I2S4A,
	TEGRA_MUX_I2S4B,
	TEGRA_MUX_I2S5A,
	TEGRA_MUX_I2S5B,
	TEGRA_MUX_IQC0,
	TEGRA_MUX_IQC1,
	TEGRA_MUX_JTAG,
	TEGRA_MUX_PE,
	TEGRA_MUX_PE0,
	TEGRA_MUX_PE1,
	TEGRA_MUX_PMI,
	TEGRA_MUX_PWM0,
	TEGRA_MUX_PWM1,
	TEGRA_MUX_PWM2,
	TEGRA_MUX_PWM3,
	TEGRA_MUX_QSPI,
	TEGRA_MUX_RSVD0,
	TEGRA_MUX_RSVD1,
	TEGRA_MUX_RSVD2,
	TEGRA_MUX_RSVD3,
	TEGRA_MUX_SATA,
	TEGRA_MUX_SDMMC1,
	TEGRA_MUX_SDMMC3,
	TEGRA_MUX_SHUTDOWN,
	TEGRA_MUX_SOC,
	TEGRA_MUX_SOR0,
	TEGRA_MUX_SOR1,
	TEGRA_MUX_SPDIF,
	TEGRA_MUX_SPI1,
	TEGRA_MUX_SPI2,
	TEGRA_MUX_SPI3,
	TEGRA_MUX_SPI4,
	TEGRA_MUX_SYS,
	TEGRA_MUX_TOUCH,
	TEGRA_MUX_UART,
	TEGRA_MUX_UARTA,
	TEGRA_MUX_UARTB,
	TEGRA_MUX_UARTC,
	TEGRA_MUX_UARTD,
	TEGRA_MUX_USB,
	TEGRA_MUX_VGP1,
	TEGRA_MUX_VGP2,
	TEGRA_MUX_VGP3,
	TEGRA_MUX_VGP4,
	TEGRA_MUX_VGP5,
	TEGRA_MUX_VGP6,
	TEGRA_MUX_VIMCLK,
	TEGRA_MUX_VIMCLK2,
};

#define FUNCTION(fname)					\
	{						\
		.name = #fname,				\
	}

static struct tegra_function tegra210_functions[] = {
	FUNCTION(aud),
	FUNCTION(bcl),
	FUNCTION(blink),
	FUNCTION(ccla),
	FUNCTION(cec),
	FUNCTION(cldvfs),
	FUNCTION(clk),
	FUNCTION(core),
	FUNCTION(cpu),
	FUNCTION(displaya),
	FUNCTION(displayb),
	FUNCTION(dmic1),
	FUNCTION(dmic2),
	FUNCTION(dmic3),
	FUNCTION(dp),
	FUNCTION(dtv),
	FUNCTION(extperiph3),
	FUNCTION(i2c1),
	FUNCTION(i2c2),
	FUNCTION(i2c3),
	FUNCTION(i2cpmu),
	FUNCTION(i2cvi),
	FUNCTION(i2s1),
	FUNCTION(i2s2),
	FUNCTION(i2s3),
	FUNCTION(i2s4a),
	FUNCTION(i2s4b),
	FUNCTION(i2s5a),
	FUNCTION(i2s5b),
	FUNCTION(iqc0),
	FUNCTION(iqc1),
	FUNCTION(jtag),
	FUNCTION(pe),
	FUNCTION(pe0),
	FUNCTION(pe1),
	FUNCTION(pmi),
	FUNCTION(pwm0),
	FUNCTION(pwm1),
	FUNCTION(pwm2),
	FUNCTION(pwm3),
	FUNCTION(qspi),
	FUNCTION(rsvd0),
	FUNCTION(rsvd1),
	FUNCTION(rsvd2),
	FUNCTION(rsvd3),
	FUNCTION(sata),
	FUNCTION(sdmmc1),
	FUNCTION(sdmmc3),
	FUNCTION(shutdown),
	FUNCTION(soc),
	FUNCTION(sor0),
	FUNCTION(sor1),
	FUNCTION(spdif),
	FUNCTION(spi1),
	FUNCTION(spi2),
	FUNCTION(spi3),
	FUNCTION(spi4),
	FUNCTION(sys),
	FUNCTION(touch),
	FUNCTION(uart),
	FUNCTION(uarta),
	FUNCTION(uartb),
	FUNCTION(uartc),
	FUNCTION(uartd),
	FUNCTION(usb),
	FUNCTION(vgp1),
	FUNCTION(vgp2),
	FUNCTION(vgp3),
	FUNCTION(vgp4),
	FUNCTION(vgp5),
	FUNCTION(vgp6),
	FUNCTION(vimclk),
	FUNCTION(vimclk2),
};

#define DRV_PINGROUP_REG_A		0x8d4	/* bank 0 */
#define PINGROUP_REG_A			0x3000	/* bank 1 */

#define DRV_PINGROUP_REG(r)		((r) - DRV_PINGROUP_REG_A)
#define PINGROUP_REG(r)			((r) - PINGROUP_REG_A)

#define PINGROUP_BIT_Y(b)		(b)
#define PINGROUP_BIT_N(b)		(-1)

#define PINGROUP(pg_name, f0, f1, f2, f3, r, hsm, drvtype, e_io_hv,	\
		 rdrv, drvdn_b, drvdn_w, drvup_b, drvup_w, slwr_b,	\
		 slwr_w, slwf_b, slwf_w)				\
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
		.einput_bit = 6,					\
		.odrain_bit = 11,					\
		.lock_bit = 7,						\
		.ioreset_bit = -1,					\
		.rcv_sel_bit = PINGROUP_BIT_##e_io_hv(10),		\
		.hsm_bit = PINGROUP_BIT_##hsm(9),			\
		.schmitt_bit = 12,					\
		.drvtype_bit = PINGROUP_BIT_##drvtype(13),		\
		.drv_reg = DRV_PINGROUP_REG(rdrv),			\
		.drv_bank = 0,						\
		.lpmd_bit = -1,						\
		.drvdn_bit = drvdn_b,					\
		.drvdn_width = drvdn_w,					\
		.drvup_bit = drvup_b,					\
		.drvup_width = drvup_w,					\
		.slwr_bit = slwr_b,					\
		.slwr_width = slwr_w,					\
		.slwf_bit = slwf_b,					\
		.slwf_width = slwf_w,					\
		.parked_bitmask = BIT(5),				\
	}

#define DRV_PINGROUP(pg_name, r, prk_mask, drvdn_b, drvdn_w, drvup_b,	\
		     drvup_w, slwr_b, slwr_w, slwf_b, slwf_w)		\
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
		.hsm_bit = -1,						\
		.schmitt_bit = -1,					\
		.lpmd_bit = -1,						\
		.drvdn_bit = drvdn_b,					\
		.drvdn_width = drvdn_w,					\
		.drvup_bit = drvup_b,					\
		.drvup_width = drvup_w,					\
		.slwr_bit = slwr_b,					\
		.slwr_width = slwr_w,					\
		.slwf_bit = slwf_b,					\
		.slwf_width = slwf_w,					\
		.drvtype_bit = -1,					\
		.parked_bitmask = prk_mask,				\
	}

static const struct tegra_pingroup tegra210_groups[] = {
	/*       pg_name,              f0,         f1,     f2,    f3,    r,      hsm, drvtype, e_io_hv, rdrv,  drvdn_b, drvdn_w, drvup_b, drvup_w, slwr_b, slwr_w, slwf_b, slwf_w */
	PINGROUP(sdmmc1_clk_pm0,       SDMMC1,     RSVD1,  RSVD2, RSVD3, 0x3000, Y,   Y,       N,       -1,    -1,      -1,      -1,      -1,      -1,     -1,     -1,     -1),
	PINGROUP(sdmmc1_cmd_pm1,       SDMMC1,     SPI3,   RSVD2, RSVD3, 0x3004, Y,   Y,       N,       -1,    -1,      -1,      -1,      -1,      -1,     -1,     -1,     -1),
	PINGROUP(sdmmc1_dat3_pm2,      SDMMC1,     SPI3,   RSVD2, RSVD3, 0x3008, Y,   Y,       N,       -1,    -1,      -1,      -1,      -1,      -1,     -1,     -1,     -1),
	PINGROUP(sdmmc1_dat2_pm3,      SDMMC1,     SPI3,   RSVD2, RSVD3, 0x300c, Y,   Y,       N,       -1,    -1,      -1,      -1,      -1,      -1,     -1,     -1,     -1),
	PINGROUP(sdmmc1_dat1_pm4,      SDMMC1,     SPI3,   RSVD2, RSVD3, 0x3010, Y,   Y,       N,       -1,    -1,      -1,      -1,      -1,      -1,     -1,     -1,     -1),
	PINGROUP(sdmmc1_dat0_pm5,      SDMMC1,     RSVD1,  RSVD2, RSVD3, 0x3014, Y,   Y,       N,       -1,    -1,      -1,      -1,      -1,      -1,     -1,     -1,     -1),
	PINGROUP(sdmmc3_clk_pp0,       SDMMC3,     RSVD1,  RSVD2, RSVD3, 0x301c, Y,   Y,       N,       -1,    -1,      -1,      -1,      -1,      -1,     -1,     -1,     -1),
	PINGROUP(sdmmc3_cmd_pp1,       SDMMC3,     RSVD1,  RSVD2, RSVD3, 0x3020, Y,   Y,       N,       -1,    -1,      -1,      -1,      -1,      -1,     -1,     -1,     -1),
	PINGROUP(sdmmc3_dat0_pp5,      SDMMC3,     RSVD1,  RSVD2, RSVD3, 0x3024, Y,   Y,       N,       -1,    -1,      -1,      -1,      -1,      -1,     -1,     -1,     -1),
	PINGROUP(sdmmc3_dat1_pp4,      SDMMC3,     RSVD1,  RSVD2, RSVD3, 0x3028, Y,   Y,       N,       -1,    -1,      -1,      -1,      -1,      -1,     -1,     -1,     -1),
	PINGROUP(sdmmc3_dat2_pp3,      SDMMC3,     RSVD1,  RSVD2, RSVD3, 0x302c, Y,   Y,       N,       -1,    -1,      -1,      -1,      -1,      -1,     -1,     -1,     -1),
	PINGROUP(sdmmc3_dat3_pp2,      SDMMC3,     RSVD1,  RSVD2, RSVD3, 0x3030, Y,   Y,       N,       -1,    -1,      -1,      -1,      -1,      -1,     -1,     -1,     -1),
	PINGROUP(pex_l0_rst_n_pa0,     PE0,        RSVD1,  RSVD2, RSVD3, 0x3038, N,   N,       Y,       0xa5c, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(pex_l0_clkreq_n_pa1,  PE0,        RSVD1,  RSVD2, RSVD3, 0x303c, N,   N,       Y,       0xa58, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(pex_wake_n_pa2,       PE,         RSVD1,  RSVD2, RSVD3, 0x3040, N,   N,       Y,       0xa68, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(pex_l1_rst_n_pa3,     PE1,        RSVD1,  RSVD2, RSVD3, 0x3044, N,   N,       Y,       0xa64, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(pex_l1_clkreq_n_pa4,  PE1,        RSVD1,  RSVD2, RSVD3, 0x3048, N,   N,       Y,       0xa60, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(sata_led_active_pa5,  SATA,       RSVD1,  RSVD2, RSVD3, 0x304c, N,   N,       N,       0xa94, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(spi1_mosi_pc0,        SPI1,       RSVD1,  RSVD2, RSVD3, 0x3050, Y,   Y,       N,       0xae0, -1,      -1,      -1,      -1,      28,     2,      30,     2),
	PINGROUP(spi1_miso_pc1,        SPI1,       RSVD1,  RSVD2, RSVD3, 0x3054, Y,   Y,       N,       0xadc, -1,      -1,      -1,      -1,      28,     2,      30,     2),
	PINGROUP(spi1_sck_pc2,         SPI1,       RSVD1,  RSVD2, RSVD3, 0x3058, Y,   Y,       N,       0xae4, -1,      -1,      -1,      -1,      28,     2,      30,     2),
	PINGROUP(spi1_cs0_pc3,         SPI1,       RSVD1,  RSVD2, RSVD3, 0x305c, Y,   Y,       N,       0xad4, -1,      -1,      -1,      -1,      28,     2,      30,     2),
	PINGROUP(spi1_cs1_pc4,         SPI1,       RSVD1,  RSVD2, RSVD3, 0x3060, Y,   Y,       N,       0xad8, -1,      -1,      -1,      -1,      28,     2,      30,     2),
	PINGROUP(spi2_mosi_pb4,        SPI2,       DTV,    RSVD2, RSVD3, 0x3064, Y,   Y,       N,       0xaf4, -1,      -1,      -1,      -1,      28,     2,      30,     2),
	PINGROUP(spi2_miso_pb5,        SPI2,       DTV,    RSVD2, RSVD3, 0x3068, Y,   Y,       N,       0xaf0, -1,      -1,      -1,      -1,      28,     2,      30,     2),
	PINGROUP(spi2_sck_pb6,         SPI2,       DTV,    RSVD2, RSVD3, 0x306c, Y,   Y,       N,       0xaf8, -1,      -1,      -1,      -1,      28,     2,      30,     2),
	PINGROUP(spi2_cs0_pb7,         SPI2,       DTV,    RSVD2, RSVD3, 0x3070, Y,   Y,       N,       0xae8, -1,      -1,      -1,      -1,      28,     2,      30,     2),
	PINGROUP(spi2_cs1_pdd0,        SPI2,       RSVD1,  RSVD2, RSVD3, 0x3074, Y,   Y,       N,       0xaec, -1,      -1,      -1,      -1,      28,     2,      30,     2),
	PINGROUP(spi4_mosi_pc7,        SPI4,       RSVD1,  RSVD2, RSVD3, 0x3078, Y,   Y,       N,       0xb04, -1,      -1,      -1,      -1,      28,     2,      30,     2),
	PINGROUP(spi4_miso_pd0,        SPI4,       RSVD1,  RSVD2, RSVD3, 0x307c, Y,   Y,       N,       0xb00, -1,      -1,      -1,      -1,      28,     2,      30,     2),
	PINGROUP(spi4_sck_pc5,         SPI4,       RSVD1,  RSVD2, RSVD3, 0x3080, Y,   Y,       N,       0xb08, -1,      -1,      -1,      -1,      28,     2,      30,     2),
	PINGROUP(spi4_cs0_pc6,         SPI4,       RSVD1,  RSVD2, RSVD3, 0x3084, Y,   Y,       N,       0xafc, -1,      -1,      -1,      -1,      28,     2,      30,     2),
	PINGROUP(qspi_sck_pee0,        QSPI,       RSVD1,  RSVD2, RSVD3, 0x3088, Y,   Y,       N,       0xa90, -1,      -1,      -1,      -1,      28,     2,      30,     2),
	PINGROUP(qspi_cs_n_pee1,       QSPI,       RSVD1,  RSVD2, RSVD3, 0x308c, Y,   Y,       N,       -1,    -1,      -1,      -1,      -1,      -1,     -1,     -1,     -1),
	PINGROUP(qspi_io0_pee2,        QSPI,       RSVD1,  RSVD2, RSVD3, 0x3090, Y,   Y,       N,       -1,    -1,      -1,      -1,      -1,      -1,     -1,     -1,     -1),
	PINGROUP(qspi_io1_pee3,        QSPI,       RSVD1,  RSVD2, RSVD3, 0x3094, Y,   Y,       N,       -1,    -1,      -1,      -1,      -1,      -1,     -1,     -1,     -1),
	PINGROUP(qspi_io2_pee4,        QSPI,       RSVD1,  RSVD2, RSVD3, 0x3098, Y,   Y,       N,       -1,    -1,      -1,      -1,      -1,      -1,     -1,     -1,     -1),
	PINGROUP(qspi_io3_pee5,        QSPI,       RSVD1,  RSVD2, RSVD3, 0x309c, Y,   Y,       N,       -1,    -1,      -1,      -1,      -1,      -1,     -1,     -1,     -1),
	PINGROUP(dmic1_clk_pe0,        DMIC1,      I2S3,   RSVD2, RSVD3, 0x30a4, N,   N,       N,       0x984, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(dmic1_dat_pe1,        DMIC1,      I2S3,   RSVD2, RSVD3, 0x30a8, N,   N,       N,       0x988, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(dmic2_clk_pe2,        DMIC2,      I2S3,   RSVD2, RSVD3, 0x30ac, N,   N,       N,       0x98c, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(dmic2_dat_pe3,        DMIC2,      I2S3,   RSVD2, RSVD3, 0x30b0, N,   N,       N,       0x990, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(dmic3_clk_pe4,        DMIC3,      I2S5A,  RSVD2, RSVD3, 0x30b4, N,   N,       N,       0x994, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(dmic3_dat_pe5,        DMIC3,      I2S5A,  RSVD2, RSVD3, 0x30b8, N,   N,       N,       0x998, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(gen1_i2c_scl_pj1,     I2C1,       RSVD1,  RSVD2, RSVD3, 0x30bc, N,   N,       Y,       0x9a8, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(gen1_i2c_sda_pj0,     I2C1,       RSVD1,  RSVD2, RSVD3, 0x30c0, N,   N,       Y,       0x9ac, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(gen2_i2c_scl_pj2,     I2C2,       RSVD1,  RSVD2, RSVD3, 0x30c4, N,   N,       Y,       0x9b0, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(gen2_i2c_sda_pj3,     I2C2,       RSVD1,  RSVD2, RSVD3, 0x30c8, N,   N,       Y,       0x9b4, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(gen3_i2c_scl_pf0,     I2C3,       RSVD1,  RSVD2, RSVD3, 0x30cc, N,   N,       Y,       0x9b8, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(gen3_i2c_sda_pf1,     I2C3,       RSVD1,  RSVD2, RSVD3, 0x30d0, N,   N,       Y,       0x9bc, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(cam_i2c_scl_ps2,      I2C3,       I2CVI,  RSVD2, RSVD3, 0x30d4, N,   N,       Y,       0x934, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(cam_i2c_sda_ps3,      I2C3,       I2CVI,  RSVD2, RSVD3, 0x30d8, N,   N,       Y,       0x938, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(pwr_i2c_scl_py3,      I2CPMU,     RSVD1,  RSVD2, RSVD3, 0x30dc, N,   N,       Y,       0xa6c, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(pwr_i2c_sda_py4,      I2CPMU,     RSVD1,  RSVD2, RSVD3, 0x30e0, N,   N,       Y,       0xa70, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(uart1_tx_pu0,         UARTA,      RSVD1,  RSVD2, RSVD3, 0x30e4, N,   N,       N,       0xb28, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(uart1_rx_pu1,         UARTA,      RSVD1,  RSVD2, RSVD3, 0x30e8, N,   N,       N,       0xb24, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(uart1_rts_pu2,        UARTA,      RSVD1,  RSVD2, RSVD3, 0x30ec, N,   N,       N,       0xb20, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(uart1_cts_pu3,        UARTA,      RSVD1,  RSVD2, RSVD3, 0x30f0, N,   N,       N,       0xb1c, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(uart2_tx_pg0,         UARTB,      I2S4A,  SPDIF, UART,  0x30f4, N,   N,       N,       0xb38, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(uart2_rx_pg1,         UARTB,      I2S4A,  SPDIF, UART,  0x30f8, N,   N,       N,       0xb34, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(uart2_rts_pg2,        UARTB,      I2S4A,  RSVD2, UART,  0x30fc, N,   N,       N,       0xb30, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(uart2_cts_pg3,        UARTB,      I2S4A,  RSVD2, UART,  0x3100, N,   N,       N,       0xb2c, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(uart3_tx_pd1,         UARTC,      SPI4,   RSVD2, RSVD3, 0x3104, N,   N,       N,       0xb48, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(uart3_rx_pd2,         UARTC,      SPI4,   RSVD2, RSVD3, 0x3108, N,   N,       N,       0xb44, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(uart3_rts_pd3,        UARTC,      SPI4,   RSVD2, RSVD3, 0x310c, N,   N,       N,       0xb40, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(uart3_cts_pd4,        UARTC,      SPI4,   RSVD2, RSVD3, 0x3110, N,   N,       N,       0xb3c, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(uart4_tx_pi4,         UARTD,      UART,   RSVD2, RSVD3, 0x3114, N,   N,       N,       0xb58, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(uart4_rx_pi5,         UARTD,      UART,   RSVD2, RSVD3, 0x3118, N,   N,       N,       0xb54, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(uart4_rts_pi6,        UARTD,      UART,   RSVD2, RSVD3, 0x311c, N,   N,       N,       0xb50, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(uart4_cts_pi7,        UARTD,      UART,   RSVD2, RSVD3, 0x3120, N,   N,       N,       0xb4c, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(dap1_fs_pb0,          I2S1,       RSVD1,  RSVD2, RSVD3, 0x3124, Y,   Y,       N,       0x95c, -1,      -1,      -1,      -1,      28,     2,      30,     2),
	PINGROUP(dap1_din_pb1,         I2S1,       RSVD1,  RSVD2, RSVD3, 0x3128, Y,   Y,       N,       0x954, -1,      -1,      -1,      -1,      28,     2,      30,     2),
	PINGROUP(dap1_dout_pb2,        I2S1,       RSVD1,  RSVD2, RSVD3, 0x312c, Y,   Y,       N,       0x958, -1,      -1,      -1,      -1,      28,     2,      30,     2),
	PINGROUP(dap1_sclk_pb3,        I2S1,       RSVD1,  RSVD2, RSVD3, 0x3130, Y,   Y,       N,       0x960, -1,      -1,      -1,      -1,      28,     2,      30,     2),
	PINGROUP(dap2_fs_paa0,         I2S2,       RSVD1,  RSVD2, RSVD3, 0x3134, Y,   Y,       N,       0x96c, -1,      -1,      -1,      -1,      28,     2,      30,     2),
	PINGROUP(dap2_din_paa2,        I2S2,       RSVD1,  RSVD2, RSVD3, 0x3138, Y,   Y,       N,       0x964, -1,      -1,      -1,      -1,      28,     2,      30,     2),
	PINGROUP(dap2_dout_paa3,       I2S2,       RSVD1,  RSVD2, RSVD3, 0x313c, Y,   Y,       N,       0x968, -1,      -1,      -1,      -1,      28,     2,      30,     2),
	PINGROUP(dap2_sclk_paa1,       I2S2,       RSVD1,  RSVD2, RSVD3, 0x3140, Y,   Y,       N,       0x970, -1,      -1,      -1,      -1,      28,     2,      30,     2),
	PINGROUP(dap4_fs_pj4,          I2S4B,      RSVD1,  RSVD2, RSVD3, 0x3144, N,   N,       N,       0x97c, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(dap4_din_pj5,         I2S4B,      RSVD1,  RSVD2, RSVD3, 0x3148, N,   N,       N,       0x974, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(dap4_dout_pj6,        I2S4B,      RSVD1,  RSVD2, RSVD3, 0x314c, N,   N,       N,       0x978, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(dap4_sclk_pj7,        I2S4B,      RSVD1,  RSVD2, RSVD3, 0x3150, N,   N,       N,       0x980, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(cam1_mclk_ps0,        EXTPERIPH3, RSVD1,  RSVD2, RSVD3, 0x3154, N,   N,       N,       0x918, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(cam2_mclk_ps1,        EXTPERIPH3, RSVD1,  RSVD2, RSVD3, 0x3158, N,   N,       N,       0x924, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(jtag_rtck,            JTAG,       RSVD1,  RSVD2, RSVD3, 0x315c, N,   N,       N,       0xa2c, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(clk_32k_in,           CLK,        RSVD1,  RSVD2, RSVD3, 0x3160, N,   N,       N,       0x940, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(clk_32k_out_py5,      SOC,        BLINK,  RSVD2, RSVD3, 0x3164, N,   N,       N,       0x944, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(batt_bcl,             BCL,        RSVD1,  RSVD2, RSVD3, 0x3168, N,   N,       Y,       0x8f8, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(clk_req,              SYS,        RSVD1,  RSVD2, RSVD3, 0x316c, N,   N,       N,       0x948, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(cpu_pwr_req,          CPU,        RSVD1,  RSVD2, RSVD3, 0x3170, N,   N,       N,       0x950, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(pwr_int_n,            PMI,        RSVD1,  RSVD2, RSVD3, 0x3174, N,   N,       N,       0xa74, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(shutdown,             SHUTDOWN,   RSVD1,  RSVD2, RSVD3, 0x3178, N,   N,       N,       0xac8, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(core_pwr_req,         CORE,       RSVD1,  RSVD2, RSVD3, 0x317c, N,   N,       N,       0x94c, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(aud_mclk_pbb0,        AUD,        RSVD1,  RSVD2, RSVD3, 0x3180, N,   N,       N,       0x8f4, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(dvfs_pwm_pbb1,        RSVD0,      CLDVFS, SPI3,  RSVD3, 0x3184, N,   N,       N,       0x9a4, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(dvfs_clk_pbb2,        RSVD0,      CLDVFS, SPI3,  RSVD3, 0x3188, N,   N,       N,       0x9a0, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(gpio_x1_aud_pbb3,     RSVD0,      RSVD1,  SPI3,  RSVD3, 0x318c, N,   N,       N,       0xa14, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(gpio_x3_aud_pbb4,     RSVD0,      RSVD1,  SPI3,  RSVD3, 0x3190, N,   N,       N,       0xa18, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(pcc7,                 RSVD0,      RSVD1,  RSVD2, RSVD3, 0x3194, N,   N,       Y,       -1,    -1,      -1,      -1,      -1,      -1,     -1,     -1,     -1),
	PINGROUP(hdmi_cec_pcc0,        CEC,        RSVD1,  RSVD2, RSVD3, 0x3198, N,   N,       Y,       0xa24, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(hdmi_int_dp_hpd_pcc1, DP,         RSVD1,  RSVD2, RSVD3, 0x319c, N,   N,       Y,       0xa28, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(spdif_out_pcc2,       SPDIF,      RSVD1,  RSVD2, RSVD3, 0x31a0, N,   N,       N,       0xad0, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(spdif_in_pcc3,        SPDIF,      RSVD1,  RSVD2, RSVD3, 0x31a4, N,   N,       N,       0xacc, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(usb_vbus_en0_pcc4,    USB,        RSVD1,  RSVD2, RSVD3, 0x31a8, N,   N,       Y,       0xb5c, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(usb_vbus_en1_pcc5,    USB,        RSVD1,  RSVD2, RSVD3, 0x31ac, N,   N,       Y,       0xb60, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(dp_hpd0_pcc6,         DP,         RSVD1,  RSVD2, RSVD3, 0x31b0, N,   N,       N,       0x99c, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(wifi_en_ph0,          RSVD0,      RSVD1,  RSVD2, RSVD3, 0x31b4, N,   N,       N,       0xb64, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(wifi_rst_ph1,         RSVD0,      RSVD1,  RSVD2, RSVD3, 0x31b8, N,   N,       N,       0xb68, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(wifi_wake_ap_ph2,     RSVD0,      RSVD1,  RSVD2, RSVD3, 0x31bc, N,   N,       N,       0xb6c, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(ap_wake_bt_ph3,       RSVD0,      UARTB,  SPDIF, RSVD3, 0x31c0, N,   N,       N,       0x8ec, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(bt_rst_ph4,           RSVD0,      UARTB,  SPDIF, RSVD3, 0x31c4, N,   N,       N,       0x8fc, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(bt_wake_ap_ph5,       RSVD0,      RSVD1,  RSVD2, RSVD3, 0x31c8, N,   N,       N,       0x900, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(ap_wake_nfc_ph7,      RSVD0,      RSVD1,  RSVD2, RSVD3, 0x31cc, N,   N,       N,       0x8f0, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(nfc_en_pi0,           RSVD0,      RSVD1,  RSVD2, RSVD3, 0x31d0, N,   N,       N,       0xa50, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(nfc_int_pi1,          RSVD0,      RSVD1,  RSVD2, RSVD3, 0x31d4, N,   N,       N,       0xa54, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(gps_en_pi2,           RSVD0,      RSVD1,  RSVD2, RSVD3, 0x31d8, N,   N,       N,       0xa1c, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(gps_rst_pi3,          RSVD0,      RSVD1,  RSVD2, RSVD3, 0x31dc, N,   N,       N,       0xa20, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(cam_rst_ps4,          VGP1,       RSVD1,  RSVD2, RSVD3, 0x31e0, N,   N,       N,       0x93c, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(cam_af_en_ps5,        VIMCLK,     VGP2,   RSVD2, RSVD3, 0x31e4, N,   N,       N,       0x92c, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(cam_flash_en_ps6,     VIMCLK,     VGP3,   RSVD2, RSVD3, 0x31e8, N,   N,       N,       0x930, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(cam1_pwdn_ps7,        VGP4,       RSVD1,  RSVD2, RSVD3, 0x31ec, N,   N,       N,       0x91c, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(cam2_pwdn_pt0,        VGP5,       RSVD1,  RSVD2, RSVD3, 0x31f0, N,   N,       N,       0x928, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(cam1_strobe_pt1,      VGP6,       RSVD1,  RSVD2, RSVD3, 0x31f4, N,   N,       N,       0x920, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(lcd_te_py2,           DISPLAYA,   RSVD1,  RSVD2, RSVD3, 0x31f8, N,   N,       N,       0xa44, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(lcd_bl_pwm_pv0,       DISPLAYA,   PWM0,   SOR0,  RSVD3, 0x31fc, N,   N,       N,       0xa34, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(lcd_bl_en_pv1,        RSVD0,      RSVD1,  RSVD2, RSVD3, 0x3200, N,   N,       N,       0xa30, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(lcd_rst_pv2,          RSVD0,      RSVD1,  RSVD2, RSVD3, 0x3204, N,   N,       N,       0xa40, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(lcd_gpio1_pv3,        DISPLAYB,   RSVD1,  RSVD2, RSVD3, 0x3208, N,   N,       N,       0xa38, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(lcd_gpio2_pv4,        DISPLAYB,   PWM1,   RSVD2, SOR1,  0x320c, N,   N,       N,       0xa3c, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(ap_ready_pv5,         RSVD0,      RSVD1,  RSVD2, RSVD3, 0x3210, N,   N,       N,       0x8e8, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(touch_rst_pv6,        RSVD0,      RSVD1,  RSVD2, RSVD3, 0x3214, N,   N,       N,       0xb18, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(touch_clk_pv7,        TOUCH,      RSVD1,  RSVD2, RSVD3, 0x3218, N,   N,       N,       0xb10, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(modem_wake_ap_px0,    RSVD0,      RSVD1,  RSVD2, RSVD3, 0x321c, N,   N,       N,       0xa48, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(touch_int_px1,        RSVD0,      RSVD1,  RSVD2, RSVD3, 0x3220, N,   N,       N,       0xb14, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(motion_int_px2,       RSVD0,      RSVD1,  RSVD2, RSVD3, 0x3224, N,   N,       N,       0xa4c, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(als_prox_int_px3,     RSVD0,      RSVD1,  RSVD2, RSVD3, 0x3228, N,   N,       N,       0x8e4, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(temp_alert_px4,       RSVD0,      RSVD1,  RSVD2, RSVD3, 0x322c, N,   N,       N,       0xb0c, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(button_power_on_px5,  RSVD0,      RSVD1,  RSVD2, RSVD3, 0x3230, N,   N,       N,       0x908, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(button_vol_up_px6,    RSVD0,      RSVD1,  RSVD2, RSVD3, 0x3234, N,   N,       N,       0x914, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(button_vol_down_px7,  RSVD0,      RSVD1,  RSVD2, RSVD3, 0x3238, N,   N,       N,       0x910, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(button_slide_sw_py0,  RSVD0,      RSVD1,  RSVD2, RSVD3, 0x323c, N,   N,       N,       0x90c, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(button_home_py1,      RSVD0,      RSVD1,  RSVD2, RSVD3, 0x3240, N,   N,       N,       0x904, 12,      5,       20,      5,       -1,     -1,     -1,     -1),
	PINGROUP(pa6,                  SATA,       RSVD1,  RSVD2, RSVD3, 0x3244, N,   N,       N,       -1,    -1,      -1,      -1,      -1,      -1,     -1,     -1,     -1),
	PINGROUP(pe6,                  RSVD0,      I2S5A,  PWM2,  RSVD3, 0x3248, N,   N,       N,       -1,    -1,      -1,      -1,      -1,      -1,     -1,     -1,     -1),
	PINGROUP(pe7,                  RSVD0,      I2S5A,  PWM3,  RSVD3, 0x324c, N,   N,       N,       -1,    -1,      -1,      -1,      -1,      -1,     -1,     -1,     -1),
	PINGROUP(ph6,                  RSVD0,      RSVD1,  RSVD2, RSVD3, 0x3250, N,   N,       N,       -1,    -1,      -1,      -1,      -1,      -1,     -1,     -1,     -1),
	PINGROUP(pk0,                  IQC0,       I2S5B,  RSVD2, RSVD3, 0x3254, Y,   Y,       N,       -1,    -1,      -1,      -1,      -1,      -1,     -1,     -1,     -1),
	PINGROUP(pk1,                  IQC0,       I2S5B,  RSVD2, RSVD3, 0x3258, Y,   Y,       N,       -1,    -1,      -1,      -1,      -1,      -1,     -1,     -1,     -1),
	PINGROUP(pk2,                  IQC0,       I2S5B,  RSVD2, RSVD3, 0x325c, Y,   Y,       N,       -1,    -1,      -1,      -1,      -1,      -1,     -1,     -1,     -1),
	PINGROUP(pk3,                  IQC0,       I2S5B,  RSVD2, RSVD3, 0x3260, Y,   Y,       N,       -1,    -1,      -1,      -1,      -1,      -1,     -1,     -1,     -1),
	PINGROUP(pk4,                  IQC1,       RSVD1,  RSVD2, RSVD3, 0x3264, Y,   Y,       N,       -1,    -1,      -1,      -1,      -1,      -1,     -1,     -1,     -1),
	PINGROUP(pk5,                  IQC1,       RSVD1,  RSVD2, RSVD3, 0x3268, Y,   Y,       N,       -1,    -1,      -1,      -1,      -1,      -1,     -1,     -1,     -1),
	PINGROUP(pk6,                  IQC1,       RSVD1,  RSVD2, RSVD3, 0x326c, Y,   Y,       N,       -1,    -1,      -1,      -1,      -1,      -1,     -1,     -1,     -1),
	PINGROUP(pk7,                  IQC1,       RSVD1,  RSVD2, RSVD3, 0x3270, Y,   Y,       N,       -1,    -1,      -1,      -1,      -1,      -1,     -1,     -1,     -1),
	PINGROUP(pl0,                  RSVD0,      RSVD1,  RSVD2, RSVD3, 0x3274, Y,   Y,       N,       -1,    -1,      -1,      -1,      -1,      -1,     -1,     -1,     -1),
	PINGROUP(pl1,                  SOC,        RSVD1,  RSVD2, RSVD3, 0x3278, Y,   Y,       N,       -1,    -1,      -1,      -1,      -1,      -1,     -1,     -1,     -1),
	PINGROUP(pz0,                  VIMCLK2,    RSVD1,  RSVD2, RSVD3, 0x327c, N,   N,       N,       -1,    -1,      -1,      -1,      -1,      -1,     -1,     -1,     -1),
	PINGROUP(pz1,                  VIMCLK2,    SDMMC1, RSVD2, RSVD3, 0x3280, N,   N,       N,       -1,    -1,      -1,      -1,      -1,      -1,     -1,     -1,     -1),
	PINGROUP(pz2,                  SDMMC3,     CCLA,   RSVD2, RSVD3, 0x3284, N,   N,       N,       -1,    -1,      -1,      -1,      -1,      -1,     -1,     -1,     -1),
	PINGROUP(pz3,                  SDMMC3,     RSVD1,  RSVD2, RSVD3, 0x3288, N,   N,       N,       -1,    -1,      -1,      -1,      -1,      -1,     -1,     -1,     -1),
	PINGROUP(pz4,                  SDMMC1,     RSVD1,  RSVD2, RSVD3, 0x328c, N,   N,       N,       -1,    -1,      -1,      -1,      -1,      -1,     -1,     -1,     -1),
	PINGROUP(pz5,                  SOC,        RSVD1,  RSVD2, RSVD3, 0x3290, N,   N,       N,       -1,    -1,      -1,      -1,      -1,      -1,     -1,     -1,     -1),

	/* pg_name, r, prk_mask, drvdn_b, drvdn_w, drvup_b, drvup_w, slwr_b, slwr_w, slwf_b, slwf_w */
	DRV_PINGROUP(pa6,    0x9c0, 0x0,       12, 5,  20, 5,  -1, -1, -1, -1),
	DRV_PINGROUP(pcc7,   0x9c4, 0x0,       12, 5,  20, 5,  -1, -1, -1, -1),
	DRV_PINGROUP(pe6,    0x9c8, 0x0,       12, 5,  20, 5,  -1, -1, -1, -1),
	DRV_PINGROUP(pe7,    0x9cc, 0x0,       12, 5,  20, 5,  -1, -1, -1, -1),
	DRV_PINGROUP(ph6,    0x9d0, 0x0,       12, 5,  20, 5,  -1, -1, -1, -1),
	DRV_PINGROUP(pk0,    0x9d4, 0x0,       -1, -1, -1, -1, 28, 2,  30, 2),
	DRV_PINGROUP(pk1,    0x9d8, 0x0,       -1, -1, -1, -1, 28, 2,  30, 2),
	DRV_PINGROUP(pk2,    0x9dc, 0x0,       -1, -1, -1, -1, 28, 2,  30, 2),
	DRV_PINGROUP(pk3,    0x9e0, 0x0,       -1, -1, -1, -1, 28, 2,  30, 2),
	DRV_PINGROUP(pk4,    0x9e4, 0x0,       -1, -1, -1, -1, 28, 2,  30, 2),
	DRV_PINGROUP(pk5,    0x9e8, 0x0,       -1, -1, -1, -1, 28, 2,  30, 2),
	DRV_PINGROUP(pk6,    0x9ec, 0x0,       -1, -1, -1, -1, 28, 2,  30, 2),
	DRV_PINGROUP(pk7,    0x9f0, 0x0,       -1, -1, -1, -1, 28, 2,  30, 2),
	DRV_PINGROUP(pl0,    0x9f4, 0x0,       -1, -1, -1, -1, 28, 2,  30, 2),
	DRV_PINGROUP(pl1,    0x9f8, 0x0,       -1, -1, -1, -1, 28, 2,  30, 2),
	DRV_PINGROUP(pz0,    0x9fc, 0x0,       12, 7,  20, 7,  -1, -1, -1, -1),
	DRV_PINGROUP(pz1,    0xa00, 0x0,       12, 7,  20, 7,  -1, -1, -1, -1),
	DRV_PINGROUP(pz2,    0xa04, 0x0,       12, 7,  20, 7,  -1, -1, -1, -1),
	DRV_PINGROUP(pz3,    0xa08, 0x0,       12, 7,  20, 7,  -1, -1, -1, -1),
	DRV_PINGROUP(pz4,    0xa0c, 0x0,       12, 7,  20, 7,  -1, -1, -1, -1),
	DRV_PINGROUP(pz5,    0xa10, 0x0,       12, 7,  20, 7,  -1, -1, -1, -1),
	DRV_PINGROUP(sdmmc1, 0xa98, 0x0,       12, 7,  20, 7,  28, 2,  30, 2),
	DRV_PINGROUP(sdmmc2, 0xa9c, 0x7ffc000, 2,  6,  8,  6,  28, 2,  30, 2),
	DRV_PINGROUP(sdmmc3, 0xab0, 0x0,       12, 7,  20, 7,  28, 2,  30, 2),
	DRV_PINGROUP(sdmmc4, 0xab4, 0x7ffc000, 2,  6,  8,  6,  28, 2,  30, 2),
};

static const struct tegra_pinctrl_soc_data tegra210_pinctrl = {
	.ngpios = NUM_GPIOS,
	.gpio_compatible = "nvidia,tegra210-gpio",
	.pins = tegra210_pins,
	.npins = ARRAY_SIZE(tegra210_pins),
	.functions = tegra210_functions,
	.nfunctions = ARRAY_SIZE(tegra210_functions),
	.groups = tegra210_groups,
	.ngroups = ARRAY_SIZE(tegra210_groups),
	.hsm_in_mux = true,
	.schmitt_in_mux = true,
	.drvtype_in_mux = true,
};

static int tegra210_pinctrl_probe(struct platform_device *pdev)
{
	return tegra_pinctrl_probe(pdev, &tegra210_pinctrl);
}

static const struct of_device_id tegra210_pinctrl_of_match[] = {
	{ .compatible = "nvidia,tegra210-pinmux", },
	{ },
};

static struct platform_driver tegra210_pinctrl_driver = {
	.driver = {
		.name = "tegra210-pinctrl",
		.of_match_table = tegra210_pinctrl_of_match,
	},
	.probe = tegra210_pinctrl_probe,
};

static int __init tegra210_pinctrl_init(void)
{
	return platform_driver_register(&tegra210_pinctrl_driver);
}
arch_initcall(tegra210_pinctrl_init);
