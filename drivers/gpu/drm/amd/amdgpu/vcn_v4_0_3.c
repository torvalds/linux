/*
 * Copyright 2022 Advanced Micro Devices, Inc.
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
#include <drm/drm_drv.h>

#include "amdgpu.h"
#include "amdgpu_vcn.h"
#include "amdgpu_pm.h"
#include "soc15.h"
#include "soc15d.h"
#include "soc15_hw_ip.h"
#include "vcn_v2_0.h"
#include "vcn_v4_0_3.h"
#include "mmsch_v4_0_3.h"

#include "vcn/vcn_4_0_3_offset.h"
#include "vcn/vcn_4_0_3_sh_mask.h"
#include "ivsrcid/vcn/irqsrcs_vcn_4_0.h"

#define mmUVD_DPG_LMA_CTL		regUVD_DPG_LMA_CTL
#define mmUVD_DPG_LMA_CTL_BASE_IDX	regUVD_DPG_LMA_CTL_BASE_IDX
#define mmUVD_DPG_LMA_DATA		regUVD_DPG_LMA_DATA
#define mmUVD_DPG_LMA_DATA_BASE_IDX	regUVD_DPG_LMA_DATA_BASE_IDX

#define VCN_VID_SOC_ADDRESS_2_0		0x1fb00
#define VCN1_VID_SOC_ADDRESS_3_0	0x48300
#define VCN1_AON_SOC_ADDRESS_3_0	0x48000

static const struct amdgpu_hwip_reg_entry vcn_reg_list_4_0_3[] = {
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

#define NORMALIZE_VCN_REG_OFFSET(offset) \
		(offset & 0x1FFFF)

static int vcn_v4_0_3_start_sriov(struct amdgpu_device *adev);
static void vcn_v4_0_3_set_unified_ring_funcs(struct amdgpu_device *adev);
static void vcn_v4_0_3_set_irq_funcs(struct amdgpu_device *adev);
static int vcn_v4_0_3_set_pg_state(struct amdgpu_vcn_inst *vinst,
				   enum amd_powergating_state state);
static int vcn_v4_0_3_pause_dpg_mode(struct amdgpu_vcn_inst *vinst,
				     struct dpg_pause_state *new_state);
static void vcn_v4_0_3_unified_ring_set_wptr(struct amdgpu_ring *ring);
static void vcn_v4_0_3_set_ras_funcs(struct amdgpu_device *adev);
static void vcn_v4_0_3_enable_ras(struct amdgpu_device *adev,
				  int inst_idx, bool indirect);

static inline bool vcn_v4_0_3_normalizn_reqd(struct amdgpu_device *adev)
{
	return (adev->vcn.caps & AMDGPU_VCN_CAPS(RRMT_ENABLED)) == 0;
}

/**
 * vcn_v4_0_3_early_init - set function pointers
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 *
 * Set ring and irq function pointers
 */
static int vcn_v4_0_3_early_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	int i, r;

	for (i = 0; i < adev->vcn.num_vcn_inst; ++i)
		/* re-use enc ring as unified ring */
		adev->vcn.inst[i].num_enc_rings = 1;

	vcn_v4_0_3_set_unified_ring_funcs(adev);
	vcn_v4_0_3_set_irq_funcs(adev);
	vcn_v4_0_3_set_ras_funcs(adev);

	for (i = 0; i < adev->vcn.num_vcn_inst; ++i) {
		adev->vcn.inst[i].set_pg_state = vcn_v4_0_3_set_pg_state;

		r = amdgpu_vcn_early_init(adev, i);
		if (r)
			return r;
	}

	return 0;
}

static int vcn_v4_0_3_fw_shared_init(struct amdgpu_device *adev, int inst_idx)
{
	struct amdgpu_vcn4_fw_shared *fw_shared;

	fw_shared = adev->vcn.inst[inst_idx].fw_shared.cpu_addr;
	fw_shared->present_flag_0 = cpu_to_le32(AMDGPU_FW_SHARED_FLAG_0_UNIFIED_QUEUE);
	fw_shared->sq.is_enabled = 1;

	if (amdgpu_vcnfw_log)
		amdgpu_vcn_fwlog_init(&adev->vcn.inst[inst_idx]);

	return 0;
}

/**
 * vcn_v4_0_3_sw_init - sw init for VCN block
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 *
 * Load firmware and sw initialization
 */
static int vcn_v4_0_3_sw_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	struct amdgpu_ring *ring;
	int i, r, vcn_inst;
	uint32_t reg_count = ARRAY_SIZE(vcn_reg_list_4_0_3);
	uint32_t *ptr;

	/* VCN DEC TRAP */
	r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_VCN,
		VCN_4_0__SRCID__UVD_ENC_GENERAL_PURPOSE, &adev->vcn.inst->irq);
	if (r)
		return r;

	for (i = 0; i < adev->vcn.num_vcn_inst; i++) {

		r = amdgpu_vcn_sw_init(adev, i);
		if (r)
			return r;

		amdgpu_vcn_setup_ucode(adev, i);

		r = amdgpu_vcn_resume(adev, i);
		if (r)
			return r;

		vcn_inst = GET_INST(VCN, i);

		ring = &adev->vcn.inst[i].ring_enc[0];
		ring->use_doorbell = true;

		if (!amdgpu_sriov_vf(adev))
			ring->doorbell_index =
				(adev->doorbell_index.vcn.vcn_ring0_1 << 1) +
				9 * vcn_inst;
		else
			ring->doorbell_index =
				(adev->doorbell_index.vcn.vcn_ring0_1 << 1) +
				32 * vcn_inst;

		ring->vm_hub = AMDGPU_MMHUB0(adev->vcn.inst[i].aid_id);
		sprintf(ring->name, "vcn_unified_%d", adev->vcn.inst[i].aid_id);
		r = amdgpu_ring_init(adev, ring, 512, &adev->vcn.inst->irq, 0,
				     AMDGPU_RING_PRIO_DEFAULT,
				     &adev->vcn.inst[i].sched_score);
		if (r)
			return r;

		vcn_v4_0_3_fw_shared_init(adev, i);

		if (adev->pg_flags & AMD_PG_SUPPORT_VCN_DPG)
			adev->vcn.inst[i].pause_dpg_mode = vcn_v4_0_3_pause_dpg_mode;
	}

	/* TODO: Add queue reset mask when FW fully supports it */
	adev->vcn.supported_reset =
		amdgpu_get_soft_full_reset_mask(&adev->vcn.inst[0].ring_enc[0]);

	if (amdgpu_sriov_vf(adev)) {
		r = amdgpu_virt_alloc_mm_table(adev);
		if (r)
			return r;
	}

	if (amdgpu_ras_is_supported(adev, AMDGPU_RAS_BLOCK__VCN)) {
		r = amdgpu_vcn_ras_sw_init(adev);
		if (r) {
			dev_err(adev->dev, "Failed to initialize vcn ras block!\n");
			return r;
		}
	}

	/* Allocate memory for VCN IP Dump buffer */
	ptr = kcalloc(adev->vcn.num_vcn_inst * reg_count, sizeof(uint32_t), GFP_KERNEL);
	if (!ptr) {
		DRM_ERROR("Failed to allocate memory for VCN IP Dump\n");
		adev->vcn.ip_dump = NULL;
	} else {
		adev->vcn.ip_dump = ptr;
	}

	r = amdgpu_vcn_sysfs_reset_mask_init(adev);
	if (r)
		return r;

	return 0;
}

/**
 * vcn_v4_0_3_sw_fini - sw fini for VCN block
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 *
 * VCN suspend and free up sw allocation
 */
static int vcn_v4_0_3_sw_fini(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	int i, r, idx;

	if (drm_dev_enter(&adev->ddev, &idx)) {
		for (i = 0; i < adev->vcn.num_vcn_inst; i++) {
			volatile struct amdgpu_vcn4_fw_shared *fw_shared;

			fw_shared = adev->vcn.inst[i].fw_shared.cpu_addr;
			fw_shared->present_flag_0 = 0;
			fw_shared->sq.is_enabled = cpu_to_le32(false);
		}
		drm_dev_exit(idx);
	}

	if (amdgpu_sriov_vf(adev))
		amdgpu_virt_free_mm_table(adev);

	for (i = 0; i < adev->vcn.num_vcn_inst; i++) {
		r = amdgpu_vcn_suspend(adev, i);
		if (r)
			return r;
	}

	amdgpu_vcn_sysfs_reset_mask_fini(adev);

	for (i = 0; i < adev->vcn.num_vcn_inst; i++) {
		r = amdgpu_vcn_sw_fini(adev, i);
		if (r)
			return r;
	}

	kfree(adev->vcn.ip_dump);

	return 0;
}

/**
 * vcn_v4_0_3_hw_init - start and test VCN block
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 *
 * Initialize the hardware, boot up the VCPU and do some testing
 */
static int vcn_v4_0_3_hw_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	struct amdgpu_ring *ring;
	int i, r, vcn_inst;

	if (amdgpu_sriov_vf(adev)) {
		r = vcn_v4_0_3_start_sriov(adev);
		if (r)
			return r;

		for (i = 0; i < adev->vcn.num_vcn_inst; ++i) {
			ring = &adev->vcn.inst[i].ring_enc[0];
			ring->wptr = 0;
			ring->wptr_old = 0;
			vcn_v4_0_3_unified_ring_set_wptr(ring);
			ring->sched.ready = true;
		}
	} else {
		/* This flag is not set for VF, assumed to be disabled always */
		if (RREG32_SOC15(VCN, GET_INST(VCN, 0), regVCN_RRMT_CNTL) &
		    0x100)
			adev->vcn.caps |= AMDGPU_VCN_CAPS(RRMT_ENABLED);

		for (i = 0; i < adev->vcn.num_vcn_inst; ++i) {
			struct amdgpu_vcn4_fw_shared *fw_shared;

			vcn_inst = GET_INST(VCN, i);
			ring = &adev->vcn.inst[i].ring_enc[0];

			if (ring->use_doorbell) {
				adev->nbio.funcs->vcn_doorbell_range(
					adev, ring->use_doorbell,
					(adev->doorbell_index.vcn.vcn_ring0_1 << 1) +
						9 * vcn_inst,
					adev->vcn.inst[i].aid_id);

				WREG32_SOC15(
					VCN, GET_INST(VCN, ring->me),
					regVCN_RB1_DB_CTRL,
					ring->doorbell_index
							<< VCN_RB1_DB_CTRL__OFFSET__SHIFT |
						VCN_RB1_DB_CTRL__EN_MASK);

				/* Read DB_CTRL to flush the write DB_CTRL command. */
				RREG32_SOC15(
					VCN, GET_INST(VCN, ring->me),
					regVCN_RB1_DB_CTRL);
			}

			/* Re-init fw_shared when RAS fatal error occurred */
			fw_shared = adev->vcn.inst[i].fw_shared.cpu_addr;
			if (!fw_shared->sq.is_enabled)
				vcn_v4_0_3_fw_shared_init(adev, i);

			r = amdgpu_ring_test_helper(ring);
			if (r)
				return r;
		}
	}

	return r;
}

