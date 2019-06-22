/*
 * Pin controller and GPIO driver for Amlogic Meson8 and Meson8m2.
 *
 * Copyright (C) 2014 Beniamino Galvani <b.galvani@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <dt-bindings/gpio/meson8-gpio.h>
#include "pinctrl-meson.h"
#include "pinctrl-meson8-pmx.h"

static const struct pinctrl_pin_desc meson8_cbus_pins[] = {
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
	MESON_PIN(GPIOX_20),
	MESON_PIN(GPIOX_21),
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
	MESON_PIN(GPIODV_0),
	MESON_PIN(GPIODV_1),
	MESON_PIN(GPIODV_2),
	MESON_PIN(GPIODV_3),
	MESON_PIN(GPIODV_4),
	MESON_PIN(GPIODV_5),
	MESON_PIN(GPIODV_6),
	MESON_PIN(GPIODV_7),
	MESON_PIN(GPIODV_8),
	MESON_PIN(GPIODV_9),
	MESON_PIN(GPIODV_10),
	MESON_PIN(GPIODV_11),
	MESON_PIN(GPIODV_12),
	MESON_PIN(GPIODV_13),
	MESON_PIN(GPIODV_14),
	MESON_PIN(GPIODV_15),
	MESON_PIN(GPIODV_16),
	MESON_PIN(GPIODV_17),
	MESON_PIN(GPIODV_18),
	MESON_PIN(GPIODV_19),
	MESON_PIN(GPIODV_20),
	MESON_PIN(GPIODV_21),
	MESON_PIN(GPIODV_22),
	MESON_PIN(GPIODV_23),
	MESON_PIN(GPIODV_24),
	MESON_PIN(GPIODV_25),
	MESON_PIN(GPIODV_26),
	MESON_PIN(GPIODV_27),
	MESON_PIN(GPIODV_28),
	MESON_PIN(GPIODV_29),
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
	MESON_PIN(GPIOZ_14),
	MESON_PIN(CARD_0),
	MESON_PIN(CARD_1),
	MESON_PIN(CARD_2),
	MESON_PIN(CARD_3),
	MESON_PIN(CARD_4),
	MESON_PIN(CARD_5),
	MESON_PIN(CARD_6),
	MESON_PIN(BOOT_0),
	MESON_PIN(BOOT_1),
	MESON_PIN(BOOT_2),
	MESON_PIN(BOOT_3),
	MESON_PIN(BOOT_4),
	MESON_PIN(BOOT_5),
	MESON_PIN(BOOT_6),
	MESON_PIN(BOOT_7),
	MESON_PIN(BOOT_8),
	MESON_PIN(BOOT_9),
	MESON_PIN(BOOT_10),
	MESON_PIN(BOOT_11),
	MESON_PIN(BOOT_12),
	MESON_PIN(BOOT_13),
	MESON_PIN(BOOT_14),
	MESON_PIN(BOOT_15),
	MESON_PIN(BOOT_16),
	MESON_PIN(BOOT_17),
	MESON_PIN(BOOT_18),
};

static const struct pinctrl_pin_desc meson8_aobus_pins[] = {
	MESON_PIN(GPIOAO_0),
	MESON_PIN(GPIOAO_1),
	MESON_PIN(GPIOAO_2),
	MESON_PIN(GPIOAO_3),
	MESON_PIN(GPIOAO_4),
	MESON_PIN(GPIOAO_5),
	MESON_PIN(GPIOAO_6),
	MESON_PIN(GPIOAO_7),
	MESON_PIN(GPIOAO_8),
	MESON_PIN(GPIOAO_9),
	MESON_PIN(GPIOAO_10),
	MESON_PIN(GPIOAO_11),
	MESON_PIN(GPIOAO_12),
	MESON_PIN(GPIOAO_13),
	MESON_PIN(GPIO_BSD_EN),
	MESON_PIN(GPIO_TEST_N),
};

/* bank X */
static const unsigned int sd_d0_a_pins[]	= { GPIOX_0 };
static const unsigned int sd_d1_a_pins[]	= { GPIOX_1 };
static const unsigned int sd_d2_a_pins[]	= { GPIOX_2 };
static const unsigned int sd_d3_a_pins[]	= { GPIOX_3 };
static const unsigned int sd_clk_a_pins[]	= { GPIOX_8 };
static const unsigned int sd_cmd_a_pins[]	= { GPIOX_9 };

static const unsigned int sdxc_d0_a_pins[]	= { GPIOX_0 };
static const unsigned int sdxc_d13_a_pins[]	= { GPIOX_1, GPIOX_2, GPIOX_3 };
static const unsigned int sdxc_d47_a_pins[]	= { GPIOX_4, GPIOX_5, GPIOX_6,
						    GPIOX_7 };
static const unsigned int sdxc_clk_a_pins[]	= { GPIOX_8 };
static const unsigned int sdxc_cmd_a_pins[]	= { GPIOX_9 };

static const unsigned int pcm_out_a_pins[]	= { GPIOX_4 };
static const unsigned int pcm_in_a_pins[]	= { GPIOX_5 };
static const unsigned int pcm_fs_a_pins[]	= { GPIOX_6 };
static const unsigned int pcm_clk_a_pins[]	= { GPIOX_7 };

static const unsigned int uart_tx_a0_pins[]	= { GPIOX_4 };
static const unsigned int uart_rx_a0_pins[]	= { GPIOX_5 };
static const unsigned int uart_cts_a0_pins[]	= { GPIOX_6 };
static const unsigned int uart_rts_a0_pins[]	= { GPIOX_7 };

static const unsigned int uart_tx_a1_pins[]	= { GPIOX_12 };
static const unsigned int uart_rx_a1_pins[]	= { GPIOX_13 };
static const unsigned int uart_cts_a1_pins[]	= { GPIOX_14 };
static const unsigned int uart_rts_a1_pins[]	= { GPIOX_15 };

static const unsigned int uart_tx_b0_pins[]	= { GPIOX_16 };
static const unsigned int uart_rx_b0_pins[]	= { GPIOX_17 };
static const unsigned int uart_cts_b0_pins[]	= { GPIOX_18 };
static const unsigned int uart_rts_b0_pins[]	= { GPIOX_19 };

