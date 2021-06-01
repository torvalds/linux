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

#include <linux/firmware.h>
#include "amdgpu.h"
#include "amdgpu_vcn.h"
#include "amdgpu_pm.h"
#include "soc15.h"
#include "soc15d.h"
#include "vcn_v2_0.h"
#include "mmsch_v3_0.h"

#include "vcn/vcn_3_0_0_offset.h"
#include "vcn/vcn_3_0_0_sh_mask.h"
#include "ivsrcid/vcn/irqsrcs_vcn_2_0.h"

#define mmUVD_CONTEXT_ID_INTERNAL_OFFSET			0x27
#define mmUVD_GPCOM_VCPU_CMD_INTERNAL_OFFSET			0x0f
#define mmUVD_GPCOM_VCPU_DATA0_INTERNAL_OFFSET			0x10
#define mmUVD_GPCOM_VCPU_DATA1_INTERNAL_OFFSET			0x11
#define mmUVD_NO_OP_INTERNAL_OFFSET				0x29
#define mmUVD_GP_SCRATCH8_INTERNAL_OFFSET			0x66
#define mmUVD_SCRATCH9_INTERNAL_OFFSET				0xc01d

#define mmUVD_LMI_RBC_IB_VMID_INTERNAL_OFFSET			0x431
#define mmUVD_LMI_RBC_IB_64BIT_BAR_LOW_INTERNAL_OFFSET		0x3b4
#define mmUVD_LMI_RBC_IB_64BIT_BAR_HIGH_INTERNAL_OFFSET		0x3b5
#define mmUVD_RBC_IB_SIZE_INTERNAL_OFFSET			0x25c

#define VCN_INSTANCES_SIENNA_CICHLID				2
#define DEC_SW_RING_ENABLED					FALSE

#define RDECODE_MSG_CREATE					0x00000000
#define RDECODE_MESSAGE_CREATE					0x00000001

static int amdgpu_ih_clientid_vcns[] = {
	SOC15_IH_CLIENTID_VCN,
	SOC15_IH_CLIENTID_VCN1
};

static int amdgpu_ucode_id_vcns[] = {
	AMDGPU_UCODE_ID_VCN,
	AMDGPU_UCODE_ID_VCN1
};

static int vcn_v3_0_start_sriov(struct amdgpu_device *adev);
static void vcn_v3_0_set_dec_ring_funcs(struct amdgpu_device *adev);
static void vcn_v3_0_set_enc_ring_funcs(struct amdgpu_device *adev);
static void vcn_v3_0_set_irq_funcs(struct amdgpu_device *adev);
static int vcn_v3_0_set_powergating_state(void *handle,
			enum amd_powergating_state state);
static int vcn_v3_0_pause_dpg_mode(struct amdgpu_device *adev,
			int inst_idx, struct dpg_pause_state *new_state);

static void vcn_v3_0_dec_ring_set_wptr(struct amdgpu_ring *ring);
static void vcn_v3_0_enc_ring_set_wptr(struct amdgpu_ring *ring);

/**
 * vcn_v3_0_early_init - set function pointers
 *
 * @handle: amdgpu_device pointer
 *
 * Set ring and irq function pointers
 */
static int vcn_v3_0_early_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (amdgpu_sriov_vf(adev)) {
		adev->vcn.num_vcn_inst = VCN_INSTANCES_SIENNA_CICHLID;
		adev->vcn.harvest_config = 0;
		adev->vcn.num_enc_rings = 1;

	} else {
		if (adev->asic_type == CHIP_SIENNA_CICHLID) {
			u32 harvest;
			int i;

			adev->vcn.num_vcn_inst = VCN_INSTANCES_SIENNA_CICHLID;
			for (i = 0; i < adev->vcn.num_vcn_inst; i++) {
				harvest = RREG32_SOC15(VCN, i, mmCC_UVD_HARVESTING);
				if (harvest & CC_UVD_HARVESTING__UVD_DISABLE_MASK)
					adev->vcn.harvest_config |= 1 << i;
			}

			if (adev->vcn.harvest_config == (AMDGPU_VCN_HARVEST_VCN0 |
						AMDGPU_VCN_HARVEST_VCN1))
				/* both instances are harvested, disable the block */
				return -ENOENT;
		} else
			adev->vcn.num_vcn_inst = 1;

		adev->vcn.num_enc_rings = 2;
	}

	vcn_v3_0_set_dec_ring_funcs(adev);
	vcn_v3_0_set_enc_ring_funcs(adev);
	vcn_v3_0_set_irq_funcs(adev);

	return 0;
}

/**
 * vcn_v3_0_sw_init - sw init for VCN block
 *
 * @handle: amdgpu_device pointer
 *
 * Load firmware and sw initialization
 */
static int vcn_v3_0_sw_init(void *handle)
{
	struct amdgpu_ring *ring;
	int i, j, r;
	int vcn_doorbell_index = 0;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	r = amdgpu_vcn_sw_init(adev);
	if (r)
		return r;

	if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) {
		const struct common_firmware_header *hdr;
		hdr = (const struct common_firmware_header *)adev->vcn.fw->data;
		adev->firmware.ucode[AMDGPU_UCODE_ID_VCN].ucode_id = AMDGPU_UCODE_ID_VCN;
		adev->firmware.ucode[AMDGPU_UCODE_ID_VCN].fw = adev->vcn.fw;
		adev->firmware.fw_size +=
			ALIGN(le32_to_cpu(hdr->ucode_size_bytes), PAGE_SIZE);

		if (adev->vcn.num_vcn_inst == VCN_INSTANCES_SIENNA_CICHLID) {
			adev->firmware.ucode[AMDGPU_UCODE_ID_VCN1].ucode_id = AMDGPU_UCODE_ID_VCN1;
			adev->firmware.ucode[AMDGPU_UCODE_ID_VCN1].fw = adev->vcn.fw;
			adev->firmware.fw_size +=
				ALIGN(le32_to_cpu(hdr->ucode_size_bytes), PAGE_SIZE);
		}
		DRM_INFO("PSP loading VCN firmware\n");
	}

	r = amdgpu_vcn_resume(adev);
	if (r)
		return r;

	/*
	 * Note: doorbell assignment is fixed for SRIOV multiple VCN engines
	 * Formula:
	 *   vcn_db_base  = adev->doorbell_index.vcn.vcn_ring0_1 << 1;
	 *   dec_ring_i   = vcn_db_base + i * (adev->vcn.num_enc_rings + 1)
	 *   enc_ring_i,j = vcn_db_base + i * (adev->vcn.num_enc_rings + 1) + 1 + j
	 */
	if (amdgpu_sriov_vf(adev)) {
		vcn_doorbell_index = adev->doorbell_index.vcn.vcn_ring0_1;
		/* get DWORD offset */
		vcn_doorbell_index = vcn_doorbell_index << 1;
	}

	for (i = 0; i < adev->vcn.num_vcn_inst; i++) {
		volatile struct amdgpu_fw_shared *fw_shared;

		if (adev->vcn.harvest_config & (1 << i))
			continue;

		adev->vcn.internal.context_id = mmUVD_CONTEXT_ID_INTERNAL_OFFSET;
		adev->vcn.internal.ib_vmid = mmUVD_LMI_RBC_IB_VMID_INTERNAL_OFFSET;
		adev->vcn.internal.ib_bar_low = mmUVD_LMI_RBC_IB_64BIT_BAR_LOW_INTERNAL_OFFSET;
		adev->vcn.internal.ib_bar_high = mmUVD_LMI_RBC_IB_64BIT_BAR_HIGH_INTERNAL_OFFSET;
		adev->vcn.internal.ib_size = mmUVD_RBC_IB_SIZE_INTERNAL_OFFSET;
		adev->vcn.internal.gp_scratch8 = mmUVD_GP_SCRATCH8_INTERNAL_OFFSET;

		adev->vcn.internal.scratch9 = mmUVD_SCRATCH9_INTERNAL_OFFSET;
		adev->vcn.inst[i].external.scratch9 = SOC15_REG_OFFSET(VCN, i, mmUVD_SCRATCH9);
		adev->vcn.internal.data0 = mmUVD_GPCOM_VCPU_DATA0_INTERNAL_OFFSET;
		adev->vcn.inst[i].external.data0 = SOC15_REG_OFFSET(VCN, i, mmUVD_GPCOM_VCPU_DATA0);
		adev->vcn.internal.data1 = mmUVD_GPCOM_VCPU_DATA1_INTERNAL_OFFSET;
		adev->vcn.inst[i].external.data1 = SOC15_REG_OFFSET(VCN, i, mmUVD_GPCOM_VCPU_DATA1);
		adev->vcn.internal.cmd = mmUVD_GPCOM_VCPU_CMD_INTERNAL_OFFSET;
		adev->vcn.inst[i].external.cmd = SOC15_REG_OFFSET(VCN, i, mmUVD_GPCOM_VCPU_CMD);
		adev->vcn.internal.nop = mmUVD_NO_OP_INTERNAL_OFFSET;
		adev->vcn.inst[i].external.nop = SOC15_REG_OFFSET(VCN, i, mmUVD_NO_OP);

		/* VCN DEC TRAP */
		r = amdgpu_irq_add_id(adev, amdgpu_ih_clientid_vcns[i],
				VCN_2_0__SRCID__UVD_SYSTEM_MESSAGE_INTERRUPT, &adev->vcn.inst[i].irq);
		if (r)
			return r;

		atomic_set(&adev->vcn.inst[i].sched_score, 0);

		ring = &adev->vcn.inst[i].ring_dec;
		ring->use_doorbell = true;
		if (amdgpu_sriov_vf(adev)) {
			ring->doorbell_index = vcn_doorbell_index + i * (adev->vcn.num_enc_rings + 1);
		} else {
			ring->doorbell_index = (adev->doorbell_index.vcn.vcn_ring0_1 << 1) + 8 * i;
		}
		sprintf(ring->name, "vcn_dec_%d", i);
		r = amdgpu_ring_init(adev, ring, 512, &adev->vcn.inst[i].irq, 0,
				     AMDGPU_RING_PRIO_DEFAULT,
				     &adev->vcn.inst[i].sched_score);
		if (r)
			return r;

		for (j = 0; j < adev->vcn.num_enc_rings; ++j) {
			/* VCN ENC TRAP */
			r = amdgpu_irq_add_id(adev, amdgpu_ih_clientid_vcns[i],
				j + VCN_2_0__SRCID__UVD_ENC_GENERAL_PURPOSE, &adev->vcn.inst[i].irq);
			if (r)
				return r;

			ring = &adev->vcn.inst[i].ring_enc[j];
			ring->use_doorbell = true;
			if (amdgpu_sriov_vf(adev)) {
				ring->doorbell_index = vcn_doorbell_index + i * (adev->vcn.num_enc_rings + 1) + 1 + j;
			} else {
				ring->doorbell_index = (adev->doorbell_index.vcn.vcn_ring0_1 << 1) + 2 + j + 8 * i;
			}
			sprintf(ring->name, "vcn_enc_%d.%d", i, j);
			r = amdgpu_ring_init(adev, ring, 512, &adev->vcn.inst[i].irq, 0,
					     AMDGPU_RING_PRIO_DEFAULT,
					     &adev->vcn.inst[i].sched_score);
			if (r)
				return r;
		}

		fw_shared = adev->vcn.inst[i].fw_shared_cpu_addr;
		fw_shared->present_flag_0 |= cpu_to_le32(AMDGPU_VCN_SW_RING_FLAG) |
					     cpu_to_le32(AMDGPU_VCN_MULTI_QUEUE_FLAG) |
					     cpu_to_le32(AMDGPU_VCN_FW_SHARED_FLAG_0_RB);
		fw_shared->sw_ring.is_enabled = cpu_to_le32(DEC_SW_RING_ENABLED);
	}

	if (amdgpu_sriov_vf(adev)) {
		r = amdgpu_virt_alloc_mm_table(adev);
		if (r)
			return r;
	}
	if (adev->pg_flags & AMD_PG_SUPPORT_VCN_DPG)
		adev->vcn.pause_dpg_mode = vcn_v3_0_pause_dpg_mode;

	return 0;
}

/**
 * vcn_v3_0_sw_fini - sw fini for VCN block
 *
 * @handle: amdgpu_device pointer
 *
 * VCN suspend and free up sw allocation
 */
static int vcn_v3_0_sw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int i, r;

	for (i = 0; i < adev->vcn.num_vcn_inst; i++) {
		volatile struct amdgpu_fw_shared *fw_shared;

		if (adev->vcn.harvest_config & (1 << i))
			continue;
		fw_shared = adev->vcn.inst[i].fw_shared_cpu_addr;
		fw_shared->present_flag_0 = 0;
		fw_shared->sw_ring.is_enabled = false;
	}

	if (amdgpu_sriov_vf(adev))
		amdgpu_virt_free_mm_table(adev);

	r = amdgpu_vcn_suspend(adev);
	if (r)
		return r;

	r = amdgpu_vcn_sw_fini(adev);

	return r;
}

/**
 * vcn_v3_0_hw_init - start and test VCN block
 *
 * @handle: amdgpu_device pointer
 *
 * Initialize the hardware, boot up the VCPU and do some testing
 */
static int vcn_v3_0_hw_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct amdgpu_ring *ring;
	int i, j, r;

	if (amdgpu_sriov_vf(adev)) {
		r = vcn_v3_0_start_sriov(adev);
		if (r)
			goto done;

		/* initialize VCN dec and enc ring buffers */
		for (i = 0; i < adev->vcn.num_vcn_inst; ++i) {
			if (adev->vcn.harvest_config & (1 << i))
				continue;

			ring = &adev->vcn.inst[i].ring_dec;
			if (ring->sched.ready) {
				ring->wptr = 0;
				ring->wptr_old = 0;
				vcn_v3_0_dec_ring_set_wptr(ring);
			}

			for (j = 0; j < adev->vcn.num_enc_rings; ++j) {
				ring = &adev->vcn.inst[i].ring_enc[j];
				if (ring->sched.ready) {
					ring->wptr = 0;
					ring->wptr_old = 0;
					vcn_v3_0_enc_ring_set_wptr(ring);
				}
			}
		}
	} else {
		for (i = 0; i < adev->vcn.num_vcn_inst; ++i) {
			if (adev->vcn.harvest_config & (1 << i))
				continue;

			ring = &adev->vcn.inst[i].ring_dec;

			adev->nbio.funcs->vcn_doorbell_range(adev, ring->use_doorbell,
						     ring->doorbell_index, i);

			r = amdgpu_ring_test_helper(ring);
			if (r)
				goto done;

			for (j = 0; j < adev->vcn.num_enc_rings; ++j) {
				ring = &adev->vcn.inst[i].ring_enc[j];
				r = amdgpu_ring_test_helper(ring);
				if (r)
					goto done;
			}
		}
	}

