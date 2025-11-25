// SPDX-License-Identifier: GPL-2.0 or MIT
/* Copyright 2025 ARM Limited. All rights reserved. */

#include <drm/drm_print.h>

#include "panthor_device.h"
#include "panthor_gpu.h"
#include "panthor_hw.h"
#include "panthor_regs.h"

#define GPU_PROD_ID_MAKE(arch_major, prod_major) \
	(((arch_major) << 24) | (prod_major))

/** struct panthor_hw_entry - HW arch major to panthor_hw binding entry */
struct panthor_hw_entry {
	/** @arch_min: Minimum supported architecture major value (inclusive) */
	u8 arch_min;

	/** @arch_max: Maximum supported architecture major value (inclusive) */
	u8 arch_max;

	/** @hwdev: Pointer to panthor_hw structure */
	struct panthor_hw *hwdev;
};

static struct panthor_hw panthor_hw_arch_v10 = {
	.ops = {
		.soft_reset = panthor_gpu_soft_reset,
		.l2_power_off = panthor_gpu_l2_power_off,
		.l2_power_on = panthor_gpu_l2_power_on,
	},
};

static struct panthor_hw_entry panthor_hw_match[] = {
	{
		.arch_min = 10,
		.arch_max = 13,
		.hwdev = &panthor_hw_arch_v10,
	},
};

static char *get_gpu_model_name(struct panthor_device *ptdev)
{
	const u32 gpu_id = ptdev->gpu_info.gpu_id;
	const u32 product_id = GPU_PROD_ID_MAKE(GPU_ARCH_MAJOR(gpu_id),
						GPU_PROD_MAJOR(gpu_id));
	const bool ray_intersection = !!(ptdev->gpu_info.gpu_features &
					 GPU_FEATURES_RAY_INTERSECTION);
	const u8 shader_core_count = hweight64(ptdev->gpu_info.shader_present);

	switch (product_id) {
	case GPU_PROD_ID_MAKE(10, 2):
		return "Mali-G710";
	case GPU_PROD_ID_MAKE(10, 3):
		return "Mali-G510";
	case GPU_PROD_ID_MAKE(10, 4):
		return "Mali-G310";
	case GPU_PROD_ID_MAKE(10, 7):
		return "Mali-G610";
	case GPU_PROD_ID_MAKE(11, 2):
		if (shader_core_count > 10 && ray_intersection)
			return "Mali-G715-Immortalis";
		else if (shader_core_count >= 7)
			return "Mali-G715";

		fallthrough;
	case GPU_PROD_ID_MAKE(11, 3):
		return "Mali-G615";
	case GPU_PROD_ID_MAKE(12, 0):
		if (shader_core_count >= 10 && ray_intersection)
			return "Mali-G720-Immortalis";
		else if (shader_core_count >= 6)
			return "Mali-G720";

		fallthrough;
	case GPU_PROD_ID_MAKE(12, 1):
		return "Mali-G620";
	case GPU_PROD_ID_MAKE(13, 0):
		if (shader_core_count >= 10 && ray_intersection)
			return "Mali-G925-Immortalis";
		else if (shader_core_count >= 6)
			return "Mali-G725";

		fallthrough;
	case GPU_PROD_ID_MAKE(13, 1):
		return "Mali-G625";
	}

	return "(Unknown Mali GPU)";
}

static void panthor_gpu_info_init(struct panthor_device *ptdev)
{
	unsigned int i;

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

	/* Introduced in arch 11.x */
	ptdev->gpu_info.gpu_features = gpu_read64(ptdev, GPU_FEATURES);
}

static void panthor_hw_info_init(struct panthor_device *ptdev)
{
	u32 major, minor, status;

	panthor_gpu_info_init(ptdev);

	major = GPU_VER_MAJOR(ptdev->gpu_info.gpu_id);
	minor = GPU_VER_MINOR(ptdev->gpu_info.gpu_id);
	status = GPU_VER_STATUS(ptdev->gpu_info.gpu_id);

	drm_info(&ptdev->base,
		 "%s id 0x%x major 0x%x minor 0x%x status 0x%x",
		 get_gpu_model_name(ptdev), ptdev->gpu_info.gpu_id >> 16,
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

static int panthor_hw_bind_device(struct panthor_device *ptdev)
{
	struct panthor_hw *hdev = NULL;
	const u32 arch_major = GPU_ARCH_MAJOR(ptdev->gpu_info.gpu_id);
	int i = 0;

	for (i = 0; i < ARRAY_SIZE(panthor_hw_match); i++) {
		struct panthor_hw_entry *entry = &panthor_hw_match[i];

		if (arch_major >= entry->arch_min && arch_major <= entry->arch_max) {
			hdev = entry->hwdev;
			break;
		}
	}

	if (!hdev)
		return -EOPNOTSUPP;

	ptdev->hw = hdev;

	return 0;
}

static int panthor_hw_gpu_id_init(struct panthor_device *ptdev)
{
	ptdev->gpu_info.gpu_id = gpu_read(ptdev, GPU_ID);
	if (!ptdev->gpu_info.gpu_id)
		return -ENXIO;

	return 0;
}

int panthor_hw_init(struct panthor_device *ptdev)
{
	int ret = 0;

	ret = panthor_hw_gpu_id_init(ptdev);
	if (ret)
		return ret;

	ret = panthor_hw_bind_device(ptdev);
	if (ret)
		return ret;

	panthor_hw_info_init(ptdev);

	return 0;
}
