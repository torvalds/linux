/*
 *
 * (C) COPYRIGHT 2014-2019 ARM Limited. All rights reserved.
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
 * Base kernel property query backend APIs
 */

#include <mali_kbase.h>
#include <backend/gpu/mali_kbase_device_internal.h>
#include <backend/gpu/mali_kbase_pm_internal.h>
#include <mali_kbase_hwaccess_gpuprops.h>

void kbase_backend_gpuprops_get(struct kbase_device *kbdev,
					struct kbase_gpuprops_regdump *regdump)
{
	int i;

	/* Fill regdump with the content of the relevant registers */
	regdump->gpu_id = kbase_reg_read(kbdev, GPU_CONTROL_REG(GPU_ID));

	regdump->l2_features = kbase_reg_read(kbdev,
				GPU_CONTROL_REG(L2_FEATURES));
	regdump->core_features = kbase_reg_read(kbdev,
				GPU_CONTROL_REG(CORE_FEATURES));
	regdump->tiler_features = kbase_reg_read(kbdev,
				GPU_CONTROL_REG(TILER_FEATURES));
	regdump->mem_features = kbase_reg_read(kbdev,
				GPU_CONTROL_REG(MEM_FEATURES));
	regdump->mmu_features = kbase_reg_read(kbdev,
				GPU_CONTROL_REG(MMU_FEATURES));
	regdump->as_present = kbase_reg_read(kbdev,
				GPU_CONTROL_REG(AS_PRESENT));
	regdump->js_present = kbase_reg_read(kbdev,
				GPU_CONTROL_REG(JS_PRESENT));

	for (i = 0; i < GPU_MAX_JOB_SLOTS; i++)
		regdump->js_features[i] = kbase_reg_read(kbdev,
				GPU_CONTROL_REG(JS_FEATURES_REG(i)));

	for (i = 0; i < BASE_GPU_NUM_TEXTURE_FEATURES_REGISTERS; i++)
		regdump->texture_features[i] = kbase_reg_read(kbdev,
				GPU_CONTROL_REG(TEXTURE_FEATURES_REG(i)));

	regdump->thread_max_threads = kbase_reg_read(kbdev,
				GPU_CONTROL_REG(THREAD_MAX_THREADS));
	regdump->thread_max_workgroup_size = kbase_reg_read(kbdev,
				GPU_CONTROL_REG(THREAD_MAX_WORKGROUP_SIZE));
	regdump->thread_max_barrier_size = kbase_reg_read(kbdev,
				GPU_CONTROL_REG(THREAD_MAX_BARRIER_SIZE));
	regdump->thread_features = kbase_reg_read(kbdev,
				GPU_CONTROL_REG(THREAD_FEATURES));
	regdump->thread_tls_alloc = kbase_reg_read(kbdev,
				GPU_CONTROL_REG(THREAD_TLS_ALLOC));

	regdump->shader_present_lo = kbase_reg_read(kbdev,
				GPU_CONTROL_REG(SHADER_PRESENT_LO));
	regdump->shader_present_hi = kbase_reg_read(kbdev,
				GPU_CONTROL_REG(SHADER_PRESENT_HI));

	regdump->tiler_present_lo = kbase_reg_read(kbdev,
				GPU_CONTROL_REG(TILER_PRESENT_LO));
	regdump->tiler_present_hi = kbase_reg_read(kbdev,
				GPU_CONTROL_REG(TILER_PRESENT_HI));

	regdump->l2_present_lo = kbase_reg_read(kbdev,
				GPU_CONTROL_REG(L2_PRESENT_LO));
	regdump->l2_present_hi = kbase_reg_read(kbdev,
				GPU_CONTROL_REG(L2_PRESENT_HI));

	regdump->stack_present_lo = kbase_reg_read(kbdev,
				GPU_CONTROL_REG(STACK_PRESENT_LO));
	regdump->stack_present_hi = kbase_reg_read(kbdev,
				GPU_CONTROL_REG(STACK_PRESENT_HI));
}

void kbase_backend_gpuprops_get_features(struct kbase_device *kbdev,
					struct kbase_gpuprops_regdump *regdump)
{
	if (kbase_hw_has_feature(kbdev, BASE_HW_FEATURE_COHERENCY_REG)) {
		/* Ensure we can access the GPU registers */
		kbase_pm_register_access_enable(kbdev);

		regdump->coherency_features = kbase_reg_read(kbdev,
				GPU_CONTROL_REG(COHERENCY_FEATURES));

		/* We're done accessing the GPU registers for now. */
		kbase_pm_register_access_disable(kbdev);
	} else {
		/* Pre COHERENCY_FEATURES we only supported ACE_LITE */
		regdump->coherency_features =
				COHERENCY_FEATURE_BIT(COHERENCY_NONE) |
				COHERENCY_FEATURE_BIT(COHERENCY_ACE_LITE);
	}
}

void kbase_backend_gpuprops_get_l2_features(struct kbase_device *kbdev,
					struct kbase_gpuprops_regdump *regdump)
{
	if (kbase_hw_has_feature(kbdev, BASE_HW_FEATURE_L2_CONFIG)) {
		regdump->l2_features = kbase_reg_read(kbdev,
				GPU_CONTROL_REG(L2_FEATURES));
	}
}

