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

#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "amdgpu.h"
#include "amdgpu_xcp.h"
#include "amdgpu_ucode.h"
#include "amdgpu_trace.h"
#include "amdgpu_reset.h"

#include "sdma/sdma_4_4_2_offset.h"
#include "sdma/sdma_4_4_2_sh_mask.h"

#include "soc15_common.h"
#include "soc15.h"
#include "vega10_sdma_pkt_open.h"

#include "ivsrcid/sdma0/irqsrcs_sdma0_4_0.h"
#include "ivsrcid/sdma1/irqsrcs_sdma1_4_0.h"

#include "amdgpu_ras.h"

MODULE_FIRMWARE("amdgpu/sdma_4_4_2.bin");
MODULE_FIRMWARE("amdgpu/sdma_4_4_5.bin");

static const struct amdgpu_hwip_reg_entry sdma_reg_list_4_4_2[] = {
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA_STATUS_REG),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA_STATUS1_REG),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA_STATUS2_REG),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA_STATUS3_REG),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA_UCODE_CHECKSUM),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA_RB_RPTR_FETCH_HI),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA_RB_RPTR_FETCH),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA_UTCL1_RD_STATUS),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA_UTCL1_WR_STATUS),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA_UTCL1_RD_XNACK0),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA_UTCL1_RD_XNACK1),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA_UTCL1_WR_XNACK0),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA_UTCL1_WR_XNACK1),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA_GFX_RB_CNTL),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA_GFX_RB_RPTR),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA_GFX_RB_RPTR_HI),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA_GFX_RB_WPTR),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA_GFX_RB_WPTR_HI),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA_GFX_IB_OFFSET),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA_GFX_IB_BASE_LO),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA_GFX_IB_BASE_HI),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA_GFX_IB_CNTL),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA_GFX_IB_RPTR),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA_GFX_IB_SUB_REMAIN),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA_GFX_DUMMY_REG),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA_PAGE_RB_CNTL),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA_PAGE_RB_RPTR),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA_PAGE_RB_RPTR_HI),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA_PAGE_RB_WPTR),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA_PAGE_RB_WPTR_HI),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA_PAGE_IB_OFFSET),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA_PAGE_IB_BASE_LO),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA_PAGE_IB_BASE_HI),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA_PAGE_DUMMY_REG),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA_RLC0_RB_CNTL),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA_RLC0_RB_RPTR),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA_RLC0_RB_RPTR_HI),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA_RLC0_RB_WPTR),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA_RLC0_RB_WPTR_HI),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA_RLC0_IB_OFFSET),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA_RLC0_IB_BASE_LO),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA_RLC0_IB_BASE_HI),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA_RLC0_DUMMY_REG),
	SOC15_REG_ENTRY_STR(GC, 0, regSDMA_VM_CNTL)
};

#define mmSMNAID_AID0_MCA_SMU 0x03b30400

#define WREG32_SDMA(instance, offset, value) \
	WREG32(sdma_v4_4_2_get_reg_offset(adev, (instance), (offset)), value)
#define RREG32_SDMA(instance, offset) \
	RREG32(sdma_v4_4_2_get_reg_offset(adev, (instance), (offset)))

static void sdma_v4_4_2_set_ring_funcs(struct amdgpu_device *adev);
static void sdma_v4_4_2_set_buffer_funcs(struct amdgpu_device *adev);
static void sdma_v4_4_2_set_vm_pte_funcs(struct amdgpu_device *adev);
static void sdma_v4_4_2_set_irq_funcs(struct amdgpu_device *adev);
static void sdma_v4_4_2_set_ras_funcs(struct amdgpu_device *adev);
static void sdma_v4_4_2_update_reset_mask(struct amdgpu_device *adev);
static int sdma_v4_4_2_stop_queue(struct amdgpu_ring *ring);
static int sdma_v4_4_2_restore_queue(struct amdgpu_ring *ring);

static u32 sdma_v4_4_2_get_reg_offset(struct amdgpu_device *adev,
		u32 instance, u32 offset)
{
	u32 dev_inst = GET_INST(SDMA0, instance);

	return (adev->reg_offset[SDMA0_HWIP][dev_inst][0] + offset);
}

static unsigned sdma_v4_4_2_seq_to_irq_id(int seq_num)
{
	switch (seq_num) {
	case 0:
		return SOC15_IH_CLIENTID_SDMA0;
	case 1:
		return SOC15_IH_CLIENTID_SDMA1;
	case 2:
		return SOC15_IH_CLIENTID_SDMA2;
	case 3:
		return SOC15_IH_CLIENTID_SDMA3;
	default:
		return -EINVAL;
	}
}

static int sdma_v4_4_2_irq_id_to_seq(struct amdgpu_device *adev, unsigned client_id)
{
	switch (client_id) {
	case SOC15_IH_CLIENTID_SDMA0:
		return 0;
	case SOC15_IH_CLIENTID_SDMA1:
		return 1;
	case SOC15_IH_CLIENTID_SDMA2:
		if (amdgpu_sriov_vf(adev) && (adev->gfx.xcc_mask == 0x1))
			return 0;
		else
			return 2;
	case SOC15_IH_CLIENTID_SDMA3:
		if (amdgpu_sriov_vf(adev) && (adev->gfx.xcc_mask == 0x1))
			return 1;
		else
			return 3;
	default:
		return -EINVAL;
	}
}

static void sdma_v4_4_2_inst_init_golden_registers(struct amdgpu_device *adev,
						   uint32_t inst_mask)
{
	u32 val;
	int i;

	for (i = 0; i < adev->sdma.num_instances; i++) {
		val = RREG32_SDMA(i, regSDMA_GB_ADDR_CONFIG);
		val = REG_SET_FIELD(val, SDMA_GB_ADDR_CONFIG, NUM_BANKS, 4);
		val = REG_SET_FIELD(val, SDMA_GB_ADDR_CONFIG,
				    PIPE_INTERLEAVE_SIZE, 0);
		WREG32_SDMA(i, regSDMA_GB_ADDR_CONFIG, val);

		val = RREG32_SDMA(i, regSDMA_GB_ADDR_CONFIG_READ);
		val = REG_SET_FIELD(val, SDMA_GB_ADDR_CONFIG_READ, NUM_BANKS,
				    4);
		val = REG_SET_FIELD(val, SDMA_GB_ADDR_CONFIG_READ,
				    PIPE_INTERLEAVE_SIZE, 0);
		WREG32_SDMA(i, regSDMA_GB_ADDR_CONFIG_READ, val);
	}
}

/**
 * sdma_v4_4_2_init_microcode - load ucode images from disk
 *
 * @adev: amdgpu_device pointer
 *
 * Use the firmware interface to load the ucode images into
 * the driver (not loaded into hw).
 * Returns 0 on success, error on failure.
 */
static int sdma_v4_4_2_init_microcode(struct amdgpu_device *adev)
{
	int ret, i;

	for (i = 0; i < adev->sdma.num_instances; i++) {
		if (amdgpu_ip_version(adev, SDMA0_HWIP, 0) == IP_VERSION(4, 4, 2) ||
		    amdgpu_ip_version(adev, SDMA0_HWIP, 0) == IP_VERSION(4, 4, 4) ||
		    amdgpu_ip_version(adev, SDMA0_HWIP, 0) == IP_VERSION(4, 4, 5)) {
			ret = amdgpu_sdma_init_microcode(adev, 0, true);
			break;
		} else {
			ret = amdgpu_sdma_init_microcode(adev, i, false);
			if (ret)
				return ret;
		}
	}

	return ret;
}

/**
 * sdma_v4_4_2_ring_get_rptr - get the current read pointer
 *
 * @ring: amdgpu ring pointer
 *
 * Get the current rptr from the hardware.
 */
static uint64_t sdma_v4_4_2_ring_get_rptr(struct amdgpu_ring *ring)
{
	u64 rptr;

	/* XXX check if swapping is necessary on BE */
	rptr = READ_ONCE(*((u64 *)&ring->adev->wb.wb[ring->rptr_offs]));

	DRM_DEBUG("rptr before shift == 0x%016llx\n", rptr);
	return rptr >> 2;
}

/**
 * sdma_v4_4_2_ring_get_wptr - get the current write pointer
 *
 * @ring: amdgpu ring pointer
 *
 * Get the current wptr from the hardware.
 */
static uint64_t sdma_v4_4_2_ring_get_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	u64 wptr;

	if (ring->use_doorbell) {
		/* XXX check if swapping is necessary on BE */
		wptr = READ_ONCE(*((u64 *)&adev->wb.wb[ring->wptr_offs]));
		DRM_DEBUG("wptr/doorbell before shift == 0x%016llx\n", wptr);
	} else {
		wptr = RREG32_SDMA(ring->me, regSDMA_GFX_RB_WPTR_HI);
		wptr = wptr << 32;
		wptr |= RREG32_SDMA(ring->me, regSDMA_GFX_RB_WPTR);
		DRM_DEBUG("wptr before shift [%i] wptr == 0x%016llx\n",
				ring->me, wptr);
	}

	return wptr >> 2;
}

/**
 * sdma_v4_4_2_ring_set_wptr - commit the write pointer
 *
 * @ring: amdgpu ring pointer
 *
 * Write the wptr back to the hardware.
 */
static void sdma_v4_4_2_ring_set_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	DRM_DEBUG("Setting write pointer\n");
	if (ring->use_doorbell) {
		u64 *wb = (u64 *)&adev->wb.wb[ring->wptr_offs];

		DRM_DEBUG("Using doorbell -- "
				"wptr_offs == 0x%08x "
				"lower_32_bits(ring->wptr) << 2 == 0x%08x "
				"upper_32_bits(ring->wptr) << 2 == 0x%08x\n",
				ring->wptr_offs,
				lower_32_bits(ring->wptr << 2),
				upper_32_bits(ring->wptr << 2));
		/* XXX check if swapping is necessary on BE */
		WRITE_ONCE(*wb, (ring->wptr << 2));
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
		WREG32_SDMA(ring->me, regSDMA_GFX_RB_WPTR,
			    lower_32_bits(ring->wptr << 2));
		WREG32_SDMA(ring->me, regSDMA_GFX_RB_WPTR_HI,
			    upper_32_bits(ring->wptr << 2));
	}
}

/**
 * sdma_v4_4_2_page_ring_get_wptr - get the current write pointer
 *
 * @ring: amdgpu ring pointer
 *
 * Get the current wptr from the hardware.
 */
static uint64_t sdma_v4_4_2_page_ring_get_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	u64 wptr;

	if (ring->use_doorbell) {
		/* XXX check if swapping is necessary on BE */
		wptr = READ_ONCE(*((u64 *)&adev->wb.wb[ring->wptr_offs]));
	} else {
		wptr = RREG32_SDMA(ring->me, regSDMA_PAGE_RB_WPTR_HI);
		wptr = wptr << 32;
		wptr |= RREG32_SDMA(ring->me, regSDMA_PAGE_RB_WPTR);
	}

	return wptr >> 2;
}

/**
 * sdma_v4_4_2_page_ring_set_wptr - commit the write pointer
 *
 * @ring: amdgpu ring pointer
 *
 * Write the wptr back to the hardware.
 */
static void sdma_v4_4_2_page_ring_set_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring->use_doorbell) {
		u64 *wb = (u64 *)&adev->wb.wb[ring->wptr_offs];

		/* XXX check if swapping is necessary on BE */
		WRITE_ONCE(*wb, (ring->wptr << 2));
		WDOORBELL64(ring->doorbell_index, ring->wptr << 2);
	} else {
		uint64_t wptr = ring->wptr << 2;

		WREG32_SDMA(ring->me, regSDMA_PAGE_RB_WPTR,
			    lower_32_bits(wptr));
		WREG32_SDMA(ring->me, regSDMA_PAGE_RB_WPTR_HI,
			    upper_32_bits(wptr));
	}
}

static void sdma_v4_4_2_ring_insert_nop(struct amdgpu_ring *ring, uint32_t count)
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
 * sdma_v4_4_2_ring_emit_ib - Schedule an IB on the DMA engine
 *
 * @ring: amdgpu ring pointer
 * @job: job to retrieve vmid from
 * @ib: IB object to schedule
 * @flags: unused
 *
 * Schedule an IB in the DMA ring.
 */
static void sdma_v4_4_2_ring_emit_ib(struct amdgpu_ring *ring,
				   struct amdgpu_job *job,
				   struct amdgpu_ib *ib,
				   uint32_t flags)
{
	unsigned vmid = AMDGPU_JOB_GET_VMID(job);

	/* IB packet must end on a 8 DW boundary */
	sdma_v4_4_2_ring_insert_nop(ring, (2 - lower_32_bits(ring->wptr)) & 7);

	amdgpu_ring_write(ring, SDMA_PKT_HEADER_OP(SDMA_OP_INDIRECT) |
			  SDMA_PKT_INDIRECT_HEADER_VMID(vmid & 0xf));
	/* base must be 32 byte aligned */
	amdgpu_ring_write(ring, lower_32_bits(ib->gpu_addr) & 0xffffffe0);
	amdgpu_ring_write(ring, upper_32_bits(ib->gpu_addr));
	amdgpu_ring_write(ring, ib->length_dw);
	amdgpu_ring_write(ring, 0);
	amdgpu_ring_write(ring, 0);

}

