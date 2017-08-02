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
 *
 */

#include <linux/firmware.h>
#include <drm/drmP.h>
#include "amdgpu.h"
#include "amdgpu_ucode.h"
#include "amdgpu_trace.h"

#include "vega10/soc15ip.h"
#include "vega10/SDMA0/sdma0_4_0_offset.h"
#include "vega10/SDMA0/sdma0_4_0_sh_mask.h"
#include "vega10/SDMA1/sdma1_4_0_offset.h"
#include "vega10/SDMA1/sdma1_4_0_sh_mask.h"
#include "vega10/MMHUB/mmhub_1_0_offset.h"
#include "vega10/MMHUB/mmhub_1_0_sh_mask.h"
#include "vega10/HDP/hdp_4_0_offset.h"
#include "raven1/SDMA0/sdma0_4_1_default.h"

#include "soc15_common.h"
#include "soc15.h"
#include "vega10_sdma_pkt_open.h"

MODULE_FIRMWARE("amdgpu/vega10_sdma.bin");
MODULE_FIRMWARE("amdgpu/vega10_sdma1.bin");
MODULE_FIRMWARE("amdgpu/raven_sdma.bin");

#define SDMA0_POWER_CNTL__ON_OFF_CONDITION_HOLD_TIME_MASK  0x000000F8L
#define SDMA0_POWER_CNTL__ON_OFF_STATUS_DURATION_TIME_MASK 0xFC000000L

static void sdma_v4_0_set_ring_funcs(struct amdgpu_device *adev);
static void sdma_v4_0_set_buffer_funcs(struct amdgpu_device *adev);
static void sdma_v4_0_set_vm_pte_funcs(struct amdgpu_device *adev);
static void sdma_v4_0_set_irq_funcs(struct amdgpu_device *adev);

static const u32 golden_settings_sdma_4[] = {
	SOC15_REG_OFFSET(SDMA0, 0, mmSDMA0_CHICKEN_BITS), 0xfe931f07, 0x02831f07,
	SOC15_REG_OFFSET(SDMA0, 0, mmSDMA0_CLK_CTRL), 0xff000ff0, 0x3f000100,
	SOC15_REG_OFFSET(SDMA0, 0, mmSDMA0_GFX_IB_CNTL), 0x800f0100, 0x00000100,
	SOC15_REG_OFFSET(SDMA0, 0, mmSDMA0_GFX_RB_WPTR_POLL_CNTL), 0xfffffff7, 0x00403000,
	SOC15_REG_OFFSET(SDMA0, 0, mmSDMA0_PAGE_IB_CNTL), 0x800f0100, 0x00000100,
	SOC15_REG_OFFSET(SDMA0, 0, mmSDMA0_PAGE_RB_WPTR_POLL_CNTL), 0x0000fff0, 0x00403000,
	SOC15_REG_OFFSET(SDMA0, 0, mmSDMA0_POWER_CNTL), 0x003ff006, 0x0003c000,
	SOC15_REG_OFFSET(SDMA0, 0, mmSDMA0_RLC0_IB_CNTL), 0x800f0100, 0x00000100,
	SOC15_REG_OFFSET(SDMA0, 0, mmSDMA0_RLC0_RB_WPTR_POLL_CNTL), 0x0000fff0, 0x00403000,
	SOC15_REG_OFFSET(SDMA0, 0, mmSDMA0_RLC1_IB_CNTL), 0x800f0100, 0x00000100,
	SOC15_REG_OFFSET(SDMA0, 0, mmSDMA0_RLC1_RB_WPTR_POLL_CNTL), 0x0000fff0, 0x00403000,
	SOC15_REG_OFFSET(SDMA0, 0, mmSDMA0_UTCL1_PAGE), 0x000003ff, 0x000003c0,
	SOC15_REG_OFFSET(SDMA1, 0, mmSDMA1_CHICKEN_BITS), 0xfe931f07, 0x02831f07,
	SOC15_REG_OFFSET(SDMA1, 0, mmSDMA1_CLK_CTRL), 0xffffffff, 0x3f000100,
	SOC15_REG_OFFSET(SDMA1, 0, mmSDMA1_GFX_IB_CNTL), 0x800f0100, 0x00000100,
	SOC15_REG_OFFSET(SDMA1, 0, mmSDMA1_GFX_RB_WPTR_POLL_CNTL), 0x0000fff0, 0x00403000,
	SOC15_REG_OFFSET(SDMA1, 0, mmSDMA1_PAGE_IB_CNTL), 0x800f0100, 0x00000100,
	SOC15_REG_OFFSET(SDMA1, 0, mmSDMA1_PAGE_RB_WPTR_POLL_CNTL), 0x0000fff0, 0x00403000,
	SOC15_REG_OFFSET(SDMA1, 0, mmSDMA1_POWER_CNTL), 0x003ff000, 0x0003c000,
	SOC15_REG_OFFSET(SDMA1, 0, mmSDMA1_RLC0_IB_CNTL), 0x800f0100, 0x00000100,
	SOC15_REG_OFFSET(SDMA1, 0, mmSDMA1_RLC0_RB_WPTR_POLL_CNTL), 0x0000fff0, 0x00403000,
	SOC15_REG_OFFSET(SDMA1, 0, mmSDMA1_RLC1_IB_CNTL), 0x800f0100, 0x00000100,
	SOC15_REG_OFFSET(SDMA1, 0, mmSDMA1_RLC1_RB_WPTR_POLL_CNTL), 0x0000fff0, 0x00403000,
	SOC15_REG_OFFSET(SDMA1, 0, mmSDMA1_UTCL1_PAGE), 0x000003ff, 0x000003c0
};

static const u32 golden_settings_sdma_vg10[] = {
	SOC15_REG_OFFSET(SDMA0, 0, mmSDMA0_GB_ADDR_CONFIG), 0x0018773f, 0x00104002,
	SOC15_REG_OFFSET(SDMA0, 0, mmSDMA0_GB_ADDR_CONFIG_READ), 0x0018773f, 0x00104002,
	SOC15_REG_OFFSET(SDMA1, 0, mmSDMA1_GB_ADDR_CONFIG), 0x0018773f, 0x00104002,
	SOC15_REG_OFFSET(SDMA1, 0, mmSDMA1_GB_ADDR_CONFIG_READ), 0x0018773f, 0x00104002
};

static const u32 golden_settings_sdma_4_1[] =
{
	SOC15_REG_OFFSET(SDMA0, 0, mmSDMA0_CHICKEN_BITS), 0xfe931f07, 0x02831f07,
	SOC15_REG_OFFSET(SDMA0, 0, mmSDMA0_CLK_CTRL), 0xffffffff, 0x3f000100,
	SOC15_REG_OFFSET(SDMA0, 0, mmSDMA0_GFX_IB_CNTL), 0x800f0111, 0x00000100,
	SOC15_REG_OFFSET(SDMA0, 0, mmSDMA0_GFX_RB_WPTR_POLL_CNTL), 0xfffffff7, 0x00403000,
	SOC15_REG_OFFSET(SDMA0, 0, mmSDMA0_POWER_CNTL), 0xfc3fffff, 0x40000051,
	SOC15_REG_OFFSET(SDMA0, 0, mmSDMA0_RLC0_IB_CNTL), 0x800f0111, 0x00000100,
	SOC15_REG_OFFSET(SDMA0, 0, mmSDMA0_RLC0_RB_WPTR_POLL_CNTL), 0xfffffff7, 0x00403000,
	SOC15_REG_OFFSET(SDMA0, 0, mmSDMA0_RLC1_IB_CNTL), 0x800f0111, 0x00000100,
	SOC15_REG_OFFSET(SDMA0, 0, mmSDMA0_RLC1_RB_WPTR_POLL_CNTL), 0xfffffff7, 0x00403000,
	SOC15_REG_OFFSET(SDMA0, 0, mmSDMA0_UTCL1_PAGE), 0x000003ff, 0x000003c0
};

static const u32 golden_settings_sdma_rv1[] =
{
	SOC15_REG_OFFSET(SDMA0, 0, mmSDMA0_GB_ADDR_CONFIG), 0x0018773f, 0x00000002,
	SOC15_REG_OFFSET(SDMA0, 0, mmSDMA0_GB_ADDR_CONFIG_READ), 0x0018773f, 0x00000002
};

static u32 sdma_v4_0_get_reg_offset(u32 instance, u32 internal_offset)
{
	u32 base = 0;

	switch (instance) {
	case 0:
		base = SDMA0_BASE.instance[0].segment[0];
		break;
	case 1:
		base = SDMA1_BASE.instance[0].segment[0];
		break;
	default:
		BUG();
		break;
	}

	return base + internal_offset;
}

