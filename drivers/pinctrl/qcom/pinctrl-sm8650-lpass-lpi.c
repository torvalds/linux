// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Linaro Ltd.
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
	LPI_MUX_dmic4_clk,
	LPI_MUX_dmic4_data,
	LPI_MUX_i2s0_clk,
	LPI_MUX_i2s0_data,
	LPI_MUX_i2s0_ws,
	LPI_MUX_i2s1_clk,
	LPI_MUX_i2s1_data,
	LPI_MUX_i2s1_ws,
	LPI_MUX_i2s2_clk,
	LPI_MUX_i2s2_data,
	LPI_MUX_i2s2_ws,
	LPI_MUX_i2s3_clk,
	LPI_MUX_i2s3_data,
	LPI_MUX_i2s3_ws,
	LPI_MUX_i2s4_clk,
	LPI_MUX_i2s4_data,
	LPI_MUX_i2s4_ws,
	LPI_MUX_qca_swr_clk,
	LPI_MUX_qca_swr_data,
	LPI_MUX_slimbus_clk,
	LPI_MUX_slimbus_data,
	LPI_MUX_swr_rx_clk,
	LPI_MUX_swr_rx_data,
	LPI_MUX_swr_tx_clk,
	LPI_MUX_swr_tx_data,
	LPI_MUX_wsa_swr_clk,
	LPI_MUX_wsa_swr_data,
	LPI_MUX_wsa2_swr_clk,
	LPI_MUX_wsa2_swr_data,
	LPI_MUX_ext_mclk1_a,
	LPI_MUX_ext_mclk1_b,
	LPI_MUX_ext_mclk1_c,
	LPI_MUX_ext_mclk1_d,
	LPI_MUX_ext_mclk1_e,
	LPI_MUX_gpio,
	LPI_MUX__,
};

static const struct pinctrl_pin_desc sm8650_lpi_pins[] = {
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
	PINCTRL_PIN(19, "gpio19"),
	PINCTRL_PIN(20, "gpio20"),
	PINCTRL_PIN(21, "gpio21"),
	PINCTRL_PIN(22, "gpio22"),
};

static const char * const gpio_groups[] = {
	"gpio0", "gpio1", "gpio2", "gpio3", "gpio4", "gpio5", "gpio6", "gpio7",
	"gpio8", "gpio9", "gpio10", "gpio11", "gpio12", "gpio13", "gpio14",
	"gpio15", "gpio16", "gpio17", "gpio18", "gpio19", "gpio20", "gpio21",
	"gpio22",
};

static const char * const dmic1_clk_groups[] = { "gpio6" };
static const char * const dmic1_data_groups[] = { "gpio7" };
static const char * const dmic2_clk_groups[] = { "gpio8" };
static const char * const dmic2_data_groups[] = { "gpio9" };
static const char * const dmic3_clk_groups[] = { "gpio12" };
static const char * const dmic3_data_groups[] = { "gpio13" };
static const char * const dmic4_clk_groups[] = { "gpio17" };
static const char * const dmic4_data_groups[] = { "gpio18" };
static const char * const i2s0_clk_groups[] = { "gpio0" };
static const char * const i2s0_ws_groups[] = { "gpio1" };
static const char * const i2s0_data_groups[] = { "gpio2", "gpio3", "gpio4", "gpio5" };
static const char * const i2s1_clk_groups[] = { "gpio6" };
static const char * const i2s1_ws_groups[] = { "gpio7" };
static const char * const i2s1_data_groups[] = { "gpio8", "gpio9" };
static const char * const i2s2_clk_groups[] = { "gpio10" };
static const char * const i2s2_ws_groups[] = { "gpio11" };
static const char * const i2s2_data_groups[] = { "gpio15", "gpio16" };
static const char * const i2s3_clk_groups[] = { "gpio12" };
static const char * const i2s3_ws_groups[] = { "gpio13" };
static const char * const i2s3_data_groups[] = { "gpio17", "gpio18" };
static const char * const i2s4_clk_groups[] = { "gpio19"};
static const char * const i2s4_ws_groups[] = { "gpio20"};
static const char * const i2s4_data_groups[] = { "gpio21", "gpio22"};
static const char * const qca_swr_clk_groups[] = { "gpio19" };
static const char * const qca_swr_data_groups[] = { "gpio20" };
static const char * const slimbus_clk_groups[] = { "gpio19"};
static const char * const slimbus_data_groups[] = { "gpio20"};
static const char * const swr_tx_clk_groups[] = { "gpio0" };
static const char * const swr_tx_data_groups[] = { "gpio1", "gpio2", "gpio14" };
static const char * const swr_rx_clk_groups[] = { "gpio3" };
static const char * const swr_rx_data_groups[] = { "gpio4", "gpio5", "gpio15" };
static const char * const wsa_swr_clk_groups[] = { "gpio10" };
static const char * const wsa_swr_data_groups[] = { "gpio11" };
static const char * const wsa2_swr_clk_groups[] = { "gpio15" };
static const char * const wsa2_swr_data_groups[] = { "gpio16" };
static const char * const ext_mclk1_c_groups[] = { "gpio5" };
static const char * const ext_mclk1_b_groups[] = { "gpio9" };
static const char * const ext_mclk1_a_groups[] = { "gpio13" };
static const char * const ext_mclk1_d_groups[] = { "gpio14" };
static const char * const ext_mclk1_e_groups[] = { "gpio22" };

