/*
 * Copyright 2013 Advanced Micro Devices, Inc.
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
#include "cikd.h"
#include "cik.h"

#include "bif/bif_4_1_d.h"
#include "bif/bif_4_1_sh_mask.h"

#include "gca/gfx_7_2_d.h"
#include "gca/gfx_7_2_enum.h"
#include "gca/gfx_7_2_sh_mask.h"

#include "gmc/gmc_7_1_d.h"
#include "gmc/gmc_7_1_sh_mask.h"

#include "oss/oss_2_0_d.h"
#include "oss/oss_2_0_sh_mask.h"

static const u32 sdma_offsets[SDMA_MAX_INSTANCE] =
{
	SDMA0_REGISTER_OFFSET,
	SDMA1_REGISTER_OFFSET
};

static void cik_sdma_set_ring_funcs(struct amdgpu_device *adev);
static void cik_sdma_set_irq_funcs(struct amdgpu_device *adev);
static void cik_sdma_set_buffer_funcs(struct amdgpu_device *adev);
static void cik_sdma_set_vm_pte_funcs(struct amdgpu_device *adev);

MODULE_FIRMWARE("radeon/bonaire_sdma.bin");
MODULE_FIRMWARE("radeon/bonaire_sdma1.bin");
MODULE_FIRMWARE("radeon/hawaii_sdma.bin");
MODULE_FIRMWARE("radeon/hawaii_sdma1.bin");
MODULE_FIRMWARE("radeon/kaveri_sdma.bin");
MODULE_FIRMWARE("radeon/kaveri_sdma1.bin");
MODULE_FIRMWARE("radeon/kabini_sdma.bin");
MODULE_FIRMWARE("radeon/kabini_sdma1.bin");
MODULE_FIRMWARE("radeon/mullins_sdma.bin");
MODULE_FIRMWARE("radeon/mullins_sdma1.bin");

u32 amdgpu_cik_gpu_check_soft_reset(struct amdgpu_device *adev);

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

/**
 * cik_sdma_init_microcode - load ucode images from disk
 *
 * @adev: amdgpu_device pointer
 *
 * Use the firmware interface to load the ucode images into
 * the driver (not loaded into hw).
 * Returns 0 on success, error on failure.
 */
static int cik_sdma_init_microcode(struct amdgpu_device *adev)
{
	const char *chip_name;
	char fw_name[30];
	int err = 0, i;

	DRM_DEBUG("\n");

	switch (adev->asic_type) {
	case CHIP_BONAIRE:
		chip_name = "bonaire";
		break;
	case CHIP_HAWAII:
		chip_name = "hawaii";
		break;
	case CHIP_KAVERI:
		chip_name = "kaveri";
		break;
	case CHIP_KABINI:
		chip_name = "kabini";
		break;
	case CHIP_MULLINS:
		chip_name = "mullins";
		break;
	default: BUG();
	}

	for (i = 0; i < adev->sdma.num_instances; i++) {
		if (i == 0)
			snprintf(fw_name, sizeof(fw_name), "radeon/%s_sdma.bin", chip_name);
		else
			snprintf(fw_name, sizeof(fw_name), "radeon/%s_sdma1.bin", chip_name);
		err = request_firmware(&adev->sdma.instance[i].fw, fw_name, adev->dev);
		if (err)
			goto out;
		err = amdgpu_ucode_validate(adev->sdma.instance[i].fw);
	}
out:
	if (err) {
		printk(KERN_ERR
		       "cik_sdma: Failed to load firmware \"%s\"\n",
		       fw_name);
		for (i = 0; i < adev->sdma.num_instances; i++) {
			release_firmware(adev->sdma.instance[i].fw);
			adev->sdma.instance[i].fw = NULL;
		}
	}
	return err;
}

/**
 * cik_sdma_ring_get_rptr - get the current read pointer
 *
 * @ring: amdgpu ring pointer
 *
 * Get the current rptr from the hardware (CIK+).
 */
static uint32_t cik_sdma_ring_get_rptr(struct amdgpu_ring *ring)
{
	u32 rptr;

	rptr = ring->adev->wb.wb[ring->rptr_offs];

	return (rptr & 0x3fffc) >> 2;
}

/**
 * cik_sdma_ring_get_wptr - get the current write pointer
 *
 * @ring: amdgpu ring pointer
 *
 * Get the current wptr from the hardware (CIK+).
 */
static uint32_t cik_sdma_ring_get_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	u32 me = (ring == &adev->sdma.instance[0].ring) ? 0 : 1;

	return (RREG32(mmSDMA0_GFX_RB_WPTR + sdma_offsets[me]) & 0x3fffc) >> 2;
}

/**
 * cik_sdma_ring_set_wptr - commit the write pointer
 *
 * @ring: amdgpu ring pointer
 *
 * Write the wptr back to the hardware (CIK+).
 */
static void cik_sdma_ring_set_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	u32 me = (ring == &adev->sdma.instance[0].ring) ? 0 : 1;

	WREG32(mmSDMA0_GFX_RB_WPTR + sdma_offsets[me], (ring->wptr << 2) & 0x3fffc);
}

static void cik_sdma_ring_insert_nop(struct amdgpu_ring *ring, uint32_t count)
{
	struct amdgpu_sdma_instance *sdma = amdgpu_get_sdma_instance(ring);
	int i;

	for (i = 0; i < count; i++)
		if (sdma && sdma->burst_nop && (i == 0))
			amdgpu_ring_write(ring, ring->nop |
					  SDMA_NOP_COUNT(count - 1));
		else
			amdgpu_ring_write(ring, ring->nop);
}

/**
 * cik_sdma_ring_emit_ib - Schedule an IB on the DMA engine
 *
 * @ring: amdgpu ring pointer
 * @ib: IB object to schedule
 *
 * Schedule an IB in the DMA ring (CIK).
 */
static void cik_sdma_ring_emit_ib(struct amdgpu_ring *ring,
			   struct amdgpu_ib *ib)
{
	u32 extra_bits = (ib->vm ? ib->vm->ids[ring->idx].id : 0) & 0xf;
	u32 next_rptr = ring->wptr + 5;

	while ((next_rptr & 7) != 4)
		next_rptr++;

