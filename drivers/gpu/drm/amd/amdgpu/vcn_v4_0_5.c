/*
 * Copyright 2023 Advanced Micro Devices, Inc.
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
#include "vcn_v4_0_5.h"

#include "vcn/vcn_4_0_5_offset.h"
#include "vcn/vcn_4_0_5_sh_mask.h"
#include "ivsrcid/vcn/irqsrcs_vcn_4_0.h"

#include <drm/drm_drv.h>

#define mmUVD_DPG_LMA_CTL							regUVD_DPG_LMA_CTL
#define mmUVD_DPG_LMA_CTL_BASE_IDX					regUVD_DPG_LMA_CTL_BASE_IDX
#define mmUVD_DPG_LMA_DATA							regUVD_DPG_LMA_DATA
#define mmUVD_DPG_LMA_DATA_BASE_IDX					regUVD_DPG_LMA_DATA_BASE_IDX

#define VCN_VID_SOC_ADDRESS_2_0						0x1fb00
#define VCN1_VID_SOC_ADDRESS_3_0					(0x48300 + 0x38000)
#define VCN1_AON_SOC_ADDRESS_3_0					(0x48000 + 0x38000)

#define VCN_HARVEST_MMSCH							0

#define RDECODE_MSG_CREATE							0x00000000
#define RDECODE_MESSAGE_CREATE						0x00000001

static const struct amdgpu_hwip_reg_entry vcn_reg_list_4_0_5[] = {
	SOC15_REG_ENTRY_STR(VCN, 0, regUVD_POWER_STATUS),
	SOC15_REG_ENTRY_STR(VCN, 0, regUVD_STATUS),
	SOC15_REG_ENTRY_STR(VCN, 0, regUVD_CONTEXT_ID),
	SOC15_REG_ENTRY_STR(VCN, 0, regUVD_CONTEXT_ID2),
	SOC15_REG_ENTRY_STR(VCN, 0, regUVD_GPCOM_VCPU_DATA0),
	SOC15_REG_ENTRY_STR(VCN, 0, regUVD_GPCOM_VCPU_DATA1),
	SOC15_REG_ENTRY_STR(VCN, 0, regUVD_GPCOM_VCPU_CMD),
	SOC15_REG_ENTRY_STR(VCN, 0, regUVD_RB_BASE_HI),
	SOC15_REG_ENTRY_STR(VCN, 0, regUVD_RB_BASE_LO),
	SOC15_REG_ENTRY_STR(VCN, 0, regUVD_RB_BASE_HI2),
	SOC15_REG_ENTRY_STR(VCN, 0, regUVD_RB_BASE_LO2),
	SOC15_REG_ENTRY_STR(VCN, 0, regUVD_RB_BASE_HI3),
	SOC15_REG_ENTRY_STR(VCN, 0, regUVD_RB_BASE_LO3),
	SOC15_REG_ENTRY_STR(VCN, 0, regUVD_RB_BASE_HI4),
	SOC15_REG_ENTRY_STR(VCN, 0, regUVD_RB_BASE_LO4),
	SOC15_REG_ENTRY_STR(VCN, 0, regUVD_RB_RPTR),
	SOC15_REG_ENTRY_STR(VCN, 0, regUVD_RB_WPTR),
	SOC15_REG_ENTRY_STR(VCN, 0, regUVD_RB_RPTR2),
	SOC15_REG_ENTRY_STR(VCN, 0, regUVD_RB_WPTR2),
	SOC15_REG_ENTRY_STR(VCN, 0, regUVD_RB_RPTR3),
	SOC15_REG_ENTRY_STR(VCN, 0, regUVD_RB_WPTR3),
	SOC15_REG_ENTRY_STR(VCN, 0, regUVD_RB_RPTR4),
	SOC15_REG_ENTRY_STR(VCN, 0, regUVD_RB_WPTR4),
	SOC15_REG_ENTRY_STR(VCN, 0, regUVD_RB_SIZE),
	SOC15_REG_ENTRY_STR(VCN, 0, regUVD_RB_SIZE2),
	SOC15_REG_ENTRY_STR(VCN, 0, regUVD_RB_SIZE3),
	SOC15_REG_ENTRY_STR(VCN, 0, regUVD_RB_SIZE4),
	SOC15_REG_ENTRY_STR(VCN, 0, regUVD_PGFSM_CONFIG),
	SOC15_REG_ENTRY_STR(VCN, 0, regUVD_PGFSM_STATUS),
	SOC15_REG_ENTRY_STR(VCN, 0, regUVD_DPG_LMA_CTL),
	SOC15_REG_ENTRY_STR(VCN, 0, regUVD_DPG_LMA_DATA),
	SOC15_REG_ENTRY_STR(VCN, 0, regUVD_DPG_LMA_MASK),
	SOC15_REG_ENTRY_STR(VCN, 0, regUVD_DPG_PAUSE)
};

static int amdgpu_ih_clientid_vcns[] = {
	SOC15_IH_CLIENTID_VCN,
	SOC15_IH_CLIENTID_VCN1
};

static void vcn_v4_0_5_set_unified_ring_funcs(struct amdgpu_device *adev);
static void vcn_v4_0_5_set_irq_funcs(struct amdgpu_device *adev);
static int vcn_v4_0_5_set_pg_state(struct amdgpu_vcn_inst *vinst,
				   enum amd_powergating_state state);
static int vcn_v4_0_5_pause_dpg_mode(struct amdgpu_vcn_inst *vinst,
				     struct dpg_pause_state *new_state);
static void vcn_v4_0_5_unified_ring_set_wptr(struct amdgpu_ring *ring);

/**
 * vcn_v4_0_5_early_init - set function pointers and load microcode
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 *
 * Set ring and irq function pointers
 * Load microcode from filesystem
 */
