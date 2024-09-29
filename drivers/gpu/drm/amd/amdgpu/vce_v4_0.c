/*
 * Copyright 2016 Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 */

#include <linux/firmware.h>
#include <drm/drm_drv.h>

#include "amdgpu.h"
#include "amdgpu_vce.h"
#include "soc15.h"
#include "soc15d.h"
#include "soc15_common.h"
#include "mmsch_v1_0.h"

#include "vce/vce_4_0_offset.h"
#include "vce/vce_4_0_default.h"
#include "vce/vce_4_0_sh_mask.h"
#include "mmhub/mmhub_1_0_offset.h"
#include "mmhub/mmhub_1_0_sh_mask.h"

#include "ivsrcid/vce/irqsrcs_vce_4_0.h"

#define VCE_STATUS_VCPU_REPORT_FW_LOADED_MASK	0x02

#define VCE_V4_0_FW_SIZE	(384 * 1024)
#define VCE_V4_0_STACK_SIZE	(64 * 1024)
#define VCE_V4_0_DATA_SIZE	((16 * 1024 * AMDGPU_MAX_VCE_HANDLES) + (52 * 1024))

static void vce_v4_0_mc_resume(struct amdgpu_device *adev);
static void vce_v4_0_set_ring_funcs(struct amdgpu_device *adev);
static void vce_v4_0_set_irq_funcs(struct amdgpu_device *adev);

/**
 * vce_v4_0_ring_get_rptr - get read pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Returns the current hardware read pointer
 */
static uint64_t vce_v4_0_ring_get_rptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring->me == 0)
		return RREG32(SOC15_REG_OFFSET(VCE, 0, mmVCE_RB_RPTR));
	else if (ring->me == 1)
		return RREG32(SOC15_REG_OFFSET(VCE, 0, mmVCE_RB_RPTR2));
	else
		return RREG32(SOC15_REG_OFFSET(VCE, 0, mmVCE_RB_RPTR3));
}

/**
 * vce_v4_0_ring_get_wptr - get write pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Returns the current hardware write pointer
 */
static uint64_t vce_v4_0_ring_get_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring->use_doorbell)
		return *ring->wptr_cpu_addr;

	if (ring->me == 0)
		return RREG32(SOC15_REG_OFFSET(VCE, 0, mmVCE_RB_WPTR));
	else if (ring->me == 1)
		return RREG32(SOC15_REG_OFFSET(VCE, 0, mmVCE_RB_WPTR2));
	else
		return RREG32(SOC15_REG_OFFSET(VCE, 0, mmVCE_RB_WPTR3));
}

/**
 * vce_v4_0_ring_set_wptr - set write pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Commits the write pointer to the hardware
 */
static void vce_v4_0_ring_set_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring->use_doorbell) {
		/* XXX check if swapping is necessary on BE */
		*ring->wptr_cpu_addr = lower_32_bits(ring->wptr);
		WDOORBELL32(ring->doorbell_index, lower_32_bits(ring->wptr));
		return;
	}

	if (ring->me == 0)
		WREG32(SOC15_REG_OFFSET(VCE, 0, mmVCE_RB_WPTR),
			lower_32_bits(ring->wptr));
	else if (ring->me == 1)
		WREG32(SOC15_REG_OFFSET(VCE, 0, mmVCE_RB_WPTR2),
			lower_32_bits(ring->wptr));
	else
		WREG32(SOC15_REG_OFFSET(VCE, 0, mmVCE_RB_WPTR3),
			lower_32_bits(ring->wptr));
}

static int vce_v4_0_firmware_loaded(struct amdgpu_device *adev)
{
	int i, j;

	for (i = 0; i < 10; ++i) {
		for (j = 0; j < 100; ++j) {
			uint32_t status =
				RREG32(SOC15_REG_OFFSET(VCE, 0, mmVCE_STATUS));

			if (status & VCE_STATUS_VCPU_REPORT_FW_LOADED_MASK)
				return 0;
			mdelay(10);
		}

		DRM_ERROR("VCE not responding, trying to reset the ECPU!!!\n");
		WREG32_P(SOC15_REG_OFFSET(VCE, 0, mmVCE_SOFT_RESET),
				VCE_SOFT_RESET__ECPU_SOFT_RESET_MASK,
				~VCE_SOFT_RESET__ECPU_SOFT_RESET_MASK);
		mdelay(10);
		WREG32_P(SOC15_REG_OFFSET(VCE, 0, mmVCE_SOFT_RESET), 0,
				~VCE_SOFT_RESET__ECPU_SOFT_RESET_MASK);
		mdelay(10);

	}

	return -ETIMEDOUT;
}

static int vce_v4_0_mmsch_start(struct amdgpu_device *adev,
				struct amdgpu_mm_table *table)
{
	uint32_t data = 0, loop;
	uint64_t addr = table->gpu_addr;
	struct mmsch_v1_0_init_header *header = (struct mmsch_v1_0_init_header *)table->cpu_addr;
	uint32_t size;

	size = header->header_size + header->vce_table_size + header->uvd_table_size;

