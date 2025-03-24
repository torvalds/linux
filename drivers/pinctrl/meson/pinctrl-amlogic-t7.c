// SPDX-License-Identifier: (GPL-2.0-only OR MIT)
/*
 * Pin controller and GPIO driver for Amlogic T7 SoC.
 *
 * Copyright (c) 2023 Amlogic, Inc. All rights reserved.
 * Author: Huqiang Qin <huqiang.qin@amlogic.com>
 */

#include <dt-bindings/gpio/amlogic,t7-periphs-pinctrl.h>
#include "pinctrl-meson.h"
#include "pinctrl-meson-axg-pmx.h"

static const struct pinctrl_pin_desc t7_periphs_pins[] = {
	MESON_PIN(GPIOB_0),
	MESON_PIN(GPIOB_1),
	MESON_PIN(GPIOB_2),
	MESON_PIN(GPIOB_3),
	MESON_PIN(GPIOB_4),
	MESON_PIN(GPIOB_5),
	MESON_PIN(GPIOB_6),
	MESON_PIN(GPIOB_7),
	MESON_PIN(GPIOB_8),
	MESON_PIN(GPIOB_9),
	MESON_PIN(GPIOB_10),
	MESON_PIN(GPIOB_11),
	MESON_PIN(GPIOB_12),

	MESON_PIN(GPIOC_0),
	MESON_PIN(GPIOC_1),
	MESON_PIN(GPIOC_2),
	MESON_PIN(GPIOC_3),
	MESON_PIN(GPIOC_4),
	MESON_PIN(GPIOC_5),
	MESON_PIN(GPIOC_6),

	MESON_PIN(GPIOX_0),
	MESON_PIN(GPIOX_1),
	MESON_PIN(GPIOX_2),
	MESON_PIN(GPIOX_3),
	MESON_PIN(GPIOX_4),
	MESON_PIN(GPIOX_5),
	MESON_PIN(GPIOX_6),
	MESON_PIN(GPIOX_7),
	MESON_PIN(GPIOX_8),
	MESON_PIN(GPIOX_9),
	MESON_PIN(GPIOX_10),
	MESON_PIN(GPIOX_11),
	MESON_PIN(GPIOX_12),
	MESON_PIN(GPIOX_13),
	MESON_PIN(GPIOX_14),
	MESON_PIN(GPIOX_15),
	MESON_PIN(GPIOX_16),
	MESON_PIN(GPIOX_17),
	MESON_PIN(GPIOX_18),
	MESON_PIN(GPIOX_19),

	MESON_PIN(GPIOW_0),
	MESON_PIN(GPIOW_1),
	MESON_PIN(GPIOW_2),
	MESON_PIN(GPIOW_3),
	MESON_PIN(GPIOW_4),
	MESON_PIN(GPIOW_5),
	MESON_PIN(GPIOW_6),
	MESON_PIN(GPIOW_7),
	MESON_PIN(GPIOW_8),
	MESON_PIN(GPIOW_9),
	MESON_PIN(GPIOW_10),
	MESON_PIN(GPIOW_11),
	MESON_PIN(GPIOW_12),
	MESON_PIN(GPIOW_13),
	MESON_PIN(GPIOW_14),
	MESON_PIN(GPIOW_15),
	MESON_PIN(GPIOW_16),

	MESON_PIN(GPIOD_0),
	MESON_PIN(GPIOD_1),
	MESON_PIN(GPIOD_2),
	MESON_PIN(GPIOD_3),
	MESON_PIN(GPIOD_4),
	MESON_PIN(GPIOD_5),
	MESON_PIN(GPIOD_6),
	MESON_PIN(GPIOD_7),
	MESON_PIN(GPIOD_8),
	MESON_PIN(GPIOD_9),
	MESON_PIN(GPIOD_10),
	MESON_PIN(GPIOD_11),
	MESON_PIN(GPIOD_12),

	MESON_PIN(GPIOE_0),
	MESON_PIN(GPIOE_1),
	MESON_PIN(GPIOE_2),
	MESON_PIN(GPIOE_3),
	MESON_PIN(GPIOE_4),
	MESON_PIN(GPIOE_5),
	MESON_PIN(GPIOE_6),

	MESON_PIN(GPIOZ_0),
	MESON_PIN(GPIOZ_1),
	MESON_PIN(GPIOZ_2),
	MESON_PIN(GPIOZ_3),
	MESON_PIN(GPIOZ_4),
	MESON_PIN(GPIOZ_5),
	MESON_PIN(GPIOZ_6),
	MESON_PIN(GPIOZ_7),
	MESON_PIN(GPIOZ_8),
	MESON_PIN(GPIOZ_9),
	MESON_PIN(GPIOZ_10),
	MESON_PIN(GPIOZ_11),
	MESON_PIN(GPIOZ_12),
	MESON_PIN(GPIOZ_13),

	MESON_PIN(GPIOT_0),
	MESON_PIN(GPIOT_1),
	MESON_PIN(GPIOT_2),
	MESON_PIN(GPIOT_3),
	MESON_PIN(GPIOT_4),
	MESON_PIN(GPIOT_5),
	MESON_PIN(GPIOT_6),
	MESON_PIN(GPIOT_7),
	MESON_PIN(GPIOT_8),
	MESON_PIN(GPIOT_9),
	MESON_PIN(GPIOT_10),
	MESON_PIN(GPIOT_11),
	MESON_PIN(GPIOT_12),
	MESON_PIN(GPIOT_13),
	MESON_PIN(GPIOT_14),
	MESON_PIN(GPIOT_15),
	MESON_PIN(GPIOT_16),
	MESON_PIN(GPIOT_17),
	MESON_PIN(GPIOT_18),
	MESON_PIN(GPIOT_19),
	MESON_PIN(GPIOT_20),
	MESON_PIN(GPIOT_21),
	MESON_PIN(GPIOT_22),
	MESON_PIN(GPIOT_23),

	MESON_PIN(GPIOM_0),
	MESON_PIN(GPIOM_1),
	MESON_PIN(GPIOM_2),
	MESON_PIN(GPIOM_3),
	MESON_PIN(GPIOM_4),
	MESON_PIN(GPIOM_5),
	MESON_PIN(GPIOM_6),
	MESON_PIN(GPIOM_7),
	MESON_PIN(GPIOM_8),
	MESON_PIN(GPIOM_9),
	MESON_PIN(GPIOM_10),
	MESON_PIN(GPIOM_11),
	MESON_PIN(GPIOM_12),
	MESON_PIN(GPIOM_13),

	MESON_PIN(GPIOY_0),
	MESON_PIN(GPIOY_1),
	MESON_PIN(GPIOY_2),
	MESON_PIN(GPIOY_3),
	MESON_PIN(GPIOY_4),
	MESON_PIN(GPIOY_5),
	MESON_PIN(GPIOY_6),
	MESON_PIN(GPIOY_7),
	MESON_PIN(GPIOY_8),
	MESON_PIN(GPIOY_9),
	MESON_PIN(GPIOY_10),
	MESON_PIN(GPIOY_11),
	MESON_PIN(GPIOY_12),
	MESON_PIN(GPIOY_13),
	MESON_PIN(GPIOY_14),
	MESON_PIN(GPIOY_15),
	MESON_PIN(GPIOY_16),
	MESON_PIN(GPIOY_17),
	MESON_PIN(GPIOY_18),

	MESON_PIN(GPIOH_0),
	MESON_PIN(GPIOH_1),
	MESON_PIN(GPIOH_2),
	MESON_PIN(GPIOH_3),
	MESON_PIN(GPIOH_4),
	MESON_PIN(GPIOH_5),
	MESON_PIN(GPIOH_6),
	MESON_PIN(GPIOH_7),

	MESON_PIN(GPIO_TEST_N),
};

/* Bank B func1 */
static const unsigned int emmc_nand_d0_pins[]		= { GPIOB_0 };
static const unsigned int emmc_nand_d1_pins[]		= { GPIOB_1 };
static const unsigned int emmc_nand_d2_pins[]		= { GPIOB_2 };
static const unsigned int emmc_nand_d3_pins[]		= { GPIOB_3 };
static const unsigned int emmc_nand_d4_pins[]		= { GPIOB_4 };
static const unsigned int emmc_nand_d5_pins[]		= { GPIOB_5 };
static const unsigned int emmc_nand_d6_pins[]		= { GPIOB_6 };
static const unsigned int emmc_nand_d7_pins[]		= { GPIOB_7 };
static const unsigned int emmc_clk_pins[]		= { GPIOB_8 };
static const unsigned int emmc_cmd_pins[]		= { GPIOB_10 };
static const unsigned int emmc_nand_ds_pins[]		= { GPIOB_11 };

/* Bank B func2 */
static const unsigned int nor_hold_pins[]		= { GPIOB_3 };
static const unsigned int nor_d_pins[]			= { GPIOB_4 };
static const unsigned int nor_q_pins[]			= { GPIOB_5 };
static const unsigned int nor_c_pins[]			= { GPIOB_6 };
static const unsigned int nor_wp_pins[]			= { GPIOB_7 };
static const unsigned int nor_cs_pins[]			= { GPIOB_12 };

/* Bank C func1 */
static const unsigned int sdcard_d0_pins[]		= { GPIOC_0 };
static const unsigned int sdcard_d1_pins[]		= { GPIOC_1 };
static const unsigned int sdcard_d2_pins[]		= { GPIOC_2 };
static const unsigned int sdcard_d3_pins[]		= { GPIOC_3 };
static const unsigned int sdcard_clk_pins[]		= { GPIOC_4 };
static const unsigned int sdcard_cmd_pins[]		= { GPIOC_5 };
static const unsigned int gen_clk_out_c_pins[]		= { GPIOC_6 };

