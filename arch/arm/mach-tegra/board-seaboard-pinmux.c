/*
 * Copyright (C) 2010-2012 NVIDIA Corporation
 * Copyright (C) 2011 Google, Inc.
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

#include "board-seaboard.h"
#include "board-pinmux.h"

static unsigned long seaboard_pincfg_drive_sdio1[] = {
	TEGRA_PINCONF_PACK(TEGRA_PINCONF_PARAM_HIGH_SPEED_MODE, 0),
	TEGRA_PINCONF_PACK(TEGRA_PINCONF_PARAM_SCHMITT, 0),
	TEGRA_PINCONF_PACK(TEGRA_PINCONF_PARAM_LOW_POWER_MODE, 3),
	TEGRA_PINCONF_PACK(TEGRA_PINCONF_PARAM_DRIVE_DOWN_STRENGTH, 31),
	TEGRA_PINCONF_PACK(TEGRA_PINCONF_PARAM_DRIVE_UP_STRENGTH, 31),
	TEGRA_PINCONF_PACK(TEGRA_PINCONF_PARAM_SLEW_RATE_FALLING, 3),
	TEGRA_PINCONF_PACK(TEGRA_PINCONF_PARAM_SLEW_RATE_RISING, 3),
};

static struct pinctrl_map common_map[] = {
	TEGRA_MAP_MUXCONF("ata",   "ide",           none, driven),
	TEGRA_MAP_MUXCONF("atb",   "sdio4",         none, driven),
	TEGRA_MAP_MUXCONF("atc",   "nand",          none, driven),
	TEGRA_MAP_MUXCONF("atd",   "gmi",           none, driven),
	TEGRA_MAP_MUXCONF("ate",   "gmi",           none, tristate),
	TEGRA_MAP_MUXCONF("cdev1", "plla_out",      none, driven),
	TEGRA_MAP_MUXCONF("cdev2", "pllp_out4",     none, driven),
	TEGRA_MAP_MUXCONF("crtp",  "crt",           up,   tristate),
	TEGRA_MAP_MUXCONF("csus",  "vi_sensor_clk", none, tristate),
	TEGRA_MAP_MUXCONF("dap1",  "dap1",          none, driven),
	TEGRA_MAP_MUXCONF("dap2",  "dap2",          none, driven),
	TEGRA_MAP_MUXCONF("dap3",  "dap3",          none, tristate),
	TEGRA_MAP_MUXCONF("dap4",  "dap4",          none, driven),
	TEGRA_MAP_MUXCONF("dta",   "vi",            down, driven),
	TEGRA_MAP_MUXCONF("dtb",   "vi",            down, driven),
	TEGRA_MAP_MUXCONF("dtc",   "vi",            down, driven),
	TEGRA_MAP_MUXCONF("dtd",   "vi",            down, driven),
	TEGRA_MAP_MUXCONF("dte",   "vi",            down, tristate),
	TEGRA_MAP_MUXCONF("dtf",   "i2c3",          none, driven),
	TEGRA_MAP_MUXCONF("gma",   "sdio4",         none, driven),
	TEGRA_MAP_MUXCONF("gmb",   "gmi",           up,   tristate),
	TEGRA_MAP_MUXCONF("gmc",   "uartd",         none, driven),
	TEGRA_MAP_MUXCONF("gme",   "sdio4",         none, driven),
	TEGRA_MAP_MUXCONF("gpu",   "pwm",           none, driven),
	TEGRA_MAP_MUXCONF("gpu7",  "rtck",          none, driven),
	TEGRA_MAP_MUXCONF("gpv",   "pcie",          none, tristate),
	TEGRA_MAP_MUXCONF("hdint", "hdmi",          na,   tristate),
	TEGRA_MAP_MUXCONF("i2cp",  "i2cp",          none, driven),
	TEGRA_MAP_MUXCONF("irrx",  "uartb",         none, driven),
	TEGRA_MAP_MUXCONF("irtx",  "uartb",         none, driven),
	TEGRA_MAP_MUXCONF("kbca",  "kbc",           up,   driven),
	TEGRA_MAP_MUXCONF("kbcb",  "kbc",           up,   driven),
	TEGRA_MAP_MUXCONF("kbcc",  "kbc",           up,   driven),
	TEGRA_MAP_MUXCONF("kbcd",  "kbc",           up,   driven),
	TEGRA_MAP_MUXCONF("kbce",  "kbc",           up,   driven),
	TEGRA_MAP_MUXCONF("kbcf",  "kbc",           up,   driven),
	TEGRA_MAP_MUXCONF("lcsn",  "rsvd4",         na,   tristate),
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
	TEGRA_MAP_MUXCONF("ldc",   "rsvd4",         na,   tristate),
	TEGRA_MAP_MUXCONF("ldi",   "displaya",      na,   driven),
	TEGRA_MAP_MUXCONF("lhp0",  "displaya",      na,   driven),
	TEGRA_MAP_MUXCONF("lhp1",  "displaya",      na,   driven),
	TEGRA_MAP_MUXCONF("lhp2",  "displaya",      na,   driven),
	TEGRA_MAP_MUXCONF("lhs",   "displaya",      na,   driven),
	TEGRA_MAP_MUXCONF("lm0",   "rsvd4",         na,   driven),
	TEGRA_MAP_MUXCONF("lm1",   "crt",           na,   tristate),
	TEGRA_MAP_MUXCONF("lpp",   "displaya",      na,   driven),
	TEGRA_MAP_MUXCONF("lpw1",  "rsvd4",         na,   tristate),
	TEGRA_MAP_MUXCONF("lsc0",  "displaya",      na,   driven),
	TEGRA_MAP_MUXCONF("lsdi",  "rsvd4",         na,   tristate),
	TEGRA_MAP_MUXCONF("lspi",  "displaya",      na,   driven),
	TEGRA_MAP_MUXCONF("lvp0",  "rsvd4",         na,   tristate),
	TEGRA_MAP_MUXCONF("lvp1",  "displaya",      na,   driven),
	TEGRA_MAP_MUXCONF("lvs",   "displaya",      na,   driven),
	TEGRA_MAP_MUXCONF("owc",   "rsvd2",         none, tristate),
	TEGRA_MAP_MUXCONF("pmc",   "pwr_on",        na,   driven),
	TEGRA_MAP_MUXCONF("pta",   "hdmi",          none, driven),
	TEGRA_MAP_MUXCONF("rm",    "i2c1",          none, driven),
	TEGRA_MAP_MUXCONF("sdb",   "sdio3",         na,   driven),
	TEGRA_MAP_MUXCONF("sdc",   "sdio3",         none, driven),
	TEGRA_MAP_MUXCONF("sdd",   "sdio3",         none, driven),
	TEGRA_MAP_MUXCONF("sdio1", "sdio1",         up,   driven),
	TEGRA_MAP_MUXCONF("slxa",  "pcie",          up,   tristate),
	TEGRA_MAP_MUXCONF("slxd",  "spdif",         none, driven),
	TEGRA_MAP_MUXCONF("slxk",  "pcie",          none, driven),
	TEGRA_MAP_MUXCONF("spdi",  "rsvd2",         none, driven),
	TEGRA_MAP_MUXCONF("spdo",  "rsvd2",         none, driven),
	TEGRA_MAP_MUXCONF("spib",  "gmi",           none, tristate),
	TEGRA_MAP_MUXCONF("spid",  "spi1",          none, tristate),
	TEGRA_MAP_MUXCONF("spie",  "spi1",          none, tristate),
	TEGRA_MAP_MUXCONF("spif",  "spi1",          down, tristate),
	TEGRA_MAP_MUXCONF("spih",  "spi2_alt",      up,   tristate),
	TEGRA_MAP_MUXCONF("uaa",   "ulpi",          up,   driven),
	TEGRA_MAP_MUXCONF("uab",   "ulpi",          up,   driven),
	TEGRA_MAP_MUXCONF("uac",   "rsvd2",         none, driven),
	TEGRA_MAP_MUXCONF("uad",   "irda",          none, driven),
	TEGRA_MAP_MUXCONF("uca",   "uartc",         none, driven),
	TEGRA_MAP_MUXCONF("ucb",   "uartc",         none, driven),
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

static struct pinctrl_map seaboard_map[] = {
	TEGRA_MAP_MUXCONF("ddc",   "rsvd2",         none, tristate),
	TEGRA_MAP_MUXCONF("gmd",   "sflash",        none, driven),
	TEGRA_MAP_MUXCONF("lpw0",  "hdmi",          na,   driven),
	TEGRA_MAP_MUXCONF("lpw2",  "hdmi",          na,   driven),
	TEGRA_MAP_MUXCONF("lsc1",  "hdmi",          na,   tristate),
	TEGRA_MAP_MUXCONF("lsck",  "hdmi",          na,   tristate),
	TEGRA_MAP_MUXCONF("lsda",  "hdmi",          na,   tristate),
	TEGRA_MAP_MUXCONF("slxc",  "spdif",         none, tristate),
	TEGRA_MAP_MUXCONF("spia",  "gmi",           up,   tristate),
	TEGRA_MAP_MUXCONF("spic",  "gmi",           up,   driven),
	TEGRA_MAP_MUXCONF("spig",  "spi2_alt",      up,   tristate),
	PIN_MAP_CONFIGS_GROUP_HOG_DEFAULT(PINMUX_DEV, "drive_sdio1", seaboard_pincfg_drive_sdio1),
};

static struct pinctrl_map ventana_map[] = {
	TEGRA_MAP_MUXCONF("ddc",   "rsvd2",         none, driven),
	TEGRA_MAP_MUXCONF("gmd",   "sflash",        none, tristate),
	TEGRA_MAP_MUXCONF("lpw0",  "displaya",      na,   driven),
	TEGRA_MAP_MUXCONF("lpw2",  "displaya",      na,   driven),
	TEGRA_MAP_MUXCONF("lsc1",  "displaya",      na,   driven),
	TEGRA_MAP_MUXCONF("lsck",  "displaya",      na,   tristate),
	TEGRA_MAP_MUXCONF("lsda",  "displaya",      na,   tristate),
	TEGRA_MAP_MUXCONF("slxc",  "sdio3",         none, driven),
	TEGRA_MAP_MUXCONF("spia",  "gmi",           none, tristate),
	TEGRA_MAP_MUXCONF("spic",  "gmi",           none, tristate),
	TEGRA_MAP_MUXCONF("spig",  "spi2_alt",      none, tristate),
};

static struct tegra_board_pinmux_conf common_conf = {
	.maps = common_map,
	.map_count = ARRAY_SIZE(common_map),
};

static struct tegra_board_pinmux_conf seaboard_conf = {
	.maps = seaboard_map,
	.map_count = ARRAY_SIZE(seaboard_map),
};

static struct tegra_board_pinmux_conf ventana_conf = {
	.maps = ventana_map,
	.map_count = ARRAY_SIZE(ventana_map),
};

void seaboard_pinmux_init(void)
{
	tegra_board_pinmux_init(&common_conf, &seaboard_conf);
}

void ventana_pinmux_init(void)
{
	tegra_board_pinmux_init(&common_conf, &ventana_conf);
}
