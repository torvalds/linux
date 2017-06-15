/*
 * Copyright 2014 Advanced Micro Devices, Inc.
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
#include <drm/drmP.h>
#include "amdgpu.h"
#include "gmc_v6_0.h"
#include "amdgpu_ucode.h"

#include "bif/bif_3_0_d.h"
#include "bif/bif_3_0_sh_mask.h"
#include "oss/oss_1_0_d.h"
#include "oss/oss_1_0_sh_mask.h"
#include "gmc/gmc_6_0_d.h"
#include "gmc/gmc_6_0_sh_mask.h"
#include "dce/dce_6_0_d.h"
#include "dce/dce_6_0_sh_mask.h"
#include "si_enums.h"

static void gmc_v6_0_set_gart_funcs(struct amdgpu_device *adev);
static void gmc_v6_0_set_irq_funcs(struct amdgpu_device *adev);
static int gmc_v6_0_wait_for_idle(void *handle);

MODULE_FIRMWARE("radeon/tahiti_mc.bin");
MODULE_FIRMWARE("radeon/pitcairn_mc.bin");
MODULE_FIRMWARE("radeon/verde_mc.bin");
MODULE_FIRMWARE("radeon/oland_mc.bin");
MODULE_FIRMWARE("radeon/si58_mc.bin");

#define MC_SEQ_MISC0__MT__MASK   0xf0000000
#define MC_SEQ_MISC0__MT__GDDR1  0x10000000
#define MC_SEQ_MISC0__MT__DDR2   0x20000000
#define MC_SEQ_MISC0__MT__GDDR3  0x30000000
#define MC_SEQ_MISC0__MT__GDDR4  0x40000000
#define MC_SEQ_MISC0__MT__GDDR5  0x50000000
#define MC_SEQ_MISC0__MT__HBM    0x60000000
#define MC_SEQ_MISC0__MT__DDR3   0xB0000000


static const u32 crtc_offsets[6] =
{
	SI_CRTC0_REGISTER_OFFSET,
	SI_CRTC1_REGISTER_OFFSET,
	SI_CRTC2_REGISTER_OFFSET,
	SI_CRTC3_REGISTER_OFFSET,
	SI_CRTC4_REGISTER_OFFSET,
	SI_CRTC5_REGISTER_OFFSET
};

static void gmc_v6_0_mc_stop(struct amdgpu_device *adev,
			     struct amdgpu_mode_mc_save *save)
{
	u32 blackout;

	if (adev->mode_info.num_crtc)
		amdgpu_display_stop_mc_access(adev, save);

	gmc_v6_0_wait_for_idle((void *)adev);

	blackout = RREG32(mmMC_SHARED_BLACKOUT_CNTL);
	if (REG_GET_FIELD(blackout, MC_SHARED_BLACKOUT_CNTL, BLACKOUT_MODE) != 1) {
		/* Block CPU access */
		WREG32(mmBIF_FB_EN, 0);
		/* blackout the MC */
		blackout = REG_SET_FIELD(blackout,
					 MC_SHARED_BLACKOUT_CNTL, BLACKOUT_MODE, 0);
		WREG32(mmMC_SHARED_BLACKOUT_CNTL, blackout | 1);
	}
	/* wait for the MC to settle */
	udelay(100);

}

static void gmc_v6_0_mc_resume(struct amdgpu_device *adev,
			       struct amdgpu_mode_mc_save *save)
{
	u32 tmp;

	/* unblackout the MC */
	tmp = RREG32(mmMC_SHARED_BLACKOUT_CNTL);
	tmp = REG_SET_FIELD(tmp, MC_SHARED_BLACKOUT_CNTL, BLACKOUT_MODE, 0);
	WREG32(mmMC_SHARED_BLACKOUT_CNTL, tmp);
	/* allow CPU access */
	tmp = REG_SET_FIELD(0, BIF_FB_EN, FB_READ_EN, 1);
	tmp = REG_SET_FIELD(tmp, BIF_FB_EN, FB_WRITE_EN, 1);
	WREG32(mmBIF_FB_EN, tmp);

	if (adev->mode_info.num_crtc)
		amdgpu_display_resume_mc_access(adev, save);

}

static int gmc_v6_0_init_microcode(struct amdgpu_device *adev)
{
	const char *chip_name;
	char fw_name[30];
	int err;
	bool is_58_fw = false;

	DRM_DEBUG("\n");

	switch (adev->asic_type) {
	case CHIP_TAHITI:
		chip_name = "tahiti";
		break;
	case CHIP_PITCAIRN:
		chip_name = "pitcairn";
		break;
	case CHIP_VERDE:
		chip_name = "verde";
		break;
	case CHIP_OLAND:
		chip_name = "oland";
		break;
	case CHIP_HAINAN:
		chip_name = "hainan";
		break;
	default: BUG();
	}

	/* this memory configuration requires special firmware */
	if (((RREG32(mmMC_SEQ_MISC0) & 0xff000000) >> 24) == 0x58)
		is_58_fw = true;

	if (is_58_fw)
		snprintf(fw_name, sizeof(fw_name), "radeon/si58_mc.bin");
	else
		snprintf(fw_name, sizeof(fw_name), "radeon/%s_mc.bin", chip_name);
	err = request_firmware(&adev->mc.fw, fw_name, adev->dev);
	if (err)
		goto out;

	err = amdgpu_ucode_validate(adev->mc.fw);

out:
	if (err) {
		dev_err(adev->dev,
		       "si_mc: Failed to load firmware \"%s\"\n",
		       fw_name);
		release_firmware(adev->mc.fw);
		adev->mc.fw = NULL;
	}
	return err;
}

