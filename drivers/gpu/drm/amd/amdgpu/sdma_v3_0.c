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

#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/module.h>

#include "amdgpu.h"
#include "amdgpu_ucode.h"
#include "amdgpu_trace.h"
#include "vi.h"
#include "vid.h"

#include "oss/oss_3_0_d.h"
#include "oss/oss_3_0_sh_mask.h"

#include "gmc/gmc_8_1_d.h"
#include "gmc/gmc_8_1_sh_mask.h"

#include "gca/gfx_8_0_d.h"
#include "gca/gfx_8_0_enum.h"
#include "gca/gfx_8_0_sh_mask.h"

#include "bif/bif_5_0_d.h"
#include "bif/bif_5_0_sh_mask.h"

#include "tonga_sdma_pkt_open.h"

#include "ivsrcid/ivsrcid_vislands30.h"

static void sdma_v3_0_set_ring_funcs(struct amdgpu_device *adev);
static void sdma_v3_0_set_buffer_funcs(struct amdgpu_device *adev);
static void sdma_v3_0_set_vm_pte_funcs(struct amdgpu_device *adev);
static void sdma_v3_0_set_irq_funcs(struct amdgpu_device *adev);

MODULE_FIRMWARE("amdgpu/tonga_sdma.bin");
MODULE_FIRMWARE("amdgpu/tonga_sdma1.bin");
MODULE_FIRMWARE("amdgpu/carrizo_sdma.bin");
MODULE_FIRMWARE("amdgpu/carrizo_sdma1.bin");
MODULE_FIRMWARE("amdgpu/fiji_sdma.bin");
MODULE_FIRMWARE("amdgpu/fiji_sdma1.bin");
MODULE_FIRMWARE("amdgpu/stoney_sdma.bin");
MODULE_FIRMWARE("amdgpu/polaris10_sdma.bin");
MODULE_FIRMWARE("amdgpu/polaris10_sdma1.bin");
MODULE_FIRMWARE("amdgpu/polaris11_sdma.bin");
MODULE_FIRMWARE("amdgpu/polaris11_sdma1.bin");
MODULE_FIRMWARE("amdgpu/polaris12_sdma.bin");
MODULE_FIRMWARE("amdgpu/polaris12_sdma1.bin");
MODULE_FIRMWARE("amdgpu/vegam_sdma.bin");
MODULE_FIRMWARE("amdgpu/vegam_sdma1.bin");


static const u32 sdma_offsets[SDMA_MAX_INSTANCE] =
{
	SDMA0_REGISTER_OFFSET,
	SDMA1_REGISTER_OFFSET
};

static const u32 golden_settings_tonga_a11[] =
{
	mmSDMA0_CHICKEN_BITS, 0xfc910007, 0x00810007,
	mmSDMA0_CLK_CTRL, 0xff000fff, 0x00000000,
	mmSDMA0_GFX_IB_CNTL, 0x800f0111, 0x00000100,
	mmSDMA0_RLC0_IB_CNTL, 0x800f0111, 0x00000100,
	mmSDMA0_RLC1_IB_CNTL, 0x800f0111, 0x00000100,
	mmSDMA1_CHICKEN_BITS, 0xfc910007, 0x00810007,
	mmSDMA1_CLK_CTRL, 0xff000fff, 0x00000000,
	mmSDMA1_GFX_IB_CNTL, 0x800f0111, 0x00000100,
	mmSDMA1_RLC0_IB_CNTL, 0x800f0111, 0x00000100,
	mmSDMA1_RLC1_IB_CNTL, 0x800f0111, 0x00000100,
};

static const u32 tonga_mgcg_cgcg_init[] =
{
	mmSDMA0_CLK_CTRL, 0xff000ff0, 0x00000100,
	mmSDMA1_CLK_CTRL, 0xff000ff0, 0x00000100
};

static const u32 golden_settings_fiji_a10[] =
{
	mmSDMA0_CHICKEN_BITS, 0xfc910007, 0x00810007,
	mmSDMA0_GFX_IB_CNTL, 0x800f0111, 0x00000100,
	mmSDMA0_RLC0_IB_CNTL, 0x800f0111, 0x00000100,
	mmSDMA0_RLC1_IB_CNTL, 0x800f0111, 0x00000100,
	mmSDMA1_CHICKEN_BITS, 0xfc910007, 0x00810007,
	mmSDMA1_GFX_IB_CNTL, 0x800f0111, 0x00000100,
	mmSDMA1_RLC0_IB_CNTL, 0x800f0111, 0x00000100,
	mmSDMA1_RLC1_IB_CNTL, 0x800f0111, 0x00000100,
};

static const u32 fiji_mgcg_cgcg_init[] =
{
	mmSDMA0_CLK_CTRL, 0xff000ff0, 0x00000100,
	mmSDMA1_CLK_CTRL, 0xff000ff0, 0x00000100
};

static const u32 golden_settings_polaris11_a11[] =
{
	mmSDMA0_CHICKEN_BITS, 0xfc910007, 0x00810007,
	mmSDMA0_CLK_CTRL, 0xff000fff, 0x00000000,
	mmSDMA0_GFX_IB_CNTL, 0x800f0111, 0x00000100,
	mmSDMA0_RLC0_IB_CNTL, 0x800f0111, 0x00000100,
	mmSDMA0_RLC1_IB_CNTL, 0x800f0111, 0x00000100,
	mmSDMA1_CHICKEN_BITS, 0xfc910007, 0x00810007,
	mmSDMA1_CLK_CTRL, 0xff000fff, 0x00000000,
	mmSDMA1_GFX_IB_CNTL, 0x800f0111, 0x00000100,
	mmSDMA1_RLC0_IB_CNTL, 0x800f0111, 0x00000100,
	mmSDMA1_RLC1_IB_CNTL, 0x800f0111, 0x00000100,
};

static const u32 golden_settings_polaris10_a11[] =
{
	mmSDMA0_CHICKEN_BITS, 0xfc910007, 0x00810007,
	mmSDMA0_CLK_CTRL, 0xff000fff, 0x00000000,
	mmSDMA0_GFX_IB_CNTL, 0x800f0111, 0x00000100,
	mmSDMA0_RLC0_IB_CNTL, 0x800f0111, 0x00000100,
	mmSDMA0_RLC1_IB_CNTL, 0x800f0111, 0x00000100,
	mmSDMA1_CHICKEN_BITS, 0xfc910007, 0x00810007,
	mmSDMA1_CLK_CTRL, 0xff000fff, 0x00000000,
	mmSDMA1_GFX_IB_CNTL, 0x800f0111, 0x00000100,
	mmSDMA1_RLC0_IB_CNTL, 0x800f0111, 0x00000100,
	mmSDMA1_RLC1_IB_CNTL, 0x800f0111, 0x00000100,
};