static void sdma_v4_4_2_wait_reg_mem(struct amdgpu_ring *ring,
				   int mem_space, int hdp,
				   uint32_t addr0, uint32_t addr1,
				   uint32_t ref, uint32_t mask,
				   uint32_t inv)
{
	amdgpu_ring_write(ring, SDMA_PKT_HEADER_OP(SDMA_OP_POLL_REGMEM) |
			  SDMA_PKT_POLL_REGMEM_HEADER_HDP_FLUSH(hdp) |
			  SDMA_PKT_POLL_REGMEM_HEADER_MEM_POLL(mem_space) |
			  SDMA_PKT_POLL_REGMEM_HEADER_FUNC(3)); /* == */
	if (mem_space) {
		/* memory */
		amdgpu_ring_write(ring, addr0);
		amdgpu_ring_write(ring, addr1);
	} else {
		/* registers */
		amdgpu_ring_write(ring, addr0 << 2);
		amdgpu_ring_write(ring, addr1 << 2);
	}
	amdgpu_ring_write(ring, ref); /* reference */
	amdgpu_ring_write(ring, mask); /* mask */
	amdgpu_ring_write(ring, SDMA_PKT_POLL_REGMEM_DW5_RETRY_COUNT(0xfff) |
			  SDMA_PKT_POLL_REGMEM_DW5_INTERVAL(inv)); /* retry count, poll interval */
}

/**
 * sdma_v4_4_2_ring_emit_hdp_flush - emit an hdp flush on the DMA ring
 *
 * @ring: amdgpu ring pointer
 *
 * Emit an hdp flush packet on the requested DMA ring.
 */
static void sdma_v4_4_2_ring_emit_hdp_flush(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	u32 ref_and_mask = 0;
	const struct nbio_hdp_flush_reg *nbio_hf_reg = adev->nbio.hdp_flush_reg;

	ref_and_mask = nbio_hf_reg->ref_and_mask_sdma0
		       << (ring->me % adev->sdma.num_inst_per_aid);

	sdma_v4_4_2_wait_reg_mem(ring, 0, 1,
			       adev->nbio.funcs->get_hdp_flush_done_offset(adev),
			       adev->nbio.funcs->get_hdp_flush_req_offset(adev),
			       ref_and_mask, ref_and_mask, 10);
}

/**
 * sdma_v4_4_2_ring_emit_fence - emit a fence on the DMA ring
 *
 * @ring: amdgpu ring pointer
 * @addr: address
 * @seq: sequence number
 * @flags: fence related flags
 *
 * Add a DMA fence packet to the ring to write
 * the fence seq number and DMA trap packet to generate
 * an interrupt if needed.
 */
static void sdma_v4_4_2_ring_emit_fence(struct amdgpu_ring *ring, u64 addr, u64 seq,
				      unsigned flags)
{
	bool write64bit = flags & AMDGPU_FENCE_FLAG_64BIT;
	/* write the fence */
	amdgpu_ring_write(ring, SDMA_PKT_HEADER_OP(SDMA_OP_FENCE));
	/* zero in first two bits */
	BUG_ON(addr & 0x3);
	amdgpu_ring_write(ring, lower_32_bits(addr));
	amdgpu_ring_write(ring, upper_32_bits(addr));
	amdgpu_ring_write(ring, lower_32_bits(seq));

	/* optionally write high bits as well */
	if (write64bit) {
		addr += 4;
		amdgpu_ring_write(ring, SDMA_PKT_HEADER_OP(SDMA_OP_FENCE));
		/* zero in first two bits */
		BUG_ON(addr & 0x3);
		amdgpu_ring_write(ring, lower_32_bits(addr));
		amdgpu_ring_write(ring, upper_32_bits(addr));
		amdgpu_ring_write(ring, upper_32_bits(seq));
	}

	/* generate an interrupt */
	amdgpu_ring_write(ring, SDMA_PKT_HEADER_OP(SDMA_OP_TRAP));
	amdgpu_ring_write(ring, SDMA_PKT_TRAP_INT_CONTEXT_INT_CONTEXT(0));
}


/**
 * sdma_v4_4_2_inst_gfx_stop - stop the gfx async dma engines
 *
 * @adev: amdgpu_device pointer
 * @inst_mask: mask of dma engine instances to be disabled
 *
 * Stop the gfx async dma ring buffers.
 */
static void sdma_v4_4_2_inst_gfx_stop(struct amdgpu_device *adev,
				      uint32_t inst_mask)
{
	struct amdgpu_ring *sdma[AMDGPU_MAX_SDMA_INSTANCES];
	u32 doorbell_offset, doorbell;
	u32 rb_cntl, ib_cntl, sdma_cntl;
	int i;

	for_each_inst(i, inst_mask) {
		sdma[i] = &adev->sdma.instance[i].ring;

		rb_cntl = RREG32_SDMA(i, regSDMA_GFX_RB_CNTL);
		rb_cntl = REG_SET_FIELD(rb_cntl, SDMA_GFX_RB_CNTL, RB_ENABLE, 0);
		WREG32_SDMA(i, regSDMA_GFX_RB_CNTL, rb_cntl);
		ib_cntl = RREG32_SDMA(i, regSDMA_GFX_IB_CNTL);
		ib_cntl = REG_SET_FIELD(ib_cntl, SDMA_GFX_IB_CNTL, IB_ENABLE, 0);
		WREG32_SDMA(i, regSDMA_GFX_IB_CNTL, ib_cntl);
		sdma_cntl = RREG32_SDMA(i, regSDMA_CNTL);
		sdma_cntl = REG_SET_FIELD(sdma_cntl, SDMA_CNTL, UTC_L1_ENABLE, 0);
		WREG32_SDMA(i, regSDMA_CNTL, sdma_cntl);

		if (sdma[i]->use_doorbell) {
			doorbell = RREG32_SDMA(i, regSDMA_GFX_DOORBELL);
			doorbell_offset = RREG32_SDMA(i, regSDMA_GFX_DOORBELL_OFFSET);

			doorbell = REG_SET_FIELD(doorbell, SDMA_GFX_DOORBELL, ENABLE, 0);
			doorbell_offset = REG_SET_FIELD(doorbell_offset,
					SDMA_GFX_DOORBELL_OFFSET,
					OFFSET, 0);
			WREG32_SDMA(i, regSDMA_GFX_DOORBELL, doorbell);
			WREG32_SDMA(i, regSDMA_GFX_DOORBELL_OFFSET, doorbell_offset);
		}
	}
}

/**
 * sdma_v4_4_2_inst_rlc_stop - stop the compute async dma engines
 *
 * @adev: amdgpu_device pointer
 * @inst_mask: mask of dma engine instances to be disabled
 *
 * Stop the compute async dma queues.
 */
static void sdma_v4_4_2_inst_rlc_stop(struct amdgpu_device *adev,
				      uint32_t inst_mask)
{
	/* XXX todo */
}

/**
 * sdma_v4_4_2_inst_page_stop - stop the page async dma engines
 *
 * @adev: amdgpu_device pointer
 * @inst_mask: mask of dma engine instances to be disabled
 *
 * Stop the page async dma ring buffers.
 */
static void sdma_v4_4_2_inst_page_stop(struct amdgpu_device *adev,
				       uint32_t inst_mask)
{
	u32 rb_cntl, ib_cntl;
	int i;

	for_each_inst(i, inst_mask) {
		rb_cntl = RREG32_SDMA(i, regSDMA_PAGE_RB_CNTL);
		rb_cntl = REG_SET_FIELD(rb_cntl, SDMA_PAGE_RB_CNTL,
					RB_ENABLE, 0);
		WREG32_SDMA(i, regSDMA_PAGE_RB_CNTL, rb_cntl);
		ib_cntl = RREG32_SDMA(i, regSDMA_PAGE_IB_CNTL);
		ib_cntl = REG_SET_FIELD(ib_cntl, SDMA_PAGE_IB_CNTL,
					IB_ENABLE, 0);
		WREG32_SDMA(i, regSDMA_PAGE_IB_CNTL, ib_cntl);
	}
}

/**
 * sdma_v4_4_2_inst_ctx_switch_enable - stop the async dma engines context switch
 *
 * @adev: amdgpu_device pointer
 * @enable: enable/disable the DMA MEs context switch.
 * @inst_mask: mask of dma engine instances to be enabled
 *
 * Halt or unhalt the async dma engines context switch.
 */
static void sdma_v4_4_2_inst_ctx_switch_enable(struct amdgpu_device *adev,
					       bool enable, uint32_t inst_mask)
{
	u32 f32_cntl, phase_quantum = 0;
	int i;

	if (amdgpu_sdma_phase_quantum) {
		unsigned value = amdgpu_sdma_phase_quantum;
		unsigned unit = 0;

		while (value > (SDMA_PHASE0_QUANTUM__VALUE_MASK >>
				SDMA_PHASE0_QUANTUM__VALUE__SHIFT)) {
			value = (value + 1) >> 1;
			unit++;
		}
		if (unit > (SDMA_PHASE0_QUANTUM__UNIT_MASK >>
			    SDMA_PHASE0_QUANTUM__UNIT__SHIFT)) {
			value = (SDMA_PHASE0_QUANTUM__VALUE_MASK >>
				 SDMA_PHASE0_QUANTUM__VALUE__SHIFT);
			unit = (SDMA_PHASE0_QUANTUM__UNIT_MASK >>
				SDMA_PHASE0_QUANTUM__UNIT__SHIFT);
			WARN_ONCE(1,
			"clamping sdma_phase_quantum to %uK clock cycles\n",
				  value << unit);
		}
		phase_quantum =
			value << SDMA_PHASE0_QUANTUM__VALUE__SHIFT |
			unit  << SDMA_PHASE0_QUANTUM__UNIT__SHIFT;
	}

	for_each_inst(i, inst_mask) {
		f32_cntl = RREG32_SDMA(i, regSDMA_CNTL);
		f32_cntl = REG_SET_FIELD(f32_cntl, SDMA_CNTL,
				AUTO_CTXSW_ENABLE, enable ? 1 : 0);
		if (enable && amdgpu_sdma_phase_quantum) {
			WREG32_SDMA(i, regSDMA_PHASE0_QUANTUM, phase_quantum);
			WREG32_SDMA(i, regSDMA_PHASE1_QUANTUM, phase_quantum);
			WREG32_SDMA(i, regSDMA_PHASE2_QUANTUM, phase_quantum);
		}
		WREG32_SDMA(i, regSDMA_CNTL, f32_cntl);

		/* Extend page fault timeout to avoid interrupt storm */
		WREG32_SDMA(i, regSDMA_UTCL1_TIMEOUT, 0x00800080);
	}
}

/**
 * sdma_v4_4_2_inst_enable - stop the async dma engines
 *
 * @adev: amdgpu_device pointer
 * @enable: enable/disable the DMA MEs.
 * @inst_mask: mask of dma engine instances to be enabled
 *
 * Halt or unhalt the async dma engines.
 */
static void sdma_v4_4_2_inst_enable(struct amdgpu_device *adev, bool enable,
				    uint32_t inst_mask)
{
	u32 f32_cntl;
	int i;

	if (!enable) {
		sdma_v4_4_2_inst_gfx_stop(adev, inst_mask);
		sdma_v4_4_2_inst_rlc_stop(adev, inst_mask);
		if (adev->sdma.has_page_queue)
			sdma_v4_4_2_inst_page_stop(adev, inst_mask);

		/* SDMA FW needs to respond to FREEZE requests during reset.
		 * Keep it running during reset */
		if (!amdgpu_sriov_vf(adev) && amdgpu_in_reset(adev))
			return;
	}

	if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP)
		return;

	for_each_inst(i, inst_mask) {
		f32_cntl = RREG32_SDMA(i, regSDMA_F32_CNTL);
		f32_cntl = REG_SET_FIELD(f32_cntl, SDMA_F32_CNTL, HALT, enable ? 0 : 1);
		WREG32_SDMA(i, regSDMA_F32_CNTL, f32_cntl);
	}
}

/*
 * sdma_v4_4_2_rb_cntl - get parameters for rb_cntl
 */
static uint32_t sdma_v4_4_2_rb_cntl(struct amdgpu_ring *ring, uint32_t rb_cntl)
{
	/* Set ring buffer size in dwords */
	uint32_t rb_bufsz = order_base_2(ring->ring_size / 4);

	barrier(); /* work around https://llvm.org/pr42576 */
	rb_cntl = REG_SET_FIELD(rb_cntl, SDMA_GFX_RB_CNTL, RB_SIZE, rb_bufsz);
#ifdef __BIG_ENDIAN
	rb_cntl = REG_SET_FIELD(rb_cntl, SDMA_GFX_RB_CNTL, RB_SWAP_ENABLE, 1);
	rb_cntl = REG_SET_FIELD(rb_cntl, SDMA_GFX_RB_CNTL,
				RPTR_WRITEBACK_SWAP_ENABLE, 1);
#endif
	return rb_cntl;
}

/**
 * sdma_v4_4_2_gfx_resume - setup and start the async dma engines
 *
 * @adev: amdgpu_device pointer
 * @i: instance to resume
 * @restore: used to restore wptr when restart
 *
 * Set up the gfx DMA ring buffers and enable them.
 * Returns 0 for success, error for failure.
 */
