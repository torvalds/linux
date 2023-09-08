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

#include "amdgpu.h"
#include "gfxhub_v2_1.h"

#include "gc/gc_10_3_0_offset.h"
#include "gc/gc_10_3_0_sh_mask.h"
#include "gc/gc_10_3_0_default.h"
#include "navi10_enum.h"

#include "soc15_common.h"

#define mmGCUTCL2_HARVEST_BYPASS_GROUPS_YELLOW_CARP				0x16f8
#define mmGCUTCL2_HARVEST_BYPASS_GROUPS_YELLOW_CARP_BASE_IDX	0

static const char *gfxhub_client_ids[] = {
	"CB/DB",
	"Reserved",
	"GE1",
	"GE2",
	"CPF",
	"CPC",
	"CPG",
	"RLC",
	"TCP",
	"SQC (inst)",
	"SQC (data)",
	"SQG",
	"Reserved",
	"SDMA0",
	"SDMA1",
	"GCR",
	"SDMA2",
	"SDMA3",
};

static uint32_t gfxhub_v2_1_get_invalidate_req(unsigned int vmid,
					       uint32_t flush_type)
{
	u32 req = 0;

	/* invalidate using legacy mode on vmid*/
	req = REG_SET_FIELD(req, GCVM_INVALIDATE_ENG0_REQ,
			    PER_VMID_INVALIDATE_REQ, 1 << vmid);
	req = REG_SET_FIELD(req, GCVM_INVALIDATE_ENG0_REQ, FLUSH_TYPE, flush_type);
	req = REG_SET_FIELD(req, GCVM_INVALIDATE_ENG0_REQ, INVALIDATE_L2_PTES, 1);
	req = REG_SET_FIELD(req, GCVM_INVALIDATE_ENG0_REQ, INVALIDATE_L2_PDE0, 1);
	req = REG_SET_FIELD(req, GCVM_INVALIDATE_ENG0_REQ, INVALIDATE_L2_PDE1, 1);
	req = REG_SET_FIELD(req, GCVM_INVALIDATE_ENG0_REQ, INVALIDATE_L2_PDE2, 1);
	req = REG_SET_FIELD(req, GCVM_INVALIDATE_ENG0_REQ, INVALIDATE_L1_PTES, 1);
	req = REG_SET_FIELD(req, GCVM_INVALIDATE_ENG0_REQ,
			    CLEAR_PROTECTION_FAULT_STATUS_ADDR,	0);

	return req;
}

static void
gfxhub_v2_1_print_l2_protection_fault_status(struct amdgpu_device *adev,
					     uint32_t status)
{
	u32 cid = REG_GET_FIELD(status,
				GCVM_L2_PROTECTION_FAULT_STATUS, CID);

	dev_err(adev->dev,
		"GCVM_L2_PROTECTION_FAULT_STATUS:0x%08X\n",
		status);
	dev_err(adev->dev, "\t Faulty UTCL2 client ID: %s (0x%x)\n",
		cid >= ARRAY_SIZE(gfxhub_client_ids) ? "unknown" : gfxhub_client_ids[cid],
		cid);
	dev_err(adev->dev, "\t MORE_FAULTS: 0x%lx\n",
		REG_GET_FIELD(status,
		GCVM_L2_PROTECTION_FAULT_STATUS, MORE_FAULTS));
	dev_err(adev->dev, "\t WALKER_ERROR: 0x%lx\n",
		REG_GET_FIELD(status,
		GCVM_L2_PROTECTION_FAULT_STATUS, WALKER_ERROR));
	dev_err(adev->dev, "\t PERMISSION_FAULTS: 0x%lx\n",
		REG_GET_FIELD(status,
		GCVM_L2_PROTECTION_FAULT_STATUS, PERMISSION_FAULTS));
	dev_err(adev->dev, "\t MAPPING_ERROR: 0x%lx\n",
		REG_GET_FIELD(status,
		GCVM_L2_PROTECTION_FAULT_STATUS, MAPPING_ERROR));
	dev_err(adev->dev, "\t RW: 0x%lx\n",
		REG_GET_FIELD(status,
		GCVM_L2_PROTECTION_FAULT_STATUS, RW));
}

static u64 gfxhub_v2_1_get_fb_location(struct amdgpu_device *adev)
{
	u64 base = RREG32_SOC15(GC, 0, mmGCMC_VM_FB_LOCATION_BASE);

	base &= GCMC_VM_FB_LOCATION_BASE__FB_BASE_MASK;
	base <<= 24;

	return base;
}

static u64 gfxhub_v2_1_get_mc_fb_offset(struct amdgpu_device *adev)
{
	return (u64)RREG32_SOC15(GC, 0, mmGCMC_VM_FB_OFFSET) << 24;
}

static void gfxhub_v2_1_setup_vm_pt_regs(struct amdgpu_device *adev, uint32_t vmid,
				uint64_t page_table_base)
{
	struct amdgpu_vmhub *hub = &adev->vmhub[AMDGPU_GFXHUB_0];

	WREG32_SOC15_OFFSET(GC, 0, mmGCVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32,
			    hub->ctx_addr_distance * vmid,
			    lower_32_bits(page_table_base));

	WREG32_SOC15_OFFSET(GC, 0, mmGCVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_HI32,
			    hub->ctx_addr_distance * vmid,
			    upper_32_bits(page_table_base));
}

