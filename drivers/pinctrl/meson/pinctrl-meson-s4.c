// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Pin controller and GPIO driver for Amlogic Meson S4 SoC.
 *
 * Copyright (c) 2021 Amlogic, Inc. All rights reserved.
 * Author: Qianggui Song <qianggui.song@amlogic.com>
 */

#include <dt-bindings/gpio/meson-s4-gpio.h>
#include "pinctrl-meson.h"
#include "pinctrl-meson-axg-pmx.h"

static const struct pinctrl_pin_desc meson_s4_periphs_pins[] = {
	MESON_PIN(GPIOE_0),
	MESON_PIN(GPIOE_1),

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
	MESON_PIN(GPIOB_13),

	MESON_PIN(GPIOC_0),
	MESON_PIN(GPIOC_1),
	MESON_PIN(GPIOC_2),
	MESON_PIN(GPIOC_3),
	MESON_PIN(GPIOC_4),
	MESON_PIN(GPIOC_5),
	MESON_PIN(GPIOC_6),
	MESON_PIN(GPIOC_7),

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

	MESON_PIN(GPIOH_0),
	MESON_PIN(GPIOH_1),
	MESON_PIN(GPIOH_2),
	MESON_PIN(GPIOH_3),
	MESON_PIN(GPIOH_4),
	MESON_PIN(GPIOH_5),
	MESON_PIN(GPIOH_6),
	MESON_PIN(GPIOH_7),
	MESON_PIN(GPIOH_8),
	MESON_PIN(GPIOH_9),
	MESON_PIN(GPIOH_10),
	MESON_PIN(GPIOH_11),

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

	MESON_PIN(GPIO_TEST_N),
};

/* BANK E func1 */
static const unsigned int i2c0_sda_pins[]		= { GPIOE_0 };
static const unsigned int i2c0_scl_pins[]		= { GPIOE_1 };

/* BANK E func2 */
static const unsigned int uart_b_tx_e_pins[]		= { GPIOE_0 };
static const unsigned int uart_b_rx_e_pins[]		= { GPIOE_1 };

/* BANK E func3 */
static const unsigned int pwm_h_pins[]			= { GPIOE_0 };
static const unsigned int pwm_j_pins[]			= { GPIOE_1 };

/* BANK B func1 */
static const unsigned int emmc_nand_d0_pins[]		= { GPIOB_0 };
static const unsigned int emmc_nand_d1_pins[]		= { GPIOB_1 };
static const unsigned int emmc_nand_d2_pins[]		= { GPIOB_2 };
static const unsigned int emmc_nand_d3_pins[]		= { GPIOB_3 };
static const unsigned int emmc_nand_d4_pins[]		= { GPIOB_4 };
static const unsigned int emmc_nand_d5_pins[]		= { GPIOB_5 };
static const unsigned int emmc_nand_d6_pins[]		= { GPIOB_6 };
static const unsigned int emmc_nand_d7_pins[]		= { GPIOB_7 };
static const unsigned int emmc_clk_pins[]		= { GPIOB_8 };
static const unsigned int emmc_rst_pins[]		= { GPIOB_9 };
static const unsigned int emmc_cmd_pins[]		= { GPIOB_10 };
static const unsigned int emmc_nand_ds_pins[]		= { GPIOB_11 };

/* Bank B func2 */
static const unsigned int nand_wen_clk_pins[]		= { GPIOB_8 };
static const unsigned int nand_ale_pins[]		= { GPIOB_9 };
static const unsigned int nand_ren_wr_pins[]		= { GPIOB_10 };
static const unsigned int nand_cle_pins[]		= { GPIOB_11 };
static const unsigned int nand_ce0_pins[]		= { GPIOB_12 };

/* Bank B func3 */
static const unsigned int spif_hold_pins[]		= { GPIOB_3 };
static const unsigned int spif_mo_pins[]		= { GPIOB_4 };
static const unsigned int spif_mi_pins[]		= { GPIOB_5 };
static const unsigned int spif_clk_pins[]		= { GPIOB_6 };
static const unsigned int spif_wp_pins[]		= { GPIOB_7 };
static const unsigned int spif_cs_pins[]		= { GPIOB_13 };

/* Bank C func1 */
static const unsigned int sdcard_d0_c_pins[]		= { GPIOC_0 };
static const unsigned int sdcard_d1_c_pins[]		= { GPIOC_1 };
static const unsigned int sdcard_d2_c_pins[]		= { GPIOC_2 };
static const unsigned int sdcard_d3_c_pins[]		= { GPIOC_3 };
static const unsigned int sdcard_clk_c_pins[]		= { GPIOC_4 };
static const unsigned int sdcard_cmd_c_pins[]		= { GPIOC_5 };
static const unsigned int sdcard_cd_pins[]		= { GPIOC_6 };

/* Bank C func2 */
static const unsigned int jtag_2_tdo_pins[]		= { GPIOC_0 };
static const unsigned int jtag_2_tdi_pins[]		= { GPIOC_1 };
static const unsigned int uart_b_rx_c_pins[]		= { GPIOC_2 };
static const unsigned int uart_b_tx_c_pins[]		= { GPIOC_3 };
static const unsigned int jtag_2_clk_pins[]		= { GPIOC_4 };
static const unsigned int jtag_2_tms_pins[]		= { GPIOC_5 };
static const unsigned int i2c1_sda_c_pins[]		= { GPIOC_6 };
static const unsigned int i2c1_scl_c_pins[]		= { GPIOC_7 };

/* Bank C func3 */
static const unsigned int pdm_din1_c_pins[]		= { GPIOC_0 };
static const unsigned int pdm_din0_c_pins[]		= { GPIOC_1 };
static const unsigned int i2c4_sda_c_pins[]		= { GPIOC_2 };
static const unsigned int i2c4_scl_c_pins[]		= { GPIOC_3 };
static const unsigned int pdm_dclk_c_pins[]		= { GPIOC_4 };
static const unsigned int iso7816_clk_c_pins[]		= { GPIOC_5 };
static const unsigned int iso7816_data_c_pins[]		= { GPIOC_6 };

/* Bank C func4 */
static const unsigned int tdm_d2_c_pins[]		= { GPIOC_0 };
static const unsigned int tdm_d3_c_pins[]		= { GPIOC_1 };
static const unsigned int tdm_fs1_c_pins[]		= { GPIOC_2 };
static const unsigned int tdm_sclk1_c_pins[]		= { GPIOC_3 };
static const unsigned int mclk_1_c_pins[]		= { GPIOC_4 };
static const unsigned int tdm_d4_c_pins[]		= { GPIOC_5 };
static const unsigned int tdm_d5_c_pins[]		= { GPIOC_6 };