done:
	if (!r)
		DRM_INFO("VCN decode and encode initialized successfully(under %s).\n",
			(adev->pg_flags & AMD_PG_SUPPORT_VCN_DPG)?"DPG Mode":"SPG Mode");

	return r;
}

/**
 * vcn_v3_0_hw_fini - stop the hardware block
 *
 * @handle: amdgpu_device pointer
 *
 * Stop the VCN block, mark ring as not ready any more
 */
static int vcn_v3_0_hw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int i;

	cancel_delayed_work_sync(&adev->vcn.idle_work);

	for (i = 0; i < adev->vcn.num_vcn_inst; ++i) {
		if (adev->vcn.harvest_config & (1 << i))
			continue;

		if (!amdgpu_sriov_vf(adev)) {
			if ((adev->pg_flags & AMD_PG_SUPPORT_VCN_DPG) ||
					(adev->vcn.cur_state != AMD_PG_STATE_GATE &&
					 RREG32_SOC15(VCN, i, mmUVD_STATUS))) {
				vcn_v3_0_set_powergating_state(adev, AMD_PG_STATE_GATE);
			}
		}
	}

	return 0;
}

/**
 * vcn_v3_0_suspend - suspend VCN block
 *
 * @handle: amdgpu_device pointer
 *
 * HW fini and suspend VCN block
 */
static int vcn_v3_0_suspend(void *handle)
{
	int r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	r = vcn_v3_0_hw_fini(adev);
	if (r)
		return r;

	r = amdgpu_vcn_suspend(adev);

	return r;
}

/**
 * vcn_v3_0_resume - resume VCN block
 *
 * @handle: amdgpu_device pointer
 *
 * Resume firmware and hw init VCN block
 */
static int vcn_v3_0_resume(void *handle)
{
	int r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	r = amdgpu_vcn_resume(adev);
	if (r)
		return r;

	r = vcn_v3_0_hw_init(adev);

	return r;
}

/**
 * vcn_v3_0_mc_resume - memory controller programming
 *
 * @adev: amdgpu_device pointer
 * @inst: instance number
 *
 * Let the VCN memory controller know it's offsets
 */
static void vcn_v3_0_mc_resume(struct amdgpu_device *adev, int inst)
{
	uint32_t size = AMDGPU_GPU_PAGE_ALIGN(adev->vcn.fw->size + 4);
	uint32_t offset;

	/* cache window 0: fw */
	if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) {
		WREG32_SOC15(VCN, inst, mmUVD_LMI_VCPU_CACHE_64BIT_BAR_LOW,
			(adev->firmware.ucode[AMDGPU_UCODE_ID_VCN + inst].tmr_mc_addr_lo));
		WREG32_SOC15(VCN, inst, mmUVD_LMI_VCPU_CACHE_64BIT_BAR_HIGH,
			(adev->firmware.ucode[AMDGPU_UCODE_ID_VCN + inst].tmr_mc_addr_hi));
		WREG32_SOC15(VCN, inst, mmUVD_VCPU_CACHE_OFFSET0, 0);
		offset = 0;
	} else {
		WREG32_SOC15(VCN, inst, mmUVD_LMI_VCPU_CACHE_64BIT_BAR_LOW,
			lower_32_bits(adev->vcn.inst[inst].gpu_addr));
		WREG32_SOC15(VCN, inst, mmUVD_LMI_VCPU_CACHE_64BIT_BAR_HIGH,
			upper_32_bits(adev->vcn.inst[inst].gpu_addr));
		offset = size;
		WREG32_SOC15(VCN, inst, mmUVD_VCPU_CACHE_OFFSET0,
			AMDGPU_UVD_FIRMWARE_OFFSET >> 3);
	}
	WREG32_SOC15(VCN, inst, mmUVD_VCPU_CACHE_SIZE0, size);

	/* cache window 1: stack */
	WREG32_SOC15(VCN, inst, mmUVD_LMI_VCPU_CACHE1_64BIT_BAR_LOW,
		lower_32_bits(adev->vcn.inst[inst].gpu_addr + offset));
	WREG32_SOC15(VCN, inst, mmUVD_LMI_VCPU_CACHE1_64BIT_BAR_HIGH,
		upper_32_bits(adev->vcn.inst[inst].gpu_addr + offset));
	WREG32_SOC15(VCN, inst, mmUVD_VCPU_CACHE_OFFSET1, 0);
	WREG32_SOC15(VCN, inst, mmUVD_VCPU_CACHE_SIZE1, AMDGPU_VCN_STACK_SIZE);

	/* cache window 2: context */
	WREG32_SOC15(VCN, inst, mmUVD_LMI_VCPU_CACHE2_64BIT_BAR_LOW,
		lower_32_bits(adev->vcn.inst[inst].gpu_addr + offset + AMDGPU_VCN_STACK_SIZE));
	WREG32_SOC15(VCN, inst, mmUVD_LMI_VCPU_CACHE2_64BIT_BAR_HIGH,
		upper_32_bits(adev->vcn.inst[inst].gpu_addr + offset + AMDGPU_VCN_STACK_SIZE));
	WREG32_SOC15(VCN, inst, mmUVD_VCPU_CACHE_OFFSET2, 0);
	WREG32_SOC15(VCN, inst, mmUVD_VCPU_CACHE_SIZE2, AMDGPU_VCN_CONTEXT_SIZE);

	/* non-cache window */
	WREG32_SOC15(VCN, inst, mmUVD_LMI_VCPU_NC0_64BIT_BAR_LOW,
		lower_32_bits(adev->vcn.inst[inst].fw_shared_gpu_addr));
	WREG32_SOC15(VCN, inst, mmUVD_LMI_VCPU_NC0_64BIT_BAR_HIGH,
		upper_32_bits(adev->vcn.inst[inst].fw_shared_gpu_addr));
	WREG32_SOC15(VCN, inst, mmUVD_VCPU_NONCACHE_OFFSET0, 0);
	WREG32_SOC15(VCN, inst, mmUVD_VCPU_NONCACHE_SIZE0,
		AMDGPU_GPU_PAGE_ALIGN(sizeof(struct amdgpu_fw_shared)));
}

static void vcn_v3_0_mc_resume_dpg_mode(struct amdgpu_device *adev, int inst_idx, bool indirect)
{
	uint32_t size = AMDGPU_GPU_PAGE_ALIGN(adev->vcn.fw->size + 4);
	uint32_t offset;

	/* cache window 0: fw */
	if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) {
		if (!indirect) {
			WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
				VCN, inst_idx, mmUVD_LMI_VCPU_CACHE_64BIT_BAR_LOW),
				(adev->firmware.ucode[AMDGPU_UCODE_ID_VCN + inst_idx].tmr_mc_addr_lo), 0, indirect);
			WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
				VCN, inst_idx, mmUVD_LMI_VCPU_CACHE_64BIT_BAR_HIGH),
				(adev->firmware.ucode[AMDGPU_UCODE_ID_VCN + inst_idx].tmr_mc_addr_hi), 0, indirect);
			WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
				VCN, inst_idx, mmUVD_VCPU_CACHE_OFFSET0), 0, 0, indirect);
		} else {
			WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
				VCN, inst_idx, mmUVD_LMI_VCPU_CACHE_64BIT_BAR_LOW), 0, 0, indirect);
			WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
				VCN, inst_idx, mmUVD_LMI_VCPU_CACHE_64BIT_BAR_HIGH), 0, 0, indirect);
			WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
				VCN, inst_idx, mmUVD_VCPU_CACHE_OFFSET0), 0, 0, indirect);
		}
		offset = 0;
	} else {
		WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
			VCN, inst_idx, mmUVD_LMI_VCPU_CACHE_64BIT_BAR_LOW),
			lower_32_bits(adev->vcn.inst[inst_idx].gpu_addr), 0, indirect);
		WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
			VCN, inst_idx, mmUVD_LMI_VCPU_CACHE_64BIT_BAR_HIGH),
			upper_32_bits(adev->vcn.inst[inst_idx].gpu_addr), 0, indirect);
		offset = size;
		WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
			VCN, inst_idx, mmUVD_VCPU_CACHE_OFFSET0),
			AMDGPU_UVD_FIRMWARE_OFFSET >> 3, 0, indirect);
	}

	if (!indirect)
		WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
			VCN, inst_idx, mmUVD_VCPU_CACHE_SIZE0), size, 0, indirect);
	else
		WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
			VCN, inst_idx, mmUVD_VCPU_CACHE_SIZE0), 0, 0, indirect);

	/* cache window 1: stack */
	if (!indirect) {
		WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
			VCN, inst_idx, mmUVD_LMI_VCPU_CACHE1_64BIT_BAR_LOW),
			lower_32_bits(adev->vcn.inst[inst_idx].gpu_addr + offset), 0, indirect);
		WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
			VCN, inst_idx, mmUVD_LMI_VCPU_CACHE1_64BIT_BAR_HIGH),
			upper_32_bits(adev->vcn.inst[inst_idx].gpu_addr + offset), 0, indirect);
		WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
			VCN, inst_idx, mmUVD_VCPU_CACHE_OFFSET1), 0, 0, indirect);
	} else {
		WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
			VCN, inst_idx, mmUVD_LMI_VCPU_CACHE1_64BIT_BAR_LOW), 0, 0, indirect);
		WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
			VCN, inst_idx, mmUVD_LMI_VCPU_CACHE1_64BIT_BAR_HIGH), 0, 0, indirect);
		WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
			VCN, inst_idx, mmUVD_VCPU_CACHE_OFFSET1), 0, 0, indirect);
	}
	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
			VCN, inst_idx, mmUVD_VCPU_CACHE_SIZE1), AMDGPU_VCN_STACK_SIZE, 0, indirect);

	/* cache window 2: context */
	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
			VCN, inst_idx, mmUVD_LMI_VCPU_CACHE2_64BIT_BAR_LOW),
			lower_32_bits(adev->vcn.inst[inst_idx].gpu_addr + offset + AMDGPU_VCN_STACK_SIZE), 0, indirect);
	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
			VCN, inst_idx, mmUVD_LMI_VCPU_CACHE2_64BIT_BAR_HIGH),
			upper_32_bits(adev->vcn.inst[inst_idx].gpu_addr + offset + AMDGPU_VCN_STACK_SIZE), 0, indirect);
	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
			VCN, inst_idx, mmUVD_VCPU_CACHE_OFFSET2), 0, 0, indirect);
	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
			VCN, inst_idx, mmUVD_VCPU_CACHE_SIZE2), AMDGPU_VCN_CONTEXT_SIZE, 0, indirect);

	/* non-cache window */
	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
			VCN, inst_idx, mmUVD_LMI_VCPU_NC0_64BIT_BAR_LOW),
			lower_32_bits(adev->vcn.inst[inst_idx].fw_shared_gpu_addr), 0, indirect);
	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
			VCN, inst_idx, mmUVD_LMI_VCPU_NC0_64BIT_BAR_HIGH),
			upper_32_bits(adev->vcn.inst[inst_idx].fw_shared_gpu_addr), 0, indirect);
	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
			VCN, inst_idx, mmUVD_VCPU_NONCACHE_OFFSET0), 0, 0, indirect);
	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
			VCN, inst_idx, mmUVD_VCPU_NONCACHE_SIZE0),
			AMDGPU_GPU_PAGE_ALIGN(sizeof(struct amdgpu_fw_shared)), 0, indirect);

	/* VCN global tiling registers */
	WREG32_SOC15_DPG_MODE(0, SOC15_DPG_MODE_OFFSET(
		UVD, 0, mmUVD_GFX10_ADDR_CONFIG), adev->gfx.config.gb_addr_config, 0, indirect);
}

static void vcn_v3_0_disable_static_power_gating(struct amdgpu_device *adev, int inst)
{
	uint32_t data = 0;

	if (adev->pg_flags & AMD_PG_SUPPORT_VCN) {
		data = (1 << UVD_PGFSM_CONFIG__UVDM_PWR_CONFIG__SHIFT
			| 1 << UVD_PGFSM_CONFIG__UVDU_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDF_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDC_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDB_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDIRL_PWR_CONFIG__SHIFT
			| 1 << UVD_PGFSM_CONFIG__UVDLM_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDTD_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDTE_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDE_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDAB_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDATD_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDNA_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDNB_PWR_CONFIG__SHIFT);

		WREG32_SOC15(VCN, inst, mmUVD_PGFSM_CONFIG, data);
		SOC15_WAIT_ON_RREG(VCN, inst, mmUVD_PGFSM_STATUS,
			UVD_PGFSM_STATUS__UVDM_UVDU_UVDLM_PWR_ON_3_0, 0x3F3FFFFF);
	} else {
		data = (1 << UVD_PGFSM_CONFIG__UVDM_PWR_CONFIG__SHIFT
			| 1 << UVD_PGFSM_CONFIG__UVDU_PWR_CONFIG__SHIFT
			| 1 << UVD_PGFSM_CONFIG__UVDF_PWR_CONFIG__SHIFT
			| 1 << UVD_PGFSM_CONFIG__UVDC_PWR_CONFIG__SHIFT
			| 1 << UVD_PGFSM_CONFIG__UVDB_PWR_CONFIG__SHIFT
			| 1 << UVD_PGFSM_CONFIG__UVDIRL_PWR_CONFIG__SHIFT
			| 1 << UVD_PGFSM_CONFIG__UVDLM_PWR_CONFIG__SHIFT
			| 1 << UVD_PGFSM_CONFIG__UVDTD_PWR_CONFIG__SHIFT
			| 1 << UVD_PGFSM_CONFIG__UVDTE_PWR_CONFIG__SHIFT
			| 1 << UVD_PGFSM_CONFIG__UVDE_PWR_CONFIG__SHIFT
			| 1 << UVD_PGFSM_CONFIG__UVDAB_PWR_CONFIG__SHIFT
			| 1 << UVD_PGFSM_CONFIG__UVDATD_PWR_CONFIG__SHIFT
			| 1 << UVD_PGFSM_CONFIG__UVDNA_PWR_CONFIG__SHIFT
			| 1 << UVD_PGFSM_CONFIG__UVDNB_PWR_CONFIG__SHIFT);
		WREG32_SOC15(VCN, inst, mmUVD_PGFSM_CONFIG, data);
		SOC15_WAIT_ON_RREG(VCN, inst, mmUVD_PGFSM_STATUS, 0,  0x3F3FFFFF);
	}

	data = RREG32_SOC15(VCN, inst, mmUVD_POWER_STATUS);
	data &= ~0x103;
	if (adev->pg_flags & AMD_PG_SUPPORT_VCN)
		data |= UVD_PGFSM_CONFIG__UVDM_UVDU_PWR_ON |
			UVD_POWER_STATUS__UVD_PG_EN_MASK;

	WREG32_SOC15(VCN, inst, mmUVD_POWER_STATUS, data);
}