static void gfxhub_v2_1_init_gart_aperture_regs(struct amdgpu_device *adev)
{
	uint64_t pt_base = amdgpu_gmc_pd_addr(adev->gart.bo);

	gfxhub_v2_1_setup_vm_pt_regs(adev, 0, pt_base);

	WREG32_SOC15(GC, 0, mmGCVM_CONTEXT0_PAGE_TABLE_START_ADDR_LO32,
		     (u32)(adev->gmc.gart_start >> 12));
	WREG32_SOC15(GC, 0, mmGCVM_CONTEXT0_PAGE_TABLE_START_ADDR_HI32,
		     (u32)(adev->gmc.gart_start >> 44));

	WREG32_SOC15(GC, 0, mmGCVM_CONTEXT0_PAGE_TABLE_END_ADDR_LO32,
		     (u32)(adev->gmc.gart_end >> 12));
	WREG32_SOC15(GC, 0, mmGCVM_CONTEXT0_PAGE_TABLE_END_ADDR_HI32,
		     (u32)(adev->gmc.gart_end >> 44));
}

static void gfxhub_v2_1_init_system_aperture_regs(struct amdgpu_device *adev)
{
	uint64_t value;

	/* Program the AGP BAR */
	WREG32_SOC15(GC, 0, mmGCMC_VM_AGP_BASE, 0);
	WREG32_SOC15(GC, 0, mmGCMC_VM_AGP_BOT, adev->gmc.agp_start >> 24);
	WREG32_SOC15(GC, 0, mmGCMC_VM_AGP_TOP, adev->gmc.agp_end >> 24);

	/* Program the system aperture low logical page number. */
	WREG32_SOC15(GC, 0, mmGCMC_VM_SYSTEM_APERTURE_LOW_ADDR,
		     min(adev->gmc.fb_start, adev->gmc.agp_start) >> 18);
	WREG32_SOC15(GC, 0, mmGCMC_VM_SYSTEM_APERTURE_HIGH_ADDR,
		     max(adev->gmc.fb_end, adev->gmc.agp_end) >> 18);

	/* Set default page address. */
	value = amdgpu_gmc_vram_mc2pa(adev, adev->mem_scratch.gpu_addr);
	WREG32_SOC15(GC, 0, mmGCMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_LSB,
		     (u32)(value >> 12));
	WREG32_SOC15(GC, 0, mmGCMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_MSB,
		     (u32)(value >> 44));

	/* Program "protection fault". */
	WREG32_SOC15(GC, 0, mmGCVM_L2_PROTECTION_FAULT_DEFAULT_ADDR_LO32,
		     (u32)(adev->dummy_page_addr >> 12));
	WREG32_SOC15(GC, 0, mmGCVM_L2_PROTECTION_FAULT_DEFAULT_ADDR_HI32,
		     (u32)((u64)adev->dummy_page_addr >> 44));

	WREG32_FIELD15(GC, 0, GCVM_L2_PROTECTION_FAULT_CNTL2,
		       ACTIVE_PAGE_MIGRATION_PTE_READ_RETRY, 1);
}


static void gfxhub_v2_1_init_tlb_regs(struct amdgpu_device *adev)
{
	uint32_t tmp;

	/* Setup TLB control */
	tmp = RREG32_SOC15(GC, 0, mmGCMC_VM_MX_L1_TLB_CNTL);

	tmp = REG_SET_FIELD(tmp, GCMC_VM_MX_L1_TLB_CNTL, ENABLE_L1_TLB, 1);
	tmp = REG_SET_FIELD(tmp, GCMC_VM_MX_L1_TLB_CNTL, SYSTEM_ACCESS_MODE, 3);
	tmp = REG_SET_FIELD(tmp, GCMC_VM_MX_L1_TLB_CNTL,
			    ENABLE_ADVANCED_DRIVER_MODEL, 1);
	tmp = REG_SET_FIELD(tmp, GCMC_VM_MX_L1_TLB_CNTL,
			    SYSTEM_APERTURE_UNMAPPED_ACCESS, 0);
	tmp = REG_SET_FIELD(tmp, GCMC_VM_MX_L1_TLB_CNTL,
			    MTYPE, MTYPE_UC); /* UC, uncached */

	WREG32_SOC15(GC, 0, mmGCMC_VM_MX_L1_TLB_CNTL, tmp);
}

