// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Pin controller and GPIO driver for Amlogic Meson AXG SoC.
 *
 * Copyright (c) 2017 Amlogic, Inc. All rights reserved.
 * Author: Xingyu Chen <xingyu.chen@amlogic.com>
 */

#include <dt-bindings/gpio/meson-axg-gpio.h>
#include "pinctrl-meson.h"
#include "pinctrl-meson-axg-pmx.h"

static const struct pinctrl_pin_desc meson_axg_periphs_pins[] = {
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
	MESON_PIN(GPIOA_12),
	MESON_PIN(GPIOA_13),
	MESON_PIN(GPIOA_14),
	MESON_PIN(GPIOA_15),
	MESON_PIN(GPIOA_16),
	MESON_PIN(GPIOA_17),
	MESON_PIN(GPIOA_18),
	MESON_PIN(GPIOA_19),
	MESON_PIN(GPIOA_20),
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
	MESON_PIN(GPIOX_22),
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
};

static const struct pinctrl_pin_desc meson_axg_aobus_pins[] = {
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
	MESON_PIN(GPIO_TEST_N),
};

/* emmc */
static const unsigned int emmc_nand_d0_pins[] = {BOOT_0};
static const unsigned int emmc_nand_d1_pins[] = {BOOT_1};
static const unsigned int emmc_nand_d2_pins[] = {BOOT_2};
static const unsigned int emmc_nand_d3_pins[] = {BOOT_3};
static const unsigned int emmc_nand_d4_pins[] = {BOOT_4};
static const unsigned int emmc_nand_d5_pins[] = {BOOT_5};
static const unsigned int emmc_nand_d6_pins[] = {BOOT_6};
static const unsigned int emmc_nand_d7_pins[] = {BOOT_7};

static const unsigned int emmc_clk_pins[] = {BOOT_8};
static const unsigned int emmc_cmd_pins[] = {BOOT_10};
static const unsigned int emmc_ds_pins[]  = {BOOT_13};

/* nand */
static const unsigned int nand_ce0_pins[] = {BOOT_8};
static const unsigned int nand_ale_pins[] = {BOOT_9};
static const unsigned int nand_cle_pins[] = {BOOT_10};
static const unsigned int nand_wen_clk_pins[] = {BOOT_11};
static const unsigned int nand_ren_wr_pins[] = {BOOT_12};
static const unsigned int nand_rb0_pins[] = {BOOT_13};

/* nor */
static const unsigned int nor_hold_pins[] = {BOOT_3};
static const unsigned int nor_d_pins[] = {BOOT_4};
static const unsigned int nor_q_pins[] = {BOOT_5};
static const unsigned int nor_c_pins[] = {BOOT_6};
static const unsigned int nor_wp_pins[] = {BOOT_9};
static const unsigned int nor_cs_pins[] = {BOOT_14};

/* sdio */
static const unsigned int sdio_d0_pins[] = {GPIOX_0};
static const unsigned int sdio_d1_pins[] = {GPIOX_1};
static const unsigned int sdio_d2_pins[] = {GPIOX_2};
static const unsigned int sdio_d3_pins[] = {GPIOX_3};
static const unsigned int sdio_clk_pins[] = {GPIOX_4};
static const unsigned int sdio_cmd_pins[] = {GPIOX_5};

/* spi0 */
static const unsigned int spi0_clk_pins[] = {GPIOZ_0};
static const unsigned int spi0_mosi_pins[] = {GPIOZ_1};
static const unsigned int spi0_miso_pins[] = {GPIOZ_2};
static const unsigned int spi0_ss0_pins[] = {GPIOZ_3};
static const unsigned int spi0_ss1_pins[] = {GPIOZ_4};
static const unsigned int spi0_ss2_pins[] = {GPIOZ_5};

/* spi1 */
static const unsigned int spi1_clk_x_pins[] = {GPIOX_19};
static const unsigned int spi1_mosi_x_pins[] = {GPIOX_17};
static const unsigned int spi1_miso_x_pins[] = {GPIOX_18};
static const unsigned int spi1_ss0_x_pins[] = {GPIOX_16};

static const unsigned int spi1_clk_a_pins[] = {GPIOA_4};
static const unsigned int spi1_mosi_a_pins[] = {GPIOA_2};
static const unsigned int spi1_miso_a_pins[] = {GPIOA_3};
static const unsigned int spi1_ss0_a_pins[] = {GPIOA_5};
static const unsigned int spi1_ss1_pins[] = {GPIOA_6};

/* i2c0 */
static const unsigned int i2c0_sck_pins[] = {GPIOZ_6};
static const unsigned int i2c0_sda_pins[] = {GPIOZ_7};

/* i2c1 */
static const unsigned int i2c1_sck_z_pins[] = {GPIOZ_8};
static const unsigned int i2c1_sda_z_pins[] = {GPIOZ_9};

static const unsigned int i2c1_sck_x_pins[] = {GPIOX_16};
static const unsigned int i2c1_sda_x_pins[] = {GPIOX_17};

/* i2c2 */
static const unsigned int i2c2_sck_x_pins[] = {GPIOX_18};
static const unsigned int i2c2_sda_x_pins[] = {GPIOX_19};

static const unsigned int i2c2_sda_a_pins[] = {GPIOA_17};
static const unsigned int i2c2_sck_a_pins[] = {GPIOA_18};

/* i2c3 */
static const unsigned int i2c3_sda_a6_pins[] = {GPIOA_6};
static const unsigned int i2c3_sck_a7_pins[] = {GPIOA_7};

static const unsigned int i2c3_sda_a12_pins[] = {GPIOA_12};
static const unsigned int i2c3_sck_a13_pins[] = {GPIOA_13};

