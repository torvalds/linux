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
#include <linux/module.h>
#include <linux/pci.h>

#include <drm/drm_cache.h>
#include "amdgpu.h"
#include "gmc_v8_0.h"
#include "amdgpu_ucode.h"
#include "amdgpu_amdkfd.h"
#include "amdgpu_gem.h"

#include "gmc/gmc_8_1_d.h"
#include "gmc/gmc_8_1_sh_mask.h"

#include "bif/bif_5_0_d.h"
#include "bif/bif_5_0_sh_mask.h"

#include "oss/oss_3_0_d.h"
#include "oss/oss_3_0_sh_mask.h"

#include "dce/dce_10_0_d.h"
#include "dce/dce_10_0_sh_mask.h"

#include "vid.h"
#include "vi.h"

#include "amdgpu_atombios.h"

#include "ivsrcid/ivsrcid_vislands30.h"

static void gmc_v8_0_set_gmc_funcs(struct amdgpu_device *adev);
static void gmc_v8_0_set_irq_funcs(struct amdgpu_device *adev);
static int gmc_v8_0_wait_for_idle(void *handle);

MODULE_FIRMWARE("amdgpu/tonga_mc.bin");
MODULE_FIRMWARE("amdgpu/polaris11_mc.bin");
MODULE_FIRMWARE("amdgpu/polaris10_mc.bin");
MODULE_FIRMWARE("amdgpu/polaris12_mc.bin");
MODULE_FIRMWARE("amdgpu/polaris12_32_mc.bin");
MODULE_FIRMWARE("amdgpu/polaris11_k_mc.bin");
MODULE_FIRMWARE("amdgpu/polaris10_k_mc.bin");
MODULE_FIRMWARE("amdgpu/polaris12_k_mc.bin");

static const u32 golden_settings_tonga_a11[] = {
	mmMC_ARB_WTM_GRPWT_RD, 0x00000003, 0x00000000,
	mmMC_HUB_RDREQ_DMIF_LIMIT, 0x0000007f, 0x00000028,
	mmMC_HUB_WDP_UMC, 0x00007fb6, 0x00000991,
	mmVM_PRT_APERTURE0_LOW_ADDR, 0x0fffffff, 0x0fffffff,
	mmVM_PRT_APERTURE1_LOW_ADDR, 0x0fffffff, 0x0fffffff,
	mmVM_PRT_APERTURE2_LOW_ADDR, 0x0fffffff, 0x0fffffff,
	mmVM_PRT_APERTURE3_LOW_ADDR, 0x0fffffff, 0x0fffffff,
};

static const u32 tonga_mgcg_cgcg_init[] = {
	mmMC_MEM_POWER_LS, 0xffffffff, 0x00000104
};

static const u32 golden_settings_fiji_a10[] = {
	mmVM_PRT_APERTURE0_LOW_ADDR, 0x0fffffff, 0x0fffffff,
	mmVM_PRT_APERTURE1_LOW_ADDR, 0x0fffffff, 0x0fffffff,
	mmVM_PRT_APERTURE2_LOW_ADDR, 0x0fffffff, 0x0fffffff,
	mmVM_PRT_APERTURE3_LOW_ADDR, 0x0fffffff, 0x0fffffff,
};

static const u32 fiji_mgcg_cgcg_init[] = {
	mmMC_MEM_POWER_LS, 0xffffffff, 0x00000104
};

static const u32 golden_settings_polaris11_a11[] = {
	mmVM_PRT_APERTURE0_LOW_ADDR, 0x0fffffff, 0x0fffffff,
	mmVM_PRT_APERTURE1_LOW_ADDR, 0x0fffffff, 0x0fffffff,
	mmVM_PRT_APERTURE2_LOW_ADDR, 0x0fffffff, 0x0fffffff,
	mmVM_PRT_APERTURE3_LOW_ADDR, 0x0fffffff, 0x0fffffff
};

static const u32 golden_settings_polaris10_a11[] = {
	mmMC_ARB_WTM_GRPWT_RD, 0x00000003, 0x00000000,
	mmVM_PRT_APERTURE0_LOW_ADDR, 0x0fffffff, 0x0fffffff,
	mmVM_PRT_APERTURE1_LOW_ADDR, 0x0fffffff, 0x0fffffff,
	mmVM_PRT_APERTURE2_LOW_ADDR, 0x0fffffff, 0x0fffffff,
	mmVM_PRT_APERTURE3_LOW_ADDR, 0x0fffffff, 0x0fffffff
};

static const u32 cz_mgcg_cgcg_init[] = {
	mmMC_MEM_POWER_LS, 0xffffffff, 0x00000104
};

static const u32 stoney_mgcg_cgcg_init[] = {
	mmATC_MISC_CG, 0xffffffff, 0x000c0200,
	mmMC_MEM_POWER_LS, 0xffffffff, 0x00000104
};

static const u32 golden_settings_stoney_common[] = {
	mmMC_HUB_RDREQ_UVD, MC_HUB_RDREQ_UVD__PRESCALE_MASK, 0x00000004,
	mmMC_RD_GRP_OTH, MC_RD_GRP_OTH__UVD_MASK, 0x00600000
};

static void gmc_v8_0_init_golden_registers(struct amdgpu_device *adev)
{
	switch (adev->asic_type) {
	case CHIP_FIJI:
		amdgpu_device_program_register_sequence(adev,
							fiji_mgcg_cgcg_init,
							ARRAY_SIZE(fiji_mgcg_cgcg_init));
		amdgpu_device_program_register_sequence(adev,
							golden_settings_fiji_a10,
							ARRAY_SIZE(golden_settings_fiji_a10));
		break;
	case CHIP_TONGA:
		amdgpu_device_program_register_sequence(adev,
							tonga_mgcg_cgcg_init,
							ARRAY_SIZE(tonga_mgcg_cgcg_init));
		amdgpu_device_program_register_sequence(adev,
							golden_settings_tonga_a11,
							ARRAY_SIZE(golden_settings_tonga_a11));
		break;
	case CHIP_POLARIS11:
	case CHIP_POLARIS12:
	case CHIP_VEGAM:
		amdgpu_device_program_register_sequence(adev,
							golden_settings_polaris11_a11,
							ARRAY_SIZE(golden_settings_polaris11_a11));
		break;
	case CHIP_POLARIS10:
		amdgpu_device_program_register_sequence(adev,
							golden_settings_polaris10_a11,
							ARRAY_SIZE(golden_settings_polaris10_a11));
		break;
	case CHIP_CARRIZO:
		amdgpu_device_program_register_sequence(adev,
							cz_mgcg_cgcg_init,
							ARRAY_SIZE(cz_mgcg_cgcg_init));
		break;
	case CHIP_STONEY:
		amdgpu_device_program_register_sequence(adev,
							stoney_mgcg_cgcg_init,
							ARRAY_SIZE(stoney_mgcg_cgcg_init));
		amdgpu_device_program_register_sequence(adev,
							golden_settings_stoney_common,
							ARRAY_SIZE(golden_settings_stoney_common));
		break;
	default:
		break;
	}
}

static void gmc_v8_0_mc_stop(struct amdgpu_device *adev)
{
	u32 blackout;

	gmc_v8_0_wait_for_idle(adev);

	blackout = RREG32(mmMC_SHARED_BLACKOUT_CNTL);
	if (REG_GET_FIELD(blackout, MC_SHARED_BLACKOUT_CNTL, BLACKOUT_MODE) != 1) {
		/* Block CPU access */
		WREG32(mmBIF_FB_EN, 0);
		/* blackout the MC */
		blackout = REG_SET_FIELD(blackout,
					 MC_SHARED_BLACKOUT_CNTL, BLACKOUT_MODE, 1);
		WREG32(mmMC_SHARED_BLACKOUT_CNTL, blackout);
	}
	/* wait for the MC to settle */
	udelay(100);
}

static void gmc_v8_0_mc_resume(struct amdgpu_device *adev)
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
}

/**
 * gmc_v8_0_init_microcode - load ucode images from disk
 *
 * @adev: amdgpu_device pointer
 *
 * Use the firmware interface to load the ucode images into
 * the driver (not loaded into hw).
 * Returns 0 on success, error on failure.
 */
static int gmc_v8_0_init_microcode(struct amdgpu_device *adev)
{
	const char *chip_name;
	char fw_name[30];
	int err;

	DRM_DEBUG("\n");

	switch (adev->asic_type) {
	case CHIP_TONGA:
		chip_name = "tonga";
		break;
	case CHIP_POLARIS11:
		if (ASICID_IS_P21(adev->pdev->device, adev->pdev->revision) ||
		    ASICID_IS_P31(adev->pdev->device, adev->pdev->revision))
			chip_name = "polaris11_k";
		else
			chip_name = "polaris11";
		break;
	case CHIP_POLARIS10:
		if (ASICID_IS_P30(adev->pdev->device, adev->pdev->revision))
			chip_name = "polaris10_k";
		else
			chip_name = "polaris10";
		break;
	case CHIP_POLARIS12:
		if (ASICID_IS_P23(adev->pdev->device, adev->pdev->revision)) {
			chip_name = "polaris12_k";
		} else {
			WREG32(mmMC_SEQ_IO_DEBUG_INDEX, ixMC_IO_DEBUG_UP_159);
			/* Polaris12 32bit ASIC needs a special MC firmware */
			if (RREG32(mmMC_SEQ_IO_DEBUG_DATA) == 0x05b4dc40)
				chip_name = "polaris12_32";
			else
				chip_name = "polaris12";
		}
		break;
	case CHIP_FIJI:
	case CHIP_CARRIZO:
	case CHIP_STONEY:
	case CHIP_VEGAM:
		return 0;
	default:
		return -EINVAL;
	}

	snprintf(fw_name, sizeof(fw_name), "amdgpu/%s_mc.bin", chip_name);
	err = amdgpu_ucode_request(adev, &adev->gmc.fw, fw_name);
	if (err) {
		pr_err("mc: Failed to load firmware \"%s\"\n", fw_name);
		amdgpu_ucode_release(&adev->gmc.fw);
	}
	return err;
}

