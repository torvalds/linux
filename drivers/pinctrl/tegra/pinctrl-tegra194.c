// SPDX-License-Identifier: GPL-2.0+
/*
 * Pinctrl data for the NVIDIA Tegra194 pinmux
 *
 * Copyright (c) 2019, NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/init.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>

#include "pinctrl-tegra.h"

/* Define unique ID for each pins */
enum pin_id {
	TEGRA_PIN_PEX_L5_CLKREQ_N_PGG0,
	TEGRA_PIN_PEX_L5_RST_N_PGG1,
};

/* Table for pin descriptor */
static const struct pinctrl_pin_desc tegra194_pins[] = {
	PINCTRL_PIN(TEGRA_PIN_PEX_L5_CLKREQ_N_PGG0, "PEX_L5_CLKREQ_N_PGG0"),
	PINCTRL_PIN(TEGRA_PIN_PEX_L5_RST_N_PGG1, "PEX_L5_RST_N_PGG1"),
};

static const unsigned int pex_l5_clkreq_n_pgg0_pins[] = {
	TEGRA_PIN_PEX_L5_CLKREQ_N_PGG0,
};

static const unsigned int pex_l5_rst_n_pgg1_pins[] = {
	TEGRA_PIN_PEX_L5_RST_N_PGG1,
};

/* Define unique ID for each function */
enum tegra_mux_dt {
	TEGRA_MUX_RSVD0,
	TEGRA_MUX_RSVD1,
	TEGRA_MUX_RSVD2,
	TEGRA_MUX_RSVD3,
	TEGRA_MUX_PE5,
};

/* Make list of each function name */
#define TEGRA_PIN_FUNCTION(lid)			\
	{					\
		.name = #lid,			\
	}

static struct tegra_function tegra194_functions[] = {
	TEGRA_PIN_FUNCTION(rsvd0),
	TEGRA_PIN_FUNCTION(rsvd1),
	TEGRA_PIN_FUNCTION(rsvd2),
	TEGRA_PIN_FUNCTION(rsvd3),
	TEGRA_PIN_FUNCTION(pe5),
};

#define DRV_PINGROUP_ENTRY_Y(r, drvdn_b, drvdn_w, drvup_b,	\
			     drvup_w, slwr_b, slwr_w, slwf_b,	\
			     slwf_w, bank)			\
		.drv_reg = ((r)),				\
		.drv_bank = bank,				\
		.drvdn_bit = drvdn_b,				\
		.drvdn_width = drvdn_w,				\
		.drvup_bit = drvup_b,				\
		.drvup_width = drvup_w,				\
		.slwr_bit = slwr_b,				\
		.slwr_width = slwr_w,				\
		.slwf_bit = slwf_b,				\
		.slwf_width = slwf_w

#define PIN_PINGROUP_ENTRY_Y(r, bank, pupd, e_lpbk, e_input,	\
			     e_od, schmitt_b, drvtype)		\
		.mux_reg = ((r)),				\
		.lpmd_bit = -1,					\
		.lock_bit = -1,					\
		.hsm_bit = -1,					\
		.mux_bank = bank,				\
		.mux_bit = 0,					\
		.pupd_reg = ((r)),				\
		.pupd_bank = bank,				\
		.pupd_bit = 2,					\
		.tri_reg = ((r)),				\
		.tri_bank = bank,				\
		.tri_bit = 4,					\
		.einput_bit = e_input,				\
		.odrain_bit = e_od,				\
		.sfsel_bit = 10,				\
		.schmitt_bit = schmitt_b,			\
		.drvtype_bit = 13,				\
		.parked_bitmask = 0

#define drive_pex_l5_clkreq_n_pgg0				\
	DRV_PINGROUP_ENTRY_Y(0x14004, 12, 5, 20, 5, -1, -1, -1, -1, 0)
#define drive_pex_l5_rst_n_pgg1					\
	DRV_PINGROUP_ENTRY_Y(0x1400c, 12, 5, 20, 5, -1, -1, -1, -1, 0)

#define PINGROUP(pg_name, f0, f1, f2, f3, r, bank, pupd, e_lpbk,	\
		 e_input, e_lpdr, e_od, schmitt_b, drvtype, io_rail)	\
	{								\
		.name = #pg_name,					\
		.pins = pg_name##_pins,					\
		.npins = ARRAY_SIZE(pg_name##_pins),			\
			.funcs = {					\
				TEGRA_MUX_##f0,				\
				TEGRA_MUX_##f1,				\
				TEGRA_MUX_##f2,				\
				TEGRA_MUX_##f3,				\
			},						\
		PIN_PINGROUP_ENTRY_Y(r, bank, pupd, e_lpbk,		\
				     e_input, e_od,			\
				     schmitt_b, drvtype),		\
		drive_##pg_name,					\
	}

static const struct tegra_pingroup tegra194_groups[] = {
	PINGROUP(pex_l5_clkreq_n_pgg0, PE5, RSVD1, RSVD2, RSVD3, 0x14000, 0,
		 Y, -1, 6, 8, 11, 12, N, "vddio_pex_ctl_2"),
	PINGROUP(pex_l5_rst_n_pgg1, PE5, RSVD1, RSVD2, RSVD3, 0x14008, 0,
		 Y, -1, 6, 8, 11, 12, N, "vddio_pex_ctl_2"),
};

static const struct tegra_pinctrl_soc_data tegra194_pinctrl = {
	.pins = tegra194_pins,
	.npins = ARRAY_SIZE(tegra194_pins),
	.functions = tegra194_functions,
	.nfunctions = ARRAY_SIZE(tegra194_functions),
	.groups = tegra194_groups,
	.ngroups = ARRAY_SIZE(tegra194_groups),
	.hsm_in_mux = true,
	.schmitt_in_mux = true,
	.drvtype_in_mux = true,
	.sfsel_in_mux = true,
};

static int tegra194_pinctrl_probe(struct platform_device *pdev)
{
	return tegra_pinctrl_probe(pdev, &tegra194_pinctrl);
}

static const struct of_device_id tegra194_pinctrl_of_match[] = {
	{ .compatible = "nvidia,tegra194-pinmux", },
	{ },
};

static struct platform_driver tegra194_pinctrl_driver = {
	.driver = {
		.name = "tegra194-pinctrl",
		.of_match_table = tegra194_pinctrl_of_match,
	},
	.probe = tegra194_pinctrl_probe,
};

static int __init tegra194_pinctrl_init(void)
{
	return platform_driver_register(&tegra194_pinctrl_driver);
}
arch_initcall(tegra194_pinctrl_init);