static const unsigned int i2c3_sda_a19_pins[] = {GPIOA_19};
static const unsigned int i2c3_sck_a20_pins[] = {GPIOA_20};

/* uart_a */
static const unsigned int uart_rts_a_pins[] = {GPIOX_11};
static const unsigned int uart_cts_a_pins[] = {GPIOX_10};
static const unsigned int uart_tx_a_pins[] = {GPIOX_8};
static const unsigned int uart_rx_a_pins[] = {GPIOX_9};

/* uart_b */
static const unsigned int uart_rts_b_z_pins[] = {GPIOZ_0};
static const unsigned int uart_cts_b_z_pins[] = {GPIOZ_1};
static const unsigned int uart_tx_b_z_pins[] = {GPIOZ_2};
static const unsigned int uart_rx_b_z_pins[] = {GPIOZ_3};

static const unsigned int uart_rts_b_x_pins[] = {GPIOX_18};
static const unsigned int uart_cts_b_x_pins[] = {GPIOX_19};
static const unsigned int uart_tx_b_x_pins[] = {GPIOX_16};
static const unsigned int uart_rx_b_x_pins[] = {GPIOX_17};

/* uart_ao_b */
static const unsigned int uart_ao_tx_b_z_pins[] = {GPIOZ_8};
static const unsigned int uart_ao_rx_b_z_pins[] = {GPIOZ_9};
static const unsigned int uart_ao_cts_b_z_pins[] = {GPIOZ_6};
static const unsigned int uart_ao_rts_b_z_pins[] = {GPIOZ_7};

/* pwm_a */
static const unsigned int pwm_a_z_pins[] = {GPIOZ_5};

static const unsigned int pwm_a_x18_pins[] = {GPIOX_18};
static const unsigned int pwm_a_x20_pins[] = {GPIOX_20};

static const unsigned int pwm_a_a_pins[] = {GPIOA_14};

/* pwm_b */
static const unsigned int pwm_b_z_pins[] = {GPIOZ_4};

static const unsigned int pwm_b_x_pins[] = {GPIOX_19};

static const unsigned int pwm_b_a_pins[] = {GPIOA_15};

/* pwm_c */
static const unsigned int pwm_c_x10_pins[] = {GPIOX_10};
static const unsigned int pwm_c_x17_pins[] = {GPIOX_17};

static const unsigned int pwm_c_a_pins[] = {GPIOA_16};

/* pwm_d */
static const unsigned int pwm_d_x11_pins[] = {GPIOX_11};
static const unsigned int pwm_d_x16_pins[] = {GPIOX_16};

/* pwm_vs */
static const unsigned int pwm_vs_pins[] = {GPIOA_0};

/* spdif_in */
static const unsigned int spdif_in_z_pins[] = {GPIOZ_4};

static const unsigned int spdif_in_a1_pins[] = {GPIOA_1};
static const unsigned int spdif_in_a7_pins[] = {GPIOA_7};
static const unsigned int spdif_in_a19_pins[] = {GPIOA_19};
static const unsigned int spdif_in_a20_pins[] = {GPIOA_20};

/* spdif_out */
static const unsigned int spdif_out_z_pins[] = {GPIOZ_5};

static const unsigned int spdif_out_a1_pins[] = {GPIOA_1};
static const unsigned int spdif_out_a11_pins[] = {GPIOA_11};
static const unsigned int spdif_out_a19_pins[] = {GPIOA_19};
static const unsigned int spdif_out_a20_pins[] = {GPIOA_20};

/* jtag_ee */
static const unsigned int jtag_tdo_x_pins[] = {GPIOX_0};
static const unsigned int jtag_tdi_x_pins[] = {GPIOX_1};
static const unsigned int jtag_clk_x_pins[] = {GPIOX_4};
static const unsigned int jtag_tms_x_pins[] = {GPIOX_5};

/* eth */
static const unsigned int eth_txd0_x_pins[] = {GPIOX_8};
static const unsigned int eth_txd1_x_pins[] = {GPIOX_9};
static const unsigned int eth_txen_x_pins[] = {GPIOX_10};
static const unsigned int eth_rgmii_rx_clk_x_pins[] = {GPIOX_12};
static const unsigned int eth_rxd0_x_pins[] = {GPIOX_13};
static const unsigned int eth_rxd1_x_pins[] = {GPIOX_14};
static const unsigned int eth_rx_dv_x_pins[] = {GPIOX_15};
static const unsigned int eth_mdio_x_pins[] = {GPIOX_21};
static const unsigned int eth_mdc_x_pins[] = {GPIOX_22};

static const unsigned int eth_txd0_y_pins[] = {GPIOY_10};
static const unsigned int eth_txd1_y_pins[] = {GPIOY_11};
static const unsigned int eth_txen_y_pins[] = {GPIOY_9};
static const unsigned int eth_rgmii_rx_clk_y_pins[] = {GPIOY_2};
static const unsigned int eth_rxd0_y_pins[] = {GPIOY_4};
static const unsigned int eth_rxd1_y_pins[] = {GPIOY_5};
static const unsigned int eth_rx_dv_y_pins[] = {GPIOY_3};
static const unsigned int eth_mdio_y_pins[] = {GPIOY_0};
static const unsigned int eth_mdc_y_pins[] = {GPIOY_1};

static const unsigned int eth_rxd2_rgmii_pins[] = {GPIOY_6};
static const unsigned int eth_rxd3_rgmii_pins[] = {GPIOY_7};
static const unsigned int eth_rgmii_tx_clk_pins[] = {GPIOY_8};
static const unsigned int eth_txd2_rgmii_pins[] = {GPIOY_12};
static const unsigned int eth_txd3_rgmii_pins[] = {GPIOY_13};