/**
 * gmc_v8_0_tonga_mc_load_microcode - load tonga MC ucode into the hw
 *
 * @adev: amdgpu_device pointer
 *
 * Load the GDDR MC ucode into the hw (VI).
 * Returns 0 on success, error on failure.
 */
static int gmc_v8_0_tonga_mc_load_microcode(struct amdgpu_device *adev)
{
	const struct mc_firmware_header_v1_0 *hdr;
	const __le32 *fw_data = NULL;
	const __le32 *io_mc_regs = NULL;
	u32 running;
	int i, ucode_size, regs_size;

	/* Skip MC ucode loading on SR-IOV capable boards.
	 * vbios does this for us in asic_init in that case.
	 * Skip MC ucode loading on VF, because hypervisor will do that
	 * for this adaptor.
	 */
	if (amdgpu_sriov_bios(adev))
		return 0;

	if (!adev->gmc.fw)
		return -EINVAL;

	hdr = (const struct mc_firmware_header_v1_0 *)adev->gmc.fw->data;
	amdgpu_ucode_print_mc_hdr(&hdr->header);

	adev->gmc.fw_version = le32_to_cpu(hdr->header.ucode_version);
	regs_size = le32_to_cpu(hdr->io_debug_size_bytes) / (4 * 2);
	io_mc_regs = (const __le32 *)
		(adev->gmc.fw->data + le32_to_cpu(hdr->io_debug_array_offset_bytes));
	ucode_size = le32_to_cpu(hdr->header.ucode_size_bytes) / 4;
	fw_data = (const __le32 *)
		(adev->gmc.fw->data + le32_to_cpu(hdr->header.ucode_array_offset_bytes));

	running = REG_GET_FIELD(RREG32(mmMC_SEQ_SUP_CNTL), MC_SEQ_SUP_CNTL, RUN);

	if (running == 0) {
		/* reset the engine and set to writable */
		WREG32(mmMC_SEQ_SUP_CNTL, 0x00000008);
		WREG32(mmMC_SEQ_SUP_CNTL, 0x00000010);

		/* load mc io regs */
		for (i = 0; i < regs_size; i++) {
			WREG32(mmMC_SEQ_IO_DEBUG_INDEX, le32_to_cpup(io_mc_regs++));
			WREG32(mmMC_SEQ_IO_DEBUG_DATA, le32_to_cpup(io_mc_regs++));
		}
		/* load the MC ucode */
		for (i = 0; i < ucode_size; i++)
			WREG32(mmMC_SEQ_SUP_PGM, le32_to_cpup(fw_data++));

		/* put the engine back into the active state */
		WREG32(mmMC_SEQ_SUP_CNTL, 0x00000008);
		WREG32(mmMC_SEQ_SUP_CNTL, 0x00000004);
		WREG32(mmMC_SEQ_SUP_CNTL, 0x00000001);

		/* wait for training to complete */
		for (i = 0; i < adev->usec_timeout; i++) {
			if (REG_GET_FIELD(RREG32(mmMC_SEQ_TRAIN_WAKEUP_CNTL),
					  MC_SEQ_TRAIN_WAKEUP_CNTL, TRAIN_DONE_D0))
				break;
			udelay(1);
		}
		for (i = 0; i < adev->usec_timeout; i++) {
			if (REG_GET_FIELD(RREG32(mmMC_SEQ_TRAIN_WAKEUP_CNTL),
					  MC_SEQ_TRAIN_WAKEUP_CNTL, TRAIN_DONE_D1))
				break;
			udelay(1);
		}
	}

	return 0;
}

static int gmc_v8_0_polaris_mc_load_microcode(struct amdgpu_device *adev)
{
	const struct mc_firmware_header_v1_0 *hdr;
	const __le32 *fw_data = NULL;
	const __le32 *io_mc_regs = NULL;
	u32 data;
	int i, ucode_size, regs_size;

	/* Skip MC ucode loading on SR-IOV capable boards.
	 * vbios does this for us in asic_init in that case.
	 * Skip MC ucode loading on VF, because hypervisor will do that
	 * for this adaptor.
	 */
	if (amdgpu_sriov_bios(adev))
		return 0;

	if (!adev->gmc.fw)
		return -EINVAL;

	hdr = (const struct mc_firmware_header_v1_0 *)adev->gmc.fw->data;
	amdgpu_ucode_print_mc_hdr(&hdr->header);

	adev->gmc.fw_version = le32_to_cpu(hdr->header.ucode_version);
	regs_size = le32_to_cpu(hdr->io_debug_size_bytes) / (4 * 2);
	io_mc_regs = (const __le32 *)
		(adev->gmc.fw->data + le32_to_cpu(hdr->io_debug_array_offset_bytes));
	ucode_size = le32_to_cpu(hdr->header.ucode_size_bytes) / 4;
	fw_data = (const __le32 *)
		(adev->gmc.fw->data + le32_to_cpu(hdr->header.ucode_array_offset_bytes));

	data = RREG32(mmMC_SEQ_MISC0);
	data &= ~(0x40);
	WREG32(mmMC_SEQ_MISC0, data);

	/* load mc io regs */
	for (i = 0; i < regs_size; i++) {
		WREG32(mmMC_SEQ_IO_DEBUG_INDEX, le32_to_cpup(io_mc_regs++));
		WREG32(mmMC_SEQ_IO_DEBUG_DATA, le32_to_cpup(io_mc_regs++));
	}

	WREG32(mmMC_SEQ_SUP_CNTL, 0x00000008);
	WREG32(mmMC_SEQ_SUP_CNTL, 0x00000010);

	/* load the MC ucode */
	for (i = 0; i < ucode_size; i++)
		WREG32(mmMC_SEQ_SUP_PGM, le32_to_cpup(fw_data++));

	/* put the engine back into the active state */
	WREG32(mmMC_SEQ_SUP_CNTL, 0x00000008);
	WREG32(mmMC_SEQ_SUP_CNTL, 0x00000004);
	WREG32(mmMC_SEQ_SUP_CNTL, 0x00000001);

	/* wait for training to complete */
	for (i = 0; i < adev->usec_timeout; i++) {
		data = RREG32(mmMC_SEQ_MISC0);
		if (data & 0x80)
			break;
		udelay(1);
	}

	return 0;
}

static void gmc_v8_0_vram_gtt_location(struct amdgpu_device *adev,
				       struct amdgpu_gmc *mc)
{
	u64 base = 0;

	if (!amdgpu_sriov_vf(adev))
		base = RREG32(mmMC_VM_FB_LOCATION) & 0xFFFF;
	base <<= 24;

	amdgpu_gmc_vram_location(adev, mc, base);
	amdgpu_gmc_gart_location(adev, mc, AMDGPU_GART_PLACEMENT_BEST_FIT);
}

/**
 * gmc_v8_0_mc_program - program the GPU memory controller
 *
 * @adev: amdgpu_device pointer
 *
 * Set the location of vram, gart, and AGP in the GPU's
 * physical address space (VI).
 */