static void vcn_v3_0_enable_static_power_gating(struct amdgpu_device *adev, int inst)
{
	uint32_t data;

	if (adev->pg_flags & AMD_PG_SUPPORT_VCN) {
		/* Before power off, this indicator has to be turned on */
		data = RREG32_SOC15(VCN, inst, mmUVD_POWER_STATUS);
		data &= ~UVD_POWER_STATUS__UVD_POWER_STATUS_MASK;
		data |= UVD_POWER_STATUS__UVD_POWER_STATUS_TILES_OFF;
		WREG32_SOC15(VCN, inst, mmUVD_POWER_STATUS, data);

		data = (2 << UVD_PGFSM_CONFIG__UVDM_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDU_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDF_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDC_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDB_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDIRL_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDLM_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDTD_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDTE_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDE_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDAB_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDATD_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDNA_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDNB_PWR_CONFIG__SHIFT);
		WREG32_SOC15(VCN, inst, mmUVD_PGFSM_CONFIG, data);

		data = (2 << UVD_PGFSM_STATUS__UVDM_PWR_STATUS__SHIFT
			| 2 << UVD_PGFSM_STATUS__UVDU_PWR_STATUS__SHIFT
			| 2 << UVD_PGFSM_STATUS__UVDF_PWR_STATUS__SHIFT
			| 2 << UVD_PGFSM_STATUS__UVDC_PWR_STATUS__SHIFT
			| 2 << UVD_PGFSM_STATUS__UVDB_PWR_STATUS__SHIFT
			| 2 << UVD_PGFSM_STATUS__UVDIRL_PWR_STATUS__SHIFT
			| 2 << UVD_PGFSM_STATUS__UVDLM_PWR_STATUS__SHIFT
			| 2 << UVD_PGFSM_STATUS__UVDTD_PWR_STATUS__SHIFT
			| 2 << UVD_PGFSM_STATUS__UVDTE_PWR_STATUS__SHIFT
			| 2 << UVD_PGFSM_STATUS__UVDE_PWR_STATUS__SHIFT
			| 2 << UVD_PGFSM_STATUS__UVDAB_PWR_STATUS__SHIFT
			| 2 << UVD_PGFSM_STATUS__UVDATD_PWR_STATUS__SHIFT
			| 2 << UVD_PGFSM_STATUS__UVDNA_PWR_STATUS__SHIFT
			| 2 << UVD_PGFSM_STATUS__UVDNB_PWR_STATUS__SHIFT);
		SOC15_WAIT_ON_RREG(VCN, inst, mmUVD_PGFSM_STATUS, data, 0x3F3FFFFF);
	}
}

/**
 * vcn_v3_0_disable_clock_gating - disable VCN clock gating
 *
 * @adev: amdgpu_device pointer
 * @inst: instance number
 *
 * Disable clock gating for VCN block
 */
static void vcn_v3_0_disable_clock_gating(struct amdgpu_device *adev, int inst)
{
	uint32_t data;

	/* VCN disable CGC */
	data = RREG32_SOC15(VCN, inst, mmUVD_CGC_CTRL);
	if (adev->cg_flags & AMD_CG_SUPPORT_VCN_MGCG)
		data |= 1 << UVD_CGC_CTRL__DYN_CLOCK_MODE__SHIFT;
	else
		data &= ~UVD_CGC_CTRL__DYN_CLOCK_MODE_MASK;
	data |= 1 << UVD_CGC_CTRL__CLK_GATE_DLY_TIMER__SHIFT;
	data |= 4 << UVD_CGC_CTRL__CLK_OFF_DELAY__SHIFT;
	WREG32_SOC15(VCN, inst, mmUVD_CGC_CTRL, data);

	data = RREG32_SOC15(VCN, inst, mmUVD_CGC_GATE);
	data &= ~(UVD_CGC_GATE__SYS_MASK
		| UVD_CGC_GATE__UDEC_MASK
		| UVD_CGC_GATE__MPEG2_MASK
		| UVD_CGC_GATE__REGS_MASK
		| UVD_CGC_GATE__RBC_MASK
		| UVD_CGC_GATE__LMI_MC_MASK
		| UVD_CGC_GATE__LMI_UMC_MASK
		| UVD_CGC_GATE__IDCT_MASK
		| UVD_CGC_GATE__MPRD_MASK
		| UVD_CGC_GATE__MPC_MASK
		| UVD_CGC_GATE__LBSI_MASK
		| UVD_CGC_GATE__LRBBM_MASK
		| UVD_CGC_GATE__UDEC_RE_MASK
		| UVD_CGC_GATE__UDEC_CM_MASK
		| UVD_CGC_GATE__UDEC_IT_MASK
		| UVD_CGC_GATE__UDEC_DB_MASK
		| UVD_CGC_GATE__UDEC_MP_MASK
		| UVD_CGC_GATE__WCB_MASK
		| UVD_CGC_GATE__VCPU_MASK
		| UVD_CGC_GATE__MMSCH_MASK);

	WREG32_SOC15(VCN, inst, mmUVD_CGC_GATE, data);

	SOC15_WAIT_ON_RREG(VCN, inst, mmUVD_CGC_GATE, 0,  0xFFFFFFFF);

	data = RREG32_SOC15(VCN, inst, mmUVD_CGC_CTRL);
	data &= ~(UVD_CGC_CTRL__UDEC_RE_MODE_MASK
		| UVD_CGC_CTRL__UDEC_CM_MODE_MASK
		| UVD_CGC_CTRL__UDEC_IT_MODE_MASK
		| UVD_CGC_CTRL__UDEC_DB_MODE_MASK
		| UVD_CGC_CTRL__UDEC_MP_MODE_MASK
		| UVD_CGC_CTRL__SYS_MODE_MASK
		| UVD_CGC_CTRL__UDEC_MODE_MASK
		| UVD_CGC_CTRL__MPEG2_MODE_MASK
		| UVD_CGC_CTRL__REGS_MODE_MASK
		| UVD_CGC_CTRL__RBC_MODE_MASK
		| UVD_CGC_CTRL__LMI_MC_MODE_MASK
		| UVD_CGC_CTRL__LMI_UMC_MODE_MASK
		| UVD_CGC_CTRL__IDCT_MODE_MASK
		| UVD_CGC_CTRL__MPRD_MODE_MASK
		| UVD_CGC_CTRL__MPC_MODE_MASK
		| UVD_CGC_CTRL__LBSI_MODE_MASK
		| UVD_CGC_CTRL__LRBBM_MODE_MASK
		| UVD_CGC_CTRL__WCB_MODE_MASK
		| UVD_CGC_CTRL__VCPU_MODE_MASK
		| UVD_CGC_CTRL__MMSCH_MODE_MASK);
	WREG32_SOC15(VCN, inst, mmUVD_CGC_CTRL, data);

	data = RREG32_SOC15(VCN, inst, mmUVD_SUVD_CGC_GATE);
	data |= (UVD_SUVD_CGC_GATE__SRE_MASK
		| UVD_SUVD_CGC_GATE__SIT_MASK
		| UVD_SUVD_CGC_GATE__SMP_MASK
		| UVD_SUVD_CGC_GATE__SCM_MASK
		| UVD_SUVD_CGC_GATE__SDB_MASK
		| UVD_SUVD_CGC_GATE__SRE_H264_MASK
		| UVD_SUVD_CGC_GATE__SRE_HEVC_MASK
		| UVD_SUVD_CGC_GATE__SIT_H264_MASK
		| UVD_SUVD_CGC_GATE__SIT_HEVC_MASK
		| UVD_SUVD_CGC_GATE__SCM_H264_MASK
		| UVD_SUVD_CGC_GATE__SCM_HEVC_MASK
		| UVD_SUVD_CGC_GATE__SDB_H264_MASK
		| UVD_SUVD_CGC_GATE__SDB_HEVC_MASK
		| UVD_SUVD_CGC_GATE__SCLR_MASK
		| UVD_SUVD_CGC_GATE__ENT_MASK
		| UVD_SUVD_CGC_GATE__IME_MASK
		| UVD_SUVD_CGC_GATE__SIT_HEVC_DEC_MASK
		| UVD_SUVD_CGC_GATE__SIT_HEVC_ENC_MASK
		| UVD_SUVD_CGC_GATE__SITE_MASK
		| UVD_SUVD_CGC_GATE__SRE_VP9_MASK
		| UVD_SUVD_CGC_GATE__SCM_VP9_MASK
		| UVD_SUVD_CGC_GATE__SIT_VP9_DEC_MASK
		| UVD_SUVD_CGC_GATE__SDB_VP9_MASK
		| UVD_SUVD_CGC_GATE__IME_HEVC_MASK
		| UVD_SUVD_CGC_GATE__EFC_MASK
		| UVD_SUVD_CGC_GATE__SAOE_MASK
		| UVD_SUVD_CGC_GATE__SRE_AV1_MASK
		| UVD_SUVD_CGC_GATE__FBC_PCLK_MASK
		| UVD_SUVD_CGC_GATE__FBC_CCLK_MASK
		| UVD_SUVD_CGC_GATE__SCM_AV1_MASK
		| UVD_SUVD_CGC_GATE__SMPA_MASK);
	WREG32_SOC15(VCN, inst, mmUVD_SUVD_CGC_GATE, data);

	data = RREG32_SOC15(VCN, inst, mmUVD_SUVD_CGC_GATE2);
	data |= (UVD_SUVD_CGC_GATE2__MPBE0_MASK
		| UVD_SUVD_CGC_GATE2__MPBE1_MASK
		| UVD_SUVD_CGC_GATE2__SIT_AV1_MASK
		| UVD_SUVD_CGC_GATE2__SDB_AV1_MASK
		| UVD_SUVD_CGC_GATE2__MPC1_MASK);
	WREG32_SOC15(VCN, inst, mmUVD_SUVD_CGC_GATE2, data);

	data = RREG32_SOC15(VCN, inst, mmUVD_SUVD_CGC_CTRL);
	data &= ~(UVD_SUVD_CGC_CTRL__SRE_MODE_MASK
		| UVD_SUVD_CGC_CTRL__SIT_MODE_MASK
		| UVD_SUVD_CGC_CTRL__SMP_MODE_MASK
		| UVD_SUVD_CGC_CTRL__SCM_MODE_MASK
		| UVD_SUVD_CGC_CTRL__SDB_MODE_MASK
		| UVD_SUVD_CGC_CTRL__SCLR_MODE_MASK
		| UVD_SUVD_CGC_CTRL__ENT_MODE_MASK
		| UVD_SUVD_CGC_CTRL__IME_MODE_MASK
		| UVD_SUVD_CGC_CTRL__SITE_MODE_MASK
		| UVD_SUVD_CGC_CTRL__EFC_MODE_MASK
		| UVD_SUVD_CGC_CTRL__SAOE_MODE_MASK
		| UVD_SUVD_CGC_CTRL__SMPA_MODE_MASK
		| UVD_SUVD_CGC_CTRL__MPBE0_MODE_MASK
		| UVD_SUVD_CGC_CTRL__MPBE1_MODE_MASK
		| UVD_SUVD_CGC_CTRL__SIT_AV1_MODE_MASK
		| UVD_SUVD_CGC_CTRL__SDB_AV1_MODE_MASK
		| UVD_SUVD_CGC_CTRL__MPC1_MODE_MASK
		| UVD_SUVD_CGC_CTRL__FBC_PCLK_MASK
		| UVD_SUVD_CGC_CTRL__FBC_CCLK_MASK);
	WREG32_SOC15(VCN, inst, mmUVD_SUVD_CGC_CTRL, data);
}

static void vcn_v3_0_clock_gating_dpg_mode(struct amdgpu_device *adev,
		uint8_t sram_sel, int inst_idx, uint8_t indirect)
{
	uint32_t reg_data = 0;

	/* enable sw clock gating control */
	if (adev->cg_flags & AMD_CG_SUPPORT_VCN_MGCG)
		reg_data = 1 << UVD_CGC_CTRL__DYN_CLOCK_MODE__SHIFT;
	else
		reg_data = 0 << UVD_CGC_CTRL__DYN_CLOCK_MODE__SHIFT;
	reg_data |= 1 << UVD_CGC_CTRL__CLK_GATE_DLY_TIMER__SHIFT;
	reg_data |= 4 << UVD_CGC_CTRL__CLK_OFF_DELAY__SHIFT;
	reg_data &= ~(UVD_CGC_CTRL__UDEC_RE_MODE_MASK |
		 UVD_CGC_CTRL__UDEC_CM_MODE_MASK |
		 UVD_CGC_CTRL__UDEC_IT_MODE_MASK |
		 UVD_CGC_CTRL__UDEC_DB_MODE_MASK |
		 UVD_CGC_CTRL__UDEC_MP_MODE_MASK |
		 UVD_CGC_CTRL__SYS_MODE_MASK |
		 UVD_CGC_CTRL__UDEC_MODE_MASK |
		 UVD_CGC_CTRL__MPEG2_MODE_MASK |
		 UVD_CGC_CTRL__REGS_MODE_MASK |
		 UVD_CGC_CTRL__RBC_MODE_MASK |
		 UVD_CGC_CTRL__LMI_MC_MODE_MASK |
		 UVD_CGC_CTRL__LMI_UMC_MODE_MASK |
		 UVD_CGC_CTRL__IDCT_MODE_MASK |
		 UVD_CGC_CTRL__MPRD_MODE_MASK |
		 UVD_CGC_CTRL__MPC_MODE_MASK |
		 UVD_CGC_CTRL__LBSI_MODE_MASK |
		 UVD_CGC_CTRL__LRBBM_MODE_MASK |
		 UVD_CGC_CTRL__WCB_MODE_MASK |
		 UVD_CGC_CTRL__VCPU_MODE_MASK |
		 UVD_CGC_CTRL__MMSCH_MODE_MASK);
	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
		VCN, inst_idx, mmUVD_CGC_CTRL), reg_data, sram_sel, indirect);

	/* turn off clock gating */
	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
		VCN, inst_idx, mmUVD_CGC_GATE), 0, sram_sel, indirect);

	/* turn on SUVD clock gating */
	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
		VCN, inst_idx, mmUVD_SUVD_CGC_GATE), 1, sram_sel, indirect);

	/* turn on sw mode in UVD_SUVD_CGC_CTRL */
	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
		VCN, inst_idx, mmUVD_SUVD_CGC_CTRL), 0, sram_sel, indirect);
}