	/* 1, write to vce_mmsch_vf_ctx_addr_lo/hi register with GPU mc addr of memory descriptor location */
	WREG32(SOC15_REG_OFFSET(VCE, 0, mmVCE_MMSCH_VF_CTX_ADDR_LO), lower_32_bits(addr));
	WREG32(SOC15_REG_OFFSET(VCE, 0, mmVCE_MMSCH_VF_CTX_ADDR_HI), upper_32_bits(addr));

	/* 2, update vmid of descriptor */
	data = RREG32(SOC15_REG_OFFSET(VCE, 0, mmVCE_MMSCH_VF_VMID));
	data &= ~VCE_MMSCH_VF_VMID__VF_CTX_VMID_MASK;
	data |= (0 << VCE_MMSCH_VF_VMID__VF_CTX_VMID__SHIFT); /* use domain0 for MM scheduler */
	WREG32(SOC15_REG_OFFSET(VCE, 0, mmVCE_MMSCH_VF_VMID), data);

	/* 3, notify mmsch about the size of this descriptor */
	WREG32(SOC15_REG_OFFSET(VCE, 0, mmVCE_MMSCH_VF_CTX_SIZE), size);

	/* 4, set resp to zero */
	WREG32(SOC15_REG_OFFSET(VCE, 0, mmVCE_MMSCH_VF_MAILBOX_RESP), 0);

	WDOORBELL32(adev->vce.ring[0].doorbell_index, 0);
	*adev->vce.ring[0].wptr_cpu_addr = 0;
	adev->vce.ring[0].wptr = 0;
	adev->vce.ring[0].wptr_old = 0;

	/* 5, kick off the initialization and wait until VCE_MMSCH_VF_MAILBOX_RESP becomes non-zero */
	WREG32(SOC15_REG_OFFSET(VCE, 0, mmVCE_MMSCH_VF_MAILBOX_HOST), 0x10000001);

	data = RREG32(SOC15_REG_OFFSET(VCE, 0, mmVCE_MMSCH_VF_MAILBOX_RESP));
	loop = 1000;
	while ((data & 0x10000002) != 0x10000002) {
		udelay(10);
		data = RREG32(SOC15_REG_OFFSET(VCE, 0, mmVCE_MMSCH_VF_MAILBOX_RESP));
		loop--;
		if (!loop)
			break;
	}

	if (!loop) {
		dev_err(adev->dev, "failed to init MMSCH, mmVCE_MMSCH_VF_MAILBOX_RESP = %x\n", data);
		return -EBUSY;
	}

	return 0;
}

