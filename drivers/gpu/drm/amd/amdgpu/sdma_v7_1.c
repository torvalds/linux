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

#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "amdgpu.h"
#include "amdgpu_ucode.h"
#include "amdgpu_trace.h"

#include "gc/gc_12_1_0_offset.h"
#include "gc/gc_12_1_0_sh_mask.h"
#include "ivsrcid/gfx/irqsrcs_gfx_12_1_0.h"

#include "soc15_common.h"
#include "soc15.h"
#include "sdma_v7_1_0_pkt_open.h"
#include "nbio_v4_3.h"
#include "sdma_common.h"
#include "sdma_v7_1.h"
#include "v12_structs.h"
#include "mes_userqueue.h"
#include "soc_v1_0.h"

MODULE_FIRMWARE("amdgpu/sdma_7_1_0.bin");

#define SDMA1_REG_OFFSET 0x600
#define SDMA0_SDMA_IDX_0_END 0x450
#define SDMA1_HYP_DEC_REG_OFFSET 0x30

static const struct amdgpu_hwip_reg_entry sdma_reg_list_7_1[] = {
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA0_SDMA_STATUS_REG),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA0_SDMA_STATUS1_REG),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA0_SDMA_STATUS2_REG),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA0_SDMA_STATUS3_REG),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA0_SDMA_STATUS4_REG),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA0_SDMA_STATUS5_REG),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA0_SDMA_STATUS6_REG),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA0_SDMA_UCODE_REV),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA0_SDMA_RB_RPTR_FETCH_HI),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA0_SDMA_RB_RPTR_FETCH),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA0_SDMA_UTCL1_RD_STATUS),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA0_SDMA_UTCL1_WR_STATUS),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA0_SDMA_UTCL1_RD_XNACK0),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA0_SDMA_UTCL1_RD_XNACK1),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA0_SDMA_UTCL1_WR_XNACK0),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA0_SDMA_UTCL1_WR_XNACK1),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA0_SDMA_QUEUE0_RB_CNTL),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA0_SDMA_QUEUE0_RB_RPTR),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA0_SDMA_QUEUE0_RB_RPTR_HI),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA0_SDMA_QUEUE0_RB_WPTR),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA0_SDMA_QUEUE0_RB_WPTR_HI),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA0_SDMA_QUEUE0_IB_OFFSET),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA0_SDMA_QUEUE0_IB_BASE_LO),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA0_SDMA_QUEUE0_IB_BASE_HI),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA0_SDMA_QUEUE0_IB_CNTL),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA0_SDMA_QUEUE0_IB_RPTR),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA0_SDMA_QUEUE0_IB_SUB_REMAIN),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA0_SDMA_QUEUE0_DUMMY_REG),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA0_SDMA_QUEUE_STATUS0),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA0_SDMA_QUEUE1_RB_CNTL),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA0_SDMA_QUEUE1_RB_RPTR),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA0_SDMA_QUEUE1_RB_RPTR_HI),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA0_SDMA_QUEUE1_RB_WPTR),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA0_SDMA_QUEUE1_RB_WPTR_HI),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA0_SDMA_QUEUE1_IB_OFFSET),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA0_SDMA_QUEUE1_IB_BASE_LO),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA0_SDMA_QUEUE1_IB_BASE_HI),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA0_SDMA_QUEUE1_IB_RPTR),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA0_SDMA_QUEUE1_IB_SUB_REMAIN),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA0_SDMA_QUEUE1_DUMMY_REG),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA0_SDMA_QUEUE2_RB_CNTL),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA0_SDMA_QUEUE2_RB_RPTR),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA0_SDMA_QUEUE2_RB_RPTR_HI),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA0_SDMA_QUEUE2_RB_WPTR),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA0_SDMA_QUEUE2_RB_WPTR_HI),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA0_SDMA_QUEUE2_IB_OFFSET),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA0_SDMA_QUEUE2_IB_BASE_LO),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA0_SDMA_QUEUE2_IB_BASE_HI),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA0_SDMA_QUEUE2_IB_RPTR),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA0_SDMA_QUEUE2_IB_SUB_REMAIN),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA0_SDMA_QUEUE2_DUMMY_REG),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA0_SDMA_INT_STATUS),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA0_SDMA_VM_CNTL),
	SOC15_REG_ENTRY_STR(GC, 0, regGRBM_STATUS2),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA0_SDMA_CHICKEN_BITS),
};

static void sdma_v7_1_set_ring_funcs(struct amdgpu_device *adev);
static void sdma_v7_1_set_buffer_funcs(struct amdgpu_device *adev);
static void sdma_v7_1_set_vm_pte_funcs(struct amdgpu_device *adev);
static void sdma_v7_1_set_irq_funcs(struct amdgpu_device *adev);
static int sdma_v7_1_inst_start(struct amdgpu_device *adev,
				uint32_t inst_mask);

static u32 sdma_v7_1_get_reg_offset(struct amdgpu_device *adev, u32 instance, u32 internal_offset)
{
	u32 base;
	u32 dev_inst = GET_INST(SDMA0, instance);
	int xcc_id = adev->sdma.instance[instance].xcc_id;
	int xcc_inst = dev_inst % adev->sdma.num_inst_per_xcc;

	if (internal_offset >= SDMA0_SDMA_IDX_0_END) {
		base = adev->reg_offset[GC_HWIP][xcc_id][1];
		if (xcc_inst != 0)
			internal_offset += SDMA1_HYP_DEC_REG_OFFSET * xcc_inst;
	} else {
		base = adev->reg_offset[GC_HWIP][xcc_id][0];
		if (xcc_inst != 0)
			internal_offset += SDMA1_REG_OFFSET * xcc_inst;
	}

	return base + internal_offset;
}

static unsigned sdma_v7_1_ring_init_cond_exec(struct amdgpu_ring *ring,
					      uint64_t addr)
{
	unsigned ret;

	amdgpu_ring_write(ring, SDMA_PKT_COPY_LINEAR_HEADER_OP(SDMA_OP_COND_EXE));
	amdgpu_ring_write(ring, lower_32_bits(addr));
	amdgpu_ring_write(ring, upper_32_bits(addr));
	amdgpu_ring_write(ring, 1);
	/* this is the offset we need patch later */
	ret = ring->wptr & ring->buf_mask;
	/* insert dummy here and patch it later */
	amdgpu_ring_write(ring, 0);

	return ret;
}

/**
 * sdma_v7_1_ring_get_rptr - get the current read pointer
 *
 * @ring: amdgpu ring pointer
 *
 * Get the current rptr from the hardware.
 */
static uint64_t sdma_v7_1_ring_get_rptr(struct amdgpu_ring *ring)
{
	u64 *rptr;

	/* XXX check if swapping is necessary on BE */
	rptr = (u64 *)ring->rptr_cpu_addr;

	DRM_DEBUG("rptr before shift == 0x%016llx\n", *rptr);
	return ((*rptr) >> 2);
}

/**
 * sdma_v7_1_ring_get_wptr - get the current write pointer
 *
 * @ring: amdgpu ring pointer
 *
 * Get the current wptr from the hardware.
 */
static uint64_t sdma_v7_1_ring_get_wptr(struct amdgpu_ring *ring)
{
	u64 wptr = 0;

	if (ring->use_doorbell) {
		/* XXX check if swapping is necessary on BE */
		wptr = READ_ONCE(*((u64 *)ring->wptr_cpu_addr));
		DRM_DEBUG("wptr/doorbell before shift == 0x%016llx\n", wptr);
	}

	return wptr >> 2;
}

/**
 * sdma_v7_1_ring_set_wptr - commit the write pointer
 *
 * @ring: amdgpu ring pointer
 *
 * Write the wptr back to the hardware.
 */
static void sdma_v7_1_ring_set_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	DRM_DEBUG("Setting write pointer\n");

	if (ring->use_doorbell) {
		DRM_DEBUG("Using doorbell -- "
			  "wptr_offs == 0x%08x "
			  "lower_32_bits(ring->wptr) << 2 == 0x%08x "
			  "upper_32_bits(ring->wptr) << 2 == 0x%08x\n",
			  ring->wptr_offs,
			  lower_32_bits(ring->wptr << 2),
			  upper_32_bits(ring->wptr << 2));
		/* XXX check if swapping is necessary on BE */
		atomic64_set((atomic64_t *)ring->wptr_cpu_addr,
			     ring->wptr << 2);
		DRM_DEBUG("calling WDOORBELL64(0x%08x, 0x%016llx)\n",
			  ring->doorbell_index, ring->wptr << 2);
		WDOORBELL64(ring->doorbell_index, ring->wptr << 2);
	} else {
		DRM_DEBUG("Not using doorbell -- "
			  "regSDMA%i_GFX_RB_WPTR == 0x%08x "
			  "regSDMA%i_GFX_RB_WPTR_HI == 0x%08x\n",
			  ring->me,
			  lower_32_bits(ring->wptr << 2),
			  ring->me,
			  upper_32_bits(ring->wptr << 2));
		WREG32_SOC15_IP(GC, sdma_v7_1_get_reg_offset(adev,
							     ring->me,
							     regSDMA0_SDMA_QUEUE0_RB_WPTR),
				lower_32_bits(ring->wptr << 2));
		WREG32_SOC15_IP(GC, sdma_v7_1_get_reg_offset(adev,
							     ring->me,
							     regSDMA0_SDMA_QUEUE0_RB_WPTR_HI),
				upper_32_bits(ring->wptr << 2));
	}
}

static void sdma_v7_1_ring_insert_nop(struct amdgpu_ring *ring, uint32_t count)
{
	struct amdgpu_sdma_instance *sdma = amdgpu_sdma_get_instance_from_ring(ring);
	int i;

	for (i = 0; i < count; i++)
		if (sdma && sdma->burst_nop && (i == 0))
			amdgpu_ring_write(ring, ring->funcs->nop |
				SDMA_PKT_NOP_HEADER_COUNT(count - 1));
		else
			amdgpu_ring_write(ring, ring->funcs->nop);
}

/**
 * sdma_v7_1_ring_emit_ib - Schedule an IB on the DMA engine
 *
 * @ring: amdgpu ring pointer
 * @job: job to retrieve vmid from
 * @ib: IB object to schedule
 * @flags: unused
 *
 * Schedule an IB in the DMA ring.
 */