/**
 * vcn_v4_0_3_hw_fini - stop the hardware block
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 *
 * Stop the VCN block, mark ring as not ready any more
 */
static int vcn_v4_0_3_hw_fini(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	int i;

	for (i = 0; i < adev->vcn.num_vcn_inst; ++i) {
		struct amdgpu_vcn_inst *vinst = &adev->vcn.inst[i];

		cancel_delayed_work_sync(&vinst->idle_work);

		if (vinst->cur_state != AMD_PG_STATE_GATE)
			vinst->set_pg_state(vinst, AMD_PG_STATE_GATE);
	}

	return 0;
}

/**
 * vcn_v4_0_3_suspend - suspend VCN block
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 *
 * HW fini and suspend VCN block
 */
static int vcn_v4_0_3_suspend(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	int r, i;

	r = vcn_v4_0_3_hw_fini(ip_block);
	if (r)
		return r;

	for (i = 0; i < adev->vcn.num_vcn_inst; i++) {
		r = amdgpu_vcn_suspend(adev, i);
		if (r)
			return r;
	}

	return 0;
}

/**
 * vcn_v4_0_3_resume - resume VCN block
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 *
 * Resume firmware and hw init VCN block
 */
static int vcn_v4_0_3_resume(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	int r, i;

	for (i = 0; i < adev->vcn.num_vcn_inst; i++) {
		r = amdgpu_vcn_resume(ip_block->adev, i);
		if (r)
			return r;
	}

	r = vcn_v4_0_3_hw_init(ip_block);

	return r;
}

/**
 * vcn_v4_0_3_mc_resume - memory controller programming
 *
 * @vinst: VCN instance
 *
 * Let the VCN memory controller know it's offsets
 */
static void vcn_v4_0_3_mc_resume(struct amdgpu_vcn_inst *vinst)
{
	struct amdgpu_device *adev = vinst->adev;
	int inst_idx = vinst->inst;
	uint32_t offset, size, vcn_inst;
	const struct common_firmware_header *hdr;

	hdr = (const struct common_firmware_header *)adev->vcn.inst[inst_idx].fw->data;
	size = AMDGPU_GPU_PAGE_ALIGN(le32_to_cpu(hdr->ucode_size_bytes) + 8);

	vcn_inst = GET_INST(VCN, inst_idx);
	/* cache window 0: fw */
	if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) {
		WREG32_SOC15(
			VCN, vcn_inst, regUVD_LMI_VCPU_CACHE_64BIT_BAR_LOW,
			(adev->firmware.ucode[AMDGPU_UCODE_ID_VCN + inst_idx]
				 .tmr_mc_addr_lo));
		WREG32_SOC15(
			VCN, vcn_inst, regUVD_LMI_VCPU_CACHE_64BIT_BAR_HIGH,
			(adev->firmware.ucode[AMDGPU_UCODE_ID_VCN + inst_idx]
				 .tmr_mc_addr_hi));
		WREG32_SOC15(VCN, vcn_inst, regUVD_VCPU_CACHE_OFFSET0, 0);
		offset = 0;
	} else {
		WREG32_SOC15(VCN, vcn_inst, regUVD_LMI_VCPU_CACHE_64BIT_BAR_LOW,
			     lower_32_bits(adev->vcn.inst[inst_idx].gpu_addr));
		WREG32_SOC15(VCN, vcn_inst,
			     regUVD_LMI_VCPU_CACHE_64BIT_BAR_HIGH,
			     upper_32_bits(adev->vcn.inst[inst_idx].gpu_addr));
		offset = size;
		WREG32_SOC15(VCN, vcn_inst, regUVD_VCPU_CACHE_OFFSET0,
			     AMDGPU_UVD_FIRMWARE_OFFSET >> 3);
	}
	WREG32_SOC15(VCN, vcn_inst, regUVD_VCPU_CACHE_SIZE0, size);

	/* cache window 1: stack */
	WREG32_SOC15(VCN, vcn_inst, regUVD_LMI_VCPU_CACHE1_64BIT_BAR_LOW,
		     lower_32_bits(adev->vcn.inst[inst_idx].gpu_addr + offset));
	WREG32_SOC15(VCN, vcn_inst, regUVD_LMI_VCPU_CACHE1_64BIT_BAR_HIGH,
		     upper_32_bits(adev->vcn.inst[inst_idx].gpu_addr + offset));
	WREG32_SOC15(VCN, vcn_inst, regUVD_VCPU_CACHE_OFFSET1, 0);
	WREG32_SOC15(VCN, vcn_inst, regUVD_VCPU_CACHE_SIZE1,
		     AMDGPU_VCN_STACK_SIZE);

	/* cache window 2: context */
	WREG32_SOC15(VCN, vcn_inst, regUVD_LMI_VCPU_CACHE2_64BIT_BAR_LOW,
		     lower_32_bits(adev->vcn.inst[inst_idx].gpu_addr + offset +
				   AMDGPU_VCN_STACK_SIZE));
	WREG32_SOC15(VCN, vcn_inst, regUVD_LMI_VCPU_CACHE2_64BIT_BAR_HIGH,
		     upper_32_bits(adev->vcn.inst[inst_idx].gpu_addr + offset +
				   AMDGPU_VCN_STACK_SIZE));
	WREG32_SOC15(VCN, vcn_inst, regUVD_VCPU_CACHE_OFFSET2, 0);
	WREG32_SOC15(VCN, vcn_inst, regUVD_VCPU_CACHE_SIZE2,
		     AMDGPU_VCN_CONTEXT_SIZE);

	/* non-cache window */
	WREG32_SOC15(
		VCN, vcn_inst, regUVD_LMI_VCPU_NC0_64BIT_BAR_LOW,
		lower_32_bits(adev->vcn.inst[inst_idx].fw_shared.gpu_addr));
	WREG32_SOC15(
		VCN, vcn_inst, regUVD_LMI_VCPU_NC0_64BIT_BAR_HIGH,
		upper_32_bits(adev->vcn.inst[inst_idx].fw_shared.gpu_addr));
	WREG32_SOC15(VCN, vcn_inst, regUVD_VCPU_NONCACHE_OFFSET0, 0);
	WREG32_SOC15(
		VCN, vcn_inst, regUVD_VCPU_NONCACHE_SIZE0,
		AMDGPU_GPU_PAGE_ALIGN(sizeof(struct amdgpu_vcn4_fw_shared)));
}

/**
 * vcn_v4_0_3_mc_resume_dpg_mode - memory controller programming for dpg mode
 *
 * @vinst: VCN instance
 * @indirect: indirectly write sram
 *
 * Let the VCN memory controller know it's offsets with dpg mode
 */
static void vcn_v4_0_3_mc_resume_dpg_mode(struct amdgpu_vcn_inst *vinst,
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
				VCN, 0, regUVD_LMI_VCPU_CACHE_64BIT_BAR_LOW),
				(adev->firmware.ucode[AMDGPU_UCODE_ID_VCN +
					inst_idx].tmr_mc_addr_lo), 0, indirect);
			WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
				VCN, 0, regUVD_LMI_VCPU_CACHE_64BIT_BAR_HIGH),
				(adev->firmware.ucode[AMDGPU_UCODE_ID_VCN +
					inst_idx].tmr_mc_addr_hi), 0, indirect);
			WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
				VCN, 0, regUVD_VCPU_CACHE_OFFSET0), 0, 0, indirect);
		} else {
			WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
				VCN, 0, regUVD_LMI_VCPU_CACHE_64BIT_BAR_LOW), 0, 0, indirect);
			WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
				VCN, 0, regUVD_LMI_VCPU_CACHE_64BIT_BAR_HIGH), 0, 0, indirect);
			WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
				VCN, 0, regUVD_VCPU_CACHE_OFFSET0), 0, 0, indirect);
		}
		offset = 0;
	} else {
		WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
			VCN, 0, regUVD_LMI_VCPU_CACHE_64BIT_BAR_LOW),
			lower_32_bits(adev->vcn.inst[inst_idx].gpu_addr), 0, indirect);
		WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
			VCN, 0, regUVD_LMI_VCPU_CACHE_64BIT_BAR_HIGH),
			upper_32_bits(adev->vcn.inst[inst_idx].gpu_addr), 0, indirect);
		offset = size;
		WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
			VCN, 0, regUVD_VCPU_CACHE_OFFSET0),
			AMDGPU_UVD_FIRMWARE_OFFSET >> 3, 0, indirect);
	}

	if (!indirect)
		WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
			VCN, 0, regUVD_VCPU_CACHE_SIZE0), size, 0, indirect);
	else
		WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
			VCN, 0, regUVD_VCPU_CACHE_SIZE0), 0, 0, indirect);

	/* cache window 1: stack */
	if (!indirect) {
		WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
			VCN, 0, regUVD_LMI_VCPU_CACHE1_64BIT_BAR_LOW),
			lower_32_bits(adev->vcn.inst[inst_idx].gpu_addr + offset), 0, indirect);
		WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
			VCN, 0, regUVD_LMI_VCPU_CACHE1_64BIT_BAR_HIGH),
			upper_32_bits(adev->vcn.inst[inst_idx].gpu_addr + offset), 0, indirect);
		WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
			VCN, 0, regUVD_VCPU_CACHE_OFFSET1), 0, 0, indirect);
	} else {
		WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
			VCN, 0, regUVD_LMI_VCPU_CACHE1_64BIT_BAR_LOW), 0, 0, indirect);
		WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
			VCN, 0, regUVD_LMI_VCPU_CACHE1_64BIT_BAR_HIGH), 0, 0, indirect);
		WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
			VCN, 0, regUVD_VCPU_CACHE_OFFSET1), 0, 0, indirect);
	}
	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
			VCN, 0, regUVD_VCPU_CACHE_SIZE1), AMDGPU_VCN_STACK_SIZE, 0, indirect);

	/* cache window 2: context */
	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
			VCN, 0, regUVD_LMI_VCPU_CACHE2_64BIT_BAR_LOW),
			lower_32_bits(adev->vcn.inst[inst_idx].gpu_addr + offset +
				AMDGPU_VCN_STACK_SIZE), 0, indirect);
	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
			VCN, 0, regUVD_LMI_VCPU_CACHE2_64BIT_BAR_HIGH),
			upper_32_bits(adev->vcn.inst[inst_idx].gpu_addr + offset +
				AMDGPU_VCN_STACK_SIZE), 0, indirect);
	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
			VCN, 0, regUVD_VCPU_CACHE_OFFSET2), 0, 0, indirect);
	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
			VCN, 0, regUVD_VCPU_CACHE_SIZE2), AMDGPU_VCN_CONTEXT_SIZE, 0, indirect);

	/* non-cache window */
	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
			VCN, 0, regUVD_LMI_VCPU_NC0_64BIT_BAR_LOW),
			lower_32_bits(adev->vcn.inst[inst_idx].fw_shared.gpu_addr), 0, indirect);
	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
			VCN, 0, regUVD_LMI_VCPU_NC0_64BIT_BAR_HIGH),
			upper_32_bits(adev->vcn.inst[inst_idx].fw_shared.gpu_addr), 0, indirect);
	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
			VCN, 0, regUVD_VCPU_NONCACHE_OFFSET0), 0, 0, indirect);
	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
			VCN, 0, regUVD_VCPU_NONCACHE_SIZE0),
			AMDGPU_GPU_PAGE_ALIGN(sizeof(struct amdgpu_vcn4_fw_shared)), 0, indirect);

	/* VCN global tiling registers */
	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
		VCN, 0, regUVD_GFX8_ADDR_CONFIG), adev->gfx.config.gb_addr_config, 0, indirect);
	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
		VCN, 0, regUVD_GFX10_ADDR_CONFIG), adev->gfx.config.gb_addr_config, 0, indirect);
}

