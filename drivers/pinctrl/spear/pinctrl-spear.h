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

#define SPEAR_PIN_0_TO_101		\
	PINCTRL_PIN(0, "PLGPIO0"),	\
	PINCTRL_PIN(1, "PLGPIO1"),	\
	PINCTRL_PIN(2, "PLGPIO2"),	\
	PINCTRL_PIN(3, "PLGPIO3"),	\
	PINCTRL_PIN(4, "PLGPIO4"),	\
	PINCTRL_PIN(5, "PLGPIO5"),	\
	PINCTRL_PIN(6, "PLGPIO6"),	\
	PINCTRL_PIN(7, "PLGPIO7"),	\
	PINCTRL_PIN(8, "PLGPIO8"),	\
	PINCTRL_PIN(9, "PLGPIO9"),	\
	PINCTRL_PIN(10, "PLGPIO10"),	\
	PINCTRL_PIN(11, "PLGPIO11"),	\
	PINCTRL_PIN(12, "PLGPIO12"),	\
	PINCTRL_PIN(13, "PLGPIO13"),	\
	PINCTRL_PIN(14, "PLGPIO14"),	\
	PINCTRL_PIN(15, "PLGPIO15"),	\
	PINCTRL_PIN(16, "PLGPIO16"),	\
	PINCTRL_PIN(17, "PLGPIO17"),	\
	PINCTRL_PIN(18, "PLGPIO18"),	\
	PINCTRL_PIN(19, "PLGPIO19"),	\
	PINCTRL_PIN(20, "PLGPIO20"),	\
	PINCTRL_PIN(21, "PLGPIO21"),	\
	PINCTRL_PIN(22, "PLGPIO22"),	\
	PINCTRL_PIN(23, "PLGPIO23"),	\
	PINCTRL_PIN(24, "PLGPIO24"),	\
	PINCTRL_PIN(25, "PLGPIO25"),	\
	PINCTRL_PIN(26, "PLGPIO26"),	\
	PINCTRL_PIN(27, "PLGPIO27"),	\
	PINCTRL_PIN(28, "PLGPIO28"),	\
	PINCTRL_PIN(29, "PLGPIO29"),	\
	PINCTRL_PIN(30, "PLGPIO30"),	\
	PINCTRL_PIN(31, "PLGPIO31"),	\
	PINCTRL_PIN(32, "PLGPIO32"),	\
	PINCTRL_PIN(33, "PLGPIO33"),	\
	PINCTRL_PIN(34, "PLGPIO34"),	\
	PINCTRL_PIN(35, "PLGPIO35"),	\
	PINCTRL_PIN(36, "PLGPIO36"),	\
	PINCTRL_PIN(37, "PLGPIO37"),	\
	PINCTRL_PIN(38, "PLGPIO38"),	\
	PINCTRL_PIN(39, "PLGPIO39"),	\
	PINCTRL_PIN(40, "PLGPIO40"),	\
	PINCTRL_PIN(41, "PLGPIO41"),	\
	PINCTRL_PIN(42, "PLGPIO42"),	\
	PINCTRL_PIN(43, "PLGPIO43"),	\
	PINCTRL_PIN(44, "PLGPIO44"),	\
	PINCTRL_PIN(45, "PLGPIO45"),	\
	PINCTRL_PIN(46, "PLGPIO46"),	\
	PINCTRL_PIN(47, "PLGPIO47"),	\
	PINCTRL_PIN(48, "PLGPIO48"),	\
	PINCTRL_PIN(49, "PLGPIO49"),	\
	PINCTRL_PIN(50, "PLGPIO50"),	\
	PINCTRL_PIN(51, "PLGPIO51"),	\
	PINCTRL_PIN(52, "PLGPIO52"),	\
	PINCTRL_PIN(53, "PLGPIO53"),	\
	PINCTRL_PIN(54, "PLGPIO54"),	\
	PINCTRL_PIN(55, "PLGPIO55"),	\
	PINCTRL_PIN(56, "PLGPIO56"),	\
	PINCTRL_PIN(57, "PLGPIO57"),	\
	PINCTRL_PIN(58, "PLGPIO58"),	\
	PINCTRL_PIN(59, "PLGPIO59"),	\
	PINCTRL_PIN(60, "PLGPIO60"),	\
	PINCTRL_PIN(61, "PLGPIO61"),	\
	PINCTRL_PIN(62, "PLGPIO62"),	\
	PINCTRL_PIN(63, "PLGPIO63"),	\
	PINCTRL_PIN(64, "PLGPIO64"),	\
	PINCTRL_PIN(65, "PLGPIO65"),	\
	PINCTRL_PIN(66, "PLGPIO66"),	\
	PINCTRL_PIN(67, "PLGPIO67"),	\
	PINCTRL_PIN(68, "PLGPIO68"),	\
	PINCTRL_PIN(69, "PLGPIO69"),	\
	PINCTRL_PIN(70, "PLGPIO70"),	\
	PINCTRL_PIN(71, "PLGPIO71"),	\
	PINCTRL_PIN(72, "PLGPIO72"),	\
	PINCTRL_PIN(73, "PLGPIO73"),	\
	PINCTRL_PIN(74, "PLGPIO74"),	\
	PINCTRL_PIN(75, "PLGPIO75"),	\
	PINCTRL_PIN(76, "PLGPIO76"),	\
	PINCTRL_PIN(77, "PLGPIO77"),	\
	PINCTRL_PIN(78, "PLGPIO78"),	\
	PINCTRL_PIN(79, "PLGPIO79"),	\
	PINCTRL_PIN(80, "PLGPIO80"),	\
	PINCTRL_PIN(81, "PLGPIO81"),	\
	PINCTRL_PIN(82, "PLGPIO82"),	\
	PINCTRL_PIN(83, "PLGPIO83"),	\
	PINCTRL_PIN(84, "PLGPIO84"),	\
	PINCTRL_PIN(85, "PLGPIO85"),	\
	PINCTRL_PIN(86, "PLGPIO86"),	\
	PINCTRL_PIN(87, "PLGPIO87"),	\
	PINCTRL_PIN(88, "PLGPIO88"),	\
	PINCTRL_PIN(89, "PLGPIO89"),	\
	PINCTRL_PIN(90, "PLGPIO90"),	\
	PINCTRL_PIN(91, "PLGPIO91"),	\
	PINCTRL_PIN(92, "PLGPIO92"),	\
	PINCTRL_PIN(93, "PLGPIO93"),	\
	PINCTRL_PIN(94, "PLGPIO94"),	\
	PINCTRL_PIN(95, "PLGPIO95"),	\
	PINCTRL_PIN(96, "PLGPIO96"),	\
	PINCTRL_PIN(97, "PLGPIO97"),	\
	PINCTRL_PIN(98, "PLGPIO98"),	\
	PINCTRL_PIN(99, "PLGPIO99"),	\
	PINCTRL_PIN(100, "PLGPIO100"),	\
	PINCTRL_PIN(101, "PLGPIO101")

#endif /* __PINMUX_SPEAR_H__ */
