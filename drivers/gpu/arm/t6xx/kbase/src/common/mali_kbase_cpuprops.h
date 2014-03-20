/*
 *
 * (C) COPYRIGHT ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */





/**
 * @file mali_kbase_cpuprops.h
 * Base kernel property query APIs
 */

#ifndef _KBASE_CPUPROPS_H_
#define _KBASE_CPUPROPS_H_

#include <malisw/mali_malisw.h>

/* Forward declarations */
struct kbase_uk_cpuprops;

/**
 * @brief Default implementation of @ref KBASE_CONFIG_ATTR_CPU_SPEED_FUNC.
 *
 * This function sets clock_speed to 100, so will be an underestimate for
 * any real system.
 *
 * See @ref kbase_cpuprops_clock_speed_function for details on the parameters
 * and return value.
 */
int kbase_cpuprops_get_default_clock_speed(u32 * const clock_speed);

/**
 * @brief Provides CPU properties data.
 *
 * Fill the kbase_uk_cpuprops with values from CPU configuration.
 *
 * @param kctx         The kbase context
 * @param kbase_props  A copy of the kbase_uk_cpuprops structure from userspace
 *
 * @return MALI_ERROR_NONE on success. Any other value indicates failure.
 */
mali_error kbase_cpuprops_uk_get_props(kbase_context *kctx, struct kbase_uk_cpuprops * const kbase_props);

#endif /*_KBASE_CPUPROPS_H_*/
