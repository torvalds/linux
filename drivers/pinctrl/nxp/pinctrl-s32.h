/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * S32 pinmux core definitions
 *
 * Copyright 2016-2020, 2022 NXP
 * Copyright (C) 2022 SUSE LLC
 * Copyright 2015-2016 Freescale Semiconductor, Inc.
 * Copyright (C) 2012 Linaro Ltd.
 */

#ifndef __DRIVERS_PINCTRL_S32_H
#define __DRIVERS_PINCTRL_S32_H

struct platform_device;

/**
 * struct s32_pin_group - describes an S32 pin group
 * @data: generic data describes group name, number of pins, and a pin array in
	this group.
 * @pin_sss: an array of source signal select configs paired with pin array.
 */
struct s32_pin_group {
	struct pingroup data;
	unsigned int *pin_sss;
};

/**
 * struct s32_pin_range - pin ID range for each memory region.
 * @start: start pin ID
 * @end: end pin ID
 */
struct s32_pin_range {
	unsigned int start;
	unsigned int end;
};

struct s32_pinctrl_soc_data {
	const struct pinctrl_pin_desc *pins;
	unsigned int npins;
	const struct s32_pin_range *mem_pin_ranges;
	unsigned int mem_regions;
};

struct s32_pinctrl_soc_info {
	struct device *dev;
	const struct s32_pinctrl_soc_data *soc_data;
	struct s32_pin_group *groups;
	unsigned int ngroups;
	struct pinfunction *functions;
	unsigned int nfunctions;
	unsigned int grp_index;
};

#define S32_PINCTRL_PIN(pin)	PINCTRL_PIN(pin, #pin)
#define S32_PIN_RANGE(_start, _end) { .start = _start, .end = _end }

int s32_pinctrl_probe(struct platform_device *pdev,
		      const struct s32_pinctrl_soc_data *soc_data);
int s32_pinctrl_resume(struct device *dev);
int s32_pinctrl_suspend(struct device *dev);
#endif /* __DRIVERS_PINCTRL_S32_H */
