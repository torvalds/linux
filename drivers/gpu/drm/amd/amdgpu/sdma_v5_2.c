/*
 * Copyright 2019 Advanced Micro Devices, Inc.
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

#include "gc/gc_10_3_0_offset.h"
#include "gc/gc_10_3_0_sh_mask.h"
#include "ivsrcid/sdma0/irqsrcs_sdma0_5_0.h"
#include "ivsrcid/sdma1/irqsrcs_sdma1_5_0.h"
#include "ivsrcid/sdma2/irqsrcs_sdma2_5_0.h"
#include "ivsrcid/sdma3/irqsrcs_sdma3_5_0.h"

#include "soc15_common.h"
#include "soc15.h"
#include "navi10_sdma_pkt_open.h"
#include "nbio_v2_3.h"
#include "sdma_common.h"
#include "sdma_v5_2.h"

MODULE_FIRMWARE("amdgpu/sienna_cichlid_sdma.bin");
MODULE_FIRMWARE("amdgpu/navy_flounder_sdma.bin");
MODULE_FIRMWARE("amdgpu/dimgrey_cavefish_sdma.bin");
MODULE_FIRMWARE("amdgpu/beige_goby_sdma.bin");

MODULE_FIRMWARE("amdgpu/vangogh_sdma.bin");

#define SDMA1_REG_OFFSET 0x600
#define SDMA3_REG_OFFSET 0x400
#define SDMA0_HYP_DEC_REG_START 0x5880
#define SDMA0_HYP_DEC_REG_END 0x5893
#define SDMA1_HYP_DEC_REG_OFFSET 0x20

static void sdma_v5_2_set_ring_funcs(struct amdgpu_device *adev);
static void sdma_v5_2_set_buffer_funcs(struct amdgpu_device *adev);
static void sdma_v5_2_set_vm_pte_funcs(struct amdgpu_device *adev);
static void sdma_v5_2_set_irq_funcs(struct amdgpu_device *adev);

static u32 sdma_v5_2_get_reg_offset(struct amdgpu_device *adev, u32 instance, u32 internal_offset)
{
	u32 base;

	if (internal_offset >= SDMA0_HYP_DEC_REG_START &&
	    internal_offset <= SDMA0_HYP_DEC_REG_END) {
		base = adev->reg_offset[GC_HWIP][0][1];
		if (instance != 0)
			internal_offset += SDMA1_HYP_DEC_REG_OFFSET * instance;
	} else {
		if (instance < 2) {
			base = adev->reg_offset[GC_HWIP][0][0];
			if (instance == 1)
				internal_offset += SDMA1_REG_OFFSET;
		} else {
			base = adev->reg_offset[GC_HWIP][0][2];
			if (instance == 3)
				internal_offset += SDMA3_REG_OFFSET;
		}
	}

	return base + internal_offset;
}

static void sdma_v5_2_init_golden_registers(struct amdgpu_device *adev)
{
	switch (adev->asic_type) {
	case CHIP_SIENNA_CICHLID:
	case CHIP_NAVY_FLOUNDER:
	case CHIP_VANGOGH:
	case CHIP_DIMGREY_CAVEFISH:
	case CHIP_BEIGE_GOBY:
		break;
	default:
		break;
	}
}

static int sdma_v5_2_init_inst_ctx(struct amdgpu_sdma_instance *sdma_inst)
{
	int err = 0;
	const struct sdma_firmware_header_v1_0 *hdr;

	err = amdgpu_ucode_validate(sdma_inst->fw);
	if (err)
		return err;

	hdr = (const struct sdma_firmware_header_v1_0 *)sdma_inst->fw->data;
	sdma_inst->fw_version = le32_to_cpu(hdr->header.ucode_version);
	sdma_inst->feature_version = le32_to_cpu(hdr->ucode_feature_version);

	if (sdma_inst->feature_version >= 20)
		sdma_inst->burst_nop = true;

	return 0;
}

static void sdma_v5_2_destroy_inst_ctx(struct amdgpu_device *adev)
{
	release_firmware(adev->sdma.instance[0].fw);

	memset((void *)adev->sdma.instance, 0,
	       sizeof(struct amdgpu_sdma_instance) * AMDGPU_MAX_SDMA_INSTANCES);
}

/**
 * sdma_v5_2_init_microcode - load ucode images from disk
 *
 * @adev: amdgpu_device pointer
 *
 * Use the firmware interface to load the ucode images into
 * the driver (not loaded into hw).
 * Returns 0 on success, error on failure.
 */

// emulation only, won't work on real chip
// navi10 real chip need to use PSP to load firmware
static int sdma_v5_2_init_microcode(struct amdgpu_device *adev)
{
	const char *chip_name;
	char fw_name[40];
	int err = 0, i;
	struct amdgpu_firmware_info *info = NULL;
	const struct common_firmware_header *header = NULL;

	if (amdgpu_sriov_vf(adev) && (adev->asic_type == CHIP_SIENNA_CICHLID))
		return 0;

	DRM_DEBUG("\n");

	switch (adev->asic_type) {
	case CHIP_SIENNA_CICHLID:
		chip_name = "sienna_cichlid";
		break;
	case CHIP_NAVY_FLOUNDER:
		chip_name = "navy_flounder";
		break;
	case CHIP_VANGOGH:
		chip_name = "vangogh";
		break;
	case CHIP_DIMGREY_CAVEFISH:
		chip_name = "dimgrey_cavefish";
		break;
	case CHIP_BEIGE_GOBY:
		chip_name = "beige_goby";
		break;
	default:
		BUG();
	}

	snprintf(fw_name, sizeof(fw_name), "amdgpu/%s_sdma.bin", chip_name);

	err = request_firmware(&adev->sdma.instance[0].fw, fw_name, adev->dev);
	if (err)
		goto out;

	err = sdma_v5_2_init_inst_ctx(&adev->sdma.instance[0]);
	if (err)
		goto out;

	for (i = 1; i < adev->sdma.num_instances; i++)
		memcpy((void *)&adev->sdma.instance[i],
		       (void *)&adev->sdma.instance[0],
		       sizeof(struct amdgpu_sdma_instance));

	DRM_DEBUG("psp_load == '%s'\n",
		  adev->firmware.load_type == AMDGPU_FW_LOAD_PSP ? "true" : "false");

	if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) {
		for (i = 0; i < adev->sdma.num_instances; i++) {
			info = &adev->firmware.ucode[AMDGPU_UCODE_ID_SDMA0 + i];
			info->ucode_id = AMDGPU_UCODE_ID_SDMA0 + i;
			info->fw = adev->sdma.instance[i].fw;
			header = (const struct common_firmware_header *)info->fw->data;
			adev->firmware.fw_size +=
				ALIGN(le32_to_cpu(header->ucode_size_bytes), PAGE_SIZE);
		}
	}

out:
	if (err) {
		DRM_ERROR("sdma_v5_2: Failed to load firmware \"%s\"\n", fw_name);
		sdma_v5_2_destroy_inst_ctx(adev);
	}
	return err;
}

static unsigned sdma_v5_2_ring_init_cond_exec(struct amdgpu_ring *ring)
{
	unsigned ret;

	amdgpu_ring_write(ring, SDMA_PKT_HEADER_OP(SDMA_OP_COND_EXE));
	amdgpu_ring_write(ring, lower_32_bits(ring->cond_exe_gpu_addr));
	amdgpu_ring_write(ring, upper_32_bits(ring->cond_exe_gpu_addr));
	amdgpu_ring_write(ring, 1);
	ret = ring->wptr & ring->buf_mask;/* this is the offset we need patch later */
	amdgpu_ring_write(ring, 0x55aa55aa);/* insert dummy here and patch it later */

	return ret;
}

static void sdma_v5_2_ring_patch_cond_exec(struct amdgpu_ring *ring,
					   unsigned offset)
{
	unsigned cur;

	BUG_ON(offset > ring->buf_mask);
	BUG_ON(ring->ring[offset] != 0x55aa55aa);

	cur = (ring->wptr - 1) & ring->buf_mask;
	if (cur > offset)
		ring->ring[offset] = cur - offset;
	else
		ring->ring[offset] = (ring->buf_mask + 1) - offset + cur;
}

/**
 * sdma_v5_2_ring_get_rptr - get the current read pointer
 *
 * @ring: amdgpu ring pointer
 *
 * Get the current rptr from the hardware (NAVI10+).
 */