/* Bank C func2 */
static const unsigned int jtag_b_tdo_pins[]		= { GPIOC_0 };
static const unsigned int jtag_b_tdi_pins[]		= { GPIOC_1 };
static const unsigned int uart_ao_a_rx_c_pins[]		= { GPIOC_2 };
static const unsigned int uart_ao_a_tx_c_pins[]		= { GPIOC_3 };
static const unsigned int jtag_b_clk_pins[]		= { GPIOC_4 };
static const unsigned int jtag_b_tms_pins[]		= { GPIOC_5 };

/* Bank C func3 */
static const unsigned int spi1_mosi_c_pins[]		= { GPIOC_0 };
static const unsigned int spi1_miso_c_pins[]		= { GPIOC_1 };
static const unsigned int spi1_sclk_c_pins[]		= { GPIOC_2 };
static const unsigned int spi1_ss0_c_pins[]		= { GPIOC_3 };

/* Bank X func1 */
static const unsigned int sdio_d0_pins[]		= { GPIOX_0 };
static const unsigned int sdio_d1_pins[]		= { GPIOX_1 };
static const unsigned int sdio_d2_pins[]		= { GPIOX_2 };
static const unsigned int sdio_d3_pins[]		= { GPIOX_3 };
static const unsigned int sdio_clk_pins[]		= { GPIOX_4 };
static const unsigned int sdio_cmd_pins[]		= { GPIOX_5 };
static const unsigned int pwm_b_pins[]			= { GPIOX_6 };
static const unsigned int pwm_c_pins[]			= { GPIOX_7 };
static const unsigned int tdm_d0_pins[]			= { GPIOX_8 };
static const unsigned int tdm_d1_pins[]			= { GPIOX_9 };
static const unsigned int tdm_fs0_pins[]		= { GPIOX_10 };
static const unsigned int tdm_sclk0_pins[]		= { GPIOX_11 };
static const unsigned int uart_c_tx_pins[]		= { GPIOX_12 };
static const unsigned int uart_c_rx_pins[]		= { GPIOX_13 };
static const unsigned int uart_c_cts_pins[]		= { GPIOX_14 };
static const unsigned int uart_c_rts_pins[]		= { GPIOX_15 };
static const unsigned int pwm_a_pins[]			= { GPIOX_16 };
static const unsigned int i2c2_sda_x_pins[]		= { GPIOX_17 };
static const unsigned int i2c2_sck_x_pins[]		= { GPIOX_18 };
static const unsigned int pwm_d_pins[]			= { GPIOX_19 };

/* Bank X func2 */
static const unsigned int clk12_24_x_pins[]		= { GPIOX_14 };

/* Bank W func1 */
static const unsigned int hdmirx_a_hpd_pins[]		= { GPIOW_0 };
static const unsigned int hdmirx_a_det_pins[]		= { GPIOW_1 };
static const unsigned int hdmirx_a_sda_pins[]		= { GPIOW_2 };
static const unsigned int hdmirx_a_sck_pins[]		= { GPIOW_3 };
static const unsigned int hdmirx_c_hpd_pins[]		= { GPIOW_4 };
static const unsigned int hdmirx_c_det_pins[]		= { GPIOW_5 };
static const unsigned int hdmirx_c_sda_pins[]		= { GPIOW_6 };
static const unsigned int hdmirx_c_sck_pins[]		= { GPIOW_7 };
static const unsigned int hdmirx_b_hpd_pins[]		= { GPIOW_8 };
static const unsigned int hdmirx_b_det_pins[]		= { GPIOW_9 };
static const unsigned int hdmirx_b_sda_pins[]		= { GPIOW_10 };
static const unsigned int hdmirx_b_sck_pins[]		= { GPIOW_11 };
static const unsigned int cec_a_pins[]			= { GPIOW_12 };
static const unsigned int hdmitx_sda_w13_pins[]		= { GPIOW_13 };
static const unsigned int hdmitx_sck_w14_pins[]		= { GPIOW_14 };
static const unsigned int hdmitx_hpd_in_pins[]		= { GPIOW_15 };
static const unsigned int cec_b_pins[]			= { GPIOW_16 };

/* Bank W func2 */
static const unsigned int uart_ao_a_tx_w2_pins[]	= { GPIOW_2 };
static const unsigned int uart_ao_a_rx_w3_pins[]	= { GPIOW_3 };
static const unsigned int uart_ao_a_tx_w6_pins[]	= { GPIOW_6 };
static const unsigned int uart_ao_a_rx_w7_pins[]	= { GPIOW_7 };
static const unsigned int uart_ao_a_tx_w10_pins[]	= { GPIOW_10 };
static const unsigned int uart_ao_a_rx_w11_pins[]	= { GPIOW_11 };

/* Bank W func3 */
static const unsigned int hdmitx_sda_w2_pins[]		= { GPIOW_2 };
static const unsigned int hdmitx_sck_w3_pins[]		= { GPIOW_3 };

/* Bank D func1 */
static const unsigned int uart_ao_a_tx_d0_pins[]	= { GPIOD_0 };
static const unsigned int uart_ao_a_rx_d1_pins[]	= { GPIOD_1 };
static const unsigned int i2c0_ao_sck_d_pins[]		= { GPIOD_2 };
static const unsigned int i2c0_ao_sda_d_pins[]		= { GPIOD_3 };
static const unsigned int remote_out_d4_pins[]		= { GPIOD_4 };
static const unsigned int remote_in_pins[]		= { GPIOD_5 };
static const unsigned int jtag_a_clk_pins[]		= { GPIOD_6 };
static const unsigned int jtag_a_tms_pins[]		= { GPIOD_7 };
static const unsigned int jtag_a_tdi_pins[]		= { GPIOD_8 };
static const unsigned int jtag_a_tdo_pins[]		= { GPIOD_9 };
static const unsigned int gen_clk_out_d_pins[]		= { GPIOD_10 };
static const unsigned int pwm_ao_g_d11_pins[]		= { GPIOD_11 };
static const unsigned int wd_rsto_pins[]		= { GPIOD_12 };

/* Bank D func2 */
static const unsigned int i2c0_slave_ao_sck_pins[]	= { GPIOD_2 };
static const unsigned int i2c0_slave_ao_sda_pins[]	= { GPIOD_3 };
static const unsigned int rtc_clk_in_pins[]		= { GPIOD_4 };
static const unsigned int pwm_ao_h_d5_pins[]		= { GPIOD_5 };
static const unsigned int pwm_ao_c_d_pins[]		= { GPIOD_6 };
static const unsigned int pwm_ao_g_d7_pins[]		= { GPIOD_7 };
static const unsigned int spdif_out_d_pins[]		= { GPIOD_8 };
static const unsigned int spdif_in_d_pins[]		= { GPIOD_9 };
static const unsigned int pwm_ao_h_d10_pins[]		= { GPIOD_10 };

/* Bank D func3 */
static const unsigned int uart_ao_b_tx_pins[]		= { GPIOD_2 };
static const unsigned int uart_ao_b_rx_pins[]		= { GPIOD_3 };
static const unsigned int uart_ao_b_cts_pins[]		= { GPIOD_4 };
static const unsigned int pwm_ao_c_hiz_pins[]		= { GPIOD_6 };
static const unsigned int pwm_ao_g_hiz_pins[]		= { GPIOD_7 };
static const unsigned int uart_ao_b_rts_pins[]		= { GPIOD_10 };

/* Bank D func4 */
static const unsigned int remote_out_d6_pins[]		= { GPIOD_6 };

/* Bank E func1 */
static const unsigned int pwm_ao_a_pins[]		= { GPIOE_0 };
static const unsigned int pwm_ao_b_pins[]		= { GPIOE_1 };
static const unsigned int pwm_ao_c_e_pins[]		= { GPIOE_2 };
static const unsigned int pwm_ao_d_pins[]		= { GPIOE_3 };
static const unsigned int pwm_ao_e_pins[]		= { GPIOE_4 };
static const unsigned int pwm_ao_f_pins[]		= { GPIOE_5 };
static const unsigned int pwm_ao_g_e_pins[]		= { GPIOE_6 };

/* Bank E func2 */
static const unsigned int i2c0_ao_sck_e_pins[]		= { GPIOE_0 };
static const unsigned int i2c0_ao_sda_e_pins[]		= { GPIOE_1 };
static const unsigned int clk25m_pins[]			= { GPIOE_2 };
static const unsigned int i2c1_ao_sck_pins[]		= { GPIOE_3 };
static const unsigned int i2c1_ao_sda_pins[]		= { GPIOE_4 };
static const unsigned int rtc_clk_out_pins[]		= { GPIOD_5 };

/* Bank E func3 */
static const unsigned int clk12_24_e_pins[]		= { GPIOE_4 };

/* Bank Z func1 */
static const unsigned int eth_mdio_pins[]		= { GPIOZ_0 };
static const unsigned int eth_mdc_pins[]		= { GPIOZ_1 };
static const unsigned int eth_rgmii_rx_clk_pins[]	= { GPIOZ_2 };
static const unsigned int eth_rx_dv_pins[]		= { GPIOZ_3 };
static const unsigned int eth_rxd0_pins[]		= { GPIOZ_4 };
static const unsigned int eth_rxd1_pins[]		= { GPIOZ_5 };
static const unsigned int eth_rxd2_rgmii_pins[]		= { GPIOZ_6 };
static const unsigned int eth_rxd3_rgmii_pins[]		= { GPIOZ_7 };
static const unsigned int eth_rgmii_tx_clk_pins[]	= { GPIOZ_8 };
static const unsigned int eth_txen_pins[]		= { GPIOZ_9 };
static const unsigned int eth_txd0_pins[]		= { GPIOZ_10 };
static const unsigned int eth_txd1_pins[]		= { GPIOZ_11 };
static const unsigned int eth_txd2_rgmii_pins[]		= { GPIOZ_12 };
static const unsigned int eth_txd3_rgmii_pins[]		= { GPIOZ_13 };