static void sdma_v4_0_init_golden_registers(struct amdgpu_device *adev)
{
	switch (adev->asic_type) {
	case CHIP_VEGA10:
		amdgpu_program_register_sequence(adev,
						 golden_settings_sdma_4,
						 (const u32)ARRAY_SIZE(golden_settings_sdma_4));
		amdgpu_program_register_sequence(adev,
						 golden_settings_sdma_vg10,
						 (const u32)ARRAY_SIZE(golden_settings_sdma_vg10));
		break;
	case CHIP_RAVEN:
		amdgpu_program_register_sequence(adev,
						 golden_settings_sdma_4_1,
						 (const u32)ARRAY_SIZE(golden_settings_sdma_4_1));
		amdgpu_program_register_sequence(adev,
						 golden_settings_sdma_rv1,
						 (const u32)ARRAY_SIZE(golden_settings_sdma_rv1));
		break;
	default:
		break;
	}
}

/**
 * sdma_v4_0_init_microcode - load ucode images from disk
 *
 * @adev: amdgpu_device pointer
 *
 * Use the firmware interface to load the ucode images into
 * the driver (not loaded into hw).
 * Returns 0 on success, error on failure.
 */

// emulation only, won't work on real chip
// vega10 real chip need to use PSP to load firmware
static int sdma_v4_0_init_microcode(struct amdgpu_device *adev)
{
	const char *chip_name;
	char fw_name[30];
	int err = 0, i;
	struct amdgpu_firmware_info *info = NULL;
	const struct common_firmware_header *header = NULL;
	const struct sdma_firmware_header_v1_0 *hdr;

	DRM_DEBUG("\n");

	switch (adev->asic_type) {
	case CHIP_VEGA10:
		chip_name = "vega10";
		break;
	case CHIP_RAVEN:
		chip_name = "raven";
		break;
	default:
		BUG();
	}

	for (i = 0; i < adev->sdma.num_instances; i++) {
		if (i == 0)
			snprintf(fw_name, sizeof(fw_name), "amdgpu/%s_sdma.bin", chip_name);
		else
			snprintf(fw_name, sizeof(fw_name), "amdgpu/%s_sdma1.bin", chip_name);
		err = request_firmware(&adev->sdma.instance[i].fw, fw_name, adev->dev);
		if (err)
			goto out;
		err = amdgpu_ucode_validate(adev->sdma.instance[i].fw);
		if (err)
			goto out;
		hdr = (const struct sdma_firmware_header_v1_0 *)adev->sdma.instance[i].fw->data;
		adev->sdma.instance[i].fw_version = le32_to_cpu(hdr->header.ucode_version);
		adev->sdma.instance[i].feature_version = le32_to_cpu(hdr->ucode_feature_version);
		if (adev->sdma.instance[i].feature_version >= 20)
			adev->sdma.instance[i].burst_nop = true;
		DRM_DEBUG("psp_load == '%s'\n",
				adev->firmware.load_type == AMDGPU_FW_LOAD_PSP ? "true" : "false");

		if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) {
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
		DRM_ERROR("sdma_v4_0: Failed to load firmware \"%s\"\n", fw_name);
		for (i = 0; i < adev->sdma.num_instances; i++) {
			release_firmware(adev->sdma.instance[i].fw);
			adev->sdma.instance[i].fw = NULL;
		}
	}
	return err;
}

/**
 * sdma_v4_0_ring_get_rptr - get the current read pointer
 *
 * @ring: amdgpu ring pointer
 *
 * Get the current rptr from the hardware (VEGA10+).
 */
static uint64_t sdma_v4_0_ring_get_rptr(struct amdgpu_ring *ring)
{
	u64 *rptr;

	/* XXX check if swapping is necessary on BE */
	rptr = ((u64 *)&ring->adev->wb.wb[ring->rptr_offs]);

	DRM_DEBUG("rptr before shift == 0x%016llx\n", *rptr);
	return ((*rptr) >> 2);
}

/**
 * sdma_v4_0_ring_get_wptr - get the current write pointer
 *
 * @ring: amdgpu ring pointer
 *
 * Get the current wptr from the hardware (VEGA10+).
 */
static uint64_t sdma_v4_0_ring_get_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	u64 *wptr = NULL;
	uint64_t local_wptr = 0;

	if (ring->use_doorbell) {
		/* XXX check if swapping is necessary on BE */
		wptr = ((u64 *)&adev->wb.wb[ring->wptr_offs]);
		DRM_DEBUG("wptr/doorbell before shift == 0x%016llx\n", *wptr);
		*wptr = (*wptr) >> 2;
		DRM_DEBUG("wptr/doorbell after shift == 0x%016llx\n", *wptr);
	} else {
		u32 lowbit, highbit;
		int me = (ring == &adev->sdma.instance[0].ring) ? 0 : 1;

		wptr = &local_wptr;
		lowbit = RREG32(sdma_v4_0_get_reg_offset(me, mmSDMA0_GFX_RB_WPTR)) >> 2;
		highbit = RREG32(sdma_v4_0_get_reg_offset(me, mmSDMA0_GFX_RB_WPTR_HI)) >> 2;

		DRM_DEBUG("wptr [%i]high== 0x%08x low==0x%08x\n",
				me, highbit, lowbit);
		*wptr = highbit;
		*wptr = (*wptr) << 32;
		*wptr |= lowbit;
	}

	return *wptr;
}

/**
 * sdma_v4_0_ring_set_wptr - commit the write pointer
 *
 * @ring: amdgpu ring pointer
 *
 * Write the wptr back to the hardware (VEGA10+).
 */
static void sdma_v4_0_ring_set_wptr(struct amdgpu_ring *ring)
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
		int me = (ring == &ring->adev->sdma.instance[0].ring) ? 0 : 1;

		DRM_DEBUG("Not using doorbell -- "
				"mmSDMA%i_GFX_RB_WPTR == 0x%08x "
				"mmSDMA%i_GFX_RB_WPTR_HI == 0x%08x\n",
				me,
				lower_32_bits(ring->wptr << 2),
				me,
				upper_32_bits(ring->wptr << 2));
		WREG32(sdma_v4_0_get_reg_offset(me, mmSDMA0_GFX_RB_WPTR), lower_32_bits(ring->wptr << 2));
		WREG32(sdma_v4_0_get_reg_offset(me, mmSDMA0_GFX_RB_WPTR_HI), upper_32_bits(ring->wptr << 2));
	}
}

static void sdma_v4_0_ring_insert_nop(struct amdgpu_ring *ring, uint32_t count)
{
	struct amdgpu_sdma_instance *sdma = amdgpu_get_sdma_instance(ring);
	int i;

	for (i = 0; i < count; i++)
		if (sdma && sdma->burst_nop && (i == 0))
			amdgpu_ring_write(ring, ring->funcs->nop |
				SDMA_PKT_NOP_HEADER_COUNT(count - 1));
		else
			amdgpu_ring_write(ring, ring->funcs->nop);
}

/**
 * sdma_v4_0_ring_emit_ib - Schedule an IB on the DMA engine
 *
 * @ring: amdgpu ring pointer
 * @ib: IB object to schedule
 *
 * Schedule an IB in the DMA ring (VEGA10).
 */
static void sdma_v4_0_ring_emit_ib(struct amdgpu_ring *ring,
					struct amdgpu_ib *ib,
					unsigned vm_id, bool ctx_switch)
{
	u32 vmid = vm_id & 0xf;

	/* IB packet must end on a 8 DW boundary */
	sdma_v4_0_ring_insert_nop(ring, (10 - (lower_32_bits(ring->wptr) & 7)) % 8);

	amdgpu_ring_write(ring, SDMA_PKT_HEADER_OP(SDMA_OP_INDIRECT) |
			  SDMA_PKT_INDIRECT_HEADER_VMID(vmid));
	/* base must be 32 byte aligned */
	amdgpu_ring_write(ring, lower_32_bits(ib->gpu_addr) & 0xffffffe0);
	amdgpu_ring_write(ring, upper_32_bits(ib->gpu_addr));
	amdgpu_ring_write(ring, ib->length_dw);
	amdgpu_ring_write(ring, 0);
	amdgpu_ring_write(ring, 0);

}

/**
 * sdma_v4_0_ring_emit_hdp_flush - emit an hdp flush on the DMA ring
 *
 * @ring: amdgpu ring pointer
 *
 * Emit an hdp flush packet on the requested DMA ring.
 */