static int gmc_v6_0_mc_load_microcode(struct amdgpu_device *adev)
{
	const __le32 *new_fw_data = NULL;
	u32 running;
	const __le32 *new_io_mc_regs = NULL;
	int i, regs_size, ucode_size;
	const struct mc_firmware_header_v1_0 *hdr;

	if (!adev->mc.fw)
		return -EINVAL;

	hdr = (const struct mc_firmware_header_v1_0 *)adev->mc.fw->data;

	amdgpu_ucode_print_mc_hdr(&hdr->header);

	adev->mc.fw_version = le32_to_cpu(hdr->header.ucode_version);
	regs_size = le32_to_cpu(hdr->io_debug_size_bytes) / (4 * 2);
	new_io_mc_regs = (const __le32 *)
		(adev->mc.fw->data + le32_to_cpu(hdr->io_debug_array_offset_bytes));
	ucode_size = le32_to_cpu(hdr->header.ucode_size_bytes) / 4;
	new_fw_data = (const __le32 *)
		(adev->mc.fw->data + le32_to_cpu(hdr->header.ucode_array_offset_bytes));

	running = RREG32(mmMC_SEQ_SUP_CNTL) & MC_SEQ_SUP_CNTL__RUN_MASK;

	if (running == 0) {

		/* reset the engine and set to writable */
		WREG32(mmMC_SEQ_SUP_CNTL, 0x00000008);
		WREG32(mmMC_SEQ_SUP_CNTL, 0x00000010);

		/* load mc io regs */
		for (i = 0; i < regs_size; i++) {
			WREG32(mmMC_SEQ_IO_DEBUG_INDEX, le32_to_cpup(new_io_mc_regs++));
			WREG32(mmMC_SEQ_IO_DEBUG_DATA, le32_to_cpup(new_io_mc_regs++));
		}
		/* load the MC ucode */
		for (i = 0; i < ucode_size; i++) {
			WREG32(mmMC_SEQ_SUP_PGM, le32_to_cpup(new_fw_data++));
		}

		/* put the engine back into the active state */
		WREG32(mmMC_SEQ_SUP_CNTL, 0x00000008);
		WREG32(mmMC_SEQ_SUP_CNTL, 0x00000004);
		WREG32(mmMC_SEQ_SUP_CNTL, 0x00000001);

		/* wait for training to complete */
		for (i = 0; i < adev->usec_timeout; i++) {
			if (RREG32(mmMC_SEQ_TRAIN_WAKEUP_CNTL) & MC_SEQ_TRAIN_WAKEUP_CNTL__TRAIN_DONE_D0_MASK)
				break;
			udelay(1);
		}
		for (i = 0; i < adev->usec_timeout; i++) {
			if (RREG32(mmMC_SEQ_TRAIN_WAKEUP_CNTL) & MC_SEQ_TRAIN_WAKEUP_CNTL__TRAIN_DONE_D1_MASK)
				break;
			udelay(1);
		}

	}

	return 0;
}

static void gmc_v6_0_vram_gtt_location(struct amdgpu_device *adev,
				       struct amdgpu_mc *mc)
{
	if (mc->mc_vram_size > 0xFFC0000000ULL) {
		dev_warn(adev->dev, "limiting VRAM\n");
		mc->real_vram_size = 0xFFC0000000ULL;
		mc->mc_vram_size = 0xFFC0000000ULL;
	}
	amdgpu_vram_location(adev, &adev->mc, 0);
	adev->mc.gtt_base_align = 0;
	amdgpu_gtt_location(adev, mc);
}

static void gmc_v6_0_mc_program(struct amdgpu_device *adev)
{
	struct amdgpu_mode_mc_save save;
	u32 tmp;
	int i, j;

	/* Initialize HDP */
	for (i = 0, j = 0; i < 32; i++, j += 0x6) {
		WREG32((0xb05 + j), 0x00000000);
		WREG32((0xb06 + j), 0x00000000);
		WREG32((0xb07 + j), 0x00000000);
		WREG32((0xb08 + j), 0x00000000);
		WREG32((0xb09 + j), 0x00000000);
	}
	WREG32(mmHDP_REG_COHERENCY_FLUSH_CNTL, 0);

	if (adev->mode_info.num_crtc)
		amdgpu_display_set_vga_render_state(adev, false);

	gmc_v6_0_mc_stop(adev, &save);

	if (gmc_v6_0_wait_for_idle((void *)adev)) {
		dev_warn(adev->dev, "Wait for MC idle timedout !\n");
	}

	WREG32(mmVGA_HDP_CONTROL, VGA_HDP_CONTROL__VGA_MEMORY_DISABLE_MASK);
	/* Update configuration */
	WREG32(mmMC_VM_SYSTEM_APERTURE_LOW_ADDR,
	       adev->mc.vram_start >> 12);
	WREG32(mmMC_VM_SYSTEM_APERTURE_HIGH_ADDR,
	       adev->mc.vram_end >> 12);
	WREG32(mmMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR,
	       adev->vram_scratch.gpu_addr >> 12);
	tmp = ((adev->mc.vram_end >> 24) & 0xFFFF) << 16;
	tmp |= ((adev->mc.vram_start >> 24) & 0xFFFF);
	WREG32(mmMC_VM_FB_LOCATION, tmp);
	/* XXX double check these! */
	WREG32(mmHDP_NONSURFACE_BASE, (adev->mc.vram_start >> 8));
	WREG32(mmHDP_NONSURFACE_INFO, (2 << 7) | (1 << 30));
	WREG32(mmHDP_NONSURFACE_SIZE, 0x3FFFFFFF);
	WREG32(mmMC_VM_AGP_BASE, 0);
	WREG32(mmMC_VM_AGP_TOP, 0x0FFFFFFF);
	WREG32(mmMC_VM_AGP_BOT, 0x0FFFFFFF);

	if (gmc_v6_0_wait_for_idle((void *)adev)) {
		dev_warn(adev->dev, "Wait for MC idle timedout !\n");
	}
	gmc_v6_0_mc_resume(adev, &save);
}

