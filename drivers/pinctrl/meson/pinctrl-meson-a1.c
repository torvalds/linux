// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Pin controller and GPIO driver for Amlogic Meson A1 SoC.
 *
 * Copyright (c) 2019 Amlogic, Inc. All rights reserved.
 * Author: Qianggui Song <qianggui.song@amlogic.com>
 */

#include <dt-bindings/gpio/meson-a1-gpio.h>
#include "pinctrl-meson.h"
#include "pinctrl-meson-axg-pmx.h"

static const struct pinctrl_pin_desc meson_a1_periphs_pins[] = {
	MESON_PIN(GPIOP_0),
	MESON_PIN(GPIOP_1),
	MESON_PIN(GPIOP_2),
	MESON_PIN(GPIOP_3),
	MESON_PIN(GPIOP_4),
	MESON_PIN(GPIOP_5),
	MESON_PIN(GPIOP_6),
	MESON_PIN(GPIOP_7),
	MESON_PIN(GPIOP_8),
	MESON_PIN(GPIOP_9),
	MESON_PIN(GPIOP_10),
	MESON_PIN(GPIOP_11),
	MESON_PIN(GPIOP_12),
	MESON_PIN(GPIOB_0),
	MESON_PIN(GPIOB_1),
	MESON_PIN(GPIOB_2),
	MESON_PIN(GPIOB_3),
	MESON_PIN(GPIOB_4),
	MESON_PIN(GPIOB_5),
	MESON_PIN(GPIOB_6),
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
	MESON_PIN(GPIOF_0),
	MESON_PIN(GPIOF_1),
	MESON_PIN(GPIOF_2),
	MESON_PIN(GPIOF_3),
	MESON_PIN(GPIOF_4),
	MESON_PIN(GPIOF_5),
	MESON_PIN(GPIOF_6),
	MESON_PIN(GPIOF_7),
	MESON_PIN(GPIOF_8),
	MESON_PIN(GPIOF_9),
	MESON_PIN(GPIOF_10),
	MESON_PIN(GPIOF_11),
	MESON_PIN(GPIOF_12),
	MESON_PIN(GPIOA_0),
	MESON_PIN(GPIOA_1),
	MESON_PIN(GPIOA_2),
	MESON_PIN(GPIOA_3),
	MESON_PIN(GPIOA_4),
	MESON_PIN(GPIOA_5),
	MESON_PIN(GPIOA_6),
	MESON_PIN(GPIOA_7),
	MESON_PIN(GPIOA_8),
	MESON_PIN(GPIOA_9),
	MESON_PIN(GPIOA_10),
	MESON_PIN(GPIOA_11),
};

/* psram */
static const unsigned int psram_clkn_pins[]		= { GPIOP_0 };
static const unsigned int psram_clkp_pins[]		= { GPIOP_1 };
static const unsigned int psram_ce_n_pins[]		= { GPIOP_2 };
static const unsigned int psram_rst_n_pins[]		= { GPIOP_3 };
static const unsigned int psram_adq0_pins[]		= { GPIOP_4 };
static const unsigned int psram_adq1_pins[]		= { GPIOP_5 };
static const unsigned int psram_adq2_pins[]		= { GPIOP_6 };
static const unsigned int psram_adq3_pins[]		= { GPIOP_7 };
static const unsigned int psram_adq4_pins[]		= { GPIOP_8 };
static const unsigned int psram_adq5_pins[]		= { GPIOP_9 };
static const unsigned int psram_adq6_pins[]		= { GPIOP_10 };
static const unsigned int psram_adq7_pins[]		= { GPIOP_11 };
static const unsigned int psram_dqs_dm_pins[]		= { GPIOP_12 };

/* sdcard */
static const unsigned int sdcard_d0_b_pins[]		= { GPIOB_0 };
static const unsigned int sdcard_d1_b_pins[]		= { GPIOB_1 };
static const unsigned int sdcard_d2_b_pins[]		= { GPIOB_2 };
static const unsigned int sdcard_d3_b_pins[]		= { GPIOB_3 };
static const unsigned int sdcard_clk_b_pins[]		= { GPIOB_4 };
static const unsigned int sdcard_cmd_b_pins[]		= { GPIOB_5 };

static const unsigned int sdcard_d0_x_pins[]		= { GPIOX_0 };
static const unsigned int sdcard_d1_x_pins[]		= { GPIOX_1 };
static const unsigned int sdcard_d2_x_pins[]		= { GPIOX_2 };
static const unsigned int sdcard_d3_x_pins[]		= { GPIOX_3 };
static const unsigned int sdcard_clk_x_pins[]		= { GPIOX_4 };
static const unsigned int sdcard_cmd_x_pins[]		= { GPIOX_5 };

/* spif */
static const unsigned int spif_mo_pins[]		= { GPIOB_0 };
static const unsigned int spif_mi_pins[]		= { GPIOB_1 };
static const unsigned int spif_wp_n_pins[]		= { GPIOB_2 };
static const unsigned int spif_hold_n_pins[]		= { GPIOB_3 };
static const unsigned int spif_clk_pins[]		= { GPIOB_4 };
static const unsigned int spif_cs_pins[]		= { GPIOB_5 };

/* i2c0 */
static const unsigned int i2c0_sck_f9_pins[]		= { GPIOF_9 };
static const unsigned int i2c0_sda_f10_pins[]		= { GPIOF_10 };
static const unsigned int i2c0_sck_f11_pins[]		= { GPIOF_11 };
static const unsigned int i2c0_sda_f12_pins[]		= { GPIOF_12 };