static int vce_v4_0_sriov_start(struct amdgpu_device *adev)
{
	struct amdgpu_ring *ring;
	uint32_t offset, size;
	uint32_t table_size = 0;
	struct mmsch_v1_0_cmd_direct_write direct_wt = { { 0 } };
	struct mmsch_v1_0_cmd_direct_read_modify_write direct_rd_mod_wt = { { 0 } };
	struct mmsch_v1_0_cmd_direct_polling direct_poll = { { 0 } };
	struct mmsch_v1_0_cmd_end end = { { 0 } };
	uint32_t *init_table = adev->virt.mm_table.cpu_addr;
	struct mmsch_v1_0_init_header *header = (struct mmsch_v1_0_init_header *)init_table;

	direct_wt.cmd_header.command_type = MMSCH_COMMAND__DIRECT_REG_WRITE;
	direct_rd_mod_wt.cmd_header.command_type = MMSCH_COMMAND__DIRECT_REG_READ_MODIFY_WRITE;
	direct_poll.cmd_header.command_type = MMSCH_COMMAND__DIRECT_REG_POLLING;
	end.cmd_header.command_type = MMSCH_COMMAND__END;

	if (header->vce_table_offset == 0 && header->vce_table_size == 0) {
		header->version = MMSCH_VERSION;
		header->header_size = sizeof(struct mmsch_v1_0_init_header) >> 2;

		if (header->uvd_table_offset == 0 && header->uvd_table_size == 0)
			header->vce_table_offset = header->header_size;
		else
			header->vce_table_offset = header->uvd_table_size + header->uvd_table_offset;

		init_table += header->vce_table_offset;

		ring = &adev->vce.ring[0];
		MMSCH_V1_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCE, 0, mmVCE_RB_BASE_LO),
					    lower_32_bits(ring->gpu_addr));
		MMSCH_V1_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCE, 0, mmVCE_RB_BASE_HI),
					    upper_32_bits(ring->gpu_addr));
		MMSCH_V1_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCE, 0, mmVCE_RB_SIZE),
					    ring->ring_size / 4);

		/* BEGING OF MC_RESUME */
		MMSCH_V1_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCE, 0, mmVCE_LMI_CTRL), 0x398000);
		MMSCH_V1_0_INSERT_DIRECT_RD_MOD_WT(SOC15_REG_OFFSET(VCE, 0, mmVCE_LMI_CACHE_CTRL), ~0x1, 0);
		MMSCH_V1_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCE, 0, mmVCE_LMI_SWAP_CNTL), 0);
		MMSCH_V1_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCE, 0, mmVCE_LMI_SWAP_CNTL1), 0);
		MMSCH_V1_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCE, 0, mmVCE_LMI_VM_CTRL), 0);

		offset = AMDGPU_VCE_FIRMWARE_OFFSET;
		if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) {
			uint32_t low = adev->firmware.ucode[AMDGPU_UCODE_ID_VCE].tmr_mc_addr_lo;
			uint32_t hi = adev->firmware.ucode[AMDGPU_UCODE_ID_VCE].tmr_mc_addr_hi;
			uint64_t tmr_mc_addr = (uint64_t)(hi) << 32 | low;

			MMSCH_V1_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCE, 0,
						mmVCE_LMI_VCPU_CACHE_40BIT_BAR0), tmr_mc_addr >> 8);
			MMSCH_V1_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCE, 0,
						mmVCE_LMI_VCPU_CACHE_64BIT_BAR0),
						(tmr_mc_addr >> 40) & 0xff);
			MMSCH_V1_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCE, 0, mmVCE_VCPU_CACHE_OFFSET0), 0);
		} else {
			MMSCH_V1_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCE, 0,
						mmVCE_LMI_VCPU_CACHE_40BIT_BAR0),
						adev->vce.gpu_addr >> 8);
			MMSCH_V1_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCE, 0,
						mmVCE_LMI_VCPU_CACHE_64BIT_BAR0),
						(adev->vce.gpu_addr >> 40) & 0xff);
			MMSCH_V1_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCE, 0, mmVCE_VCPU_CACHE_OFFSET0),
						offset & ~0x0f000000);

		}
		MMSCH_V1_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCE, 0,
						mmVCE_LMI_VCPU_CACHE_40BIT_BAR1),
						adev->vce.gpu_addr >> 8);
		MMSCH_V1_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCE, 0,
						mmVCE_LMI_VCPU_CACHE_64BIT_BAR1),
						(adev->vce.gpu_addr >> 40) & 0xff);
		MMSCH_V1_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCE, 0,
						mmVCE_LMI_VCPU_CACHE_40BIT_BAR2),
						adev->vce.gpu_addr >> 8);
		MMSCH_V1_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCE, 0,
						mmVCE_LMI_VCPU_CACHE_64BIT_BAR2),
						(adev->vce.gpu_addr >> 40) & 0xff);

		size = VCE_V4_0_FW_SIZE;
		MMSCH_V1_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCE, 0, mmVCE_VCPU_CACHE_SIZE0), size);

		offset = (adev->firmware.load_type != AMDGPU_FW_LOAD_PSP) ? offset + size : 0;
		size = VCE_V4_0_STACK_SIZE;
		MMSCH_V1_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCE, 0, mmVCE_VCPU_CACHE_OFFSET1),
					(offset & ~0x0f000000) | (1 << 24));
		MMSCH_V1_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCE, 0, mmVCE_VCPU_CACHE_SIZE1), size);

		offset += size;
		size = VCE_V4_0_DATA_SIZE;
		MMSCH_V1_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCE, 0, mmVCE_VCPU_CACHE_OFFSET2),
					(offset & ~0x0f000000) | (2 << 24));
		MMSCH_V1_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCE, 0, mmVCE_VCPU_CACHE_SIZE2), size);

		MMSCH_V1_0_INSERT_DIRECT_RD_MOD_WT(SOC15_REG_OFFSET(VCE, 0, mmVCE_LMI_CTRL2), ~0x100, 0);
		MMSCH_V1_0_INSERT_DIRECT_RD_MOD_WT(SOC15_REG_OFFSET(VCE, 0, mmVCE_SYS_INT_EN),
						   VCE_SYS_INT_EN__VCE_SYS_INT_TRAP_INTERRUPT_EN_MASK,
						   VCE_SYS_INT_EN__VCE_SYS_INT_TRAP_INTERRUPT_EN_MASK);

		/* end of MC_RESUME */
		MMSCH_V1_0_INSERT_DIRECT_RD_MOD_WT(SOC15_REG_OFFSET(VCE, 0, mmVCE_STATUS),
						   VCE_STATUS__JOB_BUSY_MASK, ~VCE_STATUS__JOB_BUSY_MASK);
		MMSCH_V1_0_INSERT_DIRECT_RD_MOD_WT(SOC15_REG_OFFSET(VCE, 0, mmVCE_VCPU_CNTL),
						   ~0x200001, VCE_VCPU_CNTL__CLK_EN_MASK);
		MMSCH_V1_0_INSERT_DIRECT_RD_MOD_WT(SOC15_REG_OFFSET(VCE, 0, mmVCE_SOFT_RESET),
						   ~VCE_SOFT_RESET__ECPU_SOFT_RESET_MASK, 0);

		MMSCH_V1_0_INSERT_DIRECT_POLL(SOC15_REG_OFFSET(VCE, 0, mmVCE_STATUS),
					      VCE_STATUS_VCPU_REPORT_FW_LOADED_MASK,
					      VCE_STATUS_VCPU_REPORT_FW_LOADED_MASK);

		/* clear BUSY flag */
		MMSCH_V1_0_INSERT_DIRECT_RD_MOD_WT(SOC15_REG_OFFSET(VCE, 0, mmVCE_STATUS),
						   ~VCE_STATUS__JOB_BUSY_MASK, 0);

		/* add end packet */
		memcpy((void *)init_table, &end, sizeof(struct mmsch_v1_0_cmd_end));
		table_size += sizeof(struct mmsch_v1_0_cmd_end) / 4;
		header->vce_table_size = table_size;
	}

	return vce_v4_0_mmsch_start(adev, &adev->virt.mm_table);
}

