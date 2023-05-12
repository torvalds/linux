/*
 * Copyright 2021 Advanced Micro Devices, Inc.
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
#include "amdgpu_cs.h"
#include "soc15.h"
#include "soc15d.h"
#include "soc15_hw_ip.h"
#include "vcn_v2_0.h"
#include "mmsch_v4_0.h"
#include "vcn_v4_0.h"

#include "vcn/vcn_4_0_0_offset.h"
#include "vcn/vcn_4_0_0_sh_mask.h"
#include "ivsrcid/vcn/irqsrcs_vcn_4_0.h"

#include <drm/drm_drv.h>

#define mmUVD_DPG_LMA_CTL							regUVD_DPG_LMA_CTL
#define mmUVD_DPG_LMA_CTL_BASE_IDX						regUVD_DPG_LMA_CTL_BASE_IDX
#define mmUVD_DPG_LMA_DATA							regUVD_DPG_LMA_DATA
#define mmUVD_DPG_LMA_DATA_BASE_IDX						regUVD_DPG_LMA_DATA_BASE_IDX

#define VCN_VID_SOC_ADDRESS_2_0							0x1fb00
#define VCN1_VID_SOC_ADDRESS_3_0						0x48300

#define VCN_HARVEST_MMSCH								0

#define RDECODE_MSG_CREATE							0x00000000
#define RDECODE_MESSAGE_CREATE							0x00000001

static int amdgpu_ih_clientid_vcns[] = {
	SOC15_IH_CLIENTID_VCN,
	SOC15_IH_CLIENTID_VCN1
};

static int vcn_v4_0_start_sriov(struct amdgpu_device *adev);
static void vcn_v4_0_set_unified_ring_funcs(struct amdgpu_device *adev);
static void vcn_v4_0_set_irq_funcs(struct amdgpu_device *adev);
static int vcn_v4_0_set_powergating_state(void *handle,
        enum amd_powergating_state state);
static int vcn_v4_0_pause_dpg_mode(struct amdgpu_device *adev,
        int inst_idx, struct dpg_pause_state *new_state);
static void vcn_v4_0_unified_ring_set_wptr(struct amdgpu_ring *ring);
static void vcn_v4_0_set_ras_funcs(struct amdgpu_device *adev);

/**
 * vcn_v4_0_early_init - set function pointers and load microcode
 *
 * @handle: amdgpu_device pointer
 *
 * Set ring and irq function pointers
 * Load microcode from filesystem
 */
static int vcn_v4_0_early_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int i;

	if (amdgpu_sriov_vf(adev)) {
		adev->vcn.harvest_config = VCN_HARVEST_MMSCH;
		for (i = 0; i < adev->vcn.num_vcn_inst; ++i) {
			if (amdgpu_vcn_is_disabled_vcn(adev, VCN_ENCODE_RING, i)) {
				adev->vcn.harvest_config |= 1 << i;
				dev_info(adev->dev, "VCN%d is disabled by hypervisor\n", i);
			}
		}
	}

	/* re-use enc ring as unified ring */
	adev->vcn.num_enc_rings = 1;

	vcn_v4_0_set_unified_ring_funcs(adev);
	vcn_v4_0_set_irq_funcs(adev);
	vcn_v4_0_set_ras_funcs(adev);

	return amdgpu_vcn_early_init(adev);
}

/**
 * vcn_v4_0_sw_init - sw init for VCN block
 *
 * @handle: amdgpu_device pointer
 *
 * Load firmware and sw initialization
 */
static int vcn_v4_0_sw_init(void *handle)
{
	struct amdgpu_ring *ring;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int i, r;

	r = amdgpu_vcn_sw_init(adev);
	if (r)
		return r;

	amdgpu_vcn_setup_ucode(adev);

	r = amdgpu_vcn_resume(adev);
	if (r)
		return r;

	for (i = 0; i < adev->vcn.num_vcn_inst; i++) {
		volatile struct amdgpu_vcn4_fw_shared *fw_shared;

		if (adev->vcn.harvest_config & (1 << i))
			continue;

		atomic_set(&adev->vcn.inst[i].sched_score, 0);

		/* VCN UNIFIED TRAP */
		r = amdgpu_irq_add_id(adev, amdgpu_ih_clientid_vcns[i],
				VCN_4_0__SRCID__UVD_ENC_GENERAL_PURPOSE, &adev->vcn.inst[i].irq);
		if (r)
			return r;

		/* VCN POISON TRAP */
		r = amdgpu_irq_add_id(adev, amdgpu_ih_clientid_vcns[i],
				VCN_4_0__SRCID_UVD_POISON, &adev->vcn.inst[i].irq);
		if (r)
			return r;

		ring = &adev->vcn.inst[i].ring_enc[0];
		ring->use_doorbell = true;
		if (amdgpu_sriov_vf(adev))
			ring->doorbell_index = (adev->doorbell_index.vcn.vcn_ring0_1 << 1) + i * (adev->vcn.num_enc_rings + 1) + 1;
		else
			ring->doorbell_index = (adev->doorbell_index.vcn.vcn_ring0_1 << 1) + 2 + 8 * i;
		ring->vm_hub = AMDGPU_MMHUB_0;
		sprintf(ring->name, "vcn_unified_%d", i);

		r = amdgpu_ring_init(adev, ring, 512, &adev->vcn.inst[i].irq, 0,
						AMDGPU_RING_PRIO_0, &adev->vcn.inst[i].sched_score);
		if (r)
			return r;

		fw_shared = adev->vcn.inst[i].fw_shared.cpu_addr;
		fw_shared->present_flag_0 = cpu_to_le32(AMDGPU_FW_SHARED_FLAG_0_UNIFIED_QUEUE);
		fw_shared->sq.is_enabled = 1;

		fw_shared->present_flag_0 |= cpu_to_le32(AMDGPU_VCN_SMU_DPM_INTERFACE_FLAG);
		fw_shared->smu_dpm_interface.smu_interface_type = (adev->flags & AMD_IS_APU) ?
			AMDGPU_VCN_SMU_DPM_INTERFACE_APU : AMDGPU_VCN_SMU_DPM_INTERFACE_DGPU;

		if (amdgpu_sriov_vf(adev))
			fw_shared->present_flag_0 |= cpu_to_le32(AMDGPU_VCN_VF_RB_SETUP_FLAG);

		if (amdgpu_vcnfw_log)
			amdgpu_vcn_fwlog_init(&adev->vcn.inst[i]);
	}

	if (amdgpu_sriov_vf(adev)) {
		r = amdgpu_virt_alloc_mm_table(adev);
		if (r)
			return r;
	}

	if (adev->pg_flags & AMD_PG_SUPPORT_VCN_DPG)
		adev->vcn.pause_dpg_mode = vcn_v4_0_pause_dpg_mode;

	r = amdgpu_vcn_ras_sw_init(adev);
	if (r)
		return r;

	return 0;
}

/**
 * vcn_v4_0_sw_fini - sw fini for VCN block
 *
 * @handle: amdgpu_device pointer
 *
 * VCN suspend and free up sw allocation
 */
static int vcn_v4_0_sw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int i, r, idx;

	if (drm_dev_enter(adev_to_drm(adev), &idx)) {
		for (i = 0; i < adev->vcn.num_vcn_inst; i++) {
			volatile struct amdgpu_vcn4_fw_shared *fw_shared;

			if (adev->vcn.harvest_config & (1 << i))
				continue;

			fw_shared = adev->vcn.inst[i].fw_shared.cpu_addr;
			fw_shared->present_flag_0 = 0;
			fw_shared->sq.is_enabled = 0;
		}

		drm_dev_exit(idx);
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
 * vcn_v4_0_hw_init - start and test VCN block
 *
 * @handle: amdgpu_device pointer
 *
 * Initialize the hardware, boot up the VCPU and do some testing
 */
static int vcn_v4_0_hw_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct amdgpu_ring *ring;
	int i, r;

	if (amdgpu_sriov_vf(adev)) {
		r = vcn_v4_0_start_sriov(adev);
		if (r)
			goto done;

		for (i = 0; i < adev->vcn.num_vcn_inst; ++i) {
			if (adev->vcn.harvest_config & (1 << i))
				continue;

			ring = &adev->vcn.inst[i].ring_enc[0];
			ring->wptr = 0;
			ring->wptr_old = 0;
			vcn_v4_0_unified_ring_set_wptr(ring);
			ring->sched.ready = true;

		}
	} else {
		for (i = 0; i < adev->vcn.num_vcn_inst; ++i) {
			if (adev->vcn.harvest_config & (1 << i))
				continue;

			ring = &adev->vcn.inst[i].ring_enc[0];

			adev->nbio.funcs->vcn_doorbell_range(adev, ring->use_doorbell,
					((adev->doorbell_index.vcn.vcn_ring0_1 << 1) + 8 * i), i);

			r = amdgpu_ring_test_helper(ring);
			if (r)
				goto done;

		}
	}

done:
	if (!r)
		DRM_INFO("VCN decode and encode initialized successfully(under %s).\n",
			(adev->pg_flags & AMD_PG_SUPPORT_VCN_DPG)?"DPG Mode":"SPG Mode");

	return r;
}

/**
 * vcn_v4_0_hw_fini - stop the hardware block
 *
 * @handle: amdgpu_device pointer
 *
 * Stop the VCN block, mark ring as not ready any more
 */
static int vcn_v4_0_hw_fini(void *handle)
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
                                RREG32_SOC15(VCN, i, regUVD_STATUS))) {
                        vcn_v4_0_set_powergating_state(adev, AMD_PG_STATE_GATE);
			}
		}

		amdgpu_irq_put(adev, &adev->vcn.inst[i].irq, 0);
	}

	return 0;
}

/**
 * vcn_v4_0_suspend - suspend VCN block
 *
 * @handle: amdgpu_device pointer
 *
 * HW fini and suspend VCN block
 */
static int vcn_v4_0_suspend(void *handle)
{
	int r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	r = vcn_v4_0_hw_fini(adev);
	if (r)
		return r;

	r = amdgpu_vcn_suspend(adev);

	return r;
}

/**
 * vcn_v4_0_resume - resume VCN block
 *
 * @handle: amdgpu_device pointer
 *
 * Resume firmware and hw init VCN block
 */
static int vcn_v4_0_resume(void *handle)
{
	int r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	r = amdgpu_vcn_resume(adev);
	if (r)
		return r;

	r = vcn_v4_0_hw_init(adev);

	return r;
}

/**
 * vcn_v4_0_mc_resume - memory controller programming
 *
 * @adev: amdgpu_device pointer
 * @inst: instance number
 *
 * Let the VCN memory controller know it's offsets
 */
