/*
 * arch/arm/mach-rk29/include/mach/iomux.h
 *
 *Copyright (C) 2010 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __RK29_IOMUX_H__
#define __RK29_IOMUX_H__

#include "rk29_iomap.h"
///GPIO0L
#define GPIO0L_GPIO0B7				0
#define GPIO0L_EBC_GDOE				1
#define GPIO0L_SMC_OE_N				2
#define GPIO0L_GPIO0B6				0
#define GPIO0L_EBC_SDSHR			1
#define GPIO0L_SMC_BLS_N_1			2
#define GPIO0L_HOST_INT				3
#define GPIO0L_GPIO0B5				0
#define GPIO0L_EBC_VCOM				1
#define GPIO0L_SMC_BLS_N_0			2
#define GPIO0L_GPIO0B4				0
#define GPIO0L_EBC_BORDER1			1
#define GPIO0L_SMC_WE_N				2
#define GPIO0L_GPIO0B3				0
#define GPIO0L_EBC_BORDER0			1
#define GPIO0L_SMC_ADDR3			2
#define GPIO0L_HOST_DATA3			3
#define GPIO0L_GPIO0B2				0
#define GPIO0L_EBC_SDCE2			1
#define GPIO0L_SMC_ADDR2			2
#define GPIO0L_HOST_DATA2			3
#define GPIO0L_GPIO0B1				0
#define GPIO0L_EBC_SDCE1			1
#define GPIO0L_SMC_ADDR1			2
#define GPIO0L_HOST_DATA1			3
#define GPIO0L_GPIO0B0				0
#define GPIO0L_EBC_SDCE0			1
#define GPIO0L_SMC_ADDR0			2
#define GPIO0L_HOST_DATA0			3
#define GPIO0L_GPIO0A7				0
#define GPIO0L_MII_MDCLK			1
#define GPIO0L_GPIO0A6				0
#define GPIO0L_MII_MD				1
#define GPIO0L_GPIO0A5				0
#define GPIO0L_FLASH_DQS			1

///GPIO0H
#define GPIO0H_GPIO0D7				0
#define GPIO0H_FLASH_CSN6			1
#define GPIO0H_GPIO0D6				0
#define GPIO0H_FLASH_CSN5			1
#define GPIO0H_GPIO0D5				0
#define GPIO0H_FLASH_CSN4			1
#define GPIO0H_GPIO0D4				0
#define GPIO0H_FLASH_CSN3			1
#define GPIO0H_GPIO0D3				0
#define GPIO0H_FLASH_CSN2			1
#define GPIO0H_GPIO0D2				0
#define GPIO0H_FLASH_CSN1			1
#define GPIO0H_GPIO0D1				0
#define GPIO0H_EBC_GDCLK			1
#define GPIO0H_SMC_ADDR4			2
#define GPIO0H_HOST_DATA4			3
#define GPIO0H_GPIO0D0				0
#define GPIO0H_EBC_SDOE				1
#define GPIO0H_SMC_ADV_N			2
#define GPIO0H_GPIO0C7				0
#define GPIO0H_EBC_SDCE5			1
#define GPIO0H_SMC_DATA15			2
#define GPIO0H_GPIO0C6				0
#define GPIO0H_EBC_SDCE4			1
#define GPIO0H_SMC_DATA14			2
#define GPIO0H_GPIO0C5				0
#define GPIO0H_EBC_SDCE3			1
#define GPIO0H_SMC_DATA13			2
#define GPIO0H_GPIO0C4				0
#define GPIO0H_EBC_GDPWR2			1
#define GPIO0H_SMC_DATA12			2
#define GPIO0H_GPIO0C3				0
#define GPIO0H_EBC_GDPWR1			1
#define GPIO0H_SMC_DATA11			2
#define GPIO0H_GPIO0C2				0
#define GPIO0H_EBC_GDPWR0			1
#define GPIO0H_SMC_DATA10			2
#define GPIO0H_GPIO0C1				0
#define GPIO0H_EBC_GDRL				1
#define GPIO0H_SMC_DATA9			2
#define GPIO0H_GPIO0C0				0
#define GPIO0H_EBC_GDSP				1
#define GPIO0H_SMC_DATA8			2

///GPIO1L
#define GPIO1L_GPIO1B7				0
#define GPIO1L_UART0_SOUT			1
#define GPIO1L_GPIO1B6				0
#define GPIO1L_UART0_SIN			1
#define GPIO1L_GPIO1B5				0
#define GPIO1L_PWM0					1
#define GPIO1L_GPIO1B4				0
#define GPIO1L_VIP_CLKOUT			1
#define GPIO1L_GPIO1B3				0
#define GPIO1L_VIP_DATA3			1
#define GPIO1L_GPIO1B2				0
#define GPIO1L_VIP_DATA2			1
#define GPIO1L_GPIO1B1				0
#define GPIO1L_VIP_DATA1			1
#define GPIO1L_GPIO1B0				0
#define GPIO1L_VIP_DATA0			1
#define GPIO1L_GPIO1A7				0
#define GPIO1L_I2C1_SCL				1
#define GPIO1L_GPIO1A6				0
#define GPIO1L_I2C1_SDA				1
#define GPIO1L_GPIO1A5				0
#define GPIO1L_EMMC_PWR_EN			1
#define GPIO1L_PWM3					2
#define GPIO1L_GPIO1A4				0
#define GPIO1L_EMMC_WRITE_PRT		1
#define GPIO1L_SPI0_CSN1			2
#define GPIO1L_GPIO1A3				0
#define GPIO1L_EMMC_DETECT_N		1
#define GPIO1L_SPI1_CSN1			2
#define GPIO1L_GPIO1A2				0
#define GPIO1L_SMC_CSN1				1
#define GPIO1L_GPIO1A1				0
#define GPIO1L_SMC_CSN0				1
#define GPIO1L_GPIO1A0				0
#define GPIO1L_FLASH_CS7			1
#define GPIO1L_MDDR_TQ				2

///GPIO1H
#define GPIO1H_GPIO1D7				0
#define GPIO1H_SDMMC0_DATA5			1
#define GPIO1H_GPIO1D6				0
#define GPIO1H_SDMMC0_DATA4			1
#define GPIO1H_GPIO1D5				0
#define GPIO1H_SDMMC0_DATA3			1
#define GPIO1H_GPIO1D4				0
#define GPIO1H_SDMMC0_DATA2			1
#define GPIO1H_GPIO1D3				0
#define GPIO1H_SDMMC0_DATA1			1
#define GPIO1H_GPIO1D2				0
#define GPIO1H_SDMMC0_DATA0			1
#define GPIO1H_GPIO1_D1				0
#define GPIO1H_SDMMC0_CMD			1
#define GPIO1H_GPIO1_D0				0
#define GPIO1H_SDMMC0_CLKOUT		1
#define GPIO1H_GPIO1C7				0
#define GPIO1H_SDMMC1_CLKOUT		1
#define GPIO1H_GPIO1C6				0
#define GPIO1H_SDMMC1_DATA3			1
#define GPIO1H_GPIO1C5				0
#define GPIO1H_SDMMC1_DATA2			1
#define GPIO1H_GPIO1C4				0
#define GPIO1H_SDMMC1_DATA1			1
#define GPIO1H_GPIO1C3				0
#define GPIO1H_SDMMC1_DATA0			1
#define GPIO1H_GPIO1C2				0
#define GPIO1H_SDMMC1_CMD			1
#define GPIO1H_GPIO1C1				0
#define GPIO1H_UART0_RTS_N			1
#define GPIO1H_SDMMC1_WRITE_PRT		2
#define GPIO1H_GPIO1C0				0
#define GPIO1H_UART0_CTS_N			1
#define GPIO1H_SDMMC1_DETECT_N		2

///GPIO2L
#define GPIO2L_GPIO2B7				0
#define GPIO2L_I2C0_SCL				1
#define GPIO2L_GPIO2B6				0
#define GPIO2L_I2C0_SDA				1
#define GPIO2L_GPIO2B5				0
#define GPIO2L_UART3_RTS_N			1
#define GPIO2L_I2C3_SCL				2
#define GPIO2L_GPIO2B4				0
#define GPIO2L_UART3_CTS_N			1
#define GPIO2L_I2C3_SDA				2
#define GPIO2L_GPIO2B3				0
#define GPIO2L_UART3_SOUT			1
#define GPIO2L_GPIO2B2				0
#define GPIO2L_UART3_SIN			1
#define GPIO2L_GPIO2B1				0
#define GPIO2L_UART2_SOUT			1
#define GPIO2L_GPIO2B0				0
#define GPIO2L_UART2_SIN			1
#define GPIO2L_GPIO2A7				0
#define GPIO2L_UART2_RTS_N			1
#define GPIO2L_GPIO2A6				0
#define GPIO2L_UART2_CTS_N			1
#define GPIO2L_GPIO2A5				0
#define GPIO2L_UART1_SOUT			1
#define GPIO2L_GPIO2A4				0
#define GPIO2L_UART1_SIN			1
#define GPIO2L_GPIO2A3				0
#define GPIO2L_SDMMC0_WRITE_PRT		1
#define GPIO2L_PWM2					2
#define GPIO2L_UART1_SIR_OUT_N		3
#define GPIO2L_GPIO2A2				0
#define GPIO2L_SDMMC0_DETECT_N		1
#define GPIO2L_GPIO2A1				0
#define GPIO2L_SDMMC0_DATA7			1
#define GPIO2L_GPIO2A0				0
#define GPIO2L_SDMMC0_DATA6			1

///GPIO2H
#define GPIO2H_GPIO2D7				0
#define GPIO2H_I2S0_SDO3			1
#define GPIO2H_MII_TXD3				2
#define GPIO2H_GPIO2D6				0
#define GPIO2H_I2S0_SDO2			1
#define GPIO2H_MII_TXD2				2
#define GPIO2H_GPIO2D5				0
#define GPIO2H_I2S0_SDO1			1
#define GPIO2H_MII_RXD3				2
#define GPIO2H_GPIO2D4				0
#define GPIO2H_I2S0_SDO0			1
#define GPIO2H_MII_RXD2				2
#define GPIO2H_GPIO2D3				0
#define GPIO2H_I2S0_SDI				1
#define GPIO2H_MII_COL				2
#define GPIO2H_GPIO2D2				0
#define GPIO2H_I2S0_LRCK_RX			1
#define GPIO2H_MII_TX_ERR			2
#define GPIO2H_GPIO2D1				0
#define GPIO2H_I2S0_SCLK			1
#define GPIO2H_MII_CRS				2
#define GPIO2H_GPIO2D0				0
#define GPIO2H_I2S0_CLK				1
#define GPIO2H_MII_RX_CLKIN			2
#define GPIO2H_GPIO2C7				0
#define GPIO2H_SPI1_RXD				1
#define GPIO2H_GPIO2C6				0
#define GPIO2H_SPI1_TXD				1
#define GPIO2H_GPIO2C5				0
#define GPIO2H_SPI1_CSN0			1
#define GPIO2H_GPIO2C4				0
#define GPIO2H_SPI1_CLK				1
#define GPIO2H_GPIO2C3				0
#define GPIO2H_SPI0_RXD				1
#define GPIO2H_GPIO2C2				0
#define GPIO2H_SPI0_TXD				1
#define GPIO2H_GPIO2C1				0
#define GPIO2H_SPI0_CSN0			1
#define GPIO2H_GPIO2C0				0
#define GPIO2H_SPI0_CLK				1

///GPIO3L
#define GPIO3L_GPIO3B7				0
#define GPIO3L_EMMC_DATA5			1
#define GPIO3L_GPIO3B6				0
#define GPIO3L_EMMC_DATA4			1
#define GPIO3L_GPIO3B5				0
#define GPIO3L_EMMC_DATA3			1
#define GPIO3L_GPIO3B4				0
#define GPIO3L_EMMC_DATA2			1
#define GPIO3L_GPIO3B3				0
#define GPIO3L_EMMC_DATA1			1
#define GPIO3L_GPIO3B2				0
#define GPIO3L_EMMC_DATA0			1
#define GPIO3L_GPIO3B1				0
#define GPIO3L_EMMC_CMD				1
#define GPIO3L_GPIO3B0				0
#define GPIO3L_EMMC_CLKOUT			1
#define GPIO3L_GPIO3A7				0
#define GPIO3L_SMC_ADDR15			1
#define GPIO3L_HOST_DATA15			2
#define GPIO3L_GPIO3A6				0
#define GPIO3L_SMC_ADDR14			1
#define GPIO3L_HOST_DATA14			2
#define GPIO3L_GPIO3A5				0
#define GPIO3L_I2S1_LRCK_TX			1
#define GPIO3L_GPIO3A4				0
#define GPIO3L_I2S1_SDO				1
#define GPIO3L_GPIO3A3				0
#define GPIO3L_I2S1_SDI				1
#define GPIO3L_GPIO3A2				0
#define GPIO3L_I2S1_LRCK_RX			1
#define GPIO3L_GPIO3A1				0
#define GPIO3L_I2S1_SCLK			1
#define GPIO3L_GPIO3A0				0
#define GPIO3L_I2S1_CLK				1

///GPIO3H
#define GPIO3H_GPIO3D7				0
#define GPIO3H_SMC_ADDR9			1
#define GPIO3H_HOST_DATA9			2
#define GPIO3H_GPIO3D6				0
#define GPIO3H_SMC_ADDR8			1
#define GPIO3H_HOST_DATA8			2
#define GPIO3H_GPIO3D5				0
#define GPIO3H_SMC_ADDR7			1
#define GPIO3H_HOST_DATA7			2
#define GPIO3H_GPIO3D4				0
#define GPIO3H_HOST_WRN				1
#define GPIO3H_GPIO3D3				0
#define GPIO3H_HOST_RDN				1
#define GPIO3H_GPIO3D2				0
#define GPIO3H_HOST_CSN				1
#define GPIO3H_GPIO3D1				0
#define GPIO3H_SMC_ADDR19			1
#define GPIO3H_HOST_ADDR1			2
#define GPIO3H_GPIO3D0				0
#define GPIO3H_SMC_ADDR18			1
#define GPIO3H_HOST_ADDR0			2
#define GPIO3H_GPIO3C7				0
#define GPIO3H_SMC_ADDR17			1
#define GPIO3H_HOST_DATA17			2
#define GPIO3H_GPIO3C6				0
#define GPIO3H_SMC_ADDR16			1
#define GPIO3H_HOST_DATA16			2
#define GPIO3H_GPIO3C5				0
#define GPIO3H_SMC_ADDR12			1
#define GPIO3H_HOST_DATA12			2
#define GPIO3H_GPIO3C4				0
#define GPIO3H_SMC_ADDR11			1
#define GPIO3H_HOST_DATA11			2
#define GPIO3H_GPIO3C3				0
#define GPIO3H_SMC_ADDR10			1
#define GPIO3H_HOST_DATA10			2
#define GPIO3H_GPIO3C2				0
#define GPIO3H_SMC_ADDR13			1
#define GPIO3H_HOST_DATA13			2
#define GPIO3H_GPIO3C1				0
#define GPIO3H_EMMC_DATA7			1
#define GPIO3H_GPIO3C0				0
#define GPIO3H_EMMC_DATA6			1

///GPIO4L
#define GPIO4L_GPIO4B7				0
#define GPIO4L_FLASH_DATA15			1
#define GPIO4L_GPIO4B6				0
#define GPIO4L_FLASH_DATA14			1
#define GPIO4L_GPIO4B5				0
#define GPIO4L_FLASH_DATA13			1
#define GPIO4L_GPIO4B4				0
#define GPIO4L_FLASH_DATA12			1
#define GPIO4L_GPIO4B3				0
#define GPIO4L_FLASH_DATA11			1
#define GPIO4L_GPIO4B2				0
#define GPIO4L_FLASH_DATA10			1
#define GPIO4L_GPIO4B1				0
#define GPIO4L_FLASH_DATA9			1
#define GPIO4L_GPIO4B0				0
#define GPIO4L_FLASH_DATA8			1
#define GPIO4L_GPIO4A7				0
#define GPIO4L_SPDIF_TX				1
#define GPIO4L_GPIO4A6				0
#define GPIO4L_OTG1_DRV_VBUS		1
#define GPIO4L_GPIO4A5				0
#define GPIO4L_OTG0_DRV_VBUS		1

///GPIO4H
#define GPIO4H_GPIO4D7				0
#define GPIO4H_I2S0_LRCK_TX1		1
#define GPIO4H_GPIO4D6				0
#define GPIO4H_I2S0_LRCK_TX0		1
#define GPIO4H_GPIO4D5				0
#define GPIO4H_CPU_TRACE_CTL		1
#define GPIO4H_GPIO4D4				0
#define GPIO4H_CPU_TRACE_CLK		1
#define GPIO4H_GPIO6C76				0
#define GPIO4H_CPU_TRACE_DATA76		1
#define GPIO4H_GPIO6C54				0
#define GPIO4H_CPU_TRACE_DATA54		1
#define GPIO4H_GPIO4D32				0
#define GPIO4H_CPU_TRACE_DATA32		1
#define GPIO4H_GPIO4D10				0
#define GPIO4H_CPU_TRACE_DATA10		1
#define GPIO4H_GPIO4C7				0
#define GPIO4H_RMII_RXD0			1
#define GPIO4H_MII_RXD0				2
#define GPIO4H_GPIO4C6				0
#define GPIO4H_RMII_RXD1			1
#define GPIO4H_MII_RXD1				2
#define GPIO4H_GPIO4C5				0
#define GPIO4H_RMII_CSR_DVALID		1
#define GPIO4H_MII_RXD_VALID		2
#define GPIO4H_GPIO4C4				0
#define GPIO4H_RMII_RX_ERR			1
#define GPIO4H_MII_RX_ERR			2
#define GPIO4H_GPIO4C3				0
#define GPIO4H_RMII_TXD0			1
#define GPIO4H_MII_TXD0				2
#define GPIO4H_GPIO4C2				0
#define GPIO4H_RMII_TXD1			1
#define GPIO4H_MII_TXD1				2
#define GPIO4H_GPIO4C1				0
#define GPIO4H_RMII_TX_EN			1
#define GPIO4H_MII_TX_EN			2
#define GPIO4H_GPIO4C0				0
#define GPIO4H_RMII_CLKOUT			1
#define GPIO4H_RMII_CLKIN			2

///GPIO5L
#define GPIO5L_GPIO5B7				0
#define GPIO5L_HSADC_CLKOUT			1
#define GPIO5L_GPS_CLK				2
#define GPIO5L_GPIO5B6				0
#define GPIO5L_HSADC_DATA9			1
#define GPIO5L_GPIO5B5				0
#define GPIO5L_HSADC_DATA8			1
#define GPIO5L_GPIO5B4				0
#define GPIO5L_HSADC_DATA7			1
#define GPIO5L_GPIO5B3				0
#define GPIO5L_HSADC_DATA6			1
#define GPIO5L_GPIO5B2				0
#define GPIO5L_HSADC_DATA5			1
#define GPIO5L_GPIO5B1				0
#define GPIO5L_HSADC_DATA4			1
#define GPIO5L_GPIO5B0				0
#define GPIO5L_HSADC_DATA3			1
#define GPIO5L_GPIO5A7				0
#define GPIO5L_HSADC_DATA2			1
#define GPIO5L_GPIO5A6				0
#define GPIO5L_HSADC_DATA1			1
#define GPIO5L_GPIO5A5				0
#define GPIO5L_HSADC_DATA0			1
#define GPIO5L_GPIO5A4				0
#define GPIO5L_TS_SYNC				1
#define GPIO5L_GPIO5A3				0
#define GPIO5L_MII_TX_CLKIN			1

///GPIO5H
#define GPIO5H_GPIO5D6				0
#define GPIO5H_SDMMC1_PWR_EN		1
#define GPIO5H_GPIO5D5				0
#define GPIO5H_SDMMC0_PWR_EN		1
#define GPIO5H_GPIO5D4				0
#define GPIO5H_I2C2_SCL				1
#define GPIO5H_GPIO5D3				0
#define GPIO5H_I2C2_SDA				1
#define GPIO5H_GPIO5D2				0
#define GPIO5H_PWM1					1
#define GPIO5H_UART1_SIR_IN			2
#define GPIO5H_GPIO5D1				0
#define GPIO5H_EBC_SDCLK			1
#define GPIO5H_SMC_ADDR6			2
#define GPIO5H_HOST_DATA6			3
#define GPIO5H_GPIO5D0				0
#define GPIO5H_EBC_SDLE				1
#define GPIO5H_SMC_ADDR5			2
#define GPIO5H_HOST_DATA5			3
#define GPIO5H_GPIO5C7				0
#define GPIO5H_EBC_SDDO7			1
#define GPIO5H_SMC_DATA7			2
#define GPIO5H_GPIO5C6				0
#define GPIO5H_EBC_SDDO6			1
#define GPIO5H_SMC_DATA6			2
#define GPIO5H_GPIO5C5				0
#define GPIO5H_EBC_SDDO5			1
#define GPIO5H_SMC_DATA5			2
#define GPIO5H_GPIO5C4				0
#define GPIO5H_EBC_SDDO4			1
#define GPIO5H_SMC_DATA4			2
#define GPIO5H_GPIO5C3				0
#define GPIO5H_EBC_SDDO3			1
#define GPIO5H_SMC_DATA3			2
#define GPIO5H_GPIO5C2				0
#define GPIO5H_EBC_SDDO2			1
#define GPIO5H_SMC_DATA2			2
#define GPIO5H_GPIO5C1				0
#define GPIO5H_EBC_SDDO1			1
#define GPIO5H_SMC_DATA1			2
#define GPIO5H_GPIO5C0				0
#define GPIO5H_EBC_SDDO0			1
#define GPIO5H_SMC_DATA0			2

#define DEFAULT						0
#define INITIAL						1

#define RK29_IOMUX_GPIO0L_CON			RK29_GRF_BASE+0x48
#define RK29_IOMUX_GPIO0H_CON			RK29_GRF_BASE+0x4c
#define RK29_IOMUX_GPIO1L_CON			RK29_GRF_BASE+0x50
#define RK29_IOMUX_GPIO1H_CON			RK29_GRF_BASE+0x54
#define RK29_IOMUX_GPIO2L_CON			RK29_GRF_BASE+0x58
#define RK29_IOMUX_GPIO2H_CON			RK29_GRF_BASE+0x5c
#define RK29_IOMUX_GPIO3L_CON			RK29_GRF_BASE+0x60
#define RK29_IOMUX_GPIO3H_CON			RK29_GRF_BASE+0x64
#define RK29_IOMUX_GPIO4L_CON			RK29_GRF_BASE+0x68
#define RK29_IOMUX_GPIO4H_CON			RK29_GRF_BASE+0x6c
#define RK29_IOMUX_GPIO5L_CON			RK29_GRF_BASE+0x70
#define RK29_IOMUX_GPIO5H_CON			RK29_GRF_BASE+0x74

///GPIO0L
#define GPIO0B7_EBCGDOE_SMCOEN_NAME					"gpio0b7_ebcgdoe_smcoen_name"
#define GPIO0B6_EBCSDSHR_SMCBLSN1_HOSTINT_NAME		"gpio0b6_ebcsdshr_smcblsn1_hostint_name"	
#define GPIO0B5_EBCVCOM_SMCBLSN0_NAME				"gpio0b5_ebcvcom_smcblsn0_name"
#define GPIO0B4_EBCBORDER1_SMCWEN_NAME				"gpio0b4_ebcborder1_smcwen_name"
#define GPIO0B3_EBCBORDER0_SMCADDR3_HOSTDATA3_NAME	"gpio0b3_ebcborder0_smcaddr3_hostdata3_name"
#define GPIO0B2_EBCSDCE2_SMCADDR2_HOSTDATA2_NAME	"gpio0b2_ebcsdce2_smcaddr2_hostdata2_name"
#define GPIO0B1_EBCSDCE1_SMCADDR1_HOSTDATA1_NAME	"gpio0b1_ebcsdce1_smcaddr1_hostdata1_name"
#define GPIO0B0_EBCSDCE0_SMCADDR0_HOSTDATA0_NAME	"gpio0b0_ebcsdce0_smcaddr0_hostdata0_name"
#define GPIO0A7_MIIMDCLK_NAME						"gpio0a7_miimdclk_name"
#define GPIO0A6_MIIMD_NAME							"gpio0a6_miimd_name"
#define GPIO0A5_FLASHDQS_NAME						"gpio0a5_flashdqs_name"

///GPIO0H
#define GPIO0D7_FLASHCSN6_NAME						"gpio0d7_flashcsn6_name"						
#define GPIO0D6_FLASHCSN5_NAME						"gpio0d6_flashcsn5_name"
#define GPIO0D5_FLASHCSN4_NAME						"gpio0d5_flashcsn4_name"
#define GPIO0D4_FLASHCSN3_NAME						"gpio0d4_flashcsn3_name"
#define GPIO0D3_FLASHCSN2_NAME						"gpio0d3_flashcsn2_name"
#define GPIO0D2_FLASHCSN1_NAME						"gpio0d2_flashcsn1_name"
#define GPIO0D1_EBCGDCLK_SMCADDR4_HOSTDATA4_NAME	"gpio0d1_ebcgdclk_smcaddr4_hostdata4_name"
#define GPIO0D0_EBCSDOE_SMCADVN_NAME				"gpio0d0_ebcsdoe_smcadvn_name"
#define GPIO0C7_EBCSDCE5_SMCDATA15_NAME				"gpio0c7_ebcsdce5_smcdata15_name"
#define GPIO0C6_EBCSDCE4_SMCDATA14_NAME				"gpio0c6_ebcsdce4_smcdata14_name"
#define GPIO0C5_EBCSDCE3_SMCDATA13_NAME				"gpio0c5_ebcsdce3_smcdata13_name"
#define GPIO0C4_EBCSDCE2_SMCDATA12_NAME				"gpio0c4_ebcsdce2_smcdata12_name"
#define GPIO0C3_EBCSDCE1_SMCDATA11_NAME				"gpio0c3_ebcsdce1_smcdata11_name"
#define GPIO0C2_EBCSDCE0_SMCDATA10_NAME				"gpio0c2_ebcsdce0_smcdata10_name"
#define GPIO0C1_EBCGDR1_SMCDATA9_NAME				"gpio0c1_ebcgdr1_smcdata9_name"
#define GPIO0C0_EBCGDSP_SMCDATA8_NAME				"gpio0c0_ebcgdsp_smcdata8_name"

///GPIO1L
#define GPIO1B7_UART0SOUT_NAME						"gpio1b7_uart0sout_name"
#define GPIO1B6_UART0SIN_NAME						"gpio1b6_uart0sin_name"
#define GPIO1B5_PWM0_NAME							"gpio1b5_pwm0_name"
#define GPIO1B4_VIPCLKOUT_NAME						"gpio1b4_vipclkout_name"
#define GPIO1B3_VIPDATA3_NAME						"gpio1b3_vipdata3_name"
#define GPIO1B2_VIPDATA2_NAME						"gpio1b2_vipdata2_name"
#define GPIO1B1_VIPDATA1_NAME						"gpio1b1_vipdata1_name"
#define GPIO1B0_VIPDATA0_NAME						"gpio1b0_vipdata0_name"
#define GPIO1A7_I2C1SCL_NAME						"gpio1a7_i2c1scl_name"
#define GPIO1A6_I2C1SDA_NAME						"gpio1a6_i2c1sda_name"
#define GPIO1A5_EMMCPWREN_PWM3_NAME					"gpio1a5_emmcpwren_pwm3_name"
#define GPIO1A4_EMMCWRITEPRT_SPI0CS1_NAME			"gpio1a4_emmcwriteprt_spi0cs1_name"
#define GPIO1A3_EMMCDETECTN_SPI1CS1_NAME			"gpio1a3_emmcdetectn_spi1cs1_name"
#define GPIO1A2_SMCCSN1_NAME						"gpio1a2_smccsn1_name"
#define GPIO1A1_SMCCSN0_NAME						"gpio1a1_smccsn0_name"
#define GPIO1A0_FLASHCS7_MDDRTQ_NAME				"gpio1a0_flashcs7_mddrtq_name"

///GPIO1H
#define GPIO1D7_SDMMC0DATA5_NAME					"gpio1d7_sdmmc0data5_name"
#define GPIO1D6_SDMMC0DATA4_NAME					"gpio1d6_sdmmc0data4_name"
#define GPIO1D5_SDMMC0DATA3_NAME					"gpio1d5_sdmmc0data3_name"
#define GPIO1D4_SDMMC0DATA2_NAME					"gpio1d4_sdmmc0data2_name"
#define GPIO1D3_SDMMC0DATA1_NAME					"gpio1d3_sdmmc0data1_name"
#define GPIO1D2_SDMMC0DATA0_NAME					"gpio1d2_sdmmc0data0_name"
#define GPIO1D1_SDMMC0CMD_NAME						"gpio1d1_sdmmc0cmd_name"
#define GPIO1D0_SDMMC0CLKOUT_NAME					"gpio1d0_sdmmc0clkout_name"
#define GPIO1C7_SDMMC1CLKOUT_NAME					"gpio1c7_sdmmc1clkout_name"
#define GPIO1C6_SDMMC1DATA3_NAME					"gpio1c6_sdmmc1data3_name"
#define GPIO1C5_SDMMC1DATA2_NAME					"gpio1c5_sdmmc1data2_name"
#define GPIO1C4_SDMMC1DATA1_NAME					"gpio1c4_sdmmc1data1_name"
#define GPIO1C3_SDMMC1DATA0_NAME					"gpio1c3_sdmmc1data0_name"
#define GPIO1C2_SDMMC1CMD_NAME						"gpio1c2_sdmmc1cmd_name"
#define GPIO1C1_UART0RTSN_SDMMC1WRITEPRT_NAME		"gpio1c1_uart0rtsn_sdmmc1writeprt_name"
#define GPIO1C0_UART0CTSN_SDMMC1DETECTN_NAME		"gpio1c1_uart0ctsn_sdmmc1detectn_name"

///GPIO2L
#define GPIO2B7_I2C0SCL_NAME						"gpio2b7_i2c0scl_name"
#define GPIO2B6_I2C0SDA_NAME						"gpio2b6_i2c0sda_name"
#define GPIO2B5_UART3RTSN_I2C3SCL_NAME				"gpio2b5_uart3rtsn_i2c3scl_name"
#define GPIO2B4_UART3CTSN_I2C3SDA_NAME				"gpio2b4_uart3ctsn_i2c3sda_name"
#define GPIO2B3_UART3SOUT_NAME						"gpio2b3_uart3sout_name"
#define GPIO2B2_UART3SIN_NAME						"gpio2b2_uart3sin_name"
#define GPIO2B1_UART2SOUT_NAME						"gpio2b1_uart2sout_name"
#define GPIO2B0_UART2SIN_NAME						"gpio2b0_uart2sin_name"
#define GPIO2A7_UART2RTSN_NAME						"gpio2a7_uart2rtsn_name"
#define GPIO2A6_UART2CTSN_NAME						"gpio2a6_uart2ctsn_name"
#define GPIO2A5_UART1SOUT_NAME						"gpio2a5_uart1sout_name"
#define GPIO2A4_UART1SIN_NAME						"gpio2a4_uart1sin_name"
#define GPIO2A3_SDMMC0WRITEPRT_PWM2_NAME			"gpio2a3_sdmmc0writeprt_pwm2_name"
#define GPIO2A2_SDMMC0DETECTN_NAME					"gpio2a2_sdmmc0detectn_name"
#define GPIO2A1_SDMMC0DATA7_NAME					"gpio2a1_sdmmc0data7_name"
#define GPIO2A0_SDMMC0DATA6_NAME					"gpio2a0_sdmmc0data6_name"

///GPIO2H
#define GPIO2D7_I2S0SDO3_MIITXD3_NAME				"gpio2d7_i2s0sdo3_miitxd3_name"
#define GPIO2D6_I2S0SDO2_MIITXD2_NAME				"gpio2d6_i2s0sdo2_miitxd2_name"
#define GPIO2D5_I2S0SDO1_MIIRXD3_NAME				"gpio2d5_i2s0sdo1_miirxd3_name"
#define GPIO2D4_I2S0SDO0_MIIRXD2_NAME				"gpio2d4_i2s0sdo0_miirxd2_name"
#define GPIO2D3_I2S0SDI_MIICOL_NAME					"gpio2d3_i2s0sdi_miicol_name"
#define GPIO2D2_I2S0LRCKRX_MIITXERR_NAME			"gpio2d2_i2s0lrckrx_miitxerr_name"
#define GPIO2D1_I2S0SCLK_MIICRS_NAME				"gpio2d1_i2s0sclk_miicrs_name"
#define GPIO2D0_I2S0CLK_MIIRXCLKIN_NAME				"gpio2d0_i2s0clk_miirxclkin_name"
#define GPIO2C7_SPI1RXD_NAME						"gpio2c7_spi1rxd_name"
#define GPIO2C6_SPI1TXD_NAME						"gpio2c6_spi1txd_name"
#define GPIO2C5_SPI1CSN0_NAME						"gpio2c5_spi1csn0_name"
#define GPIO2C4_SPI1CLK_NAME						"gpio2c4_spi1clk_name"
#define GPIO2C3_SPI0RXD_NAME						"gpio2c3_spi0rxd_name"
#define GPIO2C2_SPI0TXD_NAME						"gpio2c2_spi0txd_name"
#define GPIO2C1_SPI0CSN0_NAME						"gpio2c1_spi0csn0_name"
#define GPIO2C0_SPI0CLK_NAME						"gpio2c0_spi0clk_name"

///GPIO3L
#define GPIO3B7_EMMCDATA5_NAME						"gpio3b7_emmcdata5_name"
#define GPIO3B6_EMMCDATA4_NAME						"gpio3b6_emmcdata4_name"
#define GPIO3B5_EMMCDATA3_NAME						"gpio3b5_emmcdata3_name"
#define GPIO3B4_EMMCDATA2_NAME						"gpio3b4_emmcdata2_name"
#define GPIO3B3_EMMCDATA1_NAME						"gpio3b3_emmcdata1_name"
#define GPIO3B2_EMMCDATA0_NAME						"gpio3b2_emmcdata0_name"
#define GPIO3B1_EMMCMD_NAME							"gpio3b1_emmcmd_name"
#define GPIO3B0_EMMCLKOUT_NAME						"gpio3b0_emmclkout_name"
#define GPIO3A7_SMCADDR15_HOSTDATA15_NAME			"gpio3a7_smcaddr15_hostdata15_name"
#define GPIO3A6_SMCADDR14_HOSTDATA14_NAME			"gpio3a6_smcaddr14_hostdata14_name"
#define GPIO3A5_I2S1LRCKTX_NAME						"gpio3a5_i2s1lrcktx_name"
#define GPIO3A4_I2S1SDO_NAME						"gpio3a4_i2s1sdo_name"
#define GPIO3A3_I2S1SDI_NAME						"gpio3a3_i2s1sdi_name"
#define GPIO3A2_I2S1LRCKRX_NAME						"gpio3a2_i2s1lrckrx_name"
#define GPIO3A1_I2S1SCLK_NAME						"gpio3a1_i2s1sclk_name"
#define GPIO3A0_I2S1CLK_NAME						"gpio3a0_i2s1clk_name"

///GPIO3H
#define GPIO3D7_SMCADDR9_HOSTDATA9_NAME				"gpio3d7_smcaddr9_hostdata9_name"
#define GPIO3D6_SMCADDR8_HOSTDATA8_NAME				"gpio3d6_smcaddr8_hostdata8_name"
#define GPIO3D5_SMCADDR7_HOSTDATA7_NAME				"gpio3d5_smcaddr7_hostdata7_name"
#define GPIO3D4_HOSTWRN_NAME						"gpio3d4_hostwrn_name"
#define GPIO3D3_HOSTRDN_NAME						"gpio3d4_hostwrn_name"
#define GPIO3D2_HOSTCSN_NAME						"gpio3d2_hostcsn_name"
#define GPIO3D1_SMCADDR19_HOSTADDR1_NAME			"gpio3d1_smcaddr19_hostaddr1_name"
#define GPIO3D0_SMCADDR18_HOSTADDR0_NAME			"gpio3d0_smcaddr18_hostaddr0_name"
#define GPIO3C7_SMCADDR17_HOSTDATA17_NAME			"gpio3c7_smcaddr17_hostdata17_name"
#define GPIO3C6_SMCADDR16_HOSTDATA16_NAME			"gpio3c6_smcaddr16_hostdata16_name"
#define GPIO3C5_SMCADDR12_HOSTDATA12_NAME			"gpio3c5_smcaddr12_hostdata12_name"
#define GPIO3C4_SMCADDR11_HOSTDATA11_NAME			"gpio3c4_smcaddr11_hostdata11_name"
#define GPIO3C3_SMCADDR10_HOSTDATA10_NAME			"gpio3c3_smcaddr10_hostdata10_name"
#define GPIO3C2_SMCADDR13_HOSTDATA13_NAME			"gpio3c2_smcaddr13_hostdata13_name"
#define GPIO3C1_EMMCDATA7_NAME						"gpio3c1_emmcdata7_name"
#define GPIO3C0_EMMCDATA6_NAME						"gpio3c0_emmcdata6_name"

///GPIO4L
#define GPIO4B7_FLASHDATA15_NAME					"gpio4b7_flashdata15_name"
#define GPIO4B6_FLASHDATA14_NAME					"gpio4b6_flashdata14_name"
#define GPIO4B5_FLASHDATA13_NAME					"gpio4b5_flashdata13_name"
#define GPIO4B4_FLASHDATA12_NAME					"gpio4b4_flashdata12_name"
#define GPIO4B3_FLASHDATA11_NAME					"gpio4b3_flashdata11_name"
#define GPIO4B2_FLASHDATA10_NAME					"gpio4b2_flashdata10_name"
#define GPIO4B1_FLASHDATA9_NAME						"gpio4b1_flashdata9_name"	
#define GPIO4B0_FLASHDATA8_NAME						"gpio4b0_flashdata8_name"
#define GPIO4A7_SPDIFTX_NAME						"gpio4a7_spdiftx_name"
#define GPIO4A6_OTG1DRVVBUS_NAME					"gpio4a6_otg1drvvbus_name"
#define GPIO4A5_OTG0DRVVBUS_NAME					"gpio4a5_otg0drvvbus_name"

///GPIO4H
#define GPIO4D7_I2S0LRCKTX1_NAME					"gpio4d7_i2s0lrcktx1_name"
#define GPIO4D6_I2S0LRCKTX0_NAME					"gpio4d6_i2s0lrcktx0_name"
#define GPIO4D5_CPUTRACECTL_NAME					"gpio4d5_cputracectl_name"
#define GPIO4D4_CPUTRACECLK_NAME					"gpio4d4_cputraceclk_name"
#define GPIO6C76_CPUTRACEDATA76_NAME				"gpio6c76_cputracedata76_name"
#define GPIO6C54_CPUTRACEDATA54_NAME				"gpio6c54_cputracedata54_name"
#define GPIO4D32_CPUTRACEDATA32_NAME				"gpio4d32_cputracedata32_name"
#define GPIO4D10_CPUTRACEDATA10_NAME				"gpio4d10_cputracedata10_name"
#define GPIO4C7_RMIIRXD0_MIIRXD0_NAME				"gpio4c7_rmiirxd0_miirxd0_name"
#define GPIO4C6_RMIIRXD1_MIIRXD1_NAME				"gpio4c6_rmiirxd1_miirxd1_name"
#define GPIO4C5_RMIICSRDVALID_MIIRXDVALID_NAME		"gpio4c5_rmiicsrdvalid_miirxdvalid_name"
#define GPIO4C4_RMIIRXERR_MIIRXERR_NAME				"gpio4c4_rmiirxerr_miirxerr_name"
#define GPIO4C3_RMIITXD0_MIITXD0_NAME				"gpio4c3_rmiitxd0_miitxd0_name"
#define GPIO4C2_RMIITXD1_MIITXD1_NAME				"gpio4c2_rmiitxd1_miitxd1_name"
#define GPIO4C1_RMIITXEN_MIITXEN_NAME				"gpio4c1_rmiitxen_miitxen_name"
#define GPIO4C0_RMIICLKOUT_RMIICLKIN_NAME			"gpio4c0_rmiiclkout_rmiiclkin_name"

///GPIO5L
#define GPIO5B7_HSADCCLKOUTGPSCLK_NAME				"gpio5b7_hsadcclkoutgpsclk_name"
#define GPIO5B6_HSADCDATA9_NAME						"gpio5b6_hsadcdata9_name"
#define GPIO5B5_HSADCDATA8_NAME						"gpio5b5_hsadcdata8_name"
#define GPIO5B4_HSADCDATA7_NAME						"gpio5b4_hsadcdata7_name"
#define GPIO5B3_HSADCDATA6_NAME						"gpio5b3_hsadcdata6_name"
#define GPIO5B2_HSADCDATA5_NAME						"gpio5b2_hsadcdata5_name"
#define GPIO5B1_HSADCDATA4_NAME						"gpio5b1_hsadcdata4_name"
#define GPIO5B0_HSADCDATA3_NAME						"gpio5b0_hsadcdata3_name"
#define GPIO5A7_HSADCDATA2_NAME						"gpio5a7_hsadcdata2_name"
#define GPIO5A6_HSADCDATA1_NAME						"gpio5a6_hsadcdata1_name"
#define GPIO5A5_HSADCDATA0_NAME						"gpio5a5_hsadcdata0_name"
#define GPIO5A4_TSSYNC_NAME							"gpio5a4_tssync_name"
#define GPIO5A3_MIITXCLKIN_NAME						"gpio5a3_miitxclkin_name"

///GPIO5H
#define GPIO5D6_SDMMC1PWREN_NAME					"gpio5d6_sdmmc1pwren_name"
#define GPIO5D5_SDMMC0PWREN_NAME					"gpio5d5_sdmmc0pwren_name"
#define GPIO5D4_I2C2SCL_NAME						"gpio5d4_i2c2scl_name"
#define GPIO5D3_I2C2SDA_NAME						"gpio5d3_i2c2sda_name"
#define GPIO5D2_PWM1_UART1SIRIN_NAME				"gpio5d2_pwm1_uart1sirin_name"
#define GPIO5D1_EBCSDCLK_SMCADDR6_HOSTDATA6_NAME	"gpio5d1_ebcsdclk_smcaddr6_hostdata6_name"
#define GPIO5D0_EBCSDLE_SMCADDR5_HOSTDATA5_NAME		"gpio5d0_ebcsdle_smcaddr5_hostdata5_name"
#define GPIO5C7_EBCSDDO7_SMCDATA7_NAME				"gpio5c7_ebcsddo7_smcdata7_name"
#define GPIO5C6_EBCSDDO6_SMCDATA6_NAME				"gpio5c6_ebcsddo6_smcdata6_name"
#define GPIO5C5_EBCSDDO5_SMCDATA5_NAME				"gpio5c5_ebcsddo5_smcdata5_name"
#define GPIO5C4_EBCSDDO4_SMCDATA4_NAME				"gpio5c4_ebcsddo4_smcdata4_name"
#define GPIO5C3_EBCSDDO3_SMCDATA3_NAME				"gpio5c3_ebcsddo3_smcdata3_name"
#define GPIO5C2_EBCSDDO2_SMCDATA2_NAME				"gpio5c2_ebcsddo2_smcdata2_name"
#define GPIO5C1_EBCSDDO1_SMCDATA1_NAME				"gpio5c1_ebcsddo1_smcdata1_name"
#define GPIO5C0_EBCSDDO0_SMCDATA0_NAME				"gpio5c0_ebcsddo0_smcdata0_name"

#define GRF_GPIO0_PULL								0x0078
#define GRF_GPIO1_PULL								0x007C
#define GRF_GPIO2_PULL								0x0080
#define GRF_GPIO3_PULL								0x0084
#define GRF_GPIO4_PULL								0x0088
#define GRF_GPIO5_PULL								0x008C
#define GRF_GPIO6_PULL								0x0090

#define MUX_CFG(desc,reg,off,interl,mux_mode,bflags)	\
{						  	\
        .name = desc,                                   \
        .offset = off,                               	\
        .interleave = interl,                       	\
        .mux_reg = RK29_IOMUX_##reg##_CON,          \
        .mode = mux_mode,                               \
        .premode = mux_mode,                            \
        .flags = bflags,				\
},

struct mux_config {
	char *name;
	const unsigned int offset;
	unsigned int mode;
	unsigned int premode;
	const unsigned int mux_reg;
	const unsigned int interleave;
	unsigned int flags;
};

extern int rk29_iomux_init(void);
extern void rk29_mux_api_set(char *name, unsigned int mode);

#endif