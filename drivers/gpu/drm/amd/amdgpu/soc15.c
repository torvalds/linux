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
#include <linux/firmware.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <drm/drmP.h>
#include "amdgpu.h"
#include "amdgpu_atombios.h"
#include "amdgpu_ih.h"
#include "amdgpu_uvd.h"
#include "amdgpu_vce.h"
#include "amdgpu_ucode.h"
#include "amdgpu_psp.h"
#include "atom.h"
#include "amd_pcie.h"

#include "vega10/soc15ip.h"
#include "vega10/UVD/uvd_7_0_offset.h"
#include "vega10/GC/gc_9_0_offset.h"
#include "vega10/GC/gc_9_0_sh_mask.h"
#include "vega10/SDMA0/sdma0_4_0_offset.h"
#include "vega10/SDMA1/sdma1_4_0_offset.h"
#include "vega10/HDP/hdp_4_0_offset.h"
#include "vega10/HDP/hdp_4_0_sh_mask.h"
#include "vega10/MP/mp_9_0_offset.h"
#include "vega10/MP/mp_9_0_sh_mask.h"
#include "vega10/SMUIO/smuio_9_0_offset.h"
#include "vega10/SMUIO/smuio_9_0_sh_mask.h"

#include "soc15.h"
#include "soc15_common.h"
#include "gfx_v9_0.h"
#include "gmc_v9_0.h"
#include "gfxhub_v1_0.h"
#include "mmhub_v1_0.h"
#include "vega10_ih.h"
#include "sdma_v4_0.h"
#include "uvd_v7_0.h"
#include "vce_v4_0.h"
#include "vcn_v1_0.h"
#include "amdgpu_powerplay.h"
#include "dce_virtual.h"
#include "mxgpu_ai.h"

#define mmFabricConfigAccessControl                                                                    0x0410
#define mmFabricConfigAccessControl_BASE_IDX                                                           0
#define mmFabricConfigAccessControl_DEFAULT                                      0x00000000
//FabricConfigAccessControl
#define FabricConfigAccessControl__CfgRegInstAccEn__SHIFT                                                     0x0
#define FabricConfigAccessControl__CfgRegInstAccRegLock__SHIFT                                                0x1
#define FabricConfigAccessControl__CfgRegInstID__SHIFT                                                        0x10
#define FabricConfigAccessControl__CfgRegInstAccEn_MASK                                                       0x00000001L
#define FabricConfigAccessControl__CfgRegInstAccRegLock_MASK                                                  0x00000002L
#define FabricConfigAccessControl__CfgRegInstID_MASK                                                          0x00FF0000L


#define mmDF_PIE_AON0_DfGlobalClkGater                                                                 0x00fc
#define mmDF_PIE_AON0_DfGlobalClkGater_BASE_IDX                                                        0
//DF_PIE_AON0_DfGlobalClkGater
#define DF_PIE_AON0_DfGlobalClkGater__MGCGMode__SHIFT                                                         0x0
#define DF_PIE_AON0_DfGlobalClkGater__MGCGMode_MASK                                                           0x0000000FL

enum {
	DF_MGCG_DISABLE = 0,
	DF_MGCG_ENABLE_00_CYCLE_DELAY =1,
	DF_MGCG_ENABLE_01_CYCLE_DELAY =2,
	DF_MGCG_ENABLE_15_CYCLE_DELAY =13,
	DF_MGCG_ENABLE_31_CYCLE_DELAY =14,
	DF_MGCG_ENABLE_63_CYCLE_DELAY =15
};

#define mmMP0_MISC_CGTT_CTRL0                                                                   0x01b9
#define mmMP0_MISC_CGTT_CTRL0_BASE_IDX                                                          0
#define mmMP0_MISC_LIGHT_SLEEP_CTRL                                                             0x01ba
#define mmMP0_MISC_LIGHT_SLEEP_CTRL_BASE_IDX                                                    0

/*
 * Indirect registers accessor
 */
static u32 soc15_pcie_rreg(struct amdgpu_device *adev, u32 reg)
{
	unsigned long flags, address, data;
	u32 r;
	const struct nbio_pcie_index_data *nbio_pcie_id;

	if (adev->flags & AMD_IS_APU)
		nbio_pcie_id = &nbio_v7_0_pcie_index_data;
	else
		nbio_pcie_id = &nbio_v6_1_pcie_index_data;

	address = nbio_pcie_id->index_offset;
	data = nbio_pcie_id->data_offset;

	spin_lock_irqsave(&adev->pcie_idx_lock, flags);
	WREG32(address, reg);
	(void)RREG32(address);
	r = RREG32(data);
	spin_unlock_irqrestore(&adev->pcie_idx_lock, flags);
	return r;
}

