/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Driver for the NVIDIA Tegra pinmux
 *
 * Copyright (c) 2011, NVIDIA CORPORATION.  All rights reserved.
 */

#ifndef __PINMUX_TEGRA_H__
#define __PINMUX_TEGRA_H__

struct tegra_pmx {
	struct device *dev;
	struct pinctrl_dev *pctl;

	const struct tegra_pinctrl_soc_data *soc;
	const char **group_pins;

	int nbanks;
	void __iomem **regs;
	u32 *backup_regs;
};

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
	TEGRA_PINCONF_PARAM_RCV_SEL,
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
	/* argument: Integer, range is HW-dependant */
	TEGRA_PINCONF_PARAM_DRIVE_TYPE,
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

/**
 * struct tegra_function - Tegra pinctrl mux function
 * @name: The name of the function, exported to pinctrl core.
 * @groups: An array of pin groups that may select this function.
 * @ngroups: The number of entries in @groups.
 */
struct tegra_function {
	const char *name;
	const char **groups;
	unsigned ngroups;
};

/**
 * struct tegra_pingroup - Tegra pin group
 * @name		The name of the pin group.
 * @pins		An array of pin IDs included in this pin group.
 * @npins		The number of entries in @pins.
 * @funcs		The mux functions which can be muxed onto this group.
 * @mux_reg:		Mux register offset.
 *			This register contains the mux, einput, odrain, lock,
 *			ioreset, rcv_sel parameters.
 * @mux_bank:		Mux register bank.
 * @mux_bit:		Mux register bit.
 * @pupd_reg:		Pull-up/down register offset.
 * @pupd_bank:		Pull-up/down register bank.
 * @pupd_bit:		Pull-up/down register bit.
 * @tri_reg:		Tri-state register offset.
 * @tri_bank:		Tri-state register bank.
 * @tri_bit:		Tri-state register bit.
 * @einput_bit:		Enable-input register bit.
 * @odrain_bit:		Open-drain register bit.
 * @lock_bit:		Lock register bit.
 * @ioreset_bit:	IO reset register bit.
 * @rcv_sel_bit:	Receiver select bit.
 * @drv_reg:		Drive fields register offset.
 *			This register contains hsm, schmitt, lpmd, drvdn,
 *			drvup, slwr, slwf, and drvtype parameters.
 * @drv_bank:		Drive fields register bank.
 * @hsm_bit:		High Speed Mode register bit.
 * @sfsel_bit:		GPIO/SFIO selection register bit.
 * @schmitt_bit:	Schmitt register bit.
 * @lpmd_bit:		Low Power Mode register bit.
 * @drvdn_bit:		Drive Down register bit.
 * @drvdn_width:	Drive Down field width.
 * @drvup_bit:		Drive Up register bit.
 * @drvup_width:	Drive Up field width.
 * @slwr_bit:		Slew Rising register bit.
 * @slwr_width:		Slew Rising field width.
 * @slwf_bit:		Slew Falling register bit.
 * @slwf_width:		Slew Falling field width.
 * @lpdr_bit:		Base driver enabling bit.
 * @drvtype_bit:	Drive type register bit.
 * @parked_bitmask:	Parked register mask. 0 if unsupported.
 *
 * -1 in a *_reg field means that feature is unsupported for this group.
 * *_bank and *_reg values are irrelevant when *_reg is -1.
 * When *_reg is valid, *_bit may be -1 to indicate an unsupported feature.
 *
 * A representation of a group of pins (possibly just one pin) in the Tegra
 * pin controller. Each group allows some parameter or parameters to be
 * configured. The most common is mux function selection. Many others exist
 * such as pull-up/down, tri-state, etc. Tegra's pin controller is complex;
 * certain groups may only support configuring certain parameters, hence
 * each parameter is optional.
 */
struct tegra_pingroup {
	const char *name;
	const unsigned *pins;
	u8 npins;
	u8 funcs[4];
	s32 mux_reg;
	s32 pupd_reg;
	s32 tri_reg;
	s32 drv_reg;
	u32 mux_bank:2;
	u32 pupd_bank:2;
	u32 tri_bank:2;
	u32 drv_bank:2;
	s32 mux_bit:6;
	s32 pupd_bit:6;
	s32 tri_bit:6;
	s32 einput_bit:6;
	s32 odrain_bit:6;
	s32 lock_bit:6;
	s32 ioreset_bit:6;
	s32 rcv_sel_bit:6;
	s32 hsm_bit:6;
	s32 sfsel_bit:6;
	s32 schmitt_bit:6;
	s32 lpmd_bit:6;
	s32 drvdn_bit:6;
	s32 drvup_bit:6;
	s32 slwr_bit:6;
	s32 slwf_bit:6;
	s32 lpdr_bit:6;
	s32 drvtype_bit:6;
	s32 drvdn_width:6;
	s32 drvup_width:6;
	s32 slwr_width:6;
	s32 slwf_width:6;
	u32 parked_bitmask;
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
	const char *gpio_compatible;
	const struct pinctrl_pin_desc *pins;
	unsigned npins;
	struct tegra_function *functions;
	unsigned nfunctions;
	const struct tegra_pingroup *groups;
	unsigned ngroups;
	bool hsm_in_mux;
	bool schmitt_in_mux;
	bool drvtype_in_mux;
	bool sfsel_in_mux;
};

extern const struct dev_pm_ops tegra_pinctrl_pm;

int tegra_pinctrl_probe(struct platform_device *pdev,
			const struct tegra_pinctrl_soc_data *soc_data);
#endif
