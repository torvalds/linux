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
#include "amdgpu_xcp.h"
#include "gfxhub_v12_1.h"

#include "gc/gc_12_1_0_offset.h"
#include "gc/gc_12_1_0_sh_mask.h"
#include "soc_v1_0_enum.h"

#include "soc15_common.h"

#define regGCVM_L2_CNTL3_DEFAULT		0x80120007
#define regGCVM_L2_CNTL4_DEFAULT		0x000000c1
#define regGCVM_L2_CNTL5_DEFAULT		0x00003fe0
#define regGRBM_GFX_INDEX_DEFAULT			0xe0000000


static u64 gfxhub_v12_1_get_fb_location(struct amdgpu_device *adev)
{
	u64 base;

	base = RREG32_SOC15(GC, GET_INST(GC, 0),
			    regGCMC_VM_FB_LOCATION_BASE_LO32);
	base &= GCMC_VM_FB_LOCATION_BASE_LO32__FB_BASE_LO32_MASK;
	base <<= 24;

	base |= ((u64)(GCMC_VM_FB_LOCATION_BASE_HI32__FB_BASE_HI1_MASK &
		       RREG32_SOC15(GC, GET_INST(GC, 0),
				    regGCMC_VM_FB_LOCATION_BASE_HI32)) << 56);
	return base;
}

static u64 gfxhub_v12_1_get_mc_fb_offset(struct amdgpu_device *adev)
{
	return (u64)(RREG32_SOC15(GC, GET_INST(GC, 0),
				  regGCMC_VM_FB_OFFSET) << 24);
}

static void gfxhub_v12_1_xcc_setup_vm_pt_regs(struct amdgpu_device *adev,
					      uint32_t vmid,
					      uint64_t page_table_base,
					      uint32_t xcc_mask)
{
	struct amdgpu_vmhub *hub;
	int i;

	for_each_inst(i, xcc_mask) {
		hub = &adev->vmhub[AMDGPU_GFXHUB(i)];
		WREG32_SOC15_OFFSET(GC, GET_INST(GC, i),
				    regGCVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32,
				    hub->ctx_addr_distance * vmid,
				    lower_32_bits(page_table_base));

		WREG32_SOC15_OFFSET(GC, GET_INST(GC, i),
				    regGCVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_HI32,
				    hub->ctx_addr_distance * vmid,
				    upper_32_bits(page_table_base));
	}
}

static void gfxhub_v12_1_setup_vm_pt_regs(struct amdgpu_device *adev,
					  uint32_t vmid,
					  uint64_t page_table_base)
{
	uint32_t xcc_mask;

	xcc_mask = GENMASK(NUM_XCC(adev->gfx.xcc_mask) - 1, 0);
	gfxhub_v12_1_xcc_setup_vm_pt_regs(adev, vmid, page_table_base,
					  xcc_mask);
}

static void gfxhub_v12_1_xcc_init_gart_aperture_regs(struct amdgpu_device *adev,
						     uint32_t xcc_mask)
{
	uint64_t pt_base;
	int i;

	if (adev->gmc.pdb0_bo)
		pt_base = amdgpu_gmc_pd_addr(adev->gmc.pdb0_bo);
	else
		pt_base = amdgpu_gmc_pd_addr(adev->gart.bo);

	gfxhub_v12_1_xcc_setup_vm_pt_regs(adev, 0, pt_base, xcc_mask);

	/* If use GART for FB translation, vmid0 page table covers both
	 * vram and system memory (gart)
	 */
	for_each_inst(i, xcc_mask) {
		if (adev->gmc.pdb0_bo) {
			WREG32_SOC15(GC, GET_INST(GC, i),
				     regGCVM_CONTEXT0_PAGE_TABLE_START_ADDR_LO32,
				     (u32)(adev->gmc.fb_start >> 12));
			WREG32_SOC15(GC, GET_INST(GC, i),
				     regGCVM_CONTEXT0_PAGE_TABLE_START_ADDR_HI32,
				     (u32)(adev->gmc.fb_start >> 44));

			WREG32_SOC15(GC, GET_INST(GC, i),
				     regGCVM_CONTEXT0_PAGE_TABLE_END_ADDR_LO32,
				     (u32)(adev->gmc.gart_end >> 12));
			WREG32_SOC15(GC, GET_INST(GC, i),
				     regGCVM_CONTEXT0_PAGE_TABLE_END_ADDR_HI32,
				     (u32)(adev->gmc.gart_end >> 44));
		} else {
			WREG32_SOC15(GC, GET_INST(GC, i),
				     regGCVM_CONTEXT0_PAGE_TABLE_START_ADDR_LO32,
				     (u32)(adev->gmc.gart_start >> 12));
			WREG32_SOC15(GC, GET_INST(GC, i),
				     regGCVM_CONTEXT0_PAGE_TABLE_START_ADDR_HI32,
				     (u32)(adev->gmc.gart_start >> 44));

			WREG32_SOC15(GC, GET_INST(GC, i),
				     regGCVM_CONTEXT0_PAGE_TABLE_END_ADDR_LO32,
				     (u32)(adev->gmc.gart_end >> 12));
			WREG32_SOC15(GC, GET_INST(GC, i),
				     regGCVM_CONTEXT0_PAGE_TABLE_END_ADDR_HI32,
				     (u32)(adev->gmc.gart_end >> 44));
		}
	}
}