static void soc15_pcie_wreg(struct amdgpu_device *adev, u32 reg, u32 v)
{
	unsigned long flags, address, data;
	const struct nbio_pcie_index_data *nbio_pcie_id;

	if (adev->flags & AMD_IS_APU)
		nbio_pcie_id = &nbio_v7_0_pcie_index_data;
	else
		nbio_pcie_id = &nbio_v6_1_pcie_index_data;

	address = nbio_pcie_id->index_offset;
	data = nbio_pcie_id->data_offset;

	spin_lock_irqsave(&adev->pcie_idx_lock, flags);
	WREG32(address, reg);
	(void)RREG32(address);
	WREG32(data, v);
	(void)RREG32(data);
	spin_unlock_irqrestore(&adev->pcie_idx_lock, flags);
}

static u32 soc15_uvd_ctx_rreg(struct amdgpu_device *adev, u32 reg)
{
	unsigned long flags, address, data;
	u32 r;

	address = SOC15_REG_OFFSET(UVD, 0, mmUVD_CTX_INDEX);
	data = SOC15_REG_OFFSET(UVD, 0, mmUVD_CTX_DATA);

	spin_lock_irqsave(&adev->uvd_ctx_idx_lock, flags);
	WREG32(address, ((reg) & 0x1ff));
	r = RREG32(data);
	spin_unlock_irqrestore(&adev->uvd_ctx_idx_lock, flags);
	return r;
}

static void soc15_uvd_ctx_wreg(struct amdgpu_device *adev, u32 reg, u32 v)
{
	unsigned long flags, address, data;

	address = SOC15_REG_OFFSET(UVD, 0, mmUVD_CTX_INDEX);
	data = SOC15_REG_OFFSET(UVD, 0, mmUVD_CTX_DATA);

	spin_lock_irqsave(&adev->uvd_ctx_idx_lock, flags);
	WREG32(address, ((reg) & 0x1ff));
	WREG32(data, (v));
	spin_unlock_irqrestore(&adev->uvd_ctx_idx_lock, flags);
}

static u32 soc15_didt_rreg(struct amdgpu_device *adev, u32 reg)
{
	unsigned long flags, address, data;
	u32 r;

	address = SOC15_REG_OFFSET(GC, 0, mmDIDT_IND_INDEX);
	data = SOC15_REG_OFFSET(GC, 0, mmDIDT_IND_DATA);

	spin_lock_irqsave(&adev->didt_idx_lock, flags);
	WREG32(address, (reg));
	r = RREG32(data);
	spin_unlock_irqrestore(&adev->didt_idx_lock, flags);
	return r;
}

static void soc15_didt_wreg(struct amdgpu_device *adev, u32 reg, u32 v)
{
	unsigned long flags, address, data;

	address = SOC15_REG_OFFSET(GC, 0, mmDIDT_IND_INDEX);
	data = SOC15_REG_OFFSET(GC, 0, mmDIDT_IND_DATA);

	spin_lock_irqsave(&adev->didt_idx_lock, flags);
	WREG32(address, (reg));
	WREG32(data, (v));
	spin_unlock_irqrestore(&adev->didt_idx_lock, flags);
}

static u32 soc15_gc_cac_rreg(struct amdgpu_device *adev, u32 reg)
{
	unsigned long flags;
	u32 r;

	spin_lock_irqsave(&adev->gc_cac_idx_lock, flags);
	WREG32_SOC15(GC, 0, mmGC_CAC_IND_INDEX, (reg));
	r = RREG32_SOC15(GC, 0, mmGC_CAC_IND_DATA);
	spin_unlock_irqrestore(&adev->gc_cac_idx_lock, flags);
	return r;
}

static void soc15_gc_cac_wreg(struct amdgpu_device *adev, u32 reg, u32 v)
{
	unsigned long flags;

	spin_lock_irqsave(&adev->gc_cac_idx_lock, flags);
	WREG32_SOC15(GC, 0, mmGC_CAC_IND_INDEX, (reg));
	WREG32_SOC15(GC, 0, mmGC_CAC_IND_DATA, (v));
	spin_unlock_irqrestore(&adev->gc_cac_idx_lock, flags);
}

static u32 soc15_se_cac_rreg(struct amdgpu_device *adev, u32 reg)
{
	unsigned long flags;
	u32 r;

	spin_lock_irqsave(&adev->se_cac_idx_lock, flags);
	WREG32_SOC15(GC, 0, mmSE_CAC_IND_INDEX, (reg));
	r = RREG32_SOC15(GC, 0, mmSE_CAC_IND_DATA);
	spin_unlock_irqrestore(&adev->se_cac_idx_lock, flags);
	return r;
}