static void sdma_v4_4_2_gfx_resume(struct amdgpu_device *adev, unsigned int i, bool restore)
{
	struct amdgpu_ring *ring = &adev->sdma.instance[i].ring;
	u32 rb_cntl, ib_cntl, wptr_poll_cntl;
	u32 wb_offset;
	u32 doorbell;
	u32 doorbell_offset;
	u64 wptr_gpu_addr;
	u64 rwptr;

	wb_offset = (ring->rptr_offs * 4);

	rb_cntl = RREG32_SDMA(i, regSDMA_GFX_RB_CNTL);
	rb_cntl = sdma_v4_4_2_rb_cntl(ring, rb_cntl);
	WREG32_SDMA(i, regSDMA_GFX_RB_CNTL, rb_cntl);

	/* set the wb address whether it's enabled or not */
	WREG32_SDMA(i, regSDMA_GFX_RB_RPTR_ADDR_HI,
	       upper_32_bits(adev->wb.gpu_addr + wb_offset) & 0xFFFFFFFF);
	WREG32_SDMA(i, regSDMA_GFX_RB_RPTR_ADDR_LO,
	       lower_32_bits(adev->wb.gpu_addr + wb_offset) & 0xFFFFFFFC);

	rb_cntl = REG_SET_FIELD(rb_cntl, SDMA_GFX_RB_CNTL,
				RPTR_WRITEBACK_ENABLE, 1);

	WREG32_SDMA(i, regSDMA_GFX_RB_BASE, ring->gpu_addr >> 8);
	WREG32_SDMA(i, regSDMA_GFX_RB_BASE_HI, ring->gpu_addr >> 40);

	if (!restore)
		ring->wptr = 0;

	/* before programing wptr to a less value, need set minor_ptr_update first */
	WREG32_SDMA(i, regSDMA_GFX_MINOR_PTR_UPDATE, 1);

	/* For the guilty queue, set RPTR to the current wptr to skip bad commands,
	 * It is not a guilty queue, restore cache_rptr and continue execution.
	 */
	if (adev->sdma.instance[i].gfx_guilty)
		rwptr = ring->wptr;
	else
		rwptr = ring->cached_rptr;

	/* Initialize the ring buffer's read and write pointers */
	if (restore) {
		WREG32_SDMA(i, regSDMA_GFX_RB_RPTR, lower_32_bits(rwptr << 2));
		WREG32_SDMA(i, regSDMA_GFX_RB_RPTR_HI, upper_32_bits(rwptr << 2));
		WREG32_SDMA(i, regSDMA_GFX_RB_WPTR, lower_32_bits(rwptr << 2));
		WREG32_SDMA(i, regSDMA_GFX_RB_WPTR_HI, upper_32_bits(rwptr << 2));
	} else {
		WREG32_SDMA(i, regSDMA_GFX_RB_RPTR, 0);
		WREG32_SDMA(i, regSDMA_GFX_RB_RPTR_HI, 0);
		WREG32_SDMA(i, regSDMA_GFX_RB_WPTR, 0);
		WREG32_SDMA(i, regSDMA_GFX_RB_WPTR_HI, 0);
	}

	doorbell = RREG32_SDMA(i, regSDMA_GFX_DOORBELL);
	doorbell_offset = RREG32_SDMA(i, regSDMA_GFX_DOORBELL_OFFSET);

	doorbell = REG_SET_FIELD(doorbell, SDMA_GFX_DOORBELL, ENABLE,
				 ring->use_doorbell);
	doorbell_offset = REG_SET_FIELD(doorbell_offset,
					SDMA_GFX_DOORBELL_OFFSET,
					OFFSET, ring->doorbell_index);
	WREG32_SDMA(i, regSDMA_GFX_DOORBELL, doorbell);
	WREG32_SDMA(i, regSDMA_GFX_DOORBELL_OFFSET, doorbell_offset);

	sdma_v4_4_2_ring_set_wptr(ring);

	/* set minor_ptr_update to 0 after wptr programed */
	WREG32_SDMA(i, regSDMA_GFX_MINOR_PTR_UPDATE, 0);

	/* setup the wptr shadow polling */
	wptr_gpu_addr = adev->wb.gpu_addr + (ring->wptr_offs * 4);
	WREG32_SDMA(i, regSDMA_GFX_RB_WPTR_POLL_ADDR_LO,
		    lower_32_bits(wptr_gpu_addr));
	WREG32_SDMA(i, regSDMA_GFX_RB_WPTR_POLL_ADDR_HI,
		    upper_32_bits(wptr_gpu_addr));
	wptr_poll_cntl = RREG32_SDMA(i, regSDMA_GFX_RB_WPTR_POLL_CNTL);
	wptr_poll_cntl = REG_SET_FIELD(wptr_poll_cntl,
				       SDMA_GFX_RB_WPTR_POLL_CNTL,
				       F32_POLL_ENABLE, amdgpu_sriov_vf(adev)? 1 : 0);
	WREG32_SDMA(i, regSDMA_GFX_RB_WPTR_POLL_CNTL, wptr_poll_cntl);

	/* enable DMA RB */
	rb_cntl = REG_SET_FIELD(rb_cntl, SDMA_GFX_RB_CNTL, RB_ENABLE, 1);
	WREG32_SDMA(i, regSDMA_GFX_RB_CNTL, rb_cntl);

	ib_cntl = RREG32_SDMA(i, regSDMA_GFX_IB_CNTL);
	ib_cntl = REG_SET_FIELD(ib_cntl, SDMA_GFX_IB_CNTL, IB_ENABLE, 1);
#ifdef __BIG_ENDIAN
	ib_cntl = REG_SET_FIELD(ib_cntl, SDMA_GFX_IB_CNTL, IB_SWAP_ENABLE, 1);
#endif
	/* enable DMA IBs */
	WREG32_SDMA(i, regSDMA_GFX_IB_CNTL, ib_cntl);
}

/**
 * sdma_v4_4_2_page_resume - setup and start the async dma engines
 *
 * @adev: amdgpu_device pointer
 * @i: instance to resume
 * @restore: boolean to say restore needed or not
 *
 * Set up the page DMA ring buffers and enable them.
 * Returns 0 for success, error for failure.
 */
static void sdma_v4_4_2_page_resume(struct amdgpu_device *adev, unsigned int i, bool restore)
{
	struct amdgpu_ring *ring = &adev->sdma.instance[i].page;
	u32 rb_cntl, ib_cntl, wptr_poll_cntl;
	u32 wb_offset;
	u32 doorbell;
	u32 doorbell_offset;
	u64 wptr_gpu_addr;
	u64 rwptr;

	wb_offset = (ring->rptr_offs * 4);

	rb_cntl = RREG32_SDMA(i, regSDMA_PAGE_RB_CNTL);
	rb_cntl = sdma_v4_4_2_rb_cntl(ring, rb_cntl);
	WREG32_SDMA(i, regSDMA_PAGE_RB_CNTL, rb_cntl);

	/* For the guilty queue, set RPTR to the current wptr to skip bad commands,
	 * It is not a guilty queue, restore cache_rptr and continue execution.
	 */
	if (adev->sdma.instance[i].page_guilty)
		rwptr = ring->wptr;
	else
		rwptr = ring->cached_rptr;

	/* Initialize the ring buffer's read and write pointers */
	if (restore) {
		WREG32_SDMA(i, regSDMA_PAGE_RB_RPTR, lower_32_bits(rwptr << 2));
		WREG32_SDMA(i, regSDMA_PAGE_RB_RPTR_HI, upper_32_bits(rwptr << 2));
		WREG32_SDMA(i, regSDMA_PAGE_RB_WPTR, lower_32_bits(rwptr << 2));
		WREG32_SDMA(i, regSDMA_PAGE_RB_WPTR_HI, upper_32_bits(rwptr << 2));
	} else {
		WREG32_SDMA(i, regSDMA_PAGE_RB_RPTR, 0);
		WREG32_SDMA(i, regSDMA_PAGE_RB_RPTR_HI, 0);
		WREG32_SDMA(i, regSDMA_PAGE_RB_WPTR, 0);
		WREG32_SDMA(i, regSDMA_PAGE_RB_WPTR_HI, 0);
	}

	/* set the wb address whether it's enabled or not */
	WREG32_SDMA(i, regSDMA_PAGE_RB_RPTR_ADDR_HI,
	       upper_32_bits(adev->wb.gpu_addr + wb_offset) & 0xFFFFFFFF);
	WREG32_SDMA(i, regSDMA_PAGE_RB_RPTR_ADDR_LO,
	       lower_32_bits(adev->wb.gpu_addr + wb_offset) & 0xFFFFFFFC);

	rb_cntl = REG_SET_FIELD(rb_cntl, SDMA_PAGE_RB_CNTL,
				RPTR_WRITEBACK_ENABLE, 1);

	WREG32_SDMA(i, regSDMA_PAGE_RB_BASE, ring->gpu_addr >> 8);
	WREG32_SDMA(i, regSDMA_PAGE_RB_BASE_HI, ring->gpu_addr >> 40);

	if (!restore)
		ring->wptr = 0;

	/* before programing wptr to a less value, need set minor_ptr_update first */
	WREG32_SDMA(i, regSDMA_PAGE_MINOR_PTR_UPDATE, 1);

	doorbell = RREG32_SDMA(i, regSDMA_PAGE_DOORBELL);
	doorbell_offset = RREG32_SDMA(i, regSDMA_PAGE_DOORBELL_OFFSET);

	doorbell = REG_SET_FIELD(doorbell, SDMA_PAGE_DOORBELL, ENABLE,
				 ring->use_doorbell);
	doorbell_offset = REG_SET_FIELD(doorbell_offset,
					SDMA_PAGE_DOORBELL_OFFSET,
					OFFSET, ring->doorbell_index);
	WREG32_SDMA(i, regSDMA_PAGE_DOORBELL, doorbell);
	WREG32_SDMA(i, regSDMA_PAGE_DOORBELL_OFFSET, doorbell_offset);

	/* paging queue doorbell range is setup at sdma_v4_4_2_gfx_resume */
	sdma_v4_4_2_page_ring_set_wptr(ring);

	/* set minor_ptr_update to 0 after wptr programed */
	WREG32_SDMA(i, regSDMA_PAGE_MINOR_PTR_UPDATE, 0);

	/* setup the wptr shadow polling */
	wptr_gpu_addr = adev->wb.gpu_addr + (ring->wptr_offs * 4);
	WREG32_SDMA(i, regSDMA_PAGE_RB_WPTR_POLL_ADDR_LO,
		    lower_32_bits(wptr_gpu_addr));
	WREG32_SDMA(i, regSDMA_PAGE_RB_WPTR_POLL_ADDR_HI,
		    upper_32_bits(wptr_gpu_addr));
	wptr_poll_cntl = RREG32_SDMA(i, regSDMA_PAGE_RB_WPTR_POLL_CNTL);
	wptr_poll_cntl = REG_SET_FIELD(wptr_poll_cntl,
				       SDMA_PAGE_RB_WPTR_POLL_CNTL,
				       F32_POLL_ENABLE, amdgpu_sriov_vf(adev)? 1 : 0);
	WREG32_SDMA(i, regSDMA_PAGE_RB_WPTR_POLL_CNTL, wptr_poll_cntl);

	/* enable DMA RB */
	rb_cntl = REG_SET_FIELD(rb_cntl, SDMA_PAGE_RB_CNTL, RB_ENABLE, 1);
	WREG32_SDMA(i, regSDMA_PAGE_RB_CNTL, rb_cntl);

	ib_cntl = RREG32_SDMA(i, regSDMA_PAGE_IB_CNTL);
	ib_cntl = REG_SET_FIELD(ib_cntl, SDMA_PAGE_IB_CNTL, IB_ENABLE, 1);
#ifdef __BIG_ENDIAN
	ib_cntl = REG_SET_FIELD(ib_cntl, SDMA_PAGE_IB_CNTL, IB_SWAP_ENABLE, 1);
#endif
	/* enable DMA IBs */
	WREG32_SDMA(i, regSDMA_PAGE_IB_CNTL, ib_cntl);
}

static void sdma_v4_4_2_init_pg(struct amdgpu_device *adev)
{

}

/**
 * sdma_v4_4_2_inst_rlc_resume - setup and start the async dma engines
 *
 * @adev: amdgpu_device pointer
 * @inst_mask: mask of dma engine instances to be enabled
 *
 * Set up the compute DMA queues and enable them.
 * Returns 0 for success, error for failure.
 */
static int sdma_v4_4_2_inst_rlc_resume(struct amdgpu_device *adev,
				       uint32_t inst_mask)
{
	sdma_v4_4_2_init_pg(adev);

	return 0;
}

/**
 * sdma_v4_4_2_inst_load_microcode - load the sDMA ME ucode
 *
 * @adev: amdgpu_device pointer
 * @inst_mask: mask of dma engine instances to be enabled
 *
 * Loads the sDMA0/1 ucode.
 * Returns 0 for success, -EINVAL if the ucode is not available.
 */
static int sdma_v4_4_2_inst_load_microcode(struct amdgpu_device *adev,
					   uint32_t inst_mask)
{
	const struct sdma_firmware_header_v1_0 *hdr;
	const __le32 *fw_data;
	u32 fw_size;
	int i, j;

	/* halt the MEs */
	sdma_v4_4_2_inst_enable(adev, false, inst_mask);

	for_each_inst(i, inst_mask) {
		if (!adev->sdma.instance[i].fw)
			return -EINVAL;

		hdr = (const struct sdma_firmware_header_v1_0 *)adev->sdma.instance[i].fw->data;
		amdgpu_ucode_print_sdma_hdr(&hdr->header);
		fw_size = le32_to_cpu(hdr->header.ucode_size_bytes) / 4;

		fw_data = (const __le32 *)
			(adev->sdma.instance[i].fw->data +
				le32_to_cpu(hdr->header.ucode_array_offset_bytes));

		WREG32_SDMA(i, regSDMA_UCODE_ADDR, 0);

		for (j = 0; j < fw_size; j++)
			WREG32_SDMA(i, regSDMA_UCODE_DATA,
				    le32_to_cpup(fw_data++));

		WREG32_SDMA(i, regSDMA_UCODE_ADDR,
			    adev->sdma.instance[i].fw_version);
	}

	return 0;
}

