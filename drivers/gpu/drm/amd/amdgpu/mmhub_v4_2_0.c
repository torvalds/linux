/*
 * Copyright 2025 Advanced Micro Devices, Inc.
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

#include "amdgpu.h"
#include "mmhub_v4_2_0.h"

#include "mmhub/mmhub_4_2_0_offset.h"
#include "mmhub/mmhub_4_2_0_sh_mask.h"

#include "soc15_common.h"
#include "soc24_enum.h"
#include "soc_v1_0.h"

#define regMMVM_L2_CNTL3_DEFAULT				0x80100007
#define regMMVM_L2_CNTL4_DEFAULT				0x000000c1
#define regMMVM_L2_CNTL5_DEFAULT				0x00003fe0
#define MMHUB_V4_2_0_MID1_EXT_ID				0x3

static u64 mmhub_v4_2_0_get_reg_addr(int inst, u32 reg_offset)
{
	int ext_id = inst ? MMHUB_V4_2_0_MID1_EXT_ID : 0;

	return (u64)reg_offset * 4 +
	       soc_v1_0_encode_ext_smn_addressing(ext_id);
}

static u32 mmhub_v4_2_0_rreg(struct amdgpu_device *adev, int inst,
			     u32 reg_offset)
{
	u64 reg_addr;

	reg_addr = mmhub_v4_2_0_get_reg_addr(inst, reg_offset);

	return RREG32_PCIE_EXT(reg_addr);
}

static void mmhub_v4_2_0_wreg(struct amdgpu_device *adev, int inst,
			      u32 reg_offset, u32 value)
{
	u64 reg_addr;

	reg_addr = mmhub_v4_2_0_get_reg_addr(inst, reg_offset);

	WREG32_PCIE_EXT(reg_addr, value);
}

#define RREG32_MMHUB(inst, reg) \
	mmhub_v4_2_0_rreg(adev, inst, \
		(adev->reg_offset[MMHUB_HWIP][GET_INST(MMHUB, 0)] \
				 [reg##_BASE_IDX] + (reg)))
#define RREG32_MMHUB_OFFSET(inst, reg, offset) \
	mmhub_v4_2_0_rreg(adev, inst, \
		(adev->reg_offset[MMHUB_HWIP][GET_INST(MMHUB, 0)] \
				 [reg##_BASE_IDX] + (reg) + (offset)))
#define WREG32_MMHUB(inst, reg, value) \
	mmhub_v4_2_0_wreg(adev, inst, \
		(adev->reg_offset[MMHUB_HWIP][GET_INST(MMHUB, 0)] \
				 [reg##_BASE_IDX] + (reg)), value)
#define WREG32_MMHUB_OFFSET(inst, reg, offset, value) \
	mmhub_v4_2_0_wreg(adev, inst, \
		(adev->reg_offset[MMHUB_HWIP][GET_INST(MMHUB, 0)] \
				 [reg##_BASE_IDX] + (reg) + (offset)), value)

static const char *mmhub_client_ids_v4_2_0[][2] = {
	[0][0] = "VMC",
	[2][0] = "MPNHT",
	[7][0] = "MPIFOE",
	[8][0] = "MPIO",
	[11][0] = "JPEG0",
	[12][0] = "VCN0",
	[13][0] = "VCNU0",
	[14][0] = "VSCH0",
	[15][0] = "LSDMA",
	[32+5][0] = "MPRAS",
	[32+6][0] = "MP1",
	[32+7][0] = "MP0",
	[32+11][0] = "JPEG1",
	[32+12][0] = "VCN1",
	[32+13][0] = "VCNU1",
	[32+14][0] = "VSCH1",
	[2][1] = "MPNHT",
	[3][1] = "DBGU0",
	[7][1] = "MPIFOE",
	[8][1] = "MPIO",
	[10][1] = "UTCL2_NHT",
	[11][1] = "JPEG0",
	[12][1] = "VCN0",
	[13][1] = "VCNU0",
	[14][1] = "VSCH0",
	[15][1] = "LSDMA",
	[32+3][1] = "DBGU1",
	[32+4][1] = "DBGU2",
	[32+5][1] = "MPRAS",
	[32+6][1] = "MP1",
	[32+7][1] = "MP0",
	[32+8][1] = "IH",
	[32+11][1] = "JPEG1",
	[32+12][1] = "VCN1",
	[32+13][1] = "VCNU1",
	[32+14][1] = "VSCH1",
};

static int mmhub_v4_2_0_get_xgmi_info(struct amdgpu_device *adev)
{
	u32 max_num_physical_nodes;
	u32 max_physical_node_id;
	u32 xgmi_lfb_cntl;
	u32 max_region;
	u64 seg_size;

	/* limit this callback to A + A configuration only */
	if (!adev->gmc.xgmi.connected_to_cpu)
		return 0;

	xgmi_lfb_cntl = RREG32_SOC15(MMHUB, GET_INST(MMHUB, 0),
				     regMMMC_VM_XGMI_LFB_CNTL);
	seg_size = REG_GET_FIELD(
		RREG32_SOC15(MMHUB, GET_INST(MMHUB, 0), regMMMC_VM_XGMI_LFB_SIZE),
		MMMC_VM_XGMI_LFB_SIZE, PF_LFB_SIZE) << 24;
	max_region =
		REG_GET_FIELD(xgmi_lfb_cntl, MMMC_VM_XGMI_LFB_CNTL, PF_MAX_REGION);

	max_num_physical_nodes   = 4;
	max_physical_node_id     = 3;

	adev->gmc.xgmi.num_physical_nodes = max_region + 1;

	if (adev->gmc.xgmi.num_physical_nodes > max_num_physical_nodes)
		return -EINVAL;

	adev->gmc.xgmi.physical_node_id =
		REG_GET_FIELD(xgmi_lfb_cntl, MMMC_VM_XGMI_LFB_CNTL, PF_LFB_REGION);

	if (adev->gmc.xgmi.physical_node_id > max_physical_node_id)
		return -EINVAL;

	if (amdgpu_virt_vram_is_spm(adev) && adev->gmc.aper_size)
		adev->gmc.xgmi.node_segment_size = adev->gmc.aper_size;
	else
		adev->gmc.xgmi.node_segment_size = seg_size;

	return 0;
}

