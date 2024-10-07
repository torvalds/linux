/*
 * Copyright 2022 Advanced Micro Devices, Inc.
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

#include "amdgpu.h"
#include "amdgpu_gfx.h"
#include "soc15.h"
#include "soc15d.h"
#include "soc15_common.h"
#include "vega10_enum.h"

#include "v9_structs.h"

#include "ivsrcid/gfx/irqsrcs_gfx_9_0.h"

#include "gc/gc_9_4_3_offset.h"
#include "gc/gc_9_4_3_sh_mask.h"

#include "gfx_v9_4_3.h"
#include "gfx_v9_4_3_cleaner_shader.h"
#include "amdgpu_xcp.h"
#include "amdgpu_aca.h"

MODULE_FIRMWARE("amdgpu/gc_9_4_3_mec.bin");
MODULE_FIRMWARE("amdgpu/gc_9_4_4_mec.bin");
MODULE_FIRMWARE("amdgpu/gc_9_4_3_rlc.bin");
MODULE_FIRMWARE("amdgpu/gc_9_4_4_rlc.bin");
MODULE_FIRMWARE("amdgpu/gc_9_4_3_sjt_mec.bin");
MODULE_FIRMWARE("amdgpu/gc_9_4_4_sjt_mec.bin");

#define GFX9_MEC_HPD_SIZE 4096
#define RLCG_UCODE_LOADING_START_ADDRESS 0x00002000L

#define GOLDEN_GB_ADDR_CONFIG 0x2a114042
#define CP_HQD_PERSISTENT_STATE_DEFAULT 0xbe05301

#define mmSMNAID_XCD0_MCA_SMU 0x36430400	/* SMN AID XCD0 */
#define mmSMNAID_XCD1_MCA_SMU 0x38430400	/* SMN AID XCD1 */
#define mmSMNXCD_XCD0_MCA_SMU 0x40430400	/* SMN XCD XCD0 */

#define XCC_REG_RANGE_0_LOW  0x2000     /* XCC gfxdec0 lower Bound */
#define XCC_REG_RANGE_0_HIGH 0x3400     /* XCC gfxdec0 upper Bound */
#define XCC_REG_RANGE_1_LOW  0xA000     /* XCC gfxdec1 lower Bound */
#define XCC_REG_RANGE_1_HIGH 0x10000    /* XCC gfxdec1 upper Bound */

#define NORMALIZE_XCC_REG_OFFSET(offset) \
	(offset & 0xFFFF)

static const struct amdgpu_hwip_reg_entry gc_reg_list_9_4_3[] = {
	SOC15_REG_ENTRY_STR(GC, 0, regGRBM_STATUS),
	SOC15_REG_ENTRY_STR(GC, 0, regGRBM_STATUS2),
	SOC15_REG_ENTRY_STR(GC, 0, regCP_STALLED_STAT1),
	SOC15_REG_ENTRY_STR(GC, 0, regCP_STALLED_STAT2),
	SOC15_REG_ENTRY_STR(GC, 0, regCP_CPC_STALLED_STAT1),
	SOC15_REG_ENTRY_STR(GC, 0, regCP_CPF_STALLED_STAT1),
	SOC15_REG_ENTRY_STR(GC, 0, regCP_BUSY_STAT),
	SOC15_REG_ENTRY_STR(GC, 0, regCP_CPC_BUSY_STAT),
	SOC15_REG_ENTRY_STR(GC, 0, regCP_CPF_BUSY_STAT),
	SOC15_REG_ENTRY_STR(GC, 0, regCP_CPF_STATUS),
	SOC15_REG_ENTRY_STR(GC, 0, regCP_GFX_ERROR),
	SOC15_REG_ENTRY_STR(GC, 0, regCPF_UTCL1_STATUS),
	SOC15_REG_ENTRY_STR(GC, 0, regCPC_UTCL1_STATUS),
	SOC15_REG_ENTRY_STR(GC, 0, regCPG_UTCL1_STATUS),
	SOC15_REG_ENTRY_STR(GC, 0, regGDS_PROTECTION_FAULT),
	SOC15_REG_ENTRY_STR(GC, 0, regGDS_VM_PROTECTION_FAULT),
	SOC15_REG_ENTRY_STR(GC, 0, regRLC_UTCL1_STATUS),
	SOC15_REG_ENTRY_STR(GC, 0, regRMI_UTCL1_STATUS),
	SOC15_REG_ENTRY_STR(GC, 0, regSQC_DCACHE_UTCL1_STATUS),
	SOC15_REG_ENTRY_STR(GC, 0, regSQC_ICACHE_UTCL1_STATUS),
	SOC15_REG_ENTRY_STR(GC, 0, regSQ_UTCL1_STATUS),
	SOC15_REG_ENTRY_STR(GC, 0, regTCP_UTCL1_STATUS),
	SOC15_REG_ENTRY_STR(GC, 0, regWD_UTCL1_STATUS),
	SOC15_REG_ENTRY_STR(GC, 0, regVM_L2_PROTECTION_FAULT_CNTL),
	SOC15_REG_ENTRY_STR(GC, 0, regVM_L2_PROTECTION_FAULT_STATUS),
	SOC15_REG_ENTRY_STR(GC, 0, regCP_DEBUG),
	SOC15_REG_ENTRY_STR(GC, 0, regCP_MEC_CNTL),
	SOC15_REG_ENTRY_STR(GC, 0, regCP_MEC1_INSTR_PNTR),
	SOC15_REG_ENTRY_STR(GC, 0, regCP_MEC2_INSTR_PNTR),
	SOC15_REG_ENTRY_STR(GC, 0, regCP_CPC_STATUS),
	SOC15_REG_ENTRY_STR(GC, 0, regRLC_STAT),
	SOC15_REG_ENTRY_STR(GC, 0, regRLC_SMU_COMMAND),
	SOC15_REG_ENTRY_STR(GC, 0, regRLC_SMU_MESSAGE),
	SOC15_REG_ENTRY_STR(GC, 0, regRLC_SMU_ARGUMENT_1),
	SOC15_REG_ENTRY_STR(GC, 0, regRLC_SMU_ARGUMENT_2),
	SOC15_REG_ENTRY_STR(GC, 0, regSMU_RLC_RESPONSE),
	SOC15_REG_ENTRY_STR(GC, 0, regRLC_SAFE_MODE),
	SOC15_REG_ENTRY_STR(GC, 0, regRLC_SMU_SAFE_MODE),
	SOC15_REG_ENTRY_STR(GC, 0, regRLC_INT_STAT),
	SOC15_REG_ENTRY_STR(GC, 0, regRLC_GPM_GENERAL_6),
	/* cp header registers */
	SOC15_REG_ENTRY_STR(GC, 0, regCP_MEC_ME1_HEADER_DUMP),
	SOC15_REG_ENTRY_STR(GC, 0, regCP_MEC_ME2_HEADER_DUMP),
	/* SE status registers */
	SOC15_REG_ENTRY_STR(GC, 0, regGRBM_STATUS_SE0),
	SOC15_REG_ENTRY_STR(GC, 0, regGRBM_STATUS_SE1),
	SOC15_REG_ENTRY_STR(GC, 0, regGRBM_STATUS_SE2),
	SOC15_REG_ENTRY_STR(GC, 0, regGRBM_STATUS_SE3)
};

static const struct amdgpu_hwip_reg_entry gc_cp_reg_list_9_4_3[] = {
	/* compute queue registers */
	SOC15_REG_ENTRY_STR(GC, 0, regCP_HQD_VMID),
	SOC15_REG_ENTRY_STR(GC, 0, regCP_HQD_ACTIVE),
	SOC15_REG_ENTRY_STR(GC, 0, regCP_HQD_PERSISTENT_STATE),
	SOC15_REG_ENTRY_STR(GC, 0, regCP_HQD_PIPE_PRIORITY),
	SOC15_REG_ENTRY_STR(GC, 0, regCP_HQD_QUEUE_PRIORITY),
	SOC15_REG_ENTRY_STR(GC, 0, regCP_HQD_QUANTUM),
	SOC15_REG_ENTRY_STR(GC, 0, regCP_HQD_PQ_BASE),
	SOC15_REG_ENTRY_STR(GC, 0, regCP_HQD_PQ_BASE_HI),
	SOC15_REG_ENTRY_STR(GC, 0, regCP_HQD_PQ_RPTR),
	SOC15_REG_ENTRY_STR(GC, 0, regCP_HQD_PQ_WPTR_POLL_ADDR),
	SOC15_REG_ENTRY_STR(GC, 0, regCP_HQD_PQ_WPTR_POLL_ADDR_HI),
	SOC15_REG_ENTRY_STR(GC, 0, regCP_HQD_PQ_DOORBELL_CONTROL),
	SOC15_REG_ENTRY_STR(GC, 0, regCP_HQD_PQ_CONTROL),
	SOC15_REG_ENTRY_STR(GC, 0, regCP_HQD_IB_BASE_ADDR),
	SOC15_REG_ENTRY_STR(GC, 0, regCP_HQD_IB_BASE_ADDR_HI),
	SOC15_REG_ENTRY_STR(GC, 0, regCP_HQD_IB_RPTR),
	SOC15_REG_ENTRY_STR(GC, 0, regCP_HQD_IB_CONTROL),
	SOC15_REG_ENTRY_STR(GC, 0, regCP_HQD_DEQUEUE_REQUEST),
	SOC15_REG_ENTRY_STR(GC, 0, regCP_HQD_EOP_BASE_ADDR),
	SOC15_REG_ENTRY_STR(GC, 0, regCP_HQD_EOP_BASE_ADDR_HI),
	SOC15_REG_ENTRY_STR(GC, 0, regCP_HQD_EOP_CONTROL),
	SOC15_REG_ENTRY_STR(GC, 0, regCP_HQD_EOP_RPTR),
	SOC15_REG_ENTRY_STR(GC, 0, regCP_HQD_EOP_WPTR),
	SOC15_REG_ENTRY_STR(GC, 0, regCP_HQD_EOP_EVENTS),
	SOC15_REG_ENTRY_STR(GC, 0, regCP_HQD_CTX_SAVE_BASE_ADDR_LO),
	SOC15_REG_ENTRY_STR(GC, 0, regCP_HQD_CTX_SAVE_BASE_ADDR_HI),
	SOC15_REG_ENTRY_STR(GC, 0, regCP_HQD_CTX_SAVE_CONTROL),
	SOC15_REG_ENTRY_STR(GC, 0, regCP_HQD_CNTL_STACK_OFFSET),
	SOC15_REG_ENTRY_STR(GC, 0, regCP_HQD_CNTL_STACK_SIZE),
	SOC15_REG_ENTRY_STR(GC, 0, regCP_HQD_WG_STATE_OFFSET),
	SOC15_REG_ENTRY_STR(GC, 0, regCP_HQD_CTX_SAVE_SIZE),
	SOC15_REG_ENTRY_STR(GC, 0, regCP_HQD_GDS_RESOURCE_STATE),
	SOC15_REG_ENTRY_STR(GC, 0, regCP_HQD_ERROR),
	SOC15_REG_ENTRY_STR(GC, 0, regCP_HQD_EOP_WPTR_MEM),
	SOC15_REG_ENTRY_STR(GC, 0, regCP_HQD_PQ_WPTR_LO),
	SOC15_REG_ENTRY_STR(GC, 0, regCP_HQD_PQ_WPTR_HI),
	SOC15_REG_ENTRY_STR(GC, 0, regCP_HQD_GFX_STATUS),
};

struct amdgpu_gfx_ras gfx_v9_4_3_ras;

static void gfx_v9_4_3_set_ring_funcs(struct amdgpu_device *adev);
static void gfx_v9_4_3_set_irq_funcs(struct amdgpu_device *adev);
static void gfx_v9_4_3_set_gds_init(struct amdgpu_device *adev);
static void gfx_v9_4_3_set_rlc_funcs(struct amdgpu_device *adev);
static int gfx_v9_4_3_get_cu_info(struct amdgpu_device *adev,
				struct amdgpu_cu_info *cu_info);
static void gfx_v9_4_3_xcc_set_safe_mode(struct amdgpu_device *adev, int xcc_id);
static void gfx_v9_4_3_xcc_unset_safe_mode(struct amdgpu_device *adev, int xcc_id);

static void gfx_v9_4_3_kiq_set_resources(struct amdgpu_ring *kiq_ring,
				uint64_t queue_mask)
{
	struct amdgpu_device *adev = kiq_ring->adev;
	u64 shader_mc_addr;

	/* Cleaner shader MC address */
	shader_mc_addr = adev->gfx.cleaner_shader_gpu_addr >> 8;

	amdgpu_ring_write(kiq_ring, PACKET3(PACKET3_SET_RESOURCES, 6));
	amdgpu_ring_write(kiq_ring,
		PACKET3_SET_RESOURCES_VMID_MASK(0) |
		/* vmid_mask:0* queue_type:0 (KIQ) */
		PACKET3_SET_RESOURCES_QUEUE_TYPE(0));
	amdgpu_ring_write(kiq_ring,
			lower_32_bits(queue_mask));	/* queue mask lo */
	amdgpu_ring_write(kiq_ring,
			upper_32_bits(queue_mask));	/* queue mask hi */
	amdgpu_ring_write(kiq_ring, lower_32_bits(shader_mc_addr)); /* cleaner shader addr lo */
	amdgpu_ring_write(kiq_ring, upper_32_bits(shader_mc_addr)); /* cleaner shader addr hi */
	amdgpu_ring_write(kiq_ring, 0);	/* oac mask */
	amdgpu_ring_write(kiq_ring, 0);	/* gds heap base:0, gds heap size:0 */
}

static void gfx_v9_4_3_kiq_map_queues(struct amdgpu_ring *kiq_ring,
				 struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = kiq_ring->adev;
	uint64_t mqd_addr = amdgpu_bo_gpu_offset(ring->mqd_obj);
	uint64_t wptr_addr = adev->wb.gpu_addr + (ring->wptr_offs * 4);
	uint32_t eng_sel = ring->funcs->type == AMDGPU_RING_TYPE_GFX ? 4 : 0;

	amdgpu_ring_write(kiq_ring, PACKET3(PACKET3_MAP_QUEUES, 5));
	/* Q_sel:0, vmid:0, vidmem: 1, engine:0, num_Q:1*/
	amdgpu_ring_write(kiq_ring, /* Q_sel: 0, vmid: 0, engine: 0, num_Q: 1 */
			 PACKET3_MAP_QUEUES_QUEUE_SEL(0) | /* Queue_Sel */
			 PACKET3_MAP_QUEUES_VMID(0) | /* VMID */
			 PACKET3_MAP_QUEUES_QUEUE(ring->queue) |
			 PACKET3_MAP_QUEUES_PIPE(ring->pipe) |
			 PACKET3_MAP_QUEUES_ME((ring->me == 1 ? 0 : 1)) |
			 /*queue_type: normal compute queue */
			 PACKET3_MAP_QUEUES_QUEUE_TYPE(0) |
			 /* alloc format: all_on_one_pipe */
			 PACKET3_MAP_QUEUES_ALLOC_FORMAT(0) |
			 PACKET3_MAP_QUEUES_ENGINE_SEL(eng_sel) |
			 /* num_queues: must be 1 */
			 PACKET3_MAP_QUEUES_NUM_QUEUES(1));
	amdgpu_ring_write(kiq_ring,
			PACKET3_MAP_QUEUES_DOORBELL_OFFSET(ring->doorbell_index));
	amdgpu_ring_write(kiq_ring, lower_32_bits(mqd_addr));
	amdgpu_ring_write(kiq_ring, upper_32_bits(mqd_addr));
	amdgpu_ring_write(kiq_ring, lower_32_bits(wptr_addr));
	amdgpu_ring_write(kiq_ring, upper_32_bits(wptr_addr));
}

static void gfx_v9_4_3_kiq_unmap_queues(struct amdgpu_ring *kiq_ring,
				   struct amdgpu_ring *ring,
				   enum amdgpu_unmap_queues_action action,
				   u64 gpu_addr, u64 seq)
{
	uint32_t eng_sel = ring->funcs->type == AMDGPU_RING_TYPE_GFX ? 4 : 0;

	amdgpu_ring_write(kiq_ring, PACKET3(PACKET3_UNMAP_QUEUES, 4));
	amdgpu_ring_write(kiq_ring, /* Q_sel: 0, vmid: 0, engine: 0, num_Q: 1 */
			  PACKET3_UNMAP_QUEUES_ACTION(action) |
			  PACKET3_UNMAP_QUEUES_QUEUE_SEL(0) |
			  PACKET3_UNMAP_QUEUES_ENGINE_SEL(eng_sel) |
			  PACKET3_UNMAP_QUEUES_NUM_QUEUES(1));
	amdgpu_ring_write(kiq_ring,
			PACKET3_UNMAP_QUEUES_DOORBELL_OFFSET0(ring->doorbell_index));

	if (action == PREEMPT_QUEUES_NO_UNMAP) {
		amdgpu_ring_write(kiq_ring, lower_32_bits(gpu_addr));
		amdgpu_ring_write(kiq_ring, upper_32_bits(gpu_addr));
		amdgpu_ring_write(kiq_ring, seq);
	} else {
		amdgpu_ring_write(kiq_ring, 0);
		amdgpu_ring_write(kiq_ring, 0);
		amdgpu_ring_write(kiq_ring, 0);
	}
}

static void gfx_v9_4_3_kiq_query_status(struct amdgpu_ring *kiq_ring,
				   struct amdgpu_ring *ring,
				   u64 addr,
				   u64 seq)
{
	uint32_t eng_sel = ring->funcs->type == AMDGPU_RING_TYPE_GFX ? 4 : 0;

	amdgpu_ring_write(kiq_ring, PACKET3(PACKET3_QUERY_STATUS, 5));
	amdgpu_ring_write(kiq_ring,
			  PACKET3_QUERY_STATUS_CONTEXT_ID(0) |
			  PACKET3_QUERY_STATUS_INTERRUPT_SEL(0) |
			  PACKET3_QUERY_STATUS_COMMAND(2));
	/* Q_sel: 0, vmid: 0, engine: 0, num_Q: 1 */
	amdgpu_ring_write(kiq_ring,
			PACKET3_QUERY_STATUS_DOORBELL_OFFSET(ring->doorbell_index) |
			PACKET3_QUERY_STATUS_ENG_SEL(eng_sel));
	amdgpu_ring_write(kiq_ring, lower_32_bits(addr));
	amdgpu_ring_write(kiq_ring, upper_32_bits(addr));
	amdgpu_ring_write(kiq_ring, lower_32_bits(seq));
	amdgpu_ring_write(kiq_ring, upper_32_bits(seq));
}

static void gfx_v9_4_3_kiq_invalidate_tlbs(struct amdgpu_ring *kiq_ring,
				uint16_t pasid, uint32_t flush_type,
				bool all_hub)
{
	amdgpu_ring_write(kiq_ring, PACKET3(PACKET3_INVALIDATE_TLBS, 0));
	amdgpu_ring_write(kiq_ring,
			PACKET3_INVALIDATE_TLBS_DST_SEL(1) |
			PACKET3_INVALIDATE_TLBS_ALL_HUB(all_hub) |
			PACKET3_INVALIDATE_TLBS_PASID(pasid) |
			PACKET3_INVALIDATE_TLBS_FLUSH_TYPE(flush_type));
}

static void gfx_v9_4_3_kiq_reset_hw_queue(struct amdgpu_ring *kiq_ring, uint32_t queue_type,
					  uint32_t me_id, uint32_t pipe_id, uint32_t queue_id,
					  uint32_t xcc_id, uint32_t vmid)
{
	struct amdgpu_device *adev = kiq_ring->adev;
	unsigned i;

	/* enter save mode */
	amdgpu_gfx_rlc_enter_safe_mode(adev, xcc_id);
	mutex_lock(&adev->srbm_mutex);
	soc15_grbm_select(adev, me_id, pipe_id, queue_id, 0, xcc_id);

	if (queue_type == AMDGPU_RING_TYPE_COMPUTE) {
		WREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_HQD_DEQUEUE_REQUEST, 0x2);
		WREG32_SOC15(GC, GET_INST(GC, xcc_id), regSPI_COMPUTE_QUEUE_RESET, 0x1);
		/* wait till dequeue take effects */
		for (i = 0; i < adev->usec_timeout; i++) {
			if (!(RREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_HQD_ACTIVE) & 1))
				break;
			udelay(1);
		}
		if (i >= adev->usec_timeout)
			dev_err(adev->dev, "fail to wait on hqd deactive\n");
	} else {
		dev_err(adev->dev, "reset queue_type(%d) not supported\n\n", queue_type);
	}

	soc15_grbm_select(adev, 0, 0, 0, 0, 0);
	mutex_unlock(&adev->srbm_mutex);
	/* exit safe mode */
	amdgpu_gfx_rlc_exit_safe_mode(adev, xcc_id);
}

static const struct kiq_pm4_funcs gfx_v9_4_3_kiq_pm4_funcs = {
	.kiq_set_resources = gfx_v9_4_3_kiq_set_resources,
	.kiq_map_queues = gfx_v9_4_3_kiq_map_queues,
	.kiq_unmap_queues = gfx_v9_4_3_kiq_unmap_queues,
	.kiq_query_status = gfx_v9_4_3_kiq_query_status,
	.kiq_invalidate_tlbs = gfx_v9_4_3_kiq_invalidate_tlbs,
	.kiq_reset_hw_queue = gfx_v9_4_3_kiq_reset_hw_queue,
	.set_resources_size = 8,
	.map_queues_size = 7,
	.unmap_queues_size = 6,
	.query_status_size = 7,
	.invalidate_tlbs_size = 2,
};

static void gfx_v9_4_3_set_kiq_pm4_funcs(struct amdgpu_device *adev)
{
	int i, num_xcc;

	num_xcc = NUM_XCC(adev->gfx.xcc_mask);
	for (i = 0; i < num_xcc; i++)
		adev->gfx.kiq[i].pmf = &gfx_v9_4_3_kiq_pm4_funcs;
}

static void gfx_v9_4_3_init_golden_registers(struct amdgpu_device *adev)
{
	int i, num_xcc, dev_inst;

	num_xcc = NUM_XCC(adev->gfx.xcc_mask);
	for (i = 0; i < num_xcc; i++) {
		dev_inst = GET_INST(GC, i);

		WREG32_SOC15(GC, dev_inst, regGB_ADDR_CONFIG,
			     GOLDEN_GB_ADDR_CONFIG);
		/* Golden settings applied by driver for ASIC with rev_id 0 */
		if (adev->rev_id == 0) {
			WREG32_FIELD15_PREREG(GC, dev_inst, TCP_UTCL1_CNTL1,
					      REDUCE_FIFO_DEPTH_BY_2, 2);
		} else {
			WREG32_FIELD15_PREREG(GC, dev_inst, TCP_UTCL1_CNTL2,
						SPARE, 0x1);
		}
	}
}

static uint32_t gfx_v9_4_3_normalize_xcc_reg_offset(uint32_t reg)
{
	uint32_t normalized_reg = NORMALIZE_XCC_REG_OFFSET(reg);

	/* If it is an XCC reg, normalize the reg to keep
	   lower 16 bits in local xcc */

	if (((normalized_reg >= XCC_REG_RANGE_0_LOW) && (normalized_reg < XCC_REG_RANGE_0_HIGH)) ||
		((normalized_reg >= XCC_REG_RANGE_1_LOW) && (normalized_reg < XCC_REG_RANGE_1_HIGH)))
		return normalized_reg;
	else
		return reg;
}

static void gfx_v9_4_3_write_data_to_reg(struct amdgpu_ring *ring, int eng_sel,
				       bool wc, uint32_t reg, uint32_t val)
{
	reg = gfx_v9_4_3_normalize_xcc_reg_offset(reg);
	amdgpu_ring_write(ring, PACKET3(PACKET3_WRITE_DATA, 3));
	amdgpu_ring_write(ring, WRITE_DATA_ENGINE_SEL(eng_sel) |
				WRITE_DATA_DST_SEL(0) |
				(wc ? WR_CONFIRM : 0));
	amdgpu_ring_write(ring, reg);
	amdgpu_ring_write(ring, 0);
	amdgpu_ring_write(ring, val);
}

static void gfx_v9_4_3_wait_reg_mem(struct amdgpu_ring *ring, int eng_sel,
				  int mem_space, int opt, uint32_t addr0,
				  uint32_t addr1, uint32_t ref, uint32_t mask,
				  uint32_t inv)
{
	/* Only do the normalization on regspace */
	if (mem_space == 0) {
		addr0 = gfx_v9_4_3_normalize_xcc_reg_offset(addr0);
		addr1 = gfx_v9_4_3_normalize_xcc_reg_offset(addr1);
	}

	amdgpu_ring_write(ring, PACKET3(PACKET3_WAIT_REG_MEM, 5));
	amdgpu_ring_write(ring,
				 /* memory (1) or register (0) */
				 (WAIT_REG_MEM_MEM_SPACE(mem_space) |
				 WAIT_REG_MEM_OPERATION(opt) | /* wait */
				 WAIT_REG_MEM_FUNCTION(3) |  /* equal */
				 WAIT_REG_MEM_ENGINE(eng_sel)));

	if (mem_space)
		BUG_ON(addr0 & 0x3); /* Dword align */
	amdgpu_ring_write(ring, addr0);
	amdgpu_ring_write(ring, addr1);
	amdgpu_ring_write(ring, ref);
	amdgpu_ring_write(ring, mask);
	amdgpu_ring_write(ring, inv); /* poll interval */
}

static int gfx_v9_4_3_ring_test_ring(struct amdgpu_ring *ring)
{
	uint32_t scratch_reg0_offset, xcc_offset;
	struct amdgpu_device *adev = ring->adev;
	uint32_t tmp = 0;
	unsigned i;
	int r;

	/* Use register offset which is local to XCC in the packet */
	xcc_offset = SOC15_REG_OFFSET(GC, 0, regSCRATCH_REG0);
	scratch_reg0_offset = SOC15_REG_OFFSET(GC, GET_INST(GC, ring->xcc_id), regSCRATCH_REG0);
	WREG32(scratch_reg0_offset, 0xCAFEDEAD);
	tmp = RREG32(scratch_reg0_offset);

	r = amdgpu_ring_alloc(ring, 3);
	if (r)
		return r;

	amdgpu_ring_write(ring, PACKET3(PACKET3_SET_UCONFIG_REG, 1));
	amdgpu_ring_write(ring, xcc_offset - PACKET3_SET_UCONFIG_REG_START);
	amdgpu_ring_write(ring, 0xDEADBEEF);
	amdgpu_ring_commit(ring);

	for (i = 0; i < adev->usec_timeout; i++) {
		tmp = RREG32(scratch_reg0_offset);
		if (tmp == 0xDEADBEEF)
			break;
		udelay(1);
	}

	if (i >= adev->usec_timeout)
		r = -ETIMEDOUT;
	return r;
}

static int gfx_v9_4_3_ring_test_ib(struct amdgpu_ring *ring, long timeout)
{
	struct amdgpu_device *adev = ring->adev;
	struct amdgpu_ib ib;
	struct dma_fence *f = NULL;

	unsigned index;
	uint64_t gpu_addr;
	uint32_t tmp;
	long r;

	r = amdgpu_device_wb_get(adev, &index);
	if (r)
		return r;

	gpu_addr = adev->wb.gpu_addr + (index * 4);
	adev->wb.wb[index] = cpu_to_le32(0xCAFEDEAD);
	memset(&ib, 0, sizeof(ib));

	r = amdgpu_ib_get(adev, NULL, 20, AMDGPU_IB_POOL_DIRECT, &ib);
	if (r)
		goto err1;

	ib.ptr[0] = PACKET3(PACKET3_WRITE_DATA, 3);
	ib.ptr[1] = WRITE_DATA_DST_SEL(5) | WR_CONFIRM;
	ib.ptr[2] = lower_32_bits(gpu_addr);
	ib.ptr[3] = upper_32_bits(gpu_addr);
	ib.ptr[4] = 0xDEADBEEF;
	ib.length_dw = 5;

	r = amdgpu_ib_schedule(ring, 1, &ib, NULL, &f);
	if (r)
		goto err2;

	r = dma_fence_wait_timeout(f, false, timeout);
	if (r == 0) {
		r = -ETIMEDOUT;
		goto err2;
	} else if (r < 0) {
		goto err2;
	}

	tmp = adev->wb.wb[index];
	if (tmp == 0xDEADBEEF)
		r = 0;
	else
		r = -EINVAL;

err2:
	amdgpu_ib_free(adev, &ib, NULL);
	dma_fence_put(f);
err1:
	amdgpu_device_wb_free(adev, index);
	return r;
}


/* This value might differs per partition */
static uint64_t gfx_v9_4_3_get_gpu_clock_counter(struct amdgpu_device *adev)
{
	uint64_t clock;

	mutex_lock(&adev->gfx.gpu_clock_mutex);
	WREG32_SOC15(GC, GET_INST(GC, 0), regRLC_CAPTURE_GPU_CLOCK_COUNT, 1);
	clock = (uint64_t)RREG32_SOC15(GC, GET_INST(GC, 0), regRLC_GPU_CLOCK_COUNT_LSB) |
		((uint64_t)RREG32_SOC15(GC, GET_INST(GC, 0), regRLC_GPU_CLOCK_COUNT_MSB) << 32ULL);
	mutex_unlock(&adev->gfx.gpu_clock_mutex);

	return clock;
}

static void gfx_v9_4_3_free_microcode(struct amdgpu_device *adev)
{
	amdgpu_ucode_release(&adev->gfx.pfp_fw);
	amdgpu_ucode_release(&adev->gfx.me_fw);
	amdgpu_ucode_release(&adev->gfx.ce_fw);
	amdgpu_ucode_release(&adev->gfx.rlc_fw);
	amdgpu_ucode_release(&adev->gfx.mec_fw);
	amdgpu_ucode_release(&adev->gfx.mec2_fw);

	kfree(adev->gfx.rlc.register_list_format);
}

static int gfx_v9_4_3_init_rlc_microcode(struct amdgpu_device *adev,
					  const char *chip_name)
{
	int err;
	const struct rlc_firmware_header_v2_0 *rlc_hdr;
	uint16_t version_major;
	uint16_t version_minor;


	err = amdgpu_ucode_request(adev, &adev->gfx.rlc_fw,
				   "amdgpu/%s_rlc.bin", chip_name);
	if (err)
		goto out;
	rlc_hdr = (const struct rlc_firmware_header_v2_0 *)adev->gfx.rlc_fw->data;

	version_major = le16_to_cpu(rlc_hdr->header.header_version_major);
	version_minor = le16_to_cpu(rlc_hdr->header.header_version_minor);
	err = amdgpu_gfx_rlc_init_microcode(adev, version_major, version_minor);
out:
	if (err)
		amdgpu_ucode_release(&adev->gfx.rlc_fw);

	return err;
}

static bool gfx_v9_4_3_should_disable_gfxoff(struct pci_dev *pdev)
{
	return true;
}

static void gfx_v9_4_3_check_if_need_gfxoff(struct amdgpu_device *adev)
{
	if (gfx_v9_4_3_should_disable_gfxoff(adev->pdev))
		adev->pm.pp_feature &= ~PP_GFXOFF_MASK;
}

static int gfx_v9_4_3_init_cp_compute_microcode(struct amdgpu_device *adev,
					  const char *chip_name)
{
	int err;

	if (amdgpu_sriov_vf(adev))
		err = amdgpu_ucode_request(adev, &adev->gfx.mec_fw,
				"amdgpu/%s_sjt_mec.bin", chip_name);
	else
		err = amdgpu_ucode_request(adev, &adev->gfx.mec_fw,
				"amdgpu/%s_mec.bin", chip_name);
	if (err)
		goto out;
	amdgpu_gfx_cp_init_microcode(adev, AMDGPU_UCODE_ID_CP_MEC1);
	amdgpu_gfx_cp_init_microcode(adev, AMDGPU_UCODE_ID_CP_MEC1_JT);

	adev->gfx.mec2_fw_version = adev->gfx.mec_fw_version;
	adev->gfx.mec2_feature_version = adev->gfx.mec_feature_version;

	gfx_v9_4_3_check_if_need_gfxoff(adev);

out:
	if (err)
		amdgpu_ucode_release(&adev->gfx.mec_fw);
	return err;
}

static int gfx_v9_4_3_init_microcode(struct amdgpu_device *adev)
{
	char ucode_prefix[15];
	int r;

	amdgpu_ucode_ip_version_decode(adev, GC_HWIP, ucode_prefix, sizeof(ucode_prefix));

	r = gfx_v9_4_3_init_rlc_microcode(adev, ucode_prefix);
	if (r)
		return r;

	r = gfx_v9_4_3_init_cp_compute_microcode(adev, ucode_prefix);
	if (r)
		return r;

	return r;
}

