/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2014-2016, 2020-2021 ARM Limited. All rights reserved.
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
  *			in the GPU.
  * @kbdev:	Device pointer
  * @mode:	Coherency mode. COHERENCY_ACE/ACE_LITE
  */
void kbase_cache_set_coherency_mode(struct kbase_device *kbdev,
		u32 mode);

#endif				/* _KBASE_CACHE_POLICY_H_ */