static u64 mmhub_v4_2_0_get_fb_location(struct amdgpu_device *adev)
{
	u64 base;

	base = RREG32_SOC15(MMHUB, GET_INST(MMHUB, 0),
			    regMMMC_VM_FB_LOCATION_BASE_LO32);
	base &= MMMC_VM_FB_LOCATION_BASE_LO32__FB_BASE_LO32_MASK;
	base <<= 24;

	base |= ((u64)(MMMC_VM_FB_LOCATION_BASE_HI32__FB_BASE_HI1_MASK &
		       RREG32_SOC15(MMHUB, GET_INST(MMHUB, 0),
				    regMMMC_VM_FB_LOCATION_BASE_HI32)) << 56);

	return base;
}

static u64 mmhub_v4_2_0_get_mc_fb_offset(struct amdgpu_device *adev)
{
	return (u64)RREG32_SOC15(MMHUB, GET_INST(MMHUB, 0),
			         regMMMC_VM_FB_OFFSET) << 24;
}

static void mmhub_v4_2_0_mid_setup_vm_pt_regs(struct amdgpu_device *adev,
					      uint32_t vmid,
					      uint64_t page_table_base,
					      uint32_t inst_mask)
{
	struct amdgpu_vmhub *hub;
	int i;

	for_each_inst(i, inst_mask) {
		hub = &adev->vmhub[AMDGPU_MMHUB0(i)];
		WREG32_MMHUB_OFFSET(i,
				   regMMVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32,
				   hub->ctx_addr_distance * vmid,
				   lower_32_bits(page_table_base));

		WREG32_MMHUB_OFFSET(i,
				   regMMVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_HI32,
				   hub->ctx_addr_distance * vmid,
				   upper_32_bits(page_table_base));
	}
}

static void mmhub_v4_2_0_setup_vm_pt_regs(struct amdgpu_device *adev,
					  uint32_t vmid,
					  uint64_t page_table_base)
{
	mmhub_v4_2_0_mid_setup_vm_pt_regs(adev, vmid,
					  page_table_base,
					  adev->mmhub.inst_mask);
}

static void mmhub_v4_2_0_mid_init_gart_aperture_regs(struct amdgpu_device *adev,
						     uint32_t inst_mask)
{
	uint64_t pt_base;
	int i;

	if (adev->gmc.pdb0_bo)
		pt_base = amdgpu_gmc_pd_addr(adev->gmc.pdb0_bo);
	else
		pt_base = amdgpu_gmc_pd_addr(adev->gart.bo);

	mmhub_v4_2_0_mid_setup_vm_pt_regs(adev, 0, pt_base, inst_mask);

	for_each_inst(i, inst_mask) {
		if (adev->gmc.pdb0_bo) {
			WREG32_MMHUB(i,
				     regMMVM_CONTEXT0_PAGE_TABLE_START_ADDR_LO32,
				     (u32)(adev->gmc.fb_start >> 12));
			WREG32_MMHUB(i,
				     regMMVM_CONTEXT0_PAGE_TABLE_START_ADDR_HI32,
				     (u32)(adev->gmc.fb_start >> 44));

			WREG32_MMHUB(i,
				     regMMVM_CONTEXT0_PAGE_TABLE_END_ADDR_LO32,
				     (u32)(adev->gmc.gart_end >> 12));
			WREG32_MMHUB(i,
				     regMMVM_CONTEXT0_PAGE_TABLE_END_ADDR_HI32,
				     (u32)(adev->gmc.gart_end >> 44));
		} else {
			WREG32_MMHUB(i,
				     regMMVM_CONTEXT0_PAGE_TABLE_START_ADDR_LO32,
				     (u32)(adev->gmc.gart_start >> 12));
			WREG32_MMHUB(i,
				     regMMVM_CONTEXT0_PAGE_TABLE_START_ADDR_HI32,
				     (u32)(adev->gmc.gart_start >> 44));

			WREG32_MMHUB(i,
				     regMMVM_CONTEXT0_PAGE_TABLE_END_ADDR_LO32,
				     (u32)(adev->gmc.gart_end >> 12));
			WREG32_MMHUB(i,
				     regMMVM_CONTEXT0_PAGE_TABLE_END_ADDR_HI32,
				     (u32)(adev->gmc.gart_end >> 44));
		}
	}
}

