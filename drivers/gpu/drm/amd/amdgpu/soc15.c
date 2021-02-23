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
#include <linux/pci.h>

#include "amdgpu.h"
#include "amdgpu_atombios.h"
#include "amdgpu_ih.h"
#include "amdgpu_uvd.h"
#include "amdgpu_vce.h"
#include "amdgpu_ucode.h"
#include "amdgpu_psp.h"
#include "atom.h"
#include "amd_pcie.h"

#include "uvd/uvd_7_0_offset.h"
#include "gc/gc_9_0_offset.h"
#include "gc/gc_9_0_sh_mask.h"
#include "sdma0/sdma0_4_0_offset.h"
#include "sdma1/sdma1_4_0_offset.h"
#include "hdp/hdp_4_0_offset.h"
#include "hdp/hdp_4_0_sh_mask.h"
#include "nbio/nbio_7_0_default.h"
#include "nbio/nbio_7_0_offset.h"
#include "nbio/nbio_7_0_sh_mask.h"
#include "nbio/nbio_7_0_smn.h"
#include "mp/mp_9_0_offset.h"

#include "soc15.h"
#include "soc15_common.h"
#include "gfx_v9_0.h"
#include "gmc_v9_0.h"
#include "gfxhub_v1_0.h"
#include "mmhub_v1_0.h"
#include "df_v1_7.h"
#include "df_v3_6.h"
#include "nbio_v6_1.h"
#include "nbio_v7_0.h"
#include "nbio_v7_4.h"
#include "vega10_ih.h"
#include "navi10_ih.h"
#include "sdma_v4_0.h"
#include "uvd_v7_0.h"
#include "vce_v4_0.h"
#include "vcn_v1_0.h"
#include "vcn_v2_0.h"
#include "jpeg_v2_0.h"
#include "vcn_v2_5.h"
#include "jpeg_v2_5.h"
#include "smuio_v9_0.h"
#include "smuio_v11_0.h"
#include "dce_virtual.h"
#include "mxgpu_ai.h"
#include "amdgpu_smu.h"
#include "amdgpu_ras.h"
#include "amdgpu_xgmi.h"
#include <uapi/linux/kfd_ioctl.h>

#define mmMP0_MISC_CGTT_CTRL0                                                                   0x01b9
#define mmMP0_MISC_CGTT_CTRL0_BASE_IDX                                                          0
#define mmMP0_MISC_LIGHT_SLEEP_CTRL                                                             0x01ba
#define mmMP0_MISC_LIGHT_SLEEP_CTRL_BASE_IDX                                                    0

/* for Vega20 register name change */
#define mmHDP_MEM_POWER_CTRL	0x00d4
#define HDP_MEM_POWER_CTRL__IPH_MEM_POWER_CTRL_EN_MASK	0x00000001L
#define HDP_MEM_POWER_CTRL__IPH_MEM_POWER_LS_EN_MASK	0x00000002L
#define HDP_MEM_POWER_CTRL__RC_MEM_POWER_CTRL_EN_MASK	0x00010000L
#define HDP_MEM_POWER_CTRL__RC_MEM_POWER_LS_EN_MASK		0x00020000L
#define mmHDP_MEM_POWER_CTRL_BASE_IDX	0

/*
 * Indirect registers accessor
 */
static u32 soc15_pcie_rreg(struct amdgpu_device *adev, u32 reg)
{
	unsigned long address, data;
	address = adev->nbio.funcs->get_pcie_index_offset(adev);
	data = adev->nbio.funcs->get_pcie_data_offset(adev);

	return amdgpu_device_indirect_rreg(adev, address, data, reg);
}

static void soc15_pcie_wreg(struct amdgpu_device *adev, u32 reg, u32 v)
{
	unsigned long address, data;

	address = adev->nbio.funcs->get_pcie_index_offset(adev);
	data = adev->nbio.funcs->get_pcie_data_offset(adev);

	amdgpu_device_indirect_wreg(adev, address, data, reg, v);
}

static u64 soc15_pcie_rreg64(struct amdgpu_device *adev, u32 reg)
{
	unsigned long address, data;
	address = adev->nbio.funcs->get_pcie_index_offset(adev);
	data = adev->nbio.funcs->get_pcie_data_offset(adev);

	return amdgpu_device_indirect_rreg64(adev, address, data, reg);
}

static void soc15_pcie_wreg64(struct amdgpu_device *adev, u32 reg, u64 v)
{
	unsigned long address, data;

	address = adev->nbio.funcs->get_pcie_index_offset(adev);
	data = adev->nbio.funcs->get_pcie_data_offset(adev);

	amdgpu_device_indirect_wreg64(adev, address, data, reg, v);
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
	return adev->nbio.funcs->get_memsize(adev);
}

static u32 soc15_get_xclk(struct amdgpu_device *adev)
{
	u32 reference_clock = adev->clock.spll.reference_freq;

	if (adev->asic_type == CHIP_RAVEN)
		return reference_clock / 4;

	return reference_clock;
}


void soc15_grbm_select(struct amdgpu_device *adev,
		     u32 me, u32 pipe, u32 queue, u32 vmid)
{
	u32 grbm_gfx_cntl = 0;
	grbm_gfx_cntl = REG_SET_FIELD(grbm_gfx_cntl, GRBM_GFX_CNTL, PIPEID, pipe);
	grbm_gfx_cntl = REG_SET_FIELD(grbm_gfx_cntl, GRBM_GFX_CNTL, MEID, me);
	grbm_gfx_cntl = REG_SET_FIELD(grbm_gfx_cntl, GRBM_GFX_CNTL, VMID, vmid);
	grbm_gfx_cntl = REG_SET_FIELD(grbm_gfx_cntl, GRBM_GFX_CNTL, QUEUEID, queue);

	WREG32_SOC15_RLC_SHADOW(GC, 0, mmGRBM_GFX_CNTL, grbm_gfx_cntl);
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
	uint32_t rom_index_offset;
	uint32_t rom_data_offset;

	if (bios == NULL)
		return false;
	if (length_bytes == 0)
		return false;
	/* APU vbios image is part of sbios image */
	if (adev->flags & AMD_IS_APU)
		return false;

	dw_ptr = (u32 *)bios;
	length_dw = ALIGN(length_bytes, 4) / 4;

	rom_index_offset =
		adev->smuio.funcs->get_rom_index_offset(adev);
	rom_data_offset =
		adev->smuio.funcs->get_rom_data_offset(adev);

	/* set rom index to 0 */
	WREG32(rom_index_offset, 0);
	/* read out the rom data */
	for (i = 0; i < length_dw; i++)
		dw_ptr[i] = RREG32(rom_data_offset);

	return true;
}