static const unsigned int iso7816_det_pins[]	= { GPIOX_16 };
static const unsigned int iso7816_reset_pins[]	= { GPIOX_17 };
static const unsigned int iso7816_clk_pins[]	= { GPIOX_18 };
static const unsigned int iso7816_data_pins[]	= { GPIOX_19 };

static const unsigned int i2c_sda_d0_pins[]	= { GPIOX_16 };
static const unsigned int i2c_sck_d0_pins[]	= { GPIOX_17 };

static const unsigned int xtal_32k_out_pins[]	= { GPIOX_10 };
static const unsigned int xtal_24m_out_pins[]	= { GPIOX_11 };

static const unsigned int pwm_e_pins[]		= { GPIOX_10 };
static const unsigned int pwm_b_x_pins[]	= { GPIOX_11 };

/* bank Y */
static const unsigned int uart_tx_c_pins[]	= { GPIOY_0 };
static const unsigned int uart_rx_c_pins[]	= { GPIOY_1 };
static const unsigned int uart_cts_c_pins[]	= { GPIOY_2 };
static const unsigned int uart_rts_c_pins[]	= { GPIOY_3 };

static const unsigned int pcm_out_b_pins[]	= { GPIOY_4 };
static const unsigned int pcm_in_b_pins[]	= { GPIOY_5 };
static const unsigned int pcm_fs_b_pins[]	= { GPIOY_6 };
static const unsigned int pcm_clk_b_pins[]	= { GPIOY_7 };

static const unsigned int i2c_sda_c0_pins[]	= { GPIOY_0 };
static const unsigned int i2c_sck_c0_pins[]	= { GPIOY_1 };

static const unsigned int pwm_a_y_pins[]	= { GPIOY_16 };

static const unsigned int i2s_out_ch45_pins[]	= { GPIOY_0 };
static const unsigned int i2s_out_ch23_pins[]	= { GPIOY_1 };
static const unsigned int i2s_out_ch01_pins[]	= { GPIOY_4 };
static const unsigned int i2s_in_ch01_pins[]	= { GPIOY_5 };
static const unsigned int i2s_lr_clk_in_pins[]	= { GPIOY_6 };
static const unsigned int i2s_ao_clk_in_pins[]	= { GPIOY_7 };
static const unsigned int i2s_am_clk_pins[]	= { GPIOY_8 };
static const unsigned int i2s_out_ch78_pins[]	= { GPIOY_9 };

static const unsigned int spdif_in_pins[]	= { GPIOY_2 };
static const unsigned int spdif_out_pins[]	= { GPIOY_3 };

/* bank DV */
static const unsigned int dvin_rgb_pins[] = {
	GPIODV_0, GPIODV_1, GPIODV_2, GPIODV_3, GPIODV_4, GPIODV_5,
	GPIODV_6, GPIODV_7, GPIODV_8, GPIODV_9, GPIODV_10, GPIODV_11,
	GPIODV_12, GPIODV_13, GPIODV_14, GPIODV_15, GPIODV_16, GPIODV_17,
	GPIODV_18, GPIODV_19, GPIODV_20, GPIODV_21, GPIODV_22, GPIODV_23
};
static const unsigned int dvin_vs_pins[]	= { GPIODV_24 };
static const unsigned int dvin_hs_pins[]	= { GPIODV_25 };
static const unsigned int dvin_clk_pins[]	= { GPIODV_26 };
static const unsigned int dvin_de_pins[]	= { GPIODV_27 };

static const unsigned int enc_0_pins[]		= { GPIODV_0 };
static const unsigned int enc_1_pins[]		= { GPIODV_1 };
static const unsigned int enc_2_pins[]		= { GPIODV_2 };
static const unsigned int enc_3_pins[]		= { GPIODV_3 };
static const unsigned int enc_4_pins[]		= { GPIODV_4 };
static const unsigned int enc_5_pins[]		= { GPIODV_5 };
static const unsigned int enc_6_pins[]		= { GPIODV_6 };
static const unsigned int enc_7_pins[]		= { GPIODV_7 };
static const unsigned int enc_8_pins[]		= { GPIODV_8 };
static const unsigned int enc_9_pins[]		= { GPIODV_9 };
static const unsigned int enc_10_pins[]		= { GPIODV_10 };
static const unsigned int enc_11_pins[]		= { GPIODV_11 };
static const unsigned int enc_12_pins[]		= { GPIODV_12 };
static const unsigned int enc_13_pins[]		= { GPIODV_13 };
static const unsigned int enc_14_pins[]		= { GPIODV_14 };
static const unsigned int enc_15_pins[]		= { GPIODV_15 };
static const unsigned int enc_16_pins[]		= { GPIODV_16 };
static const unsigned int enc_17_pins[]		= { GPIODV_17 };

static const unsigned int uart_tx_b1_pins[]	= { GPIODV_24 };
static const unsigned int uart_rx_b1_pins[]	= { GPIODV_25 };
static const unsigned int uart_cts_b1_pins[]	= { GPIODV_26 };
static const unsigned int uart_rts_b1_pins[]	= { GPIODV_27 };

static const unsigned int vga_vs_pins[]		= { GPIODV_24 };
static const unsigned int vga_hs_pins[]		= { GPIODV_25 };

static const unsigned int pwm_c_dv9_pins[]	= { GPIODV_9 };
static const unsigned int pwm_c_dv29_pins[]	= { GPIODV_29 };
static const unsigned int pwm_d_pins[]		= { GPIODV_28 };

/* bank H */
static const unsigned int hdmi_hpd_pins[]	= { GPIOH_0 };
static const unsigned int hdmi_sda_pins[]	= { GPIOH_1 };
static const unsigned int hdmi_scl_pins[]	= { GPIOH_2 };
static const unsigned int hdmi_cec_pins[]	= { GPIOH_3 };

static const unsigned int spi_ss0_0_pins[]	= { GPIOH_3 };
static const unsigned int spi_miso_0_pins[]	= { GPIOH_4 };
static const unsigned int spi_mosi_0_pins[]	= { GPIOH_5 };
static const unsigned int spi_sclk_0_pins[]	= { GPIOH_6 };

