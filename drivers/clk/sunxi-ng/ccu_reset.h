/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016 Maxime Ripard. All rights reserved.
 */

#ifndef _CCU_RESET_H_
#define _CCU_RESET_H_

#include <linux/reset-controller.h>
#include <linux/spinlock.h>

struct ccu_reset_map {
	u16	reg;
	u32	bit;
};


struct ccu_reset {
	void __iomem			*base;
	const struct ccu_reset_map	*reset_map;
	spinlock_t			*lock;

	struct reset_controller_dev	rcdev;
};

static inline struct ccu_reset *rcdev_to_ccu_reset(struct reset_controller_dev *rcdev)
{
	return container_of(rcdev, struct ccu_reset, rcdev);
}

extern const struct reset_control_ops ccu_reset_ops;

#endif /* _CCU_RESET_H_ */