/* Bank D func1 */
static const unsigned int uart_b_tx_d_pins[]		= { GPIOD_0 };
static const unsigned int uart_b_rx_d_pins[]		= { GPIOD_1 };
static const unsigned int uart_b_cts_d_pins[]		= { GPIOD_2 };
static const unsigned int uart_b_rts_d_pins[]		= { GPIOD_3 };
static const unsigned int remote_out_pins[]		= { GPIOD_4 };
static const unsigned int remote_in_pins[]		= { GPIOD_5 };
static const unsigned int jtag_1_clk_pins[]		= { GPIOD_6 };
static const unsigned int jtag_1_tms_pins[]		= { GPIOD_7 };
static const unsigned int jtag_1_tdi_pins[]		= { GPIOD_8 };
static const unsigned int jtag_1_tdo_pins[]		= { GPIOD_9 };
static const unsigned int clk12_24_pins[]		= { GPIOD_10 };
static const unsigned int pwm_g_hiz_pins[]		= { GPIOD_11 };

/* Bank D func2 */
static const unsigned int i2c4_sda_d_pins[]		= { GPIOD_2 };
static const unsigned int i2c4_scl_d_pins[]		= { GPIOD_3 };
static const unsigned int mclk_1_d_pins[]		= { GPIOD_4 };
static const unsigned int tdm_sclk1_d_pins[]		= { GPIOD_6 };
static const unsigned int tdm_fs1_d_pins[]		= { GPIOD_7 };
static const unsigned int tdm_d4_d_pins[]		= { GPIOD_8 };
static const unsigned int tdm_d3_d_pins[]		= { GPIOD_9 };
static const unsigned int tdm_d2_d_pins[]		= { GPIOD_10 };
static const unsigned int pwm_g_d_pins[]		= { GPIOD_11 };

/* Bank D func3 */
static const unsigned int uart_c_tx_pins[]		= { GPIOD_2 };
static const unsigned int uart_c_rx_pins[]		= { GPIOD_3 };
static const unsigned int pwm_b_d_pins[]		= { GPIOD_4 };
static const unsigned int pwm_a_d_pins[]		= { GPIOD_6 };
static const unsigned int pwm_c_d_pins[]		= { GPIOD_7 };
static const unsigned int pwm_d_d_pins[]		= { GPIOD_8 };
static const unsigned int pwm_i_d_pins[]		= { GPIOD_9 };

/* Bank D func4 */
static const unsigned int clk_32k_in_pins[]		= { GPIOD_2 };
static const unsigned int pwm_b_hiz_pins[]		= { GPIOD_4 };
static const unsigned int pwm_a_hiz_pins[]		= { GPIOD_6 };
static const unsigned int pwm_c_hiz_pins[]		= { GPIOD_7 };
static const unsigned int pdm_dclk_d_pins[]		= { GPIOD_8 };
static const unsigned int pdm_din0_d_pins[]		= { GPIOD_9 };
static const unsigned int pdm_din1_d_pins[]		= { GPIOD_10 };

/* Bank D func5 */
static const unsigned int mic_mute_en_pins[]		= { GPIOD_2 };
static const unsigned int mic_mute_key_pins[]		= { GPIOD_3 };
static const unsigned int i2c1_sda_d_pins[]		= { GPIOD_6 };
static const unsigned int i2c1_scl_d_pins[]		= { GPIOD_7 };
static const unsigned int i2c2_sda_d_pins[]		= { GPIOD_10 };
static const unsigned int i2c2_scl_d_pins[]		= { GPIOD_11 };

/* Bank D func6 */
static const unsigned int gen_clk_d_pins[]		= { GPIOD_10 };
static const unsigned int tsin_b_clk_c_pins[]		= { GPIOD_6 };
static const unsigned int tsin_b_sop_c_pins[]		= { GPIOD_7 };
static const unsigned int tsin_b_valid_c_pins[]		= { GPIOD_8 };
static const unsigned int tsin_b_d0_c_pins[]		= { GPIOD_9 };

/* Bank H func1 */
static const unsigned int hdmitx_sda_pins[]		= { GPIOH_0 };
static const unsigned int hdmitx_sck_pins[]		= { GPIOH_1 };
static const unsigned int hdmitx_hpd_in_pins[]		= { GPIOH_2 };
static const unsigned int ao_cec_a_pins[]		= { GPIOH_3 };
static const unsigned int spdif_out_h_pins[]		= { GPIOH_4 };
static const unsigned int spdif_in_pins[]		= { GPIOH_5 };
static const unsigned int i2c1_sda_h_pins[]		= { GPIOH_6 };
static const unsigned int i2c1_scl_h_pins[]		= { GPIOH_7 };
static const unsigned int i2c2_sda_h8_pins[]		= { GPIOH_8 };
static const unsigned int i2c2_scl_h9_pins[]		= { GPIOH_9 };
static const unsigned int eth_link_led_pins[]		= { GPIOH_10 };
static const unsigned int eth_act_led_pins[]		= { GPIOH_11 };

/* Bank H func2 */
static const unsigned int i2c2_sda_h0_pins[]		= { GPIOH_0 };
static const unsigned int i2c2_scl_h1_pins[]		= { GPIOH_1 };
static const unsigned int ao_cec_b_pins[]		= { GPIOH_3 };
static const unsigned int uart_d_tx_h_pins[]		= { GPIOH_4 };
static const unsigned int uart_d_rx_h_pins[]		= { GPIOH_5 };
static const unsigned int uart_d_cts_h_pins[]		= { GPIOH_6 };
static const unsigned int uart_d_rts_h_pins[]		= { GPIOH_7 };
static const unsigned int iso7816_clk_h_pins[]		= { GPIOH_8 };
static const unsigned int iso7816_data_h_pins[]		= { GPIOH_9 };
static const unsigned int uart_e_tx_h_pins[]		= { GPIOH_10 };
static const unsigned int uart_e_rx_h_pins[]		= { GPIOH_11 };

/* Bank H func3 */
static const unsigned int pwm_d_h_pins[]		= { GPIOH_6 };
static const unsigned int pwm_i_h_pins[]		= { GPIOH_7 };
static const unsigned int pdm_dclk_h_pins[]		= { GPIOH_8 };
static const unsigned int pdm_din0_h_pins[]		= { GPIOH_9 };
static const unsigned int pdm_din1_h_pins[]		= { GPIOH_10 };

/* Bank H func4 */
static const unsigned int mclk_1_h_pins[]		= { GPIOH_4 };
static const unsigned int tdm_sclk1_h_pins[]		= { GPIOH_5 };
static const unsigned int tdm_fs1_h_pins[]		= { GPIOH_6 };
static const unsigned int tdm_d2_h_pins[]		= { GPIOH_7 };
static const unsigned int tdm_d3_h_pins[]		= { GPIOH_8 };
static const unsigned int tdm_d4_h_pins[]		= { GPIOH_9 };

/* Bank H func5 */
static const unsigned int spi_a_miso_h_pins[]		= { GPIOH_4 };
static const unsigned int spi_a_mosi_h_pins[]		= { GPIOH_5 };
static const unsigned int spi_a_clk_h_pins[]		= { GPIOH_6 };
static const unsigned int spi_a_ss0_h_pins[]		= { GPIOH_7 };