static void soc15_se_cac_wreg(struct amdgpu_device *adev, u32 reg, u32 v)
{
	unsigned long flags;

	spin_lock_irqsave(&adev->se_cac_idx_lock, flags);
	WREG32_SOC15(GC, 0, mmSE_CAC_IND_INDEX, (reg));
	WREG32_SOC15(GC, 0, mmSE_CAC_IND_DATA, (v));
	spin_unlock_irqrestore(&adev->se_cac_idx_lock, flags);
}

static u32 soc15_get_config_memsize(struct amdgpu_device *adev)
{
	if (adev->flags & AMD_IS_APU)
		return nbio_v7_0_get_memsize(adev);
	else
		return nbio_v6_1_get_memsize(adev);
}

static const u32 vega10_golden_init[] =
{
};

static const u32 raven_golden_init[] =
{
};

static void soc15_init_golden_registers(struct amdgpu_device *adev)
{
	/* Some of the registers might be dependent on GRBM_GFX_INDEX */
	mutex_lock(&adev->grbm_idx_mutex);

	switch (adev->asic_type) {
	case CHIP_VEGA10:
		amdgpu_program_register_sequence(adev,
						 vega10_golden_init,
						 (const u32)ARRAY_SIZE(vega10_golden_init));
		break;
	case CHIP_RAVEN:
		amdgpu_program_register_sequence(adev,
						 raven_golden_init,
						 (const u32)ARRAY_SIZE(raven_golden_init));
		break;
	default:
		break;
	}
	mutex_unlock(&adev->grbm_idx_mutex);
}
static u32 soc15_get_xclk(struct amdgpu_device *adev)
{
	return adev->clock.spll.reference_freq;
}


void soc15_grbm_select(struct amdgpu_device *adev,
		     u32 me, u32 pipe, u32 queue, u32 vmid)
{
	u32 grbm_gfx_cntl = 0;
	grbm_gfx_cntl = REG_SET_FIELD(grbm_gfx_cntl, GRBM_GFX_CNTL, PIPEID, pipe);
	grbm_gfx_cntl = REG_SET_FIELD(grbm_gfx_cntl, GRBM_GFX_CNTL, MEID, me);
	grbm_gfx_cntl = REG_SET_FIELD(grbm_gfx_cntl, GRBM_GFX_CNTL, VMID, vmid);
	grbm_gfx_cntl = REG_SET_FIELD(grbm_gfx_cntl, GRBM_GFX_CNTL, QUEUEID, queue);

	WREG32(SOC15_REG_OFFSET(GC, 0, mmGRBM_GFX_CNTL), grbm_gfx_cntl);
}

static void soc15_vga_set_state(struct amdgpu_device *adev, bool state)
{
	/* todo */
}

static bool soc15_read_disabled_bios(struct amdgpu_device *adev)
{
	/* todo */
	return false;
}

static bool soc15_read_bios_from_rom(struct amdgpu_device *adev,
				     u8 *bios, u32 length_bytes)
{
	u32 *dw_ptr;
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

	/* set rom index to 0 */
	WREG32(SOC15_REG_OFFSET(SMUIO, 0, mmROM_INDEX), 0);
	/* read out the rom data */
	for (i = 0; i < length_dw; i++)
		dw_ptr[i] = RREG32(SOC15_REG_OFFSET(SMUIO, 0, mmROM_DATA));

	return true;
}

static struct amdgpu_allowed_register_entry soc15_allowed_read_registers[] = {
	{ SOC15_REG_OFFSET(GC, 0, mmGRBM_STATUS)},
	{ SOC15_REG_OFFSET(GC, 0, mmGRBM_STATUS2)},
	{ SOC15_REG_OFFSET(GC, 0, mmGRBM_STATUS_SE0)},
	{ SOC15_REG_OFFSET(GC, 0, mmGRBM_STATUS_SE1)},
	{ SOC15_REG_OFFSET(GC, 0, mmGRBM_STATUS_SE2)},
	{ SOC15_REG_OFFSET(GC, 0, mmGRBM_STATUS_SE3)},
	{ SOC15_REG_OFFSET(SDMA0, 0, mmSDMA0_STATUS_REG)},
	{ SOC15_REG_OFFSET(SDMA1, 0, mmSDMA1_STATUS_REG)},
	{ SOC15_REG_OFFSET(GC, 0, mmCP_STAT)},
	{ SOC15_REG_OFFSET(GC, 0, mmCP_STALLED_STAT1)},
	{ SOC15_REG_OFFSET(GC, 0, mmCP_STALLED_STAT2)},
	{ SOC15_REG_OFFSET(GC, 0, mmCP_STALLED_STAT3)},
	{ SOC15_REG_OFFSET(GC, 0, mmCP_CPF_BUSY_STAT)},
	{ SOC15_REG_OFFSET(GC, 0, mmCP_CPF_STALLED_STAT1)},
	{ SOC15_REG_OFFSET(GC, 0, mmCP_CPF_STATUS)},
	{ SOC15_REG_OFFSET(GC, 0, mmCP_CPC_STALLED_STAT1)},
	{ SOC15_REG_OFFSET(GC, 0, mmCP_CPC_STATUS)},
	{ SOC15_REG_OFFSET(GC, 0, mmGB_ADDR_CONFIG)},
};