static const unsigned int i2c_sda_d1_pins[]	= { GPIOH_7 };
static const unsigned int i2c_sck_d1_pins[]	= { GPIOH_8 };

/* bank Z */
static const unsigned int spi_ss0_1_pins[]	= { GPIOZ_9 };
static const unsigned int spi_ss1_1_pins[]	= { GPIOZ_10 };
static const unsigned int spi_sclk_1_pins[]	= { GPIOZ_11 };
static const unsigned int spi_mosi_1_pins[]	= { GPIOZ_12 };
static const unsigned int spi_miso_1_pins[]	= { GPIOZ_13 };
static const unsigned int spi_ss2_1_pins[]	= { GPIOZ_14 };

static const unsigned int eth_txd3_pins[]	= { GPIOZ_0 };
static const unsigned int eth_txd2_pins[]	= { GPIOZ_1 };
static const unsigned int eth_rxd3_pins[]	= { GPIOZ_2 };
static const unsigned int eth_rxd2_pins[]	= { GPIOZ_3 };
static const unsigned int eth_tx_clk_50m_pins[]	= { GPIOZ_4 };
static const unsigned int eth_tx_en_pins[]	= { GPIOZ_5 };
static const unsigned int eth_txd1_pins[]	= { GPIOZ_6 };
static const unsigned int eth_txd0_pins[]	= { GPIOZ_7 };
static const unsigned int eth_rx_clk_in_pins[]	= { GPIOZ_8 };
static const unsigned int eth_rx_dv_pins[]	= { GPIOZ_9 };
static const unsigned int eth_rxd1_pins[]	= { GPIOZ_10 };
static const unsigned int eth_rxd0_pins[]	= { GPIOZ_11 };
static const unsigned int eth_mdio_pins[]	= { GPIOZ_12 };
static const unsigned int eth_mdc_pins[]	= { GPIOZ_13 };

static const unsigned int i2c_sda_a0_pins[]	= { GPIOZ_0 };
static const unsigned int i2c_sck_a0_pins[]	= { GPIOZ_1 };

static const unsigned int i2c_sda_b_pins[]	= { GPIOZ_2 };
static const unsigned int i2c_sck_b_pins[]	= { GPIOZ_3 };

static const unsigned int i2c_sda_c1_pins[]	= { GPIOZ_4 };
static const unsigned int i2c_sck_c1_pins[]	= { GPIOZ_5 };

static const unsigned int i2c_sda_a1_pins[]	= { GPIOZ_0 };
static const unsigned int i2c_sck_a1_pins[]	= { GPIOZ_1 };

static const unsigned int i2c_sda_a2_pins[]	= { GPIOZ_0 };
static const unsigned int i2c_sck_a2_pins[]	= { GPIOZ_1 };

static const unsigned int pwm_a_z0_pins[]	= { GPIOZ_0 };
static const unsigned int pwm_a_z7_pins[]	= { GPIOZ_7 };
static const unsigned int pwm_b_z_pins[]	= { GPIOZ_1 };
static const unsigned int pwm_c_z_pins[]	= { GPIOZ_8 };

/* bank BOOT */
static const unsigned int sd_d0_c_pins[]	= { BOOT_0 };
static const unsigned int sd_d1_c_pins[]	= { BOOT_1 };
static const unsigned int sd_d2_c_pins[]	= { BOOT_2 };
static const unsigned int sd_d3_c_pins[]	= { BOOT_3 };
static const unsigned int sd_cmd_c_pins[]	= { BOOT_16 };
static const unsigned int sd_clk_c_pins[]	= { BOOT_17 };

static const unsigned int sdxc_d0_c_pins[]	= { BOOT_0};
static const unsigned int sdxc_d13_c_pins[]	= { BOOT_1, BOOT_2, BOOT_3 };
static const unsigned int sdxc_d47_c_pins[]	= { BOOT_4, BOOT_5, BOOT_6,
						    BOOT_7 };
static const unsigned int sdxc_cmd_c_pins[]	= { BOOT_16 };
static const unsigned int sdxc_clk_c_pins[]	= { BOOT_17 };

static const unsigned int nand_io_pins[] = {
	BOOT_0, BOOT_1, BOOT_2, BOOT_3, BOOT_4, BOOT_5, BOOT_6, BOOT_7
};
static const unsigned int nand_io_ce0_pins[]	= { BOOT_8 };
static const unsigned int nand_io_ce1_pins[]	= { BOOT_9 };
static const unsigned int nand_io_rb0_pins[]	= { BOOT_10 };
static const unsigned int nand_ale_pins[]	= { BOOT_11 };
static const unsigned int nand_cle_pins[]	= { BOOT_12 };
static const unsigned int nand_wen_clk_pins[]	= { BOOT_13 };
static const unsigned int nand_ren_clk_pins[]	= { BOOT_14 };
static const unsigned int nand_dqs_pins[]	= { BOOT_15 };
static const unsigned int nand_ce2_pins[]	= { BOOT_16 };
static const unsigned int nand_ce3_pins[]	= { BOOT_17 };

static const unsigned int nor_d_pins[]		= { BOOT_11 };
static const unsigned int nor_q_pins[]		= { BOOT_12 };
static const unsigned int nor_c_pins[]		= { BOOT_13 };
static const unsigned int nor_cs_pins[]		= { BOOT_18 };

/* bank CARD */
static const unsigned int sd_d1_b_pins[]	= { CARD_0 };
static const unsigned int sd_d0_b_pins[]	= { CARD_1 };
static const unsigned int sd_clk_b_pins[]	= { CARD_2 };
static const unsigned int sd_cmd_b_pins[]	= { CARD_3 };
static const unsigned int sd_d3_b_pins[]	= { CARD_4 };
static const unsigned int sd_d2_b_pins[]	= { CARD_5 };

static const unsigned int sdxc_d13_b_pins[]	= { CARD_0, CARD_4, CARD_5 };
static const unsigned int sdxc_d0_b_pins[]	= { CARD_1 };
static const unsigned int sdxc_clk_b_pins[]	= { CARD_2 };
static const unsigned int sdxc_cmd_b_pins[]	= { CARD_3 };