/**
 * vcn_v4_0_3_disable_clock_gating - disable VCN clock gating
 *
 * @vinst: VCN instance
 *
 * Disable clock gating for VCN block
 */
static void vcn_v4_0_3_disable_clock_gating(struct amdgpu_vcn_inst *vinst)
{
	struct amdgpu_device *adev = vinst->adev;
	int inst_idx = vinst->inst;
	uint32_t data;
	int vcn_inst;

	if (adev->cg_flags & AMD_CG_SUPPORT_VCN_MGCG)
		return;

	vcn_inst = GET_INST(VCN, inst_idx);

	/* VCN disable CGC */
	data = RREG32_SOC15(VCN, vcn_inst, regUVD_CGC_CTRL);
	data &= ~UVD_CGC_CTRL__DYN_CLOCK_MODE_MASK;
	data |= 1 << UVD_CGC_CTRL__CLK_GATE_DLY_TIMER__SHIFT;
	data |= 4 << UVD_CGC_CTRL__CLK_OFF_DELAY__SHIFT;
	WREG32_SOC15(VCN, vcn_inst, regUVD_CGC_CTRL, data);

	data = RREG32_SOC15(VCN, vcn_inst, regUVD_CGC_GATE);
	data &= ~(UVD_CGC_GATE__SYS_MASK
		| UVD_CGC_GATE__MPEG2_MASK
		| UVD_CGC_GATE__REGS_MASK
		| UVD_CGC_GATE__RBC_MASK
		| UVD_CGC_GATE__LMI_MC_MASK
		| UVD_CGC_GATE__LMI_UMC_MASK
		| UVD_CGC_GATE__MPC_MASK
		| UVD_CGC_GATE__LBSI_MASK
		| UVD_CGC_GATE__LRBBM_MASK
		| UVD_CGC_GATE__WCB_MASK
		| UVD_CGC_GATE__VCPU_MASK
		| UVD_CGC_GATE__MMSCH_MASK);

	WREG32_SOC15(VCN, vcn_inst, regUVD_CGC_GATE, data);
	SOC15_WAIT_ON_RREG(VCN, vcn_inst, regUVD_CGC_GATE, 0, 0xFFFFFFFF);

	data = RREG32_SOC15(VCN, vcn_inst, regUVD_CGC_CTRL);
	data &= ~(UVD_CGC_CTRL__SYS_MODE_MASK
		| UVD_CGC_CTRL__MPEG2_MODE_MASK
		| UVD_CGC_CTRL__REGS_MODE_MASK
		| UVD_CGC_CTRL__RBC_MODE_MASK
		| UVD_CGC_CTRL__LMI_MC_MODE_MASK
		| UVD_CGC_CTRL__LMI_UMC_MODE_MASK
		| UVD_CGC_CTRL__MPC_MODE_MASK
		| UVD_CGC_CTRL__LBSI_MODE_MASK
		| UVD_CGC_CTRL__LRBBM_MODE_MASK
		| UVD_CGC_CTRL__WCB_MODE_MASK
		| UVD_CGC_CTRL__VCPU_MODE_MASK
		| UVD_CGC_CTRL__MMSCH_MODE_MASK);
	WREG32_SOC15(VCN, vcn_inst, regUVD_CGC_CTRL, data);

	data = RREG32_SOC15(VCN, vcn_inst, regUVD_SUVD_CGC_GATE);
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
		| UVD_SUVD_CGC_GATE__ENT_MASK
		| UVD_SUVD_CGC_GATE__SIT_HEVC_DEC_MASK
		| UVD_SUVD_CGC_GATE__SITE_MASK
		| UVD_SUVD_CGC_GATE__SRE_VP9_MASK
		| UVD_SUVD_CGC_GATE__SCM_VP9_MASK
		| UVD_SUVD_CGC_GATE__SIT_VP9_DEC_MASK
		| UVD_SUVD_CGC_GATE__SDB_VP9_MASK
		| UVD_SUVD_CGC_GATE__IME_HEVC_MASK);
	WREG32_SOC15(VCN, vcn_inst, regUVD_SUVD_CGC_GATE, data);

	data = RREG32_SOC15(VCN, vcn_inst, regUVD_SUVD_CGC_CTRL);
	data &= ~(UVD_SUVD_CGC_CTRL__SRE_MODE_MASK
		| UVD_SUVD_CGC_CTRL__SIT_MODE_MASK
		| UVD_SUVD_CGC_CTRL__SMP_MODE_MASK
		| UVD_SUVD_CGC_CTRL__SCM_MODE_MASK
		| UVD_SUVD_CGC_CTRL__SDB_MODE_MASK
		| UVD_SUVD_CGC_CTRL__ENT_MODE_MASK
		| UVD_SUVD_CGC_CTRL__IME_MODE_MASK
		| UVD_SUVD_CGC_CTRL__SITE_MODE_MASK);
	WREG32_SOC15(VCN, vcn_inst, regUVD_SUVD_CGC_CTRL, data);
}

/**
 * vcn_v4_0_3_disable_clock_gating_dpg_mode - disable VCN clock gating dpg mode
 *
 * @vinst: VCN instance
 * @sram_sel: sram select
 * @indirect: indirectly write sram
 *
 * Disable clock gating for VCN block with dpg mode
 */
static void vcn_v4_0_3_disable_clock_gating_dpg_mode(struct amdgpu_vcn_inst *vinst,
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
	reg_data &= ~(UVD_CGC_CTRL__SYS_MODE_MASK |
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
		VCN, 0, regUVD_CGC_CTRL), reg_data, sram_sel, indirect);

	/* turn off clock gating */
	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
		VCN, 0, regUVD_CGC_GATE), 0, sram_sel, indirect);

	/* turn on SUVD clock gating */
	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
		VCN, 0, regUVD_SUVD_CGC_GATE), 1, sram_sel, indirect);

	/* turn on sw mode in UVD_SUVD_CGC_CTRL */
	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
		VCN, 0, regUVD_SUVD_CGC_CTRL), 0, sram_sel, indirect);
}

/**
 * vcn_v4_0_3_enable_clock_gating - enable VCN clock gating
 *
 * @vinst: VCN instance
 *
 * Enable clock gating for VCN block
 */
static void vcn_v4_0_3_enable_clock_gating(struct amdgpu_vcn_inst *vinst)
{
	struct amdgpu_device *adev = vinst->adev;
	int inst_idx = vinst->inst;
	uint32_t data;
	int vcn_inst;

	if (adev->cg_flags & AMD_CG_SUPPORT_VCN_MGCG)
		return;

	vcn_inst = GET_INST(VCN, inst_idx);

	/* enable VCN CGC */
	data = RREG32_SOC15(VCN, vcn_inst, regUVD_CGC_CTRL);
	data |= 0 << UVD_CGC_CTRL__DYN_CLOCK_MODE__SHIFT;
	data |= 1 << UVD_CGC_CTRL__CLK_GATE_DLY_TIMER__SHIFT;
	data |= 4 << UVD_CGC_CTRL__CLK_OFF_DELAY__SHIFT;
	WREG32_SOC15(VCN, vcn_inst, regUVD_CGC_CTRL, data);

	data = RREG32_SOC15(VCN, vcn_inst, regUVD_CGC_CTRL);
	data |= (UVD_CGC_CTRL__SYS_MODE_MASK
		| UVD_CGC_CTRL__MPEG2_MODE_MASK
		| UVD_CGC_CTRL__REGS_MODE_MASK
		| UVD_CGC_CTRL__RBC_MODE_MASK
		| UVD_CGC_CTRL__LMI_MC_MODE_MASK
		| UVD_CGC_CTRL__LMI_UMC_MODE_MASK
		| UVD_CGC_CTRL__MPC_MODE_MASK
		| UVD_CGC_CTRL__LBSI_MODE_MASK
		| UVD_CGC_CTRL__LRBBM_MODE_MASK
		| UVD_CGC_CTRL__WCB_MODE_MASK
		| UVD_CGC_CTRL__VCPU_MODE_MASK);
	WREG32_SOC15(VCN, vcn_inst, regUVD_CGC_CTRL, data);

	data = RREG32_SOC15(VCN, vcn_inst, regUVD_SUVD_CGC_CTRL);
	data |= (UVD_SUVD_CGC_CTRL__SRE_MODE_MASK
		| UVD_SUVD_CGC_CTRL__SIT_MODE_MASK
		| UVD_SUVD_CGC_CTRL__SMP_MODE_MASK
		| UVD_SUVD_CGC_CTRL__SCM_MODE_MASK
		| UVD_SUVD_CGC_CTRL__SDB_MODE_MASK
		| UVD_SUVD_CGC_CTRL__ENT_MODE_MASK
		| UVD_SUVD_CGC_CTRL__IME_MODE_MASK
		| UVD_SUVD_CGC_CTRL__SITE_MODE_MASK);
	WREG32_SOC15(VCN, vcn_inst, regUVD_SUVD_CGC_CTRL, data);
}

/**
 * vcn_v4_0_3_start_dpg_mode - VCN start with dpg mode
 *
 * @vinst: VCN instance
 * @indirect: indirectly write sram
 *
 * Start VCN block with dpg mode
 */