static void gmc_v8_0_mc_program(struct amdgpu_device *adev)
{
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

	if (gmc_v8_0_wait_for_idle((void *)adev))
		dev_warn(adev->dev, "Wait for MC idle timedout !\n");

	if (adev->mode_info.num_crtc) {
		/* Lockout access through VGA aperture*/
		tmp = RREG32(mmVGA_HDP_CONTROL);
		tmp = REG_SET_FIELD(tmp, VGA_HDP_CONTROL, VGA_MEMORY_DISABLE, 1);
		WREG32(mmVGA_HDP_CONTROL, tmp);

		/* disable VGA render */
		tmp = RREG32(mmVGA_RENDER_CONTROL);
		tmp = REG_SET_FIELD(tmp, VGA_RENDER_CONTROL, VGA_VSTATUS_CNTL, 0);
		WREG32(mmVGA_RENDER_CONTROL, tmp);
	}
	/* Update configuration */
	WREG32(mmMC_VM_SYSTEM_APERTURE_LOW_ADDR,
	       adev->gmc.vram_start >> 12);
	WREG32(mmMC_VM_SYSTEM_APERTURE_HIGH_ADDR,
	       adev->gmc.vram_end >> 12);
	WREG32(mmMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR,
	       adev->mem_scratch.gpu_addr >> 12);

	if (amdgpu_sriov_vf(adev)) {
		tmp = ((adev->gmc.vram_end >> 24) & 0xFFFF) << 16;
		tmp |= ((adev->gmc.vram_start >> 24) & 0xFFFF);
		WREG32(mmMC_VM_FB_LOCATION, tmp);
		/* XXX double check these! */
		WREG32(mmHDP_NONSURFACE_BASE, (adev->gmc.vram_start >> 8));
		WREG32(mmHDP_NONSURFACE_INFO, (2 << 7) | (1 << 30));
		WREG32(mmHDP_NONSURFACE_SIZE, 0x3FFFFFFF);
	}

	WREG32(mmMC_VM_AGP_BASE, 0);
	WREG32(mmMC_VM_AGP_TOP, adev->gmc.agp_end >> 22);
	WREG32(mmMC_VM_AGP_BOT, adev->gmc.agp_start >> 22);
	if (gmc_v8_0_wait_for_idle((void *)adev))
		dev_warn(adev->dev, "Wait for MC idle timedout !\n");

	WREG32(mmBIF_FB_EN, BIF_FB_EN__FB_READ_EN_MASK | BIF_FB_EN__FB_WRITE_EN_MASK);

	tmp = RREG32(mmHDP_MISC_CNTL);
	tmp = REG_SET_FIELD(tmp, HDP_MISC_CNTL, FLUSH_INVALIDATE_CACHE, 0);
	WREG32(mmHDP_MISC_CNTL, tmp);

	tmp = RREG32(mmHDP_HOST_PATH_CNTL);
	WREG32(mmHDP_HOST_PATH_CNTL, tmp);
}

/**
 * gmc_v8_0_mc_init - initialize the memory controller driver params
 *
 * @adev: amdgpu_device pointer
 *
 * Look up the amount of vram, vram width, and decide how to place
 * vram and gart within the GPU's physical address space (VI).
 * Returns 0 for success.
 */