/* Bank H func6 */
static const unsigned int gen_clk_h_pins[]		= { GPIOH_11 };
static const unsigned int tsin_b1_clk_pins[]		= { GPIOH_4 };
static const unsigned int tsin_b1_sop_pins[]		= { GPIOH_5 };
static const unsigned int tsin_b1_valid_pins[]		= { GPIOH_6 };
static const unsigned int tsin_b1_d0_pins[]		= { GPIOH_7 };

/* Bank X func1 */
static const unsigned int sdio_d0_pins[]		= { GPIOX_0 };
static const unsigned int sdio_d1_pins[]		= { GPIOX_1 };
static const unsigned int sdio_d2_pins[]		= { GPIOX_2 };
static const unsigned int sdio_d3_pins[]		= { GPIOX_3 };
static const unsigned int sdio_clk_pins[]		= { GPIOX_4 };
static const unsigned int sdio_cmd_pins[]		= { GPIOX_5 };
static const unsigned int pwm_a_x_pins[]		= { GPIOX_6 };
static const unsigned int pwm_f_x_pins[]		= { GPIOX_7 };
static const unsigned int tdm_d1_pins[]			= { GPIOX_8 };
static const unsigned int tdm_d0_pins[]			= { GPIOX_9 };
static const unsigned int tdm_fs0_pins[]		= { GPIOX_10 };
static const unsigned int tdm_sclk0_pins[]		= { GPIOX_11 };
static const unsigned int uart_a_tx_pins[]		= { GPIOX_12 };
static const unsigned int uart_a_rx_pins[]		= { GPIOX_13 };
static const unsigned int uart_a_cts_pins[]		= { GPIOX_14 };
static const unsigned int uart_a_rts_pins[]		= { GPIOX_15 };
static const unsigned int pwm_e_x_pins[]		= { GPIOX_16 };
static const unsigned int i2c1_sda_x_pins[]		= { GPIOX_17 };
static const unsigned int i2c1_scl_x_pins[]		= { GPIOX_18 };
static const unsigned int pwm_b_x_pins[]		= { GPIOX_19 };

/* Bank X func2 */
static const unsigned int pdm_din0_x_pins[]		= { GPIOX_8 };
static const unsigned int pdm_din1_x_pins[]		= { GPIOX_9 };
static const unsigned int pdm_dclk_x_pins[]		= { GPIOX_11 };

/* Bank X func3 */
static const unsigned int spi_a_mosi_x_pins[]		= { GPIOX_8 };
static const unsigned int spi_a_miso_x_pins[]		= { GPIOX_9 };
static const unsigned int spi_a_ss0_x_pins[]		= { GPIOX_10 };
static const unsigned int spi_a_clk_x_pins[]		= { GPIOX_11 };

/* Bank X func4 */
static const unsigned int pwm_c_x_pins[]		= { GPIOX_8 };
static const unsigned int i2c_slave_scl_pins[]		= { GPIOX_10 };
static const unsigned int i2c_slave_sda_pins[]		= { GPIOX_11 };

/* Bank X func5 */
static const unsigned int i2c3_sda_x_pins[]		= { GPIOX_10 };
static const unsigned int i2c3_scl_x_pins[]		= { GPIOX_11 };

/* Bank Z func1 */
static const unsigned int tdm_fs2_pins[]		= { GPIOZ_0 };
static const unsigned int tdm_sclk2_pins[]		= { GPIOZ_1 };
static const unsigned int tdm_d4_z_pins[]		= { GPIOZ_2 };
static const unsigned int tdm_d5_z_pins[]		= { GPIOZ_3 };
static const unsigned int tdm_d6_pins[]			= { GPIOZ_4 };
static const unsigned int tdm_d7_pins[]			= { GPIOZ_5 };
static const unsigned int mclk_2_pins[]			= { GPIOZ_6 };
static const unsigned int spdif_out_z_pins[]		= { GPIOZ_9 };
static const unsigned int dtv_a_if_agc_z10_pins[]	= { GPIOZ_10 };
static const unsigned int uart_e_tx_z11_pins[]		= { GPIOZ_11 };
static const unsigned int uart_e_rx_z12_pins[]		= { GPIOZ_12 };

/* Bank Z func2 */
static const unsigned int tsin_a_clk_pins[]		= { GPIOZ_0 };
static const unsigned int tsin_a_sop_pins[]		= { GPIOZ_1 };
static const unsigned int tsin_a_valid_pins[]		= { GPIOZ_2 };
static const unsigned int tsin_a_din0_pins[]		= { GPIOZ_3 };
static const unsigned int dtv_a_if_agc_z6_pins[]	= { GPIOZ_6 };
static const unsigned int dtv_b_if_agc_pins[]		= { GPIOZ_7 };
static const unsigned int i2c3_sda_z_pins[]		= { GPIOZ_8 };
static const unsigned int i2c3_scl_z_pins[]		= { GPIOZ_9 };
static const unsigned int dtv_a_rf_agc_pins[]		= { GPIOZ_10 };
static const unsigned int dtv_b_rf_agc_pins[]		= { GPIOZ_11 };

/* Bank Z func3 */
static const unsigned int sdcard_d0_z_pins[]		= { GPIOZ_0 };
static const unsigned int sdcard_d1_z_pins[]		= { GPIOZ_1 };
static const unsigned int sdcard_d2_z_pins[]		= { GPIOZ_2 };
static const unsigned int sdcard_d3_z_pins[]		= { GPIOZ_3 };
static const unsigned int sdcard_clk_z_pins[]		= { GPIOZ_4 };
static const unsigned int sdcard_cmd_z_pins[]		= { GPIOZ_5 };
static const unsigned int uart_e_tx_z8_pins[]		= { GPIOZ_8 };
static const unsigned int uart_e_rx_z9_pins[]		= { GPIOZ_9 };
static const unsigned int pdm_din1_z_pins[]		= { GPIOZ_10 };
static const unsigned int pdm_din0_z_pins[]		= { GPIOZ_11 };
static const unsigned int pdm_dclk_z_pins[]		= { GPIOZ_12 };

/* Bank Z func4 */
static const unsigned int spi_a_miso_z_pins[]		= { GPIOZ_0 };
static const unsigned int spi_a_mosi_z_pins[]		= { GPIOZ_1 };
static const unsigned int spi_a_clk_z_pins[]		= { GPIOZ_2 };
static const unsigned int spi_a_ss0_z_pins[]		= { GPIOZ_3 };
static const unsigned int spi_a_ss1_z_pins[]		= { GPIOZ_4 };
static const unsigned int spi_a_ss2_z_pins[]		= { GPIOZ_5 };
static const unsigned int i2c4_scl_z_pins[]		= { GPIOZ_11 };
static const unsigned int i2c4_sda_z_pins[]		= { GPIOZ_12 };