/**
 * vcn_v3_0_enable_clock_gating - enable VCN clock gating
 *
 * @adev: amdgpu_device pointer
 * @inst: instance number
 *
 * Enable clock gating for VCN block
 */
static void vcn_v3_0_enable_clock_gating(struct amdgpu_device *adev, int inst)
{
	uint32_t data;

	/* enable VCN CGC */
	data = RREG32_SOC15(VCN, inst, mmUVD_CGC_CTRL);
	if (adev->cg_flags & AMD_CG_SUPPORT_VCN_MGCG)
		data |= 1 << UVD_CGC_CTRL__DYN_CLOCK_MODE__SHIFT;
	else
		data |= 0 << UVD_CGC_CTRL__DYN_CLOCK_MODE__SHIFT;
	data |= 1 << UVD_CGC_CTRL__CLK_GATE_DLY_TIMER__SHIFT;
	data |= 4 << UVD_CGC_CTRL__CLK_OFF_DELAY__SHIFT;
	WREG32_SOC15(VCN, inst, mmUVD_CGC_CTRL, data);

	data = RREG32_SOC15(VCN, inst, mmUVD_CGC_CTRL);
	data |= (UVD_CGC_CTRL__UDEC_RE_MODE_MASK
		| UVD_CGC_CTRL__UDEC_CM_MODE_MASK
		| UVD_CGC_CTRL__UDEC_IT_MODE_MASK
		| UVD_CGC_CTRL__UDEC_DB_MODE_MASK
		| UVD_CGC_CTRL__UDEC_MP_MODE_MASK
		| UVD_CGC_CTRL__SYS_MODE_MASK
		| UVD_CGC_CTRL__UDEC_MODE_MASK
		| UVD_CGC_CTRL__MPEG2_MODE_MASK
		| UVD_CGC_CTRL__REGS_MODE_MASK
		| UVD_CGC_CTRL__RBC_MODE_MASK
		| UVD_CGC_CTRL__LMI_MC_MODE_MASK
		| UVD_CGC_CTRL__LMI_UMC_MODE_MASK
		| UVD_CGC_CTRL__IDCT_MODE_MASK
		| UVD_CGC_CTRL__MPRD_MODE_MASK
		| UVD_CGC_CTRL__MPC_MODE_MASK
		| UVD_CGC_CTRL__LBSI_MODE_MASK
		| UVD_CGC_CTRL__LRBBM_MODE_MASK
		| UVD_CGC_CTRL__WCB_MODE_MASK
		| UVD_CGC_CTRL__VCPU_MODE_MASK
		| UVD_CGC_CTRL__MMSCH_MODE_MASK);
	WREG32_SOC15(VCN, inst, mmUVD_CGC_CTRL, data);

	data = RREG32_SOC15(VCN, inst, mmUVD_SUVD_CGC_CTRL);
	data |= (UVD_SUVD_CGC_CTRL__SRE_MODE_MASK
		| UVD_SUVD_CGC_CTRL__SIT_MODE_MASK
		| UVD_SUVD_CGC_CTRL__SMP_MODE_MASK
		| UVD_SUVD_CGC_CTRL__SCM_MODE_MASK
		| UVD_SUVD_CGC_CTRL__SDB_MODE_MASK
		| UVD_SUVD_CGC_CTRL__SCLR_MODE_MASK
		| UVD_SUVD_CGC_CTRL__ENT_MODE_MASK
		| UVD_SUVD_CGC_CTRL__IME_MODE_MASK
		| UVD_SUVD_CGC_CTRL__SITE_MODE_MASK
		| UVD_SUVD_CGC_CTRL__EFC_MODE_MASK
		| UVD_SUVD_CGC_CTRL__SAOE_MODE_MASK
		| UVD_SUVD_CGC_CTRL__SMPA_MODE_MASK
		| UVD_SUVD_CGC_CTRL__MPBE0_MODE_MASK
		| UVD_SUVD_CGC_CTRL__MPBE1_MODE_MASK
		| UVD_SUVD_CGC_CTRL__SIT_AV1_MODE_MASK
		| UVD_SUVD_CGC_CTRL__SDB_AV1_MODE_MASK
		| UVD_SUVD_CGC_CTRL__MPC1_MODE_MASK
		| UVD_SUVD_CGC_CTRL__FBC_PCLK_MASK
		| UVD_SUVD_CGC_CTRL__FBC_CCLK_MASK);
	WREG32_SOC15(VCN, inst, mmUVD_SUVD_CGC_CTRL, data);
}

static int vcn_v3_0_start_dpg_mode(struct amdgpu_device *adev, int inst_idx, bool indirect)
{
	volatile struct amdgpu_fw_shared *fw_shared = adev->vcn.inst[inst_idx].fw_shared_cpu_addr;
	struct amdgpu_ring *ring;
	uint32_t rb_bufsz, tmp;

	/* disable register anti-hang mechanism */
	WREG32_P(SOC15_REG_OFFSET(VCN, inst_idx, mmUVD_POWER_STATUS), 1,
		~UVD_POWER_STATUS__UVD_POWER_STATUS_MASK);
	/* enable dynamic power gating mode */
	tmp = RREG32_SOC15(VCN, inst_idx, mmUVD_POWER_STATUS);
	tmp |= UVD_POWER_STATUS__UVD_PG_MODE_MASK;
	tmp |= UVD_POWER_STATUS__UVD_PG_EN_MASK;
	WREG32_SOC15(VCN, inst_idx, mmUVD_POWER_STATUS, tmp);

	if (indirect)
		adev->vcn.inst[inst_idx].dpg_sram_curr_addr = (uint32_t *)adev->vcn.inst[inst_idx].dpg_sram_cpu_addr;

	/* enable clock gating */
	vcn_v3_0_clock_gating_dpg_mode(adev, 0, inst_idx, indirect);

	/* enable VCPU clock */
	tmp = (0xFF << UVD_VCPU_CNTL__PRB_TIMEOUT_VAL__SHIFT);
	tmp |= UVD_VCPU_CNTL__CLK_EN_MASK;
	tmp |= UVD_VCPU_CNTL__BLK_RST_MASK;
	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
		VCN, inst_idx, mmUVD_VCPU_CNTL), tmp, 0, indirect);

	/* disable master interupt */
	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
		VCN, inst_idx, mmUVD_MASTINT_EN), 0, 0, indirect);

	/* setup mmUVD_LMI_CTRL */
	tmp = (0x8 | UVD_LMI_CTRL__WRITE_CLEAN_TIMER_EN_MASK |
		UVD_LMI_CTRL__REQ_MODE_MASK |
		UVD_LMI_CTRL__CRC_RESET_MASK |
		UVD_LMI_CTRL__MASK_MC_URGENT_MASK |
		UVD_LMI_CTRL__DATA_COHERENCY_EN_MASK |
		UVD_LMI_CTRL__VCPU_DATA_COHERENCY_EN_MASK |
		(8 << UVD_LMI_CTRL__WRITE_CLEAN_TIMER__SHIFT) |
		0x00100000L);
	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
		VCN, inst_idx, mmUVD_LMI_CTRL), tmp, 0, indirect);

	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
		VCN, inst_idx, mmUVD_MPC_CNTL),
		0x2 << UVD_MPC_CNTL__REPLACEMENT_MODE__SHIFT, 0, indirect);

	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
		VCN, inst_idx, mmUVD_MPC_SET_MUXA0),
		((0x1 << UVD_MPC_SET_MUXA0__VARA_1__SHIFT) |
		 (0x2 << UVD_MPC_SET_MUXA0__VARA_2__SHIFT) |
		 (0x3 << UVD_MPC_SET_MUXA0__VARA_3__SHIFT) |
		 (0x4 << UVD_MPC_SET_MUXA0__VARA_4__SHIFT)), 0, indirect);

	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
		VCN, inst_idx, mmUVD_MPC_SET_MUXB0),
		 ((0x1 << UVD_MPC_SET_MUXB0__VARB_1__SHIFT) |
		 (0x2 << UVD_MPC_SET_MUXB0__VARB_2__SHIFT) |
		 (0x3 << UVD_MPC_SET_MUXB0__VARB_3__SHIFT) |
		 (0x4 << UVD_MPC_SET_MUXB0__VARB_4__SHIFT)), 0, indirect);

	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
		VCN, inst_idx, mmUVD_MPC_SET_MUX),
		((0x0 << UVD_MPC_SET_MUX__SET_0__SHIFT) |
		 (0x1 << UVD_MPC_SET_MUX__SET_1__SHIFT) |
		 (0x2 << UVD_MPC_SET_MUX__SET_2__SHIFT)), 0, indirect);

	vcn_v3_0_mc_resume_dpg_mode(adev, inst_idx, indirect);

	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
		VCN, inst_idx, mmUVD_REG_XX_MASK), 0x10, 0, indirect);
	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
		VCN, inst_idx, mmUVD_RBC_XX_IB_REG_CHECK), 0x3, 0, indirect);

	/* enable LMI MC and UMC channels */
	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
		VCN, inst_idx, mmUVD_LMI_CTRL2), 0, 0, indirect);

	/* unblock VCPU register access */
	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
		VCN, inst_idx, mmUVD_RB_ARB_CTRL), 0, 0, indirect);

	tmp = (0xFF << UVD_VCPU_CNTL__PRB_TIMEOUT_VAL__SHIFT);
	tmp |= UVD_VCPU_CNTL__CLK_EN_MASK;
	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
		VCN, inst_idx, mmUVD_VCPU_CNTL), tmp, 0, indirect);

	/* enable master interrupt */
	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
		VCN, inst_idx, mmUVD_MASTINT_EN),
		UVD_MASTINT_EN__VCPU_EN_MASK, 0, indirect);

	/* add nop to workaround PSP size check */
	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
		VCN, inst_idx, mmUVD_VCPU_CNTL), tmp, 0, indirect);

	if (indirect)
		psp_update_vcn_sram(adev, inst_idx, adev->vcn.inst[inst_idx].dpg_sram_gpu_addr,
			(uint32_t)((uintptr_t)adev->vcn.inst[inst_idx].dpg_sram_curr_addr -
				(uintptr_t)adev->vcn.inst[inst_idx].dpg_sram_cpu_addr));

	ring = &adev->vcn.inst[inst_idx].ring_dec;
	/* force RBC into idle state */
	rb_bufsz = order_base_2(ring->ring_size);
	tmp = REG_SET_FIELD(0, UVD_RBC_RB_CNTL, RB_BUFSZ, rb_bufsz);
	tmp = REG_SET_FIELD(tmp, UVD_RBC_RB_CNTL, RB_BLKSZ, 1);
	tmp = REG_SET_FIELD(tmp, UVD_RBC_RB_CNTL, RB_NO_FETCH, 1);
	tmp = REG_SET_FIELD(tmp, UVD_RBC_RB_CNTL, RB_NO_UPDATE, 1);
	tmp = REG_SET_FIELD(tmp, UVD_RBC_RB_CNTL, RB_RPTR_WR_EN, 1);
	WREG32_SOC15(VCN, inst_idx, mmUVD_RBC_RB_CNTL, tmp);

	/* Stall DPG before WPTR/RPTR reset */
	WREG32_P(SOC15_REG_OFFSET(VCN, inst_idx, mmUVD_POWER_STATUS),
		UVD_POWER_STATUS__STALL_DPG_POWER_UP_MASK,
		~UVD_POWER_STATUS__STALL_DPG_POWER_UP_MASK);
	fw_shared->multi_queue.decode_queue_mode |= cpu_to_le32(FW_QUEUE_RING_RESET);

	/* set the write pointer delay */
	WREG32_SOC15(VCN, inst_idx, mmUVD_RBC_RB_WPTR_CNTL, 0);

	/* set the wb address */
	WREG32_SOC15(VCN, inst_idx, mmUVD_RBC_RB_RPTR_ADDR,
		(upper_32_bits(ring->gpu_addr) >> 2));

	/* programm the RB_BASE for ring buffer */
	WREG32_SOC15(VCN, inst_idx, mmUVD_LMI_RBC_RB_64BIT_BAR_LOW,
		lower_32_bits(ring->gpu_addr));
	WREG32_SOC15(VCN, inst_idx, mmUVD_LMI_RBC_RB_64BIT_BAR_HIGH,
		upper_32_bits(ring->gpu_addr));

	/* Initialize the ring buffer's read and write pointers */
	WREG32_SOC15(VCN, inst_idx, mmUVD_RBC_RB_RPTR, 0);

	WREG32_SOC15(VCN, inst_idx, mmUVD_SCRATCH2, 0);

	ring->wptr = RREG32_SOC15(VCN, inst_idx, mmUVD_RBC_RB_RPTR);
	WREG32_SOC15(VCN, inst_idx, mmUVD_RBC_RB_WPTR,
		lower_32_bits(ring->wptr));

	/* Reset FW shared memory RBC WPTR/RPTR */
	fw_shared->rb.rptr = 0;
	fw_shared->rb.wptr = lower_32_bits(ring->wptr);

	/*resetting done, fw can check RB ring */
	fw_shared->multi_queue.decode_queue_mode &= cpu_to_le32(~FW_QUEUE_RING_RESET);

	/* Unstall DPG */
	WREG32_P(SOC15_REG_OFFSET(VCN, inst_idx, mmUVD_POWER_STATUS),
		0, ~UVD_POWER_STATUS__STALL_DPG_POWER_UP_MASK);

	return 0;
}

