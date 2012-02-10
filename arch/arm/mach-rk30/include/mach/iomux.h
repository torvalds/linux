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

#include "io.h"

//GPIO0A
#define GPIO0A_GPIO0A7				0 
#define GPIO0A_I2S_8CH_SDI			1 
#define GPIO0A_GPIO0A6				0 
#define GPIO0A_HOST_DRV_VBUS			1 
#define GPIO0A_GPIO0A5				0 
#define GPIO0A_OTG_DRV_VBUS			1 
#define GPIO0A_GPIO0A4				0 
#define GPIO0A_PWM1				1 
#define GPIO0A_GPIO0A3				0 
#define GPIO0A_PWM0				1 
#define GPIO0A_GPIO0A2				0 
#define GPIO0A_HDMI_I2C_SDA			1 
#define GPIO0A_GPIO0A1				0 
#define GPIO0A_HDMI_I2C_SCL			1 
#define GPIO0A_GPIO0A0				0 
#define GPIO0A_HDMI_HOT_PLUG_IN			1 


//GPIO0B
#define GPIO0B_GPIO0B7				0 
#define GPIO0B_I2S_8CH_SDO3			1 
#define GPIO0B_GPIO0B6				0 
#define GPIO0B_I2S_8CH_SDO2			1 
#define GPIO0B_GPIO0B5				0 
#define GPIO0B_I2S_8CH_SDO1			1 
#define GPIO0B_GPIO0B4				0 
#define GPIO0B_I2S_8CH_SDO0			1 
#define GPIO0B_GPIO0B3				0 
#define GPIO0B_I2S_8CH_LRCK_TX			1 
#define GPIO0B_GPIO0B2				0 
#define GPIO0B_I2S_8CH_LRCK_RX			1 
#define GPIO0B_GPIO0B1				0 
#define GPIO0B_I2S_8CH_SCLK			1 
#define GPIO0B_GPIO0B0				0 
#define GPIO0B_I2S_8CH_CLK			1 


//GPIO0C
#define GPIO0C_GPIO0C7				0 
#define GPIO0C_TRACE_CTL			1 
#define GPIO0C_SMC_ADDR3			2 
#define GPIO0C_GPIO0C6				0 
#define GPIO0C_TRACE_CLK			1 
#define GPIO0C_SMC_ADDR2			2 
#define GPIO0C_GPIO0C5				0 
#define GPIO0C_I2S1_2CH_SDO			1 
#define GPIO0C_GPIO0C4				0 
#define GPIO0C_I2S1_2CH_SDI			1 
#define GPIO0C_GPIO0C3				0 
#define GPIO0C_I2S1_2CH_LRCK_TX			1 
#define GPIO0C_GPIO0C2				0 
#define GPIO0C_I2S_8CH_LRCK_RX			1 
#define GPIO0C_GPIO0C1				0 
#define GPIO0C_I2S1_2CH_SCLK			1 
#define GPIO0C_GPIO0C0				0 
#define GPIO0C_I2S1_2CH_CLK			1 



//GPIO0D
#define GPIO0D_GPIO0D7				0 
#define GPIO0D_PWM3				1 
#define GPIO0D_GPIO0D6				0 
#define GPIO0D_PWM2				1 
#define GPIO0D_GPIO0D5				0 
#define GPIO0D_I2S2_2CH_SDO			1 
#define GPIO0D_SMC_ADDR1			2 
#define GPIO0D_GPIO0D4				0 
#define GPIO0D_I2S1_2CH_SDI			1 
#define GPIO0D_SMC_ADDR0			2 
#define GPIO0D_GPIO0D3				0 
#define GPIO0D_I2S1_2CH_LRCK_TX			1 
#define GPIO0D_SMC_ADV_N			2 
#define GPIO0D_GPIO0D2				0 
#define GPIO0D_I2S1_2CH_LRCK_RX			1 
#define GPIO0D_SMC_OE_N				2 
#define GPIO0D_GPIO0D1				0 
#define GPIO0D_I2S2_2CH_SCLK			1 
#define GPIO0D_SMC_WE_N				2 
#define GPIO0D_GPIO0D0				0 
#define GPIO0D_I2S2_2CH_CLK			1 
#define GPIO0D_SMC_CSN0				2 


//GPIO1A
#define GPIO1A_GPIO1A7				0 
#define GPIO1A_UART1_RTS_N			1 
#define GPIO1A_SPI0_TXD				2 
#define GPIO1A_GPIO1A6				0 
#define GPIO1A_UART1_CTS_N			1 
#define GPIO1A_SPI0_RXD				2 
#define GPIO1A_GPIO1A5				0 
#define GPIO1A_UART1_SOUT			1 
#define GPIO1A_SPI0_CLK				2 
#define GPIO1A_GPIO1A4				0 
#define GPIO1A_UART1_SIN			1 
#define GPIO1A_SPI0_CSN0			2 
#define GPIO1A_GPIO1A3				0 
#define GPIO1A_UART0_RTS_N			1 
#define GPIO1A_GPIO1A2				0 
#define GPIO1A_UART0_CTS_N			1 
#define GPIO1A_GPIO1A1				0 
#define GPIO1A_UART0_SOUT			1 
#define GPIO1A_GPIO1A0				0 
#define GPIO1A_UART0_SIN			1 


//GPIO1B
#define GPIO1B_GPIO1B7				0 
#define GPIO1B_CIF_DATA11			1 
#define GPIO1B_GPIO1B6				0 
#define GPIO1B_CIF_DATA10			1 
#define GPIO1B_GPIO1B5				0 
#define GPIO1B_CIF0_DATA1			1 
#define GPIO1B_GPIO1B4				0 
#define GPIO1B_CIF0_DATA0			1 
#define GPIO1B_GPIO1B3				0 
#define GPIO1B_CIF0_CLKOUT			1 
#define GPIO1B_GPIO1B2				0 
#define GPIO1B_SPDIF_TX				1 
#define GPIO1B_GPIO1B1				0 
#define GPIO1B_UART2_SOUT			1 
#define GPIO1B_GPIO1B0				0 
#define GPIO1B_UART2_SIN			1 


