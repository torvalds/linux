/*
 * Copyright (c) 2011, NVIDIA CORPORATION.  All rights reserved.
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

#define GPIO_DEV "tegra-gpio"
#define PINMUX_DEV "tegra-pinmux"

struct tegra_pingroup_config;
struct tegra_gpio_table;

struct tegra_board_pinmux_conf {
	struct tegra_pingroup_config *pgs;
	int pg_count;

	struct tegra_drive_pingroup_config *drives;
	int drive_count;

	struct tegra_gpio_table *gpios;
	int gpio_count;
};

void tegra_board_pinmux_init(struct tegra_board_pinmux_conf *conf_a,
			     struct tegra_board_pinmux_conf *conf_b);

#endif