/* i2c1 */
static const unsigned int i2c1_sda_x_pins[]		= { GPIOX_9 };
static const unsigned int i2c1_sck_x_pins[]		= { GPIOX_10 };
static const unsigned int i2c1_sda_a_pins[]		= { GPIOA_10 };
static const unsigned int i2c1_sck_a_pins[]		= { GPIOA_11 };

/* i2c2 */
static const unsigned int i2c2_sck_x0_pins[]		= { GPIOX_0 };
static const unsigned int i2c2_sda_x1_pins[]		= { GPIOX_1 };
static const unsigned int i2c2_sck_x15_pins[]		= { GPIOX_15 };
static const unsigned int i2c2_sda_x16_pins[]		= { GPIOX_16 };
static const unsigned int i2c2_sck_a4_pins[]		= { GPIOA_4 };
static const unsigned int i2c2_sda_a5_pins[]		= { GPIOA_5 };
static const unsigned int i2c2_sck_a8_pins[]		= { GPIOA_8 };
static const unsigned int i2c2_sda_a9_pins[]		= { GPIOA_9 };

/* i2c3 */
static const unsigned int i2c3_sck_f_pins[]		= { GPIOF_4 };
static const unsigned int i2c3_sda_f_pins[]		= { GPIOF_5 };
static const unsigned int i2c3_sck_x_pins[]		= { GPIOX_11 };
static const unsigned int i2c3_sda_x_pins[]		= { GPIOX_12 };

/* i2c slave */
static const unsigned int i2c_slave_sck_a_pins[]	= { GPIOA_10 };
static const unsigned int i2c_slave_sda_a_pins[]	= { GPIOA_11 };
static const unsigned int i2c_slave_sck_f_pins[]	= { GPIOF_11 };
static const unsigned int i2c_slave_sda_f_pins[]	= { GPIOF_12 };

/* uart_a */
static const unsigned int uart_a_tx_pins[]		= { GPIOX_11 };
static const unsigned int uart_a_rx_pins[]		= { GPIOX_12 };
static const unsigned int uart_a_cts_pins[]		= { GPIOX_13 };
static const unsigned int uart_a_rts_pins[]		= { GPIOX_14 };

/* uart_b */
static const unsigned int uart_b_tx_x_pins[]		= { GPIOX_7 };
static const unsigned int uart_b_rx_x_pins[]		= { GPIOX_8 };
static const unsigned int uart_b_tx_f_pins[]		= { GPIOF_0 };
static const unsigned int uart_b_rx_f_pins[]		= { GPIOF_1 };

/* uart_c */
static const unsigned int uart_c_tx_x0_pins[]		= { GPIOX_0 };
static const unsigned int uart_c_rx_x1_pins[]		= { GPIOX_1 };
static const unsigned int uart_c_cts_pins[]		= { GPIOX_2 };
static const unsigned int uart_c_rts_pins[]		= { GPIOX_3 };
static const unsigned int uart_c_tx_x15_pins[]		= { GPIOX_15 };
static const unsigned int uart_c_rx_x16_pins[]		= { GPIOX_16 };

/* pmw_a */
static const unsigned int pwm_a_x6_pins[]		= { GPIOX_6 };
static const unsigned int pwm_a_x7_pins[]		= { GPIOX_7 };
static const unsigned int pwm_a_f6_pins[]		= { GPIOF_6 };
static const unsigned int pwm_a_f10_pins[]		= { GPIOF_10 };
static const unsigned int pwm_a_a_pins[]		= { GPIOA_5 };

/* pmw_b */
static const unsigned int pwm_b_x_pins[]		= { GPIOX_8 };
static const unsigned int pwm_b_f_pins[]		= { GPIOF_7 };
static const unsigned int pwm_b_a_pins[]		= { GPIOA_11 };

/* pmw_c */
static const unsigned int pwm_c_x_pins[]		= { GPIOX_9 };
static const unsigned int pwm_c_f3_pins[]		= { GPIOF_3 };
static const unsigned int pwm_c_f8_pins[]		= { GPIOF_8 };
static const unsigned int pwm_c_a_pins[]		= { GPIOA_10 };

/* pwm_d */
static const unsigned int pwm_d_x10_pins[]		= { GPIOX_10 };
static const unsigned int pwm_d_x13_pins[]		= { GPIOX_13 };
static const unsigned int pwm_d_x15_pins[]		= { GPIOX_15 };
static const unsigned int pwm_d_f_pins[]		= { GPIOF_11 };

/* pwm_e */
static const unsigned int pwm_e_p_pins[]		= { GPIOP_3 };
static const unsigned int pwm_e_x2_pins[]		= { GPIOX_2 };
static const unsigned int pwm_e_x14_pins[]		= { GPIOX_14 };
static const unsigned int pwm_e_x16_pins[]		= { GPIOX_16 };
static const unsigned int pwm_e_f_pins[]		= { GPIOF_3 };
static const unsigned int pwm_e_a_pins[]		= { GPIOA_0 };

/* pwm_f */
static const unsigned int pwm_f_b_pins[]		= { GPIOB_6 };
static const unsigned int pwm_f_x_pins[]		= { GPIOX_3 };
static const unsigned int pwm_f_f4_pins[]		= { GPIOF_4 };
static const unsigned int pwm_f_f12_pins[]		= { GPIOF_12 };