/**
 * vce_v4_0_start - start VCE block
 *
 * @adev: amdgpu_device pointer
 *
 * Setup and start the VCE block
 */
static int vce_v4_0_start(struct amdgpu_device *adev)
{
	struct amdgpu_ring *ring;
	int r;

	ring = &adev->vce.ring[0];

	WREG32(SOC15_REG_OFFSET(VCE, 0, mmVCE_RB_RPTR), lower_32_bits(ring->wptr));
	WREG32(SOC15_REG_OFFSET(VCE, 0, mmVCE_RB_WPTR), lower_32_bits(ring->wptr));
	WREG32(SOC15_REG_OFFSET(VCE, 0, mmVCE_RB_BASE_LO), ring->gpu_addr);
	WREG32(SOC15_REG_OFFSET(VCE, 0, mmVCE_RB_BASE_HI), upper_32_bits(ring->gpu_addr));
	WREG32(SOC15_REG_OFFSET(VCE, 0, mmVCE_RB_SIZE), ring->ring_size / 4);

	ring = &adev->vce.ring[1];

	WREG32(SOC15_REG_OFFSET(VCE, 0, mmVCE_RB_RPTR2), lower_32_bits(ring->wptr));
	WREG32(SOC15_REG_OFFSET(VCE, 0, mmVCE_RB_WPTR2), lower_32_bits(ring->wptr));
	WREG32(SOC15_REG_OFFSET(VCE, 0, mmVCE_RB_BASE_LO2), ring->gpu_addr);
	WREG32(SOC15_REG_OFFSET(VCE, 0, mmVCE_RB_BASE_HI2), upper_32_bits(ring->gpu_addr));
	WREG32(SOC15_REG_OFFSET(VCE, 0, mmVCE_RB_SIZE2), ring->ring_size / 4);

	ring = &adev->vce.ring[2];

	WREG32(SOC15_REG_OFFSET(VCE, 0, mmVCE_RB_RPTR3), lower_32_bits(ring->wptr));
	WREG32(SOC15_REG_OFFSET(VCE, 0, mmVCE_RB_WPTR3), lower_32_bits(ring->wptr));
	WREG32(SOC15_REG_OFFSET(VCE, 0, mmVCE_RB_BASE_LO3), ring->gpu_addr);
	WREG32(SOC15_REG_OFFSET(VCE, 0, mmVCE_RB_BASE_HI3), upper_32_bits(ring->gpu_addr));
	WREG32(SOC15_REG_OFFSET(VCE, 0, mmVCE_RB_SIZE3), ring->ring_size / 4);

	vce_v4_0_mc_resume(adev);
	WREG32_P(SOC15_REG_OFFSET(VCE, 0, mmVCE_STATUS), VCE_STATUS__JOB_BUSY_MASK,
			~VCE_STATUS__JOB_BUSY_MASK);

	WREG32_P(SOC15_REG_OFFSET(VCE, 0, mmVCE_VCPU_CNTL), 1, ~0x200001);

	WREG32_P(SOC15_REG_OFFSET(VCE, 0, mmVCE_SOFT_RESET), 0,
			~VCE_SOFT_RESET__ECPU_SOFT_RESET_MASK);
	mdelay(100);

	r = vce_v4_0_firmware_loaded(adev);

	/* clear BUSY flag */
	WREG32_P(SOC15_REG_OFFSET(VCE, 0, mmVCE_STATUS), 0, ~VCE_STATUS__JOB_BUSY_MASK);

	if (r) {
		DRM_ERROR("VCE not responding, giving up!!!\n");
		return r;
	}

	return 0;
}

static int vce_v4_0_stop(struct amdgpu_device *adev)
{

	/* Disable VCPU */
	WREG32_P(SOC15_REG_OFFSET(VCE, 0, mmVCE_VCPU_CNTL), 0, ~0x200001);

	/* hold on ECPU */
	WREG32_P(SOC15_REG_OFFSET(VCE, 0, mmVCE_SOFT_RESET),
			VCE_SOFT_RESET__ECPU_SOFT_RESET_MASK,
			~VCE_SOFT_RESET__ECPU_SOFT_RESET_MASK);

	/* clear VCE_STATUS */
	WREG32(SOC15_REG_OFFSET(VCE, 0, mmVCE_STATUS), 0);

	/* Set Clock-Gating off */
	/* if (adev->cg_flags & AMD_CG_SUPPORT_VCE_MGCG)
		vce_v4_0_set_vce_sw_clock_gating(adev, false);
	*/

	return 0;
}

