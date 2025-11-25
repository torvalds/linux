/* SPDX-License-Identifier: GPL-2.0 or MIT */
/* Copyright 2025 ARM Limited. All rights reserved. */

#ifndef __PANTHOR_HW_H__
#define __PANTHOR_HW_H__

#include "panthor_device.h"

/**
 * struct panthor_hw_ops - HW operations that are specific to a GPU
 */
struct panthor_hw_ops {
	/** @soft_reset: Soft reset function pointer */
	int (*soft_reset)(struct panthor_device *ptdev);

	/** @l2_power_off: L2 power off function pointer */
	void (*l2_power_off)(struct panthor_device *ptdev);

	/** @l2_power_on: L2 power on function pointer */
	int (*l2_power_on)(struct panthor_device *ptdev);
};

/**
 * struct panthor_hw - GPU specific register mapping and functions
 */
struct panthor_hw {
	/** @features: Bitmap containing panthor_hw_feature */

	/** @ops: Panthor HW specific operations */
	struct panthor_hw_ops ops;
};

int panthor_hw_init(struct panthor_device *ptdev);

static inline int panthor_hw_soft_reset(struct panthor_device *ptdev)
{
	return ptdev->hw->ops.soft_reset(ptdev);
}

static inline int panthor_hw_l2_power_on(struct panthor_device *ptdev)
{
	return ptdev->hw->ops.l2_power_on(ptdev);
}

static inline void panthor_hw_l2_power_off(struct panthor_device *ptdev)
{
	ptdev->hw->ops.l2_power_off(ptdev);
}

#endif /* __PANTHOR_HW_H__ */