static uint64_t sdma_v5_2_ring_get_rptr(struct amdgpu_ring *ring)
{
	u64 *rptr;

	/* XXX check if swapping is necessary on BE */
	rptr = ((u64 *)&ring->adev->wb.wb[ring->rptr_offs]);

	DRM_DEBUG("rptr before shift == 0x%016llx\n", *rptr);
	return ((*rptr) >> 2);
}

/**
 * sdma_v5_2_ring_get_wptr - get the current write pointer
 *
 * @ring: amdgpu ring pointer
 *
 * Get the current wptr from the hardware (NAVI10+).
 */
static uint64_t sdma_v5_2_ring_get_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	u64 wptr;

	if (ring->use_doorbell) {
		/* XXX check if swapping is necessary on BE */
		wptr = READ_ONCE(*((u64 *)&adev->wb.wb[ring->wptr_offs]));
		DRM_DEBUG("wptr/doorbell before shift == 0x%016llx\n", wptr);
	} else {
		wptr = RREG32(sdma_v5_2_get_reg_offset(adev, ring->me, mmSDMA0_GFX_RB_WPTR_HI));
		wptr = wptr << 32;
		wptr |= RREG32(sdma_v5_2_get_reg_offset(adev, ring->me, mmSDMA0_GFX_RB_WPTR));
		DRM_DEBUG("wptr before shift [%i] wptr == 0x%016llx\n", ring->me, wptr);
	}

	return wptr >> 2;
}

/**
 * sdma_v5_2_ring_set_wptr - commit the write pointer
 *
 * @ring: amdgpu ring pointer
 *
 * Write the wptr back to the hardware (NAVI10+).
 */
static void sdma_v5_2_ring_set_wptr(struct amdgpu_ring *ring)
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
		adev->wb.wb[ring->wptr_offs] = lower_32_bits(ring->wptr << 2);
		adev->wb.wb[ring->wptr_offs + 1] = upper_32_bits(ring->wptr << 2);
		DRM_DEBUG("calling WDOORBELL64(0x%08x, 0x%016llx)\n",
				ring->doorbell_index, ring->wptr << 2);
		WDOORBELL64(ring->doorbell_index, ring->wptr << 2);
	} else {
		DRM_DEBUG("Not using doorbell -- "
				"mmSDMA%i_GFX_RB_WPTR == 0x%08x "
				"mmSDMA%i_GFX_RB_WPTR_HI == 0x%08x\n",
				ring->me,
				lower_32_bits(ring->wptr << 2),
				ring->me,
				upper_32_bits(ring->wptr << 2));
		WREG32(sdma_v5_2_get_reg_offset(adev, ring->me, mmSDMA0_GFX_RB_WPTR),
			lower_32_bits(ring->wptr << 2));
		WREG32(sdma_v5_2_get_reg_offset(adev, ring->me, mmSDMA0_GFX_RB_WPTR_HI),
			upper_32_bits(ring->wptr << 2));
	}
}

static void sdma_v5_2_ring_insert_nop(struct amdgpu_ring *ring, uint32_t count)
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
 * sdma_v5_2_ring_emit_ib - Schedule an IB on the DMA engine
 *
 * @ring: amdgpu ring pointer
 * @job: job to retrieve vmid from
 * @ib: IB object to schedule
 * @flags: unused
 *
 * Schedule an IB in the DMA ring.
 */
static void sdma_v5_2_ring_emit_ib(struct amdgpu_ring *ring,
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
	sdma_v5_2_ring_insert_nop(ring, (2 - lower_32_bits(ring->wptr)) & 7);

	amdgpu_ring_write(ring, SDMA_PKT_HEADER_OP(SDMA_OP_INDIRECT) |
			  SDMA_PKT_INDIRECT_HEADER_VMID(vmid & 0xf));
	/* base must be 32 byte aligned */
	amdgpu_ring_write(ring, lower_32_bits(ib->gpu_addr) & 0xffffffe0);
	amdgpu_ring_write(ring, upper_32_bits(ib->gpu_addr));
	amdgpu_ring_write(ring, ib->length_dw);
	amdgpu_ring_write(ring, lower_32_bits(csa_mc_addr));
	amdgpu_ring_write(ring, upper_32_bits(csa_mc_addr));
}

/**
 * sdma_v5_2_ring_emit_mem_sync - flush the IB by graphics cache rinse
 *
 * @ring: amdgpu ring pointer
 * @job: job to retrieve vmid from
 * @ib: IB object to schedule
 *
 * flush the IB by graphics cache rinse.
 */
static void sdma_v5_2_ring_emit_mem_sync(struct amdgpu_ring *ring)
{
    uint32_t gcr_cntl =
		    SDMA_GCR_GL2_INV | SDMA_GCR_GL2_WB | SDMA_GCR_GLM_INV |
			SDMA_GCR_GL1_INV | SDMA_GCR_GLV_INV | SDMA_GCR_GLK_INV |
			SDMA_GCR_GLI_INV(1);

	/* flush entire cache L0/L1/L2, this can be optimized by performance requirement */
	amdgpu_ring_write(ring, SDMA_PKT_HEADER_OP(SDMA_OP_GCR_REQ));
	amdgpu_ring_write(ring, SDMA_PKT_GCR_REQ_PAYLOAD1_BASE_VA_31_7(0));
	amdgpu_ring_write(ring, SDMA_PKT_GCR_REQ_PAYLOAD2_GCR_CONTROL_15_0(gcr_cntl) |
			SDMA_PKT_GCR_REQ_PAYLOAD2_BASE_VA_47_32(0));
	amdgpu_ring_write(ring, SDMA_PKT_GCR_REQ_PAYLOAD3_LIMIT_VA_31_7(0) |
			SDMA_PKT_GCR_REQ_PAYLOAD3_GCR_CONTROL_18_16(gcr_cntl >> 16));
	amdgpu_ring_write(ring, SDMA_PKT_GCR_REQ_PAYLOAD4_LIMIT_VA_47_32(0) |
			SDMA_PKT_GCR_REQ_PAYLOAD4_VMID(0));
}

/**
 * sdma_v5_2_ring_emit_hdp_flush - emit an hdp flush on the DMA ring
 *
 * @ring: amdgpu ring pointer
 *
 * Emit an hdp flush packet on the requested DMA ring.
 */
static void sdma_v5_2_ring_emit_hdp_flush(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	u32 ref_and_mask = 0;
	const struct nbio_hdp_flush_reg *nbio_hf_reg = adev->nbio.hdp_flush_reg;

	ref_and_mask = nbio_hf_reg->ref_and_mask_sdma0 << ring->me;

	amdgpu_ring_write(ring, SDMA_PKT_HEADER_OP(SDMA_OP_POLL_REGMEM) |
			  SDMA_PKT_POLL_REGMEM_HEADER_HDP_FLUSH(1) |
			  SDMA_PKT_POLL_REGMEM_HEADER_FUNC(3)); /* == */
	amdgpu_ring_write(ring, (adev->nbio.funcs->get_hdp_flush_done_offset(adev)) << 2);
	amdgpu_ring_write(ring, (adev->nbio.funcs->get_hdp_flush_req_offset(adev)) << 2);
	amdgpu_ring_write(ring, ref_and_mask); /* reference */
	amdgpu_ring_write(ring, ref_and_mask); /* mask */
	amdgpu_ring_write(ring, SDMA_PKT_POLL_REGMEM_DW5_RETRY_COUNT(0xfff) |
			  SDMA_PKT_POLL_REGMEM_DW5_INTERVAL(10)); /* retry count, poll interval */
}

/**
 * sdma_v5_2_ring_emit_fence - emit a fence on the DMA ring
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
static void sdma_v5_2_ring_emit_fence(struct amdgpu_ring *ring, u64 addr, u64 seq,
				      unsigned flags)
{
	bool write64bit = flags & AMDGPU_FENCE_FLAG_64BIT;
	/* write the fence */
	amdgpu_ring_write(ring, SDMA_PKT_HEADER_OP(SDMA_OP_FENCE) |
			  SDMA_PKT_FENCE_HEADER_MTYPE(0x3)); /* Ucached(UC) */
	/* zero in first two bits */
	BUG_ON(addr & 0x3);
	amdgpu_ring_write(ring, lower_32_bits(addr));
	amdgpu_ring_write(ring, upper_32_bits(addr));
	amdgpu_ring_write(ring, lower_32_bits(seq));

	/* optionally write high bits as well */
	if (write64bit) {
		addr += 4;
		amdgpu_ring_write(ring, SDMA_PKT_HEADER_OP(SDMA_OP_FENCE) |
				  SDMA_PKT_FENCE_HEADER_MTYPE(0x3));
		/* zero in first two bits */
		BUG_ON(addr & 0x3);
		amdgpu_ring_write(ring, lower_32_bits(addr));
		amdgpu_ring_write(ring, upper_32_bits(addr));
		amdgpu_ring_write(ring, upper_32_bits(seq));
	}

	if (flags & AMDGPU_FENCE_FLAG_INT) {
		/* generate an interrupt */
		amdgpu_ring_write(ring, SDMA_PKT_HEADER_OP(SDMA_OP_TRAP));
		amdgpu_ring_write(ring, SDMA_PKT_TRAP_INT_CONTEXT_INT_CONTEXT(0));
	}
}


