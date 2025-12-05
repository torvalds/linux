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
#include "ras_sys.h"
#include "amdgpu_ras_mgr.h"
#include "amdgpu_ras.h"
#include "amdgpu_reset.h"

static int amdgpu_ras_sys_detect_fatal_event(struct ras_core_context *ras_core, void *data)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)ras_core->dev;
	int ret;
	uint64_t seq_no;

	ret = amdgpu_ras_global_ras_isr(adev);
	if (ret)
		return ret;

	seq_no = amdgpu_ras_mgr_gen_ras_event_seqno(adev, RAS_SEQNO_TYPE_UE);
	RAS_DEV_INFO(adev,
		"{%llu} Uncorrectable hardware error(ERREVENT_ATHUB_INTERRUPT) detected!\n",
		seq_no);

	return amdgpu_ras_process_handle_unexpected_interrupt(adev, data);
}

static int amdgpu_ras_sys_poison_consumption_event(struct ras_core_context *ras_core,
				void *data)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)ras_core->dev;
	struct ras_event_req *req = (struct ras_event_req *)data;
	pasid_notify pasid_fn;

	if (!req)
		return -EINVAL;

	if (req->pasid_fn) {
		pasid_fn = (pasid_notify)req->pasid_fn;
		pasid_fn(adev, req->pasid, req->data);
	}

	return 0;
}

static int amdgpu_ras_sys_gen_seqno(struct ras_core_context *ras_core,
			enum ras_seqno_type seqno_type, uint64_t *seqno)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)ras_core->dev;
	struct amdgpu_ras_mgr *ras_mgr = amdgpu_ras_mgr_get_context(adev);
	struct ras_event_manager *event_mgr;
	struct ras_event_state *event_state;
	struct amdgpu_hive_info *hive;
	enum ras_event_type event_type;
	uint64_t seq_no;

	if (!ras_mgr || !seqno ||
		(seqno_type >= RAS_SEQNO_TYPE_COUNT_MAX))
		return -EINVAL;

	switch (seqno_type) {
	case RAS_SEQNO_TYPE_UE:
		event_type = RAS_EVENT_TYPE_FATAL;
		break;
	case RAS_SEQNO_TYPE_CE:
	case RAS_SEQNO_TYPE_DE:
		event_type = RAS_EVENT_TYPE_POISON_CREATION;
		break;
	case RAS_SEQNO_TYPE_POISON_CONSUMPTION:
		event_type = RAS_EVENT_TYPE_POISON_CONSUMPTION;
		break;
	default:
		event_type = RAS_EVENT_TYPE_INVALID;
		break;
	}

	hive = amdgpu_get_xgmi_hive(adev);
	event_mgr = hive ? &hive->event_mgr : &ras_mgr->ras_event_mgr;
	event_state = &event_mgr->event_state[event_type];
	if ((event_type == RAS_EVENT_TYPE_FATAL) && amdgpu_ras_in_recovery(adev)) {
		seq_no = event_state->last_seqno;
	} else {
		seq_no = atomic64_inc_return(&event_mgr->seqno);
		event_state->last_seqno = seq_no;
		atomic64_inc(&event_state->count);
	}
	amdgpu_put_xgmi_hive(hive);

	*seqno = seq_no;
	return 0;

}

static int amdgpu_ras_sys_event_notifier(struct ras_core_context *ras_core,
				   enum ras_notify_event event_id, void *data)
{
	struct amdgpu_ras_mgr *ras_mgr = amdgpu_ras_mgr_get_context(ras_core->dev);
	int ret = 0;

	switch (event_id) {
	case RAS_EVENT_ID__BAD_PAGE_DETECTED:
		schedule_delayed_work(&ras_mgr->retire_page_dwork, 0);
		break;
	case RAS_EVENT_ID__POISON_CONSUMPTION:
		amdgpu_ras_sys_poison_consumption_event(ras_core, data);
		break;
	case RAS_EVENT_ID__RESERVE_BAD_PAGE:
		ret = amdgpu_ras_reserve_page(ras_core->dev, *(uint64_t *)data);
		break;
	case RAS_EVENT_ID__FATAL_ERROR_DETECTED:
		ret = amdgpu_ras_sys_detect_fatal_event(ras_core, data);
		break;
	case RAS_EVENT_ID__UPDATE_BAD_PAGE_NUM:
		ret = amdgpu_dpm_send_hbm_bad_pages_num(ras_core->dev, *(uint32_t *)data);
		break;
	case RAS_EVENT_ID__UPDATE_BAD_CHANNEL_BITMAP:
		ret = amdgpu_dpm_send_hbm_bad_channel_flag(ras_core->dev, *(uint32_t *)data);
		break;
	case RAS_EVENT_ID__DEVICE_RMA:
		ras_log_ring_add_log_event(ras_core, RAS_LOG_EVENT_RMA, NULL, NULL);
		ret = amdgpu_dpm_send_rma_reason(ras_core->dev);
		break;
	case RAS_EVENT_ID__RESET_GPU:
		ret = amdgpu_ras_mgr_reset_gpu(ras_core->dev, *(uint32_t *)data);
		break;
	case RAS_EVENT_ID__RAS_EVENT_PROC_BEGIN:
		ret = amdgpu_ras_process_begin(ras_core->dev);
		break;
	case RAS_EVENT_ID__RAS_EVENT_PROC_END:
		ret = amdgpu_ras_process_end(ras_core->dev);
		break;
	default:
		RAS_DEV_WARN(ras_core->dev, "Invalid ras notify event:%d\n", event_id);
		break;
	}