static void gfxhub_v12_1_xcc_init_system_aperture_regs(struct amdgpu_device *adev,
						       uint32_t xcc_mask)
{
	uint64_t value;
	uint32_t tmp;
	int i;

	for_each_inst(i, xcc_mask) {
		/* Program the AGP BAR */
		WREG32_SOC15_RLC(GC, GET_INST(GC, i),
				 regGCMC_VM_AGP_BASE_LO32, 0);
		WREG32_SOC15_RLC(GC, GET_INST(GC, i),
				 regGCMC_VM_AGP_BASE_HI32, 0);
		WREG32_SOC15_RLC(GC, GET_INST(GC, i),
				 regGCMC_VM_AGP_BOT_LO32,
				 lower_32_bits(adev->gmc.agp_start >> 24));
		WREG32_SOC15_RLC(GC, GET_INST(GC, i),
				 regGCMC_VM_AGP_BOT_HI32,
				 upper_32_bits(adev->gmc.agp_start >> 24));
		WREG32_SOC15_RLC(GC, GET_INST(GC, i),
				 regGCMC_VM_AGP_TOP_LO32,
				 lower_32_bits(adev->gmc.agp_end >> 24));
		WREG32_SOC15_RLC(GC, GET_INST(GC, i),
				 regGCMC_VM_AGP_TOP_HI32,
				 upper_32_bits(adev->gmc.agp_end >> 24));

		if (!amdgpu_sriov_vf(adev)) {
			/* Program the system aperture low logical page number. */
			WREG32_SOC15(GC, GET_INST(GC, i),
				     regGCMC_VM_SYSTEM_APERTURE_LOW_ADDR_LO32,
				     lower_32_bits(min(adev->gmc.fb_start, adev->gmc.agp_start) >> 18));
			WREG32_SOC15(GC, GET_INST(GC, i),
				     regGCMC_VM_SYSTEM_APERTURE_LOW_ADDR_HI32,
				     upper_32_bits(min(adev->gmc.fb_start, adev->gmc.agp_start) >> 18));
			WREG32_SOC15(GC, GET_INST(GC, i),
				     regGCMC_VM_SYSTEM_APERTURE_HIGH_ADDR_LO32,
				     lower_32_bits(max(adev->gmc.fb_end, adev->gmc.agp_end) >> 18));
			WREG32_SOC15(GC, GET_INST(GC, i),
				     regGCMC_VM_SYSTEM_APERTURE_HIGH_ADDR_HI32,
				     upper_32_bits(max(adev->gmc.fb_end, adev->gmc.agp_end) >> 18));

			/* Set default page address. */
			value = amdgpu_gmc_vram_mc2pa(adev, adev->mem_scratch.gpu_addr);
			WREG32_SOC15(GC, GET_INST(GC, i),
				     regGCMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_LSB,
				     (u32)(value >> 12));
			WREG32_SOC15(GC, GET_INST(GC, i),
				     regGCMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_MSB,
				     (u32)(value >> 44));

			/* Program "protection fault". */
			WREG32_SOC15(GC, GET_INST(GC, i),
				     regGCVM_L2_PROTECTION_FAULT_DEFAULT_ADDR_LO32,
				     (u32)(adev->dummy_page_addr >> 12));
			WREG32_SOC15(GC, GET_INST(GC, i),
				     regGCVM_L2_PROTECTION_FAULT_DEFAULT_ADDR_HI32,
				     (u32)((u64)adev->dummy_page_addr >> 44));

			tmp = RREG32_SOC15(GC, GET_INST(GC, i),
					   regGCVM_L2_PROTECTION_FAULT_CNTL2);
			tmp = REG_SET_FIELD(tmp, GCVM_L2_PROTECTION_FAULT_CNTL2,
					    ACTIVE_PAGE_MIGRATION_PTE_READ_RETRY, 1);
			tmp = REG_SET_FIELD(tmp, GCVM_L2_PROTECTION_FAULT_CNTL2,
					    ENABLE_RETRY_FAULT_INTERRUPT, 0x1);
			WREG32_SOC15(GC, GET_INST(GC, i),
				     regGCVM_L2_PROTECTION_FAULT_CNTL2, tmp);
		}

		/* In the case squeezing vram into GART aperture, we don't use
		 * FB aperture and AGP aperture. Disable them.
		 */
		if (adev->gmc.pdb0_bo) {
			WREG32_SOC15(GC, GET_INST(GC, i),
				     regGCMC_VM_FB_LOCATION_TOP_LO32, 0);
			WREG32_SOC15(GC, GET_INST(GC, i),
				     regGCMC_VM_FB_LOCATION_TOP_HI32, 0);
			WREG32_SOC15(GC, GET_INST(GC, i),
				     regGCMC_VM_FB_LOCATION_BASE_LO32,
				     0xFFFFFFFF);
			WREG32_SOC15(GC, GET_INST(GC, i),
				     regGCMC_VM_FB_LOCATION_BASE_HI32, 1);
			WREG32_SOC15(GC, GET_INST(GC, i),
				     regGCMC_VM_AGP_TOP_LO32, 0);
			WREG32_SOC15(GC, GET_INST(GC, i),
				     regGCMC_VM_AGP_TOP_HI32, 0);
			WREG32_SOC15(GC, GET_INST(GC, i),
				     regGCMC_VM_AGP_BOT_LO32, 0xFFFFFFFF);
			WREG32_SOC15(GC, GET_INST(GC, i),
				     regGCMC_VM_AGP_BOT_HI32, 1);
			WREG32_SOC15(GC, GET_INST(GC, i),
				     regGCMC_VM_SYSTEM_APERTURE_LOW_ADDR_LO32,
				     0xFFFFFFFF);
			WREG32_SOC15(GC, GET_INST(GC, i),
				     regGCMC_VM_SYSTEM_APERTURE_LOW_ADDR_HI32,
				     0x7F);
			WREG32_SOC15(GC, GET_INST(GC, i),
				     regGCMC_VM_SYSTEM_APERTURE_HIGH_ADDR_LO32, 0);
			WREG32_SOC15(GC, GET_INST(GC, i),
				     regGCMC_VM_SYSTEM_APERTURE_HIGH_ADDR_HI32, 0);
		}
	}
}

