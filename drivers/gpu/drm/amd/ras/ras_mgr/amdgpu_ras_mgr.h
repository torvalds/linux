/* SPDX-License-Identifier: MIT */
/*
 * Copyright (c) 2025 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#ifndef __AMDGPU_RAS_MGR_H__
#define __AMDGPU_RAS_MGR_H__
#include "ras.h"
#include "amdgpu_ras_process.h"

enum ras_ih_type {
	RAS_IH_NONE,
	RAS_IH_FROM_BLOCK_CONTROLLER,
	RAS_IH_FROM_CONSUMER_CLIENT,
	RAS_IH_FROM_FATAL_ERROR,
};

struct ras_ih_info {
	uint32_t block;
	union {
		struct amdgpu_iv_entry iv_entry;
		struct {
			uint16_t pasid;
			uint32_t reset;
			pasid_notify pasid_fn;
			void *data;
		};
	};
};

struct amdgpu_ras_mgr {
	struct amdgpu_device *adev;
	struct ras_core_context *ras_core;
	struct delayed_work retire_page_dwork;
	struct ras_event_manager ras_event_mgr;
	uint64_t last_poison_consumption_seqno;
	bool ras_is_ready;
};

extern const struct amdgpu_ip_block_version ras_v1_0_ip_block;

struct amdgpu_ras_mgr *amdgpu_ras_mgr_get_context(
			struct amdgpu_device *adev);
int amdgpu_enable_uniras(struct amdgpu_device *adev, bool enable);
bool amdgpu_uniras_enabled(struct amdgpu_device *adev);
int amdgpu_ras_mgr_handle_fatal_interrupt(struct amdgpu_device *adev, void *data);
int amdgpu_ras_mgr_handle_controller_interrupt(struct amdgpu_device *adev, void *data);
int amdgpu_ras_mgr_handle_consumer_interrupt(struct amdgpu_device *adev, void *data);
int amdgpu_ras_mgr_update_ras_ecc(struct amdgpu_device *adev);
int amdgpu_ras_mgr_reset_gpu(struct amdgpu_device *adev, uint32_t flags);
uint64_t amdgpu_ras_mgr_gen_ras_event_seqno(struct amdgpu_device *adev,
			enum ras_seqno_type seqno_type);
bool amdgpu_ras_mgr_check_eeprom_safety_watermark(struct amdgpu_device *adev);
int amdgpu_ras_mgr_get_curr_nps_mode(struct amdgpu_device *adev, uint32_t *nps_mode);
bool amdgpu_ras_mgr_check_retired_addr(struct amdgpu_device *adev,
			uint64_t addr);
bool amdgpu_ras_mgr_is_rma(struct amdgpu_device *adev);
int amdgpu_ras_mgr_handle_ras_cmd(struct amdgpu_device *adev,
		uint32_t cmd_id, void *input, uint32_t input_size,
		void *output, uint32_t out_size);
#endif