static void vcn_v4_0_mc_resume(struct amdgpu_device *adev, int inst)
{
	uint32_t offset, size;
	const struct common_firmware_header *hdr;

	hdr = (const struct common_firmware_header *)adev->vcn.fw->data;
	size = AMDGPU_GPU_PAGE_ALIGN(le32_to_cpu(hdr->ucode_size_bytes) + 8);

	/* cache window 0: fw */
	if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) {
		WREG32_SOC15(VCN, inst, regUVD_LMI_VCPU_CACHE_64BIT_BAR_LOW,
			(adev->firmware.ucode[AMDGPU_UCODE_ID_VCN + inst].tmr_mc_addr_lo));
		WREG32_SOC15(VCN, inst, regUVD_LMI_VCPU_CACHE_64BIT_BAR_HIGH,
			(adev->firmware.ucode[AMDGPU_UCODE_ID_VCN + inst].tmr_mc_addr_hi));
		WREG32_SOC15(VCN, inst, regUVD_VCPU_CACHE_OFFSET0, 0);
		offset = 0;
	} else {
		WREG32_SOC15(VCN, inst, regUVD_LMI_VCPU_CACHE_64BIT_BAR_LOW,
			lower_32_bits(adev->vcn.inst[inst].gpu_addr));
		WREG32_SOC15(VCN, inst, regUVD_LMI_VCPU_CACHE_64BIT_BAR_HIGH,
			upper_32_bits(adev->vcn.inst[inst].gpu_addr));
		offset = size;
                WREG32_SOC15(VCN, inst, regUVD_VCPU_CACHE_OFFSET0, AMDGPU_UVD_FIRMWARE_OFFSET >> 3);
	}
	WREG32_SOC15(VCN, inst, regUVD_VCPU_CACHE_SIZE0, size);

	/* cache window 1: stack */
	WREG32_SOC15(VCN, inst, regUVD_LMI_VCPU_CACHE1_64BIT_BAR_LOW,
		lower_32_bits(adev->vcn.inst[inst].gpu_addr + offset));
	WREG32_SOC15(VCN, inst, regUVD_LMI_VCPU_CACHE1_64BIT_BAR_HIGH,
		upper_32_bits(adev->vcn.inst[inst].gpu_addr + offset));
	WREG32_SOC15(VCN, inst, regUVD_VCPU_CACHE_OFFSET1, 0);
	WREG32_SOC15(VCN, inst, regUVD_VCPU_CACHE_SIZE1, AMDGPU_VCN_STACK_SIZE);

	/* cache window 2: context */
	WREG32_SOC15(VCN, inst, regUVD_LMI_VCPU_CACHE2_64BIT_BAR_LOW,
		lower_32_bits(adev->vcn.inst[inst].gpu_addr + offset + AMDGPU_VCN_STACK_SIZE));
	WREG32_SOC15(VCN, inst, regUVD_LMI_VCPU_CACHE2_64BIT_BAR_HIGH,
		upper_32_bits(adev->vcn.inst[inst].gpu_addr + offset + AMDGPU_VCN_STACK_SIZE));
	WREG32_SOC15(VCN, inst, regUVD_VCPU_CACHE_OFFSET2, 0);
	WREG32_SOC15(VCN, inst, regUVD_VCPU_CACHE_SIZE2, AMDGPU_VCN_CONTEXT_SIZE);

	/* non-cache window */
	WREG32_SOC15(VCN, inst, regUVD_LMI_VCPU_NC0_64BIT_BAR_LOW,
		lower_32_bits(adev->vcn.inst[inst].fw_shared.gpu_addr));
	WREG32_SOC15(VCN, inst, regUVD_LMI_VCPU_NC0_64BIT_BAR_HIGH,
		upper_32_bits(adev->vcn.inst[inst].fw_shared.gpu_addr));
	WREG32_SOC15(VCN, inst, regUVD_VCPU_NONCACHE_OFFSET0, 0);
	WREG32_SOC15(VCN, inst, regUVD_VCPU_NONCACHE_SIZE0,
		AMDGPU_GPU_PAGE_ALIGN(sizeof(struct amdgpu_vcn4_fw_shared)));
}

/**
 * vcn_v4_0_mc_resume_dpg_mode - memory controller programming for dpg mode
 *
 * @adev: amdgpu_device pointer
 * @inst_idx: instance number index
 * @indirect: indirectly write sram
 *
 * Let the VCN memory controller know it's offsets with dpg mode
 */
static void vcn_v4_0_mc_resume_dpg_mode(struct amdgpu_device *adev, int inst_idx, bool indirect)
{
	uint32_t offset, size;
	const struct common_firmware_header *hdr;
	hdr = (const struct common_firmware_header *)adev->vcn.fw->data;
	size = AMDGPU_GPU_PAGE_ALIGN(le32_to_cpu(hdr->ucode_size_bytes) + 8);

	/* cache window 0: fw */
	if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) {
		if (!indirect) {
			WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
				VCN, inst_idx, regUVD_LMI_VCPU_CACHE_64BIT_BAR_LOW),
				(adev->firmware.ucode[AMDGPU_UCODE_ID_VCN + inst_idx].tmr_mc_addr_lo), 0, indirect);
			WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
				VCN, inst_idx, regUVD_LMI_VCPU_CACHE_64BIT_BAR_HIGH),
				(adev->firmware.ucode[AMDGPU_UCODE_ID_VCN + inst_idx].tmr_mc_addr_hi), 0, indirect);
			WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
				VCN, inst_idx, regUVD_VCPU_CACHE_OFFSET0), 0, 0, indirect);
		} else {
			WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
				VCN, inst_idx, regUVD_LMI_VCPU_CACHE_64BIT_BAR_LOW), 0, 0, indirect);
			WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
				VCN, inst_idx, regUVD_LMI_VCPU_CACHE_64BIT_BAR_HIGH), 0, 0, indirect);
			WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
				VCN, inst_idx, regUVD_VCPU_CACHE_OFFSET0), 0, 0, indirect);
		}
		offset = 0;
	} else {
		WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
			VCN, inst_idx, regUVD_LMI_VCPU_CACHE_64BIT_BAR_LOW),
			lower_32_bits(adev->vcn.inst[inst_idx].gpu_addr), 0, indirect);
		WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
			VCN, inst_idx, regUVD_LMI_VCPU_CACHE_64BIT_BAR_HIGH),
			upper_32_bits(adev->vcn.inst[inst_idx].gpu_addr), 0, indirect);
		offset = size;
		WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
			VCN, inst_idx, regUVD_VCPU_CACHE_OFFSET0),
			AMDGPU_UVD_FIRMWARE_OFFSET >> 3, 0, indirect);
	}

	if (!indirect)
		WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
			VCN, inst_idx, regUVD_VCPU_CACHE_SIZE0), size, 0, indirect);
	else
		WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
			VCN, inst_idx, regUVD_VCPU_CACHE_SIZE0), 0, 0, indirect);

	/* cache window 1: stack */
	if (!indirect) {
		WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
			VCN, inst_idx, regUVD_LMI_VCPU_CACHE1_64BIT_BAR_LOW),
			lower_32_bits(adev->vcn.inst[inst_idx].gpu_addr + offset), 0, indirect);
		WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
			VCN, inst_idx, regUVD_LMI_VCPU_CACHE1_64BIT_BAR_HIGH),
			upper_32_bits(adev->vcn.inst[inst_idx].gpu_addr + offset), 0, indirect);
		WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
			VCN, inst_idx, regUVD_VCPU_CACHE_OFFSET1), 0, 0, indirect);
	} else {
		WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
			VCN, inst_idx, regUVD_LMI_VCPU_CACHE1_64BIT_BAR_LOW), 0, 0, indirect);
		WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
			VCN, inst_idx, regUVD_LMI_VCPU_CACHE1_64BIT_BAR_HIGH), 0, 0, indirect);
		WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
			VCN, inst_idx, regUVD_VCPU_CACHE_OFFSET1), 0, 0, indirect);
	}
	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
			VCN, inst_idx, regUVD_VCPU_CACHE_SIZE1), AMDGPU_VCN_STACK_SIZE, 0, indirect);

	/* cache window 2: context */
	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
			VCN, inst_idx, regUVD_LMI_VCPU_CACHE2_64BIT_BAR_LOW),
			lower_32_bits(adev->vcn.inst[inst_idx].gpu_addr + offset + AMDGPU_VCN_STACK_SIZE), 0, indirect);
	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
			VCN, inst_idx, regUVD_LMI_VCPU_CACHE2_64BIT_BAR_HIGH),
			upper_32_bits(adev->vcn.inst[inst_idx].gpu_addr + offset + AMDGPU_VCN_STACK_SIZE), 0, indirect);
	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
			VCN, inst_idx, regUVD_VCPU_CACHE_OFFSET2), 0, 0, indirect);
	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
			VCN, inst_idx, regUVD_VCPU_CACHE_SIZE2), AMDGPU_VCN_CONTEXT_SIZE, 0, indirect);

	/* non-cache window */
	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
			VCN, inst_idx, regUVD_LMI_VCPU_NC0_64BIT_BAR_LOW),
			lower_32_bits(adev->vcn.inst[inst_idx].fw_shared.gpu_addr), 0, indirect);
	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
			VCN, inst_idx, regUVD_LMI_VCPU_NC0_64BIT_BAR_HIGH),
			upper_32_bits(adev->vcn.inst[inst_idx].fw_shared.gpu_addr), 0, indirect);
	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
			VCN, inst_idx, regUVD_VCPU_NONCACHE_OFFSET0), 0, 0, indirect);
	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
			VCN, inst_idx, regUVD_VCPU_NONCACHE_SIZE0),
			AMDGPU_GPU_PAGE_ALIGN(sizeof(struct amdgpu_vcn4_fw_shared)), 0, indirect);

	/* VCN global tiling registers */
	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
		VCN, 0, regUVD_GFX10_ADDR_CONFIG), adev->gfx.config.gb_addr_config, 0, indirect);
}

/**
 * vcn_v4_0_disable_static_power_gating - disable VCN static power gating
 *
 * @adev: amdgpu_device pointer
 * @inst: instance number
 *
 * Disable static power gating for VCN block
 */
