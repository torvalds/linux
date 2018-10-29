/*
 * Pin controller and GPIO driver for Amlogic Meson8b.
 *
 * Copyright (C) 2015 Endless Mobile, Inc.
 * Author: Carlo Caione <carlo@endlessm.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <dt-bindings/gpio/meson8b-gpio.h>
#include "pinctrl-meson.h"
#include "pinctrl-meson8-pmx.h"

static const struct pinctrl_pin_desc meson8b_cbus_pins[] = {
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
	MESON_PIN(GPIOX_16),
	MESON_PIN(GPIOX_17),
	MESON_PIN(GPIOX_18),
	MESON_PIN(GPIOX_19),
	MESON_PIN(GPIOX_20),
	MESON_PIN(GPIOX_21),

	MESON_PIN(GPIOY_0),
	MESON_PIN(GPIOY_1),
	MESON_PIN(GPIOY_3),
	MESON_PIN(GPIOY_6),
	MESON_PIN(GPIOY_7),
	MESON_PIN(GPIOY_8),
	MESON_PIN(GPIOY_9),
	MESON_PIN(GPIOY_10),
	MESON_PIN(GPIOY_11),
	MESON_PIN(GPIOY_12),
	MESON_PIN(GPIOY_13),
	MESON_PIN(GPIOY_14),

	MESON_PIN(GPIODV_9),
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

	MESON_PIN(DIF_0_P),
	MESON_PIN(DIF_0_N),
	MESON_PIN(DIF_1_P),
	MESON_PIN(DIF_1_N),
	MESON_PIN(DIF_2_P),
	MESON_PIN(DIF_2_N),
	MESON_PIN(DIF_3_P),
	MESON_PIN(DIF_3_N),
	MESON_PIN(DIF_4_P),
	MESON_PIN(DIF_4_N),
};

static const struct pinctrl_pin_desc meson8b_aobus_pins[] = {
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

	/*
	 * The following 2 pins are not mentionned in the public datasheet
	 * According to this datasheet, they can't be used with the gpio
	 * interrupt controller
	 */
	MESON_PIN(GPIO_BSD_EN),
	MESON_PIN(GPIO_TEST_N),
};

/* bank X */
static const unsigned int sd_d0_a_pins[]	= { GPIOX_0 };
static const unsigned int sd_d1_a_pins[]	= { GPIOX_1 };
static const unsigned int sd_d2_a_pins[]	= { GPIOX_2 };
static const unsigned int sd_d3_a_pins[]	= { GPIOX_3 };
static const unsigned int sdxc_d0_0_a_pins[]	= { GPIOX_4 };
static const unsigned int sdxc_d47_a_pins[]	= { GPIOX_4, GPIOX_5,
						    GPIOX_6, GPIOX_7 };
static const unsigned int sdxc_d13_0_a_pins[]	= { GPIOX_5, GPIOX_6,
						    GPIOX_7 };
static const unsigned int sd_clk_a_pins[]	= { GPIOX_8 };
static const unsigned int sd_cmd_a_pins[]	= { GPIOX_9 };
static const unsigned int xtal_32k_out_pins[]	= { GPIOX_10 };
static const unsigned int xtal_24m_out_pins[]	= { GPIOX_11 };
static const unsigned int uart_tx_b0_pins[]	= { GPIOX_16 };
static const unsigned int uart_rx_b0_pins[]	= { GPIOX_17 };
static const unsigned int uart_cts_b0_pins[]	= { GPIOX_18 };
static const unsigned int uart_rts_b0_pins[]	= { GPIOX_19 };

static const unsigned int sdxc_d0_1_a_pins[]	= { GPIOX_0 };
static const unsigned int sdxc_d13_1_a_pins[]	= { GPIOX_1, GPIOX_2,
						    GPIOX_3 };
static const unsigned int pcm_out_a_pins[]	= { GPIOX_4 };
static const unsigned int pcm_in_a_pins[]	= { GPIOX_5 };
static const unsigned int pcm_fs_a_pins[]	= { GPIOX_6 };
static const unsigned int pcm_clk_a_pins[]	= { GPIOX_7 };
static const unsigned int sdxc_clk_a_pins[]	= { GPIOX_8 };
static const unsigned int sdxc_cmd_a_pins[]	= { GPIOX_9 };
static const unsigned int pwm_vs_0_pins[]	= { GPIOX_10 };
static const unsigned int pwm_e_pins[]		= { GPIOX_10 };
static const unsigned int pwm_vs_1_pins[]	= { GPIOX_11 };

static const unsigned int uart_tx_a_pins[]	= { GPIOX_4 };
static const unsigned int uart_rx_a_pins[]	= { GPIOX_5 };
static const unsigned int uart_cts_a_pins[]	= { GPIOX_6 };
static const unsigned int uart_rts_a_pins[]	= { GPIOX_7 };
static const unsigned int uart_tx_b1_pins[]	= { GPIOX_8 };
static const unsigned int uart_rx_b1_pins[]	= { GPIOX_9 };
static const unsigned int uart_cts_b1_pins[]	= { GPIOX_10 };
static const unsigned int uart_rts_b1_pins[]	= { GPIOX_20 };

