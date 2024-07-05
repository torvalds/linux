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
#include "amdgpu.h"
#include "mmhub_v1_8.h"

#include "mmhub/mmhub_1_8_0_offset.h"
#include "mmhub/mmhub_1_8_0_sh_mask.h"
#include "vega10_enum.h"

#include "soc15_common.h"
#include "soc15.h"
#include "amdgpu_ras.h"

#define regVM_L2_CNTL3_DEFAULT	0x80100007
#define regVM_L2_CNTL4_DEFAULT	0x000000c1
#define mmSMNAID_AID0_MCA_SMU 0x03b30400

static u64 mmhub_v1_8_get_fb_location(struct amdgpu_device *adev)
{
	u64 base = RREG32_SOC15(MMHUB, 0, regMC_VM_FB_LOCATION_BASE);
	u64 top = RREG32_SOC15(MMHUB, 0, regMC_VM_FB_LOCATION_TOP);

	base &= MC_VM_FB_LOCATION_BASE__FB_BASE_MASK;
	base <<= 24;

	top &= MC_VM_FB_LOCATION_TOP__FB_TOP_MASK;
	top <<= 24;

	adev->gmc.fb_start = base;
	adev->gmc.fb_end = top;

	return base;
}

static void mmhub_v1_8_setup_vm_pt_regs(struct amdgpu_device *adev, uint32_t vmid,
				uint64_t page_table_base)
{
	struct amdgpu_vmhub *hub;
	u32 inst_mask;
	int i;

	inst_mask = adev->aid_mask;
	for_each_inst(i, inst_mask) {
		hub = &adev->vmhub[AMDGPU_MMHUB0(i)];
		WREG32_SOC15_OFFSET(MMHUB, i,
				    regVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32,
				    hub->ctx_addr_distance * vmid,
				    lower_32_bits(page_table_base));

		WREG32_SOC15_OFFSET(MMHUB, i,
				    regVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_HI32,
				    hub->ctx_addr_distance * vmid,
				    upper_32_bits(page_table_base));
	}
}

static void mmhub_v1_8_init_gart_aperture_regs(struct amdgpu_device *adev)
{
	uint64_t pt_base;
	u32 inst_mask;
	int i;

	if (adev->gmc.pdb0_bo)
		pt_base = amdgpu_gmc_pd_addr(adev->gmc.pdb0_bo);
	else
		pt_base = amdgpu_gmc_pd_addr(adev->gart.bo);

	mmhub_v1_8_setup_vm_pt_regs(adev, 0, pt_base);

	/* If use GART for FB translation, vmid0 page table covers both
	 * vram and system memory (gart)
	 */
	inst_mask = adev->aid_mask;
	for_each_inst(i, inst_mask) {
		if (adev->gmc.pdb0_bo) {
			WREG32_SOC15(MMHUB, i,
				     regVM_CONTEXT0_PAGE_TABLE_START_ADDR_LO32,
				     (u32)(adev->gmc.fb_start >> 12));
			WREG32_SOC15(MMHUB, i,
				     regVM_CONTEXT0_PAGE_TABLE_START_ADDR_HI32,
				     (u32)(adev->gmc.fb_start >> 44));

			WREG32_SOC15(MMHUB, i,
				     regVM_CONTEXT0_PAGE_TABLE_END_ADDR_LO32,
				     (u32)(adev->gmc.gart_end >> 12));
			WREG32_SOC15(MMHUB, i,
				     regVM_CONTEXT0_PAGE_TABLE_END_ADDR_HI32,
				     (u32)(adev->gmc.gart_end >> 44));

		} else {
			WREG32_SOC15(MMHUB, i,
				     regVM_CONTEXT0_PAGE_TABLE_START_ADDR_LO32,
				     (u32)(adev->gmc.gart_start >> 12));
			WREG32_SOC15(MMHUB, i,
				     regVM_CONTEXT0_PAGE_TABLE_START_ADDR_HI32,
				     (u32)(adev->gmc.gart_start >> 44));

			WREG32_SOC15(MMHUB, i,
				     regVM_CONTEXT0_PAGE_TABLE_END_ADDR_LO32,
				     (u32)(adev->gmc.gart_end >> 12));
			WREG32_SOC15(MMHUB, i,
				     regVM_CONTEXT0_PAGE_TABLE_END_ADDR_HI32,
				     (u32)(adev->gmc.gart_end >> 44));
		}
	}
}

