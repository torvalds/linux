/*
 * Driver for the NVIDIA Tegra pinmux
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

#ifndef __PINMUX_TEGRA_H__
#define __PINMUX_TEGRA_H__

/**
 * struct tegra_function - Tegra pinctrl mux function
 * @name: The name of the function, exported to pinctrl core.
 * @groups: An array of pin groups that may select this function.
 * @ngroups: The number of entries in @groups.
 */
struct tegra_function {
	const char *name;
	const char * const *groups;
	unsigned ngroups;
};

/**
 * struct tegra_pingroup - Tegra pin group
 * @mux_reg:		Mux register offset. -1 if unsupported.
 * @mux_bank:		Mux register bank. 0 if unsupported.
 * @mux_bit:		Mux register bit. 0 if unsupported.
 * @pupd_reg:		Pull-up/down register offset. -1 if unsupported.
 * @pupd_bank:		Pull-up/down register bank. 0 if unsupported.
 * @pupd_bit:		Pull-up/down register bit. 0 if unsupported.
 * @tri_reg:		Tri-state register offset. -1 if unsupported.
 * @tri_bank:		Tri-state register bank. 0 if unsupported.
 * @tri_bit:		Tri-state register bit. 0 if unsupported.
 * @einput_reg:		Enable-input register offset. -1 if unsupported.
 * @einput_bank:	Enable-input register bank. 0 if unsupported.
 * @einput_bit:		Enable-input register bit. 0 if unsupported.
 * @odrain_reg:		Open-drain register offset. -1 if unsupported.
 * @odrain_bank:	Open-drain register bank. 0 if unsupported.
 * @odrain_bit:		Open-drain register bit. 0 if unsupported.
 * @lock_reg:		Lock register offset. -1 if unsupported.
 * @lock_bank:		Lock register bank. 0 if unsupported.
 * @lock_bit:		Lock register bit. 0 if unsupported.
 * @ioreset_reg:	IO reset register offset. -1 if unsupported.
 * @ioreset_bank:	IO reset register bank. 0 if unsupported.
 * @ioreset_bit:	IO reset register bit. 0 if unsupported.
 * @drv_reg:		Drive fields register offset. -1 if unsupported.
 *			This register contains the hsm, schmitt, lpmd, drvdn,
 *			drvup, slwr, and slwf parameters.
 * @drv_bank:		Drive fields register bank. 0 if unsupported.
 * @hsm_bit:		High Speed Mode register bit. 0 if unsupported.
 * @schmitt_bit:	Scmitt register bit. 0 if unsupported.
 * @lpmd_bit:		Low Power Mode register bit. 0 if unsupported.
 * @drvdn_bit:		Drive Down register bit. 0 if unsupported.
 * @drvdn_width:	Drive Down field width. 0 if unsupported.
 * @drvup_bit:		Drive Up register bit. 0 if unsupported.
 * @drvup_width:	Drive Up field width. 0 if unsupported.
 * @slwr_bit:		Slew Rising register bit. 0 if unsupported.
 * @slwr_width:		Slew Rising field width. 0 if unsupported.
 * @slwf_bit:		Slew Falling register bit. 0 if unsupported.
 * @slwf_width:		Slew Falling field width. 0 if unsupported.
 *
 * A representation of a group of pins (possibly just one pin) in the Tegra
 * pin controller. Each group allows some parameter or parameters to be
 * configured. The most common is mux function selection. Many others exist
 * such as pull-up/down, tri-state, etc. Tegra's pin controller is complex;
 * certain groups may only support configuring certain parameters, hence
 * each parameter is optional, represented by a -1 "reg" value.
 */
struct tegra_pingroup {
	const char *name;
	const unsigned *pins;
	unsigned npins;
	unsigned funcs[4];
	unsigned func_safe;
	s16 mux_reg;
	s16 pupd_reg;
	s16 tri_reg;
	s16 einput_reg;
	s16 odrain_reg;
	s16 lock_reg;
	s16 ioreset_reg;
	s16 drv_reg;
	u32 mux_bank:2;
	u32 pupd_bank:2;
	u32 tri_bank:2;
	u32 einput_bank:2;
	u32 odrain_bank:2;
	u32 ioreset_bank:2;
	u32 lock_bank:2;
	u32 drv_bank:2;
	u32 mux_bit:5;
	u32 pupd_bit:5;
	u32 tri_bit:5;
	u32 einput_bit:5;
	u32 odrain_bit:5;
	u32 lock_bit:5;
	u32 ioreset_bit:5;
	u32 hsm_bit:5;
	u32 schmitt_bit:5;
	u32 lpmd_bit:5;
	u32 drvdn_bit:5;
	u32 drvup_bit:5;
	u32 slwr_bit:5;
	u32 slwf_bit:5;
	u32 drvdn_width:6;
	u32 drvup_width:6;
	u32 slwr_width:6;
	u32 slwf_width:6;
};

/**
 * struct tegra_pinctrl_soc_data - Tegra pin controller driver configuration
 * @ngpios:	The number of GPIO pins the pin controller HW affects.
 * @pins:	An array describing all pins the pin controller affects.
 *		All pins which are also GPIOs must be listed first within the
 *		array, and be numbered identically to the GPIO controller's
 *		numbering.
 * @npins:	The numbmer of entries in @pins.
 * @functions:	An array describing all mux functions the SoC supports.
 * @nfunctions:	The numbmer of entries in @functions.
 * @groups:	An array describing all pin groups the pin SoC supports.
 * @ngroups:	The numbmer of entries in @groups.
 */
struct tegra_pinctrl_soc_data {
	unsigned ngpios;
	const struct pinctrl_pin_desc *pins;
	unsigned npins;
	const struct tegra_function *functions;
	unsigned nfunctions;
	const struct tegra_pingroup *groups;
	unsigned ngroups;
};

/**
 * tegra_pinctrl_soc_initf() - Retrieve pin controller details for a SoC.
 * @soc_data:	This pointer must be updated to point at a struct containing
 *		details of the SoC.
 */
typedef void (*tegra_pinctrl_soc_initf)(
			const struct tegra_pinctrl_soc_data **soc_data);

/**
 * tegra20_pinctrl_init() - Retrieve pin controller details for Tegra20
 * @soc_data:	This pointer will be updated to point at a struct containing
 *		details of Tegra20's pin controller.
 */
void tegra20_pinctrl_init(const struct tegra_pinctrl_soc_data **soc_data);
/**
 * tegra30_pinctrl_init() - Retrieve pin controller details for Tegra20
 * @soc_data:	This pointer will be updated to point at a struct containing
 *		details of Tegra30's pin controller.
 */
void tegra30_pinctrl_init(const struct tegra_pinctrl_soc_data **soc_data);

#endif