static void gfx_v9_4_3_mec_fini(struct amdgpu_device *adev)
{
	amdgpu_bo_free_kernel(&adev->gfx.mec.hpd_eop_obj, NULL, NULL);
	amdgpu_bo_free_kernel(&adev->gfx.mec.mec_fw_obj, NULL, NULL);
}

static int gfx_v9_4_3_mec_init(struct amdgpu_device *adev)
{
	int r, i, num_xcc;
	u32 *hpd;
	const __le32 *fw_data;
	unsigned fw_size;
	u32 *fw;
	size_t mec_hpd_size;

	const struct gfx_firmware_header_v1_0 *mec_hdr;

	num_xcc = NUM_XCC(adev->gfx.xcc_mask);
	for (i = 0; i < num_xcc; i++)
		bitmap_zero(adev->gfx.mec_bitmap[i].queue_bitmap,
			AMDGPU_MAX_COMPUTE_QUEUES);

	/* take ownership of the relevant compute queues */
	amdgpu_gfx_compute_queue_acquire(adev);
	mec_hpd_size =
		adev->gfx.num_compute_rings * num_xcc * GFX9_MEC_HPD_SIZE;
	if (mec_hpd_size) {
		r = amdgpu_bo_create_reserved(adev, mec_hpd_size, PAGE_SIZE,
					      AMDGPU_GEM_DOMAIN_VRAM |
					      AMDGPU_GEM_DOMAIN_GTT,
					      &adev->gfx.mec.hpd_eop_obj,
					      &adev->gfx.mec.hpd_eop_gpu_addr,
					      (void **)&hpd);
		if (r) {
			dev_warn(adev->dev, "(%d) create HDP EOP bo failed\n", r);
			gfx_v9_4_3_mec_fini(adev);
			return r;
		}

		if (amdgpu_emu_mode == 1) {
			for (i = 0; i < mec_hpd_size / 4; i++) {
				memset((void *)(hpd + i), 0, 4);
				if (i % 50 == 0)
					msleep(1);
			}
		} else {
			memset(hpd, 0, mec_hpd_size);
		}

		amdgpu_bo_kunmap(adev->gfx.mec.hpd_eop_obj);
		amdgpu_bo_unreserve(adev->gfx.mec.hpd_eop_obj);
	}

	mec_hdr = (const struct gfx_firmware_header_v1_0 *)adev->gfx.mec_fw->data;

	fw_data = (const __le32 *)
		(adev->gfx.mec_fw->data +
		 le32_to_cpu(mec_hdr->header.ucode_array_offset_bytes));
	fw_size = le32_to_cpu(mec_hdr->header.ucode_size_bytes);

	r = amdgpu_bo_create_reserved(adev, mec_hdr->header.ucode_size_bytes,
				      PAGE_SIZE, AMDGPU_GEM_DOMAIN_GTT,
				      &adev->gfx.mec.mec_fw_obj,
				      &adev->gfx.mec.mec_fw_gpu_addr,
				      (void **)&fw);
	if (r) {
		dev_warn(adev->dev, "(%d) create mec firmware bo failed\n", r);
		gfx_v9_4_3_mec_fini(adev);
		return r;
	}

	memcpy(fw, fw_data, fw_size);

	amdgpu_bo_kunmap(adev->gfx.mec.mec_fw_obj);
	amdgpu_bo_unreserve(adev->gfx.mec.mec_fw_obj);

	return 0;
}

static void gfx_v9_4_3_xcc_select_se_sh(struct amdgpu_device *adev, u32 se_num,
					u32 sh_num, u32 instance, int xcc_id)
{
	u32 data;

	if (instance == 0xffffffff)
		data = REG_SET_FIELD(0, GRBM_GFX_INDEX,
				     INSTANCE_BROADCAST_WRITES, 1);
	else
		data = REG_SET_FIELD(0, GRBM_GFX_INDEX,
				     INSTANCE_INDEX, instance);

	if (se_num == 0xffffffff)
		data = REG_SET_FIELD(data, GRBM_GFX_INDEX,
				     SE_BROADCAST_WRITES, 1);
	else
		data = REG_SET_FIELD(data, GRBM_GFX_INDEX, SE_INDEX, se_num);

	if (sh_num == 0xffffffff)
		data = REG_SET_FIELD(data, GRBM_GFX_INDEX,
				     SH_BROADCAST_WRITES, 1);
	else
		data = REG_SET_FIELD(data, GRBM_GFX_INDEX, SH_INDEX, sh_num);

	WREG32_SOC15_RLC_SHADOW_EX(reg, GC, GET_INST(GC, xcc_id), regGRBM_GFX_INDEX, data);
}

static uint32_t wave_read_ind(struct amdgpu_device *adev, uint32_t xcc_id, uint32_t simd, uint32_t wave, uint32_t address)
{
	WREG32_SOC15_RLC(GC, GET_INST(GC, xcc_id), regSQ_IND_INDEX,
		(wave << SQ_IND_INDEX__WAVE_ID__SHIFT) |
		(simd << SQ_IND_INDEX__SIMD_ID__SHIFT) |
		(address << SQ_IND_INDEX__INDEX__SHIFT) |
		(SQ_IND_INDEX__FORCE_READ_MASK));
	return RREG32_SOC15(GC, GET_INST(GC, xcc_id), regSQ_IND_DATA);
}

static void wave_read_regs(struct amdgpu_device *adev, uint32_t xcc_id, uint32_t simd,
			   uint32_t wave, uint32_t thread,
			   uint32_t regno, uint32_t num, uint32_t *out)
{
	WREG32_SOC15_RLC(GC, GET_INST(GC, xcc_id), regSQ_IND_INDEX,
		(wave << SQ_IND_INDEX__WAVE_ID__SHIFT) |
		(simd << SQ_IND_INDEX__SIMD_ID__SHIFT) |
		(regno << SQ_IND_INDEX__INDEX__SHIFT) |
		(thread << SQ_IND_INDEX__THREAD_ID__SHIFT) |
		(SQ_IND_INDEX__FORCE_READ_MASK) |
		(SQ_IND_INDEX__AUTO_INCR_MASK));
	while (num--)
		*(out++) = RREG32_SOC15(GC, GET_INST(GC, xcc_id), regSQ_IND_DATA);
}

static void gfx_v9_4_3_read_wave_data(struct amdgpu_device *adev,
				      uint32_t xcc_id, uint32_t simd, uint32_t wave,
				      uint32_t *dst, int *no_fields)
{
	/* type 1 wave data */
	dst[(*no_fields)++] = 1;
	dst[(*no_fields)++] = wave_read_ind(adev, xcc_id, simd, wave, ixSQ_WAVE_STATUS);
	dst[(*no_fields)++] = wave_read_ind(adev, xcc_id, simd, wave, ixSQ_WAVE_PC_LO);
	dst[(*no_fields)++] = wave_read_ind(adev, xcc_id, simd, wave, ixSQ_WAVE_PC_HI);
	dst[(*no_fields)++] = wave_read_ind(adev, xcc_id, simd, wave, ixSQ_WAVE_EXEC_LO);
	dst[(*no_fields)++] = wave_read_ind(adev, xcc_id, simd, wave, ixSQ_WAVE_EXEC_HI);
	dst[(*no_fields)++] = wave_read_ind(adev, xcc_id, simd, wave, ixSQ_WAVE_HW_ID);
	dst[(*no_fields)++] = wave_read_ind(adev, xcc_id, simd, wave, ixSQ_WAVE_INST_DW0);
	dst[(*no_fields)++] = wave_read_ind(adev, xcc_id, simd, wave, ixSQ_WAVE_INST_DW1);
	dst[(*no_fields)++] = wave_read_ind(adev, xcc_id, simd, wave, ixSQ_WAVE_GPR_ALLOC);
	dst[(*no_fields)++] = wave_read_ind(adev, xcc_id, simd, wave, ixSQ_WAVE_LDS_ALLOC);
	dst[(*no_fields)++] = wave_read_ind(adev, xcc_id, simd, wave, ixSQ_WAVE_TRAPSTS);
	dst[(*no_fields)++] = wave_read_ind(adev, xcc_id, simd, wave, ixSQ_WAVE_IB_STS);
	dst[(*no_fields)++] = wave_read_ind(adev, xcc_id, simd, wave, ixSQ_WAVE_IB_DBG0);
	dst[(*no_fields)++] = wave_read_ind(adev, xcc_id, simd, wave, ixSQ_WAVE_M0);
	dst[(*no_fields)++] = wave_read_ind(adev, xcc_id, simd, wave, ixSQ_WAVE_MODE);
}

static void gfx_v9_4_3_read_wave_sgprs(struct amdgpu_device *adev, uint32_t xcc_id, uint32_t simd,
				       uint32_t wave, uint32_t start,
				       uint32_t size, uint32_t *dst)
{
	wave_read_regs(adev, xcc_id, simd, wave, 0,
		       start + SQIND_WAVE_SGPRS_OFFSET, size, dst);
}

static void gfx_v9_4_3_read_wave_vgprs(struct amdgpu_device *adev, uint32_t xcc_id, uint32_t simd,
				       uint32_t wave, uint32_t thread,
				       uint32_t start, uint32_t size,
				       uint32_t *dst)
{
	wave_read_regs(adev, xcc_id, simd, wave, thread,
		       start + SQIND_WAVE_VGPRS_OFFSET, size, dst);
}

static void gfx_v9_4_3_select_me_pipe_q(struct amdgpu_device *adev,
					u32 me, u32 pipe, u32 q, u32 vm, u32 xcc_id)
{
	soc15_grbm_select(adev, me, pipe, q, vm, GET_INST(GC, xcc_id));
}

static int gfx_v9_4_3_get_xccs_per_xcp(struct amdgpu_device *adev)
{
	u32 xcp_ctl;

	/* Value is expected to be the same on all, fetch from first instance */
	xcp_ctl = RREG32_SOC15(GC, GET_INST(GC, 0), regCP_HYP_XCP_CTL);

	return REG_GET_FIELD(xcp_ctl, CP_HYP_XCP_CTL, NUM_XCC_IN_XCP);
}

static int gfx_v9_4_3_switch_compute_partition(struct amdgpu_device *adev,
						int num_xccs_per_xcp)
{
	int ret, i, num_xcc;
	u32 tmp = 0;

	if (adev->psp.funcs) {
		ret = psp_spatial_partition(&adev->psp,
					    NUM_XCC(adev->gfx.xcc_mask) /
						    num_xccs_per_xcp);
		if (ret)
			return ret;
	} else {
		num_xcc = NUM_XCC(adev->gfx.xcc_mask);

		for (i = 0; i < num_xcc; i++) {
			tmp = REG_SET_FIELD(tmp, CP_HYP_XCP_CTL, NUM_XCC_IN_XCP,
					    num_xccs_per_xcp);
			tmp = REG_SET_FIELD(tmp, CP_HYP_XCP_CTL, VIRTUAL_XCC_ID,
					    i % num_xccs_per_xcp);
			WREG32_SOC15(GC, GET_INST(GC, i), regCP_HYP_XCP_CTL,
				     tmp);
		}
		ret = 0;
	}

	adev->gfx.num_xcc_per_xcp = num_xccs_per_xcp;

	return ret;
}

static int gfx_v9_4_3_ih_to_xcc_inst(struct amdgpu_device *adev, int ih_node)
{
	int xcc;

	xcc = hweight8(adev->gfx.xcc_mask & GENMASK(ih_node / 2, 0));
	if (!xcc) {
		dev_err(adev->dev, "Couldn't find xcc mapping from IH node");
		return -EINVAL;
	}

	return xcc - 1;
}

static const struct amdgpu_gfx_funcs gfx_v9_4_3_gfx_funcs = {
	.get_gpu_clock_counter = &gfx_v9_4_3_get_gpu_clock_counter,
	.select_se_sh = &gfx_v9_4_3_xcc_select_se_sh,
	.read_wave_data = &gfx_v9_4_3_read_wave_data,
	.read_wave_sgprs = &gfx_v9_4_3_read_wave_sgprs,
	.read_wave_vgprs = &gfx_v9_4_3_read_wave_vgprs,
	.select_me_pipe_q = &gfx_v9_4_3_select_me_pipe_q,
	.switch_partition_mode = &gfx_v9_4_3_switch_compute_partition,
	.ih_node_to_logical_xcc = &gfx_v9_4_3_ih_to_xcc_inst,
	.get_xccs_per_xcp = &gfx_v9_4_3_get_xccs_per_xcp,
};

static int gfx_v9_4_3_aca_bank_parser(struct aca_handle *handle,
				      struct aca_bank *bank, enum aca_smu_type type,
				      void *data)
{
	struct aca_bank_info info;
	u64 misc0;
	u32 instlo;
	int ret;

	ret = aca_bank_info_decode(bank, &info);
	if (ret)
		return ret;

	/* NOTE: overwrite info.die_id with xcd id for gfx */
	instlo = ACA_REG__IPID__INSTANCEIDLO(bank->regs[ACA_REG_IDX_IPID]);
	instlo &= GENMASK(31, 1);
	info.die_id = instlo == mmSMNAID_XCD0_MCA_SMU ? 0 : 1;

	misc0 = bank->regs[ACA_REG_IDX_MISC0];

	switch (type) {
	case ACA_SMU_TYPE_UE:
		ret = aca_error_cache_log_bank_error(handle, &info,
						     ACA_ERROR_TYPE_UE, 1ULL);
		break;
	case ACA_SMU_TYPE_CE:
		ret = aca_error_cache_log_bank_error(handle, &info,
						     ACA_ERROR_TYPE_CE, ACA_REG__MISC0__ERRCNT(misc0));
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

static bool gfx_v9_4_3_aca_bank_is_valid(struct aca_handle *handle, struct aca_bank *bank,
					 enum aca_smu_type type, void *data)
{
	u32 instlo;

	instlo = ACA_REG__IPID__INSTANCEIDLO(bank->regs[ACA_REG_IDX_IPID]);
	instlo &= GENMASK(31, 1);
	switch (instlo) {
	case mmSMNAID_XCD0_MCA_SMU:
	case mmSMNAID_XCD1_MCA_SMU:
	case mmSMNXCD_XCD0_MCA_SMU:
		return true;
	default:
		break;
	}

	return false;
}

static const struct aca_bank_ops gfx_v9_4_3_aca_bank_ops = {
	.aca_bank_parser = gfx_v9_4_3_aca_bank_parser,
	.aca_bank_is_valid = gfx_v9_4_3_aca_bank_is_valid,
};

static const struct aca_info gfx_v9_4_3_aca_info = {
	.hwip = ACA_HWIP_TYPE_SMU,
	.mask = ACA_ERROR_UE_MASK | ACA_ERROR_CE_MASK,
	.bank_ops = &gfx_v9_4_3_aca_bank_ops,
};

static int gfx_v9_4_3_gpu_early_init(struct amdgpu_device *adev)
{
	u32 gb_addr_config;

	adev->gfx.funcs = &gfx_v9_4_3_gfx_funcs;
	adev->gfx.ras = &gfx_v9_4_3_ras;

	switch (amdgpu_ip_version(adev, GC_HWIP, 0)) {
	case IP_VERSION(9, 4, 3):
	case IP_VERSION(9, 4, 4):
		adev->gfx.config.max_hw_contexts = 8;
		adev->gfx.config.sc_prim_fifo_size_frontend = 0x20;
		adev->gfx.config.sc_prim_fifo_size_backend = 0x100;
		adev->gfx.config.sc_hiz_tile_fifo_size = 0x30;
		adev->gfx.config.sc_earlyz_tile_fifo_size = 0x4C0;
		gb_addr_config = RREG32_SOC15(GC, GET_INST(GC, 0), regGB_ADDR_CONFIG);
		break;
	default:
		BUG();
		break;
	}

	adev->gfx.config.gb_addr_config = gb_addr_config;

	adev->gfx.config.gb_addr_config_fields.num_pipes = 1 <<
			REG_GET_FIELD(
					adev->gfx.config.gb_addr_config,
					GB_ADDR_CONFIG,
					NUM_PIPES);

	adev->gfx.config.max_tile_pipes =
		adev->gfx.config.gb_addr_config_fields.num_pipes;

	adev->gfx.config.gb_addr_config_fields.num_banks = 1 <<
			REG_GET_FIELD(
					adev->gfx.config.gb_addr_config,
					GB_ADDR_CONFIG,
					NUM_BANKS);
	adev->gfx.config.gb_addr_config_fields.max_compress_frags = 1 <<
			REG_GET_FIELD(
					adev->gfx.config.gb_addr_config,
					GB_ADDR_CONFIG,
					MAX_COMPRESSED_FRAGS);
	adev->gfx.config.gb_addr_config_fields.num_rb_per_se = 1 <<
			REG_GET_FIELD(
					adev->gfx.config.gb_addr_config,
					GB_ADDR_CONFIG,
					NUM_RB_PER_SE);
	adev->gfx.config.gb_addr_config_fields.num_se = 1 <<
			REG_GET_FIELD(
					adev->gfx.config.gb_addr_config,
					GB_ADDR_CONFIG,
					NUM_SHADER_ENGINES);
	adev->gfx.config.gb_addr_config_fields.pipe_interleave_size = 1 << (8 +
			REG_GET_FIELD(
					adev->gfx.config.gb_addr_config,
					GB_ADDR_CONFIG,
					PIPE_INTERLEAVE_SIZE));

	return 0;
}

static int gfx_v9_4_3_compute_ring_init(struct amdgpu_device *adev, int ring_id,
				        int xcc_id, int mec, int pipe, int queue)
{
	unsigned irq_type;
	struct amdgpu_ring *ring = &adev->gfx.compute_ring[ring_id];
	unsigned int hw_prio;
	uint32_t xcc_doorbell_start;

	ring = &adev->gfx.compute_ring[xcc_id * adev->gfx.num_compute_rings +
				       ring_id];

	/* mec0 is me1 */
	ring->xcc_id = xcc_id;
	ring->me = mec + 1;
	ring->pipe = pipe;
	ring->queue = queue;

	ring->ring_obj = NULL;
	ring->use_doorbell = true;
	xcc_doorbell_start = adev->doorbell_index.mec_ring0 +
			     xcc_id * adev->doorbell_index.xcc_doorbell_range;
	ring->doorbell_index = (xcc_doorbell_start + ring_id) << 1;
	ring->eop_gpu_addr = adev->gfx.mec.hpd_eop_gpu_addr +
			     (ring_id + xcc_id * adev->gfx.num_compute_rings) *
				     GFX9_MEC_HPD_SIZE;
	ring->vm_hub = AMDGPU_GFXHUB(xcc_id);
	sprintf(ring->name, "comp_%d.%d.%d.%d",
			ring->xcc_id, ring->me, ring->pipe, ring->queue);

	irq_type = AMDGPU_CP_IRQ_COMPUTE_MEC1_PIPE0_EOP
		+ ((ring->me - 1) * adev->gfx.mec.num_pipe_per_mec)
		+ ring->pipe;
	hw_prio = amdgpu_gfx_is_high_priority_compute_queue(adev, ring) ?
			AMDGPU_GFX_PIPE_PRIO_HIGH : AMDGPU_GFX_PIPE_PRIO_NORMAL;
	/* type-2 packets are deprecated on MEC, use type-3 instead */
	return amdgpu_ring_init(adev, ring, 1024, &adev->gfx.eop_irq, irq_type,
				hw_prio, NULL);
}

static void gfx_v9_4_3_alloc_ip_dump(struct amdgpu_device *adev)
{
	uint32_t reg_count = ARRAY_SIZE(gc_reg_list_9_4_3);
	uint32_t *ptr, num_xcc, inst;

	num_xcc = NUM_XCC(adev->gfx.xcc_mask);

	ptr = kcalloc(reg_count * num_xcc, sizeof(uint32_t), GFP_KERNEL);
	if (!ptr) {
		DRM_ERROR("Failed to allocate memory for GFX IP Dump\n");
		adev->gfx.ip_dump_core = NULL;
	} else {
		adev->gfx.ip_dump_core = ptr;
	}

	/* Allocate memory for compute queue registers for all the instances */
	reg_count = ARRAY_SIZE(gc_cp_reg_list_9_4_3);
	inst = adev->gfx.mec.num_mec * adev->gfx.mec.num_pipe_per_mec *
		adev->gfx.mec.num_queue_per_pipe;

	ptr = kcalloc(reg_count * inst * num_xcc, sizeof(uint32_t), GFP_KERNEL);
	if (!ptr) {
		DRM_ERROR("Failed to allocate memory for Compute Queues IP Dump\n");
		adev->gfx.ip_dump_compute_queues = NULL;
	} else {
		adev->gfx.ip_dump_compute_queues = ptr;
	}
}

static int gfx_v9_4_3_sw_init(struct amdgpu_ip_block *ip_block)
{
	int i, j, k, r, ring_id, xcc_id, num_xcc;
	struct amdgpu_device *adev = ip_block->adev;

	switch (amdgpu_ip_version(adev, GC_HWIP, 0)) {
	case IP_VERSION(9, 4, 3):
	case IP_VERSION(9, 4, 4):
		adev->gfx.cleaner_shader_ptr = gfx_9_4_3_cleaner_shader_hex;
		adev->gfx.cleaner_shader_size = sizeof(gfx_9_4_3_cleaner_shader_hex);
		if (adev->gfx.mec_fw_version >= 153) {
			adev->gfx.enable_cleaner_shader = true;
			r = amdgpu_gfx_cleaner_shader_sw_init(adev, adev->gfx.cleaner_shader_size);
			if (r) {
				adev->gfx.enable_cleaner_shader = false;
				dev_err(adev->dev, "Failed to initialize cleaner shader\n");
			}
		}
		break;
	default:
		adev->gfx.enable_cleaner_shader = false;
		break;
	}

	adev->gfx.mec.num_mec = 2;
	adev->gfx.mec.num_pipe_per_mec = 4;
	adev->gfx.mec.num_queue_per_pipe = 8;

	num_xcc = NUM_XCC(adev->gfx.xcc_mask);

	/* EOP Event */
	r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_GRBM_CP, GFX_9_0__SRCID__CP_EOP_INTERRUPT, &adev->gfx.eop_irq);
	if (r)
		return r;

	/* Bad opcode Event */
	r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_GRBM_CP,
			      GFX_9_0__SRCID__CP_BAD_OPCODE_ERROR,
			      &adev->gfx.bad_op_irq);
	if (r)
		return r;

	/* Privileged reg */
	r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_GRBM_CP, GFX_9_0__SRCID__CP_PRIV_REG_FAULT,
			      &adev->gfx.priv_reg_irq);
	if (r)
		return r;

	/* Privileged inst */
	r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_GRBM_CP, GFX_9_0__SRCID__CP_PRIV_INSTR_FAULT,
			      &adev->gfx.priv_inst_irq);
	if (r)
		return r;

	adev->gfx.gfx_current_status = AMDGPU_GFX_NORMAL_MODE;

	r = adev->gfx.rlc.funcs->init(adev);
	if (r) {
		DRM_ERROR("Failed to init rlc BOs!\n");
		return r;
	}

	r = gfx_v9_4_3_mec_init(adev);
	if (r) {
		DRM_ERROR("Failed to init MEC BOs!\n");
		return r;
	}

	/* set up the compute queues - allocate horizontally across pipes */
	for (xcc_id = 0; xcc_id < num_xcc; xcc_id++) {
		ring_id = 0;
		for (i = 0; i < adev->gfx.mec.num_mec; ++i) {
			for (j = 0; j < adev->gfx.mec.num_queue_per_pipe; j++) {
				for (k = 0; k < adev->gfx.mec.num_pipe_per_mec;
				     k++) {
					if (!amdgpu_gfx_is_mec_queue_enabled(
							adev, xcc_id, i, k, j))
						continue;

					r = gfx_v9_4_3_compute_ring_init(adev,
								       ring_id,
								       xcc_id,
								       i, k, j);
					if (r)
						return r;

					ring_id++;
				}
			}
		}

		r = amdgpu_gfx_kiq_init(adev, GFX9_MEC_HPD_SIZE, xcc_id);
		if (r) {
			DRM_ERROR("Failed to init KIQ BOs!\n");
			return r;
		}

		r = amdgpu_gfx_kiq_init_ring(adev, xcc_id);
		if (r)
			return r;

		/* create MQD for all compute queues as wel as KIQ for SRIOV case */
		r = amdgpu_gfx_mqd_sw_init(adev,
				sizeof(struct v9_mqd_allocation), xcc_id);
		if (r)
			return r;
	}

	adev->gfx.compute_supported_reset =
		amdgpu_get_soft_full_reset_mask(&adev->gfx.compute_ring[0]);
	switch (amdgpu_ip_version(adev, GC_HWIP, 0)) {
	case IP_VERSION(9, 4, 3):
	case IP_VERSION(9, 4, 4):
		if (adev->gfx.mec_fw_version >= 155) {
			adev->gfx.compute_supported_reset |= AMDGPU_RESET_TYPE_PER_QUEUE;
			adev->gfx.compute_supported_reset |= AMDGPU_RESET_TYPE_PER_PIPE;
		}
		break;
	default:
		break;
	}
	r = gfx_v9_4_3_gpu_early_init(adev);
	if (r)
		return r;

	r = amdgpu_gfx_ras_sw_init(adev);
	if (r)
		return r;

	r = amdgpu_gfx_sysfs_init(adev);
	if (r)
		return r;

	gfx_v9_4_3_alloc_ip_dump(adev);

	return 0;
}

static int gfx_v9_4_3_sw_fini(struct amdgpu_ip_block *ip_block)
{
	int i, num_xcc;
	struct amdgpu_device *adev = ip_block->adev;

	num_xcc = NUM_XCC(adev->gfx.xcc_mask);
	for (i = 0; i < adev->gfx.num_compute_rings * num_xcc; i++)
		amdgpu_ring_fini(&adev->gfx.compute_ring[i]);

	for (i = 0; i < num_xcc; i++) {
		amdgpu_gfx_mqd_sw_fini(adev, i);
		amdgpu_gfx_kiq_free_ring(&adev->gfx.kiq[i].ring);
		amdgpu_gfx_kiq_fini(adev, i);
	}

	amdgpu_gfx_cleaner_shader_sw_fini(adev);

	gfx_v9_4_3_mec_fini(adev);
	amdgpu_bo_unref(&adev->gfx.rlc.clear_state_obj);
	gfx_v9_4_3_free_microcode(adev);
	amdgpu_gfx_sysfs_fini(adev);

	kfree(adev->gfx.ip_dump_core);
	kfree(adev->gfx.ip_dump_compute_queues);

	return 0;
}

#define DEFAULT_SH_MEM_BASES	(0x6000)
static void gfx_v9_4_3_xcc_init_compute_vmid(struct amdgpu_device *adev,
					     int xcc_id)
{
	int i;
	uint32_t sh_mem_config;
	uint32_t sh_mem_bases;
	uint32_t data;

	/*
	 * Configure apertures:
	 * LDS:         0x60000000'00000000 - 0x60000001'00000000 (4GB)
	 * Scratch:     0x60000001'00000000 - 0x60000002'00000000 (4GB)
	 * GPUVM:       0x60010000'00000000 - 0x60020000'00000000 (1TB)
	 */
	sh_mem_bases = DEFAULT_SH_MEM_BASES | (DEFAULT_SH_MEM_BASES << 16);

	sh_mem_config = SH_MEM_ADDRESS_MODE_64 |
			SH_MEM_ALIGNMENT_MODE_UNALIGNED <<
			SH_MEM_CONFIG__ALIGNMENT_MODE__SHIFT;

	mutex_lock(&adev->srbm_mutex);
	for (i = adev->vm_manager.first_kfd_vmid; i < AMDGPU_NUM_VMID; i++) {
		soc15_grbm_select(adev, 0, 0, 0, i, GET_INST(GC, xcc_id));
		/* CP and shaders */
		WREG32_SOC15_RLC(GC, GET_INST(GC, xcc_id), regSH_MEM_CONFIG, sh_mem_config);
		WREG32_SOC15_RLC(GC, GET_INST(GC, xcc_id), regSH_MEM_BASES, sh_mem_bases);

		/* Enable trap for each kfd vmid. */
		data = RREG32_SOC15(GC, GET_INST(GC, xcc_id), regSPI_GDBG_PER_VMID_CNTL);
		data = REG_SET_FIELD(data, SPI_GDBG_PER_VMID_CNTL, TRAP_EN, 1);
		WREG32_SOC15_RLC(GC, GET_INST(GC, xcc_id), regSPI_GDBG_PER_VMID_CNTL, data);
	}
	soc15_grbm_select(adev, 0, 0, 0, 0, GET_INST(GC, xcc_id));
	mutex_unlock(&adev->srbm_mutex);

	/*
	 * Initialize all compute VMIDs to have no GDS, GWS, or OA
	 * access. These should be enabled by FW for target VMIDs.
	 */
	for (i = adev->vm_manager.first_kfd_vmid; i < AMDGPU_NUM_VMID; i++) {
		WREG32_SOC15_OFFSET(GC, GET_INST(GC, xcc_id), regGDS_VMID0_BASE, 2 * i, 0);
		WREG32_SOC15_OFFSET(GC, GET_INST(GC, xcc_id), regGDS_VMID0_SIZE, 2 * i, 0);
		WREG32_SOC15_OFFSET(GC, GET_INST(GC, xcc_id), regGDS_GWS_VMID0, i, 0);
		WREG32_SOC15_OFFSET(GC, GET_INST(GC, xcc_id), regGDS_OA_VMID0, i, 0);
	}
}

static void gfx_v9_4_3_xcc_init_gds_vmid(struct amdgpu_device *adev, int xcc_id)
{
	int vmid;

	/*
	 * Initialize all compute and user-gfx VMIDs to have no GDS, GWS, or OA
	 * access. Compute VMIDs should be enabled by FW for target VMIDs,
	 * the driver can enable them for graphics. VMID0 should maintain
	 * access so that HWS firmware can save/restore entries.
	 */
	for (vmid = 1; vmid < AMDGPU_NUM_VMID; vmid++) {
		WREG32_SOC15_OFFSET(GC, GET_INST(GC, xcc_id), regGDS_VMID0_BASE, 2 * vmid, 0);
		WREG32_SOC15_OFFSET(GC, GET_INST(GC, xcc_id), regGDS_VMID0_SIZE, 2 * vmid, 0);
		WREG32_SOC15_OFFSET(GC, GET_INST(GC, xcc_id), regGDS_GWS_VMID0, vmid, 0);
		WREG32_SOC15_OFFSET(GC, GET_INST(GC, xcc_id), regGDS_OA_VMID0, vmid, 0);
	}
}

static void gfx_v9_4_3_xcc_constants_init(struct amdgpu_device *adev,
					  int xcc_id)
{
	u32 tmp;
	int i;

	/* XXX SH_MEM regs */
	/* where to put LDS, scratch, GPUVM in FSA64 space */
	mutex_lock(&adev->srbm_mutex);
	for (i = 0; i < adev->vm_manager.id_mgr[AMDGPU_GFXHUB(0)].num_ids; i++) {
		soc15_grbm_select(adev, 0, 0, 0, i, GET_INST(GC, xcc_id));
		/* CP and shaders */
		if (i == 0) {
			tmp = REG_SET_FIELD(0, SH_MEM_CONFIG, ALIGNMENT_MODE,
					    SH_MEM_ALIGNMENT_MODE_UNALIGNED);
			tmp = REG_SET_FIELD(tmp, SH_MEM_CONFIG, RETRY_DISABLE,
					    !!adev->gmc.noretry);
			WREG32_SOC15_RLC(GC, GET_INST(GC, xcc_id),
					 regSH_MEM_CONFIG, tmp);
			WREG32_SOC15_RLC(GC, GET_INST(GC, xcc_id),
					 regSH_MEM_BASES, 0);
		} else {
			tmp = REG_SET_FIELD(0, SH_MEM_CONFIG, ALIGNMENT_MODE,
					    SH_MEM_ALIGNMENT_MODE_UNALIGNED);
			tmp = REG_SET_FIELD(tmp, SH_MEM_CONFIG, RETRY_DISABLE,
					    !!adev->gmc.noretry);
			WREG32_SOC15_RLC(GC, GET_INST(GC, xcc_id),
					 regSH_MEM_CONFIG, tmp);
			tmp = REG_SET_FIELD(0, SH_MEM_BASES, PRIVATE_BASE,
					    (adev->gmc.private_aperture_start >>
					     48));
			tmp = REG_SET_FIELD(tmp, SH_MEM_BASES, SHARED_BASE,
					    (adev->gmc.shared_aperture_start >>
					     48));
			WREG32_SOC15_RLC(GC, GET_INST(GC, xcc_id),
					 regSH_MEM_BASES, tmp);
		}
	}
	soc15_grbm_select(adev, 0, 0, 0, 0, GET_INST(GC, 0));

	mutex_unlock(&adev->srbm_mutex);

	gfx_v9_4_3_xcc_init_compute_vmid(adev, xcc_id);
	gfx_v9_4_3_xcc_init_gds_vmid(adev, xcc_id);
}

