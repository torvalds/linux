/*
 * Marvell MVEBU pinctrl driver
 *
 * Authors: Sebastian Hesselbarth <sebastian.hesselbarth@gmail.com>
 *          Thomas Petazzoni <thomas.petazzoni@free-electrons.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __PINCTRL_MVEBU_H__
#define __PINCTRL_MVEBU_H__

/**
 * struct mvebu_mpp_ctrl - describe a mpp control
 * @name: name of the control group
 * @pid: first pin id handled by this control
 * @npins: number of pins controlled by this control
 * @mpp_get: (optional) special function to get mpp setting
 * @mpp_set: (optional) special function to set mpp setting
 * @mpp_gpio_req: (optional) special function to request gpio
 * @mpp_gpio_dir: (optional) special function to set gpio direction
 *
 * A mpp_ctrl describes a muxable unit, e.g. pin, group of pins, or
 * internal function, inside the SoC. Each muxable unit can be switched
 * between two or more different settings, e.g. assign mpp pin 13 to
 * uart1 or sata.
 *
 * If optional mpp_get/_set functions are set these are used to get/set
 * a specific mode. Otherwise it is assumed that the mpp control is based
 * on 4-bit groups in subsequent registers. The optional mpp_gpio_req/_dir
 * functions can be used to allow pin settings with varying gpio pins.
 */
struct mvebu_mpp_ctrl {
	const char *name;
	u8 pid;
	u8 npins;
	unsigned *pins;
	int (*mpp_get)(struct mvebu_mpp_ctrl *ctrl, unsigned long *config);
	int (*mpp_set)(struct mvebu_mpp_ctrl *ctrl, unsigned long config);
	int (*mpp_gpio_req)(struct mvebu_mpp_ctrl *ctrl, u8 pid);
	int (*mpp_gpio_dir)(struct mvebu_mpp_ctrl *ctrl, u8 pid, bool input);
};

/**
 * struct mvebu_mpp_ctrl_setting - describe a mpp ctrl setting
 * @val: ctrl setting value
 * @name: ctrl setting name, e.g. uart2, spi0 - unique per mpp_mode
 * @subname: (optional) additional ctrl setting name, e.g. rts, cts
 * @variant: (optional) variant identifier mask
 * @flags: (private) flags to store gpi/gpo/gpio capabilities
 *
 * A ctrl_setting describes a specific internal mux function that a mpp pin
 * can be switched to. The value (val) will be written in the corresponding
 * register for common mpp pin configuration registers on MVEBU. SoC specific
 * mpp_get/_set function may use val to distinguish between different settings.
 *
 * The name will be used to switch to this setting in DT description, e.g.
 * marvell,function = "uart2". subname is only for debugging purposes.
 *
 * If name is one of "gpi", "gpo", "gpio" gpio capabilities are
 * parsed during initialization and stored in flags.
 *
 * The variant can be used to combine different revisions of one SoC to a
 * common pinctrl driver. It is matched (AND) with variant of soc_info to
 * determine if a setting is available on the current SoC revision.
 */
struct mvebu_mpp_ctrl_setting {
	u8 val;
	const char *name;
	const char *subname;
	u8 variant;
	u8 flags;
#define  MVEBU_SETTING_GPO	(1 << 0)
#define  MVEBU_SETTING_GPI	(1 << 1)
};

/**
 * struct mvebu_mpp_mode - link ctrl and settings
 * @pid: first pin id handled by this mode
 * @settings: list of settings available for this mode
 *
 * A mode connects all available settings with the corresponding mpp_ctrl
 * given by pid.
 */
struct mvebu_mpp_mode {
	u8 pid;
	struct mvebu_mpp_ctrl_setting *settings;
};

/**
 * struct mvebu_pinctrl_soc_info - SoC specific info passed to pinctrl-mvebu
 * @variant: variant mask of soc_info
 * @controls: list of available mvebu_mpp_ctrls
 * @ncontrols: number of available mvebu_mpp_ctrls
 * @modes: list of available mvebu_mpp_modes
 * @nmodes: number of available mvebu_mpp_modes
 * @gpioranges: list of pinctrl_gpio_ranges
 * @ngpioranges: number of available pinctrl_gpio_ranges
 *
 * This struct describes all pinctrl related information for a specific SoC.
 * If variant is unequal 0 it will be matched (AND) with variant of each
 * setting and allows to distinguish between different revisions of one SoC.
 */
struct mvebu_pinctrl_soc_info {
	u8 variant;
	struct mvebu_mpp_ctrl *controls;
	int ncontrols;
	struct mvebu_mpp_mode *modes;
	int nmodes;
	struct pinctrl_gpio_range *gpioranges;
	int ngpioranges;
};

#define MPP_REG_CTRL(_idl, _idh)				\
	{							\
		.name = NULL,					\
		.pid = _idl,					\
		.npins = _idh - _idl + 1,			\
		.pins = (unsigned[_idh - _idl + 1]) { },	\
		.mpp_get = NULL,				\
		.mpp_set = NULL,				\
		.mpp_gpio_req = NULL,				\
		.mpp_gpio_dir = NULL,				\
	}

#define MPP_FUNC_CTRL(_idl, _idh, _name, _func)			\
	{							\
		.name = _name,					\
		.pid = _idl,					\
		.npins = _idh - _idl + 1,			\
		.pins = (unsigned[_idh - _idl + 1]) { },	\
		.mpp_get = _func ## _get,			\
		.mpp_set = _func ## _set,			\
		.mpp_gpio_req = NULL,				\
		.mpp_gpio_dir = NULL,				\
	}

#define MPP_FUNC_GPIO_CTRL(_idl, _idh, _name, _func)		\
	{							\
		.name = _name,					\
		.pid = _idl,					\
		.npins = _idh - _idl + 1,			\
		.pins = (unsigned[_idh - _idl + 1]) { },	\
		.mpp_get = _func ## _get,			\
		.mpp_set = _func ## _set,			\
		.mpp_gpio_req = _func ## _gpio_req,		\
		.mpp_gpio_dir = _func ## _gpio_dir,		\
	}

#define _MPP_VAR_FUNCTION(_val, _name, _subname, _mask)		\
	{							\
		.val = _val,					\
		.name = _name,					\
		.subname = _subname,				\
		.variant = _mask,				\
		.flags = 0,					\
	}

#if defined(CONFIG_DEBUG_FS)
#define MPP_VAR_FUNCTION(_val, _name, _subname, _mask)		\
	_MPP_VAR_FUNCTION(_val, _name, _subname, _mask)
#else
#define MPP_VAR_FUNCTION(_val, _name, _subname, _mask)		\
	_MPP_VAR_FUNCTION(_val, _name, NULL, _mask)
#endif

#define MPP_FUNCTION(_val, _name, _subname)			\
	MPP_VAR_FUNCTION(_val, _name, _subname, (u8)-1)

#define MPP_MODE(_id, ...)					\
	{							\
		.pid = _id,					\
		.settings = (struct mvebu_mpp_ctrl_setting[]){	\
			__VA_ARGS__, { } },			\
	}

#define MPP_GPIO_RANGE(_id, _pinbase, _gpiobase, _npins)	\
	{							\
		.name = "mvebu-gpio",				\
		.id = _id,					\
		.pin_base = _pinbase,				\
		.base = _gpiobase,				\
		.npins = _npins,				\
	}

int mvebu_pinctrl_probe(struct platform_device *pdev);
int mvebu_pinctrl_remove(struct platform_device *pdev);

#endif
