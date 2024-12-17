/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (c) 2024 BayLibre, SAS.
 * Author: Jerome Brunet <jbrunet@baylibre.com>
 */

#ifndef __MESON_RESET_H
#define __MESON_RESET_H

#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/reset-controller.h>

struct meson_reset_param {
	const struct reset_control_ops *reset_ops;
	unsigned int reset_num;
	unsigned int reset_offset;
	unsigned int level_offset;
	bool level_low_reset;
};

int meson_reset_controller_register(struct device *dev, struct regmap *map,
				    const struct meson_reset_param *param);

extern const struct reset_control_ops meson_reset_ops;
extern const struct reset_control_ops meson_reset_toggle_ops;

#endif /* __MESON_RESET_H */