/* Bank Z func5 */
static const unsigned int uart_d_tx_z_pins[]		= { GPIOZ_0 };
static const unsigned int uart_d_rx_z_pins[]		= { GPIOZ_1 };
static const unsigned int uart_d_cts_z_pins[]		= { GPIOZ_2 };
static const unsigned int uart_d_rts_z_pins[]		= { GPIOZ_3 };
static const unsigned int pwm_g_z_pins[]		= { GPIOZ_4 };
static const unsigned int pwm_f_z_pins[]		= { GPIOZ_5 };
static const unsigned int pwm_e_z_pins[]		= { GPIOZ_6 };
static const unsigned int tsin_b_clk_z_pins[]		= { GPIOZ_7 };
static const unsigned int tsin_b_sop_z_pins[]		= { GPIOZ_10 };
static const unsigned int tsin_b_valid_z_pins[]		= { GPIOZ_11 };
static const unsigned int tsin_b_d0_z_pins[]		= { GPIOZ_12 };

/* Bank Z func6 */
static const unsigned int s2_demod_gpio7_pins[]		= { GPIOZ_0 };
static const unsigned int s2_demod_gpio6_pins[]		= { GPIOZ_1 };
static const unsigned int s2_demod_gpio5_pins[]		= { GPIOZ_2 };
static const unsigned int s2_demod_gpio4_pins[]		= { GPIOZ_3 };
static const unsigned int s2_demod_gpio3_pins[]		= { GPIOZ_4 };
static const unsigned int s2_demod_gpio2_pins[]		= { GPIOZ_5 };
static const unsigned int diseqc_out_pins[]		= { GPIOZ_7 };
static const unsigned int s2_demod_gpio1_pins[]		= { GPIOZ_8 };
static const unsigned int s2_demod_gpio0_pins[]		= { GPIOZ_12 };

/* Bank Z func7 */
static const unsigned int gen_clk_z9_pins[]		= { GPIOZ_9 };
static const unsigned int gen_clk_z12_pins[]		= { GPIOZ_12 };

static struct meson_pmx_group meson_s4_periphs_groups[] = {
	GPIO_GROUP(GPIOE_0),
	GPIO_GROUP(GPIOE_1),

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
	GPIO_GROUP(GPIOB_13),

	GPIO_GROUP(GPIOC_0),
	GPIO_GROUP(GPIOC_1),
	GPIO_GROUP(GPIOC_2),
	GPIO_GROUP(GPIOC_3),
	GPIO_GROUP(GPIOC_4),
	GPIO_GROUP(GPIOC_5),
	GPIO_GROUP(GPIOC_6),
	GPIO_GROUP(GPIOC_7),

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

	GPIO_GROUP(GPIOH_0),
	GPIO_GROUP(GPIOH_1),
	GPIO_GROUP(GPIOH_2),
	GPIO_GROUP(GPIOH_3),
	GPIO_GROUP(GPIOH_4),
	GPIO_GROUP(GPIOH_5),
	GPIO_GROUP(GPIOH_6),
	GPIO_GROUP(GPIOH_7),
	GPIO_GROUP(GPIOH_8),
	GPIO_GROUP(GPIOH_9),
	GPIO_GROUP(GPIOH_10),
	GPIO_GROUP(GPIOH_11),

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

	GPIO_GROUP(GPIO_TEST_N),

	/* BANK E func1 */
	GROUP(i2c0_sda,			1),
	GROUP(i2c0_scl,			1),

	/* BANK E func2 */
	GROUP(uart_b_tx_e,		2),
	GROUP(uart_b_rx_e,		2),

	/* BANK E func3 */
	GROUP(pwm_h,			3),
	GROUP(pwm_j,			3),

	/* BANK B func1 */
	GROUP(emmc_nand_d0,		1),
	GROUP(emmc_nand_d1,		1),
	GROUP(emmc_nand_d2,		1),
	GROUP(emmc_nand_d3,		1),
	GROUP(emmc_nand_d4,		1),
	GROUP(emmc_nand_d5,		1),
	GROUP(emmc_nand_d6,		1),
	GROUP(emmc_nand_d7,		1),
	GROUP(emmc_clk,			1),
	GROUP(emmc_rst,			1),
	GROUP(emmc_cmd,			1),
	GROUP(emmc_nand_ds,		1),

	/* Bank B func2 */
	GROUP(nand_wen_clk,		2),
	GROUP(nand_ale,			2),
	GROUP(nand_ren_wr,		2),
	GROUP(nand_cle,			2),
	GROUP(nand_ce0,			2),

	/* Bank B func3 */
	GROUP(spif_hold,		3),
	GROUP(spif_mo,			3),
	GROUP(spif_mi,			3),
	GROUP(spif_clk,			3),
	GROUP(spif_wp,			3),
	GROUP(spif_cs,			3),

	/* Bank C func1 */
	GROUP(sdcard_d0_c,		1),
	GROUP(sdcard_d1_c,		1),
	GROUP(sdcard_d2_c,		1),
	GROUP(sdcard_d3_c,		1),
	GROUP(sdcard_clk_c,		1),
	GROUP(sdcard_cmd_c,		1),
	GROUP(sdcard_cd,		1),

	/* Bank C func2 */
	GROUP(jtag_2_tdo,		2),
	GROUP(jtag_2_tdi,		2),
	GROUP(uart_b_rx_c,		2),
	GROUP(uart_b_tx_c,		2),
	GROUP(jtag_2_clk,		2),
	GROUP(jtag_2_tms,		2),
	GROUP(i2c1_sda_c,		2),
	GROUP(i2c1_scl_c,		2),

	/* Bank C func3 */
	GROUP(pdm_din1_c,		3),
	GROUP(pdm_din0_c,		3),
	GROUP(i2c4_sda_c,		3),
	GROUP(i2c4_scl_c,		3),
	GROUP(pdm_dclk_c,		3),
	GROUP(iso7816_clk_c,		3),
	GROUP(iso7816_data_c,		3),

	/* Bank C func4 */
	GROUP(tdm_d2_c,			4),
	GROUP(tdm_d3_c,			4),
	GROUP(tdm_fs1_c,		4),
	GROUP(tdm_sclk1_c,		4),
	GROUP(mclk_1_c,			4),
	GROUP(tdm_d4_c,			4),
	GROUP(tdm_d5_c,			4),

	/* Bank D func1 */
	GROUP(uart_b_tx_d,		1),
	GROUP(uart_b_rx_d,		1),
	GROUP(uart_b_cts_d,		1),
	GROUP(uart_b_rts_d,		1),
	GROUP(remote_out,		1),
	GROUP(remote_in,		1),
	GROUP(jtag_1_clk,		1),
	GROUP(jtag_1_tms,		1),
	GROUP(jtag_1_tdi,		1),
	GROUP(jtag_1_tdo,		1),
	GROUP(clk12_24,			1),
	GROUP(pwm_g_hiz,		1),

