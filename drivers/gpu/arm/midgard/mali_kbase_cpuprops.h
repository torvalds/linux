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



#include "mali_kbase.h"
#ifdef BASE_LEGACY_UK7_SUPPORT

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
 * This file is kept for the backward compatibility reasons.
 * It shall be removed as soon as KBASE_FUNC_CPU_PROPS_REG_DUMP_OBSOLETE
 * (previously KBASE_FUNC_CPU_PROPS_REG_DUMP) ioctl call
 * is removed. Removal of KBASE_FUNC_CPU_PROPS_REG_DUMP is part of having
 * the function for reading cpu properties moved from base to osu.
 */

/**
 * @brief Provides CPU properties data.
 *
 * Fill the struct kbase_uk_cpuprops with values from CPU configuration.
 *
 * @param kctx         The kbase context
 * @param kbase_props  A copy of the struct kbase_uk_cpuprops structure from userspace
 *
 * @return MALI_ERROR_NONE on success. Any other value indicates failure.
 */
mali_error kbase_cpuprops_uk_get_props(struct kbase_context *kctx, struct kbase_uk_cpuprops * const kbase_props);

#endif /*_KBASE_CPUPROPS_H_*/
#endif /* BASE_LEGACY_UK7_SUPPORT */
