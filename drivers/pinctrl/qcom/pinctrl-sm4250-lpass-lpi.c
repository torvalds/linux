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
	LPI_MUX_dmic4_clk,
	LPI_MUX_dmic4_data,
	LPI_MUX_ext_mclk0_a,
	LPI_MUX_ext_mclk0_b,
	LPI_MUX_ext_mclk1_a,
	LPI_MUX_ext_mclk1_b,
	LPI_MUX_ext_mclk1_c,
	LPI_MUX_i2s1_clk,
	LPI_MUX_i2s1_data,
	LPI_MUX_i2s1_ws,
	LPI_MUX_i2s2_clk,
	LPI_MUX_i2s2_data,
	LPI_MUX_i2s2_ws,
	LPI_MUX_i2s3_clk,
	LPI_MUX_i2s3_data,
	LPI_MUX_i2s3_ws,
	LPI_MUX_qup_io_00,
	LPI_MUX_qup_io_01,
	LPI_MUX_qup_io_05,
	LPI_MUX_qup_io_10,
	LPI_MUX_qup_io_11,
	LPI_MUX_qup_io_25,
	LPI_MUX_qup_io_21,
	LPI_MUX_qup_io_26,
	LPI_MUX_qup_io_31,
	LPI_MUX_qup_io_36,
	LPI_MUX_qua_mi2s_data,
	LPI_MUX_qua_mi2s_sclk,
	LPI_MUX_qua_mi2s_ws,
	LPI_MUX_slim_clk,
	LPI_MUX_slim_data,
	LPI_MUX_sync_out,
	LPI_MUX_swr_rx_clk,
	LPI_MUX_swr_rx_data,
	LPI_MUX_swr_tx_clk,
	LPI_MUX_swr_tx_data,
	LPI_MUX_swr_wsa_clk,
	LPI_MUX_swr_wsa_data,
	LPI_MUX_gpio,
	LPI_MUX__,
};

static const struct pinctrl_pin_desc sm4250_lpi_pins[] = {
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
	PINCTRL_PIN(23, "gpio23"),
	PINCTRL_PIN(24, "gpio24"),
	PINCTRL_PIN(25, "gpio25"),
	PINCTRL_PIN(26, "gpio26"),
};

static const char * const dmic01_clk_groups[] = { "gpio6" };
static const char * const dmic01_data_groups[] = { "gpio7" };
static const char * const dmic23_clk_groups[] = { "gpio8" };
static const char * const dmic23_data_groups[] = { "gpio9" };
static const char * const dmic4_clk_groups[] = { "gpio10" };
static const char * const dmic4_data_groups[] = { "gpio11" };
static const char * const ext_mclk0_a_groups[] = { "gpio13" };
static const char * const ext_mclk0_b_groups[] = { "gpio5" };
static const char * const ext_mclk1_a_groups[] = { "gpio18" };
static const char * const ext_mclk1_b_groups[] = { "gpio9" };
static const char * const ext_mclk1_c_groups[] = { "gpio17" };
static const char * const slim_clk_groups[] = { "gpio14" };
static const char * const slim_data_groups[] = { "gpio15" };
static const char * const i2s1_clk_groups[] = { "gpio6" };
static const char * const i2s1_data_groups[] = { "gpio8", "gpio9" };
static const char * const i2s1_ws_groups[] = { "gpio7" };
static const char * const i2s2_clk_groups[] = { "gpio10" };
static const char * const i2s2_data_groups[] = { "gpio12", "gpio13" };
static const char * const i2s2_ws_groups[] = { "gpio11" };
static const char * const i2s3_clk_groups[] = { "gpio14" };
static const char * const i2s3_data_groups[] = { "gpio16", "gpio17" };
static const char * const i2s3_ws_groups[] = { "gpio15" };
static const char * const qup_io_00_groups[] = { "gpio19" };
static const char * const qup_io_01_groups[] = { "gpio21" };
static const char * const qup_io_05_groups[] = { "gpio23" };
static const char * const qup_io_10_groups[] = { "gpio20" };
static const char * const qup_io_11_groups[] = { "gpio22" };
static const char * const qup_io_25_groups[] = { "gpio23" };
static const char * const qup_io_21_groups[] = { "gpio25" };
static const char * const qup_io_26_groups[] = { "gpio25" };
static const char * const qup_io_31_groups[] = { "gpio26" };
static const char * const qup_io_36_groups[] = { "gpio26" };
static const char * const qua_mi2s_data_groups[] = { "gpio2", "gpio3", "gpio4", "gpio5" };
static const char * const qua_mi2s_sclk_groups[] = { "gpio0" };
static const char * const qua_mi2s_ws_groups[] = { "gpio1" };
static const char * const sync_out_groups[] = { "gpio19", "gpio20", "gpio21", "gpio22",
						"gpio23", "gpio24", "gpio25", "gpio26"};
