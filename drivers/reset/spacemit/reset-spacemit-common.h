/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SpacemiT reset controller driver - common definitions
 */

#ifndef _RESET_SPACEMIT_COMMON_H_
#define _RESET_SPACEMIT_COMMON_H_

#include <linux/auxiliary_bus.h>
#include <linux/regmap.h>
#include <linux/reset-controller.h>
#include <linux/types.h>

struct ccu_reset_data {
	u32 offset;
	u32 assert_mask;
	u32 deassert_mask;
};

struct ccu_reset_controller_data {
	const struct ccu_reset_data *reset_data;	/* array */
	size_t count;
};

struct ccu_reset_controller {
	struct reset_controller_dev rcdev;
	const struct ccu_reset_controller_data *data;
	struct regmap *regmap;
};

#define RESET_DATA(_offset, _assert_mask, _deassert_mask)	\
	{							\
		.offset		= (_offset),			\
		.assert_mask	= (_assert_mask),		\
		.deassert_mask	= (_deassert_mask),		\
	}

/* Common probe function */
int spacemit_reset_probe(struct auxiliary_device *adev,
			 const struct auxiliary_device_id *id);

#endif /* _RESET_SPACEMIT_COMMON_H_ */
