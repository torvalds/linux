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
#include "amdgpu_xcp.h"
#include "gfxhub_v1_2.h"
#include "gfxhub_v1_1.h"

#include "gc/gc_9_4_3_offset.h"
#include "gc/gc_9_4_3_sh_mask.h"
#include "vega10_enum.h"

#include "soc15_common.h"

#define regVM_L2_CNTL3_DEFAULT	0x80100007
#define regVM_L2_CNTL4_DEFAULT	0x000000c1

static u64 gfxhub_v1_2_get_mc_fb_offset(struct amdgpu_device *adev)
{
	return (u64)RREG32_SOC15(GC, GET_INST(GC, 0), regMC_VM_FB_OFFSET) << 24;
}

static void gfxhub_v1_2_xcc_setup_vm_pt_regs(struct amdgpu_device *adev,
					     uint32_t vmid,
					     uint64_t page_table_base,
					     uint32_t xcc_mask)
{
	struct amdgpu_vmhub *hub;
	int i;

	for_each_inst(i, xcc_mask) {
		hub = &adev->vmhub[AMDGPU_GFXHUB(i)];
		WREG32_SOC15_OFFSET(GC, GET_INST(GC, i),
				    regVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32,
				    hub->ctx_addr_distance * vmid,
				    lower_32_bits(page_table_base));

		WREG32_SOC15_OFFSET(GC, GET_INST(GC, i),
				    regVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_HI32,
				    hub->ctx_addr_distance * vmid,
				    upper_32_bits(page_table_base));
	}
}

static void gfxhub_v1_2_setup_vm_pt_regs(struct amdgpu_device *adev,
					 uint32_t vmid,
					 uint64_t page_table_base)
{
	uint32_t xcc_mask;

	xcc_mask = GENMASK(NUM_XCC(adev->gfx.xcc_mask) - 1, 0);
	gfxhub_v1_2_xcc_setup_vm_pt_regs(adev, vmid, page_table_base, xcc_mask);
}

static void gfxhub_v1_2_xcc_init_gart_aperture_regs(struct amdgpu_device *adev,
						    uint32_t xcc_mask)
{
	uint64_t pt_base;
	int i;

	if (adev->gmc.pdb0_bo)
		pt_base = amdgpu_gmc_pd_addr(adev->gmc.pdb0_bo);
	else
		pt_base = amdgpu_gmc_pd_addr(adev->gart.bo);

	gfxhub_v1_2_xcc_setup_vm_pt_regs(adev, 0, pt_base, xcc_mask);

	/* If use GART for FB translation, vmid0 page table covers both
	 * vram and system memory (gart)
	 */
	for_each_inst(i, xcc_mask) {
		if (adev->gmc.pdb0_bo) {
			WREG32_SOC15(GC, GET_INST(GC, i),
				     regVM_CONTEXT0_PAGE_TABLE_START_ADDR_LO32,
				     (u32)(adev->gmc.fb_start >> 12));
			WREG32_SOC15(GC, GET_INST(GC, i),
				     regVM_CONTEXT0_PAGE_TABLE_START_ADDR_HI32,
				     (u32)(adev->gmc.fb_start >> 44));

			WREG32_SOC15(GC, GET_INST(GC, i),
				     regVM_CONTEXT0_PAGE_TABLE_END_ADDR_LO32,
				     (u32)(adev->gmc.gart_end >> 12));
			WREG32_SOC15(GC, GET_INST(GC, i),
				     regVM_CONTEXT0_PAGE_TABLE_END_ADDR_HI32,
				     (u32)(adev->gmc.gart_end >> 44));
		} else {
			WREG32_SOC15(GC, GET_INST(GC, i),
				     regVM_CONTEXT0_PAGE_TABLE_START_ADDR_LO32,
				     (u32)(adev->gmc.gart_start >> 12));
			WREG32_SOC15(GC, GET_INST(GC, i),
				     regVM_CONTEXT0_PAGE_TABLE_START_ADDR_HI32,
				     (u32)(adev->gmc.gart_start >> 44));

			WREG32_SOC15(GC, GET_INST(GC, i),
				     regVM_CONTEXT0_PAGE_TABLE_END_ADDR_LO32,
				     (u32)(adev->gmc.gart_end >> 12));
			WREG32_SOC15(GC, GET_INST(GC, i),
				     regVM_CONTEXT0_PAGE_TABLE_END_ADDR_HI32,
				     (u32)(adev->gmc.gart_end >> 44));
		}
	}
}