static const char * const swr_rx_clk_groups[] = { "gpio3" };
static const char * const swr_rx_data_groups[] = { "gpio4", "gpio5" };
static const char * const swr_tx_clk_groups[] = { "gpio0" };
static const char * const swr_tx_data_groups[] = { "gpio1", "gpio2" };
static const char * const swr_wsa_clk_groups[] = { "gpio10" };
static const char * const swr_wsa_data_groups[] = { "gpio11" };


static const struct lpi_pingroup sm4250_groups[] = {
	LPI_PINGROUP(0, 0, swr_tx_clk, qua_mi2s_sclk, _, _),
	LPI_PINGROUP(1, 2, swr_tx_data, qua_mi2s_ws, _, _),
	LPI_PINGROUP(2, 4, swr_tx_data, qua_mi2s_data, _, _),
	LPI_PINGROUP(3, 8, swr_rx_clk, qua_mi2s_data, _, _),
	LPI_PINGROUP(4, 10, swr_rx_data, qua_mi2s_data, _, _),
	LPI_PINGROUP(5, 12, swr_rx_data, ext_mclk0_b, qua_mi2s_data, _),
	LPI_PINGROUP(6, LPI_NO_SLEW, dmic01_clk, i2s1_clk, _, _),
	LPI_PINGROUP(7, LPI_NO_SLEW, dmic01_data, i2s1_ws, _, _),
	LPI_PINGROUP(8, LPI_NO_SLEW, dmic23_clk, i2s1_data, _, _),
	LPI_PINGROUP(9, LPI_NO_SLEW, dmic23_data, i2s1_data, ext_mclk1_b, _),
	LPI_PINGROUP(10, 16, i2s2_clk, swr_wsa_clk, dmic4_clk, _),
	LPI_PINGROUP(11, 18, i2s2_ws, swr_wsa_data, dmic4_data, _),
	LPI_PINGROUP(12, LPI_NO_SLEW, dmic23_clk, i2s2_data, _, _),
	LPI_PINGROUP(13, LPI_NO_SLEW, dmic23_data, i2s2_data, ext_mclk0_a, _),
	LPI_PINGROUP(14, LPI_NO_SLEW, i2s3_clk, slim_clk, _, _),
	LPI_PINGROUP(15, LPI_NO_SLEW, i2s3_ws, slim_data, _, _),
	LPI_PINGROUP(16, LPI_NO_SLEW, i2s3_data, _, _, _),
	LPI_PINGROUP(17, LPI_NO_SLEW, i2s3_data, ext_mclk1_c, _, _),
	LPI_PINGROUP(18, 20, ext_mclk1_a, swr_rx_data, _, _),
	LPI_PINGROUP(19, LPI_NO_SLEW, qup_io_00, sync_out, _, _),
	LPI_PINGROUP(20, LPI_NO_SLEW, qup_io_10, sync_out, _, _),
	LPI_PINGROUP(21, LPI_NO_SLEW, qup_io_01, sync_out, _, _),
	LPI_PINGROUP(22, LPI_NO_SLEW, qup_io_11, sync_out, _, _),
	LPI_PINGROUP(23, LPI_NO_SLEW, qup_io_25, qup_io_05, sync_out, _),
	LPI_PINGROUP(25, LPI_NO_SLEW, qup_io_26, qup_io_21, sync_out, _),
	LPI_PINGROUP(26, LPI_NO_SLEW, qup_io_36, qup_io_31, sync_out, _),
};