static void gfxhub_v12_1_xcc_init_tlb_regs(struct amdgpu_device *adev,
					   uint32_t xcc_mask)
{
	uint32_t tmp;
	int i;

	for_each_inst(i, xcc_mask) {
		/* Setup TLB control */
		tmp = RREG32_SOC15(GC, GET_INST(GC, i),
				   regGCMC_VM_MX_L1_TLB_CNTL);

		tmp = REG_SET_FIELD(tmp,
				    GCMC_VM_MX_L1_TLB_CNTL,
				    ENABLE_L1_TLB, 1);
		tmp = REG_SET_FIELD(tmp,
				    GCMC_VM_MX_L1_TLB_CNTL,
				    SYSTEM_ACCESS_MODE, 3);
		tmp = REG_SET_FIELD(tmp,
				    GCMC_VM_MX_L1_TLB_CNTL,
				    ENABLE_ADVANCED_DRIVER_MODEL, 1);
		tmp = REG_SET_FIELD(tmp,
				    GCMC_VM_MX_L1_TLB_CNTL,
				    SYSTEM_APERTURE_UNMAPPED_ACCESS, 0);
		tmp = REG_SET_FIELD(tmp,
				    GCMC_VM_MX_L1_TLB_CNTL,
				    ECO_BITS, 0);
		tmp = REG_SET_FIELD(tmp,
				    GCMC_VM_MX_L1_TLB_CNTL,
				    MTYPE, MTYPE_UC);

		WREG32_SOC15_RLC(GC, GET_INST(GC, i),
				 regGCMC_VM_MX_L1_TLB_CNTL, tmp);
	}
}

static void gfxhub_v12_1_xcc_init_cache_regs(struct amdgpu_device *adev,
					     uint32_t xcc_mask)
{
	uint32_t tmp;
	int i;

	for_each_inst(i, xcc_mask) {
		/* Setup L2 cache */
		tmp = RREG32_SOC15(GC, GET_INST(GC, i), regGCVM_L2_CNTL);
		tmp = REG_SET_FIELD(tmp, GCVM_L2_CNTL,
				    ENABLE_L2_CACHE, 1);
		/*TODO: set ENABLE_L2_FRAGMENT_PROCESSING to 1? */
		tmp = REG_SET_FIELD(tmp, GCVM_L2_CNTL,
				    ENABLE_L2_FRAGMENT_PROCESSING, 0);
		tmp = REG_SET_FIELD(tmp, GCVM_L2_CNTL,
				    ENABLE_DEFAULT_PAGE_OUT_TO_SYSTEM_MEMORY, 1);
		tmp = REG_SET_FIELD(tmp, GCVM_L2_CNTL,
				    L2_PDE0_CACHE_TAG_GENERATION_MODE, 0);
		tmp = REG_SET_FIELD(tmp, GCVM_L2_CNTL,
				    PDE_FAULT_CLASSIFICATION, 0);
		tmp = REG_SET_FIELD(tmp, GCVM_L2_CNTL,
				    CONTEXT1_IDENTITY_ACCESS_MODE, 1);
		tmp = REG_SET_FIELD(tmp, GCVM_L2_CNTL,
				    IDENTITY_MODE_FRAGMENT_SIZE, 0);
		WREG32_SOC15_RLC(GC, GET_INST(GC, i),
				 regGCVM_L2_CNTL, tmp);

		tmp = RREG32_SOC15(GC, GET_INST(GC, i), regGCVM_L2_CNTL2);
		tmp = REG_SET_FIELD(tmp, GCVM_L2_CNTL2,
				    INVALIDATE_ALL_L1_TLBS, 1);
		tmp = REG_SET_FIELD(tmp, GCVM_L2_CNTL2,
				    INVALIDATE_L2_CACHE, 1);
		WREG32_SOC15_RLC(GC, GET_INST(GC, i),
				 regGCVM_L2_CNTL2, tmp);

		tmp = regGCVM_L2_CNTL3_DEFAULT;
		if (adev->gmc.translate_further) {
			tmp = REG_SET_FIELD(tmp, GCVM_L2_CNTL3, BANK_SELECT, 12);
			tmp = REG_SET_FIELD(tmp, GCVM_L2_CNTL3,
					    L2_CACHE_BIGK_FRAGMENT_SIZE, 9);
		} else {
			tmp = REG_SET_FIELD(tmp, GCVM_L2_CNTL3, BANK_SELECT, 9);
			tmp = REG_SET_FIELD(tmp, GCVM_L2_CNTL3,
					    L2_CACHE_BIGK_FRAGMENT_SIZE, 6);
		}
		WREG32_SOC15_RLC(GC, GET_INST(GC, i), regGCVM_L2_CNTL3, tmp);

		tmp = regGCVM_L2_CNTL4_DEFAULT;
		tmp = REG_SET_FIELD(tmp, GCVM_L2_CNTL4,
				    VMC_TAP_PDE_REQUEST_PHYSICAL, 1);
		tmp = REG_SET_FIELD(tmp, GCVM_L2_CNTL4,
				    VMC_TAP_PTE_REQUEST_PHYSICAL, 1);
		WREG32_SOC15_RLC(GC, GET_INST(GC, i), regGCVM_L2_CNTL4, tmp);

		tmp = regGCVM_L2_CNTL5_DEFAULT;
		tmp = REG_SET_FIELD(tmp, GCVM_L2_CNTL5,
				    L2_CACHE_SMALLK_FRAGMENT_SIZE, 0);
		WREG32_SOC15_RLC(GC, GET_INST(GC, i), regGCVM_L2_CNTL5, tmp);
	}
}

