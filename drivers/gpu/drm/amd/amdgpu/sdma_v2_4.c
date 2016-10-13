/*
 * Copyright 2014 Advanced Micro Devices, Inc.
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
 * Authors: Alex Deucher
 */
#include <linux/firmware.h>
#include <drm/drmP.h>
#include "amdgpu.h"
#include "amdgpu_ucode.h"
#include "amdgpu_trace.h"
#include "vi.h"
#include "vid.h"

#include "oss/oss_2_4_d.h"
#include "oss/oss_2_4_sh_mask.h"

#include "gmc/gmc_7_1_d.h"
#include "gmc/gmc_7_1_sh_mask.h"

#include "gca/gfx_8_0_d.h"
#include "gca/gfx_8_0_enum.h"
#include "gca/gfx_8_0_sh_mask.h"

#include "bif/bif_5_0_d.h"
#include "bif/bif_5_0_sh_mask.h"

#include "iceland_sdma_pkt_open.h"

static void sdma_v2_4_set_ring_funcs(struct amdgpu_device *adev);
static void sdma_v2_4_set_buffer_funcs(struct amdgpu_device *adev);
static void sdma_v2_4_set_vm_pte_funcs(struct amdgpu_device *adev);
static void sdma_v2_4_set_irq_funcs(struct amdgpu_device *adev);

MODULE_FIRMWARE("amdgpu/topaz_sdma.bin");
MODULE_FIRMWARE("amdgpu/topaz_sdma1.bin");

static const u32 sdma_offsets[SDMA_MAX_INSTANCE] =
{
	SDMA0_REGISTER_OFFSET,
	SDMA1_REGISTER_OFFSET
};

static const u32 golden_settings_iceland_a11[] =
{
	mmSDMA0_CHICKEN_BITS, 0xfc910007, 0x00810007,
	mmSDMA0_CLK_CTRL, 0xff000fff, 0x00000000,
	mmSDMA1_CHICKEN_BITS, 0xfc910007, 0x00810007,
	mmSDMA1_CLK_CTRL, 0xff000fff, 0x00000000,
};

static const u32 iceland_mgcg_cgcg_init[] =
{
	mmSDMA0_CLK_CTRL, 0xff000ff0, 0x00000100,
	mmSDMA1_CLK_CTRL, 0xff000ff0, 0x00000100
};

/*
 * sDMA - System DMA
 * Starting with CIK, the GPU has new asynchronous
 * DMA engines.  These engines are used for compute
 * and gfx.  There are two DMA engines (SDMA0, SDMA1)
 * and each one supports 1 ring buffer used for gfx
 * and 2 queues used for compute.
 *
 * The programming model is very similar to the CP
 * (ring buffer, IBs, etc.), but sDMA has it's own
 * packet format that is different from the PM4 format
 * used by the CP. sDMA supports copying data, writing
 * embedded data, solid fills, and a number of other
 * things.  It also has support for tiling/detiling of
 * buffers.
 */

static void sdma_v2_4_init_golden_registers(struct amdgpu_device *adev)
{
	switch (adev->asic_type) {
	case CHIP_TOPAZ:
		amdgpu_program_register_sequence(adev,
						 iceland_mgcg_cgcg_init,
						 (const u32)ARRAY_SIZE(iceland_mgcg_cgcg_init));
		amdgpu_program_register_sequence(adev,
						 golden_settings_iceland_a11,
						 (const u32)ARRAY_SIZE(golden_settings_iceland_a11));
		break;
	default:
		break;
	}
}

static void sdma_v2_4_free_microcode(struct amdgpu_device *adev)
{
	int i;
	for (i = 0; i < adev->sdma.num_instances; i++) {
		release_firmware(adev->sdma.instance[i].fw);
		adev->sdma.instance[i].fw = NULL;
	}
}

/**
 * sdma_v2_4_init_microcode - load ucode images from disk
 *
 * @adev: amdgpu_device pointer
 *
 * Use the firmware interface to load the ucode images into
 * the driver (not loaded into hw).
 * Returns 0 on success, error on failure.
 */
static int sdma_v2_4_init_microcode(struct amdgpu_device *adev)
{
	const char *chip_name;
	char fw_name[30];
	int err = 0, i;
	struct amdgpu_firmware_info *info = NULL;
	const struct common_firmware_header *header = NULL;
	const struct sdma_firmware_header_v1_0 *hdr;

	DRM_DEBUG("\n");

	switch (adev->asic_type) {
	case CHIP_TOPAZ:
		chip_name = "topaz";
		break;
	default: BUG();
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

		if (adev->firmware.smu_load) {
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
		printk(KERN_ERR
		       "sdma_v2_4: Failed to load firmware \"%s\"\n",
		       fw_name);
		for (i = 0; i < adev->sdma.num_instances; i++) {
			release_firmware(adev->sdma.instance[i].fw);
			adev->sdma.instance[i].fw = NULL;
		}
	}
	return err;
}

/**
 * sdma_v2_4_ring_get_rptr - get the current read pointer
 *
 * @ring: amdgpu ring pointer
 *
 * Get the current rptr from the hardware (VI+).
 */
static uint32_t sdma_v2_4_ring_get_rptr(struct amdgpu_ring *ring)
{
	/* XXX check if swapping is necessary on BE */
	return ring->adev->wb.wb[ring->rptr_offs] >> 2;
}

/**
 * sdma_v2_4_ring_get_wptr - get the current write pointer
 *
 * @ring: amdgpu ring pointer
 *
 * Get the current wptr from the hardware (VI+).
 */
static uint32_t sdma_v2_4_ring_get_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	int me = (ring == &ring->adev->sdma.instance[0].ring) ? 0 : 1;
	u32 wptr = RREG32(mmSDMA0_GFX_RB_WPTR + sdma_offsets[me]) >> 2;

	return wptr;
}