static void mmhub_v1_8_init_system_aperture_regs(struct amdgpu_device *adev)
{
	uint32_t tmp, inst_mask;
	uint64_t value;
	int i;

	if (amdgpu_sriov_vf(adev))
		return;

	inst_mask = adev->aid_mask;
	for_each_inst(i, inst_mask) {
		/* Program the AGP BAR */
		WREG32_SOC15(MMHUB, i, regMC_VM_AGP_BASE, 0);
		WREG32_SOC15(MMHUB, i, regMC_VM_AGP_BOT,
			     adev->gmc.agp_start >> 24);
		WREG32_SOC15(MMHUB, i, regMC_VM_AGP_TOP,
			     adev->gmc.agp_end >> 24);

		/* Program the system aperture low logical page number. */
		WREG32_SOC15(MMHUB, i, regMC_VM_SYSTEM_APERTURE_LOW_ADDR,
			min(adev->gmc.fb_start, adev->gmc.agp_start) >> 18);

		WREG32_SOC15(MMHUB, i, regMC_VM_SYSTEM_APERTURE_HIGH_ADDR,
			max(adev->gmc.fb_end, adev->gmc.agp_end) >> 18);

		/* In the case squeezing vram into GART aperture, we don't use
		 * FB aperture and AGP aperture. Disable them.
		 */
		if (adev->gmc.pdb0_bo) {
			WREG32_SOC15(MMHUB, i, regMC_VM_AGP_BOT, 0xFFFFFF);
			WREG32_SOC15(MMHUB, i, regMC_VM_AGP_TOP, 0);
			WREG32_SOC15(MMHUB, i, regMC_VM_FB_LOCATION_TOP, 0);
			WREG32_SOC15(MMHUB, i, regMC_VM_FB_LOCATION_BASE,
				     0x00FFFFFF);
			WREG32_SOC15(MMHUB, i,
				     regMC_VM_SYSTEM_APERTURE_LOW_ADDR,
				     0x3FFFFFFF);
			WREG32_SOC15(MMHUB, i,
				     regMC_VM_SYSTEM_APERTURE_HIGH_ADDR, 0);
		}

		/* Set default page address. */
		value = amdgpu_gmc_vram_mc2pa(adev, adev->mem_scratch.gpu_addr);
		WREG32_SOC15(MMHUB, i, regMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_LSB,
			     (u32)(value >> 12));
		WREG32_SOC15(MMHUB, i, regMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_MSB,
			     (u32)(value >> 44));

		/* Program "protection fault". */
		WREG32_SOC15(MMHUB, i,
			     regVM_L2_PROTECTION_FAULT_DEFAULT_ADDR_LO32,
			     (u32)(adev->dummy_page_addr >> 12));
		WREG32_SOC15(MMHUB, i,
			     regVM_L2_PROTECTION_FAULT_DEFAULT_ADDR_HI32,
			     (u32)((u64)adev->dummy_page_addr >> 44));

		tmp = RREG32_SOC15(MMHUB, i, regVM_L2_PROTECTION_FAULT_CNTL2);
		tmp = REG_SET_FIELD(tmp, VM_L2_PROTECTION_FAULT_CNTL2,
				    ACTIVE_PAGE_MIGRATION_PTE_READ_RETRY, 1);
		WREG32_SOC15(MMHUB, i, regVM_L2_PROTECTION_FAULT_CNTL2, tmp);
	}
}

static void mmhub_v1_8_init_tlb_regs(struct amdgpu_device *adev)
{
	uint32_t tmp, inst_mask;
	int i;

	/* Setup TLB control */
	inst_mask = adev->aid_mask;
	for_each_inst(i, inst_mask) {
		tmp = RREG32_SOC15(MMHUB, i, regMC_VM_MX_L1_TLB_CNTL);

		tmp = REG_SET_FIELD(tmp, MC_VM_MX_L1_TLB_CNTL, ENABLE_L1_TLB,
				    1);
		tmp = REG_SET_FIELD(tmp, MC_VM_MX_L1_TLB_CNTL,
				    SYSTEM_ACCESS_MODE, 3);
		tmp = REG_SET_FIELD(tmp, MC_VM_MX_L1_TLB_CNTL,
				    ENABLE_ADVANCED_DRIVER_MODEL, 1);
		tmp = REG_SET_FIELD(tmp, MC_VM_MX_L1_TLB_CNTL,
				    SYSTEM_APERTURE_UNMAPPED_ACCESS, 0);
		tmp = REG_SET_FIELD(tmp, MC_VM_MX_L1_TLB_CNTL,
				    MTYPE, MTYPE_UC);/* XXX for emulation. */
		tmp = REG_SET_FIELD(tmp, MC_VM_MX_L1_TLB_CNTL, ATC_EN, 1);

		WREG32_SOC15(MMHUB, i, regMC_VM_MX_L1_TLB_CNTL, tmp);
	}
}

