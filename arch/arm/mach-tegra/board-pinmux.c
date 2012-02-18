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

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/string.h>

#include <mach/pinmux.h>

#include "board-pinmux.h"
#include "devices.h"

struct tegra_board_pinmux_conf *confs[2];

static void tegra_board_pinmux_setup_pinmux(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(confs); i++) {
		if (!confs[i])
			continue;

		tegra_pinmux_config_table(confs[i]->pgs, confs[i]->pg_count);

		if (confs[i]->drives)
			tegra_drive_pinmux_config_table(confs[i]->drives,
							confs[i]->drive_count);
	}
}

static int tegra_board_pinmux_bus_notify(struct notifier_block *nb,
					 unsigned long event, void *vdev)
{
	struct device *dev = vdev;

	if (event != BUS_NOTIFY_BOUND_DRIVER)
		return NOTIFY_DONE;

	if (strcmp(dev_name(dev), PINMUX_DEV))
		return NOTIFY_DONE;

	tegra_board_pinmux_setup_pinmux();

	return NOTIFY_STOP_MASK;
}

static struct notifier_block nb = {
	.notifier_call = tegra_board_pinmux_bus_notify,
};

static struct platform_device *devices[] = {
	&tegra_gpio_device,
	&tegra_pinmux_device,
};

void tegra_board_pinmux_init(struct tegra_board_pinmux_conf *conf_a,
			     struct tegra_board_pinmux_conf *conf_b)
{
	confs[0] = conf_a;
	confs[1] = conf_b;

	bus_register_notifier(&platform_bus_type, &nb);

	if (!of_machine_is_compatible("nvidia,tegra20"))
		platform_add_devices(devices, ARRAY_SIZE(devices));
}
