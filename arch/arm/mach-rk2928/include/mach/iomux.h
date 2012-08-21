/*
 * arch/arm/mach-rk2928/include/mach/iomux.h
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

#ifndef __RK2928_IOMUX_H__
#define __RK2928_IOMUX_H__

#include <linux/init.h>

//gpio0a
#define	GPIO0A_GPIO0A0                  0
#define	GPIO0A_I2C0_SCL                 1
#define	GPIO0A_GPIO0A1                  0
#define	GPIO0A_I2C0_SDA                 1
#define	GPIO0A_GPIO0A2                  0
#define	GPIO0A_I2C1_SCL                 1
#define	GPIO0A_GPIO0A3                  0
#define	GPIO0A_I2C1_SDA                 1
#define	GPIO0A_GPIO0A6                  0
#define	GPIO0A_I2C3_SCL                 1
#define	GPIO0A_HDMI_DDCSCL              2
#define	GPIO0A_GPIO0A7                  0
#define	GPIO0A_I2C3_SDA                 1
#define	GPIO0A_HDMI_DDCSDA              2

//gpio0b
#define	GPIO0B_GPIO0B0                  0
#define	GPIO0B_MMC1_CMD                 1
#define	GPIO0B_GPIO0B1                  0
#define	GPIO0B_MMC1_CLKOUT              1
#define	GPIO0B_GPIO0B2                  0
#define	GPIO0B_MMC1_DETN                1
#define	GPIO0B_GPIO0B3                  0
#define	GPIO0B_MMC1_D0                  1
#define	GPIO0B_GPIO0B4                  0
#define	GPIO0B_MMC1_D1                  1
#define	GPIO0B_GPIO0B5                  0
#define	GPIO0B_MMC1_D2                  1
#define	GPIO0B_GPIO0B6                  0
#define	GPIO0B_MMC1_D3                  1
#define	GPIO0B_GPIO0B7                  0
#define	GPIO0B_HDMI_HOTPLUGIN           1

//gpio0c
#define	GPIO0C_GPIO0C0                  0
#define	GPIO0C_UART0_SOUT               1
#define	GPIO0C_GPIO0C1                  0
#define	GPIO0C_UART0_SIN                1
#define	GPIO0C_GPIO0C2                  0
#define	GPIO0C_UART0_RTSN               1
#define	GPIO0C_GPIO0C3                  0
#define	GPIO0C_UART0_CTSN               1
#define	GPIO0C_GPIO0C4                  0
#define	GPIO0C_HDMI_CECSDA              1
#define	GPIO0C_GPIO0C7                  0
#define	GPIO0C_NAND_CS1                 1

//gpio0d
#define	GPIO0D_GPIO0D0                  0
#define	GPIO0D_UART2_RTSN               1
#define	GPIO0D_GPIO0D1                  0
#define	GPIO0D_UART2_CTSN               1
#define	GPIO0D_GPIO0D2                  0
#define	GPIO0D_PWM_0                    1
#define	GPIO0D_GPIO0D3                  0
#define	GPIO0D_PWM_1                    1
#define	GPIO0D_GPIO0D4                  0
#define	GPIO0D_PWM_2                    1
#define	GPIO0D_GPIO0D5                  0
#define	GPIO0D_MMC1_WRPRT               1
#define	GPIO0D_GPIO0D6                  0
#define	GPIO0D_MMC1_PWREN               1
#define	GPIO0D_GPIO0D7                  0
#define	GPIO0D_MMC1_BKEPWR              1

//gpio1a
#define	GPIO1A_GPIO1A0                  0
#define	GPIO1A_I2S_MCLK                 1
#define	GPIO1A_GPIO1A1                  0
#define	GPIO1A_I2S_SCLK                 1
#define	GPIO1A_GPIO1A2                  0
#define	GPIO1A_I2S_LRCKRX               1
#define	GPIO1A_GPS_CLK                  2
#define	GPIO1A_GPIO1A3                  0
#define	GPIO1A_I2S_LRCKTX               1
#define	GPIO1A_GPIO1A4                  0
#define	GPIO1A_I2S_SDO                  1
#define	GPIO1A_GPS_MAG                  2
#define	GPIO1A_GPIO1A5                  0
#define	GPIO1A_I2S_SDI                  1
#define	GPIO1A_GPS_SIGN                 2
#define	GPIO1A_GPIO1A6                  0
#define	GPIO1A_MMC1_INTN                1
#define	GPIO1A_GPIO1A7                  0
#define	GPIO1A_MMC0_WRPRT               1

//gpio1b
#define	GPIO1B_GPIO1B0                  0
#define	GPIO1B_SPI_CLK                  1
#define	GPIO1B_UART1_CTSN               2
#define	GPIO1B_GPIO1B1                  0
#define	GPIO1B_SPI_TXD                  1
#define	GPIO1B_UART1_SOUT               2
#define	GPIO1B_GPIO1B2                  0
#define	GPIO1B_SPI_RXD                  1
#define	GPIO1B_UART1_SIN                2
#define	GPIO1B_GPIO1B3                  0
#define	GPIO1B_SPI_CSN0                 1
#define	GPIO1B_UART1_RTSN               2
#define	GPIO1B_GPIO1B4                  0
#define	GPIO1B_SPI_CSN1                 1
#define	GPIO1B_GPIO1B5                  0
#define	GPIO1B_MMC0_RSTNOUT             1
#define	GPIO1B_GPIO1B6                  0
#define	GPIO1B_MMC0_PWREN               1
#define	GPIO1B_GPIO1B7                  0
#define	GPIO1B_MMC0_CMD                 1

//gpio1c
#define	GPIO1C_GPIO1C0                  0
#define	GPIO1C_MMC0_CLKOUT              1
#define	GPIO1C_GPIO1C1                  0
#define	GPIO1C_MMC0_DETN                1
#define	GPIO1C_GPIO1C2                  0
#define	GPIO1C_MMC0_D0                  1
#define	GPIO1C_GPIO1C3                  0
#define	GPIO1C_MMC0_D1                  1
#define	GPIO1C_GPIO1C4                  0
#define	GPIO1C_MMC0_D2                  1
#define	GPIO1C_GPIO1C5                  0
#define	GPIO1C_MMC0_D3                  1
#define	GPIO1C_GPIO1C6                  0
#define	GPIO1C_NAND_CS2                 1
#define	GPIO1C_EMMC_CMD                 2
#define	GPIO1C_GPIO1C7                  0
#define	GPIO1C_NAND_CS3                 1
#define	GPIO1C_EMMC_RSTNOUT             2

//gpio1d
#define	GPIO1D_GPIO1D0                  0
#define	GPIO1D_NAND_D0                  1
#define	GPIO1D_EMMC_D0                  2
#define	GPIO1D_GPIO1D1                  0
#define	GPIO1D_NAND_D1                  1
#define	GPIO1D_EMMC_D1                  2
#define	GPIO1D_GPIO1D2                  0
#define	GPIO1D_NAND_D2                  1
#define	GPIO1D_EMMC_D2                  2
#define	GPIO1D_GPIO1D3                  0
#define	GPIO1D_NAND_D3                  1
#define	GPIO1D_EMMC_D3                  2
#define	GPIO1D_GPIO1D4                  0
#define	GPIO1D_NAND_D4                  1
#define	GPIO1D_EMMC_D4                  2
#define	GPIO1D_GPIO1D5                  0
#define	GPIO1D_NAND_D5                  1
#define	GPIO1D_EMMC_D5                  2
#define	GPIO1D_GPIO1D6                  0
#define	GPIO1D_NAND_D6                  1
#define	GPIO1D_EMMC_D6                  2
#define	GPIO1D_GPIO1D7                  0
#define	GPIO1D_NAND_D7                  1
#define	GPIO1D_EMMC_D7                  2

//gpio2a
#define	GPIO2A_GPIO2A0                  0
#define	GPIO2A_NAND_ALE                 1
#define	GPIO2A_GPIO2A1                  0
#define	GPIO2A_NAND_CLE                 1
#define	GPIO2A_GPIO2A2                  0
#define	GPIO2A_NAND_WRN                 1
#define	GPIO2A_GPIO2A3                  0
#define	GPIO2A_NAND_RDN                 1
#define	GPIO2A_GPIO2A4                  0
#define	GPIO2A_NAND_RDY                 1
#define	GPIO2A_GPIO2A5                  0
#define	GPIO2A_NAND_WP                  1
#define	GPIO2A_EMMC_PWREN               2
#define	GPIO2A_GPIO2A6                  0
#define	GPIO2A_NAND_CS0                 1
#define	GPIO2A_GPIO2A7                  0
#define	GPIO2A_NAND_DPS                 1
#define	GPIO2A_EMMC_CLKOUT              2

//gpio2b
#define	GPIO2B_GPIO2B0                  0
#define	GPIO2B_LCDC0_DCLK               1
#define	GPIO2B_LCDC1_DCLK               2
#define	GPIO2B_GPIO2B1                  0
#define	GPIO2B_LCDC0_HSYNC              1
#define	GPIO2B_LCDC1_HSYNC              2
#define	GPIO2B_GPIO2B2                  0
#define	GPIO2B_LCDC0_VSYNC              1
#define	GPIO2B_LCDC1_VSYNC              2
#define	GPIO2B_GPIO2B3                  0
#define	GPIO2B_LCDC0_DEN                1
#define	GPIO2B_LCDC1_DEN                2
#define	GPIO2B_GPIO2B4                  0
#define	GPIO2B_LCDC0_D10                1
#define	GPIO2B_LCDC1_D10                2
#define	GPIO2B_GPIO2B5                  0
#define	GPIO2B_LCDC0_D11                1
#define	GPIO2B_LCDC1_D11                2
#define	GPIO2B_GPIO2B6                  0
#define	GPIO2B_LCDC0_D12                1
#define	GPIO2B_LCDC1_D12                2
#define	GPIO2B_GPIO2B7                  0
#define	GPIO2B_LCDC0_D13                1
#define	GPIO2B_LCDC1_D13                2

//gpio2c
#define	GPIO2C_GPIO2C0                  0
#define	GPIO2C_LCDC0_D14                1
#define	GPIO2C_LCDC1_D14                2
#define	GPIO2C_GPIO2C1                  0
#define	GPIO2C_LCDC0_D15                1
#define	GPIO2C_LCDC1_D15                2
#define	GPIO2C_GPIO2C2                  0
#define	GPIO2C_LCDC0_D16                1
#define	GPIO2C_LCDC1_D16                2
#define	GPIO2C_GPIO2C3                  0
#define	GPIO2C_LCDC0_D17                1
#define	GPIO2C_LCDC1_D17                2
#define	GPIO2C_GPIO2C4                  0
#define	GPIO2C_LCDC0_D18                1
#define	GPIO2C_LCDC1_D18                2
#define	GPIO2C_I2C2_SDA                 3
#define	GPIO2C_GPIO2C5                  0
#define	GPIO2C_LCDC0_D19                1
#define	GPIO2C_LCDC1_D19                2
#define	GPIO2C_I2C2_SCL                 3
#define	GPIO2C_GPIO2C6                  0
#define	GPIO2C_LCDC0_D20                1
#define	GPIO2C_LCDC1_D20                2
#define	GPIO2C_UART2_SIN                3
#define	GPIO2C_GPIO2C7                  0
#define	GPIO2C_LCDC0_D21                1
#define	GPIO2C_LCDC1_D21                2
#define	GPIO2C_UART2_SOUT               3

//gpio2d
#define	GPIO2D_GPIO2D0                  0
#define	GPIO2D_LCDC0_D22                1
#define	GPIO2D_LCDC1_D22                2
#define	GPIO2D_GPIO2D1                  0
#define	GPIO2D_LCDC0_D23                1
#define	GPIO2D_LCDC1_D23                2

//gpio3c
#define	GPIO3C_GPIO3C1                  0
#define	GPIO3C_OTG_DRVVBUS              1

//gpio3d
#define	GPIO3D_GPIO3D7                  0
#define	GPIO3D_TESTCLK_OUT              1



//gpio0a
#define	GPIO0A0_I2C0_SCL_NAME                           "gpio0a0_i2c0_scl_name"
#define	GPIO0A1_I2C0_SDA_NAME                           "gpio0a1_i2c0_sda_name"
#define	GPIO0A2_I2C1_SCL_NAME                           "gpio0a2_i2c1_scl_name"
#define	GPIO0A3_I2C1_SDA_NAME                           "gpio0a3_i2c1_sda_name"
#define	GPIO0A6_I2C3_SCL_HDMI_DDCSCL_NAME               "gpio0a6_i2c3_scl_hdmi_ddcscl_name"
#define	GPIO0A7_I2C3_SDA_HDMI_DDCSDA_NAME               "gpio0a7_i2c3_sda_hdmi_ddcsda_name"

//gpio0b
#define	GPIO0B0_MMC1_CMD_NAME                           "gpio0b0_mmc1_cmd_name"
#define	GPIO0B1_MMC1_CLKOUT_NAME                        "gpio0b1_mmc1_clkout_name"
#define	GPIO0B2_MMC1_DETN_NAME                          "gpio0b2_mmc1_detn_name"
#define	GPIO0B3_MMC1_D0_NAME                            "gpio0b3_mmc1_d0_name"
#define	GPIO0B4_MMC1_D1_NAME                            "gpio0b4_mmc1_d1_name"
#define	GPIO0B5_MMC1_D2_NAME                            "gpio0b5_mmc1_d2_name"
#define	GPIO0B6_MMC1_D3_NAME                            "gpio0b6_mmc1_d3_name"
#define	GPIO0B7_HDMI_HOTPLUGIN_NAME                     "gpio0b7_hdmi_hotplugin_name"

//gpio0c
#define	GPIO0C0_UART0_SOUT_NAME                         "gpio0c0_uart0_sout_name"
#define	GPIO0C1_UART0_SIN_NAME                          "gpio0c1_uart0_sin_name"
#define	GPIO0C2_UART0_RTSN_NAME                         "gpio0c2_uart0_rtsn_name"
#define	GPIO0C3_UART0_CTSN_NAME                         "gpio0c3_uart0_ctsn_name"
#define	GPIO0C4_HDMI_CECSDA_NAME                        "gpio0c4_hdmi_cecsda_name"
#define	GPIO0C7_NAND_CS1_NAME                           "gpio0c7_nand_cs1_name"

//gpio0d
#define	GPIO0D0_UART2_RTSN_NAME                         "gpio0d0_uart2_rtsn_name"
#define	GPIO0D1_UART2_CTSN_NAME                         "gpio0d1_uart2_ctsn_name"
#define	GPIO0D2_PWM_0_NAME                              "gpio0d2_pwm_0_name"
#define	GPIO0D3_PWM_1_NAME                              "gpio0d3_pwm_1_name"
#define	GPIO0D4_PWM_2_NAME                              "gpio0d4_pwm_2_name"
#define	GPIO0D5_MMC1_WRPRT_NAME                         "gpio0d5_mmc1_wrprt_name"
#define	GPIO0D6_MMC1_PWREN_NAME                         "gpio0d6_mmc1_pwren_name"
#define	GPIO0D7_MMC1_BKEPWR_NAME                        "gpio0d7_mmc1_bkepwr_name"

//gpio1a
#define	GPIO1A0_I2S_MCLK_NAME                           "gpio1a0_i2s_mclk_name"
#define	GPIO1A1_I2S_SCLK_NAME                           "gpio1a1_i2s_sclk_name"
#define	GPIO1A2_I2S_LRCKRX_GPS_CLK_NAME                 "gpio1a2_i2s_lrckrx_gps_clk_name"
#define	GPIO1A3_I2S_LRCKTX_NAME                         "gpio1a3_i2s_lrcktx_name"
#define	GPIO1A4_I2S_SDO_GPS_MAG_NAME                    "gpio1a4_i2s_sdo_gps_mag_name"
#define	GPIO1A5_I2S_SDI_GPS_SIGN_NAME                   "gpio1a5_i2s_sdi_gps_sign_name"
#define	GPIO1A6_MMC1_INTN_NAME                          "gpio1a6_mmc1_intn_name"
#define	GPIO1A7_MMC0_WRPRT_NAME                         "gpio1a7_mmc0_wrprt_name"

//gpio1b
#define	GPIO1B0_SPI_CLK_UART1_CTSN_NAME                 "gpio1b0_spi_clk_uart1_ctsn_name"
#define	GPIO1B1_SPI_TXD_UART1_SOUT_NAME                 "gpio1b1_spi_txd_uart1_sout_name"
#define	GPIO1B2_SPI_RXD_UART1_SIN_NAME                  "gpio1b2_spi_rxd_uart1_sin_name"
#define	GPIO1B3_SPI_CSN0_UART1_RTSN_NAME                "gpio1b3_spi_csn0_uart1_rtsn_name"
#define	GPIO1B4_SPI_CSN1_NAME                           "gpio1b4_spi_csn1_name"
#define	GPIO1B5_MMC0_RSTNOUT_NAME                       "gpio1b5_mmc0_rstnout_name"
#define	GPIO1B6_MMC0_PWREN_NAME                         "gpio1b6_mmc0_pwren_name"
#define	GPIO1B7_MMC0_CMD_NAME                           "gpio1b7_mmc0_cmd_name"

//gpio1c
#define	GPIO1C0_MMC0_CLKOUT_NAME                        "gpio1c0_mmc0_clkout_name"
#define	GPIO1C1_MMC0_DETN_NAME                          "gpio1c1_mmc0_detn_name"
#define	GPIO1C2_MMC0_D0_NAME                            "gpio1c2_mmc0_d0_name"
#define	GPIO1C3_MMC0_D1_NAME                            "gpio1c3_mmc0_d1_name"
#define	GPIO1C4_MMC0_D2_NAME                            "gpio1c4_mmc0_d2_name"
#define	GPIO1C5_MMC0_D3_NAME                            "gpio1c5_mmc0_d3_name"
#define	GPIO1C6_NAND_CS2_EMMC_CMD_NAME                  "gpio1c6_nand_cs2_emmc_cmd_name"
#define	GPIO1C7_NAND_CS3_EMMC_RSTNOUT_NAME              "gpio1c7_nand_cs3_emmc_rstnout_name"

//gpio1d
#define	GPIO1D0_NAND_D0_EMMC_D0_NAME                    "gpio1d0_nand_d0_emmc_d0_name"
#define	GPIO1D1_NAND_D1_EMMC_D1_NAME                    "gpio1d1_nand_d1_emmc_d1_name"
#define	GPIO1D2_NAND_D2_EMMC_D2_NAME                    "gpio1d2_nand_d2_emmc_d2_name"
#define	GPIO1D3_NAND_D3_EMMC_D3_NAME                    "gpio1d3_nand_d3_emmc_d3_name"
#define	GPIO1D4_NAND_D4_EMMC_D4_NAME                    "gpio1d4_nand_d4_emmc_d4_name"
#define	GPIO1D5_NAND_D5_EMMC_D5_NAME                    "gpio1d5_nand_d5_emmc_d5_name"
#define	GPIO1D6_NAND_D6_EMMC_D6_NAME                    "gpio1d6_nand_d6_emmc_d6_name"
#define	GPIO1D7_NAND_D7_EMMC_D7_NAME                    "gpio1d7_nand_d7_emmc_d7_name"

//gpio2a
#define	GPIO2A0_NAND_ALE_NAME                           "gpio2a0_nand_ale_name"
#define	GPIO2A1_NAND_CLE_NAME                           "gpio2a1_nand_cle_name"
#define	GPIO2A2_NAND_WRN_NAME                           "gpio2a2_nand_wrn_name"
#define	GPIO2A3_NAND_RDN_NAME                           "gpio2a3_nand_rdn_name"
#define	GPIO2A4_NAND_RDY_NAME                           "gpio2a4_nand_rdy_name"
#define	GPIO2A5_NAND_WP_EMMC_PWREN_NAME                 "gpio2a5_nand_wp_emmc_pwren_name"
#define	GPIO2A6_NAND_CS0_NAME                           "gpio2a6_nand_cs0_name"
#define	GPIO2A7_NAND_DPS_EMMC_CLKOUT_NAME               "gpio2a7_nand_dps_emmc_clkout_name"

//gpio2b
#define	GPIO2B0_LCDC0_DCLK_LCDC1_DCLK_NAME              "gpio2b0_lcdc0_dclk_lcdc1_dclk_name"
#define	GPIO2B1_LCDC0_HSYNC_LCDC1_HSYNC_NAME            "gpio2b1_lcdc0_hsync_lcdc1_hsync_name"
#define	GPIO2B2_LCDC0_VSYNC_LCDC1_VSYNC_NAME            "gpio2b2_lcdc0_vsync_lcdc1_vsync_name"
#define	GPIO2B3_LCDC0_DEN_LCDC1_DEN_NAME                "gpio2b3_lcdc0_den_lcdc1_den_name"
#define	GPIO2B4_LCDC0_D10_LCDC1_D10_NAME                "gpio2b4_lcdc0_d10_lcdc1_d10_name"
#define	GPIO2B5_LCDC0_D11_LCDC1_D11_NAME                "gpio2b5_lcdc0_d11_lcdc1_d11_name"
#define	GPIO2B6_LCDC0_D12_LCDC1_D12_NAME                "gpio2b6_lcdc0_d12_lcdc1_d12_name"
#define	GPIO2B7_LCDC0_D13_LCDC1_D13_NAME                "gpio2b7_lcdc0_d13_lcdc1_d13_name"

//gpio2c
#define	GPIO2C0_LCDC0_D14_LCDC1_D14_NAME                "gpio2c0_lcdc0_d14_lcdc1_d14_name"
#define	GPIO2C1_LCDC0_D15_LCDC1_D15_NAME                "gpio2c1_lcdc0_d15_lcdc1_d15_name"
#define	GPIO2C2_LCDC0_D16_LCDC1_D16_NAME                "gpio2c2_lcdc0_d16_lcdc1_d16_name"
#define	GPIO2C3_LCDC0_D17_LCDC1_D17_NAME                "gpio2c3_lcdc0_d17_lcdc1_d17_name"
#define	GPIO2C4_LCDC0_D18_LCDC1_D18_I2C2_SDA_NAME       "gpio2c4_lcdc0_d18_lcdc1_d18_i2c2_sda_name"
#define	GPIO2C5_LCDC0_D19_LCDC1_D19_I2C2_SCL_NAME       "gpio2c5_lcdc0_d19_lcdc1_d19_i2c2_scl_name"
#define	GPIO2C6_LCDC0_D20_LCDC1_D20_UART2_SIN_NAME      "gpio2c6_lcdc0_d20_lcdc1_d20_uart2_sin_name"
#define	GPIO2C7_LCDC0_D21_LCDC1_D21_UART2_SOUT_NAME     "gpio2c7_lcdc0_d21_lcdc1_d21_uart2_sout_name"

//gpio2d
#define	GPIO2D0_LCDC0_D22_LCDC1_D22_NAME                "gpio2d0_lcdc0_d22_lcdc1_d22_name"
#define	GPIO2D1_LCDC0_D23_LCDC1_D23_NAME                "gpio2d1_lcdc0_d23_lcdc1_d23_name"

//gpio3c
#define	GPIO3C1_OTG_DRVVBUS_NAME                        "gpio3c1_otg_drvvbus_name"

//gpio3d
#define	GPIO3D7_TESTCLK_OUT_NAME                        "gpio3d7_testclk_out_name"


#define DEFAULT					0
#define INITIAL					1

#define      GRF_GPIO0A_IOMUX                     RK2928_GRF_BASE+0x00a8
#define      GRF_GPIO0B_IOMUX                     RK2928_GRF_BASE+0x00ac
#define      GRF_GPIO0C_IOMUX                     RK2928_GRF_BASE+0x00b0
#define      GRF_GPIO0D_IOMUX                     RK2928_GRF_BASE+0x00b4

#define      GRF_GPIO1A_IOMUX                     RK2928_GRF_BASE+0x00b8
#define      GRF_GPIO1B_IOMUX                     RK2928_GRF_BASE+0x00bc
#define      GRF_GPIO1C_IOMUX                     RK2928_GRF_BASE+0x00c0
#define      GRF_GPIO1D_IOMUX                     RK2928_GRF_BASE+0x00c4

#define      GRF_GPIO2A_IOMUX                     RK2928_GRF_BASE+0x00c8
#define      GRF_GPIO2B_IOMUX                     RK2928_GRF_BASE+0x00cc
#define      GRF_GPIO2C_IOMUX                     RK2928_GRF_BASE+0x00d0
#define      GRF_GPIO2D_IOMUX                     RK2928_GRF_BASE+0x00d4

#define      GRF_GPIO3C_IOMUX                     RK2928_GRF_BASE+0x00e0
#define      GRF_GPIO3D_IOMUX                     RK2928_GRF_BASE+0x00e4

#define      GRF_GPIO0L_PULL                      0x0118
#define      GRF_GPIO0H_PULL                      0x011c

#define      GRF_GPIO1L_PULL                      0x0120
#define      GRF_GPIO1H_PULL                      0x0124

#define      GRF_GPIO2L_PULL                      0x0128
#define      GRF_GPIO2H_PULL                      0x012c

#define      GRF_GPIO3L_PULL                      0x0130
#define      GRF_GPIO3H_PULL                      0x0134

#define      GRF_SOC_CON0                         0x0140
#define      GRF_SOC_CON1                         0x0144
#define      GRF_SOC_CON2                         0x0148

#define      GRF_SOC_STATUS0                      0x014c

#define      GRF_LVDS_CON0                        0x0150

#define      GRF_DMAC1_CON0                       0x015c
#define      GRF_DMAC1_CON1                       0x0160
#define      GRF_DMAC1_CON2                       0x0164

#define      GRF_UOC0_CON0                        0x016c
#define      GRF_UOC0_CON1                        0x0170
#define      GRF_UOC0_CON2                        0x0174
#define      GRF_UOC0_CON3                        0x0178
#define      GRF_UOC0_CON5                        0x017c

#define      GRF_UOC1_CON0                        0x0180
#define      GRF_UOC1_CON1                        0x0184
#define      GRF_UOC1_CON2                        0x0188
#define      GRF_UOC1_CON3                        0x018c
#define      GRF_UOC1_CON4                        0x0190
#define      GRF_UOC1_CON5                        0x0194

#define      GRF_DDRC_STAT                        0x019c

#define      GRF_OS_REG0                          0x01c8
#define      GRF_OS_REG1                          0x01cc
#define      GRF_OS_REG2                          0x01d0
#define      GRF_OS_REG3                          0x01d4
#define      GRF_OS_REG4                          0x01d8
#define      GRF_OS_REG5                          0x01dc
#define      GRF_OS_REG6                          0x01e0
#define      GRF_OS_REG7                          0x01e4

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
extern int rk30_mux_api_get(char *name);

#endif