static void sdma_v4_0_ring_emit_hdp_flush(struct amdgpu_ring *ring)
{
	u32 ref_and_mask = 0;
	struct nbio_hdp_flush_reg *nbio_hf_reg;

	if (ring->adev->flags & AMD_IS_APU)
		nbio_hf_reg = &nbio_v7_0_hdp_flush_reg;
	else
		nbio_hf_reg = &nbio_v6_1_hdp_flush_reg;

	if (ring == &ring->adev->sdma.instance[0].ring)
		ref_and_mask = nbio_hf_reg->ref_and_mask_sdma0;
	else
		ref_and_mask = nbio_hf_reg->ref_and_mask_sdma1;

	amdgpu_ring_write(ring, SDMA_PKT_HEADER_OP(SDMA_OP_POLL_REGMEM) |
			  SDMA_PKT_POLL_REGMEM_HEADER_HDP_FLUSH(1) |
			  SDMA_PKT_POLL_REGMEM_HEADER_FUNC(3)); /* == */
	amdgpu_ring_write(ring, nbio_hf_reg->hdp_flush_done_offset << 2);
	amdgpu_ring_write(ring, nbio_hf_reg->hdp_flush_req_offset << 2);
	amdgpu_ring_write(ring, ref_and_mask); /* reference */
	amdgpu_ring_write(ring, ref_and_mask); /* mask */
	amdgpu_ring_write(ring, SDMA_PKT_POLL_REGMEM_DW5_RETRY_COUNT(0xfff) |
			  SDMA_PKT_POLL_REGMEM_DW5_INTERVAL(10)); /* retry count, poll interval */
}

static void sdma_v4_0_ring_emit_hdp_invalidate(struct amdgpu_ring *ring)
{
	amdgpu_ring_write(ring, SDMA_PKT_HEADER_OP(SDMA_OP_SRBM_WRITE) |
			  SDMA_PKT_SRBM_WRITE_HEADER_BYTE_EN(0xf));
	amdgpu_ring_write(ring, SOC15_REG_OFFSET(HDP, 0, mmHDP_DEBUG0));
	amdgpu_ring_write(ring, 1);
}

/**
 * sdma_v4_0_ring_emit_fence - emit a fence on the DMA ring
 *
 * @ring: amdgpu ring pointer
 * @fence: amdgpu fence object
 *
 * Add a DMA fence packet to the ring to write
 * the fence seq number and DMA trap packet to generate
 * an interrupt if needed (VEGA10).
 */
static void sdma_v4_0_ring_emit_fence(struct amdgpu_ring *ring, u64 addr, u64 seq,
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
 * sdma_v4_0_gfx_stop - stop the gfx async dma engines
 *
 * @adev: amdgpu_device pointer
 *
 * Stop the gfx async dma ring buffers (VEGA10).
 */
static void sdma_v4_0_gfx_stop(struct amdgpu_device *adev)
{
	struct amdgpu_ring *sdma0 = &adev->sdma.instance[0].ring;
	struct amdgpu_ring *sdma1 = &adev->sdma.instance[1].ring;
	u32 rb_cntl, ib_cntl;
	int i;

	if ((adev->mman.buffer_funcs_ring == sdma0) ||
	    (adev->mman.buffer_funcs_ring == sdma1))
		amdgpu_ttm_set_active_vram_size(adev, adev->mc.visible_vram_size);

	for (i = 0; i < adev->sdma.num_instances; i++) {
		rb_cntl = RREG32(sdma_v4_0_get_reg_offset(i, mmSDMA0_GFX_RB_CNTL));
		rb_cntl = REG_SET_FIELD(rb_cntl, SDMA0_GFX_RB_CNTL, RB_ENABLE, 0);
		WREG32(sdma_v4_0_get_reg_offset(i, mmSDMA0_GFX_RB_CNTL), rb_cntl);
		ib_cntl = RREG32(sdma_v4_0_get_reg_offset(i, mmSDMA0_GFX_IB_CNTL));
		ib_cntl = REG_SET_FIELD(ib_cntl, SDMA0_GFX_IB_CNTL, IB_ENABLE, 0);
		WREG32(sdma_v4_0_get_reg_offset(i, mmSDMA0_GFX_IB_CNTL), ib_cntl);
	}

	sdma0->ready = false;
	sdma1->ready = false;
}

/**
 * sdma_v4_0_rlc_stop - stop the compute async dma engines
 *
 * @adev: amdgpu_device pointer
 *
 * Stop the compute async dma queues (VEGA10).
 */
static void sdma_v4_0_rlc_stop(struct amdgpu_device *adev)
{
	/* XXX todo */
}

/**
 * sdma_v_0_ctx_switch_enable - stop the async dma engines context switch
 *
 * @adev: amdgpu_device pointer
 * @enable: enable/disable the DMA MEs context switch.
 *
 * Halt or unhalt the async dma engines context switch (VEGA10).
 */
static void sdma_v4_0_ctx_switch_enable(struct amdgpu_device *adev, bool enable)
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
		f32_cntl = RREG32(sdma_v4_0_get_reg_offset(i, mmSDMA0_CNTL));
		f32_cntl = REG_SET_FIELD(f32_cntl, SDMA0_CNTL,
				AUTO_CTXSW_ENABLE, enable ? 1 : 0);
		if (enable && amdgpu_sdma_phase_quantum) {
			WREG32(sdma_v4_0_get_reg_offset(i, mmSDMA0_PHASE0_QUANTUM),
			       phase_quantum);
			WREG32(sdma_v4_0_get_reg_offset(i, mmSDMA0_PHASE1_QUANTUM),
			       phase_quantum);
			WREG32(sdma_v4_0_get_reg_offset(i, mmSDMA0_PHASE2_QUANTUM),
			       phase_quantum);
		}
		WREG32(sdma_v4_0_get_reg_offset(i, mmSDMA0_CNTL), f32_cntl);
	}

}

/**
 * sdma_v4_0_enable - stop the async dma engines
 *
 * @adev: amdgpu_device pointer
 * @enable: enable/disable the DMA MEs.
 *
 * Halt or unhalt the async dma engines (VEGA10).
 */
static void sdma_v4_0_enable(struct amdgpu_device *adev, bool enable)
{
	u32 f32_cntl;
	int i;

	if (enable == false) {
		sdma_v4_0_gfx_stop(adev);
		sdma_v4_0_rlc_stop(adev);
	}

	for (i = 0; i < adev->sdma.num_instances; i++) {
		f32_cntl = RREG32(sdma_v4_0_get_reg_offset(i, mmSDMA0_F32_CNTL));
		f32_cntl = REG_SET_FIELD(f32_cntl, SDMA0_F32_CNTL, HALT, enable ? 0 : 1);
		WREG32(sdma_v4_0_get_reg_offset(i, mmSDMA0_F32_CNTL), f32_cntl);
	}
}

/**
 * sdma_v4_0_gfx_resume - setup and start the async dma engines
 *
 * @adev: amdgpu_device pointer
 *
 * Set up the gfx DMA ring buffers and enable them (VEGA10).
 * Returns 0 for success, error for failure.
 */
