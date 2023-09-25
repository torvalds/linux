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
#include <linux/module.h>

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
static int cik_sdma_soft_reset(void *handle);

MODULE_FIRMWARE("amdgpu/bonaire_sdma.bin");
MODULE_FIRMWARE("amdgpu/bonaire_sdma1.bin");
MODULE_FIRMWARE("amdgpu/hawaii_sdma.bin");
MODULE_FIRMWARE("amdgpu/hawaii_sdma1.bin");
MODULE_FIRMWARE("amdgpu/kaveri_sdma.bin");
MODULE_FIRMWARE("amdgpu/kaveri_sdma1.bin");
MODULE_FIRMWARE("amdgpu/kabini_sdma.bin");
MODULE_FIRMWARE("amdgpu/kabini_sdma1.bin");
MODULE_FIRMWARE("amdgpu/mullins_sdma.bin");
MODULE_FIRMWARE("amdgpu/mullins_sdma1.bin");

u32 amdgpu_cik_gpu_check_soft_reset(struct amdgpu_device *adev);


static void cik_sdma_free_microcode(struct amdgpu_device *adev)
{
	int i;

	for (i = 0; i < adev->sdma.num_instances; i++)
		amdgpu_ucode_release(&adev->sdma.instance[i].fw);
}

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
			snprintf(fw_name, sizeof(fw_name), "amdgpu/%s_sdma.bin", chip_name);
		else
			snprintf(fw_name, sizeof(fw_name), "amdgpu/%s_sdma1.bin", chip_name);
		err = amdgpu_ucode_request(adev, &adev->sdma.instance[i].fw, fw_name);
		if (err)
			goto out;
	}
out:
	if (err) {
		pr_err("cik_sdma: Failed to load firmware \"%s\"\n", fw_name);
		for (i = 0; i < adev->sdma.num_instances; i++)
			amdgpu_ucode_release(&adev->sdma.instance[i].fw);
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
static uint64_t cik_sdma_ring_get_rptr(struct amdgpu_ring *ring)
{
	u32 rptr;

	rptr = *ring->rptr_cpu_addr;

	return (rptr & 0x3fffc) >> 2;
}

/**
 * cik_sdma_ring_get_wptr - get the current write pointer
 *
 * @ring: amdgpu ring pointer
 *
 * Get the current wptr from the hardware (CIK+).
 */
static uint64_t cik_sdma_ring_get_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	return (RREG32(mmSDMA0_GFX_RB_WPTR + sdma_offsets[ring->me]) & 0x3fffc) >> 2;
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

	WREG32(mmSDMA0_GFX_RB_WPTR + sdma_offsets[ring->me],
	       (ring->wptr << 2) & 0x3fffc);
}

static void cik_sdma_ring_insert_nop(struct amdgpu_ring *ring, uint32_t count)
{
	struct amdgpu_sdma_instance *sdma = amdgpu_sdma_get_instance_from_ring(ring);
	int i;

	for (i = 0; i < count; i++)
		if (sdma && sdma->burst_nop && (i == 0))
			amdgpu_ring_write(ring, ring->funcs->nop |
					  SDMA_NOP_COUNT(count - 1));
		else
			amdgpu_ring_write(ring, ring->funcs->nop);
}

/**
 * cik_sdma_ring_emit_ib - Schedule an IB on the DMA engine
 *
 * @ring: amdgpu ring pointer
 * @job: job to retrive vmid from
 * @ib: IB object to schedule
 * @flags: unused
 *
 * Schedule an IB in the DMA ring (CIK).
 */
