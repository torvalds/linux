/*
 * Copyright (c) 2011,2012, NVIDIA CORPORATION.  All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __MACH_TEGRA_BOARD_PINMUX_H
#define __MACH_TEGRA_BOARD_PINMUX_H

#include <linux/pinctrl/machine.h>

#include <mach/pinconf-tegra.h>

#define PINMUX_DEV "tegra20-pinctrl"

#define TEGRA_MAP_MUX(_group_, _function_) \
	PIN_MAP_MUX_GROUP_HOG_DEFAULT(PINMUX_DEV, _group_, _function_)

#define TEGRA_MAP_CONF(_group_, _pull_, _drive_) \
	PIN_MAP_CONFIGS_GROUP_HOG_DEFAULT(PINMUX_DEV, _group_, tegra_pincfg_pull##_pull_##_##_drive_)

#define TEGRA_MAP_MUXCONF(_group_, _function_, _pull_, _drive_) \
	TEGRA_MAP_MUX(_group_, _function_), \
	TEGRA_MAP_CONF(_group_, _pull_, _drive_)

extern unsigned long tegra_pincfg_pullnone_driven[2];
extern unsigned long tegra_pincfg_pullnone_tristate[2];
extern unsigned long tegra_pincfg_pullnone_na[1];
extern unsigned long tegra_pincfg_pullup_driven[2];
extern unsigned long tegra_pincfg_pullup_tristate[2];
extern unsigned long tegra_pincfg_pullup_na[1];
extern unsigned long tegra_pincfg_pulldown_driven[2];
extern unsigned long tegra_pincfg_pulldown_tristate[2];
extern unsigned long tegra_pincfg_pulldown_na[1];
extern unsigned long tegra_pincfg_pullna_driven[1];
extern unsigned long tegra_pincfg_pullna_tristate[1];

struct tegra_board_pinmux_conf {
	struct pinctrl_map *maps;
	int map_count;
};

void tegra_board_pinmux_init(struct tegra_board_pinmux_conf *conf_a,
			     struct tegra_board_pinmux_conf *conf_b);

#endif
