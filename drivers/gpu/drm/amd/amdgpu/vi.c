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

#include <linux/pci.h>
#include <linux/slab.h>

#include <drm/amdgpu_drm.h>

#include "amdgpu.h"
#include "amdgpu_atombios.h"
#include "amdgpu_ih.h"
#include "amdgpu_uvd.h"
#include "amdgpu_vce.h"
#include "amdgpu_ucode.h"
#include "atom.h"
#include "amd_pcie.h"

#include "gmc/gmc_8_1_d.h"
#include "gmc/gmc_8_1_sh_mask.h"

#include "oss/oss_3_0_d.h"
#include "oss/oss_3_0_sh_mask.h"

#include "bif/bif_5_0_d.h"
#include "bif/bif_5_0_sh_mask.h"

#include "gca/gfx_8_0_d.h"
#include "gca/gfx_8_0_sh_mask.h"

#include "smu/smu_7_1_1_d.h"
#include "smu/smu_7_1_1_sh_mask.h"

#include "uvd/uvd_5_0_d.h"
#include "uvd/uvd_5_0_sh_mask.h"

#include "vce/vce_3_0_d.h"
#include "vce/vce_3_0_sh_mask.h"

#include "dce/dce_10_0_d.h"
#include "dce/dce_10_0_sh_mask.h"

#include "vid.h"
#include "vi.h"
#include "gmc_v8_0.h"
#include "gmc_v7_0.h"
#include "gfx_v8_0.h"
#include "sdma_v2_4.h"
#include "sdma_v3_0.h"
#include "dce_v10_0.h"
#include "dce_v11_0.h"
#include "iceland_ih.h"
#include "tonga_ih.h"
#include "cz_ih.h"
#include "uvd_v5_0.h"
#include "uvd_v6_0.h"
#include "vce_v3_0.h"
#if defined(CONFIG_DRM_AMD_ACP)
#include "amdgpu_acp.h"
#endif
#include "amdgpu_vkms.h"
#include "mxgpu_vi.h"
#include "amdgpu_dm.h"

#if IS_ENABLED(CONFIG_X86)
#include <asm/intel-family.h>
#endif

#define ixPCIE_LC_L1_PM_SUBSTATE	0x100100C6
#define PCIE_LC_L1_PM_SUBSTATE__LC_L1_SUBSTATES_OVERRIDE_EN_MASK	0x00000001L
#define PCIE_LC_L1_PM_SUBSTATE__LC_PCI_PM_L1_2_OVERRIDE_MASK	0x00000002L
#define PCIE_LC_L1_PM_SUBSTATE__LC_PCI_PM_L1_1_OVERRIDE_MASK	0x00000004L
#define PCIE_LC_L1_PM_SUBSTATE__LC_ASPM_L1_2_OVERRIDE_MASK		0x00000008L
#define PCIE_LC_L1_PM_SUBSTATE__LC_ASPM_L1_1_OVERRIDE_MASK		0x00000010L
#define ixPCIE_L1_PM_SUB_CNTL	0x378
#define PCIE_L1_PM_SUB_CNTL__ASPM_L1_2_EN_MASK	0x00000004L
#define PCIE_L1_PM_SUB_CNTL__ASPM_L1_1_EN_MASK	0x00000008L
#define PCIE_L1_PM_SUB_CNTL__PCI_PM_L1_2_EN_MASK	0x00000001L
#define PCIE_L1_PM_SUB_CNTL__PCI_PM_L1_1_EN_MASK	0x00000002L
#define PCIE_LC_CNTL6__LC_L1_POWERDOWN_MASK		0x00200000L
#define LINK_CAP	0x64
#define PCIE_LINK_CAP__CLOCK_POWER_MANAGEMENT_MASK	0x00040000L
#define ixCPM_CONTROL	0x1400118
#define ixPCIE_LC_CNTL7	0x100100BC
#define PCIE_LC_CNTL7__LC_L1_SIDEBAND_CLKREQ_PDWN_EN_MASK	0x00000400L
#define PCIE_LC_CNTL__LC_L0S_INACTIVITY_DEFAULT	0x00000007
#define PCIE_LC_CNTL__LC_L1_INACTIVITY_DEFAULT	0x00000009
#define CPM_CONTROL__CLKREQb_UNGATE_TXCLK_ENABLE_MASK	0x01000000L
#define PCIE_L1_PM_SUB_CNTL	0x378
#define ASIC_IS_P22(asic_type, rid)	((asic_type >= CHIP_POLARIS10) && \
									(asic_type <= CHIP_POLARIS12) && \
									(rid >= 0x6E))
/* Topaz */
static const struct amdgpu_video_codecs topaz_video_codecs_encode =
{
	.codec_count = 0,
	.codec_array = NULL,
};

/* Tonga, CZ, ST, Fiji */
static const struct amdgpu_video_codec_info tonga_video_codecs_encode_array[] =
{
	{
		.codec_type = AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_MPEG4_AVC,
		.max_width = 4096,
		.max_height = 2304,
		.max_pixels_per_frame = 4096 * 2304,
		.max_level = 0,
	},
};

static const struct amdgpu_video_codecs tonga_video_codecs_encode =
{
	.codec_count = ARRAY_SIZE(tonga_video_codecs_encode_array),
	.codec_array = tonga_video_codecs_encode_array,
};

/* Polaris */
static const struct amdgpu_video_codec_info polaris_video_codecs_encode_array[] =
{
	{
		.codec_type = AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_MPEG4_AVC,
		.max_width = 4096,
		.max_height = 2304,
		.max_pixels_per_frame = 4096 * 2304,
		.max_level = 0,
	},
	{
		.codec_type = AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_HEVC,
		.max_width = 4096,
		.max_height = 2304,
		.max_pixels_per_frame = 4096 * 2304,
		.max_level = 0,
	},
};

static const struct amdgpu_video_codecs polaris_video_codecs_encode =
{
	.codec_count = ARRAY_SIZE(polaris_video_codecs_encode_array),
	.codec_array = polaris_video_codecs_encode_array,
};

/* Topaz */
static const struct amdgpu_video_codecs topaz_video_codecs_decode =
{
	.codec_count = 0,
	.codec_array = NULL,
};

/* Tonga */
static const struct amdgpu_video_codec_info tonga_video_codecs_decode_array[] =
{
	{
		.codec_type = AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_MPEG2,
		.max_width = 4096,
		.max_height = 4096,
		.max_pixels_per_frame = 4096 * 4096,
		.max_level = 3,
	},
	{
		.codec_type = AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_MPEG4,
		.max_width = 4096,
		.max_height = 4096,
		.max_pixels_per_frame = 4096 * 4096,
		.max_level = 5,
	},
	{
		.codec_type = AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_MPEG4_AVC,
		.max_width = 4096,
		.max_height = 4096,
		.max_pixels_per_frame = 4096 * 4096,
		.max_level = 52,
	},
	{
		.codec_type = AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_VC1,
		.max_width = 4096,
		.max_height = 4096,
		.max_pixels_per_frame = 4096 * 4096,
		.max_level = 4,
	},
};

static const struct amdgpu_video_codecs tonga_video_codecs_decode =
{
	.codec_count = ARRAY_SIZE(tonga_video_codecs_decode_array),
	.codec_array = tonga_video_codecs_decode_array,
};

/* CZ, ST, Fiji, Polaris */
static const struct amdgpu_video_codec_info cz_video_codecs_decode_array[] =
{
	{
		.codec_type = AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_MPEG2,
		.max_width = 4096,
		.max_height = 4096,
		.max_pixels_per_frame = 4096 * 4096,
		.max_level = 3,
	},
	{
		.codec_type = AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_MPEG4,
		.max_width = 4096,
		.max_height = 4096,
		.max_pixels_per_frame = 4096 * 4096,
		.max_level = 5,
	},
	{
		.codec_type = AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_MPEG4_AVC,
		.max_width = 4096,
		.max_height = 4096,
		.max_pixels_per_frame = 4096 * 4096,
		.max_level = 52,
	},
	{
		.codec_type = AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_VC1,
		.max_width = 4096,
		.max_height = 4096,
		.max_pixels_per_frame = 4096 * 4096,
		.max_level = 4,
	},
	{
		.codec_type = AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_HEVC,
		.max_width = 4096,
		.max_height = 4096,
		.max_pixels_per_frame = 4096 * 4096,
		.max_level = 186,
	},
	{
		.codec_type = AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_JPEG,
		.max_width = 4096,
		.max_height = 4096,
		.max_pixels_per_frame = 4096 * 4096,
		.max_level = 0,
	},
};

static const struct amdgpu_video_codecs cz_video_codecs_decode =
{
	.codec_count = ARRAY_SIZE(cz_video_codecs_decode_array),
	.codec_array = cz_video_codecs_decode_array,
};

static int vi_query_video_codecs(struct amdgpu_device *adev, bool encode,
				 const struct amdgpu_video_codecs **codecs)
{
	switch (adev->asic_type) {
	case CHIP_TOPAZ:
		if (encode)
			*codecs = &topaz_video_codecs_encode;
		else
			*codecs = &topaz_video_codecs_decode;
		return 0;
	case CHIP_TONGA:
		if (encode)
			*codecs = &tonga_video_codecs_encode;
		else
			*codecs = &tonga_video_codecs_decode;
		return 0;
	case CHIP_POLARIS10:
	case CHIP_POLARIS11:
	case CHIP_POLARIS12:
	case CHIP_VEGAM:
		if (encode)
			*codecs = &polaris_video_codecs_encode;
		else
			*codecs = &cz_video_codecs_decode;
		return 0;
	case CHIP_FIJI:
	case CHIP_CARRIZO:
	case CHIP_STONEY:
		if (encode)
			*codecs = &tonga_video_codecs_encode;
		else
			*codecs = &cz_video_codecs_decode;
		return 0;
	default:
		return -EINVAL;
	}
}

/*
 * Indirect registers accessor
 */
static u32 vi_pcie_rreg(struct amdgpu_device *adev, u32 reg)
{
	unsigned long flags;
	u32 r;

	spin_lock_irqsave(&adev->pcie_idx_lock, flags);
	WREG32_NO_KIQ(mmPCIE_INDEX, reg);
	(void)RREG32_NO_KIQ(mmPCIE_INDEX);
	r = RREG32_NO_KIQ(mmPCIE_DATA);
	spin_unlock_irqrestore(&adev->pcie_idx_lock, flags);
	return r;
}

static void vi_pcie_wreg(struct amdgpu_device *adev, u32 reg, u32 v)
{
	unsigned long flags;

	spin_lock_irqsave(&adev->pcie_idx_lock, flags);
	WREG32_NO_KIQ(mmPCIE_INDEX, reg);
	(void)RREG32_NO_KIQ(mmPCIE_INDEX);
	WREG32_NO_KIQ(mmPCIE_DATA, v);
	(void)RREG32_NO_KIQ(mmPCIE_DATA);
	spin_unlock_irqrestore(&adev->pcie_idx_lock, flags);
}

static u32 vi_smc_rreg(struct amdgpu_device *adev, u32 reg)
{
	unsigned long flags;
	u32 r;

	spin_lock_irqsave(&adev->smc_idx_lock, flags);
	WREG32_NO_KIQ(mmSMC_IND_INDEX_11, (reg));
	r = RREG32_NO_KIQ(mmSMC_IND_DATA_11);
	spin_unlock_irqrestore(&adev->smc_idx_lock, flags);
	return r;
}

static void vi_smc_wreg(struct amdgpu_device *adev, u32 reg, u32 v)
{
	unsigned long flags;

	spin_lock_irqsave(&adev->smc_idx_lock, flags);
	WREG32_NO_KIQ(mmSMC_IND_INDEX_11, (reg));
	WREG32_NO_KIQ(mmSMC_IND_DATA_11, (v));
	spin_unlock_irqrestore(&adev->smc_idx_lock, flags);
}

