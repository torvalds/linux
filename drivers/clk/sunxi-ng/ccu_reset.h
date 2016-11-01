/*
 * Copyright (c) 2016 Maxime Ripard. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _CCU_RESET_H_
#define _CCU_RESET_H_

#include <linux/reset-controller.h>

struct ccu_reset_map {
	u16	reg;
	u32	bit;
};


struct ccu_reset {
	void __iomem			*base;
	struct ccu_reset_map		*reset_map;
	spinlock_t			*lock;

	struct reset_controller_dev	rcdev;
};

static inline struct ccu_reset *rcdev_to_ccu_reset(struct reset_controller_dev *rcdev)
{
	return container_of(rcdev, struct ccu_reset, rcdev);
}

extern const struct reset_control_ops ccu_reset_ops;

#endif /* _CCU_RESET_H_ */