static void vcn_v4_0_disable_static_power_gating(struct amdgpu_device *adev, int inst)
{
	uint32_t data = 0;

	if (adev->pg_flags & AMD_PG_SUPPORT_VCN) {
		data = (1 << UVD_PGFSM_CONFIG__UVDM_PWR_CONFIG__SHIFT
			| 1 << UVD_PGFSM_CONFIG__UVDS_PWR_CONFIG__SHIFT
			| 1 << UVD_PGFSM_CONFIG__UVDLM_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDF_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDTC_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDB_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDTA_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDTD_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDTE_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDE_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDAB_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDTB_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDNA_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDNB_PWR_CONFIG__SHIFT);

		WREG32_SOC15(VCN, inst, regUVD_PGFSM_CONFIG, data);
		SOC15_WAIT_ON_RREG(VCN, inst, regUVD_PGFSM_STATUS,
			UVD_PGFSM_STATUS__UVDM_UVDU_UVDLM_PWR_ON_3_0, 0x3F3FFFFF);
	} else {
		uint32_t value;

		value = (inst) ? 0x2200800 : 0;
		data = (1 << UVD_PGFSM_CONFIG__UVDM_PWR_CONFIG__SHIFT
			| 1 << UVD_PGFSM_CONFIG__UVDS_PWR_CONFIG__SHIFT
			| 1 << UVD_PGFSM_CONFIG__UVDLM_PWR_CONFIG__SHIFT
			| 1 << UVD_PGFSM_CONFIG__UVDF_PWR_CONFIG__SHIFT
			| 1 << UVD_PGFSM_CONFIG__UVDTC_PWR_CONFIG__SHIFT
			| 1 << UVD_PGFSM_CONFIG__UVDB_PWR_CONFIG__SHIFT
			| 1 << UVD_PGFSM_CONFIG__UVDTA_PWR_CONFIG__SHIFT
			| 1 << UVD_PGFSM_CONFIG__UVDTD_PWR_CONFIG__SHIFT
			| 1 << UVD_PGFSM_CONFIG__UVDTE_PWR_CONFIG__SHIFT
			| 1 << UVD_PGFSM_CONFIG__UVDE_PWR_CONFIG__SHIFT
			| 1 << UVD_PGFSM_CONFIG__UVDAB_PWR_CONFIG__SHIFT
			| 1 << UVD_PGFSM_CONFIG__UVDTB_PWR_CONFIG__SHIFT
			| 1 << UVD_PGFSM_CONFIG__UVDNA_PWR_CONFIG__SHIFT
			| 1 << UVD_PGFSM_CONFIG__UVDNB_PWR_CONFIG__SHIFT);

                WREG32_SOC15(VCN, inst, regUVD_PGFSM_CONFIG, data);
                SOC15_WAIT_ON_RREG(VCN, inst, regUVD_PGFSM_STATUS, value,  0x3F3FFFFF);
        }

        data = RREG32_SOC15(VCN, inst, regUVD_POWER_STATUS);
        data &= ~0x103;
        if (adev->pg_flags & AMD_PG_SUPPORT_VCN)
                data |= UVD_PGFSM_CONFIG__UVDM_UVDU_PWR_ON |
                        UVD_POWER_STATUS__UVD_PG_EN_MASK;

        WREG32_SOC15(VCN, inst, regUVD_POWER_STATUS, data);

        return;
}

/**
 * vcn_v4_0_enable_static_power_gating - enable VCN static power gating
 *
 * @adev: amdgpu_device pointer
 * @inst: instance number
 *
 * Enable static power gating for VCN block
 */
static void vcn_v4_0_enable_static_power_gating(struct amdgpu_device *adev, int inst)
{
	uint32_t data;

	if (adev->pg_flags & AMD_PG_SUPPORT_VCN) {
		/* Before power off, this indicator has to be turned on */
		data = RREG32_SOC15(VCN, inst, regUVD_POWER_STATUS);
		data &= ~UVD_POWER_STATUS__UVD_POWER_STATUS_MASK;
		data |= UVD_POWER_STATUS__UVD_POWER_STATUS_TILES_OFF;
		WREG32_SOC15(VCN, inst, regUVD_POWER_STATUS, data);

		data = (2 << UVD_PGFSM_CONFIG__UVDM_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDS_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDF_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDTC_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDB_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDTA_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDLM_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDTD_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDTE_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDE_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDAB_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDTB_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDNA_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDNB_PWR_CONFIG__SHIFT);
		WREG32_SOC15(VCN, inst, regUVD_PGFSM_CONFIG, data);

		data = (2 << UVD_PGFSM_STATUS__UVDM_PWR_STATUS__SHIFT
			| 2 << UVD_PGFSM_STATUS__UVDS_PWR_STATUS__SHIFT
			| 2 << UVD_PGFSM_STATUS__UVDF_PWR_STATUS__SHIFT
			| 2 << UVD_PGFSM_STATUS__UVDTC_PWR_STATUS__SHIFT
			| 2 << UVD_PGFSM_STATUS__UVDB_PWR_STATUS__SHIFT
			| 2 << UVD_PGFSM_STATUS__UVDTA_PWR_STATUS__SHIFT
			| 2 << UVD_PGFSM_STATUS__UVDLM_PWR_STATUS__SHIFT
			| 2 << UVD_PGFSM_STATUS__UVDTD_PWR_STATUS__SHIFT
			| 2 << UVD_PGFSM_STATUS__UVDTE_PWR_STATUS__SHIFT
			| 2 << UVD_PGFSM_STATUS__UVDE_PWR_STATUS__SHIFT
			| 2 << UVD_PGFSM_STATUS__UVDAB_PWR_STATUS__SHIFT
			| 2 << UVD_PGFSM_STATUS__UVDTB_PWR_STATUS__SHIFT
			| 2 << UVD_PGFSM_STATUS__UVDNA_PWR_STATUS__SHIFT
			| 2 << UVD_PGFSM_STATUS__UVDNB_PWR_STATUS__SHIFT);
		SOC15_WAIT_ON_RREG(VCN, inst, regUVD_PGFSM_STATUS, data, 0x3F3FFFFF);
	}

        return;
}

/**
 * vcn_v4_0_disable_clock_gating - disable VCN clock gating
 *
 * @adev: amdgpu_device pointer
 * @inst: instance number
 *
 * Disable clock gating for VCN block
 */
static void vcn_v4_0_disable_clock_gating(struct amdgpu_device *adev, int inst)
{
	uint32_t data;

	if (adev->cg_flags & AMD_CG_SUPPORT_VCN_MGCG)
		return;

	/* VCN disable CGC */
	data = RREG32_SOC15(VCN, inst, regUVD_CGC_CTRL);
	data &= ~UVD_CGC_CTRL__DYN_CLOCK_MODE_MASK;
	data |= 1 << UVD_CGC_CTRL__CLK_GATE_DLY_TIMER__SHIFT;
	data |= 4 << UVD_CGC_CTRL__CLK_OFF_DELAY__SHIFT;
	WREG32_SOC15(VCN, inst, regUVD_CGC_CTRL, data);

	data = RREG32_SOC15(VCN, inst, regUVD_CGC_GATE);
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

	WREG32_SOC15(VCN, inst, regUVD_CGC_GATE, data);
	SOC15_WAIT_ON_RREG(VCN, inst, regUVD_CGC_GATE, 0,  0xFFFFFFFF);

	data = RREG32_SOC15(VCN, inst, regUVD_CGC_CTRL);
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
	WREG32_SOC15(VCN, inst, regUVD_CGC_CTRL, data);

	data = RREG32_SOC15(VCN, inst, regUVD_SUVD_CGC_GATE);
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
		| UVD_SUVD_CGC_GATE__UVD_SC_MASK
		| UVD_SUVD_CGC_GATE__ENT_MASK
		| UVD_SUVD_CGC_GATE__SIT_HEVC_DEC_MASK
		| UVD_SUVD_CGC_GATE__SIT_HEVC_ENC_MASK
		| UVD_SUVD_CGC_GATE__SITE_MASK
		| UVD_SUVD_CGC_GATE__SRE_VP9_MASK
		| UVD_SUVD_CGC_GATE__SCM_VP9_MASK
		| UVD_SUVD_CGC_GATE__SIT_VP9_DEC_MASK
		| UVD_SUVD_CGC_GATE__SDB_VP9_MASK
		| UVD_SUVD_CGC_GATE__IME_HEVC_MASK);
	WREG32_SOC15(VCN, inst, regUVD_SUVD_CGC_GATE, data);

	data = RREG32_SOC15(VCN, inst, regUVD_SUVD_CGC_CTRL);
	data &= ~(UVD_SUVD_CGC_CTRL__SRE_MODE_MASK
		| UVD_SUVD_CGC_CTRL__SIT_MODE_MASK
		| UVD_SUVD_CGC_CTRL__SMP_MODE_MASK
		| UVD_SUVD_CGC_CTRL__SCM_MODE_MASK
		| UVD_SUVD_CGC_CTRL__SDB_MODE_MASK
		| UVD_SUVD_CGC_CTRL__SCLR_MODE_MASK
		| UVD_SUVD_CGC_CTRL__UVD_SC_MODE_MASK
		| UVD_SUVD_CGC_CTRL__ENT_MODE_MASK
		| UVD_SUVD_CGC_CTRL__IME_MODE_MASK
		| UVD_SUVD_CGC_CTRL__SITE_MODE_MASK);
	WREG32_SOC15(VCN, inst, regUVD_SUVD_CGC_CTRL, data);
}

/**
 * vcn_v4_0_disable_clock_gating_dpg_mode - disable VCN clock gating dpg mode
 *
 * @adev: amdgpu_device pointer
 * @sram_sel: sram select
 * @inst_idx: instance number index
 * @indirect: indirectly write sram
 *
 * Disable clock gating for VCN block with dpg mode
 */
static void vcn_v4_0_disable_clock_gating_dpg_mode(struct amdgpu_device *adev, uint8_t sram_sel,
      int inst_idx, uint8_t indirect)
{
	uint32_t reg_data = 0;

	if (adev->cg_flags & AMD_CG_SUPPORT_VCN_MGCG)
		return;

	/* enable sw clock gating control */
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
		 UVD_CGC_CTRL__VCPU_MODE_MASK);
	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
		VCN, inst_idx, regUVD_CGC_CTRL), reg_data, sram_sel, indirect);

	/* turn off clock gating */
	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
		VCN, inst_idx, regUVD_CGC_GATE), 0, sram_sel, indirect);

	/* turn on SUVD clock gating */
	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
		VCN, inst_idx, regUVD_SUVD_CGC_GATE), 1, sram_sel, indirect);

	/* turn on sw mode in UVD_SUVD_CGC_CTRL */
	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
		VCN, inst_idx, regUVD_SUVD_CGC_CTRL), 0, sram_sel, indirect);
}

/**
 * vcn_v4_0_enable_clock_gating - enable VCN clock gating
 *
 * @adev: amdgpu_device pointer
 * @inst: instance number
 *
 * Enable clock gating for VCN block
 */
