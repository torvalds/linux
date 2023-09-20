// SPDX-License-Identifier: GPL-2.0+
/*
 * Pinctrl data for the NVIDIA Tegra234 pinmux
 *
 * Copyright (c) 2021-2023, NVIDIA CORPORATION.  All rights reserved.
 */

#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/property.h>

#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>

#include "pinctrl-tegra.h"

/* Define unique ID for each pins */
enum {
	TEGRA_PIN_DAP6_SCLK_PA0,
	TEGRA_PIN_DAP6_DOUT_PA1,
	TEGRA_PIN_DAP6_DIN_PA2,
	TEGRA_PIN_DAP6_FS_PA3,
	TEGRA_PIN_DAP4_SCLK_PA4,
	TEGRA_PIN_DAP4_DOUT_PA5,
	TEGRA_PIN_DAP4_DIN_PA6,
	TEGRA_PIN_DAP4_FS_PA7,
	TEGRA_PIN_SOC_GPIO08_PB0,
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
	TEGRA_PIN_SOC_GPIO13_PG0,
	TEGRA_PIN_SOC_GPIO14_PG1,
	TEGRA_PIN_SOC_GPIO15_PG2,
	TEGRA_PIN_SOC_GPIO16_PG3,
	TEGRA_PIN_SOC_GPIO17_PG4,
	TEGRA_PIN_SOC_GPIO18_PG5,
	TEGRA_PIN_SOC_GPIO19_PG6,
	TEGRA_PIN_SOC_GPIO20_PG7,
	TEGRA_PIN_SOC_GPIO21_PH0,
	TEGRA_PIN_SOC_GPIO22_PH1,
	TEGRA_PIN_SOC_GPIO06_PH2,
	TEGRA_PIN_UART4_TX_PH3,
	TEGRA_PIN_UART4_RX_PH4,
	TEGRA_PIN_UART4_RTS_PH5,
	TEGRA_PIN_UART4_CTS_PH6,
	TEGRA_PIN_SOC_GPIO41_PH7,
	TEGRA_PIN_SOC_GPIO42_PI0,
	TEGRA_PIN_SOC_GPIO43_PI1,
	TEGRA_PIN_SOC_GPIO44_PI2,
	TEGRA_PIN_GEN1_I2C_SCL_PI3,
	TEGRA_PIN_GEN1_I2C_SDA_PI4,
	TEGRA_PIN_CPU_PWR_REQ_PI5,
	TEGRA_PIN_SOC_GPIO07_PI6,
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
	TEGRA_PIN_SOC_GPIO34_PL3,
	TEGRA_PIN_DP_AUX_CH0_HPD_PM0,
	TEGRA_PIN_DP_AUX_CH1_HPD_PM1,
	TEGRA_PIN_DP_AUX_CH2_HPD_PM2,
	TEGRA_PIN_DP_AUX_CH3_HPD_PM3,
	TEGRA_PIN_SOC_GPIO55_PM4,
	TEGRA_PIN_SOC_GPIO36_PM5,
	TEGRA_PIN_SOC_GPIO53_PM6,
	TEGRA_PIN_SOC_GPIO38_PM7,
	TEGRA_PIN_DP_AUX_CH3_N_PN0,
	TEGRA_PIN_SOC_GPIO39_PN1,
	TEGRA_PIN_SOC_GPIO40_PN2,
	TEGRA_PIN_DP_AUX_CH1_P_PN3,
	TEGRA_PIN_DP_AUX_CH1_N_PN4,
	TEGRA_PIN_DP_AUX_CH2_P_PN5,
	TEGRA_PIN_DP_AUX_CH2_N_PN6,
	TEGRA_PIN_DP_AUX_CH3_P_PN7,
	TEGRA_PIN_EXTPERIPH1_CLK_PP0,
	TEGRA_PIN_EXTPERIPH2_CLK_PP1,
	TEGRA_PIN_CAM_I2C_SCL_PP2,
	TEGRA_PIN_CAM_I2C_SDA_PP3,
	TEGRA_PIN_SOC_GPIO23_PP4,
	TEGRA_PIN_SOC_GPIO24_PP5,
	TEGRA_PIN_SOC_GPIO25_PP6,
	TEGRA_PIN_PWR_I2C_SCL_PP7,
	TEGRA_PIN_PWR_I2C_SDA_PQ0,
	TEGRA_PIN_SOC_GPIO28_PQ1,
	TEGRA_PIN_SOC_GPIO29_PQ2,
	TEGRA_PIN_SOC_GPIO30_PQ3,
	TEGRA_PIN_SOC_GPIO31_PQ4,
	TEGRA_PIN_SOC_GPIO32_PQ5,
	TEGRA_PIN_SOC_GPIO33_PQ6,
	TEGRA_PIN_SOC_GPIO35_PQ7,
	TEGRA_PIN_SOC_GPIO37_PR0,
	TEGRA_PIN_SOC_GPIO56_PR1,
	TEGRA_PIN_UART1_TX_PR2,
	TEGRA_PIN_UART1_RX_PR3,
	TEGRA_PIN_UART1_RTS_PR4,
	TEGRA_PIN_UART1_CTS_PR5,
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
	TEGRA_PIN_SPI5_SCK_PAC0,
	TEGRA_PIN_SPI5_MISO_PAC1,
	TEGRA_PIN_SPI5_MOSI_PAC2,
	TEGRA_PIN_SPI5_CS0_PAC3,
	TEGRA_PIN_SOC_GPIO57_PAC4,
	TEGRA_PIN_SOC_GPIO58_PAC5,
	TEGRA_PIN_SOC_GPIO59_PAC6,
	TEGRA_PIN_SOC_GPIO60_PAC7,
	TEGRA_PIN_SOC_GPIO45_PAD0,
	TEGRA_PIN_SOC_GPIO46_PAD1,
	TEGRA_PIN_SOC_GPIO47_PAD2,
	TEGRA_PIN_SOC_GPIO48_PAD3,
	TEGRA_PIN_UFS0_REF_CLK_PAE0,
	TEGRA_PIN_UFS0_RST_N_PAE1,
	TEGRA_PIN_PEX_L5_CLKREQ_N_PAF0,
	TEGRA_PIN_PEX_L5_RST_N_PAF1,
	TEGRA_PIN_PEX_L6_CLKREQ_N_PAF2,
	TEGRA_PIN_PEX_L6_RST_N_PAF3,
	TEGRA_PIN_PEX_L7_CLKREQ_N_PAG0,
	TEGRA_PIN_PEX_L7_RST_N_PAG1,
	TEGRA_PIN_PEX_L8_CLKREQ_N_PAG2,
	TEGRA_PIN_PEX_L8_RST_N_PAG3,
	TEGRA_PIN_PEX_L9_CLKREQ_N_PAG4,
	TEGRA_PIN_PEX_L9_RST_N_PAG5,
	TEGRA_PIN_PEX_L10_CLKREQ_N_PAG6,
	TEGRA_PIN_PEX_L10_RST_N_PAG7,
	TEGRA_PIN_EQOS_COMP,
	TEGRA_PIN_QSPI_COMP,
	TEGRA_PIN_SDMMC1_COMP,
};

enum {
	TEGRA_PIN_CAN0_DOUT_PAA0,
	TEGRA_PIN_CAN0_DIN_PAA1,
	TEGRA_PIN_CAN1_DOUT_PAA2,
	TEGRA_PIN_CAN1_DIN_PAA3,
	TEGRA_PIN_CAN0_STB_PAA4,
	TEGRA_PIN_CAN0_EN_PAA5,
	TEGRA_PIN_SOC_GPIO49_PAA6,
	TEGRA_PIN_CAN0_ERR_PAA7,
	TEGRA_PIN_CAN1_STB_PBB0,
	TEGRA_PIN_CAN1_EN_PBB1,
	TEGRA_PIN_SOC_GPIO50_PBB2,
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
	TEGRA_PIN_SCE_ERROR_PEE0,
	TEGRA_PIN_VCOMP_ALERT_PEE1,
	TEGRA_PIN_AO_RETENTION_N_PEE2,
	TEGRA_PIN_BATT_OC_PEE3,
	TEGRA_PIN_POWER_ON_PEE4,
	TEGRA_PIN_SOC_GPIO26_PEE5,
	TEGRA_PIN_SOC_GPIO27_PEE6,
	TEGRA_PIN_BOOTV_CTL_N_PEE7,
	TEGRA_PIN_HDMI_CEC_PGG0,
};