/* smu_8_0_d.h */
#define mmMP0PUB_IND_INDEX                                                      0x180
#define mmMP0PUB_IND_DATA                                                       0x181

static u32 cz_smc_rreg(struct amdgpu_device *adev, u32 reg)
{
	unsigned long flags;
	u32 r;

	spin_lock_irqsave(&adev->smc_idx_lock, flags);
	WREG32(mmMP0PUB_IND_INDEX, (reg));
	r = RREG32(mmMP0PUB_IND_DATA);
	spin_unlock_irqrestore(&adev->smc_idx_lock, flags);
	return r;
}

static void cz_smc_wreg(struct amdgpu_device *adev, u32 reg, u32 v)
{
	unsigned long flags;

	spin_lock_irqsave(&adev->smc_idx_lock, flags);
	WREG32(mmMP0PUB_IND_INDEX, (reg));
	WREG32(mmMP0PUB_IND_DATA, (v));
	spin_unlock_irqrestore(&adev->smc_idx_lock, flags);
}

static u32 vi_uvd_ctx_rreg(struct amdgpu_device *adev, u32 reg)
{
	unsigned long flags;
	u32 r;

	spin_lock_irqsave(&adev->uvd_ctx_idx_lock, flags);
	WREG32(mmUVD_CTX_INDEX, ((reg) & 0x1ff));
	r = RREG32(mmUVD_CTX_DATA);
	spin_unlock_irqrestore(&adev->uvd_ctx_idx_lock, flags);
	return r;
}

static void vi_uvd_ctx_wreg(struct amdgpu_device *adev, u32 reg, u32 v)
{
	unsigned long flags;

	spin_lock_irqsave(&adev->uvd_ctx_idx_lock, flags);
	WREG32(mmUVD_CTX_INDEX, ((reg) & 0x1ff));
	WREG32(mmUVD_CTX_DATA, (v));
	spin_unlock_irqrestore(&adev->uvd_ctx_idx_lock, flags);
}

static u32 vi_didt_rreg(struct amdgpu_device *adev, u32 reg)
{
	unsigned long flags;
	u32 r;

	spin_lock_irqsave(&adev->didt_idx_lock, flags);
	WREG32(mmDIDT_IND_INDEX, (reg));
	r = RREG32(mmDIDT_IND_DATA);
	spin_unlock_irqrestore(&adev->didt_idx_lock, flags);
	return r;
}

static void vi_didt_wreg(struct amdgpu_device *adev, u32 reg, u32 v)
{
	unsigned long flags;

	spin_lock_irqsave(&adev->didt_idx_lock, flags);
	WREG32(mmDIDT_IND_INDEX, (reg));
	WREG32(mmDIDT_IND_DATA, (v));
	spin_unlock_irqrestore(&adev->didt_idx_lock, flags);
}

static u32 vi_gc_cac_rreg(struct amdgpu_device *adev, u32 reg)
{
	unsigned long flags;
	u32 r;

	spin_lock_irqsave(&adev->gc_cac_idx_lock, flags);
	WREG32(mmGC_CAC_IND_INDEX, (reg));
	r = RREG32(mmGC_CAC_IND_DATA);
	spin_unlock_irqrestore(&adev->gc_cac_idx_lock, flags);
	return r;
}

static void vi_gc_cac_wreg(struct amdgpu_device *adev, u32 reg, u32 v)
{
	unsigned long flags;

	spin_lock_irqsave(&adev->gc_cac_idx_lock, flags);
	WREG32(mmGC_CAC_IND_INDEX, (reg));
	WREG32(mmGC_CAC_IND_DATA, (v));
	spin_unlock_irqrestore(&adev->gc_cac_idx_lock, flags);
}


static const u32 tonga_mgcg_cgcg_init[] =
{
	mmCGTT_DRM_CLK_CTRL0, 0xffffffff, 0x00600100,
	mmPCIE_INDEX, 0xffffffff, 0x0140001c,
	mmPCIE_DATA, 0x000f0000, 0x00000000,
	mmSMC_IND_INDEX_4, 0xffffffff, 0xC060000C,
	mmSMC_IND_DATA_4, 0xc0000fff, 0x00000100,
	mmCGTT_DRM_CLK_CTRL0, 0xff000fff, 0x00000100,
	mmHDP_XDP_CGTT_BLK_CTRL, 0xc0000fff, 0x00000104,
};

static const u32 fiji_mgcg_cgcg_init[] =
{
	mmCGTT_DRM_CLK_CTRL0, 0xffffffff, 0x00600100,
	mmPCIE_INDEX, 0xffffffff, 0x0140001c,
	mmPCIE_DATA, 0x000f0000, 0x00000000,
	mmSMC_IND_INDEX_4, 0xffffffff, 0xC060000C,
	mmSMC_IND_DATA_4, 0xc0000fff, 0x00000100,
	mmCGTT_DRM_CLK_CTRL0, 0xff000fff, 0x00000100,
	mmHDP_XDP_CGTT_BLK_CTRL, 0xc0000fff, 0x00000104,
};

static const u32 iceland_mgcg_cgcg_init[] =
{
	mmPCIE_INDEX, 0xffffffff, ixPCIE_CNTL2,
	mmPCIE_DATA, 0x000f0000, 0x00000000,
	mmSMC_IND_INDEX_4, 0xffffffff, ixCGTT_ROM_CLK_CTRL0,
	mmSMC_IND_DATA_4, 0xc0000fff, 0x00000100,
	mmHDP_XDP_CGTT_BLK_CTRL, 0xc0000fff, 0x00000104,
};

static const u32 cz_mgcg_cgcg_init[] =
{
	mmCGTT_DRM_CLK_CTRL0, 0xffffffff, 0x00600100,
	mmPCIE_INDEX, 0xffffffff, 0x0140001c,
	mmPCIE_DATA, 0x000f0000, 0x00000000,
	mmCGTT_DRM_CLK_CTRL0, 0xff000fff, 0x00000100,
	mmHDP_XDP_CGTT_BLK_CTRL, 0xc0000fff, 0x00000104,
};

static const u32 stoney_mgcg_cgcg_init[] =
{
	mmCGTT_DRM_CLK_CTRL0, 0xffffffff, 0x00000100,
	mmHDP_XDP_CGTT_BLK_CTRL, 0xffffffff, 0x00000104,
	mmHDP_HOST_PATH_CNTL, 0xffffffff, 0x0f000027,
};

static void vi_init_golden_registers(struct amdgpu_device *adev)
{
	/* Some of the registers might be dependent on GRBM_GFX_INDEX */
	mutex_lock(&adev->grbm_idx_mutex);

	if (amdgpu_sriov_vf(adev)) {
		xgpu_vi_init_golden_registers(adev);
		mutex_unlock(&adev->grbm_idx_mutex);
		return;
	}

	switch (adev->asic_type) {
	case CHIP_TOPAZ:
		amdgpu_device_program_register_sequence(adev,
							iceland_mgcg_cgcg_init,
							ARRAY_SIZE(iceland_mgcg_cgcg_init));
		break;
	case CHIP_FIJI:
		amdgpu_device_program_register_sequence(adev,
							fiji_mgcg_cgcg_init,
							ARRAY_SIZE(fiji_mgcg_cgcg_init));
		break;
	case CHIP_TONGA:
		amdgpu_device_program_register_sequence(adev,
							tonga_mgcg_cgcg_init,
							ARRAY_SIZE(tonga_mgcg_cgcg_init));
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
		break;
	case CHIP_POLARIS10:
	case CHIP_POLARIS11:
	case CHIP_POLARIS12:
	case CHIP_VEGAM:
	default:
		break;
	}
	mutex_unlock(&adev->grbm_idx_mutex);
}

/**
 * vi_get_xclk - get the xclk
 *
 * @adev: amdgpu_device pointer
 *
 * Returns the reference clock used by the gfx engine
 * (VI).
 */
static u32 vi_get_xclk(struct amdgpu_device *adev)
{
	u32 reference_clock = adev->clock.spll.reference_freq;
	u32 tmp;

	if (adev->flags & AMD_IS_APU)
		return reference_clock;

	tmp = RREG32_SMC(ixCG_CLKPIN_CNTL_2);
	if (REG_GET_FIELD(tmp, CG_CLKPIN_CNTL_2, MUX_TCLK_TO_XCLK))
		return 1000;

	tmp = RREG32_SMC(ixCG_CLKPIN_CNTL);
	if (REG_GET_FIELD(tmp, CG_CLKPIN_CNTL, XTALIN_DIVIDE))
		return reference_clock / 4;

	return reference_clock;
}

/**
 * vi_srbm_select - select specific register instances
 *
 * @adev: amdgpu_device pointer
 * @me: selected ME (micro engine)
 * @pipe: pipe
 * @queue: queue
 * @vmid: VMID
 *
 * Switches the currently active registers instances.  Some
 * registers are instanced per VMID, others are instanced per
 * me/pipe/queue combination.
 */
void vi_srbm_select(struct amdgpu_device *adev,
		     u32 me, u32 pipe, u32 queue, u32 vmid)
{
	u32 srbm_gfx_cntl = 0;
	srbm_gfx_cntl = REG_SET_FIELD(srbm_gfx_cntl, SRBM_GFX_CNTL, PIPEID, pipe);
	srbm_gfx_cntl = REG_SET_FIELD(srbm_gfx_cntl, SRBM_GFX_CNTL, MEID, me);
	srbm_gfx_cntl = REG_SET_FIELD(srbm_gfx_cntl, SRBM_GFX_CNTL, VMID, vmid);
	srbm_gfx_cntl = REG_SET_FIELD(srbm_gfx_cntl, SRBM_GFX_CNTL, QUEUEID, queue);
	WREG32(mmSRBM_GFX_CNTL, srbm_gfx_cntl);
}

static void vi_vga_set_state(struct amdgpu_device *adev, bool state)
{
	/* todo */
}

static bool vi_read_disabled_bios(struct amdgpu_device *adev)
{
	u32 bus_cntl;
	u32 d1vga_control = 0;
	u32 d2vga_control = 0;
	u32 vga_render_control = 0;
	u32 rom_cntl;
	bool r;

	bus_cntl = RREG32(mmBUS_CNTL);
	if (adev->mode_info.num_crtc) {
		d1vga_control = RREG32(mmD1VGA_CONTROL);
		d2vga_control = RREG32(mmD2VGA_CONTROL);
		vga_render_control = RREG32(mmVGA_RENDER_CONTROL);
	}
	rom_cntl = RREG32_SMC(ixROM_CNTL);

	/* enable the rom */
	WREG32(mmBUS_CNTL, (bus_cntl & ~BUS_CNTL__BIOS_ROM_DIS_MASK));
	if (adev->mode_info.num_crtc) {
		/* Disable VGA mode */
		WREG32(mmD1VGA_CONTROL,
		       (d1vga_control & ~(D1VGA_CONTROL__D1VGA_MODE_ENABLE_MASK |
					  D1VGA_CONTROL__D1VGA_TIMING_SELECT_MASK)));
		WREG32(mmD2VGA_CONTROL,
		       (d2vga_control & ~(D2VGA_CONTROL__D2VGA_MODE_ENABLE_MASK |
					  D2VGA_CONTROL__D2VGA_TIMING_SELECT_MASK)));
		WREG32(mmVGA_RENDER_CONTROL,
		       (vga_render_control & ~VGA_RENDER_CONTROL__VGA_VSTATUS_CNTL_MASK));
	}
	WREG32_SMC(ixROM_CNTL, rom_cntl | ROM_CNTL__SCK_OVERWRITE_MASK);

	r = amdgpu_read_bios(adev);

	/* restore regs */
	WREG32(mmBUS_CNTL, bus_cntl);
	if (adev->mode_info.num_crtc) {
		WREG32(mmD1VGA_CONTROL, d1vga_control);
		WREG32(mmD2VGA_CONTROL, d2vga_control);
		WREG32(mmVGA_RENDER_CONTROL, vga_render_control);
	}
	WREG32_SMC(ixROM_CNTL, rom_cntl);
	return r;
}