static int vcn_v4_0_5_early_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	int i, r;

	if (amdgpu_ip_version(adev, UVD_HWIP, 0) == IP_VERSION(4, 0, 6))
		adev->vcn.per_inst_fw = true;

	for (i = 0; i < adev->vcn.num_vcn_inst; ++i)
		/* re-use enc ring as unified ring */
		adev->vcn.inst[i].num_enc_rings = 1;
	vcn_v4_0_5_set_unified_ring_funcs(adev);
	vcn_v4_0_5_set_irq_funcs(adev);

	for (i = 0; i < adev->vcn.num_vcn_inst; ++i) {
		adev->vcn.inst[i].set_pg_state = vcn_v4_0_5_set_pg_state;

		r = amdgpu_vcn_early_init(adev, i);
		if (r)
			return r;
	}

	return 0;
}

/**
 * vcn_v4_0_5_sw_init - sw init for VCN block
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 *
 * Load firmware and sw initialization
 */
static int vcn_v4_0_5_sw_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_ring *ring;
	struct amdgpu_device *adev = ip_block->adev;
	int i, r;

	for (i = 0; i < adev->vcn.num_vcn_inst; i++) {
		struct amdgpu_vcn4_fw_shared *fw_shared;

		if (adev->vcn.harvest_config & (1 << i))
			continue;

		r = amdgpu_vcn_sw_init(adev, i);
		if (r)
			return r;

		amdgpu_vcn_setup_ucode(adev, i);

		r = amdgpu_vcn_resume(adev, i);
		if (r)
			return r;

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
			ring->doorbell_index = (adev->doorbell_index.vcn.vcn_ring0_1 << 1) +
						i * (adev->vcn.inst[i].num_enc_rings + 1) + 1;
		else
			ring->doorbell_index = (adev->doorbell_index.vcn.vcn_ring0_1 << 1) +
						2 + 8 * i;
		ring->vm_hub = AMDGPU_MMHUB0(0);
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

		fw_shared->present_flag_0 |= AMDGPU_FW_SHARED_FLAG_0_DRM_KEY_INJECT;
		fw_shared->drm_key_wa.method =
			AMDGPU_DRM_KEY_INJECT_WORKAROUND_VCNFW_ASD_HANDSHAKING;

		if (amdgpu_vcnfw_log)
			amdgpu_vcn_fwlog_init(&adev->vcn.inst[i]);

		if (adev->pg_flags & AMD_PG_SUPPORT_VCN_DPG)
			adev->vcn.inst[i].pause_dpg_mode = vcn_v4_0_5_pause_dpg_mode;
	}

	adev->vcn.supported_reset = amdgpu_get_soft_full_reset_mask(&adev->vcn.inst[0].ring_enc[0]);
	if (!amdgpu_sriov_vf(adev))
		adev->vcn.supported_reset |= AMDGPU_RESET_TYPE_PER_QUEUE;

	r = amdgpu_vcn_sysfs_reset_mask_init(adev);
	if (r)
		return r;

	if (amdgpu_sriov_vf(adev)) {
		r = amdgpu_virt_alloc_mm_table(adev);
		if (r)
			return r;
	}

	r = amdgpu_vcn_reg_dump_init(adev, vcn_reg_list_4_0_5, ARRAY_SIZE(vcn_reg_list_4_0_5));

	return r;
}

/**
 * vcn_v4_0_5_sw_fini - sw fini for VCN block
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 *
 * VCN suspend and free up sw allocation
 */