static int vcn_v4_0_3_start_dpg_mode(struct amdgpu_vcn_inst *vinst,
				     bool indirect)
{
	struct amdgpu_device *adev = vinst->adev;
	int inst_idx = vinst->inst;
	volatile struct amdgpu_vcn4_fw_shared *fw_shared =
						adev->vcn.inst[inst_idx].fw_shared.cpu_addr;
	struct amdgpu_ring *ring;
	int vcn_inst;
	uint32_t tmp;

	vcn_inst = GET_INST(VCN, inst_idx);
	/* disable register anti-hang mechanism */
	WREG32_P(SOC15_REG_OFFSET(VCN, vcn_inst, regUVD_POWER_STATUS), 1,
		 ~UVD_POWER_STATUS__UVD_POWER_STATUS_MASK);
	/* enable dynamic power gating mode */
	tmp = RREG32_SOC15(VCN, vcn_inst, regUVD_POWER_STATUS);
	tmp |= UVD_POWER_STATUS__UVD_PG_MODE_MASK;
	tmp |= UVD_POWER_STATUS__UVD_PG_EN_MASK;
	WREG32_SOC15(VCN, vcn_inst, regUVD_POWER_STATUS, tmp);

	if (indirect) {
		DRM_DEV_DEBUG(adev->dev, "VCN %d start: on AID %d",
			inst_idx, adev->vcn.inst[inst_idx].aid_id);
		adev->vcn.inst[inst_idx].dpg_sram_curr_addr =
				(uint32_t *)adev->vcn.inst[inst_idx].dpg_sram_cpu_addr;
		/* Use dummy register 0xDEADBEEF passing AID selection to PSP FW */
		WREG32_SOC15_DPG_MODE(inst_idx, 0xDEADBEEF,
			adev->vcn.inst[inst_idx].aid_id, 0, true);
	}

	/* enable clock gating */
	vcn_v4_0_3_disable_clock_gating_dpg_mode(vinst, 0, indirect);

	/* enable VCPU clock */
	tmp = (0xFF << UVD_VCPU_CNTL__PRB_TIMEOUT_VAL__SHIFT);
	tmp |= UVD_VCPU_CNTL__CLK_EN_MASK;
	tmp |= UVD_VCPU_CNTL__BLK_RST_MASK;

	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
		VCN, 0, regUVD_VCPU_CNTL), tmp, 0, indirect);

	/* disable master interrupt */
	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
		VCN, 0, regUVD_MASTINT_EN), 0, 0, indirect);

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
		VCN, 0, regUVD_LMI_CTRL), tmp, 0, indirect);

	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
		VCN, 0, regUVD_MPC_CNTL),
		0x2 << UVD_MPC_CNTL__REPLACEMENT_MODE__SHIFT, 0, indirect);

	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
		VCN, 0, regUVD_MPC_SET_MUXA0),
		((0x1 << UVD_MPC_SET_MUXA0__VARA_1__SHIFT) |
		 (0x2 << UVD_MPC_SET_MUXA0__VARA_2__SHIFT) |
		 (0x3 << UVD_MPC_SET_MUXA0__VARA_3__SHIFT) |
		 (0x4 << UVD_MPC_SET_MUXA0__VARA_4__SHIFT)), 0, indirect);

	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
		VCN, 0, regUVD_MPC_SET_MUXB0),
		 ((0x1 << UVD_MPC_SET_MUXB0__VARB_1__SHIFT) |
		 (0x2 << UVD_MPC_SET_MUXB0__VARB_2__SHIFT) |
		 (0x3 << UVD_MPC_SET_MUXB0__VARB_3__SHIFT) |
		 (0x4 << UVD_MPC_SET_MUXB0__VARB_4__SHIFT)), 0, indirect);

	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
		VCN, 0, regUVD_MPC_SET_MUX),
		((0x0 << UVD_MPC_SET_MUX__SET_0__SHIFT) |
		 (0x1 << UVD_MPC_SET_MUX__SET_1__SHIFT) |
		 (0x2 << UVD_MPC_SET_MUX__SET_2__SHIFT)), 0, indirect);

	vcn_v4_0_3_mc_resume_dpg_mode(vinst, indirect);

	tmp = (0xFF << UVD_VCPU_CNTL__PRB_TIMEOUT_VAL__SHIFT);
	tmp |= UVD_VCPU_CNTL__CLK_EN_MASK;
	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
		VCN, 0, regUVD_VCPU_CNTL), tmp, 0, indirect);

	/* enable LMI MC and UMC channels */
	tmp = 0x1f << UVD_LMI_CTRL2__RE_OFLD_MIF_WR_REQ_NUM__SHIFT;
	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
		VCN, 0, regUVD_LMI_CTRL2), tmp, 0, indirect);

	vcn_v4_0_3_enable_ras(adev, inst_idx, indirect);

	/* enable master interrupt */
	WREG32_SOC15_DPG_MODE(inst_idx, SOC15_DPG_MODE_OFFSET(
		VCN, 0, regUVD_MASTINT_EN),
		UVD_MASTINT_EN__VCPU_EN_MASK, 0, indirect);

	if (indirect)
		amdgpu_vcn_psp_update_sram(adev, inst_idx, AMDGPU_UCODE_ID_VCN0_RAM);

	ring = &adev->vcn.inst[inst_idx].ring_enc[0];

	/* program the RB_BASE for ring buffer */
	WREG32_SOC15(VCN, vcn_inst, regUVD_RB_BASE_LO,
		     lower_32_bits(ring->gpu_addr));
	WREG32_SOC15(VCN, vcn_inst, regUVD_RB_BASE_HI,
		     upper_32_bits(ring->gpu_addr));

	WREG32_SOC15(VCN, vcn_inst, regUVD_RB_SIZE,
		     ring->ring_size / sizeof(uint32_t));

	/* resetting ring, fw should not check RB ring */
	tmp = RREG32_SOC15(VCN, vcn_inst, regVCN_RB_ENABLE);
	tmp &= ~(VCN_RB_ENABLE__RB_EN_MASK);
	WREG32_SOC15(VCN, vcn_inst, regVCN_RB_ENABLE, tmp);
	fw_shared->sq.queue_mode |= FW_QUEUE_RING_RESET;

	/* Initialize the ring buffer's read and write pointers */
	WREG32_SOC15(VCN, vcn_inst, regUVD_RB_RPTR, 0);
	WREG32_SOC15(VCN, vcn_inst, regUVD_RB_WPTR, 0);
	ring->wptr = RREG32_SOC15(VCN, vcn_inst, regUVD_RB_WPTR);

	tmp = RREG32_SOC15(VCN, vcn_inst, regVCN_RB_ENABLE);
	tmp |= VCN_RB_ENABLE__RB_EN_MASK;
	WREG32_SOC15(VCN, vcn_inst, regVCN_RB_ENABLE, tmp);
	fw_shared->sq.queue_mode &= ~(FW_QUEUE_RING_RESET | FW_QUEUE_DPG_HOLD_OFF);

	/*resetting done, fw can check RB ring */
	fw_shared->sq.queue_mode &= cpu_to_le32(~FW_QUEUE_RING_RESET);

	return 0;
}