static bool vi_read_bios_from_rom(struct amdgpu_device *adev,
				  u8 *bios, u32 length_bytes)
{
	u32 *dw_ptr;
	unsigned long flags;
	u32 i, length_dw;

	if (bios == NULL)
		return false;
	if (length_bytes == 0)
		return false;
	/* APU vbios image is part of sbios image */
	if (adev->flags & AMD_IS_APU)
		return false;

	dw_ptr = (u32 *)bios;
	length_dw = ALIGN(length_bytes, 4) / 4;
	/* take the smc lock since we are using the smc index */
	spin_lock_irqsave(&adev->smc_idx_lock, flags);
	/* set rom index to 0 */
	WREG32(mmSMC_IND_INDEX_11, ixROM_INDEX);
	WREG32(mmSMC_IND_DATA_11, 0);
	/* set index to data for continous read */
	WREG32(mmSMC_IND_INDEX_11, ixROM_DATA);
	for (i = 0; i < length_dw; i++)
		dw_ptr[i] = RREG32(mmSMC_IND_DATA_11);
	spin_unlock_irqrestore(&adev->smc_idx_lock, flags);

	return true;
}

static const struct amdgpu_allowed_register_entry vi_allowed_read_registers[] = {
	{mmGRBM_STATUS},
	{mmGRBM_STATUS2},
	{mmGRBM_STATUS_SE0},
	{mmGRBM_STATUS_SE1},
	{mmGRBM_STATUS_SE2},
	{mmGRBM_STATUS_SE3},
	{mmSRBM_STATUS},
	{mmSRBM_STATUS2},
	{mmSRBM_STATUS3},
	{mmSDMA0_STATUS_REG + SDMA0_REGISTER_OFFSET},
	{mmSDMA0_STATUS_REG + SDMA1_REGISTER_OFFSET},
	{mmCP_STAT},
	{mmCP_STALLED_STAT1},
	{mmCP_STALLED_STAT2},
	{mmCP_STALLED_STAT3},
	{mmCP_CPF_BUSY_STAT},
	{mmCP_CPF_STALLED_STAT1},
	{mmCP_CPF_STATUS},
	{mmCP_CPC_BUSY_STAT},
	{mmCP_CPC_STALLED_STAT1},
	{mmCP_CPC_STATUS},
	{mmGB_ADDR_CONFIG},
	{mmMC_ARB_RAMCFG},
	{mmGB_TILE_MODE0},
	{mmGB_TILE_MODE1},
	{mmGB_TILE_MODE2},
	{mmGB_TILE_MODE3},
	{mmGB_TILE_MODE4},
	{mmGB_TILE_MODE5},
	{mmGB_TILE_MODE6},
	{mmGB_TILE_MODE7},
	{mmGB_TILE_MODE8},
	{mmGB_TILE_MODE9},
	{mmGB_TILE_MODE10},
	{mmGB_TILE_MODE11},
	{mmGB_TILE_MODE12},
	{mmGB_TILE_MODE13},
	{mmGB_TILE_MODE14},
	{mmGB_TILE_MODE15},
	{mmGB_TILE_MODE16},
	{mmGB_TILE_MODE17},
	{mmGB_TILE_MODE18},
	{mmGB_TILE_MODE19},
	{mmGB_TILE_MODE20},
	{mmGB_TILE_MODE21},
	{mmGB_TILE_MODE22},
	{mmGB_TILE_MODE23},
	{mmGB_TILE_MODE24},
	{mmGB_TILE_MODE25},
	{mmGB_TILE_MODE26},
	{mmGB_TILE_MODE27},
	{mmGB_TILE_MODE28},
	{mmGB_TILE_MODE29},
	{mmGB_TILE_MODE30},
	{mmGB_TILE_MODE31},
	{mmGB_MACROTILE_MODE0},
	{mmGB_MACROTILE_MODE1},
	{mmGB_MACROTILE_MODE2},
	{mmGB_MACROTILE_MODE3},
	{mmGB_MACROTILE_MODE4},
	{mmGB_MACROTILE_MODE5},
	{mmGB_MACROTILE_MODE6},
	{mmGB_MACROTILE_MODE7},
	{mmGB_MACROTILE_MODE8},
	{mmGB_MACROTILE_MODE9},
	{mmGB_MACROTILE_MODE10},
	{mmGB_MACROTILE_MODE11},
	{mmGB_MACROTILE_MODE12},
	{mmGB_MACROTILE_MODE13},
	{mmGB_MACROTILE_MODE14},
	{mmGB_MACROTILE_MODE15},
	{mmCC_RB_BACKEND_DISABLE, true},
	{mmGC_USER_RB_BACKEND_DISABLE, true},
	{mmGB_BACKEND_MAP, false},
	{mmPA_SC_RASTER_CONFIG, true},
	{mmPA_SC_RASTER_CONFIG_1, true},
};

static uint32_t vi_get_register_value(struct amdgpu_device *adev,
				      bool indexed, u32 se_num,
				      u32 sh_num, u32 reg_offset)
{
	if (indexed) {
		uint32_t val;
		unsigned se_idx = (se_num == 0xffffffff) ? 0 : se_num;
		unsigned sh_idx = (sh_num == 0xffffffff) ? 0 : sh_num;

		switch (reg_offset) {
		case mmCC_RB_BACKEND_DISABLE:
			return adev->gfx.config.rb_config[se_idx][sh_idx].rb_backend_disable;
		case mmGC_USER_RB_BACKEND_DISABLE:
			return adev->gfx.config.rb_config[se_idx][sh_idx].user_rb_backend_disable;
		case mmPA_SC_RASTER_CONFIG:
			return adev->gfx.config.rb_config[se_idx][sh_idx].raster_config;
		case mmPA_SC_RASTER_CONFIG_1:
			return adev->gfx.config.rb_config[se_idx][sh_idx].raster_config_1;
		}

		mutex_lock(&adev->grbm_idx_mutex);
		if (se_num != 0xffffffff || sh_num != 0xffffffff)
			amdgpu_gfx_select_se_sh(adev, se_num, sh_num, 0xffffffff);

		val = RREG32(reg_offset);

		if (se_num != 0xffffffff || sh_num != 0xffffffff)
			amdgpu_gfx_select_se_sh(adev, 0xffffffff, 0xffffffff, 0xffffffff);
		mutex_unlock(&adev->grbm_idx_mutex);
		return val;
	} else {
		unsigned idx;

		switch (reg_offset) {
		case mmGB_ADDR_CONFIG:
			return adev->gfx.config.gb_addr_config;
		case mmMC_ARB_RAMCFG:
			return adev->gfx.config.mc_arb_ramcfg;
		case mmGB_TILE_MODE0:
		case mmGB_TILE_MODE1:
		case mmGB_TILE_MODE2:
		case mmGB_TILE_MODE3:
		case mmGB_TILE_MODE4:
		case mmGB_TILE_MODE5:
		case mmGB_TILE_MODE6:
		case mmGB_TILE_MODE7:
		case mmGB_TILE_MODE8:
		case mmGB_TILE_MODE9:
		case mmGB_TILE_MODE10:
		case mmGB_TILE_MODE11:
		case mmGB_TILE_MODE12:
		case mmGB_TILE_MODE13:
		case mmGB_TILE_MODE14:
		case mmGB_TILE_MODE15:
		case mmGB_TILE_MODE16:
		case mmGB_TILE_MODE17:
		case mmGB_TILE_MODE18:
		case mmGB_TILE_MODE19:
		case mmGB_TILE_MODE20:
		case mmGB_TILE_MODE21:
		case mmGB_TILE_MODE22:
		case mmGB_TILE_MODE23:
		case mmGB_TILE_MODE24:
		case mmGB_TILE_MODE25:
		case mmGB_TILE_MODE26:
		case mmGB_TILE_MODE27:
		case mmGB_TILE_MODE28:
		case mmGB_TILE_MODE29:
		case mmGB_TILE_MODE30:
		case mmGB_TILE_MODE31:
			idx = (reg_offset - mmGB_TILE_MODE0);
			return adev->gfx.config.tile_mode_array[idx];
		case mmGB_MACROTILE_MODE0:
		case mmGB_MACROTILE_MODE1:
		case mmGB_MACROTILE_MODE2:
		case mmGB_MACROTILE_MODE3:
		case mmGB_MACROTILE_MODE4:
		case mmGB_MACROTILE_MODE5:
		case mmGB_MACROTILE_MODE6:
		case mmGB_MACROTILE_MODE7:
		case mmGB_MACROTILE_MODE8:
		case mmGB_MACROTILE_MODE9:
		case mmGB_MACROTILE_MODE10:
		case mmGB_MACROTILE_MODE11:
		case mmGB_MACROTILE_MODE12:
		case mmGB_MACROTILE_MODE13:
		case mmGB_MACROTILE_MODE14:
		case mmGB_MACROTILE_MODE15:
			idx = (reg_offset - mmGB_MACROTILE_MODE0);
			return adev->gfx.config.macrotile_mode_array[idx];
		default:
			return RREG32(reg_offset);
		}
	}
}

static int vi_read_register(struct amdgpu_device *adev, u32 se_num,
			    u32 sh_num, u32 reg_offset, u32 *value)
{
	uint32_t i;

	*value = 0;
	for (i = 0; i < ARRAY_SIZE(vi_allowed_read_registers); i++) {
		bool indexed = vi_allowed_read_registers[i].grbm_indexed;

		if (reg_offset != vi_allowed_read_registers[i].reg_offset)
			continue;

		*value = vi_get_register_value(adev, indexed, se_num, sh_num,
					       reg_offset);
		return 0;
	}
	return -EINVAL;
}

/**
 * vi_asic_pci_config_reset - soft reset GPU
 *
 * @adev: amdgpu_device pointer
 *
 * Use PCI Config method to reset the GPU.
 *
 * Returns 0 for success.
 */
static int vi_asic_pci_config_reset(struct amdgpu_device *adev)
{
	u32 i;
	int r = -EINVAL;

	amdgpu_atombios_scratch_regs_engine_hung(adev, true);

	/* disable BM */
	pci_clear_master(adev->pdev);
	/* reset */
	amdgpu_device_pci_config_reset(adev);

	udelay(100);

	/* wait for asic to come out of reset */
	for (i = 0; i < adev->usec_timeout; i++) {
		if (RREG32(mmCONFIG_MEMSIZE) != 0xffffffff) {
			/* enable BM */
			pci_set_master(adev->pdev);
			adev->has_hw_reset = true;
			r = 0;
			break;
		}
		udelay(1);
	}

	amdgpu_atombios_scratch_regs_engine_hung(adev, false);

	return r;
}

static bool vi_asic_supports_baco(struct amdgpu_device *adev)
{
	switch (adev->asic_type) {
	case CHIP_FIJI:
	case CHIP_TONGA:
	case CHIP_POLARIS10:
	case CHIP_POLARIS11:
	case CHIP_POLARIS12:
	case CHIP_TOPAZ:
		return amdgpu_dpm_is_baco_supported(adev);
	default:
		return false;
	}
}

static enum amd_reset_method
vi_asic_reset_method(struct amdgpu_device *adev)
{
	bool baco_reset;

	if (amdgpu_reset_method == AMD_RESET_METHOD_LEGACY ||
	    amdgpu_reset_method == AMD_RESET_METHOD_BACO)
		return amdgpu_reset_method;

	if (amdgpu_reset_method != -1)
		dev_warn(adev->dev, "Specified reset method:%d isn't supported, using AUTO instead.\n",
				  amdgpu_reset_method);

	switch (adev->asic_type) {
	case CHIP_FIJI:
	case CHIP_TONGA:
	case CHIP_POLARIS10:
	case CHIP_POLARIS11:
	case CHIP_POLARIS12:
	case CHIP_TOPAZ:
		baco_reset = amdgpu_dpm_is_baco_supported(adev);
		break;
	default:
		baco_reset = false;
		break;
	}

	if (baco_reset)
		return AMD_RESET_METHOD_BACO;
	else
		return AMD_RESET_METHOD_LEGACY;
}

