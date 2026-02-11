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

#define regMMVM_L2_CNTL3_DEFAULT				0x80100007
#define regMMVM_L2_CNTL4_DEFAULT				0x000000c1
#define regMMVM_L2_CNTL5_DEFAULT				0x00003fe0

static const char *mmhub_client_ids_v4_2_0[][2] = {
	[0][0] = "VMC",
	[4][0] = "DCEDMC",
	[5][0] = "DCEVGA",
	[6][0] = "MP0",
	[7][0] = "MP1",
	[8][0] = "MPIO",
	[16][0] = "HDP",
	[17][0] = "LSDMA",
	[18][0] = "JPEG",
	[19][0] = "VCNU0",
	[21][0] = "VSCH",
	[22][0] = "VCNU1",
	[23][0] = "VCN1",
	[32+20][0] = "VCN0",
	[2][1] = "DBGUNBIO",
	[3][1] = "DCEDWB",
	[4][1] = "DCEDMC",
	[5][1] = "DCEVGA",
	[6][1] = "MP0",
	[7][1] = "MP1",
	[8][1] = "MPIO",
	[10][1] = "DBGU0",
	[11][1] = "DBGU1",
	[12][1] = "DBGU2",
	[13][1] = "DBGU3",
	[14][1] = "XDP",
	[15][1] = "OSSSYS",
	[16][1] = "HDP",
	[17][1] = "LSDMA",
	[18][1] = "JPEG",
	[19][1] = "VCNU0",
	[20][1] = "VCN0",
	[21][1] = "VSCH",
	[22][1] = "VCNU1",
	[23][1] = "VCN1",
};

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
					      uint32_t mid_mask)
{
	struct amdgpu_vmhub *hub;
	int i;

	for_each_inst(i, mid_mask) {
		hub = &adev->vmhub[AMDGPU_MMHUB0(i)];
		WREG32_SOC15_OFFSET(MMHUB, GET_INST(MMHUB, i),
				    regMMVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32,
				    hub->ctx_addr_distance * vmid,
				    lower_32_bits(page_table_base));

		WREG32_SOC15_OFFSET(MMHUB, GET_INST(MMHUB, i),
				    regMMVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_HI32,
				    hub->ctx_addr_distance * vmid,
				    upper_32_bits(page_table_base));
	}
}

static void mmhub_v4_2_0_setup_vm_pt_regs(struct amdgpu_device *adev,
					  uint32_t vmid,
					  uint64_t page_table_base)
{
	uint32_t mid_mask;

	mid_mask = adev->aid_mask;
	mmhub_v4_2_0_mid_setup_vm_pt_regs(adev, vmid,
					  page_table_base,
					  mid_mask);
}

