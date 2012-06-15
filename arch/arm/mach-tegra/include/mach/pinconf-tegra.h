/*
 * pinctrl configuration definitions for the NVIDIA Tegra pinmux
 *
 * Copyright (c) 2011, NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef __PINCONF_TEGRA_H__
#define __PINCONF_TEGRA_H__

enum tegra_pinconf_param {
	/* argument: tegra_pinconf_pull */
	TEGRA_PINCONF_PARAM_PULL,
	/* argument: tegra_pinconf_tristate */
	TEGRA_PINCONF_PARAM_TRISTATE,
	/* argument: Boolean */
	TEGRA_PINCONF_PARAM_ENABLE_INPUT,
	/* argument: Boolean */
	TEGRA_PINCONF_PARAM_OPEN_DRAIN,
	/* argument: Boolean */
	TEGRA_PINCONF_PARAM_LOCK,
	/* argument: Boolean */
	TEGRA_PINCONF_PARAM_IORESET,
	/* argument: Boolean */
	TEGRA_PINCONF_PARAM_HIGH_SPEED_MODE,
	/* argument: Boolean */
	TEGRA_PINCONF_PARAM_SCHMITT,
	/* argument: Boolean */
	TEGRA_PINCONF_PARAM_LOW_POWER_MODE,
	/* argument: Integer, range is HW-dependant */
	TEGRA_PINCONF_PARAM_DRIVE_DOWN_STRENGTH,
	/* argument: Integer, range is HW-dependant */
	TEGRA_PINCONF_PARAM_DRIVE_UP_STRENGTH,
	/* argument: Integer, range is HW-dependant */
	TEGRA_PINCONF_PARAM_SLEW_RATE_FALLING,
	/* argument: Integer, range is HW-dependant */
	TEGRA_PINCONF_PARAM_SLEW_RATE_RISING,
};

enum tegra_pinconf_pull {
	TEGRA_PINCONFIG_PULL_NONE,
	TEGRA_PINCONFIG_PULL_DOWN,
	TEGRA_PINCONFIG_PULL_UP,
};

enum tegra_pinconf_tristate {
	TEGRA_PINCONFIG_DRIVEN,
	TEGRA_PINCONFIG_TRISTATE,
};

#define TEGRA_PINCONF_PACK(_param_, _arg_) ((_param_) << 16 | (_arg_))
#define TEGRA_PINCONF_UNPACK_PARAM(_conf_) ((_conf_) >> 16)
#define TEGRA_PINCONF_UNPACK_ARG(_conf_) ((_conf_) & 0xffff)

#endif