static struct soc15_allowed_register_entry soc15_allowed_read_registers[] = {
	{ SOC15_REG_ENTRY(GC, 0, mmGRBM_STATUS)},
	{ SOC15_REG_ENTRY(GC, 0, mmGRBM_STATUS2)},
	{ SOC15_REG_ENTRY(GC, 0, mmGRBM_STATUS_SE0)},
	{ SOC15_REG_ENTRY(GC, 0, mmGRBM_STATUS_SE1)},
	{ SOC15_REG_ENTRY(GC, 0, mmGRBM_STATUS_SE2)},
	{ SOC15_REG_ENTRY(GC, 0, mmGRBM_STATUS_SE3)},
	{ SOC15_REG_ENTRY(SDMA0, 0, mmSDMA0_STATUS_REG)},
	{ SOC15_REG_ENTRY(SDMA1, 0, mmSDMA1_STATUS_REG)},
	{ SOC15_REG_ENTRY(GC, 0, mmCP_STAT)},
	{ SOC15_REG_ENTRY(GC, 0, mmCP_STALLED_STAT1)},
	{ SOC15_REG_ENTRY(GC, 0, mmCP_STALLED_STAT2)},
	{ SOC15_REG_ENTRY(GC, 0, mmCP_STALLED_STAT3)},
	{ SOC15_REG_ENTRY(GC, 0, mmCP_CPF_BUSY_STAT)},
	{ SOC15_REG_ENTRY(GC, 0, mmCP_CPF_STALLED_STAT1)},
	{ SOC15_REG_ENTRY(GC, 0, mmCP_CPF_STATUS)},
	{ SOC15_REG_ENTRY(GC, 0, mmCP_CPC_BUSY_STAT)},
	{ SOC15_REG_ENTRY(GC, 0, mmCP_CPC_STALLED_STAT1)},
	{ SOC15_REG_ENTRY(GC, 0, mmCP_CPC_STATUS)},
	{ SOC15_REG_ENTRY(GC, 0, mmGB_ADDR_CONFIG)},
	{ SOC15_REG_ENTRY(GC, 0, mmDB_DEBUG2)},
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
		if (reg_offset == SOC15_REG_OFFSET(GC, 0, mmGB_ADDR_CONFIG))
			return adev->gfx.config.gb_addr_config;
		else if (reg_offset == SOC15_REG_OFFSET(GC, 0, mmDB_DEBUG2))
			return adev->gfx.config.db_debug2;
		return RREG32(reg_offset);
	}
}

static int soc15_read_register(struct amdgpu_device *adev, u32 se_num,
			    u32 sh_num, u32 reg_offset, u32 *value)
{
	uint32_t i;
	struct soc15_allowed_register_entry  *en;

	*value = 0;
	for (i = 0; i < ARRAY_SIZE(soc15_allowed_read_registers); i++) {
		en = &soc15_allowed_read_registers[i];
		if (adev->reg_offset[en->hwip][en->inst] &&
			reg_offset != (adev->reg_offset[en->hwip][en->inst][en->seg]
					+ en->reg_offset))
			continue;

		*value = soc15_get_register_value(adev,
						  soc15_allowed_read_registers[i].grbm_indexed,
						  se_num, sh_num, reg_offset);
		return 0;
	}
	return -EINVAL;
}


/**
 * soc15_program_register_sequence - program an array of registers.
 *
 * @adev: amdgpu_device pointer
 * @regs: pointer to the register array
 * @array_size: size of the register array
 *
 * Programs an array or registers with and and or masks.
 * This is a helper for setting golden registers.
 */

void soc15_program_register_sequence(struct amdgpu_device *adev,
					     const struct soc15_reg_golden *regs,
					     const u32 array_size)
{
	const struct soc15_reg_golden *entry;
	u32 tmp, reg;
	int i;

	for (i = 0; i < array_size; ++i) {
		entry = &regs[i];
		reg =  adev->reg_offset[entry->hwip][entry->instance][entry->segment] + entry->reg;

		if (entry->and_mask == 0xffffffff) {
			tmp = entry->or_mask;
		} else {
			tmp = RREG32(reg);
			tmp &= ~(entry->and_mask);
			tmp |= (entry->or_mask & entry->and_mask);
		}

		if (reg == SOC15_REG_OFFSET(GC, 0, mmPA_SC_BINNER_EVENT_CNTL_3) ||
			reg == SOC15_REG_OFFSET(GC, 0, mmPA_SC_ENHANCE) ||
			reg == SOC15_REG_OFFSET(GC, 0, mmPA_SC_ENHANCE_1) ||
			reg == SOC15_REG_OFFSET(GC, 0, mmSH_MEM_CONFIG))
			WREG32_RLC(reg, tmp);
		else
			WREG32(reg, tmp);

	}

}

static int soc15_asic_mode1_reset(struct amdgpu_device *adev)
{
	u32 i;
	int ret = 0;

	amdgpu_atombios_scratch_regs_engine_hung(adev, true);

	dev_info(adev->dev, "GPU mode1 reset\n");

	/* disable BM */
	pci_clear_master(adev->pdev);

	amdgpu_device_cache_pci_state(adev->pdev);

	ret = psp_gpu_reset(adev);
	if (ret)
		dev_err(adev->dev, "GPU mode1 reset failed\n");

	amdgpu_device_load_pci_state(adev->pdev);

	/* wait for asic to come out of reset */
	for (i = 0; i < adev->usec_timeout; i++) {
		u32 memsize = adev->nbio.funcs->get_memsize(adev);

		if (memsize != 0xffffffff)
			break;
		udelay(1);
	}

	amdgpu_atombios_scratch_regs_engine_hung(adev, false);

	return ret;
}

static int soc15_asic_baco_reset(struct amdgpu_device *adev)
{
	struct amdgpu_ras *ras = amdgpu_ras_get_context(adev);
	int ret = 0;

	/* avoid NBIF got stuck when do RAS recovery in BACO reset */
	if (ras && ras->supported)
		adev->nbio.funcs->enable_doorbell_interrupt(adev, false);

	ret = amdgpu_dpm_baco_reset(adev);
	if (ret)
		return ret;

	/* re-enable doorbell interrupt after BACO exit */
	if (ras && ras->supported)
		adev->nbio.funcs->enable_doorbell_interrupt(adev, true);

	return 0;
}