static void cik_sdma_ring_emit_ib(struct amdgpu_ring *ring,
				  struct amdgpu_job *job,
				  struct amdgpu_ib *ib,
				  uint32_t flags)
{
	unsigned vmid = AMDGPU_JOB_GET_VMID(job);
	u32 extra_bits = vmid & 0xf;

	/* IB packet must end on a 8 DW boundary */
	cik_sdma_ring_insert_nop(ring, (4 - lower_32_bits(ring->wptr)) & 7);

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

	if (ring->me == 0)
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
 * @addr: address
 * @seq: sequence number
 * @flags: fence related flags
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
 * cik_sdma_gfx_stop - stop the gfx async dma engines
 *
 * @adev: amdgpu_device pointer
 *
 * Stop the gfx async dma ring buffers (CIK).
 */
static void cik_sdma_gfx_stop(struct amdgpu_device *adev)
{
	u32 rb_cntl;
	int i;

	amdgpu_sdma_unset_buffer_funcs_helper(adev);

	for (i = 0; i < adev->sdma.num_instances; i++) {
		rb_cntl = RREG32(mmSDMA0_GFX_RB_CNTL + sdma_offsets[i]);
		rb_cntl &= ~SDMA0_GFX_RB_CNTL__RB_ENABLE_MASK;
		WREG32(mmSDMA0_GFX_RB_CNTL + sdma_offsets[i], rb_cntl);
		WREG32(mmSDMA0_GFX_IB_CNTL + sdma_offsets[i], 0);
	}
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
 * cik_ctx_switch_enable - stop the async dma engines context switch
 *
 * @adev: amdgpu_device pointer
 * @enable: enable/disable the DMA MEs context switch.
 *
 * Halt or unhalt the async dma engines context switch (VI).
 */
static void cik_ctx_switch_enable(struct amdgpu_device *adev, bool enable)
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
			if (amdgpu_sdma_phase_quantum) {
				WREG32(mmSDMA0_PHASE0_QUANTUM + sdma_offsets[i],
				       phase_quantum);
				WREG32(mmSDMA0_PHASE1_QUANTUM + sdma_offsets[i],
				       phase_quantum);
			}
		} else {
			f32_cntl = REG_SET_FIELD(f32_cntl, SDMA0_CNTL,
					AUTO_CTXSW_ENABLE, 0);
		}

		WREG32(mmSDMA0_CNTL + sdma_offsets[i], f32_cntl);
	}
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

	if (!enable) {
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
	int i, j, r;

	for (i = 0; i < adev->sdma.num_instances; i++) {
		ring = &adev->sdma.instance[i].ring;

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

		WREG32(mmSDMA0_TILING_CONFIG + sdma_offsets[i],
		       adev->gfx.config.gb_addr_config & 0x70);

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
		WREG32(mmSDMA0_GFX_IB_RPTR + sdma_offsets[i], 0);
		WREG32(mmSDMA0_GFX_IB_OFFSET + sdma_offsets[i], 0);

		/* set the wb address whether it's enabled or not */
		WREG32(mmSDMA0_GFX_RB_RPTR_ADDR_HI + sdma_offsets[i],
		       upper_32_bits(ring->rptr_gpu_addr) & 0xFFFFFFFF);
		WREG32(mmSDMA0_GFX_RB_RPTR_ADDR_LO + sdma_offsets[i],
		       ((ring->rptr_gpu_addr) & 0xFFFFFFFC));

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
	}

	cik_sdma_enable(adev, true);

	for (i = 0; i < adev->sdma.num_instances; i++) {
		ring = &adev->sdma.instance[i].ring;
		r = amdgpu_ring_test_helper(ring);
		if (r)
			return r;

		if (adev->mman.buffer_funcs_ring == ring)
			amdgpu_ttm_set_buffer_funcs_status(adev, true);
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

	/* halt the engine before programing */
	cik_sdma_enable(adev, false);
	/* enable sdma ring preemption */
	cik_ctx_switch_enable(adev, true);

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

	r = amdgpu_device_wb_get(adev, &index);
	if (r)
		return r;

	gpu_addr = adev->wb.gpu_addr + (index * 4);
	tmp = 0xCAFEDEAD;
	adev->wb.wb[index] = cpu_to_le32(tmp);

	r = amdgpu_ring_alloc(ring, 5);
	if (r)
		goto error_free_wb;

	amdgpu_ring_write(ring, SDMA_PACKET(SDMA_OPCODE_WRITE, SDMA_WRITE_SUB_OPCODE_LINEAR, 0));
	amdgpu_ring_write(ring, lower_32_bits(gpu_addr));
	amdgpu_ring_write(ring, upper_32_bits(gpu_addr));
	amdgpu_ring_write(ring, 1); /* number of DWs to follow */
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
 * cik_sdma_ring_test_ib - test an IB on the DMA engine
 *
 * @ring: amdgpu_ring structure holding ring information
 * @timeout: timeout value in jiffies, or MAX_SCHEDULE_TIMEOUT
 *
 * Test a simple IB in the DMA ring (CIK).
 * Returns 0 on success, error on failure.
 */
static int cik_sdma_ring_test_ib(struct amdgpu_ring *ring, long timeout)
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

	ib.ptr[0] = SDMA_PACKET(SDMA_OPCODE_WRITE,
				SDMA_WRITE_SUB_OPCODE_LINEAR, 0);
	ib.ptr[1] = lower_32_bits(gpu_addr);
	ib.ptr[2] = upper_32_bits(gpu_addr);
	ib.ptr[3] = 1;
	ib.ptr[4] = 0xDEADBEEF;
	ib.length_dw = 5;
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
 * cik_sdma_vm_copy_pte - update PTEs by copying them from the GART
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
	unsigned bytes = count * 8;

	ib->ptr[ib->length_dw++] = SDMA_PACKET(SDMA_OPCODE_COPY,
		SDMA_WRITE_SUB_OPCODE_LINEAR, 0);
	ib->ptr[ib->length_dw++] = bytes;
	ib->ptr[ib->length_dw++] = 0; /* src/dst endian swap */
	ib->ptr[ib->length_dw++] = lower_32_bits(src);
	ib->ptr[ib->length_dw++] = upper_32_bits(src);
	ib->ptr[ib->length_dw++] = lower_32_bits(pe);
	ib->ptr[ib->length_dw++] = upper_32_bits(pe);
}

/**
 * cik_sdma_vm_write_pte - update PTEs by writing them manually
 *
 * @ib: indirect buffer to fill with commands
 * @pe: addr of the page entry
 * @value: dst addr to write into pe
 * @count: number of page entries to update
 * @incr: increase next addr by incr bytes
 *
 * Update PTEs by writing them manually using sDMA (CIK).
 */
static void cik_sdma_vm_write_pte(struct amdgpu_ib *ib, uint64_t pe,
				  uint64_t value, unsigned count,
				  uint32_t incr)
{
	unsigned ndw = count * 2;

	ib->ptr[ib->length_dw++] = SDMA_PACKET(SDMA_OPCODE_WRITE,
		SDMA_WRITE_SUB_OPCODE_LINEAR, 0);
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
 * cik_sdma_vm_set_pte_pde - update the page tables using sDMA
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
static void cik_sdma_vm_set_pte_pde(struct amdgpu_ib *ib, uint64_t pe,
				    uint64_t addr, unsigned count,
				    uint32_t incr, uint64_t flags)
{
	/* for physically contiguous pages (vram) */
	ib->ptr[ib->length_dw++] = SDMA_PACKET(SDMA_OPCODE_GENERATE_PTE_PDE, 0, 0);
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
 * cik_sdma_ring_pad_ib - pad the IB to the required number of dw
 *
 * @ring: amdgpu_ring structure holding ring information
 * @ib: indirect buffer to fill with padding
 *
 */
static void cik_sdma_ring_pad_ib(struct amdgpu_ring *ring, struct amdgpu_ib *ib)
{
	struct amdgpu_sdma_instance *sdma = amdgpu_sdma_get_instance_from_ring(ring);
	u32 pad_count;
	int i;

	pad_count = (-ib->length_dw) & 7;
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
 * cik_sdma_ring_emit_pipeline_sync - sync the pipeline
 *
 * @ring: amdgpu_ring pointer
 *
 * Make sure all previous operations are completed (CIK).
 */
static void cik_sdma_ring_emit_pipeline_sync(struct amdgpu_ring *ring)
{
	uint32_t seq = ring->fence_drv.sync_seq;
	uint64_t addr = ring->fence_drv.gpu_addr;

	/* wait for idle */
	amdgpu_ring_write(ring, SDMA_PACKET(SDMA_OPCODE_POLL_REG_MEM, 0,
					    SDMA_POLL_REG_MEM_EXTRA_OP(0) |
					    SDMA_POLL_REG_MEM_EXTRA_FUNC(3) | /* equal */
					    SDMA_POLL_REG_MEM_EXTRA_M));
	amdgpu_ring_write(ring, addr & 0xfffffffc);
	amdgpu_ring_write(ring, upper_32_bits(addr) & 0xffffffff);
	amdgpu_ring_write(ring, seq); /* reference */
	amdgpu_ring_write(ring, 0xffffffff); /* mask */
	amdgpu_ring_write(ring, (0xfff << 16) | 4); /* retry count, poll interval */
}

/**
 * cik_sdma_ring_emit_vm_flush - cik vm flush using sDMA
 *
 * @ring: amdgpu_ring pointer
 * @vmid: vmid number to use
 * @pd_addr: address
 *
 * Update the page table base and flush the VM TLB
 * using sDMA (CIK).
 */
static void cik_sdma_ring_emit_vm_flush(struct amdgpu_ring *ring,
					unsigned vmid, uint64_t pd_addr)
{
	u32 extra_bits = (SDMA_POLL_REG_MEM_EXTRA_OP(0) |
			  SDMA_POLL_REG_MEM_EXTRA_FUNC(0)); /* always */

	amdgpu_gmc_emit_flush_gpu_tlb(ring, vmid, pd_addr);

	amdgpu_ring_write(ring, SDMA_PACKET(SDMA_OPCODE_POLL_REG_MEM, 0, extra_bits));
	amdgpu_ring_write(ring, mmVM_INVALIDATE_REQUEST << 2);
	amdgpu_ring_write(ring, 0);
	amdgpu_ring_write(ring, 0); /* reference */
	amdgpu_ring_write(ring, 0); /* mask */
	amdgpu_ring_write(ring, (0xfff << 16) | 10); /* retry count, poll interval */
}

static void cik_sdma_ring_emit_wreg(struct amdgpu_ring *ring,
				    uint32_t reg, uint32_t val)
{
	amdgpu_ring_write(ring, SDMA_PACKET(SDMA_OPCODE_SRBM_WRITE, 0, 0xf000));
	amdgpu_ring_write(ring, reg);
	amdgpu_ring_write(ring, val);
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
	int r;

	adev->sdma.num_instances = SDMA_MAX_INSTANCE;

	r = cik_sdma_init_microcode(adev);
	if (r)
		return r;

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

	/* SDMA trap event */
	r = amdgpu_irq_add_id(adev, AMDGPU_IRQ_CLIENTID_LEGACY, 224,
			      &adev->sdma.trap_irq);
	if (r)
		return r;

	/* SDMA Privileged inst */
	r = amdgpu_irq_add_id(adev, AMDGPU_IRQ_CLIENTID_LEGACY, 241,
			      &adev->sdma.illegal_inst_irq);
	if (r)
		return r;

	/* SDMA Privileged inst */
	r = amdgpu_irq_add_id(adev, AMDGPU_IRQ_CLIENTID_LEGACY, 247,
			      &adev->sdma.illegal_inst_irq);
	if (r)
		return r;

	for (i = 0; i < adev->sdma.num_instances; i++) {
		ring = &adev->sdma.instance[i].ring;
		ring->ring_obj = NULL;
		sprintf(ring->name, "sdma%d", i);
		r = amdgpu_ring_init(adev, ring, 1024,
				     &adev->sdma.trap_irq,
				     (i == 0) ? AMDGPU_SDMA_IRQ_INSTANCE0 :
				     AMDGPU_SDMA_IRQ_INSTANCE1,
				     AMDGPU_RING_PRIO_DEFAULT, NULL);
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

	cik_sdma_free_microcode(adev);
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

	cik_ctx_switch_enable(adev, false);
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

	cik_sdma_soft_reset(handle);

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

static int cik_sdma_soft_reset(void *handle)
{
	u32 srbm_soft_reset = 0;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	u32 tmp;

	/* sdma0 */
	tmp = RREG32(mmSDMA0_F32_CNTL + SDMA0_REGISTER_OFFSET);
	tmp |= SDMA0_F32_CNTL__HALT_MASK;
	WREG32(mmSDMA0_F32_CNTL + SDMA0_REGISTER_OFFSET, tmp);
	srbm_soft_reset |= SRBM_SOFT_RESET__SOFT_RESET_SDMA_MASK;

	/* sdma1 */
	tmp = RREG32(mmSDMA0_F32_CNTL + SDMA1_REGISTER_OFFSET);
	tmp |= SDMA0_F32_CNTL__HALT_MASK;
	WREG32(mmSDMA0_F32_CNTL + SDMA1_REGISTER_OFFSET, tmp);
	srbm_soft_reset |= SRBM_SOFT_RESET__SOFT_RESET_SDMA1_MASK;

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

static int cik_sdma_set_trap_irq_state(struct amdgpu_device *adev,
				       struct amdgpu_irq_src *src,
				       unsigned type,
				       enum amdgpu_interrupt_state state)
{
	u32 sdma_cntl;

	switch (type) {
	case AMDGPU_SDMA_IRQ_INSTANCE0:
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
	case AMDGPU_SDMA_IRQ_INSTANCE1:
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
	u8 instance_id;

	DRM_ERROR("Illegal instruction in SDMA command stream\n");
	instance_id = (entry->ring_id & 0x3) >> 0;
	drm_sched_fault(&adev->sdma.instance[instance_id].ring.sched);
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

static const struct amd_ip_funcs cik_sdma_ip_funcs = {
	.name = "cik_sdma",
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
	.set_clockgating_state = cik_sdma_set_clockgating_state,
	.set_powergating_state = cik_sdma_set_powergating_state,
};

static const struct amdgpu_ring_funcs cik_sdma_ring_funcs = {
	.type = AMDGPU_RING_TYPE_SDMA,
	.align_mask = 0xf,
	.nop = SDMA_PACKET(SDMA_OPCODE_NOP, 0, 0),
	.support_64bit_ptrs = false,
	.get_rptr = cik_sdma_ring_get_rptr,
	.get_wptr = cik_sdma_ring_get_wptr,
	.set_wptr = cik_sdma_ring_set_wptr,
	.emit_frame_size =
		6 + /* cik_sdma_ring_emit_hdp_flush */
		3 + /* hdp invalidate */
		6 + /* cik_sdma_ring_emit_pipeline_sync */
		CIK_FLUSH_GPU_TLB_NUM_WREG * 3 + 6 + /* cik_sdma_ring_emit_vm_flush */
		9 + 9 + 9, /* cik_sdma_ring_emit_fence x3 for user fence, vm fence */
	.emit_ib_size = 7 + 4, /* cik_sdma_ring_emit_ib */
	.emit_ib = cik_sdma_ring_emit_ib,
	.emit_fence = cik_sdma_ring_emit_fence,
	.emit_pipeline_sync = cik_sdma_ring_emit_pipeline_sync,
	.emit_vm_flush = cik_sdma_ring_emit_vm_flush,
	.emit_hdp_flush = cik_sdma_ring_emit_hdp_flush,
	.test_ring = cik_sdma_ring_test_ring,
	.test_ib = cik_sdma_ring_test_ib,
	.insert_nop = cik_sdma_ring_insert_nop,
	.pad_ib = cik_sdma_ring_pad_ib,
	.emit_wreg = cik_sdma_ring_emit_wreg,
};

static void cik_sdma_set_ring_funcs(struct amdgpu_device *adev)
{
	int i;

	for (i = 0; i < adev->sdma.num_instances; i++) {
		adev->sdma.instance[i].ring.funcs = &cik_sdma_ring_funcs;
		adev->sdma.instance[i].ring.me = i;
	}
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
 * @ib: indirect buffer to copy to
 * @src_offset: src GPU address
 * @dst_offset: dst GPU address
 * @byte_count: number of bytes to xfer
 * @tmz: is this a secure operation
 *
 * Copy GPU buffers using the DMA engine (CIK).
 * Used by the amdgpu ttm implementation to move pages if
 * registered as the asic copy callback.
 */
static void cik_sdma_emit_copy_buffer(struct amdgpu_ib *ib,
				      uint64_t src_offset,
				      uint64_t dst_offset,
				      uint32_t byte_count,
				      bool tmz)
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
 * @ib: indirect buffer to fill
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
	adev->mman.buffer_funcs = &cik_sdma_buffer_funcs;
	adev->mman.buffer_funcs_ring = &adev->sdma.instance[0].ring;
}

static const struct amdgpu_vm_pte_funcs cik_sdma_vm_pte_funcs = {
	.copy_pte_num_dw = 7,
	.copy_pte = cik_sdma_vm_copy_pte,

	.write_pte = cik_sdma_vm_write_pte,
	.set_pte_pde = cik_sdma_vm_set_pte_pde,
};

static void cik_sdma_set_vm_pte_funcs(struct amdgpu_device *adev)
{
	unsigned i;

	adev->vm_manager.vm_pte_funcs = &cik_sdma_vm_pte_funcs;
	for (i = 0; i < adev->sdma.num_instances; i++) {
		adev->vm_manager.vm_pte_scheds[i] =
			&adev->sdma.instance[i].ring.sched;
	}
	adev->vm_manager.vm_pte_num_scheds = adev->sdma.num_instances;
}

const struct amdgpu_ip_block_version cik_sdma_ip_block =
{
	.type = AMD_IP_BLOCK_TYPE_SDMA,
	.major = 2,
	.minor = 0,
	.rev = 0,
	.funcs = &cik_sdma_ip_funcs,
};