static void gfx_v9_4_3_constants_init(struct amdgpu_device *adev)
{
	int i, num_xcc;

	num_xcc = NUM_XCC(adev->gfx.xcc_mask);

	gfx_v9_4_3_get_cu_info(adev, &adev->gfx.cu_info);
	adev->gfx.config.db_debug2 =
		RREG32_SOC15(GC, GET_INST(GC, 0), regDB_DEBUG2);

	for (i = 0; i < num_xcc; i++)
		gfx_v9_4_3_xcc_constants_init(adev, i);
}

static void
gfx_v9_4_3_xcc_enable_save_restore_machine(struct amdgpu_device *adev,
					   int xcc_id)
{
	WREG32_FIELD15_PREREG(GC, GET_INST(GC, xcc_id), RLC_SRM_CNTL, SRM_ENABLE, 1);
}

static void gfx_v9_4_3_xcc_init_pg(struct amdgpu_device *adev, int xcc_id)
{
	/*
	 * Rlc save restore list is workable since v2_1.
	 * And it's needed by gfxoff feature.
	 */
	if (adev->gfx.rlc.is_rlc_v2_1)
		gfx_v9_4_3_xcc_enable_save_restore_machine(adev, xcc_id);
}

static void gfx_v9_4_3_xcc_disable_gpa_mode(struct amdgpu_device *adev, int xcc_id)
{
	uint32_t data;

	data = RREG32_SOC15(GC, GET_INST(GC, xcc_id), regCPC_PSP_DEBUG);
	data |= CPC_PSP_DEBUG__UTCL2IUGPAOVERRIDE_MASK;
	WREG32_SOC15(GC, GET_INST(GC, xcc_id), regCPC_PSP_DEBUG, data);
}

static bool gfx_v9_4_3_is_rlc_enabled(struct amdgpu_device *adev)
{
	uint32_t rlc_setting;

	/* if RLC is not enabled, do nothing */
	rlc_setting = RREG32_SOC15(GC, GET_INST(GC, 0), regRLC_CNTL);
	if (!(rlc_setting & RLC_CNTL__RLC_ENABLE_F32_MASK))
		return false;

	return true;
}

static void gfx_v9_4_3_xcc_set_safe_mode(struct amdgpu_device *adev, int xcc_id)
{
	uint32_t data;
	unsigned i;

	data = RLC_SAFE_MODE__CMD_MASK;
	data |= (1 << RLC_SAFE_MODE__MESSAGE__SHIFT);
	WREG32_SOC15(GC, GET_INST(GC, xcc_id), regRLC_SAFE_MODE, data);

	/* wait for RLC_SAFE_MODE */
	for (i = 0; i < adev->usec_timeout; i++) {
		if (!REG_GET_FIELD(RREG32_SOC15(GC, GET_INST(GC, xcc_id), regRLC_SAFE_MODE), RLC_SAFE_MODE, CMD))
			break;
		udelay(1);
	}
}

static void gfx_v9_4_3_xcc_unset_safe_mode(struct amdgpu_device *adev,
					   int xcc_id)
{
	uint32_t data;

	data = RLC_SAFE_MODE__CMD_MASK;
	WREG32_SOC15(GC, GET_INST(GC, xcc_id), regRLC_SAFE_MODE, data);
}

static void gfx_v9_4_3_init_rlcg_reg_access_ctrl(struct amdgpu_device *adev)
{
	int xcc_id, num_xcc;
	struct amdgpu_rlcg_reg_access_ctrl *reg_access_ctrl;

	num_xcc = NUM_XCC(adev->gfx.xcc_mask);
	for (xcc_id = 0; xcc_id < num_xcc; xcc_id++) {
		reg_access_ctrl = &adev->gfx.rlc.reg_access_ctrl[GET_INST(GC, xcc_id)];
		reg_access_ctrl->scratch_reg0 = SOC15_REG_OFFSET(GC, GET_INST(GC, xcc_id), regSCRATCH_REG0);
		reg_access_ctrl->scratch_reg1 = SOC15_REG_OFFSET(GC, GET_INST(GC, xcc_id), regSCRATCH_REG1);
		reg_access_ctrl->scratch_reg2 = SOC15_REG_OFFSET(GC, GET_INST(GC, xcc_id), regSCRATCH_REG2);
		reg_access_ctrl->scratch_reg3 = SOC15_REG_OFFSET(GC, GET_INST(GC, xcc_id), regSCRATCH_REG3);
		reg_access_ctrl->grbm_cntl = SOC15_REG_OFFSET(GC, GET_INST(GC, xcc_id), regGRBM_GFX_CNTL);
		reg_access_ctrl->grbm_idx = SOC15_REG_OFFSET(GC, GET_INST(GC, xcc_id), regGRBM_GFX_INDEX);
		reg_access_ctrl->spare_int = SOC15_REG_OFFSET(GC, GET_INST(GC, xcc_id), regRLC_SPARE_INT);
	}
	adev->gfx.rlc.rlcg_reg_access_supported = true;
}

static int gfx_v9_4_3_rlc_init(struct amdgpu_device *adev)
{
	/* init spm vmid with 0xf */
	if (adev->gfx.rlc.funcs->update_spm_vmid)
		adev->gfx.rlc.funcs->update_spm_vmid(adev, NULL, 0xf);

	return 0;
}

static void gfx_v9_4_3_xcc_wait_for_rlc_serdes(struct amdgpu_device *adev,
					       int xcc_id)
{
	u32 i, j, k;
	u32 mask;

	mutex_lock(&adev->grbm_idx_mutex);
	for (i = 0; i < adev->gfx.config.max_shader_engines; i++) {
		for (j = 0; j < adev->gfx.config.max_sh_per_se; j++) {
			gfx_v9_4_3_xcc_select_se_sh(adev, i, j, 0xffffffff,
						    xcc_id);
			for (k = 0; k < adev->usec_timeout; k++) {
				if (RREG32_SOC15(GC, GET_INST(GC, xcc_id), regRLC_SERDES_CU_MASTER_BUSY) == 0)
					break;
				udelay(1);
			}
			if (k == adev->usec_timeout) {
				gfx_v9_4_3_xcc_select_se_sh(adev, 0xffffffff,
							    0xffffffff,
							    0xffffffff, xcc_id);
				mutex_unlock(&adev->grbm_idx_mutex);
				DRM_INFO("Timeout wait for RLC serdes %u,%u\n",
					 i, j);
				return;
			}
		}
	}
	gfx_v9_4_3_xcc_select_se_sh(adev, 0xffffffff, 0xffffffff, 0xffffffff,
				    xcc_id);
	mutex_unlock(&adev->grbm_idx_mutex);

	mask = RLC_SERDES_NONCU_MASTER_BUSY__SE_MASTER_BUSY_MASK |
		RLC_SERDES_NONCU_MASTER_BUSY__GC_MASTER_BUSY_MASK |
		RLC_SERDES_NONCU_MASTER_BUSY__TC0_MASTER_BUSY_MASK |
		RLC_SERDES_NONCU_MASTER_BUSY__TC1_MASTER_BUSY_MASK;
	for (k = 0; k < adev->usec_timeout; k++) {
		if ((RREG32_SOC15(GC, GET_INST(GC, xcc_id), regRLC_SERDES_NONCU_MASTER_BUSY) & mask) == 0)
			break;
		udelay(1);
	}
}

static void gfx_v9_4_3_xcc_enable_gui_idle_interrupt(struct amdgpu_device *adev,
						     bool enable, int xcc_id)
{
	u32 tmp;

	/* These interrupts should be enabled to drive DS clock */

	tmp = RREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_INT_CNTL_RING0);

	tmp = REG_SET_FIELD(tmp, CP_INT_CNTL_RING0, CNTX_BUSY_INT_ENABLE, enable ? 1 : 0);
	tmp = REG_SET_FIELD(tmp, CP_INT_CNTL_RING0, CNTX_EMPTY_INT_ENABLE, enable ? 1 : 0);
	tmp = REG_SET_FIELD(tmp, CP_INT_CNTL_RING0, CMP_BUSY_INT_ENABLE, enable ? 1 : 0);

	WREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_INT_CNTL_RING0, tmp);
}

static void gfx_v9_4_3_xcc_rlc_stop(struct amdgpu_device *adev, int xcc_id)
{
	WREG32_FIELD15_PREREG(GC, GET_INST(GC, xcc_id), RLC_CNTL,
			      RLC_ENABLE_F32, 0);
	gfx_v9_4_3_xcc_enable_gui_idle_interrupt(adev, false, xcc_id);
	gfx_v9_4_3_xcc_wait_for_rlc_serdes(adev, xcc_id);
}

static void gfx_v9_4_3_rlc_stop(struct amdgpu_device *adev)
{
	int i, num_xcc;

	num_xcc = NUM_XCC(adev->gfx.xcc_mask);
	for (i = 0; i < num_xcc; i++)
		gfx_v9_4_3_xcc_rlc_stop(adev, i);
}

static void gfx_v9_4_3_xcc_rlc_reset(struct amdgpu_device *adev, int xcc_id)
{
	WREG32_FIELD15_PREREG(GC, GET_INST(GC, xcc_id), GRBM_SOFT_RESET,
			      SOFT_RESET_RLC, 1);
	udelay(50);
	WREG32_FIELD15_PREREG(GC, GET_INST(GC, xcc_id), GRBM_SOFT_RESET,
			      SOFT_RESET_RLC, 0);
	udelay(50);
}

static void gfx_v9_4_3_rlc_reset(struct amdgpu_device *adev)
{
	int i, num_xcc;

	num_xcc = NUM_XCC(adev->gfx.xcc_mask);
	for (i = 0; i < num_xcc; i++)
		gfx_v9_4_3_xcc_rlc_reset(adev, i);
}

static void gfx_v9_4_3_xcc_rlc_start(struct amdgpu_device *adev, int xcc_id)
{
	WREG32_FIELD15_PREREG(GC, GET_INST(GC, xcc_id), RLC_CNTL,
			      RLC_ENABLE_F32, 1);
	udelay(50);

	/* carrizo do enable cp interrupt after cp inited */
	if (!(adev->flags & AMD_IS_APU)) {
		gfx_v9_4_3_xcc_enable_gui_idle_interrupt(adev, true, xcc_id);
		udelay(50);
	}
}

static void gfx_v9_4_3_rlc_start(struct amdgpu_device *adev)
{
#ifdef AMDGPU_RLC_DEBUG_RETRY
	u32 rlc_ucode_ver;
#endif
	int i, num_xcc;

	num_xcc = NUM_XCC(adev->gfx.xcc_mask);
	for (i = 0; i < num_xcc; i++) {
		gfx_v9_4_3_xcc_rlc_start(adev, i);
#ifdef AMDGPU_RLC_DEBUG_RETRY
		/* RLC_GPM_GENERAL_6 : RLC Ucode version */
		rlc_ucode_ver = RREG32_SOC15(GC, GET_INST(GC, i), regRLC_GPM_GENERAL_6);
		if (rlc_ucode_ver == 0x108) {
			dev_info(adev->dev,
				 "Using rlc debug ucode. regRLC_GPM_GENERAL_6 ==0x08%x / fw_ver == %i \n",
				 rlc_ucode_ver, adev->gfx.rlc_fw_version);
			/* RLC_GPM_TIMER_INT_3 : Timer interval in RefCLK cycles,
			 * default is 0x9C4 to create a 100us interval */
			WREG32_SOC15(GC, GET_INST(GC, i), regRLC_GPM_TIMER_INT_3, 0x9C4);
			/* RLC_GPM_GENERAL_12 : Minimum gap between wptr and rptr
			 * to disable the page fault retry interrupts, default is
			 * 0x100 (256) */
			WREG32_SOC15(GC, GET_INST(GC, i), regRLC_GPM_GENERAL_12, 0x100);
		}
#endif
	}
}

static int gfx_v9_4_3_xcc_rlc_load_microcode(struct amdgpu_device *adev,
					     int xcc_id)
{
	const struct rlc_firmware_header_v2_0 *hdr;
	const __le32 *fw_data;
	unsigned i, fw_size;

	if (!adev->gfx.rlc_fw)
		return -EINVAL;

	hdr = (const struct rlc_firmware_header_v2_0 *)adev->gfx.rlc_fw->data;
	amdgpu_ucode_print_rlc_hdr(&hdr->header);

	fw_data = (const __le32 *)(adev->gfx.rlc_fw->data +
			   le32_to_cpu(hdr->header.ucode_array_offset_bytes));
	fw_size = le32_to_cpu(hdr->header.ucode_size_bytes) / 4;

	WREG32_SOC15(GC, GET_INST(GC, xcc_id), regRLC_GPM_UCODE_ADDR,
			RLCG_UCODE_LOADING_START_ADDRESS);
	for (i = 0; i < fw_size; i++) {
		if (amdgpu_emu_mode == 1 && i % 100 == 0) {
			dev_info(adev->dev, "Write RLC ucode data %u DWs\n", i);
			msleep(1);
		}
		WREG32_SOC15(GC, GET_INST(GC, xcc_id), regRLC_GPM_UCODE_DATA, le32_to_cpup(fw_data++));
	}
	WREG32_SOC15(GC, GET_INST(GC, xcc_id), regRLC_GPM_UCODE_ADDR, adev->gfx.rlc_fw_version);

	return 0;
}

static int gfx_v9_4_3_xcc_rlc_resume(struct amdgpu_device *adev, int xcc_id)
{
	int r;

	if (adev->firmware.load_type != AMDGPU_FW_LOAD_PSP) {
		gfx_v9_4_3_xcc_rlc_stop(adev, xcc_id);
		/* legacy rlc firmware loading */
		r = gfx_v9_4_3_xcc_rlc_load_microcode(adev, xcc_id);
		if (r)
			return r;
		gfx_v9_4_3_xcc_rlc_start(adev, xcc_id);
	}

	amdgpu_gfx_rlc_enter_safe_mode(adev, xcc_id);
	/* disable CG */
	WREG32_SOC15(GC, GET_INST(GC, xcc_id), regRLC_CGCG_CGLS_CTRL, 0);
	gfx_v9_4_3_xcc_init_pg(adev, xcc_id);
	amdgpu_gfx_rlc_exit_safe_mode(adev, xcc_id);

	return 0;
}

static int gfx_v9_4_3_rlc_resume(struct amdgpu_device *adev)
{
	int r, i, num_xcc;

	if (amdgpu_sriov_vf(adev))
		return 0;

	num_xcc = NUM_XCC(adev->gfx.xcc_mask);
	for (i = 0; i < num_xcc; i++) {
		r = gfx_v9_4_3_xcc_rlc_resume(adev, i);
		if (r)
			return r;
	}

	return 0;
}

static void gfx_v9_4_3_update_spm_vmid(struct amdgpu_device *adev, struct amdgpu_ring *ring,
				       unsigned vmid)
{
	u32 reg, pre_data, data;

	reg = SOC15_REG_OFFSET(GC, GET_INST(GC, 0), regRLC_SPM_MC_CNTL);
	if (amdgpu_sriov_is_pp_one_vf(adev) && !amdgpu_sriov_runtime(adev))
		pre_data = RREG32_NO_KIQ(reg);
	else
		pre_data = RREG32(reg);

	data =	pre_data & (~RLC_SPM_MC_CNTL__RLC_SPM_VMID_MASK);
	data |= (vmid & RLC_SPM_MC_CNTL__RLC_SPM_VMID_MASK) << RLC_SPM_MC_CNTL__RLC_SPM_VMID__SHIFT;

	if (pre_data != data) {
		if (amdgpu_sriov_is_pp_one_vf(adev) && !amdgpu_sriov_runtime(adev)) {
			WREG32_SOC15_NO_KIQ(GC, GET_INST(GC, 0), regRLC_SPM_MC_CNTL, data);
		} else
			WREG32_SOC15(GC, GET_INST(GC, 0), regRLC_SPM_MC_CNTL, data);
	}
}

static const struct soc15_reg_rlcg rlcg_access_gc_9_4_3[] = {
	{SOC15_REG_ENTRY(GC, 0, regGRBM_GFX_INDEX)},
	{SOC15_REG_ENTRY(GC, 0, regSQ_IND_INDEX)},
};

static bool gfx_v9_4_3_check_rlcg_range(struct amdgpu_device *adev,
					uint32_t offset,
					struct soc15_reg_rlcg *entries, int arr_size)
{
	int i, inst;
	uint32_t reg;

	if (!entries)
		return false;

	for (i = 0; i < arr_size; i++) {
		const struct soc15_reg_rlcg *entry;

		entry = &entries[i];
		inst = adev->ip_map.logical_to_dev_inst ?
			       adev->ip_map.logical_to_dev_inst(
				       adev, entry->hwip, entry->instance) :
			       entry->instance;
		reg = adev->reg_offset[entry->hwip][inst][entry->segment] +
		      entry->reg;
		if (offset == reg)
			return true;
	}

	return false;
}

static bool gfx_v9_4_3_is_rlcg_access_range(struct amdgpu_device *adev, u32 offset)
{
	return gfx_v9_4_3_check_rlcg_range(adev, offset,
					(void *)rlcg_access_gc_9_4_3,
					ARRAY_SIZE(rlcg_access_gc_9_4_3));
}

static void gfx_v9_4_3_xcc_cp_compute_enable(struct amdgpu_device *adev,
					     bool enable, int xcc_id)
{
	if (enable) {
		WREG32_SOC15_RLC(GC, GET_INST(GC, xcc_id), regCP_MEC_CNTL, 0);
	} else {
		WREG32_SOC15_RLC(GC, GET_INST(GC, xcc_id), regCP_MEC_CNTL,
			(CP_MEC_CNTL__MEC_INVALIDATE_ICACHE_MASK |
			 CP_MEC_CNTL__MEC_ME1_PIPE0_RESET_MASK |
			 CP_MEC_CNTL__MEC_ME1_PIPE1_RESET_MASK |
			 CP_MEC_CNTL__MEC_ME1_PIPE2_RESET_MASK |
			 CP_MEC_CNTL__MEC_ME1_PIPE3_RESET_MASK |
			 CP_MEC_CNTL__MEC_ME2_PIPE0_RESET_MASK |
			 CP_MEC_CNTL__MEC_ME2_PIPE1_RESET_MASK |
			 CP_MEC_CNTL__MEC_ME1_HALT_MASK |
			 CP_MEC_CNTL__MEC_ME2_HALT_MASK));
		adev->gfx.kiq[xcc_id].ring.sched.ready = false;
	}
	udelay(50);
}

static int gfx_v9_4_3_xcc_cp_compute_load_microcode(struct amdgpu_device *adev,
						    int xcc_id)
{
	const struct gfx_firmware_header_v1_0 *mec_hdr;
	const __le32 *fw_data;
	unsigned i;
	u32 tmp;
	u32 mec_ucode_addr_offset;
	u32 mec_ucode_data_offset;

	if (!adev->gfx.mec_fw)
		return -EINVAL;

	gfx_v9_4_3_xcc_cp_compute_enable(adev, false, xcc_id);

	mec_hdr = (const struct gfx_firmware_header_v1_0 *)adev->gfx.mec_fw->data;
	amdgpu_ucode_print_gfx_hdr(&mec_hdr->header);

	fw_data = (const __le32 *)
		(adev->gfx.mec_fw->data +
		 le32_to_cpu(mec_hdr->header.ucode_array_offset_bytes));
	tmp = 0;
	tmp = REG_SET_FIELD(tmp, CP_CPC_IC_BASE_CNTL, VMID, 0);
	tmp = REG_SET_FIELD(tmp, CP_CPC_IC_BASE_CNTL, CACHE_POLICY, 0);
	WREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_CPC_IC_BASE_CNTL, tmp);

	WREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_CPC_IC_BASE_LO,
		adev->gfx.mec.mec_fw_gpu_addr & 0xFFFFF000);
	WREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_CPC_IC_BASE_HI,
		upper_32_bits(adev->gfx.mec.mec_fw_gpu_addr));

	mec_ucode_addr_offset =
		SOC15_REG_OFFSET(GC, GET_INST(GC, xcc_id), regCP_MEC_ME1_UCODE_ADDR);
	mec_ucode_data_offset =
		SOC15_REG_OFFSET(GC, GET_INST(GC, xcc_id), regCP_MEC_ME1_UCODE_DATA);

	/* MEC1 */
	WREG32(mec_ucode_addr_offset, mec_hdr->jt_offset);
	for (i = 0; i < mec_hdr->jt_size; i++)
		WREG32(mec_ucode_data_offset,
		       le32_to_cpup(fw_data + mec_hdr->jt_offset + i));

	WREG32(mec_ucode_addr_offset, adev->gfx.mec_fw_version);
	/* Todo : Loading MEC2 firmware is only necessary if MEC2 should run different microcode than MEC1. */

	return 0;
}

/* KIQ functions */
static void gfx_v9_4_3_xcc_kiq_setting(struct amdgpu_ring *ring, int xcc_id)
{
	uint32_t tmp;
	struct amdgpu_device *adev = ring->adev;

	/* tell RLC which is KIQ queue */
	tmp = RREG32_SOC15(GC, GET_INST(GC, xcc_id), regRLC_CP_SCHEDULERS);
	tmp &= 0xffffff00;
	tmp |= (ring->me << 5) | (ring->pipe << 3) | (ring->queue);
	WREG32_SOC15_RLC(GC, GET_INST(GC, xcc_id), regRLC_CP_SCHEDULERS, tmp | 0x80);
}

static void gfx_v9_4_3_mqd_set_priority(struct amdgpu_ring *ring, struct v9_mqd *mqd)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring->funcs->type == AMDGPU_RING_TYPE_COMPUTE) {
		if (amdgpu_gfx_is_high_priority_compute_queue(adev, ring)) {
			mqd->cp_hqd_pipe_priority = AMDGPU_GFX_PIPE_PRIO_HIGH;
			mqd->cp_hqd_queue_priority =
				AMDGPU_GFX_QUEUE_PRIORITY_MAXIMUM;
		}
	}
}

static int gfx_v9_4_3_xcc_mqd_init(struct amdgpu_ring *ring, int xcc_id)
{
	struct amdgpu_device *adev = ring->adev;
	struct v9_mqd *mqd = ring->mqd_ptr;
	uint64_t hqd_gpu_addr, wb_gpu_addr, eop_base_addr;
	uint32_t tmp;

	mqd->header = 0xC0310800;
	mqd->compute_pipelinestat_enable = 0x00000001;
	mqd->compute_static_thread_mgmt_se0 = 0xffffffff;
	mqd->compute_static_thread_mgmt_se1 = 0xffffffff;
	mqd->compute_static_thread_mgmt_se2 = 0xffffffff;
	mqd->compute_static_thread_mgmt_se3 = 0xffffffff;
	mqd->compute_misc_reserved = 0x00000003;

	mqd->dynamic_cu_mask_addr_lo =
		lower_32_bits(ring->mqd_gpu_addr
			      + offsetof(struct v9_mqd_allocation, dynamic_cu_mask));
	mqd->dynamic_cu_mask_addr_hi =
		upper_32_bits(ring->mqd_gpu_addr
			      + offsetof(struct v9_mqd_allocation, dynamic_cu_mask));

	eop_base_addr = ring->eop_gpu_addr >> 8;
	mqd->cp_hqd_eop_base_addr_lo = eop_base_addr;
	mqd->cp_hqd_eop_base_addr_hi = upper_32_bits(eop_base_addr);

	/* set the EOP size, register value is 2^(EOP_SIZE+1) dwords */
	tmp = RREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_HQD_EOP_CONTROL);
	tmp = REG_SET_FIELD(tmp, CP_HQD_EOP_CONTROL, EOP_SIZE,
			(order_base_2(GFX9_MEC_HPD_SIZE / 4) - 1));

	mqd->cp_hqd_eop_control = tmp;

	/* enable doorbell? */
	tmp = RREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_HQD_PQ_DOORBELL_CONTROL);

	if (ring->use_doorbell) {
		tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_DOORBELL_CONTROL,
				    DOORBELL_OFFSET, ring->doorbell_index);
		tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_DOORBELL_CONTROL,
				    DOORBELL_EN, 1);
		tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_DOORBELL_CONTROL,
				    DOORBELL_SOURCE, 0);
		tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_DOORBELL_CONTROL,
				    DOORBELL_HIT, 0);
		if (amdgpu_sriov_vf(adev))
			tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_DOORBELL_CONTROL,
					    DOORBELL_MODE, 1);
	} else {
		tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_DOORBELL_CONTROL,
					 DOORBELL_EN, 0);
	}

	mqd->cp_hqd_pq_doorbell_control = tmp;

	/* disable the queue if it's active */
	ring->wptr = 0;
	mqd->cp_hqd_dequeue_request = 0;
	mqd->cp_hqd_pq_rptr = 0;
	mqd->cp_hqd_pq_wptr_lo = 0;
	mqd->cp_hqd_pq_wptr_hi = 0;

	/* set the pointer to the MQD */
	mqd->cp_mqd_base_addr_lo = ring->mqd_gpu_addr & 0xfffffffc;
	mqd->cp_mqd_base_addr_hi = upper_32_bits(ring->mqd_gpu_addr);

	/* set MQD vmid to 0 */
	tmp = RREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_MQD_CONTROL);
	tmp = REG_SET_FIELD(tmp, CP_MQD_CONTROL, VMID, 0);
	mqd->cp_mqd_control = tmp;

	/* set the pointer to the HQD, this is similar CP_RB0_BASE/_HI */
	hqd_gpu_addr = ring->gpu_addr >> 8;
	mqd->cp_hqd_pq_base_lo = hqd_gpu_addr;
	mqd->cp_hqd_pq_base_hi = upper_32_bits(hqd_gpu_addr);

	/* set up the HQD, this is similar to CP_RB0_CNTL */
	tmp = RREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_HQD_PQ_CONTROL);
	tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_CONTROL, QUEUE_SIZE,
			    (order_base_2(ring->ring_size / 4) - 1));
	tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_CONTROL, RPTR_BLOCK_SIZE,
			((order_base_2(AMDGPU_GPU_PAGE_SIZE / 4) - 1) << 8));
#ifdef __BIG_ENDIAN
	tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_CONTROL, ENDIAN_SWAP, 1);
#endif
	tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_CONTROL, UNORD_DISPATCH, 0);
	tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_CONTROL, ROQ_PQ_IB_FLIP, 0);
	tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_CONTROL, PRIV_STATE, 1);
	tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_CONTROL, KMD_QUEUE, 1);
	mqd->cp_hqd_pq_control = tmp;

	/* set the wb address whether it's enabled or not */
	wb_gpu_addr = adev->wb.gpu_addr + (ring->rptr_offs * 4);
	mqd->cp_hqd_pq_rptr_report_addr_lo = wb_gpu_addr & 0xfffffffc;
	mqd->cp_hqd_pq_rptr_report_addr_hi =
		upper_32_bits(wb_gpu_addr) & 0xffff;

	/* only used if CP_PQ_WPTR_POLL_CNTL.CP_PQ_WPTR_POLL_CNTL__EN_MASK=1 */
	wb_gpu_addr = adev->wb.gpu_addr + (ring->wptr_offs * 4);
	mqd->cp_hqd_pq_wptr_poll_addr_lo = wb_gpu_addr & 0xfffffffc;
	mqd->cp_hqd_pq_wptr_poll_addr_hi = upper_32_bits(wb_gpu_addr) & 0xffff;

	/* reset read and write pointers, similar to CP_RB0_WPTR/_RPTR */
	ring->wptr = 0;
	mqd->cp_hqd_pq_rptr = RREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_HQD_PQ_RPTR);

	/* set the vmid for the queue */
	mqd->cp_hqd_vmid = 0;

	tmp = RREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_HQD_PERSISTENT_STATE);
	tmp = REG_SET_FIELD(tmp, CP_HQD_PERSISTENT_STATE, PRELOAD_SIZE, 0x53);
	mqd->cp_hqd_persistent_state = tmp;

	/* set MIN_IB_AVAIL_SIZE */
	tmp = RREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_HQD_IB_CONTROL);
	tmp = REG_SET_FIELD(tmp, CP_HQD_IB_CONTROL, MIN_IB_AVAIL_SIZE, 3);
	mqd->cp_hqd_ib_control = tmp;

	/* set static priority for a queue/ring */
	gfx_v9_4_3_mqd_set_priority(ring, mqd);
	mqd->cp_hqd_quantum = RREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_HQD_QUANTUM);

	/* map_queues packet doesn't need activate the queue,
	 * so only kiq need set this field.
	 */
	if (ring->funcs->type == AMDGPU_RING_TYPE_KIQ)
		mqd->cp_hqd_active = 1;

	return 0;
}

static int gfx_v9_4_3_xcc_kiq_init_register(struct amdgpu_ring *ring,
					    int xcc_id)
{
	struct amdgpu_device *adev = ring->adev;
	struct v9_mqd *mqd = ring->mqd_ptr;
	int j;

	/* disable wptr polling */
	WREG32_FIELD15_PREREG(GC, GET_INST(GC, xcc_id), CP_PQ_WPTR_POLL_CNTL, EN, 0);

	WREG32_SOC15_RLC(GC, GET_INST(GC, xcc_id), regCP_HQD_EOP_BASE_ADDR,
	       mqd->cp_hqd_eop_base_addr_lo);
	WREG32_SOC15_RLC(GC, GET_INST(GC, xcc_id), regCP_HQD_EOP_BASE_ADDR_HI,
	       mqd->cp_hqd_eop_base_addr_hi);

	/* set the EOP size, register value is 2^(EOP_SIZE+1) dwords */
	WREG32_SOC15_RLC(GC, GET_INST(GC, xcc_id), regCP_HQD_EOP_CONTROL,
	       mqd->cp_hqd_eop_control);

	/* enable doorbell? */
	WREG32_SOC15_RLC(GC, GET_INST(GC, xcc_id), regCP_HQD_PQ_DOORBELL_CONTROL,
	       mqd->cp_hqd_pq_doorbell_control);

	/* disable the queue if it's active */
	if (RREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_HQD_ACTIVE) & 1) {
		WREG32_SOC15_RLC(GC, GET_INST(GC, xcc_id), regCP_HQD_DEQUEUE_REQUEST, 1);
		for (j = 0; j < adev->usec_timeout; j++) {
			if (!(RREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_HQD_ACTIVE) & 1))
				break;
			udelay(1);
		}
		WREG32_SOC15_RLC(GC, GET_INST(GC, xcc_id), regCP_HQD_DEQUEUE_REQUEST,
		       mqd->cp_hqd_dequeue_request);
		WREG32_SOC15_RLC(GC, GET_INST(GC, xcc_id), regCP_HQD_PQ_RPTR,
		       mqd->cp_hqd_pq_rptr);
		WREG32_SOC15_RLC(GC, GET_INST(GC, xcc_id), regCP_HQD_PQ_WPTR_LO,
		       mqd->cp_hqd_pq_wptr_lo);
		WREG32_SOC15_RLC(GC, GET_INST(GC, xcc_id), regCP_HQD_PQ_WPTR_HI,
		       mqd->cp_hqd_pq_wptr_hi);
	}

	/* set the pointer to the MQD */
	WREG32_SOC15_RLC(GC, GET_INST(GC, xcc_id), regCP_MQD_BASE_ADDR,
	       mqd->cp_mqd_base_addr_lo);
	WREG32_SOC15_RLC(GC, GET_INST(GC, xcc_id), regCP_MQD_BASE_ADDR_HI,
	       mqd->cp_mqd_base_addr_hi);

	/* set MQD vmid to 0 */
	WREG32_SOC15_RLC(GC, GET_INST(GC, xcc_id), regCP_MQD_CONTROL,
	       mqd->cp_mqd_control);

	/* set the pointer to the HQD, this is similar CP_RB0_BASE/_HI */
	WREG32_SOC15_RLC(GC, GET_INST(GC, xcc_id), regCP_HQD_PQ_BASE,
	       mqd->cp_hqd_pq_base_lo);
	WREG32_SOC15_RLC(GC, GET_INST(GC, xcc_id), regCP_HQD_PQ_BASE_HI,
	       mqd->cp_hqd_pq_base_hi);

	/* set up the HQD, this is similar to CP_RB0_CNTL */
	WREG32_SOC15_RLC(GC, GET_INST(GC, xcc_id), regCP_HQD_PQ_CONTROL,
	       mqd->cp_hqd_pq_control);

	/* set the wb address whether it's enabled or not */
	WREG32_SOC15_RLC(GC, GET_INST(GC, xcc_id), regCP_HQD_PQ_RPTR_REPORT_ADDR,
				mqd->cp_hqd_pq_rptr_report_addr_lo);
	WREG32_SOC15_RLC(GC, GET_INST(GC, xcc_id), regCP_HQD_PQ_RPTR_REPORT_ADDR_HI,
				mqd->cp_hqd_pq_rptr_report_addr_hi);

	/* only used if CP_PQ_WPTR_POLL_CNTL.CP_PQ_WPTR_POLL_CNTL__EN_MASK=1 */
	WREG32_SOC15_RLC(GC, GET_INST(GC, xcc_id), regCP_HQD_PQ_WPTR_POLL_ADDR,
	       mqd->cp_hqd_pq_wptr_poll_addr_lo);
	WREG32_SOC15_RLC(GC, GET_INST(GC, xcc_id), regCP_HQD_PQ_WPTR_POLL_ADDR_HI,
	       mqd->cp_hqd_pq_wptr_poll_addr_hi);

	/* enable the doorbell if requested */
	if (ring->use_doorbell) {
		WREG32_SOC15(
			GC, GET_INST(GC, xcc_id),
			regCP_MEC_DOORBELL_RANGE_LOWER,
			((adev->doorbell_index.kiq +
			  xcc_id * adev->doorbell_index.xcc_doorbell_range) *
			 2) << 2);
		WREG32_SOC15(
			GC, GET_INST(GC, xcc_id),
			regCP_MEC_DOORBELL_RANGE_UPPER,
			((adev->doorbell_index.userqueue_end +
			  xcc_id * adev->doorbell_index.xcc_doorbell_range) *
			 2) << 2);
	}

	WREG32_SOC15_RLC(GC, GET_INST(GC, xcc_id), regCP_HQD_PQ_DOORBELL_CONTROL,
	       mqd->cp_hqd_pq_doorbell_control);

	/* reset read and write pointers, similar to CP_RB0_WPTR/_RPTR */
	WREG32_SOC15_RLC(GC, GET_INST(GC, xcc_id), regCP_HQD_PQ_WPTR_LO,
	       mqd->cp_hqd_pq_wptr_lo);
	WREG32_SOC15_RLC(GC, GET_INST(GC, xcc_id), regCP_HQD_PQ_WPTR_HI,
	       mqd->cp_hqd_pq_wptr_hi);

	/* set the vmid for the queue */
	WREG32_SOC15_RLC(GC, GET_INST(GC, xcc_id), regCP_HQD_VMID, mqd->cp_hqd_vmid);

	WREG32_SOC15_RLC(GC, GET_INST(GC, xcc_id), regCP_HQD_PERSISTENT_STATE,
	       mqd->cp_hqd_persistent_state);

	/* activate the queue */
	WREG32_SOC15_RLC(GC, GET_INST(GC, xcc_id), regCP_HQD_ACTIVE,
	       mqd->cp_hqd_active);

	if (ring->use_doorbell)
		WREG32_FIELD15_PREREG(GC, GET_INST(GC, xcc_id), CP_PQ_STATUS, DOORBELL_ENABLE, 1);

	return 0;
}

