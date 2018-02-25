/*
 *
 * (C) COPYRIGHT 2012-2013, 2015 ARM Limited. All rights reserved.
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



/*
 * Cache Policy API.
 */

#ifndef _KBASE_CACHE_POLICY_H_
#define _KBASE_CACHE_POLICY_H_

#include "mali_kbase.h"
#include "mali_base_kernel.h"

/**
 * kbase_cache_enabled - Choose the cache policy for a specific region
 * @flags:    flags describing attributes of the region
 * @nr_pages: total number of pages (backed or not) for the region
 *
 * Tells whether the CPU and GPU caches should be enabled or not for a specific
 * region.
 * This function can be modified to customize the cache policy depending on the
 * flags and size of the region.
 *
 * Return: a combination of %KBASE_REG_CPU_CACHED and %KBASE_REG_GPU_CACHED
 *         depending on the cache policy
 */
u32 kbase_cache_enabled(u32 flags, u32 nr_pages);

#endif				/* _KBASE_CACHE_POLICY_H_ */