static void gfxhub_v12_1_xcc_enable_system_domain(struct amdgpu_device *adev,
						  uint32_t xcc_mask)
{
	uint32_t tmp;
	int i;

	for_each_inst(i, xcc_mask) {
		tmp = RREG32_SOC15(GC, GET_INST(GC, i),
				   regGCVM_CONTEXT0_CNTL);
		tmp = REG_SET_FIELD(tmp, GCVM_CONTEXT0_CNTL,
				    ENABLE_CONTEXT, 1);
		tmp = REG_SET_FIELD(tmp, GCVM_CONTEXT0_CNTL,
				    PAGE_TABLE_DEPTH,
				    adev->gmc.vmid0_page_table_depth);
		tmp = REG_SET_FIELD(tmp, GCVM_CONTEXT0_CNTL,
				    PAGE_TABLE_BLOCK_SIZE,
				    adev->gmc.vmid0_page_table_block_size);
		tmp = REG_SET_FIELD(tmp, GCVM_CONTEXT0_CNTL,
				    RETRY_PERMISSION_OR_INVALID_PAGE_FAULT, 0);
		WREG32_SOC15(GC, GET_INST(GC, i),
			     regGCVM_CONTEXT0_CNTL, tmp);
	}
}

static void gfxhub_v12_1_xcc_disable_identity_aperture(struct amdgpu_device *adev,
						       uint32_t xcc_mask)
{
	int i;

	for_each_inst(i, xcc_mask) {
		WREG32_SOC15(GC, GET_INST(GC, i),
			     regGCVM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_LO32,
			     0XFFFFFFFF);
		WREG32_SOC15(GC, GET_INST(GC, i),
			     regGCVM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_HI32,
			     0x00001FFF);

		WREG32_SOC15(GC, GET_INST(GC, i),
			     regGCVM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_LO32,
			     0);
		WREG32_SOC15(GC, GET_INST(GC, i),
			     regGCVM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_HI32,
			     0);

		WREG32_SOC15(GC, GET_INST(GC, i),
			     regGCVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_LO32,
			     0);
		WREG32_SOC15(GC, GET_INST(GC, i),
			     regGCVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_HI32,
			     0);
	}
}

static void gfxhub_v12_1_xcc_setup_vmid_config(struct amdgpu_device *adev,
					       uint32_t xcc_mask)
{
	struct amdgpu_vmhub *hub;
	unsigned int num_level, block_size;
	uint32_t tmp;
	int i, j;

	num_level = adev->vm_manager.num_level;
	block_size = adev->vm_manager.block_size;
	block_size -= 9;

	for_each_inst(j, xcc_mask) {
		hub = &adev->vmhub[AMDGPU_GFXHUB(j)];
		for (i = 0; i <= 14; i++) {
			tmp = RREG32_SOC15_OFFSET(GC, GET_INST(GC, j),
					          regGCVM_CONTEXT1_CNTL,
						  i * hub->ctx_distance);
			tmp = REG_SET_FIELD(tmp, GCVM_CONTEXT1_CNTL,
					    ENABLE_CONTEXT, 1);
			tmp = REG_SET_FIELD(tmp, GCVM_CONTEXT1_CNTL,
					    PAGE_TABLE_DEPTH, num_level);
			tmp = REG_SET_FIELD(tmp, GCVM_CONTEXT1_CNTL,
					    RANGE_PROTECTION_FAULT_ENABLE_DEFAULT, 1);
			tmp = REG_SET_FIELD(tmp, GCVM_CONTEXT1_CNTL,
					    DUMMY_PAGE_PROTECTION_FAULT_ENABLE_DEFAULT, 1);
			tmp = REG_SET_FIELD(tmp, GCVM_CONTEXT1_CNTL,
					    PDE0_PROTECTION_FAULT_ENABLE_DEFAULT, 1);
			tmp = REG_SET_FIELD(tmp, GCVM_CONTEXT1_CNTL,
					    VALID_PROTECTION_FAULT_ENABLE_DEFAULT, 1);
			tmp = REG_SET_FIELD(tmp, GCVM_CONTEXT1_CNTL,
					    READ_PROTECTION_FAULT_ENABLE_DEFAULT, 1);
			tmp = REG_SET_FIELD(tmp, GCVM_CONTEXT1_CNTL,
					    WRITE_PROTECTION_FAULT_ENABLE_DEFAULT, 1);
			tmp = REG_SET_FIELD(tmp, GCVM_CONTEXT1_CNTL,
					    EXECUTE_PROTECTION_FAULT_ENABLE_DEFAULT, 1);
			tmp = REG_SET_FIELD(tmp, GCVM_CONTEXT1_CNTL,
					    PAGE_TABLE_BLOCK_SIZE, block_size);
			/* Send no-retry XNACK on fault to suppress VM fault storm */
			tmp = REG_SET_FIELD(tmp, GCVM_CONTEXT1_CNTL,
					    RETRY_PERMISSION_OR_INVALID_PAGE_FAULT,
					    1);
			WREG32_SOC15_OFFSET(GC, GET_INST(GC, j), regGCVM_CONTEXT1_CNTL,
					    i * hub->ctx_distance, tmp);
			WREG32_SOC15_OFFSET(GC, GET_INST(GC, j),
					    regGCVM_CONTEXT1_PAGE_TABLE_START_ADDR_LO32,
					    i * hub->ctx_addr_distance, 0);
			WREG32_SOC15_OFFSET(GC, GET_INST(GC, j),
					    regGCVM_CONTEXT1_PAGE_TABLE_START_ADDR_HI32,
					    i * hub->ctx_addr_distance, 0);
			WREG32_SOC15_OFFSET(GC, GET_INST(GC, j),
					    regGCVM_CONTEXT1_PAGE_TABLE_END_ADDR_LO32,
					    i * hub->ctx_addr_distance,
					    lower_32_bits(adev->vm_manager.max_pfn - 1));
			WREG32_SOC15_OFFSET(GC, GET_INST(GC, j),
					    regGCVM_CONTEXT1_PAGE_TABLE_END_ADDR_HI32,
					    i * hub->ctx_addr_distance,
					    upper_32_bits(adev->vm_manager.max_pfn - 1));
		}

		hub->vm_cntx_cntl = tmp;
	}
}