static void sdma_v7_1_ring_emit_ib(struct amdgpu_ring *ring,
				   struct amdgpu_job *job,
				   struct amdgpu_ib *ib,
				   uint32_t flags)
{
	unsigned vmid = AMDGPU_JOB_GET_VMID(job);
	uint64_t csa_mc_addr = amdgpu_sdma_get_csa_mc_addr(ring, vmid);

	/* An IB packet must end on a 8 DW boundary--the next dword
	 * must be on a 8-dword boundary. Our IB packet below is 6
	 * dwords long, thus add x number of NOPs, such that, in
	 * modular arithmetic,
	 * wptr + 6 + x = 8k, k >= 0, which in C is,
	 * (wptr + 6 + x) % 8 = 0.
	 * The expression below, is a solution of x.
	 */
	sdma_v7_1_ring_insert_nop(ring, (2 - lower_32_bits(ring->wptr)) & 7);

	amdgpu_ring_write(ring, SDMA_PKT_COPY_LINEAR_HEADER_OP(SDMA_OP_INDIRECT) |
			  SDMA_PKT_INDIRECT_HEADER_VMID(vmid & 0xf));
	/* base must be 32 byte aligned */
	amdgpu_ring_write(ring, lower_32_bits(ib->gpu_addr) & 0xffffffe0);
	amdgpu_ring_write(ring, upper_32_bits(ib->gpu_addr));
	amdgpu_ring_write(ring, ib->length_dw);
	amdgpu_ring_write(ring, lower_32_bits(csa_mc_addr));
	amdgpu_ring_write(ring, upper_32_bits(csa_mc_addr));
}

/**
 * sdma_v7_1_ring_emit_mem_sync - flush the IB by graphics cache rinse
 *
 * @ring: amdgpu ring pointer
 *
 * flush the IB by graphics cache rinse.
 */
static void sdma_v7_1_ring_emit_mem_sync(struct amdgpu_ring *ring)
{
	uint32_t gcr_cntl = SDMA_GCR_GL2_INV | SDMA_GCR_GL2_WB | SDMA_GCR_GLM_INV |
		SDMA_GCR_GL1_INV | SDMA_GCR_GLV_INV | SDMA_GCR_GLK_INV |
		SDMA_GCR_GLI_INV(1);

	/* flush entire cache L0/L1/L2, this can be optimized by performance requirement */
	amdgpu_ring_write(ring, SDMA_PKT_COPY_LINEAR_HEADER_OP(SDMA_OP_GCR_REQ));
	amdgpu_ring_write(ring, SDMA_PKT_GCR_REQ_PAYLOAD1_BASE_VA_31_7(0));
	amdgpu_ring_write(ring, SDMA_PKT_GCR_REQ_PAYLOAD2_BASE_VA_56_32(0));
	amdgpu_ring_write(ring, SDMA_PKT_GCR_REQ_PAYLOAD3_GCR_CONTROL_18_0(gcr_cntl) |
			  SDMA_PKT_GCR_REQ_PAYLOAD3_LIMIT_VA_15_7(0));
	amdgpu_ring_write(ring, SDMA_PKT_GCR_REQ_PAYLOAD4_LIMIT_VA_47_16(0));
	amdgpu_ring_write(ring, SDMA_PKT_GCR_REQ_PAYLOAD5_LIMIT_VA_56_48(0) |
			  SDMA_PKT_GCR_REQ_PAYLOAD5_VMID(0));
}


/**
 * sdma_v7_1_ring_emit_fence - emit a fence on the DMA ring
 *
 * @ring: amdgpu ring pointer
 * @addr: address
 * @seq: fence seq number
 * @flags: fence flags
 *
 * Add a DMA fence packet to the ring to write
 * the fence seq number and DMA trap packet to generate
 * an interrupt if needed.
 */
static void sdma_v7_1_ring_emit_fence(struct amdgpu_ring *ring, u64 addr, u64 seq,
				      unsigned flags)
{
	bool write64bit = flags & AMDGPU_FENCE_FLAG_64BIT;
	/* write the fence */
	amdgpu_ring_write(ring, SDMA_PKT_COPY_LINEAR_HEADER_OP(SDMA_OP_FENCE) |
			  SDMA_PKT_FENCE_HEADER_MTYPE(0x3)); /* Ucached(UC) */
	/* zero in first two bits */
	BUG_ON(addr & 0x3);
	amdgpu_ring_write(ring, lower_32_bits(addr));
	amdgpu_ring_write(ring, upper_32_bits(addr));
	amdgpu_ring_write(ring, lower_32_bits(seq));

	/* optionally write high bits as well */
	if (write64bit) {
		addr += 4;
		amdgpu_ring_write(ring, SDMA_PKT_COPY_LINEAR_HEADER_OP(SDMA_OP_FENCE) |
				  SDMA_PKT_FENCE_HEADER_MTYPE(0x3));
		/* zero in first two bits */
		BUG_ON(addr & 0x3);
		amdgpu_ring_write(ring, lower_32_bits(addr));
		amdgpu_ring_write(ring, upper_32_bits(addr));
		amdgpu_ring_write(ring, upper_32_bits(seq));
	}

	if (flags & AMDGPU_FENCE_FLAG_INT) {
		/* generate an interrupt */
		amdgpu_ring_write(ring, SDMA_PKT_COPY_LINEAR_HEADER_OP(SDMA_OP_TRAP));
		amdgpu_ring_write(ring, SDMA_PKT_TRAP_INT_CONTEXT_INT_CONTEXT(0));
	}
}

/**
 * sdma_v7_1_inst_gfx_stop - stop the gfx async dma engines
 *
 * @adev: amdgpu_device pointer
 * @inst_mask: mask of dma engine instances to be disabled
 *
 * Stop the gfx async dma ring buffers.
 */
static void sdma_v7_1_inst_gfx_stop(struct amdgpu_device *adev,
				    uint32_t inst_mask)
{
	u32 rb_cntl, ib_cntl;
	int i;

	for_each_inst(i, inst_mask) {
		rb_cntl = RREG32_SOC15_IP(GC, sdma_v7_1_get_reg_offset(adev, i, regSDMA0_SDMA_QUEUE0_RB_CNTL));
		rb_cntl = REG_SET_FIELD(rb_cntl, SDMA0_SDMA_QUEUE0_RB_CNTL, RB_ENABLE, 0);
		WREG32_SOC15_IP(GC, sdma_v7_1_get_reg_offset(adev, i, regSDMA0_SDMA_QUEUE0_RB_CNTL), rb_cntl);
		ib_cntl = RREG32_SOC15_IP(GC, sdma_v7_1_get_reg_offset(adev, i, regSDMA0_SDMA_QUEUE0_IB_CNTL));
		ib_cntl = REG_SET_FIELD(ib_cntl, SDMA0_SDMA_QUEUE0_IB_CNTL, IB_ENABLE, 0);
		WREG32_SOC15_IP(GC, sdma_v7_1_get_reg_offset(adev, i, regSDMA0_SDMA_QUEUE0_IB_CNTL), ib_cntl);
	}
}

/**
 * sdma_v7_1_inst_rlc_stop - stop the compute async dma engines
 *
 * @adev: amdgpu_device pointer
 * @inst_mask: mask of dma engine instances to be disabled
 *
 * Stop the compute async dma queues.
 */
static void sdma_v7_1_inst_rlc_stop(struct amdgpu_device *adev,
				    uint32_t inst_mask)
{
	/* XXX todo */
}

/**
 * sdma_v7_1_inst_ctx_switch_enable - stop the async dma engines context switch
 *
 * @adev: amdgpu_device pointer
 * @enable: enable/disable the DMA MEs context switch.
 * @inst_mask: mask of dma engine instances to be enabled
 *
 * Halt or unhalt the async dma engines context switch.
 */
static void sdma_v7_1_inst_ctx_switch_enable(struct amdgpu_device *adev,
					     bool enable, uint32_t inst_mask)
{
	int i;

	for_each_inst(i, inst_mask) {
		WREG32_SOC15_IP(GC,
			sdma_v7_1_get_reg_offset(adev, i, regSDMA0_SDMA_UTCL1_TIMEOUT), 0x80);
	}
}

/**
 * sdma_v7_1_inst_enable - stop the async dma engines
 *
 * @adev: amdgpu_device pointer
 * @enable: enable/disable the DMA MEs.
 * @inst_mask: mask of dma engine instances to be enabled
 *
 * Halt or unhalt the async dma engines.
 */
static void sdma_v7_1_inst_enable(struct amdgpu_device *adev,
				  bool enable, uint32_t inst_mask)
{
	u32 mcu_cntl;
	int i;

	if (!enable) {
		sdma_v7_1_inst_gfx_stop(adev, inst_mask);
		sdma_v7_1_inst_rlc_stop(adev, inst_mask);
	}

	if (amdgpu_sriov_vf(adev))
		return;

	for_each_inst(i, inst_mask) {
		mcu_cntl = RREG32_SOC15_IP(GC, sdma_v7_1_get_reg_offset(adev, i, regSDMA0_SDMA_MCU_CNTL));
		mcu_cntl = REG_SET_FIELD(mcu_cntl, SDMA0_SDMA_MCU_CNTL, HALT, enable ? 0 : 1);
		WREG32_SOC15_IP(GC, sdma_v7_1_get_reg_offset(adev, i, regSDMA0_SDMA_MCU_CNTL), mcu_cntl);
	}
}

/**
 * sdma_v7_1_gfx_resume_instance - start/restart a certain sdma engine
 *
 * @adev: amdgpu_device pointer
 * @i: instance
 * @restore: used to restore wptr when restart
 *
 * Set up the gfx DMA ring buffers and enable them. On restart, we will restore wptr and rptr.
 * Return 0 for success.
 */