	/* Bank D func2 */
	GROUP(i2c4_sda_d,		2),
	GROUP(i2c4_scl_d,		2),
	GROUP(mclk_1_d,			2),
	GROUP(tdm_sclk1_d,		2),
	GROUP(tdm_fs1_d,		2),
	GROUP(tdm_d4_d,			2),
	GROUP(tdm_d3_d,			2),
	GROUP(tdm_d2_d,			2),
	GROUP(pwm_g_d,			2),

	/* Bank D func3 */
	GROUP(uart_c_tx,		3),
	GROUP(uart_c_rx,		3),
	GROUP(pwm_b_d,			3),
	GROUP(pwm_a_d,			3),
	GROUP(pwm_c_d,			3),
	GROUP(pwm_d_d,			3),
	GROUP(pwm_i_d,			3),

	/* Bank D func4 */
	GROUP(clk_32k_in,		4),
	GROUP(pwm_b_hiz,		4),
	GROUP(pwm_a_hiz,		4),
	GROUP(pwm_c_hiz,		4),
	GROUP(pdm_dclk_d,		4),
	GROUP(pdm_din0_d,		4),
	GROUP(pdm_din1_d,		4),

	/* Bank D func5 */
	GROUP(mic_mute_en,		5),
	GROUP(mic_mute_key,		5),
	GROUP(i2c1_sda_d,		5),
	GROUP(i2c1_scl_d,		5),
	GROUP(i2c2_sda_d,		5),
	GROUP(i2c2_scl_d,		5),

	/* Bank D func6 */
	GROUP(gen_clk_d,		6),
	GROUP(tsin_b_clk_c,		6),
	GROUP(tsin_b_sop_c,		6),
	GROUP(tsin_b_valid_c,		6),
	GROUP(tsin_b_d0_c,		6),

	/* Bank H func1 */
	GROUP(hdmitx_sda,		1),
	GROUP(hdmitx_sck,		1),
	GROUP(hdmitx_hpd_in,		1),
	GROUP(ao_cec_a,			1),
	GROUP(spdif_out_h,		1),
	GROUP(spdif_in,			1),
	GROUP(i2c1_sda_h,		1),
	GROUP(i2c1_scl_h,		1),
	GROUP(i2c2_sda_h8,		1),
	GROUP(i2c2_scl_h9,		1),
	GROUP(eth_link_led,		1),
	GROUP(eth_act_led,		1),

	/* Bank H func2 */
	GROUP(i2c2_sda_h0,		2),
	GROUP(i2c2_scl_h1,		2),
	GROUP(ao_cec_b,			2),
	GROUP(uart_d_tx_h,		2),
	GROUP(uart_d_rx_h,		2),
	GROUP(uart_d_cts_h,		2),
	GROUP(uart_d_rts_h,		2),
	GROUP(iso7816_clk_h,		2),
	GROUP(iso7816_data_h,		2),
	GROUP(uart_e_tx_h,		2),
	GROUP(uart_e_rx_h,		2),

	/* Bank H func3 */
	GROUP(pwm_d_h,			3),
	GROUP(pwm_i_h,			3),
	GROUP(pdm_dclk_h,		3),
	GROUP(pdm_din0_h,		3),
	GROUP(pdm_din1_h,		3),

	/* Bank H func4 */
	GROUP(mclk_1_h,			4),
	GROUP(tdm_sclk1_h,		4),
	GROUP(tdm_fs1_h,		4),
	GROUP(tdm_d2_h,			4),
	GROUP(tdm_d3_h,			4),
	GROUP(tdm_d4_h,			4),

	/* Bank H func5 */
	GROUP(spi_a_miso_h,		5),
	GROUP(spi_a_mosi_h,		5),
	GROUP(spi_a_clk_h,		5),
	GROUP(spi_a_ss0_h,		5),

	/* Bank H func6 */
	GROUP(gen_clk_h,		6),
	GROUP(tsin_b1_clk,		6),
	GROUP(tsin_b1_sop,		6),
	GROUP(tsin_b1_valid,		6),
	GROUP(tsin_b1_d0,		6),

	/* Bank X func1 */
	GROUP(sdio_d0,			1),
	GROUP(sdio_d1,			1),
	GROUP(sdio_d2,			1),
	GROUP(sdio_d3,			1),
	GROUP(sdio_clk,			1),
	GROUP(sdio_cmd,			1),
	GROUP(pwm_a_x,			1),
	GROUP(pwm_f_x,			1),
	GROUP(tdm_d1,			1),
	GROUP(tdm_d0,			1),
	GROUP(tdm_fs0,			1),
	GROUP(tdm_sclk0,		1),
	GROUP(uart_a_tx,		1),
	GROUP(uart_a_rx,		1),
	GROUP(uart_a_cts,		1),
	GROUP(uart_a_rts,		1),
	GROUP(pwm_e_x,			1),
	GROUP(i2c1_sda_x,		1),
	GROUP(i2c1_scl_x,		1),
	GROUP(pwm_b_x,			1),

	/* Bank X func2 */
	GROUP(pdm_din0_x,		2),
	GROUP(pdm_din1_x,		2),
	GROUP(pdm_dclk_x,		2),

	/* Bank X func3 */
	GROUP(spi_a_mosi_x,		3),
	GROUP(spi_a_miso_x,		3),
	GROUP(spi_a_ss0_x,		3),
	GROUP(spi_a_clk_x,		3),

	/* Bank X func4 */
	GROUP(pwm_c_x,			4),
	GROUP(i2c_slave_scl,		4),
	GROUP(i2c_slave_sda,		4),

	/* Bank X func5 */
	GROUP(i2c3_sda_x,		5),
	GROUP(i2c3_scl_x,		5),

	/* Bank Z func1 */
	GROUP(tdm_fs2,			1),
	GROUP(tdm_sclk2,		1),
	GROUP(tdm_d4_z,			1),
	GROUP(tdm_d5_z,			1),
	GROUP(tdm_d6,			1),
	GROUP(tdm_d7,			1),
	GROUP(mclk_2,			1),
	GROUP(spdif_out_z,		1),
	GROUP(dtv_a_if_agc_z10,		1),
	GROUP(uart_e_tx_z11,		1),
	GROUP(uart_e_rx_z12,		1),

	/* Bank Z func2 */
	GROUP(tsin_a_clk,		2),
	GROUP(tsin_a_sop,		2),
	GROUP(tsin_a_valid,		2),
	GROUP(tsin_a_din0,		2),
	GROUP(dtv_a_if_agc_z6,		2),
	GROUP(dtv_b_if_agc,		2),
	GROUP(i2c3_sda_z,		2),
	GROUP(i2c3_scl_z,		2),
	GROUP(dtv_a_rf_agc,		2),
	GROUP(dtv_b_rf_agc,		2),

