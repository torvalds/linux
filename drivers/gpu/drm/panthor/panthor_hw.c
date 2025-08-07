// SPDX-License-Identifier: GPL-2.0 or MIT
/* Copyright 2025 ARM Limited. All rights reserved. */

#include "panthor_device.h"
#include "panthor_hw.h"
#include "panthor_regs.h"

/**
 * struct panthor_model - GPU model description
 */
struct panthor_model {
	/** @name: Model name. */
	const char *name;

	/** @arch_major: Major version number of architecture. */
	u8 arch_major;

	/** @product_major: Major version number of product. */
	u8 product_major;
};

/**
 * GPU_MODEL() - Define a GPU model. A GPU product can be uniquely identified
 * by a combination of the major architecture version and the major product
 * version.
 * @_name: Name for the GPU model.
 * @_arch_major: Architecture major.
 * @_product_major: Product major.
 */
#define GPU_MODEL(_name, _arch_major, _product_major) \
{\
	.name = __stringify(_name),				\
	.arch_major = _arch_major,				\
	.product_major = _product_major,			\
}

static const struct panthor_model gpu_models[] = {
	GPU_MODEL(g610, 10, 7),
	{},
};

static void panthor_gpu_info_init(struct panthor_device *ptdev)
{
	unsigned int i;

	ptdev->gpu_info.gpu_id = gpu_read(ptdev, GPU_ID);
	ptdev->gpu_info.csf_id = gpu_read(ptdev, GPU_CSF_ID);
	ptdev->gpu_info.gpu_rev = gpu_read(ptdev, GPU_REVID);
	ptdev->gpu_info.core_features = gpu_read(ptdev, GPU_CORE_FEATURES);
	ptdev->gpu_info.l2_features = gpu_read(ptdev, GPU_L2_FEATURES);
	ptdev->gpu_info.tiler_features = gpu_read(ptdev, GPU_TILER_FEATURES);
	ptdev->gpu_info.mem_features = gpu_read(ptdev, GPU_MEM_FEATURES);
	ptdev->gpu_info.mmu_features = gpu_read(ptdev, GPU_MMU_FEATURES);
	ptdev->gpu_info.thread_features = gpu_read(ptdev, GPU_THREAD_FEATURES);
	ptdev->gpu_info.max_threads = gpu_read(ptdev, GPU_THREAD_MAX_THREADS);
	ptdev->gpu_info.thread_max_workgroup_size = gpu_read(ptdev, GPU_THREAD_MAX_WORKGROUP_SIZE);
	ptdev->gpu_info.thread_max_barrier_size = gpu_read(ptdev, GPU_THREAD_MAX_BARRIER_SIZE);
	ptdev->gpu_info.coherency_features = gpu_read(ptdev, GPU_COHERENCY_FEATURES);
	for (i = 0; i < 4; i++)
		ptdev->gpu_info.texture_features[i] = gpu_read(ptdev, GPU_TEXTURE_FEATURES(i));

	ptdev->gpu_info.as_present = gpu_read(ptdev, GPU_AS_PRESENT);

	ptdev->gpu_info.shader_present = gpu_read64(ptdev, GPU_SHADER_PRESENT);
	ptdev->gpu_info.tiler_present = gpu_read64(ptdev, GPU_TILER_PRESENT);
	ptdev->gpu_info.l2_present = gpu_read64(ptdev, GPU_L2_PRESENT);
}

static void panthor_hw_info_init(struct panthor_device *ptdev)
{
	const struct panthor_model *model;
	u32 arch_major, product_major;
	u32 major, minor, status;

	panthor_gpu_info_init(ptdev);

	arch_major = GPU_ARCH_MAJOR(ptdev->gpu_info.gpu_id);
	product_major = GPU_PROD_MAJOR(ptdev->gpu_info.gpu_id);
	major = GPU_VER_MAJOR(ptdev->gpu_info.gpu_id);
	minor = GPU_VER_MINOR(ptdev->gpu_info.gpu_id);
	status = GPU_VER_STATUS(ptdev->gpu_info.gpu_id);

	for (model = gpu_models; model->name; model++) {
		if (model->arch_major == arch_major &&
		    model->product_major == product_major)
			break;
	}

	drm_info(&ptdev->base,
		 "mali-%s id 0x%x major 0x%x minor 0x%x status 0x%x",
		 model->name ?: "unknown", ptdev->gpu_info.gpu_id >> 16,
		 major, minor, status);

	drm_info(&ptdev->base,
		 "Features: L2:%#x Tiler:%#x Mem:%#x MMU:%#x AS:%#x",
		 ptdev->gpu_info.l2_features,
		 ptdev->gpu_info.tiler_features,
		 ptdev->gpu_info.mem_features,
		 ptdev->gpu_info.mmu_features,
		 ptdev->gpu_info.as_present);

	drm_info(&ptdev->base,
		 "shader_present=0x%0llx l2_present=0x%0llx tiler_present=0x%0llx",
		 ptdev->gpu_info.shader_present, ptdev->gpu_info.l2_present,
		 ptdev->gpu_info.tiler_present);
}

int panthor_hw_init(struct panthor_device *ptdev)
{
	panthor_hw_info_init(ptdev);

	return 0;
}