static int sdma_v7_1_gfx_resume_instance(struct amdgpu_device *adev, int i, bool restore)
{
	struct amdgpu_ring *ring;
	u32 rb_cntl, ib_cntl;
	u32 rb_bufsz;
	u32 doorbell;
	u32 doorbell_offset;
	u32 temp;
	u64 wptr_gpu_addr;
	int r;

	ring = &adev->sdma.instance[i].ring;

	/* Set ring buffer size in dwords */
	rb_bufsz = order_base_2(ring->ring_size / 4);
	rb_cntl = RREG32_SOC15_IP(GC, sdma_v7_1_get_reg_offset(adev, i, regSDMA0_SDMA_QUEUE0_RB_CNTL));
	rb_cntl = REG_SET_FIELD(rb_cntl, SDMA0_SDMA_QUEUE0_RB_CNTL, RB_SIZE, rb_bufsz);
#ifdef __BIG_ENDIAN
	rb_cntl = REG_SET_FIELD(rb_cntl, SDMA0_SDMA_QUEUE0_RB_CNTL, RB_SWAP_ENABLE, 1);
	rb_cntl = REG_SET_FIELD(rb_cntl, SDMA0_SDMA_QUEUE0_RB_CNTL,
				RPTR_WRITEBACK_SWAP_ENABLE, 1);
#endif
	rb_cntl = REG_SET_FIELD(rb_cntl, SDMA0_SDMA_QUEUE0_RB_CNTL, RB_PRIV, 1);
	WREG32_SOC15_IP(GC, sdma_v7_1_get_reg_offset(adev, i, regSDMA0_SDMA_QUEUE0_RB_CNTL), rb_cntl);

	/* Initialize the ring buffer's read and write pointers */
	if (restore) {
		WREG32_SOC15_IP(GC, sdma_v7_1_get_reg_offset(adev, i, regSDMA0_SDMA_QUEUE0_RB_RPTR), lower_32_bits(ring->wptr << 2));
		WREG32_SOC15_IP(GC, sdma_v7_1_get_reg_offset(adev, i, regSDMA0_SDMA_QUEUE0_RB_RPTR_HI), upper_32_bits(ring->wptr << 2));
		WREG32_SOC15_IP(GC, sdma_v7_1_get_reg_offset(adev, i, regSDMA0_SDMA_QUEUE0_RB_WPTR), lower_32_bits(ring->wptr << 2));
		WREG32_SOC15_IP(GC, sdma_v7_1_get_reg_offset(adev, i, regSDMA0_SDMA_QUEUE0_RB_WPTR_HI), upper_32_bits(ring->wptr << 2));
	} else {
		WREG32_SOC15_IP(GC, sdma_v7_1_get_reg_offset(adev, i, regSDMA0_SDMA_QUEUE0_RB_RPTR), 0);
		WREG32_SOC15_IP(GC, sdma_v7_1_get_reg_offset(adev, i, regSDMA0_SDMA_QUEUE0_RB_RPTR_HI), 0);
		WREG32_SOC15_IP(GC, sdma_v7_1_get_reg_offset(adev, i, regSDMA0_SDMA_QUEUE0_RB_WPTR), 0);
		WREG32_SOC15_IP(GC, sdma_v7_1_get_reg_offset(adev, i, regSDMA0_SDMA_QUEUE0_RB_WPTR_HI), 0);
	}
	/* setup the wptr shadow polling */
	wptr_gpu_addr = ring->wptr_gpu_addr;
	WREG32_SOC15_IP(GC, sdma_v7_1_get_reg_offset(adev, i, regSDMA0_SDMA_QUEUE0_RB_WPTR_POLL_ADDR_LO),
	       lower_32_bits(wptr_gpu_addr));
	WREG32_SOC15_IP(GC, sdma_v7_1_get_reg_offset(adev, i, regSDMA0_SDMA_QUEUE0_RB_WPTR_POLL_ADDR_HI),
	       upper_32_bits(wptr_gpu_addr));

	/* set the wb address whether it's enabled or not */
	WREG32_SOC15_IP(GC, sdma_v7_1_get_reg_offset(adev, i, regSDMA0_SDMA_QUEUE0_RB_RPTR_ADDR_HI),
	       upper_32_bits(ring->rptr_gpu_addr) & 0xFFFFFFFF);
	WREG32_SOC15_IP(GC, sdma_v7_1_get_reg_offset(adev, i, regSDMA0_SDMA_QUEUE0_RB_RPTR_ADDR_LO),
	       lower_32_bits(ring->rptr_gpu_addr) & 0xFFFFFFFC);

	rb_cntl = REG_SET_FIELD(rb_cntl, SDMA0_SDMA_QUEUE0_RB_CNTL, RPTR_WRITEBACK_ENABLE, 1);
	if (amdgpu_sriov_vf(adev))
		rb_cntl = REG_SET_FIELD(rb_cntl, SDMA0_SDMA_QUEUE0_RB_CNTL, WPTR_POLL_ENABLE, 1);
	else
		rb_cntl = REG_SET_FIELD(rb_cntl, SDMA0_SDMA_QUEUE0_RB_CNTL, WPTR_POLL_ENABLE, 0);

	rb_cntl = REG_SET_FIELD(rb_cntl, SDMA0_SDMA_QUEUE0_RB_CNTL, MCU_WPTR_POLL_ENABLE, 1);

	WREG32_SOC15_IP(GC, sdma_v7_1_get_reg_offset(adev, i, regSDMA0_SDMA_QUEUE0_RB_BASE), ring->gpu_addr >> 8);
	WREG32_SOC15_IP(GC, sdma_v7_1_get_reg_offset(adev, i, regSDMA0_SDMA_QUEUE0_RB_BASE_HI), ring->gpu_addr >> 40);

	if (!restore)
		ring->wptr = 0;

	/* before programing wptr to a less value, need set minor_ptr_update first */
	WREG32_SOC15_IP(GC, sdma_v7_1_get_reg_offset(adev, i, regSDMA0_SDMA_QUEUE0_MINOR_PTR_UPDATE), 1);

	if (!amdgpu_sriov_vf(adev)) { /* only bare-metal use register write for wptr */
		WREG32_SOC15_IP(GC, sdma_v7_1_get_reg_offset(adev, i, regSDMA0_SDMA_QUEUE0_RB_WPTR), lower_32_bits(ring->wptr) << 2);
		WREG32_SOC15_IP(GC, sdma_v7_1_get_reg_offset(adev, i, regSDMA0_SDMA_QUEUE0_RB_WPTR_HI), upper_32_bits(ring->wptr) << 2);
	}

	doorbell = RREG32_SOC15_IP(GC, sdma_v7_1_get_reg_offset(adev, i, regSDMA0_SDMA_QUEUE0_DOORBELL));
	doorbell_offset = RREG32_SOC15_IP(GC, sdma_v7_1_get_reg_offset(adev, i, regSDMA0_SDMA_QUEUE0_DOORBELL_OFFSET));

	if (ring->use_doorbell) {
		doorbell = REG_SET_FIELD(doorbell, SDMA0_SDMA_QUEUE0_DOORBELL, ENABLE, 1);
		doorbell_offset = REG_SET_FIELD(doorbell_offset, SDMA0_SDMA_QUEUE0_DOORBELL_OFFSET,
				OFFSET, ring->doorbell_index);
	} else {
		doorbell = REG_SET_FIELD(doorbell, SDMA0_SDMA_QUEUE0_DOORBELL, ENABLE, 0);
	}
	WREG32_SOC15_IP(GC, sdma_v7_1_get_reg_offset(adev, i, regSDMA0_SDMA_QUEUE0_DOORBELL), doorbell);
	WREG32_SOC15_IP(GC, sdma_v7_1_get_reg_offset(adev, i, regSDMA0_SDMA_QUEUE0_DOORBELL_OFFSET), doorbell_offset);

	if (i == 0)
		adev->nbio.funcs->sdma_doorbell_range(adev, i, ring->use_doorbell,
					      ring->doorbell_index,
					      adev->doorbell_index.sdma_doorbell_range * adev->sdma.num_instances);

	if (amdgpu_sriov_vf(adev))
		sdma_v7_1_ring_set_wptr(ring);

	/* set minor_ptr_update to 0 after wptr programed */
	WREG32_SOC15_IP(GC, sdma_v7_1_get_reg_offset(adev, i, regSDMA0_SDMA_QUEUE0_MINOR_PTR_UPDATE), 0);

	/* Set up sdma hang watchdog */
	temp = RREG32_SOC15_IP(GC, sdma_v7_1_get_reg_offset(adev, i, regSDMA0_SDMA_WATCHDOG_CNTL));
	/* 100ms per unit */
	temp = REG_SET_FIELD(temp, SDMA0_SDMA_WATCHDOG_CNTL, QUEUE_HANG_COUNT,
			     max(adev->usec_timeout/100000, 1));
	WREG32_SOC15_IP(GC, sdma_v7_1_get_reg_offset(adev, i, regSDMA0_SDMA_WATCHDOG_CNTL), temp);

	/* Set up RESP_MODE to non-copy addresses */
	temp = RREG32_SOC15_IP(GC, sdma_v7_1_get_reg_offset(adev, i, regSDMA0_SDMA_UTCL1_CNTL));
	temp = REG_SET_FIELD(temp, SDMA0_SDMA_UTCL1_CNTL, RESP_MODE, 3);
	temp = REG_SET_FIELD(temp, SDMA0_SDMA_UTCL1_CNTL, REDO_DELAY, 9);
	WREG32_SOC15_IP(GC, sdma_v7_1_get_reg_offset(adev, i, regSDMA0_SDMA_UTCL1_CNTL), temp);

	/* program default cache read and write policy */
	temp = RREG32_SOC15_IP(GC, sdma_v7_1_get_reg_offset(adev, i, regSDMA0_SDMA_UTCL1_PAGE));
	/* clean read policy and write policy bits */
	temp &= 0xFF0FFF;
	temp |= ((CACHE_READ_POLICY_L2__DEFAULT << 12) |
		 (CACHE_WRITE_POLICY_L2__DEFAULT << 14));
	WREG32_SOC15_IP(GC, sdma_v7_1_get_reg_offset(adev, i, regSDMA0_SDMA_UTCL1_PAGE), temp);

	if (!amdgpu_sriov_vf(adev)) {
		/* unhalt engine */
		temp = RREG32_SOC15_IP(GC, sdma_v7_1_get_reg_offset(adev, i, regSDMA0_SDMA_MCU_CNTL));
		temp = REG_SET_FIELD(temp, SDMA0_SDMA_MCU_CNTL, HALT, 0);
		temp = REG_SET_FIELD(temp, SDMA0_SDMA_MCU_CNTL, RESET, 0);
		WREG32_SOC15_IP(GC, sdma_v7_1_get_reg_offset(adev, i, regSDMA0_SDMA_MCU_CNTL), temp);
	}

	/* enable DMA RB */
	rb_cntl = REG_SET_FIELD(rb_cntl, SDMA0_SDMA_QUEUE0_RB_CNTL, RB_ENABLE, 1);
	WREG32_SOC15_IP(GC, sdma_v7_1_get_reg_offset(adev, i, regSDMA0_SDMA_QUEUE0_RB_CNTL), rb_cntl);

	ib_cntl = RREG32_SOC15_IP(GC, sdma_v7_1_get_reg_offset(adev, i, regSDMA0_SDMA_QUEUE0_IB_CNTL));
	ib_cntl = REG_SET_FIELD(ib_cntl, SDMA0_SDMA_QUEUE0_IB_CNTL, IB_ENABLE, 1);
#ifdef __BIG_ENDIAN
	ib_cntl = REG_SET_FIELD(ib_cntl, SDMA0_SDMA_QUEUE0_IB_CNTL, IB_SWAP_ENABLE, 1);
#endif
	/* enable DMA IBs */
	WREG32_SOC15_IP(GC, sdma_v7_1_get_reg_offset(adev, i, regSDMA0_SDMA_QUEUE0_IB_CNTL), ib_cntl);
	ring->sched.ready = true;

	if (amdgpu_sriov_vf(adev)) { /* bare-metal sequence doesn't need below to lines */
		sdma_v7_1_inst_ctx_switch_enable(adev, true, i);
		sdma_v7_1_inst_enable(adev, true, i);
	}

	r = amdgpu_ring_test_helper(ring);
	if (r)
		ring->sched.ready = false;

	return r;
}