/* pwm_a_hiz */
static const unsigned int pwm_a_hiz_f8_pins[]		= { GPIOF_8 };
static const unsigned int pwm_a_hiz_f10_pins[]		= { GPIOF_10 };
static const unsigned int pmw_a_hiz_f6_pins[]		= { GPIOF_6 };

/* pwm_b_hiz */
static const unsigned int pwm_b_hiz_pins[]		= { GPIOF_7 };

/* pmw_c_hiz */
static const unsigned int pwm_c_hiz_pins[]		= { GPIOF_8 };

/* tdm_a */
static const unsigned int tdm_a_dout1_pins[]		= { GPIOX_7 };
static const unsigned int tdm_a_dout0_pins[]		= { GPIOX_8 };
static const unsigned int tdm_a_fs_pins[]		= { GPIOX_9 };
static const unsigned int tdm_a_sclk_pins[]		= { GPIOX_10 };
static const unsigned int tdm_a_din1_pins[]		= { GPIOX_7 };
static const unsigned int tdm_a_din0_pins[]		= { GPIOX_8 };
static const unsigned int tdm_a_slv_fs_pins[]		= { GPIOX_9 };
static const unsigned int tdm_a_slv_sclk_pins[]		= { GPIOX_10 };

/* spi_a */
static const unsigned int spi_a_mosi_x2_pins[]		= { GPIOX_2 };
static const unsigned int spi_a_ss0_x3_pins[]		= { GPIOX_3 };
static const unsigned int spi_a_sclk_x4_pins[]		= { GPIOX_4 };
static const unsigned int spi_a_miso_x5_pins[]		= { GPIOX_5 };
static const unsigned int spi_a_mosi_x7_pins[]		= { GPIOX_7 };
static const unsigned int spi_a_miso_x8_pins[]		= { GPIOX_8 };
static const unsigned int spi_a_ss0_x9_pins[]		= { GPIOX_9 };
static const unsigned int spi_a_sclk_x10_pins[]		= { GPIOX_10 };

static const unsigned int spi_a_mosi_a_pins[]		= { GPIOA_6 };
static const unsigned int spi_a_miso_a_pins[]		= { GPIOA_7 };
static const unsigned int spi_a_ss0_a_pins[]		= { GPIOA_8 };
static const unsigned int spi_a_sclk_a_pins[]		= { GPIOA_9 };

/* pdm */
static const unsigned int pdm_din0_x_pins[]		= { GPIOX_7 };
static const unsigned int pdm_din1_x_pins[]		= { GPIOX_8 };
static const unsigned int pdm_din2_x_pins[]		= { GPIOX_9 };
static const unsigned int pdm_dclk_x_pins[]		= { GPIOX_10 };

static const unsigned int pdm_din2_a_pins[]		= { GPIOA_6 };
static const unsigned int pdm_din1_a_pins[]		= { GPIOA_7 };
static const unsigned int pdm_din0_a_pins[]		= { GPIOA_8 };
static const unsigned int pdm_dclk_a_pins[]		= { GPIOA_9 };

/* gen_clk */
static const unsigned int gen_clk_x_pins[]		= { GPIOX_7 };
static const unsigned int gen_clk_f8_pins[]		= { GPIOF_8 };
static const unsigned int gen_clk_f10_pins[]		= { GPIOF_10 };
static const unsigned int gen_clk_a_pins[]		= { GPIOA_11 };

/* jtag_a */
static const unsigned int jtag_a_clk_pins[]		= { GPIOF_4 };
static const unsigned int jtag_a_tms_pins[]		= { GPIOF_5 };
static const unsigned int jtag_a_tdi_pins[]		= { GPIOF_6 };
static const unsigned int jtag_a_tdo_pins[]		= { GPIOF_7 };

/* clk_32_in */
static const unsigned int clk_32k_in_pins[]		= { GPIOF_2 };

/* ir in */
static const unsigned int remote_input_f_pins[]		= { GPIOF_3 };
static const unsigned int remote_input_a_pins[]		= { GPIOA_11 };

/* ir out */
static const unsigned int remote_out_pins[]		= { GPIOF_5 };

/* spdif */
static const unsigned int spdif_in_f6_pins[]		= { GPIOF_6 };
static const unsigned int spdif_in_f7_pins[]		= { GPIOF_7 };

/* sw */
static const unsigned int swclk_pins[]			= { GPIOF_4 };
static const unsigned int swdio_pins[]			= { GPIOF_5 };

/* clk_25 */
static const unsigned int clk25_pins[]			= { GPIOF_10 };

/* cec_a */
static const unsigned int cec_a_pins[]			= { GPIOF_2 };

/* cec_b */
static const unsigned int cec_b_pins[]			= { GPIOF_2 };

/* clk12_24 */
static const unsigned int clk12_24_pins[]		= { GPIOF_10 };

/* mclk_0 */
static const unsigned int mclk_0_pins[]			= { GPIOA_0 };

/* tdm_b */
static const unsigned int tdm_b_sclk_pins[]		= { GPIOA_1 };
static const unsigned int tdm_b_fs_pins[]		= { GPIOA_2 };
static const unsigned int tdm_b_dout0_pins[]		= { GPIOA_3 };
static const unsigned int tdm_b_dout1_pins[]		= { GPIOA_4 };
static const unsigned int tdm_b_dout2_pins[]		= { GPIOA_5 };
static const unsigned int tdm_b_dout3_pins[]		= { GPIOA_6 };
static const unsigned int tdm_b_dout4_pins[]		= { GPIOA_7 };
static const unsigned int tdm_b_dout5_pins[]		= { GPIOA_8 };
static const unsigned int tdm_b_slv_sclk_pins[]		= { GPIOA_5 };
static const unsigned int tdm_b_slv_fs_pins[]		= { GPIOA_6 };
static const unsigned int tdm_b_din0_pins[]		= { GPIOA_7 };
static const unsigned int tdm_b_din1_pins[]		= { GPIOA_8 };
static const unsigned int tdm_b_din2_pins[]		= { GPIOA_9 };