/* bank AO */
static const unsigned int uart_tx_ao_a_pins[]	= { GPIOAO_0 };
static const unsigned int uart_rx_ao_a_pins[]	= { GPIOAO_1 };
static const unsigned int uart_cts_ao_a_pins[]	= { GPIOAO_2 };
static const unsigned int uart_rts_ao_a_pins[]	= { GPIOAO_3 };

static const unsigned int remote_input_pins[]	= { GPIOAO_7 };
static const unsigned int remote_output_ao_pins[] = { GPIOAO_13 };

static const unsigned int i2c_slave_sck_ao_pins[] = { GPIOAO_4 };
static const unsigned int i2c_slave_sda_ao_pins[] = { GPIOAO_5 };

static const unsigned int uart_tx_ao_b0_pins[]	= { GPIOAO_0 };
static const unsigned int uart_rx_ao_b0_pins[]	= { GPIOAO_1 };

static const unsigned int uart_tx_ao_b1_pins[]	= { GPIOAO_4 };
static const unsigned int uart_rx_ao_b1_pins[]	= { GPIOAO_5 };

static const unsigned int i2c_mst_sck_ao_pins[]	= { GPIOAO_4 };
static const unsigned int i2c_mst_sda_ao_pins[]	= { GPIOAO_5 };

static const unsigned int pwm_f_ao_pins[]	= { GPIO_TEST_N };

static const unsigned int i2s_am_clk_out_ao_pins[] = { GPIOAO_8 };
static const unsigned int i2s_ao_clk_out_ao_pins[] = { GPIOAO_9 };
static const unsigned int i2s_lr_clk_out_ao_pins[] = { GPIOAO_10 };
static const unsigned int i2s_out_ch01_ao_pins[] = { GPIOAO_11 };

static const unsigned int hdmi_cec_ao_pins[]	= { GPIOAO_12 };

static struct meson_pmx_group meson8_cbus_groups[] = {
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
	GPIO_GROUP(GPIOX_20),
	GPIO_GROUP(GPIOX_21),
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
	GPIO_GROUP(GPIODV_0),
	GPIO_GROUP(GPIODV_1),
	GPIO_GROUP(GPIODV_2),
	GPIO_GROUP(GPIODV_3),
	GPIO_GROUP(GPIODV_4),
	GPIO_GROUP(GPIODV_5),
	GPIO_GROUP(GPIODV_6),
	GPIO_GROUP(GPIODV_7),
	GPIO_GROUP(GPIODV_8),
	GPIO_GROUP(GPIODV_9),
	GPIO_GROUP(GPIODV_10),
	GPIO_GROUP(GPIODV_11),
	GPIO_GROUP(GPIODV_12),
	GPIO_GROUP(GPIODV_13),
	GPIO_GROUP(GPIODV_14),
	GPIO_GROUP(GPIODV_15),
	GPIO_GROUP(GPIODV_16),
	GPIO_GROUP(GPIODV_17),
	GPIO_GROUP(GPIODV_18),
	GPIO_GROUP(GPIODV_19),
	GPIO_GROUP(GPIODV_20),
	GPIO_GROUP(GPIODV_21),
	GPIO_GROUP(GPIODV_22),
	GPIO_GROUP(GPIODV_23),
	GPIO_GROUP(GPIODV_24),
	GPIO_GROUP(GPIODV_25),
	GPIO_GROUP(GPIODV_26),
	GPIO_GROUP(GPIODV_27),
	GPIO_GROUP(GPIODV_28),
	GPIO_GROUP(GPIODV_29),
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
	GPIO_GROUP(GPIOZ_14),

	/* bank X */
	GROUP(sd_d0_a,		8,	5),
	GROUP(sd_d1_a,		8,	4),
	GROUP(sd_d2_a,		8,	3),
	GROUP(sd_d3_a,		8,	2),
	GROUP(sd_clk_a,		8,	1),
	GROUP(sd_cmd_a,		8,	0),

	GROUP(sdxc_d0_a,	5,	14),
	GROUP(sdxc_d13_a,	5,	13),
	GROUP(sdxc_d47_a,	5,	12),
	GROUP(sdxc_clk_a,	5,	11),
	GROUP(sdxc_cmd_a,	5,	10),

	GROUP(pcm_out_a,	3,	30),
	GROUP(pcm_in_a,		3,	29),
	GROUP(pcm_fs_a,		3,	28),
	GROUP(pcm_clk_a,	3,	27),

	GROUP(uart_tx_a0,	4,	17),
	GROUP(uart_rx_a0,	4,	16),
	GROUP(uart_cts_a0,	4,	15),
	GROUP(uart_rts_a0,	4,	14),

	GROUP(uart_tx_a1,	4,	13),
	GROUP(uart_rx_a1,	4,	12),
	GROUP(uart_cts_a1,	4,	11),
	GROUP(uart_rts_a1,	4,	10),

	GROUP(uart_tx_b0,	4,	9),
	GROUP(uart_rx_b0,	4,	8),
	GROUP(uart_cts_b0,	4,	7),
	GROUP(uart_rts_b0,	4,	6),

	GROUP(iso7816_det,	4,	21),
	GROUP(iso7816_reset,	4,	20),
	GROUP(iso7816_clk,	4,	19),
	GROUP(iso7816_data,	4,	18),

	GROUP(i2c_sda_d0,	4,	5),
	GROUP(i2c_sck_d0,	4,	4),

	GROUP(xtal_32k_out,	3,	22),
	GROUP(xtal_24m_out,	3,	23),

	GROUP(pwm_e,		9,	19),
	GROUP(pwm_b_x,		2,	3),

	/* bank Y */
	GROUP(uart_tx_c,	1,	19),
	GROUP(uart_rx_c,	1,	18),
	GROUP(uart_cts_c,	1,	17),
	GROUP(uart_rts_c,	1,	16),

	GROUP(pcm_out_b,	4,	25),
	GROUP(pcm_in_b,		4,	24),
	GROUP(pcm_fs_b,		4,	23),
	GROUP(pcm_clk_b,	4,	22),