static enum amd_reset_method
soc15_asic_reset_method(struct amdgpu_device *adev)
{
	bool baco_reset = false;
	struct amdgpu_ras *ras = amdgpu_ras_get_context(adev);

	if (amdgpu_reset_method == AMD_RESET_METHOD_MODE1 ||
	    amdgpu_reset_method == AMD_RESET_METHOD_MODE2 ||
		amdgpu_reset_method == AMD_RESET_METHOD_BACO)
		return amdgpu_reset_method;

	if (amdgpu_reset_method != -1)
		dev_warn(adev->dev, "Specified reset method:%d isn't supported, using AUTO instead.\n",
				  amdgpu_reset_method);

	switch (adev->asic_type) {
	case CHIP_RAVEN:
	case CHIP_RENOIR:
		return AMD_RESET_METHOD_MODE2;
	case CHIP_VEGA10:
	case CHIP_VEGA12:
	case CHIP_ARCTURUS:
		baco_reset = amdgpu_dpm_is_baco_supported(adev);
		break;
	case CHIP_VEGA20:
		if (adev->psp.sos_fw_version >= 0x80067)
			baco_reset = amdgpu_dpm_is_baco_supported(adev);

		/*
		 * 1. PMFW version > 0x284300: all cases use baco
		 * 2. PMFW version <= 0x284300: only sGPU w/o RAS use baco
		 */
		if ((ras && ras->supported) && adev->pm.fw_version <= 0x283400)
			baco_reset = false;
		break;
	default:
		break;
	}

	if (baco_reset)
		return AMD_RESET_METHOD_BACO;
	else
		return AMD_RESET_METHOD_MODE1;
}

static int soc15_asic_reset(struct amdgpu_device *adev)
{
	/* original raven doesn't have full asic reset */
	if ((adev->apu_flags & AMD_APU_IS_RAVEN) &&
	    !(adev->apu_flags & AMD_APU_IS_RAVEN2))
		return 0;

	switch (soc15_asic_reset_method(adev)) {
		case AMD_RESET_METHOD_BACO:
			dev_info(adev->dev, "BACO reset\n");
			return soc15_asic_baco_reset(adev);
		case AMD_RESET_METHOD_MODE2:
			dev_info(adev->dev, "MODE2 reset\n");
			return amdgpu_dpm_mode2_reset(adev);
		default:
			dev_info(adev->dev, "MODE1 reset\n");
			return soc15_asic_mode1_reset(adev);
	}
}

static bool soc15_supports_baco(struct amdgpu_device *adev)
{
	switch (adev->asic_type) {
	case CHIP_VEGA10:
	case CHIP_VEGA12:
	case CHIP_ARCTURUS:
		return amdgpu_dpm_is_baco_supported(adev);
	case CHIP_VEGA20:
		if (adev->psp.sos_fw_version >= 0x80067)
			return amdgpu_dpm_is_baco_supported(adev);
		return false;
	default:
		return false;
	}
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
	adev->nbio.funcs->enable_doorbell_aperture(adev, enable);
	adev->nbio.funcs->enable_doorbell_selfring_aperture(adev, enable);
}

static const struct amdgpu_ip_block_version vega10_common_ip_block =
{
	.type = AMD_IP_BLOCK_TYPE_COMMON,
	.major = 2,
	.minor = 0,
	.rev = 0,
	.funcs = &soc15_common_ip_funcs,
};

static uint32_t soc15_get_rev_id(struct amdgpu_device *adev)
{
	return adev->nbio.funcs->get_rev_id(adev);
}

static void soc15_reg_base_init(struct amdgpu_device *adev)
{
	int r;

	/* Set IP register base before any HW register access */
	switch (adev->asic_type) {
	case CHIP_VEGA10:
	case CHIP_VEGA12:
	case CHIP_RAVEN:
		vega10_reg_base_init(adev);
		break;
	case CHIP_RENOIR:
		/* It's safe to do ip discovery here for Renior,
		 * it doesn't support SRIOV. */
		if (amdgpu_discovery) {
			r = amdgpu_discovery_reg_base_init(adev);
			if (r == 0)
				break;
			DRM_WARN("failed to init reg base from ip discovery table, "
				 "fallback to legacy init method\n");
		}
		vega10_reg_base_init(adev);
		break;
	case CHIP_VEGA20:
		vega20_reg_base_init(adev);
		break;
	case CHIP_ARCTURUS:
		arct_reg_base_init(adev);
		break;
	default:
		DRM_ERROR("Unsupported asic type: %d!\n", adev->asic_type);
		break;
	}
}

void soc15_set_virt_ops(struct amdgpu_device *adev)
{
	adev->virt.ops = &xgpu_ai_virt_ops;

	/* init soc15 reg base early enough so we can
	 * request request full access for sriov before
	 * set_ip_blocks. */
	soc15_reg_base_init(adev);
}