static const struct lpi_pingroup sm8650_groups[] = {
	LPI_PINGROUP(0, 11, swr_tx_clk, i2s0_clk, _, _),
	LPI_PINGROUP(1, 11, swr_tx_data, i2s0_ws, _, _),
	LPI_PINGROUP(2, 11, swr_tx_data, i2s0_data, _, _),
	LPI_PINGROUP(3, 11, swr_rx_clk, i2s0_data, _, _),
	LPI_PINGROUP(4, 11, swr_rx_data, i2s0_data, _, _),
	LPI_PINGROUP(5, 11, swr_rx_data, ext_mclk1_c, i2s0_data, _),
	LPI_PINGROUP(6, LPI_NO_SLEW, dmic1_clk, i2s1_clk, _,  _),
	LPI_PINGROUP(7, LPI_NO_SLEW, dmic1_data, i2s1_ws, _, _),
	LPI_PINGROUP(8, LPI_NO_SLEW, dmic2_clk, i2s1_data, _, _),
	LPI_PINGROUP(9, LPI_NO_SLEW, dmic2_data, i2s1_data, ext_mclk1_b, _),
	LPI_PINGROUP(10, 11, i2s2_clk, wsa_swr_clk, _, _),
	LPI_PINGROUP(11, 11, i2s2_ws, wsa_swr_data, _, _),
	LPI_PINGROUP(12, LPI_NO_SLEW, dmic3_clk, i2s3_clk, _, _),
	LPI_PINGROUP(13, LPI_NO_SLEW, dmic3_data, i2s3_ws, ext_mclk1_a, _),
	LPI_PINGROUP(14, 11, swr_tx_data, ext_mclk1_d, _, _),
	LPI_PINGROUP(15, 11, i2s2_data, wsa2_swr_clk, _, _),
	LPI_PINGROUP(16, 11, i2s2_data, wsa2_swr_data, _, _),
	LPI_PINGROUP(17, LPI_NO_SLEW, dmic4_clk, i2s3_data, _, _),
	LPI_PINGROUP(18, LPI_NO_SLEW, dmic4_data, i2s3_data, _, _),
	LPI_PINGROUP(19, 11, i2s4_clk, slimbus_clk, qca_swr_clk, _),
	LPI_PINGROUP(20, 11, i2s4_ws, slimbus_data, qca_swr_data, _),
	LPI_PINGROUP(21, LPI_NO_SLEW, i2s4_data, _, _, _),
	LPI_PINGROUP(22, LPI_NO_SLEW, i2s4_data, ext_mclk1_e, _, _),
};

static const struct lpi_function sm8650_functions[] = {
	LPI_FUNCTION(gpio),
	LPI_FUNCTION(dmic1_clk),
	LPI_FUNCTION(dmic1_data),
	LPI_FUNCTION(dmic2_clk),
	LPI_FUNCTION(dmic2_data),
	LPI_FUNCTION(dmic3_clk),
	LPI_FUNCTION(dmic3_data),
	LPI_FUNCTION(dmic4_clk),
	LPI_FUNCTION(dmic4_data),
	LPI_FUNCTION(i2s0_clk),
	LPI_FUNCTION(i2s0_data),
	LPI_FUNCTION(i2s0_ws),
	LPI_FUNCTION(i2s1_clk),
	LPI_FUNCTION(i2s1_data),
	LPI_FUNCTION(i2s1_ws),
	LPI_FUNCTION(i2s2_clk),
	LPI_FUNCTION(i2s2_data),
	LPI_FUNCTION(i2s2_ws),
	LPI_FUNCTION(i2s3_clk),
	LPI_FUNCTION(i2s3_data),
	LPI_FUNCTION(i2s3_ws),
	LPI_FUNCTION(i2s4_clk),
	LPI_FUNCTION(i2s4_data),
	LPI_FUNCTION(i2s4_ws),
	LPI_FUNCTION(qca_swr_clk),
	LPI_FUNCTION(qca_swr_data),
	LPI_FUNCTION(slimbus_clk),
	LPI_FUNCTION(slimbus_data),
	LPI_FUNCTION(swr_rx_clk),
	LPI_FUNCTION(swr_rx_data),
	LPI_FUNCTION(swr_tx_clk),
	LPI_FUNCTION(swr_tx_data),
	LPI_FUNCTION(wsa_swr_clk),
	LPI_FUNCTION(wsa_swr_data),
	LPI_FUNCTION(wsa2_swr_clk),
	LPI_FUNCTION(wsa2_swr_data),
	LPI_FUNCTION(ext_mclk1_a),
	LPI_FUNCTION(ext_mclk1_b),
	LPI_FUNCTION(ext_mclk1_c),
	LPI_FUNCTION(ext_mclk1_d),
	LPI_FUNCTION(ext_mclk1_e),
};

static const struct lpi_pinctrl_variant_data sm8650_lpi_data = {
	.pins = sm8650_lpi_pins,
	.npins = ARRAY_SIZE(sm8650_lpi_pins),
	.groups = sm8650_groups,
	.ngroups = ARRAY_SIZE(sm8650_groups),
	.functions = sm8650_functions,
	.nfunctions = ARRAY_SIZE(sm8650_functions),
	.flags = LPI_FLAG_SLEW_RATE_SAME_REG,
};

static const struct of_device_id lpi_pinctrl_of_match[] = {
	{
	       .compatible = "qcom,sm8650-lpass-lpi-pinctrl",
	       .data = &sm8650_lpi_data,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, lpi_pinctrl_of_match);

static struct platform_driver lpi_pinctrl_driver = {
	.driver = {
		   .name = "qcom-sm8650-lpass-lpi-pinctrl",
		   .of_match_table = lpi_pinctrl_of_match,
	},
	.probe = lpi_pinctrl_probe,
	.remove = lpi_pinctrl_remove,
};

module_platform_driver(lpi_pinctrl_driver);
MODULE_DESCRIPTION("Qualcomm SM8650 LPI GPIO pin control driver");
MODULE_LICENSE("GPL");
