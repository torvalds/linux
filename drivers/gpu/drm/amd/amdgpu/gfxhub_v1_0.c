/*
 * Copyright 2016 Advanced Micro Devices, Inc.
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
#include "gfxhub_v1_0.h"

#include "vega10/soc15ip.h"
#include "vega10/GC/gc_9_0_offset.h"
#include "vega10/GC/gc_9_0_sh_mask.h"
#include "vega10/GC/gc_9_0_default.h"
#include "vega10/vega10_enum.h"

#include "soc15_common.h"

u64 gfxhub_v1_0_get_mc_fb_offset(struct amdgpu_device *adev)
{
	return (u64)RREG32(SOC15_REG_OFFSET(GC, 0, mmMC_VM_FB_OFFSET)) << 24;
}

static void gfxhub_v1_0_init_gart_pt_regs(struct amdgpu_device *adev)
{
	uint64_t value;

	BUG_ON(adev->gart.table_addr & (~0x0000FFFFFFFFF000ULL));
	value = adev->gart.table_addr - adev->mc.vram_start
		+ adev->vm_manager.vram_base_offset;
	value &= 0x0000FFFFFFFFF000ULL;
	value |= 0x1; /*valid bit*/

	WREG32(SOC15_REG_OFFSET(GC, 0,
				mmVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32),
	       lower_32_bits(value));

	WREG32(SOC15_REG_OFFSET(GC, 0,
				mmVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_HI32),
	       upper_32_bits(value));
}

static void gfxhub_v1_0_init_gart_aperture_regs(struct amdgpu_device *adev)
{
	gfxhub_v1_0_init_gart_pt_regs(adev);

	WREG32(SOC15_REG_OFFSET(GC, 0,
				mmVM_CONTEXT0_PAGE_TABLE_START_ADDR_LO32),
		(u32)(adev->mc.gtt_start >> 12));
	WREG32(SOC15_REG_OFFSET(GC, 0,
				mmVM_CONTEXT0_PAGE_TABLE_START_ADDR_HI32),
		(u32)(adev->mc.gtt_start >> 44));

	WREG32(SOC15_REG_OFFSET(GC, 0,
				mmVM_CONTEXT0_PAGE_TABLE_END_ADDR_LO32),
		(u32)(adev->mc.gtt_end >> 12));
	WREG32(SOC15_REG_OFFSET(GC, 0,
				mmVM_CONTEXT0_PAGE_TABLE_END_ADDR_HI32),
		(u32)(adev->mc.gtt_end >> 44));
}

static void gfxhub_v1_0_init_system_aperture_regs(struct amdgpu_device *adev)
{
	uint64_t value;
	uint32_t tmp;

	/* Disable AGP. */
	WREG32(SOC15_REG_OFFSET(GC, 0, mmMC_VM_AGP_BASE), 0);
	WREG32(SOC15_REG_OFFSET(GC, 0, mmMC_VM_AGP_TOP), 0);
	WREG32(SOC15_REG_OFFSET(GC, 0, mmMC_VM_AGP_BOT), 0xFFFFFFFF);

	/* Program the system aperture low logical page number. */
	WREG32(SOC15_REG_OFFSET(GC, 0, mmMC_VM_SYSTEM_APERTURE_LOW_ADDR),
		adev->mc.vram_start >> 18);
	WREG32(SOC15_REG_OFFSET(GC, 0, mmMC_VM_SYSTEM_APERTURE_HIGH_ADDR),
		adev->mc.vram_end >> 18);

	/* Set default page address. */
	value = adev->vram_scratch.gpu_addr - adev->mc.vram_start
		+ adev->vm_manager.vram_base_offset;
	WREG32(SOC15_REG_OFFSET(GC, 0,
				mmMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_LSB),
	       (u32)(value >> 12));
	WREG32(SOC15_REG_OFFSET(GC, 0,
				mmMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_MSB),
	       (u32)(value >> 44));

	/* Program "protection fault". */
	WREG32(SOC15_REG_OFFSET(GC, 0,
				mmVM_L2_PROTECTION_FAULT_DEFAULT_ADDR_LO32),
	       (u32)(adev->dummy_page.addr >> 12));
	WREG32(SOC15_REG_OFFSET(GC, 0,
				mmVM_L2_PROTECTION_FAULT_DEFAULT_ADDR_HI32),
	       (u32)((u64)adev->dummy_page.addr >> 44));

	tmp = RREG32(SOC15_REG_OFFSET(GC, 0, mmVM_L2_PROTECTION_FAULT_CNTL2));
	tmp = REG_SET_FIELD(tmp, VM_L2_PROTECTION_FAULT_CNTL2,
			    ACTIVE_PAGE_MIGRATION_PTE_READ_RETRY, 1);
	WREG32(SOC15_REG_OFFSET(GC, 0, mmVM_L2_PROTECTION_FAULT_CNTL2), tmp);
}