static uint32_t soc15_read_indexed_register(struct amdgpu_device *adev, u32 se_num,
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

static uint32_t soc15_get_register_value(struct amdgpu_device *adev,
					 bool indexed, u32 se_num,
					 u32 sh_num, u32 reg_offset)
{
	if (indexed) {
		return soc15_read_indexed_register(adev, se_num, sh_num, reg_offset);
	} else {
		switch (reg_offset) {
		case SOC15_REG_OFFSET(GC, 0, mmGB_ADDR_CONFIG):
			return adev->gfx.config.gb_addr_config;
		default:
			return RREG32(reg_offset);
		}
	}
}

static int soc15_read_register(struct amdgpu_device *adev, u32 se_num,
			    u32 sh_num, u32 reg_offset, u32 *value)
{
	uint32_t i;

	*value = 0;
	for (i = 0; i < ARRAY_SIZE(soc15_allowed_read_registers); i++) {
		if (reg_offset != soc15_allowed_read_registers[i].reg_offset)
			continue;

		*value = soc15_get_register_value(adev,
						  soc15_allowed_read_registers[i].grbm_indexed,
						  se_num, sh_num, reg_offset);
		return 0;
	}
	return -EINVAL;
}

static int soc15_asic_reset(struct amdgpu_device *adev)
{
	u32 i;

	amdgpu_atombios_scratch_regs_engine_hung(adev, true);

	dev_info(adev->dev, "GPU reset\n");

	/* disable BM */
	pci_clear_master(adev->pdev);

	pci_save_state(adev->pdev);

	for (i = 0; i < AMDGPU_MAX_IP_NUM; i++) {
		if (adev->ip_blocks[i].version->type == AMD_IP_BLOCK_TYPE_PSP){
			adev->ip_blocks[i].version->funcs->soft_reset((void *)adev);
			break;
		}
	}

	pci_restore_state(adev->pdev);

	/* wait for asic to come out of reset */
	for (i = 0; i < adev->usec_timeout; i++) {
		u32 memsize = (adev->flags & AMD_IS_APU) ?
			nbio_v7_0_get_memsize(adev) :
			nbio_v6_1_get_memsize(adev);
		if (memsize != 0xffffffff)
			break;
		udelay(1);
	}

	amdgpu_atombios_scratch_regs_engine_hung(adev, false);

	return 0;
}

/*static int soc15_set_uvd_clock(struct amdgpu_device *adev, u32 clock,
			u32 cntl_reg, u32 status_reg)
{
	return 0;
}*/

static int soc15_set_uvd_clocks(struct amdgpu_device *adev, u32 vclk, u32 dclk)
{
	/*int r;

	r = soc15_set_uvd_clock(adev, vclk, ixCG_VCLK_CNTL, ixCG_VCLK_STATUS);
	if (r)
		return r;

	r = soc15_set_uvd_clock(adev, dclk, ixCG_DCLK_CNTL, ixCG_DCLK_STATUS);
	*/
	return 0;
}

static int soc15_set_vce_clocks(struct amdgpu_device *adev, u32 evclk, u32 ecclk)
{
	/* todo */

	return 0;
}

static void soc15_pcie_gen3_enable(struct amdgpu_device *adev)
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

static void soc15_program_aspm(struct amdgpu_device *adev)
{

	if (amdgpu_aspm == 0)
		return;

	/* todo */
}

static void soc15_enable_doorbell_aperture(struct amdgpu_device *adev,
					bool enable)
{
	if (adev->flags & AMD_IS_APU) {
		nbio_v7_0_enable_doorbell_aperture(adev, enable);
	} else {
		nbio_v6_1_enable_doorbell_aperture(adev, enable);
		nbio_v6_1_enable_doorbell_selfring_aperture(adev, enable);
	}
}

static const struct amdgpu_ip_block_version vega10_common_ip_block =
{
	.type = AMD_IP_BLOCK_TYPE_COMMON,
	.major = 2,
	.minor = 0,
	.rev = 0,
	.funcs = &soc15_common_ip_funcs,
};

int soc15_set_ip_blocks(struct amdgpu_device *adev)
{
	nbio_v6_1_detect_hw_virt(adev);

	if (amdgpu_sriov_vf(adev))
		adev->virt.ops = &xgpu_ai_virt_ops;

	switch (adev->asic_type) {
	case CHIP_VEGA10:
		amdgpu_ip_block_add(adev, &vega10_common_ip_block);
		amdgpu_ip_block_add(adev, &gmc_v9_0_ip_block);
		amdgpu_ip_block_add(adev, &vega10_ih_ip_block);
		if (amdgpu_fw_load_type == 2 || amdgpu_fw_load_type == -1)
			amdgpu_ip_block_add(adev, &psp_v3_1_ip_block);
		if (!amdgpu_sriov_vf(adev))
			amdgpu_ip_block_add(adev, &amdgpu_pp_ip_block);
		if (adev->enable_virtual_display || amdgpu_sriov_vf(adev))
			amdgpu_ip_block_add(adev, &dce_virtual_ip_block);
#if defined(CONFIG_DRM_AMD_DC)
		else if (amdgpu_device_has_dc_support(adev))
			amdgpu_ip_block_add(adev, &dm_ip_block);
#else
#	warning "Enable CONFIG_DRM_AMD_DC for display support on SOC15."
#endif
		amdgpu_ip_block_add(adev, &gfx_v9_0_ip_block);
		amdgpu_ip_block_add(adev, &sdma_v4_0_ip_block);
		amdgpu_ip_block_add(adev, &uvd_v7_0_ip_block);
		amdgpu_ip_block_add(adev, &vce_v4_0_ip_block);
		break;
	case CHIP_RAVEN:
		amdgpu_ip_block_add(adev, &vega10_common_ip_block);
		amdgpu_ip_block_add(adev, &gmc_v9_0_ip_block);
		amdgpu_ip_block_add(adev, &vega10_ih_ip_block);
		amdgpu_ip_block_add(adev, &psp_v10_0_ip_block);
		amdgpu_ip_block_add(adev, &amdgpu_pp_ip_block);
		if (adev->enable_virtual_display || amdgpu_sriov_vf(adev))
			amdgpu_ip_block_add(adev, &dce_virtual_ip_block);
#if defined(CONFIG_DRM_AMD_DC)
		else if (amdgpu_device_has_dc_support(adev))
			amdgpu_ip_block_add(adev, &dm_ip_block);
#else
#	warning "Enable CONFIG_DRM_AMD_DC for display support on SOC15."
#endif
		amdgpu_ip_block_add(adev, &gfx_v9_0_ip_block);
		amdgpu_ip_block_add(adev, &sdma_v4_0_ip_block);
		amdgpu_ip_block_add(adev, &vcn_v1_0_ip_block);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static uint32_t soc15_get_rev_id(struct amdgpu_device *adev)
{
	if (adev->flags & AMD_IS_APU)
		return nbio_v7_0_get_rev_id(adev);
	else
		return nbio_v6_1_get_rev_id(adev);
}

static const struct amdgpu_asic_funcs soc15_asic_funcs =
{
	.read_disabled_bios = &soc15_read_disabled_bios,
	.read_bios_from_rom = &soc15_read_bios_from_rom,
	.read_register = &soc15_read_register,
	.reset = &soc15_asic_reset,
	.set_vga_state = &soc15_vga_set_state,
	.get_xclk = &soc15_get_xclk,
	.set_uvd_clocks = &soc15_set_uvd_clocks,
	.set_vce_clocks = &soc15_set_vce_clocks,
	.get_config_memsize = &soc15_get_config_memsize,
};

static int soc15_common_early_init(void *handle)
{
	bool psp_enabled = false;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	adev->smc_rreg = NULL;
	adev->smc_wreg = NULL;
	adev->pcie_rreg = &soc15_pcie_rreg;
	adev->pcie_wreg = &soc15_pcie_wreg;
	adev->uvd_ctx_rreg = &soc15_uvd_ctx_rreg;
	adev->uvd_ctx_wreg = &soc15_uvd_ctx_wreg;
	adev->didt_rreg = &soc15_didt_rreg;
	adev->didt_wreg = &soc15_didt_wreg;
	adev->gc_cac_rreg = &soc15_gc_cac_rreg;
	adev->gc_cac_wreg = &soc15_gc_cac_wreg;
	adev->se_cac_rreg = &soc15_se_cac_rreg;
	adev->se_cac_wreg = &soc15_se_cac_wreg;

	adev->asic_funcs = &soc15_asic_funcs;

	if (amdgpu_get_ip_block(adev, AMD_IP_BLOCK_TYPE_PSP) &&
		(amdgpu_ip_block_mask & (1 << AMD_IP_BLOCK_TYPE_PSP)))
		psp_enabled = true;

	adev->rev_id = soc15_get_rev_id(adev);
	adev->external_rev_id = 0xFF;
	switch (adev->asic_type) {
	case CHIP_VEGA10:
		adev->cg_flags = AMD_CG_SUPPORT_GFX_MGCG |
			AMD_CG_SUPPORT_GFX_MGLS |
			AMD_CG_SUPPORT_GFX_RLC_LS |
			AMD_CG_SUPPORT_GFX_CP_LS |
			AMD_CG_SUPPORT_GFX_3D_CGCG |
			AMD_CG_SUPPORT_GFX_3D_CGLS |
			AMD_CG_SUPPORT_GFX_CGCG |
			AMD_CG_SUPPORT_GFX_CGLS |
			AMD_CG_SUPPORT_BIF_MGCG |
			AMD_CG_SUPPORT_BIF_LS |
			AMD_CG_SUPPORT_HDP_LS |
			AMD_CG_SUPPORT_DRM_MGCG |
			AMD_CG_SUPPORT_DRM_LS |
			AMD_CG_SUPPORT_ROM_MGCG |
			AMD_CG_SUPPORT_DF_MGCG |
			AMD_CG_SUPPORT_SDMA_MGCG |
			AMD_CG_SUPPORT_SDMA_LS |
			AMD_CG_SUPPORT_MC_MGCG |
			AMD_CG_SUPPORT_MC_LS;
		adev->pg_flags = 0;
		adev->external_rev_id = 0x1;
		break;
	case CHIP_RAVEN:
		adev->cg_flags = AMD_CG_SUPPORT_GFX_MGCG |
			AMD_CG_SUPPORT_GFX_MGLS |
			AMD_CG_SUPPORT_GFX_RLC_LS |
			AMD_CG_SUPPORT_GFX_CP_LS |
			AMD_CG_SUPPORT_GFX_3D_CGCG |
			AMD_CG_SUPPORT_GFX_3D_CGLS |
			AMD_CG_SUPPORT_GFX_CGCG |
			AMD_CG_SUPPORT_GFX_CGLS |
			AMD_CG_SUPPORT_BIF_MGCG |
			AMD_CG_SUPPORT_BIF_LS |
			AMD_CG_SUPPORT_HDP_MGCG |
			AMD_CG_SUPPORT_HDP_LS |
			AMD_CG_SUPPORT_DRM_MGCG |
			AMD_CG_SUPPORT_DRM_LS |
			AMD_CG_SUPPORT_ROM_MGCG |
			AMD_CG_SUPPORT_MC_MGCG |
			AMD_CG_SUPPORT_MC_LS |
			AMD_CG_SUPPORT_SDMA_MGCG |
			AMD_CG_SUPPORT_SDMA_LS;
		adev->pg_flags = AMD_PG_SUPPORT_SDMA |
				 AMD_PG_SUPPORT_MMHUB;
		adev->external_rev_id = 0x1;
		break;
	default:
		/* FIXME: not supported yet */
		return -EINVAL;
	}

	if (amdgpu_sriov_vf(adev)) {
		amdgpu_virt_init_setting(adev);
		xgpu_ai_mailbox_set_irq_funcs(adev);
	}

	adev->firmware.load_type = amdgpu_ucode_get_load_type(adev, amdgpu_fw_load_type);

	amdgpu_get_pcie_info(adev);

	return 0;
}

static int soc15_common_late_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (amdgpu_sriov_vf(adev))
		xgpu_ai_mailbox_get_irq(adev);

	return 0;
}

static int soc15_common_sw_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (amdgpu_sriov_vf(adev))
		xgpu_ai_mailbox_add_irq_id(adev);

	return 0;
}

static int soc15_common_sw_fini(void *handle)
{
	return 0;
}

static int soc15_common_hw_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	/* move the golden regs per IP block */
	soc15_init_golden_registers(adev);
	/* enable pcie gen2/3 link */
	soc15_pcie_gen3_enable(adev);
	/* enable aspm */
	soc15_program_aspm(adev);
	/* setup nbio registers */
	if (!(adev->flags & AMD_IS_APU))
		nbio_v6_1_init_registers(adev);
	/* enable the doorbell aperture */
	soc15_enable_doorbell_aperture(adev, true);

	return 0;
}