/**
 * sdma_v7_1_inst_gfx_resume - setup and start the async dma engines
 *
 * @adev: amdgpu_device pointer
 * @inst_mask: mask of dma engine instances to be enabled
 *
 * Set up the gfx DMA ring buffers and enable them.
 * Returns 0 for success, error for failure.
 */
static int sdma_v7_1_inst_gfx_resume(struct amdgpu_device *adev,
				     uint32_t inst_mask)
{
	int i, r;

	for_each_inst(i, inst_mask) {
		r = sdma_v7_1_gfx_resume_instance(adev, i, false);
		if (r)
			return r;
	}

	return 0;

}

/**
 * sdma_v7_1_inst_rlc_resume - setup and start the async dma engines
 *
 * @adev: amdgpu_device pointer
 * @inst_mask: mask of dma engine instances to be enabled
 *
 * Set up the compute DMA queues and enable them.
 * Returns 0 for success, error for failure.
 */
static int sdma_v7_1_inst_rlc_resume(struct amdgpu_device *adev,
				     uint32_t inst_mask)
{
	return 0;
}

static void sdma_v7_1_inst_free_ucode_buffer(struct amdgpu_device *adev,
					     uint32_t inst_mask)
{
	int i;

	for_each_inst(i, inst_mask) {
		amdgpu_bo_free_kernel(&adev->sdma.instance[i].sdma_fw_obj,
				      &adev->sdma.instance[i].sdma_fw_gpu_addr,
				      (void **)&adev->sdma.instance[i].sdma_fw_ptr);
	}
}

/**
 * sdma_v7_1_inst_load_microcode - load the sDMA ME ucode
 *
 * @adev: amdgpu_device pointer
 * @inst_mask: mask of dma engine instances to be enabled
 *
 * Loads the sDMA0/1 ucode.
 * Returns 0 for success, -EINVAL if the ucode is not available.
 */
static int sdma_v7_1_inst_load_microcode(struct amdgpu_device *adev,
					 uint32_t inst_mask)
{
	const struct sdma_firmware_header_v3_0 *hdr;
	const __le32 *fw_data;
	u32 fw_size;
	uint32_t tmp, sdma_status, ic_op_cntl;
	int i, r, j;

	/* halt the MEs */
	sdma_v7_1_inst_enable(adev, false, inst_mask);

	if (!adev->sdma.instance[0].fw)
		return -EINVAL;

	hdr = (const struct sdma_firmware_header_v3_0 *)
		adev->sdma.instance[0].fw->data;
	amdgpu_ucode_print_sdma_hdr(&hdr->header);

	fw_data = (const __le32 *)(adev->sdma.instance[0].fw->data +
			le32_to_cpu(hdr->ucode_offset_bytes));
	fw_size = le32_to_cpu(hdr->ucode_size_bytes);

	for_each_inst(i, inst_mask) {
		r = amdgpu_bo_create_reserved(adev, fw_size,
					      PAGE_SIZE,
					      AMDGPU_GEM_DOMAIN_VRAM,
					      &adev->sdma.instance[i].sdma_fw_obj,
					      &adev->sdma.instance[i].sdma_fw_gpu_addr,
					      (void **)&adev->sdma.instance[i].sdma_fw_ptr);
		if (r) {
			dev_err(adev->dev, "(%d) failed to create sdma ucode bo\n", r);
			return r;
		}

		memcpy(adev->sdma.instance[i].sdma_fw_ptr, fw_data, fw_size);

		amdgpu_bo_kunmap(adev->sdma.instance[i].sdma_fw_obj);
		amdgpu_bo_unreserve(adev->sdma.instance[i].sdma_fw_obj);

		tmp = RREG32_SOC15_IP(GC, sdma_v7_1_get_reg_offset(adev, i, regSDMA0_SDMA_IC_CNTL));
		tmp = REG_SET_FIELD(tmp, SDMA0_SDMA_IC_CNTL, GPA, 0);
		WREG32_SOC15_IP(GC, sdma_v7_1_get_reg_offset(adev, i, regSDMA0_SDMA_IC_CNTL), tmp);

		WREG32_SOC15_IP(GC, sdma_v7_1_get_reg_offset(adev, i, regSDMA0_SDMA_IC_BASE_LO),
			lower_32_bits(adev->sdma.instance[i].sdma_fw_gpu_addr));
		WREG32_SOC15_IP(GC, sdma_v7_1_get_reg_offset(adev, i, regSDMA0_SDMA_IC_BASE_HI),
			upper_32_bits(adev->sdma.instance[i].sdma_fw_gpu_addr));

		tmp = RREG32_SOC15_IP(GC, sdma_v7_1_get_reg_offset(adev, i, regSDMA0_SDMA_IC_OP_CNTL));
		tmp = REG_SET_FIELD(tmp, SDMA0_SDMA_IC_OP_CNTL, PRIME_ICACHE, 1);
		WREG32_SOC15_IP(GC, sdma_v7_1_get_reg_offset(adev, i, regSDMA0_SDMA_IC_OP_CNTL), tmp);

		/* Wait for sdma ucode init complete */
		for (j = 0; j < adev->usec_timeout; j++) {
			ic_op_cntl = RREG32_SOC15_IP(GC,
					sdma_v7_1_get_reg_offset(adev, i, regSDMA0_SDMA_IC_OP_CNTL));
			sdma_status = RREG32_SOC15_IP(GC,
					sdma_v7_1_get_reg_offset(adev, i, regSDMA0_SDMA_STATUS_REG));
			if ((REG_GET_FIELD(ic_op_cntl, SDMA0_SDMA_IC_OP_CNTL, ICACHE_PRIMED) == 1) &&
			    (REG_GET_FIELD(sdma_status, SDMA0_SDMA_STATUS_REG, UCODE_INIT_DONE) == 1))
				break;
			udelay(1);
		}

		if (j >= adev->usec_timeout) {
			dev_err(adev->dev, "failed to init sdma ucode\n");
			return -EINVAL;
		}
	}

	return 0;
}

static int sdma_v7_1_soft_reset(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	uint32_t inst_mask;
	u32 tmp;
	int i;

	inst_mask = GENMASK(NUM_XCC(adev->sdma.sdma_mask) - 1, 0);
	sdma_v7_1_inst_gfx_stop(adev, inst_mask);

	for_each_inst(i, inst_mask) {
		//tmp = RREG32_SOC15_IP(GC, sdma_v7_1_get_reg_offset(adev, i, regSDMA0_SDMA_FREEZE));
		//tmp |= SDMA0_SDMA_FREEZE__FREEZE_MASK;
		//WREG32_SOC15_IP(GC, sdma_v7_1_get_reg_offset(adev, i, regSDMA0_SDMA_FREEZE), tmp);
		tmp = RREG32_SOC15_IP(GC, sdma_v7_1_get_reg_offset(adev, i, regSDMA0_SDMA_MCU_CNTL));
		tmp |= SDMA0_SDMA_MCU_CNTL__HALT_MASK;
		tmp |= SDMA0_SDMA_MCU_CNTL__RESET_MASK;
		WREG32_SOC15_IP(GC, sdma_v7_1_get_reg_offset(adev, i, regSDMA0_SDMA_MCU_CNTL), tmp);

		WREG32_SOC15_IP(GC, sdma_v7_1_get_reg_offset(adev, i, regSDMA0_SDMA_QUEUE0_PREEMPT), 0);

		udelay(100);

		tmp = GRBM_SOFT_RESET__SOFT_RESET_SDMA0_MASK << i;
		WREG32_SOC15(GC, 0, regGRBM_SOFT_RESET, tmp);
		tmp = RREG32_SOC15(GC, 0, regGRBM_SOFT_RESET);

		udelay(100);

		WREG32_SOC15(GC, 0, regGRBM_SOFT_RESET, 0);
		tmp = RREG32_SOC15(GC, 0, regGRBM_SOFT_RESET);

		udelay(100);
	}

	return sdma_v7_1_inst_start(adev, inst_mask);
}

static bool sdma_v7_1_check_soft_reset(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	struct amdgpu_ring *ring;
	int i, r;
	long tmo = msecs_to_jiffies(1000);

	for (i = 0; i < adev->sdma.num_instances; i++) {
		ring = &adev->sdma.instance[i].ring;
		r = amdgpu_ring_test_ib(ring, tmo);
		if (r)
			return true;
	}

	return false;
}

static int sdma_v7_1_reset_queue(struct amdgpu_ring *ring,
				 unsigned int vmid,
				 struct amdgpu_fence *timedout_fence)
{
	struct amdgpu_device *adev = ring->adev;
	int r;

	if (ring->me >= adev->sdma.num_instances) {
		dev_err(adev->dev, "sdma instance not found\n");
		return -EINVAL;
	}

	amdgpu_ring_reset_helper_begin(ring, timedout_fence);

	r = amdgpu_mes_reset_legacy_queue(adev, ring, vmid, true, 0);
	if (r)
		return r;

	r = sdma_v7_1_gfx_resume_instance(adev, ring->me, true);
	if (r)
		return r;