	/* Bank Z func3 */
	GROUP(sdcard_d0_z,		3),
	GROUP(sdcard_d1_z,		3),
	GROUP(sdcard_d2_z,		3),
	GROUP(sdcard_d3_z,		3),
	GROUP(sdcard_clk_z,		3),
	GROUP(sdcard_cmd_z,		3),
	GROUP(uart_e_tx_z8,		3),
	GROUP(uart_e_rx_z9,		3),
	GROUP(pdm_din1_z,		3),
	GROUP(pdm_din0_z,		3),
	GROUP(pdm_dclk_z,		3),

	/* Bank Z func4 */
	GROUP(spi_a_miso_z,		4),
	GROUP(spi_a_mosi_z,		4),
	GROUP(spi_a_clk_z,		4),
	GROUP(spi_a_ss0_z,		4),
	GROUP(spi_a_ss1_z,		4),
	GROUP(spi_a_ss2_z,		4),
	GROUP(i2c4_scl_z,		4),
	GROUP(i2c4_sda_z,		4),

	/* Bank Z func5 */
	GROUP(uart_d_tx_z,		5),
	GROUP(uart_d_rx_z,		5),
	GROUP(uart_d_cts_z,		5),
	GROUP(uart_d_rts_z,		5),
	GROUP(pwm_g_z,			5),
	GROUP(pwm_f_z,			5),
	GROUP(pwm_e_z,			5),
	GROUP(tsin_b_clk_z,		5),
	GROUP(tsin_b_sop_z,		5),
	GROUP(tsin_b_valid_z,		5),
	GROUP(tsin_b_d0_z,		5),

	/* Bank Z func6 */
	GROUP(s2_demod_gpio7,		6),
	GROUP(s2_demod_gpio6,		6),
	GROUP(s2_demod_gpio5,		6),
	GROUP(s2_demod_gpio4,		6),
	GROUP(s2_demod_gpio3,		6),
	GROUP(s2_demod_gpio2,		6),
	GROUP(diseqc_out,		6),
	GROUP(s2_demod_gpio1,		6),
	GROUP(s2_demod_gpio0,		6),

	/* Bank Z func7 */
	GROUP(gen_clk_z9,		7),
	GROUP(gen_clk_z12,		7),
};

static const char * const gpio_periphs_groups[] = {
	"GPIOE_0", "GPIOE_1",

	"GPIOB_0", "GPIOB_1", "GPIOB_2", "GPIOB_3", "GPIOB_4", "GPIOB_5",
	"GPIOB_6", "GPIOB_7", "GPIOB_8", "GPIOB_9", "GPIOB_10", "GPIOB_11",
	"GPIOB_12", "GPIOB_13",

	"GPIOC_0", "GPIOC_1", "GPIOC_2", "GPIOC_3", "GPIOC_4", "GPIOC_5",
	"GPIOC_6", "GPIOC_7",

	"GPIOD_0", "GPIOD_1", "GPIOD_2", "GPIOD_3", "GPIOD_4", "GPIOD_5",
	"GPIOD_6", "GPIOD_7", "GPIOD_8", "GPIOD_9", "GPIOD_10", "GPIOD_11",

	"GPIOH_0", "GPIOH_1", "GPIOH_2", "GPIOH_3", "GPIOH_4", "GPIOH_5",
	"GPIOH_6", "GPIOH_7", "GPIOH_8", "GPIOH_9", "GPIOH_10", "GPIOH_11",

	"GPIOX_0", "GPIOX_1", "GPIOX_2", "GPIOX_3", "GPIOX_4", "GPIOX_5",
	"GPIOX_6", "GPIOX_7", "GPIOX_8", "GPIOX_9", "GPIOX_10", "GPIOX_11",
	"GPIOX_12", "GPIOX_13", "GPIOX_14", "GPIOX_15", "GPIOX_16", "GPIOX_17",
	"GPIOX_18", "GPIOX_19",

	"GPIOZ_0", "GPIOZ_1", "GPIOZ_2", "GPIOZ_3", "GPIOZ_4", "GPIOZ_5",
	"GPIOZ_6", "GPIOZ_7", "GPIOZ_8", "GPIOZ_9", "GPIOZ_10",
	"GPIOZ_11", "GPIOZ_12",

	"GPIO_TEST_N",
};

static const char * const i2c0_groups[] = {
	"i2c0_sda", "i2c0_scl",
};

static const char * const i2c1_groups[] = {
	"i2c1_sda_c", "i2c1_scl_c",
	"i2c1_sda_d", "i2c1_scl_d",
	"i2c1_sda_h", "i2c1_scl_h",
	"i2c1_sda_x", "i2c1_scl_x",
};

static const char * const i2c2_groups[] = {
	"i2c2_sda_d", "i2c2_scl_d",
	"i2c2_sda_h8", "i2c2_scl_h9",
	"i2c2_sda_h0", "i2c2_scl_h1l,"
};

static const char * const i2c3_groups[] = {
	"i2c3_sda_x", "i2c3_scl_x",
	"i2c3_sda_z", "i2c3_scl_z",
};

static const char * const i2c4_groups[] = {
	"i2c4_sda_c", "i2c4_scl_c",
	"i2c4_sda_d", "i2c4_scl_d",
	"i2c4_scl_z", "i2c4_sda_z",
};

static const char * const uart_a_groups[] = {
	"uart_a_tx", "uart_a_rx", "uart_a_cts", "uart_a_rts",
};

static const char * const uart_b_groups[] = {
	"uart_b_tx_e", "uart_b_rx_e", "uart_b_rx_c", "uart_b_tx_c",
	"uart_b_tx_d", "uart_b_rx_d", "uart_b_cts_d", "uart_b_rts_d",
};

static const char * const uart_c_groups[] = {
	"uart_c_tx", "uart_c_rx",
};

static const char * const uart_d_groups[] = {
	"uart_d_tx_h", "uart_d_rx_h", "uart_d_cts_h", "uart_d_rts_h",
	"uart_d_tx_z", "uart_d_rx_z", "uart_d_cts_z", "uart_d_rts_z",
};

static const char * const uart_e_groups[] = {
	"uart_e_tx_h", "uart_e_rx_h", "uart_e_tx_z11", "uart_e_rx_z12",
	"uart_e_tx_z8", "uart_e_rx_z9",
};

static const char * const emmc_groups[] = {
	"emmc_nand_d0", "emmc_nand_d1", "emmc_nand_d2", "emmc_nand_d3",
	"emmc_nand_d4", "emmc_nand_d5", "emmc_nand_d6", "emmc_nand_d7",
	"emmc_clk", "emmc_rst", "emmc_cmd", "emmc_nand_ds",
};