//GPIO1C
#define GPIO1C_GPIO1C7				0 
#define GPIO1C_CIF_DATA9			1 
#define GPIO1C_RMII_RXD0			2 
#define GPIO1C_GPIO1C6				0 
#define GPIO1C_CIF_DATA8			1 
#define GPIO1C_RMII_RXD1			2 
#define GPIO1C_GPIO1C5				0 
#define GPIO1C_CIF_DATA7			1 
#define GPIO1C_RMII_CRS_DVALID			2 
#define GPIO1C_GPIO1C4				0 
#define GPIO1C_CIF_DATA6			1 
#define GPIO1C_RMII_RX_ERR			2 
#define GPIO1C_GPIO1C3				0 
#define GPIO1C_CIF_DATA5			1 
#define GPIO1C_RMII_TXD0			2 
#define GPIO1C_GPIO1C2				0 
#define GPIO1C_CIF1_DATA4			1 
#define GPIO1C_RMII_TXD1			2 
#define GPIO1C_GPIO1C1				0 
#define GPIO1C_CIF_DATA3			1 
#define GPIO1C_RMII_TX_EN			2 
#define GPIO1C_GPIO1C0				0 
#define GPIO1C_CIF1_DATA2			1 
#define GPIO1C_RMII_CLKOUT			2 
#define GPIO1C_RMII_CLKIN			3 


//GPIO1D
#define GPIO1D_GPIO1D7				0 
#define GPIO1D_CIF1_CLKOUT			1 
#define GPIO1D_GPIO1D6				0 
#define GPIO1D_CIF1_DATA11			1 
#define GPIO1D_GPIO1D5				0 
#define GPIO1D_CIF1_DATA10			1 
#define GPIO1D_GPIO1D4				0 
#define GPIO1D_CIF1_DATA1			1 
#define GPIO1D_GPIO1D3				0 
#define GPIO1D_CIF1_DATA0			1 
#define GPIO1D_GPIO1D2				0 
#define GPIO1D_CIF1_CLKIN			1 
#define GPIO1D_GPIO1D1				0 
#define GPIO1D_CIF1_HREF			1 
#define GPIO1D_MII_MDCLK			2 
#define GPIO1D_GPIO1D0				0 
#define GPIO1D_CIF1_VSYNC			1 
#define GPIO1D_MII_MD				2 


//GPIO2A
#define GPIO2A_GPIO2A7				0 
#define GPIO2A_LCDC1_DATA7			1 
#define GPIO2A_SMC_ADDR11			2 
#define GPIO2A_GPIO2A6				0 
#define GPIO2A_LCDC1_DATA6			1 
#define GPIO2A_SMC_ADDR10			2 
#define GPIO2A_GPIO2A5				0 
#define GPIO2A_LCDC1_DATA5			1 
#define GPIO2A_SMC_ADDR9			2 
#define GPIO2A_GPIO2A4				0 
#define GPIO2A_LCDC1_DATA4			1 
#define GPIO2A_SMC_ADDR8			2 
#define GPIO2A_GPIO2A3				0 
#define GPIO2A_LCDC_DATA3			1 
#define GPIO2A_SMC_ADDR7			2 
#define GPIO2A_GPIO2A2				0 
#define GPIO2A_LCDC_DATA2			1 
#define GPIO2A_SMC_ADDR6			2 
#define GPIO2A_GPIO2A1				0 
#define GPIO2A_LCDC1_DATA1			1 
#define GPIO2A_SMC_ADDR5			2 
#define GPIO2A_GPIO2A0				0 
#define GPIO2A_LCDC1_DATA0			1 
#define GPIO2A_SMC_ADDR4			2 


//GPIO2B
#define GPIO2B_GPIO2B7				0 
#define GPIO2B_LCDC1_DATA15			1 
#define GPIO2B_SMC_ADDR19			2 
#define GPIO2B_HSADC_DATA7			3 
#define GPIO2B_GPIO2B6				0 
#define GPIO2B_LCDC1_DATA14			1 
#define GPIO2B_SMC_ADDR18			2 
#define GPIO2B_TS_SYNC				3 
#define GPIO2B_GPIO2B5				0 
#define GPIO2B_LCDC1_DATA13			1 
#define GPIO2B_SMC_ADDR17			2 
#define GPIO2B_HSADC_DATA8			3 
#define GPIO2B_GPIO2B4				0 
#define GPIO2B_LCDC1_DATA12			1 
#define GPIO2B_SMC_ADDR16			2 
#define GPIO2B_HSADC_DATA9			3 
#define GPIO2B_GPIO2B3				0 
#define GPIO2B_LCDC1_DATA11			1 
#define GPIO2B_SMC_ADDR15			2 
#define GPIO2B_GPIO2B2				0 
#define GPIO2B_LCDC1_DATA10			1 
#define GPIO2B_SMC_ADDR14			2 
#define GPIO2B_GPIO2B1				0 
#define GPIO2B_LCDC1_DATA9			1 
#define GPIO2B_SMC_ADDR13			2 
#define GPIO2B_GPIO2B0				0 
#define GPIO2B_LCDC1_DATA8			1 
#define GPIO2B_SMC_ADDR12			2 


//GPIO2C
#define GPIO2C_GPIO2C7				0 
#define GPIO2C_LCDC1_DATA23			1 
#define GPIO2C_SPI1_CSN1			2 
#define GPIO2C_HSADC_DATA4			3 
#define GPIO2C_GPIO2C6				0 
#define GPIO2C_LCDC1_DATA22			1 
#define GPIO2C_SPI1_RXD				2 
#define GPIO2C_HSADC_DATA3			3 
#define GPIO2C_GPIO2C5				0 
#define GPIO2C_LCDC1_DATA21			1 
#define GPIO2C_SPI1_TXD				2 
#define GPIO2C_HSADC_DATA2			3 
#define GPIO2C_GPIO2C4				0 
#define GPIO2C_LCDC1_DATA20			1 
#define GPIO2C_SPI1_CSN0			2 
#define GPIO2C_HSADC_DATA1			3 
#define GPIO2C_GPIO2C3				0 
#define GPIO2C_LCDC1_DATA19			1 
#define GPIO2C_SPI1_CLK				2 
#define GPIO2C_HSADC_DATA0			3 
#define GPIO2C_GPIO2C2				0 
#define GPIO2C_LCDC1_DATA18			1 
#define GPIO2C_SMC_BLS_N1			2 
#define GPIO2C_HSADC_DATA5			3 
#define GPIO2C_GPIO2C1				0 
#define GPIO2C_LCDC1_DATA17			1 
#define GPIO2C_SMC_BLS_N0			2 
#define GPIO2C_HSADC_DATA6			3 
#define GPIO2C_GPIO2C0				0 
#define GPIO2C_LCDC_DATA16			1 
#define GPIO2C_GPS_CLK				2 
#define GPIO2C_HSADC_CLKOUT			3 