/**
 * vi_asic_reset - soft reset GPU
 *
 * @adev: amdgpu_device pointer
 *
 * Look up which blocks are hung and attempt
 * to reset them.
 * Returns 0 for success.
 */
static int vi_asic_reset(struct amdgpu_device *adev)
{
	int r;

	/* APUs don't have full asic reset */
	if (adev->flags & AMD_IS_APU)
		return 0;

	if (vi_asic_reset_method(adev) == AMD_RESET_METHOD_BACO) {
		dev_info(adev->dev, "BACO reset\n");
		r = amdgpu_dpm_baco_reset(adev);
	} else {
		dev_info(adev->dev, "PCI CONFIG reset\n");
		r = vi_asic_pci_config_reset(adev);
	}

	return r;
}

static u32 vi_get_config_memsize(struct amdgpu_device *adev)
{
	return RREG32(mmCONFIG_MEMSIZE);
}

static int vi_set_uvd_clock(struct amdgpu_device *adev, u32 clock,
			u32 cntl_reg, u32 status_reg)
{
	int r, i;
	struct atom_clock_dividers dividers;
	uint32_t tmp;

	r = amdgpu_atombios_get_clock_dividers(adev,
					       COMPUTE_GPUCLK_INPUT_FLAG_DEFAULT_GPUCLK,
					       clock, false, &dividers);
	if (r)
		return r;

	tmp = RREG32_SMC(cntl_reg);

	if (adev->flags & AMD_IS_APU)
		tmp &= ~CG_DCLK_CNTL__DCLK_DIVIDER_MASK;
	else
		tmp &= ~(CG_DCLK_CNTL__DCLK_DIR_CNTL_EN_MASK |
				CG_DCLK_CNTL__DCLK_DIVIDER_MASK);
	tmp |= dividers.post_divider;
	WREG32_SMC(cntl_reg, tmp);

	for (i = 0; i < 100; i++) {
		tmp = RREG32_SMC(status_reg);
		if (adev->flags & AMD_IS_APU) {
			if (tmp & 0x10000)
				break;
		} else {
			if (tmp & CG_DCLK_STATUS__DCLK_STATUS_MASK)
				break;
		}
		mdelay(10);
	}
	if (i == 100)
		return -ETIMEDOUT;
	return 0;
}

#define ixGNB_CLK1_DFS_CNTL 0xD82200F0
#define ixGNB_CLK1_STATUS   0xD822010C
#define ixGNB_CLK2_DFS_CNTL 0xD8220110
#define ixGNB_CLK2_STATUS   0xD822012C
#define ixGNB_CLK3_DFS_CNTL 0xD8220130
#define ixGNB_CLK3_STATUS   0xD822014C

static int vi_set_uvd_clocks(struct amdgpu_device *adev, u32 vclk, u32 dclk)
{
	int r;

	if (adev->flags & AMD_IS_APU) {
		r = vi_set_uvd_clock(adev, vclk, ixGNB_CLK2_DFS_CNTL, ixGNB_CLK2_STATUS);
		if (r)
			return r;

		r = vi_set_uvd_clock(adev, dclk, ixGNB_CLK1_DFS_CNTL, ixGNB_CLK1_STATUS);
		if (r)
			return r;
	} else {
		r = vi_set_uvd_clock(adev, vclk, ixCG_VCLK_CNTL, ixCG_VCLK_STATUS);
		if (r)
			return r;

		r = vi_set_uvd_clock(adev, dclk, ixCG_DCLK_CNTL, ixCG_DCLK_STATUS);
		if (r)
			return r;
	}

	return 0;
}

static int vi_set_vce_clocks(struct amdgpu_device *adev, u32 evclk, u32 ecclk)
{
	int r, i;
	struct atom_clock_dividers dividers;
	u32 tmp;
	u32 reg_ctrl;
	u32 reg_status;
	u32 status_mask;
	u32 reg_mask;

	if (adev->flags & AMD_IS_APU) {
		reg_ctrl = ixGNB_CLK3_DFS_CNTL;
		reg_status = ixGNB_CLK3_STATUS;
		status_mask = 0x00010000;
		reg_mask = CG_ECLK_CNTL__ECLK_DIVIDER_MASK;
	} else {
		reg_ctrl = ixCG_ECLK_CNTL;
		reg_status = ixCG_ECLK_STATUS;
		status_mask = CG_ECLK_STATUS__ECLK_STATUS_MASK;
		reg_mask = CG_ECLK_CNTL__ECLK_DIR_CNTL_EN_MASK | CG_ECLK_CNTL__ECLK_DIVIDER_MASK;
	}

	r = amdgpu_atombios_get_clock_dividers(adev,
					       COMPUTE_GPUCLK_INPUT_FLAG_DEFAULT_GPUCLK,
					       ecclk, false, &dividers);
	if (r)
		return r;

	for (i = 0; i < 100; i++) {
		if (RREG32_SMC(reg_status) & status_mask)
			break;
		mdelay(10);
	}

	if (i == 100)
		return -ETIMEDOUT;

	tmp = RREG32_SMC(reg_ctrl);
	tmp &= ~reg_mask;
	tmp |= dividers.post_divider;
	WREG32_SMC(reg_ctrl, tmp);

	for (i = 0; i < 100; i++) {
		if (RREG32_SMC(reg_status) & status_mask)
			break;
		mdelay(10);
	}

	if (i == 100)
		return -ETIMEDOUT;

	return 0;
}

static void vi_pcie_gen3_enable(struct amdgpu_device *adev)
{
	if (pci_is_root_bus(adev->pdev->bus))
		return;

	if (amdgpu_pcie_gen2 == 0)
		return;

	if (adev->flags & AMD_IS_APU)
		return;

	if (!(adev->pm.pcie_gen_mask & (CAIL_PCIE_LINK_SPEED_SUPPORT_GEN2 |
					CAIL_PCIE_LINK_SPEED_SUPPORT_GEN3)))
		return;

	/* todo */
}

static void vi_enable_aspm(struct amdgpu_device *adev)
{
	u32 data, orig;

	orig = data = RREG32_PCIE(ixPCIE_LC_CNTL);
	data |= PCIE_LC_CNTL__LC_L0S_INACTIVITY_DEFAULT <<
			PCIE_LC_CNTL__LC_L0S_INACTIVITY__SHIFT;
	data |= PCIE_LC_CNTL__LC_L1_INACTIVITY_DEFAULT <<
			PCIE_LC_CNTL__LC_L1_INACTIVITY__SHIFT;
	data &= ~PCIE_LC_CNTL__LC_PMI_TO_L1_DIS_MASK;
	data |= PCIE_LC_CNTL__LC_DELAY_L1_EXIT_MASK;
	if (orig != data)
		WREG32_PCIE(ixPCIE_LC_CNTL, data);
}

static bool aspm_support_quirk_check(void)
{
#if IS_ENABLED(CONFIG_X86)
	struct cpuinfo_x86 *c = &cpu_data(0);

	return !(c->x86 == 6 && c->x86_model == INTEL_FAM6_ALDERLAKE);
#else
	return true;
#endif
}

