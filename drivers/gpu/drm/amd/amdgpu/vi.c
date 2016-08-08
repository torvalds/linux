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
#include <linux/slab.h>
#include <linux/module.h>
#include "drmP.h"
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
#include "vi_dpm.h"
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
#include "amdgpu_powerplay.h"
#if defined(CONFIG_DRM_AMD_ACP)
#include "amdgpu_acp.h"
#endif

MODULE_FIRMWARE("amdgpu/polaris10_smc.bin");
MODULE_FIRMWARE("amdgpu/polaris10_smc_sk.bin");
MODULE_FIRMWARE("amdgpu/polaris11_smc.bin");
MODULE_FIRMWARE("amdgpu/polaris11_smc_sk.bin");

/*
 * Indirect registers accessor
 */
static u32 vi_pcie_rreg(struct amdgpu_device *adev, u32 reg)
{
	unsigned long flags;
	u32 r;

	spin_lock_irqsave(&adev->pcie_idx_lock, flags);
	WREG32(mmPCIE_INDEX, reg);
	(void)RREG32(mmPCIE_INDEX);
	r = RREG32(mmPCIE_DATA);
	spin_unlock_irqrestore(&adev->pcie_idx_lock, flags);
	return r;
}

static void vi_pcie_wreg(struct amdgpu_device *adev, u32 reg, u32 v)
{
	unsigned long flags;

	spin_lock_irqsave(&adev->pcie_idx_lock, flags);
	WREG32(mmPCIE_INDEX, reg);
	(void)RREG32(mmPCIE_INDEX);
	WREG32(mmPCIE_DATA, v);
	(void)RREG32(mmPCIE_DATA);
	spin_unlock_irqrestore(&adev->pcie_idx_lock, flags);
}

static u32 vi_smc_rreg(struct amdgpu_device *adev, u32 reg)
{
	unsigned long flags;
	u32 r;

	spin_lock_irqsave(&adev->smc_idx_lock, flags);
	WREG32(mmSMC_IND_INDEX_0, (reg));
	r = RREG32(mmSMC_IND_DATA_0);
	spin_unlock_irqrestore(&adev->smc_idx_lock, flags);
	return r;
}