/**
 * sdma_v4_4_2_inst_start - setup and start the async dma engines
 *
 * @adev: amdgpu_device pointer
 * @inst_mask: mask of dma engine instances to be enabled
 * @restore: boolean to say restore needed or not
 *
 * Set up the DMA engines and enable them.
 * Returns 0 for success, error for failure.
 */
static int sdma_v4_4_2_inst_start(struct amdgpu_device *adev,
				  uint32_t inst_mask, bool restore)
{
	struct amdgpu_ring *ring;
	uint32_t tmp_mask;
	int i, r = 0;

	if (amdgpu_sriov_vf(adev)) {
		sdma_v4_4_2_inst_ctx_switch_enable(adev, false, inst_mask);
		sdma_v4_4_2_inst_enable(adev, false, inst_mask);
	} else {
		/* bypass sdma microcode loading on Gopher */
		if (!restore && adev->firmware.load_type != AMDGPU_FW_LOAD_PSP &&
		    adev->sdma.instance[0].fw) {
			r = sdma_v4_4_2_inst_load_microcode(adev, inst_mask);
			if (r)
				return r;
		}

		/* unhalt the MEs */
		sdma_v4_4_2_inst_enable(adev, true, inst_mask);
		/* enable sdma ring preemption */
		sdma_v4_4_2_inst_ctx_switch_enable(adev, true, inst_mask);
	}

	/* start the gfx rings and rlc compute queues */
	tmp_mask = inst_mask;
	for_each_inst(i, tmp_mask) {
		uint32_t temp;

		WREG32_SDMA(i, regSDMA_SEM_WAIT_FAIL_TIMER_CNTL, 0);
		sdma_v4_4_2_gfx_resume(adev, i, restore);
		if (adev->sdma.has_page_queue)
			sdma_v4_4_2_page_resume(adev, i, restore);

		/* set utc l1 enable flag always to 1 */
		temp = RREG32_SDMA(i, regSDMA_CNTL);
		temp = REG_SET_FIELD(temp, SDMA_CNTL, UTC_L1_ENABLE, 1);
		WREG32_SDMA(i, regSDMA_CNTL, temp);

		if (amdgpu_ip_version(adev, SDMA0_HWIP, 0) < IP_VERSION(4, 4, 5)) {
			/* enable context empty interrupt during initialization */
			temp = REG_SET_FIELD(temp, SDMA_CNTL, CTXEMPTY_INT_ENABLE, 1);
			WREG32_SDMA(i, regSDMA_CNTL, temp);
		}
		if (!amdgpu_sriov_vf(adev)) {
			if (adev->firmware.load_type != AMDGPU_FW_LOAD_PSP) {
				/* unhalt engine */
				temp = RREG32_SDMA(i, regSDMA_F32_CNTL);
				temp = REG_SET_FIELD(temp, SDMA_F32_CNTL, HALT, 0);
				WREG32_SDMA(i, regSDMA_F32_CNTL, temp);
			}
		}
	}

	if (amdgpu_sriov_vf(adev)) {
		sdma_v4_4_2_inst_ctx_switch_enable(adev, true, inst_mask);
		sdma_v4_4_2_inst_enable(adev, true, inst_mask);
	} else {
		r = sdma_v4_4_2_inst_rlc_resume(adev, inst_mask);
		if (r)
			return r;
	}

	tmp_mask = inst_mask;
	for_each_inst(i, tmp_mask) {
		ring = &adev->sdma.instance[i].ring;

		r = amdgpu_ring_test_helper(ring);
		if (r)
			return r;

		if (adev->sdma.has_page_queue) {
			struct amdgpu_ring *page = &adev->sdma.instance[i].page;

			r = amdgpu_ring_test_helper(page);
			if (r)
				return r;
		}
	}

	return r;
}

/**
 * sdma_v4_4_2_ring_test_ring - simple async dma engine test
 *
 * @ring: amdgpu_ring structure holding ring information
 *
 * Test the DMA engine by writing using it to write an
 * value to memory.
 * Returns 0 for success, error for failure.
 */
static int sdma_v4_4_2_ring_test_ring(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	unsigned i;
	unsigned index;
	int r;
	u32 tmp;
	u64 gpu_addr;

	r = amdgpu_device_wb_get(adev, &index);
	if (r)
		return r;

	gpu_addr = adev->wb.gpu_addr + (index * 4);
	tmp = 0xCAFEDEAD;
	adev->wb.wb[index] = cpu_to_le32(tmp);

	r = amdgpu_ring_alloc(ring, 5);
	if (r)
		goto error_free_wb;

	amdgpu_ring_write(ring, SDMA_PKT_HEADER_OP(SDMA_OP_WRITE) |
			  SDMA_PKT_HEADER_SUB_OP(SDMA_SUBOP_WRITE_LINEAR));
	amdgpu_ring_write(ring, lower_32_bits(gpu_addr));
	amdgpu_ring_write(ring, upper_32_bits(gpu_addr));
	amdgpu_ring_write(ring, SDMA_PKT_WRITE_UNTILED_DW_3_COUNT(0));
	amdgpu_ring_write(ring, 0xDEADBEEF);
	amdgpu_ring_commit(ring);

	for (i = 0; i < adev->usec_timeout; i++) {
		tmp = le32_to_cpu(adev->wb.wb[index]);
		if (tmp == 0xDEADBEEF)
			break;
		udelay(1);
	}

	if (i >= adev->usec_timeout)
		r = -ETIMEDOUT;

error_free_wb:
	amdgpu_device_wb_free(adev, index);
	return r;
}

/**
 * sdma_v4_4_2_ring_test_ib - test an IB on the DMA engine
 *
 * @ring: amdgpu_ring structure holding ring information
 * @timeout: timeout value in jiffies, or MAX_SCHEDULE_TIMEOUT
 *
 * Test a simple IB in the DMA ring.
 * Returns 0 on success, error on failure.
 */
static int sdma_v4_4_2_ring_test_ib(struct amdgpu_ring *ring, long timeout)
{
	struct amdgpu_device *adev = ring->adev;
	struct amdgpu_ib ib;
	struct dma_fence *f = NULL;
	unsigned index;
	long r;
	u32 tmp = 0;
	u64 gpu_addr;

	r = amdgpu_device_wb_get(adev, &index);
	if (r)
		return r;

	gpu_addr = adev->wb.gpu_addr + (index * 4);
	tmp = 0xCAFEDEAD;
	adev->wb.wb[index] = cpu_to_le32(tmp);
	memset(&ib, 0, sizeof(ib));
	r = amdgpu_ib_get(adev, NULL, 256,
					AMDGPU_IB_POOL_DIRECT, &ib);
	if (r)
		goto err0;

	ib.ptr[0] = SDMA_PKT_HEADER_OP(SDMA_OP_WRITE) |
		SDMA_PKT_HEADER_SUB_OP(SDMA_SUBOP_WRITE_LINEAR);
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
		r = -ETIMEDOUT;
		goto err1;
	} else if (r < 0) {
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
 * sdma_v4_4_2_vm_copy_pte - update PTEs by copying them from the GART
 *
 * @ib: indirect buffer to fill with commands
 * @pe: addr of the page entry
 * @src: src addr to copy from
 * @count: number of page entries to update
 *
 * Update PTEs by copying them from the GART using sDMA.
 */
static void sdma_v4_4_2_vm_copy_pte(struct amdgpu_ib *ib,
				  uint64_t pe, uint64_t src,
				  unsigned count)
{
	unsigned bytes = count * 8;

	ib->ptr[ib->length_dw++] = SDMA_PKT_HEADER_OP(SDMA_OP_COPY) |
		SDMA_PKT_HEADER_SUB_OP(SDMA_SUBOP_COPY_LINEAR);
	ib->ptr[ib->length_dw++] = bytes - 1;
	ib->ptr[ib->length_dw++] = 0; /* src/dst endian swap */
	ib->ptr[ib->length_dw++] = lower_32_bits(src);
	ib->ptr[ib->length_dw++] = upper_32_bits(src);
	ib->ptr[ib->length_dw++] = lower_32_bits(pe);
	ib->ptr[ib->length_dw++] = upper_32_bits(pe);

}

/**
 * sdma_v4_4_2_vm_write_pte - update PTEs by writing them manually
 *
 * @ib: indirect buffer to fill with commands
 * @pe: addr of the page entry
 * @value: dst addr to write into pe
 * @count: number of page entries to update
 * @incr: increase next addr by incr bytes
 *
 * Update PTEs by writing them manually using sDMA.
 */
static void sdma_v4_4_2_vm_write_pte(struct amdgpu_ib *ib, uint64_t pe,
				   uint64_t value, unsigned count,
				   uint32_t incr)
{
	unsigned ndw = count * 2;

	ib->ptr[ib->length_dw++] = SDMA_PKT_HEADER_OP(SDMA_OP_WRITE) |
		SDMA_PKT_HEADER_SUB_OP(SDMA_SUBOP_WRITE_LINEAR);
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
 * sdma_v4_4_2_vm_set_pte_pde - update the page tables using sDMA
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
static void sdma_v4_4_2_vm_set_pte_pde(struct amdgpu_ib *ib,
				     uint64_t pe,
				     uint64_t addr, unsigned count,
				     uint32_t incr, uint64_t flags)
{
	/* for physically contiguous pages (vram) */
	ib->ptr[ib->length_dw++] = SDMA_PKT_HEADER_OP(SDMA_OP_PTEPDE);
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
 * sdma_v4_4_2_ring_pad_ib - pad the IB to the required number of dw
 *
 * @ring: amdgpu_ring structure holding ring information
 * @ib: indirect buffer to fill with padding
 */
static void sdma_v4_4_2_ring_pad_ib(struct amdgpu_ring *ring, struct amdgpu_ib *ib)
{
	struct amdgpu_sdma_instance *sdma = amdgpu_sdma_get_instance_from_ring(ring);
	u32 pad_count;
	int i;

	pad_count = (-ib->length_dw) & 7;
	for (i = 0; i < pad_count; i++)
		if (sdma && sdma->burst_nop && (i == 0))
			ib->ptr[ib->length_dw++] =
				SDMA_PKT_HEADER_OP(SDMA_OP_NOP) |
				SDMA_PKT_NOP_HEADER_COUNT(pad_count - 1);
		else
			ib->ptr[ib->length_dw++] =
				SDMA_PKT_HEADER_OP(SDMA_OP_NOP);
}


/**
 * sdma_v4_4_2_ring_emit_pipeline_sync - sync the pipeline
 *
 * @ring: amdgpu_ring pointer
 *
 * Make sure all previous operations are completed (CIK).
 */
static void sdma_v4_4_2_ring_emit_pipeline_sync(struct amdgpu_ring *ring)
{
	uint32_t seq = ring->fence_drv.sync_seq;
	uint64_t addr = ring->fence_drv.gpu_addr;

	/* wait for idle */
	sdma_v4_4_2_wait_reg_mem(ring, 1, 0,
			       addr & 0xfffffffc,
			       upper_32_bits(addr) & 0xffffffff,
			       seq, 0xffffffff, 4);
}


/**
 * sdma_v4_4_2_ring_emit_vm_flush - vm flush using sDMA
 *
 * @ring: amdgpu_ring pointer
 * @vmid: vmid number to use
 * @pd_addr: address
 *
 * Update the page table base and flush the VM TLB
 * using sDMA.
 */
static void sdma_v4_4_2_ring_emit_vm_flush(struct amdgpu_ring *ring,
					 unsigned vmid, uint64_t pd_addr)
{
	amdgpu_gmc_emit_flush_gpu_tlb(ring, vmid, pd_addr);
}

static void sdma_v4_4_2_ring_emit_wreg(struct amdgpu_ring *ring,
				     uint32_t reg, uint32_t val)
{
	amdgpu_ring_write(ring, SDMA_PKT_HEADER_OP(SDMA_OP_SRBM_WRITE) |
			  SDMA_PKT_SRBM_WRITE_HEADER_BYTE_EN(0xf));
	amdgpu_ring_write(ring, reg);
	amdgpu_ring_write(ring, val);
}

static void sdma_v4_4_2_ring_emit_reg_wait(struct amdgpu_ring *ring, uint32_t reg,
					 uint32_t val, uint32_t mask)
{
	sdma_v4_4_2_wait_reg_mem(ring, 0, 0, reg, 0, val, mask, 10);
}

static bool sdma_v4_4_2_fw_support_paging_queue(struct amdgpu_device *adev)
{
	switch (amdgpu_ip_version(adev, SDMA0_HWIP, 0)) {
	case IP_VERSION(4, 4, 2):
	case IP_VERSION(4, 4, 5):
		return false;
	default:
		return false;
	}
}

static const struct amdgpu_sdma_funcs sdma_v4_4_2_sdma_funcs = {
	.stop_kernel_queue = &sdma_v4_4_2_stop_queue,
	.start_kernel_queue = &sdma_v4_4_2_restore_queue,
};

static int sdma_v4_4_2_early_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	int r;

	r = sdma_v4_4_2_init_microcode(adev);
	if (r)
		return r;

	/* TODO: Page queue breaks driver reload under SRIOV */
	if (sdma_v4_4_2_fw_support_paging_queue(adev))
		adev->sdma.has_page_queue = true;

	sdma_v4_4_2_set_ring_funcs(adev);
	sdma_v4_4_2_set_buffer_funcs(adev);
	sdma_v4_4_2_set_vm_pte_funcs(adev);
	sdma_v4_4_2_set_irq_funcs(adev);
	sdma_v4_4_2_set_ras_funcs(adev);
	return 0;
}

#if 0
static int sdma_v4_4_2_process_ras_data_cb(struct amdgpu_device *adev,
		void *err_data,
		struct amdgpu_iv_entry *entry);
#endif

static int sdma_v4_4_2_late_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
#if 0
	struct ras_ih_if ih_info = {
		.cb = sdma_v4_4_2_process_ras_data_cb,
	};
#endif
	if (!amdgpu_persistent_edc_harvesting_supported(adev))
		amdgpu_ras_reset_error_count(adev, AMDGPU_RAS_BLOCK__SDMA);

	/* The initialization is done in the late_init stage to ensure that the SMU
	 * initialization and capability setup are completed before we check the SDMA
	 * reset capability
	 */
	sdma_v4_4_2_update_reset_mask(adev);

	return 0;
}

static int sdma_v4_4_2_sw_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_ring *ring;
	int r, i;
	struct amdgpu_device *adev = ip_block->adev;
	u32 aid_id;
	uint32_t reg_count = ARRAY_SIZE(sdma_reg_list_4_4_2);
	uint32_t *ptr;

	/* SDMA trap event */
	for (i = 0; i < adev->sdma.num_inst_per_aid; i++) {
		r = amdgpu_irq_add_id(adev, sdma_v4_4_2_seq_to_irq_id(i),
				      SDMA0_4_0__SRCID__SDMA_TRAP,
				      &adev->sdma.trap_irq);
		if (r)
			return r;
	}

	/* SDMA SRAM ECC event */
	for (i = 0; i < adev->sdma.num_inst_per_aid; i++) {
		r = amdgpu_irq_add_id(adev, sdma_v4_4_2_seq_to_irq_id(i),
				      SDMA0_4_0__SRCID__SDMA_SRAM_ECC,
				      &adev->sdma.ecc_irq);
		if (r)
			return r;
	}

	/* SDMA VM_HOLE/DOORBELL_INV/POLL_TIMEOUT/SRBM_WRITE_PROTECTION event*/
	for (i = 0; i < adev->sdma.num_inst_per_aid; i++) {
		r = amdgpu_irq_add_id(adev, sdma_v4_4_2_seq_to_irq_id(i),
				      SDMA0_4_0__SRCID__SDMA_VM_HOLE,
				      &adev->sdma.vm_hole_irq);
		if (r)
			return r;

		r = amdgpu_irq_add_id(adev, sdma_v4_4_2_seq_to_irq_id(i),
				      SDMA0_4_0__SRCID__SDMA_DOORBELL_INVALID,
				      &adev->sdma.doorbell_invalid_irq);
		if (r)
			return r;

		r = amdgpu_irq_add_id(adev, sdma_v4_4_2_seq_to_irq_id(i),
				      SDMA0_4_0__SRCID__SDMA_POLL_TIMEOUT,
				      &adev->sdma.pool_timeout_irq);
		if (r)
			return r;

		r = amdgpu_irq_add_id(adev, sdma_v4_4_2_seq_to_irq_id(i),
				      SDMA0_4_0__SRCID__SDMA_SRBMWRITE,
				      &adev->sdma.srbm_write_irq);
		if (r)
			return r;

		r = amdgpu_irq_add_id(adev, sdma_v4_4_2_seq_to_irq_id(i),
				      SDMA0_4_0__SRCID__SDMA_CTXEMPTY,
				      &adev->sdma.ctxt_empty_irq);
		if (r)
			return r;
	}

	for (i = 0; i < adev->sdma.num_instances; i++) {
		mutex_init(&adev->sdma.instance[i].engine_reset_mutex);
		/* Initialize guilty flags for GFX and PAGE queues */
		adev->sdma.instance[i].gfx_guilty = false;
		adev->sdma.instance[i].page_guilty = false;
		adev->sdma.instance[i].funcs = &sdma_v4_4_2_sdma_funcs;

		ring = &adev->sdma.instance[i].ring;
		ring->ring_obj = NULL;
		ring->use_doorbell = true;
		aid_id = adev->sdma.instance[i].aid_id;

		DRM_DEBUG("SDMA %d use_doorbell being set to: [%s]\n", i,
				ring->use_doorbell?"true":"false");

		/* doorbell size is 2 dwords, get DWORD offset */
		ring->doorbell_index = adev->doorbell_index.sdma_engine[i] << 1;
		ring->vm_hub = AMDGPU_MMHUB0(aid_id);

		sprintf(ring->name, "sdma%d.%d", aid_id,
				i % adev->sdma.num_inst_per_aid);
		r = amdgpu_ring_init(adev, ring, 1024, &adev->sdma.trap_irq,
				     AMDGPU_SDMA_IRQ_INSTANCE0 + i,
				     AMDGPU_RING_PRIO_DEFAULT, NULL);
		if (r)
			return r;

		if (adev->sdma.has_page_queue) {
			ring = &adev->sdma.instance[i].page;
			ring->ring_obj = NULL;
			ring->use_doorbell = true;

			/* doorbell index of page queue is assigned right after
			 * gfx queue on the same instance
			 */
			ring->doorbell_index =
				(adev->doorbell_index.sdma_engine[i] + 1) << 1;
			ring->vm_hub = AMDGPU_MMHUB0(aid_id);

			sprintf(ring->name, "page%d.%d", aid_id,
					i % adev->sdma.num_inst_per_aid);
			r = amdgpu_ring_init(adev, ring, 1024,
					     &adev->sdma.trap_irq,
					     AMDGPU_SDMA_IRQ_INSTANCE0 + i,
					     AMDGPU_RING_PRIO_DEFAULT, NULL);
			if (r)
				return r;
		}
	}

	adev->sdma.supported_reset =
		amdgpu_get_soft_full_reset_mask(&adev->sdma.instance[0].ring);

	if (amdgpu_sdma_ras_sw_init(adev)) {
		dev_err(adev->dev, "fail to initialize sdma ras block\n");
		return -EINVAL;
	}

	/* Allocate memory for SDMA IP Dump buffer */
	ptr = kcalloc(adev->sdma.num_instances * reg_count, sizeof(uint32_t), GFP_KERNEL);
	if (ptr)
		adev->sdma.ip_dump = ptr;
	else
		DRM_ERROR("Failed to allocated memory for SDMA IP Dump\n");

	r = amdgpu_sdma_sysfs_reset_mask_init(adev);
	if (r)
		return r;

	return r;
}