static int soc15_common_hw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	/* disable the doorbell aperture */
	soc15_enable_doorbell_aperture(adev, false);
	if (amdgpu_sriov_vf(adev))
		xgpu_ai_mailbox_put_irq(adev);

	return 0;
}

static int soc15_common_suspend(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	return soc15_common_hw_fini(adev);
}

static int soc15_common_resume(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	return soc15_common_hw_init(adev);
}

static bool soc15_common_is_idle(void *handle)
{
	return true;
}

static int soc15_common_wait_for_idle(void *handle)
{
	return 0;
}

static int soc15_common_soft_reset(void *handle)
{
	return 0;
}

static void soc15_update_hdp_light_sleep(struct amdgpu_device *adev, bool enable)
{
	uint32_t def, data;

	def = data = RREG32(SOC15_REG_OFFSET(HDP, 0, mmHDP_MEM_POWER_LS));

	if (enable && (adev->cg_flags & AMD_CG_SUPPORT_HDP_LS))
		data |= HDP_MEM_POWER_LS__LS_ENABLE_MASK;
	else
		data &= ~HDP_MEM_POWER_LS__LS_ENABLE_MASK;

	if (def != data)
		WREG32(SOC15_REG_OFFSET(HDP, 0, mmHDP_MEM_POWER_LS), data);
}