static void gfxhub_v2_1_init_cache_regs(struct amdgpu_device *adev)
{
	uint32_t tmp;

	/* These registers are not accessible to VF-SRIOV.
	 * The PF will program them instead.
	 */
	if (amdgpu_sriov_vf(adev))
		return;

	/* Setup L2 cache */
	tmp = RREG32_SOC15(GC, 0, mmGCVM_L2_CNTL);
	tmp = REG_SET_FIELD(tmp, GCVM_L2_CNTL, ENABLE_L2_CACHE, 1);
	tmp = REG_SET_FIELD(tmp, GCVM_L2_CNTL, ENABLE_L2_FRAGMENT_PROCESSING, 0);
	tmp = REG_SET_FIELD(tmp, GCVM_L2_CNTL,
			    ENABLE_DEFAULT_PAGE_OUT_TO_SYSTEM_MEMORY, 1);
	/* XXX for emulation, Refer to closed source code.*/
	tmp = REG_SET_FIELD(tmp, GCVM_L2_CNTL,
			    L2_PDE0_CACHE_TAG_GENERATION_MODE, 0);
	tmp = REG_SET_FIELD(tmp, GCVM_L2_CNTL, PDE_FAULT_CLASSIFICATION, 0);
	tmp = REG_SET_FIELD(tmp, GCVM_L2_CNTL, CONTEXT1_IDENTITY_ACCESS_MODE, 1);
	tmp = REG_SET_FIELD(tmp, GCVM_L2_CNTL, IDENTITY_MODE_FRAGMENT_SIZE, 0);
	WREG32_SOC15(GC, 0, mmGCVM_L2_CNTL, tmp);

	tmp = RREG32_SOC15(GC, 0, mmGCVM_L2_CNTL2);
	tmp = REG_SET_FIELD(tmp, GCVM_L2_CNTL2, INVALIDATE_ALL_L1_TLBS, 1);
	tmp = REG_SET_FIELD(tmp, GCVM_L2_CNTL2, INVALIDATE_L2_CACHE, 1);
	WREG32_SOC15(GC, 0, mmGCVM_L2_CNTL2, tmp);

	tmp = mmGCVM_L2_CNTL3_DEFAULT;
	if (adev->gmc.translate_further) {
		tmp = REG_SET_FIELD(tmp, GCVM_L2_CNTL3, BANK_SELECT, 12);
		tmp = REG_SET_FIELD(tmp, GCVM_L2_CNTL3,
				    L2_CACHE_BIGK_FRAGMENT_SIZE, 9);
	} else {
		tmp = REG_SET_FIELD(tmp, GCVM_L2_CNTL3, BANK_SELECT, 9);
		tmp = REG_SET_FIELD(tmp, GCVM_L2_CNTL3,
				    L2_CACHE_BIGK_FRAGMENT_SIZE, 6);
	}
	WREG32_SOC15(GC, 0, mmGCVM_L2_CNTL3, tmp);

	tmp = mmGCVM_L2_CNTL4_DEFAULT;
	tmp = REG_SET_FIELD(tmp, GCVM_L2_CNTL4, VMC_TAP_PDE_REQUEST_PHYSICAL, 0);
	tmp = REG_SET_FIELD(tmp, GCVM_L2_CNTL4, VMC_TAP_PTE_REQUEST_PHYSICAL, 0);
	WREG32_SOC15(GC, 0, mmGCVM_L2_CNTL4, tmp);

	tmp = mmGCVM_L2_CNTL5_DEFAULT;
	tmp = REG_SET_FIELD(tmp, GCVM_L2_CNTL5, L2_CACHE_SMALLK_FRAGMENT_SIZE, 0);
	WREG32_SOC15(GC, 0, mmGCVM_L2_CNTL5, tmp);
}

static void gfxhub_v2_1_enable_system_domain(struct amdgpu_device *adev)
{
	uint32_t tmp;

	tmp = RREG32_SOC15(GC, 0, mmGCVM_CONTEXT0_CNTL);
	tmp = REG_SET_FIELD(tmp, GCVM_CONTEXT0_CNTL, ENABLE_CONTEXT, 1);
	tmp = REG_SET_FIELD(tmp, GCVM_CONTEXT0_CNTL, PAGE_TABLE_DEPTH, 0);
	tmp = REG_SET_FIELD(tmp, GCVM_CONTEXT0_CNTL,
			    RETRY_PERMISSION_OR_INVALID_PAGE_FAULT, 0);
	WREG32_SOC15(GC, 0, mmGCVM_CONTEXT0_CNTL, tmp);
}

static void gfxhub_v2_1_disable_identity_aperture(struct amdgpu_device *adev)
{
	/* These registers are not accessible to VF-SRIOV.
	 * The PF will program them instead.
	 */
	if (amdgpu_sriov_vf(adev))
		return;

	WREG32_SOC15(GC, 0, mmGCVM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_LO32,
		     0xFFFFFFFF);
	WREG32_SOC15(GC, 0, mmGCVM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_HI32,
		     0x0000000F);

	WREG32_SOC15(GC, 0, mmGCVM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_LO32,
		     0);
	WREG32_SOC15(GC, 0, mmGCVM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_HI32,
		     0);

	WREG32_SOC15(GC, 0, mmGCVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_LO32, 0);
	WREG32_SOC15(GC, 0, mmGCVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_HI32, 0);

}

static void gfxhub_v2_1_setup_vmid_config(struct amdgpu_device *adev)
{
	struct amdgpu_vmhub *hub = &adev->vmhub[AMDGPU_GFXHUB_0];
	int i;
	uint32_t tmp;

	for (i = 0; i <= 14; i++) {
		tmp = RREG32_SOC15_OFFSET(GC, 0, mmGCVM_CONTEXT1_CNTL, i);
		tmp = REG_SET_FIELD(tmp, GCVM_CONTEXT1_CNTL, ENABLE_CONTEXT, 1);
		tmp = REG_SET_FIELD(tmp, GCVM_CONTEXT1_CNTL, PAGE_TABLE_DEPTH,
				    adev->vm_manager.num_level);
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
				PAGE_TABLE_BLOCK_SIZE,
				adev->vm_manager.block_size - 9);
		/* Send no-retry XNACK on fault to suppress VM fault storm. */
		tmp = REG_SET_FIELD(tmp, GCVM_CONTEXT1_CNTL,
				    RETRY_PERMISSION_OR_INVALID_PAGE_FAULT,
				    !adev->gmc.noretry);
		WREG32_SOC15_OFFSET(GC, 0, mmGCVM_CONTEXT1_CNTL,
				    i * hub->ctx_distance, tmp);
		WREG32_SOC15_OFFSET(GC, 0, mmGCVM_CONTEXT1_PAGE_TABLE_START_ADDR_LO32,
				    i * hub->ctx_addr_distance, 0);
		WREG32_SOC15_OFFSET(GC, 0, mmGCVM_CONTEXT1_PAGE_TABLE_START_ADDR_HI32,
				    i * hub->ctx_addr_distance, 0);
		WREG32_SOC15_OFFSET(GC, 0, mmGCVM_CONTEXT1_PAGE_TABLE_END_ADDR_LO32,
				    i * hub->ctx_addr_distance,
				    lower_32_bits(adev->vm_manager.max_pfn - 1));
		WREG32_SOC15_OFFSET(GC, 0, mmGCVM_CONTEXT1_PAGE_TABLE_END_ADDR_HI32,
				    i * hub->ctx_addr_distance,
				    upper_32_bits(adev->vm_manager.max_pfn - 1));
	}

	hub->vm_cntx_cntl = tmp;
}