	return amdgpu_ring_reset_helper_end(ring, timedout_fence);
}

/**
 * sdma_v7_1_inst_start - setup and start the async dma engines
 *
 * @adev: amdgpu_device pointer
 * @inst_mask: mask of dma engine instances to be enabled
 *
 * Set up the DMA engines and enable them.
 * Returns 0 for success, error for failure.
 */
static int sdma_v7_1_inst_start(struct amdgpu_device *adev,
				uint32_t inst_mask)
{
	int r = 0;

	if (amdgpu_sriov_vf(adev)) {
		sdma_v7_1_inst_ctx_switch_enable(adev, false, inst_mask);
		sdma_v7_1_inst_enable(adev, false, inst_mask);

		/* set RB registers */
		r = sdma_v7_1_inst_gfx_resume(adev, inst_mask);
		return r;
	}

	if (adev->firmware.load_type == AMDGPU_FW_LOAD_DIRECT) {
		r = sdma_v7_1_inst_load_microcode(adev, inst_mask);
		if (r) {
			sdma_v7_1_inst_free_ucode_buffer(adev, inst_mask);
			return r;
		}

		if (amdgpu_emu_mode == 1)
			msleep(1000);
	}

	/* unhalt the MEs */
	sdma_v7_1_inst_enable(adev, true, inst_mask);
	/* enable sdma ring preemption */
	sdma_v7_1_inst_ctx_switch_enable(adev, true, inst_mask);

	/* start the gfx rings and rlc compute queues */
	r = sdma_v7_1_inst_gfx_resume(adev, inst_mask);
	if (r)
		return r;
	r = sdma_v7_1_inst_rlc_resume(adev, inst_mask);

	return r;
}

static int sdma_v7_1_mqd_init(struct amdgpu_device *adev, void *mqd,
			      struct amdgpu_mqd_prop *prop)
{
	struct v12_sdma_mqd *m = mqd;
	uint64_t wb_gpu_addr;

	m->sdmax_rlcx_rb_cntl =
		order_base_2(prop->queue_size / 4) << SDMA0_SDMA_QUEUE0_RB_CNTL__RB_SIZE__SHIFT |
		1 << SDMA0_SDMA_QUEUE0_RB_CNTL__RPTR_WRITEBACK_ENABLE__SHIFT |
		4 << SDMA0_SDMA_QUEUE0_RB_CNTL__RPTR_WRITEBACK_TIMER__SHIFT |
		1 << SDMA0_SDMA_QUEUE0_RB_CNTL__MCU_WPTR_POLL_ENABLE__SHIFT;

	m->sdmax_rlcx_rb_base = lower_32_bits(prop->hqd_base_gpu_addr >> 8);
	m->sdmax_rlcx_rb_base_hi = upper_32_bits(prop->hqd_base_gpu_addr >> 8);

	wb_gpu_addr = prop->wptr_gpu_addr;
	m->sdmax_rlcx_rb_wptr_poll_addr_lo = lower_32_bits(wb_gpu_addr);
	m->sdmax_rlcx_rb_wptr_poll_addr_hi = upper_32_bits(wb_gpu_addr);

	wb_gpu_addr = prop->rptr_gpu_addr;
	m->sdmax_rlcx_rb_rptr_addr_lo = lower_32_bits(wb_gpu_addr);
	m->sdmax_rlcx_rb_rptr_addr_hi = upper_32_bits(wb_gpu_addr);

	m->sdmax_rlcx_ib_cntl = RREG32_SOC15_IP(GC, sdma_v7_1_get_reg_offset(adev, 0,
							regSDMA0_SDMA_QUEUE0_IB_CNTL));

	m->sdmax_rlcx_doorbell_offset =
		prop->doorbell_index << SDMA0_SDMA_QUEUE0_DOORBELL_OFFSET__OFFSET__SHIFT;

	m->sdmax_rlcx_doorbell = REG_SET_FIELD(0, SDMA0_SDMA_QUEUE0_DOORBELL, ENABLE, 1);

	m->sdmax_rlcx_doorbell_log = 0;
	m->sdmax_rlcx_rb_aql_cntl = 0x4000;	//regSDMA0_SDMA_QUEUE0_RB_AQL_CNTL_DEFAULT;
	m->sdmax_rlcx_dummy_reg = 0xf;	//regSDMA0_SDMA_QUEUE0_DUMMY_REG_DEFAULT;

	m->sdmax_rlcx_csa_addr_lo = lower_32_bits(prop->csa_addr);
	m->sdmax_rlcx_csa_addr_hi = upper_32_bits(prop->csa_addr);

	return 0;
}

static void sdma_v7_1_set_mqd_funcs(struct amdgpu_device *adev)
{
	adev->mqds[AMDGPU_HW_IP_DMA].mqd_size = sizeof(struct v12_sdma_mqd);
	adev->mqds[AMDGPU_HW_IP_DMA].init_mqd = sdma_v7_1_mqd_init;
}

/**
 * sdma_v7_1_ring_test_ring - simple async dma engine test
 *
 * @ring: amdgpu_ring structure holding ring information
 *
 * Test the DMA engine by writing using it to write an
 * value to memory.
 * Returns 0 for success, error for failure.
 */
static int sdma_v7_1_ring_test_ring(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	unsigned i;
	unsigned index;
	int r;
	u32 tmp;
	u64 gpu_addr;

	tmp = 0xCAFEDEAD;

	r = amdgpu_device_wb_get(adev, &index);
	if (r) {
		dev_err(adev->dev, "(%d) failed to allocate wb slot\n", r);
		return r;
	}

	gpu_addr = adev->wb.gpu_addr + (index * 4);
	adev->wb.wb[index] = cpu_to_le32(tmp);

	r = amdgpu_ring_alloc(ring, 5);
	if (r) {
		DRM_ERROR("amdgpu: dma failed to lock ring %d (%d).\n", ring->idx, r);
		amdgpu_device_wb_free(adev, index);
		return r;
	}

	amdgpu_ring_write(ring, SDMA_PKT_COPY_LINEAR_HEADER_OP(SDMA_OP_WRITE) |
			  SDMA_PKT_COPY_LINEAR_HEADER_SUB_OP(SDMA_SUBOP_WRITE_LINEAR));
	amdgpu_ring_write(ring, lower_32_bits(gpu_addr));
	amdgpu_ring_write(ring, upper_32_bits(gpu_addr));
	amdgpu_ring_write(ring, SDMA_PKT_WRITE_UNTILED_DW_3_COUNT(0));
	amdgpu_ring_write(ring, 0xDEADBEEF);
	amdgpu_ring_commit(ring);

	for (i = 0; i < adev->usec_timeout; i++) {
		tmp = le32_to_cpu(adev->wb.wb[index]);
		if (tmp == 0xDEADBEEF)
			break;
		if (amdgpu_emu_mode == 1)
			msleep(1);
		else
			udelay(1);
	}

	if (i >= adev->usec_timeout)
		r = -ETIMEDOUT;

	amdgpu_device_wb_free(adev, index);

	return r;
}

/**
 * sdma_v7_1_ring_test_ib - test an IB on the DMA engine
 *
 * @ring: amdgpu_ring structure holding ring information
 * @timeout: timeout value in jiffies, or MAX_SCHEDULE_TIMEOUT
 *
 * Test a simple IB in the DMA ring.
 * Returns 0 on success, error on failure.
 */
static int sdma_v7_1_ring_test_ib(struct amdgpu_ring *ring, long timeout)
{
	struct amdgpu_device *adev = ring->adev;
	struct amdgpu_ib ib;
	struct dma_fence *f = NULL;
	unsigned index;
	long r;
	u32 tmp = 0;
	u64 gpu_addr;

	tmp = 0xCAFEDEAD;
	memset(&ib, 0, sizeof(ib));

	r = amdgpu_device_wb_get(adev, &index);
	if (r) {
		dev_err(adev->dev, "(%ld) failed to allocate wb slot\n", r);
		return r;
	}

	gpu_addr = adev->wb.gpu_addr + (index * 4);
	adev->wb.wb[index] = cpu_to_le32(tmp);

	r = amdgpu_ib_get(adev, NULL, 256, AMDGPU_IB_POOL_DIRECT, &ib);
	if (r) {
		DRM_ERROR("amdgpu: failed to get ib (%ld).\n", r);
		goto err0;
	}

	ib.ptr[0] = SDMA_PKT_COPY_LINEAR_HEADER_OP(SDMA_OP_WRITE) |
		SDMA_PKT_COPY_LINEAR_HEADER_SUB_OP(SDMA_SUBOP_WRITE_LINEAR);
	ib.ptr[1] = lower_32_bits(gpu_addr);
	ib.ptr[2] = upper_32_bits(gpu_addr);
	ib.ptr[3] = SDMA_PKT_WRITE_UNTILED_DW_3_COUNT(0);
	ib.ptr[4] = 0xDEADBEEF;
	ib.ptr[5] = SDMA_PKT_NOP_HEADER_OP(SDMA_OP_NOP);
	ib.ptr[6] = SDMA_PKT_NOP_HEADER_OP(SDMA_OP_NOP);
	ib.ptr[7] = SDMA_PKT_NOP_HEADER_OP(SDMA_OP_NOP);
	ib.length_dw = 8;

	r = amdgpu_ib_schedule(ring, 1, &ib, NULL, &f);
	if (r)
		goto err1;

	r = dma_fence_wait_timeout(f, false, timeout);
	if (r == 0) {
		DRM_ERROR("amdgpu: IB test timed out\n");
		r = -ETIMEDOUT;
		goto err1;
	} else if (r < 0) {
		DRM_ERROR("amdgpu: fence wait failed (%ld).\n", r);
		goto err1;
	}

	tmp = le32_to_cpu(adev->wb.wb[index]);

	if (tmp == 0xDEADBEEF)
		r = 0;
	else
		r = -EINVAL;

err1:
	amdgpu_ib_free(&ib, NULL);
	dma_fence_put(f);
err0:
	amdgpu_device_wb_free(adev, index);
	return r;
}


/**
 * sdma_v7_1_vm_copy_pte - update PTEs by copying them from the GART
 *
 * @ib: indirect buffer to fill with commands
 * @pe: addr of the page entry
 * @src: src addr to copy from
 * @count: number of page entries to update
 *
 * Update PTEs by copying them from the GART using sDMA.
 */