static int sdma_v4_0_gfx_resume(struct amdgpu_device *adev)
{
	struct amdgpu_ring *ring;
	u32 rb_cntl, ib_cntl;
	u32 rb_bufsz;
	u32 wb_offset;
	u32 doorbell;
	u32 doorbell_offset;
	u32 temp;
	int i, r;

	for (i = 0; i < adev->sdma.num_instances; i++) {
		ring = &adev->sdma.instance[i].ring;
		wb_offset = (ring->rptr_offs * 4);

		WREG32(sdma_v4_0_get_reg_offset(i, mmSDMA0_SEM_WAIT_FAIL_TIMER_CNTL), 0);

		/* Set ring buffer size in dwords */
		rb_bufsz = order_base_2(ring->ring_size / 4);
		rb_cntl = RREG32(sdma_v4_0_get_reg_offset(i, mmSDMA0_GFX_RB_CNTL));
		rb_cntl = REG_SET_FIELD(rb_cntl, SDMA0_GFX_RB_CNTL, RB_SIZE, rb_bufsz);
#ifdef __BIG_ENDIAN
		rb_cntl = REG_SET_FIELD(rb_cntl, SDMA0_GFX_RB_CNTL, RB_SWAP_ENABLE, 1);
		rb_cntl = REG_SET_FIELD(rb_cntl, SDMA0_GFX_RB_CNTL,
					RPTR_WRITEBACK_SWAP_ENABLE, 1);
#endif
		WREG32(sdma_v4_0_get_reg_offset(i, mmSDMA0_GFX_RB_CNTL), rb_cntl);

		/* Initialize the ring buffer's read and write pointers */
		WREG32(sdma_v4_0_get_reg_offset(i, mmSDMA0_GFX_RB_RPTR), 0);
		WREG32(sdma_v4_0_get_reg_offset(i, mmSDMA0_GFX_RB_RPTR_HI), 0);
		WREG32(sdma_v4_0_get_reg_offset(i, mmSDMA0_GFX_RB_WPTR), 0);
		WREG32(sdma_v4_0_get_reg_offset(i, mmSDMA0_GFX_RB_WPTR_HI), 0);

		/* set the wb address whether it's enabled or not */
		WREG32(sdma_v4_0_get_reg_offset(i, mmSDMA0_GFX_RB_RPTR_ADDR_HI),
		       upper_32_bits(adev->wb.gpu_addr + wb_offset) & 0xFFFFFFFF);
		WREG32(sdma_v4_0_get_reg_offset(i, mmSDMA0_GFX_RB_RPTR_ADDR_LO),
		       lower_32_bits(adev->wb.gpu_addr + wb_offset) & 0xFFFFFFFC);

		rb_cntl = REG_SET_FIELD(rb_cntl, SDMA0_GFX_RB_CNTL, RPTR_WRITEBACK_ENABLE, 1);

		WREG32(sdma_v4_0_get_reg_offset(i, mmSDMA0_GFX_RB_BASE), ring->gpu_addr >> 8);
		WREG32(sdma_v4_0_get_reg_offset(i, mmSDMA0_GFX_RB_BASE_HI), ring->gpu_addr >> 40);

		ring->wptr = 0;

		/* before programing wptr to a less value, need set minor_ptr_update first */
		WREG32(sdma_v4_0_get_reg_offset(i, mmSDMA0_GFX_MINOR_PTR_UPDATE), 1);

		if (!amdgpu_sriov_vf(adev)) { /* only bare-metal use register write for wptr */
			WREG32(sdma_v4_0_get_reg_offset(i, mmSDMA0_GFX_RB_WPTR), lower_32_bits(ring->wptr) << 2);
			WREG32(sdma_v4_0_get_reg_offset(i, mmSDMA0_GFX_RB_WPTR_HI), upper_32_bits(ring->wptr) << 2);
		}

		doorbell = RREG32(sdma_v4_0_get_reg_offset(i, mmSDMA0_GFX_DOORBELL));
		doorbell_offset = RREG32(sdma_v4_0_get_reg_offset(i, mmSDMA0_GFX_DOORBELL_OFFSET));

		if (ring->use_doorbell) {
			doorbell = REG_SET_FIELD(doorbell, SDMA0_GFX_DOORBELL, ENABLE, 1);
			doorbell_offset = REG_SET_FIELD(doorbell_offset, SDMA0_GFX_DOORBELL_OFFSET,
					OFFSET, ring->doorbell_index);
		} else {
			doorbell = REG_SET_FIELD(doorbell, SDMA0_GFX_DOORBELL, ENABLE, 0);
		}
		WREG32(sdma_v4_0_get_reg_offset(i, mmSDMA0_GFX_DOORBELL), doorbell);
		WREG32(sdma_v4_0_get_reg_offset(i, mmSDMA0_GFX_DOORBELL_OFFSET), doorbell_offset);
		if (adev->flags & AMD_IS_APU)
			nbio_v7_0_sdma_doorbell_range(adev, i, ring->use_doorbell, ring->doorbell_index);
		else
			nbio_v6_1_sdma_doorbell_range(adev, i, ring->use_doorbell, ring->doorbell_index);

		if (amdgpu_sriov_vf(adev))
			sdma_v4_0_ring_set_wptr(ring);

		/* set minor_ptr_update to 0 after wptr programed */
		WREG32(sdma_v4_0_get_reg_offset(i, mmSDMA0_GFX_MINOR_PTR_UPDATE), 0);

		/* set utc l1 enable flag always to 1 */
		temp = RREG32(sdma_v4_0_get_reg_offset(i, mmSDMA0_CNTL));
		temp = REG_SET_FIELD(temp, SDMA0_CNTL, UTC_L1_ENABLE, 1);
		WREG32(sdma_v4_0_get_reg_offset(i, mmSDMA0_CNTL), temp);

		if (!amdgpu_sriov_vf(adev)) {
			/* unhalt engine */
			temp = RREG32(sdma_v4_0_get_reg_offset(i, mmSDMA0_F32_CNTL));
			temp = REG_SET_FIELD(temp, SDMA0_F32_CNTL, HALT, 0);
			WREG32(sdma_v4_0_get_reg_offset(i, mmSDMA0_F32_CNTL), temp);
		}

		/* enable DMA RB */
		rb_cntl = REG_SET_FIELD(rb_cntl, SDMA0_GFX_RB_CNTL, RB_ENABLE, 1);
		WREG32(sdma_v4_0_get_reg_offset(i, mmSDMA0_GFX_RB_CNTL), rb_cntl);

		ib_cntl = RREG32(sdma_v4_0_get_reg_offset(i, mmSDMA0_GFX_IB_CNTL));
		ib_cntl = REG_SET_FIELD(ib_cntl, SDMA0_GFX_IB_CNTL, IB_ENABLE, 1);
#ifdef __BIG_ENDIAN
		ib_cntl = REG_SET_FIELD(ib_cntl, SDMA0_GFX_IB_CNTL, IB_SWAP_ENABLE, 1);
#endif
		/* enable DMA IBs */
		WREG32(sdma_v4_0_get_reg_offset(i, mmSDMA0_GFX_IB_CNTL), ib_cntl);

		ring->ready = true;

		if (amdgpu_sriov_vf(adev)) { /* bare-metal sequence doesn't need below to lines */
			sdma_v4_0_ctx_switch_enable(adev, true);
			sdma_v4_0_enable(adev, true);
		}

		r = amdgpu_ring_test_ring(ring);
		if (r) {
			ring->ready = false;
			return r;
		}

		if (adev->mman.buffer_funcs_ring == ring)
			amdgpu_ttm_set_active_vram_size(adev, adev->mc.real_vram_size);
	}

	return 0;
}

static void
sdma_v4_1_update_power_gating(struct amdgpu_device *adev, bool enable)
{
	uint32_t def, data;

	if (enable && (adev->pg_flags & AMD_PG_SUPPORT_SDMA)) {
		/* disable idle interrupt */
		def = data = RREG32(SOC15_REG_OFFSET(SDMA0, 0, mmSDMA0_CNTL));
		data |= SDMA0_CNTL__CTXEMPTY_INT_ENABLE_MASK;

		if (data != def)
			WREG32(SOC15_REG_OFFSET(SDMA0, 0, mmSDMA0_CNTL), data);
	} else {
		/* disable idle interrupt */
		def = data = RREG32(SOC15_REG_OFFSET(SDMA0, 0, mmSDMA0_CNTL));
		data &= ~SDMA0_CNTL__CTXEMPTY_INT_ENABLE_MASK;
		if (data != def)
			WREG32(SOC15_REG_OFFSET(SDMA0, 0, mmSDMA0_CNTL), data);
	}
}

static void sdma_v4_1_init_power_gating(struct amdgpu_device *adev)
{
	uint32_t def, data;

	/* Enable HW based PG. */
	def = data = RREG32(SOC15_REG_OFFSET(SDMA0, 0, mmSDMA0_POWER_CNTL));
	data |= SDMA0_POWER_CNTL__PG_CNTL_ENABLE_MASK;
	if (data != def)
		WREG32(SOC15_REG_OFFSET(SDMA0, 0, mmSDMA0_POWER_CNTL), data);

	/* enable interrupt */
	def = data = RREG32(SOC15_REG_OFFSET(SDMA0, 0, mmSDMA0_CNTL));
	data |= SDMA0_CNTL__CTXEMPTY_INT_ENABLE_MASK;
	if (data != def)
		WREG32(SOC15_REG_OFFSET(SDMA0, 0, mmSDMA0_CNTL), data);

	/* Configure hold time to filter in-valid power on/off request. Use default right now */
	def = data = RREG32(SOC15_REG_OFFSET(SDMA0, 0, mmSDMA0_POWER_CNTL));
	data &= ~SDMA0_POWER_CNTL__ON_OFF_CONDITION_HOLD_TIME_MASK;
	data |= (mmSDMA0_POWER_CNTL_DEFAULT & SDMA0_POWER_CNTL__ON_OFF_CONDITION_HOLD_TIME_MASK);
	/* Configure switch time for hysteresis purpose. Use default right now */
	data &= ~SDMA0_POWER_CNTL__ON_OFF_STATUS_DURATION_TIME_MASK;
	data |= (mmSDMA0_POWER_CNTL_DEFAULT & SDMA0_POWER_CNTL__ON_OFF_STATUS_DURATION_TIME_MASK);
	if(data != def)
		WREG32(SOC15_REG_OFFSET(SDMA0, 0, mmSDMA0_POWER_CNTL), data);
}