static int gmc_v6_0_mc_init(struct amdgpu_device *adev)
{

	u32 tmp;
	int chansize, numchan;

	tmp = RREG32(mmMC_ARB_RAMCFG);
	if (tmp & (1 << 11)) {
		chansize = 16;
	} else if (tmp & MC_ARB_RAMCFG__CHANSIZE_MASK) {
		chansize = 64;
	} else {
		chansize = 32;
	}
	tmp = RREG32(mmMC_SHARED_CHMAP);
	switch ((tmp & MC_SHARED_CHMAP__NOOFCHAN_MASK) >> MC_SHARED_CHMAP__NOOFCHAN__SHIFT) {
	case 0:
	default:
		numchan = 1;
		break;
	case 1:
		numchan = 2;
		break;
	case 2:
		numchan = 4;
		break;
	case 3:
		numchan = 8;
		break;
	case 4:
		numchan = 3;
		break;
	case 5:
		numchan = 6;
		break;
	case 6:
		numchan = 10;
		break;
	case 7:
		numchan = 12;
		break;
	case 8:
		numchan = 16;
		break;
	}
	adev->mc.vram_width = numchan * chansize;
	/* Could aper size report 0 ? */
	adev->mc.aper_base = pci_resource_start(adev->pdev, 0);
	adev->mc.aper_size = pci_resource_len(adev->pdev, 0);
	/* size in MB on si */
	adev->mc.mc_vram_size = RREG32(mmCONFIG_MEMSIZE) * 1024ULL * 1024ULL;
	adev->mc.real_vram_size = RREG32(mmCONFIG_MEMSIZE) * 1024ULL * 1024ULL;
	adev->mc.visible_vram_size = adev->mc.aper_size;

	/* unless the user had overridden it, set the gart
	 * size equal to the 1024 or vram, whichever is larger.
	 */
	if (amdgpu_gart_size == -1)
		adev->mc.gtt_size = max((AMDGPU_DEFAULT_GTT_SIZE_MB << 20),
					adev->mc.mc_vram_size);
	else
		adev->mc.gtt_size = (uint64_t)amdgpu_gart_size << 20;

	gmc_v6_0_vram_gtt_location(adev, &adev->mc);

	return 0;
}

static void gmc_v6_0_gart_flush_gpu_tlb(struct amdgpu_device *adev,
					uint32_t vmid)
{
	WREG32(mmHDP_MEM_COHERENCY_FLUSH_CNTL, 0);

	WREG32(mmVM_INVALIDATE_REQUEST, 1 << vmid);
}

static int gmc_v6_0_gart_set_pte_pde(struct amdgpu_device *adev,
				     void *cpu_pt_addr,
				     uint32_t gpu_page_idx,
				     uint64_t addr,
				     uint64_t flags)
{
	void __iomem *ptr = (void *)cpu_pt_addr;
	uint64_t value;

	value = addr & 0xFFFFFFFFFFFFF000ULL;
	value |= flags;
	writeq(value, ptr + (gpu_page_idx * 8));

	return 0;
}

static uint64_t gmc_v6_0_get_vm_pte_flags(struct amdgpu_device *adev,
					  uint32_t flags)
{
	uint64_t pte_flag = 0;

	if (flags & AMDGPU_VM_PAGE_READABLE)
		pte_flag |= AMDGPU_PTE_READABLE;
	if (flags & AMDGPU_VM_PAGE_WRITEABLE)
		pte_flag |= AMDGPU_PTE_WRITEABLE;
	if (flags & AMDGPU_VM_PAGE_PRT)
		pte_flag |= AMDGPU_PTE_PRT;

	return pte_flag;
}

static uint64_t gmc_v6_0_get_vm_pde(struct amdgpu_device *adev, uint64_t addr)
{
	BUG_ON(addr & 0xFFFFFF0000000FFFULL);
	return addr;
}

static void gmc_v6_0_set_fault_enable_default(struct amdgpu_device *adev,
					      bool value)
{
	u32 tmp;

	tmp = RREG32(mmVM_CONTEXT1_CNTL);
	tmp = REG_SET_FIELD(tmp, VM_CONTEXT1_CNTL,
			    RANGE_PROTECTION_FAULT_ENABLE_DEFAULT, value);
	tmp = REG_SET_FIELD(tmp, VM_CONTEXT1_CNTL,
			    DUMMY_PAGE_PROTECTION_FAULT_ENABLE_DEFAULT, value);
	tmp = REG_SET_FIELD(tmp, VM_CONTEXT1_CNTL,
			    PDE0_PROTECTION_FAULT_ENABLE_DEFAULT, value);
	tmp = REG_SET_FIELD(tmp, VM_CONTEXT1_CNTL,
			    VALID_PROTECTION_FAULT_ENABLE_DEFAULT, value);
	tmp = REG_SET_FIELD(tmp, VM_CONTEXT1_CNTL,
			    READ_PROTECTION_FAULT_ENABLE_DEFAULT, value);
	tmp = REG_SET_FIELD(tmp, VM_CONTEXT1_CNTL,
			    WRITE_PROTECTION_FAULT_ENABLE_DEFAULT, value);
	WREG32(mmVM_CONTEXT1_CNTL, tmp);
}

 /**
   + * gmc_v8_0_set_prt - set PRT VM fault
   + *
   + * @adev: amdgpu_device pointer
   + * @enable: enable/disable VM fault handling for PRT
   +*/
