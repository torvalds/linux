// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2014-2021 ARM Limited. All rights reserved.
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

/*
 * Base kernel property query backend APIs
 */

#include <mali_kbase.h>
#include <device/mali_kbase_device.h>
#include <backend/gpu/mali_kbase_pm_internal.h>
#include <mali_kbase_hwaccess_gpuprops.h>

int kbase_backend_gpuprops_get(struct kbase_device *kbdev,
					struct kbase_gpuprops_regdump *regdump)
{
	int i;
	struct kbase_gpuprops_regdump registers;

	/* Fill regdump with the content of the relevant registers */
	registers.gpu_id = kbase_reg_read(kbdev, GPU_CONTROL_REG(GPU_ID));

	registers.l2_features = kbase_reg_read(kbdev,
				GPU_CONTROL_REG(L2_FEATURES));
	registers.core_features = 0;
#if !MALI_USE_CSF
	/* TGOx */
	registers.core_features = kbase_reg_read(kbdev,
				GPU_CONTROL_REG(CORE_FEATURES));
#else /* !MALI_USE_CSF */
	if (((registers.gpu_id & GPU_ID2_PRODUCT_MODEL) ==
	     GPU_ID2_PRODUCT_TGRX) ||
	    ((registers.gpu_id & GPU_ID2_PRODUCT_MODEL) ==
	     GPU_ID2_PRODUCT_TVAX))
		registers.core_features =
			kbase_reg_read(kbdev, GPU_CONTROL_REG(CORE_FEATURES));
#endif /* MALI_USE_CSF */
	registers.tiler_features = kbase_reg_read(kbdev,
				GPU_CONTROL_REG(TILER_FEATURES));
	registers.mem_features = kbase_reg_read(kbdev,
				GPU_CONTROL_REG(MEM_FEATURES));
	registers.mmu_features = kbase_reg_read(kbdev,
				GPU_CONTROL_REG(MMU_FEATURES));
	registers.as_present = kbase_reg_read(kbdev,
				GPU_CONTROL_REG(AS_PRESENT));
#if !MALI_USE_CSF
	registers.js_present = kbase_reg_read(kbdev,
				GPU_CONTROL_REG(JS_PRESENT));
#else /* !MALI_USE_CSF */
	registers.js_present = 0;
#endif /* !MALI_USE_CSF */

	for (i = 0; i < GPU_MAX_JOB_SLOTS; i++)
#if !MALI_USE_CSF
		registers.js_features[i] = kbase_reg_read(kbdev,
				GPU_CONTROL_REG(JS_FEATURES_REG(i)));
#else /* !MALI_USE_CSF */
		registers.js_features[i] = 0;
#endif /* !MALI_USE_CSF */

	for (i = 0; i < BASE_GPU_NUM_TEXTURE_FEATURES_REGISTERS; i++)
		registers.texture_features[i] = kbase_reg_read(kbdev,
				GPU_CONTROL_REG(TEXTURE_FEATURES_REG(i)));

	registers.thread_max_threads = kbase_reg_read(kbdev,
				GPU_CONTROL_REG(THREAD_MAX_THREADS));
	registers.thread_max_workgroup_size = kbase_reg_read(kbdev,
				GPU_CONTROL_REG(THREAD_MAX_WORKGROUP_SIZE));
	registers.thread_max_barrier_size = kbase_reg_read(kbdev,
				GPU_CONTROL_REG(THREAD_MAX_BARRIER_SIZE));
	registers.thread_features = kbase_reg_read(kbdev,
				GPU_CONTROL_REG(THREAD_FEATURES));
	registers.thread_tls_alloc = kbase_reg_read(kbdev,
				GPU_CONTROL_REG(THREAD_TLS_ALLOC));

	registers.shader_present_lo = kbase_reg_read(kbdev,
				GPU_CONTROL_REG(SHADER_PRESENT_LO));
	registers.shader_present_hi = kbase_reg_read(kbdev,
				GPU_CONTROL_REG(SHADER_PRESENT_HI));

	registers.tiler_present_lo = kbase_reg_read(kbdev,
				GPU_CONTROL_REG(TILER_PRESENT_LO));
	registers.tiler_present_hi = kbase_reg_read(kbdev,
				GPU_CONTROL_REG(TILER_PRESENT_HI));