static void soc15_update_drm_clock_gating(struct amdgpu_device *adev, bool enable)
{
	uint32_t def, data;

	def = data = RREG32(SOC15_REG_OFFSET(MP0, 0, mmMP0_MISC_CGTT_CTRL0));

	if (enable && (adev->cg_flags & AMD_CG_SUPPORT_DRM_MGCG))
		data &= ~(0x01000000 |
			  0x02000000 |
			  0x04000000 |
			  0x08000000 |
			  0x10000000 |
			  0x20000000 |
			  0x40000000 |
			  0x80000000);
	else
		data |= (0x01000000 |
			 0x02000000 |
			 0x04000000 |
			 0x08000000 |
			 0x10000000 |
			 0x20000000 |
			 0x40000000 |
			 0x80000000);

	if (def != data)
		WREG32(SOC15_REG_OFFSET(MP0, 0, mmMP0_MISC_CGTT_CTRL0), data);
}

static void soc15_update_drm_light_sleep(struct amdgpu_device *adev, bool enable)
{
	uint32_t def, data;

	def = data = RREG32(SOC15_REG_OFFSET(MP0, 0, mmMP0_MISC_LIGHT_SLEEP_CTRL));

	if (enable && (adev->cg_flags & AMD_CG_SUPPORT_DRM_LS))
		data |= 1;
	else
		data &= ~1;

	if (def != data)
		WREG32(SOC15_REG_OFFSET(MP0, 0, mmMP0_MISC_LIGHT_SLEEP_CTRL), data);
}

