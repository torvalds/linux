// SPDX-License-Identifier: GPL-2.0+
/*
 * Pinctrl data for the NVIDIA Tegra194 pinmux
 *
 * Copyright (c) 2019-2021, NVIDIA CORPORATION.  All rights reserved.
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

/* Define unique ID for each pins */
enum pin_id {
	TEGRA_PIN_DAP6_SCLK_PA0,
	TEGRA_PIN_DAP6_DOUT_PA1,
	TEGRA_PIN_DAP6_DIN_PA2,
	TEGRA_PIN_DAP6_FS_PA3,
	TEGRA_PIN_DAP4_SCLK_PA4,
	TEGRA_PIN_DAP4_DOUT_PA5,
	TEGRA_PIN_DAP4_DIN_PA6,
	TEGRA_PIN_DAP4_FS_PA7,
	TEGRA_PIN_CPU_PWR_REQ_0_PB0,
	TEGRA_PIN_CPU_PWR_REQ_1_PB1,
	TEGRA_PIN_QSPI0_SCK_PC0,
	TEGRA_PIN_QSPI0_CS_N_PC1,
	TEGRA_PIN_QSPI0_IO0_PC2,
	TEGRA_PIN_QSPI0_IO1_PC3,
	TEGRA_PIN_QSPI0_IO2_PC4,
	TEGRA_PIN_QSPI0_IO3_PC5,
	TEGRA_PIN_QSPI1_SCK_PC6,
	TEGRA_PIN_QSPI1_CS_N_PC7,
	TEGRA_PIN_QSPI1_IO0_PD0,
	TEGRA_PIN_QSPI1_IO1_PD1,
	TEGRA_PIN_QSPI1_IO2_PD2,
	TEGRA_PIN_QSPI1_IO3_PD3,
	TEGRA_PIN_EQOS_TXC_PE0,
	TEGRA_PIN_EQOS_TD0_PE1,
	TEGRA_PIN_EQOS_TD1_PE2,
	TEGRA_PIN_EQOS_TD2_PE3,
	TEGRA_PIN_EQOS_TD3_PE4,
	TEGRA_PIN_EQOS_TX_CTL_PE5,
	TEGRA_PIN_EQOS_RD0_PE6,
	TEGRA_PIN_EQOS_RD1_PE7,
	TEGRA_PIN_EQOS_RD2_PF0,
	TEGRA_PIN_EQOS_RD3_PF1,
	TEGRA_PIN_EQOS_RX_CTL_PF2,
	TEGRA_PIN_EQOS_RXC_PF3,
	TEGRA_PIN_EQOS_SMA_MDIO_PF4,
	TEGRA_PIN_EQOS_SMA_MDC_PF5,
	TEGRA_PIN_SOC_GPIO00_PG0,
	TEGRA_PIN_SOC_GPIO01_PG1,
	TEGRA_PIN_SOC_GPIO02_PG2,
	TEGRA_PIN_SOC_GPIO03_PG3,
	TEGRA_PIN_SOC_GPIO08_PG4,
	TEGRA_PIN_SOC_GPIO09_PG5,
	TEGRA_PIN_SOC_GPIO10_PG6,
	TEGRA_PIN_SOC_GPIO11_PG7,
	TEGRA_PIN_SOC_GPIO12_PH0,
	TEGRA_PIN_SOC_GPIO13_PH1,
	TEGRA_PIN_SOC_GPIO14_PH2,
	TEGRA_PIN_UART4_TX_PH3,
	TEGRA_PIN_UART4_RX_PH4,
	TEGRA_PIN_UART4_RTS_PH5,
	TEGRA_PIN_UART4_CTS_PH6,
	TEGRA_PIN_DAP2_SCLK_PH7,
	TEGRA_PIN_DAP2_DOUT_PI0,
	TEGRA_PIN_DAP2_DIN_PI1,
	TEGRA_PIN_DAP2_FS_PI2,
	TEGRA_PIN_GEN1_I2C_SCL_PI3,
	TEGRA_PIN_GEN1_I2C_SDA_PI4,
	TEGRA_PIN_SDMMC1_CLK_PJ0,
	TEGRA_PIN_SDMMC1_CMD_PJ1,
	TEGRA_PIN_SDMMC1_DAT0_PJ2,
	TEGRA_PIN_SDMMC1_DAT1_PJ3,
	TEGRA_PIN_SDMMC1_DAT2_PJ4,
	TEGRA_PIN_SDMMC1_DAT3_PJ5,
	TEGRA_PIN_PEX_L0_CLKREQ_N_PK0,
	TEGRA_PIN_PEX_L0_RST_N_PK1,
	TEGRA_PIN_PEX_L1_CLKREQ_N_PK2,
	TEGRA_PIN_PEX_L1_RST_N_PK3,
	TEGRA_PIN_PEX_L2_CLKREQ_N_PK4,
	TEGRA_PIN_PEX_L2_RST_N_PK5,
	TEGRA_PIN_PEX_L3_CLKREQ_N_PK6,
	TEGRA_PIN_PEX_L3_RST_N_PK7,
	TEGRA_PIN_PEX_L4_CLKREQ_N_PL0,
	TEGRA_PIN_PEX_L4_RST_N_PL1,
	TEGRA_PIN_PEX_WAKE_N_PL2,
	TEGRA_PIN_SATA_DEV_SLP_PL3,
	TEGRA_PIN_DP_AUX_CH0_HPD_PM0,
	TEGRA_PIN_DP_AUX_CH1_HPD_PM1,
	TEGRA_PIN_DP_AUX_CH2_HPD_PM2,
	TEGRA_PIN_DP_AUX_CH3_HPD_PM3,
	TEGRA_PIN_HDMI_CEC_PM4,
	TEGRA_PIN_SOC_GPIO50_PM5,
	TEGRA_PIN_SOC_GPIO51_PM6,
	TEGRA_PIN_SOC_GPIO52_PM7,
	TEGRA_PIN_SOC_GPIO53_PN0,
	TEGRA_PIN_SOC_GPIO54_PN1,
	TEGRA_PIN_SOC_GPIO55_PN2,
	TEGRA_PIN_SDMMC3_CLK_PO0,
	TEGRA_PIN_SDMMC3_CMD_PO1,
	TEGRA_PIN_SDMMC3_DAT0_PO2,
	TEGRA_PIN_SDMMC3_DAT1_PO3,
	TEGRA_PIN_SDMMC3_DAT2_PO4,
	TEGRA_PIN_SDMMC3_DAT3_PO5,
	TEGRA_PIN_EXTPERIPH1_CLK_PP0,
	TEGRA_PIN_EXTPERIPH2_CLK_PP1,
	TEGRA_PIN_CAM_I2C_SCL_PP2,
	TEGRA_PIN_CAM_I2C_SDA_PP3,
	TEGRA_PIN_SOC_GPIO04_PP4,
	TEGRA_PIN_SOC_GPIO05_PP5,
	TEGRA_PIN_SOC_GPIO06_PP6,
	TEGRA_PIN_SOC_GPIO07_PP7,
	TEGRA_PIN_SOC_GPIO20_PQ0,
	TEGRA_PIN_SOC_GPIO21_PQ1,
	TEGRA_PIN_SOC_GPIO22_PQ2,
	TEGRA_PIN_SOC_GPIO23_PQ3,
	TEGRA_PIN_SOC_GPIO40_PQ4,
	TEGRA_PIN_SOC_GPIO41_PQ5,
	TEGRA_PIN_SOC_GPIO42_PQ6,
	TEGRA_PIN_SOC_GPIO43_PQ7,
	TEGRA_PIN_SOC_GPIO44_PR0,
	TEGRA_PIN_SOC_GPIO45_PR1,
	TEGRA_PIN_UART1_TX_PR2,
	TEGRA_PIN_UART1_RX_PR3,
	TEGRA_PIN_UART1_RTS_PR4,
	TEGRA_PIN_UART1_CTS_PR5,
	TEGRA_PIN_DAP1_SCLK_PS0,
	TEGRA_PIN_DAP1_DOUT_PS1,
	TEGRA_PIN_DAP1_DIN_PS2,
	TEGRA_PIN_DAP1_FS_PS3,
	TEGRA_PIN_AUD_MCLK_PS4,
	TEGRA_PIN_SOC_GPIO30_PS5,
	TEGRA_PIN_SOC_GPIO31_PS6,
	TEGRA_PIN_SOC_GPIO32_PS7,
	TEGRA_PIN_SOC_GPIO33_PT0,
	TEGRA_PIN_DAP3_SCLK_PT1,
	TEGRA_PIN_DAP3_DOUT_PT2,
	TEGRA_PIN_DAP3_DIN_PT3,
	TEGRA_PIN_DAP3_FS_PT4,
	TEGRA_PIN_DAP5_SCLK_PT5,
	TEGRA_PIN_DAP5_DOUT_PT6,
	TEGRA_PIN_DAP5_DIN_PT7,
	TEGRA_PIN_DAP5_FS_PU0,
	TEGRA_PIN_DIRECTDC1_CLK_PV0,
	TEGRA_PIN_DIRECTDC1_IN_PV1,
	TEGRA_PIN_DIRECTDC1_OUT0_PV2,
	TEGRA_PIN_DIRECTDC1_OUT1_PV3,
	TEGRA_PIN_DIRECTDC1_OUT2_PV4,
	TEGRA_PIN_DIRECTDC1_OUT3_PV5,
	TEGRA_PIN_DIRECTDC1_OUT4_PV6,
	TEGRA_PIN_DIRECTDC1_OUT5_PV7,
	TEGRA_PIN_DIRECTDC1_OUT6_PW0,
	TEGRA_PIN_DIRECTDC1_OUT7_PW1,
	TEGRA_PIN_GPU_PWR_REQ_PX0,
	TEGRA_PIN_CV_PWR_REQ_PX1,
	TEGRA_PIN_GP_PWM2_PX2,
	TEGRA_PIN_GP_PWM3_PX3,
	TEGRA_PIN_UART2_TX_PX4,
	TEGRA_PIN_UART2_RX_PX5,
	TEGRA_PIN_UART2_RTS_PX6,
	TEGRA_PIN_UART2_CTS_PX7,
	TEGRA_PIN_SPI3_SCK_PY0,
	TEGRA_PIN_SPI3_MISO_PY1,
	TEGRA_PIN_SPI3_MOSI_PY2,
	TEGRA_PIN_SPI3_CS0_PY3,
	TEGRA_PIN_SPI3_CS1_PY4,
	TEGRA_PIN_UART5_TX_PY5,
	TEGRA_PIN_UART5_RX_PY6,
	TEGRA_PIN_UART5_RTS_PY7,
	TEGRA_PIN_UART5_CTS_PZ0,
	TEGRA_PIN_USB_VBUS_EN0_PZ1,
	TEGRA_PIN_USB_VBUS_EN1_PZ2,
	TEGRA_PIN_SPI1_SCK_PZ3,
	TEGRA_PIN_SPI1_MISO_PZ4,
	TEGRA_PIN_SPI1_MOSI_PZ5,
	TEGRA_PIN_SPI1_CS0_PZ6,
	TEGRA_PIN_SPI1_CS1_PZ7,
	TEGRA_PIN_CAN1_DOUT_PAA0,
	TEGRA_PIN_CAN1_DIN_PAA1,
	TEGRA_PIN_CAN0_DOUT_PAA2,
	TEGRA_PIN_CAN0_DIN_PAA3,
	TEGRA_PIN_CAN0_STB_PAA4,
	TEGRA_PIN_CAN0_EN_PAA5,
	TEGRA_PIN_CAN0_WAKE_PAA6,
	TEGRA_PIN_CAN0_ERR_PAA7,
	TEGRA_PIN_CAN1_STB_PBB0,
	TEGRA_PIN_CAN1_EN_PBB1,
	TEGRA_PIN_CAN1_WAKE_PBB2,
	TEGRA_PIN_CAN1_ERR_PBB3,
	TEGRA_PIN_SPI2_SCK_PCC0,
	TEGRA_PIN_SPI2_MISO_PCC1,
	TEGRA_PIN_SPI2_MOSI_PCC2,
	TEGRA_PIN_SPI2_CS0_PCC3,
	TEGRA_PIN_TOUCH_CLK_PCC4,
	TEGRA_PIN_UART3_TX_PCC5,
	TEGRA_PIN_UART3_RX_PCC6,
	TEGRA_PIN_GEN2_I2C_SCL_PCC7,
	TEGRA_PIN_GEN2_I2C_SDA_PDD0,
	TEGRA_PIN_GEN8_I2C_SCL_PDD1,
	TEGRA_PIN_GEN8_I2C_SDA_PDD2,
	TEGRA_PIN_SAFE_STATE_PEE0,
	TEGRA_PIN_VCOMP_ALERT_PEE1,
	TEGRA_PIN_AO_RETENTION_N_PEE2,
	TEGRA_PIN_BATT_OC_PEE3,
	TEGRA_PIN_POWER_ON_PEE4,
	TEGRA_PIN_PWR_I2C_SCL_PEE5,
	TEGRA_PIN_PWR_I2C_SDA_PEE6,
	TEGRA_PIN_UFS0_REF_CLK_PFF0,
	TEGRA_PIN_UFS0_RST_PFF1,
	TEGRA_PIN_PEX_L5_CLKREQ_N_PGG0,
	TEGRA_PIN_PEX_L5_RST_N_PGG1,
	TEGRA_PIN_DIRECTDC_COMP,
	TEGRA_PIN_SDMMC4_CLK,
	TEGRA_PIN_SDMMC4_CMD,
	TEGRA_PIN_SDMMC4_DQS,
	TEGRA_PIN_SDMMC4_DAT7,
	TEGRA_PIN_SDMMC4_DAT6,
	TEGRA_PIN_SDMMC4_DAT5,
	TEGRA_PIN_SDMMC4_DAT4,
	TEGRA_PIN_SDMMC4_DAT3,
	TEGRA_PIN_SDMMC4_DAT2,
	TEGRA_PIN_SDMMC4_DAT1,
	TEGRA_PIN_SDMMC4_DAT0,
	TEGRA_PIN_SDMMC1_COMP,
	TEGRA_PIN_SDMMC1_HV_TRIM,
	TEGRA_PIN_SDMMC3_COMP,
	TEGRA_PIN_SDMMC3_HV_TRIM,
	TEGRA_PIN_EQOS_COMP,
	TEGRA_PIN_QSPI_COMP,
	TEGRA_PIN_SYS_RESET_N,
	TEGRA_PIN_SHUTDOWN_N,
	TEGRA_PIN_PMU_INT_N,
	TEGRA_PIN_SOC_PWR_REQ,
	TEGRA_PIN_CLK_32K_IN,
};