static void gmc_v6_0_set_prt(struct amdgpu_device *adev, bool enable)
{
	u32 tmp;

	if (enable && !adev->mc.prt_warning) {
		dev_warn(adev->dev, "Disabling VM faults because of PRT request!\n");
		adev->mc.prt_warning = true;
	}

	tmp = RREG32(mmVM_PRT_CNTL);
	tmp = REG_SET_FIELD(tmp, VM_PRT_CNTL,
			    CB_DISABLE_FAULT_ON_UNMAPPED_ACCESS,
			    enable);
	tmp = REG_SET_FIELD(tmp, VM_PRT_CNTL,
			    TC_DISABLE_FAULT_ON_UNMAPPED_ACCESS,
			    enable);
	tmp = REG_SET_FIELD(tmp, VM_PRT_CNTL,
			    L2_CACHE_STORE_INVALID_ENTRIES,
			    enable);
	tmp = REG_SET_FIELD(tmp, VM_PRT_CNTL,
			    L1_TLB_STORE_INVALID_ENTRIES,
			    enable);
	WREG32(mmVM_PRT_CNTL, tmp);

	if (enable) {
		uint32_t low = AMDGPU_VA_RESERVED_SIZE >> AMDGPU_GPU_PAGE_SHIFT;
		uint32_t high = adev->vm_manager.max_pfn;

		WREG32(mmVM_PRT_APERTURE0_LOW_ADDR, low);
		WREG32(mmVM_PRT_APERTURE1_LOW_ADDR, low);
		WREG32(mmVM_PRT_APERTURE2_LOW_ADDR, low);
		WREG32(mmVM_PRT_APERTURE3_LOW_ADDR, low);
		WREG32(mmVM_PRT_APERTURE0_HIGH_ADDR, high);
		WREG32(mmVM_PRT_APERTURE1_HIGH_ADDR, high);
		WREG32(mmVM_PRT_APERTURE2_HIGH_ADDR, high);
		WREG32(mmVM_PRT_APERTURE3_HIGH_ADDR, high);
	} else {
		WREG32(mmVM_PRT_APERTURE0_LOW_ADDR, 0xfffffff);
		WREG32(mmVM_PRT_APERTURE1_LOW_ADDR, 0xfffffff);
		WREG32(mmVM_PRT_APERTURE2_LOW_ADDR, 0xfffffff);
		WREG32(mmVM_PRT_APERTURE3_LOW_ADDR, 0xfffffff);
		WREG32(mmVM_PRT_APERTURE0_HIGH_ADDR, 0x0);
		WREG32(mmVM_PRT_APERTURE1_HIGH_ADDR, 0x0);
		WREG32(mmVM_PRT_APERTURE2_HIGH_ADDR, 0x0);
		WREG32(mmVM_PRT_APERTURE3_HIGH_ADDR, 0x0);
	}
}

static int gmc_v6_0_gart_enable(struct amdgpu_device *adev)
{
	int r, i;

	if (adev->gart.robj == NULL) {
		dev_err(adev->dev, "No VRAM object for PCIE GART.\n");
		return -EINVAL;
	}
	r = amdgpu_gart_table_vram_pin(adev);
	if (r)
		return r;
	/* Setup TLB control */
	WREG32(mmMC_VM_MX_L1_TLB_CNTL,
	       (0xA << 7) |
	       MC_VM_MX_L1_TLB_CNTL__ENABLE_L1_TLB_MASK |
	       MC_VM_MX_L1_TLB_CNTL__ENABLE_L1_FRAGMENT_PROCESSING_MASK |
	       MC_VM_MX_L1_TLB_CNTL__SYSTEM_ACCESS_MODE_MASK |
	       MC_VM_MX_L1_TLB_CNTL__ENABLE_ADVANCED_DRIVER_MODEL_MASK |
	       (0UL << MC_VM_MX_L1_TLB_CNTL__SYSTEM_APERTURE_UNMAPPED_ACCESS__SHIFT));
	/* Setup L2 cache */
	WREG32(mmVM_L2_CNTL,
	       VM_L2_CNTL__ENABLE_L2_CACHE_MASK |
	       VM_L2_CNTL__ENABLE_L2_FRAGMENT_PROCESSING_MASK |
	       VM_L2_CNTL__ENABLE_L2_PTE_CACHE_LRU_UPDATE_BY_WRITE_MASK |
	       VM_L2_CNTL__ENABLE_L2_PDE0_CACHE_LRU_UPDATE_BY_WRITE_MASK |
	       (7UL << VM_L2_CNTL__EFFECTIVE_L2_QUEUE_SIZE__SHIFT) |
	       (1UL << VM_L2_CNTL__CONTEXT1_IDENTITY_ACCESS_MODE__SHIFT));
	WREG32(mmVM_L2_CNTL2,
	       VM_L2_CNTL2__INVALIDATE_ALL_L1_TLBS_MASK |
	       VM_L2_CNTL2__INVALIDATE_L2_CACHE_MASK);
	WREG32(mmVM_L2_CNTL3,
	       VM_L2_CNTL3__L2_CACHE_BIGK_ASSOCIATIVITY_MASK |
	       (4UL << VM_L2_CNTL3__BANK_SELECT__SHIFT) |
	       (4UL << VM_L2_CNTL3__L2_CACHE_BIGK_FRAGMENT_SIZE__SHIFT));
	/* setup context0 */
	WREG32(mmVM_CONTEXT0_PAGE_TABLE_START_ADDR, adev->mc.gtt_start >> 12);
	WREG32(mmVM_CONTEXT0_PAGE_TABLE_END_ADDR, adev->mc.gtt_end >> 12);
	WREG32(mmVM_CONTEXT0_PAGE_TABLE_BASE_ADDR, adev->gart.table_addr >> 12);
	WREG32(mmVM_CONTEXT0_PROTECTION_FAULT_DEFAULT_ADDR,
			(u32)(adev->dummy_page.addr >> 12));
	WREG32(mmVM_CONTEXT0_CNTL2, 0);
	WREG32(mmVM_CONTEXT0_CNTL,
	       VM_CONTEXT0_CNTL__ENABLE_CONTEXT_MASK |
	       (0UL << VM_CONTEXT0_CNTL__PAGE_TABLE_DEPTH__SHIFT) |
	       VM_CONTEXT0_CNTL__RANGE_PROTECTION_FAULT_ENABLE_DEFAULT_MASK);

	WREG32(0x575, 0);
	WREG32(0x576, 0);
	WREG32(0x577, 0);

	/* empty context1-15 */
	/* set vm size, must be a multiple of 4 */
	WREG32(mmVM_CONTEXT1_PAGE_TABLE_START_ADDR, 0);
	WREG32(mmVM_CONTEXT1_PAGE_TABLE_END_ADDR, adev->vm_manager.max_pfn - 1);
	/* Assign the pt base to something valid for now; the pts used for
	 * the VMs are determined by the application and setup and assigned
	 * on the fly in the vm part of radeon_gart.c
	 */
	for (i = 1; i < 16; i++) {
		if (i < 8)
			WREG32(mmVM_CONTEXT0_PAGE_TABLE_BASE_ADDR + i,
			       adev->gart.table_addr >> 12);
		else
			WREG32(mmVM_CONTEXT8_PAGE_TABLE_BASE_ADDR + i - 8,
			       adev->gart.table_addr >> 12);
	}

	/* enable context1-15 */
	WREG32(mmVM_CONTEXT1_PROTECTION_FAULT_DEFAULT_ADDR,
	       (u32)(adev->dummy_page.addr >> 12));
	WREG32(mmVM_CONTEXT1_CNTL2, 4);
	WREG32(mmVM_CONTEXT1_CNTL,
	       VM_CONTEXT1_CNTL__ENABLE_CONTEXT_MASK |
	       (1UL << VM_CONTEXT1_CNTL__PAGE_TABLE_DEPTH__SHIFT) |
	       ((adev->vm_manager.block_size - 9)
	       << VM_CONTEXT1_CNTL__PAGE_TABLE_BLOCK_SIZE__SHIFT));
	if (amdgpu_vm_fault_stop == AMDGPU_VM_FAULT_STOP_ALWAYS)
		gmc_v6_0_set_fault_enable_default(adev, false);
	else
		gmc_v6_0_set_fault_enable_default(adev, true);

	gmc_v6_0_gart_flush_gpu_tlb(adev, 0);
	dev_info(adev->dev, "PCIE GART of %uM enabled (table at 0x%016llX).\n",
		 (unsigned)(adev->mc.gtt_size >> 20),
		 (unsigned long long)adev->gart.table_addr);
	adev->gart.ready = true;
	return 0;
}

