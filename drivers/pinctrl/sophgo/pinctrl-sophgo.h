/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2024 Inochi Amaoto <inochiama@outlook.com>
 */

#ifndef _PINCTRL_SOPHGO_H
#define _PINCTRL_SOPHGO_H

#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/spinlock.h>

#include "../core.h"

struct sophgo_pin {
	u16				id;
	u16				flags;
};

struct sophgo_pin_mux_config {
	const struct sophgo_pin	*pin;
	u32			config;
};

/**
 * struct sophgo_vddio_cfg_ops - pin vddio operations
 *
 * @get_pull_up: get resistor for pull up;
 * @get_pull_down: get resistor for pull down.
 * @get_oc_map: get mapping for typical low level output current value to
 *	register value map.
 * @get_schmitt_map: get mapping for register value to typical schmitt
 *	threshold.
 */
struct sophgo_vddio_cfg_ops {
	int (*get_pull_up)(const struct sophgo_pin *pin, const u32 *psmap);
	int (*get_pull_down)(const struct sophgo_pin *pin, const u32 *psmap);
	int (*get_oc_map)(const struct sophgo_pin *pin, const u32 *psmap,
			  const u32 **map);
	int (*get_schmitt_map)(const struct sophgo_pin *pin, const u32 *psmap,
			       const u32 **map);
};

struct sophgo_pinctrl_data {
	const struct pinctrl_pin_desc		*pins;
	const void				*pindata;
	const char				* const *pdnames;
	const struct sophgo_vddio_cfg_ops	*vddio_ops;
	u16					npins;
	u16					npds;
	u16					pinsize;
};

struct sophgo_pinctrl {
	struct device				*dev;
	struct pinctrl_dev			*pctrl_dev;
	const struct sophgo_pinctrl_data	*data;
	struct pinctrl_desc			pdesc;

	struct mutex				mutex;
	raw_spinlock_t				lock;
	void					*priv_ctrl;
};

#endif /* _PINCTRL_SOPHGO_H */