//GPIO2D
#define GPIO2D_GPIO2D7				0 
#define GPIO2D_I2C1_SCL			 	1 
#define GPIO2D_GPIO2D6				0 
#define GPIO2D_I2C1_SDA			 	1 
#define GPIO2D_GPIO2D5				0 
#define GPIO2D_I2C0_SCL			 	1 
#define GPIO2D_GPIO2D4				0 
#define GPIO2D_I2C0_SDA			 	1 
#define GPIO2D_GPIO2D3				0 
#define GPIO2D_LCDC1_VSYNC			1 
#define GPIO2D_GPIO2D2				0 
#define GPIO2D_LCDC1_HSYNC			1 
#define GPIO2D_GPIO2D1				0 
#define GPIO2D_LCDC1_DEN			1 
#define GPIO2D_SMC_CSN1				2 
#define GPIO2D_GPIO2D0				0 
#define GPIO2D_LCDC1_DCLK			1 


//GPIO3A
#define GPIO3A_GPIO3A7				0 
#define GPIO3A_SDMMC0_WRITE_PRT			1 
#define GPIO3A_GPIO3A6				0 
#define GPIO3A_SDMMC0_RSTN_OUT			1 
#define GPIO3A_GPIO3A5				0 
#define GPIO3A_I2C4_SCL				1 
#define GPIO3A_GPIO3A4				0 
#define GPIO3A_I2C4_SDA			 	1 
#define GPIO3A_GPIO3A3				0 
#define GPIO3A_I2C3_SCL			 	1 
#define GPIO3A_GPIO3A2				0 
#define GPIO3A_I2C3_SDA			 	1 
#define GPIO3A_GPIO3A1				0 
#define GPIO3A_I2C2_SCL				1 
#define GPIO3A_GPIO3A0				0 
#define GPIO3A_I2C2_SDA			 	1 


//GPIO3B
#define GPIO3B_GPIO3B7				0 
#define GPIO3B_SDMMC0_WRITE_PRT			1 
#define GPIO3B_GPIO3B6				0 
#define GPIO3B_SDMMC0_DETECT_N			1 
#define GPIO3B_GPIO3B5				0 
#define GPIO3B_SDMMC0_DATA3			1 
#define GPIO3B_GPIO3B4				0 
#define GPIO3B_SDMMC0_DATA2			1 
#define GPIO3B_GPIO3B3				0 
#define GPIO3B_SDMMC0_DATA1			1 
#define GPIO3B_GPIO3B2				0 
#define GPIO3B_SDMMC0_DATA0			1 
#define GPIO3B_GPIO3B1				0 
#define GPIO3B_SDMMC0_CMD			1 
#define GPIO3B_GPIO3B0				0 
#define GPIO3B_SDMMC0_CLKOUT			1 


//GPIO3C
#define GPIO3C_GPIO3C7				0 
#define GPIO3C_SDMMC1_WRITE_PRT			1 
#define GPIO3C_GPIO3C6				0 
#define GPIO3C_SDMMC1_DETECT_N			1 
#define GPIO3C_GPIO3C5				0 
#define GPIO3C_SDMMC1_CLKOUT			1 
#define GPIO3C_GPIO3C4				0 
#define GPIO3C_SDMMC1_DATA3			1 
#define GPIO3C_GPIO3C3				0 
#define GPIO3C_SDMMC1_DATA2			1 
#define GPIO3C_GPIO3C2				0 
#define GPIO3C_SDMMC1_DATA1			1 
#define GPIO3C_GPIO3C1				0 
#define GPIO3C_SDMMC1_DATA0			1 
#define GPIO3C_GPIO3C0				0 
#define GPIO3C_SMMC1_CMD			1 


//GPIO3D
#define GPIO3D_GPIO3D7				0 
#define GPIO3D_FLASH_DQS			1 
#define GPIO3D_EMMC_CLKOUT			2 
#define GPIO3D_GPIO3D6				0 
#define GPIO3D_UART3_RTS_N			1 
#define GPIO3D_GPIO3D5				0 
#define GPIO3D_UART3_CTS_N			1 
#define GPIO3D_GPIO3D4				0 
#define GPIO3D_UART3_SOUT			1 
#define GPIO3D_GPIO3D3				0 
#define GPIO3D_UART3_SIN			1 
#define GPIO3D_GPIO3D2				0 
#define GPIO3D_SDMMC1_INT_N			1 
#define GPIO3D_GPIO3D1				0 
#define GPIO3D_SDMMC1_BACKEND_PWR		1 
#define GPIO3D_GPIO3D0				0 
#define GPIO3D_SDMMC1_PWR_EN			1 


//GPIO4A
#define GPIO4A_GPIO4A7				0 
#define GPIO4A_FLASH_DATA15			1 
#define GPIO4A_GPIO4A6				0 
#define GPIO4A_FLASH_DATA14			1 
#define GPIO4A_GPIO4A5				0 
#define GPIO4A_FLASH_DATA13			1 
#define GPIO4A_GPIO4A4				0 
#define GPIO4A_FLASH_DATA12			1 
#define GPIO4A_GPIO4A3				0 
#define GPIO4A_FLASH_DATA11			1 
#define GPIO4A_GPIO4A2				0 
#define GPIO4A_FLASH_DATA10			1 
#define GPIO4A_GPIO4A1				0 
#define GPIO4A_FLASH_DATA9			1 
#define GPIO4A_GPIO4A0				0 
#define GPIO4A_FLASH_DATA8			1 