/* Bank Z func2 */
static const unsigned int iso7816_clk_z_pins[]		= { GPIOZ_0 };
static const unsigned int iso7816_data_z_pins[]		= { GPIOZ_1 };
static const unsigned int tsin_b_valid_pins[]		= { GPIOZ_2 };
static const unsigned int tsin_b_sop_pins[]		= { GPIOZ_3 };
static const unsigned int tsin_b_din0_pins[]		= { GPIOZ_4 };
static const unsigned int tsin_b_clk_pins[]		= { GPIOZ_5 };
static const unsigned int tsin_b_fail_pins[]		= { GPIOZ_6 };
static const unsigned int tsin_b_din1_pins[]		= { GPIOZ_7 };
static const unsigned int tsin_b_din2_pins[]		= { GPIOZ_8 };
static const unsigned int tsin_b_din3_pins[]		= { GPIOZ_9 };
static const unsigned int tsin_b_din4_pins[]		= { GPIOZ_10 };
static const unsigned int tsin_b_din5_pins[]		= { GPIOZ_11 };
static const unsigned int tsin_b_din6_pins[]		= { GPIOZ_12 };
static const unsigned int tsin_b_din7_pins[]		= { GPIOZ_13 };

/* Bank Z func3 */
static const unsigned int tsin_c_z_valid_pins[]		= { GPIOZ_6 };
static const unsigned int tsin_c_z_sop_pins[]		= { GPIOZ_7 };
static const unsigned int tsin_c_z_din0_pins[]		= { GPIOZ_8 };
static const unsigned int tsin_c_z_clk_pins[]		= { GPIOZ_9  };
static const unsigned int tsin_d_z_valid_pins[]		= { GPIOZ_10 };
static const unsigned int tsin_d_z_sop_pins[]		= { GPIOZ_11 };
static const unsigned int tsin_d_z_din0_pins[]		= { GPIOZ_12 };
static const unsigned int tsin_d_z_clk_pins[]		= { GPIOZ_13 };

/* Bank Z func4 */
static const unsigned int spi4_mosi_pins[]		= { GPIOZ_0 };
static const unsigned int spi4_miso_pins[]		= { GPIOZ_1 };
static const unsigned int spi4_sclk_pins[]		= { GPIOZ_2 };
static const unsigned int spi4_ss0_pins[]		= { GPIOZ_3 };
static const unsigned int spi5_mosi_pins[]		= { GPIOZ_4 };
static const unsigned int spi5_miso_pins[]		= { GPIOZ_5 };
static const unsigned int spi5_sclk_pins[]		= { GPIOZ_6 };
static const unsigned int spi5_ss0_pins[]		= { GPIOZ_7 };

/* Bank T func1 */
static const unsigned int mclk1_pins[]			= { GPIOT_0 };
static const unsigned int tdm_sclk1_pins[]		= { GPIOT_1 };
static const unsigned int tdm_fs1_pins[]		= { GPIOT_2 };
static const unsigned int tdm_d2_pins[]			= { GPIOT_3 };
static const unsigned int tdm_d3_pins[]			= { GPIOT_4 };
static const unsigned int tdm_d4_pins[]			= { GPIOT_5 };
static const unsigned int tdm_d5_pins[]			= { GPIOT_6 };
static const unsigned int tdm_d6_pins[]			= { GPIOT_7 };
static const unsigned int tdm_d7_pins[]			= { GPIOT_8 };
static const unsigned int tdm_d8_pins[]			= { GPIOT_9 };
static const unsigned int tdm_d9_pins[]			= { GPIOT_10 };
static const unsigned int tdm_d10_pins[]		= { GPIOT_11 };
static const unsigned int tdm_d11_pins[]		= { GPIOT_12 };
static const unsigned int mclk2_pins[]			= { GPIOT_13 };
static const unsigned int tdm_sclk2_pins[]		= { GPIOT_14 };
static const unsigned int tdm_fs2_pins[]		= { GPIOT_15 };
static const unsigned int i2c1_sck_pins[]		= { GPIOT_16 };
static const unsigned int i2c1_sda_pins[]		= { GPIOT_17 };
static const unsigned int spi0_mosi_pins[]		= { GPIOT_18 };
static const unsigned int spi0_miso_pins[]		= { GPIOT_19 };
static const unsigned int spi0_sclk_pins[]		= { GPIOT_20 };
static const unsigned int spi0_ss0_pins[]		= { GPIOT_21 };
static const unsigned int spi0_ss1_pins[]		= { GPIOT_22 };
static const unsigned int spi0_ss2_pins[]		= { GPIOT_23 };

/* Bank T func2 */
static const unsigned int spdif_in_t_pins[]		= { GPIOT_3 };
static const unsigned int spdif_out_t_pins[]		= { GPIOT_4 };
static const unsigned int iso7816_clk_t_pins[]		= { GPIOT_5 };
static const unsigned int iso7816_data_t_pins[]		= { GPIOT_6 };
static const unsigned int tsin_a_sop_t_pins[]		= { GPIOT_7 };
static const unsigned int tsin_a_din0_t_pins[]		= { GPIOT_8 };
static const unsigned int tsin_a_clk_t_pins[]		= { GPIOT_9 };
static const unsigned int tsin_a_valid_t_pins[]		= { GPIOT_10 };
static const unsigned int i2c0_sck_t_pins[]		= { GPIOT_20 };
static const unsigned int i2c0_sda_t_pins[]		= { GPIOT_21 };
static const unsigned int i2c2_sck_t_pins[]		= { GPIOT_22 };
static const unsigned int i2c2_sda_t_pins[]		= { GPIOT_23 };

/* Bank T func3 */
static const unsigned int spi3_mosi_pins[]		= { GPIOT_6 };
static const unsigned int spi3_miso_pins[]		= { GPIOT_7 };
static const unsigned int spi3_sclk_pins[]		= { GPIOT_8 };
static const unsigned int spi3_ss0_pins[]		= { GPIOT_9 };

/* Bank M func1 */
static const unsigned int tdm_d12_pins[]		= { GPIOM_0 };
static const unsigned int tdm_d13_pins[]		= { GPIOM_1 };
static const unsigned int tdm_d14_pins[]		= { GPIOM_2 };
static const unsigned int tdm_d15_pins[]		= { GPIOM_3 };
static const unsigned int tdm_sclk3_pins[]		= { GPIOM_4 };
static const unsigned int tdm_fs3_pins[]		= { GPIOM_5 };
static const unsigned int i2c3_sda_m_pins[]		= { GPIOM_6 };
static const unsigned int i2c3_sck_m_pins[]		= { GPIOM_7 };
static const unsigned int spi1_mosi_m_pins[]		= { GPIOM_8 };
static const unsigned int spi1_miso_m_pins[]		= { GPIOM_9 };
static const unsigned int spi1_sclk_m_pins[]		= { GPIOM_10 };
static const unsigned int spi1_ss0_m_pins[]		= { GPIOM_11 };
static const unsigned int spi1_ss1_m_pins[]		= { GPIOM_12 };
static const unsigned int spi1_ss2_m_pins[]		= { GPIOM_13 };

/* Bank M func2 */
static const unsigned int pdm_din1_m0_pins[]		= { GPIOM_0 };
static const unsigned int pdm_din2_pins[]		= { GPIOM_1 };
static const unsigned int pdm_din3_pins[]		= { GPIOM_2 };
static const unsigned int pdm_dclk_pins[]		= { GPIOM_3 };
static const unsigned int pdm_din0_pins[]		= { GPIOM_4 };
static const unsigned int pdm_din1_m5_pins[]		= { GPIOM_5 };
static const unsigned int uart_d_tx_m_pins[]		= { GPIOM_8 };
static const unsigned int uart_d_rx_m_pins[]		= { GPIOM_9 };
static const unsigned int uart_d_cts_m_pins[]		= { GPIOM_10 };
static const unsigned int uart_d_rts_m_pins[]		= { GPIOM_11 };
static const unsigned int i2c2_sda_m_pins[]		= { GPIOM_12 };
static const unsigned int i2c2_sck_m_pins[]		= { GPIOM_13 };

/* Bank Y func1 */
static const unsigned int spi2_mosi_pins[]		= { GPIOY_0 };
static const unsigned int spi2_miso_pins[]		= { GPIOY_1 };
static const unsigned int spi2_sclk_pins[]		= { GPIOY_2 };
static const unsigned int spi2_ss0_pins[]		= { GPIOY_3 };
static const unsigned int spi2_ss1_pins[]		= { GPIOY_4 };
static const unsigned int spi2_ss2_pins[]		= { GPIOY_5 };
static const unsigned int uart_e_tx_pins[]		= { GPIOY_6 };
static const unsigned int uart_e_rx_pins[]		= { GPIOY_7 };
static const unsigned int uart_e_cts_pins[]		= { GPIOY_8 };
static const unsigned int uart_e_rts_pins[]		= { GPIOY_9 };
static const unsigned int uart_d_cts_y_pins[]		= { GPIOY_10 };
static const unsigned int uart_d_rts_y_pins[]		= { GPIOY_11 };
static const unsigned int uart_d_tx_y_pins[]		= { GPIOY_12 };
static const unsigned int uart_d_rx_y_pins[]		= { GPIOY_13 };
static const unsigned int i2c4_sck_y_pins[]		= { GPIOY_15 };
static const unsigned int i2c4_sda_y_pins[]		= { GPIOY_16 };
static const unsigned int i2c5_sck_pins[]		= { GPIOY_17 };
static const unsigned int i2c5_sda_pins[]		= { GPIOY_18 };

/* Bank Y func2 */
static const unsigned int tsin_c_y_sop_pins[]		= { GPIOY_4 };
static const unsigned int tsin_c_y_din0_pins[]		= { GPIOY_5 };
static const unsigned int tsin_c_y_clk_pins[]		= { GPIOY_6 };
static const unsigned int tsin_c_y_valid_pins[]		= { GPIOY_7 };
static const unsigned int tsin_d_y_sop_pins[]		= { GPIOY_8 };
static const unsigned int tsin_d_y_din0_pins[]		= { GPIOY_9 };
static const unsigned int tsin_d_y_clk_pins[]		= { GPIOY_10 };
static const unsigned int tsin_d_y_valid_pins[]		= { GPIOY_11 };
static const unsigned int pcieck_reqn_y_pins[]		= { GPIOY_18 };