static void vi_program_aspm(struct amdgpu_device *adev)
{
	u32 data, data1, orig;
	bool bL1SS = false;
	bool bClkReqSupport = true;

	if (!amdgpu_device_should_use_aspm(adev) || !aspm_support_quirk_check())
		return;

	if (adev->flags & AMD_IS_APU ||
	    adev->asic_type < CHIP_POLARIS10)
		return;

	orig = data = RREG32_PCIE(ixPCIE_LC_CNTL);
	data &= ~PCIE_LC_CNTL__LC_L1_INACTIVITY_MASK;
	data &= ~PCIE_LC_CNTL__LC_L0S_INACTIVITY_MASK;
	data |= PCIE_LC_CNTL__LC_PMI_TO_L1_DIS_MASK;
	if (orig != data)
		WREG32_PCIE(ixPCIE_LC_CNTL, data);

	orig = data = RREG32_PCIE(ixPCIE_LC_N_FTS_CNTL);
	data &= ~PCIE_LC_N_FTS_CNTL__LC_XMIT_N_FTS_MASK;
	data |= 0x0024 << PCIE_LC_N_FTS_CNTL__LC_XMIT_N_FTS__SHIFT;
	data |= PCIE_LC_N_FTS_CNTL__LC_XMIT_N_FTS_OVERRIDE_EN_MASK;
	if (orig != data)
		WREG32_PCIE(ixPCIE_LC_N_FTS_CNTL, data);

	orig = data = RREG32_PCIE(ixPCIE_LC_CNTL3);
	data |= PCIE_LC_CNTL3__LC_GO_TO_RECOVERY_MASK;
	if (orig != data)
		WREG32_PCIE(ixPCIE_LC_CNTL3, data);

	orig = data = RREG32_PCIE(ixPCIE_P_CNTL);
	data |= PCIE_P_CNTL__P_IGNORE_EDB_ERR_MASK;
	if (orig != data)
		WREG32_PCIE(ixPCIE_P_CNTL, data);

	data = RREG32_PCIE(ixPCIE_LC_L1_PM_SUBSTATE);
	pci_read_config_dword(adev->pdev, PCIE_L1_PM_SUB_CNTL, &data1);
	if (data & PCIE_LC_L1_PM_SUBSTATE__LC_L1_SUBSTATES_OVERRIDE_EN_MASK &&
	    (data & (PCIE_LC_L1_PM_SUBSTATE__LC_PCI_PM_L1_2_OVERRIDE_MASK |
		    PCIE_LC_L1_PM_SUBSTATE__LC_PCI_PM_L1_1_OVERRIDE_MASK |
			PCIE_LC_L1_PM_SUBSTATE__LC_ASPM_L1_2_OVERRIDE_MASK |
			PCIE_LC_L1_PM_SUBSTATE__LC_ASPM_L1_1_OVERRIDE_MASK))) {
		bL1SS = true;
	} else if (data1 & (PCIE_L1_PM_SUB_CNTL__ASPM_L1_2_EN_MASK |
	    PCIE_L1_PM_SUB_CNTL__ASPM_L1_1_EN_MASK |
	    PCIE_L1_PM_SUB_CNTL__PCI_PM_L1_2_EN_MASK |
	    PCIE_L1_PM_SUB_CNTL__PCI_PM_L1_1_EN_MASK)) {
		bL1SS = true;
	}

	orig = data = RREG32_PCIE(ixPCIE_LC_CNTL6);
	data |= PCIE_LC_CNTL6__LC_L1_POWERDOWN_MASK;
	if (orig != data)
		WREG32_PCIE(ixPCIE_LC_CNTL6, data);

	orig = data = RREG32_PCIE(ixPCIE_LC_LINK_WIDTH_CNTL);
	data |= PCIE_LC_LINK_WIDTH_CNTL__LC_DYN_LANES_PWR_STATE_MASK;
	if (orig != data)
		WREG32_PCIE(ixPCIE_LC_LINK_WIDTH_CNTL, data);

	pci_read_config_dword(adev->pdev, LINK_CAP, &data);
	if (!(data & PCIE_LINK_CAP__CLOCK_POWER_MANAGEMENT_MASK))
		bClkReqSupport = false;

	if (bClkReqSupport) {
		orig = data = RREG32_SMC(ixTHM_CLK_CNTL);
		data &= ~(THM_CLK_CNTL__CMON_CLK_SEL_MASK | THM_CLK_CNTL__TMON_CLK_SEL_MASK);
		data |= (1 << THM_CLK_CNTL__CMON_CLK_SEL__SHIFT) |
				(1 << THM_CLK_CNTL__TMON_CLK_SEL__SHIFT);
		if (orig != data)
			WREG32_SMC(ixTHM_CLK_CNTL, data);

		orig = data = RREG32_SMC(ixMISC_CLK_CTRL);
		data &= ~(MISC_CLK_CTRL__DEEP_SLEEP_CLK_SEL_MASK |
			MISC_CLK_CTRL__ZCLK_SEL_MASK | MISC_CLK_CTRL__DFT_SMS_PG_CLK_SEL_MASK);
		data |= (1 << MISC_CLK_CTRL__DEEP_SLEEP_CLK_SEL__SHIFT) |
				(1 << MISC_CLK_CTRL__ZCLK_SEL__SHIFT);
		data |= (0x20 << MISC_CLK_CTRL__DFT_SMS_PG_CLK_SEL__SHIFT);
		if (orig != data)
			WREG32_SMC(ixMISC_CLK_CTRL, data);

		orig = data = RREG32_SMC(ixCG_CLKPIN_CNTL);
		data |= CG_CLKPIN_CNTL__XTALIN_DIVIDE_MASK;
		if (orig != data)
			WREG32_SMC(ixCG_CLKPIN_CNTL, data);

		orig = data = RREG32_SMC(ixCG_CLKPIN_CNTL_2);
		data |= CG_CLKPIN_CNTL_2__ENABLE_XCLK_MASK;
		if (orig != data)
			WREG32_SMC(ixCG_CLKPIN_CNTL, data);

		orig = data = RREG32_SMC(ixMPLL_BYPASSCLK_SEL);
		data &= ~MPLL_BYPASSCLK_SEL__MPLL_CLKOUT_SEL_MASK;
		data |= (4 << MPLL_BYPASSCLK_SEL__MPLL_CLKOUT_SEL__SHIFT);
		if (orig != data)
			WREG32_SMC(ixMPLL_BYPASSCLK_SEL, data);

		orig = data = RREG32_PCIE(ixCPM_CONTROL);
		data |= (CPM_CONTROL__REFCLK_XSTCLK_ENABLE_MASK |
				CPM_CONTROL__CLKREQb_UNGATE_TXCLK_ENABLE_MASK);
		if (orig != data)
			WREG32_PCIE(ixCPM_CONTROL, data);

		orig = data = RREG32_PCIE(ixPCIE_CONFIG_CNTL);
		data &= ~PCIE_CONFIG_CNTL__DYN_CLK_LATENCY_MASK;
		data |= (0xE << PCIE_CONFIG_CNTL__DYN_CLK_LATENCY__SHIFT);
		if (orig != data)
			WREG32_PCIE(ixPCIE_CONFIG_CNTL, data);

		orig = data = RREG32(mmBIF_CLK_CTRL);
		data |= BIF_CLK_CTRL__BIF_XSTCLK_READY_MASK;
		if (orig != data)
			WREG32(mmBIF_CLK_CTRL, data);

		orig = data = RREG32_PCIE(ixPCIE_LC_CNTL7);
		data |= PCIE_LC_CNTL7__LC_L1_SIDEBAND_CLKREQ_PDWN_EN_MASK;
		if (orig != data)
			WREG32_PCIE(ixPCIE_LC_CNTL7, data);

		orig = data = RREG32_PCIE(ixPCIE_HW_DEBUG);
		data |= PCIE_HW_DEBUG__HW_01_DEBUG_MASK;
		if (orig != data)
			WREG32_PCIE(ixPCIE_HW_DEBUG, data);

		orig = data = RREG32_PCIE(ixPCIE_LC_CNTL2);
		data |= PCIE_LC_CNTL2__LC_ALLOW_PDWN_IN_L23_MASK;
		data |= PCIE_LC_CNTL2__LC_ALLOW_PDWN_IN_L1_MASK;
		if (bL1SS)
			data &= ~PCIE_LC_CNTL2__LC_ALLOW_PDWN_IN_L1_MASK;
		if (orig != data)
			WREG32_PCIE(ixPCIE_LC_CNTL2, data);

	}

	vi_enable_aspm(adev);

	data = RREG32_PCIE(ixPCIE_LC_N_FTS_CNTL);
	data1 = RREG32_PCIE(ixPCIE_LC_STATUS1);
	if (((data & PCIE_LC_N_FTS_CNTL__LC_N_FTS_MASK) == PCIE_LC_N_FTS_CNTL__LC_N_FTS_MASK) &&
	    data1 & PCIE_LC_STATUS1__LC_REVERSE_XMIT_MASK &&
	    data1 & PCIE_LC_STATUS1__LC_REVERSE_RCVR_MASK) {
		orig = data = RREG32_PCIE(ixPCIE_LC_CNTL);
		data &= ~PCIE_LC_CNTL__LC_L0S_INACTIVITY_MASK;
		if (orig != data)
			WREG32_PCIE(ixPCIE_LC_CNTL, data);
	}

	if ((adev->asic_type == CHIP_POLARIS12 &&
	    !(ASICID_IS_P23(adev->pdev->device, adev->pdev->revision))) ||
	    ASIC_IS_P22(adev->asic_type, adev->external_rev_id)) {
		orig = data = RREG32_PCIE(ixPCIE_LC_TRAINING_CNTL);
		data &= ~PCIE_LC_TRAINING_CNTL__LC_DISABLE_TRAINING_BIT_ARCH_MASK;
		if (orig != data)
			WREG32_PCIE(ixPCIE_LC_TRAINING_CNTL, data);
	}
}

static void vi_enable_doorbell_aperture(struct amdgpu_device *adev,
					bool enable)
{
	u32 tmp;

	/* not necessary on CZ */
	if (adev->flags & AMD_IS_APU)
		return;

	tmp = RREG32(mmBIF_DOORBELL_APER_EN);
	if (enable)
		tmp = REG_SET_FIELD(tmp, BIF_DOORBELL_APER_EN, BIF_DOORBELL_APER_EN, 1);
	else
		tmp = REG_SET_FIELD(tmp, BIF_DOORBELL_APER_EN, BIF_DOORBELL_APER_EN, 0);

	WREG32(mmBIF_DOORBELL_APER_EN, tmp);
}

#define ATI_REV_ID_FUSE_MACRO__ADDRESS      0xC0014044
#define ATI_REV_ID_FUSE_MACRO__SHIFT        9
#define ATI_REV_ID_FUSE_MACRO__MASK         0x00001E00

static uint32_t vi_get_rev_id(struct amdgpu_device *adev)
{
	if (adev->flags & AMD_IS_APU)
		return (RREG32_SMC(ATI_REV_ID_FUSE_MACRO__ADDRESS) & ATI_REV_ID_FUSE_MACRO__MASK)
			>> ATI_REV_ID_FUSE_MACRO__SHIFT;
	else
		return (RREG32(mmPCIE_EFUSE4) & PCIE_EFUSE4__STRAP_BIF_ATI_REV_ID_MASK)
			>> PCIE_EFUSE4__STRAP_BIF_ATI_REV_ID__SHIFT;
}

static void vi_flush_hdp(struct amdgpu_device *adev, struct amdgpu_ring *ring)
{
	if (!ring || !ring->funcs->emit_wreg) {
		WREG32(mmHDP_MEM_COHERENCY_FLUSH_CNTL, 1);
		RREG32(mmHDP_MEM_COHERENCY_FLUSH_CNTL);
	} else {
		amdgpu_ring_emit_wreg(ring, mmHDP_MEM_COHERENCY_FLUSH_CNTL, 1);
	}
}

static void vi_invalidate_hdp(struct amdgpu_device *adev,
			      struct amdgpu_ring *ring)
{
	if (!ring || !ring->funcs->emit_wreg) {
		WREG32(mmHDP_DEBUG0, 1);
		RREG32(mmHDP_DEBUG0);
	} else {
		amdgpu_ring_emit_wreg(ring, mmHDP_DEBUG0, 1);
	}
}

static bool vi_need_full_reset(struct amdgpu_device *adev)
{
	switch (adev->asic_type) {
	case CHIP_CARRIZO:
	case CHIP_STONEY:
		/* CZ has hang issues with full reset at the moment */
		return false;
	case CHIP_FIJI:
	case CHIP_TONGA:
		/* XXX: soft reset should work on fiji and tonga */
		return true;
	case CHIP_POLARIS10:
	case CHIP_POLARIS11:
	case CHIP_POLARIS12:
	case CHIP_TOPAZ:
	default:
		/* change this when we support soft reset */
		return true;
	}
}

static void vi_get_pcie_usage(struct amdgpu_device *adev, uint64_t *count0,
			      uint64_t *count1)
{
	uint32_t perfctr = 0;
	uint64_t cnt0_of, cnt1_of;
	int tmp;

	/* This reports 0 on APUs, so return to avoid writing/reading registers
	 * that may or may not be different from their GPU counterparts
	 */
	if (adev->flags & AMD_IS_APU)
		return;

	/* Set the 2 events that we wish to watch, defined above */
	/* Reg 40 is # received msgs, Reg 104 is # of posted requests sent */
	perfctr = REG_SET_FIELD(perfctr, PCIE_PERF_CNTL_TXCLK, EVENT0_SEL, 40);
	perfctr = REG_SET_FIELD(perfctr, PCIE_PERF_CNTL_TXCLK, EVENT1_SEL, 104);

	/* Write to enable desired perf counters */
	WREG32_PCIE(ixPCIE_PERF_CNTL_TXCLK, perfctr);
	/* Zero out and enable the perf counters
	 * Write 0x5:
	 * Bit 0 = Start all counters(1)
	 * Bit 2 = Global counter reset enable(1)
	 */
	WREG32_PCIE(ixPCIE_PERF_COUNT_CNTL, 0x00000005);

	msleep(1000);

	/* Load the shadow and disable the perf counters
	 * Write 0x2:
	 * Bit 0 = Stop counters(0)
	 * Bit 1 = Load the shadow counters(1)
	 */
	WREG32_PCIE(ixPCIE_PERF_COUNT_CNTL, 0x00000002);

	/* Read register values to get any >32bit overflow */
	tmp = RREG32_PCIE(ixPCIE_PERF_CNTL_TXCLK);
	cnt0_of = REG_GET_FIELD(tmp, PCIE_PERF_CNTL_TXCLK, COUNTER0_UPPER);
	cnt1_of = REG_GET_FIELD(tmp, PCIE_PERF_CNTL_TXCLK, COUNTER1_UPPER);

	/* Get the values and add the overflow */
	*count0 = RREG32_PCIE(ixPCIE_PERF_COUNT0_TXCLK) | (cnt0_of << 32);
	*count1 = RREG32_PCIE(ixPCIE_PERF_COUNT1_TXCLK) | (cnt1_of << 32);
}

static uint64_t vi_get_pcie_replay_count(struct amdgpu_device *adev)
{
	uint64_t nak_r, nak_g;

	/* Get the number of NAKs received and generated */
	nak_r = RREG32_PCIE(ixPCIE_RX_NUM_NAK);
	nak_g = RREG32_PCIE(ixPCIE_RX_NUM_NAK_GENERATED);

	/* Add the total number of NAKs, i.e the number of replays */
	return (nak_r + nak_g);
}

static bool vi_need_reset_on_init(struct amdgpu_device *adev)
{
	u32 clock_cntl, pc;

	if (adev->flags & AMD_IS_APU)
		return false;

	/* check if the SMC is already running */
	clock_cntl = RREG32_SMC(ixSMC_SYSCON_CLOCK_CNTL_0);
	pc = RREG32_SMC(ixSMC_PC_C);
	if ((0 == REG_GET_FIELD(clock_cntl, SMC_SYSCON_CLOCK_CNTL_0, ck_disable)) &&
	    (0x20100 <= pc))
		return true;

	return false;
}

static void vi_pre_asic_init(struct amdgpu_device *adev)
{
}

