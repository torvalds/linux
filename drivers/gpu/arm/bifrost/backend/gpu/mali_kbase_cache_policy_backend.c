// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2014-2016, 2018, 2020-2022 ARM Limited. All rights reserved.
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

/**
 * kbasep_amba_register_present() - Check AMBA_<> register is present
 *                                  in the GPU.
 * @kbdev:    Device pointer
 *
 * Note: Only for arch version 12.x.1 onwards.
 *
 * Return: true if AMBA_FEATURES/ENABLE registers are present.
 */
static bool kbasep_amba_register_present(struct kbase_device *kbdev)
{
	return (ARCH_MAJOR_REV_REG(kbdev->gpu_props.props.raw_props.gpu_id) >=
		GPU_ID2_ARCH_MAJOR_REV_MAKE(12, 1));
}

void kbase_cache_set_coherency_mode(struct kbase_device *kbdev,
		u32 mode)
{
	kbdev->current_gpu_coherency_mode = mode;

	if (kbasep_amba_register_present(kbdev)) {
		u32 val = kbase_reg_read(kbdev, AMBA_ENABLE);

		val = AMBA_ENABLE_COHERENCY_PROTOCOL_SET(val, mode);
		kbase_reg_write(kbdev, AMBA_ENABLE, val);
	} else
		kbase_reg_write(kbdev, COHERENCY_ENABLE, mode);
}

u32 kbase_cache_get_coherency_features(struct kbase_device *kbdev)
{
	u32 coherency_features;

	if (kbasep_amba_register_present(kbdev))
		coherency_features =
			kbase_reg_read(kbdev, GPU_CONTROL_REG(AMBA_FEATURES));
	else
		coherency_features = kbase_reg_read(
			kbdev, GPU_CONTROL_REG(COHERENCY_FEATURES));

	return coherency_features;
}

void kbase_amba_set_memory_cache_support(struct kbase_device *kbdev,
					 bool enable)
{
	if (kbasep_amba_register_present(kbdev)) {
		u32 val = kbase_reg_read(kbdev, AMBA_ENABLE);

		val = AMBA_ENABLE_MEMORY_CACHE_SUPPORT_SET(val, enable);
		kbase_reg_write(kbdev, AMBA_ENABLE, val);

	} else {
		WARN(1, "memory_cache_support not supported");
	}
}

void kbase_amba_set_invalidate_hint(struct kbase_device *kbdev, bool enable)
{
	if (kbasep_amba_register_present(kbdev)) {
		u32 val = kbase_reg_read(kbdev, AMBA_ENABLE);

		val = AMBA_ENABLE_INVALIDATE_HINT_SET(val, enable);
		kbase_reg_write(kbdev, AMBA_ENABLE, val);
	} else {
		WARN(1, "invalidate_hint not supported");
	}
}