static const unsigned int iso7816_0_clk_pins[]	= { GPIOX_6 };
static const unsigned int iso7816_0_data_pins[]	= { GPIOX_7 };
static const unsigned int spi_sclk_0_pins[]	= { GPIOX_8 };
static const unsigned int spi_miso_0_pins[]	= { GPIOX_9 };
static const unsigned int spi_mosi_0_pins[]	= { GPIOX_10 };
static const unsigned int iso7816_det_pins[]	= { GPIOX_16 };
static const unsigned int iso7816_reset_pins[]	= { GPIOX_17 };
static const unsigned int iso7816_1_clk_pins[]	= { GPIOX_18 };
static const unsigned int iso7816_1_data_pins[]	= { GPIOX_19 };
static const unsigned int spi_ss0_0_pins[]	= { GPIOX_20 };

static const unsigned int tsin_clk_b_pins[]	= { GPIOX_8 };
static const unsigned int tsin_sop_b_pins[]	= { GPIOX_9 };
static const unsigned int tsin_d0_b_pins[]	= { GPIOX_10 };
static const unsigned int pwm_b_pins[]		= { GPIOX_11 };
static const unsigned int i2c_sda_d0_pins[]	= { GPIOX_16 };
static const unsigned int i2c_sck_d0_pins[]	= { GPIOX_17 };
static const unsigned int tsin_d_valid_b_pins[] = { GPIOX_20 };

/* bank Y */
static const unsigned int tsin_d_valid_a_pins[] = { GPIOY_0 };
static const unsigned int tsin_sop_a_pins[]	= { GPIOY_1 };
static const unsigned int tsin_d17_a_pins[] = {
	GPIOY_6, GPIOY_7, GPIOY_10, GPIOY_11, GPIOY_12, GPIOY_13, GPIOY_14,
};
static const unsigned int tsin_clk_a_pins[]	= { GPIOY_8 };
static const unsigned int tsin_d0_a_pins[]	= { GPIOY_9 };

static const unsigned int spdif_out_0_pins[]	= { GPIOY_3 };

static const unsigned int xtal_24m_pins[]	= { GPIOY_3 };
static const unsigned int iso7816_2_clk_pins[]	= { GPIOY_13 };
static const unsigned int iso7816_2_data_pins[] = { GPIOY_14 };

/* bank DV */
static const unsigned int pwm_d_pins[]		= { GPIODV_28 };
static const unsigned int pwm_c0_pins[]		= { GPIODV_29 };

static const unsigned int pwm_vs_2_pins[]	= { GPIODV_9 };
static const unsigned int pwm_vs_3_pins[]	= { GPIODV_28 };
static const unsigned int pwm_vs_4_pins[]	= { GPIODV_29 };

static const unsigned int xtal24_out_pins[]	= { GPIODV_29 };

static const unsigned int uart_tx_c_pins[]	= { GPIODV_24 };
static const unsigned int uart_rx_c_pins[]	= { GPIODV_25 };
static const unsigned int uart_cts_c_pins[]	= { GPIODV_26 };
static const unsigned int uart_rts_c_pins[]	= { GPIODV_27 };

static const unsigned int pwm_c1_pins[]		= { GPIODV_9 };

static const unsigned int i2c_sda_a_pins[]	= { GPIODV_24 };
static const unsigned int i2c_sck_a_pins[]	= { GPIODV_25 };
static const unsigned int i2c_sda_b0_pins[]	= { GPIODV_26 };
static const unsigned int i2c_sck_b0_pins[]	= { GPIODV_27 };
static const unsigned int i2c_sda_c0_pins[]	= { GPIODV_28 };
static const unsigned int i2c_sck_c0_pins[]	= { GPIODV_29 };

/* bank H */
static const unsigned int hdmi_hpd_pins[]	= { GPIOH_0 };
static const unsigned int hdmi_sda_pins[]	= { GPIOH_1 };
static const unsigned int hdmi_scl_pins[]	= { GPIOH_2 };
static const unsigned int hdmi_cec_0_pins[]	= { GPIOH_3 };
static const unsigned int eth_txd1_0_pins[]	= { GPIOH_5 };
static const unsigned int eth_txd0_0_pins[]	= { GPIOH_6 };
static const unsigned int clk_24m_out_pins[]	= { GPIOH_9 };

static const unsigned int spi_ss1_pins[]	= { GPIOH_0 };
static const unsigned int spi_ss2_pins[]	= { GPIOH_1 };
static const unsigned int spi_ss0_1_pins[]	= { GPIOH_3 };
static const unsigned int spi_miso_1_pins[]	= { GPIOH_4 };
static const unsigned int spi_mosi_1_pins[]	= { GPIOH_5 };
static const unsigned int spi_sclk_1_pins[]	= { GPIOH_6 };

static const unsigned int eth_txd3_pins[]	= { GPIOH_7 };
static const unsigned int eth_txd2_pins[]	= { GPIOH_8 };
static const unsigned int eth_tx_clk_pins[]	= { GPIOH_9 };