static void vcn_v4_0_enable_clock_gating(struct amdgpu_device *adev, int inst)
{
	uint32_t data;

	if (adev->cg_flags & AMD_CG_SUPPORT_VCN_MGCG)
		return;

	/* enable VCN CGC */
	data = RREG32_SOC15(VCN, inst, regUVD_CGC_CTRL);
	data |= 0 << UVD_CGC_CTRL__DYN_CLOCK_MODE__SHIFT;
	data |= 1 << UVD_CGC_CTRL__CLK_GATE_DLY_TIMER__SHIFT;
	data |= 4 << UVD_CGC_CTRL__CLK_OFF_DELAY__SHIFT;
	WREG32_SOC15(VCN, inst, regUVD_CGC_CTRL, data);

	data = RREG32_SOC15(VCN, inst, regUVD_CGC_CTRL);
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
	WREG32_SOC15(VCN, inst, regUVD_CGC_CTRL, data);

	data = RREG32_SOC15(VCN, inst, regUVD_SUVD_CGC_CTRL);
	data |= (UVD_SUVD_CGC_CTRL__SRE_MODE_MASK
		| UVD_SUVD_CGC_CTRL__SIT_MODE_MASK
		| UVD_SUVD_CGC_CTRL__SMP_MODE_MASK
		| UVD_SUVD_CGC_CTRL__SCM_MODE_MASK
		| UVD_SUVD_CGC_CTRL__SDB_MODE_MASK
		| UVD_SUVD_CGC_CTRL__SCLR_MODE_MASK
		| UVD_SUVD_CGC_CTRL__UVD_SC_MODE_MASK
		| UVD_SUVD_CGC_CTRL__ENT_MODE_MASK
		| UVD_SUVD_CGC_CTRL__IME_MODE_MASK
		| UVD_SUVD_CGC_CTRL__SITE_MODE_MASK);
	WREG32_SOC15(VCN, inst, regUVD_SUVD_CGC_CTRL, data);

	return;
}

static void vcn_v4_0_enable_ras(struct amdgpu_device *adev, int inst_idx,
				bool indirect)
{
	uint32_t tmp;

	if (!amdgpu_ras_is_supported(adev, AMDGPU_RAS_BLOCK__VCN))
		return;

	tmp = VCN_RAS_CNTL__VCPU_VCODEC_REARM_MASK |
	      VCN_RAS_CNTL__VCPU_VCODEC_IH_EN_MASK |
	      VCN_RAS_CNTL__VCPU_VCODEC_PMI_EN_MASK |
	      VCN_RAS_CNTL__VCPU_VCODEC_STALL_EN_MASK;
	WREG32_SOC15_DPG_MODE(inst_idx,
			      SOC15_DPG_MODE_OFFSET(VCN, 0, regVCN_RAS_CNTL),
			      tmp, 0, indirect);

	tmp = UVD_SYS_INT_EN__RASCNTL_VCPU_VCODEC_EN_MASK;
	WREG32_SOC15_DPG_MODE(inst_idx,
			      SOC15_DPG_MODE_OFFSET(VCN, 0, regUVD_SYS_INT_EN),
			      tmp, 0, indirect);
}

/**
 * vcn_v4_0_start_dpg_mode - VCN start with dpg mode
 *
 * @adev: amdgpu_device pointer
 * @inst_idx: instance number index
 * @indirect: indirectly write sram
 *
 * Start VCN block with dpg mode
 */
static int vcn_v4_0_start_dpg_mode(struct amdgpu_device *adev, int inst_idx, bool indirect)
{
	volatile struct amdgpu_vcn4_fw_shared *fw_shared = adev->vcn.inst[inst_idx].fw_shared.cpu_addr;
	struct amdgpu_ring *ring;
	uint32_t tmp;

	/* disable register anti-hang mechanism */
	WREG32_P(SOC15_REG_OFFSET(VCN, inst_idx, regUVD_POWER_STATUS), 1,
		~UVD_POWER_STATUS__UVD_POWER_STATUS_MASK);
	/* enable dynamic power gating mode */
	tmp = RREG32_SOC15(VCN, inst_idx, regUVD_POWER_STATUS);
	tmp |= UVD_POWER_STATUS__UVD_PG_MODE_MASK;
	tmp |= UVD_POWER_STATUS__UVD_PG_EN_MASK;
	WREG32_SOC15(VCN, inst_idx, regUVD_POWER_STATUS, tmp);

	if (indirect)
		adev->vcn.inst[inst_idx].dpg_sram_curr_addr = (uint32_t *)adev->vcn.inst[inst_idx].dpg_sram_cpu_addr;

	/* enable clock gating */
	vcn_v4_0_disable_clock_gating_dpg_mode(adev, 0, inst_idx, indirect);

	/* enable VCPU clock */
	tmp = (0xFF << UVD_VCPU_CNTL__PRB_TIMEOUT_VAL__SHIFT);
	tmp |= UVD_VCPU_CNTL__CLK_EN_MASK | UVD_VCPU_CNTL__BLK_RST_MASK;
	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
		VCN, inst_idx, regUVD_VCPU_CNTL), tmp, 0, indirect);

	/* disable master interupt */
	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
		VCN, inst_idx, regUVD_MASTINT_EN), 0, 0, indirect);

	/* setup regUVD_LMI_CTRL */
	tmp = (UVD_LMI_CTRL__WRITE_CLEAN_TIMER_EN_MASK |
		UVD_LMI_CTRL__REQ_MODE_MASK |
		UVD_LMI_CTRL__CRC_RESET_MASK |
		UVD_LMI_CTRL__MASK_MC_URGENT_MASK |
		UVD_LMI_CTRL__DATA_COHERENCY_EN_MASK |
		UVD_LMI_CTRL__VCPU_DATA_COHERENCY_EN_MASK |
		(8 << UVD_LMI_CTRL__WRITE_CLEAN_TIMER__SHIFT) |
		0x00100000L);
	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
		VCN, inst_idx, regUVD_LMI_CTRL), tmp, 0, indirect);

	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
		VCN, inst_idx, regUVD_MPC_CNTL),
		0x2 << UVD_MPC_CNTL__REPLACEMENT_MODE__SHIFT, 0, indirect);

	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
		VCN, inst_idx, regUVD_MPC_SET_MUXA0),
		((0x1 << UVD_MPC_SET_MUXA0__VARA_1__SHIFT) |
		 (0x2 << UVD_MPC_SET_MUXA0__VARA_2__SHIFT) |
		 (0x3 << UVD_MPC_SET_MUXA0__VARA_3__SHIFT) |
		 (0x4 << UVD_MPC_SET_MUXA0__VARA_4__SHIFT)), 0, indirect);

	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
		VCN, inst_idx, regUVD_MPC_SET_MUXB0),
		 ((0x1 << UVD_MPC_SET_MUXB0__VARB_1__SHIFT) |
		 (0x2 << UVD_MPC_SET_MUXB0__VARB_2__SHIFT) |
		 (0x3 << UVD_MPC_SET_MUXB0__VARB_3__SHIFT) |
		 (0x4 << UVD_MPC_SET_MUXB0__VARB_4__SHIFT)), 0, indirect);

	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
		VCN, inst_idx, regUVD_MPC_SET_MUX),
		((0x0 << UVD_MPC_SET_MUX__SET_0__SHIFT) |
		 (0x1 << UVD_MPC_SET_MUX__SET_1__SHIFT) |
		 (0x2 << UVD_MPC_SET_MUX__SET_2__SHIFT)), 0, indirect);

	vcn_v4_0_mc_resume_dpg_mode(adev, inst_idx, indirect);

	tmp = (0xFF << UVD_VCPU_CNTL__PRB_TIMEOUT_VAL__SHIFT);
	tmp |= UVD_VCPU_CNTL__CLK_EN_MASK;
	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
		VCN, inst_idx, regUVD_VCPU_CNTL), tmp, 0, indirect);

	/* enable LMI MC and UMC channels */
	tmp = 0x1f << UVD_LMI_CTRL2__RE_OFLD_MIF_WR_REQ_NUM__SHIFT;
	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
		VCN, inst_idx, regUVD_LMI_CTRL2), tmp, 0, indirect);

	vcn_v4_0_enable_ras(adev, inst_idx, indirect);

	/* enable master interrupt */
	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
		VCN, inst_idx, regUVD_MASTINT_EN),
		UVD_MASTINT_EN__VCPU_EN_MASK, 0, indirect);


	if (indirect)
		psp_update_vcn_sram(adev, inst_idx, adev->vcn.inst[inst_idx].dpg_sram_gpu_addr,
			(uint32_t)((uintptr_t)adev->vcn.inst[inst_idx].dpg_sram_curr_addr -
				(uintptr_t)adev->vcn.inst[inst_idx].dpg_sram_cpu_addr));

	ring = &adev->vcn.inst[inst_idx].ring_enc[0];

	WREG32_SOC15(VCN, inst_idx, regUVD_RB_BASE_LO, ring->gpu_addr);
	WREG32_SOC15(VCN, inst_idx, regUVD_RB_BASE_HI, upper_32_bits(ring->gpu_addr));
	WREG32_SOC15(VCN, inst_idx, regUVD_RB_SIZE, ring->ring_size / 4);

	tmp = RREG32_SOC15(VCN, inst_idx, regVCN_RB_ENABLE);
	tmp &= ~(VCN_RB_ENABLE__RB1_EN_MASK);
	WREG32_SOC15(VCN, inst_idx, regVCN_RB_ENABLE, tmp);
	fw_shared->sq.queue_mode |= FW_QUEUE_RING_RESET;
	WREG32_SOC15(VCN, inst_idx, regUVD_RB_RPTR, 0);
	WREG32_SOC15(VCN, inst_idx, regUVD_RB_WPTR, 0);

	tmp = RREG32_SOC15(VCN, inst_idx, regUVD_RB_RPTR);
	WREG32_SOC15(VCN, inst_idx, regUVD_RB_WPTR, tmp);
	ring->wptr = RREG32_SOC15(VCN, inst_idx, regUVD_RB_WPTR);

	tmp = RREG32_SOC15(VCN, inst_idx, regVCN_RB_ENABLE);
	tmp |= VCN_RB_ENABLE__RB1_EN_MASK;
	WREG32_SOC15(VCN, inst_idx, regVCN_RB_ENABLE, tmp);
	fw_shared->sq.queue_mode &= ~(FW_QUEUE_RING_RESET | FW_QUEUE_DPG_HOLD_OFF);

	WREG32_SOC15(VCN, inst_idx, regVCN_RB1_DB_CTRL,
			ring->doorbell_index << VCN_RB1_DB_CTRL__OFFSET__SHIFT |
			VCN_RB1_DB_CTRL__EN_MASK);

	return 0;
}