static int gfx_v9_4_3_xcc_q_fini_register(struct amdgpu_ring *ring,
					    int xcc_id)
{
	struct amdgpu_device *adev = ring->adev;
	int j;

	/* disable the queue if it's active */
	if (RREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_HQD_ACTIVE) & 1) {

		WREG32_SOC15_RLC(GC, GET_INST(GC, xcc_id), regCP_HQD_DEQUEUE_REQUEST, 1);

		for (j = 0; j < adev->usec_timeout; j++) {
			if (!(RREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_HQD_ACTIVE) & 1))
				break;
			udelay(1);
		}

		if (j == AMDGPU_MAX_USEC_TIMEOUT) {
			DRM_DEBUG("%s dequeue request failed.\n", ring->name);

			/* Manual disable if dequeue request times out */
			WREG32_SOC15_RLC(GC, GET_INST(GC, xcc_id), regCP_HQD_ACTIVE, 0);
		}

		WREG32_SOC15_RLC(GC, GET_INST(GC, xcc_id), regCP_HQD_DEQUEUE_REQUEST,
		      0);
	}

	WREG32_SOC15_RLC(GC, GET_INST(GC, xcc_id), regCP_HQD_IQ_TIMER, 0);
	WREG32_SOC15_RLC(GC, GET_INST(GC, xcc_id), regCP_HQD_IB_CONTROL, 0);
	WREG32_SOC15_RLC(GC, GET_INST(GC, xcc_id), regCP_HQD_PERSISTENT_STATE, CP_HQD_PERSISTENT_STATE_DEFAULT);
	WREG32_SOC15_RLC(GC, GET_INST(GC, xcc_id), regCP_HQD_PQ_DOORBELL_CONTROL, 0x40000000);
	WREG32_SOC15_RLC(GC, GET_INST(GC, xcc_id), regCP_HQD_PQ_DOORBELL_CONTROL, 0);
	WREG32_SOC15_RLC(GC, GET_INST(GC, xcc_id), regCP_HQD_PQ_RPTR, 0);
	WREG32_SOC15_RLC(GC, GET_INST(GC, xcc_id), regCP_HQD_PQ_WPTR_HI, 0);
	WREG32_SOC15_RLC(GC, GET_INST(GC, xcc_id), regCP_HQD_PQ_WPTR_LO, 0);

	return 0;
}

static int gfx_v9_4_3_xcc_kiq_init_queue(struct amdgpu_ring *ring, int xcc_id)
{
	struct amdgpu_device *adev = ring->adev;
	struct v9_mqd *mqd = ring->mqd_ptr;
	struct v9_mqd *tmp_mqd;

	gfx_v9_4_3_xcc_kiq_setting(ring, xcc_id);

	/* GPU could be in bad state during probe, driver trigger the reset
	 * after load the SMU, in this case , the mqd is not be initialized.
	 * driver need to re-init the mqd.
	 * check mqd->cp_hqd_pq_control since this value should not be 0
	 */
	tmp_mqd = (struct v9_mqd *)adev->gfx.kiq[xcc_id].mqd_backup;
	if (amdgpu_in_reset(adev) && tmp_mqd->cp_hqd_pq_control) {
		/* for GPU_RESET case , reset MQD to a clean status */
		if (adev->gfx.kiq[xcc_id].mqd_backup)
			memcpy(mqd, adev->gfx.kiq[xcc_id].mqd_backup, sizeof(struct v9_mqd_allocation));

		/* reset ring buffer */
		ring->wptr = 0;
		amdgpu_ring_clear_ring(ring);
		mutex_lock(&adev->srbm_mutex);
		soc15_grbm_select(adev, ring->me, ring->pipe, ring->queue, 0, GET_INST(GC, xcc_id));
		gfx_v9_4_3_xcc_kiq_init_register(ring, xcc_id);
		soc15_grbm_select(adev, 0, 0, 0, 0, GET_INST(GC, xcc_id));
		mutex_unlock(&adev->srbm_mutex);
	} else {
		memset((void *)mqd, 0, sizeof(struct v9_mqd_allocation));
		((struct v9_mqd_allocation *)mqd)->dynamic_cu_mask = 0xFFFFFFFF;
		((struct v9_mqd_allocation *)mqd)->dynamic_rb_mask = 0xFFFFFFFF;
		mutex_lock(&adev->srbm_mutex);
		if (amdgpu_sriov_vf(adev) && adev->in_suspend)
			amdgpu_ring_clear_ring(ring);
		soc15_grbm_select(adev, ring->me, ring->pipe, ring->queue, 0, GET_INST(GC, xcc_id));
		gfx_v9_4_3_xcc_mqd_init(ring, xcc_id);
		gfx_v9_4_3_xcc_kiq_init_register(ring, xcc_id);
		soc15_grbm_select(adev, 0, 0, 0, 0, GET_INST(GC, xcc_id));
		mutex_unlock(&adev->srbm_mutex);

		if (adev->gfx.kiq[xcc_id].mqd_backup)
			memcpy(adev->gfx.kiq[xcc_id].mqd_backup, mqd, sizeof(struct v9_mqd_allocation));
	}

	return 0;
}

static int gfx_v9_4_3_xcc_kcq_init_queue(struct amdgpu_ring *ring, int xcc_id, bool restore)
{
	struct amdgpu_device *adev = ring->adev;
	struct v9_mqd *mqd = ring->mqd_ptr;
	int mqd_idx = ring - &adev->gfx.compute_ring[0];
	struct v9_mqd *tmp_mqd;

	/* Same as above kiq init, driver need to re-init the mqd if mqd->cp_hqd_pq_control
	 * is not be initialized before
	 */
	tmp_mqd = (struct v9_mqd *)adev->gfx.mec.mqd_backup[mqd_idx];

	if (!restore && (!tmp_mqd->cp_hqd_pq_control ||
	    (!amdgpu_in_reset(adev) && !adev->in_suspend))) {
		memset((void *)mqd, 0, sizeof(struct v9_mqd_allocation));
		((struct v9_mqd_allocation *)mqd)->dynamic_cu_mask = 0xFFFFFFFF;
		((struct v9_mqd_allocation *)mqd)->dynamic_rb_mask = 0xFFFFFFFF;
		mutex_lock(&adev->srbm_mutex);
		soc15_grbm_select(adev, ring->me, ring->pipe, ring->queue, 0, GET_INST(GC, xcc_id));
		gfx_v9_4_3_xcc_mqd_init(ring, xcc_id);
		soc15_grbm_select(adev, 0, 0, 0, 0, GET_INST(GC, xcc_id));
		mutex_unlock(&adev->srbm_mutex);

		if (adev->gfx.mec.mqd_backup[mqd_idx])
			memcpy(adev->gfx.mec.mqd_backup[mqd_idx], mqd, sizeof(struct v9_mqd_allocation));
	} else {
		/* restore MQD to a clean status */
		if (adev->gfx.mec.mqd_backup[mqd_idx])
			memcpy(mqd, adev->gfx.mec.mqd_backup[mqd_idx], sizeof(struct v9_mqd_allocation));
		/* reset ring buffer */
		ring->wptr = 0;
		atomic64_set((atomic64_t *)&adev->wb.wb[ring->wptr_offs], 0);
		amdgpu_ring_clear_ring(ring);
	}

	return 0;
}

static int gfx_v9_4_3_xcc_kcq_fini_register(struct amdgpu_device *adev, int xcc_id)
{
	struct amdgpu_ring *ring;
	int j;

	for (j = 0; j < adev->gfx.num_compute_rings; j++) {
		ring = &adev->gfx.compute_ring[j +  xcc_id * adev->gfx.num_compute_rings];
		if (!amdgpu_in_reset(adev) && !adev->in_suspend) {
			mutex_lock(&adev->srbm_mutex);
			soc15_grbm_select(adev, ring->me,
					ring->pipe,
					ring->queue, 0, GET_INST(GC, xcc_id));
			gfx_v9_4_3_xcc_q_fini_register(ring, xcc_id);
			soc15_grbm_select(adev, 0, 0, 0, 0, GET_INST(GC, xcc_id));
			mutex_unlock(&adev->srbm_mutex);
		}
	}

	return 0;
}

static int gfx_v9_4_3_xcc_kiq_resume(struct amdgpu_device *adev, int xcc_id)
{
	struct amdgpu_ring *ring;
	int r;

	ring = &adev->gfx.kiq[xcc_id].ring;

	r = amdgpu_bo_reserve(ring->mqd_obj, false);
	if (unlikely(r != 0))
		return r;

	r = amdgpu_bo_kmap(ring->mqd_obj, (void **)&ring->mqd_ptr);
	if (unlikely(r != 0)) {
		amdgpu_bo_unreserve(ring->mqd_obj);
		return r;
	}

	gfx_v9_4_3_xcc_kiq_init_queue(ring, xcc_id);
	amdgpu_bo_kunmap(ring->mqd_obj);
	ring->mqd_ptr = NULL;
	amdgpu_bo_unreserve(ring->mqd_obj);
	return 0;
}

static int gfx_v9_4_3_xcc_kcq_resume(struct amdgpu_device *adev, int xcc_id)
{
	struct amdgpu_ring *ring = NULL;
	int r = 0, i;

	gfx_v9_4_3_xcc_cp_compute_enable(adev, true, xcc_id);

	for (i = 0; i < adev->gfx.num_compute_rings; i++) {
		ring = &adev->gfx.compute_ring[i + xcc_id * adev->gfx.num_compute_rings];

		r = amdgpu_bo_reserve(ring->mqd_obj, false);
		if (unlikely(r != 0))
			goto done;
		r = amdgpu_bo_kmap(ring->mqd_obj, (void **)&ring->mqd_ptr);
		if (!r) {
			r = gfx_v9_4_3_xcc_kcq_init_queue(ring, xcc_id, false);
			amdgpu_bo_kunmap(ring->mqd_obj);
			ring->mqd_ptr = NULL;
		}
		amdgpu_bo_unreserve(ring->mqd_obj);
		if (r)
			goto done;
	}

	r = amdgpu_gfx_enable_kcq(adev, xcc_id);
done:
	return r;
}

static int gfx_v9_4_3_xcc_cp_resume(struct amdgpu_device *adev, int xcc_id)
{
	struct amdgpu_ring *ring;
	int r, j;

	gfx_v9_4_3_xcc_enable_gui_idle_interrupt(adev, false, xcc_id);

	if (adev->firmware.load_type != AMDGPU_FW_LOAD_PSP) {
		gfx_v9_4_3_xcc_disable_gpa_mode(adev, xcc_id);

		r = gfx_v9_4_3_xcc_cp_compute_load_microcode(adev, xcc_id);
		if (r)
			return r;
	} else {
		gfx_v9_4_3_xcc_cp_compute_enable(adev, false, xcc_id);
	}

	r = gfx_v9_4_3_xcc_kiq_resume(adev, xcc_id);
	if (r)
		return r;

	r = gfx_v9_4_3_xcc_kcq_resume(adev, xcc_id);
	if (r)
		return r;

	for (j = 0; j < adev->gfx.num_compute_rings; j++) {
		ring = &adev->gfx.compute_ring
				[j + xcc_id * adev->gfx.num_compute_rings];
		r = amdgpu_ring_test_helper(ring);
		if (r)
			return r;
	}

	gfx_v9_4_3_xcc_enable_gui_idle_interrupt(adev, true, xcc_id);

	return 0;
}

static int gfx_v9_4_3_cp_resume(struct amdgpu_device *adev)
{
	int r = 0, i, num_xcc, num_xcp, num_xcc_per_xcp;

	num_xcc = NUM_XCC(adev->gfx.xcc_mask);
	if (amdgpu_sriov_vf(adev)) {
		enum amdgpu_gfx_partition mode;

		mode = amdgpu_xcp_query_partition_mode(adev->xcp_mgr,
						       AMDGPU_XCP_FL_NONE);
		if (mode == AMDGPU_UNKNOWN_COMPUTE_PARTITION_MODE)
			return -EINVAL;
		num_xcc_per_xcp = gfx_v9_4_3_get_xccs_per_xcp(adev);
		adev->gfx.num_xcc_per_xcp = num_xcc_per_xcp;
		num_xcp = num_xcc / num_xcc_per_xcp;
		r = amdgpu_xcp_init(adev->xcp_mgr, num_xcp, mode);

	} else {
		if (amdgpu_xcp_query_partition_mode(adev->xcp_mgr,
						    AMDGPU_XCP_FL_NONE) ==
		    AMDGPU_UNKNOWN_COMPUTE_PARTITION_MODE)
			r = amdgpu_xcp_switch_partition_mode(
				adev->xcp_mgr, amdgpu_user_partt_mode);
	}
	if (r)
		return r;

	for (i = 0; i < num_xcc; i++) {
		r = gfx_v9_4_3_xcc_cp_resume(adev, i);
		if (r)
			return r;
	}

	return 0;
}

static void gfx_v9_4_3_xcc_fini(struct amdgpu_device *adev, int xcc_id)
{
	if (amdgpu_gfx_disable_kcq(adev, xcc_id))
		DRM_ERROR("XCD %d KCQ disable failed\n", xcc_id);

	if (amdgpu_sriov_vf(adev)) {
		/* must disable polling for SRIOV when hw finished, otherwise
		 * CPC engine may still keep fetching WB address which is already
		 * invalid after sw finished and trigger DMAR reading error in
		 * hypervisor side.
		 */
		WREG32_FIELD15_PREREG(GC, GET_INST(GC, xcc_id), CP_PQ_WPTR_POLL_CNTL, EN, 0);
		return;
	}

	/* Use deinitialize sequence from CAIL when unbinding device
	 * from driver, otherwise KIQ is hanging when binding back
	 */
	if (!amdgpu_in_reset(adev) && !adev->in_suspend) {
		mutex_lock(&adev->srbm_mutex);
		soc15_grbm_select(adev, adev->gfx.kiq[xcc_id].ring.me,
				  adev->gfx.kiq[xcc_id].ring.pipe,
				  adev->gfx.kiq[xcc_id].ring.queue, 0,
				  GET_INST(GC, xcc_id));
		gfx_v9_4_3_xcc_q_fini_register(&adev->gfx.kiq[xcc_id].ring,
						 xcc_id);
		soc15_grbm_select(adev, 0, 0, 0, 0, GET_INST(GC, xcc_id));
		mutex_unlock(&adev->srbm_mutex);
	}

	gfx_v9_4_3_xcc_kcq_fini_register(adev, xcc_id);
	gfx_v9_4_3_xcc_cp_compute_enable(adev, false, xcc_id);
}

static int gfx_v9_4_3_hw_init(struct amdgpu_ip_block *ip_block)
{
	int r;
	struct amdgpu_device *adev = ip_block->adev;

	amdgpu_gfx_cleaner_shader_init(adev, adev->gfx.cleaner_shader_size,
				       adev->gfx.cleaner_shader_ptr);

	if (!amdgpu_sriov_vf(adev))
		gfx_v9_4_3_init_golden_registers(adev);

	gfx_v9_4_3_constants_init(adev);

	r = adev->gfx.rlc.funcs->resume(adev);
	if (r)
		return r;

	r = gfx_v9_4_3_cp_resume(adev);
	if (r)
		return r;

	return r;
}

static int gfx_v9_4_3_hw_fini(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	int i, num_xcc;

	amdgpu_irq_put(adev, &adev->gfx.priv_reg_irq, 0);
	amdgpu_irq_put(adev, &adev->gfx.priv_inst_irq, 0);
	amdgpu_irq_put(adev, &adev->gfx.bad_op_irq, 0);

	num_xcc = NUM_XCC(adev->gfx.xcc_mask);
	for (i = 0; i < num_xcc; i++) {
		gfx_v9_4_3_xcc_fini(adev, i);
	}

	return 0;
}

static int gfx_v9_4_3_suspend(struct amdgpu_ip_block *ip_block)
{
	return gfx_v9_4_3_hw_fini(ip_block);
}

static int gfx_v9_4_3_resume(struct amdgpu_ip_block *ip_block)
{
	return gfx_v9_4_3_hw_init(ip_block);
}

static bool gfx_v9_4_3_is_idle(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int i, num_xcc;

	num_xcc = NUM_XCC(adev->gfx.xcc_mask);
	for (i = 0; i < num_xcc; i++) {
		if (REG_GET_FIELD(RREG32_SOC15(GC, GET_INST(GC, i), regGRBM_STATUS),
					GRBM_STATUS, GUI_ACTIVE))
			return false;
	}
	return true;
}

static int gfx_v9_4_3_wait_for_idle(struct amdgpu_ip_block *ip_block)
{
	unsigned i;
	struct amdgpu_device *adev = ip_block->adev;

	for (i = 0; i < adev->usec_timeout; i++) {
		if (gfx_v9_4_3_is_idle(adev))
			return 0;
		udelay(1);
	}
	return -ETIMEDOUT;
}

static int gfx_v9_4_3_soft_reset(struct amdgpu_ip_block *ip_block)
{
	u32 grbm_soft_reset = 0;
	u32 tmp;
	struct amdgpu_device *adev = ip_block->adev;

	/* GRBM_STATUS */
	tmp = RREG32_SOC15(GC, GET_INST(GC, 0), regGRBM_STATUS);
	if (tmp & (GRBM_STATUS__PA_BUSY_MASK | GRBM_STATUS__SC_BUSY_MASK |
		   GRBM_STATUS__BCI_BUSY_MASK | GRBM_STATUS__SX_BUSY_MASK |
		   GRBM_STATUS__TA_BUSY_MASK | GRBM_STATUS__VGT_BUSY_MASK |
		   GRBM_STATUS__DB_BUSY_MASK | GRBM_STATUS__CB_BUSY_MASK |
		   GRBM_STATUS__GDS_BUSY_MASK | GRBM_STATUS__SPI_BUSY_MASK |
		   GRBM_STATUS__IA_BUSY_MASK | GRBM_STATUS__IA_BUSY_NO_DMA_MASK)) {
		grbm_soft_reset = REG_SET_FIELD(grbm_soft_reset,
						GRBM_SOFT_RESET, SOFT_RESET_CP, 1);
		grbm_soft_reset = REG_SET_FIELD(grbm_soft_reset,
						GRBM_SOFT_RESET, SOFT_RESET_GFX, 1);
	}

	if (tmp & (GRBM_STATUS__CP_BUSY_MASK | GRBM_STATUS__CP_COHERENCY_BUSY_MASK)) {
		grbm_soft_reset = REG_SET_FIELD(grbm_soft_reset,
						GRBM_SOFT_RESET, SOFT_RESET_CP, 1);
	}

	/* GRBM_STATUS2 */
	tmp = RREG32_SOC15(GC, GET_INST(GC, 0), regGRBM_STATUS2);
	if (REG_GET_FIELD(tmp, GRBM_STATUS2, RLC_BUSY))
		grbm_soft_reset = REG_SET_FIELD(grbm_soft_reset,
						GRBM_SOFT_RESET, SOFT_RESET_RLC, 1);


	if (grbm_soft_reset) {
		/* stop the rlc */
		adev->gfx.rlc.funcs->stop(adev);

		/* Disable MEC parsing/prefetching */
		gfx_v9_4_3_xcc_cp_compute_enable(adev, false, 0);

		if (grbm_soft_reset) {
			tmp = RREG32_SOC15(GC, GET_INST(GC, 0), regGRBM_SOFT_RESET);
			tmp |= grbm_soft_reset;
			dev_info(adev->dev, "GRBM_SOFT_RESET=0x%08X\n", tmp);
			WREG32_SOC15(GC, GET_INST(GC, 0), regGRBM_SOFT_RESET, tmp);
			tmp = RREG32_SOC15(GC, GET_INST(GC, 0), regGRBM_SOFT_RESET);

			udelay(50);

			tmp &= ~grbm_soft_reset;
			WREG32_SOC15(GC, GET_INST(GC, 0), regGRBM_SOFT_RESET, tmp);
			tmp = RREG32_SOC15(GC, GET_INST(GC, 0), regGRBM_SOFT_RESET);
		}

		/* Wait a little for things to settle down */
		udelay(50);
	}
	return 0;
}

static void gfx_v9_4_3_ring_emit_gds_switch(struct amdgpu_ring *ring,
					  uint32_t vmid,
					  uint32_t gds_base, uint32_t gds_size,
					  uint32_t gws_base, uint32_t gws_size,
					  uint32_t oa_base, uint32_t oa_size)
{
	struct amdgpu_device *adev = ring->adev;

	/* GDS Base */
	gfx_v9_4_3_write_data_to_reg(ring, 0, false,
				   SOC15_REG_OFFSET(GC, GET_INST(GC, 0), regGDS_VMID0_BASE) + 2 * vmid,
				   gds_base);

	/* GDS Size */
	gfx_v9_4_3_write_data_to_reg(ring, 0, false,
				   SOC15_REG_OFFSET(GC, GET_INST(GC, 0), regGDS_VMID0_SIZE) + 2 * vmid,
				   gds_size);

	/* GWS */
	gfx_v9_4_3_write_data_to_reg(ring, 0, false,
				   SOC15_REG_OFFSET(GC, GET_INST(GC, 0), regGDS_GWS_VMID0) + vmid,
				   gws_size << GDS_GWS_VMID0__SIZE__SHIFT | gws_base);

	/* OA */
	gfx_v9_4_3_write_data_to_reg(ring, 0, false,
				   SOC15_REG_OFFSET(GC, GET_INST(GC, 0), regGDS_OA_VMID0) + vmid,
				   (1 << (oa_size + oa_base)) - (1 << oa_base));
}

static int gfx_v9_4_3_early_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;

	adev->gfx.num_compute_rings = min(amdgpu_gfx_get_num_kcq(adev),
					  AMDGPU_MAX_COMPUTE_RINGS);
	gfx_v9_4_3_set_kiq_pm4_funcs(adev);
	gfx_v9_4_3_set_ring_funcs(adev);
	gfx_v9_4_3_set_irq_funcs(adev);
	gfx_v9_4_3_set_gds_init(adev);
	gfx_v9_4_3_set_rlc_funcs(adev);

	/* init rlcg reg access ctrl */
	gfx_v9_4_3_init_rlcg_reg_access_ctrl(adev);

	return gfx_v9_4_3_init_microcode(adev);
}

static int gfx_v9_4_3_late_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	int r;

	r = amdgpu_irq_get(adev, &adev->gfx.priv_reg_irq, 0);
	if (r)
		return r;

	r = amdgpu_irq_get(adev, &adev->gfx.priv_inst_irq, 0);
	if (r)
		return r;

	r = amdgpu_irq_get(adev, &adev->gfx.bad_op_irq, 0);
	if (r)
		return r;

	if (adev->gfx.ras &&
	    adev->gfx.ras->enable_watchdog_timer)
		adev->gfx.ras->enable_watchdog_timer(adev);

	return 0;
}

static void gfx_v9_4_3_xcc_update_sram_fgcg(struct amdgpu_device *adev,
					    bool enable, int xcc_id)
{
	uint32_t def, data;

	if (!(adev->cg_flags & AMD_CG_SUPPORT_GFX_FGCG))
		return;

	def = data = RREG32_SOC15(GC, GET_INST(GC, xcc_id),
				  regRLC_CGTT_MGCG_OVERRIDE);

	if (enable)
		data &= ~RLC_CGTT_MGCG_OVERRIDE__GFXIP_FGCG_OVERRIDE_MASK;
	else
		data |= RLC_CGTT_MGCG_OVERRIDE__GFXIP_FGCG_OVERRIDE_MASK;

	if (def != data)
		WREG32_SOC15(GC, GET_INST(GC, xcc_id),
			     regRLC_CGTT_MGCG_OVERRIDE, data);

}

static void gfx_v9_4_3_xcc_update_repeater_fgcg(struct amdgpu_device *adev,
						bool enable, int xcc_id)
{
	uint32_t def, data;

	if (!(adev->cg_flags & AMD_CG_SUPPORT_REPEATER_FGCG))
		return;

	def = data = RREG32_SOC15(GC, GET_INST(GC, xcc_id),
				  regRLC_CGTT_MGCG_OVERRIDE);

	if (enable)
		data &= ~RLC_CGTT_MGCG_OVERRIDE__GFXIP_REP_FGCG_OVERRIDE_MASK;
	else
		data |= RLC_CGTT_MGCG_OVERRIDE__GFXIP_REP_FGCG_OVERRIDE_MASK;

	if (def != data)
		WREG32_SOC15(GC, GET_INST(GC, xcc_id),
			     regRLC_CGTT_MGCG_OVERRIDE, data);
}

static void
gfx_v9_4_3_xcc_update_medium_grain_clock_gating(struct amdgpu_device *adev,
						bool enable, int xcc_id)
{
	uint32_t data, def;

	/* It is disabled by HW by default */
	if (enable && (adev->cg_flags & AMD_CG_SUPPORT_GFX_MGCG)) {
		/* 1 - RLC_CGTT_MGCG_OVERRIDE */
		def = data = RREG32_SOC15(GC, GET_INST(GC, xcc_id), regRLC_CGTT_MGCG_OVERRIDE);

		data &= ~(RLC_CGTT_MGCG_OVERRIDE__GRBM_CGTT_SCLK_OVERRIDE_MASK |
			  RLC_CGTT_MGCG_OVERRIDE__GFXIP_MGCG_OVERRIDE_MASK |
			  RLC_CGTT_MGCG_OVERRIDE__RLC_CGTT_SCLK_OVERRIDE_MASK |
			  RLC_CGTT_MGCG_OVERRIDE__GFXIP_MGLS_OVERRIDE_MASK);

		if (def != data)
			WREG32_SOC15(GC, GET_INST(GC, xcc_id), regRLC_CGTT_MGCG_OVERRIDE, data);

		/* MGLS is a global flag to control all MGLS in GFX */
		if (adev->cg_flags & AMD_CG_SUPPORT_GFX_MGLS) {
			/* 2 - RLC memory Light sleep */
			if (adev->cg_flags & AMD_CG_SUPPORT_GFX_RLC_LS) {
				def = data = RREG32_SOC15(GC, GET_INST(GC, xcc_id), regRLC_MEM_SLP_CNTL);
				data |= RLC_MEM_SLP_CNTL__RLC_MEM_LS_EN_MASK;
				if (def != data)
					WREG32_SOC15(GC, GET_INST(GC, xcc_id), regRLC_MEM_SLP_CNTL, data);
			}
			/* 3 - CP memory Light sleep */
			if (adev->cg_flags & AMD_CG_SUPPORT_GFX_CP_LS) {
				def = data = RREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_MEM_SLP_CNTL);
				data |= CP_MEM_SLP_CNTL__CP_MEM_LS_EN_MASK;
				if (def != data)
					WREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_MEM_SLP_CNTL, data);
			}
		}
	} else {
		/* 1 - MGCG_OVERRIDE */
		def = data = RREG32_SOC15(GC, GET_INST(GC, xcc_id), regRLC_CGTT_MGCG_OVERRIDE);

		data |= (RLC_CGTT_MGCG_OVERRIDE__RLC_CGTT_SCLK_OVERRIDE_MASK |
			 RLC_CGTT_MGCG_OVERRIDE__GRBM_CGTT_SCLK_OVERRIDE_MASK |
			 RLC_CGTT_MGCG_OVERRIDE__GFXIP_MGCG_OVERRIDE_MASK |
			 RLC_CGTT_MGCG_OVERRIDE__GFXIP_MGLS_OVERRIDE_MASK);

		if (def != data)
			WREG32_SOC15(GC, GET_INST(GC, xcc_id), regRLC_CGTT_MGCG_OVERRIDE, data);

		/* 2 - disable MGLS in RLC */
		data = RREG32_SOC15(GC, GET_INST(GC, xcc_id), regRLC_MEM_SLP_CNTL);
		if (data & RLC_MEM_SLP_CNTL__RLC_MEM_LS_EN_MASK) {
			data &= ~RLC_MEM_SLP_CNTL__RLC_MEM_LS_EN_MASK;
			WREG32_SOC15(GC, GET_INST(GC, xcc_id), regRLC_MEM_SLP_CNTL, data);
		}

		/* 3 - disable MGLS in CP */
		data = RREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_MEM_SLP_CNTL);
		if (data & CP_MEM_SLP_CNTL__CP_MEM_LS_EN_MASK) {
			data &= ~CP_MEM_SLP_CNTL__CP_MEM_LS_EN_MASK;
			WREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_MEM_SLP_CNTL, data);
		}
	}

}

static void
gfx_v9_4_3_xcc_update_coarse_grain_clock_gating(struct amdgpu_device *adev,
						bool enable, int xcc_id)
{
	uint32_t def, data;

	if (enable && (adev->cg_flags & AMD_CG_SUPPORT_GFX_CGCG)) {

		def = data = RREG32_SOC15(GC, GET_INST(GC, xcc_id), regRLC_CGTT_MGCG_OVERRIDE);
		/* unset CGCG override */
		data &= ~RLC_CGTT_MGCG_OVERRIDE__GFXIP_CGCG_OVERRIDE_MASK;
		if (adev->cg_flags & AMD_CG_SUPPORT_GFX_CGLS)
			data &= ~RLC_CGTT_MGCG_OVERRIDE__GFXIP_CGLS_OVERRIDE_MASK;
		else
			data |= RLC_CGTT_MGCG_OVERRIDE__GFXIP_CGLS_OVERRIDE_MASK;
		/* update CGCG and CGLS override bits */
		if (def != data)
			WREG32_SOC15(GC, GET_INST(GC, xcc_id), regRLC_CGTT_MGCG_OVERRIDE, data);

		/* CGCG Hysteresis: 400us */
		def = RREG32_SOC15(GC, GET_INST(GC, xcc_id), regRLC_CGCG_CGLS_CTRL);

		data = (0x2710
			<< RLC_CGCG_CGLS_CTRL__CGCG_GFX_IDLE_THRESHOLD__SHIFT) |
		       RLC_CGCG_CGLS_CTRL__CGCG_EN_MASK;
		if (adev->cg_flags & AMD_CG_SUPPORT_GFX_CGLS)
			data |= (0x000F << RLC_CGCG_CGLS_CTRL__CGLS_REP_COMPANSAT_DELAY__SHIFT) |
				RLC_CGCG_CGLS_CTRL__CGLS_EN_MASK;
		if (def != data)
			WREG32_SOC15(GC, GET_INST(GC, xcc_id), regRLC_CGCG_CGLS_CTRL, data);

		/* set IDLE_POLL_COUNT(0x33450100)*/
		def = RREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_RB_WPTR_POLL_CNTL);
		data = (0x0100 << CP_RB_WPTR_POLL_CNTL__POLL_FREQUENCY__SHIFT) |
			(0x3345 << CP_RB_WPTR_POLL_CNTL__IDLE_POLL_COUNT__SHIFT);
		if (def != data)
			WREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_RB_WPTR_POLL_CNTL, data);
	} else {
		def = data = RREG32_SOC15(GC, GET_INST(GC, xcc_id), regRLC_CGCG_CGLS_CTRL);
		/* reset CGCG/CGLS bits */
		data &= ~(RLC_CGCG_CGLS_CTRL__CGCG_EN_MASK | RLC_CGCG_CGLS_CTRL__CGLS_EN_MASK);
		/* disable cgcg and cgls in FSM */
		if (def != data)
			WREG32_SOC15(GC, GET_INST(GC, xcc_id), regRLC_CGCG_CGLS_CTRL, data);
	}

}