static int vcn_v4_0_5_sw_fini(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	int i, r, idx;

	if (drm_dev_enter(adev_to_drm(adev), &idx)) {
		for (i = 0; i < adev->vcn.num_vcn_inst; i++) {
			struct amdgpu_vcn4_fw_shared *fw_shared;

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

	for (i = 0; i < adev->vcn.num_vcn_inst; i++) {
		r = amdgpu_vcn_suspend(adev, i);
		if (r)
			return r;

		amdgpu_vcn_sw_fini(adev, i);
	}

	return 0;
}

/**
 * vcn_v4_0_5_hw_init - start and test VCN block
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 *
 * Initialize the hardware, boot up the VCPU and do some testing
 */
static int vcn_v4_0_5_hw_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	struct amdgpu_ring *ring;
	int i, r;

	for (i = 0; i < adev->vcn.num_vcn_inst; ++i) {
		if (adev->vcn.harvest_config & (1 << i))
			continue;

		ring = &adev->vcn.inst[i].ring_enc[0];

		adev->nbio.funcs->vcn_doorbell_range(adev, ring->use_doorbell,
				((adev->doorbell_index.vcn.vcn_ring0_1 << 1) + 8 * i), i);

		r = amdgpu_ring_test_helper(ring);
		if (r)
			return r;
	}

	return 0;
}

/**
 * vcn_v4_0_5_hw_fini - stop the hardware block
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 *
 * Stop the VCN block, mark ring as not ready any more
 */
static int vcn_v4_0_5_hw_fini(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	int i;

	for (i = 0; i < adev->vcn.num_vcn_inst; ++i) {
		struct amdgpu_vcn_inst *vinst = &adev->vcn.inst[i];

		if (adev->vcn.harvest_config & (1 << i))
			continue;

		cancel_delayed_work_sync(&vinst->idle_work);

		if (!amdgpu_sriov_vf(adev)) {
			if ((adev->pg_flags & AMD_PG_SUPPORT_VCN_DPG) ||
			    (vinst->cur_state != AMD_PG_STATE_GATE &&
			     RREG32_SOC15(VCN, i, regUVD_STATUS))) {
				vinst->set_pg_state(vinst, AMD_PG_STATE_GATE);
			}
		}
	}

	return 0;
}

/**
 * vcn_v4_0_5_suspend - suspend VCN block
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 *
 * HW fini and suspend VCN block
 */
static int vcn_v4_0_5_suspend(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	int r, i;

	r = vcn_v4_0_5_hw_fini(ip_block);
	if (r)
		return r;

	for (i = 0; i < adev->vcn.num_vcn_inst; i++) {
		r = amdgpu_vcn_suspend(ip_block->adev, i);
		if (r)
			return r;
	}

	return r;
}

/**
 * vcn_v4_0_5_resume - resume VCN block
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 *
 * Resume firmware and hw init VCN block
 */
static int vcn_v4_0_5_resume(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	int r, i;

	for (i = 0; i < adev->vcn.num_vcn_inst; i++) {
		r = amdgpu_vcn_resume(ip_block->adev, i);
		if (r)
			return r;
	}

	r = vcn_v4_0_5_hw_init(ip_block);

	return r;
}

/**
 * vcn_v4_0_5_mc_resume - memory controller programming
 *
 * @vinst: VCN instance
 *
 * Let the VCN memory controller know it's offsets
 */
static void vcn_v4_0_5_mc_resume(struct amdgpu_vcn_inst *vinst)
{
	struct amdgpu_device *adev = vinst->adev;
	int inst = vinst->inst;
	uint32_t offset, size;
	const struct common_firmware_header *hdr;

	hdr = (const struct common_firmware_header *)adev->vcn.inst[inst].fw->data;
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
 * vcn_v4_0_5_mc_resume_dpg_mode - memory controller programming for dpg mode
 *
 * @vinst: VCN instance
 * @indirect: indirectly write sram
 *
 * Let the VCN memory controller know it's offsets with dpg mode
 */
static void vcn_v4_0_5_mc_resume_dpg_mode(struct amdgpu_vcn_inst *vinst,
					  bool indirect)
{
	struct amdgpu_device *adev = vinst->adev;
	int inst_idx = vinst->inst;
	uint32_t offset, size;
	const struct common_firmware_header *hdr;

	hdr = (const struct common_firmware_header *)adev->vcn.inst[inst_idx].fw->data;
	size = AMDGPU_GPU_PAGE_ALIGN(le32_to_cpu(hdr->ucode_size_bytes) + 8);

	/* cache window 0: fw */
	if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) {
		if (!indirect) {
			WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
			VCN, inst_idx, regUVD_LMI_VCPU_CACHE_64BIT_BAR_LOW),
			(adev->firmware.ucode[AMDGPU_UCODE_ID_VCN + inst_idx].tmr_mc_addr_lo),
			0, indirect);
			WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
			VCN, inst_idx, regUVD_LMI_VCPU_CACHE_64BIT_BAR_HIGH),
			(adev->firmware.ucode[AMDGPU_UCODE_ID_VCN + inst_idx].tmr_mc_addr_hi),
			0, indirect);
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
		lower_32_bits(adev->vcn.inst[inst_idx].gpu_addr + offset + AMDGPU_VCN_STACK_SIZE),
		0, indirect);
	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
		VCN, inst_idx, regUVD_LMI_VCPU_CACHE2_64BIT_BAR_HIGH),
		upper_32_bits(adev->vcn.inst[inst_idx].gpu_addr + offset + AMDGPU_VCN_STACK_SIZE),
		0, indirect);
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
		VCN, inst_idx, regUVD_GFX10_ADDR_CONFIG),
		adev->gfx.config.gb_addr_config, 0, indirect);
}

/**
 * vcn_v4_0_5_disable_static_power_gating - disable VCN static power gating
 *
 * @vinst: VCN instance
 *
 * Disable static power gating for VCN block
 */