static const struct lpi_function sm4250_functions[] = {
	LPI_FUNCTION(dmic01_clk),
	LPI_FUNCTION(dmic01_data),
	LPI_FUNCTION(dmic23_clk),
	LPI_FUNCTION(dmic23_data),
	LPI_FUNCTION(dmic4_clk),
	LPI_FUNCTION(dmic4_data),
	LPI_FUNCTION(ext_mclk0_a),
	LPI_FUNCTION(ext_mclk0_b),
	LPI_FUNCTION(ext_mclk1_a),
	LPI_FUNCTION(ext_mclk1_b),
	LPI_FUNCTION(ext_mclk1_c),
	LPI_FUNCTION(i2s1_clk),
	LPI_FUNCTION(i2s1_data),
	LPI_FUNCTION(i2s1_ws),
	LPI_FUNCTION(i2s2_clk),
	LPI_FUNCTION(i2s2_data),
	LPI_FUNCTION(i2s2_ws),
	LPI_FUNCTION(i2s3_clk),
	LPI_FUNCTION(i2s3_data),
	LPI_FUNCTION(i2s3_ws),
	LPI_FUNCTION(qup_io_00),
	LPI_FUNCTION(qup_io_01),
	LPI_FUNCTION(qup_io_05),
	LPI_FUNCTION(qup_io_10),
	LPI_FUNCTION(qup_io_11),
	LPI_FUNCTION(qup_io_25),
	LPI_FUNCTION(qup_io_21),
	LPI_FUNCTION(qup_io_26),
	LPI_FUNCTION(qup_io_31),
	LPI_FUNCTION(qup_io_36),
	LPI_FUNCTION(qua_mi2s_data),
	LPI_FUNCTION(qua_mi2s_sclk),
	LPI_FUNCTION(qua_mi2s_ws),
	LPI_FUNCTION(slim_clk),
	LPI_FUNCTION(slim_data),
	LPI_FUNCTION(sync_out),
	LPI_FUNCTION(swr_rx_clk),
	LPI_FUNCTION(swr_rx_data),
	LPI_FUNCTION(swr_tx_clk),
	LPI_FUNCTION(swr_tx_data),
	LPI_FUNCTION(swr_wsa_clk),
	LPI_FUNCTION(swr_wsa_data),
};

static const struct lpi_pinctrl_variant_data sm4250_lpi_data = {
	.pins = sm4250_lpi_pins,
	.npins = ARRAY_SIZE(sm4250_lpi_pins),
	.groups = sm4250_groups,
	.ngroups = ARRAY_SIZE(sm4250_groups),
	.functions = sm4250_functions,
	.nfunctions = ARRAY_SIZE(sm4250_functions),
};

static const struct of_device_id lpi_pinctrl_of_match[] = {
	{ .compatible = "qcom,sm4250-lpass-lpi-pinctrl", .data = &sm4250_lpi_data },
	{ }
};
MODULE_DEVICE_TABLE(of, lpi_pinctrl_of_match);

static struct platform_driver lpi_pinctrl_driver = {
	.driver = {
		.name = "qcom-sm4250-lpass-lpi-pinctrl",
		.of_match_table = lpi_pinctrl_of_match,
	},
	.probe = lpi_pinctrl_probe,
	.remove = lpi_pinctrl_remove,
};

module_platform_driver(lpi_pinctrl_driver);
MODULE_DESCRIPTION("QTI SM4250 LPI GPIO pin control driver");
MODULE_AUTHOR("Srinivas Kandagatla <srinivas.kandagatla@linaro.org>");
MODULE_LICENSE("GPL");