	next_rptr += 4;
	amdgpu_ring_write(ring, SDMA_PACKET(SDMA_OPCODE_WRITE, SDMA_WRITE_SUB_OPCODE_LINEAR, 0));
	amdgpu_ring_write(ring, ring->next_rptr_gpu_addr & 0xfffffffc);
	amdgpu_ring_write(ring, upper_32_bits(ring->next_rptr_gpu_addr) & 0xffffffff);
	amdgpu_ring_write(ring, 1); /* number of DWs to follow */
	amdgpu_ring_write(ring, next_rptr);

	/* IB packet must end on a 8 DW boundary */
	cik_sdma_ring_insert_nop(ring, (12 - (ring->wptr & 7)) % 8);

	amdgpu_ring_write(ring, SDMA_PACKET(SDMA_OPCODE_INDIRECT_BUFFER, 0, extra_bits));
	amdgpu_ring_write(ring, ib->gpu_addr & 0xffffffe0); /* base must be 32 byte aligned */
	amdgpu_ring_write(ring, upper_32_bits(ib->gpu_addr) & 0xffffffff);
	amdgpu_ring_write(ring, ib->length_dw);

}

/**
 * cik_sdma_ring_emit_hdp_flush - emit an hdp flush on the DMA ring
 *
 * @ring: amdgpu ring pointer
 *
 * Emit an hdp flush packet on the requested DMA ring.
 */
static void cik_sdma_ring_emit_hdp_flush(struct amdgpu_ring *ring)
{
	u32 extra_bits = (SDMA_POLL_REG_MEM_EXTRA_OP(1) |
			  SDMA_POLL_REG_MEM_EXTRA_FUNC(3)); /* == */
	u32 ref_and_mask;

	if (ring == &ring->adev->sdma.instance[0].ring)
		ref_and_mask = GPU_HDP_FLUSH_DONE__SDMA0_MASK;
	else
		ref_and_mask = GPU_HDP_FLUSH_DONE__SDMA1_MASK;

	amdgpu_ring_write(ring, SDMA_PACKET(SDMA_OPCODE_POLL_REG_MEM, 0, extra_bits));
	amdgpu_ring_write(ring, mmGPU_HDP_FLUSH_DONE << 2);
	amdgpu_ring_write(ring, mmGPU_HDP_FLUSH_REQ << 2);
	amdgpu_ring_write(ring, ref_and_mask); /* reference */
	amdgpu_ring_write(ring, ref_and_mask); /* mask */
	amdgpu_ring_write(ring, (0xfff << 16) | 10); /* retry count, poll interval */
}

/**
 * cik_sdma_ring_emit_fence - emit a fence on the DMA ring
 *
 * @ring: amdgpu ring pointer
 * @fence: amdgpu fence object
 *
 * Add a DMA fence packet to the ring to write
 * the fence seq number and DMA trap packet to generate
 * an interrupt if needed (CIK).
 */
static void cik_sdma_ring_emit_fence(struct amdgpu_ring *ring, u64 addr, u64 seq,
				     unsigned flags)
{
	bool write64bit = flags & AMDGPU_FENCE_FLAG_64BIT;
	/* write the fence */
	amdgpu_ring_write(ring, SDMA_PACKET(SDMA_OPCODE_FENCE, 0, 0));
	amdgpu_ring_write(ring, lower_32_bits(addr));
	amdgpu_ring_write(ring, upper_32_bits(addr));
	amdgpu_ring_write(ring, lower_32_bits(seq));

	/* optionally write high bits as well */
	if (write64bit) {
		addr += 4;
		amdgpu_ring_write(ring, SDMA_PACKET(SDMA_OPCODE_FENCE, 0, 0));
		amdgpu_ring_write(ring, lower_32_bits(addr));
		amdgpu_ring_write(ring, upper_32_bits(addr));
		amdgpu_ring_write(ring, upper_32_bits(seq));
	}

	/* generate an interrupt */
	amdgpu_ring_write(ring, SDMA_PACKET(SDMA_OPCODE_TRAP, 0, 0));
}

/**
 * cik_sdma_ring_emit_semaphore - emit a semaphore on the dma ring
 *
 * @ring: amdgpu_ring structure holding ring information
 * @semaphore: amdgpu semaphore object
 * @emit_wait: wait or signal semaphore
 *
 * Add a DMA semaphore packet to the ring wait on or signal
 * other rings (CIK).
 */
static bool cik_sdma_ring_emit_semaphore(struct amdgpu_ring *ring,
					 struct amdgpu_semaphore *semaphore,
					 bool emit_wait)
{
	u64 addr = semaphore->gpu_addr;
	u32 extra_bits = emit_wait ? 0 : SDMA_SEMAPHORE_EXTRA_S;

	amdgpu_ring_write(ring, SDMA_PACKET(SDMA_OPCODE_SEMAPHORE, 0, extra_bits));
	amdgpu_ring_write(ring, addr & 0xfffffff8);
	amdgpu_ring_write(ring, upper_32_bits(addr) & 0xffffffff);

	return true;
}

/**
 * cik_sdma_gfx_stop - stop the gfx async dma engines
 *
 * @adev: amdgpu_device pointer
 *
 * Stop the gfx async dma ring buffers (CIK).
 */
static void cik_sdma_gfx_stop(struct amdgpu_device *adev)
{
	struct amdgpu_ring *sdma0 = &adev->sdma.instance[0].ring;
	struct amdgpu_ring *sdma1 = &adev->sdma.instance[1].ring;
	u32 rb_cntl;
	int i;

	if ((adev->mman.buffer_funcs_ring == sdma0) ||
	    (adev->mman.buffer_funcs_ring == sdma1))
		amdgpu_ttm_set_active_vram_size(adev, adev->mc.visible_vram_size);

	for (i = 0; i < adev->sdma.num_instances; i++) {
		rb_cntl = RREG32(mmSDMA0_GFX_RB_CNTL + sdma_offsets[i]);
		rb_cntl &= ~SDMA0_GFX_RB_CNTL__RB_ENABLE_MASK;
		WREG32(mmSDMA0_GFX_RB_CNTL + sdma_offsets[i], rb_cntl);
		WREG32(mmSDMA0_GFX_IB_CNTL + sdma_offsets[i], 0);
	}
	sdma0->ready = false;
	sdma1->ready = false;
}

