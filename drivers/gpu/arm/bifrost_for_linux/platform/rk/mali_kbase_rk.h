/* drivers/gpu/t6xx/kbase/src/platform/rk/mali_kbase_platform.h
 * Rockchip SoC Mali-Midgard platform-dependent codes
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software FoundatIon.
 */

/**
 * @file mali_kbase_rk.h
 *
 * defines work_context type of platform_dependent_part.
 */

#ifndef _MALI_KBASE_RK_H_
#define _MALI_KBASE_RK_H_

#include <linux/wakelock.h>

/*---------------------------------------------------------------------------*/

#define DEFAULT_UTILISATION_PERIOD_IN_MS (100)

/*---------------------------------------------------------------------------*/

/*
 * struct rk_context - work_context of platform_dependent_part_of_rk.
 */
struct rk_context {
	/*
	 * record the status of common_parts calling 'power_on_callback'
	 * and 'power_off_callback'.
	 */
	bool is_powered;

	struct kbase_device *kbdev;

	struct workqueue_struct *power_off_wq;
	/* delayed_work_to_power_off_gpu. */
	struct delayed_work work;
	unsigned int delay_ms;

	/*
	 * WAKE_LOCK_SUSPEND for ensuring to run
	 * delayed_work_to_power_off_gpu before suspend.
	 */
	struct wake_lock wake_lock;

	/* debug only, the period in ms to count gpu_utilisation. */
	unsigned int utilisation_period;
};

/*---------------------------------------------------------------------------*/

static inline struct rk_context *get_rk_context(
		const struct kbase_device *kbdev)
{
	return (struct rk_context *)(kbdev->platform_context);
}

#endif				/* _MALI_KBASE_RK_H_ */