static int sdma_v4_4_2_sw_fini(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	int i;

	for (i = 0; i < adev->sdma.num_instances; i++) {
		amdgpu_ring_fini(&adev->sdma.instance[i].ring);
		if (adev->sdma.has_page_queue)
			amdgpu_ring_fini(&adev->sdma.instance[i].page);
	}

	amdgpu_sdma_sysfs_reset_mask_fini(adev);
	if (amdgpu_ip_version(adev, SDMA0_HWIP, 0) == IP_VERSION(4, 4, 2) ||
	    amdgpu_ip_version(adev, SDMA0_HWIP, 0) == IP_VERSION(4, 4, 4) ||
	    amdgpu_ip_version(adev, SDMA0_HWIP, 0) == IP_VERSION(4, 4, 5))
		amdgpu_sdma_destroy_inst_ctx(adev, true);
	else
		amdgpu_sdma_destroy_inst_ctx(adev, false);

	kfree(adev->sdma.ip_dump);

	return 0;
}

static int sdma_v4_4_2_hw_init(struct amdgpu_ip_block *ip_block)
{
	int r;
	struct amdgpu_device *adev = ip_block->adev;
	uint32_t inst_mask;

	inst_mask = GENMASK(adev->sdma.num_instances - 1, 0);
	if (!amdgpu_sriov_vf(adev))
		sdma_v4_4_2_inst_init_golden_registers(adev, inst_mask);

	r = sdma_v4_4_2_inst_start(adev, inst_mask, false);

	return r;
}

static int sdma_v4_4_2_hw_fini(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	uint32_t inst_mask;
	int i;

	if (amdgpu_sriov_vf(adev))
		return 0;

	inst_mask = GENMASK(adev->sdma.num_instances - 1, 0);
	if (amdgpu_ras_is_supported(adev, AMDGPU_RAS_BLOCK__SDMA)) {
		for (i = 0; i < adev->sdma.num_instances; i++) {
			amdgpu_irq_put(adev, &adev->sdma.ecc_irq,
				       AMDGPU_SDMA_IRQ_INSTANCE0 + i);
		}
	}

	sdma_v4_4_2_inst_ctx_switch_enable(adev, false, inst_mask);
	sdma_v4_4_2_inst_enable(adev, false, inst_mask);

	return 0;
}

static int sdma_v4_4_2_set_clockgating_state(struct amdgpu_ip_block *ip_block,
					     enum amd_clockgating_state state);

static int sdma_v4_4_2_suspend(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;

	if (amdgpu_in_reset(adev))
		sdma_v4_4_2_set_clockgating_state(ip_block, AMD_CG_STATE_UNGATE);

	return sdma_v4_4_2_hw_fini(ip_block);
}

static int sdma_v4_4_2_resume(struct amdgpu_ip_block *ip_block)
{
	return sdma_v4_4_2_hw_init(ip_block);
}

static bool sdma_v4_4_2_is_idle(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	u32 i;

	for (i = 0; i < adev->sdma.num_instances; i++) {
		u32 tmp = RREG32_SDMA(i, regSDMA_STATUS_REG);

		if (!(tmp & SDMA_STATUS_REG__IDLE_MASK))
			return false;
	}

	return true;
}

static int sdma_v4_4_2_wait_for_idle(struct amdgpu_ip_block *ip_block)
{
	unsigned i, j;
	u32 sdma[AMDGPU_MAX_SDMA_INSTANCES];
	struct amdgpu_device *adev = ip_block->adev;

	for (i = 0; i < adev->usec_timeout; i++) {
		for (j = 0; j < adev->sdma.num_instances; j++) {
			sdma[j] = RREG32_SDMA(j, regSDMA_STATUS_REG);
			if (!(sdma[j] & SDMA_STATUS_REG__IDLE_MASK))
				break;
		}
		if (j == adev->sdma.num_instances)
			return 0;
		udelay(1);
	}
	return -ETIMEDOUT;
}

static int sdma_v4_4_2_soft_reset(struct amdgpu_ip_block *ip_block)
{
	/* todo */

	return 0;
}

static bool sdma_v4_4_2_is_queue_selected(struct amdgpu_device *adev, uint32_t instance_id, bool is_page_queue)
{
	uint32_t reg_offset = is_page_queue ? regSDMA_PAGE_CONTEXT_STATUS : regSDMA_GFX_CONTEXT_STATUS;
	uint32_t context_status = RREG32(sdma_v4_4_2_get_reg_offset(adev, instance_id, reg_offset));

	/* Check if the SELECTED bit is set */
	return (context_status & SDMA_GFX_CONTEXT_STATUS__SELECTED_MASK) != 0;
}

static bool sdma_v4_4_2_ring_is_guilty(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	uint32_t instance_id = ring->me;

	return sdma_v4_4_2_is_queue_selected(adev, instance_id, false);
}

static bool sdma_v4_4_2_page_ring_is_guilty(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	uint32_t instance_id = ring->me;

	if (!adev->sdma.has_page_queue)
		return false;

	return sdma_v4_4_2_is_queue_selected(adev, instance_id, true);
}

static int sdma_v4_4_2_reset_queue(struct amdgpu_ring *ring, unsigned int vmid)
{
	struct amdgpu_device *adev = ring->adev;
	u32 id = ring->me;
	int r;

	if (!(adev->sdma.supported_reset & AMDGPU_RESET_TYPE_PER_QUEUE))
		return -EOPNOTSUPP;

	amdgpu_amdkfd_suspend(adev, false);
	r = amdgpu_sdma_reset_engine(adev, id);
	amdgpu_amdkfd_resume(adev, false);

	return r;
}

static int sdma_v4_4_2_stop_queue(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	u32 instance_id = ring->me;
	u32 inst_mask;
	uint64_t rptr;

	if (amdgpu_sriov_vf(adev))
		return -EINVAL;

	/* Check if this queue is the guilty one */
	adev->sdma.instance[instance_id].gfx_guilty =
		sdma_v4_4_2_is_queue_selected(adev, instance_id, false);
	if (adev->sdma.has_page_queue)
		adev->sdma.instance[instance_id].page_guilty =
			sdma_v4_4_2_is_queue_selected(adev, instance_id, true);

	/* Cache the rptr before reset, after the reset,
	* all of the registers will be reset to 0
	*/
	rptr = amdgpu_ring_get_rptr(ring);
	ring->cached_rptr = rptr;
	/* Cache the rptr for the page queue if it exists */
	if (adev->sdma.has_page_queue) {
		struct amdgpu_ring *page_ring = &adev->sdma.instance[instance_id].page;
		rptr = amdgpu_ring_get_rptr(page_ring);
		page_ring->cached_rptr = rptr;
	}

	/* stop queue */
	inst_mask = 1 << ring->me;
	sdma_v4_4_2_inst_gfx_stop(adev, inst_mask);
	if (adev->sdma.has_page_queue)
		sdma_v4_4_2_inst_page_stop(adev, inst_mask);

	return 0;
}

