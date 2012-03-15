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

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/notifier.h>
#include <linux/string.h>

#include "board-pinmux.h"
#include "devices.h"

unsigned long tegra_pincfg_pullnone_driven[2] = {
	TEGRA_PINCONF_PACK(TEGRA_PINCONF_PARAM_PULL, TEGRA_PINCONFIG_PULL_NONE),
	TEGRA_PINCONF_PACK(TEGRA_PINCONF_PARAM_TRISTATE, TEGRA_PINCONFIG_DRIVEN),
};

unsigned long tegra_pincfg_pullnone_tristate[2] = {
	TEGRA_PINCONF_PACK(TEGRA_PINCONF_PARAM_PULL, TEGRA_PINCONFIG_PULL_NONE),
	TEGRA_PINCONF_PACK(TEGRA_PINCONF_PARAM_TRISTATE, TEGRA_PINCONFIG_TRISTATE),
};

unsigned long tegra_pincfg_pullnone_na[1] = {
	TEGRA_PINCONF_PACK(TEGRA_PINCONF_PARAM_PULL, TEGRA_PINCONFIG_PULL_NONE),
};

unsigned long tegra_pincfg_pullup_driven[2] = {
	TEGRA_PINCONF_PACK(TEGRA_PINCONF_PARAM_PULL, TEGRA_PINCONFIG_PULL_UP),
	TEGRA_PINCONF_PACK(TEGRA_PINCONF_PARAM_TRISTATE, TEGRA_PINCONFIG_DRIVEN),
};

unsigned long tegra_pincfg_pullup_tristate[2] = {
	TEGRA_PINCONF_PACK(TEGRA_PINCONF_PARAM_PULL, TEGRA_PINCONFIG_PULL_UP),
	TEGRA_PINCONF_PACK(TEGRA_PINCONF_PARAM_TRISTATE, TEGRA_PINCONFIG_TRISTATE),
};

unsigned long tegra_pincfg_pullup_na[1] = {
	TEGRA_PINCONF_PACK(TEGRA_PINCONF_PARAM_PULL, TEGRA_PINCONFIG_PULL_UP),
};

unsigned long tegra_pincfg_pulldown_driven[2] = {
	TEGRA_PINCONF_PACK(TEGRA_PINCONF_PARAM_PULL, TEGRA_PINCONFIG_PULL_DOWN),
	TEGRA_PINCONF_PACK(TEGRA_PINCONF_PARAM_TRISTATE, TEGRA_PINCONFIG_DRIVEN),
};

unsigned long tegra_pincfg_pulldown_tristate[2] = {
	TEGRA_PINCONF_PACK(TEGRA_PINCONF_PARAM_PULL, TEGRA_PINCONFIG_PULL_DOWN),
	TEGRA_PINCONF_PACK(TEGRA_PINCONF_PARAM_TRISTATE, TEGRA_PINCONFIG_TRISTATE),
};

unsigned long tegra_pincfg_pulldown_na[1] = {
	TEGRA_PINCONF_PACK(TEGRA_PINCONF_PARAM_PULL, TEGRA_PINCONFIG_PULL_DOWN),
};

unsigned long tegra_pincfg_pullna_driven[1] = {
	TEGRA_PINCONF_PACK(TEGRA_PINCONF_PARAM_TRISTATE, TEGRA_PINCONFIG_DRIVEN),
};

unsigned long tegra_pincfg_pullna_tristate[1] = {
	TEGRA_PINCONF_PACK(TEGRA_PINCONF_PARAM_TRISTATE, TEGRA_PINCONFIG_TRISTATE),
};

static struct platform_device *devices[] = {
	&tegra_gpio_device,
	&tegra_pinmux_device,
};

void tegra_board_pinmux_init(struct tegra_board_pinmux_conf *conf_a,
			     struct tegra_board_pinmux_conf *conf_b)
{
	if (conf_a)
		pinctrl_register_mappings(conf_a->maps, conf_a->map_count);
	if (conf_b)
		pinctrl_register_mappings(conf_b->maps, conf_b->map_count);

	platform_add_devices(devices, ARRAY_SIZE(devices));
}