static void gfxhub_v12_1_xcc_program_invalidation(struct amdgpu_device *adev,
						  uint32_t xcc_mask)
{
	struct amdgpu_vmhub *hub;
	unsigned int i, j;

	for_each_inst(j, xcc_mask) {
		hub = &adev->vmhub[AMDGPU_GFXHUB(j)];

		for (i = 0 ; i < 18; ++i) {
			WREG32_SOC15_OFFSET(GC, GET_INST(GC, j),
					    regGCVM_INVALIDATE_ENG0_ADDR_RANGE_LO32,
					    i * hub->eng_addr_distance, 0xFFFFFFFF);
			WREG32_SOC15_OFFSET(GC, GET_INST(GC, j),
					    regGCVM_INVALIDATE_ENG0_ADDR_RANGE_HI32,
					    i * hub->eng_addr_distance, 0x3FFF);
		}
	}
}

static int gfxhub_v12_1_xcc_gart_enable(struct amdgpu_device *adev,
					uint32_t xcc_mask)
{
	uint32_t i;

	if (amdgpu_sriov_vf(adev)) {
		/* GCMC_VM_FB_LOCATION_BASE/TOP are VF copy registers
		 * VBIO post does not program them at boot up phase
		 * Need driver to program them from guest side */
		for_each_inst(i, xcc_mask) {
			WREG32_SOC15(GC, GET_INST(GC, i),
				     regGCMC_VM_FB_LOCATION_BASE_LO32,
				     lower_32_bits(adev->gmc.vram_start >> 24));
			WREG32_SOC15(GC, GET_INST(GC, i),
				     regGCMC_VM_FB_LOCATION_BASE_HI32,
				     upper_32_bits(adev->gmc.vram_start >> 24));
			WREG32_SOC15(GC, GET_INST(GC, i),
				     regGCMC_VM_FB_LOCATION_TOP_LO32,
				     lower_32_bits(adev->gmc.vram_end >> 24));
			WREG32_SOC15(GC, GET_INST(GC, i),
				     regGCMC_VM_FB_LOCATION_TOP_HI32,
				     upper_32_bits(adev->gmc.vram_end >> 24));
		}
	}
	/* GART Enable. */
	gfxhub_v12_1_xcc_init_gart_aperture_regs(adev, xcc_mask);
	gfxhub_v12_1_xcc_init_system_aperture_regs(adev, xcc_mask);
	gfxhub_v12_1_xcc_init_tlb_regs(adev, xcc_mask);
	if (!amdgpu_sriov_vf(adev))
		gfxhub_v12_1_xcc_init_cache_regs(adev, xcc_mask);

	gfxhub_v12_1_xcc_enable_system_domain(adev, xcc_mask);
	if (!amdgpu_sriov_vf(adev))
		gfxhub_v12_1_xcc_disable_identity_aperture(adev, xcc_mask);
	gfxhub_v12_1_xcc_setup_vmid_config(adev, xcc_mask);
	gfxhub_v12_1_xcc_program_invalidation(adev, xcc_mask);

	return 0;
}

static int gfxhub_v12_1_gart_enable(struct amdgpu_device *adev)
{
	uint32_t xcc_mask;

	xcc_mask = GENMASK(NUM_XCC(adev->gfx.xcc_mask) - 1, 0);
	return gfxhub_v12_1_xcc_gart_enable(adev, xcc_mask);
}

static void gfxhub_v12_1_xcc_gart_disable(struct amdgpu_device *adev,
					  uint32_t xcc_mask)
{
	struct amdgpu_vmhub *hub;
	u32 tmp;
	u32 i, j;

	for_each_inst(j, xcc_mask) {
		hub = &adev->vmhub[AMDGPU_GFXHUB(j)];
		/* Disable all tables */
		for (i = 0; i < 16; i++)
			WREG32_SOC15_OFFSET(GC, GET_INST(GC, j),
					    regGCVM_CONTEXT0_CNTL,
					    i * hub->ctx_distance, 0);

		/* Setup TLB control */
		tmp = RREG32_SOC15(GC, GET_INST(GC, j),
				   regGCMC_VM_MX_L1_TLB_CNTL);
		tmp = REG_SET_FIELD(tmp, GCMC_VM_MX_L1_TLB_CNTL,
				    ENABLE_L1_TLB, 0);
		tmp = REG_SET_FIELD(tmp, GCMC_VM_MX_L1_TLB_CNTL,
				    ENABLE_ADVANCED_DRIVER_MODEL, 0);
		WREG32_SOC15_RLC(GC, GET_INST(GC, j),
				 regGCMC_VM_MX_L1_TLB_CNTL, tmp);

		/* Setup L2 cache */
		if (!amdgpu_sriov_vf(adev)) {
			tmp = RREG32_SOC15(GC, GET_INST(GC, j), regGCVM_L2_CNTL);
			tmp = REG_SET_FIELD(tmp, GCVM_L2_CNTL, ENABLE_L2_CACHE, 0);
			WREG32_SOC15(GC, GET_INST(GC, j), regGCVM_L2_CNTL, tmp);
			WREG32_SOC15(GC, GET_INST(GC, j), regGCVM_L2_CNTL3, 0);
		}
	}
}