static void soc15_update_rom_medium_grain_clock_gating(struct amdgpu_device *adev,
						       bool enable)
{
	uint32_t def, data;

	def = data = RREG32(SOC15_REG_OFFSET(SMUIO, 0, mmCGTT_ROM_CLK_CTRL0));

	if (enable && (adev->cg_flags & AMD_CG_SUPPORT_ROM_MGCG))
		data &= ~(CGTT_ROM_CLK_CTRL0__SOFT_OVERRIDE0_MASK |
			CGTT_ROM_CLK_CTRL0__SOFT_OVERRIDE1_MASK);
	else
		data |= CGTT_ROM_CLK_CTRL0__SOFT_OVERRIDE0_MASK |
			CGTT_ROM_CLK_CTRL0__SOFT_OVERRIDE1_MASK;

	if (def != data)
		WREG32(SOC15_REG_OFFSET(SMUIO, 0, mmCGTT_ROM_CLK_CTRL0), data);
}

static void soc15_update_df_medium_grain_clock_gating(struct amdgpu_device *adev,
						       bool enable)
{
	uint32_t data;

	/* Put DF on broadcast mode */
	data = RREG32(SOC15_REG_OFFSET(DF, 0, mmFabricConfigAccessControl));
	data &= ~FabricConfigAccessControl__CfgRegInstAccEn_MASK;
	WREG32(SOC15_REG_OFFSET(DF, 0, mmFabricConfigAccessControl), data);

	if (enable && (adev->cg_flags & AMD_CG_SUPPORT_DF_MGCG)) {
		data = RREG32(SOC15_REG_OFFSET(DF, 0, mmDF_PIE_AON0_DfGlobalClkGater));
		data &= ~DF_PIE_AON0_DfGlobalClkGater__MGCGMode_MASK;
		data |= DF_MGCG_ENABLE_15_CYCLE_DELAY;
		WREG32(SOC15_REG_OFFSET(DF, 0, mmDF_PIE_AON0_DfGlobalClkGater), data);
	} else {
		data = RREG32(SOC15_REG_OFFSET(DF, 0, mmDF_PIE_AON0_DfGlobalClkGater));
		data &= ~DF_PIE_AON0_DfGlobalClkGater__MGCGMode_MASK;
		data |= DF_MGCG_DISABLE;
		WREG32(SOC15_REG_OFFSET(DF, 0, mmDF_PIE_AON0_DfGlobalClkGater), data);
	}

	WREG32(SOC15_REG_OFFSET(DF, 0, mmFabricConfigAccessControl),
	       mmFabricConfigAccessControl_DEFAULT);
}

