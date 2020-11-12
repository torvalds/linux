/*
 *
 * (C) COPYRIGHT 2010-2018, 2020 ARM Limited. All rights reserved.
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

#ifndef _KBASE_FENCE_DEFS_H_
#define _KBASE_FENCE_DEFS_H_

/*
 * There was a big rename in the 4.10 kernel (fence* -> dma_fence*)
 * This file hides the compatibility issues with this for the rest the driver
 */

#include <linux/version.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0))

#include <linux/fence.h>

#define dma_fence_context_alloc(a) fence_context_alloc(a)
#define dma_fence_init(a, b, c, d, e) fence_init(a, b, c, d, e)
#define dma_fence_get(a) fence_get(a)
#define dma_fence_put(a) fence_put(a)
#define dma_fence_signal(a) fence_signal(a)
#define dma_fence_is_signaled(a) fence_is_signaled(a)
#define dma_fence_add_callback(a, b, c) fence_add_callback(a, b, c)
#define dma_fence_remove_callback(a, b) fence_remove_callback(a, b)

#if (KERNEL_VERSION(4, 9, 68) <= LINUX_VERSION_CODE)
#define dma_fence_get_status(a) (fence_is_signaled(a) ? (a)->error ?: 1 : 0)
#else
#define dma_fence_get_status(a) (fence_is_signaled(a) ? (a)->status ?: 1 : 0)
#endif

#else

#include <linux/dma-fence.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 11, 0))
#define dma_fence_get_status(a) (dma_fence_is_signaled(a) ? \
	(a)->status ?: 1 \
	: 0)
#endif

#endif /* < 4.10.0 */

#endif /* _KBASE_FENCE_DEFS_H_ */