static void gfxhub_v2_1_program_invalidation(struct amdgpu_device *adev)
{
	struct amdgpu_vmhub *hub = &adev->vmhub[AMDGPU_GFXHUB_0];
	unsigned i;

	for (i = 0 ; i < 18; ++i) {
		WREG32_SOC15_OFFSET(GC, 0, mmGCVM_INVALIDATE_ENG0_ADDR_RANGE_LO32,
				    i * hub->eng_addr_distance, 0xffffffff);
		WREG32_SOC15_OFFSET(GC, 0, mmGCVM_INVALIDATE_ENG0_ADDR_RANGE_HI32,
				    i * hub->eng_addr_distance, 0x1f);
	}
}

static int gfxhub_v2_1_gart_enable(struct amdgpu_device *adev)
{
	if (amdgpu_sriov_vf(adev)) {
		/*
		 * GCMC_VM_FB_LOCATION_BASE/TOP is NULL for VF, becuase they are
		 * VF copy registers so vbios post doesn't program them, for
		 * SRIOV driver need to program them
		 */
		WREG32_SOC15(GC, 0, mmGCMC_VM_FB_LOCATION_BASE,
			     adev->gmc.vram_start >> 24);
		WREG32_SOC15(GC, 0, mmGCMC_VM_FB_LOCATION_TOP,
			     adev->gmc.vram_end >> 24);
	}

	/* GART Enable. */
	gfxhub_v2_1_init_gart_aperture_regs(adev);
	gfxhub_v2_1_init_system_aperture_regs(adev);
	gfxhub_v2_1_init_tlb_regs(adev);
	gfxhub_v2_1_init_cache_regs(adev);

	gfxhub_v2_1_enable_system_domain(adev);
	gfxhub_v2_1_disable_identity_aperture(adev);
	gfxhub_v2_1_setup_vmid_config(adev);
	gfxhub_v2_1_program_invalidation(adev);

	return 0;
}

static void gfxhub_v2_1_gart_disable(struct amdgpu_device *adev)
{
	struct amdgpu_vmhub *hub = &adev->vmhub[AMDGPU_GFXHUB_0];
	u32 tmp;
	u32 i;

	/* Disable all tables */
	for (i = 0; i < 16; i++)
		WREG32_SOC15_OFFSET(GC, 0, mmGCVM_CONTEXT0_CNTL,
				    i * hub->ctx_distance, 0);

	/* Setup TLB control */
	tmp = RREG32_SOC15(GC, 0, mmGCMC_VM_MX_L1_TLB_CNTL);
	tmp = REG_SET_FIELD(tmp, GCMC_VM_MX_L1_TLB_CNTL, ENABLE_L1_TLB, 0);
	tmp = REG_SET_FIELD(tmp, GCMC_VM_MX_L1_TLB_CNTL,
			    ENABLE_ADVANCED_DRIVER_MODEL, 0);
	WREG32_SOC15(GC, 0, mmGCMC_VM_MX_L1_TLB_CNTL, tmp);

	if (amdgpu_sriov_vf(adev))
		return;

	/* Setup L2 cache */
	WREG32_FIELD15(GC, 0, GCVM_L2_CNTL, ENABLE_L2_CACHE, 0);
	WREG32_SOC15(GC, 0, mmGCVM_L2_CNTL3, 0);
}

/**
 * gfxhub_v2_1_set_fault_enable_default - update GART/VM fault handling
 *
 * @adev: amdgpu_device pointer
 * @value: true redirects VM faults to the default page
 */