/* Table for pin descriptor */
static const struct pinctrl_pin_desc tegra194_pins[] = {
	PINCTRL_PIN(TEGRA_PIN_DAP6_SCLK_PA0, "DAP6_SCLK_PA0"),
	PINCTRL_PIN(TEGRA_PIN_DAP6_DOUT_PA1, "DAP6_DOUT_PA1"),
	PINCTRL_PIN(TEGRA_PIN_DAP6_DIN_PA2, "DAP6_DIN_PA2"),
	PINCTRL_PIN(TEGRA_PIN_DAP6_FS_PA3, "DAP6_FS_PA3"),
	PINCTRL_PIN(TEGRA_PIN_DAP4_SCLK_PA4, "DAP4_SCLK_PA4"),
	PINCTRL_PIN(TEGRA_PIN_DAP4_DOUT_PA5, "DAP4_DOUT_PA5"),
	PINCTRL_PIN(TEGRA_PIN_DAP4_DIN_PA6, "DAP4_DIN_PA6"),
	PINCTRL_PIN(TEGRA_PIN_DAP4_FS_PA7, "DAP4_FS_PA7"),
	PINCTRL_PIN(TEGRA_PIN_CPU_PWR_REQ_0_PB0, "CPU_PWR_REQ_0_PB0"),
	PINCTRL_PIN(TEGRA_PIN_CPU_PWR_REQ_1_PB1, "CPU_PWR_REQ_1_PB1"),
	PINCTRL_PIN(TEGRA_PIN_QSPI0_SCK_PC0, "QSPI0_SCK_PC0"),
	PINCTRL_PIN(TEGRA_PIN_QSPI0_CS_N_PC1, "QSPI0_CS_N_PC1"),
	PINCTRL_PIN(TEGRA_PIN_QSPI0_IO0_PC2, "QSPI0_IO0_PC2"),
	PINCTRL_PIN(TEGRA_PIN_QSPI0_IO1_PC3, "QSPI0_IO1_PC3"),
	PINCTRL_PIN(TEGRA_PIN_QSPI0_IO2_PC4, "QSPI0_IO2_PC4"),
	PINCTRL_PIN(TEGRA_PIN_QSPI0_IO3_PC5, "QSPI0_IO3_PC5"),
	PINCTRL_PIN(TEGRA_PIN_QSPI1_SCK_PC6, "QSPI1_SCK_PC6"),
	PINCTRL_PIN(TEGRA_PIN_QSPI1_CS_N_PC7, "QSPI1_CS_N_PC7"),
	PINCTRL_PIN(TEGRA_PIN_QSPI1_IO0_PD0, "QSPI1_IO0_PD0"),
	PINCTRL_PIN(TEGRA_PIN_QSPI1_IO1_PD1, "QSPI1_IO1_PD1"),
	PINCTRL_PIN(TEGRA_PIN_QSPI1_IO2_PD2, "QSPI1_IO2_PD2"),
	PINCTRL_PIN(TEGRA_PIN_QSPI1_IO3_PD3, "QSPI1_IO3_PD3"),
	PINCTRL_PIN(TEGRA_PIN_EQOS_TXC_PE0, "EQOS_TXC_PE0"),
	PINCTRL_PIN(TEGRA_PIN_EQOS_TD0_PE1, "EQOS_TD0_PE1"),
	PINCTRL_PIN(TEGRA_PIN_EQOS_TD1_PE2, "EQOS_TD1_PE2"),
	PINCTRL_PIN(TEGRA_PIN_EQOS_TD2_PE3, "EQOS_TD2_PE3"),
	PINCTRL_PIN(TEGRA_PIN_EQOS_TD3_PE4, "EQOS_TD3_PE4"),
	PINCTRL_PIN(TEGRA_PIN_EQOS_TX_CTL_PE5, "EQOS_TX_CTL_PE5"),
	PINCTRL_PIN(TEGRA_PIN_EQOS_RD0_PE6, "EQOS_RD0_PE6"),
	PINCTRL_PIN(TEGRA_PIN_EQOS_RD1_PE7, "EQOS_RD1_PE7"),
	PINCTRL_PIN(TEGRA_PIN_EQOS_RD2_PF0, "EQOS_RD2_PF0"),
	PINCTRL_PIN(TEGRA_PIN_EQOS_RD3_PF1, "EQOS_RD3_PF1"),
	PINCTRL_PIN(TEGRA_PIN_EQOS_RX_CTL_PF2, "EQOS_RX_CTL_PF2"),
	PINCTRL_PIN(TEGRA_PIN_EQOS_RXC_PF3, "EQOS_RXC_PF3"),
	PINCTRL_PIN(TEGRA_PIN_EQOS_SMA_MDIO_PF4, "EQOS_SMA_MDIO_PF4"),
	PINCTRL_PIN(TEGRA_PIN_EQOS_SMA_MDC_PF5, "EQOS_SMA_MDC_PF5"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO00_PG0, "SOC_GPIO00_PG0"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO01_PG1, "SOC_GPIO01_PG1"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO02_PG2, "SOC_GPIO02_PG2"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO03_PG3, "SOC_GPIO03_PG3"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO08_PG4, "SOC_GPIO08_PG4"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO09_PG5, "SOC_GPIO09_PG5"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO10_PG6, "SOC_GPIO10_PG6"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO11_PG7, "SOC_GPIO11_PG7"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO12_PH0, "SOC_GPIO12_PH0"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO13_PH1, "SOC_GPIO13_PH1"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO14_PH2, "SOC_GPIO14_PH2"),
	PINCTRL_PIN(TEGRA_PIN_UART4_TX_PH3, "UART4_TX_PH3"),
	PINCTRL_PIN(TEGRA_PIN_UART4_RX_PH4, "UART4_RX_PH4"),
	PINCTRL_PIN(TEGRA_PIN_UART4_RTS_PH5, "UART4_RTS_PH5"),
	PINCTRL_PIN(TEGRA_PIN_UART4_CTS_PH6, "UART4_CTS_PH6"),
	PINCTRL_PIN(TEGRA_PIN_DAP2_SCLK_PH7, "DAP2_SCLK_PH7"),
	PINCTRL_PIN(TEGRA_PIN_DAP2_DOUT_PI0, "DAP2_DOUT_PI0"),
	PINCTRL_PIN(TEGRA_PIN_DAP2_DIN_PI1, "DAP2_DIN_PI1"),
	PINCTRL_PIN(TEGRA_PIN_DAP2_FS_PI2, "DAP2_FS_PI2"),
	PINCTRL_PIN(TEGRA_PIN_GEN1_I2C_SCL_PI3, "GEN1_I2C_SCL_PI3"),
	PINCTRL_PIN(TEGRA_PIN_GEN1_I2C_SDA_PI4, "GEN1_I2C_SDA_PI4"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC1_CLK_PJ0, "SDMMC1_CLK_PJ0"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC1_CMD_PJ1, "SDMMC1_CMD_PJ1"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC1_DAT0_PJ2, "SDMMC1_DAT0_PJ2"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC1_DAT1_PJ3, "SDMMC1_DAT1_PJ3"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC1_DAT2_PJ4, "SDMMC1_DAT2_PJ4"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC1_DAT3_PJ5, "SDMMC1_DAT3_PJ5"),
	PINCTRL_PIN(TEGRA_PIN_PEX_L0_CLKREQ_N_PK0, "PEX_L0_CLKREQ_N_PK0"),
	PINCTRL_PIN(TEGRA_PIN_PEX_L0_RST_N_PK1, "PEX_L0_RST_N_PK1"),
	PINCTRL_PIN(TEGRA_PIN_PEX_L1_CLKREQ_N_PK2, "PEX_L1_CLKREQ_N_PK2"),
	PINCTRL_PIN(TEGRA_PIN_PEX_L1_RST_N_PK3, "PEX_L1_RST_N_PK3"),
	PINCTRL_PIN(TEGRA_PIN_PEX_L2_CLKREQ_N_PK4, "PEX_L2_CLKREQ_N_PK4"),
	PINCTRL_PIN(TEGRA_PIN_PEX_L2_RST_N_PK5, "PEX_L2_RST_N_PK5"),
	PINCTRL_PIN(TEGRA_PIN_PEX_L3_CLKREQ_N_PK6, "PEX_L3_CLKREQ_N_PK6"),
	PINCTRL_PIN(TEGRA_PIN_PEX_L3_RST_N_PK7, "PEX_L3_RST_N_PK7"),
	PINCTRL_PIN(TEGRA_PIN_PEX_L4_CLKREQ_N_PL0, "PEX_L4_CLKREQ_N_PL0"),
	PINCTRL_PIN(TEGRA_PIN_PEX_L4_RST_N_PL1, "PEX_L4_RST_N_PL1"),
	PINCTRL_PIN(TEGRA_PIN_PEX_WAKE_N_PL2, "PEX_WAKE_N_PL2"),
	PINCTRL_PIN(TEGRA_PIN_SATA_DEV_SLP_PL3, "SATA_DEV_SLP_PL3"),
	PINCTRL_PIN(TEGRA_PIN_DP_AUX_CH0_HPD_PM0, "DP_AUX_CH0_HPD_PM0"),
	PINCTRL_PIN(TEGRA_PIN_DP_AUX_CH1_HPD_PM1, "DP_AUX_CH1_HPD_PM1"),
	PINCTRL_PIN(TEGRA_PIN_DP_AUX_CH2_HPD_PM2, "DP_AUX_CH2_HPD_PM2"),
	PINCTRL_PIN(TEGRA_PIN_DP_AUX_CH3_HPD_PM3, "DP_AUX_CH3_HPD_PM3"),
	PINCTRL_PIN(TEGRA_PIN_HDMI_CEC_PM4, "HDMI_CEC_PM4"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO50_PM5, "SOC_GPIO50_PM5"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO51_PM6, "SOC_GPIO51_PM6"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO52_PM7, "SOC_GPIO52_PM7"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO53_PN0, "SOC_GPIO53_PN0"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO54_PN1, "SOC_GPIO54_PN1"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO55_PN2, "SOC_GPIO55_PN2"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC3_CLK_PO0, "SDMMC3_CLK_PO0"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC3_CMD_PO1, "SDMMC3_CMD_PO1"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC3_DAT0_PO2, "SDMMC3_DAT0_PO2"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC3_DAT1_PO3, "SDMMC3_DAT1_PO3"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC3_DAT2_PO4, "SDMMC3_DAT2_PO4"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC3_DAT3_PO5, "SDMMC3_DAT3_PO5"),
	PINCTRL_PIN(TEGRA_PIN_EXTPERIPH1_CLK_PP0, "EXTPERIPH1_CLK_PP0"),
	PINCTRL_PIN(TEGRA_PIN_EXTPERIPH2_CLK_PP1, "EXTPERIPH2_CLK_PP1"),
	PINCTRL_PIN(TEGRA_PIN_CAM_I2C_SCL_PP2, "CAM_I2C_SCL_PP2"),
	PINCTRL_PIN(TEGRA_PIN_CAM_I2C_SDA_PP3, "CAM_I2C_SDA_PP3"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO04_PP4, "SOC_GPIO04_PP4"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO05_PP5, "SOC_GPIO05_PP5"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO06_PP6, "SOC_GPIO06_PP6"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO07_PP7, "SOC_GPIO07_PP7"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO20_PQ0, "SOC_GPIO20_PQ0"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO21_PQ1, "SOC_GPIO21_PQ1"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO22_PQ2, "SOC_GPIO22_PQ2"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO23_PQ3, "SOC_GPIO23_PQ3"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO40_PQ4, "SOC_GPIO40_PQ4"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO41_PQ5, "SOC_GPIO41_PQ5"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO42_PQ6, "SOC_GPIO42_PQ6"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO43_PQ7, "SOC_GPIO43_PQ7"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO44_PR0, "SOC_GPIO44_PR0"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO45_PR1, "SOC_GPIO45_PR1"),
	PINCTRL_PIN(TEGRA_PIN_UART1_TX_PR2, "UART1_TX_PR2"),
	PINCTRL_PIN(TEGRA_PIN_UART1_RX_PR3, "UART1_RX_PR3"),
	PINCTRL_PIN(TEGRA_PIN_UART1_RTS_PR4, "UART1_RTS_PR4"),
	PINCTRL_PIN(TEGRA_PIN_UART1_CTS_PR5, "UART1_CTS_PR5"),
	PINCTRL_PIN(TEGRA_PIN_DAP1_SCLK_PS0, "DAP1_SCLK_PS0"),
	PINCTRL_PIN(TEGRA_PIN_DAP1_DOUT_PS1, "DAP1_DOUT_PS1"),
	PINCTRL_PIN(TEGRA_PIN_DAP1_DIN_PS2, "DAP1_DIN_PS2"),
	PINCTRL_PIN(TEGRA_PIN_DAP1_FS_PS3, "DAP1_FS_PS3"),
	PINCTRL_PIN(TEGRA_PIN_AUD_MCLK_PS4, "AUD_MCLK_PS4"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO30_PS5, "SOC_GPIO30_PS5"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO31_PS6, "SOC_GPIO31_PS6"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO32_PS7, "SOC_GPIO32_PS7"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO33_PT0, "SOC_GPIO33_PT0"),
	PINCTRL_PIN(TEGRA_PIN_DAP3_SCLK_PT1, "DAP3_SCLK_PT1"),
	PINCTRL_PIN(TEGRA_PIN_DAP3_DOUT_PT2, "DAP3_DOUT_PT2"),
	PINCTRL_PIN(TEGRA_PIN_DAP3_DIN_PT3, "DAP3_DIN_PT3"),
	PINCTRL_PIN(TEGRA_PIN_DAP3_FS_PT4, "DAP3_FS_PT4"),
	PINCTRL_PIN(TEGRA_PIN_DAP5_SCLK_PT5, "DAP5_SCLK_PT5"),
	PINCTRL_PIN(TEGRA_PIN_DAP5_DOUT_PT6, "DAP5_DOUT_PT6"),
	PINCTRL_PIN(TEGRA_PIN_DAP5_DIN_PT7, "DAP5_DIN_PT7"),
	PINCTRL_PIN(TEGRA_PIN_DAP5_FS_PU0, "DAP5_FS_PU0"),
	PINCTRL_PIN(TEGRA_PIN_DIRECTDC1_CLK_PV0, "DIRECTDC1_CLK_PV0"),
	PINCTRL_PIN(TEGRA_PIN_DIRECTDC1_IN_PV1, "DIRECTDC1_IN_PV1"),
	PINCTRL_PIN(TEGRA_PIN_DIRECTDC1_OUT0_PV2, "DIRECTDC1_OUT0_PV2"),
	PINCTRL_PIN(TEGRA_PIN_DIRECTDC1_OUT1_PV3, "DIRECTDC1_OUT1_PV3"),
	PINCTRL_PIN(TEGRA_PIN_DIRECTDC1_OUT2_PV4, "DIRECTDC1_OUT2_PV4"),
	PINCTRL_PIN(TEGRA_PIN_DIRECTDC1_OUT3_PV5, "DIRECTDC1_OUT3_PV5"),
	PINCTRL_PIN(TEGRA_PIN_DIRECTDC1_OUT4_PV6, "DIRECTDC1_OUT4_PV6"),
	PINCTRL_PIN(TEGRA_PIN_DIRECTDC1_OUT5_PV7, "DIRECTDC1_OUT5_PV7"),
	PINCTRL_PIN(TEGRA_PIN_DIRECTDC1_OUT6_PW0, "DIRECTDC1_OUT6_PW0"),
	PINCTRL_PIN(TEGRA_PIN_DIRECTDC1_OUT7_PW1, "DIRECTDC1_OUT7_PW1"),
	PINCTRL_PIN(TEGRA_PIN_GPU_PWR_REQ_PX0, "GPU_PWR_REQ_PX0"),
	PINCTRL_PIN(TEGRA_PIN_CV_PWR_REQ_PX1, "CV_PWR_REQ_PX1"),
	PINCTRL_PIN(TEGRA_PIN_GP_PWM2_PX2, "GP_PWM2_PX2"),
	PINCTRL_PIN(TEGRA_PIN_GP_PWM3_PX3, "GP_PWM3_PX3"),
	PINCTRL_PIN(TEGRA_PIN_UART2_TX_PX4, "UART2_TX_PX4"),
	PINCTRL_PIN(TEGRA_PIN_UART2_RX_PX5, "UART2_RX_PX5"),
	PINCTRL_PIN(TEGRA_PIN_UART2_RTS_PX6, "UART2_RTS_PX6"),
	PINCTRL_PIN(TEGRA_PIN_UART2_CTS_PX7, "UART2_CTS_PX7"),
	PINCTRL_PIN(TEGRA_PIN_SPI3_SCK_PY0, "SPI3_SCK_PY0"),
	PINCTRL_PIN(TEGRA_PIN_SPI3_MISO_PY1, "SPI3_MISO_PY1"),
	PINCTRL_PIN(TEGRA_PIN_SPI3_MOSI_PY2, "SPI3_MOSI_PY2"),
	PINCTRL_PIN(TEGRA_PIN_SPI3_CS0_PY3, "SPI3_CS0_PY3"),
	PINCTRL_PIN(TEGRA_PIN_SPI3_CS1_PY4, "SPI3_CS1_PY4"),
	PINCTRL_PIN(TEGRA_PIN_UART5_TX_PY5, "UART5_TX_PY5"),
	PINCTRL_PIN(TEGRA_PIN_UART5_RX_PY6, "UART5_RX_PY6"),
	PINCTRL_PIN(TEGRA_PIN_UART5_RTS_PY7, "UART5_RTS_PY7"),
	PINCTRL_PIN(TEGRA_PIN_UART5_CTS_PZ0, "UART5_CTS_PZ0"),
	PINCTRL_PIN(TEGRA_PIN_USB_VBUS_EN0_PZ1, "USB_VBUS_EN0_PZ1"),
	PINCTRL_PIN(TEGRA_PIN_USB_VBUS_EN1_PZ2, "USB_VBUS_EN1_PZ2"),
	PINCTRL_PIN(TEGRA_PIN_SPI1_SCK_PZ3, "SPI1_SCK_PZ3"),
	PINCTRL_PIN(TEGRA_PIN_SPI1_MISO_PZ4, "SPI1_MISO_PZ4"),
	PINCTRL_PIN(TEGRA_PIN_SPI1_MOSI_PZ5, "SPI1_MOSI_PZ5"),
	PINCTRL_PIN(TEGRA_PIN_SPI1_CS0_PZ6, "SPI1_CS0_PZ6"),
	PINCTRL_PIN(TEGRA_PIN_SPI1_CS1_PZ7, "SPI1_CS1_PZ7"),
	PINCTRL_PIN(TEGRA_PIN_CAN1_DOUT_PAA0, "CAN1_DOUT_PAA0"),
	PINCTRL_PIN(TEGRA_PIN_CAN1_DIN_PAA1, "CAN1_DIN_PAA1"),
	PINCTRL_PIN(TEGRA_PIN_CAN0_DOUT_PAA2, "CAN0_DOUT_PAA2"),
	PINCTRL_PIN(TEGRA_PIN_CAN0_DIN_PAA3, "CAN0_DIN_PAA3"),
	PINCTRL_PIN(TEGRA_PIN_CAN0_STB_PAA4, "CAN0_STB_PAA4"),
	PINCTRL_PIN(TEGRA_PIN_CAN0_EN_PAA5, "CAN0_EN_PAA5"),
	PINCTRL_PIN(TEGRA_PIN_CAN0_WAKE_PAA6, "CAN0_WAKE_PAA6"),
	PINCTRL_PIN(TEGRA_PIN_CAN0_ERR_PAA7, "CAN0_ERR_PAA7"),
	PINCTRL_PIN(TEGRA_PIN_CAN1_STB_PBB0, "CAN1_STB_PBB0"),
	PINCTRL_PIN(TEGRA_PIN_CAN1_EN_PBB1, "CAN1_EN_PBB1"),
	PINCTRL_PIN(TEGRA_PIN_CAN1_WAKE_PBB2, "CAN1_WAKE_PBB2"),
	PINCTRL_PIN(TEGRA_PIN_CAN1_ERR_PBB3, "CAN1_ERR_PBB3"),
	PINCTRL_PIN(TEGRA_PIN_SPI2_SCK_PCC0, "SPI2_SCK_PCC0"),
	PINCTRL_PIN(TEGRA_PIN_SPI2_MISO_PCC1, "SPI2_MISO_PCC1"),
	PINCTRL_PIN(TEGRA_PIN_SPI2_MOSI_PCC2, "SPI2_MOSI_PCC2"),
	PINCTRL_PIN(TEGRA_PIN_SPI2_CS0_PCC3, "SPI2_CS0_PCC3"),
	PINCTRL_PIN(TEGRA_PIN_TOUCH_CLK_PCC4, "TOUCH_CLK_PCC4"),
	PINCTRL_PIN(TEGRA_PIN_UART3_TX_PCC5, "UART3_TX_PCC5"),
	PINCTRL_PIN(TEGRA_PIN_UART3_RX_PCC6, "UART3_RX_PCC6"),
	PINCTRL_PIN(TEGRA_PIN_GEN2_I2C_SCL_PCC7, "GEN2_I2C_SCL_PCC7"),
	PINCTRL_PIN(TEGRA_PIN_GEN2_I2C_SDA_PDD0, "GEN2_I2C_SDA_PDD0"),
	PINCTRL_PIN(TEGRA_PIN_GEN8_I2C_SCL_PDD1, "GEN8_I2C_SCL_PDD1"),
	PINCTRL_PIN(TEGRA_PIN_GEN8_I2C_SDA_PDD2, "GEN8_I2C_SDA_PDD2"),
	PINCTRL_PIN(TEGRA_PIN_SAFE_STATE_PEE0, "SAFE_STATE_PEE0"),
	PINCTRL_PIN(TEGRA_PIN_VCOMP_ALERT_PEE1, "VCOMP_ALERT_PEE1"),
	PINCTRL_PIN(TEGRA_PIN_AO_RETENTION_N_PEE2, "AO_RETENTION_N_PEE2"),
	PINCTRL_PIN(TEGRA_PIN_BATT_OC_PEE3, "BATT_OC_PEE3"),
	PINCTRL_PIN(TEGRA_PIN_POWER_ON_PEE4, "POWER_ON_PEE4"),
	PINCTRL_PIN(TEGRA_PIN_PWR_I2C_SCL_PEE5, "PWR_I2C_SCL_PEE5"),
	PINCTRL_PIN(TEGRA_PIN_PWR_I2C_SDA_PEE6, "PWR_I2C_SDA_PEE6"),
	PINCTRL_PIN(TEGRA_PIN_UFS0_REF_CLK_PFF0, "UFS0_REF_CLK_PFF0"),
	PINCTRL_PIN(TEGRA_PIN_UFS0_RST_PFF1, "UFS0_RST_PFF1"),
	PINCTRL_PIN(TEGRA_PIN_PEX_L5_CLKREQ_N_PGG0, "PEX_L5_CLKREQ_N_PGG0"),
	PINCTRL_PIN(TEGRA_PIN_PEX_L5_RST_N_PGG1, "PEX_L5_RST_N_PGG1"),
	PINCTRL_PIN(TEGRA_PIN_DIRECTDC_COMP, "DIRECTDC_COMP"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC4_CLK, "SDMMC4_CLK"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC4_CMD, "SDMMC4_CMD"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC4_DQS, "SDMMC4_DQS"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC4_DAT7, "SDMMC4_DAT7"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC4_DAT6, "SDMMC4_DAT6"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC4_DAT5, "SDMMC4_DAT5"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC4_DAT4, "SDMMC4_DAT4"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC4_DAT3, "SDMMC4_DAT3"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC4_DAT2, "SDMMC4_DAT2"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC4_DAT1, "SDMMC4_DAT1"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC4_DAT0, "SDMMC4_DAT0"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC1_COMP, "SDMMC1_COMP"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC1_HV_TRIM, "SDMMC1_HV_TRIM"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC3_COMP, "SDMMC3_COMP"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC3_HV_TRIM, "SDMMC3_HV_TRIM"),
	PINCTRL_PIN(TEGRA_PIN_EQOS_COMP, "EQOS_COMP"),
	PINCTRL_PIN(TEGRA_PIN_QSPI_COMP, "QSPI_COMP"),
	PINCTRL_PIN(TEGRA_PIN_SYS_RESET_N, "SYS_RESET_N"),
	PINCTRL_PIN(TEGRA_PIN_SHUTDOWN_N, "SHUTDOWN_N"),
	PINCTRL_PIN(TEGRA_PIN_PMU_INT_N, "PMU_INT_N"),
	PINCTRL_PIN(TEGRA_PIN_SOC_PWR_REQ, "SOC_PWR_REQ"),
	PINCTRL_PIN(TEGRA_PIN_CLK_32K_IN, "CLK_32K_IN"),
};