static int sdma_v4_4_2_restore_queue(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	u32 inst_mask;
	int i;

	inst_mask = 1 << ring->me;
	udelay(50);

	for (i = 0; i < adev->usec_timeout; i++) {
		if (!REG_GET_FIELD(RREG32_SDMA(ring->me, regSDMA_F32_CNTL), SDMA_F32_CNTL, HALT))
			break;
		udelay(1);
	}

	if (i == adev->usec_timeout) {
		dev_err(adev->dev, "timed out waiting for SDMA%d unhalt after reset\n",
			ring->me);
		return -ETIMEDOUT;
	}

	return sdma_v4_4_2_inst_start(adev, inst_mask, true);
}

static int sdma_v4_4_2_set_trap_irq_state(struct amdgpu_device *adev,
					struct amdgpu_irq_src *source,
					unsigned type,
					enum amdgpu_interrupt_state state)
{
	u32 sdma_cntl;

	sdma_cntl = RREG32_SDMA(type, regSDMA_CNTL);
	sdma_cntl = REG_SET_FIELD(sdma_cntl, SDMA_CNTL, TRAP_ENABLE,
		       state == AMDGPU_IRQ_STATE_ENABLE ? 1 : 0);
	WREG32_SDMA(type, regSDMA_CNTL, sdma_cntl);

	return 0;
}

static int sdma_v4_4_2_process_trap_irq(struct amdgpu_device *adev,
				      struct amdgpu_irq_src *source,
				      struct amdgpu_iv_entry *entry)
{
	uint32_t instance, i;

	DRM_DEBUG("IH: SDMA trap\n");
	instance = sdma_v4_4_2_irq_id_to_seq(adev, entry->client_id);

	/* Client id gives the SDMA instance in AID. To know the exact SDMA
	 * instance, interrupt entry gives the node id which corresponds to the AID instance.
	 * Match node id with the AID id associated with the SDMA instance. */
	for (i = instance; i < adev->sdma.num_instances;
	     i += adev->sdma.num_inst_per_aid) {
		if (adev->sdma.instance[i].aid_id ==
		    node_id_to_phys_map[entry->node_id])
			break;
	}

	if (i >= adev->sdma.num_instances) {
		dev_WARN_ONCE(
			adev->dev, 1,
			"Couldn't find the right sdma instance in trap handler");
		return 0;
	}

	switch (entry->ring_id) {
	case 0:
		amdgpu_fence_process(&adev->sdma.instance[i].ring);
		break;
	case 1:
		amdgpu_fence_process(&adev->sdma.instance[i].page);
		break;
	default:
		break;
	}
	return 0;
}

#if 0
static int sdma_v4_4_2_process_ras_data_cb(struct amdgpu_device *adev,
		void *err_data,
		struct amdgpu_iv_entry *entry)
{
	int instance;

	/* When “Full RAS” is enabled, the per-IP interrupt sources should
	 * be disabled and the driver should only look for the aggregated
	 * interrupt via sync flood
	 */
	if (amdgpu_ras_is_supported(adev, AMDGPU_RAS_BLOCK__SDMA))
		goto out;

	instance = sdma_v4_4_2_irq_id_to_seq(adev, entry->client_id);
	if (instance < 0)
		goto out;

	amdgpu_sdma_process_ras_data_cb(adev, err_data, entry);

out:
	return AMDGPU_RAS_SUCCESS;
}
#endif

static int sdma_v4_4_2_process_illegal_inst_irq(struct amdgpu_device *adev,
					      struct amdgpu_irq_src *source,
					      struct amdgpu_iv_entry *entry)
{
	int instance;

	DRM_ERROR("Illegal instruction in SDMA command stream\n");

	instance = sdma_v4_4_2_irq_id_to_seq(adev, entry->client_id);
	if (instance < 0)
		return 0;

	switch (entry->ring_id) {
	case 0:
		drm_sched_fault(&adev->sdma.instance[instance].ring.sched);
		break;
	}
	return 0;
}

static int sdma_v4_4_2_set_ecc_irq_state(struct amdgpu_device *adev,
					struct amdgpu_irq_src *source,
					unsigned type,
					enum amdgpu_interrupt_state state)
{
	u32 sdma_cntl;

	sdma_cntl = RREG32_SDMA(type, regSDMA_CNTL);
	sdma_cntl = REG_SET_FIELD(sdma_cntl, SDMA_CNTL, DRAM_ECC_INT_ENABLE,
					state == AMDGPU_IRQ_STATE_ENABLE ? 1 : 0);
	WREG32_SDMA(type, regSDMA_CNTL, sdma_cntl);

	return 0;
}

static int sdma_v4_4_2_print_iv_entry(struct amdgpu_device *adev,
					      struct amdgpu_iv_entry *entry)
{
	int instance;
	struct amdgpu_task_info *task_info;
	u64 addr;

	instance = sdma_v4_4_2_irq_id_to_seq(adev, entry->client_id);
	if (instance < 0 || instance >= adev->sdma.num_instances) {
		dev_err(adev->dev, "sdma instance invalid %d\n", instance);
		return -EINVAL;
	}

	addr = (u64)entry->src_data[0] << 12;
	addr |= ((u64)entry->src_data[1] & 0xf) << 44;

	dev_dbg_ratelimited(adev->dev,
			    "[sdma%d] address:0x%016llx src_id:%u ring:%u vmid:%u pasid:%u\n",
			    instance, addr, entry->src_id, entry->ring_id, entry->vmid,
			    entry->pasid);

	task_info = amdgpu_vm_get_task_info_pasid(adev, entry->pasid);
	if (task_info) {
		dev_dbg_ratelimited(adev->dev, " for process %s pid %d thread %s pid %d\n",
				    task_info->process_name, task_info->tgid,
				    task_info->task_name, task_info->pid);
		amdgpu_vm_put_task_info(task_info);
	}

	return 0;
}

static int sdma_v4_4_2_process_vm_hole_irq(struct amdgpu_device *adev,
					      struct amdgpu_irq_src *source,
					      struct amdgpu_iv_entry *entry)
{
	dev_dbg_ratelimited(adev->dev, "MC or SEM address in VM hole\n");
	sdma_v4_4_2_print_iv_entry(adev, entry);
	return 0;
}

static int sdma_v4_4_2_process_doorbell_invalid_irq(struct amdgpu_device *adev,
					      struct amdgpu_irq_src *source,
					      struct amdgpu_iv_entry *entry)
{

	dev_dbg_ratelimited(adev->dev, "SDMA received a doorbell from BIF with byte_enable !=0xff\n");
	sdma_v4_4_2_print_iv_entry(adev, entry);
	return 0;
}

static int sdma_v4_4_2_process_pool_timeout_irq(struct amdgpu_device *adev,
					      struct amdgpu_irq_src *source,
					      struct amdgpu_iv_entry *entry)
{
	dev_dbg_ratelimited(adev->dev,
		"Polling register/memory timeout executing POLL_REG/MEM with finite timer\n");
	sdma_v4_4_2_print_iv_entry(adev, entry);
	return 0;
}

static int sdma_v4_4_2_process_srbm_write_irq(struct amdgpu_device *adev,
					      struct amdgpu_irq_src *source,
					      struct amdgpu_iv_entry *entry)
{
	dev_dbg_ratelimited(adev->dev,
		"SDMA gets an Register Write SRBM_WRITE command in non-privilege command buffer\n");
	sdma_v4_4_2_print_iv_entry(adev, entry);
	return 0;
}

static int sdma_v4_4_2_process_ctxt_empty_irq(struct amdgpu_device *adev,
					      struct amdgpu_irq_src *source,
					      struct amdgpu_iv_entry *entry)
{
	/* There is nothing useful to be done here, only kept for debug */
	dev_dbg_ratelimited(adev->dev, "SDMA context empty interrupt");
	sdma_v4_4_2_print_iv_entry(adev, entry);
	return 0;
}

static void sdma_v4_4_2_inst_update_medium_grain_light_sleep(
	struct amdgpu_device *adev, bool enable, uint32_t inst_mask)
{
	uint32_t data, def;
	int i;

	/* leave as default if it is not driver controlled */
	if (!(adev->cg_flags & AMD_CG_SUPPORT_SDMA_LS))
		return;

	if (enable) {
		for_each_inst(i, inst_mask) {
			/* 1-not override: enable sdma mem light sleep */
			def = data = RREG32_SDMA(i, regSDMA_POWER_CNTL);
			data |= SDMA_POWER_CNTL__MEM_POWER_OVERRIDE_MASK;
			if (def != data)
				WREG32_SDMA(i, regSDMA_POWER_CNTL, data);
		}
	} else {
		for_each_inst(i, inst_mask) {
			/* 0-override:disable sdma mem light sleep */
			def = data = RREG32_SDMA(i, regSDMA_POWER_CNTL);
			data &= ~SDMA_POWER_CNTL__MEM_POWER_OVERRIDE_MASK;
			if (def != data)
				WREG32_SDMA(i, regSDMA_POWER_CNTL, data);
		}
	}
}

static void sdma_v4_4_2_inst_update_medium_grain_clock_gating(
	struct amdgpu_device *adev, bool enable, uint32_t inst_mask)
{
	uint32_t data, def;
	int i;

	/* leave as default if it is not driver controlled */
	if (!(adev->cg_flags & AMD_CG_SUPPORT_SDMA_MGCG))
		return;

	if (enable) {
		for_each_inst(i, inst_mask) {
			def = data = RREG32_SDMA(i, regSDMA_CLK_CTRL);
			data &= ~(SDMA_CLK_CTRL__SOFT_OVERRIDE5_MASK |
				  SDMA_CLK_CTRL__SOFT_OVERRIDE4_MASK |
				  SDMA_CLK_CTRL__SOFT_OVERRIDE3_MASK |
				  SDMA_CLK_CTRL__SOFT_OVERRIDE2_MASK |
				  SDMA_CLK_CTRL__SOFT_OVERRIDE1_MASK |
				  SDMA_CLK_CTRL__SOFT_OVERRIDE0_MASK);
			if (def != data)
				WREG32_SDMA(i, regSDMA_CLK_CTRL, data);
		}
	} else {
		for_each_inst(i, inst_mask) {
			def = data = RREG32_SDMA(i, regSDMA_CLK_CTRL);
			data |= (SDMA_CLK_CTRL__SOFT_OVERRIDE5_MASK |
				 SDMA_CLK_CTRL__SOFT_OVERRIDE4_MASK |
				 SDMA_CLK_CTRL__SOFT_OVERRIDE3_MASK |
				 SDMA_CLK_CTRL__SOFT_OVERRIDE2_MASK |
				 SDMA_CLK_CTRL__SOFT_OVERRIDE1_MASK |
				 SDMA_CLK_CTRL__SOFT_OVERRIDE0_MASK);
			if (def != data)
				WREG32_SDMA(i, regSDMA_CLK_CTRL, data);
		}
	}
}

static int sdma_v4_4_2_set_clockgating_state(struct amdgpu_ip_block *ip_block,
					  enum amd_clockgating_state state)
{
	struct amdgpu_device *adev = ip_block->adev;
	uint32_t inst_mask;

	if (amdgpu_sriov_vf(adev))
		return 0;

	inst_mask = GENMASK(adev->sdma.num_instances - 1, 0);

	sdma_v4_4_2_inst_update_medium_grain_clock_gating(
		adev, state == AMD_CG_STATE_GATE, inst_mask);
	sdma_v4_4_2_inst_update_medium_grain_light_sleep(
		adev, state == AMD_CG_STATE_GATE, inst_mask);
	return 0;
}

static int sdma_v4_4_2_set_powergating_state(struct amdgpu_ip_block *ip_block,
					  enum amd_powergating_state state)
{
	return 0;
}

static void sdma_v4_4_2_get_clockgating_state(struct amdgpu_ip_block *ip_block, u64 *flags)
{
	struct amdgpu_device *adev = ip_block->adev;
	int data;

	if (amdgpu_sriov_vf(adev))
		*flags = 0;

	/* AMD_CG_SUPPORT_SDMA_MGCG */
	data = RREG32(SOC15_REG_OFFSET(SDMA0, GET_INST(SDMA0, 0), regSDMA_CLK_CTRL));
	if (!(data & SDMA_CLK_CTRL__SOFT_OVERRIDE5_MASK))
		*flags |= AMD_CG_SUPPORT_SDMA_MGCG;

	/* AMD_CG_SUPPORT_SDMA_LS */
	data = RREG32(SOC15_REG_OFFSET(SDMA0, GET_INST(SDMA0, 0), regSDMA_POWER_CNTL));
	if (data & SDMA_POWER_CNTL__MEM_POWER_OVERRIDE_MASK)
		*flags |= AMD_CG_SUPPORT_SDMA_LS;
}

static void sdma_v4_4_2_print_ip_state(struct amdgpu_ip_block *ip_block, struct drm_printer *p)
{
	struct amdgpu_device *adev = ip_block->adev;
	int i, j;
	uint32_t reg_count = ARRAY_SIZE(sdma_reg_list_4_4_2);
	uint32_t instance_offset;

	if (!adev->sdma.ip_dump)
		return;

	drm_printf(p, "num_instances:%d\n", adev->sdma.num_instances);
	for (i = 0; i < adev->sdma.num_instances; i++) {
		instance_offset = i * reg_count;
		drm_printf(p, "\nInstance:%d\n", i);

		for (j = 0; j < reg_count; j++)
			drm_printf(p, "%-50s \t 0x%08x\n", sdma_reg_list_4_4_2[j].reg_name,
				   adev->sdma.ip_dump[instance_offset + j]);
	}
}