	registers.l2_present_lo = kbase_reg_read(kbdev,
				GPU_CONTROL_REG(L2_PRESENT_LO));
	registers.l2_present_hi = kbase_reg_read(kbdev,
				GPU_CONTROL_REG(L2_PRESENT_HI));

	registers.stack_present_lo = kbase_reg_read(kbdev,
				GPU_CONTROL_REG(STACK_PRESENT_LO));
	registers.stack_present_hi = kbase_reg_read(kbdev,
				GPU_CONTROL_REG(STACK_PRESENT_HI));

	if (registers.gpu_id >= GPU_ID2_PRODUCT_MAKE(11, 8, 5, 2)) {
		registers.gpu_features_lo = kbase_reg_read(kbdev,
					GPU_CONTROL_REG(GPU_FEATURES_LO));
		registers.gpu_features_hi = kbase_reg_read(kbdev,
					GPU_CONTROL_REG(GPU_FEATURES_HI));
	} else {
		registers.gpu_features_lo = 0;
		registers.gpu_features_hi = 0;
	}

	if (!kbase_is_gpu_removed(kbdev)) {
		*regdump = registers;
		return 0;
	} else
		return -EIO;
}

int kbase_backend_gpuprops_get_curr_config(struct kbase_device *kbdev,
		struct kbase_current_config_regdump *curr_config_regdump)
{
	if (WARN_ON(!kbdev) || WARN_ON(!curr_config_regdump))
		return -EINVAL;

	curr_config_regdump->mem_features = kbase_reg_read(kbdev,
					GPU_CONTROL_REG(MEM_FEATURES));

	curr_config_regdump->shader_present_lo = kbase_reg_read(kbdev,
					GPU_CONTROL_REG(SHADER_PRESENT_LO));
	curr_config_regdump->shader_present_hi = kbase_reg_read(kbdev,
					GPU_CONTROL_REG(SHADER_PRESENT_HI));

	curr_config_regdump->l2_present_lo = kbase_reg_read(kbdev,
					GPU_CONTROL_REG(L2_PRESENT_LO));
	curr_config_regdump->l2_present_hi = kbase_reg_read(kbdev,
					GPU_CONTROL_REG(L2_PRESENT_HI));

	if (WARN_ON(kbase_is_gpu_removed(kbdev)))
		return -EIO;

	return 0;

}

int kbase_backend_gpuprops_get_features(struct kbase_device *kbdev,
					struct kbase_gpuprops_regdump *regdump)
{
	if (kbase_hw_has_feature(kbdev, BASE_HW_FEATURE_COHERENCY_REG)) {
		u32 coherency_features;

		/* Ensure we can access the GPU registers */
		kbase_pm_register_access_enable(kbdev);

		coherency_features = kbase_reg_read(kbdev,
				GPU_CONTROL_REG(COHERENCY_FEATURES));

		if (kbase_is_gpu_removed(kbdev))
			return -EIO;

		regdump->coherency_features = coherency_features;

		/* We're done accessing the GPU registers for now. */
		kbase_pm_register_access_disable(kbdev);
	} else {
		/* Pre COHERENCY_FEATURES we only supported ACE_LITE */
		regdump->coherency_features =
				COHERENCY_FEATURE_BIT(COHERENCY_NONE) |
				COHERENCY_FEATURE_BIT(COHERENCY_ACE_LITE);
	}

	return 0;
}

int kbase_backend_gpuprops_get_l2_features(struct kbase_device *kbdev,
					struct kbase_gpuprops_regdump *regdump)
{
	if (kbase_hw_has_feature(kbdev, BASE_HW_FEATURE_L2_CONFIG)) {
		u32 l2_features = kbase_reg_read(kbdev,
				GPU_CONTROL_REG(L2_FEATURES));
		u32 l2_config =
			kbase_reg_read(kbdev, GPU_CONTROL_REG(L2_CONFIG));


		if (kbase_is_gpu_removed(kbdev))
			return -EIO;

		regdump->l2_features = l2_features;
		regdump->l2_config = l2_config;
	}

	return 0;
}