static const unsigned int dap6_sclk_pa0_pins[] = {
	TEGRA_PIN_DAP6_SCLK_PA0,
};
static const unsigned int dap6_dout_pa1_pins[] = {
	TEGRA_PIN_DAP6_DOUT_PA1,
};
static const unsigned int dap6_din_pa2_pins[] = {
	TEGRA_PIN_DAP6_DIN_PA2,
};
static const unsigned int dap6_fs_pa3_pins[] = {
	TEGRA_PIN_DAP6_FS_PA3,
};
static const unsigned int dap4_sclk_pa4_pins[] = {
	TEGRA_PIN_DAP4_SCLK_PA4,
};
static const unsigned int dap4_dout_pa5_pins[] = {
	TEGRA_PIN_DAP4_DOUT_PA5,
};
static const unsigned int dap4_din_pa6_pins[] = {
	TEGRA_PIN_DAP4_DIN_PA6,
};
static const unsigned int dap4_fs_pa7_pins[] = {
	TEGRA_PIN_DAP4_FS_PA7,
};
static const unsigned int cpu_pwr_req_0_pb0_pins[] = {
	TEGRA_PIN_CPU_PWR_REQ_0_PB0,
};
static const unsigned int cpu_pwr_req_1_pb1_pins[] = {
	TEGRA_PIN_CPU_PWR_REQ_1_PB1,
};
static const unsigned int qspi0_sck_pc0_pins[] = {
	TEGRA_PIN_QSPI0_SCK_PC0,
};
static const unsigned int qspi0_cs_n_pc1_pins[] = {
	TEGRA_PIN_QSPI0_CS_N_PC1,
};
static const unsigned int qspi0_io0_pc2_pins[] = {
	TEGRA_PIN_QSPI0_IO0_PC2,
};
static const unsigned int qspi0_io1_pc3_pins[] = {
	TEGRA_PIN_QSPI0_IO1_PC3,
};
static const unsigned int qspi0_io2_pc4_pins[] = {
	TEGRA_PIN_QSPI0_IO2_PC4,
};
static const unsigned int qspi0_io3_pc5_pins[] = {
	TEGRA_PIN_QSPI0_IO3_PC5,
};
static const unsigned int qspi1_sck_pc6_pins[] = {
	TEGRA_PIN_QSPI1_SCK_PC6,
};
static const unsigned int qspi1_cs_n_pc7_pins[] = {
	TEGRA_PIN_QSPI1_CS_N_PC7,
};
static const unsigned int qspi1_io0_pd0_pins[] = {
	TEGRA_PIN_QSPI1_IO0_PD0,
};
static const unsigned int qspi1_io1_pd1_pins[] = {
	TEGRA_PIN_QSPI1_IO1_PD1,
};
static const unsigned int qspi1_io2_pd2_pins[] = {
	TEGRA_PIN_QSPI1_IO2_PD2,
};
static const unsigned int qspi1_io3_pd3_pins[] = {
	TEGRA_PIN_QSPI1_IO3_PD3,
};
static const unsigned int eqos_txc_pe0_pins[] = {
	TEGRA_PIN_EQOS_TXC_PE0,
};
static const unsigned int eqos_td0_pe1_pins[] = {
	TEGRA_PIN_EQOS_TD0_PE1,
};
static const unsigned int eqos_td1_pe2_pins[] = {
	TEGRA_PIN_EQOS_TD1_PE2,
};
static const unsigned int eqos_td2_pe3_pins[] = {
	TEGRA_PIN_EQOS_TD2_PE3,
};
static const unsigned int eqos_td3_pe4_pins[] = {
	TEGRA_PIN_EQOS_TD3_PE4,
};
static const unsigned int eqos_tx_ctl_pe5_pins[] = {
	TEGRA_PIN_EQOS_TX_CTL_PE5,
};
static const unsigned int eqos_rd0_pe6_pins[] = {
	TEGRA_PIN_EQOS_RD0_PE6,
};
static const unsigned int eqos_rd1_pe7_pins[] = {
	TEGRA_PIN_EQOS_RD1_PE7,
};
static const unsigned int eqos_rd2_pf0_pins[] = {
	TEGRA_PIN_EQOS_RD2_PF0,
};
static const unsigned int eqos_rd3_pf1_pins[] = {
	TEGRA_PIN_EQOS_RD3_PF1,
};
static const unsigned int eqos_rx_ctl_pf2_pins[] = {
	TEGRA_PIN_EQOS_RX_CTL_PF2,
};
static const unsigned int eqos_rxc_pf3_pins[] = {
	TEGRA_PIN_EQOS_RXC_PF3,
};
static const unsigned int eqos_sma_mdio_pf4_pins[] = {
	TEGRA_PIN_EQOS_SMA_MDIO_PF4,
};
static const unsigned int eqos_sma_mdc_pf5_pins[] = {
	TEGRA_PIN_EQOS_SMA_MDC_PF5,
};
static const unsigned int soc_gpio00_pg0_pins[] = {
	TEGRA_PIN_SOC_GPIO00_PG0,
};
static const unsigned int soc_gpio01_pg1_pins[] = {
	TEGRA_PIN_SOC_GPIO01_PG1,
};
static const unsigned int soc_gpio02_pg2_pins[] = {
	TEGRA_PIN_SOC_GPIO02_PG2,
};
static const unsigned int soc_gpio03_pg3_pins[] = {
	TEGRA_PIN_SOC_GPIO03_PG3,
};
static const unsigned int soc_gpio08_pg4_pins[] = {
	TEGRA_PIN_SOC_GPIO08_PG4,
};
static const unsigned int soc_gpio09_pg5_pins[] = {
	TEGRA_PIN_SOC_GPIO09_PG5,
};
static const unsigned int soc_gpio10_pg6_pins[] = {
	TEGRA_PIN_SOC_GPIO10_PG6,
};
static const unsigned int soc_gpio11_pg7_pins[] = {
	TEGRA_PIN_SOC_GPIO11_PG7,
};
static const unsigned int soc_gpio12_ph0_pins[] = {
	TEGRA_PIN_SOC_GPIO12_PH0,
};
static const unsigned int soc_gpio13_ph1_pins[] = {
	TEGRA_PIN_SOC_GPIO13_PH1,
};
static const unsigned int soc_gpio14_ph2_pins[] = {
	TEGRA_PIN_SOC_GPIO14_PH2,
};
static const unsigned int uart4_tx_ph3_pins[] = {
	TEGRA_PIN_UART4_TX_PH3,
};
static const unsigned int uart4_rx_ph4_pins[] = {
	TEGRA_PIN_UART4_RX_PH4,
};
static const unsigned int uart4_rts_ph5_pins[] = {
	TEGRA_PIN_UART4_RTS_PH5,
};
static const unsigned int uart4_cts_ph6_pins[] = {
	TEGRA_PIN_UART4_CTS_PH6,
};
static const unsigned int dap2_sclk_ph7_pins[] = {
	TEGRA_PIN_DAP2_SCLK_PH7,
};
static const unsigned int dap2_dout_pi0_pins[] = {
	TEGRA_PIN_DAP2_DOUT_PI0,
};
static const unsigned int dap2_din_pi1_pins[] = {
	TEGRA_PIN_DAP2_DIN_PI1,
};
static const unsigned int dap2_fs_pi2_pins[] = {
	TEGRA_PIN_DAP2_FS_PI2,
};
static const unsigned int gen1_i2c_scl_pi3_pins[] = {
	TEGRA_PIN_GEN1_I2C_SCL_PI3,
};
static const unsigned int gen1_i2c_sda_pi4_pins[] = {
	TEGRA_PIN_GEN1_I2C_SDA_PI4,
};
static const unsigned int sdmmc1_clk_pj0_pins[] = {
	TEGRA_PIN_SDMMC1_CLK_PJ0,
};
static const unsigned int sdmmc1_cmd_pj1_pins[] = {
	TEGRA_PIN_SDMMC1_CMD_PJ1,
};
static const unsigned int sdmmc1_dat0_pj2_pins[] = {
	TEGRA_PIN_SDMMC1_DAT0_PJ2,
};
static const unsigned int sdmmc1_dat1_pj3_pins[] = {
	TEGRA_PIN_SDMMC1_DAT1_PJ3,
};
static const unsigned int sdmmc1_dat2_pj4_pins[] = {
	TEGRA_PIN_SDMMC1_DAT2_PJ4,
};
static const unsigned int sdmmc1_dat3_pj5_pins[] = {
	TEGRA_PIN_SDMMC1_DAT3_PJ5,
};
static const unsigned int pex_l0_clkreq_n_pk0_pins[] = {
	TEGRA_PIN_PEX_L0_CLKREQ_N_PK0,
};
static const unsigned int pex_l0_rst_n_pk1_pins[] = {
	TEGRA_PIN_PEX_L0_RST_N_PK1,
};
static const unsigned int pex_l1_clkreq_n_pk2_pins[] = {
	TEGRA_PIN_PEX_L1_CLKREQ_N_PK2,
};
static const unsigned int pex_l1_rst_n_pk3_pins[] = {
	TEGRA_PIN_PEX_L1_RST_N_PK3,
};
static const unsigned int pex_l2_clkreq_n_pk4_pins[] = {
	TEGRA_PIN_PEX_L2_CLKREQ_N_PK4,
};
static const unsigned int pex_l2_rst_n_pk5_pins[] = {
	TEGRA_PIN_PEX_L2_RST_N_PK5,
};
static const unsigned int pex_l3_clkreq_n_pk6_pins[] = {
	TEGRA_PIN_PEX_L3_CLKREQ_N_PK6,
};
static const unsigned int pex_l3_rst_n_pk7_pins[] = {
	TEGRA_PIN_PEX_L3_RST_N_PK7,
};
static const unsigned int pex_l4_clkreq_n_pl0_pins[] = {
	TEGRA_PIN_PEX_L4_CLKREQ_N_PL0,
};
static const unsigned int pex_l4_rst_n_pl1_pins[] = {
	TEGRA_PIN_PEX_L4_RST_N_PL1,
};
static const unsigned int pex_wake_n_pl2_pins[] = {
	TEGRA_PIN_PEX_WAKE_N_PL2,
};
static const unsigned int sata_dev_slp_pl3_pins[] = {
	TEGRA_PIN_SATA_DEV_SLP_PL3,
};
static const unsigned int dp_aux_ch0_hpd_pm0_pins[] = {
	TEGRA_PIN_DP_AUX_CH0_HPD_PM0,
};
static const unsigned int dp_aux_ch1_hpd_pm1_pins[] = {
	TEGRA_PIN_DP_AUX_CH1_HPD_PM1,
};
static const unsigned int dp_aux_ch2_hpd_pm2_pins[] = {
	TEGRA_PIN_DP_AUX_CH2_HPD_PM2,
};
static const unsigned int dp_aux_ch3_hpd_pm3_pins[] = {
	TEGRA_PIN_DP_AUX_CH3_HPD_PM3,
};
static const unsigned int hdmi_cec_pm4_pins[] = {
	TEGRA_PIN_HDMI_CEC_PM4,
};
static const unsigned int soc_gpio50_pm5_pins[] = {
	TEGRA_PIN_SOC_GPIO50_PM5,
};
static const unsigned int soc_gpio51_pm6_pins[] = {
	TEGRA_PIN_SOC_GPIO51_PM6,
};
static const unsigned int soc_gpio52_pm7_pins[] = {
	TEGRA_PIN_SOC_GPIO52_PM7,
};
static const unsigned int soc_gpio53_pn0_pins[] = {
	TEGRA_PIN_SOC_GPIO53_PN0,
};
static const unsigned int soc_gpio54_pn1_pins[] = {
	TEGRA_PIN_SOC_GPIO54_PN1,
};
static const unsigned int soc_gpio55_pn2_pins[] = {
	TEGRA_PIN_SOC_GPIO55_PN2,
};
static const unsigned int sdmmc3_clk_po0_pins[] = {
	TEGRA_PIN_SDMMC3_CLK_PO0,
};
static const unsigned int sdmmc3_cmd_po1_pins[] = {
	TEGRA_PIN_SDMMC3_CMD_PO1,
};
static const unsigned int sdmmc3_dat0_po2_pins[] = {
	TEGRA_PIN_SDMMC3_DAT0_PO2,
};
static const unsigned int sdmmc3_dat1_po3_pins[] = {
	TEGRA_PIN_SDMMC3_DAT1_PO3,
};
static const unsigned int sdmmc3_dat2_po4_pins[] = {
	TEGRA_PIN_SDMMC3_DAT2_PO4,
};
static const unsigned int sdmmc3_dat3_po5_pins[] = {
	TEGRA_PIN_SDMMC3_DAT3_PO5,
};
static const unsigned int extperiph1_clk_pp0_pins[] = {
	TEGRA_PIN_EXTPERIPH1_CLK_PP0,
};
static const unsigned int extperiph2_clk_pp1_pins[] = {
	TEGRA_PIN_EXTPERIPH2_CLK_PP1,
};
static const unsigned int cam_i2c_scl_pp2_pins[] = {
	TEGRA_PIN_CAM_I2C_SCL_PP2,
};
static const unsigned int cam_i2c_sda_pp3_pins[] = {
	TEGRA_PIN_CAM_I2C_SDA_PP3,
};
static const unsigned int soc_gpio04_pp4_pins[] = {
	TEGRA_PIN_SOC_GPIO04_PP4,
};
static const unsigned int soc_gpio05_pp5_pins[] = {
	TEGRA_PIN_SOC_GPIO05_PP5,
};
static const unsigned int soc_gpio06_pp6_pins[] = {
	TEGRA_PIN_SOC_GPIO06_PP6,
};
static const unsigned int soc_gpio07_pp7_pins[] = {
	TEGRA_PIN_SOC_GPIO07_PP7,
};
static const unsigned int soc_gpio20_pq0_pins[] = {
	TEGRA_PIN_SOC_GPIO20_PQ0,
};
static const unsigned int soc_gpio21_pq1_pins[] = {
	TEGRA_PIN_SOC_GPIO21_PQ1,
};
static const unsigned int soc_gpio22_pq2_pins[] = {
	TEGRA_PIN_SOC_GPIO22_PQ2,
};
static const unsigned int soc_gpio23_pq3_pins[] = {
	TEGRA_PIN_SOC_GPIO23_PQ3,
};
static const unsigned int soc_gpio40_pq4_pins[] = {
	TEGRA_PIN_SOC_GPIO40_PQ4,
};
static const unsigned int soc_gpio41_pq5_pins[] = {
	TEGRA_PIN_SOC_GPIO41_PQ5,
};
static const unsigned int soc_gpio42_pq6_pins[] = {
	TEGRA_PIN_SOC_GPIO42_PQ6,
};
static const unsigned int soc_gpio43_pq7_pins[] = {
	TEGRA_PIN_SOC_GPIO43_PQ7,
};
static const unsigned int soc_gpio44_pr0_pins[] = {
	TEGRA_PIN_SOC_GPIO44_PR0,
};
static const unsigned int soc_gpio45_pr1_pins[] = {
	TEGRA_PIN_SOC_GPIO45_PR1,
};
static const unsigned int uart1_tx_pr2_pins[] = {
	TEGRA_PIN_UART1_TX_PR2,
};
static const unsigned int uart1_rx_pr3_pins[] = {
	TEGRA_PIN_UART1_RX_PR3,
};
static const unsigned int uart1_rts_pr4_pins[] = {
	TEGRA_PIN_UART1_RTS_PR4,
};
static const unsigned int uart1_cts_pr5_pins[] = {
	TEGRA_PIN_UART1_CTS_PR5,
};
static const unsigned int dap1_sclk_ps0_pins[] = {
	TEGRA_PIN_DAP1_SCLK_PS0,
};
static const unsigned int dap1_dout_ps1_pins[] = {
	TEGRA_PIN_DAP1_DOUT_PS1,
};
static const unsigned int dap1_din_ps2_pins[] = {
	TEGRA_PIN_DAP1_DIN_PS2,
};
static const unsigned int dap1_fs_ps3_pins[] = {
	TEGRA_PIN_DAP1_FS_PS3,
};
static const unsigned int aud_mclk_ps4_pins[] = {
	TEGRA_PIN_AUD_MCLK_PS4,
};
static const unsigned int soc_gpio30_ps5_pins[] = {
	TEGRA_PIN_SOC_GPIO30_PS5,
};
static const unsigned int soc_gpio31_ps6_pins[] = {
	TEGRA_PIN_SOC_GPIO31_PS6,
};
static const unsigned int soc_gpio32_ps7_pins[] = {
	TEGRA_PIN_SOC_GPIO32_PS7,
};
static const unsigned int soc_gpio33_pt0_pins[] = {
	TEGRA_PIN_SOC_GPIO33_PT0,
};
static const unsigned int dap3_sclk_pt1_pins[] = {
	TEGRA_PIN_DAP3_SCLK_PT1,
};
static const unsigned int dap3_dout_pt2_pins[] = {
	TEGRA_PIN_DAP3_DOUT_PT2,
};
static const unsigned int dap3_din_pt3_pins[] = {
	TEGRA_PIN_DAP3_DIN_PT3,
};
static const unsigned int dap3_fs_pt4_pins[] = {
	TEGRA_PIN_DAP3_FS_PT4,
};
static const unsigned int dap5_sclk_pt5_pins[] = {
	TEGRA_PIN_DAP5_SCLK_PT5,
};
static const unsigned int dap5_dout_pt6_pins[] = {
	TEGRA_PIN_DAP5_DOUT_PT6,
};
static const unsigned int dap5_din_pt7_pins[] = {
	TEGRA_PIN_DAP5_DIN_PT7,
};
static const unsigned int dap5_fs_pu0_pins[] = {
	TEGRA_PIN_DAP5_FS_PU0,
};
static const unsigned int directdc1_clk_pv0_pins[] = {
	TEGRA_PIN_DIRECTDC1_CLK_PV0,
};
static const unsigned int directdc1_in_pv1_pins[] = {
	TEGRA_PIN_DIRECTDC1_IN_PV1,
};
static const unsigned int directdc1_out0_pv2_pins[] = {
	TEGRA_PIN_DIRECTDC1_OUT0_PV2,
};
static const unsigned int directdc1_out1_pv3_pins[] = {
	TEGRA_PIN_DIRECTDC1_OUT1_PV3,
};
static const unsigned int directdc1_out2_pv4_pins[] = {
	TEGRA_PIN_DIRECTDC1_OUT2_PV4,
};
static const unsigned int directdc1_out3_pv5_pins[] = {
	TEGRA_PIN_DIRECTDC1_OUT3_PV5,
};
static const unsigned int directdc1_out4_pv6_pins[] = {
	TEGRA_PIN_DIRECTDC1_OUT4_PV6,
};
static const unsigned int directdc1_out5_pv7_pins[] = {
	TEGRA_PIN_DIRECTDC1_OUT5_PV7,
};
static const unsigned int directdc1_out6_pw0_pins[] = {
	TEGRA_PIN_DIRECTDC1_OUT6_PW0,
};
static const unsigned int directdc1_out7_pw1_pins[] = {
	TEGRA_PIN_DIRECTDC1_OUT7_PW1,
};
static const unsigned int gpu_pwr_req_px0_pins[] = {
	TEGRA_PIN_GPU_PWR_REQ_PX0,
};
static const unsigned int cv_pwr_req_px1_pins[] = {
	TEGRA_PIN_CV_PWR_REQ_PX1,
};
static const unsigned int gp_pwm2_px2_pins[] = {
	TEGRA_PIN_GP_PWM2_PX2,
};
static const unsigned int gp_pwm3_px3_pins[] = {
	TEGRA_PIN_GP_PWM3_PX3,
};
static const unsigned int uart2_tx_px4_pins[] = {
	TEGRA_PIN_UART2_TX_PX4,
};
static const unsigned int uart2_rx_px5_pins[] = {
	TEGRA_PIN_UART2_RX_PX5,
};
static const unsigned int uart2_rts_px6_pins[] = {
	TEGRA_PIN_UART2_RTS_PX6,
};
static const unsigned int uart2_cts_px7_pins[] = {
	TEGRA_PIN_UART2_CTS_PX7,
};
static const unsigned int spi3_sck_py0_pins[] = {
	TEGRA_PIN_SPI3_SCK_PY0,
};
static const unsigned int spi3_miso_py1_pins[] = {
	TEGRA_PIN_SPI3_MISO_PY1,
};
static const unsigned int spi3_mosi_py2_pins[] = {
	TEGRA_PIN_SPI3_MOSI_PY2,
};
static const unsigned int spi3_cs0_py3_pins[] = {
	TEGRA_PIN_SPI3_CS0_PY3,
};
static const unsigned int spi3_cs1_py4_pins[] = {
	TEGRA_PIN_SPI3_CS1_PY4,
};
static const unsigned int uart5_tx_py5_pins[] = {
	TEGRA_PIN_UART5_TX_PY5,
};
static const unsigned int uart5_rx_py6_pins[] = {
	TEGRA_PIN_UART5_RX_PY6,
};
static const unsigned int uart5_rts_py7_pins[] = {
	TEGRA_PIN_UART5_RTS_PY7,
};
static const unsigned int uart5_cts_pz0_pins[] = {
	TEGRA_PIN_UART5_CTS_PZ0,
};
static const unsigned int usb_vbus_en0_pz1_pins[] = {
	TEGRA_PIN_USB_VBUS_EN0_PZ1,
};
static const unsigned int usb_vbus_en1_pz2_pins[] = {
	TEGRA_PIN_USB_VBUS_EN1_PZ2,
};
static const unsigned int spi1_sck_pz3_pins[] = {
	TEGRA_PIN_SPI1_SCK_PZ3,
};
static const unsigned int spi1_miso_pz4_pins[] = {
	TEGRA_PIN_SPI1_MISO_PZ4,
};
static const unsigned int spi1_mosi_pz5_pins[] = {
	TEGRA_PIN_SPI1_MOSI_PZ5,
};
static const unsigned int spi1_cs0_pz6_pins[] = {
	TEGRA_PIN_SPI1_CS0_PZ6,
};
static const unsigned int spi1_cs1_pz7_pins[] = {
	TEGRA_PIN_SPI1_CS1_PZ7,
};
static const unsigned int can1_dout_paa0_pins[] = {
	TEGRA_PIN_CAN1_DOUT_PAA0,
};
static const unsigned int can1_din_paa1_pins[] = {
	TEGRA_PIN_CAN1_DIN_PAA1,
};
static const unsigned int can0_dout_paa2_pins[] = {
	TEGRA_PIN_CAN0_DOUT_PAA2,
};
static const unsigned int can0_din_paa3_pins[] = {
	TEGRA_PIN_CAN0_DIN_PAA3,
};
static const unsigned int can0_stb_paa4_pins[] = {
	TEGRA_PIN_CAN0_STB_PAA4,
};
static const unsigned int can0_en_paa5_pins[] = {
	TEGRA_PIN_CAN0_EN_PAA5,
};
static const unsigned int can0_wake_paa6_pins[] = {
	TEGRA_PIN_CAN0_WAKE_PAA6,
};
static const unsigned int can0_err_paa7_pins[] = {
	TEGRA_PIN_CAN0_ERR_PAA7,
};
static const unsigned int can1_stb_pbb0_pins[] = {
	TEGRA_PIN_CAN1_STB_PBB0,
};
static const unsigned int can1_en_pbb1_pins[] = {
	TEGRA_PIN_CAN1_EN_PBB1,
};
static const unsigned int can1_wake_pbb2_pins[] = {
	TEGRA_PIN_CAN1_WAKE_PBB2,
};
static const unsigned int can1_err_pbb3_pins[] = {
	TEGRA_PIN_CAN1_ERR_PBB3,
};
static const unsigned int spi2_sck_pcc0_pins[] = {
	TEGRA_PIN_SPI2_SCK_PCC0,
};
static const unsigned int spi2_miso_pcc1_pins[] = {
	TEGRA_PIN_SPI2_MISO_PCC1,
};
static const unsigned int spi2_mosi_pcc2_pins[] = {
	TEGRA_PIN_SPI2_MOSI_PCC2,
};
static const unsigned int spi2_cs0_pcc3_pins[] = {
	TEGRA_PIN_SPI2_CS0_PCC3,
};
static const unsigned int touch_clk_pcc4_pins[] = {
	TEGRA_PIN_TOUCH_CLK_PCC4,
};
static const unsigned int uart3_tx_pcc5_pins[] = {
	TEGRA_PIN_UART3_TX_PCC5,
};
static const unsigned int uart3_rx_pcc6_pins[] = {
	TEGRA_PIN_UART3_RX_PCC6,
};
static const unsigned int gen2_i2c_scl_pcc7_pins[] = {
	TEGRA_PIN_GEN2_I2C_SCL_PCC7,
};
static const unsigned int gen2_i2c_sda_pdd0_pins[] = {
	TEGRA_PIN_GEN2_I2C_SDA_PDD0,
};
static const unsigned int gen8_i2c_scl_pdd1_pins[] = {
	TEGRA_PIN_GEN8_I2C_SCL_PDD1,
};
static const unsigned int gen8_i2c_sda_pdd2_pins[] = {
	TEGRA_PIN_GEN8_I2C_SDA_PDD2,
};
static const unsigned int safe_state_pee0_pins[] = {
	TEGRA_PIN_SAFE_STATE_PEE0,
};
static const unsigned int vcomp_alert_pee1_pins[] = {
	TEGRA_PIN_VCOMP_ALERT_PEE1,
};
static const unsigned int ao_retention_n_pee2_pins[] = {
	TEGRA_PIN_AO_RETENTION_N_PEE2,
};
static const unsigned int batt_oc_pee3_pins[] = {
	TEGRA_PIN_BATT_OC_PEE3,
};
static const unsigned int power_on_pee4_pins[] = {
	TEGRA_PIN_POWER_ON_PEE4,
};
static const unsigned int pwr_i2c_scl_pee5_pins[] = {
	TEGRA_PIN_PWR_I2C_SCL_PEE5,
};
static const unsigned int pwr_i2c_sda_pee6_pins[] = {
	TEGRA_PIN_PWR_I2C_SDA_PEE6,
};
static const unsigned int ufs0_ref_clk_pff0_pins[] = {
	TEGRA_PIN_UFS0_REF_CLK_PFF0,
};
static const unsigned int ufs0_rst_pff1_pins[] = {
	TEGRA_PIN_UFS0_RST_PFF1,
};
static const unsigned int pex_l5_clkreq_n_pgg0_pins[] = {
	TEGRA_PIN_PEX_L5_CLKREQ_N_PGG0,
};
static const unsigned int pex_l5_rst_n_pgg1_pins[] = {
	TEGRA_PIN_PEX_L5_RST_N_PGG1,
};
static const unsigned int directdc_comp_pins[] = {
	TEGRA_PIN_DIRECTDC_COMP,
};
static const unsigned int sdmmc4_clk_pins[] = {
	TEGRA_PIN_SDMMC4_CLK,
};
static const unsigned int sdmmc4_cmd_pins[] = {
	TEGRA_PIN_SDMMC4_CMD,
};
static const unsigned int sdmmc4_dqs_pins[] = {
	TEGRA_PIN_SDMMC4_DQS,
};
static const unsigned int sdmmc4_dat7_pins[] = {
	TEGRA_PIN_SDMMC4_DAT7,
};
static const unsigned int sdmmc4_dat6_pins[] = {
	TEGRA_PIN_SDMMC4_DAT6,
};
static const unsigned int sdmmc4_dat5_pins[] = {
	TEGRA_PIN_SDMMC4_DAT5,
};
static const unsigned int sdmmc4_dat4_pins[] = {
	TEGRA_PIN_SDMMC4_DAT4,
};
static const unsigned int sdmmc4_dat3_pins[] = {
	TEGRA_PIN_SDMMC4_DAT3,
};
static const unsigned int sdmmc4_dat2_pins[] = {
	TEGRA_PIN_SDMMC4_DAT2,
};
static const unsigned int sdmmc4_dat1_pins[] = {
	TEGRA_PIN_SDMMC4_DAT1,
};
static const unsigned int sdmmc4_dat0_pins[] = {
	TEGRA_PIN_SDMMC4_DAT0,
};
static const unsigned int sdmmc1_comp_pins[] = {
	TEGRA_PIN_SDMMC1_COMP,
};
static const unsigned int sdmmc1_hv_trim_pins[] = {
	TEGRA_PIN_SDMMC1_HV_TRIM,
};
static const unsigned int sdmmc3_comp_pins[] = {
	TEGRA_PIN_SDMMC3_COMP,
};
static const unsigned int sdmmc3_hv_trim_pins[] = {
	TEGRA_PIN_SDMMC3_HV_TRIM,
};
static const unsigned int eqos_comp_pins[] = {
	TEGRA_PIN_EQOS_COMP,
};
static const unsigned int qspi_comp_pins[] = {
	TEGRA_PIN_QSPI_COMP,
};
static const unsigned int sys_reset_n_pins[] = {
	TEGRA_PIN_SYS_RESET_N,
};
static const unsigned int shutdown_n_pins[] = {
	TEGRA_PIN_SHUTDOWN_N,
};
static const unsigned int pmu_int_n_pins[] = {
	TEGRA_PIN_PMU_INT_N,
};
static const unsigned int soc_pwr_req_pins[] = {
	TEGRA_PIN_SOC_PWR_REQ,
};
static const unsigned int clk_32k_in_pins[] = {
	TEGRA_PIN_CLK_32K_IN,
};