/* Bank Y func3 */
static const unsigned int pwm_e_pins[]			= { GPIOY_1 };
static const unsigned int hsync_pins[]			= { GPIOY_4 };
static const unsigned int vsync_pins[]			= { GPIOY_5 };
static const unsigned int pwm_f_pins[]			= { GPIOY_8 };
static const unsigned int sync_3d_out_pins[]		= { GPIOY_9 };
static const unsigned int vx1_a_htpdn_pins[]		= { GPIOY_10 };
static const unsigned int vx1_b_htpdn_pins[]		= { GPIOY_11 };
static const unsigned int vx1_a_lockn_pins[]		= { GPIOY_12 };
static const unsigned int vx1_b_lockn_pins[]		= { GPIOY_13 };
static const unsigned int pwm_vs_y_pins[]		= { GPIOY_14 };

/* Bank Y func4 */
static const unsigned int edp_a_hpd_pins[]		= { GPIOY_10 };
static const unsigned int edp_b_hpd_pins[]		= { GPIOY_11 };

/* Bank H func1 */
static const unsigned int mic_mute_key_pins[]		= { GPIOH_0 };
static const unsigned int mic_mute_led_pins[]		= { GPIOH_1 };
static const unsigned int i2c3_sck_h_pins[]		= { GPIOH_2 };
static const unsigned int i2c3_sda_h_pins[]		= { GPIOH_3 };
static const unsigned int i2c4_sck_h_pins[]		= { GPIOH_4 };
static const unsigned int i2c4_sda_h_pins[]		= { GPIOH_5 };
static const unsigned int eth_link_led_pins[]		= { GPIOH_6 };
static const unsigned int eth_act_led_pins[]		= { GPIOH_7 };

/* Bank H func2 */
static const unsigned int pwm_vs_h_pins[]		= { GPIOH_1 };
static const unsigned int uart_f_tx_pins[]		= { GPIOH_2 };
static const unsigned int uart_f_rx_pins[]		= { GPIOH_3 };
static const unsigned int uart_f_cts_pins[]		= { GPIOH_4 };
static const unsigned int uart_f_rts_pins[]		= { GPIOH_5 };
static const unsigned int i2c0_sda_h_pins[]		= { GPIOH_6 };
static const unsigned int i2c0_sck_h_pins[]		= { GPIOH_7 };

/* Bank H func3 */
static const unsigned int pcieck_reqn_h_pins[]		= { GPIOH_2 };