	GROUP(i2c_sda_c0,	1,	15),
	GROUP(i2c_sck_c0,	1,	14),

	GROUP(pwm_a_y,		9,	14),

	GROUP(i2s_out_ch45,	1,	10),
	GROUP(i2s_out_ch23,	1,	19),
	GROUP(i2s_out_ch01,	1,	6),
	GROUP(i2s_in_ch01,	1,	5),
	GROUP(i2s_lr_clk_in,	1,	4),
	GROUP(i2s_ao_clk_in,	1,	2),
	GROUP(i2s_am_clk,	1,	0),
	GROUP(i2s_out_ch78,	1,	11),

	GROUP(spdif_in,		1,	8),
	GROUP(spdif_out,	1,	7),

	/* bank DV */
	GROUP(dvin_rgb,		0,	6),
	GROUP(dvin_vs,		0,	9),
	GROUP(dvin_hs,		0,	8),
	GROUP(dvin_clk,		0,	7),
	GROUP(dvin_de,		0,	10),

	GROUP(enc_0,		7,	0),
	GROUP(enc_1,		7,	1),
	GROUP(enc_2,		7,	2),
	GROUP(enc_3,		7,	3),
	GROUP(enc_4,		7,	4),
	GROUP(enc_5,		7,	5),
	GROUP(enc_6,		7,	6),
	GROUP(enc_7,		7,	7),
	GROUP(enc_8,		7,	8),
	GROUP(enc_9,		7,	9),
	GROUP(enc_10,		7,	10),
	GROUP(enc_11,		7,	11),
	GROUP(enc_12,		7,	12),
	GROUP(enc_13,		7,	13),
	GROUP(enc_14,		7,	14),
	GROUP(enc_15,		7,	15),
	GROUP(enc_16,		7,	16),
	GROUP(enc_17,		7,	17),

	GROUP(uart_tx_b1,	6,	23),
	GROUP(uart_rx_b1,	6,	22),
	GROUP(uart_cts_b1,	6,	21),
	GROUP(uart_rts_b1,	6,	20),

	GROUP(vga_vs,		0,	21),
	GROUP(vga_hs,		0,	20),

	GROUP(pwm_c_dv9,	3,	24),
	GROUP(pwm_c_dv29,	3,	25),
	GROUP(pwm_d,		3,	26),

	/* bank H */
	GROUP(hdmi_hpd,		1,	26),
	GROUP(hdmi_sda,		1,	25),
	GROUP(hdmi_scl,		1,	24),
	GROUP(hdmi_cec,		1,	23),

	GROUP(spi_ss0_0,	9,	13),
	GROUP(spi_miso_0,	9,	12),
	GROUP(spi_mosi_0,	9,	11),
	GROUP(spi_sclk_0,	9,	10),

	GROUP(i2c_sda_d1,	4,	3),
	GROUP(i2c_sck_d1,	4,	2),

	/* bank Z */
	GROUP(spi_ss0_1,	8,	16),
	GROUP(spi_ss1_1,	8,	12),
	GROUP(spi_sclk_1,	8,	15),
	GROUP(spi_mosi_1,	8,	14),
	GROUP(spi_miso_1,	8,	13),
	GROUP(spi_ss2_1,	8,	17),

	GROUP(eth_tx_clk_50m,	6,	15),
	GROUP(eth_tx_en,	6,	14),
	GROUP(eth_txd1,		6,	13),
	GROUP(eth_txd0,		6,	12),
	GROUP(eth_rx_clk_in,	6,	10),
	GROUP(eth_rx_dv,	6,	11),
	GROUP(eth_rxd1,		6,	8),
	GROUP(eth_rxd0,		6,	7),
	GROUP(eth_mdio,		6,	6),
	GROUP(eth_mdc,		6,	5),

	/* NOTE: the following four groups are only available on Meson8m2: */
	GROUP(eth_rxd2,		6,	3),
	GROUP(eth_rxd3,		6,	2),
	GROUP(eth_txd2,		6,	1),
	GROUP(eth_txd3,		6,	0),

	GROUP(i2c_sda_a0,	5,	31),
	GROUP(i2c_sck_a0,	5,	30),

	GROUP(i2c_sda_b,	5,	27),
	GROUP(i2c_sck_b,	5,	26),

	GROUP(i2c_sda_c1,	5,	25),
	GROUP(i2c_sck_c1,	5,	24),

	GROUP(i2c_sda_a1,	5,	9),
	GROUP(i2c_sck_a1,	5,	8),

	GROUP(i2c_sda_a2,	5,	7),
	GROUP(i2c_sck_a2,	5,	6),

	GROUP(pwm_a_z0,		9,	16),
	GROUP(pwm_a_z7,		2,	0),
	GROUP(pwm_b_z,		9,	15),
	GROUP(pwm_c_z,		2,	1),

	/* bank BOOT */
	GROUP(sd_d0_c,		6,	29),
	GROUP(sd_d1_c,		6,	28),
	GROUP(sd_d2_c,		6,	27),
	GROUP(sd_d3_c,		6,	26),
	GROUP(sd_cmd_c,		6,	25),
	GROUP(sd_clk_c,		6,	24),

	GROUP(sdxc_d0_c,	4,	30),
	GROUP(sdxc_d13_c,	4,	29),
	GROUP(sdxc_d47_c,	4,	28),
	GROUP(sdxc_cmd_c,	4,	27),
	GROUP(sdxc_clk_c,	4,	26),

	GROUP(nand_io,		2,	26),
	GROUP(nand_io_ce0,	2,	25),
	GROUP(nand_io_ce1,	2,	24),
	GROUP(nand_io_rb0,	2,	17),
	GROUP(nand_ale,		2,	21),
	GROUP(nand_cle,		2,	20),
	GROUP(nand_wen_clk,	2,	19),
	GROUP(nand_ren_clk,	2,	18),
	GROUP(nand_dqs,		2,	27),
	GROUP(nand_ce2,		2,	23),
	GROUP(nand_ce3,		2,	22),

	GROUP(nor_d,		5,	1),
	GROUP(nor_q,		5,	3),
	GROUP(nor_c,		5,	2),
	GROUP(nor_cs,		5,	0),