/* mclk_vad */
static const unsigned int mclk_vad_pins[]		= { GPIOA_0 };

/* tdm_vad */
static const unsigned int tdm_vad_sclk_a1_pins[]	= { GPIOA_1 };
static const unsigned int tdm_vad_fs_a2_pins[]		= { GPIOA_2 };
static const unsigned int tdm_vad_sclk_a5_pins[]	= { GPIOA_5 };
static const unsigned int tdm_vad_fs_a6_pins[]		= { GPIOA_6 };

/* tst_out */
static const unsigned int tst_out0_pins[]		= { GPIOA_0 };
static const unsigned int tst_out1_pins[]		= { GPIOA_1 };
static const unsigned int tst_out2_pins[]		= { GPIOA_2 };
static const unsigned int tst_out3_pins[]		= { GPIOA_3 };
static const unsigned int tst_out4_pins[]		= { GPIOA_4 };
static const unsigned int tst_out5_pins[]		= { GPIOA_5 };
static const unsigned int tst_out6_pins[]		= { GPIOA_6 };
static const unsigned int tst_out7_pins[]		= { GPIOA_7 };
static const unsigned int tst_out8_pins[]		= { GPIOA_8 };
static const unsigned int tst_out9_pins[]		= { GPIOA_9 };
static const unsigned int tst_out10_pins[]		= { GPIOA_10 };
static const unsigned int tst_out11_pins[]		= { GPIOA_11 };

/* mute */
static const unsigned int mute_key_pins[]		= { GPIOA_4 };
static const unsigned int mute_en_pins[]		= { GPIOA_5 };

static struct meson_pmx_group meson_a1_periphs_groups[] = {
	GPIO_GROUP(GPIOP_0),
	GPIO_GROUP(GPIOP_1),
	GPIO_GROUP(GPIOP_2),
	GPIO_GROUP(GPIOP_3),
	GPIO_GROUP(GPIOP_4),
	GPIO_GROUP(GPIOP_5),
	GPIO_GROUP(GPIOP_6),
	GPIO_GROUP(GPIOP_7),
	GPIO_GROUP(GPIOP_8),
	GPIO_GROUP(GPIOP_9),
	GPIO_GROUP(GPIOP_10),
	GPIO_GROUP(GPIOP_11),
	GPIO_GROUP(GPIOP_12),
	GPIO_GROUP(GPIOB_0),
	GPIO_GROUP(GPIOB_1),
	GPIO_GROUP(GPIOB_2),
	GPIO_GROUP(GPIOB_3),
	GPIO_GROUP(GPIOB_4),
	GPIO_GROUP(GPIOB_5),
	GPIO_GROUP(GPIOB_6),
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
	GPIO_GROUP(GPIOF_0),
	GPIO_GROUP(GPIOF_1),
	GPIO_GROUP(GPIOF_2),
	GPIO_GROUP(GPIOF_3),
	GPIO_GROUP(GPIOF_4),
	GPIO_GROUP(GPIOF_5),
	GPIO_GROUP(GPIOF_6),
	GPIO_GROUP(GPIOF_7),
	GPIO_GROUP(GPIOF_8),
	GPIO_GROUP(GPIOF_9),
	GPIO_GROUP(GPIOF_10),
	GPIO_GROUP(GPIOF_11),
	GPIO_GROUP(GPIOF_12),
	GPIO_GROUP(GPIOA_0),
	GPIO_GROUP(GPIOA_1),
	GPIO_GROUP(GPIOA_2),
	GPIO_GROUP(GPIOA_3),
	GPIO_GROUP(GPIOA_4),
	GPIO_GROUP(GPIOA_5),
	GPIO_GROUP(GPIOA_6),
	GPIO_GROUP(GPIOA_7),
	GPIO_GROUP(GPIOA_8),
	GPIO_GROUP(GPIOA_9),
	GPIO_GROUP(GPIOA_10),
	GPIO_GROUP(GPIOA_11),

	/* bank P func1 */
	GROUP(psram_clkn,		1),
	GROUP(psram_clkp,		1),
	GROUP(psram_ce_n,		1),
	GROUP(psram_rst_n,		1),
	GROUP(psram_adq0,		1),
	GROUP(psram_adq1,		1),
	GROUP(psram_adq2,		1),
	GROUP(psram_adq3,		1),
	GROUP(psram_adq4,		1),
	GROUP(psram_adq5,		1),
	GROUP(psram_adq6,		1),
	GROUP(psram_adq7,		1),
	GROUP(psram_dqs_dm,		1),

	/*bank P func2 */
	GROUP(pwm_e_p,			2),

	/*bank B func1 */
	GROUP(spif_mo,			1),
	GROUP(spif_mi,			1),
	GROUP(spif_wp_n,		1),
	GROUP(spif_hold_n,		1),
	GROUP(spif_clk,			1),
	GROUP(spif_cs,			1),
	GROUP(pwm_f_b,			1),