static void
gfxhub_v1_2_xcc_init_system_aperture_regs(struct amdgpu_device *adev,
					  uint32_t xcc_mask)
{
	uint64_t value;
	uint32_t tmp;
	int i;

	for_each_inst(i, xcc_mask) {
		/* Program the AGP BAR */
		WREG32_SOC15_RLC(GC, GET_INST(GC, i), regMC_VM_AGP_BASE, 0);
		WREG32_SOC15_RLC(GC, GET_INST(GC, i), regMC_VM_AGP_BOT, adev->gmc.agp_start >> 24);
		WREG32_SOC15_RLC(GC, GET_INST(GC, i), regMC_VM_AGP_TOP, adev->gmc.agp_end >> 24);

		if (!amdgpu_sriov_vf(adev) || adev->asic_type <= CHIP_VEGA10) {
			/* Program the system aperture low logical page number. */
			WREG32_SOC15_RLC(GC, GET_INST(GC, i), regMC_VM_SYSTEM_APERTURE_LOW_ADDR,
				min(adev->gmc.fb_start, adev->gmc.agp_start) >> 18);

			if (adev->apu_flags & AMD_APU_IS_RAVEN2)
				/*
				* Raven2 has a HW issue that it is unable to use the
				* vram which is out of MC_VM_SYSTEM_APERTURE_HIGH_ADDR.
				* So here is the workaround that increase system
				* aperture high address (add 1) to get rid of the VM
				* fault and hardware hang.
				*/
				WREG32_SOC15_RLC(GC, GET_INST(GC, i),
						 regMC_VM_SYSTEM_APERTURE_HIGH_ADDR,
						 max((adev->gmc.fb_end >> 18) + 0x1,
						     adev->gmc.agp_end >> 18));
			else
				WREG32_SOC15_RLC(GC, GET_INST(GC, i),
					regMC_VM_SYSTEM_APERTURE_HIGH_ADDR,
					max(adev->gmc.fb_end, adev->gmc.agp_end) >> 18);

			/* Set default page address. */
			value = amdgpu_gmc_vram_mc2pa(adev, adev->mem_scratch.gpu_addr);
			WREG32_SOC15(GC, GET_INST(GC, i), regMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_LSB,
				     (u32)(value >> 12));
			WREG32_SOC15(GC, GET_INST(GC, i), regMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_MSB,
				     (u32)(value >> 44));

			/* Program "protection fault". */
			WREG32_SOC15(GC, GET_INST(GC, i), regVM_L2_PROTECTION_FAULT_DEFAULT_ADDR_LO32,
				     (u32)(adev->dummy_page_addr >> 12));
			WREG32_SOC15(GC, GET_INST(GC, i), regVM_L2_PROTECTION_FAULT_DEFAULT_ADDR_HI32,
				     (u32)((u64)adev->dummy_page_addr >> 44));

			tmp = RREG32_SOC15(GC, GET_INST(GC, i), regVM_L2_PROTECTION_FAULT_CNTL2);
			tmp = REG_SET_FIELD(tmp, VM_L2_PROTECTION_FAULT_CNTL2,
					    ACTIVE_PAGE_MIGRATION_PTE_READ_RETRY, 1);
			WREG32_SOC15(GC, GET_INST(GC, i), regVM_L2_PROTECTION_FAULT_CNTL2, tmp);
		}

		/* In the case squeezing vram into GART aperture, we don't use
		 * FB aperture and AGP aperture. Disable them.
		 */
		if (adev->gmc.pdb0_bo) {
			WREG32_SOC15(GC, GET_INST(GC, i), regMC_VM_FB_LOCATION_TOP, 0);
			WREG32_SOC15(GC, GET_INST(GC, i), regMC_VM_FB_LOCATION_BASE, 0x00FFFFFF);
			WREG32_SOC15(GC, GET_INST(GC, i), regMC_VM_AGP_TOP, 0);
			WREG32_SOC15(GC, GET_INST(GC, i), regMC_VM_AGP_BOT, 0xFFFFFF);
			WREG32_SOC15(GC, GET_INST(GC, i), regMC_VM_SYSTEM_APERTURE_LOW_ADDR, 0x3FFFFFFF);
			WREG32_SOC15(GC, GET_INST(GC, i), regMC_VM_SYSTEM_APERTURE_HIGH_ADDR, 0);
		}
	}
}