static void mmhub_v1_8_init_cache_regs(struct amdgpu_device *adev)
{
	uint32_t tmp, inst_mask;
	int i;

	if (amdgpu_sriov_vf(adev))
		return;

	/* Setup L2 cache */
	inst_mask = adev->aid_mask;
	for_each_inst(i, inst_mask) {
		tmp = RREG32_SOC15(MMHUB, i, regVM_L2_CNTL);
		tmp = REG_SET_FIELD(tmp, VM_L2_CNTL, ENABLE_L2_CACHE, 1);
		tmp = REG_SET_FIELD(tmp, VM_L2_CNTL,
				    ENABLE_L2_FRAGMENT_PROCESSING, 1);
		/* XXX for emulation, Refer to closed source code.*/
		tmp = REG_SET_FIELD(tmp, VM_L2_CNTL,
				    L2_PDE0_CACHE_TAG_GENERATION_MODE, 0);
		tmp = REG_SET_FIELD(tmp, VM_L2_CNTL, PDE_FAULT_CLASSIFICATION,
				    0);
		tmp = REG_SET_FIELD(tmp, VM_L2_CNTL,
				    CONTEXT1_IDENTITY_ACCESS_MODE, 1);
		tmp = REG_SET_FIELD(tmp, VM_L2_CNTL,
				    IDENTITY_MODE_FRAGMENT_SIZE, 0);
		WREG32_SOC15(MMHUB, i, regVM_L2_CNTL, tmp);

		tmp = RREG32_SOC15(MMHUB, i, regVM_L2_CNTL2);
		tmp = REG_SET_FIELD(tmp, VM_L2_CNTL2, INVALIDATE_ALL_L1_TLBS,
				    1);
		tmp = REG_SET_FIELD(tmp, VM_L2_CNTL2, INVALIDATE_L2_CACHE, 1);
		WREG32_SOC15(MMHUB, i, regVM_L2_CNTL2, tmp);

		tmp = regVM_L2_CNTL3_DEFAULT;
		if (adev->gmc.translate_further) {
			tmp = REG_SET_FIELD(tmp, VM_L2_CNTL3, BANK_SELECT, 12);
			tmp = REG_SET_FIELD(tmp, VM_L2_CNTL3,
					    L2_CACHE_BIGK_FRAGMENT_SIZE, 9);
		} else {
			tmp = REG_SET_FIELD(tmp, VM_L2_CNTL3, BANK_SELECT, 9);
			tmp = REG_SET_FIELD(tmp, VM_L2_CNTL3,
					    L2_CACHE_BIGK_FRAGMENT_SIZE, 6);
		}
		WREG32_SOC15(MMHUB, i, regVM_L2_CNTL3, tmp);

		tmp = regVM_L2_CNTL4_DEFAULT;
		/* For AMD APP APUs setup WC memory */
		if (adev->gmc.xgmi.connected_to_cpu || adev->gmc.is_app_apu) {
			tmp = REG_SET_FIELD(tmp, VM_L2_CNTL4,
					    VMC_TAP_PDE_REQUEST_PHYSICAL, 1);
			tmp = REG_SET_FIELD(tmp, VM_L2_CNTL4,
					    VMC_TAP_PTE_REQUEST_PHYSICAL, 1);
		} else {
			tmp = REG_SET_FIELD(tmp, VM_L2_CNTL4,
					    VMC_TAP_PDE_REQUEST_PHYSICAL, 0);
			tmp = REG_SET_FIELD(tmp, VM_L2_CNTL4,
					    VMC_TAP_PTE_REQUEST_PHYSICAL, 0);
		}
		WREG32_SOC15(MMHUB, i, regVM_L2_CNTL4, tmp);
	}
}

static void mmhub_v1_8_enable_system_domain(struct amdgpu_device *adev)
{
	uint32_t tmp, inst_mask;
	int i;

	inst_mask = adev->aid_mask;
	for_each_inst(i, inst_mask) {
		tmp = RREG32_SOC15(MMHUB, i, regVM_CONTEXT0_CNTL);
		tmp = REG_SET_FIELD(tmp, VM_CONTEXT0_CNTL, ENABLE_CONTEXT, 1);
		tmp = REG_SET_FIELD(tmp, VM_CONTEXT0_CNTL, PAGE_TABLE_DEPTH,
				adev->gmc.vmid0_page_table_depth);
		tmp = REG_SET_FIELD(tmp,
				    VM_CONTEXT0_CNTL, PAGE_TABLE_BLOCK_SIZE,
				    adev->gmc.vmid0_page_table_block_size);
		tmp = REG_SET_FIELD(tmp, VM_CONTEXT0_CNTL,
				    RETRY_PERMISSION_OR_INVALID_PAGE_FAULT, 0);
		WREG32_SOC15(MMHUB, i, regVM_CONTEXT0_CNTL, tmp);
	}
}

static void mmhub_v1_8_disable_identity_aperture(struct amdgpu_device *adev)
{
	u32 inst_mask;
	int i;

	if (amdgpu_sriov_vf(adev))
		return;

	inst_mask = adev->aid_mask;
	for_each_inst(i, inst_mask) {
		WREG32_SOC15(MMHUB, i,
			     regVM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_LO32,
			     0XFFFFFFFF);
		WREG32_SOC15(MMHUB, i,
			     regVM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_HI32,
			     0x0000000F);

		WREG32_SOC15(MMHUB, i,
			     regVM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_LO32,
			     0);
		WREG32_SOC15(MMHUB, i,
			     regVM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_HI32,
			     0);

		WREG32_SOC15(MMHUB, i,
			     regVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_LO32, 0);
		WREG32_SOC15(MMHUB, i,
			     regVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_HI32, 0);
	}
}