static int gmc_v6_0_gart_init(struct amdgpu_device *adev)
{
	int r;

	if (adev->gart.robj) {
		dev_warn(adev->dev, "gmc_v6_0 PCIE GART already initialized\n");
		return 0;
	}
	r = amdgpu_gart_init(adev);
	if (r)
		return r;
	adev->gart.table_size = adev->gart.num_gpu_pages * 8;
	adev->gart.gart_pte_flags = 0;
	return amdgpu_gart_table_vram_alloc(adev);
}

static void gmc_v6_0_gart_disable(struct amdgpu_device *adev)
{
	/*unsigned i;

	for (i = 1; i < 16; ++i) {
		uint32_t reg;
		if (i < 8)
			reg = VM_CONTEXT0_PAGE_TABLE_BASE_ADDR + i ;
		else
			reg = VM_CONTEXT8_PAGE_TABLE_BASE_ADDR + (i - 8);
		adev->vm_manager.saved_table_addr[i] = RREG32(reg);
	}*/

	/* Disable all tables */
	WREG32(mmVM_CONTEXT0_CNTL, 0);
	WREG32(mmVM_CONTEXT1_CNTL, 0);
	/* Setup TLB control */
	WREG32(mmMC_VM_MX_L1_TLB_CNTL,
	       MC_VM_MX_L1_TLB_CNTL__SYSTEM_ACCESS_MODE_MASK |
	       (0UL << MC_VM_MX_L1_TLB_CNTL__SYSTEM_APERTURE_UNMAPPED_ACCESS__SHIFT));
	/* Setup L2 cache */
	WREG32(mmVM_L2_CNTL,
	       VM_L2_CNTL__ENABLE_L2_PTE_CACHE_LRU_UPDATE_BY_WRITE_MASK |
	       VM_L2_CNTL__ENABLE_L2_PDE0_CACHE_LRU_UPDATE_BY_WRITE_MASK |
	       (7UL << VM_L2_CNTL__EFFECTIVE_L2_QUEUE_SIZE__SHIFT) |
	       (1UL << VM_L2_CNTL__CONTEXT1_IDENTITY_ACCESS_MODE__SHIFT));
	WREG32(mmVM_L2_CNTL2, 0);
	WREG32(mmVM_L2_CNTL3,
	       VM_L2_CNTL3__L2_CACHE_BIGK_ASSOCIATIVITY_MASK |
	       (0UL << VM_L2_CNTL3__L2_CACHE_BIGK_FRAGMENT_SIZE__SHIFT));
	amdgpu_gart_table_vram_unpin(adev);
}

static void gmc_v6_0_gart_fini(struct amdgpu_device *adev)
{
	amdgpu_gart_table_vram_free(adev);
	amdgpu_gart_fini(adev);
}

static void gmc_v6_0_vm_decode_fault(struct amdgpu_device *adev,
				     u32 status, u32 addr, u32 mc_client)
{
	u32 mc_id;
	u32 vmid = REG_GET_FIELD(status, VM_CONTEXT1_PROTECTION_FAULT_STATUS, VMID);
	u32 protections = REG_GET_FIELD(status, VM_CONTEXT1_PROTECTION_FAULT_STATUS,
					PROTECTIONS);
	char block[5] = { mc_client >> 24, (mc_client >> 16) & 0xff,
		(mc_client >> 8) & 0xff, mc_client & 0xff, 0 };

	mc_id = REG_GET_FIELD(status, VM_CONTEXT1_PROTECTION_FAULT_STATUS,
			      MEMORY_CLIENT_ID);

	dev_err(adev->dev, "VM fault (0x%02x, vmid %d) at page %u, %s from '%s' (0x%08x) (%d)\n",
	       protections, vmid, addr,
	       REG_GET_FIELD(status, VM_CONTEXT1_PROTECTION_FAULT_STATUS,
			     MEMORY_CLIENT_RW) ?
	       "write" : "read", block, mc_client, mc_id);
}