static void vcn_v4_0_5_disable_static_power_gating(struct amdgpu_vcn_inst *vinst)
{
	struct amdgpu_device *adev = vinst->adev;
	int inst = vinst->inst;
	uint32_t data = 0;

	if (adev->pg_flags & AMD_PG_SUPPORT_VCN) {
		WREG32_SOC15(VCN, inst, regUVD_IPX_DLDO_CONFIG,
					1 << UVD_IPX_DLDO_CONFIG__ONO2_PWR_CONFIG__SHIFT);
		SOC15_WAIT_ON_RREG(VCN, inst, regUVD_IPX_DLDO_STATUS, 0,
					UVD_IPX_DLDO_STATUS__ONO2_PWR_STATUS_MASK);
		WREG32_SOC15(VCN, inst, regUVD_IPX_DLDO_CONFIG,
					2 << UVD_IPX_DLDO_CONFIG__ONO3_PWR_CONFIG__SHIFT);
		SOC15_WAIT_ON_RREG(VCN, inst, regUVD_IPX_DLDO_STATUS,
					1 << UVD_IPX_DLDO_STATUS__ONO3_PWR_STATUS__SHIFT,
					UVD_IPX_DLDO_STATUS__ONO3_PWR_STATUS_MASK);
		WREG32_SOC15(VCN, inst, regUVD_IPX_DLDO_CONFIG,
					2 << UVD_IPX_DLDO_CONFIG__ONO4_PWR_CONFIG__SHIFT);
		SOC15_WAIT_ON_RREG(VCN, inst, regUVD_IPX_DLDO_STATUS,
					1 << UVD_IPX_DLDO_STATUS__ONO4_PWR_STATUS__SHIFT,
					UVD_IPX_DLDO_STATUS__ONO4_PWR_STATUS_MASK);
		WREG32_SOC15(VCN, inst, regUVD_IPX_DLDO_CONFIG,
					2 << UVD_IPX_DLDO_CONFIG__ONO5_PWR_CONFIG__SHIFT);
		SOC15_WAIT_ON_RREG(VCN, inst, regUVD_IPX_DLDO_STATUS,
					1 << UVD_IPX_DLDO_STATUS__ONO5_PWR_STATUS__SHIFT,
					UVD_IPX_DLDO_STATUS__ONO5_PWR_STATUS_MASK);
	} else {
		WREG32_SOC15(VCN, inst, regUVD_IPX_DLDO_CONFIG,
			1 << UVD_IPX_DLDO_CONFIG__ONO2_PWR_CONFIG__SHIFT);
		SOC15_WAIT_ON_RREG(VCN, inst, regUVD_IPX_DLDO_STATUS,
			0, UVD_IPX_DLDO_STATUS__ONO2_PWR_STATUS_MASK);
		WREG32_SOC15(VCN, inst, regUVD_IPX_DLDO_CONFIG,
			1 << UVD_IPX_DLDO_CONFIG__ONO3_PWR_CONFIG__SHIFT);
		SOC15_WAIT_ON_RREG(VCN, inst, regUVD_IPX_DLDO_STATUS,
			0, UVD_IPX_DLDO_STATUS__ONO3_PWR_STATUS_MASK);
		WREG32_SOC15(VCN, inst, regUVD_IPX_DLDO_CONFIG,
			1 << UVD_IPX_DLDO_CONFIG__ONO4_PWR_CONFIG__SHIFT);
		SOC15_WAIT_ON_RREG(VCN, inst, regUVD_IPX_DLDO_STATUS,
			0, UVD_IPX_DLDO_STATUS__ONO4_PWR_STATUS_MASK);
		WREG32_SOC15(VCN, inst, regUVD_IPX_DLDO_CONFIG,
			1 << UVD_IPX_DLDO_CONFIG__ONO5_PWR_CONFIG__SHIFT);
		SOC15_WAIT_ON_RREG(VCN, inst, regUVD_IPX_DLDO_STATUS,
			0, UVD_IPX_DLDO_STATUS__ONO5_PWR_STATUS_MASK);
	}

	data = RREG32_SOC15(VCN, inst, regUVD_POWER_STATUS);
	data &= ~0x103;
	if (adev->pg_flags & AMD_PG_SUPPORT_VCN)
		data |= UVD_PGFSM_CONFIG__UVDM_UVDU_PWR_ON |
			UVD_POWER_STATUS__UVD_PG_EN_MASK;
	WREG32_SOC15(VCN, inst, regUVD_POWER_STATUS, data);
}

/**
 * vcn_v4_0_5_enable_static_power_gating - enable VCN static power gating
 *
 * @vinst: VCN instance
 *
 * Enable static power gating for VCN block
 */