static int gmc_v8_0_mc_init(struct amdgpu_device *adev)
{
	int r;
	u32 tmp;

	adev->gmc.vram_width = amdgpu_atombios_get_vram_width(adev);
	if (!adev->gmc.vram_width) {
		int chansize, numchan;

		/* Get VRAM informations */
		tmp = RREG32(mmMC_ARB_RAMCFG);
		if (REG_GET_FIELD(tmp, MC_ARB_RAMCFG, CHANSIZE))
			chansize = 64;
		else
			chansize = 32;

		tmp = RREG32(mmMC_SHARED_CHMAP);
		switch (REG_GET_FIELD(tmp, MC_SHARED_CHMAP, NOOFCHAN)) {
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
		adev->gmc.vram_width = numchan * chansize;
	}
	/* size in MB on si */
	tmp = RREG32(mmCONFIG_MEMSIZE);
	/* some boards may have garbage in the upper 16 bits */
	if (tmp & 0xffff0000) {
		DRM_INFO("Probable bad vram size: 0x%08x\n", tmp);
		if (tmp & 0xffff)
			tmp &= 0xffff;
	}
	adev->gmc.mc_vram_size = tmp * 1024ULL * 1024ULL;
	adev->gmc.real_vram_size = adev->gmc.mc_vram_size;

	if (!(adev->flags & AMD_IS_APU)) {
		r = amdgpu_device_resize_fb_bar(adev);
		if (r)
			return r;
	}
	adev->gmc.aper_base = pci_resource_start(adev->pdev, 0);
	adev->gmc.aper_size = pci_resource_len(adev->pdev, 0);

#ifdef CONFIG_X86_64
	if ((adev->flags & AMD_IS_APU) && !amdgpu_passthrough(adev)) {
		adev->gmc.aper_base = ((u64)RREG32(mmMC_VM_FB_OFFSET)) << 22;
		adev->gmc.aper_size = adev->gmc.real_vram_size;
	}
#endif

	adev->gmc.visible_vram_size = adev->gmc.aper_size;

	/* set the gart size */
	if (amdgpu_gart_size == -1) {
		switch (adev->asic_type) {
		case CHIP_POLARIS10: /* all engines support GPUVM */
		case CHIP_POLARIS11: /* all engines support GPUVM */
		case CHIP_POLARIS12: /* all engines support GPUVM */
		case CHIP_VEGAM:     /* all engines support GPUVM */
		default:
			adev->gmc.gart_size = 256ULL << 20;
			break;
		case CHIP_TONGA:   /* UVD, VCE do not support GPUVM */
		case CHIP_FIJI:    /* UVD, VCE do not support GPUVM */
		case CHIP_CARRIZO: /* UVD, VCE do not support GPUVM, DCE SG support */
		case CHIP_STONEY:  /* UVD does not support GPUVM, DCE SG support */
			adev->gmc.gart_size = 1024ULL << 20;
			break;
		}
	} else {
		adev->gmc.gart_size = (u64)amdgpu_gart_size << 20;
	}

	adev->gmc.gart_size += adev->pm.smu_prv_buffer_size;
	gmc_v8_0_vram_gtt_location(adev, &adev->gmc);

	return 0;
}

/**
 * gmc_v8_0_flush_gpu_tlb_pasid - tlb flush via pasid
 *
 * @adev: amdgpu_device pointer
 * @pasid: pasid to be flush
 * @flush_type: type of flush
 * @all_hub: flush all hubs
 * @inst: is used to select which instance of KIQ to use for the invalidation
 *
 * Flush the TLB for the requested pasid.
 */
static void gmc_v8_0_flush_gpu_tlb_pasid(struct amdgpu_device *adev,
					 uint16_t pasid, uint32_t flush_type,
					 bool all_hub, uint32_t inst)
{
	u32 mask = 0x0;
	int vmid;

	for (vmid = 1; vmid < 16; vmid++) {
		u32 tmp = RREG32(mmATC_VMID0_PASID_MAPPING + vmid);

		if ((tmp & ATC_VMID0_PASID_MAPPING__VALID_MASK) &&
		    (tmp & ATC_VMID0_PASID_MAPPING__PASID_MASK) == pasid)
			mask |= 1 << vmid;
	}

	WREG32(mmVM_INVALIDATE_REQUEST, mask);
	RREG32(mmVM_INVALIDATE_RESPONSE);
}

/*
 * GART
 * VMID 0 is the physical GPU addresses as used by the kernel.
 * VMIDs 1-15 are used for userspace clients and are handled
 * by the amdgpu vm/hsa code.
 */

/**
 * gmc_v8_0_flush_gpu_tlb - gart tlb flush callback
 *
 * @adev: amdgpu_device pointer
 * @vmid: vm instance to flush
 * @vmhub: which hub to flush
 * @flush_type: type of flush
 *
 * Flush the TLB for the requested page table (VI).
 */
static void gmc_v8_0_flush_gpu_tlb(struct amdgpu_device *adev, uint32_t vmid,
					uint32_t vmhub, uint32_t flush_type)
{
	/* bits 0-15 are the VM contexts0-15 */
	WREG32(mmVM_INVALIDATE_REQUEST, 1 << vmid);
}

static uint64_t gmc_v8_0_emit_flush_gpu_tlb(struct amdgpu_ring *ring,
					    unsigned int vmid, uint64_t pd_addr)
{
	uint32_t reg;

	if (vmid < 8)
		reg = mmVM_CONTEXT0_PAGE_TABLE_BASE_ADDR + vmid;
	else
		reg = mmVM_CONTEXT8_PAGE_TABLE_BASE_ADDR + vmid - 8;
	amdgpu_ring_emit_wreg(ring, reg, pd_addr >> 12);

	/* bits 0-15 are the VM contexts0-15 */
	amdgpu_ring_emit_wreg(ring, mmVM_INVALIDATE_REQUEST, 1 << vmid);

	return pd_addr;
}

static void gmc_v8_0_emit_pasid_mapping(struct amdgpu_ring *ring, unsigned int vmid,
					unsigned int pasid)
{
	amdgpu_ring_emit_wreg(ring, mmIH_VMID_0_LUT + vmid, pasid);
}

/*
 * PTE format on VI:
 * 63:40 reserved
 * 39:12 4k physical page base address
 * 11:7 fragment
 * 6 write
 * 5 read
 * 4 exe
 * 3 reserved
 * 2 snooped
 * 1 system
 * 0 valid
 *
 * PDE format on VI:
 * 63:59 block fragment size
 * 58:40 reserved
 * 39:1 physical base address of PTE
 * bits 5:1 must be 0.
 * 0 valid
 */

static void gmc_v8_0_get_vm_pde(struct amdgpu_device *adev, int level,
				uint64_t *addr, uint64_t *flags)
{
	BUG_ON(*addr & 0xFFFFFF0000000FFFULL);
}

static void gmc_v8_0_get_vm_pte(struct amdgpu_device *adev,
				struct amdgpu_bo_va_mapping *mapping,
				uint64_t *flags)
{
	*flags &= ~AMDGPU_PTE_EXECUTABLE;
	*flags |= mapping->flags & AMDGPU_PTE_EXECUTABLE;
	*flags &= ~AMDGPU_PTE_PRT;
}

/**
 * gmc_v8_0_set_fault_enable_default - update VM fault handling
 *
 * @adev: amdgpu_device pointer
 * @value: true redirects VM faults to the default page
 */
static void gmc_v8_0_set_fault_enable_default(struct amdgpu_device *adev,
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
	tmp = REG_SET_FIELD(tmp, VM_CONTEXT1_CNTL,
			    EXECUTE_PROTECTION_FAULT_ENABLE_DEFAULT, value);
	WREG32(mmVM_CONTEXT1_CNTL, tmp);
}

/**
 * gmc_v8_0_set_prt() - set PRT VM fault
 *
 * @adev: amdgpu_device pointer
 * @enable: enable/disable VM fault handling for PRT
 */
static void gmc_v8_0_set_prt(struct amdgpu_device *adev, bool enable)
{
	u32 tmp;

	if (enable && !adev->gmc.prt_warning) {
		dev_warn(adev->dev, "Disabling VM faults because of PRT request!\n");
		adev->gmc.prt_warning = true;
	}

	tmp = RREG32(mmVM_PRT_CNTL);
	tmp = REG_SET_FIELD(tmp, VM_PRT_CNTL,
			    CB_DISABLE_READ_FAULT_ON_UNMAPPED_ACCESS, enable);
	tmp = REG_SET_FIELD(tmp, VM_PRT_CNTL,
			    CB_DISABLE_WRITE_FAULT_ON_UNMAPPED_ACCESS, enable);
	tmp = REG_SET_FIELD(tmp, VM_PRT_CNTL,
			    TC_DISABLE_READ_FAULT_ON_UNMAPPED_ACCESS, enable);
	tmp = REG_SET_FIELD(tmp, VM_PRT_CNTL,
			    TC_DISABLE_WRITE_FAULT_ON_UNMAPPED_ACCESS, enable);
	tmp = REG_SET_FIELD(tmp, VM_PRT_CNTL,
			    L2_CACHE_STORE_INVALID_ENTRIES, enable);
	tmp = REG_SET_FIELD(tmp, VM_PRT_CNTL,
			    L1_TLB_STORE_INVALID_ENTRIES, enable);
	tmp = REG_SET_FIELD(tmp, VM_PRT_CNTL,
			    MASK_PDE0_FAULT, enable);
	WREG32(mmVM_PRT_CNTL, tmp);

	if (enable) {
		uint32_t low = AMDGPU_VA_RESERVED_SIZE >> AMDGPU_GPU_PAGE_SHIFT;
		uint32_t high = adev->vm_manager.max_pfn -
			(AMDGPU_VA_RESERVED_SIZE >> AMDGPU_GPU_PAGE_SHIFT);

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

/**
 * gmc_v8_0_gart_enable - gart enable
 *
 * @adev: amdgpu_device pointer
 *
 * This sets up the TLBs, programs the page tables for VMID0,
 * sets up the hw for VMIDs 1-15 which are allocated on
 * demand, and sets up the global locations for the LDS, GDS,
 * and GPUVM for FSA64 clients (VI).
 * Returns 0 for success, errors for failure.
 */
static int gmc_v8_0_gart_enable(struct amdgpu_device *adev)
{
	uint64_t table_addr;
	u32 tmp, field;
	int i;

	if (adev->gart.bo == NULL) {
		dev_err(adev->dev, "No VRAM object for PCIE GART.\n");
		return -EINVAL;
	}
	amdgpu_gtt_mgr_recover(&adev->mman.gtt_mgr);
	table_addr = amdgpu_bo_gpu_offset(adev->gart.bo);

	/* Setup TLB control */
	tmp = RREG32(mmMC_VM_MX_L1_TLB_CNTL);
	tmp = REG_SET_FIELD(tmp, MC_VM_MX_L1_TLB_CNTL, ENABLE_L1_TLB, 1);
	tmp = REG_SET_FIELD(tmp, MC_VM_MX_L1_TLB_CNTL, ENABLE_L1_FRAGMENT_PROCESSING, 1);
	tmp = REG_SET_FIELD(tmp, MC_VM_MX_L1_TLB_CNTL, SYSTEM_ACCESS_MODE, 3);
	tmp = REG_SET_FIELD(tmp, MC_VM_MX_L1_TLB_CNTL, ENABLE_ADVANCED_DRIVER_MODEL, 1);
	tmp = REG_SET_FIELD(tmp, MC_VM_MX_L1_TLB_CNTL, SYSTEM_APERTURE_UNMAPPED_ACCESS, 0);
	WREG32(mmMC_VM_MX_L1_TLB_CNTL, tmp);
	/* Setup L2 cache */
	tmp = RREG32(mmVM_L2_CNTL);
	tmp = REG_SET_FIELD(tmp, VM_L2_CNTL, ENABLE_L2_CACHE, 1);
	tmp = REG_SET_FIELD(tmp, VM_L2_CNTL, ENABLE_L2_FRAGMENT_PROCESSING, 1);
	tmp = REG_SET_FIELD(tmp, VM_L2_CNTL, ENABLE_L2_PTE_CACHE_LRU_UPDATE_BY_WRITE, 1);
	tmp = REG_SET_FIELD(tmp, VM_L2_CNTL, ENABLE_L2_PDE0_CACHE_LRU_UPDATE_BY_WRITE, 1);
	tmp = REG_SET_FIELD(tmp, VM_L2_CNTL, EFFECTIVE_L2_QUEUE_SIZE, 7);
	tmp = REG_SET_FIELD(tmp, VM_L2_CNTL, CONTEXT1_IDENTITY_ACCESS_MODE, 1);
	tmp = REG_SET_FIELD(tmp, VM_L2_CNTL, ENABLE_DEFAULT_PAGE_OUT_TO_SYSTEM_MEMORY, 1);
	WREG32(mmVM_L2_CNTL, tmp);
	tmp = RREG32(mmVM_L2_CNTL2);
	tmp = REG_SET_FIELD(tmp, VM_L2_CNTL2, INVALIDATE_ALL_L1_TLBS, 1);
	tmp = REG_SET_FIELD(tmp, VM_L2_CNTL2, INVALIDATE_L2_CACHE, 1);
	WREG32(mmVM_L2_CNTL2, tmp);

	field = adev->vm_manager.fragment_size;
	tmp = RREG32(mmVM_L2_CNTL3);
	tmp = REG_SET_FIELD(tmp, VM_L2_CNTL3, L2_CACHE_BIGK_ASSOCIATIVITY, 1);
	tmp = REG_SET_FIELD(tmp, VM_L2_CNTL3, BANK_SELECT, field);
	tmp = REG_SET_FIELD(tmp, VM_L2_CNTL3, L2_CACHE_BIGK_FRAGMENT_SIZE, field);
	WREG32(mmVM_L2_CNTL3, tmp);
	/* XXX: set to enable PTE/PDE in system memory */
	tmp = RREG32(mmVM_L2_CNTL4);
	tmp = REG_SET_FIELD(tmp, VM_L2_CNTL4, VMC_TAP_CONTEXT0_PDE_REQUEST_PHYSICAL, 0);
	tmp = REG_SET_FIELD(tmp, VM_L2_CNTL4, VMC_TAP_CONTEXT0_PDE_REQUEST_SHARED, 0);
	tmp = REG_SET_FIELD(tmp, VM_L2_CNTL4, VMC_TAP_CONTEXT0_PDE_REQUEST_SNOOP, 0);
	tmp = REG_SET_FIELD(tmp, VM_L2_CNTL4, VMC_TAP_CONTEXT0_PTE_REQUEST_PHYSICAL, 0);
	tmp = REG_SET_FIELD(tmp, VM_L2_CNTL4, VMC_TAP_CONTEXT0_PTE_REQUEST_SHARED, 0);
	tmp = REG_SET_FIELD(tmp, VM_L2_CNTL4, VMC_TAP_CONTEXT0_PTE_REQUEST_SNOOP, 0);
	tmp = REG_SET_FIELD(tmp, VM_L2_CNTL4, VMC_TAP_CONTEXT1_PDE_REQUEST_PHYSICAL, 0);
	tmp = REG_SET_FIELD(tmp, VM_L2_CNTL4, VMC_TAP_CONTEXT1_PDE_REQUEST_SHARED, 0);
	tmp = REG_SET_FIELD(tmp, VM_L2_CNTL4, VMC_TAP_CONTEXT1_PDE_REQUEST_SNOOP, 0);
	tmp = REG_SET_FIELD(tmp, VM_L2_CNTL4, VMC_TAP_CONTEXT1_PTE_REQUEST_PHYSICAL, 0);
	tmp = REG_SET_FIELD(tmp, VM_L2_CNTL4, VMC_TAP_CONTEXT1_PTE_REQUEST_SHARED, 0);
	tmp = REG_SET_FIELD(tmp, VM_L2_CNTL4, VMC_TAP_CONTEXT1_PTE_REQUEST_SNOOP, 0);
	WREG32(mmVM_L2_CNTL4, tmp);
	/* setup context0 */
	WREG32(mmVM_CONTEXT0_PAGE_TABLE_START_ADDR, adev->gmc.gart_start >> 12);
	WREG32(mmVM_CONTEXT0_PAGE_TABLE_END_ADDR, adev->gmc.gart_end >> 12);
	WREG32(mmVM_CONTEXT0_PAGE_TABLE_BASE_ADDR, table_addr >> 12);
	WREG32(mmVM_CONTEXT0_PROTECTION_FAULT_DEFAULT_ADDR,
			(u32)(adev->dummy_page_addr >> 12));
	WREG32(mmVM_CONTEXT0_CNTL2, 0);
	tmp = RREG32(mmVM_CONTEXT0_CNTL);
	tmp = REG_SET_FIELD(tmp, VM_CONTEXT0_CNTL, ENABLE_CONTEXT, 1);
	tmp = REG_SET_FIELD(tmp, VM_CONTEXT0_CNTL, PAGE_TABLE_DEPTH, 0);
	tmp = REG_SET_FIELD(tmp, VM_CONTEXT0_CNTL, RANGE_PROTECTION_FAULT_ENABLE_DEFAULT, 1);
	WREG32(mmVM_CONTEXT0_CNTL, tmp);

	WREG32(mmVM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR, 0);
	WREG32(mmVM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR, 0);
	WREG32(mmVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET, 0);

	/* empty context1-15 */
	/* FIXME start with 4G, once using 2 level pt switch to full
	 * vm size space
	 */
	/* set vm size, must be a multiple of 4 */
	WREG32(mmVM_CONTEXT1_PAGE_TABLE_START_ADDR, 0);
	WREG32(mmVM_CONTEXT1_PAGE_TABLE_END_ADDR, adev->vm_manager.max_pfn - 1);
	for (i = 1; i < AMDGPU_NUM_VMID; i++) {
		if (i < 8)
			WREG32(mmVM_CONTEXT0_PAGE_TABLE_BASE_ADDR + i,
			       table_addr >> 12);
		else
			WREG32(mmVM_CONTEXT8_PAGE_TABLE_BASE_ADDR + i - 8,
			       table_addr >> 12);
	}

	/* enable context1-15 */
	WREG32(mmVM_CONTEXT1_PROTECTION_FAULT_DEFAULT_ADDR,
	       (u32)(adev->dummy_page_addr >> 12));
	WREG32(mmVM_CONTEXT1_CNTL2, 4);
	tmp = RREG32(mmVM_CONTEXT1_CNTL);
	tmp = REG_SET_FIELD(tmp, VM_CONTEXT1_CNTL, ENABLE_CONTEXT, 1);
	tmp = REG_SET_FIELD(tmp, VM_CONTEXT1_CNTL, PAGE_TABLE_DEPTH, 1);
	tmp = REG_SET_FIELD(tmp, VM_CONTEXT1_CNTL, RANGE_PROTECTION_FAULT_ENABLE_DEFAULT, 1);
	tmp = REG_SET_FIELD(tmp, VM_CONTEXT1_CNTL, DUMMY_PAGE_PROTECTION_FAULT_ENABLE_DEFAULT, 1);
	tmp = REG_SET_FIELD(tmp, VM_CONTEXT1_CNTL, PDE0_PROTECTION_FAULT_ENABLE_DEFAULT, 1);
	tmp = REG_SET_FIELD(tmp, VM_CONTEXT1_CNTL, VALID_PROTECTION_FAULT_ENABLE_DEFAULT, 1);
	tmp = REG_SET_FIELD(tmp, VM_CONTEXT1_CNTL, READ_PROTECTION_FAULT_ENABLE_DEFAULT, 1);
	tmp = REG_SET_FIELD(tmp, VM_CONTEXT1_CNTL, WRITE_PROTECTION_FAULT_ENABLE_DEFAULT, 1);
	tmp = REG_SET_FIELD(tmp, VM_CONTEXT1_CNTL, EXECUTE_PROTECTION_FAULT_ENABLE_DEFAULT, 1);
	tmp = REG_SET_FIELD(tmp, VM_CONTEXT1_CNTL, PAGE_TABLE_BLOCK_SIZE,
			    adev->vm_manager.block_size - 9);
	WREG32(mmVM_CONTEXT1_CNTL, tmp);
	if (amdgpu_vm_fault_stop == AMDGPU_VM_FAULT_STOP_ALWAYS)
		gmc_v8_0_set_fault_enable_default(adev, false);
	else
		gmc_v8_0_set_fault_enable_default(adev, true);

	gmc_v8_0_flush_gpu_tlb(adev, 0, 0, 0);
	DRM_INFO("PCIE GART of %uM enabled (table at 0x%016llX).\n",
		 (unsigned int)(adev->gmc.gart_size >> 20),
		 (unsigned long long)table_addr);
	return 0;
}

static int gmc_v8_0_gart_init(struct amdgpu_device *adev)
{
	int r;

	if (adev->gart.bo) {
		WARN(1, "R600 PCIE GART already initialized\n");
		return 0;
	}
	/* Initialize common gart structure */
	r = amdgpu_gart_init(adev);
	if (r)
		return r;
	adev->gart.table_size = adev->gart.num_gpu_pages * 8;
	adev->gart.gart_pte_flags = AMDGPU_PTE_EXECUTABLE;
	return amdgpu_gart_table_vram_alloc(adev);
}

/**
 * gmc_v8_0_gart_disable - gart disable
 *
 * @adev: amdgpu_device pointer
 *
 * This disables all VM page table (VI).
 */
static void gmc_v8_0_gart_disable(struct amdgpu_device *adev)
{
	u32 tmp;

	/* Disable all tables */
	WREG32(mmVM_CONTEXT0_CNTL, 0);
	WREG32(mmVM_CONTEXT1_CNTL, 0);
	/* Setup TLB control */
	tmp = RREG32(mmMC_VM_MX_L1_TLB_CNTL);
	tmp = REG_SET_FIELD(tmp, MC_VM_MX_L1_TLB_CNTL, ENABLE_L1_TLB, 0);
	tmp = REG_SET_FIELD(tmp, MC_VM_MX_L1_TLB_CNTL, ENABLE_L1_FRAGMENT_PROCESSING, 0);
	tmp = REG_SET_FIELD(tmp, MC_VM_MX_L1_TLB_CNTL, ENABLE_ADVANCED_DRIVER_MODEL, 0);
	WREG32(mmMC_VM_MX_L1_TLB_CNTL, tmp);
	/* Setup L2 cache */
	tmp = RREG32(mmVM_L2_CNTL);
	tmp = REG_SET_FIELD(tmp, VM_L2_CNTL, ENABLE_L2_CACHE, 0);
	WREG32(mmVM_L2_CNTL, tmp);
	WREG32(mmVM_L2_CNTL2, 0);
}

/**
 * gmc_v8_0_vm_decode_fault - print human readable fault info
 *
 * @adev: amdgpu_device pointer
 * @status: VM_CONTEXT1_PROTECTION_FAULT_STATUS register value
 * @addr: VM_CONTEXT1_PROTECTION_FAULT_ADDR register value
 * @mc_client: VM_CONTEXT1_PROTECTION_FAULT_MCCLIENT register value
 * @pasid: debug logging only - no functional use
 *
 * Print human readable fault information (VI).
 */
static void gmc_v8_0_vm_decode_fault(struct amdgpu_device *adev, u32 status,
				     u32 addr, u32 mc_client, unsigned int pasid)
{
	u32 vmid = REG_GET_FIELD(status, VM_CONTEXT1_PROTECTION_FAULT_STATUS, VMID);
	u32 protections = REG_GET_FIELD(status, VM_CONTEXT1_PROTECTION_FAULT_STATUS,
					PROTECTIONS);
	char block[5] = { mc_client >> 24, (mc_client >> 16) & 0xff,
		(mc_client >> 8) & 0xff, mc_client & 0xff, 0 };
	u32 mc_id;

	mc_id = REG_GET_FIELD(status, VM_CONTEXT1_PROTECTION_FAULT_STATUS,
			      MEMORY_CLIENT_ID);

	dev_err(adev->dev, "VM fault (0x%02x, vmid %d, pasid %d) at page %u, %s from '%s' (0x%08x) (%d)\n",
	       protections, vmid, pasid, addr,
	       REG_GET_FIELD(status, VM_CONTEXT1_PROTECTION_FAULT_STATUS,
			     MEMORY_CLIENT_RW) ?
	       "write" : "read", block, mc_client, mc_id);
}

static int gmc_v8_0_convert_vram_type(int mc_seq_vram_type)
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
	case MC_SEQ_MISC0__MT__HBM:
		return AMDGPU_VRAM_TYPE_HBM;
	case MC_SEQ_MISC0__MT__DDR3:
		return AMDGPU_VRAM_TYPE_DDR3;
	default:
		return AMDGPU_VRAM_TYPE_UNKNOWN;
	}
}

static int gmc_v8_0_early_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	gmc_v8_0_set_gmc_funcs(adev);
	gmc_v8_0_set_irq_funcs(adev);

	adev->gmc.shared_aperture_start = 0x2000000000000000ULL;
	adev->gmc.shared_aperture_end =
		adev->gmc.shared_aperture_start + (4ULL << 30) - 1;
	adev->gmc.private_aperture_start =
		adev->gmc.shared_aperture_end + 1;
	adev->gmc.private_aperture_end =
		adev->gmc.private_aperture_start + (4ULL << 30) - 1;
	adev->gmc.noretry_flags = AMDGPU_VM_NORETRY_FLAGS_TF;

	return 0;
}