static void gfxhub_v2_1_set_fault_enable_default(struct amdgpu_device *adev,
					  bool value)
{
	u32 tmp;

	/* These registers are not accessible to VF-SRIOV.
	 * The PF will program them instead.
	 */
	if (amdgpu_sriov_vf(adev))
		return;

	tmp = RREG32_SOC15(GC, 0, mmGCVM_L2_PROTECTION_FAULT_CNTL);
	tmp = REG_SET_FIELD(tmp, GCVM_L2_PROTECTION_FAULT_CNTL,
			    RANGE_PROTECTION_FAULT_ENABLE_DEFAULT, value);
	tmp = REG_SET_FIELD(tmp, GCVM_L2_PROTECTION_FAULT_CNTL,
			    PDE0_PROTECTION_FAULT_ENABLE_DEFAULT, value);
	tmp = REG_SET_FIELD(tmp, GCVM_L2_PROTECTION_FAULT_CNTL,
			    PDE1_PROTECTION_FAULT_ENABLE_DEFAULT, value);
	tmp = REG_SET_FIELD(tmp, GCVM_L2_PROTECTION_FAULT_CNTL,
			    PDE2_PROTECTION_FAULT_ENABLE_DEFAULT, value);
	tmp = REG_SET_FIELD(tmp, GCVM_L2_PROTECTION_FAULT_CNTL,
			    TRANSLATE_FURTHER_PROTECTION_FAULT_ENABLE_DEFAULT,
			    value);
	tmp = REG_SET_FIELD(tmp, GCVM_L2_PROTECTION_FAULT_CNTL,
			    NACK_PROTECTION_FAULT_ENABLE_DEFAULT, value);
	tmp = REG_SET_FIELD(tmp, GCVM_L2_PROTECTION_FAULT_CNTL,
			    DUMMY_PAGE_PROTECTION_FAULT_ENABLE_DEFAULT, value);
	tmp = REG_SET_FIELD(tmp, GCVM_L2_PROTECTION_FAULT_CNTL,
			    VALID_PROTECTION_FAULT_ENABLE_DEFAULT, value);
	tmp = REG_SET_FIELD(tmp, GCVM_L2_PROTECTION_FAULT_CNTL,
			    READ_PROTECTION_FAULT_ENABLE_DEFAULT, value);
	tmp = REG_SET_FIELD(tmp, GCVM_L2_PROTECTION_FAULT_CNTL,
			    WRITE_PROTECTION_FAULT_ENABLE_DEFAULT, value);
	tmp = REG_SET_FIELD(tmp, GCVM_L2_PROTECTION_FAULT_CNTL,
			    EXECUTE_PROTECTION_FAULT_ENABLE_DEFAULT, value);
	if (!value) {
		tmp = REG_SET_FIELD(tmp, GCVM_L2_PROTECTION_FAULT_CNTL,
				CRASH_ON_NO_RETRY_FAULT, 1);
		tmp = REG_SET_FIELD(tmp, GCVM_L2_PROTECTION_FAULT_CNTL,
				CRASH_ON_RETRY_FAULT, 1);
	}
	WREG32_SOC15(GC, 0, mmGCVM_L2_PROTECTION_FAULT_CNTL, tmp);
}

static const struct amdgpu_vmhub_funcs gfxhub_v2_1_vmhub_funcs = {
	.print_l2_protection_fault_status = gfxhub_v2_1_print_l2_protection_fault_status,
	.get_invalidate_req = gfxhub_v2_1_get_invalidate_req,
};

static void gfxhub_v2_1_init(struct amdgpu_device *adev)
{
	struct amdgpu_vmhub *hub = &adev->vmhub[AMDGPU_GFXHUB_0];

	hub->ctx0_ptb_addr_lo32 =
		SOC15_REG_OFFSET(GC, 0,
				 mmGCVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32);
	hub->ctx0_ptb_addr_hi32 =
		SOC15_REG_OFFSET(GC, 0,
				 mmGCVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_HI32);
	hub->vm_inv_eng0_sem =
		SOC15_REG_OFFSET(GC, 0, mmGCVM_INVALIDATE_ENG0_SEM);
	hub->vm_inv_eng0_req =
		SOC15_REG_OFFSET(GC, 0, mmGCVM_INVALIDATE_ENG0_REQ);
	hub->vm_inv_eng0_ack =
		SOC15_REG_OFFSET(GC, 0, mmGCVM_INVALIDATE_ENG0_ACK);
	hub->vm_context0_cntl =
		SOC15_REG_OFFSET(GC, 0, mmGCVM_CONTEXT0_CNTL);
	hub->vm_l2_pro_fault_status =
		SOC15_REG_OFFSET(GC, 0, mmGCVM_L2_PROTECTION_FAULT_STATUS);
	hub->vm_l2_pro_fault_cntl =
		SOC15_REG_OFFSET(GC, 0, mmGCVM_L2_PROTECTION_FAULT_CNTL);

	hub->ctx_distance = mmGCVM_CONTEXT1_CNTL - mmGCVM_CONTEXT0_CNTL;
	hub->ctx_addr_distance = mmGCVM_CONTEXT1_PAGE_TABLE_BASE_ADDR_LO32 -
		mmGCVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32;
	hub->eng_distance = mmGCVM_INVALIDATE_ENG1_REQ -
		mmGCVM_INVALIDATE_ENG0_REQ;
	hub->eng_addr_distance = mmGCVM_INVALIDATE_ENG1_ADDR_RANGE_LO32 -
		mmGCVM_INVALIDATE_ENG0_ADDR_RANGE_LO32;

	hub->vm_cntx_cntl_vm_fault = GCVM_CONTEXT1_CNTL__RANGE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK |
		GCVM_CONTEXT1_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK |
		GCVM_CONTEXT1_CNTL__PDE0_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK |
		GCVM_CONTEXT1_CNTL__VALID_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK |
		GCVM_CONTEXT1_CNTL__READ_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK |
		GCVM_CONTEXT1_CNTL__WRITE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK |
		GCVM_CONTEXT1_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK;

	hub->vmhub_funcs = &gfxhub_v2_1_vmhub_funcs;
}