/**
 * vcn_v4_0_start - VCN start
 *
 * @adev: amdgpu_device pointer
 *
 * Start VCN block
 */
static int vcn_v4_0_start(struct amdgpu_device *adev)
{
	volatile struct amdgpu_vcn4_fw_shared *fw_shared;
	struct amdgpu_ring *ring;
	uint32_t tmp;
	int i, j, k, r;

	if (adev->pm.dpm_enabled)
		amdgpu_dpm_enable_uvd(adev, true);

	for (i = 0; i < adev->vcn.num_vcn_inst; ++i) {
		fw_shared = adev->vcn.inst[i].fw_shared.cpu_addr;

		if (adev->pg_flags & AMD_PG_SUPPORT_VCN_DPG) {
			r = vcn_v4_0_start_dpg_mode(adev, i, adev->vcn.indirect_sram);
			continue;
		}

		/* disable VCN power gating */
		vcn_v4_0_disable_static_power_gating(adev, i);

		/* set VCN status busy */
		tmp = RREG32_SOC15(VCN, i, regUVD_STATUS) | UVD_STATUS__UVD_BUSY;
		WREG32_SOC15(VCN, i, regUVD_STATUS, tmp);

		/*SW clock gating */
		vcn_v4_0_disable_clock_gating(adev, i);

		/* enable VCPU clock */
		WREG32_P(SOC15_REG_OFFSET(VCN, i, regUVD_VCPU_CNTL),
				UVD_VCPU_CNTL__CLK_EN_MASK, ~UVD_VCPU_CNTL__CLK_EN_MASK);

		/* disable master interrupt */
		WREG32_P(SOC15_REG_OFFSET(VCN, i, regUVD_MASTINT_EN), 0,
				~UVD_MASTINT_EN__VCPU_EN_MASK);

		/* enable LMI MC and UMC channels */
		WREG32_P(SOC15_REG_OFFSET(VCN, i, regUVD_LMI_CTRL2), 0,
				~UVD_LMI_CTRL2__STALL_ARB_UMC_MASK);

		tmp = RREG32_SOC15(VCN, i, regUVD_SOFT_RESET);
		tmp &= ~UVD_SOFT_RESET__LMI_SOFT_RESET_MASK;
		tmp &= ~UVD_SOFT_RESET__LMI_UMC_SOFT_RESET_MASK;
		WREG32_SOC15(VCN, i, regUVD_SOFT_RESET, tmp);

		/* setup regUVD_LMI_CTRL */
		tmp = RREG32_SOC15(VCN, i, regUVD_LMI_CTRL);
		WREG32_SOC15(VCN, i, regUVD_LMI_CTRL, tmp |
				UVD_LMI_CTRL__WRITE_CLEAN_TIMER_EN_MASK |
				UVD_LMI_CTRL__MASK_MC_URGENT_MASK |
				UVD_LMI_CTRL__DATA_COHERENCY_EN_MASK |
				UVD_LMI_CTRL__VCPU_DATA_COHERENCY_EN_MASK);

		/* setup regUVD_MPC_CNTL */
		tmp = RREG32_SOC15(VCN, i, regUVD_MPC_CNTL);
		tmp &= ~UVD_MPC_CNTL__REPLACEMENT_MODE_MASK;
		tmp |= 0x2 << UVD_MPC_CNTL__REPLACEMENT_MODE__SHIFT;
		WREG32_SOC15(VCN, i, regUVD_MPC_CNTL, tmp);

		/* setup UVD_MPC_SET_MUXA0 */
		WREG32_SOC15(VCN, i, regUVD_MPC_SET_MUXA0,
				((0x1 << UVD_MPC_SET_MUXA0__VARA_1__SHIFT) |
				 (0x2 << UVD_MPC_SET_MUXA0__VARA_2__SHIFT) |
				 (0x3 << UVD_MPC_SET_MUXA0__VARA_3__SHIFT) |
				 (0x4 << UVD_MPC_SET_MUXA0__VARA_4__SHIFT)));

		/* setup UVD_MPC_SET_MUXB0 */
		WREG32_SOC15(VCN, i, regUVD_MPC_SET_MUXB0,
				((0x1 << UVD_MPC_SET_MUXB0__VARB_1__SHIFT) |
				 (0x2 << UVD_MPC_SET_MUXB0__VARB_2__SHIFT) |
				 (0x3 << UVD_MPC_SET_MUXB0__VARB_3__SHIFT) |
				 (0x4 << UVD_MPC_SET_MUXB0__VARB_4__SHIFT)));

		/* setup UVD_MPC_SET_MUX */
		WREG32_SOC15(VCN, i, regUVD_MPC_SET_MUX,
				((0x0 << UVD_MPC_SET_MUX__SET_0__SHIFT) |
				 (0x1 << UVD_MPC_SET_MUX__SET_1__SHIFT) |
				 (0x2 << UVD_MPC_SET_MUX__SET_2__SHIFT)));

		vcn_v4_0_mc_resume(adev, i);

		/* VCN global tiling registers */
		WREG32_SOC15(VCN, i, regUVD_GFX10_ADDR_CONFIG,
				adev->gfx.config.gb_addr_config);

		/* unblock VCPU register access */
		WREG32_P(SOC15_REG_OFFSET(VCN, i, regUVD_RB_ARB_CTRL), 0,
				~UVD_RB_ARB_CTRL__VCPU_DIS_MASK);

		/* release VCPU reset to boot */
		WREG32_P(SOC15_REG_OFFSET(VCN, i, regUVD_VCPU_CNTL), 0,
				~UVD_VCPU_CNTL__BLK_RST_MASK);

		for (j = 0; j < 10; ++j) {
			uint32_t status;

			for (k = 0; k < 100; ++k) {
				status = RREG32_SOC15(VCN, i, regUVD_STATUS);
				if (status & 2)
					break;
				mdelay(10);
				if (amdgpu_emu_mode==1)
					msleep(1);
			}

			if (amdgpu_emu_mode==1) {
				r = -1;
				if (status & 2) {
					r = 0;
					break;
				}
			} else {
				r = 0;
				if (status & 2)
					break;

				dev_err(adev->dev, "VCN[%d] is not responding, trying to reset the VCPU!!!\n", i);
				WREG32_P(SOC15_REG_OFFSET(VCN, i, regUVD_VCPU_CNTL),
							UVD_VCPU_CNTL__BLK_RST_MASK,
							~UVD_VCPU_CNTL__BLK_RST_MASK);
				mdelay(10);
				WREG32_P(SOC15_REG_OFFSET(VCN, i, regUVD_VCPU_CNTL), 0,
						~UVD_VCPU_CNTL__BLK_RST_MASK);

				mdelay(10);
				r = -1;
			}
		}

		if (r) {
			dev_err(adev->dev, "VCN[%d] is not responding, giving up!!!\n", i);
			return r;
		}

		/* enable master interrupt */
		WREG32_P(SOC15_REG_OFFSET(VCN, i, regUVD_MASTINT_EN),
				UVD_MASTINT_EN__VCPU_EN_MASK,
				~UVD_MASTINT_EN__VCPU_EN_MASK);

		/* clear the busy bit of VCN_STATUS */
		WREG32_P(SOC15_REG_OFFSET(VCN, i, regUVD_STATUS), 0,
				~(2 << UVD_STATUS__VCPU_REPORT__SHIFT));

		ring = &adev->vcn.inst[i].ring_enc[0];
		WREG32_SOC15(VCN, i, regVCN_RB1_DB_CTRL,
				ring->doorbell_index << VCN_RB1_DB_CTRL__OFFSET__SHIFT |
				VCN_RB1_DB_CTRL__EN_MASK);

		WREG32_SOC15(VCN, i, regUVD_RB_BASE_LO, ring->gpu_addr);
		WREG32_SOC15(VCN, i, regUVD_RB_BASE_HI, upper_32_bits(ring->gpu_addr));
		WREG32_SOC15(VCN, i, regUVD_RB_SIZE, ring->ring_size / 4);

		tmp = RREG32_SOC15(VCN, i, regVCN_RB_ENABLE);
		tmp &= ~(VCN_RB_ENABLE__RB1_EN_MASK);
		WREG32_SOC15(VCN, i, regVCN_RB_ENABLE, tmp);
		fw_shared->sq.queue_mode |= FW_QUEUE_RING_RESET;
		WREG32_SOC15(VCN, i, regUVD_RB_RPTR, 0);
		WREG32_SOC15(VCN, i, regUVD_RB_WPTR, 0);

		tmp = RREG32_SOC15(VCN, i, regUVD_RB_RPTR);
		WREG32_SOC15(VCN, i, regUVD_RB_WPTR, tmp);
		ring->wptr = RREG32_SOC15(VCN, i, regUVD_RB_WPTR);

		tmp = RREG32_SOC15(VCN, i, regVCN_RB_ENABLE);
		tmp |= VCN_RB_ENABLE__RB1_EN_MASK;
		WREG32_SOC15(VCN, i, regVCN_RB_ENABLE, tmp);
		fw_shared->sq.queue_mode &= ~(FW_QUEUE_RING_RESET | FW_QUEUE_DPG_HOLD_OFF);
	}

	return 0;
}

