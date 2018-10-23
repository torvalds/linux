/*
 * Pinctrl driver for the Wondermedia SoC's
 *
 * Copyright (c) 2013 Tony Prisk <linux@prisktech.co.nz>
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

#include <linux/gpio/driver.h>

/* VT8500 has no enable register in the extgpio bank. */
#define NO_REG	0xFFFF

#define WMT_PINCTRL_BANK(__en, __dir, __dout, __din, __pen, __pcfg)	\
{									\
	.reg_en		= __en,						\
	.reg_dir	= __dir,					\
	.reg_data_out	= __dout,					\
	.reg_data_in	= __din,					\
	.reg_pull_en	= __pen,					\
	.reg_pull_cfg	= __pcfg,					\
}

/* Encode/decode the bank/bit pairs into a pin value */
#define WMT_PIN(__bank, __offset)	((__bank << 5) | __offset)
#define WMT_BANK_FROM_PIN(__pin)	(__pin >> 5)
#define WMT_BIT_FROM_PIN(__pin)		(__pin & 0x1f)

#define WMT_GROUP(__name, __data)		\
{						\
	.name = __name,				\
	.pins = __data,				\
	.npins = ARRAY_SIZE(__data),		\
}

struct wmt_pinctrl_bank_registers {
	u32	reg_en;
	u32	reg_dir;
	u32	reg_data_out;
	u32	reg_data_in;

	u32	reg_pull_en;
	u32	reg_pull_cfg;
};

struct wmt_pinctrl_group {
	const char *name;
	const unsigned int *pins;
	const unsigned npins;
};

struct wmt_pinctrl_data {
	struct device *dev;
	struct pinctrl_dev *pctl_dev;

	/* must be initialized before calling wmt_pinctrl_probe */
	void __iomem *base;
	const struct wmt_pinctrl_bank_registers *banks;
	const struct pinctrl_pin_desc *pins;
	const char * const *groups;

	u32 nbanks;
	u32 npins;
	u32 ngroups;

	struct gpio_chip gpio_chip;
	struct pinctrl_gpio_range gpio_range;
};

int wmt_pinctrl_probe(struct platform_device *pdev,
		      struct wmt_pinctrl_data *data);