static const u32 cz_golden_settings_a11[] =
{
	mmSDMA0_CHICKEN_BITS, 0xfc910007, 0x00810007,
	mmSDMA0_CLK_CTRL, 0xff000fff, 0x00000000,
	mmSDMA0_GFX_IB_CNTL, 0x00000100, 0x00000100,
	mmSDMA0_POWER_CNTL, 0x00000800, 0x0003c800,
	mmSDMA0_RLC0_IB_CNTL, 0x00000100, 0x00000100,
	mmSDMA0_RLC1_IB_CNTL, 0x00000100, 0x00000100,
	mmSDMA1_CHICKEN_BITS, 0xfc910007, 0x00810007,
	mmSDMA1_CLK_CTRL, 0xff000fff, 0x00000000,
	mmSDMA1_GFX_IB_CNTL, 0x00000100, 0x00000100,
	mmSDMA1_POWER_CNTL, 0x00000800, 0x0003c800,
	mmSDMA1_RLC0_IB_CNTL, 0x00000100, 0x00000100,
	mmSDMA1_RLC1_IB_CNTL, 0x00000100, 0x00000100,
};

static const u32 cz_mgcg_cgcg_init[] =
{
	mmSDMA0_CLK_CTRL, 0xff000ff0, 0x00000100,
	mmSDMA1_CLK_CTRL, 0xff000ff0, 0x00000100
};

static const u32 stoney_golden_settings_a11[] =
{
	mmSDMA0_GFX_IB_CNTL, 0x00000100, 0x00000100,
	mmSDMA0_POWER_CNTL, 0x00000800, 0x0003c800,
	mmSDMA0_RLC0_IB_CNTL, 0x00000100, 0x00000100,
	mmSDMA0_RLC1_IB_CNTL, 0x00000100, 0x00000100,
};

static const u32 stoney_mgcg_cgcg_init[] =
{
	mmSDMA0_CLK_CTRL, 0xffffffff, 0x00000100,
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

static void sdma_v3_0_init_golden_registers(struct amdgpu_device *adev)
{
	switch (adev->asic_type) {
	case CHIP_FIJI:
		amdgpu_device_program_register_sequence(adev,
							fiji_mgcg_cgcg_init,
							ARRAY_SIZE(fiji_mgcg_cgcg_init));
		amdgpu_device_program_register_sequence(adev,
							golden_settings_fiji_a10,
							ARRAY_SIZE(golden_settings_fiji_a10));
		break;
	case CHIP_TONGA:
		amdgpu_device_program_register_sequence(adev,
							tonga_mgcg_cgcg_init,
							ARRAY_SIZE(tonga_mgcg_cgcg_init));
		amdgpu_device_program_register_sequence(adev,
							golden_settings_tonga_a11,
							ARRAY_SIZE(golden_settings_tonga_a11));
		break;
	case CHIP_POLARIS11:
	case CHIP_POLARIS12:
	case CHIP_VEGAM:
		amdgpu_device_program_register_sequence(adev,
							golden_settings_polaris11_a11,
							ARRAY_SIZE(golden_settings_polaris11_a11));
		break;
	case CHIP_POLARIS10:
		amdgpu_device_program_register_sequence(adev,
							golden_settings_polaris10_a11,
							ARRAY_SIZE(golden_settings_polaris10_a11));
		break;
	case CHIP_CARRIZO:
		amdgpu_device_program_register_sequence(adev,
							cz_mgcg_cgcg_init,
							ARRAY_SIZE(cz_mgcg_cgcg_init));
		amdgpu_device_program_register_sequence(adev,
							cz_golden_settings_a11,
							ARRAY_SIZE(cz_golden_settings_a11));
		break;
	case CHIP_STONEY:
		amdgpu_device_program_register_sequence(adev,
							stoney_mgcg_cgcg_init,
							ARRAY_SIZE(stoney_mgcg_cgcg_init));
		amdgpu_device_program_register_sequence(adev,
							stoney_golden_settings_a11,
							ARRAY_SIZE(stoney_golden_settings_a11));
		break;
	default:
		break;
	}
}

static void sdma_v3_0_free_microcode(struct amdgpu_device *adev)
{
	int i;

	for (i = 0; i < adev->sdma.num_instances; i++)
		amdgpu_ucode_release(&adev->sdma.instance[i].fw);
}

/**
 * sdma_v3_0_init_microcode - load ucode images from disk
 *
 * @adev: amdgpu_device pointer
 *
 * Use the firmware interface to load the ucode images into
 * the driver (not loaded into hw).
 * Returns 0 on success, error on failure.
 */
static int sdma_v3_0_init_microcode(struct amdgpu_device *adev)
{
	const char *chip_name;
	int err = 0, i;
	struct amdgpu_firmware_info *info = NULL;
	const struct common_firmware_header *header = NULL;
	const struct sdma_firmware_header_v1_0 *hdr;

	DRM_DEBUG("\n");

	switch (adev->asic_type) {
	case CHIP_TONGA:
		chip_name = "tonga";
		break;
	case CHIP_FIJI:
		chip_name = "fiji";
		break;
	case CHIP_POLARIS10:
		chip_name = "polaris10";
		break;
	case CHIP_POLARIS11:
		chip_name = "polaris11";
		break;
	case CHIP_POLARIS12:
		chip_name = "polaris12";
		break;
	case CHIP_VEGAM:
		chip_name = "vegam";
		break;
	case CHIP_CARRIZO:
		chip_name = "carrizo";
		break;
	case CHIP_STONEY:
		chip_name = "stoney";
		break;
	default: BUG();
	}

	for (i = 0; i < adev->sdma.num_instances; i++) {
		if (i == 0)
			err = amdgpu_ucode_request(adev, &adev->sdma.instance[i].fw,
						   "amdgpu/%s_sdma.bin", chip_name);
		else
			err = amdgpu_ucode_request(adev, &adev->sdma.instance[i].fw,
						   "amdgpu/%s_sdma1.bin", chip_name);
		if (err)
			goto out;
		hdr = (const struct sdma_firmware_header_v1_0 *)adev->sdma.instance[i].fw->data;
		adev->sdma.instance[i].fw_version = le32_to_cpu(hdr->header.ucode_version);
		adev->sdma.instance[i].feature_version = le32_to_cpu(hdr->ucode_feature_version);
		if (adev->sdma.instance[i].feature_version >= 20)
			adev->sdma.instance[i].burst_nop = true;

		info = &adev->firmware.ucode[AMDGPU_UCODE_ID_SDMA0 + i];
		info->ucode_id = AMDGPU_UCODE_ID_SDMA0 + i;
		info->fw = adev->sdma.instance[i].fw;
		header = (const struct common_firmware_header *)info->fw->data;
		adev->firmware.fw_size +=
			ALIGN(le32_to_cpu(header->ucode_size_bytes), PAGE_SIZE);

	}
out:
	if (err) {
		pr_err("sdma_v3_0: Failed to load firmware \"%s_sdma%s.bin\"\n",
		       chip_name, i == 0 ? "" : "1");
		for (i = 0; i < adev->sdma.num_instances; i++)
			amdgpu_ucode_release(&adev->sdma.instance[i].fw);
	}
	return err;
}

/**
 * sdma_v3_0_ring_get_rptr - get the current read pointer
 *
 * @ring: amdgpu ring pointer
 *
 * Get the current rptr from the hardware (VI+).
 */
static uint64_t sdma_v3_0_ring_get_rptr(struct amdgpu_ring *ring)
{
	/* XXX check if swapping is necessary on BE */
	return *ring->rptr_cpu_addr >> 2;
}

/**
 * sdma_v3_0_ring_get_wptr - get the current write pointer
 *
 * @ring: amdgpu ring pointer
 *
 * Get the current wptr from the hardware (VI+).
 */
static uint64_t sdma_v3_0_ring_get_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	u32 wptr;

	if (ring->use_doorbell || ring->use_pollmem) {
		/* XXX check if swapping is necessary on BE */
		wptr = *ring->wptr_cpu_addr >> 2;
	} else {
		wptr = RREG32(mmSDMA0_GFX_RB_WPTR + sdma_offsets[ring->me]) >> 2;
	}

	return wptr;
}

