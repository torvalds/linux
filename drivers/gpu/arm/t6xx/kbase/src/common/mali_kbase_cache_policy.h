/*
 *
 * (C) COPYRIGHT 2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */



/**
 * @file mali_kbase_cache_policy.h
 * Cache Policy API.
 */

#ifndef _KBASE_CACHE_POLICY_H_
#define _KBASE_CACHE_POLICY_H_

#include <malisw/mali_malisw.h>
#include "mali_kbase.h"
#include <kbase/mali_base_kernel.h>

/**
 * @brief Choose the cache policy for a specific region
 *
 * Tells whether the CPU and GPU caches should be enabled or not for a specific region.
 * This function can be modified to customize the cache policy depending on the flags
 * and size of the region.
 *
 * @param[in] flags     flags describing attributes of the region
 * @param[in] nr_pages  total number of pages (backed or not) for the region
 *
 * @return a combination of KBASE_REG_CPU_CACHED and KBASE_REG_GPU_CACHED depending
 * on the cache policy
 */
u32 kbase_cache_enabled(u32 flags, u32 nr_pages);

#endif /* _KBASE_CACHE_POLICY_H_ */