int soc15_set_ip_blocks(struct amdgpu_device *adev)
{
	/* for bare metal case */
	if (!amdgpu_sriov_vf(adev))
		soc15_reg_base_init(adev);

	if (adev->asic_type == CHIP_VEGA20 || adev->asic_type == CHIP_ARCTURUS)
		adev->gmc.xgmi.supported = true;

	if (adev->flags & AMD_IS_APU) {
		adev->nbio.funcs = &nbio_v7_0_funcs;
		adev->nbio.hdp_flush_reg = &nbio_v7_0_hdp_flush_reg;
	} else if (adev->asic_type == CHIP_VEGA20 ||
		   adev->asic_type == CHIP_ARCTURUS) {
		adev->nbio.funcs = &nbio_v7_4_funcs;
		adev->nbio.hdp_flush_reg = &nbio_v7_4_hdp_flush_reg;
	} else {
		adev->nbio.funcs = &nbio_v6_1_funcs;
		adev->nbio.hdp_flush_reg = &nbio_v6_1_hdp_flush_reg;
	}

	if (adev->asic_type == CHIP_VEGA20 || adev->asic_type == CHIP_ARCTURUS)
		adev->df.funcs = &df_v3_6_funcs;
	else
		adev->df.funcs = &df_v1_7_funcs;

	if (adev->asic_type == CHIP_VEGA20 ||
	    adev->asic_type == CHIP_ARCTURUS)
		adev->smuio.funcs = &smuio_v11_0_funcs;
	else
		adev->smuio.funcs = &smuio_v9_0_funcs;

	adev->rev_id = soc15_get_rev_id(adev);

	switch (adev->asic_type) {
	case CHIP_VEGA10:
	case CHIP_VEGA12:
	case CHIP_VEGA20:
		amdgpu_device_ip_block_add(adev, &vega10_common_ip_block);
		amdgpu_device_ip_block_add(adev, &gmc_v9_0_ip_block);

		/* For Vega10 SR-IOV, PSP need to be initialized before IH */
		if (amdgpu_sriov_vf(adev)) {
			if (likely(adev->firmware.load_type == AMDGPU_FW_LOAD_PSP)) {
				if (adev->asic_type == CHIP_VEGA20)
					amdgpu_device_ip_block_add(adev, &psp_v11_0_ip_block);
				else
					amdgpu_device_ip_block_add(adev, &psp_v3_1_ip_block);
			}
			if (adev->asic_type == CHIP_VEGA20)
				amdgpu_device_ip_block_add(adev, &navi10_ih_ip_block);
			else
				amdgpu_device_ip_block_add(adev, &vega10_ih_ip_block);
		} else {
			if (adev->asic_type == CHIP_VEGA20)
				amdgpu_device_ip_block_add(adev, &navi10_ih_ip_block);
			else
				amdgpu_device_ip_block_add(adev, &vega10_ih_ip_block);
			if (likely(adev->firmware.load_type == AMDGPU_FW_LOAD_PSP)) {
				if (adev->asic_type == CHIP_VEGA20)
					amdgpu_device_ip_block_add(adev, &psp_v11_0_ip_block);
				else
					amdgpu_device_ip_block_add(adev, &psp_v3_1_ip_block);
			}
		}
		amdgpu_device_ip_block_add(adev, &gfx_v9_0_ip_block);
		amdgpu_device_ip_block_add(adev, &sdma_v4_0_ip_block);
		if (is_support_sw_smu(adev)) {
			if (!amdgpu_sriov_vf(adev))
				amdgpu_device_ip_block_add(adev, &smu_v11_0_ip_block);
		} else {
			amdgpu_device_ip_block_add(adev, &pp_smu_ip_block);
		}
		if (adev->enable_virtual_display || amdgpu_sriov_vf(adev))
			amdgpu_device_ip_block_add(adev, &dce_virtual_ip_block);
#if defined(CONFIG_DRM_AMD_DC)
		else if (amdgpu_device_has_dc_support(adev))
			amdgpu_device_ip_block_add(adev, &dm_ip_block);
#endif
		if (!(adev->asic_type == CHIP_VEGA20 && amdgpu_sriov_vf(adev))) {
			amdgpu_device_ip_block_add(adev, &uvd_v7_0_ip_block);
			amdgpu_device_ip_block_add(adev, &vce_v4_0_ip_block);
		}
		break;
	case CHIP_RAVEN:
		amdgpu_device_ip_block_add(adev, &vega10_common_ip_block);
		amdgpu_device_ip_block_add(adev, &gmc_v9_0_ip_block);
		amdgpu_device_ip_block_add(adev, &vega10_ih_ip_block);
		if (likely(adev->firmware.load_type == AMDGPU_FW_LOAD_PSP))
			amdgpu_device_ip_block_add(adev, &psp_v10_0_ip_block);
		amdgpu_device_ip_block_add(adev, &gfx_v9_0_ip_block);
		amdgpu_device_ip_block_add(adev, &sdma_v4_0_ip_block);
		amdgpu_device_ip_block_add(adev, &pp_smu_ip_block);
		if (adev->enable_virtual_display || amdgpu_sriov_vf(adev))
			amdgpu_device_ip_block_add(adev, &dce_virtual_ip_block);
#if defined(CONFIG_DRM_AMD_DC)
		else if (amdgpu_device_has_dc_support(adev))
			amdgpu_device_ip_block_add(adev, &dm_ip_block);
#endif
		amdgpu_device_ip_block_add(adev, &vcn_v1_0_ip_block);
		break;
	case CHIP_ARCTURUS:
		amdgpu_device_ip_block_add(adev, &vega10_common_ip_block);
		amdgpu_device_ip_block_add(adev, &gmc_v9_0_ip_block);

		if (amdgpu_sriov_vf(adev)) {
			if (likely(adev->firmware.load_type == AMDGPU_FW_LOAD_PSP))
				amdgpu_device_ip_block_add(adev, &psp_v11_0_ip_block);
			amdgpu_device_ip_block_add(adev, &navi10_ih_ip_block);
		} else {
			amdgpu_device_ip_block_add(adev, &navi10_ih_ip_block);
			if (likely(adev->firmware.load_type == AMDGPU_FW_LOAD_PSP))
				amdgpu_device_ip_block_add(adev, &psp_v11_0_ip_block);
		}

		if (adev->enable_virtual_display || amdgpu_sriov_vf(adev))
			amdgpu_device_ip_block_add(adev, &dce_virtual_ip_block);
		amdgpu_device_ip_block_add(adev, &gfx_v9_0_ip_block);
		amdgpu_device_ip_block_add(adev, &sdma_v4_0_ip_block);
		amdgpu_device_ip_block_add(adev, &smu_v11_0_ip_block);

		if (amdgpu_sriov_vf(adev)) {
			if (likely(adev->firmware.load_type == AMDGPU_FW_LOAD_PSP))
				amdgpu_device_ip_block_add(adev, &vcn_v2_5_ip_block);
		} else {
			amdgpu_device_ip_block_add(adev, &vcn_v2_5_ip_block);
		}
		if (!amdgpu_sriov_vf(adev))
			amdgpu_device_ip_block_add(adev, &jpeg_v2_5_ip_block);
		break;
	case CHIP_RENOIR:
		amdgpu_device_ip_block_add(adev, &vega10_common_ip_block);
		amdgpu_device_ip_block_add(adev, &gmc_v9_0_ip_block);
		amdgpu_device_ip_block_add(adev, &vega10_ih_ip_block);
		if (likely(adev->firmware.load_type == AMDGPU_FW_LOAD_PSP))
			amdgpu_device_ip_block_add(adev, &psp_v12_0_ip_block);
		amdgpu_device_ip_block_add(adev, &smu_v12_0_ip_block);
		amdgpu_device_ip_block_add(adev, &gfx_v9_0_ip_block);
		amdgpu_device_ip_block_add(adev, &sdma_v4_0_ip_block);
		if (adev->enable_virtual_display || amdgpu_sriov_vf(adev))
			amdgpu_device_ip_block_add(adev, &dce_virtual_ip_block);
#if defined(CONFIG_DRM_AMD_DC)
                else if (amdgpu_device_has_dc_support(adev))
			amdgpu_device_ip_block_add(adev, &dm_ip_block);
#endif
		amdgpu_device_ip_block_add(adev, &vcn_v2_0_ip_block);
		amdgpu_device_ip_block_add(adev, &jpeg_v2_0_ip_block);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static void soc15_flush_hdp(struct amdgpu_device *adev, struct amdgpu_ring *ring)
{
	adev->nbio.funcs->hdp_flush(adev, ring);
}

static void soc15_invalidate_hdp(struct amdgpu_device *adev,
				 struct amdgpu_ring *ring)
{
	if (!ring || !ring->funcs->emit_wreg)
		WREG32_SOC15_NO_KIQ(HDP, 0, mmHDP_READ_CACHE_INVALIDATE, 1);
	else
		amdgpu_ring_emit_wreg(ring, SOC15_REG_OFFSET(
			HDP, 0, mmHDP_READ_CACHE_INVALIDATE), 1);
}

static bool soc15_need_full_reset(struct amdgpu_device *adev)
{
	/* change this when we implement soft reset */
	return true;
}

static void vega20_reset_hdp_ras_error_count(struct amdgpu_device *adev)
{
	if (!amdgpu_ras_is_supported(adev, AMDGPU_RAS_BLOCK__HDP))
		return;
	/*read back hdp ras counter to reset it to 0 */
	RREG32_SOC15(HDP, 0, mmHDP_EDC_CNT);
}

static void soc15_get_pcie_usage(struct amdgpu_device *adev, uint64_t *count0,
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
	/* Reg 40 is # received msgs */
	/* Reg 104 is # of posted requests sent */
	perfctr = REG_SET_FIELD(perfctr, PCIE_PERF_CNTL_TXCLK, EVENT0_SEL, 40);
	perfctr = REG_SET_FIELD(perfctr, PCIE_PERF_CNTL_TXCLK, EVENT1_SEL, 104);

	/* Write to enable desired perf counters */
	WREG32_PCIE(smnPCIE_PERF_CNTL_TXCLK, perfctr);
	/* Zero out and enable the perf counters
	 * Write 0x5:
	 * Bit 0 = Start all counters(1)
	 * Bit 2 = Global counter reset enable(1)
	 */
	WREG32_PCIE(smnPCIE_PERF_COUNT_CNTL, 0x00000005);

	msleep(1000);

	/* Load the shadow and disable the perf counters
	 * Write 0x2:
	 * Bit 0 = Stop counters(0)
	 * Bit 1 = Load the shadow counters(1)
	 */
	WREG32_PCIE(smnPCIE_PERF_COUNT_CNTL, 0x00000002);

	/* Read register values to get any >32bit overflow */
	tmp = RREG32_PCIE(smnPCIE_PERF_CNTL_TXCLK);
	cnt0_of = REG_GET_FIELD(tmp, PCIE_PERF_CNTL_TXCLK, COUNTER0_UPPER);
	cnt1_of = REG_GET_FIELD(tmp, PCIE_PERF_CNTL_TXCLK, COUNTER1_UPPER);

	/* Get the values and add the overflow */
	*count0 = RREG32_PCIE(smnPCIE_PERF_COUNT0_TXCLK) | (cnt0_of << 32);
	*count1 = RREG32_PCIE(smnPCIE_PERF_COUNT1_TXCLK) | (cnt1_of << 32);
}

static void vega20_get_pcie_usage(struct amdgpu_device *adev, uint64_t *count0,
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
	/* Reg 40 is # received msgs */
	/* Reg 108 is # of posted requests sent on VG20 */
	perfctr = REG_SET_FIELD(perfctr, PCIE_PERF_CNTL_TXCLK3,
				EVENT0_SEL, 40);
	perfctr = REG_SET_FIELD(perfctr, PCIE_PERF_CNTL_TXCLK3,
				EVENT1_SEL, 108);

	/* Write to enable desired perf counters */
	WREG32_PCIE(smnPCIE_PERF_CNTL_TXCLK3, perfctr);
	/* Zero out and enable the perf counters
	 * Write 0x5:
	 * Bit 0 = Start all counters(1)
	 * Bit 2 = Global counter reset enable(1)
	 */
	WREG32_PCIE(smnPCIE_PERF_COUNT_CNTL, 0x00000005);

	msleep(1000);

	/* Load the shadow and disable the perf counters
	 * Write 0x2:
	 * Bit 0 = Stop counters(0)
	 * Bit 1 = Load the shadow counters(1)
	 */
	WREG32_PCIE(smnPCIE_PERF_COUNT_CNTL, 0x00000002);

	/* Read register values to get any >32bit overflow */
	tmp = RREG32_PCIE(smnPCIE_PERF_CNTL_TXCLK3);
	cnt0_of = REG_GET_FIELD(tmp, PCIE_PERF_CNTL_TXCLK3, COUNTER0_UPPER);
	cnt1_of = REG_GET_FIELD(tmp, PCIE_PERF_CNTL_TXCLK3, COUNTER1_UPPER);

	/* Get the values and add the overflow */
	*count0 = RREG32_PCIE(smnPCIE_PERF_COUNT0_TXCLK3) | (cnt0_of << 32);
	*count1 = RREG32_PCIE(smnPCIE_PERF_COUNT1_TXCLK3) | (cnt1_of << 32);
}

static bool soc15_need_reset_on_init(struct amdgpu_device *adev)
{
	u32 sol_reg;

	/* Just return false for soc15 GPUs.  Reset does not seem to
	 * be necessary.
	 */
	if (!amdgpu_passthrough(adev))
		return false;

	if (adev->flags & AMD_IS_APU)
		return false;

	/* Check sOS sign of life register to confirm sys driver and sOS
	 * are already been loaded.
	 */
	sol_reg = RREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_81);
	if (sol_reg)
		return true;

	return false;
}

static uint64_t soc15_get_pcie_replay_count(struct amdgpu_device *adev)
{
	uint64_t nak_r, nak_g;

	/* Get the number of NAKs received and generated */
	nak_r = RREG32_PCIE(smnPCIE_RX_NUM_NAK);
	nak_g = RREG32_PCIE(smnPCIE_RX_NUM_NAK_GENERATED);

	/* Add the total number of NAKs, i.e the number of replays */
	return (nak_r + nak_g);
}

static void soc15_pre_asic_init(struct amdgpu_device *adev)
{
	gmc_v9_0_restore_registers(adev);
}

static const struct amdgpu_asic_funcs soc15_asic_funcs =
{
	.read_disabled_bios = &soc15_read_disabled_bios,
	.read_bios_from_rom = &soc15_read_bios_from_rom,
	.read_register = &soc15_read_register,
	.reset = &soc15_asic_reset,
	.reset_method = &soc15_asic_reset_method,
	.set_vga_state = &soc15_vga_set_state,
	.get_xclk = &soc15_get_xclk,
	.set_uvd_clocks = &soc15_set_uvd_clocks,
	.set_vce_clocks = &soc15_set_vce_clocks,
	.get_config_memsize = &soc15_get_config_memsize,
	.flush_hdp = &soc15_flush_hdp,
	.invalidate_hdp = &soc15_invalidate_hdp,
	.need_full_reset = &soc15_need_full_reset,
	.init_doorbell_index = &vega10_doorbell_index_init,
	.get_pcie_usage = &soc15_get_pcie_usage,
	.need_reset_on_init = &soc15_need_reset_on_init,
	.get_pcie_replay_count = &soc15_get_pcie_replay_count,
	.supports_baco = &soc15_supports_baco,
	.pre_asic_init = &soc15_pre_asic_init,
};

static const struct amdgpu_asic_funcs vega20_asic_funcs =
{
	.read_disabled_bios = &soc15_read_disabled_bios,
	.read_bios_from_rom = &soc15_read_bios_from_rom,
	.read_register = &soc15_read_register,
	.reset = &soc15_asic_reset,
	.reset_method = &soc15_asic_reset_method,
	.set_vga_state = &soc15_vga_set_state,
	.get_xclk = &soc15_get_xclk,
	.set_uvd_clocks = &soc15_set_uvd_clocks,
	.set_vce_clocks = &soc15_set_vce_clocks,
	.get_config_memsize = &soc15_get_config_memsize,
	.flush_hdp = &soc15_flush_hdp,
	.invalidate_hdp = &soc15_invalidate_hdp,
	.reset_hdp_ras_error_count = &vega20_reset_hdp_ras_error_count,
	.need_full_reset = &soc15_need_full_reset,
	.init_doorbell_index = &vega20_doorbell_index_init,
	.get_pcie_usage = &vega20_get_pcie_usage,
	.need_reset_on_init = &soc15_need_reset_on_init,
	.get_pcie_replay_count = &soc15_get_pcie_replay_count,
	.supports_baco = &soc15_supports_baco,
	.pre_asic_init = &soc15_pre_asic_init,
};

static int soc15_common_early_init(void *handle)
{
#define MMIO_REG_HOLE_OFFSET (0x80000 - PAGE_SIZE)
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	adev->rmmio_remap.reg_offset = MMIO_REG_HOLE_OFFSET;
	adev->rmmio_remap.bus_addr = adev->rmmio_base + MMIO_REG_HOLE_OFFSET;
	adev->smc_rreg = NULL;
	adev->smc_wreg = NULL;
	adev->pcie_rreg = &soc15_pcie_rreg;
	adev->pcie_wreg = &soc15_pcie_wreg;
	adev->pcie_rreg64 = &soc15_pcie_rreg64;
	adev->pcie_wreg64 = &soc15_pcie_wreg64;
	adev->uvd_ctx_rreg = &soc15_uvd_ctx_rreg;
	adev->uvd_ctx_wreg = &soc15_uvd_ctx_wreg;
	adev->didt_rreg = &soc15_didt_rreg;
	adev->didt_wreg = &soc15_didt_wreg;
	adev->gc_cac_rreg = &soc15_gc_cac_rreg;
	adev->gc_cac_wreg = &soc15_gc_cac_wreg;
	adev->se_cac_rreg = &soc15_se_cac_rreg;
	adev->se_cac_wreg = &soc15_se_cac_wreg;


	adev->external_rev_id = 0xFF;
	switch (adev->asic_type) {
	case CHIP_VEGA10:
		adev->asic_funcs = &soc15_asic_funcs;
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
	case CHIP_VEGA12:
		adev->asic_funcs = &soc15_asic_funcs;
		adev->cg_flags = AMD_CG_SUPPORT_GFX_MGCG |
			AMD_CG_SUPPORT_GFX_MGLS |
			AMD_CG_SUPPORT_GFX_CGCG |
			AMD_CG_SUPPORT_GFX_CGLS |
			AMD_CG_SUPPORT_GFX_3D_CGCG |
			AMD_CG_SUPPORT_GFX_3D_CGLS |
			AMD_CG_SUPPORT_GFX_CP_LS |
			AMD_CG_SUPPORT_MC_LS |
			AMD_CG_SUPPORT_MC_MGCG |
			AMD_CG_SUPPORT_SDMA_MGCG |
			AMD_CG_SUPPORT_SDMA_LS |
			AMD_CG_SUPPORT_BIF_MGCG |
			AMD_CG_SUPPORT_BIF_LS |
			AMD_CG_SUPPORT_HDP_MGCG |
			AMD_CG_SUPPORT_HDP_LS |
			AMD_CG_SUPPORT_ROM_MGCG |
			AMD_CG_SUPPORT_VCE_MGCG |
			AMD_CG_SUPPORT_UVD_MGCG;
		adev->pg_flags = 0;
		adev->external_rev_id = adev->rev_id + 0x14;
		break;
	case CHIP_VEGA20:
		adev->asic_funcs = &vega20_asic_funcs;
		adev->cg_flags = AMD_CG_SUPPORT_GFX_MGCG |
			AMD_CG_SUPPORT_GFX_MGLS |
			AMD_CG_SUPPORT_GFX_CGCG |
			AMD_CG_SUPPORT_GFX_CGLS |
			AMD_CG_SUPPORT_GFX_3D_CGCG |
			AMD_CG_SUPPORT_GFX_3D_CGLS |
			AMD_CG_SUPPORT_GFX_CP_LS |
			AMD_CG_SUPPORT_MC_LS |
			AMD_CG_SUPPORT_MC_MGCG |
			AMD_CG_SUPPORT_SDMA_MGCG |
			AMD_CG_SUPPORT_SDMA_LS |
			AMD_CG_SUPPORT_BIF_MGCG |
			AMD_CG_SUPPORT_BIF_LS |
			AMD_CG_SUPPORT_HDP_MGCG |
			AMD_CG_SUPPORT_HDP_LS |
			AMD_CG_SUPPORT_ROM_MGCG |
			AMD_CG_SUPPORT_VCE_MGCG |
			AMD_CG_SUPPORT_UVD_MGCG;
		adev->pg_flags = 0;
		adev->external_rev_id = adev->rev_id + 0x28;
		break;
	case CHIP_RAVEN:
		adev->asic_funcs = &soc15_asic_funcs;
		if (adev->pdev->device == 0x15dd)
			adev->apu_flags |= AMD_APU_IS_RAVEN;
		if (adev->pdev->device == 0x15d8)
			adev->apu_flags |= AMD_APU_IS_PICASSO;
		if (adev->rev_id >= 0x8)
			adev->apu_flags |= AMD_APU_IS_RAVEN2;

		if (adev->apu_flags & AMD_APU_IS_RAVEN2)
			adev->external_rev_id = adev->rev_id + 0x79;
		else if (adev->apu_flags & AMD_APU_IS_PICASSO)
			adev->external_rev_id = adev->rev_id + 0x41;
		else if (adev->rev_id == 1)
			adev->external_rev_id = adev->rev_id + 0x20;
		else
			adev->external_rev_id = adev->rev_id + 0x01;

		if (adev->apu_flags & AMD_APU_IS_RAVEN2) {
			adev->cg_flags = AMD_CG_SUPPORT_GFX_MGCG |
				AMD_CG_SUPPORT_GFX_MGLS |
				AMD_CG_SUPPORT_GFX_CP_LS |
				AMD_CG_SUPPORT_GFX_3D_CGCG |
				AMD_CG_SUPPORT_GFX_3D_CGLS |
				AMD_CG_SUPPORT_GFX_CGCG |
				AMD_CG_SUPPORT_GFX_CGLS |
				AMD_CG_SUPPORT_BIF_LS |
				AMD_CG_SUPPORT_HDP_LS |
				AMD_CG_SUPPORT_MC_MGCG |
				AMD_CG_SUPPORT_MC_LS |
				AMD_CG_SUPPORT_SDMA_MGCG |
				AMD_CG_SUPPORT_SDMA_LS |
				AMD_CG_SUPPORT_VCN_MGCG;

			adev->pg_flags = AMD_PG_SUPPORT_SDMA | AMD_PG_SUPPORT_VCN;
		} else if (adev->apu_flags & AMD_APU_IS_PICASSO) {
			adev->cg_flags = AMD_CG_SUPPORT_GFX_MGCG |
				AMD_CG_SUPPORT_GFX_MGLS |
				AMD_CG_SUPPORT_GFX_CP_LS |
				AMD_CG_SUPPORT_GFX_3D_CGCG |
				AMD_CG_SUPPORT_GFX_3D_CGLS |
				AMD_CG_SUPPORT_GFX_CGCG |
				AMD_CG_SUPPORT_GFX_CGLS |
				AMD_CG_SUPPORT_BIF_LS |
				AMD_CG_SUPPORT_HDP_LS |
				AMD_CG_SUPPORT_MC_MGCG |
				AMD_CG_SUPPORT_MC_LS |
				AMD_CG_SUPPORT_SDMA_MGCG |
				AMD_CG_SUPPORT_SDMA_LS;

			adev->pg_flags = AMD_PG_SUPPORT_SDMA |
				AMD_PG_SUPPORT_MMHUB |
				AMD_PG_SUPPORT_VCN;
		} else {
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
				AMD_CG_SUPPORT_MC_MGCG |
				AMD_CG_SUPPORT_MC_LS |
				AMD_CG_SUPPORT_SDMA_MGCG |
				AMD_CG_SUPPORT_SDMA_LS |
				AMD_CG_SUPPORT_VCN_MGCG;

			adev->pg_flags = AMD_PG_SUPPORT_SDMA | AMD_PG_SUPPORT_VCN;
		}
		break;
	case CHIP_ARCTURUS:
		adev->asic_funcs = &vega20_asic_funcs;
		adev->cg_flags = AMD_CG_SUPPORT_GFX_MGCG |
			AMD_CG_SUPPORT_GFX_MGLS |
			AMD_CG_SUPPORT_GFX_CGCG |
			AMD_CG_SUPPORT_GFX_CGLS |
			AMD_CG_SUPPORT_GFX_CP_LS |
			AMD_CG_SUPPORT_HDP_MGCG |
			AMD_CG_SUPPORT_HDP_LS |
			AMD_CG_SUPPORT_SDMA_MGCG |
			AMD_CG_SUPPORT_SDMA_LS |
			AMD_CG_SUPPORT_MC_MGCG |
			AMD_CG_SUPPORT_MC_LS |
			AMD_CG_SUPPORT_IH_CG |
			AMD_CG_SUPPORT_VCN_MGCG |
			AMD_CG_SUPPORT_JPEG_MGCG;
		adev->pg_flags = AMD_PG_SUPPORT_VCN | AMD_PG_SUPPORT_VCN_DPG;
		adev->external_rev_id = adev->rev_id + 0x32;
		break;
	case CHIP_RENOIR:
		adev->asic_funcs = &soc15_asic_funcs;
		if ((adev->pdev->device == 0x1636) ||
		    (adev->pdev->device == 0x164c))
			adev->apu_flags |= AMD_APU_IS_RENOIR;
		else
			adev->apu_flags |= AMD_APU_IS_GREEN_SARDINE;

		if (adev->apu_flags & AMD_APU_IS_RENOIR)
			adev->external_rev_id = adev->rev_id + 0x91;
		else
			adev->external_rev_id = adev->rev_id + 0xa1;
		adev->cg_flags = AMD_CG_SUPPORT_GFX_MGCG |
				 AMD_CG_SUPPORT_GFX_MGLS |
				 AMD_CG_SUPPORT_GFX_3D_CGCG |
				 AMD_CG_SUPPORT_GFX_3D_CGLS |
				 AMD_CG_SUPPORT_GFX_CGCG |
				 AMD_CG_SUPPORT_GFX_CGLS |
				 AMD_CG_SUPPORT_GFX_CP_LS |
				 AMD_CG_SUPPORT_MC_MGCG |
				 AMD_CG_SUPPORT_MC_LS |
				 AMD_CG_SUPPORT_SDMA_MGCG |
				 AMD_CG_SUPPORT_SDMA_LS |
				 AMD_CG_SUPPORT_BIF_LS |
				 AMD_CG_SUPPORT_HDP_LS |
				 AMD_CG_SUPPORT_VCN_MGCG |
				 AMD_CG_SUPPORT_JPEG_MGCG |
				 AMD_CG_SUPPORT_IH_CG |
				 AMD_CG_SUPPORT_ATHUB_LS |
				 AMD_CG_SUPPORT_ATHUB_MGCG |
				 AMD_CG_SUPPORT_DF_MGCG;
		adev->pg_flags = AMD_PG_SUPPORT_SDMA |
				 AMD_PG_SUPPORT_VCN |
				 AMD_PG_SUPPORT_JPEG |
				 AMD_PG_SUPPORT_VCN_DPG;
		break;
	default:
		/* FIXME: not supported yet */
		return -EINVAL;
	}

	if (amdgpu_sriov_vf(adev)) {
		amdgpu_virt_init_setting(adev);
		xgpu_ai_mailbox_set_irq_funcs(adev);
	}

	return 0;
}

static int soc15_common_late_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int r = 0;

	if (amdgpu_sriov_vf(adev))
		xgpu_ai_mailbox_get_irq(adev);

	if (adev->asic_funcs &&
	    adev->asic_funcs->reset_hdp_ras_error_count)
		adev->asic_funcs->reset_hdp_ras_error_count(adev);

	if (adev->nbio.funcs->ras_late_init)
		r = adev->nbio.funcs->ras_late_init(adev);

	return r;
}