/**
 * sdma_v3_0_ring_set_wptr - commit the write pointer
 *
 * @ring: amdgpu ring pointer
 *
 * Write the wptr back to the hardware (VI+).
 */
static void sdma_v3_0_ring_set_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring->use_doorbell) {
		u32 *wb = (u32 *)ring->wptr_cpu_addr;
		/* XXX check if swapping is necessary on BE */
		WRITE_ONCE(*wb, ring->wptr << 2);
		WDOORBELL32(ring->doorbell_index, ring->wptr << 2);
	} else if (ring->use_pollmem) {
		u32 *wb = (u32 *)ring->wptr_cpu_addr;

		WRITE_ONCE(*wb, ring->wptr << 2);
	} else {
		WREG32(mmSDMA0_GFX_RB_WPTR + sdma_offsets[ring->me], ring->wptr << 2);
	}
}

static void sdma_v3_0_ring_insert_nop(struct amdgpu_ring *ring, uint32_t count)
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
 * sdma_v3_0_ring_emit_ib - Schedule an IB on the DMA engine
 *
 * @ring: amdgpu ring pointer
 * @job: job to retrieve vmid from
 * @ib: IB object to schedule
 * @flags: unused
 *
 * Schedule an IB in the DMA ring (VI).
 */
static void sdma_v3_0_ring_emit_ib(struct amdgpu_ring *ring,
				   struct amdgpu_job *job,
				   struct amdgpu_ib *ib,
				   uint32_t flags)
{
	unsigned vmid = AMDGPU_JOB_GET_VMID(job);

	/* IB packet must end on a 8 DW boundary */
	sdma_v3_0_ring_insert_nop(ring, (2 - lower_32_bits(ring->wptr)) & 7);

	amdgpu_ring_write(ring, SDMA_PKT_HEADER_OP(SDMA_OP_INDIRECT) |
			  SDMA_PKT_INDIRECT_HEADER_VMID(vmid & 0xf));
	/* base must be 32 byte aligned */
	amdgpu_ring_write(ring, lower_32_bits(ib->gpu_addr) & 0xffffffe0);
	amdgpu_ring_write(ring, upper_32_bits(ib->gpu_addr));
	amdgpu_ring_write(ring, ib->length_dw);
	amdgpu_ring_write(ring, 0);
	amdgpu_ring_write(ring, 0);

}

/**
 * sdma_v3_0_ring_emit_hdp_flush - emit an hdp flush on the DMA ring
 *
 * @ring: amdgpu ring pointer
 *
 * Emit an hdp flush packet on the requested DMA ring.
 */
static void sdma_v3_0_ring_emit_hdp_flush(struct amdgpu_ring *ring)
{
	u32 ref_and_mask = 0;

	if (ring->me == 0)
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

/**
 * sdma_v3_0_ring_emit_fence - emit a fence on the DMA ring
 *
 * @ring: amdgpu ring pointer
 * @addr: address
 * @seq: sequence number
 * @flags: fence related flags
 *
 * Add a DMA fence packet to the ring to write
 * the fence seq number and DMA trap packet to generate
 * an interrupt if needed (VI).
 */
static void sdma_v3_0_ring_emit_fence(struct amdgpu_ring *ring, u64 addr, u64 seq,
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
 * sdma_v3_0_gfx_stop - stop the gfx async dma engines
 *
 * @adev: amdgpu_device pointer
 *
 * Stop the gfx async dma ring buffers (VI).
 */
static void sdma_v3_0_gfx_stop(struct amdgpu_device *adev)
{
	u32 rb_cntl, ib_cntl;
	int i;

	for (i = 0; i < adev->sdma.num_instances; i++) {
		rb_cntl = RREG32(mmSDMA0_GFX_RB_CNTL + sdma_offsets[i]);
		rb_cntl = REG_SET_FIELD(rb_cntl, SDMA0_GFX_RB_CNTL, RB_ENABLE, 0);
		WREG32(mmSDMA0_GFX_RB_CNTL + sdma_offsets[i], rb_cntl);
		ib_cntl = RREG32(mmSDMA0_GFX_IB_CNTL + sdma_offsets[i]);
		ib_cntl = REG_SET_FIELD(ib_cntl, SDMA0_GFX_IB_CNTL, IB_ENABLE, 0);
		WREG32(mmSDMA0_GFX_IB_CNTL + sdma_offsets[i], ib_cntl);
	}
}

/**
 * sdma_v3_0_rlc_stop - stop the compute async dma engines
 *
 * @adev: amdgpu_device pointer
 *
 * Stop the compute async dma queues (VI).
 */
static void sdma_v3_0_rlc_stop(struct amdgpu_device *adev)
{
	/* XXX todo */
}

/**
 * sdma_v3_0_ctx_switch_enable - stop the async dma engines context switch
 *
 * @adev: amdgpu_device pointer
 * @enable: enable/disable the DMA MEs context switch.
 *
 * Halt or unhalt the async dma engines context switch (VI).
 */
static void sdma_v3_0_ctx_switch_enable(struct amdgpu_device *adev, bool enable)
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
		f32_cntl = RREG32(mmSDMA0_CNTL + sdma_offsets[i]);
		if (enable) {
			f32_cntl = REG_SET_FIELD(f32_cntl, SDMA0_CNTL,
					AUTO_CTXSW_ENABLE, 1);
			f32_cntl = REG_SET_FIELD(f32_cntl, SDMA0_CNTL,
					ATC_L1_ENABLE, 1);
			if (amdgpu_sdma_phase_quantum) {
				WREG32(mmSDMA0_PHASE0_QUANTUM + sdma_offsets[i],
				       phase_quantum);
				WREG32(mmSDMA0_PHASE1_QUANTUM + sdma_offsets[i],
				       phase_quantum);
			}
		} else {
			f32_cntl = REG_SET_FIELD(f32_cntl, SDMA0_CNTL,
					AUTO_CTXSW_ENABLE, 0);
			f32_cntl = REG_SET_FIELD(f32_cntl, SDMA0_CNTL,
					ATC_L1_ENABLE, 1);
		}

		WREG32(mmSDMA0_CNTL + sdma_offsets[i], f32_cntl);
	}
}

