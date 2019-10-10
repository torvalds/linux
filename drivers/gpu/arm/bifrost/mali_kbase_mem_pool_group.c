/*
 *
 * (C) COPYRIGHT 2019 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
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
 * SPDX-License-Identifier: GPL-2.0
 *
 */

#include <mali_kbase.h>
#include <mali_kbase_mem.h>
#include <mali_kbase_mem_pool_group.h>

#include <linux/memory_group_manager.h>

void kbase_mem_pool_group_config_set_max_size(
	struct kbase_mem_pool_group_config *const configs,
	size_t const max_size)
{
	size_t const large_max_size = max_size >>
		(KBASE_MEM_POOL_2MB_PAGE_TABLE_ORDER -
		KBASE_MEM_POOL_4KB_PAGE_TABLE_ORDER);
	int gid;

	for (gid = 0; gid < MEMORY_GROUP_MANAGER_NR_GROUPS; ++gid) {
		kbase_mem_pool_config_set_max_size(&configs->small[gid],
			max_size);

		kbase_mem_pool_config_set_max_size(&configs->large[gid],
			large_max_size);
	}
}

int kbase_mem_pool_group_init(
	struct kbase_mem_pool_group *const mem_pools,
	struct kbase_device *const kbdev,
	const struct kbase_mem_pool_group_config *const configs,
	struct kbase_mem_pool_group *next_pools)
{
	int gid, err = 0;

	for (gid = 0; gid < MEMORY_GROUP_MANAGER_NR_GROUPS; ++gid) {
		err = kbase_mem_pool_init(&mem_pools->small[gid],
			&configs->small[gid],
			KBASE_MEM_POOL_4KB_PAGE_TABLE_ORDER,
			gid,
			kbdev,
			next_pools ? &next_pools->small[gid] : NULL);

		if (!err) {
			err = kbase_mem_pool_init(&mem_pools->large[gid],
				&configs->large[gid],
				KBASE_MEM_POOL_2MB_PAGE_TABLE_ORDER,
				gid,
				kbdev,
				next_pools ? &next_pools->large[gid] : NULL);
			if (err)
				kbase_mem_pool_term(&mem_pools->small[gid]);
		}

		/* Break out of the loop early to avoid incrementing the count
		 * of memory pool pairs successfully initialized.
		 */
		if (err)
			break;
	}

	if (err) {
		/* gid gives the number of memory pool pairs successfully
		 * initialized, which is one greater than the array index of the
		 * last group.
		 */
		while (gid-- > 0) {
			kbase_mem_pool_term(&mem_pools->small[gid]);
			kbase_mem_pool_term(&mem_pools->large[gid]);
		}
	}

	return err;
}

void kbase_mem_pool_group_mark_dying(
	struct kbase_mem_pool_group *const mem_pools)
{
	int gid;

	for (gid = 0; gid < MEMORY_GROUP_MANAGER_NR_GROUPS; ++gid) {
		kbase_mem_pool_mark_dying(&mem_pools->small[gid]);
		kbase_mem_pool_mark_dying(&mem_pools->large[gid]);
	}
}

void kbase_mem_pool_group_term(
	struct kbase_mem_pool_group *const mem_pools)
{
	int gid;

	for (gid = 0; gid < MEMORY_GROUP_MANAGER_NR_GROUPS; ++gid) {
		kbase_mem_pool_term(&mem_pools->small[gid]);
		kbase_mem_pool_term(&mem_pools->large[gid]);
	}
}