/* Define unique ID for each function */
enum tegra_mux_dt {
	TEGRA_MUX_RSVD0,
	TEGRA_MUX_RSVD1,
	TEGRA_MUX_RSVD2,
	TEGRA_MUX_RSVD3,
	TEGRA_MUX_TOUCH,
	TEGRA_MUX_UARTC,
	TEGRA_MUX_I2C8,
	TEGRA_MUX_UARTG,
	TEGRA_MUX_SPI2,
	TEGRA_MUX_GP,
	TEGRA_MUX_DCA,
	TEGRA_MUX_WDT,
	TEGRA_MUX_I2C2,
	TEGRA_MUX_CAN1,
	TEGRA_MUX_CAN0,
	TEGRA_MUX_DMIC3,
	TEGRA_MUX_DMIC5,
	TEGRA_MUX_GPIO,
	TEGRA_MUX_DSPK1,
	TEGRA_MUX_DSPK0,
	TEGRA_MUX_SPDIF,
	TEGRA_MUX_AUD,
	TEGRA_MUX_I2S1,
	TEGRA_MUX_DMIC1,
	TEGRA_MUX_DMIC2,
	TEGRA_MUX_I2S3,
	TEGRA_MUX_DMIC4,
	TEGRA_MUX_I2S4,
	TEGRA_MUX_EXTPERIPH2,
	TEGRA_MUX_EXTPERIPH1,
	TEGRA_MUX_I2C3,
	TEGRA_MUX_VGP1,
	TEGRA_MUX_VGP2,
	TEGRA_MUX_VGP3,
	TEGRA_MUX_VGP4,
	TEGRA_MUX_VGP5,
	TEGRA_MUX_VGP6,
	TEGRA_MUX_SLVS,
	TEGRA_MUX_EXTPERIPH3,
	TEGRA_MUX_EXTPERIPH4,
	TEGRA_MUX_I2S2,
	TEGRA_MUX_UARTD,
	TEGRA_MUX_I2C1,
	TEGRA_MUX_UARTA,
	TEGRA_MUX_DIRECTDC1,
	TEGRA_MUX_DIRECTDC,
	TEGRA_MUX_IQC1,
	TEGRA_MUX_IQC2,
	TEGRA_MUX_I2S6,
	TEGRA_MUX_SDMMC3,
	TEGRA_MUX_SDMMC1,
	TEGRA_MUX_DP,
	TEGRA_MUX_HDMI,
	TEGRA_MUX_PE2,
	TEGRA_MUX_IGPU,
	TEGRA_MUX_SATA,
	TEGRA_MUX_PE1,
	TEGRA_MUX_PE0,
	TEGRA_MUX_PE3,
	TEGRA_MUX_PE4,
	TEGRA_MUX_PE5,
	TEGRA_MUX_SOC,
	TEGRA_MUX_EQOS,
	TEGRA_MUX_QSPI,
	TEGRA_MUX_QSPI0,
	TEGRA_MUX_QSPI1,
	TEGRA_MUX_MIPI,
	TEGRA_MUX_SCE,
	TEGRA_MUX_I2C5,
	TEGRA_MUX_DISPLAYA,
	TEGRA_MUX_DISPLAYB,
	TEGRA_MUX_DCB,
	TEGRA_MUX_SPI1,
	TEGRA_MUX_UARTB,
	TEGRA_MUX_UARTE,
	TEGRA_MUX_SPI3,
	TEGRA_MUX_NV,
	TEGRA_MUX_CCLA,
	TEGRA_MUX_I2S5,
	TEGRA_MUX_USB,
	TEGRA_MUX_UFS0,
	TEGRA_MUX_DGPU,
	TEGRA_MUX_SDMMC4,
};

/* Make list of each function name */
#define TEGRA_PIN_FUNCTION(lid)			\
	{					\
		.name = #lid,			\
	}

static struct tegra_function tegra194_functions[] = {
	TEGRA_PIN_FUNCTION(rsvd0),
	TEGRA_PIN_FUNCTION(rsvd1),
	TEGRA_PIN_FUNCTION(rsvd2),
	TEGRA_PIN_FUNCTION(rsvd3),
	TEGRA_PIN_FUNCTION(touch),
	TEGRA_PIN_FUNCTION(uartc),
	TEGRA_PIN_FUNCTION(i2c8),
	TEGRA_PIN_FUNCTION(uartg),
	TEGRA_PIN_FUNCTION(spi2),
	TEGRA_PIN_FUNCTION(gp),
	TEGRA_PIN_FUNCTION(dca),
	TEGRA_PIN_FUNCTION(wdt),
	TEGRA_PIN_FUNCTION(i2c2),
	TEGRA_PIN_FUNCTION(can1),
	TEGRA_PIN_FUNCTION(can0),
	TEGRA_PIN_FUNCTION(dmic3),
	TEGRA_PIN_FUNCTION(dmic5),
	TEGRA_PIN_FUNCTION(gpio),
	TEGRA_PIN_FUNCTION(dspk1),
	TEGRA_PIN_FUNCTION(dspk0),
	TEGRA_PIN_FUNCTION(spdif),
	TEGRA_PIN_FUNCTION(aud),
	TEGRA_PIN_FUNCTION(i2s1),
	TEGRA_PIN_FUNCTION(dmic1),
	TEGRA_PIN_FUNCTION(dmic2),
	TEGRA_PIN_FUNCTION(i2s3),
	TEGRA_PIN_FUNCTION(dmic4),
	TEGRA_PIN_FUNCTION(i2s4),
	TEGRA_PIN_FUNCTION(extperiph2),
	TEGRA_PIN_FUNCTION(extperiph1),
	TEGRA_PIN_FUNCTION(i2c3),
	TEGRA_PIN_FUNCTION(vgp1),
	TEGRA_PIN_FUNCTION(vgp2),
	TEGRA_PIN_FUNCTION(vgp3),
	TEGRA_PIN_FUNCTION(vgp4),
	TEGRA_PIN_FUNCTION(vgp5),
	TEGRA_PIN_FUNCTION(vgp6),
	TEGRA_PIN_FUNCTION(slvs),
	TEGRA_PIN_FUNCTION(extperiph3),
	TEGRA_PIN_FUNCTION(extperiph4),
	TEGRA_PIN_FUNCTION(i2s2),
	TEGRA_PIN_FUNCTION(uartd),
	TEGRA_PIN_FUNCTION(i2c1),
	TEGRA_PIN_FUNCTION(uarta),
	TEGRA_PIN_FUNCTION(directdc1),
	TEGRA_PIN_FUNCTION(directdc),
	TEGRA_PIN_FUNCTION(iqc1),
	TEGRA_PIN_FUNCTION(iqc2),
	TEGRA_PIN_FUNCTION(i2s6),
	TEGRA_PIN_FUNCTION(sdmmc3),
	TEGRA_PIN_FUNCTION(sdmmc1),
	TEGRA_PIN_FUNCTION(dp),
	TEGRA_PIN_FUNCTION(hdmi),
	TEGRA_PIN_FUNCTION(pe2),
	TEGRA_PIN_FUNCTION(igpu),
	TEGRA_PIN_FUNCTION(sata),
	TEGRA_PIN_FUNCTION(pe1),
	TEGRA_PIN_FUNCTION(pe0),
	TEGRA_PIN_FUNCTION(pe3),
	TEGRA_PIN_FUNCTION(pe4),
	TEGRA_PIN_FUNCTION(pe5),
	TEGRA_PIN_FUNCTION(soc),
	TEGRA_PIN_FUNCTION(eqos),
	TEGRA_PIN_FUNCTION(qspi),
	TEGRA_PIN_FUNCTION(qspi0),
	TEGRA_PIN_FUNCTION(qspi1),
	TEGRA_PIN_FUNCTION(mipi),
	TEGRA_PIN_FUNCTION(sce),
	TEGRA_PIN_FUNCTION(i2c5),
	TEGRA_PIN_FUNCTION(displaya),
	TEGRA_PIN_FUNCTION(displayb),
	TEGRA_PIN_FUNCTION(dcb),
	TEGRA_PIN_FUNCTION(spi1),
	TEGRA_PIN_FUNCTION(uartb),
	TEGRA_PIN_FUNCTION(uarte),
	TEGRA_PIN_FUNCTION(spi3),
	TEGRA_PIN_FUNCTION(nv),
	TEGRA_PIN_FUNCTION(ccla),
	TEGRA_PIN_FUNCTION(i2s5),
	TEGRA_PIN_FUNCTION(usb),
	TEGRA_PIN_FUNCTION(ufs0),
	TEGRA_PIN_FUNCTION(dgpu),
	TEGRA_PIN_FUNCTION(sdmmc4),

};