static const unsigned int i2c_sda_b1_pins[]	= { GPIOH_3 };
static const unsigned int i2c_sck_b1_pins[]	= { GPIOH_4 };
static const unsigned int i2c_sda_c1_pins[]	= { GPIOH_5 };
static const unsigned int i2c_sck_c1_pins[]	= { GPIOH_6 };
static const unsigned int i2c_sda_d1_pins[]	= { GPIOH_7 };
static const unsigned int i2c_sck_d1_pins[]	= { GPIOH_8 };

/* bank BOOT */
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
static const unsigned int nand_dqs_15_pins[]	= { BOOT_15 };
static const unsigned int nand_dqs_18_pins[]	= { BOOT_18 };

static const unsigned int sdxc_d0_c_pins[]	= { BOOT_0};
static const unsigned int sdxc_d13_c_pins[]	= { BOOT_1, BOOT_2,
						    BOOT_3 };
static const unsigned int sdxc_d47_c_pins[]	= { BOOT_4, BOOT_5,
						    BOOT_6, BOOT_7 };
static const unsigned int sdxc_clk_c_pins[]	= { BOOT_8 };
static const unsigned int sdxc_cmd_c_pins[]	= { BOOT_10 };
static const unsigned int nor_d_pins[]		= { BOOT_11 };
static const unsigned int nor_q_pins[]		= { BOOT_12 };
static const unsigned int nor_c_pins[]		= { BOOT_13 };
static const unsigned int nor_cs_pins[]		= { BOOT_18 };

static const unsigned int sd_d0_c_pins[]	= { BOOT_0 };
static const unsigned int sd_d1_c_pins[]	= { BOOT_1 };
static const unsigned int sd_d2_c_pins[]	= { BOOT_2 };
static const unsigned int sd_d3_c_pins[]	= { BOOT_3 };
static const unsigned int sd_cmd_c_pins[]	= { BOOT_8 };
static const unsigned int sd_clk_c_pins[]	= { BOOT_10 };

/* bank CARD */
static const unsigned int sd_d1_b_pins[]	= { CARD_0 };
static const unsigned int sd_d0_b_pins[]	= { CARD_1 };
static const unsigned int sd_clk_b_pins[]	= { CARD_2 };
static const unsigned int sd_cmd_b_pins[]	= { CARD_3 };
static const unsigned int sd_d3_b_pins[]	= { CARD_4 };
static const unsigned int sd_d2_b_pins[]	= { CARD_5 };

static const unsigned int sdxc_d13_b_pins[]	= { CARD_0,  CARD_4,
						    CARD_5 };
static const unsigned int sdxc_d0_b_pins[]	= { CARD_1 };
static const unsigned int sdxc_clk_b_pins[]	= { CARD_2 };
static const unsigned int sdxc_cmd_b_pins[]	= { CARD_3 };

/* bank AO */
static const unsigned int uart_tx_ao_a_pins[]	= { GPIOAO_0 };
static const unsigned int uart_rx_ao_a_pins[]	= { GPIOAO_1 };
static const unsigned int uart_cts_ao_a_pins[]	= { GPIOAO_2 };
static const unsigned int uart_rts_ao_a_pins[]	= { GPIOAO_3 };
static const unsigned int i2c_mst_sck_ao_pins[] = { GPIOAO_4 };
static const unsigned int i2c_mst_sda_ao_pins[] = { GPIOAO_5 };
static const unsigned int clk_32k_in_out_pins[]	= { GPIOAO_6 };
static const unsigned int remote_input_pins[]	= { GPIOAO_7 };
static const unsigned int hdmi_cec_1_pins[]	= { GPIOAO_12 };
static const unsigned int ir_blaster_pins[]	= { GPIOAO_13 };

static const unsigned int pwm_c2_pins[]		= { GPIOAO_3 };
static const unsigned int i2c_sck_ao_pins[]	= { GPIOAO_4 };
static const unsigned int i2c_sda_ao_pins[]	= { GPIOAO_5 };
static const unsigned int ir_remote_out_pins[]	= { GPIOAO_7 };
static const unsigned int i2s_am_clk_out_pins[]	= { GPIOAO_8 };
static const unsigned int i2s_ao_clk_out_pins[]	= { GPIOAO_9 };
static const unsigned int i2s_lr_clk_out_pins[]	= { GPIOAO_10 };
static const unsigned int i2s_out_01_pins[]	= { GPIOAO_11 };

static const unsigned int uart_tx_ao_b0_pins[]	= { GPIOAO_0 };
static const unsigned int uart_rx_ao_b0_pins[]	= { GPIOAO_1 };
static const unsigned int uart_cts_ao_b_pins[]	= { GPIOAO_2 };
static const unsigned int uart_rts_ao_b_pins[]	= { GPIOAO_3 };
static const unsigned int uart_tx_ao_b1_pins[]	= { GPIOAO_4 };
static const unsigned int uart_rx_ao_b1_pins[]	= { GPIOAO_5 };
static const unsigned int spdif_out_1_pins[]	= { GPIOAO_6 };