static int gfx_v9_4_3_xcc_update_gfx_clock_gating(struct amdgpu_device *adev,
						  bool enable, int xcc_id)
{
	amdgpu_gfx_rlc_enter_safe_mode(adev, xcc_id);

	if (enable) {
		/* FGCG */
		gfx_v9_4_3_xcc_update_sram_fgcg(adev, enable, xcc_id);
		gfx_v9_4_3_xcc_update_repeater_fgcg(adev, enable, xcc_id);

		/* CGCG/CGLS should be enabled after MGCG/MGLS
		 * ===  MGCG + MGLS ===
		 */
		gfx_v9_4_3_xcc_update_medium_grain_clock_gating(adev, enable,
								xcc_id);
		/* ===  CGCG + CGLS === */
		gfx_v9_4_3_xcc_update_coarse_grain_clock_gating(adev, enable,
								xcc_id);
	} else {
		/* CGCG/CGLS should be disabled before MGCG/MGLS
		 * ===  CGCG + CGLS ===
		 */
		gfx_v9_4_3_xcc_update_coarse_grain_clock_gating(adev, enable,
								xcc_id);
		/* ===  MGCG + MGLS === */
		gfx_v9_4_3_xcc_update_medium_grain_clock_gating(adev, enable,
								xcc_id);

		/* FGCG */
		gfx_v9_4_3_xcc_update_sram_fgcg(adev, enable, xcc_id);
		gfx_v9_4_3_xcc_update_repeater_fgcg(adev, enable, xcc_id);
	}

	amdgpu_gfx_rlc_exit_safe_mode(adev, xcc_id);

	return 0;
}

static const struct amdgpu_rlc_funcs gfx_v9_4_3_rlc_funcs = {
	.is_rlc_enabled = gfx_v9_4_3_is_rlc_enabled,
	.set_safe_mode = gfx_v9_4_3_xcc_set_safe_mode,
	.unset_safe_mode = gfx_v9_4_3_xcc_unset_safe_mode,
	.init = gfx_v9_4_3_rlc_init,
	.resume = gfx_v9_4_3_rlc_resume,
	.stop = gfx_v9_4_3_rlc_stop,
	.reset = gfx_v9_4_3_rlc_reset,
	.start = gfx_v9_4_3_rlc_start,
	.update_spm_vmid = gfx_v9_4_3_update_spm_vmid,
	.is_rlcg_access_range = gfx_v9_4_3_is_rlcg_access_range,
};

static int gfx_v9_4_3_set_powergating_state(struct amdgpu_ip_block *ip_block,
					  enum amd_powergating_state state)
{
	return 0;
}

static int gfx_v9_4_3_set_clockgating_state(struct amdgpu_ip_block *ip_block,
					  enum amd_clockgating_state state)
{
	struct amdgpu_device *adev = ip_block->adev;
	int i, num_xcc;

	if (amdgpu_sriov_vf(adev))
		return 0;

	num_xcc = NUM_XCC(adev->gfx.xcc_mask);
	switch (amdgpu_ip_version(adev, GC_HWIP, 0)) {
	case IP_VERSION(9, 4, 3):
	case IP_VERSION(9, 4, 4):
		for (i = 0; i < num_xcc; i++)
			gfx_v9_4_3_xcc_update_gfx_clock_gating(
				adev, state == AMD_CG_STATE_GATE, i);
		break;
	default:
		break;
	}
	return 0;
}

static void gfx_v9_4_3_get_clockgating_state(void *handle, u64 *flags)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int data;

	if (amdgpu_sriov_vf(adev))
		*flags = 0;

	/* AMD_CG_SUPPORT_GFX_MGCG */
	data = RREG32_KIQ(SOC15_REG_OFFSET(GC, GET_INST(GC, 0), regRLC_CGTT_MGCG_OVERRIDE));
	if (!(data & RLC_CGTT_MGCG_OVERRIDE__GFXIP_MGCG_OVERRIDE_MASK))
		*flags |= AMD_CG_SUPPORT_GFX_MGCG;

	/* AMD_CG_SUPPORT_GFX_CGCG */
	data = RREG32_KIQ(SOC15_REG_OFFSET(GC, GET_INST(GC, 0), regRLC_CGCG_CGLS_CTRL));
	if (data & RLC_CGCG_CGLS_CTRL__CGCG_EN_MASK)
		*flags |= AMD_CG_SUPPORT_GFX_CGCG;

	/* AMD_CG_SUPPORT_GFX_CGLS */
	if (data & RLC_CGCG_CGLS_CTRL__CGLS_EN_MASK)
		*flags |= AMD_CG_SUPPORT_GFX_CGLS;

	/* AMD_CG_SUPPORT_GFX_RLC_LS */
	data = RREG32_KIQ(SOC15_REG_OFFSET(GC, GET_INST(GC, 0), regRLC_MEM_SLP_CNTL));
	if (data & RLC_MEM_SLP_CNTL__RLC_MEM_LS_EN_MASK)
		*flags |= AMD_CG_SUPPORT_GFX_RLC_LS | AMD_CG_SUPPORT_GFX_MGLS;

	/* AMD_CG_SUPPORT_GFX_CP_LS */
	data = RREG32_KIQ(SOC15_REG_OFFSET(GC, GET_INST(GC, 0), regCP_MEM_SLP_CNTL));
	if (data & CP_MEM_SLP_CNTL__CP_MEM_LS_EN_MASK)
		*flags |= AMD_CG_SUPPORT_GFX_CP_LS | AMD_CG_SUPPORT_GFX_MGLS;
}

static void gfx_v9_4_3_ring_emit_hdp_flush(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	u32 ref_and_mask, reg_mem_engine;
	const struct nbio_hdp_flush_reg *nbio_hf_reg = adev->nbio.hdp_flush_reg;

	if (ring->funcs->type == AMDGPU_RING_TYPE_COMPUTE) {
		switch (ring->me) {
		case 1:
			ref_and_mask = nbio_hf_reg->ref_and_mask_cp2 << ring->pipe;
			break;
		case 2:
			ref_and_mask = nbio_hf_reg->ref_and_mask_cp6 << ring->pipe;
			break;
		default:
			return;
		}
		reg_mem_engine = 0;
	} else {
		ref_and_mask = nbio_hf_reg->ref_and_mask_cp0;
		reg_mem_engine = 1; /* pfp */
	}

	gfx_v9_4_3_wait_reg_mem(ring, reg_mem_engine, 0, 1,
			      adev->nbio.funcs->get_hdp_flush_req_offset(adev),
			      adev->nbio.funcs->get_hdp_flush_done_offset(adev),
			      ref_and_mask, ref_and_mask, 0x20);
}

static void gfx_v9_4_3_ring_emit_ib_compute(struct amdgpu_ring *ring,
					  struct amdgpu_job *job,
					  struct amdgpu_ib *ib,
					  uint32_t flags)
{
	unsigned vmid = AMDGPU_JOB_GET_VMID(job);
	u32 control = INDIRECT_BUFFER_VALID | ib->length_dw | (vmid << 24);

	/* Currently, there is a high possibility to get wave ID mismatch
	 * between ME and GDS, leading to a hw deadlock, because ME generates
	 * different wave IDs than the GDS expects. This situation happens
	 * randomly when at least 5 compute pipes use GDS ordered append.
	 * The wave IDs generated by ME are also wrong after suspend/resume.
	 * Those are probably bugs somewhere else in the kernel driver.
	 *
	 * Writing GDS_COMPUTE_MAX_WAVE_ID resets wave ID counters in ME and
	 * GDS to 0 for this ring (me/pipe).
	 */
	if (ib->flags & AMDGPU_IB_FLAG_RESET_GDS_MAX_WAVE_ID) {
		amdgpu_ring_write(ring, PACKET3(PACKET3_SET_CONFIG_REG, 1));
		amdgpu_ring_write(ring, regGDS_COMPUTE_MAX_WAVE_ID);
		amdgpu_ring_write(ring, ring->adev->gds.gds_compute_max_wave_id);
	}

	amdgpu_ring_write(ring, PACKET3(PACKET3_INDIRECT_BUFFER, 2));
	BUG_ON(ib->gpu_addr & 0x3); /* Dword align */
	amdgpu_ring_write(ring,
#ifdef __BIG_ENDIAN
				(2 << 0) |
#endif
				lower_32_bits(ib->gpu_addr));
	amdgpu_ring_write(ring, upper_32_bits(ib->gpu_addr));
	amdgpu_ring_write(ring, control);
}

static void gfx_v9_4_3_ring_emit_fence(struct amdgpu_ring *ring, u64 addr,
				     u64 seq, unsigned flags)
{
	bool write64bit = flags & AMDGPU_FENCE_FLAG_64BIT;
	bool int_sel = flags & AMDGPU_FENCE_FLAG_INT;
	bool writeback = flags & AMDGPU_FENCE_FLAG_TC_WB_ONLY;

	/* RELEASE_MEM - flush caches, send int */
	amdgpu_ring_write(ring, PACKET3(PACKET3_RELEASE_MEM, 6));
	amdgpu_ring_write(ring, ((writeback ? (EOP_TC_WB_ACTION_EN |
					       EOP_TC_NC_ACTION_EN) :
					      (EOP_TCL1_ACTION_EN |
					       EOP_TC_ACTION_EN |
					       EOP_TC_WB_ACTION_EN |
					       EOP_TC_MD_ACTION_EN)) |
				 EVENT_TYPE(CACHE_FLUSH_AND_INV_TS_EVENT) |
				 EVENT_INDEX(5)));
	amdgpu_ring_write(ring, DATA_SEL(write64bit ? 2 : 1) | INT_SEL(int_sel ? 2 : 0));

	/*
	 * the address should be Qword aligned if 64bit write, Dword
	 * aligned if only send 32bit data low (discard data high)
	 */
	if (write64bit)
		BUG_ON(addr & 0x7);
	else
		BUG_ON(addr & 0x3);
	amdgpu_ring_write(ring, lower_32_bits(addr));
	amdgpu_ring_write(ring, upper_32_bits(addr));
	amdgpu_ring_write(ring, lower_32_bits(seq));
	amdgpu_ring_write(ring, upper_32_bits(seq));
	amdgpu_ring_write(ring, 0);
}

static void gfx_v9_4_3_ring_emit_pipeline_sync(struct amdgpu_ring *ring)
{
	int usepfp = (ring->funcs->type == AMDGPU_RING_TYPE_GFX);
	uint32_t seq = ring->fence_drv.sync_seq;
	uint64_t addr = ring->fence_drv.gpu_addr;

	gfx_v9_4_3_wait_reg_mem(ring, usepfp, 1, 0,
			      lower_32_bits(addr), upper_32_bits(addr),
			      seq, 0xffffffff, 4);
}

static void gfx_v9_4_3_ring_emit_vm_flush(struct amdgpu_ring *ring,
					unsigned vmid, uint64_t pd_addr)
{
	amdgpu_gmc_emit_flush_gpu_tlb(ring, vmid, pd_addr);
}

static u64 gfx_v9_4_3_ring_get_rptr_compute(struct amdgpu_ring *ring)
{
	return ring->adev->wb.wb[ring->rptr_offs]; /* gfx9 hardware is 32bit rptr */
}

static u64 gfx_v9_4_3_ring_get_wptr_compute(struct amdgpu_ring *ring)
{
	u64 wptr;

	/* XXX check if swapping is necessary on BE */
	if (ring->use_doorbell)
		wptr = atomic64_read((atomic64_t *)&ring->adev->wb.wb[ring->wptr_offs]);
	else
		BUG();
	return wptr;
}

static void gfx_v9_4_3_ring_set_wptr_compute(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	/* XXX check if swapping is necessary on BE */
	if (ring->use_doorbell) {
		atomic64_set((atomic64_t *)&adev->wb.wb[ring->wptr_offs], ring->wptr);
		WDOORBELL64(ring->doorbell_index, ring->wptr);
	} else {
		BUG(); /* only DOORBELL method supported on gfx9 now */
	}
}

static void gfx_v9_4_3_ring_emit_fence_kiq(struct amdgpu_ring *ring, u64 addr,
					 u64 seq, unsigned int flags)
{
	struct amdgpu_device *adev = ring->adev;

	/* we only allocate 32bit for each seq wb address */
	BUG_ON(flags & AMDGPU_FENCE_FLAG_64BIT);

	/* write fence seq to the "addr" */
	amdgpu_ring_write(ring, PACKET3(PACKET3_WRITE_DATA, 3));
	amdgpu_ring_write(ring, (WRITE_DATA_ENGINE_SEL(0) |
				 WRITE_DATA_DST_SEL(5) | WR_CONFIRM));
	amdgpu_ring_write(ring, lower_32_bits(addr));
	amdgpu_ring_write(ring, upper_32_bits(addr));
	amdgpu_ring_write(ring, lower_32_bits(seq));

	if (flags & AMDGPU_FENCE_FLAG_INT) {
		/* set register to trigger INT */
		amdgpu_ring_write(ring, PACKET3(PACKET3_WRITE_DATA, 3));
		amdgpu_ring_write(ring, (WRITE_DATA_ENGINE_SEL(0) |
					 WRITE_DATA_DST_SEL(0) | WR_CONFIRM));
		amdgpu_ring_write(ring, SOC15_REG_OFFSET(GC, GET_INST(GC, 0), regCPC_INT_STATUS));
		amdgpu_ring_write(ring, 0);
		amdgpu_ring_write(ring, 0x20000000); /* src_id is 178 */
	}
}

static void gfx_v9_4_3_ring_emit_rreg(struct amdgpu_ring *ring, uint32_t reg,
				    uint32_t reg_val_offs)
{
	struct amdgpu_device *adev = ring->adev;

	reg = gfx_v9_4_3_normalize_xcc_reg_offset(reg);

	amdgpu_ring_write(ring, PACKET3(PACKET3_COPY_DATA, 4));
	amdgpu_ring_write(ring, 0 |	/* src: register*/
				(5 << 8) |	/* dst: memory */
				(1 << 20));	/* write confirm */
	amdgpu_ring_write(ring, reg);
	amdgpu_ring_write(ring, 0);
	amdgpu_ring_write(ring, lower_32_bits(adev->wb.gpu_addr +
				reg_val_offs * 4));
	amdgpu_ring_write(ring, upper_32_bits(adev->wb.gpu_addr +
				reg_val_offs * 4));
}

static void gfx_v9_4_3_ring_emit_wreg(struct amdgpu_ring *ring, uint32_t reg,
				    uint32_t val)
{
	uint32_t cmd = 0;

	reg = gfx_v9_4_3_normalize_xcc_reg_offset(reg);

	switch (ring->funcs->type) {
	case AMDGPU_RING_TYPE_GFX:
		cmd = WRITE_DATA_ENGINE_SEL(1) | WR_CONFIRM;
		break;
	case AMDGPU_RING_TYPE_KIQ:
		cmd = (1 << 16); /* no inc addr */
		break;
	default:
		cmd = WR_CONFIRM;
		break;
	}
	amdgpu_ring_write(ring, PACKET3(PACKET3_WRITE_DATA, 3));
	amdgpu_ring_write(ring, cmd);
	amdgpu_ring_write(ring, reg);
	amdgpu_ring_write(ring, 0);
	amdgpu_ring_write(ring, val);
}

static void gfx_v9_4_3_ring_emit_reg_wait(struct amdgpu_ring *ring, uint32_t reg,
					uint32_t val, uint32_t mask)
{
	gfx_v9_4_3_wait_reg_mem(ring, 0, 0, 0, reg, 0, val, mask, 0x20);
}

static void gfx_v9_4_3_ring_emit_reg_write_reg_wait(struct amdgpu_ring *ring,
						  uint32_t reg0, uint32_t reg1,
						  uint32_t ref, uint32_t mask)
{
	amdgpu_ring_emit_reg_write_reg_wait_helper(ring, reg0, reg1,
						   ref, mask);
}

static void gfx_v9_4_3_ring_soft_recovery(struct amdgpu_ring *ring,
					  unsigned vmid)
{
	struct amdgpu_device *adev = ring->adev;
	uint32_t value = 0;

	value = REG_SET_FIELD(value, SQ_CMD, CMD, 0x03);
	value = REG_SET_FIELD(value, SQ_CMD, MODE, 0x01);
	value = REG_SET_FIELD(value, SQ_CMD, CHECK_VMID, 1);
	value = REG_SET_FIELD(value, SQ_CMD, VM_ID, vmid);
	amdgpu_gfx_rlc_enter_safe_mode(adev, ring->xcc_id);
	WREG32_SOC15(GC, GET_INST(GC, ring->xcc_id), regSQ_CMD, value);
	amdgpu_gfx_rlc_exit_safe_mode(adev, ring->xcc_id);
}

static void gfx_v9_4_3_xcc_set_compute_eop_interrupt_state(
	struct amdgpu_device *adev, int me, int pipe,
	enum amdgpu_interrupt_state state, int xcc_id)
{
	u32 mec_int_cntl, mec_int_cntl_reg;

	/*
	 * amdgpu controls only the first MEC. That's why this function only
	 * handles the setting of interrupts for this specific MEC. All other
	 * pipes' interrupts are set by amdkfd.
	 */

	if (me == 1) {
		switch (pipe) {
		case 0:
			mec_int_cntl_reg = SOC15_REG_OFFSET(GC, GET_INST(GC, xcc_id), regCP_ME1_PIPE0_INT_CNTL);
			break;
		case 1:
			mec_int_cntl_reg = SOC15_REG_OFFSET(GC, GET_INST(GC, xcc_id), regCP_ME1_PIPE1_INT_CNTL);
			break;
		case 2:
			mec_int_cntl_reg = SOC15_REG_OFFSET(GC, GET_INST(GC, xcc_id), regCP_ME1_PIPE2_INT_CNTL);
			break;
		case 3:
			mec_int_cntl_reg = SOC15_REG_OFFSET(GC, GET_INST(GC, xcc_id), regCP_ME1_PIPE3_INT_CNTL);
			break;
		default:
			DRM_DEBUG("invalid pipe %d\n", pipe);
			return;
		}
	} else {
		DRM_DEBUG("invalid me %d\n", me);
		return;
	}

	switch (state) {
	case AMDGPU_IRQ_STATE_DISABLE:
		mec_int_cntl = RREG32_XCC(mec_int_cntl_reg, xcc_id);
		mec_int_cntl = REG_SET_FIELD(mec_int_cntl, CP_ME1_PIPE0_INT_CNTL,
					     TIME_STAMP_INT_ENABLE, 0);
		WREG32_XCC(mec_int_cntl_reg, mec_int_cntl, xcc_id);
		break;
	case AMDGPU_IRQ_STATE_ENABLE:
		mec_int_cntl = RREG32_XCC(mec_int_cntl_reg, xcc_id);
		mec_int_cntl = REG_SET_FIELD(mec_int_cntl, CP_ME1_PIPE0_INT_CNTL,
					     TIME_STAMP_INT_ENABLE, 1);
		WREG32_XCC(mec_int_cntl_reg, mec_int_cntl, xcc_id);
		break;
	default:
		break;
	}
}

static u32 gfx_v9_4_3_get_cpc_int_cntl(struct amdgpu_device *adev,
				     int xcc_id, int me, int pipe)
{
	/*
	 * amdgpu controls only the first MEC. That's why this function only
	 * handles the setting of interrupts for this specific MEC. All other
	 * pipes' interrupts are set by amdkfd.
	 */
	if (me != 1)
		return 0;

	switch (pipe) {
	case 0:
		return SOC15_REG_OFFSET(GC, GET_INST(GC, xcc_id), regCP_ME1_PIPE0_INT_CNTL);
	case 1:
		return SOC15_REG_OFFSET(GC, GET_INST(GC, xcc_id), regCP_ME1_PIPE1_INT_CNTL);
	case 2:
		return SOC15_REG_OFFSET(GC, GET_INST(GC, xcc_id), regCP_ME1_PIPE2_INT_CNTL);
	case 3:
		return SOC15_REG_OFFSET(GC, GET_INST(GC, xcc_id), regCP_ME1_PIPE3_INT_CNTL);
	default:
		return 0;
	}
}

static int gfx_v9_4_3_set_priv_reg_fault_state(struct amdgpu_device *adev,
					     struct amdgpu_irq_src *source,
					     unsigned type,
					     enum amdgpu_interrupt_state state)
{
	u32 mec_int_cntl_reg, mec_int_cntl;
	int i, j, k, num_xcc;

	num_xcc = NUM_XCC(adev->gfx.xcc_mask);
	switch (state) {
	case AMDGPU_IRQ_STATE_DISABLE:
	case AMDGPU_IRQ_STATE_ENABLE:
		for (i = 0; i < num_xcc; i++) {
			WREG32_FIELD15_PREREG(GC, GET_INST(GC, i), CP_INT_CNTL_RING0,
					      PRIV_REG_INT_ENABLE,
					      state == AMDGPU_IRQ_STATE_ENABLE ? 1 : 0);
			for (j = 0; j < adev->gfx.mec.num_mec; j++) {
				for (k = 0; k < adev->gfx.mec.num_pipe_per_mec; k++) {
					/* MECs start at 1 */
					mec_int_cntl_reg = gfx_v9_4_3_get_cpc_int_cntl(adev, i, j + 1, k);

					if (mec_int_cntl_reg) {
						mec_int_cntl = RREG32_XCC(mec_int_cntl_reg, i);
						mec_int_cntl = REG_SET_FIELD(mec_int_cntl, CP_ME1_PIPE0_INT_CNTL,
									     PRIV_REG_INT_ENABLE,
									     state == AMDGPU_IRQ_STATE_ENABLE ?
									     1 : 0);
						WREG32_XCC(mec_int_cntl_reg, mec_int_cntl, i);
					}
				}
			}
		}
		break;
	default:
		break;
	}

	return 0;
}

static int gfx_v9_4_3_set_bad_op_fault_state(struct amdgpu_device *adev,
					     struct amdgpu_irq_src *source,
					     unsigned type,
					     enum amdgpu_interrupt_state state)
{
	u32 mec_int_cntl_reg, mec_int_cntl;
	int i, j, k, num_xcc;

	num_xcc = NUM_XCC(adev->gfx.xcc_mask);
	switch (state) {
	case AMDGPU_IRQ_STATE_DISABLE:
	case AMDGPU_IRQ_STATE_ENABLE:
		for (i = 0; i < num_xcc; i++) {
			WREG32_FIELD15_PREREG(GC, GET_INST(GC, i), CP_INT_CNTL_RING0,
					      OPCODE_ERROR_INT_ENABLE,
					      state == AMDGPU_IRQ_STATE_ENABLE ? 1 : 0);
			for (j = 0; j < adev->gfx.mec.num_mec; j++) {
				for (k = 0; k < adev->gfx.mec.num_pipe_per_mec; k++) {
					/* MECs start at 1 */
					mec_int_cntl_reg = gfx_v9_4_3_get_cpc_int_cntl(adev, i, j + 1, k);

					if (mec_int_cntl_reg) {
						mec_int_cntl = RREG32_XCC(mec_int_cntl_reg, i);
						mec_int_cntl = REG_SET_FIELD(mec_int_cntl, CP_ME1_PIPE0_INT_CNTL,
									     OPCODE_ERROR_INT_ENABLE,
									     state == AMDGPU_IRQ_STATE_ENABLE ?
									     1 : 0);
						WREG32_XCC(mec_int_cntl_reg, mec_int_cntl, i);
					}
				}
			}
		}
		break;
	default:
		break;
	}

	return 0;
}

static int gfx_v9_4_3_set_priv_inst_fault_state(struct amdgpu_device *adev,
					      struct amdgpu_irq_src *source,
					      unsigned type,
					      enum amdgpu_interrupt_state state)
{
	int i, num_xcc;

	num_xcc = NUM_XCC(adev->gfx.xcc_mask);
	switch (state) {
	case AMDGPU_IRQ_STATE_DISABLE:
	case AMDGPU_IRQ_STATE_ENABLE:
		for (i = 0; i < num_xcc; i++)
			WREG32_FIELD15_PREREG(GC, GET_INST(GC, i), CP_INT_CNTL_RING0,
				PRIV_INSTR_INT_ENABLE,
				state == AMDGPU_IRQ_STATE_ENABLE ? 1 : 0);
		break;
	default:
		break;
	}

	return 0;
}

static int gfx_v9_4_3_set_eop_interrupt_state(struct amdgpu_device *adev,
					    struct amdgpu_irq_src *src,
					    unsigned type,
					    enum amdgpu_interrupt_state state)
{
	int i, num_xcc;

	num_xcc = NUM_XCC(adev->gfx.xcc_mask);
	for (i = 0; i < num_xcc; i++) {
		switch (type) {
		case AMDGPU_CP_IRQ_COMPUTE_MEC1_PIPE0_EOP:
			gfx_v9_4_3_xcc_set_compute_eop_interrupt_state(
				adev, 1, 0, state, i);
			break;
		case AMDGPU_CP_IRQ_COMPUTE_MEC1_PIPE1_EOP:
			gfx_v9_4_3_xcc_set_compute_eop_interrupt_state(
				adev, 1, 1, state, i);
			break;
		case AMDGPU_CP_IRQ_COMPUTE_MEC1_PIPE2_EOP:
			gfx_v9_4_3_xcc_set_compute_eop_interrupt_state(
				adev, 1, 2, state, i);
			break;
		case AMDGPU_CP_IRQ_COMPUTE_MEC1_PIPE3_EOP:
			gfx_v9_4_3_xcc_set_compute_eop_interrupt_state(
				adev, 1, 3, state, i);
			break;
		case AMDGPU_CP_IRQ_COMPUTE_MEC2_PIPE0_EOP:
			gfx_v9_4_3_xcc_set_compute_eop_interrupt_state(
				adev, 2, 0, state, i);
			break;
		case AMDGPU_CP_IRQ_COMPUTE_MEC2_PIPE1_EOP:
			gfx_v9_4_3_xcc_set_compute_eop_interrupt_state(
				adev, 2, 1, state, i);
			break;
		case AMDGPU_CP_IRQ_COMPUTE_MEC2_PIPE2_EOP:
			gfx_v9_4_3_xcc_set_compute_eop_interrupt_state(
				adev, 2, 2, state, i);
			break;
		case AMDGPU_CP_IRQ_COMPUTE_MEC2_PIPE3_EOP:
			gfx_v9_4_3_xcc_set_compute_eop_interrupt_state(
				adev, 2, 3, state, i);
			break;
		default:
			break;
		}
	}

	return 0;
}

static int gfx_v9_4_3_eop_irq(struct amdgpu_device *adev,
			    struct amdgpu_irq_src *source,
			    struct amdgpu_iv_entry *entry)
{
	int i, xcc_id;
	u8 me_id, pipe_id, queue_id;
	struct amdgpu_ring *ring;

	DRM_DEBUG("IH: CP EOP\n");
	me_id = (entry->ring_id & 0x0c) >> 2;
	pipe_id = (entry->ring_id & 0x03) >> 0;
	queue_id = (entry->ring_id & 0x70) >> 4;

	xcc_id = gfx_v9_4_3_ih_to_xcc_inst(adev, entry->node_id);

	if (xcc_id == -EINVAL)
		return -EINVAL;

	switch (me_id) {
	case 0:
	case 1:
	case 2:
		for (i = 0; i < adev->gfx.num_compute_rings; i++) {
			ring = &adev->gfx.compute_ring
					[i +
					 xcc_id * adev->gfx.num_compute_rings];
			/* Per-queue interrupt is supported for MEC starting from VI.
			  * The interrupt can only be enabled/disabled per pipe instead of per queue.
			  */

			if ((ring->me == me_id) && (ring->pipe == pipe_id) && (ring->queue == queue_id))
				amdgpu_fence_process(ring);
		}
		break;
	}
	return 0;
}

static void gfx_v9_4_3_fault(struct amdgpu_device *adev,
			   struct amdgpu_iv_entry *entry)
{
	u8 me_id, pipe_id, queue_id;
	struct amdgpu_ring *ring;
	int i, xcc_id;

	me_id = (entry->ring_id & 0x0c) >> 2;
	pipe_id = (entry->ring_id & 0x03) >> 0;
	queue_id = (entry->ring_id & 0x70) >> 4;

	xcc_id = gfx_v9_4_3_ih_to_xcc_inst(adev, entry->node_id);

	if (xcc_id == -EINVAL)
		return;

	switch (me_id) {
	case 0:
	case 1:
	case 2:
		for (i = 0; i < adev->gfx.num_compute_rings; i++) {
			ring = &adev->gfx.compute_ring
					[i +
					 xcc_id * adev->gfx.num_compute_rings];
			if (ring->me == me_id && ring->pipe == pipe_id &&
			    ring->queue == queue_id)
				drm_sched_fault(&ring->sched);
		}
		break;
	}
}

static int gfx_v9_4_3_priv_reg_irq(struct amdgpu_device *adev,
				 struct amdgpu_irq_src *source,
				 struct amdgpu_iv_entry *entry)
{
	DRM_ERROR("Illegal register access in command stream\n");
	gfx_v9_4_3_fault(adev, entry);
	return 0;
}

static int gfx_v9_4_3_bad_op_irq(struct amdgpu_device *adev,
				 struct amdgpu_irq_src *source,
				 struct amdgpu_iv_entry *entry)
{
	DRM_ERROR("Illegal opcode in command stream\n");
	gfx_v9_4_3_fault(adev, entry);
	return 0;
}

static int gfx_v9_4_3_priv_inst_irq(struct amdgpu_device *adev,
				  struct amdgpu_irq_src *source,
				  struct amdgpu_iv_entry *entry)
{
	DRM_ERROR("Illegal instruction in command stream\n");
	gfx_v9_4_3_fault(adev, entry);
	return 0;
}

static void gfx_v9_4_3_emit_mem_sync(struct amdgpu_ring *ring)
{
	const unsigned int cp_coher_cntl =
			PACKET3_ACQUIRE_MEM_CP_COHER_CNTL_SH_ICACHE_ACTION_ENA(1) |
			PACKET3_ACQUIRE_MEM_CP_COHER_CNTL_SH_KCACHE_ACTION_ENA(1) |
			PACKET3_ACQUIRE_MEM_CP_COHER_CNTL_TC_ACTION_ENA(1) |
			PACKET3_ACQUIRE_MEM_CP_COHER_CNTL_TCL1_ACTION_ENA(1) |
			PACKET3_ACQUIRE_MEM_CP_COHER_CNTL_TC_WB_ACTION_ENA(1);

	/* ACQUIRE_MEM -make one or more surfaces valid for use by the subsequent operations */
	amdgpu_ring_write(ring, PACKET3(PACKET3_ACQUIRE_MEM, 5));
	amdgpu_ring_write(ring, cp_coher_cntl); /* CP_COHER_CNTL */
	amdgpu_ring_write(ring, 0xffffffff);  /* CP_COHER_SIZE */
	amdgpu_ring_write(ring, 0xffffff);  /* CP_COHER_SIZE_HI */
	amdgpu_ring_write(ring, 0); /* CP_COHER_BASE */
	amdgpu_ring_write(ring, 0);  /* CP_COHER_BASE_HI */
	amdgpu_ring_write(ring, 0x0000000A); /* POLL_INTERVAL */
}