static const struct meson_pmx_group t7_periphs_groups[] = {
	GPIO_GROUP(GPIOB_0),
	GPIO_GROUP(GPIOB_1),
	GPIO_GROUP(GPIOB_2),
	GPIO_GROUP(GPIOB_3),
	GPIO_GROUP(GPIOB_4),
	GPIO_GROUP(GPIOB_5),
	GPIO_GROUP(GPIOB_6),
	GPIO_GROUP(GPIOB_7),
	GPIO_GROUP(GPIOB_8),
	GPIO_GROUP(GPIOB_9),
	GPIO_GROUP(GPIOB_10),
	GPIO_GROUP(GPIOB_11),
	GPIO_GROUP(GPIOB_12),

	GPIO_GROUP(GPIOC_0),
	GPIO_GROUP(GPIOC_1),
	GPIO_GROUP(GPIOC_2),
	GPIO_GROUP(GPIOC_3),
	GPIO_GROUP(GPIOC_4),
	GPIO_GROUP(GPIOC_5),
	GPIO_GROUP(GPIOC_6),

	GPIO_GROUP(GPIOX_0),
	GPIO_GROUP(GPIOX_1),
	GPIO_GROUP(GPIOX_2),
	GPIO_GROUP(GPIOX_3),
	GPIO_GROUP(GPIOX_4),
	GPIO_GROUP(GPIOX_5),
	GPIO_GROUP(GPIOX_6),
	GPIO_GROUP(GPIOX_7),
	GPIO_GROUP(GPIOX_8),
	GPIO_GROUP(GPIOX_9),
	GPIO_GROUP(GPIOX_10),
	GPIO_GROUP(GPIOX_11),
	GPIO_GROUP(GPIOX_12),
	GPIO_GROUP(GPIOX_13),
	GPIO_GROUP(GPIOX_14),
	GPIO_GROUP(GPIOX_15),
	GPIO_GROUP(GPIOX_16),
	GPIO_GROUP(GPIOX_17),
	GPIO_GROUP(GPIOX_18),
	GPIO_GROUP(GPIOX_19),

	GPIO_GROUP(GPIOW_0),
	GPIO_GROUP(GPIOW_1),
	GPIO_GROUP(GPIOW_2),
	GPIO_GROUP(GPIOW_3),
	GPIO_GROUP(GPIOW_4),
	GPIO_GROUP(GPIOW_5),
	GPIO_GROUP(GPIOW_6),
	GPIO_GROUP(GPIOW_7),
	GPIO_GROUP(GPIOW_8),
	GPIO_GROUP(GPIOW_9),
	GPIO_GROUP(GPIOW_10),
	GPIO_GROUP(GPIOW_11),
	GPIO_GROUP(GPIOW_12),
	GPIO_GROUP(GPIOW_13),
	GPIO_GROUP(GPIOW_14),
	GPIO_GROUP(GPIOW_15),
	GPIO_GROUP(GPIOW_16),

	GPIO_GROUP(GPIOD_0),
	GPIO_GROUP(GPIOD_1),
	GPIO_GROUP(GPIOD_2),
	GPIO_GROUP(GPIOD_3),
	GPIO_GROUP(GPIOD_4),
	GPIO_GROUP(GPIOD_5),
	GPIO_GROUP(GPIOD_6),
	GPIO_GROUP(GPIOD_7),
	GPIO_GROUP(GPIOD_8),
	GPIO_GROUP(GPIOD_9),
	GPIO_GROUP(GPIOD_10),
	GPIO_GROUP(GPIOD_11),
	GPIO_GROUP(GPIOD_12),

	GPIO_GROUP(GPIOE_0),
	GPIO_GROUP(GPIOE_1),
	GPIO_GROUP(GPIOE_2),
	GPIO_GROUP(GPIOE_3),
	GPIO_GROUP(GPIOE_4),
	GPIO_GROUP(GPIOE_5),
	GPIO_GROUP(GPIOE_6),

	GPIO_GROUP(GPIOZ_0),
	GPIO_GROUP(GPIOZ_1),
	GPIO_GROUP(GPIOZ_2),
	GPIO_GROUP(GPIOZ_3),
	GPIO_GROUP(GPIOZ_4),
	GPIO_GROUP(GPIOZ_5),
	GPIO_GROUP(GPIOZ_6),
	GPIO_GROUP(GPIOZ_7),
	GPIO_GROUP(GPIOZ_8),
	GPIO_GROUP(GPIOZ_9),
	GPIO_GROUP(GPIOZ_10),
	GPIO_GROUP(GPIOZ_11),
	GPIO_GROUP(GPIOZ_12),
	GPIO_GROUP(GPIOZ_13),

	GPIO_GROUP(GPIOT_0),
	GPIO_GROUP(GPIOT_1),
	GPIO_GROUP(GPIOT_2),
	GPIO_GROUP(GPIOT_3),
	GPIO_GROUP(GPIOT_4),
	GPIO_GROUP(GPIOT_5),
	GPIO_GROUP(GPIOT_6),
	GPIO_GROUP(GPIOT_7),
	GPIO_GROUP(GPIOT_8),
	GPIO_GROUP(GPIOT_9),
	GPIO_GROUP(GPIOT_10),
	GPIO_GROUP(GPIOT_11),
	GPIO_GROUP(GPIOT_12),
	GPIO_GROUP(GPIOT_13),
	GPIO_GROUP(GPIOT_14),
	GPIO_GROUP(GPIOT_15),
	GPIO_GROUP(GPIOT_16),
	GPIO_GROUP(GPIOT_17),
	GPIO_GROUP(GPIOT_18),
	GPIO_GROUP(GPIOT_19),
	GPIO_GROUP(GPIOT_20),
	GPIO_GROUP(GPIOT_21),
	GPIO_GROUP(GPIOT_22),
	GPIO_GROUP(GPIOT_23),

	GPIO_GROUP(GPIOM_0),
	GPIO_GROUP(GPIOM_1),
	GPIO_GROUP(GPIOM_2),
	GPIO_GROUP(GPIOM_3),
	GPIO_GROUP(GPIOM_4),
	GPIO_GROUP(GPIOM_5),
	GPIO_GROUP(GPIOM_6),
	GPIO_GROUP(GPIOM_7),
	GPIO_GROUP(GPIOM_8),
	GPIO_GROUP(GPIOM_9),
	GPIO_GROUP(GPIOM_10),
	GPIO_GROUP(GPIOM_11),
	GPIO_GROUP(GPIOM_12),
	GPIO_GROUP(GPIOM_13),

	GPIO_GROUP(GPIOY_0),
	GPIO_GROUP(GPIOY_1),
	GPIO_GROUP(GPIOY_2),
	GPIO_GROUP(GPIOY_3),
	GPIO_GROUP(GPIOY_4),
	GPIO_GROUP(GPIOY_5),
	GPIO_GROUP(GPIOY_6),
	GPIO_GROUP(GPIOY_7),
	GPIO_GROUP(GPIOY_8),
	GPIO_GROUP(GPIOY_9),
	GPIO_GROUP(GPIOY_10),
	GPIO_GROUP(GPIOY_11),
	GPIO_GROUP(GPIOY_12),
	GPIO_GROUP(GPIOY_13),
	GPIO_GROUP(GPIOY_14),
	GPIO_GROUP(GPIOY_15),
	GPIO_GROUP(GPIOY_16),
	GPIO_GROUP(GPIOY_17),
	GPIO_GROUP(GPIOY_18),

	GPIO_GROUP(GPIOH_0),
	GPIO_GROUP(GPIOH_1),
	GPIO_GROUP(GPIOH_2),
	GPIO_GROUP(GPIOH_3),
	GPIO_GROUP(GPIOH_4),
	GPIO_GROUP(GPIOH_5),
	GPIO_GROUP(GPIOH_6),
	GPIO_GROUP(GPIOH_7),
	GPIO_GROUP(GPIO_TEST_N),

	/* Bank B func1 */
	GROUP(emmc_nand_d0,		1),
	GROUP(emmc_nand_d1,		1),
	GROUP(emmc_nand_d2,		1),
	GROUP(emmc_nand_d3,		1),
	GROUP(emmc_nand_d4,		1),
	GROUP(emmc_nand_d5,		1),
	GROUP(emmc_nand_d6,		1),
	GROUP(emmc_nand_d7,		1),
	GROUP(emmc_clk,			1),
	GROUP(emmc_cmd,			1),
	GROUP(emmc_nand_ds,		1),

	/* Bank B func1 */
	GROUP(nor_hold,			2),
	GROUP(nor_d,			2),
	GROUP(nor_q,			2),
	GROUP(nor_c,			2),
	GROUP(nor_wp,			2),
	GROUP(nor_cs,			2),

	/* Bank C func1 */
	GROUP(sdcard_d0,		1),
	GROUP(sdcard_d1,		1),
	GROUP(sdcard_d2,		1),
	GROUP(sdcard_d3,		1),
	GROUP(sdcard_clk,		1),
	GROUP(sdcard_cmd,		1),
	GROUP(gen_clk_out_c,		1),

	/* Bank C func2 */
	GROUP(jtag_b_tdo,		2),
	GROUP(jtag_b_tdi,		2),
	GROUP(uart_ao_a_rx_c,		2),
	GROUP(uart_ao_a_tx_c,		2),
	GROUP(jtag_b_clk,		2),
	GROUP(jtag_b_tms,		2),

	/* Bank C func3 */
	GROUP(spi1_mosi_c,		3),
	GROUP(spi1_miso_c,		3),
	GROUP(spi1_sclk_c,		3),
	GROUP(spi1_ss0_c,		3),

	/* Bank X func1 */
	GROUP(sdio_d0,			1),
	GROUP(sdio_d1,			1),
	GROUP(sdio_d2,			1),
	GROUP(sdio_d3,			1),
	GROUP(sdio_clk,			1),
	GROUP(sdio_cmd,			1),
	GROUP(pwm_b,			1),
	GROUP(pwm_c,			1),
	GROUP(tdm_d0,			1),
	GROUP(tdm_d1,			1),
	GROUP(tdm_fs0,			1),
	GROUP(tdm_sclk0,		1),
	GROUP(uart_c_tx,		1),
	GROUP(uart_c_rx,		1),
	GROUP(uart_c_cts,		1),
	GROUP(uart_c_rts,		1),
	GROUP(pwm_a,			1),
	GROUP(i2c2_sda_x,		1),
	GROUP(i2c2_sck_x,		1),
	GROUP(pwm_d,			1),

	/* Bank X func2 */
	GROUP(clk12_24_x,		2),

	/* Bank W func1 */
	GROUP(hdmirx_a_hpd,		1),
	GROUP(hdmirx_a_det,		1),
	GROUP(hdmirx_a_sda,		1),
	GROUP(hdmirx_a_sck,		1),
	GROUP(hdmirx_c_hpd,		1),
	GROUP(hdmirx_c_det,		1),
	GROUP(hdmirx_c_sda,		1),
	GROUP(hdmirx_c_sck,		1),
	GROUP(hdmirx_b_hpd,		1),
	GROUP(hdmirx_b_det,		1),
	GROUP(hdmirx_b_sda,		1),
	GROUP(hdmirx_b_sck,		1),
	GROUP(cec_a,			1),
	GROUP(hdmitx_sda_w13,		1),
	GROUP(hdmitx_sck_w14,		1),
	GROUP(hdmitx_hpd_in,		1),
	GROUP(cec_b,			1),

	/* Bank W func2 */
	GROUP(uart_ao_a_tx_w2,		2),
	GROUP(uart_ao_a_rx_w3,		2),
	GROUP(uart_ao_a_tx_w6,		2),
	GROUP(uart_ao_a_rx_w7,		2),
	GROUP(uart_ao_a_tx_w10,		2),
	GROUP(uart_ao_a_rx_w11,		2),

	/* Bank W func3 */
	GROUP(hdmitx_sda_w2,		3),
	GROUP(hdmitx_sck_w3,		3),

	/* Bank D func1 */
	GROUP(uart_ao_a_tx_d0,		1),
	GROUP(uart_ao_a_rx_d1,		1),
	GROUP(i2c0_ao_sck_d,		1),
	GROUP(i2c0_ao_sda_d,		1),
	GROUP(remote_out_d4,		1),
	GROUP(remote_in,		1),
	GROUP(jtag_a_clk,		1),
	GROUP(jtag_a_tms,		1),
	GROUP(jtag_a_tdi,		1),
	GROUP(jtag_a_tdo,		1),
	GROUP(gen_clk_out_d,		1),
	GROUP(pwm_ao_g_d11,		1),
	GROUP(wd_rsto,			1),

	/* Bank D func2 */
	GROUP(i2c0_slave_ao_sck,	2),
	GROUP(i2c0_slave_ao_sda,	2),
	GROUP(rtc_clk_in,		2),
	GROUP(pwm_ao_h_d5,		2),
	GROUP(pwm_ao_c_d,		2),
	GROUP(pwm_ao_g_d7,		2),
	GROUP(spdif_out_d,		2),
	GROUP(spdif_in_d,		2),
	GROUP(pwm_ao_h_d10,		2),

	/* Bank D func3 */
	GROUP(uart_ao_b_tx,		3),
	GROUP(uart_ao_b_rx,		3),
	GROUP(uart_ao_b_cts,		3),
	GROUP(pwm_ao_c_hiz,		3),
	GROUP(pwm_ao_g_hiz,		3),
	GROUP(uart_ao_b_rts,		3),

	/* Bank D func4 */
	GROUP(remote_out_d6,		4),

	/* Bank E func1 */
	GROUP(pwm_ao_a,			1),
	GROUP(pwm_ao_b,			1),
	GROUP(pwm_ao_c_e,		1),
	GROUP(pwm_ao_d,			1),
	GROUP(pwm_ao_e,			1),
	GROUP(pwm_ao_f,			1),
	GROUP(pwm_ao_g_e,		1),

	/* Bank E func2 */
	GROUP(i2c0_ao_sck_e,		2),
	GROUP(i2c0_ao_sda_e,		2),
	GROUP(clk25m,			2),
	GROUP(i2c1_ao_sck,		2),
	GROUP(i2c1_ao_sda,		2),
	GROUP(rtc_clk_out,		2),

	/* Bank E func3 */
	GROUP(clk12_24_e,		3),

	/* Bank Z func1 */
	GROUP(eth_mdio,			1),
	GROUP(eth_mdc,			1),
	GROUP(eth_rgmii_rx_clk,		1),
	GROUP(eth_rx_dv,		1),
	GROUP(eth_rxd0,			1),
	GROUP(eth_rxd1,			1),
	GROUP(eth_rxd2_rgmii,		1),
	GROUP(eth_rxd3_rgmii,		1),
	GROUP(eth_rgmii_tx_clk,		1),
	GROUP(eth_txen,			1),
	GROUP(eth_txd0,			1),
	GROUP(eth_txd1,			1),
	GROUP(eth_txd2_rgmii,		1),
	GROUP(eth_txd3_rgmii,		1),

	/* Bank Z func2 */
	GROUP(iso7816_clk_z,		2),
	GROUP(iso7816_data_z,		2),
	GROUP(tsin_b_valid,		2),
	GROUP(tsin_b_sop,		2),
	GROUP(tsin_b_din0,		2),
	GROUP(tsin_b_clk,		2),
	GROUP(tsin_b_fail,		2),
	GROUP(tsin_b_din1,		2),
	GROUP(tsin_b_din2,		2),
	GROUP(tsin_b_din3,		2),
	GROUP(tsin_b_din4,		2),
	GROUP(tsin_b_din5,		2),
	GROUP(tsin_b_din6,		2),
	GROUP(tsin_b_din7,		2),

	/* Bank Z func3 */
	GROUP(tsin_c_z_valid,		3),
	GROUP(tsin_c_z_sop,		3),
	GROUP(tsin_c_z_din0,		3),
	GROUP(tsin_c_z_clk,		3),
	GROUP(tsin_d_z_valid,		3),
	GROUP(tsin_d_z_sop,		3),
	GROUP(tsin_d_z_din0,		3),
	GROUP(tsin_d_z_clk,		3),

	/* Bank Z func4 */
	GROUP(spi4_mosi,		4),
	GROUP(spi4_miso,		4),
	GROUP(spi4_sclk,		4),
	GROUP(spi4_ss0,			4),
	GROUP(spi5_mosi,		4),
	GROUP(spi5_miso,		4),
	GROUP(spi5_sclk,		4),
	GROUP(spi5_ss0,			4),

	/* Bank T func1 */
	GROUP(mclk1,			1),
	GROUP(tdm_sclk1,		1),
	GROUP(tdm_fs1,			1),
	GROUP(tdm_d2,			1),
	GROUP(tdm_d3,			1),
	GROUP(tdm_d4,			1),
	GROUP(tdm_d5,			1),
	GROUP(tdm_d6,			1),
	GROUP(tdm_d7,			1),
	GROUP(tdm_d8,			1),
	GROUP(tdm_d9,			1),
	GROUP(tdm_d10,			1),
	GROUP(tdm_d11,			1),
	GROUP(mclk2,			1),
	GROUP(tdm_sclk2,		1),
	GROUP(tdm_fs2,			1),
	GROUP(i2c1_sck,			1),
	GROUP(i2c1_sda,			1),
	GROUP(spi0_mosi,		1),
	GROUP(spi0_miso,		1),
	GROUP(spi0_sclk,		1),
	GROUP(spi0_ss0,			1),
	GROUP(spi0_ss1,			1),
	GROUP(spi0_ss2,			1),

	/* Bank T func2 */
	GROUP(spdif_in_t,		2),
	GROUP(spdif_out_t,		2),
	GROUP(iso7816_clk_t,		2),
	GROUP(iso7816_data_t,		2),
	GROUP(tsin_a_sop_t,		2),
	GROUP(tsin_a_din0_t,		2),
	GROUP(tsin_a_clk_t,		2),
	GROUP(tsin_a_valid_t,		2),
	GROUP(i2c0_sck_t,		2),
	GROUP(i2c0_sda_t,		2),
	GROUP(i2c2_sck_t,		2),
	GROUP(i2c2_sda_t,		2),

	/* Bank T func3 */
	GROUP(spi3_mosi,		3),
	GROUP(spi3_miso,		3),
	GROUP(spi3_sclk,		3),
	GROUP(spi3_ss0,			3),

	/* Bank M func1 */
	GROUP(tdm_d12,			1),
	GROUP(tdm_d13,			1),
	GROUP(tdm_d14,			1),
	GROUP(tdm_d15,			1),
	GROUP(tdm_sclk3,		1),
	GROUP(tdm_fs3,			1),
	GROUP(i2c3_sda_m,		1),
	GROUP(i2c3_sck_m,		1),
	GROUP(spi1_mosi_m,		1),
	GROUP(spi1_miso_m,		1),
	GROUP(spi1_sclk_m,		1),
	GROUP(spi1_ss0_m,		1),
	GROUP(spi1_ss1_m,		1),
	GROUP(spi1_ss2_m,		1),

	/* Bank M func2 */
	GROUP(pdm_din1_m0,		2),
	GROUP(pdm_din2,			2),
	GROUP(pdm_din3,			2),
	GROUP(pdm_dclk,			2),
	GROUP(pdm_din0,			2),
	GROUP(pdm_din1_m5,		2),
	GROUP(uart_d_tx_m,		2),
	GROUP(uart_d_rx_m,		2),
	GROUP(uart_d_cts_m,		2),
	GROUP(uart_d_rts_m,		2),
	GROUP(i2c2_sda_m,		2),
	GROUP(i2c2_sck_m,		2),

	/* Bank Y func1 */
	GROUP(spi2_mosi,		1),
	GROUP(spi2_miso,		1),
	GROUP(spi2_sclk,		1),
	GROUP(spi2_ss0,			1),
	GROUP(spi2_ss1,			1),
	GROUP(spi2_ss2,			1),
	GROUP(uart_e_tx,		1),
	GROUP(uart_e_rx,		1),
	GROUP(uart_e_cts,		1),
	GROUP(uart_e_rts,		1),
	GROUP(uart_d_cts_y,		1),
	GROUP(uart_d_rts_y,		1),
	GROUP(uart_d_tx_y,		1),
	GROUP(uart_d_rx_y,		1),
	GROUP(i2c4_sck_y,		1),
	GROUP(i2c4_sda_y,		1),
	GROUP(i2c5_sck,			1),
	GROUP(i2c5_sda,			1),

	/* Bank Y func2 */
	GROUP(tsin_c_y_sop,		2),
	GROUP(tsin_c_y_din0,		2),
	GROUP(tsin_c_y_clk,		2),
	GROUP(tsin_c_y_valid,		2),
	GROUP(tsin_d_y_sop,		2),
	GROUP(tsin_d_y_din0,		2),
	GROUP(tsin_d_y_clk,		2),
	GROUP(tsin_d_y_valid,		2),
	GROUP(pcieck_reqn_y,		2),

	/* Bank Y func3 */
	GROUP(pwm_e,			3),
	GROUP(hsync,			3),
	GROUP(vsync,			3),
	GROUP(pwm_f,			3),
	GROUP(sync_3d_out,		3),
	GROUP(vx1_a_htpdn,		3),
	GROUP(vx1_b_htpdn,		3),
	GROUP(vx1_a_lockn,		3),
	GROUP(vx1_b_lockn,		3),
	GROUP(pwm_vs_y,			3),

	/* Bank Y func4 */
	GROUP(edp_a_hpd,		4),
	GROUP(edp_b_hpd,		4),

	/* Bank H func1 */
	GROUP(mic_mute_key,		1),
	GROUP(mic_mute_led,		1),
	GROUP(i2c3_sck_h,		1),
	GROUP(i2c3_sda_h,		1),
	GROUP(i2c4_sck_h,		1),
	GROUP(i2c4_sda_h,		1),
	GROUP(eth_link_led,		1),
	GROUP(eth_act_led,		1),

	/* Bank H func2 */
	GROUP(pwm_vs_h,			2),
	GROUP(uart_f_tx,		2),
	GROUP(uart_f_rx,		2),
	GROUP(uart_f_cts,		2),
	GROUP(uart_f_rts,		2),
	GROUP(i2c0_sda_h,		2),
	GROUP(i2c0_sck_h,		2),

	/* Bank H func3 */
	GROUP(pcieck_reqn_h,		3),
};