	return ret;
}

static u64 amdgpu_ras_sys_get_utc_second_timestamp(struct ras_core_context *ras_core)
{
	return ktime_get_real_seconds();
}

static int amdgpu_ras_sys_check_gpu_status(struct ras_core_context *ras_core,
				uint32_t *status)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)ras_core->dev;
	uint32_t gpu_status = 0;

	if (amdgpu_in_reset(adev) || amdgpu_ras_in_recovery(adev))
		gpu_status |= RAS_GPU_STATUS__IN_RESET;

	if (amdgpu_sriov_vf(adev))
		gpu_status |= RAS_GPU_STATUS__IS_VF;

	*status = gpu_status;

	return 0;
}

static int amdgpu_ras_sys_get_device_system_info(struct ras_core_context *ras_core,
			struct device_system_info *dev_info)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)ras_core->dev;

	dev_info->device_id = adev->pdev->device;
	dev_info->vendor_id = adev->pdev->vendor;
	dev_info->socket_id = adev->smuio.funcs->get_socket_id(adev);

	return 0;
}

static int amdgpu_ras_sys_gpu_reset_lock(struct ras_core_context *ras_core,
			bool down, bool try)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)ras_core->dev;
	int ret = 0;

	if (down && try)
		ret = down_read_trylock(&adev->reset_domain->sem);
	else if (down)
		down_read(&adev->reset_domain->sem);
	else
		up_read(&adev->reset_domain->sem);

	return ret;
}

static bool amdgpu_ras_sys_detect_ras_interrupt(struct ras_core_context *ras_core)
{
	return !!atomic_read(&amdgpu_ras_in_intr);
}

static int amdgpu_ras_sys_get_gpu_mem(struct ras_core_context *ras_core,
	enum gpu_mem_type mem_type, struct gpu_mem_block *gpu_mem)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)ras_core->dev;
	struct psp_context *psp = &adev->psp;
	struct psp_ring *psp_ring;
	struct ta_mem_context *mem_ctx;

	if (mem_type == GPU_MEM_TYPE_RAS_PSP_RING) {
		psp_ring = &psp->km_ring;
		gpu_mem->mem_bo = adev->firmware.rbuf;
		gpu_mem->mem_size = psp_ring->ring_size;
		gpu_mem->mem_mc_addr = psp_ring->ring_mem_mc_addr;
		gpu_mem->mem_cpu_addr = psp_ring->ring_mem;
	} else if (mem_type == GPU_MEM_TYPE_RAS_PSP_CMD) {
		gpu_mem->mem_bo = psp->cmd_buf_bo;
		gpu_mem->mem_size = PSP_CMD_BUFFER_SIZE;
		gpu_mem->mem_mc_addr = psp->cmd_buf_mc_addr;
		gpu_mem->mem_cpu_addr = psp->cmd_buf_mem;
	} else if (mem_type == GPU_MEM_TYPE_RAS_PSP_FENCE) {
		gpu_mem->mem_bo = psp->fence_buf_bo;
		gpu_mem->mem_size = PSP_FENCE_BUFFER_SIZE;
		gpu_mem->mem_mc_addr = psp->fence_buf_mc_addr;
		gpu_mem->mem_cpu_addr = psp->fence_buf;
	} else if (mem_type == GPU_MEM_TYPE_RAS_TA_FW) {
		gpu_mem->mem_bo = psp->fw_pri_bo;
		gpu_mem->mem_size = PSP_1_MEG;
		gpu_mem->mem_mc_addr = psp->fw_pri_mc_addr;
		gpu_mem->mem_cpu_addr = psp->fw_pri_buf;
	} else if (mem_type == GPU_MEM_TYPE_RAS_TA_CMD) {
		mem_ctx = &psp->ras_context.context.mem_context;
		gpu_mem->mem_bo = mem_ctx->shared_bo;
		gpu_mem->mem_size = mem_ctx->shared_mem_size;
		gpu_mem->mem_mc_addr = mem_ctx->shared_mc_addr;
		gpu_mem->mem_cpu_addr = mem_ctx->shared_buf;
	} else {
		return -EINVAL;
	}

	if (!gpu_mem->mem_bo || !gpu_mem->mem_size ||
		!gpu_mem->mem_mc_addr || !gpu_mem->mem_cpu_addr) {
		RAS_DEV_ERR(ras_core->dev, "The ras psp gpu memory is invalid!\n");
		return -ENOMEM;
	}

	return 0;
}

static int amdgpu_ras_sys_put_gpu_mem(struct ras_core_context *ras_core,
	enum gpu_mem_type mem_type, struct gpu_mem_block *gpu_mem)
{

	return 0;
}

const struct ras_sys_func amdgpu_ras_sys_fn = {
	.ras_notifier = amdgpu_ras_sys_event_notifier,
	.get_utc_second_timestamp = amdgpu_ras_sys_get_utc_second_timestamp,
	.gen_seqno = amdgpu_ras_sys_gen_seqno,
	.check_gpu_status = amdgpu_ras_sys_check_gpu_status,
	.get_device_system_info = amdgpu_ras_sys_get_device_system_info,
	.gpu_reset_lock = amdgpu_ras_sys_gpu_reset_lock,
	.detect_ras_interrupt = amdgpu_ras_sys_detect_ras_interrupt,
	.get_gpu_mem = amdgpu_ras_sys_get_gpu_mem,
	.put_gpu_mem = amdgpu_ras_sys_put_gpu_mem,
};