/* pdm */
static const unsigned int pdm_dclk_a14_pins[] = {GPIOA_14};
static const unsigned int pdm_dclk_a19_pins[] = {GPIOA_19};
static const unsigned int pdm_din0_pins[] = {GPIOA_15};
static const unsigned int pdm_din1_pins[] = {GPIOA_16};
static const unsigned int pdm_din2_pins[] = {GPIOA_17};
static const unsigned int pdm_din3_pins[] = {GPIOA_18};

/* mclk */
static const unsigned int mclk_c_pins[] = {GPIOA_0};
static const unsigned int mclk_b_pins[] = {GPIOA_1};

/* tdm */
static const unsigned int tdma_sclk_pins[] = {GPIOX_12};
static const unsigned int tdma_sclk_slv_pins[] = {GPIOX_12};
static const unsigned int tdma_fs_pins[] = {GPIOX_13};
static const unsigned int tdma_fs_slv_pins[] = {GPIOX_13};
static const unsigned int tdma_din0_pins[] = {GPIOX_14};
static const unsigned int tdma_dout0_x14_pins[] = {GPIOX_14};
static const unsigned int tdma_dout0_x15_pins[] = {GPIOX_15};
static const unsigned int tdma_dout1_pins[] = {GPIOX_15};
static const unsigned int tdma_din1_pins[] = {GPIOX_15};

static const unsigned int tdmc_sclk_pins[] = {GPIOA_2};
static const unsigned int tdmc_sclk_slv_pins[] = {GPIOA_2};
static const unsigned int tdmc_fs_pins[] = {GPIOA_3};
static const unsigned int tdmc_fs_slv_pins[] = {GPIOA_3};
static const unsigned int tdmc_din0_pins[] = {GPIOA_4};
static const unsigned int tdmc_dout0_pins[] = {GPIOA_4};
static const unsigned int tdmc_din1_pins[] = {GPIOA_5};
static const unsigned int tdmc_dout1_pins[] = {GPIOA_5};
static const unsigned int tdmc_din2_pins[] = {GPIOA_6};
static const unsigned int tdmc_dout2_pins[] = {GPIOA_6};
static const unsigned int tdmc_din3_pins[] = {GPIOA_7};
static const unsigned int tdmc_dout3_pins[] = {GPIOA_7};

static const unsigned int tdmb_sclk_pins[] = {GPIOA_8};
static const unsigned int tdmb_sclk_slv_pins[] = {GPIOA_8};
static const unsigned int tdmb_fs_pins[] = {GPIOA_9};
static const unsigned int tdmb_fs_slv_pins[] = {GPIOA_9};
static const unsigned int tdmb_din0_pins[] = {GPIOA_10};
static const unsigned int tdmb_dout0_pins[] = {GPIOA_10};
static const unsigned int tdmb_din1_pins[] = {GPIOA_11};
static const unsigned int tdmb_dout1_pins[] = {GPIOA_11};
static const unsigned int tdmb_din2_pins[] = {GPIOA_12};
static const unsigned int tdmb_dout2_pins[] = {GPIOA_12};
static const unsigned int tdmb_din3_pins[] = {GPIOA_13};
static const unsigned int tdmb_dout3_pins[] = {GPIOA_13};