static int gmc_v8_0_late_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (amdgpu_vm_fault_stop != AMDGPU_VM_FAULT_STOP_ALWAYS)
		return amdgpu_irq_get(adev, &adev->gmc.vm_fault, 0);
	else
		return 0;
}

static unsigned int gmc_v8_0_get_vbios_fb_size(struct amdgpu_device *adev)
{
	u32 d1vga_control = RREG32(mmD1VGA_CONTROL);
	unsigned int size;

	if (REG_GET_FIELD(d1vga_control, D1VGA_CONTROL, D1VGA_MODE_ENABLE)) {
		size = AMDGPU_VBIOS_VGA_ALLOCATION;
	} else {
		u32 viewport = RREG32(mmVIEWPORT_SIZE);

		size = (REG_GET_FIELD(viewport, VIEWPORT_SIZE, VIEWPORT_HEIGHT) *
			REG_GET_FIELD(viewport, VIEWPORT_SIZE, VIEWPORT_WIDTH) *
			4);
	}

	return size;
}

#define mmMC_SEQ_MISC0_FIJI 0xA71

static int gmc_v8_0_sw_init(void *handle)
{
	int r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	set_bit(AMDGPU_GFXHUB(0), adev->vmhubs_mask);

	if (adev->flags & AMD_IS_APU) {
		adev->gmc.vram_type = AMDGPU_VRAM_TYPE_UNKNOWN;
	} else {
		u32 tmp;

		if ((adev->asic_type == CHIP_FIJI) ||
		    (adev->asic_type == CHIP_VEGAM))
			tmp = RREG32(mmMC_SEQ_MISC0_FIJI);
		else
			tmp = RREG32(mmMC_SEQ_MISC0);
		tmp &= MC_SEQ_MISC0__MT__MASK;
		adev->gmc.vram_type = gmc_v8_0_convert_vram_type(tmp);
	}

	r = amdgpu_irq_add_id(adev, AMDGPU_IRQ_CLIENTID_LEGACY, VISLANDS30_IV_SRCID_GFX_PAGE_INV_FAULT, &adev->gmc.vm_fault);
	if (r)
		return r;

	r = amdgpu_irq_add_id(adev, AMDGPU_IRQ_CLIENTID_LEGACY, VISLANDS30_IV_SRCID_GFX_MEM_PROT_FAULT, &adev->gmc.vm_fault);
	if (r)
		return r;

	/* Adjust VM size here.
	 * Currently set to 4GB ((1 << 20) 4k pages).
	 * Max GPUVM size for cayman and SI is 40 bits.
	 */
	amdgpu_vm_adjust_size(adev, 64, 9, 1, 40);

	/* Set the internal MC address mask
	 * This is the max address of the GPU's
	 * internal address space.
	 */
	adev->gmc.mc_mask = 0xffffffffffULL; /* 40 bit MC */

	r = dma_set_mask_and_coherent(adev->dev, DMA_BIT_MASK(40));
	if (r) {
		pr_warn("No suitable DMA available\n");
		return r;
	}
	adev->need_swiotlb = drm_need_swiotlb(40);

	r = gmc_v8_0_init_microcode(adev);
	if (r) {
		DRM_ERROR("Failed to load mc firmware!\n");
		return r;
	}

	r = gmc_v8_0_mc_init(adev);
	if (r)
		return r;

	amdgpu_gmc_get_vbios_allocations(adev);

	/* Memory manager */
	r = amdgpu_bo_init(adev);
	if (r)
		return r;

	r = gmc_v8_0_gart_init(adev);
	if (r)
		return r;

	/*
	 * number of VMs
	 * VMID 0 is reserved for System
	 * amdgpu graphics/compute will use VMIDs 1-7
	 * amdkfd will use VMIDs 8-15
	 */
	adev->vm_manager.first_kfd_vmid = 8;
	amdgpu_vm_manager_init(adev);

	/* base offset of vram pages */
	if (adev->flags & AMD_IS_APU) {
		u64 tmp = RREG32(mmMC_VM_FB_OFFSET);

		tmp <<= 22;
		adev->vm_manager.vram_base_offset = tmp;
	} else {
		adev->vm_manager.vram_base_offset = 0;
	}

	adev->gmc.vm_fault_info = kmalloc(sizeof(struct kfd_vm_fault_info),
					GFP_KERNEL);
	if (!adev->gmc.vm_fault_info)
		return -ENOMEM;
	atomic_set(&adev->gmc.vm_fault_info_updated, 0);

	return 0;
}