static void vcn_v4_0_5_enable_static_power_gating(struct amdgpu_vcn_inst *vinst)
{
	struct amdgpu_device *adev = vinst->adev;
	int inst = vinst->inst;
	uint32_t data;

	if (adev->pg_flags & AMD_PG_SUPPORT_VCN) {
		/* Before power off, this indicator has to be turned on */
		data = RREG32_SOC15(VCN, inst, regUVD_POWER_STATUS);
		data &= ~UVD_POWER_STATUS__UVD_POWER_STATUS_MASK;
		data |= UVD_POWER_STATUS__UVD_POWER_STATUS_TILES_OFF;
		WREG32_SOC15(VCN, inst, regUVD_POWER_STATUS, data);

		WREG32_SOC15(VCN, inst, regUVD_IPX_DLDO_CONFIG,
			2 << UVD_IPX_DLDO_CONFIG__ONO5_PWR_CONFIG__SHIFT);
		SOC15_WAIT_ON_RREG(VCN, inst, regUVD_IPX_DLDO_STATUS,
			1 << UVD_IPX_DLDO_STATUS__ONO5_PWR_STATUS__SHIFT,
			UVD_IPX_DLDO_STATUS__ONO5_PWR_STATUS_MASK);
		WREG32_SOC15(VCN, inst, regUVD_IPX_DLDO_CONFIG,
			2 << UVD_IPX_DLDO_CONFIG__ONO4_PWR_CONFIG__SHIFT);
		SOC15_WAIT_ON_RREG(VCN, inst, regUVD_IPX_DLDO_STATUS,
			1 << UVD_IPX_DLDO_STATUS__ONO4_PWR_STATUS__SHIFT,
			UVD_IPX_DLDO_STATUS__ONO4_PWR_STATUS_MASK);
		WREG32_SOC15(VCN, inst, regUVD_IPX_DLDO_CONFIG,
			2 << UVD_IPX_DLDO_CONFIG__ONO3_PWR_CONFIG__SHIFT);
		SOC15_WAIT_ON_RREG(VCN, inst, regUVD_IPX_DLDO_STATUS,
			1 << UVD_IPX_DLDO_STATUS__ONO3_PWR_STATUS__SHIFT,
			UVD_IPX_DLDO_STATUS__ONO3_PWR_STATUS_MASK);
		WREG32_SOC15(VCN, inst, regUVD_IPX_DLDO_CONFIG,
			2 << UVD_IPX_DLDO_CONFIG__ONO2_PWR_CONFIG__SHIFT);
		SOC15_WAIT_ON_RREG(VCN, inst, regUVD_IPX_DLDO_STATUS,
			1 << UVD_IPX_DLDO_STATUS__ONO2_PWR_STATUS__SHIFT,
			UVD_IPX_DLDO_STATUS__ONO2_PWR_STATUS_MASK);
	}
}

/**
 * vcn_v4_0_5_disable_clock_gating - disable VCN clock gating
 *
 * @vinst: VCN instance
 *
 * Disable clock gating for VCN block
 */
static void vcn_v4_0_5_disable_clock_gating(struct amdgpu_vcn_inst *vinst)
{
	struct amdgpu_device *adev = vinst->adev;
	int inst = vinst->inst;
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
 * vcn_v4_0_5_disable_clock_gating_dpg_mode - disable VCN clock gating dpg mode
 *
 * @vinst: VCN instance
 * @sram_sel: sram select
 * @indirect: indirectly write sram
 *
 * Disable clock gating for VCN block with dpg mode
 */
static void vcn_v4_0_5_disable_clock_gating_dpg_mode(struct amdgpu_vcn_inst *vinst,
						     uint8_t sram_sel,
						     uint8_t indirect)
{
	struct amdgpu_device *adev = vinst->adev;
	int inst_idx = vinst->inst;
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
 * vcn_v4_0_5_enable_clock_gating - enable VCN clock gating
 *
 * @vinst: VCN instance
 *
 * Enable clock gating for VCN block
 */
static void vcn_v4_0_5_enable_clock_gating(struct amdgpu_vcn_inst *vinst)
{
	struct amdgpu_device *adev = vinst->adev;
	int inst = vinst->inst;
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
}

/**
 * vcn_v4_0_5_start_dpg_mode - VCN start with dpg mode
 *
 * @vinst: VCN instance
 * @indirect: indirectly write sram
 *
 * Start VCN block with dpg mode
 */
static int vcn_v4_0_5_start_dpg_mode(struct amdgpu_vcn_inst *vinst,
				     bool indirect)
{
	struct amdgpu_device *adev = vinst->adev;
	int inst_idx = vinst->inst;
	struct amdgpu_vcn4_fw_shared *fw_shared = adev->vcn.inst[inst_idx].fw_shared.cpu_addr;
	struct amdgpu_ring *ring;
	uint32_t tmp;
	int ret;

	/* disable register anti-hang mechanism */
	WREG32_P(SOC15_REG_OFFSET(VCN, inst_idx, regUVD_POWER_STATUS), 1,
		~UVD_POWER_STATUS__UVD_POWER_STATUS_MASK);
	/* enable dynamic power gating mode */
	tmp = RREG32_SOC15(VCN, inst_idx, regUVD_POWER_STATUS);
	tmp |= UVD_POWER_STATUS__UVD_PG_MODE_MASK;
	tmp |= UVD_POWER_STATUS__UVD_PG_EN_MASK;
	WREG32_SOC15(VCN, inst_idx, regUVD_POWER_STATUS, tmp);

	if (indirect)
		adev->vcn.inst[inst_idx].dpg_sram_curr_addr =
					(uint32_t *)adev->vcn.inst[inst_idx].dpg_sram_cpu_addr;

	/* enable clock gating */
	vcn_v4_0_5_disable_clock_gating_dpg_mode(vinst, 0, indirect);

	/* enable VCPU clock */
	tmp = (0xFF << UVD_VCPU_CNTL__PRB_TIMEOUT_VAL__SHIFT);
	tmp |= UVD_VCPU_CNTL__CLK_EN_MASK | UVD_VCPU_CNTL__BLK_RST_MASK;
	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
		VCN, inst_idx, regUVD_VCPU_CNTL), tmp, 0, indirect);

	/* disable master interrupt */
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

	vcn_v4_0_5_mc_resume_dpg_mode(vinst, indirect);

	tmp = (0xFF << UVD_VCPU_CNTL__PRB_TIMEOUT_VAL__SHIFT);
	tmp |= UVD_VCPU_CNTL__CLK_EN_MASK;
	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
		VCN, inst_idx, regUVD_VCPU_CNTL), tmp, 0, indirect);

	/* enable LMI MC and UMC channels */
	tmp = 0x1f << UVD_LMI_CTRL2__RE_OFLD_MIF_WR_REQ_NUM__SHIFT;
	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
		VCN, inst_idx, regUVD_LMI_CTRL2), tmp, 0, indirect);

	/* enable master interrupt */
	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
		VCN, inst_idx, regUVD_MASTINT_EN),
		UVD_MASTINT_EN__VCPU_EN_MASK, 0, indirect);

	if (indirect) {
		ret = amdgpu_vcn_psp_update_sram(adev, inst_idx, 0);
		if (ret) {
			dev_err(adev->dev, "vcn sram load failed %d\n", ret);
			return ret;
		}
	}

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

	/* Keeping one read-back to ensure all register writes are done, otherwise
	 * it may introduce race conditions */
	RREG32_SOC15(VCN, inst_idx, regVCN_RB1_DB_CTRL);

	return 0;
}


