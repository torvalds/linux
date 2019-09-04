/*
 * Copyright 2018 Advanced Micro Devices, Inc.
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
#include "mmhub_v9_4.h"

#include "mmhub/mmhub_9_4_1_offset.h"
#include "mmhub/mmhub_9_4_1_sh_mask.h"
#include "mmhub/mmhub_9_4_1_default.h"
#include "athub/athub_1_0_offset.h"
#include "athub/athub_1_0_sh_mask.h"
#include "vega10_enum.h"

#include "soc15_common.h"

#define MMHUB_NUM_INSTANCES			2
#define MMHUB_INSTANCE_REGISTER_OFFSET		0x3000

u64 mmhub_v9_4_get_fb_location(struct amdgpu_device *adev)
{
	/* The base should be same b/t 2 mmhubs on Acrturus. Read one here. */
	u64 base = RREG32_SOC15(MMHUB, 0, mmVMSHAREDVC0_MC_VM_FB_LOCATION_BASE);
	u64 top = RREG32_SOC15(MMHUB, 0, mmVMSHAREDVC0_MC_VM_FB_LOCATION_TOP);

	base &= VMSHAREDVC0_MC_VM_FB_LOCATION_BASE__FB_BASE_MASK;
	base <<= 24;

	top &= VMSHAREDVC0_MC_VM_FB_LOCATION_TOP__FB_TOP_MASK;
	top <<= 24;

	adev->gmc.fb_start = base;
	adev->gmc.fb_end = top;

	return base;
}

void mmhub_v9_4_setup_vm_pt_regs(struct amdgpu_device *adev, int hubid,
				uint32_t vmid, uint64_t value)
{
	/* two registers distance between mmVML2VC0_VM_CONTEXT0_* to
	 * mmVML2VC0_VM_CONTEXT1_*
	 */
	int dist = mmVML2VC0_VM_CONTEXT1_PAGE_TABLE_BASE_ADDR_LO32
			- mmVML2VC0_VM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32;

	WREG32_SOC15_OFFSET(MMHUB, 0,
			    mmVML2VC0_VM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32,
			    dist * vmid + hubid * MMHUB_INSTANCE_REGISTER_OFFSET,
			    lower_32_bits(value));

	WREG32_SOC15_OFFSET(MMHUB, 0,
			    mmVML2VC0_VM_CONTEXT0_PAGE_TABLE_BASE_ADDR_HI32,
			    dist * vmid + hubid * MMHUB_INSTANCE_REGISTER_OFFSET,
			    upper_32_bits(value));

}

static void mmhub_v9_4_init_gart_aperture_regs(struct amdgpu_device *adev,
					       int hubid)
{
	uint64_t pt_base = amdgpu_gmc_pd_addr(adev->gart.bo);

	mmhub_v9_4_setup_vm_pt_regs(adev, hubid, 0, pt_base);

	WREG32_SOC15_OFFSET(MMHUB, 0,
			    mmVML2VC0_VM_CONTEXT0_PAGE_TABLE_START_ADDR_LO32,
			    hubid * MMHUB_INSTANCE_REGISTER_OFFSET,
			    (u32)(adev->gmc.gart_start >> 12));
	WREG32_SOC15_OFFSET(MMHUB, 0,
			    mmVML2VC0_VM_CONTEXT0_PAGE_TABLE_START_ADDR_HI32,
			    hubid * MMHUB_INSTANCE_REGISTER_OFFSET,
			    (u32)(adev->gmc.gart_start >> 44));

	WREG32_SOC15_OFFSET(MMHUB, 0,
			    mmVML2VC0_VM_CONTEXT0_PAGE_TABLE_END_ADDR_LO32,
			    hubid * MMHUB_INSTANCE_REGISTER_OFFSET,
			    (u32)(adev->gmc.gart_end >> 12));
	WREG32_SOC15_OFFSET(MMHUB, 0,
			    mmVML2VC0_VM_CONTEXT0_PAGE_TABLE_END_ADDR_HI32,
			    hubid * MMHUB_INSTANCE_REGISTER_OFFSET,
			    (u32)(adev->gmc.gart_end >> 44));
}