static void mmhub_v4_2_0_mid_init_gart_aperture_regs(struct amdgpu_device *adev,
						     uint32_t mid_mask)
{
	uint64_t pt_base = amdgpu_gmc_pd_addr(adev->gart.bo);
	int i;

	if (adev->gmc.pdb0_bo)
		pt_base = amdgpu_gmc_pd_addr(adev->gmc.pdb0_bo);
	else
		pt_base = amdgpu_gmc_pd_addr(adev->gart.bo);

	mmhub_v4_2_0_mid_setup_vm_pt_regs(adev, 0, pt_base, mid_mask);

	for_each_inst(i, mid_mask) {
		if (adev->gmc.pdb0_bo) {
			WREG32_SOC15(MMHUB, GET_INST(MMHUB, i),
				     regMMVM_CONTEXT0_PAGE_TABLE_START_ADDR_LO32,
				     (u32)(adev->gmc.fb_start >> 12));
			WREG32_SOC15(MMHUB, GET_INST(MMHUB, i),
				     regMMVM_CONTEXT0_PAGE_TABLE_START_ADDR_HI32,
				     (u32)(adev->gmc.fb_start >> 44));

			WREG32_SOC15(MMHUB, GET_INST(MMHUB, i),
				     regMMVM_CONTEXT0_PAGE_TABLE_END_ADDR_LO32,
				     (u32)(adev->gmc.fb_end >> 12));
			WREG32_SOC15(MMHUB, GET_INST(MMHUB, i),
				     regMMVM_CONTEXT0_PAGE_TABLE_END_ADDR_HI32,
				     (u32)(adev->gmc.fb_end >> 44));
		} else {
			WREG32_SOC15(MMHUB, GET_INST(MMHUB, i),
				     regMMVM_CONTEXT0_PAGE_TABLE_START_ADDR_LO32,
				     (u32)(adev->gmc.gart_start >> 12));
			WREG32_SOC15(MMHUB, GET_INST(MMHUB, i),
				     regMMVM_CONTEXT0_PAGE_TABLE_START_ADDR_HI32,
				     (u32)(adev->gmc.gart_start >> 44));

			WREG32_SOC15(MMHUB, GET_INST(MMHUB, i),
				     regMMVM_CONTEXT0_PAGE_TABLE_END_ADDR_LO32,
				     (u32)(adev->gmc.gart_end >> 12));
			WREG32_SOC15(MMHUB, GET_INST(MMHUB, i),
				     regMMVM_CONTEXT0_PAGE_TABLE_END_ADDR_HI32,
				     (u32)(adev->gmc.gart_end >> 44));
		}
	}
}