static int gmc_v8_0_sw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	amdgpu_gem_force_release(adev);
	amdgpu_vm_manager_fini(adev);
	kfree(adev->gmc.vm_fault_info);
	amdgpu_gart_table_vram_free(adev);
	amdgpu_bo_fini(adev);
	amdgpu_ucode_release(&adev->gmc.fw);

	return 0;
}

static int gmc_v8_0_hw_init(void *handle)
{
	int r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	gmc_v8_0_init_golden_registers(adev);

	gmc_v8_0_mc_program(adev);

	if (adev->asic_type == CHIP_TONGA) {
		r = gmc_v8_0_tonga_mc_load_microcode(adev);
		if (r) {
			DRM_ERROR("Failed to load MC firmware!\n");
			return r;
		}
	} else if (adev->asic_type == CHIP_POLARIS11 ||
			adev->asic_type == CHIP_POLARIS10 ||
			adev->asic_type == CHIP_POLARIS12) {
		r = gmc_v8_0_polaris_mc_load_microcode(adev);
		if (r) {
			DRM_ERROR("Failed to load MC firmware!\n");
			return r;
		}
	}

	r = gmc_v8_0_gart_enable(adev);
	if (r)
		return r;

	if (amdgpu_emu_mode == 1)
		return amdgpu_gmc_vram_checking(adev);
	else
		return r;
}

static int gmc_v8_0_hw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	amdgpu_irq_put(adev, &adev->gmc.vm_fault, 0);
	gmc_v8_0_gart_disable(adev);

	return 0;
}

static int gmc_v8_0_suspend(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	gmc_v8_0_hw_fini(adev);

	return 0;
}

static int gmc_v8_0_resume(void *handle)
{
	int r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	r = gmc_v8_0_hw_init(adev);
	if (r)
		return r;

	amdgpu_vmid_reset_all(adev);

	return 0;
}

static bool gmc_v8_0_is_idle(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	u32 tmp = RREG32(mmSRBM_STATUS);

	if (tmp & (SRBM_STATUS__MCB_BUSY_MASK | SRBM_STATUS__MCB_NON_DISPLAY_BUSY_MASK |
		   SRBM_STATUS__MCC_BUSY_MASK | SRBM_STATUS__MCD_BUSY_MASK | SRBM_STATUS__VMC_BUSY_MASK))
		return false;

	return true;
}

static int gmc_v8_0_wait_for_idle(void *handle)
{
	unsigned int i;
	u32 tmp;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	for (i = 0; i < adev->usec_timeout; i++) {
		/* read MC_STATUS */
		tmp = RREG32(mmSRBM_STATUS) & (SRBM_STATUS__MCB_BUSY_MASK |
					       SRBM_STATUS__MCB_NON_DISPLAY_BUSY_MASK |
					       SRBM_STATUS__MCC_BUSY_MASK |
					       SRBM_STATUS__MCD_BUSY_MASK |
					       SRBM_STATUS__VMC_BUSY_MASK |
					       SRBM_STATUS__VMC1_BUSY_MASK);
		if (!tmp)
			return 0;
		udelay(1);
	}
	return -ETIMEDOUT;

}

static bool gmc_v8_0_check_soft_reset(void *handle)
{
	u32 srbm_soft_reset = 0;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
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
		adev->gmc.srbm_soft_reset = srbm_soft_reset;
		return true;
	}

	adev->gmc.srbm_soft_reset = 0;

	return false;
}

static int gmc_v8_0_pre_soft_reset(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (!adev->gmc.srbm_soft_reset)
		return 0;

	gmc_v8_0_mc_stop(adev);
	if (gmc_v8_0_wait_for_idle(adev))
		dev_warn(adev->dev, "Wait for GMC idle timed out !\n");

	return 0;
}

static int gmc_v8_0_soft_reset(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	u32 srbm_soft_reset;

	if (!adev->gmc.srbm_soft_reset)
		return 0;
	srbm_soft_reset = adev->gmc.srbm_soft_reset;

	if (srbm_soft_reset) {
		u32 tmp;

		tmp = RREG32(mmSRBM_SOFT_RESET);
		tmp |= srbm_soft_reset;
		dev_info(adev->dev, "SRBM_SOFT_RESET=0x%08X\n", tmp);
		WREG32(mmSRBM_SOFT_RESET, tmp);
		tmp = RREG32(mmSRBM_SOFT_RESET);

		udelay(50);

		tmp &= ~srbm_soft_reset;
		WREG32(mmSRBM_SOFT_RESET, tmp);
		tmp = RREG32(mmSRBM_SOFT_RESET);

		/* Wait a little for things to settle down */
		udelay(50);
	}

	return 0;
}