static void mmhub_v9_4_init_system_aperture_regs(struct amdgpu_device *adev,
					         int hubid)
{
	uint64_t value;
	uint32_t tmp;

	/* Program the AGP BAR */
	WREG32_SOC15_OFFSET(MMHUB, 0, mmVMSHAREDVC0_MC_VM_AGP_BASE,
			    hubid * MMHUB_INSTANCE_REGISTER_OFFSET,
			    0);
	WREG32_SOC15_OFFSET(MMHUB, 0, mmVMSHAREDVC0_MC_VM_AGP_TOP,
			    hubid * MMHUB_INSTANCE_REGISTER_OFFSET,
			    adev->gmc.agp_end >> 24);
	WREG32_SOC15_OFFSET(MMHUB, 0, mmVMSHAREDVC0_MC_VM_AGP_BOT,
			    hubid * MMHUB_INSTANCE_REGISTER_OFFSET,
			    adev->gmc.agp_start >> 24);

	/* Program the system aperture low logical page number. */
	WREG32_SOC15_OFFSET(MMHUB, 0,
			    mmVMSHAREDVC0_MC_VM_SYSTEM_APERTURE_LOW_ADDR,
			    hubid * MMHUB_INSTANCE_REGISTER_OFFSET,
			    min(adev->gmc.fb_start, adev->gmc.agp_start) >> 18);
	WREG32_SOC15_OFFSET(MMHUB, 0,
			    mmVMSHAREDVC0_MC_VM_SYSTEM_APERTURE_HIGH_ADDR,
			    hubid * MMHUB_INSTANCE_REGISTER_OFFSET,
			    max(adev->gmc.fb_end, adev->gmc.agp_end) >> 18);

	/* Set default page address. */
	value = adev->vram_scratch.gpu_addr - adev->gmc.vram_start +
		adev->vm_manager.vram_base_offset;
	WREG32_SOC15_OFFSET(MMHUB, 0,
			mmVMSHAREDPF0_MC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_LSB,
			hubid * MMHUB_INSTANCE_REGISTER_OFFSET,
			(u32)(value >> 12));
	WREG32_SOC15_OFFSET(MMHUB, 0,
			mmVMSHAREDPF0_MC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_MSB,
			hubid * MMHUB_INSTANCE_REGISTER_OFFSET,
			(u32)(value >> 44));

	/* Program "protection fault". */
	WREG32_SOC15_OFFSET(MMHUB, 0,
			    mmVML2PF0_VM_L2_PROTECTION_FAULT_DEFAULT_ADDR_LO32,
			    hubid * MMHUB_INSTANCE_REGISTER_OFFSET,
			    (u32)(adev->dummy_page_addr >> 12));
	WREG32_SOC15_OFFSET(MMHUB, 0,
			    mmVML2PF0_VM_L2_PROTECTION_FAULT_DEFAULT_ADDR_HI32,
			    hubid * MMHUB_INSTANCE_REGISTER_OFFSET,
			    (u32)((u64)adev->dummy_page_addr >> 44));

	tmp = RREG32_SOC15_OFFSET(MMHUB, 0,
				  mmVML2PF0_VM_L2_PROTECTION_FAULT_CNTL2,
				  hubid * MMHUB_INSTANCE_REGISTER_OFFSET);
	tmp = REG_SET_FIELD(tmp, VML2PF0_VM_L2_PROTECTION_FAULT_CNTL2,
			    ACTIVE_PAGE_MIGRATION_PTE_READ_RETRY, 1);
	WREG32_SOC15_OFFSET(MMHUB, 0, mmVML2PF0_VM_L2_PROTECTION_FAULT_CNTL2,
			    hubid * MMHUB_INSTANCE_REGISTER_OFFSET, tmp);
}

static void mmhub_v9_4_init_tlb_regs(struct amdgpu_device *adev, int hubid)
{
	uint32_t tmp;

	/* Setup TLB control */
	tmp = RREG32_SOC15_OFFSET(MMHUB, 0,
			   mmVMSHAREDVC0_MC_VM_MX_L1_TLB_CNTL,
			   hubid * MMHUB_INSTANCE_REGISTER_OFFSET);

	tmp = REG_SET_FIELD(tmp, VMSHAREDVC0_MC_VM_MX_L1_TLB_CNTL,
			    ENABLE_L1_TLB, 1);
	tmp = REG_SET_FIELD(tmp, VMSHAREDVC0_MC_VM_MX_L1_TLB_CNTL,
			    SYSTEM_ACCESS_MODE, 3);
	tmp = REG_SET_FIELD(tmp, VMSHAREDVC0_MC_VM_MX_L1_TLB_CNTL,
			    ENABLE_ADVANCED_DRIVER_MODEL, 1);
	tmp = REG_SET_FIELD(tmp, VMSHAREDVC0_MC_VM_MX_L1_TLB_CNTL,
			    SYSTEM_APERTURE_UNMAPPED_ACCESS, 0);
	tmp = REG_SET_FIELD(tmp, VMSHAREDVC0_MC_VM_MX_L1_TLB_CNTL,
			    ECO_BITS, 0);
	tmp = REG_SET_FIELD(tmp, VMSHAREDVC0_MC_VM_MX_L1_TLB_CNTL,
			    MTYPE, MTYPE_UC);/* XXX for emulation. */
	tmp = REG_SET_FIELD(tmp, VMSHAREDVC0_MC_VM_MX_L1_TLB_CNTL,
			    ATC_EN, 1);

	WREG32_SOC15_OFFSET(MMHUB, 0, mmVMSHAREDVC0_MC_VM_MX_L1_TLB_CNTL,
			    hubid * MMHUB_INSTANCE_REGISTER_OFFSET, tmp);
}