/**
 * cik_sdma_rlc_stop - stop the compute async dma engines
 *
 * @adev: amdgpu_device pointer
 *
 * Stop the compute async dma queues (CIK).
 */
static void cik_sdma_rlc_stop(struct amdgpu_device *adev)
{
	/* XXX todo */
}

/**
 * cik_sdma_enable - stop the async dma engines
 *
 * @adev: amdgpu_device pointer
 * @enable: enable/disable the DMA MEs.
 *
 * Halt or unhalt the async dma engines (CIK).
 */
static void cik_sdma_enable(struct amdgpu_device *adev, bool enable)
{
	u32 me_cntl;
	int i;

	if (enable == false) {
		cik_sdma_gfx_stop(adev);
		cik_sdma_rlc_stop(adev);
	}

	for (i = 0; i < adev->sdma.num_instances; i++) {
		me_cntl = RREG32(mmSDMA0_F32_CNTL + sdma_offsets[i]);
		if (enable)
			me_cntl &= ~SDMA0_F32_CNTL__HALT_MASK;
		else
			me_cntl |= SDMA0_F32_CNTL__HALT_MASK;
		WREG32(mmSDMA0_F32_CNTL + sdma_offsets[i], me_cntl);
	}
}

/**
 * cik_sdma_gfx_resume - setup and start the async dma engines
 *
 * @adev: amdgpu_device pointer
 *
 * Set up the gfx DMA ring buffers and enable them (CIK).
 * Returns 0 for success, error for failure.
 */
static int cik_sdma_gfx_resume(struct amdgpu_device *adev)
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
			cik_srbm_select(adev, 0, 0, 0, j);
			/* SDMA GFX */
			WREG32(mmSDMA0_GFX_VIRTUAL_ADDR + sdma_offsets[i], 0);
			WREG32(mmSDMA0_GFX_APE1_CNTL + sdma_offsets[i], 0);
			/* XXX SDMA RLC - todo */
		}
		cik_srbm_select(adev, 0, 0, 0, 0);
		mutex_unlock(&adev->srbm_mutex);

		WREG32(mmSDMA0_SEM_INCOMPLETE_TIMER_CNTL + sdma_offsets[i], 0);
		WREG32(mmSDMA0_SEM_WAIT_FAIL_TIMER_CNTL + sdma_offsets[i], 0);

		/* Set ring buffer size in dwords */
		rb_bufsz = order_base_2(ring->ring_size / 4);
		rb_cntl = rb_bufsz << 1;
#ifdef __BIG_ENDIAN
		rb_cntl |= SDMA0_GFX_RB_CNTL__RB_SWAP_ENABLE_MASK |
			SDMA0_GFX_RB_CNTL__RPTR_WRITEBACK_SWAP_ENABLE_MASK;
#endif
		WREG32(mmSDMA0_GFX_RB_CNTL + sdma_offsets[i], rb_cntl);

		/* Initialize the ring buffer's read and write pointers */
		WREG32(mmSDMA0_GFX_RB_RPTR + sdma_offsets[i], 0);
		WREG32(mmSDMA0_GFX_RB_WPTR + sdma_offsets[i], 0);

		/* set the wb address whether it's enabled or not */
		WREG32(mmSDMA0_GFX_RB_RPTR_ADDR_HI + sdma_offsets[i],
		       upper_32_bits(adev->wb.gpu_addr + wb_offset) & 0xFFFFFFFF);
		WREG32(mmSDMA0_GFX_RB_RPTR_ADDR_LO + sdma_offsets[i],
		       ((adev->wb.gpu_addr + wb_offset) & 0xFFFFFFFC));

		rb_cntl |= SDMA0_GFX_RB_CNTL__RPTR_WRITEBACK_ENABLE_MASK;

		WREG32(mmSDMA0_GFX_RB_BASE + sdma_offsets[i], ring->gpu_addr >> 8);
		WREG32(mmSDMA0_GFX_RB_BASE_HI + sdma_offsets[i], ring->gpu_addr >> 40);

		ring->wptr = 0;
		WREG32(mmSDMA0_GFX_RB_WPTR + sdma_offsets[i], ring->wptr << 2);

		/* enable DMA RB */
		WREG32(mmSDMA0_GFX_RB_CNTL + sdma_offsets[i],
		       rb_cntl | SDMA0_GFX_RB_CNTL__RB_ENABLE_MASK);

		ib_cntl = SDMA0_GFX_IB_CNTL__IB_ENABLE_MASK;
#ifdef __BIG_ENDIAN
		ib_cntl |= SDMA0_GFX_IB_CNTL__IB_SWAP_ENABLE_MASK;
#endif
		/* enable DMA IBs */
		WREG32(mmSDMA0_GFX_IB_CNTL + sdma_offsets[i], ib_cntl);

		ring->ready = true;

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
 * cik_sdma_rlc_resume - setup and start the async dma engines
 *
 * @adev: amdgpu_device pointer
 *
 * Set up the compute DMA queues and enable them (CIK).
 * Returns 0 for success, error for failure.
 */
static int cik_sdma_rlc_resume(struct amdgpu_device *adev)
{
	/* XXX todo */
	return 0;
}

/**
 * cik_sdma_load_microcode - load the sDMA ME ucode
 *
 * @adev: amdgpu_device pointer
 *
 * Loads the sDMA0/1 ucode.
 * Returns 0 for success, -EINVAL if the ucode is not available.
 */