/*
static const u32 mc_cg_registers[] = {
	MC_HUB_MISC_HUB_CG,
	MC_HUB_MISC_SIP_CG,
	MC_HUB_MISC_VM_CG,
	MC_XPB_CLK_GAT,
	ATC_MISC_CG,
	MC_CITF_MISC_WR_CG,
	MC_CITF_MISC_RD_CG,
	MC_CITF_MISC_VM_CG,
	VM_L2_CG,
};

static const u32 mc_cg_ls_en[] = {
	MC_HUB_MISC_HUB_CG__MEM_LS_ENABLE_MASK,
	MC_HUB_MISC_SIP_CG__MEM_LS_ENABLE_MASK,
	MC_HUB_MISC_VM_CG__MEM_LS_ENABLE_MASK,
	MC_XPB_CLK_GAT__MEM_LS_ENABLE_MASK,
	ATC_MISC_CG__MEM_LS_ENABLE_MASK,
	MC_CITF_MISC_WR_CG__MEM_LS_ENABLE_MASK,
	MC_CITF_MISC_RD_CG__MEM_LS_ENABLE_MASK,
	MC_CITF_MISC_VM_CG__MEM_LS_ENABLE_MASK,
	VM_L2_CG__MEM_LS_ENABLE_MASK,
};

static const u32 mc_cg_en[] = {
	MC_HUB_MISC_HUB_CG__ENABLE_MASK,
	MC_HUB_MISC_SIP_CG__ENABLE_MASK,
	MC_HUB_MISC_VM_CG__ENABLE_MASK,
	MC_XPB_CLK_GAT__ENABLE_MASK,
	ATC_MISC_CG__ENABLE_MASK,
	MC_CITF_MISC_WR_CG__ENABLE_MASK,
	MC_CITF_MISC_RD_CG__ENABLE_MASK,
	MC_CITF_MISC_VM_CG__ENABLE_MASK,
	VM_L2_CG__ENABLE_MASK,
};

static void gmc_v6_0_enable_mc_ls(struct amdgpu_device *adev,
				  bool enable)
{
	int i;
	u32 orig, data;

	for (i = 0; i < ARRAY_SIZE(mc_cg_registers); i++) {
		orig = data = RREG32(mc_cg_registers[i]);
		if (enable && (adev->cg_flags & AMDGPU_CG_SUPPORT_MC_LS))
			data |= mc_cg_ls_en[i];
		else
			data &= ~mc_cg_ls_en[i];
		if (data != orig)
			WREG32(mc_cg_registers[i], data);
	}
}

static void gmc_v6_0_enable_mc_mgcg(struct amdgpu_device *adev,
				    bool enable)
{
	int i;
	u32 orig, data;

	for (i = 0; i < ARRAY_SIZE(mc_cg_registers); i++) {
		orig = data = RREG32(mc_cg_registers[i]);
		if (enable && (adev->cg_flags & AMDGPU_CG_SUPPORT_MC_MGCG))
			data |= mc_cg_en[i];
		else
			data &= ~mc_cg_en[i];
		if (data != orig)
			WREG32(mc_cg_registers[i], data);
	}
}

static void gmc_v6_0_enable_bif_mgls(struct amdgpu_device *adev,
				     bool enable)
{
	u32 orig, data;

	orig = data = RREG32_PCIE(ixPCIE_CNTL2);

	if (enable && (adev->cg_flags & AMDGPU_CG_SUPPORT_BIF_LS)) {
		data = REG_SET_FIELD(data, PCIE_CNTL2, SLV_MEM_LS_EN, 1);
		data = REG_SET_FIELD(data, PCIE_CNTL2, MST_MEM_LS_EN, 1);
		data = REG_SET_FIELD(data, PCIE_CNTL2, REPLAY_MEM_LS_EN, 1);
		data = REG_SET_FIELD(data, PCIE_CNTL2, SLV_MEM_AGGRESSIVE_LS_EN, 1);
	} else {
		data = REG_SET_FIELD(data, PCIE_CNTL2, SLV_MEM_LS_EN, 0);
		data = REG_SET_FIELD(data, PCIE_CNTL2, MST_MEM_LS_EN, 0);
		data = REG_SET_FIELD(data, PCIE_CNTL2, REPLAY_MEM_LS_EN, 0);
		data = REG_SET_FIELD(data, PCIE_CNTL2, SLV_MEM_AGGRESSIVE_LS_EN, 0);
	}

	if (orig != data)
		WREG32_PCIE(ixPCIE_CNTL2, data);
}

static void gmc_v6_0_enable_hdp_mgcg(struct amdgpu_device *adev,
				     bool enable)
{
	u32 orig, data;

	orig = data = RREG32(mmHDP_HOST_PATH_CNTL);

	if (enable && (adev->cg_flags & AMDGPU_CG_SUPPORT_HDP_MGCG))
		data = REG_SET_FIELD(data, HDP_HOST_PATH_CNTL, CLOCK_GATING_DIS, 0);
	else
		data = REG_SET_FIELD(data, HDP_HOST_PATH_CNTL, CLOCK_GATING_DIS, 1);

	if (orig != data)
		WREG32(mmHDP_HOST_PATH_CNTL, data);
}

static void gmc_v6_0_enable_hdp_ls(struct amdgpu_device *adev,
				   bool enable)
{
	u32 orig, data;

	orig = data = RREG32(mmHDP_MEM_POWER_LS);

	if (enable && (adev->cg_flags & AMDGPU_CG_SUPPORT_HDP_LS))
		data = REG_SET_FIELD(data, HDP_MEM_POWER_LS, LS_ENABLE, 1);
	else
		data = REG_SET_FIELD(data, HDP_MEM_POWER_LS, LS_ENABLE, 0);

	if (orig != data)
		WREG32(mmHDP_MEM_POWER_LS, data);
}
*/

static int gmc_v6_0_convert_vram_type(int mc_seq_vram_type)
{
	switch (mc_seq_vram_type) {
	case MC_SEQ_MISC0__MT__GDDR1:
		return AMDGPU_VRAM_TYPE_GDDR1;
	case MC_SEQ_MISC0__MT__DDR2:
		return AMDGPU_VRAM_TYPE_DDR2;
	case MC_SEQ_MISC0__MT__GDDR3:
		return AMDGPU_VRAM_TYPE_GDDR3;
	case MC_SEQ_MISC0__MT__GDDR4:
		return AMDGPU_VRAM_TYPE_GDDR4;
	case MC_SEQ_MISC0__MT__GDDR5:
		return AMDGPU_VRAM_TYPE_GDDR5;
	case MC_SEQ_MISC0__MT__DDR3:
		return AMDGPU_VRAM_TYPE_DDR3;
	default:
		return AMDGPU_VRAM_TYPE_UNKNOWN;
	}
}