static void gfxhub_v1_0_init_tlb_regs(struct amdgpu_device *adev)
{
	uint32_t tmp;

	/* Setup TLB control */
	tmp = RREG32(SOC15_REG_OFFSET(GC, 0, mmMC_VM_MX_L1_TLB_CNTL));

	tmp = REG_SET_FIELD(tmp, MC_VM_MX_L1_TLB_CNTL, ENABLE_L1_TLB, 1);
	tmp = REG_SET_FIELD(tmp, MC_VM_MX_L1_TLB_CNTL, SYSTEM_ACCESS_MODE, 3);
	tmp = REG_SET_FIELD(tmp, MC_VM_MX_L1_TLB_CNTL,
			    ENABLE_ADVANCED_DRIVER_MODEL, 1);
	tmp = REG_SET_FIELD(tmp, MC_VM_MX_L1_TLB_CNTL,
			    SYSTEM_APERTURE_UNMAPPED_ACCESS, 0);
	tmp = REG_SET_FIELD(tmp, MC_VM_MX_L1_TLB_CNTL, ECO_BITS, 0);
	tmp = REG_SET_FIELD(tmp, MC_VM_MX_L1_TLB_CNTL,
			    MTYPE, MTYPE_UC);/* XXX for emulation. */
	tmp = REG_SET_FIELD(tmp, MC_VM_MX_L1_TLB_CNTL, ATC_EN, 1);

	WREG32(SOC15_REG_OFFSET(GC, 0, mmMC_VM_MX_L1_TLB_CNTL), tmp);
}

static void gfxhub_v1_0_init_cache_regs(struct amdgpu_device *adev)
{
	uint32_t tmp;

	/* Setup L2 cache */
	tmp = RREG32(SOC15_REG_OFFSET(GC, 0, mmVM_L2_CNTL));
	tmp = REG_SET_FIELD(tmp, VM_L2_CNTL, ENABLE_L2_CACHE, 1);
	tmp = REG_SET_FIELD(tmp, VM_L2_CNTL, ENABLE_L2_FRAGMENT_PROCESSING, 0);
	/* XXX for emulation, Refer to closed source code.*/
	tmp = REG_SET_FIELD(tmp, VM_L2_CNTL, L2_PDE0_CACHE_TAG_GENERATION_MODE,
			    0);
	tmp = REG_SET_FIELD(tmp, VM_L2_CNTL, PDE_FAULT_CLASSIFICATION, 1);
	tmp = REG_SET_FIELD(tmp, VM_L2_CNTL, CONTEXT1_IDENTITY_ACCESS_MODE, 1);
	tmp = REG_SET_FIELD(tmp, VM_L2_CNTL, IDENTITY_MODE_FRAGMENT_SIZE, 0);
	WREG32(SOC15_REG_OFFSET(GC, 0, mmVM_L2_CNTL), tmp);

	tmp = RREG32(SOC15_REG_OFFSET(GC, 0, mmVM_L2_CNTL2));
	tmp = REG_SET_FIELD(tmp, VM_L2_CNTL2, INVALIDATE_ALL_L1_TLBS, 1);
	tmp = REG_SET_FIELD(tmp, VM_L2_CNTL2, INVALIDATE_L2_CACHE, 1);
	WREG32(SOC15_REG_OFFSET(GC, 0, mmVM_L2_CNTL2), tmp);

	tmp = mmVM_L2_CNTL3_DEFAULT;
	WREG32(SOC15_REG_OFFSET(GC, 0, mmVM_L2_CNTL3), tmp);

	tmp = mmVM_L2_CNTL4_DEFAULT;
	tmp = REG_SET_FIELD(tmp, VM_L2_CNTL4, VMC_TAP_PDE_REQUEST_PHYSICAL, 0);
	tmp = REG_SET_FIELD(tmp, VM_L2_CNTL4, VMC_TAP_PTE_REQUEST_PHYSICAL, 0);
	WREG32(SOC15_REG_OFFSET(GC, 0, mmVM_L2_CNTL4), tmp);
}

static void gfxhub_v1_0_enable_system_domain(struct amdgpu_device *adev)
{
	uint32_t tmp;

	tmp = RREG32(SOC15_REG_OFFSET(GC, 0, mmVM_CONTEXT0_CNTL));
	tmp = REG_SET_FIELD(tmp, VM_CONTEXT0_CNTL, ENABLE_CONTEXT, 1);
	tmp = REG_SET_FIELD(tmp, VM_CONTEXT0_CNTL, PAGE_TABLE_DEPTH, 0);
	WREG32(SOC15_REG_OFFSET(GC, 0, mmVM_CONTEXT0_CNTL), tmp);
}