static void sdma_v4_4_2_dump_ip_state(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	int i, j;
	uint32_t instance_offset;
	uint32_t reg_count = ARRAY_SIZE(sdma_reg_list_4_4_2);

	if (!adev->sdma.ip_dump)
		return;

	for (i = 0; i < adev->sdma.num_instances; i++) {
		instance_offset = i * reg_count;
		for (j = 0; j < reg_count; j++)
			adev->sdma.ip_dump[instance_offset + j] =
				RREG32(sdma_v4_4_2_get_reg_offset(adev, i,
				       sdma_reg_list_4_4_2[j].reg_offset));
	}
}

const struct amd_ip_funcs sdma_v4_4_2_ip_funcs = {
	.name = "sdma_v4_4_2",
	.early_init = sdma_v4_4_2_early_init,
	.late_init = sdma_v4_4_2_late_init,
	.sw_init = sdma_v4_4_2_sw_init,
	.sw_fini = sdma_v4_4_2_sw_fini,
	.hw_init = sdma_v4_4_2_hw_init,
	.hw_fini = sdma_v4_4_2_hw_fini,
	.suspend = sdma_v4_4_2_suspend,
	.resume = sdma_v4_4_2_resume,
	.is_idle = sdma_v4_4_2_is_idle,
	.wait_for_idle = sdma_v4_4_2_wait_for_idle,
	.soft_reset = sdma_v4_4_2_soft_reset,
	.set_clockgating_state = sdma_v4_4_2_set_clockgating_state,
	.set_powergating_state = sdma_v4_4_2_set_powergating_state,
	.get_clockgating_state = sdma_v4_4_2_get_clockgating_state,
	.dump_ip_state = sdma_v4_4_2_dump_ip_state,
	.print_ip_state = sdma_v4_4_2_print_ip_state,
};

static const struct amdgpu_ring_funcs sdma_v4_4_2_ring_funcs = {
	.type = AMDGPU_RING_TYPE_SDMA,
	.align_mask = 0xff,
	.nop = SDMA_PKT_NOP_HEADER_OP(SDMA_OP_NOP),
	.support_64bit_ptrs = true,
	.get_rptr = sdma_v4_4_2_ring_get_rptr,
	.get_wptr = sdma_v4_4_2_ring_get_wptr,
	.set_wptr = sdma_v4_4_2_ring_set_wptr,
	.emit_frame_size =
		6 + /* sdma_v4_4_2_ring_emit_hdp_flush */
		3 + /* hdp invalidate */
		6 + /* sdma_v4_4_2_ring_emit_pipeline_sync */
		/* sdma_v4_4_2_ring_emit_vm_flush */
		SOC15_FLUSH_GPU_TLB_NUM_WREG * 3 +
		SOC15_FLUSH_GPU_TLB_NUM_REG_WAIT * 6 +
		10 + 10 + 10, /* sdma_v4_4_2_ring_emit_fence x3 for user fence, vm fence */
	.emit_ib_size = 7 + 6, /* sdma_v4_4_2_ring_emit_ib */
	.emit_ib = sdma_v4_4_2_ring_emit_ib,
	.emit_fence = sdma_v4_4_2_ring_emit_fence,
	.emit_pipeline_sync = sdma_v4_4_2_ring_emit_pipeline_sync,
	.emit_vm_flush = sdma_v4_4_2_ring_emit_vm_flush,
	.emit_hdp_flush = sdma_v4_4_2_ring_emit_hdp_flush,
	.test_ring = sdma_v4_4_2_ring_test_ring,
	.test_ib = sdma_v4_4_2_ring_test_ib,
	.insert_nop = sdma_v4_4_2_ring_insert_nop,
	.pad_ib = sdma_v4_4_2_ring_pad_ib,
	.emit_wreg = sdma_v4_4_2_ring_emit_wreg,
	.emit_reg_wait = sdma_v4_4_2_ring_emit_reg_wait,
	.emit_reg_write_reg_wait = amdgpu_ring_emit_reg_write_reg_wait_helper,
	.reset = sdma_v4_4_2_reset_queue,
	.is_guilty = sdma_v4_4_2_ring_is_guilty,
};

static const struct amdgpu_ring_funcs sdma_v4_4_2_page_ring_funcs = {
	.type = AMDGPU_RING_TYPE_SDMA,
	.align_mask = 0xff,
	.nop = SDMA_PKT_NOP_HEADER_OP(SDMA_OP_NOP),
	.support_64bit_ptrs = true,
	.get_rptr = sdma_v4_4_2_ring_get_rptr,
	.get_wptr = sdma_v4_4_2_page_ring_get_wptr,
	.set_wptr = sdma_v4_4_2_page_ring_set_wptr,
	.emit_frame_size =
		6 + /* sdma_v4_4_2_ring_emit_hdp_flush */
		3 + /* hdp invalidate */
		6 + /* sdma_v4_4_2_ring_emit_pipeline_sync */
		/* sdma_v4_4_2_ring_emit_vm_flush */
		SOC15_FLUSH_GPU_TLB_NUM_WREG * 3 +
		SOC15_FLUSH_GPU_TLB_NUM_REG_WAIT * 6 +
		10 + 10 + 10, /* sdma_v4_4_2_ring_emit_fence x3 for user fence, vm fence */
	.emit_ib_size = 7 + 6, /* sdma_v4_4_2_ring_emit_ib */
	.emit_ib = sdma_v4_4_2_ring_emit_ib,
	.emit_fence = sdma_v4_4_2_ring_emit_fence,
	.emit_pipeline_sync = sdma_v4_4_2_ring_emit_pipeline_sync,
	.emit_vm_flush = sdma_v4_4_2_ring_emit_vm_flush,
	.emit_hdp_flush = sdma_v4_4_2_ring_emit_hdp_flush,
	.test_ring = sdma_v4_4_2_ring_test_ring,
	.test_ib = sdma_v4_4_2_ring_test_ib,
	.insert_nop = sdma_v4_4_2_ring_insert_nop,
	.pad_ib = sdma_v4_4_2_ring_pad_ib,
	.emit_wreg = sdma_v4_4_2_ring_emit_wreg,
	.emit_reg_wait = sdma_v4_4_2_ring_emit_reg_wait,
	.emit_reg_write_reg_wait = amdgpu_ring_emit_reg_write_reg_wait_helper,
	.reset = sdma_v4_4_2_reset_queue,
	.is_guilty = sdma_v4_4_2_page_ring_is_guilty,
};

static void sdma_v4_4_2_set_ring_funcs(struct amdgpu_device *adev)
{
	int i, dev_inst;

	for (i = 0; i < adev->sdma.num_instances; i++) {
		adev->sdma.instance[i].ring.funcs = &sdma_v4_4_2_ring_funcs;
		adev->sdma.instance[i].ring.me = i;
		if (adev->sdma.has_page_queue) {
			adev->sdma.instance[i].page.funcs =
				&sdma_v4_4_2_page_ring_funcs;
			adev->sdma.instance[i].page.me = i;
		}

		dev_inst = GET_INST(SDMA0, i);
		/* AID to which SDMA belongs depends on physical instance */
		adev->sdma.instance[i].aid_id =
			dev_inst / adev->sdma.num_inst_per_aid;
	}
}

static const struct amdgpu_irq_src_funcs sdma_v4_4_2_trap_irq_funcs = {
	.set = sdma_v4_4_2_set_trap_irq_state,
	.process = sdma_v4_4_2_process_trap_irq,
};

static const struct amdgpu_irq_src_funcs sdma_v4_4_2_illegal_inst_irq_funcs = {
	.process = sdma_v4_4_2_process_illegal_inst_irq,
};

static const struct amdgpu_irq_src_funcs sdma_v4_4_2_ecc_irq_funcs = {
	.set = sdma_v4_4_2_set_ecc_irq_state,
	.process = amdgpu_sdma_process_ecc_irq,
};

static const struct amdgpu_irq_src_funcs sdma_v4_4_2_vm_hole_irq_funcs = {
	.process = sdma_v4_4_2_process_vm_hole_irq,
};

static const struct amdgpu_irq_src_funcs sdma_v4_4_2_doorbell_invalid_irq_funcs = {
	.process = sdma_v4_4_2_process_doorbell_invalid_irq,
};

static const struct amdgpu_irq_src_funcs sdma_v4_4_2_pool_timeout_irq_funcs = {
	.process = sdma_v4_4_2_process_pool_timeout_irq,
};

static const struct amdgpu_irq_src_funcs sdma_v4_4_2_srbm_write_irq_funcs = {
	.process = sdma_v4_4_2_process_srbm_write_irq,
};

static const struct amdgpu_irq_src_funcs sdma_v4_4_2_ctxt_empty_irq_funcs = {
	.process = sdma_v4_4_2_process_ctxt_empty_irq,
};

static void sdma_v4_4_2_set_irq_funcs(struct amdgpu_device *adev)
{
	adev->sdma.trap_irq.num_types = adev->sdma.num_instances;
	adev->sdma.ecc_irq.num_types = adev->sdma.num_instances;
	adev->sdma.vm_hole_irq.num_types = adev->sdma.num_instances;
	adev->sdma.doorbell_invalid_irq.num_types = adev->sdma.num_instances;
	adev->sdma.pool_timeout_irq.num_types = adev->sdma.num_instances;
	adev->sdma.srbm_write_irq.num_types = adev->sdma.num_instances;
	adev->sdma.ctxt_empty_irq.num_types = adev->sdma.num_instances;

	adev->sdma.trap_irq.funcs = &sdma_v4_4_2_trap_irq_funcs;
	adev->sdma.illegal_inst_irq.funcs = &sdma_v4_4_2_illegal_inst_irq_funcs;
	adev->sdma.ecc_irq.funcs = &sdma_v4_4_2_ecc_irq_funcs;
	adev->sdma.vm_hole_irq.funcs = &sdma_v4_4_2_vm_hole_irq_funcs;
	adev->sdma.doorbell_invalid_irq.funcs = &sdma_v4_4_2_doorbell_invalid_irq_funcs;
	adev->sdma.pool_timeout_irq.funcs = &sdma_v4_4_2_pool_timeout_irq_funcs;
	adev->sdma.srbm_write_irq.funcs = &sdma_v4_4_2_srbm_write_irq_funcs;
	adev->sdma.ctxt_empty_irq.funcs = &sdma_v4_4_2_ctxt_empty_irq_funcs;
}

/**
 * sdma_v4_4_2_emit_copy_buffer - copy buffer using the sDMA engine
 *
 * @ib: indirect buffer to copy to
 * @src_offset: src GPU address
 * @dst_offset: dst GPU address
 * @byte_count: number of bytes to xfer
 * @copy_flags: copy flags for the buffers
 *
 * Copy GPU buffers using the DMA engine.
 * Used by the amdgpu ttm implementation to move pages if
 * registered as the asic copy callback.
 */
static void sdma_v4_4_2_emit_copy_buffer(struct amdgpu_ib *ib,
				       uint64_t src_offset,
				       uint64_t dst_offset,
				       uint32_t byte_count,
				       uint32_t copy_flags)
{
	ib->ptr[ib->length_dw++] = SDMA_PKT_HEADER_OP(SDMA_OP_COPY) |
		SDMA_PKT_HEADER_SUB_OP(SDMA_SUBOP_COPY_LINEAR) |
		SDMA_PKT_COPY_LINEAR_HEADER_TMZ((copy_flags & AMDGPU_COPY_FLAGS_TMZ) ? 1 : 0);
	ib->ptr[ib->length_dw++] = byte_count - 1;
	ib->ptr[ib->length_dw++] = 0; /* src/dst endian swap */
	ib->ptr[ib->length_dw++] = lower_32_bits(src_offset);
	ib->ptr[ib->length_dw++] = upper_32_bits(src_offset);
	ib->ptr[ib->length_dw++] = lower_32_bits(dst_offset);
	ib->ptr[ib->length_dw++] = upper_32_bits(dst_offset);
}

/**
 * sdma_v4_4_2_emit_fill_buffer - fill buffer using the sDMA engine
 *
 * @ib: indirect buffer to copy to
 * @src_data: value to write to buffer
 * @dst_offset: dst GPU address
 * @byte_count: number of bytes to xfer
 *
 * Fill GPU buffers using the DMA engine.
 */
static void sdma_v4_4_2_emit_fill_buffer(struct amdgpu_ib *ib,
				       uint32_t src_data,
				       uint64_t dst_offset,
				       uint32_t byte_count)
{
	ib->ptr[ib->length_dw++] = SDMA_PKT_HEADER_OP(SDMA_OP_CONST_FILL);
	ib->ptr[ib->length_dw++] = lower_32_bits(dst_offset);
	ib->ptr[ib->length_dw++] = upper_32_bits(dst_offset);
	ib->ptr[ib->length_dw++] = src_data;
	ib->ptr[ib->length_dw++] = byte_count - 1;
}

static const struct amdgpu_buffer_funcs sdma_v4_4_2_buffer_funcs = {
	.copy_max_bytes = 0x400000,
	.copy_num_dw = 7,
	.emit_copy_buffer = sdma_v4_4_2_emit_copy_buffer,

	.fill_max_bytes = 0x400000,
	.fill_num_dw = 5,
	.emit_fill_buffer = sdma_v4_4_2_emit_fill_buffer,
};

static void sdma_v4_4_2_set_buffer_funcs(struct amdgpu_device *adev)
{
	adev->mman.buffer_funcs = &sdma_v4_4_2_buffer_funcs;
	if (adev->sdma.has_page_queue)
		adev->mman.buffer_funcs_ring = &adev->sdma.instance[0].page;
	else
		adev->mman.buffer_funcs_ring = &adev->sdma.instance[0].ring;
}