static int vce_v4_0_early_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;

	if (amdgpu_sriov_vf(adev)) /* currently only VCN0 support SRIOV */
		adev->vce.num_rings = 1;
	else
		adev->vce.num_rings = 3;

	vce_v4_0_set_ring_funcs(adev);
	vce_v4_0_set_irq_funcs(adev);

	return 0;
}

static int vce_v4_0_sw_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	struct amdgpu_ring *ring;

	unsigned size;
	int r, i;

	r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_VCE0, 167, &adev->vce.irq);
	if (r)
		return r;

	size  = VCE_V4_0_STACK_SIZE + VCE_V4_0_DATA_SIZE;
	if (adev->firmware.load_type != AMDGPU_FW_LOAD_PSP)
		size += VCE_V4_0_FW_SIZE;

	r = amdgpu_vce_sw_init(adev, size);
	if (r)
		return r;

	if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) {
		const struct common_firmware_header *hdr;
		unsigned size = amdgpu_bo_size(adev->vce.vcpu_bo);

		adev->vce.saved_bo = kvmalloc(size, GFP_KERNEL);
		if (!adev->vce.saved_bo)
			return -ENOMEM;

		hdr = (const struct common_firmware_header *)adev->vce.fw->data;
		adev->firmware.ucode[AMDGPU_UCODE_ID_VCE].ucode_id = AMDGPU_UCODE_ID_VCE;
		adev->firmware.ucode[AMDGPU_UCODE_ID_VCE].fw = adev->vce.fw;
		adev->firmware.fw_size +=
			ALIGN(le32_to_cpu(hdr->ucode_size_bytes), PAGE_SIZE);
		DRM_INFO("PSP loading VCE firmware\n");
	} else {
		r = amdgpu_vce_resume(adev);
		if (r)
			return r;
	}

	for (i = 0; i < adev->vce.num_rings; i++) {
		enum amdgpu_ring_priority_level hw_prio = amdgpu_vce_get_ring_prio(i);

		ring = &adev->vce.ring[i];
		ring->vm_hub = AMDGPU_MMHUB0(0);
		sprintf(ring->name, "vce%d", i);
		if (amdgpu_sriov_vf(adev)) {
			/* DOORBELL only works under SRIOV */
			ring->use_doorbell = true;

			/* currently only use the first encoding ring for sriov,
			 * so set unused location for other unused rings.
			 */
			if (i == 0)
				ring->doorbell_index = adev->doorbell_index.uvd_vce.vce_ring0_1 * 2;
			else
				ring->doorbell_index = adev->doorbell_index.uvd_vce.vce_ring2_3 * 2 + 1;
		}
		r = amdgpu_ring_init(adev, ring, 512, &adev->vce.irq, 0,
				     hw_prio, NULL);
		if (r)
			return r;
	}

	r = amdgpu_virt_alloc_mm_table(adev);
	if (r)
		return r;

	return r;
}

static int vce_v4_0_sw_fini(struct amdgpu_ip_block *ip_block)
{
	int r;
	struct amdgpu_device *adev = ip_block->adev;

	/* free MM table */
	amdgpu_virt_free_mm_table(adev);

	if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) {
		kvfree(adev->vce.saved_bo);
		adev->vce.saved_bo = NULL;
	}

	r = amdgpu_vce_suspend(adev);
	if (r)
		return r;

	return amdgpu_vce_sw_fini(adev);
}

static int vce_v4_0_hw_init(struct amdgpu_ip_block *ip_block)
{
	int r, i;
	struct amdgpu_device *adev = ip_block->adev;

	if (amdgpu_sriov_vf(adev))
		r = vce_v4_0_sriov_start(adev);
	else
		r = vce_v4_0_start(adev);
	if (r)
		return r;

	for (i = 0; i < adev->vce.num_rings; i++) {
		r = amdgpu_ring_test_helper(&adev->vce.ring[i]);
		if (r)
			return r;
	}

	DRM_INFO("VCE initialized successfully.\n");

	return 0;
}

static int vce_v4_0_hw_fini(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;

	cancel_delayed_work_sync(&adev->vce.idle_work);

	if (!amdgpu_sriov_vf(adev)) {
		/* vce_v4_0_wait_for_idle(ip_block); */
		vce_v4_0_stop(adev);
	} else {
		/* full access mode, so don't touch any VCE register */
		DRM_DEBUG("For SRIOV client, shouldn't do anything.\n");
	}

	return 0;
}