/**
 * sdma_v5_2_gfx_stop - stop the gfx async dma engines
 *
 * @adev: amdgpu_device pointer
 *
 * Stop the gfx async dma ring buffers.
 */
static void sdma_v5_2_gfx_stop(struct amdgpu_device *adev)
{
	struct amdgpu_ring *sdma0 = &adev->sdma.instance[0].ring;
	struct amdgpu_ring *sdma1 = &adev->sdma.instance[1].ring;
	struct amdgpu_ring *sdma2 = &adev->sdma.instance[2].ring;
	struct amdgpu_ring *sdma3 = &adev->sdma.instance[3].ring;
	u32 rb_cntl, ib_cntl;
	int i;

	if ((adev->mman.buffer_funcs_ring == sdma0) ||
	    (adev->mman.buffer_funcs_ring == sdma1) ||
	    (adev->mman.buffer_funcs_ring == sdma2) ||
	    (adev->mman.buffer_funcs_ring == sdma3))
		amdgpu_ttm_set_buffer_funcs_status(adev, false);

	for (i = 0; i < adev->sdma.num_instances; i++) {
		rb_cntl = RREG32(sdma_v5_2_get_reg_offset(adev, i, mmSDMA0_GFX_RB_CNTL));
		rb_cntl = REG_SET_FIELD(rb_cntl, SDMA0_GFX_RB_CNTL, RB_ENABLE, 0);
		WREG32(sdma_v5_2_get_reg_offset(adev, i, mmSDMA0_GFX_RB_CNTL), rb_cntl);
		ib_cntl = RREG32(sdma_v5_2_get_reg_offset(adev, i, mmSDMA0_GFX_IB_CNTL));
		ib_cntl = REG_SET_FIELD(ib_cntl, SDMA0_GFX_IB_CNTL, IB_ENABLE, 0);
		WREG32(sdma_v5_2_get_reg_offset(adev, i, mmSDMA0_GFX_IB_CNTL), ib_cntl);
	}
}

/**
 * sdma_v5_2_rlc_stop - stop the compute async dma engines
 *
 * @adev: amdgpu_device pointer
 *
 * Stop the compute async dma queues.
 */
static void sdma_v5_2_rlc_stop(struct amdgpu_device *adev)
{
	/* XXX todo */
}

/**
 * sdma_v5_2_ctx_switch_enable - stop the async dma engines context switch
 *
 * @adev: amdgpu_device pointer
 * @enable: enable/disable the DMA MEs context switch.
 *
 * Halt or unhalt the async dma engines context switch.
 */
static void sdma_v5_2_ctx_switch_enable(struct amdgpu_device *adev, bool enable)
{
	u32 f32_cntl, phase_quantum = 0;
	int i;

	if (amdgpu_sdma_phase_quantum) {
		unsigned value = amdgpu_sdma_phase_quantum;
		unsigned unit = 0;

		while (value > (SDMA0_PHASE0_QUANTUM__VALUE_MASK >>
				SDMA0_PHASE0_QUANTUM__VALUE__SHIFT)) {
			value = (value + 1) >> 1;
			unit++;
		}
		if (unit > (SDMA0_PHASE0_QUANTUM__UNIT_MASK >>
			    SDMA0_PHASE0_QUANTUM__UNIT__SHIFT)) {
			value = (SDMA0_PHASE0_QUANTUM__VALUE_MASK >>
				 SDMA0_PHASE0_QUANTUM__VALUE__SHIFT);
			unit = (SDMA0_PHASE0_QUANTUM__UNIT_MASK >>
				SDMA0_PHASE0_QUANTUM__UNIT__SHIFT);
			WARN_ONCE(1,
			"clamping sdma_phase_quantum to %uK clock cycles\n",
				  value << unit);
		}
		phase_quantum =
			value << SDMA0_PHASE0_QUANTUM__VALUE__SHIFT |
			unit  << SDMA0_PHASE0_QUANTUM__UNIT__SHIFT;
	}

	for (i = 0; i < adev->sdma.num_instances; i++) {
		f32_cntl = RREG32(sdma_v5_2_get_reg_offset(adev, i, mmSDMA0_CNTL));
		f32_cntl = REG_SET_FIELD(f32_cntl, SDMA0_CNTL,
				AUTO_CTXSW_ENABLE, enable ? 1 : 0);
		if (enable && amdgpu_sdma_phase_quantum) {
			WREG32(sdma_v5_2_get_reg_offset(adev, i, mmSDMA0_PHASE0_QUANTUM),
			       phase_quantum);
			WREG32(sdma_v5_2_get_reg_offset(adev, i, mmSDMA0_PHASE1_QUANTUM),
			       phase_quantum);
			WREG32(sdma_v5_2_get_reg_offset(adev, i, mmSDMA0_PHASE2_QUANTUM),
			       phase_quantum);
		}
		WREG32(sdma_v5_2_get_reg_offset(adev, i, mmSDMA0_CNTL), f32_cntl);
	}

}

/**
 * sdma_v5_2_enable - stop the async dma engines
 *
 * @adev: amdgpu_device pointer
 * @enable: enable/disable the DMA MEs.
 *
 * Halt or unhalt the async dma engines.
 */
static void sdma_v5_2_enable(struct amdgpu_device *adev, bool enable)
{
	u32 f32_cntl;
	int i;

	if (!enable) {
		sdma_v5_2_gfx_stop(adev);
		sdma_v5_2_rlc_stop(adev);
	}

	for (i = 0; i < adev->sdma.num_instances; i++) {
		f32_cntl = RREG32(sdma_v5_2_get_reg_offset(adev, i, mmSDMA0_F32_CNTL));
		f32_cntl = REG_SET_FIELD(f32_cntl, SDMA0_F32_CNTL, HALT, enable ? 0 : 1);
		WREG32(sdma_v5_2_get_reg_offset(adev, i, mmSDMA0_F32_CNTL), f32_cntl);
	}
}

/**
 * sdma_v5_2_gfx_resume - setup and start the async dma engines
 *
 * @adev: amdgpu_device pointer
 *
 * Set up the gfx DMA ring buffers and enable them.
 * Returns 0 for success, error for failure.
 */