static int soc15_common_sw_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (amdgpu_sriov_vf(adev))
		xgpu_ai_mailbox_add_irq_id(adev);

	adev->df.funcs->sw_init(adev);

	return 0;
}

static int soc15_common_sw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	amdgpu_nbio_ras_fini(adev);
	adev->df.funcs->sw_fini(adev);
	return 0;
}

static void soc15_doorbell_range_init(struct amdgpu_device *adev)
{
	int i;
	struct amdgpu_ring *ring;

	/* sdma/ih doorbell range are programed by hypervisor */
	if (!amdgpu_sriov_vf(adev)) {
		for (i = 0; i < adev->sdma.num_instances; i++) {
			ring = &adev->sdma.instance[i].ring;
			adev->nbio.funcs->sdma_doorbell_range(adev, i,
				ring->use_doorbell, ring->doorbell_index,
				adev->doorbell_index.sdma_doorbell_range);
		}

		adev->nbio.funcs->ih_doorbell_range(adev, adev->irq.ih.use_doorbell,
						adev->irq.ih.doorbell_index);
	}
}

static int soc15_common_hw_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	/* enable pcie gen2/3 link */
	soc15_pcie_gen3_enable(adev);
	/* enable aspm */
	soc15_program_aspm(adev);
	/* setup nbio registers */
	adev->nbio.funcs->init_registers(adev);
	/* remap HDP registers to a hole in mmio space,
	 * for the purpose of expose those registers
	 * to process space
	 */
	if (adev->nbio.funcs->remap_hdp_registers)
		adev->nbio.funcs->remap_hdp_registers(adev);

	/* enable the doorbell aperture */
	soc15_enable_doorbell_aperture(adev, true);
	/* HW doorbell routing policy: doorbell writing not
	 * in SDMA/IH/MM/ACV range will be routed to CP. So
	 * we need to init SDMA/IH/MM/ACV doorbell range prior
	 * to CP ip block init and ring test.
	 */
	soc15_doorbell_range_init(adev);

	return 0;
}