static void gfxhub_v1_2_xcc_init_tlb_regs(struct amdgpu_device *adev,
					  uint32_t xcc_mask)
{
	uint32_t tmp;
	int i;

	for_each_inst(i, xcc_mask) {
		/* Setup TLB control */
		tmp = RREG32_SOC15(GC, GET_INST(GC, i), regMC_VM_MX_L1_TLB_CNTL);

		tmp = REG_SET_FIELD(tmp, MC_VM_MX_L1_TLB_CNTL,
				    ENABLE_L1_TLB, 1);
		tmp = REG_SET_FIELD(tmp, MC_VM_MX_L1_TLB_CNTL,
				    SYSTEM_ACCESS_MODE, 3);
		tmp = REG_SET_FIELD(tmp, MC_VM_MX_L1_TLB_CNTL,
				    ENABLE_ADVANCED_DRIVER_MODEL, 1);
		tmp = REG_SET_FIELD(tmp, MC_VM_MX_L1_TLB_CNTL,
				    SYSTEM_APERTURE_UNMAPPED_ACCESS, 0);
		tmp = REG_SET_FIELD(tmp, MC_VM_MX_L1_TLB_CNTL,
				    MTYPE, MTYPE_UC);/* XXX for emulation. */
		tmp = REG_SET_FIELD(tmp, MC_VM_MX_L1_TLB_CNTL, ATC_EN, 1);

		WREG32_SOC15_RLC(GC, GET_INST(GC, i), regMC_VM_MX_L1_TLB_CNTL, tmp);
	}
}