static const struct amdgpu_asic_funcs vi_asic_funcs =
{
	.read_disabled_bios = &vi_read_disabled_bios,
	.read_bios_from_rom = &vi_read_bios_from_rom,
	.read_register = &vi_read_register,
	.reset = &vi_asic_reset,
	.reset_method = &vi_asic_reset_method,
	.set_vga_state = &vi_vga_set_state,
	.get_xclk = &vi_get_xclk,
	.set_uvd_clocks = &vi_set_uvd_clocks,
	.set_vce_clocks = &vi_set_vce_clocks,
	.get_config_memsize = &vi_get_config_memsize,
	.flush_hdp = &vi_flush_hdp,
	.invalidate_hdp = &vi_invalidate_hdp,
	.need_full_reset = &vi_need_full_reset,
	.init_doorbell_index = &legacy_doorbell_index_init,
	.get_pcie_usage = &vi_get_pcie_usage,
	.need_reset_on_init = &vi_need_reset_on_init,
	.get_pcie_replay_count = &vi_get_pcie_replay_count,
	.supports_baco = &vi_asic_supports_baco,
	.pre_asic_init = &vi_pre_asic_init,
	.query_video_codecs = &vi_query_video_codecs,
};

#define CZ_REV_BRISTOL(rev)	 \
	((rev >= 0xC8 && rev <= 0xCE) || (rev >= 0xE1 && rev <= 0xE6))

static int vi_common_early_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (adev->flags & AMD_IS_APU) {
		adev->smc_rreg = &cz_smc_rreg;
		adev->smc_wreg = &cz_smc_wreg;
	} else {
		adev->smc_rreg = &vi_smc_rreg;
		adev->smc_wreg = &vi_smc_wreg;
	}
	adev->pcie_rreg = &vi_pcie_rreg;
	adev->pcie_wreg = &vi_pcie_wreg;
	adev->uvd_ctx_rreg = &vi_uvd_ctx_rreg;
	adev->uvd_ctx_wreg = &vi_uvd_ctx_wreg;
	adev->didt_rreg = &vi_didt_rreg;
	adev->didt_wreg = &vi_didt_wreg;
	adev->gc_cac_rreg = &vi_gc_cac_rreg;
	adev->gc_cac_wreg = &vi_gc_cac_wreg;

	adev->asic_funcs = &vi_asic_funcs;

	adev->rev_id = vi_get_rev_id(adev);
	adev->external_rev_id = 0xFF;
	switch (adev->asic_type) {
	case CHIP_TOPAZ:
		adev->cg_flags = 0;
		adev->pg_flags = 0;
		adev->external_rev_id = 0x1;
		break;
	case CHIP_FIJI:
		adev->cg_flags = AMD_CG_SUPPORT_GFX_MGCG |
			AMD_CG_SUPPORT_GFX_MGLS |
			AMD_CG_SUPPORT_GFX_RLC_LS |
			AMD_CG_SUPPORT_GFX_CP_LS |
			AMD_CG_SUPPORT_GFX_CGTS |
			AMD_CG_SUPPORT_GFX_CGTS_LS |
			AMD_CG_SUPPORT_GFX_CGCG |
			AMD_CG_SUPPORT_GFX_CGLS |
			AMD_CG_SUPPORT_SDMA_MGCG |
			AMD_CG_SUPPORT_SDMA_LS |
			AMD_CG_SUPPORT_BIF_LS |
			AMD_CG_SUPPORT_HDP_MGCG |
			AMD_CG_SUPPORT_HDP_LS |
			AMD_CG_SUPPORT_ROM_MGCG |
			AMD_CG_SUPPORT_MC_MGCG |
			AMD_CG_SUPPORT_MC_LS |
			AMD_CG_SUPPORT_UVD_MGCG;
		adev->pg_flags = 0;
		adev->external_rev_id = adev->rev_id + 0x3c;
		break;
	case CHIP_TONGA:
		adev->cg_flags = AMD_CG_SUPPORT_GFX_MGCG |
			AMD_CG_SUPPORT_GFX_CGCG |
			AMD_CG_SUPPORT_GFX_CGLS |
			AMD_CG_SUPPORT_SDMA_MGCG |
			AMD_CG_SUPPORT_SDMA_LS |
			AMD_CG_SUPPORT_BIF_LS |
			AMD_CG_SUPPORT_HDP_MGCG |
			AMD_CG_SUPPORT_HDP_LS |
			AMD_CG_SUPPORT_ROM_MGCG |
			AMD_CG_SUPPORT_MC_MGCG |
			AMD_CG_SUPPORT_MC_LS |
			AMD_CG_SUPPORT_DRM_LS |
			AMD_CG_SUPPORT_UVD_MGCG;
		adev->pg_flags = 0;
		adev->external_rev_id = adev->rev_id + 0x14;
		break;
	case CHIP_POLARIS11:
		adev->cg_flags = AMD_CG_SUPPORT_GFX_MGCG |
			AMD_CG_SUPPORT_GFX_RLC_LS |
			AMD_CG_SUPPORT_GFX_CP_LS |
			AMD_CG_SUPPORT_GFX_CGCG |
			AMD_CG_SUPPORT_GFX_CGLS |
			AMD_CG_SUPPORT_GFX_3D_CGCG |
			AMD_CG_SUPPORT_GFX_3D_CGLS |
			AMD_CG_SUPPORT_SDMA_MGCG |
			AMD_CG_SUPPORT_SDMA_LS |
			AMD_CG_SUPPORT_BIF_MGCG |
			AMD_CG_SUPPORT_BIF_LS |
			AMD_CG_SUPPORT_HDP_MGCG |
			AMD_CG_SUPPORT_HDP_LS |
			AMD_CG_SUPPORT_ROM_MGCG |
			AMD_CG_SUPPORT_MC_MGCG |
			AMD_CG_SUPPORT_MC_LS |
			AMD_CG_SUPPORT_DRM_LS |
			AMD_CG_SUPPORT_UVD_MGCG |
			AMD_CG_SUPPORT_VCE_MGCG;
		adev->pg_flags = 0;
		adev->external_rev_id = adev->rev_id + 0x5A;
		break;
	case CHIP_POLARIS10:
		adev->cg_flags = AMD_CG_SUPPORT_GFX_MGCG |
			AMD_CG_SUPPORT_GFX_RLC_LS |
			AMD_CG_SUPPORT_GFX_CP_LS |
			AMD_CG_SUPPORT_GFX_CGCG |
			AMD_CG_SUPPORT_GFX_CGLS |
			AMD_CG_SUPPORT_GFX_3D_CGCG |
			AMD_CG_SUPPORT_GFX_3D_CGLS |
			AMD_CG_SUPPORT_SDMA_MGCG |
			AMD_CG_SUPPORT_SDMA_LS |
			AMD_CG_SUPPORT_BIF_MGCG |
			AMD_CG_SUPPORT_BIF_LS |
			AMD_CG_SUPPORT_HDP_MGCG |
			AMD_CG_SUPPORT_HDP_LS |
			AMD_CG_SUPPORT_ROM_MGCG |
			AMD_CG_SUPPORT_MC_MGCG |
			AMD_CG_SUPPORT_MC_LS |
			AMD_CG_SUPPORT_DRM_LS |
			AMD_CG_SUPPORT_UVD_MGCG |
			AMD_CG_SUPPORT_VCE_MGCG;
		adev->pg_flags = 0;
		adev->external_rev_id = adev->rev_id + 0x50;
		break;
	case CHIP_POLARIS12:
		adev->cg_flags = AMD_CG_SUPPORT_GFX_MGCG |
			AMD_CG_SUPPORT_GFX_RLC_LS |
			AMD_CG_SUPPORT_GFX_CP_LS |
			AMD_CG_SUPPORT_GFX_CGCG |
			AMD_CG_SUPPORT_GFX_CGLS |
			AMD_CG_SUPPORT_GFX_3D_CGCG |
			AMD_CG_SUPPORT_GFX_3D_CGLS |
			AMD_CG_SUPPORT_SDMA_MGCG |
			AMD_CG_SUPPORT_SDMA_LS |
			AMD_CG_SUPPORT_BIF_MGCG |
			AMD_CG_SUPPORT_BIF_LS |
			AMD_CG_SUPPORT_HDP_MGCG |
			AMD_CG_SUPPORT_HDP_LS |
			AMD_CG_SUPPORT_ROM_MGCG |
			AMD_CG_SUPPORT_MC_MGCG |
			AMD_CG_SUPPORT_MC_LS |
			AMD_CG_SUPPORT_DRM_LS |
			AMD_CG_SUPPORT_UVD_MGCG |
			AMD_CG_SUPPORT_VCE_MGCG;
		adev->pg_flags = 0;
		adev->external_rev_id = adev->rev_id + 0x64;
		break;
	case CHIP_VEGAM:
		adev->cg_flags = 0;
			/*AMD_CG_SUPPORT_GFX_MGCG |
			AMD_CG_SUPPORT_GFX_RLC_LS |
			AMD_CG_SUPPORT_GFX_CP_LS |
			AMD_CG_SUPPORT_GFX_CGCG |
			AMD_CG_SUPPORT_GFX_CGLS |
			AMD_CG_SUPPORT_GFX_3D_CGCG |
			AMD_CG_SUPPORT_GFX_3D_CGLS |
			AMD_CG_SUPPORT_SDMA_MGCG |
			AMD_CG_SUPPORT_SDMA_LS |
			AMD_CG_SUPPORT_BIF_MGCG |
			AMD_CG_SUPPORT_BIF_LS |
			AMD_CG_SUPPORT_HDP_MGCG |
			AMD_CG_SUPPORT_HDP_LS |
			AMD_CG_SUPPORT_ROM_MGCG |
			AMD_CG_SUPPORT_MC_MGCG |
			AMD_CG_SUPPORT_MC_LS |
			AMD_CG_SUPPORT_DRM_LS |
			AMD_CG_SUPPORT_UVD_MGCG |
			AMD_CG_SUPPORT_VCE_MGCG;*/
		adev->pg_flags = 0;
		adev->external_rev_id = adev->rev_id + 0x6E;
		break;
	case CHIP_CARRIZO:
		adev->cg_flags = AMD_CG_SUPPORT_UVD_MGCG |
			AMD_CG_SUPPORT_GFX_MGCG |
			AMD_CG_SUPPORT_GFX_MGLS |
			AMD_CG_SUPPORT_GFX_RLC_LS |
			AMD_CG_SUPPORT_GFX_CP_LS |
			AMD_CG_SUPPORT_GFX_CGTS |
			AMD_CG_SUPPORT_GFX_CGTS_LS |
			AMD_CG_SUPPORT_GFX_CGCG |
			AMD_CG_SUPPORT_GFX_CGLS |
			AMD_CG_SUPPORT_BIF_LS |
			AMD_CG_SUPPORT_HDP_MGCG |
			AMD_CG_SUPPORT_HDP_LS |
			AMD_CG_SUPPORT_SDMA_MGCG |
			AMD_CG_SUPPORT_SDMA_LS |
			AMD_CG_SUPPORT_VCE_MGCG;
		/* rev0 hardware requires workarounds to support PG */
		adev->pg_flags = 0;
		if (adev->rev_id != 0x00 || CZ_REV_BRISTOL(adev->pdev->revision)) {
			adev->pg_flags |= AMD_PG_SUPPORT_GFX_SMG |
				AMD_PG_SUPPORT_GFX_PIPELINE |
				AMD_PG_SUPPORT_CP |
				AMD_PG_SUPPORT_UVD |
				AMD_PG_SUPPORT_VCE;
		}
		adev->external_rev_id = adev->rev_id + 0x1;
		break;
	case CHIP_STONEY:
		adev->cg_flags = AMD_CG_SUPPORT_UVD_MGCG |
			AMD_CG_SUPPORT_GFX_MGCG |
			AMD_CG_SUPPORT_GFX_MGLS |
			AMD_CG_SUPPORT_GFX_RLC_LS |
			AMD_CG_SUPPORT_GFX_CP_LS |
			AMD_CG_SUPPORT_GFX_CGTS |
			AMD_CG_SUPPORT_GFX_CGTS_LS |
			AMD_CG_SUPPORT_GFX_CGLS |
			AMD_CG_SUPPORT_BIF_LS |
			AMD_CG_SUPPORT_HDP_MGCG |
			AMD_CG_SUPPORT_HDP_LS |
			AMD_CG_SUPPORT_SDMA_MGCG |
			AMD_CG_SUPPORT_SDMA_LS |
			AMD_CG_SUPPORT_VCE_MGCG;
		adev->pg_flags = AMD_PG_SUPPORT_GFX_PG |
			AMD_PG_SUPPORT_GFX_SMG |
			AMD_PG_SUPPORT_GFX_PIPELINE |
			AMD_PG_SUPPORT_CP |
			AMD_PG_SUPPORT_UVD |
			AMD_PG_SUPPORT_VCE;
		adev->external_rev_id = adev->rev_id + 0x61;
		break;
	default:
		/* FIXME: not supported yet */
		return -EINVAL;
	}

	if (amdgpu_sriov_vf(adev)) {
		amdgpu_virt_init_setting(adev);
		xgpu_vi_mailbox_set_irq_funcs(adev);
	}

	return 0;
}