static void sdma_v7_1_vm_copy_pte(struct amdgpu_ib *ib,
				  uint64_t pe, uint64_t src,
				  unsigned count)
{
	unsigned bytes = count * 8;

	ib->ptr[ib->length_dw++] = SDMA_PKT_COPY_LINEAR_HEADER_OP(SDMA_OP_COPY) |
		SDMA_PKT_COPY_LINEAR_HEADER_SUB_OP(SDMA_SUBOP_COPY_LINEAR);

	ib->ptr[ib->length_dw++] = bytes - 1;
	ib->ptr[ib->length_dw++] = 0; /* src/dst endian swap */
	ib->ptr[ib->length_dw++] = lower_32_bits(src);
	ib->ptr[ib->length_dw++] = upper_32_bits(src);
	ib->ptr[ib->length_dw++] = lower_32_bits(pe);
	ib->ptr[ib->length_dw++] = upper_32_bits(pe);

}

/**
 * sdma_v7_1_vm_write_pte - update PTEs by writing them manually
 *
 * @ib: indirect buffer to fill with commands
 * @pe: addr of the page entry
 * @value: dst addr to write into pe
 * @count: number of page entries to update
 * @incr: increase next addr by incr bytes
 *
 * Update PTEs by writing them manually using sDMA.
 */
static void sdma_v7_1_vm_write_pte(struct amdgpu_ib *ib, uint64_t pe,
				   uint64_t value, unsigned count,
				   uint32_t incr)
{
	unsigned ndw = count * 2;

	ib->ptr[ib->length_dw++] = SDMA_PKT_COPY_LINEAR_HEADER_OP(SDMA_OP_WRITE) |
		SDMA_PKT_COPY_LINEAR_HEADER_SUB_OP(SDMA_SUBOP_WRITE_LINEAR);
	ib->ptr[ib->length_dw++] = lower_32_bits(pe);
	ib->ptr[ib->length_dw++] = upper_32_bits(pe);
	ib->ptr[ib->length_dw++] = ndw - 1;
	for (; ndw > 0; ndw -= 2) {
		ib->ptr[ib->length_dw++] = lower_32_bits(value);
		ib->ptr[ib->length_dw++] = upper_32_bits(value);
		value += incr;
	}
}

/**
 * sdma_v7_1_vm_set_pte_pde - update the page tables using sDMA
 *
 * @ib: indirect buffer to fill with commands
 * @pe: addr of the page entry
 * @addr: dst addr to write into pe
 * @count: number of page entries to update
 * @incr: increase next addr by incr bytes
 * @flags: access flags
 *
 * Update the page tables using sDMA.
 */
static void sdma_v7_1_vm_set_pte_pde(struct amdgpu_ib *ib,
				     uint64_t pe,
				     uint64_t addr, unsigned count,
				     uint32_t incr, uint64_t flags)
{
	/* for physically contiguous pages (vram) */
	u32 header = SDMA_PKT_COPY_LINEAR_HEADER_OP(SDMA_OP_PTEPDE);

	if (amdgpu_mtype_local)
		header |= SDMA_PKT_PTEPDE_COPY_HEADER_MTYPE(0x3);
	else
		header |= (SDMA_PKT_PTEPDE_COPY_HEADER_MTYPE(0x2) |
			   SDMA_PKT_PTEPDE_COPY_HEADER_SNOOP(0x1) |
			   SDMA_PKT_PTEPDE_COPY_HEADER_SCOPE(0x3));

	ib->ptr[ib->length_dw++] = header;
	ib->ptr[ib->length_dw++] = lower_32_bits(pe); /* dst addr */
	ib->ptr[ib->length_dw++] = upper_32_bits(pe);
	ib->ptr[ib->length_dw++] = lower_32_bits(flags); /* mask */
	ib->ptr[ib->length_dw++] = upper_32_bits(flags);
	ib->ptr[ib->length_dw++] = lower_32_bits(addr); /* value */
	ib->ptr[ib->length_dw++] = upper_32_bits(addr);
	ib->ptr[ib->length_dw++] = incr; /* increment size */
	ib->ptr[ib->length_dw++] = 0;
	ib->ptr[ib->length_dw++] = count - 1; /* number of entries */
}

/**
 * sdma_v7_1_ring_pad_ib - pad the IB
 *
 * @ring: amdgpu ring pointer
 * @ib: indirect buffer to fill with padding
 *
 * Pad the IB with NOPs to a boundary multiple of 8.
 */
static void sdma_v7_1_ring_pad_ib(struct amdgpu_ring *ring, struct amdgpu_ib *ib)
{
	struct amdgpu_sdma_instance *sdma = amdgpu_sdma_get_instance_from_ring(ring);
	u32 pad_count;
	int i;

	pad_count = (-ib->length_dw) & 0x7;
	for (i = 0; i < pad_count; i++)
		if (sdma && sdma->burst_nop && (i == 0))
			ib->ptr[ib->length_dw++] =
				SDMA_PKT_COPY_LINEAR_HEADER_OP(SDMA_OP_NOP) |
				SDMA_PKT_NOP_HEADER_COUNT(pad_count - 1);
		else
			ib->ptr[ib->length_dw++] =
				SDMA_PKT_COPY_LINEAR_HEADER_OP(SDMA_OP_NOP);
}

/**
 * sdma_v7_1_ring_emit_pipeline_sync - sync the pipeline
 *
 * @ring: amdgpu_ring pointer
 *
 * Make sure all previous operations are completed (CIK).
 */
static void sdma_v7_1_ring_emit_pipeline_sync(struct amdgpu_ring *ring)
{
	uint32_t seq = ring->fence_drv.sync_seq;
	uint64_t addr = ring->fence_drv.gpu_addr;

	/* wait for idle */
	amdgpu_ring_write(ring, SDMA_PKT_COPY_LINEAR_HEADER_OP(SDMA_OP_POLL_REGMEM) |
			  SDMA_PKT_POLL_REGMEM_HEADER_FUNC(3) | /* equal */
			  SDMA_PKT_POLL_REGMEM_HEADER_MEM_POLL(1));
	amdgpu_ring_write(ring, addr & 0xfffffffc);
	amdgpu_ring_write(ring, upper_32_bits(addr) & 0xffffffff);
	amdgpu_ring_write(ring, seq); /* reference */
	amdgpu_ring_write(ring, 0xffffffff); /* mask */
	amdgpu_ring_write(ring, SDMA_PKT_POLL_REGMEM_DW5_RETRY_COUNT(0xfff) |
			  SDMA_PKT_POLL_REGMEM_DW5_INTERVAL(4)); /* retry count, poll interval */
}

/**
 * sdma_v7_1_ring_emit_vm_flush - vm flush using sDMA
 *
 * @ring: amdgpu_ring pointer
 * @vmid: vmid number to use
 * @pd_addr: address
 *
 * Update the page table base and flush the VM TLB
 * using sDMA.
 */
static void sdma_v7_1_ring_emit_vm_flush(struct amdgpu_ring *ring,
					 unsigned vmid, uint64_t pd_addr)
{
	amdgpu_gmc_emit_flush_gpu_tlb(ring, vmid, pd_addr);
}

static void sdma_v7_1_ring_emit_wreg(struct amdgpu_ring *ring,
				     uint32_t reg, uint32_t val)
{
	/* SRBM WRITE command will not support on sdma v7.
	 * Use Register WRITE command instead, which OPCODE is same as SRBM WRITE
	 */
	amdgpu_ring_write(ring, SDMA_PKT_COPY_LINEAR_HEADER_OP(SDMA_OP_SRBM_WRITE));
	amdgpu_ring_write(ring, soc_v1_0_normalize_xcc_reg_offset(reg) << 2);
	amdgpu_ring_write(ring, val);
}

static void sdma_v7_1_ring_emit_reg_wait(struct amdgpu_ring *ring, uint32_t reg,
					 uint32_t val, uint32_t mask)
{
	amdgpu_ring_write(ring, SDMA_PKT_COPY_LINEAR_HEADER_OP(SDMA_OP_POLL_REGMEM) |
			  SDMA_PKT_POLL_REGMEM_HEADER_FUNC(3)); /* equal */
	amdgpu_ring_write(ring, soc_v1_0_normalize_xcc_reg_offset(reg) << 2);
	amdgpu_ring_write(ring, 0);
	amdgpu_ring_write(ring, val); /* reference */
	amdgpu_ring_write(ring, mask); /* mask */
	amdgpu_ring_write(ring, SDMA_PKT_POLL_REGMEM_DW5_RETRY_COUNT(0xfff) |
			  SDMA_PKT_POLL_REGMEM_DW5_INTERVAL(10));
}

static void sdma_v7_1_ring_emit_reg_write_reg_wait(struct amdgpu_ring *ring,
						   uint32_t reg0, uint32_t reg1,
						   uint32_t ref, uint32_t mask)
{
	amdgpu_ring_emit_wreg(ring, reg0, ref);
	/* wait for a cycle to reset vm_inv_eng*_ack */
	amdgpu_ring_emit_reg_wait(ring, reg0, 0, 0);
	amdgpu_ring_emit_reg_wait(ring, reg1, mask, mask);
}

static int sdma_v7_1_early_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	int r;

	r = amdgpu_sdma_init_microcode(adev, 0, true);
	if (r) {
		DRM_ERROR("Failed to init sdma firmware!\n");
		return r;
	}

	sdma_v7_1_set_ring_funcs(adev);
	sdma_v7_1_set_buffer_funcs(adev);
	sdma_v7_1_set_vm_pte_funcs(adev);
	sdma_v7_1_set_irq_funcs(adev);
	sdma_v7_1_set_mqd_funcs(adev);

	return 0;
}

