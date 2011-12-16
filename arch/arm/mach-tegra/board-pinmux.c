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

#include <linux/of.h>

#include <mach/gpio-tegra.h>
#include <mach/pinmux.h>

#include "board-pinmux.h"
#include "devices.h"

static struct platform_device *devices[] = {
	&tegra_gpio_device,
	&tegra_pinmux_device,
};

void tegra_board_pinmux_init(struct tegra_board_pinmux_conf *conf_a,
			     struct tegra_board_pinmux_conf *conf_b)
{
	struct tegra_board_pinmux_conf *confs[] = {conf_a, conf_b};
	int i;

	if (of_machine_is_compatible("nvidia,tegra20"))
		platform_add_devices(devices, ARRAY_SIZE(devices));

	for (i = 0; i < ARRAY_SIZE(confs); i++) {
		if (!confs[i])
			continue;

		tegra_pinmux_config_table(confs[i]->pgs, confs[i]->pg_count);

		if (confs[i]->drives)
			tegra_drive_pinmux_config_table(confs[i]->drives,
							confs[i]->drive_count);

		tegra_gpio_config(confs[i]->gpios, confs[i]->gpio_count);
	}

	if (!of_machine_is_compatible("nvidia,tegra20"))
		platform_add_devices(devices, ARRAY_SIZE(devices));
}