//GPIO4B
#define GPIO4B_GPIO4B7				0 
#define GPIO4B_SPI0_CSN1			1 
#define GPIO4B_GPIO4B6				0 
#define GPIO4B_FLASH_CSN7			1 
#define GPIO4B_GPIO4B5				0 
#define GPIO4B_FLASH_CSN6			1 
#define GPIO4B_GPIO4B4				0 
#define GPIO4B_FLASH_CSN5			1 
#define GPIO4B_GPIO4B3				0 
#define GPIO4B_FLASH_CSN4			1 
#define GPIO4B_GPIO4B2				0 
#define GPIO4B_FLASH_CSN3			1 
#define GPIO4B_EMMC_RSTN_OUT			2 
#define GPIO4B_GPIO4B1				0 
#define GPIO4B_FLASH_CSN2			1 
#define GPIO4B_EMMC_CMD				2 
#define GPIO4B_GPIO4B0				0 
#define GPIO4B_FLASH_CSN1			1 


//GPIO4C
#define GPIO4C_GPIO4C7				0 
#define GPIO4C_SMC_DATA7			1 
#define GPIO4C_TRACE_DATA7			2 
#define GPIO4C_GPIO4C6				0 
#define GPIO4C_SMC_DATA6			1 
#define GPIO4C_TRACE_DATA6			2 
#define GPIO4C_GPIO4C5				0 
#define GPIO4C_SMC_DATA5			1 
#define GPIO4C_TRACE_DATA5			2 
#define GPIO4C_GPIO4C4				0 
#define GPIO4C_SMC_DATA4			1 
#define GPIO4C_TRACE_DATA4			2 
#define GPIO4C_GPIO4C3				0 
#define GPIO4C_SMC_DATA3			1 
#define GPIO4C_TRACE_DATA3			2 
#define GPIO4C_GPIO4C2				0 
#define GPIO4C_SMC_DATA2			1 
#define GPIO4C_TRACE_DATA2			2 
#define GPIO4C_GPIO4C1				0 
#define GPIO4C_SMC_DATA1			1 
#define GPIO4C_TRACE_DATA1			2 
#define GPIO4C_GPIO4C0				0 
#define GPIO4C_SMC_DATA0			1 
#define GPIO4C_TRACE_DATA0			2 


//GPIO4D
#define GPIO4D_GPIO4D7				0 
#define GPIO4D_SMC_DATA15			1 
#define GPIO4D_TRACE_DATA15			2 
#define GPIO4D_GPIO4D6				0 
#define GPIO4D_SMC_DATA14			1 
#define GPIO4D_TRACE_DATA14			2 
#define GPIO4D_GPIO4D5				0 
#define GPIO4D_SMC_DATA13			1 
#define GPIO4D_TRACE_DATA13			2 
#define GPIO4D_GPIO4D4				0 
#define GPIO4D_SMC_DATA12			1 
#define GPIO4D_TRACE_DATA12			2 
#define GPIO4D_GPIO4D3				0 
#define GPIO4D_SMC_DATA11			1 
#define GPIO4D_TRACE_DATA11			2 
#define GPIO4D_GPIO4D2				0 
#define GPIO4D_SMC_DATA10			1 
#define GPIO4D_TRACE_DATA10			2 
#define GPIO4D_GPIO4D1				0 
#define GPIO4D_SMC_DATA9			1 
#define GPIO4D_TRACE_DATA9			2 
#define GPIO4D_GPIO4D0				0 
#define GPIO4D_SMC_DATA8			1 
#define GPIO4D_TRACE_DATA8			2 


//GPIO6B
#define GPIO6B_GPIO6B7				0 
#define GPIO6B_TEST_CLOCK_OUT			1 


#define DEFAULT					0
#define INITIAL					1