static void mmhub_v1_8_setup_vmid_config(struct amdgpu_device *adev)
{
	struct amdgpu_vmhub *hub;
	unsigned int num_level, block_size;
	uint32_t tmp, inst_mask;
	int i, j;

	num_level = adev->vm_manager.num_level;
	block_size = adev->vm_manager.block_size;
	if (adev->gmc.translate_further)
		num_level -= 1;
	else
		block_size -= 9;

	inst_mask = adev->aid_mask;
	for_each_inst(j, inst_mask) {
		hub = &adev->vmhub[AMDGPU_MMHUB0(j)];
		for (i = 0; i <= 14; i++) {
			tmp = RREG32_SOC15_OFFSET(MMHUB, j, regVM_CONTEXT1_CNTL,
						  i * hub->ctx_distance);
			tmp = REG_SET_FIELD(tmp, VM_CONTEXT1_CNTL,
					    ENABLE_CONTEXT, 1);
			tmp = REG_SET_FIELD(tmp, VM_CONTEXT1_CNTL,
					    PAGE_TABLE_DEPTH, num_level);
			tmp = REG_SET_FIELD(tmp, VM_CONTEXT1_CNTL,
				RANGE_PROTECTION_FAULT_ENABLE_DEFAULT, 1);
			tmp = REG_SET_FIELD(tmp, VM_CONTEXT1_CNTL,
				DUMMY_PAGE_PROTECTION_FAULT_ENABLE_DEFAULT, 1);
			tmp = REG_SET_FIELD(tmp, VM_CONTEXT1_CNTL,
				PDE0_PROTECTION_FAULT_ENABLE_DEFAULT, 1);
			tmp = REG_SET_FIELD(tmp, VM_CONTEXT1_CNTL,
				VALID_PROTECTION_FAULT_ENABLE_DEFAULT, 1);
			tmp = REG_SET_FIELD(tmp, VM_CONTEXT1_CNTL,
				READ_PROTECTION_FAULT_ENABLE_DEFAULT, 1);
			tmp = REG_SET_FIELD(tmp, VM_CONTEXT1_CNTL,
				WRITE_PROTECTION_FAULT_ENABLE_DEFAULT, 1);
			tmp = REG_SET_FIELD(tmp, VM_CONTEXT1_CNTL,
				EXECUTE_PROTECTION_FAULT_ENABLE_DEFAULT, 1);
			tmp = REG_SET_FIELD(tmp, VM_CONTEXT1_CNTL,
					    PAGE_TABLE_BLOCK_SIZE,
					    block_size);
			/* On 9.4.3, XNACK can be enabled in the SQ
			 * per-process. Retry faults need to be enabled for
			 * that to work.
			 */
			tmp = REG_SET_FIELD(tmp, VM_CONTEXT1_CNTL,
				RETRY_PERMISSION_OR_INVALID_PAGE_FAULT, 1);
			WREG32_SOC15_OFFSET(MMHUB, j, regVM_CONTEXT1_CNTL,
					    i * hub->ctx_distance, tmp);
			WREG32_SOC15_OFFSET(MMHUB, j,
				regVM_CONTEXT1_PAGE_TABLE_START_ADDR_LO32,
				i * hub->ctx_addr_distance, 0);
			WREG32_SOC15_OFFSET(MMHUB, j,
				regVM_CONTEXT1_PAGE_TABLE_START_ADDR_HI32,
				i * hub->ctx_addr_distance, 0);
			WREG32_SOC15_OFFSET(MMHUB, j,
				regVM_CONTEXT1_PAGE_TABLE_END_ADDR_LO32,
				i * hub->ctx_addr_distance,
				lower_32_bits(adev->vm_manager.max_pfn - 1));
			WREG32_SOC15_OFFSET(MMHUB, j,
				regVM_CONTEXT1_PAGE_TABLE_END_ADDR_HI32,
				i * hub->ctx_addr_distance,
				upper_32_bits(adev->vm_manager.max_pfn - 1));
		}
	}
}

static void mmhub_v1_8_program_invalidation(struct amdgpu_device *adev)
{
	struct amdgpu_vmhub *hub;
	u32 i, j, inst_mask;

	inst_mask = adev->aid_mask;
	for_each_inst(j, inst_mask) {
		hub = &adev->vmhub[AMDGPU_MMHUB0(j)];
		for (i = 0; i < 18; ++i) {
			WREG32_SOC15_OFFSET(MMHUB, j,
					regVM_INVALIDATE_ENG0_ADDR_RANGE_LO32,
					i * hub->eng_addr_distance, 0xffffffff);
			WREG32_SOC15_OFFSET(MMHUB, j,
					regVM_INVALIDATE_ENG0_ADDR_RANGE_HI32,
					i * hub->eng_addr_distance, 0x1f);
		}
	}
}

static int mmhub_v1_8_gart_enable(struct amdgpu_device *adev)
{
	/* GART Enable. */
	mmhub_v1_8_init_gart_aperture_regs(adev);
	mmhub_v1_8_init_system_aperture_regs(adev);
	mmhub_v1_8_init_tlb_regs(adev);
	mmhub_v1_8_init_cache_regs(adev);

	mmhub_v1_8_enable_system_domain(adev);
	mmhub_v1_8_disable_identity_aperture(adev);
	mmhub_v1_8_setup_vmid_config(adev);
	mmhub_v1_8_program_invalidation(adev);

	return 0;
}