static int vcn_v4_0_3_start_sriov(struct amdgpu_device *adev)
{
	int i, vcn_inst;
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
	struct mmsch_v4_0_3_init_header header;

	volatile struct amdgpu_vcn4_fw_shared *fw_shared;
	volatile struct amdgpu_fw_shared_rb_setup *rb_setup;

	direct_wt.cmd_header.command_type =
		MMSCH_COMMAND__DIRECT_REG_WRITE;
	direct_rd_mod_wt.cmd_header.command_type =
		MMSCH_COMMAND__DIRECT_REG_READ_MODIFY_WRITE;
	end.cmd_header.command_type = MMSCH_COMMAND__END;

	for (i = 0; i < adev->vcn.num_vcn_inst; i++) {
		vcn_inst = GET_INST(VCN, i);

		vcn_v4_0_3_fw_shared_init(adev, vcn_inst);

		memset(&header, 0, sizeof(struct mmsch_v4_0_3_init_header));
		header.version = MMSCH_VERSION;
		header.total_size = sizeof(struct mmsch_v4_0_3_init_header) >> 2;

		table_loc = (uint32_t *)table->cpu_addr;
		table_loc += header.total_size;

		table_size = 0;

		MMSCH_V4_0_INSERT_DIRECT_RD_MOD_WT(SOC15_REG_OFFSET(VCN, 0, regUVD_STATUS),
			~UVD_STATUS__UVD_BUSY, UVD_STATUS__UVD_BUSY);

		cache_size = AMDGPU_GPU_PAGE_ALIGN(adev->vcn.inst[i].fw->size + 4);

		if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) {
			MMSCH_V4_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCN, 0,
				regUVD_LMI_VCPU_CACHE_64BIT_BAR_LOW),
				adev->firmware.ucode[AMDGPU_UCODE_ID_VCN + i].tmr_mc_addr_lo);

			MMSCH_V4_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCN, 0,
				regUVD_LMI_VCPU_CACHE_64BIT_BAR_HIGH),
				adev->firmware.ucode[AMDGPU_UCODE_ID_VCN + i].tmr_mc_addr_hi);

			offset = 0;
			MMSCH_V4_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCN, 0,
				regUVD_VCPU_CACHE_OFFSET0), 0);
		} else {
			MMSCH_V4_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCN, 0,
				regUVD_LMI_VCPU_CACHE_64BIT_BAR_LOW),
				lower_32_bits(adev->vcn.inst[i].gpu_addr));
			MMSCH_V4_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCN, 0,
				regUVD_LMI_VCPU_CACHE_64BIT_BAR_HIGH),
				upper_32_bits(adev->vcn.inst[i].gpu_addr));
			offset = cache_size;
			MMSCH_V4_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCN, 0,
				regUVD_VCPU_CACHE_OFFSET0),
				AMDGPU_UVD_FIRMWARE_OFFSET >> 3);
		}

		MMSCH_V4_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCN, 0,
			regUVD_VCPU_CACHE_SIZE0),
			cache_size);

		cache_addr = adev->vcn.inst[vcn_inst].gpu_addr + offset;
		MMSCH_V4_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCN, 0,
			regUVD_LMI_VCPU_CACHE1_64BIT_BAR_LOW), lower_32_bits(cache_addr));
		MMSCH_V4_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCN, 0,
			regUVD_LMI_VCPU_CACHE1_64BIT_BAR_HIGH), upper_32_bits(cache_addr));
		MMSCH_V4_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCN, 0,
			regUVD_VCPU_CACHE_OFFSET1), 0);
		MMSCH_V4_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCN, 0,
			regUVD_VCPU_CACHE_SIZE1), AMDGPU_VCN_STACK_SIZE);

		cache_addr = adev->vcn.inst[vcn_inst].gpu_addr + offset +
			AMDGPU_VCN_STACK_SIZE;

		MMSCH_V4_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCN, 0,
			regUVD_LMI_VCPU_CACHE2_64BIT_BAR_LOW), lower_32_bits(cache_addr));

		MMSCH_V4_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCN, 0,
			regUVD_LMI_VCPU_CACHE2_64BIT_BAR_HIGH), upper_32_bits(cache_addr));

		MMSCH_V4_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCN, 0,
			regUVD_VCPU_CACHE_OFFSET2), 0);

		MMSCH_V4_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCN, 0,
			regUVD_VCPU_CACHE_SIZE2), AMDGPU_VCN_CONTEXT_SIZE);

		fw_shared = adev->vcn.inst[vcn_inst].fw_shared.cpu_addr;
		rb_setup = &fw_shared->rb_setup;

		ring_enc = &adev->vcn.inst[vcn_inst].ring_enc[0];
		ring_enc->wptr = 0;
		rb_enc_addr = ring_enc->gpu_addr;

		rb_setup->is_rb_enabled_flags |= RB_ENABLED;
		rb_setup->rb_addr_lo = lower_32_bits(rb_enc_addr);
		rb_setup->rb_addr_hi = upper_32_bits(rb_enc_addr);
		rb_setup->rb_size = ring_enc->ring_size / 4;
		fw_shared->present_flag_0 |= cpu_to_le32(AMDGPU_VCN_VF_RB_SETUP_FLAG);

		MMSCH_V4_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCN, 0,
			regUVD_LMI_VCPU_NC0_64BIT_BAR_LOW),
			lower_32_bits(adev->vcn.inst[vcn_inst].fw_shared.gpu_addr));
		MMSCH_V4_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCN, 0,
			regUVD_LMI_VCPU_NC0_64BIT_BAR_HIGH),
			upper_32_bits(adev->vcn.inst[vcn_inst].fw_shared.gpu_addr));
		MMSCH_V4_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(VCN, 0,
			regUVD_VCPU_NONCACHE_SIZE0),
			AMDGPU_GPU_PAGE_ALIGN(sizeof(struct amdgpu_vcn4_fw_shared)));
		MMSCH_V4_0_INSERT_END();

		header.vcn0.init_status = 0;
		header.vcn0.table_offset = header.total_size;
		header.vcn0.table_size = table_size;
		header.total_size += table_size;

		/* Send init table to mmsch */
		size = sizeof(struct mmsch_v4_0_3_init_header);
		table_loc = (uint32_t *)table->cpu_addr;
		memcpy((void *)table_loc, &header, size);

		ctx_addr = table->gpu_addr;
		WREG32_SOC15(VCN, vcn_inst, regMMSCH_VF_CTX_ADDR_LO, lower_32_bits(ctx_addr));
		WREG32_SOC15(VCN, vcn_inst, regMMSCH_VF_CTX_ADDR_HI, upper_32_bits(ctx_addr));

		tmp = RREG32_SOC15(VCN, vcn_inst, regMMSCH_VF_VMID);
		tmp &= ~MMSCH_VF_VMID__VF_CTX_VMID_MASK;
		tmp |= (0 << MMSCH_VF_VMID__VF_CTX_VMID__SHIFT);
		WREG32_SOC15(VCN, vcn_inst, regMMSCH_VF_VMID, tmp);

		size = header.total_size;
		WREG32_SOC15(VCN, vcn_inst, regMMSCH_VF_CTX_SIZE, size);

		WREG32_SOC15(VCN, vcn_inst, regMMSCH_VF_MAILBOX_RESP, 0);

		param = 0x00000001;
		WREG32_SOC15(VCN, vcn_inst, regMMSCH_VF_MAILBOX_HOST, param);
		tmp = 0;
		timeout = 1000;
		resp = 0;
		expected = MMSCH_VF_MAILBOX_RESP__OK;
		while (resp != expected) {
			resp = RREG32_SOC15(VCN, vcn_inst, regMMSCH_VF_MAILBOX_RESP);
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
		init_status = ((struct mmsch_v4_0_3_init_header *)(table_loc))->vcn0.init_status;
		if (resp != expected && resp != MMSCH_VF_MAILBOX_RESP__INCOMPLETE
					&& init_status != MMSCH_VF_ENGINE_STATUS__PASS) {
			DRM_ERROR("MMSCH init status is incorrect! readback=0x%08x, header init "\
				"status for VCN%x: 0x%x\n", resp, enabled_vcn, init_status);
		}
	}

	return 0;
}

/**
 * vcn_v4_0_3_start - VCN start
 *
 * @vinst: VCN instance
 *
 * Start VCN block
 */
static int vcn_v4_0_3_start(struct amdgpu_vcn_inst *vinst)
{
	struct amdgpu_device *adev = vinst->adev;
	int i = vinst->inst;
	volatile struct amdgpu_vcn4_fw_shared *fw_shared;
	struct amdgpu_ring *ring;
	int j, k, r, vcn_inst;
	uint32_t tmp;

	if (adev->pg_flags & AMD_PG_SUPPORT_VCN_DPG)
		return vcn_v4_0_3_start_dpg_mode(vinst, adev->vcn.inst[i].indirect_sram);

	vcn_inst = GET_INST(VCN, i);
	/* set VCN status busy */
	tmp = RREG32_SOC15(VCN, vcn_inst, regUVD_STATUS) |
		UVD_STATUS__UVD_BUSY;
	WREG32_SOC15(VCN, vcn_inst, regUVD_STATUS, tmp);

	/* SW clock gating */
	vcn_v4_0_3_disable_clock_gating(vinst);

	/* enable VCPU clock */
	WREG32_P(SOC15_REG_OFFSET(VCN, vcn_inst, regUVD_VCPU_CNTL),
		 UVD_VCPU_CNTL__CLK_EN_MASK,
		 ~UVD_VCPU_CNTL__CLK_EN_MASK);

	/* disable master interrupt */
	WREG32_P(SOC15_REG_OFFSET(VCN, vcn_inst, regUVD_MASTINT_EN), 0,
		 ~UVD_MASTINT_EN__VCPU_EN_MASK);

	/* enable LMI MC and UMC channels */
	WREG32_P(SOC15_REG_OFFSET(VCN, vcn_inst, regUVD_LMI_CTRL2), 0,
		 ~UVD_LMI_CTRL2__STALL_ARB_UMC_MASK);

	tmp = RREG32_SOC15(VCN, vcn_inst, regUVD_SOFT_RESET);
	tmp &= ~UVD_SOFT_RESET__LMI_SOFT_RESET_MASK;
	tmp &= ~UVD_SOFT_RESET__LMI_UMC_SOFT_RESET_MASK;
	WREG32_SOC15(VCN, vcn_inst, regUVD_SOFT_RESET, tmp);

	/* setup regUVD_LMI_CTRL */
	tmp = RREG32_SOC15(VCN, vcn_inst, regUVD_LMI_CTRL);
	WREG32_SOC15(VCN, vcn_inst, regUVD_LMI_CTRL,
		     tmp | UVD_LMI_CTRL__WRITE_CLEAN_TIMER_EN_MASK |
		     UVD_LMI_CTRL__MASK_MC_URGENT_MASK |
		     UVD_LMI_CTRL__DATA_COHERENCY_EN_MASK |
		     UVD_LMI_CTRL__VCPU_DATA_COHERENCY_EN_MASK);

	/* setup regUVD_MPC_CNTL */
	tmp = RREG32_SOC15(VCN, vcn_inst, regUVD_MPC_CNTL);
	tmp &= ~UVD_MPC_CNTL__REPLACEMENT_MODE_MASK;
	tmp |= 0x2 << UVD_MPC_CNTL__REPLACEMENT_MODE__SHIFT;
	WREG32_SOC15(VCN, vcn_inst, regUVD_MPC_CNTL, tmp);

	/* setup UVD_MPC_SET_MUXA0 */
	WREG32_SOC15(VCN, vcn_inst, regUVD_MPC_SET_MUXA0,
		     ((0x1 << UVD_MPC_SET_MUXA0__VARA_1__SHIFT) |
		      (0x2 << UVD_MPC_SET_MUXA0__VARA_2__SHIFT) |
		      (0x3 << UVD_MPC_SET_MUXA0__VARA_3__SHIFT) |
		      (0x4 << UVD_MPC_SET_MUXA0__VARA_4__SHIFT)));

	/* setup UVD_MPC_SET_MUXB0 */
	WREG32_SOC15(VCN, vcn_inst, regUVD_MPC_SET_MUXB0,
		     ((0x1 << UVD_MPC_SET_MUXB0__VARB_1__SHIFT) |
		      (0x2 << UVD_MPC_SET_MUXB0__VARB_2__SHIFT) |
		      (0x3 << UVD_MPC_SET_MUXB0__VARB_3__SHIFT) |
		      (0x4 << UVD_MPC_SET_MUXB0__VARB_4__SHIFT)));

	/* setup UVD_MPC_SET_MUX */
	WREG32_SOC15(VCN, vcn_inst, regUVD_MPC_SET_MUX,
		     ((0x0 << UVD_MPC_SET_MUX__SET_0__SHIFT) |
		      (0x1 << UVD_MPC_SET_MUX__SET_1__SHIFT) |
		      (0x2 << UVD_MPC_SET_MUX__SET_2__SHIFT)));

	vcn_v4_0_3_mc_resume(vinst);

	/* VCN global tiling registers */
	WREG32_SOC15(VCN, vcn_inst, regUVD_GFX8_ADDR_CONFIG,
		     adev->gfx.config.gb_addr_config);
	WREG32_SOC15(VCN, vcn_inst, regUVD_GFX10_ADDR_CONFIG,
		     adev->gfx.config.gb_addr_config);

	/* unblock VCPU register access */
	WREG32_P(SOC15_REG_OFFSET(VCN, vcn_inst, regUVD_RB_ARB_CTRL), 0,
		 ~UVD_RB_ARB_CTRL__VCPU_DIS_MASK);

	/* release VCPU reset to boot */
	WREG32_P(SOC15_REG_OFFSET(VCN, vcn_inst, regUVD_VCPU_CNTL), 0,
		 ~UVD_VCPU_CNTL__BLK_RST_MASK);

	for (j = 0; j < 10; ++j) {
		uint32_t status;

		for (k = 0; k < 100; ++k) {
			status = RREG32_SOC15(VCN, vcn_inst,
					      regUVD_STATUS);
			if (status & 2)
				break;
			mdelay(10);
		}
		r = 0;
		if (status & 2)
			break;

		DRM_DEV_ERROR(adev->dev,
			      "VCN decode not responding, trying to reset the VCPU!!!\n");
		WREG32_P(SOC15_REG_OFFSET(VCN, vcn_inst,
					  regUVD_VCPU_CNTL),
			 UVD_VCPU_CNTL__BLK_RST_MASK,
			 ~UVD_VCPU_CNTL__BLK_RST_MASK);
		mdelay(10);
		WREG32_P(SOC15_REG_OFFSET(VCN, vcn_inst,
					  regUVD_VCPU_CNTL),
			 0, ~UVD_VCPU_CNTL__BLK_RST_MASK);

		mdelay(10);
		r = -1;
	}

	if (r) {
		DRM_DEV_ERROR(adev->dev, "VCN decode not responding, giving up!!!\n");
		return r;
	}

	/* enable master interrupt */
	WREG32_P(SOC15_REG_OFFSET(VCN, vcn_inst, regUVD_MASTINT_EN),
		 UVD_MASTINT_EN__VCPU_EN_MASK,
		 ~UVD_MASTINT_EN__VCPU_EN_MASK);

	/* clear the busy bit of VCN_STATUS */
	WREG32_P(SOC15_REG_OFFSET(VCN, vcn_inst, regUVD_STATUS), 0,
		 ~(2 << UVD_STATUS__VCPU_REPORT__SHIFT));

	ring = &adev->vcn.inst[i].ring_enc[0];
	fw_shared = adev->vcn.inst[i].fw_shared.cpu_addr;

	/* program the RB_BASE for ring buffer */
	WREG32_SOC15(VCN, vcn_inst, regUVD_RB_BASE_LO,
		     lower_32_bits(ring->gpu_addr));
	WREG32_SOC15(VCN, vcn_inst, regUVD_RB_BASE_HI,
		     upper_32_bits(ring->gpu_addr));

	WREG32_SOC15(VCN, vcn_inst, regUVD_RB_SIZE,
		     ring->ring_size / sizeof(uint32_t));

	/* resetting ring, fw should not check RB ring */
	tmp = RREG32_SOC15(VCN, vcn_inst, regVCN_RB_ENABLE);
	tmp &= ~(VCN_RB_ENABLE__RB_EN_MASK);
	WREG32_SOC15(VCN, vcn_inst, regVCN_RB_ENABLE, tmp);

	/* Initialize the ring buffer's read and write pointers */
	WREG32_SOC15(VCN, vcn_inst, regUVD_RB_RPTR, 0);
	WREG32_SOC15(VCN, vcn_inst, regUVD_RB_WPTR, 0);

	tmp = RREG32_SOC15(VCN, vcn_inst, regVCN_RB_ENABLE);
	tmp |= VCN_RB_ENABLE__RB_EN_MASK;
	WREG32_SOC15(VCN, vcn_inst, regVCN_RB_ENABLE, tmp);

	ring->wptr = RREG32_SOC15(VCN, vcn_inst, regUVD_RB_WPTR);
	fw_shared->sq.queue_mode &=
		cpu_to_le32(~(FW_QUEUE_RING_RESET | FW_QUEUE_DPG_HOLD_OFF));

	return 0;
}

