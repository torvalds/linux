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

#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinmux.h>

struct platform_device;

extern struct pinmux_ops imx_pmx_ops;

/**
 * struct imx_pin - describes a single i.MX pin
 * @pin: the pin_id of this pin
 * @mux_mode: the mux mode for this pin.
 * @input_reg: the select input register offset for this pin if any
 *	0 if no select input setting needed.
 * @input_val: the select input value for this pin.
 * @configs: the config for this pin.
 */
struct imx_pin {
	unsigned int pin;
	unsigned int mux_mode;
	u16 input_reg;
	unsigned int input_val;
	unsigned long config;
};

/**
 * struct imx_pin_reg - describe a pin reg map
 * @mux_reg: mux register offset
 * @conf_reg: config register offset
 */
struct imx_pin_reg {
	s16 mux_reg;
	s16 conf_reg;
};

/* decode a generic config into raw register value */
struct imx_cfg_params_decode {
	enum pin_config_param param;
	u32 mask;
	u8 shift;
	bool invert;
};

struct imx_pinctrl_soc_info {
	const struct pinctrl_pin_desc *pins;
	unsigned int npins;
	unsigned int flags;
	const char *gpr_compatible;

	/* MUX_MODE shift and mask in case SHARE_MUX_CONF_REG */
	unsigned int mux_mask;
	u8 mux_shift;

	/* generic pinconf */
	bool generic_pinconf;
	const struct pinconf_generic_params *custom_params;
	unsigned int num_custom_params;
	const struct imx_cfg_params_decode *decodes;
	unsigned int num_decodes;
	void (*fixup)(unsigned long *configs, unsigned int num_configs,
		      u32 *raw_config);

	int (*gpio_set_direction)(struct pinctrl_dev *pctldev,
				  struct pinctrl_gpio_range *range,
				  unsigned offset,
				  bool input);
};

/**
 * @dev: a pointer back to containing device
 * @base: the offset to the controller in virtual memory
 */
struct imx_pinctrl {
	struct device *dev;
	struct pinctrl_dev *pctl;
	void __iomem *base;
	void __iomem *input_sel_base;
	const struct imx_pinctrl_soc_info *info;
	struct imx_pin_reg *pin_regs;
	unsigned int group_index;
	struct mutex mutex;
};

#define IMX_CFG_PARAMS_DECODE(p, m, o) \
	{ .param = p, .mask = m, .shift = o, .invert = false, }

#define IMX_CFG_PARAMS_DECODE_INVERT(p, m, o) \
	{ .param = p, .mask = m, .shift = o, .invert = true, }

#define SHARE_MUX_CONF_REG	0x1
#define ZERO_OFFSET_VALID	0x2

#define NO_MUX		0x0
#define NO_PAD		0x0

#define IMX_PINCTRL_PIN(pin) PINCTRL_PIN(pin, #pin)

#define PAD_CTL_MASK(len)	((1 << len) - 1)
#define IMX_MUX_MASK	0x7
#define IOMUXC_CONFIG_SION	(0x1 << 4)

int imx_pinctrl_probe(struct platform_device *pdev,
			const struct imx_pinctrl_soc_info *info);
#endif /* __DRIVERS_PINCTRL_IMX_H */