/**
 * vcn_v4_0_5_start - VCN start
 *
 * @vinst: VCN instance
 *
 * Start VCN block
 */
static int vcn_v4_0_5_start(struct amdgpu_vcn_inst *vinst)
{
	struct amdgpu_device *adev = vinst->adev;
	int i = vinst->inst;
	struct amdgpu_vcn4_fw_shared *fw_shared;
	struct amdgpu_ring *ring;
	uint32_t tmp;
	int j, k, r;

	if (adev->vcn.harvest_config & (1 << i))
		return 0;

	if (adev->pm.dpm_enabled)
		amdgpu_dpm_enable_vcn(adev, true, i);

	fw_shared = adev->vcn.inst[i].fw_shared.cpu_addr;

	if (adev->pg_flags & AMD_PG_SUPPORT_VCN_DPG)
		return vcn_v4_0_5_start_dpg_mode(vinst, adev->vcn.inst[i].indirect_sram);

	/* disable VCN power gating */
	vcn_v4_0_5_disable_static_power_gating(vinst);

	/* set VCN status busy */
	tmp = RREG32_SOC15(VCN, i, regUVD_STATUS) | UVD_STATUS__UVD_BUSY;
	WREG32_SOC15(VCN, i, regUVD_STATUS, tmp);

	/* SW clock gating */
	vcn_v4_0_5_disable_clock_gating(vinst);

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

	vcn_v4_0_5_mc_resume(vinst);

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
			if (amdgpu_emu_mode == 1)
				msleep(1);
		}

		if (amdgpu_emu_mode == 1) {
			r = -1;
			if (status & 2) {
				r = 0;
				break;
			}
		} else {
			r = 0;
			if (status & 2)
				break;

			dev_err(adev->dev,
				"VCN[%d] is not responding, trying to reset VCPU!!!\n", i);
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

	/* Keeping one read-back to ensure all register writes are done, otherwise
	 * it may introduce race conditions */
	RREG32_SOC15(VCN, i, regVCN_RB_ENABLE);

	return 0;
}

/**
 * vcn_v4_0_5_stop_dpg_mode - VCN stop with dpg mode
 *
 * @vinst: VCN instance
 *
 * Stop VCN block with dpg mode
 */
static void vcn_v4_0_5_stop_dpg_mode(struct amdgpu_vcn_inst *vinst)
{
	struct amdgpu_device *adev = vinst->adev;
	int inst_idx = vinst->inst;
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

	/* Keeping one read-back to ensure all register writes are done,
	 * otherwise it may introduce race conditions.
	 */
	RREG32_SOC15(VCN, inst_idx, regUVD_STATUS);
}

/**
 * vcn_v4_0_5_stop - VCN stop
 *
 * @vinst: VCN instance
 *
 * Stop VCN block
 */