static const char * const gpio_periphs_groups[] = {
	"GPIOB_0", "GPIOB_1", "GPIOB_2", "GPIOB_3", "GPIOB_4", "GPIOB_5",
	"GPIOB_6", "GPIOB_7", "GPIOB_8", "GPIOB_9", "GPIOB_10",
	"GPIOB_11", "GPIOB_12",

	"GPIOC_0", "GPIOC_1", "GPIOC_2", "GPIOC_3", "GPIOC_4", "GPIOC_5",
	"GPIOC_6",

	"GPIOX_0", "GPIOX_1", "GPIOX_2", "GPIOX_3", "GPIOX_4", "GPIOX_5",
	"GPIOX_6", "GPIOX_7", "GPIOX_8", "GPIOX_9", "GPIOX_10", "GPIOX_11",
	"GPIOX_12", "GPIOX_13", "GPIOX_14", "GPIOX_15", "GPIOX_16", "GPIOX_17",
	"GPIOX_18", "GPIOX_19",

	"GPIOW_0", "GPIOW_1", "GPIOW_2", "GPIOW_3", "GPIOW_4", "GPIOW_5",
	"GPIOW_6", "GPIOW_7", "GPIOW_8", "GPIOW_9", "GPIOW_10", "GPIOW_11",
	"GPIOW_12", "GPIOW_13", "GPIOW_14", "GPIOW_15", "GPIOW_16",

	"GPIOD_0", "GPIOD_1", "GPIOD_2", "GPIOD_3", "GPIOD_4", "GPIOD_5",
	"GPIOD_6", "GPIOD_7", "GPIOD_8", "GPIOD_9", "GPIOD_10", "GPIOD_11",
	"GPIOD_12",

	"GPIOE_0", "GPIOE_1", "GPIOE_2", "GPIOE_3", "GPIOE_4", "GPIOE_5",
	"GPIOE_6",

	"GPIOZ_0", "GPIOZ_1", "GPIOZ_2", "GPIOZ_3", "GPIOZ_4", "GPIOZ_5",
	"GPIOZ_6", "GPIOZ_7", "GPIOZ_8", "GPIOZ_9", "GPIOZ_10", "GPIOZ_11",
	"GPIOZ_12", "GPIOZ_13",

	"GPIOT_0", "GPIOT_1", "GPIOT_2", "GPIOT_3", "GPIOT_4", "GPIOT_5",
	"GPIOT_6", "GPIOT_7", "GPIOT_8", "GPIOT_9", "GPIOT_10", "GPIOT_11",
	"GPIOT_12", "GPIOT_13", "GPIOT_14", "GPIOT_15", "GPIOT_16",
	"GPIOT_17", "GPIOT_18", "GPIOT_19", "GPIOT_20", "GPIOT_21",
	"GPIOT_22", "GPIOT_23",

	"GPIOM_0", "GPIOM_1", "GPIOM_2", "GPIOM_3", "GPIOM_4", "GPIOM_5",
	"GPIOM_6", "GPIOM_7", "GPIOM_8", "GPIOM_9", "GPIOM_10", "GPIOM_11",
	"GPIOM_12", "GPIOM_13",

	"GPIOY_0", "GPIOY_1", "GPIOY_2", "GPIOY_3", "GPIOY_4", "GPIOY_5",
	"GPIOY_6", "GPIOY_7", "GPIOY_8", "GPIOY_9", "GPIOY_10", "GPIOY_11",
	"GPIOY_12", "GPIOY_13", "GPIOY_14", "GPIOY_15", "GPIOY_16",
	"GPIOY_17", "GPIOY_18",

	"GPIOH_0", "GPIOH_1", "GPIOH_2", "GPIOH_3", "GPIOH_4", "GPIOH_5",
	"GPIOH_6", "GPIOH_7",

	"GPIO_TEST_N",
};

static const char * const emmc_groups[] = {
	"emmc_nand_d0", "emmc_nand_d1", "emmc_nand_d2", "emmc_nand_d3",
	"emmc_nand_d4", "emmc_nand_d5", "emmc_nand_d6", "emmc_nand_d7",
	"emmc_clk", "emmc_cmd", "emmc_nand_ds",
};

static const char * const nor_groups[] = {
	"nor_hold", "nor_d", "nor_q", "nor_c", "nor_wp", "nor_cs",
};

static const char * const sdcard_groups[] = {
	"sdcard_d0", "sdcard_d1", "sdcard_d2", "sdcard_d3", "sdcard_clk",
	"sdcard_cmd",
};

