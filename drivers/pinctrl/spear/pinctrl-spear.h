/*
 * Driver header file for the ST Microelectronics SPEAr pinmux
 *
 * Copyright (C) 2012 ST Microelectronics
 * Viresh Kumar <viresh.kumar@st.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __PINMUX_SPEAR_H__
#define __PINMUX_SPEAR_H__

#include <linux/pinctrl/pinctrl.h>
#include <linux/types.h>

struct platform_device;
struct device;

/**
 * struct spear_pmx_mode - SPEAr pmx mode
 * @name: name of pmx mode
 * @mode: mode id
 * @reg: register for configuring this mode
 * @mask: mask of this mode in reg
 * @val: val to be configured at reg after doing (val & mask)
 */
struct spear_pmx_mode {
	const char *const name;
	u16 mode;
	u16 reg;
	u16 mask;
	u32 val;
};

/**
 * struct spear_muxreg - SPEAr mux reg configuration
 * @reg: register offset
 * @mask: mask bits
 * @val: val to be written on mask bits
 */
struct spear_muxreg {
	u16 reg;
	u32 mask;
	u32 val;
};

/**
 * struct spear_modemux - SPEAr mode mux configuration
 * @modes: mode ids supported by this group of muxregs
 * @nmuxregs: number of muxreg configurations to be done for modes
 * @muxregs: array of muxreg configurations to be done for modes
 */
struct spear_modemux {
	u16 modes;
	u8 nmuxregs;
	struct spear_muxreg *muxregs;
};

/**
 * struct spear_pingroup - SPEAr pin group configurations
 * @name: name of pin group
 * @pins: array containing pin numbers
 * @npins: size of pins array
 * @modemuxs: array of modemux configurations for this pin group
 * @nmodemuxs: size of array modemuxs
 *
 * A representation of a group of pins in the SPEAr pin controller. Each group
 * allows some parameter or parameters to be configured.
 */
struct spear_pingroup {
	const char *name;
	const unsigned *pins;
	unsigned npins;
	struct spear_modemux *modemuxs;
	unsigned nmodemuxs;
};

/**
 * struct spear_function - SPEAr pinctrl mux function
 * @name: The name of the function, exported to pinctrl core.
 * @groups: An array of pin groups that may select this function.
 * @ngroups: The number of entries in @groups.
 */
struct spear_function {
	const char *name;
	const char *const *groups;
	unsigned ngroups;
};

/**
 * struct spear_pinctrl_machdata - SPEAr pin controller machine driver
 *	configuration
 * @pins: An array describing all pins the pin controller affects.
 *	All pins which are also GPIOs must be listed first within the *array,
 *	and be numbered identically to the GPIO controller's *numbering.
 * @npins: The numbmer of entries in @pins.
 * @functions: An array describing all mux functions the SoC supports.
 * @nfunctions: The numbmer of entries in @functions.
 * @groups: An array describing all pin groups the pin SoC supports.
 * @ngroups: The numbmer of entries in @groups.
 *
 * @modes_supported: Does SoC support modes
 * @mode: mode configured from probe
 * @pmx_modes: array of modes supported by SoC
 * @npmx_modes: number of entries in pmx_modes.
 */
struct spear_pinctrl_machdata {
	const struct pinctrl_pin_desc *pins;
	unsigned npins;
	struct spear_function **functions;
	unsigned nfunctions;
	struct spear_pingroup **groups;
	unsigned ngroups;

	bool modes_supported;
	u16 mode;
	struct spear_pmx_mode **pmx_modes;
	unsigned npmx_modes;
};

/**
 * struct spear_pmx - SPEAr pinctrl mux
 * @dev: pointer to struct dev of platform_device registered
 * @pctl: pointer to struct pinctrl_dev
 * @machdata: pointer to SoC or machine specific structure
 * @vbase: virtual base address of pinmux controller
 */
struct spear_pmx {
	struct device *dev;
	struct pinctrl_dev *pctl;
	struct spear_pinctrl_machdata *machdata;
	void __iomem *vbase;
};

/* exported routines */
void __devinit pmx_init_addr(struct spear_pinctrl_machdata *machdata, u16 reg);
int __devinit spear_pinctrl_probe(struct platform_device *pdev,
		struct spear_pinctrl_machdata *machdata);
int __devexit spear_pinctrl_remove(struct platform_device *pdev);
#endif /* __PINMUX_SPEAR_H__ */