static void mmhub_v4_2_0_mid_init_system_aperture_regs(struct amdgpu_device *adev,
						       uint32_t mid_mask)
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

	for_each_inst(i, mid_mask) {
		/* Program the AGP BAR */
		WREG32_SOC15(MMHUB, GET_INST(MMHUB, i),
			     regMMMC_VM_AGP_BASE_LO32, 0);
		WREG32_SOC15(MMHUB, GET_INST(MMHUB, i),
			     regMMMC_VM_AGP_BASE_HI32, 0);
		WREG32_SOC15(MMHUB, GET_INST(MMHUB, i),
			     regMMMC_VM_AGP_BOT_LO32,
			     lower_32_bits(adev->gmc.agp_start >> 24));
		WREG32_SOC15(MMHUB, GET_INST(MMHUB, i),
			     regMMMC_VM_AGP_BOT_HI32,
			     upper_32_bits(adev->gmc.agp_start >> 24));
		WREG32_SOC15(MMHUB, GET_INST(MMHUB, i),
			     regMMMC_VM_AGP_TOP_LO32,
			     lower_32_bits(adev->gmc.agp_end >> 24));
		WREG32_SOC15(MMHUB, GET_INST(MMHUB, i),
			     regMMMC_VM_AGP_TOP_HI32,
			     upper_32_bits(adev->gmc.agp_end >> 24));

		/* Program the system aperture low logical page number. */
		WREG32_SOC15(MMHUB, GET_INST(MMHUB, i),
			     regMMMC_VM_SYSTEM_APERTURE_LOW_ADDR_LO32,
			     lower_32_bits(min(adev->gmc.fb_start,
					       adev->gmc.agp_start) >> 18));
		WREG32_SOC15(MMHUB, GET_INST(MMHUB, i),
			     regMMMC_VM_SYSTEM_APERTURE_LOW_ADDR_HI32,
			     upper_32_bits(min(adev->gmc.fb_start,
					       adev->gmc.agp_start) >> 18));
		WREG32_SOC15(MMHUB, GET_INST(MMHUB, i),
			     regMMMC_VM_SYSTEM_APERTURE_HIGH_ADDR_LO32,
			     lower_32_bits(max(adev->gmc.fb_end,
					       adev->gmc.agp_end) >> 18));
		WREG32_SOC15(MMHUB, GET_INST(MMHUB, i),
			     regMMMC_VM_SYSTEM_APERTURE_HIGH_ADDR_HI32,
			     upper_32_bits(max(adev->gmc.fb_end,
					       adev->gmc.agp_end) >> 18));

		/* Set default page address. */
		value = amdgpu_gmc_vram_mc2pa(adev, adev->mem_scratch.gpu_addr);
		WREG32_SOC15(MMHUB, GET_INST(MMHUB, i),
			     regMMMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_LSB,
			     (u32)(value >> 12));
		WREG32_SOC15(MMHUB, GET_INST(MMHUB, i),
			     regMMMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_MSB,
			     (u32)(value >> 44));

		/* Program "protection fault". */
		WREG32_SOC15(MMHUB, GET_INST(MMHUB, i),
			     regMMVM_L2_PROTECTION_FAULT_DEFAULT_ADDR_LO32,
			     (u32)(adev->dummy_page_addr >> 12));
		WREG32_SOC15(MMHUB, GET_INST(MMHUB, i),
			     regMMVM_L2_PROTECTION_FAULT_DEFAULT_ADDR_HI32,
			     (u32)((u64)adev->dummy_page_addr >> 44));

		tmp = RREG32_SOC15(MMHUB, GET_INST(MMHUB, i),
				   regMMVM_L2_PROTECTION_FAULT_CNTL2);
		tmp = REG_SET_FIELD(tmp, MMVM_L2_PROTECTION_FAULT_CNTL2,
				    ACTIVE_PAGE_MIGRATION_PTE_READ_RETRY, 1);
		tmp = REG_SET_FIELD(tmp, MMVM_L2_PROTECTION_FAULT_CNTL2,
				    ENABLE_RETRY_FAULT_INTERRUPT, 0x1);
		WREG32_SOC15(MMHUB, GET_INST(MMHUB, i),
			     regMMVM_L2_PROTECTION_FAULT_CNTL2, tmp);
	}

	/* In the case squeezing vram into GART aperture, we don't use
	 * FB aperture and AGP aperture. Disable them.
	 */
	if (adev->gmc.pdb0_bo) {
		WREG32_SOC15(MMHUB, GET_INST(MMHUB, i),
			     regMMMC_VM_FB_LOCATION_TOP_LO32, 0);
		WREG32_SOC15(MMHUB, GET_INST(MMHUB, i),
			     regMMMC_VM_FB_LOCATION_TOP_HI32, 0);
		WREG32_SOC15(MMHUB, GET_INST(MMHUB, i),
			     regMMMC_VM_FB_LOCATION_BASE_LO32, 0xFFFFFFFF);
		WREG32_SOC15(MMHUB, GET_INST(MMHUB, i),
			     regMMMC_VM_FB_LOCATION_BASE_HI32, 1);
		WREG32_SOC15(MMHUB, GET_INST(MMHUB, i),
			     regMMMC_VM_AGP_TOP_LO32, 0);
		WREG32_SOC15(MMHUB, GET_INST(MMHUB, i),
			     regMMMC_VM_AGP_TOP_HI32, 0);
		WREG32_SOC15(MMHUB, GET_INST(MMHUB, i),
			     regMMMC_VM_AGP_BOT_LO32, 0xFFFFFFFF);
		WREG32_SOC15(MMHUB, GET_INST(MMHUB, i),
			     regMMMC_VM_AGP_BOT_HI32, 1);
		WREG32_SOC15(MMHUB, GET_INST(MMHUB, i),
			     regMMMC_VM_SYSTEM_APERTURE_LOW_ADDR_LO32,
			     0xFFFFFFFF);
		WREG32_SOC15(MMHUB, GET_INST(MMHUB, i),
			     regMMMC_VM_SYSTEM_APERTURE_LOW_ADDR_HI32,
			     0x7F);
		WREG32_SOC15(MMHUB, GET_INST(MMHUB, i),
			     regMMMC_VM_SYSTEM_APERTURE_HIGH_ADDR_LO32, 0);
		WREG32_SOC15(MMHUB, GET_INST(MMHUB, i),
			     regMMMC_VM_SYSTEM_APERTURE_HIGH_ADDR_HI32, 0);
	}
}