#define      GRF_GPIO0L_DIR                       0x0000
#define      GRF_GPIO0H_DIR                       0x0004
#define      GRF_GPIO1L_DIR                       0x0008
#define      GRF_GPIO1H_DIR                       0x000c
#define      GRF_GPIO2L_DIR                       0x0010
#define      GRF_GPIO2H_DIR                       0x0014
#define      GRF_GPIO3L_DIR                       0x0018
#define      GRF_GPIO3H_DIR                       0x001c
#define      GRF_GPIO4L_DIR                       0x0020
#define      GRF_GPIO4H_DIR                       0x0024
#define      GRF_GPIO6L_DIR                       0x0030
#define      GRF_GPIO0L_DO                        0x0038
#define      GRF_GPIO0H_DO                        0x003c
#define      GRF_GPIO1L_DO                        0x0040
#define      GRF_GPIO1H_DO                        0x0044
#define      GRF_GPIO2L_DO                        0x0048
#define      GRF_GPIO2H_DO                        0x004c
#define      GRF_GPIO3L_DO                        0x0050
#define      GRF_GPIO3H_DO                        0x0054
#define      GRF_GPIO4L_DO                        0x0058
#define      GRF_GPIO4H_DO                        0x005c
#define      GRF_GPIO6L_DO                        0x0068
#define      GRF_GPIO0L_EN                        0x0070
#define      GRF_GPIO0H_EN                        0x0074
#define      GRF_GPIO1L_EN                        0x0078
#define      GRF_GPIO1H_EN                        0x007c
#define      GRF_GPIO2L_EN                        0x0080
#define      GRF_GPIO2H_EN                        0x0084
#define      GRF_GPIO3L_EN                        0x0088
#define      GRF_GPIO3H_EN                        0x008c
#define      GRF_GPIO4L_EN                        0x0090
#define      GRF_GPIO4H_EN                        0x0094
#define      GRF_GPIO6L_EN                        0x00a0
#define      GRF_GPIO0A_IOMUX                     RK30_GRF_BASE+0x00a8
#define      GRF_GPIO0B_IOMUX                     RK30_GRF_BASE+0x00ac
#define      GRF_GPIO0C_IOMUX                     RK30_GRF_BASE+0x00b0
#define      GRF_GPIO0D_IOMUX                     RK30_GRF_BASE+0x00b4
#define      GRF_GPIO1A_IOMUX                     RK30_GRF_BASE+0x00b8
#define      GRF_GPIO1B_IOMUX                     RK30_GRF_BASE+0x00bc
#define      GRF_GPIO1C_IOMUX                     RK30_GRF_BASE+0x00c0
#define      GRF_GPIO1D_IOMUX                     RK30_GRF_BASE+0x00c4
#define      GRF_GPIO2A_IOMUX                     RK30_GRF_BASE+0x00c8
#define      GRF_GPIO2B_IOMUX                     RK30_GRF_BASE+0x00cc
#define      GRF_GPIO2C_IOMUX                     RK30_GRF_BASE+0x00d0
#define      GRF_GPIO2D_IOMUX                     RK30_GRF_BASE+0x00d4
#define      GRF_GPIO3A_IOMUX                     RK30_GRF_BASE+0x00d8
#define      GRF_GPIO3B_IOMUX                     RK30_GRF_BASE+0x00dc
#define      GRF_GPIO3C_IOMUX                     RK30_GRF_BASE+0x00e0
#define      GRF_GPIO3D_IOMUX                     RK30_GRF_BASE+0x00e4
#define      GRF_GPIO4A_IOMUX                     RK30_GRF_BASE+0x00e8
#define      GRF_GPIO4B_IOMUX                     RK30_GRF_BASE+0x00ec
#define      GRF_GPIO4C_IOMUX                     RK30_GRF_BASE+0x00f0
#define      GRF_GPIO4D_IOMUX                     RK30_GRF_BASE+0x00f4
#define      GRF_GPIO6B_IOMUX                     RK30_GRF_BASE+0x010c
#define      GRF_GPIO0L_PULL                      0x0118
#define      GRF_GPIO0H_PULL                      0x011c
#define      GRF_GPIO1L_PULL                      0x0120
#define      GRF_GPIO1H_PULL                      0x0124
#define      GRF_GPIO2L_PULL                      0x0128
#define      GRF_GPIO2H_PULL                      0x012c
#define      GRF_GPIO3L_PULL                      0x0130
#define      GRF_GPIO3H_PULL                      0x0134
#define      GRF_GPIO4L_PULL                      0x0138
#define      GRF_GPIO4H_PULL                      0x013c
#define      GRF_GPIO6L_PULL                      0x0148
#define      GRF_SOC_CON0                         0x0150
#define      GRF_SOC_CON1                         0x0154
#define      GRF_SOC_CON2                         0x0158
#define      GRF_SOC_STATUS0                      0x015c
#define      GRF_DMAC1_CON0                       0x0160
#define      GRF_DMAC1_CON1                       0x0164
#define      GRF_DMAC1_CON2                       0x0168
#define      GRF_DMAC2_CON0                       0x016c
#define      GRF_DMAC2_CON1                       0x0170
#define      GRF_DMAC2_CON2                       0x0174
#define      GRF_DMAC2_CON3                       0x0178
#define      GRF_UOC0_CON0                        0x017c
#define      GRF_UOC0_CON1                        0x0180
#define      GRF_UOC0_CON2                        0x0184
#define      GRF_UOC1_CON0                        0x0188
#define      GRF_UOC1_CON1                        0x018c
#define      GRF_UOC1_CON2                        0x0190
#define      GRF_UOC1_CON3                        0x0194
#define      GRF_DDRC_CON0                        0x0198
#define      GRF_DDRC_STAT                        0x019c
#define      GRF_OS_REG0                          0x01c8
#define      GRF_OS_REG1                          0x01cc
#define      GRF_OS_REG2                          0x01d0
#define      GRF_OS_REG3                          0x01d4


//GPIO0A
#define GPIO0A7_I2S8CHSDI_NAME				"gpio0a7_i2s8chsdi_name"
#define	GPIO0A6_HOSTDRVVBUS_NAME			"gpio0a6_hostdrvvbus_name"
#define	GPIO0A5_OTGDRVVBUS_NAME				"gpio0a5_otgdrvvbus_name"
#define	GPIO0A4_PWM1_NAME				"gpio0a4_pwm1_name"
#define	GPIO0A3_PWM0_NAME				"gpio0a3_pwm0_name"
#define	GPIO0A2_HDMII2CSDA_NAME				"gpio0a2_hdmii2csda_name"
#define	GPIO0A1_HDMII2CSCL_NAME				"gpio0a1_hdmii2cscl_name"
#define	GPIO0A0_HDMIHOTPLUGIN_NAME			"gpio0a0_hdmihotplugin_name"


//GPIO0B
#define GPIO0B7_I2S8CHSDO3_NAME				"gpio0b7_i2s8chsdo3_name"
#define	GPIO0B6_I2S8CHSDO2_NAME				"gpio0b6_i2s8chsdo2_name"
#define	GPIO0B5_I2S8CHSDO1_NAME				"gpio0b5_i2s8chsdo1_name"
#define	GPIO0B4_I2S8CHSDO0_NAME				"gpio0b4_i2s8chsdo0_name"
#define	GPIO0B3_I2S8CHLRCKTX_NAME			"gpio0b3_i2s8chlrcktx_name"
#define	GPIO0B2_I2S8CHLRCKRX_NAME			"gpio0b2_i2s8chlrckrx_name"
#define	GPIO0B1_I2S8CHSCLK_NAME				"gpio0b1_i2s8chsclk_name"
#define	GPIO0B0_I2S8CHCLK_NAME				"gpio0b0_i2s8chclk_name"


//GPIO0C
#define	GPIO0C7_TRACECTL_SMCADDR3_NAME			"gpio0c7_tracectl_smcaddr3_name"
#define	GPIO0C6_TRACECLK_SMCADDR2_NAME			"gpio0c6_traceclk_smcaddr2_name"
#define	GPIO0C5_I2S12CHSDO_NAME				"gpio0c5_i2s12chsdo_name"
#define	GPIO0C4_I2S12CHSDI_NAME				"gpio0c4_i2s12chsdi_name"
#define	GPIO0C3_I2S12CHLRCKTX_NAME			"gpio0c3_i2s12chlrcktx_name"
#define	GPIO0C2_I2S8CHLRCKRX_NAME			"gpio0c2_i2s8chlrckrx_name"
#define	GPIO0C1_I2S12CHSCLK_NAME			"gpio0c1_i2s12chsclk_name"
#define	GPIO0C0_I2S12CHCLK_NAME				"gpio0c0_i2s12chclk_name"


