// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2012-2018, 2020-2021 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

/*
 * Cache Policy API.
 */

#include "mali_kbase_cache_policy.h"

/*
 * The output flags should be a combination of the following values:
 * KBASE_REG_CPU_CACHED: CPU cache should be enabled
 * KBASE_REG_GPU_CACHED: GPU cache should be enabled
 *
 * NOTE: Some components within the GPU might only be able to access memory
 * that is KBASE_REG_GPU_CACHED. Refer to the specific GPU implementation for
 * more details.
 */
u32 kbase_cache_enabled(u32 flags, u32 nr_pages)
{
	u32 cache_flags = 0;

	CSTD_UNUSED(nr_pages);

	if (!(flags & BASE_MEM_UNCACHED_GPU))
		cache_flags |= KBASE_REG_GPU_CACHED;

	if (flags & BASE_MEM_CACHED_CPU)
		cache_flags |= KBASE_REG_CPU_CACHED;

	return cache_flags;
}


void kbase_sync_single_for_device(struct kbase_device *kbdev, dma_addr_t handle,
		size_t size, enum dma_data_direction dir)
{
	dma_sync_single_for_device(kbdev->dev, handle, size, dir);
}
KBASE_EXPORT_TEST_API(kbase_sync_single_for_device);

void kbase_sync_single_for_cpu(struct kbase_device *kbdev, dma_addr_t handle,
		size_t size, enum dma_data_direction dir)
{
	dma_sync_single_for_cpu(kbdev->dev, handle, size, dir);
}
KBASE_EXPORT_TEST_API(kbase_sync_single_for_cpu);