static void mmhub_v1_8_gart_disable(struct amdgpu_device *adev)
{
	struct amdgpu_vmhub *hub;
	u32 tmp;
	u32 i, j, inst_mask;

	/* Disable all tables */
	inst_mask = adev->aid_mask;
	for_each_inst(j, inst_mask) {
		hub = &adev->vmhub[AMDGPU_MMHUB0(j)];
		for (i = 0; i < 16; i++)
			WREG32_SOC15_OFFSET(MMHUB, j, regVM_CONTEXT0_CNTL,
					    i * hub->ctx_distance, 0);

		/* Setup TLB control */
		tmp = RREG32_SOC15(MMHUB, j, regMC_VM_MX_L1_TLB_CNTL);
		tmp = REG_SET_FIELD(tmp, MC_VM_MX_L1_TLB_CNTL, ENABLE_L1_TLB,
				    0);
		tmp = REG_SET_FIELD(tmp, MC_VM_MX_L1_TLB_CNTL,
				    ENABLE_ADVANCED_DRIVER_MODEL, 0);
		WREG32_SOC15(MMHUB, j, regMC_VM_MX_L1_TLB_CNTL, tmp);

		if (!amdgpu_sriov_vf(adev)) {
			/* Setup L2 cache */
			tmp = RREG32_SOC15(MMHUB, j, regVM_L2_CNTL);
			tmp = REG_SET_FIELD(tmp, VM_L2_CNTL, ENABLE_L2_CACHE,
					    0);
			WREG32_SOC15(MMHUB, j, regVM_L2_CNTL, tmp);
			WREG32_SOC15(MMHUB, j, regVM_L2_CNTL3, 0);
		}
	}
}

/**
 * mmhub_v1_8_set_fault_enable_default - update GART/VM fault handling
 *
 * @adev: amdgpu_device pointer
 * @value: true redirects VM faults to the default page
 */
static void mmhub_v1_8_set_fault_enable_default(struct amdgpu_device *adev, bool value)
{
	u32 tmp, inst_mask;
	int i;

	if (amdgpu_sriov_vf(adev))
		return;

	inst_mask = adev->aid_mask;
	for_each_inst(i, inst_mask) {
		tmp = RREG32_SOC15(MMHUB, i, regVM_L2_PROTECTION_FAULT_CNTL);
		tmp = REG_SET_FIELD(tmp, VM_L2_PROTECTION_FAULT_CNTL,
				RANGE_PROTECTION_FAULT_ENABLE_DEFAULT, value);
		tmp = REG_SET_FIELD(tmp, VM_L2_PROTECTION_FAULT_CNTL,
				PDE0_PROTECTION_FAULT_ENABLE_DEFAULT, value);
		tmp = REG_SET_FIELD(tmp, VM_L2_PROTECTION_FAULT_CNTL,
				PDE1_PROTECTION_FAULT_ENABLE_DEFAULT, value);
		tmp = REG_SET_FIELD(tmp, VM_L2_PROTECTION_FAULT_CNTL,
				PDE2_PROTECTION_FAULT_ENABLE_DEFAULT, value);
		tmp = REG_SET_FIELD(tmp, VM_L2_PROTECTION_FAULT_CNTL,
			TRANSLATE_FURTHER_PROTECTION_FAULT_ENABLE_DEFAULT,
			value);
		tmp = REG_SET_FIELD(tmp, VM_L2_PROTECTION_FAULT_CNTL,
				NACK_PROTECTION_FAULT_ENABLE_DEFAULT, value);
		tmp = REG_SET_FIELD(tmp, VM_L2_PROTECTION_FAULT_CNTL,
				DUMMY_PAGE_PROTECTION_FAULT_ENABLE_DEFAULT, value);
		tmp = REG_SET_FIELD(tmp, VM_L2_PROTECTION_FAULT_CNTL,
				VALID_PROTECTION_FAULT_ENABLE_DEFAULT, value);
		tmp = REG_SET_FIELD(tmp, VM_L2_PROTECTION_FAULT_CNTL,
				READ_PROTECTION_FAULT_ENABLE_DEFAULT, value);
		tmp = REG_SET_FIELD(tmp, VM_L2_PROTECTION_FAULT_CNTL,
				WRITE_PROTECTION_FAULT_ENABLE_DEFAULT, value);
		tmp = REG_SET_FIELD(tmp, VM_L2_PROTECTION_FAULT_CNTL,
				EXECUTE_PROTECTION_FAULT_ENABLE_DEFAULT, value);
		if (!value) {
			tmp = REG_SET_FIELD(tmp, VM_L2_PROTECTION_FAULT_CNTL,
					    CRASH_ON_NO_RETRY_FAULT, 1);
			tmp = REG_SET_FIELD(tmp, VM_L2_PROTECTION_FAULT_CNTL,
					    CRASH_ON_RETRY_FAULT, 1);
		}

		WREG32_SOC15(MMHUB, i, regVM_L2_PROTECTION_FAULT_CNTL, tmp);
	}
}

