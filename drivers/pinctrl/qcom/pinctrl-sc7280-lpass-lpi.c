// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * ALSA SoC platform-machine driver for QTi LPASS
 */

#include <linux/gpio/driver.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include "pinctrl-lpass-lpi.h"

enum lpass_lpi_functions {
	LPI_MUX_dmic1_clk,
	LPI_MUX_dmic1_data,
	LPI_MUX_dmic2_clk,
	LPI_MUX_dmic2_data,
	LPI_MUX_dmic3_clk,
	LPI_MUX_dmic3_data,
	LPI_MUX_i2s1_clk,
	LPI_MUX_i2s1_data,
	LPI_MUX_i2s1_ws,
	LPI_MUX_i2s2_clk,
	LPI_MUX_i2s2_data,
	LPI_MUX_i2s2_ws,
	LPI_MUX_qua_mi2s_data,
	LPI_MUX_qua_mi2s_sclk,
	LPI_MUX_qua_mi2s_ws,
	LPI_MUX_swr_rx_clk,
	LPI_MUX_swr_rx_data,
	LPI_MUX_swr_tx_clk,
	LPI_MUX_swr_tx_data,
	LPI_MUX_wsa_swr_clk,
	LPI_MUX_wsa_swr_data,
	LPI_MUX_gpio,
	LPI_MUX__,
};

static int gpio0_pins[] = { 0 };
static int gpio1_pins[] = { 1 };
static int gpio2_pins[] = { 2 };
static int gpio3_pins[] = { 3 };
static int gpio4_pins[] = { 4 };
static int gpio5_pins[] = { 5 };
static int gpio6_pins[] = { 6 };
static int gpio7_pins[] = { 7 };
static int gpio8_pins[] = { 8 };
static int gpio9_pins[] = { 9 };
static int gpio10_pins[] = { 10 };
static int gpio11_pins[] = { 11 };
static int gpio12_pins[] = { 12 };
static int gpio13_pins[] = { 13 };
static int gpio14_pins[] = { 14 };

static const struct pinctrl_pin_desc sc7280_lpi_pins[] = {
	PINCTRL_PIN(0, "gpio0"),
	PINCTRL_PIN(1, "gpio1"),
	PINCTRL_PIN(2, "gpio2"),
	PINCTRL_PIN(3, "gpio3"),
	PINCTRL_PIN(4, "gpio4"),
	PINCTRL_PIN(5, "gpio5"),
	PINCTRL_PIN(6, "gpio6"),
	PINCTRL_PIN(7, "gpio7"),
	PINCTRL_PIN(8, "gpio8"),
	PINCTRL_PIN(9, "gpio9"),
	PINCTRL_PIN(10, "gpio10"),
	PINCTRL_PIN(11, "gpio11"),
	PINCTRL_PIN(12, "gpio12"),
	PINCTRL_PIN(13, "gpio13"),
	PINCTRL_PIN(14, "gpio14"),
};

static const char * const swr_tx_clk_groups[] = { "gpio0" };
static const char * const swr_tx_data_groups[] = { "gpio1", "gpio2", "gpio14" };
static const char * const swr_rx_clk_groups[] = { "gpio3" };
static const char * const swr_rx_data_groups[] = { "gpio4", "gpio5" };
static const char * const dmic1_clk_groups[] = { "gpio6" };
static const char * const dmic1_data_groups[] = { "gpio7" };
static const char * const dmic2_clk_groups[] = { "gpio8" };
static const char * const dmic2_data_groups[] = { "gpio9" };
static const char * const i2s2_clk_groups[] = { "gpio10" };
static const char * const i2s2_ws_groups[] = { "gpio11" };
static const char * const dmic3_clk_groups[] = { "gpio12" };
static const char * const dmic3_data_groups[] = { "gpio13" };
static const char * const qua_mi2s_sclk_groups[] = { "gpio0" };
static const char * const qua_mi2s_ws_groups[] = { "gpio1" };
static const char * const qua_mi2s_data_groups[] = { "gpio2", "gpio3", "gpio4" };
static const char * const i2s1_clk_groups[] = { "gpio6" };
static const char * const i2s1_ws_groups[] = { "gpio7" };
static const char * const i2s1_data_groups[] = { "gpio8", "gpio9" };
static const char * const wsa_swr_clk_groups[] = { "gpio10" };
static const char * const wsa_swr_data_groups[] = { "gpio11" };
static const char * const i2s2_data_groups[] = { "gpio12", "gpio13" };

