/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2014-2016, 2020-2022 ARM Limited. All rights reserved.
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

#ifndef _KBASE_CACHE_POLICY_BACKEND_H_
#define _KBASE_CACHE_POLICY_BACKEND_H_

#include "mali_kbase.h"
#include <uapi/gpu/arm/bifrost/mali_base_kernel.h>

/**
 * kbase_cache_set_coherency_mode() - Sets the system coherency mode
 *                                    in the GPU.
 * @kbdev:    Device pointer
 * @mode:     Coherency mode. COHERENCY_ACE/ACE_LITE
 */
void kbase_cache_set_coherency_mode(struct kbase_device *kbdev,
		u32 mode);

/**
 * kbase_cache_get_coherency_features() - Get the coherency features
 *                                        in the GPU.
 * @kbdev:    Device pointer
 *
 * Return:    Register value to be returned
 */
u32 kbase_cache_get_coherency_features(struct kbase_device *kbdev);

/**
 * kbase_amba_set_memory_cache_support() - Sets AMBA memory cache support
 *                                         in the GPU.
 * @kbdev:    Device pointer
 * @enable:   true for enable.
 *
 * Note: Only for arch version 12.x.1 onwards.
 */
void kbase_amba_set_memory_cache_support(struct kbase_device *kbdev,
					 bool enable);
/**
 * kbase_amba_set_invalidate_hint() - Sets AMBA invalidate hint
 *                                    in the GPU.
 * @kbdev:    Device pointer
 * @enable:   true for enable.
 *
 * Note: Only for arch version 12.x.1 onwards.
 */
void kbase_amba_set_invalidate_hint(struct kbase_device *kbdev, bool enable);
#endif /* _KBASE_CACHE_POLICY_BACKEND_H_ */