static void mmhub_v4_2_0_mid_init_tlb_regs(struct amdgpu_device *adev,
					   uint32_t mid_mask)
{
	uint32_t tmp;
	int i;

	for_each_inst(i, mid_mask) {
		/* Setup TLB control */
		tmp = RREG32_SOC15(MMHUB, GET_INST(MMHUB, i),
				   regMMMC_VM_MX_L1_TLB_CNTL);

		tmp = REG_SET_FIELD(tmp, MMMC_VM_MX_L1_TLB_CNTL, ENABLE_L1_TLB, 1);
		tmp = REG_SET_FIELD(tmp, MMMC_VM_MX_L1_TLB_CNTL, SYSTEM_ACCESS_MODE, 3);
		tmp = REG_SET_FIELD(tmp, MMMC_VM_MX_L1_TLB_CNTL,
				    ENABLE_ADVANCED_DRIVER_MODEL, 1);
		tmp = REG_SET_FIELD(tmp, MMMC_VM_MX_L1_TLB_CNTL,
				    SYSTEM_APERTURE_UNMAPPED_ACCESS, 0);
		tmp = REG_SET_FIELD(tmp, MMMC_VM_MX_L1_TLB_CNTL, ECO_BITS, 0);
		tmp = REG_SET_FIELD(tmp, MMMC_VM_MX_L1_TLB_CNTL,
				    MTYPE, MTYPE_UC); /* UC, uncached */

		WREG32_SOC15(MMHUB, GET_INST(MMHUB, i),
			     regMMMC_VM_MX_L1_TLB_CNTL, tmp);
	}
}

static void mmhub_v4_2_0_mid_init_cache_regs(struct amdgpu_device *adev,
					     uint32_t mid_mask)
{
	uint32_t tmp;
	int i;

	/* These registers are not accessible to VF-SRIOV.
	 * The PF will program them instead.
	 */
	if (amdgpu_sriov_vf(adev))
		return;

	for_each_inst(i, mid_mask) {
		/* Setup L2 cache */
		tmp = RREG32_SOC15(MMHUB, GET_INST(MMHUB, i), regMMVM_L2_CNTL);
		tmp = REG_SET_FIELD(tmp, MMVM_L2_CNTL, ENABLE_L2_CACHE, 1);
		tmp = REG_SET_FIELD(tmp, MMVM_L2_CNTL, ENABLE_L2_FRAGMENT_PROCESSING, 0);
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
		WREG32_SOC15(MMHUB, GET_INST(MMHUB, i), regMMVM_L2_CNTL, tmp);

		tmp = RREG32_SOC15(MMHUB, GET_INST(MMHUB, i), regMMVM_L2_CNTL2);
		tmp = REG_SET_FIELD(tmp, MMVM_L2_CNTL2,
				    INVALIDATE_ALL_L1_TLBS, 1);
		tmp = REG_SET_FIELD(tmp, MMVM_L2_CNTL2,
				    INVALIDATE_L2_CACHE, 1);
		WREG32_SOC15(MMHUB, GET_INST(MMHUB, i), regMMVM_L2_CNTL2, tmp);

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
		WREG32_SOC15(MMHUB, GET_INST(MMHUB, i), regMMVM_L2_CNTL3, tmp);

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
		WREG32_SOC15(MMHUB, GET_INST(MMHUB, i), regMMVM_L2_CNTL4, tmp);

		tmp = regMMVM_L2_CNTL5_DEFAULT;
		tmp = REG_SET_FIELD(tmp, MMVM_L2_CNTL5,
				    L2_CACHE_SMALLK_FRAGMENT_SIZE, 0);
		WREG32_SOC15(MMHUB, GET_INST(MMHUB, i), regMMVM_L2_CNTL5, tmp);
	}
}