/**
 * vcn_v4_0_3_stop_dpg_mode - VCN stop with dpg mode
 *
 * @vinst: VCN instance
 *
 * Stop VCN block with dpg mode
 */
static int vcn_v4_0_3_stop_dpg_mode(struct amdgpu_vcn_inst *vinst)
{
	struct amdgpu_device *adev = vinst->adev;
	int inst_idx = vinst->inst;
	uint32_t tmp;
	int vcn_inst;

	vcn_inst = GET_INST(VCN, inst_idx);

	/* Wait for power status to be 1 */
	SOC15_WAIT_ON_RREG(VCN, vcn_inst, regUVD_POWER_STATUS, 1,
			   UVD_POWER_STATUS__UVD_POWER_STATUS_MASK);

	/* wait for read ptr to be equal to write ptr */
	tmp = RREG32_SOC15(VCN, vcn_inst, regUVD_RB_WPTR);
	SOC15_WAIT_ON_RREG(VCN, vcn_inst, regUVD_RB_RPTR, tmp, 0xFFFFFFFF);

	SOC15_WAIT_ON_RREG(VCN, vcn_inst, regUVD_POWER_STATUS, 1,
			   UVD_POWER_STATUS__UVD_POWER_STATUS_MASK);

	/* disable dynamic power gating mode */
	WREG32_P(SOC15_REG_OFFSET(VCN, vcn_inst, regUVD_POWER_STATUS), 0,
		 ~UVD_POWER_STATUS__UVD_PG_MODE_MASK);
	return 0;
}

/**
 * vcn_v4_0_3_stop - VCN stop
 *
 * @vinst: VCN instance
 *
 * Stop VCN block
 */
static int vcn_v4_0_3_stop(struct amdgpu_vcn_inst *vinst)
{
	struct amdgpu_device *adev = vinst->adev;
	int i = vinst->inst;
	volatile struct amdgpu_vcn4_fw_shared *fw_shared;
	int r = 0, vcn_inst;
	uint32_t tmp;

	vcn_inst = GET_INST(VCN, i);

	fw_shared = adev->vcn.inst[i].fw_shared.cpu_addr;
	fw_shared->sq.queue_mode |= FW_QUEUE_DPG_HOLD_OFF;

	if (adev->pg_flags & AMD_PG_SUPPORT_VCN_DPG) {
		vcn_v4_0_3_stop_dpg_mode(vinst);
		goto Done;
	}

	/* wait for vcn idle */
	r = SOC15_WAIT_ON_RREG(VCN, vcn_inst, regUVD_STATUS,
			       UVD_STATUS__IDLE, 0x7);
	if (r)
		goto Done;

	tmp = UVD_LMI_STATUS__VCPU_LMI_WRITE_CLEAN_MASK |
		UVD_LMI_STATUS__READ_CLEAN_MASK |
		UVD_LMI_STATUS__WRITE_CLEAN_MASK |
		UVD_LMI_STATUS__WRITE_CLEAN_RAW_MASK;
	r = SOC15_WAIT_ON_RREG(VCN, vcn_inst, regUVD_LMI_STATUS, tmp,
			       tmp);
	if (r)
		goto Done;

	/* stall UMC channel */
	tmp = RREG32_SOC15(VCN, vcn_inst, regUVD_LMI_CTRL2);
	tmp |= UVD_LMI_CTRL2__STALL_ARB_UMC_MASK;
	WREG32_SOC15(VCN, vcn_inst, regUVD_LMI_CTRL2, tmp);
	tmp = UVD_LMI_STATUS__UMC_READ_CLEAN_RAW_MASK |
		UVD_LMI_STATUS__UMC_WRITE_CLEAN_RAW_MASK;
	r = SOC15_WAIT_ON_RREG(VCN, vcn_inst, regUVD_LMI_STATUS, tmp,
			       tmp);
	if (r)
		goto Done;

	/* Unblock VCPU Register access */
	WREG32_P(SOC15_REG_OFFSET(VCN, vcn_inst, regUVD_RB_ARB_CTRL),
		 UVD_RB_ARB_CTRL__VCPU_DIS_MASK,
		 ~UVD_RB_ARB_CTRL__VCPU_DIS_MASK);

	/* release VCPU reset to boot */
	WREG32_P(SOC15_REG_OFFSET(VCN, vcn_inst, regUVD_VCPU_CNTL),
		 UVD_VCPU_CNTL__BLK_RST_MASK,
		 ~UVD_VCPU_CNTL__BLK_RST_MASK);

	/* disable VCPU clock */
	WREG32_P(SOC15_REG_OFFSET(VCN, vcn_inst, regUVD_VCPU_CNTL), 0,
		 ~(UVD_VCPU_CNTL__CLK_EN_MASK));

	/* reset LMI UMC/LMI/VCPU */
	tmp = RREG32_SOC15(VCN, vcn_inst, regUVD_SOFT_RESET);
	tmp |= UVD_SOFT_RESET__LMI_UMC_SOFT_RESET_MASK;
	WREG32_SOC15(VCN, vcn_inst, regUVD_SOFT_RESET, tmp);

	tmp = RREG32_SOC15(VCN, vcn_inst, regUVD_SOFT_RESET);
	tmp |= UVD_SOFT_RESET__LMI_SOFT_RESET_MASK;
	WREG32_SOC15(VCN, vcn_inst, regUVD_SOFT_RESET, tmp);

	/* clear VCN status */
	WREG32_SOC15(VCN, vcn_inst, regUVD_STATUS, 0);

	/* apply HW clock gating */
	vcn_v4_0_3_enable_clock_gating(vinst);

Done:
	return 0;
}

/**
 * vcn_v4_0_3_pause_dpg_mode - VCN pause with dpg mode
 *
 * @vinst: VCN instance
 * @new_state: pause state
 *
 * Pause dpg mode for VCN block
 */
static int vcn_v4_0_3_pause_dpg_mode(struct amdgpu_vcn_inst *vinst,
				     struct dpg_pause_state *new_state)
{

	return 0;
}

/**
 * vcn_v4_0_3_unified_ring_get_rptr - get unified read pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Returns the current hardware unified read pointer
 */
static uint64_t vcn_v4_0_3_unified_ring_get_rptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring != &adev->vcn.inst[ring->me].ring_enc[0])
		DRM_ERROR("wrong ring id is identified in %s", __func__);

	return RREG32_SOC15(VCN, GET_INST(VCN, ring->me), regUVD_RB_RPTR);
}

/**
 * vcn_v4_0_3_unified_ring_get_wptr - get unified write pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Returns the current hardware unified write pointer
 */
static uint64_t vcn_v4_0_3_unified_ring_get_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring != &adev->vcn.inst[ring->me].ring_enc[0])
		DRM_ERROR("wrong ring id is identified in %s", __func__);

	if (ring->use_doorbell)
		return *ring->wptr_cpu_addr;
	else
		return RREG32_SOC15(VCN, GET_INST(VCN, ring->me),
				    regUVD_RB_WPTR);
}

void vcn_v4_0_3_enc_ring_emit_reg_wait(struct amdgpu_ring *ring, uint32_t reg,
				       uint32_t val, uint32_t mask)
{
	/* Use normalized offsets when required */
	if (vcn_v4_0_3_normalizn_reqd(ring->adev))
		reg = NORMALIZE_VCN_REG_OFFSET(reg);

	amdgpu_ring_write(ring, VCN_ENC_CMD_REG_WAIT);
	amdgpu_ring_write(ring, reg << 2);
	amdgpu_ring_write(ring, mask);
	amdgpu_ring_write(ring, val);
}