//GPIO0D
#define	GPIO0D7_PWM3_NAME				"gpio0d7_pwm3_name"
#define	GPIO0D6_PWM2_NAME				"gpio0d6_pwm2_name"
#define	GPIO0D5_I2S22CHSDO_SMCADDR1_NAME		"gpio0d5_i2s22chsdo_smcaddr1_name"
#define	GPIO0D4_I2S12CHSDI_SMCADDR0_NAME		"gpio0d4_i2s12chsdi_smcaddr0_name"
#define	GPIO0D3_I2S12CHLRCKTX_SMCADVN_NAME		"gpio0d3_i2s12chlrcktx_smcadvn_name"
#define	GPIO0D2_I2S12CHLRCKRX_SMCOEN_NAME		"gpio0d2_i2s12chlrckrx_smcoen_name"
#define	GPIO0D1_I2S22CHSCLK_SMCWEN_NAME			"gpio0d1_i2s22chsclk_smcwen_name"
#define	GPIO0D0_I2S22CHCLK_SMCCSN0_NAME			"gpio0d0_i2s22chclk_smccsn0_name"


//GPIO1A
#define	GPIO1A7_UART1RTSN_SPI0TXD_NAME			"gpio1a7_uart1rtsn_spi0txd_name"
#define	GPIO1A6_UART1CTSN_SPI0RXD_NAME			"gpio1a6_uart1ctsn_spi0rxd_name"
#define	GPIO1A5_UART1SOUT_SPI0CLK_NAME			"gpio1a5_uart1sout_spi0clk_name"
#define	GPIO1A4_UART1SIN_SPI0CSN0_NAME			"gpio1a4_uart1sin_spi0csn0_name"
#define	GPIO1A3_UART0RTSN_NAME				"gpio1a3_uart0rtsn_name"
#define	GPIO1A2_UART0CTSN_NAME				"gpio1a2_uart0ctsn_name"
#define	GPIO1A1_UART0SOUT_NAME				"gpio1a1_uart0sout_name"
#define	GPIO1A0_UART0SIN_NAME				"gpio1a0_uart0sin_name"



//GPIO1B
#define GPIO1B7_CIFDATA11_NAME				"gpio1b7_cifdata11_name" 
#define GPIO1B6_CIFDATA10_NAME				"gpio1b6_cifdata10_name" 
#define GPIO1B5_CIF0DATA1_NAME				"gpio1b5_cif0data1_name" 
#define GPIO1B4_CIF0DATA0_NAME				"gpio1b4_cif0data0_name" 
#define GPIO1B3_CIF0CLKOUT_NAME				"gpio1b3_cif0clkout_name" 
#define GPIO1B2_SPDIFTX_NAME				"gpio1b2_spdiftx_name" 
#define GPIO1B1_UART2SOUT_NAME				"gpio1b1_uart2sout_name" 
#define GPIO1B0_UART2SIN_NAME				"gpio1b0_uart2sin_name" 


//GPIO1C
#define GPIO1C7_CIFDATA9_RMIIRXD0_NAME			"gpio1c7_cifdata9_rmiirxd0_name" 
#define GPIO1C6_CIFDATA8_RMIIRXD1_NAME			"gpio1c6_cifdata8_rmiirxd1_name" 
#define GPIO1C5_CIFDATA7_RMIICRSDVALID_NAME		"gpio1c5_cifdata7_rmiicrsdvalid_name" 
#define GPIO1C4_CIFDATA6_RMIIRXERR_NAME			"gpio1c4_cifdata6_rmiirxerr_name" 
#define GPIO1C3_CIFDATA5_RMIITXD0_NAME			"gpio1c3_cifdata5_rmiitxd0_name" 
#define GPIO1C2_CIF1DATA4_RMIITXD1_NAME			"gpio1c2_cif1data4_rmiitxd1_name" 
#define GPIO1C1_CIFDATA3_RMIITXEN_NAME			"gpio1c1_cifdata3_rmiitxen_name" 
#define GPIO1C0_CIF1DATA2_RMIICLKOUT_RMIICLKIN_NAME	"gpio1c0_cif1data2_rmiiclkout_rmiiclkin_name" 


//GPIO1D
#define GPIO1D7_CIF1CLKOUT_NAME				"gpio1d7_cif1clkout_name" 
#define GPIO1D6_CIF1DATA11_NAME				"gpio1d6_cif1data11_name" 
#define GPIO1D5_CIF1DATA10_NAME				"gpio1d5_cif1data10_name" 
#define GPIO1D4_CIF1DATA1_NAME				"gpio1d4_cif1data1_name" 
#define GPIO1D3_CIF1DATA0_NAME				"gpio1d3_cif1data0_name" 
#define GPIO1D2_CIF1CLKIN_NAME				"gpio1d2_cif1clkin_name" 
#define GPIO1D1_CIF1HREF_MIIMDCLK_NAME			"gpio1d1_cif1href_miimdclk_name" 
#define GPIO1D0_CIF1VSYNC_MIIMD_NAME			"gpio1d0_cif1vsync_miimd_name" 


//GPIO2A
#define GPIO2A7_LCDC1DATA7_SMCADDR11_NAME		"gpio2a7_lcdc1data7_smcaddr11_name" 
#define GPIO2A6_LCDC1DATA6_SMCADDR10_NAME		"gpio2a6_lcdc1data6_smcaddr10_name" 
#define GPIO2A5_LCDC1DATA5_SMCADDR9_NAME		"gpio2a5_lcdc1data5_smcaddr9_name" 
#define GPIO2A4_LCDC1DATA4_SMCADDR8_NAME		"gpio2a4_lcdc1data4_smcaddr8_name" 
#define GPIO2A3_LCDCDATA3_SMCADDR7_NAME			"gpio2a3_lcdcdata3_smcaddr7_name" 
#define GPIO2A2_LCDCDATA2_SMCADDR6_NAME			"gpio2a2_lcdcdata2_smcaddr6_name" 
#define GPIO2A1_LCDC1DATA1_SMCADDR5_NAME		"gpio2a1_lcdc1data1_smcaddr5_name" 
#define GPIO2A0_LCDC1DATA0_SMCADDR4_NAME		"gpio2a0_lcdc1data0_smcaddr4_name" 


