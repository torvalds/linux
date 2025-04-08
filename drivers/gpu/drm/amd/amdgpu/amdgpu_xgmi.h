/*
 * Copyright 2016 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */
#ifndef __AMDGPU_XGMI_H__
#define __AMDGPU_XGMI_H__

#include <drm/task_barrier.h>
#include "amdgpu_ras.h"

enum amdgpu_xgmi_link_speed {
	XGMI_SPEED_16GT = 16,
	XGMI_SPEED_25GT = 25,
	XGMI_SPEED_32GT = 32
};

struct amdgpu_hive_info {
	struct kobject kobj;
	uint64_t hive_id;
	struct list_head device_list;
	struct list_head node;
	atomic_t number_devices;
	struct mutex hive_lock;
	int hi_req_count;
	struct amdgpu_device *hi_req_gpu;
	struct task_barrier tb;
	enum {
		AMDGPU_XGMI_PSTATE_MIN,
		AMDGPU_XGMI_PSTATE_MAX_VEGA20,
		AMDGPU_XGMI_PSTATE_UNKNOWN
	} pstate;

	struct amdgpu_reset_domain *reset_domain;
	atomic_t ras_recovery;
	struct ras_event_manager event_mgr;
	struct work_struct reset_on_init_work;
	atomic_t requested_nps_mode;
};

struct amdgpu_pcs_ras_field {
	const char *err_name;
	uint32_t pcs_err_mask;
	uint32_t pcs_err_shift;
};

/**
 * Bandwidth range reporting comes in two modes.
 *
 * PER_LINK - range for any xgmi link
 * PER_PEER - range of max of single xgmi link to max of multiple links based on source peer
 */
enum amdgpu_xgmi_bw_mode {
	AMDGPU_XGMI_BW_MODE_PER_LINK = 0,
	AMDGPU_XGMI_BW_MODE_PER_PEER
};

enum amdgpu_xgmi_bw_unit {
	AMDGPU_XGMI_BW_UNIT_GBYTES = 0,
	AMDGPU_XGMI_BW_UNIT_MBYTES
};

struct amdgpu_xgmi_ras {
	struct amdgpu_ras_block_object ras_block;
};
extern struct amdgpu_xgmi_ras xgmi_ras;

struct amdgpu_xgmi {
	/* from psp */
	u64 node_id;
	u64 hive_id;
	/* fixed per family */
	u64 node_segment_size;
	/* physical node (0-3) */
	unsigned physical_node_id;
	/* number of nodes (0-4) */
	unsigned num_physical_nodes;
	/* gpu list in the same hive */
	struct list_head head;
	bool supported;
	struct ras_common_if *ras_if;
	bool connected_to_cpu;
	struct amdgpu_xgmi_ras *ras;
	enum amdgpu_xgmi_link_speed max_speed;
	uint8_t max_width;
};

struct amdgpu_hive_info *amdgpu_get_xgmi_hive(struct amdgpu_device *adev);
void amdgpu_put_xgmi_hive(struct amdgpu_hive_info *hive);
int amdgpu_xgmi_update_topology(struct amdgpu_hive_info *hive, struct amdgpu_device *adev);
int amdgpu_xgmi_add_device(struct amdgpu_device *adev);
int amdgpu_xgmi_remove_device(struct amdgpu_device *adev);
int amdgpu_xgmi_set_pstate(struct amdgpu_device *adev, int pstate);
int amdgpu_xgmi_get_hops_count(struct amdgpu_device *adev, struct amdgpu_device *peer_adev);
int amdgpu_xgmi_get_bandwidth(struct amdgpu_device *adev, struct amdgpu_device *peer_adev,
			      enum amdgpu_xgmi_bw_mode bw_mode, enum amdgpu_xgmi_bw_unit bw_unit,
			      uint32_t *min_bw, uint32_t *max_bw);
bool amdgpu_xgmi_get_is_sharing_enabled(struct amdgpu_device *adev,
					struct amdgpu_device *peer_adev);
uint64_t amdgpu_xgmi_get_relative_phy_addr(struct amdgpu_device *adev,
					   uint64_t addr);
bool amdgpu_xgmi_same_hive(struct amdgpu_device *adev,
			   struct amdgpu_device *bo_adev);
int amdgpu_xgmi_ras_sw_init(struct amdgpu_device *adev);
int amdgpu_xgmi_reset_on_init(struct amdgpu_device *adev);

int amdgpu_xgmi_request_nps_change(struct amdgpu_device *adev,
				   struct amdgpu_hive_info *hive,
				   int req_nps_mode);
int amdgpu_get_xgmi_link_status(struct amdgpu_device *adev,
				int global_link_num);

void amdgpu_xgmi_early_init(struct amdgpu_device *adev);
uint32_t amdgpu_xgmi_get_max_bandwidth(struct amdgpu_device *adev);

#endif