/**
 * sdma_v3_0_enable - stop the async dma engines
 *
 * @adev: amdgpu_device pointer
 * @enable: enable/disable the DMA MEs.
 *
 * Halt or unhalt the async dma engines (VI).
 */
static void sdma_v3_0_enable(struct amdgpu_device *adev, bool enable)
{
	u32 f32_cntl;
	int i;

	if (!enable) {
		sdma_v3_0_gfx_stop(adev);
		sdma_v3_0_rlc_stop(adev);
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
 * sdma_v3_0_gfx_resume - setup and start the async dma engines
 *
 * @adev: amdgpu_device pointer
 *
 * Set up the gfx DMA ring buffers and enable them (VI).
 * Returns 0 for success, error for failure.
 */
static int sdma_v3_0_gfx_resume(struct amdgpu_device *adev)
{
	struct amdgpu_ring *ring;
	u32 rb_cntl, ib_cntl, wptr_poll_cntl;
	u32 rb_bufsz;
	u32 doorbell;
	u64 wptr_gpu_addr;
	int i, j, r;

	for (i = 0; i < adev->sdma.num_instances; i++) {
		ring = &adev->sdma.instance[i].ring;
		amdgpu_ring_clear_ring(ring);

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
		ring->wptr = 0;
		WREG32(mmSDMA0_GFX_RB_RPTR + sdma_offsets[i], 0);
		sdma_v3_0_ring_set_wptr(ring);
		WREG32(mmSDMA0_GFX_IB_RPTR + sdma_offsets[i], 0);
		WREG32(mmSDMA0_GFX_IB_OFFSET + sdma_offsets[i], 0);

		/* set the wb address whether it's enabled or not */
		WREG32(mmSDMA0_GFX_RB_RPTR_ADDR_HI + sdma_offsets[i],
		       upper_32_bits(ring->rptr_gpu_addr) & 0xFFFFFFFF);
		WREG32(mmSDMA0_GFX_RB_RPTR_ADDR_LO + sdma_offsets[i],
		       lower_32_bits(ring->rptr_gpu_addr) & 0xFFFFFFFC);

		rb_cntl = REG_SET_FIELD(rb_cntl, SDMA0_GFX_RB_CNTL, RPTR_WRITEBACK_ENABLE, 1);

		WREG32(mmSDMA0_GFX_RB_BASE + sdma_offsets[i], ring->gpu_addr >> 8);
		WREG32(mmSDMA0_GFX_RB_BASE_HI + sdma_offsets[i], ring->gpu_addr >> 40);

		doorbell = RREG32(mmSDMA0_GFX_DOORBELL + sdma_offsets[i]);

		if (ring->use_doorbell) {
			doorbell = REG_SET_FIELD(doorbell, SDMA0_GFX_DOORBELL,
						 OFFSET, ring->doorbell_index);
			doorbell = REG_SET_FIELD(doorbell, SDMA0_GFX_DOORBELL, ENABLE, 1);
		} else {
			doorbell = REG_SET_FIELD(doorbell, SDMA0_GFX_DOORBELL, ENABLE, 0);
		}
		WREG32(mmSDMA0_GFX_DOORBELL + sdma_offsets[i], doorbell);

		/* setup the wptr shadow polling */
		wptr_gpu_addr = ring->wptr_gpu_addr;

		WREG32(mmSDMA0_GFX_RB_WPTR_POLL_ADDR_LO + sdma_offsets[i],
		       lower_32_bits(wptr_gpu_addr));
		WREG32(mmSDMA0_GFX_RB_WPTR_POLL_ADDR_HI + sdma_offsets[i],
		       upper_32_bits(wptr_gpu_addr));
		wptr_poll_cntl = RREG32(mmSDMA0_GFX_RB_WPTR_POLL_CNTL + sdma_offsets[i]);
		if (ring->use_pollmem) {
			/*wptr polling is not enough fast, directly clean the wptr register */
			WREG32(mmSDMA0_GFX_RB_WPTR + sdma_offsets[i], 0);
			wptr_poll_cntl = REG_SET_FIELD(wptr_poll_cntl,
						       SDMA0_GFX_RB_WPTR_POLL_CNTL,
						       ENABLE, 1);
		} else {
			wptr_poll_cntl = REG_SET_FIELD(wptr_poll_cntl,
						       SDMA0_GFX_RB_WPTR_POLL_CNTL,
						       ENABLE, 0);
		}
		WREG32(mmSDMA0_GFX_RB_WPTR_POLL_CNTL + sdma_offsets[i], wptr_poll_cntl);

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
	}

	/* unhalt the MEs */
	sdma_v3_0_enable(adev, true);
	/* enable sdma ring preemption */
	sdma_v3_0_ctx_switch_enable(adev, true);

	for (i = 0; i < adev->sdma.num_instances; i++) {
		ring = &adev->sdma.instance[i].ring;
		r = amdgpu_ring_test_helper(ring);
		if (r)
			return r;
	}

	return 0;
}

/**
 * sdma_v3_0_rlc_resume - setup and start the async dma engines
 *
 * @adev: amdgpu_device pointer
 *
 * Set up the compute DMA queues and enable them (VI).
 * Returns 0 for success, error for failure.
 */
static int sdma_v3_0_rlc_resume(struct amdgpu_device *adev)
{
	/* XXX todo */
	return 0;
}

/**
 * sdma_v3_0_start - setup and start the async dma engines
 *
 * @adev: amdgpu_device pointer
 *
 * Set up the DMA engines and enable them (VI).
 * Returns 0 for success, error for failure.
 */
static int sdma_v3_0_start(struct amdgpu_device *adev)
{
	int r;

	/* disable sdma engine before programing it */
	sdma_v3_0_ctx_switch_enable(adev, false);
	sdma_v3_0_enable(adev, false);

	/* start the gfx rings and rlc compute queues */
	r = sdma_v3_0_gfx_resume(adev);
	if (r)
		return r;
	r = sdma_v3_0_rlc_resume(adev);
	if (r)
		return r;

	return 0;
}

/**
 * sdma_v3_0_ring_test_ring - simple async dma engine test
 *
 * @ring: amdgpu_ring structure holding ring information
 *
 * Test the DMA engine by writing using it to write an
 * value to memory. (VI).
 * Returns 0 for success, error for failure.
 */
static int sdma_v3_0_ring_test_ring(struct amdgpu_ring *ring)
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
	amdgpu_ring_write(ring, SDMA_PKT_WRITE_UNTILED_DW_3_COUNT(1));
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
 * sdma_v3_0_ring_test_ib - test an IB on the DMA engine
 *
 * @ring: amdgpu_ring structure holding ring information
 * @timeout: timeout value in jiffies, or MAX_SCHEDULE_TIMEOUT
 *
 * Test a simple IB in the DMA ring (VI).
 * Returns 0 on success, error on failure.
 */
static int sdma_v3_0_ring_test_ib(struct amdgpu_ring *ring, long timeout)
{
	struct amdgpu_device *adev = ring->adev;
	struct amdgpu_ib ib;
	struct dma_fence *f = NULL;
	unsigned index;
	u32 tmp = 0;
	u64 gpu_addr;
	long r;

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
	ib.ptr[3] = SDMA_PKT_WRITE_UNTILED_DW_3_COUNT(1);
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
	amdgpu_ib_free(adev, &ib, NULL);
	dma_fence_put(f);
err0:
	amdgpu_device_wb_free(adev, index);
	return r;
}

/**
 * sdma_v3_0_vm_copy_pte - update PTEs by copying them from the GART
 *
 * @ib: indirect buffer to fill with commands
 * @pe: addr of the page entry
 * @src: src addr to copy from
 * @count: number of page entries to update
 *
 * Update PTEs by copying them from the GART using sDMA (CIK).
 */
static void sdma_v3_0_vm_copy_pte(struct amdgpu_ib *ib,
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
 * sdma_v3_0_vm_write_pte - update PTEs by writing them manually
 *
 * @ib: indirect buffer to fill with commands
 * @pe: addr of the page entry
 * @value: dst addr to write into pe
 * @count: number of page entries to update
 * @incr: increase next addr by incr bytes
 *
 * Update PTEs by writing them manually using sDMA (CIK).
 */
static void sdma_v3_0_vm_write_pte(struct amdgpu_ib *ib, uint64_t pe,
				   uint64_t value, unsigned count,
				   uint32_t incr)
{
	unsigned ndw = count * 2;

	ib->ptr[ib->length_dw++] = SDMA_PKT_HEADER_OP(SDMA_OP_WRITE) |
		SDMA_PKT_HEADER_SUB_OP(SDMA_SUBOP_WRITE_LINEAR);
	ib->ptr[ib->length_dw++] = lower_32_bits(pe);
	ib->ptr[ib->length_dw++] = upper_32_bits(pe);
	ib->ptr[ib->length_dw++] = ndw;
	for (; ndw > 0; ndw -= 2) {
		ib->ptr[ib->length_dw++] = lower_32_bits(value);
		ib->ptr[ib->length_dw++] = upper_32_bits(value);
		value += incr;
	}
}

/**
 * sdma_v3_0_vm_set_pte_pde - update the page tables using sDMA
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
static void sdma_v3_0_vm_set_pte_pde(struct amdgpu_ib *ib, uint64_t pe,
				     uint64_t addr, unsigned count,
				     uint32_t incr, uint64_t flags)
{
	/* for physically contiguous pages (vram) */
	ib->ptr[ib->length_dw++] = SDMA_PKT_HEADER_OP(SDMA_OP_GEN_PTEPDE);
	ib->ptr[ib->length_dw++] = lower_32_bits(pe); /* dst addr */
	ib->ptr[ib->length_dw++] = upper_32_bits(pe);
	ib->ptr[ib->length_dw++] = lower_32_bits(flags); /* mask */
	ib->ptr[ib->length_dw++] = upper_32_bits(flags);
	ib->ptr[ib->length_dw++] = lower_32_bits(addr); /* value */
	ib->ptr[ib->length_dw++] = upper_32_bits(addr);
	ib->ptr[ib->length_dw++] = incr; /* increment size */
	ib->ptr[ib->length_dw++] = 0;
	ib->ptr[ib->length_dw++] = count; /* number of entries */
}

/**
 * sdma_v3_0_ring_pad_ib - pad the IB to the required number of dw
 *
 * @ring: amdgpu_ring structure holding ring information
 * @ib: indirect buffer to fill with padding
 *
 */
static void sdma_v3_0_ring_pad_ib(struct amdgpu_ring *ring, struct amdgpu_ib *ib)
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
 * sdma_v3_0_ring_emit_pipeline_sync - sync the pipeline
 *
 * @ring: amdgpu_ring pointer
 *
 * Make sure all previous operations are completed (CIK).
 */
static void sdma_v3_0_ring_emit_pipeline_sync(struct amdgpu_ring *ring)
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
 * sdma_v3_0_ring_emit_vm_flush - cik vm flush using sDMA
 *
 * @ring: amdgpu_ring pointer
 * @vmid: vmid number to use
 * @pd_addr: address
 *
 * Update the page table base and flush the VM TLB
 * using sDMA (VI).
 */
static void sdma_v3_0_ring_emit_vm_flush(struct amdgpu_ring *ring,
					 unsigned vmid, uint64_t pd_addr)
{
	amdgpu_gmc_emit_flush_gpu_tlb(ring, vmid, pd_addr);

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

static void sdma_v3_0_ring_emit_wreg(struct amdgpu_ring *ring,
				     uint32_t reg, uint32_t val)
{
	amdgpu_ring_write(ring, SDMA_PKT_HEADER_OP(SDMA_OP_SRBM_WRITE) |
			  SDMA_PKT_SRBM_WRITE_HEADER_BYTE_EN(0xf));
	amdgpu_ring_write(ring, reg);
	amdgpu_ring_write(ring, val);
}

static int sdma_v3_0_early_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	int r;

	switch (adev->asic_type) {
	case CHIP_STONEY:
		adev->sdma.num_instances = 1;
		break;
	default:
		adev->sdma.num_instances = SDMA_MAX_INSTANCE;
		break;
	}

	r = sdma_v3_0_init_microcode(adev);
	if (r)
		return r;

	sdma_v3_0_set_ring_funcs(adev);
	sdma_v3_0_set_buffer_funcs(adev);
	sdma_v3_0_set_vm_pte_funcs(adev);
	sdma_v3_0_set_irq_funcs(adev);

	return 0;
}

static int sdma_v3_0_sw_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_ring *ring;
	int r, i;
	struct amdgpu_device *adev = ip_block->adev;

	/* SDMA trap event */
	r = amdgpu_irq_add_id(adev, AMDGPU_IRQ_CLIENTID_LEGACY, VISLANDS30_IV_SRCID_SDMA_TRAP,
			      &adev->sdma.trap_irq);
	if (r)
		return r;

	/* SDMA Privileged inst */
	r = amdgpu_irq_add_id(adev, AMDGPU_IRQ_CLIENTID_LEGACY, 241,
			      &adev->sdma.illegal_inst_irq);
	if (r)
		return r;

	/* SDMA Privileged inst */
	r = amdgpu_irq_add_id(adev, AMDGPU_IRQ_CLIENTID_LEGACY, VISLANDS30_IV_SRCID_SDMA_SRBM_WRITE,
			      &adev->sdma.illegal_inst_irq);
	if (r)
		return r;

	for (i = 0; i < adev->sdma.num_instances; i++) {
		ring = &adev->sdma.instance[i].ring;
		ring->ring_obj = NULL;
		if (!amdgpu_sriov_vf(adev)) {
			ring->use_doorbell = true;
			ring->doorbell_index = adev->doorbell_index.sdma_engine[i];
		} else {
			ring->use_pollmem = true;
		}

		sprintf(ring->name, "sdma%d", i);
		r = amdgpu_ring_init(adev, ring, 1024, &adev->sdma.trap_irq,
				     (i == 0) ? AMDGPU_SDMA_IRQ_INSTANCE0 :
				     AMDGPU_SDMA_IRQ_INSTANCE1,
				     AMDGPU_RING_PRIO_DEFAULT, NULL);
		if (r)
			return r;
	}

	return r;
}

static int sdma_v3_0_sw_fini(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	int i;

	for (i = 0; i < adev->sdma.num_instances; i++)
		amdgpu_ring_fini(&adev->sdma.instance[i].ring);

	sdma_v3_0_free_microcode(adev);
	return 0;
}

static int sdma_v3_0_hw_init(struct amdgpu_ip_block *ip_block)
{
	int r;
	struct amdgpu_device *adev = ip_block->adev;

	sdma_v3_0_init_golden_registers(adev);

	r = sdma_v3_0_start(adev);
	if (r)
		return r;

	return r;
}

static int sdma_v3_0_hw_fini(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;

	sdma_v3_0_ctx_switch_enable(adev, false);
	sdma_v3_0_enable(adev, false);

	return 0;
}

static int sdma_v3_0_suspend(struct amdgpu_ip_block *ip_block)
{
	return sdma_v3_0_hw_fini(ip_block);
}

static int sdma_v3_0_resume(struct amdgpu_ip_block *ip_block)
{
	return sdma_v3_0_hw_init(ip_block);
}

static bool sdma_v3_0_is_idle(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	u32 tmp = RREG32(mmSRBM_STATUS2);

	if (tmp & (SRBM_STATUS2__SDMA_BUSY_MASK |
		   SRBM_STATUS2__SDMA1_BUSY_MASK))
	    return false;

	return true;
}

static int sdma_v3_0_wait_for_idle(struct amdgpu_ip_block *ip_block)
{
	unsigned i;
	u32 tmp;
	struct amdgpu_device *adev = ip_block->adev;

	for (i = 0; i < adev->usec_timeout; i++) {
		tmp = RREG32(mmSRBM_STATUS2) & (SRBM_STATUS2__SDMA_BUSY_MASK |
				SRBM_STATUS2__SDMA1_BUSY_MASK);

		if (!tmp)
			return 0;
		udelay(1);
	}
	return -ETIMEDOUT;
}

static bool sdma_v3_0_check_soft_reset(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	u32 srbm_soft_reset = 0;
	u32 tmp = RREG32(mmSRBM_STATUS2);

	if ((tmp & SRBM_STATUS2__SDMA_BUSY_MASK) ||
	    (tmp & SRBM_STATUS2__SDMA1_BUSY_MASK)) {
		srbm_soft_reset |= SRBM_SOFT_RESET__SOFT_RESET_SDMA_MASK;
		srbm_soft_reset |= SRBM_SOFT_RESET__SOFT_RESET_SDMA1_MASK;
	}

	if (srbm_soft_reset) {
		adev->sdma.srbm_soft_reset = srbm_soft_reset;
		return true;
	} else {
		adev->sdma.srbm_soft_reset = 0;
		return false;
	}
}

static int sdma_v3_0_pre_soft_reset(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	u32 srbm_soft_reset = 0;

	if (!adev->sdma.srbm_soft_reset)
		return 0;

	srbm_soft_reset = adev->sdma.srbm_soft_reset;

	if (REG_GET_FIELD(srbm_soft_reset, SRBM_SOFT_RESET, SOFT_RESET_SDMA) ||
	    REG_GET_FIELD(srbm_soft_reset, SRBM_SOFT_RESET, SOFT_RESET_SDMA1)) {
		sdma_v3_0_ctx_switch_enable(adev, false);
		sdma_v3_0_enable(adev, false);
	}

	return 0;
}

static int sdma_v3_0_post_soft_reset(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	u32 srbm_soft_reset = 0;

	if (!adev->sdma.srbm_soft_reset)
		return 0;

	srbm_soft_reset = adev->sdma.srbm_soft_reset;

	if (REG_GET_FIELD(srbm_soft_reset, SRBM_SOFT_RESET, SOFT_RESET_SDMA) ||
	    REG_GET_FIELD(srbm_soft_reset, SRBM_SOFT_RESET, SOFT_RESET_SDMA1)) {
		sdma_v3_0_gfx_resume(adev);
		sdma_v3_0_rlc_resume(adev);
	}

	return 0;
}

static int sdma_v3_0_soft_reset(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	u32 srbm_soft_reset = 0;
	u32 tmp;

	if (!adev->sdma.srbm_soft_reset)
		return 0;

	srbm_soft_reset = adev->sdma.srbm_soft_reset;

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

static int sdma_v3_0_set_trap_irq_state(struct amdgpu_device *adev,
					struct amdgpu_irq_src *source,
					unsigned type,
					enum amdgpu_interrupt_state state)
{
	u32 sdma_cntl;

	switch (type) {
	case AMDGPU_SDMA_IRQ_INSTANCE0:
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
	case AMDGPU_SDMA_IRQ_INSTANCE1:
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

static int sdma_v3_0_process_trap_irq(struct amdgpu_device *adev,
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

static int sdma_v3_0_process_illegal_inst_irq(struct amdgpu_device *adev,
					      struct amdgpu_irq_src *source,
					      struct amdgpu_iv_entry *entry)
{
	u8 instance_id, queue_id;

	DRM_ERROR("Illegal instruction in SDMA command stream\n");
	instance_id = (entry->ring_id & 0x3) >> 0;
	queue_id = (entry->ring_id & 0xc) >> 2;

	if (instance_id <= 1 && queue_id == 0)
		drm_sched_fault(&adev->sdma.instance[instance_id].ring.sched);
	return 0;
}

static void sdma_v3_0_update_sdma_medium_grain_clock_gating(
		struct amdgpu_device *adev,
		bool enable)
{
	uint32_t temp, data;
	int i;

	if (enable && (adev->cg_flags & AMD_CG_SUPPORT_SDMA_MGCG)) {
		for (i = 0; i < adev->sdma.num_instances; i++) {
			temp = data = RREG32(mmSDMA0_CLK_CTRL + sdma_offsets[i]);
			data &= ~(SDMA0_CLK_CTRL__SOFT_OVERRIDE7_MASK |
				  SDMA0_CLK_CTRL__SOFT_OVERRIDE6_MASK |
				  SDMA0_CLK_CTRL__SOFT_OVERRIDE5_MASK |
				  SDMA0_CLK_CTRL__SOFT_OVERRIDE4_MASK |
				  SDMA0_CLK_CTRL__SOFT_OVERRIDE3_MASK |
				  SDMA0_CLK_CTRL__SOFT_OVERRIDE2_MASK |
				  SDMA0_CLK_CTRL__SOFT_OVERRIDE1_MASK |
				  SDMA0_CLK_CTRL__SOFT_OVERRIDE0_MASK);
			if (data != temp)
				WREG32(mmSDMA0_CLK_CTRL + sdma_offsets[i], data);
		}
	} else {
		for (i = 0; i < adev->sdma.num_instances; i++) {
			temp = data = RREG32(mmSDMA0_CLK_CTRL + sdma_offsets[i]);
			data |= SDMA0_CLK_CTRL__SOFT_OVERRIDE7_MASK |
				SDMA0_CLK_CTRL__SOFT_OVERRIDE6_MASK |
				SDMA0_CLK_CTRL__SOFT_OVERRIDE5_MASK |
				SDMA0_CLK_CTRL__SOFT_OVERRIDE4_MASK |
				SDMA0_CLK_CTRL__SOFT_OVERRIDE3_MASK |
				SDMA0_CLK_CTRL__SOFT_OVERRIDE2_MASK |
				SDMA0_CLK_CTRL__SOFT_OVERRIDE1_MASK |
				SDMA0_CLK_CTRL__SOFT_OVERRIDE0_MASK;

			if (data != temp)
				WREG32(mmSDMA0_CLK_CTRL + sdma_offsets[i], data);
		}
	}
}

static void sdma_v3_0_update_sdma_medium_grain_light_sleep(
		struct amdgpu_device *adev,
		bool enable)
{
	uint32_t temp, data;
	int i;

	if (enable && (adev->cg_flags & AMD_CG_SUPPORT_SDMA_LS)) {
		for (i = 0; i < adev->sdma.num_instances; i++) {
			temp = data = RREG32(mmSDMA0_POWER_CNTL + sdma_offsets[i]);
			data |= SDMA0_POWER_CNTL__MEM_POWER_OVERRIDE_MASK;

			if (temp != data)
				WREG32(mmSDMA0_POWER_CNTL + sdma_offsets[i], data);
		}
	} else {
		for (i = 0; i < adev->sdma.num_instances; i++) {
			temp = data = RREG32(mmSDMA0_POWER_CNTL + sdma_offsets[i]);
			data &= ~SDMA0_POWER_CNTL__MEM_POWER_OVERRIDE_MASK;

			if (temp != data)
				WREG32(mmSDMA0_POWER_CNTL + sdma_offsets[i], data);
		}
	}
}

static int sdma_v3_0_set_clockgating_state(void *handle,
					  enum amd_clockgating_state state)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (amdgpu_sriov_vf(adev))
		return 0;

	switch (adev->asic_type) {
	case CHIP_FIJI:
	case CHIP_CARRIZO:
	case CHIP_STONEY:
		sdma_v3_0_update_sdma_medium_grain_clock_gating(adev,
				state == AMD_CG_STATE_GATE);
		sdma_v3_0_update_sdma_medium_grain_light_sleep(adev,
				state == AMD_CG_STATE_GATE);
		break;
	default:
		break;
	}
	return 0;
}

static int sdma_v3_0_set_powergating_state(struct amdgpu_ip_block *ip_block,
					  enum amd_powergating_state state)
{
	return 0;
}

static void sdma_v3_0_get_clockgating_state(void *handle, u64 *flags)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int data;

	if (amdgpu_sriov_vf(adev))
		*flags = 0;

	/* AMD_CG_SUPPORT_SDMA_MGCG */
	data = RREG32(mmSDMA0_CLK_CTRL + sdma_offsets[0]);
	if (!(data & SDMA0_CLK_CTRL__SOFT_OVERRIDE0_MASK))
		*flags |= AMD_CG_SUPPORT_SDMA_MGCG;

	/* AMD_CG_SUPPORT_SDMA_LS */
	data = RREG32(mmSDMA0_POWER_CNTL + sdma_offsets[0]);
	if (data & SDMA0_POWER_CNTL__MEM_POWER_OVERRIDE_MASK)
		*flags |= AMD_CG_SUPPORT_SDMA_LS;
}

static const struct amd_ip_funcs sdma_v3_0_ip_funcs = {
	.name = "sdma_v3_0",
	.early_init = sdma_v3_0_early_init,
	.sw_init = sdma_v3_0_sw_init,
	.sw_fini = sdma_v3_0_sw_fini,
	.hw_init = sdma_v3_0_hw_init,
	.hw_fini = sdma_v3_0_hw_fini,
	.suspend = sdma_v3_0_suspend,
	.resume = sdma_v3_0_resume,
	.is_idle = sdma_v3_0_is_idle,
	.wait_for_idle = sdma_v3_0_wait_for_idle,
	.check_soft_reset = sdma_v3_0_check_soft_reset,
	.pre_soft_reset = sdma_v3_0_pre_soft_reset,
	.post_soft_reset = sdma_v3_0_post_soft_reset,
	.soft_reset = sdma_v3_0_soft_reset,
	.set_clockgating_state = sdma_v3_0_set_clockgating_state,
	.set_powergating_state = sdma_v3_0_set_powergating_state,
	.get_clockgating_state = sdma_v3_0_get_clockgating_state,
};

static const struct amdgpu_ring_funcs sdma_v3_0_ring_funcs = {
	.type = AMDGPU_RING_TYPE_SDMA,
	.align_mask = 0xf,
	.nop = SDMA_PKT_NOP_HEADER_OP(SDMA_OP_NOP),
	.support_64bit_ptrs = false,
	.secure_submission_supported = true,
	.get_rptr = sdma_v3_0_ring_get_rptr,
	.get_wptr = sdma_v3_0_ring_get_wptr,
	.set_wptr = sdma_v3_0_ring_set_wptr,
	.emit_frame_size =
		6 + /* sdma_v3_0_ring_emit_hdp_flush */
		3 + /* hdp invalidate */
		6 + /* sdma_v3_0_ring_emit_pipeline_sync */
		VI_FLUSH_GPU_TLB_NUM_WREG * 3 + 6 + /* sdma_v3_0_ring_emit_vm_flush */
		10 + 10 + 10, /* sdma_v3_0_ring_emit_fence x3 for user fence, vm fence */
	.emit_ib_size = 7 + 6, /* sdma_v3_0_ring_emit_ib */
	.emit_ib = sdma_v3_0_ring_emit_ib,
	.emit_fence = sdma_v3_0_ring_emit_fence,
	.emit_pipeline_sync = sdma_v3_0_ring_emit_pipeline_sync,
	.emit_vm_flush = sdma_v3_0_ring_emit_vm_flush,
	.emit_hdp_flush = sdma_v3_0_ring_emit_hdp_flush,
	.test_ring = sdma_v3_0_ring_test_ring,
	.test_ib = sdma_v3_0_ring_test_ib,
	.insert_nop = sdma_v3_0_ring_insert_nop,
	.pad_ib = sdma_v3_0_ring_pad_ib,
	.emit_wreg = sdma_v3_0_ring_emit_wreg,
};

static void sdma_v3_0_set_ring_funcs(struct amdgpu_device *adev)
{
	int i;

	for (i = 0; i < adev->sdma.num_instances; i++) {
		adev->sdma.instance[i].ring.funcs = &sdma_v3_0_ring_funcs;
		adev->sdma.instance[i].ring.me = i;
	}
}

static const struct amdgpu_irq_src_funcs sdma_v3_0_trap_irq_funcs = {
	.set = sdma_v3_0_set_trap_irq_state,
	.process = sdma_v3_0_process_trap_irq,
};

static const struct amdgpu_irq_src_funcs sdma_v3_0_illegal_inst_irq_funcs = {
	.process = sdma_v3_0_process_illegal_inst_irq,
};

static void sdma_v3_0_set_irq_funcs(struct amdgpu_device *adev)
{
	adev->sdma.trap_irq.num_types = AMDGPU_SDMA_IRQ_LAST;
	adev->sdma.trap_irq.funcs = &sdma_v3_0_trap_irq_funcs;
	adev->sdma.illegal_inst_irq.funcs = &sdma_v3_0_illegal_inst_irq_funcs;
}

/**
 * sdma_v3_0_emit_copy_buffer - copy buffer using the sDMA engine
 *
 * @ib: indirect buffer to copy to
 * @src_offset: src GPU address
 * @dst_offset: dst GPU address
 * @byte_count: number of bytes to xfer
 * @copy_flags: unused
 *
 * Copy GPU buffers using the DMA engine (VI).
 * Used by the amdgpu ttm implementation to move pages if
 * registered as the asic copy callback.
 */
static void sdma_v3_0_emit_copy_buffer(struct amdgpu_ib *ib,
				       uint64_t src_offset,
				       uint64_t dst_offset,
				       uint32_t byte_count,
				       uint32_t copy_flags)
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
 * sdma_v3_0_emit_fill_buffer - fill buffer using the sDMA engine
 *
 * @ib: indirect buffer to copy to
 * @src_data: value to write to buffer
 * @dst_offset: dst GPU address
 * @byte_count: number of bytes to xfer
 *
 * Fill GPU buffers using the DMA engine (VI).
 */
static void sdma_v3_0_emit_fill_buffer(struct amdgpu_ib *ib,
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

static const struct amdgpu_buffer_funcs sdma_v3_0_buffer_funcs = {
	.copy_max_bytes = 0x3fffe0, /* not 0x3fffff due to HW limitation */
	.copy_num_dw = 7,
	.emit_copy_buffer = sdma_v3_0_emit_copy_buffer,

	.fill_max_bytes = 0x3fffe0, /* not 0x3fffff due to HW limitation */
	.fill_num_dw = 5,
	.emit_fill_buffer = sdma_v3_0_emit_fill_buffer,
};

static void sdma_v3_0_set_buffer_funcs(struct amdgpu_device *adev)
{
	adev->mman.buffer_funcs = &sdma_v3_0_buffer_funcs;
	adev->mman.buffer_funcs_ring = &adev->sdma.instance[0].ring;
}

static const struct amdgpu_vm_pte_funcs sdma_v3_0_vm_pte_funcs = {
	.copy_pte_num_dw = 7,
	.copy_pte = sdma_v3_0_vm_copy_pte,

	.write_pte = sdma_v3_0_vm_write_pte,
	.set_pte_pde = sdma_v3_0_vm_set_pte_pde,
};

static void sdma_v3_0_set_vm_pte_funcs(struct amdgpu_device *adev)
{
	unsigned i;

	adev->vm_manager.vm_pte_funcs = &sdma_v3_0_vm_pte_funcs;
	for (i = 0; i < adev->sdma.num_instances; i++) {
		adev->vm_manager.vm_pte_scheds[i] =
			 &adev->sdma.instance[i].ring.sched;
	}
	adev->vm_manager.vm_pte_num_scheds = adev->sdma.num_instances;
}

const struct amdgpu_ip_block_version sdma_v3_0_ip_block =
{
	.type = AMD_IP_BLOCK_TYPE_SDMA,
	.major = 3,
	.minor = 0,
	.rev = 0,
	.funcs = &sdma_v3_0_ip_funcs,
};

const struct amdgpu_ip_block_version sdma_v3_1_ip_block =
{
	.type = AMD_IP_BLOCK_TYPE_SDMA,
	.major = 3,
	.minor = 1,
	.rev = 0,
	.funcs = &sdma_v3_0_ip_funcs,
};