static void gfxhub_v1_0_disable_identity_aperture(struct amdgpu_device *adev)
{
	WREG32(SOC15_REG_OFFSET(GC, 0,
				mmVM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_LO32),
	       0XFFFFFFFF);
	WREG32(SOC15_REG_OFFSET(GC, 0,
		mmVM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_HI32), 0x0000000F);

	WREG32(SOC15_REG_OFFSET(GC, 0,
		mmVM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_LO32), 0);
	WREG32(SOC15_REG_OFFSET(GC, 0,
		mmVM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_HI32), 0);

	WREG32(SOC15_REG_OFFSET(GC, 0,
		mmVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_LO32), 0);
	WREG32(SOC15_REG_OFFSET(GC, 0,
		mmVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_HI32), 0);

}

int gfxhub_v1_0_gart_enable(struct amdgpu_device *adev)
{
	u32 tmp;
	u32 i;

	if (amdgpu_sriov_vf(adev)) {
		/*
		 * MC_VM_FB_LOCATION_BASE/TOP is NULL for VF, becuase they are
		 * VF copy registers so vbios post doesn't program them, for
		 * SRIOV driver need to program them
		 */
		WREG32(SOC15_REG_OFFSET(GC, 0, mmMC_VM_FB_LOCATION_BASE),
				adev->mc.vram_start >> 24);
		WREG32(SOC15_REG_OFFSET(GC, 0, mmMC_VM_FB_LOCATION_TOP),
				adev->mc.vram_end >> 24);
	}

	/* GART Enable. */
	gfxhub_v1_0_init_gart_aperture_regs(adev);
	gfxhub_v1_0_init_system_aperture_regs(adev);
	gfxhub_v1_0_init_tlb_regs(adev);
	gfxhub_v1_0_init_cache_regs(adev);

	gfxhub_v1_0_enable_system_domain(adev);
	gfxhub_v1_0_disable_identity_aperture(adev);

	for (i = 0; i <= 14; i++) {
		tmp = RREG32(SOC15_REG_OFFSET(GC, 0, mmVM_CONTEXT1_CNTL) + i);
		tmp = REG_SET_FIELD(tmp, VM_CONTEXT1_CNTL, ENABLE_CONTEXT, 1);
		tmp = REG_SET_FIELD(tmp, VM_CONTEXT1_CNTL, PAGE_TABLE_DEPTH,
				    adev->vm_manager.num_level);
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
				adev->vm_manager.block_size - 9);
		WREG32(SOC15_REG_OFFSET(GC, 0, mmVM_CONTEXT1_CNTL) + i, tmp);
		WREG32(SOC15_REG_OFFSET(GC, 0, mmVM_CONTEXT1_PAGE_TABLE_START_ADDR_LO32) + i*2, 0);
		WREG32(SOC15_REG_OFFSET(GC, 0, mmVM_CONTEXT1_PAGE_TABLE_START_ADDR_HI32) + i*2, 0);
		WREG32(SOC15_REG_OFFSET(GC, 0, mmVM_CONTEXT1_PAGE_TABLE_END_ADDR_LO32) + i*2,
			lower_32_bits(adev->vm_manager.max_pfn - 1));
		WREG32(SOC15_REG_OFFSET(GC, 0, mmVM_CONTEXT1_PAGE_TABLE_END_ADDR_HI32) + i*2,
			upper_32_bits(adev->vm_manager.max_pfn - 1));
	}


	return 0;
}

void gfxhub_v1_0_gart_disable(struct amdgpu_device *adev)
{
	u32 tmp;
	u32 i;

	/* Disable all tables */
	for (i = 0; i < 16; i++)
		WREG32(SOC15_REG_OFFSET(GC, 0, mmVM_CONTEXT0_CNTL) + i, 0);

	/* Setup TLB control */
	tmp = RREG32(SOC15_REG_OFFSET(GC, 0, mmMC_VM_MX_L1_TLB_CNTL));
	tmp = REG_SET_FIELD(tmp, MC_VM_MX_L1_TLB_CNTL, ENABLE_L1_TLB, 0);
	tmp = REG_SET_FIELD(tmp,
				MC_VM_MX_L1_TLB_CNTL,
				ENABLE_ADVANCED_DRIVER_MODEL,
				0);
	WREG32(SOC15_REG_OFFSET(GC, 0, mmMC_VM_MX_L1_TLB_CNTL), tmp);

	/* Setup L2 cache */
	tmp = RREG32(SOC15_REG_OFFSET(GC, 0, mmVM_L2_CNTL));
	tmp = REG_SET_FIELD(tmp, VM_L2_CNTL, ENABLE_L2_CACHE, 0);
	WREG32(SOC15_REG_OFFSET(GC, 0, mmVM_L2_CNTL), tmp);
	WREG32(SOC15_REG_OFFSET(GC, 0, mmVM_L2_CNTL3), 0);
}