static int vce_v4_0_suspend(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	int r, idx;

	if (adev->vce.vcpu_bo == NULL)
		return 0;

	if (drm_dev_enter(adev_to_drm(adev), &idx)) {
		if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) {
			unsigned size = amdgpu_bo_size(adev->vce.vcpu_bo);
			void *ptr = adev->vce.cpu_addr;

			memcpy_fromio(adev->vce.saved_bo, ptr, size);
		}
		drm_dev_exit(idx);
	}

	/*
	 * Proper cleanups before halting the HW engine:
	 *   - cancel the delayed idle work
	 *   - enable powergating
	 *   - enable clockgating
	 *   - disable dpm
	 *
	 * TODO: to align with the VCN implementation, move the
	 * jobs for clockgating/powergating/dpm setting to
	 * ->set_powergating_state().
	 */
	cancel_delayed_work_sync(&adev->vce.idle_work);

	if (adev->pm.dpm_enabled) {
		amdgpu_dpm_enable_vce(adev, false);
	} else {
		amdgpu_asic_set_vce_clocks(adev, 0, 0);
		amdgpu_device_ip_set_powergating_state(adev, AMD_IP_BLOCK_TYPE_VCE,
						       AMD_PG_STATE_GATE);
		amdgpu_device_ip_set_clockgating_state(adev, AMD_IP_BLOCK_TYPE_VCE,
						       AMD_CG_STATE_GATE);
	}

	r = vce_v4_0_hw_fini(ip_block);
	if (r)
		return r;

	return amdgpu_vce_suspend(adev);
}

static int vce_v4_0_resume(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	int r, idx;

	if (adev->vce.vcpu_bo == NULL)
		return -EINVAL;

	if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) {

		if (drm_dev_enter(adev_to_drm(adev), &idx)) {
			unsigned size = amdgpu_bo_size(adev->vce.vcpu_bo);
			void *ptr = adev->vce.cpu_addr;

			memcpy_toio(ptr, adev->vce.saved_bo, size);
			drm_dev_exit(idx);
		}
	} else {
		r = amdgpu_vce_resume(adev);
		if (r)
			return r;
	}

	return vce_v4_0_hw_init(ip_block);
}

static void vce_v4_0_mc_resume(struct amdgpu_device *adev)
{
	uint32_t offset, size;
	uint64_t tmr_mc_addr;

	WREG32_P(SOC15_REG_OFFSET(VCE, 0, mmVCE_CLOCK_GATING_A), 0, ~(1 << 16));
	WREG32_P(SOC15_REG_OFFSET(VCE, 0, mmVCE_UENC_CLOCK_GATING), 0x1FF000, ~0xFF9FF000);
	WREG32_P(SOC15_REG_OFFSET(VCE, 0, mmVCE_UENC_REG_CLOCK_GATING), 0x3F, ~0x3F);
	WREG32(SOC15_REG_OFFSET(VCE, 0, mmVCE_CLOCK_GATING_B), 0x1FF);

	WREG32(SOC15_REG_OFFSET(VCE, 0, mmVCE_LMI_CTRL), 0x00398000);
	WREG32_P(SOC15_REG_OFFSET(VCE, 0, mmVCE_LMI_CACHE_CTRL), 0x0, ~0x1);
	WREG32(SOC15_REG_OFFSET(VCE, 0, mmVCE_LMI_SWAP_CNTL), 0);
	WREG32(SOC15_REG_OFFSET(VCE, 0, mmVCE_LMI_SWAP_CNTL1), 0);
	WREG32(SOC15_REG_OFFSET(VCE, 0, mmVCE_LMI_VM_CTRL), 0);

	offset = AMDGPU_VCE_FIRMWARE_OFFSET;

	if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) {
		tmr_mc_addr = (uint64_t)(adev->firmware.ucode[AMDGPU_UCODE_ID_VCE].tmr_mc_addr_hi) << 32 |
										adev->firmware.ucode[AMDGPU_UCODE_ID_VCE].tmr_mc_addr_lo;
		WREG32(SOC15_REG_OFFSET(VCE, 0, mmVCE_LMI_VCPU_CACHE_40BIT_BAR0),
			(tmr_mc_addr >> 8));
		WREG32(SOC15_REG_OFFSET(VCE, 0, mmVCE_LMI_VCPU_CACHE_64BIT_BAR0),
			(tmr_mc_addr >> 40) & 0xff);
		WREG32(SOC15_REG_OFFSET(VCE, 0, mmVCE_VCPU_CACHE_OFFSET0), 0);
	} else {
		WREG32(SOC15_REG_OFFSET(VCE, 0, mmVCE_LMI_VCPU_CACHE_40BIT_BAR0),
			(adev->vce.gpu_addr >> 8));
		WREG32(SOC15_REG_OFFSET(VCE, 0, mmVCE_LMI_VCPU_CACHE_64BIT_BAR0),
			(adev->vce.gpu_addr >> 40) & 0xff);
		WREG32(SOC15_REG_OFFSET(VCE, 0, mmVCE_VCPU_CACHE_OFFSET0), offset & ~0x0f000000);
	}

	size = VCE_V4_0_FW_SIZE;
	WREG32(SOC15_REG_OFFSET(VCE, 0, mmVCE_VCPU_CACHE_SIZE0), size);

	WREG32(SOC15_REG_OFFSET(VCE, 0, mmVCE_LMI_VCPU_CACHE_40BIT_BAR1), (adev->vce.gpu_addr >> 8));
	WREG32(SOC15_REG_OFFSET(VCE, 0, mmVCE_LMI_VCPU_CACHE_64BIT_BAR1), (adev->vce.gpu_addr >> 40) & 0xff);
	offset = (adev->firmware.load_type != AMDGPU_FW_LOAD_PSP) ? offset + size : 0;
	size = VCE_V4_0_STACK_SIZE;
	WREG32(SOC15_REG_OFFSET(VCE, 0, mmVCE_VCPU_CACHE_OFFSET1), (offset & ~0x0f000000) | (1 << 24));
	WREG32(SOC15_REG_OFFSET(VCE, 0, mmVCE_VCPU_CACHE_SIZE1), size);

	WREG32(SOC15_REG_OFFSET(VCE, 0, mmVCE_LMI_VCPU_CACHE_40BIT_BAR2), (adev->vce.gpu_addr >> 8));
	WREG32(SOC15_REG_OFFSET(VCE, 0, mmVCE_LMI_VCPU_CACHE_64BIT_BAR2), (adev->vce.gpu_addr >> 40) & 0xff);
	offset += size;
	size = VCE_V4_0_DATA_SIZE;
	WREG32(SOC15_REG_OFFSET(VCE, 0, mmVCE_VCPU_CACHE_OFFSET2), (offset & ~0x0f000000) | (2 << 24));
	WREG32(SOC15_REG_OFFSET(VCE, 0, mmVCE_VCPU_CACHE_SIZE2), size);

	WREG32_P(SOC15_REG_OFFSET(VCE, 0, mmVCE_LMI_CTRL2), 0x0, ~0x100);
	WREG32_P(SOC15_REG_OFFSET(VCE, 0, mmVCE_SYS_INT_EN),
			VCE_SYS_INT_EN__VCE_SYS_INT_TRAP_INTERRUPT_EN_MASK,
			~VCE_SYS_INT_EN__VCE_SYS_INT_TRAP_INTERRUPT_EN_MASK);
}