#define PINGROUP_REG_Y(r) ((r))
#define PINGROUP_REG_N(r) -1

#define DRV_PINGROUP_Y(r) ((r))
#define DRV_PINGROUP_N(r) -1

#define DRV_PINGROUP_ENTRY_N(pg_name)				\
		.drv_reg = -1,					\
		.drv_bank = -1,					\
		.drvdn_bit = -1,				\
		.drvup_bit = -1,				\
		.slwr_bit = -1,					\
		.slwf_bit = -1

#define DRV_PINGROUP_ENTRY_Y(r, drvdn_b, drvdn_w, drvup_b,	\
			     drvup_w, slwr_b, slwr_w, slwf_b,	\
			     slwf_w, bank)			\
		.drv_reg = ((r)),				\
		.drv_bank = bank,				\
		.drvdn_bit = drvdn_b,				\
		.drvdn_width = drvdn_w,				\
		.drvup_bit = drvup_b,				\
		.drvup_width = drvup_w,				\
		.slwr_bit = slwr_b,				\
		.slwr_width = slwr_w,				\
		.slwf_bit = slwf_b,				\
		.slwf_width = slwf_w

#define PIN_PINGROUP_ENTRY_N(pg_name)				\
		.mux_reg = -1,					\
		.pupd_reg = -1,					\
		.tri_reg = -1,					\
		.einput_bit = -1,				\
		.e_io_hv_bit = -1,				\
		.odrain_bit = -1,				\
		.lock_bit = -1,					\
		.parked_bit = -1,				\
		.lpmd_bit = -1,					\
		.drvtype_bit = -1,				\
		.lpdr_bit = -1,					\
		.pbias_buf_bit = -1,				\
		.preemp_bit = -1,				\
		.rfu_in_bit = -1

#define PIN_PINGROUP_ENTRY_Y(r, bank, pupd, e_io_hv, e_lpbk, e_input,	\
			     e_lpdr, e_pbias_buf, gpio_sfio_sel, \
			     e_od, schmitt_b, drvtype, epreemp,	\
			     io_reset, rfu_in, io_rail)		\
		.mux_reg = PINGROUP_REG_Y(r),			\
		.lpmd_bit = -1,					\
		.lock_bit = -1,					\
		.hsm_bit = -1,					\
		.mux_bank = bank,				\
		.mux_bit = 0,					\
		.pupd_reg = PINGROUP_REG_##pupd(r),		\
		.pupd_bank = bank,				\
		.pupd_bit = 2,					\
		.tri_reg = PINGROUP_REG_Y(r),			\
		.tri_bank = bank,				\
		.tri_bit = 4,					\
		.einput_bit = e_input,				\
		.sfsel_bit = gpio_sfio_sel,			\
		.odrain_bit = e_od,				\
		.schmitt_bit = schmitt_b,			\
		.drvtype_bit = 13,				\
		.lpdr_bit = e_lpdr,				\

#define drive_touch_clk_pcc4            DRV_PINGROUP_ENTRY_Y(0x2004,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	1)
#define drive_uart3_rx_pcc6             DRV_PINGROUP_ENTRY_Y(0x200c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	1)
#define drive_uart3_tx_pcc5             DRV_PINGROUP_ENTRY_Y(0x2014,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	1)
#define drive_gen8_i2c_sda_pdd2         DRV_PINGROUP_ENTRY_Y(0x201c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	1)
#define drive_gen8_i2c_scl_pdd1         DRV_PINGROUP_ENTRY_Y(0x2024,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	1)
#define drive_spi2_mosi_pcc2            DRV_PINGROUP_ENTRY_Y(0x202c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	1)
#define drive_gen2_i2c_scl_pcc7         DRV_PINGROUP_ENTRY_Y(0x2034,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	1)
#define drive_spi2_cs0_pcc3             DRV_PINGROUP_ENTRY_Y(0x203c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	1)
#define drive_gen2_i2c_sda_pdd0         DRV_PINGROUP_ENTRY_Y(0x2044,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	1)
#define drive_spi2_sck_pcc0             DRV_PINGROUP_ENTRY_Y(0x204c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	1)
#define drive_spi2_miso_pcc1            DRV_PINGROUP_ENTRY_Y(0x2054,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	1)
#define drive_can1_dout_paa0            DRV_PINGROUP_ENTRY_Y(0x3004,	28,	2,	30,	2,	-1,	-1,	-1,	-1,	1)
#define drive_can1_din_paa1             DRV_PINGROUP_ENTRY_Y(0x300c,	28,	2,	30,	2,	-1,	-1,	-1,	-1,	1)
#define drive_can0_dout_paa2            DRV_PINGROUP_ENTRY_Y(0x3014,	28,	2,	30,	2,	-1,	-1,	-1,	-1,	1)
#define drive_can0_din_paa3             DRV_PINGROUP_ENTRY_Y(0x301c,	28,	2,	30,	2,	-1,	-1,	-1,	-1,	1)
#define drive_can0_stb_paa4             DRV_PINGROUP_ENTRY_Y(0x3024,	28,	2,	30,	2,	-1,	-1,	-1,	-1,	1)
#define drive_can0_en_paa5              DRV_PINGROUP_ENTRY_Y(0x302c,	28,	2,	30,	2,	-1,	-1,	-1,	-1,	1)
#define drive_can0_wake_paa6            DRV_PINGROUP_ENTRY_Y(0x3034,	28,	2,	30,	2,	-1,	-1,	-1,	-1,	1)
#define drive_can0_err_paa7             DRV_PINGROUP_ENTRY_Y(0x303c,	28,	2,	30,	2,	-1,	-1,	-1,	-1,	1)
#define drive_can1_stb_pbb0             DRV_PINGROUP_ENTRY_Y(0x3044,	28,	2,	30,	2,	-1,	-1,	-1,	-1,	1)
#define drive_can1_en_pbb1              DRV_PINGROUP_ENTRY_Y(0x304c,	28,	2,	30,	2,	-1,	-1,	-1,	-1,	1)
#define drive_can1_wake_pbb2            DRV_PINGROUP_ENTRY_Y(0x3054,	28,	2,	30,	2,	-1,	-1,	-1,	-1,	1)
#define drive_can1_err_pbb3             DRV_PINGROUP_ENTRY_Y(0x305c,	28,	2,	30,	2,	-1,	-1,	-1,	-1,	1)
#define drive_soc_gpio33_pt0            DRV_PINGROUP_ENTRY_Y(0x1004,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_soc_gpio32_ps7            DRV_PINGROUP_ENTRY_Y(0x100c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_soc_gpio31_ps6            DRV_PINGROUP_ENTRY_Y(0x1014,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_soc_gpio30_ps5            DRV_PINGROUP_ENTRY_Y(0x101c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_aud_mclk_ps4              DRV_PINGROUP_ENTRY_Y(0x1024,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_dap1_fs_ps3               DRV_PINGROUP_ENTRY_Y(0x102c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_dap1_din_ps2              DRV_PINGROUP_ENTRY_Y(0x1034,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_dap1_dout_ps1             DRV_PINGROUP_ENTRY_Y(0x103c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_dap1_sclk_ps0             DRV_PINGROUP_ENTRY_Y(0x1044,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_dap3_fs_pt4               DRV_PINGROUP_ENTRY_Y(0x104c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_dap3_din_pt3              DRV_PINGROUP_ENTRY_Y(0x1054,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_dap3_dout_pt2             DRV_PINGROUP_ENTRY_Y(0x105c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_dap3_sclk_pt1             DRV_PINGROUP_ENTRY_Y(0x1064,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_dap5_fs_pu0               DRV_PINGROUP_ENTRY_Y(0x106c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_dap5_din_pt7              DRV_PINGROUP_ENTRY_Y(0x1074,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_dap5_dout_pt6             DRV_PINGROUP_ENTRY_Y(0x107c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_dap5_sclk_pt5             DRV_PINGROUP_ENTRY_Y(0x1084,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_dap6_fs_pa3               DRV_PINGROUP_ENTRY_Y(0x2004,	28,	2,	30,	2,	-1,	-1,	-1,	-1,	0)
#define drive_dap6_din_pa2              DRV_PINGROUP_ENTRY_Y(0x200c,	28,	2,	30,	2,	-1,	-1,	-1,	-1,	0)
#define drive_dap6_dout_pa1             DRV_PINGROUP_ENTRY_Y(0x2014,	28,	2,	30,	2,	-1,	-1,	-1,	-1,	0)
#define drive_dap6_sclk_pa0             DRV_PINGROUP_ENTRY_Y(0x201c,	28,	2,	30,	2,	-1,	-1,	-1,	-1,	0)
#define drive_dap4_fs_pa7               DRV_PINGROUP_ENTRY_Y(0x2024,	28,	2,	30,	2,	-1,	-1,	-1,	-1,	0)
#define drive_dap4_din_pa6              DRV_PINGROUP_ENTRY_Y(0x202c,	28,	2,	30,	2,	-1,	-1,	-1,	-1,	0)
#define drive_dap4_dout_pa5             DRV_PINGROUP_ENTRY_Y(0x2034,	28,	2,	30,	2,	-1,	-1,	-1,	-1,	0)
#define drive_dap4_sclk_pa4             DRV_PINGROUP_ENTRY_Y(0x203c,	28,	2,	30,	2,	-1,	-1,	-1,	-1,	0)
#define drive_extperiph2_clk_pp1        DRV_PINGROUP_ENTRY_Y(0x0004,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_extperiph1_clk_pp0        DRV_PINGROUP_ENTRY_Y(0x000c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_cam_i2c_sda_pp3           DRV_PINGROUP_ENTRY_Y(0x0014,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_cam_i2c_scl_pp2           DRV_PINGROUP_ENTRY_Y(0x001c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_soc_gpio40_pq4            DRV_PINGROUP_ENTRY_Y(0x0024,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_soc_gpio41_pq5            DRV_PINGROUP_ENTRY_Y(0x002c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_soc_gpio42_pq6            DRV_PINGROUP_ENTRY_Y(0x0034,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_soc_gpio43_pq7            DRV_PINGROUP_ENTRY_Y(0x003c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_soc_gpio44_pr0            DRV_PINGROUP_ENTRY_Y(0x0044,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_soc_gpio45_pr1            DRV_PINGROUP_ENTRY_Y(0x004c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_soc_gpio20_pq0            DRV_PINGROUP_ENTRY_Y(0x0054,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_soc_gpio21_pq1            DRV_PINGROUP_ENTRY_Y(0x005c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_soc_gpio22_pq2            DRV_PINGROUP_ENTRY_Y(0x0064,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_soc_gpio23_pq3            DRV_PINGROUP_ENTRY_Y(0x006c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_soc_gpio04_pp4            DRV_PINGROUP_ENTRY_Y(0x0074,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_soc_gpio05_pp5            DRV_PINGROUP_ENTRY_Y(0x007c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_soc_gpio06_pp6            DRV_PINGROUP_ENTRY_Y(0x0084,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_soc_gpio07_pp7            DRV_PINGROUP_ENTRY_Y(0x008c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_uart1_cts_pr5             DRV_PINGROUP_ENTRY_Y(0x0094,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_uart1_rts_pr4             DRV_PINGROUP_ENTRY_Y(0x009c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_uart1_rx_pr3              DRV_PINGROUP_ENTRY_Y(0x00a4,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_uart1_tx_pr2              DRV_PINGROUP_ENTRY_Y(0x00ac,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_dap2_din_pi1              DRV_PINGROUP_ENTRY_Y(0x4004,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_dap2_dout_pi0             DRV_PINGROUP_ENTRY_Y(0x400c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_dap2_fs_pi2               DRV_PINGROUP_ENTRY_Y(0x4014,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_dap2_sclk_ph7             DRV_PINGROUP_ENTRY_Y(0x401c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_uart4_cts_ph6             DRV_PINGROUP_ENTRY_Y(0x4024,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_uart4_rts_ph5             DRV_PINGROUP_ENTRY_Y(0x402c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_uart4_rx_ph4              DRV_PINGROUP_ENTRY_Y(0x4034,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_uart4_tx_ph3              DRV_PINGROUP_ENTRY_Y(0x403c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_soc_gpio03_pg3            DRV_PINGROUP_ENTRY_Y(0x4044,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_soc_gpio02_pg2            DRV_PINGROUP_ENTRY_Y(0x404c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_soc_gpio01_pg1            DRV_PINGROUP_ENTRY_Y(0x4054,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_soc_gpio00_pg0            DRV_PINGROUP_ENTRY_Y(0x405c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_gen1_i2c_scl_pi3          DRV_PINGROUP_ENTRY_Y(0x4064,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_gen1_i2c_sda_pi4          DRV_PINGROUP_ENTRY_Y(0x406c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_soc_gpio08_pg4            DRV_PINGROUP_ENTRY_Y(0x4074,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_soc_gpio09_pg5            DRV_PINGROUP_ENTRY_Y(0x407c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_soc_gpio10_pg6            DRV_PINGROUP_ENTRY_Y(0x4084,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_soc_gpio11_pg7            DRV_PINGROUP_ENTRY_Y(0x408c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_soc_gpio12_ph0            DRV_PINGROUP_ENTRY_Y(0x4094,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_soc_gpio13_ph1            DRV_PINGROUP_ENTRY_Y(0x409c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_soc_gpio14_ph2            DRV_PINGROUP_ENTRY_Y(0x40a4,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_soc_gpio50_pm5            DRV_PINGROUP_ENTRY_Y(0x10004,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_soc_gpio51_pm6            DRV_PINGROUP_ENTRY_Y(0x1000c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_soc_gpio52_pm7            DRV_PINGROUP_ENTRY_Y(0x10014,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_soc_gpio53_pn0            DRV_PINGROUP_ENTRY_Y(0x1001c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_soc_gpio54_pn1            DRV_PINGROUP_ENTRY_Y(0x10024,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_soc_gpio55_pn2            DRV_PINGROUP_ENTRY_Y(0x1002c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_dp_aux_ch0_hpd_pm0        DRV_PINGROUP_ENTRY_Y(0x10034,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_dp_aux_ch1_hpd_pm1        DRV_PINGROUP_ENTRY_Y(0x1003c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_dp_aux_ch2_hpd_pm2        DRV_PINGROUP_ENTRY_Y(0x10044,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_dp_aux_ch3_hpd_pm3        DRV_PINGROUP_ENTRY_Y(0x1004c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_hdmi_cec_pm4              DRV_PINGROUP_ENTRY_Y(0x10054,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_pex_l2_clkreq_n_pk4       DRV_PINGROUP_ENTRY_Y(0x7004,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_pex_wake_n_pl2            DRV_PINGROUP_ENTRY_Y(0x700c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_pex_l1_clkreq_n_pk2       DRV_PINGROUP_ENTRY_Y(0x7014,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_pex_l1_rst_n_pk3          DRV_PINGROUP_ENTRY_Y(0x701c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_pex_l0_clkreq_n_pk0       DRV_PINGROUP_ENTRY_Y(0x7024,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_pex_l0_rst_n_pk1          DRV_PINGROUP_ENTRY_Y(0x702c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_pex_l2_rst_n_pk5          DRV_PINGROUP_ENTRY_Y(0x7034,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_pex_l3_clkreq_n_pk6       DRV_PINGROUP_ENTRY_Y(0x703c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_pex_l3_rst_n_pk7          DRV_PINGROUP_ENTRY_Y(0x7044,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_pex_l4_clkreq_n_pl0       DRV_PINGROUP_ENTRY_Y(0x704c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_pex_l4_rst_n_pl1          DRV_PINGROUP_ENTRY_Y(0x7054,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_sata_dev_slp_pl3          DRV_PINGROUP_ENTRY_Y(0x705c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_pex_l5_clkreq_n_pgg0      DRV_PINGROUP_ENTRY_Y(0x14004,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_pex_l5_rst_n_pgg1         DRV_PINGROUP_ENTRY_Y(0x1400c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_cpu_pwr_req_1_pb1         DRV_PINGROUP_ENTRY_Y(0x16004,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_cpu_pwr_req_0_pb0         DRV_PINGROUP_ENTRY_Y(0x1600c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_sdmmc1_clk_pj0            DRV_PINGROUP_ENTRY_Y(0x8004,	-1,	-1,	-1,	-1,	28,	2,	30,	2,	0)
#define drive_sdmmc1_cmd_pj1            DRV_PINGROUP_ENTRY_Y(0x800c,	-1,	-1,	-1,	-1,	28,	2,	30,	2,	0)
#define drive_sdmmc1_dat3_pj5           DRV_PINGROUP_ENTRY_Y(0x801c,	-1,	-1,	-1,	-1,	28,	2,	30,	2,	0)
#define drive_sdmmc1_dat2_pj4           DRV_PINGROUP_ENTRY_Y(0x8024,	-1,	-1,	-1,	-1,	28,	2,	30,	2,	0)
#define drive_sdmmc1_dat1_pj3           DRV_PINGROUP_ENTRY_Y(0x802c,	-1,	-1,	-1,	-1,	28,	2,	30,	2,	0)
#define drive_sdmmc1_dat0_pj2           DRV_PINGROUP_ENTRY_Y(0x8034,	-1,	-1,	-1,	-1,	28,	2,	30,	2,	0)
#define drive_sdmmc3_dat3_po5           DRV_PINGROUP_ENTRY_Y(0xa004,	-1,	-1,	-1,	-1,	28,	2,	30,	2,	0)
#define drive_sdmmc3_dat2_po4           DRV_PINGROUP_ENTRY_Y(0xa00c,	-1,	-1,	-1,	-1,	28,	2,	30,	2,	0)
#define drive_sdmmc3_dat1_po3           DRV_PINGROUP_ENTRY_Y(0xa014,	-1,	-1,	-1,	-1,	28,	2,	30,	2,	0)
#define drive_sdmmc3_dat0_po2           DRV_PINGROUP_ENTRY_Y(0xa01c,	-1,	-1,	-1,	-1,	28,	2,	30,	2,	0)
#define drive_sdmmc3_cmd_po1            DRV_PINGROUP_ENTRY_Y(0xa02c,	-1,	-1,	-1,	-1,	28,	2,	30,	2,	0)
#define drive_sdmmc3_clk_po0            DRV_PINGROUP_ENTRY_Y(0xa034,	-1,	-1,	-1,	-1,	28,	2,	30,	2,	0)
#define drive_shutdown_n                DRV_PINGROUP_ENTRY_Y(0x1004,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	1)
#define drive_pmu_int_n                 DRV_PINGROUP_ENTRY_Y(0x100c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	1)
#define drive_safe_state_pee0           DRV_PINGROUP_ENTRY_Y(0x1014,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	1)
#define drive_vcomp_alert_pee1          DRV_PINGROUP_ENTRY_Y(0x101c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	1)
#define drive_soc_pwr_req               DRV_PINGROUP_ENTRY_Y(0x1024,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	1)
#define drive_batt_oc_pee3              DRV_PINGROUP_ENTRY_Y(0x102c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	1)
#define drive_clk_32k_in                DRV_PINGROUP_ENTRY_Y(0x1034,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	1)
#define drive_power_on_pee4             DRV_PINGROUP_ENTRY_Y(0x103c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	1)
#define drive_pwr_i2c_scl_pee5          DRV_PINGROUP_ENTRY_Y(0x1044,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	1)
#define drive_pwr_i2c_sda_pee6          DRV_PINGROUP_ENTRY_Y(0x104c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	1)
#define drive_ao_retention_n_pee2       DRV_PINGROUP_ENTRY_Y(0x1064,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	1)
#define drive_gpu_pwr_req_px0           DRV_PINGROUP_ENTRY_Y(0xD004,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_spi3_miso_py1             DRV_PINGROUP_ENTRY_Y(0xD00c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_spi1_cs0_pz6              DRV_PINGROUP_ENTRY_Y(0xD014,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_spi3_cs0_py3              DRV_PINGROUP_ENTRY_Y(0xD01c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_spi1_miso_pz4             DRV_PINGROUP_ENTRY_Y(0xD024,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_spi3_cs1_py4              DRV_PINGROUP_ENTRY_Y(0xD02c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_gp_pwm3_px3               DRV_PINGROUP_ENTRY_Y(0xD034,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_gp_pwm2_px2               DRV_PINGROUP_ENTRY_Y(0xD03c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_spi1_sck_pz3              DRV_PINGROUP_ENTRY_Y(0xD044,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_spi3_sck_py0              DRV_PINGROUP_ENTRY_Y(0xD04c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_spi1_cs1_pz7              DRV_PINGROUP_ENTRY_Y(0xD054,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_spi1_mosi_pz5             DRV_PINGROUP_ENTRY_Y(0xD05c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_spi3_mosi_py2             DRV_PINGROUP_ENTRY_Y(0xD064,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_cv_pwr_req_px1            DRV_PINGROUP_ENTRY_Y(0xD06c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_uart2_tx_px4              DRV_PINGROUP_ENTRY_Y(0xD074,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_uart2_rx_px5              DRV_PINGROUP_ENTRY_Y(0xD07c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_uart2_rts_px6             DRV_PINGROUP_ENTRY_Y(0xD084,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_uart2_cts_px7             DRV_PINGROUP_ENTRY_Y(0xD08c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_uart5_rx_py6              DRV_PINGROUP_ENTRY_Y(0xD094,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_uart5_tx_py5              DRV_PINGROUP_ENTRY_Y(0xD09c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_uart5_rts_py7             DRV_PINGROUP_ENTRY_Y(0xD0a4,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_uart5_cts_pz0             DRV_PINGROUP_ENTRY_Y(0xD0ac,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_usb_vbus_en0_pz1          DRV_PINGROUP_ENTRY_Y(0xD0b4,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_usb_vbus_en1_pz2          DRV_PINGROUP_ENTRY_Y(0xD0bc,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define drive_ufs0_rst_pff1             DRV_PINGROUP_ENTRY_Y(0x11004,	12,	9,	24,	8,	-1,	-1,	-1,	-1,	0)
#define drive_ufs0_ref_clk_pff0         DRV_PINGROUP_ENTRY_Y(0x1100c,	12,	9,	24,	8,	-1,	-1,	-1,	-1,	0)

