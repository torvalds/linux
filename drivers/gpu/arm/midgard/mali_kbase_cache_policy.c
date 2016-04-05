/*
 *
 * (C) COPYRIGHT 2012-2016 ARM Limited. All rights reserved.
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





/*
 * Cache Policy API.
 */

#include "mali_kbase_cache_policy.h"

/*
 * The output flags should be a combination of the following values:
 * KBASE_REG_CPU_CACHED: CPU cache should be enabled.
 */
u32 kbase_cache_enabled(u32 flags, u32 nr_pages)
{
	u32 cache_flags = 0;

	CSTD_UNUSED(nr_pages);

	if (flags & BASE_MEM_CACHED_CPU)
		cache_flags |= KBASE_REG_CPU_CACHED;

	return cache_flags;
}


void kbase_sync_single_for_device(struct kbase_device *kbdev, dma_addr_t handle,
		size_t size, enum dma_data_direction dir)
{
/* Check if kernel is using coherency with GPU */
#ifdef CONFIG_MALI_COH_KERN
	if (kbdev->system_coherency == COHERENCY_ACE)
		return;
#endif /* CONFIG_MALI_COH_KERN */
	dma_sync_single_for_device(kbdev->dev, handle, size, dir);
}


void kbase_sync_single_for_cpu(struct kbase_device *kbdev, dma_addr_t handle,
		size_t size, enum dma_data_direction dir)
{
/* Check if kernel is using coherency with GPU */
#ifdef CONFIG_MALI_COH_KERN
	if (kbdev->system_coherency == COHERENCY_ACE)
		return;
#endif /* CONFIG_MALI_COH_KERN */
	dma_sync_single_for_cpu(kbdev->dev, handle, size, dir);
}