	/*bank B func2 */
	GROUP(sdcard_d0_b,		2),
	GROUP(sdcard_d1_b,		2),
	GROUP(sdcard_d2_b,		2),
	GROUP(sdcard_d3_b,		2),
	GROUP(sdcard_clk_b,		2),
	GROUP(sdcard_cmd_b,		2),

	/*bank X func1 */
	GROUP(sdcard_d0_x,		1),
	GROUP(sdcard_d1_x,		1),
	GROUP(sdcard_d2_x,		1),
	GROUP(sdcard_d3_x,		1),
	GROUP(sdcard_clk_x,		1),
	GROUP(sdcard_cmd_x,		1),
	GROUP(pwm_a_x6,			1),
	GROUP(tdm_a_dout1,		1),
	GROUP(tdm_a_dout0,		1),
	GROUP(tdm_a_fs,			1),
	GROUP(tdm_a_sclk,		1),
	GROUP(uart_a_tx,		1),
	GROUP(uart_a_rx,		1),
	GROUP(uart_a_cts,		1),
	GROUP(uart_a_rts,		1),
	GROUP(pwm_d_x15,		1),
	GROUP(pwm_e_x16,		1),

	/*bank X func2 */
	GROUP(i2c2_sck_x0,		2),
	GROUP(i2c2_sda_x1,		2),
	GROUP(spi_a_mosi_x2,		2),
	GROUP(spi_a_ss0_x3,		2),
	GROUP(spi_a_sclk_x4,		2),
	GROUP(spi_a_miso_x5,		2),
	GROUP(tdm_a_din1,		2),
	GROUP(tdm_a_din0,		2),
	GROUP(tdm_a_slv_fs,		2),
	GROUP(tdm_a_slv_sclk,		2),
	GROUP(i2c3_sck_x,		2),
	GROUP(i2c3_sda_x,		2),
	GROUP(pwm_d_x13,		2),
	GROUP(pwm_e_x14,		2),
	GROUP(i2c2_sck_x15,		2),
	GROUP(i2c2_sda_x16,		2),

	/*bank X func3 */
	GROUP(uart_c_tx_x0,		3),
	GROUP(uart_c_rx_x1,		3),
	GROUP(uart_c_cts,		3),
	GROUP(uart_c_rts,		3),
	GROUP(pdm_din0_x,		3),
	GROUP(pdm_din1_x,		3),
	GROUP(pdm_din2_x,		3),
	GROUP(pdm_dclk_x,		3),
	GROUP(uart_c_tx_x15,		3),
	GROUP(uart_c_rx_x16,		3),

	/*bank X func4 */
	GROUP(pwm_e_x2,			4),
	GROUP(pwm_f_x,			4),
	GROUP(spi_a_mosi_x7,		4),
	GROUP(spi_a_miso_x8,		4),
	GROUP(spi_a_ss0_x9,		4),
	GROUP(spi_a_sclk_x10,		4),

	/*bank X func5 */
	GROUP(uart_b_tx_x,		5),
	GROUP(uart_b_rx_x,		5),
	GROUP(i2c1_sda_x,		5),
	GROUP(i2c1_sck_x,		5),

	/*bank X func6 */
	GROUP(pwm_a_x7,			6),
	GROUP(pwm_b_x,			6),
	GROUP(pwm_c_x,			6),
	GROUP(pwm_d_x10,		6),

	/*bank X func7 */
	GROUP(gen_clk_x,		7),

	/*bank F func1 */
	GROUP(uart_b_tx_f,		1),
	GROUP(uart_b_rx_f,		1),
	GROUP(remote_input_f,		1),
	GROUP(jtag_a_clk,		1),
	GROUP(jtag_a_tms,		1),
	GROUP(jtag_a_tdi,		1),
	GROUP(jtag_a_tdo,		1),
	GROUP(gen_clk_f8,		1),
	GROUP(pwm_a_f10,		1),
	GROUP(i2c0_sck_f11,		1),
	GROUP(i2c0_sda_f12,		1),

	/*bank F func2 */
	GROUP(clk_32k_in,		2),
	GROUP(pwm_e_f,			2),
	GROUP(pwm_f_f4,			2),
	GROUP(remote_out,		2),
	GROUP(spdif_in_f6,		2),
	GROUP(spdif_in_f7,		2),
	GROUP(pwm_a_hiz_f8,		2),
	GROUP(pwm_a_hiz_f10,		2),
	GROUP(pwm_d_f,			2),
	GROUP(pwm_f_f12,		2),

	/*bank F func3 */
	GROUP(pwm_c_f3,			3),
	GROUP(swclk,			3),
	GROUP(swdio,			3),
	GROUP(pwm_a_f6,			3),
	GROUP(pwm_b_f,			3),
	GROUP(pwm_c_f8,			3),
	GROUP(clk25,			3),
	GROUP(i2c_slave_sck_f,		3),
	GROUP(i2c_slave_sda_f,		3),

	/*bank F func4 */
	GROUP(cec_a,			4),
	GROUP(i2c3_sck_f,		4),
	GROUP(i2c3_sda_f,		4),
	GROUP(pmw_a_hiz_f6,		4),
	GROUP(pwm_b_hiz,		4),
	GROUP(pwm_c_hiz,		4),
	GROUP(i2c0_sck_f9,		4),
	GROUP(i2c0_sda_f10,		4),

	/*bank F func5 */
	GROUP(cec_b,			5),
	GROUP(clk12_24,			5),