	/* bank CARD */
	GROUP(sd_d1_b,		2,	14),
	GROUP(sd_d0_b,		2,	15),
	GROUP(sd_clk_b,		2,	11),
	GROUP(sd_cmd_b,		2,	10),
	GROUP(sd_d3_b,		2,	12),
	GROUP(sd_d2_b,		2,	13),

	GROUP(sdxc_d13_b,	2,	6),
	GROUP(sdxc_d0_b,	2,	7),
	GROUP(sdxc_clk_b,	2,	5),
	GROUP(sdxc_cmd_b,	2,	4),
};

static struct meson_pmx_group meson8_aobus_groups[] = {
	GPIO_GROUP(GPIOAO_0),
	GPIO_GROUP(GPIOAO_1),
	GPIO_GROUP(GPIOAO_2),
	GPIO_GROUP(GPIOAO_3),
	GPIO_GROUP(GPIOAO_4),
	GPIO_GROUP(GPIOAO_5),
	GPIO_GROUP(GPIOAO_6),
	GPIO_GROUP(GPIOAO_7),
	GPIO_GROUP(GPIOAO_8),
	GPIO_GROUP(GPIOAO_9),
	GPIO_GROUP(GPIOAO_10),
	GPIO_GROUP(GPIOAO_11),
	GPIO_GROUP(GPIOAO_12),
	GPIO_GROUP(GPIOAO_13),
	GPIO_GROUP(GPIO_BSD_EN),
	GPIO_GROUP(GPIO_TEST_N),

	/* bank AO */
	GROUP(uart_tx_ao_a,		0,	12),
	GROUP(uart_rx_ao_a,		0,	11),
	GROUP(uart_cts_ao_a,		0,	10),
	GROUP(uart_rts_ao_a,		0,	9),

	GROUP(remote_input,		0,	0),
	GROUP(remote_output_ao,		0,	31),

	GROUP(i2c_slave_sck_ao,		0,	2),
	GROUP(i2c_slave_sda_ao,		0,	1),

	GROUP(uart_tx_ao_b0,		0,	26),
	GROUP(uart_rx_ao_b0,		0,	25),

	GROUP(uart_tx_ao_b1,		0,	24),
	GROUP(uart_rx_ao_b1,		0,	23),

	GROUP(i2c_mst_sck_ao,		0,	6),
	GROUP(i2c_mst_sda_ao,		0,	5),

	GROUP(pwm_f_ao,			0,	19),

	GROUP(i2s_am_clk_out_ao,	0,	30),
	GROUP(i2s_ao_clk_out_ao,	0,	29),
	GROUP(i2s_lr_clk_out_ao,	0,	28),
	GROUP(i2s_out_ch01_ao,		0,	27),

	GROUP(hdmi_cec_ao,		0,	17),
};

static const char * const gpio_groups[] = {
	"GPIOX_0", "GPIOX_1", "GPIOX_2", "GPIOX_3", "GPIOX_4",
	"GPIOX_5", "GPIOX_6", "GPIOX_7", "GPIOX_8", "GPIOX_9",
	"GPIOX_10", "GPIOX_11", "GPIOX_12", "GPIOX_13", "GPIOX_14",
	"GPIOX_15", "GPIOX_16", "GPIOX_17", "GPIOX_18", "GPIOX_19",
	"GPIOX_20", "GPIOX_21",

	"GPIOY_0", "GPIOY_1", "GPIOY_2", "GPIOY_3", "GPIOY_4",
	"GPIOY_5", "GPIOY_6", "GPIOY_7", "GPIOY_8", "GPIOY_9",
	"GPIOY_10", "GPIOY_11", "GPIOY_12", "GPIOY_13", "GPIOY_14",
	"GPIOY_15", "GPIOY_16",

	"GPIODV_0", "GPIODV_1", "GPIODV_2", "GPIODV_3", "GPIODV_4",
	"GPIODV_5", "GPIODV_6", "GPIODV_7", "GPIODV_8", "GPIODV_9",
	"GPIODV_10", "GPIODV_11", "GPIODV_12", "GPIODV_13", "GPIODV_14",
	"GPIODV_15", "GPIODV_16", "GPIODV_17", "GPIODV_18", "GPIODV_19",
	"GPIODV_20", "GPIODV_21", "GPIODV_22", "GPIODV_23", "GPIODV_24",
	"GPIODV_25", "GPIODV_26", "GPIODV_27", "GPIODV_28", "GPIODV_29",

	"GPIOH_0", "GPIOH_1", "GPIOH_2", "GPIOH_3", "GPIOH_4",
	"GPIOH_5", "GPIOH_6", "GPIOH_7", "GPIOH_8", "GPIOH_9",

	"GPIOZ_0", "GPIOZ_1", "GPIOZ_2", "GPIOZ_3", "GPIOZ_4",
	"GPIOZ_5", "GPIOZ_6", "GPIOZ_7", "GPIOZ_8", "GPIOZ_9",
	"GPIOZ_10", "GPIOZ_11", "GPIOZ_12", "GPIOZ_13", "GPIOZ_14",

	"CARD_0", "CARD_1", "CARD_2", "CARD_3", "CARD_4",
	"CARD_5", "CARD_6",

	"BOOT_0", "BOOT_1", "BOOT_2", "BOOT_3", "BOOT_4",
	"BOOT_5", "BOOT_6", "BOOT_7", "BOOT_8", "BOOT_9",
	"BOOT_10", "BOOT_11", "BOOT_12", "BOOT_13", "BOOT_14",
	"BOOT_15", "BOOT_16", "BOOT_17", "BOOT_18",
};

static const char * const gpio_aobus_groups[] = {
	"GPIOAO_0", "GPIOAO_1", "GPIOAO_2", "GPIOAO_3",
	"GPIOAO_4", "GPIOAO_5", "GPIOAO_6", "GPIOAO_7",
	"GPIOAO_8", "GPIOAO_9", "GPIOAO_10", "GPIOAO_11",
	"GPIOAO_12", "GPIOAO_13", "GPIO_BSD_EN", "GPIO_TEST_N"
};