static int vcn_v4_0_5_stop(struct amdgpu_vcn_inst *vinst)
{
	struct amdgpu_device *adev = vinst->adev;
	int i = vinst->inst;
	struct amdgpu_vcn4_fw_shared *fw_shared;
	uint32_t tmp;
	int r = 0;

	if (adev->vcn.harvest_config & (1 << i))
		return 0;

	fw_shared = adev->vcn.inst[i].fw_shared.cpu_addr;
	fw_shared->sq.queue_mode |= FW_QUEUE_DPG_HOLD_OFF;

	if (adev->pg_flags & AMD_PG_SUPPORT_VCN_DPG) {
		vcn_v4_0_5_stop_dpg_mode(vinst);
		r = 0;
		goto done;
	}

	/* wait for vcn idle */
	r = SOC15_WAIT_ON_RREG(VCN, i, regUVD_STATUS, UVD_STATUS__IDLE, 0x7);
	if (r)
		goto done;

	tmp = UVD_LMI_STATUS__VCPU_LMI_WRITE_CLEAN_MASK |
		UVD_LMI_STATUS__READ_CLEAN_MASK |
		UVD_LMI_STATUS__WRITE_CLEAN_MASK |
		UVD_LMI_STATUS__WRITE_CLEAN_RAW_MASK;
	r = SOC15_WAIT_ON_RREG(VCN, i, regUVD_LMI_STATUS, tmp, tmp);
	if (r)
		goto done;

	/* disable LMI UMC channel */
	tmp = RREG32_SOC15(VCN, i, regUVD_LMI_CTRL2);
	tmp |= UVD_LMI_CTRL2__STALL_ARB_UMC_MASK;
	WREG32_SOC15(VCN, i, regUVD_LMI_CTRL2, tmp);
	tmp = UVD_LMI_STATUS__UMC_READ_CLEAN_RAW_MASK |
		UVD_LMI_STATUS__UMC_WRITE_CLEAN_RAW_MASK;
	r = SOC15_WAIT_ON_RREG(VCN, i, regUVD_LMI_STATUS, tmp, tmp);
	if (r)
		goto done;

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
	vcn_v4_0_5_enable_clock_gating(vinst);

	/* enable VCN power gating */
	vcn_v4_0_5_enable_static_power_gating(vinst);

	/* Keeping one read-back to ensure all register writes are done,
	 * otherwise it may introduce race conditions.
	 */
	RREG32_SOC15(VCN, i, regUVD_STATUS);

done:
	if (adev->pm.dpm_enabled)
		amdgpu_dpm_enable_vcn(adev, false, i);

	return r;
}

/**
 * vcn_v4_0_5_pause_dpg_mode - VCN pause with dpg mode
 *
 * @vinst: VCN instance
 * @new_state: pause state
 *
 * Pause dpg mode for VCN block
 */
static int vcn_v4_0_5_pause_dpg_mode(struct amdgpu_vcn_inst *vinst,
				     struct dpg_pause_state *new_state)
{
	struct amdgpu_device *adev = vinst->adev;
	int inst_idx = vinst->inst;
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
					UVD_PGFSM_CONFIG__UVDM_UVDU_PWR_ON,
					UVD_POWER_STATUS__UVD_POWER_STATUS_MASK);
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
 * vcn_v4_0_5_unified_ring_get_rptr - get unified read pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Returns the current hardware unified read pointer
 */
static uint64_t vcn_v4_0_5_unified_ring_get_rptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring != &adev->vcn.inst[ring->me].ring_enc[0])
		DRM_ERROR("wrong ring id is identified in %s", __func__);

	return RREG32_SOC15(VCN, ring->me, regUVD_RB_RPTR);
}

/**
 * vcn_v4_0_5_unified_ring_get_wptr - get unified write pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Returns the current hardware unified write pointer
 */
static uint64_t vcn_v4_0_5_unified_ring_get_wptr(struct amdgpu_ring *ring)
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
 * vcn_v4_0_5_unified_ring_set_wptr - set enc write pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Commits the enc write pointer to the hardware
 */
static void vcn_v4_0_5_unified_ring_set_wptr(struct amdgpu_ring *ring)
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

static int vcn_v4_0_5_ring_reset(struct amdgpu_ring *ring,
				 unsigned int vmid,
				 struct amdgpu_fence *timedout_fence)
{
	struct amdgpu_device *adev = ring->adev;
	struct amdgpu_vcn_inst *vinst = &adev->vcn.inst[ring->me];
	int r;

	amdgpu_ring_reset_helper_begin(ring, timedout_fence);
	r = vcn_v4_0_5_stop(vinst);
	if (r)
		return r;
	r = vcn_v4_0_5_start(vinst);
	if (r)
		return r;
	return amdgpu_ring_reset_helper_end(ring, timedout_fence);
}

static struct amdgpu_ring_funcs vcn_v4_0_5_unified_ring_vm_funcs = {
	.type = AMDGPU_RING_TYPE_VCN_ENC,
	.align_mask = 0x3f,
	.nop = VCN_ENC_CMD_NO_OP,
	.get_rptr = vcn_v4_0_5_unified_ring_get_rptr,
	.get_wptr = vcn_v4_0_5_unified_ring_get_wptr,
	.set_wptr = vcn_v4_0_5_unified_ring_set_wptr,
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
	.reset = vcn_v4_0_5_ring_reset,
};

/**
 * vcn_v4_0_5_set_unified_ring_funcs - set unified ring functions
 *
 * @adev: amdgpu_device pointer
 *
 * Set unified ring functions
 */