static const struct meson_pmx_group meson_axg_periphs_groups[] = {
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

	GPIO_GROUP(BOOT_0),
	GPIO_GROUP(BOOT_1),
	GPIO_GROUP(BOOT_2),
	GPIO_GROUP(BOOT_3),
	GPIO_GROUP(BOOT_4),
	GPIO_GROUP(BOOT_5),
	GPIO_GROUP(BOOT_6),
	GPIO_GROUP(BOOT_7),
	GPIO_GROUP(BOOT_8),
	GPIO_GROUP(BOOT_9),
	GPIO_GROUP(BOOT_10),
	GPIO_GROUP(BOOT_11),
	GPIO_GROUP(BOOT_12),
	GPIO_GROUP(BOOT_13),
	GPIO_GROUP(BOOT_14),

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
	GPIO_GROUP(GPIOA_12),
	GPIO_GROUP(GPIOA_13),
	GPIO_GROUP(GPIOA_14),
	GPIO_GROUP(GPIOA_15),
	GPIO_GROUP(GPIOA_16),
	GPIO_GROUP(GPIOA_17),
	GPIO_GROUP(GPIOA_18),
	GPIO_GROUP(GPIOA_19),
	GPIO_GROUP(GPIOA_20),

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
	GPIO_GROUP(GPIOX_22),

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

	/* bank BOOT */
	GROUP(emmc_nand_d0, 1),
	GROUP(emmc_nand_d1, 1),
	GROUP(emmc_nand_d2, 1),
	GROUP(emmc_nand_d3, 1),
	GROUP(emmc_nand_d4, 1),
	GROUP(emmc_nand_d5, 1),
	GROUP(emmc_nand_d6, 1),
	GROUP(emmc_nand_d7, 1),
	GROUP(emmc_clk, 1),
	GROUP(emmc_cmd, 1),
	GROUP(emmc_ds, 1),
	GROUP(nand_ce0, 2),
	GROUP(nand_ale, 2),
	GROUP(nand_cle, 2),
	GROUP(nand_wen_clk, 2),
	GROUP(nand_ren_wr, 2),
	GROUP(nand_rb0, 2),
	GROUP(nor_hold, 3),
	GROUP(nor_d, 3),
	GROUP(nor_q, 3),
	GROUP(nor_c, 3),
	GROUP(nor_wp, 3),
	GROUP(nor_cs, 3),

	/* bank GPIOZ */
	GROUP(spi0_clk, 1),
	GROUP(spi0_mosi, 1),
	GROUP(spi0_miso, 1),
	GROUP(spi0_ss0, 1),
	GROUP(spi0_ss1, 1),
	GROUP(spi0_ss2, 1),
	GROUP(i2c0_sck, 1),
	GROUP(i2c0_sda, 1),
	GROUP(i2c1_sck_z, 1),
	GROUP(i2c1_sda_z, 1),
	GROUP(uart_rts_b_z, 2),
	GROUP(uart_cts_b_z, 2),
	GROUP(uart_tx_b_z, 2),
	GROUP(uart_rx_b_z, 2),
	GROUP(pwm_a_z, 2),
	GROUP(pwm_b_z, 2),
	GROUP(spdif_in_z, 3),
	GROUP(spdif_out_z, 3),
	GROUP(uart_ao_tx_b_z, 2),
	GROUP(uart_ao_rx_b_z, 2),
	GROUP(uart_ao_cts_b_z, 2),
	GROUP(uart_ao_rts_b_z, 2),

	/* bank GPIOX */
	GROUP(sdio_d0, 1),
	GROUP(sdio_d1, 1),
	GROUP(sdio_d2, 1),
	GROUP(sdio_d3, 1),
	GROUP(sdio_clk, 1),
	GROUP(sdio_cmd, 1),
	GROUP(i2c1_sck_x, 1),
	GROUP(i2c1_sda_x, 1),
	GROUP(i2c2_sck_x, 1),
	GROUP(i2c2_sda_x, 1),
	GROUP(uart_rts_a, 1),
	GROUP(uart_cts_a, 1),
	GROUP(uart_tx_a, 1),
	GROUP(uart_rx_a, 1),
	GROUP(uart_rts_b_x, 2),
	GROUP(uart_cts_b_x, 2),
	GROUP(uart_tx_b_x, 2),
	GROUP(uart_rx_b_x, 2),
	GROUP(jtag_tdo_x, 2),
	GROUP(jtag_tdi_x, 2),
	GROUP(jtag_clk_x, 2),
	GROUP(jtag_tms_x, 2),
	GROUP(spi1_clk_x, 4),
	GROUP(spi1_mosi_x, 4),
	GROUP(spi1_miso_x, 4),
	GROUP(spi1_ss0_x, 4),
	GROUP(pwm_a_x18, 3),
	GROUP(pwm_a_x20, 1),
	GROUP(pwm_b_x, 3),
	GROUP(pwm_c_x10, 3),
	GROUP(pwm_c_x17, 3),
	GROUP(pwm_d_x11, 3),
	GROUP(pwm_d_x16, 3),
	GROUP(eth_txd0_x, 4),
	GROUP(eth_txd1_x, 4),
	GROUP(eth_txen_x, 4),
	GROUP(eth_rgmii_rx_clk_x, 4),
	GROUP(eth_rxd0_x, 4),
	GROUP(eth_rxd1_x, 4),
	GROUP(eth_rx_dv_x, 4),
	GROUP(eth_mdio_x, 4),
	GROUP(eth_mdc_x, 4),
	GROUP(tdma_sclk, 1),
	GROUP(tdma_sclk_slv, 2),
	GROUP(tdma_fs, 1),
	GROUP(tdma_fs_slv, 2),
	GROUP(tdma_din0, 1),
	GROUP(tdma_dout0_x14, 2),
	GROUP(tdma_dout0_x15, 1),
	GROUP(tdma_dout1, 2),
	GROUP(tdma_din1, 3),

	/* bank GPIOY */
	GROUP(eth_txd0_y, 1),
	GROUP(eth_txd1_y, 1),
	GROUP(eth_txen_y, 1),
	GROUP(eth_rgmii_rx_clk_y, 1),
	GROUP(eth_rxd0_y, 1),
	GROUP(eth_rxd1_y, 1),
	GROUP(eth_rx_dv_y, 1),
	GROUP(eth_mdio_y, 1),
	GROUP(eth_mdc_y, 1),
	GROUP(eth_rxd2_rgmii, 1),
	GROUP(eth_rxd3_rgmii, 1),
	GROUP(eth_rgmii_tx_clk, 1),
	GROUP(eth_txd2_rgmii, 1),
	GROUP(eth_txd3_rgmii, 1),

	/* bank GPIOA */
	GROUP(spdif_out_a1, 4),
	GROUP(spdif_out_a11, 3),
	GROUP(spdif_out_a19, 2),
	GROUP(spdif_out_a20, 1),
	GROUP(spdif_in_a1, 3),
	GROUP(spdif_in_a7, 3),
	GROUP(spdif_in_a19, 1),
	GROUP(spdif_in_a20, 2),
	GROUP(spi1_clk_a, 3),
	GROUP(spi1_mosi_a, 3),
	GROUP(spi1_miso_a, 3),
	GROUP(spi1_ss0_a, 3),
	GROUP(spi1_ss1, 3),
	GROUP(pwm_a_a, 3),
	GROUP(pwm_b_a, 3),
	GROUP(pwm_c_a, 3),
	GROUP(pwm_vs, 2),
	GROUP(i2c2_sda_a, 3),
	GROUP(i2c2_sck_a, 3),
	GROUP(i2c3_sda_a6, 4),
	GROUP(i2c3_sck_a7, 4),
	GROUP(i2c3_sda_a12, 4),
	GROUP(i2c3_sck_a13, 4),
	GROUP(i2c3_sda_a19, 4),
	GROUP(i2c3_sck_a20, 4),
	GROUP(pdm_dclk_a14, 1),
	GROUP(pdm_dclk_a19, 3),
	GROUP(pdm_din0, 1),
	GROUP(pdm_din1, 1),
	GROUP(pdm_din2, 1),
	GROUP(pdm_din3, 1),
	GROUP(mclk_c, 1),
	GROUP(mclk_b, 1),
	GROUP(tdmc_sclk, 1),
	GROUP(tdmc_sclk_slv, 2),
	GROUP(tdmc_fs, 1),
	GROUP(tdmc_fs_slv, 2),
	GROUP(tdmc_din0, 2),
	GROUP(tdmc_dout0, 1),
	GROUP(tdmc_din1, 2),
	GROUP(tdmc_dout1, 1),
	GROUP(tdmc_din2, 2),
	GROUP(tdmc_dout2, 1),
	GROUP(tdmc_din3, 2),
	GROUP(tdmc_dout3, 1),
	GROUP(tdmb_sclk, 1),
	GROUP(tdmb_sclk_slv, 2),
	GROUP(tdmb_fs, 1),
	GROUP(tdmb_fs_slv, 2),
	GROUP(tdmb_din0, 2),
	GROUP(tdmb_dout0, 1),
	GROUP(tdmb_din1, 2),
	GROUP(tdmb_dout1, 1),
	GROUP(tdmb_din2, 2),
	GROUP(tdmb_dout2, 1),
	GROUP(tdmb_din3, 2),
	GROUP(tdmb_dout3, 1),
};