static int sdma_v5_2_gfx_resume(struct amdgpu_device *adev)
{
	struct amdgpu_ring *ring;
	u32 rb_cntl, ib_cntl;
	u32 rb_bufsz;
	u32 wb_offset;
	u32 doorbell;
	u32 doorbell_offset;
	u32 temp;
	u32 wptr_poll_cntl;
	u64 wptr_gpu_addr;
	int i, r;

	for (i = 0; i < adev->sdma.num_instances; i++) {
		ring = &adev->sdma.instance[i].ring;
		wb_offset = (ring->rptr_offs * 4);

		WREG32(sdma_v5_2_get_reg_offset(adev, i, mmSDMA0_SEM_WAIT_FAIL_TIMER_CNTL), 0);

		/* Set ring buffer size in dwords */
		rb_bufsz = order_base_2(ring->ring_size / 4);
		rb_cntl = RREG32(sdma_v5_2_get_reg_offset(adev, i, mmSDMA0_GFX_RB_CNTL));
		rb_cntl = REG_SET_FIELD(rb_cntl, SDMA0_GFX_RB_CNTL, RB_SIZE, rb_bufsz);
#ifdef __BIG_ENDIAN
		rb_cntl = REG_SET_FIELD(rb_cntl, SDMA0_GFX_RB_CNTL, RB_SWAP_ENABLE, 1);
		rb_cntl = REG_SET_FIELD(rb_cntl, SDMA0_GFX_RB_CNTL,
					RPTR_WRITEBACK_SWAP_ENABLE, 1);
#endif
		WREG32(sdma_v5_2_get_reg_offset(adev, i, mmSDMA0_GFX_RB_CNTL), rb_cntl);

		/* Initialize the ring buffer's read and write pointers */
		WREG32(sdma_v5_2_get_reg_offset(adev, i, mmSDMA0_GFX_RB_RPTR), 0);
		WREG32(sdma_v5_2_get_reg_offset(adev, i, mmSDMA0_GFX_RB_RPTR_HI), 0);
		WREG32(sdma_v5_2_get_reg_offset(adev, i, mmSDMA0_GFX_RB_WPTR), 0);
		WREG32(sdma_v5_2_get_reg_offset(adev, i, mmSDMA0_GFX_RB_WPTR_HI), 0);

		/* setup the wptr shadow polling */
		wptr_gpu_addr = adev->wb.gpu_addr + (ring->wptr_offs * 4);
		WREG32(sdma_v5_2_get_reg_offset(adev, i, mmSDMA0_GFX_RB_WPTR_POLL_ADDR_LO),
		       lower_32_bits(wptr_gpu_addr));
		WREG32(sdma_v5_2_get_reg_offset(adev, i, mmSDMA0_GFX_RB_WPTR_POLL_ADDR_HI),
		       upper_32_bits(wptr_gpu_addr));
		wptr_poll_cntl = RREG32(sdma_v5_2_get_reg_offset(adev, i,
							 mmSDMA0_GFX_RB_WPTR_POLL_CNTL));
		wptr_poll_cntl = REG_SET_FIELD(wptr_poll_cntl,
					       SDMA0_GFX_RB_WPTR_POLL_CNTL,
					       F32_POLL_ENABLE, 1);
		WREG32(sdma_v5_2_get_reg_offset(adev, i, mmSDMA0_GFX_RB_WPTR_POLL_CNTL),
		       wptr_poll_cntl);

		/* set the wb address whether it's enabled or not */
		WREG32(sdma_v5_2_get_reg_offset(adev, i, mmSDMA0_GFX_RB_RPTR_ADDR_HI),
		       upper_32_bits(adev->wb.gpu_addr + wb_offset) & 0xFFFFFFFF);
		WREG32(sdma_v5_2_get_reg_offset(adev, i, mmSDMA0_GFX_RB_RPTR_ADDR_LO),
		       lower_32_bits(adev->wb.gpu_addr + wb_offset) & 0xFFFFFFFC);

		rb_cntl = REG_SET_FIELD(rb_cntl, SDMA0_GFX_RB_CNTL, RPTR_WRITEBACK_ENABLE, 1);

		WREG32(sdma_v5_2_get_reg_offset(adev, i, mmSDMA0_GFX_RB_BASE), ring->gpu_addr >> 8);
		WREG32(sdma_v5_2_get_reg_offset(adev, i, mmSDMA0_GFX_RB_BASE_HI), ring->gpu_addr >> 40);

		ring->wptr = 0;

		/* before programing wptr to a less value, need set minor_ptr_update first */
		WREG32(sdma_v5_2_get_reg_offset(adev, i, mmSDMA0_GFX_MINOR_PTR_UPDATE), 1);

		if (!amdgpu_sriov_vf(adev)) { /* only bare-metal use register write for wptr */
			WREG32(sdma_v5_2_get_reg_offset(adev, i, mmSDMA0_GFX_RB_WPTR), lower_32_bits(ring->wptr) << 2);
			WREG32(sdma_v5_2_get_reg_offset(adev, i, mmSDMA0_GFX_RB_WPTR_HI), upper_32_bits(ring->wptr) << 2);
		}

		doorbell = RREG32(sdma_v5_2_get_reg_offset(adev, i, mmSDMA0_GFX_DOORBELL));
		doorbell_offset = RREG32(sdma_v5_2_get_reg_offset(adev, i, mmSDMA0_GFX_DOORBELL_OFFSET));

		if (ring->use_doorbell) {
			doorbell = REG_SET_FIELD(doorbell, SDMA0_GFX_DOORBELL, ENABLE, 1);
			doorbell_offset = REG_SET_FIELD(doorbell_offset, SDMA0_GFX_DOORBELL_OFFSET,
					OFFSET, ring->doorbell_index);
		} else {
			doorbell = REG_SET_FIELD(doorbell, SDMA0_GFX_DOORBELL, ENABLE, 0);
		}
		WREG32(sdma_v5_2_get_reg_offset(adev, i, mmSDMA0_GFX_DOORBELL), doorbell);
		WREG32(sdma_v5_2_get_reg_offset(adev, i, mmSDMA0_GFX_DOORBELL_OFFSET), doorbell_offset);

		adev->nbio.funcs->sdma_doorbell_range(adev, i, ring->use_doorbell,
						      ring->doorbell_index,
						      adev->doorbell_index.sdma_doorbell_range);

		if (amdgpu_sriov_vf(adev))
			sdma_v5_2_ring_set_wptr(ring);

		/* set minor_ptr_update to 0 after wptr programed */
		WREG32(sdma_v5_2_get_reg_offset(adev, i, mmSDMA0_GFX_MINOR_PTR_UPDATE), 0);

		/* set utc l1 enable flag always to 1 */
		temp = RREG32(sdma_v5_2_get_reg_offset(adev, i, mmSDMA0_CNTL));
		temp = REG_SET_FIELD(temp, SDMA0_CNTL, UTC_L1_ENABLE, 1);

		/* enable MCBP */
		temp = REG_SET_FIELD(temp, SDMA0_CNTL, MIDCMD_PREEMPT_ENABLE, 1);
		WREG32(sdma_v5_2_get_reg_offset(adev, i, mmSDMA0_CNTL), temp);

		/* Set up RESP_MODE to non-copy addresses */
		temp = RREG32(sdma_v5_2_get_reg_offset(adev, i, mmSDMA0_UTCL1_CNTL));
		temp = REG_SET_FIELD(temp, SDMA0_UTCL1_CNTL, RESP_MODE, 3);
		temp = REG_SET_FIELD(temp, SDMA0_UTCL1_CNTL, REDO_DELAY, 9);
		WREG32(sdma_v5_2_get_reg_offset(adev, i, mmSDMA0_UTCL1_CNTL), temp);

		/* program default cache read and write policy */
		temp = RREG32(sdma_v5_2_get_reg_offset(adev, i, mmSDMA0_UTCL1_PAGE));
		/* clean read policy and write policy bits */
		temp &= 0xFF0FFF;
		temp |= ((CACHE_READ_POLICY_L2__DEFAULT << 12) |
			 (CACHE_WRITE_POLICY_L2__DEFAULT << 14) |
			 SDMA0_UTCL1_PAGE__LLC_NOALLOC_MASK);
		WREG32(sdma_v5_2_get_reg_offset(adev, i, mmSDMA0_UTCL1_PAGE), temp);

		if (!amdgpu_sriov_vf(adev)) {
			/* unhalt engine */
			temp = RREG32(sdma_v5_2_get_reg_offset(adev, i, mmSDMA0_F32_CNTL));
			temp = REG_SET_FIELD(temp, SDMA0_F32_CNTL, HALT, 0);
			WREG32(sdma_v5_2_get_reg_offset(adev, i, mmSDMA0_F32_CNTL), temp);
		}

		/* enable DMA RB */
		rb_cntl = REG_SET_FIELD(rb_cntl, SDMA0_GFX_RB_CNTL, RB_ENABLE, 1);
		WREG32(sdma_v5_2_get_reg_offset(adev, i, mmSDMA0_GFX_RB_CNTL), rb_cntl);

		ib_cntl = RREG32(sdma_v5_2_get_reg_offset(adev, i, mmSDMA0_GFX_IB_CNTL));
		ib_cntl = REG_SET_FIELD(ib_cntl, SDMA0_GFX_IB_CNTL, IB_ENABLE, 1);
#ifdef __BIG_ENDIAN
		ib_cntl = REG_SET_FIELD(ib_cntl, SDMA0_GFX_IB_CNTL, IB_SWAP_ENABLE, 1);
#endif
		/* enable DMA IBs */
		WREG32(sdma_v5_2_get_reg_offset(adev, i, mmSDMA0_GFX_IB_CNTL), ib_cntl);

		ring->sched.ready = true;

		if (amdgpu_sriov_vf(adev)) { /* bare-metal sequence doesn't need below to lines */
			sdma_v5_2_ctx_switch_enable(adev, true);
			sdma_v5_2_enable(adev, true);
		}

		r = amdgpu_ring_test_ring(ring);
		if (r) {
			ring->sched.ready = false;
			return r;
		}

		if (adev->mman.buffer_funcs_ring == ring)
			amdgpu_ttm_set_buffer_funcs_status(adev, true);
	}

	return 0;
}