static void mmhub_v1_8_init(struct amdgpu_device *adev)
{
	struct amdgpu_vmhub *hub;
	u32 inst_mask;
	int i;

	inst_mask = adev->aid_mask;
	for_each_inst(i, inst_mask) {
		hub = &adev->vmhub[AMDGPU_MMHUB0(i)];

		hub->ctx0_ptb_addr_lo32 = SOC15_REG_OFFSET(MMHUB, i,
			regVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32);
		hub->ctx0_ptb_addr_hi32 = SOC15_REG_OFFSET(MMHUB, i,
			regVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_HI32);
		hub->vm_inv_eng0_req =
			SOC15_REG_OFFSET(MMHUB, i, regVM_INVALIDATE_ENG0_REQ);
		hub->vm_inv_eng0_ack =
			SOC15_REG_OFFSET(MMHUB, i, regVM_INVALIDATE_ENG0_ACK);
		hub->vm_context0_cntl =
			SOC15_REG_OFFSET(MMHUB, i, regVM_CONTEXT0_CNTL);
		hub->vm_l2_pro_fault_status = SOC15_REG_OFFSET(MMHUB, i,
			regVM_L2_PROTECTION_FAULT_STATUS);
		hub->vm_l2_pro_fault_cntl = SOC15_REG_OFFSET(MMHUB, i,
			regVM_L2_PROTECTION_FAULT_CNTL);

		hub->ctx_distance = regVM_CONTEXT1_CNTL - regVM_CONTEXT0_CNTL;
		hub->ctx_addr_distance =
			regVM_CONTEXT1_PAGE_TABLE_BASE_ADDR_LO32 -
			regVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32;
		hub->eng_distance = regVM_INVALIDATE_ENG1_REQ -
			regVM_INVALIDATE_ENG0_REQ;
		hub->eng_addr_distance = regVM_INVALIDATE_ENG1_ADDR_RANGE_LO32 -
			regVM_INVALIDATE_ENG0_ADDR_RANGE_LO32;
	}
}

static int mmhub_v1_8_set_clockgating(struct amdgpu_device *adev,
				      enum amd_clockgating_state state)
{
	return 0;
}

static void mmhub_v1_8_get_clockgating(struct amdgpu_device *adev, u64 *flags)
{

}

static bool mmhub_v1_8_query_utcl2_poison_status(struct amdgpu_device *adev,
				int hub_inst)
{
	u32 fed, status;

	status = RREG32_SOC15(MMHUB, hub_inst, regVM_L2_PROTECTION_FAULT_STATUS);
	fed = REG_GET_FIELD(status, VM_L2_PROTECTION_FAULT_STATUS, FED);
	if (!amdgpu_sriov_vf(adev)) {
		/* clear page fault status and address */
		WREG32_P(SOC15_REG_OFFSET(MMHUB, hub_inst,
			 regVM_L2_PROTECTION_FAULT_CNTL), 1, ~1);
	}

	return fed;
}

const struct amdgpu_mmhub_funcs mmhub_v1_8_funcs = {
	.get_fb_location = mmhub_v1_8_get_fb_location,
	.init = mmhub_v1_8_init,
	.gart_enable = mmhub_v1_8_gart_enable,
	.set_fault_enable_default = mmhub_v1_8_set_fault_enable_default,
	.gart_disable = mmhub_v1_8_gart_disable,
	.setup_vm_pt_regs = mmhub_v1_8_setup_vm_pt_regs,
	.set_clockgating = mmhub_v1_8_set_clockgating,
	.get_clockgating = mmhub_v1_8_get_clockgating,
	.query_utcl2_poison_status = mmhub_v1_8_query_utcl2_poison_status,
};

static const struct amdgpu_ras_err_status_reg_entry mmhub_v1_8_ce_reg_list[] = {
	{AMDGPU_RAS_REG_ENTRY(MMHUB, 0, regMMEA0_CE_ERR_STATUS_LO, regMMEA0_CE_ERR_STATUS_HI),
	1, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "MMEA0"},
	{AMDGPU_RAS_REG_ENTRY(MMHUB, 0, regMMEA1_CE_ERR_STATUS_LO, regMMEA1_CE_ERR_STATUS_HI),
	1, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "MMEA1"},
	{AMDGPU_RAS_REG_ENTRY(MMHUB, 0, regMMEA2_CE_ERR_STATUS_LO, regMMEA2_CE_ERR_STATUS_HI),
	1, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "MMEA2"},
	{AMDGPU_RAS_REG_ENTRY(MMHUB, 0, regMMEA3_CE_ERR_STATUS_LO, regMMEA3_CE_ERR_STATUS_HI),
	1, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "MMEA3"},
	{AMDGPU_RAS_REG_ENTRY(MMHUB, 0, regMMEA4_CE_ERR_STATUS_LO, regMMEA4_CE_ERR_STATUS_HI),
	1, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "MMEA4"},
	{AMDGPU_RAS_REG_ENTRY(MMHUB, 0, regMM_CANE_CE_ERR_STATUS_LO, regMM_CANE_CE_ERR_STATUS_HI),
	1, 0, "MM_CANE"},
};