/* uart_ao_a */
static const unsigned int uart_ao_tx_a_pins[] = {GPIOAO_0};
static const unsigned int uart_ao_rx_a_pins[] = {GPIOAO_1};
static const unsigned int uart_ao_cts_a_pins[] = {GPIOAO_2};
static const unsigned int uart_ao_rts_a_pins[] = {GPIOAO_3};

/* uart_ao_b */
static const unsigned int uart_ao_tx_b_pins[] = {GPIOAO_4};
static const unsigned int uart_ao_rx_b_pins[] = {GPIOAO_5};
static const unsigned int uart_ao_cts_b_pins[] = {GPIOAO_2};
static const unsigned int uart_ao_rts_b_pins[] = {GPIOAO_3};

/* i2c_ao */
static const unsigned int i2c_ao_sck_4_pins[] = {GPIOAO_4};
static const unsigned int i2c_ao_sda_5_pins[] = {GPIOAO_5};
static const unsigned int i2c_ao_sck_8_pins[] = {GPIOAO_8};
static const unsigned int i2c_ao_sda_9_pins[] = {GPIOAO_9};
static const unsigned int i2c_ao_sck_10_pins[] = {GPIOAO_10};
static const unsigned int i2c_ao_sda_11_pins[] = {GPIOAO_11};

/* i2c_ao_slave */
static const unsigned int i2c_ao_slave_sck_pins[] = {GPIOAO_10};
static const unsigned int i2c_ao_slave_sda_pins[] = {GPIOAO_11};

/* ir_in */
static const unsigned int remote_input_ao_pins[] = {GPIOAO_6};

/* ir_out */
static const unsigned int remote_out_ao_pins[] = {GPIOAO_7};

/* pwm_ao_a */
static const unsigned int pwm_ao_a_pins[] = {GPIOAO_3};

/* pwm_ao_b */
static const unsigned int pwm_ao_b_ao2_pins[] = {GPIOAO_2};
static const unsigned int pwm_ao_b_ao12_pins[] = {GPIOAO_12};

/* pwm_ao_c */
static const unsigned int pwm_ao_c_ao8_pins[] = {GPIOAO_8};
static const unsigned int pwm_ao_c_ao13_pins[] = {GPIOAO_13};

/* pwm_ao_d */
static const unsigned int pwm_ao_d_pins[] = {GPIOAO_9};

/* jtag_ao */
static const unsigned int jtag_ao_tdi_pins[] = {GPIOAO_3};
static const unsigned int jtag_ao_tdo_pins[] = {GPIOAO_4};
static const unsigned int jtag_ao_clk_pins[] = {GPIOAO_5};
static const unsigned int jtag_ao_tms_pins[] = {GPIOAO_7};

/* gen_clk */
static const unsigned int gen_clk_ee_pins[] = {GPIOAO_13};

static const struct meson_pmx_group meson_axg_aobus_groups[] = {
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
	GPIO_GROUP(GPIO_TEST_N),

	/* bank AO */
	GROUP(uart_ao_tx_a, 1),
	GROUP(uart_ao_rx_a, 1),
	GROUP(uart_ao_cts_a, 2),
	GROUP(uart_ao_rts_a, 2),
	GROUP(uart_ao_tx_b, 1),
	GROUP(uart_ao_rx_b, 1),
	GROUP(uart_ao_cts_b, 1),
	GROUP(uart_ao_rts_b, 1),
	GROUP(i2c_ao_sck_4, 2),
	GROUP(i2c_ao_sda_5, 2),
	GROUP(i2c_ao_sck_8, 2),
	GROUP(i2c_ao_sda_9, 2),
	GROUP(i2c_ao_sck_10, 2),
	GROUP(i2c_ao_sda_11, 2),
	GROUP(i2c_ao_slave_sck, 1),
	GROUP(i2c_ao_slave_sda, 1),
	GROUP(remote_input_ao, 1),
	GROUP(remote_out_ao, 1),
	GROUP(pwm_ao_a, 3),
	GROUP(pwm_ao_b_ao2, 3),
	GROUP(pwm_ao_b_ao12, 3),
	GROUP(pwm_ao_c_ao8, 3),
	GROUP(pwm_ao_c_ao13, 3),
	GROUP(pwm_ao_d, 3),
	GROUP(jtag_ao_tdi, 4),
	GROUP(jtag_ao_tdo, 4),
	GROUP(jtag_ao_clk, 4),
	GROUP(jtag_ao_tms, 4),
	GROUP(gen_clk_ee, 4),
};