static void mmhub_v4_2_0_mid_init_system_aperture_regs(struct amdgpu_device *adev,
						       uint32_t inst_mask)
{
	uint64_t value;
	uint32_t tmp;
	int i;

	/*
	 * the new L1 policy will block SRIOV guest from writing
	 * these regs, and they will be programed at host.
	 * so skip programing these regs.
	 */
	if (amdgpu_sriov_vf(adev))
		return;

	for_each_inst(i, inst_mask) {
		if (adev->gmc.pdb0_bo) {
			/* Disable agp and system aperture
			 * when vmid0 page table is enabled */
			WREG32_MMHUB(i,
				     regMMMC_VM_FB_LOCATION_TOP_LO32, 0);
			WREG32_MMHUB(i,
				     regMMMC_VM_FB_LOCATION_TOP_HI32, 0);
			WREG32_MMHUB(i,
				     regMMMC_VM_FB_LOCATION_BASE_LO32,
				     0xFFFFFFFF);
			WREG32_MMHUB(i,
				     regMMMC_VM_FB_LOCATION_BASE_HI32, 1);
			WREG32_MMHUB(i,
				     regMMMC_VM_AGP_TOP_LO32, 0);
			WREG32_MMHUB(i,
				     regMMMC_VM_AGP_TOP_HI32, 0);
			WREG32_MMHUB(i,
				     regMMMC_VM_AGP_BOT_LO32,
				     0xFFFFFFFF);
			WREG32_MMHUB(i,
				     regMMMC_VM_AGP_BOT_HI32, 1);
			WREG32_MMHUB(i,
				     regMMMC_VM_SYSTEM_APERTURE_LOW_ADDR_LO32,
				     0xFFFFFFFF);
			WREG32_MMHUB(i,
				     regMMMC_VM_SYSTEM_APERTURE_LOW_ADDR_HI32,
				     0x7F);
			WREG32_MMHUB(i,
				     regMMMC_VM_SYSTEM_APERTURE_HIGH_ADDR_LO32, 0);
			WREG32_MMHUB(i,
				     regMMMC_VM_SYSTEM_APERTURE_HIGH_ADDR_HI32, 0);
		} else {
			/* Program the AGP BAR */
			WREG32_MMHUB(i,
				     regMMMC_VM_AGP_BASE_LO32, 0);
			WREG32_MMHUB(i,
				     regMMMC_VM_AGP_BASE_HI32, 0);
			WREG32_MMHUB(i,
				     regMMMC_VM_AGP_BOT_LO32,
				     lower_32_bits(adev->gmc.agp_start >> 24));
			WREG32_MMHUB(i,
				     regMMMC_VM_AGP_BOT_HI32,
				     upper_32_bits(adev->gmc.agp_start >> 24));
			WREG32_MMHUB(i,
				     regMMMC_VM_AGP_TOP_LO32,
				     lower_32_bits(adev->gmc.agp_end >> 24));
			WREG32_MMHUB(i,
				     regMMMC_VM_AGP_TOP_HI32,
				     upper_32_bits(adev->gmc.agp_end >> 24));

			/* Program the system aperture low logical page number. */
			WREG32_MMHUB(i,
				     regMMMC_VM_SYSTEM_APERTURE_LOW_ADDR_LO32,
				     lower_32_bits(min(adev->gmc.fb_start,
						   adev->gmc.agp_start) >> 18));
			WREG32_MMHUB(i,
				     regMMMC_VM_SYSTEM_APERTURE_LOW_ADDR_HI32,
				     upper_32_bits(min(adev->gmc.fb_start,
						   adev->gmc.agp_start) >> 18));
			WREG32_MMHUB(i,
				     regMMMC_VM_SYSTEM_APERTURE_HIGH_ADDR_LO32,
				     lower_32_bits(max(adev->gmc.fb_end,
						   adev->gmc.agp_end) >> 18));
			WREG32_MMHUB(i,
				     regMMMC_VM_SYSTEM_APERTURE_HIGH_ADDR_HI32,
				     upper_32_bits(max(adev->gmc.fb_end,
						   adev->gmc.agp_end) >> 18));
		}

		/* Set default page address. */
		value = amdgpu_gmc_vram_mc2pa(adev, adev->mem_scratch.gpu_addr);
		WREG32_MMHUB(i,
			     regMMMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_LSB,
			     (u32)(value >> 12));
		WREG32_MMHUB(i,
			     regMMMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_MSB,
			     (u32)(value >> 44));

		/* Program "protection fault". */
		WREG32_MMHUB(i,
			     regMMVM_L2_PROTECTION_FAULT_DEFAULT_ADDR_LO32,
			     (u32)(adev->dummy_page_addr >> 12));
		WREG32_MMHUB(i,
			     regMMVM_L2_PROTECTION_FAULT_DEFAULT_ADDR_HI32,
			     (u32)((u64)adev->dummy_page_addr >> 44));

		tmp = RREG32_MMHUB(i, regMMVM_L2_PROTECTION_FAULT_CNTL2);
		tmp = REG_SET_FIELD(tmp, MMVM_L2_PROTECTION_FAULT_CNTL2,
				    ACTIVE_PAGE_MIGRATION_PTE_READ_RETRY, 1);
		tmp = REG_SET_FIELD(tmp, MMVM_L2_PROTECTION_FAULT_CNTL2,
				    ENABLE_RETRY_FAULT_INTERRUPT, 0x1);
		WREG32_MMHUB(i,
			     regMMVM_L2_PROTECTION_FAULT_CNTL2, tmp);
	}
}

static void mmhub_v4_2_0_mid_init_tlb_regs(struct amdgpu_device *adev,
					   uint32_t inst_mask)
{
	uint32_t tmp;
	int i;

	for_each_inst(i, inst_mask) {
		/* Setup TLB control */
		tmp = RREG32_MMHUB(i, regMMMC_VM_MX_L1_TLB_CNTL);

		tmp = REG_SET_FIELD(tmp, MMMC_VM_MX_L1_TLB_CNTL, ENABLE_L1_TLB, 1);
		tmp = REG_SET_FIELD(tmp, MMMC_VM_MX_L1_TLB_CNTL, SYSTEM_ACCESS_MODE, 3);
		tmp = REG_SET_FIELD(tmp, MMMC_VM_MX_L1_TLB_CNTL,
				    ENABLE_ADVANCED_DRIVER_MODEL, 1);
		tmp = REG_SET_FIELD(tmp, MMMC_VM_MX_L1_TLB_CNTL,
				    SYSTEM_APERTURE_UNMAPPED_ACCESS, 0);
		tmp = REG_SET_FIELD(tmp, MMMC_VM_MX_L1_TLB_CNTL,
				    MTYPE, MTYPE_UC); /* UC, uncached */

		WREG32_MMHUB(i,
			     regMMMC_VM_MX_L1_TLB_CNTL, tmp);
	}
}

