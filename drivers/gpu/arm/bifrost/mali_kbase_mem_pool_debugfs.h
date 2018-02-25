/*
 *
 * (C) COPYRIGHT 2014-2015, 2017 ARM Limited. All rights reserved.
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

#ifndef _KBASE_MEM_POOL_DEBUGFS_H
#define _KBASE_MEM_POOL_DEBUGFS_H

#include <mali_kbase.h>

/**
 * kbase_mem_pool_debugfs_init - add debugfs knobs for @pool
 * @parent:  Parent debugfs dentry
 * @pool:    Memory pool of small pages to control
 * @lp_pool: Memory pool of large pages to control
 *
 * Adds four debugfs files under @parent:
 * - mem_pool_size: get/set the current size of @pool
 * - mem_pool_max_size: get/set the max size of @pool
 * - lp_mem_pool_size: get/set the current size of @lp_pool
 * - lp_mem_pool_max_size: get/set the max size of @lp_pool
 */
void kbase_mem_pool_debugfs_init(struct dentry *parent,
		struct kbase_mem_pool *pool,
		struct kbase_mem_pool *lp_pool);

#endif  /*_KBASE_MEM_POOL_DEBUGFS_H*/