static const char * const sdio_groups[] = {
	"sdio_d0", "sdio_d1", "sdio_d2", "sdio_d3", "sdio_clk", "sdio_cmd",
};

static const char * const gen_clk_groups[] = {
	"gen_clk_out_c", "gen_clk_out_d",
};

static const char * const jtag_a_groups[] = {
	"jtag_a_clk", "jtag_a_tms", "jtag_a_tdi", "jtag_a_tdo",
};

static const char * const jtag_b_groups[] = {
	"jtag_b_tdo", "jtag_b_tdi", "jtag_b_clk", "jtag_b_tms",
};

static const char * const uart_c_groups[] = {
	"uart_c_tx", "uart_c_rx", "uart_c_cts", "uart_c_rts",
};

static const char * const uart_d_groups[] = {
	"uart_d_tx_m", "uart_d_rx_m", "uart_d_cts_m", "uart_d_rts_m",
	"uart_d_rts_y", "uart_d_tx_y", "uart_d_rx_y", "uart_d_cts_y",
};

static const char * const uart_e_groups[] = {
	"uart_e_tx", "uart_e_rx", "uart_e_cts", "uart_e_rts",
};

static const char * const uart_f_groups[] = {
	"uart_f_tx", "uart_f_rx", "uart_f_cts", "uart_f_rts",
};

static const char * const uart_ao_a_groups[] = {
	"uart_ao_a_rx_c", "uart_ao_a_tx_c", "uart_ao_a_tx_w2",
	"uart_ao_a_rx_w3", "uart_ao_a_tx_w6", "uart_ao_a_rx_w7",
	"uart_ao_a_tx_w10", "uart_ao_a_rx_w11", "uart_ao_a_tx_d0",
	"uart_ao_a_rx_d1",
};

static const char * const uart_ao_b_groups[] = {
	"uart_ao_b_tx", "uart_ao_b_rx", "uart_ao_b_cts", "uart_ao_b_rts",
};

static const char * const spi0_groups[] = {
	"spi0_mosi", "spi0_miso", "spi0_sclk", "spi0_ss0", "spi0_ss1",
	"spi0_ss2",
};

static const char * const spi1_groups[] = {
	"spi1_mosi_c", "spi1_miso_c", "spi1_sclk_c", "spi1_ss0_c",
	"spi1_mosi_m", "spi1_miso_m", "spi1_sclk_m", "spi1_ss0_m",
	"spi1_ss1_m", "spi1_ss2_m",
};

static const char * const spi2_groups[] = {
	"spi2_mosi", "spi2_miso", "spi2_sclk", "spi2_ss0", "spi2_ss1",
	"spi2_ss2",
};

static const char * const spi3_groups[] = {
	"spi3_mosi", "spi3_miso", "spi3_sclk", "spi3_ss0",
};

static const char * const spi4_groups[] = {
	"spi4_mosi", "spi4_miso", "spi4_sclk", "spi4_ss0",
};

static const char * const spi5_groups[] = {
	"spi5_mosi", "spi5_miso", "spi5_sclk", "spi5_ss0",
};

static const char * const pwm_a_groups[] = {
	"pwm_a",
};

static const char * const pwm_b_groups[] = {
	"pwm_b",
};

static const char * const pwm_c_groups[] = {
	"pwm_c",
};

static const char * const pwm_d_groups[] = {
	"pwm_d",
};

static const char * const pwm_e_groups[] = {
	"pwm_e",
};

static const char * const pwm_f_groups[] = {
	"pwm_f",
};

static const char * const pwm_ao_c_hiz_groups[] = {
	"pwm_ao_c_hiz",
};

static const char * const pwm_ao_g_hiz_groups[] = {
	"pwm_ao_g_hiz",
};

static const char * const pwm_ao_a_groups[] = {
	"pwm_ao_a",
};

static const char * const pwm_ao_b_groups[] = {
	"pwm_ao_b",
};

static const char * const pwm_ao_c_groups[] = {
	"pwm_ao_c_d", "pwm_ao_c_e",
};

static const char * const pwm_ao_d_groups[] = {
	"pwm_ao_d",
};

static const char * const pwm_ao_e_groups[] = {
	"pwm_ao_e",
};

static const char * const pwm_ao_f_groups[] = {
	"pwm_ao_f",
};

static const char * const pwm_ao_h_groups[] = {
	"pwm_ao_h_d5", "pwm_ao_h_d10",
};

static const char * const pwm_ao_g_groups[] = {
	"pwm_ao_g_d11", "pwm_ao_g_d7", "pwm_ao_g_e",
};

static const char * const pwm_vs_groups[] = {
	"pwm_vs_y", "pwm_vs_h",
};

static const char * const tdm_groups[] = {
	"tdm_d0", "tdm_d1", "tdm_fs0", "tdm_sclk0", "tdm_sclk1", "tdm_fs1",
	"tdm_d2", "tdm_d3", "tdm_d4", "tdm_d5", "tdm_d6", "tdm_d7",
	"tdm_d8", "tdm_d9", "tdm_d10", "tdm_d11", "tdm_sclk2", "tdm_fs2",
	"tdm_d12", "tdm_d13", "tdm_d14", "tdm_d15", "tdm_sclk3", "tdm_fs3",
};

static const char * const i2c0_slave_ao_groups[] = {
	"i2c0_slave_ao_sck", "i2c0_slave_ao_sda",
};

static const char * const i2c0_ao_groups[] = {
	"i2c0_ao_sck_d", "i2c0_ao_sda_d",
	"i2c0_ao_sck_e", "i2c0_ao_sda_e",
};

static const char * const i2c1_ao_groups[] = {
	"i2c1_ao_sck", "i2c1_ao_sda",
};

static const char * const i2c0_groups[] = {
	"i2c0_sck_t", "i2c0_sda_t", "i2c0_sck_h", "i2c0_sda_h",
};

static const char * const i2c1_groups[] = {
	"i2c1_sck", "i2c1_sda",
};

static const char * const i2c2_groups[] = {
	"i2c2_sda_x", "i2c2_sck_x",
	"i2c2_sda_t", "i2c2_sck_t",
	"i2c2_sda_m", "i2c2_sck_m",
};

static const char * const i2c3_groups[] = {
	"i2c3_sda_m", "i2c3_sck_m", "i2c3_sck_h", "i2c3_sda_h",
};

static const char * const i2c4_groups[] = {
	"i2c4_sck_y", "i2c4_sda_y", "i2c4_sck_h", "i2c4_sda_h",
};

static const char * const i2c5_groups[] = {
	"i2c5_sck", "i2c5_sda",
};

static const char * const clk12_24_groups[] = {
	"clk12_24_x", "clk12_24_e",
};

static const char * const hdmirx_a_groups[] = {
	"hdmirx_a_hpd", "hdmirx_a_det", "hdmirx_a_sda", "hdmirx_a_sck",
};

static const char * const hdmirx_b_groups[] = {
	"hdmirx_b_hpd", "hdmirx_b_det", "hdmirx_b_sda", "hdmirx_b_sck",
};

static const char * const hdmirx_c_groups[] = {
	"hdmirx_c_hpd", "hdmirx_c_det", "hdmirx_c_sda", "hdmirx_c_sck",
};

static const char * const cec_a_groups[] = {
	"cec_a",
};

static const char * const cec_b_groups[] = {
	"cec_b",
};

static const char * const hdmitx_groups[] = {
	"hdmitx_sda_w13", "hdmitx_sck_w14", "hdmitx_hpd_in",
	"hdmitx_sda_w2", "hdmitx_sck_w3",
};

static const char * const remote_out_groups[] = {
	"remote_out_d4", "remote_out_d6",
};

static const char * const remote_in_groups[] = {
	"remote_in",
};

static const char * const wd_rsto_groups[] = {
	"wd_rsto",
};

static const char * const rtc_clk_groups[] = {
	"rtc_clk_in", "rtc_clk_out",
};

static const char * const spdif_out_groups[] = {
	"spdif_out_d", "spdif_out_t",
};

static const char * const spdif_in_groups[] = {
	"spdif_in_d", "spdif_in_t",
};

static const char * const clk25m_groups[] = {
	"clk25m",
};

static const char * const eth_groups[] = {
	"eth_mdio", "eth_mdc", "eth_rgmii_rx_clk", "eth_rx_dv", "eth_rxd0",
	"eth_rxd1", "eth_rxd2_rgmii", "eth_rxd3_rgmii", "eth_rgmii_tx_clk",
	"eth_txen", "eth_txd0", "eth_txd1", "eth_txd2_rgmii",
	"eth_txd3_rgmii", "eth_link_led", "eth_act_led",
};

static const char * const iso7816_groups[] = {
	"iso7816_clk_z", "iso7816_data_z",
	"iso7816_clk_t", "iso7816_data_t",
};

static const char * const tsin_a_groups[] = {
	"tsin_a_sop_t", "tsin_a_din0_t", "tsin_a_clk_t", "tsin_a_valid_t",
};

static const char * const tsin_b_groups[] = {
	"tsin_b_valid", "tsin_b_sop", "tsin_b_din0", "tsin_b_clk",
	"tsin_b_fail", "tsin_b_din1", "tsin_b_din2", "tsin_b_din3",
	"tsin_b_din4", "tsin_b_din5", "tsin_b_din6", "tsin_b_din7",
};

static const char * const tsin_c_groups[] = {
	"tsin_c_z_valid", "tsin_c_z_sop", "tsin_c_z_din0", "tsin_c_z_clk",
	"tsin_c_y_sop", "tsin_c_y_din0", "tsin_c_y_clk", "tsin_c_y_valid",
};

static const char * const tsin_d_groups[] = {
	"tsin_d_z_valid", "tsin_d_z_sop", "tsin_d_z_din0", "tsin_d_z_clk",
	"tsin_d_y_sop", "tsin_d_y_din0", "tsin_d_y_clk", "tsin_d_y_valid",
};

