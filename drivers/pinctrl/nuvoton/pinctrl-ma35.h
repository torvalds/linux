/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2024 Nuvoton Technology Corp.
 *
 * Author: Shan-Chun Hung <schung@nuvoton.com>
 * *       Jacky Huang <ychuang3@nuvoton.com>
 */
#ifndef __PINCTRL_MA35_H
#define __PINCTRL_MA35_H

#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/platform_device.h>

struct ma35_mux_desc {
	const char *name;
	u32 muxval;
};

struct ma35_pin_data {
	u32 offset;
	u32 shift;
	struct ma35_mux_desc *muxes;
};

struct ma35_pinctrl_soc_info {
	const struct pinctrl_pin_desc *pins;
	unsigned int npins;
	int (*get_pin_num)(int offset, int shift);
};

#define MA35_PIN(num, n, o, s, ...) {			\
	.number = num,					\
	.name = #n,					\
	.drv_data = &(struct ma35_pin_data) {		\
		.offset = o,				\
		.shift = s,				\
		.muxes = (struct ma35_mux_desc[]) {	\
			 __VA_ARGS__, { } },		\
	},						\
}

#define MA35_MUX(_val, _name) {				\
	.name = _name,					\
	.muxval = _val,					\
}

int ma35_pinctrl_probe(struct platform_device *pdev, const struct ma35_pinctrl_soc_info *info);
int ma35_pinctrl_suspend(struct device *dev);
int ma35_pinctrl_resume(struct device *dev);

#endif /* __PINCTRL_MA35_H */