//GPIO2B
#define GPIO2B7_LCDC1DATA15_SMCADDR19_HSADCDATA7_NAME	"gpio2b7_lcdc1data15_smcaddr19_hsadcdata7_name" 
#define GPIO2B6_LCDC1DATA14_SMCADDR18_TSSYNC_NAME	"gpio2b6_lcdc1data14_smcaddr18_tssync_name" 
#define GPIO2B5_LCDC1DATA13_SMCADDR17_HSADCDATA8_NAME	"gpio2b5_lcdc1data13_smcaddr17_hsadcdata8_name" 
#define GPIO2B4_LCDC1DATA12_SMCADDR16_HSADCDATA9_NAME	"gpio2b4_lcdc1data12_smcaddr16_hsadcdata9_name" 
#define GPIO2B3_LCDC1DATA11_SMCADDR15_NAME		"gpio2b3_lcdc1data11_smcaddr15_name" 
#define GPIO2B2_LCDC1DATA10_SMCADDR14_NAME		"gpio2b2_lcdc1data10_smcaddr14_name" 
#define GPIO2B1_LCDC1DATA9_SMCADDR13_NAME		"gpio2b1_lcdc1data9_smcaddr13_name" 
#define GPIO2B0_LCDC1DATA8_SMCADDR12_NAME		"gpio2b0_lcdc1data8_smcaddr12_name" 


//GPIO2C
#define GPIO2C7_LCDC1DATA23_SPI1CSN1_HSADCDATA4_NAME	"gpio2c7_lcdc1data23_spi1csn1_hsadcdata4_name" 
#define GPIO2C6_LCDC1DATA22_SPI1RXD_HSADCDATA3_NAME	"gpio2c6_lcdc1data22_spi1rxd_hsadcdata3_name" 
#define GPIO2C5_LCDC1DATA21_SPI1TXD_HSADCDATA2_NAME	"gpio2c5_lcdc1data21_spi1txd_hsadcdata2_name" 
#define GPIO2C4_LCDC1DATA20_SPI1CSN0_HSADCDATA1_NAME	"gpio2c4_lcdc1data20_spi1csn0_hsadcdata1_name" 
#define GPIO2C3_LCDC1DATA19_SPI1CLK_HSADCDATA0_NAME	"gpio2c3_lcdc1data19_spi1clk_hsadcdata0_name" 
#define GPIO2C2_LCDC1DATA18_SMCBLSN1_HSADCDATA5_NAME	"gpio2c2_lcdc1data18_smcblsn1_hsadcdata5_name" 
#define GPIO2C1_LCDC1DATA17_SMCBLSN0_HSADCDATA6_NAME	"gpio2c1_lcdc1data17_smcblsn0_hsadcdata6_name" 
#define GPIO2C0_LCDCDATA16_GPSCLK_HSADCCLKOUT_NAME	"gpio2c0_lcdcdata16_gpsclk_hsadcclkout_name" 


//GPIO2D
#define GPIO2D7_I2C1SCL_NAME				"gpio2d7_i2c1scl_name" 
#define GPIO2D6_I2C1SDA_NAME				"gpio2d6_i2c1sda_name" 
#define GPIO2D5_I2C0SCL_NAME				"gpio2d5_i2c0scl_name" 
#define GPIO2D4_I2C0SDA_NAME				"gpio2d4_i2c0sda_name" 
#define GPIO2D3_LCDC1VSYNC_NAME				"gpio2d3_lcdc1vsync_name" 
#define GPIO2D2_LCDC1HSYNC_NAME				"gpio2d2_lcdc1hsync_name" 
#define GPIO2D1_LCDC1DEN_SMCCSN1_NAME			"gpio2d1_lcdc1den_smccsn1_name" 
#define GPIO2D0_LCDC1DCLK_NAME				"gpio2d0_lcdc1dclk_name" 


//GPIO3A
#define GPIO3A7_SDMMC0WRITEPRT_NAME			"gpio3a7_sdmmc0writeprt_name" 
#define GPIO3A6_SDMMC0RSTNOUT_NAME			"gpio3a6_sdmmc0rstnout_name" 
#define GPIO3A5_I2C4SCL_NAME				"gpio3a5_i2c4scl_name" 
#define GPIO3A4_I2C4SDA_NAME				"gpio3a4_i2c4sda_name" 
#define GPIO3A3_I2C3SCL_NAME				"gpio3a3_i2c3scl_name" 
#define GPIO3A2_I2C3SDA_NAME				"gpio3a2_i2c3sda_name" 
#define GPIO3A1_I2C2SCL_NAME				"gpio3a1_i2c2scl_name" 
#define GPIO3A0_I2C2SDA_NAME				"gpio3a0_i2c2sda_name" 



//GPIO3B
#define GPIO3B7_SDMMC0WRITEPRT_NAME			"gpio3b7_sdmmc0writeprt_name" 
#define GPIO3B6_SDMMC0DETECTN_NAME			"gpio3b6_sdmmc0detectn_name" 
#define GPIO3B5_SDMMC0DATA3_NAME			"gpio3b5_sdmmc0data3_name" 
#define GPIO3B4_SDMMC0DATA2_NAME			"gpio3b4_sdmmc0data2_name" 
#define GPIO3B3_SDMMC0DATA1_NAME			"gpio3b3_sdmmc0data1_name" 
#define GPIO3B2_SDMMC0DATA0_NAME			"gpio3b2_sdmmc0data0_name" 
#define GPIO3B1_SDMMC0CMD_NAME				"gpio3b1_sdmmc0cmd_name" 
#define GPIO3B0_SDMMC0CLKOUT_NAME			"gpio3b0_sdmmc0clkout_name" 


//GPIO3C
#define GPIO3C7_SDMMC1WRITEPRT_NAME			"gpio3c7_sdmmc1writeprt_name" 
#define GPIO3C6_SDMMC1DETECTN_NAME			"gpio3c6_sdmmc1detectn_name" 
#define GPIO3C5_SDMMC1CLKOUT_NAME			"gpio3c5_sdmmc1clkout_name" 
#define GPIO3C4_SDMMC1DATA3_NAME			"gpio3c4_sdmmc1data3_name" 
#define GPIO3C3_SDMMC1DATA2_NAME			"gpio3c3_sdmmc1data2_name" 
#define GPIO3C2_SDMMC1DATA1_NAME			"gpio3c2_sdmmc1data1_name" 
#define GPIO3C1_SDMMC1DATA0_NAME			"gpio3c1_sdmmc1data0_name" 
#define GPIO3C0_SMMC1CMD_NAME				"gpio3c0_smmc1cmd_name" 