static int cik_sdma_load_microcode(struct amdgpu_device *adev)
{
	const struct sdma_firmware_header_v1_0 *hdr;
	const __le32 *fw_data;
	u32 fw_size;
	int i, j;

	/* halt the MEs */
	cik_sdma_enable(adev, false);

	for (i = 0; i < adev->sdma.num_instances; i++) {
		if (!adev->sdma.instance[i].fw)
			return -EINVAL;
		hdr = (const struct sdma_firmware_header_v1_0 *)adev->sdma.instance[i].fw->data;
		amdgpu_ucode_print_sdma_hdr(&hdr->header);
		fw_size = le32_to_cpu(hdr->header.ucode_size_bytes) / 4;
		adev->sdma.instance[i].fw_version = le32_to_cpu(hdr->header.ucode_version);
		adev->sdma.instance[i].feature_version = le32_to_cpu(hdr->ucode_feature_version);
		if (adev->sdma.instance[i].feature_version >= 20)
			adev->sdma.instance[i].burst_nop = true;
		fw_data = (const __le32 *)
			(adev->sdma.instance[i].fw->data + le32_to_cpu(hdr->header.ucode_array_offset_bytes));
		WREG32(mmSDMA0_UCODE_ADDR + sdma_offsets[i], 0);
		for (j = 0; j < fw_size; j++)
			WREG32(mmSDMA0_UCODE_DATA + sdma_offsets[i], le32_to_cpup(fw_data++));
		WREG32(mmSDMA0_UCODE_ADDR + sdma_offsets[i], adev->sdma.instance[i].fw_version);
	}

	return 0;
}

/**
 * cik_sdma_start - setup and start the async dma engines
 *
 * @adev: amdgpu_device pointer
 *
 * Set up the DMA engines and enable them (CIK).
 * Returns 0 for success, error for failure.
 */
static int cik_sdma_start(struct amdgpu_device *adev)
{
	int r;

	r = cik_sdma_load_microcode(adev);
	if (r)
		return r;

	/* unhalt the MEs */
	cik_sdma_enable(adev, true);

	/* start the gfx rings and rlc compute queues */
	r = cik_sdma_gfx_resume(adev);
	if (r)
		return r;
	r = cik_sdma_rlc_resume(adev);
	if (r)
		return r;

	return 0;
}

/**
 * cik_sdma_ring_test_ring - simple async dma engine test
 *
 * @ring: amdgpu_ring structure holding ring information
 *
 * Test the DMA engine by writing using it to write an
 * value to memory. (CIK).
 * Returns 0 for success, error for failure.
 */
static int cik_sdma_ring_test_ring(struct amdgpu_ring *ring)
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

	r = amdgpu_ring_lock(ring, 5);
	if (r) {
		DRM_ERROR("amdgpu: dma failed to lock ring %d (%d).\n", ring->idx, r);
		amdgpu_wb_free(adev, index);
		return r;
	}
	amdgpu_ring_write(ring, SDMA_PACKET(SDMA_OPCODE_WRITE, SDMA_WRITE_SUB_OPCODE_LINEAR, 0));
	amdgpu_ring_write(ring, lower_32_bits(gpu_addr));
	amdgpu_ring_write(ring, upper_32_bits(gpu_addr));
	amdgpu_ring_write(ring, 1); /* number of DWs to follow */
	amdgpu_ring_write(ring, 0xDEADBEEF);
	amdgpu_ring_unlock_commit(ring);

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
 * cik_sdma_ring_test_ib - test an IB on the DMA engine
 *
 * @ring: amdgpu_ring structure holding ring information
 *
 * Test a simple IB in the DMA ring (CIK).
 * Returns 0 on success, error on failure.
 */
static int cik_sdma_ring_test_ib(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	struct amdgpu_ib ib;
	struct fence *f = NULL;
	unsigned i;
	unsigned index;
	int r;
	u32 tmp = 0;
	u64 gpu_addr;

	r = amdgpu_wb_get(adev, &index);
	if (r) {
		dev_err(adev->dev, "(%d) failed to allocate wb slot\n", r);
		return r;
	}

	gpu_addr = adev->wb.gpu_addr + (index * 4);
	tmp = 0xCAFEDEAD;
	adev->wb.wb[index] = cpu_to_le32(tmp);
	memset(&ib, 0, sizeof(ib));
	r = amdgpu_ib_get(ring, NULL, 256, &ib);
	if (r) {
		DRM_ERROR("amdgpu: failed to get ib (%d).\n", r);
		goto err0;
	}

	ib.ptr[0] = SDMA_PACKET(SDMA_OPCODE_WRITE, SDMA_WRITE_SUB_OPCODE_LINEAR, 0);
	ib.ptr[1] = lower_32_bits(gpu_addr);
	ib.ptr[2] = upper_32_bits(gpu_addr);
	ib.ptr[3] = 1;
	ib.ptr[4] = 0xDEADBEEF;
	ib.length_dw = 5;
	r = amdgpu_sched_ib_submit_kernel_helper(adev, ring, &ib, 1, NULL,
						 AMDGPU_FENCE_OWNER_UNDEFINED,
						 &f);
	if (r)
		goto err1;

	r = fence_wait(f, false);
	if (r) {
		DRM_ERROR("amdgpu: fence wait failed (%d).\n", r);
		goto err1;
	}
	for (i = 0; i < adev->usec_timeout; i++) {
		tmp = le32_to_cpu(adev->wb.wb[index]);
		if (tmp == 0xDEADBEEF)
			break;
		DRM_UDELAY(1);
	}
	if (i < adev->usec_timeout) {
		DRM_INFO("ib test on ring %d succeeded in %u usecs\n",
			 ring->idx, i);
		goto err1;
	} else {
		DRM_ERROR("amdgpu: ib test failed (0x%08X)\n", tmp);
		r = -EINVAL;
	}

err1:
	fence_put(f);
	amdgpu_ib_free(adev, &ib);
err0:
	amdgpu_wb_free(adev, index);
	return r;
}

/**
 * cik_sdma_vm_copy_pages - update PTEs by copying them from the GART
 *
 * @ib: indirect buffer to fill with commands
 * @pe: addr of the page entry
 * @src: src addr to copy from
 * @count: number of page entries to update
 *
 * Update PTEs by copying them from the GART using sDMA (CIK).
 */
static void cik_sdma_vm_copy_pte(struct amdgpu_ib *ib,
				 uint64_t pe, uint64_t src,
				 unsigned count)
{
	while (count) {
		unsigned bytes = count * 8;
		if (bytes > 0x1FFFF8)
			bytes = 0x1FFFF8;

		ib->ptr[ib->length_dw++] = SDMA_PACKET(SDMA_OPCODE_COPY,
			SDMA_WRITE_SUB_OPCODE_LINEAR, 0);
		ib->ptr[ib->length_dw++] = bytes;
		ib->ptr[ib->length_dw++] = 0; /* src/dst endian swap */
		ib->ptr[ib->length_dw++] = lower_32_bits(src);
		ib->ptr[ib->length_dw++] = upper_32_bits(src);
		ib->ptr[ib->length_dw++] = lower_32_bits(pe);
		ib->ptr[ib->length_dw++] = upper_32_bits(pe);

		pe += bytes;
		src += bytes;
		count -= bytes / 8;
	}
}