static int vcn_v3_0_start(struct amdgpu_device *adev)
{
	volatile struct amdgpu_fw_shared *fw_shared;
	struct amdgpu_ring *ring;
	uint32_t rb_bufsz, tmp;
	int i, j, k, r;

	if (adev->pm.dpm_enabled)
		amdgpu_dpm_enable_uvd(adev, true);

	for (i = 0; i < adev->vcn.num_vcn_inst; ++i) {
		if (adev->vcn.harvest_config & (1 << i))
			continue;

		if (adev->pg_flags & AMD_PG_SUPPORT_VCN_DPG){
			r = vcn_v3_0_start_dpg_mode(adev, i, adev->vcn.indirect_sram);
			continue;
		}

		/* disable VCN power gating */
		vcn_v3_0_disable_static_power_gating(adev, i);

		/* set VCN status busy */
		tmp = RREG32_SOC15(VCN, i, mmUVD_STATUS) | UVD_STATUS__UVD_BUSY;
		WREG32_SOC15(VCN, i, mmUVD_STATUS, tmp);

		/*SW clock gating */
		vcn_v3_0_disable_clock_gating(adev, i);

		/* enable VCPU clock */
		WREG32_P(SOC15_REG_OFFSET(VCN, i, mmUVD_VCPU_CNTL),
			UVD_VCPU_CNTL__CLK_EN_MASK, ~UVD_VCPU_CNTL__CLK_EN_MASK);

		/* disable master interrupt */
		WREG32_P(SOC15_REG_OFFSET(VCN, i, mmUVD_MASTINT_EN), 0,
			~UVD_MASTINT_EN__VCPU_EN_MASK);

		/* enable LMI MC and UMC channels */
		WREG32_P(SOC15_REG_OFFSET(VCN, i, mmUVD_LMI_CTRL2), 0,
			~UVD_LMI_CTRL2__STALL_ARB_UMC_MASK);

		tmp = RREG32_SOC15(VCN, i, mmUVD_SOFT_RESET);
		tmp &= ~UVD_SOFT_RESET__LMI_SOFT_RESET_MASK;
		tmp &= ~UVD_SOFT_RESET__LMI_UMC_SOFT_RESET_MASK;
		WREG32_SOC15(VCN, i, mmUVD_SOFT_RESET, tmp);

		/* setup mmUVD_LMI_CTRL */
		tmp = RREG32_SOC15(VCN, i, mmUVD_LMI_CTRL);
		WREG32_SOC15(VCN, i, mmUVD_LMI_CTRL, tmp |
			UVD_LMI_CTRL__WRITE_CLEAN_TIMER_EN_MASK	|
			UVD_LMI_CTRL__MASK_MC_URGENT_MASK |
			UVD_LMI_CTRL__DATA_COHERENCY_EN_MASK |
			UVD_LMI_CTRL__VCPU_DATA_COHERENCY_EN_MASK);

		/* setup mmUVD_MPC_CNTL */
		tmp = RREG32_SOC15(VCN, i, mmUVD_MPC_CNTL);
		tmp &= ~UVD_MPC_CNTL__REPLACEMENT_MODE_MASK;
		tmp |= 0x2 << UVD_MPC_CNTL__REPLACEMENT_MODE__SHIFT;
		WREG32_SOC15(VCN, i, mmUVD_MPC_CNTL, tmp);

		/* setup UVD_MPC_SET_MUXA0 */
		WREG32_SOC15(VCN, i, mmUVD_MPC_SET_MUXA0,
			((0x1 << UVD_MPC_SET_MUXA0__VARA_1__SHIFT) |
			(0x2 << UVD_MPC_SET_MUXA0__VARA_2__SHIFT) |
			(0x3 << UVD_MPC_SET_MUXA0__VARA_3__SHIFT) |
			(0x4 << UVD_MPC_SET_MUXA0__VARA_4__SHIFT)));

		/* setup UVD_MPC_SET_MUXB0 */
		WREG32_SOC15(VCN, i, mmUVD_MPC_SET_MUXB0,
			((0x1 << UVD_MPC_SET_MUXB0__VARB_1__SHIFT) |
			(0x2 << UVD_MPC_SET_MUXB0__VARB_2__SHIFT) |
			(0x3 << UVD_MPC_SET_MUXB0__VARB_3__SHIFT) |
			(0x4 << UVD_MPC_SET_MUXB0__VARB_4__SHIFT)));

		/* setup mmUVD_MPC_SET_MUX */
		WREG32_SOC15(VCN, i, mmUVD_MPC_SET_MUX,
			((0x0 << UVD_MPC_SET_MUX__SET_0__SHIFT) |
			(0x1 << UVD_MPC_SET_MUX__SET_1__SHIFT) |
			(0x2 << UVD_MPC_SET_MUX__SET_2__SHIFT)));

		vcn_v3_0_mc_resume(adev, i);

		/* VCN global tiling registers */
		WREG32_SOC15(VCN, i, mmUVD_GFX10_ADDR_CONFIG,
			adev->gfx.config.gb_addr_config);

		/* unblock VCPU register access */
		WREG32_P(SOC15_REG_OFFSET(VCN, i, mmUVD_RB_ARB_CTRL), 0,
			~UVD_RB_ARB_CTRL__VCPU_DIS_MASK);

		/* release VCPU reset to boot */
		WREG32_P(SOC15_REG_OFFSET(VCN, i, mmUVD_VCPU_CNTL), 0,
			~UVD_VCPU_CNTL__BLK_RST_MASK);

		for (j = 0; j < 10; ++j) {
			uint32_t status;

			for (k = 0; k < 100; ++k) {
				status = RREG32_SOC15(VCN, i, mmUVD_STATUS);
				if (status & 2)
					break;
				mdelay(10);
			}
			r = 0;
			if (status & 2)
				break;

			DRM_ERROR("VCN[%d] decode not responding, trying to reset the VCPU!!!\n", i);
			WREG32_P(SOC15_REG_OFFSET(VCN, i, mmUVD_VCPU_CNTL),
				UVD_VCPU_CNTL__BLK_RST_MASK,
				~UVD_VCPU_CNTL__BLK_RST_MASK);
			mdelay(10);
			WREG32_P(SOC15_REG_OFFSET(VCN, i, mmUVD_VCPU_CNTL), 0,
				~UVD_VCPU_CNTL__BLK_RST_MASK);

			mdelay(10);
			r = -1;
		}

		if (r) {
			DRM_ERROR("VCN[%d] decode not responding, giving up!!!\n", i);
			return r;
		}

		/* enable master interrupt */
		WREG32_P(SOC15_REG_OFFSET(VCN, i, mmUVD_MASTINT_EN),
			UVD_MASTINT_EN__VCPU_EN_MASK,
			~UVD_MASTINT_EN__VCPU_EN_MASK);

		/* clear the busy bit of VCN_STATUS */
		WREG32_P(SOC15_REG_OFFSET(VCN, i, mmUVD_STATUS), 0,
			~(2 << UVD_STATUS__VCPU_REPORT__SHIFT));

		WREG32_SOC15(VCN, i, mmUVD_LMI_RBC_RB_VMID, 0);

		ring = &adev->vcn.inst[i].ring_dec;
		/* force RBC into idle state */
		rb_bufsz = order_base_2(ring->ring_size);
		tmp = REG_SET_FIELD(0, UVD_RBC_RB_CNTL, RB_BUFSZ, rb_bufsz);
		tmp = REG_SET_FIELD(tmp, UVD_RBC_RB_CNTL, RB_BLKSZ, 1);
		tmp = REG_SET_FIELD(tmp, UVD_RBC_RB_CNTL, RB_NO_FETCH, 1);
		tmp = REG_SET_FIELD(tmp, UVD_RBC_RB_CNTL, RB_NO_UPDATE, 1);
		tmp = REG_SET_FIELD(tmp, UVD_RBC_RB_CNTL, RB_RPTR_WR_EN, 1);
		WREG32_SOC15(VCN, i, mmUVD_RBC_RB_CNTL, tmp);

		fw_shared = adev->vcn.inst[i].fw_shared_cpu_addr;
		fw_shared->multi_queue.decode_queue_mode |= cpu_to_le32(FW_QUEUE_RING_RESET);

		/* programm the RB_BASE for ring buffer */
		WREG32_SOC15(VCN, i, mmUVD_LMI_RBC_RB_64BIT_BAR_LOW,
			lower_32_bits(ring->gpu_addr));
		WREG32_SOC15(VCN, i, mmUVD_LMI_RBC_RB_64BIT_BAR_HIGH,
			upper_32_bits(ring->gpu_addr));

		/* Initialize the ring buffer's read and write pointers */
		WREG32_SOC15(VCN, i, mmUVD_RBC_RB_RPTR, 0);

		WREG32_SOC15(VCN, i, mmUVD_SCRATCH2, 0);
		ring->wptr = RREG32_SOC15(VCN, i, mmUVD_RBC_RB_RPTR);
		WREG32_SOC15(VCN, i, mmUVD_RBC_RB_WPTR,
			lower_32_bits(ring->wptr));
		fw_shared->rb.wptr = lower_32_bits(ring->wptr);
		fw_shared->multi_queue.decode_queue_mode &= cpu_to_le32(~FW_QUEUE_RING_RESET);

		fw_shared->multi_queue.encode_generalpurpose_queue_mode |= cpu_to_le32(FW_QUEUE_RING_RESET);
		ring = &adev->vcn.inst[i].ring_enc[0];
		WREG32_SOC15(VCN, i, mmUVD_RB_RPTR, lower_32_bits(ring->wptr));
		WREG32_SOC15(VCN, i, mmUVD_RB_WPTR, lower_32_bits(ring->wptr));
		WREG32_SOC15(VCN, i, mmUVD_RB_BASE_LO, ring->gpu_addr);
		WREG32_SOC15(VCN, i, mmUVD_RB_BASE_HI, upper_32_bits(ring->gpu_addr));
		WREG32_SOC15(VCN, i, mmUVD_RB_SIZE, ring->ring_size / 4);
		fw_shared->multi_queue.encode_generalpurpose_queue_mode &= cpu_to_le32(~FW_QUEUE_RING_RESET);

		fw_shared->multi_queue.encode_lowlatency_queue_mode |= cpu_to_le32(FW_QUEUE_RING_RESET);
		ring = &adev->vcn.inst[i].ring_enc[1];
		WREG32_SOC15(VCN, i, mmUVD_RB_RPTR2, lower_32_bits(ring->wptr));
		WREG32_SOC15(VCN, i, mmUVD_RB_WPTR2, lower_32_bits(ring->wptr));
		WREG32_SOC15(VCN, i, mmUVD_RB_BASE_LO2, ring->gpu_addr);
		WREG32_SOC15(VCN, i, mmUVD_RB_BASE_HI2, upper_32_bits(ring->gpu_addr));
		WREG32_SOC15(VCN, i, mmUVD_RB_SIZE2, ring->ring_size / 4);
		fw_shared->multi_queue.encode_lowlatency_queue_mode &= cpu_to_le32(~FW_QUEUE_RING_RESET);
	}

	return 0;
}