#define drive_directdc_comp             DRV_PINGROUP_ENTRY_N(no_entry)
#define drive_sdmmc1_comp               DRV_PINGROUP_ENTRY_N(no_entry)
#define drive_eqos_comp                 DRV_PINGROUP_ENTRY_N(no_entry)
#define drive_sdmmc3_comp               DRV_PINGROUP_ENTRY_N(no_entry)
#define drive_sdmmc4_clk                DRV_PINGROUP_ENTRY_N(no_entry)
#define drive_sdmmc4_cmd                DRV_PINGROUP_ENTRY_N(no_entry)
#define drive_sdmmc4_dqs                DRV_PINGROUP_ENTRY_N(no_entry)
#define drive_sdmmc4_dat7               DRV_PINGROUP_ENTRY_N(no_entry)
#define drive_sdmmc4_dat6               DRV_PINGROUP_ENTRY_N(no_entry)
#define drive_sdmmc4_dat5               DRV_PINGROUP_ENTRY_N(no_entry)
#define drive_sdmmc4_dat4               DRV_PINGROUP_ENTRY_N(no_entry)
#define drive_sdmmc4_dat3               DRV_PINGROUP_ENTRY_N(no_entry)
#define drive_sdmmc4_dat2               DRV_PINGROUP_ENTRY_N(no_entry)
#define drive_sdmmc4_dat1               DRV_PINGROUP_ENTRY_N(no_entry)
#define drive_sdmmc4_dat0               DRV_PINGROUP_ENTRY_N(no_entry)
#define drive_qspi_comp                 DRV_PINGROUP_ENTRY_N(no_entry)
#define drive_qspi1_cs_n_pc7            DRV_PINGROUP_ENTRY_N(no_entry)
#define drive_qspi1_sck_pc6             DRV_PINGROUP_ENTRY_N(no_entry)
#define drive_qspi1_io0_pd0             DRV_PINGROUP_ENTRY_N(no_entry)
#define drive_qspi1_io1_pd1             DRV_PINGROUP_ENTRY_N(no_entry)
#define drive_qspi1_io2_pd2             DRV_PINGROUP_ENTRY_N(no_entry)
#define drive_qspi1_io3_pd3             DRV_PINGROUP_ENTRY_N(no_entry)
#define drive_qspi0_io0_pc2             DRV_PINGROUP_ENTRY_N(no_entry)
#define drive_qspi0_io1_pc3             DRV_PINGROUP_ENTRY_N(no_entry)
#define drive_qspi0_io2_pc4             DRV_PINGROUP_ENTRY_N(no_entry)
#define drive_qspi0_io3_pc5             DRV_PINGROUP_ENTRY_N(no_entry)
#define drive_qspi0_cs_n_pc1            DRV_PINGROUP_ENTRY_N(no_entry)
#define drive_qspi0_sck_pc0             DRV_PINGROUP_ENTRY_N(no_entry)
#define drive_eqos_rx_ctl_pf2           DRV_PINGROUP_ENTRY_N(no_entry)
#define drive_eqos_tx_ctl_pe5           DRV_PINGROUP_ENTRY_N(no_entry)
#define drive_eqos_rxc_pf3              DRV_PINGROUP_ENTRY_N(no_entry)
#define drive_eqos_txc_pe0              DRV_PINGROUP_ENTRY_N(no_entry)
#define drive_eqos_sma_mdc_pf5          DRV_PINGROUP_ENTRY_N(no_entry)
#define drive_eqos_sma_mdio_pf4         DRV_PINGROUP_ENTRY_N(no_entry)
#define drive_eqos_rd0_pe6              DRV_PINGROUP_ENTRY_N(no_entry)
#define drive_eqos_rd1_pe7              DRV_PINGROUP_ENTRY_N(no_entry)
#define drive_eqos_rd2_pf0              DRV_PINGROUP_ENTRY_N(no_entry)
#define drive_eqos_rd3_pf1              DRV_PINGROUP_ENTRY_N(no_entry)
#define drive_eqos_td0_pe1              DRV_PINGROUP_ENTRY_N(no_entry)
#define drive_eqos_td1_pe2              DRV_PINGROUP_ENTRY_N(no_entry)
#define drive_eqos_td2_pe3              DRV_PINGROUP_ENTRY_N(no_entry)
#define drive_eqos_td3_pe4              DRV_PINGROUP_ENTRY_N(no_entry)
#define drive_directdc1_out7_pw1        DRV_PINGROUP_ENTRY_N(no_entry)
#define drive_directdc1_out6_pw0        DRV_PINGROUP_ENTRY_N(no_entry)
#define drive_directdc1_out5_pv7        DRV_PINGROUP_ENTRY_N(no_entry)
#define drive_directdc1_out4_pv6        DRV_PINGROUP_ENTRY_N(no_entry)
#define drive_directdc1_out3_pv5        DRV_PINGROUP_ENTRY_N(no_entry)
#define drive_directdc1_out2_pv4        DRV_PINGROUP_ENTRY_N(no_entry)
#define drive_directdc1_out1_pv3        DRV_PINGROUP_ENTRY_N(no_entry)
#define drive_directdc1_out0_pv2        DRV_PINGROUP_ENTRY_N(no_entry)
#define drive_directdc1_in_pv1          DRV_PINGROUP_ENTRY_N(no_entry)
#define drive_directdc1_clk_pv0         DRV_PINGROUP_ENTRY_N(no_entry)