/* Table for pin descriptor */
static const struct pinctrl_pin_desc tegra234_pins[] = {
	PINCTRL_PIN(TEGRA_PIN_DAP6_SCLK_PA0, "DAP6_SCLK_PA0"),
	PINCTRL_PIN(TEGRA_PIN_DAP6_DOUT_PA1, "DAP6_DOUT_PA1"),
	PINCTRL_PIN(TEGRA_PIN_DAP6_DIN_PA2, "DAP6_DIN_PA2"),
	PINCTRL_PIN(TEGRA_PIN_DAP6_FS_PA3, "DAP6_FS_PA3"),
	PINCTRL_PIN(TEGRA_PIN_DAP4_SCLK_PA4, "DAP4_SCLK_PA4"),
	PINCTRL_PIN(TEGRA_PIN_DAP4_DOUT_PA5, "DAP4_DOUT_PA5"),
	PINCTRL_PIN(TEGRA_PIN_DAP4_DIN_PA6, "DAP4_DIN_PA6"),
	PINCTRL_PIN(TEGRA_PIN_DAP4_FS_PA7, "DAP4_FS_PA7"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO08_PB0, "SOC_GPIO08_PB0"),
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
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO13_PG0, "SOC_GPIO13_PG0"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO14_PG1, "SOC_GPIO14_PG1"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO15_PG2, "SOC_GPIO15_PG2"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO16_PG3, "SOC_GPIO16_PG3"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO17_PG4, "SOC_GPIO17_PG4"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO18_PG5, "SOC_GPIO18_PG5"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO19_PG6, "SOC_GPIO19_PG6"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO20_PG7, "SOC_GPIO20_PG7"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO21_PH0, "SOC_GPIO21_PH0"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO22_PH1, "SOC_GPIO22_PH1"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO06_PH2, "SOC_GPIO06_PH2"),
	PINCTRL_PIN(TEGRA_PIN_UART4_TX_PH3, "UART4_TX_PH3"),
	PINCTRL_PIN(TEGRA_PIN_UART4_RX_PH4, "UART4_RX_PH4"),
	PINCTRL_PIN(TEGRA_PIN_UART4_RTS_PH5, "UART4_RTS_PH5"),
	PINCTRL_PIN(TEGRA_PIN_UART4_CTS_PH6, "UART4_CTS_PH6"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO41_PH7, "SOC_GPIO41_PH7"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO42_PI0, "SOC_GPIO42_PI0"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO43_PI1, "SOC_GPIO43_PI1"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO44_PI2, "SOC_GPIO44_PI2"),
	PINCTRL_PIN(TEGRA_PIN_GEN1_I2C_SCL_PI3, "GEN1_I2C_SCL_PI3"),
	PINCTRL_PIN(TEGRA_PIN_GEN1_I2C_SDA_PI4, "GEN1_I2C_SDA_PI4"),
	PINCTRL_PIN(TEGRA_PIN_CPU_PWR_REQ_PI5, "CPU_PWR_REQ_PI5"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO07_PI6, "SOC_GPIO07_PI6"),
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
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO34_PL3, "SOC_GPIO34_PL3"),
	PINCTRL_PIN(TEGRA_PIN_DP_AUX_CH0_HPD_PM0, "DP_AUX_CH0_HPD_PM0"),
	PINCTRL_PIN(TEGRA_PIN_DP_AUX_CH1_HPD_PM1, "DP_AUX_CH1_HPD_PM1"),
	PINCTRL_PIN(TEGRA_PIN_DP_AUX_CH2_HPD_PM2, "DP_AUX_CH2_HPD_PM2"),
	PINCTRL_PIN(TEGRA_PIN_DP_AUX_CH3_HPD_PM3, "DP_AUX_CH3_HPD_PM3"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO55_PM4, "SOC_GPIO55_PM4"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO36_PM5, "SOC_GPIO36_PM5"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO53_PM6, "SOC_GPIO53_PM6"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO38_PM7, "SOC_GPIO38_PM7"),
	PINCTRL_PIN(TEGRA_PIN_DP_AUX_CH3_N_PN0, "DP_AUX_CH3_N_PN0"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO39_PN1, "SOC_GPIO39_PN1"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO40_PN2, "SOC_GPIO40_PN2"),
	PINCTRL_PIN(TEGRA_PIN_DP_AUX_CH1_P_PN3, "DP_AUX_CH1_P_PN3"),
	PINCTRL_PIN(TEGRA_PIN_DP_AUX_CH1_N_PN4, "DP_AUX_CH1_N_PN4"),
	PINCTRL_PIN(TEGRA_PIN_DP_AUX_CH2_P_PN5, "DP_AUX_CH2_P_PN5"),
	PINCTRL_PIN(TEGRA_PIN_DP_AUX_CH2_N_PN6, "DP_AUX_CH2_N_PN6"),
	PINCTRL_PIN(TEGRA_PIN_DP_AUX_CH3_P_PN7, "DP_AUX_CH3_P_PN7"),
	PINCTRL_PIN(TEGRA_PIN_EXTPERIPH1_CLK_PP0, "EXTPERIPH1_CLK_PP0"),
	PINCTRL_PIN(TEGRA_PIN_EXTPERIPH2_CLK_PP1, "EXTPERIPH2_CLK_PP1"),
	PINCTRL_PIN(TEGRA_PIN_CAM_I2C_SCL_PP2, "CAM_I2C_SCL_PP2"),
	PINCTRL_PIN(TEGRA_PIN_CAM_I2C_SDA_PP3, "CAM_I2C_SDA_PP3"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO23_PP4, "SOC_GPIO23_PP4"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO24_PP5, "SOC_GPIO24_PP5"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO25_PP6, "SOC_GPIO25_PP6"),
	PINCTRL_PIN(TEGRA_PIN_PWR_I2C_SCL_PP7, "PWR_I2C_SCL_PP7"),
	PINCTRL_PIN(TEGRA_PIN_PWR_I2C_SDA_PQ0, "PWR_I2C_SDA_PQ0"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO28_PQ1, "SOC_GPIO28_PQ1"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO29_PQ2, "SOC_GPIO29_PQ2"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO30_PQ3, "SOC_GPIO30_PQ3"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO31_PQ4, "SOC_GPIO31_PQ4"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO32_PQ5, "SOC_GPIO32_PQ5"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO33_PQ6, "SOC_GPIO33_PQ6"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO35_PQ7, "SOC_GPIO35_PQ7"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO37_PR0, "SOC_GPIO37_PR0"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO56_PR1, "SOC_GPIO56_PR1"),
	PINCTRL_PIN(TEGRA_PIN_UART1_TX_PR2, "UART1_TX_PR2"),
	PINCTRL_PIN(TEGRA_PIN_UART1_RX_PR3, "UART1_RX_PR3"),
	PINCTRL_PIN(TEGRA_PIN_UART1_RTS_PR4, "UART1_RTS_PR4"),
	PINCTRL_PIN(TEGRA_PIN_UART1_CTS_PR5, "UART1_CTS_PR5"),
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
	PINCTRL_PIN(TEGRA_PIN_SPI5_SCK_PAC0, "SPI5_SCK_PAC0"),
	PINCTRL_PIN(TEGRA_PIN_SPI5_MISO_PAC1, "SPI5_MISO_PAC1"),
	PINCTRL_PIN(TEGRA_PIN_SPI5_MOSI_PAC2, "SPI5_MOSI_PAC2"),
	PINCTRL_PIN(TEGRA_PIN_SPI5_CS0_PAC3, "SPI5_CS0_PAC3"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO57_PAC4, "SOC_GPIO57_PAC4"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO58_PAC5, "SOC_GPIO58_PAC5"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO59_PAC6, "SOC_GPIO59_PAC6"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO60_PAC7, "SOC_GPIO60_PAC7"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO45_PAD0, "SOC_GPIO45_PAD0"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO46_PAD1, "SOC_GPIO46_PAD1"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO47_PAD2, "SOC_GPIO47_PAD2"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO48_PAD3, "SOC_GPIO48_PAD3"),
	PINCTRL_PIN(TEGRA_PIN_UFS0_REF_CLK_PAE0, "UFS0_REF_CLK_PAE0"),
	PINCTRL_PIN(TEGRA_PIN_UFS0_RST_N_PAE1, "UFS0_RST_N_PAE1"),
	PINCTRL_PIN(TEGRA_PIN_PEX_L5_CLKREQ_N_PAF0, "PEX_L5_CLKREQ_N_PAF0"),
	PINCTRL_PIN(TEGRA_PIN_PEX_L5_RST_N_PAF1, "PEX_L5_RST_N_PAF1"),
	PINCTRL_PIN(TEGRA_PIN_PEX_L6_CLKREQ_N_PAF2, "PEX_L6_CLKREQ_N_PAF2"),
	PINCTRL_PIN(TEGRA_PIN_PEX_L6_RST_N_PAF3, "PEX_L6_RST_N_PAF3"),
	PINCTRL_PIN(TEGRA_PIN_PEX_L7_CLKREQ_N_PAG0, "PEX_L7_CLKREQ_N_PAG0"),
	PINCTRL_PIN(TEGRA_PIN_PEX_L7_RST_N_PAG1, "PEX_L7_RST_N_PAG1"),
	PINCTRL_PIN(TEGRA_PIN_PEX_L8_CLKREQ_N_PAG2, "PEX_L8_CLKREQ_N_PAG2"),
	PINCTRL_PIN(TEGRA_PIN_PEX_L8_RST_N_PAG3, "PEX_L8_RST_N_PAG3"),
	PINCTRL_PIN(TEGRA_PIN_PEX_L9_CLKREQ_N_PAG4, "PEX_L9_CLKREQ_N_PAG4"),
	PINCTRL_PIN(TEGRA_PIN_PEX_L9_RST_N_PAG5, "PEX_L9_RST_N_PAG5"),
	PINCTRL_PIN(TEGRA_PIN_PEX_L10_CLKREQ_N_PAG6, "PEX_L10_CLKREQ_N_PAG6"),
	PINCTRL_PIN(TEGRA_PIN_PEX_L10_RST_N_PAG7, "PEX_L10_RST_N_PAG7"),
	PINCTRL_PIN(TEGRA_PIN_EQOS_COMP, "EQOS_COMP"),
	PINCTRL_PIN(TEGRA_PIN_QSPI_COMP, "QSPI_COMP"),
	PINCTRL_PIN(TEGRA_PIN_SDMMC1_COMP, "SDMMC1_COMP"),
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