static const unsigned int i2s_in_ch01_pins[]	= { GPIOAO_6 };
static const unsigned int i2s_ao_clk_in_pins[]	= { GPIOAO_9 };
static const unsigned int i2s_lr_clk_in_pins[]	= { GPIOAO_10 };

/* bank DIF */
static const unsigned int eth_rxd1_pins[]	= { DIF_0_P };
static const unsigned int eth_rxd0_pins[]	= { DIF_0_N };
static const unsigned int eth_rx_dv_pins[]	= { DIF_1_P };
static const unsigned int eth_rx_clk_pins[]	= { DIF_1_N };
static const unsigned int eth_txd0_1_pins[]	= { DIF_2_P };
static const unsigned int eth_txd1_1_pins[]	= { DIF_2_N };
static const unsigned int eth_tx_en_pins[]	= { DIF_3_P };
static const unsigned int eth_ref_clk_pins[]	= { DIF_3_N };
static const unsigned int eth_mdc_pins[]	= { DIF_4_P };
static const unsigned int eth_mdio_en_pins[]	= { DIF_4_N };

static struct meson_pmx_group meson8b_cbus_groups[] = {
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
	GPIO_GROUP(GPIOX_16),
	GPIO_GROUP(GPIOX_17),
	GPIO_GROUP(GPIOX_18),
	GPIO_GROUP(GPIOX_19),
	GPIO_GROUP(GPIOX_20),
	GPIO_GROUP(GPIOX_21),

	GPIO_GROUP(GPIOY_0),
	GPIO_GROUP(GPIOY_1),
	GPIO_GROUP(GPIOY_3),
	GPIO_GROUP(GPIOY_6),
	GPIO_GROUP(GPIOY_7),
	GPIO_GROUP(GPIOY_8),
	GPIO_GROUP(GPIOY_9),
	GPIO_GROUP(GPIOY_10),
	GPIO_GROUP(GPIOY_11),
	GPIO_GROUP(GPIOY_12),
	GPIO_GROUP(GPIOY_13),
	GPIO_GROUP(GPIOY_14),

	GPIO_GROUP(GPIODV_9),
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

	GPIO_GROUP(DIF_0_P),
	GPIO_GROUP(DIF_0_N),
	GPIO_GROUP(DIF_1_P),
	GPIO_GROUP(DIF_1_N),
	GPIO_GROUP(DIF_2_P),
	GPIO_GROUP(DIF_2_N),
	GPIO_GROUP(DIF_3_P),
	GPIO_GROUP(DIF_3_N),
	GPIO_GROUP(DIF_4_P),
	GPIO_GROUP(DIF_4_N),

	/* bank X */
	GROUP(sd_d0_a,		8,	5),
	GROUP(sd_d1_a,		8,	4),
	GROUP(sd_d2_a,		8,	3),
	GROUP(sd_d3_a,		8,	2),
	GROUP(sdxc_d0_0_a,	5,	29),
	GROUP(sdxc_d47_a,	5,	12),
	GROUP(sdxc_d13_0_a,	5,	28),
	GROUP(sd_clk_a,		8,	1),
	GROUP(sd_cmd_a,		8,	0),
	GROUP(xtal_32k_out,	3,	22),
	GROUP(xtal_24m_out,	3,	20),
	GROUP(uart_tx_b0,	4,	9),
	GROUP(uart_rx_b0,	4,	8),
	GROUP(uart_cts_b0,	4,	7),
	GROUP(uart_rts_b0,	4,	6),
	GROUP(sdxc_d0_1_a,	5,	14),
	GROUP(sdxc_d13_1_a,	5,	13),
	GROUP(pcm_out_a,	3,	30),
	GROUP(pcm_in_a,		3,	29),
	GROUP(pcm_fs_a,		3,	28),
	GROUP(pcm_clk_a,	3,	27),
	GROUP(sdxc_clk_a,	5,	11),
	GROUP(sdxc_cmd_a,	5,	10),
	GROUP(pwm_vs_0,		7,	31),
	GROUP(pwm_e,		9,	19),
	GROUP(pwm_vs_1,		7,	30),
	GROUP(uart_tx_a,	4,	17),
	GROUP(uart_rx_a,	4,	16),
	GROUP(uart_cts_a,	4,	15),
	GROUP(uart_rts_a,	4,	14),
	GROUP(uart_tx_b1,	6,	19),
	GROUP(uart_rx_b1,	6,	18),
	GROUP(uart_cts_b1,	6,	17),
	GROUP(uart_rts_b1,	6,	16),
	GROUP(iso7816_0_clk,	5,	9),
	GROUP(iso7816_0_data,	5,	8),
	GROUP(spi_sclk_0,	4,	22),
	GROUP(spi_miso_0,	4,	24),
	GROUP(spi_mosi_0,	4,	23),
	GROUP(iso7816_det,	4,	21),
	GROUP(iso7816_reset,	4,	20),
	GROUP(iso7816_1_clk,	4,	19),
	GROUP(iso7816_1_data,	4,	18),
	GROUP(spi_ss0_0,	4,	25),
	GROUP(tsin_clk_b,	3,	6),
	GROUP(tsin_sop_b,	3,	7),
	GROUP(tsin_d0_b,	3,	8),
	GROUP(pwm_b,		2,	3),
	GROUP(i2c_sda_d0,	4,	5),
	GROUP(i2c_sck_d0,	4,	4),
	GROUP(tsin_d_valid_b,	3,	9),

