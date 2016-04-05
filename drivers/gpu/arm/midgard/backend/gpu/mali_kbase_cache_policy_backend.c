/*
 *
 * (C) COPYRIGHT 2015-2016 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */



#include "backend/gpu/mali_kbase_cache_policy_backend.h"
#include <backend/gpu/mali_kbase_pm_internal.h>
#include <backend/gpu/mali_kbase_device_internal.h>

void kbase_cache_set_coherency_mode(struct kbase_device *kbdev,
		u32 mode)
{
	if (kbase_hw_has_feature(kbdev, BASE_HW_FEATURE_COHERENCY_REG))
		kbase_reg_write(kbdev, COHERENCY_ENABLE, mode, NULL);
}