/**
 * sdma_v5_2_rlc_resume - setup and start the async dma engines
 *
 * @adev: amdgpu_device pointer
 *
 * Set up the compute DMA queues and enable them.
 * Returns 0 for success, error for failure.
 */
static int sdma_v5_2_rlc_resume(struct amdgpu_device *adev)
{
	return 0;
}

/**
 * sdma_v5_2_load_microcode - load the sDMA ME ucode
 *
 * @adev: amdgpu_device pointer
 *
 * Loads the sDMA0/1/2/3 ucode.
 * Returns 0 for success, -EINVAL if the ucode is not available.
 */
static int sdma_v5_2_load_microcode(struct amdgpu_device *adev)
{
	const struct sdma_firmware_header_v1_0 *hdr;
	const __le32 *fw_data;
	u32 fw_size;
	int i, j;

	/* halt the MEs */
	sdma_v5_2_enable(adev, false);

	for (i = 0; i < adev->sdma.num_instances; i++) {
		if (!adev->sdma.instance[i].fw)
			return -EINVAL;

		hdr = (const struct sdma_firmware_header_v1_0 *)adev->sdma.instance[i].fw->data;
		amdgpu_ucode_print_sdma_hdr(&hdr->header);
		fw_size = le32_to_cpu(hdr->header.ucode_size_bytes) / 4;

		fw_data = (const __le32 *)
			(adev->sdma.instance[i].fw->data +
				le32_to_cpu(hdr->header.ucode_array_offset_bytes));

		WREG32(sdma_v5_2_get_reg_offset(adev, i, mmSDMA0_UCODE_ADDR), 0);

		for (j = 0; j < fw_size; j++) {
			if (amdgpu_emu_mode == 1 && j % 500 == 0)
				msleep(1);
			WREG32(sdma_v5_2_get_reg_offset(adev, i, mmSDMA0_UCODE_DATA), le32_to_cpup(fw_data++));
		}

		WREG32(sdma_v5_2_get_reg_offset(adev, i, mmSDMA0_UCODE_ADDR), adev->sdma.instance[i].fw_version);
	}

	return 0;
}

static int sdma_v5_2_soft_reset(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	u32 grbm_soft_reset;
	u32 tmp;
	int i;

	for (i = 0; i < adev->sdma.num_instances; i++) {
		grbm_soft_reset = REG_SET_FIELD(0,
						GRBM_SOFT_RESET, SOFT_RESET_SDMA0,
						1);
		grbm_soft_reset <<= i;

		tmp = RREG32_SOC15(GC, 0, mmGRBM_SOFT_RESET);
		tmp |= grbm_soft_reset;
		DRM_DEBUG("GRBM_SOFT_RESET=0x%08X\n", tmp);
		WREG32_SOC15(GC, 0, mmGRBM_SOFT_RESET, tmp);
		tmp = RREG32_SOC15(GC, 0, mmGRBM_SOFT_RESET);

		udelay(50);

		tmp &= ~grbm_soft_reset;
		WREG32_SOC15(GC, 0, mmGRBM_SOFT_RESET, tmp);
		tmp = RREG32_SOC15(GC, 0, mmGRBM_SOFT_RESET);

		udelay(50);
	}

	return 0;
}

/**
 * sdma_v5_2_start - setup and start the async dma engines
 *
 * @adev: amdgpu_device pointer
 *
 * Set up the DMA engines and enable them.
 * Returns 0 for success, error for failure.
 */
static int sdma_v5_2_start(struct amdgpu_device *adev)
{
	int r = 0;

	if (amdgpu_sriov_vf(adev)) {
		sdma_v5_2_ctx_switch_enable(adev, false);
		sdma_v5_2_enable(adev, false);

		/* set RB registers */
		r = sdma_v5_2_gfx_resume(adev);
		return r;
	}

	if (adev->firmware.load_type == AMDGPU_FW_LOAD_DIRECT) {
		r = sdma_v5_2_load_microcode(adev);
		if (r)
			return r;

		/* The value of mmSDMA_F32_CNTL is invalid the moment after loading fw */
		if (amdgpu_emu_mode == 1)
			msleep(1000);
	}

	sdma_v5_2_soft_reset(adev);
	/* unhalt the MEs */
	sdma_v5_2_enable(adev, true);
	/* enable sdma ring preemption */
	sdma_v5_2_ctx_switch_enable(adev, true);

	/* start the gfx rings and rlc compute queues */
	r = sdma_v5_2_gfx_resume(adev);
	if (r)
		return r;
	r = sdma_v5_2_rlc_resume(adev);

	return r;
}

/**
 * sdma_v5_2_ring_test_ring - simple async dma engine test
 *
 * @ring: amdgpu_ring structure holding ring information
 *
 * Test the DMA engine by writing using it to write an
 * value to memory.
 * Returns 0 for success, error for failure.
 */