static void mmhub_v4_2_0_mid_enable_system_domain(struct amdgpu_device *adev,
						  uint32_t mid_mask)
{
	uint32_t tmp;
	int i;

	for_each_inst(i, mid_mask) {
		tmp = RREG32_SOC15(MMHUB, GET_INST(MMHUB, i),
				   regMMVM_CONTEXT0_CNTL);
		tmp = REG_SET_FIELD(tmp, MMVM_CONTEXT0_CNTL,
				    ENABLE_CONTEXT, 1);
		tmp = REG_SET_FIELD(tmp, MMVM_CONTEXT0_CNTL,
				    PAGE_TABLE_DEPTH, 0);
		tmp = REG_SET_FIELD(tmp, MMVM_CONTEXT0_CNTL,
				    RETRY_PERMISSION_OR_INVALID_PAGE_FAULT, 0);
		WREG32_SOC15(MMHUB, GET_INST(MMHUB, i),
			     regMMVM_CONTEXT0_CNTL, tmp);
	}
}

static void mmhub_v4_2_0_mid_disable_identity_aperture(struct amdgpu_device *adev,
						       uint32_t mid_mask)
{
	int i;

	/* These registers are not accessible to VF-SRIOV.
	 * The PF will program them instead.
	 */
	if (amdgpu_sriov_vf(adev))
		return;

	for_each_inst(i, mid_mask) {
		WREG32_SOC15(MMHUB, GET_INST(MMHUB, i),
			     regMMVM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_LO32,
			     0xFFFFFFFF);
		WREG32_SOC15(MMHUB, GET_INST(MMHUB, i),
			     regMMVM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_HI32,
			     0x00001FFF);

		WREG32_SOC15(MMHUB, GET_INST(MMHUB, i),
			     regMMVM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_LO32,
			     0);
		WREG32_SOC15(MMHUB, GET_INST(MMHUB, i),
			     regMMVM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_HI32,
			     0);

		WREG32_SOC15(MMHUB, GET_INST(MMHUB, i),
			     regMMVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_LO32,
			     0);
		WREG32_SOC15(MMHUB, GET_INST(MMHUB, i),
			     regMMVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_HI32,
			     0);
	}
}

static void mmhub_v4_2_0_mid_setup_vmid_config(struct amdgpu_device *adev,
					       uint32_t mid_mask)
{
	struct amdgpu_vmhub *hub;
	uint32_t tmp;
	int i, j;

	for_each_inst(j, mid_mask) {
		hub = &adev->vmhub[AMDGPU_MMHUB0(j)];
		for (i = 0; i <= 14; i++) {
			tmp = RREG32_SOC15_OFFSET(MMHUB, GET_INST(MMHUB, j),
					          regMMVM_CONTEXT1_CNTL,
						  i * hub->ctx_distance);
			tmp = REG_SET_FIELD(tmp, MMVM_CONTEXT1_CNTL, ENABLE_CONTEXT, 1);
			tmp = REG_SET_FIELD(tmp, MMVM_CONTEXT1_CNTL, PAGE_TABLE_DEPTH,
					    adev->vm_manager.num_level);
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
					    PAGE_TABLE_BLOCK_SIZE,
					    adev->vm_manager.block_size - 9);
			/* Send no-retry XNACK on fault to suppress VM fault storm. */
			tmp = REG_SET_FIELD(tmp, MMVM_CONTEXT1_CNTL,
					    RETRY_PERMISSION_OR_INVALID_PAGE_FAULT,
					    !amdgpu_noretry);
			WREG32_SOC15_OFFSET(MMHUB, GET_INST(MMHUB, j), regMMVM_CONTEXT1_CNTL,
					    i * hub->ctx_distance, tmp);
			WREG32_SOC15_OFFSET(MMHUB, GET_INST(MMHUB, j), regMMVM_CONTEXT1_PAGE_TABLE_START_ADDR_LO32,
					    i * hub->ctx_addr_distance, 0);
			WREG32_SOC15_OFFSET(MMHUB, GET_INST(MMHUB, j), regMMVM_CONTEXT1_PAGE_TABLE_START_ADDR_HI32,
					    i * hub->ctx_addr_distance, 0);
			WREG32_SOC15_OFFSET(MMHUB, GET_INST(MMHUB, j), regMMVM_CONTEXT1_PAGE_TABLE_END_ADDR_LO32,
					    i * hub->ctx_addr_distance,
					    lower_32_bits(adev->vm_manager.max_pfn - 1));
			WREG32_SOC15_OFFSET(MMHUB, GET_INST(MMHUB, j), regMMVM_CONTEXT1_PAGE_TABLE_END_ADDR_HI32,
					    i * hub->ctx_addr_distance,
					    upper_32_bits(adev->vm_manager.max_pfn - 1));
		}
	}

	hub->vm_cntx_cntl = tmp;
}