static void mmhub_v9_4_init_cache_regs(struct amdgpu_device *adev, int hubid)
{
	uint32_t tmp;

	/* Setup L2 cache */
	tmp = RREG32_SOC15_OFFSET(MMHUB, 0, mmVML2PF0_VM_L2_CNTL,
				  hubid * MMHUB_INSTANCE_REGISTER_OFFSET);
	tmp = REG_SET_FIELD(tmp, VML2PF0_VM_L2_CNTL,
			    ENABLE_L2_CACHE, 1);
	tmp = REG_SET_FIELD(tmp, VML2PF0_VM_L2_CNTL,
			    ENABLE_L2_FRAGMENT_PROCESSING, 1);
	/* XXX for emulation, Refer to closed source code.*/
	tmp = REG_SET_FIELD(tmp, VML2PF0_VM_L2_CNTL,
			    L2_PDE0_CACHE_TAG_GENERATION_MODE, 0);
	tmp = REG_SET_FIELD(tmp, VML2PF0_VM_L2_CNTL,
			    PDE_FAULT_CLASSIFICATION, 0);
	tmp = REG_SET_FIELD(tmp, VML2PF0_VM_L2_CNTL,
			    CONTEXT1_IDENTITY_ACCESS_MODE, 1);
	tmp = REG_SET_FIELD(tmp, VML2PF0_VM_L2_CNTL,
			    IDENTITY_MODE_FRAGMENT_SIZE, 0);
	WREG32_SOC15_OFFSET(MMHUB, 0, mmVML2PF0_VM_L2_CNTL,
		     hubid * MMHUB_INSTANCE_REGISTER_OFFSET, tmp);

	tmp = RREG32_SOC15_OFFSET(MMHUB, 0, mmVML2PF0_VM_L2_CNTL2,
				  hubid * MMHUB_INSTANCE_REGISTER_OFFSET);
	tmp = REG_SET_FIELD(tmp, VML2PF0_VM_L2_CNTL2,
			    INVALIDATE_ALL_L1_TLBS, 1);
	tmp = REG_SET_FIELD(tmp, VML2PF0_VM_L2_CNTL2,
			    INVALIDATE_L2_CACHE, 1);
	WREG32_SOC15_OFFSET(MMHUB, 0, mmVML2PF0_VM_L2_CNTL2,
			    hubid * MMHUB_INSTANCE_REGISTER_OFFSET, tmp);

	tmp = mmVML2PF0_VM_L2_CNTL3_DEFAULT;
	WREG32_SOC15_OFFSET(MMHUB, 0, mmVML2PF0_VM_L2_CNTL3,
			    hubid * MMHUB_INSTANCE_REGISTER_OFFSET, tmp);

	tmp = mmVML2PF0_VM_L2_CNTL4_DEFAULT;
	tmp = REG_SET_FIELD(tmp, VML2PF0_VM_L2_CNTL4,
			    VMC_TAP_PDE_REQUEST_PHYSICAL, 0);
	tmp = REG_SET_FIELD(tmp, VML2PF0_VM_L2_CNTL4,
			    VMC_TAP_PTE_REQUEST_PHYSICAL, 0);
	WREG32_SOC15_OFFSET(MMHUB, 0, mmVML2PF0_VM_L2_CNTL4,
			    hubid * MMHUB_INSTANCE_REGISTER_OFFSET, tmp);
}

static void mmhub_v9_4_enable_system_domain(struct amdgpu_device *adev,
					    int hubid)
{
	uint32_t tmp;

	tmp = RREG32_SOC15_OFFSET(MMHUB, 0, mmVML2VC0_VM_CONTEXT0_CNTL,
				  hubid * MMHUB_INSTANCE_REGISTER_OFFSET);
	tmp = REG_SET_FIELD(tmp, VML2VC0_VM_CONTEXT0_CNTL, ENABLE_CONTEXT, 1);
	tmp = REG_SET_FIELD(tmp, VML2VC0_VM_CONTEXT0_CNTL, PAGE_TABLE_DEPTH, 0);
	tmp = REG_SET_FIELD(tmp, VML2VC0_VM_CONTEXT0_CNTL,
			    RETRY_PERMISSION_OR_INVALID_PAGE_FAULT, 0);
	WREG32_SOC15_OFFSET(MMHUB, 0, mmVML2VC0_VM_CONTEXT0_CNTL,
			    hubid * MMHUB_INSTANCE_REGISTER_OFFSET, tmp);
}