static const unsigned int soc_gpio08_pb0_pins[] = {
	TEGRA_PIN_SOC_GPIO08_PB0,
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

static const unsigned int soc_gpio13_pg0_pins[] = {
	TEGRA_PIN_SOC_GPIO13_PG0,
};

static const unsigned int soc_gpio14_pg1_pins[] = {
	TEGRA_PIN_SOC_GPIO14_PG1,
};

static const unsigned int soc_gpio15_pg2_pins[] = {
	TEGRA_PIN_SOC_GPIO15_PG2,
};

static const unsigned int soc_gpio16_pg3_pins[] = {
	TEGRA_PIN_SOC_GPIO16_PG3,
};

static const unsigned int soc_gpio17_pg4_pins[] = {
	TEGRA_PIN_SOC_GPIO17_PG4,
};

static const unsigned int soc_gpio18_pg5_pins[] = {
	TEGRA_PIN_SOC_GPIO18_PG5,
};

static const unsigned int soc_gpio19_pg6_pins[] = {
	TEGRA_PIN_SOC_GPIO19_PG6,
};

static const unsigned int soc_gpio20_pg7_pins[] = {
	TEGRA_PIN_SOC_GPIO20_PG7,
};

static const unsigned int soc_gpio21_ph0_pins[] = {
	TEGRA_PIN_SOC_GPIO21_PH0,
};

static const unsigned int soc_gpio22_ph1_pins[] = {
	TEGRA_PIN_SOC_GPIO22_PH1,
};

static const unsigned int soc_gpio06_ph2_pins[] = {
	TEGRA_PIN_SOC_GPIO06_PH2,
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

static const unsigned int soc_gpio41_ph7_pins[] = {
	TEGRA_PIN_SOC_GPIO41_PH7,
};

static const unsigned int soc_gpio42_pi0_pins[] = {
	TEGRA_PIN_SOC_GPIO42_PI0,
};

static const unsigned int soc_gpio43_pi1_pins[] = {
	TEGRA_PIN_SOC_GPIO43_PI1,
};

static const unsigned int soc_gpio44_pi2_pins[] = {
	TEGRA_PIN_SOC_GPIO44_PI2,
};

static const unsigned int gen1_i2c_scl_pi3_pins[] = {
	TEGRA_PIN_GEN1_I2C_SCL_PI3,
};

static const unsigned int gen1_i2c_sda_pi4_pins[] = {
	TEGRA_PIN_GEN1_I2C_SDA_PI4,
};

static const unsigned int cpu_pwr_req_pi5_pins[] = {
	TEGRA_PIN_CPU_PWR_REQ_PI5,
};

static const unsigned int soc_gpio07_pi6_pins[] = {
	TEGRA_PIN_SOC_GPIO07_PI6,
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

static const unsigned int soc_gpio34_pl3_pins[] = {
	TEGRA_PIN_SOC_GPIO34_PL3,
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

static const unsigned int soc_gpio55_pm4_pins[] = {
	TEGRA_PIN_SOC_GPIO55_PM4,
};

static const unsigned int soc_gpio36_pm5_pins[] = {
	TEGRA_PIN_SOC_GPIO36_PM5,
};

static const unsigned int soc_gpio53_pm6_pins[] = {
	TEGRA_PIN_SOC_GPIO53_PM6,
};

static const unsigned int soc_gpio38_pm7_pins[] = {
	TEGRA_PIN_SOC_GPIO38_PM7,
};

static const unsigned int dp_aux_ch3_n_pn0_pins[] = {
	TEGRA_PIN_DP_AUX_CH3_N_PN0,
};

static const unsigned int soc_gpio39_pn1_pins[] = {
	TEGRA_PIN_SOC_GPIO39_PN1,
};

static const unsigned int soc_gpio40_pn2_pins[] = {
	TEGRA_PIN_SOC_GPIO40_PN2,
};

static const unsigned int dp_aux_ch1_p_pn3_pins[] = {
	TEGRA_PIN_DP_AUX_CH1_P_PN3,
};

static const unsigned int dp_aux_ch1_n_pn4_pins[] = {
	TEGRA_PIN_DP_AUX_CH1_N_PN4,
};

static const unsigned int dp_aux_ch2_p_pn5_pins[] = {
	TEGRA_PIN_DP_AUX_CH2_P_PN5,
};

static const unsigned int dp_aux_ch2_n_pn6_pins[] = {
	TEGRA_PIN_DP_AUX_CH2_N_PN6,
};

static const unsigned int dp_aux_ch3_p_pn7_pins[] = {
	TEGRA_PIN_DP_AUX_CH3_P_PN7,
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

static const unsigned int soc_gpio23_pp4_pins[] = {
	TEGRA_PIN_SOC_GPIO23_PP4,
};

static const unsigned int soc_gpio24_pp5_pins[] = {
	TEGRA_PIN_SOC_GPIO24_PP5,
};

static const unsigned int soc_gpio25_pp6_pins[] = {
	TEGRA_PIN_SOC_GPIO25_PP6,
};

static const unsigned int pwr_i2c_scl_pp7_pins[] = {
	TEGRA_PIN_PWR_I2C_SCL_PP7,
};

static const unsigned int pwr_i2c_sda_pq0_pins[] = {
	TEGRA_PIN_PWR_I2C_SDA_PQ0,
};

static const unsigned int soc_gpio28_pq1_pins[] = {
	TEGRA_PIN_SOC_GPIO28_PQ1,
};

static const unsigned int soc_gpio29_pq2_pins[] = {
	TEGRA_PIN_SOC_GPIO29_PQ2,
};

static const unsigned int soc_gpio30_pq3_pins[] = {
	TEGRA_PIN_SOC_GPIO30_PQ3,
};

static const unsigned int soc_gpio31_pq4_pins[] = {
	TEGRA_PIN_SOC_GPIO31_PQ4,
};

static const unsigned int soc_gpio32_pq5_pins[] = {
	TEGRA_PIN_SOC_GPIO32_PQ5,
};

static const unsigned int soc_gpio33_pq6_pins[] = {
	TEGRA_PIN_SOC_GPIO33_PQ6,
};

static const unsigned int soc_gpio35_pq7_pins[] = {
	TEGRA_PIN_SOC_GPIO35_PQ7,
};

static const unsigned int soc_gpio37_pr0_pins[] = {
	TEGRA_PIN_SOC_GPIO37_PR0,
};

static const unsigned int soc_gpio56_pr1_pins[] = {
	TEGRA_PIN_SOC_GPIO56_PR1,
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

static const unsigned int can0_dout_paa0_pins[] = {
	TEGRA_PIN_CAN0_DOUT_PAA0,
};

static const unsigned int can0_din_paa1_pins[] = {
	TEGRA_PIN_CAN0_DIN_PAA1,
};

static const unsigned int can1_dout_paa2_pins[] = {
	TEGRA_PIN_CAN1_DOUT_PAA2,
};

static const unsigned int can1_din_paa3_pins[] = {
	TEGRA_PIN_CAN1_DIN_PAA3,
};

static const unsigned int can0_stb_paa4_pins[] = {
	TEGRA_PIN_CAN0_STB_PAA4,
};

static const unsigned int can0_en_paa5_pins[] = {
	TEGRA_PIN_CAN0_EN_PAA5,
};

static const unsigned int soc_gpio49_paa6_pins[] = {
	TEGRA_PIN_SOC_GPIO49_PAA6,
};

static const unsigned int can0_err_paa7_pins[] = {
	TEGRA_PIN_CAN0_ERR_PAA7,
};

static const unsigned int spi5_sck_pac0_pins[] = {
	TEGRA_PIN_SPI5_SCK_PAC0,
};

static const unsigned int spi5_miso_pac1_pins[] = {
	TEGRA_PIN_SPI5_MISO_PAC1,
};

static const unsigned int spi5_mosi_pac2_pins[] = {
	TEGRA_PIN_SPI5_MOSI_PAC2,
};

static const unsigned int spi5_cs0_pac3_pins[] = {
	TEGRA_PIN_SPI5_CS0_PAC3,
};

static const unsigned int soc_gpio57_pac4_pins[] = {
	TEGRA_PIN_SOC_GPIO57_PAC4,
};

static const unsigned int soc_gpio58_pac5_pins[] = {
	TEGRA_PIN_SOC_GPIO58_PAC5,
};

static const unsigned int soc_gpio59_pac6_pins[] = {
	TEGRA_PIN_SOC_GPIO59_PAC6,
};

static const unsigned int soc_gpio60_pac7_pins[] = {
	TEGRA_PIN_SOC_GPIO60_PAC7,
};

static const unsigned int soc_gpio45_pad0_pins[] = {
	TEGRA_PIN_SOC_GPIO45_PAD0,
};

static const unsigned int soc_gpio46_pad1_pins[] = {
	TEGRA_PIN_SOC_GPIO46_PAD1,
};

static const unsigned int soc_gpio47_pad2_pins[] = {
	TEGRA_PIN_SOC_GPIO47_PAD2,
};

static const unsigned int soc_gpio48_pad3_pins[] = {
	TEGRA_PIN_SOC_GPIO48_PAD3,
};

static const unsigned int ufs0_ref_clk_pae0_pins[] = {
	TEGRA_PIN_UFS0_REF_CLK_PAE0,
};

static const unsigned int ufs0_rst_n_pae1_pins[] = {
	TEGRA_PIN_UFS0_RST_N_PAE1,
};

static const unsigned int pex_l5_clkreq_n_paf0_pins[] = {
	TEGRA_PIN_PEX_L5_CLKREQ_N_PAF0,
};

static const unsigned int pex_l5_rst_n_paf1_pins[] = {
	TEGRA_PIN_PEX_L5_RST_N_PAF1,
};

static const unsigned int pex_l6_clkreq_n_paf2_pins[] = {
	TEGRA_PIN_PEX_L6_CLKREQ_N_PAF2,
};

static const unsigned int pex_l6_rst_n_paf3_pins[] = {
	TEGRA_PIN_PEX_L6_RST_N_PAF3,
};

static const unsigned int pex_l7_clkreq_n_pag0_pins[] = {
	TEGRA_PIN_PEX_L7_CLKREQ_N_PAG0,
};

static const unsigned int pex_l7_rst_n_pag1_pins[] = {
	TEGRA_PIN_PEX_L7_RST_N_PAG1,
};

static const unsigned int pex_l8_clkreq_n_pag2_pins[] = {
	TEGRA_PIN_PEX_L8_CLKREQ_N_PAG2,
};

static const unsigned int pex_l8_rst_n_pag3_pins[] = {
	TEGRA_PIN_PEX_L8_RST_N_PAG3,
};

static const unsigned int pex_l9_clkreq_n_pag4_pins[] = {
	TEGRA_PIN_PEX_L9_CLKREQ_N_PAG4,
};

static const unsigned int pex_l9_rst_n_pag5_pins[] = {
	TEGRA_PIN_PEX_L9_RST_N_PAG5,
};

static const unsigned int pex_l10_clkreq_n_pag6_pins[] = {
	TEGRA_PIN_PEX_L10_CLKREQ_N_PAG6,
};

static const unsigned int pex_l10_rst_n_pag7_pins[] = {
	TEGRA_PIN_PEX_L10_RST_N_PAG7,
};

static const unsigned int can1_stb_pbb0_pins[] = {
	TEGRA_PIN_CAN1_STB_PBB0,
};

static const unsigned int can1_en_pbb1_pins[] = {
	TEGRA_PIN_CAN1_EN_PBB1,
};

static const unsigned int soc_gpio50_pbb2_pins[] = {
	TEGRA_PIN_SOC_GPIO50_PBB2,
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

static const unsigned int sce_error_pee0_pins[] = {
	TEGRA_PIN_SCE_ERROR_PEE0,
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

static const unsigned int soc_gpio26_pee5_pins[] = {
	TEGRA_PIN_SOC_GPIO26_PEE5,
};

static const unsigned int soc_gpio27_pee6_pins[] = {
	TEGRA_PIN_SOC_GPIO27_PEE6,
};

static const unsigned int bootv_ctl_n_pee7_pins[] = {
	TEGRA_PIN_BOOTV_CTL_N_PEE7,
};

static const unsigned int hdmi_cec_pgg0_pins[] = {
	TEGRA_PIN_HDMI_CEC_PGG0,
};

static const unsigned int eqos_comp_pins[] = {
	TEGRA_PIN_EQOS_COMP,
};

static const unsigned int qspi_comp_pins[] = {
	TEGRA_PIN_QSPI_COMP,
};

static const unsigned int sdmmc1_comp_pins[] = {
	TEGRA_PIN_SDMMC1_COMP,
};

/* Define unique ID for each function */
enum tegra_mux_dt {
	TEGRA_MUX_GP,
	TEGRA_MUX_UARTC,
	TEGRA_MUX_I2C8,
	TEGRA_MUX_SPI2,
	TEGRA_MUX_I2C2,
	TEGRA_MUX_CAN1,
	TEGRA_MUX_CAN0,
	TEGRA_MUX_RSVD0,
	TEGRA_MUX_ETH0,
	TEGRA_MUX_ETH2,
	TEGRA_MUX_ETH1,
	TEGRA_MUX_DP,
	TEGRA_MUX_ETH3,
	TEGRA_MUX_I2C4,
	TEGRA_MUX_I2C7,
	TEGRA_MUX_I2C9,
	TEGRA_MUX_EQOS,
	TEGRA_MUX_PE2,
	TEGRA_MUX_PE1,
	TEGRA_MUX_PE0,
	TEGRA_MUX_PE3,
	TEGRA_MUX_PE4,
	TEGRA_MUX_PE5,
	TEGRA_MUX_PE6,
	TEGRA_MUX_PE10,
	TEGRA_MUX_PE7,
	TEGRA_MUX_PE8,
	TEGRA_MUX_PE9,
	TEGRA_MUX_QSPI0,
	TEGRA_MUX_QSPI1,
	TEGRA_MUX_QSPI,
	TEGRA_MUX_SDMMC1,
	TEGRA_MUX_SCE,
	TEGRA_MUX_SOC,
	TEGRA_MUX_GPIO,
	TEGRA_MUX_HDMI,
	TEGRA_MUX_UFS0,
	TEGRA_MUX_SPI3,
	TEGRA_MUX_SPI1,
	TEGRA_MUX_UARTB,
	TEGRA_MUX_UARTE,
	TEGRA_MUX_USB,
	TEGRA_MUX_EXTPERIPH2,
	TEGRA_MUX_EXTPERIPH1,
	TEGRA_MUX_I2C3,
	TEGRA_MUX_VI0,
	TEGRA_MUX_I2C5,
	TEGRA_MUX_UARTA,
	TEGRA_MUX_UARTD,
	TEGRA_MUX_I2C1,
	TEGRA_MUX_I2S4,
	TEGRA_MUX_I2S6,
	TEGRA_MUX_AUD,
	TEGRA_MUX_SPI5,
	TEGRA_MUX_TOUCH,
	TEGRA_MUX_UARTJ,
	TEGRA_MUX_RSVD1,
	TEGRA_MUX_WDT,
	TEGRA_MUX_TSC,
	TEGRA_MUX_DMIC3,
	TEGRA_MUX_LED,
	TEGRA_MUX_VI0_ALT,
	TEGRA_MUX_I2S5,
	TEGRA_MUX_NV,
	TEGRA_MUX_EXTPERIPH3,
	TEGRA_MUX_EXTPERIPH4,
	TEGRA_MUX_SPI4,
	TEGRA_MUX_CCLA,
	TEGRA_MUX_I2S2,
	TEGRA_MUX_I2S1,
	TEGRA_MUX_I2S8,
	TEGRA_MUX_I2S3,
	TEGRA_MUX_RSVD2,
	TEGRA_MUX_DMIC5,
	TEGRA_MUX_DCA,
	TEGRA_MUX_DISPLAYB,
	TEGRA_MUX_DISPLAYA,
	TEGRA_MUX_VI1,
	TEGRA_MUX_DCB,
	TEGRA_MUX_DMIC1,
	TEGRA_MUX_DMIC4,
	TEGRA_MUX_I2S7,
	TEGRA_MUX_DMIC2,
	TEGRA_MUX_DSPK0,
	TEGRA_MUX_RSVD3,
	TEGRA_MUX_TSC_ALT,
	TEGRA_MUX_ISTCTRL,
	TEGRA_MUX_VI1_ALT,
	TEGRA_MUX_DSPK1,
	TEGRA_MUX_IGPU,
};

/* Make list of each function name */
#define TEGRA_PIN_FUNCTION(lid) #lid

static const char * const tegra234_functions[] = {
	TEGRA_PIN_FUNCTION(gp),
	TEGRA_PIN_FUNCTION(uartc),
	TEGRA_PIN_FUNCTION(i2c8),
	TEGRA_PIN_FUNCTION(spi2),
	TEGRA_PIN_FUNCTION(i2c2),
	TEGRA_PIN_FUNCTION(can1),
	TEGRA_PIN_FUNCTION(can0),
	TEGRA_PIN_FUNCTION(rsvd0),
	TEGRA_PIN_FUNCTION(eth0),
	TEGRA_PIN_FUNCTION(eth2),
	TEGRA_PIN_FUNCTION(eth1),
	TEGRA_PIN_FUNCTION(dp),
	TEGRA_PIN_FUNCTION(eth3),
	TEGRA_PIN_FUNCTION(i2c4),
	TEGRA_PIN_FUNCTION(i2c7),
	TEGRA_PIN_FUNCTION(i2c9),
	TEGRA_PIN_FUNCTION(eqos),
	TEGRA_PIN_FUNCTION(pe2),
	TEGRA_PIN_FUNCTION(pe1),
	TEGRA_PIN_FUNCTION(pe0),
	TEGRA_PIN_FUNCTION(pe3),
	TEGRA_PIN_FUNCTION(pe4),
	TEGRA_PIN_FUNCTION(pe5),
	TEGRA_PIN_FUNCTION(pe6),
	TEGRA_PIN_FUNCTION(pe10),
	TEGRA_PIN_FUNCTION(pe7),
	TEGRA_PIN_FUNCTION(pe8),
	TEGRA_PIN_FUNCTION(pe9),
	TEGRA_PIN_FUNCTION(qspi0),
	TEGRA_PIN_FUNCTION(qspi1),
	TEGRA_PIN_FUNCTION(qspi),
	TEGRA_PIN_FUNCTION(sdmmc1),
	TEGRA_PIN_FUNCTION(sce),
	TEGRA_PIN_FUNCTION(soc),
	TEGRA_PIN_FUNCTION(gpio),
	TEGRA_PIN_FUNCTION(hdmi),
	TEGRA_PIN_FUNCTION(ufs0),
	TEGRA_PIN_FUNCTION(spi3),
	TEGRA_PIN_FUNCTION(spi1),
	TEGRA_PIN_FUNCTION(uartb),
	TEGRA_PIN_FUNCTION(uarte),
	TEGRA_PIN_FUNCTION(usb),
	TEGRA_PIN_FUNCTION(extperiph2),
	TEGRA_PIN_FUNCTION(extperiph1),
	TEGRA_PIN_FUNCTION(i2c3),
	TEGRA_PIN_FUNCTION(vi0),
	TEGRA_PIN_FUNCTION(i2c5),
	TEGRA_PIN_FUNCTION(uarta),
	TEGRA_PIN_FUNCTION(uartd),
	TEGRA_PIN_FUNCTION(i2c1),
	TEGRA_PIN_FUNCTION(i2s4),
	TEGRA_PIN_FUNCTION(i2s6),
	TEGRA_PIN_FUNCTION(aud),
	TEGRA_PIN_FUNCTION(spi5),
	TEGRA_PIN_FUNCTION(touch),
	TEGRA_PIN_FUNCTION(uartj),
	TEGRA_PIN_FUNCTION(rsvd1),
	TEGRA_PIN_FUNCTION(wdt),
	TEGRA_PIN_FUNCTION(tsc),
	TEGRA_PIN_FUNCTION(dmic3),
	TEGRA_PIN_FUNCTION(led),
	TEGRA_PIN_FUNCTION(vi0_alt),
	TEGRA_PIN_FUNCTION(i2s5),
	TEGRA_PIN_FUNCTION(nv),
	TEGRA_PIN_FUNCTION(extperiph3),
	TEGRA_PIN_FUNCTION(extperiph4),
	TEGRA_PIN_FUNCTION(spi4),
	TEGRA_PIN_FUNCTION(ccla),
	TEGRA_PIN_FUNCTION(i2s2),
	TEGRA_PIN_FUNCTION(i2s1),
	TEGRA_PIN_FUNCTION(i2s8),
	TEGRA_PIN_FUNCTION(i2s3),
	TEGRA_PIN_FUNCTION(rsvd2),
	TEGRA_PIN_FUNCTION(dmic5),
	TEGRA_PIN_FUNCTION(dca),
	TEGRA_PIN_FUNCTION(displayb),
	TEGRA_PIN_FUNCTION(displaya),
	TEGRA_PIN_FUNCTION(vi1),
	TEGRA_PIN_FUNCTION(dcb),
	TEGRA_PIN_FUNCTION(dmic1),
	TEGRA_PIN_FUNCTION(dmic4),
	TEGRA_PIN_FUNCTION(i2s7),
	TEGRA_PIN_FUNCTION(dmic2),
	TEGRA_PIN_FUNCTION(dspk0),
	TEGRA_PIN_FUNCTION(rsvd3),
	TEGRA_PIN_FUNCTION(tsc_alt),
	TEGRA_PIN_FUNCTION(istctrl),
	TEGRA_PIN_FUNCTION(vi1_alt),
	TEGRA_PIN_FUNCTION(dspk1),
	TEGRA_PIN_FUNCTION(igpu),
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
		.drv_reg = DRV_PINGROUP_Y(r),			\
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
				e_lpdr, e_pbias_buf, gpio_sfio_sel,	\
				schmitt_b)				\
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
		.schmitt_bit = schmitt_b,			\
		.drvtype_bit = 13,				\
		.lpdr_bit = e_lpdr,				\

/* main drive pin groups */
#define	drive_soc_gpio08_pb0			DRV_PINGROUP_ENTRY_Y(0x500c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_soc_gpio36_pm5			DRV_PINGROUP_ENTRY_Y(0x10004,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_soc_gpio53_pm6			DRV_PINGROUP_ENTRY_Y(0x1000c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_soc_gpio55_pm4			DRV_PINGROUP_ENTRY_Y(0x10014,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_soc_gpio38_pm7			DRV_PINGROUP_ENTRY_Y(0x1001c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_soc_gpio39_pn1			DRV_PINGROUP_ENTRY_Y(0x10024,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_soc_gpio40_pn2			DRV_PINGROUP_ENTRY_Y(0x1002c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_dp_aux_ch0_hpd_pm0		DRV_PINGROUP_ENTRY_Y(0x10034,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_dp_aux_ch1_hpd_pm1		DRV_PINGROUP_ENTRY_Y(0x1003c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_dp_aux_ch2_hpd_pm2		DRV_PINGROUP_ENTRY_Y(0x10044,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_dp_aux_ch3_hpd_pm3		DRV_PINGROUP_ENTRY_Y(0x1004c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_dp_aux_ch1_p_pn3			DRV_PINGROUP_ENTRY_Y(0x10054,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_dp_aux_ch1_n_pn4			DRV_PINGROUP_ENTRY_Y(0x1005c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_dp_aux_ch2_p_pn5			DRV_PINGROUP_ENTRY_Y(0x10064,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_dp_aux_ch2_n_pn6			DRV_PINGROUP_ENTRY_Y(0x1006c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_dp_aux_ch3_p_pn7			DRV_PINGROUP_ENTRY_Y(0x10074,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_dp_aux_ch3_n_pn0			DRV_PINGROUP_ENTRY_Y(0x1007c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_pex_l2_clkreq_n_pk4		DRV_PINGROUP_ENTRY_Y(0x7004,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_pex_wake_n_pl2			DRV_PINGROUP_ENTRY_Y(0x700c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_pex_l1_clkreq_n_pk2		DRV_PINGROUP_ENTRY_Y(0x7014,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_pex_l1_rst_n_pk3			DRV_PINGROUP_ENTRY_Y(0x701c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_pex_l0_clkreq_n_pk0		DRV_PINGROUP_ENTRY_Y(0x7024,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_pex_l0_rst_n_pk1			DRV_PINGROUP_ENTRY_Y(0x702c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_pex_l2_rst_n_pk5			DRV_PINGROUP_ENTRY_Y(0x7034,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_pex_l3_clkreq_n_pk6		DRV_PINGROUP_ENTRY_Y(0x703c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_pex_l3_rst_n_pk7			DRV_PINGROUP_ENTRY_Y(0x7044,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_pex_l4_clkreq_n_pl0		DRV_PINGROUP_ENTRY_Y(0x704c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_pex_l4_rst_n_pl1			DRV_PINGROUP_ENTRY_Y(0x7054,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_soc_gpio34_pl3			DRV_PINGROUP_ENTRY_Y(0x705c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_pex_l5_clkreq_n_paf0		DRV_PINGROUP_ENTRY_Y(0x14004,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_pex_l5_rst_n_paf1			DRV_PINGROUP_ENTRY_Y(0x1400c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_pex_l6_clkreq_n_paf2		DRV_PINGROUP_ENTRY_Y(0x14014,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_pex_l6_rst_n_paf3			DRV_PINGROUP_ENTRY_Y(0x1401c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_pex_l10_clkreq_n_pag6		DRV_PINGROUP_ENTRY_Y(0x19004,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_pex_l10_rst_n_pag7		DRV_PINGROUP_ENTRY_Y(0x1900c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_pex_l7_clkreq_n_pag0		DRV_PINGROUP_ENTRY_Y(0x19014,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_pex_l7_rst_n_pag1			DRV_PINGROUP_ENTRY_Y(0x1901c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_pex_l8_clkreq_n_pag2		DRV_PINGROUP_ENTRY_Y(0x19024,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_pex_l8_rst_n_pag3			DRV_PINGROUP_ENTRY_Y(0x1902c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_pex_l9_clkreq_n_pag4		DRV_PINGROUP_ENTRY_Y(0x19034,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_pex_l9_rst_n_pag5			DRV_PINGROUP_ENTRY_Y(0x1903c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_sdmmc1_clk_pj0			DRV_PINGROUP_ENTRY_Y(0x8004,	28,	2,	30,	2,	-1,	-1,	-1,	-1,	0)
#define	drive_sdmmc1_cmd_pj1			DRV_PINGROUP_ENTRY_Y(0x800c,	28,	2,	30,	2,	-1,	-1,	-1,	-1,	0)
#define	drive_sdmmc1_dat3_pj5			DRV_PINGROUP_ENTRY_Y(0x801c,	28,	2,	30,	2,	-1,	-1,	-1,	-1,	0)
#define	drive_sdmmc1_dat2_pj4			DRV_PINGROUP_ENTRY_Y(0x8024,	28,	2,	30,	2,	-1,	-1,	-1,	-1,	0)
#define	drive_sdmmc1_dat1_pj3			DRV_PINGROUP_ENTRY_Y(0x802c,	28,	2,	30,	2,	-1,	-1,	-1,	-1,	0)
#define	drive_sdmmc1_dat0_pj2			DRV_PINGROUP_ENTRY_Y(0x8034,	28,	2,	30,	2,	-1,	-1,	-1,	-1,	0)
#define	drive_ufs0_rst_n_pae1			DRV_PINGROUP_ENTRY_Y(0x11004,	12,	5,	24,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_ufs0_ref_clk_pae0			DRV_PINGROUP_ENTRY_Y(0x1100c,	12,	5,	24,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_spi3_miso_py1			DRV_PINGROUP_ENTRY_Y(0xd004,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_spi1_cs0_pz6			DRV_PINGROUP_ENTRY_Y(0xd00c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_spi3_cs0_py3			DRV_PINGROUP_ENTRY_Y(0xd014,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_spi1_miso_pz4			DRV_PINGROUP_ENTRY_Y(0xd01c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_spi3_cs1_py4			DRV_PINGROUP_ENTRY_Y(0xd024,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_spi1_sck_pz3			DRV_PINGROUP_ENTRY_Y(0xd02c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_spi3_sck_py0			DRV_PINGROUP_ENTRY_Y(0xd034,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_spi1_cs1_pz7			DRV_PINGROUP_ENTRY_Y(0xd03c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_spi1_mosi_pz5			DRV_PINGROUP_ENTRY_Y(0xd044,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_spi3_mosi_py2			DRV_PINGROUP_ENTRY_Y(0xd04c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_uart2_tx_px4			DRV_PINGROUP_ENTRY_Y(0xd054,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_uart2_rx_px5			DRV_PINGROUP_ENTRY_Y(0xd05c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_uart2_rts_px6			DRV_PINGROUP_ENTRY_Y(0xd064,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_uart2_cts_px7			DRV_PINGROUP_ENTRY_Y(0xd06c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_uart5_tx_py5			DRV_PINGROUP_ENTRY_Y(0xd074,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_uart5_rx_py6			DRV_PINGROUP_ENTRY_Y(0xd07c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_uart5_rts_py7			DRV_PINGROUP_ENTRY_Y(0xd084,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_uart5_cts_pz0			DRV_PINGROUP_ENTRY_Y(0xd08c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_gpu_pwr_req_px0			DRV_PINGROUP_ENTRY_Y(0xd094,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_gp_pwm3_px3			DRV_PINGROUP_ENTRY_Y(0xd09c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_gp_pwm2_px2			DRV_PINGROUP_ENTRY_Y(0xd0a4,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_cv_pwr_req_px1			DRV_PINGROUP_ENTRY_Y(0xd0ac,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_usb_vbus_en0_pz1			DRV_PINGROUP_ENTRY_Y(0xd0b4,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_usb_vbus_en1_pz2			DRV_PINGROUP_ENTRY_Y(0xd0bc,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_extperiph2_clk_pp1		DRV_PINGROUP_ENTRY_Y(0x0004,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_extperiph1_clk_pp0		DRV_PINGROUP_ENTRY_Y(0x000c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_cam_i2c_sda_pp3			DRV_PINGROUP_ENTRY_Y(0x0014,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_cam_i2c_scl_pp2			DRV_PINGROUP_ENTRY_Y(0x001c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_soc_gpio23_pp4			DRV_PINGROUP_ENTRY_Y(0x0024,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_soc_gpio24_pp5			DRV_PINGROUP_ENTRY_Y(0x002c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_soc_gpio25_pp6			DRV_PINGROUP_ENTRY_Y(0x0034,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_pwr_i2c_scl_pp7			DRV_PINGROUP_ENTRY_Y(0x003c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_pwr_i2c_sda_pq0			DRV_PINGROUP_ENTRY_Y(0x0044,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_soc_gpio28_pq1			DRV_PINGROUP_ENTRY_Y(0x004c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_soc_gpio29_pq2			DRV_PINGROUP_ENTRY_Y(0x0054,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_soc_gpio30_pq3			DRV_PINGROUP_ENTRY_Y(0x005c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_soc_gpio31_pq4			DRV_PINGROUP_ENTRY_Y(0x0064,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_soc_gpio32_pq5			DRV_PINGROUP_ENTRY_Y(0x006c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_soc_gpio33_pq6			DRV_PINGROUP_ENTRY_Y(0x0074,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_soc_gpio35_pq7			DRV_PINGROUP_ENTRY_Y(0x007c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_soc_gpio37_pr0			DRV_PINGROUP_ENTRY_Y(0x0084,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_soc_gpio56_pr1			DRV_PINGROUP_ENTRY_Y(0x008c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_uart1_cts_pr5			DRV_PINGROUP_ENTRY_Y(0x0094,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_uart1_rts_pr4			DRV_PINGROUP_ENTRY_Y(0x009c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_uart1_rx_pr3			DRV_PINGROUP_ENTRY_Y(0x00a4,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_uart1_tx_pr2			DRV_PINGROUP_ENTRY_Y(0x00ac,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_cpu_pwr_req_pi5			DRV_PINGROUP_ENTRY_Y(0x4004,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_uart4_cts_ph6			DRV_PINGROUP_ENTRY_Y(0x400c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_uart4_rts_ph5			DRV_PINGROUP_ENTRY_Y(0x4014,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_uart4_rx_ph4			DRV_PINGROUP_ENTRY_Y(0x401c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_uart4_tx_ph3			DRV_PINGROUP_ENTRY_Y(0x4024,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_gen1_i2c_scl_pi3			DRV_PINGROUP_ENTRY_Y(0x402c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_gen1_i2c_sda_pi4			DRV_PINGROUP_ENTRY_Y(0x4034,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_soc_gpio20_pg7			DRV_PINGROUP_ENTRY_Y(0x403c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_soc_gpio21_ph0			DRV_PINGROUP_ENTRY_Y(0x4044,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_soc_gpio22_ph1			DRV_PINGROUP_ENTRY_Y(0x404c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_soc_gpio13_pg0			DRV_PINGROUP_ENTRY_Y(0x4054,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_soc_gpio14_pg1			DRV_PINGROUP_ENTRY_Y(0x405c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_soc_gpio15_pg2			DRV_PINGROUP_ENTRY_Y(0x4064,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_soc_gpio16_pg3			DRV_PINGROUP_ENTRY_Y(0x406c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_soc_gpio17_pg4			DRV_PINGROUP_ENTRY_Y(0x4074,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_soc_gpio18_pg5			DRV_PINGROUP_ENTRY_Y(0x407c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_soc_gpio19_pg6			DRV_PINGROUP_ENTRY_Y(0x4084,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_soc_gpio41_ph7			DRV_PINGROUP_ENTRY_Y(0x408c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_soc_gpio42_pi0			DRV_PINGROUP_ENTRY_Y(0x4094,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_soc_gpio43_pi1			DRV_PINGROUP_ENTRY_Y(0x409c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_soc_gpio44_pi2			DRV_PINGROUP_ENTRY_Y(0x40a4,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_soc_gpio06_ph2			DRV_PINGROUP_ENTRY_Y(0x40ac,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_soc_gpio07_pi6			DRV_PINGROUP_ENTRY_Y(0x40b4,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_dap4_sclk_pa4			DRV_PINGROUP_ENTRY_Y(0x2004,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_dap4_dout_pa5			DRV_PINGROUP_ENTRY_Y(0x200c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_dap4_din_pa6			DRV_PINGROUP_ENTRY_Y(0x2014,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_dap4_fs_pa7			DRV_PINGROUP_ENTRY_Y(0x201c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_dap6_sclk_pa0			DRV_PINGROUP_ENTRY_Y(0x2024,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_dap6_dout_pa1			DRV_PINGROUP_ENTRY_Y(0x202c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_dap6_din_pa2			DRV_PINGROUP_ENTRY_Y(0x2034,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_dap6_fs_pa3			DRV_PINGROUP_ENTRY_Y(0x203c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_soc_gpio45_pad0			DRV_PINGROUP_ENTRY_Y(0x18004,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_soc_gpio46_pad1			DRV_PINGROUP_ENTRY_Y(0x1800c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_soc_gpio47_pad2			DRV_PINGROUP_ENTRY_Y(0x18014,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_soc_gpio48_pad3			DRV_PINGROUP_ENTRY_Y(0x1801c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_soc_gpio57_pac4			DRV_PINGROUP_ENTRY_Y(0x18024,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_soc_gpio58_pac5			DRV_PINGROUP_ENTRY_Y(0x1802c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_soc_gpio59_pac6			DRV_PINGROUP_ENTRY_Y(0x18034,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_soc_gpio60_pac7			DRV_PINGROUP_ENTRY_Y(0x1803c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_spi5_cs0_pac3			DRV_PINGROUP_ENTRY_Y(0x18044,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_spi5_miso_pac1			DRV_PINGROUP_ENTRY_Y(0x1804c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_spi5_mosi_pac2			DRV_PINGROUP_ENTRY_Y(0x18054,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_spi5_sck_pac0			DRV_PINGROUP_ENTRY_Y(0x1805c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_eqos_td3_pe4			DRV_PINGROUP_ENTRY_N(no_entry)
#define	drive_eqos_td2_pe3			DRV_PINGROUP_ENTRY_N(no_entry)
#define	drive_eqos_td1_pe2			DRV_PINGROUP_ENTRY_N(no_entry)
#define	drive_eqos_td0_pe1			DRV_PINGROUP_ENTRY_N(no_entry)
#define	drive_eqos_rd3_pf1			DRV_PINGROUP_ENTRY_N(no_entry)
#define	drive_eqos_rd2_pf0			DRV_PINGROUP_ENTRY_N(no_entry)
#define	drive_eqos_rd1_pe7			DRV_PINGROUP_ENTRY_N(no_entry)
#define	drive_eqos_sma_mdio_pf4			DRV_PINGROUP_ENTRY_N(no_entry)
#define	drive_eqos_rd0_pe6			DRV_PINGROUP_ENTRY_N(no_entry)
#define	drive_eqos_sma_mdc_pf5			DRV_PINGROUP_ENTRY_N(no_entry)
#define	drive_eqos_comp				DRV_PINGROUP_ENTRY_N(no_entry)
#define	drive_eqos_txc_pe0			DRV_PINGROUP_ENTRY_N(no_entry)
#define	drive_eqos_rxc_pf3			DRV_PINGROUP_ENTRY_N(no_entry)
#define	drive_eqos_tx_ctl_pe5			DRV_PINGROUP_ENTRY_N(no_entry)
#define	drive_eqos_rx_ctl_pf2			DRV_PINGROUP_ENTRY_N(no_entry)
#define	drive_qspi0_io3_pc5			DRV_PINGROUP_ENTRY_N(no_entry)
#define	drive_qspi0_io2_pc4			DRV_PINGROUP_ENTRY_N(no_entry)
#define	drive_qspi0_io1_pc3			DRV_PINGROUP_ENTRY_N(no_entry)
#define	drive_qspi0_io0_pc2			DRV_PINGROUP_ENTRY_N(no_entry)
#define	drive_qspi0_sck_pc0			DRV_PINGROUP_ENTRY_N(no_entry)
#define	drive_qspi0_cs_n_pc1			DRV_PINGROUP_ENTRY_N(no_entry)
#define	drive_qspi1_io3_pd3			DRV_PINGROUP_ENTRY_N(no_entry)
#define	drive_qspi1_io2_pd2			DRV_PINGROUP_ENTRY_N(no_entry)
#define	drive_qspi1_io1_pd1			DRV_PINGROUP_ENTRY_N(no_entry)
#define	drive_qspi1_io0_pd0			DRV_PINGROUP_ENTRY_N(no_entry)
#define	drive_qspi1_sck_pc6			DRV_PINGROUP_ENTRY_N(no_entry)
#define	drive_qspi1_cs_n_pc7			DRV_PINGROUP_ENTRY_N(no_entry)
#define	drive_qspi_comp				DRV_PINGROUP_ENTRY_N(no_entry)
#define	drive_sdmmc1_comp			DRV_PINGROUP_ENTRY_N(no_entry)

#define PINGROUP(pg_name, f0, f1, f2, f3, r, bank, pupd, e_io_hv, e_lpbk, e_input, e_lpdr, e_pbias_buf,	\
			gpio_sfio_sel, schmitt_b)							\
	{								\
		.name = #pg_name,					\
		.pins = pg_name##_pins,					\
		.npins = ARRAY_SIZE(pg_name##_pins),			\
			.funcs = {					\
				TEGRA_MUX_##f0,				\
				TEGRA_MUX_##f1,				\
				TEGRA_MUX_##f2,				\
				TEGRA_MUX_##f3,				\
			},						\
		PIN_PINGROUP_ENTRY_Y(r, bank, pupd, e_io_hv, e_lpbk,	\
					e_input, e_lpdr, e_pbias_buf,	\
					gpio_sfio_sel, schmitt_b)	\
		drive_##pg_name,					\
	}

static const struct tegra_pingroup tegra234_groups[] = {
	PINGROUP(soc_gpio08_pb0,	RSVD0,		RSVD1,		RSVD2,		RSVD3,		0x5008,		0,	Y,	-1,	7,	6,	8,	-1,	10,	12),
	PINGROUP(soc_gpio36_pm5,	ETH0,		RSVD1,		DCA,		RSVD3,		0x10000,	0,	Y,	-1,	7,	6,	8,	-1,	10,	12),
	PINGROUP(soc_gpio53_pm6,	ETH0,		RSVD1,		DCA,		RSVD3,		0x10008,	0,	Y,	-1,	7,	6,	8,	-1,	10,	12),
	PINGROUP(soc_gpio55_pm4,	ETH2,		RSVD1,		RSVD2,		RSVD3,		0x10010,	0,	Y,	-1,	7,	6,	8,	-1,	10,	12),
	PINGROUP(soc_gpio38_pm7,	ETH1,		RSVD1,		RSVD2,		RSVD3,		0x10018,	0,	Y,	-1,	7,	6,	8,	-1,	10,	12),
	PINGROUP(soc_gpio39_pn1,	GP,		RSVD1,		RSVD2,		RSVD3,		0x10020,	0,	Y,	-1,	7,	6,	8,	-1,	10,	12),
	PINGROUP(soc_gpio40_pn2,	ETH1,		RSVD1,		RSVD2,		RSVD3,		0x10028,	0,	Y,	-1,	7,	6,	8,	-1,	10,	12),
	PINGROUP(dp_aux_ch0_hpd_pm0,	DP,		RSVD1,		RSVD2,		RSVD3,		0x10030,	0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(dp_aux_ch1_hpd_pm1,	ETH3,		RSVD1,		RSVD2,		RSVD3,		0x10038,	0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(dp_aux_ch2_hpd_pm2,	ETH3,		RSVD1,		DISPLAYB,	RSVD3,		0x10040,	0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(dp_aux_ch3_hpd_pm3,	ETH2,		RSVD1,		DISPLAYA,	RSVD3,		0x10048,	0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(dp_aux_ch1_p_pn3,	I2C4,		RSVD1,		RSVD2,		RSVD3,		0x10050,	0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(dp_aux_ch1_n_pn4,	I2C4,		RSVD1,		RSVD2,		RSVD3,		0x10058,	0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(dp_aux_ch2_p_pn5,	I2C7,		RSVD1,		RSVD2,		RSVD3,		0x10060,	0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(dp_aux_ch2_n_pn6,	I2C7,		RSVD1,		RSVD2,		RSVD3,		0x10068,	0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(dp_aux_ch3_p_pn7,	I2C9,		RSVD1,		RSVD2,		RSVD3,		0x10070,	0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(dp_aux_ch3_n_pn0,	I2C9,		RSVD1,		RSVD2,		RSVD3,		0x10078,	0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(eqos_td3_pe4,		EQOS,		RSVD1,		RSVD2,		RSVD3,		0x15000,	0,	Y,	-1,	5,	6,	-1,	-1,	10,	12),
	PINGROUP(eqos_td2_pe3,		EQOS,		RSVD1,		RSVD2,		RSVD3,		0x15008,	0,	Y,	-1,	5,	6,	-1,	-1,	10,	12),
	PINGROUP(eqos_td1_pe2,		EQOS,		RSVD1,		RSVD2,		RSVD3,		0x15010,	0,	Y,	-1,	5,	6,	-1,	-1,	10,	12),
	PINGROUP(eqos_td0_pe1,		EQOS,		RSVD1,		RSVD2,		RSVD3,		0x15018,	0,	Y,	-1,	5,	6,	-1,	-1,	10,	12),
	PINGROUP(eqos_rd3_pf1,		EQOS,		RSVD1,		RSVD2,		RSVD3,		0x15020,	0,	Y,	-1,	5,	6,	-1,	-1,	10,	12),
	PINGROUP(eqos_rd2_pf0,		EQOS,		RSVD1,		RSVD2,		RSVD3,		0x15028,	0,	Y,	-1,	5,	6,	-1,	-1,	10,	12),
	PINGROUP(eqos_rd1_pe7,		EQOS,		RSVD1,		RSVD2,		RSVD3,		0x15030,	0,	Y,	-1,	5,	6,	-1,	-1,	10,	12),
	PINGROUP(eqos_sma_mdio_pf4,	EQOS,		RSVD1,		RSVD2,		RSVD3,		0x15038,	0,	Y,	-1,	5,	6,	-1,	-1,	10,	12),
	PINGROUP(eqos_rd0_pe6,		EQOS,		RSVD1,		RSVD2,		RSVD3,		0x15040,	0,	Y,	-1,	5,	6,	-1,	-1,	10,	12),
	PINGROUP(eqos_sma_mdc_pf5,	EQOS,		RSVD1,		RSVD2,		RSVD3,		0x15048,	0,	Y,	-1,	5,	6,	-1,	-1,	10,	12),
	PINGROUP(eqos_comp,		EQOS,		RSVD1,		RSVD2,		RSVD3,		0x15050,	0,	N,	-1,	-1,	-1,	-1,	-1,	-1,	-1),
	PINGROUP(eqos_txc_pe0,		EQOS,		RSVD1,		RSVD2,		RSVD3,		0x15058,	0,	Y,	-1,	5,	6,	-1,	-1,	10,	12),
	PINGROUP(eqos_rxc_pf3,		EQOS,		RSVD1,		RSVD2,		RSVD3,		0x15060,	0,	Y,	-1,	5,	6,	-1,	-1,	10,	12),
	PINGROUP(eqos_tx_ctl_pe5,	EQOS,		RSVD1,		RSVD2,		RSVD3,		0x15068,	0,	Y,	-1,	5,	6,	-1,	-1,	10,	12),
	PINGROUP(eqos_rx_ctl_pf2,	EQOS,		RSVD1,		RSVD2,		RSVD3,		0x15070,	0,	Y,	-1,	5,	6,	-1,	-1,	10,	12),
	PINGROUP(pex_l2_clkreq_n_pk4,	PE2,		RSVD1,		RSVD2,		RSVD3,		0x7000,		0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(pex_wake_n_pl2,	RSVD0,		RSVD1,		RSVD2,		RSVD3,		0x7008,		0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(pex_l1_clkreq_n_pk2,	PE1,		RSVD1,		RSVD2,		RSVD3,		0x7010,		0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(pex_l1_rst_n_pk3,	PE1,		RSVD1,		RSVD2,		RSVD3,		0x7018,		0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(pex_l0_clkreq_n_pk0,	PE0,		RSVD1,		RSVD2,		RSVD3,		0x7020,		0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(pex_l0_rst_n_pk1,	PE0,		RSVD1,		RSVD2,		RSVD3,		0x7028,		0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(pex_l2_rst_n_pk5,	PE2,		RSVD1,		RSVD2,		RSVD3,		0x7030,		0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(pex_l3_clkreq_n_pk6,	PE3,		RSVD1,		RSVD2,		RSVD3,		0x7038,		0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(pex_l3_rst_n_pk7,	PE3,		RSVD1,		RSVD2,		RSVD3,		0x7040,		0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(pex_l4_clkreq_n_pl0,	PE4,		RSVD1,		RSVD2,		RSVD3,		0x7048,		0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(pex_l4_rst_n_pl1,	PE4,		RSVD1,		RSVD2,		RSVD3,		0x7050,		0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(soc_gpio34_pl3,	RSVD0,		RSVD1,		RSVD2,		RSVD3,		0x7058,		0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(pex_l5_clkreq_n_paf0,	PE5,		RSVD1,		RSVD2,		RSVD3,		0x14000,	0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(pex_l5_rst_n_paf1,	PE5,		RSVD1,		RSVD2,		RSVD3,		0x14008,	0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(pex_l6_clkreq_n_paf2,  PE6,		RSVD1,		RSVD2,		RSVD3,		0x14010,	0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(pex_l6_rst_n_paf3,	PE6,		RSVD1,		RSVD2,		RSVD3,		0x14018,	0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(pex_l10_clkreq_n_pag6,	PE10,		RSVD1,		RSVD2,		RSVD3,		0x19000,	0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(pex_l10_rst_n_pag7,	PE10,		RSVD1,		RSVD2,		RSVD3,		0x19008,	0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(pex_l7_clkreq_n_pag0,	PE7,		RSVD1,		RSVD2,		RSVD3,		0x19010,	0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(pex_l7_rst_n_pag1,	PE7,		RSVD1,		RSVD2,		RSVD3,		0x19018,	0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(pex_l8_clkreq_n_pag2,	PE8,		RSVD1,		RSVD2,		RSVD3,		0x19020,	0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(pex_l8_rst_n_pag3,	PE8,		RSVD1,		RSVD2,		RSVD3,		0x19028,	0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(pex_l9_clkreq_n_pag4,	PE9,		RSVD1,		RSVD2,		RSVD3,		0x19030,	0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(pex_l9_rst_n_pag5,	PE9,		RSVD1,		RSVD2,		RSVD3,		0x19038,	0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(qspi0_io3_pc5,		QSPI0,		RSVD1,		RSVD2,		RSVD3,		0xB000,		0,	Y,	-1,	5,	6,	-1,	-1,	10,	12),
	PINGROUP(qspi0_io2_pc4,		QSPI0,		RSVD1,		RSVD2,		RSVD3,		0xB008,		0,	Y,	-1,	5,	6,	-1,	-1,	10,	12),
	PINGROUP(qspi0_io1_pc3,		QSPI0,		RSVD1,		RSVD2,		RSVD3,		0xB010,		0,	Y,	-1,	5,	6,	-1,	-1,	10,	12),
	PINGROUP(qspi0_io0_pc2,		QSPI0,		RSVD1,		RSVD2,		RSVD3,		0xB018,		0,	Y,	-1,	5,	6,	-1,	-1,	10,	12),
	PINGROUP(qspi0_sck_pc0,		QSPI0,		RSVD1,		RSVD2,		RSVD3,		0xB020,		0,	Y,	-1,	5,	6,	-1,	-1,	10,	12),
	PINGROUP(qspi0_cs_n_pc1,	QSPI0,		RSVD1,		RSVD2,		RSVD3,		0xB028,		0,	Y,	-1,	5,	6,	-1,	-1,	10,	12),
	PINGROUP(qspi1_io3_pd3,		QSPI1,		RSVD1,		RSVD2,		RSVD3,		0xB030,		0,	Y,	-1,	5,	6,	-1,	-1,	10,	12),
	PINGROUP(qspi1_io2_pd2,		QSPI1,		RSVD1,		RSVD2,		RSVD3,		0xB038,		0,	Y,	-1,	5,	6,	-1,	-1,	10,	12),
	PINGROUP(qspi1_io1_pd1,		QSPI1,		RSVD1,		RSVD2,		RSVD3,		0xB040,		0,	Y,	-1,	5,	6,	-1,	-1,	10,	12),
	PINGROUP(qspi1_io0_pd0,		QSPI1,		RSVD1,		RSVD2,		RSVD3,		0xB048,		0,	Y,	-1,	5,	6,	-1,	-1,	10,	12),
	PINGROUP(qspi1_sck_pc6,		QSPI1,		RSVD1,		RSVD2,		RSVD3,		0xB050,		0,	Y,	-1,	5,	6,	-1,	-1,	10,	12),
	PINGROUP(qspi1_cs_n_pc7,	QSPI1,		RSVD1,		RSVD2,		RSVD3,		0xB058,		0,	Y,	-1,	5,	6,	-1,	-1,	10,	12),
	PINGROUP(qspi_comp,		QSPI,		RSVD1,		RSVD2,		RSVD3,		0xB060,		0,	N,	-1,	-1,	-1,	-1,	-1,	-1,	-1),
	PINGROUP(sdmmc1_clk_pj0,	SDMMC1,		RSVD1,		RSVD2,		RSVD3,		0x8000,		0,	Y,	-1,	5,	6,	-1,	9,	10,	12),
	PINGROUP(sdmmc1_cmd_pj1,	SDMMC1,		RSVD1,		RSVD2,		RSVD3,		0x8008,		0,	Y,	-1,	5,	6,	-1,	9,	10,	12),
	PINGROUP(sdmmc1_comp,		SDMMC1,		RSVD1,		RSVD2,		RSVD3,		0x8010,		0,	N,	-1,	-1,	-1,	-1,	-1,	-1,	-1),
	PINGROUP(sdmmc1_dat3_pj5,	SDMMC1,		RSVD1,		RSVD2,		RSVD3,		0x8018,		0,	Y,	-1,	5,	6,	-1,	9,	10,	12),
	PINGROUP(sdmmc1_dat2_pj4,	SDMMC1,		RSVD1,		RSVD2,		RSVD3,		0x8020,		0,	Y,	-1,	5,	6,	-1,	9,	10,	12),
	PINGROUP(sdmmc1_dat1_pj3,	SDMMC1,		RSVD1,		RSVD2,		RSVD3,		0x8028,		0,	Y,	-1,	5,	6,	-1,	9,	10,	12),
	PINGROUP(sdmmc1_dat0_pj2,	SDMMC1,		RSVD1,		RSVD2,		RSVD3,		0x8030,		0,	Y,	-1,	5,	6,	-1,	9,	10,	12),
	PINGROUP(ufs0_rst_n_pae1,	UFS0,		RSVD1,		RSVD2,		RSVD3,		0x11000,	0,	Y,	-1,	5,	6,	-1,	-1,	10,	12),
	PINGROUP(ufs0_ref_clk_pae0,	UFS0,		RSVD1,		RSVD2,		RSVD3,		0x11008,	0,	Y,	-1,	5,	6,	-1,	-1,	10,	12),
	PINGROUP(spi3_miso_py1,		SPI3,		RSVD1,		RSVD2,		RSVD3,		0xD000,		0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(spi1_cs0_pz6,		SPI1,		RSVD1,		RSVD2,		RSVD3,		0xD008,		0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(spi3_cs0_py3,		SPI3,		RSVD1,		RSVD2,		RSVD3,		0xD010,		0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(spi1_miso_pz4,		SPI1,		RSVD1,		RSVD2,		RSVD3,		0xD018,		0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(spi3_cs1_py4,		SPI3,		RSVD1,		RSVD2,		RSVD3,		0xD020,		0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(spi1_sck_pz3,		SPI1,		RSVD1,		RSVD2,		RSVD3,		0xD028,		0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(spi3_sck_py0,		SPI3,		RSVD1,		RSVD2,		RSVD3,		0xD030,		0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(spi1_cs1_pz7,		SPI1,		RSVD1,		RSVD2,		RSVD3,		0xD038,		0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(spi1_mosi_pz5,		SPI1,		RSVD1,		RSVD2,		RSVD3,		0xD040,		0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(spi3_mosi_py2,		SPI3,		RSVD1,		RSVD2,		RSVD3,		0xD048,		0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(uart2_tx_px4,		UARTB,		RSVD1,		RSVD2,		RSVD3,		0xD050,		0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(uart2_rx_px5,		UARTB,		RSVD1,		RSVD2,		RSVD3,		0xD058,		0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(uart2_rts_px6,		UARTB,		RSVD1,		RSVD2,		RSVD3,		0xD060,		0,	Y,	-1,	7,	6,	8,	-1,	10,	12),
	PINGROUP(uart2_cts_px7,		UARTB,		RSVD1,		RSVD2,		RSVD3,		0xD068,		0,	Y,	-1,	7,	6,	8,	-1,	10,	12),
	PINGROUP(uart5_tx_py5,		UARTE,		RSVD1,		RSVD2,		RSVD3,		0xD070,		0,	Y,	-1,	7,	6,	8,	-1,	10,	12),
	PINGROUP(uart5_rx_py6,		UARTE,		RSVD1,		RSVD2,		RSVD3,		0xD078,		0,	Y,	-1,	7,	6,	8,	-1,	10,	12),
	PINGROUP(uart5_rts_py7,		UARTE,		RSVD1,		RSVD2,		RSVD3,		0xD080,		0,	Y,	-1,	7,	6,	8,	-1,	10,	12),
	PINGROUP(uart5_cts_pz0,		UARTE,		RSVD1,		RSVD2,		RSVD3,		0xD088,		0,	Y,	-1,	7,	6,	8,	-1,	10,	12),
	PINGROUP(gpu_pwr_req_px0,	RSVD0,		RSVD1,		RSVD2,		RSVD3,		0xD090,		0,	Y,	-1,	7,	6,	8,	-1,	10,	12),
	PINGROUP(gp_pwm3_px3,		GP,		RSVD1,		RSVD2,		RSVD3,		0xD098,		0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(gp_pwm2_px2,		GP,		RSVD1,		RSVD2,		RSVD3,		0xD0A0,		0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(cv_pwr_req_px1,	RSVD0,		RSVD1,		RSVD2,		RSVD3,		0xD0A8,		0,	Y,	-1,	7,	6,	8,	-1,	10,	12),
	PINGROUP(usb_vbus_en0_pz1,	USB,		RSVD1,		RSVD2,		RSVD3,		0xD0B0,		0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(usb_vbus_en1_pz2,	USB,		RSVD1,		RSVD2,		RSVD3,		0xD0B8,		0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(extperiph2_clk_pp1,	EXTPERIPH2,	RSVD1,		RSVD2,		RSVD3,		0x0000,		0,	Y,	-1,	7,	6,	8,	-1,	10,	12),
	PINGROUP(extperiph1_clk_pp0,	EXTPERIPH1,	RSVD1,		RSVD2,		RSVD3,		0x0008,		0,	Y,	-1,	7,	6,	8,	-1,	10,	12),
	PINGROUP(cam_i2c_sda_pp3,	I2C3,		VI0,		RSVD2,		VI1,		0x0010,		0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(cam_i2c_scl_pp2,	I2C3,		VI0,		VI0_ALT,	VI1,		0x0018,		0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(soc_gpio23_pp4,	VI0,		VI0_ALT,	VI1,		VI1_ALT,	0x0020,		0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(soc_gpio24_pp5,	VI0,		SOC,		VI1,		VI1_ALT,	0x0028,		0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(soc_gpio25_pp6,	VI0,		I2S5,		VI1,		DMIC1,		0x0030,		0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(pwr_i2c_scl_pp7,	I2C5,		RSVD1,		RSVD2,		RSVD3,		0x0038,		0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(pwr_i2c_sda_pq0,	I2C5,		RSVD1,		RSVD2,		RSVD3,		0x0040,		0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(soc_gpio28_pq1,	VI0,		RSVD1,		VI1,		RSVD3,		0x0048,		0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(soc_gpio29_pq2,	RSVD0,		NV,		RSVD2,		RSVD3,		0x0050,		0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(soc_gpio30_pq3,	RSVD0,		WDT,		RSVD2,		RSVD3,		0x0058,		0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(soc_gpio31_pq4,	RSVD0,		RSVD1,		RSVD2,		RSVD3,		0x0060,		0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(soc_gpio32_pq5,	RSVD0,		EXTPERIPH3,	DCB,		RSVD3,		0x0068,		0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(soc_gpio33_pq6,	RSVD0,		EXTPERIPH4,	DCB,		RSVD3,		0x0070,		0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(soc_gpio35_pq7,	RSVD0,		I2S5,		DMIC1,		RSVD3,		0x0078,		0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(soc_gpio37_pr0,	GP,		I2S5,		DMIC4,		DSPK1,		0x0080,		0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(soc_gpio56_pr1,	RSVD0,		I2S5,		DMIC4,		DSPK1,		0x0088,		0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(uart1_cts_pr5,		UARTA,		RSVD1,		RSVD2,		RSVD3,		0x0090,		0,	Y,	-1,	7,	6,	8,	-1,	10,	12),
	PINGROUP(uart1_rts_pr4,		UARTA,		RSVD1,		RSVD2,		RSVD3,		0x0098,		0,	Y,	-1,	7,	6,	8,	-1,	10,	12),
	PINGROUP(uart1_rx_pr3,		UARTA,		RSVD1,		RSVD2,		RSVD3,		0x00A0,		0,	Y,	-1,	7,	6,	8,	-1,	10,	12),
	PINGROUP(uart1_tx_pr2,		UARTA,		RSVD1,		RSVD2,		RSVD3,		0x00A8,		0,	Y,	-1,	7,	6,	8,	-1,	10,	12),
	PINGROUP(cpu_pwr_req_pi5,	RSVD0,		RSVD1,		RSVD2,		RSVD3,		0x4000,		0,	Y,	-1,	7,	6,	8,	-1,	10,	12),
	PINGROUP(uart4_cts_ph6,		UARTD,		RSVD1,		I2S7,		RSVD3,		0x4008,		0,	Y,	-1,	7,	6,	8,	-1,	10,	12),
	PINGROUP(uart4_rts_ph5,		UARTD,		SPI4,		RSVD2,		RSVD3,		0x4010,		0,	Y,	-1,	7,	6,	8,	-1,	10,	12),
	PINGROUP(uart4_rx_ph4,		UARTD,		RSVD1,		I2S7,		RSVD3,		0x4018,		0,	Y,	-1,	7,	6,	8,	-1,	10,	12),
	PINGROUP(uart4_tx_ph3,		UARTD,		SPI4,		RSVD2,		RSVD3,		0x4020,		0,	Y,	-1,	7,	6,	8,	-1,	10,	12),
	PINGROUP(gen1_i2c_scl_pi3,	I2C1,		RSVD1,		RSVD2,		RSVD3,		0x4028,		0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(gen1_i2c_sda_pi4,	I2C1,		RSVD1,		RSVD2,		RSVD3,		0x4030,		0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(soc_gpio20_pg7,	RSVD0,		SDMMC1,		RSVD2,		RSVD3,		0x4038,		0,	Y,	-1,	7,	6,	8,	-1,	10,	12),
	PINGROUP(soc_gpio21_ph0,	RSVD0,		GP,		I2S7,		RSVD3,		0x4040,		0,	Y,	-1,	7,	6,	8,	-1,	10,	12),
	PINGROUP(soc_gpio22_ph1,	RSVD0,		RSVD1,		I2S7,		RSVD3,		0x4048,		0,	Y,	-1,	7,	6,	8,	-1,	10,	12),
	PINGROUP(soc_gpio13_pg0,	RSVD0,		RSVD1,		RSVD2,		RSVD3,		0x4050,		0,	Y,	-1,	7,	6,	8,	-1,	10,	12),
	PINGROUP(soc_gpio14_pg1,	RSVD0,		SPI4,		RSVD2,		RSVD3,		0x4058,		0,	Y,	-1,	7,	6,	8,	-1,	10,	12),
	PINGROUP(soc_gpio15_pg2,	RSVD0,		SPI4,		RSVD2,		RSVD3,		0x4060,		0,	Y,	-1,	7,	6,	8,	-1,	10,	12),
	PINGROUP(soc_gpio16_pg3,	RSVD0,		SPI4,		RSVD2,		RSVD3,		0x4068,		0,	Y,	-1,	7,	6,	8,	-1,	10,	12),
	PINGROUP(soc_gpio17_pg4,	RSVD0,		CCLA,		RSVD2,		RSVD3,		0x4070,		0,	Y,	-1,	7,	6,	8,	-1,	10,	12),
	PINGROUP(soc_gpio18_pg5,	RSVD0,		RSVD1,		RSVD2,		RSVD3,		0x4078,		0,	Y,	-1,	7,	6,	8,	-1,	10,	12),
	PINGROUP(soc_gpio19_pg6,	GP,		RSVD1,		RSVD2,		RSVD3,		0x4080,		0,	Y,	-1,	7,	6,	8,	-1,	10,	12),
	PINGROUP(soc_gpio41_ph7,	RSVD0,		I2S2,		RSVD2,		RSVD3,		0x4088,		0,	Y,	-1,	7,	6,	8,	-1,	10,	12),
	PINGROUP(soc_gpio42_pi0,	RSVD0,		I2S2,		RSVD2,		RSVD3,		0x4090,		0,	Y,	-1,	7,	6,	8,	-1,	10,	12),
	PINGROUP(soc_gpio43_pi1,	RSVD0,		I2S2,		RSVD2,		RSVD3,		0x4098,		0,	Y,	-1,	7,	6,	8,	-1,	10,	12),
	PINGROUP(soc_gpio44_pi2,	RSVD0,		I2S2,		RSVD2,		RSVD3,		0x40A0,		0,	Y,	-1,	7,	6,	8,	-1,	10,	12),
	PINGROUP(soc_gpio06_ph2,	RSVD0,		RSVD1,		RSVD2,		RSVD3,		0x40A8,		0,	Y,	-1,	7,	6,	8,	-1,	10,	12),
	PINGROUP(soc_gpio07_pi6,	GP,		RSVD1,		RSVD2,		RSVD3,		0x40B0,		0,	Y,	-1,	7,	6,	8,	-1,	10,	12),
	PINGROUP(dap4_sclk_pa4,		I2S4,		RSVD1,		RSVD2,		RSVD3,		0x2000,		0,	Y,	-1,	7,	6,	8,	-1,	10,	12),
	PINGROUP(dap4_dout_pa5,		I2S4,		RSVD1,		RSVD2,		RSVD3,		0x2008,		0,	Y,	-1,	7,	6,	8,	-1,	10,	12),
	PINGROUP(dap4_din_pa6,		I2S4,		RSVD1,		RSVD2,		RSVD3,		0x2010,		0,	Y,	-1,	7,	6,	8,	-1,	10,	12),
	PINGROUP(dap4_fs_pa7,		I2S4,		RSVD1,		RSVD2,		RSVD3,		0x2018,		0,	Y,	-1,	7,	6,	8,	-1,	10,	12),
	PINGROUP(dap6_sclk_pa0,		I2S6,		RSVD1,		RSVD2,		RSVD3,		0x2020,		0,	Y,	-1,	7,	6,	8,	-1,	10,	12),
	PINGROUP(dap6_dout_pa1,		I2S6,		RSVD1,		RSVD2,		RSVD3,		0x2028,		0,	Y,	-1,	7,	6,	8,	-1,	10,	12),
	PINGROUP(dap6_din_pa2,		I2S6,		RSVD1,		RSVD2,		RSVD3,		0x2030,		0,	Y,	-1,	7,	6,	8,	-1,	10,	12),
	PINGROUP(dap6_fs_pa3,		I2S6,		RSVD1,		RSVD2,		RSVD3,		0x2038,		0,	Y,	-1,	7,	6,	8,	-1,	10,	12),
	PINGROUP(soc_gpio45_pad0,	RSVD0,		I2S1,		RSVD2,		RSVD3,		0x18000,	0,	Y,	-1,	7,	6,	8,	-1,	10,	12),
	PINGROUP(soc_gpio46_pad1,	RSVD0,		I2S1,		RSVD2,		RSVD3,		0x18008,	0,	Y,	-1,	7,	6,	8,	-1,	10,	12),
	PINGROUP(soc_gpio47_pad2,	RSVD0,		I2S1,		RSVD2,		RSVD3,		0x18010,	0,	Y,	-1,	7,	6,	8,	-1,	10,	12),
	PINGROUP(soc_gpio48_pad3,	RSVD0,		I2S1,		RSVD2,		RSVD3,		0x18018,	0,	Y,	-1,	7,	6,	8,	-1,	10,	12),
	PINGROUP(soc_gpio57_pac4,	RSVD0,		I2S8,		RSVD2,		SDMMC1,		0x18020,	0,	Y,	-1,	7,	6,	8,	-1,	10,	12),
	PINGROUP(soc_gpio58_pac5,	RSVD0,		I2S8,		RSVD2,		SDMMC1,		0x18028,	0,	Y,	-1,	7,	6,	8,	-1,	10,	12),
	PINGROUP(soc_gpio59_pac6,	AUD,		I2S8,		RSVD2,		RSVD3,		0x18030,	0,	Y,	-1,	7,	6,	8,	-1,	10,	12),
	PINGROUP(soc_gpio60_pac7,	RSVD0,		I2S8,		NV,		IGPU,		0x18038,	0,	Y,	-1,	7,	6,	8,	-1,	10,	12),
	PINGROUP(spi5_cs0_pac3,		SPI5,		I2S3,		DMIC2,		RSVD3,		0x18040,	0,	Y,	-1,	7,	6,	8,	-1,	10,	12),
	PINGROUP(spi5_miso_pac1,	SPI5,		I2S3,		DSPK0,		RSVD3,		0x18048,	0,	Y,	-1,	7,	6,	8,	-1,	10,	12),
	PINGROUP(spi5_mosi_pac2,	SPI5,		I2S3,		DMIC2,		RSVD3,		0x18050,	0,	Y,	-1,	7,	6,	8,	-1,	10,	12),
	PINGROUP(spi5_sck_pac0,		SPI5,		I2S3,		DSPK0,		RSVD3,		0x18058,	0,	Y,	-1,	7,	6,	8,	-1,	10,	12),

};

static const struct tegra_pinctrl_soc_data tegra234_pinctrl = {
	.pins = tegra234_pins,
	.npins = ARRAY_SIZE(tegra234_pins),
	.functions = tegra234_functions,
	.nfunctions = ARRAY_SIZE(tegra234_functions),
	.groups = tegra234_groups,
	.ngroups = ARRAY_SIZE(tegra234_groups),
	.hsm_in_mux = false,
	.schmitt_in_mux = true,
	.drvtype_in_mux = true,
	.sfsel_in_mux = true,
};

static const struct pinctrl_pin_desc tegra234_aon_pins[] = {
	PINCTRL_PIN(TEGRA_PIN_CAN0_DOUT_PAA0, "CAN0_DOUT_PAA0"),
	PINCTRL_PIN(TEGRA_PIN_CAN0_DIN_PAA1, "CAN0_DIN_PAA1"),
	PINCTRL_PIN(TEGRA_PIN_CAN1_DOUT_PAA2, "CAN1_DOUT_PAA2"),
	PINCTRL_PIN(TEGRA_PIN_CAN1_DIN_PAA3, "CAN1_DIN_PAA3"),
	PINCTRL_PIN(TEGRA_PIN_CAN0_STB_PAA4, "CAN0_STB_PAA4"),
	PINCTRL_PIN(TEGRA_PIN_CAN0_EN_PAA5, "CAN0_EN_PAA5"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO49_PAA6, "SOC_GPIO49_PAA6"),
	PINCTRL_PIN(TEGRA_PIN_CAN0_ERR_PAA7, "CAN0_ERR_PAA7"),
	PINCTRL_PIN(TEGRA_PIN_CAN1_STB_PBB0, "CAN1_STB_PBB0"),
	PINCTRL_PIN(TEGRA_PIN_CAN1_EN_PBB1, "CAN1_EN_PBB1"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO50_PBB2, "SOC_GPIO50_PBB2"),
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
	PINCTRL_PIN(TEGRA_PIN_SCE_ERROR_PEE0, "SCE_ERROR_PEE0"),
	PINCTRL_PIN(TEGRA_PIN_VCOMP_ALERT_PEE1, "VCOMP_ALERT_PEE1"),
	PINCTRL_PIN(TEGRA_PIN_AO_RETENTION_N_PEE2, "AO_RETENTION_N_PEE2"),
	PINCTRL_PIN(TEGRA_PIN_BATT_OC_PEE3, "BATT_OC_PEE3"),
	PINCTRL_PIN(TEGRA_PIN_POWER_ON_PEE4, "POWER_ON_PEE4"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO26_PEE5, "SOC_GPIO26_PEE5"),
	PINCTRL_PIN(TEGRA_PIN_SOC_GPIO27_PEE6, "SOC_GPIO27_PEE6"),
	PINCTRL_PIN(TEGRA_PIN_BOOTV_CTL_N_PEE7, "BOOTV_CTL_N_PEE7"),
	PINCTRL_PIN(TEGRA_PIN_HDMI_CEC_PGG0, "HDMI_CEC_PGG0"),
};

/* AON drive pin groups */
#define	drive_touch_clk_pcc4			DRV_PINGROUP_ENTRY_Y(0x2004,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_uart3_rx_pcc6			DRV_PINGROUP_ENTRY_Y(0x200c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_uart3_tx_pcc5			DRV_PINGROUP_ENTRY_Y(0x2014,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_gen8_i2c_sda_pdd2			DRV_PINGROUP_ENTRY_Y(0x201c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_gen8_i2c_scl_pdd1			DRV_PINGROUP_ENTRY_Y(0x2024,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_spi2_mosi_pcc2			DRV_PINGROUP_ENTRY_Y(0x202c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_gen2_i2c_scl_pcc7			DRV_PINGROUP_ENTRY_Y(0x2034,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_spi2_cs0_pcc3			DRV_PINGROUP_ENTRY_Y(0x203c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_gen2_i2c_sda_pdd0			DRV_PINGROUP_ENTRY_Y(0x2044,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_spi2_sck_pcc0			DRV_PINGROUP_ENTRY_Y(0x204c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_spi2_miso_pcc1			DRV_PINGROUP_ENTRY_Y(0x2054,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_can1_dout_paa2			DRV_PINGROUP_ENTRY_Y(0x3004,	28,	2,	30,	2,	-1,	-1,	-1,	-1,	0)
#define	drive_can1_din_paa3			DRV_PINGROUP_ENTRY_Y(0x300c,	28,	2,	30,	2,	-1,	-1,	-1,	-1,	0)
#define	drive_can0_dout_paa0			DRV_PINGROUP_ENTRY_Y(0x3014,	28,	2,	30,	2,	-1,	-1,	-1,	-1,	0)
#define	drive_can0_din_paa1			DRV_PINGROUP_ENTRY_Y(0x301c,	28,	2,	30,	2,	-1,	-1,	-1,	-1,	0)
#define	drive_can0_stb_paa4			DRV_PINGROUP_ENTRY_Y(0x3024,	28,	2,	30,	2,	-1,	-1,	-1,	-1,	0)
#define	drive_can0_en_paa5			DRV_PINGROUP_ENTRY_Y(0x302c,	28,	2,	30,	2,	-1,	-1,	-1,	-1,	0)
#define	drive_soc_gpio49_paa6			DRV_PINGROUP_ENTRY_Y(0x3034,	28,	2,	30,	2,	-1,	-1,	-1,	-1,	0)
#define	drive_can0_err_paa7			DRV_PINGROUP_ENTRY_Y(0x303c,	28,	2,	30,	2,	-1,	-1,	-1,	-1,	0)
#define	drive_can1_stb_pbb0			DRV_PINGROUP_ENTRY_Y(0x3044,	28,	2,	30,	2,	-1,	-1,	-1,	-1,	0)
#define	drive_can1_en_pbb1			DRV_PINGROUP_ENTRY_Y(0x304c,	28,	2,	30,	2,	-1,	-1,	-1,	-1,	0)
#define	drive_soc_gpio50_pbb2			DRV_PINGROUP_ENTRY_Y(0x3054,	28,	2,	30,	2,	-1,	-1,	-1,	-1,	0)
#define	drive_can1_err_pbb3			DRV_PINGROUP_ENTRY_Y(0x305c,	28,	2,	30,	2,	-1,	-1,	-1,	-1,	0)
#define	drive_sce_error_pee0			DRV_PINGROUP_ENTRY_Y(0x1014,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_batt_oc_pee3			DRV_PINGROUP_ENTRY_Y(0x1024,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_bootv_ctl_n_pee7			DRV_PINGROUP_ENTRY_Y(0x102c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_power_on_pee4			DRV_PINGROUP_ENTRY_Y(0x103c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_soc_gpio26_pee5			DRV_PINGROUP_ENTRY_Y(0x1044,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_soc_gpio27_pee6			DRV_PINGROUP_ENTRY_Y(0x104c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_ao_retention_n_pee2		DRV_PINGROUP_ENTRY_Y(0x1054,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_vcomp_alert_pee1			DRV_PINGROUP_ENTRY_Y(0x105c,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)
#define	drive_hdmi_cec_pgg0			DRV_PINGROUP_ENTRY_Y(0x1064,	12,	5,	20,	5,	-1,	-1,	-1,	-1,	0)

static const struct tegra_pingroup tegra234_aon_groups[] = {
	PINGROUP(touch_clk_pcc4,	GP,		TOUCH,		RSVD2,		RSVD3,		0x2000,		0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(uart3_rx_pcc6,		UARTC,		UARTJ,		RSVD2,		RSVD3,		0x2008,		0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(uart3_tx_pcc5,		UARTC,		UARTJ,		RSVD2,		RSVD3,		0x2010,		0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(gen8_i2c_sda_pdd2,	I2C8,		RSVD1,		RSVD2,		RSVD3,		0x2018,		0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(gen8_i2c_scl_pdd1,	I2C8,		RSVD1,		RSVD2,		RSVD3,		0x2020,		0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(spi2_mosi_pcc2,	SPI2,		RSVD1,		RSVD2,		RSVD3,		0x2028,		0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(gen2_i2c_scl_pcc7,	I2C2,		RSVD1,		RSVD2,		RSVD3,		0x2030,		0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(spi2_cs0_pcc3,		SPI2,		RSVD1,		RSVD2,		RSVD3,		0x2038,		0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(gen2_i2c_sda_pdd0,	I2C2,		RSVD1,		RSVD2,		RSVD3,		0x2040,		0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(spi2_sck_pcc0,		SPI2,		RSVD1,		RSVD2,		RSVD3,		0x2048,		0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(spi2_miso_pcc1,	SPI2,		RSVD1,		RSVD2,		RSVD3,		0x2050,		0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(can1_dout_paa2,	CAN1,		RSVD1,		RSVD2,		RSVD3,		0x3000,		0,	Y,	-1,	5,	6,	-1,	9,	10,	12),
	PINGROUP(can1_din_paa3,		CAN1,		RSVD1,		RSVD2,		RSVD3,		0x3008,		0,	Y,	-1,	5,	6,	-1,	9,	10,	12),
	PINGROUP(can0_dout_paa0,	CAN0,		RSVD1,		RSVD2,		RSVD3,		0x3010,		0,	Y,	-1,	5,	6,	-1,	9,	10,	12),
	PINGROUP(can0_din_paa1,		CAN0,		RSVD1,		RSVD2,		RSVD3,		0x3018,		0,	Y,	-1,	5,	6,	-1,	9,	10,	12),
	PINGROUP(can0_stb_paa4,		RSVD0,		WDT,		TSC,		TSC_ALT,	0x3020,		0,	Y,	-1,	5,	6,	-1,	9,	10,	12),
	PINGROUP(can0_en_paa5,		RSVD0,		RSVD1,		RSVD2,		RSVD3,		0x3028,		0,	Y,	-1,	5,	6,	-1,	9,	10,	12),
	PINGROUP(soc_gpio49_paa6,	RSVD0,		RSVD1,		RSVD2,		RSVD3,		0x3030,		0,	Y,	-1,	5,	6,	-1,	9,	10,	12),
	PINGROUP(can0_err_paa7,		RSVD0,		TSC,		RSVD2,		TSC_ALT,	0x3038,		0,	Y,	-1,	5,	6,	-1,	9,	10,	12),
	PINGROUP(can1_stb_pbb0,		RSVD0,		DMIC3,		DMIC5,		RSVD3,		0x3040,		0,	Y,	-1,	5,	6,	-1,	9,	10,	12),
	PINGROUP(can1_en_pbb1,		RSVD0,		DMIC3,		DMIC5,		RSVD3,		0x3048,		0,	Y,	-1,	5,	6,	-1,	9,	10,	12),
	PINGROUP(soc_gpio50_pbb2,	RSVD0,		TSC,		RSVD2,		TSC_ALT,	0x3050,		0,	Y,	-1,	5,	6,	-1,	9,	10,	12),
	PINGROUP(can1_err_pbb3,		RSVD0,		TSC,		RSVD2,		TSC_ALT,	0x3058,		0,	Y,	-1,	5,	6,	-1,	9,	10,	12),
	PINGROUP(sce_error_pee0,	SCE,		RSVD1,		RSVD2,		RSVD3,		0x1010,		0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(batt_oc_pee3,		SOC,		RSVD1,		RSVD2,		RSVD3,		0x1020,		0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(bootv_ctl_n_pee7,	RSVD0,		RSVD1,		RSVD2,		RSVD3,		0x1028,		0,	Y,	-1,	7,	6,	8,	-1,	10,	12),
	PINGROUP(power_on_pee4,		RSVD0,		RSVD1,		RSVD2,		RSVD3,		0x1038,		0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(soc_gpio26_pee5,	RSVD0,		RSVD1,		RSVD2,		RSVD3,		0x1040,		0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(soc_gpio27_pee6,	RSVD0,		RSVD1,		RSVD2,		RSVD3,		0x1048,		0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(ao_retention_n_pee2,	GPIO,		LED,		RSVD2,		ISTCTRL,	0x1050,		0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(vcomp_alert_pee1,	SOC,		RSVD1,		RSVD2,		RSVD3,		0x1058,		0,	Y,	5,	7,	6,	8,	-1,	10,	12),
	PINGROUP(hdmi_cec_pgg0,		HDMI,		RSVD1,		RSVD2,		RSVD3,		0x1060,		0,	Y,	5,	7,	6,	8,	-1,	10,	12),
};

static const struct tegra_pinctrl_soc_data tegra234_pinctrl_aon = {
	.pins = tegra234_aon_pins,
	.npins = ARRAY_SIZE(tegra234_aon_pins),
	.functions = tegra234_functions,
	.nfunctions = ARRAY_SIZE(tegra234_functions),
	.groups = tegra234_aon_groups,
	.ngroups = ARRAY_SIZE(tegra234_aon_groups),
	.hsm_in_mux = false,
	.schmitt_in_mux = true,
	.drvtype_in_mux = true,
	.sfsel_in_mux = true,
};

static int tegra234_pinctrl_probe(struct platform_device *pdev)
{
	const struct tegra_pinctrl_soc_data *soc = device_get_match_data(&pdev->dev);

	return tegra_pinctrl_probe(pdev, soc);
}

static const struct of_device_id tegra234_pinctrl_of_match[] = {
	{ .compatible = "nvidia,tegra234-pinmux", .data = &tegra234_pinctrl},
	{ .compatible = "nvidia,tegra234-pinmux-aon", .data = &tegra234_pinctrl_aon },
	{ }
};
MODULE_DEVICE_TABLE(of, tegra234_pinctrl_of_match);

static struct platform_driver tegra234_pinctrl_driver = {
	.driver = {
		.name = "tegra234-pinctrl",
		.of_match_table = tegra234_pinctrl_of_match,
	},
	.probe = tegra234_pinctrl_probe,
};

static int __init tegra234_pinctrl_init(void)
{
	return platform_driver_register(&tegra234_pinctrl_driver);
}
arch_initcall(tegra234_pinctrl_init);