static void vi_smc_wreg(struct amdgpu_device *adev, u32 reg, u32 v)
{
	unsigned long flags;

	spin_lock_irqsave(&adev->smc_idx_lock, flags);
	WREG32(mmSMC_IND_INDEX_0, (reg));
	WREG32(mmSMC_IND_DATA_0, (v));
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

	switch (adev->asic_type) {
	case CHIP_TOPAZ:
		amdgpu_program_register_sequence(adev,
						 iceland_mgcg_cgcg_init,
						 (const u32)ARRAY_SIZE(iceland_mgcg_cgcg_init));
		break;
	case CHIP_FIJI:
		amdgpu_program_register_sequence(adev,
						 fiji_mgcg_cgcg_init,
						 (const u32)ARRAY_SIZE(fiji_mgcg_cgcg_init));
		break;
	case CHIP_TONGA:
		amdgpu_program_register_sequence(adev,
						 tonga_mgcg_cgcg_init,
						 (const u32)ARRAY_SIZE(tonga_mgcg_cgcg_init));
		break;
	case CHIP_CARRIZO:
		amdgpu_program_register_sequence(adev,
						 cz_mgcg_cgcg_init,
						 (const u32)ARRAY_SIZE(cz_mgcg_cgcg_init));
		break;
	case CHIP_STONEY:
		amdgpu_program_register_sequence(adev,
						 stoney_mgcg_cgcg_init,
						 (const u32)ARRAY_SIZE(stoney_mgcg_cgcg_init));
		break;
	case CHIP_POLARIS11:
	case CHIP_POLARIS10:
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
	WREG32(mmSMC_IND_INDEX_0, ixROM_INDEX);
	WREG32(mmSMC_IND_DATA_0, 0);
	/* set index to data for continous read */
	WREG32(mmSMC_IND_INDEX_0, ixROM_DATA);
	for (i = 0; i < length_dw; i++)
		dw_ptr[i] = RREG32(mmSMC_IND_DATA_0);
	spin_unlock_irqrestore(&adev->smc_idx_lock, flags);

	return true;
}

static u32 vi_get_virtual_caps(struct amdgpu_device *adev)
{
	u32 caps = 0;
	u32 reg = RREG32(mmBIF_IOV_FUNC_IDENTIFIER);

	if (REG_GET_FIELD(reg, BIF_IOV_FUNC_IDENTIFIER, IOV_ENABLE))
		caps |= AMDGPU_VIRT_CAPS_SRIOV_EN;

	if (REG_GET_FIELD(reg, BIF_IOV_FUNC_IDENTIFIER, FUNC_IDENTIFIER))
		caps |= AMDGPU_VIRT_CAPS_IS_VF;

	return caps;
}

static const struct amdgpu_allowed_register_entry tonga_allowed_read_registers[] = {
	{mmGB_MACROTILE_MODE7, true},
};

static const struct amdgpu_allowed_register_entry cz_allowed_read_registers[] = {
	{mmGB_TILE_MODE7, true},
	{mmGB_TILE_MODE12, true},
	{mmGB_TILE_MODE17, true},
	{mmGB_TILE_MODE23, true},
	{mmGB_MACROTILE_MODE7, true},
};

static const struct amdgpu_allowed_register_entry vi_allowed_read_registers[] = {
	{mmGRBM_STATUS, false},
	{mmGRBM_STATUS2, false},
	{mmGRBM_STATUS_SE0, false},
	{mmGRBM_STATUS_SE1, false},
	{mmGRBM_STATUS_SE2, false},
	{mmGRBM_STATUS_SE3, false},
	{mmSRBM_STATUS, false},
	{mmSRBM_STATUS2, false},
	{mmSRBM_STATUS3, false},
	{mmSDMA0_STATUS_REG + SDMA0_REGISTER_OFFSET, false},
	{mmSDMA0_STATUS_REG + SDMA1_REGISTER_OFFSET, false},
	{mmCP_STAT, false},
	{mmCP_STALLED_STAT1, false},
	{mmCP_STALLED_STAT2, false},
	{mmCP_STALLED_STAT3, false},
	{mmCP_CPF_BUSY_STAT, false},
	{mmCP_CPF_STALLED_STAT1, false},
	{mmCP_CPF_STATUS, false},
	{mmCP_CPC_BUSY_STAT, false},
	{mmCP_CPC_STALLED_STAT1, false},
	{mmCP_CPC_STATUS, false},
	{mmGB_ADDR_CONFIG, false},
	{mmMC_ARB_RAMCFG, false},
	{mmGB_TILE_MODE0, false},
	{mmGB_TILE_MODE1, false},
	{mmGB_TILE_MODE2, false},
	{mmGB_TILE_MODE3, false},
	{mmGB_TILE_MODE4, false},
	{mmGB_TILE_MODE5, false},
	{mmGB_TILE_MODE6, false},
	{mmGB_TILE_MODE7, false},
	{mmGB_TILE_MODE8, false},
	{mmGB_TILE_MODE9, false},
	{mmGB_TILE_MODE10, false},
	{mmGB_TILE_MODE11, false},
	{mmGB_TILE_MODE12, false},
	{mmGB_TILE_MODE13, false},
	{mmGB_TILE_MODE14, false},
	{mmGB_TILE_MODE15, false},
	{mmGB_TILE_MODE16, false},
	{mmGB_TILE_MODE17, false},
	{mmGB_TILE_MODE18, false},
	{mmGB_TILE_MODE19, false},
	{mmGB_TILE_MODE20, false},
	{mmGB_TILE_MODE21, false},
	{mmGB_TILE_MODE22, false},
	{mmGB_TILE_MODE23, false},
	{mmGB_TILE_MODE24, false},
	{mmGB_TILE_MODE25, false},
	{mmGB_TILE_MODE26, false},
	{mmGB_TILE_MODE27, false},
	{mmGB_TILE_MODE28, false},
	{mmGB_TILE_MODE29, false},
	{mmGB_TILE_MODE30, false},
	{mmGB_TILE_MODE31, false},
	{mmGB_MACROTILE_MODE0, false},
	{mmGB_MACROTILE_MODE1, false},
	{mmGB_MACROTILE_MODE2, false},
	{mmGB_MACROTILE_MODE3, false},
	{mmGB_MACROTILE_MODE4, false},
	{mmGB_MACROTILE_MODE5, false},
	{mmGB_MACROTILE_MODE6, false},
	{mmGB_MACROTILE_MODE7, false},
	{mmGB_MACROTILE_MODE8, false},
	{mmGB_MACROTILE_MODE9, false},
	{mmGB_MACROTILE_MODE10, false},
	{mmGB_MACROTILE_MODE11, false},
	{mmGB_MACROTILE_MODE12, false},
	{mmGB_MACROTILE_MODE13, false},
	{mmGB_MACROTILE_MODE14, false},
	{mmGB_MACROTILE_MODE15, false},
	{mmCC_RB_BACKEND_DISABLE, false, true},
	{mmGC_USER_RB_BACKEND_DISABLE, false, true},
	{mmGB_BACKEND_MAP, false, false},
	{mmPA_SC_RASTER_CONFIG, false, true},
	{mmPA_SC_RASTER_CONFIG_1, false, true},
};

static uint32_t vi_read_indexed_register(struct amdgpu_device *adev, u32 se_num,
					 u32 sh_num, u32 reg_offset)
{
	uint32_t val;

	mutex_lock(&adev->grbm_idx_mutex);
	if (se_num != 0xffffffff || sh_num != 0xffffffff)
		amdgpu_gfx_select_se_sh(adev, se_num, sh_num, 0xffffffff);

	val = RREG32(reg_offset);

	if (se_num != 0xffffffff || sh_num != 0xffffffff)
		amdgpu_gfx_select_se_sh(adev, 0xffffffff, 0xffffffff, 0xffffffff);
	mutex_unlock(&adev->grbm_idx_mutex);
	return val;
}

static int vi_read_register(struct amdgpu_device *adev, u32 se_num,
			    u32 sh_num, u32 reg_offset, u32 *value)
{
	const struct amdgpu_allowed_register_entry *asic_register_table = NULL;
	const struct amdgpu_allowed_register_entry *asic_register_entry;
	uint32_t size, i;

	*value = 0;
	switch (adev->asic_type) {
	case CHIP_TOPAZ:
		asic_register_table = tonga_allowed_read_registers;
		size = ARRAY_SIZE(tonga_allowed_read_registers);
		break;
	case CHIP_FIJI:
	case CHIP_TONGA:
	case CHIP_POLARIS11:
	case CHIP_POLARIS10:
	case CHIP_CARRIZO:
	case CHIP_STONEY:
		asic_register_table = cz_allowed_read_registers;
		size = ARRAY_SIZE(cz_allowed_read_registers);
		break;
	default:
		return -EINVAL;
	}

	if (asic_register_table) {
		for (i = 0; i < size; i++) {
			asic_register_entry = asic_register_table + i;
			if (reg_offset != asic_register_entry->reg_offset)
				continue;
			if (!asic_register_entry->untouched)
				*value = asic_register_entry->grbm_indexed ?
					vi_read_indexed_register(adev, se_num,
								 sh_num, reg_offset) :
					RREG32(reg_offset);
			return 0;
		}
	}

	for (i = 0; i < ARRAY_SIZE(vi_allowed_read_registers); i++) {
		if (reg_offset != vi_allowed_read_registers[i].reg_offset)
			continue;

		if (!vi_allowed_read_registers[i].untouched)
			*value = vi_allowed_read_registers[i].grbm_indexed ?
				vi_read_indexed_register(adev, se_num,
							 sh_num, reg_offset) :
				RREG32(reg_offset);
		return 0;
	}
	return -EINVAL;
}

static int vi_gpu_pci_config_reset(struct amdgpu_device *adev)
{
	u32 i;

	dev_info(adev->dev, "GPU pci config reset\n");

	/* disable BM */
	pci_clear_master(adev->pdev);
	/* reset */
	amdgpu_pci_config_reset(adev);

	udelay(100);

	/* wait for asic to come out of reset */
	for (i = 0; i < adev->usec_timeout; i++) {
		if (RREG32(mmCONFIG_MEMSIZE) != 0xffffffff) {
			/* enable BM */
			pci_set_master(adev->pdev);
			return 0;
		}
		udelay(1);
	}
	return -EINVAL;
}

static void vi_set_bios_scratch_engine_hung(struct amdgpu_device *adev, bool hung)
{
	u32 tmp = RREG32(mmBIOS_SCRATCH_3);

	if (hung)
		tmp |= ATOM_S3_ASIC_GUI_ENGINE_HUNG;
	else
		tmp &= ~ATOM_S3_ASIC_GUI_ENGINE_HUNG;

	WREG32(mmBIOS_SCRATCH_3, tmp);
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

	vi_set_bios_scratch_engine_hung(adev, true);

	r = vi_gpu_pci_config_reset(adev);

	vi_set_bios_scratch_engine_hung(adev, false);

	return r;
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
	tmp &= ~(CG_DCLK_CNTL__DCLK_DIR_CNTL_EN_MASK |
		CG_DCLK_CNTL__DCLK_DIVIDER_MASK);
	tmp |= dividers.post_divider;
	WREG32_SMC(cntl_reg, tmp);

	for (i = 0; i < 100; i++) {
		if (RREG32_SMC(status_reg) & CG_DCLK_STATUS__DCLK_STATUS_MASK)
			break;
		mdelay(10);
	}
	if (i == 100)
		return -ETIMEDOUT;

	return 0;
}

static int vi_set_uvd_clocks(struct amdgpu_device *adev, u32 vclk, u32 dclk)
{
	int r;

	r = vi_set_uvd_clock(adev, vclk, ixCG_VCLK_CNTL, ixCG_VCLK_STATUS);
	if (r)
		return r;

	r = vi_set_uvd_clock(adev, dclk, ixCG_DCLK_CNTL, ixCG_DCLK_STATUS);

	return 0;
}

static int vi_set_vce_clocks(struct amdgpu_device *adev, u32 evclk, u32 ecclk)
{
	/* todo */

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

static void vi_program_aspm(struct amdgpu_device *adev)
{

	if (amdgpu_aspm == 0)
		return;

	/* todo */
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

/* topaz has no DCE, UVD, VCE */
static const struct amdgpu_ip_block_version topaz_ip_blocks[] =
{
	/* ORDER MATTERS! */
	{
		.type = AMD_IP_BLOCK_TYPE_COMMON,
		.major = 2,
		.minor = 0,
		.rev = 0,
		.funcs = &vi_common_ip_funcs,
	},
	{
		.type = AMD_IP_BLOCK_TYPE_GMC,
		.major = 7,
		.minor = 4,
		.rev = 0,
		.funcs = &gmc_v7_0_ip_funcs,
	},
	{
		.type = AMD_IP_BLOCK_TYPE_IH,
		.major = 2,
		.minor = 4,
		.rev = 0,
		.funcs = &iceland_ih_ip_funcs,
	},
	{
		.type = AMD_IP_BLOCK_TYPE_SMC,
		.major = 7,
		.minor = 1,
		.rev = 0,
		.funcs = &amdgpu_pp_ip_funcs,
	},
	{
		.type = AMD_IP_BLOCK_TYPE_GFX,
		.major = 8,
		.minor = 0,
		.rev = 0,
		.funcs = &gfx_v8_0_ip_funcs,
	},
	{
		.type = AMD_IP_BLOCK_TYPE_SDMA,
		.major = 2,
		.minor = 4,
		.rev = 0,
		.funcs = &sdma_v2_4_ip_funcs,
	},
};

static const struct amdgpu_ip_block_version tonga_ip_blocks[] =
{
	/* ORDER MATTERS! */
	{
		.type = AMD_IP_BLOCK_TYPE_COMMON,
		.major = 2,
		.minor = 0,
		.rev = 0,
		.funcs = &vi_common_ip_funcs,
	},
	{
		.type = AMD_IP_BLOCK_TYPE_GMC,
		.major = 8,
		.minor = 0,
		.rev = 0,
		.funcs = &gmc_v8_0_ip_funcs,
	},
	{
		.type = AMD_IP_BLOCK_TYPE_IH,
		.major = 3,
		.minor = 0,
		.rev = 0,
		.funcs = &tonga_ih_ip_funcs,
	},
	{
		.type = AMD_IP_BLOCK_TYPE_SMC,
		.major = 7,
		.minor = 1,
		.rev = 0,
		.funcs = &amdgpu_pp_ip_funcs,
	},
	{
		.type = AMD_IP_BLOCK_TYPE_DCE,
		.major = 10,
		.minor = 0,
		.rev = 0,
		.funcs = &dce_v10_0_ip_funcs,
	},
	{
		.type = AMD_IP_BLOCK_TYPE_GFX,
		.major = 8,
		.minor = 0,
		.rev = 0,
		.funcs = &gfx_v8_0_ip_funcs,
	},
	{
		.type = AMD_IP_BLOCK_TYPE_SDMA,
		.major = 3,
		.minor = 0,
		.rev = 0,
		.funcs = &sdma_v3_0_ip_funcs,
	},
	{
		.type = AMD_IP_BLOCK_TYPE_UVD,
		.major = 5,
		.minor = 0,
		.rev = 0,
		.funcs = &uvd_v5_0_ip_funcs,
	},
	{
		.type = AMD_IP_BLOCK_TYPE_VCE,
		.major = 3,
		.minor = 0,
		.rev = 0,
		.funcs = &vce_v3_0_ip_funcs,
	},
};

static const struct amdgpu_ip_block_version fiji_ip_blocks[] =
{
	/* ORDER MATTERS! */
	{
		.type = AMD_IP_BLOCK_TYPE_COMMON,
		.major = 2,
		.minor = 0,
		.rev = 0,
		.funcs = &vi_common_ip_funcs,
	},
	{
		.type = AMD_IP_BLOCK_TYPE_GMC,
		.major = 8,
		.minor = 5,
		.rev = 0,
		.funcs = &gmc_v8_0_ip_funcs,
	},
	{
		.type = AMD_IP_BLOCK_TYPE_IH,
		.major = 3,
		.minor = 0,
		.rev = 0,
		.funcs = &tonga_ih_ip_funcs,
	},
	{
		.type = AMD_IP_BLOCK_TYPE_SMC,
		.major = 7,
		.minor = 1,
		.rev = 0,
		.funcs = &amdgpu_pp_ip_funcs,
	},
	{
		.type = AMD_IP_BLOCK_TYPE_DCE,
		.major = 10,
		.minor = 1,
		.rev = 0,
		.funcs = &dce_v10_0_ip_funcs,
	},
	{
		.type = AMD_IP_BLOCK_TYPE_GFX,
		.major = 8,
		.minor = 0,
		.rev = 0,
		.funcs = &gfx_v8_0_ip_funcs,
	},
	{
		.type = AMD_IP_BLOCK_TYPE_SDMA,
		.major = 3,
		.minor = 0,
		.rev = 0,
		.funcs = &sdma_v3_0_ip_funcs,
	},
	{
		.type = AMD_IP_BLOCK_TYPE_UVD,
		.major = 6,
		.minor = 0,
		.rev = 0,
		.funcs = &uvd_v6_0_ip_funcs,
	},
	{
		.type = AMD_IP_BLOCK_TYPE_VCE,
		.major = 3,
		.minor = 0,
		.rev = 0,
		.funcs = &vce_v3_0_ip_funcs,
	},
};

static const struct amdgpu_ip_block_version polaris11_ip_blocks[] =
{
	/* ORDER MATTERS! */
	{
		.type = AMD_IP_BLOCK_TYPE_COMMON,
		.major = 2,
		.minor = 0,
		.rev = 0,
		.funcs = &vi_common_ip_funcs,
	},
	{
		.type = AMD_IP_BLOCK_TYPE_GMC,
		.major = 8,
		.minor = 1,
		.rev = 0,
		.funcs = &gmc_v8_0_ip_funcs,
	},
	{
		.type = AMD_IP_BLOCK_TYPE_IH,
		.major = 3,
		.minor = 1,
		.rev = 0,
		.funcs = &tonga_ih_ip_funcs,
	},
	{
		.type = AMD_IP_BLOCK_TYPE_SMC,
		.major = 7,
		.minor = 2,
		.rev = 0,
		.funcs = &amdgpu_pp_ip_funcs,
	},
	{
		.type = AMD_IP_BLOCK_TYPE_DCE,
		.major = 11,
		.minor = 2,
		.rev = 0,
		.funcs = &dce_v11_0_ip_funcs,
	},
	{
		.type = AMD_IP_BLOCK_TYPE_GFX,
		.major = 8,
		.minor = 0,
		.rev = 0,
		.funcs = &gfx_v8_0_ip_funcs,
	},
	{
		.type = AMD_IP_BLOCK_TYPE_SDMA,
		.major = 3,
		.minor = 1,
		.rev = 0,
		.funcs = &sdma_v3_0_ip_funcs,
	},
	{
		.type = AMD_IP_BLOCK_TYPE_UVD,
		.major = 6,
		.minor = 3,
		.rev = 0,
		.funcs = &uvd_v6_0_ip_funcs,
	},
	{
		.type = AMD_IP_BLOCK_TYPE_VCE,
		.major = 3,
		.minor = 4,
		.rev = 0,
		.funcs = &vce_v3_0_ip_funcs,
	},
};

static const struct amdgpu_ip_block_version cz_ip_blocks[] =
{
	/* ORDER MATTERS! */
	{
		.type = AMD_IP_BLOCK_TYPE_COMMON,
		.major = 2,
		.minor = 0,
		.rev = 0,
		.funcs = &vi_common_ip_funcs,
	},
	{
		.type = AMD_IP_BLOCK_TYPE_GMC,
		.major = 8,
		.minor = 0,
		.rev = 0,
		.funcs = &gmc_v8_0_ip_funcs,
	},
	{
		.type = AMD_IP_BLOCK_TYPE_IH,
		.major = 3,
		.minor = 0,
		.rev = 0,
		.funcs = &cz_ih_ip_funcs,
	},
	{
		.type = AMD_IP_BLOCK_TYPE_SMC,
		.major = 8,
		.minor = 0,
		.rev = 0,
		.funcs = &amdgpu_pp_ip_funcs
	},
	{
		.type = AMD_IP_BLOCK_TYPE_DCE,
		.major = 11,
		.minor = 0,
		.rev = 0,
		.funcs = &dce_v11_0_ip_funcs,
	},
	{
		.type = AMD_IP_BLOCK_TYPE_GFX,
		.major = 8,
		.minor = 0,
		.rev = 0,
		.funcs = &gfx_v8_0_ip_funcs,
	},
	{
		.type = AMD_IP_BLOCK_TYPE_SDMA,
		.major = 3,
		.minor = 0,
		.rev = 0,
		.funcs = &sdma_v3_0_ip_funcs,
	},
	{
		.type = AMD_IP_BLOCK_TYPE_UVD,
		.major = 6,
		.minor = 0,
		.rev = 0,
		.funcs = &uvd_v6_0_ip_funcs,
	},
	{
		.type = AMD_IP_BLOCK_TYPE_VCE,
		.major = 3,
		.minor = 0,
		.rev = 0,
		.funcs = &vce_v3_0_ip_funcs,
	},
#if defined(CONFIG_DRM_AMD_ACP)
	{
		.type = AMD_IP_BLOCK_TYPE_ACP,
		.major = 2,
		.minor = 2,
		.rev = 0,
		.funcs = &acp_ip_funcs,
	},
#endif
};

int vi_set_ip_blocks(struct amdgpu_device *adev)
{
	switch (adev->asic_type) {
	case CHIP_TOPAZ:
		adev->ip_blocks = topaz_ip_blocks;
		adev->num_ip_blocks = ARRAY_SIZE(topaz_ip_blocks);
		break;
	case CHIP_FIJI:
		adev->ip_blocks = fiji_ip_blocks;
		adev->num_ip_blocks = ARRAY_SIZE(fiji_ip_blocks);
		break;
	case CHIP_TONGA:
		adev->ip_blocks = tonga_ip_blocks;
		adev->num_ip_blocks = ARRAY_SIZE(tonga_ip_blocks);
		break;
	case CHIP_POLARIS11:
	case CHIP_POLARIS10:
		adev->ip_blocks = polaris11_ip_blocks;
		adev->num_ip_blocks = ARRAY_SIZE(polaris11_ip_blocks);
		break;
	case CHIP_CARRIZO:
	case CHIP_STONEY:
		adev->ip_blocks = cz_ip_blocks;
		adev->num_ip_blocks = ARRAY_SIZE(cz_ip_blocks);
		break;
	default:
		/* FIXME: not supported yet */
		return -EINVAL;
	}

	return 0;
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

static const struct amdgpu_asic_funcs vi_asic_funcs =
{
	.read_disabled_bios = &vi_read_disabled_bios,
	.read_bios_from_rom = &vi_read_bios_from_rom,
	.read_register = &vi_read_register,
	.reset = &vi_asic_reset,
	.set_vga_state = &vi_vga_set_state,
	.get_xclk = &vi_get_xclk,
	.set_uvd_clocks = &vi_set_uvd_clocks,
	.set_vce_clocks = &vi_set_vce_clocks,
	.get_virtual_caps = &vi_get_virtual_caps,
};

static int vi_common_early_init(void *handle)
{
	bool smc_enabled = false;
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

	if (amdgpu_get_ip_block(adev, AMD_IP_BLOCK_TYPE_SMC) &&
		(amdgpu_ip_block_mask & (1 << AMD_IP_BLOCK_TYPE_SMC)))
		smc_enabled = true;

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
			AMD_CG_SUPPORT_MC_LS;
		adev->pg_flags = 0;
		adev->external_rev_id = adev->rev_id + 0x3c;
		break;
	case CHIP_TONGA:
		adev->cg_flags = AMD_CG_SUPPORT_UVD_MGCG;
		adev->pg_flags = 0;
		adev->external_rev_id = adev->rev_id + 0x14;
		break;
	case CHIP_POLARIS11:
		adev->cg_flags = 0;
		adev->pg_flags = 0;
		adev->external_rev_id = adev->rev_id + 0x5A;
		break;
	case CHIP_POLARIS10:
		adev->cg_flags = 0;
		adev->pg_flags = 0;
		adev->external_rev_id = adev->rev_id + 0x50;
		break;
	case CHIP_CARRIZO:
		adev->cg_flags = AMD_CG_SUPPORT_UVD_MGCG |
			AMD_CG_SUPPORT_GFX_MGCG |
			AMD_CG_SUPPORT_GFX_MGLS |
			AMD_CG_SUPPORT_GFX_RLC_LS |
			AMD_CG_SUPPORT_GFX_CP_LS |
			AMD_CG_SUPPORT_GFX_CGTS |
			AMD_CG_SUPPORT_GFX_MGLS |
			AMD_CG_SUPPORT_GFX_CGTS_LS |
			AMD_CG_SUPPORT_GFX_CGCG |
			AMD_CG_SUPPORT_GFX_CGLS |
			AMD_CG_SUPPORT_BIF_LS |
			AMD_CG_SUPPORT_HDP_MGCG |
			AMD_CG_SUPPORT_HDP_LS |
			AMD_CG_SUPPORT_SDMA_MGCG |
			AMD_CG_SUPPORT_SDMA_LS;
		adev->pg_flags = 0;
		adev->external_rev_id = adev->rev_id + 0x1;
		break;
	case CHIP_STONEY:
		adev->cg_flags = AMD_CG_SUPPORT_UVD_MGCG |
			AMD_CG_SUPPORT_GFX_MGCG |
			AMD_CG_SUPPORT_GFX_MGLS |
			AMD_CG_SUPPORT_GFX_RLC_LS |
			AMD_CG_SUPPORT_GFX_CP_LS |
			AMD_CG_SUPPORT_GFX_CGTS |
			AMD_CG_SUPPORT_GFX_MGLS |
			AMD_CG_SUPPORT_GFX_CGTS_LS |
			AMD_CG_SUPPORT_GFX_CGCG |
			AMD_CG_SUPPORT_GFX_CGLS |
			AMD_CG_SUPPORT_BIF_LS |
			AMD_CG_SUPPORT_HDP_MGCG |
			AMD_CG_SUPPORT_HDP_LS |
			AMD_CG_SUPPORT_SDMA_MGCG |
			AMD_CG_SUPPORT_SDMA_LS;
		adev->external_rev_id = adev->rev_id + 0x1;
		break;
	default:
		/* FIXME: not supported yet */
		return -EINVAL;
	}

	if (amdgpu_smc_load_fw && smc_enabled)
		adev->firmware.smu_load = true;

	amdgpu_get_pcie_info(adev);

	return 0;
}

static int vi_common_sw_init(void *handle)
{
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

static int vi_common_set_clockgating_state(void *handle,
					   enum amd_clockgating_state state)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	switch (adev->asic_type) {
	case CHIP_FIJI:
		vi_update_bif_medium_grain_light_sleep(adev,
				state == AMD_CG_STATE_GATE ? true : false);
		vi_update_hdp_medium_grain_clock_gating(adev,
				state == AMD_CG_STATE_GATE ? true : false);
		vi_update_hdp_light_sleep(adev,
				state == AMD_CG_STATE_GATE ? true : false);
		vi_update_rom_medium_grain_clock_gating(adev,
				state == AMD_CG_STATE_GATE ? true : false);
		break;
	case CHIP_CARRIZO:
	case CHIP_STONEY:
		vi_update_bif_medium_grain_light_sleep(adev,
				state == AMD_CG_STATE_GATE ? true : false);
		vi_update_hdp_medium_grain_clock_gating(adev,
				state == AMD_CG_STATE_GATE ? true : false);
		vi_update_hdp_light_sleep(adev,
				state == AMD_CG_STATE_GATE ? true : false);
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

const struct amd_ip_funcs vi_common_ip_funcs = {
	.name = "vi_common",
	.early_init = vi_common_early_init,
	.late_init = NULL,
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
};