static void mmhub_v4_2_0_mid_program_invalidation(struct amdgpu_device *adev,
						  uint32_t mid_mask)
{
	struct amdgpu_vmhub *hub;
	unsigned int i, j;

	for_each_inst(j, mid_mask) {
		hub = &adev->vmhub[AMDGPU_MMHUB0(j)];

		for (i = 0; i < 18; ++i) {
			WREG32_SOC15_OFFSET(MMHUB, GET_INST(MMHUB, j),
					    regMMVM_INVALIDATE_ENG0_ADDR_RANGE_LO32,
					    i * hub->eng_addr_distance, 0xffffffff);
			WREG32_SOC15_OFFSET(MMHUB, GET_INST(MMHUB, j),
					    regMMVM_INVALIDATE_ENG0_ADDR_RANGE_HI32,
					    i * hub->eng_addr_distance, 0x3fff);
		}
	}
}

static int mmhub_v4_2_0_mid_gart_enable(struct amdgpu_device *adev,
					uint32_t mid_mask)
{
	/* GART Enable. */
	mmhub_v4_2_0_mid_init_gart_aperture_regs(adev, mid_mask);
	mmhub_v4_2_0_mid_init_system_aperture_regs(adev, mid_mask);
	mmhub_v4_2_0_mid_init_tlb_regs(adev, mid_mask);
	mmhub_v4_2_0_mid_init_cache_regs(adev, mid_mask);

	mmhub_v4_2_0_mid_enable_system_domain(adev, mid_mask);
	mmhub_v4_2_0_mid_disable_identity_aperture(adev, mid_mask);
	mmhub_v4_2_0_mid_setup_vmid_config(adev, mid_mask);
	mmhub_v4_2_0_mid_program_invalidation(adev, mid_mask);

	return 0;
}
static int mmhub_v4_2_0_gart_enable(struct amdgpu_device *adev)
{
	uint32_t mid_mask;

	mid_mask = adev->aid_mask;
	return mmhub_v4_2_0_mid_gart_enable(adev, mid_mask);
}

static void mmhub_v4_2_0_mid_gart_disable(struct amdgpu_device *adev,
					  uint32_t mid_mask)
{
	struct amdgpu_vmhub *hub;
	u32 tmp;
	u32 i, j;

	for_each_inst(j, mid_mask) {
		hub = &adev->vmhub[AMDGPU_MMHUB0(j)];
		/* Disable all tables */
		for (i = 0; i < 16; i++)
			WREG32_SOC15_OFFSET(MMHUB, GET_INST(MMHUB, j),
					    regMMVM_CONTEXT0_CNTL,
					    i * hub->ctx_distance, 0);

		/* Setup TLB control */
		tmp = RREG32_SOC15(MMHUB, GET_INST(MMHUB, j),
				   regMMMC_VM_MX_L1_TLB_CNTL);
		tmp = REG_SET_FIELD(tmp, MMMC_VM_MX_L1_TLB_CNTL,
				    ENABLE_L1_TLB, 0);
		tmp = REG_SET_FIELD(tmp, MMMC_VM_MX_L1_TLB_CNTL,
				    ENABLE_ADVANCED_DRIVER_MODEL, 0);
		WREG32_SOC15(MMHUB, GET_INST(MMHUB, j),
			     regMMMC_VM_MX_L1_TLB_CNTL, tmp);

		/* Setup L2 cache */
		tmp = RREG32_SOC15(MMHUB, GET_INST(MMHUB, j), regMMVM_L2_CNTL);
		tmp = REG_SET_FIELD(tmp, MMVM_L2_CNTL, ENABLE_L2_CACHE, 0);
		WREG32_SOC15(MMHUB, GET_INST(MMHUB, j), regMMVM_L2_CNTL, tmp);
		WREG32_SOC15(MMHUB, GET_INST(MMHUB, j), regMMVM_L2_CNTL3, 0);
	}
}