static int gfxhub_v2_1_get_xgmi_info(struct amdgpu_device *adev)
{
	u32 xgmi_lfb_cntl = RREG32_SOC15(GC, 0, mmGCMC_VM_XGMI_LFB_CNTL);
	u32 max_region =
		REG_GET_FIELD(xgmi_lfb_cntl, GCMC_VM_XGMI_LFB_CNTL, PF_MAX_REGION);
	u32 max_num_physical_nodes   = 0;
	u32 max_physical_node_id     = 0;

	switch (adev->ip_versions[XGMI_HWIP][0]) {
	case IP_VERSION(4, 8, 0):
		max_num_physical_nodes   = 4;
		max_physical_node_id     = 3;
		break;
	default:
		return -EINVAL;
	}

	/* PF_MAX_REGION=0 means xgmi is disabled */
	if (max_region) {
		adev->gmc.xgmi.num_physical_nodes = max_region + 1;
		if (adev->gmc.xgmi.num_physical_nodes > max_num_physical_nodes)
			return -EINVAL;

		adev->gmc.xgmi.physical_node_id =
			REG_GET_FIELD(xgmi_lfb_cntl, GCMC_VM_XGMI_LFB_CNTL, PF_LFB_REGION);
		if (adev->gmc.xgmi.physical_node_id > max_physical_node_id)
			return -EINVAL;

		adev->gmc.xgmi.node_segment_size = REG_GET_FIELD(
			RREG32_SOC15(GC, 0, mmGCMC_VM_XGMI_LFB_SIZE),
			GCMC_VM_XGMI_LFB_SIZE, PF_LFB_SIZE) << 24;
	}

	return 0;
}

static void gfxhub_v2_1_utcl2_harvest(struct amdgpu_device *adev)
{
	int i;
	u32 tmp = 0, disabled_sa = 0;
	u32 efuse_setting, vbios_setting;

	u32 max_sa_mask = amdgpu_gfx_create_bitmask(
		adev->gfx.config.max_sh_per_se *
		adev->gfx.config.max_shader_engines);

	switch (adev->ip_versions[GC_HWIP][0]) {
	case IP_VERSION(10, 3, 1):
	case IP_VERSION(10, 3, 3):
		/* Get SA disabled bitmap from eFuse setting */
		efuse_setting = RREG32_SOC15(GC, 0, mmCC_GC_SA_UNIT_DISABLE);
		efuse_setting &= CC_GC_SA_UNIT_DISABLE__SA_DISABLE_MASK;
		efuse_setting >>= CC_GC_SA_UNIT_DISABLE__SA_DISABLE__SHIFT;

		/* Get SA disabled bitmap from VBIOS setting */
		vbios_setting = RREG32_SOC15(GC, 0, mmGC_USER_SA_UNIT_DISABLE);
		vbios_setting &= GC_USER_SA_UNIT_DISABLE__SA_DISABLE_MASK;
		vbios_setting >>= GC_USER_SA_UNIT_DISABLE__SA_DISABLE__SHIFT;

		disabled_sa |= efuse_setting | vbios_setting;
		/* Make sure not to report harvested SAs beyond the max SA count */
		disabled_sa &= max_sa_mask;

		for (i = 0; disabled_sa > 0; i++) {
			if (disabled_sa & 1)
				tmp |= 0x3 << (i * 2);
			disabled_sa >>= 1;
		}
		disabled_sa = tmp;

		WREG32_SOC15(GC, 0, mmGCUTCL2_HARVEST_BYPASS_GROUPS_YELLOW_CARP, disabled_sa);
		break;
	default:
		break;
	}
}