static void gfxhub_v12_1_gart_disable(struct amdgpu_device *adev)
{
	uint32_t xcc_mask;

	xcc_mask = GENMASK(NUM_XCC(adev->gfx.xcc_mask) - 1, 0);
	gfxhub_v12_1_xcc_gart_disable(adev, xcc_mask);
}

static void gfxhub_v12_1_xcc_set_fault_enable_default(struct amdgpu_device *adev,
						      bool value, uint32_t xcc_mask)
{
	u32 tmp;
	int i;

	for_each_inst(i, xcc_mask) {
		tmp = RREG32_SOC15(GC, GET_INST(GC, i),
				   regGCVM_L2_PROTECTION_FAULT_CNTL_LO32);
		tmp = REG_SET_FIELD(tmp,
				    GCVM_L2_PROTECTION_FAULT_CNTL_LO32,
				    RANGE_PROTECTION_FAULT_ENABLE_DEFAULT, value);
		tmp = REG_SET_FIELD(tmp,
				    GCVM_L2_PROTECTION_FAULT_CNTL_LO32,
				    PDE0_PROTECTION_FAULT_ENABLE_DEFAULT, value);
		tmp = REG_SET_FIELD(tmp,
				    GCVM_L2_PROTECTION_FAULT_CNTL_LO32,
				    PDE1_PROTECTION_FAULT_ENABLE_DEFAULT, value);
		tmp = REG_SET_FIELD(tmp,
				    GCVM_L2_PROTECTION_FAULT_CNTL_LO32,
				    PDE2_PROTECTION_FAULT_ENABLE_DEFAULT, value);
		tmp = REG_SET_FIELD(tmp,
				    GCVM_L2_PROTECTION_FAULT_CNTL_LO32,
				    PDE3_PROTECTION_FAULT_ENABLE_DEFAULT, value);
		tmp = REG_SET_FIELD(tmp,
				    GCVM_L2_PROTECTION_FAULT_CNTL_LO32,
				    TRANSLATE_FURTHER_PROTECTION_FAULT_ENABLE_DEFAULT,
				    value);
		tmp = REG_SET_FIELD(tmp,
				    GCVM_L2_PROTECTION_FAULT_CNTL_LO32,
				    NACK_PROTECTION_FAULT_ENABLE_DEFAULT, value);
		tmp = REG_SET_FIELD(tmp,
				    GCVM_L2_PROTECTION_FAULT_CNTL_LO32,
				    DUMMY_PAGE_PROTECTION_FAULT_ENABLE_DEFAULT, value);
		tmp = REG_SET_FIELD(tmp,
				    GCVM_L2_PROTECTION_FAULT_CNTL_LO32,
				    VALID_PROTECTION_FAULT_ENABLE_DEFAULT, value);
		tmp = REG_SET_FIELD(tmp,
				    GCVM_L2_PROTECTION_FAULT_CNTL_LO32,
				    READ_PROTECTION_FAULT_ENABLE_DEFAULT, value);
		tmp = REG_SET_FIELD(tmp,
				    GCVM_L2_PROTECTION_FAULT_CNTL_LO32,
				    WRITE_PROTECTION_FAULT_ENABLE_DEFAULT, value);
		tmp = REG_SET_FIELD(tmp,
				    GCVM_L2_PROTECTION_FAULT_CNTL_LO32,
				    EXECUTE_PROTECTION_FAULT_ENABLE_DEFAULT, value);
		tmp = REG_SET_FIELD(tmp,
				    GCVM_L2_PROTECTION_FAULT_CNTL_LO32,
				    CLIENT_ID_NO_RETRY_FAULT_INTERRUPT, value ? 0xFFFF:0);
		tmp = REG_SET_FIELD(tmp,
				    GCVM_L2_PROTECTION_FAULT_CNTL_LO32,
				    OTHER_CLIENT_ID_NO_RETRY_FAULT_INTERRUPT, value);
		if (!value)
			tmp = REG_SET_FIELD(tmp,
					    GCVM_L2_PROTECTION_FAULT_CNTL_LO32,
					    CRASH_ON_NO_RETRY_FAULT, 1);
		WREG32_SOC15(GC, GET_INST(GC, i),
			     regGCVM_L2_PROTECTION_FAULT_CNTL_LO32, tmp);

		tmp = RREG32_SOC15(GC, GET_INST(GC, i),
				   regGCVM_L2_PROTECTION_FAULT_CNTL_HI32);
		if (!value)
			tmp = REG_SET_FIELD(tmp,
					    GCVM_L2_PROTECTION_FAULT_CNTL_HI32,
					    CRASH_ON_RETRY_FAULT, 1);
		WREG32_SOC15(GC, GET_INST(GC, i),
			     regGCVM_L2_PROTECTION_FAULT_CNTL_HI32, tmp);
	}
}

/**
 * gfxhub_v12_1_set_fault_enable_default - update GART/VM fault handling
 *
 * @adev: amdgpu_device pointer
 * @value: true redirects VM faults to the default page
 */