	/* bank Y */
	GROUP(tsin_d_valid_a,	3,	2),
	GROUP(tsin_sop_a,	3,	1),
	GROUP(tsin_d17_a,	3,	5),
	GROUP(tsin_clk_a,	3,	0),
	GROUP(tsin_d0_a,	3,	4),
	GROUP(spdif_out_0,	1,	7),
	GROUP(xtal_24m,		3,	18),
	GROUP(iso7816_2_clk,	5,	7),
	GROUP(iso7816_2_data,	5,	6),

	/* bank DV */
	GROUP(pwm_d,		3,	26),
	GROUP(pwm_c0,		3,	25),
	GROUP(pwm_vs_2,		7,	28),
	GROUP(pwm_vs_3,		7,	27),
	GROUP(pwm_vs_4,		7,	26),
	GROUP(xtal24_out,	7,	25),
	GROUP(uart_tx_c,	6,	23),
	GROUP(uart_rx_c,	6,	22),
	GROUP(uart_cts_c,	6,	21),
	GROUP(uart_rts_c,	6,	20),
	GROUP(pwm_c1,		3,	24),
	GROUP(i2c_sda_a,	9,	31),
	GROUP(i2c_sck_a,	9,	30),
	GROUP(i2c_sda_b0,	9,	29),
	GROUP(i2c_sck_b0,	9,	28),
	GROUP(i2c_sda_c0,	9,	27),
	GROUP(i2c_sck_c0,	9,	26),

	/* bank H */
	GROUP(hdmi_hpd,		1,	26),
	GROUP(hdmi_sda,		1,	25),
	GROUP(hdmi_scl,		1,	24),
	GROUP(hdmi_cec_0,	1,	23),
	GROUP(eth_txd1_0,	7,	21),
	GROUP(eth_txd0_0,	7,	20),
	GROUP(clk_24m_out,	4,	1),
	GROUP(spi_ss1,		8,	11),
	GROUP(spi_ss2,		8,	12),
	GROUP(spi_ss0_1,	9,	13),
	GROUP(spi_miso_1,	9,	12),
	GROUP(spi_mosi_1,	9,	11),
	GROUP(spi_sclk_1,	9,	10),
	GROUP(eth_txd3,		6,	13),
	GROUP(eth_txd2,		6,	12),
	GROUP(eth_tx_clk,	6,	11),
	GROUP(i2c_sda_b1,	5,	27),
	GROUP(i2c_sck_b1,	5,	26),
	GROUP(i2c_sda_c1,	5,	25),
	GROUP(i2c_sck_c1,	5,	24),
	GROUP(i2c_sda_d1,	4,	3),
	GROUP(i2c_sck_d1,	4,	2),

	/* bank BOOT */
	GROUP(nand_io,		2,	26),
	GROUP(nand_io_ce0,	2,	25),
	GROUP(nand_io_ce1,	2,	24),
	GROUP(nand_io_rb0,	2,	17),
	GROUP(nand_ale,		2,	21),
	GROUP(nand_cle,		2,	20),
	GROUP(nand_wen_clk,	2,	19),
	GROUP(nand_ren_clk,	2,	18),
	GROUP(nand_dqs_15,	2,	27),
	GROUP(nand_dqs_18,	2,	28),
	GROUP(sdxc_d0_c,	4,	30),
	GROUP(sdxc_d13_c,	4,	29),
	GROUP(sdxc_d47_c,	4,	28),
	GROUP(sdxc_clk_c,	7,	19),
	GROUP(sdxc_cmd_c,	7,	18),
	GROUP(nor_d,		5,	1),
	GROUP(nor_q,		5,	3),
	GROUP(nor_c,		5,	2),
	GROUP(nor_cs,		5,	0),
	GROUP(sd_d0_c,		6,	29),
	GROUP(sd_d1_c,		6,	28),
	GROUP(sd_d2_c,		6,	27),
	GROUP(sd_d3_c,		6,	26),
	GROUP(sd_cmd_c,		6,	30),
	GROUP(sd_clk_c,		6,	31),

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

	/* bank DIF */
	GROUP(eth_rxd1,		6,	0),
	GROUP(eth_rxd0,		6,	1),
	GROUP(eth_rx_dv,	6,	2),
	GROUP(eth_rx_clk,	6,	3),
	GROUP(eth_txd0_1,	6,	4),
	GROUP(eth_txd1_1,	6,	5),
	GROUP(eth_tx_en,	6,	6),
	GROUP(eth_ref_clk,	6,	8),
	GROUP(eth_mdc,		6,	9),
	GROUP(eth_mdio_en,	6,	10),
};