static int sdma_v7_1_sw_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_ring *ring;
	int r, i;
	struct amdgpu_device *adev = ip_block->adev;
	uint32_t reg_count = ARRAY_SIZE(sdma_reg_list_7_1);
	uint32_t *ptr;
	u32 xcc_id;

	/* SDMA trap event */
	r = amdgpu_irq_add_id(adev, SOC_V1_0_IH_CLIENTID_GFX,
			      GFX_12_1_0__SRCID__SDMA_TRAP,
			      &adev->sdma.trap_irq);
	if (r)
		return r;

	for (i = 0; i < adev->sdma.num_instances; i++) {
		ring = &adev->sdma.instance[i].ring;
		ring->ring_obj = NULL;
		ring->use_doorbell = true;
		ring->me = i;

		for (xcc_id = 0; xcc_id < fls(adev->gfx.xcc_mask); xcc_id++) {
			if (adev->sdma.instance[i].xcc_id == GET_INST(GC, xcc_id))
				break;
		}

		DRM_DEBUG("SDMA%d.%d use_doorbell being set to: [%s]\n",
				xcc_id, GET_INST(SDMA0, i) % adev->sdma.num_inst_per_xcc,
				ring->use_doorbell?"true":"false");

		ring->doorbell_index =
			(adev->doorbell_index.sdma_engine[i] << 1); // get DWORD offset

		ring->vm_hub = AMDGPU_GFXHUB(xcc_id);
		sprintf(ring->name, "sdma%d.%d", xcc_id,
				GET_INST(SDMA0, i) % adev->sdma.num_inst_per_xcc);
		r = amdgpu_ring_init(adev, ring, 1024,
				     &adev->sdma.trap_irq,
				     AMDGPU_SDMA_IRQ_INSTANCE0 + i,
				     AMDGPU_RING_PRIO_DEFAULT, NULL);
		if (r)
			return r;
	}

	adev->sdma.supported_reset =
		amdgpu_get_soft_full_reset_mask(&adev->sdma.instance[0].ring);
	if (!amdgpu_sriov_vf(adev) &&
	    !adev->debug_disable_gpu_ring_reset)
		adev->sdma.supported_reset |= AMDGPU_RESET_TYPE_PER_QUEUE;

	r = amdgpu_sdma_sysfs_reset_mask_init(adev);
	if (r)
		return r;
	/* Allocate memory for SDMA IP Dump buffer */
	ptr = kcalloc(adev->sdma.num_instances * reg_count, sizeof(uint32_t), GFP_KERNEL);
	if (ptr)
		adev->sdma.ip_dump = ptr;
	else
		DRM_ERROR("Failed to allocated memory for SDMA IP Dump\n");

#ifdef CONFIG_DRM_AMDGPU_NAVI3X_USERQ
	adev->userq_funcs[AMDGPU_HW_IP_DMA] = &userq_mes_funcs;
#endif

	return r;
}

static int sdma_v7_1_sw_fini(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	int i;

	for (i = 0; i < adev->sdma.num_instances; i++)
		amdgpu_ring_fini(&adev->sdma.instance[i].ring);

	amdgpu_sdma_sysfs_reset_mask_fini(adev);
	amdgpu_sdma_destroy_inst_ctx(adev, true);

	if (adev->firmware.load_type == AMDGPU_FW_LOAD_DIRECT)
		sdma_v7_1_inst_free_ucode_buffer(adev, adev->sdma.sdma_mask);

	kfree(adev->sdma.ip_dump);

	return 0;
}

static int sdma_v7_1_hw_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	uint32_t inst_mask;

	inst_mask = GENMASK(adev->sdma.num_instances - 1, 0);

	return sdma_v7_1_inst_start(adev, inst_mask);
}

static int sdma_v7_1_hw_fini(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;

	if (amdgpu_sriov_vf(adev))
		return 0;

	sdma_v7_1_inst_ctx_switch_enable(adev, false, adev->sdma.sdma_mask);
	sdma_v7_1_inst_enable(adev, false, adev->sdma.sdma_mask);

	return 0;
}

static int sdma_v7_1_suspend(struct amdgpu_ip_block *ip_block)
{
	return sdma_v7_1_hw_fini(ip_block);
}

static int sdma_v7_1_resume(struct amdgpu_ip_block *ip_block)
{
	return sdma_v7_1_hw_init(ip_block);
}

static bool sdma_v7_1_is_idle(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	u32 i;

	for (i = 0; i < adev->sdma.num_instances; i++) {
		u32 tmp = RREG32(sdma_v7_1_get_reg_offset(adev, i, regSDMA0_SDMA_STATUS_REG));

		if (!(tmp & SDMA0_SDMA_STATUS_REG__IDLE_MASK))
			return false;
	}

	return true;
}

static int sdma_v7_1_wait_for_idle(struct amdgpu_ip_block *ip_block)
{
	unsigned i, j;
	u32 sdma[AMDGPU_MAX_SDMA_INSTANCES];
	struct amdgpu_device *adev = ip_block->adev;

	for (i = 0; i < adev->usec_timeout; i++) {
		for (j = 0; j < adev->sdma.num_instances; j++) {
			sdma[j] = RREG32(sdma_v7_1_get_reg_offset(adev,
						j, regSDMA0_SDMA_STATUS_REG));
			if (!(sdma[j] & SDMA0_SDMA_STATUS_REG__IDLE_MASK))
				break;
		}
		if (j == adev->sdma.num_instances)
			return 0;
		udelay(1);
	}
	return -ETIMEDOUT;
}

static int sdma_v7_1_ring_preempt_ib(struct amdgpu_ring *ring)
{
	int i, r = 0;
	struct amdgpu_device *adev = ring->adev;
	u32 index = 0;
	u64 sdma_gfx_preempt;

	amdgpu_sdma_get_index_from_ring(ring, &index);
	sdma_gfx_preempt =
		sdma_v7_1_get_reg_offset(adev, index, regSDMA0_SDMA_QUEUE0_PREEMPT);

	/* assert preemption condition */
	amdgpu_ring_set_preempt_cond_exec(ring, false);

	/* emit the trailing fence */
	ring->trail_seq += 1;
	r = amdgpu_ring_alloc(ring, 10);
	if (r) {
		DRM_ERROR("ring %d failed to be allocated \n", ring->idx);
		return r;
	}
	sdma_v7_1_ring_emit_fence(ring, ring->trail_fence_gpu_addr,
				  ring->trail_seq, 0);
	amdgpu_ring_commit(ring);

	/* assert IB preemption */
	WREG32(sdma_gfx_preempt, 1);

	/* poll the trailing fence */
	for (i = 0; i < adev->usec_timeout; i++) {
		if (ring->trail_seq ==
		    le32_to_cpu(*(ring->trail_fence_cpu_addr)))
			break;
		udelay(1);
	}

	if (i >= adev->usec_timeout) {
		r = -EINVAL;
		DRM_ERROR("ring %d failed to be preempted\n", ring->idx);
	}

	/* deassert IB preemption */
	WREG32(sdma_gfx_preempt, 0);

	/* deassert the preemption condition */
	amdgpu_ring_set_preempt_cond_exec(ring, true);
	return r;
}

static int sdma_v7_1_set_trap_irq_state(struct amdgpu_device *adev,
					struct amdgpu_irq_src *source,
					unsigned type,
					enum amdgpu_interrupt_state state)
{
	u32 sdma_cntl;

	u32 reg_offset = sdma_v7_1_get_reg_offset(adev, type, regSDMA0_SDMA_CNTL);

	sdma_cntl = RREG32(reg_offset);
	sdma_cntl = REG_SET_FIELD(sdma_cntl, SDMA0_SDMA_CNTL, TRAP_ENABLE,
		       state == AMDGPU_IRQ_STATE_ENABLE ? 1 : 0);
	WREG32(reg_offset, sdma_cntl);

	return 0;
}

static int sdma_v7_1_process_trap_irq(struct amdgpu_device *adev,
				      struct amdgpu_irq_src *source,
				      struct amdgpu_iv_entry *entry)
{
	int inst, instances, queue, xcc_id = 0;

	DRM_DEBUG("IH: SDMA trap\n");

	if (drm_WARN_ON_ONCE(&adev->ddev,
			     adev->enable_mes &&
			     (entry->src_data[0] & AMDGPU_FENCE_MES_QUEUE_FLAG)))
		return 0;

	queue = entry->ring_id & 0xf;
	if (adev->gfx.funcs && adev->gfx.funcs->ih_node_to_logical_xcc)
		xcc_id = adev->gfx.funcs->ih_node_to_logical_xcc(adev, entry->node_id);
	else
		dev_warn(adev->dev, "IH: SDMA may get wrong xcc id as gfx function not available\n");
	inst = ((entry->ring_id & 0xf0) >> 4) +
		GET_INST(GC, xcc_id) * adev->sdma.num_inst_per_xcc;
	for (instances = 0; instances < adev->sdma.num_instances; instances++) {
		if (inst == GET_INST(SDMA0, instances))
			break;
	}
	if (instances > adev->sdma.num_instances - 1) {
		DRM_ERROR("IH: wrong ring_ID detected, as wrong sdma instance\n");
		return -EINVAL;
	}

	switch (entry->client_id) {
	case SOC_V1_0_IH_CLIENTID_GFX:
		switch (queue) {
		case 0:
			amdgpu_fence_process(&adev->sdma.instance[instances].ring);
			break;
		default:
			break;
		}
		break;
	}
	return 0;
}

static int sdma_v7_1_process_illegal_inst_irq(struct amdgpu_device *adev,
					      struct amdgpu_irq_src *source,
					      struct amdgpu_iv_entry *entry)
{
	return 0;
}

static int sdma_v7_1_set_clockgating_state(struct amdgpu_ip_block *ip_block,
					   enum amd_clockgating_state state)
{
	return 0;
}

static int sdma_v7_1_set_powergating_state(struct amdgpu_ip_block *ip_block,
					  enum amd_powergating_state state)
{
	return 0;
}

static void sdma_v7_1_get_clockgating_state(struct amdgpu_ip_block *ip_block,
					    u64 *flags)
{
}

static void sdma_v7_1_print_ip_state(struct amdgpu_ip_block *ip_block, struct drm_printer *p)
{
	struct amdgpu_device *adev = ip_block->adev;
	int i, j;
	uint32_t reg_count = ARRAY_SIZE(sdma_reg_list_7_1);
	uint32_t instance_offset;

	if (!adev->sdma.ip_dump)
		return;

	drm_printf(p, "num_instances:%d\n", adev->sdma.num_instances);
	for (i = 0; i < adev->sdma.num_instances; i++) {
		instance_offset = i * reg_count;
		drm_printf(p, "\nInstance:%d\n", i);

		for (j = 0; j < reg_count; j++)
			drm_printf(p, "%-50s \t 0x%08x\n", sdma_reg_list_7_1[j].reg_name,
				   adev->sdma.ip_dump[instance_offset + j]);
	}
}