static void mmhub_v9_4_disable_identity_aperture(struct amdgpu_device *adev,
						 int hubid)
{
	WREG32_SOC15_OFFSET(MMHUB, 0,
		    mmVML2PF0_VM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_LO32,
		    hubid * MMHUB_INSTANCE_REGISTER_OFFSET, 0XFFFFFFFF);
	WREG32_SOC15_OFFSET(MMHUB, 0,
		    mmVML2PF0_VM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_HI32,
		    hubid * MMHUB_INSTANCE_REGISTER_OFFSET, 0x0000000F);

	WREG32_SOC15_OFFSET(MMHUB, 0,
		    mmVML2PF0_VM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_LO32,
		    hubid * MMHUB_INSTANCE_REGISTER_OFFSET, 0);
	WREG32_SOC15_OFFSET(MMHUB, 0,
		    mmVML2PF0_VM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_HI32,
		    hubid * MMHUB_INSTANCE_REGISTER_OFFSET, 0);

	WREG32_SOC15_OFFSET(MMHUB, 0,
		    mmVML2PF0_VM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_LO32,
		    hubid * MMHUB_INSTANCE_REGISTER_OFFSET, 0);
	WREG32_SOC15_OFFSET(MMHUB, 0,
		    mmVML2PF0_VM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_HI32,
		    hubid * MMHUB_INSTANCE_REGISTER_OFFSET, 0);
}

static void mmhub_v9_4_setup_vmid_config(struct amdgpu_device *adev, int hubid)
{
	uint32_t tmp;
	int i;

	for (i = 0; i <= 14; i++) {
		tmp = RREG32_SOC15_OFFSET(MMHUB, 0, mmVML2VC0_VM_CONTEXT1_CNTL,
				hubid * MMHUB_INSTANCE_REGISTER_OFFSET + i);
		tmp = REG_SET_FIELD(tmp, VML2VC0_VM_CONTEXT1_CNTL,
				    ENABLE_CONTEXT, 1);
		tmp = REG_SET_FIELD(tmp, VML2VC0_VM_CONTEXT1_CNTL,
				    PAGE_TABLE_DEPTH,
				    adev->vm_manager.num_level);
		tmp = REG_SET_FIELD(tmp, VML2VC0_VM_CONTEXT1_CNTL,
				    RANGE_PROTECTION_FAULT_ENABLE_DEFAULT, 1);
		tmp = REG_SET_FIELD(tmp, VML2VC0_VM_CONTEXT1_CNTL,
				    DUMMY_PAGE_PROTECTION_FAULT_ENABLE_DEFAULT,
				    1);
		tmp = REG_SET_FIELD(tmp, VML2VC0_VM_CONTEXT1_CNTL,
				    PDE0_PROTECTION_FAULT_ENABLE_DEFAULT, 1);
		tmp = REG_SET_FIELD(tmp, VML2VC0_VM_CONTEXT1_CNTL,
				    VALID_PROTECTION_FAULT_ENABLE_DEFAULT, 1);
		tmp = REG_SET_FIELD(tmp, VML2VC0_VM_CONTEXT1_CNTL,
				    READ_PROTECTION_FAULT_ENABLE_DEFAULT, 1);
		tmp = REG_SET_FIELD(tmp, VML2VC0_VM_CONTEXT1_CNTL,
				    WRITE_PROTECTION_FAULT_ENABLE_DEFAULT, 1);
		tmp = REG_SET_FIELD(tmp, VML2VC0_VM_CONTEXT1_CNTL,
				    EXECUTE_PROTECTION_FAULT_ENABLE_DEFAULT, 1);
		tmp = REG_SET_FIELD(tmp, VML2VC0_VM_CONTEXT1_CNTL,
				    PAGE_TABLE_BLOCK_SIZE,
				    adev->vm_manager.block_size - 9);
		/* Send no-retry XNACK on fault to suppress VM fault storm. */
		tmp = REG_SET_FIELD(tmp, VML2VC0_VM_CONTEXT1_CNTL,
				    RETRY_PERMISSION_OR_INVALID_PAGE_FAULT, 0);
		WREG32_SOC15_OFFSET(MMHUB, 0, mmVML2VC0_VM_CONTEXT1_CNTL,
				    hubid * MMHUB_INSTANCE_REGISTER_OFFSET + i,
				    tmp);
		WREG32_SOC15_OFFSET(MMHUB, 0,
			    mmVML2VC0_VM_CONTEXT1_PAGE_TABLE_START_ADDR_LO32,
			    hubid * MMHUB_INSTANCE_REGISTER_OFFSET + i*2, 0);
		WREG32_SOC15_OFFSET(MMHUB, 0,
			    mmVML2VC0_VM_CONTEXT1_PAGE_TABLE_START_ADDR_HI32,
			    hubid * MMHUB_INSTANCE_REGISTER_OFFSET + i*2, 0);
		WREG32_SOC15_OFFSET(MMHUB, 0,
				mmVML2VC0_VM_CONTEXT1_PAGE_TABLE_END_ADDR_LO32,
				hubid * MMHUB_INSTANCE_REGISTER_OFFSET + i*2,
				lower_32_bits(adev->vm_manager.max_pfn - 1));
		WREG32_SOC15_OFFSET(MMHUB, 0,
				mmVML2VC0_VM_CONTEXT1_PAGE_TABLE_END_ADDR_HI32,
				hubid * MMHUB_INSTANCE_REGISTER_OFFSET + i*2,
				upper_32_bits(adev->vm_manager.max_pfn - 1));
	}
}