static void gfxhub_v1_2_xcc_init_cache_regs(struct amdgpu_device *adev,
					    uint32_t xcc_mask)
{
	uint32_t tmp;
	int i;

	for_each_inst(i, xcc_mask) {
		/* Setup L2 cache */
		tmp = RREG32_SOC15(GC, GET_INST(GC, i), regVM_L2_CNTL);
		tmp = REG_SET_FIELD(tmp, VM_L2_CNTL, ENABLE_L2_CACHE, 1);
		tmp = REG_SET_FIELD(tmp, VM_L2_CNTL, ENABLE_L2_FRAGMENT_PROCESSING, 1);
		/* XXX for emulation, Refer to closed source code.*/
		tmp = REG_SET_FIELD(tmp, VM_L2_CNTL, L2_PDE0_CACHE_TAG_GENERATION_MODE,
				    0);
		tmp = REG_SET_FIELD(tmp, VM_L2_CNTL, PDE_FAULT_CLASSIFICATION, 0);
		tmp = REG_SET_FIELD(tmp, VM_L2_CNTL, CONTEXT1_IDENTITY_ACCESS_MODE, 1);
		tmp = REG_SET_FIELD(tmp, VM_L2_CNTL, IDENTITY_MODE_FRAGMENT_SIZE, 0);
		WREG32_SOC15_RLC(GC, GET_INST(GC, i), regVM_L2_CNTL, tmp);

		tmp = RREG32_SOC15(GC, GET_INST(GC, i), regVM_L2_CNTL2);
		tmp = REG_SET_FIELD(tmp, VM_L2_CNTL2, INVALIDATE_ALL_L1_TLBS, 1);
		tmp = REG_SET_FIELD(tmp, VM_L2_CNTL2, INVALIDATE_L2_CACHE, 1);
		WREG32_SOC15_RLC(GC, GET_INST(GC, i), regVM_L2_CNTL2, tmp);

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
		WREG32_SOC15_RLC(GC, GET_INST(GC, i), regVM_L2_CNTL3, tmp);

		tmp = regVM_L2_CNTL4_DEFAULT;
		/* For AMD APP APUs setup WC memory */
		if (adev->gmc.xgmi.connected_to_cpu || adev->gmc.is_app_apu) {
			tmp = REG_SET_FIELD(tmp, VM_L2_CNTL4, VMC_TAP_PDE_REQUEST_PHYSICAL, 1);
			tmp = REG_SET_FIELD(tmp, VM_L2_CNTL4, VMC_TAP_PTE_REQUEST_PHYSICAL, 1);
		} else {
			tmp = REG_SET_FIELD(tmp, VM_L2_CNTL4, VMC_TAP_PDE_REQUEST_PHYSICAL, 0);
			tmp = REG_SET_FIELD(tmp, VM_L2_CNTL4, VMC_TAP_PTE_REQUEST_PHYSICAL, 0);
		}
		WREG32_SOC15_RLC(GC, GET_INST(GC, i), regVM_L2_CNTL4, tmp);
	}
}

static void gfxhub_v1_2_xcc_enable_system_domain(struct amdgpu_device *adev,
						 uint32_t xcc_mask)
{
	uint32_t tmp;
	int i;

	for_each_inst(i, xcc_mask) {
		tmp = RREG32_SOC15(GC, GET_INST(GC, i), regVM_CONTEXT0_CNTL);
		tmp = REG_SET_FIELD(tmp, VM_CONTEXT0_CNTL, ENABLE_CONTEXT, 1);
		tmp = REG_SET_FIELD(tmp, VM_CONTEXT0_CNTL, PAGE_TABLE_DEPTH,
				adev->gmc.vmid0_page_table_depth);
		tmp = REG_SET_FIELD(tmp, VM_CONTEXT0_CNTL, PAGE_TABLE_BLOCK_SIZE,
				adev->gmc.vmid0_page_table_block_size);
		tmp = REG_SET_FIELD(tmp, VM_CONTEXT0_CNTL,
				    RETRY_PERMISSION_OR_INVALID_PAGE_FAULT, 0);
		WREG32_SOC15(GC, GET_INST(GC, i), regVM_CONTEXT0_CNTL, tmp);
	}
}

static void
gfxhub_v1_2_xcc_disable_identity_aperture(struct amdgpu_device *adev,
					  uint32_t xcc_mask)
{
	int i;

	for_each_inst(i, xcc_mask) {
		WREG32_SOC15(GC, GET_INST(GC, i),
			     regVM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_LO32,
			     0XFFFFFFFF);
		WREG32_SOC15(GC, GET_INST(GC, i),
			     regVM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_HI32,
			     0x0000000F);

		WREG32_SOC15(GC, GET_INST(GC, i),
			     regVM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_LO32,
			     0);
		WREG32_SOC15(GC, GET_INST(GC, i),
			     regVM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_HI32,
			     0);

		WREG32_SOC15(GC, GET_INST(GC, i),
			     regVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_LO32, 0);
		WREG32_SOC15(GC, GET_INST(GC, i),
			     regVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_HI32, 0);
	}
}