/**
 * cik_sdma_vm_write_pages - update PTEs by writing them manually
 *
 * @ib: indirect buffer to fill with commands
 * @pe: addr of the page entry
 * @addr: dst addr to write into pe
 * @count: number of page entries to update
 * @incr: increase next addr by incr bytes
 * @flags: access flags
 *
 * Update PTEs by writing them manually using sDMA (CIK).
 */
static void cik_sdma_vm_write_pte(struct amdgpu_ib *ib,
				  uint64_t pe,
				  uint64_t addr, unsigned count,
				  uint32_t incr, uint32_t flags)
{
	uint64_t value;
	unsigned ndw;

	while (count) {
		ndw = count * 2;
		if (ndw > 0xFFFFE)
			ndw = 0xFFFFE;

		/* for non-physically contiguous pages (system) */
		ib->ptr[ib->length_dw++] = SDMA_PACKET(SDMA_OPCODE_WRITE,
			SDMA_WRITE_SUB_OPCODE_LINEAR, 0);
		ib->ptr[ib->length_dw++] = pe;
		ib->ptr[ib->length_dw++] = upper_32_bits(pe);
		ib->ptr[ib->length_dw++] = ndw;
		for (; ndw > 0; ndw -= 2, --count, pe += 8) {
			if (flags & AMDGPU_PTE_SYSTEM) {
				value = amdgpu_vm_map_gart(ib->ring->adev, addr);
				value &= 0xFFFFFFFFFFFFF000ULL;
			} else if (flags & AMDGPU_PTE_VALID) {
				value = addr;
			} else {
				value = 0;
			}
			addr += incr;
			value |= flags;
			ib->ptr[ib->length_dw++] = value;
			ib->ptr[ib->length_dw++] = upper_32_bits(value);
		}
	}
}

/**
 * cik_sdma_vm_set_pages - update the page tables using sDMA
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
static void cik_sdma_vm_set_pte_pde(struct amdgpu_ib *ib,
				    uint64_t pe,
				    uint64_t addr, unsigned count,
				    uint32_t incr, uint32_t flags)
{
	uint64_t value;
	unsigned ndw;

	while (count) {
		ndw = count;
		if (ndw > 0x7FFFF)
			ndw = 0x7FFFF;

		if (flags & AMDGPU_PTE_VALID)
			value = addr;
		else
			value = 0;

		/* for physically contiguous pages (vram) */
		ib->ptr[ib->length_dw++] = SDMA_PACKET(SDMA_OPCODE_GENERATE_PTE_PDE, 0, 0);
		ib->ptr[ib->length_dw++] = pe; /* dst addr */
		ib->ptr[ib->length_dw++] = upper_32_bits(pe);
		ib->ptr[ib->length_dw++] = flags; /* mask */
		ib->ptr[ib->length_dw++] = 0;
		ib->ptr[ib->length_dw++] = value; /* value */
		ib->ptr[ib->length_dw++] = upper_32_bits(value);
		ib->ptr[ib->length_dw++] = incr; /* increment size */
		ib->ptr[ib->length_dw++] = 0;
		ib->ptr[ib->length_dw++] = ndw; /* number of entries */

		pe += ndw * 8;
		addr += ndw * incr;
		count -= ndw;
	}
}

/**
 * cik_sdma_vm_pad_ib - pad the IB to the required number of dw
 *
 * @ib: indirect buffer to fill with padding
 *
 */
static void cik_sdma_vm_pad_ib(struct amdgpu_ib *ib)
{
	struct amdgpu_sdma_instance *sdma = amdgpu_get_sdma_instance(ib->ring);
	u32 pad_count;
	int i;

	pad_count = (8 - (ib->length_dw & 0x7)) % 8;
	for (i = 0; i < pad_count; i++)
		if (sdma && sdma->burst_nop && (i == 0))
			ib->ptr[ib->length_dw++] =
					SDMA_PACKET(SDMA_OPCODE_NOP, 0, 0) |
					SDMA_NOP_COUNT(pad_count - 1);
		else
			ib->ptr[ib->length_dw++] =
					SDMA_PACKET(SDMA_OPCODE_NOP, 0, 0);
}

/**
 * cik_sdma_ring_emit_vm_flush - cik vm flush using sDMA
 *
 * @ring: amdgpu_ring pointer
 * @vm: amdgpu_vm pointer
 *
 * Update the page table base and flush the VM TLB
 * using sDMA (CIK).
 */
static void cik_sdma_ring_emit_vm_flush(struct amdgpu_ring *ring,
					unsigned vm_id, uint64_t pd_addr)
{
	u32 extra_bits = (SDMA_POLL_REG_MEM_EXTRA_OP(0) |
			  SDMA_POLL_REG_MEM_EXTRA_FUNC(0)); /* always */

	amdgpu_ring_write(ring, SDMA_PACKET(SDMA_OPCODE_SRBM_WRITE, 0, 0xf000));
	if (vm_id < 8) {
		amdgpu_ring_write(ring, (mmVM_CONTEXT0_PAGE_TABLE_BASE_ADDR + vm_id));
	} else {
		amdgpu_ring_write(ring, (mmVM_CONTEXT8_PAGE_TABLE_BASE_ADDR + vm_id - 8));
	}
	amdgpu_ring_write(ring, pd_addr >> 12);

	/* flush TLB */
	amdgpu_ring_write(ring, SDMA_PACKET(SDMA_OPCODE_SRBM_WRITE, 0, 0xf000));
	amdgpu_ring_write(ring, mmVM_INVALIDATE_REQUEST);
	amdgpu_ring_write(ring, 1 << vm_id);

	amdgpu_ring_write(ring, SDMA_PACKET(SDMA_OPCODE_POLL_REG_MEM, 0, extra_bits));
	amdgpu_ring_write(ring, mmVM_INVALIDATE_REQUEST << 2);
	amdgpu_ring_write(ring, 0);
	amdgpu_ring_write(ring, 0); /* reference */
	amdgpu_ring_write(ring, 0); /* mask */
	amdgpu_ring_write(ring, (0xfff << 16) | 10); /* retry count, poll interval */
}