static void mmhub_v9_4_program_invalidation(struct amdgpu_device *adev,
					    int hubid)
{
	unsigned i;

	for (i = 0; i < 18; ++i) {
		WREG32_SOC15_OFFSET(MMHUB, 0,
				mmVML2VC0_VM_INVALIDATE_ENG0_ADDR_RANGE_LO32,
				hubid * MMHUB_INSTANCE_REGISTER_OFFSET + 2 * i,
				0xffffffff);
		WREG32_SOC15_OFFSET(MMHUB, 0,
				mmVML2VC0_VM_INVALIDATE_ENG0_ADDR_RANGE_HI32,
				hubid * MMHUB_INSTANCE_REGISTER_OFFSET + 2 * i,
				0x1f);
	}
}

int mmhub_v9_4_gart_enable(struct amdgpu_device *adev)
{
	int i;

	for (i = 0; i < MMHUB_NUM_INSTANCES; i++) {
		if (amdgpu_sriov_vf(adev)) {
			/*
			 * MC_VM_FB_LOCATION_BASE/TOP is NULL for VF, becuase
			 * they are VF copy registers so vbios post doesn't
			 * program them, for SRIOV driver need to program them
			 */
			WREG32_SOC15_OFFSET(MMHUB, 0,
				     mmVMSHAREDVC0_MC_VM_FB_LOCATION_BASE,
				     i * MMHUB_INSTANCE_REGISTER_OFFSET,
				     adev->gmc.vram_start >> 24);
			WREG32_SOC15_OFFSET(MMHUB, 0,
				     mmVMSHAREDVC0_MC_VM_FB_LOCATION_TOP,
				     i * MMHUB_INSTANCE_REGISTER_OFFSET,
				     adev->gmc.vram_end >> 24);
		}

		/* GART Enable. */
		mmhub_v9_4_init_gart_aperture_regs(adev, i);
		mmhub_v9_4_init_system_aperture_regs(adev, i);
		mmhub_v9_4_init_tlb_regs(adev, i);
		mmhub_v9_4_init_cache_regs(adev, i);

		mmhub_v9_4_enable_system_domain(adev, i);
		mmhub_v9_4_disable_identity_aperture(adev, i);
		mmhub_v9_4_setup_vmid_config(adev, i);
		mmhub_v9_4_program_invalidation(adev, i);
	}

	return 0;
}

