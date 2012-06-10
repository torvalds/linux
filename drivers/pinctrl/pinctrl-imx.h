/*
 * IMX pinmux core definitions
 *
 * Copyright (C) 2012 Freescale Semiconductor, Inc.
 * Copyright (C) 2012 Linaro Ltd.
 *
 * Author: Dong Aisheng <dong.aisheng@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __DRIVERS_PINCTRL_IMX_H
#define __DRIVERS_PINCTRL_IMX_H

struct platform_device;

/**
 * struct imx_pin_group - describes an IMX pin group
 * @name: the name of this specific pin group
 * @pins: an array of discrete physical pins used in this group, taken
 *	from the driver-local pin enumeration space
 * @npins: the number of pins in this group array, i.e. the number of
 *	elements in .pins so we can iterate over that array
 * @mux_mode: the mux mode for each pin in this group. The size of this
 *	array is the same as pins.
 * @configs: the config for each pin in this group. The size of this
 *	array is the same as pins.
 */
struct imx_pin_group {
	const char *name;
	unsigned int *pins;
	unsigned npins;
	unsigned int *mux_mode;
	unsigned long *configs;
};

/**
 * struct imx_pmx_func - describes IMX pinmux functions
 * @name: the name of this specific function
 * @groups: corresponding pin groups
 * @num_groups: the number of groups
 */
struct imx_pmx_func {
	const char *name;
	const char **groups;
	unsigned num_groups;
};

/**
 * struct imx_pin_reg - describe a pin reg map
 * The last 3 members are used for select input setting
 * @pid: pin id
 * @mux_reg: mux register offset
 * @conf_reg: config register offset
 * @mux_mode: mux mode
 * @input_reg: select input register offset for this mux if any
 *  0 if no select input setting needed.
 * @input_val: the value set to select input register
 */
struct imx_pin_reg {
	u16 pid;
	u16 mux_reg;
	u16 conf_reg;
	u8 mux_mode;
	u16 input_reg;
	u8 input_val;
};

struct imx_pinctrl_soc_info {
	struct device *dev;
	const struct pinctrl_pin_desc *pins;
	unsigned int npins;
	const struct imx_pin_reg *pin_regs;
	unsigned int npin_regs;
	struct imx_pin_group *groups;
	unsigned int ngroups;
	struct imx_pmx_func *functions;
	unsigned int nfunctions;
};

#define NO_MUX		0x0
#define NO_PAD		0x0

#define IMX_PIN_REG(id, conf, mux, mode, input, val)	\
	{						\
		.pid = id,				\
		.conf_reg = conf,			\
		.mux_reg = mux,				\
		.mux_mode  = mode,			\
		.input_reg = input,			\
		.input_val = val,			\
	}

#define IMX_PINCTRL_PIN(pin) PINCTRL_PIN(pin, #pin)

#define PAD_CTL_MASK(len)	((1 << len) - 1)
#define IMX_MUX_MASK	0x7
#define IOMUXC_CONFIG_SION	(0x1 << 4)

int imx_pinctrl_probe(struct platform_device *pdev,
			struct imx_pinctrl_soc_info *info);
int imx_pinctrl_remove(struct platform_device *pdev);
#endif /* __DRIVERS_PINCTRL_IMX_H */