static int soc15_common_hw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	/* disable the doorbell aperture */
	soc15_enable_doorbell_aperture(adev, false);
	if (amdgpu_sriov_vf(adev))
		xgpu_ai_mailbox_put_irq(adev);

	if (adev->nbio.ras_if &&
	    amdgpu_ras_is_supported(adev, adev->nbio.ras_if->block)) {
		if (adev->nbio.funcs->init_ras_controller_interrupt)
			amdgpu_irq_put(adev, &adev->nbio.ras_controller_irq, 0);
		if (adev->nbio.funcs->init_ras_err_event_athub_interrupt)
			amdgpu_irq_put(adev, &adev->nbio.ras_err_event_athub_irq, 0);
	}

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

	if (adev->asic_type == CHIP_VEGA20 ||
		adev->asic_type == CHIP_ARCTURUS ||
		adev->asic_type == CHIP_RENOIR) {
		def = data = RREG32(SOC15_REG_OFFSET(HDP, 0, mmHDP_MEM_POWER_CTRL));

		if (enable && (adev->cg_flags & AMD_CG_SUPPORT_HDP_LS))
			data |= HDP_MEM_POWER_CTRL__IPH_MEM_POWER_CTRL_EN_MASK |
				HDP_MEM_POWER_CTRL__IPH_MEM_POWER_LS_EN_MASK |
				HDP_MEM_POWER_CTRL__RC_MEM_POWER_CTRL_EN_MASK |
				HDP_MEM_POWER_CTRL__RC_MEM_POWER_LS_EN_MASK;
		else
			data &= ~(HDP_MEM_POWER_CTRL__IPH_MEM_POWER_CTRL_EN_MASK |
				HDP_MEM_POWER_CTRL__IPH_MEM_POWER_LS_EN_MASK |
				HDP_MEM_POWER_CTRL__RC_MEM_POWER_CTRL_EN_MASK |
				HDP_MEM_POWER_CTRL__RC_MEM_POWER_LS_EN_MASK);

		if (def != data)
			WREG32(SOC15_REG_OFFSET(HDP, 0, mmHDP_MEM_POWER_CTRL), data);
	} else {
		def = data = RREG32(SOC15_REG_OFFSET(HDP, 0, mmHDP_MEM_POWER_LS));

		if (enable && (adev->cg_flags & AMD_CG_SUPPORT_HDP_LS))
			data |= HDP_MEM_POWER_LS__LS_ENABLE_MASK;
		else
			data &= ~HDP_MEM_POWER_LS__LS_ENABLE_MASK;

		if (def != data)
			WREG32(SOC15_REG_OFFSET(HDP, 0, mmHDP_MEM_POWER_LS), data);
	}
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