void vcn_v4_0_3_enc_ring_emit_wreg(struct amdgpu_ring *ring, uint32_t reg,
				   uint32_t val)
{
	/* Use normalized offsets when required */
	if (vcn_v4_0_3_normalizn_reqd(ring->adev))
		reg = NORMALIZE_VCN_REG_OFFSET(reg);

	amdgpu_ring_write(ring, VCN_ENC_CMD_REG_WRITE);
	amdgpu_ring_write(ring,	reg << 2);
	amdgpu_ring_write(ring, val);
}

void vcn_v4_0_3_enc_ring_emit_vm_flush(struct amdgpu_ring *ring,
				       unsigned int vmid, uint64_t pd_addr)
{
	struct amdgpu_vmhub *hub = &ring->adev->vmhub[ring->vm_hub];

	pd_addr = amdgpu_gmc_emit_flush_gpu_tlb(ring, vmid, pd_addr);

	/* wait for reg writes */
	vcn_v4_0_3_enc_ring_emit_reg_wait(ring, hub->ctx0_ptb_addr_lo32 +
					vmid * hub->ctx_addr_distance,
					lower_32_bits(pd_addr), 0xffffffff);
}

void vcn_v4_0_3_ring_emit_hdp_flush(struct amdgpu_ring *ring)
{
	/* VCN engine access for HDP flush doesn't work when RRMT is enabled.
	 * This is a workaround to avoid any HDP flush through VCN ring.
	 */
}

/**
 * vcn_v4_0_3_unified_ring_set_wptr - set enc write pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Commits the enc write pointer to the hardware
 */
static void vcn_v4_0_3_unified_ring_set_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring != &adev->vcn.inst[ring->me].ring_enc[0])
		DRM_ERROR("wrong ring id is identified in %s", __func__);

	if (ring->use_doorbell) {
		*ring->wptr_cpu_addr = lower_32_bits(ring->wptr);
		WDOORBELL32(ring->doorbell_index, lower_32_bits(ring->wptr));
	} else {
		WREG32_SOC15(VCN, GET_INST(VCN, ring->me), regUVD_RB_WPTR,
			     lower_32_bits(ring->wptr));
	}
}

static const struct amdgpu_ring_funcs vcn_v4_0_3_unified_ring_vm_funcs = {
	.type = AMDGPU_RING_TYPE_VCN_ENC,
	.align_mask = 0x3f,
	.nop = VCN_ENC_CMD_NO_OP,
	.get_rptr = vcn_v4_0_3_unified_ring_get_rptr,
	.get_wptr = vcn_v4_0_3_unified_ring_get_wptr,
	.set_wptr = vcn_v4_0_3_unified_ring_set_wptr,
	.emit_frame_size =
		SOC15_FLUSH_GPU_TLB_NUM_WREG * 3 +
		SOC15_FLUSH_GPU_TLB_NUM_REG_WAIT * 4 +
		4 + /* vcn_v2_0_enc_ring_emit_vm_flush */
		5 + 5 + /* vcn_v2_0_enc_ring_emit_fence x2 vm fence */
		1, /* vcn_v2_0_enc_ring_insert_end */
	.emit_ib_size = 5, /* vcn_v2_0_enc_ring_emit_ib */
	.emit_ib = vcn_v2_0_enc_ring_emit_ib,
	.emit_fence = vcn_v2_0_enc_ring_emit_fence,
	.emit_vm_flush = vcn_v4_0_3_enc_ring_emit_vm_flush,
	.emit_hdp_flush = vcn_v4_0_3_ring_emit_hdp_flush,
	.test_ring = amdgpu_vcn_enc_ring_test_ring,
	.test_ib = amdgpu_vcn_unified_ring_test_ib,
	.insert_nop = amdgpu_ring_insert_nop,
	.insert_end = vcn_v2_0_enc_ring_insert_end,
	.pad_ib = amdgpu_ring_generic_pad_ib,
	.begin_use = amdgpu_vcn_ring_begin_use,
	.end_use = amdgpu_vcn_ring_end_use,
	.emit_wreg = vcn_v4_0_3_enc_ring_emit_wreg,
	.emit_reg_wait = vcn_v4_0_3_enc_ring_emit_reg_wait,
	.emit_reg_write_reg_wait = amdgpu_ring_emit_reg_write_reg_wait_helper,
};

/**
 * vcn_v4_0_3_set_unified_ring_funcs - set unified ring functions
 *
 * @adev: amdgpu_device pointer
 *
 * Set unified ring functions
 */
static void vcn_v4_0_3_set_unified_ring_funcs(struct amdgpu_device *adev)
{
	int i, vcn_inst;

	for (i = 0; i < adev->vcn.num_vcn_inst; ++i) {
		adev->vcn.inst[i].ring_enc[0].funcs = &vcn_v4_0_3_unified_ring_vm_funcs;
		adev->vcn.inst[i].ring_enc[0].me = i;
		vcn_inst = GET_INST(VCN, i);
		adev->vcn.inst[i].aid_id =
			vcn_inst / adev->vcn.num_inst_per_aid;
	}
}

/**
 * vcn_v4_0_3_is_idle - check VCN block is idle
 *
 * @ip_block: Pointer to the amdgpu_ip_block structure
 *
 * Check whether VCN block is idle
 */
static bool vcn_v4_0_3_is_idle(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	int i, ret = 1;

	for (i = 0; i < adev->vcn.num_vcn_inst; ++i) {
		ret &= (RREG32_SOC15(VCN, GET_INST(VCN, i), regUVD_STATUS) ==
			UVD_STATUS__IDLE);
	}

	return ret;
}

/**
 * vcn_v4_0_3_wait_for_idle - wait for VCN block idle
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 *
 * Wait for VCN block idle
 */
static int vcn_v4_0_3_wait_for_idle(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	int i, ret = 0;

	for (i = 0; i < adev->vcn.num_vcn_inst; ++i) {
		ret = SOC15_WAIT_ON_RREG(VCN, GET_INST(VCN, i), regUVD_STATUS,
					 UVD_STATUS__IDLE, UVD_STATUS__IDLE);
		if (ret)
			return ret;
	}

	return ret;
}

/* vcn_v4_0_3_set_clockgating_state - set VCN block clockgating state
 *
 * @ip_block: amdgpu_ip_block pointer
 * @state: clock gating state
 *
 * Set VCN block clockgating state
 */
static int vcn_v4_0_3_set_clockgating_state(struct amdgpu_ip_block *ip_block,
					  enum amd_clockgating_state state)
{
	struct amdgpu_device *adev = ip_block->adev;
	bool enable = state == AMD_CG_STATE_GATE;
	int i;

	for (i = 0; i < adev->vcn.num_vcn_inst; ++i) {
		struct amdgpu_vcn_inst *vinst = &adev->vcn.inst[i];

		if (enable) {
			if (RREG32_SOC15(VCN, GET_INST(VCN, i),
					 regUVD_STATUS) != UVD_STATUS__IDLE)
				return -EBUSY;
			vcn_v4_0_3_enable_clock_gating(vinst);
		} else {
			vcn_v4_0_3_disable_clock_gating(vinst);
		}
	}
	return 0;
}

static int vcn_v4_0_3_set_pg_state(struct amdgpu_vcn_inst *vinst,
				   enum amd_powergating_state state)
{
	struct amdgpu_device *adev = vinst->adev;
	int ret = 0;

	/* for SRIOV, guest should not control VCN Power-gating
	 * MMSCH FW should control Power-gating and clock-gating
	 * guest should avoid touching CGC and PG
	 */
	if (amdgpu_sriov_vf(adev)) {
		vinst->cur_state = AMD_PG_STATE_UNGATE;
		return 0;
	}

	if (state == vinst->cur_state)
		return 0;

	if (state == AMD_PG_STATE_GATE)
		ret = vcn_v4_0_3_stop(vinst);
	else
		ret = vcn_v4_0_3_start(vinst);

	if (!ret)
		vinst->cur_state = state;

	return ret;
}

/**
 * vcn_v4_0_3_set_interrupt_state - set VCN block interrupt state
 *
 * @adev: amdgpu_device pointer
 * @source: interrupt sources
 * @type: interrupt types
 * @state: interrupt states
 *
 * Set VCN block interrupt state
 */
static int vcn_v4_0_3_set_interrupt_state(struct amdgpu_device *adev,
					struct amdgpu_irq_src *source,
					unsigned int type,
					enum amdgpu_interrupt_state state)
{
	return 0;
}

/**
 * vcn_v4_0_3_process_interrupt - process VCN block interrupt
 *
 * @adev: amdgpu_device pointer
 * @source: interrupt sources
 * @entry: interrupt entry from clients and sources
 *
 * Process VCN block interrupt
 */
static int vcn_v4_0_3_process_interrupt(struct amdgpu_device *adev,
				      struct amdgpu_irq_src *source,
				      struct amdgpu_iv_entry *entry)
{
	uint32_t i, inst;

	i = node_id_to_phys_map[entry->node_id];

	DRM_DEV_DEBUG(adev->dev, "IH: VCN TRAP\n");

	for (inst = 0; inst < adev->vcn.num_vcn_inst; ++inst)
		if (adev->vcn.inst[inst].aid_id == i)
			break;

	if (inst >= adev->vcn.num_vcn_inst) {
		dev_WARN_ONCE(adev->dev, 1,
			      "Interrupt received for unknown VCN instance %d",
			      entry->node_id);
		return 0;
	}

	switch (entry->src_id) {
	case VCN_4_0__SRCID__UVD_ENC_GENERAL_PURPOSE:
		amdgpu_fence_process(&adev->vcn.inst[inst].ring_enc[0]);
		break;
	default:
		DRM_DEV_ERROR(adev->dev, "Unhandled interrupt: %d %d\n",
			  entry->src_id, entry->src_data[0]);
		break;
	}

	return 0;
}

static const struct amdgpu_irq_src_funcs vcn_v4_0_3_irq_funcs = {
	.set = vcn_v4_0_3_set_interrupt_state,
	.process = vcn_v4_0_3_process_interrupt,
};

/**
 * vcn_v4_0_3_set_irq_funcs - set VCN block interrupt irq functions
 *
 * @adev: amdgpu_device pointer
 *
 * Set VCN block interrupt irq functions
 */
static void vcn_v4_0_3_set_irq_funcs(struct amdgpu_device *adev)
{
	int i;

	for (i = 0; i < adev->vcn.num_vcn_inst; ++i) {
		adev->vcn.inst->irq.num_types++;
	}
	adev->vcn.inst->irq.funcs = &vcn_v4_0_3_irq_funcs;
}