static void gfxhub_v12_1_set_fault_enable_default(struct amdgpu_device *adev,
						  bool value)
{
	uint32_t xcc_mask;

	xcc_mask = GENMASK(NUM_XCC(adev->gfx.xcc_mask) - 1, 0);
	gfxhub_v12_1_xcc_set_fault_enable_default(adev, value, xcc_mask);
}

static uint32_t gfxhub_v12_1_get_invalidate_req(unsigned int vmid,
						uint32_t flush_type)
{
	u32 req = 0;

	/* invalidate using legacy mode on vmid*/
	req = REG_SET_FIELD(req, GCVM_INVALIDATE_ENG0_REQ,
			    PER_VMID_INVALIDATE_REQ, 1 << vmid);
	req = REG_SET_FIELD(req, GCVM_INVALIDATE_ENG0_REQ,
			    FLUSH_TYPE, flush_type);
	req = REG_SET_FIELD(req, GCVM_INVALIDATE_ENG0_REQ,
			    INVALIDATE_L2_PTES, 1);
	req = REG_SET_FIELD(req, GCVM_INVALIDATE_ENG0_REQ,
			    INVALIDATE_L2_PDE0, 1);
	req = REG_SET_FIELD(req, GCVM_INVALIDATE_ENG0_REQ,
			    INVALIDATE_L2_PDE1, 1);
	req = REG_SET_FIELD(req, GCVM_INVALIDATE_ENG0_REQ,
			    INVALIDATE_L2_PDE2, 1);
	req = REG_SET_FIELD(req, GCVM_INVALIDATE_ENG0_REQ,
			    INVALIDATE_L2_PDE3, 1);
	req = REG_SET_FIELD(req, GCVM_INVALIDATE_ENG0_REQ,
			    INVALIDATE_L1_PTES, 1);
	req = REG_SET_FIELD(req, GCVM_INVALIDATE_ENG0_REQ,
			    CLEAR_PROTECTION_FAULT_STATUS_ADDR, 0);

	return req;
}

static const char *gfxhub_v12_1_client_ids[] = {
	"CB",
	"DB",
	"GE1",
	"GE2",
	"CPF",
	"CPC",
	"CPG",
	"RLC",
	"TCP",
	"SQC (inst)",
	"SQC (data)",
	"SQG/PC/SC",
	"Reserved",
	"SDMA0",
	"SDMA1",
	"GCR",
	"Reserved",
	"Reserved",
	"WGS",
	"DSM",
	"PA"
};

/*TODO: l2 protection fault status is increased to 64bits.
 * some critical fields like FED are moved to STATUS_HI32 */
static void gfxhub_v12_1_print_l2_protection_fault_status(struct amdgpu_device *adev,
							  uint32_t status)
{
	u32 cid = REG_GET_FIELD(status,
				GCVM_L2_PROTECTION_FAULT_STATUS_LO32,
				CID);

	dev_err(adev->dev,
		"GCVM_L2_PROTECTION_FAULT_STATUS_LO32:0x%08X\n",
		status);
	dev_err(adev->dev, "\t Faulty UTCL2 client ID: %s (0x%x)\n",
		cid >= ARRAY_SIZE(gfxhub_v12_1_client_ids) ?
		"unknown" : gfxhub_v12_1_client_ids[cid], cid);
	dev_err(adev->dev, "\t MORE_FAULTS: 0x%lx\n",
		REG_GET_FIELD(status,
		GCVM_L2_PROTECTION_FAULT_STATUS_LO32, MORE_FAULTS));
	dev_err(adev->dev, "\t WALKER_ERROR: 0x%lx\n",
		REG_GET_FIELD(status,
		GCVM_L2_PROTECTION_FAULT_STATUS_LO32, WALKER_ERROR));
	dev_err(adev->dev, "\t PERMISSION_FAULTS: 0x%lx\n",
		REG_GET_FIELD(status,
		GCVM_L2_PROTECTION_FAULT_STATUS_LO32, PERMISSION_FAULTS));
	dev_err(adev->dev, "\t MAPPING_ERROR: 0x%lx\n",
		REG_GET_FIELD(status,
		GCVM_L2_PROTECTION_FAULT_STATUS_LO32, MAPPING_ERROR));
	dev_err(adev->dev, "\t RW: 0x%lx\n",
		REG_GET_FIELD(status,
		GCVM_L2_PROTECTION_FAULT_STATUS_LO32, RW));
}

static const struct amdgpu_vmhub_funcs gfxhub_v12_1_vmhub_funcs = {
	.print_l2_protection_fault_status = gfxhub_v12_1_print_l2_protection_fault_status,
	.get_invalidate_req = gfxhub_v12_1_get_invalidate_req,
};