static int soc15_common_set_clockgating_state(void *handle,
					    enum amd_clockgating_state state)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (amdgpu_sriov_vf(adev))
		return 0;

	switch (adev->asic_type) {
	case CHIP_VEGA10:
	case CHIP_VEGA12:
	case CHIP_VEGA20:
		adev->nbio.funcs->update_medium_grain_clock_gating(adev,
				state == AMD_CG_STATE_GATE);
		adev->nbio.funcs->update_medium_grain_light_sleep(adev,
				state == AMD_CG_STATE_GATE);
		soc15_update_hdp_light_sleep(adev,
				state == AMD_CG_STATE_GATE);
		soc15_update_drm_clock_gating(adev,
				state == AMD_CG_STATE_GATE);
		soc15_update_drm_light_sleep(adev,
				state == AMD_CG_STATE_GATE);
		adev->smuio.funcs->update_rom_clock_gating(adev,
				state == AMD_CG_STATE_GATE);
		adev->df.funcs->update_medium_grain_clock_gating(adev,
				state == AMD_CG_STATE_GATE);
		break;
	case CHIP_RAVEN:
	case CHIP_RENOIR:
		adev->nbio.funcs->update_medium_grain_clock_gating(adev,
				state == AMD_CG_STATE_GATE);
		adev->nbio.funcs->update_medium_grain_light_sleep(adev,
				state == AMD_CG_STATE_GATE);
		soc15_update_hdp_light_sleep(adev,
				state == AMD_CG_STATE_GATE);
		soc15_update_drm_clock_gating(adev,
				state == AMD_CG_STATE_GATE);
		soc15_update_drm_light_sleep(adev,
				state == AMD_CG_STATE_GATE);
		break;
	case CHIP_ARCTURUS:
		soc15_update_hdp_light_sleep(adev,
				state == AMD_CG_STATE_GATE);
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

	adev->nbio.funcs->get_clockgating_state(adev, flags);

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
	adev->smuio.funcs->get_clock_gating_state(adev, flags);

	adev->df.funcs->get_clockgating_state(adev, flags);
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