static const char * const gpio_periphs_groups[] = {
	"GPIOZ_0", "GPIOZ_1", "GPIOZ_2", "GPIOZ_3", "GPIOZ_4",
	"GPIOZ_5", "GPIOZ_6", "GPIOZ_7", "GPIOZ_8", "GPIOZ_9",
	"GPIOZ_10",

	"BOOT_0", "BOOT_1", "BOOT_2", "BOOT_3", "BOOT_4",
	"BOOT_5", "BOOT_6", "BOOT_7", "BOOT_8", "BOOT_9",
	"BOOT_10", "BOOT_11", "BOOT_12", "BOOT_13", "BOOT_14",

	"GPIOA_0", "GPIOA_1", "GPIOA_2", "GPIOA_3", "GPIOA_4",
	"GPIOA_5", "GPIOA_6", "GPIOA_7", "GPIOA_8", "GPIOA_9",
	"GPIOA_10", "GPIOA_11", "GPIOA_12", "GPIOA_13", "GPIOA_14",
	"GPIOA_15", "GPIOA_16", "GPIOA_17", "GPIOA_18", "GPIOA_19",
	"GPIOA_20",

	"GPIOX_0", "GPIOX_1", "GPIOX_2", "GPIOX_3", "GPIOX_4",
	"GPIOX_5", "GPIOX_6", "GPIOX_7", "GPIOX_8", "GPIOX_9",
	"GPIOX_10", "GPIOX_11", "GPIOX_12", "GPIOX_13", "GPIOX_14",
	"GPIOX_15", "GPIOX_16", "GPIOX_17", "GPIOX_18", "GPIOX_19",
	"GPIOX_20", "GPIOX_21", "GPIOX_22",

	"GPIOY_0", "GPIOY_1", "GPIOY_2", "GPIOY_3", "GPIOY_4",
	"GPIOY_5", "GPIOY_6", "GPIOY_7", "GPIOY_8", "GPIOY_9",
	"GPIOY_10", "GPIOY_11", "GPIOY_12", "GPIOY_13", "GPIOY_14",
	"GPIOY_15",
};

static const char * const emmc_groups[] = {
	"emmc_nand_d0", "emmc_nand_d1", "emmc_nand_d2",
	"emmc_nand_d3", "emmc_nand_d4", "emmc_nand_d5",
	"emmc_nand_d6", "emmc_nand_d7",
	"emmc_clk", "emmc_cmd", "emmc_ds",
};

static const char * const nand_groups[] = {
	"emmc_nand_d0", "emmc_nand_d1", "emmc_nand_d2",
	"emmc_nand_d3", "emmc_nand_d4", "emmc_nand_d5",
	"emmc_nand_d6", "emmc_nand_d7",
	"nand_ce0", "nand_ale", "nand_cle",
	"nand_wen_clk", "nand_ren_wr", "nand_rb0",
};

static const char * const nor_groups[] = {
	"nor_d", "nor_q", "nor_c", "nor_cs",
	"nor_hold", "nor_wp",
};

static const char * const sdio_groups[] = {
	"sdio_d0", "sdio_d1", "sdio_d2", "sdio_d3",
	"sdio_cmd", "sdio_clk",
};

static const char * const spi0_groups[] = {
	"spi0_clk", "spi0_mosi", "spi0_miso", "spi0_ss0",
	"spi0_ss1", "spi0_ss2"
};

static const char * const spi1_groups[] = {
	"spi1_clk_x", "spi1_mosi_x", "spi1_miso_x", "spi1_ss0_x",
	"spi1_clk_a", "spi1_mosi_a", "spi1_miso_a", "spi1_ss0_a",
	"spi1_ss1"
};

static const char * const uart_a_groups[] = {
	"uart_tx_a", "uart_rx_a", "uart_cts_a", "uart_rts_a",
};

static const char * const uart_b_groups[] = {
	"uart_tx_b_z", "uart_rx_b_z", "uart_cts_b_z", "uart_rts_b_z",
	"uart_tx_b_x", "uart_rx_b_x", "uart_cts_b_x", "uart_rts_b_x",
};

static const char * const uart_ao_b_z_groups[] = {
	"uart_ao_tx_b_z", "uart_ao_rx_b_z",
	"uart_ao_cts_b_z", "uart_ao_rts_b_z",
};

static const char * const i2c0_groups[] = {
	"i2c0_sck", "i2c0_sda",
};

static const char * const i2c1_groups[] = {
	"i2c1_sck_z", "i2c1_sda_z",
	"i2c1_sck_x", "i2c1_sda_x",
};

static const char * const i2c2_groups[] = {
	"i2c2_sck_x", "i2c2_sda_x",
	"i2c2_sda_a", "i2c2_sck_a",
};

static const char * const i2c3_groups[] = {
	"i2c3_sda_a6", "i2c3_sck_a7",
	"i2c3_sda_a12", "i2c3_sck_a13",
	"i2c3_sda_a19", "i2c3_sck_a20",
};

static const char * const eth_groups[] = {
	"eth_rxd2_rgmii", "eth_rxd3_rgmii", "eth_rgmii_tx_clk",
	"eth_txd2_rgmii", "eth_txd3_rgmii",
	"eth_txd0_x", "eth_txd1_x", "eth_txen_x", "eth_rgmii_rx_clk_x",
	"eth_rxd0_x", "eth_rxd1_x", "eth_rx_dv_x", "eth_mdio_x",
	"eth_mdc_x",
	"eth_txd0_y", "eth_txd1_y", "eth_txen_y", "eth_rgmii_rx_clk_y",
	"eth_rxd0_y", "eth_rxd1_y", "eth_rx_dv_y", "eth_mdio_y",
	"eth_mdc_y",
};