static void sdma_v4_0_init_pg(struct amdgpu_device *adev)
{
	if (!(adev->pg_flags & AMD_PG_SUPPORT_SDMA))
		return;

	switch (adev->asic_type) {
	case CHIP_RAVEN:
		sdma_v4_1_init_power_gating(adev);
		sdma_v4_1_update_power_gating(adev, true);
		break;
	default:
		break;
	}
}

/**
 * sdma_v4_0_rlc_resume - setup and start the async dma engines
 *
 * @adev: amdgpu_device pointer
 *
 * Set up the compute DMA queues and enable them (VEGA10).
 * Returns 0 for success, error for failure.
 */
static int sdma_v4_0_rlc_resume(struct amdgpu_device *adev)
{
	sdma_v4_0_init_pg(adev);

	return 0;
}

/**
 * sdma_v4_0_load_microcode - load the sDMA ME ucode
 *
 * @adev: amdgpu_device pointer
 *
 * Loads the sDMA0/1 ucode.
 * Returns 0 for success, -EINVAL if the ucode is not available.
 */
static int sdma_v4_0_load_microcode(struct amdgpu_device *adev)
{
	const struct sdma_firmware_header_v1_0 *hdr;
	const __le32 *fw_data;
	u32 fw_size;
	u32 digest_size = 0;
	int i, j;

	/* halt the MEs */
	sdma_v4_0_enable(adev, false);

	for (i = 0; i < adev->sdma.num_instances; i++) {
		uint16_t version_major;
		uint16_t version_minor;
		if (!adev->sdma.instance[i].fw)
			return -EINVAL;

		hdr = (const struct sdma_firmware_header_v1_0 *)adev->sdma.instance[i].fw->data;
		amdgpu_ucode_print_sdma_hdr(&hdr->header);
		fw_size = le32_to_cpu(hdr->header.ucode_size_bytes) / 4;

		version_major = le16_to_cpu(hdr->header.header_version_major);
		version_minor = le16_to_cpu(hdr->header.header_version_minor);

		if (version_major == 1 && version_minor >= 1) {
			const struct sdma_firmware_header_v1_1 *sdma_v1_1_hdr = (const struct sdma_firmware_header_v1_1 *) hdr;
			digest_size = le32_to_cpu(sdma_v1_1_hdr->digest_size);
		}

		fw_size -= digest_size;

		fw_data = (const __le32 *)
			(adev->sdma.instance[i].fw->data +
				le32_to_cpu(hdr->header.ucode_array_offset_bytes));

		WREG32(sdma_v4_0_get_reg_offset(i, mmSDMA0_UCODE_ADDR), 0);


		for (j = 0; j < fw_size; j++)
			WREG32(sdma_v4_0_get_reg_offset(i, mmSDMA0_UCODE_DATA), le32_to_cpup(fw_data++));

		WREG32(sdma_v4_0_get_reg_offset(i, mmSDMA0_UCODE_ADDR), adev->sdma.instance[i].fw_version);
	}

	return 0;
}

/**
 * sdma_v4_0_start - setup and start the async dma engines
 *
 * @adev: amdgpu_device pointer
 *
 * Set up the DMA engines and enable them (VEGA10).
 * Returns 0 for success, error for failure.
 */
static int sdma_v4_0_start(struct amdgpu_device *adev)
{
	int r = 0;

	if (amdgpu_sriov_vf(adev)) {
		sdma_v4_0_ctx_switch_enable(adev, false);
		sdma_v4_0_enable(adev, false);

		/* set RB registers */
		r = sdma_v4_0_gfx_resume(adev);
		return r;
	}

	if (adev->firmware.load_type != AMDGPU_FW_LOAD_PSP) {
		r = sdma_v4_0_load_microcode(adev);
		if (r)
			return r;
	}

	/* unhalt the MEs */
	sdma_v4_0_enable(adev, true);
	/* enable sdma ring preemption */
	sdma_v4_0_ctx_switch_enable(adev, true);

	/* start the gfx rings and rlc compute queues */
	r = sdma_v4_0_gfx_resume(adev);
	if (r)
		return r;
	r = sdma_v4_0_rlc_resume(adev);

	return r;
}

/**
 * sdma_v4_0_ring_test_ring - simple async dma engine test
 *
 * @ring: amdgpu_ring structure holding ring information
 *
 * Test the DMA engine by writing using it to write an
 * value to memory. (VEGA10).
 * Returns 0 for success, error for failure.
 */
static int sdma_v4_0_ring_test_ring(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	unsigned i;
	unsigned index;
	int r;
	u32 tmp;
	u64 gpu_addr;

	r = amdgpu_wb_get(adev, &index);
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
		amdgpu_wb_free(adev, index);
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
		DRM_UDELAY(1);
	}

	if (i < adev->usec_timeout) {
		DRM_INFO("ring test on %d succeeded in %d usecs\n", ring->idx, i);
	} else {
		DRM_ERROR("amdgpu: ring %d test failed (0x%08X)\n",
			  ring->idx, tmp);
		r = -EINVAL;
	}
	amdgpu_wb_free(adev, index);

	return r;
}

/**
 * sdma_v4_0_ring_test_ib - test an IB on the DMA engine
 *
 * @ring: amdgpu_ring structure holding ring information
 *
 * Test a simple IB in the DMA ring (VEGA10).
 * Returns 0 on success, error on failure.
 */
static int sdma_v4_0_ring_test_ib(struct amdgpu_ring *ring, long timeout)
{
	struct amdgpu_device *adev = ring->adev;
	struct amdgpu_ib ib;
	struct dma_fence *f = NULL;
	unsigned index;
	long r;
	u32 tmp = 0;
	u64 gpu_addr;

	r = amdgpu_wb_get(adev, &index);
	if (r) {
		dev_err(adev->dev, "(%ld) failed to allocate wb slot\n", r);
		return r;
	}

	gpu_addr = adev->wb.gpu_addr + (index * 4);
	tmp = 0xCAFEDEAD;
	adev->wb.wb[index] = cpu_to_le32(tmp);
	memset(&ib, 0, sizeof(ib));
	r = amdgpu_ib_get(adev, NULL, 256, &ib);
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
	if (tmp == 0xDEADBEEF) {
		DRM_INFO("ib test on ring %d succeeded\n", ring->idx);
		r = 0;
	} else {
		DRM_ERROR("amdgpu: ib test failed (0x%08X)\n", tmp);
		r = -EINVAL;
	}
err1:
	amdgpu_ib_free(adev, &ib, NULL);
	dma_fence_put(f);
err0:
	amdgpu_wb_free(adev, index);
	return r;
}


/**
 * sdma_v4_0_vm_copy_pte - update PTEs by copying them from the GART
 *
 * @ib: indirect buffer to fill with commands
 * @pe: addr of the page entry
 * @src: src addr to copy from
 * @count: number of page entries to update
 *
 * Update PTEs by copying them from the GART using sDMA (VEGA10).
 */