static int gmc_v6_0_early_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	gmc_v6_0_set_gart_funcs(adev);
	gmc_v6_0_set_irq_funcs(adev);

	if (adev->flags & AMD_IS_APU) {
		adev->mc.vram_type = AMDGPU_VRAM_TYPE_UNKNOWN;
	} else {
		u32 tmp = RREG32(mmMC_SEQ_MISC0);
		tmp &= MC_SEQ_MISC0__MT__MASK;
		adev->mc.vram_type = gmc_v6_0_convert_vram_type(tmp);
	}

	return 0;
}

static int gmc_v6_0_late_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (amdgpu_vm_fault_stop != AMDGPU_VM_FAULT_STOP_ALWAYS)
		return amdgpu_irq_get(adev, &adev->mc.vm_fault, 0);
	else
		return 0;
}

static int gmc_v6_0_sw_init(void *handle)
{
	int r;
	int dma_bits;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	r = amdgpu_irq_add_id(adev, AMDGPU_IH_CLIENTID_LEGACY, 146, &adev->mc.vm_fault);
	if (r)
		return r;

	r = amdgpu_irq_add_id(adev, AMDGPU_IH_CLIENTID_LEGACY, 147, &adev->mc.vm_fault);
	if (r)
		return r;

	amdgpu_vm_adjust_size(adev, 64);
	adev->vm_manager.max_pfn = adev->vm_manager.vm_size << 18;

	adev->mc.mc_mask = 0xffffffffffULL;

	adev->mc.stolen_size = 256 * 1024;

	adev->need_dma32 = false;
	dma_bits = adev->need_dma32 ? 32 : 40;
	r = pci_set_dma_mask(adev->pdev, DMA_BIT_MASK(dma_bits));
	if (r) {
		adev->need_dma32 = true;
		dma_bits = 32;
		dev_warn(adev->dev, "amdgpu: No suitable DMA available.\n");
	}
	r = pci_set_consistent_dma_mask(adev->pdev, DMA_BIT_MASK(dma_bits));
	if (r) {
		pci_set_consistent_dma_mask(adev->pdev, DMA_BIT_MASK(32));
		dev_warn(adev->dev, "amdgpu: No coherent DMA available.\n");
	}

	r = gmc_v6_0_init_microcode(adev);
	if (r) {
		dev_err(adev->dev, "Failed to load mc firmware!\n");
		return r;
	}

	r = gmc_v6_0_mc_init(adev);
	if (r)
		return r;

	r = amdgpu_bo_init(adev);
	if (r)
		return r;

	r = gmc_v6_0_gart_init(adev);
	if (r)
		return r;

	/*
	 * number of VMs
	 * VMID 0 is reserved for System
	 * amdgpu graphics/compute will use VMIDs 1-7
	 * amdkfd will use VMIDs 8-15
	 */
	adev->vm_manager.id_mgr[0].num_ids = AMDGPU_NUM_OF_VMIDS;
	adev->vm_manager.num_level = 1;
	amdgpu_vm_manager_init(adev);

	/* base offset of vram pages */
	if (adev->flags & AMD_IS_APU) {
		u64 tmp = RREG32(mmMC_VM_FB_OFFSET);

		tmp <<= 22;
		adev->vm_manager.vram_base_offset = tmp;
	} else {
		adev->vm_manager.vram_base_offset = 0;
	}

	return 0;
}

static int gmc_v6_0_sw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	amdgpu_vm_manager_fini(adev);
	gmc_v6_0_gart_fini(adev);
	amdgpu_gem_force_release(adev);
	amdgpu_bo_fini(adev);

	return 0;
}

static int gmc_v6_0_hw_init(void *handle)
{
	int r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	gmc_v6_0_mc_program(adev);

	if (!(adev->flags & AMD_IS_APU)) {
		r = gmc_v6_0_mc_load_microcode(adev);
		if (r) {
			dev_err(adev->dev, "Failed to load MC firmware!\n");
			return r;
		}
	}

	r = gmc_v6_0_gart_enable(adev);
	if (r)
		return r;

	return r;
}

static int gmc_v6_0_hw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	amdgpu_irq_put(adev, &adev->mc.vm_fault, 0);
	gmc_v6_0_gart_disable(adev);

	return 0;
}

static int gmc_v6_0_suspend(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	gmc_v6_0_hw_fini(adev);

	return 0;
}

static int gmc_v6_0_resume(void *handle)
{
	int r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	r = gmc_v6_0_hw_init(adev);
	if (r)
		return r;

	amdgpu_vm_reset_all_ids(adev);

	return 0;
}

static bool gmc_v6_0_is_idle(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	u32 tmp = RREG32(mmSRBM_STATUS);

	if (tmp & (SRBM_STATUS__MCB_BUSY_MASK | SRBM_STATUS__MCB_NON_DISPLAY_BUSY_MASK |
		   SRBM_STATUS__MCC_BUSY_MASK | SRBM_STATUS__MCD_BUSY_MASK | SRBM_STATUS__VMC_BUSY_MASK))
		return false;

	return true;
}

static int gmc_v6_0_wait_for_idle(void *handle)
{
	unsigned i;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	for (i = 0; i < adev->usec_timeout; i++) {
		if (gmc_v6_0_is_idle(handle))
			return 0;
		udelay(1);
	}
	return -ETIMEDOUT;

}

static int gmc_v6_0_soft_reset(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct amdgpu_mode_mc_save save;
	u32 srbm_soft_reset = 0;
	u32 tmp = RREG32(mmSRBM_STATUS);

	if (tmp & SRBM_STATUS__VMC_BUSY_MASK)
		srbm_soft_reset = REG_SET_FIELD(srbm_soft_reset,
						SRBM_SOFT_RESET, SOFT_RESET_VMC, 1);

	if (tmp & (SRBM_STATUS__MCB_BUSY_MASK | SRBM_STATUS__MCB_NON_DISPLAY_BUSY_MASK |
		   SRBM_STATUS__MCC_BUSY_MASK | SRBM_STATUS__MCD_BUSY_MASK)) {
		if (!(adev->flags & AMD_IS_APU))
			srbm_soft_reset = REG_SET_FIELD(srbm_soft_reset,
							SRBM_SOFT_RESET, SOFT_RESET_MC, 1);
	}

	if (srbm_soft_reset) {
		gmc_v6_0_mc_stop(adev, &save);
		if (gmc_v6_0_wait_for_idle(adev)) {
			dev_warn(adev->dev, "Wait for GMC idle timed out !\n");
		}


		tmp = RREG32(mmSRBM_SOFT_RESET);
		tmp |= srbm_soft_reset;
		dev_info(adev->dev, "SRBM_SOFT_RESET=0x%08X\n", tmp);
		WREG32(mmSRBM_SOFT_RESET, tmp);
		tmp = RREG32(mmSRBM_SOFT_RESET);

		udelay(50);

		tmp &= ~srbm_soft_reset;
		WREG32(mmSRBM_SOFT_RESET, tmp);
		tmp = RREG32(mmSRBM_SOFT_RESET);

		udelay(50);

		gmc_v6_0_mc_resume(adev, &save);
		udelay(50);
	}

	return 0;
}