static int gmc_v8_0_post_soft_reset(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (!adev->gmc.srbm_soft_reset)
		return 0;

	gmc_v8_0_mc_resume(adev);
	return 0;
}

static int gmc_v8_0_vm_fault_interrupt_state(struct amdgpu_device *adev,
					     struct amdgpu_irq_src *src,
					     unsigned int type,
					     enum amdgpu_interrupt_state state)
{
	u32 tmp;
	u32 bits = (VM_CONTEXT1_CNTL__RANGE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK |
		    VM_CONTEXT1_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK |
		    VM_CONTEXT1_CNTL__PDE0_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK |
		    VM_CONTEXT1_CNTL__VALID_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK |
		    VM_CONTEXT1_CNTL__READ_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK |
		    VM_CONTEXT1_CNTL__WRITE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK |
		    VM_CONTEXT1_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK);

	switch (state) {
	case AMDGPU_IRQ_STATE_DISABLE:
		/* system context */
		tmp = RREG32(mmVM_CONTEXT0_CNTL);
		tmp &= ~bits;
		WREG32(mmVM_CONTEXT0_CNTL, tmp);
		/* VMs */
		tmp = RREG32(mmVM_CONTEXT1_CNTL);
		tmp &= ~bits;
		WREG32(mmVM_CONTEXT1_CNTL, tmp);
		break;
	case AMDGPU_IRQ_STATE_ENABLE:
		/* system context */
		tmp = RREG32(mmVM_CONTEXT0_CNTL);
		tmp |= bits;
		WREG32(mmVM_CONTEXT0_CNTL, tmp);
		/* VMs */
		tmp = RREG32(mmVM_CONTEXT1_CNTL);
		tmp |= bits;
		WREG32(mmVM_CONTEXT1_CNTL, tmp);
		break;
	default:
		break;
	}

	return 0;
}

static int gmc_v8_0_process_interrupt(struct amdgpu_device *adev,
				      struct amdgpu_irq_src *source,
				      struct amdgpu_iv_entry *entry)
{
	u32 addr, status, mc_client, vmid;

	if (amdgpu_sriov_vf(adev)) {
		dev_err(adev->dev, "GPU fault detected: %d 0x%08x\n",
			entry->src_id, entry->src_data[0]);
		dev_err(adev->dev, " Can't decode VM fault info here on SRIOV VF\n");
		return 0;
	}

	addr = RREG32(mmVM_CONTEXT1_PROTECTION_FAULT_ADDR);
	status = RREG32(mmVM_CONTEXT1_PROTECTION_FAULT_STATUS);
	mc_client = RREG32(mmVM_CONTEXT1_PROTECTION_FAULT_MCCLIENT);
	/* reset addr and status */
	WREG32_P(mmVM_CONTEXT1_CNTL2, 1, ~1);

	if (!addr && !status)
		return 0;

	amdgpu_vm_update_fault_cache(adev, entry->pasid,
				     ((u64)addr) << AMDGPU_GPU_PAGE_SHIFT, status, AMDGPU_GFXHUB(0));

	if (amdgpu_vm_fault_stop == AMDGPU_VM_FAULT_STOP_FIRST)
		gmc_v8_0_set_fault_enable_default(adev, false);

	if (printk_ratelimit()) {
		struct amdgpu_task_info task_info;

		memset(&task_info, 0, sizeof(struct amdgpu_task_info));
		amdgpu_vm_get_task_info(adev, entry->pasid, &task_info);

		dev_err(adev->dev, "GPU fault detected: %d 0x%08x for process %s pid %d thread %s pid %d\n",
			entry->src_id, entry->src_data[0], task_info.process_name,
			task_info.tgid, task_info.task_name, task_info.pid);
		dev_err(adev->dev, "  VM_CONTEXT1_PROTECTION_FAULT_ADDR   0x%08X\n",
			addr);
		dev_err(adev->dev, "  VM_CONTEXT1_PROTECTION_FAULT_STATUS 0x%08X\n",
			status);
		gmc_v8_0_vm_decode_fault(adev, status, addr, mc_client,
					 entry->pasid);
	}

	vmid = REG_GET_FIELD(status, VM_CONTEXT1_PROTECTION_FAULT_STATUS,
			     VMID);
	if (amdgpu_amdkfd_is_kfd_vmid(adev, vmid)
		&& !atomic_read(&adev->gmc.vm_fault_info_updated)) {
		struct kfd_vm_fault_info *info = adev->gmc.vm_fault_info;
		u32 protections = REG_GET_FIELD(status,
					VM_CONTEXT1_PROTECTION_FAULT_STATUS,
					PROTECTIONS);

		info->vmid = vmid;
		info->mc_id = REG_GET_FIELD(status,
					    VM_CONTEXT1_PROTECTION_FAULT_STATUS,
					    MEMORY_CLIENT_ID);
		info->status = status;
		info->page_addr = addr;
		info->prot_valid = protections & 0x7 ? true : false;
		info->prot_read = protections & 0x8 ? true : false;
		info->prot_write = protections & 0x10 ? true : false;
		info->prot_exec = protections & 0x20 ? true : false;
		mb();
		atomic_set(&adev->gmc.vm_fault_info_updated, 1);
	}

	return 0;
}

static void fiji_update_mc_medium_grain_clock_gating(struct amdgpu_device *adev,
						     bool enable)
{
	uint32_t data;

	if (enable && (adev->cg_flags & AMD_CG_SUPPORT_MC_MGCG)) {
		data = RREG32(mmMC_HUB_MISC_HUB_CG);
		data |= MC_HUB_MISC_HUB_CG__ENABLE_MASK;
		WREG32(mmMC_HUB_MISC_HUB_CG, data);

		data = RREG32(mmMC_HUB_MISC_SIP_CG);
		data |= MC_HUB_MISC_SIP_CG__ENABLE_MASK;
		WREG32(mmMC_HUB_MISC_SIP_CG, data);

		data = RREG32(mmMC_HUB_MISC_VM_CG);
		data |= MC_HUB_MISC_VM_CG__ENABLE_MASK;
		WREG32(mmMC_HUB_MISC_VM_CG, data);

		data = RREG32(mmMC_XPB_CLK_GAT);
		data |= MC_XPB_CLK_GAT__ENABLE_MASK;
		WREG32(mmMC_XPB_CLK_GAT, data);

		data = RREG32(mmATC_MISC_CG);
		data |= ATC_MISC_CG__ENABLE_MASK;
		WREG32(mmATC_MISC_CG, data);

		data = RREG32(mmMC_CITF_MISC_WR_CG);
		data |= MC_CITF_MISC_WR_CG__ENABLE_MASK;
		WREG32(mmMC_CITF_MISC_WR_CG, data);

		data = RREG32(mmMC_CITF_MISC_RD_CG);
		data |= MC_CITF_MISC_RD_CG__ENABLE_MASK;
		WREG32(mmMC_CITF_MISC_RD_CG, data);

		data = RREG32(mmMC_CITF_MISC_VM_CG);
		data |= MC_CITF_MISC_VM_CG__ENABLE_MASK;
		WREG32(mmMC_CITF_MISC_VM_CG, data);

		data = RREG32(mmVM_L2_CG);
		data |= VM_L2_CG__ENABLE_MASK;
		WREG32(mmVM_L2_CG, data);
	} else {
		data = RREG32(mmMC_HUB_MISC_HUB_CG);
		data &= ~MC_HUB_MISC_HUB_CG__ENABLE_MASK;
		WREG32(mmMC_HUB_MISC_HUB_CG, data);

		data = RREG32(mmMC_HUB_MISC_SIP_CG);
		data &= ~MC_HUB_MISC_SIP_CG__ENABLE_MASK;
		WREG32(mmMC_HUB_MISC_SIP_CG, data);

		data = RREG32(mmMC_HUB_MISC_VM_CG);
		data &= ~MC_HUB_MISC_VM_CG__ENABLE_MASK;
		WREG32(mmMC_HUB_MISC_VM_CG, data);

		data = RREG32(mmMC_XPB_CLK_GAT);
		data &= ~MC_XPB_CLK_GAT__ENABLE_MASK;
		WREG32(mmMC_XPB_CLK_GAT, data);

		data = RREG32(mmATC_MISC_CG);
		data &= ~ATC_MISC_CG__ENABLE_MASK;
		WREG32(mmATC_MISC_CG, data);

		data = RREG32(mmMC_CITF_MISC_WR_CG);
		data &= ~MC_CITF_MISC_WR_CG__ENABLE_MASK;
		WREG32(mmMC_CITF_MISC_WR_CG, data);

		data = RREG32(mmMC_CITF_MISC_RD_CG);
		data &= ~MC_CITF_MISC_RD_CG__ENABLE_MASK;
		WREG32(mmMC_CITF_MISC_RD_CG, data);

		data = RREG32(mmMC_CITF_MISC_VM_CG);
		data &= ~MC_CITF_MISC_VM_CG__ENABLE_MASK;
		WREG32(mmMC_CITF_MISC_VM_CG, data);

		data = RREG32(mmVM_L2_CG);
		data &= ~VM_L2_CG__ENABLE_MASK;
		WREG32(mmVM_L2_CG, data);
	}
}