static struct meson_pmx_group meson8b_aobus_groups[] = {
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
	GROUP(uart_tx_ao_a,	0,	12),
	GROUP(uart_rx_ao_a,	0,	11),
	GROUP(uart_cts_ao_a,	0,	10),
	GROUP(uart_rts_ao_a,	0,	9),
	GROUP(i2c_mst_sck_ao,	0,	6),
	GROUP(i2c_mst_sda_ao,	0,	5),
	GROUP(clk_32k_in_out,	0,	18),
	GROUP(remote_input,	0,	0),
	GROUP(hdmi_cec_1,	0,	17),
	GROUP(ir_blaster,	0,	31),
	GROUP(pwm_c2,		0,	22),
	GROUP(i2c_sck_ao,	0,	2),
	GROUP(i2c_sda_ao,	0,	1),
	GROUP(ir_remote_out,	0,	21),
	GROUP(i2s_am_clk_out,	0,	30),
	GROUP(i2s_ao_clk_out,	0,	29),
	GROUP(i2s_lr_clk_out,	0,	28),
	GROUP(i2s_out_01,	0,	27),
	GROUP(uart_tx_ao_b0,	0,	26),
	GROUP(uart_rx_ao_b0,	0,	25),
	GROUP(uart_cts_ao_b,	0,	8),
	GROUP(uart_rts_ao_b,	0,	7),
	GROUP(uart_tx_ao_b1,	0,	24),
	GROUP(uart_rx_ao_b1,	0,	23),
	GROUP(spdif_out_1,	0,	16),
	GROUP(i2s_in_ch01,	0,	13),
	GROUP(i2s_ao_clk_in,	0,	15),
	GROUP(i2s_lr_clk_in,	0,	14),
};

static const char * const gpio_groups[] = {
	"GPIOX_0", "GPIOX_1", "GPIOX_2", "GPIOX_3", "GPIOX_4",
	"GPIOX_5", "GPIOX_6", "GPIOX_7", "GPIOX_8", "GPIOX_9",
	"GPIOX_10", "GPIOX_11", "GPIOX_16", "GPIOX_17", "GPIOX_18",
	"GPIOX_19", "GPIOX_20", "GPIOX_21",

	"GPIOY_0", "GPIOY_1", "GPIOY_3", "GPIOY_6", "GPIOY_7",
	"GPIOY_8", "GPIOY_9", "GPIOY_10", "GPIOY_11", "GPIOY_12",
	"GPIOY_13", "GPIOY_14",

	"GPIODV_9", "GPIODV_24", "GPIODV_25", "GPIODV_26",
	"GPIODV_27", "GPIODV_28", "GPIODV_29",

	"GPIOH_0", "GPIOH_1", "GPIOH_2", "GPIOH_3", "GPIOH_4",
	"GPIOH_5", "GPIOH_6", "GPIOH_7", "GPIOH_8", "GPIOH_9",

	"CARD_0", "CARD_1", "CARD_2", "CARD_3", "CARD_4",
	"CARD_5", "CARD_6",

	"BOOT_0", "BOOT_1", "BOOT_2", "BOOT_3", "BOOT_4",
	"BOOT_5", "BOOT_6", "BOOT_7", "BOOT_8", "BOOT_9",
	"BOOT_10", "BOOT_11", "BOOT_12", "BOOT_13", "BOOT_14",
	"BOOT_15", "BOOT_16", "BOOT_17", "BOOT_18",

	"GPIOAO_0", "GPIOAO_1", "GPIOAO_2", "GPIOAO_3",
	"GPIOAO_4", "GPIOAO_5", "GPIOAO_6", "GPIOAO_7",
	"GPIOAO_8", "GPIOAO_9", "GPIOAO_10", "GPIOAO_11",
	"GPIOAO_12", "GPIOAO_13", "GPIO_BSD_EN", "GPIO_TEST_N",

	"DIF_0_P", "DIF_0_N", "DIF_1_P", "DIF_1_N",
	"DIF_2_P", "DIF_2_N", "DIF_3_P", "DIF_3_N",
	"DIF_4_P", "DIF_4_N"
};

static const char * const sd_a_groups[] = {
	"sd_d0_a", "sd_d1_a", "sd_d2_a", "sd_d3_a", "sd_clk_a",
	"sd_cmd_a"
};

static const char * const sdxc_a_groups[] = {
	"sdxc_d0_0_a", "sdxc_d13_0_a", "sdxc_d47_a", "sdxc_clk_a",
	"sdxc_cmd_a", "sdxc_d0_1_a", "sdxc_d0_13_1_a"
};

static const char * const pcm_a_groups[] = {
	"pcm_out_a", "pcm_in_a", "pcm_fs_a", "pcm_clk_a"
};

static const char * const uart_a_groups[] = {
	"uart_tx_a", "uart_rx_a", "uart_cts_a", "uart_rts_a"
};

static const char * const uart_b_groups[] = {
	"uart_tx_b0", "uart_rx_b0", "uart_cts_b0", "uart_rts_b0",
	"uart_tx_b1", "uart_rx_b1", "uart_cts_b1", "uart_rts_b1"
};

static const char * const iso7816_groups[] = {
	"iso7816_det", "iso7816_reset", "iso7816_0_clk", "iso7816_0_data",
	"iso7816_1_clk", "iso7816_1_data", "iso7816_2_clk", "iso7816_2_data"
};