static void vcn_v4_0_3_print_ip_state(struct amdgpu_ip_block *ip_block, struct drm_printer *p)
{
	struct amdgpu_device *adev = ip_block->adev;
	int i, j;
	uint32_t reg_count = ARRAY_SIZE(vcn_reg_list_4_0_3);
	uint32_t inst_off, is_powered;

	if (!adev->vcn.ip_dump)
		return;

	drm_printf(p, "num_instances:%d\n", adev->vcn.num_vcn_inst);
	for (i = 0; i < adev->vcn.num_vcn_inst; i++) {
		if (adev->vcn.harvest_config & (1 << i)) {
			drm_printf(p, "\nHarvested Instance:VCN%d Skipping dump\n", i);
			continue;
		}

		inst_off = i * reg_count;
		is_powered = (adev->vcn.ip_dump[inst_off] &
				UVD_POWER_STATUS__UVD_POWER_STATUS_MASK) != 1;

		if (is_powered) {
			drm_printf(p, "\nActive Instance:VCN%d\n", i);
			for (j = 0; j < reg_count; j++)
				drm_printf(p, "%-50s \t 0x%08x\n", vcn_reg_list_4_0_3[j].reg_name,
					   adev->vcn.ip_dump[inst_off + j]);
		} else {
			drm_printf(p, "\nInactive Instance:VCN%d\n", i);
		}
	}
}

static void vcn_v4_0_3_dump_ip_state(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	int i, j;
	bool is_powered;
	uint32_t inst_off, inst_id;
	uint32_t reg_count = ARRAY_SIZE(vcn_reg_list_4_0_3);

	if (!adev->vcn.ip_dump)
		return;

	for (i = 0; i < adev->vcn.num_vcn_inst; i++) {
		if (adev->vcn.harvest_config & (1 << i))
			continue;

		inst_id = GET_INST(VCN, i);
		inst_off = i * reg_count;
		/* mmUVD_POWER_STATUS is always readable and is first element of the array */
		adev->vcn.ip_dump[inst_off] = RREG32_SOC15(VCN, inst_id, regUVD_POWER_STATUS);
		is_powered = (adev->vcn.ip_dump[inst_off] &
				UVD_POWER_STATUS__UVD_POWER_STATUS_MASK) != 1;

		if (is_powered)
			for (j = 1; j < reg_count; j++)
				adev->vcn.ip_dump[inst_off + j] =
					RREG32(SOC15_REG_ENTRY_OFFSET_INST(vcn_reg_list_4_0_3[j],
									   inst_id));
	}
}

static const struct amd_ip_funcs vcn_v4_0_3_ip_funcs = {
	.name = "vcn_v4_0_3",
	.early_init = vcn_v4_0_3_early_init,
	.sw_init = vcn_v4_0_3_sw_init,
	.sw_fini = vcn_v4_0_3_sw_fini,
	.hw_init = vcn_v4_0_3_hw_init,
	.hw_fini = vcn_v4_0_3_hw_fini,
	.suspend = vcn_v4_0_3_suspend,
	.resume = vcn_v4_0_3_resume,
	.is_idle = vcn_v4_0_3_is_idle,
	.wait_for_idle = vcn_v4_0_3_wait_for_idle,
	.set_clockgating_state = vcn_v4_0_3_set_clockgating_state,
	.set_powergating_state = vcn_set_powergating_state,
	.dump_ip_state = vcn_v4_0_3_dump_ip_state,
	.print_ip_state = vcn_v4_0_3_print_ip_state,
};

const struct amdgpu_ip_block_version vcn_v4_0_3_ip_block = {
	.type = AMD_IP_BLOCK_TYPE_VCN,
	.major = 4,
	.minor = 0,
	.rev = 3,
	.funcs = &vcn_v4_0_3_ip_funcs,
};

static const struct amdgpu_ras_err_status_reg_entry vcn_v4_0_3_ue_reg_list[] = {
	{AMDGPU_RAS_REG_ENTRY(VCN, 0, regVCN_UE_ERR_STATUS_LO_VIDD, regVCN_UE_ERR_STATUS_HI_VIDD),
	1, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "VIDD"},
	{AMDGPU_RAS_REG_ENTRY(VCN, 0, regVCN_UE_ERR_STATUS_LO_VIDV, regVCN_UE_ERR_STATUS_HI_VIDV),
	1, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "VIDV"},
};

static void vcn_v4_0_3_inst_query_ras_error_count(struct amdgpu_device *adev,
						  uint32_t vcn_inst,
						  void *ras_err_status)
{
	struct ras_err_data *err_data = (struct ras_err_data *)ras_err_status;

	/* vcn v4_0_3 only support query uncorrectable errors */
	amdgpu_ras_inst_query_ras_error_count(adev,
			vcn_v4_0_3_ue_reg_list,
			ARRAY_SIZE(vcn_v4_0_3_ue_reg_list),
			NULL, 0, GET_INST(VCN, vcn_inst),
			AMDGPU_RAS_ERROR__MULTI_UNCORRECTABLE,
			&err_data->ue_count);
}

static void vcn_v4_0_3_query_ras_error_count(struct amdgpu_device *adev,
					     void *ras_err_status)
{
	uint32_t i;

	if (!amdgpu_ras_is_supported(adev, AMDGPU_RAS_BLOCK__VCN)) {
		dev_warn(adev->dev, "VCN RAS is not supported\n");
		return;
	}

	for (i = 0; i < adev->vcn.num_vcn_inst; i++)
		vcn_v4_0_3_inst_query_ras_error_count(adev, i, ras_err_status);
}

static void vcn_v4_0_3_inst_reset_ras_error_count(struct amdgpu_device *adev,
						  uint32_t vcn_inst)
{
	amdgpu_ras_inst_reset_ras_error_count(adev,
					vcn_v4_0_3_ue_reg_list,
					ARRAY_SIZE(vcn_v4_0_3_ue_reg_list),
					GET_INST(VCN, vcn_inst));
}

static void vcn_v4_0_3_reset_ras_error_count(struct amdgpu_device *adev)
{
	uint32_t i;

	if (!amdgpu_ras_is_supported(adev, AMDGPU_RAS_BLOCK__VCN)) {
		dev_warn(adev->dev, "VCN RAS is not supported\n");
		return;
	}

	for (i = 0; i < adev->vcn.num_vcn_inst; i++)
		vcn_v4_0_3_inst_reset_ras_error_count(adev, i);
}

static const struct amdgpu_ras_block_hw_ops vcn_v4_0_3_ras_hw_ops = {
	.query_ras_error_count = vcn_v4_0_3_query_ras_error_count,
	.reset_ras_error_count = vcn_v4_0_3_reset_ras_error_count,
};

static int vcn_v4_0_3_aca_bank_parser(struct aca_handle *handle, struct aca_bank *bank,
				      enum aca_smu_type type, void *data)
{
	struct aca_bank_info info;
	u64 misc0;
	int ret;

	ret = aca_bank_info_decode(bank, &info);
	if (ret)
		return ret;

	misc0 = bank->regs[ACA_REG_IDX_MISC0];
	switch (type) {
	case ACA_SMU_TYPE_UE:
		bank->aca_err_type = ACA_ERROR_TYPE_UE;
		ret = aca_error_cache_log_bank_error(handle, &info, ACA_ERROR_TYPE_UE,
						     1ULL);
		break;
	case ACA_SMU_TYPE_CE:
		bank->aca_err_type = ACA_ERROR_TYPE_CE;
		ret = aca_error_cache_log_bank_error(handle, &info, bank->aca_err_type,
						     ACA_REG__MISC0__ERRCNT(misc0));
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

/* reference to smu driver if header file */
static int vcn_v4_0_3_err_codes[] = {
	14, 15, /* VCN */
};

static bool vcn_v4_0_3_aca_bank_is_valid(struct aca_handle *handle, struct aca_bank *bank,
					 enum aca_smu_type type, void *data)
{
	u32 instlo;

	instlo = ACA_REG__IPID__INSTANCEIDLO(bank->regs[ACA_REG_IDX_IPID]);
	instlo &= GENMASK(31, 1);

	if (instlo != mmSMNAID_AID0_MCA_SMU)
		return false;

	if (aca_bank_check_error_codes(handle->adev, bank,
				       vcn_v4_0_3_err_codes,
				       ARRAY_SIZE(vcn_v4_0_3_err_codes)))
		return false;

	return true;
}

static const struct aca_bank_ops vcn_v4_0_3_aca_bank_ops = {
	.aca_bank_parser = vcn_v4_0_3_aca_bank_parser,
	.aca_bank_is_valid = vcn_v4_0_3_aca_bank_is_valid,
};

static const struct aca_info vcn_v4_0_3_aca_info = {
	.hwip = ACA_HWIP_TYPE_SMU,
	.mask = ACA_ERROR_UE_MASK,
	.bank_ops = &vcn_v4_0_3_aca_bank_ops,
};

static int vcn_v4_0_3_ras_late_init(struct amdgpu_device *adev, struct ras_common_if *ras_block)
{
	int r;

	r = amdgpu_ras_block_late_init(adev, ras_block);
	if (r)
		return r;

	r = amdgpu_ras_bind_aca(adev, AMDGPU_RAS_BLOCK__VCN,
				&vcn_v4_0_3_aca_info, NULL);
	if (r)
		goto late_fini;

	return 0;

late_fini:
	amdgpu_ras_block_late_fini(adev, ras_block);

	return r;
}

static struct amdgpu_vcn_ras vcn_v4_0_3_ras = {
	.ras_block = {
		.hw_ops = &vcn_v4_0_3_ras_hw_ops,
		.ras_late_init = vcn_v4_0_3_ras_late_init,
	},
};

static void vcn_v4_0_3_set_ras_funcs(struct amdgpu_device *adev)
{
	adev->vcn.ras = &vcn_v4_0_3_ras;
}

static void vcn_v4_0_3_enable_ras(struct amdgpu_device *adev,
				  int inst_idx, bool indirect)
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

	tmp = UVD_VCPU_INT_EN2__RASCNTL_VCPU_VCODEC_EN_MASK;
	WREG32_SOC15_DPG_MODE(inst_idx,
			      SOC15_DPG_MODE_OFFSET(VCN, 0, regUVD_VCPU_INT_EN2),
			      tmp, 0, indirect);

	tmp = UVD_SYS_INT_EN__RASCNTL_VCPU_VCODEC_EN_MASK;
	WREG32_SOC15_DPG_MODE(inst_idx,
			      SOC15_DPG_MODE_OFFSET(VCN, 0, regUVD_SYS_INT_EN),
			      tmp, 0, indirect);
}