static void sdma_v7_1_dump_ip_state(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	int i, j;
	uint32_t instance_offset;
	uint32_t reg_count = ARRAY_SIZE(sdma_reg_list_7_1);

	if (!adev->sdma.ip_dump)
		return;

	amdgpu_gfx_off_ctrl(adev, false);
	for (i = 0; i < adev->sdma.num_instances; i++) {
		instance_offset = i * reg_count;
		for (j = 0; j < reg_count; j++)
			adev->sdma.ip_dump[instance_offset + j] =
				RREG32(sdma_v7_1_get_reg_offset(adev, i,
				       sdma_reg_list_7_1[j].reg_offset));
	}
	amdgpu_gfx_off_ctrl(adev, true);
}

const struct amd_ip_funcs sdma_v7_1_ip_funcs = {
	.name = "sdma_v7_1",
	.early_init = sdma_v7_1_early_init,
	.late_init = NULL,
	.sw_init = sdma_v7_1_sw_init,
	.sw_fini = sdma_v7_1_sw_fini,
	.hw_init = sdma_v7_1_hw_init,
	.hw_fini = sdma_v7_1_hw_fini,
	.suspend = sdma_v7_1_suspend,
	.resume = sdma_v7_1_resume,
	.is_idle = sdma_v7_1_is_idle,
	.wait_for_idle = sdma_v7_1_wait_for_idle,
	.soft_reset = sdma_v7_1_soft_reset,
	.check_soft_reset = sdma_v7_1_check_soft_reset,
	.set_clockgating_state = sdma_v7_1_set_clockgating_state,
	.set_powergating_state = sdma_v7_1_set_powergating_state,
	.get_clockgating_state = sdma_v7_1_get_clockgating_state,
	.dump_ip_state = sdma_v7_1_dump_ip_state,
	.print_ip_state = sdma_v7_1_print_ip_state,
};

static const struct amdgpu_ring_funcs sdma_v7_1_ring_funcs = {
	.type = AMDGPU_RING_TYPE_SDMA,
	.align_mask = 0xf,
	.nop = SDMA_PKT_NOP_HEADER_OP(SDMA_OP_NOP),
	.support_64bit_ptrs = true,
	.secure_submission_supported = true,
	.get_rptr = sdma_v7_1_ring_get_rptr,
	.get_wptr = sdma_v7_1_ring_get_wptr,
	.set_wptr = sdma_v7_1_ring_set_wptr,
	.emit_frame_size =
		5 + /* sdma_v7_1_ring_init_cond_exec */
		6 + /* sdma_v7_1_ring_emit_pipeline_sync */
		/* sdma_v7_1_ring_emit_vm_flush */
		SOC15_FLUSH_GPU_TLB_NUM_WREG * 3 +
		SOC15_FLUSH_GPU_TLB_NUM_REG_WAIT * 6 +
		10 + 10 + 10, /* sdma_v7_1_ring_emit_fence x3 for user fence, vm fence */
	.emit_ib_size = 5 + 7 + 6, /* sdma_v7_1_ring_emit_ib */
	.emit_ib = sdma_v7_1_ring_emit_ib,
	.emit_mem_sync = sdma_v7_1_ring_emit_mem_sync,
	.emit_fence = sdma_v7_1_ring_emit_fence,
	.emit_pipeline_sync = sdma_v7_1_ring_emit_pipeline_sync,
	.emit_vm_flush = sdma_v7_1_ring_emit_vm_flush,
	.test_ring = sdma_v7_1_ring_test_ring,
	.test_ib = sdma_v7_1_ring_test_ib,
	.insert_nop = sdma_v7_1_ring_insert_nop,
	.pad_ib = sdma_v7_1_ring_pad_ib,
	.emit_wreg = sdma_v7_1_ring_emit_wreg,
	.emit_reg_wait = sdma_v7_1_ring_emit_reg_wait,
	.emit_reg_write_reg_wait = sdma_v7_1_ring_emit_reg_write_reg_wait,
	.init_cond_exec = sdma_v7_1_ring_init_cond_exec,
	.preempt_ib = sdma_v7_1_ring_preempt_ib,
	.reset = sdma_v7_1_reset_queue,
};

static void sdma_v7_1_set_ring_funcs(struct amdgpu_device *adev)
{
	int i, dev_inst;

	for (i = 0; i < adev->sdma.num_instances; i++) {
		adev->sdma.instance[i].ring.funcs = &sdma_v7_1_ring_funcs;
		adev->sdma.instance[i].ring.me = i;

		dev_inst = GET_INST(SDMA0, i);
		/* XCC to which SDMA belongs depends on physical instance */
		adev->sdma.instance[i].xcc_id =
			dev_inst / adev->sdma.num_inst_per_xcc;
	}
}

static const struct amdgpu_irq_src_funcs sdma_v7_1_trap_irq_funcs = {
	.set = sdma_v7_1_set_trap_irq_state,
	.process = sdma_v7_1_process_trap_irq,
};

static const struct amdgpu_irq_src_funcs sdma_v7_1_illegal_inst_irq_funcs = {
	.process = sdma_v7_1_process_illegal_inst_irq,
};

static void sdma_v7_1_set_irq_funcs(struct amdgpu_device *adev)
{
	adev->sdma.trap_irq.num_types = AMDGPU_SDMA_IRQ_INSTANCE0 +
					adev->sdma.num_instances;
	adev->sdma.trap_irq.funcs = &sdma_v7_1_trap_irq_funcs;
	adev->sdma.illegal_inst_irq.funcs = &sdma_v7_1_illegal_inst_irq_funcs;
}

/**
 * sdma_v7_1_emit_copy_buffer - copy buffer using the sDMA engine
 *
 * @ib: indirect buffer to fill with commands
 * @src_offset: src GPU address
 * @dst_offset: dst GPU address
 * @byte_count: number of bytes to xfer
 * @copy_flags: copy flags for the buffers
 *
 * Copy GPU buffers using the DMA engine.
 * Used by the amdgpu ttm implementation to move pages if
 * registered as the asic copy callback.
 */
static void sdma_v7_1_emit_copy_buffer(struct amdgpu_ib *ib,
				       uint64_t src_offset,
				       uint64_t dst_offset,
				       uint32_t byte_count,
				       uint32_t copy_flags)
{
	ib->ptr[ib->length_dw++] = SDMA_PKT_COPY_LINEAR_HEADER_OP(SDMA_OP_COPY) |
		SDMA_PKT_COPY_LINEAR_HEADER_SUB_OP(SDMA_SUBOP_COPY_LINEAR) |
		SDMA_PKT_COPY_LINEAR_HEADER_TMZ((copy_flags & AMDGPU_COPY_FLAGS_TMZ) ? 1 : 0);

	ib->ptr[ib->length_dw++] = byte_count - 1;
	ib->ptr[ib->length_dw++] = 0; /* src/dst endian swap */
	ib->ptr[ib->length_dw++] = lower_32_bits(src_offset);
	ib->ptr[ib->length_dw++] = upper_32_bits(src_offset);
	ib->ptr[ib->length_dw++] = lower_32_bits(dst_offset);
	ib->ptr[ib->length_dw++] = upper_32_bits(dst_offset);
}

/**
 * sdma_v7_1_emit_fill_buffer - fill buffer using the sDMA engine
 *
 * @ib: indirect buffer to fill
 * @src_data: value to write to buffer
 * @dst_offset: dst GPU address
 * @byte_count: number of bytes to xfer
 *
 * Fill GPU buffers using the DMA engine.
 */
static void sdma_v7_1_emit_fill_buffer(struct amdgpu_ib *ib,
				       uint32_t src_data,
				       uint64_t dst_offset,
				       uint32_t byte_count)
{
	ib->ptr[ib->length_dw++] = SDMA_PKT_CONSTANT_FILL_HEADER_OP(SDMA_OP_CONST_FILL);
	ib->ptr[ib->length_dw++] = lower_32_bits(dst_offset);
	ib->ptr[ib->length_dw++] = upper_32_bits(dst_offset);
	ib->ptr[ib->length_dw++] = src_data;
	ib->ptr[ib->length_dw++] = byte_count - 1;
}

static const struct amdgpu_buffer_funcs sdma_v7_1_buffer_funcs = {
	.copy_max_bytes = 0x400000,
	.copy_num_dw = 8,
	.emit_copy_buffer = sdma_v7_1_emit_copy_buffer,
	.fill_max_bytes = 0x400000,
	.fill_num_dw = 5,
	.emit_fill_buffer = sdma_v7_1_emit_fill_buffer,
};

static void sdma_v7_1_set_buffer_funcs(struct amdgpu_device *adev)
{
	adev->mman.buffer_funcs = &sdma_v7_1_buffer_funcs;
	adev->mman.buffer_funcs_ring = &adev->sdma.instance[0].ring;
}

static const struct amdgpu_vm_pte_funcs sdma_v7_1_vm_pte_funcs = {
	.copy_pte_num_dw = 8,
	.copy_pte = sdma_v7_1_vm_copy_pte,
	.write_pte = sdma_v7_1_vm_write_pte,
	.set_pte_pde = sdma_v7_1_vm_set_pte_pde,
};

static void sdma_v7_1_set_vm_pte_funcs(struct amdgpu_device *adev)
{
	unsigned i;

	adev->vm_manager.vm_pte_funcs = &sdma_v7_1_vm_pte_funcs;
	for (i = 0; i < adev->sdma.num_instances; i++) {
		adev->vm_manager.vm_pte_scheds[i] =
			&adev->sdma.instance[i].ring.sched;
	}
	adev->vm_manager.vm_pte_num_scheds = adev->sdma.num_instances;
}

const struct amdgpu_ip_block_version sdma_v7_1_ip_block = {
	.type = AMD_IP_BLOCK_TYPE_SDMA,
	.major = 7,
	.minor = 1,
	.rev = 0,
	.funcs = &sdma_v7_1_ip_funcs,
};

static int sdma_v7_1_xcp_resume(void *handle, uint32_t inst_mask)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int r;

	r = sdma_v7_1_inst_start(adev, inst_mask);

	return r;
}

static int sdma_v7_1_xcp_suspend(void *handle, uint32_t inst_mask)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	sdma_v7_1_inst_ctx_switch_enable(adev, false, inst_mask);
	sdma_v7_1_inst_enable(adev, false, inst_mask);

	return 0;
}

struct amdgpu_xcp_ip_funcs sdma_v7_1_xcp_funcs = {
	.suspend = &sdma_v7_1_xcp_suspend,
	.resume = &sdma_v7_1_xcp_resume
};