	/*bank F func7 */
	GROUP(gen_clk_f10,		7),

	/*bank A func1 */
	GROUP(mclk_0,			1),
	GROUP(tdm_b_sclk,		1),
	GROUP(tdm_b_fs,			1),
	GROUP(tdm_b_dout0,		1),
	GROUP(tdm_b_dout1,		1),
	GROUP(tdm_b_dout2,		1),
	GROUP(tdm_b_dout3,		1),
	GROUP(tdm_b_dout4,		1),
	GROUP(tdm_b_dout5,		1),
	GROUP(remote_input_a,		1),

	/*bank A func2 */
	GROUP(pwm_e_a,			2),
	GROUP(tdm_b_slv_sclk,		2),
	GROUP(tdm_b_slv_fs,		2),
	GROUP(tdm_b_din0,		2),
	GROUP(tdm_b_din1,		2),
	GROUP(tdm_b_din2,		2),
	GROUP(i2c1_sda_a,		2),
	GROUP(i2c1_sck_a,		2),

	/*bank A func3 */
	GROUP(i2c2_sck_a4,		3),
	GROUP(i2c2_sda_a5,		3),
	GROUP(pdm_din2_a,		3),
	GROUP(pdm_din1_a,		3),
	GROUP(pdm_din0_a,		3),
	GROUP(pdm_dclk_a,		3),
	GROUP(pwm_c_a,			3),
	GROUP(pwm_b_a,			3),

	/*bank A func4 */
	GROUP(pwm_a_a,			4),
	GROUP(spi_a_mosi_a,		4),
	GROUP(spi_a_miso_a,		4),
	GROUP(spi_a_ss0_a,		4),
	GROUP(spi_a_sclk_a,		4),
	GROUP(i2c_slave_sck_a,		4),
	GROUP(i2c_slave_sda_a,		4),

	/*bank A func5 */
	GROUP(mclk_vad,			5),
	GROUP(tdm_vad_sclk_a1,		5),
	GROUP(tdm_vad_fs_a2,		5),
	GROUP(tdm_vad_sclk_a5,		5),
	GROUP(tdm_vad_fs_a6,		5),
	GROUP(i2c2_sck_a8,		5),
	GROUP(i2c2_sda_a9,		5),

	/*bank A func6 */
	GROUP(tst_out0,			6),
	GROUP(tst_out1,			6),
	GROUP(tst_out2,			6),
	GROUP(tst_out3,			6),
	GROUP(tst_out4,			6),
	GROUP(tst_out5,			6),
	GROUP(tst_out6,			6),
	GROUP(tst_out7,			6),
	GROUP(tst_out8,			6),
	GROUP(tst_out9,			6),
	GROUP(tst_out10,		6),
	GROUP(tst_out11,		6),

	/*bank A func7 */
	GROUP(mute_key,			7),
	GROUP(mute_en,			7),
	GROUP(gen_clk_a,		7),
};

static const char * const gpio_periphs_groups[] = {
	"GPIOP_0", "GPIOP_1", "GPIOP_2", "GPIOP_3", "GPIOP_4",
	"GPIOP_5", "GPIOP_6", "GPIOP_7", "GPIOP_8", "GPIOP_9",
	"GPIOP_10", "GPIOP_11", "GPIOP_12",

	"GPIOB_0", "GPIOB_1", "GPIOB_2", "GPIOB_3", "GPIOB_4",
	"GPIOB_5", "GPIOB_6",

	"GPIOX_0", "GPIOX_1", "GPIOX_2", "GPIOX_3", "GPIOX_4",
	"GPIOX_5", "GPIOX_6", "GPIOX_7", "GPIOX_8", "GPIOX_9",
	"GPIOX_10", "GPIOX_11", "GPIOX_12", "GPIOX_13", "GPIOX_14",
	"GPIOX_15", "GPIOX_16",

	"GPIOF_0", "GPIOF_1", "GPIOF_2", "GPIOF_3", "GPIOF_4",
	"GPIOF_5", "GPIOF_6", "GPIOF_7", "GPIOF_8", "GPIOF_9",
	"GPIOF_10", "GPIOF_11", "GPIOF_12",

	"GPIOA_0", "GPIOA_1", "GPIOA_2", "GPIOA_3", "GPIOA_4",
	"GPIOA_5", "GPIOA_6", "GPIOA_7", "GPIOA_8", "GPIOA_9",
	"GPIOA_10", "GPIOA_11",
};

static const char * const psram_groups[] = {
	"psram_clkn", "psram_clkp", "psram_ce_n", "psram_rst_n", "psram_adq0",
	"psram_adq1", "psram_adq2", "psram_adq3", "psram_adq4", "psram_adq5",
	"psram_adq6", "psram_adq7", "psram_dqs_dm",
};

static const char * const pwm_a_groups[] = {
	"pwm_a_x6", "pwm_a_x7", "pwm_a_f10", "pwm_a_f6", "pwm_a_a",
};

static const char * const pwm_b_groups[] = {
	"pwm_b_x", "pwm_b_f", "pwm_b_a",
};

static const char * const pwm_c_groups[] = {
	"pwm_c_x", "pwm_c_f3", "pwm_c_f8", "pwm_c_a",
};

static const char * const pwm_d_groups[] = {
	"pwm_d_x15", "pwm_d_x13", "pwm_d_x10", "pwm_d_f",
};

