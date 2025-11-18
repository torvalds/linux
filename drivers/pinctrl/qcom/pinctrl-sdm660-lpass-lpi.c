// SPDX-License-Identifier: GPL-2.0-only
/*
 * This driver is solely based on the limited information in downstream code.
 * Any verification with schematics would be greatly appreciated.
 *
 * Copyright (c) 2023, Richard Acayan. All rights reserved.
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
	LPI_MUX_mclk0,
	LPI_MUX_pdm_tx,
	LPI_MUX_pdm_clk,
	LPI_MUX_pdm_rx,
	LPI_MUX_pdm_sync,

	LPI_MUX_gpio,
	LPI_MUX__,
};

static const struct pinctrl_pin_desc sdm660_lpi_pinctrl_pins[] = {
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
static const char * const mclk0_groups[] = { "gpio18" };
static const char * const pdm_tx_groups[] = { "gpio20" };
static const char * const pdm_clk_groups[] = { "gpio18" };
static const char * const pdm_rx_groups[] = { "gpio21", "gpio23", "gpio25" };
static const char * const pdm_sync_groups[] = { "gpio19" };

const struct lpi_pingroup sdm660_lpi_pinctrl_groups[] = {
	LPI_PINGROUP_OFFSET(0, LPI_NO_SLEW, _, _, _, _, 0x0000),
	LPI_PINGROUP_OFFSET(1, LPI_NO_SLEW, _, _, _, _, 0x1000),
	LPI_PINGROUP_OFFSET(2, LPI_NO_SLEW, _, _, _, _, 0x2000),
	LPI_PINGROUP_OFFSET(3, LPI_NO_SLEW, _, _, _, _, 0x2010),
	LPI_PINGROUP_OFFSET(4, LPI_NO_SLEW, _, _, _, _, 0x3000),
	LPI_PINGROUP_OFFSET(5, LPI_NO_SLEW, _, _, _, _, 0x3010),
	LPI_PINGROUP_OFFSET(6, LPI_NO_SLEW, _, _, _, _, 0x4000),
	LPI_PINGROUP_OFFSET(7, LPI_NO_SLEW, _, _, _, _, 0x4010),
	LPI_PINGROUP_OFFSET(8, LPI_NO_SLEW, _, _, _, _, 0x5000),
	LPI_PINGROUP_OFFSET(9, LPI_NO_SLEW, _, _, _, _, 0x5010),
	LPI_PINGROUP_OFFSET(10, LPI_NO_SLEW, _, _, _, _, 0x5020),
	LPI_PINGROUP_OFFSET(11, LPI_NO_SLEW, _, _, _, _, 0x5030),
	LPI_PINGROUP_OFFSET(12, LPI_NO_SLEW, _, _, _, _, 0x6000),
	LPI_PINGROUP_OFFSET(13, LPI_NO_SLEW, _, _, _, _, 0x6010),
	LPI_PINGROUP_OFFSET(14, LPI_NO_SLEW, _, _, _, _, 0x7000),
	LPI_PINGROUP_OFFSET(15, LPI_NO_SLEW, _, _, _, _, 0x7010),
	LPI_PINGROUP_OFFSET(16, LPI_NO_SLEW, _, _, _, _, 0x5040),
	LPI_PINGROUP_OFFSET(17, LPI_NO_SLEW, _, _, _, _, 0x5050),

	LPI_PINGROUP_OFFSET(18, LPI_NO_SLEW, pdm_clk, mclk0, _, _, 0x8000),
	LPI_PINGROUP_OFFSET(19, LPI_NO_SLEW, pdm_sync, _, _, _, 0x8010),
	LPI_PINGROUP_OFFSET(20, LPI_NO_SLEW, pdm_tx, _, _, _, 0x8020),
	LPI_PINGROUP_OFFSET(21, LPI_NO_SLEW, pdm_rx, _, _, _, 0x8030),
	LPI_PINGROUP_OFFSET(22, LPI_NO_SLEW, comp_rx, _, _, _, 0x8040),
	LPI_PINGROUP_OFFSET(23, LPI_NO_SLEW, pdm_rx, _, _, _, 0x8050),
	LPI_PINGROUP_OFFSET(24, LPI_NO_SLEW, comp_rx, _, _, _, 0x8060),
	LPI_PINGROUP_OFFSET(25, LPI_NO_SLEW, pdm_rx, _, _, _, 0x8070),
	LPI_PINGROUP_OFFSET(26, LPI_NO_SLEW, dmic1_clk, _, _, _, 0x9000),
	LPI_PINGROUP_OFFSET(27, LPI_NO_SLEW, dmic1_data, _, _, _, 0x9010),
	LPI_PINGROUP_OFFSET(28, LPI_NO_SLEW, dmic2_clk, _, _, _, 0xa000),
	LPI_PINGROUP_OFFSET(29, LPI_NO_SLEW, dmic2_data, _, _, _, 0xa010),

	LPI_PINGROUP_OFFSET(30, LPI_NO_SLEW, _, _, _, _, 0xb000),
	LPI_PINGROUP_OFFSET(31, LPI_NO_SLEW, _, _, _, _, 0xb010),
};

const struct lpi_function sdm660_lpi_pinctrl_functions[] = {
	LPI_FUNCTION(comp_rx),
	LPI_FUNCTION(dmic1_clk),
	LPI_FUNCTION(dmic1_data),
	LPI_FUNCTION(dmic2_clk),
	LPI_FUNCTION(dmic2_data),
	LPI_FUNCTION(mclk0),
	LPI_FUNCTION(pdm_tx),
	LPI_FUNCTION(pdm_clk),
	LPI_FUNCTION(pdm_rx),
	LPI_FUNCTION(pdm_sync),
};

static const struct lpi_pinctrl_variant_data sdm660_lpi_pinctrl_data = {
	.pins = sdm660_lpi_pinctrl_pins,
	.npins = ARRAY_SIZE(sdm660_lpi_pinctrl_pins),
	.groups = sdm660_lpi_pinctrl_groups,
	.ngroups = ARRAY_SIZE(sdm660_lpi_pinctrl_groups),
	.functions = sdm660_lpi_pinctrl_functions,
	.nfunctions = ARRAY_SIZE(sdm660_lpi_pinctrl_functions),
	.flags = LPI_FLAG_SLEW_RATE_SAME_REG | LPI_FLAG_USE_PREDEFINED_PIN_OFFSET
};

static const struct of_device_id sdm660_lpi_pinctrl_of_match[] = {
	{
		.compatible = "qcom,sdm660-lpass-lpi-pinctrl",
		.data = &sdm660_lpi_pinctrl_data,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, sdm660_lpi_pinctrl_of_match);

static struct platform_driver sdm660_lpi_pinctrl_driver = {
	.driver = {
		.name = "qcom-sdm660-lpass-lpi-pinctrl",
		.of_match_table = sdm660_lpi_pinctrl_of_match,
	},
	.probe = lpi_pinctrl_probe,
	.remove = lpi_pinctrl_remove,
};
module_platform_driver(sdm660_lpi_pinctrl_driver);

MODULE_AUTHOR("Richard Acayan <mailingradian@gmail.com>");
MODULE_DESCRIPTION("QTI SDM660 LPI GPIO pin control driver");
MODULE_LICENSE("GPL");