/**
 * gfxhub_v1_0_set_fault_enable_default - update GART/VM fault handling
 *
 * @adev: amdgpu_device pointer
 * @value: true redirects VM faults to the default page
 */
void gfxhub_v1_0_set_fault_enable_default(struct amdgpu_device *adev,
					  bool value)
{
	u32 tmp;
	tmp = RREG32(SOC15_REG_OFFSET(GC, 0, mmVM_L2_PROTECTION_FAULT_CNTL));
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
	WREG32(SOC15_REG_OFFSET(GC, 0, mmVM_L2_PROTECTION_FAULT_CNTL), tmp);
}

static int gfxhub_v1_0_early_init(void *handle)
{
	return 0;
}

static int gfxhub_v1_0_late_init(void *handle)
{
	return 0;
}

static int gfxhub_v1_0_sw_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct amdgpu_vmhub *hub = &adev->vmhub[AMDGPU_GFXHUB];

	hub->ctx0_ptb_addr_lo32 =
		SOC15_REG_OFFSET(GC, 0,
				 mmVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32);
	hub->ctx0_ptb_addr_hi32 =
		SOC15_REG_OFFSET(GC, 0,
				 mmVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_HI32);
	hub->vm_inv_eng0_req =
		SOC15_REG_OFFSET(GC, 0, mmVM_INVALIDATE_ENG0_REQ);
	hub->vm_inv_eng0_ack =
		SOC15_REG_OFFSET(GC, 0, mmVM_INVALIDATE_ENG0_ACK);
	hub->vm_context0_cntl =
		SOC15_REG_OFFSET(GC, 0, mmVM_CONTEXT0_CNTL);
	hub->vm_l2_pro_fault_status =
		SOC15_REG_OFFSET(GC, 0, mmVM_L2_PROTECTION_FAULT_STATUS);
	hub->vm_l2_pro_fault_cntl =
		SOC15_REG_OFFSET(GC, 0, mmVM_L2_PROTECTION_FAULT_CNTL);

	return 0;
}

static int gfxhub_v1_0_sw_fini(void *handle)
{
	return 0;
}

static int gfxhub_v1_0_hw_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	unsigned i;

	for (i = 0 ; i < 18; ++i) {
		WREG32(SOC15_REG_OFFSET(GC, 0,
					mmVM_INVALIDATE_ENG0_ADDR_RANGE_LO32) +
		       2 * i, 0xffffffff);
		WREG32(SOC15_REG_OFFSET(GC, 0,
					mmVM_INVALIDATE_ENG0_ADDR_RANGE_HI32) +
		       2 * i, 0x1f);
	}

	return 0;
}

static int gfxhub_v1_0_hw_fini(void *handle)
{
	return 0;
}

static int gfxhub_v1_0_suspend(void *handle)
{
	return 0;
}

static int gfxhub_v1_0_resume(void *handle)
{
	return 0;
}

static bool gfxhub_v1_0_is_idle(void *handle)
{
	return true;
}

static int gfxhub_v1_0_wait_for_idle(void *handle)
{
	return 0;
}

static int gfxhub_v1_0_soft_reset(void *handle)
{
	return 0;
}

static int gfxhub_v1_0_set_clockgating_state(void *handle,
					  enum amd_clockgating_state state)
{
	return 0;
}

static int gfxhub_v1_0_set_powergating_state(void *handle,
					  enum amd_powergating_state state)
{
	return 0;
}

const struct amd_ip_funcs gfxhub_v1_0_ip_funcs = {
	.name = "gfxhub_v1_0",
	.early_init = gfxhub_v1_0_early_init,
	.late_init = gfxhub_v1_0_late_init,
	.sw_init = gfxhub_v1_0_sw_init,
	.sw_fini = gfxhub_v1_0_sw_fini,
	.hw_init = gfxhub_v1_0_hw_init,
	.hw_fini = gfxhub_v1_0_hw_fini,
	.suspend = gfxhub_v1_0_suspend,
	.resume = gfxhub_v1_0_resume,
	.is_idle = gfxhub_v1_0_is_idle,
	.wait_for_idle = gfxhub_v1_0_wait_for_idle,
	.soft_reset = gfxhub_v1_0_soft_reset,
	.set_clockgating_state = gfxhub_v1_0_set_clockgating_state,
	.set_powergating_state = gfxhub_v1_0_set_powergating_state,
};

const struct amdgpu_ip_block_version gfxhub_v1_0_ip_block =
{
	.type = AMD_IP_BLOCK_TYPE_GFXHUB,
	.major = 1,
	.minor = 0,
	.rev = 0,
	.funcs = &gfxhub_v1_0_ip_funcs,
};