static int vcn_v4_0_start_sriov(struct amdgpu_device *adev)
{
	int i;
	struct amdgpu_ring *ring_enc;
	uint64_t cache_addr;
	uint64_t rb_enc_addr;
	uint64_t ctx_addr;
	uint32_t param, resp, expected;
	uint32_t offset, cache_size;
	uint32_t tmp, timeout;

	struct amdgpu_mm_table *table = &adev->virt.mm_table;
	uint32_t *table_loc;
	uint32_t table_size;
	uint32_t size, size_dw;
	uint32_t init_status;
	uint32_t enabled_vcn;

	struct mmsch_v4_0_cmd_direct_write
		direct_wt = { {0} };
	struct mmsch_v4_0_cmd_direct_read_modify_write
		direct_rd_mod_wt = { {0} };
	struct mmsch_v4_0_cmd_end end = { {0} };
	struct mmsch_v4_0_init_header header;

	volatile struct amdgpu_vcn4_fw_shared *fw_shared;
	volatile struct amdgpu_fw_shared_rb_setup *rb_setup;

	direct_wt.cmd_header.command_type =
		MMSCH_COMMAND__DIRECT_REG_WRITE;
	direct_rd_mod_wt.cmd_header.command_type =
		MMSCH_COMMAND__DIRECT_REG_READ_MODIFY_WRITE;
	end.cmd_header.command_type =
		MMSCH_COMMAND__END;

	header.version = MMSCH_VERSION;
	header.total_size = sizeof(struct mmsch_v4_0_init_header) >> 2;
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

		MMSCH_V4_0_INSERT_DIRECT_RD_MOD_WT(SOC15_REG_OFFSET(VCN, i,
			regUVD_STATUS),
			~UVD_STATUS__UVD_BUSY, UVD_STATUS__UVD_BUSY);

		cache_size = AMDGPU_GPU_PAGE_ALIGN(adev->vcn.fw->size + 4);

		if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) {
			MMSCH_V4_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCN, i,
				regUVD_LMI_VCPU_CACHE_64BIT_BAR_LOW),
				adev->firmware.ucode[AMDGPU_UCODE_ID_VCN + i].tmr_mc_addr_lo);
			MMSCH_V4_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCN, i,
				regUVD_LMI_VCPU_CACHE_64BIT_BAR_HIGH),
				adev->firmware.ucode[AMDGPU_UCODE_ID_VCN + i].tmr_mc_addr_hi);
			offset = 0;
			MMSCH_V4_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCN, i,
				regUVD_VCPU_CACHE_OFFSET0),
				0);
		} else {
			MMSCH_V4_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCN, i,
				regUVD_LMI_VCPU_CACHE_64BIT_BAR_LOW),
				lower_32_bits(adev->vcn.inst[i].gpu_addr));
			MMSCH_V4_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCN, i,
				regUVD_LMI_VCPU_CACHE_64BIT_BAR_HIGH),
				upper_32_bits(adev->vcn.inst[i].gpu_addr));
			offset = cache_size;
			MMSCH_V4_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCN, i,
				regUVD_VCPU_CACHE_OFFSET0),
				AMDGPU_UVD_FIRMWARE_OFFSET >> 3);
		}

		MMSCH_V4_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCN, i,
			regUVD_VCPU_CACHE_SIZE0),
			cache_size);

		cache_addr = adev->vcn.inst[i].gpu_addr + offset;
		MMSCH_V4_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCN, i,
			regUVD_LMI_VCPU_CACHE1_64BIT_BAR_LOW),
			lower_32_bits(cache_addr));
		MMSCH_V4_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCN, i,
			regUVD_LMI_VCPU_CACHE1_64BIT_BAR_HIGH),
			upper_32_bits(cache_addr));
		MMSCH_V4_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCN, i,
			regUVD_VCPU_CACHE_OFFSET1),
			0);
		MMSCH_V4_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCN, i,
			regUVD_VCPU_CACHE_SIZE1),
			AMDGPU_VCN_STACK_SIZE);

		cache_addr = adev->vcn.inst[i].gpu_addr + offset +
			AMDGPU_VCN_STACK_SIZE;
		MMSCH_V4_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCN, i,
			regUVD_LMI_VCPU_CACHE2_64BIT_BAR_LOW),
			lower_32_bits(cache_addr));
		MMSCH_V4_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCN, i,
			regUVD_LMI_VCPU_CACHE2_64BIT_BAR_HIGH),
			upper_32_bits(cache_addr));
		MMSCH_V4_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCN, i,
			regUVD_VCPU_CACHE_OFFSET2),
			0);
		MMSCH_V4_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCN, i,
			regUVD_VCPU_CACHE_SIZE2),
			AMDGPU_VCN_CONTEXT_SIZE);

		fw_shared = adev->vcn.inst[i].fw_shared.cpu_addr;
		rb_setup = &fw_shared->rb_setup;

		ring_enc = &adev->vcn.inst[i].ring_enc[0];
		ring_enc->wptr = 0;
		rb_enc_addr = ring_enc->gpu_addr;

		rb_setup->is_rb_enabled_flags |= RB_ENABLED;
		rb_setup->rb_addr_lo = lower_32_bits(rb_enc_addr);
		rb_setup->rb_addr_hi = upper_32_bits(rb_enc_addr);
		rb_setup->rb_size = ring_enc->ring_size / 4;
		fw_shared->present_flag_0 |= cpu_to_le32(AMDGPU_VCN_VF_RB_SETUP_FLAG);

		MMSCH_V4_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCN, i,
			regUVD_LMI_VCPU_NC0_64BIT_BAR_LOW),
			lower_32_bits(adev->vcn.inst[i].fw_shared.gpu_addr));
		MMSCH_V4_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCN, i,
			regUVD_LMI_VCPU_NC0_64BIT_BAR_HIGH),
			upper_32_bits(adev->vcn.inst[i].fw_shared.gpu_addr));
		MMSCH_V4_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCN, i,
			regUVD_VCPU_NONCACHE_SIZE0),
			AMDGPU_GPU_PAGE_ALIGN(sizeof(struct amdgpu_vcn4_fw_shared)));

		/* add end packet */
		MMSCH_V4_0_INSERT_END();

		/* refine header */
		header.inst[i].init_status = 0;
		header.inst[i].table_offset = header.total_size;
		header.inst[i].table_size = table_size;
		header.total_size += table_size;
	}

	/* Update init table header in memory */
	size = sizeof(struct mmsch_v4_0_init_header);
	table_loc = (uint32_t *)table->cpu_addr;
	memcpy((void *)table_loc, &header, size);

	/* message MMSCH (in VCN[0]) to initialize this client
	 * 1, write to mmsch_vf_ctx_addr_lo/hi register with GPU mc addr
	 * of memory descriptor location
	 */
	ctx_addr = table->gpu_addr;
	WREG32_SOC15(VCN, 0, regMMSCH_VF_CTX_ADDR_LO, lower_32_bits(ctx_addr));
	WREG32_SOC15(VCN, 0, regMMSCH_VF_CTX_ADDR_HI, upper_32_bits(ctx_addr));

	/* 2, update vmid of descriptor */
	tmp = RREG32_SOC15(VCN, 0, regMMSCH_VF_VMID);
	tmp &= ~MMSCH_VF_VMID__VF_CTX_VMID_MASK;
	/* use domain0 for MM scheduler */
	tmp |= (0 << MMSCH_VF_VMID__VF_CTX_VMID__SHIFT);
	WREG32_SOC15(VCN, 0, regMMSCH_VF_VMID, tmp);

	/* 3, notify mmsch about the size of this descriptor */
	size = header.total_size;
	WREG32_SOC15(VCN, 0, regMMSCH_VF_CTX_SIZE, size);

	/* 4, set resp to zero */
	WREG32_SOC15(VCN, 0, regMMSCH_VF_MAILBOX_RESP, 0);

	/* 5, kick off the initialization and wait until
	 * MMSCH_VF_MAILBOX_RESP becomes non-zero
	 */
	param = 0x00000001;
	WREG32_SOC15(VCN, 0, regMMSCH_VF_MAILBOX_HOST, param);
	tmp = 0;
	timeout = 1000;
	resp = 0;
	expected = MMSCH_VF_MAILBOX_RESP__OK;
	while (resp != expected) {
		resp = RREG32_SOC15(VCN, 0, regMMSCH_VF_MAILBOX_RESP);
		if (resp != 0)
			break;

		udelay(10);
		tmp = tmp + 10;
		if (tmp >= timeout) {
			DRM_ERROR("failed to init MMSCH. TIME-OUT after %d usec"\
				" waiting for regMMSCH_VF_MAILBOX_RESP "\
				"(expected=0x%08x, readback=0x%08x)\n",
				tmp, expected, resp);
			return -EBUSY;
		}
	}
	enabled_vcn = amdgpu_vcn_is_disabled_vcn(adev, VCN_DECODE_RING, 0) ? 1 : 0;
	init_status = ((struct mmsch_v4_0_init_header *)(table_loc))->inst[enabled_vcn].init_status;
	if (resp != expected && resp != MMSCH_VF_MAILBOX_RESP__INCOMPLETE
	&& init_status != MMSCH_VF_ENGINE_STATUS__PASS)
		DRM_ERROR("MMSCH init status is incorrect! readback=0x%08x, header init "\
			"status for VCN%x: 0x%x\n", resp, enabled_vcn, init_status);

	return 0;
}

/**
 * vcn_v4_0_stop_dpg_mode - VCN stop with dpg mode
 *
 * @adev: amdgpu_device pointer
 * @inst_idx: instance number index
 *
 * Stop VCN block with dpg mode
 */
static void vcn_v4_0_stop_dpg_mode(struct amdgpu_device *adev, int inst_idx)
{
	uint32_t tmp;

	/* Wait for power status to be 1 */
	SOC15_WAIT_ON_RREG(VCN, inst_idx, regUVD_POWER_STATUS, 1,
		UVD_POWER_STATUS__UVD_POWER_STATUS_MASK);

	/* wait for read ptr to be equal to write ptr */
	tmp = RREG32_SOC15(VCN, inst_idx, regUVD_RB_WPTR);
	SOC15_WAIT_ON_RREG(VCN, inst_idx, regUVD_RB_RPTR, tmp, 0xFFFFFFFF);

	SOC15_WAIT_ON_RREG(VCN, inst_idx, regUVD_POWER_STATUS, 1,
		UVD_POWER_STATUS__UVD_POWER_STATUS_MASK);

	/* disable dynamic power gating mode */
	WREG32_P(SOC15_REG_OFFSET(VCN, inst_idx, regUVD_POWER_STATUS), 0,
		~UVD_POWER_STATUS__UVD_PG_MODE_MASK);
}

/**
 * vcn_v4_0_stop - VCN stop
 *
 * @adev: amdgpu_device pointer
 *
 * Stop VCN block
 */