static void gfx_v9_4_3_emit_wave_limit_cs(struct amdgpu_ring *ring,
					uint32_t pipe, bool enable)
{
	struct amdgpu_device *adev = ring->adev;
	uint32_t val;
	uint32_t wcl_cs_reg;

	/* regSPI_WCL_PIPE_PERCENT_CS[0-7]_DEFAULT values are same */
	val = enable ? 0x1 : 0x7f;

	switch (pipe) {
	case 0:
		wcl_cs_reg = SOC15_REG_OFFSET(GC, GET_INST(GC, 0), regSPI_WCL_PIPE_PERCENT_CS0);
		break;
	case 1:
		wcl_cs_reg = SOC15_REG_OFFSET(GC, GET_INST(GC, 0), regSPI_WCL_PIPE_PERCENT_CS1);
		break;
	case 2:
		wcl_cs_reg = SOC15_REG_OFFSET(GC, GET_INST(GC, 0), regSPI_WCL_PIPE_PERCENT_CS2);
		break;
	case 3:
		wcl_cs_reg = SOC15_REG_OFFSET(GC, GET_INST(GC, 0), regSPI_WCL_PIPE_PERCENT_CS3);
		break;
	default:
		DRM_DEBUG("invalid pipe %d\n", pipe);
		return;
	}

	amdgpu_ring_emit_wreg(ring, wcl_cs_reg, val);

}
static void gfx_v9_4_3_emit_wave_limit(struct amdgpu_ring *ring, bool enable)
{
	struct amdgpu_device *adev = ring->adev;
	uint32_t val;
	int i;

	/* regSPI_WCL_PIPE_PERCENT_GFX is 7 bit multiplier register to limit
	 * number of gfx waves. Setting 5 bit will make sure gfx only gets
	 * around 25% of gpu resources.
	 */
	val = enable ? 0x1f : 0x07ffffff;
	amdgpu_ring_emit_wreg(ring,
			      SOC15_REG_OFFSET(GC, GET_INST(GC, 0), regSPI_WCL_PIPE_PERCENT_GFX),
			      val);

	/* Restrict waves for normal/low priority compute queues as well
	 * to get best QoS for high priority compute jobs.
	 *
	 * amdgpu controls only 1st ME(0-3 CS pipes).
	 */
	for (i = 0; i < adev->gfx.mec.num_pipe_per_mec; i++) {
		if (i != ring->pipe)
			gfx_v9_4_3_emit_wave_limit_cs(ring, i, enable);

	}
}

static int gfx_v9_4_3_unmap_done(struct amdgpu_device *adev, uint32_t me,
				uint32_t pipe, uint32_t queue,
				uint32_t xcc_id)
{
	int i, r;
	/* make sure dequeue is complete*/
	gfx_v9_4_3_xcc_set_safe_mode(adev, xcc_id);
	mutex_lock(&adev->srbm_mutex);
	soc15_grbm_select(adev, me, pipe, queue, 0, GET_INST(GC, xcc_id));
	for (i = 0; i < adev->usec_timeout; i++) {
		if (!(RREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_HQD_ACTIVE) & 1))
			break;
		udelay(1);
	}
	if (i >= adev->usec_timeout)
		r = -ETIMEDOUT;
	else
		r = 0;
	soc15_grbm_select(adev, 0, 0, 0, 0, GET_INST(GC, xcc_id));
	mutex_unlock(&adev->srbm_mutex);
	gfx_v9_4_3_xcc_unset_safe_mode(adev, xcc_id);

	return r;

}

static bool gfx_v9_4_3_pipe_reset_support(struct amdgpu_device *adev)
{
	/*TODO: Need check gfx9.4.4 mec fw whether supports pipe reset as well.*/
	if (amdgpu_ip_version(adev, GC_HWIP, 0) == IP_VERSION(9, 4, 3) &&
			adev->gfx.mec_fw_version >= 0x0000009b)
		return true;
	else
		dev_warn_once(adev->dev, "Please use the latest MEC version to see whether support pipe reset\n");

	return false;
}

static int gfx_v9_4_3_reset_hw_pipe(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	uint32_t reset_pipe, clean_pipe;
	int r;

	if (!gfx_v9_4_3_pipe_reset_support(adev))
		return -EINVAL;

	gfx_v9_4_3_xcc_set_safe_mode(adev, ring->xcc_id);
	mutex_lock(&adev->srbm_mutex);

	reset_pipe = RREG32_SOC15(GC, GET_INST(GC, ring->xcc_id), regCP_MEC_CNTL);
	clean_pipe = reset_pipe;

	if (ring->me == 1) {
		switch (ring->pipe) {
		case 0:
			reset_pipe = REG_SET_FIELD(reset_pipe, CP_MEC_CNTL,
						   MEC_ME1_PIPE0_RESET, 1);
			break;
		case 1:
			reset_pipe = REG_SET_FIELD(reset_pipe, CP_MEC_CNTL,
						   MEC_ME1_PIPE1_RESET, 1);
			break;
		case 2:
			reset_pipe = REG_SET_FIELD(reset_pipe, CP_MEC_CNTL,
						   MEC_ME1_PIPE2_RESET, 1);
			break;
		case 3:
			reset_pipe = REG_SET_FIELD(reset_pipe, CP_MEC_CNTL,
						   MEC_ME1_PIPE3_RESET, 1);
			break;
		default:
			break;
		}
	} else {
		if (ring->pipe)
			reset_pipe = REG_SET_FIELD(reset_pipe, CP_MEC_CNTL,
						   MEC_ME2_PIPE1_RESET, 1);
		else
			reset_pipe = REG_SET_FIELD(reset_pipe, CP_MEC_CNTL,
						   MEC_ME2_PIPE0_RESET, 1);
	}

	WREG32_SOC15(GC, GET_INST(GC, ring->xcc_id), regCP_MEC_CNTL, reset_pipe);
	WREG32_SOC15(GC, GET_INST(GC, ring->xcc_id), regCP_MEC_CNTL, clean_pipe);
	mutex_unlock(&adev->srbm_mutex);
	gfx_v9_4_3_xcc_unset_safe_mode(adev, ring->xcc_id);

	r = gfx_v9_4_3_unmap_done(adev, ring->me, ring->pipe, ring->queue, ring->xcc_id);
	return r;
}

static int gfx_v9_4_3_reset_kcq(struct amdgpu_ring *ring,
				unsigned int vmid)
{
	struct amdgpu_device *adev = ring->adev;
	struct amdgpu_kiq *kiq = &adev->gfx.kiq[ring->xcc_id];
	struct amdgpu_ring *kiq_ring = &kiq->ring;
	unsigned long flags;
	int r;

	if (amdgpu_sriov_vf(adev))
		return -EINVAL;

	if (!kiq->pmf || !kiq->pmf->kiq_unmap_queues)
		return -EINVAL;

	spin_lock_irqsave(&kiq->ring_lock, flags);

	if (amdgpu_ring_alloc(kiq_ring, kiq->pmf->unmap_queues_size)) {
		spin_unlock_irqrestore(&kiq->ring_lock, flags);
		return -ENOMEM;
	}

	kiq->pmf->kiq_unmap_queues(kiq_ring, ring, RESET_QUEUES,
				   0, 0);
	amdgpu_ring_commit(kiq_ring);

	spin_unlock_irqrestore(&kiq->ring_lock, flags);

	r = amdgpu_ring_test_ring(kiq_ring);
	if (r) {
		dev_err(adev->dev, "kiq ring test failed after ring: %s queue reset\n",
				ring->name);
		goto pipe_reset;
	}

	r = gfx_v9_4_3_unmap_done(adev, ring->me, ring->pipe, ring->queue, ring->xcc_id);
	if (r)
		dev_err(adev->dev, "fail to wait on hqd deactive and will try pipe reset\n");

pipe_reset:
	if(r) {
		r = gfx_v9_4_3_reset_hw_pipe(ring);
		dev_info(adev->dev, "ring: %s pipe reset :%s\n", ring->name,
				r ? "failed" : "successfully");
		if (r)
			return r;
	}

	r = amdgpu_bo_reserve(ring->mqd_obj, false);
	if (unlikely(r != 0)){
		dev_err(adev->dev, "fail to resv mqd_obj\n");
		return r;
	}
	r = amdgpu_bo_kmap(ring->mqd_obj, (void **)&ring->mqd_ptr);
	if (!r) {
		r = gfx_v9_4_3_xcc_kcq_init_queue(ring, ring->xcc_id, true);
		amdgpu_bo_kunmap(ring->mqd_obj);
		ring->mqd_ptr = NULL;
	}
	amdgpu_bo_unreserve(ring->mqd_obj);
	if (r) {
		dev_err(adev->dev, "fail to unresv mqd_obj\n");
		return r;
	}
	spin_lock_irqsave(&kiq->ring_lock, flags);
	r = amdgpu_ring_alloc(kiq_ring, kiq->pmf->map_queues_size);
	if (r) {
		spin_unlock_irqrestore(&kiq->ring_lock, flags);
		return -ENOMEM;
	}
	kiq->pmf->kiq_map_queues(kiq_ring, ring);
	amdgpu_ring_commit(kiq_ring);
	spin_unlock_irqrestore(&kiq->ring_lock, flags);

	r = amdgpu_ring_test_ring(kiq_ring);
	if (r) {
		dev_err(adev->dev, "fail to remap queue\n");
		return r;
	}
	return amdgpu_ring_test_ring(ring);
}

enum amdgpu_gfx_cp_ras_mem_id {
	AMDGPU_GFX_CP_MEM1 = 1,
	AMDGPU_GFX_CP_MEM2,
	AMDGPU_GFX_CP_MEM3,
	AMDGPU_GFX_CP_MEM4,
	AMDGPU_GFX_CP_MEM5,
};

enum amdgpu_gfx_gcea_ras_mem_id {
	AMDGPU_GFX_GCEA_IOWR_CMDMEM = 4,
	AMDGPU_GFX_GCEA_IORD_CMDMEM,
	AMDGPU_GFX_GCEA_GMIWR_CMDMEM,
	AMDGPU_GFX_GCEA_GMIRD_CMDMEM,
	AMDGPU_GFX_GCEA_DRAMWR_CMDMEM,
	AMDGPU_GFX_GCEA_DRAMRD_CMDMEM,
	AMDGPU_GFX_GCEA_MAM_DMEM0,
	AMDGPU_GFX_GCEA_MAM_DMEM1,
	AMDGPU_GFX_GCEA_MAM_DMEM2,
	AMDGPU_GFX_GCEA_MAM_DMEM3,
	AMDGPU_GFX_GCEA_MAM_AMEM0,
	AMDGPU_GFX_GCEA_MAM_AMEM1,
	AMDGPU_GFX_GCEA_MAM_AMEM2,
	AMDGPU_GFX_GCEA_MAM_AMEM3,
	AMDGPU_GFX_GCEA_MAM_AFLUSH_BUFFER,
	AMDGPU_GFX_GCEA_WRET_TAGMEM,
	AMDGPU_GFX_GCEA_RRET_TAGMEM,
	AMDGPU_GFX_GCEA_IOWR_DATAMEM,
	AMDGPU_GFX_GCEA_GMIWR_DATAMEM,
	AMDGPU_GFX_GCEA_DRAM_DATAMEM,
};

enum amdgpu_gfx_gc_cane_ras_mem_id {
	AMDGPU_GFX_GC_CANE_MEM0 = 0,
};

enum amdgpu_gfx_gcutcl2_ras_mem_id {
	AMDGPU_GFX_GCUTCL2_MEM2P512X95 = 160,
};

enum amdgpu_gfx_gds_ras_mem_id {
	AMDGPU_GFX_GDS_MEM0 = 0,
};

enum amdgpu_gfx_lds_ras_mem_id {
	AMDGPU_GFX_LDS_BANK0 = 0,
	AMDGPU_GFX_LDS_BANK1,
	AMDGPU_GFX_LDS_BANK2,
	AMDGPU_GFX_LDS_BANK3,
	AMDGPU_GFX_LDS_BANK4,
	AMDGPU_GFX_LDS_BANK5,
	AMDGPU_GFX_LDS_BANK6,
	AMDGPU_GFX_LDS_BANK7,
	AMDGPU_GFX_LDS_BANK8,
	AMDGPU_GFX_LDS_BANK9,
	AMDGPU_GFX_LDS_BANK10,
	AMDGPU_GFX_LDS_BANK11,
	AMDGPU_GFX_LDS_BANK12,
	AMDGPU_GFX_LDS_BANK13,
	AMDGPU_GFX_LDS_BANK14,
	AMDGPU_GFX_LDS_BANK15,
	AMDGPU_GFX_LDS_BANK16,
	AMDGPU_GFX_LDS_BANK17,
	AMDGPU_GFX_LDS_BANK18,
	AMDGPU_GFX_LDS_BANK19,
	AMDGPU_GFX_LDS_BANK20,
	AMDGPU_GFX_LDS_BANK21,
	AMDGPU_GFX_LDS_BANK22,
	AMDGPU_GFX_LDS_BANK23,
	AMDGPU_GFX_LDS_BANK24,
	AMDGPU_GFX_LDS_BANK25,
	AMDGPU_GFX_LDS_BANK26,
	AMDGPU_GFX_LDS_BANK27,
	AMDGPU_GFX_LDS_BANK28,
	AMDGPU_GFX_LDS_BANK29,
	AMDGPU_GFX_LDS_BANK30,
	AMDGPU_GFX_LDS_BANK31,
	AMDGPU_GFX_LDS_SP_BUFFER_A,
	AMDGPU_GFX_LDS_SP_BUFFER_B,
};

enum amdgpu_gfx_rlc_ras_mem_id {
	AMDGPU_GFX_RLC_GPMF32 = 1,
	AMDGPU_GFX_RLC_RLCVF32,
	AMDGPU_GFX_RLC_SCRATCH,
	AMDGPU_GFX_RLC_SRM_ARAM,
	AMDGPU_GFX_RLC_SRM_DRAM,
	AMDGPU_GFX_RLC_TCTAG,
	AMDGPU_GFX_RLC_SPM_SE,
	AMDGPU_GFX_RLC_SPM_GRBMT,
};

enum amdgpu_gfx_sp_ras_mem_id {
	AMDGPU_GFX_SP_SIMDID0 = 0,
};

enum amdgpu_gfx_spi_ras_mem_id {
	AMDGPU_GFX_SPI_MEM0 = 0,
	AMDGPU_GFX_SPI_MEM1,
	AMDGPU_GFX_SPI_MEM2,
	AMDGPU_GFX_SPI_MEM3,
};

enum amdgpu_gfx_sqc_ras_mem_id {
	AMDGPU_GFX_SQC_INST_CACHE_A = 100,
	AMDGPU_GFX_SQC_INST_CACHE_B = 101,
	AMDGPU_GFX_SQC_INST_CACHE_TAG_A = 102,
	AMDGPU_GFX_SQC_INST_CACHE_TAG_B = 103,
	AMDGPU_GFX_SQC_INST_CACHE_MISS_FIFO_A = 104,
	AMDGPU_GFX_SQC_INST_CACHE_MISS_FIFO_B = 105,
	AMDGPU_GFX_SQC_INST_CACHE_GATCL1_MISS_FIFO_A = 106,
	AMDGPU_GFX_SQC_INST_CACHE_GATCL1_MISS_FIFO_B = 107,
	AMDGPU_GFX_SQC_DATA_CACHE_A = 200,
	AMDGPU_GFX_SQC_DATA_CACHE_B = 201,
	AMDGPU_GFX_SQC_DATA_CACHE_TAG_A = 202,
	AMDGPU_GFX_SQC_DATA_CACHE_TAG_B = 203,
	AMDGPU_GFX_SQC_DATA_CACHE_MISS_FIFO_A = 204,
	AMDGPU_GFX_SQC_DATA_CACHE_MISS_FIFO_B = 205,
	AMDGPU_GFX_SQC_DATA_CACHE_HIT_FIFO_A = 206,
	AMDGPU_GFX_SQC_DATA_CACHE_HIT_FIFO_B = 207,
	AMDGPU_GFX_SQC_DIRTY_BIT_A = 208,
	AMDGPU_GFX_SQC_DIRTY_BIT_B = 209,
	AMDGPU_GFX_SQC_WRITE_DATA_BUFFER_CU0 = 210,
	AMDGPU_GFX_SQC_WRITE_DATA_BUFFER_CU1 = 211,
	AMDGPU_GFX_SQC_UTCL1_MISS_LFIFO_DATA_CACHE_A = 212,
	AMDGPU_GFX_SQC_UTCL1_MISS_LFIFO_DATA_CACHE_B = 213,
	AMDGPU_GFX_SQC_UTCL1_MISS_LFIFO_INST_CACHE = 108,
};

enum amdgpu_gfx_sq_ras_mem_id {
	AMDGPU_GFX_SQ_SGPR_MEM0 = 0,
	AMDGPU_GFX_SQ_SGPR_MEM1,
	AMDGPU_GFX_SQ_SGPR_MEM2,
	AMDGPU_GFX_SQ_SGPR_MEM3,
};

enum amdgpu_gfx_ta_ras_mem_id {
	AMDGPU_GFX_TA_FS_AFIFO_RAM_LO = 1,
	AMDGPU_GFX_TA_FS_AFIFO_RAM_HI,
	AMDGPU_GFX_TA_FS_CFIFO_RAM,
	AMDGPU_GFX_TA_FSX_LFIFO,
	AMDGPU_GFX_TA_FS_DFIFO_RAM,
};

enum amdgpu_gfx_tcc_ras_mem_id {
	AMDGPU_GFX_TCC_MEM1 = 1,
};

enum amdgpu_gfx_tca_ras_mem_id {
	AMDGPU_GFX_TCA_MEM1 = 1,
};

enum amdgpu_gfx_tci_ras_mem_id {
	AMDGPU_GFX_TCIW_MEM = 1,
};

enum amdgpu_gfx_tcp_ras_mem_id {
	AMDGPU_GFX_TCP_LFIFO0 = 1,
	AMDGPU_GFX_TCP_SET0BANK0_RAM,
	AMDGPU_GFX_TCP_SET0BANK1_RAM,
	AMDGPU_GFX_TCP_SET0BANK2_RAM,
	AMDGPU_GFX_TCP_SET0BANK3_RAM,
	AMDGPU_GFX_TCP_SET1BANK0_RAM,
	AMDGPU_GFX_TCP_SET1BANK1_RAM,
	AMDGPU_GFX_TCP_SET1BANK2_RAM,
	AMDGPU_GFX_TCP_SET1BANK3_RAM,
	AMDGPU_GFX_TCP_SET2BANK0_RAM,
	AMDGPU_GFX_TCP_SET2BANK1_RAM,
	AMDGPU_GFX_TCP_SET2BANK2_RAM,
	AMDGPU_GFX_TCP_SET2BANK3_RAM,
	AMDGPU_GFX_TCP_SET3BANK0_RAM,
	AMDGPU_GFX_TCP_SET3BANK1_RAM,
	AMDGPU_GFX_TCP_SET3BANK2_RAM,
	AMDGPU_GFX_TCP_SET3BANK3_RAM,
	AMDGPU_GFX_TCP_VM_FIFO,
	AMDGPU_GFX_TCP_DB_TAGRAM0,
	AMDGPU_GFX_TCP_DB_TAGRAM1,
	AMDGPU_GFX_TCP_DB_TAGRAM2,
	AMDGPU_GFX_TCP_DB_TAGRAM3,
	AMDGPU_GFX_TCP_UTCL1_LFIFO_PROBE0,
	AMDGPU_GFX_TCP_UTCL1_LFIFO_PROBE1,
	AMDGPU_GFX_TCP_CMD_FIFO,
};

enum amdgpu_gfx_td_ras_mem_id {
	AMDGPU_GFX_TD_UTD_CS_FIFO_MEM = 1,
	AMDGPU_GFX_TD_UTD_SS_FIFO_LO_MEM,
	AMDGPU_GFX_TD_UTD_SS_FIFO_HI_MEM,
};

enum amdgpu_gfx_tcx_ras_mem_id {
	AMDGPU_GFX_TCX_FIFOD0 = 0,
	AMDGPU_GFX_TCX_FIFOD1,
	AMDGPU_GFX_TCX_FIFOD2,
	AMDGPU_GFX_TCX_FIFOD3,
	AMDGPU_GFX_TCX_FIFOD4,
	AMDGPU_GFX_TCX_FIFOD5,
	AMDGPU_GFX_TCX_FIFOD6,
	AMDGPU_GFX_TCX_FIFOD7,
	AMDGPU_GFX_TCX_FIFOB0,
	AMDGPU_GFX_TCX_FIFOB1,
	AMDGPU_GFX_TCX_FIFOB2,
	AMDGPU_GFX_TCX_FIFOB3,
	AMDGPU_GFX_TCX_FIFOB4,
	AMDGPU_GFX_TCX_FIFOB5,
	AMDGPU_GFX_TCX_FIFOB6,
	AMDGPU_GFX_TCX_FIFOB7,
	AMDGPU_GFX_TCX_FIFOA0,
	AMDGPU_GFX_TCX_FIFOA1,
	AMDGPU_GFX_TCX_FIFOA2,
	AMDGPU_GFX_TCX_FIFOA3,
	AMDGPU_GFX_TCX_FIFOA4,
	AMDGPU_GFX_TCX_FIFOA5,
	AMDGPU_GFX_TCX_FIFOA6,
	AMDGPU_GFX_TCX_FIFOA7,
	AMDGPU_GFX_TCX_CFIFO0,
	AMDGPU_GFX_TCX_CFIFO1,
	AMDGPU_GFX_TCX_CFIFO2,
	AMDGPU_GFX_TCX_CFIFO3,
	AMDGPU_GFX_TCX_CFIFO4,
	AMDGPU_GFX_TCX_CFIFO5,
	AMDGPU_GFX_TCX_CFIFO6,
	AMDGPU_GFX_TCX_CFIFO7,
	AMDGPU_GFX_TCX_FIFO_ACKB0,
	AMDGPU_GFX_TCX_FIFO_ACKB1,
	AMDGPU_GFX_TCX_FIFO_ACKB2,
	AMDGPU_GFX_TCX_FIFO_ACKB3,
	AMDGPU_GFX_TCX_FIFO_ACKB4,
	AMDGPU_GFX_TCX_FIFO_ACKB5,
	AMDGPU_GFX_TCX_FIFO_ACKB6,
	AMDGPU_GFX_TCX_FIFO_ACKB7,
	AMDGPU_GFX_TCX_FIFO_ACKD0,
	AMDGPU_GFX_TCX_FIFO_ACKD1,
	AMDGPU_GFX_TCX_FIFO_ACKD2,
	AMDGPU_GFX_TCX_FIFO_ACKD3,
	AMDGPU_GFX_TCX_FIFO_ACKD4,
	AMDGPU_GFX_TCX_FIFO_ACKD5,
	AMDGPU_GFX_TCX_FIFO_ACKD6,
	AMDGPU_GFX_TCX_FIFO_ACKD7,
	AMDGPU_GFX_TCX_DST_FIFOA0,
	AMDGPU_GFX_TCX_DST_FIFOA1,
	AMDGPU_GFX_TCX_DST_FIFOA2,
	AMDGPU_GFX_TCX_DST_FIFOA3,
	AMDGPU_GFX_TCX_DST_FIFOA4,
	AMDGPU_GFX_TCX_DST_FIFOA5,
	AMDGPU_GFX_TCX_DST_FIFOA6,
	AMDGPU_GFX_TCX_DST_FIFOA7,
	AMDGPU_GFX_TCX_DST_FIFOB0,
	AMDGPU_GFX_TCX_DST_FIFOB1,
	AMDGPU_GFX_TCX_DST_FIFOB2,
	AMDGPU_GFX_TCX_DST_FIFOB3,
	AMDGPU_GFX_TCX_DST_FIFOB4,
	AMDGPU_GFX_TCX_DST_FIFOB5,
	AMDGPU_GFX_TCX_DST_FIFOB6,
	AMDGPU_GFX_TCX_DST_FIFOB7,
	AMDGPU_GFX_TCX_DST_FIFOD0,
	AMDGPU_GFX_TCX_DST_FIFOD1,
	AMDGPU_GFX_TCX_DST_FIFOD2,
	AMDGPU_GFX_TCX_DST_FIFOD3,
	AMDGPU_GFX_TCX_DST_FIFOD4,
	AMDGPU_GFX_TCX_DST_FIFOD5,
	AMDGPU_GFX_TCX_DST_FIFOD6,
	AMDGPU_GFX_TCX_DST_FIFOD7,
	AMDGPU_GFX_TCX_DST_FIFO_ACKB0,
	AMDGPU_GFX_TCX_DST_FIFO_ACKB1,
	AMDGPU_GFX_TCX_DST_FIFO_ACKB2,
	AMDGPU_GFX_TCX_DST_FIFO_ACKB3,
	AMDGPU_GFX_TCX_DST_FIFO_ACKB4,
	AMDGPU_GFX_TCX_DST_FIFO_ACKB5,
	AMDGPU_GFX_TCX_DST_FIFO_ACKB6,
	AMDGPU_GFX_TCX_DST_FIFO_ACKB7,
	AMDGPU_GFX_TCX_DST_FIFO_ACKD0,
	AMDGPU_GFX_TCX_DST_FIFO_ACKD1,
	AMDGPU_GFX_TCX_DST_FIFO_ACKD2,
	AMDGPU_GFX_TCX_DST_FIFO_ACKD3,
	AMDGPU_GFX_TCX_DST_FIFO_ACKD4,
	AMDGPU_GFX_TCX_DST_FIFO_ACKD5,
	AMDGPU_GFX_TCX_DST_FIFO_ACKD6,
	AMDGPU_GFX_TCX_DST_FIFO_ACKD7,
};

enum amdgpu_gfx_atc_l2_ras_mem_id {
	AMDGPU_GFX_ATC_L2_MEM0 = 0,
};

enum amdgpu_gfx_utcl2_ras_mem_id {
	AMDGPU_GFX_UTCL2_MEM0 = 0,
};

enum amdgpu_gfx_vml2_ras_mem_id {
	AMDGPU_GFX_VML2_MEM0 = 0,
};

enum amdgpu_gfx_vml2_walker_ras_mem_id {
	AMDGPU_GFX_VML2_WALKER_MEM0 = 0,
};

static const struct amdgpu_ras_memory_id_entry gfx_v9_4_3_ras_cp_mem_list[] = {
	{AMDGPU_GFX_CP_MEM1, "CP_MEM1"},
	{AMDGPU_GFX_CP_MEM2, "CP_MEM2"},
	{AMDGPU_GFX_CP_MEM3, "CP_MEM3"},
	{AMDGPU_GFX_CP_MEM4, "CP_MEM4"},
	{AMDGPU_GFX_CP_MEM5, "CP_MEM5"},
};

static const struct amdgpu_ras_memory_id_entry gfx_v9_4_3_ras_gcea_mem_list[] = {
	{AMDGPU_GFX_GCEA_IOWR_CMDMEM, "GCEA_IOWR_CMDMEM"},
	{AMDGPU_GFX_GCEA_IORD_CMDMEM, "GCEA_IORD_CMDMEM"},
	{AMDGPU_GFX_GCEA_GMIWR_CMDMEM, "GCEA_GMIWR_CMDMEM"},
	{AMDGPU_GFX_GCEA_GMIRD_CMDMEM, "GCEA_GMIRD_CMDMEM"},
	{AMDGPU_GFX_GCEA_DRAMWR_CMDMEM, "GCEA_DRAMWR_CMDMEM"},
	{AMDGPU_GFX_GCEA_DRAMRD_CMDMEM, "GCEA_DRAMRD_CMDMEM"},
	{AMDGPU_GFX_GCEA_MAM_DMEM0, "GCEA_MAM_DMEM0"},
	{AMDGPU_GFX_GCEA_MAM_DMEM1, "GCEA_MAM_DMEM1"},
	{AMDGPU_GFX_GCEA_MAM_DMEM2, "GCEA_MAM_DMEM2"},
	{AMDGPU_GFX_GCEA_MAM_DMEM3, "GCEA_MAM_DMEM3"},
	{AMDGPU_GFX_GCEA_MAM_AMEM0, "GCEA_MAM_AMEM0"},
	{AMDGPU_GFX_GCEA_MAM_AMEM1, "GCEA_MAM_AMEM1"},
	{AMDGPU_GFX_GCEA_MAM_AMEM2, "GCEA_MAM_AMEM2"},
	{AMDGPU_GFX_GCEA_MAM_AMEM3, "GCEA_MAM_AMEM3"},
	{AMDGPU_GFX_GCEA_MAM_AFLUSH_BUFFER, "GCEA_MAM_AFLUSH_BUFFER"},
	{AMDGPU_GFX_GCEA_WRET_TAGMEM, "GCEA_WRET_TAGMEM"},
	{AMDGPU_GFX_GCEA_RRET_TAGMEM, "GCEA_RRET_TAGMEM"},
	{AMDGPU_GFX_GCEA_IOWR_DATAMEM, "GCEA_IOWR_DATAMEM"},
	{AMDGPU_GFX_GCEA_GMIWR_DATAMEM, "GCEA_GMIWR_DATAMEM"},
	{AMDGPU_GFX_GCEA_DRAM_DATAMEM, "GCEA_DRAM_DATAMEM"},
};

static const struct amdgpu_ras_memory_id_entry gfx_v9_4_3_ras_gc_cane_mem_list[] = {
	{AMDGPU_GFX_GC_CANE_MEM0, "GC_CANE_MEM0"},
};

static const struct amdgpu_ras_memory_id_entry gfx_v9_4_3_ras_gcutcl2_mem_list[] = {
	{AMDGPU_GFX_GCUTCL2_MEM2P512X95, "GCUTCL2_MEM2P512X95"},
};

static const struct amdgpu_ras_memory_id_entry gfx_v9_4_3_ras_gds_mem_list[] = {
	{AMDGPU_GFX_GDS_MEM0, "GDS_MEM"},
};

static const struct amdgpu_ras_memory_id_entry gfx_v9_4_3_ras_lds_mem_list[] = {
	{AMDGPU_GFX_LDS_BANK0, "LDS_BANK0"},
	{AMDGPU_GFX_LDS_BANK1, "LDS_BANK1"},
	{AMDGPU_GFX_LDS_BANK2, "LDS_BANK2"},
	{AMDGPU_GFX_LDS_BANK3, "LDS_BANK3"},
	{AMDGPU_GFX_LDS_BANK4, "LDS_BANK4"},
	{AMDGPU_GFX_LDS_BANK5, "LDS_BANK5"},
	{AMDGPU_GFX_LDS_BANK6, "LDS_BANK6"},
	{AMDGPU_GFX_LDS_BANK7, "LDS_BANK7"},
	{AMDGPU_GFX_LDS_BANK8, "LDS_BANK8"},
	{AMDGPU_GFX_LDS_BANK9, "LDS_BANK9"},
	{AMDGPU_GFX_LDS_BANK10, "LDS_BANK10"},
	{AMDGPU_GFX_LDS_BANK11, "LDS_BANK11"},
	{AMDGPU_GFX_LDS_BANK12, "LDS_BANK12"},
	{AMDGPU_GFX_LDS_BANK13, "LDS_BANK13"},
	{AMDGPU_GFX_LDS_BANK14, "LDS_BANK14"},
	{AMDGPU_GFX_LDS_BANK15, "LDS_BANK15"},
	{AMDGPU_GFX_LDS_BANK16, "LDS_BANK16"},
	{AMDGPU_GFX_LDS_BANK17, "LDS_BANK17"},
	{AMDGPU_GFX_LDS_BANK18, "LDS_BANK18"},
	{AMDGPU_GFX_LDS_BANK19, "LDS_BANK19"},
	{AMDGPU_GFX_LDS_BANK20, "LDS_BANK20"},
	{AMDGPU_GFX_LDS_BANK21, "LDS_BANK21"},
	{AMDGPU_GFX_LDS_BANK22, "LDS_BANK22"},
	{AMDGPU_GFX_LDS_BANK23, "LDS_BANK23"},
	{AMDGPU_GFX_LDS_BANK24, "LDS_BANK24"},
	{AMDGPU_GFX_LDS_BANK25, "LDS_BANK25"},
	{AMDGPU_GFX_LDS_BANK26, "LDS_BANK26"},
	{AMDGPU_GFX_LDS_BANK27, "LDS_BANK27"},
	{AMDGPU_GFX_LDS_BANK28, "LDS_BANK28"},
	{AMDGPU_GFX_LDS_BANK29, "LDS_BANK29"},
	{AMDGPU_GFX_LDS_BANK30, "LDS_BANK30"},
	{AMDGPU_GFX_LDS_BANK31, "LDS_BANK31"},
	{AMDGPU_GFX_LDS_SP_BUFFER_A, "LDS_SP_BUFFER_A"},
	{AMDGPU_GFX_LDS_SP_BUFFER_B, "LDS_SP_BUFFER_B"},
};