void mmhub_v9_4_gart_disable(struct amdgpu_device *adev)
{
	u32 tmp;
	u32 i, j;

	for (j = 0; j < MMHUB_NUM_INSTANCES; j++) {
		/* Disable all tables */
		for (i = 0; i < 16; i++)
			WREG32_SOC15_OFFSET(MMHUB, 0,
					    mmVML2VC0_VM_CONTEXT0_CNTL,
					    j * MMHUB_INSTANCE_REGISTER_OFFSET +
					    i, 0);

		/* Setup TLB control */
		tmp = RREG32_SOC15_OFFSET(MMHUB, 0,
				   mmVMSHAREDVC0_MC_VM_MX_L1_TLB_CNTL,
				   j * MMHUB_INSTANCE_REGISTER_OFFSET);
		tmp = REG_SET_FIELD(tmp, VMSHAREDVC0_MC_VM_MX_L1_TLB_CNTL,
				    ENABLE_L1_TLB, 0);
		tmp = REG_SET_FIELD(tmp,
				    VMSHAREDVC0_MC_VM_MX_L1_TLB_CNTL,
				    ENABLE_ADVANCED_DRIVER_MODEL, 0);
		WREG32_SOC15_OFFSET(MMHUB, 0,
				    mmVMSHAREDVC0_MC_VM_MX_L1_TLB_CNTL,
				    j * MMHUB_INSTANCE_REGISTER_OFFSET, tmp);

		/* Setup L2 cache */
		tmp = RREG32_SOC15_OFFSET(MMHUB, 0, mmVML2PF0_VM_L2_CNTL,
					  j * MMHUB_INSTANCE_REGISTER_OFFSET);
		tmp = REG_SET_FIELD(tmp, VML2PF0_VM_L2_CNTL,
				    ENABLE_L2_CACHE, 0);
		WREG32_SOC15_OFFSET(MMHUB, 0, mmVML2PF0_VM_L2_CNTL,
				    j * MMHUB_INSTANCE_REGISTER_OFFSET, tmp);
		WREG32_SOC15_OFFSET(MMHUB, 0, mmVML2PF0_VM_L2_CNTL3,
				    j * MMHUB_INSTANCE_REGISTER_OFFSET, 0);
	}
}

/**
 * mmhub_v1_0_set_fault_enable_default - update GART/VM fault handling
 *
 * @adev: amdgpu_device pointer
 * @value: true redirects VM faults to the default page
 */
void mmhub_v9_4_set_fault_enable_default(struct amdgpu_device *adev, bool value)
{
	u32 tmp;
	int i;

	for (i = 0; i < MMHUB_NUM_INSTANCES; i++) {
		tmp = RREG32_SOC15_OFFSET(MMHUB, 0,
					  mmVML2PF0_VM_L2_PROTECTION_FAULT_CNTL,
					  i * MMHUB_INSTANCE_REGISTER_OFFSET);
		tmp = REG_SET_FIELD(tmp, VML2PF0_VM_L2_PROTECTION_FAULT_CNTL,
				    RANGE_PROTECTION_FAULT_ENABLE_DEFAULT,
				    value);
		tmp = REG_SET_FIELD(tmp, VML2PF0_VM_L2_PROTECTION_FAULT_CNTL,
				    PDE0_PROTECTION_FAULT_ENABLE_DEFAULT,
				    value);
		tmp = REG_SET_FIELD(tmp, VML2PF0_VM_L2_PROTECTION_FAULT_CNTL,
				    PDE1_PROTECTION_FAULT_ENABLE_DEFAULT,
				    value);
		tmp = REG_SET_FIELD(tmp, VML2PF0_VM_L2_PROTECTION_FAULT_CNTL,
				    PDE2_PROTECTION_FAULT_ENABLE_DEFAULT,
				    value);
		tmp = REG_SET_FIELD(tmp,
			    VML2PF0_VM_L2_PROTECTION_FAULT_CNTL,
			    TRANSLATE_FURTHER_PROTECTION_FAULT_ENABLE_DEFAULT,
			    value);
		tmp = REG_SET_FIELD(tmp, VML2PF0_VM_L2_PROTECTION_FAULT_CNTL,
				    NACK_PROTECTION_FAULT_ENABLE_DEFAULT,
				    value);
		tmp = REG_SET_FIELD(tmp, VML2PF0_VM_L2_PROTECTION_FAULT_CNTL,
				    DUMMY_PAGE_PROTECTION_FAULT_ENABLE_DEFAULT,
				    value);
		tmp = REG_SET_FIELD(tmp, VML2PF0_VM_L2_PROTECTION_FAULT_CNTL,
				    VALID_PROTECTION_FAULT_ENABLE_DEFAULT,
				    value);
		tmp = REG_SET_FIELD(tmp, VML2PF0_VM_L2_PROTECTION_FAULT_CNTL,
				    READ_PROTECTION_FAULT_ENABLE_DEFAULT,
				    value);
		tmp = REG_SET_FIELD(tmp, VML2PF0_VM_L2_PROTECTION_FAULT_CNTL,
				    WRITE_PROTECTION_FAULT_ENABLE_DEFAULT,
				    value);
		tmp = REG_SET_FIELD(tmp, VML2PF0_VM_L2_PROTECTION_FAULT_CNTL,
				    EXECUTE_PROTECTION_FAULT_ENABLE_DEFAULT,
				    value);
		if (!value) {
			tmp = REG_SET_FIELD(tmp,
					    VML2PF0_VM_L2_PROTECTION_FAULT_CNTL,
					    CRASH_ON_NO_RETRY_FAULT, 1);
			tmp = REG_SET_FIELD(tmp,
					    VML2PF0_VM_L2_PROTECTION_FAULT_CNTL,
					    CRASH_ON_RETRY_FAULT, 1);
		}

		WREG32_SOC15_OFFSET(MMHUB, 0,
				    mmVML2PF0_VM_L2_PROTECTION_FAULT_CNTL,
				    i * MMHUB_INSTANCE_REGISTER_OFFSET, tmp);
	}
}