static void cik_enable_sdma_mgcg(struct amdgpu_device *adev,
				 bool enable)
{
	u32 orig, data;

	if (enable && (adev->cg_flags & AMD_CG_SUPPORT_SDMA_MGCG)) {
		WREG32(mmSDMA0_CLK_CTRL + SDMA0_REGISTER_OFFSET, 0x00000100);
		WREG32(mmSDMA0_CLK_CTRL + SDMA1_REGISTER_OFFSET, 0x00000100);
	} else {
		orig = data = RREG32(mmSDMA0_CLK_CTRL + SDMA0_REGISTER_OFFSET);
		data |= 0xff000000;
		if (data != orig)
			WREG32(mmSDMA0_CLK_CTRL + SDMA0_REGISTER_OFFSET, data);

		orig = data = RREG32(mmSDMA0_CLK_CTRL + SDMA1_REGISTER_OFFSET);
		data |= 0xff000000;
		if (data != orig)
			WREG32(mmSDMA0_CLK_CTRL + SDMA1_REGISTER_OFFSET, data);
	}
}

static void cik_enable_sdma_mgls(struct amdgpu_device *adev,
				 bool enable)
{
	u32 orig, data;

	if (enable && (adev->cg_flags & AMD_CG_SUPPORT_SDMA_LS)) {
		orig = data = RREG32(mmSDMA0_POWER_CNTL + SDMA0_REGISTER_OFFSET);
		data |= 0x100;
		if (orig != data)
			WREG32(mmSDMA0_POWER_CNTL + SDMA0_REGISTER_OFFSET, data);

		orig = data = RREG32(mmSDMA0_POWER_CNTL + SDMA1_REGISTER_OFFSET);
		data |= 0x100;
		if (orig != data)
			WREG32(mmSDMA0_POWER_CNTL + SDMA1_REGISTER_OFFSET, data);
	} else {
		orig = data = RREG32(mmSDMA0_POWER_CNTL + SDMA0_REGISTER_OFFSET);
		data &= ~0x100;
		if (orig != data)
			WREG32(mmSDMA0_POWER_CNTL + SDMA0_REGISTER_OFFSET, data);

		orig = data = RREG32(mmSDMA0_POWER_CNTL + SDMA1_REGISTER_OFFSET);
		data &= ~0x100;
		if (orig != data)
			WREG32(mmSDMA0_POWER_CNTL + SDMA1_REGISTER_OFFSET, data);
	}
}

static int cik_sdma_early_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	adev->sdma.num_instances = SDMA_MAX_INSTANCE;

	cik_sdma_set_ring_funcs(adev);
	cik_sdma_set_irq_funcs(adev);
	cik_sdma_set_buffer_funcs(adev);
	cik_sdma_set_vm_pte_funcs(adev);

	return 0;
}

static int cik_sdma_sw_init(void *handle)
{
	struct amdgpu_ring *ring;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int r, i;

	r = cik_sdma_init_microcode(adev);
	if (r) {
		DRM_ERROR("Failed to load sdma firmware!\n");
		return r;
	}

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

	for (i = 0; i < adev->sdma.num_instances; i++) {
		ring = &adev->sdma.instance[i].ring;
		ring->ring_obj = NULL;
		sprintf(ring->name, "sdma%d", i);
		r = amdgpu_ring_init(adev, ring, 256 * 1024,
				     SDMA_PACKET(SDMA_OPCODE_NOP, 0, 0), 0xf,
				     &adev->sdma.trap_irq,
				     (i == 0) ?
				     AMDGPU_SDMA_IRQ_TRAP0 : AMDGPU_SDMA_IRQ_TRAP1,
				     AMDGPU_RING_TYPE_SDMA);
		if (r)
			return r;
	}

	return r;
}

static int cik_sdma_sw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int i;

	for (i = 0; i < adev->sdma.num_instances; i++)
		amdgpu_ring_fini(&adev->sdma.instance[i].ring);

	return 0;
}

static int cik_sdma_hw_init(void *handle)
{
	int r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	r = cik_sdma_start(adev);
	if (r)
		return r;

	return r;
}

static int cik_sdma_hw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	cik_sdma_enable(adev, false);

	return 0;
}

static int cik_sdma_suspend(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	return cik_sdma_hw_fini(adev);
}

static int cik_sdma_resume(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	return cik_sdma_hw_init(adev);
}

static bool cik_sdma_is_idle(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	u32 tmp = RREG32(mmSRBM_STATUS2);

	if (tmp & (SRBM_STATUS2__SDMA_BUSY_MASK |
				SRBM_STATUS2__SDMA1_BUSY_MASK))
	    return false;

	return true;
}

static int cik_sdma_wait_for_idle(void *handle)
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