static void mmhub_v4_2_0_gart_disable(struct amdgpu_device *adev)
{
	uint32_t mid_mask;

	mid_mask = adev->aid_mask;
	mmhub_v4_2_0_mid_gart_disable(adev, mid_mask);
}

static void
mmhub_v4_2_0_mid_set_fault_enable_default(struct amdgpu_device *adev,
					  bool value, uint32_t mid_mask)
{
	u32 tmp;
	int i;

	/* These registers are not accessible to VF-SRIOV.
	 * The PF will program them instead.
	 */
	if (amdgpu_sriov_vf(adev))
		return;

	for_each_inst(i, mid_mask) {
		tmp = RREG32_SOC15(MMHUB, GET_INST(MMHUB, i),
				   regMMVM_L2_PROTECTION_FAULT_CNTL_LO32);
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
		WREG32_SOC15(MMHUB, GET_INST(MMHUB, i),
			     regMMVM_L2_PROTECTION_FAULT_CNTL_LO32, tmp);
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
	uint32_t mid_mask;

	mid_mask = adev->aid_mask;
	mmhub_v4_2_0_mid_set_fault_enable_default(adev, value, mid_mask);
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

/*TODO: l2 protection fault status is increased to 64bits.
 * some critical fields like FED are moved to STATUS_HI32 */
static void
mmhub_v4_2_0_print_l2_protection_fault_status(struct amdgpu_device *adev,
					      uint32_t status)
{
	uint32_t cid, rw;
	const char *mmhub_cid = NULL;

	cid = REG_GET_FIELD(status,
			    MMVM_L2_PROTECTION_FAULT_STATUS_LO32, CID);
	rw = REG_GET_FIELD(status,
			   MMVM_L2_PROTECTION_FAULT_STATUS_LO32, RW);

	dev_err(adev->dev,
		"MMVM_L2_PROTECTION_FAULT_STATUS_LO32:0x%08X\n",
		status);
	switch (amdgpu_ip_version(adev, MMHUB_HWIP, 0)) {
	case IP_VERSION(4, 2, 0):
		mmhub_cid = mmhub_client_ids_v4_2_0[cid][rw];
		break;
	default:
		mmhub_cid = NULL;
		break;
	}
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
	.get_invalidate_req = mmhub_v4_2_0_get_invalidate_req,
};

static void mmhub_v4_2_0_mid_init(struct amdgpu_device *adev,
				  uint32_t mid_mask)
{
	struct amdgpu_vmhub *hub;
	int i;

	for_each_inst(i, mid_mask) {
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
		/* TODO: add a new member to accomandate additional fault status/cntl reg */
		hub->vm_l2_pro_fault_status =
			SOC15_REG_OFFSET(MMHUB, GET_INST(MMHUB, i),
					 regMMVM_L2_PROTECTION_FAULT_STATUS_LO32);
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
	uint32_t mid_mask;

	mid_mask = adev->aid_mask;
	mmhub_v4_2_0_mid_init(adev, mid_mask);
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
