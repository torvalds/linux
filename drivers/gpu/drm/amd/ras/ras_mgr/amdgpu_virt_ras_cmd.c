// SPDX-License-Identifier: MIT
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
 *
 */

#include <linux/pci.h>
#include "amdgpu.h"
#include "amdgpu_ras.h"
#include "ras_sys.h"
#include "amdgpu_ras_cmd.h"
#include "amdgpu_virt_ras_cmd.h"
#include "amdgpu_ras_mgr.h"

static int amdgpu_virt_ras_remote_ioctl_cmd(struct ras_core_context *ras_core,
			struct ras_cmd_ctx *cmd, void *output_data, uint32_t output_size)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)ras_core->dev;
	uint32_t mem_len = ALIGN(sizeof(*cmd) + output_size, AMDGPU_GPU_PAGE_SIZE);
	struct ras_cmd_ctx *rcmd;
	struct amdgpu_bo *rcmd_bo = NULL;
	uint64_t mc_addr = 0;
	void *cpu_addr = NULL;
	int ret = 0;

	ret = amdgpu_bo_create_kernel(adev, mem_len, PAGE_SIZE,
			AMDGPU_GEM_DOMAIN_VRAM, &rcmd_bo, &mc_addr, (void **)&cpu_addr);
	if (ret)
		return ret;

	rcmd = (struct ras_cmd_ctx *)cpu_addr;
	memset(rcmd, 0, mem_len);
	memcpy(rcmd, cmd, sizeof(*cmd));

	ret = amdgpu_virt_send_remote_ras_cmd(ras_core->dev,
				mc_addr - adev->gmc.vram_start, mem_len);
	if (!ret) {
		if (rcmd->cmd_res) {
			ret = rcmd->cmd_res;
			goto out;
		}

		cmd->cmd_res = rcmd->cmd_res;
		cmd->output_size = rcmd->output_size;
		if (rcmd->output_size && (rcmd->output_size <= output_size) && output_data)
			memcpy(output_data, rcmd->output_buff_raw, rcmd->output_size);
	}

out:
	amdgpu_bo_free_kernel(&rcmd_bo, &mc_addr, &cpu_addr);

	return ret;
}

static int amdgpu_virt_ras_send_remote_cmd(struct ras_core_context *ras_core,
	uint32_t cmd_id, void *input_data, uint32_t input_size,
	void *output_data, uint32_t output_size)
{
	struct ras_cmd_ctx rcmd = {0};
	int ret;

	rcmd.cmd_id = cmd_id;
	rcmd.input_size = input_size;
	memcpy(rcmd.input_buff_raw, input_data, input_size);

	ret = amdgpu_virt_ras_remote_ioctl_cmd(ras_core,
				&rcmd, output_data, output_size);
	if (!ret) {
		if (rcmd.output_size != output_size)
			return RAS_CMD__ERROR_GENERIC;
	}

	return ret;
}

static int amdgpu_virt_ras_get_batch_trace_overview(struct ras_core_context *ras_core,
	struct ras_log_batch_overview *overview)
{
	struct ras_cmd_batch_trace_snapshot_req req = {0};
	struct ras_cmd_batch_trace_snapshot_rsp rsp = {0};
	int ret;

	ret = amdgpu_virt_ras_send_remote_cmd(ras_core, RAS_CMD__GET_BATCH_TRACE_SNAPSHOT,
				&req, sizeof(req), &rsp, sizeof(rsp));
	if (ret)
		return ret;

	overview->first_batch_id = rsp.start_batch_id;
	overview->last_batch_id = rsp.latest_batch_id;
	overview->logged_batch_count = rsp.total_batch_num;

	return RAS_CMD__SUCCESS;
}

static int amdgpu_virt_ras_get_cper_snapshot(struct ras_core_context *ras_core,
			struct ras_cmd_ctx *cmd, void *data)
{
	struct amdgpu_ras_mgr *ras_mgr = amdgpu_ras_mgr_get_context(ras_core->dev);
	struct amdgpu_virt_ras_cmd *virt_ras =
			(struct amdgpu_virt_ras_cmd *)ras_mgr->virt_ras_cmd;
	int ret;