static void sdma_v4_0_vm_copy_pte(struct amdgpu_ib *ib,
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
 * sdma_v4_0_vm_write_pte - update PTEs by writing them manually
 *
 * @ib: indirect buffer to fill with commands
 * @pe: addr of the page entry
 * @addr: dst addr to write into pe
 * @count: number of page entries to update
 * @incr: increase next addr by incr bytes
 * @flags: access flags
 *
 * Update PTEs by writing them manually using sDMA (VEGA10).
 */
static void sdma_v4_0_vm_write_pte(struct amdgpu_ib *ib, uint64_t pe,
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
 * sdma_v4_0_vm_set_pte_pde - update the page tables using sDMA
 *
 * @ib: indirect buffer to fill with commands
 * @pe: addr of the page entry
 * @addr: dst addr to write into pe
 * @count: number of page entries to update
 * @incr: increase next addr by incr bytes
 * @flags: access flags
 *
 * Update the page tables using sDMA (VEGA10).
 */
static void sdma_v4_0_vm_set_pte_pde(struct amdgpu_ib *ib,
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
 * sdma_v4_0_ring_pad_ib - pad the IB to the required number of dw
 *
 * @ib: indirect buffer to fill with padding
 *
 */
static void sdma_v4_0_ring_pad_ib(struct amdgpu_ring *ring, struct amdgpu_ib *ib)
{
	struct amdgpu_sdma_instance *sdma = amdgpu_get_sdma_instance(ring);
	u32 pad_count;
	int i;

	pad_count = (8 - (ib->length_dw & 0x7)) % 8;
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
 * sdma_v4_0_ring_emit_pipeline_sync - sync the pipeline
 *
 * @ring: amdgpu_ring pointer
 *
 * Make sure all previous operations are completed (CIK).
 */
static void sdma_v4_0_ring_emit_pipeline_sync(struct amdgpu_ring *ring)
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
	amdgpu_ring_write(ring, 0xfffffff); /* mask */
	amdgpu_ring_write(ring, SDMA_PKT_POLL_REGMEM_DW5_RETRY_COUNT(0xfff) |
			  SDMA_PKT_POLL_REGMEM_DW5_INTERVAL(4)); /* retry count, poll interval */
}


/**
 * sdma_v4_0_ring_emit_vm_flush - vm flush using sDMA
 *
 * @ring: amdgpu_ring pointer
 * @vm: amdgpu_vm pointer
 *
 * Update the page table base and flush the VM TLB
 * using sDMA (VEGA10).
 */
static void sdma_v4_0_ring_emit_vm_flush(struct amdgpu_ring *ring,
					 unsigned vm_id, uint64_t pd_addr)
{
	struct amdgpu_vmhub *hub = &ring->adev->vmhub[ring->funcs->vmhub];
	uint32_t req = ring->adev->gart.gart_funcs->get_invalidate_req(vm_id);
	unsigned eng = ring->vm_inv_eng;

	pd_addr = amdgpu_gart_get_vm_pde(ring->adev, pd_addr);
	pd_addr |= AMDGPU_PTE_VALID;

	amdgpu_ring_write(ring, SDMA_PKT_HEADER_OP(SDMA_OP_SRBM_WRITE) |
			  SDMA_PKT_SRBM_WRITE_HEADER_BYTE_EN(0xf));
	amdgpu_ring_write(ring, hub->ctx0_ptb_addr_lo32 + vm_id * 2);
	amdgpu_ring_write(ring, lower_32_bits(pd_addr));

	amdgpu_ring_write(ring, SDMA_PKT_HEADER_OP(SDMA_OP_SRBM_WRITE) |
			  SDMA_PKT_SRBM_WRITE_HEADER_BYTE_EN(0xf));
	amdgpu_ring_write(ring, hub->ctx0_ptb_addr_hi32 + vm_id * 2);
	amdgpu_ring_write(ring, upper_32_bits(pd_addr));

	/* flush TLB */
	amdgpu_ring_write(ring, SDMA_PKT_HEADER_OP(SDMA_OP_SRBM_WRITE) |
			  SDMA_PKT_SRBM_WRITE_HEADER_BYTE_EN(0xf));
	amdgpu_ring_write(ring, hub->vm_inv_eng0_req + eng);
	amdgpu_ring_write(ring, req);

	/* wait for flush */
	amdgpu_ring_write(ring, SDMA_PKT_HEADER_OP(SDMA_OP_POLL_REGMEM) |
			  SDMA_PKT_POLL_REGMEM_HEADER_HDP_FLUSH(0) |
			  SDMA_PKT_POLL_REGMEM_HEADER_FUNC(3)); /* equal */
	amdgpu_ring_write(ring, (hub->vm_inv_eng0_ack + eng) << 2);
	amdgpu_ring_write(ring, 0);
	amdgpu_ring_write(ring, 1 << vm_id); /* reference */
	amdgpu_ring_write(ring, 1 << vm_id); /* mask */
	amdgpu_ring_write(ring, SDMA_PKT_POLL_REGMEM_DW5_RETRY_COUNT(0xfff) |
			  SDMA_PKT_POLL_REGMEM_DW5_INTERVAL(10));
}

static int sdma_v4_0_early_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (adev->asic_type == CHIP_RAVEN)
		adev->sdma.num_instances = 1;
	else
		adev->sdma.num_instances = 2;

	sdma_v4_0_set_ring_funcs(adev);
	sdma_v4_0_set_buffer_funcs(adev);
	sdma_v4_0_set_vm_pte_funcs(adev);
	sdma_v4_0_set_irq_funcs(adev);

	return 0;
}


static int sdma_v4_0_sw_init(void *handle)
{
	struct amdgpu_ring *ring;
	int r, i;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	/* SDMA trap event */
	r = amdgpu_irq_add_id(adev, AMDGPU_IH_CLIENTID_SDMA0, 224,
			      &adev->sdma.trap_irq);
	if (r)
		return r;

	/* SDMA trap event */
	r = amdgpu_irq_add_id(adev, AMDGPU_IH_CLIENTID_SDMA1, 224,
			      &adev->sdma.trap_irq);
	if (r)
		return r;

	r = sdma_v4_0_init_microcode(adev);
	if (r) {
		DRM_ERROR("Failed to load sdma firmware!\n");
		return r;
	}

	for (i = 0; i < adev->sdma.num_instances; i++) {
		ring = &adev->sdma.instance[i].ring;
		ring->ring_obj = NULL;
		ring->use_doorbell = true;

		DRM_INFO("use_doorbell being set to: [%s]\n",
				ring->use_doorbell?"true":"false");

		ring->doorbell_index = (i == 0) ?
			(AMDGPU_DOORBELL64_sDMA_ENGINE0 << 1) //get DWORD offset
			: (AMDGPU_DOORBELL64_sDMA_ENGINE1 << 1); // get DWORD offset

		sprintf(ring->name, "sdma%d", i);
		r = amdgpu_ring_init(adev, ring, 1024,
				     &adev->sdma.trap_irq,
				     (i == 0) ?
				     AMDGPU_SDMA_IRQ_TRAP0 :
				     AMDGPU_SDMA_IRQ_TRAP1);
		if (r)
			return r;
	}

	return r;
}

static int sdma_v4_0_sw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int i;

	for (i = 0; i < adev->sdma.num_instances; i++)
		amdgpu_ring_fini(&adev->sdma.instance[i].ring);

	return 0;
}

static int sdma_v4_0_hw_init(void *handle)
{
	int r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	sdma_v4_0_init_golden_registers(adev);

	r = sdma_v4_0_start(adev);

	return r;
}

static int sdma_v4_0_hw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (amdgpu_sriov_vf(adev))
		return 0;

	sdma_v4_0_ctx_switch_enable(adev, false);
	sdma_v4_0_enable(adev, false);

	return 0;
}

static int sdma_v4_0_suspend(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	return sdma_v4_0_hw_fini(adev);
}

static int sdma_v4_0_resume(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	return sdma_v4_0_hw_init(adev);
}

static bool sdma_v4_0_is_idle(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	u32 i;

	for (i = 0; i < adev->sdma.num_instances; i++) {
		u32 tmp = RREG32(sdma_v4_0_get_reg_offset(i, mmSDMA0_STATUS_REG));

		if (!(tmp & SDMA0_STATUS_REG__IDLE_MASK))
			return false;
	}

	return true;
}

static int sdma_v4_0_wait_for_idle(void *handle)
{
	unsigned i;
	u32 sdma0, sdma1;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	for (i = 0; i < adev->usec_timeout; i++) {
		sdma0 = RREG32(sdma_v4_0_get_reg_offset(0, mmSDMA0_STATUS_REG));
		sdma1 = RREG32(sdma_v4_0_get_reg_offset(1, mmSDMA0_STATUS_REG));

		if (sdma0 & sdma1 & SDMA0_STATUS_REG__IDLE_MASK)
			return 0;
		udelay(1);
	}
	return -ETIMEDOUT;
}

static int sdma_v4_0_soft_reset(void *handle)
{
	/* todo */

	return 0;
}

static int sdma_v4_0_set_trap_irq_state(struct amdgpu_device *adev,
					struct amdgpu_irq_src *source,
					unsigned type,
					enum amdgpu_interrupt_state state)
{
	u32 sdma_cntl;

	u32 reg_offset = (type == AMDGPU_SDMA_IRQ_TRAP0) ?
		sdma_v4_0_get_reg_offset(0, mmSDMA0_CNTL) :
		sdma_v4_0_get_reg_offset(1, mmSDMA0_CNTL);