static void gfxhub_v1_2_xcc_setup_vmid_config(struct amdgpu_device *adev,
					      uint32_t xcc_mask)
{
	struct amdgpu_vmhub *hub;
	unsigned num_level, block_size;
	uint32_t tmp;
	int i, j;

	num_level = adev->vm_manager.num_level;
	block_size = adev->vm_manager.block_size;
	if (adev->gmc.translate_further)
		num_level -= 1;
	else
		block_size -= 9;

	for_each_inst(j, xcc_mask) {
		hub = &adev->vmhub[AMDGPU_GFXHUB(j)];
		for (i = 0; i <= 14; i++) {
			tmp = RREG32_SOC15_OFFSET(GC, GET_INST(GC, j), regVM_CONTEXT1_CNTL, i);
			tmp = REG_SET_FIELD(tmp, VM_CONTEXT1_CNTL, ENABLE_CONTEXT, 1);
			tmp = REG_SET_FIELD(tmp, VM_CONTEXT1_CNTL, PAGE_TABLE_DEPTH,
					    num_level);
			tmp = REG_SET_FIELD(tmp, VM_CONTEXT1_CNTL,
					    RANGE_PROTECTION_FAULT_ENABLE_DEFAULT, 1);
			tmp = REG_SET_FIELD(tmp, VM_CONTEXT1_CNTL,
					    DUMMY_PAGE_PROTECTION_FAULT_ENABLE_DEFAULT,
					    1);
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
			/* Send no-retry XNACK on fault to suppress VM fault storm.
			 * On 9.4.2 and 9.4.3, XNACK can be enabled in
			 * the SQ per-process.
			 * Retry faults need to be enabled for that to work.
			 */
			tmp = REG_SET_FIELD(tmp, VM_CONTEXT1_CNTL,
					    RETRY_PERMISSION_OR_INVALID_PAGE_FAULT,
					    !adev->gmc.noretry ||
					    adev->ip_versions[GC_HWIP][0] == IP_VERSION(9, 4, 2) ||
					    adev->ip_versions[GC_HWIP][0] == IP_VERSION(9, 4, 3));
			WREG32_SOC15_OFFSET(GC, GET_INST(GC, j), regVM_CONTEXT1_CNTL,
					    i * hub->ctx_distance, tmp);
			WREG32_SOC15_OFFSET(GC, GET_INST(GC, j),
					    regVM_CONTEXT1_PAGE_TABLE_START_ADDR_LO32,
					    i * hub->ctx_addr_distance, 0);
			WREG32_SOC15_OFFSET(GC, GET_INST(GC, j),
					    regVM_CONTEXT1_PAGE_TABLE_START_ADDR_HI32,
					    i * hub->ctx_addr_distance, 0);
			WREG32_SOC15_OFFSET(GC, GET_INST(GC, j),
					    regVM_CONTEXT1_PAGE_TABLE_END_ADDR_LO32,
					    i * hub->ctx_addr_distance,
					    lower_32_bits(adev->vm_manager.max_pfn - 1));
			WREG32_SOC15_OFFSET(GC, GET_INST(GC, j),
					    regVM_CONTEXT1_PAGE_TABLE_END_ADDR_HI32,
					    i * hub->ctx_addr_distance,
					    upper_32_bits(adev->vm_manager.max_pfn - 1));
		}
	}
}

static void gfxhub_v1_2_xcc_program_invalidation(struct amdgpu_device *adev,
						 uint32_t xcc_mask)
{
	struct amdgpu_vmhub *hub;
	unsigned int i, j;

	for_each_inst(j, xcc_mask) {
		hub = &adev->vmhub[AMDGPU_GFXHUB(j)];

		for (i = 0 ; i < 18; ++i) {
			WREG32_SOC15_OFFSET(GC, GET_INST(GC, j), regVM_INVALIDATE_ENG0_ADDR_RANGE_LO32,
					    i * hub->eng_addr_distance, 0xffffffff);
			WREG32_SOC15_OFFSET(GC, GET_INST(GC, j), regVM_INVALIDATE_ENG0_ADDR_RANGE_HI32,
					    i * hub->eng_addr_distance, 0x1f);
		}
	}
}