static void mmhub_v4_2_0_mid_init_cache_regs(struct amdgpu_device *adev,
					     uint32_t inst_mask)
{
	uint32_t tmp;
	int i;

	/* These registers are not accessible to VF-SRIOV.
	 * The PF will program them instead.
	 */
	if (amdgpu_sriov_vf(adev))
		return;

	for_each_inst(i, inst_mask) {
		/* Setup L2 cache */
		tmp = RREG32_MMHUB(i, regMMVM_L2_CNTL);
		tmp = REG_SET_FIELD(tmp, MMVM_L2_CNTL, ENABLE_L2_CACHE, 1);
		tmp = REG_SET_FIELD(tmp, MMVM_L2_CNTL,
				    ENABLE_DEFAULT_PAGE_OUT_TO_SYSTEM_MEMORY, 1);
		/* XXX for emulation, Refer to closed source code.*/
		tmp = REG_SET_FIELD(tmp, MMVM_L2_CNTL,
				    L2_PDE0_CACHE_TAG_GENERATION_MODE, 0);
		tmp = REG_SET_FIELD(tmp, MMVM_L2_CNTL,
				    PDE_FAULT_CLASSIFICATION, 0);
		tmp = REG_SET_FIELD(tmp, MMVM_L2_CNTL,
				    CONTEXT1_IDENTITY_ACCESS_MODE, 1);
		tmp = REG_SET_FIELD(tmp, MMVM_L2_CNTL,
				    IDENTITY_MODE_FRAGMENT_SIZE, 0);
		WREG32_MMHUB(i, regMMVM_L2_CNTL, tmp);

		tmp = regMMVM_L2_CNTL3_DEFAULT;
		if (adev->gmc.translate_further) {
			tmp = REG_SET_FIELD(tmp, MMVM_L2_CNTL3, BANK_SELECT, 12);
			tmp = REG_SET_FIELD(tmp, MMVM_L2_CNTL3,
					    L2_CACHE_BIGK_FRAGMENT_SIZE, 9);
		} else {
			tmp = REG_SET_FIELD(tmp, MMVM_L2_CNTL3, BANK_SELECT, 9);
			tmp = REG_SET_FIELD(tmp, MMVM_L2_CNTL3,
					    L2_CACHE_BIGK_FRAGMENT_SIZE, 6);
		}
		WREG32_MMHUB(i, regMMVM_L2_CNTL3, tmp);

		tmp = regMMVM_L2_CNTL4_DEFAULT;
		/* For AMD APP APUs setup WC memory */
		if (adev->gmc.xgmi.connected_to_cpu || adev->gmc.is_app_apu) {
			tmp = REG_SET_FIELD(tmp, MMVM_L2_CNTL4,
					    VMC_TAP_PDE_REQUEST_PHYSICAL, 1);
			tmp = REG_SET_FIELD(tmp, MMVM_L2_CNTL4,
					    VMC_TAP_PTE_REQUEST_PHYSICAL, 1);
		} else {
			tmp = REG_SET_FIELD(tmp, MMVM_L2_CNTL4,
					    VMC_TAP_PDE_REQUEST_PHYSICAL, 0);
			tmp = REG_SET_FIELD(tmp, MMVM_L2_CNTL4,
					    VMC_TAP_PTE_REQUEST_PHYSICAL, 0);
		}
		WREG32_MMHUB(i, regMMVM_L2_CNTL4, tmp);

		tmp = regMMVM_L2_CNTL5_DEFAULT;
		tmp = REG_SET_FIELD(tmp, MMVM_L2_CNTL5,
				    L2_CACHE_SMALLK_FRAGMENT_SIZE, 0);
		WREG32_MMHUB(i, regMMVM_L2_CNTL5, tmp);
	}
}

static void mmhub_v4_2_0_mid_enable_system_domain(struct amdgpu_device *adev,
						  uint32_t inst_mask)
{
	uint32_t tmp;
	int i;

	for_each_inst(i, inst_mask) {
		tmp = RREG32_MMHUB(i, regMMVM_CONTEXT0_CNTL);
		tmp = REG_SET_FIELD(tmp, MMVM_CONTEXT0_CNTL,
				    ENABLE_CONTEXT, 1);
		tmp = REG_SET_FIELD(tmp, MMVM_CONTEXT0_CNTL,
				    PAGE_TABLE_DEPTH, adev->gmc.vmid0_page_table_depth);
		tmp = REG_SET_FIELD(tmp, MMVM_CONTEXT0_CNTL,
				    PAGE_TABLE_BLOCK_SIZE,
				    adev->gmc.vmid0_page_table_block_size);
		tmp = REG_SET_FIELD(tmp, MMVM_CONTEXT0_CNTL,
				    RETRY_PERMISSION_OR_INVALID_PAGE_FAULT, 0);
		WREG32_MMHUB(i,
			     regMMVM_CONTEXT0_CNTL, tmp);
	}
}

static void mmhub_v4_2_0_mid_disable_identity_aperture(struct amdgpu_device *adev,
						       uint32_t inst_mask)
{
	int i;

	/* These registers are not accessible to VF-SRIOV.
	 * The PF will program them instead.
	 */
	if (amdgpu_sriov_vf(adev))
		return;

	for_each_inst(i, inst_mask) {
		WREG32_MMHUB(i,
			     regMMVM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_LO32,
			     0xFFFFFFFF);
		WREG32_MMHUB(i,
			     regMMVM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_HI32,
			     0x00001FFF);

		WREG32_MMHUB(i,
			     regMMVM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_LO32,
			     0);
		WREG32_MMHUB(i,
			     regMMVM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_HI32,
			     0);

		WREG32_MMHUB(i,
			     regMMVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_LO32,
			     0);
		WREG32_MMHUB(i,
			     regMMVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_HI32,
			     0);
	}
}

