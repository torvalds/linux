/*
 * nVidia Tegra device tree board support
 *
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
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/pda_power.h>
#include <linux/io.h>
#include <linux/i2c.h>
#include <linux/i2c-tegra.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/setup.h>

#include <mach/iomap.h>
#include <mach/irqs.h>

#include "board.h"
#include "board-harmony.h"
#include "clock.h"
#include "devices.h"

void harmony_pinmux_init(void);
void seaboard_pinmux_init(void);
void ventana_pinmux_init(void);

struct of_dev_auxdata tegra20_auxdata_lookup[] __initdata = {
	OF_DEV_AUXDATA("nvidia,tegra20-sdhci", TEGRA_SDMMC1_BASE, "sdhci-tegra.0", NULL),
	OF_DEV_AUXDATA("nvidia,tegra20-sdhci", TEGRA_SDMMC2_BASE, "sdhci-tegra.1", NULL),
	OF_DEV_AUXDATA("nvidia,tegra20-sdhci", TEGRA_SDMMC3_BASE, "sdhci-tegra.2", NULL),
	OF_DEV_AUXDATA("nvidia,tegra20-sdhci", TEGRA_SDMMC4_BASE, "sdhci-tegra.3", NULL),
	OF_DEV_AUXDATA("nvidia,tegra20-i2c", TEGRA_I2C_BASE, "tegra-i2c.0", NULL),
	OF_DEV_AUXDATA("nvidia,tegra20-i2c", TEGRA_I2C2_BASE, "tegra-i2c.1", NULL),
	OF_DEV_AUXDATA("nvidia,tegra20-i2c", TEGRA_I2C3_BASE, "tegra-i2c.2", NULL),
	OF_DEV_AUXDATA("nvidia,tegra20-i2c", TEGRA_DVC_BASE, "tegra-i2c.3", NULL),
	OF_DEV_AUXDATA("nvidia,tegra20-i2s", TEGRA_I2S1_BASE, "tegra-i2s.0", NULL),
	OF_DEV_AUXDATA("nvidia,tegra20-i2s", TEGRA_I2S1_BASE, "tegra-i2s.1", NULL),
	OF_DEV_AUXDATA("nvidia,tegra20-das", TEGRA_APB_MISC_DAS_BASE, "tegra-das", NULL),
	{}
};

static __initdata struct tegra_clk_init_table tegra_dt_clk_init_table[] = {
	/* name		parent		rate		enabled */
	{ "uartd",	"pll_p",	216000000,	true },
	{ NULL,		NULL,		0,		0},
};

static struct of_device_id tegra_dt_match_table[] __initdata = {
	{ .compatible = "simple-bus", },
	{}
};

static struct of_device_id tegra_dt_gic_match[] __initdata = {
	{ .compatible = "nvidia,tegra20-gic", },
	{}
};

static struct {
	char *machine;
	void (*init)(void);
} pinmux_configs[] = {
	{ "nvidia,harmony", harmony_pinmux_init },
	{ "nvidia,seaboard", seaboard_pinmux_init },
	{ "nvidia,ventana", ventana_pinmux_init },
};

static void __init tegra_dt_init(void)
{
	struct device_node *node;
	int i;

	node = of_find_matching_node_by_address(NULL, tegra_dt_gic_match,
						TEGRA_ARM_INT_DIST_BASE);
	if (node)
		irq_domain_add_simple(node, INT_GIC_BASE);

	tegra_clk_init_from_table(tegra_dt_clk_init_table);

	/*
	 * Finished with the static registrations now; fill in the missing
	 * devices
	 */
	of_platform_populate(NULL, tegra_dt_match_table,
				tegra20_auxdata_lookup, NULL);

	for (i = 0; i < ARRAY_SIZE(pinmux_configs); i++) {
		if (of_machine_is_compatible(pinmux_configs[i].machine)) {
			pinmux_configs[i].init();
			break;
		}
	}

	WARN(i == ARRAY_SIZE(pinmux_configs),
		"Unknown platform! Pinmuxing not initialized\n");
}

static const char * tegra_dt_board_compat[] = {
	"nvidia,harmony",
	"nvidia,seaboard",
	"nvidia,ventana",
	NULL
};

DT_MACHINE_START(TEGRA_DT, "nVidia Tegra (Flattened Device Tree)")
	.map_io		= tegra_map_common_io,
	.init_early	= tegra_init_early,
	.init_irq	= tegra_init_irq,
	.timer		= &tegra_timer,
	.init_machine	= tegra_dt_init,
	.dt_compat	= tegra_dt_board_compat,
MACHINE_END