static const char * const pwm_e_groups[] = {
	"pwm_e_p", "pwm_e_x16", "pwm_e_x14", "pwm_e_x2", "pwm_e_f",
	"pwm_e_a",
};

static const char * const pwm_f_groups[] = {
	"pwm_f_b", "pwm_f_x", "pwm_f_f4", "pwm_f_f12",
};

static const char * const pwm_a_hiz_groups[] = {
	"pwm_a_hiz_f8", "pwm_a_hiz_f10", "pwm_a_hiz_f6",
};

static const char * const pwm_b_hiz_groups[] = {
	"pwm_b_hiz",
};

static const char * const pwm_c_hiz_groups[] = {
	"pwm_c_hiz",
};

static const char * const spif_groups[] = {
	"spif_mo", "spif_mi", "spif_wp_n", "spif_hold_n", "spif_clk",
	"spif_cs",
};

static const char * const sdcard_groups[] = {
	"sdcard_d0_b", "sdcard_d1_b", "sdcard_d2_b", "sdcard_d3_b",
	"sdcard_clk_b", "sdcard_cmd_b",

	"sdcard_d0_x", "sdcard_d1_x", "sdcard_d2_x", "sdcard_d3_x",
	"sdcard_clk_x", "sdcard_cmd_x",
};

static const char * const tdm_a_groups[] = {
	"tdm_a_din0", "tdm_a_din1",  "tdm_a_fs", "tdm_a_sclk",
	"tdm_a_slv_fs", "tdm_a_slv_sclk", "tdm_a_dout0", "tdm_a_dout1",
};

static const char * const uart_a_groups[] = {
	"uart_a_tx", "uart_a_rx", "uart_a_cts", "uart_a_rts",
};

static const char * const uart_b_groups[] = {
	"uart_b_tx_x", "uart_b_rx_x", "uart_b_tx_f", "uart_b_rx_f",
};

static const char * const uart_c_groups[] = {
	"uart_c_tx_x0", "uart_c_rx_x1", "uart_c_cts", "uart_c_rts",
	"uart_c_tx_x15", "uart_c_rx_x16",
};

static const char * const i2c0_groups[] = {
	"i2c0_sck_f11", "i2c0_sda_f12", "i2c0_sck_f9", "i2c0_sda_f10",
};

static const char * const i2c1_groups[] = {
	"i2c1_sda_x", "i2c1_sck_x", "i2c1_sda_a", "i2c1_sck_a",
};

static const char * const i2c2_groups[] = {
	"i2c2_sck_x0", "i2c2_sda_x1", "i2c2_sck_x15", "i2c2_sda_x16",
	"i2c2_sck_a4", "i2c2_sda_a5", "i2c2_sck_a8", "i2c2_sda_a9",
};

static const char * const i2c3_groups[] = {
	"i2c3_sck_x", "i2c3_sda_x", "i2c3_sck_f", "i2c3_sda_f",
};

static const char * const spi_a_groups[] = {
	"spi_a_mosi_x2", "spi_a_ss0_x3", "spi_a_sclk_x4", "spi_a_miso_x5",
	"spi_a_mosi_x7", "spi_a_miso_x8", "spi_a_ss0_x9", "spi_a_sclk_x10",

	"spi_a_mosi_a", "spi_a_miso_a", "spi_a_ss0_a", "spi_a_sclk_a",
};

static const char * const pdm_groups[] = {
	"pdm_din0_x", "pdm_din1_x", "pdm_din2_x", "pdm_dclk_x", "pdm_din2_a",
	"pdm_din1_a", "pdm_din0_a", "pdm_dclk_a",
};

static const char * const gen_clk_groups[] = {
	"gen_clk_x", "gen_clk_f8", "gen_clk_f10", "gen_clk_a",
};

static const char * const remote_input_groups[] = {
	"remote_input_f",
	"remote_input_a",
};

static const char * const jtag_a_groups[] = {
	"jtag_a_clk", "jtag_a_tms", "jtag_a_tdi", "jtag_a_tdo",
};

static const char * const clk_32k_in_groups[] = {
	"clk_32k_in",
};

static const char * const remote_out_groups[] = {
	"remote_out",
};

static const char * const spdif_in_groups[] = {
	"spdif_in_f6", "spdif_in_f7",
};

static const char * const sw_groups[] = {
	"swclk", "swdio",
};

static const char * const clk25_groups[] = {
	"clk_25",
};

static const char * const cec_a_groups[] = {
	"cec_a",
};

static const char * const cec_b_groups[] = {
	"cec_b",
};

static const char * const clk12_24_groups[] = {
	"clk12_24",
};

static const char * const mclk_0_groups[] = {
	"mclk_0",
};

static const char * const tdm_b_groups[] = {
	"tdm_b_din0", "tdm_b_din1", "tdm_b_din2",
	"tdm_b_sclk", "tdm_b_fs", "tdm_b_dout0", "tdm_b_dout1",
	"tdm_b_dout2", "tdm_b_dout3", "tdm_b_dout4", "tdm_b_dout5",
	"tdm_b_slv_sclk", "tdm_b_slv_fs",
};

static const char * const mclk_vad_groups[] = {
	"mclk_vad",
};

static const char * const tdm_vad_groups[] = {
	"tdm_vad_sclk_a1", "tdm_vad_fs_a2", "tdm_vad_sclk_a5", "tdm_vad_fs_a6",
};

static const char * const tst_out_groups[] = {
	"tst_out0", "tst_out1", "tst_out2", "tst_out3",
	"tst_out4", "tst_out5", "tst_out6", "tst_out7",
	"tst_out8", "tst_out9", "tst_out10", "tst_out11",
};

