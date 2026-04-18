// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023-2026, Richard Acayan. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/pinctrl.h>

#include "pinctrl-lpass-lpi.h"

enum lpass_lpi_functions {
	LPI_MUX_comp_rx,
	LPI_MUX_dmic1_clk,
	LPI_MUX_dmic1_data,
	LPI_MUX_dmic2_clk,
	LPI_MUX_dmic2_data,
	LPI_MUX_i2s1_clk,
	LPI_MUX_i2s1_data,
	LPI_MUX_i2s1_ws,
	LPI_MUX_lpi_cdc_rst,
	LPI_MUX_mclk0,
	LPI_MUX_pdm_rx,
	LPI_MUX_pdm_sync,
	LPI_MUX_pdm_tx,
	LPI_MUX_slimbus_clk,
	LPI_MUX_gpio,
	LPI_MUX__,
};

static const struct pinctrl_pin_desc sdm670_lpi_pinctrl_pins[] = {
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
	PINCTRL_PIN(27, "gpio27"),
	PINCTRL_PIN(28, "gpio28"),
	PINCTRL_PIN(29, "gpio29"),
	PINCTRL_PIN(30, "gpio30"),
	PINCTRL_PIN(31, "gpio31"),
};

static const char * const comp_rx_groups[] = { "gpio22", "gpio24" };
static const char * const dmic1_clk_groups[] = { "gpio26" };
static const char * const dmic1_data_groups[] = { "gpio27" };
static const char * const dmic2_clk_groups[] = { "gpio28" };
static const char * const dmic2_data_groups[] = { "gpio29" };
static const char * const i2s1_clk_groups[] = { "gpio8" };
static const char * const i2s1_ws_groups[] = { "gpio9" };
static const char * const i2s1_data_groups[] = { "gpio10", "gpio11" };
static const char * const lpi_cdc_rst_groups[] = { "gpio29" };
static const char * const mclk0_groups[] = { "gpio19" };
static const char * const pdm_rx_groups[] = { "gpio21", "gpio23", "gpio25" };
static const char * const pdm_sync_groups[] = { "gpio19" };
static const char * const pdm_tx_groups[] = { "gpio20" };
static const char * const slimbus_clk_groups[] = { "gpio18" };

static const struct lpi_pingroup sdm670_lpi_pinctrl_groups[] = {
	LPI_PINGROUP(0, LPI_NO_SLEW, _, _, _, _),
	LPI_PINGROUP(1, LPI_NO_SLEW, _, _, _, _),
	LPI_PINGROUP(2, LPI_NO_SLEW, _, _, _, _),
	LPI_PINGROUP(3, LPI_NO_SLEW, _, _, _, _),
	LPI_PINGROUP(4, LPI_NO_SLEW, _, _, _, _),
	LPI_PINGROUP(5, LPI_NO_SLEW, _, _, _, _),
	LPI_PINGROUP(6, LPI_NO_SLEW, _, _, _, _),
	LPI_PINGROUP(7, LPI_NO_SLEW, _, _, _, _),
	LPI_PINGROUP(8, LPI_NO_SLEW, _, _, i2s1_clk, _),
	LPI_PINGROUP(9, LPI_NO_SLEW, _, _, i2s1_ws, _),
	LPI_PINGROUP(10, LPI_NO_SLEW, _, _, _, i2s1_data),
	LPI_PINGROUP(11, LPI_NO_SLEW, _, i2s1_data, _, _),
	LPI_PINGROUP(12, LPI_NO_SLEW, _, _, _, _),
	LPI_PINGROUP(13, LPI_NO_SLEW, _, _, _, _),
	LPI_PINGROUP(14, LPI_NO_SLEW, _, _, _, _),
	LPI_PINGROUP(15, LPI_NO_SLEW, _, _, _, _),
	LPI_PINGROUP(16, LPI_NO_SLEW, _, _, _, _),
	LPI_PINGROUP(17, LPI_NO_SLEW, _, _, _, _),
	LPI_PINGROUP(18, LPI_NO_SLEW, _, slimbus_clk, _, _),
	LPI_PINGROUP(19, LPI_NO_SLEW, mclk0, _, pdm_sync, _),
	LPI_PINGROUP(20, LPI_NO_SLEW, _, pdm_tx, _, _),
	LPI_PINGROUP(21, LPI_NO_SLEW, _, pdm_rx, _, _),
	LPI_PINGROUP(22, LPI_NO_SLEW, _, comp_rx, _, _),
	LPI_PINGROUP(23, LPI_NO_SLEW, pdm_rx, _, _, _),
	LPI_PINGROUP(24, LPI_NO_SLEW, comp_rx, _, _, _),
	LPI_PINGROUP(25, LPI_NO_SLEW, pdm_rx, _, _, _),
	LPI_PINGROUP(26, LPI_NO_SLEW, dmic1_clk, _, _, _),
	LPI_PINGROUP(27, LPI_NO_SLEW, dmic1_data, _, _, _),
	LPI_PINGROUP(28, LPI_NO_SLEW, dmic2_clk, _, _, _),
	LPI_PINGROUP(29, LPI_NO_SLEW, dmic2_data, lpi_cdc_rst, _, _),
	LPI_PINGROUP(30, LPI_NO_SLEW, _, _, _, _),
	LPI_PINGROUP(31, LPI_NO_SLEW, _, _, _, _),
};

static const struct lpi_function sdm670_lpi_pinctrl_functions[] = {
	LPI_FUNCTION(comp_rx),
	LPI_FUNCTION(dmic1_clk),
	LPI_FUNCTION(dmic1_data),
	LPI_FUNCTION(dmic2_clk),
	LPI_FUNCTION(dmic2_data),
	LPI_FUNCTION(i2s1_clk),
	LPI_FUNCTION(i2s1_data),
	LPI_FUNCTION(i2s1_ws),
	LPI_FUNCTION(lpi_cdc_rst),
	LPI_FUNCTION(mclk0),
	LPI_FUNCTION(pdm_tx),
	LPI_FUNCTION(pdm_rx),
	LPI_FUNCTION(pdm_sync),
	LPI_FUNCTION(slimbus_clk),
};

static const struct lpi_pinctrl_variant_data sdm670_lpi_pinctrl_data = {
	.pins = sdm670_lpi_pinctrl_pins,
	.npins = ARRAY_SIZE(sdm670_lpi_pinctrl_pins),
	.groups = sdm670_lpi_pinctrl_groups,
	.ngroups = ARRAY_SIZE(sdm670_lpi_pinctrl_groups),
	.functions = sdm670_lpi_pinctrl_functions,
	.nfunctions = ARRAY_SIZE(sdm670_lpi_pinctrl_functions),
	.flags = LPI_FLAG_SLEW_RATE_SAME_REG,
};

static const struct of_device_id sdm670_lpi_pinctrl_of_match[] = {
	{
		.compatible = "qcom,sdm670-lpass-lpi-pinctrl",
		.data = &sdm670_lpi_pinctrl_data,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, sdm670_lpi_pinctrl_of_match);

static struct platform_driver sdm670_lpi_pinctrl_driver = {
	.driver = {
		.name = "qcom-sdm670-lpass-lpi-pinctrl",
		.of_match_table = sdm670_lpi_pinctrl_of_match,
	},
	.probe = lpi_pinctrl_probe,
	.remove = lpi_pinctrl_remove,
};
module_platform_driver(sdm670_lpi_pinctrl_driver);

MODULE_AUTHOR("Richard Acayan <mailingradian@gmail.com>");
MODULE_DESCRIPTION("QTI SDM670 LPI GPIO pin control driver");
MODULE_LICENSE("GPL");