static int gmc_v6_0_vm_fault_interrupt_state(struct amdgpu_device *adev,
					     struct amdgpu_irq_src *src,
					     unsigned type,
					     enum amdgpu_interrupt_state state)
{
	u32 tmp;
	u32 bits = (VM_CONTEXT1_CNTL__RANGE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK |
		    VM_CONTEXT1_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK |
		    VM_CONTEXT1_CNTL__PDE0_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK |
		    VM_CONTEXT1_CNTL__VALID_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK |
		    VM_CONTEXT1_CNTL__READ_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK |
		    VM_CONTEXT1_CNTL__WRITE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK);

	switch (state) {
	case AMDGPU_IRQ_STATE_DISABLE:
		tmp = RREG32(mmVM_CONTEXT0_CNTL);
		tmp &= ~bits;
		WREG32(mmVM_CONTEXT0_CNTL, tmp);
		tmp = RREG32(mmVM_CONTEXT1_CNTL);
		tmp &= ~bits;
		WREG32(mmVM_CONTEXT1_CNTL, tmp);
		break;
	case AMDGPU_IRQ_STATE_ENABLE:
		tmp = RREG32(mmVM_CONTEXT0_CNTL);
		tmp |= bits;
		WREG32(mmVM_CONTEXT0_CNTL, tmp);
		tmp = RREG32(mmVM_CONTEXT1_CNTL);
		tmp |= bits;
		WREG32(mmVM_CONTEXT1_CNTL, tmp);
		break;
	default:
		break;
	}

	return 0;
}

static int gmc_v6_0_process_interrupt(struct amdgpu_device *adev,
				      struct amdgpu_irq_src *source,
				      struct amdgpu_iv_entry *entry)
{
	u32 addr, status;

	addr = RREG32(mmVM_CONTEXT1_PROTECTION_FAULT_ADDR);
	status = RREG32(mmVM_CONTEXT1_PROTECTION_FAULT_STATUS);
	WREG32_P(mmVM_CONTEXT1_CNTL2, 1, ~1);

	if (!addr && !status)
		return 0;

	if (amdgpu_vm_fault_stop == AMDGPU_VM_FAULT_STOP_FIRST)
		gmc_v6_0_set_fault_enable_default(adev, false);

	if (printk_ratelimit()) {
		dev_err(adev->dev, "GPU fault detected: %d 0x%08x\n",
			entry->src_id, entry->src_data[0]);
		dev_err(adev->dev, "  VM_CONTEXT1_PROTECTION_FAULT_ADDR   0x%08X\n",
			addr);
		dev_err(adev->dev, "  VM_CONTEXT1_PROTECTION_FAULT_STATUS 0x%08X\n",
			status);
		gmc_v6_0_vm_decode_fault(adev, status, addr, 0);
	}

	return 0;
}

static int gmc_v6_0_set_clockgating_state(void *handle,
					  enum amd_clockgating_state state)
{
	return 0;
}

static int gmc_v6_0_set_powergating_state(void *handle,
					  enum amd_powergating_state state)
{
	return 0;
}

static const struct amd_ip_funcs gmc_v6_0_ip_funcs = {
	.name = "gmc_v6_0",
	.early_init = gmc_v6_0_early_init,
	.late_init = gmc_v6_0_late_init,
	.sw_init = gmc_v6_0_sw_init,
	.sw_fini = gmc_v6_0_sw_fini,
	.hw_init = gmc_v6_0_hw_init,
	.hw_fini = gmc_v6_0_hw_fini,
	.suspend = gmc_v6_0_suspend,
	.resume = gmc_v6_0_resume,
	.is_idle = gmc_v6_0_is_idle,
	.wait_for_idle = gmc_v6_0_wait_for_idle,
	.soft_reset = gmc_v6_0_soft_reset,
	.set_clockgating_state = gmc_v6_0_set_clockgating_state,
	.set_powergating_state = gmc_v6_0_set_powergating_state,
};

static const struct amdgpu_gart_funcs gmc_v6_0_gart_funcs = {
	.flush_gpu_tlb = gmc_v6_0_gart_flush_gpu_tlb,
	.set_pte_pde = gmc_v6_0_gart_set_pte_pde,
	.set_prt = gmc_v6_0_set_prt,
	.get_vm_pde = gmc_v6_0_get_vm_pde,
	.get_vm_pte_flags = gmc_v6_0_get_vm_pte_flags
};

static const struct amdgpu_irq_src_funcs gmc_v6_0_irq_funcs = {
	.set = gmc_v6_0_vm_fault_interrupt_state,
	.process = gmc_v6_0_process_interrupt,
};

static void gmc_v6_0_set_gart_funcs(struct amdgpu_device *adev)
{
	if (adev->gart.gart_funcs == NULL)
		adev->gart.gart_funcs = &gmc_v6_0_gart_funcs;
}

static void gmc_v6_0_set_irq_funcs(struct amdgpu_device *adev)
{
	adev->mc.vm_fault.num_types = 1;
	adev->mc.vm_fault.funcs = &gmc_v6_0_irq_funcs;
}

const struct amdgpu_ip_block_version gmc_v6_0_ip_block =
{
	.type = AMD_IP_BLOCK_TYPE_GMC,
	.major = 6,
	.minor = 0,
	.rev = 0,
	.funcs = &gmc_v6_0_ip_funcs,
};