static void vcn_v4_0_5_set_unified_ring_funcs(struct amdgpu_device *adev)
{
	int i;

	for (i = 0; i < adev->vcn.num_vcn_inst; ++i) {
		if (adev->vcn.harvest_config & (1 << i))
			continue;

		if (amdgpu_ip_version(adev, VCN_HWIP, 0) == IP_VERSION(4, 0, 5))
			vcn_v4_0_5_unified_ring_vm_funcs.secure_submission_supported = true;

		adev->vcn.inst[i].ring_enc[0].funcs = &vcn_v4_0_5_unified_ring_vm_funcs;
		adev->vcn.inst[i].ring_enc[0].me = i;
	}
}

/**
 * vcn_v4_0_5_is_idle - check VCN block is idle
 *
 * @ip_block: Pointer to the amdgpu_ip_block structure
 *
 * Check whether VCN block is idle
 */
static bool vcn_v4_0_5_is_idle(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	int i, ret = 1;

	for (i = 0; i < adev->vcn.num_vcn_inst; ++i) {
		if (adev->vcn.harvest_config & (1 << i))
			continue;

		ret &= (RREG32_SOC15(VCN, i, regUVD_STATUS) == UVD_STATUS__IDLE);
	}

	return ret;
}

/**
 * vcn_v4_0_5_wait_for_idle - wait for VCN block idle
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 *
 * Wait for VCN block idle
 */
static int vcn_v4_0_5_wait_for_idle(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
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
 * vcn_v4_0_5_set_clockgating_state - set VCN block clockgating state
 *
 * @ip_block: amdgpu_ip_block pointer
 * @state: clock gating state
 *
 * Set VCN block clockgating state
 */
static int vcn_v4_0_5_set_clockgating_state(struct amdgpu_ip_block *ip_block,
					  enum amd_clockgating_state state)
{
	struct amdgpu_device *adev = ip_block->adev;
	bool enable = state == AMD_CG_STATE_GATE;
	int i;

	for (i = 0; i < adev->vcn.num_vcn_inst; ++i) {
		struct amdgpu_vcn_inst *vinst = &adev->vcn.inst[i];

		if (adev->vcn.harvest_config & (1 << i))
			continue;

		if (enable) {
			if (RREG32_SOC15(VCN, i, regUVD_STATUS) != UVD_STATUS__IDLE)
				return -EBUSY;
			vcn_v4_0_5_enable_clock_gating(vinst);
		} else {
			vcn_v4_0_5_disable_clock_gating(vinst);
		}
	}

	return 0;
}

static int vcn_v4_0_5_set_pg_state(struct amdgpu_vcn_inst *vinst,
				   enum amd_powergating_state state)
{
	int ret = 0;

	if (state == vinst->cur_state)
		return 0;

	if (state == AMD_PG_STATE_GATE)
		ret = vcn_v4_0_5_stop(vinst);
	else
		ret = vcn_v4_0_5_start(vinst);

	if (!ret)
		vinst->cur_state = state;

	return ret;
}

/**
 * vcn_v4_0_5_process_interrupt - process VCN block interrupt
 *
 * @adev: amdgpu_device pointer
 * @source: interrupt sources
 * @entry: interrupt entry from clients and sources
 *
 * Process VCN block interrupt
 */
static int vcn_v4_0_5_process_interrupt(struct amdgpu_device *adev, struct amdgpu_irq_src *source,
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

static const struct amdgpu_irq_src_funcs vcn_v4_0_5_irq_funcs = {
	.process = vcn_v4_0_5_process_interrupt,
};

/**
 * vcn_v4_0_5_set_irq_funcs - set VCN block interrupt irq functions
 *
 * @adev: amdgpu_device pointer
 *
 * Set VCN block interrupt irq functions
 */
static void vcn_v4_0_5_set_irq_funcs(struct amdgpu_device *adev)
{
	int i;

	for (i = 0; i < adev->vcn.num_vcn_inst; ++i) {
		if (adev->vcn.harvest_config & (1 << i))
			continue;

		adev->vcn.inst[i].irq.num_types = adev->vcn.inst[i].num_enc_rings + 1;
		adev->vcn.inst[i].irq.funcs = &vcn_v4_0_5_irq_funcs;
	}
}

static const struct amd_ip_funcs vcn_v4_0_5_ip_funcs = {
	.name = "vcn_v4_0_5",
	.early_init = vcn_v4_0_5_early_init,
	.sw_init = vcn_v4_0_5_sw_init,
	.sw_fini = vcn_v4_0_5_sw_fini,
	.hw_init = vcn_v4_0_5_hw_init,
	.hw_fini = vcn_v4_0_5_hw_fini,
	.suspend = vcn_v4_0_5_suspend,
	.resume = vcn_v4_0_5_resume,
	.is_idle = vcn_v4_0_5_is_idle,
	.wait_for_idle = vcn_v4_0_5_wait_for_idle,
	.set_clockgating_state = vcn_v4_0_5_set_clockgating_state,
	.set_powergating_state = vcn_set_powergating_state,
	.dump_ip_state = amdgpu_vcn_dump_ip_state,
	.print_ip_state = amdgpu_vcn_print_ip_state,
};

const struct amdgpu_ip_block_version vcn_v4_0_5_ip_block = {
	.type = AMD_IP_BLOCK_TYPE_VCN,
	.major = 4,
	.minor = 0,
	.rev = 5,
	.funcs = &vcn_v4_0_5_ip_funcs,
};
