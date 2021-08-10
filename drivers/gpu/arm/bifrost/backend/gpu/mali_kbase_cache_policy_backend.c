// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2014-2016, 2018, 2020-2021 ARM Limited. All rights reserved.
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

#include "backend/gpu/mali_kbase_cache_policy_backend.h"
#include <device/mali_kbase_device.h>

void kbase_cache_set_coherency_mode(struct kbase_device *kbdev,
		u32 mode)
{
	kbdev->current_gpu_coherency_mode = mode;

	if (kbase_hw_has_feature(kbdev, BASE_HW_FEATURE_COHERENCY_REG))
		kbase_reg_write(kbdev, COHERENCY_ENABLE, mode);
}

