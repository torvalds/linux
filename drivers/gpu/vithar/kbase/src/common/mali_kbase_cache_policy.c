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
 *
 * The input flags may contain a combination of hints:
 * BASE_MEM_HINT_CPU_RD: region heavily read CPU side
 * BASE_MEM_HINT_CPU_WR: region heavily written CPU side
 * BASE_MEM_HINT_GPU_RD: region heavily read GPU side
 * BASE_MEM_HINT_GPU_WR: region heavily written GPU side
 */
u32 kbase_cache_enabled(u32 flags, u32 nr_pages)
{
	u32 cache_flags = 0;

	CSTD_UNUSED(nr_pages);

	/* The CPU cache should be enabled for regions heavily read and written
	 * from the CPU side
	 */
#if !MALI_UNCACHED
	if ((flags & BASE_MEM_HINT_CPU_RD) && (flags & BASE_MEM_HINT_CPU_WR))
	{
		cache_flags |= KBASE_REG_CPU_CACHED;
	}
#endif

	/* The GPU cache should be enabled for regions heavily read and written
	 * from the GPU side
	 */
	if ((flags & BASE_MEM_HINT_GPU_RD) && (flags & BASE_MEM_HINT_GPU_WR))
	{
		cache_flags |= KBASE_REG_GPU_CACHED;
	}

	return cache_flags;
}