static int gfxhub_v1_2_xcc_gart_enable(struct amdgpu_device *adev,
				       uint32_t xcc_mask)
{
	uint32_t tmp_mask;
	int i;

	tmp_mask = xcc_mask;
	/*
	 * MC_VM_FB_LOCATION_BASE/TOP is NULL for VF, because they are
	 * VF copy registers so vbios post doesn't program them, for
	 * SRIOV driver need to program them
	 */
	if (amdgpu_sriov_vf(adev)) {
		for_each_inst(i, tmp_mask) {
			i = ffs(tmp_mask) - 1;
			WREG32_SOC15_RLC(GC, GET_INST(GC, i), regMC_VM_FB_LOCATION_BASE,
				     adev->gmc.vram_start >> 24);
			WREG32_SOC15_RLC(GC, GET_INST(GC, i), regMC_VM_FB_LOCATION_TOP,
				     adev->gmc.vram_end >> 24);
		}
	}

	/* GART Enable. */
	gfxhub_v1_2_xcc_init_gart_aperture_regs(adev, xcc_mask);
	gfxhub_v1_2_xcc_init_system_aperture_regs(adev, xcc_mask);
	gfxhub_v1_2_xcc_init_tlb_regs(adev, xcc_mask);
	if (!amdgpu_sriov_vf(adev))
		gfxhub_v1_2_xcc_init_cache_regs(adev, xcc_mask);

	gfxhub_v1_2_xcc_enable_system_domain(adev, xcc_mask);
	if (!amdgpu_sriov_vf(adev))
		gfxhub_v1_2_xcc_disable_identity_aperture(adev, xcc_mask);
	gfxhub_v1_2_xcc_setup_vmid_config(adev, xcc_mask);
	gfxhub_v1_2_xcc_program_invalidation(adev, xcc_mask);

	return 0;
}

static int gfxhub_v1_2_gart_enable(struct amdgpu_device *adev)
{
	uint32_t xcc_mask;

	xcc_mask = GENMASK(NUM_XCC(adev->gfx.xcc_mask) - 1, 0);
	return gfxhub_v1_2_xcc_gart_enable(adev, xcc_mask);
}

static void gfxhub_v1_2_xcc_gart_disable(struct amdgpu_device *adev,
					 uint32_t xcc_mask)
{
	struct amdgpu_vmhub *hub;
	u32 tmp;
	u32 i, j;

	for_each_inst(j, xcc_mask) {
		hub = &adev->vmhub[AMDGPU_GFXHUB(j)];
		/* Disable all tables */
		for (i = 0; i < 16; i++)
			WREG32_SOC15_OFFSET(GC, GET_INST(GC, j), regVM_CONTEXT0_CNTL,
					    i * hub->ctx_distance, 0);

		/* Setup TLB control */
		tmp = RREG32_SOC15(GC, GET_INST(GC, j), regMC_VM_MX_L1_TLB_CNTL);
		tmp = REG_SET_FIELD(tmp, MC_VM_MX_L1_TLB_CNTL, ENABLE_L1_TLB, 0);
		tmp = REG_SET_FIELD(tmp,
					MC_VM_MX_L1_TLB_CNTL,
					ENABLE_ADVANCED_DRIVER_MODEL,
					0);
		WREG32_SOC15_RLC(GC, GET_INST(GC, j), regMC_VM_MX_L1_TLB_CNTL, tmp);

		/* Setup L2 cache */
		tmp = RREG32_SOC15(GC, GET_INST(GC, j), regVM_L2_CNTL);
		tmp = REG_SET_FIELD(tmp, VM_L2_CNTL, ENABLE_L2_CACHE, 0);
		WREG32_SOC15(GC, GET_INST(GC, j), regVM_L2_CNTL, tmp);
		WREG32_SOC15(GC, GET_INST(GC, j), regVM_L2_CNTL3, 0);
	}
}

static void gfxhub_v1_2_gart_disable(struct amdgpu_device *adev)
{
	uint32_t xcc_mask;

	xcc_mask = GENMASK(NUM_XCC(adev->gfx.xcc_mask) - 1, 0);
	gfxhub_v1_2_xcc_gart_disable(adev, xcc_mask);
}