static int vcn_v4_0_stop(struct amdgpu_device *adev)
{
	volatile struct amdgpu_vcn4_fw_shared *fw_shared;
	uint32_t tmp;
	int i, r = 0;

	for (i = 0; i < adev->vcn.num_vcn_inst; ++i) {
		fw_shared = adev->vcn.inst[i].fw_shared.cpu_addr;
		fw_shared->sq.queue_mode |= FW_QUEUE_DPG_HOLD_OFF;

		if (adev->pg_flags & AMD_PG_SUPPORT_VCN_DPG) {
			vcn_v4_0_stop_dpg_mode(adev, i);
			continue;
		}

		/* wait for vcn idle */
		r = SOC15_WAIT_ON_RREG(VCN, i, regUVD_STATUS, UVD_STATUS__IDLE, 0x7);
		if (r)
			return r;

		tmp = UVD_LMI_STATUS__VCPU_LMI_WRITE_CLEAN_MASK |
			UVD_LMI_STATUS__READ_CLEAN_MASK |
			UVD_LMI_STATUS__WRITE_CLEAN_MASK |
			UVD_LMI_STATUS__WRITE_CLEAN_RAW_MASK;
		r = SOC15_WAIT_ON_RREG(VCN, i, regUVD_LMI_STATUS, tmp, tmp);
		if (r)
			return r;

		/* disable LMI UMC channel */
		tmp = RREG32_SOC15(VCN, i, regUVD_LMI_CTRL2);
		tmp |= UVD_LMI_CTRL2__STALL_ARB_UMC_MASK;
		WREG32_SOC15(VCN, i, regUVD_LMI_CTRL2, tmp);
		tmp = UVD_LMI_STATUS__UMC_READ_CLEAN_RAW_MASK |
			UVD_LMI_STATUS__UMC_WRITE_CLEAN_RAW_MASK;
		r = SOC15_WAIT_ON_RREG(VCN, i, regUVD_LMI_STATUS, tmp, tmp);
		if (r)
			return r;

		/* block VCPU register access */
		WREG32_P(SOC15_REG_OFFSET(VCN, i, regUVD_RB_ARB_CTRL),
				UVD_RB_ARB_CTRL__VCPU_DIS_MASK,
				~UVD_RB_ARB_CTRL__VCPU_DIS_MASK);

		/* reset VCPU */
		WREG32_P(SOC15_REG_OFFSET(VCN, i, regUVD_VCPU_CNTL),
				UVD_VCPU_CNTL__BLK_RST_MASK,
				~UVD_VCPU_CNTL__BLK_RST_MASK);

		/* disable VCPU clock */
		WREG32_P(SOC15_REG_OFFSET(VCN, i, regUVD_VCPU_CNTL), 0,
				~(UVD_VCPU_CNTL__CLK_EN_MASK));

		/* apply soft reset */
		tmp = RREG32_SOC15(VCN, i, regUVD_SOFT_RESET);
		tmp |= UVD_SOFT_RESET__LMI_UMC_SOFT_RESET_MASK;
		WREG32_SOC15(VCN, i, regUVD_SOFT_RESET, tmp);
		tmp = RREG32_SOC15(VCN, i, regUVD_SOFT_RESET);
		tmp |= UVD_SOFT_RESET__LMI_SOFT_RESET_MASK;
		WREG32_SOC15(VCN, i, regUVD_SOFT_RESET, tmp);

		/* clear status */
		WREG32_SOC15(VCN, i, regUVD_STATUS, 0);

		/* apply HW clock gating */
		vcn_v4_0_enable_clock_gating(adev, i);

		/* enable VCN power gating */
		vcn_v4_0_enable_static_power_gating(adev, i);
	}

	if (adev->pm.dpm_enabled)
		amdgpu_dpm_enable_uvd(adev, false);

	return 0;
}

/**
 * vcn_v4_0_pause_dpg_mode - VCN pause with dpg mode
 *
 * @adev: amdgpu_device pointer
 * @inst_idx: instance number index
 * @new_state: pause state
 *
 * Pause dpg mode for VCN block
 */
static int vcn_v4_0_pause_dpg_mode(struct amdgpu_device *adev, int inst_idx,
      struct dpg_pause_state *new_state)
{
	uint32_t reg_data = 0;
	int ret_code;

	/* pause/unpause if state is changed */
	if (adev->vcn.inst[inst_idx].pause_state.fw_based != new_state->fw_based) {
		DRM_DEV_DEBUG(adev->dev, "dpg pause state changed %d -> %d",
			adev->vcn.inst[inst_idx].pause_state.fw_based,	new_state->fw_based);
		reg_data = RREG32_SOC15(VCN, inst_idx, regUVD_DPG_PAUSE) &
			(~UVD_DPG_PAUSE__NJ_PAUSE_DPG_ACK_MASK);

		if (new_state->fw_based == VCN_DPG_STATE__PAUSE) {
			ret_code = SOC15_WAIT_ON_RREG(VCN, inst_idx, regUVD_POWER_STATUS, 0x1,
				UVD_POWER_STATUS__UVD_POWER_STATUS_MASK);

			if (!ret_code) {
				/* pause DPG */
				reg_data |= UVD_DPG_PAUSE__NJ_PAUSE_DPG_REQ_MASK;
				WREG32_SOC15(VCN, inst_idx, regUVD_DPG_PAUSE, reg_data);

				/* wait for ACK */
				SOC15_WAIT_ON_RREG(VCN, inst_idx, regUVD_DPG_PAUSE,
					UVD_DPG_PAUSE__NJ_PAUSE_DPG_ACK_MASK,
					UVD_DPG_PAUSE__NJ_PAUSE_DPG_ACK_MASK);

				SOC15_WAIT_ON_RREG(VCN, inst_idx, regUVD_POWER_STATUS,
					UVD_PGFSM_CONFIG__UVDM_UVDU_PWR_ON, UVD_POWER_STATUS__UVD_POWER_STATUS_MASK);
			}
		} else {
			/* unpause dpg, no need to wait */
			reg_data &= ~UVD_DPG_PAUSE__NJ_PAUSE_DPG_REQ_MASK;
			WREG32_SOC15(VCN, inst_idx, regUVD_DPG_PAUSE, reg_data);
		}
		adev->vcn.inst[inst_idx].pause_state.fw_based = new_state->fw_based;
	}

	return 0;
}

/**
 * vcn_v4_0_unified_ring_get_rptr - get unified read pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Returns the current hardware unified read pointer
 */
static uint64_t vcn_v4_0_unified_ring_get_rptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring != &adev->vcn.inst[ring->me].ring_enc[0])
		DRM_ERROR("wrong ring id is identified in %s", __func__);

	return RREG32_SOC15(VCN, ring->me, regUVD_RB_RPTR);
}

/**
 * vcn_v4_0_unified_ring_get_wptr - get unified write pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Returns the current hardware unified write pointer
 */
static uint64_t vcn_v4_0_unified_ring_get_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring != &adev->vcn.inst[ring->me].ring_enc[0])
		DRM_ERROR("wrong ring id is identified in %s", __func__);

	if (ring->use_doorbell)
		return *ring->wptr_cpu_addr;
	else
		return RREG32_SOC15(VCN, ring->me, regUVD_RB_WPTR);
}

/**
 * vcn_v4_0_unified_ring_set_wptr - set enc write pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Commits the enc write pointer to the hardware
 */
static void vcn_v4_0_unified_ring_set_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring != &adev->vcn.inst[ring->me].ring_enc[0])
		DRM_ERROR("wrong ring id is identified in %s", __func__);

	if (ring->use_doorbell) {
		*ring->wptr_cpu_addr = lower_32_bits(ring->wptr);
		WDOORBELL32(ring->doorbell_index, lower_32_bits(ring->wptr));
	} else {
		WREG32_SOC15(VCN, ring->me, regUVD_RB_WPTR, lower_32_bits(ring->wptr));
	}
}

static int vcn_v4_0_limit_sched(struct amdgpu_cs_parser *p,
				struct amdgpu_job *job)
{
	struct drm_gpu_scheduler **scheds;

	/* The create msg must be in the first IB submitted */
	if (atomic_read(&job->base.entity->fence_seq))
		return -EINVAL;

	/* if VCN0 is harvested, we can't support AV1 */
	if (p->adev->vcn.harvest_config & AMDGPU_VCN_HARVEST_VCN0)
		return -EINVAL;

	scheds = p->adev->gpu_sched[AMDGPU_HW_IP_VCN_ENC]
		[AMDGPU_RING_PRIO_0].sched;
	drm_sched_entity_modify_sched(job->base.entity, scheds, 1);
	return 0;
}

static int vcn_v4_0_dec_msg(struct amdgpu_cs_parser *p, struct amdgpu_job *job,
			    uint64_t addr)
{
	struct ttm_operation_ctx ctx = { false, false };
	struct amdgpu_bo_va_mapping *map;
	uint32_t *msg, num_buffers;
	struct amdgpu_bo *bo;
	uint64_t start, end;
	unsigned int i;
	void *ptr;
	int r;

	addr &= AMDGPU_GMC_HOLE_MASK;
	r = amdgpu_cs_find_mapping(p, addr, &bo, &map);
	if (r) {
		DRM_ERROR("Can't find BO for addr 0x%08llx\n", addr);
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

		/* H264, HEVC and VP9 can run on any instance */
		if (create[0] == 0x7 || create[0] == 0x10 || create[0] == 0x11)
			continue;

		r = vcn_v4_0_limit_sched(p, job);
		if (r)
			goto out;
	}

out:
	amdgpu_bo_kunmap(bo);
	return r;
}

#define RADEON_VCN_ENGINE_TYPE_ENCODE			(0x00000002)
#define RADEON_VCN_ENGINE_TYPE_DECODE			(0x00000003)

#define RADEON_VCN_ENGINE_INFO				(0x30000001)
#define RADEON_VCN_ENGINE_INFO_MAX_OFFSET		16

#define RENCODE_ENCODE_STANDARD_AV1			2
#define RENCODE_IB_PARAM_SESSION_INIT			0x00000003
#define RENCODE_IB_PARAM_SESSION_INIT_MAX_OFFSET	64

/* return the offset in ib if id is found, -1 otherwise
 * to speed up the searching we only search upto max_offset
 */
static int vcn_v4_0_enc_find_ib_param(struct amdgpu_ib *ib, uint32_t id, int max_offset)
{
	int i;

	for (i = 0; i < ib->length_dw && i < max_offset && ib->ptr[i] >= 8; i += ib->ptr[i]/4) {
		if (ib->ptr[i + 1] == id)
			return i;
	}
	return -1;
}

static int vcn_v4_0_ring_patch_cs_in_place(struct amdgpu_cs_parser *p,
					   struct amdgpu_job *job,
					   struct amdgpu_ib *ib)
{
	struct amdgpu_ring *ring = amdgpu_job_ring(job);
	struct amdgpu_vcn_decode_buffer *decode_buffer;
	uint64_t addr;
	uint32_t val;
	int idx;

	/* The first instance can decode anything */
	if (!ring->me)
		return 0;

	/* RADEON_VCN_ENGINE_INFO is at the top of ib block */
	idx = vcn_v4_0_enc_find_ib_param(ib, RADEON_VCN_ENGINE_INFO,
			RADEON_VCN_ENGINE_INFO_MAX_OFFSET);
	if (idx < 0) /* engine info is missing */
		return 0;

