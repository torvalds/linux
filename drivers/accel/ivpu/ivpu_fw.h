/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020-2025 Intel Corporation
 */

#ifndef __IVPU_FW_H__
#define __IVPU_FW_H__

#include "vpu_boot_api.h"
#include "vpu_jsm_api.h"

#define FW_VERSION_HEADER_SIZE	SZ_4K
#define FW_VERSION_STR_SIZE	SZ_256

struct ivpu_device;
struct ivpu_bo;
struct vpu_boot_params;

struct ivpu_fw_info {
	const struct firmware *file;
	const char *name;
	char version[FW_VERSION_STR_SIZE];
	struct ivpu_bo *mem_bp;
	struct ivpu_bo *mem_fw_ver;
	struct ivpu_bo *mem;
	struct ivpu_bo *mem_shave_nn;
	struct ivpu_bo *mem_log_crit;
	struct ivpu_bo *mem_log_verb;
	u64 boot_params_addr;
	u64 boot_params_size;
	u64 fw_version_addr;
	u64 fw_version_size;
	u64 runtime_addr;
	u32 runtime_size;
	u64 image_load_offset;
	u32 image_size;
	u32 shave_nn_size;
	u64 warm_boot_entry_point;
	u64 cold_boot_entry_point;
	u8 last_boot_mode;
	u8 next_boot_mode;
	u32 trace_level;
	u32 trace_destination_mask;
	u64 trace_hw_component_mask;
	u32 dvfs_mode;
	u32 primary_preempt_buf_size;
	u32 secondary_preempt_buf_size;
	u64 read_only_addr;
	u32 read_only_size;
	u32 sched_mode;
	u64 last_heartbeat;
};

bool ivpu_is_within_range(u64 addr, size_t size, struct ivpu_addr_range *range);
int ivpu_fw_init(struct ivpu_device *vdev);
void ivpu_fw_fini(struct ivpu_device *vdev);
void ivpu_fw_load(struct ivpu_device *vdev);
void ivpu_fw_boot_params_setup(struct ivpu_device *vdev, struct vpu_boot_params *boot_params);

static inline bool ivpu_fw_is_warm_boot(struct ivpu_device *vdev)
{
	return vdev->fw->next_boot_mode == VPU_BOOT_TYPE_WARMBOOT;
}

static inline u32 ivpu_fw_preempt_buf_size(struct ivpu_device *vdev)
{
	return vdev->fw->primary_preempt_buf_size + vdev->fw->secondary_preempt_buf_size;
}

#endif /* __IVPU_FW_H__ */
