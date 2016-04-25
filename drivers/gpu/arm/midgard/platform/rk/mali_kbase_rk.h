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

/*---------------------------------------------------------------------------*/

/**
 * struct rk_context - work_context of platform_dependent_part_of_rk.
 * @is_powered: record the status
 *      of common_parts calling 'power_on_callback' and 'power_off_callback'.
 */
struct rk_context {
	bool is_powered;
};

#endif				/* _MALI_KBASE_RK_H_ */