static const struct amdgpu_vm_pte_funcs sdma_v4_4_2_vm_pte_funcs = {
	.copy_pte_num_dw = 7,
	.copy_pte = sdma_v4_4_2_vm_copy_pte,

	.write_pte = sdma_v4_4_2_vm_write_pte,
	.set_pte_pde = sdma_v4_4_2_vm_set_pte_pde,
};

static void sdma_v4_4_2_set_vm_pte_funcs(struct amdgpu_device *adev)
{
	struct drm_gpu_scheduler *sched;
	unsigned i;

	adev->vm_manager.vm_pte_funcs = &sdma_v4_4_2_vm_pte_funcs;
	for (i = 0; i < adev->sdma.num_instances; i++) {
		if (adev->sdma.has_page_queue)
			sched = &adev->sdma.instance[i].page.sched;
		else
			sched = &adev->sdma.instance[i].ring.sched;
		adev->vm_manager.vm_pte_scheds[i] = sched;
	}
	adev->vm_manager.vm_pte_num_scheds = adev->sdma.num_instances;
}

/**
 * sdma_v4_4_2_update_reset_mask - update  reset mask for SDMA
 * @adev: Pointer to the AMDGPU device structure
 *
 * This function update reset mask for SDMA and sets the supported
 * reset types based on the IP version and firmware versions.
 *
 */
static void sdma_v4_4_2_update_reset_mask(struct amdgpu_device *adev)
{
	/* per queue reset not supported for SRIOV */
	if (amdgpu_sriov_vf(adev))
		return;

	/*
	 * the user queue relies on MEC fw and pmfw when the sdma queue do reset.
	 * it needs to check both of them at here to skip old mec and pmfw.
	 */
	switch (amdgpu_ip_version(adev, GC_HWIP, 0)) {
	case IP_VERSION(9, 4, 3):
	case IP_VERSION(9, 4, 4):
		if ((adev->gfx.mec_fw_version >= 0xb0) && amdgpu_dpm_reset_sdma_is_supported(adev))
			adev->sdma.supported_reset |= AMDGPU_RESET_TYPE_PER_QUEUE;
		break;
	case IP_VERSION(9, 5, 0):
		if ((adev->gfx.mec_fw_version >= 0xf) && amdgpu_dpm_reset_sdma_is_supported(adev))
			adev->sdma.supported_reset |= AMDGPU_RESET_TYPE_PER_QUEUE;
		break;
	default:
		break;
	}

}

const struct amdgpu_ip_block_version sdma_v4_4_2_ip_block = {
	.type = AMD_IP_BLOCK_TYPE_SDMA,
	.major = 4,
	.minor = 4,
	.rev = 2,
	.funcs = &sdma_v4_4_2_ip_funcs,
};

static int sdma_v4_4_2_xcp_resume(void *handle, uint32_t inst_mask)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int r;

	if (!amdgpu_sriov_vf(adev))
		sdma_v4_4_2_inst_init_golden_registers(adev, inst_mask);

	r = sdma_v4_4_2_inst_start(adev, inst_mask, false);

	return r;
}

static int sdma_v4_4_2_xcp_suspend(void *handle, uint32_t inst_mask)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	uint32_t tmp_mask = inst_mask;
	int i;

	if (amdgpu_ras_is_supported(adev, AMDGPU_RAS_BLOCK__SDMA)) {
		for_each_inst(i, tmp_mask) {
			amdgpu_irq_put(adev, &adev->sdma.ecc_irq,
				       AMDGPU_SDMA_IRQ_INSTANCE0 + i);
		}
	}

	sdma_v4_4_2_inst_ctx_switch_enable(adev, false, inst_mask);
	sdma_v4_4_2_inst_enable(adev, false, inst_mask);

	return 0;
}

struct amdgpu_xcp_ip_funcs sdma_v4_4_2_xcp_funcs = {
	.suspend = &sdma_v4_4_2_xcp_suspend,
	.resume = &sdma_v4_4_2_xcp_resume
};

static const struct amdgpu_ras_err_status_reg_entry sdma_v4_2_2_ue_reg_list[] = {
	{AMDGPU_RAS_REG_ENTRY(SDMA0, 0, regSDMA_UE_ERR_STATUS_LO, regSDMA_UE_ERR_STATUS_HI),
	1, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "SDMA"},
};

static const struct amdgpu_ras_memory_id_entry sdma_v4_4_2_ras_memory_list[] = {
	{AMDGPU_SDMA_MBANK_DATA_BUF0, "SDMA_MBANK_DATA_BUF0"},
	{AMDGPU_SDMA_MBANK_DATA_BUF1, "SDMA_MBANK_DATA_BUF1"},
	{AMDGPU_SDMA_MBANK_DATA_BUF2, "SDMA_MBANK_DATA_BUF2"},
	{AMDGPU_SDMA_MBANK_DATA_BUF3, "SDMA_MBANK_DATA_BUF3"},
	{AMDGPU_SDMA_MBANK_DATA_BUF4, "SDMA_MBANK_DATA_BUF4"},
	{AMDGPU_SDMA_MBANK_DATA_BUF5, "SDMA_MBANK_DATA_BUF5"},
	{AMDGPU_SDMA_MBANK_DATA_BUF6, "SDMA_MBANK_DATA_BUF6"},
	{AMDGPU_SDMA_MBANK_DATA_BUF7, "SDMA_MBANK_DATA_BUF7"},
	{AMDGPU_SDMA_MBANK_DATA_BUF8, "SDMA_MBANK_DATA_BUF8"},
	{AMDGPU_SDMA_MBANK_DATA_BUF9, "SDMA_MBANK_DATA_BUF9"},
	{AMDGPU_SDMA_MBANK_DATA_BUF10, "SDMA_MBANK_DATA_BUF10"},
	{AMDGPU_SDMA_MBANK_DATA_BUF11, "SDMA_MBANK_DATA_BUF11"},
	{AMDGPU_SDMA_MBANK_DATA_BUF12, "SDMA_MBANK_DATA_BUF12"},
	{AMDGPU_SDMA_MBANK_DATA_BUF13, "SDMA_MBANK_DATA_BUF13"},
	{AMDGPU_SDMA_MBANK_DATA_BUF14, "SDMA_MBANK_DATA_BUF14"},
	{AMDGPU_SDMA_MBANK_DATA_BUF15, "SDMA_MBANK_DATA_BUF15"},
	{AMDGPU_SDMA_UCODE_BUF, "SDMA_UCODE_BUF"},
	{AMDGPU_SDMA_RB_CMD_BUF, "SDMA_RB_CMD_BUF"},
	{AMDGPU_SDMA_IB_CMD_BUF, "SDMA_IB_CMD_BUF"},
	{AMDGPU_SDMA_UTCL1_RD_FIFO, "SDMA_UTCL1_RD_FIFO"},
	{AMDGPU_SDMA_UTCL1_RDBST_FIFO, "SDMA_UTCL1_RDBST_FIFO"},
	{AMDGPU_SDMA_UTCL1_WR_FIFO, "SDMA_UTCL1_WR_FIFO"},
	{AMDGPU_SDMA_DATA_LUT_FIFO, "SDMA_DATA_LUT_FIFO"},
	{AMDGPU_SDMA_SPLIT_DAT_BUF, "SDMA_SPLIT_DAT_BUF"},
};

static void sdma_v4_4_2_inst_query_ras_error_count(struct amdgpu_device *adev,
						   uint32_t sdma_inst,
						   void *ras_err_status)
{
	struct ras_err_data *err_data = (struct ras_err_data *)ras_err_status;
	uint32_t sdma_dev_inst = GET_INST(SDMA0, sdma_inst);
	unsigned long ue_count = 0;
	struct amdgpu_smuio_mcm_config_info mcm_info = {
		.socket_id = adev->smuio.funcs->get_socket_id(adev),
		.die_id = adev->sdma.instance[sdma_inst].aid_id,
	};

	/* sdma v4_4_2 doesn't support query ce counts */
	amdgpu_ras_inst_query_ras_error_count(adev,
					sdma_v4_2_2_ue_reg_list,
					ARRAY_SIZE(sdma_v4_2_2_ue_reg_list),
					sdma_v4_4_2_ras_memory_list,
					ARRAY_SIZE(sdma_v4_4_2_ras_memory_list),
					sdma_dev_inst,
					AMDGPU_RAS_ERROR__MULTI_UNCORRECTABLE,
					&ue_count);

	amdgpu_ras_error_statistic_ue_count(err_data, &mcm_info, ue_count);
}

static void sdma_v4_4_2_query_ras_error_count(struct amdgpu_device *adev,
					      void *ras_err_status)
{
	uint32_t inst_mask;
	int i = 0;

	inst_mask = GENMASK(adev->sdma.num_instances - 1, 0);
	if (amdgpu_ras_is_supported(adev, AMDGPU_RAS_BLOCK__SDMA)) {
		for_each_inst(i, inst_mask)
			sdma_v4_4_2_inst_query_ras_error_count(adev, i, ras_err_status);
	} else {
		dev_warn(adev->dev, "SDMA RAS is not supported\n");
	}
}

static void sdma_v4_4_2_inst_reset_ras_error_count(struct amdgpu_device *adev,
						   uint32_t sdma_inst)
{
	uint32_t sdma_dev_inst = GET_INST(SDMA0, sdma_inst);

	amdgpu_ras_inst_reset_ras_error_count(adev,
					sdma_v4_2_2_ue_reg_list,
					ARRAY_SIZE(sdma_v4_2_2_ue_reg_list),
					sdma_dev_inst);
}

static void sdma_v4_4_2_reset_ras_error_count(struct amdgpu_device *adev)
{
	uint32_t inst_mask;
	int i = 0;

	inst_mask = GENMASK(adev->sdma.num_instances - 1, 0);
	if (amdgpu_ras_is_supported(adev, AMDGPU_RAS_BLOCK__SDMA)) {
		for_each_inst(i, inst_mask)
			sdma_v4_4_2_inst_reset_ras_error_count(adev, i);
	} else {
		dev_warn(adev->dev, "SDMA RAS is not supported\n");
	}
}

static const struct amdgpu_ras_block_hw_ops sdma_v4_4_2_ras_hw_ops = {
	.query_ras_error_count = sdma_v4_4_2_query_ras_error_count,
	.reset_ras_error_count = sdma_v4_4_2_reset_ras_error_count,
};

static int sdma_v4_4_2_aca_bank_parser(struct aca_handle *handle, struct aca_bank *bank,
				       enum aca_smu_type type, void *data)
{
	struct aca_bank_info info;
	u64 misc0;
	int ret;

	ret = aca_bank_info_decode(bank, &info);
	if (ret)
		return ret;

	misc0 = bank->regs[ACA_REG_IDX_MISC0];
	switch (type) {
	case ACA_SMU_TYPE_UE:
		bank->aca_err_type = ACA_ERROR_TYPE_UE;
		ret = aca_error_cache_log_bank_error(handle, &info, ACA_ERROR_TYPE_UE,
						     1ULL);
		break;
	case ACA_SMU_TYPE_CE:
		bank->aca_err_type = ACA_ERROR_TYPE_CE;
		ret = aca_error_cache_log_bank_error(handle, &info, bank->aca_err_type,
						     ACA_REG__MISC0__ERRCNT(misc0));
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

/* CODE_SDMA0 - CODE_SDMA4, reference to smu driver if header file */
static int sdma_v4_4_2_err_codes[] = { 33, 34, 35, 36 };

static bool sdma_v4_4_2_aca_bank_is_valid(struct aca_handle *handle, struct aca_bank *bank,
					  enum aca_smu_type type, void *data)
{
	u32 instlo;

	instlo = ACA_REG__IPID__INSTANCEIDLO(bank->regs[ACA_REG_IDX_IPID]);
	instlo &= GENMASK(31, 1);

	if (instlo != mmSMNAID_AID0_MCA_SMU)
		return false;

	if (aca_bank_check_error_codes(handle->adev, bank,
				       sdma_v4_4_2_err_codes,
				       ARRAY_SIZE(sdma_v4_4_2_err_codes)))
		return false;

	return true;
}

static const struct aca_bank_ops sdma_v4_4_2_aca_bank_ops = {
	.aca_bank_parser = sdma_v4_4_2_aca_bank_parser,
	.aca_bank_is_valid = sdma_v4_4_2_aca_bank_is_valid,
};

static const struct aca_info sdma_v4_4_2_aca_info = {
	.hwip = ACA_HWIP_TYPE_SMU,
	.mask = ACA_ERROR_UE_MASK,
	.bank_ops = &sdma_v4_4_2_aca_bank_ops,
};

static int sdma_v4_4_2_ras_late_init(struct amdgpu_device *adev, struct ras_common_if *ras_block)
{
	int r;

	r = amdgpu_sdma_ras_late_init(adev, ras_block);
	if (r)
		return r;

	return amdgpu_ras_bind_aca(adev, AMDGPU_RAS_BLOCK__SDMA,
				   &sdma_v4_4_2_aca_info, NULL);
}

static struct amdgpu_sdma_ras sdma_v4_4_2_ras = {
	.ras_block = {
		.hw_ops = &sdma_v4_4_2_ras_hw_ops,
		.ras_late_init = sdma_v4_4_2_ras_late_init,
	},
};

static void sdma_v4_4_2_set_ras_funcs(struct amdgpu_device *adev)
{
	adev->sdma.ras = &sdma_v4_4_2_ras;
}
