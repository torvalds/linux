/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2025 Advanced Micro Devices, Inc.
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

#ifndef __AMDGPU_VIRT_RAS_CMD_H__
#define __AMDGPU_VIRT_RAS_CMD_H__
#include "ras.h"

struct remote_batch_trace_mgr {
	struct ras_log_batch_overview  batch_overview;
	struct ras_cmd_batch_trace_record_rsp  batch_trace;
};

struct vram_blocks_ecc {
	struct amdgpu_bo *bo;
	uint64_t mc_addr;
	void *cpu_addr;
	uint32_t size;
	bool auto_update_actived;
};

struct amdgpu_virt_ras_cmd {
	bool remote_uniras_supported;
	struct remote_batch_trace_mgr batch_mgr;
	struct vram_blocks_ecc blocks_ecc;
};

int amdgpu_virt_ras_sw_init(struct amdgpu_device *adev);
int amdgpu_virt_ras_sw_fini(struct amdgpu_device *adev);
int amdgpu_virt_ras_hw_init(struct amdgpu_device *adev);
int amdgpu_virt_ras_hw_fini(struct amdgpu_device *adev);
int amdgpu_virt_ras_handle_cmd(struct ras_core_context *ras_core,
		struct ras_cmd_ctx *cmd);
int amdgpu_virt_ras_pre_reset(struct amdgpu_device *adev);
int amdgpu_virt_ras_post_reset(struct amdgpu_device *adev);
void amdgpu_virt_ras_set_remote_uniras(struct amdgpu_device *adev, bool en);
bool amdgpu_virt_ras_remote_uniras_enabled(struct amdgpu_device *adev);
#endif