static const char * const mute_groups[] = {
	"mute_key", "mute_en",
};

static struct meson_pmx_func meson_a1_periphs_functions[] = {
	FUNCTION(gpio_periphs),
	FUNCTION(psram),
	FUNCTION(pwm_a),
	FUNCTION(pwm_b),
	FUNCTION(pwm_c),
	FUNCTION(pwm_d),
	FUNCTION(pwm_e),
	FUNCTION(pwm_f),
	FUNCTION(pwm_a_hiz),
	FUNCTION(pwm_b_hiz),
	FUNCTION(pwm_c_hiz),
	FUNCTION(spif),
	FUNCTION(sdcard),
	FUNCTION(tdm_a),
	FUNCTION(uart_a),
	FUNCTION(uart_b),
	FUNCTION(uart_c),
	FUNCTION(i2c0),
	FUNCTION(i2c1),
	FUNCTION(i2c2),
	FUNCTION(i2c3),
	FUNCTION(spi_a),
	FUNCTION(pdm),
	FUNCTION(gen_clk),
	FUNCTION(remote_input),
	FUNCTION(jtag_a),
	FUNCTION(clk_32k_in),
	FUNCTION(remote_out),
	FUNCTION(spdif_in),
	FUNCTION(sw),
	FUNCTION(clk25),
	FUNCTION(cec_a),
	FUNCTION(cec_b),
	FUNCTION(clk12_24),
	FUNCTION(mclk_0),
	FUNCTION(tdm_b),
	FUNCTION(mclk_vad),
	FUNCTION(tdm_vad),
	FUNCTION(tst_out),
	FUNCTION(mute),
};

static struct meson_bank meson_a1_periphs_banks[] = {
	/* name  first  last  irq  pullen  pull  dir  out  in  ds*/
	BANK_DS("P",  GPIOP_0,  GPIOP_12,  0,  12, 0x3,  0,  0x4,  0,
		0x2,  0,  0x1,  0,  0x0,  0,  0x5,  0),
	BANK_DS("B",  GPIOB_0,    GPIOB_6,   13,  19,  0x13,  0,  0x14,  0,
		0x12,  0,  0x11,  0,  0x10,  0,  0x15,  0),
	BANK_DS("X",  GPIOX_0,    GPIOX_16,  20,  36,  0x23,  0,  0x24,  0,
		0x22,  0,  0x21,  0,  0x20,  0,  0x25,  0),
	BANK_DS("F",  GPIOF_0,    GPIOF_12,  37,  49,  0x33,  0,  0x34,  0,
		0x32,  0,  0x31,  0,  0x30,  0,  0x35,  0),
	BANK_DS("A",  GPIOA_0,    GPIOA_11,  50,  61,  0x43,  0,  0x44,  0,
		0x42,  0,  0x41,  0,  0x40,  0,  0x45,  0),
};

static struct meson_pmx_bank meson_a1_periphs_pmx_banks[] = {
	/*  name	 first	    lask    reg	offset  */
	BANK_PMX("P",    GPIOP_0, GPIOP_12, 0x0, 0),
	BANK_PMX("B",    GPIOB_0, GPIOB_6,  0x2, 0),
	BANK_PMX("X",    GPIOX_0, GPIOX_16, 0x3, 0),
	BANK_PMX("F",    GPIOF_0, GPIOF_12, 0x6, 0),
	BANK_PMX("A",    GPIOA_0, GPIOA_11, 0x8, 0),
};

static struct meson_axg_pmx_data meson_a1_periphs_pmx_banks_data = {
	.pmx_banks	= meson_a1_periphs_pmx_banks,
	.num_pmx_banks	= ARRAY_SIZE(meson_a1_periphs_pmx_banks),
};

static struct meson_pinctrl_data meson_a1_periphs_pinctrl_data = {
	.name		= "periphs-banks",
	.pins		= meson_a1_periphs_pins,
	.groups		= meson_a1_periphs_groups,
	.funcs		= meson_a1_periphs_functions,
	.banks		= meson_a1_periphs_banks,
	.num_pins	= ARRAY_SIZE(meson_a1_periphs_pins),
	.num_groups	= ARRAY_SIZE(meson_a1_periphs_groups),
	.num_funcs	= ARRAY_SIZE(meson_a1_periphs_functions),
	.num_banks	= ARRAY_SIZE(meson_a1_periphs_banks),
	.pmx_ops	= &meson_axg_pmx_ops,
	.pmx_data	= &meson_a1_periphs_pmx_banks_data,
	.parse_dt	= &meson_a1_parse_dt_extra,
};

static const struct of_device_id meson_a1_pinctrl_dt_match[] = {
	{
		.compatible = "amlogic,meson-a1-periphs-pinctrl",
		.data = &meson_a1_periphs_pinctrl_data,
	},
	{ },
};
MODULE_DEVICE_TABLE(of, meson_a1_pinctrl_dt_match);

static struct platform_driver meson_a1_pinctrl_driver = {
	.probe  = meson_pinctrl_probe,
	.driver = {
		.name	= "meson-a1-pinctrl",
		.of_match_table = meson_a1_pinctrl_dt_match,
	},
};

module_platform_driver(meson_a1_pinctrl_driver);
MODULE_DESCRIPTION("Amlogic Meson A1 SoC pinctrl driver");
MODULE_LICENSE("Dual BSD/GPL");