void mmhub_v9_4_init(struct amdgpu_device *adev)
{
	struct amdgpu_vmhub *hub[MMHUB_NUM_INSTANCES] =
		{&adev->vmhub[AMDGPU_MMHUB_0], &adev->vmhub[AMDGPU_MMHUB_1]};
	int i;

	for (i = 0; i < MMHUB_NUM_INSTANCES; i++) {
		hub[i]->ctx0_ptb_addr_lo32 =
			SOC15_REG_OFFSET(MMHUB, 0,
			    mmVML2VC0_VM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32) +
			    i * MMHUB_INSTANCE_REGISTER_OFFSET;
		hub[i]->ctx0_ptb_addr_hi32 =
			SOC15_REG_OFFSET(MMHUB, 0,
			    mmVML2VC0_VM_CONTEXT0_PAGE_TABLE_BASE_ADDR_HI32) +
			    i * MMHUB_INSTANCE_REGISTER_OFFSET;
		hub[i]->vm_inv_eng0_req =
			SOC15_REG_OFFSET(MMHUB, 0,
					 mmVML2VC0_VM_INVALIDATE_ENG0_REQ) +
					 i * MMHUB_INSTANCE_REGISTER_OFFSET;
		hub[i]->vm_inv_eng0_ack =
			SOC15_REG_OFFSET(MMHUB, 0,
					 mmVML2VC0_VM_INVALIDATE_ENG0_ACK) +
					 i * MMHUB_INSTANCE_REGISTER_OFFSET;
		hub[i]->vm_context0_cntl =
			SOC15_REG_OFFSET(MMHUB, 0,
					 mmVML2VC0_VM_CONTEXT0_CNTL) +
					 i * MMHUB_INSTANCE_REGISTER_OFFSET;
		hub[i]->vm_l2_pro_fault_status =
			SOC15_REG_OFFSET(MMHUB, 0,
				    mmVML2PF0_VM_L2_PROTECTION_FAULT_STATUS) +
				    i * MMHUB_INSTANCE_REGISTER_OFFSET;
		hub[i]->vm_l2_pro_fault_cntl =
			SOC15_REG_OFFSET(MMHUB, 0,
				    mmVML2PF0_VM_L2_PROTECTION_FAULT_CNTL) +
				    i * MMHUB_INSTANCE_REGISTER_OFFSET;
	}
}

static void mmhub_v9_4_update_medium_grain_clock_gating(struct amdgpu_device *adev,
							bool enable)
{
	uint32_t def, data, def1, data1;
	int i, j;
	int dist = mmDAGB1_CNTL_MISC2 - mmDAGB0_CNTL_MISC2;

	for (i = 0; i < MMHUB_NUM_INSTANCES; i++) {
		def = data = RREG32_SOC15_OFFSET(MMHUB, 0,
					mmATCL2_0_ATC_L2_MISC_CG,
					i * MMHUB_INSTANCE_REGISTER_OFFSET);

		if (enable && (adev->cg_flags & AMD_CG_SUPPORT_MC_MGCG))
			data |= ATCL2_0_ATC_L2_MISC_CG__ENABLE_MASK;
		else
			data &= ~ATCL2_0_ATC_L2_MISC_CG__ENABLE_MASK;

		if (def != data)
			WREG32_SOC15_OFFSET(MMHUB, 0, mmATCL2_0_ATC_L2_MISC_CG,
				i * MMHUB_INSTANCE_REGISTER_OFFSET, data);

		for (j = 0; j < 5; j++) {
			def1 = data1 = RREG32_SOC15_OFFSET(MMHUB, 0,
					mmDAGB0_CNTL_MISC2,
					i * MMHUB_INSTANCE_REGISTER_OFFSET +
					j * dist);
			if (enable &&
			    (adev->cg_flags & AMD_CG_SUPPORT_MC_MGCG)) {
				data1 &=
				    ~(DAGB0_CNTL_MISC2__DISABLE_WRREQ_CG_MASK |
				    DAGB0_CNTL_MISC2__DISABLE_WRRET_CG_MASK |
				    DAGB0_CNTL_MISC2__DISABLE_RDREQ_CG_MASK |
				    DAGB0_CNTL_MISC2__DISABLE_RDRET_CG_MASK |
				    DAGB0_CNTL_MISC2__DISABLE_TLBWR_CG_MASK |
				    DAGB0_CNTL_MISC2__DISABLE_TLBRD_CG_MASK);
			} else {
				data1 |=
				    (DAGB0_CNTL_MISC2__DISABLE_WRREQ_CG_MASK |
				    DAGB0_CNTL_MISC2__DISABLE_WRRET_CG_MASK |
				    DAGB0_CNTL_MISC2__DISABLE_RDREQ_CG_MASK |
				    DAGB0_CNTL_MISC2__DISABLE_RDRET_CG_MASK |
				    DAGB0_CNTL_MISC2__DISABLE_TLBWR_CG_MASK |
				    DAGB0_CNTL_MISC2__DISABLE_TLBRD_CG_MASK);
			}

			if (def1 != data1)
				WREG32_SOC15_OFFSET(MMHUB, 0,
					mmDAGB0_CNTL_MISC2,
					i * MMHUB_INSTANCE_REGISTER_OFFSET +
					j * dist, data1);

			if (i == 1 && j == 3)
				break;
		}
	}
}