static const char * const nand_groups[] = {
	"emmc_nand_d0", "emmc_nand_d1", "emmc_nand_d2", "emmc_nand_d3",
	"emmc_nand_d4", "emmc_nand_d5", "emmc_nand_d6", "emmc_nand_d7",
	"nand_wen_clk", "nand_ale", "nand_ren_wr", "nand_cle", "nand_ce0",
};

static const char * const spif_groups[] = {
	"spif_hold", "spif_mo", "spif_mi", "spif_clk", "spif_wp",
	"spif_cs",
};

static const char * const sdcard_groups[] = {
	"sdcard_d0_c", "sdcard_d1_c", "sdcard_d2_c", "sdcard_d3_c",
	"sdcard_clk_c", "sdcard_cmd_c", "sdcard_cd",
	"sdcard_d0_z", "sdcard_d1_z", "sdcard_d2_z", "sdcard_d3_z",
	"sdcard_clk_z", "sdcard_cmd_z",
};

static const char * const jtag_1_groups[] = {
	"jtag_1_clk", "jtag_1_tms", "jtag_1_tdi", "jtag_1_tdo",
};

static const char * const jtag_2_groups[] = {
	"jtag_2_tdo", "jtag_2_tdi", "jtag_2_clk", "jtag_2_tms",
};

static const char * const pdm_groups[] = {
	"pdm_din1_c", "pdm_din0_c", "pdm_dclk_c",
	"pdm_dclk_d", "pdm_din0_d", "pdm_din1_d",
	"pdm_dclk_h", "pdm_din0_h", "pdm_din1_h",
	"pdm_din0_x", "pdm_din1_x", "pdm_dclk_x",
	"pdm_din1_z", "pdm_din0_z", "pdm_dclk_z",
};

static const char * const iso7816_groups[] = {
	"iso7816_clk_c", "iso7816_data_c",
	"iso7816_clk_h", "iso7816_data_h",
};

static const char * const tdm_groups[] = {
	"tdm_d2_c", "tdm_d3_c", "tdm_fs1_c", "tdm_d4_c", "tdm_d5_c", "tdm_sclk1_c",
	"tdm_fs1_d", "tdm_d4_d", "tdm_d3_d", "tdm_d2_d", "tdm_sclk1_d",
	"tdm_sclk1_h", "tdm_fs1_h", "tdm_d2_h", "tdm_d3_h", "tdm_d4_h",
	"tdm_d1", "tdm_d0", "tdm_fs0", "tdm_sclk0", "tdm_fs2", "tdm_sclk2",
	"tdm_d4_z", "tdm_d5_z", "tdm_d6", "tdm_d7",
};

static const char * const mclk_1_groups[] = {
	"mclk_1_c", "mclk_1_d", "mclk_1_h", "mclk_2",
};

static const char * const mclk_2_groups[] = {
	"mclk_2",
};

static const char * const remote_out_groups[] = {
	"remote_out",
};

static const char * const remote_in_groups[] = {
	"remote_in",
};

static const char * const clk12_24_groups[] = {
	"clk12_24",
};

static const char * const clk_32k_in_groups[] = {
	"clk_32k_in",
};

static const char * const pwm_a_hiz_groups[] = {
	"pwm_a_hiz",
};

static const char * const pwm_b_hiz_groups[] = {
	"pwm_b_hiz",
};

static const char * const pwm_c_hiz_groups[] = {
	"pwm_c_hiz",
};

static const char * const pwm_g_hiz_groups[] = {
	"pwm_g_hiz",
};

static const char * const pwm_a_groups[] = {
	"pwm_a_d",
};

static const char * const pwm_b_groups[] = {
	"pwm_b_d", "pwm_b_x",
};

static const char * const pwm_c_groups[] = {
	"pwm_c_d", "pwm_c_x",
};

static const char * const pwm_d_groups[] = {
	"pwm_d_d", "pwm_d_h",
};

static const char * const pwm_e_groups[] = {
	"pwm_e_x", "pwm_e_z",
};

static const char * const pwm_f_groups[] = {
	"pwm_f_x", "pwm_f_z",
};

static const char * const pwm_g_groups[] = {
	"pwm_g_d", "pwm_g_z",
};

static const char * const pwm_h_groups[] = {
	"pwm_h",
};

static const char * const pwm_i_groups[] = {
	"pwm_i_d", "pwm_i_h"
};

static const char * const pwm_j_groups[] = {
	"pwm_j",
};

static const char * const mic_mute_groups[] = {
	"mic_mute_en", "mic_mute_key",
};

static const char * const hdmitx_groups[] = {
	"hdmitx_sda", "hdmitx_sck", "hdmitx_hpd_in",
};

static const char * const ao_cec_a_groups[] = {
	"ao_cec_a",
};

static const char * const ao_cec_b_groups[] = {
	"ao_cec_b",
};

static const char * const spdif_out_groups[] = {
	"spdif_out_h", "spdif_out_z",
};

static const char * const spdif_in_groups[] = {
	"spdif_in",
};

static const char * const eth_groups[] = {
	"eth_link_led", "eth_act_led",
};

static const char * const spi_a_groups[] = {
	"spi_a_miso_h", "spi_a_mosi_h", "spi_a_clk_h", "spi_a_ss0_h",

	"spi_a_mosi_x", "spi_a_miso_x", "spi_a_ss0_x", "spi_a_clk_x",

	"spi_a_miso_z", "spi_a_mosi_z", "spi_a_clk_z", "spi_a_ss0_z",
	"spi_a_ss1_z", "spi_a_ss2_z",
};

static const char * const gen_clk_groups[] = {
	"gen_clk_h", "gen_clk_z9", "gen_clk_z12",
};

static const char * const sdio_groups[] = {
	"sdio_d0", "sdio_d1", "sdio_d2", "sdio_d3", "sdio_clk", "sdio_cmd",
};

static const char * const i2c_slave_groups[] = {
	"i2c_slave_scl", "i2c_slave_sda",
};

static const char * const dtv_groups[] = {
	"dtv_a_if_agc_z10", "dtv_a_if_agc_z6", "dtv_b_if_agc",
	"dtv_a_rf_agc", "dtv_b_rf_agc",
};

static const char * const tsin_a_groups[] = {
	"tsin_a_clk", "tsin_a_sop", "tsin_a_valid", "tsin_a_din0",
};

static const char * const tsin_b_groups[] = {
	"tsin_b_clk_c", "tsin_b_sop_c", "tsin_b_valid_c", "tsin_b_d0_c",
	"tsin_b_clk_z", "tsin_b_sop_z", "tsin_b_valid_z", "tsin_b_d0_z",
};

static const char * const tsin_b1_groups[] = {
	"tsin_b1_clk", "tsin_b1_sop", "tsin_b1_valid", "tsin_b1_d0",
};

static const char * const diseqc_out_groups[] = {
	"diseqc_out",
};

static const char * const s2_demod_groups[] = {
	"s2_demod_gpio7", "s2_demod_gpio6", "s2_demod_gpio5", "s2_demod_gpio4",
	"s2_demod_gpio3", "s2_demod_gpio2", "s2_demod_gpio1", "s2_demod_gpio0",
};