static int vi_common_late_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (amdgpu_sriov_vf(adev))
		xgpu_vi_mailbox_get_irq(adev);

	return 0;
}

static int vi_common_sw_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (amdgpu_sriov_vf(adev))
		xgpu_vi_mailbox_add_irq_id(adev);

	return 0;
}

static int vi_common_sw_fini(void *handle)
{
	return 0;
}

static int vi_common_hw_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	/* move the golden regs per IP block */
	vi_init_golden_registers(adev);
	/* enable pcie gen2/3 link */
	vi_pcie_gen3_enable(adev);
	/* enable aspm */
	vi_program_aspm(adev);
	/* enable the doorbell aperture */
	vi_enable_doorbell_aperture(adev, true);

	return 0;
}

static int vi_common_hw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	/* enable the doorbell aperture */
	vi_enable_doorbell_aperture(adev, false);

	if (amdgpu_sriov_vf(adev))
		xgpu_vi_mailbox_put_irq(adev);

	return 0;
}

static int vi_common_suspend(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	return vi_common_hw_fini(adev);
}

static int vi_common_resume(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	return vi_common_hw_init(adev);
}

static bool vi_common_is_idle(void *handle)
{
	return true;
}

static int vi_common_wait_for_idle(void *handle)
{
	return 0;
}

static int vi_common_soft_reset(void *handle)
{
	return 0;
}

static void vi_update_bif_medium_grain_light_sleep(struct amdgpu_device *adev,
						   bool enable)
{
	uint32_t temp, data;

	temp = data = RREG32_PCIE(ixPCIE_CNTL2);

	if (enable && (adev->cg_flags & AMD_CG_SUPPORT_BIF_LS))
		data |= PCIE_CNTL2__SLV_MEM_LS_EN_MASK |
				PCIE_CNTL2__MST_MEM_LS_EN_MASK |
				PCIE_CNTL2__REPLAY_MEM_LS_EN_MASK;
	else
		data &= ~(PCIE_CNTL2__SLV_MEM_LS_EN_MASK |
				PCIE_CNTL2__MST_MEM_LS_EN_MASK |
				PCIE_CNTL2__REPLAY_MEM_LS_EN_MASK);

	if (temp != data)
		WREG32_PCIE(ixPCIE_CNTL2, data);
}

static void vi_update_hdp_medium_grain_clock_gating(struct amdgpu_device *adev,
						    bool enable)
{
	uint32_t temp, data;

	temp = data = RREG32(mmHDP_HOST_PATH_CNTL);

	if (enable && (adev->cg_flags & AMD_CG_SUPPORT_HDP_MGCG))
		data &= ~HDP_HOST_PATH_CNTL__CLOCK_GATING_DIS_MASK;
	else
		data |= HDP_HOST_PATH_CNTL__CLOCK_GATING_DIS_MASK;

	if (temp != data)
		WREG32(mmHDP_HOST_PATH_CNTL, data);
}

static void vi_update_hdp_light_sleep(struct amdgpu_device *adev,
				      bool enable)
{
	uint32_t temp, data;

	temp = data = RREG32(mmHDP_MEM_POWER_LS);

	if (enable && (adev->cg_flags & AMD_CG_SUPPORT_HDP_LS))
		data |= HDP_MEM_POWER_LS__LS_ENABLE_MASK;
	else
		data &= ~HDP_MEM_POWER_LS__LS_ENABLE_MASK;

	if (temp != data)
		WREG32(mmHDP_MEM_POWER_LS, data);
}

static void vi_update_drm_light_sleep(struct amdgpu_device *adev,
				      bool enable)
{
	uint32_t temp, data;

	temp = data = RREG32(0x157a);

	if (enable && (adev->cg_flags & AMD_CG_SUPPORT_DRM_LS))
		data |= 1;
	else
		data &= ~1;

	if (temp != data)
		WREG32(0x157a, data);
}


static void vi_update_rom_medium_grain_clock_gating(struct amdgpu_device *adev,
						    bool enable)
{
	uint32_t temp, data;

	temp = data = RREG32_SMC(ixCGTT_ROM_CLK_CTRL0);

	if (enable && (adev->cg_flags & AMD_CG_SUPPORT_ROM_MGCG))
		data &= ~(CGTT_ROM_CLK_CTRL0__SOFT_OVERRIDE0_MASK |
				CGTT_ROM_CLK_CTRL0__SOFT_OVERRIDE1_MASK);
	else
		data |= CGTT_ROM_CLK_CTRL0__SOFT_OVERRIDE0_MASK |
				CGTT_ROM_CLK_CTRL0__SOFT_OVERRIDE1_MASK;

	if (temp != data)
		WREG32_SMC(ixCGTT_ROM_CLK_CTRL0, data);
}

static int vi_common_set_clockgating_state_by_smu(void *handle,
					   enum amd_clockgating_state state)
{
	uint32_t msg_id, pp_state = 0;
	uint32_t pp_support_state = 0;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (adev->cg_flags & (AMD_CG_SUPPORT_MC_LS | AMD_CG_SUPPORT_MC_MGCG)) {
		if (adev->cg_flags & AMD_CG_SUPPORT_MC_LS) {
			pp_support_state = PP_STATE_SUPPORT_LS;
			pp_state = PP_STATE_LS;
		}
		if (adev->cg_flags & AMD_CG_SUPPORT_MC_MGCG) {
			pp_support_state |= PP_STATE_SUPPORT_CG;
			pp_state |= PP_STATE_CG;
		}
		if (state == AMD_CG_STATE_UNGATE)
			pp_state = 0;
		msg_id = PP_CG_MSG_ID(PP_GROUP_SYS,
			       PP_BLOCK_SYS_MC,
			       pp_support_state,
			       pp_state);
		amdgpu_dpm_set_clockgating_by_smu(adev, msg_id);
	}

	if (adev->cg_flags & (AMD_CG_SUPPORT_SDMA_LS | AMD_CG_SUPPORT_SDMA_MGCG)) {
		if (adev->cg_flags & AMD_CG_SUPPORT_SDMA_LS) {
			pp_support_state = PP_STATE_SUPPORT_LS;
			pp_state = PP_STATE_LS;
		}
		if (adev->cg_flags & AMD_CG_SUPPORT_SDMA_MGCG) {
			pp_support_state |= PP_STATE_SUPPORT_CG;
			pp_state |= PP_STATE_CG;
		}
		if (state == AMD_CG_STATE_UNGATE)
			pp_state = 0;
		msg_id = PP_CG_MSG_ID(PP_GROUP_SYS,
			       PP_BLOCK_SYS_SDMA,
			       pp_support_state,
			       pp_state);
		amdgpu_dpm_set_clockgating_by_smu(adev, msg_id);
	}

	if (adev->cg_flags & (AMD_CG_SUPPORT_HDP_LS | AMD_CG_SUPPORT_HDP_MGCG)) {
		if (adev->cg_flags & AMD_CG_SUPPORT_HDP_LS) {
			pp_support_state = PP_STATE_SUPPORT_LS;
			pp_state = PP_STATE_LS;
		}
		if (adev->cg_flags & AMD_CG_SUPPORT_HDP_MGCG) {
			pp_support_state |= PP_STATE_SUPPORT_CG;
			pp_state |= PP_STATE_CG;
		}
		if (state == AMD_CG_STATE_UNGATE)
			pp_state = 0;
		msg_id = PP_CG_MSG_ID(PP_GROUP_SYS,
			       PP_BLOCK_SYS_HDP,
			       pp_support_state,
			       pp_state);
		amdgpu_dpm_set_clockgating_by_smu(adev, msg_id);
	}


	if (adev->cg_flags & AMD_CG_SUPPORT_BIF_LS) {
		if (state == AMD_CG_STATE_UNGATE)
			pp_state = 0;
		else
			pp_state = PP_STATE_LS;

		msg_id = PP_CG_MSG_ID(PP_GROUP_SYS,
			       PP_BLOCK_SYS_BIF,
			       PP_STATE_SUPPORT_LS,
			        pp_state);
		amdgpu_dpm_set_clockgating_by_smu(adev, msg_id);
	}
	if (adev->cg_flags & AMD_CG_SUPPORT_BIF_MGCG) {
		if (state == AMD_CG_STATE_UNGATE)
			pp_state = 0;
		else
			pp_state = PP_STATE_CG;

		msg_id = PP_CG_MSG_ID(PP_GROUP_SYS,
			       PP_BLOCK_SYS_BIF,
			       PP_STATE_SUPPORT_CG,
			       pp_state);
		amdgpu_dpm_set_clockgating_by_smu(adev, msg_id);
	}

	if (adev->cg_flags & AMD_CG_SUPPORT_DRM_LS) {

		if (state == AMD_CG_STATE_UNGATE)
			pp_state = 0;
		else
			pp_state = PP_STATE_LS;

		msg_id = PP_CG_MSG_ID(PP_GROUP_SYS,
			       PP_BLOCK_SYS_DRM,
			       PP_STATE_SUPPORT_LS,
			       pp_state);
		amdgpu_dpm_set_clockgating_by_smu(adev, msg_id);
	}

	if (adev->cg_flags & AMD_CG_SUPPORT_ROM_MGCG) {

		if (state == AMD_CG_STATE_UNGATE)
			pp_state = 0;
		else
			pp_state = PP_STATE_CG;

		msg_id = PP_CG_MSG_ID(PP_GROUP_SYS,
			       PP_BLOCK_SYS_ROM,
			       PP_STATE_SUPPORT_CG,
			       pp_state);
		amdgpu_dpm_set_clockgating_by_smu(adev, msg_id);
	}
	return 0;
}

static int vi_common_set_clockgating_state(void *handle,
					   enum amd_clockgating_state state)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (amdgpu_sriov_vf(adev))
		return 0;

	switch (adev->asic_type) {
	case CHIP_FIJI:
		vi_update_bif_medium_grain_light_sleep(adev,
				state == AMD_CG_STATE_GATE);
		vi_update_hdp_medium_grain_clock_gating(adev,
				state == AMD_CG_STATE_GATE);
		vi_update_hdp_light_sleep(adev,
				state == AMD_CG_STATE_GATE);
		vi_update_rom_medium_grain_clock_gating(adev,
				state == AMD_CG_STATE_GATE);
		break;
	case CHIP_CARRIZO:
	case CHIP_STONEY:
		vi_update_bif_medium_grain_light_sleep(adev,
				state == AMD_CG_STATE_GATE);
		vi_update_hdp_medium_grain_clock_gating(adev,
				state == AMD_CG_STATE_GATE);
		vi_update_hdp_light_sleep(adev,
				state == AMD_CG_STATE_GATE);
		vi_update_drm_light_sleep(adev,
				state == AMD_CG_STATE_GATE);
		break;
	case CHIP_TONGA:
	case CHIP_POLARIS10:
	case CHIP_POLARIS11:
	case CHIP_POLARIS12:
	case CHIP_VEGAM:
		vi_common_set_clockgating_state_by_smu(adev, state);
		break;
	default:
		break;
	}
	return 0;
}