static void fiji_update_mc_light_sleep(struct amdgpu_device *adev,
				       bool enable)
{
	uint32_t data;

	if (enable && (adev->cg_flags & AMD_CG_SUPPORT_MC_LS)) {
		data = RREG32(mmMC_HUB_MISC_HUB_CG);
		data |= MC_HUB_MISC_HUB_CG__MEM_LS_ENABLE_MASK;
		WREG32(mmMC_HUB_MISC_HUB_CG, data);

		data = RREG32(mmMC_HUB_MISC_SIP_CG);
		data |= MC_HUB_MISC_SIP_CG__MEM_LS_ENABLE_MASK;
		WREG32(mmMC_HUB_MISC_SIP_CG, data);

		data = RREG32(mmMC_HUB_MISC_VM_CG);
		data |= MC_HUB_MISC_VM_CG__MEM_LS_ENABLE_MASK;
		WREG32(mmMC_HUB_MISC_VM_CG, data);

		data = RREG32(mmMC_XPB_CLK_GAT);
		data |= MC_XPB_CLK_GAT__MEM_LS_ENABLE_MASK;
		WREG32(mmMC_XPB_CLK_GAT, data);

		data = RREG32(mmATC_MISC_CG);
		data |= ATC_MISC_CG__MEM_LS_ENABLE_MASK;
		WREG32(mmATC_MISC_CG, data);

		data = RREG32(mmMC_CITF_MISC_WR_CG);
		data |= MC_CITF_MISC_WR_CG__MEM_LS_ENABLE_MASK;
		WREG32(mmMC_CITF_MISC_WR_CG, data);

		data = RREG32(mmMC_CITF_MISC_RD_CG);
		data |= MC_CITF_MISC_RD_CG__MEM_LS_ENABLE_MASK;
		WREG32(mmMC_CITF_MISC_RD_CG, data);

		data = RREG32(mmMC_CITF_MISC_VM_CG);
		data |= MC_CITF_MISC_VM_CG__MEM_LS_ENABLE_MASK;
		WREG32(mmMC_CITF_MISC_VM_CG, data);

		data = RREG32(mmVM_L2_CG);
		data |= VM_L2_CG__MEM_LS_ENABLE_MASK;
		WREG32(mmVM_L2_CG, data);
	} else {
		data = RREG32(mmMC_HUB_MISC_HUB_CG);
		data &= ~MC_HUB_MISC_HUB_CG__MEM_LS_ENABLE_MASK;
		WREG32(mmMC_HUB_MISC_HUB_CG, data);

		data = RREG32(mmMC_HUB_MISC_SIP_CG);
		data &= ~MC_HUB_MISC_SIP_CG__MEM_LS_ENABLE_MASK;
		WREG32(mmMC_HUB_MISC_SIP_CG, data);

		data = RREG32(mmMC_HUB_MISC_VM_CG);
		data &= ~MC_HUB_MISC_VM_CG__MEM_LS_ENABLE_MASK;
		WREG32(mmMC_HUB_MISC_VM_CG, data);

		data = RREG32(mmMC_XPB_CLK_GAT);
		data &= ~MC_XPB_CLK_GAT__MEM_LS_ENABLE_MASK;
		WREG32(mmMC_XPB_CLK_GAT, data);

		data = RREG32(mmATC_MISC_CG);
		data &= ~ATC_MISC_CG__MEM_LS_ENABLE_MASK;
		WREG32(mmATC_MISC_CG, data);

		data = RREG32(mmMC_CITF_MISC_WR_CG);
		data &= ~MC_CITF_MISC_WR_CG__MEM_LS_ENABLE_MASK;
		WREG32(mmMC_CITF_MISC_WR_CG, data);

		data = RREG32(mmMC_CITF_MISC_RD_CG);
		data &= ~MC_CITF_MISC_RD_CG__MEM_LS_ENABLE_MASK;
		WREG32(mmMC_CITF_MISC_RD_CG, data);

		data = RREG32(mmMC_CITF_MISC_VM_CG);
		data &= ~MC_CITF_MISC_VM_CG__MEM_LS_ENABLE_MASK;
		WREG32(mmMC_CITF_MISC_VM_CG, data);

		data = RREG32(mmVM_L2_CG);
		data &= ~VM_L2_CG__MEM_LS_ENABLE_MASK;
		WREG32(mmVM_L2_CG, data);
	}
}

static int gmc_v8_0_set_clockgating_state(void *handle,
					  enum amd_clockgating_state state)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (amdgpu_sriov_vf(adev))
		return 0;

	switch (adev->asic_type) {
	case CHIP_FIJI:
		fiji_update_mc_medium_grain_clock_gating(adev,
				state == AMD_CG_STATE_GATE);
		fiji_update_mc_light_sleep(adev,
				state == AMD_CG_STATE_GATE);
		break;
	default:
		break;
	}
	return 0;
}

static int gmc_v8_0_set_powergating_state(void *handle,
					  enum amd_powergating_state state)
{
	return 0;
}

static void gmc_v8_0_get_clockgating_state(void *handle, u64 *flags)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int data;

	if (amdgpu_sriov_vf(adev))
		*flags = 0;

	/* AMD_CG_SUPPORT_MC_MGCG */
	data = RREG32(mmMC_HUB_MISC_HUB_CG);
	if (data & MC_HUB_MISC_HUB_CG__ENABLE_MASK)
		*flags |= AMD_CG_SUPPORT_MC_MGCG;

	/* AMD_CG_SUPPORT_MC_LS */
	if (data & MC_HUB_MISC_HUB_CG__MEM_LS_ENABLE_MASK)
		*flags |= AMD_CG_SUPPORT_MC_LS;
}

static const struct amd_ip_funcs gmc_v8_0_ip_funcs = {
	.name = "gmc_v8_0",
	.early_init = gmc_v8_0_early_init,
	.late_init = gmc_v8_0_late_init,
	.sw_init = gmc_v8_0_sw_init,
	.sw_fini = gmc_v8_0_sw_fini,
	.hw_init = gmc_v8_0_hw_init,
	.hw_fini = gmc_v8_0_hw_fini,
	.suspend = gmc_v8_0_suspend,
	.resume = gmc_v8_0_resume,
	.is_idle = gmc_v8_0_is_idle,
	.wait_for_idle = gmc_v8_0_wait_for_idle,
	.check_soft_reset = gmc_v8_0_check_soft_reset,
	.pre_soft_reset = gmc_v8_0_pre_soft_reset,
	.soft_reset = gmc_v8_0_soft_reset,
	.post_soft_reset = gmc_v8_0_post_soft_reset,
	.set_clockgating_state = gmc_v8_0_set_clockgating_state,
	.set_powergating_state = gmc_v8_0_set_powergating_state,
	.get_clockgating_state = gmc_v8_0_get_clockgating_state,
};

static const struct amdgpu_gmc_funcs gmc_v8_0_gmc_funcs = {
	.flush_gpu_tlb = gmc_v8_0_flush_gpu_tlb,
	.flush_gpu_tlb_pasid = gmc_v8_0_flush_gpu_tlb_pasid,
	.emit_flush_gpu_tlb = gmc_v8_0_emit_flush_gpu_tlb,
	.emit_pasid_mapping = gmc_v8_0_emit_pasid_mapping,
	.set_prt = gmc_v8_0_set_prt,
	.get_vm_pde = gmc_v8_0_get_vm_pde,
	.get_vm_pte = gmc_v8_0_get_vm_pte,
	.get_vbios_fb_size = gmc_v8_0_get_vbios_fb_size,
};

static const struct amdgpu_irq_src_funcs gmc_v8_0_irq_funcs = {
	.set = gmc_v8_0_vm_fault_interrupt_state,
	.process = gmc_v8_0_process_interrupt,
};

static void gmc_v8_0_set_gmc_funcs(struct amdgpu_device *adev)
{
	adev->gmc.gmc_funcs = &gmc_v8_0_gmc_funcs;
}

static void gmc_v8_0_set_irq_funcs(struct amdgpu_device *adev)
{
	adev->gmc.vm_fault.num_types = 1;
	adev->gmc.vm_fault.funcs = &gmc_v8_0_irq_funcs;
}

const struct amdgpu_ip_block_version gmc_v8_0_ip_block = {
	.type = AMD_IP_BLOCK_TYPE_GMC,
	.major = 8,
	.minor = 0,
	.rev = 0,
	.funcs = &gmc_v8_0_ip_funcs,
};

const struct amdgpu_ip_block_version gmc_v8_1_ip_block = {
	.type = AMD_IP_BLOCK_TYPE_GMC,
	.major = 8,
	.minor = 1,
	.rev = 0,
	.funcs = &gmc_v8_0_ip_funcs,
};

const struct amdgpu_ip_block_version gmc_v8_5_ip_block = {
	.type = AMD_IP_BLOCK_TYPE_GMC,
	.major = 8,
	.minor = 5,
	.rev = 0,
	.funcs = &gmc_v8_0_ip_funcs,
};
