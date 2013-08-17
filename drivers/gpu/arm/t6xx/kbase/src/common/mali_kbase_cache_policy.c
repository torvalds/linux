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

#include "mali_kbase_cache_policy.h"

/*
 * The output flags should be a combination of the following values:
 * KBASE_REG_CPU_CACHED: CPU cache should be enabled
 * KBASE_REG_GPU_CACHED: GPU cache should be enabled
 */
u32 kbase_cache_enabled(u32 flags, u32 nr_pages)
{
	u32 cache_flags = 0;

	CSTD_UNUSED(nr_pages);

	if (flags & BASE_MEM_CACHED_CPU)
	{
		cache_flags |= KBASE_REG_CPU_CACHED;
	}

	if (flags & BASE_MEM_CACHED_GPU)
	{
		cache_flags |= KBASE_REG_GPU_CACHED;
	}

	return cache_flags;
}