//GPIO3D
#define GPIO3D7_FLASHDQS_EMMCCLKOUT_NAME		"gpio3d7_flashdqs_emmcclkout_name" 
#define GPIO3D6_UART3RTSN_NAME				"gpio3d6_uart3rtsn_name" 
#define GPIO3D5_UART3CTSN_NAME				"gpio3d5_uart3ctsn_name" 
#define GPIO3D4_UART3SOUT_NAME				"gpio3d4_uart3sout_name" 
#define GPIO3D3_UART3SIN_NAME				"gpio3d3_uart3sin_name" 
#define GPIO3D2_SDMMC1INTN_NAME				"gpio3d2_sdmmc1intn_name" 
#define GPIO3D1_SDMMC1BACKENDPWR_NAME			"gpio3d1_sdmmc1backendpwr_name" 
#define GPIO3D0_SDMMC1PWREN_NAME			"gpio3d0_sdmmc1pwren_name" 


//GPIO4A
#define GPIO4A7_FLASHDATA15_NAME			"gpio4a7_flashdata15_name" 
#define GPIO4A6_FLASHDATA14_NAME			"gpio4a6_flashdata14_name" 
#define GPIO4A5_FLASHDATA13_NAME			"gpio4a5_flashdata13_name" 
#define GPIO4A4_FLASHDATA12_NAME			"gpio4a4_flashdata12_name" 
#define GPIO4A3_FLASHDATA11_NAME			"gpio4a3_flashdata11_name" 
#define GPIO4A2_FLASHDATA10_NAME			"gpio4a2_flashdata10_name" 
#define GPIO4A1_FLASHDATA9_NAME				"gpio4a1_flashdata9_name" 
#define GPIO4A0_FLASHDATA8_NAME				"gpio4a0_flashdata8_name" 


//GPIO4B
#define GPIO4B7_SPI0CSN1_NAME				"gpio4b7_spi0csn1_name" 
#define GPIO4B6_FLASHCSN7_NAME				"gpio4b6_flashcsn7_name" 
#define GPIO4B5_FLASHCSN6_NAME				"gpio4b5_flashcsn6_name" 
#define GPIO4B4_FLASHCSN5_NAME				"gpio4b4_flashcsn5_name" 
#define GPIO4B3_FLASHCSN4_NAME				"gpio4b3_flashcsn4_name" 
#define GPIO4B2_FLASHCSN3_EMMCRSTNOUT_NAME		"gpio4b2_flashcsn3_emmcrstnout_name" 
#define GPIO4B1_FLASHCSN2_EMMCCMD_NAME			"gpio4b1_flashcsn2_emmccmd_name" 
#define GPIO4B0_FLASHCSN1_NAME				"gpio4b0_flashcsn1_name" 


//GPIO4C
#define GPIO4C7_SMCDATA7_TRACEDATA7_NAME		"gpio4c7_smcdata7_tracedata7_name" 
#define GPIO4C6_SMCDATA6_TRACEDATA6_NAME		"gpio4c6_smcdata6_tracedata6_name" 
#define GPIO4C5_SMCDATA5_TRACEDATA5_NAME		"gpio4c5_smcdata5_tracedata5_name" 
#define GPIO4C4_SMCDATA4_TRACEDATA4_NAME		"gpio4c4_smcdata4_tracedata4_name" 
#define GPIO4C3_SMCDATA3_TRACEDATA3_NAME		"gpio4c3_smcdata3_tracedata3_name" 
#define GPIO4C2_SMCDATA2_TRACEDATA2_NAME		"gpio4c2_smcdata2_tracedata2_name" 
#define GPIO4C1_SMCDATA1_TRACEDATA1_NAME		"gpio4c1_smcdata1_tracedata1_name" 
#define GPIO4C0_SMCDATA0_TRACEDATA0_NAME		"gpio4c0_smcdata0_tracedata0_name" 


//GPIO4D
#define GPIO4D7_SMCDATA15_TRACEDATA15_NAME		"gpio4d7_smcdata15_tracedata15_name" 
#define GPIO4D6_SMCDATA14_TRACEDATA14_NAME		"gpio4d6_smcdata14_tracedata14_name" 
#define GPIO4D5_SMCDATA13_TRACEDATA13_NAME		"gpio4d5_smcdata13_tracedata13_name" 
#define GPIO4D4_SMCDATA12_TRACEDATA12_NAME		"gpio4d4_smcdata12_tracedata12_name" 
#define GPIO4D3_SMCDATA11_TRACEDATA11_NAME		"gpio4d3_smcdata11_tracedata11_name" 
#define GPIO4D2_SMCDATA10_TRACEDATA10_NAME		"gpio4d2_smcdata10_tracedata10_name" 
#define GPIO4D1_SMCDATA9_TRACEDATA9_NAME		"gpio4d1_smcdata9_tracedata9_name" 
#define GPIO4D0_SMCDATA8_TRACEDATA8_NAME		"gpio4d0_smcdata8_tracedata8_name" 



//GPIO6B
#define GPIO6B7_TESTCLOCKOUT_NAME			"gpio6b7_testclockout_name" 

#define MUX_CFG(desc,reg,off,interl,mux_mode,bflags)	\
{						  	\
        .name = desc,                                   \
        .offset = off,                               	\
        .interleave = interl,                       	\
        .mux_reg = GRF_##reg##_IOMUX,          \
        .mode = mux_mode,                               \
        .premode = mux_mode,                            \
        .flags = bflags,				\
},

struct mux_config {
	char *name;
	const unsigned int offset;
	unsigned int mode;
	unsigned int premode;
	const void* __iomem mux_reg;
	const unsigned int interleave;
	unsigned int flags;
};
#define rk29_mux_api_set rk30_mux_api_set

extern int __init rk30_iomux_init(void);
extern void rk30_mux_api_set(char *name, unsigned int mode);

#endif