static const char * const pwm_a_groups[] = {
	"pwm_a_z", "pwm_a_x18", "pwm_a_x20", "pwm_a_a",
};

static const char * const pwm_b_groups[] = {
	"pwm_b_z", "pwm_b_x", "pwm_b_a",
};

static const char * const pwm_c_groups[] = {
	"pwm_c_x10", "pwm_c_x17", "pwm_c_a",
};

static const char * const pwm_d_groups[] = {
	"pwm_d_x11", "pwm_d_x16",
};

static const char * const pwm_vs_groups[] = {
	"pwm_vs",
};

static const char * const spdif_out_groups[] = {
	"spdif_out_z", "spdif_out_a1", "spdif_out_a11",
	"spdif_out_a19", "spdif_out_a20",
};

static const char * const spdif_in_groups[] = {
	"spdif_in_z", "spdif_in_a1", "spdif_in_a7",
	"spdif_in_a19", "spdif_in_a20",
};

static const char * const jtag_ee_groups[] = {
	"jtag_tdo_x", "jtag_tdi_x", "jtag_clk_x",
	"jtag_tms_x",
};

static const char * const pdm_groups[] = {
	"pdm_din0", "pdm_din1", "pdm_din2", "pdm_din3",
	"pdm_dclk_a14", "pdm_dclk_a19",
};

static const char * const gpio_aobus_groups[] = {
	"GPIOAO_0", "GPIOAO_1", "GPIOAO_2", "GPIOAO_3", "GPIOAO_4",
	"GPIOAO_5", "GPIOAO_6", "GPIOAO_7", "GPIOAO_8", "GPIOAO_9",
	"GPIOAO_10", "GPIOAO_11", "GPIOAO_12", "GPIOAO_13",
	"GPIO_TEST_N",
};

static const char * const uart_ao_a_groups[] = {
	"uart_ao_tx_a", "uart_ao_rx_a", "uart_ao_cts_a", "uart_ao_rts_a",
};

static const char * const uart_ao_b_groups[] = {
	"uart_ao_tx_b", "uart_ao_rx_b", "uart_ao_cts_b", "uart_ao_rts_b",
};

static const char * const i2c_ao_groups[] = {
	"i2c_ao_sck_4", "i2c_ao_sda_5",
	"i2c_ao_sck_8", "i2c_ao_sda_9",
	"i2c_ao_sck_10", "i2c_ao_sda_11",
};

static const char * const i2c_ao_slave_groups[] = {
	"i2c_ao_slave_sck", "i2c_ao_slave_sda",
};

static const char * const remote_input_ao_groups[] = {
	"remote_input_ao",
};

static const char * const remote_out_ao_groups[] = {
	"remote_out_ao",
};

static const char * const pwm_ao_a_groups[] = {
	"pwm_ao_a",
};

static const char * const pwm_ao_b_groups[] = {
	"pwm_ao_b_ao2", "pwm_ao_b_ao12",
};

static const char * const pwm_ao_c_groups[] = {
	"pwm_ao_c_ao8", "pwm_ao_c_ao13",
};

static const char * const pwm_ao_d_groups[] = {
	"pwm_ao_d",
};

static const char * const jtag_ao_groups[] = {
	"jtag_ao_tdi", "jtag_ao_tdo", "jtag_ao_clk", "jtag_ao_tms",
};

static const char * const mclk_c_groups[] = {
	"mclk_c",
};

static const char * const mclk_b_groups[] = {
	"mclk_b",
};

static const char * const tdma_groups[] = {
	"tdma_sclk", "tdma_sclk_slv", "tdma_fs", "tdma_fs_slv",
	"tdma_din0", "tdma_dout0_x14", "tdma_dout0_x15", "tdma_dout1",
	"tdma_din1",
};

static const char * const tdmc_groups[] = {
	"tdmc_sclk", "tdmc_sclk_slv", "tdmc_fs", "tdmc_fs_slv",
	"tdmc_din0", "tdmc_dout0", "tdmc_din1",	"tdmc_dout1",
	"tdmc_din2", "tdmc_dout2", "tdmc_din3",	"tdmc_dout3",
};

static const char * const tdmb_groups[] = {
	"tdmb_sclk", "tdmb_sclk_slv", "tdmb_fs", "tdmb_fs_slv",
	"tdmb_din0", "tdmb_dout0", "tdmb_din1",	"tdmb_dout1",
	"tdmb_din2", "tdmb_dout2", "tdmb_din3",	"tdmb_dout3",
};

static const char * const gen_clk_ee_groups[] = {
	"gen_clk_ee",
};

static const struct meson_pmx_func meson_axg_periphs_functions[] = {
	FUNCTION(gpio_periphs),
	FUNCTION(emmc),
	FUNCTION(nor),
	FUNCTION(spi0),
	FUNCTION(spi1),
	FUNCTION(sdio),
	FUNCTION(nand),
	FUNCTION(uart_a),
	FUNCTION(uart_b),
	FUNCTION(uart_ao_b_z),
	FUNCTION(i2c0),
	FUNCTION(i2c1),
	FUNCTION(i2c2),
	FUNCTION(i2c3),
	FUNCTION(eth),
	FUNCTION(pwm_a),
	FUNCTION(pwm_b),
	FUNCTION(pwm_c),
	FUNCTION(pwm_d),
	FUNCTION(pwm_vs),
	FUNCTION(spdif_out),
	FUNCTION(spdif_in),
	FUNCTION(jtag_ee),
	FUNCTION(pdm),
	FUNCTION(mclk_b),
	FUNCTION(mclk_c),
	FUNCTION(tdma),
	FUNCTION(tdmb),
	FUNCTION(tdmc),
};

