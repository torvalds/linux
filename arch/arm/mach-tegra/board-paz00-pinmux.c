/*
 * arch/arm/mach-tegra/board-paz00-pinmux.c
 *
 * Copyright (C) 2010 Marc Dietrich <marvin24@gmx.de>
 * Copyright (c) 2012, NVIDIA CORPORATION.  All rights reserved.
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

#include "board-paz00.h"
#include "board-pinmux.h"

static struct pinctrl_map paz00_map[] = {
	TEGRA_MAP_MUXCONF("ata",   "gmi",           none, driven),
	TEGRA_MAP_MUXCONF("atb",   "sdio4",         none, driven),
	TEGRA_MAP_MUXCONF("atc",   "gmi",           none, driven),
	TEGRA_MAP_MUXCONF("atd",   "gmi",           none, driven),
	TEGRA_MAP_MUXCONF("ate",   "gmi",           none, driven),
	TEGRA_MAP_MUXCONF("cdev1", "plla_out",      none, driven),
	TEGRA_MAP_MUXCONF("cdev2", "pllp_out4",     down, driven),
	TEGRA_MAP_MUXCONF("crtp",  "crt",           none, tristate),
	TEGRA_MAP_MUXCONF("csus",  "pllc_out1",     down, tristate),
	TEGRA_MAP_MUXCONF("dap1",  "dap1",          none, driven),
	TEGRA_MAP_MUXCONF("dap2",  "gmi",           none, driven),
	TEGRA_MAP_MUXCONF("dap3",  "dap3",          none, tristate),
	TEGRA_MAP_MUXCONF("dap4",  "dap4",          none, tristate),
	TEGRA_MAP_MUXCONF("ddc",   "i2c2",          up,   driven),
	TEGRA_MAP_MUXCONF("dta",   "rsvd1",         up,   tristate),
	TEGRA_MAP_MUXCONF("dtb",   "rsvd1",         none, tristate),
	TEGRA_MAP_MUXCONF("dtc",   "rsvd1",         none, tristate),
	TEGRA_MAP_MUXCONF("dtd",   "rsvd1",         up,   tristate),
	TEGRA_MAP_MUXCONF("dte",   "rsvd1",         none, tristate),
	TEGRA_MAP_MUXCONF("dtf",   "i2c3",          none, driven),
	TEGRA_MAP_MUXCONF("gma",   "sdio4",         none, driven),
	TEGRA_MAP_MUXCONF("gmb",   "gmi",           none, driven),
	TEGRA_MAP_MUXCONF("gmc",   "gmi",           none, driven),
	TEGRA_MAP_MUXCONF("gmd",   "gmi",           none, driven),
	TEGRA_MAP_MUXCONF("gme",   "sdio4",         none, driven),
	TEGRA_MAP_MUXCONF("gpu",   "pwm",           none, driven),
	TEGRA_MAP_MUXCONF("gpu7",  "rtck",          none, driven),
	TEGRA_MAP_MUXCONF("gpv",   "pcie",          none, driven),
	TEGRA_MAP_MUXCONF("hdint", "hdmi",          na,   driven),
	TEGRA_MAP_MUXCONF("i2cp",  "i2cp",          none, driven),
	TEGRA_MAP_MUXCONF("irrx",  "uarta",         up,   driven),
	TEGRA_MAP_MUXCONF("irtx",  "uarta",         up,   driven),
	TEGRA_MAP_MUXCONF("kbca",  "kbc",           up,   driven),
	TEGRA_MAP_MUXCONF("kbcb",  "sdio2",         up,   driven),
	TEGRA_MAP_MUXCONF("kbcc",  "kbc",           up,   driven),
	TEGRA_MAP_MUXCONF("kbcd",  "sdio2",         up,   driven),
	TEGRA_MAP_MUXCONF("kbce",  "kbc",           up,   driven),
	TEGRA_MAP_MUXCONF("kbcf",  "kbc",           up,   driven),
	TEGRA_MAP_MUXCONF("lcsn",  "displaya",      na,   tristate),
	TEGRA_MAP_MUXCONF("ld0",   "displaya",      na,   driven),
	TEGRA_MAP_MUXCONF("ld1",   "displaya",      na,   driven),
	TEGRA_MAP_MUXCONF("ld10",  "displaya",      na,   driven),
	TEGRA_MAP_MUXCONF("ld11",  "displaya",      na,   driven),
	TEGRA_MAP_MUXCONF("ld12",  "displaya",      na,   driven),
	TEGRA_MAP_MUXCONF("ld13",  "displaya",      na,   driven),
	TEGRA_MAP_MUXCONF("ld14",  "displaya",      na,   driven),
	TEGRA_MAP_MUXCONF("ld15",  "displaya",      na,   driven),
	TEGRA_MAP_MUXCONF("ld16",  "displaya",      na,   driven),
	TEGRA_MAP_MUXCONF("ld17",  "displaya",      na,   driven),
	TEGRA_MAP_MUXCONF("ld2",   "displaya",      na,   driven),
	TEGRA_MAP_MUXCONF("ld3",   "displaya",      na,   driven),
	TEGRA_MAP_MUXCONF("ld4",   "displaya",      na,   driven),
	TEGRA_MAP_MUXCONF("ld5",   "displaya",      na,   driven),
	TEGRA_MAP_MUXCONF("ld6",   "displaya",      na,   driven),
	TEGRA_MAP_MUXCONF("ld7",   "displaya",      na,   driven),
	TEGRA_MAP_MUXCONF("ld8",   "displaya",      na,   driven),
	TEGRA_MAP_MUXCONF("ld9",   "displaya",      na,   driven),
	TEGRA_MAP_MUXCONF("ldc",   "displaya",      na,   driven),
	TEGRA_MAP_MUXCONF("ldi",   "displaya",      na,   driven),
	TEGRA_MAP_MUXCONF("lhp0",  "displaya",      na,   tristate),
	TEGRA_MAP_MUXCONF("lhp1",  "displaya",      na,   tristate),
	TEGRA_MAP_MUXCONF("lhp2",  "displaya",      na,   tristate),
	TEGRA_MAP_MUXCONF("lhs",   "displaya",      na,   driven),
	TEGRA_MAP_MUXCONF("lm0",   "displaya",      na,   tristate),
	TEGRA_MAP_MUXCONF("lm1",   "displaya",      na,   tristate),
	TEGRA_MAP_MUXCONF("lpp",   "displaya",      na,   tristate),
	TEGRA_MAP_MUXCONF("lpw0",  "displaya",      na,   tristate),
	TEGRA_MAP_MUXCONF("lpw1",  "displaya",      na,   tristate),
	TEGRA_MAP_MUXCONF("lpw2",  "displaya",      na,   tristate),
	TEGRA_MAP_MUXCONF("lsc0",  "displaya",      na,   driven),
	TEGRA_MAP_MUXCONF("lsc1",  "displaya",      na,   tristate),
	TEGRA_MAP_MUXCONF("lsck",  "displaya",      na,   tristate),
	TEGRA_MAP_MUXCONF("lsda",  "displaya",      na,   tristate),
	TEGRA_MAP_MUXCONF("lsdi",  "displaya",      na,   tristate),
	TEGRA_MAP_MUXCONF("lspi",  "displaya",      na,   driven),
	TEGRA_MAP_MUXCONF("lvp0",  "displaya",      na,   tristate),
	TEGRA_MAP_MUXCONF("lvp1",  "displaya",      na,   tristate),
	TEGRA_MAP_MUXCONF("lvs",   "displaya",      na,   driven),
	TEGRA_MAP_MUXCONF("owc",   "owr",           up,   tristate),
	TEGRA_MAP_MUXCONF("pmc",   "pwr_on",        na,   driven),
	TEGRA_MAP_MUXCONF("pta",   "hdmi",          none, driven),
	TEGRA_MAP_MUXCONF("rm",    "i2c1",          none, driven),
	TEGRA_MAP_MUXCONF("sdb",   "pwm",           na,   tristate),
	TEGRA_MAP_MUXCONF("sdc",   "twc",           up,   tristate),
	TEGRA_MAP_MUXCONF("sdd",   "pwm",           up,   tristate),
	TEGRA_MAP_MUXCONF("sdio1", "sdio1",         none, driven),
	TEGRA_MAP_MUXCONF("slxa",  "pcie",          none, tristate),
	TEGRA_MAP_MUXCONF("slxc",  "spi4",          none, tristate),
	TEGRA_MAP_MUXCONF("slxd",  "spi4",          none, tristate),
	TEGRA_MAP_MUXCONF("slxk",  "pcie",          none, driven),
	TEGRA_MAP_MUXCONF("spdi",  "rsvd2",         none, tristate),
	TEGRA_MAP_MUXCONF("spdo",  "rsvd2",         none, driven),
	TEGRA_MAP_MUXCONF("spia",  "gmi",           down, tristate),
	TEGRA_MAP_MUXCONF("spib",  "gmi",           down, tristate),
	TEGRA_MAP_MUXCONF("spic",  "gmi",           up,   driven),
	TEGRA_MAP_MUXCONF("spid",  "gmi",           down, tristate),
	TEGRA_MAP_MUXCONF("spie",  "gmi",           up,   tristate),
	TEGRA_MAP_MUXCONF("spif",  "rsvd4",         down, tristate),
	TEGRA_MAP_MUXCONF("spig",  "spi2_alt",      up,   driven),
	TEGRA_MAP_MUXCONF("spih",  "spi2_alt",      up,   tristate),
	TEGRA_MAP_MUXCONF("uaa",   "ulpi",          up,   driven),
	TEGRA_MAP_MUXCONF("uab",   "ulpi",          up,   driven),
	TEGRA_MAP_MUXCONF("uac",   "rsvd4",         none, driven),
	TEGRA_MAP_MUXCONF("uad",   "spdif",         up,   tristate),
	TEGRA_MAP_MUXCONF("uca",   "uartc",         up,   tristate),
	TEGRA_MAP_MUXCONF("ucb",   "uartc",         up,   tristate),
	TEGRA_MAP_MUXCONF("uda",   "ulpi",          none, driven),
	TEGRA_MAP_CONF("ck32",    none, na),
	TEGRA_MAP_CONF("ddrc",    none, na),
	TEGRA_MAP_CONF("pmca",    none, na),
	TEGRA_MAP_CONF("pmcb",    none, na),
	TEGRA_MAP_CONF("pmcc",    none, na),
	TEGRA_MAP_CONF("pmcd",    none, na),
	TEGRA_MAP_CONF("pmce",    none, na),
	TEGRA_MAP_CONF("xm2c",    none, na),
	TEGRA_MAP_CONF("xm2d",    none, na),
	TEGRA_MAP_CONF("ls",      up,   na),
	TEGRA_MAP_CONF("lc",      up,   na),
	TEGRA_MAP_CONF("ld17_0",  down, na),
	TEGRA_MAP_CONF("ld19_18", down, na),
	TEGRA_MAP_CONF("ld21_20", down, na),
	TEGRA_MAP_CONF("ld23_22", down, na),
};

static struct tegra_board_pinmux_conf conf = {
	.maps = paz00_map,
	.map_count = ARRAY_SIZE(paz00_map),
};

void paz00_pinmux_init(void)
{
	tegra_board_pinmux_init(&conf, NULL);
}