	sdma_cntl = RREG32(reg_offset);
	sdma_cntl = REG_SET_FIELD(sdma_cntl, SDMA0_CNTL, TRAP_ENABLE,
		       state == AMDGPU_IRQ_STATE_ENABLE ? 1 : 0);
	WREG32(reg_offset, sdma_cntl);

	return 0;
}

static int sdma_v4_0_process_trap_irq(struct amdgpu_device *adev,
				      struct amdgpu_irq_src *source,
				      struct amdgpu_iv_entry *entry)
{
	DRM_DEBUG("IH: SDMA trap\n");
	switch (entry->client_id) {
	case AMDGPU_IH_CLIENTID_SDMA0:
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
	case AMDGPU_IH_CLIENTID_SDMA1:
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
	}
	return 0;
}

static int sdma_v4_0_process_illegal_inst_irq(struct amdgpu_device *adev,
					      struct amdgpu_irq_src *source,
					      struct amdgpu_iv_entry *entry)
{
	DRM_ERROR("Illegal instruction in SDMA command stream\n");
	schedule_work(&adev->reset_work);
	return 0;
}


static void sdma_v4_0_update_medium_grain_clock_gating(
		struct amdgpu_device *adev,
		bool enable)
{
	uint32_t data, def;

	if (enable && (adev->cg_flags & AMD_CG_SUPPORT_SDMA_MGCG)) {
		/* enable sdma0 clock gating */
		def = data = RREG32(SOC15_REG_OFFSET(SDMA0, 0, mmSDMA0_CLK_CTRL));
		data &= ~(SDMA0_CLK_CTRL__SOFT_OVERRIDE7_MASK |
			  SDMA0_CLK_CTRL__SOFT_OVERRIDE6_MASK |
			  SDMA0_CLK_CTRL__SOFT_OVERRIDE5_MASK |
			  SDMA0_CLK_CTRL__SOFT_OVERRIDE4_MASK |
			  SDMA0_CLK_CTRL__SOFT_OVERRIDE3_MASK |
			  SDMA0_CLK_CTRL__SOFT_OVERRIDE2_MASK |
			  SDMA0_CLK_CTRL__SOFT_OVERRIDE1_MASK |
			  SDMA0_CLK_CTRL__SOFT_OVERRIDE0_MASK);
		if (def != data)
			WREG32(SOC15_REG_OFFSET(SDMA0, 0, mmSDMA0_CLK_CTRL), data);

		if (adev->asic_type == CHIP_VEGA10) {
			def = data = RREG32(SOC15_REG_OFFSET(SDMA1, 0, mmSDMA1_CLK_CTRL));
			data &= ~(SDMA1_CLK_CTRL__SOFT_OVERRIDE7_MASK |
				  SDMA1_CLK_CTRL__SOFT_OVERRIDE6_MASK |
				  SDMA1_CLK_CTRL__SOFT_OVERRIDE5_MASK |
				  SDMA1_CLK_CTRL__SOFT_OVERRIDE4_MASK |
				  SDMA1_CLK_CTRL__SOFT_OVERRIDE3_MASK |
				  SDMA1_CLK_CTRL__SOFT_OVERRIDE2_MASK |
				  SDMA1_CLK_CTRL__SOFT_OVERRIDE1_MASK |
				  SDMA1_CLK_CTRL__SOFT_OVERRIDE0_MASK);
			if (def != data)
				WREG32(SOC15_REG_OFFSET(SDMA1, 0, mmSDMA1_CLK_CTRL), data);
		}
	} else {
		/* disable sdma0 clock gating */
		def = data = RREG32(SOC15_REG_OFFSET(SDMA0, 0, mmSDMA0_CLK_CTRL));
		data |= (SDMA0_CLK_CTRL__SOFT_OVERRIDE7_MASK |
			 SDMA0_CLK_CTRL__SOFT_OVERRIDE6_MASK |
			 SDMA0_CLK_CTRL__SOFT_OVERRIDE5_MASK |
			 SDMA0_CLK_CTRL__SOFT_OVERRIDE4_MASK |
			 SDMA0_CLK_CTRL__SOFT_OVERRIDE3_MASK |
			 SDMA0_CLK_CTRL__SOFT_OVERRIDE2_MASK |
			 SDMA0_CLK_CTRL__SOFT_OVERRIDE1_MASK |
			 SDMA0_CLK_CTRL__SOFT_OVERRIDE0_MASK);

		if (def != data)
			WREG32(SOC15_REG_OFFSET(SDMA0, 0, mmSDMA0_CLK_CTRL), data);

		if (adev->asic_type == CHIP_VEGA10) {
			def = data = RREG32(SOC15_REG_OFFSET(SDMA1, 0, mmSDMA1_CLK_CTRL));
			data |= (SDMA1_CLK_CTRL__SOFT_OVERRIDE7_MASK |
				 SDMA1_CLK_CTRL__SOFT_OVERRIDE6_MASK |
				 SDMA1_CLK_CTRL__SOFT_OVERRIDE5_MASK |
				 SDMA1_CLK_CTRL__SOFT_OVERRIDE4_MASK |
				 SDMA1_CLK_CTRL__SOFT_OVERRIDE3_MASK |
				 SDMA1_CLK_CTRL__SOFT_OVERRIDE2_MASK |
				 SDMA1_CLK_CTRL__SOFT_OVERRIDE1_MASK |
				 SDMA1_CLK_CTRL__SOFT_OVERRIDE0_MASK);
			if (def != data)
				WREG32(SOC15_REG_OFFSET(SDMA1, 0, mmSDMA1_CLK_CTRL), data);
		}
	}
}


static void sdma_v4_0_update_medium_grain_light_sleep(
		struct amdgpu_device *adev,
		bool enable)
{
	uint32_t data, def;

	if (enable && (adev->cg_flags & AMD_CG_SUPPORT_SDMA_LS)) {
		/* 1-not override: enable sdma0 mem light sleep */
		def = data = RREG32(SOC15_REG_OFFSET(SDMA0, 0, mmSDMA0_POWER_CNTL));
		data |= SDMA0_POWER_CNTL__MEM_POWER_OVERRIDE_MASK;
		if (def != data)
			WREG32(SOC15_REG_OFFSET(SDMA0, 0, mmSDMA0_POWER_CNTL), data);

		/* 1-not override: enable sdma1 mem light sleep */
		if (adev->asic_type == CHIP_VEGA10) {
			def = data = RREG32(SOC15_REG_OFFSET(SDMA1, 0, mmSDMA1_POWER_CNTL));
			data |= SDMA1_POWER_CNTL__MEM_POWER_OVERRIDE_MASK;
			if (def != data)
				WREG32(SOC15_REG_OFFSET(SDMA1, 0, mmSDMA1_POWER_CNTL), data);
		}
	} else {
		/* 0-override:disable sdma0 mem light sleep */
		def = data = RREG32(SOC15_REG_OFFSET(SDMA0, 0, mmSDMA0_POWER_CNTL));
		data &= ~SDMA0_POWER_CNTL__MEM_POWER_OVERRIDE_MASK;
		if (def != data)
			WREG32(SOC15_REG_OFFSET(SDMA0, 0, mmSDMA0_POWER_CNTL), data);

		/* 0-override:disable sdma1 mem light sleep */
		if (adev->asic_type == CHIP_VEGA10) {
			def = data = RREG32(SOC15_REG_OFFSET(SDMA1, 0, mmSDMA1_POWER_CNTL));
			data &= ~SDMA1_POWER_CNTL__MEM_POWER_OVERRIDE_MASK;
			if (def != data)
				WREG32(SOC15_REG_OFFSET(SDMA1, 0, mmSDMA1_POWER_CNTL), data);
		}
	}
}

static int sdma_v4_0_set_clockgating_state(void *handle,
					  enum amd_clockgating_state state)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (amdgpu_sriov_vf(adev))
		return 0;

	switch (adev->asic_type) {
	case CHIP_VEGA10:
	case CHIP_RAVEN:
		sdma_v4_0_update_medium_grain_clock_gating(adev,
				state == AMD_CG_STATE_GATE ? true : false);
		sdma_v4_0_update_medium_grain_light_sleep(adev,
				state == AMD_CG_STATE_GATE ? true : false);
		break;
	default:
		break;
	}
	return 0;
}

static int sdma_v4_0_set_powergating_state(void *handle,
					  enum amd_powergating_state state)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	switch (adev->asic_type) {
	case CHIP_RAVEN:
		sdma_v4_1_update_power_gating(adev,
				state == AMD_PG_STATE_GATE ? true : false);
		break;
	default:
		break;
	}

	return 0;
}