static int vcn_v3_0_start_sriov(struct amdgpu_device *adev)
{
	int i, j;
	struct amdgpu_ring *ring;
	uint64_t cache_addr;
	uint64_t rb_addr;
	uint64_t ctx_addr;
	uint32_t param, resp, expected;
	uint32_t offset, cache_size;
	uint32_t tmp, timeout;
	uint32_t id;

	struct amdgpu_mm_table *table = &adev->virt.mm_table;
	uint32_t *table_loc;
	uint32_t table_size;
	uint32_t size, size_dw;

	bool is_vcn_ready;

	struct mmsch_v3_0_cmd_direct_write
		direct_wt = { {0} };
	struct mmsch_v3_0_cmd_direct_read_modify_write
		direct_rd_mod_wt = { {0} };
	struct mmsch_v3_0_cmd_end end = { {0} };
	struct mmsch_v3_0_init_header header;

	direct_wt.cmd_header.command_type =
		MMSCH_COMMAND__DIRECT_REG_WRITE;
	direct_rd_mod_wt.cmd_header.command_type =
		MMSCH_COMMAND__DIRECT_REG_READ_MODIFY_WRITE;
	end.cmd_header.command_type =
		MMSCH_COMMAND__END;

	header.version = MMSCH_VERSION;
	header.total_size = sizeof(struct mmsch_v3_0_init_header) >> 2;
	for (i = 0; i < AMDGPU_MAX_VCN_INSTANCES; i++) {
		header.inst[i].init_status = 0;
		header.inst[i].table_offset = 0;
		header.inst[i].table_size = 0;
	}

	table_loc = (uint32_t *)table->cpu_addr;
	table_loc += header.total_size;
	for (i = 0; i < adev->vcn.num_vcn_inst; i++) {
		if (adev->vcn.harvest_config & (1 << i))
			continue;

		table_size = 0;

		MMSCH_V3_0_INSERT_DIRECT_RD_MOD_WT(SOC15_REG_OFFSET(VCN, i,
			mmUVD_STATUS),
			~UVD_STATUS__UVD_BUSY, UVD_STATUS__UVD_BUSY);

		cache_size = AMDGPU_GPU_PAGE_ALIGN(adev->vcn.fw->size + 4);

		if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) {
			id = amdgpu_ucode_id_vcns[i];
			MMSCH_V3_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCN, i,
				mmUVD_LMI_VCPU_CACHE_64BIT_BAR_LOW),
				adev->firmware.ucode[id].tmr_mc_addr_lo);
			MMSCH_V3_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCN, i,
				mmUVD_LMI_VCPU_CACHE_64BIT_BAR_HIGH),
				adev->firmware.ucode[id].tmr_mc_addr_hi);
			offset = 0;
			MMSCH_V3_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCN, i,
				mmUVD_VCPU_CACHE_OFFSET0),
				0);
		} else {
			MMSCH_V3_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCN, i,
				mmUVD_LMI_VCPU_CACHE_64BIT_BAR_LOW),
				lower_32_bits(adev->vcn.inst[i].gpu_addr));
			MMSCH_V3_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCN, i,
				mmUVD_LMI_VCPU_CACHE_64BIT_BAR_HIGH),
				upper_32_bits(adev->vcn.inst[i].gpu_addr));
			offset = cache_size;
			MMSCH_V3_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCN, i,
				mmUVD_VCPU_CACHE_OFFSET0),
				AMDGPU_UVD_FIRMWARE_OFFSET >> 3);
		}

		MMSCH_V3_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCN, i,
			mmUVD_VCPU_CACHE_SIZE0),
			cache_size);

		cache_addr = adev->vcn.inst[i].gpu_addr + offset;
		MMSCH_V3_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCN, i,
			mmUVD_LMI_VCPU_CACHE1_64BIT_BAR_LOW),
			lower_32_bits(cache_addr));
		MMSCH_V3_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCN, i,
			mmUVD_LMI_VCPU_CACHE1_64BIT_BAR_HIGH),
			upper_32_bits(cache_addr));
		MMSCH_V3_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCN, i,
			mmUVD_VCPU_CACHE_OFFSET1),
			0);
		MMSCH_V3_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCN, i,
			mmUVD_VCPU_CACHE_SIZE1),
			AMDGPU_VCN_STACK_SIZE);

		cache_addr = adev->vcn.inst[i].gpu_addr + offset +
			AMDGPU_VCN_STACK_SIZE;
		MMSCH_V3_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCN, i,
			mmUVD_LMI_VCPU_CACHE2_64BIT_BAR_LOW),
			lower_32_bits(cache_addr));
		MMSCH_V3_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCN, i,
			mmUVD_LMI_VCPU_CACHE2_64BIT_BAR_HIGH),
			upper_32_bits(cache_addr));
		MMSCH_V3_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCN, i,
			mmUVD_VCPU_CACHE_OFFSET2),
			0);
		MMSCH_V3_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCN, i,
			mmUVD_VCPU_CACHE_SIZE2),
			AMDGPU_VCN_CONTEXT_SIZE);

		for (j = 0; j < adev->vcn.num_enc_rings; ++j) {
			ring = &adev->vcn.inst[i].ring_enc[j];
			ring->wptr = 0;
			rb_addr = ring->gpu_addr;
			MMSCH_V3_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCN, i,
				mmUVD_RB_BASE_LO),
				lower_32_bits(rb_addr));
			MMSCH_V3_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCN, i,
				mmUVD_RB_BASE_HI),
				upper_32_bits(rb_addr));
			MMSCH_V3_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCN, i,
				mmUVD_RB_SIZE),
				ring->ring_size / 4);
		}

		ring = &adev->vcn.inst[i].ring_dec;
		ring->wptr = 0;
		rb_addr = ring->gpu_addr;
		MMSCH_V3_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCN, i,
			mmUVD_LMI_RBC_RB_64BIT_BAR_LOW),
			lower_32_bits(rb_addr));
		MMSCH_V3_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCN, i,
			mmUVD_LMI_RBC_RB_64BIT_BAR_HIGH),
			upper_32_bits(rb_addr));
		/* force RBC into idle state */
		tmp = order_base_2(ring->ring_size);
		tmp = REG_SET_FIELD(0, UVD_RBC_RB_CNTL, RB_BUFSZ, tmp);
		tmp = REG_SET_FIELD(tmp, UVD_RBC_RB_CNTL, RB_BLKSZ, 1);
		tmp = REG_SET_FIELD(tmp, UVD_RBC_RB_CNTL, RB_NO_FETCH, 1);
		tmp = REG_SET_FIELD(tmp, UVD_RBC_RB_CNTL, RB_NO_UPDATE, 1);
		tmp = REG_SET_FIELD(tmp, UVD_RBC_RB_CNTL, RB_RPTR_WR_EN, 1);
		MMSCH_V3_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCN, i,
			mmUVD_RBC_RB_CNTL),
			tmp);

		/* add end packet */
		MMSCH_V3_0_INSERT_END();

		/* refine header */
		header.inst[i].init_status = 0;
		header.inst[i].table_offset = header.total_size;
		header.inst[i].table_size = table_size;
		header.total_size += table_size;
	}

	/* Update init table header in memory */
	size = sizeof(struct mmsch_v3_0_init_header);
	table_loc = (uint32_t *)table->cpu_addr;
	memcpy((void *)table_loc, &header, size);

	/* message MMSCH (in VCN[0]) to initialize this client
	 * 1, write to mmsch_vf_ctx_addr_lo/hi register with GPU mc addr
	 * of memory descriptor location
	 */
	ctx_addr = table->gpu_addr;
	WREG32_SOC15(VCN, 0, mmMMSCH_VF_CTX_ADDR_LO, lower_32_bits(ctx_addr));
	WREG32_SOC15(VCN, 0, mmMMSCH_VF_CTX_ADDR_HI, upper_32_bits(ctx_addr));

	/* 2, update vmid of descriptor */
	tmp = RREG32_SOC15(VCN, 0, mmMMSCH_VF_VMID);
	tmp &= ~MMSCH_VF_VMID__VF_CTX_VMID_MASK;
	/* use domain0 for MM scheduler */
	tmp |= (0 << MMSCH_VF_VMID__VF_CTX_VMID__SHIFT);
	WREG32_SOC15(VCN, 0, mmMMSCH_VF_VMID, tmp);

	/* 3, notify mmsch about the size of this descriptor */
	size = header.total_size;
	WREG32_SOC15(VCN, 0, mmMMSCH_VF_CTX_SIZE, size);

	/* 4, set resp to zero */
	WREG32_SOC15(VCN, 0, mmMMSCH_VF_MAILBOX_RESP, 0);

	/* 5, kick off the initialization and wait until
	 * MMSCH_VF_MAILBOX_RESP becomes non-zero
	 */
	param = 0x10000001;
	WREG32_SOC15(VCN, 0, mmMMSCH_VF_MAILBOX_HOST, param);
	tmp = 0;
	timeout = 1000;
	resp = 0;
	expected = param + 1;
	while (resp != expected) {
		resp = RREG32_SOC15(VCN, 0, mmMMSCH_VF_MAILBOX_RESP);
		if (resp == expected)
			break;

		udelay(10);
		tmp = tmp + 10;
		if (tmp >= timeout) {
			DRM_ERROR("failed to init MMSCH. TIME-OUT after %d usec"\
				" waiting for mmMMSCH_VF_MAILBOX_RESP "\
				"(expected=0x%08x, readback=0x%08x)\n",
				tmp, expected, resp);
			return -EBUSY;
		}
	}

	/* 6, check each VCN's init_status
	 * if it remains as 0, then this VCN is not assigned to current VF
	 * do not start ring for this VCN
	 */
	size = sizeof(struct mmsch_v3_0_init_header);
	table_loc = (uint32_t *)table->cpu_addr;
	memcpy(&header, (void *)table_loc, size);

	for (i = 0; i < adev->vcn.num_vcn_inst; i++) {
		if (adev->vcn.harvest_config & (1 << i))
			continue;

		is_vcn_ready = (header.inst[i].init_status == 1);
		if (!is_vcn_ready)
			DRM_INFO("VCN(%d) engine is disabled by hypervisor\n", i);

		ring = &adev->vcn.inst[i].ring_dec;
		ring->sched.ready = is_vcn_ready;
		for (j = 0; j < adev->vcn.num_enc_rings; ++j) {
			ring = &adev->vcn.inst[i].ring_enc[j];
			ring->sched.ready = is_vcn_ready;
		}
	}

	return 0;
}

static int vcn_v3_0_stop_dpg_mode(struct amdgpu_device *adev, int inst_idx)
{
	uint32_t tmp;

	/* Wait for power status to be 1 */
	SOC15_WAIT_ON_RREG(VCN, inst_idx, mmUVD_POWER_STATUS, 1,
		UVD_POWER_STATUS__UVD_POWER_STATUS_MASK);

	/* wait for read ptr to be equal to write ptr */
	tmp = RREG32_SOC15(VCN, inst_idx, mmUVD_RB_WPTR);
	SOC15_WAIT_ON_RREG(VCN, inst_idx, mmUVD_RB_RPTR, tmp, 0xFFFFFFFF);

	tmp = RREG32_SOC15(VCN, inst_idx, mmUVD_RB_WPTR2);
	SOC15_WAIT_ON_RREG(VCN, inst_idx, mmUVD_RB_RPTR2, tmp, 0xFFFFFFFF);

	tmp = RREG32_SOC15(VCN, inst_idx, mmUVD_RBC_RB_WPTR) & 0x7FFFFFFF;
	SOC15_WAIT_ON_RREG(VCN, inst_idx, mmUVD_RBC_RB_RPTR, tmp, 0xFFFFFFFF);

	SOC15_WAIT_ON_RREG(VCN, inst_idx, mmUVD_POWER_STATUS, 1,
		UVD_POWER_STATUS__UVD_POWER_STATUS_MASK);

	/* disable dynamic power gating mode */
	WREG32_P(SOC15_REG_OFFSET(VCN, inst_idx, mmUVD_POWER_STATUS), 0,
		~UVD_POWER_STATUS__UVD_PG_MODE_MASK);

	return 0;
}

static int vcn_v3_0_stop(struct amdgpu_device *adev)
{
	uint32_t tmp;
	int i, r = 0;

	for (i = 0; i < adev->vcn.num_vcn_inst; ++i) {
		if (adev->vcn.harvest_config & (1 << i))
			continue;

		if (adev->pg_flags & AMD_PG_SUPPORT_VCN_DPG) {
			r = vcn_v3_0_stop_dpg_mode(adev, i);
			continue;
		}

		/* wait for vcn idle */
		r = SOC15_WAIT_ON_RREG(VCN, i, mmUVD_STATUS, UVD_STATUS__IDLE, 0x7);
		if (r)
			return r;

		tmp = UVD_LMI_STATUS__VCPU_LMI_WRITE_CLEAN_MASK |
			UVD_LMI_STATUS__READ_CLEAN_MASK |
			UVD_LMI_STATUS__WRITE_CLEAN_MASK |
			UVD_LMI_STATUS__WRITE_CLEAN_RAW_MASK;
		r = SOC15_WAIT_ON_RREG(VCN, i, mmUVD_LMI_STATUS, tmp, tmp);
		if (r)
			return r;

		/* disable LMI UMC channel */
		tmp = RREG32_SOC15(VCN, i, mmUVD_LMI_CTRL2);
		tmp |= UVD_LMI_CTRL2__STALL_ARB_UMC_MASK;
		WREG32_SOC15(VCN, i, mmUVD_LMI_CTRL2, tmp);
		tmp = UVD_LMI_STATUS__UMC_READ_CLEAN_RAW_MASK|
			UVD_LMI_STATUS__UMC_WRITE_CLEAN_RAW_MASK;
		r = SOC15_WAIT_ON_RREG(VCN, i, mmUVD_LMI_STATUS, tmp, tmp);
		if (r)
			return r;

		/* block VCPU register access */
		WREG32_P(SOC15_REG_OFFSET(VCN, i, mmUVD_RB_ARB_CTRL),
			UVD_RB_ARB_CTRL__VCPU_DIS_MASK,
			~UVD_RB_ARB_CTRL__VCPU_DIS_MASK);

		/* reset VCPU */
		WREG32_P(SOC15_REG_OFFSET(VCN, i, mmUVD_VCPU_CNTL),
			UVD_VCPU_CNTL__BLK_RST_MASK,
			~UVD_VCPU_CNTL__BLK_RST_MASK);

		/* disable VCPU clock */
		WREG32_P(SOC15_REG_OFFSET(VCN, i, mmUVD_VCPU_CNTL), 0,
			~(UVD_VCPU_CNTL__CLK_EN_MASK));

		/* apply soft reset */
		tmp = RREG32_SOC15(VCN, i, mmUVD_SOFT_RESET);
		tmp |= UVD_SOFT_RESET__LMI_UMC_SOFT_RESET_MASK;
		WREG32_SOC15(VCN, i, mmUVD_SOFT_RESET, tmp);
		tmp = RREG32_SOC15(VCN, i, mmUVD_SOFT_RESET);
		tmp |= UVD_SOFT_RESET__LMI_SOFT_RESET_MASK;
		WREG32_SOC15(VCN, i, mmUVD_SOFT_RESET, tmp);

		/* clear status */
		WREG32_SOC15(VCN, i, mmUVD_STATUS, 0);

		/* apply HW clock gating */
		vcn_v3_0_enable_clock_gating(adev, i);

		/* enable VCN power gating */
		vcn_v3_0_enable_static_power_gating(adev, i);
	}

	if (adev->pm.dpm_enabled)
		amdgpu_dpm_enable_uvd(adev, false);

	return 0;
}