static void gfxhub_v2_1_save_regs(struct amdgpu_device *adev)
{
	int i;
	adev->gmc.VM_L2_CNTL = RREG32_SOC15(GC, 0, mmGCVM_L2_CNTL);
	adev->gmc.VM_L2_CNTL2 = RREG32_SOC15(GC, 0, mmGCVM_L2_CNTL2);
	adev->gmc.VM_DUMMY_PAGE_FAULT_CNTL = RREG32_SOC15(GC, 0, mmGCVM_DUMMY_PAGE_FAULT_CNTL);
	adev->gmc.VM_DUMMY_PAGE_FAULT_ADDR_LO32 = RREG32_SOC15(GC, 0, mmGCVM_DUMMY_PAGE_FAULT_ADDR_LO32);
	adev->gmc.VM_DUMMY_PAGE_FAULT_ADDR_HI32 = RREG32_SOC15(GC, 0, mmGCVM_DUMMY_PAGE_FAULT_ADDR_HI32);
	adev->gmc.VM_L2_PROTECTION_FAULT_CNTL = RREG32_SOC15(GC, 0, mmGCVM_L2_PROTECTION_FAULT_CNTL);
	adev->gmc.VM_L2_PROTECTION_FAULT_CNTL2 = RREG32_SOC15(GC, 0, mmGCVM_L2_PROTECTION_FAULT_CNTL2);
	adev->gmc.VM_L2_PROTECTION_FAULT_MM_CNTL3 = RREG32_SOC15(GC, 0, mmGCVM_L2_PROTECTION_FAULT_MM_CNTL3);
	adev->gmc.VM_L2_PROTECTION_FAULT_MM_CNTL4 = RREG32_SOC15(GC, 0, mmGCVM_L2_PROTECTION_FAULT_MM_CNTL4);
	adev->gmc.VM_L2_PROTECTION_FAULT_ADDR_LO32 = RREG32_SOC15(GC, 0, mmGCVM_L2_PROTECTION_FAULT_ADDR_LO32);
	adev->gmc.VM_L2_PROTECTION_FAULT_ADDR_HI32 = RREG32_SOC15(GC, 0, mmGCVM_L2_PROTECTION_FAULT_ADDR_HI32);
	adev->gmc.VM_DEBUG = RREG32_SOC15(GC, 0, mmGCVM_DEBUG);
	adev->gmc.VM_L2_MM_GROUP_RT_CLASSES = RREG32_SOC15(GC, 0, mmGCVM_L2_MM_GROUP_RT_CLASSES);
	adev->gmc.VM_L2_BANK_SELECT_RESERVED_CID = RREG32_SOC15(GC, 0, mmGCVM_L2_BANK_SELECT_RESERVED_CID);
	adev->gmc.VM_L2_BANK_SELECT_RESERVED_CID2 = RREG32_SOC15(GC, 0, mmGCVM_L2_BANK_SELECT_RESERVED_CID2);
	adev->gmc.VM_L2_CACHE_PARITY_CNTL = RREG32_SOC15(GC, 0, mmGCVM_L2_CACHE_PARITY_CNTL);
	adev->gmc.VM_L2_IH_LOG_CNTL = RREG32_SOC15(GC, 0, mmGCVM_L2_IH_LOG_CNTL);

	for (i = 0; i <= 15; i++) {
		adev->gmc.VM_CONTEXT_CNTL[i] = RREG32_SOC15_OFFSET(GC, 0, mmGCVM_CONTEXT0_CNTL, i);
		adev->gmc.VM_CONTEXT_PAGE_TABLE_BASE_ADDR_LO32[i] = RREG32_SOC15_OFFSET(GC, 0, mmGCVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32, i * 2);
		adev->gmc.VM_CONTEXT_PAGE_TABLE_BASE_ADDR_HI32[i] = RREG32_SOC15_OFFSET(GC, 0, mmGCVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_HI32, i * 2);
		adev->gmc.VM_CONTEXT_PAGE_TABLE_START_ADDR_LO32[i] = RREG32_SOC15_OFFSET(GC, 0, mmGCVM_CONTEXT0_PAGE_TABLE_START_ADDR_LO32, i * 2);
		adev->gmc.VM_CONTEXT_PAGE_TABLE_START_ADDR_HI32[i] = RREG32_SOC15_OFFSET(GC, 0, mmGCVM_CONTEXT0_PAGE_TABLE_START_ADDR_HI32, i * 2);
		adev->gmc.VM_CONTEXT_PAGE_TABLE_END_ADDR_LO32[i] = RREG32_SOC15_OFFSET(GC, 0, mmGCVM_CONTEXT0_PAGE_TABLE_END_ADDR_LO32, i * 2);
		adev->gmc.VM_CONTEXT_PAGE_TABLE_END_ADDR_HI32[i] = RREG32_SOC15_OFFSET(GC, 0, mmGCVM_CONTEXT0_PAGE_TABLE_END_ADDR_HI32, i * 2);
	}

	adev->gmc.MC_VM_MX_L1_TLB_CNTL = RREG32_SOC15(GC, 0, mmGCMC_VM_MX_L1_TLB_CNTL);
}