static void sdma_v4_0_get_clockgating_state(void *handle, u32 *flags)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int data;

	if (amdgpu_sriov_vf(adev))
		*flags = 0;

	/* AMD_CG_SUPPORT_SDMA_MGCG */
	data = RREG32(SOC15_REG_OFFSET(SDMA0, 0, mmSDMA0_CLK_CTRL));
	if (!(data & SDMA0_CLK_CTRL__SOFT_OVERRIDE7_MASK))
		*flags |= AMD_CG_SUPPORT_SDMA_MGCG;

	/* AMD_CG_SUPPORT_SDMA_LS */
	data = RREG32(SOC15_REG_OFFSET(SDMA0, 0, mmSDMA0_POWER_CNTL));
	if (data & SDMA0_POWER_CNTL__MEM_POWER_OVERRIDE_MASK)
		*flags |= AMD_CG_SUPPORT_SDMA_LS;
}

const struct amd_ip_funcs sdma_v4_0_ip_funcs = {
	.name = "sdma_v4_0",
	.early_init = sdma_v4_0_early_init,
	.late_init = NULL,
	.sw_init = sdma_v4_0_sw_init,
	.sw_fini = sdma_v4_0_sw_fini,
	.hw_init = sdma_v4_0_hw_init,
	.hw_fini = sdma_v4_0_hw_fini,
	.suspend = sdma_v4_0_suspend,
	.resume = sdma_v4_0_resume,
	.is_idle = sdma_v4_0_is_idle,
	.wait_for_idle = sdma_v4_0_wait_for_idle,
	.soft_reset = sdma_v4_0_soft_reset,
	.set_clockgating_state = sdma_v4_0_set_clockgating_state,
	.set_powergating_state = sdma_v4_0_set_powergating_state,
	.get_clockgating_state = sdma_v4_0_get_clockgating_state,
};

static const struct amdgpu_ring_funcs sdma_v4_0_ring_funcs = {
	.type = AMDGPU_RING_TYPE_SDMA,
	.align_mask = 0xf,
	.nop = SDMA_PKT_NOP_HEADER_OP(SDMA_OP_NOP),
	.support_64bit_ptrs = true,
	.vmhub = AMDGPU_MMHUB,
	.get_rptr = sdma_v4_0_ring_get_rptr,
	.get_wptr = sdma_v4_0_ring_get_wptr,
	.set_wptr = sdma_v4_0_ring_set_wptr,
	.emit_frame_size =
		6 + /* sdma_v4_0_ring_emit_hdp_flush */
		3 + /* sdma_v4_0_ring_emit_hdp_invalidate */
		6 + /* sdma_v4_0_ring_emit_pipeline_sync */
		18 + /* sdma_v4_0_ring_emit_vm_flush */
		10 + 10 + 10, /* sdma_v4_0_ring_emit_fence x3 for user fence, vm fence */
	.emit_ib_size = 7 + 6, /* sdma_v4_0_ring_emit_ib */
	.emit_ib = sdma_v4_0_ring_emit_ib,
	.emit_fence = sdma_v4_0_ring_emit_fence,
	.emit_pipeline_sync = sdma_v4_0_ring_emit_pipeline_sync,
	.emit_vm_flush = sdma_v4_0_ring_emit_vm_flush,
	.emit_hdp_flush = sdma_v4_0_ring_emit_hdp_flush,
	.emit_hdp_invalidate = sdma_v4_0_ring_emit_hdp_invalidate,
	.test_ring = sdma_v4_0_ring_test_ring,
	.test_ib = sdma_v4_0_ring_test_ib,
	.insert_nop = sdma_v4_0_ring_insert_nop,
	.pad_ib = sdma_v4_0_ring_pad_ib,
};

static void sdma_v4_0_set_ring_funcs(struct amdgpu_device *adev)
{
	int i;

	for (i = 0; i < adev->sdma.num_instances; i++)
		adev->sdma.instance[i].ring.funcs = &sdma_v4_0_ring_funcs;
}

static const struct amdgpu_irq_src_funcs sdma_v4_0_trap_irq_funcs = {
	.set = sdma_v4_0_set_trap_irq_state,
	.process = sdma_v4_0_process_trap_irq,
};

static const struct amdgpu_irq_src_funcs sdma_v4_0_illegal_inst_irq_funcs = {
	.process = sdma_v4_0_process_illegal_inst_irq,
};

static void sdma_v4_0_set_irq_funcs(struct amdgpu_device *adev)
{
	adev->sdma.trap_irq.num_types = AMDGPU_SDMA_IRQ_LAST;
	adev->sdma.trap_irq.funcs = &sdma_v4_0_trap_irq_funcs;
	adev->sdma.illegal_inst_irq.funcs = &sdma_v4_0_illegal_inst_irq_funcs;
}

/**
 * sdma_v4_0_emit_copy_buffer - copy buffer using the sDMA engine
 *
 * @ring: amdgpu_ring structure holding ring information
 * @src_offset: src GPU address
 * @dst_offset: dst GPU address
 * @byte_count: number of bytes to xfer
 *
 * Copy GPU buffers using the DMA engine (VEGA10).
 * Used by the amdgpu ttm implementation to move pages if
 * registered as the asic copy callback.
 */
static void sdma_v4_0_emit_copy_buffer(struct amdgpu_ib *ib,
				       uint64_t src_offset,
				       uint64_t dst_offset,
				       uint32_t byte_count)
{
	ib->ptr[ib->length_dw++] = SDMA_PKT_HEADER_OP(SDMA_OP_COPY) |
		SDMA_PKT_HEADER_SUB_OP(SDMA_SUBOP_COPY_LINEAR);
	ib->ptr[ib->length_dw++] = byte_count - 1;
	ib->ptr[ib->length_dw++] = 0; /* src/dst endian swap */
	ib->ptr[ib->length_dw++] = lower_32_bits(src_offset);
	ib->ptr[ib->length_dw++] = upper_32_bits(src_offset);
	ib->ptr[ib->length_dw++] = lower_32_bits(dst_offset);
	ib->ptr[ib->length_dw++] = upper_32_bits(dst_offset);
}

/**
 * sdma_v4_0_emit_fill_buffer - fill buffer using the sDMA engine
 *
 * @ring: amdgpu_ring structure holding ring information
 * @src_data: value to write to buffer
 * @dst_offset: dst GPU address
 * @byte_count: number of bytes to xfer
 *
 * Fill GPU buffers using the DMA engine (VEGA10).
 */
static void sdma_v4_0_emit_fill_buffer(struct amdgpu_ib *ib,
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

static const struct amdgpu_buffer_funcs sdma_v4_0_buffer_funcs = {
	.copy_max_bytes = 0x400000,
	.copy_num_dw = 7,
	.emit_copy_buffer = sdma_v4_0_emit_copy_buffer,

	.fill_max_bytes = 0x400000,
	.fill_num_dw = 5,
	.emit_fill_buffer = sdma_v4_0_emit_fill_buffer,
};

static void sdma_v4_0_set_buffer_funcs(struct amdgpu_device *adev)
{
	if (adev->mman.buffer_funcs == NULL) {
		adev->mman.buffer_funcs = &sdma_v4_0_buffer_funcs;
		adev->mman.buffer_funcs_ring = &adev->sdma.instance[0].ring;
	}
}

static const struct amdgpu_vm_pte_funcs sdma_v4_0_vm_pte_funcs = {
	.copy_pte = sdma_v4_0_vm_copy_pte,
	.write_pte = sdma_v4_0_vm_write_pte,
	.set_pte_pde = sdma_v4_0_vm_set_pte_pde,
};

static void sdma_v4_0_set_vm_pte_funcs(struct amdgpu_device *adev)
{
	unsigned i;

	if (adev->vm_manager.vm_pte_funcs == NULL) {
		adev->vm_manager.vm_pte_funcs = &sdma_v4_0_vm_pte_funcs;
		for (i = 0; i < adev->sdma.num_instances; i++)
			adev->vm_manager.vm_pte_rings[i] =
				&adev->sdma.instance[i].ring;

		adev->vm_manager.vm_pte_num_rings = adev->sdma.num_instances;
	}
}

const struct amdgpu_ip_block_version sdma_v4_0_ip_block = {
	.type = AMD_IP_BLOCK_TYPE_SDMA,
	.major = 4,
	.minor = 0,
	.rev = 0,
	.funcs = &sdma_v4_0_ip_funcs,
};