static const struct amdgpu_ras_memory_id_entry gfx_v9_4_3_ras_rlc_mem_list[] = {
	{AMDGPU_GFX_RLC_GPMF32, "RLC_GPMF32"},
	{AMDGPU_GFX_RLC_RLCVF32, "RLC_RLCVF32"},
	{AMDGPU_GFX_RLC_SCRATCH, "RLC_SCRATCH"},
	{AMDGPU_GFX_RLC_SRM_ARAM, "RLC_SRM_ARAM"},
	{AMDGPU_GFX_RLC_SRM_DRAM, "RLC_SRM_DRAM"},
	{AMDGPU_GFX_RLC_TCTAG, "RLC_TCTAG"},
	{AMDGPU_GFX_RLC_SPM_SE, "RLC_SPM_SE"},
	{AMDGPU_GFX_RLC_SPM_GRBMT, "RLC_SPM_GRBMT"},
};

static const struct amdgpu_ras_memory_id_entry gfx_v9_4_3_ras_sp_mem_list[] = {
	{AMDGPU_GFX_SP_SIMDID0, "SP_SIMDID0"},
};

static const struct amdgpu_ras_memory_id_entry gfx_v9_4_3_ras_spi_mem_list[] = {
	{AMDGPU_GFX_SPI_MEM0, "SPI_MEM0"},
	{AMDGPU_GFX_SPI_MEM1, "SPI_MEM1"},
	{AMDGPU_GFX_SPI_MEM2, "SPI_MEM2"},
	{AMDGPU_GFX_SPI_MEM3, "SPI_MEM3"},
};

static const struct amdgpu_ras_memory_id_entry gfx_v9_4_3_ras_sqc_mem_list[] = {
	{AMDGPU_GFX_SQC_INST_CACHE_A, "SQC_INST_CACHE_A"},
	{AMDGPU_GFX_SQC_INST_CACHE_B, "SQC_INST_CACHE_B"},
	{AMDGPU_GFX_SQC_INST_CACHE_TAG_A, "SQC_INST_CACHE_TAG_A"},
	{AMDGPU_GFX_SQC_INST_CACHE_TAG_B, "SQC_INST_CACHE_TAG_B"},
	{AMDGPU_GFX_SQC_INST_CACHE_MISS_FIFO_A, "SQC_INST_CACHE_MISS_FIFO_A"},
	{AMDGPU_GFX_SQC_INST_CACHE_MISS_FIFO_B, "SQC_INST_CACHE_MISS_FIFO_B"},
	{AMDGPU_GFX_SQC_INST_CACHE_GATCL1_MISS_FIFO_A, "SQC_INST_CACHE_GATCL1_MISS_FIFO_A"},
	{AMDGPU_GFX_SQC_INST_CACHE_GATCL1_MISS_FIFO_B, "SQC_INST_CACHE_GATCL1_MISS_FIFO_B"},
	{AMDGPU_GFX_SQC_DATA_CACHE_A, "SQC_DATA_CACHE_A"},
	{AMDGPU_GFX_SQC_DATA_CACHE_B, "SQC_DATA_CACHE_B"},
	{AMDGPU_GFX_SQC_DATA_CACHE_TAG_A, "SQC_DATA_CACHE_TAG_A"},
	{AMDGPU_GFX_SQC_DATA_CACHE_TAG_B, "SQC_DATA_CACHE_TAG_B"},
	{AMDGPU_GFX_SQC_DATA_CACHE_MISS_FIFO_A, "SQC_DATA_CACHE_MISS_FIFO_A"},
	{AMDGPU_GFX_SQC_DATA_CACHE_MISS_FIFO_B, "SQC_DATA_CACHE_MISS_FIFO_B"},
	{AMDGPU_GFX_SQC_DATA_CACHE_HIT_FIFO_A, "SQC_DATA_CACHE_HIT_FIFO_A"},
	{AMDGPU_GFX_SQC_DATA_CACHE_HIT_FIFO_B, "SQC_DATA_CACHE_HIT_FIFO_B"},
	{AMDGPU_GFX_SQC_DIRTY_BIT_A, "SQC_DIRTY_BIT_A"},
	{AMDGPU_GFX_SQC_DIRTY_BIT_B, "SQC_DIRTY_BIT_B"},
	{AMDGPU_GFX_SQC_WRITE_DATA_BUFFER_CU0, "SQC_WRITE_DATA_BUFFER_CU0"},
	{AMDGPU_GFX_SQC_WRITE_DATA_BUFFER_CU1, "SQC_WRITE_DATA_BUFFER_CU1"},
	{AMDGPU_GFX_SQC_UTCL1_MISS_LFIFO_DATA_CACHE_A, "SQC_UTCL1_MISS_LFIFO_DATA_CACHE_A"},
	{AMDGPU_GFX_SQC_UTCL1_MISS_LFIFO_DATA_CACHE_B, "SQC_UTCL1_MISS_LFIFO_DATA_CACHE_B"},
	{AMDGPU_GFX_SQC_UTCL1_MISS_LFIFO_INST_CACHE, "SQC_UTCL1_MISS_LFIFO_INST_CACHE"},
};

static const struct amdgpu_ras_memory_id_entry gfx_v9_4_3_ras_sq_mem_list[] = {
	{AMDGPU_GFX_SQ_SGPR_MEM0, "SQ_SGPR_MEM0"},
	{AMDGPU_GFX_SQ_SGPR_MEM1, "SQ_SGPR_MEM1"},
	{AMDGPU_GFX_SQ_SGPR_MEM2, "SQ_SGPR_MEM2"},
	{AMDGPU_GFX_SQ_SGPR_MEM3, "SQ_SGPR_MEM3"},
};

static const struct amdgpu_ras_memory_id_entry gfx_v9_4_3_ras_ta_mem_list[] = {
	{AMDGPU_GFX_TA_FS_AFIFO_RAM_LO, "TA_FS_AFIFO_RAM_LO"},
	{AMDGPU_GFX_TA_FS_AFIFO_RAM_HI, "TA_FS_AFIFO_RAM_HI"},
	{AMDGPU_GFX_TA_FS_CFIFO_RAM, "TA_FS_CFIFO_RAM"},
	{AMDGPU_GFX_TA_FSX_LFIFO, "TA_FSX_LFIFO"},
	{AMDGPU_GFX_TA_FS_DFIFO_RAM, "TA_FS_DFIFO_RAM"},
};

static const struct amdgpu_ras_memory_id_entry gfx_v9_4_3_ras_tcc_mem_list[] = {
	{AMDGPU_GFX_TCC_MEM1, "TCC_MEM1"},
};

static const struct amdgpu_ras_memory_id_entry gfx_v9_4_3_ras_tca_mem_list[] = {
	{AMDGPU_GFX_TCA_MEM1, "TCA_MEM1"},
};

static const struct amdgpu_ras_memory_id_entry gfx_v9_4_3_ras_tci_mem_list[] = {
	{AMDGPU_GFX_TCIW_MEM, "TCIW_MEM"},
};

static const struct amdgpu_ras_memory_id_entry gfx_v9_4_3_ras_tcp_mem_list[] = {
	{AMDGPU_GFX_TCP_LFIFO0, "TCP_LFIFO0"},
	{AMDGPU_GFX_TCP_SET0BANK0_RAM, "TCP_SET0BANK0_RAM"},
	{AMDGPU_GFX_TCP_SET0BANK1_RAM, "TCP_SET0BANK1_RAM"},
	{AMDGPU_GFX_TCP_SET0BANK2_RAM, "TCP_SET0BANK2_RAM"},
	{AMDGPU_GFX_TCP_SET0BANK3_RAM, "TCP_SET0BANK3_RAM"},
	{AMDGPU_GFX_TCP_SET1BANK0_RAM, "TCP_SET1BANK0_RAM"},
	{AMDGPU_GFX_TCP_SET1BANK1_RAM, "TCP_SET1BANK1_RAM"},
	{AMDGPU_GFX_TCP_SET1BANK2_RAM, "TCP_SET1BANK2_RAM"},
	{AMDGPU_GFX_TCP_SET1BANK3_RAM, "TCP_SET1BANK3_RAM"},
	{AMDGPU_GFX_TCP_SET2BANK0_RAM, "TCP_SET2BANK0_RAM"},
	{AMDGPU_GFX_TCP_SET2BANK1_RAM, "TCP_SET2BANK1_RAM"},
	{AMDGPU_GFX_TCP_SET2BANK2_RAM, "TCP_SET2BANK2_RAM"},
	{AMDGPU_GFX_TCP_SET2BANK3_RAM, "TCP_SET2BANK3_RAM"},
	{AMDGPU_GFX_TCP_SET3BANK0_RAM, "TCP_SET3BANK0_RAM"},
	{AMDGPU_GFX_TCP_SET3BANK1_RAM, "TCP_SET3BANK1_RAM"},
	{AMDGPU_GFX_TCP_SET3BANK2_RAM, "TCP_SET3BANK2_RAM"},
	{AMDGPU_GFX_TCP_SET3BANK3_RAM, "TCP_SET3BANK3_RAM"},
	{AMDGPU_GFX_TCP_VM_FIFO, "TCP_VM_FIFO"},
	{AMDGPU_GFX_TCP_DB_TAGRAM0, "TCP_DB_TAGRAM0"},
	{AMDGPU_GFX_TCP_DB_TAGRAM1, "TCP_DB_TAGRAM1"},
	{AMDGPU_GFX_TCP_DB_TAGRAM2, "TCP_DB_TAGRAM2"},
	{AMDGPU_GFX_TCP_DB_TAGRAM3, "TCP_DB_TAGRAM3"},
	{AMDGPU_GFX_TCP_UTCL1_LFIFO_PROBE0, "TCP_UTCL1_LFIFO_PROBE0"},
	{AMDGPU_GFX_TCP_UTCL1_LFIFO_PROBE1, "TCP_UTCL1_LFIFO_PROBE1"},
	{AMDGPU_GFX_TCP_CMD_FIFO, "TCP_CMD_FIFO"},
};

static const struct amdgpu_ras_memory_id_entry gfx_v9_4_3_ras_td_mem_list[] = {
	{AMDGPU_GFX_TD_UTD_CS_FIFO_MEM, "TD_UTD_CS_FIFO_MEM"},
	{AMDGPU_GFX_TD_UTD_SS_FIFO_LO_MEM, "TD_UTD_SS_FIFO_LO_MEM"},
	{AMDGPU_GFX_TD_UTD_SS_FIFO_HI_MEM, "TD_UTD_SS_FIFO_HI_MEM"},
};

static const struct amdgpu_ras_memory_id_entry gfx_v9_4_3_ras_tcx_mem_list[] = {
	{AMDGPU_GFX_TCX_FIFOD0, "TCX_FIFOD0"},
	{AMDGPU_GFX_TCX_FIFOD1, "TCX_FIFOD1"},
	{AMDGPU_GFX_TCX_FIFOD2, "TCX_FIFOD2"},
	{AMDGPU_GFX_TCX_FIFOD3, "TCX_FIFOD3"},
	{AMDGPU_GFX_TCX_FIFOD4, "TCX_FIFOD4"},
	{AMDGPU_GFX_TCX_FIFOD5, "TCX_FIFOD5"},
	{AMDGPU_GFX_TCX_FIFOD6, "TCX_FIFOD6"},
	{AMDGPU_GFX_TCX_FIFOD7, "TCX_FIFOD7"},
	{AMDGPU_GFX_TCX_FIFOB0, "TCX_FIFOB0"},
	{AMDGPU_GFX_TCX_FIFOB1, "TCX_FIFOB1"},
	{AMDGPU_GFX_TCX_FIFOB2, "TCX_FIFOB2"},
	{AMDGPU_GFX_TCX_FIFOB3, "TCX_FIFOB3"},
	{AMDGPU_GFX_TCX_FIFOB4, "TCX_FIFOB4"},
	{AMDGPU_GFX_TCX_FIFOB5, "TCX_FIFOB5"},
	{AMDGPU_GFX_TCX_FIFOB6, "TCX_FIFOB6"},
	{AMDGPU_GFX_TCX_FIFOB7, "TCX_FIFOB7"},
	{AMDGPU_GFX_TCX_FIFOA0, "TCX_FIFOA0"},
	{AMDGPU_GFX_TCX_FIFOA1, "TCX_FIFOA1"},
	{AMDGPU_GFX_TCX_FIFOA2, "TCX_FIFOA2"},
	{AMDGPU_GFX_TCX_FIFOA3, "TCX_FIFOA3"},
	{AMDGPU_GFX_TCX_FIFOA4, "TCX_FIFOA4"},
	{AMDGPU_GFX_TCX_FIFOA5, "TCX_FIFOA5"},
	{AMDGPU_GFX_TCX_FIFOA6, "TCX_FIFOA6"},
	{AMDGPU_GFX_TCX_FIFOA7, "TCX_FIFOA7"},
	{AMDGPU_GFX_TCX_CFIFO0, "TCX_CFIFO0"},
	{AMDGPU_GFX_TCX_CFIFO1, "TCX_CFIFO1"},
	{AMDGPU_GFX_TCX_CFIFO2, "TCX_CFIFO2"},
	{AMDGPU_GFX_TCX_CFIFO3, "TCX_CFIFO3"},
	{AMDGPU_GFX_TCX_CFIFO4, "TCX_CFIFO4"},
	{AMDGPU_GFX_TCX_CFIFO5, "TCX_CFIFO5"},
	{AMDGPU_GFX_TCX_CFIFO6, "TCX_CFIFO6"},
	{AMDGPU_GFX_TCX_CFIFO7, "TCX_CFIFO7"},
	{AMDGPU_GFX_TCX_FIFO_ACKB0, "TCX_FIFO_ACKB0"},
	{AMDGPU_GFX_TCX_FIFO_ACKB1, "TCX_FIFO_ACKB1"},
	{AMDGPU_GFX_TCX_FIFO_ACKB2, "TCX_FIFO_ACKB2"},
	{AMDGPU_GFX_TCX_FIFO_ACKB3, "TCX_FIFO_ACKB3"},
	{AMDGPU_GFX_TCX_FIFO_ACKB4, "TCX_FIFO_ACKB4"},
	{AMDGPU_GFX_TCX_FIFO_ACKB5, "TCX_FIFO_ACKB5"},
	{AMDGPU_GFX_TCX_FIFO_ACKB6, "TCX_FIFO_ACKB6"},
	{AMDGPU_GFX_TCX_FIFO_ACKB7, "TCX_FIFO_ACKB7"},
	{AMDGPU_GFX_TCX_FIFO_ACKD0, "TCX_FIFO_ACKD0"},
	{AMDGPU_GFX_TCX_FIFO_ACKD1, "TCX_FIFO_ACKD1"},
	{AMDGPU_GFX_TCX_FIFO_ACKD2, "TCX_FIFO_ACKD2"},
	{AMDGPU_GFX_TCX_FIFO_ACKD3, "TCX_FIFO_ACKD3"},
	{AMDGPU_GFX_TCX_FIFO_ACKD4, "TCX_FIFO_ACKD4"},
	{AMDGPU_GFX_TCX_FIFO_ACKD5, "TCX_FIFO_ACKD5"},
	{AMDGPU_GFX_TCX_FIFO_ACKD6, "TCX_FIFO_ACKD6"},
	{AMDGPU_GFX_TCX_FIFO_ACKD7, "TCX_FIFO_ACKD7"},
	{AMDGPU_GFX_TCX_DST_FIFOA0, "TCX_DST_FIFOA0"},
	{AMDGPU_GFX_TCX_DST_FIFOA1, "TCX_DST_FIFOA1"},
	{AMDGPU_GFX_TCX_DST_FIFOA2, "TCX_DST_FIFOA2"},
	{AMDGPU_GFX_TCX_DST_FIFOA3, "TCX_DST_FIFOA3"},
	{AMDGPU_GFX_TCX_DST_FIFOA4, "TCX_DST_FIFOA4"},
	{AMDGPU_GFX_TCX_DST_FIFOA5, "TCX_DST_FIFOA5"},
	{AMDGPU_GFX_TCX_DST_FIFOA6, "TCX_DST_FIFOA6"},
	{AMDGPU_GFX_TCX_DST_FIFOA7, "TCX_DST_FIFOA7"},
	{AMDGPU_GFX_TCX_DST_FIFOB0, "TCX_DST_FIFOB0"},
	{AMDGPU_GFX_TCX_DST_FIFOB1, "TCX_DST_FIFOB1"},
	{AMDGPU_GFX_TCX_DST_FIFOB2, "TCX_DST_FIFOB2"},
	{AMDGPU_GFX_TCX_DST_FIFOB3, "TCX_DST_FIFOB3"},
	{AMDGPU_GFX_TCX_DST_FIFOB4, "TCX_DST_FIFOB4"},
	{AMDGPU_GFX_TCX_DST_FIFOB5, "TCX_DST_FIFOB5"},
	{AMDGPU_GFX_TCX_DST_FIFOB6, "TCX_DST_FIFOB6"},
	{AMDGPU_GFX_TCX_DST_FIFOB7, "TCX_DST_FIFOB7"},
	{AMDGPU_GFX_TCX_DST_FIFOD0, "TCX_DST_FIFOD0"},
	{AMDGPU_GFX_TCX_DST_FIFOD1, "TCX_DST_FIFOD1"},
	{AMDGPU_GFX_TCX_DST_FIFOD2, "TCX_DST_FIFOD2"},
	{AMDGPU_GFX_TCX_DST_FIFOD3, "TCX_DST_FIFOD3"},
	{AMDGPU_GFX_TCX_DST_FIFOD4, "TCX_DST_FIFOD4"},
	{AMDGPU_GFX_TCX_DST_FIFOD5, "TCX_DST_FIFOD5"},
	{AMDGPU_GFX_TCX_DST_FIFOD6, "TCX_DST_FIFOD6"},
	{AMDGPU_GFX_TCX_DST_FIFOD7, "TCX_DST_FIFOD7"},
	{AMDGPU_GFX_TCX_DST_FIFO_ACKB0, "TCX_DST_FIFO_ACKB0"},
	{AMDGPU_GFX_TCX_DST_FIFO_ACKB1, "TCX_DST_FIFO_ACKB1"},
	{AMDGPU_GFX_TCX_DST_FIFO_ACKB2, "TCX_DST_FIFO_ACKB2"},
	{AMDGPU_GFX_TCX_DST_FIFO_ACKB3, "TCX_DST_FIFO_ACKB3"},
	{AMDGPU_GFX_TCX_DST_FIFO_ACKB4, "TCX_DST_FIFO_ACKB4"},
	{AMDGPU_GFX_TCX_DST_FIFO_ACKB5, "TCX_DST_FIFO_ACKB5"},
	{AMDGPU_GFX_TCX_DST_FIFO_ACKB6, "TCX_DST_FIFO_ACKB6"},
	{AMDGPU_GFX_TCX_DST_FIFO_ACKB7, "TCX_DST_FIFO_ACKB7"},
	{AMDGPU_GFX_TCX_DST_FIFO_ACKD0, "TCX_DST_FIFO_ACKD0"},
	{AMDGPU_GFX_TCX_DST_FIFO_ACKD1, "TCX_DST_FIFO_ACKD1"},
	{AMDGPU_GFX_TCX_DST_FIFO_ACKD2, "TCX_DST_FIFO_ACKD2"},
	{AMDGPU_GFX_TCX_DST_FIFO_ACKD3, "TCX_DST_FIFO_ACKD3"},
	{AMDGPU_GFX_TCX_DST_FIFO_ACKD4, "TCX_DST_FIFO_ACKD4"},
	{AMDGPU_GFX_TCX_DST_FIFO_ACKD5, "TCX_DST_FIFO_ACKD5"},
	{AMDGPU_GFX_TCX_DST_FIFO_ACKD6, "TCX_DST_FIFO_ACKD6"},
	{AMDGPU_GFX_TCX_DST_FIFO_ACKD7, "TCX_DST_FIFO_ACKD7"},
};

static const struct amdgpu_ras_memory_id_entry gfx_v9_4_3_ras_atc_l2_mem_list[] = {
	{AMDGPU_GFX_ATC_L2_MEM, "ATC_L2_MEM"},
};

static const struct amdgpu_ras_memory_id_entry gfx_v9_4_3_ras_utcl2_mem_list[] = {
	{AMDGPU_GFX_UTCL2_MEM, "UTCL2_MEM"},
};

static const struct amdgpu_ras_memory_id_entry gfx_v9_4_3_ras_vml2_mem_list[] = {
	{AMDGPU_GFX_VML2_MEM, "VML2_MEM"},
};

static const struct amdgpu_ras_memory_id_entry gfx_v9_4_3_ras_vml2_walker_mem_list[] = {
	{AMDGPU_GFX_VML2_WALKER_MEM, "VML2_WALKER_MEM"},
};

static const struct amdgpu_gfx_ras_mem_id_entry gfx_v9_4_3_ras_mem_list_array[AMDGPU_GFX_MEM_TYPE_NUM] = {
	AMDGPU_GFX_MEMID_ENT(gfx_v9_4_3_ras_cp_mem_list)
	AMDGPU_GFX_MEMID_ENT(gfx_v9_4_3_ras_gcea_mem_list)
	AMDGPU_GFX_MEMID_ENT(gfx_v9_4_3_ras_gc_cane_mem_list)
	AMDGPU_GFX_MEMID_ENT(gfx_v9_4_3_ras_gcutcl2_mem_list)
	AMDGPU_GFX_MEMID_ENT(gfx_v9_4_3_ras_gds_mem_list)
	AMDGPU_GFX_MEMID_ENT(gfx_v9_4_3_ras_lds_mem_list)
	AMDGPU_GFX_MEMID_ENT(gfx_v9_4_3_ras_rlc_mem_list)
	AMDGPU_GFX_MEMID_ENT(gfx_v9_4_3_ras_sp_mem_list)
	AMDGPU_GFX_MEMID_ENT(gfx_v9_4_3_ras_spi_mem_list)
	AMDGPU_GFX_MEMID_ENT(gfx_v9_4_3_ras_sqc_mem_list)
	AMDGPU_GFX_MEMID_ENT(gfx_v9_4_3_ras_sq_mem_list)
	AMDGPU_GFX_MEMID_ENT(gfx_v9_4_3_ras_ta_mem_list)
	AMDGPU_GFX_MEMID_ENT(gfx_v9_4_3_ras_tcc_mem_list)
	AMDGPU_GFX_MEMID_ENT(gfx_v9_4_3_ras_tca_mem_list)
	AMDGPU_GFX_MEMID_ENT(gfx_v9_4_3_ras_tci_mem_list)
	AMDGPU_GFX_MEMID_ENT(gfx_v9_4_3_ras_tcp_mem_list)
	AMDGPU_GFX_MEMID_ENT(gfx_v9_4_3_ras_td_mem_list)
	AMDGPU_GFX_MEMID_ENT(gfx_v9_4_3_ras_tcx_mem_list)
	AMDGPU_GFX_MEMID_ENT(gfx_v9_4_3_ras_atc_l2_mem_list)
	AMDGPU_GFX_MEMID_ENT(gfx_v9_4_3_ras_utcl2_mem_list)
	AMDGPU_GFX_MEMID_ENT(gfx_v9_4_3_ras_vml2_mem_list)
	AMDGPU_GFX_MEMID_ENT(gfx_v9_4_3_ras_vml2_walker_mem_list)
};

static const struct amdgpu_gfx_ras_reg_entry gfx_v9_4_3_ce_reg_list[] = {
	{{AMDGPU_RAS_REG_ENTRY(GC, 0, regRLC_CE_ERR_STATUS_LOW, regRLC_CE_ERR_STATUS_HIGH),
	    1, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "RLC"},
	    AMDGPU_GFX_RLC_MEM, 1},
	{{AMDGPU_RAS_REG_ENTRY(GC, 0, regCPC_CE_ERR_STATUS_LO, regCPC_CE_ERR_STATUS_HI),
	    1, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "CPC"},
	    AMDGPU_GFX_CP_MEM, 1},
	{{AMDGPU_RAS_REG_ENTRY(GC, 0, regCPF_CE_ERR_STATUS_LO, regCPF_CE_ERR_STATUS_HI),
	    1, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "CPF"},
	    AMDGPU_GFX_CP_MEM, 1},
	{{AMDGPU_RAS_REG_ENTRY(GC, 0, regCPG_CE_ERR_STATUS_LO, regCPG_CE_ERR_STATUS_HI),
	    1, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "CPG"},
	    AMDGPU_GFX_CP_MEM, 1},
	{{AMDGPU_RAS_REG_ENTRY(GC, 0, regGDS_CE_ERR_STATUS_LO, regGDS_CE_ERR_STATUS_HI),
	    1, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "GDS"},
	    AMDGPU_GFX_GDS_MEM, 1},
	{{AMDGPU_RAS_REG_ENTRY(GC, 0, regGC_CANE_CE_ERR_STATUS_LO, regGC_CANE_CE_ERR_STATUS_HI),
	    1, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "CANE"},
	    AMDGPU_GFX_GC_CANE_MEM, 1},
	{{AMDGPU_RAS_REG_ENTRY(GC, 0, regSPI_CE_ERR_STATUS_LO, regSPI_CE_ERR_STATUS_HI),
	    1, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "SPI"},
	    AMDGPU_GFX_SPI_MEM, 1},
	{{AMDGPU_RAS_REG_ENTRY(GC, 0, regSP0_CE_ERR_STATUS_LO, regSP0_CE_ERR_STATUS_HI),
	    10, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "SP0"},
	    AMDGPU_GFX_SP_MEM, 4},
	{{AMDGPU_RAS_REG_ENTRY(GC, 0, regSP1_CE_ERR_STATUS_LO, regSP1_CE_ERR_STATUS_HI),
	    10, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "SP1"},
	    AMDGPU_GFX_SP_MEM, 4},
	{{AMDGPU_RAS_REG_ENTRY(GC, 0, regSQ_CE_ERR_STATUS_LO, regSQ_CE_ERR_STATUS_HI),
	    10, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "SQ"},
	    AMDGPU_GFX_SQ_MEM, 4},
	{{AMDGPU_RAS_REG_ENTRY(GC, 0, regSQC_CE_EDC_LO, regSQC_CE_EDC_HI),
	    5, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "SQC"},
	    AMDGPU_GFX_SQC_MEM, 4},
	{{AMDGPU_RAS_REG_ENTRY(GC, 0, regTCX_CE_ERR_STATUS_LO, regTCX_CE_ERR_STATUS_HI),
	    2, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "TCX"},
	    AMDGPU_GFX_TCX_MEM, 1},
	{{AMDGPU_RAS_REG_ENTRY(GC, 0, regTCC_CE_ERR_STATUS_LO, regTCC_CE_ERR_STATUS_HI),
	    16, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "TCC"},
	    AMDGPU_GFX_TCC_MEM, 1},
	{{AMDGPU_RAS_REG_ENTRY(GC, 0, regTA_CE_EDC_LO, regTA_CE_EDC_HI),
	    10, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "TA"},
	    AMDGPU_GFX_TA_MEM, 4},
	{{AMDGPU_RAS_REG_ENTRY(GC, 0, regTCI_CE_EDC_LO_REG, regTCI_CE_EDC_HI_REG),
	    27, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "TCI"},
	    AMDGPU_GFX_TCI_MEM, 1},
	{{AMDGPU_RAS_REG_ENTRY(GC, 0, regTCP_CE_EDC_LO_REG, regTCP_CE_EDC_HI_REG),
	    10, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "TCP"},
	    AMDGPU_GFX_TCP_MEM, 4},
	{{AMDGPU_RAS_REG_ENTRY(GC, 0, regTD_CE_EDC_LO, regTD_CE_EDC_HI),
	    10, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "TD"},
	    AMDGPU_GFX_TD_MEM, 4},
	{{AMDGPU_RAS_REG_ENTRY(GC, 0, regGCEA_CE_ERR_STATUS_LO, regGCEA_CE_ERR_STATUS_HI),
	    16, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "GCEA"},
	    AMDGPU_GFX_GCEA_MEM, 1},
	{{AMDGPU_RAS_REG_ENTRY(GC, 0, regLDS_CE_ERR_STATUS_LO, regLDS_CE_ERR_STATUS_HI),
	    10, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "LDS"},
	    AMDGPU_GFX_LDS_MEM, 4},
};

static const struct amdgpu_gfx_ras_reg_entry gfx_v9_4_3_ue_reg_list[] = {
	{{AMDGPU_RAS_REG_ENTRY(GC, 0, regRLC_UE_ERR_STATUS_LOW, regRLC_UE_ERR_STATUS_HIGH),
	    1, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "RLC"},
	    AMDGPU_GFX_RLC_MEM, 1},
	{{AMDGPU_RAS_REG_ENTRY(GC, 0, regCPC_UE_ERR_STATUS_LO, regCPC_UE_ERR_STATUS_HI),
	    1, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "CPC"},
	    AMDGPU_GFX_CP_MEM, 1},
	{{AMDGPU_RAS_REG_ENTRY(GC, 0, regCPF_UE_ERR_STATUS_LO, regCPF_UE_ERR_STATUS_HI),
	    1, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "CPF"},
	    AMDGPU_GFX_CP_MEM, 1},
	{{AMDGPU_RAS_REG_ENTRY(GC, 0, regCPG_UE_ERR_STATUS_LO, regCPG_UE_ERR_STATUS_HI),
	    1, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "CPG"},
	    AMDGPU_GFX_CP_MEM, 1},
	{{AMDGPU_RAS_REG_ENTRY(GC, 0, regGDS_UE_ERR_STATUS_LO, regGDS_UE_ERR_STATUS_HI),
	    1, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "GDS"},
	    AMDGPU_GFX_GDS_MEM, 1},
	{{AMDGPU_RAS_REG_ENTRY(GC, 0, regGC_CANE_UE_ERR_STATUS_LO, regGC_CANE_UE_ERR_STATUS_HI),
	    1, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "CANE"},
	    AMDGPU_GFX_GC_CANE_MEM, 1},
	{{AMDGPU_RAS_REG_ENTRY(GC, 0, regSPI_UE_ERR_STATUS_LO, regSPI_UE_ERR_STATUS_HI),
	    1, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "SPI"},
	    AMDGPU_GFX_SPI_MEM, 1},
	{{AMDGPU_RAS_REG_ENTRY(GC, 0, regSP0_UE_ERR_STATUS_LO, regSP0_UE_ERR_STATUS_HI),
	    10, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "SP0"},
	    AMDGPU_GFX_SP_MEM, 4},
	{{AMDGPU_RAS_REG_ENTRY(GC, 0, regSP1_UE_ERR_STATUS_LO, regSP1_UE_ERR_STATUS_HI),
	    10, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "SP1"},
	    AMDGPU_GFX_SP_MEM, 4},
	{{AMDGPU_RAS_REG_ENTRY(GC, 0, regSQ_UE_ERR_STATUS_LO, regSQ_UE_ERR_STATUS_HI),
	    10, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "SQ"},
	    AMDGPU_GFX_SQ_MEM, 4},
	{{AMDGPU_RAS_REG_ENTRY(GC, 0, regSQC_UE_EDC_LO, regSQC_UE_EDC_HI),
	    5, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "SQC"},
	    AMDGPU_GFX_SQC_MEM, 4},
	{{AMDGPU_RAS_REG_ENTRY(GC, 0, regTCX_UE_ERR_STATUS_LO, regTCX_UE_ERR_STATUS_HI),
	    2, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "TCX"},
	    AMDGPU_GFX_TCX_MEM, 1},
	{{AMDGPU_RAS_REG_ENTRY(GC, 0, regTCC_UE_ERR_STATUS_LO, regTCC_UE_ERR_STATUS_HI),
	    16, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "TCC"},
	    AMDGPU_GFX_TCC_MEM, 1},
	{{AMDGPU_RAS_REG_ENTRY(GC, 0, regTA_UE_EDC_LO, regTA_UE_EDC_HI),
	    10, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "TA"},
	    AMDGPU_GFX_TA_MEM, 4},
	{{AMDGPU_RAS_REG_ENTRY(GC, 0, regTCI_UE_EDC_LO_REG, regTCI_UE_EDC_HI_REG),
	    27, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "TCI"},
	    AMDGPU_GFX_TCI_MEM, 1},
	{{AMDGPU_RAS_REG_ENTRY(GC, 0, regTCP_UE_EDC_LO_REG, regTCP_UE_EDC_HI_REG),
	    10, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "TCP"},
	    AMDGPU_GFX_TCP_MEM, 4},
	{{AMDGPU_RAS_REG_ENTRY(GC, 0, regTD_UE_EDC_LO, regTD_UE_EDC_HI),
	    10, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "TD"},
	    AMDGPU_GFX_TD_MEM, 4},
	{{AMDGPU_RAS_REG_ENTRY(GC, 0, regTCA_UE_ERR_STATUS_LO, regTCA_UE_ERR_STATUS_HI),
	    2, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "TCA"},
	    AMDGPU_GFX_TCA_MEM, 1},
	{{AMDGPU_RAS_REG_ENTRY(GC, 0, regGCEA_UE_ERR_STATUS_LO, regGCEA_UE_ERR_STATUS_HI),
	    16, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "GCEA"},
	    AMDGPU_GFX_GCEA_MEM, 1},
	{{AMDGPU_RAS_REG_ENTRY(GC, 0, regLDS_UE_ERR_STATUS_LO, regLDS_UE_ERR_STATUS_HI),
	    10, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "LDS"},
	    AMDGPU_GFX_LDS_MEM, 4},
};