static const struct amdgpu_ras_err_status_reg_entry mmhub_v1_8_ue_reg_list[] = {
	{AMDGPU_RAS_REG_ENTRY(MMHUB, 0, regMMEA0_UE_ERR_STATUS_LO, regMMEA0_UE_ERR_STATUS_HI),
	1, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "MMEA0"},
	{AMDGPU_RAS_REG_ENTRY(MMHUB, 0, regMMEA1_UE_ERR_STATUS_LO, regMMEA1_UE_ERR_STATUS_HI),
	1, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "MMEA1"},
	{AMDGPU_RAS_REG_ENTRY(MMHUB, 0, regMMEA2_UE_ERR_STATUS_LO, regMMEA2_UE_ERR_STATUS_HI),
	1, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "MMEA2"},
	{AMDGPU_RAS_REG_ENTRY(MMHUB, 0, regMMEA3_UE_ERR_STATUS_LO, regMMEA3_UE_ERR_STATUS_HI),
	1, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "MMEA3"},
	{AMDGPU_RAS_REG_ENTRY(MMHUB, 0, regMMEA4_UE_ERR_STATUS_LO, regMMEA4_UE_ERR_STATUS_HI),
	1, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "MMEA4"},
	{AMDGPU_RAS_REG_ENTRY(MMHUB, 0, regMM_CANE_UE_ERR_STATUS_LO, regMM_CANE_UE_ERR_STATUS_HI),
	1, 0, "MM_CANE"},
};

static const struct amdgpu_ras_memory_id_entry mmhub_v1_8_ras_memory_list[] = {
	{AMDGPU_MMHUB_WGMI_PAGEMEM, "MMEA_WGMI_PAGEMEM"},
	{AMDGPU_MMHUB_RGMI_PAGEMEM, "MMEA_RGMI_PAGEMEM"},
	{AMDGPU_MMHUB_WDRAM_PAGEMEM, "MMEA_WDRAM_PAGEMEM"},
	{AMDGPU_MMHUB_RDRAM_PAGEMEM, "MMEA_RDRAM_PAGEMEM"},
	{AMDGPU_MMHUB_WIO_CMDMEM, "MMEA_WIO_CMDMEM"},
	{AMDGPU_MMHUB_RIO_CMDMEM, "MMEA_RIO_CMDMEM"},
	{AMDGPU_MMHUB_WGMI_CMDMEM, "MMEA_WGMI_CMDMEM"},
	{AMDGPU_MMHUB_RGMI_CMDMEM, "MMEA_RGMI_CMDMEM"},
	{AMDGPU_MMHUB_WDRAM_CMDMEM, "MMEA_WDRAM_CMDMEM"},
	{AMDGPU_MMHUB_RDRAM_CMDMEM, "MMEA_RDRAM_CMDMEM"},
	{AMDGPU_MMHUB_MAM_DMEM0, "MMEA_MAM_DMEM0"},
	{AMDGPU_MMHUB_MAM_DMEM1, "MMEA_MAM_DMEM1"},
	{AMDGPU_MMHUB_MAM_DMEM2, "MMEA_MAM_DMEM2"},
	{AMDGPU_MMHUB_MAM_DMEM3, "MMEA_MAM_DMEM3"},
	{AMDGPU_MMHUB_WRET_TAGMEM, "MMEA_WRET_TAGMEM"},
	{AMDGPU_MMHUB_RRET_TAGMEM, "MMEA_RRET_TAGMEM"},
	{AMDGPU_MMHUB_WIO_DATAMEM, "MMEA_WIO_DATAMEM"},
	{AMDGPU_MMHUB_WGMI_DATAMEM, "MMEA_WGMI_DATAMEM"},
	{AMDGPU_MMHUB_WDRAM_DATAMEM, "MMEA_WDRAM_DATAMEM"},
};

static void mmhub_v1_8_inst_query_ras_error_count(struct amdgpu_device *adev,
						  uint32_t mmhub_inst,
						  void *ras_err_status)
{
	struct ras_err_data *err_data = (struct ras_err_data *)ras_err_status;
	unsigned long ue_count = 0, ce_count = 0;

	/* NOTE: mmhub is converted by aid_mask and the range is 0-3,
	 * which can be used as die ID directly */
	struct amdgpu_smuio_mcm_config_info mcm_info = {
		.socket_id = adev->smuio.funcs->get_socket_id(adev),
		.die_id = mmhub_inst,
	};

	amdgpu_ras_inst_query_ras_error_count(adev,
					mmhub_v1_8_ce_reg_list,
					ARRAY_SIZE(mmhub_v1_8_ce_reg_list),
					mmhub_v1_8_ras_memory_list,
					ARRAY_SIZE(mmhub_v1_8_ras_memory_list),
					mmhub_inst,
					AMDGPU_RAS_ERROR__SINGLE_CORRECTABLE,
					&ce_count);
	amdgpu_ras_inst_query_ras_error_count(adev,
					mmhub_v1_8_ue_reg_list,
					ARRAY_SIZE(mmhub_v1_8_ue_reg_list),
					mmhub_v1_8_ras_memory_list,
					ARRAY_SIZE(mmhub_v1_8_ras_memory_list),
					mmhub_inst,
					AMDGPU_RAS_ERROR__MULTI_UNCORRECTABLE,
					&ue_count);

	amdgpu_ras_error_statistic_ce_count(err_data, &mcm_info, NULL, ce_count);
	amdgpu_ras_error_statistic_ue_count(err_data, &mcm_info, NULL, ue_count);
}

