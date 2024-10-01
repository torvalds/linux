// SPDX-License-Identifier: MIT
/*
 * Copyright 2023 Advanced Micro Devices, Inc.
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

#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/debugfs.h>
#include "amdgpu.h"
#include "soc15_common.h"
#include "soc21.h"
#include "vcn/vcn_4_0_0_offset.h"
#include "vcn/vcn_4_0_0_sh_mask.h"

#include "amdgpu_umsch_mm.h"
#include "umsch_mm_4_0_api_def.h"
#include "umsch_mm_v4_0.h"

#define regUVD_IPX_DLDO_CONFIG                             0x0064
#define regUVD_IPX_DLDO_CONFIG_BASE_IDX                    1
#define regUVD_IPX_DLDO_STATUS                             0x0065
#define regUVD_IPX_DLDO_STATUS_BASE_IDX                    1

#define UVD_IPX_DLDO_CONFIG__ONO0_PWR_CONFIG__SHIFT        0x00000002
#define UVD_IPX_DLDO_CONFIG__ONO0_PWR_CONFIG_MASK          0x0000000cUL
#define UVD_IPX_DLDO_STATUS__ONO0_PWR_STATUS__SHIFT        0x00000001
#define UVD_IPX_DLDO_STATUS__ONO0_PWR_STATUS_MASK          0x00000002UL

static int umsch_mm_v4_0_load_microcode(struct amdgpu_umsch_mm *umsch)
{
	struct amdgpu_device *adev = umsch->ring.adev;
	uint64_t data;
	int r;

	r = amdgpu_umsch_mm_allocate_ucode_buffer(umsch);
	if (r)
		return r;

	r = amdgpu_umsch_mm_allocate_ucode_data_buffer(umsch);
	if (r)
		goto err_free_ucode_bo;

	umsch->cmd_buf_curr_ptr = umsch->cmd_buf_ptr;

	if (amdgpu_ip_version(adev, VCN_HWIP, 0) >= IP_VERSION(4, 0, 5)) {
		WREG32_SOC15(VCN, 0, regUVD_IPX_DLDO_CONFIG,
			1 << UVD_IPX_DLDO_CONFIG__ONO0_PWR_CONFIG__SHIFT);
		SOC15_WAIT_ON_RREG(VCN, 0, regUVD_IPX_DLDO_STATUS,
			0 << UVD_IPX_DLDO_STATUS__ONO0_PWR_STATUS__SHIFT,
			UVD_IPX_DLDO_STATUS__ONO0_PWR_STATUS_MASK);
	}

	data = RREG32_SOC15(VCN, 0, regUMSCH_MES_RESET_CTRL);
	data = REG_SET_FIELD(data, UMSCH_MES_RESET_CTRL, MES_CORE_SOFT_RESET, 0);
	WREG32_SOC15_UMSCH(regUMSCH_MES_RESET_CTRL, data);

	data = RREG32_SOC15(VCN, 0, regVCN_MES_CNTL);
	data = REG_SET_FIELD(data, VCN_MES_CNTL, MES_INVALIDATE_ICACHE, 1);
	data = REG_SET_FIELD(data, VCN_MES_CNTL, MES_PIPE0_RESET, 1);
	data = REG_SET_FIELD(data, VCN_MES_CNTL, MES_PIPE0_ACTIVE, 0);
	data = REG_SET_FIELD(data, VCN_MES_CNTL, MES_HALT, 1);
	WREG32_SOC15_UMSCH(regVCN_MES_CNTL, data);

	data = RREG32_SOC15(VCN, 0, regVCN_MES_IC_BASE_CNTL);
	data = REG_SET_FIELD(data, VCN_MES_IC_BASE_CNTL, VMID, 0);
	data = REG_SET_FIELD(data, VCN_MES_IC_BASE_CNTL, EXE_DISABLE, 0);
	data = REG_SET_FIELD(data, VCN_MES_IC_BASE_CNTL, CACHE_POLICY, 0);
	WREG32_SOC15_UMSCH(regVCN_MES_IC_BASE_CNTL, data);

	WREG32_SOC15_UMSCH(regVCN_MES_INTR_ROUTINE_START,
		lower_32_bits(adev->umsch_mm.irq_start_addr >> 2));
	WREG32_SOC15_UMSCH(regVCN_MES_INTR_ROUTINE_START_HI,
		upper_32_bits(adev->umsch_mm.irq_start_addr >> 2));

	WREG32_SOC15_UMSCH(regVCN_MES_PRGRM_CNTR_START,
		lower_32_bits(adev->umsch_mm.uc_start_addr >> 2));
	WREG32_SOC15_UMSCH(regVCN_MES_PRGRM_CNTR_START_HI,
		upper_32_bits(adev->umsch_mm.uc_start_addr >> 2));

	WREG32_SOC15_UMSCH(regVCN_MES_LOCAL_INSTR_BASE_LO, 0);
	WREG32_SOC15_UMSCH(regVCN_MES_LOCAL_INSTR_BASE_HI, 0);

	data = adev->umsch_mm.uc_start_addr + adev->umsch_mm.ucode_size - 1;
	WREG32_SOC15_UMSCH(regVCN_MES_LOCAL_INSTR_MASK_LO, lower_32_bits(data));
	WREG32_SOC15_UMSCH(regVCN_MES_LOCAL_INSTR_MASK_HI, upper_32_bits(data));

	data = adev->firmware.load_type == AMDGPU_FW_LOAD_PSP ?
	       0 : adev->umsch_mm.ucode_fw_gpu_addr;
	WREG32_SOC15_UMSCH(regVCN_MES_IC_BASE_LO, lower_32_bits(data));
	WREG32_SOC15_UMSCH(regVCN_MES_IC_BASE_HI, upper_32_bits(data));

	WREG32_SOC15_UMSCH(regVCN_MES_MIBOUND_LO, 0x1FFFFF);

	WREG32_SOC15_UMSCH(regVCN_MES_LOCAL_BASE0_LO,
		lower_32_bits(adev->umsch_mm.data_start_addr));
	WREG32_SOC15_UMSCH(regVCN_MES_LOCAL_BASE0_HI,
		upper_32_bits(adev->umsch_mm.data_start_addr));

	WREG32_SOC15_UMSCH(regVCN_MES_LOCAL_MASK0_LO,
		adev->umsch_mm.data_size - 1);
	WREG32_SOC15_UMSCH(regVCN_MES_LOCAL_MASK0_HI, 0);

	data = adev->firmware.load_type == AMDGPU_FW_LOAD_PSP ?
	       0 : adev->umsch_mm.data_fw_gpu_addr;
	WREG32_SOC15_UMSCH(regVCN_MES_DC_BASE_LO, lower_32_bits(data));
	WREG32_SOC15_UMSCH(regVCN_MES_DC_BASE_HI, upper_32_bits(data));

	WREG32_SOC15_UMSCH(regVCN_MES_MDBOUND_LO, 0x3FFFF);

	data = RREG32_SOC15(VCN, 0, regUVD_UMSCH_FORCE);
	data = REG_SET_FIELD(data, UVD_UMSCH_FORCE, IC_FORCE_GPUVM, 1);
	data = REG_SET_FIELD(data, UVD_UMSCH_FORCE, DC_FORCE_GPUVM, 1);
	WREG32_SOC15_UMSCH(regUVD_UMSCH_FORCE, data);

	data = RREG32_SOC15(VCN, 0, regVCN_MES_IC_OP_CNTL);
	data = REG_SET_FIELD(data, VCN_MES_IC_OP_CNTL, PRIME_ICACHE, 0);
	data = REG_SET_FIELD(data, VCN_MES_IC_OP_CNTL, INVALIDATE_CACHE, 1);
	WREG32_SOC15_UMSCH(regVCN_MES_IC_OP_CNTL, data);

	data = RREG32_SOC15(VCN, 0, regVCN_MES_IC_OP_CNTL);
	data = REG_SET_FIELD(data, VCN_MES_IC_OP_CNTL, PRIME_ICACHE, 1);
	WREG32_SOC15_UMSCH(regVCN_MES_IC_OP_CNTL, data);

	WREG32_SOC15_UMSCH(regVCN_MES_GP0_LO, 0);
	WREG32_SOC15_UMSCH(regVCN_MES_GP0_HI, 0);

#if defined(CONFIG_DEBUG_FS)
	WREG32_SOC15_UMSCH(regVCN_MES_GP0_LO, lower_32_bits(umsch->log_gpu_addr));
	WREG32_SOC15_UMSCH(regVCN_MES_GP0_HI, upper_32_bits(umsch->log_gpu_addr));
#endif

	WREG32_SOC15_UMSCH(regVCN_MES_GP1_LO, 0);
	WREG32_SOC15_UMSCH(regVCN_MES_GP1_HI, 0);

	data = RREG32_SOC15(VCN, 0, regVCN_MES_CNTL);
	data = REG_SET_FIELD(data, VCN_MES_CNTL, MES_INVALIDATE_ICACHE, 0);
	data = REG_SET_FIELD(data, VCN_MES_CNTL, MES_PIPE0_RESET, 0);
	data = REG_SET_FIELD(data, VCN_MES_CNTL, MES_HALT, 0);
	data = REG_SET_FIELD(data, VCN_MES_CNTL, MES_PIPE0_ACTIVE, 1);
	WREG32_SOC15_UMSCH(regVCN_MES_CNTL, data);

	if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP)
		amdgpu_umsch_mm_psp_execute_cmd_buf(umsch);

	r = SOC15_WAIT_ON_RREG(VCN, 0, regVCN_MES_MSTATUS_LO, 0xAAAAAAAA, 0xFFFFFFFF);
	if (r) {
		dev_err(adev->dev, "UMSCH FW Load: Failed, regVCN_MES_MSTATUS_LO: 0x%08x\n",
			RREG32_SOC15(VCN, 0, regVCN_MES_MSTATUS_LO));
		goto err_free_data_bo;
	}

	return 0;

err_free_data_bo:
	amdgpu_bo_free_kernel(&adev->umsch_mm.data_fw_obj,
			      &adev->umsch_mm.data_fw_gpu_addr,
			      (void **)&adev->umsch_mm.data_fw_ptr);
err_free_ucode_bo:
	amdgpu_bo_free_kernel(&adev->umsch_mm.ucode_fw_obj,
			      &adev->umsch_mm.ucode_fw_gpu_addr,
			      (void **)&adev->umsch_mm.ucode_fw_ptr);
	return r;
}

static void umsch_mm_v4_0_aggregated_doorbell_init(struct amdgpu_umsch_mm *umsch)
{
	struct amdgpu_device *adev = umsch->ring.adev;
	uint32_t data;

	data = RREG32_SOC15(VCN, 0, regVCN_AGDB_CTRL0);
	data = REG_SET_FIELD(data, VCN_AGDB_CTRL0, OFFSET,
	       umsch->agdb_index[CONTEXT_PRIORITY_LEVEL_REALTIME]);
	data = REG_SET_FIELD(data, VCN_AGDB_CTRL0, EN, 1);
	WREG32_SOC15(VCN, 0, regVCN_AGDB_CTRL0, data);

	data = RREG32_SOC15(VCN, 0, regVCN_AGDB_CTRL1);
	data = REG_SET_FIELD(data, VCN_AGDB_CTRL1, OFFSET,
	       umsch->agdb_index[CONTEXT_PRIORITY_LEVEL_FOCUS]);
	data = REG_SET_FIELD(data, VCN_AGDB_CTRL1, EN, 1);
	WREG32_SOC15(VCN, 0, regVCN_AGDB_CTRL1, data);

	data = RREG32_SOC15(VCN, 0, regVCN_AGDB_CTRL2);
	data = REG_SET_FIELD(data, VCN_AGDB_CTRL2, OFFSET,
	       umsch->agdb_index[CONTEXT_PRIORITY_LEVEL_NORMAL]);
	data = REG_SET_FIELD(data, VCN_AGDB_CTRL2, EN, 1);
	WREG32_SOC15(VCN, 0, regVCN_AGDB_CTRL2, data);

	data = RREG32_SOC15(VCN, 0, regVCN_AGDB_CTRL3);
	data = REG_SET_FIELD(data, VCN_AGDB_CTRL3, OFFSET,
	       umsch->agdb_index[CONTEXT_PRIORITY_LEVEL_IDLE]);
	data = REG_SET_FIELD(data, VCN_AGDB_CTRL3, EN, 1);
	WREG32_SOC15(VCN, 0, regVCN_AGDB_CTRL3, data);
}

static int umsch_mm_v4_0_ring_start(struct amdgpu_umsch_mm *umsch)
{
	struct amdgpu_ring *ring = &umsch->ring;
	struct amdgpu_device *adev = ring->adev;
	uint32_t data;

	data = RREG32_SOC15(VCN, 0, regVCN_UMSCH_RB_DB_CTRL);
	data = REG_SET_FIELD(data, VCN_UMSCH_RB_DB_CTRL, OFFSET, ring->doorbell_index);
	data = REG_SET_FIELD(data, VCN_UMSCH_RB_DB_CTRL, EN, 1);
	WREG32_SOC15(VCN, 0, regVCN_UMSCH_RB_DB_CTRL, data);

	adev->nbio.funcs->vcn_doorbell_range(adev, ring->use_doorbell,
		(adev->doorbell_index.vcn.vcn_ring0_1 << 1), 0);

	WREG32_SOC15(VCN, 0, regVCN_UMSCH_RB_BASE_LO, lower_32_bits(ring->gpu_addr));
	WREG32_SOC15(VCN, 0, regVCN_UMSCH_RB_BASE_HI, upper_32_bits(ring->gpu_addr));

	WREG32_SOC15(VCN, 0, regVCN_UMSCH_RB_SIZE, ring->ring_size);

	ring->wptr = 0;

	data = RREG32_SOC15(VCN, 0, regVCN_RB_ENABLE);
	data &= ~(VCN_RB_ENABLE__AUDIO_RB_EN_MASK);
	WREG32_SOC15(VCN, 0, regVCN_RB_ENABLE, data);

	umsch_mm_v4_0_aggregated_doorbell_init(umsch);

	return 0;
}

static int umsch_mm_v4_0_ring_stop(struct amdgpu_umsch_mm *umsch)
{
	struct amdgpu_ring *ring = &umsch->ring;
	struct amdgpu_device *adev = ring->adev;
	uint32_t data;

	data = RREG32_SOC15(VCN, 0, regVCN_RB_ENABLE);
	data = REG_SET_FIELD(data, VCN_RB_ENABLE, UMSCH_RB_EN, 0);
	WREG32_SOC15(VCN, 0, regVCN_RB_ENABLE, data);

	data = RREG32_SOC15(VCN, 0, regVCN_UMSCH_RB_DB_CTRL);
	data = REG_SET_FIELD(data, VCN_UMSCH_RB_DB_CTRL, EN, 0);
	WREG32_SOC15(VCN, 0, regVCN_UMSCH_RB_DB_CTRL, data);

	if (amdgpu_ip_version(adev, VCN_HWIP, 0) >= IP_VERSION(4, 0, 5)) {
		WREG32_SOC15(VCN, 0, regUVD_IPX_DLDO_CONFIG,
			2 << UVD_IPX_DLDO_CONFIG__ONO0_PWR_CONFIG__SHIFT);
		SOC15_WAIT_ON_RREG(VCN, 0, regUVD_IPX_DLDO_STATUS,
			1 << UVD_IPX_DLDO_STATUS__ONO0_PWR_STATUS__SHIFT,
			UVD_IPX_DLDO_STATUS__ONO0_PWR_STATUS_MASK);
	}

	return 0;
}

static int umsch_mm_v4_0_set_hw_resources(struct amdgpu_umsch_mm *umsch)
{
	union UMSCHAPI__SET_HW_RESOURCES set_hw_resources = {};
	struct amdgpu_device *adev = umsch->ring.adev;
	int r;

	set_hw_resources.header.type = UMSCH_API_TYPE_SCHEDULER;
	set_hw_resources.header.opcode = UMSCH_API_SET_HW_RSRC;
	set_hw_resources.header.dwsize = API_FRAME_SIZE_IN_DWORDS;

	set_hw_resources.vmid_mask_mm_vcn = umsch->vmid_mask_mm_vcn;
	set_hw_resources.vmid_mask_mm_vpe = umsch->vmid_mask_mm_vpe;
	set_hw_resources.collaboration_mask_vpe =
		adev->vpe.collaborate_mode ? 0x3 : 0x0;
	set_hw_resources.engine_mask = umsch->engine_mask;

	set_hw_resources.vcn0_hqd_mask[0] = umsch->vcn0_hqd_mask;
	set_hw_resources.vcn1_hqd_mask[0] = umsch->vcn1_hqd_mask;
	set_hw_resources.vcn_hqd_mask[0] = umsch->vcn_hqd_mask[0];
	set_hw_resources.vcn_hqd_mask[1] = umsch->vcn_hqd_mask[1];
	set_hw_resources.vpe_hqd_mask[0] = umsch->vpe_hqd_mask;

	set_hw_resources.g_sch_ctx_gpu_mc_ptr = umsch->sch_ctx_gpu_addr;

	set_hw_resources.enable_level_process_quantum_check = 1;

	memcpy(set_hw_resources.mmhub_base, adev->reg_offset[MMHUB_HWIP][0],
	       sizeof(uint32_t) * 5);
	set_hw_resources.mmhub_version =
		IP_VERSION_MAJ_MIN_REV(amdgpu_ip_version(adev, MMHUB_HWIP, 0));

	memcpy(set_hw_resources.osssys_base, adev->reg_offset[OSSSYS_HWIP][0],
	       sizeof(uint32_t) * 5);
	set_hw_resources.osssys_version =
		IP_VERSION_MAJ_MIN_REV(amdgpu_ip_version(adev, OSSSYS_HWIP, 0));

	set_hw_resources.vcn_version =
		IP_VERSION_MAJ_MIN_REV(amdgpu_ip_version(adev, VCN_HWIP, 0));
	set_hw_resources.vpe_version =
		IP_VERSION_MAJ_MIN_REV(amdgpu_ip_version(adev, VPE_HWIP, 0));

	set_hw_resources.api_status.api_completion_fence_addr = umsch->ring.fence_drv.gpu_addr;
	set_hw_resources.api_status.api_completion_fence_value = ++umsch->ring.fence_drv.sync_seq;

	r = amdgpu_umsch_mm_submit_pkt(umsch, &set_hw_resources.max_dwords_in_api,
				       API_FRAME_SIZE_IN_DWORDS);
	if (r)
		return r;

	r = amdgpu_umsch_mm_query_fence(umsch);
	if (r) {
		dev_err(adev->dev, "UMSCH SET_HW_RESOURCES: Failed\n");
		return r;
	}

	return 0;
}

static int umsch_mm_v4_0_add_queue(struct amdgpu_umsch_mm *umsch,
				   struct umsch_mm_add_queue_input *input_ptr)
{
	struct amdgpu_device *adev = umsch->ring.adev;
	union UMSCHAPI__ADD_QUEUE add_queue = {};
	int r;

	add_queue.header.type = UMSCH_API_TYPE_SCHEDULER;
	add_queue.header.opcode = UMSCH_API_ADD_QUEUE;
	add_queue.header.dwsize = API_FRAME_SIZE_IN_DWORDS;

	add_queue.process_id = input_ptr->process_id;
	add_queue.page_table_base_addr = input_ptr->page_table_base_addr;
	add_queue.process_va_start = input_ptr->process_va_start;
	add_queue.process_va_end = input_ptr->process_va_end;
	add_queue.process_quantum = input_ptr->process_quantum;
	add_queue.process_csa_addr = input_ptr->process_csa_addr;
	add_queue.context_quantum = input_ptr->context_quantum;
	add_queue.context_csa_addr = input_ptr->context_csa_addr;
	add_queue.inprocess_context_priority = input_ptr->inprocess_context_priority;
	add_queue.context_global_priority_level =
		(enum UMSCH_AMD_PRIORITY_LEVEL)input_ptr->context_global_priority_level;
	add_queue.doorbell_offset_0 = input_ptr->doorbell_offset_0;
	add_queue.doorbell_offset_1 = input_ptr->doorbell_offset_1;
	add_queue.affinity.u32All = input_ptr->affinity;
	add_queue.mqd_addr = input_ptr->mqd_addr;
	add_queue.engine_type = (enum UMSCH_ENGINE_TYPE)input_ptr->engine_type;
	add_queue.h_context = input_ptr->h_context;
	add_queue.h_queue = input_ptr->h_queue;
	add_queue.vm_context_cntl = input_ptr->vm_context_cntl;
	add_queue.is_context_suspended = input_ptr->is_context_suspended;
	add_queue.collaboration_mode = adev->vpe.collaborate_mode ? 1 : 0;

	add_queue.api_status.api_completion_fence_addr = umsch->ring.fence_drv.gpu_addr;
	add_queue.api_status.api_completion_fence_value = ++umsch->ring.fence_drv.sync_seq;

	r = amdgpu_umsch_mm_submit_pkt(umsch, &add_queue.max_dwords_in_api,
				       API_FRAME_SIZE_IN_DWORDS);
	if (r)
		return r;

	r = amdgpu_umsch_mm_query_fence(umsch);
	if (r) {
		dev_err(adev->dev, "UMSCH ADD_QUEUE: Failed\n");
		return r;
	}

	return 0;
}

static int umsch_mm_v4_0_remove_queue(struct amdgpu_umsch_mm *umsch,
				      struct umsch_mm_remove_queue_input *input_ptr)
{
	union UMSCHAPI__REMOVE_QUEUE remove_queue = {};
	struct amdgpu_device *adev = umsch->ring.adev;
	int r;

	remove_queue.header.type = UMSCH_API_TYPE_SCHEDULER;
	remove_queue.header.opcode = UMSCH_API_REMOVE_QUEUE;
	remove_queue.header.dwsize = API_FRAME_SIZE_IN_DWORDS;

	remove_queue.doorbell_offset_0 = input_ptr->doorbell_offset_0;
	remove_queue.doorbell_offset_1 = input_ptr->doorbell_offset_1;
	remove_queue.context_csa_addr = input_ptr->context_csa_addr;

	remove_queue.api_status.api_completion_fence_addr = umsch->ring.fence_drv.gpu_addr;
	remove_queue.api_status.api_completion_fence_value = ++umsch->ring.fence_drv.sync_seq;

	r = amdgpu_umsch_mm_submit_pkt(umsch, &remove_queue.max_dwords_in_api,
				       API_FRAME_SIZE_IN_DWORDS);
	if (r)
		return r;

	r = amdgpu_umsch_mm_query_fence(umsch);
	if (r) {
		dev_err(adev->dev, "UMSCH REMOVE_QUEUE: Failed\n");
		return r;
	}

	return 0;
}

static int umsch_mm_v4_0_set_regs(struct amdgpu_umsch_mm *umsch)
{
	struct amdgpu_device *adev = container_of(umsch, struct amdgpu_device, umsch_mm);

	umsch->rb_wptr = SOC15_REG_OFFSET(VCN, 0, regVCN_UMSCH_RB_WPTR);
	umsch->rb_rptr = SOC15_REG_OFFSET(VCN, 0, regVCN_UMSCH_RB_RPTR);

	return 0;
}

static const struct umsch_mm_funcs umsch_mm_v4_0_funcs = {
	.set_hw_resources = umsch_mm_v4_0_set_hw_resources,
	.add_queue = umsch_mm_v4_0_add_queue,
	.remove_queue = umsch_mm_v4_0_remove_queue,
	.set_regs = umsch_mm_v4_0_set_regs,
	.init_microcode = amdgpu_umsch_mm_init_microcode,
	.load_microcode = umsch_mm_v4_0_load_microcode,
	.ring_init = amdgpu_umsch_mm_ring_init,
	.ring_start = umsch_mm_v4_0_ring_start,
	.ring_stop = umsch_mm_v4_0_ring_stop,
};

void umsch_mm_v4_0_set_funcs(struct amdgpu_umsch_mm *umsch)
{
	umsch->funcs = &umsch_mm_v4_0_funcs;
}