#define PINGROUP(pg_name, f0, f1, f2, f3, r, bank, pupd, e_io_hv, e_lpbk, e_input, e_lpdr, e_pbias_buf, \
			gpio_sfio_sel, e_od, schmitt_b, drvtype, epreemp, io_reset, rfu_in, io_rail)	\
	{							\
		.name = #pg_name,				\
		.pins = pg_name##_pins,				\
		.npins = ARRAY_SIZE(pg_name##_pins),		\
			.funcs = {				\
				TEGRA_MUX_##f0,			\
				TEGRA_MUX_##f1,			\
				TEGRA_MUX_##f2,			\
				TEGRA_MUX_##f3,			\
			},					\
		PIN_PINGROUP_ENTRY_Y(r, bank, pupd, e_io_hv, e_lpbk,	\
				     e_input, e_lpdr, e_pbias_buf, \
				     gpio_sfio_sel, e_od,	\
				     schmitt_b, drvtype,	\
				     epreemp, io_reset,		\
				     rfu_in, io_rail)		\
		drive_##pg_name,				\
	}

static const struct tegra_pingroup tegra194_groups[] = {

	PINGROUP(touch_clk_pcc4,	GP,		TOUCH,		RSVD2,		RSVD3,		0x2000,		1,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_ao"),
	PINGROUP(uart3_rx_pcc6,		UARTC,		RSVD1,		RSVD2,		RSVD3,		0x2008,		1,	Y,	5,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_ao"),
	PINGROUP(uart3_tx_pcc5,		UARTC,		RSVD1,		RSVD2,		RSVD3,		0x2010,		1,	Y,	5,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_ao"),
	PINGROUP(gen8_i2c_sda_pdd2,	I2C8,		RSVD1,		RSVD2,		RSVD3,		0x2018,		1,	Y,	5,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_ao"),
	PINGROUP(gen8_i2c_scl_pdd1,	I2C8,		RSVD1,		RSVD2,		RSVD3,		0x2020,		1,	Y,	5,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_ao"),
	PINGROUP(spi2_mosi_pcc2,	SPI2,		UARTG,		RSVD2,		RSVD3,		0x2028,		1,	Y,	5,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_ao"),
	PINGROUP(gen2_i2c_scl_pcc7,	I2C2,		RSVD1,		RSVD2,		RSVD3,		0x2030,		1,	Y,	5,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_ao"),
	PINGROUP(spi2_cs0_pcc3,		SPI2,		UARTG,		RSVD2,		RSVD3,		0x2038,		1,	Y,	5,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_ao"),
	PINGROUP(gen2_i2c_sda_pdd0,	I2C2,		RSVD1,		RSVD2,		RSVD3,		0x2040,		1,	Y,	5,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_ao"),
	PINGROUP(spi2_sck_pcc0,		SPI2,		UARTG,		RSVD2,		RSVD3,		0x2048,		1,	Y,	5,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_ao"),
	PINGROUP(spi2_miso_pcc1,	SPI2,		UARTG,		RSVD2,		RSVD3,		0x2050,		1,	Y,	5,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_ao"),
	PINGROUP(can1_dout_paa0,	CAN1,		RSVD1,		RSVD2,		RSVD3,		0x3000,		1,	Y,	-1,	-1,	6,	-1,	9,	10,	-1,	12,	Y,	-1,	-1,	Y,	"vddio_ao_hv"),
	PINGROUP(can1_din_paa1,		CAN1,		RSVD1,		RSVD2,		RSVD3,		0x3008,		1,	Y,	-1,	-1,	6,	-1,	9,	10,	-1,	12,	Y,	-1,	-1,	Y,	"vddio_ao_hv"),
	PINGROUP(can0_dout_paa2,	CAN0,		RSVD1,		RSVD2,		RSVD3,		0x3010,		1,	Y,	-1,	-1,	6,	-1,	9,	10,	-1,	12,	Y,	-1,	-1,	Y,	"vddio_ao_hv"),
	PINGROUP(can0_din_paa3,		CAN0,		RSVD1,		RSVD2,		RSVD3,		0x3018,		1,	Y,	-1,	-1,	6,	-1,	9,	10,	-1,	12,	Y,	-1,	-1,	Y,	"vddio_ao_hv"),
	PINGROUP(can0_stb_paa4,		RSVD0,		WDT,		RSVD2,		RSVD3,		0x3020,		1,	Y,	-1,	-1,	6,	-1,	9,	10,	-1,	12,	Y,	-1,	-1,	Y,	"vddio_ao_hv"),
	PINGROUP(can0_en_paa5,		RSVD0,		RSVD1,		RSVD2,		RSVD3,		0x3028,		1,	Y,	-1,	-1,	6,	-1,	9,	10,	-1,	12,	Y,	-1,	-1,	Y,	"vddio_ao_hv"),
	PINGROUP(can0_wake_paa6,	RSVD0,		RSVD1,		RSVD2,		RSVD3,		0x3030,		1,	Y,	-1,	-1,	6,	-1,	9,	10,	-1,	12,	Y,	-1,	-1,	Y,	"vddio_ao_hv"),
	PINGROUP(can0_err_paa7,		RSVD0,		RSVD1,		RSVD2,		RSVD3,		0x3038,		1,	Y,	-1,	-1,	6,	-1,	9,	10,	-1,	12,	Y,	-1,	-1,	Y,	"vddio_ao_hv"),
	PINGROUP(can1_stb_pbb0,		RSVD0,		DMIC3,		DMIC5,		RSVD3,		0x3040,		1,	Y,	-1,	-1,	6,	-1,	9,	10,	-1,	12,	Y,	-1,	-1,	Y,	"vddio_ao_hv"),
	PINGROUP(can1_en_pbb1,		RSVD0,		DMIC3,		DMIC5,		RSVD3,		0x3048,		1,	Y,	-1,	-1,	6,	-1,	9,	10,	-1,	12,	Y,	-1,	-1,	Y,	"vddio_ao_hv"),
	PINGROUP(can1_wake_pbb2,	RSVD0,		RSVD1,		RSVD2,		RSVD3,		0x3050,		1,	Y,	-1,	-1,	6,	-1,	9,	10,	-1,	12,	Y,	-1,	-1,	Y,	"vddio_ao_hv"),
	PINGROUP(can1_err_pbb3,		RSVD0,		RSVD1,		RSVD2,		RSVD3,		0x3058,		1,	Y,	-1,	-1,	6,	-1,	9,	10,	-1,	12,	Y,	-1,	-1,	Y,	"vddio_ao_hv"),
	PINGROUP(soc_gpio33_pt0,	RSVD0,		SPDIF,		RSVD2,		RSVD3,		0x1000,		0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_audio"),
	PINGROUP(soc_gpio32_ps7,	RSVD0,		SPDIF,		RSVD2,		RSVD3,		0x1008,		0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_audio"),
	PINGROUP(soc_gpio31_ps6,	RSVD0,		SDMMC1,		RSVD2,		RSVD3,		0x1010,		0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_audio"),
	PINGROUP(soc_gpio30_ps5,	RSVD0,		RSVD1,		RSVD2,		RSVD3,		0x1018,		0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_audio"),
	PINGROUP(aud_mclk_ps4,		AUD,		RSVD1,		RSVD2,		RSVD3,		0x1020,		0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_audio"),
	PINGROUP(dap1_fs_ps3,		I2S1,		RSVD1,		RSVD2,		RSVD3,		0x1028,		0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_audio"),
	PINGROUP(dap1_din_ps2,		I2S1,		RSVD1,		RSVD2,		RSVD3,		0x1030,		0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_audio"),
	PINGROUP(dap1_dout_ps1,		I2S1,		RSVD1,		RSVD2,		RSVD3,		0x1038,		0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_audio"),
	PINGROUP(dap1_sclk_ps0,		I2S1,		RSVD1,		RSVD2,		RSVD3,		0x1040,		0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_audio"),
	PINGROUP(dap3_fs_pt4,		I2S3,		DMIC2,		RSVD2,		RSVD3,		0x1048,		0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_audio"),
	PINGROUP(dap3_din_pt3,		I2S3,		DMIC2,		RSVD2,		RSVD3,		0x1050,		0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_audio"),
	PINGROUP(dap3_dout_pt2,		I2S3,		DMIC1,		RSVD2,		RSVD3,		0x1058,		0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_audio"),
	PINGROUP(dap3_sclk_pt1,		I2S3,		DMIC1,		RSVD2,		RSVD3,		0x1060,		0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_audio"),
	PINGROUP(dap5_fs_pu0,		I2S5,		DMIC4,		DSPK1,		RSVD3,		0x1068,		0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_audio"),
	PINGROUP(dap5_din_pt7,		I2S5,		DMIC4,		DSPK1,		RSVD3,		0x1070,		0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_audio"),
	PINGROUP(dap5_dout_pt6,		I2S5,		DSPK0,		RSVD2,		RSVD3,		0x1078,		0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_audio"),
	PINGROUP(dap5_sclk_pt5,		I2S5,		DSPK0,		RSVD2,		RSVD3,		0x1080,		0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_audio"),
	PINGROUP(dap6_fs_pa3,		I2S6,		IQC1,		RSVD2,		RSVD3,		0x2000,		0,	Y,	-1,	-1,	6,	-1,	9,	10,	-1,	12,	Y,	-1,	-1,	Y,	"vddio_audio_hv"),
	PINGROUP(dap6_din_pa2,		I2S6,		IQC1,		RSVD2,		RSVD3,		0x2008,		0,	Y,	-1,	-1,	6,	-1,	9,	10,	-1,	12,	Y,	-1,	-1,	Y,	"vddio_audio_hv"),
	PINGROUP(dap6_dout_pa1,		I2S6,		IQC1,		RSVD2,		RSVD3,		0x2010,		0,	Y,	-1,	-1,	6,	-1,	9,	10,	-1,	12,	Y,	-1,	-1,	Y,	"vddio_audio_hv"),
	PINGROUP(dap6_sclk_pa0,		I2S6,		IQC1,		RSVD2,		RSVD3,		0x2018,		0,	Y,	-1,	-1,	6,	-1,	9,	10,	-1,	12,	Y,	-1,	-1,	Y,	"vddio_audio_hv"),
	PINGROUP(dap4_fs_pa7,		I2S4,		IQC2,		RSVD2,		RSVD3,		0x2020,		0,	Y,	-1,	-1,	6,	-1,	9,	10,	-1,	12,	Y,	-1,	-1,	Y,	"vddio_audio_hv"),
	PINGROUP(dap4_din_pa6,		I2S4,		IQC2,		RSVD2,		RSVD3,		0x2028,		0,	Y,	-1,	-1,	6,	-1,	9,	10,	-1,	12,	Y,	-1,	-1,	Y,	"vddio_audio_hv"),
	PINGROUP(dap4_dout_pa5,		I2S4,		IQC2,		RSVD2,		RSVD3,		0x2030,		0,	Y,	-1,	-1,	6,	-1,	9,	10,	-1,	12,	Y,	-1,	-1,	Y,	"vddio_audio_hv"),
	PINGROUP(dap4_sclk_pa4,		I2S4,		IQC2,		RSVD2,		RSVD3,		0x2038,		0,	Y,	-1,	-1,	6,	-1,	9,	10,	-1,	12,	Y,	-1,	-1,	Y,	"vddio_audio_hv"),
	PINGROUP(extperiph2_clk_pp1,	EXTPERIPH2,	RSVD1,		RSVD2,		RSVD3,		0x0000,		0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_cam"),
	PINGROUP(extperiph1_clk_pp0,	EXTPERIPH1,	RSVD1,		RSVD2,		RSVD3,		0x0008,		0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_cam"),
	PINGROUP(cam_i2c_sda_pp3,	I2C3,		RSVD1,		RSVD2,		RSVD3,		0x0010,		0,	Y,	5,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_cam"),
	PINGROUP(cam_i2c_scl_pp2,	I2C3,		RSVD1,		RSVD2,		RSVD3,		0x0018,		0,	Y,	5,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_cam"),
	PINGROUP(soc_gpio40_pq4,	VGP1,		SLVS,		RSVD2,		RSVD3,		0x0020,		0,	Y,	5,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_cam"),
	PINGROUP(soc_gpio41_pq5,	VGP2,		EXTPERIPH3,	RSVD2,		RSVD3,		0x0028,		0,	Y,	5,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_cam"),
	PINGROUP(soc_gpio42_pq6,	VGP3,		EXTPERIPH4,	RSVD2,		RSVD3,		0x0030,		0,	Y,	5,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_cam"),
	PINGROUP(soc_gpio43_pq7,	VGP4,		SLVS,		RSVD2,		RSVD3,		0x0038,		0,	Y,	5,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_cam"),
	PINGROUP(soc_gpio44_pr0,	VGP5,		GP,		RSVD2,		RSVD3,		0x0040,		0,	Y,	5,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_cam"),
	PINGROUP(soc_gpio45_pr1,	VGP6,		RSVD1,		RSVD2,		RSVD3,		0x0048,		0,	Y,	5,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_cam"),
	PINGROUP(soc_gpio20_pq0,	RSVD0,		RSVD1,		RSVD2,		RSVD3,		0x0050,		0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_cam"),
	PINGROUP(soc_gpio21_pq1,	RSVD0,		RSVD1,		RSVD2,		RSVD3,		0x0058,		0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_cam"),
	PINGROUP(soc_gpio22_pq2,	RSVD0,		NV,		RSVD2,		RSVD3,		0x0060,		0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_cam"),
	PINGROUP(soc_gpio23_pq3,	RSVD0,		WDT,		RSVD2,		RSVD3,		0x0068,		0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_cam"),
	PINGROUP(soc_gpio04_pp4,	RSVD0,		RSVD1,		RSVD2,		RSVD3,		0x0070,		0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_cam"),
	PINGROUP(soc_gpio05_pp5,	RSVD0,		IGPU,		RSVD2,		RSVD3,		0x0078,		0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_cam"),
	PINGROUP(soc_gpio06_pp6,	RSVD0,		RSVD1,		RSVD2,		RSVD3,		0x0080,		0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_cam"),
	PINGROUP(soc_gpio07_pp7,	RSVD0,		SATA,		SOC,		RSVD3,		0x0088,		0,	Y,	5,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_cam"),
	PINGROUP(uart1_cts_pr5,		UARTA,		RSVD1,		RSVD2,		RSVD3,		0x0090,		0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_cam"),
	PINGROUP(uart1_rts_pr4,		UARTA,		RSVD1,		RSVD2,		RSVD3,		0x0098,		0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_cam"),
	PINGROUP(uart1_rx_pr3,		UARTA,		RSVD1,		RSVD2,		RSVD3,		0x00a0,		0,	Y,	5,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_cam"),
	PINGROUP(uart1_tx_pr2,		UARTA,		RSVD1,		RSVD2,		RSVD3,		0x00a8,		0,	Y,	5,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_cam"),
	PINGROUP(dap2_din_pi1,		I2S2,		RSVD1,		RSVD2,		RSVD3,		0x4000,		0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_conn"),
	PINGROUP(dap2_dout_pi0,		I2S2,		RSVD1,		RSVD2,		RSVD3,		0x4008,		0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_conn"),
	PINGROUP(dap2_fs_pi2,		I2S2,		RSVD1,		RSVD2,		RSVD3,		0x4010,		0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_conn"),
	PINGROUP(dap2_sclk_ph7,		I2S2,		RSVD1,		RSVD2,		RSVD3,		0x4018,		0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_conn"),
	PINGROUP(uart4_cts_ph6,		UARTD,		RSVD1,		RSVD2,		RSVD3,		0x4020,		0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_conn"),
	PINGROUP(uart4_rts_ph5,		UARTD,		RSVD1,		RSVD2,		RSVD3,		0x4028,		0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_conn"),
	PINGROUP(uart4_rx_ph4,		UARTD,		RSVD1,		RSVD2,		RSVD3,		0x4030,		0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_conn"),
	PINGROUP(uart4_tx_ph3,		UARTD,		RSVD1,		RSVD2,		RSVD3,		0x4038,		0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_conn"),
	PINGROUP(soc_gpio03_pg3,	RSVD0,		RSVD1,		RSVD2,		RSVD3,		0x4040,		0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_conn"),
	PINGROUP(soc_gpio02_pg2,	RSVD0,		RSVD1,		RSVD2,		RSVD3,		0x4048,		0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_conn"),
	PINGROUP(soc_gpio01_pg1,	RSVD0,		RSVD1,		RSVD2,		RSVD3,		0x4050,		0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_conn"),
	PINGROUP(soc_gpio00_pg0,	RSVD0,		RSVD1,		RSVD2,		RSVD3,		0x4058,		0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_conn"),
	PINGROUP(gen1_i2c_scl_pi3,	I2C1,		RSVD1,		RSVD2,		RSVD3,		0x4060,		0,	Y,	5,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_conn"),
	PINGROUP(gen1_i2c_sda_pi4,	I2C1,		RSVD1,		RSVD2,		RSVD3,		0x4068,		0,	Y,	5,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_conn"),
	PINGROUP(soc_gpio08_pg4,	RSVD0,		CCLA,		RSVD2,		RSVD3,		0x4070,		0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_conn"),
	PINGROUP(soc_gpio09_pg5,	RSVD0,		RSVD1,		RSVD2,		RSVD3,		0x4078,		0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_conn"),
	PINGROUP(soc_gpio10_pg6,	GP,		RSVD1,		RSVD2,		RSVD3,		0x4080,		0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_conn"),
	PINGROUP(soc_gpio11_pg7,	RSVD0,		SDMMC1,		RSVD2,		RSVD3,		0x4088,		0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_conn"),
	PINGROUP(soc_gpio12_ph0,	RSVD0,		GP,		RSVD2,		RSVD3,		0x4090,		0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_conn"),
	PINGROUP(soc_gpio13_ph1,	RSVD0,		GP,		RSVD2,		RSVD3,		0x4098,		0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_conn"),
	PINGROUP(soc_gpio14_ph2,	RSVD0,		SDMMC1,		RSVD2,		RSVD3,		0x40a0,		0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_conn"),
	PINGROUP(directdc1_out7_pw1,	DIRECTDC1,	RSVD1,		RSVD2,		RSVD3,		0x5008,		0,	Y,	-1,	-1,	6,	-1,	9,	10,	-1,	12,	Y,	15,	17,	Y,	"vddio_debug"),
	PINGROUP(directdc1_out6_pw0,	DIRECTDC1,	RSVD1,		RSVD2,		RSVD3,		0x5010,		0,	Y,	-1,	-1,	6,	-1,	9,	10,	-1,	12,	Y,	15,	17,	Y,	"vddio_debug"),
	PINGROUP(directdc1_out5_pv7,	DIRECTDC1,	RSVD1,		RSVD2,		RSVD3,		0x5018,		0,	Y,	-1,	-1,	6,	-1,	9,	10,	-1,	12,	Y,	15,	17,	Y,	"vddio_debug"),
	PINGROUP(directdc1_out4_pv6,	DIRECTDC1,	RSVD1,		RSVD2,		RSVD3,		0x5020,		0,	Y,	-1,	-1,	6,	-1,	9,	10,	-1,	12,	Y,	15,	17,	Y,	"vddio_debug"),
	PINGROUP(directdc1_out3_pv5,	DIRECTDC1,	RSVD1,		RSVD2,		RSVD3,		0x5028,		0,	Y,	-1,	-1,	6,	-1,	9,	10,	-1,	12,	Y,	15,	17,	Y,	"vddio_debug"),
	PINGROUP(directdc1_out2_pv4,	DIRECTDC1,	RSVD1,		RSVD2,		RSVD3,		0x5030,		0,	Y,	-1,	-1,	6,	-1,	9,	10,	-1,	12,	Y,	15,	17,	Y,	"vddio_debug"),
	PINGROUP(directdc1_out1_pv3,	DIRECTDC1,	RSVD1,		RSVD2,		RSVD3,		0x5038,		0,	Y,	-1,	-1,	6,	-1,	9,	10,	-1,	12,	Y,	15,	17,	Y,	"vddio_debug"),
	PINGROUP(directdc1_out0_pv2,	DIRECTDC1,	RSVD1,		RSVD2,		RSVD3,		0x5040,		0,	Y,	-1,	-1,	6,	-1,	9,	10,	-1,	12,	Y,	15,	17,	Y,	"vddio_debug"),
	PINGROUP(directdc1_in_pv1,	DIRECTDC1,	RSVD1,		RSVD2,		RSVD3,		0x5048,		0,	Y,	-1,	-1,	6,	-1,	9,	10,	-1,	12,	Y,	15,	17,	Y,	"vddio_debug"),
	PINGROUP(directdc1_clk_pv0,	DIRECTDC1,	RSVD1,		RSVD2,		RSVD3,		0x5050,		0,	Y,	-1,	-1,	6,	-1,	9,	10,	-1,	12,	Y,	15,	17,	Y,	"vddio_debug"),
	PINGROUP(directdc_comp,		DIRECTDC,	RSVD1,		RSVD2,		RSVD3,		0x5058,		0,	N,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	Y,	-1,	-1,	Y,	"vddio_debug"),
	PINGROUP(soc_gpio50_pm5,	RSVD0,		DCA,		RSVD2,		RSVD3,		0x10000,	0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_edp"),
	PINGROUP(soc_gpio51_pm6,	RSVD0,		DCA,		RSVD2,		RSVD3,		0x10008,	0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_edp"),
	PINGROUP(soc_gpio52_pm7,	RSVD0,		DCB,		DGPU,		RSVD3,		0x10010,	0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_edp"),
	PINGROUP(soc_gpio53_pn0,	RSVD0,		DCB,		RSVD2,		RSVD3,		0x10018,	0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_edp"),
	PINGROUP(soc_gpio54_pn1,	RSVD0,		SDMMC3,		GP,		RSVD3,		0x10020,	0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_edp"),
	PINGROUP(soc_gpio55_pn2,	RSVD0,		SDMMC3,		RSVD2,		RSVD3,		0x10028,	0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_edp"),
	PINGROUP(dp_aux_ch0_hpd_pm0,	DP,		RSVD1,		RSVD2,		RSVD3,		0x10030,	0,	Y,	5,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_edp"),
	PINGROUP(dp_aux_ch1_hpd_pm1,	DP,		RSVD1,		RSVD2,		RSVD3,		0x10038,	0,	Y,	5,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_edp"),
	PINGROUP(dp_aux_ch2_hpd_pm2,	DP,		DISPLAYA,	RSVD2,		RSVD3,		0x10040,	0,	Y,	5,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_edp"),
	PINGROUP(dp_aux_ch3_hpd_pm3,	DP,		DISPLAYB,	RSVD2,		RSVD3,		0x10048,	0,	Y,	5,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_edp"),
	PINGROUP(hdmi_cec_pm4,		HDMI,		RSVD1,		RSVD2,		RSVD3,		0x10050,	0,	Y,	5,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_edp"),
	PINGROUP(eqos_td3_pe4,		EQOS,		RSVD1,		RSVD2,		RSVD3,		0x15000,	0,	Y,	-1,	-1,	6,	-1,	9,	10,	-1,	12,	Y,	15,	17,	Y,	"vddio_eqos"),
	PINGROUP(eqos_td2_pe3,		EQOS,		RSVD1,		RSVD2,		RSVD3,		0x15008,	0,	Y,	-1,	-1,	6,	-1,	9,	10,	-1,	12,	Y,	15,	17,	Y,	"vddio_eqos"),
	PINGROUP(eqos_td1_pe2,		EQOS,		RSVD1,		RSVD2,		RSVD3,		0x15010,	0,	Y,	-1,	-1,	6,	-1,	9,	10,	-1,	12,	Y,	15,	17,	Y,	"vddio_eqos"),
	PINGROUP(eqos_td0_pe1,		EQOS,		RSVD1,		RSVD2,		RSVD3,		0x15018,	0,	Y,	-1,	-1,	6,	-1,	9,	10,	-1,	12,	Y,	15,	17,	Y,	"vddio_eqos"),
	PINGROUP(eqos_rd3_pf1,		EQOS,		RSVD1,		RSVD2,		RSVD3,		0x15020,	0,	Y,	-1,	5,	6,	-1,	9,	10,	-1,	12,	Y,	15,	17,	Y,	"vddio_eqos"),
	PINGROUP(eqos_rd2_pf0,		EQOS,		RSVD1,		RSVD2,		RSVD3,		0x15028,	0,	Y,	-1,	-1,	6,	-1,	9,	10,	-1,	12,	Y,	15,	17,	Y,	"vddio_eqos"),
	PINGROUP(eqos_rd1_pe7,		EQOS,		RSVD1,		RSVD2,		RSVD3,		0x15030,	0,	Y,	-1,	-1,	6,	-1,	9,	10,	-1,	12,	Y,	15,	17,	Y,	"vddio_eqos"),
	PINGROUP(eqos_sma_mdio_pf4,	EQOS,		RSVD1,		RSVD2,		RSVD3,		0x15038,	0,	Y,	-1,	-1,	6,	-1,	9,	10,	-1,	12,	Y,	15,	17,	Y,	"vddio_eqos"),
	PINGROUP(eqos_rd0_pe6,		EQOS,		RSVD1,		RSVD2,		RSVD3,		0x15040,	0,	Y,	-1,	-1,	6,	-1,	9,	10,	-1,	12,	Y,	15,	17,	Y,	"vddio_eqos"),
	PINGROUP(eqos_sma_mdc_pf5,	EQOS,		RSVD1,		RSVD2,		RSVD3,		0x15048,	0,	Y,	-1,	-1,	6,	-1,	9,	10,	-1,	12,	Y,	15,	17,	Y,	"vddio_eqos"),
	PINGROUP(eqos_comp,		EQOS,		RSVD1,		RSVD2,		RSVD3,		0x15050,	0,	N,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	Y,	-1,	-1,	Y,	"vddio_eqos"),
	PINGROUP(eqos_txc_pe0,		EQOS,		RSVD1,		RSVD2,		RSVD3,		0x15058,	0,	Y,	-1,	-1,	6,	-1,	9,	10,	-1,	12,	Y,	15,	17,	Y,	"vddio_eqos"),
	PINGROUP(eqos_rxc_pf3,		EQOS,		RSVD1,		RSVD2,		RSVD3,		0x15060,	0,	Y,	-1,	-1,	6,	-1,	9,	10,	-1,	12,	Y,	15,	17,	Y,	"vddio_eqos"),
	PINGROUP(eqos_tx_ctl_pe5,	EQOS,		RSVD1,		RSVD2,		RSVD3,		0x15068,	0,	Y,	-1,	-1,	6,	-1,	9,	10,	-1,	12,	Y,	15,	17,	Y,	"vddio_eqos"),
	PINGROUP(eqos_rx_ctl_pf2,	EQOS,		RSVD1,		RSVD2,		RSVD3,		0x15070,	0,	Y,	-1,	-1,	6,	-1,	9,	10,	-1,	12,	Y,	15,	17,	Y,	"vddio_eqos"),
	PINGROUP(pex_l2_clkreq_n_pk4,	PE2,		RSVD1,		RSVD2,		RSVD3,		0x7000,		0,	Y,	5,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_pex_ctl"),
	PINGROUP(pex_wake_n_pl2,	RSVD0,		RSVD1,		RSVD2,		RSVD3,		0x7008,		0,	Y,	5,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_pex_ctl"),
	PINGROUP(pex_l1_clkreq_n_pk2,	PE1,		RSVD1,		RSVD2,		RSVD3,		0x7010,		0,	Y,	5,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_pex_ctl"),
	PINGROUP(pex_l1_rst_n_pk3,	PE1,		RSVD1,		RSVD2,		RSVD3,		0x7018,		0,	Y,	5,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_pex_ctl"),
	PINGROUP(pex_l0_clkreq_n_pk0,	PE0,		RSVD1,		RSVD2,		RSVD3,		0x7020,		0,	Y,	5,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_pex_ctl"),
	PINGROUP(pex_l0_rst_n_pk1,	PE0,		RSVD1,		RSVD2,		RSVD3,		0x7028,		0,	Y,	5,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_pex_ctl"),
	PINGROUP(pex_l2_rst_n_pk5,	PE2,		RSVD1,		RSVD2,		RSVD3,		0x7030,		0,	Y,	5,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_pex_ctl"),
	PINGROUP(pex_l3_clkreq_n_pk6,	PE3,		RSVD1,		RSVD2,		RSVD3,		0x7038,		0,	Y,	5,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_pex_ctl"),
	PINGROUP(pex_l3_rst_n_pk7,	PE3,		RSVD1,		RSVD2,		RSVD3,		0x7040,		0,	Y,	5,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_pex_ctl"),
	PINGROUP(pex_l4_clkreq_n_pl0,	PE4,		RSVD1,		RSVD2,		RSVD3,		0x7048,		0,	Y,	5,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_pex_ctl"),
	PINGROUP(pex_l4_rst_n_pl1,	PE4,		RSVD1,		RSVD2,		RSVD3,		0x7050,		0,	Y,	5,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_pex_ctl"),
	PINGROUP(sata_dev_slp_pl3,	SATA,		RSVD1,		RSVD2,		RSVD3,		0x7058,		0,	Y,	5,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_pex_ctl"),
	PINGROUP(pex_l5_clkreq_n_pgg0,	PE5,		RSVD1,		RSVD2,		RSVD3,		0x14000,	0,	Y,	5,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_pex_ctl_2"),
	PINGROUP(pex_l5_rst_n_pgg1,	PE5,		RSVD1,		RSVD2,		RSVD3,		0x14008,	0,	Y,	5,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_pex_ctl_2"),
	PINGROUP(cpu_pwr_req_1_pb1,	RSVD0,		RSVD1,		RSVD2,		RSVD3,		0x16000,	0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_pwr_ctl"),
	PINGROUP(cpu_pwr_req_0_pb0,	RSVD0,		RSVD1,		RSVD2,		RSVD3,		0x16008,	0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_pwr_ctl"),
	PINGROUP(qspi0_io3_pc5,		QSPI0,		RSVD1,		RSVD2,		RSVD3,		0xB000,		0,	Y,	-1,	-1,	6,	-1,	9,	10,	-1,	12,	Y,	15,	17,	Y,	"vddio_qspi"),
	PINGROUP(qspi0_io2_pc4,		QSPI0,		RSVD1,		RSVD2,		RSVD3,		0xB008,		0,	Y,	-1,	-1,	6,	-1,	9,	10,	-1,	12,	Y,	15,	17,	Y,	"vddio_qspi"),
	PINGROUP(qspi0_io1_pc3,		QSPI0,		RSVD1,		RSVD2,		RSVD3,		0xB010,		0,	Y,	-1,	-1,	6,	-1,	9,	10,	-1,	12,	Y,	15,	17,	Y,	"vddio_qspi"),
	PINGROUP(qspi0_io0_pc2,		QSPI0,		RSVD1,		RSVD2,		RSVD3,		0xB018,		0,	Y,	-1,	-1,	6,	-1,	9,	10,	-1,	12,	Y,	15,	17,	Y,	"vddio_qspi"),
	PINGROUP(qspi0_sck_pc0,		QSPI0,		RSVD1,		RSVD2,		RSVD3,		0xB020,		0,	Y,	-1,	5,	6,	-1,	9,	10,	-1,	12,	Y,	15,	17,	Y,	"vddio_qspi"),
	PINGROUP(qspi0_cs_n_pc1,	QSPI0,		RSVD1,		RSVD2,		RSVD3,		0xB028,		0,	Y,	-1,	-1,	6,	-1,	9,	10,	-1,	12,	Y,	15,	17,	Y,	"vddio_qspi"),
	PINGROUP(qspi1_io3_pd3,		QSPI1,		RSVD1,		RSVD2,		RSVD3,		0xB030,		0,	Y,	-1,	-1,	6,	-1,	9,	10,	-1,	12,	Y,	15,	17,	Y,	"vddio_qspi"),
	PINGROUP(qspi1_io2_pd2,		QSPI1,		RSVD1,		RSVD2,		RSVD3,		0xB038,		0,	Y,	-1,	-1,	6,	-1,	9,	10,	-1,	12,	Y,	15,	17,	Y,	"vddio_qspi"),
	PINGROUP(qspi1_io1_pd1,		QSPI1,		RSVD1,		RSVD2,		RSVD3,		0xB040,		0,	Y,	-1,	-1,	6,	-1,	9,	10,	-1,	12,	Y,	15,	17,	Y,	"vddio_qspi"),
	PINGROUP(qspi1_io0_pd0,		QSPI1,		RSVD1,		RSVD2,		RSVD3,		0xB048,		0,	Y,	-1,	-1,	6,	-1,	9,	10,	-1,	12,	Y,	15,	17,	Y,	"vddio_qspi"),
	PINGROUP(qspi1_sck_pc6,		QSPI1,		RSVD1,		RSVD2,		RSVD3,		0xB050,		0,	Y,	-1,	5,	6,	-1,	9,	10,	-1,	12,	Y,	15,	17,	Y,	"vddio_qspi"),
	PINGROUP(qspi1_cs_n_pc7,	QSPI1,		RSVD1,		RSVD2,		RSVD3,		0xB058,		0,	Y,	-1,	-1,	6,	-1,	9,	10,	-1,	12,	Y,	15,	17,	Y,	"vddio_qspi"),
	PINGROUP(qspi_comp,		QSPI,		RSVD1,		RSVD2,		RSVD3,		0xB060,		0,	N,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	Y,	-1,	-1,	Y,	"vddio_qspi"),
	PINGROUP(sdmmc1_clk_pj0,	SDMMC1,		RSVD1,		MIPI,		RSVD3,		0x8000,		0,	Y,	-1,	5,	6,	-1,	9,	10,	-1,	12,	Y,	-1,	-1,	Y,	"vddio_sdmmc1_hv"),
	PINGROUP(sdmmc1_cmd_pj1,	SDMMC1,		RSVD1,		MIPI,		RSVD3,		0x8008,		0,	Y,	-1,	-1,	6,	-1,	9,	10,	-1,	12,	Y,	-1,	-1,	Y,	"vddio_sdmmc1_hv"),
	PINGROUP(sdmmc1_comp,		SDMMC1,		RSVD1,		RSVD2,		RSVD3,		0x8010,		0,	N,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	N,	-1,	-1,	N,	"vddio_sdmmc1_hv"),
	PINGROUP(sdmmc1_dat3_pj5,	SDMMC1,		RSVD1,		MIPI,		RSVD3,		0x8018,		0,	Y,	-1,	-1,	6,	-1,	9,	10,	-1,	12,	Y,	-1,	-1,	Y,	"vddio_sdmmc1_hv"),
	PINGROUP(sdmmc1_dat2_pj4,	SDMMC1,		RSVD1,		MIPI,		RSVD3,		0x8020,		0,	Y,	-1,	-1,	6,	-1,	9,	10,	-1,	12,	Y,	-1,	-1,	Y,	"vddio_sdmmc1_hv"),
	PINGROUP(sdmmc1_dat1_pj3,	SDMMC1,		RSVD1,		MIPI,		RSVD3,		0x8028,		0,	Y,	-1,	-1,	6,	-1,	9,	10,	-1,	12,	Y,	-1,	-1,	Y,	"vddio_sdmmc1_hv"),
	PINGROUP(sdmmc1_dat0_pj2,	SDMMC1,		RSVD1,		MIPI,		RSVD3,		0x8030,		0,	Y,	-1,	-1,	6,	-1,	9,	10,	-1,	12,	Y,	-1,	-1,	Y,	"vddio_sdmmc1_hv"),
	PINGROUP(sdmmc3_dat3_po5,	SDMMC3,		RSVD1,		RSVD2,		RSVD3,		0xA000,		0,	Y,	-1,	-1,	6,	-1,	9,	10,	-1,	12,	Y,	-1,	-1,	Y,	"vddio_sdmmc3_hv"),
	PINGROUP(sdmmc3_dat2_po4,	SDMMC3,		RSVD1,		RSVD2,		RSVD3,		0xA008,		0,	Y,	-1,	-1,	6,	-1,	9,	10,	-1,	12,	Y,	-1,	-1,	Y,	"vddio_sdmmc3_hv"),
	PINGROUP(sdmmc3_dat1_po3,	SDMMC3,		RSVD1,		RSVD2,		RSVD3,		0xA010,		0,	Y,	-1,	-1,	6,	-1,	9,	10,	-1,	12,	Y,	-1,	-1,	Y,	"vddio_sdmmc3_hv"),
	PINGROUP(sdmmc3_dat0_po2,	SDMMC3,		RSVD1,		RSVD2,		RSVD3,		0xA018,		0,	Y,	-1,	-1,	6,	-1,	9,	10,	-1,	12,	Y,	-1,	-1,	Y,	"vddio_sdmmc3_hv"),
	PINGROUP(sdmmc3_comp,		SDMMC3,		RSVD1,		RSVD2,		RSVD3,		0xA020,		0,	N,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	N,	-1,	-1,	N,	"vddio_sdmmc3_hv"),
	PINGROUP(sdmmc3_cmd_po1,	SDMMC3,		RSVD1,		RSVD2,		RSVD3,		0xA028,		0,	Y,	-1,	-1,	6,	-1,	9,	10,	-1,	12,	Y,	-1,	-1,	Y,	"vddio_sdmmc3_hv"),
	PINGROUP(sdmmc3_clk_po0,	SDMMC3,		RSVD1,		RSVD2,		RSVD3,		0xA030,		0,	Y,	-1,	5,	6,	-1,	9,	10,	-1,	12,	Y,	-1,	-1,	Y,	"vddio_sdmmc3_hv"),
	PINGROUP(sdmmc4_clk,		SDMMC4,		RSVD1,		RSVD2,		RSVD3,		0x6008,		0,	Y,	-1,	5,	6,	-1,	-1,	-1,	-1,	-1,	Y,	-1,	-1,	N,	"vddio_sdmmc4"),
	PINGROUP(sdmmc4_cmd,		SDMMC4,		RSVD1,		RSVD2,		RSVD3,		0x6010,		0,	Y,	-1,	-1,	6,	-1,	-1,	-1,	-1,	-1,	Y,	-1,	-1,	N,	"vddio_sdmmc4"),
	PINGROUP(sdmmc4_dqs,		SDMMC4,		RSVD1,		RSVD2,		RSVD3,		0x6018,		0,	Y,	-1,	-1,	6,	-1,	-1,	-1,	-1,	-1,	N,	-1,	-1,	N,	"vddio_sdmmc4"),
	PINGROUP(sdmmc4_dat7,		SDMMC4,		RSVD1,		RSVD2,		RSVD3,		0x6020,		0,	Y,	-1,	-1,	6,	-1,	-1,	-1,	-1,	-1,	Y,	-1,	-1,	N,	"vddio_sdmmc4"),
	PINGROUP(sdmmc4_dat6,		SDMMC4,		RSVD1,		RSVD2,		RSVD3,		0x6028,		0,	Y,	-1,	-1,	6,	-1,	-1,	-1,	-1,	-1,	Y,	-1,	-1,	N,	"vddio_sdmmc4"),
	PINGROUP(sdmmc4_dat5,		SDMMC4,		RSVD1,		RSVD2,		RSVD3,		0x6030,		0,	Y,	-1,	-1,	6,	-1,	-1,	-1,	-1,	-1,	Y,	-1,	-1,	N,	"vddio_sdmmc4"),
	PINGROUP(sdmmc4_dat4,		SDMMC4,		RSVD1,		RSVD2,		RSVD3,		0x6038,		0,	Y,	-1,	-1,	6,	-1,	-1,	-1,	-1,	-1,	Y,	-1,	-1,	N,	"vddio_sdmmc4"),
	PINGROUP(sdmmc4_dat3,		SDMMC4,		RSVD1,		RSVD2,		RSVD3,		0x6040,		0,	Y,	-1,	-1,	6,	-1,	-1,	-1,	-1,	-1,	Y,	-1,	-1,	N,	"vddio_sdmmc4"),
	PINGROUP(sdmmc4_dat2,		SDMMC4,		RSVD1,		RSVD2,		RSVD3,		0x6048,		0,	Y,	-1,	-1,	6,	-1,	-1,	-1,	-1,	-1,	Y,	-1,	-1,	N,	"vddio_sdmmc4"),
	PINGROUP(sdmmc4_dat1,		SDMMC4,		RSVD1,		RSVD2,		RSVD3,		0x6050,		0,	Y,	-1,	-1,	6,	-1,	-1,	-1,	-1,	-1,	Y,	-1,	-1,	N,	"vddio_sdmmc4"),
	PINGROUP(sdmmc4_dat0,		SDMMC4,		RSVD1,		RSVD2,		RSVD3,		0x6058,		0,	Y,	-1,	-1,	6,	-1,	-1,	-1,	-1,	-1,	Y,	-1,	-1,	N,	"vddio_sdmmc4"),
	PINGROUP(shutdown_n,		RSVD0,		RSVD1,		RSVD2,		RSVD3,		0x1000,		1,	Y,	5,	-1,	6,	8,	-1,	-1,	-1,	12,	N,	-1,	-1,	N,	"vddio_sys"),
	PINGROUP(pmu_int_n,		RSVD0,		RSVD1,		RSVD2,		RSVD3,		0x1008,		1,	Y,	-1,	-1,	6,	8,	-1,	-1,	-1,	12,	N,	-1,	-1,	N,	"vddio_sys"),
	PINGROUP(safe_state_pee0,	SCE,		RSVD1,		RSVD2,		RSVD3,		0x1010,		1,	Y,	5,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_sys"),
	PINGROUP(vcomp_alert_pee1,	SOC,		RSVD1,		RSVD2,		RSVD3,		0x1018,		1,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_sys"),
	PINGROUP(soc_pwr_req,		RSVD0,		RSVD1,		RSVD2,		RSVD3,		0x1020,		1,	Y,	-1,	-1,	6,	8,	-1,	-1,	-1,	12,	N,	-1,	-1,	N,	"vddio_sys"),
	PINGROUP(batt_oc_pee3,		SOC,		RSVD1,		RSVD2,		RSVD3,		0x1028,		1,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_sys"),
	PINGROUP(clk_32k_in,		RSVD0,		RSVD1,		RSVD2,		RSVD3,		0x1030,		1,	Y,	-1,	-1,	-1,	8,	-1,	-1,	-1,	12,	N,	-1,	-1,	N,	"vddio_sys"),
	PINGROUP(power_on_pee4,		RSVD0,		RSVD1,		RSVD2,		RSVD3,		0x1038,		1,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_sys"),
	PINGROUP(pwr_i2c_scl_pee5,	I2C5,		RSVD1,		RSVD2,		RSVD3,		0x1040,		1,	Y,	5,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_sys"),
	PINGROUP(pwr_i2c_sda_pee6,	I2C5,		RSVD1,		RSVD2,		RSVD3,		0x1048,		1,	Y,	5,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_sys"),
	PINGROUP(ao_retention_n_pee2,	GPIO,		RSVD1,		RSVD2,		RSVD3,		0x1060,		1,	Y,	5,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_sys"),
	PINGROUP(gpu_pwr_req_px0,	RSVD0,		RSVD1,		RSVD2,		RSVD3,		0xD000,		0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_uart"),
	PINGROUP(spi3_miso_py1,		SPI3,		RSVD1,		RSVD2,		RSVD3,		0xD008,		0,	Y,	5,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_uart"),
	PINGROUP(spi1_cs0_pz6,		SPI1,		RSVD1,		RSVD2,		RSVD3,		0xD010,		0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_uart"),
	PINGROUP(spi3_cs0_py3,		SPI3,		RSVD1,		RSVD2,		RSVD3,		0xD018,		0,	Y,	5,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_uart"),
	PINGROUP(spi1_miso_pz4,		SPI1,		RSVD1,		RSVD2,		RSVD3,		0xD020,		0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_uart"),
	PINGROUP(spi3_cs1_py4,		SPI3,		RSVD1,		RSVD2,		RSVD3,		0xD028,		0,	Y,	5,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_uart"),
	PINGROUP(gp_pwm3_px3,		GP,		RSVD1,		RSVD2,		RSVD3,		0xD030,		0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_uart"),
	PINGROUP(gp_pwm2_px2,		GP,		RSVD1,		RSVD2,		RSVD3,		0xD038,		0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_uart"),
	PINGROUP(spi1_sck_pz3,		SPI1,		RSVD1,		RSVD2,		RSVD3,		0xD040,		0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_uart"),
	PINGROUP(spi3_sck_py0,		SPI3,		RSVD1,		RSVD2,		RSVD3,		0xD048,		0,	Y,	5,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_uart"),
	PINGROUP(spi1_cs1_pz7,		SPI1,		RSVD1,		RSVD2,		RSVD3,		0xD050,		0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_uart"),
	PINGROUP(spi1_mosi_pz5,		SPI1,		RSVD1,		RSVD2,		RSVD3,		0xD058,		0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_uart"),
	PINGROUP(spi3_mosi_py2,		SPI3,		RSVD1,		RSVD2,		RSVD3,		0xD060,		0,	Y,	5,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_uart"),
	PINGROUP(cv_pwr_req_px1,	RSVD0,		RSVD1,		RSVD2,		RSVD3,		0xD068,		0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_uart"),
	PINGROUP(uart2_tx_px4,		UARTB,		RSVD1,		RSVD2,		RSVD3,		0xD070,		0,	Y,	5,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_uart"),
	PINGROUP(uart2_rx_px5,		UARTB,		RSVD1,		RSVD2,		RSVD3,		0xD078,		0,	Y,	5,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_uart"),
	PINGROUP(uart2_rts_px6,		UARTB,		RSVD1,		RSVD2,		RSVD3,		0xD080,		0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_uart"),
	PINGROUP(uart2_cts_px7,		UARTB,		RSVD1,		RSVD2,		RSVD3,		0xD088,		0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_uart"),
	PINGROUP(uart5_rx_py6,		UARTE,		RSVD1,		RSVD2,		RSVD3,		0xD090,		0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_uart"),
	PINGROUP(uart5_tx_py5,		UARTE,		RSVD1,		RSVD2,		RSVD3,		0xD098,		0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_uart"),
	PINGROUP(uart5_rts_py7,		UARTE,		RSVD1,		RSVD2,		RSVD3,		0xD0a0,		0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_uart"),
	PINGROUP(uart5_cts_pz0,		UARTE,		RSVD1,		RSVD2,		RSVD3,		0xD0a8,		0,	Y,	-1,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_uart"),
	PINGROUP(usb_vbus_en0_pz1,	USB,		RSVD1,		RSVD2,		RSVD3,		0xD0b0,		0,	Y,	5,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_uart"),
	PINGROUP(usb_vbus_en1_pz2,	USB,		RSVD1,		RSVD2,		RSVD3,		0xD0b8,		0,	Y,	5,	-1,	6,	8,	-1,	10,	11,	12,	N,	-1,	-1,	N,	"vddio_uart"),
	PINGROUP(ufs0_rst_pff1,		UFS0,		RSVD1,		RSVD2,		RSVD3,		0x11000,	0,	Y,	-1,	-1,	6,	-1,	9,	10,	-1,	12,	Y,	15,	17,	Y,	"vddio_ufs"),
	PINGROUP(ufs0_ref_clk_pff0,	UFS0,		RSVD1,		RSVD2,		RSVD3,		0x11008,	0,	Y,	-1,	-1,	6,	-1,	9,	10,	-1,	12,	Y,	15,	17,	Y,	"vddio_ufs"),
};

static const struct tegra_pinctrl_soc_data tegra194_pinctrl = {
	.pins = tegra194_pins,
	.npins = ARRAY_SIZE(tegra194_pins),
	.functions = tegra194_functions,
	.nfunctions = ARRAY_SIZE(tegra194_functions),
	.groups = tegra194_groups,
	.ngroups = ARRAY_SIZE(tegra194_groups),
	.hsm_in_mux = true,
	.schmitt_in_mux = true,
	.drvtype_in_mux = true,
	.sfsel_in_mux = true,
};

static int tegra194_pinctrl_probe(struct platform_device *pdev)
{
	return tegra_pinctrl_probe(pdev, &tegra194_pinctrl);
}

static const struct of_device_id tegra194_pinctrl_of_match[] = {
	{ .compatible = "nvidia,tegra194-pinmux", },
	{ },
};

static struct platform_driver tegra194_pinctrl_driver = {
	.driver = {
		.name = "tegra194-pinctrl",
		.of_match_table = tegra194_pinctrl_of_match,
	},
	.probe = tegra194_pinctrl_probe,
};

static int __init tegra194_pinctrl_init(void)
{
	return platform_driver_register(&tegra194_pinctrl_driver);
}
arch_initcall(tegra194_pinctrl_init);