	if (cmd->input_size != sizeof(struct ras_cmd_cper_snapshot_req))
		return RAS_CMD__ERROR_INVALID_INPUT_SIZE;

	ret = amdgpu_virt_ras_send_remote_cmd(ras_core, cmd->cmd_id,
			cmd->input_buff_raw, cmd->input_size,
			cmd->output_buff_raw, sizeof(struct ras_cmd_cper_snapshot_rsp));
	if (ret)
		return ret;

	memset(&virt_ras->batch_mgr, 0, sizeof(virt_ras->batch_mgr));
	amdgpu_virt_ras_get_batch_trace_overview(ras_core,
					&virt_ras->batch_mgr.batch_overview);

	cmd->output_size = sizeof(struct ras_cmd_cper_snapshot_rsp);
	return RAS_CMD__SUCCESS;
}

static int amdgpu_virt_ras_get_batch_records(struct ras_core_context *ras_core, uint64_t batch_id,
			struct ras_log_info **trace_arr, uint32_t arr_num,
			struct ras_cmd_batch_trace_record_rsp *rsp_cache)
{
	struct ras_cmd_batch_trace_record_req req = {
		.start_batch_id = batch_id,
		.batch_num = RAS_CMD_MAX_BATCH_NUM,
	};
	struct ras_cmd_batch_trace_record_rsp *rsp = rsp_cache;
	struct batch_ras_trace_info *batch;
	int ret = 0;
	uint8_t i;

	if (!rsp->real_batch_num || (batch_id < rsp->start_batch_id) ||
		(batch_id >=  (rsp->start_batch_id + rsp->real_batch_num))) {

		memset(rsp, 0, sizeof(*rsp));
		ret = amdgpu_virt_ras_send_remote_cmd(ras_core, RAS_CMD__GET_BATCH_TRACE_RECORD,
			&req, sizeof(req), rsp, sizeof(*rsp));
		if (ret)
			return -EPIPE;
	}

	batch = &rsp->batchs[batch_id - rsp->start_batch_id];
	if (batch_id != batch->batch_id)
		return -ENODATA;

	for (i = 0; i < batch->trace_num; i++) {
		if (i >= arr_num)
			break;
		trace_arr[i] = &rsp->records[batch->offset + i];
	}

	return i;
}

static int amdgpu_virt_ras_get_cper_records(struct ras_core_context *ras_core,
	struct ras_cmd_ctx *cmd, void *data)
{
	struct amdgpu_ras_mgr *ras_mgr = amdgpu_ras_mgr_get_context(ras_core->dev);
	struct amdgpu_virt_ras_cmd *virt_ras =
			(struct amdgpu_virt_ras_cmd *)ras_mgr->virt_ras_cmd;
	struct ras_cmd_cper_record_req *req =
		(struct ras_cmd_cper_record_req *)cmd->input_buff_raw;
	struct ras_cmd_cper_record_rsp *rsp =
		(struct ras_cmd_cper_record_rsp *)cmd->output_buff_raw;
	struct ras_log_batch_overview *overview = &virt_ras->batch_mgr.batch_overview;
	struct ras_cmd_batch_trace_record_rsp *rsp_cache = &virt_ras->batch_mgr.batch_trace;
	struct ras_log_info *trace[MAX_RECORD_PER_BATCH] = {0};
	uint32_t offset = 0, real_data_len = 0;
	uint64_t batch_id;
	uint8_t *out_buf;
	int ret = 0, i, count;

	if (cmd->input_size != sizeof(struct ras_cmd_cper_record_req))
		return RAS_CMD__ERROR_INVALID_INPUT_SIZE;

	if (!req->buf_size || !req->buf_ptr || !req->cper_num)
		return RAS_CMD__ERROR_INVALID_INPUT_DATA;