static void mmhub_v4_2_0_mid_setup_vmid_config(struct amdgpu_device *adev,
					       uint32_t inst_mask)
{
	struct amdgpu_vmhub *hub;
	u32 num_level, block_size;
	uint32_t tmp;
	int i, j;

	num_level = adev->vm_manager.num_level;
	block_size = adev->vm_manager.block_size;
	if (adev->gmc.translate_further)
		num_level -= 1;
	else
		block_size -= 9;

	for_each_inst(j, inst_mask) {
		hub = &adev->vmhub[AMDGPU_MMHUB0(j)];
		for (i = 0; i <= 14; i++) {
			tmp = RREG32_MMHUB_OFFSET(j, regMMVM_CONTEXT1_CNTL,
						 i * hub->ctx_distance);
			tmp = REG_SET_FIELD(tmp, MMVM_CONTEXT1_CNTL, ENABLE_CONTEXT, 1);
			tmp = REG_SET_FIELD(tmp, MMVM_CONTEXT1_CNTL, PAGE_TABLE_DEPTH,
					    num_level);
			tmp = REG_SET_FIELD(tmp, MMVM_CONTEXT1_CNTL,
					    RANGE_PROTECTION_FAULT_ENABLE_DEFAULT, 1);
			tmp = REG_SET_FIELD(tmp, MMVM_CONTEXT1_CNTL,
					    DUMMY_PAGE_PROTECTION_FAULT_ENABLE_DEFAULT,
					    1);
			tmp = REG_SET_FIELD(tmp, MMVM_CONTEXT1_CNTL,
					    PDE0_PROTECTION_FAULT_ENABLE_DEFAULT, 1);
			tmp = REG_SET_FIELD(tmp, MMVM_CONTEXT1_CNTL,
					    VALID_PROTECTION_FAULT_ENABLE_DEFAULT, 1);
			tmp = REG_SET_FIELD(tmp, MMVM_CONTEXT1_CNTL,
					    READ_PROTECTION_FAULT_ENABLE_DEFAULT, 1);
			tmp = REG_SET_FIELD(tmp, MMVM_CONTEXT1_CNTL,
					    WRITE_PROTECTION_FAULT_ENABLE_DEFAULT, 1);
			tmp = REG_SET_FIELD(tmp, MMVM_CONTEXT1_CNTL,
					    EXECUTE_PROTECTION_FAULT_ENABLE_DEFAULT, 1);
			tmp = REG_SET_FIELD(tmp, MMVM_CONTEXT1_CNTL,
					    PAGE_TABLE_BLOCK_SIZE, block_size);
			/* Send no-retry XNACK on fault to suppress VM fault storm. */
			tmp = REG_SET_FIELD(tmp, MMVM_CONTEXT1_CNTL,
					    RETRY_PERMISSION_OR_INVALID_PAGE_FAULT,
					    !adev->gmc.noretry);
			WREG32_MMHUB_OFFSET(j, regMMVM_CONTEXT1_CNTL,
					   i * hub->ctx_distance, tmp);
			WREG32_MMHUB_OFFSET(j,
					   regMMVM_CONTEXT1_PAGE_TABLE_START_ADDR_LO32,
					   i * hub->ctx_addr_distance, 0);
			WREG32_MMHUB_OFFSET(j,
					   regMMVM_CONTEXT1_PAGE_TABLE_START_ADDR_HI32,
					   i * hub->ctx_addr_distance, 0);
			WREG32_MMHUB_OFFSET(j,
					   regMMVM_CONTEXT1_PAGE_TABLE_END_ADDR_LO32,
					   i * hub->ctx_addr_distance,
					   lower_32_bits(adev->vm_manager.max_pfn - 1));
			WREG32_MMHUB_OFFSET(j,
					   regMMVM_CONTEXT1_PAGE_TABLE_END_ADDR_HI32,
					   i * hub->ctx_addr_distance,
					   upper_32_bits(adev->vm_manager.max_pfn - 1));
		}
	}

	hub->vm_cntx_cntl = tmp;
}

static void mmhub_v4_2_0_mid_program_invalidation(struct amdgpu_device *adev,
						  uint32_t inst_mask)
{
	struct amdgpu_vmhub *hub;
	unsigned int i, j;

	for_each_inst(j, inst_mask) {
		hub = &adev->vmhub[AMDGPU_MMHUB0(j)];

		for (i = 0; i < 18; ++i) {
			WREG32_MMHUB_OFFSET(j,
					   regMMVM_INVALIDATE_ENG0_ADDR_RANGE_LO32,
					   i * hub->eng_addr_distance, 0xffffffff);
			WREG32_MMHUB_OFFSET(j,
					   regMMVM_INVALIDATE_ENG0_ADDR_RANGE_HI32,
					   i * hub->eng_addr_distance, 0x3fff);
		}
	}
}

static int mmhub_v4_2_0_mid_gart_enable(struct amdgpu_device *adev,
					uint32_t inst_mask)
{
	/* GART Enable. */
	mmhub_v4_2_0_mid_init_gart_aperture_regs(adev, inst_mask);
	mmhub_v4_2_0_mid_init_system_aperture_regs(adev, inst_mask);
	mmhub_v4_2_0_mid_init_tlb_regs(adev, inst_mask);
	mmhub_v4_2_0_mid_init_cache_regs(adev, inst_mask);

	mmhub_v4_2_0_mid_enable_system_domain(adev, inst_mask);
	mmhub_v4_2_0_mid_disable_identity_aperture(adev, inst_mask);
	mmhub_v4_2_0_mid_setup_vmid_config(adev, inst_mask);
	mmhub_v4_2_0_mid_program_invalidation(adev, inst_mask);

	return 0;
}
static int mmhub_v4_2_0_gart_enable(struct amdgpu_device *adev)
{
	return mmhub_v4_2_0_mid_gart_enable(adev, adev->mmhub.inst_mask);
}

static void mmhub_v4_2_0_mid_gart_disable(struct amdgpu_device *adev,
					  uint32_t inst_mask)
{
	struct amdgpu_vmhub *hub;
	u32 tmp;
	u32 i, j;

	for_each_inst(j, inst_mask) {
		hub = &adev->vmhub[AMDGPU_MMHUB0(j)];
		/* Disable all tables */
		for (i = 0; i < 16; i++)
			WREG32_MMHUB_OFFSET(j, regMMVM_CONTEXT0_CNTL,
					   i * hub->ctx_distance, 0);

		/* Setup TLB control */
		tmp = RREG32_MMHUB(j, regMMMC_VM_MX_L1_TLB_CNTL);
		tmp = REG_SET_FIELD(tmp, MMMC_VM_MX_L1_TLB_CNTL,
				    ENABLE_L1_TLB, 0);
		tmp = REG_SET_FIELD(tmp, MMMC_VM_MX_L1_TLB_CNTL,
				    ENABLE_ADVANCED_DRIVER_MODEL, 0);
		WREG32_MMHUB(j, regMMMC_VM_MX_L1_TLB_CNTL, tmp);

		/* Setup L2 cache */
		tmp = RREG32_MMHUB(j, regMMVM_L2_CNTL);
		tmp = REG_SET_FIELD(tmp, MMVM_L2_CNTL, ENABLE_L2_CACHE, 0);
		WREG32_MMHUB(j, regMMVM_L2_CNTL, tmp);
		WREG32_MMHUB(j, regMMVM_L2_CNTL3, 0);
	}
}

static void mmhub_v4_2_0_gart_disable(struct amdgpu_device *adev)
{
	mmhub_v4_2_0_mid_gart_disable(adev, adev->mmhub.inst_mask);
}

