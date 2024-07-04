/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020-2023 Intel Corporation
 */

#ifndef __IVPU_FW_H__
#define __IVPU_FW_H__

struct ivpu_device;
struct ivpu_bo;
struct vpu_boot_params;

struct ivpu_fw_info {
	const struct firmware *file;
	const char *name;
	struct ivpu_bo *mem;
	struct ivpu_bo *mem_shave_nn;
	struct ivpu_bo *mem_log_crit;
	struct ivpu_bo *mem_log_verb;
	u64 runtime_addr;
	u32 runtime_size;
	u64 image_load_offset;
	u32 image_size;
	u32 shave_nn_size;
	u64 entry_point; /* Cold or warm boot entry point for next boot */
	u64 cold_boot_entry_point;
	u32 trace_level;
	u32 trace_destination_mask;
	u64 trace_hw_component_mask;
	u32 dvfs_mode;
	u32 primary_preempt_buf_size;
	u32 secondary_preempt_buf_size;
	u64 read_only_addr;
	u32 read_only_size;
};

int ivpu_fw_init(struct ivpu_device *vdev);
void ivpu_fw_fini(struct ivpu_device *vdev);
void ivpu_fw_load(struct ivpu_device *vdev);
void ivpu_fw_boot_params_setup(struct ivpu_device *vdev, struct vpu_boot_params *bp);

static inline bool ivpu_fw_is_cold_boot(struct ivpu_device *vdev)
{
	return vdev->fw->entry_point == vdev->fw->cold_boot_entry_point;
}

#endif /* __IVPU_FW_H__ */