/**
 * sdma_v2_4_ring_set_wptr - commit the write pointer
 *
 * @ring: amdgpu ring pointer
 *
 * Write the wptr back to the hardware (VI+).
 */
static void sdma_v2_4_ring_set_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	int me = (ring == &ring->adev->sdma.instance[0].ring) ? 0 : 1;

	WREG32(mmSDMA0_GFX_RB_WPTR + sdma_offsets[me], ring->wptr << 2);
}

static void sdma_v2_4_ring_insert_nop(struct amdgpu_ring *ring, uint32_t count)
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
 * sdma_v2_4_ring_emit_ib - Schedule an IB on the DMA engine
 *
 * @ring: amdgpu ring pointer
 * @ib: IB object to schedule
 *
 * Schedule an IB in the DMA ring (VI).
 */
static void sdma_v2_4_ring_emit_ib(struct amdgpu_ring *ring,
				   struct amdgpu_ib *ib,
				   unsigned vm_id, bool ctx_switch)
{
	u32 vmid = vm_id & 0xf;

	/* IB packet must end on a 8 DW boundary */
	sdma_v2_4_ring_insert_nop(ring, (10 - (ring->wptr & 7)) % 8);

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
 * sdma_v2_4_hdp_flush_ring_emit - emit an hdp flush on the DMA ring
 *
 * @ring: amdgpu ring pointer
 *
 * Emit an hdp flush packet on the requested DMA ring.
 */
static void sdma_v2_4_ring_emit_hdp_flush(struct amdgpu_ring *ring)
{
	u32 ref_and_mask = 0;

	if (ring == &ring->adev->sdma.instance[0].ring)
		ref_and_mask = REG_SET_FIELD(ref_and_mask, GPU_HDP_FLUSH_DONE, SDMA0, 1);
	else
		ref_and_mask = REG_SET_FIELD(ref_and_mask, GPU_HDP_FLUSH_DONE, SDMA1, 1);

	amdgpu_ring_write(ring, SDMA_PKT_HEADER_OP(SDMA_OP_POLL_REGMEM) |
			  SDMA_PKT_POLL_REGMEM_HEADER_HDP_FLUSH(1) |
			  SDMA_PKT_POLL_REGMEM_HEADER_FUNC(3)); /* == */
	amdgpu_ring_write(ring, mmGPU_HDP_FLUSH_DONE << 2);
	amdgpu_ring_write(ring, mmGPU_HDP_FLUSH_REQ << 2);
	amdgpu_ring_write(ring, ref_and_mask); /* reference */
	amdgpu_ring_write(ring, ref_and_mask); /* mask */
	amdgpu_ring_write(ring, SDMA_PKT_POLL_REGMEM_DW5_RETRY_COUNT(0xfff) |
			  SDMA_PKT_POLL_REGMEM_DW5_INTERVAL(10)); /* retry count, poll interval */
}

static void sdma_v2_4_ring_emit_hdp_invalidate(struct amdgpu_ring *ring)
{
	amdgpu_ring_write(ring, SDMA_PKT_HEADER_OP(SDMA_OP_SRBM_WRITE) |
			  SDMA_PKT_SRBM_WRITE_HEADER_BYTE_EN(0xf));
	amdgpu_ring_write(ring, mmHDP_DEBUG0);
	amdgpu_ring_write(ring, 1);
}
/**
 * sdma_v2_4_ring_emit_fence - emit a fence on the DMA ring
 *
 * @ring: amdgpu ring pointer
 * @fence: amdgpu fence object
 *
 * Add a DMA fence packet to the ring to write
 * the fence seq number and DMA trap packet to generate
 * an interrupt if needed (VI).
 */
static void sdma_v2_4_ring_emit_fence(struct amdgpu_ring *ring, u64 addr, u64 seq,
				      unsigned flags)
{
	bool write64bit = flags & AMDGPU_FENCE_FLAG_64BIT;
	/* write the fence */
	amdgpu_ring_write(ring, SDMA_PKT_HEADER_OP(SDMA_OP_FENCE));
	amdgpu_ring_write(ring, lower_32_bits(addr));
	amdgpu_ring_write(ring, upper_32_bits(addr));
	amdgpu_ring_write(ring, lower_32_bits(seq));

	/* optionally write high bits as well */
	if (write64bit) {
		addr += 4;
		amdgpu_ring_write(ring, SDMA_PKT_HEADER_OP(SDMA_OP_FENCE));
		amdgpu_ring_write(ring, lower_32_bits(addr));
		amdgpu_ring_write(ring, upper_32_bits(addr));
		amdgpu_ring_write(ring, upper_32_bits(seq));
	}

	/* generate an interrupt */
	amdgpu_ring_write(ring, SDMA_PKT_HEADER_OP(SDMA_OP_TRAP));
	amdgpu_ring_write(ring, SDMA_PKT_TRAP_INT_CONTEXT_INT_CONTEXT(0));
}

/**
 * sdma_v2_4_gfx_stop - stop the gfx async dma engines
 *
 * @adev: amdgpu_device pointer
 *
 * Stop the gfx async dma ring buffers (VI).
 */
static void sdma_v2_4_gfx_stop(struct amdgpu_device *adev)
{
	struct amdgpu_ring *sdma0 = &adev->sdma.instance[0].ring;
	struct amdgpu_ring *sdma1 = &adev->sdma.instance[1].ring;
	u32 rb_cntl, ib_cntl;
	int i;

	if ((adev->mman.buffer_funcs_ring == sdma0) ||
	    (adev->mman.buffer_funcs_ring == sdma1))
		amdgpu_ttm_set_active_vram_size(adev, adev->mc.visible_vram_size);

	for (i = 0; i < adev->sdma.num_instances; i++) {
		rb_cntl = RREG32(mmSDMA0_GFX_RB_CNTL + sdma_offsets[i]);
		rb_cntl = REG_SET_FIELD(rb_cntl, SDMA0_GFX_RB_CNTL, RB_ENABLE, 0);
		WREG32(mmSDMA0_GFX_RB_CNTL + sdma_offsets[i], rb_cntl);
		ib_cntl = RREG32(mmSDMA0_GFX_IB_CNTL + sdma_offsets[i]);
		ib_cntl = REG_SET_FIELD(ib_cntl, SDMA0_GFX_IB_CNTL, IB_ENABLE, 0);
		WREG32(mmSDMA0_GFX_IB_CNTL + sdma_offsets[i], ib_cntl);
	}
	sdma0->ready = false;
	sdma1->ready = false;
}

/**
 * sdma_v2_4_rlc_stop - stop the compute async dma engines
 *
 * @adev: amdgpu_device pointer
 *
 * Stop the compute async dma queues (VI).
 */
static void sdma_v2_4_rlc_stop(struct amdgpu_device *adev)
{
	/* XXX todo */
}

/**
 * sdma_v2_4_enable - stop the async dma engines
 *
 * @adev: amdgpu_device pointer
 * @enable: enable/disable the DMA MEs.
 *
 * Halt or unhalt the async dma engines (VI).
 */
static void sdma_v2_4_enable(struct amdgpu_device *adev, bool enable)
{
	u32 f32_cntl;
	int i;

	if (!enable) {
		sdma_v2_4_gfx_stop(adev);
		sdma_v2_4_rlc_stop(adev);
	}

	for (i = 0; i < adev->sdma.num_instances; i++) {
		f32_cntl = RREG32(mmSDMA0_F32_CNTL + sdma_offsets[i]);
		if (enable)
			f32_cntl = REG_SET_FIELD(f32_cntl, SDMA0_F32_CNTL, HALT, 0);
		else
			f32_cntl = REG_SET_FIELD(f32_cntl, SDMA0_F32_CNTL, HALT, 1);
		WREG32(mmSDMA0_F32_CNTL + sdma_offsets[i], f32_cntl);
	}
}

/**
 * sdma_v2_4_gfx_resume - setup and start the async dma engines
 *
 * @adev: amdgpu_device pointer
 *
 * Set up the gfx DMA ring buffers and enable them (VI).
 * Returns 0 for success, error for failure.
 */
static int sdma_v2_4_gfx_resume(struct amdgpu_device *adev)
{
	struct amdgpu_ring *ring;
	u32 rb_cntl, ib_cntl;
	u32 rb_bufsz;
	u32 wb_offset;
	int i, j, r;

	for (i = 0; i < adev->sdma.num_instances; i++) {
		ring = &adev->sdma.instance[i].ring;
		wb_offset = (ring->rptr_offs * 4);

		mutex_lock(&adev->srbm_mutex);
		for (j = 0; j < 16; j++) {
			vi_srbm_select(adev, 0, 0, 0, j);
			/* SDMA GFX */
			WREG32(mmSDMA0_GFX_VIRTUAL_ADDR + sdma_offsets[i], 0);
			WREG32(mmSDMA0_GFX_APE1_CNTL + sdma_offsets[i], 0);
		}
		vi_srbm_select(adev, 0, 0, 0, 0);
		mutex_unlock(&adev->srbm_mutex);

		WREG32(mmSDMA0_TILING_CONFIG + sdma_offsets[i],
		       adev->gfx.config.gb_addr_config & 0x70);

		WREG32(mmSDMA0_SEM_WAIT_FAIL_TIMER_CNTL + sdma_offsets[i], 0);

		/* Set ring buffer size in dwords */
		rb_bufsz = order_base_2(ring->ring_size / 4);
		rb_cntl = RREG32(mmSDMA0_GFX_RB_CNTL + sdma_offsets[i]);
		rb_cntl = REG_SET_FIELD(rb_cntl, SDMA0_GFX_RB_CNTL, RB_SIZE, rb_bufsz);
#ifdef __BIG_ENDIAN
		rb_cntl = REG_SET_FIELD(rb_cntl, SDMA0_GFX_RB_CNTL, RB_SWAP_ENABLE, 1);
		rb_cntl = REG_SET_FIELD(rb_cntl, SDMA0_GFX_RB_CNTL,
					RPTR_WRITEBACK_SWAP_ENABLE, 1);
#endif
		WREG32(mmSDMA0_GFX_RB_CNTL + sdma_offsets[i], rb_cntl);

		/* Initialize the ring buffer's read and write pointers */
		WREG32(mmSDMA0_GFX_RB_RPTR + sdma_offsets[i], 0);
		WREG32(mmSDMA0_GFX_RB_WPTR + sdma_offsets[i], 0);
		WREG32(mmSDMA0_GFX_IB_RPTR + sdma_offsets[i], 0);
		WREG32(mmSDMA0_GFX_IB_OFFSET + sdma_offsets[i], 0);

		/* set the wb address whether it's enabled or not */
		WREG32(mmSDMA0_GFX_RB_RPTR_ADDR_HI + sdma_offsets[i],
		       upper_32_bits(adev->wb.gpu_addr + wb_offset) & 0xFFFFFFFF);
		WREG32(mmSDMA0_GFX_RB_RPTR_ADDR_LO + sdma_offsets[i],
		       lower_32_bits(adev->wb.gpu_addr + wb_offset) & 0xFFFFFFFC);

		rb_cntl = REG_SET_FIELD(rb_cntl, SDMA0_GFX_RB_CNTL, RPTR_WRITEBACK_ENABLE, 1);

		WREG32(mmSDMA0_GFX_RB_BASE + sdma_offsets[i], ring->gpu_addr >> 8);
		WREG32(mmSDMA0_GFX_RB_BASE_HI + sdma_offsets[i], ring->gpu_addr >> 40);

		ring->wptr = 0;
		WREG32(mmSDMA0_GFX_RB_WPTR + sdma_offsets[i], ring->wptr << 2);

		/* enable DMA RB */
		rb_cntl = REG_SET_FIELD(rb_cntl, SDMA0_GFX_RB_CNTL, RB_ENABLE, 1);
		WREG32(mmSDMA0_GFX_RB_CNTL + sdma_offsets[i], rb_cntl);

		ib_cntl = RREG32(mmSDMA0_GFX_IB_CNTL + sdma_offsets[i]);
		ib_cntl = REG_SET_FIELD(ib_cntl, SDMA0_GFX_IB_CNTL, IB_ENABLE, 1);
#ifdef __BIG_ENDIAN
		ib_cntl = REG_SET_FIELD(ib_cntl, SDMA0_GFX_IB_CNTL, IB_SWAP_ENABLE, 1);
#endif
		/* enable DMA IBs */
		WREG32(mmSDMA0_GFX_IB_CNTL + sdma_offsets[i], ib_cntl);

		ring->ready = true;
	}

	sdma_v2_4_enable(adev, true);
	for (i = 0; i < adev->sdma.num_instances; i++) {
		ring = &adev->sdma.instance[i].ring;
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

/**
 * sdma_v2_4_rlc_resume - setup and start the async dma engines
 *
 * @adev: amdgpu_device pointer
 *
 * Set up the compute DMA queues and enable them (VI).
 * Returns 0 for success, error for failure.
 */
static int sdma_v2_4_rlc_resume(struct amdgpu_device *adev)
{
	/* XXX todo */
	return 0;
}

/**
 * sdma_v2_4_load_microcode - load the sDMA ME ucode
 *
 * @adev: amdgpu_device pointer
 *
 * Loads the sDMA0/1 ucode.
 * Returns 0 for success, -EINVAL if the ucode is not available.
 */
static int sdma_v2_4_load_microcode(struct amdgpu_device *adev)
{
	const struct sdma_firmware_header_v1_0 *hdr;
	const __le32 *fw_data;
	u32 fw_size;
	int i, j;

	/* halt the MEs */
	sdma_v2_4_enable(adev, false);

	for (i = 0; i < adev->sdma.num_instances; i++) {
		if (!adev->sdma.instance[i].fw)
			return -EINVAL;
		hdr = (const struct sdma_firmware_header_v1_0 *)adev->sdma.instance[i].fw->data;
		amdgpu_ucode_print_sdma_hdr(&hdr->header);
		fw_size = le32_to_cpu(hdr->header.ucode_size_bytes) / 4;
		fw_data = (const __le32 *)
			(adev->sdma.instance[i].fw->data +
			 le32_to_cpu(hdr->header.ucode_array_offset_bytes));
		WREG32(mmSDMA0_UCODE_ADDR + sdma_offsets[i], 0);
		for (j = 0; j < fw_size; j++)
			WREG32(mmSDMA0_UCODE_DATA + sdma_offsets[i], le32_to_cpup(fw_data++));
		WREG32(mmSDMA0_UCODE_ADDR + sdma_offsets[i], adev->sdma.instance[i].fw_version);
	}

	return 0;
}

/**
 * sdma_v2_4_start - setup and start the async dma engines
 *
 * @adev: amdgpu_device pointer
 *
 * Set up the DMA engines and enable them (VI).
 * Returns 0 for success, error for failure.
 */
static int sdma_v2_4_start(struct amdgpu_device *adev)
{
	int r;

	if (!adev->pp_enabled) {
		if (!adev->firmware.smu_load) {
			r = sdma_v2_4_load_microcode(adev);
			if (r)
				return r;
		} else {
			r = adev->smu.smumgr_funcs->check_fw_load_finish(adev,
							AMDGPU_UCODE_ID_SDMA0);
			if (r)
				return -EINVAL;
			r = adev->smu.smumgr_funcs->check_fw_load_finish(adev,
							AMDGPU_UCODE_ID_SDMA1);
			if (r)
				return -EINVAL;
		}
	}

	/* halt the engine before programing */
	sdma_v2_4_enable(adev, false);

	/* start the gfx rings and rlc compute queues */
	r = sdma_v2_4_gfx_resume(adev);
	if (r)
		return r;
	r = sdma_v2_4_rlc_resume(adev);
	if (r)
		return r;

	return 0;
}

/**
 * sdma_v2_4_ring_test_ring - simple async dma engine test
 *
 * @ring: amdgpu_ring structure holding ring information
 *
 * Test the DMA engine by writing using it to write an
 * value to memory. (VI).
 * Returns 0 for success, error for failure.
 */
static int sdma_v2_4_ring_test_ring(struct amdgpu_ring *ring)
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
	amdgpu_ring_write(ring, SDMA_PKT_WRITE_UNTILED_DW_3_COUNT(1));
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
 * sdma_v2_4_ring_test_ib - test an IB on the DMA engine
 *
 * @ring: amdgpu_ring structure holding ring information
 *
 * Test a simple IB in the DMA ring (VI).
 * Returns 0 on success, error on failure.
 */
static int sdma_v2_4_ring_test_ib(struct amdgpu_ring *ring, long timeout)
{
	struct amdgpu_device *adev = ring->adev;
	struct amdgpu_ib ib;
	struct fence *f = NULL;
	unsigned index;
	u32 tmp = 0;
	u64 gpu_addr;
	long r;

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
	ib.ptr[3] = SDMA_PKT_WRITE_UNTILED_DW_3_COUNT(1);
	ib.ptr[4] = 0xDEADBEEF;
	ib.ptr[5] = SDMA_PKT_HEADER_OP(SDMA_OP_NOP);
	ib.ptr[6] = SDMA_PKT_HEADER_OP(SDMA_OP_NOP);
	ib.ptr[7] = SDMA_PKT_HEADER_OP(SDMA_OP_NOP);
	ib.length_dw = 8;

	r = amdgpu_ib_schedule(ring, 1, &ib, NULL, NULL, &f);
	if (r)
		goto err1;

	r = fence_wait_timeout(f, false, timeout);
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
	fence_put(f);
err0:
	amdgpu_wb_free(adev, index);
	return r;
}

/**
 * sdma_v2_4_vm_copy_pte - update PTEs by copying them from the GART
 *
 * @ib: indirect buffer to fill with commands
 * @pe: addr of the page entry
 * @src: src addr to copy from
 * @count: number of page entries to update
 *
 * Update PTEs by copying them from the GART using sDMA (CIK).
 */
static void sdma_v2_4_vm_copy_pte(struct amdgpu_ib *ib,
				  uint64_t pe, uint64_t src,
				  unsigned count)
{
	unsigned bytes = count * 8;

	ib->ptr[ib->length_dw++] = SDMA_PKT_HEADER_OP(SDMA_OP_COPY) |
		SDMA_PKT_HEADER_SUB_OP(SDMA_SUBOP_COPY_LINEAR);
	ib->ptr[ib->length_dw++] = bytes;
	ib->ptr[ib->length_dw++] = 0; /* src/dst endian swap */
	ib->ptr[ib->length_dw++] = lower_32_bits(src);
	ib->ptr[ib->length_dw++] = upper_32_bits(src);
	ib->ptr[ib->length_dw++] = lower_32_bits(pe);
	ib->ptr[ib->length_dw++] = upper_32_bits(pe);
}

/**
 * sdma_v2_4_vm_write_pte - update PTEs by writing them manually
 *
 * @ib: indirect buffer to fill with commands
 * @pe: addr of the page entry
 * @value: dst addr to write into pe
 * @count: number of page entries to update
 * @incr: increase next addr by incr bytes
 *
 * Update PTEs by writing them manually using sDMA (CIK).
 */
static void sdma_v2_4_vm_write_pte(struct amdgpu_ib *ib, uint64_t pe,
				   uint64_t value, unsigned count,
				   uint32_t incr)
{
	unsigned ndw = count * 2;

	ib->ptr[ib->length_dw++] = SDMA_PKT_HEADER_OP(SDMA_OP_WRITE) |
		SDMA_PKT_HEADER_SUB_OP(SDMA_SUBOP_COPY_LINEAR);
	ib->ptr[ib->length_dw++] = pe;
	ib->ptr[ib->length_dw++] = upper_32_bits(pe);
	ib->ptr[ib->length_dw++] = ndw;
	for (; ndw > 0; ndw -= 2, --count, pe += 8) {
		ib->ptr[ib->length_dw++] = lower_32_bits(value);
		ib->ptr[ib->length_dw++] = upper_32_bits(value);
		value += incr;
	}
}

/**
 * sdma_v2_4_vm_set_pte_pde - update the page tables using sDMA
 *
 * @ib: indirect buffer to fill with commands
 * @pe: addr of the page entry
 * @addr: dst addr to write into pe
 * @count: number of page entries to update
 * @incr: increase next addr by incr bytes
 * @flags: access flags
 *
 * Update the page tables using sDMA (CIK).
 */
static void sdma_v2_4_vm_set_pte_pde(struct amdgpu_ib *ib, uint64_t pe,
				     uint64_t addr, unsigned count,
				     uint32_t incr, uint32_t flags)
{
	/* for physically contiguous pages (vram) */
	ib->ptr[ib->length_dw++] = SDMA_PKT_HEADER_OP(SDMA_OP_GEN_PTEPDE);
	ib->ptr[ib->length_dw++] = lower_32_bits(pe); /* dst addr */
	ib->ptr[ib->length_dw++] = upper_32_bits(pe);
	ib->ptr[ib->length_dw++] = flags; /* mask */
	ib->ptr[ib->length_dw++] = 0;
	ib->ptr[ib->length_dw++] = lower_32_bits(addr); /* value */
	ib->ptr[ib->length_dw++] = upper_32_bits(addr);
	ib->ptr[ib->length_dw++] = incr; /* increment size */
	ib->ptr[ib->length_dw++] = 0;
	ib->ptr[ib->length_dw++] = count; /* number of entries */
}

/**
 * sdma_v2_4_ring_pad_ib - pad the IB to the required number of dw
 *
 * @ib: indirect buffer to fill with padding
 *
 */
static void sdma_v2_4_ring_pad_ib(struct amdgpu_ring *ring, struct amdgpu_ib *ib)
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
 * sdma_v2_4_ring_emit_pipeline_sync - sync the pipeline
 *
 * @ring: amdgpu_ring pointer
 *
 * Make sure all previous operations are completed (CIK).
 */
static void sdma_v2_4_ring_emit_pipeline_sync(struct amdgpu_ring *ring)
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
 * sdma_v2_4_ring_emit_vm_flush - cik vm flush using sDMA
 *
 * @ring: amdgpu_ring pointer
 * @vm: amdgpu_vm pointer
 *
 * Update the page table base and flush the VM TLB
 * using sDMA (VI).
 */
static void sdma_v2_4_ring_emit_vm_flush(struct amdgpu_ring *ring,
					 unsigned vm_id, uint64_t pd_addr)
{
	amdgpu_ring_write(ring, SDMA_PKT_HEADER_OP(SDMA_OP_SRBM_WRITE) |
			  SDMA_PKT_SRBM_WRITE_HEADER_BYTE_EN(0xf));
	if (vm_id < 8) {
		amdgpu_ring_write(ring, (mmVM_CONTEXT0_PAGE_TABLE_BASE_ADDR + vm_id));
	} else {
		amdgpu_ring_write(ring, (mmVM_CONTEXT8_PAGE_TABLE_BASE_ADDR + vm_id - 8));
	}
	amdgpu_ring_write(ring, pd_addr >> 12);

	/* flush TLB */
	amdgpu_ring_write(ring, SDMA_PKT_HEADER_OP(SDMA_OP_SRBM_WRITE) |
			  SDMA_PKT_SRBM_WRITE_HEADER_BYTE_EN(0xf));
	amdgpu_ring_write(ring, mmVM_INVALIDATE_REQUEST);
	amdgpu_ring_write(ring, 1 << vm_id);

	/* wait for flush */
	amdgpu_ring_write(ring, SDMA_PKT_HEADER_OP(SDMA_OP_POLL_REGMEM) |
			  SDMA_PKT_POLL_REGMEM_HEADER_HDP_FLUSH(0) |
			  SDMA_PKT_POLL_REGMEM_HEADER_FUNC(0)); /* always */
	amdgpu_ring_write(ring, mmVM_INVALIDATE_REQUEST << 2);
	amdgpu_ring_write(ring, 0);
	amdgpu_ring_write(ring, 0); /* reference */
	amdgpu_ring_write(ring, 0); /* mask */
	amdgpu_ring_write(ring, SDMA_PKT_POLL_REGMEM_DW5_RETRY_COUNT(0xfff) |
			  SDMA_PKT_POLL_REGMEM_DW5_INTERVAL(10)); /* retry count, poll interval */
}

static int sdma_v2_4_early_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	adev->sdma.num_instances = SDMA_MAX_INSTANCE;

	sdma_v2_4_set_ring_funcs(adev);
	sdma_v2_4_set_buffer_funcs(adev);
	sdma_v2_4_set_vm_pte_funcs(adev);
	sdma_v2_4_set_irq_funcs(adev);

	return 0;
}

static int sdma_v2_4_sw_init(void *handle)
{
	struct amdgpu_ring *ring;
	int r, i;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	/* SDMA trap event */
	r = amdgpu_irq_add_id(adev, 224, &adev->sdma.trap_irq);
	if (r)
		return r;

	/* SDMA Privileged inst */
	r = amdgpu_irq_add_id(adev, 241, &adev->sdma.illegal_inst_irq);
	if (r)
		return r;

	/* SDMA Privileged inst */
	r = amdgpu_irq_add_id(adev, 247, &adev->sdma.illegal_inst_irq);
	if (r)
		return r;

	r = sdma_v2_4_init_microcode(adev);
	if (r) {
		DRM_ERROR("Failed to load sdma firmware!\n");
		return r;
	}

	for (i = 0; i < adev->sdma.num_instances; i++) {
		ring = &adev->sdma.instance[i].ring;
		ring->ring_obj = NULL;
		ring->use_doorbell = false;
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

static int sdma_v2_4_sw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int i;

	for (i = 0; i < adev->sdma.num_instances; i++)
		amdgpu_ring_fini(&adev->sdma.instance[i].ring);

	sdma_v2_4_free_microcode(adev);
	return 0;
}

static int sdma_v2_4_hw_init(void *handle)
{
	int r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	sdma_v2_4_init_golden_registers(adev);

	r = sdma_v2_4_start(adev);
	if (r)
		return r;

	return r;
}

static int sdma_v2_4_hw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	sdma_v2_4_enable(adev, false);

	return 0;
}

static int sdma_v2_4_suspend(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	return sdma_v2_4_hw_fini(adev);
}

static int sdma_v2_4_resume(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	return sdma_v2_4_hw_init(adev);
}

static bool sdma_v2_4_is_idle(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	u32 tmp = RREG32(mmSRBM_STATUS2);

	if (tmp & (SRBM_STATUS2__SDMA_BUSY_MASK |
		   SRBM_STATUS2__SDMA1_BUSY_MASK))
	    return false;

	return true;
}

static int sdma_v2_4_wait_for_idle(void *handle)
{
	unsigned i;
	u32 tmp;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	for (i = 0; i < adev->usec_timeout; i++) {
		tmp = RREG32(mmSRBM_STATUS2) & (SRBM_STATUS2__SDMA_BUSY_MASK |
				SRBM_STATUS2__SDMA1_BUSY_MASK);

		if (!tmp)
			return 0;
		udelay(1);
	}
	return -ETIMEDOUT;
}

static int sdma_v2_4_soft_reset(void *handle)
{
	u32 srbm_soft_reset = 0;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	u32 tmp = RREG32(mmSRBM_STATUS2);

	if (tmp & SRBM_STATUS2__SDMA_BUSY_MASK) {
		/* sdma0 */
		tmp = RREG32(mmSDMA0_F32_CNTL + SDMA0_REGISTER_OFFSET);
		tmp = REG_SET_FIELD(tmp, SDMA0_F32_CNTL, HALT, 0);
		WREG32(mmSDMA0_F32_CNTL + SDMA0_REGISTER_OFFSET, tmp);
		srbm_soft_reset |= SRBM_SOFT_RESET__SOFT_RESET_SDMA_MASK;
	}
	if (tmp & SRBM_STATUS2__SDMA1_BUSY_MASK) {
		/* sdma1 */
		tmp = RREG32(mmSDMA0_F32_CNTL + SDMA1_REGISTER_OFFSET);
		tmp = REG_SET_FIELD(tmp, SDMA0_F32_CNTL, HALT, 0);
		WREG32(mmSDMA0_F32_CNTL + SDMA1_REGISTER_OFFSET, tmp);
		srbm_soft_reset |= SRBM_SOFT_RESET__SOFT_RESET_SDMA1_MASK;
	}

	if (srbm_soft_reset) {
		tmp = RREG32(mmSRBM_SOFT_RESET);
		tmp |= srbm_soft_reset;
		dev_info(adev->dev, "SRBM_SOFT_RESET=0x%08X\n", tmp);
		WREG32(mmSRBM_SOFT_RESET, tmp);
		tmp = RREG32(mmSRBM_SOFT_RESET);

		udelay(50);

		tmp &= ~srbm_soft_reset;
		WREG32(mmSRBM_SOFT_RESET, tmp);
		tmp = RREG32(mmSRBM_SOFT_RESET);

		/* Wait a little for things to settle down */
		udelay(50);
	}

	return 0;
}

static int sdma_v2_4_set_trap_irq_state(struct amdgpu_device *adev,
					struct amdgpu_irq_src *src,
					unsigned type,
					enum amdgpu_interrupt_state state)
{
	u32 sdma_cntl;

	switch (type) {
	case AMDGPU_SDMA_IRQ_TRAP0:
		switch (state) {
		case AMDGPU_IRQ_STATE_DISABLE:
			sdma_cntl = RREG32(mmSDMA0_CNTL + SDMA0_REGISTER_OFFSET);
			sdma_cntl = REG_SET_FIELD(sdma_cntl, SDMA0_CNTL, TRAP_ENABLE, 0);
			WREG32(mmSDMA0_CNTL + SDMA0_REGISTER_OFFSET, sdma_cntl);
			break;
		case AMDGPU_IRQ_STATE_ENABLE:
			sdma_cntl = RREG32(mmSDMA0_CNTL + SDMA0_REGISTER_OFFSET);
			sdma_cntl = REG_SET_FIELD(sdma_cntl, SDMA0_CNTL, TRAP_ENABLE, 1);
			WREG32(mmSDMA0_CNTL + SDMA0_REGISTER_OFFSET, sdma_cntl);
			break;
		default:
			break;
		}
		break;
	case AMDGPU_SDMA_IRQ_TRAP1:
		switch (state) {
		case AMDGPU_IRQ_STATE_DISABLE:
			sdma_cntl = RREG32(mmSDMA0_CNTL + SDMA1_REGISTER_OFFSET);
			sdma_cntl = REG_SET_FIELD(sdma_cntl, SDMA0_CNTL, TRAP_ENABLE, 0);
			WREG32(mmSDMA0_CNTL + SDMA1_REGISTER_OFFSET, sdma_cntl);
			break;
		case AMDGPU_IRQ_STATE_ENABLE:
			sdma_cntl = RREG32(mmSDMA0_CNTL + SDMA1_REGISTER_OFFSET);
			sdma_cntl = REG_SET_FIELD(sdma_cntl, SDMA0_CNTL, TRAP_ENABLE, 1);
			WREG32(mmSDMA0_CNTL + SDMA1_REGISTER_OFFSET, sdma_cntl);
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
	return 0;
}

static int sdma_v2_4_process_trap_irq(struct amdgpu_device *adev,
				      struct amdgpu_irq_src *source,
				      struct amdgpu_iv_entry *entry)
{
	u8 instance_id, queue_id;

	instance_id = (entry->ring_id & 0x3) >> 0;
	queue_id = (entry->ring_id & 0xc) >> 2;
	DRM_DEBUG("IH: SDMA trap\n");
	switch (instance_id) {
	case 0:
		switch (queue_id) {
		case 0:
			amdgpu_fence_process(&adev->sdma.instance[0].ring);
			break;
		case 1:
			/* XXX compute */
			break;
		case 2:
			/* XXX compute */
			break;
		}
		break;
	case 1:
		switch (queue_id) {
		case 0:
			amdgpu_fence_process(&adev->sdma.instance[1].ring);
			break;
		case 1:
			/* XXX compute */
			break;
		case 2:
			/* XXX compute */
			break;
		}
		break;
	}
	return 0;
}

static int sdma_v2_4_process_illegal_inst_irq(struct amdgpu_device *adev,
					      struct amdgpu_irq_src *source,
					      struct amdgpu_iv_entry *entry)
{
	DRM_ERROR("Illegal instruction in SDMA command stream\n");
	schedule_work(&adev->reset_work);
	return 0;
}

static int sdma_v2_4_set_clockgating_state(void *handle,
					  enum amd_clockgating_state state)
{
	/* XXX handled via the smc on VI */
	return 0;
}

static int sdma_v2_4_set_powergating_state(void *handle,
					  enum amd_powergating_state state)
{
	return 0;
}

static const struct amd_ip_funcs sdma_v2_4_ip_funcs = {
	.name = "sdma_v2_4",
	.early_init = sdma_v2_4_early_init,
	.late_init = NULL,
	.sw_init = sdma_v2_4_sw_init,
	.sw_fini = sdma_v2_4_sw_fini,
	.hw_init = sdma_v2_4_hw_init,
	.hw_fini = sdma_v2_4_hw_fini,
	.suspend = sdma_v2_4_suspend,
	.resume = sdma_v2_4_resume,
	.is_idle = sdma_v2_4_is_idle,
	.wait_for_idle = sdma_v2_4_wait_for_idle,
	.soft_reset = sdma_v2_4_soft_reset,
	.set_clockgating_state = sdma_v2_4_set_clockgating_state,
	.set_powergating_state = sdma_v2_4_set_powergating_state,
};

static const struct amdgpu_ring_funcs sdma_v2_4_ring_funcs = {
	.type = AMDGPU_RING_TYPE_SDMA,
	.align_mask = 0xf,
	.nop = SDMA_PKT_NOP_HEADER_OP(SDMA_OP_NOP),
	.get_rptr = sdma_v2_4_ring_get_rptr,
	.get_wptr = sdma_v2_4_ring_get_wptr,
	.set_wptr = sdma_v2_4_ring_set_wptr,
	.emit_frame_size =
		6 + /* sdma_v2_4_ring_emit_hdp_flush */
		3 + /* sdma_v2_4_ring_emit_hdp_invalidate */
		6 + /* sdma_v2_4_ring_emit_pipeline_sync */
		12 + /* sdma_v2_4_ring_emit_vm_flush */
		10 + 10 + 10, /* sdma_v2_4_ring_emit_fence x3 for user fence, vm fence */
	.emit_ib_size = 7 + 6, /* sdma_v2_4_ring_emit_ib */
	.emit_ib = sdma_v2_4_ring_emit_ib,
	.emit_fence = sdma_v2_4_ring_emit_fence,
	.emit_pipeline_sync = sdma_v2_4_ring_emit_pipeline_sync,
	.emit_vm_flush = sdma_v2_4_ring_emit_vm_flush,
	.emit_hdp_flush = sdma_v2_4_ring_emit_hdp_flush,
	.emit_hdp_invalidate = sdma_v2_4_ring_emit_hdp_invalidate,
	.test_ring = sdma_v2_4_ring_test_ring,
	.test_ib = sdma_v2_4_ring_test_ib,
	.insert_nop = sdma_v2_4_ring_insert_nop,
	.pad_ib = sdma_v2_4_ring_pad_ib,
};

static void sdma_v2_4_set_ring_funcs(struct amdgpu_device *adev)
{
	int i;

	for (i = 0; i < adev->sdma.num_instances; i++)
		adev->sdma.instance[i].ring.funcs = &sdma_v2_4_ring_funcs;
}

static const struct amdgpu_irq_src_funcs sdma_v2_4_trap_irq_funcs = {
	.set = sdma_v2_4_set_trap_irq_state,
	.process = sdma_v2_4_process_trap_irq,
};

static const struct amdgpu_irq_src_funcs sdma_v2_4_illegal_inst_irq_funcs = {
	.process = sdma_v2_4_process_illegal_inst_irq,
};

static void sdma_v2_4_set_irq_funcs(struct amdgpu_device *adev)
{
	adev->sdma.trap_irq.num_types = AMDGPU_SDMA_IRQ_LAST;
	adev->sdma.trap_irq.funcs = &sdma_v2_4_trap_irq_funcs;
	adev->sdma.illegal_inst_irq.funcs = &sdma_v2_4_illegal_inst_irq_funcs;
}

/**
 * sdma_v2_4_emit_copy_buffer - copy buffer using the sDMA engine
 *
 * @ring: amdgpu_ring structure holding ring information
 * @src_offset: src GPU address
 * @dst_offset: dst GPU address
 * @byte_count: number of bytes to xfer
 *
 * Copy GPU buffers using the DMA engine (VI).
 * Used by the amdgpu ttm implementation to move pages if
 * registered as the asic copy callback.
 */
static void sdma_v2_4_emit_copy_buffer(struct amdgpu_ib *ib,
				       uint64_t src_offset,
				       uint64_t dst_offset,
				       uint32_t byte_count)
{
	ib->ptr[ib->length_dw++] = SDMA_PKT_HEADER_OP(SDMA_OP_COPY) |
		SDMA_PKT_HEADER_SUB_OP(SDMA_SUBOP_COPY_LINEAR);
	ib->ptr[ib->length_dw++] = byte_count;
	ib->ptr[ib->length_dw++] = 0; /* src/dst endian swap */
	ib->ptr[ib->length_dw++] = lower_32_bits(src_offset);
	ib->ptr[ib->length_dw++] = upper_32_bits(src_offset);
	ib->ptr[ib->length_dw++] = lower_32_bits(dst_offset);
	ib->ptr[ib->length_dw++] = upper_32_bits(dst_offset);
}

/**
 * sdma_v2_4_emit_fill_buffer - fill buffer using the sDMA engine
 *
 * @ring: amdgpu_ring structure holding ring information
 * @src_data: value to write to buffer
 * @dst_offset: dst GPU address
 * @byte_count: number of bytes to xfer
 *
 * Fill GPU buffers using the DMA engine (VI).
 */
static void sdma_v2_4_emit_fill_buffer(struct amdgpu_ib *ib,
				       uint32_t src_data,
				       uint64_t dst_offset,
				       uint32_t byte_count)
{
	ib->ptr[ib->length_dw++] = SDMA_PKT_HEADER_OP(SDMA_OP_CONST_FILL);
	ib->ptr[ib->length_dw++] = lower_32_bits(dst_offset);
	ib->ptr[ib->length_dw++] = upper_32_bits(dst_offset);
	ib->ptr[ib->length_dw++] = src_data;
	ib->ptr[ib->length_dw++] = byte_count;
}

static const struct amdgpu_buffer_funcs sdma_v2_4_buffer_funcs = {
	.copy_max_bytes = 0x1fffff,
	.copy_num_dw = 7,
	.emit_copy_buffer = sdma_v2_4_emit_copy_buffer,

	.fill_max_bytes = 0x1fffff,
	.fill_num_dw = 7,
	.emit_fill_buffer = sdma_v2_4_emit_fill_buffer,
};

static void sdma_v2_4_set_buffer_funcs(struct amdgpu_device *adev)
{
	if (adev->mman.buffer_funcs == NULL) {
		adev->mman.buffer_funcs = &sdma_v2_4_buffer_funcs;
		adev->mman.buffer_funcs_ring = &adev->sdma.instance[0].ring;
	}
}

static const struct amdgpu_vm_pte_funcs sdma_v2_4_vm_pte_funcs = {
	.copy_pte = sdma_v2_4_vm_copy_pte,
	.write_pte = sdma_v2_4_vm_write_pte,
	.set_pte_pde = sdma_v2_4_vm_set_pte_pde,
};

static void sdma_v2_4_set_vm_pte_funcs(struct amdgpu_device *adev)
{
	unsigned i;

	if (adev->vm_manager.vm_pte_funcs == NULL) {
		adev->vm_manager.vm_pte_funcs = &sdma_v2_4_vm_pte_funcs;
		for (i = 0; i < adev->sdma.num_instances; i++)
			adev->vm_manager.vm_pte_rings[i] =
				&adev->sdma.instance[i].ring;

		adev->vm_manager.vm_pte_num_rings = adev->sdma.num_instances;
	}
}

const struct amdgpu_ip_block_version sdma_v2_4_ip_block =
{
	.type = AMD_IP_BLOCK_TYPE_SDMA,
	.major = 2,
	.minor = 4,
	.rev = 0,
	.funcs = &sdma_v2_4_ip_funcs,
};