static void
mmhub_v4_2_0_mid_set_fault_enable_default(struct amdgpu_device *adev,
					  bool value, uint32_t inst_mask)
{
	u32 tmp;
	int i;

	/* These registers are not accessible to VF-SRIOV.
	 * The PF will program them instead.
	 */
	if (amdgpu_sriov_vf(adev))
		return;

	for_each_inst(i, inst_mask) {
		tmp = RREG32_MMHUB(i, regMMVM_L2_PROTECTION_FAULT_CNTL_LO32);
		tmp = REG_SET_FIELD(tmp, MMVM_L2_PROTECTION_FAULT_CNTL_LO32,
				    RANGE_PROTECTION_FAULT_ENABLE_DEFAULT, value);
		tmp = REG_SET_FIELD(tmp, MMVM_L2_PROTECTION_FAULT_CNTL_LO32,
				    PDE0_PROTECTION_FAULT_ENABLE_DEFAULT, value);
		tmp = REG_SET_FIELD(tmp, MMVM_L2_PROTECTION_FAULT_CNTL_LO32,
				    PDE1_PROTECTION_FAULT_ENABLE_DEFAULT, value);
		tmp = REG_SET_FIELD(tmp, MMVM_L2_PROTECTION_FAULT_CNTL_LO32,
				    PDE2_PROTECTION_FAULT_ENABLE_DEFAULT, value);
		tmp = REG_SET_FIELD(tmp, MMVM_L2_PROTECTION_FAULT_CNTL_LO32,
				    TRANSLATE_FURTHER_PROTECTION_FAULT_ENABLE_DEFAULT,
				    value);
		tmp = REG_SET_FIELD(tmp, MMVM_L2_PROTECTION_FAULT_CNTL_LO32,
				    NACK_PROTECTION_FAULT_ENABLE_DEFAULT, value);
		tmp = REG_SET_FIELD(tmp, MMVM_L2_PROTECTION_FAULT_CNTL_LO32,
				    DUMMY_PAGE_PROTECTION_FAULT_ENABLE_DEFAULT, value);
		tmp = REG_SET_FIELD(tmp, MMVM_L2_PROTECTION_FAULT_CNTL_LO32,
				    VALID_PROTECTION_FAULT_ENABLE_DEFAULT, value);
		tmp = REG_SET_FIELD(tmp, MMVM_L2_PROTECTION_FAULT_CNTL_LO32,
				    READ_PROTECTION_FAULT_ENABLE_DEFAULT, value);
		tmp = REG_SET_FIELD(tmp, MMVM_L2_PROTECTION_FAULT_CNTL_LO32,
				    WRITE_PROTECTION_FAULT_ENABLE_DEFAULT, value);
		tmp = REG_SET_FIELD(tmp, MMVM_L2_PROTECTION_FAULT_CNTL_LO32,
				    EXECUTE_PROTECTION_FAULT_ENABLE_DEFAULT, value);
		if (!value) {
			tmp = REG_SET_FIELD(tmp, MMVM_L2_PROTECTION_FAULT_CNTL_LO32,
					    CRASH_ON_NO_RETRY_FAULT, 1);
		}
		WREG32_MMHUB(i, regMMVM_L2_PROTECTION_FAULT_CNTL_LO32, tmp);
	}
}


/**
 * mmhub_v4_2_0_set_fault_enable_default - update GART/VM fault handling
 *
 * @adev: amdgpu_device pointer
 * @value: true redirects VM faults to the default page
 */
static void
mmhub_v4_2_0_set_fault_enable_default(struct amdgpu_device *adev,
				      bool value)
{
	mmhub_v4_2_0_mid_set_fault_enable_default(adev, value,
						  adev->mmhub.inst_mask);
}

static uint32_t mmhub_v4_2_0_get_invalidate_req(unsigned int vmid,
						uint32_t flush_type)
{
	u32 req = 0;

	/* invalidate using legacy mode on vmid*/
	req = REG_SET_FIELD(req, MMVM_INVALIDATE_ENG0_REQ,
			    PER_VMID_INVALIDATE_REQ, 1 << vmid);
	/* Only use legacy inv on mmhub side */
	req = REG_SET_FIELD(req, MMVM_INVALIDATE_ENG0_REQ, FLUSH_TYPE, 0);
	req = REG_SET_FIELD(req, MMVM_INVALIDATE_ENG0_REQ, INVALIDATE_L2_PTES, 1);
	req = REG_SET_FIELD(req, MMVM_INVALIDATE_ENG0_REQ, INVALIDATE_L2_PDE0, 1);
	req = REG_SET_FIELD(req, MMVM_INVALIDATE_ENG0_REQ, INVALIDATE_L2_PDE1, 1);
	req = REG_SET_FIELD(req, MMVM_INVALIDATE_ENG0_REQ, INVALIDATE_L2_PDE2, 1);
	req = REG_SET_FIELD(req, MMVM_INVALIDATE_ENG0_REQ, INVALIDATE_L2_PDE3, 1);
	req = REG_SET_FIELD(req, MMVM_INVALIDATE_ENG0_REQ, INVALIDATE_L1_PTES, 1);
	req = REG_SET_FIELD(req, MMVM_INVALIDATE_ENG0_REQ,
			    CLEAR_PROTECTION_FAULT_STATUS_ADDR,	0);

	return req;
}

static void
mmhub_v4_2_0_print_l2_protection_fault_status_hi(struct amdgpu_device *adev,
						 uint32_t status)
{
	dev_err(adev->dev,
		"MMVM_L2_PROTECTION_FAULT_STATUS_HI32:0x%08X\n",
		status);
	dev_err(adev->dev, "\t FED: 0x%lx\n",
		REG_GET_FIELD(status,
			      MMVM_L2_PROTECTION_FAULT_STATUS_HI32, FED));
}