static int vce_v4_0_set_clockgating_state(void *handle,
					  enum amd_clockgating_state state)
{
	/* needed for driver unload*/
	return 0;
}

static int vce_v4_0_set_powergating_state(struct amdgpu_ip_block *ip_block,
					  enum amd_powergating_state state)
{
	/* This doesn't actually powergate the VCE block.
	 * That's done in the dpm code via the SMC.  This
	 * just re-inits the block as necessary.  The actual
	 * gating still happens in the dpm code.  We should
	 * revisit this when there is a cleaner line between
	 * the smc and the hw blocks
	 */
	struct amdgpu_device *adev = ip_block->adev;

	if (state == AMD_PG_STATE_GATE)
		return vce_v4_0_stop(adev);
	else
		return vce_v4_0_start(adev);
}

static void vce_v4_0_ring_emit_ib(struct amdgpu_ring *ring, struct amdgpu_job *job,
					struct amdgpu_ib *ib, uint32_t flags)
{
	unsigned vmid = AMDGPU_JOB_GET_VMID(job);

	amdgpu_ring_write(ring, VCE_CMD_IB_VM);
	amdgpu_ring_write(ring, vmid);
	amdgpu_ring_write(ring, lower_32_bits(ib->gpu_addr));
	amdgpu_ring_write(ring, upper_32_bits(ib->gpu_addr));
	amdgpu_ring_write(ring, ib->length_dw);
}

static void vce_v4_0_ring_emit_fence(struct amdgpu_ring *ring, u64 addr,
			u64 seq, unsigned flags)
{
	WARN_ON(flags & AMDGPU_FENCE_FLAG_64BIT);

	amdgpu_ring_write(ring, VCE_CMD_FENCE);
	amdgpu_ring_write(ring, addr);
	amdgpu_ring_write(ring, upper_32_bits(addr));
	amdgpu_ring_write(ring, seq);
	amdgpu_ring_write(ring, VCE_CMD_TRAP);
}

static void vce_v4_0_ring_insert_end(struct amdgpu_ring *ring)
{
	amdgpu_ring_write(ring, VCE_CMD_END);
}

static void vce_v4_0_emit_reg_wait(struct amdgpu_ring *ring, uint32_t reg,
				   uint32_t val, uint32_t mask)
{
	amdgpu_ring_write(ring, VCE_CMD_REG_WAIT);
	amdgpu_ring_write(ring,	reg << 2);
	amdgpu_ring_write(ring, mask);
	amdgpu_ring_write(ring, val);
}

static void vce_v4_0_emit_vm_flush(struct amdgpu_ring *ring,
				   unsigned int vmid, uint64_t pd_addr)
{
	struct amdgpu_vmhub *hub = &ring->adev->vmhub[ring->vm_hub];

	pd_addr = amdgpu_gmc_emit_flush_gpu_tlb(ring, vmid, pd_addr);

	/* wait for reg writes */
	vce_v4_0_emit_reg_wait(ring, hub->ctx0_ptb_addr_lo32 +
			       vmid * hub->ctx_addr_distance,
			       lower_32_bits(pd_addr), 0xffffffff);
}

static void vce_v4_0_emit_wreg(struct amdgpu_ring *ring,
			       uint32_t reg, uint32_t val)
{
	amdgpu_ring_write(ring, VCE_CMD_REG_WRITE);
	amdgpu_ring_write(ring,	reg << 2);
	amdgpu_ring_write(ring, val);
}