static void cik_sdma_print_status(void *handle)
{
	int i, j;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	dev_info(adev->dev, "CIK SDMA registers\n");
	dev_info(adev->dev, "  SRBM_STATUS2=0x%08X\n",
		 RREG32(mmSRBM_STATUS2));
	for (i = 0; i < adev->sdma.num_instances; i++) {
		dev_info(adev->dev, "  SDMA%d_STATUS_REG=0x%08X\n",
			 i, RREG32(mmSDMA0_STATUS_REG + sdma_offsets[i]));
		dev_info(adev->dev, "  SDMA%d_ME_CNTL=0x%08X\n",
			 i, RREG32(mmSDMA0_F32_CNTL + sdma_offsets[i]));
		dev_info(adev->dev, "  SDMA%d_CNTL=0x%08X\n",
			 i, RREG32(mmSDMA0_CNTL + sdma_offsets[i]));
		dev_info(adev->dev, "  SDMA%d_SEM_INCOMPLETE_TIMER_CNTL=0x%08X\n",
			 i, RREG32(mmSDMA0_SEM_INCOMPLETE_TIMER_CNTL + sdma_offsets[i]));
		dev_info(adev->dev, "  SDMA%d_SEM_WAIT_FAIL_TIMER_CNTL=0x%08X\n",
			 i, RREG32(mmSDMA0_SEM_WAIT_FAIL_TIMER_CNTL + sdma_offsets[i]));
		dev_info(adev->dev, "  SDMA%d_GFX_IB_CNTL=0x%08X\n",
			 i, RREG32(mmSDMA0_GFX_IB_CNTL + sdma_offsets[i]));
		dev_info(adev->dev, "  SDMA%d_GFX_RB_CNTL=0x%08X\n",
			 i, RREG32(mmSDMA0_GFX_RB_CNTL + sdma_offsets[i]));
		dev_info(adev->dev, "  SDMA%d_GFX_RB_RPTR=0x%08X\n",
			 i, RREG32(mmSDMA0_GFX_RB_RPTR + sdma_offsets[i]));
		dev_info(adev->dev, "  SDMA%d_GFX_RB_WPTR=0x%08X\n",
			 i, RREG32(mmSDMA0_GFX_RB_WPTR + sdma_offsets[i]));
		dev_info(adev->dev, "  SDMA%d_GFX_RB_RPTR_ADDR_HI=0x%08X\n",
			 i, RREG32(mmSDMA0_GFX_RB_RPTR_ADDR_HI + sdma_offsets[i]));
		dev_info(adev->dev, "  SDMA%d_GFX_RB_RPTR_ADDR_LO=0x%08X\n",
			 i, RREG32(mmSDMA0_GFX_RB_RPTR_ADDR_LO + sdma_offsets[i]));
		dev_info(adev->dev, "  SDMA%d_GFX_RB_BASE=0x%08X\n",
			 i, RREG32(mmSDMA0_GFX_RB_BASE + sdma_offsets[i]));
		dev_info(adev->dev, "  SDMA%d_GFX_RB_BASE_HI=0x%08X\n",
			 i, RREG32(mmSDMA0_GFX_RB_BASE_HI + sdma_offsets[i]));
		mutex_lock(&adev->srbm_mutex);
		for (j = 0; j < 16; j++) {
			cik_srbm_select(adev, 0, 0, 0, j);
			dev_info(adev->dev, "  VM %d:\n", j);
			dev_info(adev->dev, "  SDMA0_GFX_VIRTUAL_ADDR=0x%08X\n",
				 RREG32(mmSDMA0_GFX_VIRTUAL_ADDR + sdma_offsets[i]));
			dev_info(adev->dev, "  SDMA0_GFX_APE1_CNTL=0x%08X\n",
				 RREG32(mmSDMA0_GFX_APE1_CNTL + sdma_offsets[i]));
		}
		cik_srbm_select(adev, 0, 0, 0, 0);
		mutex_unlock(&adev->srbm_mutex);
	}
}

static int cik_sdma_soft_reset(void *handle)
{
	u32 srbm_soft_reset = 0;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	u32 tmp = RREG32(mmSRBM_STATUS2);

	if (tmp & SRBM_STATUS2__SDMA_BUSY_MASK) {
		/* sdma0 */
		tmp = RREG32(mmSDMA0_F32_CNTL + SDMA0_REGISTER_OFFSET);
		tmp |= SDMA0_F32_CNTL__HALT_MASK;
		WREG32(mmSDMA0_F32_CNTL + SDMA0_REGISTER_OFFSET, tmp);
		srbm_soft_reset |= SRBM_SOFT_RESET__SOFT_RESET_SDMA_MASK;
	}
	if (tmp & SRBM_STATUS2__SDMA1_BUSY_MASK) {
		/* sdma1 */
		tmp = RREG32(mmSDMA0_F32_CNTL + SDMA1_REGISTER_OFFSET);
		tmp |= SDMA0_F32_CNTL__HALT_MASK;
		WREG32(mmSDMA0_F32_CNTL + SDMA1_REGISTER_OFFSET, tmp);
		srbm_soft_reset |= SRBM_SOFT_RESET__SOFT_RESET_SDMA1_MASK;
	}

	if (srbm_soft_reset) {
		cik_sdma_print_status((void *)adev);

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

		cik_sdma_print_status((void *)adev);
	}

	return 0;
}