static int sdma_v5_2_ring_test_ring(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	unsigned i;
	unsigned index;
	int r;
	u32 tmp;
	u64 gpu_addr;

	r = amdgpu_device_wb_get(adev, &index);
	if (r) {
		dev_err(adev->dev, "(%d) failed to allocate wb slot\n", r);
		return r;
	}

	gpu_addr = adev->wb.gpu_addr + (index * 4);
	tmp = 0xCAFEDEAD;
	adev->wb.wb[index] = cpu_to_le32(tmp);

	r = amdgpu_ring_alloc(ring, 5);
	if (r) {
		DRM_ERROR("amdgpu: dma failed to lock ring %d (%d).\n", ring->idx, r);
		amdgpu_device_wb_free(adev, index);
		return r;
	}

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
 * sdma_v5_2_ring_test_ib - test an IB on the DMA engine
 *
 * @ring: amdgpu_ring structure holding ring information
 * @timeout: timeout value in jiffies, or MAX_SCHEDULE_TIMEOUT
 *
 * Test a simple IB in the DMA ring.
 * Returns 0 on success, error on failure.
 */
static int sdma_v5_2_ring_test_ib(struct amdgpu_ring *ring, long timeout)
{
	struct amdgpu_device *adev = ring->adev;
	struct amdgpu_ib ib;
	struct dma_fence *f = NULL;
	unsigned index;
	long r;
	u32 tmp = 0;
	u64 gpu_addr;

	r = amdgpu_device_wb_get(adev, &index);
	if (r) {
		dev_err(adev->dev, "(%ld) failed to allocate wb slot\n", r);
		return r;
	}

	gpu_addr = adev->wb.gpu_addr + (index * 4);
	tmp = 0xCAFEDEAD;
	adev->wb.wb[index] = cpu_to_le32(tmp);
	memset(&ib, 0, sizeof(ib));
	r = amdgpu_ib_get(adev, NULL, 256, AMDGPU_IB_POOL_DIRECT, &ib);
	if (r) {
		DRM_ERROR("amdgpu: failed to get ib (%ld).\n", r);
		goto err0;
	}

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
	amdgpu_ib_free(adev, &ib, NULL);
	dma_fence_put(f);
err0:
	amdgpu_device_wb_free(adev, index);
	return r;
}


/**
 * sdma_v5_2_vm_copy_pte - update PTEs by copying them from the GART
 *
 * @ib: indirect buffer to fill with commands
 * @pe: addr of the page entry
 * @src: src addr to copy from
 * @count: number of page entries to update
 *
 * Update PTEs by copying them from the GART using sDMA.
 */
static void sdma_v5_2_vm_copy_pte(struct amdgpu_ib *ib,
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
 * sdma_v5_2_vm_write_pte - update PTEs by writing them manually
 *
 * @ib: indirect buffer to fill with commands
 * @pe: addr of the page entry
 * @value: dst addr to write into pe
 * @count: number of page entries to update
 * @incr: increase next addr by incr bytes
 *
 * Update PTEs by writing them manually using sDMA.
 */
static void sdma_v5_2_vm_write_pte(struct amdgpu_ib *ib, uint64_t pe,
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
 * sdma_v5_2_vm_set_pte_pde - update the page tables using sDMA
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
static void sdma_v5_2_vm_set_pte_pde(struct amdgpu_ib *ib,
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
 * sdma_v5_2_ring_pad_ib - pad the IB
 *
 * @ib: indirect buffer to fill with padding
 * @ring: amdgpu_ring structure holding ring information
 *
 * Pad the IB with NOPs to a boundary multiple of 8.
 */
static void sdma_v5_2_ring_pad_ib(struct amdgpu_ring *ring, struct amdgpu_ib *ib)
{
	struct amdgpu_sdma_instance *sdma = amdgpu_sdma_get_instance_from_ring(ring);
	u32 pad_count;
	int i;

	pad_count = (-ib->length_dw) & 0x7;
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
 * sdma_v5_2_ring_emit_pipeline_sync - sync the pipeline
 *
 * @ring: amdgpu_ring pointer
 *
 * Make sure all previous operations are completed (CIK).
 */
static void sdma_v5_2_ring_emit_pipeline_sync(struct amdgpu_ring *ring)
{
	uint32_t seq = ring->fence_drv.sync_seq;
	uint64_t addr = ring->fence_drv.gpu_addr;

	/* wait for idle */
	amdgpu_ring_write(ring, SDMA_PKT_HEADER_OP(SDMA_OP_POLL_REGMEM) |
			  SDMA_PKT_POLL_REGMEM_HEADER_HDP_FLUSH(0) |
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
 * sdma_v5_2_ring_emit_vm_flush - vm flush using sDMA
 *
 * @ring: amdgpu_ring pointer
 * @vmid: vmid number to use
 * @pd_addr: address
 *
 * Update the page table base and flush the VM TLB
 * using sDMA.
 */
static void sdma_v5_2_ring_emit_vm_flush(struct amdgpu_ring *ring,
					 unsigned vmid, uint64_t pd_addr)
{
	amdgpu_gmc_emit_flush_gpu_tlb(ring, vmid, pd_addr);
}

static void sdma_v5_2_ring_emit_wreg(struct amdgpu_ring *ring,
				     uint32_t reg, uint32_t val)
{
	amdgpu_ring_write(ring, SDMA_PKT_HEADER_OP(SDMA_OP_SRBM_WRITE) |
			  SDMA_PKT_SRBM_WRITE_HEADER_BYTE_EN(0xf));
	amdgpu_ring_write(ring, reg);
	amdgpu_ring_write(ring, val);
}

static void sdma_v5_2_ring_emit_reg_wait(struct amdgpu_ring *ring, uint32_t reg,
					 uint32_t val, uint32_t mask)
{
	amdgpu_ring_write(ring, SDMA_PKT_HEADER_OP(SDMA_OP_POLL_REGMEM) |
			  SDMA_PKT_POLL_REGMEM_HEADER_HDP_FLUSH(0) |
			  SDMA_PKT_POLL_REGMEM_HEADER_FUNC(3)); /* equal */
	amdgpu_ring_write(ring, reg << 2);
	amdgpu_ring_write(ring, 0);
	amdgpu_ring_write(ring, val); /* reference */
	amdgpu_ring_write(ring, mask); /* mask */
	amdgpu_ring_write(ring, SDMA_PKT_POLL_REGMEM_DW5_RETRY_COUNT(0xfff) |
			  SDMA_PKT_POLL_REGMEM_DW5_INTERVAL(10));
}

static void sdma_v5_2_ring_emit_reg_write_reg_wait(struct amdgpu_ring *ring,
						   uint32_t reg0, uint32_t reg1,
						   uint32_t ref, uint32_t mask)
{
	amdgpu_ring_emit_wreg(ring, reg0, ref);
	/* wait for a cycle to reset vm_inv_eng*_ack */
	amdgpu_ring_emit_reg_wait(ring, reg0, 0, 0);
	amdgpu_ring_emit_reg_wait(ring, reg1, mask, mask);
}

static int sdma_v5_2_early_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	switch (adev->asic_type) {
	case CHIP_SIENNA_CICHLID:
		adev->sdma.num_instances = 4;
		break;
	case CHIP_NAVY_FLOUNDER:
	case CHIP_DIMGREY_CAVEFISH:
		adev->sdma.num_instances = 2;
		break;
	case CHIP_VANGOGH:
	case CHIP_BEIGE_GOBY:
		adev->sdma.num_instances = 1;
		break;
	default:
		break;
	}

	sdma_v5_2_set_ring_funcs(adev);
	sdma_v5_2_set_buffer_funcs(adev);
	sdma_v5_2_set_vm_pte_funcs(adev);
	sdma_v5_2_set_irq_funcs(adev);

	return 0;
}

static unsigned sdma_v5_2_seq_to_irq_id(int seq_num)
{
	switch (seq_num) {
	case 0:
		return SOC15_IH_CLIENTID_SDMA0;
	case 1:
		return SOC15_IH_CLIENTID_SDMA1;
	case 2:
		return SOC15_IH_CLIENTID_SDMA2;
	case 3:
		return SOC15_IH_CLIENTID_SDMA3_Sienna_Cichlid;
	default:
		break;
	}
	return -EINVAL;
}

static unsigned sdma_v5_2_seq_to_trap_id(int seq_num)
{
	switch (seq_num) {
	case 0:
		return SDMA0_5_0__SRCID__SDMA_TRAP;
	case 1:
		return SDMA1_5_0__SRCID__SDMA_TRAP;
	case 2:
		return SDMA2_5_0__SRCID__SDMA_TRAP;
	case 3:
		return SDMA3_5_0__SRCID__SDMA_TRAP;
	default:
		break;
	}
	return -EINVAL;
}

static int sdma_v5_2_sw_init(void *handle)
{
	struct amdgpu_ring *ring;
	int r, i;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	/* SDMA trap event */
	for (i = 0; i < adev->sdma.num_instances; i++) {
		r = amdgpu_irq_add_id(adev, sdma_v5_2_seq_to_irq_id(i),
				      sdma_v5_2_seq_to_trap_id(i),
				      &adev->sdma.trap_irq);
		if (r)
			return r;
	}

	r = sdma_v5_2_init_microcode(adev);
	if (r) {
		DRM_ERROR("Failed to load sdma firmware!\n");
		return r;
	}

	for (i = 0; i < adev->sdma.num_instances; i++) {
		ring = &adev->sdma.instance[i].ring;
		ring->ring_obj = NULL;
		ring->use_doorbell = true;
		ring->me = i;

		DRM_INFO("use_doorbell being set to: [%s]\n",
				ring->use_doorbell?"true":"false");

		ring->doorbell_index =
			(adev->doorbell_index.sdma_engine[i] << 1); //get DWORD offset

		sprintf(ring->name, "sdma%d", i);
		r = amdgpu_ring_init(adev, ring, 1024, &adev->sdma.trap_irq,
				     AMDGPU_SDMA_IRQ_INSTANCE0 + i,
				     AMDGPU_RING_PRIO_DEFAULT, NULL);
		if (r)
			return r;
	}

	return r;
}

static int sdma_v5_2_sw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int i;

	for (i = 0; i < adev->sdma.num_instances; i++)
		amdgpu_ring_fini(&adev->sdma.instance[i].ring);

	sdma_v5_2_destroy_inst_ctx(adev);

	return 0;
}

static int sdma_v5_2_hw_init(void *handle)
{
	int r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	sdma_v5_2_init_golden_registers(adev);

	r = sdma_v5_2_start(adev);

	return r;
}

static int sdma_v5_2_hw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (amdgpu_sriov_vf(adev))
		return 0;

	sdma_v5_2_ctx_switch_enable(adev, false);
	sdma_v5_2_enable(adev, false);

	return 0;
}

static int sdma_v5_2_suspend(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	return sdma_v5_2_hw_fini(adev);
}

static int sdma_v5_2_resume(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	return sdma_v5_2_hw_init(adev);
}

static bool sdma_v5_2_is_idle(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	u32 i;

	for (i = 0; i < adev->sdma.num_instances; i++) {
		u32 tmp = RREG32(sdma_v5_2_get_reg_offset(adev, i, mmSDMA0_STATUS_REG));

		if (!(tmp & SDMA0_STATUS_REG__IDLE_MASK))
			return false;
	}

	return true;
}

static int sdma_v5_2_wait_for_idle(void *handle)
{
	unsigned i;
	u32 sdma0, sdma1, sdma2, sdma3;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	for (i = 0; i < adev->usec_timeout; i++) {
		sdma0 = RREG32(sdma_v5_2_get_reg_offset(adev, 0, mmSDMA0_STATUS_REG));
		sdma1 = RREG32(sdma_v5_2_get_reg_offset(adev, 1, mmSDMA0_STATUS_REG));
		sdma2 = RREG32(sdma_v5_2_get_reg_offset(adev, 2, mmSDMA0_STATUS_REG));
		sdma3 = RREG32(sdma_v5_2_get_reg_offset(adev, 3, mmSDMA0_STATUS_REG));

		if (sdma0 & sdma1 & sdma2 & sdma3 & SDMA0_STATUS_REG__IDLE_MASK)
			return 0;
		udelay(1);
	}
	return -ETIMEDOUT;
}

static int sdma_v5_2_ring_preempt_ib(struct amdgpu_ring *ring)
{
	int i, r = 0;
	struct amdgpu_device *adev = ring->adev;
	u32 index = 0;
	u64 sdma_gfx_preempt;

	amdgpu_sdma_get_index_from_ring(ring, &index);
	sdma_gfx_preempt =
		sdma_v5_2_get_reg_offset(adev, index, mmSDMA0_GFX_PREEMPT);

	/* assert preemption condition */
	amdgpu_ring_set_preempt_cond_exec(ring, false);

	/* emit the trailing fence */
	ring->trail_seq += 1;
	amdgpu_ring_alloc(ring, 10);
	sdma_v5_2_ring_emit_fence(ring, ring->trail_fence_gpu_addr,
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

static int sdma_v5_2_set_trap_irq_state(struct amdgpu_device *adev,
					struct amdgpu_irq_src *source,
					unsigned type,
					enum amdgpu_interrupt_state state)
{
	u32 sdma_cntl;

	u32 reg_offset = sdma_v5_2_get_reg_offset(adev, type, mmSDMA0_CNTL);

	sdma_cntl = RREG32(reg_offset);
	sdma_cntl = REG_SET_FIELD(sdma_cntl, SDMA0_CNTL, TRAP_ENABLE,
		       state == AMDGPU_IRQ_STATE_ENABLE ? 1 : 0);
	WREG32(reg_offset, sdma_cntl);

	return 0;
}

static int sdma_v5_2_process_trap_irq(struct amdgpu_device *adev,
				      struct amdgpu_irq_src *source,
				      struct amdgpu_iv_entry *entry)
{
	DRM_DEBUG("IH: SDMA trap\n");
	switch (entry->client_id) {
	case SOC15_IH_CLIENTID_SDMA0:
		switch (entry->ring_id) {
		case 0:
			amdgpu_fence_process(&adev->sdma.instance[0].ring);
			break;
		case 1:
			/* XXX compute */
			break;
		case 2:
			/* XXX compute */
			break;
		case 3:
			/* XXX page queue*/
			break;
		}
		break;
	case SOC15_IH_CLIENTID_SDMA1:
		switch (entry->ring_id) {
		case 0:
			amdgpu_fence_process(&adev->sdma.instance[1].ring);
			break;
		case 1:
			/* XXX compute */
			break;
		case 2:
			/* XXX compute */
			break;
		case 3:
			/* XXX page queue*/
			break;
		}
		break;
	case SOC15_IH_CLIENTID_SDMA2:
		switch (entry->ring_id) {
		case 0:
			amdgpu_fence_process(&adev->sdma.instance[2].ring);
			break;
		case 1:
			/* XXX compute */
			break;
		case 2:
			/* XXX compute */
			break;
		case 3:
			/* XXX page queue*/
			break;
		}
		break;
	case SOC15_IH_CLIENTID_SDMA3_Sienna_Cichlid:
		switch (entry->ring_id) {
		case 0:
			amdgpu_fence_process(&adev->sdma.instance[3].ring);
			break;
		case 1:
			/* XXX compute */
			break;
		case 2:
			/* XXX compute */
			break;
		case 3:
			/* XXX page queue*/
			break;
		}
		break;
	}
	return 0;
}

static int sdma_v5_2_process_illegal_inst_irq(struct amdgpu_device *adev,
					      struct amdgpu_irq_src *source,
					      struct amdgpu_iv_entry *entry)
{
	return 0;
}

static void sdma_v5_2_update_medium_grain_clock_gating(struct amdgpu_device *adev,
						       bool enable)
{
	uint32_t data, def;
	int i;

	for (i = 0; i < adev->sdma.num_instances; i++) {

		if (adev->sdma.instance[i].fw_version < 70 && adev->asic_type == CHIP_VANGOGH)
			adev->cg_flags &= ~AMD_CG_SUPPORT_SDMA_MGCG;

		if (enable && (adev->cg_flags & AMD_CG_SUPPORT_SDMA_MGCG)) {
			/* Enable sdma clock gating */
			def = data = RREG32(sdma_v5_2_get_reg_offset(adev, i, mmSDMA0_CLK_CTRL));
			data &= ~(SDMA0_CLK_CTRL__SOFT_OVERRIDE4_MASK |
				  SDMA0_CLK_CTRL__SOFT_OVERRIDE3_MASK |
				  SDMA0_CLK_CTRL__SOFT_OVERRIDE2_MASK |
				  SDMA0_CLK_CTRL__SOFT_OVERRIDE1_MASK |
				  SDMA0_CLK_CTRL__SOFT_OVERRIDE0_MASK |
				  SDMA0_CLK_CTRL__SOFT_OVERRIDER_REG_MASK);
			if (def != data)
				WREG32(sdma_v5_2_get_reg_offset(adev, i, mmSDMA0_CLK_CTRL), data);
		} else {
			/* Disable sdma clock gating */
			def = data = RREG32(sdma_v5_2_get_reg_offset(adev, i, mmSDMA0_CLK_CTRL));
			data |= (SDMA0_CLK_CTRL__SOFT_OVERRIDE4_MASK |
				 SDMA0_CLK_CTRL__SOFT_OVERRIDE3_MASK |
				 SDMA0_CLK_CTRL__SOFT_OVERRIDE2_MASK |
				 SDMA0_CLK_CTRL__SOFT_OVERRIDE1_MASK |
				 SDMA0_CLK_CTRL__SOFT_OVERRIDE0_MASK |
				 SDMA0_CLK_CTRL__SOFT_OVERRIDER_REG_MASK);
			if (def != data)
				WREG32(sdma_v5_2_get_reg_offset(adev, i, mmSDMA0_CLK_CTRL), data);
		}
	}
}

static void sdma_v5_2_update_medium_grain_light_sleep(struct amdgpu_device *adev,
						      bool enable)
{
	uint32_t data, def;
	int i;

	for (i = 0; i < adev->sdma.num_instances; i++) {

		if (adev->sdma.instance[i].fw_version < 70 && adev->asic_type == CHIP_VANGOGH)
			adev->cg_flags &= ~AMD_CG_SUPPORT_SDMA_LS;

		if (enable && (adev->cg_flags & AMD_CG_SUPPORT_SDMA_LS)) {
			/* Enable sdma mem light sleep */
			def = data = RREG32(sdma_v5_2_get_reg_offset(adev, i, mmSDMA0_POWER_CNTL));
			data |= SDMA0_POWER_CNTL__MEM_POWER_OVERRIDE_MASK;
			if (def != data)
				WREG32(sdma_v5_2_get_reg_offset(adev, i, mmSDMA0_POWER_CNTL), data);

		} else {
			/* Disable sdma mem light sleep */
			def = data = RREG32(sdma_v5_2_get_reg_offset(adev, i, mmSDMA0_POWER_CNTL));
			data &= ~SDMA0_POWER_CNTL__MEM_POWER_OVERRIDE_MASK;
			if (def != data)
				WREG32(sdma_v5_2_get_reg_offset(adev, i, mmSDMA0_POWER_CNTL), data);

		}
	}
}

static int sdma_v5_2_set_clockgating_state(void *handle,
					   enum amd_clockgating_state state)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (amdgpu_sriov_vf(adev))
		return 0;

	switch (adev->asic_type) {
	case CHIP_SIENNA_CICHLID:
	case CHIP_NAVY_FLOUNDER:
	case CHIP_VANGOGH:
	case CHIP_DIMGREY_CAVEFISH:
	case CHIP_BEIGE_GOBY:
		sdma_v5_2_update_medium_grain_clock_gating(adev,
				state == AMD_CG_STATE_GATE);
		sdma_v5_2_update_medium_grain_light_sleep(adev,
				state == AMD_CG_STATE_GATE);
		break;
	default:
		break;
	}

	return 0;
}

static int sdma_v5_2_set_powergating_state(void *handle,
					  enum amd_powergating_state state)
{
	return 0;
}

static void sdma_v5_2_get_clockgating_state(void *handle, u32 *flags)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int data;

	if (amdgpu_sriov_vf(adev))
		*flags = 0;

	/* AMD_CG_SUPPORT_SDMA_LS */
	data = RREG32_KIQ(sdma_v5_2_get_reg_offset(adev, 0, mmSDMA0_POWER_CNTL));
	if (data & SDMA0_POWER_CNTL__MEM_POWER_OVERRIDE_MASK)
		*flags |= AMD_CG_SUPPORT_SDMA_LS;
}

const struct amd_ip_funcs sdma_v5_2_ip_funcs = {
	.name = "sdma_v5_2",
	.early_init = sdma_v5_2_early_init,
	.late_init = NULL,
	.sw_init = sdma_v5_2_sw_init,
	.sw_fini = sdma_v5_2_sw_fini,
	.hw_init = sdma_v5_2_hw_init,
	.hw_fini = sdma_v5_2_hw_fini,
	.suspend = sdma_v5_2_suspend,
	.resume = sdma_v5_2_resume,
	.is_idle = sdma_v5_2_is_idle,
	.wait_for_idle = sdma_v5_2_wait_for_idle,
	.soft_reset = sdma_v5_2_soft_reset,
	.set_clockgating_state = sdma_v5_2_set_clockgating_state,
	.set_powergating_state = sdma_v5_2_set_powergating_state,
	.get_clockgating_state = sdma_v5_2_get_clockgating_state,
};

static const struct amdgpu_ring_funcs sdma_v5_2_ring_funcs = {
	.type = AMDGPU_RING_TYPE_SDMA,
	.align_mask = 0xf,
	.nop = SDMA_PKT_NOP_HEADER_OP(SDMA_OP_NOP),
	.support_64bit_ptrs = true,
	.vmhub = AMDGPU_GFXHUB_0,
	.get_rptr = sdma_v5_2_ring_get_rptr,
	.get_wptr = sdma_v5_2_ring_get_wptr,
	.set_wptr = sdma_v5_2_ring_set_wptr,
	.emit_frame_size =
		5 + /* sdma_v5_2_ring_init_cond_exec */
		6 + /* sdma_v5_2_ring_emit_hdp_flush */
		3 + /* hdp_invalidate */
		6 + /* sdma_v5_2_ring_emit_pipeline_sync */
		/* sdma_v5_2_ring_emit_vm_flush */
		SOC15_FLUSH_GPU_TLB_NUM_WREG * 3 +
		SOC15_FLUSH_GPU_TLB_NUM_REG_WAIT * 6 +
		10 + 10 + 10, /* sdma_v5_2_ring_emit_fence x3 for user fence, vm fence */
	.emit_ib_size = 7 + 6, /* sdma_v5_2_ring_emit_ib */
	.emit_ib = sdma_v5_2_ring_emit_ib,
	.emit_mem_sync = sdma_v5_2_ring_emit_mem_sync,
	.emit_fence = sdma_v5_2_ring_emit_fence,
	.emit_pipeline_sync = sdma_v5_2_ring_emit_pipeline_sync,
	.emit_vm_flush = sdma_v5_2_ring_emit_vm_flush,
	.emit_hdp_flush = sdma_v5_2_ring_emit_hdp_flush,
	.test_ring = sdma_v5_2_ring_test_ring,
	.test_ib = sdma_v5_2_ring_test_ib,
	.insert_nop = sdma_v5_2_ring_insert_nop,
	.pad_ib = sdma_v5_2_ring_pad_ib,
	.emit_wreg = sdma_v5_2_ring_emit_wreg,
	.emit_reg_wait = sdma_v5_2_ring_emit_reg_wait,
	.emit_reg_write_reg_wait = sdma_v5_2_ring_emit_reg_write_reg_wait,
	.init_cond_exec = sdma_v5_2_ring_init_cond_exec,
	.patch_cond_exec = sdma_v5_2_ring_patch_cond_exec,
	.preempt_ib = sdma_v5_2_ring_preempt_ib,
};

static void sdma_v5_2_set_ring_funcs(struct amdgpu_device *adev)
{
	int i;

	for (i = 0; i < adev->sdma.num_instances; i++) {
		adev->sdma.instance[i].ring.funcs = &sdma_v5_2_ring_funcs;
		adev->sdma.instance[i].ring.me = i;
	}
}

static const struct amdgpu_irq_src_funcs sdma_v5_2_trap_irq_funcs = {
	.set = sdma_v5_2_set_trap_irq_state,
	.process = sdma_v5_2_process_trap_irq,
};

static const struct amdgpu_irq_src_funcs sdma_v5_2_illegal_inst_irq_funcs = {
	.process = sdma_v5_2_process_illegal_inst_irq,
};

static void sdma_v5_2_set_irq_funcs(struct amdgpu_device *adev)
{
	adev->sdma.trap_irq.num_types = AMDGPU_SDMA_IRQ_INSTANCE0 +
					adev->sdma.num_instances;
	adev->sdma.trap_irq.funcs = &sdma_v5_2_trap_irq_funcs;
	adev->sdma.illegal_inst_irq.funcs = &sdma_v5_2_illegal_inst_irq_funcs;
}

/**
 * sdma_v5_2_emit_copy_buffer - copy buffer using the sDMA engine
 *
 * @ib: indirect buffer to copy to
 * @src_offset: src GPU address
 * @dst_offset: dst GPU address
 * @byte_count: number of bytes to xfer
 * @tmz: if a secure copy should be used
 *
 * Copy GPU buffers using the DMA engine.
 * Used by the amdgpu ttm implementation to move pages if
 * registered as the asic copy callback.
 */
static void sdma_v5_2_emit_copy_buffer(struct amdgpu_ib *ib,
				       uint64_t src_offset,
				       uint64_t dst_offset,
				       uint32_t byte_count,
				       bool tmz)
{
	ib->ptr[ib->length_dw++] = SDMA_PKT_HEADER_OP(SDMA_OP_COPY) |
		SDMA_PKT_HEADER_SUB_OP(SDMA_SUBOP_COPY_LINEAR) |
		SDMA_PKT_COPY_LINEAR_HEADER_TMZ(tmz ? 1 : 0);
	ib->ptr[ib->length_dw++] = byte_count - 1;
	ib->ptr[ib->length_dw++] = 0; /* src/dst endian swap */
	ib->ptr[ib->length_dw++] = lower_32_bits(src_offset);
	ib->ptr[ib->length_dw++] = upper_32_bits(src_offset);
	ib->ptr[ib->length_dw++] = lower_32_bits(dst_offset);
	ib->ptr[ib->length_dw++] = upper_32_bits(dst_offset);
}

/**
 * sdma_v5_2_emit_fill_buffer - fill buffer using the sDMA engine
 *
 * @ib: indirect buffer to fill
 * @src_data: value to write to buffer
 * @dst_offset: dst GPU address
 * @byte_count: number of bytes to xfer
 *
 * Fill GPU buffers using the DMA engine.
 */
static void sdma_v5_2_emit_fill_buffer(struct amdgpu_ib *ib,
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

static const struct amdgpu_buffer_funcs sdma_v5_2_buffer_funcs = {
	.copy_max_bytes = 0x400000,
	.copy_num_dw = 7,
	.emit_copy_buffer = sdma_v5_2_emit_copy_buffer,

	.fill_max_bytes = 0x400000,
	.fill_num_dw = 5,
	.emit_fill_buffer = sdma_v5_2_emit_fill_buffer,
};

static void sdma_v5_2_set_buffer_funcs(struct amdgpu_device *adev)
{
	if (adev->mman.buffer_funcs == NULL) {
		adev->mman.buffer_funcs = &sdma_v5_2_buffer_funcs;
		adev->mman.buffer_funcs_ring = &adev->sdma.instance[0].ring;
	}
}

static const struct amdgpu_vm_pte_funcs sdma_v5_2_vm_pte_funcs = {
	.copy_pte_num_dw = 7,
	.copy_pte = sdma_v5_2_vm_copy_pte,
	.write_pte = sdma_v5_2_vm_write_pte,
	.set_pte_pde = sdma_v5_2_vm_set_pte_pde,
};

static void sdma_v5_2_set_vm_pte_funcs(struct amdgpu_device *adev)
{
	unsigned i;

	if (adev->vm_manager.vm_pte_funcs == NULL) {
		adev->vm_manager.vm_pte_funcs = &sdma_v5_2_vm_pte_funcs;
		for (i = 0; i < adev->sdma.num_instances; i++) {
			adev->vm_manager.vm_pte_scheds[i] =
				&adev->sdma.instance[i].ring.sched;
		}
		adev->vm_manager.vm_pte_num_scheds = adev->sdma.num_instances;
	}
}

const struct amdgpu_ip_block_version sdma_v5_2_ip_block = {
	.type = AMD_IP_BLOCK_TYPE_SDMA,
	.major = 5,
	.minor = 2,
	.rev = 0,
	.funcs = &sdma_v5_2_ip_funcs,
};