static int soc15_common_set_clockgating_state(void *handle,
					    enum amd_clockgating_state state)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (amdgpu_sriov_vf(adev))
		return 0;

	switch (adev->asic_type) {
	case CHIP_VEGA10:
		nbio_v6_1_update_medium_grain_clock_gating(adev,
				state == AMD_CG_STATE_GATE ? true : false);
		nbio_v6_1_update_medium_grain_light_sleep(adev,
				state == AMD_CG_STATE_GATE ? true : false);
		soc15_update_hdp_light_sleep(adev,
				state == AMD_CG_STATE_GATE ? true : false);
		soc15_update_drm_clock_gating(adev,
				state == AMD_CG_STATE_GATE ? true : false);
		soc15_update_drm_light_sleep(adev,
				state == AMD_CG_STATE_GATE ? true : false);
		soc15_update_rom_medium_grain_clock_gating(adev,
				state == AMD_CG_STATE_GATE ? true : false);
		soc15_update_df_medium_grain_clock_gating(adev,
				state == AMD_CG_STATE_GATE ? true : false);
		break;
	case CHIP_RAVEN:
		nbio_v7_0_update_medium_grain_clock_gating(adev,
				state == AMD_CG_STATE_GATE ? true : false);
		nbio_v6_1_update_medium_grain_light_sleep(adev,
				state == AMD_CG_STATE_GATE ? true : false);
		soc15_update_hdp_light_sleep(adev,
				state == AMD_CG_STATE_GATE ? true : false);
		soc15_update_drm_clock_gating(adev,
				state == AMD_CG_STATE_GATE ? true : false);
		soc15_update_drm_light_sleep(adev,
				state == AMD_CG_STATE_GATE ? true : false);
		soc15_update_rom_medium_grain_clock_gating(adev,
				state == AMD_CG_STATE_GATE ? true : false);
		break;
	default:
		break;
	}
	return 0;
}

static void soc15_common_get_clockgating_state(void *handle, u32 *flags)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int data;

	if (amdgpu_sriov_vf(adev))
		*flags = 0;

	nbio_v6_1_get_clockgating_state(adev, flags);

	/* AMD_CG_SUPPORT_HDP_LS */
	data = RREG32(SOC15_REG_OFFSET(HDP, 0, mmHDP_MEM_POWER_LS));
	if (data & HDP_MEM_POWER_LS__LS_ENABLE_MASK)
		*flags |= AMD_CG_SUPPORT_HDP_LS;

	/* AMD_CG_SUPPORT_DRM_MGCG */
	data = RREG32(SOC15_REG_OFFSET(MP0, 0, mmMP0_MISC_CGTT_CTRL0));
	if (!(data & 0x01000000))
		*flags |= AMD_CG_SUPPORT_DRM_MGCG;

	/* AMD_CG_SUPPORT_DRM_LS */
	data = RREG32(SOC15_REG_OFFSET(MP0, 0, mmMP0_MISC_LIGHT_SLEEP_CTRL));
	if (data & 0x1)
		*flags |= AMD_CG_SUPPORT_DRM_LS;

	/* AMD_CG_SUPPORT_ROM_MGCG */
	data = RREG32(SOC15_REG_OFFSET(SMUIO, 0, mmCGTT_ROM_CLK_CTRL0));
	if (!(data & CGTT_ROM_CLK_CTRL0__SOFT_OVERRIDE0_MASK))
		*flags |= AMD_CG_SUPPORT_ROM_MGCG;

	/* AMD_CG_SUPPORT_DF_MGCG */
	data = RREG32(SOC15_REG_OFFSET(DF, 0, mmDF_PIE_AON0_DfGlobalClkGater));
	if (data & DF_MGCG_ENABLE_15_CYCLE_DELAY)
		*flags |= AMD_CG_SUPPORT_DF_MGCG;
}

static int soc15_common_set_powergating_state(void *handle,
					    enum amd_powergating_state state)
{
	/* todo */
	return 0;
}

const struct amd_ip_funcs soc15_common_ip_funcs = {
	.name = "soc15_common",
	.early_init = soc15_common_early_init,
	.late_init = soc15_common_late_init,
	.sw_init = soc15_common_sw_init,
	.sw_fini = soc15_common_sw_fini,
	.hw_init = soc15_common_hw_init,
	.hw_fini = soc15_common_hw_fini,
	.suspend = soc15_common_suspend,
	.resume = soc15_common_resume,
	.is_idle = soc15_common_is_idle,
	.wait_for_idle = soc15_common_wait_for_idle,
	.soft_reset = soc15_common_soft_reset,
	.set_clockgating_state = soc15_common_set_clockgating_state,
	.set_powergating_state = soc15_common_set_powergating_state,
	.get_clockgating_state= soc15_common_get_clockgating_state,
};