static void
mmhub_v4_2_0_print_l2_protection_fault_status(struct amdgpu_device *adev,
					      uint32_t status)
{
	uint32_t cid, rw;
	const char *mmhub_cid;

	cid = REG_GET_FIELD(status,
			    MMVM_L2_PROTECTION_FAULT_STATUS_LO32, CID);
	rw = REG_GET_FIELD(status,
			   MMVM_L2_PROTECTION_FAULT_STATUS_LO32, RW);

	dev_err(adev->dev,
		"MMVM_L2_PROTECTION_FAULT_STATUS_LO32:0x%08X\n",
		status);
	mmhub_cid = amdgpu_mmhub_client_name(&adev->mmhub, cid, rw);
	dev_err(adev->dev, "\t Faulty UTCL2 client ID: %s (0x%x)\n",
		mmhub_cid ? mmhub_cid : "unknown", cid);
	dev_err(adev->dev, "\t MORE_FAULTS: 0x%lx\n",
		REG_GET_FIELD(status,
		MMVM_L2_PROTECTION_FAULT_STATUS_LO32, MORE_FAULTS));
	dev_err(adev->dev, "\t WALKER_ERROR: 0x%lx\n",
		REG_GET_FIELD(status,
		MMVM_L2_PROTECTION_FAULT_STATUS_LO32, WALKER_ERROR));
	dev_err(adev->dev, "\t PERMISSION_FAULTS: 0x%lx\n",
		REG_GET_FIELD(status,
		MMVM_L2_PROTECTION_FAULT_STATUS_LO32, PERMISSION_FAULTS));
	dev_err(adev->dev, "\t MAPPING_ERROR: 0x%lx\n",
		REG_GET_FIELD(status,
		MMVM_L2_PROTECTION_FAULT_STATUS_LO32, MAPPING_ERROR));
	dev_err(adev->dev, "\t RW: 0x%x\n", rw);
}


static const struct amdgpu_vmhub_funcs mmhub_v4_2_0_vmhub_funcs = {
	.print_l2_protection_fault_status = mmhub_v4_2_0_print_l2_protection_fault_status,
	.print_l2_protection_fault_status_hi =
		mmhub_v4_2_0_print_l2_protection_fault_status_hi,
	.get_invalidate_req = mmhub_v4_2_0_get_invalidate_req,
};

static void mmhub_v4_2_0_mid_init(struct amdgpu_device *adev,
				  uint32_t inst_mask)
{
	struct amdgpu_vmhub *hub;
	int i;

	for_each_inst(i, inst_mask) {
		hub = &adev->vmhub[AMDGPU_MMHUB0(i)];

		hub->ctx0_ptb_addr_lo32 =
			SOC15_REG_OFFSET(MMHUB, GET_INST(MMHUB, i),
					 regMMVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32);
		hub->ctx0_ptb_addr_hi32 =
			SOC15_REG_OFFSET(MMHUB, GET_INST(MMHUB, i),
					 regMMVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_HI32);
		hub->vm_inv_eng0_sem =
			SOC15_REG_OFFSET(MMHUB, GET_INST(MMHUB, i),
					 regMMVM_INVALIDATE_ENG0_SEM);
		hub->vm_inv_eng0_req =
			SOC15_REG_OFFSET(MMHUB, GET_INST(MMHUB, i),
					 regMMVM_INVALIDATE_ENG0_REQ);
		hub->vm_inv_eng0_ack =
			SOC15_REG_OFFSET(MMHUB, GET_INST(MMHUB, i),
					 regMMVM_INVALIDATE_ENG0_ACK);
		hub->vm_context0_cntl =
			SOC15_REG_OFFSET(MMHUB, GET_INST(MMHUB, i),
					 regMMVM_CONTEXT0_CNTL);
		hub->vm_l2_pro_fault_status =
			SOC15_REG_OFFSET(MMHUB, GET_INST(MMHUB, i),
					 regMMVM_L2_PROTECTION_FAULT_STATUS_LO32);
		hub->vm_l2_pro_fault_status_hi =
			SOC15_REG_OFFSET(MMHUB, GET_INST(MMHUB, i),
					 regMMVM_L2_PROTECTION_FAULT_STATUS_HI32);
		hub->vm_l2_pro_fault_cntl =
			SOC15_REG_OFFSET(MMHUB, GET_INST(MMHUB, i),
					 regMMVM_L2_PROTECTION_FAULT_CNTL_LO32);

		hub->ctx_distance = regMMVM_CONTEXT1_CNTL - regMMVM_CONTEXT0_CNTL;
		hub->ctx_addr_distance = regMMVM_CONTEXT1_PAGE_TABLE_BASE_ADDR_LO32 -
					 regMMVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32;
		hub->eng_distance = regMMVM_INVALIDATE_ENG1_REQ -
				    regMMVM_INVALIDATE_ENG0_REQ;
		hub->eng_addr_distance = regMMVM_INVALIDATE_ENG1_ADDR_RANGE_LO32 -
					 regMMVM_INVALIDATE_ENG0_ADDR_RANGE_LO32;

		hub->vm_cntx_cntl_vm_fault = MMVM_CONTEXT1_CNTL__RANGE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK |
			MMVM_CONTEXT1_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK |
			MMVM_CONTEXT1_CNTL__PDE0_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK |
			MMVM_CONTEXT1_CNTL__VALID_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK |
			MMVM_CONTEXT1_CNTL__READ_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK |
			MMVM_CONTEXT1_CNTL__WRITE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK |
			MMVM_CONTEXT1_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK;

		hub->vm_l2_bank_select_reserved_cid2 =
			SOC15_REG_OFFSET(MMHUB, GET_INST(MMHUB, i), regMMVM_L2_BANK_SELECT_RESERVED_CID2);

		hub->vm_contexts_disable =
			SOC15_REG_OFFSET(MMHUB, GET_INST(MMHUB, i), regMMVM_CONTEXTS_DISABLE);

		hub->vmhub_funcs = &mmhub_v4_2_0_vmhub_funcs;
	}
}

static void mmhub_v4_2_0_init(struct amdgpu_device *adev)
{
	mmhub_v4_2_0_mid_init(adev, adev->mmhub.inst_mask);

	amdgpu_mmhub_init_client_info(&adev->mmhub,
				     mmhub_client_ids_v4_2_0,
				     ARRAY_SIZE(mmhub_client_ids_v4_2_0));
}

