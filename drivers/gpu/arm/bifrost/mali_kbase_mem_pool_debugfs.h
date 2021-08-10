/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2014-2021 ARM Limited. All rights reserved.
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

#ifndef _KBASE_MEM_POOL_DEBUGFS_H_
#define _KBASE_MEM_POOL_DEBUGFS_H_

#include <mali_kbase.h>

/**
 * kbase_mem_pool_debugfs_init - add debugfs knobs for @pool
 * @parent:  Parent debugfs dentry
 * @kctx:    The kbase context
 *
 * Adds four debugfs files under @parent:
 * - mem_pool_size: get/set the current sizes of @kctx: mem_pools
 * - mem_pool_max_size: get/set the max sizes of @kctx: mem_pools
 * - lp_mem_pool_size: get/set the current sizes of @kctx: lp_mem_pool
 * - lp_mem_pool_max_size: get/set the max sizes of @kctx:lp_mem_pool
 */
void kbase_mem_pool_debugfs_init(struct dentry *parent,
		struct kbase_context *kctx);

/**
 * kbase_mem_pool_debugfs_trim - Grow or shrink a memory pool to a new size
 *
 * @array: Address of the first in an array of physical memory pools.
 * @index: A memory group ID to be used as an index into the array of memory
 *         pools. Valid range is 0..(MEMORY_GROUP_MANAGER_NR_GROUPS-1).
 * @value: New number of pages in the pool.
 *
 * If @value > current size, fill the pool with new pages from the kernel, but
 * not above the max_size for the pool.
 * If @value < current size, shrink the pool by freeing pages to the kernel.
 */
void kbase_mem_pool_debugfs_trim(void *array, size_t index, size_t value);

/**
 * kbase_mem_pool_debugfs_set_max_size - Set maximum number of free pages in
 *                                       memory pool
 *
 * @array: Address of the first in an array of physical memory pools.
 * @index: A memory group ID to be used as an index into the array of memory
 *         pools. Valid range is 0..(MEMORY_GROUP_MANAGER_NR_GROUPS-1).
 * @value: Maximum number of free pages the pool can hold.
 *
 * If the maximum size is reduced, the pool will be shrunk to adhere to the
 * new limit. For details see kbase_mem_pool_shrink().
 */
void kbase_mem_pool_debugfs_set_max_size(void *array, size_t index,
	size_t value);

/**
 * kbase_mem_pool_debugfs_size - Get number of free pages in a memory pool
 *
 * @array: Address of the first in an array of physical memory pools.
 * @index: A memory group ID to be used as an index into the array of memory
 *         pools. Valid range is 0..(MEMORY_GROUP_MANAGER_NR_GROUPS-1).
 *
 * Note: the size of the pool may in certain corner cases exceed @max_size!
 *
 * Return: Number of free pages in the pool
 */
size_t kbase_mem_pool_debugfs_size(void *array, size_t index);

/**
 * kbase_mem_pool_debugfs_max_size - Get maximum number of free pages in a
 *                                   memory pool
 *
 * @array: Address of the first in an array of physical memory pools.
 * @index: A memory group ID to be used as an index into the array of memory
 *         pools. Valid range is 0..(MEMORY_GROUP_MANAGER_NR_GROUPS-1).
 *
 * Return: Maximum number of free pages in the pool
 */
size_t kbase_mem_pool_debugfs_max_size(void *array, size_t index);

/**
 * kbase_mem_pool_config_debugfs_set_max_size - Set maximum number of free pages
 *                                              in initial configuration of pool
 *
 * @array:  Array of initial configurations for a set of physical memory pools.
 * @index:  A memory group ID to be used as an index into the array.
 *          Valid range is 0..(MEMORY_GROUP_MANAGER_NR_GROUPS-1).
 * @value : Maximum number of free pages that a memory pool created from the
 *          selected configuration can hold.
 */
void kbase_mem_pool_config_debugfs_set_max_size(void *array, size_t index,
	size_t value);

/**
 * kbase_mem_pool_config_debugfs_max_size - Get maximum number of free pages
 *                                          from initial configuration of pool
 *
 * @array:  Array of initial configurations for a set of physical memory pools.
 * @index:  A memory group ID to be used as an index into the array.
 *          Valid range is 0..(MEMORY_GROUP_MANAGER_NR_GROUPS-1).
 *
 * Return: Maximum number of free pages that a memory pool created from the
 *         selected configuration can hold.
 */
size_t kbase_mem_pool_config_debugfs_max_size(void *array, size_t index);

#endif  /*_KBASE_MEM_POOL_DEBUGFS_H_ */