	val = amdgpu_ib_get_value(ib, idx + 2); /* RADEON_VCN_ENGINE_TYPE */
	if (val == RADEON_VCN_ENGINE_TYPE_DECODE) {
		decode_buffer = (struct amdgpu_vcn_decode_buffer *)&ib->ptr[idx + 6];

		if (!(decode_buffer->valid_buf_flag  & 0x1))
			return 0;

		addr = ((u64)decode_buffer->msg_buffer_address_hi) << 32 |
			decode_buffer->msg_buffer_address_lo;
		return vcn_v4_0_dec_msg(p, job, addr);
	} else if (val == RADEON_VCN_ENGINE_TYPE_ENCODE) {
		idx = vcn_v4_0_enc_find_ib_param(ib, RENCODE_IB_PARAM_SESSION_INIT,
			RENCODE_IB_PARAM_SESSION_INIT_MAX_OFFSET);
		if (idx >= 0 && ib->ptr[idx + 2] == RENCODE_ENCODE_STANDARD_AV1)
			return vcn_v4_0_limit_sched(p, job);
	}
	return 0;
}

static const struct amdgpu_ring_funcs vcn_v4_0_unified_ring_vm_funcs = {
	.type = AMDGPU_RING_TYPE_VCN_ENC,
	.align_mask = 0x3f,
	.nop = VCN_ENC_CMD_NO_OP,
	.get_rptr = vcn_v4_0_unified_ring_get_rptr,
	.get_wptr = vcn_v4_0_unified_ring_get_wptr,
	.set_wptr = vcn_v4_0_unified_ring_set_wptr,
	.patch_cs_in_place = vcn_v4_0_ring_patch_cs_in_place,
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
	.test_ib = amdgpu_vcn_unified_ring_test_ib,
	.insert_nop = amdgpu_ring_insert_nop,
	.insert_end = vcn_v2_0_enc_ring_insert_end,
	.pad_ib = amdgpu_ring_generic_pad_ib,
	.begin_use = amdgpu_vcn_ring_begin_use,
	.end_use = amdgpu_vcn_ring_end_use,
	.emit_wreg = vcn_v2_0_enc_ring_emit_wreg,
	.emit_reg_wait = vcn_v2_0_enc_ring_emit_reg_wait,
	.emit_reg_write_reg_wait = amdgpu_ring_emit_reg_write_reg_wait_helper,
};

/**
 * vcn_v4_0_set_unified_ring_funcs - set unified ring functions
 *
 * @adev: amdgpu_device pointer
 *
 * Set unified ring functions
 */
static void vcn_v4_0_set_unified_ring_funcs(struct amdgpu_device *adev)
{
	int i;

	for (i = 0; i < adev->vcn.num_vcn_inst; ++i) {
		if (adev->vcn.harvest_config & (1 << i))
			continue;

		adev->vcn.inst[i].ring_enc[0].funcs = &vcn_v4_0_unified_ring_vm_funcs;
		adev->vcn.inst[i].ring_enc[0].me = i;

		DRM_INFO("VCN(%d) encode/decode are enabled in VM mode\n", i);
	}
}

/**
 * vcn_v4_0_is_idle - check VCN block is idle
 *
 * @handle: amdgpu_device pointer
 *
 * Check whether VCN block is idle
 */
static bool vcn_v4_0_is_idle(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int i, ret = 1;

	for (i = 0; i < adev->vcn.num_vcn_inst; ++i) {
		if (adev->vcn.harvest_config & (1 << i))
			continue;

		ret &= (RREG32_SOC15(VCN, i, regUVD_STATUS) == UVD_STATUS__IDLE);
	}

	return ret;
}

/**
 * vcn_v4_0_wait_for_idle - wait for VCN block idle
 *
 * @handle: amdgpu_device pointer
 *
 * Wait for VCN block idle
 */
static int vcn_v4_0_wait_for_idle(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int i, ret = 0;

	for (i = 0; i < adev->vcn.num_vcn_inst; ++i) {
		if (adev->vcn.harvest_config & (1 << i))
			continue;

		ret = SOC15_WAIT_ON_RREG(VCN, i, regUVD_STATUS, UVD_STATUS__IDLE,
			UVD_STATUS__IDLE);
		if (ret)
			return ret;
	}

	return ret;
}

/**
 * vcn_v4_0_set_clockgating_state - set VCN block clockgating state
 *
 * @handle: amdgpu_device pointer
 * @state: clock gating state
 *
 * Set VCN block clockgating state
 */
static int vcn_v4_0_set_clockgating_state(void *handle, enum amd_clockgating_state state)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	bool enable = (state == AMD_CG_STATE_GATE) ? true : false;
	int i;

	for (i = 0; i < adev->vcn.num_vcn_inst; ++i) {
		if (adev->vcn.harvest_config & (1 << i))
			continue;

		if (enable) {
			if (RREG32_SOC15(VCN, i, regUVD_STATUS) != UVD_STATUS__IDLE)
				return -EBUSY;
			vcn_v4_0_enable_clock_gating(adev, i);
		} else {
			vcn_v4_0_disable_clock_gating(adev, i);
		}
	}

	return 0;
}

/**
 * vcn_v4_0_set_powergating_state - set VCN block powergating state
 *
 * @handle: amdgpu_device pointer
 * @state: power gating state
 *
 * Set VCN block powergating state
 */
static int vcn_v4_0_set_powergating_state(void *handle, enum amd_powergating_state state)
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
		ret = vcn_v4_0_stop(adev);
	else
		ret = vcn_v4_0_start(adev);

	if(!ret)
		adev->vcn.cur_state = state;

	return ret;
}

/**
 * vcn_v4_0_set_interrupt_state - set VCN block interrupt state
 *
 * @adev: amdgpu_device pointer
 * @source: interrupt sources
 * @type: interrupt types
 * @state: interrupt states
 *
 * Set VCN block interrupt state
 */
static int vcn_v4_0_set_interrupt_state(struct amdgpu_device *adev, struct amdgpu_irq_src *source,
      unsigned type, enum amdgpu_interrupt_state state)
{
	return 0;
}

/**
 * vcn_v4_0_process_interrupt - process VCN block interrupt
 *
 * @adev: amdgpu_device pointer
 * @source: interrupt sources
 * @entry: interrupt entry from clients and sources
 *
 * Process VCN block interrupt
 */
static int vcn_v4_0_process_interrupt(struct amdgpu_device *adev, struct amdgpu_irq_src *source,
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
	case VCN_4_0__SRCID__UVD_ENC_GENERAL_PURPOSE:
		amdgpu_fence_process(&adev->vcn.inst[ip_instance].ring_enc[0]);
		break;
	case VCN_4_0__SRCID_UVD_POISON:
		amdgpu_vcn_process_poison_irq(adev, source, entry);
		break;
	default:
		DRM_ERROR("Unhandled interrupt: %d %d\n",
			  entry->src_id, entry->src_data[0]);
		break;
	}

	return 0;
}

static const struct amdgpu_irq_src_funcs vcn_v4_0_irq_funcs = {
	.set = vcn_v4_0_set_interrupt_state,
	.process = vcn_v4_0_process_interrupt,
};

/**
 * vcn_v4_0_set_irq_funcs - set VCN block interrupt irq functions
 *
 * @adev: amdgpu_device pointer
 *
 * Set VCN block interrupt irq functions
 */
static void vcn_v4_0_set_irq_funcs(struct amdgpu_device *adev)
{
	int i;

	for (i = 0; i < adev->vcn.num_vcn_inst; ++i) {
		if (adev->vcn.harvest_config & (1 << i))
			continue;

		adev->vcn.inst[i].irq.num_types = adev->vcn.num_enc_rings + 1;
		adev->vcn.inst[i].irq.funcs = &vcn_v4_0_irq_funcs;
	}
}

static const struct amd_ip_funcs vcn_v4_0_ip_funcs = {
	.name = "vcn_v4_0",
	.early_init = vcn_v4_0_early_init,
	.late_init = NULL,
	.sw_init = vcn_v4_0_sw_init,
	.sw_fini = vcn_v4_0_sw_fini,
	.hw_init = vcn_v4_0_hw_init,
	.hw_fini = vcn_v4_0_hw_fini,
	.suspend = vcn_v4_0_suspend,
	.resume = vcn_v4_0_resume,
	.is_idle = vcn_v4_0_is_idle,
	.wait_for_idle = vcn_v4_0_wait_for_idle,
	.check_soft_reset = NULL,
	.pre_soft_reset = NULL,
	.soft_reset = NULL,
	.post_soft_reset = NULL,
	.set_clockgating_state = vcn_v4_0_set_clockgating_state,
	.set_powergating_state = vcn_v4_0_set_powergating_state,
};

const struct amdgpu_ip_block_version vcn_v4_0_ip_block =
{
	.type = AMD_IP_BLOCK_TYPE_VCN,
	.major = 4,
	.minor = 0,
	.rev = 0,
	.funcs = &vcn_v4_0_ip_funcs,
};

static uint32_t vcn_v4_0_query_poison_by_instance(struct amdgpu_device *adev,
			uint32_t instance, uint32_t sub_block)
{
	uint32_t poison_stat = 0, reg_value = 0;

	switch (sub_block) {
	case AMDGPU_VCN_V4_0_VCPU_VCODEC:
		reg_value = RREG32_SOC15(VCN, instance, regUVD_RAS_VCPU_VCODEC_STATUS);
		poison_stat = REG_GET_FIELD(reg_value, UVD_RAS_VCPU_VCODEC_STATUS, POISONED_PF);
		break;
	default:
		break;
	}

	if (poison_stat)
		dev_info(adev->dev, "Poison detected in VCN%d, sub_block%d\n",
			instance, sub_block);

	return poison_stat;
}

static bool vcn_v4_0_query_ras_poison_status(struct amdgpu_device *adev)
{
	uint32_t inst, sub;
	uint32_t poison_stat = 0;

	for (inst = 0; inst < adev->vcn.num_vcn_inst; inst++)
		for (sub = 0; sub < AMDGPU_VCN_V4_0_MAX_SUB_BLOCK; sub++)
			poison_stat +=
				vcn_v4_0_query_poison_by_instance(adev, inst, sub);

	return !!poison_stat;
}

const struct amdgpu_ras_block_hw_ops vcn_v4_0_ras_hw_ops = {
	.query_poison_status = vcn_v4_0_query_ras_poison_status,
};

static struct amdgpu_vcn_ras vcn_v4_0_ras = {
	.ras_block = {
		.hw_ops = &vcn_v4_0_ras_hw_ops,
	},
};

static void vcn_v4_0_set_ras_funcs(struct amdgpu_device *adev)
{
	switch (adev->ip_versions[VCN_HWIP][0]) {
	case IP_VERSION(4, 0, 0):
		adev->vcn.ras = &vcn_v4_0_ras;
		break;
	default:
		break;
	}
}
