// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016-2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2020, 2023 Linaro Ltd.
 */

#include <linux/gpio/driver.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include "pinctrl-lpass-lpi.h"

enum lpass_lpi_functions {
	LPI_MUX_dmic01_clk,
	LPI_MUX_dmic01_data,
	LPI_MUX_dmic23_clk,
	LPI_MUX_dmic23_data,
	LPI_MUX_i2s1_clk,
	LPI_MUX_i2s1_data,
	LPI_MUX_i2s1_ws,
	LPI_MUX_i2s2_clk,
	LPI_MUX_i2s2_data,
	LPI_MUX_i2s2_ws,
	LPI_MUX_i2s3_clk,
	LPI_MUX_i2s3_data,
	LPI_MUX_i2s3_ws,
	LPI_MUX_qua_mi2s_data,
	LPI_MUX_qua_mi2s_sclk,
	LPI_MUX_qua_mi2s_ws,
	LPI_MUX_swr_rx_clk,
	LPI_MUX_swr_rx_data,
	LPI_MUX_swr_tx_clk,
	LPI_MUX_swr_tx_data,
	LPI_MUX_wsa_mclk,
	LPI_MUX_gpio,
	LPI_MUX__,
};

static const struct pinctrl_pin_desc sm6115_lpi_pins[] = {
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
	PINCTRL_PIN(15, "gpio15"),
	PINCTRL_PIN(16, "gpio16"),
	PINCTRL_PIN(17, "gpio17"),
	PINCTRL_PIN(18, "gpio18"),
};

static const char * const dmic01_clk_groups[] = { "gpio6" };
static const char * const dmic01_data_groups[] = { "gpio7" };
static const char * const dmic23_clk_groups[] = { "gpio8" };
static const char * const dmic23_data_groups[] = { "gpio9" };
static const char * const i2s1_clk_groups[] = { "gpio6" };
static const char * const i2s1_data_groups[] = { "gpio8", "gpio9" };
static const char * const i2s1_ws_groups[] = { "gpio7" };
static const char * const i2s2_clk_groups[] = { "gpio10" };
static const char * const i2s2_data_groups[] = { "gpio12", "gpio13" };
static const char * const i2s2_ws_groups[] = { "gpio11" };
static const char * const i2s3_clk_groups[] = { "gpio14" };
static const char * const i2s3_data_groups[] = { "gpio16", "gpio17" };
static const char * const i2s3_ws_groups[] = { "gpio15" };
static const char * const qua_mi2s_data_groups[] = { "gpio2", "gpio3", "gpio4", "gpio5" };
static const char * const qua_mi2s_sclk_groups[] = { "gpio0" };
static const char * const qua_mi2s_ws_groups[] = { "gpio1" };
static const char * const swr_rx_clk_groups[] = { "gpio3" };
static const char * const swr_rx_data_groups[] = { "gpio4", "gpio5" };
static const char * const swr_tx_clk_groups[] = { "gpio0" };
static const char * const swr_tx_data_groups[] = { "gpio1", "gpio2" };
static const char * const wsa_mclk_groups[] = { "gpio18" };

static const struct lpi_pingroup sm6115_groups[] = {
	LPI_PINGROUP(0, 0, swr_tx_clk, qua_mi2s_sclk, _, _),
	LPI_PINGROUP(1, 2, swr_tx_data, qua_mi2s_ws, _, _),
	LPI_PINGROUP(2, 4, swr_tx_data, qua_mi2s_data, _, _),
	LPI_PINGROUP(3, 8, swr_rx_clk, qua_mi2s_data, _, _),
	LPI_PINGROUP(4, 10, swr_rx_data, qua_mi2s_data, _, _),
	LPI_PINGROUP(5, 12, swr_rx_data, _, qua_mi2s_data, _),
	LPI_PINGROUP(6, LPI_NO_SLEW, dmic01_clk, i2s1_clk, _, _),
	LPI_PINGROUP(7, LPI_NO_SLEW, dmic01_data, i2s1_ws, _, _),
	LPI_PINGROUP(8, LPI_NO_SLEW, dmic23_clk, i2s1_data, _, _),
	LPI_PINGROUP(9, LPI_NO_SLEW, dmic23_data, i2s1_data, _, _),
	LPI_PINGROUP(10, LPI_NO_SLEW, i2s2_clk, _, _, _),
	LPI_PINGROUP(11, LPI_NO_SLEW, i2s2_ws, _, _, _),
	LPI_PINGROUP(12, LPI_NO_SLEW, _, i2s2_data, _, _),
	LPI_PINGROUP(13, LPI_NO_SLEW, _, i2s2_data, _, _),
	LPI_PINGROUP(14, LPI_NO_SLEW, i2s3_clk, _, _, _),
	LPI_PINGROUP(15, LPI_NO_SLEW, i2s3_ws, _, _, _),
	LPI_PINGROUP(16, LPI_NO_SLEW, i2s3_data, _, _, _),
	LPI_PINGROUP(17, LPI_NO_SLEW, i2s3_data, _, _, _),
	LPI_PINGROUP(18, 14, wsa_mclk, _, _, _),
};

static const struct lpi_function sm6115_functions[] = {
	LPI_FUNCTION(dmic01_clk),
	LPI_FUNCTION(dmic01_data),
	LPI_FUNCTION(dmic23_clk),
	LPI_FUNCTION(dmic23_data),
	LPI_FUNCTION(i2s1_clk),
	LPI_FUNCTION(i2s1_data),
	LPI_FUNCTION(i2s1_ws),
	LPI_FUNCTION(i2s2_clk),
	LPI_FUNCTION(i2s2_data),
	LPI_FUNCTION(i2s2_ws),
	LPI_FUNCTION(i2s3_clk),
	LPI_FUNCTION(i2s3_data),
	LPI_FUNCTION(i2s3_ws),
	LPI_FUNCTION(qua_mi2s_data),
	LPI_FUNCTION(qua_mi2s_sclk),
	LPI_FUNCTION(qua_mi2s_ws),
	LPI_FUNCTION(swr_rx_clk),
	LPI_FUNCTION(swr_rx_data),
	LPI_FUNCTION(swr_tx_clk),
	LPI_FUNCTION(swr_tx_data),
	LPI_FUNCTION(wsa_mclk),
};

static const struct lpi_pinctrl_variant_data sm6115_lpi_data = {
	.pins = sm6115_lpi_pins,
	.npins = ARRAY_SIZE(sm6115_lpi_pins),
	.groups = sm6115_groups,
	.ngroups = ARRAY_SIZE(sm6115_groups),
	.functions = sm6115_functions,
	.nfunctions = ARRAY_SIZE(sm6115_functions),
};

static const struct of_device_id lpi_pinctrl_of_match[] = {
	{ .compatible = "qcom,sm6115-lpass-lpi-pinctrl", .data = &sm6115_lpi_data },
	{ }
};
MODULE_DEVICE_TABLE(of, lpi_pinctrl_of_match);

static struct platform_driver lpi_pinctrl_driver = {
	.driver = {
		.name = "qcom-sm6115-lpass-lpi-pinctrl",
		.of_match_table = lpi_pinctrl_of_match,
	},
	.probe = lpi_pinctrl_probe,
	.remove = lpi_pinctrl_remove,
};

module_platform_driver(lpi_pinctrl_driver);
MODULE_DESCRIPTION("QTI SM6115 LPI GPIO pin control driver");
MODULE_LICENSE("GPL");