static const char * const sd_a_groups[] = {
	"sd_d0_a", "sd_d1_a", "sd_d2_a", "sd_d3_a", "sd_clk_a", "sd_cmd_a"
};

static const char * const sdxc_a_groups[] = {
	"sdxc_d0_a", "sdxc_d13_a", "sdxc_d47_a", "sdxc_clk_a", "sdxc_cmd_a"
};

static const char * const pcm_a_groups[] = {
	"pcm_out_a", "pcm_in_a", "pcm_fs_a", "pcm_clk_a"
};

static const char * const uart_a_groups[] = {
	"uart_tx_a0", "uart_rx_a0", "uart_cts_a0", "uart_rts_a0",
	"uart_tx_a1", "uart_rx_a1", "uart_cts_a1", "uart_rts_a1"
};

static const char * const uart_b_groups[] = {
	"uart_tx_b0", "uart_rx_b0", "uart_cts_b0", "uart_rts_b0",
	"uart_tx_b1", "uart_rx_b1", "uart_cts_b1", "uart_rts_b1"
};

static const char * const iso7816_groups[] = {
	"iso7816_det", "iso7816_reset", "iso7816_clk", "iso7816_data"
};

static const char * const i2c_d_groups[] = {
	"i2c_sda_d0", "i2c_sck_d0", "i2c_sda_d1", "i2c_sck_d1"
};

static const char * const xtal_groups[] = {
	"xtal_32k_out", "xtal_24m_out"
};

static const char * const uart_c_groups[] = {
	"uart_tx_c", "uart_rx_c", "uart_cts_c", "uart_rts_c"
};

static const char * const pcm_b_groups[] = {
	"pcm_out_b", "pcm_in_b", "pcm_fs_b", "pcm_clk_b"
};

static const char * const i2c_c_groups[] = {
	"i2c_sda_c0", "i2c_sck_c0", "i2c_sda_c1", "i2c_sck_c1"
};

static const char * const dvin_groups[] = {
	"dvin_rgb", "dvin_vs", "dvin_hs", "dvin_clk", "dvin_de"
};

static const char * const enc_groups[] = {
	"enc_0", "enc_1", "enc_2", "enc_3", "enc_4", "enc_5",
	"enc_6", "enc_7", "enc_8", "enc_9", "enc_10", "enc_11",
	"enc_12", "enc_13", "enc_14", "enc_15", "enc_16", "enc_17"
};

static const char * const vga_groups[] = {
	"vga_vs", "vga_hs"
};

static const char * const hdmi_groups[] = {
	"hdmi_hpd", "hdmi_sda", "hdmi_scl", "hdmi_cec"
};

static const char * const spi_groups[] = {
	"spi_ss0_0", "spi_miso_0", "spi_mosi_0", "spi_sclk_0",
	"spi_ss0_1", "spi_ss1_1", "spi_sclk_1", "spi_mosi_1",
	"spi_miso_1", "spi_ss2_1"
};

static const char * const ethernet_groups[] = {
	"eth_tx_clk_50m", "eth_tx_en", "eth_txd1",
	"eth_txd0", "eth_rx_clk_in", "eth_rx_dv",
	"eth_rxd1", "eth_rxd0", "eth_mdio", "eth_mdc", "eth_rxd2",
	"eth_rxd3", "eth_txd2", "eth_txd3"
};

static const char * const i2c_a_groups[] = {
	"i2c_sda_a0", "i2c_sck_a0", "i2c_sda_a1", "i2c_sck_a1",
	"i2c_sda_a2", "i2c_sck_a2"
};

static const char * const i2c_b_groups[] = {
	"i2c_sda_b", "i2c_sck_b"
};

static const char * const i2s_groups[] = {
	"i2s_out_ch45", "i2s_out_ch23_pins", "i2s_out_ch01_pins",
	"i2s_in_ch01_pins", "i2s_lr_clk_in_pins", "i2s_ao_clk_in_pins",
	"i2s_am_clk_pins", "i2s_out_ch78_pins"
};

static const char * const sd_c_groups[] = {
	"sd_d0_c", "sd_d1_c", "sd_d2_c", "sd_d3_c",
	"sd_cmd_c", "sd_clk_c"
};

static const char * const sdxc_c_groups[] = {
	"sdxc_d0_c", "sdxc_d13_c", "sdxc_d47_c", "sdxc_cmd_c",
	"sdxc_clk_c"
};

static const char * const nand_groups[] = {
	"nand_io", "nand_io_ce0", "nand_io_ce1",
	"nand_io_rb0", "nand_ale", "nand_cle",
	"nand_wen_clk", "nand_ren_clk", "nand_dqs",
	"nand_ce2", "nand_ce3"
};

static const char * const nor_groups[] = {
	"nor_d", "nor_q", "nor_c", "nor_cs"
};

static const char * const pwm_a_groups[] = {
	"pwm_a_y", "pwm_a_z0", "pwm_a_z7"
};

static const char * const pwm_b_groups[] = {
	"pwm_b_x", "pwm_b_z"
};

static const char * const pwm_c_groups[] = {
	"pwm_c_dv9", "pwm_c_dv29", "pwm_c_z"
};

static const char * const pwm_d_groups[] = {
	"pwm_d"
};

static const char * const pwm_e_groups[] = {
	"pwm_e"
};

static const char * const sd_b_groups[] = {
	"sd_d1_b", "sd_d0_b", "sd_clk_b", "sd_cmd_b",
	"sd_d3_b", "sd_d2_b"
};

static const char * const sdxc_b_groups[] = {
	"sdxc_d13_b", "sdxc_d0_b", "sdxc_clk_b", "sdxc_cmd_b"
};

static const char * const spdif_groups[] = {
	"spdif_in", "spdif_out"
};

static const char * const uart_ao_groups[] = {
	"uart_tx_ao_a", "uart_rx_ao_a", "uart_cts_ao_a", "uart_rts_ao_a"
};

static const char * const remote_groups[] = {
	"remote_input", "remote_output_ao"
};

static const char * const i2c_slave_ao_groups[] = {
	"i2c_slave_sck_ao", "i2c_slave_sda_ao"
};