static const struct meson_pmx_func meson_axg_aobus_functions[] = {
	FUNCTION(gpio_aobus),
	FUNCTION(uart_ao_a),
	FUNCTION(uart_ao_b),
	FUNCTION(i2c_ao),
	FUNCTION(i2c_ao_slave),
	FUNCTION(remote_input_ao),
	FUNCTION(remote_out_ao),
	FUNCTION(pwm_ao_a),
	FUNCTION(pwm_ao_b),
	FUNCTION(pwm_ao_c),
	FUNCTION(pwm_ao_d),
	FUNCTION(jtag_ao),
	FUNCTION(gen_clk_ee),
};

static const struct meson_bank meson_axg_periphs_banks[] = {
	/*   name    first      last       irq	     pullen  pull    dir     out     in  */
	BANK("Z",    GPIOZ_0,	GPIOZ_10, 14,  24, 3,  0,  3,  0,  9,  0,  10, 0,  11, 0),
	BANK("BOOT", BOOT_0,	BOOT_14,  25,  39, 4,  0,  4,  0,  12, 0,  13, 0,  14, 0),
	BANK("A",    GPIOA_0,	GPIOA_20, 40,  60, 0,  0,  0,  0,  0,  0,  1,  0,  2,  0),
	BANK("X",    GPIOX_0,	GPIOX_22, 61,  83, 2,  0,  2,  0,  6,  0,  7,  0,  8,  0),
	BANK("Y", 	 GPIOY_0,	GPIOY_15, 84,  99, 1,  0,  1,  0,  3,  0,  4,  0,  5,  0),
};

static const struct meson_bank meson_axg_aobus_banks[] = {
	/*   name    first      last      irq	pullen  pull    dir     out     in  */
	BANK("AO",   GPIOAO_0,  GPIOAO_13, 0, 13, 0,  16,  0, 0,  0,  0,  0, 16,  1,  0),
};

static const struct meson_pmx_bank meson_axg_periphs_pmx_banks[] = {
	/*	 name	 first		lask	   reg	offset  */
	BANK_PMX("Z",	 GPIOZ_0, GPIOZ_10, 0x2, 0),
	BANK_PMX("BOOT", BOOT_0,  BOOT_14,  0x0, 0),
	BANK_PMX("A",	 GPIOA_0, GPIOA_20, 0xb, 0),
	BANK_PMX("X",	 GPIOX_0, GPIOX_22, 0x4, 0),
	BANK_PMX("Y",	 GPIOY_0, GPIOY_15, 0x8, 0),
};

static const struct meson_axg_pmx_data meson_axg_periphs_pmx_banks_data = {
	.pmx_banks	= meson_axg_periphs_pmx_banks,
	.num_pmx_banks = ARRAY_SIZE(meson_axg_periphs_pmx_banks),
};

static const struct meson_pmx_bank meson_axg_aobus_pmx_banks[] = {
	BANK_PMX("AO", GPIOAO_0, GPIOAO_13, 0x0, 0),
};

static const struct meson_axg_pmx_data meson_axg_aobus_pmx_banks_data = {
	.pmx_banks	= meson_axg_aobus_pmx_banks,
	.num_pmx_banks = ARRAY_SIZE(meson_axg_aobus_pmx_banks),
};

static const struct meson_pinctrl_data meson_axg_periphs_pinctrl_data = {
	.name		= "periphs-banks",
	.pins		= meson_axg_periphs_pins,
	.groups		= meson_axg_periphs_groups,
	.funcs		= meson_axg_periphs_functions,
	.banks		= meson_axg_periphs_banks,
	.num_pins	= ARRAY_SIZE(meson_axg_periphs_pins),
	.num_groups	= ARRAY_SIZE(meson_axg_periphs_groups),
	.num_funcs	= ARRAY_SIZE(meson_axg_periphs_functions),
	.num_banks	= ARRAY_SIZE(meson_axg_periphs_banks),
	.pmx_ops	= &meson_axg_pmx_ops,
	.pmx_data	= &meson_axg_periphs_pmx_banks_data,
};

static const struct meson_pinctrl_data meson_axg_aobus_pinctrl_data = {
	.name		= "aobus-banks",
	.pins		= meson_axg_aobus_pins,
	.groups		= meson_axg_aobus_groups,
	.funcs		= meson_axg_aobus_functions,
	.banks		= meson_axg_aobus_banks,
	.num_pins	= ARRAY_SIZE(meson_axg_aobus_pins),
	.num_groups	= ARRAY_SIZE(meson_axg_aobus_groups),
	.num_funcs	= ARRAY_SIZE(meson_axg_aobus_functions),
	.num_banks	= ARRAY_SIZE(meson_axg_aobus_banks),
	.pmx_ops	= &meson_axg_pmx_ops,
	.pmx_data	= &meson_axg_aobus_pmx_banks_data,
	.parse_dt	= meson8_aobus_parse_dt_extra,
};

static const struct of_device_id meson_axg_pinctrl_dt_match[] = {
	{
		.compatible = "amlogic,meson-axg-periphs-pinctrl",
		.data = &meson_axg_periphs_pinctrl_data,
	},
	{
		.compatible = "amlogic,meson-axg-aobus-pinctrl",
		.data = &meson_axg_aobus_pinctrl_data,
	},
	{ },
};
MODULE_DEVICE_TABLE(of, meson_axg_pinctrl_dt_match);

static struct platform_driver meson_axg_pinctrl_driver = {
	.probe		= meson_pinctrl_probe,
	.driver = {
		.name	= "meson-axg-pinctrl",
		.of_match_table = meson_axg_pinctrl_dt_match,
	},
};

module_platform_driver(meson_axg_pinctrl_driver);
MODULE_DESCRIPTION("Amlogic Meson AXG pinctrl driver");
MODULE_LICENSE("Dual BSD/GPL");
