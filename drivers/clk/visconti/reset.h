/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Toshiba ARM SoC reset controller driver
 *
 * Copyright (c) 2021 TOSHIBA CORPORATION
 *
 * Nobuhiro Iwamatsu <nobuhiro1.iwamatsu@toshiba.co.jp>
 */

#ifndef _VISCONTI_RESET_H_
#define _VISCONTI_RESET_H_

#include <linux/reset-controller.h>

struct visconti_reset_data {
	u32	rson_offset;
	u32	rsoff_offset;
	u8	rs_idx;
};

struct visconti_reset {
	struct reset_controller_dev rcdev;
	struct regmap *regmap;
	const struct visconti_reset_data *resets;
	spinlock_t *lock;
};

extern const struct reset_control_ops visconti_reset_ops;

int visconti_register_reset_controller(struct device *dev,
				       struct regmap *regmap,
				       const struct visconti_reset_data *resets,
				       unsigned int num_resets,
				       const struct reset_control_ops *reset_ops,
				       spinlock_t *lock);
#endif /* _VISCONTI_RESET_H_ */