static void
mmhub_v4_2_0_update_medium_grain_clock_gating(struct amdgpu_device *adev,
					      bool enable)
{
	uint32_t def, data;
	uint32_t def1, data1, def2 = 0, data2 = 0;
	def  = data  = RREG32_SOC15(MMHUB, GET_INST(MMHUB, 0), regMM_ATC_L2_MISC_CG);
	def1 = data1 = RREG32_SOC15(MMHUB, GET_INST(MMHUB, 0), regDAGB0_CNTL_MISC2);
	def2 = data2 = RREG32_SOC15(MMHUB, GET_INST(MMHUB, 0), regDAGB1_CNTL_MISC2);

	if (enable) {
		data |= MM_ATC_L2_MISC_CG__ENABLE_MASK;
		data1 &= ~(DAGB0_CNTL_MISC2__DISABLE_RDRET_TAP_CHAIN_FGCG_MASK |
			   DAGB0_CNTL_MISC2__DISABLE_WRRET_TAP_CHAIN_FGCG_MASK);

		data2 &= ~(DAGB1_CNTL_MISC2__DISABLE_RDRET_TAP_CHAIN_FGCG_MASK |
			   DAGB1_CNTL_MISC2__DISABLE_WRRET_TAP_CHAIN_FGCG_MASK);
	} else {
		data &= ~MM_ATC_L2_MISC_CG__ENABLE_MASK;
		data1 |= (DAGB0_CNTL_MISC2__DISABLE_RDRET_TAP_CHAIN_FGCG_MASK |
			  DAGB0_CNTL_MISC2__DISABLE_WRRET_TAP_CHAIN_FGCG_MASK);

		data2 |= (DAGB1_CNTL_MISC2__DISABLE_RDRET_TAP_CHAIN_FGCG_MASK |
			  DAGB1_CNTL_MISC2__DISABLE_WRRET_TAP_CHAIN_FGCG_MASK);
	}

	if (def != data)
		WREG32_SOC15(MMHUB, GET_INST(MMHUB, 0), regMM_ATC_L2_MISC_CG, data);
	if (def1 != data1)
		WREG32_SOC15(MMHUB, GET_INST(MMHUB, 0), regDAGB0_CNTL_MISC2, data1);

	if (def2 != data2)
		WREG32_SOC15(MMHUB, GET_INST(MMHUB, 0), regDAGB1_CNTL_MISC2, data2);
}

static void
mmhub_v4_2_0_update_medium_grain_light_sleep(struct amdgpu_device *adev,
					     bool enable)
{
	uint32_t def, data;

	def = data = RREG32_SOC15(MMHUB, GET_INST(MMHUB, 0), regMM_ATC_L2_MISC_CG);

	if (enable)
		data |= MM_ATC_L2_MISC_CG__MEM_LS_ENABLE_MASK;
	else
		data &= ~MM_ATC_L2_MISC_CG__MEM_LS_ENABLE_MASK;

	if (def != data)
		WREG32_SOC15(MMHUB, GET_INST(MMHUB, 0), regMM_ATC_L2_MISC_CG, data);
}

static int mmhub_v4_2_0_set_clockgating(struct amdgpu_device *adev,
					enum amd_clockgating_state state)
{
	if (amdgpu_sriov_vf(adev))
		return 0;

	if (adev->cg_flags & AMD_CG_SUPPORT_MC_MGCG)
		mmhub_v4_2_0_update_medium_grain_clock_gating(adev,
				state == AMD_CG_STATE_GATE);

	if (adev->cg_flags & AMD_CG_SUPPORT_MC_LS)
		mmhub_v4_2_0_update_medium_grain_light_sleep(adev,
				state == AMD_CG_STATE_GATE);

	return 0;
}

static void mmhub_v4_2_0_get_clockgating(struct amdgpu_device *adev, u64 *flags)
{
	int data;

	if (amdgpu_sriov_vf(adev))
		*flags = 0;

	data = RREG32_SOC15(MMHUB, GET_INST(MMHUB, 0), regMM_ATC_L2_MISC_CG);

	/* AMD_CG_SUPPORT_MC_MGCG */
	if (data & MM_ATC_L2_MISC_CG__ENABLE_MASK)
		*flags |= AMD_CG_SUPPORT_MC_MGCG;

	/* AMD_CG_SUPPORT_MC_LS */
	if (data & MM_ATC_L2_MISC_CG__MEM_LS_ENABLE_MASK)
		*flags |= AMD_CG_SUPPORT_MC_LS;
}

const struct amdgpu_mmhub_funcs mmhub_v4_2_0_funcs = {
	.init = mmhub_v4_2_0_init,
	.get_fb_location = mmhub_v4_2_0_get_fb_location,
	.get_mc_fb_offset = mmhub_v4_2_0_get_mc_fb_offset,
	.setup_vm_pt_regs = mmhub_v4_2_0_setup_vm_pt_regs,
	.gart_enable = mmhub_v4_2_0_gart_enable,
	.gart_disable = mmhub_v4_2_0_gart_disable,
	.set_fault_enable_default = mmhub_v4_2_0_set_fault_enable_default,
	.set_clockgating = mmhub_v4_2_0_set_clockgating,
	.get_clockgating = mmhub_v4_2_0_get_clockgating,
	.get_xgmi_info = mmhub_v4_2_0_get_xgmi_info,
};

static int mmhub_v4_2_0_xcp_resume(void *handle, uint32_t inst_mask)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	bool value;

	if (amdgpu_vm_fault_stop == AMDGPU_VM_FAULT_STOP_ALWAYS)
		value = false;
	else
		value = true;

	mmhub_v4_2_0_mid_set_fault_enable_default(adev, value, inst_mask);

	if (!amdgpu_sriov_vf(adev))
		return mmhub_v4_2_0_mid_gart_enable(adev, inst_mask);

	return 0;
}

static int mmhub_v4_2_0_xcp_suspend(void *handle, uint32_t inst_mask)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (!amdgpu_sriov_vf(adev))
		mmhub_v4_2_0_mid_gart_disable(adev, inst_mask);

	return 0;
}

struct amdgpu_xcp_ip_funcs mmhub_v4_2_0_xcp_funcs = {
	.suspend = &mmhub_v4_2_0_xcp_suspend,
	.resume = &mmhub_v4_2_0_xcp_resume
};