static void gfx_v9_4_3_inst_query_ras_err_count(struct amdgpu_device *adev,
					void *ras_error_status, int xcc_id)
{
	struct ras_err_data *err_data = (struct ras_err_data *)ras_error_status;
	unsigned long ce_count = 0, ue_count = 0;
	uint32_t i, j, k;

	/* NOTE: convert xcc_id to physical XCD ID (XCD0 or XCD1) */
	struct amdgpu_smuio_mcm_config_info mcm_info = {
		.socket_id = adev->smuio.funcs->get_socket_id(adev),
		.die_id = xcc_id & 0x01 ? 1 : 0,
	};

	mutex_lock(&adev->grbm_idx_mutex);

	for (i = 0; i < ARRAY_SIZE(gfx_v9_4_3_ce_reg_list); i++) {
		for (j = 0; j < gfx_v9_4_3_ce_reg_list[i].se_num; j++) {
			for (k = 0; k < gfx_v9_4_3_ce_reg_list[i].reg_entry.reg_inst; k++) {
				/* no need to select if instance number is 1 */
				if (gfx_v9_4_3_ce_reg_list[i].se_num > 1 ||
				    gfx_v9_4_3_ce_reg_list[i].reg_entry.reg_inst > 1)
					gfx_v9_4_3_xcc_select_se_sh(adev, j, 0, k, xcc_id);

				amdgpu_ras_inst_query_ras_error_count(adev,
					&(gfx_v9_4_3_ce_reg_list[i].reg_entry),
					1,
					gfx_v9_4_3_ras_mem_list_array[gfx_v9_4_3_ce_reg_list[i].mem_id_type].mem_id_ent,
					gfx_v9_4_3_ras_mem_list_array[gfx_v9_4_3_ce_reg_list[i].mem_id_type].size,
					GET_INST(GC, xcc_id),
					AMDGPU_RAS_ERROR__SINGLE_CORRECTABLE,
					&ce_count);

				amdgpu_ras_inst_query_ras_error_count(adev,
					&(gfx_v9_4_3_ue_reg_list[i].reg_entry),
					1,
					gfx_v9_4_3_ras_mem_list_array[gfx_v9_4_3_ue_reg_list[i].mem_id_type].mem_id_ent,
					gfx_v9_4_3_ras_mem_list_array[gfx_v9_4_3_ue_reg_list[i].mem_id_type].size,
					GET_INST(GC, xcc_id),
					AMDGPU_RAS_ERROR__MULTI_UNCORRECTABLE,
					&ue_count);
			}
		}
	}

	/* handle extra register entries of UE */
	for (; i < ARRAY_SIZE(gfx_v9_4_3_ue_reg_list); i++) {
		for (j = 0; j < gfx_v9_4_3_ue_reg_list[i].se_num; j++) {
			for (k = 0; k < gfx_v9_4_3_ue_reg_list[i].reg_entry.reg_inst; k++) {
				/* no need to select if instance number is 1 */
				if (gfx_v9_4_3_ue_reg_list[i].se_num > 1 ||
					gfx_v9_4_3_ue_reg_list[i].reg_entry.reg_inst > 1)
					gfx_v9_4_3_xcc_select_se_sh(adev, j, 0, k, xcc_id);

				amdgpu_ras_inst_query_ras_error_count(adev,
					&(gfx_v9_4_3_ue_reg_list[i].reg_entry),
					1,
					gfx_v9_4_3_ras_mem_list_array[gfx_v9_4_3_ue_reg_list[i].mem_id_type].mem_id_ent,
					gfx_v9_4_3_ras_mem_list_array[gfx_v9_4_3_ue_reg_list[i].mem_id_type].size,
					GET_INST(GC, xcc_id),
					AMDGPU_RAS_ERROR__MULTI_UNCORRECTABLE,
					&ue_count);
			}
		}
	}

	gfx_v9_4_3_xcc_select_se_sh(adev, 0xffffffff, 0xffffffff, 0xffffffff,
			xcc_id);
	mutex_unlock(&adev->grbm_idx_mutex);

	/* the caller should make sure initialize value of
	 * err_data->ue_count and err_data->ce_count
	 */
	amdgpu_ras_error_statistic_ue_count(err_data, &mcm_info, ue_count);
	amdgpu_ras_error_statistic_ce_count(err_data, &mcm_info, ce_count);
}

static void gfx_v9_4_3_inst_reset_ras_err_count(struct amdgpu_device *adev,
					void *ras_error_status, int xcc_id)
{
	uint32_t i, j, k;

	mutex_lock(&adev->grbm_idx_mutex);

	for (i = 0; i < ARRAY_SIZE(gfx_v9_4_3_ce_reg_list); i++) {
		for (j = 0; j < gfx_v9_4_3_ce_reg_list[i].se_num; j++) {
			for (k = 0; k < gfx_v9_4_3_ce_reg_list[i].reg_entry.reg_inst; k++) {
				/* no need to select if instance number is 1 */
				if (gfx_v9_4_3_ce_reg_list[i].se_num > 1 ||
				    gfx_v9_4_3_ce_reg_list[i].reg_entry.reg_inst > 1)
					gfx_v9_4_3_xcc_select_se_sh(adev, j, 0, k, xcc_id);

				amdgpu_ras_inst_reset_ras_error_count(adev,
					&(gfx_v9_4_3_ce_reg_list[i].reg_entry),
					1,
					GET_INST(GC, xcc_id));

				amdgpu_ras_inst_reset_ras_error_count(adev,
					&(gfx_v9_4_3_ue_reg_list[i].reg_entry),
					1,
					GET_INST(GC, xcc_id));
			}
		}
	}

	/* handle extra register entries of UE */
	for (; i < ARRAY_SIZE(gfx_v9_4_3_ue_reg_list); i++) {
		for (j = 0; j < gfx_v9_4_3_ue_reg_list[i].se_num; j++) {
			for (k = 0; k < gfx_v9_4_3_ue_reg_list[i].reg_entry.reg_inst; k++) {
				/* no need to select if instance number is 1 */
				if (gfx_v9_4_3_ue_reg_list[i].se_num > 1 ||
					gfx_v9_4_3_ue_reg_list[i].reg_entry.reg_inst > 1)
					gfx_v9_4_3_xcc_select_se_sh(adev, j, 0, k, xcc_id);

				amdgpu_ras_inst_reset_ras_error_count(adev,
					&(gfx_v9_4_3_ue_reg_list[i].reg_entry),
					1,
					GET_INST(GC, xcc_id));
			}
		}
	}

	gfx_v9_4_3_xcc_select_se_sh(adev, 0xffffffff, 0xffffffff, 0xffffffff,
			xcc_id);
	mutex_unlock(&adev->grbm_idx_mutex);
}

static void gfx_v9_4_3_inst_enable_watchdog_timer(struct amdgpu_device *adev,
					void *ras_error_status, int xcc_id)
{
	uint32_t i;
	uint32_t data;

	if (amdgpu_sriov_vf(adev))
		return;

	data = RREG32_SOC15(GC, GET_INST(GC, 0), regSQ_TIMEOUT_CONFIG);
	data = REG_SET_FIELD(data, SQ_TIMEOUT_CONFIG, TIMEOUT_FATAL_DISABLE,
			     amdgpu_watchdog_timer.timeout_fatal_disable ? 1 : 0);

	if (amdgpu_watchdog_timer.timeout_fatal_disable &&
	    (amdgpu_watchdog_timer.period < 1 ||
	     amdgpu_watchdog_timer.period > 0x23)) {
		dev_warn(adev->dev, "Watchdog period range is 1 to 0x23\n");
		amdgpu_watchdog_timer.period = 0x23;
	}
	data = REG_SET_FIELD(data, SQ_TIMEOUT_CONFIG, PERIOD_SEL,
			     amdgpu_watchdog_timer.period);

	mutex_lock(&adev->grbm_idx_mutex);
	for (i = 0; i < adev->gfx.config.max_shader_engines; i++) {
		gfx_v9_4_3_xcc_select_se_sh(adev, i, 0xffffffff, 0xffffffff, xcc_id);
		WREG32_SOC15(GC, GET_INST(GC, xcc_id), regSQ_TIMEOUT_CONFIG, data);
	}
	gfx_v9_4_3_xcc_select_se_sh(adev, 0xffffffff, 0xffffffff, 0xffffffff,
			xcc_id);
	mutex_unlock(&adev->grbm_idx_mutex);
}

static void gfx_v9_4_3_query_ras_error_count(struct amdgpu_device *adev,
					void *ras_error_status)
{
	amdgpu_gfx_ras_error_func(adev, ras_error_status,
			gfx_v9_4_3_inst_query_ras_err_count);
}

static void gfx_v9_4_3_reset_ras_error_count(struct amdgpu_device *adev)
{
	amdgpu_gfx_ras_error_func(adev, NULL, gfx_v9_4_3_inst_reset_ras_err_count);
}

static void gfx_v9_4_3_enable_watchdog_timer(struct amdgpu_device *adev)
{
	amdgpu_gfx_ras_error_func(adev, NULL, gfx_v9_4_3_inst_enable_watchdog_timer);
}

static void gfx_v9_4_3_ring_insert_nop(struct amdgpu_ring *ring, uint32_t num_nop)
{
	/* Header itself is a NOP packet */
	if (num_nop == 1) {
		amdgpu_ring_write(ring, ring->funcs->nop);
		return;
	}

	/* Max HW optimization till 0x3ffe, followed by remaining one NOP at a time*/
	amdgpu_ring_write(ring, PACKET3(PACKET3_NOP, min(num_nop - 2, 0x3ffe)));

	/* Header is at index 0, followed by num_nops - 1 NOP packet's */
	amdgpu_ring_insert_nop(ring, num_nop - 1);
}

static void gfx_v9_4_3_ip_print(struct amdgpu_ip_block *ip_block, struct drm_printer *p)
{
	struct amdgpu_device *adev = ip_block->adev;
	uint32_t i, j, k;
	uint32_t xcc_id, xcc_offset, inst_offset;
	uint32_t num_xcc, reg, num_inst;
	uint32_t reg_count = ARRAY_SIZE(gc_reg_list_9_4_3);

	if (!adev->gfx.ip_dump_core)
		return;

	num_xcc = NUM_XCC(adev->gfx.xcc_mask);
	drm_printf(p, "Number of Instances:%d\n", num_xcc);
	for (xcc_id = 0; xcc_id < num_xcc; xcc_id++) {
		xcc_offset = xcc_id * reg_count;
		drm_printf(p, "\nInstance id:%d\n", xcc_id);
		for (i = 0; i < reg_count; i++)
			drm_printf(p, "%-50s \t 0x%08x\n",
				   gc_reg_list_9_4_3[i].reg_name,
				   adev->gfx.ip_dump_core[xcc_offset + i]);
	}

	/* print compute queue registers for all instances */
	if (!adev->gfx.ip_dump_compute_queues)
		return;

	num_inst = adev->gfx.mec.num_mec * adev->gfx.mec.num_pipe_per_mec *
		adev->gfx.mec.num_queue_per_pipe;

	reg_count = ARRAY_SIZE(gc_cp_reg_list_9_4_3);
	drm_printf(p, "\nnum_xcc: %d num_mec: %d num_pipe: %d num_queue: %d\n",
		   num_xcc,
		   adev->gfx.mec.num_mec,
		   adev->gfx.mec.num_pipe_per_mec,
		   adev->gfx.mec.num_queue_per_pipe);

	for (xcc_id = 0; xcc_id < num_xcc; xcc_id++) {
		xcc_offset = xcc_id * reg_count * num_inst;
		inst_offset = 0;
		for (i = 0; i < adev->gfx.mec.num_mec; i++) {
			for (j = 0; j < adev->gfx.mec.num_pipe_per_mec; j++) {
				for (k = 0; k < adev->gfx.mec.num_queue_per_pipe; k++) {
					drm_printf(p,
						   "\nxcc:%d mec:%d, pipe:%d, queue:%d\n",
						    xcc_id, i, j, k);
					for (reg = 0; reg < reg_count; reg++) {
						drm_printf(p,
							   "%-50s \t 0x%08x\n",
							   gc_cp_reg_list_9_4_3[reg].reg_name,
							   adev->gfx.ip_dump_compute_queues
								[xcc_offset + inst_offset +
								reg]);
					}
					inst_offset += reg_count;
				}
			}
		}
	}
}

static void gfx_v9_4_3_ip_dump(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	uint32_t i, j, k;
	uint32_t num_xcc, reg, num_inst;
	uint32_t xcc_id, xcc_offset, inst_offset;
	uint32_t reg_count = ARRAY_SIZE(gc_reg_list_9_4_3);

	if (!adev->gfx.ip_dump_core)
		return;

	num_xcc = NUM_XCC(adev->gfx.xcc_mask);

	amdgpu_gfx_off_ctrl(adev, false);
	for (xcc_id = 0; xcc_id < num_xcc; xcc_id++) {
		xcc_offset = xcc_id * reg_count;
		for (i = 0; i < reg_count; i++)
			adev->gfx.ip_dump_core[xcc_offset + i] =
				RREG32(SOC15_REG_ENTRY_OFFSET_INST(gc_reg_list_9_4_3[i],
								   GET_INST(GC, xcc_id)));
	}
	amdgpu_gfx_off_ctrl(adev, true);

	/* dump compute queue registers for all instances */
	if (!adev->gfx.ip_dump_compute_queues)
		return;

	num_inst = adev->gfx.mec.num_mec * adev->gfx.mec.num_pipe_per_mec *
		adev->gfx.mec.num_queue_per_pipe;
	reg_count = ARRAY_SIZE(gc_cp_reg_list_9_4_3);
	amdgpu_gfx_off_ctrl(adev, false);
	mutex_lock(&adev->srbm_mutex);
	for (xcc_id = 0; xcc_id < num_xcc; xcc_id++) {
		xcc_offset = xcc_id * reg_count * num_inst;
		inst_offset = 0;
		for (i = 0; i < adev->gfx.mec.num_mec; i++) {
			for (j = 0; j < adev->gfx.mec.num_pipe_per_mec; j++) {
				for (k = 0; k < adev->gfx.mec.num_queue_per_pipe; k++) {
					/* ME0 is for GFX so start from 1 for CP */
					soc15_grbm_select(adev, 1 + i, j, k, 0,
							  GET_INST(GC, xcc_id));

					for (reg = 0; reg < reg_count; reg++) {
						adev->gfx.ip_dump_compute_queues
							[xcc_offset +
							 inst_offset + reg] =
							RREG32(SOC15_REG_ENTRY_OFFSET_INST(
								gc_cp_reg_list_9_4_3[reg],
								GET_INST(GC, xcc_id)));
					}
					inst_offset += reg_count;
				}
			}
		}
	}
	soc15_grbm_select(adev, 0, 0, 0, 0, 0);
	mutex_unlock(&adev->srbm_mutex);
	amdgpu_gfx_off_ctrl(adev, true);
}

static void gfx_v9_4_3_ring_emit_cleaner_shader(struct amdgpu_ring *ring)
{
	/* Emit the cleaner shader */
	amdgpu_ring_write(ring, PACKET3(PACKET3_RUN_CLEANER_SHADER, 0));
	amdgpu_ring_write(ring, 0);  /* RESERVED field, programmed to zero */
}

static const struct amd_ip_funcs gfx_v9_4_3_ip_funcs = {
	.name = "gfx_v9_4_3",
	.early_init = gfx_v9_4_3_early_init,
	.late_init = gfx_v9_4_3_late_init,
	.sw_init = gfx_v9_4_3_sw_init,
	.sw_fini = gfx_v9_4_3_sw_fini,
	.hw_init = gfx_v9_4_3_hw_init,
	.hw_fini = gfx_v9_4_3_hw_fini,
	.suspend = gfx_v9_4_3_suspend,
	.resume = gfx_v9_4_3_resume,
	.is_idle = gfx_v9_4_3_is_idle,
	.wait_for_idle = gfx_v9_4_3_wait_for_idle,
	.soft_reset = gfx_v9_4_3_soft_reset,
	.set_clockgating_state = gfx_v9_4_3_set_clockgating_state,
	.set_powergating_state = gfx_v9_4_3_set_powergating_state,
	.get_clockgating_state = gfx_v9_4_3_get_clockgating_state,
	.dump_ip_state = gfx_v9_4_3_ip_dump,
	.print_ip_state = gfx_v9_4_3_ip_print,
};

static const struct amdgpu_ring_funcs gfx_v9_4_3_ring_funcs_compute = {
	.type = AMDGPU_RING_TYPE_COMPUTE,
	.align_mask = 0xff,
	.nop = PACKET3(PACKET3_NOP, 0x3FFF),
	.support_64bit_ptrs = true,
	.get_rptr = gfx_v9_4_3_ring_get_rptr_compute,
	.get_wptr = gfx_v9_4_3_ring_get_wptr_compute,
	.set_wptr = gfx_v9_4_3_ring_set_wptr_compute,
	.emit_frame_size =
		20 + /* gfx_v9_4_3_ring_emit_gds_switch */
		7 + /* gfx_v9_4_3_ring_emit_hdp_flush */
		5 + /* hdp invalidate */
		7 + /* gfx_v9_4_3_ring_emit_pipeline_sync */
		SOC15_FLUSH_GPU_TLB_NUM_WREG * 5 +
		SOC15_FLUSH_GPU_TLB_NUM_REG_WAIT * 7 +
		2 + /* gfx_v9_4_3_ring_emit_vm_flush */
		8 + 8 + 8 + /* gfx_v9_4_3_ring_emit_fence x3 for user fence, vm fence */
		7 + /* gfx_v9_4_3_emit_mem_sync */
		5 + /* gfx_v9_4_3_emit_wave_limit for updating regSPI_WCL_PIPE_PERCENT_GFX register */
		15 + /* for updating 3 regSPI_WCL_PIPE_PERCENT_CS registers */
		2, /* gfx_v9_4_3_ring_emit_cleaner_shader */
	.emit_ib_size =	7, /* gfx_v9_4_3_ring_emit_ib_compute */
	.emit_ib = gfx_v9_4_3_ring_emit_ib_compute,
	.emit_fence = gfx_v9_4_3_ring_emit_fence,
	.emit_pipeline_sync = gfx_v9_4_3_ring_emit_pipeline_sync,
	.emit_vm_flush = gfx_v9_4_3_ring_emit_vm_flush,
	.emit_gds_switch = gfx_v9_4_3_ring_emit_gds_switch,
	.emit_hdp_flush = gfx_v9_4_3_ring_emit_hdp_flush,
	.test_ring = gfx_v9_4_3_ring_test_ring,
	.test_ib = gfx_v9_4_3_ring_test_ib,
	.insert_nop = gfx_v9_4_3_ring_insert_nop,
	.pad_ib = amdgpu_ring_generic_pad_ib,
	.emit_wreg = gfx_v9_4_3_ring_emit_wreg,
	.emit_reg_wait = gfx_v9_4_3_ring_emit_reg_wait,
	.emit_reg_write_reg_wait = gfx_v9_4_3_ring_emit_reg_write_reg_wait,
	.soft_recovery = gfx_v9_4_3_ring_soft_recovery,
	.emit_mem_sync = gfx_v9_4_3_emit_mem_sync,
	.emit_wave_limit = gfx_v9_4_3_emit_wave_limit,
	.reset = gfx_v9_4_3_reset_kcq,
	.emit_cleaner_shader = gfx_v9_4_3_ring_emit_cleaner_shader,
	.begin_use = amdgpu_gfx_enforce_isolation_ring_begin_use,
	.end_use = amdgpu_gfx_enforce_isolation_ring_end_use,
};

static const struct amdgpu_ring_funcs gfx_v9_4_3_ring_funcs_kiq = {
	.type = AMDGPU_RING_TYPE_KIQ,
	.align_mask = 0xff,
	.nop = PACKET3(PACKET3_NOP, 0x3FFF),
	.support_64bit_ptrs = true,
	.get_rptr = gfx_v9_4_3_ring_get_rptr_compute,
	.get_wptr = gfx_v9_4_3_ring_get_wptr_compute,
	.set_wptr = gfx_v9_4_3_ring_set_wptr_compute,
	.emit_frame_size =
		20 + /* gfx_v9_4_3_ring_emit_gds_switch */
		7 + /* gfx_v9_4_3_ring_emit_hdp_flush */
		5 + /* hdp invalidate */
		7 + /* gfx_v9_4_3_ring_emit_pipeline_sync */
		SOC15_FLUSH_GPU_TLB_NUM_WREG * 5 +
		SOC15_FLUSH_GPU_TLB_NUM_REG_WAIT * 7 +
		2 + /* gfx_v9_4_3_ring_emit_vm_flush */
		8 + 8 + 8, /* gfx_v9_4_3_ring_emit_fence_kiq x3 for user fence, vm fence */
	.emit_ib_size =	7, /* gfx_v9_4_3_ring_emit_ib_compute */
	.emit_fence = gfx_v9_4_3_ring_emit_fence_kiq,
	.test_ring = gfx_v9_4_3_ring_test_ring,
	.insert_nop = amdgpu_ring_insert_nop,
	.pad_ib = amdgpu_ring_generic_pad_ib,
	.emit_rreg = gfx_v9_4_3_ring_emit_rreg,
	.emit_wreg = gfx_v9_4_3_ring_emit_wreg,
	.emit_reg_wait = gfx_v9_4_3_ring_emit_reg_wait,
	.emit_reg_write_reg_wait = gfx_v9_4_3_ring_emit_reg_write_reg_wait,
};

static void gfx_v9_4_3_set_ring_funcs(struct amdgpu_device *adev)
{
	int i, j, num_xcc;

	num_xcc = NUM_XCC(adev->gfx.xcc_mask);
	for (i = 0; i < num_xcc; i++) {
		adev->gfx.kiq[i].ring.funcs = &gfx_v9_4_3_ring_funcs_kiq;

		for (j = 0; j < adev->gfx.num_compute_rings; j++)
			adev->gfx.compute_ring[j + i * adev->gfx.num_compute_rings].funcs
					= &gfx_v9_4_3_ring_funcs_compute;
	}
}

static const struct amdgpu_irq_src_funcs gfx_v9_4_3_eop_irq_funcs = {
	.set = gfx_v9_4_3_set_eop_interrupt_state,
	.process = gfx_v9_4_3_eop_irq,
};

static const struct amdgpu_irq_src_funcs gfx_v9_4_3_priv_reg_irq_funcs = {
	.set = gfx_v9_4_3_set_priv_reg_fault_state,
	.process = gfx_v9_4_3_priv_reg_irq,
};

static const struct amdgpu_irq_src_funcs gfx_v9_4_3_bad_op_irq_funcs = {
	.set = gfx_v9_4_3_set_bad_op_fault_state,
	.process = gfx_v9_4_3_bad_op_irq,
};

static const struct amdgpu_irq_src_funcs gfx_v9_4_3_priv_inst_irq_funcs = {
	.set = gfx_v9_4_3_set_priv_inst_fault_state,
	.process = gfx_v9_4_3_priv_inst_irq,
};

static void gfx_v9_4_3_set_irq_funcs(struct amdgpu_device *adev)
{
	adev->gfx.eop_irq.num_types = AMDGPU_CP_IRQ_LAST;
	adev->gfx.eop_irq.funcs = &gfx_v9_4_3_eop_irq_funcs;

	adev->gfx.priv_reg_irq.num_types = 1;
	adev->gfx.priv_reg_irq.funcs = &gfx_v9_4_3_priv_reg_irq_funcs;

	adev->gfx.bad_op_irq.num_types = 1;
	adev->gfx.bad_op_irq.funcs = &gfx_v9_4_3_bad_op_irq_funcs;

	adev->gfx.priv_inst_irq.num_types = 1;
	adev->gfx.priv_inst_irq.funcs = &gfx_v9_4_3_priv_inst_irq_funcs;
}

static void gfx_v9_4_3_set_rlc_funcs(struct amdgpu_device *adev)
{
	adev->gfx.rlc.funcs = &gfx_v9_4_3_rlc_funcs;
}


static void gfx_v9_4_3_set_gds_init(struct amdgpu_device *adev)
{
	/* init asci gds info */
	switch (amdgpu_ip_version(adev, GC_HWIP, 0)) {
	case IP_VERSION(9, 4, 3):
	case IP_VERSION(9, 4, 4):
		/* 9.4.3 removed all the GDS internal memory,
		 * only support GWS opcode in kernel, like barrier
		 * semaphore.etc */
		adev->gds.gds_size = 0;
		break;
	default:
		adev->gds.gds_size = 0x10000;
		break;
	}

	switch (amdgpu_ip_version(adev, GC_HWIP, 0)) {
	case IP_VERSION(9, 4, 3):
	case IP_VERSION(9, 4, 4):
		/* deprecated for 9.4.3, no usage at all */
		adev->gds.gds_compute_max_wave_id = 0;
		break;
	default:
		/* this really depends on the chip */
		adev->gds.gds_compute_max_wave_id = 0x7ff;
		break;
	}

	adev->gds.gws_size = 64;
	adev->gds.oa_size = 16;
}

static void gfx_v9_4_3_set_user_cu_inactive_bitmap(struct amdgpu_device *adev,
						 u32 bitmap, int xcc_id)
{
	u32 data;

	if (!bitmap)
		return;

	data = bitmap << GC_USER_SHADER_ARRAY_CONFIG__INACTIVE_CUS__SHIFT;
	data &= GC_USER_SHADER_ARRAY_CONFIG__INACTIVE_CUS_MASK;

	WREG32_SOC15(GC, GET_INST(GC, xcc_id), regGC_USER_SHADER_ARRAY_CONFIG, data);
}

static u32 gfx_v9_4_3_get_cu_active_bitmap(struct amdgpu_device *adev, int xcc_id)
{
	u32 data, mask;

	data = RREG32_SOC15(GC, GET_INST(GC, xcc_id), regCC_GC_SHADER_ARRAY_CONFIG);
	data |= RREG32_SOC15(GC, GET_INST(GC, xcc_id), regGC_USER_SHADER_ARRAY_CONFIG);

	data &= CC_GC_SHADER_ARRAY_CONFIG__INACTIVE_CUS_MASK;
	data >>= CC_GC_SHADER_ARRAY_CONFIG__INACTIVE_CUS__SHIFT;

	mask = amdgpu_gfx_create_bitmask(adev->gfx.config.max_cu_per_sh);

	return (~data) & mask;
}

static int gfx_v9_4_3_get_cu_info(struct amdgpu_device *adev,
				 struct amdgpu_cu_info *cu_info)
{
	int i, j, k, prev_counter, counter, xcc_id, active_cu_number = 0;
	u32 mask, bitmap, ao_bitmap, ao_cu_mask = 0, tmp;
	unsigned disable_masks[4 * 4];
	bool is_symmetric_cus;

	if (!adev || !cu_info)
		return -EINVAL;

	/*
	 * 16 comes from bitmap array size 4*4, and it can cover all gfx9 ASICs
	 */
	if (adev->gfx.config.max_shader_engines *
		adev->gfx.config.max_sh_per_se > 16)
		return -EINVAL;

	amdgpu_gfx_parse_disable_cu(disable_masks,
				    adev->gfx.config.max_shader_engines,
				    adev->gfx.config.max_sh_per_se);

	mutex_lock(&adev->grbm_idx_mutex);
	for (xcc_id = 0; xcc_id < NUM_XCC(adev->gfx.xcc_mask); xcc_id++) {
		is_symmetric_cus = true;
		for (i = 0; i < adev->gfx.config.max_shader_engines; i++) {
			for (j = 0; j < adev->gfx.config.max_sh_per_se; j++) {
				mask = 1;
				ao_bitmap = 0;
				counter = 0;
				gfx_v9_4_3_xcc_select_se_sh(adev, i, j, 0xffffffff, xcc_id);
				gfx_v9_4_3_set_user_cu_inactive_bitmap(
					adev,
					disable_masks[i * adev->gfx.config.max_sh_per_se + j],
					xcc_id);
				bitmap = gfx_v9_4_3_get_cu_active_bitmap(adev, xcc_id);

				cu_info->bitmap[xcc_id][i][j] = bitmap;

				for (k = 0; k < adev->gfx.config.max_cu_per_sh; k++) {
					if (bitmap & mask) {
						if (counter < adev->gfx.config.max_cu_per_sh)
							ao_bitmap |= mask;
						counter++;
					}
					mask <<= 1;
				}
				active_cu_number += counter;
				if (i < 2 && j < 2)
					ao_cu_mask |= (ao_bitmap << (i * 16 + j * 8));
				cu_info->ao_cu_bitmap[i][j] = ao_bitmap;
			}
			if (i && is_symmetric_cus && prev_counter != counter)
				is_symmetric_cus = false;
			prev_counter = counter;
		}
		if (is_symmetric_cus) {
			tmp = RREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_CPC_DEBUG);
			tmp = REG_SET_FIELD(tmp, CP_CPC_DEBUG, CPC_HARVESTING_RELAUNCH_DISABLE, 1);
			tmp = REG_SET_FIELD(tmp, CP_CPC_DEBUG, CPC_HARVESTING_DISPATCH_DISABLE, 1);
			WREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_CPC_DEBUG, tmp);
		}
		gfx_v9_4_3_xcc_select_se_sh(adev, 0xffffffff, 0xffffffff, 0xffffffff,
					    xcc_id);
	}
	mutex_unlock(&adev->grbm_idx_mutex);

	cu_info->number = active_cu_number;
	cu_info->ao_cu_mask = ao_cu_mask;
	cu_info->simd_per_cu = NUM_SIMD_PER_CU;

	return 0;
}

const struct amdgpu_ip_block_version gfx_v9_4_3_ip_block = {
	.type = AMD_IP_BLOCK_TYPE_GFX,
	.major = 9,
	.minor = 4,
	.rev = 3,
	.funcs = &gfx_v9_4_3_ip_funcs,
};

static int gfx_v9_4_3_xcp_resume(void *handle, uint32_t inst_mask)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	uint32_t tmp_mask;
	int i, r;

	/* TODO : Initialize golden regs */
	/* gfx_v9_4_3_init_golden_registers(adev); */

	tmp_mask = inst_mask;
	for_each_inst(i, tmp_mask)
		gfx_v9_4_3_xcc_constants_init(adev, i);

	if (!amdgpu_sriov_vf(adev)) {
		tmp_mask = inst_mask;
		for_each_inst(i, tmp_mask) {
			r = gfx_v9_4_3_xcc_rlc_resume(adev, i);
			if (r)
				return r;
		}
	}

	tmp_mask = inst_mask;
	for_each_inst(i, tmp_mask) {
		r = gfx_v9_4_3_xcc_cp_resume(adev, i);
		if (r)
			return r;
	}

	return 0;
}

static int gfx_v9_4_3_xcp_suspend(void *handle, uint32_t inst_mask)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int i;

	for_each_inst(i, inst_mask)
		gfx_v9_4_3_xcc_fini(adev, i);

	return 0;
}

struct amdgpu_xcp_ip_funcs gfx_v9_4_3_xcp_funcs = {
	.suspend = &gfx_v9_4_3_xcp_suspend,
	.resume = &gfx_v9_4_3_xcp_resume
};

struct amdgpu_ras_block_hw_ops  gfx_v9_4_3_ras_ops = {
	.query_ras_error_count = &gfx_v9_4_3_query_ras_error_count,
	.reset_ras_error_count = &gfx_v9_4_3_reset_ras_error_count,
};

static int gfx_v9_4_3_ras_late_init(struct amdgpu_device *adev, struct ras_common_if *ras_block)
{
	int r;

	r = amdgpu_ras_block_late_init(adev, ras_block);
	if (r)
		return r;

	r = amdgpu_ras_bind_aca(adev, AMDGPU_RAS_BLOCK__GFX,
				&gfx_v9_4_3_aca_info,
				NULL);
	if (r)
		goto late_fini;

	return 0;

late_fini:
	amdgpu_ras_block_late_fini(adev, ras_block);

	return r;
}

struct amdgpu_gfx_ras gfx_v9_4_3_ras = {
	.ras_block = {
		.hw_ops = &gfx_v9_4_3_ras_ops,
		.ras_late_init = &gfx_v9_4_3_ras_late_init,
	},
	.enable_watchdog_timer = &gfx_v9_4_3_enable_watchdog_timer,
};