static int vcn_v3_0_pause_dpg_mode(struct amdgpu_device *adev,
		   int inst_idx, struct dpg_pause_state *new_state)
{
	volatile struct amdgpu_fw_shared *fw_shared;
	struct amdgpu_ring *ring;
	uint32_t reg_data = 0;
	int ret_code;

	/* pause/unpause if state is changed */
	if (adev->vcn.inst[inst_idx].pause_state.fw_based != new_state->fw_based) {
		DRM_DEBUG("dpg pause state changed %d -> %d",
			adev->vcn.inst[inst_idx].pause_state.fw_based,	new_state->fw_based);
		reg_data = RREG32_SOC15(VCN, inst_idx, mmUVD_DPG_PAUSE) &
			(~UVD_DPG_PAUSE__NJ_PAUSE_DPG_ACK_MASK);

		if (new_state->fw_based == VCN_DPG_STATE__PAUSE) {
			ret_code = SOC15_WAIT_ON_RREG(VCN, inst_idx, mmUVD_POWER_STATUS, 0x1,
				UVD_POWER_STATUS__UVD_POWER_STATUS_MASK);

			if (!ret_code) {
				/* pause DPG */
				reg_data |= UVD_DPG_PAUSE__NJ_PAUSE_DPG_REQ_MASK;
				WREG32_SOC15(VCN, inst_idx, mmUVD_DPG_PAUSE, reg_data);

				/* wait for ACK */
				SOC15_WAIT_ON_RREG(VCN, inst_idx, mmUVD_DPG_PAUSE,
					UVD_DPG_PAUSE__NJ_PAUSE_DPG_ACK_MASK,
					UVD_DPG_PAUSE__NJ_PAUSE_DPG_ACK_MASK);

				/* Stall DPG before WPTR/RPTR reset */
				WREG32_P(SOC15_REG_OFFSET(VCN, inst_idx, mmUVD_POWER_STATUS),
					UVD_POWER_STATUS__STALL_DPG_POWER_UP_MASK,
					~UVD_POWER_STATUS__STALL_DPG_POWER_UP_MASK);

				/* Restore */
				fw_shared = adev->vcn.inst[inst_idx].fw_shared_cpu_addr;
				fw_shared->multi_queue.encode_generalpurpose_queue_mode |= cpu_to_le32(FW_QUEUE_RING_RESET);
				ring = &adev->vcn.inst[inst_idx].ring_enc[0];
				ring->wptr = 0;
				WREG32_SOC15(VCN, inst_idx, mmUVD_RB_BASE_LO, ring->gpu_addr);
				WREG32_SOC15(VCN, inst_idx, mmUVD_RB_BASE_HI, upper_32_bits(ring->gpu_addr));
				WREG32_SOC15(VCN, inst_idx, mmUVD_RB_SIZE, ring->ring_size / 4);
				WREG32_SOC15(VCN, inst_idx, mmUVD_RB_RPTR, lower_32_bits(ring->wptr));
				WREG32_SOC15(VCN, inst_idx, mmUVD_RB_WPTR, lower_32_bits(ring->wptr));
				fw_shared->multi_queue.encode_generalpurpose_queue_mode &= cpu_to_le32(~FW_QUEUE_RING_RESET);

				fw_shared->multi_queue.encode_lowlatency_queue_mode |= cpu_to_le32(FW_QUEUE_RING_RESET);
				ring = &adev->vcn.inst[inst_idx].ring_enc[1];
				ring->wptr = 0;
				WREG32_SOC15(VCN, inst_idx, mmUVD_RB_BASE_LO2, ring->gpu_addr);
				WREG32_SOC15(VCN, inst_idx, mmUVD_RB_BASE_HI2, upper_32_bits(ring->gpu_addr));
				WREG32_SOC15(VCN, inst_idx, mmUVD_RB_SIZE2, ring->ring_size / 4);
				WREG32_SOC15(VCN, inst_idx, mmUVD_RB_RPTR2, lower_32_bits(ring->wptr));
				WREG32_SOC15(VCN, inst_idx, mmUVD_RB_WPTR2, lower_32_bits(ring->wptr));
				fw_shared->multi_queue.encode_lowlatency_queue_mode &= cpu_to_le32(~FW_QUEUE_RING_RESET);

				/* restore wptr/rptr with pointers saved in FW shared memory*/
				WREG32_SOC15(VCN, inst_idx, mmUVD_RBC_RB_RPTR, fw_shared->rb.rptr);
				WREG32_SOC15(VCN, inst_idx, mmUVD_RBC_RB_WPTR, fw_shared->rb.wptr);

				/* Unstall DPG */
				WREG32_P(SOC15_REG_OFFSET(VCN, inst_idx, mmUVD_POWER_STATUS),
					0, ~UVD_POWER_STATUS__STALL_DPG_POWER_UP_MASK);

				SOC15_WAIT_ON_RREG(VCN, inst_idx, mmUVD_POWER_STATUS,
					UVD_PGFSM_CONFIG__UVDM_UVDU_PWR_ON, UVD_POWER_STATUS__UVD_POWER_STATUS_MASK);
			}
		} else {
			/* unpause dpg, no need to wait */
			reg_data &= ~UVD_DPG_PAUSE__NJ_PAUSE_DPG_REQ_MASK;
			WREG32_SOC15(VCN, inst_idx, mmUVD_DPG_PAUSE, reg_data);
		}
		adev->vcn.inst[inst_idx].pause_state.fw_based = new_state->fw_based;
	}

	return 0;
}

/**
 * vcn_v3_0_dec_ring_get_rptr - get read pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Returns the current hardware read pointer
 */
static uint64_t vcn_v3_0_dec_ring_get_rptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	return RREG32_SOC15(VCN, ring->me, mmUVD_RBC_RB_RPTR);
}

/**
 * vcn_v3_0_dec_ring_get_wptr - get write pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Returns the current hardware write pointer
 */
static uint64_t vcn_v3_0_dec_ring_get_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring->use_doorbell)
		return adev->wb.wb[ring->wptr_offs];
	else
		return RREG32_SOC15(VCN, ring->me, mmUVD_RBC_RB_WPTR);
}

/**
 * vcn_v3_0_dec_ring_set_wptr - set write pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Commits the write pointer to the hardware
 */
static void vcn_v3_0_dec_ring_set_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	volatile struct amdgpu_fw_shared *fw_shared;

	if (adev->pg_flags & AMD_PG_SUPPORT_VCN_DPG) {
		/*whenever update RBC_RB_WPTR, we save the wptr in shared rb.wptr and scratch2 */
		fw_shared = adev->vcn.inst[ring->me].fw_shared_cpu_addr;
		fw_shared->rb.wptr = lower_32_bits(ring->wptr);
		WREG32_SOC15(VCN, ring->me, mmUVD_SCRATCH2,
			lower_32_bits(ring->wptr));
	}

	if (ring->use_doorbell) {
		adev->wb.wb[ring->wptr_offs] = lower_32_bits(ring->wptr);
		WDOORBELL32(ring->doorbell_index, lower_32_bits(ring->wptr));
	} else {
		WREG32_SOC15(VCN, ring->me, mmUVD_RBC_RB_WPTR, lower_32_bits(ring->wptr));
	}
}

static void vcn_v3_0_dec_sw_ring_emit_fence(struct amdgpu_ring *ring, u64 addr,
				u64 seq, uint32_t flags)
{
	WARN_ON(flags & AMDGPU_FENCE_FLAG_64BIT);

	amdgpu_ring_write(ring, VCN_DEC_SW_CMD_FENCE);
	amdgpu_ring_write(ring, addr);
	amdgpu_ring_write(ring, upper_32_bits(addr));
	amdgpu_ring_write(ring, seq);
	amdgpu_ring_write(ring, VCN_DEC_SW_CMD_TRAP);
}

static void vcn_v3_0_dec_sw_ring_insert_end(struct amdgpu_ring *ring)
{
	amdgpu_ring_write(ring, VCN_DEC_SW_CMD_END);
}

static void vcn_v3_0_dec_sw_ring_emit_ib(struct amdgpu_ring *ring,
			       struct amdgpu_job *job,
			       struct amdgpu_ib *ib,
			       uint32_t flags)
{
	uint32_t vmid = AMDGPU_JOB_GET_VMID(job);

	amdgpu_ring_write(ring, VCN_DEC_SW_CMD_IB);
	amdgpu_ring_write(ring, vmid);
	amdgpu_ring_write(ring, lower_32_bits(ib->gpu_addr));
	amdgpu_ring_write(ring, upper_32_bits(ib->gpu_addr));
	amdgpu_ring_write(ring, ib->length_dw);
}

static void vcn_v3_0_dec_sw_ring_emit_reg_wait(struct amdgpu_ring *ring, uint32_t reg,
				uint32_t val, uint32_t mask)
{
	amdgpu_ring_write(ring, VCN_DEC_SW_CMD_REG_WAIT);
	amdgpu_ring_write(ring, reg << 2);
	amdgpu_ring_write(ring, mask);
	amdgpu_ring_write(ring, val);
}

static void vcn_v3_0_dec_sw_ring_emit_vm_flush(struct amdgpu_ring *ring,
				uint32_t vmid, uint64_t pd_addr)
{
	struct amdgpu_vmhub *hub = &ring->adev->vmhub[ring->funcs->vmhub];
	uint32_t data0, data1, mask;

	pd_addr = amdgpu_gmc_emit_flush_gpu_tlb(ring, vmid, pd_addr);

	/* wait for register write */
	data0 = hub->ctx0_ptb_addr_lo32 + vmid * hub->ctx_addr_distance;
	data1 = lower_32_bits(pd_addr);
	mask = 0xffffffff;
	vcn_v3_0_dec_sw_ring_emit_reg_wait(ring, data0, data1, mask);
}

static void vcn_v3_0_dec_sw_ring_emit_wreg(struct amdgpu_ring *ring, uint32_t reg, uint32_t val)
{
	amdgpu_ring_write(ring, VCN_DEC_SW_CMD_REG_WRITE);
	amdgpu_ring_write(ring,	reg << 2);
	amdgpu_ring_write(ring, val);
}

static const struct amdgpu_ring_funcs vcn_v3_0_dec_sw_ring_vm_funcs = {
	.type = AMDGPU_RING_TYPE_VCN_DEC,
	.align_mask = 0x3f,
	.nop = VCN_DEC_SW_CMD_NO_OP,
	.vmhub = AMDGPU_MMHUB_0,
	.get_rptr = vcn_v3_0_dec_ring_get_rptr,
	.get_wptr = vcn_v3_0_dec_ring_get_wptr,
	.set_wptr = vcn_v3_0_dec_ring_set_wptr,
	.emit_frame_size =
		SOC15_FLUSH_GPU_TLB_NUM_WREG * 3 +
		SOC15_FLUSH_GPU_TLB_NUM_REG_WAIT * 4 +
		4 + /* vcn_v3_0_dec_sw_ring_emit_vm_flush */
		5 + 5 + /* vcn_v3_0_dec_sw_ring_emit_fdec_swe x2 vm fdec_swe */
		1, /* vcn_v3_0_dec_sw_ring_insert_end */
	.emit_ib_size = 5, /* vcn_v3_0_dec_sw_ring_emit_ib */
	.emit_ib = vcn_v3_0_dec_sw_ring_emit_ib,
	.emit_fence = vcn_v3_0_dec_sw_ring_emit_fence,
	.emit_vm_flush = vcn_v3_0_dec_sw_ring_emit_vm_flush,
	.test_ring = amdgpu_vcn_dec_sw_ring_test_ring,
	.test_ib = NULL,//amdgpu_vcn_dec_sw_ring_test_ib,
	.insert_nop = amdgpu_ring_insert_nop,
	.insert_end = vcn_v3_0_dec_sw_ring_insert_end,
	.pad_ib = amdgpu_ring_generic_pad_ib,
	.begin_use = amdgpu_vcn_ring_begin_use,
	.end_use = amdgpu_vcn_ring_end_use,
	.emit_wreg = vcn_v3_0_dec_sw_ring_emit_wreg,
	.emit_reg_wait = vcn_v3_0_dec_sw_ring_emit_reg_wait,
	.emit_reg_write_reg_wait = amdgpu_ring_emit_reg_write_reg_wait_helper,
};

static int vcn_v3_0_limit_sched(struct amdgpu_cs_parser *p)
{
	struct drm_gpu_scheduler **scheds;

	/* The create msg must be in the first IB submitted */
	if (atomic_read(&p->entity->fence_seq))
		return -EINVAL;

	scheds = p->adev->gpu_sched[AMDGPU_HW_IP_VCN_DEC]
		[AMDGPU_RING_PRIO_DEFAULT].sched;
	drm_sched_entity_modify_sched(p->entity, scheds, 1);
	return 0;
}

static int vcn_v3_0_dec_msg(struct amdgpu_cs_parser *p, uint64_t addr)
{
	struct ttm_operation_ctx ctx = { false, false };
	struct amdgpu_bo_va_mapping *map;
	uint32_t *msg, num_buffers;
	struct amdgpu_bo *bo;
	uint64_t start, end;
	unsigned int i;
	void * ptr;
	int r;

	addr &= AMDGPU_GMC_HOLE_MASK;
	r = amdgpu_cs_find_mapping(p, addr, &bo, &map);
	if (r) {
		DRM_ERROR("Can't find BO for addr 0x%08Lx\n", addr);
		return r;
	}

	start = map->start * AMDGPU_GPU_PAGE_SIZE;
	end = (map->last + 1) * AMDGPU_GPU_PAGE_SIZE;
	if (addr & 0x7) {
		DRM_ERROR("VCN messages must be 8 byte aligned!\n");
		return -EINVAL;
	}

	bo->flags |= AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED;
	amdgpu_bo_placement_from_domain(bo, bo->allowed_domains);
	r = ttm_bo_validate(&bo->tbo, &bo->placement, &ctx);
	if (r) {
		DRM_ERROR("Failed validating the VCN message BO (%d)!\n", r);
		return r;
	}

	r = amdgpu_bo_kmap(bo, &ptr);
	if (r) {
		DRM_ERROR("Failed mapping the VCN message (%d)!\n", r);
		return r;
	}

	msg = ptr + addr - start;

	/* Check length */
	if (msg[1] > end - addr) {
		r = -EINVAL;
		goto out;
	}

	if (msg[3] != RDECODE_MSG_CREATE)
		goto out;

	num_buffers = msg[2];
	for (i = 0, msg = &msg[6]; i < num_buffers; ++i, msg += 4) {
		uint32_t offset, size, *create;

		if (msg[0] != RDECODE_MESSAGE_CREATE)
			continue;

		offset = msg[1];
		size = msg[2];

		if (offset + size > end) {
			r = -EINVAL;
			goto out;
		}

		create = ptr + addr + offset - start;

		/* H246, HEVC and VP9 can run on any instance */
		if (create[0] == 0x7 || create[0] == 0x10 || create[0] == 0x11)
			continue;

		r = vcn_v3_0_limit_sched(p);
		if (r)
			goto out;
	}

out:
	amdgpu_bo_kunmap(bo);
	return r;
}