static int vce_v4_0_set_interrupt_state(struct amdgpu_device *adev,
					struct amdgpu_irq_src *source,
					unsigned type,
					enum amdgpu_interrupt_state state)
{
	uint32_t val = 0;

	if (!amdgpu_sriov_vf(adev)) {
		if (state == AMDGPU_IRQ_STATE_ENABLE)
			val |= VCE_SYS_INT_EN__VCE_SYS_INT_TRAP_INTERRUPT_EN_MASK;

		WREG32_P(SOC15_REG_OFFSET(VCE, 0, mmVCE_SYS_INT_EN), val,
				~VCE_SYS_INT_EN__VCE_SYS_INT_TRAP_INTERRUPT_EN_MASK);
	}
	return 0;
}

static int vce_v4_0_process_interrupt(struct amdgpu_device *adev,
				      struct amdgpu_irq_src *source,
				      struct amdgpu_iv_entry *entry)
{
	DRM_DEBUG("IH: VCE\n");

	switch (entry->src_data[0]) {
	case 0:
	case 1:
	case 2:
		amdgpu_fence_process(&adev->vce.ring[entry->src_data[0]]);
		break;
	default:
		DRM_ERROR("Unhandled interrupt: %d %d\n",
			  entry->src_id, entry->src_data[0]);
		break;
	}

	return 0;
}

const struct amd_ip_funcs vce_v4_0_ip_funcs = {
	.name = "vce_v4_0",
	.early_init = vce_v4_0_early_init,
	.sw_init = vce_v4_0_sw_init,
	.sw_fini = vce_v4_0_sw_fini,
	.hw_init = vce_v4_0_hw_init,
	.hw_fini = vce_v4_0_hw_fini,
	.suspend = vce_v4_0_suspend,
	.resume = vce_v4_0_resume,
	.set_clockgating_state = vce_v4_0_set_clockgating_state,
	.set_powergating_state = vce_v4_0_set_powergating_state,
};

static const struct amdgpu_ring_funcs vce_v4_0_ring_vm_funcs = {
	.type = AMDGPU_RING_TYPE_VCE,
	.align_mask = 0x3f,
	.nop = VCE_CMD_NO_OP,
	.support_64bit_ptrs = false,
	.no_user_fence = true,
	.get_rptr = vce_v4_0_ring_get_rptr,
	.get_wptr = vce_v4_0_ring_get_wptr,
	.set_wptr = vce_v4_0_ring_set_wptr,
	.patch_cs_in_place = amdgpu_vce_ring_parse_cs_vm,
	.emit_frame_size =
		SOC15_FLUSH_GPU_TLB_NUM_WREG * 3 +
		SOC15_FLUSH_GPU_TLB_NUM_REG_WAIT * 4 +
		4 + /* vce_v4_0_emit_vm_flush */
		5 + 5 + /* amdgpu_vce_ring_emit_fence x2 vm fence */
		1, /* vce_v4_0_ring_insert_end */
	.emit_ib_size = 5, /* vce_v4_0_ring_emit_ib */
	.emit_ib = vce_v4_0_ring_emit_ib,
	.emit_vm_flush = vce_v4_0_emit_vm_flush,
	.emit_fence = vce_v4_0_ring_emit_fence,
	.test_ring = amdgpu_vce_ring_test_ring,
	.test_ib = amdgpu_vce_ring_test_ib,
	.insert_nop = amdgpu_ring_insert_nop,
	.insert_end = vce_v4_0_ring_insert_end,
	.pad_ib = amdgpu_ring_generic_pad_ib,
	.begin_use = amdgpu_vce_ring_begin_use,
	.end_use = amdgpu_vce_ring_end_use,
	.emit_wreg = vce_v4_0_emit_wreg,
	.emit_reg_wait = vce_v4_0_emit_reg_wait,
	.emit_reg_write_reg_wait = amdgpu_ring_emit_reg_write_reg_wait_helper,
};

static void vce_v4_0_set_ring_funcs(struct amdgpu_device *adev)
{
	int i;

	for (i = 0; i < adev->vce.num_rings; i++) {
		adev->vce.ring[i].funcs = &vce_v4_0_ring_vm_funcs;
		adev->vce.ring[i].me = i;
	}
	DRM_INFO("VCE enabled in VM mode\n");
}

static const struct amdgpu_irq_src_funcs vce_v4_0_irq_funcs = {
	.set = vce_v4_0_set_interrupt_state,
	.process = vce_v4_0_process_interrupt,
};

static void vce_v4_0_set_irq_funcs(struct amdgpu_device *adev)
{
	adev->vce.irq.num_types = 1;
	adev->vce.irq.funcs = &vce_v4_0_irq_funcs;
};

const struct amdgpu_ip_block_version vce_v4_0_ip_block =
{
	.type = AMD_IP_BLOCK_TYPE_VCE,
	.major = 4,
	.minor = 0,
	.rev = 0,
	.funcs = &vce_v4_0_ip_funcs,
};