static void mmhub_v1_8_query_ras_error_count(struct amdgpu_device *adev,
					     void *ras_err_status)
{
	uint32_t inst_mask;
	uint32_t i;

	if (!amdgpu_ras_is_supported(adev, AMDGPU_RAS_BLOCK__MMHUB)) {
		dev_warn(adev->dev, "MMHUB RAS is not supported\n");
		return;
	}

	inst_mask = adev->aid_mask;
	for_each_inst(i, inst_mask)
		mmhub_v1_8_inst_query_ras_error_count(adev, i, ras_err_status);
}

static void mmhub_v1_8_inst_reset_ras_error_count(struct amdgpu_device *adev,
						  uint32_t mmhub_inst)
{
	amdgpu_ras_inst_reset_ras_error_count(adev,
					mmhub_v1_8_ce_reg_list,
					ARRAY_SIZE(mmhub_v1_8_ce_reg_list),
					mmhub_inst);
	amdgpu_ras_inst_reset_ras_error_count(adev,
					mmhub_v1_8_ue_reg_list,
					ARRAY_SIZE(mmhub_v1_8_ue_reg_list),
					mmhub_inst);
}

static void mmhub_v1_8_reset_ras_error_count(struct amdgpu_device *adev)
{
	uint32_t inst_mask;
	uint32_t i;

	if (!amdgpu_ras_is_supported(adev, AMDGPU_RAS_BLOCK__MMHUB)) {
		dev_warn(adev->dev, "MMHUB RAS is not supported\n");
		return;
	}

	inst_mask = adev->aid_mask;
	for_each_inst(i, inst_mask)
		mmhub_v1_8_inst_reset_ras_error_count(adev, i);
}

static const struct amdgpu_ras_block_hw_ops mmhub_v1_8_ras_hw_ops = {
	.query_ras_error_count = mmhub_v1_8_query_ras_error_count,
	.reset_ras_error_count = mmhub_v1_8_reset_ras_error_count,
};

static int mmhub_v1_8_aca_bank_parser(struct aca_handle *handle, struct aca_bank *bank,
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
		ret = aca_error_cache_log_bank_error(handle, &info, ACA_ERROR_TYPE_UE,
						     1ULL);
		break;
	case ACA_SMU_TYPE_CE:
		ret = aca_error_cache_log_bank_error(handle, &info, ACA_ERROR_TYPE_CE,
						     ACA_REG__MISC0__ERRCNT(misc0));
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

/* reference to smu driver if header file */
static int mmhub_v1_8_err_codes[] = {
	0, 1, 2, 3, 4, /* CODE_DAGB0 - 4 */
	5, 6, 7, 8, 9, /* CODE_EA0 - 4 */
	10, /* CODE_UTCL2_ROUTER */
	11, /* CODE_VML2 */
	12, /* CODE_VML2_WALKER */
	13, /* CODE_MMCANE */
};

static bool mmhub_v1_8_aca_bank_is_valid(struct aca_handle *handle, struct aca_bank *bank,
					 enum aca_smu_type type, void *data)
{
	u32 instlo;

	instlo = ACA_REG__IPID__INSTANCEIDLO(bank->regs[ACA_REG_IDX_IPID]);
	instlo &= GENMASK(31, 1);

	if (instlo != mmSMNAID_AID0_MCA_SMU)
		return false;

	if (aca_bank_check_error_codes(handle->adev, bank,
				       mmhub_v1_8_err_codes,
				       ARRAY_SIZE(mmhub_v1_8_err_codes)))
		return false;

	return true;
}

static const struct aca_bank_ops mmhub_v1_8_aca_bank_ops = {
	.aca_bank_parser = mmhub_v1_8_aca_bank_parser,
	.aca_bank_is_valid = mmhub_v1_8_aca_bank_is_valid,
};

static const struct aca_info mmhub_v1_8_aca_info = {
	.hwip = ACA_HWIP_TYPE_SMU,
	.mask = ACA_ERROR_UE_MASK,
	.bank_ops = &mmhub_v1_8_aca_bank_ops,
};

static int mmhub_v1_8_ras_late_init(struct amdgpu_device *adev, struct ras_common_if *ras_block)
{
	int r;

	r = amdgpu_ras_block_late_init(adev, ras_block);
	if (r)
		return r;

	r = amdgpu_ras_bind_aca(adev, AMDGPU_RAS_BLOCK__MMHUB,
				&mmhub_v1_8_aca_info, NULL);
	if (r)
		goto late_fini;

	return 0;

late_fini:
	amdgpu_ras_block_late_fini(adev, ras_block);

	return r;
}

struct amdgpu_mmhub_ras mmhub_v1_8_ras = {
	.ras_block = {
		.hw_ops = &mmhub_v1_8_ras_hw_ops,
		.ras_late_init = mmhub_v1_8_ras_late_init,
	},
};