static const char * const i2c_d_groups[] = {
	"i2c_sda_d0", "i2c_sck_d0", "i2c_sda_d1", "i2c_sck_d1"
};

static const char * const xtal_groups[] = {
	"xtal_32k_out", "xtal_24m_out", "xtal_24m", "xtal24_out"
};

static const char * const uart_c_groups[] = {
	"uart_tx_c", "uart_rx_c", "uart_cts_c", "uart_rts_c"
};

static const char * const i2c_c_groups[] = {
	"i2c_sda_c0", "i2c_sck_c0", "i2c_sda_c1", "i2c_sck_c1"
};

static const char * const hdmi_groups[] = {
	"hdmi_hpd", "hdmi_sda", "hdmi_scl", "hdmi_cec_0"
};

static const char * const hdmi_cec_groups[] = {
	"hdmi_cec_1"
};

static const char * const spi_groups[] = {
	"spi_ss0_0", "spi_miso_0", "spi_mosi_0", "spi_sclk_0",
	"spi_ss0_1", "spi_ss1", "spi_sclk_1", "spi_mosi_1",
	"spi_miso_1", "spi_ss2"
};

static const char * const ethernet_groups[] = {
	"eth_tx_clk", "eth_tx_en", "eth_txd1_0", "eth_txd1_1",
	"eth_txd0_0", "eth_txd0_1", "eth_rx_clk", "eth_rx_dv",
	"eth_rxd1", "eth_rxd0", "eth_mdio_en", "eth_mdc", "eth_ref_clk",
	"eth_txd2", "eth_txd3"
};

static const char * const i2c_a_groups[] = {
	"i2c_sda_a", "i2c_sck_a",
};

static const char * const i2c_b_groups[] = {
	"i2c_sda_b0", "i2c_sck_b0", "i2c_sda_b1", "i2c_sck_b1"
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
	"nand_wen_clk", "nand_ren_clk", "nand_dqs_15",
	"nand_dqs_18"
};

static const char * const nor_groups[] = {
	"nor_d", "nor_q", "nor_c", "nor_cs"
};

static const char * const sd_b_groups[] = {
	"sd_d1_b", "sd_d0_b", "sd_clk_b", "sd_cmd_b",
	"sd_d3_b", "sd_d2_b"
};

static const char * const sdxc_b_groups[] = {
	"sdxc_d13_b", "sdxc_d0_b", "sdxc_clk_b", "sdxc_cmd_b"
};

static const char * const uart_ao_groups[] = {
	"uart_tx_ao_a", "uart_rx_ao_a", "uart_cts_ao_a", "uart_rts_ao_a"
};

static const char * const remote_groups[] = {
	"remote_input", "ir_blaster", "ir_remote_out"
};

static const char * const i2c_slave_ao_groups[] = {
	"i2c_sck_ao", "i2c_sda_ao"
};

static const char * const uart_ao_b_groups[] = {
	"uart_tx_ao_b0", "uart_rx_ao_b0", "uart_tx_ao_b1", "uart_rx_ao_b1",
	"uart_cts_ao_b", "uart_rts_ao_b"
};

static const char * const i2c_mst_ao_groups[] = {
	"i2c_mst_sck_ao", "i2c_mst_sda_ao"
};

static const char * const clk_24m_groups[] = {
	"clk_24m_out"
};

static const char * const clk_32k_groups[] = {
	"clk_32k_in_out"
};

static const char * const spdif_0_groups[] = {
	"spdif_out_0"
};

static const char * const spdif_1_groups[] = {
	"spdif_out_1"
};

static const char * const i2s_groups[] = {
	"i2s_am_clk_out", "i2s_ao_clk_out", "i2s_lr_clk_out",
	"i2s_out_01", "i2s_in_ch01", "i2s_ao_clk_in",
	"i2s_lr_clk_in"
};

static const char * const pwm_b_groups[] = {
	"pwm_b"
};

static const char * const pwm_c_groups[] = {
	"pwm_c0", "pwm_c1"
};

static const char * const pwm_c_ao_groups[] = {
	"pwm_c2"
};

static const char * const pwm_d_groups[] = {
	"pwm_d"
};

static const char * const pwm_e_groups[] = {
	"pwm_e"
};

static const char * const pwm_vs_groups[] = {
	"pwm_vs_0", "pwm_vs_1", "pwm_vs_2",
	"pwm_vs_3", "pwm_vs_4"
};

static const char * const tsin_a_groups[] = {
	"tsin_d0_a", "tsin_d17_a", "tsin_clk_a", "tsin_sop_a",
	"tsin_d_valid_a"
};

static const char * const tsin_b_groups[] = {
	"tsin_d0_b", "tsin_clk_b", "tsin_sop_b", "tsin_d_valid_b"
};

static struct meson_pmx_func meson8b_cbus_functions[] = {
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
	FUNCTION(i2c_c),
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
	FUNCTION(spdif_0),
	FUNCTION(pwm_b),
	FUNCTION(pwm_c),
	FUNCTION(pwm_d),
	FUNCTION(pwm_e),
	FUNCTION(pwm_vs),
	FUNCTION(tsin_a),
	FUNCTION(tsin_b),
	FUNCTION(clk_24m),
};