static struct meson_pmx_func meson_s4_periphs_functions[] = {
	FUNCTION(gpio_periphs),
	FUNCTION(i2c0),
	FUNCTION(i2c1),
	FUNCTION(i2c2),
	FUNCTION(i2c3),
	FUNCTION(i2c4),
	FUNCTION(uart_a),
	FUNCTION(uart_b),
	FUNCTION(uart_c),
	FUNCTION(uart_d),
	FUNCTION(uart_e),
	FUNCTION(emmc),
	FUNCTION(nand),
	FUNCTION(spif),
	FUNCTION(sdcard),
	FUNCTION(jtag_1),
	FUNCTION(jtag_2),
	FUNCTION(pdm),
	FUNCTION(iso7816),
	FUNCTION(tdm),
	FUNCTION(mclk_1),
	FUNCTION(mclk_2),
	FUNCTION(remote_out),
	FUNCTION(remote_in),
	FUNCTION(clk12_24),
	FUNCTION(clk_32k_in),
	FUNCTION(pwm_a_hiz),
	FUNCTION(pwm_b_hiz),
	FUNCTION(pwm_c_hiz),
	FUNCTION(pwm_g_hiz),
	FUNCTION(pwm_a),
	FUNCTION(pwm_b),
	FUNCTION(pwm_c),
	FUNCTION(pwm_d),
	FUNCTION(pwm_e),
	FUNCTION(pwm_f),
	FUNCTION(pwm_g),
	FUNCTION(pwm_h),
	FUNCTION(pwm_i),
	FUNCTION(pwm_j),
	FUNCTION(mic_mute),
	FUNCTION(hdmitx),
	FUNCTION(ao_cec_a),
	FUNCTION(ao_cec_b),
	FUNCTION(spdif_out),
	FUNCTION(spdif_in),
	FUNCTION(eth),
	FUNCTION(spi_a),
	FUNCTION(gen_clk),
	FUNCTION(sdio),
	FUNCTION(i2c_slave),
	FUNCTION(dtv),
	FUNCTION(tsin_a),
	FUNCTION(tsin_b),
	FUNCTION(tsin_b1),
	FUNCTION(diseqc_out),
	FUNCTION(s2_demod),
};

static struct meson_bank meson_s4_periphs_banks[] = {
	/* name  first  last  irq  pullen  pull  dir  out  in */
	BANK_DS("B", GPIOB_0,    GPIOB_13,  0, 13,
		0x63,  0,  0x64,  0,  0x62, 0,  0x61, 0,  0x60, 0, 0x67, 0),
	BANK_DS("C", GPIOC_0,    GPIOC_7,   14, 21,
		0x53,  0,  0x54,  0,  0x52, 0,  0x51, 0,  0x50, 0, 0x57, 0),
	BANK_DS("E", GPIOE_0,    GPIOE_1,   22, 23,
		0x43,  0,  0x44,  0,  0x42, 0,  0x41, 0,  0x40, 0, 0x47, 0),
	BANK_DS("D", GPIOD_0,    GPIOD_11,  24, 35,
		0x33,  0,  0x34,  0,  0x32, 0,  0x31, 0,  0x30, 0, 0x37, 0),
	BANK_DS("H", GPIOH_0,    GPIOH_11,  36, 47,
		0x23,  0,  0x24,  0,  0x22, 0,  0x21, 0,  0x20, 0, 0x27, 0),
	BANK_DS("X", GPIOX_0,    GPIOX_19,   48, 67,
		0x13,  0,  0x14,  0,  0x12, 0,  0x11, 0,  0x10, 0, 0x17, 0),
	BANK_DS("Z", GPIOZ_0,    GPIOZ_12,  68, 80,
		0x03,  0,  0x04,  0,  0x02, 0,  0x01, 0,  0x00, 0, 0x07, 0),
	BANK_DS("TEST_N", GPIO_TEST_N,    GPIO_TEST_N,   -1, -1,
		0x83,  0,  0x84,  0,  0x82, 0,  0x81,  0, 0x80, 0, 0x87, 0),
};

static struct meson_pmx_bank meson_s4_periphs_pmx_banks[] = {
	/*name	            first	 lask        reg offset*/
	BANK_PMX("B",      GPIOB_0,     GPIOB_13,    0x00, 0),
	BANK_PMX("C",      GPIOC_0,     GPIOC_7,     0x9,  0),
	BANK_PMX("E",      GPIOE_0,     GPIOE_1,     0x12, 0),
	BANK_PMX("D",      GPIOD_0,     GPIOD_11,    0x10, 0),
	BANK_PMX("H",      GPIOH_0,     GPIOH_11,    0xb,  0),
	BANK_PMX("X",      GPIOX_0,     GPIOX_19,    0x3,  0),
	BANK_PMX("Z",      GPIOZ_0,     GPIOZ_12,    0x6,  0),
	BANK_PMX("TEST_N", GPIO_TEST_N, GPIO_TEST_N, 0xf,  0)
};

static struct meson_axg_pmx_data meson_s4_periphs_pmx_banks_data = {
	.pmx_banks	= meson_s4_periphs_pmx_banks,
	.num_pmx_banks	= ARRAY_SIZE(meson_s4_periphs_pmx_banks),
};

static struct meson_pinctrl_data meson_s4_periphs_pinctrl_data = {
	.name		= "periphs-banks",
	.pins		= meson_s4_periphs_pins,
	.groups		= meson_s4_periphs_groups,
	.funcs		= meson_s4_periphs_functions,
	.banks		= meson_s4_periphs_banks,
	.num_pins	= ARRAY_SIZE(meson_s4_periphs_pins),
	.num_groups	= ARRAY_SIZE(meson_s4_periphs_groups),
	.num_funcs	= ARRAY_SIZE(meson_s4_periphs_functions),
	.num_banks	= ARRAY_SIZE(meson_s4_periphs_banks),
	.pmx_ops	= &meson_axg_pmx_ops,
	.pmx_data	= &meson_s4_periphs_pmx_banks_data,
	.parse_dt	= &meson_a1_parse_dt_extra,
};

static const struct of_device_id meson_s4_pinctrl_dt_match[] = {
	{
		.compatible = "amlogic,meson-s4-periphs-pinctrl",
		.data = &meson_s4_periphs_pinctrl_data,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, meson_s4_pinctrl_dt_match);

static struct platform_driver meson_s4_pinctrl_driver = {
	.probe  = meson_pinctrl_probe,
	.driver = {
		.name	= "meson-s4-pinctrl",
		.of_match_table = meson_s4_pinctrl_dt_match,
	},
};
module_platform_driver(meson_s4_pinctrl_driver);

MODULE_DESCRIPTION("Amlogic Meson S4 SoC pinctrl driver");
MODULE_LICENSE("Dual BSD/GPL");