static const char * const uart_ao_b_groups[] = {
	"uart_tx_ao_b0", "uart_rx_ao_b0", "uart_tx_ao_b1", "uart_rx_ao_b1"
};

static const char * const i2c_mst_ao_groups[] = {
	"i2c_mst_sck_ao", "i2c_mst_sda_ao"
};

static const char * const pwm_f_ao_groups[] = {
	"pwm_f_ao"
};

static const char * const i2s_ao_groups[] = {
	"i2s_am_clk_out_ao", "i2s_ao_clk_out_ao", "i2s_lr_clk_out_ao",
	"i2s_out_ch01_ao"
};

static const char * const hdmi_cec_ao_groups[] = {
	"hdmi_cec_ao"
};

static struct meson_pmx_func meson8_cbus_functions[] = {
	FUNCTION(gpio),
	FUNCTION(sd_a),
	FUNCTION(sdxc_a),
	FUNCTION(pcm_a),
	FUNCTION(uart_a),
	FUNCTION(uart_b),
	FUNCTION(iso7816),
	FUNCTION(i2c_d),
	FUNCTION(xtal),
	FUNCTION(uart_c),
	FUNCTION(pcm_b),
	FUNCTION(i2c_c),
	FUNCTION(dvin),
	FUNCTION(enc),
	FUNCTION(vga),
	FUNCTION(hdmi),
	FUNCTION(spi),
	FUNCTION(ethernet),
	FUNCTION(i2c_a),
	FUNCTION(i2c_b),
	FUNCTION(sd_c),
	FUNCTION(sdxc_c),
	FUNCTION(nand),
	FUNCTION(nor),
	FUNCTION(sd_b),
	FUNCTION(sdxc_b),
	FUNCTION(pwm_a),
	FUNCTION(pwm_b),
	FUNCTION(pwm_c),
	FUNCTION(pwm_d),
	FUNCTION(pwm_e),
	FUNCTION(i2s),
	FUNCTION(spdif),
};

static struct meson_pmx_func meson8_aobus_functions[] = {
	FUNCTION(gpio_aobus),
	FUNCTION(uart_ao),
	FUNCTION(remote),
	FUNCTION(i2c_slave_ao),
	FUNCTION(uart_ao_b),
	FUNCTION(i2c_mst_ao),
	FUNCTION(pwm_f_ao),
	FUNCTION(i2s_ao),
	FUNCTION(hdmi_cec_ao),
};

static struct meson_bank meson8_cbus_banks[] = {
	/*   name    first     last         irq       pullen  pull    dir     out     in  */
	BANK("X",    GPIOX_0,  GPIOX_21,    112, 133, 4,  0,  4,  0,  0,  0,  1,  0,  2,  0),
	BANK("Y",    GPIOY_0,  GPIOY_16,    95,  111, 3,  0,  3,  0,  3,  0,  4,  0,  5,  0),
	BANK("DV",   GPIODV_0, GPIODV_29,   65,   94, 0,  0,  0,  0,  7,  0,  8,  0,  9,  0),
	BANK("H",    GPIOH_0,  GPIOH_9,     29,   38, 1, 16,  1, 16,  9, 19, 10, 19, 11, 19),
	BANK("Z",    GPIOZ_0,  GPIOZ_14,    14,   28, 1,  0,  1,  0,  3, 17,  4, 17,  5, 17),
	BANK("CARD", CARD_0,   CARD_6,      58,   64, 2, 20,  2, 20,  0, 22,  1, 22,  2, 22),
	BANK("BOOT", BOOT_0,   BOOT_18,     39,   57, 2,  0,  2,  0,  9,  0, 10,  0, 11,  0),
};

static struct meson_bank meson8_aobus_banks[] = {
	/*   name    first     last         irq    pullen  pull    dir     out     in  */
	BANK("AO",   GPIOAO_0, GPIO_TEST_N, 0, 13, 0, 16,  0,  0,  0,  0,  0, 16,  1,  0),
};

static struct meson_pinctrl_data meson8_cbus_pinctrl_data = {
	.name		= "cbus-banks",
	.pins		= meson8_cbus_pins,
	.groups		= meson8_cbus_groups,
	.funcs		= meson8_cbus_functions,
	.banks		= meson8_cbus_banks,
	.num_pins	= ARRAY_SIZE(meson8_cbus_pins),
	.num_groups	= ARRAY_SIZE(meson8_cbus_groups),
	.num_funcs	= ARRAY_SIZE(meson8_cbus_functions),
	.num_banks	= ARRAY_SIZE(meson8_cbus_banks),
	.pmx_ops	= &meson8_pmx_ops,
};

static struct meson_pinctrl_data meson8_aobus_pinctrl_data = {
	.name		= "ao-bank",
	.pins		= meson8_aobus_pins,
	.groups		= meson8_aobus_groups,
	.funcs		= meson8_aobus_functions,
	.banks		= meson8_aobus_banks,
	.num_pins	= ARRAY_SIZE(meson8_aobus_pins),
	.num_groups	= ARRAY_SIZE(meson8_aobus_groups),
	.num_funcs	= ARRAY_SIZE(meson8_aobus_functions),
	.num_banks	= ARRAY_SIZE(meson8_aobus_banks),
	.pmx_ops	= &meson8_pmx_ops,
};

static const struct of_device_id meson8_pinctrl_dt_match[] = {
	{
		.compatible = "amlogic,meson8-cbus-pinctrl",
		.data = &meson8_cbus_pinctrl_data,
	},
	{
		.compatible = "amlogic,meson8-aobus-pinctrl",
		.data = &meson8_aobus_pinctrl_data,
	},
	{
		.compatible = "amlogic,meson8m2-cbus-pinctrl",
		.data = &meson8_cbus_pinctrl_data,
	},
	{
		.compatible = "amlogic,meson8m2-aobus-pinctrl",
		.data = &meson8_aobus_pinctrl_data,
	},
	{ },
};

static struct platform_driver meson8_pinctrl_driver = {
	.probe		= meson_pinctrl_probe,
	.driver = {
		.name	= "meson8-pinctrl",
		.of_match_table = meson8_pinctrl_dt_match,
	},
};
builtin_platform_driver(meson8_pinctrl_driver);
