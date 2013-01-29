/*
 * arch/arm/mach-tegra/board-dt-tegra30.c
 *
 * NVIDIA Tegra30 device tree board support
 *
 * Copyright (C) 2011 NVIDIA Corporation
 *
 * Derived from:
 *
 * arch/arm/mach-tegra/board-dt-tegra20.c
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
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_fdt.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>

#include <asm/mach/arch.h>

#include "board.h"
#include "clock.h"
#include "common.h"
#include "iomap.h"

struct of_dev_auxdata tegra30_auxdata_lookup[] __initdata = {
	OF_DEV_AUXDATA("nvidia,tegra20-sdhci", 0x78000000, "sdhci-tegra.0", NULL),
	OF_DEV_AUXDATA("nvidia,tegra20-sdhci", 0x78000200, "sdhci-tegra.1", NULL),
	OF_DEV_AUXDATA("nvidia,tegra20-sdhci", 0x78000400, "sdhci-tegra.2", NULL),
	OF_DEV_AUXDATA("nvidia,tegra20-sdhci", 0x78000600, "sdhci-tegra.3", NULL),
	OF_DEV_AUXDATA("nvidia,tegra20-i2c", 0x7000C000, "tegra-i2c.0", NULL),
	OF_DEV_AUXDATA("nvidia,tegra20-i2c", 0x7000C400, "tegra-i2c.1", NULL),
	OF_DEV_AUXDATA("nvidia,tegra20-i2c", 0x7000C500, "tegra-i2c.2", NULL),
	OF_DEV_AUXDATA("nvidia,tegra20-i2c", 0x7000C700, "tegra-i2c.3", NULL),
	OF_DEV_AUXDATA("nvidia,tegra20-i2c", 0x7000D000, "tegra-i2c.4", NULL),
	OF_DEV_AUXDATA("nvidia,tegra30-ahub", 0x70080000, "tegra30-ahub", NULL),
	OF_DEV_AUXDATA("nvidia,tegra30-apbdma", 0x6000a000, "tegra-apbdma", NULL),
	OF_DEV_AUXDATA("nvidia,tegra30-pwm", TEGRA_PWFM_BASE, "tegra-pwm", NULL),
	OF_DEV_AUXDATA("nvidia,tegra30-slink", 0x7000D400, "spi_tegra.0", NULL),
	OF_DEV_AUXDATA("nvidia,tegra30-slink", 0x7000D600, "spi_tegra.1", NULL),
	OF_DEV_AUXDATA("nvidia,tegra30-slink", 0x7000D800, "spi_tegra.2", NULL),
	OF_DEV_AUXDATA("nvidia,tegra30-slink", 0x7000DA00, "spi_tegra.3", NULL),
	OF_DEV_AUXDATA("nvidia,tegra30-slink", 0x7000DC00, "spi_tegra.4", NULL),
	OF_DEV_AUXDATA("nvidia,tegra30-slink", 0x7000DE00, "spi_tegra.5", NULL),
	OF_DEV_AUXDATA("nvidia,tegra30-host1x", 0x50000000, "host1x", NULL),
	OF_DEV_AUXDATA("nvidia,tegra30-dc", 0x54200000, "tegradc.0", NULL),
	OF_DEV_AUXDATA("nvidia,tegra30-dc", 0x54240000, "tegradc.1", NULL),
	OF_DEV_AUXDATA("nvidia,tegra30-hdmi", 0x54280000, "hdmi", NULL),
	OF_DEV_AUXDATA("nvidia,tegra30-dsi", 0x54300000, "dsi", NULL),
	OF_DEV_AUXDATA("nvidia,tegra30-tvo", 0x542c0000, "tvo", NULL),
	{}
};

static __initdata struct tegra_clk_init_table tegra_dt_clk_init_table[] = {
	/* name		parent		rate		enabled */
	{ "uarta",	"pll_p",	408000000,	true },
	{ "pll_a",	"pll_p_out1",	564480000,	true },
	{ "pll_a_out0",	"pll_a",	11289600,	true },
	{ "extern1",	"pll_a_out0",	0,		true },
	{ "clk_out_1",	"extern1",	0,		true },
	{ "blink",	"clk_32k",	32768,		true },
	{ "i2s0",	"pll_a_out0",	11289600,	false},
	{ "i2s1",	"pll_a_out0",	11289600,	false},
	{ "i2s2",	"pll_a_out0",	11289600,	false},
	{ "i2s3",	"pll_a_out0",	11289600,	false},
	{ "i2s4",	"pll_a_out0",	11289600,	false},
	{ "sdmmc1",	"pll_p",	48000000,	false},
	{ "sdmmc3",	"pll_p",	48000000,	false},
	{ "sdmmc4",	"pll_p",	48000000,	false},
	{ "sbc1",	"pll_p",	100000000,	false},
	{ "sbc2",	"pll_p",	100000000,	false},
	{ "sbc3",	"pll_p",	100000000,	false},
	{ "sbc4",	"pll_p",	100000000,	false},
	{ "sbc5",	"pll_p",	100000000,	false},
	{ "sbc6",	"pll_p",	100000000,	false},
	{ "host1x",	"pll_c",	150000000,	false},
	{ "disp1",	"pll_p",	600000000,	false},
	{ "disp2",	"pll_p",	600000000,	false},
	{ NULL,		NULL,		0,		0},
};

static void __init tegra30_dt_init(void)
{
	tegra_clk_init_from_table(tegra_dt_clk_init_table);

	of_platform_populate(NULL, of_default_bus_match_table,
				tegra30_auxdata_lookup, NULL);
}

static const char *tegra30_dt_board_compat[] = {
	"nvidia,tegra30",
	NULL
};

DT_MACHINE_START(TEGRA30_DT, "NVIDIA Tegra30 (Flattened Device Tree)")
	.smp		= smp_ops(tegra_smp_ops),
	.map_io		= tegra_map_common_io,
	.init_early	= tegra30_init_early,
	.init_irq	= tegra_dt_init_irq,
	.init_time	= tegra_init_timer,
	.init_machine	= tegra30_dt_init,
	.init_late	= tegra_init_late,
	.restart	= tegra_assert_system_reset,
	.dt_compat	= tegra30_dt_board_compat,
MACHINE_END