static void gfxhub_v2_1_restore_regs(struct amdgpu_device *adev)
{
	int i;
	WREG32_SOC15(GC, 0, mmGCVM_L2_CNTL, adev->gmc.VM_L2_CNTL);
	WREG32_SOC15(GC, 0, mmGCVM_L2_CNTL2, adev->gmc.VM_L2_CNTL2);
	WREG32_SOC15(GC, 0, mmGCVM_DUMMY_PAGE_FAULT_CNTL, adev->gmc.VM_DUMMY_PAGE_FAULT_CNTL);
	WREG32_SOC15(GC, 0, mmGCVM_DUMMY_PAGE_FAULT_ADDR_LO32, adev->gmc.VM_DUMMY_PAGE_FAULT_ADDR_LO32);
	WREG32_SOC15(GC, 0, mmGCVM_DUMMY_PAGE_FAULT_ADDR_HI32, adev->gmc.VM_DUMMY_PAGE_FAULT_ADDR_HI32);
	WREG32_SOC15(GC, 0, mmGCVM_L2_PROTECTION_FAULT_CNTL, adev->gmc.VM_L2_PROTECTION_FAULT_CNTL);
	WREG32_SOC15(GC, 0, mmGCVM_L2_PROTECTION_FAULT_CNTL2, adev->gmc.VM_L2_PROTECTION_FAULT_CNTL2);
	WREG32_SOC15(GC, 0, mmGCVM_L2_PROTECTION_FAULT_MM_CNTL3, adev->gmc.VM_L2_PROTECTION_FAULT_MM_CNTL3);
	WREG32_SOC15(GC, 0, mmGCVM_L2_PROTECTION_FAULT_MM_CNTL4, adev->gmc.VM_L2_PROTECTION_FAULT_MM_CNTL4);
	WREG32_SOC15(GC, 0, mmGCVM_L2_PROTECTION_FAULT_ADDR_LO32, adev->gmc.VM_L2_PROTECTION_FAULT_ADDR_LO32);
	WREG32_SOC15(GC, 0, mmGCVM_L2_PROTECTION_FAULT_ADDR_HI32, adev->gmc.VM_L2_PROTECTION_FAULT_ADDR_HI32);
	WREG32_SOC15(GC, 0, mmGCVM_DEBUG, adev->gmc.VM_DEBUG);
	WREG32_SOC15(GC, 0, mmGCVM_L2_MM_GROUP_RT_CLASSES, adev->gmc.VM_L2_MM_GROUP_RT_CLASSES);
	WREG32_SOC15(GC, 0, mmGCVM_L2_BANK_SELECT_RESERVED_CID, adev->gmc.VM_L2_BANK_SELECT_RESERVED_CID);
	WREG32_SOC15(GC, 0, mmGCVM_L2_BANK_SELECT_RESERVED_CID2, adev->gmc.VM_L2_BANK_SELECT_RESERVED_CID2);
	WREG32_SOC15(GC, 0, mmGCVM_L2_CACHE_PARITY_CNTL, adev->gmc.VM_L2_CACHE_PARITY_CNTL);
	WREG32_SOC15(GC, 0, mmGCVM_L2_IH_LOG_CNTL, adev->gmc.VM_L2_IH_LOG_CNTL);

	for (i = 0; i <= 15; i++) {
		WREG32_SOC15_OFFSET(GC, 0, mmGCVM_CONTEXT0_CNTL, i, adev->gmc.VM_CONTEXT_CNTL[i]);
		WREG32_SOC15_OFFSET(GC, 0, mmGCVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32, i * 2, adev->gmc.VM_CONTEXT_PAGE_TABLE_BASE_ADDR_LO32[i]);
		WREG32_SOC15_OFFSET(GC, 0, mmGCVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_HI32, i * 2, adev->gmc.VM_CONTEXT_PAGE_TABLE_BASE_ADDR_HI32[i]);
		WREG32_SOC15_OFFSET(GC, 0, mmGCVM_CONTEXT0_PAGE_TABLE_START_ADDR_LO32, i * 2, adev->gmc.VM_CONTEXT_PAGE_TABLE_START_ADDR_LO32[i]);
		WREG32_SOC15_OFFSET(GC, 0, mmGCVM_CONTEXT0_PAGE_TABLE_START_ADDR_HI32, i * 2, adev->gmc.VM_CONTEXT_PAGE_TABLE_START_ADDR_HI32[i]);
		WREG32_SOC15_OFFSET(GC, 0, mmGCVM_CONTEXT0_PAGE_TABLE_END_ADDR_LO32, i * 2, adev->gmc.VM_CONTEXT_PAGE_TABLE_END_ADDR_LO32[i]);
		WREG32_SOC15_OFFSET(GC, 0, mmGCVM_CONTEXT0_PAGE_TABLE_END_ADDR_HI32, i * 2, adev->gmc.VM_CONTEXT_PAGE_TABLE_END_ADDR_HI32[i]);
	}

	WREG32_SOC15(GC, 0, mmGCMC_VM_FB_LOCATION_BASE, adev->gmc.vram_start >> 24);
	WREG32_SOC15(GC, 0, mmGCMC_VM_FB_LOCATION_TOP, adev->gmc.vram_end >> 24);
	WREG32_SOC15(GC, 0, mmGCMC_VM_MX_L1_TLB_CNTL, adev->gmc.MC_VM_MX_L1_TLB_CNTL);
}

static void gfxhub_v2_1_halt(struct amdgpu_device *adev)
{
	struct amdgpu_vmhub *hub = &adev->vmhub[AMDGPU_GFXHUB_0];
	int i;
	uint32_t tmp;
	int time = 1000;

	gfxhub_v2_1_set_fault_enable_default(adev, false);

	for (i = 0; i <= 14; i++) {
		WREG32_SOC15_OFFSET(GC, 0, mmGCVM_CONTEXT1_PAGE_TABLE_START_ADDR_LO32,
				    i * hub->ctx_addr_distance, ~0);
		WREG32_SOC15_OFFSET(GC, 0, mmGCVM_CONTEXT1_PAGE_TABLE_START_ADDR_HI32,
				    i * hub->ctx_addr_distance, ~0);
		WREG32_SOC15_OFFSET(GC, 0, mmGCVM_CONTEXT1_PAGE_TABLE_END_ADDR_LO32,
				    i * hub->ctx_addr_distance,
				    0);
		WREG32_SOC15_OFFSET(GC, 0, mmGCVM_CONTEXT1_PAGE_TABLE_END_ADDR_HI32,
				    i * hub->ctx_addr_distance,
				    0);
	}
	tmp = RREG32_SOC15(GC, 0, mmGRBM_STATUS2);
	while ((tmp & (GRBM_STATUS2__EA_BUSY_MASK |
		      GRBM_STATUS2__EA_LINK_BUSY_MASK)) != 0 &&
	       time) {
		udelay(100);
		time--;
		tmp = RREG32_SOC15(GC, 0, mmGRBM_STATUS2);
	}

	if (!time) {
		DRM_WARN("failed to wait for GRBM(EA) idle\n");
	}
}

const struct amdgpu_gfxhub_funcs gfxhub_v2_1_funcs = {
	.get_fb_location = gfxhub_v2_1_get_fb_location,
	.get_mc_fb_offset = gfxhub_v2_1_get_mc_fb_offset,
	.setup_vm_pt_regs = gfxhub_v2_1_setup_vm_pt_regs,
	.gart_enable = gfxhub_v2_1_gart_enable,
	.gart_disable = gfxhub_v2_1_gart_disable,
	.set_fault_enable_default = gfxhub_v2_1_set_fault_enable_default,
	.init = gfxhub_v2_1_init,
	.get_xgmi_info = gfxhub_v2_1_get_xgmi_info,
	.utcl2_harvest = gfxhub_v2_1_utcl2_harvest,
	.mode2_save_regs = gfxhub_v2_1_save_regs,
	.mode2_restore_regs = gfxhub_v2_1_restore_regs,
	.halt = gfxhub_v2_1_halt,
};