static void gfxhub_v12_1_xcc_init(struct amdgpu_device *adev, uint32_t xcc_mask)
{
	struct amdgpu_vmhub *hub;
	int i;

	for_each_inst(i, xcc_mask) {
		hub = &adev->vmhub[AMDGPU_GFXHUB(i)];

		hub->ctx0_ptb_addr_lo32 =
			SOC15_REG_OFFSET(GC, GET_INST(GC, i),
				regGCVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32);
		hub->ctx0_ptb_addr_hi32 =
			SOC15_REG_OFFSET(GC, GET_INST(GC, i),
				regGCVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_HI32);
		hub->vm_inv_eng0_sem =
			SOC15_REG_OFFSET(GC, GET_INST(GC, i),
				regGCVM_INVALIDATE_ENG0_SEM);
		hub->vm_inv_eng0_req =
			SOC15_REG_OFFSET(GC, GET_INST(GC, i),
				regGCVM_INVALIDATE_ENG0_REQ);
		hub->vm_inv_eng0_ack =
			SOC15_REG_OFFSET(GC, GET_INST(GC, i),
				regGCVM_INVALIDATE_ENG0_ACK);
		hub->vm_context0_cntl =
			SOC15_REG_OFFSET(GC, GET_INST(GC, i),
				regGCVM_CONTEXT0_CNTL);
		/* TODO: add a new member to accomandate additional fault status/cntl reg */
		hub->vm_l2_pro_fault_status =
			SOC15_REG_OFFSET(GC, GET_INST(GC, i),
				regGCVM_L2_PROTECTION_FAULT_STATUS_LO32);
		hub->vm_l2_pro_fault_cntl =
			SOC15_REG_OFFSET(GC, GET_INST(GC, i),
				regGCVM_L2_PROTECTION_FAULT_CNTL_LO32);
		hub->ctx_distance =
				regGCVM_CONTEXT1_CNTL -
				regGCVM_CONTEXT0_CNTL;
		hub->ctx_addr_distance =
				regGCVM_CONTEXT1_PAGE_TABLE_BASE_ADDR_LO32 -
				regGCVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32;
		hub->eng_distance =
				regGCVM_INVALIDATE_ENG1_REQ -
				regGCVM_INVALIDATE_ENG0_REQ;
		hub->eng_addr_distance =
				regGCVM_INVALIDATE_ENG1_ADDR_RANGE_LO32 -
				regGCVM_INVALIDATE_ENG0_ADDR_RANGE_LO32;

		hub->vm_cntx_cntl_vm_fault =
			GCVM_CONTEXT1_CNTL__RANGE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK |
			GCVM_CONTEXT1_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK |
			GCVM_CONTEXT1_CNTL__PDE0_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK |
			GCVM_CONTEXT1_CNTL__VALID_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK |
			GCVM_CONTEXT1_CNTL__READ_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK |
			GCVM_CONTEXT1_CNTL__WRITE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK |
			GCVM_CONTEXT1_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK;

		hub->vmhub_funcs = &gfxhub_v12_1_vmhub_funcs;
	}
}

static void gfxhub_v12_1_init(struct amdgpu_device *adev)
{
	uint32_t xcc_mask;

	xcc_mask = GENMASK(NUM_XCC(adev->gfx.xcc_mask) - 1, 0);
	gfxhub_v12_1_xcc_init(adev, xcc_mask);
}

static int gfxhub_v12_1_get_xgmi_info(struct amdgpu_device *adev)
{
	u32 max_num_physical_nodes;
	u32 max_physical_node_id;
	u32 xgmi_lfb_cntl;
	u32 max_region;
	u64 seg_size;

	xgmi_lfb_cntl = RREG32_SOC15(GC, GET_INST(GC, 0),
				     regGCMC_VM_XGMI_LFB_CNTL);
	seg_size = REG_GET_FIELD(RREG32_SOC15(GC, GET_INST(GC, 0),
				 regGCMC_VM_XGMI_LFB_SIZE),
				 GCMC_VM_XGMI_LFB_SIZE, PF_LFB_SIZE) << 24;
	max_region = REG_GET_FIELD(xgmi_lfb_cntl,
				   GCMC_VM_XGMI_LFB_CNTL,
				   PF_MAX_REGION);

	max_num_physical_nodes   = 8;
	max_physical_node_id     = 7;

	/* PF_MAX_REGION=0 means xgmi is disabled */
	if (max_region || adev->gmc.xgmi.connected_to_cpu) {
		adev->gmc.xgmi.num_physical_nodes = max_region + 1;

		if (adev->gmc.xgmi.num_physical_nodes > max_num_physical_nodes)
			return -EINVAL;

		adev->gmc.xgmi.physical_node_id =
			REG_GET_FIELD(xgmi_lfb_cntl,
				      GCMC_VM_XGMI_LFB_CNTL,
				      PF_LFB_REGION);

		if (adev->gmc.xgmi.physical_node_id > max_physical_node_id)
			return -EINVAL;

		adev->gmc.xgmi.node_segment_size = seg_size;
	}

	return 0;
}

const struct amdgpu_gfxhub_funcs gfxhub_v12_1_funcs = {
	.get_fb_location = gfxhub_v12_1_get_fb_location,
	.get_mc_fb_offset = gfxhub_v12_1_get_mc_fb_offset,
	.setup_vm_pt_regs = gfxhub_v12_1_setup_vm_pt_regs,
	.gart_enable = gfxhub_v12_1_gart_enable,
	.gart_disable = gfxhub_v12_1_gart_disable,
	.set_fault_enable_default = gfxhub_v12_1_set_fault_enable_default,
	.init = gfxhub_v12_1_init,
	.get_xgmi_info = gfxhub_v12_1_get_xgmi_info,
};

static int gfxhub_v12_1_xcp_resume(void *handle, uint32_t inst_mask)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	bool value;

	if (amdgpu_vm_fault_stop == AMDGPU_VM_FAULT_STOP_ALWAYS)
		value = false;
	else
		value = true;

	gfxhub_v12_1_xcc_set_fault_enable_default(adev, value, inst_mask);

	if (!amdgpu_sriov_vf(adev))
		return gfxhub_v12_1_xcc_gart_enable(adev, inst_mask);

	return 0;
}

static int gfxhub_v12_1_xcp_suspend(void *handle, uint32_t inst_mask)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (!amdgpu_sriov_vf(adev))
		gfxhub_v12_1_xcc_gart_disable(adev, inst_mask);

	return 0;
}

struct amdgpu_xcp_ip_funcs gfxhub_v12_1_xcp_funcs = {
	.suspend = &gfxhub_v12_1_xcp_suspend,
	.resume = &gfxhub_v12_1_xcp_resume
};