static int cik_sdma_set_trap_irq_state(struct amdgpu_device *adev,
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
			sdma_cntl &= ~SDMA0_CNTL__TRAP_ENABLE_MASK;
			WREG32(mmSDMA0_CNTL + SDMA0_REGISTER_OFFSET, sdma_cntl);
			break;
		case AMDGPU_IRQ_STATE_ENABLE:
			sdma_cntl = RREG32(mmSDMA0_CNTL + SDMA0_REGISTER_OFFSET);
			sdma_cntl |= SDMA0_CNTL__TRAP_ENABLE_MASK;
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
			sdma_cntl &= ~SDMA0_CNTL__TRAP_ENABLE_MASK;
			WREG32(mmSDMA0_CNTL + SDMA1_REGISTER_OFFSET, sdma_cntl);
			break;
		case AMDGPU_IRQ_STATE_ENABLE:
			sdma_cntl = RREG32(mmSDMA0_CNTL + SDMA1_REGISTER_OFFSET);
			sdma_cntl |= SDMA0_CNTL__TRAP_ENABLE_MASK;
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

static int cik_sdma_process_trap_irq(struct amdgpu_device *adev,
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

static int cik_sdma_process_illegal_inst_irq(struct amdgpu_device *adev,
					     struct amdgpu_irq_src *source,
					     struct amdgpu_iv_entry *entry)
{
	DRM_ERROR("Illegal instruction in SDMA command stream\n");
	schedule_work(&adev->reset_work);
	return 0;
}

static int cik_sdma_set_clockgating_state(void *handle,
					  enum amd_clockgating_state state)
{
	bool gate = false;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (state == AMD_CG_STATE_GATE)
		gate = true;

	cik_enable_sdma_mgcg(adev, gate);
	cik_enable_sdma_mgls(adev, gate);

	return 0;
}

static int cik_sdma_set_powergating_state(void *handle,
					  enum amd_powergating_state state)
{
	return 0;
}

const struct amd_ip_funcs cik_sdma_ip_funcs = {
	.early_init = cik_sdma_early_init,
	.late_init = NULL,
	.sw_init = cik_sdma_sw_init,
	.sw_fini = cik_sdma_sw_fini,
	.hw_init = cik_sdma_hw_init,
	.hw_fini = cik_sdma_hw_fini,
	.suspend = cik_sdma_suspend,
	.resume = cik_sdma_resume,
	.is_idle = cik_sdma_is_idle,
	.wait_for_idle = cik_sdma_wait_for_idle,
	.soft_reset = cik_sdma_soft_reset,
	.print_status = cik_sdma_print_status,
	.set_clockgating_state = cik_sdma_set_clockgating_state,
	.set_powergating_state = cik_sdma_set_powergating_state,
};

static const struct amdgpu_ring_funcs cik_sdma_ring_funcs = {
	.get_rptr = cik_sdma_ring_get_rptr,
	.get_wptr = cik_sdma_ring_get_wptr,
	.set_wptr = cik_sdma_ring_set_wptr,
	.parse_cs = NULL,
	.emit_ib = cik_sdma_ring_emit_ib,
	.emit_fence = cik_sdma_ring_emit_fence,
	.emit_semaphore = cik_sdma_ring_emit_semaphore,
	.emit_vm_flush = cik_sdma_ring_emit_vm_flush,
	.emit_hdp_flush = cik_sdma_ring_emit_hdp_flush,
	.test_ring = cik_sdma_ring_test_ring,
	.test_ib = cik_sdma_ring_test_ib,
	.insert_nop = cik_sdma_ring_insert_nop,
};

static void cik_sdma_set_ring_funcs(struct amdgpu_device *adev)
{
	int i;

	for (i = 0; i < adev->sdma.num_instances; i++)
		adev->sdma.instance[i].ring.funcs = &cik_sdma_ring_funcs;
}

static const struct amdgpu_irq_src_funcs cik_sdma_trap_irq_funcs = {
	.set = cik_sdma_set_trap_irq_state,
	.process = cik_sdma_process_trap_irq,
};

static const struct amdgpu_irq_src_funcs cik_sdma_illegal_inst_irq_funcs = {
	.process = cik_sdma_process_illegal_inst_irq,
};

static void cik_sdma_set_irq_funcs(struct amdgpu_device *adev)
{
	adev->sdma.trap_irq.num_types = AMDGPU_SDMA_IRQ_LAST;
	adev->sdma.trap_irq.funcs = &cik_sdma_trap_irq_funcs;
	adev->sdma.illegal_inst_irq.funcs = &cik_sdma_illegal_inst_irq_funcs;
}

/**
 * cik_sdma_emit_copy_buffer - copy buffer using the sDMA engine
 *
 * @ring: amdgpu_ring structure holding ring information
 * @src_offset: src GPU address
 * @dst_offset: dst GPU address
 * @byte_count: number of bytes to xfer
 *
 * Copy GPU buffers using the DMA engine (CIK).
 * Used by the amdgpu ttm implementation to move pages if
 * registered as the asic copy callback.
 */
static void cik_sdma_emit_copy_buffer(struct amdgpu_ib *ib,
				      uint64_t src_offset,
				      uint64_t dst_offset,
				      uint32_t byte_count)
{
	ib->ptr[ib->length_dw++] = SDMA_PACKET(SDMA_OPCODE_COPY, SDMA_COPY_SUB_OPCODE_LINEAR, 0);
	ib->ptr[ib->length_dw++] = byte_count;
	ib->ptr[ib->length_dw++] = 0; /* src/dst endian swap */
	ib->ptr[ib->length_dw++] = lower_32_bits(src_offset);
	ib->ptr[ib->length_dw++] = upper_32_bits(src_offset);
	ib->ptr[ib->length_dw++] = lower_32_bits(dst_offset);
	ib->ptr[ib->length_dw++] = upper_32_bits(dst_offset);
}

/**
 * cik_sdma_emit_fill_buffer - fill buffer using the sDMA engine
 *
 * @ring: amdgpu_ring structure holding ring information
 * @src_data: value to write to buffer
 * @dst_offset: dst GPU address
 * @byte_count: number of bytes to xfer
 *
 * Fill GPU buffers using the DMA engine (CIK).
 */
static void cik_sdma_emit_fill_buffer(struct amdgpu_ib *ib,
				      uint32_t src_data,
				      uint64_t dst_offset,
				      uint32_t byte_count)
{
	ib->ptr[ib->length_dw++] = SDMA_PACKET(SDMA_OPCODE_CONSTANT_FILL, 0, 0);
	ib->ptr[ib->length_dw++] = lower_32_bits(dst_offset);
	ib->ptr[ib->length_dw++] = upper_32_bits(dst_offset);
	ib->ptr[ib->length_dw++] = src_data;
	ib->ptr[ib->length_dw++] = byte_count;
}

static const struct amdgpu_buffer_funcs cik_sdma_buffer_funcs = {
	.copy_max_bytes = 0x1fffff,
	.copy_num_dw = 7,
	.emit_copy_buffer = cik_sdma_emit_copy_buffer,

	.fill_max_bytes = 0x1fffff,
	.fill_num_dw = 5,
	.emit_fill_buffer = cik_sdma_emit_fill_buffer,
};

static void cik_sdma_set_buffer_funcs(struct amdgpu_device *adev)
{
	if (adev->mman.buffer_funcs == NULL) {
		adev->mman.buffer_funcs = &cik_sdma_buffer_funcs;
		adev->mman.buffer_funcs_ring = &adev->sdma.instance[0].ring;
	}
}

static const struct amdgpu_vm_pte_funcs cik_sdma_vm_pte_funcs = {
	.copy_pte = cik_sdma_vm_copy_pte,
	.write_pte = cik_sdma_vm_write_pte,
	.set_pte_pde = cik_sdma_vm_set_pte_pde,
	.pad_ib = cik_sdma_vm_pad_ib,
};

static void cik_sdma_set_vm_pte_funcs(struct amdgpu_device *adev)
{
	if (adev->vm_manager.vm_pte_funcs == NULL) {
		adev->vm_manager.vm_pte_funcs = &cik_sdma_vm_pte_funcs;
		adev->vm_manager.vm_pte_funcs_ring = &adev->sdma.instance[0].ring;
		adev->vm_manager.vm_pte_funcs_ring->is_pte_ring = true;
	}
}