static struct meson_pmx_func meson8b_aobus_functions[] = {
	FUNCTION(uart_ao),
	FUNCTION(uart_ao_b),
	FUNCTION(i2c_slave_ao),
	FUNCTION(i2c_mst_ao),
	FUNCTION(i2s),
	FUNCTION(remote),
	FUNCTION(clk_32k),
	FUNCTION(pwm_c_ao),
	FUNCTION(spdif_1),
	FUNCTION(hdmi_cec),
};

static struct meson_bank meson8b_cbus_banks[] = {
	/*   name        first          last        irq       pullen   pull     dir      out      in   */
	BANK("X0..11",	 GPIOX_0,	GPIOX_11,   97, 108,  4,  0,   4,  0,   0,  0,   1,  0,   2,  0),
	BANK("X16..21",	 GPIOX_16,	GPIOX_21,  113, 118,  4, 16,   4, 16,   0, 16,   1, 16,   2, 16),
	BANK("Y0..1",	 GPIOY_0,	GPIOY_1,    80,  81,  3,  0,   3,  0,   3,  0,   4,  0,   5,  0),
	BANK("Y3",	 GPIOY_3,	GPIOY_3,    83,  83,  3,  3,   3,  3,   3,  3,   4,  3,   5,  3),
	BANK("Y6..14",	 GPIOY_6,	GPIOY_14,   86,  94,  3,  6,   3,  6,   3,  6,   4,  6,   5,  6),
	BANK("DV9",	 GPIODV_9,	GPIODV_9,   59,  59,  0,  9,   0,  9,   7,  9,   8,  9,   9,  9),
	BANK("DV24..29", GPIODV_24,	GPIODV_29,  74,  79,  0, 24,   0, 24,   7, 24,   8, 24,   9, 24),
	BANK("H",	 GPIOH_0,	GPIOH_9,    14,  23,  1, 16,   1, 16,   9, 19,  10, 19,  11, 19),
	BANK("CARD",	 CARD_0,	CARD_6,     43,  49,  2, 20,   2, 20,   0, 22,   1, 22,   2, 22),
	BANK("BOOT",	 BOOT_0,	BOOT_18,    24,  42,  2,  0,   2,  0,   9,  0,  10,  0,  11,  0),

	/*
	 * The following bank is not mentionned in the public datasheet
	 * There is no information whether it can be used with the gpio
	 * interrupt controller
	 */
	BANK("DIF",	 DIF_0_P,	DIF_4_N,    -1,  -1,  5,  8,   5,  8,  12, 12,  13, 12,  14, 12),
};

static struct meson_bank meson8b_aobus_banks[] = {
	/*   name    first     lastc        irq    pullen  pull    dir     out     in  */
	BANK("AO",   GPIOAO_0, GPIO_TEST_N, 0, 13, 0,  16, 0, 0,  0,  0,  0, 16,  1,  0),
};

static struct meson_pinctrl_data meson8b_cbus_pinctrl_data = {
	.name		= "cbus-banks",
	.pins		= meson8b_cbus_pins,
	.groups		= meson8b_cbus_groups,
	.funcs		= meson8b_cbus_functions,
	.banks		= meson8b_cbus_banks,
	.num_pins	= ARRAY_SIZE(meson8b_cbus_pins),
	.num_groups	= ARRAY_SIZE(meson8b_cbus_groups),
	.num_funcs	= ARRAY_SIZE(meson8b_cbus_functions),
	.num_banks	= ARRAY_SIZE(meson8b_cbus_banks),
	.pmx_ops	= &meson8_pmx_ops,
};

static struct meson_pinctrl_data meson8b_aobus_pinctrl_data = {
	.name		= "aobus-banks",
	.pins		= meson8b_aobus_pins,
	.groups		= meson8b_aobus_groups,
	.funcs		= meson8b_aobus_functions,
	.banks		= meson8b_aobus_banks,
	.num_pins	= ARRAY_SIZE(meson8b_aobus_pins),
	.num_groups	= ARRAY_SIZE(meson8b_aobus_groups),
	.num_funcs	= ARRAY_SIZE(meson8b_aobus_functions),
	.num_banks	= ARRAY_SIZE(meson8b_aobus_banks),
	.pmx_ops	= &meson8_pmx_ops,
};

static const struct of_device_id meson8b_pinctrl_dt_match[] = {
	{
		.compatible = "amlogic,meson8b-cbus-pinctrl",
		.data = &meson8b_cbus_pinctrl_data,
	},
	{
		.compatible = "amlogic,meson8b-aobus-pinctrl",
		.data = &meson8b_aobus_pinctrl_data,
	},
	{ },
};

static struct platform_driver meson8b_pinctrl_driver = {
	.probe		= meson_pinctrl_probe,
	.driver = {
		.name	= "meson8b-pinctrl",
		.of_match_table = meson8b_pinctrl_dt_match,
	},
};
builtin_platform_driver(meson8b_pinctrl_driver);