static void mmhub_v9_4_update_medium_grain_light_sleep(struct amdgpu_device *adev,
						       bool enable)
{
	uint32_t def, data;
	int i;

	for (i = 0; i < MMHUB_NUM_INSTANCES; i++) {
		def = data = RREG32_SOC15_OFFSET(MMHUB, 0,
					mmATCL2_0_ATC_L2_MISC_CG,
					i * MMHUB_INSTANCE_REGISTER_OFFSET);

		if (enable && (adev->cg_flags & AMD_CG_SUPPORT_MC_LS))
			data |= ATCL2_0_ATC_L2_MISC_CG__MEM_LS_ENABLE_MASK;
		else
			data &= ~ATCL2_0_ATC_L2_MISC_CG__MEM_LS_ENABLE_MASK;

		if (def != data)
			WREG32_SOC15_OFFSET(MMHUB, 0, mmATCL2_0_ATC_L2_MISC_CG,
				i * MMHUB_INSTANCE_REGISTER_OFFSET, data);
	}
}

int mmhub_v9_4_set_clockgating(struct amdgpu_device *adev,
			       enum amd_clockgating_state state)
{
	if (amdgpu_sriov_vf(adev))
		return 0;

	switch (adev->asic_type) {
	case CHIP_ARCTURUS:
		mmhub_v9_4_update_medium_grain_clock_gating(adev,
				state == AMD_CG_STATE_GATE ? true : false);
		mmhub_v9_4_update_medium_grain_light_sleep(adev,
				state == AMD_CG_STATE_GATE ? true : false);
		break;
	default:
		break;
	}

	return 0;
}

void mmhub_v9_4_get_clockgating(struct amdgpu_device *adev, u32 *flags)
{
	int data, data1;

	if (amdgpu_sriov_vf(adev))
		*flags = 0;

	/* AMD_CG_SUPPORT_MC_MGCG */
	data = RREG32_SOC15(MMHUB, 0, mmATCL2_0_ATC_L2_MISC_CG);

	data1 = RREG32_SOC15(MMHUB, 0, mmATCL2_0_ATC_L2_MISC_CG);

	if ((data & ATCL2_0_ATC_L2_MISC_CG__ENABLE_MASK) &&
	    !(data1 & (DAGB0_CNTL_MISC2__DISABLE_WRREQ_CG_MASK |
		       DAGB0_CNTL_MISC2__DISABLE_WRRET_CG_MASK |
		       DAGB0_CNTL_MISC2__DISABLE_RDREQ_CG_MASK |
		       DAGB0_CNTL_MISC2__DISABLE_RDRET_CG_MASK |
		       DAGB0_CNTL_MISC2__DISABLE_TLBWR_CG_MASK |
		       DAGB0_CNTL_MISC2__DISABLE_TLBRD_CG_MASK)))
		*flags |= AMD_CG_SUPPORT_MC_MGCG;

	/* AMD_CG_SUPPORT_MC_LS */
	if (data & ATCL2_0_ATC_L2_MISC_CG__MEM_LS_ENABLE_MASK)
		*flags |= AMD_CG_SUPPORT_MC_LS;
}