	out_buf = kzalloc(req->buf_size, GFP_KERNEL);
	if (!out_buf)
		return RAS_CMD__ERROR_GENERIC;

	memset(out_buf, 0, req->buf_size);

	for (i = 0; i < req->cper_num; i++) {
		batch_id = req->cper_start_id + i;
		if (batch_id >= overview->last_batch_id)
			break;
		count = amdgpu_virt_ras_get_batch_records(ras_core, batch_id, trace,
					ARRAY_SIZE(trace), rsp_cache);
		if (count > 0) {
			ret = ras_cper_generate_cper(ras_core, trace, count,
					&out_buf[offset], req->buf_size - offset, &real_data_len);
			if (ret)
				break;

			offset += real_data_len;
		}
	}

	if ((ret && (ret != -ENOMEM)) ||
	    copy_to_user(u64_to_user_ptr(req->buf_ptr), out_buf, offset)) {
		kfree(out_buf);
		return RAS_CMD__ERROR_GENERIC;
	}

	rsp->real_data_size = offset;
	rsp->real_cper_num = i;
	rsp->remain_num = (ret == -ENOMEM) ? (req->cper_num - i) : 0;
	rsp->version = 0;

	cmd->output_size = sizeof(struct ras_cmd_cper_record_rsp);

	kfree(out_buf);

	return RAS_CMD__SUCCESS;
}

static struct ras_cmd_func_map amdgpu_virt_ras_cmd_maps[] = {
	{RAS_CMD__GET_CPER_SNAPSHOT, amdgpu_virt_ras_get_cper_snapshot},
	{RAS_CMD__GET_CPER_RECORD, amdgpu_virt_ras_get_cper_records},
};

int amdgpu_virt_ras_handle_cmd(struct ras_core_context *ras_core,
		struct ras_cmd_ctx *cmd)
{
	struct ras_cmd_func_map *ras_cmd = NULL;
	int i, res;

	for (i = 0; i < ARRAY_SIZE(amdgpu_virt_ras_cmd_maps); i++) {
		if (cmd->cmd_id == amdgpu_virt_ras_cmd_maps[i].cmd_id) {
			ras_cmd = &amdgpu_virt_ras_cmd_maps[i];
			break;
		}
	}

	if (ras_cmd)
		res = ras_cmd->func(ras_core, cmd, NULL);
	else
		res = amdgpu_virt_ras_remote_ioctl_cmd(ras_core, cmd,
					cmd->output_buff_raw, cmd->output_buf_size);

	cmd->cmd_res = res;

	if (cmd->output_size > cmd->output_buf_size) {
		RAS_DEV_ERR(ras_core->dev,
			"Output data size 0x%x exceeds buffer size 0x%x!\n",
			cmd->output_size, cmd->output_buf_size);
		return RAS_CMD__SUCCESS_EXEED_BUFFER;
	}

	return RAS_CMD__SUCCESS;
}

int amdgpu_virt_ras_sw_init(struct amdgpu_device *adev)
{
	struct amdgpu_ras_mgr *ras_mgr = amdgpu_ras_mgr_get_context(adev);

	ras_mgr->virt_ras_cmd = kzalloc(sizeof(struct amdgpu_virt_ras_cmd), GFP_KERNEL);
	if (!ras_mgr->virt_ras_cmd)
		return -ENOMEM;

	return 0;
}

int amdgpu_virt_ras_sw_fini(struct amdgpu_device *adev)
{
	struct amdgpu_ras_mgr *ras_mgr = amdgpu_ras_mgr_get_context(adev);

	kfree(ras_mgr->virt_ras_cmd);
	ras_mgr->virt_ras_cmd = NULL;

	return 0;
}

int amdgpu_virt_ras_hw_init(struct amdgpu_device *adev)
{
	return 0;
}

int amdgpu_virt_ras_hw_fini(struct amdgpu_device *adev)
{
	return 0;
}