static const char * const mclk_groups[] = {
	"mclk1", "mclk2",
};

static const char * const pdm_groups[] = {
	"pdm_din1_m0", "pdm_din2", "pdm_din3", "pdm_dclk", "pdm_din0",
	"pdm_din1_m5",
};

static const char * const pcieck_groups[] = {
	"pcieck_reqn_y", "pcieck_reqn_h",
};

static const char * const hsync_groups[] = {
	"hsync",
};

static const char * const vsync_groups[] = {
	"vsync",
};

static const char * const sync_3d_groups[] = {
	"sync_3d_out",
};

static const char * const vx1_a_groups[] = {
	"vx1_a_htpdn", "vx1_a_lockn",
};

static const char * const vx1_b_groups[] = {
	"vx1_b_htpdn", "vx1_b_lockn",
};

static const char * const edp_a_groups[] = {
	"edp_a_hpd",
};

static const char * const edp_b_groups[] = {
	"edp_b_hpd",
};

static const char * const mic_mute_groups[] = {
	"mic_mute_key", "mic_mute_led",
};

static const struct meson_pmx_func t7_periphs_functions[] = {
	FUNCTION(gpio_periphs),
	FUNCTION(emmc),
	FUNCTION(nor),
	FUNCTION(sdcard),
	FUNCTION(sdio),
	FUNCTION(gen_clk),
	FUNCTION(jtag_a),
	FUNCTION(jtag_b),
	FUNCTION(uart_c),
	FUNCTION(uart_d),
	FUNCTION(uart_e),
	FUNCTION(uart_f),
	FUNCTION(uart_ao_a),
	FUNCTION(uart_ao_b),
	FUNCTION(spi0),
	FUNCTION(spi1),
	FUNCTION(spi2),
	FUNCTION(spi3),
	FUNCTION(spi4),
	FUNCTION(spi5),
	FUNCTION(pwm_a),
	FUNCTION(pwm_b),
	FUNCTION(pwm_c),
	FUNCTION(pwm_d),
	FUNCTION(pwm_e),
	FUNCTION(pwm_f),
	FUNCTION(pwm_ao_c_hiz),
	FUNCTION(pwm_ao_g_hiz),
	FUNCTION(pwm_ao_a),
	FUNCTION(pwm_ao_b),
	FUNCTION(pwm_ao_c),
	FUNCTION(pwm_ao_d),
	FUNCTION(pwm_ao_e),
	FUNCTION(pwm_ao_f),
	FUNCTION(pwm_ao_h),
	FUNCTION(pwm_ao_g),
	FUNCTION(pwm_vs),
	FUNCTION(tdm),
	FUNCTION(i2c0_slave_ao),
	FUNCTION(i2c0_ao),
	FUNCTION(i2c1_ao),
	FUNCTION(i2c0),
	FUNCTION(i2c1),
	FUNCTION(i2c2),
	FUNCTION(i2c3),
	FUNCTION(i2c4),
	FUNCTION(i2c5),
	FUNCTION(clk12_24),
	FUNCTION(hdmirx_a),
	FUNCTION(hdmirx_b),
	FUNCTION(hdmirx_c),
	FUNCTION(cec_a),
	FUNCTION(cec_b),
	FUNCTION(hdmitx),
	FUNCTION(remote_out),
	FUNCTION(remote_in),
	FUNCTION(wd_rsto),
	FUNCTION(rtc_clk),
	FUNCTION(spdif_out),
	FUNCTION(spdif_in),
	FUNCTION(clk25m),
	FUNCTION(eth),
	FUNCTION(iso7816),
	FUNCTION(tsin_a),
	FUNCTION(tsin_b),
	FUNCTION(tsin_c),
	FUNCTION(tsin_d),
	FUNCTION(mclk),
	FUNCTION(pdm),
	FUNCTION(pcieck),
	FUNCTION(hsync),
	FUNCTION(vsync),
	FUNCTION(sync_3d),
	FUNCTION(vx1_a),
	FUNCTION(vx1_b),
	FUNCTION(edp_a),
	FUNCTION(edp_b),
	FUNCTION(mic_mute),
};

static const struct meson_bank t7_periphs_banks[] = {
	/* name  first  last  irq pullen  pull  dir  out  in  ds */
	BANK_DS("D",      GPIOD_0,     GPIOD_12, 57, 69,
		0x03, 0,  0x04,  0,  0x02,  0, 0x01, 0,  0x00, 0, 0x07, 0),
	BANK_DS("E",      GPIOE_0,     GPIOE_6,  70, 76,
		0x0b, 0,  0x0c,  0,  0x0a,  0, 0x09, 0,  0x08, 0, 0x0f, 0),
	BANK_DS("Z",      GPIOZ_0,     GPIOZ_13, 77, 90,
		0x13, 0,  0x14,  0,  0x12, 0,  0x11, 0,  0x10, 0, 0x17, 0),
	BANK_DS("H",      GPIOH_0,     GPIOH_7,  148, 155,
		0x1b, 0,  0x1c,  0,  0x1a, 0,  0x19, 0,  0x18, 0, 0x1f, 0),
	BANK_DS("C",      GPIOC_0,     GPIOC_6,  13, 19,
		0x23, 0,  0x24,  0,  0x22, 0,  0x21, 0,  0x20, 0, 0x27, 0),
	BANK_DS("B",      GPIOB_0,     GPIOB_12, 0, 12,
		0x2b, 0,  0x2c,  0,  0x2a, 0,  0x29, 0,  0x28, 0, 0x2f, 0),
	BANK_DS("X",      GPIOX_0,     GPIOX_19, 20, 39,
		0x33, 0,  0x34,  0,  0x32, 0,  0x31, 0,  0x30, 0, 0x37, 0),
	BANK_DS("T",      GPIOT_0,     GPIOT_23, 91, 114,
		0x43, 0,  0x44,  0,  0x42, 0,  0x41, 0,  0x40, 0, 0x47, 0),
	BANK_DS("Y",      GPIOY_0,     GPIOY_18, 129, 147,
		0x53, 0,  0x54,  0,  0x52, 0,  0x51, 0,  0x50, 0, 0x57, 0),
	BANK_DS("W",      GPIOW_0,     GPIOW_16, 40, 56,
		0x63, 0,  0x64,  0,  0x62, 0,  0x61, 0,  0x60, 0, 0x67, 0),
	BANK_DS("M",      GPIOM_0,     GPIOM_13, 115, 128,
		0x73, 0,  0x74,  0,  0x72, 0,  0x71, 0,  0x70, 0, 0x77, 0),
	BANK_DS("TEST_N", GPIO_TEST_N, GPIO_TEST_N, 156, 156,
		0x83, 0,  0x84,  0,  0x82, 0,  0x81,  0, 0x80, 0, 0x87, 0),
};

static const struct meson_pmx_bank t7_periphs_pmx_banks[] = {
	/*      name	    first	 last        reg  offset */
	BANK_PMX("D",      GPIOD_0,     GPIOD_12,    0x0a,  0),
	BANK_PMX("E",      GPIOE_0,     GPIOE_6,     0x0c,  0),
	BANK_PMX("Z",      GPIOZ_0,     GPIOZ_13,    0x05,  0),
	BANK_PMX("H",      GPIOH_0,     GPIOH_7,     0x08,  0),
	BANK_PMX("C",      GPIOC_0,     GPIOC_6,     0x07,  0),
	BANK_PMX("B",      GPIOB_0,     GPIOB_12,    0x00,  0),
	BANK_PMX("X",      GPIOX_0,     GPIOX_19,    0x02,  0),
	BANK_PMX("T",      GPIOT_0,     GPIOT_23,    0x0f,  0),
	BANK_PMX("Y",      GPIOY_0,     GPIOY_18,    0x13,  0),
	BANK_PMX("W",      GPIOW_0,     GPIOW_16,    0x16,  0),
	BANK_PMX("M",      GPIOM_0,     GPIOM_13,    0x0d,  0),
	BANK_PMX("TEST_N", GPIO_TEST_N, GPIO_TEST_N, 0x09,  0),
};

static const struct meson_axg_pmx_data t7_periphs_pmx_banks_data = {
	.pmx_banks	= t7_periphs_pmx_banks,
	.num_pmx_banks	= ARRAY_SIZE(t7_periphs_pmx_banks),
};

static const struct meson_pinctrl_data t7_periphs_pinctrl_data = {
	.name		= "periphs-banks",
	.pins		= t7_periphs_pins,
	.groups		= t7_periphs_groups,
	.funcs		= t7_periphs_functions,
	.banks		= t7_periphs_banks,
	.num_pins	= ARRAY_SIZE(t7_periphs_pins),
	.num_groups	= ARRAY_SIZE(t7_periphs_groups),
	.num_funcs	= ARRAY_SIZE(t7_periphs_functions),
	.num_banks	= ARRAY_SIZE(t7_periphs_banks),
	.pmx_ops	= &meson_axg_pmx_ops,
	.pmx_data	= &t7_periphs_pmx_banks_data,
	.parse_dt	= &meson_a1_parse_dt_extra,
};

static const struct of_device_id t7_pinctrl_dt_match[] = {
	{
		.compatible = "amlogic,t7-periphs-pinctrl",
		.data = &t7_periphs_pinctrl_data,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, t7_pinctrl_dt_match);

static struct platform_driver t7_pinctrl_driver = {
	.probe  = meson_pinctrl_probe,
	.driver = {
		.name = "amlogic-t7-pinctrl",
		.of_match_table = t7_pinctrl_dt_match,
	},
};
module_platform_driver(t7_pinctrl_driver);

MODULE_AUTHOR("Huqiang Qin <huqiang.qin@amlogic.com>");
MODULE_DESCRIPTION("Pin controller and GPIO driver for Amlogic T7 SoC");
MODULE_LICENSE("Dual BSD/GPL");