static const struct lpi_pingroup sc7280_groups[] = {
	LPI_PINGROUP(0, 0, swr_tx_clk, qua_mi2s_sclk, _, _),
	LPI_PINGROUP(1, 2, swr_tx_data, qua_mi2s_ws, _, _),
	LPI_PINGROUP(2, 4, swr_tx_data, qua_mi2s_data, _, _),
	LPI_PINGROUP(3, 8, swr_rx_clk, qua_mi2s_data, _, _),
	LPI_PINGROUP(4, 10, swr_rx_data, qua_mi2s_data, _, _),
	LPI_PINGROUP(5, 12, swr_rx_data, _, _, _),
	LPI_PINGROUP(6, LPI_NO_SLEW, dmic1_clk, i2s1_clk, _,  _),
	LPI_PINGROUP(7, LPI_NO_SLEW, dmic1_data, i2s1_ws, _, _),
	LPI_PINGROUP(8, LPI_NO_SLEW, dmic2_clk, i2s1_data, _, _),
	LPI_PINGROUP(9, LPI_NO_SLEW, dmic2_data, i2s1_data, _, _),
	LPI_PINGROUP(10, 16, i2s2_clk, wsa_swr_clk, _, _),
	LPI_PINGROUP(11, 18, i2s2_ws, wsa_swr_data, _, _),
	LPI_PINGROUP(12, LPI_NO_SLEW, dmic3_clk, i2s2_data, _, _),
	LPI_PINGROUP(13, LPI_NO_SLEW, dmic3_data, i2s2_data, _, _),
	LPI_PINGROUP(14, 6, swr_tx_data, _, _, _),
};

static const struct lpi_function sc7280_functions[] = {
	LPI_FUNCTION(dmic1_clk),
	LPI_FUNCTION(dmic1_data),
	LPI_FUNCTION(dmic2_clk),
	LPI_FUNCTION(dmic2_data),
	LPI_FUNCTION(dmic3_clk),
	LPI_FUNCTION(dmic3_data),
	LPI_FUNCTION(i2s1_clk),
	LPI_FUNCTION(i2s1_data),
	LPI_FUNCTION(i2s1_ws),
	LPI_FUNCTION(i2s2_clk),
	LPI_FUNCTION(i2s2_data),
	LPI_FUNCTION(i2s2_ws),
	LPI_FUNCTION(qua_mi2s_data),
	LPI_FUNCTION(qua_mi2s_sclk),
	LPI_FUNCTION(qua_mi2s_ws),
	LPI_FUNCTION(swr_rx_clk),
	LPI_FUNCTION(swr_rx_data),
	LPI_FUNCTION(swr_tx_clk),
	LPI_FUNCTION(swr_tx_data),
	LPI_FUNCTION(wsa_swr_clk),
	LPI_FUNCTION(wsa_swr_data),
};

static const struct lpi_pinctrl_variant_data sc7280_lpi_data = {
	.pins = sc7280_lpi_pins,
	.npins = ARRAY_SIZE(sc7280_lpi_pins),
	.groups = sc7280_groups,
	.ngroups = ARRAY_SIZE(sc7280_groups),
	.functions = sc7280_functions,
	.nfunctions = ARRAY_SIZE(sc7280_functions),
};

static const struct of_device_id lpi_pinctrl_of_match[] = {
	{
	       .compatible = "qcom,sc7280-lpass-lpi-pinctrl",
	       .data = &sc7280_lpi_data,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, lpi_pinctrl_of_match);

static struct platform_driver lpi_pinctrl_driver = {
	.driver = {
		   .name = "qcom-sc7280-lpass-lpi-pinctrl",
		   .of_match_table = lpi_pinctrl_of_match,
	},
	.probe = lpi_pinctrl_probe,
	.remove = lpi_pinctrl_remove,
};

module_platform_driver(lpi_pinctrl_driver);
MODULE_DESCRIPTION("QTI SC7280 LPI GPIO pin control driver");
MODULE_LICENSE("GPL");