static void gfxhub_v1_2_xcc_set_fault_enable_default(struct amdgpu_device *adev,
						     bool value,
						     uint32_t xcc_mask)
{
	u32 tmp;
	int i;

	for_each_inst(i, xcc_mask) {
		tmp = RREG32_SOC15(GC, GET_INST(GC, i), regVM_L2_PROTECTION_FAULT_CNTL);
		tmp = REG_SET_FIELD(tmp, VM_L2_PROTECTION_FAULT_CNTL,
				RANGE_PROTECTION_FAULT_ENABLE_DEFAULT, value);
		tmp = REG_SET_FIELD(tmp, VM_L2_PROTECTION_FAULT_CNTL,
				PDE0_PROTECTION_FAULT_ENABLE_DEFAULT, value);
		tmp = REG_SET_FIELD(tmp, VM_L2_PROTECTION_FAULT_CNTL,
				PDE1_PROTECTION_FAULT_ENABLE_DEFAULT, value);
		tmp = REG_SET_FIELD(tmp, VM_L2_PROTECTION_FAULT_CNTL,
				PDE2_PROTECTION_FAULT_ENABLE_DEFAULT, value);
		tmp = REG_SET_FIELD(tmp,
				VM_L2_PROTECTION_FAULT_CNTL,
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
		WREG32_SOC15(GC, GET_INST(GC, i), regVM_L2_PROTECTION_FAULT_CNTL, tmp);
	}
}

/**
 * gfxhub_v1_2_set_fault_enable_default - update GART/VM fault handling
 *
 * @adev: amdgpu_device pointer
 * @value: true redirects VM faults to the default page
 */
static void gfxhub_v1_2_set_fault_enable_default(struct amdgpu_device *adev,
						 bool value)
{
	uint32_t xcc_mask;

	xcc_mask = GENMASK(NUM_XCC(adev->gfx.xcc_mask) - 1, 0);
	gfxhub_v1_2_xcc_set_fault_enable_default(adev, value, xcc_mask);
}

static void gfxhub_v1_2_xcc_init(struct amdgpu_device *adev, uint32_t xcc_mask)
{
	struct amdgpu_vmhub *hub;
	int i;

	for_each_inst(i, xcc_mask) {
		hub = &adev->vmhub[AMDGPU_GFXHUB(i)];

		hub->ctx0_ptb_addr_lo32 =
			SOC15_REG_OFFSET(GC, GET_INST(GC, i),
				regVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32);
		hub->ctx0_ptb_addr_hi32 =
			SOC15_REG_OFFSET(GC, GET_INST(GC, i),
				regVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_HI32);
		hub->vm_inv_eng0_sem =
			SOC15_REG_OFFSET(GC, GET_INST(GC, i), regVM_INVALIDATE_ENG0_SEM);
		hub->vm_inv_eng0_req =
			SOC15_REG_OFFSET(GC, GET_INST(GC, i), regVM_INVALIDATE_ENG0_REQ);
		hub->vm_inv_eng0_ack =
			SOC15_REG_OFFSET(GC, GET_INST(GC, i), regVM_INVALIDATE_ENG0_ACK);
		hub->vm_context0_cntl =
			SOC15_REG_OFFSET(GC, GET_INST(GC, i), regVM_CONTEXT0_CNTL);
		hub->vm_l2_pro_fault_status =
			SOC15_REG_OFFSET(GC, GET_INST(GC, i),
				regVM_L2_PROTECTION_FAULT_STATUS);
		hub->vm_l2_pro_fault_cntl =
			SOC15_REG_OFFSET(GC, GET_INST(GC, i), regVM_L2_PROTECTION_FAULT_CNTL);

		hub->ctx_distance = regVM_CONTEXT1_CNTL -
				regVM_CONTEXT0_CNTL;
		hub->ctx_addr_distance =
				regVM_CONTEXT1_PAGE_TABLE_BASE_ADDR_LO32 -
				regVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32;
		hub->eng_distance = regVM_INVALIDATE_ENG1_REQ -
				regVM_INVALIDATE_ENG0_REQ;
		hub->eng_addr_distance =
				regVM_INVALIDATE_ENG1_ADDR_RANGE_LO32 -
				regVM_INVALIDATE_ENG0_ADDR_RANGE_LO32;
	}
}

static void gfxhub_v1_2_init(struct amdgpu_device *adev)
{
	uint32_t xcc_mask;

	xcc_mask = GENMASK(NUM_XCC(adev->gfx.xcc_mask) - 1, 0);
	gfxhub_v1_2_xcc_init(adev, xcc_mask);
}

static int gfxhub_v1_2_get_xgmi_info(struct amdgpu_device *adev)
{
	u32 max_num_physical_nodes;
	u32 max_physical_node_id;
	u32 xgmi_lfb_cntl;
	u32 max_region;
	u64 seg_size;

	xgmi_lfb_cntl = RREG32_SOC15(GC, GET_INST(GC, 0), regMC_VM_XGMI_LFB_CNTL);
	seg_size = REG_GET_FIELD(
		RREG32_SOC15(GC, GET_INST(GC, 0), regMC_VM_XGMI_LFB_SIZE),
		MC_VM_XGMI_LFB_SIZE, PF_LFB_SIZE) << 24;
	max_region =
		REG_GET_FIELD(xgmi_lfb_cntl, MC_VM_XGMI_LFB_CNTL, PF_MAX_REGION);



	max_num_physical_nodes   = 8;
	max_physical_node_id     = 7;

	/* PF_MAX_REGION=0 means xgmi is disabled */
	if (max_region || adev->gmc.xgmi.connected_to_cpu) {
		adev->gmc.xgmi.num_physical_nodes = max_region + 1;

		if (adev->gmc.xgmi.num_physical_nodes > max_num_physical_nodes)
			return -EINVAL;

		adev->gmc.xgmi.physical_node_id =
			REG_GET_FIELD(xgmi_lfb_cntl, MC_VM_XGMI_LFB_CNTL,
					PF_LFB_REGION);

		if (adev->gmc.xgmi.physical_node_id > max_physical_node_id)
			return -EINVAL;

		adev->gmc.xgmi.node_segment_size = seg_size;
	}

	return 0;
}

const struct amdgpu_gfxhub_funcs gfxhub_v1_2_funcs = {
	.get_mc_fb_offset = gfxhub_v1_2_get_mc_fb_offset,
	.setup_vm_pt_regs = gfxhub_v1_2_setup_vm_pt_regs,
	.gart_enable = gfxhub_v1_2_gart_enable,
	.gart_disable = gfxhub_v1_2_gart_disable,
	.set_fault_enable_default = gfxhub_v1_2_set_fault_enable_default,
	.init = gfxhub_v1_2_init,
	.get_xgmi_info = gfxhub_v1_2_get_xgmi_info,
};

static int gfxhub_v1_2_xcp_resume(void *handle, uint32_t inst_mask)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	bool value;

	if (amdgpu_vm_fault_stop == AMDGPU_VM_FAULT_STOP_ALWAYS)
		value = false;
	else
		value = true;

	gfxhub_v1_2_xcc_set_fault_enable_default(adev, value, inst_mask);

	if (!amdgpu_sriov_vf(adev))
		return gfxhub_v1_2_xcc_gart_enable(adev, inst_mask);

	return 0;
}

static int gfxhub_v1_2_xcp_suspend(void *handle, uint32_t inst_mask)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (!amdgpu_sriov_vf(adev))
		gfxhub_v1_2_xcc_gart_disable(adev, inst_mask);

	return 0;
}

struct amdgpu_xcp_ip_funcs gfxhub_v1_2_xcp_funcs = {
	.suspend = &gfxhub_v1_2_xcp_suspend,
	.resume = &gfxhub_v1_2_xcp_resume
};