static int vi_common_set_powergating_state(void *handle,
					    enum amd_powergating_state state)
{
	return 0;
}

static void vi_common_get_clockgating_state(void *handle, u64 *flags)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int data;

	if (amdgpu_sriov_vf(adev))
		*flags = 0;

	/* AMD_CG_SUPPORT_BIF_LS */
	data = RREG32_PCIE(ixPCIE_CNTL2);
	if (data & PCIE_CNTL2__SLV_MEM_LS_EN_MASK)
		*flags |= AMD_CG_SUPPORT_BIF_LS;

	/* AMD_CG_SUPPORT_HDP_LS */
	data = RREG32(mmHDP_MEM_POWER_LS);
	if (data & HDP_MEM_POWER_LS__LS_ENABLE_MASK)
		*flags |= AMD_CG_SUPPORT_HDP_LS;

	/* AMD_CG_SUPPORT_HDP_MGCG */
	data = RREG32(mmHDP_HOST_PATH_CNTL);
	if (!(data & HDP_HOST_PATH_CNTL__CLOCK_GATING_DIS_MASK))
		*flags |= AMD_CG_SUPPORT_HDP_MGCG;

	/* AMD_CG_SUPPORT_ROM_MGCG */
	data = RREG32_SMC(ixCGTT_ROM_CLK_CTRL0);
	if (!(data & CGTT_ROM_CLK_CTRL0__SOFT_OVERRIDE0_MASK))
		*flags |= AMD_CG_SUPPORT_ROM_MGCG;
}

static const struct amd_ip_funcs vi_common_ip_funcs = {
	.name = "vi_common",
	.early_init = vi_common_early_init,
	.late_init = vi_common_late_init,
	.sw_init = vi_common_sw_init,
	.sw_fini = vi_common_sw_fini,
	.hw_init = vi_common_hw_init,
	.hw_fini = vi_common_hw_fini,
	.suspend = vi_common_suspend,
	.resume = vi_common_resume,
	.is_idle = vi_common_is_idle,
	.wait_for_idle = vi_common_wait_for_idle,
	.soft_reset = vi_common_soft_reset,
	.set_clockgating_state = vi_common_set_clockgating_state,
	.set_powergating_state = vi_common_set_powergating_state,
	.get_clockgating_state = vi_common_get_clockgating_state,
};

static const struct amdgpu_ip_block_version vi_common_ip_block =
{
	.type = AMD_IP_BLOCK_TYPE_COMMON,
	.major = 1,
	.minor = 0,
	.rev = 0,
	.funcs = &vi_common_ip_funcs,
};

void vi_set_virt_ops(struct amdgpu_device *adev)
{
	adev->virt.ops = &xgpu_vi_virt_ops;
}

int vi_set_ip_blocks(struct amdgpu_device *adev)
{
	amdgpu_device_set_sriov_virtual_display(adev);

	switch (adev->asic_type) {
	case CHIP_TOPAZ:
		/* topaz has no DCE, UVD, VCE */
		amdgpu_device_ip_block_add(adev, &vi_common_ip_block);
		amdgpu_device_ip_block_add(adev, &gmc_v7_4_ip_block);
		amdgpu_device_ip_block_add(adev, &iceland_ih_ip_block);
		amdgpu_device_ip_block_add(adev, &gfx_v8_0_ip_block);
		amdgpu_device_ip_block_add(adev, &sdma_v2_4_ip_block);
		amdgpu_device_ip_block_add(adev, &pp_smu_ip_block);
		if (adev->enable_virtual_display)
			amdgpu_device_ip_block_add(adev, &amdgpu_vkms_ip_block);
		break;
	case CHIP_FIJI:
		amdgpu_device_ip_block_add(adev, &vi_common_ip_block);
		amdgpu_device_ip_block_add(adev, &gmc_v8_5_ip_block);
		amdgpu_device_ip_block_add(adev, &tonga_ih_ip_block);
		amdgpu_device_ip_block_add(adev, &gfx_v8_0_ip_block);
		amdgpu_device_ip_block_add(adev, &sdma_v3_0_ip_block);
		amdgpu_device_ip_block_add(adev, &pp_smu_ip_block);
		if (adev->enable_virtual_display)
			amdgpu_device_ip_block_add(adev, &amdgpu_vkms_ip_block);
#if defined(CONFIG_DRM_AMD_DC)
		else if (amdgpu_device_has_dc_support(adev))
			amdgpu_device_ip_block_add(adev, &dm_ip_block);
#endif
		else
			amdgpu_device_ip_block_add(adev, &dce_v10_1_ip_block);
		if (!amdgpu_sriov_vf(adev)) {
			amdgpu_device_ip_block_add(adev, &uvd_v6_0_ip_block);
			amdgpu_device_ip_block_add(adev, &vce_v3_0_ip_block);
		}
		break;
	case CHIP_TONGA:
		amdgpu_device_ip_block_add(adev, &vi_common_ip_block);
		amdgpu_device_ip_block_add(adev, &gmc_v8_0_ip_block);
		amdgpu_device_ip_block_add(adev, &tonga_ih_ip_block);
		amdgpu_device_ip_block_add(adev, &gfx_v8_0_ip_block);
		amdgpu_device_ip_block_add(adev, &sdma_v3_0_ip_block);
		amdgpu_device_ip_block_add(adev, &pp_smu_ip_block);
		if (adev->enable_virtual_display)
			amdgpu_device_ip_block_add(adev, &amdgpu_vkms_ip_block);
#if defined(CONFIG_DRM_AMD_DC)
		else if (amdgpu_device_has_dc_support(adev))
			amdgpu_device_ip_block_add(adev, &dm_ip_block);
#endif
		else
			amdgpu_device_ip_block_add(adev, &dce_v10_0_ip_block);
		if (!amdgpu_sriov_vf(adev)) {
			amdgpu_device_ip_block_add(adev, &uvd_v5_0_ip_block);
			amdgpu_device_ip_block_add(adev, &vce_v3_0_ip_block);
		}
		break;
	case CHIP_POLARIS10:
	case CHIP_POLARIS11:
	case CHIP_POLARIS12:
	case CHIP_VEGAM:
		amdgpu_device_ip_block_add(adev, &vi_common_ip_block);
		amdgpu_device_ip_block_add(adev, &gmc_v8_1_ip_block);
		amdgpu_device_ip_block_add(adev, &tonga_ih_ip_block);
		amdgpu_device_ip_block_add(adev, &gfx_v8_0_ip_block);
		amdgpu_device_ip_block_add(adev, &sdma_v3_1_ip_block);
		amdgpu_device_ip_block_add(adev, &pp_smu_ip_block);
		if (adev->enable_virtual_display)
			amdgpu_device_ip_block_add(adev, &amdgpu_vkms_ip_block);
#if defined(CONFIG_DRM_AMD_DC)
		else if (amdgpu_device_has_dc_support(adev))
			amdgpu_device_ip_block_add(adev, &dm_ip_block);
#endif
		else
			amdgpu_device_ip_block_add(adev, &dce_v11_2_ip_block);
		amdgpu_device_ip_block_add(adev, &uvd_v6_3_ip_block);
		amdgpu_device_ip_block_add(adev, &vce_v3_4_ip_block);
		break;
	case CHIP_CARRIZO:
		amdgpu_device_ip_block_add(adev, &vi_common_ip_block);
		amdgpu_device_ip_block_add(adev, &gmc_v8_0_ip_block);
		amdgpu_device_ip_block_add(adev, &cz_ih_ip_block);
		amdgpu_device_ip_block_add(adev, &gfx_v8_0_ip_block);
		amdgpu_device_ip_block_add(adev, &sdma_v3_0_ip_block);
		amdgpu_device_ip_block_add(adev, &pp_smu_ip_block);
		if (adev->enable_virtual_display)
			amdgpu_device_ip_block_add(adev, &amdgpu_vkms_ip_block);
#if defined(CONFIG_DRM_AMD_DC)
		else if (amdgpu_device_has_dc_support(adev))
			amdgpu_device_ip_block_add(adev, &dm_ip_block);
#endif
		else
			amdgpu_device_ip_block_add(adev, &dce_v11_0_ip_block);
		amdgpu_device_ip_block_add(adev, &uvd_v6_0_ip_block);
		amdgpu_device_ip_block_add(adev, &vce_v3_1_ip_block);
#if defined(CONFIG_DRM_AMD_ACP)
		amdgpu_device_ip_block_add(adev, &acp_ip_block);
#endif
		break;
	case CHIP_STONEY:
		amdgpu_device_ip_block_add(adev, &vi_common_ip_block);
		amdgpu_device_ip_block_add(adev, &gmc_v8_0_ip_block);
		amdgpu_device_ip_block_add(adev, &cz_ih_ip_block);
		amdgpu_device_ip_block_add(adev, &gfx_v8_1_ip_block);
		amdgpu_device_ip_block_add(adev, &sdma_v3_0_ip_block);
		amdgpu_device_ip_block_add(adev, &pp_smu_ip_block);
		if (adev->enable_virtual_display)
			amdgpu_device_ip_block_add(adev, &amdgpu_vkms_ip_block);
#if defined(CONFIG_DRM_AMD_DC)
		else if (amdgpu_device_has_dc_support(adev))
			amdgpu_device_ip_block_add(adev, &dm_ip_block);
#endif
		else
			amdgpu_device_ip_block_add(adev, &dce_v11_0_ip_block);
		amdgpu_device_ip_block_add(adev, &uvd_v6_2_ip_block);
		amdgpu_device_ip_block_add(adev, &vce_v3_4_ip_block);
#if defined(CONFIG_DRM_AMD_ACP)
		amdgpu_device_ip_block_add(adev, &acp_ip_block);
#endif
		break;
	default:
		/* FIXME: not supported yet */
		return -EINVAL;
	}

	return 0;
}

void legacy_doorbell_index_init(struct amdgpu_device *adev)
{
	adev->doorbell_index.kiq = AMDGPU_DOORBELL_KIQ;
	adev->doorbell_index.mec_ring0 = AMDGPU_DOORBELL_MEC_RING0;
	adev->doorbell_index.mec_ring1 = AMDGPU_DOORBELL_MEC_RING1;
	adev->doorbell_index.mec_ring2 = AMDGPU_DOORBELL_MEC_RING2;
	adev->doorbell_index.mec_ring3 = AMDGPU_DOORBELL_MEC_RING3;
	adev->doorbell_index.mec_ring4 = AMDGPU_DOORBELL_MEC_RING4;
	adev->doorbell_index.mec_ring5 = AMDGPU_DOORBELL_MEC_RING5;
	adev->doorbell_index.mec_ring6 = AMDGPU_DOORBELL_MEC_RING6;
	adev->doorbell_index.mec_ring7 = AMDGPU_DOORBELL_MEC_RING7;
	adev->doorbell_index.gfx_ring0 = AMDGPU_DOORBELL_GFX_RING0;
	adev->doorbell_index.sdma_engine[0] = AMDGPU_DOORBELL_sDMA_ENGINE0;
	adev->doorbell_index.sdma_engine[1] = AMDGPU_DOORBELL_sDMA_ENGINE1;
	adev->doorbell_index.ih = AMDGPU_DOORBELL_IH;
	adev->doorbell_index.max_assignment = AMDGPU_DOORBELL_MAX_ASSIGNMENT;
}
