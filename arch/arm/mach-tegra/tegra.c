/*
 * NVIDIA Tegra SoC device tree board support
 *
 * Copyright (C) 2011, 2013, NVIDIA Corporation
 * Copyright (C) 2010 Secret Lab Technologies, Ltd.
 * Copyright (C) 2010 Google, Inc.
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

#include <linux/clocksource.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/serial_8250.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/irqdomain.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_fdt.h>
#include <linux/of_platform.h>
#include <linux/pda_power.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/sys_soc.h>
#include <linux/usb/tegra_usb_phy.h>
#include <linux/clk/tegra.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/setup.h>

#include "board.h"
#include "common.h"
#include "fuse.h"
#include "iomap.h"

static void __init tegra_dt_init(void)
{
	struct soc_device_attribute *soc_dev_attr;
	struct soc_device *soc_dev;
	struct device *parent = NULL;

	tegra_clocks_apply_init_table();

	soc_dev_attr = kzalloc(sizeof(*soc_dev_attr), GFP_KERNEL);
	if (!soc_dev_attr)
		goto out;

	soc_dev_attr->family = kasprintf(GFP_KERNEL, "Tegra");
	soc_dev_attr->revision = kasprintf(GFP_KERNEL, "%d", tegra_revision);
	soc_dev_attr->soc_id = kasprintf(GFP_KERNEL, "%d", tegra_chip_id);

	soc_dev = soc_device_register(soc_dev_attr);
	if (IS_ERR(soc_dev)) {
		kfree(soc_dev_attr->family);
		kfree(soc_dev_attr->revision);
		kfree(soc_dev_attr->soc_id);
		kfree(soc_dev_attr);
		goto out;
	}

	parent = soc_device_to_device(soc_dev);

	/*
	 * Finished with the static registrations now; fill in the missing
	 * devices
	 */
out:
	of_platform_populate(NULL, of_default_bus_match_table, NULL, parent);
}

static void __init paz00_init(void)
{
	if (IS_ENABLED(CONFIG_ARCH_TEGRA_2x_SOC))
		tegra_paz00_wifikill_init();
}

static struct {
	char *machine;
	void (*init)(void);
} board_init_funcs[] = {
	{ "compal,paz00", paz00_init },
};

static void __init tegra_dt_init_late(void)
{
	int i;

	tegra_init_late();

	for (i = 0; i < ARRAY_SIZE(board_init_funcs); i++) {
		if (of_machine_is_compatible(board_init_funcs[i].machine)) {
			board_init_funcs[i].init();
			break;
		}
	}
}

static const char * const tegra_dt_board_compat[] = {
	"nvidia,tegra114",
	"nvidia,tegra30",
	"nvidia,tegra20",
	NULL
};

DT_MACHINE_START(TEGRA_DT, "NVIDIA Tegra SoC (Flattened Device Tree)")
	.map_io		= tegra_map_common_io,
	.smp		= smp_ops(tegra_smp_ops),
	.init_early	= tegra_init_early,
	.init_irq	= tegra_dt_init_irq,
	.init_time	= clocksource_of_init,
	.init_machine	= tegra_dt_init,
	.init_late	= tegra_dt_init_late,
	.restart	= tegra_assert_system_reset,
	.dt_compat	= tegra_dt_board_compat,
MACHINE_END