static int vcn_v3_0_ring_patch_cs_in_place(struct amdgpu_cs_parser *p,
					   uint32_t ib_idx)
{
	struct amdgpu_ring *ring = to_amdgpu_ring(p->entity->rq->sched);
	struct amdgpu_ib *ib = &p->job->ibs[ib_idx];
	uint32_t msg_lo = 0, msg_hi = 0;
	unsigned i;
	int r;

	/* The first instance can decode anything */
	if (!ring->me)
		return 0;

	for (i = 0; i < ib->length_dw; i += 2) {
		uint32_t reg = amdgpu_get_ib_value(p, ib_idx, i);
		uint32_t val = amdgpu_get_ib_value(p, ib_idx, i + 1);

		if (reg == PACKET0(p->adev->vcn.internal.data0, 0)) {
			msg_lo = val;
		} else if (reg == PACKET0(p->adev->vcn.internal.data1, 0)) {
			msg_hi = val;
		} else if (reg == PACKET0(p->adev->vcn.internal.cmd, 0) &&
			   val == 0) {
			r = vcn_v3_0_dec_msg(p, ((u64)msg_hi) << 32 | msg_lo);
			if (r)
				return r;
		}
	}
	return 0;
}

static const struct amdgpu_ring_funcs vcn_v3_0_dec_ring_vm_funcs = {
	.type = AMDGPU_RING_TYPE_VCN_DEC,
	.align_mask = 0xf,
	.vmhub = AMDGPU_MMHUB_0,
	.get_rptr = vcn_v3_0_dec_ring_get_rptr,
	.get_wptr = vcn_v3_0_dec_ring_get_wptr,
	.set_wptr = vcn_v3_0_dec_ring_set_wptr,
	.patch_cs_in_place = vcn_v3_0_ring_patch_cs_in_place,
	.emit_frame_size =
		SOC15_FLUSH_GPU_TLB_NUM_WREG * 6 +
		SOC15_FLUSH_GPU_TLB_NUM_REG_WAIT * 8 +
		8 + /* vcn_v2_0_dec_ring_emit_vm_flush */
		14 + 14 + /* vcn_v2_0_dec_ring_emit_fence x2 vm fence */
		6,
	.emit_ib_size = 8, /* vcn_v2_0_dec_ring_emit_ib */
	.emit_ib = vcn_v2_0_dec_ring_emit_ib,
	.emit_fence = vcn_v2_0_dec_ring_emit_fence,
	.emit_vm_flush = vcn_v2_0_dec_ring_emit_vm_flush,
	.test_ring = vcn_v2_0_dec_ring_test_ring,
	.test_ib = amdgpu_vcn_dec_ring_test_ib,
	.insert_nop = vcn_v2_0_dec_ring_insert_nop,
	.insert_start = vcn_v2_0_dec_ring_insert_start,
	.insert_end = vcn_v2_0_dec_ring_insert_end,
	.pad_ib = amdgpu_ring_generic_pad_ib,
	.begin_use = amdgpu_vcn_ring_begin_use,
	.end_use = amdgpu_vcn_ring_end_use,
	.emit_wreg = vcn_v2_0_dec_ring_emit_wreg,
	.emit_reg_wait = vcn_v2_0_dec_ring_emit_reg_wait,
	.emit_reg_write_reg_wait = amdgpu_ring_emit_reg_write_reg_wait_helper,
};

/**
 * vcn_v3_0_enc_ring_get_rptr - get enc read pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Returns the current hardware enc read pointer
 */
static uint64_t vcn_v3_0_enc_ring_get_rptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring == &adev->vcn.inst[ring->me].ring_enc[0])
		return RREG32_SOC15(VCN, ring->me, mmUVD_RB_RPTR);
	else
		return RREG32_SOC15(VCN, ring->me, mmUVD_RB_RPTR2);
}

/**
 * vcn_v3_0_enc_ring_get_wptr - get enc write pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Returns the current hardware enc write pointer
 */
static uint64_t vcn_v3_0_enc_ring_get_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring == &adev->vcn.inst[ring->me].ring_enc[0]) {
		if (ring->use_doorbell)
			return adev->wb.wb[ring->wptr_offs];
		else
			return RREG32_SOC15(VCN, ring->me, mmUVD_RB_WPTR);
	} else {
		if (ring->use_doorbell)
			return adev->wb.wb[ring->wptr_offs];
		else
			return RREG32_SOC15(VCN, ring->me, mmUVD_RB_WPTR2);
	}
}

/**
 * vcn_v3_0_enc_ring_set_wptr - set enc write pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Commits the enc write pointer to the hardware
 */
static void vcn_v3_0_enc_ring_set_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring == &adev->vcn.inst[ring->me].ring_enc[0]) {
		if (ring->use_doorbell) {
			adev->wb.wb[ring->wptr_offs] = lower_32_bits(ring->wptr);
			WDOORBELL32(ring->doorbell_index, lower_32_bits(ring->wptr));
		} else {
			WREG32_SOC15(VCN, ring->me, mmUVD_RB_WPTR, lower_32_bits(ring->wptr));
		}
	} else {
		if (ring->use_doorbell) {
			adev->wb.wb[ring->wptr_offs] = lower_32_bits(ring->wptr);
			WDOORBELL32(ring->doorbell_index, lower_32_bits(ring->wptr));
		} else {
			WREG32_SOC15(VCN, ring->me, mmUVD_RB_WPTR2, lower_32_bits(ring->wptr));
		}
	}
}

static const struct amdgpu_ring_funcs vcn_v3_0_enc_ring_vm_funcs = {
	.type = AMDGPU_RING_TYPE_VCN_ENC,
	.align_mask = 0x3f,
	.nop = VCN_ENC_CMD_NO_OP,
	.vmhub = AMDGPU_MMHUB_0,
	.get_rptr = vcn_v3_0_enc_ring_get_rptr,
	.get_wptr = vcn_v3_0_enc_ring_get_wptr,
	.set_wptr = vcn_v3_0_enc_ring_set_wptr,
	.emit_frame_size =
		SOC15_FLUSH_GPU_TLB_NUM_WREG * 3 +
		SOC15_FLUSH_GPU_TLB_NUM_REG_WAIT * 4 +
		4 + /* vcn_v2_0_enc_ring_emit_vm_flush */
		5 + 5 + /* vcn_v2_0_enc_ring_emit_fence x2 vm fence */
		1, /* vcn_v2_0_enc_ring_insert_end */
	.emit_ib_size = 5, /* vcn_v2_0_enc_ring_emit_ib */
	.emit_ib = vcn_v2_0_enc_ring_emit_ib,
	.emit_fence = vcn_v2_0_enc_ring_emit_fence,
	.emit_vm_flush = vcn_v2_0_enc_ring_emit_vm_flush,
	.test_ring = amdgpu_vcn_enc_ring_test_ring,
	.test_ib = amdgpu_vcn_enc_ring_test_ib,
	.insert_nop = amdgpu_ring_insert_nop,
	.insert_end = vcn_v2_0_enc_ring_insert_end,
	.pad_ib = amdgpu_ring_generic_pad_ib,
	.begin_use = amdgpu_vcn_ring_begin_use,
	.end_use = amdgpu_vcn_ring_end_use,
	.emit_wreg = vcn_v2_0_enc_ring_emit_wreg,
	.emit_reg_wait = vcn_v2_0_enc_ring_emit_reg_wait,
	.emit_reg_write_reg_wait = amdgpu_ring_emit_reg_write_reg_wait_helper,
};

static void vcn_v3_0_set_dec_ring_funcs(struct amdgpu_device *adev)
{
	int i;

	for (i = 0; i < adev->vcn.num_vcn_inst; ++i) {
		if (adev->vcn.harvest_config & (1 << i))
			continue;

		if (!DEC_SW_RING_ENABLED)
			adev->vcn.inst[i].ring_dec.funcs = &vcn_v3_0_dec_ring_vm_funcs;
		else
			adev->vcn.inst[i].ring_dec.funcs = &vcn_v3_0_dec_sw_ring_vm_funcs;
		adev->vcn.inst[i].ring_dec.me = i;
		DRM_INFO("VCN(%d) decode%s is enabled in VM mode\n", i,
			  DEC_SW_RING_ENABLED?"(Software Ring)":"");
	}
}

static void vcn_v3_0_set_enc_ring_funcs(struct amdgpu_device *adev)
{
	int i, j;

	for (i = 0; i < adev->vcn.num_vcn_inst; ++i) {
		if (adev->vcn.harvest_config & (1 << i))
			continue;

		for (j = 0; j < adev->vcn.num_enc_rings; ++j) {
			adev->vcn.inst[i].ring_enc[j].funcs = &vcn_v3_0_enc_ring_vm_funcs;
			adev->vcn.inst[i].ring_enc[j].me = i;
		}
		DRM_INFO("VCN(%d) encode is enabled in VM mode\n", i);
	}
}

static bool vcn_v3_0_is_idle(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int i, ret = 1;

	for (i = 0; i < adev->vcn.num_vcn_inst; ++i) {
		if (adev->vcn.harvest_config & (1 << i))
			continue;

		ret &= (RREG32_SOC15(VCN, i, mmUVD_STATUS) == UVD_STATUS__IDLE);
	}

	return ret;
}

static int vcn_v3_0_wait_for_idle(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int i, ret = 0;

	for (i = 0; i < adev->vcn.num_vcn_inst; ++i) {
		if (adev->vcn.harvest_config & (1 << i))
			continue;

		ret = SOC15_WAIT_ON_RREG(VCN, i, mmUVD_STATUS, UVD_STATUS__IDLE,
			UVD_STATUS__IDLE);
		if (ret)
			return ret;
	}

	return ret;
}

static int vcn_v3_0_set_clockgating_state(void *handle,
					  enum amd_clockgating_state state)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	bool enable = (state == AMD_CG_STATE_GATE) ? true : false;
	int i;

	for (i = 0; i < adev->vcn.num_vcn_inst; ++i) {
		if (adev->vcn.harvest_config & (1 << i))
			continue;

		if (enable) {
			if (RREG32_SOC15(VCN, i, mmUVD_STATUS) != UVD_STATUS__IDLE)
				return -EBUSY;
			vcn_v3_0_enable_clock_gating(adev, i);
		} else {
			vcn_v3_0_disable_clock_gating(adev, i);
		}
	}

	return 0;
}

static int vcn_v3_0_set_powergating_state(void *handle,
					  enum amd_powergating_state state)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int ret;

	/* for SRIOV, guest should not control VCN Power-gating
	 * MMSCH FW should control Power-gating and clock-gating
	 * guest should avoid touching CGC and PG
	 */
	if (amdgpu_sriov_vf(adev)) {
		adev->vcn.cur_state = AMD_PG_STATE_UNGATE;
		return 0;
	}

	if(state == adev->vcn.cur_state)
		return 0;

	if (state == AMD_PG_STATE_GATE)
		ret = vcn_v3_0_stop(adev);
	else
		ret = vcn_v3_0_start(adev);

	if(!ret)
		adev->vcn.cur_state = state;

	return ret;
}

static int vcn_v3_0_set_interrupt_state(struct amdgpu_device *adev,
					struct amdgpu_irq_src *source,
					unsigned type,
					enum amdgpu_interrupt_state state)
{
	return 0;
}

static int vcn_v3_0_process_interrupt(struct amdgpu_device *adev,
				      struct amdgpu_irq_src *source,
				      struct amdgpu_iv_entry *entry)
{
	uint32_t ip_instance;

	switch (entry->client_id) {
	case SOC15_IH_CLIENTID_VCN:
		ip_instance = 0;
		break;
	case SOC15_IH_CLIENTID_VCN1:
		ip_instance = 1;
		break;
	default:
		DRM_ERROR("Unhandled client id: %d\n", entry->client_id);
		return 0;
	}

	DRM_DEBUG("IH: VCN TRAP\n");

	switch (entry->src_id) {
	case VCN_2_0__SRCID__UVD_SYSTEM_MESSAGE_INTERRUPT:
		amdgpu_fence_process(&adev->vcn.inst[ip_instance].ring_dec);
		break;
	case VCN_2_0__SRCID__UVD_ENC_GENERAL_PURPOSE:
		amdgpu_fence_process(&adev->vcn.inst[ip_instance].ring_enc[0]);
		break;
	case VCN_2_0__SRCID__UVD_ENC_LOW_LATENCY:
		amdgpu_fence_process(&adev->vcn.inst[ip_instance].ring_enc[1]);
		break;
	default:
		DRM_ERROR("Unhandled interrupt: %d %d\n",
			  entry->src_id, entry->src_data[0]);
		break;
	}

	return 0;
}

static const struct amdgpu_irq_src_funcs vcn_v3_0_irq_funcs = {
	.set = vcn_v3_0_set_interrupt_state,
	.process = vcn_v3_0_process_interrupt,
};

static void vcn_v3_0_set_irq_funcs(struct amdgpu_device *adev)
{
	int i;

	for (i = 0; i < adev->vcn.num_vcn_inst; ++i) {
		if (adev->vcn.harvest_config & (1 << i))
			continue;

		adev->vcn.inst[i].irq.num_types = adev->vcn.num_enc_rings + 1;
		adev->vcn.inst[i].irq.funcs = &vcn_v3_0_irq_funcs;
	}
}

static const struct amd_ip_funcs vcn_v3_0_ip_funcs = {
	.name = "vcn_v3_0",
	.early_init = vcn_v3_0_early_init,
	.late_init = NULL,
	.sw_init = vcn_v3_0_sw_init,
	.sw_fini = vcn_v3_0_sw_fini,
	.hw_init = vcn_v3_0_hw_init,
	.hw_fini = vcn_v3_0_hw_fini,
	.suspend = vcn_v3_0_suspend,
	.resume = vcn_v3_0_resume,
	.is_idle = vcn_v3_0_is_idle,
	.wait_for_idle = vcn_v3_0_wait_for_idle,
	.check_soft_reset = NULL,
	.pre_soft_reset = NULL,
	.soft_reset = NULL,
	.post_soft_reset = NULL,
	.set_clockgating_state = vcn_v3_0_set_clockgating_state,
	.set_powergating_state = vcn_v3_0_set_powergating_state,
};

const struct amdgpu_ip_block_version vcn_v3_0_ip_block =
{
	.type = AMD_IP_BLOCK_TYPE_VCN,
	.major = 3,
	.minor = 0,
	.rev = 0,
	.funcs = &vcn_v3_0_ip_funcs,
};
